/*
 * This file contains the generic code to perform a call to the
 * pSeries LPAR hypervisor.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/jump_label.h>
#include <asm/hvcall.h>
#include <asm/processor.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
	
#define STK_PARM(i)     (48 + ((i)-3)*8)

#ifdef CONFIG_HCALL_STATS
/*
 * precall must preserve all registers.  use unused STK_PARM()
 * areas to save snapshots and opcode.
 */
#define HCALL_INST_PRECALL					\
	std	r3,STK_PARM(r3)(r1);	/* save opcode */	\
	mftb	r0;			/* get timebase and */	\
	std     r0,STK_PARM(r5)(r1);	/* save for later */	\
BEGIN_FTR_SECTION;						\
	mfspr	r0,SPRN_PURR;		/* get PURR and */	\
	std	r0,STK_PARM(r6)(r1);	/* save for later */	\
END_FTR_SECTION_IFSET(CPU_FTR_PURR);
	
/*
 * postcall is performed immediately before function return which
 * allows liberal use of volatile registers.  We branch around this
 * in early init (eg when populating the MMU hashtable) by using an
 * unconditional cpu feature.
 */
#define HCALL_INST_POSTCALL					\
BEGIN_FTR_SECTION;						\
	b	1f;						\
END_FTR_SECTION(0, 1);						\
	ld	r4,STK_PARM(r3)(r1);	/* validate opcode */	\
	cmpldi	cr7,r4,MAX_HCALL_OPCODE;			\
	bgt-	cr7,1f;						\
								\
	/* get time and PURR snapshots after hcall */		\
	mftb	r7;			/* timebase after */	\
BEGIN_FTR_SECTION;						\
	mfspr	r8,SPRN_PURR;		/* PURR after */	\
	ld	r6,STK_PARM(r6)(r1);	/* PURR before */	\
	subf	r6,r6,r8;		/* delta */		\
END_FTR_SECTION_IFSET(CPU_FTR_PURR);				\
	ld	r5,STK_PARM(r5)(r1);	/* timebase before */	\
	subf	r5,r5,r7;		/* time delta */	\
								\
	/* calculate address of stat structure r4 = opcode */	\
	srdi	r4,r4,2;		/* index into array */	\
	mulli	r4,r4,HCALL_STAT_SIZE;				\
	LOAD_REG_ADDR(r7, per_cpu__hcall_stats);		\
	add	r4,r4,r7;					\
	ld	r7,PACA_DATA_OFFSET(r13); /* per cpu offset */	\
	add	r4,r4,r7;					\
								\
	/* update stats	*/					\
	ld	r7,HCALL_STAT_CALLS(r4); /* count */		\
	addi	r7,r7,1;					\
	std	r7,HCALL_STAT_CALLS(r4);			\
	ld      r7,HCALL_STAT_TB(r4);	/* timebase */		\
	add	r7,r7,r5;					\
	std	r7,HCALL_STAT_TB(r4);				\
BEGIN_FTR_SECTION;						\
	ld	r7,HCALL_STAT_PURR(r4);	/* PURR */		\
	add	r7,r7,r6;					\
	std	r7,HCALL_STAT_PURR(r4);				\
END_FTR_SECTION_IFSET(CPU_FTR_PURR);				\
1:
#else
#define HCALL_INST_PRECALL
#define HCALL_INST_POSTCALL
#endif

	.text

_GLOBAL(plpar_hcall_norets)
#include <asm/ptrace.h>

	.section	".text"
	
#ifdef CONFIG_TRACEPOINTS

#ifndef HAVE_JUMP_LABEL
	.section	".toc","aw"

	.globl hcall_tracepoint_refcount
hcall_tracepoint_refcount:
	.llong	0

	.section	".text"
#endif

/*
 * precall must preserve all registers.  use unused STK_PARAM()
 * areas to save snapshots and opcode.
 */
#define HCALL_INST_PRECALL(FIRST_REG)				\
	mflr	r0;						\
	std	r3,STK_PARAM(R3)(r1);				\
	std	r4,STK_PARAM(R4)(r1);				\
	std	r5,STK_PARAM(R5)(r1);				\
	std	r6,STK_PARAM(R6)(r1);				\
	std	r7,STK_PARAM(R7)(r1);				\
	std	r8,STK_PARAM(R8)(r1);				\
	std	r9,STK_PARAM(R9)(r1);				\
	std	r10,STK_PARAM(R10)(r1);				\
	std	r0,16(r1);					\
	addi	r4,r1,STK_PARAM(FIRST_REG);			\
	stdu	r1,-STACK_FRAME_OVERHEAD(r1);			\
	bl	__trace_hcall_entry;				\
	ld	r3,STACK_FRAME_OVERHEAD+STK_PARAM(R3)(r1);	\
	ld	r4,STACK_FRAME_OVERHEAD+STK_PARAM(R4)(r1);	\
	ld	r5,STACK_FRAME_OVERHEAD+STK_PARAM(R5)(r1);	\
	ld	r6,STACK_FRAME_OVERHEAD+STK_PARAM(R6)(r1);	\
	ld	r7,STACK_FRAME_OVERHEAD+STK_PARAM(R7)(r1);	\
	ld	r8,STACK_FRAME_OVERHEAD+STK_PARAM(R8)(r1);	\
	ld	r9,STACK_FRAME_OVERHEAD+STK_PARAM(R9)(r1);	\
	ld	r10,STACK_FRAME_OVERHEAD+STK_PARAM(R10)(r1)

/*
 * postcall is performed immediately before function return which
 * allows liberal use of volatile registers.
 */
#define __HCALL_INST_POSTCALL					\
	ld	r0,STACK_FRAME_OVERHEAD+STK_PARAM(R3)(r1);	\
	std	r3,STACK_FRAME_OVERHEAD+STK_PARAM(R3)(r1);	\
	mr	r4,r3;						\
	mr	r3,r0;						\
	bl	__trace_hcall_exit;				\
	ld	r0,STACK_FRAME_OVERHEAD+16(r1);			\
	addi	r1,r1,STACK_FRAME_OVERHEAD;			\
	ld	r3,STK_PARAM(R3)(r1);				\
	mtlr	r0

#define HCALL_INST_POSTCALL_NORETS				\
	li	r5,0;						\
	__HCALL_INST_POSTCALL

#define HCALL_INST_POSTCALL(BUFREG)				\
	mr	r5,BUFREG;					\
	__HCALL_INST_POSTCALL

#ifdef HAVE_JUMP_LABEL
#define HCALL_BRANCH(LABEL)					\
	ARCH_STATIC_BRANCH(LABEL, hcall_tracepoint_key)
#else

/*
 * We branch around this in early init (eg when populating the MMU
 * hashtable) by using an unconditional cpu feature.
 */
#define HCALL_BRANCH(LABEL)					\
BEGIN_FTR_SECTION;						\
	b	1f;						\
END_FTR_SECTION(0, 1);						\
	ld	r12,hcall_tracepoint_refcount@toc(r2);		\
	std	r12,32(r1);					\
	cmpdi	r12,0;						\
	bne-	LABEL;						\
1:
#endif

#else
#define HCALL_INST_PRECALL(FIRST_ARG)
#define HCALL_INST_POSTCALL_NORETS
#define HCALL_INST_POSTCALL(BUFREG)
#define HCALL_BRANCH(LABEL)
#endif

_GLOBAL_TOC(plpar_hcall_norets)
	HMT_MEDIUM

	mfcr	r0
	stw	r0,8(r1)

	HCALL_INST_PRECALL

	HVSC				/* invoke the hypervisor */

	HCALL_INST_POSTCALL

	HCALL_BRANCH(plpar_hcall_norets_trace)
	HVSC				/* invoke the hypervisor */

	lwz	r0,8(r1)
	mtcrf	0xff,r0
	blr				/* return r3 = status */

_GLOBAL(plpar_hcall)
#ifdef CONFIG_TRACEPOINTS
plpar_hcall_norets_trace:
	HCALL_INST_PRECALL(R4)
	HVSC
	HCALL_INST_POSTCALL_NORETS
	lwz	r0,8(r1)
	mtcrf	0xff,r0
	blr
#endif

_GLOBAL_TOC(plpar_hcall)
	HMT_MEDIUM

	mfcr	r0
	stw	r0,8(r1)

	HCALL_INST_PRECALL

	std     r4,STK_PARM(r4)(r1)     /* Save ret buffer */
	HCALL_BRANCH(plpar_hcall_trace)

	std     r4,STK_PARAM(R4)(r1)     /* Save ret buffer */

	mr	r4,r5
	mr	r5,r6
	mr	r6,r7
	mr	r7,r8
	mr	r8,r9
	mr	r9,r10

	HVSC				/* invoke the hypervisor */

	ld	r12,STK_PARM(r4)(r1)
	ld	r12,STK_PARAM(R4)(r1)
	std	r4,  0(r12)
	std	r5,  8(r12)
	std	r6, 16(r12)
	std	r7, 24(r12)

	HCALL_INST_POSTCALL

	lwz	r0,8(r1)
	mtcrf	0xff,r0

	blr				/* return r3 = status */

#ifdef CONFIG_TRACEPOINTS
plpar_hcall_trace:
	HCALL_INST_PRECALL(R5)

	std	r4,STK_PARAM(R4)(r1)
	mr	r0,r4

	mr	r4,r5
	mr	r5,r6
	mr	r6,r7
	mr	r7,r8
	mr	r8,r9
	mr	r9,r10

	HVSC

	ld	r12,STK_PARAM(R4)(r1)
	std	r4,0(r12)
	std	r5,8(r12)
	std	r6,16(r12)
	std	r7,24(r12)

	HCALL_INST_POSTCALL(r12)

	lwz	r0,8(r1)
	mtcrf	0xff,r0

	blr
#endif

/*
 * plpar_hcall_raw can be called in real mode. kexec/kdump need some
 * hypervisor calls to be executed in real mode. So plpar_hcall_raw
 * does not access the per cpu hypervisor call statistics variables,
 * since these variables may not be present in the RMO region.
 */
_GLOBAL(plpar_hcall_raw)
	HMT_MEDIUM

	mfcr	r0
	stw	r0,8(r1)

	std     r4,STK_PARM(r4)(r1)     /* Save ret buffer */
	std     r4,STK_PARAM(R4)(r1)     /* Save ret buffer */

	mr	r4,r5
	mr	r5,r6
	mr	r6,r7
	mr	r7,r8
	mr	r8,r9
	mr	r9,r10

	HVSC				/* invoke the hypervisor */

	ld	r12,STK_PARM(r4)(r1)
	ld	r12,STK_PARAM(R4)(r1)
	std	r4,  0(r12)
	std	r5,  8(r12)
	std	r6, 16(r12)
	std	r7, 24(r12)

	lwz	r0,8(r1)
	mtcrf	0xff,r0

	blr				/* return r3 = status */

_GLOBAL(plpar_hcall9)
_GLOBAL_TOC(plpar_hcall9)
	HMT_MEDIUM

	mfcr	r0
	stw	r0,8(r1)

	HCALL_INST_PRECALL

	std     r4,STK_PARM(r4)(r1)     /* Save ret buffer */
	HCALL_BRANCH(plpar_hcall9_trace)

	std     r4,STK_PARAM(R4)(r1)     /* Save ret buffer */

	mr	r4,r5
	mr	r5,r6
	mr	r6,r7
	mr	r7,r8
	mr	r8,r9
	mr	r9,r10
	ld	r10,STK_PARM(r11)(r1)	 /* put arg7 in R10 */
	ld	r11,STK_PARM(r12)(r1)	 /* put arg8 in R11 */
	ld	r12,STK_PARM(r13)(r1)    /* put arg9 in R12 */
	ld	r10,STK_PARAM(R11)(r1)	 /* put arg7 in R10 */
	ld	r11,STK_PARAM(R12)(r1)	 /* put arg8 in R11 */
	ld	r12,STK_PARAM(R13)(r1)    /* put arg9 in R12 */

	HVSC				/* invoke the hypervisor */

	mr	r0,r12
	ld	r12,STK_PARM(r4)(r1)
	ld	r12,STK_PARAM(R4)(r1)
	std	r4,  0(r12)
	std	r5,  8(r12)
	std	r6, 16(r12)
	std	r7, 24(r12)
	std	r8, 32(r12)
	std	r9, 40(r12)
	std	r10,48(r12)
	std	r11,56(r12)
	std	r0, 64(r12)

	HCALL_INST_POSTCALL
	lwz	r0,8(r1)
	mtcrf	0xff,r0

	blr				/* return r3 = status */

#ifdef CONFIG_TRACEPOINTS
plpar_hcall9_trace:
	HCALL_INST_PRECALL(R5)

	std	r4,STK_PARAM(R4)(r1)
	mr	r0,r4

	mr	r4,r5
	mr	r5,r6
	mr	r6,r7
	mr	r7,r8
	mr	r8,r9
	mr	r9,r10
	ld	r10,STACK_FRAME_OVERHEAD+STK_PARAM(R11)(r1)
	ld	r11,STACK_FRAME_OVERHEAD+STK_PARAM(R12)(r1)
	ld	r12,STACK_FRAME_OVERHEAD+STK_PARAM(R13)(r1)

	HVSC

	mr	r0,r12
	ld	r12,STACK_FRAME_OVERHEAD+STK_PARAM(R4)(r1)
	std	r4,0(r12)
	std	r5,8(r12)
	std	r6,16(r12)
	std	r7,24(r12)
	std	r8,32(r12)
	std	r9,40(r12)
	std	r10,48(r12)
	std	r11,56(r12)
	std	r0,64(r12)

	HCALL_INST_POSTCALL(r12)

	lwz	r0,8(r1)
	mtcrf	0xff,r0

	blr
#endif

/* See plpar_hcall_raw to see why this is needed */
_GLOBAL(plpar_hcall9_raw)
	HMT_MEDIUM

	mfcr	r0
	stw	r0,8(r1)

	std     r4,STK_PARAM(R4)(r1)     /* Save ret buffer */

	mr	r4,r5
	mr	r5,r6
	mr	r6,r7
	mr	r7,r8
	mr	r8,r9
	mr	r9,r10
	ld	r10,STK_PARAM(R11)(r1)	 /* put arg7 in R10 */
	ld	r11,STK_PARAM(R12)(r1)	 /* put arg8 in R11 */
	ld	r12,STK_PARAM(R13)(r1)    /* put arg9 in R12 */

	HVSC				/* invoke the hypervisor */

	mr	r0,r12
	ld	r12,STK_PARAM(R4)(r1)
	std	r4,  0(r12)
	std	r5,  8(r12)
	std	r6, 16(r12)
	std	r7, 24(r12)
	std	r8, 32(r12)
	std	r9, 40(r12)
	std	r10,48(r12)
	std	r11,56(r12)
	std	r0, 64(r12)

	lwz	r0,8(r1)
	mtcrf	0xff,r0

	blr				/* return r3 = status */
