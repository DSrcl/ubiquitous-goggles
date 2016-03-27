#include <stdio.h>
#include <stdlib.h>

void add(int *dst, int src)
{
	*dst += src;
}

int main()
{
	int *sum = malloc(sizeof (int));
	*sum = 0;
	for (int i = 0; i < 10; i++) {
		add(sum, i);
	}
	printf("sum = %d\n", *sum);
}
