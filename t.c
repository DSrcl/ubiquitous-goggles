unsigned char data[1000];

void foo(int x);


void add()
{
	*(int *)0xdeadbeef = 10;
}
