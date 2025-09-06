#ifndef __ASM_SH_ATOMIC_LLSC_H
#define __ASM_SH_ATOMIC_LLSC_H

/*
 * To get proper branch prediction for the main line, we must branch
 * forward to code at the end of this object's .text section, then
 * branch back to restart the operation.
 */
static inline void atomic_add(int i, atomic_t *v)
{
	unsigned long tmp;

	__asm__ __volatile__ (
"1:	movli.l @%2, %0		! atomic_add	\n"
"	add	%1, %0				\n"
"	movco.l	%0, @%2				\n"
"	bf	1b				\n"
	: "=&z" (tmp)
	: "r" (i), "r" (&v->counter)
	: "t");
}

static inline void atomic_sub(int i, atomic_t *v)
{
	unsigned long tmp;

	__asm__ __volatile__ (
"1:	movli.l @%2, %0		! atomic_sub	\n"
"	sub	%1, %0				\n"
"	movco.l	%0, @%2				\n"
"	bf	1b				\n"
	: "=&z" (tmp)
	: "r" (i), "r" (&v->counter)
	: "t");
}

/*
 * SH-4A note:
 *
 * We basically get atomic_xxx_return() for free compared with
 * atomic_xxx(). movli.l/movco.l require r0 due to the instruction
 * encoding, so the retval is automatically set without having to
 * do any special work.
 */
static inline int atomic_add_return(int i, atomic_t *v)
{
	unsigned long temp;

	__asm__ __volatile__ (
"1:	movli.l @%2, %0		! atomic_add_return	\n"
"	add	%1, %0					\n"
"	movco.l	%0, @%2					\n"
"	bf	1b					\n"
"	synco						\n"
	: "=&z" (temp)
	: "r" (i), "r" (&v->counter)
	: "t");

	return temp;
}

static inline int atomic_sub_return(int i, atomic_t *v)
{
	unsigned long temp;

	__asm__ __volatile__ (
"1:	movli.l @%2, %0		! atomic_sub_return	\n"
"	sub	%1, %0					\n"
"	movco.l	%0, @%2					\n"
"	bf	1b					\n"
"	synco						\n"
	: "=&z" (temp)
	: "r" (i), "r" (&v->counter)
	: "t");

	return temp;
}

static inline void atomic_clear_mask(unsigned int mask, atomic_t *v)
{
	unsigned long tmp;

	__asm__ __volatile__ (
"1:	movli.l @%2, %0		! atomic_clear_mask	\n"
"	and	%1, %0					\n"
"	movco.l	%0, @%2					\n"
"	bf	1b					\n"
	: "=&z" (tmp)
	: "r" (~mask), "r" (&v->counter)
	: "t");
}

static inline void atomic_set_mask(unsigned int mask, atomic_t *v)
{
	unsigned long tmp;

	__asm__ __volatile__ (
"1:	movli.l @%2, %0		! atomic_set_mask	\n"
"	or	%1, %0					\n"
"	movco.l	%0, @%2					\n"
"	bf	1b					\n"
	: "=&z" (tmp)
	: "r" (mask), "r" (&v->counter)
	: "t");
}
/*
 * To get proper branch prediction for the main line, we must branch
 * forward to code at the end of this object's .text section, then
 * branch back to restart the operation.
 */

#define ATOMIC_OP(op)							\
static inline void atomic_##op(int i, atomic_t *v)			\
{									\
	unsigned long tmp;						\
									\
	__asm__ __volatile__ (						\
"1:	movli.l @%2, %0		! atomic_" #op "\n"			\
"	" #op "	%1, %0				\n"			\
"	movco.l	%0, @%2				\n"			\
"	bf	1b				\n"			\
	: "=&z" (tmp)							\
	: "r" (i), "r" (&v->counter)					\
	: "t");								\
}

#define ATOMIC_OP_RETURN(op)						\
static inline int atomic_##op##_return(int i, atomic_t *v)		\
{									\
	unsigned long temp;						\
									\
	__asm__ __volatile__ (						\
"1:	movli.l @%2, %0		! atomic_" #op "_return	\n"		\
"	" #op "	%1, %0					\n"		\
"	movco.l	%0, @%2					\n"		\
"	bf	1b					\n"		\
"	synco						\n"		\
	: "=&z" (temp)							\
	: "r" (i), "r" (&v->counter)					\
	: "t");								\
									\
	return temp;							\
}

#define ATOMIC_OPS(op) ATOMIC_OP(op) ATOMIC_OP_RETURN(op)

ATOMIC_OPS(add)
ATOMIC_OPS(sub)
ATOMIC_OP(and)
ATOMIC_OP(or)
ATOMIC_OP(xor)

#undef ATOMIC_OPS
#undef ATOMIC_OP_RETURN
#undef ATOMIC_OP

#endif /* __ASM_SH_ATOMIC_LLSC_H */
