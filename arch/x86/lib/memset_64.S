/* Copyright 2002 Andi Kleen, SuSE Labs */

#include <linux/linkage.h>
#include <asm/dwarf2.h>

/*
 * ISO C memset - set a memory block to a byte value.
 *	
 * rdi   destination
 * rsi   value (char) 
 * rdx   count (bytes) 
 * 
 * rax   original destination
 */	
	ALIGN
memset_c:
	CFI_STARTPROC
	movq %rdi,%r9
	movl %edx,%r8d
	andl $7,%r8d
	movl %edx,%ecx
	shrl $3,%ecx
	/* expand byte value  */
	movzbl %sil,%esi
	movabs $0x0101010101010101,%rax
	mulq %rsi		/* with rax, clobbers rdx */
	rep stosq
	movl %r8d,%ecx
	rep stosb
	movq %r9,%rax
	ret
	CFI_ENDPROC
ENDPROC(memset_c)

ENTRY(memset)
ENTRY(__memset)
	CFI_STARTPROC
	movq %rdi,%r10
	movq %rdx,%r11
#include <asm/cpufeature.h>
#include <asm/cpufeatures.h>
#include <asm/alternative-asm.h>

.weak memset

/*
 * ISO C memset - set a memory block to a byte value. This function uses fast
 * string to get better performance than the original function. The code is
 * simpler and shorter than the orignal function as well.
 *
 * rdi   destination
 * rsi   value (char)
 * rdx   count (bytes)
 *
 * rax   original destination
 */
ENTRY(memset)
ENTRY(__memset)
	/*
	 * Some CPUs support enhanced REP MOVSB/STOSB feature. It is recommended
	 * to use it when possible. If not available, use fast string instructions.
	 *
	 * Otherwise, use original memset function.
	 */
	ALTERNATIVE_2 "jmp memset_orig", "", X86_FEATURE_REP_GOOD, \
		      "jmp memset_erms", X86_FEATURE_ERMS

	movq %rdi,%r9
	movq %rdx,%rcx
	andl $7,%edx
	shrq $3,%rcx
	/* expand byte value  */
	movzbl %sil,%esi
	movabs $0x0101010101010101,%rax
	imulq %rsi,%rax
	rep stosq
	movl %edx,%ecx
	rep stosb
	movq %r9,%rax
	ret
ENDPROC(memset)
ENDPROC(__memset)

/*
 * ISO C memset - set a memory block to a byte value. This function uses
 * enhanced rep stosb to override the fast string function.
 * The code is simpler and shorter than the fast string function as well.
 *
 * rdi   destination
 * rsi   value (char)
 * rdx   count (bytes)
 *
 * rax   original destination
 */
ENTRY(memset_erms)
	movq %rdi,%r9
	movb %sil,%al
	movq %rdx,%rcx
	rep stosb
	movq %r9,%rax
	ret
ENDPROC(memset_erms)

ENTRY(memset_orig)
	movq %rdi,%r10

	/* expand byte value  */
	movzbl %sil,%ecx
	movabs $0x0101010101010101,%rax
	mul    %rcx		/* with rax, clobbers rdx */
	imulq  %rcx,%rax

	/* align dst */
	movl  %edi,%r9d
	andl  $7,%r9d
	jnz  .Lbad_alignment
	CFI_REMEMBER_STATE
.Lafter_bad_alignment:

	movl %r11d,%ecx
	shrl $6,%ecx
.Lafter_bad_alignment:

	movq  %rdx,%rcx
	shrq  $6,%rcx
	jz	 .Lhandle_tail

	.p2align 4
.Lloop_64:
	decl   %ecx
	decq  %rcx
	movq  %rax,(%rdi)
	movq  %rax,8(%rdi)
	movq  %rax,16(%rdi)
	movq  %rax,24(%rdi)
	movq  %rax,32(%rdi)
	movq  %rax,40(%rdi)
	movq  %rax,48(%rdi)
	movq  %rax,56(%rdi)
	leaq  64(%rdi),%rdi
	jnz    .Lloop_64

	/* Handle tail in loops. The loops should be faster than hard
	   to predict jump tables. */
	.p2align 4
.Lhandle_tail:
	movl	%r11d,%ecx
	movl	%edx,%ecx
	andl    $63&(~7),%ecx
	jz 		.Lhandle_7
	shrl	$3,%ecx
	.p2align 4
.Lloop_8:
	decl   %ecx
	movq  %rax,(%rdi)
	leaq  8(%rdi),%rdi
	jnz    .Lloop_8

.Lhandle_7:
	movl	%r11d,%ecx
	andl	$7,%ecx
	jz      .Lende
	.p2align 4
.Lloop_1:
	decl    %ecx
	andl	$7,%edx
	jz      .Lende
	.p2align 4
.Lloop_1:
	decl    %edx
	movb 	%al,(%rdi)
	leaq	1(%rdi),%rdi
	jnz     .Lloop_1

.Lende:
	movq	%r10,%rax
	ret

	CFI_RESTORE_STATE
.Lbad_alignment:
	cmpq $7,%r11
.Lbad_alignment:
	cmpq $7,%rdx
	jbe	.Lhandle_7
	movq %rax,(%rdi)	/* unaligned store */
	movq $8,%r8
	subq %r9,%r8
	addq %r8,%rdi
	subq %r8,%r11
	jmp .Lafter_bad_alignment
.Lfinal:
	CFI_ENDPROC
ENDPROC(memset)
ENDPROC(__memset)

	/* Some CPUs run faster using the string instructions.
	   It is also a lot simpler. Use this when possible */

#include <asm/cpufeature.h>

	.section .altinstr_replacement,"ax"
1:	.byte 0xeb				/* jmp <disp8> */
	.byte (memset_c - memset) - (2f - 1b)	/* offset */
2:
	.previous
	.section .altinstructions,"a"
	.align 8
	.quad memset
	.quad 1b
	.byte X86_FEATURE_REP_GOOD
	.byte .Lfinal - memset
	.byte 2b - 1b
	.previous
	subq %r8,%rdx
	jmp .Lafter_bad_alignment
.Lfinal:
ENDPROC(memset_orig)
