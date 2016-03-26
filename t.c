#include <stdio.h>

int main()
{
	unsigned aa = 0, bb = 0;
	unsigned char a = 42, b;
	b = ~a;

	unsigned int x = (a ^ b) << 24;
	unsigned int y = (a ^ b);
	printf("%d %d\n", __builtin_popcount(x), __builtin_popcount(y));
}
