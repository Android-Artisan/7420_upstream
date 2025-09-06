/*
 *  linux/lib/vsprintf.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/* vsprintf.c -- Lars Wirzenius & Linus Torvalds. */
/*
 * Wirzenius wrote this portably, Torvalds fucked it up :-)
 */

/* 
/*
 * Fri Jul 13 2001 Crutcher Dunnavant <crutcher+kernel@datastacks.com>
 * - changed to provide snprintf and vsnprintf functions
 * So Feb  1 16:51:32 CET 2004 Juergen Quade <quade@hsnr.de>
 * - scnprintf and vscnprintf
 */

#include <stdarg.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/module.h>	/* for KSYM_SYMBOL_LEN */
#include <linux/types.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/kallsyms.h>
#include <linux/uaccess.h>

#include <asm/page.h>		/* for PAGE_SIZE */
#include <asm/div64.h>
#include <asm/sections.h>	/* for dereference_function_descriptor() */

/* Works only for digits and letters, but small and fast */
#define TOLOWER(x) ((x) | 0x20)
#include <linux/math64.h>
#include <linux/uaccess.h>
#include <linux/ioport.h>
#include <linux/dcache.h>
#include <linux/cred.h>
#include <net/addrconf.h>

#include <asm/page.h>		/* for PAGE_SIZE */
#include <asm/sections.h>	/* for dereference_function_descriptor() */
#include <asm/byteorder.h>	/* cpu_to_le16 */

#include <linux/string_helpers.h>
#include "kstrtox.h"

/**
 * simple_strtoull - convert a string to an unsigned long long
 * @cp: The start of the string
 * @endp: A pointer to the end of the parsed string will be placed here
 * @base: The number base to use
 *
 * This function is obsolete. Please use kstrtoull instead.
 */
unsigned long long simple_strtoull(const char *cp, char **endp, unsigned int base)
{
	unsigned long long result;
	unsigned int rv;

	cp = _parse_integer_fixup_radix(cp, &base);
	rv = _parse_integer(cp, base, &result);
	/* FIXME */
	cp += (rv & ~KSTRTOX_OVERFLOW);

	if (endp)
		*endp = (char *)cp;

	return result;
}
EXPORT_SYMBOL(simple_strtoull);

/**
 * simple_strtoul - convert a string to an unsigned long
 * @cp: The start of the string
 * @endp: A pointer to the end of the parsed string will be placed here
 * @base: The number base to use
 */
unsigned long simple_strtoul(const char *cp,char **endp,unsigned int base)
{
	unsigned long result = 0,value;

	if (!base) {
		base = 10;
		if (*cp == '0') {
			base = 8;
			cp++;
			if ((TOLOWER(*cp) == 'x') && isxdigit(cp[1])) {
				cp++;
				base = 16;
			}
		}
	} else if (base == 16) {
		if (cp[0] == '0' && TOLOWER(cp[1]) == 'x')
			cp += 2;
	}
	while (isxdigit(*cp) &&
	       (value = isdigit(*cp) ? *cp-'0' : TOLOWER(*cp)-'a'+10) < base) {
		result = result*base + value;
		cp++;
	}
	if (endp)
		*endp = (char *)cp;
	return result;
}

 *
 * This function is obsolete. Please use kstrtoul instead.
 */
unsigned long simple_strtoul(const char *cp, char **endp, unsigned int base)
{
	return simple_strtoull(cp, endp, base);
}
EXPORT_SYMBOL(simple_strtoul);

/**
 * simple_strtol - convert a string to a signed long
 * @cp: The start of the string
 * @endp: A pointer to the end of the parsed string will be placed here
 * @base: The number base to use
 */
long simple_strtol(const char *cp,char **endp,unsigned int base)
{
	if(*cp=='-')
		return -simple_strtoul(cp+1,endp,base);
	return simple_strtoul(cp,endp,base);
}

EXPORT_SYMBOL(simple_strtol);

/**
 * simple_strtoull - convert a string to an unsigned long long
 * @cp: The start of the string
 * @endp: A pointer to the end of the parsed string will be placed here
 * @base: The number base to use
 */
unsigned long long simple_strtoull(const char *cp,char **endp,unsigned int base)
{
	unsigned long long result = 0,value;

	if (!base) {
		base = 10;
		if (*cp == '0') {
			base = 8;
			cp++;
			if ((TOLOWER(*cp) == 'x') && isxdigit(cp[1])) {
				cp++;
				base = 16;
			}
		}
	} else if (base == 16) {
		if (cp[0] == '0' && TOLOWER(cp[1]) == 'x')
			cp += 2;
	}
	while (isxdigit(*cp)
	 && (value = isdigit(*cp) ? *cp-'0' : TOLOWER(*cp)-'a'+10) < base) {
		result = result*base + value;
		cp++;
	}
	if (endp)
		*endp = (char *)cp;
	return result;
}

EXPORT_SYMBOL(simple_strtoull);

/**
 *
 * This function is obsolete. Please use kstrtol instead.
 */
long simple_strtol(const char *cp, char **endp, unsigned int base)
{
	if (*cp == '-')
		return -simple_strtoul(cp + 1, endp, base);

	return simple_strtoul(cp, endp, base);
}
EXPORT_SYMBOL(simple_strtol);

/**
 * simple_strtoll - convert a string to a signed long long
 * @cp: The start of the string
 * @endp: A pointer to the end of the parsed string will be placed here
 * @base: The number base to use
 */
long long simple_strtoll(const char *cp,char **endp,unsigned int base)
{
	if(*cp=='-')
		return -simple_strtoull(cp+1,endp,base);
	return simple_strtoull(cp,endp,base);
}


/**
 * strict_strtoul - convert a string to an unsigned long strictly
 * @cp: The string to be converted
 * @base: The number base to use
 * @res: The converted result value
 *
 * strict_strtoul converts a string to an unsigned long only if the
 * string is really an unsigned long string, any string containing
 * any invalid char at the tail will be rejected and -EINVAL is returned,
 * only a newline char at the tail is acceptible because people generally
 * change a module parameter in the following way:
 *
 * 	echo 1024 > /sys/module/e1000/parameters/copybreak
 *
 * echo will append a newline to the tail.
 *
 * It returns 0 if conversion is successful and *res is set to the converted
 * value, otherwise it returns -EINVAL and *res is set to 0.
 *
 * simple_strtoul just ignores the successive invalid characters and
 * return the converted value of prefix part of the string.
 */
int strict_strtoul(const char *cp, unsigned int base, unsigned long *res);

/**
 * strict_strtol - convert a string to a long strictly
 * @cp: The string to be converted
 * @base: The number base to use
 * @res: The converted result value
 *
 * strict_strtol is similiar to strict_strtoul, but it allows the first
 * character of a string is '-'.
 *
 * It returns 0 if conversion is successful and *res is set to the converted
 * value, otherwise it returns -EINVAL and *res is set to 0.
 */
int strict_strtol(const char *cp, unsigned int base, long *res);

/**
 * strict_strtoull - convert a string to an unsigned long long strictly
 * @cp: The string to be converted
 * @base: The number base to use
 * @res: The converted result value
 *
 * strict_strtoull converts a string to an unsigned long long only if the
 * string is really an unsigned long long string, any string containing
 * any invalid char at the tail will be rejected and -EINVAL is returned,
 * only a newline char at the tail is acceptible because people generally
 * change a module parameter in the following way:
 *
 * 	echo 1024 > /sys/module/e1000/parameters/copybreak
 *
 * echo will append a newline to the tail of the string.
 *
 * It returns 0 if conversion is successful and *res is set to the converted
 * value, otherwise it returns -EINVAL and *res is set to 0.
 *
 * simple_strtoull just ignores the successive invalid characters and
 * return the converted value of prefix part of the string.
 */
int strict_strtoull(const char *cp, unsigned int base, unsigned long long *res);

/**
 * strict_strtoll - convert a string to a long long strictly
 * @cp: The string to be converted
 * @base: The number base to use
 * @res: The converted result value
 *
 * strict_strtoll is similiar to strict_strtoull, but it allows the first
 * character of a string is '-'.
 *
 * It returns 0 if conversion is successful and *res is set to the converted
 * value, otherwise it returns -EINVAL and *res is set to 0.
 */
int strict_strtoll(const char *cp, unsigned int base, long long *res);

#define define_strict_strtoux(type, valtype)				\
int strict_strtou##type(const char *cp, unsigned int base, valtype *res)\
{									\
	char *tail;							\
	valtype val;							\
	size_t len;							\
									\
	*res = 0;							\
	len = strlen(cp);						\
	if (len == 0)							\
		return -EINVAL;						\
									\
	val = simple_strtou##type(cp, &tail, base);			\
	if ((*tail == '\0') ||						\
		((len == (size_t)(tail - cp) + 1) && (*tail == '\n'))) {\
		*res = val;						\
		return 0;						\
	}								\
									\
	return -EINVAL;							\
}									\

#define define_strict_strtox(type, valtype)				\
int strict_strto##type(const char *cp, unsigned int base, valtype *res)	\
{									\
	int ret;							\
	if (*cp == '-') {						\
		ret = strict_strtou##type(cp+1, base, res);		\
		if (!ret)						\
			*res = -(*res);					\
	} else								\
		ret = strict_strtou##type(cp, base, res);		\
									\
	return ret;							\
}									\

define_strict_strtoux(l, unsigned long)
define_strict_strtox(l, long)
define_strict_strtoux(ll, unsigned long long)
define_strict_strtox(ll, long long)

EXPORT_SYMBOL(strict_strtoul);
EXPORT_SYMBOL(strict_strtol);
EXPORT_SYMBOL(strict_strtoll);
EXPORT_SYMBOL(strict_strtoull);

static int skip_atoi(const char **s)
{
	int i=0;

	while (isdigit(**s))
		i = i*10 + *((*s)++) - '0';
	return i;
}

/* Decimal conversion is by far the most typical, and is used
 * for /proc and /sys data. This directly impacts e.g. top performance
 * with many processes running. We optimize it for speed
 * using code from
 * http://www.cs.uiowa.edu/~jones/bcd/decimal.html
 * (with permission from the author, Douglas W. Jones). */

/* Formats correctly any integer in [0,99999].
 * Outputs from one to five digits depending on input.
 * On i386 gcc 4.1.2 -O2: ~250 bytes of code. */
static char* put_dec_trunc(char *buf, unsigned q)
{
	unsigned d3, d2, d1, d0;
	d1 = (q>>4) & 0xf;
	d2 = (q>>8) & 0xf;
	d3 = (q>>12);

	d0 = 6*(d3 + d2 + d1) + (q & 0xf);
	q = (d0 * 0xcd) >> 11;
	d0 = d0 - 10*q;
	*buf++ = d0 + '0'; /* least significant digit */
	d1 = q + 9*d3 + 5*d2 + d1;
	if (d1 != 0) {
		q = (d1 * 0xcd) >> 11;
		d1 = d1 - 10*q;
		*buf++ = d1 + '0'; /* next digit */

		d2 = q + 2*d2;
		if ((d2 != 0) || (d3 != 0)) {
			q = (d2 * 0xd) >> 7;
			d2 = d2 - 10*q;
			*buf++ = d2 + '0'; /* next digit */

			d3 = q + 4*d3;
			if (d3 != 0) {
				q = (d3 * 0xcd) >> 11;
				d3 = d3 - 10*q;
				*buf++ = d3 + '0';  /* next digit */
				if (q != 0)
					*buf++ = q + '0';  /* most sign. digit */
			}
		}
	}
	return buf;
}
/* Same with if's removed. Always emits five digits */
static char* put_dec_full(char *buf, unsigned q)
{
	/* BTW, if q is in [0,9999], 8-bit ints will be enough, */
	/* but anyway, gcc produces better code with full-sized ints */
	unsigned d3, d2, d1, d0;
	d1 = (q>>4) & 0xf;
	d2 = (q>>8) & 0xf;
	d3 = (q>>12);

	/* Possible ways to approx. divide by 10 */
	/* gcc -O2 replaces multiply with shifts and adds */
	// (x * 0xcd) >> 11: 11001101 - shorter code than * 0x67 (on i386)
	// (x * 0x67) >> 10:  1100111
	// (x * 0x34) >> 9:    110100 - same
	// (x * 0x1a) >> 8:     11010 - same
	// (x * 0x0d) >> 7:      1101 - same, shortest code (on i386)

	d0 = 6*(d3 + d2 + d1) + (q & 0xf);
	q = (d0 * 0xcd) >> 11;
	d0 = d0 - 10*q;
	*buf++ = d0 + '0';
	d1 = q + 9*d3 + 5*d2 + d1;
		q = (d1 * 0xcd) >> 11;
		d1 = d1 - 10*q;
		*buf++ = d1 + '0';

		d2 = q + 2*d2;
			q = (d2 * 0xd) >> 7;
			d2 = d2 - 10*q;
			*buf++ = d2 + '0';

			d3 = q + 4*d3;
				q = (d3 * 0xcd) >> 11; /* - shorter code */
				/* q = (d3 * 0x67) >> 10; - would also work */
				d3 = d3 - 10*q;
				*buf++ = d3 + '0';
					*buf++ = q + '0';
	return buf;
}
/* No inlining helps gcc to use registers better */
static noinline char* put_dec(char *buf, unsigned long long num)
{
	while (1) {
		unsigned rem;
		if (num < 100000)
			return put_dec_trunc(buf, num);
		rem = do_div(num, 100000);
		buf = put_dec_full(buf, rem);
	}
}

#define ZEROPAD	1		/* pad with zero */
#define SIGN	2		/* unsigned/signed long */
#define PLUS	4		/* show plus */
#define SPACE	8		/* space if plus */
#define LEFT	16		/* left justified */
#define SMALL	32		/* Must be 32 == 0x20 */
#define SPECIAL	64		/* 0x */

static char *number(char *buf, char *end, unsigned long long num, int base, int size, int precision, int type)
{
	/* we are called with base 8, 10 or 16, only, thus don't need "G..."  */
	static const char digits[16] = "0123456789ABCDEF"; /* "GHIJKLMNOPQRSTUVWXYZ"; */

	char tmp[66];
	char sign;
	char locase;
	int need_pfx = ((type & SPECIAL) && base != 10);
	int i;

	/* locase = 0 or 0x20. ORing digits or letters with 'locase'
	 * produces same digits or (maybe lowercased) letters */
	locase = (type & SMALL);
	if (type & LEFT)
		type &= ~ZEROPAD;
	sign = 0;
	if (type & SIGN) {
		if ((signed long long) num < 0) {
			sign = '-';
			num = - (signed long long) num;
			size--;
		} else if (type & PLUS) {
			sign = '+';
			size--;
		} else if (type & SPACE) {
			sign = ' ';
			size--;
		}
	}
	if (need_pfx) {
		size--;
		if (base == 16)
			size--;
 *
 * This function is obsolete. Please use kstrtoll instead.
 */
long long simple_strtoll(const char *cp, char **endp, unsigned int base)
{
	if (*cp == '-')
		return -simple_strtoull(cp + 1, endp, base);

	return simple_strtoull(cp, endp, base);
}
EXPORT_SYMBOL(simple_strtoll);

static noinline_for_stack
int skip_atoi(const char **s)
{
	int i = 0;

	do {
		i = i*10 + *((*s)++) - '0';
	} while (isdigit(**s));

	return i;
}

/*
 * Decimal conversion is by far the most typical, and is used for
 * /proc and /sys data. This directly impacts e.g. top performance
 * with many processes running. We optimize it for speed by emitting
 * two characters at a time, using a 200 byte lookup table. This
 * roughly halves the number of multiplications compared to computing
 * the digits one at a time. Implementation strongly inspired by the
 * previous version, which in turn used ideas described at
 * <http://www.cs.uiowa.edu/~jones/bcd/divide.html> (with permission
 * from the author, Douglas W. Jones).
 *
 * It turns out there is precisely one 26 bit fixed-point
 * approximation a of 64/100 for which x/100 == (x * (u64)a) >> 32
 * holds for all x in [0, 10^8-1], namely a = 0x28f5c29. The actual
 * range happens to be somewhat larger (x <= 1073741898), but that's
 * irrelevant for our purpose.
 *
 * For dividing a number in the range [10^4, 10^6-1] by 100, we still
 * need a 32x32->64 bit multiply, so we simply use the same constant.
 *
 * For dividing a number in the range [100, 10^4-1] by 100, there are
 * several options. The simplest is (x * 0x147b) >> 19, which is valid
 * for all x <= 43698.
 */

static const u16 decpair[100] = {
#define _(x) (__force u16) cpu_to_le16(((x % 10) | ((x / 10) << 8)) + 0x3030)
	_( 0), _( 1), _( 2), _( 3), _( 4), _( 5), _( 6), _( 7), _( 8), _( 9),
	_(10), _(11), _(12), _(13), _(14), _(15), _(16), _(17), _(18), _(19),
	_(20), _(21), _(22), _(23), _(24), _(25), _(26), _(27), _(28), _(29),
	_(30), _(31), _(32), _(33), _(34), _(35), _(36), _(37), _(38), _(39),
	_(40), _(41), _(42), _(43), _(44), _(45), _(46), _(47), _(48), _(49),
	_(50), _(51), _(52), _(53), _(54), _(55), _(56), _(57), _(58), _(59),
	_(60), _(61), _(62), _(63), _(64), _(65), _(66), _(67), _(68), _(69),
	_(70), _(71), _(72), _(73), _(74), _(75), _(76), _(77), _(78), _(79),
	_(80), _(81), _(82), _(83), _(84), _(85), _(86), _(87), _(88), _(89),
	_(90), _(91), _(92), _(93), _(94), _(95), _(96), _(97), _(98), _(99),
#undef _
};

/*
 * This will print a single '0' even if r == 0, since we would
 * immediately jump to out_r where two 0s would be written but only
 * one of them accounted for in buf. This is needed by ip4_string
 * below. All other callers pass a non-zero value of r.
*/
static noinline_for_stack
char *put_dec_trunc8(char *buf, unsigned r)
{
	unsigned q;

	/* 1 <= r < 10^8 */
	if (r < 100)
		goto out_r;

	/* 100 <= r < 10^8 */
	q = (r * (u64)0x28f5c29) >> 32;
	*((u16 *)buf) = decpair[r - 100*q];
	buf += 2;

	/* 1 <= q < 10^6 */
	if (q < 100)
		goto out_q;

	/*  100 <= q < 10^6 */
	r = (q * (u64)0x28f5c29) >> 32;
	*((u16 *)buf) = decpair[q - 100*r];
	buf += 2;

	/* 1 <= r < 10^4 */
	if (r < 100)
		goto out_r;

	/* 100 <= r < 10^4 */
	q = (r * 0x147b) >> 19;
	*((u16 *)buf) = decpair[r - 100*q];
	buf += 2;
out_q:
	/* 1 <= q < 100 */
	r = q;
out_r:
	/* 1 <= r < 100 */
	*((u16 *)buf) = decpair[r];
	buf += r < 10 ? 1 : 2;
	return buf;
}

#if BITS_PER_LONG == 64 && BITS_PER_LONG_LONG == 64
static noinline_for_stack
char *put_dec_full8(char *buf, unsigned r)
{
	unsigned q;

	/* 0 <= r < 10^8 */
	q = (r * (u64)0x28f5c29) >> 32;
	*((u16 *)buf) = decpair[r - 100*q];
	buf += 2;

	/* 0 <= q < 10^6 */
	r = (q * (u64)0x28f5c29) >> 32;
	*((u16 *)buf) = decpair[q - 100*r];
	buf += 2;

	/* 0 <= r < 10^4 */
	q = (r * 0x147b) >> 19;
	*((u16 *)buf) = decpair[r - 100*q];
	buf += 2;

	/* 0 <= q < 100 */
	*((u16 *)buf) = decpair[q];
	buf += 2;
	return buf;
}

static noinline_for_stack
char *put_dec(char *buf, unsigned long long n)
{
	if (n >= 100*1000*1000)
		buf = put_dec_full8(buf, do_div(n, 100*1000*1000));
	/* 1 <= n <= 1.6e11 */
	if (n >= 100*1000*1000)
		buf = put_dec_full8(buf, do_div(n, 100*1000*1000));
	/* 1 <= n < 1e8 */
	return put_dec_trunc8(buf, n);
}

#elif BITS_PER_LONG == 32 && BITS_PER_LONG_LONG == 64

static void
put_dec_full4(char *buf, unsigned r)
{
	unsigned q;

	/* 0 <= r < 10^4 */
	q = (r * 0x147b) >> 19;
	*((u16 *)buf) = decpair[r - 100*q];
	buf += 2;
	/* 0 <= q < 100 */
	*((u16 *)buf) = decpair[q];
}

/*
 * Call put_dec_full4 on x % 10000, return x / 10000.
 * The approximation x/10000 == (x * 0x346DC5D7) >> 43
 * holds for all x < 1,128,869,999.  The largest value this
 * helper will ever be asked to convert is 1,125,520,955.
 * (second call in the put_dec code, assuming n is all-ones).
 */
static noinline_for_stack
unsigned put_dec_helper4(char *buf, unsigned x)
{
        uint32_t q = (x * (uint64_t)0x346DC5D7) >> 43;

        put_dec_full4(buf, x - q * 10000);
        return q;
}

/* Based on code by Douglas W. Jones found at
 * <http://www.cs.uiowa.edu/~jones/bcd/decimal.html#sixtyfour>
 * (with permission from the author).
 * Performs no 64-bit division and hence should be fast on 32-bit machines.
 */
static
char *put_dec(char *buf, unsigned long long n)
{
	uint32_t d3, d2, d1, q, h;

	if (n < 100*1000*1000)
		return put_dec_trunc8(buf, n);

	d1  = ((uint32_t)n >> 16); /* implicit "& 0xffff" */
	h   = (n >> 32);
	d2  = (h      ) & 0xffff;
	d3  = (h >> 16); /* implicit "& 0xffff" */

	/* n = 2^48 d3 + 2^32 d2 + 2^16 d1 + d0
	     = 281_4749_7671_0656 d3 + 42_9496_7296 d2 + 6_5536 d1 + d0 */
	q   = 656 * d3 + 7296 * d2 + 5536 * d1 + ((uint32_t)n & 0xffff);
	q = put_dec_helper4(buf, q);

	q += 7671 * d3 + 9496 * d2 + 6 * d1;
	q = put_dec_helper4(buf+4, q);

	q += 4749 * d3 + 42 * d2;
	q = put_dec_helper4(buf+8, q);

	q += 281 * d3;
	buf += 12;
	if (q)
		buf = put_dec_trunc8(buf, q);
	else while (buf[-1] == '0')
		--buf;

	return buf;
}

#endif

/*
 * Convert passed number to decimal string.
 * Returns the length of string.  On buffer overflow, returns 0.
 *
 * If speed is not important, use snprintf(). It's easy to read the code.
 */
int num_to_str(char *buf, int size, unsigned long long num)
{
	/* put_dec requires 2-byte alignment of the buffer. */
	char tmp[sizeof(num) * 3] __aligned(2);
	int idx, len;

	/* put_dec() may work incorrectly for num = 0 (generate "", not "0") */
	if (num <= 9) {
		tmp[0] = '0' + num;
		len = 1;
	} else {
		len = put_dec(tmp, num) - tmp;
	}

	if (len > size)
		return 0;
	for (idx = 0; idx < len; ++idx)
		buf[idx] = tmp[len - idx - 1];
	return len;
}

#define SIGN	1		/* unsigned/signed, must be 1 */
#define LEFT	2		/* left justified */
#define PLUS	4		/* show plus */
#define SPACE	8		/* space if plus */
#define ZEROPAD	16		/* pad with zero, must be 16 == '0' - ' ' */
#define SMALL	32		/* use lowercase in hex (must be 32 == 0x20) */
#define SPECIAL	64		/* prefix hex with "0x", octal with "0" */

enum format_type {
	FORMAT_TYPE_NONE, /* Just a string part */
	FORMAT_TYPE_WIDTH,
	FORMAT_TYPE_PRECISION,
	FORMAT_TYPE_CHAR,
	FORMAT_TYPE_STR,
	FORMAT_TYPE_PTR,
	FORMAT_TYPE_PERCENT_CHAR,
	FORMAT_TYPE_INVALID,
	FORMAT_TYPE_LONG_LONG,
	FORMAT_TYPE_ULONG,
	FORMAT_TYPE_LONG,
	FORMAT_TYPE_UBYTE,
	FORMAT_TYPE_BYTE,
	FORMAT_TYPE_USHORT,
	FORMAT_TYPE_SHORT,
	FORMAT_TYPE_UINT,
	FORMAT_TYPE_INT,
	FORMAT_TYPE_SIZE_T,
	FORMAT_TYPE_PTRDIFF
};

struct printf_spec {
	u8	type;		/* format_type enum */
	u8	flags;		/* flags to number() */
	u8	base;		/* number base, 8, 10 or 16 only */
	u8	qualifier;	/* number qualifier, one of 'hHlLtzZ' */
	s16	field_width;	/* width of output field */
	s16	precision;	/* # of digits/chars */
};

static noinline_for_stack
char *number(char *buf, char *end, unsigned long long num,
	     struct printf_spec spec)
{
	/* put_dec requires 2-byte alignment of the buffer. */
	char tmp[3 * sizeof(num)] __aligned(2);
	char sign;
	char locase;
	int need_pfx = ((spec.flags & SPECIAL) && spec.base != 10);
	int i;
	bool is_zero = num == 0LL;

	/* locase = 0 or 0x20. ORing digits or letters with 'locase'
	 * produces same digits or (maybe lowercased) letters */
	locase = (spec.flags & SMALL);
	if (spec.flags & LEFT)
		spec.flags &= ~ZEROPAD;
	sign = 0;
	if (spec.flags & SIGN) {
		if ((signed long long)num < 0) {
			sign = '-';
			num = -(signed long long)num;
			spec.field_width--;
		} else if (spec.flags & PLUS) {
			sign = '+';
			spec.field_width--;
		} else if (spec.flags & SPACE) {
			sign = ' ';
			spec.field_width--;
		}
	}
	if (need_pfx) {
		if (spec.base == 16)
			spec.field_width -= 2;
		else if (!is_zero)
			spec.field_width--;
	}

	/* generate full string in tmp[], in reverse order */
	i = 0;
	if (num == 0)
		tmp[i++] = '0';
	/* Generic code, for any base:
	else do {
		tmp[i++] = (digits[do_div(num,base)] | locase);
	} while (num != 0);
	*/
	else if (base != 10) { /* 8 or 16 */
		int mask = base - 1;
		int shift = 3;
		if (base == 16) shift = 4;
		do {
			tmp[i++] = (digits[((unsigned char)num) & mask] | locase);
	if (num < spec.base)
		tmp[i++] = hex_asc_upper[num] | locase;
	else if (spec.base != 10) { /* 8 or 16 */
		int mask = spec.base - 1;
		int shift = 3;

		if (spec.base == 16)
			shift = 4;
		do {
			tmp[i++] = (hex_asc_upper[((unsigned char)num) & mask] | locase);
			num >>= shift;
		} while (num);
	} else { /* base 10 */
		i = put_dec(tmp, num) - tmp;
	}

	/* printing 100 using %2d gives "100", not "00" */
	if (i > precision)
		precision = i;
	/* leading space padding */
	size -= precision;
	if (!(type & (ZEROPAD+LEFT))) {
		while(--size >= 0) {
	if (i > spec.precision)
		spec.precision = i;
	/* leading space padding */
	spec.field_width -= spec.precision;
	if (!(spec.flags & (ZEROPAD | LEFT))) {
		while (--spec.field_width >= 0) {
			if (buf < end)
				*buf = ' ';
			++buf;
		}
	}
	/* sign */
	if (sign) {
		if (buf < end)
			*buf = sign;
		++buf;
	}
	/* "0x" / "0" prefix */
	if (need_pfx) {
		if (buf < end)
			*buf = '0';
		++buf;
		if (base == 16) {
		if (spec.base == 16 || !is_zero) {
			if (buf < end)
				*buf = '0';
			++buf;
		}
		if (spec.base == 16) {
			if (buf < end)
				*buf = ('X' | locase);
			++buf;
		}
	}
	/* zero or space padding */
	if (!(type & LEFT)) {
		char c = (type & ZEROPAD) ? '0' : ' ';
		while (--size >= 0) {
	if (!(spec.flags & LEFT)) {
		char c = ' ' + (spec.flags & ZEROPAD);
		BUILD_BUG_ON(' ' + ZEROPAD != '0');
		while (--spec.field_width >= 0) {
			if (buf < end)
				*buf = c;
			++buf;
		}
	}
	/* hmm even more zero padding? */
	while (i <= --precision) {
	while (i <= --spec.precision) {
		if (buf < end)
			*buf = '0';
		++buf;
	}
	/* actual digits of result */
	while (--i >= 0) {
		if (buf < end)
			*buf = tmp[i];
		++buf;
	}
	/* trailing space padding */
	while (--size >= 0) {
	while (--spec.field_width >= 0) {
		if (buf < end)
			*buf = ' ';
		++buf;
	}
	return buf;
}

static char *string(char *buf, char *end, char *s, int field_width, int precision, int flags)

	return buf;
}

static noinline_for_stack
char *string(char *buf, char *end, const char *s, struct printf_spec spec)
{
	int len, i;

	if ((unsigned long)s < PAGE_SIZE)
		s = "<NULL>";

	len = strnlen(s, precision);

	if (!(flags & LEFT)) {
		while (len < field_width--) {
		s = "(null)";

	len = strnlen(s, spec.precision);

	if (!(spec.flags & LEFT)) {
		while (len < spec.field_width--) {
			if (buf < end)
				*buf = ' ';
			++buf;
		}
	}
	for (i = 0; i < len; ++i) {
		if (buf < end)
			*buf = *s;
		++buf; ++s;
	}
	while (len < field_width--) {
	while (len < spec.field_width--) {
		if (buf < end)
			*buf = ' ';
		++buf;
	}
	return buf;
}

static char *symbol_string(char *buf, char *end, void *ptr, int field_width, int precision, int flags)
{
	unsigned long value = (unsigned long) ptr;
#ifdef CONFIG_KALLSYMS
	char sym[KSYM_SYMBOL_LEN];
	sprint_symbol(sym, value);
	return string(buf, end, sym, field_width, precision, flags);
#else
	field_width = 2*sizeof(void *);
	flags |= SPECIAL | SMALL | ZEROPAD;
	return number(buf, end, value, 16, field_width, precision, flags);
#endif
}


	return buf;
}

static void widen(char *buf, char *end, unsigned len, unsigned spaces)
{
	size_t size;
	if (buf >= end)	/* nowhere to put anything */
		return;
	size = end - buf;
	if (size <= spaces) {
		memset(buf, ' ', size);
		return;
	}
	if (len) {
		if (len > size - spaces)
			len = size - spaces;
		memmove(buf + spaces, buf, len);
	}
	memset(buf, ' ', spaces);
}

static noinline_for_stack
char *dentry_name(char *buf, char *end, const struct dentry *d, struct printf_spec spec,
		  const char *fmt)
{
	const char *array[4], *s;
	const struct dentry *p;
	int depth;
	int i, n;

	switch (fmt[1]) {
		case '2': case '3': case '4':
			depth = fmt[1] - '0';
			break;
		default:
			depth = 1;
	}

	rcu_read_lock();
	for (i = 0; i < depth; i++, d = p) {
		p = ACCESS_ONCE(d->d_parent);
		array[i] = ACCESS_ONCE(d->d_name.name);
		if (p == d) {
			if (i)
				array[i] = "";
			i++;
			break;
		}
	}
	s = array[--i];
	for (n = 0; n != spec.precision; n++, buf++) {
		char c = *s++;
		if (!c) {
			if (!i)
				break;
			c = '/';
			s = array[--i];
		}
		if (buf < end)
			*buf = c;
	}
	rcu_read_unlock();
	if (n < spec.field_width) {
		/* we want to pad the sucker */
		unsigned spaces = spec.field_width - n;
		if (!(spec.flags & LEFT)) {
			widen(buf - n, end, n, spaces);
			return buf + spaces;
		}
		while (spaces--) {
			if (buf < end)
				*buf = ' ';
			++buf;
		}
	}
	return buf;
}

static noinline_for_stack
char *symbol_string(char *buf, char *end, void *ptr,
		    struct printf_spec spec, const char *fmt)
{
	unsigned long value;
#ifdef CONFIG_KALLSYMS
	char sym[KSYM_SYMBOL_LEN];
#endif

	if (fmt[1] == 'R')
		ptr = __builtin_extract_return_addr(ptr);
	value = (unsigned long)ptr;

#ifdef CONFIG_KALLSYMS
	if (*fmt == 'B')
		sprint_backtrace(sym, value);
	else if (*fmt != 'f' && *fmt != 's')
		sprint_symbol(sym, value);
	else
		sprint_symbol_no_offset(sym, value);

	return string(buf, end, sym, spec);
#else
	spec.field_width = 2 * sizeof(void *);
	spec.flags |= SPECIAL | SMALL | ZEROPAD;
	spec.base = 16;

	return number(buf, end, value, spec);
#endif
}

static noinline_for_stack
char *resource_string(char *buf, char *end, struct resource *res,
		      struct printf_spec spec, const char *fmt)
{
#ifndef IO_RSRC_PRINTK_SIZE
#define IO_RSRC_PRINTK_SIZE	6
#endif

#ifndef MEM_RSRC_PRINTK_SIZE
#define MEM_RSRC_PRINTK_SIZE	10
#endif
	static const struct printf_spec io_spec = {
		.base = 16,
		.field_width = IO_RSRC_PRINTK_SIZE,
		.precision = -1,
		.flags = SPECIAL | SMALL | ZEROPAD,
	};
	static const struct printf_spec mem_spec = {
		.base = 16,
		.field_width = MEM_RSRC_PRINTK_SIZE,
		.precision = -1,
		.flags = SPECIAL | SMALL | ZEROPAD,
	};
	static const struct printf_spec bus_spec = {
		.base = 16,
		.field_width = 2,
		.precision = -1,
		.flags = SMALL | ZEROPAD,
	};
	static const struct printf_spec dec_spec = {
		.base = 10,
		.precision = -1,
		.flags = 0,
	};
	static const struct printf_spec str_spec = {
		.field_width = -1,
		.precision = 10,
		.flags = LEFT,
	};
	static const struct printf_spec flag_spec = {
		.base = 16,
		.precision = -1,
		.flags = SPECIAL | SMALL,
	};

	/* 32-bit res (sizeof==4): 10 chars in dec, 10 in hex ("0x" + 8)
	 * 64-bit res (sizeof==8): 20 chars in dec, 18 in hex ("0x" + 16) */
#define RSRC_BUF_SIZE		((2 * sizeof(resource_size_t)) + 4)
#define FLAG_BUF_SIZE		(2 * sizeof(res->flags))
#define DECODED_BUF_SIZE	sizeof("[mem - 64bit pref window disabled]")
#define RAW_BUF_SIZE		sizeof("[mem - flags 0x]")
	char sym[max(2*RSRC_BUF_SIZE + DECODED_BUF_SIZE,
		     2*RSRC_BUF_SIZE + FLAG_BUF_SIZE + RAW_BUF_SIZE)];

	char *p = sym, *pend = sym + sizeof(sym);
	int decode = (fmt[0] == 'R') ? 1 : 0;
	const struct printf_spec *specp;

	*p++ = '[';
	if (res->flags & IORESOURCE_IO) {
		p = string(p, pend, "io  ", str_spec);
		specp = &io_spec;
	} else if (res->flags & IORESOURCE_MEM) {
		p = string(p, pend, "mem ", str_spec);
		specp = &mem_spec;
	} else if (res->flags & IORESOURCE_IRQ) {
		p = string(p, pend, "irq ", str_spec);
		specp = &dec_spec;
	} else if (res->flags & IORESOURCE_DMA) {
		p = string(p, pend, "dma ", str_spec);
		specp = &dec_spec;
	} else if (res->flags & IORESOURCE_BUS) {
		p = string(p, pend, "bus ", str_spec);
		specp = &bus_spec;
	} else {
		p = string(p, pend, "??? ", str_spec);
		specp = &mem_spec;
		decode = 0;
	}
	if (decode && res->flags & IORESOURCE_UNSET) {
		p = string(p, pend, "size ", str_spec);
		p = number(p, pend, resource_size(res), *specp);
	} else {
		p = number(p, pend, res->start, *specp);
		if (res->start != res->end) {
			*p++ = '-';
			p = number(p, pend, res->end, *specp);
		}
	}
	if (decode) {
		if (res->flags & IORESOURCE_MEM_64)
			p = string(p, pend, " 64bit", str_spec);
		if (res->flags & IORESOURCE_PREFETCH)
			p = string(p, pend, " pref", str_spec);
		if (res->flags & IORESOURCE_WINDOW)
			p = string(p, pend, " window", str_spec);
		if (res->flags & IORESOURCE_DISABLED)
			p = string(p, pend, " disabled", str_spec);
	} else {
		p = string(p, pend, " flags ", str_spec);
		p = number(p, pend, res->flags, flag_spec);
	}
	*p++ = ']';
	*p = '\0';

	return string(buf, end, sym, spec);
}

static noinline_for_stack
char *hex_string(char *buf, char *end, u8 *addr, struct printf_spec spec,
		 const char *fmt)
{
	int i, len = 1;		/* if we pass '%ph[CDN]', field width remains
				   negative value, fallback to the default */
	char separator;

	if (spec.field_width == 0)
		/* nothing to print */
		return buf;

	if (ZERO_OR_NULL_PTR(addr))
		/* NULL pointer */
		return string(buf, end, NULL, spec);

	switch (fmt[1]) {
	case 'C':
		separator = ':';
		break;
	case 'D':
		separator = '-';
		break;
	case 'N':
		separator = 0;
		break;
	default:
		separator = ' ';
		break;
	}

	if (spec.field_width > 0)
		len = min_t(int, spec.field_width, 64);

	for (i = 0; i < len; ++i) {
		if (buf < end)
			*buf = hex_asc_hi(addr[i]);
		++buf;
		if (buf < end)
			*buf = hex_asc_lo(addr[i]);
		++buf;

		if (separator && i != len - 1) {
			if (buf < end)
				*buf = separator;
			++buf;
		}
	}

	return buf;
}

static noinline_for_stack
char *bitmap_string(char *buf, char *end, unsigned long *bitmap,
		    struct printf_spec spec, const char *fmt)
{
	const int CHUNKSZ = 32;
	int nr_bits = max_t(int, spec.field_width, 0);
	int i, chunksz;
	bool first = true;

	/* reused to print numbers */
	spec = (struct printf_spec){ .flags = SMALL | ZEROPAD, .base = 16 };

	chunksz = nr_bits & (CHUNKSZ - 1);
	if (chunksz == 0)
		chunksz = CHUNKSZ;

	i = ALIGN(nr_bits, CHUNKSZ) - CHUNKSZ;
	for (; i >= 0; i -= CHUNKSZ) {
		u32 chunkmask, val;
		int word, bit;

		chunkmask = ((1ULL << chunksz) - 1);
		word = i / BITS_PER_LONG;
		bit = i % BITS_PER_LONG;
		val = (bitmap[word] >> bit) & chunkmask;

		if (!first) {
			if (buf < end)
				*buf = ',';
			buf++;
		}
		first = false;

		spec.field_width = DIV_ROUND_UP(chunksz, 4);
		buf = number(buf, end, val, spec);

		chunksz = CHUNKSZ;
	}
	return buf;
}

static noinline_for_stack
char *bitmap_list_string(char *buf, char *end, unsigned long *bitmap,
			 struct printf_spec spec, const char *fmt)
{
	int nr_bits = max_t(int, spec.field_width, 0);
	/* current bit is 'cur', most recently seen range is [rbot, rtop] */
	int cur, rbot, rtop;
	bool first = true;

	/* reused to print numbers */
	spec = (struct printf_spec){ .base = 10 };

	rbot = cur = find_first_bit(bitmap, nr_bits);
	while (cur < nr_bits) {
		rtop = cur;
		cur = find_next_bit(bitmap, nr_bits, cur + 1);
		if (cur < nr_bits && cur <= rtop + 1)
			continue;

		if (!first) {
			if (buf < end)
				*buf = ',';
			buf++;
		}
		first = false;

		buf = number(buf, end, rbot, spec);
		if (rbot < rtop) {
			if (buf < end)
				*buf = '-';
			buf++;

			buf = number(buf, end, rtop, spec);
		}

		rbot = cur;
	}
	return buf;
}

static noinline_for_stack
char *mac_address_string(char *buf, char *end, u8 *addr,
			 struct printf_spec spec, const char *fmt)
{
	char mac_addr[sizeof("xx:xx:xx:xx:xx:xx")];
	char *p = mac_addr;
	int i;
	char separator;
	bool reversed = false;

	switch (fmt[1]) {
	case 'F':
		separator = '-';
		break;

	case 'R':
		reversed = true;
		/* fall through */

	default:
		separator = ':';
		break;
	}

	for (i = 0; i < 6; i++) {
		if (reversed)
			p = hex_byte_pack(p, addr[5 - i]);
		else
			p = hex_byte_pack(p, addr[i]);

		if (fmt[0] == 'M' && i != 5)
			*p++ = separator;
	}
	*p = '\0';

	return string(buf, end, mac_addr, spec);
}

static noinline_for_stack
char *ip4_string(char *p, const u8 *addr, const char *fmt)
{
	int i;
	bool leading_zeros = (fmt[0] == 'i');
	int index;
	int step;

	switch (fmt[2]) {
	case 'h':
#ifdef __BIG_ENDIAN
		index = 0;
		step = 1;
#else
		index = 3;
		step = -1;
#endif
		break;
	case 'l':
		index = 3;
		step = -1;
		break;
	case 'n':
	case 'b':
	default:
		index = 0;
		step = 1;
		break;
	}
	for (i = 0; i < 4; i++) {
		char temp[4] __aligned(2);	/* hold each IP quad in reverse order */
		int digits = put_dec_trunc8(temp, addr[index]) - temp;
		if (leading_zeros) {
			if (digits < 3)
				*p++ = '0';
			if (digits < 2)
				*p++ = '0';
		}
		/* reverse the digits in the quad */
		while (digits--)
			*p++ = temp[digits];
		if (i < 3)
			*p++ = '.';
		index += step;
	}
	*p = '\0';

	return p;
}

static noinline_for_stack
char *ip6_compressed_string(char *p, const char *addr)
{
	int i, j, range;
	unsigned char zerolength[8];
	int longest = 1;
	int colonpos = -1;
	u16 word;
	u8 hi, lo;
	bool needcolon = false;
	bool useIPv4;
	struct in6_addr in6;

	memcpy(&in6, addr, sizeof(struct in6_addr));

	useIPv4 = ipv6_addr_v4mapped(&in6) || ipv6_addr_is_isatap(&in6);

	memset(zerolength, 0, sizeof(zerolength));

	if (useIPv4)
		range = 6;
	else
		range = 8;

	/* find position of longest 0 run */
	for (i = 0; i < range; i++) {
		for (j = i; j < range; j++) {
			if (in6.s6_addr16[j] != 0)
				break;
			zerolength[i]++;
		}
	}
	for (i = 0; i < range; i++) {
		if (zerolength[i] > longest) {
			longest = zerolength[i];
			colonpos = i;
		}
	}
	if (longest == 1)		/* don't compress a single 0 */
		colonpos = -1;

	/* emit address */
	for (i = 0; i < range; i++) {
		if (i == colonpos) {
			if (needcolon || i == 0)
				*p++ = ':';
			*p++ = ':';
			needcolon = false;
			i += longest - 1;
			continue;
		}
		if (needcolon) {
			*p++ = ':';
			needcolon = false;
		}
		/* hex u16 without leading 0s */
		word = ntohs(in6.s6_addr16[i]);
		hi = word >> 8;
		lo = word & 0xff;
		if (hi) {
			if (hi > 0x0f)
				p = hex_byte_pack(p, hi);
			else
				*p++ = hex_asc_lo(hi);
			p = hex_byte_pack(p, lo);
		}
		else if (lo > 0x0f)
			p = hex_byte_pack(p, lo);
		else
			*p++ = hex_asc_lo(lo);
		needcolon = true;
	}

	if (useIPv4) {
		if (needcolon)
			*p++ = ':';
		p = ip4_string(p, &in6.s6_addr[12], "I4");
	}
	*p = '\0';

	return p;
}

static noinline_for_stack
char *ip6_string(char *p, const char *addr, const char *fmt)
{
	int i;

	for (i = 0; i < 8; i++) {
		p = hex_byte_pack(p, *addr++);
		p = hex_byte_pack(p, *addr++);
		if (fmt[0] == 'I' && i != 7)
			*p++ = ':';
	}
	*p = '\0';

	return p;
}

static noinline_for_stack
char *ip6_addr_string(char *buf, char *end, const u8 *addr,
		      struct printf_spec spec, const char *fmt)
{
	char ip6_addr[sizeof("xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:255.255.255.255")];

	if (fmt[0] == 'I' && fmt[2] == 'c')
		ip6_compressed_string(ip6_addr, addr);
	else
		ip6_string(ip6_addr, addr, fmt);

	return string(buf, end, ip6_addr, spec);
}

static noinline_for_stack
char *ip4_addr_string(char *buf, char *end, const u8 *addr,
		      struct printf_spec spec, const char *fmt)
{
	char ip4_addr[sizeof("255.255.255.255")];

	ip4_string(ip4_addr, addr, fmt);

	return string(buf, end, ip4_addr, spec);
}

static noinline_for_stack
char *ip6_addr_string_sa(char *buf, char *end, const struct sockaddr_in6 *sa,
			 struct printf_spec spec, const char *fmt)
{
	bool have_p = false, have_s = false, have_f = false, have_c = false;
	char ip6_addr[sizeof("[xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:255.255.255.255]") +
		      sizeof(":12345") + sizeof("/123456789") +
		      sizeof("%1234567890")];
	char *p = ip6_addr, *pend = ip6_addr + sizeof(ip6_addr);
	const u8 *addr = (const u8 *) &sa->sin6_addr;
	char fmt6[2] = { fmt[0], '6' };
	u8 off = 0;

	fmt++;
	while (isalpha(*++fmt)) {
		switch (*fmt) {
		case 'p':
			have_p = true;
			break;
		case 'f':
			have_f = true;
			break;
		case 's':
			have_s = true;
			break;
		case 'c':
			have_c = true;
			break;
		}
	}

	if (have_p || have_s || have_f) {
		*p = '[';
		off = 1;
	}

	if (fmt6[0] == 'I' && have_c)
		p = ip6_compressed_string(ip6_addr + off, addr);
	else
		p = ip6_string(ip6_addr + off, addr, fmt6);

	if (have_p || have_s || have_f)
		*p++ = ']';

	if (have_p) {
		*p++ = ':';
		p = number(p, pend, ntohs(sa->sin6_port), spec);
	}
	if (have_f) {
		*p++ = '/';
		p = number(p, pend, ntohl(sa->sin6_flowinfo &
					  IPV6_FLOWINFO_MASK), spec);
	}
	if (have_s) {
		*p++ = '%';
		p = number(p, pend, sa->sin6_scope_id, spec);
	}
	*p = '\0';

	return string(buf, end, ip6_addr, spec);
}

static noinline_for_stack
char *ip4_addr_string_sa(char *buf, char *end, const struct sockaddr_in *sa,
			 struct printf_spec spec, const char *fmt)
{
	bool have_p = false;
	char *p, ip4_addr[sizeof("255.255.255.255") + sizeof(":12345")];
	char *pend = ip4_addr + sizeof(ip4_addr);
	const u8 *addr = (const u8 *) &sa->sin_addr.s_addr;
	char fmt4[3] = { fmt[0], '4', 0 };

	fmt++;
	while (isalpha(*++fmt)) {
		switch (*fmt) {
		case 'p':
			have_p = true;
			break;
		case 'h':
		case 'l':
		case 'n':
		case 'b':
			fmt4[2] = *fmt;
			break;
		}
	}

	p = ip4_string(ip4_addr, addr, fmt4);
	if (have_p) {
		*p++ = ':';
		p = number(p, pend, ntohs(sa->sin_port), spec);
	}
	*p = '\0';

	return string(buf, end, ip4_addr, spec);
}

static noinline_for_stack
char *escaped_string(char *buf, char *end, u8 *addr, struct printf_spec spec,
		     const char *fmt)
{
	bool found = true;
	int count = 1;
	unsigned int flags = 0;
	int len;

	if (spec.field_width == 0)
		return buf;				/* nothing to print */

	if (ZERO_OR_NULL_PTR(addr))
		return string(buf, end, NULL, spec);	/* NULL pointer */


	do {
		switch (fmt[count++]) {
		case 'a':
			flags |= ESCAPE_ANY;
			break;
		case 'c':
			flags |= ESCAPE_SPECIAL;
			break;
		case 'h':
			flags |= ESCAPE_HEX;
			break;
		case 'n':
			flags |= ESCAPE_NULL;
			break;
		case 'o':
			flags |= ESCAPE_OCTAL;
			break;
		case 'p':
			flags |= ESCAPE_NP;
			break;
		case 's':
			flags |= ESCAPE_SPACE;
			break;
		default:
			found = false;
			break;
		}
	} while (found);

	if (!flags)
		flags = ESCAPE_ANY_NP;

	len = spec.field_width < 0 ? 1 : spec.field_width;

	/*
	 * string_escape_mem() writes as many characters as it can to
	 * the given buffer, and returns the total size of the output
	 * had the buffer been big enough.
	 */
	buf += string_escape_mem(addr, len, buf, buf < end ? end - buf : 0, flags, NULL);

	return buf;
}

static noinline_for_stack
char *uuid_string(char *buf, char *end, const u8 *addr,
		  struct printf_spec spec, const char *fmt)
{
	char uuid[sizeof("xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx")];
	char *p = uuid;
	int i;
	static const u8 be[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
	static const u8 le[16] = {3,2,1,0,5,4,7,6,8,9,10,11,12,13,14,15};
	const u8 *index = be;
	bool uc = false;

	switch (*(++fmt)) {
	case 'L':
		uc = true;		/* fall-through */
	case 'l':
		index = le;
		break;
	case 'B':
		uc = true;
		break;
	}

	for (i = 0; i < 16; i++) {
		p = hex_byte_pack(p, addr[index[i]]);
		switch (i) {
		case 3:
		case 5:
		case 7:
		case 9:
			*p++ = '-';
			break;
		}
	}

	*p = 0;

	if (uc) {
		p = uuid;
		do {
			*p = toupper(*p);
		} while (*(++p));
	}

	return string(buf, end, uuid, spec);
}

static
char *netdev_feature_string(char *buf, char *end, const u8 *addr,
		      struct printf_spec spec)
{
	spec.flags |= SPECIAL | SMALL | ZEROPAD;
	if (spec.field_width == -1)
		spec.field_width = 2 + 2 * sizeof(netdev_features_t);
	spec.base = 16;

	return number(buf, end, *(const netdev_features_t *)addr, spec);
}

static noinline_for_stack
char *address_val(char *buf, char *end, const void *addr,
		  struct printf_spec spec, const char *fmt)
{
	unsigned long long num;

	spec.flags |= SPECIAL | SMALL | ZEROPAD;
	spec.base = 16;

	switch (fmt[1]) {
	case 'd':
		num = *(const dma_addr_t *)addr;
		spec.field_width = sizeof(dma_addr_t) * 2 + 2;
		break;
	case 'p':
	default:
		num = *(const phys_addr_t *)addr;
		spec.field_width = sizeof(phys_addr_t) * 2 + 2;
		break;
	}

	return number(buf, end, num, spec);
}

static noinline_for_stack
char *clock(char *buf, char *end, struct clk *clk, struct printf_spec spec,
	    const char *fmt)
{
	if (!IS_ENABLED(CONFIG_HAVE_CLK) || !clk)
		return string(buf, end, NULL, spec);

	switch (fmt[1]) {
	case 'n':
	default:
#ifdef CONFIG_COMMON_CLK
		return string(buf, end, __clk_get_name(clk), spec);
#else
		spec.base = 16;
		spec.field_width = sizeof(unsigned long) * 2 + 2;
		spec.flags |= SPECIAL | SMALL | ZEROPAD;
		return number(buf, end, (unsigned long)clk, spec);
#endif
	}
}

int kptr_restrict __read_mostly;

/*
 * Show a '%p' thing.  A kernel extension is that the '%p' is followed
 * by an extra set of alphanumeric characters that are extended format
 * specifiers.
 *
 * Right now we just handle 'F' (for symbolic Function descriptor pointers)
 * and 'S' (for Symbolic direct pointers), but this can easily be
 * extended in the future (network address types etc).
 *
 * The difference between 'S' and 'F' is that on ia64 and ppc64 function
 * pointers are really function descriptors, which contain a pointer the
 * real address. 
 */
static char *pointer(const char *fmt, char *buf, char *end, void *ptr, int field_width, int precision, int flags)
{
	switch (*fmt) {
	case 'F':
		ptr = dereference_function_descriptor(ptr);
		/* Fallthrough */
	case 'S':
		return symbol_string(buf, end, ptr, field_width, precision, flags);
	}
	flags |= SMALL;
	if (field_width == -1) {
		field_width = 2*sizeof(void *);
		flags |= ZEROPAD;
	}
	return number(buf, end, (unsigned long) ptr, 16, field_width, precision, flags);
 * Right now we handle:
 *
 * - 'F' For symbolic function descriptor pointers with offset
 * - 'f' For simple symbolic function names without offset
 * - 'S' For symbolic direct pointers with offset
 * - 's' For symbolic direct pointers without offset
 * - '[FfSs]R' as above with __builtin_extract_return_addr() translation
 * - 'B' For backtraced symbolic direct pointers with offset
 * - 'R' For decoded struct resource, e.g., [mem 0x0-0x1f 64bit pref]
 * - 'r' For raw struct resource, e.g., [mem 0x0-0x1f flags 0x201]
 * - 'b[l]' For a bitmap, the number of bits is determined by the field
 *       width which must be explicitly specified either as part of the
 *       format string '%32b[l]' or through '%*b[l]', [l] selects
 *       range-list format instead of hex format
 * - 'M' For a 6-byte MAC address, it prints the address in the
 *       usual colon-separated hex notation
 * - 'm' For a 6-byte MAC address, it prints the hex address without colons
 * - 'MF' For a 6-byte MAC FDDI address, it prints the address
 *       with a dash-separated hex notation
 * - '[mM]R' For a 6-byte MAC address, Reverse order (Bluetooth)
 * - 'I' [46] for IPv4/IPv6 addresses printed in the usual way
 *       IPv4 uses dot-separated decimal without leading 0's (1.2.3.4)
 *       IPv6 uses colon separated network-order 16 bit hex with leading 0's
 *       [S][pfs]
 *       Generic IPv4/IPv6 address (struct sockaddr *) that falls back to
 *       [4] or [6] and is able to print port [p], flowinfo [f], scope [s]
 * - 'i' [46] for 'raw' IPv4/IPv6 addresses
 *       IPv6 omits the colons (01020304...0f)
 *       IPv4 uses dot-separated decimal with leading 0's (010.123.045.006)
 *       [S][pfs]
 *       Generic IPv4/IPv6 address (struct sockaddr *) that falls back to
 *       [4] or [6] and is able to print port [p], flowinfo [f], scope [s]
 * - '[Ii][4S][hnbl]' IPv4 addresses in host, network, big or little endian order
 * - 'I[6S]c' for IPv6 addresses printed as specified by
 *       http://tools.ietf.org/html/rfc5952
 * - 'E[achnops]' For an escaped buffer, where rules are defined by combination
 *                of the following flags (see string_escape_mem() for the
 *                details):
 *                  a - ESCAPE_ANY
 *                  c - ESCAPE_SPECIAL
 *                  h - ESCAPE_HEX
 *                  n - ESCAPE_NULL
 *                  o - ESCAPE_OCTAL
 *                  p - ESCAPE_NP
 *                  s - ESCAPE_SPACE
 *                By default ESCAPE_ANY_NP is used.
 * - 'U' For a 16 byte UUID/GUID, it prints the UUID/GUID in the form
 *       "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
 *       Options for %pU are:
 *         b big endian lower case hex (default)
 *         B big endian UPPER case hex
 *         l little endian lower case hex
 *         L little endian UPPER case hex
 *           big endian output byte order is:
 *             [0][1][2][3]-[4][5]-[6][7]-[8][9]-[10][11][12][13][14][15]
 *           little endian output byte order is:
 *             [3][2][1][0]-[5][4]-[7][6]-[8][9]-[10][11][12][13][14][15]
 * - 'V' For a struct va_format which contains a format string * and va_list *,
 *       call vsnprintf(->format, *->va_list).
 *       Implements a "recursive vsnprintf".
 *       Do not use this feature without some mechanism to verify the
 *       correctness of the format string and va_list arguments.
 * - 'K' For a kernel pointer that should be hidden from unprivileged users
 * - 'NF' For a netdev_features_t
 * - 'h[CDN]' For a variable-length buffer, it prints it as a hex string with
 *            a certain separator (' ' by default):
 *              C colon
 *              D dash
 *              N no separator
 *            The maximum supported length is 64 bytes of the input. Consider
 *            to use print_hex_dump() for the larger input.
 * - 'a[pd]' For address types [p] phys_addr_t, [d] dma_addr_t and derivatives
 *           (default assumed to be phys_addr_t, passed by reference)
 * - 'd[234]' For a dentry name (optionally 2-4 last components)
 * - 'D[234]' Same as 'd' but for a struct file
 * - 'C' For a clock, it prints the name (Common Clock Framework) or address
 *       (legacy clock framework) of the clock
 * - 'Cn' For a clock, it prints the name (Common Clock Framework) or address
 *        (legacy clock framework) of the clock
 * - 'Cr' For a clock, it prints the current rate of the clock
 *
 * ** Please update also Documentation/printk-formats.txt when making changes **
 *
 * Note: The difference between 'S' and 'F' is that on ia64 and ppc64
 * function pointers are really function descriptors, which contain a
 * pointer to the real address.
 */
static noinline_for_stack
char *pointer(const char *fmt, char *buf, char *end, void *ptr,
	      struct printf_spec spec)
{
	const int default_width = 2 * sizeof(void *);

	if (!ptr && *fmt != 'K') {
		/*
		 * Print (null) with the same width as a pointer so it makes
		 * tabular output look nice.
		 */
		if (spec.field_width == -1)
			spec.field_width = default_width;
		return string(buf, end, "(null)", spec);
	}

	switch (*fmt) {
	case 'F':
	case 'f':
		ptr = dereference_function_descriptor(ptr);
		/* Fallthrough */
	case 'S':
	case 's':
	case 'B':
		return symbol_string(buf, end, ptr, spec, fmt);
	case 'R':
	case 'r':
		return resource_string(buf, end, ptr, spec, fmt);
	case 'h':
		return hex_string(buf, end, ptr, spec, fmt);
	case 'b':
		switch (fmt[1]) {
		case 'l':
			return bitmap_list_string(buf, end, ptr, spec, fmt);
		default:
			return bitmap_string(buf, end, ptr, spec, fmt);
		}
	case 'M':			/* Colon separated: 00:01:02:03:04:05 */
	case 'm':			/* Contiguous: 000102030405 */
					/* [mM]F (FDDI) */
					/* [mM]R (Reverse order; Bluetooth) */
		return mac_address_string(buf, end, ptr, spec, fmt);
	case 'I':			/* Formatted IP supported
					 * 4:	1.2.3.4
					 * 6:	0001:0203:...:0708
					 * 6c:	1::708 or 1::1.2.3.4
					 */
	case 'i':			/* Contiguous:
					 * 4:	001.002.003.004
					 * 6:   000102...0f
					 */
		switch (fmt[1]) {
		case '6':
			return ip6_addr_string(buf, end, ptr, spec, fmt);
		case '4':
			return ip4_addr_string(buf, end, ptr, spec, fmt);
		case 'S': {
			const union {
				struct sockaddr		raw;
				struct sockaddr_in	v4;
				struct sockaddr_in6	v6;
			} *sa = ptr;

			switch (sa->raw.sa_family) {
			case AF_INET:
				return ip4_addr_string_sa(buf, end, &sa->v4, spec, fmt);
			case AF_INET6:
				return ip6_addr_string_sa(buf, end, &sa->v6, spec, fmt);
			default:
				return string(buf, end, "(invalid address)", spec);
			}}
		}
		break;
	case 'E':
		return escaped_string(buf, end, ptr, spec, fmt);
	case 'U':
		return uuid_string(buf, end, ptr, spec, fmt);
	case 'V':
		{
			va_list va;

			va_copy(va, *((struct va_format *)ptr)->va);
			buf += vsnprintf(buf, end > buf ? end - buf : 0,
					 ((struct va_format *)ptr)->fmt, va);
			va_end(va);
			return buf;
		}
	case 'K':
		/*
		 * %pK cannot be used in IRQ context because its test
		 * for CAP_SYSLOG would be meaningless.
		 */
		if (kptr_restrict && (in_irq() || in_serving_softirq() ||
				      in_nmi())) {
			if (spec.field_width == -1)
				spec.field_width = default_width;
			return string(buf, end, "pK-error", spec);
		}

		switch (kptr_restrict) {
		case 0:
			/* Always print %pK values */
			break;
		case 1: {
			/*
			 * Only print the real pointer value if the current
			 * process has CAP_SYSLOG and is running with the
			 * same credentials it started with. This is because
			 * access to files is checked at open() time, but %pK
			 * checks permission at read() time. We don't want to
			 * leak pointer values if a binary opens a file using
			 * %pK and then elevates privileges before reading it.
			 */
			const struct cred *cred = current_cred();

			if (!has_capability_noaudit(current, CAP_SYSLOG) ||
			    !uid_eq(cred->euid, cred->uid) ||
			    !gid_eq(cred->egid, cred->gid))
				ptr = NULL;
			break;
		}
		case 2:
		default:
			/* Always print 0's for %pK */
			ptr = NULL;
			break;
		}
		break;

	case 'N':
		switch (fmt[1]) {
		case 'F':
			return netdev_feature_string(buf, end, ptr, spec);
		}
		break;
	case 'a':
		return address_val(buf, end, ptr, spec, fmt);
	case 'd':
		return dentry_name(buf, end, ptr, spec, fmt);
	case 'C':
		return clock(buf, end, ptr, spec, fmt);
	case 'D':
		return dentry_name(buf, end,
				   ((const struct file *)ptr)->f_path.dentry,
				   spec, fmt);
	}
	spec.flags |= SMALL;
	if (spec.field_width == -1) {
		spec.field_width = default_width;
		spec.flags |= ZEROPAD;
	}
	spec.base = 16;

	return number(buf, end, (unsigned long) ptr, spec);
}

/*
 * Helper function to decode printf style format.
 * Each call decode a token from the format and return the
 * number of characters read (or likely the delta where it wants
 * to go on the next call).
 * The decoded token is returned through the parameters
 *
 * 'h', 'l', or 'L' for integer fields
 * 'z' support added 23/7/1999 S.H.
 * 'z' changed to 'Z' --davidm 1/25/99
 * 't' added for ptrdiff_t
 *
 * @fmt: the format string
 * @type of the token returned
 * @flags: various flags such as +, -, # tokens..
 * @field_width: overwritten width
 * @base: base of the number (octal, hex, ...)
 * @precision: precision of a number
 * @qualifier: qualifier of a number (long, size_t, ...)
 */
static noinline_for_stack
int format_decode(const char *fmt, struct printf_spec *spec)
{
	const char *start = fmt;

	/* we finished early by reading the field width */
	if (spec->type == FORMAT_TYPE_WIDTH) {
		if (spec->field_width < 0) {
			spec->field_width = -spec->field_width;
			spec->flags |= LEFT;
		}
		spec->type = FORMAT_TYPE_NONE;
		goto precision;
	}

	/* we finished early by reading the precision */
	if (spec->type == FORMAT_TYPE_PRECISION) {
		if (spec->precision < 0)
			spec->precision = 0;

		spec->type = FORMAT_TYPE_NONE;
		goto qualifier;
	}

	/* By default */
	spec->type = FORMAT_TYPE_NONE;

	for (; *fmt ; ++fmt) {
		if (*fmt == '%')
			break;
	}

	/* Return the current non-format string */
	if (fmt != start || !*fmt)
		return fmt - start;

	/* Process flags */
	spec->flags = 0;

	while (1) { /* this also skips first '%' */
		bool found = true;

		++fmt;

		switch (*fmt) {
		case '-': spec->flags |= LEFT;    break;
		case '+': spec->flags |= PLUS;    break;
		case ' ': spec->flags |= SPACE;   break;
		case '#': spec->flags |= SPECIAL; break;
		case '0': spec->flags |= ZEROPAD; break;
		default:  found = false;
		}

		if (!found)
			break;
	}

	/* get field width */
	spec->field_width = -1;

	if (isdigit(*fmt))
		spec->field_width = skip_atoi(&fmt);
	else if (*fmt == '*') {
		/* it's the next argument */
		spec->type = FORMAT_TYPE_WIDTH;
		return ++fmt - start;
	}

precision:
	/* get the precision */
	spec->precision = -1;
	if (*fmt == '.') {
		++fmt;
		if (isdigit(*fmt)) {
			spec->precision = skip_atoi(&fmt);
			if (spec->precision < 0)
				spec->precision = 0;
		} else if (*fmt == '*') {
			/* it's the next argument */
			spec->type = FORMAT_TYPE_PRECISION;
			return ++fmt - start;
		}
	}

qualifier:
	/* get the conversion qualifier */
	spec->qualifier = -1;
	if (*fmt == 'h' || _tolower(*fmt) == 'l' ||
	    _tolower(*fmt) == 'z' || *fmt == 't') {
		spec->qualifier = *fmt++;
		if (unlikely(spec->qualifier == *fmt)) {
			if (spec->qualifier == 'l') {
				spec->qualifier = 'L';
				++fmt;
			} else if (spec->qualifier == 'h') {
				spec->qualifier = 'H';
				++fmt;
			}
		}
	}

	/* default base */
	spec->base = 10;
	switch (*fmt) {
	case 'c':
		spec->type = FORMAT_TYPE_CHAR;
		return ++fmt - start;

	case 's':
		spec->type = FORMAT_TYPE_STR;
		return ++fmt - start;

	case 'p':
		spec->type = FORMAT_TYPE_PTR;
		return ++fmt - start;

	case '%':
		spec->type = FORMAT_TYPE_PERCENT_CHAR;
		return ++fmt - start;

	/* integer number formats - set up the flags and "break" */
	case 'o':
		spec->base = 8;
		break;

	case 'x':
		spec->flags |= SMALL;

	case 'X':
		spec->base = 16;
		break;

	case 'd':
	case 'i':
		spec->flags |= SIGN;
	case 'u':
		break;

	case 'n':
		/*
		 * Since %n poses a greater security risk than
		 * utility, treat it as any other invalid or
		 * unsupported format specifier.
		 */
		/* Fall-through */

	default:
		WARN_ONCE(1, "Please remove unsupported %%%c in format string\n", *fmt);
		spec->type = FORMAT_TYPE_INVALID;
		return fmt - start;
	}

	if (spec->qualifier == 'L')
		spec->type = FORMAT_TYPE_LONG_LONG;
	else if (spec->qualifier == 'l') {
		BUILD_BUG_ON(FORMAT_TYPE_ULONG + SIGN != FORMAT_TYPE_LONG);
		spec->type = FORMAT_TYPE_ULONG + (spec->flags & SIGN);
	} else if (_tolower(spec->qualifier) == 'z') {
		spec->type = FORMAT_TYPE_SIZE_T;
	} else if (spec->qualifier == 't') {
		spec->type = FORMAT_TYPE_PTRDIFF;
	} else if (spec->qualifier == 'H') {
		BUILD_BUG_ON(FORMAT_TYPE_UBYTE + SIGN != FORMAT_TYPE_BYTE);
		spec->type = FORMAT_TYPE_UBYTE + (spec->flags & SIGN);
	} else if (spec->qualifier == 'h') {
		BUILD_BUG_ON(FORMAT_TYPE_USHORT + SIGN != FORMAT_TYPE_SHORT);
		spec->type = FORMAT_TYPE_USHORT + (spec->flags & SIGN);
	} else {
		BUILD_BUG_ON(FORMAT_TYPE_UINT + SIGN != FORMAT_TYPE_INT);
		spec->type = FORMAT_TYPE_UINT + (spec->flags & SIGN);
	}

	return ++fmt - start;
}

/**
 * vsnprintf - Format a string and place it in a buffer
 * @buf: The buffer to place the result into
 * @size: The size of the buffer, including the trailing null space
 * @fmt: The format string to use
 * @args: Arguments for the format string
 *
 * This function generally follows C99 vsnprintf, but has some
 * extensions and a few limitations:
 *
 * %n is unsupported
 * %p* is handled by pointer()
 *
 * See pointer() or Documentation/printk-formats.txt for more
 * extensive description.
 *
 * ** Please update the documentation in both places when making changes **
 *
 * The return value is the number of characters which would
 * be generated for the given input, excluding the trailing
 * '\0', as per ISO C99. If you want to have the exact
 * number of characters written into @buf as return value
 * (not including the trailing '\0'), use vscnprintf(). If the
 * return is greater than or equal to @size, the resulting
 * string is truncated.
 *
 * Call this function if you are already dealing with a va_list.
 * You probably want snprintf() instead.
 * If you're not already dealing with a va_list consider using snprintf().
 */
int vsnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
	unsigned long long num;
	int base;
	char *str, *end, c;

	int flags;		/* flags to number() */

	int field_width;	/* width of output field */
	int precision;		/* min. # of digits for integers; max
				   number of chars for from string */
	int qualifier;		/* 'h', 'l', or 'L' for integer fields */
				/* 'z' support added 23/7/1999 S.H.    */
				/* 'z' changed to 'Z' --davidm 1/25/99 */
				/* 't' added for ptrdiff_t */

	/* Reject out-of-range values early.  Large positive sizes are
	   used for unknown buffer sizes. */
	if (unlikely((int) size < 0)) {
		/* There can be only one.. */
		static char warn = 1;
		WARN_ON(warn);
		warn = 0;
		return 0;
	}
	char *str, *end;
	struct printf_spec spec = {0};

	/* Reject out-of-range values early.  Large positive sizes are
	   used for unknown buffer sizes. */
	if (WARN_ON_ONCE(size > INT_MAX))
		return 0;

	str = buf;
	end = buf + size;

	/* Make sure end is always >= buf */
	if (end < buf) {
		end = ((void *)-1);
		size = end - buf;
	}

	for (; *fmt ; ++fmt) {
		if (*fmt != '%') {
			if (str < end)
				*str = *fmt;
			++str;
			continue;
		}

		/* process flags */
		flags = 0;
		repeat:
			++fmt;		/* this also skips first '%' */
			switch (*fmt) {
				case '-': flags |= LEFT; goto repeat;
				case '+': flags |= PLUS; goto repeat;
				case ' ': flags |= SPACE; goto repeat;
				case '#': flags |= SPECIAL; goto repeat;
				case '0': flags |= ZEROPAD; goto repeat;
			}

		/* get field width */
		field_width = -1;
		if (isdigit(*fmt))
			field_width = skip_atoi(&fmt);
		else if (*fmt == '*') {
			++fmt;
			/* it's the next argument */
			field_width = va_arg(args, int);
			if (field_width < 0) {
				field_width = -field_width;
				flags |= LEFT;
			}
		}

		/* get the precision */
		precision = -1;
		if (*fmt == '.') {
			++fmt;	
			if (isdigit(*fmt))
				precision = skip_atoi(&fmt);
			else if (*fmt == '*') {
				++fmt;
				/* it's the next argument */
				precision = va_arg(args, int);
			}
			if (precision < 0)
				precision = 0;
		}

		/* get the conversion qualifier */
		qualifier = -1;
		if (*fmt == 'h' || *fmt == 'l' || *fmt == 'L' ||
		    *fmt =='Z' || *fmt == 'z' || *fmt == 't') {
			qualifier = *fmt;
			++fmt;
			if (qualifier == 'l' && *fmt == 'l') {
				qualifier = 'L';
				++fmt;
			}
		}

		/* default base */
		base = 10;

		switch (*fmt) {
			case 'c':
				if (!(flags & LEFT)) {
					while (--field_width > 0) {
						if (str < end)
							*str = ' ';
						++str;
					}
				}
				c = (unsigned char) va_arg(args, int);
				if (str < end)
					*str = c;
				++str;
				while (--field_width > 0) {
					if (str < end)
						*str = ' ';
					++str;
				}
				continue;

			case 's':
				str = string(str, end, va_arg(args, char *), field_width, precision, flags);
				continue;

			case 'p':
				str = pointer(fmt+1, str, end,
						va_arg(args, void *),
						field_width, precision, flags);
				/* Skip all alphanumeric pointer suffixes */
				while (isalnum(fmt[1]))
					fmt++;
				continue;

			case 'n':
				/* FIXME:
				* What does C99 say about the overflow case here? */
				if (qualifier == 'l') {
					long * ip = va_arg(args, long *);
					*ip = (str - buf);
				} else if (qualifier == 'Z' || qualifier == 'z') {
					size_t * ip = va_arg(args, size_t *);
					*ip = (str - buf);
				} else {
					int * ip = va_arg(args, int *);
					*ip = (str - buf);
				}
				continue;

			case '%':
				if (str < end)
					*str = '%';
				++str;
				continue;

				/* integer number formats - set up the flags and "break" */
			case 'o':
				base = 8;
				break;

			case 'x':
				flags |= SMALL;
			case 'X':
				base = 16;
				break;

			case 'd':
			case 'i':
				flags |= SIGN;
			case 'u':
				break;

			default:
				if (str < end)
					*str = '%';
				++str;
				if (*fmt) {
					if (str < end)
						*str = *fmt;
					++str;
				} else {
					--fmt;
				}
				continue;
		}
		if (qualifier == 'L')
			num = va_arg(args, long long);
		else if (qualifier == 'l') {
			num = va_arg(args, unsigned long);
			if (flags & SIGN)
				num = (signed long) num;
		} else if (qualifier == 'Z' || qualifier == 'z') {
			num = va_arg(args, size_t);
		} else if (qualifier == 't') {
			num = va_arg(args, ptrdiff_t);
		} else if (qualifier == 'h') {
			num = (unsigned short) va_arg(args, int);
			if (flags & SIGN)
				num = (signed short) num;
		} else {
			num = va_arg(args, unsigned int);
			if (flags & SIGN)
				num = (signed int) num;
		}
		str = number(str, end, num, base,
				field_width, precision, flags);
	}
	while (*fmt) {
		const char *old_fmt = fmt;
		int read = format_decode(fmt, &spec);

		fmt += read;

		switch (spec.type) {
		case FORMAT_TYPE_NONE: {
			int copy = read;
			if (str < end) {
				if (copy > end - str)
					copy = end - str;
				memcpy(str, old_fmt, copy);
			}
			str += read;
			break;
		}

		case FORMAT_TYPE_WIDTH:
			spec.field_width = va_arg(args, int);
			break;

		case FORMAT_TYPE_PRECISION:
			spec.precision = va_arg(args, int);
			break;

		case FORMAT_TYPE_CHAR: {
			char c;

			if (!(spec.flags & LEFT)) {
				while (--spec.field_width > 0) {
					if (str < end)
						*str = ' ';
					++str;

				}
			}
			c = (unsigned char) va_arg(args, int);
			if (str < end)
				*str = c;
			++str;
			while (--spec.field_width > 0) {
				if (str < end)
					*str = ' ';
				++str;
			}
			break;
		}

		case FORMAT_TYPE_STR:
			str = string(str, end, va_arg(args, char *), spec);
			break;

		case FORMAT_TYPE_PTR:
			str = pointer(fmt, str, end, va_arg(args, void *),
				      spec);
			while (isalnum(*fmt))
				fmt++;
			break;

		case FORMAT_TYPE_PERCENT_CHAR:
			if (str < end)
				*str = '%';
			++str;
			break;

		case FORMAT_TYPE_INVALID:
			/*
			 * Presumably the arguments passed gcc's type
			 * checking, but there is no safe or sane way
			 * for us to continue parsing the format and
			 * fetching from the va_list; the remaining
			 * specifiers and arguments would be out of
			 * sync.
			 */
			goto out;

		default:
			switch (spec.type) {
			case FORMAT_TYPE_LONG_LONG:
				num = va_arg(args, long long);
				break;
			case FORMAT_TYPE_ULONG:
				num = va_arg(args, unsigned long);
				break;
			case FORMAT_TYPE_LONG:
				num = va_arg(args, long);
				break;
			case FORMAT_TYPE_SIZE_T:
				if (spec.flags & SIGN)
					num = va_arg(args, ssize_t);
				else
					num = va_arg(args, size_t);
				break;
			case FORMAT_TYPE_PTRDIFF:
				num = va_arg(args, ptrdiff_t);
				break;
			case FORMAT_TYPE_UBYTE:
				num = (unsigned char) va_arg(args, int);
				break;
			case FORMAT_TYPE_BYTE:
				num = (signed char) va_arg(args, int);
				break;
			case FORMAT_TYPE_USHORT:
				num = (unsigned short) va_arg(args, int);
				break;
			case FORMAT_TYPE_SHORT:
				num = (short) va_arg(args, int);
				break;
			case FORMAT_TYPE_INT:
				num = (int) va_arg(args, int);
				break;
			default:
				num = va_arg(args, unsigned int);
			}

			str = number(str, end, num, spec);
		}
	}

out:
	if (size > 0) {
		if (str < end)
			*str = '\0';
		else
			end[-1] = '\0';
	}
	/* the trailing null byte doesn't count towards the total */
	return str-buf;
}


	/* the trailing null byte doesn't count towards the total */
	return str-buf;

}
EXPORT_SYMBOL(vsnprintf);

/**
 * vscnprintf - Format a string and place it in a buffer
 * @buf: The buffer to place the result into
 * @size: The size of the buffer, including the trailing null space
 * @fmt: The format string to use
 * @args: Arguments for the format string
 *
 * The return value is the number of characters which have been written into
 * the @buf not including the trailing '\0'. If @size is <= 0 the function
 * returns 0.
 *
 * Call this function if you are already dealing with a va_list.
 * You probably want scnprintf() instead.
 * the @buf not including the trailing '\0'. If @size is == 0 the function
 * returns 0.
 *
 * If you're not already dealing with a va_list consider using scnprintf().
 *
 * See the vsnprintf() documentation for format string extensions over C99.
 */
int vscnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
	int i;

	i=vsnprintf(buf,size,fmt,args);
	return (i >= size) ? (size - 1) : i;
}

	i = vsnprintf(buf, size, fmt, args);

	if (likely(i < size))
		return i;
	if (size != 0)
		return size - 1;
	return 0;
}
EXPORT_SYMBOL(vscnprintf);

/**
 * snprintf - Format a string and place it in a buffer
 * @buf: The buffer to place the result into
 * @size: The size of the buffer, including the trailing null space
 * @fmt: The format string to use
 * @...: Arguments for the format string
 *
 * The return value is the number of characters which would be
 * generated for the given input, excluding the trailing null,
 * as per ISO C99.  If the return is greater than or equal to
 * @size, the resulting string is truncated.
 */
int snprintf(char * buf, size_t size, const char *fmt, ...)
 *
 * See the vsnprintf() documentation for format string extensions over C99.
 */
int snprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	i=vsnprintf(buf,size,fmt,args);
	va_end(args);
	return i;
}

	i = vsnprintf(buf, size, fmt, args);
	va_end(args);

	return i;
}
EXPORT_SYMBOL(snprintf);

/**
 * scnprintf - Format a string and place it in a buffer
 * @buf: The buffer to place the result into
 * @size: The size of the buffer, including the trailing null space
 * @fmt: The format string to use
 * @...: Arguments for the format string
 *
 * The return value is the number of characters written into @buf not including
 * the trailing '\0'. If @size is <= 0 the function returns 0.
 */

int scnprintf(char * buf, size_t size, const char *fmt, ...)
 * the trailing '\0'. If @size is == 0 the function returns 0.
 */

int scnprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	i = vsnprintf(buf, size, fmt, args);
	va_end(args);
	return (i >= size) ? (size - 1) : i;
	i = vscnprintf(buf, size, fmt, args);
	va_end(args);

	return i;
}
EXPORT_SYMBOL(scnprintf);

/**
 * vsprintf - Format a string and place it in a buffer
 * @buf: The buffer to place the result into
 * @fmt: The format string to use
 * @args: Arguments for the format string
 *
 * The function returns the number of characters written
 * into @buf. Use vsnprintf() or vscnprintf() in order to avoid
 * buffer overflows.
 *
 * Call this function if you are already dealing with a va_list.
 * You probably want sprintf() instead.
 * If you're not already dealing with a va_list consider using sprintf().
 *
 * See the vsnprintf() documentation for format string extensions over C99.
 */
int vsprintf(char *buf, const char *fmt, va_list args)
{
	return vsnprintf(buf, INT_MAX, fmt, args);
}

EXPORT_SYMBOL(vsprintf);

/**
 * sprintf - Format a string and place it in a buffer
 * @buf: The buffer to place the result into
 * @fmt: The format string to use
 * @...: Arguments for the format string
 *
 * The function returns the number of characters written
 * into @buf. Use snprintf() or scnprintf() in order to avoid
 * buffer overflows.
 */
int sprintf(char * buf, const char *fmt, ...)
 *
 * See the vsnprintf() documentation for format string extensions over C99.
 */
int sprintf(char *buf, const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	i=vsnprintf(buf, INT_MAX, fmt, args);
	va_end(args);
	return i;
}

EXPORT_SYMBOL(sprintf);

	i = vsnprintf(buf, INT_MAX, fmt, args);
	va_end(args);

	return i;
}
EXPORT_SYMBOL(sprintf);

#ifdef CONFIG_BINARY_PRINTF
/*
 * bprintf service:
 * vbin_printf() - VA arguments to binary data
 * bstr_printf() - Binary data to text string
 */

/**
 * vbin_printf - Parse a format string and place args' binary value in a buffer
 * @bin_buf: The buffer to place args' binary value
 * @size: The size of the buffer(by words(32bits), not characters)
 * @fmt: The format string to use
 * @args: Arguments for the format string
 *
 * The format follows C99 vsnprintf, except %n is ignored, and its argument
 * is skipped.
 *
 * The return value is the number of words(32bits) which would be generated for
 * the given input.
 *
 * NOTE:
 * If the return value is greater than @size, the resulting bin_buf is NOT
 * valid for bstr_printf().
 */
int vbin_printf(u32 *bin_buf, size_t size, const char *fmt, va_list args)
{
	struct printf_spec spec = {0};
	char *str, *end;

	str = (char *)bin_buf;
	end = (char *)(bin_buf + size);

#define save_arg(type)							\
do {									\
	if (sizeof(type) == 8) {					\
		unsigned long long value;				\
		str = PTR_ALIGN(str, sizeof(u32));			\
		value = va_arg(args, unsigned long long);		\
		if (str + sizeof(type) <= end) {			\
			*(u32 *)str = *(u32 *)&value;			\
			*(u32 *)(str + 4) = *((u32 *)&value + 1);	\
		}							\
	} else {							\
		unsigned long value;					\
		str = PTR_ALIGN(str, sizeof(type));			\
		value = va_arg(args, int);				\
		if (str + sizeof(type) <= end)				\
			*(typeof(type) *)str = (type)value;		\
	}								\
	str += sizeof(type);						\
} while (0)

	while (*fmt) {
		int read = format_decode(fmt, &spec);

		fmt += read;

		switch (spec.type) {
		case FORMAT_TYPE_NONE:
		case FORMAT_TYPE_PERCENT_CHAR:
			break;
		case FORMAT_TYPE_INVALID:
			goto out;

		case FORMAT_TYPE_WIDTH:
		case FORMAT_TYPE_PRECISION:
			save_arg(int);
			break;

		case FORMAT_TYPE_CHAR:
			save_arg(char);
			break;

		case FORMAT_TYPE_STR: {
			const char *save_str = va_arg(args, char *);
			size_t len;

			if ((unsigned long)save_str > (unsigned long)-PAGE_SIZE
					|| (unsigned long)save_str < PAGE_SIZE)
				save_str = "(null)";
			len = strlen(save_str) + 1;
			if (str + len < end)
				memcpy(str, save_str, len);
			str += len;
			break;
		}

		case FORMAT_TYPE_PTR:
			save_arg(void *);
			/* skip all alphanumeric pointer suffixes */
			while (isalnum(*fmt))
				fmt++;
			break;

		default:
			switch (spec.type) {

			case FORMAT_TYPE_LONG_LONG:
				save_arg(long long);
				break;
			case FORMAT_TYPE_ULONG:
			case FORMAT_TYPE_LONG:
				save_arg(unsigned long);
				break;
			case FORMAT_TYPE_SIZE_T:
				save_arg(size_t);
				break;
			case FORMAT_TYPE_PTRDIFF:
				save_arg(ptrdiff_t);
				break;
			case FORMAT_TYPE_UBYTE:
			case FORMAT_TYPE_BYTE:
				save_arg(char);
				break;
			case FORMAT_TYPE_USHORT:
			case FORMAT_TYPE_SHORT:
				save_arg(short);
				break;
			default:
				save_arg(int);
			}
		}
	}

out:
	return (u32 *)(PTR_ALIGN(str, sizeof(u32))) - bin_buf;
#undef save_arg
}
EXPORT_SYMBOL_GPL(vbin_printf);

/**
 * bstr_printf - Format a string from binary arguments and place it in a buffer
 * @buf: The buffer to place the result into
 * @size: The size of the buffer, including the trailing null space
 * @fmt: The format string to use
 * @bin_buf: Binary arguments for the format string
 *
 * This function like C99 vsnprintf, but the difference is that vsnprintf gets
 * arguments from stack, and bstr_printf gets arguments from @bin_buf which is
 * a binary buffer that generated by vbin_printf.
 *
 * The format follows C99 vsnprintf, but has some extensions:
 *  see vsnprintf comment for details.
 *
 * The return value is the number of characters which would
 * be generated for the given input, excluding the trailing
 * '\0', as per ISO C99. If you want to have the exact
 * number of characters written into @buf as return value
 * (not including the trailing '\0'), use vscnprintf(). If the
 * return is greater than or equal to @size, the resulting
 * string is truncated.
 */
int bstr_printf(char *buf, size_t size, const char *fmt, const u32 *bin_buf)
{
	struct printf_spec spec = {0};
	char *str, *end;
	const char *args = (const char *)bin_buf;

	if (WARN_ON_ONCE(size > INT_MAX))
		return 0;

	str = buf;
	end = buf + size;

#define get_arg(type)							\
({									\
	typeof(type) value;						\
	if (sizeof(type) == 8) {					\
		args = PTR_ALIGN(args, sizeof(u32));			\
		*(u32 *)&value = *(u32 *)args;				\
		*((u32 *)&value + 1) = *(u32 *)(args + 4);		\
	} else {							\
		args = PTR_ALIGN(args, sizeof(type));			\
		value = *(typeof(type) *)args;				\
	}								\
	args += sizeof(type);						\
	value;								\
})

	/* Make sure end is always >= buf */
	if (end < buf) {
		end = ((void *)-1);
		size = end - buf;
	}

	while (*fmt) {
		const char *old_fmt = fmt;
		int read = format_decode(fmt, &spec);

		fmt += read;

		switch (spec.type) {
		case FORMAT_TYPE_NONE: {
			int copy = read;
			if (str < end) {
				if (copy > end - str)
					copy = end - str;
				memcpy(str, old_fmt, copy);
			}
			str += read;
			break;
		}

		case FORMAT_TYPE_WIDTH:
			spec.field_width = get_arg(int);
			break;

		case FORMAT_TYPE_PRECISION:
			spec.precision = get_arg(int);
			break;

		case FORMAT_TYPE_CHAR: {
			char c;

			if (!(spec.flags & LEFT)) {
				while (--spec.field_width > 0) {
					if (str < end)
						*str = ' ';
					++str;
				}
			}
			c = (unsigned char) get_arg(char);
			if (str < end)
				*str = c;
			++str;
			while (--spec.field_width > 0) {
				if (str < end)
					*str = ' ';
				++str;
			}
			break;
		}

		case FORMAT_TYPE_STR: {
			const char *str_arg = args;
			args += strlen(str_arg) + 1;
			str = string(str, end, (char *)str_arg, spec);
			break;
		}

		case FORMAT_TYPE_PTR:
			str = pointer(fmt, str, end, get_arg(void *), spec);
			while (isalnum(*fmt))
				fmt++;
			break;

		case FORMAT_TYPE_PERCENT_CHAR:
			if (str < end)
				*str = '%';
			++str;
			break;

		case FORMAT_TYPE_INVALID:
			goto out;

		default: {
			unsigned long long num;

			switch (spec.type) {

			case FORMAT_TYPE_LONG_LONG:
				num = get_arg(long long);
				break;
			case FORMAT_TYPE_ULONG:
			case FORMAT_TYPE_LONG:
				num = get_arg(unsigned long);
				break;
			case FORMAT_TYPE_SIZE_T:
				num = get_arg(size_t);
				break;
			case FORMAT_TYPE_PTRDIFF:
				num = get_arg(ptrdiff_t);
				break;
			case FORMAT_TYPE_UBYTE:
				num = get_arg(unsigned char);
				break;
			case FORMAT_TYPE_BYTE:
				num = get_arg(signed char);
				break;
			case FORMAT_TYPE_USHORT:
				num = get_arg(unsigned short);
				break;
			case FORMAT_TYPE_SHORT:
				num = get_arg(short);
				break;
			case FORMAT_TYPE_UINT:
				num = get_arg(unsigned int);
				break;
			default:
				num = get_arg(int);
			}

			str = number(str, end, num, spec);
		} /* default: */
		} /* switch(spec.type) */
	} /* while(*fmt) */

out:
	if (size > 0) {
		if (str < end)
			*str = '\0';
		else
			end[-1] = '\0';
	}

#undef get_arg

	/* the trailing null byte doesn't count towards the total */
	return str - buf;
}
EXPORT_SYMBOL_GPL(bstr_printf);

/**
 * bprintf - Parse a format string and place args' binary value in a buffer
 * @bin_buf: The buffer to place args' binary value
 * @size: The size of the buffer(by words(32bits), not characters)
 * @fmt: The format string to use
 * @...: Arguments for the format string
 *
 * The function returns the number of words(u32) written
 * into @bin_buf.
 */
int bprintf(u32 *bin_buf, size_t size, const char *fmt, ...)
{
	va_list args;
	int ret;

	va_start(args, fmt);
	ret = vbin_printf(bin_buf, size, fmt, args);
	va_end(args);

	return ret;
}
EXPORT_SYMBOL_GPL(bprintf);

#endif /* CONFIG_BINARY_PRINTF */

/**
 * vsscanf - Unformat a buffer into a list of arguments
 * @buf:	input buffer
 * @fmt:	format of buffer
 * @args:	arguments
 */
int vsscanf(const char * buf, const char * fmt, va_list args)
int vsscanf(const char *buf, const char *fmt, va_list args)
{
	const char *str = buf;
	char *next;
	char digit;
	int num = 0;
	int qualifier;
	int base;
	int field_width;
	int is_sign = 0;

	while(*fmt && *str) {
	u8 qualifier;
	unsigned int base;
	union {
		long long s;
		unsigned long long u;
	} val;
	s16 field_width;
	bool is_sign;

	while (*fmt) {
		/* skip any white space in format */
		/* white space in format matchs any amount of
		 * white space, including none, in the input.
		 */
		if (isspace(*fmt)) {
			while (isspace(*fmt))
				++fmt;
			while (isspace(*str))
				++str;
			fmt = skip_spaces(++fmt);
			str = skip_spaces(str);
		}

		/* anything that is not a conversion must match exactly */
		if (*fmt != '%' && *fmt) {
			if (*fmt++ != *str++)
				break;
			continue;
		}

		if (!*fmt)
			break;
		++fmt;
		

		/* skip this conversion.
		 * advance both strings to next white space
		 */
		if (*fmt == '*') {
			while (!isspace(*fmt) && *fmt)
			if (!*str)
				break;
			while (!isspace(*fmt) && *fmt != '%' && *fmt)
				fmt++;
			while (!isspace(*str) && *str)
				str++;
			continue;
		}

		/* get field width */
		field_width = -1;
		if (isdigit(*fmt))
			field_width = skip_atoi(&fmt);

		/* get conversion qualifier */
		qualifier = -1;
		if (*fmt == 'h' || *fmt == 'l' || *fmt == 'L' ||
		    *fmt == 'Z' || *fmt == 'z') {
		if (isdigit(*fmt)) {
			field_width = skip_atoi(&fmt);
			if (field_width <= 0)
				break;
		}

		/* get conversion qualifier */
		qualifier = -1;
		if (*fmt == 'h' || _tolower(*fmt) == 'l' ||
		    _tolower(*fmt) == 'z') {
			qualifier = *fmt++;
			if (unlikely(qualifier == *fmt)) {
				if (qualifier == 'h') {
					qualifier = 'H';
					fmt++;
				} else if (qualifier == 'l') {
					qualifier = 'L';
					fmt++;
				}
			}
		}
		base = 10;
		is_sign = 0;

		if (!*fmt || !*str)
			break;

		switch(*fmt++) {
		case 'c':
		{
			char *s = (char *) va_arg(args,char*);

		if (!*fmt)
			break;

		if (*fmt == 'n') {
			/* return number of characters read so far */
			*va_arg(args, int *) = str - buf;
			++fmt;
			continue;
		}

		if (!*str)
			break;

		base = 10;
		is_sign = false;

		switch (*fmt++) {
		case 'c':
		{
			char *s = (char *)va_arg(args, char*);
			if (field_width == -1)
				field_width = 1;
			do {
				*s++ = *str++;
			} while (--field_width > 0 && *str);
			num++;
		}
		continue;
		case 's':
		{
			char *s = (char *) va_arg(args, char *);
			if(field_width == -1)
				field_width = INT_MAX;
			/* first, skip leading white space in buffer */
			while (isspace(*str))
				str++;

			/* now copy until next white space */
			while (*str && !isspace(*str) && field_width--) {
				*s++ = *str++;
			}
			char *s = (char *)va_arg(args, char *);
			if (field_width == -1)
				field_width = SHRT_MAX;
			/* first, skip leading white space in buffer */
			str = skip_spaces(str);

			/* now copy until next white space */
			while (*str && !isspace(*str) && field_width--)
				*s++ = *str++;
			*s = '\0';
			num++;
		}
		continue;
		case 'n':
			/* return number of characters read so far */
		{
			int *i = (int *)va_arg(args,int*);
			*i = str - buf;
		}
		continue;
		case 'o':
			base = 8;
			break;
		case 'x':
		case 'X':
			base = 16;
			break;
		case 'i':
                        base = 0;
		case 'd':
			is_sign = 1;
			base = 0;
		case 'd':
			is_sign = true;
		case 'u':
			break;
		case '%':
			/* looking for '%' in str */
			if (*str++ != '%') 
			if (*str++ != '%')
				return num;
			continue;
		default:
			/* invalid format; stop here */
			return num;
		}

		/* have some sort of integer conversion.
		 * first, skip white space in buffer.
		 */
		while (isspace(*str))
			str++;
		str = skip_spaces(str);

		digit = *str;
		if (is_sign && digit == '-')
			digit = *(str + 1);

		if (!digit
                    || (base == 16 && !isxdigit(digit))
                    || (base == 10 && !isdigit(digit))
                    || (base == 8 && (!isdigit(digit) || digit > '7'))
                    || (base == 0 && !isdigit(digit)))
				break;

		switch(qualifier) {
		case 'H':	/* that's 'hh' in format */
			if (is_sign) {
				signed char *s = (signed char *) va_arg(args,signed char *);
				*s = (signed char) simple_strtol(str,&next,base);
			} else {
				unsigned char *s = (unsigned char *) va_arg(args, unsigned char *);
				*s = (unsigned char) simple_strtoul(str, &next, base);
			}
			break;
		case 'h':
			if (is_sign) {
				short *s = (short *) va_arg(args,short *);
				*s = (short) simple_strtol(str,&next,base);
			} else {
				unsigned short *s = (unsigned short *) va_arg(args, unsigned short *);
				*s = (unsigned short) simple_strtoul(str, &next, base);
			}
			break;
		case 'l':
			if (is_sign) {
				long *l = (long *) va_arg(args,long *);
				*l = simple_strtol(str,&next,base);
			} else {
				unsigned long *l = (unsigned long*) va_arg(args,unsigned long*);
				*l = simple_strtoul(str,&next,base);
			}
			break;
		case 'L':
			if (is_sign) {
				long long *l = (long long*) va_arg(args,long long *);
				*l = simple_strtoll(str,&next,base);
			} else {
				unsigned long long *l = (unsigned long long*) va_arg(args,unsigned long long*);
				*l = simple_strtoull(str,&next,base);
			}
			break;
		case 'Z':
		case 'z':
		{
			size_t *s = (size_t*) va_arg(args,size_t*);
			*s = (size_t) simple_strtoul(str,&next,base);
		}
		break;
		default:
			if (is_sign) {
				int *i = (int *) va_arg(args, int*);
				*i = (int) simple_strtol(str,&next,base);
			} else {
				unsigned int *i = (unsigned int*) va_arg(args, unsigned int*);
				*i = (unsigned int) simple_strtoul(str,&next,base);
			}
		    || (base == 16 && !isxdigit(digit))
		    || (base == 10 && !isdigit(digit))
		    || (base == 8 && (!isdigit(digit) || digit > '7'))
		    || (base == 0 && !isdigit(digit)))
			break;

		if (is_sign)
			val.s = qualifier != 'L' ?
				simple_strtol(str, &next, base) :
				simple_strtoll(str, &next, base);
		else
			val.u = qualifier != 'L' ?
				simple_strtoul(str, &next, base) :
				simple_strtoull(str, &next, base);

		if (field_width > 0 && next - str > field_width) {
			if (base == 0)
				_parse_integer_fixup_radix(str, &base);
			while (next - str > field_width) {
				if (is_sign)
					val.s = div_s64(val.s, base);
				else
					val.u = div_u64(val.u, base);
				--next;
			}
		}

		switch (qualifier) {
		case 'H':	/* that's 'hh' in format */
			if (is_sign)
				*va_arg(args, signed char *) = val.s;
			else
				*va_arg(args, unsigned char *) = val.u;
			break;
		case 'h':
			if (is_sign)
				*va_arg(args, short *) = val.s;
			else
				*va_arg(args, unsigned short *) = val.u;
			break;
		case 'l':
			if (is_sign)
				*va_arg(args, long *) = val.s;
			else
				*va_arg(args, unsigned long *) = val.u;
			break;
		case 'L':
			if (is_sign)
				*va_arg(args, long long *) = val.s;
			else
				*va_arg(args, unsigned long long *) = val.u;
			break;
		case 'Z':
		case 'z':
			*va_arg(args, size_t *) = val.u;
			break;
		default:
			if (is_sign)
				*va_arg(args, int *) = val.s;
			else
				*va_arg(args, unsigned int *) = val.u;
			break;
		}
		num++;

		if (!next)
			break;
		str = next;
	}

	/*
	 * Now we've come all the way through so either the input string or the
	 * format ended. In the former case, there can be a %n at the current
	 * position in the format that needs to be filled.
	 */
	if (*fmt == '%' && *(fmt + 1) == 'n') {
		int *p = (int *)va_arg(args, int *);
		*p = str - buf;
	}

	return num;
}

	return num;
}
EXPORT_SYMBOL(vsscanf);

/**
 * sscanf - Unformat a buffer into a list of arguments
 * @buf:	input buffer
 * @fmt:	formatting of buffer
 * @...:	resulting arguments
 */
int sscanf(const char * buf, const char * fmt, ...)
int sscanf(const char *buf, const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args,fmt);
	i = vsscanf(buf,fmt,args);
	va_end(args);
	return i;
}

	va_start(args, fmt);
	i = vsscanf(buf, fmt, args);
	va_end(args);

	return i;
}
EXPORT_SYMBOL(sscanf);
