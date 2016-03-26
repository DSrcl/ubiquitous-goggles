LIBS = support irreader ipo bitwriter bitreader codegen
LDFLAGS = $(shell llvm-config --ldflags --system-libs --libs $(LIBS) | sed 's/-DNDEBUG//g')
CXXFLAGS = $(shell llvm-config --cxxflags | sed 's/-DNDEBUG//g') -g
CXX = clang++

.PHONY: all clean

all: server.bc create-server

server.bc: server.c common.h
	clang -c -O3 -emit-llvm -o $@ $<

create-server: create-server.cpp

clean:
	rm -f server.bc create-server worker-data.txt
