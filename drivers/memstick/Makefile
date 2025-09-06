#
# Makefile for the kernel MemoryStick device drivers.
#

ifeq ($(CONFIG_MEMSTICK_DEBUG),y)
	EXTRA_CFLAGS		+= -DDEBUG
endif
subdir-ccflags-$(CONFIG_MEMSTICK_DEBUG) := -DDEBUG

obj-$(CONFIG_MEMSTICK)		+= core/
obj-$(CONFIG_MEMSTICK)		+= host/

