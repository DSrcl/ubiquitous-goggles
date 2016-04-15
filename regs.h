#ifndef _REGS_H_
#define _REGS_H_

struct reg_info {
	size_t offset, size;
};

size_t get_reg_dist(struct reg_info info[], uint8_t *a, uint8_t *b, int ai, int bi);

#endif
