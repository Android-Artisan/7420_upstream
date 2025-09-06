/*
 *  include/asm-s390/cputime.h
 *
 *  (C) Copyright IBM Corp. 2004
 *  Copyright IBM Corp. 2004
 *
 *  Author: Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#ifndef _S390_CPUTIME_H
#define _S390_CPUTIME_H

#include <asm/div64.h>

/* We want to use micro-second resolution. */

typedef unsigned long long cputime_t;
typedef unsigned long long cputime64_t;

#ifndef __s390x__

static inline unsigned int
__div(unsigned long long n, unsigned int base)
{
	register_pair rp;

	rp.pair = n >> 1;
	asm ("dr %0,%1" : "+d" (rp) : "d" (base >> 1));
	return rp.subreg.odd;
}

#else /* __s390x__ */

static inline unsigned int
__div(unsigned long long n, unsigned int base)
#include <linux/types.h>
#include <asm/div64.h>

#define CPUTIME_PER_USEC 4096ULL
#define CPUTIME_PER_SEC (CPUTIME_PER_USEC * USEC_PER_SEC)

/* We want to use full resolution of the CPU timer: 2**-12 micro-seconds. */

typedef unsigned long long __nocast cputime_t;
typedef unsigned long long __nocast cputime64_t;

#define cmpxchg_cputime(ptr, old, new) cmpxchg64(ptr, old, new)

static inline unsigned long __div(unsigned long long n, unsigned long base)
{
	return n / base;
}

#endif /* __s390x__ */

#define cputime_zero			(0ULL)
#define cputime_max			((~0UL >> 1) - 1)
#define cputime_add(__a, __b)		((__a) +  (__b))
#define cputime_sub(__a, __b)		((__a) -  (__b))
#define cputime_div(__a, __n) ({		\
	unsigned long long __div = (__a);	\
	do_div(__div,__n);			\
	__div;					\
})
#define cputime_halve(__a)		((__a) >> 1)
#define cputime_eq(__a, __b)		((__a) == (__b))
#define cputime_gt(__a, __b)		((__a) >  (__b))
#define cputime_ge(__a, __b)		((__a) >= (__b))
#define cputime_lt(__a, __b)		((__a) <  (__b))
#define cputime_le(__a, __b)		((__a) <= (__b))
#define cputime_to_jiffies(__ct)	(__div((__ct), 1000000 / HZ))
#define cputime_to_scaled(__ct)		(__ct)
#define jiffies_to_cputime(__hz)	((cputime_t)(__hz) * (1000000 / HZ))

#define cputime64_zero			(0ULL)
#define cputime64_add(__a, __b)		((__a) + (__b))
#define cputime_to_cputime64(__ct)	(__ct)

static inline u64
cputime64_to_jiffies64(cputime64_t cputime)
{
	do_div(cputime, 1000000 / HZ);
	return cputime;
}

/*
 * Convert cputime to milliseconds and back.
 */
static inline unsigned int
cputime_to_msecs(const cputime_t cputime)
{
	return __div(cputime, 1000);
}

static inline cputime_t
msecs_to_cputime(const unsigned int m)
{
	return (cputime_t) m * 1000;
}

/*
 * Convert cputime to milliseconds and back.
 */
static inline unsigned int
cputime_to_secs(const cputime_t cputime)
{
	return __div(cputime, 1000000);
}

static inline cputime_t
secs_to_cputime(const unsigned int s)
{
	return (cputime_t) s * 1000000;
#define cputime_one_jiffy		jiffies_to_cputime(1)

/*
 * Convert cputime to jiffies and back.
 */
static inline unsigned long cputime_to_jiffies(const cputime_t cputime)
{
	return __div((__force unsigned long long) cputime, CPUTIME_PER_SEC / HZ);
}

static inline cputime_t jiffies_to_cputime(const unsigned int jif)
{
	return (__force cputime_t)(jif * (CPUTIME_PER_SEC / HZ));
}

static inline u64 cputime64_to_jiffies64(cputime64_t cputime)
{
	unsigned long long jif = (__force unsigned long long) cputime;
	do_div(jif, CPUTIME_PER_SEC / HZ);
	return jif;
}

static inline cputime64_t jiffies64_to_cputime64(const u64 jif)
{
	return (__force cputime64_t)(jif * (CPUTIME_PER_SEC / HZ));
}

/*
 * Convert cputime to microseconds and back.
 */
static inline unsigned int cputime_to_usecs(const cputime_t cputime)
{
	return (__force unsigned long long) cputime >> 12;
}

static inline cputime_t usecs_to_cputime(const unsigned int m)
{
	return (__force cputime_t)(m * CPUTIME_PER_USEC);
}

#define usecs_to_cputime64(m)		usecs_to_cputime(m)

/*
 * Convert cputime to milliseconds and back.
 */
static inline unsigned int cputime_to_secs(const cputime_t cputime)
{
	return __div((__force unsigned long long) cputime, CPUTIME_PER_SEC / 2) >> 1;
}

static inline cputime_t secs_to_cputime(const unsigned int s)
{
	return (__force cputime_t)(s * CPUTIME_PER_SEC);
}

/*
 * Convert cputime to timespec and back.
 */
static inline cputime_t
timespec_to_cputime(const struct timespec *value)
{
        return value->tv_nsec / 1000 + (u64) value->tv_sec * 1000000;
}

static inline void
cputime_to_timespec(const cputime_t cputime, struct timespec *value)
{
#ifndef __s390x__
	register_pair rp;

	rp.pair = cputime >> 1;
	asm ("dr %0,%1" : "+d" (rp) : "d" (1000000 >> 1));
	value->tv_nsec = rp.subreg.even * 1000;
	value->tv_sec = rp.subreg.odd;
#else
	value->tv_nsec = (cputime % 1000000) * 1000;
	value->tv_sec = cputime / 1000000;
#endif
static inline cputime_t timespec_to_cputime(const struct timespec *value)
{
	unsigned long long ret = value->tv_sec * CPUTIME_PER_SEC;
	return (__force cputime_t)(ret + __div(value->tv_nsec * CPUTIME_PER_USEC, NSEC_PER_USEC));
}

static inline void cputime_to_timespec(const cputime_t cputime,
				       struct timespec *value)
{
	unsigned long long __cputime = (__force unsigned long long) cputime;
	value->tv_nsec = (__cputime % CPUTIME_PER_SEC) * NSEC_PER_USEC / CPUTIME_PER_USEC;
	value->tv_sec = __cputime / CPUTIME_PER_SEC;
}

/*
 * Convert cputime to timeval and back.
 * Since cputime and timeval have the same resolution (microseconds)
 * this is easy.
 */
static inline cputime_t
timeval_to_cputime(const struct timeval *value)
{
        return value->tv_usec + (u64) value->tv_sec * 1000000;
}

static inline void
cputime_to_timeval(const cputime_t cputime, struct timeval *value)
{
#ifndef __s390x__
	register_pair rp;

	rp.pair = cputime >> 1;
	asm ("dr %0,%1" : "+d" (rp) : "d" (1000000 >> 1));
	value->tv_usec = rp.subreg.even;
	value->tv_sec = rp.subreg.odd;
#else
	value->tv_usec = cputime % 1000000;
	value->tv_sec = cputime / 1000000;
#endif
static inline cputime_t timeval_to_cputime(const struct timeval *value)
{
	unsigned long long ret = value->tv_sec * CPUTIME_PER_SEC;
	return (__force cputime_t)(ret + value->tv_usec * CPUTIME_PER_USEC);
}

static inline void cputime_to_timeval(const cputime_t cputime,
				      struct timeval *value)
{
	unsigned long long __cputime = (__force unsigned long long) cputime;
	value->tv_usec = (__cputime % CPUTIME_PER_SEC) / CPUTIME_PER_USEC;
	value->tv_sec = __cputime / CPUTIME_PER_SEC;
}

/*
 * Convert cputime to clock and back.
 */
static inline clock_t
cputime_to_clock_t(cputime_t cputime)
{
	return __div(cputime, 1000000 / USER_HZ);
}

static inline cputime_t
clock_t_to_cputime(unsigned long x)
{
	return (cputime_t) x * (1000000 / USER_HZ);
static inline clock_t cputime_to_clock_t(cputime_t cputime)
{
	unsigned long long clock = (__force unsigned long long) cputime;
	do_div(clock, CPUTIME_PER_SEC / USER_HZ);
	return clock;
}

static inline cputime_t clock_t_to_cputime(unsigned long x)
{
	return (__force cputime_t)(x * (CPUTIME_PER_SEC / USER_HZ));
}

/*
 * Convert cputime64 to clock.
 */
static inline clock_t
cputime64_to_clock_t(cputime64_t cputime)
{
       return __div(cputime, 1000000 / USER_HZ);
}

static inline clock_t cputime64_to_clock_t(cputime64_t cputime)
{
	unsigned long long clock = (__force unsigned long long) cputime;
	do_div(clock, CPUTIME_PER_SEC / USER_HZ);
	return clock;
}

cputime64_t arch_cpu_idle_time(int cpu);

#define arch_idle_time(cpu) arch_cpu_idle_time(cpu)

#endif /* _S390_CPUTIME_H */
