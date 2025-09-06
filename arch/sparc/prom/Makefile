# Makefile for the Sun Boot PROM interface library under
# Linux.
#

lib-y := bootstr.o devmap.o devops.o init.o memory.o misc.o mp.o \
	 palloc.o ranges.o segment.o console.o printf.o tree.o

lib-$(CONFIG_SUN4) += sun4prom.o
asflags := -ansi
ccflags := -Werror

lib-y                 := bootstr_$(BITS).o
lib-y                 += init_$(BITS).o
lib-$(CONFIG_SPARC32) += memory.o
lib-y                 += misc_$(BITS).o
lib-$(CONFIG_SPARC32) += mp.o
lib-$(CONFIG_SPARC32) += ranges.o
lib-y                 += console_$(BITS).o
lib-y                 += printf.o
lib-y                 += tree_$(BITS).o
lib-$(CONFIG_SPARC64) += p1275.o
lib-$(CONFIG_SPARC64) += cif.o
