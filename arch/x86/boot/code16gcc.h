/*
 * code16gcc.h
 *
 * This file is -include'd when compiling 16-bit C code.
 * Note: this asm() needs to be emitted before gcc emits any code.
 * Depending on gcc version, this requires -fno-unit-at-a-time or
 * -fno-toplevel-reorder.
 *
 * Hopefully gcc will eventually have a real -m16 option so we can
 * drop this hack long term.
 */

#ifndef __ASSEMBLY__
asm(".code16gcc");
#endif
#
# code16gcc.h
#
# This file is added to the assembler via -Wa when compiling 16-bit C code.
# This is done this way instead via asm() to make sure gcc does not reorder
# things around us.
#
# gcc 4.9+ has a real -m16 option so we can drop this hack long term.
#

	.code16gcc
