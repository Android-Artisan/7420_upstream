#
# Makefile for s390-specific library files..
#

lib-y += delay.o string.o uaccess_std.o uaccess_pt.o
obj-$(CONFIG_32BIT) += div64.o qrnnd.o
lib-$(CONFIG_64BIT) += uaccess_mvcos.o
lib-$(CONFIG_SMP) += spinlock.o
lib-y += delay.o string.o uaccess.o find.o
obj-y += mem.o
lib-$(CONFIG_SMP) += spinlock.o
lib-$(CONFIG_KPROBES) += probes.o
lib-$(CONFIG_UPROBES) += probes.o
