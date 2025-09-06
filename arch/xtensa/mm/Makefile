#
# Makefile for the Linux/Xtensa-specific parts of the memory manager.
#

obj-y	 := init.o fault.o tlb.o misc.o cache.o
obj-y			:= init.o misc.o
obj-$(CONFIG_MMU)	+= cache.o fault.o mmu.o tlb.o
obj-$(CONFIG_HIGHMEM)	+= highmem.o
