#include <linux/linkage.h>
#include <asm/dwarf2.h>

/*
 * Zero a page. 	
 * rdi	page
 */			
	ALIGN
clear_page_c:
	CFI_STARTPROC
#include <asm/cpufeature.h>
#include <asm/cpufeatures.h>
#include <asm/alternative-asm.h>

/*
 * Most CPUs support enhanced REP MOVSB/STOSB instructions. It is
 * recommended to use this when possible and we do use them by default.
 * If enhanced REP MOVSB/STOSB is not available, try to use fast string.
 * Otherwise, use original.
 */

/*
 * Zero a page.
 * %rdi	- page
 */
ENTRY(clear_page)

	ALTERNATIVE_2 "jmp clear_page_orig", "", X86_FEATURE_REP_GOOD, \
		      "jmp clear_page_c_e", X86_FEATURE_ERMS

	movl $4096/8,%ecx
	xorl %eax,%eax
	rep stosq
	ret
	CFI_ENDPROC
ENDPROC(clear_page)

ENTRY(clear_page)
	CFI_STARTPROC
ENDPROC(clear_page)

ENTRY(clear_page_orig)

	xorl   %eax,%eax
	movl   $4096/64,%ecx
	.p2align 4
.Lloop:
	decl	%ecx
#define PUT(x) movq %rax,x*8(%rdi)
	movq %rax,(%rdi)
	PUT(1)
	PUT(2)
	PUT(3)
	PUT(4)
	PUT(5)
	PUT(6)
	PUT(7)
	leaq	64(%rdi),%rdi
	jnz	.Lloop
	nop
	ret
	CFI_ENDPROC
.Lclear_page_end:
ENDPROC(clear_page)

	/* Some CPUs run faster using the string instructions.
	   It is also a lot simpler. Use this when possible */

#include <asm/cpufeature.h>

	.section .altinstr_replacement,"ax"
1:	.byte 0xeb					/* jmp <disp8> */
	.byte (clear_page_c - clear_page) - (2f - 1b)	/* offset */
2:
	.previous
	.section .altinstructions,"a"
	.align 8
	.quad clear_page
	.quad 1b
	.byte X86_FEATURE_REP_GOOD
	.byte .Lclear_page_end - clear_page
	.byte 2b - 1b
	.previous
ENDPROC(clear_page_orig)

ENTRY(clear_page_c_e)
	movl $4096,%ecx
	xorl %eax,%eax
	rep stosb
	ret
ENDPROC(clear_page_c_e)
