#include <stdio.h>

unsigned char data[1000];

void foo(int x);


char add()
{
	data[42] = 10;
	return 4;
}

void foo()
{
	printf("%c\n", add());
}
