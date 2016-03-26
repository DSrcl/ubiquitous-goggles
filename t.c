#include <stdio.h>

void *top, *bottom;

void foo()
{
	asm("movq %%rbp, %0" :"=r"(bottom));
	printf("!!! %d\n", bottom < top);
	printf("stack size = %zu\n", top-bottom);
}

int main()
{
	asm("movq %%rbp, %0" :"=r"(top));
	int x[1000];
	foo();
}
