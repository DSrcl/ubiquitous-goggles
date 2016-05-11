#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <setjmp.h>
#include <assert.h>
#include <signal.h>

#include "regs.h"
#include "common.h"
#include "replay.h"

#define KILL '\0'

#define CANT_LOAD_LIB "unable to open library"
#define CANT_LOAD_FUNC "unable to load function from library"

#define OUT_FILENAME "worker-data.txt"
#define JB_FILENAME "jmp_buf.txt"
#define MAXFD 256
#define MAX_CLIENT 10
#define MAX_WORKER 32
#define MISMATCH_PENALTY 1

#define X86_64

#ifdef X86_64
#define GET_STACKBOUND(BOUND) asm("movq %%rbp, %0" : "=r"(BOUND))
#endif

// target reg_data
extern uint8_t _ug_target_reg_data[];
// target reg_info
extern struct reg_info _ug_reg_info[];
extern size_t _ug_num_regs;
extern size_t _ug_num_output_regs;
// dump target registers
extern void dump_registers(void);

void _server_init() __attribute__((constructor));

int max_client;

int is_parent = 1;

int crash_signal;

static sigjmp_buf jb;

void *_server_stack_top, *_server_heap_bottom;

void *frame_begin;
size_t frame_size;

static inline struct response *make_error(char *msg) {
  struct response *resp = malloc(sizeof(struct response));
  resp->success = 0;
  strcpy(resp->msg, msg);
  return resp;
}

static inline struct response *make_report(size_t reg_dist, size_t stack_dist,
                                           size_t heap_dist, int crash_signal) {
  struct response *resp = malloc(sizeof(struct response));
  resp->success = 1;
  resp->stack_dist = stack_dist;
  resp->heap_dist = heap_dist;
  resp->reg_dist = reg_dist;
  resp->signal = crash_signal;
  return resp;
}

// placeholder for calling target function to construct the reference output
// state
uint32_t _stub_target_call(uint32_t (*)());

// placeholder for testing a rewrite
uint32_t _stub_rewrite_call(uint32_t (*)());

// send response to the client and kill current process
static inline void respond(int fd, struct response *resp) {
  write(fd, resp, sizeof(struct response));
  _Exit(0);
}

static inline void dump_worker_data(const char *sock_path, void *frame_begin,
                                    size_t frame_size) {
  FILE *out_file = fopen(OUT_FILENAME, "a");
  fprintf(out_file, "%s,%zu,%zu\n", sock_path, (size_t)frame_begin, frame_size);
  fclose(out_file);
}

size_t get_byte_dist(uint8_t a, uint8_t b) {
  return __builtin_popcount((unsigned int)(a ^ b));
}

size_t get_mem_dist(void *a, void *b, size_t size) __attribute__((noinline));
size_t get_mem_dist(void *a, void *b, size_t size) {
  size_t i;
  size_t dist = 0;
  for (i = 0; i < size; i++) {
    dist += get_byte_dist(((uint8_t *)a)[i], ((uint8_t *)b)[i]);
  }

  return dist;

}

size_t get_reg_dist(struct reg_info info[], uint8_t *a, uint8_t *b, int ai,
                    int bi) __attribute__((noinline));
// compare ai'th register at `a` and bi'th register at `b`
size_t get_reg_dist(struct reg_info info[], uint8_t *a, uint8_t *b, int ai,
                    int bi){
  assert(info[ai].size == info[bi].size);
  uint8_t *a_start = a + info[ai].offset, *b_start = b + info[bi].offset;
  size_t size = info[ai].size;

  size_t i;
  size_t dist = 0;
  for (i = 0; i < size; i++) {
    dist += get_byte_dist(a_start[i], b_start[i]);
  }

  return dist;
}

int cli_fd;

void handle_signal(int signo, siginfo_t *siginfo, void *context) {
  crash_signal = signo;
  mprotect(frame_begin, frame_size, PROT_READ | PROT_WRITE);
  siglongjmp(jb, 42);
}

void register_signal_handler() {
  static char handler_stack[SIGSTKSZ];
  stack_t ss;
  ss.ss_size = SIGSTKSZ;
  ss.ss_sp = handler_stack;
  struct sigaction sa;
  sa.sa_sigaction = handle_signal;
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
  sigaltstack(&ss, NULL);
  sigfillset(&sa.sa_mask);
  if (sigaction(SIGSEGV, &sa, NULL))
    exit(1);
  if (sigaction(SIGBUS, &sa, NULL))
    exit(1);
  if (sigaction(SIGILL, &sa, NULL))
    exit(1);
  if (sigaction(SIGFPE, &sa, NULL))
    exit(1);
}

uint32_t spawn_impl(uint32_t (*orig_func)(void), char *funcname) {
  void *fp = __builtin_frame_address(0);

  // assuming compiler can't constprop getpagesize
  int page_size = getpagesize();
  char *stack_boundary = alloca(page_size);
  frame_begin =
      (char *)(((size_t)stack_boundary + page_size - 1) & ~(page_size - 1));
  frame_size = fp - frame_begin;

  void *stack_bottom, *heap_top;
  stack_bottom = fp;
  heap_top = sbrk(0);

  size_t stack_size = _server_stack_top - stack_bottom,
         heap_size = heap_top - _server_heap_bottom, reg_buf_size = 0;
  if (_ug_num_regs > 0) {
    struct reg_info *last_reg = &_ug_reg_info[_ug_num_regs - 1];
    reg_buf_size = last_reg->offset + last_reg->size;
  }

  // layout of `shared_mem` = |sem| stack | heap | reg buf|
  void *shared_mem =
      mmap(NULL, heap_size + stack_size + sizeof(sem_t) + reg_buf_size,
           PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);

  assert(shared_mem && "failed to mmap");
  sem_t *sem = shared_mem;
  void *target_stack = shared_mem + sizeof(sem_t),
       *target_heap = target_stack + stack_size;

  uint8_t *target_reg_data = target_heap + heap_size;

  sem_init(sem, 1, 0);

  static int invo = 0;

  int can_spawn = 0;
  invo++;
  can_spawn = is_parent && invo <= MAX_WORKER;

  char msg[LIBPATH_MAX_LEN];
  memset(msg, 0, sizeof msg);

  char sock_path[100] = "/tmp/tuning-XXXXXX";
  if (can_spawn) {
    mkdtemp(sock_path);
    strcat(sock_path, "/socket");
  }

  if (can_spawn && fork() == 0) { // body of worker process
    register_signal_handler();

    sem_wait(sem);
    is_parent = 0;

    struct sockaddr_un addr;
    int sockfd;

    if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
      exit(-1);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path, (sizeof(addr.sun_path)) - 1);

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
      exit(-1);
    }

    if (listen(sockfd, MAX_CLIENT) == -1) {
      exit(-1);
    }

    if ((cli_fd = accept(sockfd, NULL, NULL)) == -1) {
      exit(-1);
    }

    for (;;) {
      bzero(msg, sizeof msg);
      if (read(cli_fd, msg, LIBPATH_MAX_LEN) <= 1) {
        break;
      }

      pid_t pid = fork();
        static int itr;
        itr++;
      if (pid == 0) {
        // lookup the function from shared library
        void *lib = dlopen(msg, RTLD_NOW);
        if (!lib)
          respond(cli_fd, make_error(CANT_LOAD_LIB));

        uint32_t (*rewrite)(void) = dlsym(lib, funcname);
        if (!rewrite)
          respond(cli_fd, make_error(CANT_LOAD_FUNC));

        uint8_t *rewrite_reg_data = dlsym(lib, "_ug_rewrite_reg_data");
        if (!rewrite_reg_data)
          respond(cli_fd, make_error("can't load _ug_rewrite_reg_data"));

        int ret;
        if ((ret = sigsetjmp(jb, 1)) == 0) {
          // run the function
          _stub_rewrite_call(rewrite);
        }

        size_t stack_dist =
                   get_mem_dist(stack_bottom, target_stack, stack_size),
               heap_dist =
                   get_mem_dist(_server_heap_bottom, target_heap, heap_size),
               reg_dist = 0;

        int i, j;
        for (i = 0; i < _ug_num_output_regs; i++) {
          size_t dist = get_reg_dist(_ug_reg_info, rewrite_reg_data, target_reg_data, i, i); 
          if (dist == 0) continue;
          
          // do relax comparison 
          int mismatched = 0;
          for (j = _ug_num_output_regs; j < _ug_num_regs; j++) {
            if (_ug_reg_info[i].regclass == _ug_reg_info[j].regclass) {
              if (get_reg_dist(_ug_reg_info, rewrite_reg_data, target_reg_data, j, i) == 0) {
                reg_dist += MISMATCH_PENALTY;
                mismatched = 1;
                break;
              }
            }
          }
          if (!mismatched) reg_dist += dist;
        }

        respond(cli_fd, make_report(reg_dist, stack_dist, heap_dist, crash_signal));
      }

      int retval;
      waitpid(pid, &retval, 0);
      if (WIFSIGNALED(retval)) {
        struct response *resp = make_report(0, 0, 0, WTERMSIG(retval));
        write(cli_fd, resp, sizeof(struct response));
        free(resp);
      }
    }

    unlink(sock_path);
    exit(0);
  } else { // body of parent process
    if (can_spawn) {
      dump_worker_data(sock_path, frame_begin, frame_size);
    }

    // this will finally be transformed into `int ret = orig_func(...)`
    int ret = _stub_target_call(orig_func);
    dump_registers();

    // copy stack[bottom:top] to the shared memory
    memcpy(target_stack, stack_bottom, stack_size);

    if (heap_size) {
      // copy heap[bottom:top] to shared memory
      memcpy(target_heap, _server_heap_bottom, heap_size);
    }

    // copy reg buffer  to shared memory
    if (_ug_num_regs > 0) {
      memcpy(target_reg_data, _ug_target_reg_data, reg_buf_size);
    }

    // let go of the child process
    sem_post(sem);

    return ret;
  }
}

uint32_t _server_spawn_worker(uint32_t (*orig_func)(void), char *funcname) {
  // make sure the spawn function's frame uses different pages than the
  // testcases'
  volatile char placeholder[getpagesize()];
  // make sure the compiler doesn't remove placeholder's allocation
  placeholder[42] = 42;

  return spawn_impl(orig_func, funcname);
}

void _server_init() {
  _server_heap_bottom = sbrk(0);

  remove(OUT_FILENAME);
  FILE *buf_out = fopen(JB_FILENAME, "w");
  fprintf(buf_out, "%ld\n", (long)&jb);
  fclose(buf_out);

  daemon(1, 0);
}
