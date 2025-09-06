#include <linux/types.h>
#include <linux/errno.h>
#include <asm/uaccess.h>

#include "soft-fp.h"
#include <asm/sfp-machine.h>
#include <math-emu/soft-fp.h>

int
mtfsf(unsigned int FM, u32 *frB)
{
	u32 mask;

	if (FM == 0)
		return 0;

	if (FM == 0xff)
		mask = 0x9fffffff;
	else {
		mask = 0;
		if (FM & (1 << 0))
			mask |= 0x90000000;
		if (FM & (1 << 1))
			mask |= 0x0f000000;
		if (FM & (1 << 2))
			mask |= 0x00f00000;
		if (FM & (1 << 3))
			mask |= 0x000f0000;
		if (FM & (1 << 4))
			mask |= 0x0000f000;
		if (FM & (1 << 5))
			mask |= 0x00000f00;
		if (FM & (1 << 6))
			mask |= 0x000000f0;
		if (FM & (1 << 7))
			mask |= 0x0000000f;
	}

	__FPU_FPSCR &= ~(mask);
	__FPU_FPSCR |= (frB[1] & mask);
	u32 fpscr;

	if (likely(FM == 1))
		mask = 0x0f;
	else if (likely(FM == 0xff))
		mask = ~0;
	else {
		mask = ((FM & 1) |
				((FM << 3) & 0x10) |
				((FM << 6) & 0x100) |
				((FM << 9) & 0x1000) |
				((FM << 12) & 0x10000) |
				((FM << 15) & 0x100000) |
				((FM << 18) & 0x1000000) |
				((FM << 21) & 0x10000000)) * 15;
	}

	fpscr = ((__FPU_FPSCR & ~mask) | (frB[1] & mask)) &
		~(FPSCR_VX | FPSCR_FEX | 0x800);

	if (fpscr & (FPSCR_VXSNAN | FPSCR_VXISI | FPSCR_VXIDI |
		     FPSCR_VXZDZ | FPSCR_VXIMZ | FPSCR_VXVC |
		     FPSCR_VXSOFT | FPSCR_VXSQRT | FPSCR_VXCVI))
		fpscr |= FPSCR_VX;

	/* The bit order of exception enables and exception status
	 * is the same. Simply shift and mask to check for enabled
	 * exceptions.
	 */
	if (fpscr & (fpscr >> 22) &  0xf8)
		fpscr |= FPSCR_FEX;

	__FPU_FPSCR = fpscr;

#ifdef DEBUG
	printk("%s: %02x %p: %08lx\n", __func__, FM, frB, __FPU_FPSCR);
#endif

	return 0;
}
