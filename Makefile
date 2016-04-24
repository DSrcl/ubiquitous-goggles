#LIBS = support irreader ipo bitwriter bitreader codegen mc
#CONFIG = ~/workspace/llvm-3.7.1.obj/bin/llvm-config
CONFIG = ~/workspace/llvm-fast/bin/llvm-config
LDFLAGS = $(shell $(CONFIG) --ldflags --system-libs --libs $(LIBS) | sed 's/-DNDEBUG//g')
CXXFLAGS = $(shell $(CONFIG) --cxxflags | sed 's/-DNDEBUG//g') -g
CXX = clang++

.PHONY: all clean

OBJS = mf_compiler.o mf_instrument.o transform.o
BC = server.bc
TESTS = mf_compiler_test 
TOOLS = create-server replay-cli ug

all: $(TOOLS) $(OBJS) $(TESTS) $(BC)

ug: $(OBJS)

server.bc: server.c common.h
	clang -c -O3 -emit-llvm -o $@ $<

mf_compiler_test: mf_compiler.o mf_instrument.o mf_compiler_test.o transform.o
	$(CXX) $^ $(LDFLAGS) -o $@ -g

malloc.o: malloc.c
	# force malloc to use sbrk only
	cc $< -c -o $@ -DHAVE_MMAP=0

replay-server.o: $(TESTCASE) server.bc create-server
	llvm-link $(TESTCASE) server.bc -o - | ./create-server - -o - -f$(FUNC) | llc -filetype=obj -o $@

replay-server: replay-server.o malloc.o
	cc $^ -o $@

replay-cli: replay-cli.cpp
	$(CXX) $^ -o $@ -std=c++11

clean:
	rm -f $(TOOLS) $(OBJS) $(TESTS)
