#
# Makefile for the FPU instruction emulation.
#

obj-y    := math.o

EXTRA_AFLAGS := -ansi
EXTRA_CFLAGS = -I. -Iinclude/math-emu -w
# suppress all warnings - as math.c produces a lot!
ccflags-y := -w

obj-y    := math_$(BITS).o
