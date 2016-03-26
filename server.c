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
#include <assert.h>

#include "common.h"
#include "replay.h"

#define KILL '\0'

#define CANT_LOAD_LIB "unable to open library"
#define CANT_LOAD_FUNC "unable to load function from library"

#define OUT_FILENAME "worker-data.txt"
#define MAXFD 256
#define DEFAULT_MAX_CLIENT 4
#define MAX_WORKER 32

#define X86_64

#ifdef X86_64
#define GET_STACKBOUND(BOUND) asm("movq %%rbp, %0" :"=r"(BOUND))
#endif

void _server_init() __attribute__ ((constructor));

int max_client; 

int is_parent = 1;

void *_server_stack_top;

static inline struct response *make_error(char *msg)
{ 
	struct response *resp = malloc(sizeof (struct response));
	resp->success = 0;
	strcpy(resp->msg, msg);
	return resp;
}

static inline struct response *make_report(size_t dist)
{ 
	struct response *resp = malloc(sizeof (struct response));
	resp->success = 1;
	resp->dist = dist;
	return resp;
}

// placeholder for calling target function to construct the reference output state
uint32_t _stub_target_call(uint32_t (*)());

// placeholder for testing a rewrite
uint32_t _stub_rewrite_call(uint32_t (*)());

// send response to the client and kill current process
static inline void *respond(int fd, struct response *resp)
{ 
    write(fd, resp, sizeof (struct response));
    free(resp);
    exit(0);
}

static inline void dump_worker_data(const char *sock_path)
{ 
    FILE *out_file = fopen(OUT_FILENAME, "a");
    fprintf(out_file, "%s\n", sock_path);
	fclose(out_file);
} 

size_t get_byte_dist(uint8_t a, uint8_t b)
{
	return __builtin_popcount((unsigned int)(a ^ b));
}

size_t get_mem_dist(void *a, void *b, size_t size)
{
	size_t i;
	size_t dist = 0;
	for (i = 0; i < size; i++) {
		dist += get_byte_dist(((uint8_t *)a)[i], ((uint8_t *)b)[i]);
	}

	return dist;
}

uint32_t _server_spawn_worker(uint32_t (*orig_func)(), char *funcname)
{ 
	void *stack_bottom;
	GET_STACKBOUND(stack_bottom);

	size_t stack_size = _server_stack_top - stack_bottom;
	void *shared_mem = mmap(NULL, stack_size+sizeof (sem_t),
			PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANON, -1, 0);
	assert(shared_mem && "failed to mmap");
	sem_t *sem = shared_mem;
	void *target_stack = shared_mem + sizeof (sem_t);
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
    
    if (can_spawn && fork()) { // body of worker process
		sem_wait(sem);
        is_parent = 0;

        daemon(1, 0);
        struct sockaddr_un addr;
        int sockfd;

        if ((sockfd=socket(AF_UNIX, SOCK_STREAM, 0)) == - 1) {
            exit(-1);
        }

        memset(&addr, 0, sizeof (addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, sock_path, (sizeof (addr.sun_path))-1);

        if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
            exit(-1);
        }

        if (listen(sockfd, max_client) == -1) {
            exit(-1);
        }

        for (;;) {
            int cli_fd;
            if ((cli_fd=accept(sockfd, NULL, NULL)) == -1) {
                continue;
            }

            if (read(cli_fd, msg, LIBPATH_MAX_LEN) <= 0) {
				continue;
			}

            // read control byte and see if needs to kill current worker
            if (!msg[0]) {
                close(cli_fd);
                break;
            }

            if (fork()) { 
                // lookup the function from shared library
                void *lib = dlopen(msg, RTLD_NOW);
                if (!lib) respond(cli_fd, make_error(CANT_LOAD_LIB));

                uint32_t (*rewrite)() = dlsym(lib, funcname); 
                if (!rewrite) respond(cli_fd, make_error(CANT_LOAD_FUNC));

                // run the function
                _stub_rewrite_call(rewrite); 

				size_t dist = get_mem_dist(stack_bottom, target_stack, stack_size);
				
                respond(cli_fd, make_report(dist));
            }

            memset(msg, 0, sizeof msg);
            close(cli_fd);
        }
        unlink(sock_path);
        exit(0);
    } else { // body of parent process 
        if (can_spawn) {
            dump_worker_data(sock_path);
        }

		// this will finally be transformed into `int ret = orig_func(...)`
        int ret = _stub_target_call(orig_func);
		
		// copy mem[bottom:top] to the shared memory
		memcpy(target_stack, stack_bottom, stack_size);

		// let go of the child process
		sem_post(sem);

		return ret;
    }
}

void _server_init()
{
    const char *max_client_str = getenv("MAX_CLIENT");
    if (!max_client_str) {
        max_client = DEFAULT_MAX_CLIENT;
    } else { 
        max_client = atoi(max_client_str);
        if (!max_client) {
            fprintf(stderr, "invalid $MAX_CLIENT\n");
            exit(-1);
        }
    }

	remove(OUT_FILENAME);
}
