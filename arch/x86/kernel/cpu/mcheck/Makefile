obj-y				=  mce_$(BITS).o therm_throt.o

obj-$(CONFIG_X86_32)		+= k7.o p4.o p5.o p6.o winchip.o
obj-$(CONFIG_X86_MCE_INTEL)	+= mce_intel_64.o
obj-$(CONFIG_X86_MCE_AMD)	+= mce_amd_64.o
obj-$(CONFIG_X86_MCE_NONFATAL)	+= non-fatal.o
obj-y				=  mce.o mce-severity.o mce-genpool.o

obj-$(CONFIG_X86_ANCIENT_MCE)	+= winchip.o p5.o
obj-$(CONFIG_X86_MCE_INTEL)	+= mce_intel.o
obj-$(CONFIG_X86_MCE_AMD)	+= mce_amd.o
obj-$(CONFIG_X86_MCE_THRESHOLD) += threshold.o
obj-$(CONFIG_X86_MCE_INJECT)	+= mce-inject.o

obj-$(CONFIG_X86_THERMAL_VECTOR) += therm_throt.o

obj-$(CONFIG_ACPI_APEI)		+= mce-apei.o
