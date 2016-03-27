#LIBS = support irreader ipo bitwriter bitreader codegen mc
LDFLAGS = $(shell llvm-config --ldflags --system-libs --libs $(LIBS) | sed 's/-DNDEBUG//g')
CXXFLAGS = $(shell llvm-config --cxxflags | sed 's/-DNDEBUG//g') -g
CXX = clang++

.PHONY: all clean

all: server.bc create-server mf_compiler.o mf_compiler_test replay-cli

server.bc: server.c common.h
	clang -c -O3 -emit-llvm -o $@ $<

mf_compiler_test: mf_compiler.o mf_compiler_test.o
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
	rm -f server.bc create-server worker-data.txt *.o
