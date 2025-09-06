/*
 * Copyright (C) 2002 Paul Mackerras, IBM Corp.
 * Copyright (C) 2008 Mark Nelson, IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <asm/processor.h>
#include <asm/ppc_asm.h>

_GLOBAL(copy_4K_page)
	std	r31,-8(1)
	std	r30,-16(1)
	std	r29,-24(1)
	std	r28,-32(1)
	std	r27,-40(1)
	std	r26,-48(1)
	std	r25,-56(1)
	std	r24,-64(1)
	std	r23,-72(1)
	std	r22,-80(1)
	std	r21,-88(1)
	std	r20,-96(1)
	li	r5,4096/32 - 1
	addi	r3,r3,-8
	li	r12,5
0:	addi	r5,r5,-24
	mtctr	r12
	ld	r22,640(4)
	ld	r21,512(4)
	ld	r20,384(4)
	ld	r11,256(4)
	ld	r9,128(4)
	ld	r7,0(4)
	ld	r25,648(4)
	ld	r24,520(4)
	ld	r23,392(4)
	ld	r10,264(4)
	ld	r8,136(4)
	ldu	r6,8(4)
	cmpwi	r5,24
1:	std	r22,648(3)
	std	r21,520(3)
	std	r20,392(3)
	std	r11,264(3)
	std	r9,136(3)
	std	r7,8(3)
	ld	r28,648(4)
	ld	r27,520(4)
	ld	r26,392(4)
	ld	r31,264(4)
	ld	r30,136(4)
	ld	r29,8(4)
	std	r25,656(3)
	std	r24,528(3)
	std	r23,400(3)
	std	r10,272(3)
	std	r8,144(3)
	std	r6,16(3)
	ld	r22,656(4)
	ld	r21,528(4)
	ld	r20,400(4)
	ld	r11,272(4)
	ld	r9,144(4)
	ld	r7,16(4)
	std	r28,664(3)
	std	r27,536(3)
	std	r26,408(3)
	std	r31,280(3)
	std	r30,152(3)
	stdu	r29,24(3)
	ld	r25,664(4)
	ld	r24,536(4)
	ld	r23,408(4)
	ld	r10,280(4)
	ld	r8,152(4)
	ldu	r6,24(4)
	bdnz	1b
	std	r22,648(3)
	std	r21,520(3)
	std	r20,392(3)
	std	r11,264(3)
	std	r9,136(3)
	std	r7,8(3)
	addi	r4,r4,640
	addi	r3,r3,648
	bge	0b
	mtctr	r5
	ld	r7,0(4)
	ld	r8,8(4)
	ldu	r9,16(4)
3:	ld	r10,8(4)
	std	r7,8(3)
	ld	r7,16(4)
	std	r8,16(3)
	ld	r8,24(4)
	std	r9,24(3)
	ldu	r9,32(4)
	stdu	r10,32(3)
	bdnz	3b
4:	ld	r10,8(4)
	std	r7,8(3)
	std	r8,16(3)
	std	r9,24(3)
	std	r10,32(3)
9:	ld	r20,-96(1)
	ld	r21,-88(1)
	ld	r22,-80(1)
	ld	r23,-72(1)
	ld	r24,-64(1)
	ld	r25,-56(1)
	ld	r26,-48(1)
	ld	r27,-40(1)
	ld	r28,-32(1)
	ld	r29,-24(1)
	ld	r30,-16(1)
	ld	r31,-8(1)
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>

        .section        ".toc","aw"
PPC64_CACHES:
        .tc             ppc64_caches[TC],ppc64_caches
        .section        ".text"

_GLOBAL_TOC(copy_page)
BEGIN_FTR_SECTION
	lis	r5,PAGE_SIZE@h
FTR_SECTION_ELSE
	b	copypage_power7
ALT_FTR_SECTION_END_IFCLR(CPU_FTR_VMX_COPY)
	ori	r5,r5,PAGE_SIZE@l
BEGIN_FTR_SECTION
	ld      r10,PPC64_CACHES@toc(r2)
	lwz	r11,DCACHEL1LOGLINESIZE(r10)	/* log2 of cache line size */
	lwz     r12,DCACHEL1LINESIZE(r10)	/* get cache line size */
	li	r9,0
	srd	r8,r5,r11

	mtctr	r8
.Lsetup:
	dcbt	r9,r4
	dcbz	r9,r3
	add	r9,r9,r12
	bdnz	.Lsetup
END_FTR_SECTION_IFSET(CPU_FTR_CP_USE_DCBTZ)
	addi	r3,r3,-8
	srdi    r8,r5,7		/* page is copied in 128 byte strides */
	addi	r8,r8,-1	/* one stride copied outside loop */

	mtctr	r8

	ld	r5,0(r4)
	ld	r6,8(r4)
	ld	r7,16(r4)
	ldu	r8,24(r4)
1:	std	r5,8(r3)
	std	r6,16(r3)
	ld	r9,8(r4)
	ld	r10,16(r4)
	std	r7,24(r3)
	std	r8,32(r3)
	ld	r11,24(r4)
	ld	r12,32(r4)
	std	r9,40(r3)
	std	r10,48(r3)
	ld	r5,40(r4)
	ld	r6,48(r4)
	std	r11,56(r3)
	std	r12,64(r3)
	ld	r7,56(r4)
	ld	r8,64(r4)
	std	r5,72(r3)
	std	r6,80(r3)
	ld	r9,72(r4)
	ld	r10,80(r4)
	std	r7,88(r3)
	std	r8,96(r3)
	ld	r11,88(r4)
	ld	r12,96(r4)
	std	r9,104(r3)
	std	r10,112(r3)
	ld	r5,104(r4)
	ld	r6,112(r4)
	std	r11,120(r3)
	stdu	r12,128(r3)
	ld	r7,120(r4)
	ldu	r8,128(r4)
	bdnz	1b

	std	r5,8(r3)
	std	r6,16(r3)
	ld	r9,8(r4)
	ld	r10,16(r4)
	std	r7,24(r3)
	std	r8,32(r3)
	ld	r11,24(r4)
	ld	r12,32(r4)
	std	r9,40(r3)
	std	r10,48(r3)
	ld	r5,40(r4)
	ld	r6,48(r4)
	std	r11,56(r3)
	std	r12,64(r3)
	ld	r7,56(r4)
	ld	r8,64(r4)
	std	r5,72(r3)
	std	r6,80(r3)
	ld	r9,72(r4)
	ld	r10,80(r4)
	std	r7,88(r3)
	std	r8,96(r3)
	ld	r11,88(r4)
	ld	r12,96(r4)
	std	r9,104(r3)
	std	r10,112(r3)
	std	r11,120(r3)
	std	r12,128(r3)
	blr
