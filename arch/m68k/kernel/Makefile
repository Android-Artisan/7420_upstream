#
# Makefile for the linux kernel.
#

ifndef CONFIG_SUN3
  extra-y := head.o
else
  extra-y := sun3-head.o
endif
extra-y	+= vmlinux.lds

obj-y	:= entry.o process.o traps.o ints.o signal.o ptrace.o module.o \
	   sys_m68k.o time.o setup.o m68k_ksyms.o devres.o

devres-y = ../../../kernel/irq/devres.o

obj-$(CONFIG_PCI)	+= bios32.o
obj-y$(CONFIG_MMU_SUN3) += dma.o	# no, it's not a typo
extra-$(CONFIG_AMIGA)	:= head.o
extra-$(CONFIG_ATARI)	:= head.o
extra-$(CONFIG_MAC)	:= head.o
extra-$(CONFIG_APOLLO)	:= head.o
extra-$(CONFIG_VME)	:= head.o
extra-$(CONFIG_HP300)	:= head.o
extra-$(CONFIG_Q40)	:= head.o
extra-$(CONFIG_SUN3X)	:= head.o
extra-$(CONFIG_SUN3)	:= sun3-head.o
extra-y			+= vmlinux.lds

obj-y	:= entry.o irq.o m68k_ksyms.o module.o process.o ptrace.o
obj-y	+= setup.o signal.o sys_m68k.o syscalltable.o time.o traps.o

obj-$(CONFIG_MMU_MOTOROLA) += ints.o vectors.o
obj-$(CONFIG_MMU_SUN3) += ints.o vectors.o
obj-$(CONFIG_PCI) += pcibios.o

obj-$(CONFIG_HAS_DMA)	+= dma.o

obj-$(CONFIG_KEXEC)		+= machine_kexec.o relocate_kernel.o
obj-$(CONFIG_BOOTINFO_PROC)	+= bootinfo_proc.o

obj-$(CONFIG_EARLY_PRINTK)	+= early_printk.o

