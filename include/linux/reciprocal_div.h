#ifndef _LINUX_RECIPROCAL_DIV_H
#define _LINUX_RECIPROCAL_DIV_H

#include <linux/types.h>

/*
 * This file describes reciprocical division.
 *
 * This optimizes the (A/B) problem, when A and B are two u32
 * and B is a known value (but not known at compile time)
 *
 * The math principle used is :
 *   Let RECIPROCAL_VALUE(B) be (((1LL << 32) + (B - 1))/ B)
 *   Then A / B = (u32)(((u64)(A) * (R)) >> 32)
 *
 * This replaces a divide by a multiply (and a shift), and
 * is generally less expensive in CPU cycles.
 */

/*
 * Computes the reciprocal value (R) for the value B of the divisor.
 * Should not be called before each reciprocal_divide(),
 * or else the performance is slower than a normal divide.
 */
extern u32 reciprocal_value(u32 B);


static inline u32 reciprocal_divide(u32 A, u32 R)
{
	return (u32)(((u64)A * R) >> 32);
}
#endif
 * This algorithm is based on the paper "Division by Invariant
 * Integers Using Multiplication" by Torbjörn Granlund and Peter
 * L. Montgomery.
 *
 * The assembler implementation from Agner Fog, which this code is
 * based on, can be found here:
 * http://www.agner.org/optimize/asmlib.zip
 *
 * This optimization for A/B is helpful if the divisor B is mostly
 * runtime invariant. The reciprocal of B is calculated in the
 * slow-path with reciprocal_value(). The fast-path can then just use
 * a much faster multiplication operation with a variable dividend A
 * to calculate the division A/B.
 */

struct reciprocal_value {
	u32 m;
	u8 sh1, sh2;
};

struct reciprocal_value reciprocal_value(u32 d);

static inline u32 reciprocal_divide(u32 a, struct reciprocal_value R)
{
	u32 t = (u32)(((u64)a * R.m) >> 32);
	return (t + ((a - t) >> R.sh1)) >> R.sh2;
}

#endif /* _LINUX_RECIPROCAL_DIV_H */
