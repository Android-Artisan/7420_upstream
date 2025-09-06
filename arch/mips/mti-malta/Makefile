#
# Carsten Langgaard, carstenl@mips.com
# Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
#
# Copyright (C) 2008 Wind River Systems, Inc.
#   written by Ralf Baechle <ralf@linux-mips.org>
#
obj-y				:= malta-amon.o malta-cmdline.o \
				   malta-display.o malta-init.o malta-int.o \
				   malta-memory.o malta-mtd.o \
				   malta-platform.o malta-reset.o \
				   malta-setup.o malta-time.o

obj-$(CONFIG_EARLY_PRINTK)	+= malta-console.o
obj-$(CONFIG_PCI)		+= malta-pci.o

# FIXME FIXME FIXME
obj-$(CONFIG_MIPS_MT_SMTC)	+= malta-smtc.o

EXTRA_CFLAGS += -Werror
obj-y				+= malta-display.o
obj-y				+= malta-dt.o
obj-y				+= malta-dtshim.o
obj-y				+= malta-init.o
obj-y				+= malta-int.o
obj-y				+= malta-memory.o
obj-y				+= malta-platform.o
obj-y				+= malta-reset.o
obj-y				+= malta-setup.o
obj-y				+= malta-time.o

obj-$(CONFIG_MIPS_CMP)		+= malta-amon.o
obj-$(CONFIG_MIPS_MALTA_PM)	+= malta-pm.o

CFLAGS_malta-dtshim.o = -I$(src)/../../../scripts/dtc/libfdt
