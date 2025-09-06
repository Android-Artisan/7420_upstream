/*
 * File:         arch/blackfin/mach-common/cache.S
 * Based on:
 * Author:       LG Soft India
 *
 * Created:
 * Description:  cache control support
 *
 * Modified:
 *               Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/linkage.h>
#include <asm/cplb.h>
#include <asm/entry.h>
#include <asm/blackfin.h>
#include <asm/cache.h>

.text

/*
 * blackfin_cache_flush_range(start, end)
 * Invalidate all cache lines assocoiated with this
 * area of memory.
 *
 * start:	Start address
 * end:		End address
 */
ENTRY(_blackfin_icache_flush_range)
	R2 = -L1_CACHE_BYTES;
	R2 = R0 & R2;
	P0 = R2;
	P1 = R1;
	CSYNC(R3);
	IFLUSH [P0];
1:
	IFLUSH [P0++];
	CC = P0 < P1 (iu);
	IF CC JUMP 1b (bp);
	IFLUSH [P0];
	SSYNC(R3);
	RTS;
ENDPROC(_blackfin_icache_flush_range)

/*
 * blackfin_icache_dcache_flush_range(start, end)
 * FLUSH all cache lines assocoiated with this
 * area of memory.
 *
 * start:	Start address
 * end:		End address
 */

ENTRY(_blackfin_icache_dcache_flush_range)
	R2 = -L1_CACHE_BYTES;
	R2 = R0 & R2;
	P0 = R2;
	P1 = R1;
	CSYNC(R3);
	IFLUSH [P0];
1:
	FLUSH [P0];
	IFLUSH [P0++];
	CC = P0 < P1 (iu);
	IF CC JUMP 1b (bp);
	IFLUSH [P0];
	FLUSH [P0];
	SSYNC(R3);
	RTS;
ENDPROC(_blackfin_icache_dcache_flush_range)

/* Throw away all D-cached data in specified region without any obligation to
 * write them back. However, we must clean the D-cached entries around the
 * boundaries of the start and/or end address is not cache aligned.
 *
 * Start: start address,
 * end  : end address.
 */

ENTRY(_blackfin_dcache_invalidate_range)
	R2 = -L1_CACHE_BYTES;
	R2 = R0 & R2;
	P0 = R2;
	P1 = R1;
	CSYNC(R3);
	FLUSHINV[P0];
1:
	FLUSHINV[P0++];
	CC = P0 < P1 (iu);
	IF CC JUMP 1b (bp);

	/* If the data crosses a cache line, then we'll be pointing to
	 * the last cache line, but won't have flushed/invalidated it yet,
	 * so do one more.
	 */
	FLUSHINV[P0];
	SSYNC(R3);
	RTS;
ENDPROC(_blackfin_dcache_invalidate_range)

ENTRY(_blackfin_dcache_flush_range)
	R2 = -L1_CACHE_BYTES;
	R2 = R0 & R2;
	P0 = R2;
	P1 = R1;
	CSYNC(R3);
	FLUSH[P0];
1:
	FLUSH[P0++];
	CC = P0 < P1 (iu);
	IF CC JUMP 1b (bp);

	/* If the data crosses a cache line, then we'll be pointing to
	 * the last cache line, but won't have flushed it yet, so do
	 * one more.
	 */
	FLUSH[P0];
	SSYNC(R3);
	RTS;
ENDPROC(_blackfin_dcache_flush_range)

ENTRY(_blackfin_dflush_page)
	P1 = 1 << (PAGE_SHIFT - L1_CACHE_SHIFT);
	P0 = R0;
	CSYNC(R3);
	FLUSH[P0];
	LSETUP (.Lfl1, .Lfl1) LC0 = P1;
.Lfl1:	FLUSH [P0++];
	SSYNC(R3);
	RTS;
 * Blackfin cache control code
 *
 * Copyright 2004-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/linkage.h>
#include <asm/blackfin.h>
#include <asm/cache.h>
#include <asm/page.h>

/* 05000443 - IFLUSH cannot be last instruction in hardware loop */
#if ANOMALY_05000443
# define BROK_FLUSH_INST "IFLUSH"
#else
# define BROK_FLUSH_INST "no anomaly! yeah!"
#endif

/* Since all L1 caches work the same way, we use the same method for flushing
 * them.  Only the actual flush instruction differs.  We write this in asm as
 * GCC can be hard to coax into writing nice hardware loops.
 *
 * Also, we assume the following register setup:
 * R0 = start address
 * R1 = end address
 */
.macro do_flush flushins:req label

	R2 = -L1_CACHE_BYTES;

	/* start = (start & -L1_CACHE_BYTES) */
	R0 = R0 & R2;

	/* end = ((end - 1) & -L1_CACHE_BYTES) + L1_CACHE_BYTES; */
	R1 += -1;
	R1 = R1 & R2;
	R1 += L1_CACHE_BYTES;

	/* count = (end - start) >> L1_CACHE_SHIFT */
	R2 = R1 - R0;
	R2 >>= L1_CACHE_SHIFT;
	P1 = R2;

.ifnb \label
\label :
.endif
	P0 = R0;

	LSETUP (1f, 2f) LC1 = P1;
1:
.ifeqs "\flushins", BROK_FLUSH_INST
	\flushins [P0++];
	nop;
	nop;
2:	nop;
.else
2:	\flushins [P0++];
.endif

	RTS;
.endm

#ifdef CONFIG_ICACHE_FLUSH_L1
.section .l1.text
#else
.text
#endif

/* Invalidate all instruction cache lines assocoiated with this memory area */
#ifdef CONFIG_SMP
# define _blackfin_icache_flush_range _blackfin_icache_flush_range_l1
#endif
ENTRY(_blackfin_icache_flush_range)
	do_flush IFLUSH
ENDPROC(_blackfin_icache_flush_range)

#ifdef CONFIG_SMP
.text
# undef _blackfin_icache_flush_range
ENTRY(_blackfin_icache_flush_range)
	p0.L = LO(DSPID);
	p0.H = HI(DSPID);
	r3 = [p0];
	r3 = r3.b (z);
	p2 = r3;
	p0.L = _blackfin_iflush_l1_entry;
	p0.H = _blackfin_iflush_l1_entry;
	p0 = p0 + (p2 << 2);
	p1 = [p0];
	jump (p1);
ENDPROC(_blackfin_icache_flush_range)
#endif

#ifdef CONFIG_DCACHE_FLUSH_L1
.section .l1.text
#else
.text
#endif

/* Throw away all D-cached data in specified region without any obligation to
 * write them back.  Since the Blackfin ISA does not have an "invalidate"
 * instruction, we use flush/invalidate.  Perhaps as a speed optimization we
 * could bang on the DTEST MMRs ...
 */
ENTRY(_blackfin_dcache_invalidate_range)
	do_flush FLUSHINV
ENDPROC(_blackfin_dcache_invalidate_range)

/* Flush all data cache lines assocoiated with this memory area */
ENTRY(_blackfin_dcache_flush_range)
	do_flush FLUSH, .Ldfr
ENDPROC(_blackfin_dcache_flush_range)

/* Our headers convert the page structure to an address, so just need to flush
 * its contents like normal.  We know the start address is page aligned (which
 * greater than our cache alignment), as is the end address.  So just jump into
 * the middle of the dcache flush function.
 */
ENTRY(_blackfin_dflush_page)
	P1 = 1 << (PAGE_SHIFT - L1_CACHE_SHIFT);
	jump .Ldfr;
ENDPROC(_blackfin_dflush_page)
