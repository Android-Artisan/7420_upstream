/*
 * This file contains assembly-language implementations
 * of IP-style 1's complement checksum routines.
 *	
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 * Severely hacked about by Paul Mackerras (paulus@cs.anu.edu.au).
 */

#include <linux/sys.h>
#include <asm/processor.h>
#include <asm/errno.h>
#include <asm/ppc_asm.h>

/*
 * ip_fast_csum(r3=buf, r4=len) -- Optimized for IP header
 * len is in words and is always >= 5.
 *
 * In practice len == 5, but this is not guaranteed.  So this code does not
 * attempt to use doubleword instructions.
 */
_GLOBAL(ip_fast_csum)
	lwz	r0,0(r3)
	lwzu	r5,4(r3)
	addic.	r4,r4,-2
	addc	r0,r0,r5
	mtctr	r4
	blelr-
1:	lwzu	r4,4(r3)
	adde	r0,r0,r4
	bdnz	1b
	addze	r0,r0		/* add in final carry */
        rldicl  r4,r0,32,0      /* fold two 32-bit halves together */
        add     r0,r0,r4
        srdi    r0,r0,32
	rlwinm	r3,r0,16,0,31	/* fold two halves together */
	add	r3,r0,r3
	not	r3,r3
	srwi	r3,r3,16
	blr

/*
 * Compute checksum of TCP or UDP pseudo-header:
 *   csum_tcpudp_magic(r3=saddr, r4=daddr, r5=len, r6=proto, r7=sum)
 * No real gain trying to do this specially for 64 bit, but
 * the 32 bit addition may spill into the upper bits of
 * the doubleword so we still must fold it down from 64.
 */	
_GLOBAL(csum_tcpudp_magic)
	rlwimi	r5,r6,16,0,15	/* put proto in upper half of len */
	addc	r0,r3,r4	/* add 4 32-bit words together */
	adde	r0,r0,r5
	adde	r0,r0,r7
        rldicl  r4,r0,32,0      /* fold 64 bit value */
        add     r0,r4,r0
        srdi    r0,r0,32
	rlwinm	r3,r0,16,0,31	/* fold two halves together */
	add	r3,r0,r3
	not	r3,r3
	srwi	r3,r3,16
	blr

/*
 * Computes the checksum of a memory block at buff, length len,
 * and adds in "sum" (32-bit).
 *
 * This code assumes at least halfword alignment, though the length
 * can be any number of bytes.  The sum is accumulated in r5.
 *
 * csum_partial(r3=buff, r4=len, r5=sum)
 */
_GLOBAL(csum_partial)
        subi	r3,r3,8		/* we'll offset by 8 for the loads */
        srdi.	r6,r4,3         /* divide by 8 for doubleword count */
        addic   r5,r5,0         /* clear carry */
        beq	3f              /* if we're doing < 8 bytes */
        andi.	r0,r3,2         /* aligned on a word boundary already? */
        beq+	1f
        lhz     r6,8(r3)        /* do 2 bytes to get aligned */
        addi    r3,r3,2
        subi    r4,r4,2
        addc    r5,r5,r6
        srdi.   r6,r4,3         /* recompute number of doublewords */
        beq     3f              /* any left? */
1:      mtctr   r6
2:      ldu     r6,8(r3)        /* main sum loop */
        adde    r5,r5,r6
        bdnz    2b
        andi.	r4,r4,7         /* compute bytes left to sum after doublewords */
3:	cmpwi	0,r4,4		/* is at least a full word left? */
	blt	4f
	lwz	r6,8(r3)	/* sum this word */
	addi	r3,r3,4
	subi	r4,r4,4
	adde	r5,r5,r6
4:	cmpwi	0,r4,2		/* is at least a halfword left? */
        blt+	5f
        lhz     r6,8(r3)        /* sum this halfword */
        addi    r3,r3,2
        subi    r4,r4,2
        adde    r5,r5,r6
5:	cmpwi	0,r4,1		/* is at least a byte left? */
        bne+    6f
        lbz     r6,8(r3)        /* sum this byte */
        slwi    r6,r6,8         /* this byte is assumed to be the upper byte of a halfword */
        adde    r5,r5,r6
6:      addze	r5,r5		/* add in final carry */
	rldicl  r4,r5,32,0      /* fold two 32-bit halves together */
        add     r3,r4,r5
        srdi    r3,r3,32
        blr
 * Computes the checksum of a memory block at buff, length len,
 * and adds in "sum" (32-bit).
 *
 * csum_partial(r3=buff, r4=len, r5=sum)
 */
_GLOBAL(csum_partial)
	addic	r0,r5,0			/* clear carry */

	srdi.	r6,r4,3			/* less than 8 bytes? */
	beq	.Lcsum_tail_word

	/*
	 * If only halfword aligned, align to a double word. Since odd
	 * aligned addresses should be rare and they would require more
	 * work to calculate the correct checksum, we ignore that case
	 * and take the potential slowdown of unaligned loads.
	 */
	rldicl. r6,r3,64-1,64-2		/* r6 = (r3 & 0x3) >> 1 */
	beq	.Lcsum_aligned

	li	r7,4
	sub	r6,r7,r6
	mtctr	r6

1:
	lhz	r6,0(r3)		/* align to doubleword */
	subi	r4,r4,2
	addi	r3,r3,2
	adde	r0,r0,r6
	bdnz	1b

.Lcsum_aligned:
	/*
	 * We unroll the loop such that each iteration is 64 bytes with an
	 * entry and exit limb of 64 bytes, meaning a minimum size of
	 * 128 bytes.
	 */
	srdi.	r6,r4,7
	beq	.Lcsum_tail_doublewords		/* len < 128 */

	srdi	r6,r4,6
	subi	r6,r6,1
	mtctr	r6

	stdu	r1,-STACKFRAMESIZE(r1)
	std	r14,STK_REG(R14)(r1)
	std	r15,STK_REG(R15)(r1)
	std	r16,STK_REG(R16)(r1)

	ld	r6,0(r3)
	ld	r9,8(r3)

	ld	r10,16(r3)
	ld	r11,24(r3)

	/*
	 * On POWER6 and POWER7 back to back addes take 2 cycles because of
	 * the XER dependency. This means the fastest this loop can go is
	 * 16 cycles per iteration. The scheduling of the loop below has
	 * been shown to hit this on both POWER6 and POWER7.
	 */
	.align 5
2:
	adde	r0,r0,r6
	ld	r12,32(r3)
	ld	r14,40(r3)

	adde	r0,r0,r9
	ld	r15,48(r3)
	ld	r16,56(r3)
	addi	r3,r3,64

	adde	r0,r0,r10

	adde	r0,r0,r11

	adde	r0,r0,r12

	adde	r0,r0,r14

	adde	r0,r0,r15
	ld	r6,0(r3)
	ld	r9,8(r3)

	adde	r0,r0,r16
	ld	r10,16(r3)
	ld	r11,24(r3)
	bdnz	2b


	adde	r0,r0,r6
	ld	r12,32(r3)
	ld	r14,40(r3)

	adde	r0,r0,r9
	ld	r15,48(r3)
	ld	r16,56(r3)
	addi	r3,r3,64

	adde	r0,r0,r10
	adde	r0,r0,r11
	adde	r0,r0,r12
	adde	r0,r0,r14
	adde	r0,r0,r15
	adde	r0,r0,r16

	ld	r14,STK_REG(R14)(r1)
	ld	r15,STK_REG(R15)(r1)
	ld	r16,STK_REG(R16)(r1)
	addi	r1,r1,STACKFRAMESIZE

	andi.	r4,r4,63

.Lcsum_tail_doublewords:		/* Up to 127 bytes to go */
	srdi.	r6,r4,3
	beq	.Lcsum_tail_word

	mtctr	r6
3:
	ld	r6,0(r3)
	addi	r3,r3,8
	adde	r0,r0,r6
	bdnz	3b

	andi.	r4,r4,7

.Lcsum_tail_word:			/* Up to 7 bytes to go */
	srdi.	r6,r4,2
	beq	.Lcsum_tail_halfword

	lwz	r6,0(r3)
	addi	r3,r3,4
	adde	r0,r0,r6
	subi	r4,r4,4

.Lcsum_tail_halfword:			/* Up to 3 bytes to go */
	srdi.	r6,r4,1
	beq	.Lcsum_tail_byte

	lhz	r6,0(r3)
	addi	r3,r3,2
	adde	r0,r0,r6
	subi	r4,r4,2

.Lcsum_tail_byte:			/* Up to 1 byte to go */
	andi.	r6,r4,1
	beq	.Lcsum_finish

	lbz	r6,0(r3)
	sldi	r9,r6,8			/* Pad the byte out to 16 bits */
	adde	r0,r0,r9

.Lcsum_finish:
	addze	r0,r0			/* add in final carry */
	rldicl	r4,r0,32,0		/* fold two 32 bit halves together */
	add	r3,r4,r0
	srdi	r3,r3,32
	blr


	.macro srcnr
100:
	.section __ex_table,"a"
	.align 3
	.llong 100b,.Lsrc_error_nr
	.previous
	.endm

	.macro source
150:
	.section __ex_table,"a"
	.align 3
	.llong 150b,.Lsrc_error
	.previous
	.endm

	.macro dstnr
200:
	.section __ex_table,"a"
	.align 3
	.llong 200b,.Ldest_error_nr
	.previous
	.endm

	.macro dest
250:
	.section __ex_table,"a"
	.align 3
	.llong 250b,.Ldest_error
	.previous
	.endm

/*
 * Computes the checksum of a memory block at src, length len,
 * and adds in "sum" (32-bit), while copying the block to dst.
 * If an access exception occurs on src or dst, it stores -EFAULT
 * to *src_err or *dst_err respectively, and (for an error on
 * src) zeroes the rest of dst.
 *
 * This code needs to be reworked to take advantage of 64 bit sum+copy.
 * However, due to tokenring halfword alignment problems this will be very
 * tricky.  For now we'll leave it until we instrument it somehow.
 * to *src_err or *dst_err respectively. The caller must take any action
 * required in this case (zeroing memory, recalculating partial checksum etc).
 *
 * csum_partial_copy_generic(r3=src, r4=dst, r5=len, r6=sum, r7=src_err, r8=dst_err)
 */
_GLOBAL(csum_partial_copy_generic)
	addic	r0,r6,0
	subi	r3,r3,4
	subi	r4,r4,4
	srwi.	r6,r5,2
	beq	3f		/* if we're doing < 4 bytes */
	andi.	r9,r4,2		/* Align dst to longword boundary */
	beq+	1f
81:	lhz	r6,4(r3)	/* do 2 bytes to get aligned */
	addi	r3,r3,2
	subi	r5,r5,2
91:	sth	r6,4(r4)
	addi	r4,r4,2
	addc	r0,r0,r6
	srwi.	r6,r5,2		/* # words to do */
	beq	3f
1:	mtctr	r6
82:	lwzu	r6,4(r3)	/* the bdnz has zero overhead, so it should */
92:	stwu	r6,4(r4)	/* be unnecessary to unroll this loop */
	adde	r0,r0,r6
	bdnz	82b
	andi.	r5,r5,3
3:	cmpwi	0,r5,2
	blt+	4f
83:	lhz	r6,4(r3)
	addi	r3,r3,2
	subi	r5,r5,2
93:	sth	r6,4(r4)
	addi	r4,r4,2
	adde	r0,r0,r6
4:	cmpwi	0,r5,1
	bne+	5f
84:	lbz	r6,4(r3)
94:	stb	r6,4(r4)
	slwi	r6,r6,8		/* Upper byte of word */
	adde	r0,r0,r6
5:	addze	r3,r0		/* add in final carry (unlikely with 64-bit regs) */
        rldicl  r4,r3,32,0      /* fold 64 bit value */
        add     r3,r4,r3
        srdi    r3,r3,32
	blr

/* These shouldn't go in the fixup section, since that would
   cause the ex_table addresses to get out of order. */

	.globl src_error_1
src_error_1:
	li	r6,0
	subi	r5,r5,2
95:	sth	r6,4(r4)
	addi	r4,r4,2
	srwi.	r6,r5,2
	beq	3f
	mtctr	r6
	.globl src_error_2
src_error_2:
	li	r6,0
96:	stwu	r6,4(r4)
	bdnz	96b
3:	andi.	r5,r5,3
	beq	src_error
	.globl src_error_3
src_error_3:
	li	r6,0
	mtctr	r5
	addi	r4,r4,3
97:	stbu	r6,1(r4)
	bdnz	97b
	.globl src_error
src_error:
	cmpdi	0,r7,0
	beq	1f
	li	r6,-EFAULT
	stw	r6,0(r7)
1:	addze	r3,r0
	blr

	.globl dst_error
dst_error:
	cmpdi	0,r8,0
	beq	1f
	li	r6,-EFAULT
	stw	r6,0(r8)
1:	addze	r3,r0
	blr

.section __ex_table,"a"
	.align  3
	.llong	81b,src_error_1
	.llong	91b,dst_error
	.llong	82b,src_error_2
	.llong	92b,dst_error
	.llong	83b,src_error_3
	.llong	93b,dst_error
	.llong	84b,src_error_3
	.llong	94b,dst_error
	.llong	95b,dst_error
	.llong	96b,dst_error
	.llong	97b,dst_error
	addic	r0,r6,0			/* clear carry */

	srdi.	r6,r5,3			/* less than 8 bytes? */
	beq	.Lcopy_tail_word

	/*
	 * If only halfword aligned, align to a double word. Since odd
	 * aligned addresses should be rare and they would require more
	 * work to calculate the correct checksum, we ignore that case
	 * and take the potential slowdown of unaligned loads.
	 *
	 * If the source and destination are relatively unaligned we only
	 * align the source. This keeps things simple.
	 */
	rldicl. r6,r3,64-1,64-2		/* r6 = (r3 & 0x3) >> 1 */
	beq	.Lcopy_aligned

	li	r9,4
	sub	r6,r9,r6
	mtctr	r6

1:
srcnr;	lhz	r6,0(r3)		/* align to doubleword */
	subi	r5,r5,2
	addi	r3,r3,2
	adde	r0,r0,r6
dstnr;	sth	r6,0(r4)
	addi	r4,r4,2
	bdnz	1b

.Lcopy_aligned:
	/*
	 * We unroll the loop such that each iteration is 64 bytes with an
	 * entry and exit limb of 64 bytes, meaning a minimum size of
	 * 128 bytes.
	 */
	srdi.	r6,r5,7
	beq	.Lcopy_tail_doublewords		/* len < 128 */

	srdi	r6,r5,6
	subi	r6,r6,1
	mtctr	r6

	stdu	r1,-STACKFRAMESIZE(r1)
	std	r14,STK_REG(R14)(r1)
	std	r15,STK_REG(R15)(r1)
	std	r16,STK_REG(R16)(r1)

source;	ld	r6,0(r3)
source;	ld	r9,8(r3)

source;	ld	r10,16(r3)
source;	ld	r11,24(r3)

	/*
	 * On POWER6 and POWER7 back to back addes take 2 cycles because of
	 * the XER dependency. This means the fastest this loop can go is
	 * 16 cycles per iteration. The scheduling of the loop below has
	 * been shown to hit this on both POWER6 and POWER7.
	 */
	.align 5
2:
	adde	r0,r0,r6
source;	ld	r12,32(r3)
source;	ld	r14,40(r3)

	adde	r0,r0,r9
source;	ld	r15,48(r3)
source;	ld	r16,56(r3)
	addi	r3,r3,64

	adde	r0,r0,r10
dest;	std	r6,0(r4)
dest;	std	r9,8(r4)

	adde	r0,r0,r11
dest;	std	r10,16(r4)
dest;	std	r11,24(r4)

	adde	r0,r0,r12
dest;	std	r12,32(r4)
dest;	std	r14,40(r4)

	adde	r0,r0,r14
dest;	std	r15,48(r4)
dest;	std	r16,56(r4)
	addi	r4,r4,64

	adde	r0,r0,r15
source;	ld	r6,0(r3)
source;	ld	r9,8(r3)

	adde	r0,r0,r16
source;	ld	r10,16(r3)
source;	ld	r11,24(r3)
	bdnz	2b


	adde	r0,r0,r6
source;	ld	r12,32(r3)
source;	ld	r14,40(r3)

	adde	r0,r0,r9
source;	ld	r15,48(r3)
source;	ld	r16,56(r3)
	addi	r3,r3,64

	adde	r0,r0,r10
dest;	std	r6,0(r4)
dest;	std	r9,8(r4)

	adde	r0,r0,r11
dest;	std	r10,16(r4)
dest;	std	r11,24(r4)

	adde	r0,r0,r12
dest;	std	r12,32(r4)
dest;	std	r14,40(r4)

	adde	r0,r0,r14
dest;	std	r15,48(r4)
dest;	std	r16,56(r4)
	addi	r4,r4,64

	adde	r0,r0,r15
	adde	r0,r0,r16

	ld	r14,STK_REG(R14)(r1)
	ld	r15,STK_REG(R15)(r1)
	ld	r16,STK_REG(R16)(r1)
	addi	r1,r1,STACKFRAMESIZE

	andi.	r5,r5,63

.Lcopy_tail_doublewords:		/* Up to 127 bytes to go */
	srdi.	r6,r5,3
	beq	.Lcopy_tail_word

	mtctr	r6
3:
srcnr;	ld	r6,0(r3)
	addi	r3,r3,8
	adde	r0,r0,r6
dstnr;	std	r6,0(r4)
	addi	r4,r4,8
	bdnz	3b

	andi.	r5,r5,7

.Lcopy_tail_word:			/* Up to 7 bytes to go */
	srdi.	r6,r5,2
	beq	.Lcopy_tail_halfword

srcnr;	lwz	r6,0(r3)
	addi	r3,r3,4
	adde	r0,r0,r6
dstnr;	stw	r6,0(r4)
	addi	r4,r4,4
	subi	r5,r5,4

.Lcopy_tail_halfword:			/* Up to 3 bytes to go */
	srdi.	r6,r5,1
	beq	.Lcopy_tail_byte

srcnr;	lhz	r6,0(r3)
	addi	r3,r3,2
	adde	r0,r0,r6
dstnr;	sth	r6,0(r4)
	addi	r4,r4,2
	subi	r5,r5,2

.Lcopy_tail_byte:			/* Up to 1 byte to go */
	andi.	r6,r5,1
	beq	.Lcopy_finish

srcnr;	lbz	r6,0(r3)
	sldi	r9,r6,8			/* Pad the byte out to 16 bits */
	adde	r0,r0,r9
dstnr;	stb	r6,0(r4)

.Lcopy_finish:
	addze	r0,r0			/* add in final carry */
	rldicl	r4,r0,32,0		/* fold two 32 bit halves together */
	add	r3,r4,r0
	srdi	r3,r3,32
	blr

.Lsrc_error:
	ld	r14,STK_REG(R14)(r1)
	ld	r15,STK_REG(R15)(r1)
	ld	r16,STK_REG(R16)(r1)
	addi	r1,r1,STACKFRAMESIZE
.Lsrc_error_nr:
	cmpdi	0,r7,0
	beqlr
	li	r6,-EFAULT
	stw	r6,0(r7)
	blr

.Ldest_error:
	ld	r14,STK_REG(R14)(r1)
	ld	r15,STK_REG(R15)(r1)
	ld	r16,STK_REG(R16)(r1)
	addi	r1,r1,STACKFRAMESIZE
.Ldest_error_nr:
	cmpdi	0,r8,0
	beqlr
	li	r6,-EFAULT
	stw	r6,0(r8)
	blr
