#
# Makefile for the linux kernel.
#
obj-y			:= core.o clock.o gpio.o
obj-m			:=
obj-n			:=
obj-			:=

obj-$(CONFIG_MACH_ADSSPHERE)	+= adssphere.o
obj-$(CONFIG_MACH_EDB9302)	+= edb9302.o
obj-$(CONFIG_MACH_EDB9302A)	+= edb9302a.o
obj-$(CONFIG_MACH_EDB9307)	+= edb9307.o
obj-$(CONFIG_MACH_EDB9312)	+= edb9312.o
obj-$(CONFIG_MACH_EDB9315)	+= edb9315.o
obj-$(CONFIG_MACH_EDB9315A)	+= edb9315a.o
obj-$(CONFIG_MACH_GESBC9312)	+= gesbc9312.o
obj-$(CONFIG_MACH_MICRO9)	+= micro9.o
obj-$(CONFIG_MACH_TS72XX)	+= ts72xx.o
obj-y			:= core.o clock.o timer-ep93xx.o

obj-$(CONFIG_EP93XX_DMA)	+= dma.o

obj-$(CONFIG_CRUNCH)		+= crunch.o crunch-bits.o
AFLAGS_crunch-bits.o		:= -Wa,-mcpu=ep9312

obj-$(CONFIG_MACH_ADSSPHERE)	+= adssphere.o
obj-$(CONFIG_MACH_EDB93XX)	+= edb93xx.o
obj-$(CONFIG_MACH_GESBC9312)	+= gesbc9312.o
obj-$(CONFIG_MACH_MICRO9)	+= micro9.o
obj-$(CONFIG_MACH_SIM_ONE)	+= simone.o
obj-$(CONFIG_MACH_SNAPPER_CL15)	+= snappercl15.o
obj-$(CONFIG_MACH_TS72XX)	+= ts72xx.o
obj-$(CONFIG_MACH_VISION_EP9307)+= vision_ep9307.o
