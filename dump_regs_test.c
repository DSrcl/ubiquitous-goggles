#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include "regs.h"

extern uint8_t _ug_reg_data[];
extern struct reg_info _ug_reg_info[];
extern void dump_registers();

void check_regs()
{
	int esi = *(int *)_ug_reg_data;
	assert(esi == 42);
	printf("%%esi: %d\n", esi);
}

int main()
{
	asm("movl $42, %esi");
	dump_registers();
	check_regs();
}
