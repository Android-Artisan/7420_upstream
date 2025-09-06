# Makefile for the linux Sparc-specific parts of the memory manager.
#

EXTRA_AFLAGS := -ansi

obj-y    := fault.o init.o loadmmu.o generic.o extable.o btfixup.o

ifeq ($(CONFIG_SUN4),y)
obj-y	 += nosrmmu.o
else
obj-y	 += srmmu.o iommu.o io-unit.o hypersparc.o viking.o tsunami.o swift.o
endif

ifdef CONFIG_HIGHMEM
obj-y	+= highmem.o
endif

ifdef CONFIG_SMP
obj-y   += nosun4c.o
else
obj-y   += sun4c.o
endif
asflags-y := -ansi
ccflags-y := -Werror

obj-$(CONFIG_SPARC64)   += ultra.o tlb.o tsb.o gup.o
obj-y                   += fault_$(BITS).o
obj-y                   += init_$(BITS).o
obj-$(CONFIG_SPARC32)   += extable.o srmmu.o iommu.o io-unit.o
obj-$(CONFIG_SPARC32)   += srmmu_access.o
obj-$(CONFIG_SPARC32)   += hypersparc.o viking.o tsunami.o swift.o
obj-$(CONFIG_SPARC32)   += leon_mm.o

# Only used by sparc64
obj-$(CONFIG_HUGETLB_PAGE) += hugetlbpage.o

# Only used by sparc32
obj-$(CONFIG_HIGHMEM)   += highmem.o
