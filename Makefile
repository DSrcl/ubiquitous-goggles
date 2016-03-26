#LIBS = support irreader ipo bitwriter bitreader codegen mc
LIBS = all
LDFLAGS = $(shell llvm-config --ldflags --system-libs --libs $(LIBS) | sed 's/-DNDEBUG//g')
CXXFLAGS = $(shell llvm-config --cxxflags | sed 's/-DNDEBUG//g')
CXX = clang++

.PHONY: all clean

all: server.bc create-server mf_compiler.o mf_compiler_test

server.bc: server.c common.h
	clang -c -O3 -emit-llvm -o $@ $<

mf_compiler_test: mf_compiler.o mf_compiler_test.o
	$(CXX) $(LDFLAGS) -o $@ $^

clean:
	rm -f server.bc create-server worker-data.txt
