### How to build this
1. get a copy of our modified llvm from [here](https://github.com/DSrcl/expert-disco) and build it
2. modify `Makefile` to set `CONFIG` to `/path-to-custom-built-llvm/bin/llvm-config`
3. do `make`

### How to use
1. compile testcase to llvm bitcode file. For example, you can do this
```
clang -c -emit-llvm add.c -o testcase
```
2. run the superoptimizer like this
```
# in this case `add` is name of the function you would like to optimize
./ug -fadd testcase.bc
```
