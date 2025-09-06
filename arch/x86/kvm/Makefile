#
# Makefile for Kernel-based Virtual Machine module
#

common-objs = $(addprefix ../../../virt/kvm/, kvm_main.o ioapic.o \
                coalesced_mmio.o)
ifeq ($(CONFIG_KVM_TRACE),y)
common-objs += $(addprefix ../../../virt/kvm/, kvm_trace.o)
endif

EXTRA_CFLAGS += -Ivirt/kvm -Iarch/x86/kvm

kvm-objs := $(common-objs) x86.o mmu.o x86_emulate.o i8259.o irq.o lapic.o \
	i8254.o
obj-$(CONFIG_KVM) += kvm.o
kvm-intel-objs = vmx.o
obj-$(CONFIG_KVM_INTEL) += kvm-intel.o
kvm-amd-objs = svm.o
obj-$(CONFIG_KVM_AMD) += kvm-amd.o

ccflags-y += -Iarch/x86/kvm

CFLAGS_x86.o := -I.
CFLAGS_svm.o := -I.
CFLAGS_vmx.o := -I.

KVM := ../../../virt/kvm

kvm-y			+= $(KVM)/kvm_main.o $(KVM)/coalesced_mmio.o \
				$(KVM)/eventfd.o $(KVM)/irqchip.o $(KVM)/vfio.o
kvm-$(CONFIG_KVM_ASYNC_PF)	+= $(KVM)/async_pf.o

kvm-y			+= x86.o mmu.o emulate.o i8259.o irq.o lapic.o \
			   i8254.o ioapic.o irq_comm.o cpuid.o pmu.o mtrr.o \
			   hyperv.o

kvm-$(CONFIG_KVM_DEVICE_ASSIGNMENT)	+= assigned-dev.o iommu.o
kvm-intel-y		+= vmx.o pmu_intel.o
kvm-amd-y		+= svm.o pmu_amd.o

obj-$(CONFIG_KVM)	+= kvm.o
obj-$(CONFIG_KVM_INTEL)	+= kvm-intel.o
obj-$(CONFIG_KVM_AMD)	+= kvm-amd.o
