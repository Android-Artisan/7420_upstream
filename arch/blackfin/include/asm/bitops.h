#ifndef _BLACKFIN_BITOPS_H
#define _BLACKFIN_BITOPS_H

/*
 * Copyright 1992, Linus Torvalds.
 */

#include <linux/compiler.h>
#include <asm/byteorder.h>	/* swab32 */
#include <asm/system.h>		/* save_flags */

#ifdef __KERNEL__
/*
 * Copyright 2004-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _BLACKFIN_BITOPS_H
#define _BLACKFIN_BITOPS_H

#include <linux/compiler.h>

#include <asm-generic/bitops/__ffs.h>
#include <asm-generic/bitops/ffz.h>
#include <asm-generic/bitops/fls.h>
#include <asm-generic/bitops/__fls.h>
#include <asm-generic/bitops/fls64.h>
#include <asm-generic/bitops/find.h>

#ifndef _LINUX_BITOPS_H
#error only <linux/bitops.h> can be included directly
#endif

#include <asm-generic/bitops/ffs.h>
#include <asm-generic/bitops/__ffs.h>
#include <asm-generic/bitops/sched.h>
#include <asm-generic/bitops/ffz.h>

static __inline__ void set_bit(int nr, volatile unsigned long *addr)
{
	int *a = (int *)addr;
	int mask;
	unsigned long flags;

	a += nr >> 5;
	mask = 1 << (nr & 0x1f);
	local_irq_save(flags);
	*a |= mask;
	local_irq_restore(flags);
}

static __inline__ void __set_bit(int nr, volatile unsigned long *addr)
{
	int *a = (int *)addr;
	int mask;

	a += nr >> 5;
	mask = 1 << (nr & 0x1f);
	*a |= mask;
}

/*
 * clear_bit() doesn't provide any barrier for the compiler.
 */
#define smp_mb__before_clear_bit()	barrier()
#define smp_mb__after_clear_bit()	barrier()

static __inline__ void clear_bit(int nr, volatile unsigned long *addr)
{
	int *a = (int *)addr;
	int mask;
	unsigned long flags;
	a += nr >> 5;
	mask = 1 << (nr & 0x1f);
	local_irq_save(flags);
	*a &= ~mask;
	local_irq_restore(flags);
}

static __inline__ void __clear_bit(int nr, volatile unsigned long *addr)
{
	int *a = (int *)addr;
	int mask;

	a += nr >> 5;
	mask = 1 << (nr & 0x1f);
	*a &= ~mask;
}

static __inline__ void change_bit(int nr, volatile unsigned long *addr)
{
	int mask, flags;
	unsigned long *ADDR = (unsigned long *)addr;

	ADDR += nr >> 5;
	mask = 1 << (nr & 31);
	local_irq_save(flags);
	*ADDR ^= mask;
	local_irq_restore(flags);
}

static __inline__ void __change_bit(int nr, volatile unsigned long *addr)
{
	int mask;
	unsigned long *ADDR = (unsigned long *)addr;

	ADDR += nr >> 5;
	mask = 1 << (nr & 31);
	*ADDR ^= mask;
}

static __inline__ int test_and_set_bit(int nr, void *addr)
{
	int mask, retval;
	volatile unsigned int *a = (volatile unsigned int *)addr;
	unsigned long flags;

	a += nr >> 5;
	mask = 1 << (nr & 0x1f);
	local_irq_save(flags);
	retval = (mask & *a) != 0;
	*a |= mask;
	local_irq_restore(flags);

	return retval;
}

static __inline__ int __test_and_set_bit(int nr, volatile unsigned long *addr)
{
	int mask, retval;
	volatile unsigned int *a = (volatile unsigned int *)addr;

	a += nr >> 5;
	mask = 1 << (nr & 0x1f);
	retval = (mask & *a) != 0;
	*a |= mask;
	return retval;
}

static __inline__ int test_and_clear_bit(int nr, volatile unsigned long *addr)
{
	int mask, retval;
	volatile unsigned int *a = (volatile unsigned int *)addr;
	unsigned long flags;

	a += nr >> 5;
	mask = 1 << (nr & 0x1f);
	local_irq_save(flags);
	retval = (mask & *a) != 0;
	*a &= ~mask;
	local_irq_restore(flags);

	return retval;
}

static __inline__ int __test_and_clear_bit(int nr, volatile unsigned long *addr)
{
	int mask, retval;
	volatile unsigned int *a = (volatile unsigned int *)addr;

	a += nr >> 5;
	mask = 1 << (nr & 0x1f);
	retval = (mask & *a) != 0;
	*a &= ~mask;
	return retval;
}

static __inline__ int test_and_change_bit(int nr, volatile unsigned long *addr)
{
	int mask, retval;
	volatile unsigned int *a = (volatile unsigned int *)addr;
	unsigned long flags;

	a += nr >> 5;
	mask = 1 << (nr & 0x1f);
	local_irq_save(flags);
	retval = (mask & *a) != 0;
	*a ^= mask;
	local_irq_restore(flags);
	return retval;
}

static __inline__ int __test_and_change_bit(int nr,
					    volatile unsigned long *addr)
{
	int mask, retval;
	volatile unsigned int *a = (volatile unsigned int *)addr;

	a += nr >> 5;
	mask = 1 << (nr & 0x1f);
	retval = (mask & *a) != 0;
	*a ^= mask;
	return retval;
}

/*
 * This routine doesn't need to be atomic.
 */
static __inline__ int __constant_test_bit(int nr, const void *addr)
{
	return ((1UL << (nr & 31)) &
		(((const volatile unsigned int *)addr)[nr >> 5])) != 0;
}

static __inline__ int __test_bit(int nr, const void *addr)
{
	int *a = (int *)addr;
	int mask;

	a += nr >> 5;
	mask = 1 << (nr & 0x1f);
	return ((mask & *a) != 0);
}

#define test_bit(nr,addr) \
(__builtin_constant_p(nr) ? \
 __constant_test_bit((nr),(addr)) : \
 __test_bit((nr),(addr)))

#include <asm-generic/bitops/find.h>
#include <asm-generic/bitops/hweight.h>
#include <asm-generic/bitops/lock.h>

#include <asm-generic/bitops/ext2-atomic.h>
#include <asm-generic/bitops/ext2-non-atomic.h>

#include <asm-generic/bitops/minix.h>

#endif				/* __KERNEL__ */

#include <asm-generic/bitops/fls.h>
#include <asm-generic/bitops/fls64.h>
#include <asm-generic/bitops/sched.h>
#include <asm-generic/bitops/ffs.h>
#include <asm-generic/bitops/const_hweight.h>
#include <asm-generic/bitops/lock.h>

#include <asm-generic/bitops/ext2-atomic.h>

#include <asm/barrier.h>

#ifndef CONFIG_SMP
#include <linux/irqflags.h>
/*
 * clear_bit may not imply a memory barrier
 */
#include <asm-generic/bitops/atomic.h>
#include <asm-generic/bitops/non-atomic.h>
#else

#include <asm/byteorder.h>	/* swab32 */
#include <linux/linkage.h>

asmlinkage int __raw_bit_set_asm(volatile unsigned long *addr, int nr);

asmlinkage int __raw_bit_clear_asm(volatile unsigned long *addr, int nr);

asmlinkage int __raw_bit_toggle_asm(volatile unsigned long *addr, int nr);

asmlinkage int __raw_bit_test_set_asm(volatile unsigned long *addr, int nr);

asmlinkage int __raw_bit_test_clear_asm(volatile unsigned long *addr, int nr);

asmlinkage int __raw_bit_test_toggle_asm(volatile unsigned long *addr, int nr);

asmlinkage int __raw_bit_test_asm(const volatile unsigned long *addr, int nr);

static inline void set_bit(int nr, volatile unsigned long *addr)
{
	volatile unsigned long *a = addr + (nr >> 5);
	__raw_bit_set_asm(a, nr & 0x1f);
}

static inline void clear_bit(int nr, volatile unsigned long *addr)
{
	volatile unsigned long *a = addr + (nr >> 5);
	__raw_bit_clear_asm(a, nr & 0x1f);
}

static inline void change_bit(int nr, volatile unsigned long *addr)
{
	volatile unsigned long *a = addr + (nr >> 5);
	__raw_bit_toggle_asm(a, nr & 0x1f);
}

static inline int test_bit(int nr, const volatile unsigned long *addr)
{
	volatile const unsigned long *a = addr + (nr >> 5);
	return __raw_bit_test_asm(a, nr & 0x1f) != 0;
}

static inline int test_and_set_bit(int nr, volatile unsigned long *addr)
{
	volatile unsigned long *a = addr + (nr >> 5);
	return __raw_bit_test_set_asm(a, nr & 0x1f);
}

static inline int test_and_clear_bit(int nr, volatile unsigned long *addr)
{
	volatile unsigned long *a = addr + (nr >> 5);
	return __raw_bit_test_clear_asm(a, nr & 0x1f);
}

static inline int test_and_change_bit(int nr, volatile unsigned long *addr)
{
	volatile unsigned long *a = addr + (nr >> 5);
	return __raw_bit_test_toggle_asm(a, nr & 0x1f);
}

#define test_bit __skip_test_bit
#include <asm-generic/bitops/non-atomic.h>
#undef test_bit

#endif /* CONFIG_SMP */

/* Needs to be after test_bit and friends */
#include <asm-generic/bitops/le.h>

/*
 * hweightN: returns the hamming weight (i.e. the number
 * of bits set) of a N-bit word
 */

static inline unsigned int __arch_hweight32(unsigned int w)
{
	unsigned int res;

	__asm__ ("%0.l = ONES %1;"
		"%0 = %0.l (Z);"
		: "=d" (res) : "d" (w));
	return res;
}

static inline unsigned int __arch_hweight64(__u64 w)
{
	return __arch_hweight32((unsigned int)(w >> 32)) +
	       __arch_hweight32((unsigned int)w);
}

static inline unsigned int __arch_hweight16(unsigned int w)
{
	return __arch_hweight32(w & 0xffff);
}

static inline unsigned int __arch_hweight8(unsigned int w)
{
	return __arch_hweight32(w & 0xff);
}

#endif				/* _BLACKFIN_BITOPS_H */
