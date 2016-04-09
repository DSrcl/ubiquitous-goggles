#include <stdint.h>
#include <stdio.h>

struct reg_info { size_t offset, size; };

extern struct reg_info _ug_reg_info[];
extern size_t _ug_num_regs;

int main()
{
	size_t i;
	for (i = 0; i < _ug_num_regs; i++) {
		struct reg_info *info = &_ug_reg_info[i];
		printf("offset=%zu, size=%zu\n", info->offset, info->size);
	}
}
