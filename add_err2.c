#include <stdio.h>

void add(int *dst, int src)
{
	*dst = (*dst + src) ^ 1;
}

int main()
{
	int sum = 0;
	for (int i = 0; i < 10; i++) {
		add(&sum, i);
	}
	printf("sum = %d\n", sum);
}
