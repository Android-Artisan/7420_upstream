#ifndef _ASM_GENERIC_ATOMIC_H
#define _ASM_GENERIC_ATOMIC_H
/*
 * Copyright (C) 2005 Silicon Graphics, Inc.
 *	Christoph Lameter
 *
 * Allows to provide arch independent atomic definitions without the need to
 * edit all arch specific atomic.h files.
 */

#include <asm/types.h>

/*
 * Suppport for atomic_long_t
 *
 * Casts for parameters are avoided for existing atomic functions in order to
 * avoid issues with cast-as-lval under gcc 4.x and other limitations that the
 * macros of a platform may have.
 */

#if BITS_PER_LONG == 64

typedef atomic64_t atomic_long_t;

#define ATOMIC_LONG_INIT(i)	ATOMIC64_INIT(i)

static inline long atomic_long_read(atomic_long_t *l)
{
	atomic64_t *v = (atomic64_t *)l;

	return (long)atomic64_read(v);
}

static inline void atomic_long_set(atomic_long_t *l, long i)
{
	atomic64_t *v = (atomic64_t *)l;

	atomic64_set(v, i);
}

static inline void atomic_long_inc(atomic_long_t *l)
{
	atomic64_t *v = (atomic64_t *)l;

	atomic64_inc(v);
}

static inline void atomic_long_dec(atomic_long_t *l)
{
	atomic64_t *v = (atomic64_t *)l;

	atomic64_dec(v);
}

static inline void atomic_long_add(long i, atomic_long_t *l)
{
	atomic64_t *v = (atomic64_t *)l;

	atomic64_add(i, v);
}

static inline void atomic_long_sub(long i, atomic_long_t *l)
{
	atomic64_t *v = (atomic64_t *)l;

	atomic64_sub(i, v);
}

static inline int atomic_long_sub_and_test(long i, atomic_long_t *l)
{
	atomic64_t *v = (atomic64_t *)l;

	return atomic64_sub_and_test(i, v);
}

static inline int atomic_long_dec_and_test(atomic_long_t *l)
{
	atomic64_t *v = (atomic64_t *)l;

	return atomic64_dec_and_test(v);
}

static inline int atomic_long_inc_and_test(atomic_long_t *l)
{
	atomic64_t *v = (atomic64_t *)l;

	return atomic64_inc_and_test(v);
}

static inline int atomic_long_add_negative(long i, atomic_long_t *l)
{
	atomic64_t *v = (atomic64_t *)l;

	return atomic64_add_negative(i, v);
}

static inline long atomic_long_add_return(long i, atomic_long_t *l)
{
	atomic64_t *v = (atomic64_t *)l;

	return (long)atomic64_add_return(i, v);
}

static inline long atomic_long_sub_return(long i, atomic_long_t *l)
{
	atomic64_t *v = (atomic64_t *)l;

	return (long)atomic64_sub_return(i, v);
}

static inline long atomic_long_inc_return(atomic_long_t *l)
{
	atomic64_t *v = (atomic64_t *)l;

	return (long)atomic64_inc_return(v);
}

static inline long atomic_long_dec_return(atomic_long_t *l)
{
	atomic64_t *v = (atomic64_t *)l;

	return (long)atomic64_dec_return(v);
}

static inline long atomic_long_add_unless(atomic_long_t *l, long a, long u)
{
	atomic64_t *v = (atomic64_t *)l;

	return (long)atomic64_add_unless(v, a, u);
}

#define atomic_long_inc_not_zero(l) atomic64_inc_not_zero((atomic64_t *)(l))

#define atomic_long_cmpxchg(l, old, new) \
	(atomic_cmpxchg((atomic64_t *)(l), (old), (new)))
#define atomic_long_xchg(v, new) \
	(atomic_xchg((atomic64_t *)(l), (new)))

#else  /*  BITS_PER_LONG == 64  */

typedef atomic_t atomic_long_t;

#define ATOMIC_LONG_INIT(i)	ATOMIC_INIT(i)
static inline long atomic_long_read(atomic_long_t *l)
{
	atomic_t *v = (atomic_t *)l;

	return (long)atomic_read(v);
}

static inline void atomic_long_set(atomic_long_t *l, long i)
{
	atomic_t *v = (atomic_t *)l;

	atomic_set(v, i);
}

static inline void atomic_long_inc(atomic_long_t *l)
{
	atomic_t *v = (atomic_t *)l;

	atomic_inc(v);
}

static inline void atomic_long_dec(atomic_long_t *l)
{
	atomic_t *v = (atomic_t *)l;

	atomic_dec(v);
}

static inline void atomic_long_add(long i, atomic_long_t *l)
{
	atomic_t *v = (atomic_t *)l;

	atomic_add(i, v);
}

static inline void atomic_long_sub(long i, atomic_long_t *l)
{
	atomic_t *v = (atomic_t *)l;

	atomic_sub(i, v);
}

static inline int atomic_long_sub_and_test(long i, atomic_long_t *l)
{
	atomic_t *v = (atomic_t *)l;

	return atomic_sub_and_test(i, v);
}

static inline int atomic_long_dec_and_test(atomic_long_t *l)
{
	atomic_t *v = (atomic_t *)l;

	return atomic_dec_and_test(v);
}

static inline int atomic_long_inc_and_test(atomic_long_t *l)
{
	atomic_t *v = (atomic_t *)l;

	return atomic_inc_and_test(v);
}

static inline int atomic_long_add_negative(long i, atomic_long_t *l)
{
	atomic_t *v = (atomic_t *)l;

	return atomic_add_negative(i, v);
}

static inline long atomic_long_add_return(long i, atomic_long_t *l)
{
	atomic_t *v = (atomic_t *)l;

	return (long)atomic_add_return(i, v);
}

static inline long atomic_long_sub_return(long i, atomic_long_t *l)
{
	atomic_t *v = (atomic_t *)l;

	return (long)atomic_sub_return(i, v);
}

static inline long atomic_long_inc_return(atomic_long_t *l)
{
	atomic_t *v = (atomic_t *)l;

	return (long)atomic_inc_return(v);
}

static inline long atomic_long_dec_return(atomic_long_t *l)
{
	atomic_t *v = (atomic_t *)l;

	return (long)atomic_dec_return(v);
}

static inline long atomic_long_add_unless(atomic_long_t *l, long a, long u)
{
	atomic_t *v = (atomic_t *)l;

	return (long)atomic_add_unless(v, a, u);
}

#define atomic_long_inc_not_zero(l) atomic_inc_not_zero((atomic_t *)(l))

#define atomic_long_cmpxchg(l, old, new) \
	(atomic_cmpxchg((atomic_t *)(l), (old), (new)))
#define atomic_long_xchg(v, new) \
	(atomic_xchg((atomic_t *)(l), (new)))

#endif  /*  BITS_PER_LONG == 64  */

#endif  /*  _ASM_GENERIC_ATOMIC_H  */
/*
 * Generic C implementation of atomic counter operations. Usable on
 * UP systems only. Do not include in machine independent code.
 *
 * Originally implemented for MN10300.
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef __ASM_GENERIC_ATOMIC_H
#define __ASM_GENERIC_ATOMIC_H

#include <asm/cmpxchg.h>
#include <asm/barrier.h>

/*
 * atomic_$op() - $op integer to atomic variable
 * @i: integer value to $op
 * @v: pointer to the atomic variable
 *
 * Atomically $ops @i to @v. Does not strictly guarantee a memory-barrier, use
 * smp_mb__{before,after}_atomic().
 */

/*
 * atomic_$op_return() - $op interer to atomic variable and returns the result
 * @i: integer value to $op
 * @v: pointer to the atomic variable
 *
 * Atomically $ops @i to @v. Does imply a full memory barrier.
 */

#ifdef CONFIG_SMP

/* we can build all atomic primitives from cmpxchg */

#define ATOMIC_OP(op, c_op)						\
static inline void atomic_##op(int i, atomic_t *v)			\
{									\
	int c, old;							\
									\
	c = v->counter;							\
	while ((old = cmpxchg(&v->counter, c, c c_op i)) != c)		\
		c = old;						\
}

#define ATOMIC_OP_RETURN(op, c_op)					\
static inline int atomic_##op##_return(int i, atomic_t *v)		\
{									\
	int c, old;							\
									\
	c = v->counter;							\
	while ((old = cmpxchg(&v->counter, c, c c_op i)) != c)		\
		c = old;						\
									\
	return c c_op i;						\
}

#else

#include <linux/irqflags.h>

#define ATOMIC_OP(op, c_op)						\
static inline void atomic_##op(int i, atomic_t *v)			\
{									\
	unsigned long flags;						\
									\
	raw_local_irq_save(flags);					\
	v->counter = v->counter c_op i;					\
	raw_local_irq_restore(flags);					\
}

#define ATOMIC_OP_RETURN(op, c_op)					\
static inline int atomic_##op##_return(int i, atomic_t *v)		\
{									\
	unsigned long flags;						\
	int ret;							\
									\
	raw_local_irq_save(flags);					\
	ret = (v->counter = v->counter c_op i);				\
	raw_local_irq_restore(flags);					\
									\
	return ret;							\
}

#endif /* CONFIG_SMP */

#ifndef atomic_add_return
ATOMIC_OP_RETURN(add, +)
#endif

#ifndef atomic_sub_return
ATOMIC_OP_RETURN(sub, -)
#endif

#ifndef atomic_and
ATOMIC_OP(and, &)
#endif

#ifndef atomic_or
ATOMIC_OP(or, |)
#endif

#ifndef atomic_xor
ATOMIC_OP(xor, ^)
#endif

#undef ATOMIC_OP_RETURN
#undef ATOMIC_OP

/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc..
 */

#define ATOMIC_INIT(i)	{ (i) }

/**
 * atomic_read - read atomic variable
 * @v: pointer of type atomic_t
 *
 * Atomically reads the value of @v.
 */
#ifndef atomic_read
#define atomic_read(v)	READ_ONCE((v)->counter)
#endif

/**
 * atomic_set - set atomic variable
 * @v: pointer of type atomic_t
 * @i: required value
 *
 * Atomically sets the value of @v to @i.
 */
#define atomic_set(v, i) WRITE_ONCE(((v)->counter), (i))

#include <linux/irqflags.h>

static inline int atomic_add_negative(int i, atomic_t *v)
{
	return atomic_add_return(i, v) < 0;
}

static inline void atomic_add(int i, atomic_t *v)
{
	atomic_add_return(i, v);
}

static inline void atomic_sub(int i, atomic_t *v)
{
	atomic_sub_return(i, v);
}

static inline void atomic_inc(atomic_t *v)
{
	atomic_add_return(1, v);
}

static inline void atomic_dec(atomic_t *v)
{
	atomic_sub_return(1, v);
}

#define atomic_dec_return(v)		atomic_sub_return(1, (v))
#define atomic_inc_return(v)		atomic_add_return(1, (v))

#define atomic_sub_and_test(i, v)	(atomic_sub_return((i), (v)) == 0)
#define atomic_dec_and_test(v)		(atomic_dec_return(v) == 0)
#define atomic_inc_and_test(v)		(atomic_inc_return(v) == 0)

#define atomic_xchg(ptr, v)		(xchg(&(ptr)->counter, (v)))
#define atomic_cmpxchg(v, old, new)	(cmpxchg(&((v)->counter), (old), (new)))

static inline int __atomic_add_unless(atomic_t *v, int a, int u)
{
	int c, old;
	c = atomic_read(v);
	while (c != u && (old = atomic_cmpxchg(v, c, c + a)) != c)
		c = old;
	return c;
}

#endif /* __ASM_GENERIC_ATOMIC_H */
