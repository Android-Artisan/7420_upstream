#
# Makefile for the linux alpha-specific parts of the memory manager.
#

EXTRA_CFLAGS := -Werror
ccflags-y := -Werror

obj-y	:= init.o fault.o extable.o

obj-$(CONFIG_DISCONTIGMEM) += numa.o
