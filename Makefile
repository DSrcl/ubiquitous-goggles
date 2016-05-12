LIBS = support irreader ipo bitwriter bitreader codegen mc all-targets
CONFIG = ~/workspace/llvm-3.7.1.obj/bin/llvm-config
#CONFIG = ~/workspace/llvm-fast/bin/llvm-config
LDFLAGS = $(shell $(CONFIG) --ldflags --system-libs --libs $(LIBS) | sed 's/-DNDEBUG//g')
CXXFLAGS = $(shell $(CONFIG) --cxxflags | sed 's/-DNDEBUG//g') -g
CPPFLAGS += -MMD -MP
CXX = clang++

.PHONY: all clean

OBJS = mf_compiler.o mf_instrument.o transform.o replay_cli.o search.o
TOOLS = create-server ug
DEPS = $(OBJS:.o=.d)
-include $(DEPS)

all: $(TOOLS) server.bc malloc.o

ug: $(OBJS)

server.bc: server.c common.h replay.h regs.h
	clang -c -O3 -emit-llvm -o $@ $<

malloc.o: malloc.c
	# force malloc to use sbrk only
	cc $< -c -o $@ -DHAVE_MMAP=0

clean:
	rm -f $(TOOLS) $(OBJS) $(DEPS) worker-data.txt jmp_buf.txt
