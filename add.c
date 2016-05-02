#include <stdio.h>

int add(int a, int b)
{
  return a + b;
}

int main()
{
	int sum = 0;
	for (int i = 0; i < 10; i++) {
		sum += add(sum, i);
	}
	printf("sum = %d\n", sum);
}
