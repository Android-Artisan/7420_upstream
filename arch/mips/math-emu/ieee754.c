/* ieee754 floating point arithmetic
 * single and double precision
 *
 * BUGS
 * not much dp done
 * doesn't generate IEEE754_INEXACT
 *
 */
/*
 * MIPS floating point support
 * Copyright (C) 1994-2000 Algorithmics Ltd.
 * http://www.algor.co.uk
 *
 * ########################################################################
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * ########################################################################
 */


#include "ieee754int.h"
#include "ieee754sp.h"
#include "ieee754dp.h"

#define DP_EBIAS	1023
#define DP_EMIN		(-1022)
#define DP_EMAX		1023

#define SP_EBIAS	127
#define SP_EMIN		(-126)
#define SP_EMAX		127

/* special constants
*/


#if (defined(BYTE_ORDER) && BYTE_ORDER == LITTLE_ENDIAN) || defined(__MIPSEL__)
#define SPSTR(s, b, m) {m, b, s}
#define DPSTR(s, b, mh, ml) {ml, mh, b, s}
#endif

#ifdef __MIPSEB__
#define SPSTR(s, b, m) {s, b, m}
#define DPSTR(s, b, mh, ml) {s, b, mh, ml}
#endif

const struct ieee754dp_konst __ieee754dp_spcvals[] = {
	DPSTR(0, DP_EMIN - 1 + DP_EBIAS, 0, 0),	/* + zero   */
	DPSTR(1, DP_EMIN - 1 + DP_EBIAS, 0, 0),	/* - zero   */
	DPSTR(0, DP_EBIAS, 0, 0),	/* + 1.0   */
	DPSTR(1, DP_EBIAS, 0, 0),	/* - 1.0   */
	DPSTR(0, 3 + DP_EBIAS, 0x40000, 0),	/* + 10.0   */
	DPSTR(1, 3 + DP_EBIAS, 0x40000, 0),	/* - 10.0   */
	DPSTR(0, DP_EMAX + 1 + DP_EBIAS, 0, 0),	/* + infinity */
	DPSTR(1, DP_EMAX + 1 + DP_EBIAS, 0, 0),	/* - infinity */
	DPSTR(0, DP_EMAX+1+DP_EBIAS, 0x7FFFF, 0xFFFFFFFF), /* + indef quiet Nan */
	DPSTR(0, DP_EMAX + DP_EBIAS, 0xFFFFF, 0xFFFFFFFF),	/* + max */
	DPSTR(1, DP_EMAX + DP_EBIAS, 0xFFFFF, 0xFFFFFFFF),	/* - max */
	DPSTR(0, DP_EMIN + DP_EBIAS, 0, 0),	/* + min normal */
	DPSTR(1, DP_EMIN + DP_EBIAS, 0, 0),	/* - min normal */
	DPSTR(0, DP_EMIN - 1 + DP_EBIAS, 0, 1),	/* + min denormal */
	DPSTR(1, DP_EMIN - 1 + DP_EBIAS, 0, 1),	/* - min denormal */
	DPSTR(0, 31 + DP_EBIAS, 0, 0),	/* + 1.0e31 */
	DPSTR(0, 63 + DP_EBIAS, 0, 0),	/* + 1.0e63 */
};

const struct ieee754sp_konst __ieee754sp_spcvals[] = {
	SPSTR(0, SP_EMIN - 1 + SP_EBIAS, 0),	/* + zero   */
	SPSTR(1, SP_EMIN - 1 + SP_EBIAS, 0),	/* - zero   */
	SPSTR(0, SP_EBIAS, 0),	/* + 1.0   */
	SPSTR(1, SP_EBIAS, 0),	/* - 1.0   */
	SPSTR(0, 3 + SP_EBIAS, 0x200000),	/* + 10.0   */
	SPSTR(1, 3 + SP_EBIAS, 0x200000),	/* - 10.0   */
	SPSTR(0, SP_EMAX + 1 + SP_EBIAS, 0),	/* + infinity */
	SPSTR(1, SP_EMAX + 1 + SP_EBIAS, 0),	/* - infinity */
	SPSTR(0, SP_EMAX+1+SP_EBIAS, 0x3FFFFF),     /* + indef quiet Nan  */
	SPSTR(0, SP_EMAX + SP_EBIAS, 0x7FFFFF),	/* + max normal */
	SPSTR(1, SP_EMAX + SP_EBIAS, 0x7FFFFF),	/* - max normal */
	SPSTR(0, SP_EMIN + SP_EBIAS, 0),	/* + min normal */
	SPSTR(1, SP_EMIN + SP_EBIAS, 0),	/* - min normal */
	SPSTR(0, SP_EMIN - 1 + SP_EBIAS, 1),	/* + min denormal */
	SPSTR(1, SP_EMIN - 1 + SP_EBIAS, 1),	/* - min denormal */
	SPSTR(0, 31 + SP_EBIAS, 0),	/* + 1.0e31 */
	SPSTR(0, 63 + SP_EBIAS, 0),	/* + 1.0e63 */
};


int ieee754si_xcpt(int r, const char *op, ...)
{
	struct ieee754xctx ax;

	if (!TSTX())
		return r;
	ax.op = op;
	ax.rt = IEEE754_RT_SI;
	ax.rv.si = r;
	va_start(ax.ap, op);
	ieee754_xcpt(&ax);
	va_end(ax.ap);
	return ax.rv.si;
}

s64 ieee754di_xcpt(s64 r, const char *op, ...)
{
	struct ieee754xctx ax;

	if (!TSTX())
		return r;
	ax.op = op;
	ax.rt = IEEE754_RT_DI;
	ax.rv.di = r;
	va_start(ax.ap, op);
	ieee754_xcpt(&ax);
	va_end(ax.ap);
	return ax.rv.di;
}
 *  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#include <linux/compiler.h>

#include "ieee754.h"
#include "ieee754sp.h"
#include "ieee754dp.h"

/*
 * Special constants
 */

/*
 * Older GCC requires the inner braces for initialization of union ieee754dp's
 * anonymous struct member.  Without an error will result.
 */
#define xPCNST(s, b, m, ebias)						\
{									\
	{								\
		.sign	= (s),						\
		.bexp	= (b) + ebias,					\
		.mant	= (m)						\
	}								\
}

#define DPCNST(s, b, m)							\
	xPCNST(s, b, m, DP_EBIAS)

const union ieee754dp __ieee754dp_spcvals[] = {
	DPCNST(0, DP_EMIN - 1, 0x0000000000000ULL),	/* + zero   */
	DPCNST(1, DP_EMIN - 1, 0x0000000000000ULL),	/* - zero   */
	DPCNST(0, 0,	       0x0000000000000ULL),	/* + 1.0   */
	DPCNST(1, 0,	       0x0000000000000ULL),	/* - 1.0   */
	DPCNST(0, 3,           0x4000000000000ULL),	/* + 10.0   */
	DPCNST(1, 3,           0x4000000000000ULL),	/* - 10.0   */
	DPCNST(0, DP_EMAX + 1, 0x0000000000000ULL),	/* + infinity */
	DPCNST(1, DP_EMAX + 1, 0x0000000000000ULL),	/* - infinity */
	DPCNST(0, DP_EMAX + 1, 0x7FFFFFFFFFFFFULL),	/* + indef quiet Nan */
	DPCNST(0, DP_EMAX,     0xFFFFFFFFFFFFFULL),	/* + max */
	DPCNST(1, DP_EMAX,     0xFFFFFFFFFFFFFULL),	/* - max */
	DPCNST(0, DP_EMIN,     0x0000000000000ULL),	/* + min normal */
	DPCNST(1, DP_EMIN,     0x0000000000000ULL),	/* - min normal */
	DPCNST(0, DP_EMIN - 1, 0x0000000000001ULL),	/* + min denormal */
	DPCNST(1, DP_EMIN - 1, 0x0000000000001ULL),	/* - min denormal */
	DPCNST(0, 31,          0x0000000000000ULL),	/* + 1.0e31 */
	DPCNST(0, 63,          0x0000000000000ULL),	/* + 1.0e63 */
};

#define SPCNST(s, b, m)							\
	xPCNST(s, b, m, SP_EBIAS)

const union ieee754sp __ieee754sp_spcvals[] = {
	SPCNST(0, SP_EMIN - 1, 0x000000),	/* + zero   */
	SPCNST(1, SP_EMIN - 1, 0x000000),	/* - zero   */
	SPCNST(0, 0,	       0x000000),	/* + 1.0   */
	SPCNST(1, 0,	       0x000000),	/* - 1.0   */
	SPCNST(0, 3,	       0x200000),	/* + 10.0   */
	SPCNST(1, 3,	       0x200000),	/* - 10.0   */
	SPCNST(0, SP_EMAX + 1, 0x000000),	/* + infinity */
	SPCNST(1, SP_EMAX + 1, 0x000000),	/* - infinity */
	SPCNST(0, SP_EMAX + 1, 0x3FFFFF),	/* + indef quiet Nan  */
	SPCNST(0, SP_EMAX,     0x7FFFFF),	/* + max normal */
	SPCNST(1, SP_EMAX,     0x7FFFFF),	/* - max normal */
	SPCNST(0, SP_EMIN,     0x000000),	/* + min normal */
	SPCNST(1, SP_EMIN,     0x000000),	/* - min normal */
	SPCNST(0, SP_EMIN - 1, 0x000001),	/* + min denormal */
	SPCNST(1, SP_EMIN - 1, 0x000001),	/* - min denormal */
	SPCNST(0, 31,	       0x000000),	/* + 1.0e31 */
	SPCNST(0, 63,	       0x000000),	/* + 1.0e63 */
};
