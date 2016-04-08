#include <setjmp.h>

void add(int *dst, int src)
{
	*dst += src;
	longjmp((jmp_buf *)0x1043601b0, 13);
}
