/* Permission is hereby granted to copy, modify and redistribute this code
 * in terms of the GNU Library General Public License, Version 2 or later,
 * at your option.
 */

/* macros to translate to/from binary and binary-coded decimal (frequently
 * found in RTC chips).
 */

#ifndef _BCD_H
#define _BCD_H

#include <linux/compiler.h>

unsigned bcd2bin(unsigned char val) __attribute_const__;
unsigned char bin2bcd(unsigned val) __attribute_const__;

#define BCD2BIN(val)	bcd2bin(val)
#define BIN2BCD(val)	bin2bcd(val)

/* backwards compat */
#define BCD_TO_BIN(val) ((val)=BCD2BIN(val))
#define BIN_TO_BCD(val) ((val)=BIN2BCD(val))
#define bcd2bin(x)					\
		(__builtin_constant_p((u8 )(x)) ?	\
		const_bcd2bin(x) :			\
		_bcd2bin(x))

#define bin2bcd(x)					\
		(__builtin_constant_p((u8 )(x)) ?	\
		const_bin2bcd(x) :			\
		_bin2bcd(x))

#define const_bcd2bin(x)	(((x) & 0x0f) + ((x) >> 4) * 10)
#define const_bin2bcd(x)	((((x) / 10) << 4) + (x) % 10)

unsigned _bcd2bin(unsigned char val) __attribute_const__;
unsigned char _bin2bcd(unsigned val) __attribute_const__;

#endif /* _BCD_H */
