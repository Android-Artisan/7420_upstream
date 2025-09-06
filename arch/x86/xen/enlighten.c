/*
 * Core of Xen paravirt_ops implementation.
 *
 * This file contains the xen_paravirt_ops structure itself, and the
 * implementations for:
 * - privileged instructions
 * - interrupt flags
 * - segment operations
 * - booting and setup
 *
 * Jeremy Fitzhardinge <jeremy@xensource.com>, XenSource Inc, 2007
 */

#include <linux/cpu.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/preempt.h>
#include <linux/hardirq.h>
#include <linux/percpu.h>
#include <linux/delay.h>
#include <linux/start_kernel.h>
#include <linux/sched.h>
#include <linux/kprobes.h>
#include <linux/bootmem.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/page-flags.h>
#include <linux/highmem.h>
#include <linux/console.h>

#include <xen/interface/xen.h>
#include <xen/interface/physdev.h>
#include <xen/interface/vcpu.h>
#include <xen/interface/sched.h>
#include <xen/features.h>
#include <xen/page.h>
#include <xen/hvc-console.h>

#include <asm/paravirt.h>
#include <asm/page.h>
#include <linux/pci.h>
#include <linux/gfp.h>
#include <linux/memblock.h>
#include <linux/edd.h>

#ifdef CONFIG_KEXEC_CORE
#include <linux/kexec.h>
#endif

#include <xen/xen.h>
#include <xen/events.h>
#include <xen/interface/xen.h>
#include <xen/interface/version.h>
#include <xen/interface/physdev.h>
#include <xen/interface/vcpu.h>
#include <xen/interface/memory.h>
#include <xen/interface/nmi.h>
#include <xen/interface/xen-mca.h>
#include <xen/features.h>
#include <xen/page.h>
#include <xen/hvm.h>
#include <xen/hvc-console.h>
#include <xen/acpi.h>

#include <asm/paravirt.h>
#include <asm/apic.h>
#include <asm/page.h>
#include <asm/xen/pci.h>
#include <asm/xen/hypercall.h>
#include <asm/xen/hypervisor.h>
#include <asm/fixmap.h>
#include <asm/processor.h>
#include <asm/msr-index.h>
#include <asm/setup.h>
#include <asm/desc.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/reboot.h>

#include "xen-ops.h"
#include "mmu.h"
#include "multicalls.h"

EXPORT_SYMBOL_GPL(hypercall_page);

DEFINE_PER_CPU(struct vcpu_info *, xen_vcpu);
DEFINE_PER_CPU(struct vcpu_info, xen_vcpu_info);

/*
 * Identity map, in addition to plain kernel map.  This needs to be
 * large enough to allocate page table pages to allocate the rest.
 * Each page can map 2MB.
 */
static pte_t level1_ident_pgt[PTRS_PER_PTE * 4] __page_aligned_bss;

#ifdef CONFIG_X86_64
/* l3 pud for userspace vsyscall mapping */
static pud_t level3_user_vsyscall[PTRS_PER_PUD] __page_aligned_bss;
#endif /* CONFIG_X86_64 */

/*
 * Note about cr3 (pagetable base) values:
 *
 * xen_cr3 contains the current logical cr3 value; it contains the
 * last set cr3.  This may not be the current effective cr3, because
 * its update may be being lazily deferred.  However, a vcpu looking
 * at its own cr3 can use this value knowing that it everything will
 * be self-consistent.
 *
 * xen_current_cr3 contains the actual vcpu cr3; it is set once the
 * hypercall to set the vcpu cr3 is complete (so it may be a little
 * out of date, but it will never be set early).  If one vcpu is
 * looking at another vcpu's cr3 value, it should use this variable.
 */
DEFINE_PER_CPU(unsigned long, xen_cr3);	 /* cr3 stored as physaddr */
DEFINE_PER_CPU(unsigned long, xen_current_cr3);	 /* actual vcpu cr3 */
#include <asm/proto.h>
#include <asm/msr-index.h>
#include <asm/traps.h>
#include <asm/setup.h>
#include <asm/desc.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/reboot.h>
#include <asm/stackprotector.h>
#include <asm/hypervisor.h>
#include <asm/mach_traps.h>
#include <asm/mwait.h>
#include <asm/pci_x86.h>
#include <asm/cpu.h>

#ifdef CONFIG_ACPI
#include <linux/acpi.h>
#include <asm/acpi.h>
#include <acpi/pdc_intel.h>
#include <acpi/processor.h>
#include <xen/interface/platform.h>
#endif

#include "xen-ops.h"
#include "mmu.h"
#include "smp.h"
#include "multicalls.h"
#include "pmu.h"

EXPORT_SYMBOL_GPL(hypercall_page);

/*
 * Pointer to the xen_vcpu_info structure or
 * &HYPERVISOR_shared_info->vcpu_info[cpu]. See xen_hvm_init_shared_info
 * and xen_vcpu_setup for details. By default it points to share_info->vcpu_info
 * but if the hypervisor supports VCPUOP_register_vcpu_info then it can point
 * to xen_vcpu_info. The pointer is used in __xen_evtchn_do_upcall to
 * acknowledge pending events.
 * Also more subtly it is used by the patched version of irq enable/disable
 * e.g. xen_irq_enable_direct and xen_iret in PV mode.
 *
 * The desire to be able to do those mask/unmask operations as a single
 * instruction by using the per-cpu offset held in %gs is the real reason
 * vcpu info is in a per-cpu pointer and the original reason for this
 * hypercall.
 *
 */
DEFINE_PER_CPU(struct vcpu_info *, xen_vcpu);

/*
 * Per CPU pages used if hypervisor supports VCPUOP_register_vcpu_info
 * hypercall. This can be used both in PV and PVHVM mode. The structure
 * overrides the default per_cpu(xen_vcpu, cpu) value.
 */
DEFINE_PER_CPU(struct vcpu_info, xen_vcpu_info);

enum xen_domain_type xen_domain_type = XEN_NATIVE;
EXPORT_SYMBOL_GPL(xen_domain_type);

unsigned long *machine_to_phys_mapping = (void *)MACH2PHYS_VIRT_START;
EXPORT_SYMBOL(machine_to_phys_mapping);
unsigned long  machine_to_phys_nr;
EXPORT_SYMBOL(machine_to_phys_nr);

struct start_info *xen_start_info;
EXPORT_SYMBOL_GPL(xen_start_info);

struct shared_info xen_dummy_shared_info;

void *xen_initial_gdt;

RESERVE_BRK(shared_info_page_brk, PAGE_SIZE);
__read_mostly int xen_have_vector_callback;
EXPORT_SYMBOL_GPL(xen_have_vector_callback);

/*
 * Point at some empty memory to start with. We map the real shared_info
 * page as soon as fixmap is up and running.
 */
struct shared_info *HYPERVISOR_shared_info = (void *)&xen_dummy_shared_info;
struct shared_info *HYPERVISOR_shared_info = &xen_dummy_shared_info;

/*
 * Flag to determine whether vcpu info placement is available on all
 * VCPUs.  We assume it is to start with, and then set it to zero on
 * the first failure.  This is because it can succeed on some VCPUs
 * and not others, since it can involve hypervisor memory allocation,
 * or because the guest failed to guarantee all the appropriate
 * constraints on all VCPUs (ie buffer can't cross a page boundary).
 *
 * Note that any particular CPU may be using a placed vcpu structure,
 * but we can only optimise if the all are.
 *
 * 0: not available, 1: available
 */
static int have_vcpu_info_placement = 1;

struct tls_descs {
	struct desc_struct desc[3];
};

/*
 * Updating the 3 TLS descriptors in the GDT on every task switch is
 * surprisingly expensive so we avoid updating them if they haven't
 * changed.  Since Xen writes different descriptors than the one
 * passed in the update_descriptor hypercall we keep shadow copies to
 * compare against.
 */
static DEFINE_PER_CPU(struct tls_descs, shadow_tls_desc);

static void clamp_max_cpus(void)
{
#ifdef CONFIG_SMP
	if (setup_max_cpus > MAX_VIRT_CPUS)
		setup_max_cpus = MAX_VIRT_CPUS;
#endif
}

static void xen_vcpu_setup(int cpu)
{
	struct vcpu_register_vcpu_info info;
	int err;
	struct vcpu_info *vcpup;

	BUG_ON(HYPERVISOR_shared_info == &xen_dummy_shared_info);
	per_cpu(xen_vcpu, cpu) = &HYPERVISOR_shared_info->vcpu_info[cpu];

	if (!have_vcpu_info_placement)
		return;		/* already tested, not available */

	vcpup = &per_cpu(xen_vcpu_info, cpu);

	info.mfn = virt_to_mfn(vcpup);
	info.offset = offset_in_page(vcpup);

	printk(KERN_DEBUG "trying to map vcpu_info %d at %p, mfn %llx, offset %d\n",
	       cpu, vcpup, info.mfn, info.offset);

	/* Check to see if the hypervisor will put the vcpu_info
	   structure where we want it, which allows direct access via
	   a percpu-variable. */

	/*
	 * This path is called twice on PVHVM - first during bootup via
	 * smp_init -> xen_hvm_cpu_notify, and then if the VCPU is being
	 * hotplugged: cpu_up -> xen_hvm_cpu_notify.
	 * As we can only do the VCPUOP_register_vcpu_info once lets
	 * not over-write its result.
	 *
	 * For PV it is called during restore (xen_vcpu_restore) and bootup
	 * (xen_setup_vcpu_info_placement). The hotplug mechanism does not
	 * use this function.
	 */
	if (xen_hvm_domain()) {
		if (per_cpu(xen_vcpu, cpu) == &per_cpu(xen_vcpu_info, cpu))
			return;
	}
	if (cpu < MAX_VIRT_CPUS)
		per_cpu(xen_vcpu,cpu) = &HYPERVISOR_shared_info->vcpu_info[cpu];

	if (!have_vcpu_info_placement) {
		if (cpu >= MAX_VIRT_CPUS)
			clamp_max_cpus();
		return;
	}

	vcpup = &per_cpu(xen_vcpu_info, cpu);
	info.mfn = arbitrary_virt_to_mfn(vcpup);
	info.offset = offset_in_page(vcpup);

	/* Check to see if the hypervisor will put the vcpu_info
	   structure where we want it, which allows direct access via
	   a percpu-variable.
	   N.B. This hypercall can _only_ be called once per CPU. Subsequent
	   calls will error out with -EINVAL. This is due to the fact that
	   hypervisor has no unregister variant and this hypercall does not
	   allow to over-write info.mfn and info.offset.
	 */
	err = HYPERVISOR_vcpu_op(VCPUOP_register_vcpu_info, cpu, &info);

	if (err) {
		printk(KERN_DEBUG "register_vcpu_info failed: err=%d\n", err);
		have_vcpu_info_placement = 0;
		clamp_max_cpus();
	} else {
		/* This cpu is using the registered vcpu info, even if
		   later ones fail to. */
		per_cpu(xen_vcpu, cpu) = vcpup;

		printk(KERN_DEBUG "cpu %d using vcpu_info at %p\n",
		       cpu, vcpup);
	}
}

/*
 * On restore, set the vcpu placement up again.
 * If it fails, then we're in a bad state, since
 * we can't back out from using it...
 */
void xen_vcpu_restore(void)
{
	if (have_vcpu_info_placement) {
		int cpu;

		for_each_online_cpu(cpu) {
			bool other_cpu = (cpu != smp_processor_id());

			if (other_cpu &&
			    HYPERVISOR_vcpu_op(VCPUOP_down, cpu, NULL))
				BUG();

			xen_vcpu_setup(cpu);

			if (other_cpu &&
			    HYPERVISOR_vcpu_op(VCPUOP_up, cpu, NULL))
				BUG();
		}

		BUG_ON(!have_vcpu_info_placement);
	int cpu;

	for_each_possible_cpu(cpu) {
		bool other_cpu = (cpu != smp_processor_id());
		bool is_up = HYPERVISOR_vcpu_op(VCPUOP_is_up, cpu, NULL);

		if (other_cpu && is_up &&
		    HYPERVISOR_vcpu_op(VCPUOP_down, cpu, NULL))
			BUG();

		xen_setup_runstate_info(cpu);

		if (have_vcpu_info_placement)
			xen_vcpu_setup(cpu);

		if (other_cpu && is_up &&
		    HYPERVISOR_vcpu_op(VCPUOP_up, cpu, NULL))
			BUG();
	}
}

static void __init xen_banner(void)
{
	unsigned version = HYPERVISOR_xen_version(XENVER_version, NULL);
	struct xen_extraversion extra;
	HYPERVISOR_xen_version(XENVER_extraversion, &extra);

	printk(KERN_INFO "Booting paravirtualized kernel on %s\n",
	       pv_info.name);
	pr_info("Booting paravirtualized kernel %son %s\n",
		xen_feature(XENFEAT_auto_translated_physmap) ?
			"with PVH extensions " : "", pv_info.name);
	printk(KERN_INFO "Xen version: %d.%d%s%s\n",
	       version >> 16, version & 0xffff, extra.extraversion,
	       xen_feature(XENFEAT_mmu_pt_update_preserve_ad) ? " (preserve-AD)" : "");
}
/* Check if running on Xen version (major, minor) or later */
bool
xen_running_on_version_or_later(unsigned int major, unsigned int minor)
{
	unsigned int version;

	if (!xen_domain())
		return false;

	version = HYPERVISOR_xen_version(XENVER_version, NULL);
	if ((((version >> 16) == major) && ((version & 0xffff) >= minor)) ||
		((version >> 16) > major))
		return true;
	return false;
}

#define CPUID_THERM_POWER_LEAF 6
#define APERFMPERF_PRESENT 0

static __read_mostly unsigned int cpuid_leaf1_edx_mask = ~0;
static __read_mostly unsigned int cpuid_leaf1_ecx_mask = ~0;

static __read_mostly unsigned int cpuid_leaf1_ecx_set_mask;
static __read_mostly unsigned int cpuid_leaf5_ecx_val;
static __read_mostly unsigned int cpuid_leaf5_edx_val;

static void xen_cpuid(unsigned int *ax, unsigned int *bx,
		      unsigned int *cx, unsigned int *dx)
{
	unsigned maskedx = ~0;

	unsigned maskebx = ~0;
	unsigned maskecx = ~0;
	unsigned maskedx = ~0;
	unsigned setecx = 0;
	/*
	 * Mask out inconvenient features, to try and disable as many
	 * unsupported kernel subsystems as possible.
	 */
	if (*ax == 1)
		maskedx = ~((1 << X86_FEATURE_APIC) |  /* disable APIC */
			    (1 << X86_FEATURE_ACPI) |  /* disable ACPI */
			    (1 << X86_FEATURE_MCE)  |  /* disable MCE */
			    (1 << X86_FEATURE_MCA)  |  /* disable MCA */
			    (1 << X86_FEATURE_ACC));   /* thermal monitoring */
	switch (*ax) {
	case 1:
		maskecx = cpuid_leaf1_ecx_mask;
		setecx = cpuid_leaf1_ecx_set_mask;
		maskedx = cpuid_leaf1_edx_mask;
		break;

	case CPUID_MWAIT_LEAF:
		/* Synthesize the values.. */
		*ax = 0;
		*bx = 0;
		*cx = cpuid_leaf5_ecx_val;
		*dx = cpuid_leaf5_edx_val;
		return;

	case CPUID_THERM_POWER_LEAF:
		/* Disabling APERFMPERF for kernel usage */
		maskecx = ~(1 << APERFMPERF_PRESENT);
		break;

	case 0xb:
		/* Suppress extended topology stuff */
		maskebx = 0;
		break;
	}

	asm(XEN_EMULATE_PREFIX "cpuid"
		: "=a" (*ax),
		  "=b" (*bx),
		  "=c" (*cx),
		  "=d" (*dx)
		: "0" (*ax), "2" (*cx));
	*dx &= maskedx;

	*bx &= maskebx;
	*cx &= maskecx;
	*cx |= setecx;
	*dx &= maskedx;

}

static bool __init xen_check_mwait(void)
{
#ifdef CONFIG_ACPI
	struct xen_platform_op op = {
		.cmd			= XENPF_set_processor_pminfo,
		.u.set_pminfo.id	= -1,
		.u.set_pminfo.type	= XEN_PM_PDC,
	};
	uint32_t buf[3];
	unsigned int ax, bx, cx, dx;
	unsigned int mwait_mask;

	/* We need to determine whether it is OK to expose the MWAIT
	 * capability to the kernel to harvest deeper than C3 states from ACPI
	 * _CST using the processor_harvest_xen.c module. For this to work, we
	 * need to gather the MWAIT_LEAF values (which the cstate.c code
	 * checks against). The hypervisor won't expose the MWAIT flag because
	 * it would break backwards compatibility; so we will find out directly
	 * from the hardware and hypercall.
	 */
	if (!xen_initial_domain())
		return false;

	/*
	 * When running under platform earlier than Xen4.2, do not expose
	 * mwait, to avoid the risk of loading native acpi pad driver
	 */
	if (!xen_running_on_version_or_later(4, 2))
		return false;

	ax = 1;
	cx = 0;

	native_cpuid(&ax, &bx, &cx, &dx);

	mwait_mask = (1 << (X86_FEATURE_EST % 32)) |
		     (1 << (X86_FEATURE_MWAIT % 32));

	if ((cx & mwait_mask) != mwait_mask)
		return false;

	/* We need to emulate the MWAIT_LEAF and for that we need both
	 * ecx and edx. The hypercall provides only partial information.
	 */

	ax = CPUID_MWAIT_LEAF;
	bx = 0;
	cx = 0;
	dx = 0;

	native_cpuid(&ax, &bx, &cx, &dx);

	/* Ask the Hypervisor whether to clear ACPI_PDC_C_C2C3_FFH. If so,
	 * don't expose MWAIT_LEAF and let ACPI pick the IOPORT version of C3.
	 */
	buf[0] = ACPI_PDC_REVISION_ID;
	buf[1] = 1;
	buf[2] = (ACPI_PDC_C_CAPABILITY_SMP | ACPI_PDC_EST_CAPABILITY_SWSMP);

	set_xen_guest_handle(op.u.set_pminfo.pdc, buf);

	if ((HYPERVISOR_dom0_op(&op) == 0) &&
	    (buf[2] & (ACPI_PDC_C_C1_FFH | ACPI_PDC_C_C2C3_FFH))) {
		cpuid_leaf5_ecx_val = cx;
		cpuid_leaf5_edx_val = dx;
	}
	return true;
#else
	return false;
#endif
}
static void __init xen_init_cpuid_mask(void)
{
	unsigned int ax, bx, cx, dx;
	unsigned int xsave_mask;

	cpuid_leaf1_edx_mask =
		~((1 << X86_FEATURE_MTRR) |  /* disable MTRR */
		  (1 << X86_FEATURE_ACC));   /* thermal monitoring */

	/*
	 * Xen PV would need some work to support PCID: CR3 handling as well
	 * as xen_flush_tlb_others() would need updating.
	 */
	cpuid_leaf1_ecx_mask &= ~(1 << (X86_FEATURE_PCID % 32));  /* disable PCID */

	if (!xen_initial_domain())
		cpuid_leaf1_edx_mask &=
			~((1 << X86_FEATURE_ACPI));  /* disable ACPI */

	cpuid_leaf1_ecx_mask &= ~(1 << (X86_FEATURE_X2APIC % 32));

	ax = 1;
	cx = 0;
	cpuid(1, &ax, &bx, &cx, &dx);

	xsave_mask =
		(1 << (X86_FEATURE_XSAVE % 32)) |
		(1 << (X86_FEATURE_OSXSAVE % 32));

	/* Xen will set CR4.OSXSAVE if supported and not disabled by force */
	if ((cx & xsave_mask) != xsave_mask)
		cpuid_leaf1_ecx_mask &= ~xsave_mask; /* disable XSAVE & OSXSAVE */
	if (xen_check_mwait())
		cpuid_leaf1_ecx_set_mask = (1 << (X86_FEATURE_MWAIT % 32));
}

static void __init xen_init_capabilities(void)
{
	if (xen_pv_domain())
		setup_force_cpu_cap(X86_FEATURE_XENPV);
}

static void xen_set_debugreg(int reg, unsigned long val)
{
	HYPERVISOR_set_debugreg(reg, val);
}

static unsigned long xen_get_debugreg(int reg)
{
	return HYPERVISOR_get_debugreg(reg);
}

static unsigned long xen_save_fl(void)
{
	struct vcpu_info *vcpu;
	unsigned long flags;

	vcpu = x86_read_percpu(xen_vcpu);

	/* flag has opposite sense of mask */
	flags = !vcpu->evtchn_upcall_mask;

	/* convert to IF type flag
	   -0 -> 0x00000000
	   -1 -> 0xffffffff
	*/
	return (-flags) & X86_EFLAGS_IF;
}

static void xen_restore_fl(unsigned long flags)
{
	struct vcpu_info *vcpu;

	/* convert from IF type flag */
	flags = !(flags & X86_EFLAGS_IF);

	/* There's a one instruction preempt window here.  We need to
	   make sure we're don't switch CPUs between getting the vcpu
	   pointer and updating the mask. */
	preempt_disable();
	vcpu = x86_read_percpu(xen_vcpu);
	vcpu->evtchn_upcall_mask = flags;
	preempt_enable_no_resched();

	/* Doesn't matter if we get preempted here, because any
	   pending event will get dealt with anyway. */

	if (flags == 0) {
		preempt_check_resched();
		barrier(); /* unmask then check (avoid races) */
		if (unlikely(vcpu->evtchn_upcall_pending))
			force_evtchn_callback();
	}
}

static void xen_irq_disable(void)
{
	/* There's a one instruction preempt window here.  We need to
	   make sure we're don't switch CPUs between getting the vcpu
	   pointer and updating the mask. */
	preempt_disable();
	x86_read_percpu(xen_vcpu)->evtchn_upcall_mask = 1;
	preempt_enable_no_resched();
}

static void xen_irq_enable(void)
{
	struct vcpu_info *vcpu;

	/* We don't need to worry about being preempted here, since
	   either a) interrupts are disabled, so no preemption, or b)
	   the caller is confused and is trying to re-enable interrupts
	   on an indeterminate processor. */

	vcpu = x86_read_percpu(xen_vcpu);
	vcpu->evtchn_upcall_mask = 0;

	/* Doesn't matter if we get preempted here, because any
	   pending event will get dealt with anyway. */

	barrier(); /* unmask then check (avoid races) */
	if (unlikely(vcpu->evtchn_upcall_pending))
		force_evtchn_callback();
}

static void xen_safe_halt(void)
{
	/* Blocking includes an implicit local_irq_enable(). */
	if (HYPERVISOR_sched_op(SCHEDOP_block, NULL) != 0)
		BUG();
}

static void xen_halt(void)
{
	if (irqs_disabled())
		HYPERVISOR_vcpu_op(VCPUOP_down, smp_processor_id(), NULL);
	else
		xen_safe_halt();
}

static void xen_leave_lazy(void)
{
	paravirt_leave_lazy(paravirt_get_lazy_mode());
	xen_mc_flush();
static void xen_end_context_switch(struct task_struct *next)
{
	xen_mc_flush();
	paravirt_end_context_switch(next);
}

static unsigned long xen_store_tr(void)
{
	return 0;
}

/*
 * Set the page permissions for a particular virtual address.  If the
 * address is a vmalloc mapping (or other non-linear mapping), then
 * find the linear mapping of the page and also set its protections to
 * match.
 */
static void set_aliased_prot(void *v, pgprot_t prot)
{
	int level;
	pte_t *ptep;
	pte_t pte;
	unsigned long pfn;
	struct page *page;
	unsigned char dummy;

	ptep = lookup_address((unsigned long)v, &level);
	BUG_ON(ptep == NULL);

	pfn = pte_pfn(*ptep);
	page = pfn_to_page(pfn);

	pte = pfn_pte(pfn, prot);

	/*
	 * Careful: update_va_mapping() will fail if the virtual address
	 * we're poking isn't populated in the page tables.  We don't
	 * need to worry about the direct map (that's always in the page
	 * tables), but we need to be careful about vmap space.  In
	 * particular, the top level page table can lazily propagate
	 * entries between processes, so if we've switched mms since we
	 * vmapped the target in the first place, we might not have the
	 * top-level page table entry populated.
	 *
	 * We disable preemption because we want the same mm active when
	 * we probe the target and when we issue the hypercall.  We'll
	 * have the same nominal mm, but if we're a kernel thread, lazy
	 * mm dropping could change our pgd.
	 *
	 * Out of an abundance of caution, this uses __get_user() to fault
	 * in the target address just in case there's some obscure case
	 * in which the target address isn't readable.
	 */

	preempt_disable();

	pagefault_disable();	/* Avoid warnings due to being atomic. */
	__get_user(dummy, (unsigned char __user __force *)v);
	pagefault_enable();

	if (HYPERVISOR_update_va_mapping((unsigned long)v, pte, 0))
		BUG();

	if (!PageHighMem(page)) {
		void *av = __va(PFN_PHYS(pfn));

		if (av != v)
			if (HYPERVISOR_update_va_mapping((unsigned long)av, pte, 0))
				BUG();
	} else
		kmap_flush_unused();

	preempt_enable();
}

static void xen_alloc_ldt(struct desc_struct *ldt, unsigned entries)
{
	const unsigned entries_per_page = PAGE_SIZE / LDT_ENTRY_SIZE;
	int i;

	/*
	 * We need to mark the all aliases of the LDT pages RO.  We
	 * don't need to call vm_flush_aliases(), though, since that's
	 * only responsible for flushing aliases out the TLBs, not the
	 * page tables, and Xen will flush the TLB for us if needed.
	 *
	 * To avoid confusing future readers: none of this is necessary
	 * to load the LDT.  The hypervisor only checks this when the
	 * LDT is faulted in due to subsequent descriptor access.
	 */

	for(i = 0; i < entries; i += entries_per_page)
		set_aliased_prot(ldt + i, PAGE_KERNEL_RO);
}

static void xen_free_ldt(struct desc_struct *ldt, unsigned entries)
{
	const unsigned entries_per_page = PAGE_SIZE / LDT_ENTRY_SIZE;
	int i;

	for(i = 0; i < entries; i += entries_per_page)
		set_aliased_prot(ldt + i, PAGE_KERNEL);
}

static void xen_set_ldt(const void *addr, unsigned entries)
{
	struct mmuext_op *op;
	struct multicall_space mcs = xen_mc_entry(sizeof(*op));

	trace_xen_cpu_set_ldt(addr, entries);

	op = mcs.args;
	op->cmd = MMUEXT_SET_LDT;
	op->arg1.linear_addr = (unsigned long)addr;
	op->arg2.nr_ents = entries;

	MULTI_mmuext_op(mcs.mc, op, 1, NULL, DOMID_SELF);

	xen_mc_issue(PARAVIRT_LAZY_CPU);
}

static void xen_load_gdt(const struct desc_ptr *dtr)
{
	unsigned long *frames;
	unsigned long va = dtr->address;
	unsigned int size = dtr->size + 1;
	unsigned pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
	int f;
	struct multicall_space mcs;

	/* A GDT can be up to 64k in size, which corresponds to 8192
	   8-byte entries, or 16 4k pages.. */
	unsigned long va = dtr->address;
	unsigned int size = dtr->size + 1;
	unsigned pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
	unsigned long frames[pages];
	int f;

	/*
	 * A GDT can be up to 64k in size, which corresponds to 8192
	 * 8-byte entries, or 16 4k pages..
	 */

	BUG_ON(size > 65536);
	BUG_ON(va & ~PAGE_MASK);

	mcs = xen_mc_entry(sizeof(*frames) * pages);
	frames = mcs.args;

	for (f = 0; va < dtr->address + size; va += PAGE_SIZE, f++) {
		frames[f] = virt_to_mfn(va);
		make_lowmem_page_readonly((void *)va);
	}

	MULTI_set_gdt(mcs.mc, frames, size / sizeof(struct desc_struct));

	xen_mc_issue(PARAVIRT_LAZY_CPU);
	for (f = 0; va < dtr->address + size; va += PAGE_SIZE, f++) {
		int level;
		pte_t *ptep;
		unsigned long pfn, mfn;
		void *virt;

		/*
		 * The GDT is per-cpu and is in the percpu data area.
		 * That can be virtually mapped, so we need to do a
		 * page-walk to get the underlying MFN for the
		 * hypercall.  The page can also be in the kernel's
		 * linear range, so we need to RO that mapping too.
		 */
		ptep = lookup_address(va, &level);
		BUG_ON(ptep == NULL);

		pfn = pte_pfn(*ptep);
		mfn = pfn_to_mfn(pfn);
		virt = __va(PFN_PHYS(pfn));

		frames[f] = mfn;

		make_lowmem_page_readonly((void *)va);
		make_lowmem_page_readonly(virt);
	}

	if (HYPERVISOR_set_gdt(frames, size / sizeof(struct desc_struct)))
		BUG();
}

/*
 * load_gdt for early boot, when the gdt is only mapped once
 */
static void __init xen_load_gdt_boot(const struct desc_ptr *dtr)
{
	unsigned long va = dtr->address;
	unsigned int size = dtr->size + 1;
	unsigned pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
	unsigned long frames[pages];
	int f;

	/*
	 * A GDT can be up to 64k in size, which corresponds to 8192
	 * 8-byte entries, or 16 4k pages..
	 */

	BUG_ON(size > 65536);
	BUG_ON(va & ~PAGE_MASK);

	for (f = 0; va < dtr->address + size; va += PAGE_SIZE, f++) {
		pte_t pte;
		unsigned long pfn, mfn;

		pfn = virt_to_pfn(va);
		mfn = pfn_to_mfn(pfn);

		pte = pfn_pte(pfn, PAGE_KERNEL_RO);

		if (HYPERVISOR_update_va_mapping((unsigned long)va, pte, 0))
			BUG();

		frames[f] = mfn;
	}

	if (HYPERVISOR_set_gdt(frames, size / sizeof(struct desc_struct)))
		BUG();
}

static inline bool desc_equal(const struct desc_struct *d1,
			      const struct desc_struct *d2)
{
	return d1->a == d2->a && d1->b == d2->b;
}

static void load_TLS_descriptor(struct thread_struct *t,
				unsigned int cpu, unsigned int i)
{
	struct desc_struct *gdt = get_cpu_gdt_table(cpu);
	xmaddr_t maddr = virt_to_machine(&gdt[GDT_ENTRY_TLS_MIN+i]);
	struct multicall_space mc = __xen_mc_entry(0);
	struct desc_struct *shadow = &per_cpu(shadow_tls_desc, cpu).desc[i];
	struct desc_struct *gdt;
	xmaddr_t maddr;
	struct multicall_space mc;

	if (desc_equal(shadow, &t->tls_array[i]))
		return;

	*shadow = t->tls_array[i];

	gdt = get_cpu_gdt_table(cpu);
	maddr = arbitrary_virt_to_machine(&gdt[GDT_ENTRY_TLS_MIN+i]);
	mc = __xen_mc_entry(0);

	MULTI_update_descriptor(mc.mc, maddr.maddr, t->tls_array[i]);
}

static void xen_load_tls(struct thread_struct *t, unsigned int cpu)
{
	/*
	 * XXX sleazy hack: If we're being called in a lazy-cpu zone,
	 * it means we're in a context switch, and %gs has just been
	 * saved.  This means we can zero it out to prevent faults on
	 * exit from the hypervisor if the next process has no %gs.
	 * Either way, it has been saved, and the new value will get
	 * loaded properly.  This will go away as soon as Xen has been
	 * modified to not save/restore %gs for normal hypercalls.
	 * XXX sleazy hack: If we're being called in a lazy-cpu zone
	 * and lazy gs handling is enabled, it means we're in a
	 * context switch, and %gs has just been saved.  This means we
	 * can zero it out to prevent faults on exit from the
	 * hypervisor if the next process has no %gs.  Either way, it
	 * has been saved, and the new value will get loaded properly.
	 * This will go away as soon as Xen has been modified to not
	 * save/restore %gs for normal hypercalls.
	 *
	 * On x86_64, this hack is not used for %gs, because gs points
	 * to KERNEL_GS_BASE (and uses it for PDA references), so we
	 * must not zero %gs on x86_64
	 *
	 * For x86_64, we need to zero %fs, otherwise we may get an
	 * exception between the new %fs descriptor being loaded and
	 * %fs being effectively cleared at __switch_to().
	 */
	if (paravirt_get_lazy_mode() == PARAVIRT_LAZY_CPU) {
#ifdef CONFIG_X86_32
		loadsegment(gs, 0);
		lazy_load_gs(0);
#else
		loadsegment(fs, 0);
#endif
	}

	xen_mc_batch();

	load_TLS_descriptor(t, cpu, 0);
	load_TLS_descriptor(t, cpu, 1);
	load_TLS_descriptor(t, cpu, 2);

	xen_mc_issue(PARAVIRT_LAZY_CPU);
}

#ifdef CONFIG_X86_64
static void xen_load_gs_index(unsigned int idx)
{
	if (HYPERVISOR_set_segment_base(SEGBASE_GS_USER_SEL, idx))
		BUG();
}
#endif

static void xen_write_ldt_entry(struct desc_struct *dt, int entrynum,
				const void *ptr)
{
	unsigned long lp = (unsigned long)&dt[entrynum];
	xmaddr_t mach_lp = virt_to_machine(lp);
	u64 entry = *(u64 *)ptr;

	xmaddr_t mach_lp = arbitrary_virt_to_machine(&dt[entrynum]);
	u64 entry = *(u64 *)ptr;

	trace_xen_cpu_write_ldt_entry(dt, entrynum, entry);

	preempt_disable();

	xen_mc_flush();
	if (HYPERVISOR_update_descriptor(mach_lp.maddr, entry))
		BUG();

	preempt_enable();
}

static int cvt_gate_to_trap(int vector, const gate_desc *val,
			    struct trap_info *info)
{
	if (val->type != 0xf && val->type != 0xe)
		return 0;

	info->vector = vector;
	info->address = gate_offset(*val);
	info->cs = gate_segment(*val);
	info->flags = val->dpl;
	/* interrupt gates clear IF */
	if (val->type == 0xe)
		info->flags |= 4;
	unsigned long addr;

	if (val->type != GATE_TRAP && val->type != GATE_INTERRUPT)
		return 0;

	info->vector = vector;

	addr = gate_offset(*val);
#ifdef CONFIG_X86_64
	/*
	 * Look for known traps using IST, and substitute them
	 * appropriately.  The debugger ones are the only ones we care
	 * about.  Xen will handle faults like double_fault,
	 * so we should never see them.  Warn if
	 * there's an unexpected IST-using fault handler.
	 */
	if (addr == (unsigned long)debug)
		addr = (unsigned long)xen_debug;
	else if (addr == (unsigned long)int3)
		addr = (unsigned long)xen_int3;
	else if (addr == (unsigned long)stack_segment)
		addr = (unsigned long)xen_stack_segment;
	else if (addr == (unsigned long)double_fault) {
		/* Don't need to handle these */
		return 0;
#ifdef CONFIG_X86_MCE
	} else if (addr == (unsigned long)machine_check) {
		/*
		 * when xen hypervisor inject vMCE to guest,
		 * use native mce handler to handle it
		 */
		;
#endif
	} else if (addr == (unsigned long)nmi)
		/*
		 * Use the native version as well.
		 */
		;
	else {
		/* Some other trap using IST? */
		if (WARN_ON(val->ist != 0))
			return 0;
	}
#endif	/* CONFIG_X86_64 */
	info->address = addr;

	info->cs = gate_segment(*val);
	info->flags = val->dpl;
	/* interrupt gates clear IF */
	if (val->type == GATE_INTERRUPT)
		info->flags |= 1 << 2;

	return 1;
}

/* Locations of each CPU's IDT */
static DEFINE_PER_CPU(struct desc_ptr, idt_desc);

/* Set an IDT entry.  If the entry is part of the current IDT, then
   also update Xen. */
static void xen_write_idt_entry(gate_desc *dt, int entrynum, const gate_desc *g)
{
	unsigned long p = (unsigned long)&dt[entrynum];
	unsigned long start, end;

	preempt_disable();

	start = __get_cpu_var(idt_desc).address;
	end = start + __get_cpu_var(idt_desc).size + 1;
	trace_xen_cpu_write_idt_entry(dt, entrynum, g);

	preempt_disable();

	start = __this_cpu_read(idt_desc.address);
	end = start + __this_cpu_read(idt_desc.size) + 1;

	xen_mc_flush();

	native_write_idt_entry(dt, entrynum, g);

	if (p >= start && (p + 8) <= end) {
		struct trap_info info[2];

		info[1].address = 0;

		if (cvt_gate_to_trap(entrynum, g, &info[0]))
			if (HYPERVISOR_set_trap_table(info))
				BUG();
	}

	preempt_enable();
}

static unsigned xen_convert_trap_info(const struct desc_ptr *desc,
				      struct trap_info *traps, bool full)
{
	unsigned in, out, count;

	count = (desc->size+1) / sizeof(gate_desc);
	BUG_ON(count > 256);

	for (in = out = 0; in < count; in++) {
		gate_desc *entry = (gate_desc*)(desc->address) + in;

		if (cvt_gate_to_trap(in, entry, &traps[out]) || full)
			out++;
	}

	return out;
}

void xen_copy_trap_info(struct trap_info *traps)
{
	const struct desc_ptr *desc = &__get_cpu_var(idt_desc);
	const struct desc_ptr *desc = this_cpu_ptr(&idt_desc);

	xen_convert_trap_info(desc, traps, true);
}

/* Load a new IDT into Xen.  In principle this can be per-CPU, so we
   hold a spinlock to protect the static traps[] array (static because
   it avoids allocation, and saves stack space). */
static void xen_load_idt(const struct desc_ptr *desc)
{
	static DEFINE_SPINLOCK(lock);
	static struct trap_info traps[257];
	unsigned out;

	spin_lock(&lock);

	__get_cpu_var(idt_desc) = *desc;
	trace_xen_cpu_load_idt(desc);

	spin_lock(&lock);

	memcpy(this_cpu_ptr(&idt_desc), desc, sizeof(idt_desc));

	out = xen_convert_trap_info(desc, traps, false);
	memset(&traps[out], 0, sizeof(traps[0]));

	xen_mc_flush();
	if (HYPERVISOR_set_trap_table(traps))
		BUG();

	spin_unlock(&lock);
}

/* Write a GDT descriptor entry.  Ignore LDT descriptors, since
   they're handled differently. */
static void xen_write_gdt_entry(struct desc_struct *dt, int entry,
				const void *desc, int type)
{
	trace_xen_cpu_write_gdt_entry(dt, entry, desc, type);

	preempt_disable();

	switch (type) {
	case DESC_LDT:
	case DESC_TSS:
		/* ignore */
		break;

	default: {
		xmaddr_t maddr = virt_to_machine(&dt[entry]);
		xmaddr_t maddr = arbitrary_virt_to_machine(&dt[entry]);

		xen_mc_flush();
		if (HYPERVISOR_update_descriptor(maddr.maddr, *(u64 *)desc))
			BUG();
	}

	}

	preempt_enable();
}

static void xen_load_sp0(struct tss_struct *tss,
			  struct thread_struct *thread)
{
	struct multicall_space mcs = xen_mc_entry(0);
	MULTI_stack_switch(mcs.mc, __KERNEL_DS, thread->sp0);
	xen_mc_issue(PARAVIRT_LAZY_CPU);
/*
 * Version of write_gdt_entry for use at early boot-time needed to
 * update an entry as simply as possible.
 */
static void __init xen_write_gdt_entry_boot(struct desc_struct *dt, int entry,
					    const void *desc, int type)
{
	trace_xen_cpu_write_gdt_entry(dt, entry, desc, type);

	switch (type) {
	case DESC_LDT:
	case DESC_TSS:
		/* ignore */
		break;

	default: {
		xmaddr_t maddr = virt_to_machine(&dt[entry]);

		if (HYPERVISOR_update_descriptor(maddr.maddr, *(u64 *)desc))
			dt[entry] = *(struct desc_struct *)desc;
	}

	}
}

static void xen_load_sp0(struct tss_struct *tss,
			 struct thread_struct *thread)
{
	struct multicall_space mcs;

	mcs = xen_mc_entry(0);
	MULTI_stack_switch(mcs.mc, __KERNEL_DS, thread->sp0);
	xen_mc_issue(PARAVIRT_LAZY_CPU);
	tss->x86_tss.sp0 = thread->sp0;
}

void xen_set_iopl_mask(unsigned mask)
{
	struct physdev_set_iopl set_iopl;

	/* Force the change at ring 0. */
	set_iopl.iopl = (mask == 0) ? 1 : (mask >> 12) & 3;
	HYPERVISOR_physdev_op(PHYSDEVOP_set_iopl, &set_iopl);
}

static void xen_io_delay(void)
{
}

#ifdef CONFIG_X86_LOCAL_APIC
static u32 xen_apic_read(unsigned long reg)
{
	return 0;
}

static void xen_apic_write(unsigned long reg, u32 val)
{
	/* Warn to see if there's any stray references */
	WARN_ON(1);
}
#endif

static void xen_flush_tlb(void)
{
	struct mmuext_op *op;
	struct multicall_space mcs;

	preempt_disable();

	mcs = xen_mc_entry(sizeof(*op));

	op = mcs.args;
	op->cmd = MMUEXT_TLB_FLUSH_LOCAL;
	MULTI_mmuext_op(mcs.mc, op, 1, NULL, DOMID_SELF);

	xen_mc_issue(PARAVIRT_LAZY_MMU);

	preempt_enable();
}

static void xen_flush_tlb_single(unsigned long addr)
{
	struct mmuext_op *op;
	struct multicall_space mcs;

	preempt_disable();

	mcs = xen_mc_entry(sizeof(*op));
	op = mcs.args;
	op->cmd = MMUEXT_INVLPG_LOCAL;
	op->arg1.linear_addr = addr & PAGE_MASK;
	MULTI_mmuext_op(mcs.mc, op, 1, NULL, DOMID_SELF);

	xen_mc_issue(PARAVIRT_LAZY_MMU);

	preempt_enable();
}

static void xen_flush_tlb_others(const cpumask_t *cpus, struct mm_struct *mm,
				 unsigned long va)
{
	struct {
		struct mmuext_op op;
		cpumask_t mask;
	} *args;
	cpumask_t cpumask = *cpus;
	struct multicall_space mcs;

	/*
	 * A couple of (to be removed) sanity checks:
	 *
	 * - current CPU must not be in mask
	 * - mask must exist :)
	 */
	BUG_ON(cpus_empty(cpumask));
	BUG_ON(cpu_isset(smp_processor_id(), cpumask));
	BUG_ON(!mm);

	/* If a CPU which we ran on has gone down, OK. */
	cpus_and(cpumask, cpumask, cpu_online_map);
	if (cpus_empty(cpumask))
		return;

	mcs = xen_mc_entry(sizeof(*args));
	args = mcs.args;
	args->mask = cpumask;
	args->op.arg2.vcpumask = &args->mask;

	if (va == TLB_FLUSH_ALL) {
		args->op.cmd = MMUEXT_TLB_FLUSH_MULTI;
	} else {
		args->op.cmd = MMUEXT_INVLPG_MULTI;
		args->op.arg1.linear_addr = va;
	}

	MULTI_mmuext_op(mcs.mc, &args->op, 1, NULL, DOMID_SELF);

	xen_mc_issue(PARAVIRT_LAZY_MMU);
}

static void xen_clts(void)
{
	struct multicall_space mcs;

	mcs = xen_mc_entry(0);

	MULTI_fpu_taskswitch(mcs.mc, 0);

	xen_mc_issue(PARAVIRT_LAZY_CPU);
}

static DEFINE_PER_CPU(unsigned long, xen_cr0_value);

static unsigned long xen_read_cr0(void)
{
	unsigned long cr0 = this_cpu_read(xen_cr0_value);

	if (unlikely(cr0 == 0)) {
		cr0 = native_read_cr0();
		this_cpu_write(xen_cr0_value, cr0);
	}

	return cr0;
}

static void xen_write_cr0(unsigned long cr0)
{
	struct multicall_space mcs;

	this_cpu_write(xen_cr0_value, cr0);

	/* Only pay attention to cr0.TS; everything else is
	   ignored. */
	mcs = xen_mc_entry(0);

	MULTI_fpu_taskswitch(mcs.mc, (cr0 & X86_CR0_TS) != 0);

	xen_mc_issue(PARAVIRT_LAZY_CPU);
}

static void xen_write_cr2(unsigned long cr2)
{
	x86_read_percpu(xen_vcpu)->arch.cr2 = cr2;
}

static unsigned long xen_read_cr2(void)
{
	return x86_read_percpu(xen_vcpu)->arch.cr2;
}

static unsigned long xen_read_cr2_direct(void)
{
	return x86_read_percpu(xen_vcpu_info.arch.cr2);
}

static void xen_write_cr4(unsigned long cr4)
{
	cr4 &= ~X86_CR4_PGE;
	cr4 &= ~X86_CR4_PSE;

	native_write_cr4(cr4);
}

static unsigned long xen_read_cr3(void)
{
	return x86_read_percpu(xen_cr3);
}

static void set_current_cr3(void *v)
{
	x86_write_percpu(xen_current_cr3, (unsigned long)v);
}

static void __xen_write_cr3(bool kernel, unsigned long cr3)
{
	struct mmuext_op *op;
	struct multicall_space mcs;
	unsigned long mfn;

	if (cr3)
		mfn = pfn_to_mfn(PFN_DOWN(cr3));
	else
		mfn = 0;

	WARN_ON(mfn == 0 && kernel);

	mcs = __xen_mc_entry(sizeof(*op));

	op = mcs.args;
	op->cmd = kernel ? MMUEXT_NEW_BASEPTR : MMUEXT_NEW_USER_BASEPTR;
	op->arg1.mfn = mfn;

	MULTI_mmuext_op(mcs.mc, op, 1, NULL, DOMID_SELF);

	if (kernel) {
		x86_write_percpu(xen_cr3, cr3);

		/* Update xen_current_cr3 once the batch has actually
		   been submitted. */
		xen_mc_callback(set_current_cr3, (void *)cr3);
	}
}

static void xen_write_cr3(unsigned long cr3)
{
	BUG_ON(preemptible());

	xen_mc_batch();  /* disables interrupts */

	/* Update while interrupts are disabled, so its atomic with
	   respect to ipis */
	x86_write_percpu(xen_cr3, cr3);

	__xen_write_cr3(true, cr3);

#ifdef CONFIG_X86_64
	{
		pgd_t *user_pgd = xen_get_user_pgd(__va(cr3));
		if (user_pgd)
			__xen_write_cr3(false, __pa(user_pgd));
		else
			__xen_write_cr3(false, 0);
	}
#endif

	xen_mc_issue(PARAVIRT_LAZY_CPU);  /* interrupts restored */
static void xen_write_cr4(unsigned long cr4)
{
	cr4 &= ~(X86_CR4_PGE | X86_CR4_PSE | X86_CR4_PCE);

	native_write_cr4(cr4);
}
#ifdef CONFIG_X86_64
static inline unsigned long xen_read_cr8(void)
{
	return 0;
}
static inline void xen_write_cr8(unsigned long val)
{
	BUG_ON(val);
}
#endif

static u64 xen_read_msr_safe(unsigned int msr, int *err)
{
	u64 val;

	if (pmu_msr_read(msr, &val, err))
		return val;

	val = native_read_msr_safe(msr, err);
	switch (msr) {
	case MSR_IA32_APICBASE:
#ifdef CONFIG_X86_X2APIC
		if (!(cpuid_ecx(1) & (1 << (X86_FEATURE_X2APIC & 31))))
#endif
			val &= ~X2APIC_ENABLE;
		break;
	}
	return val;
}

static int xen_write_msr_safe(unsigned int msr, unsigned low, unsigned high)
{
	int ret;

	ret = 0;

	switch(msr) {
	switch (msr) {
#ifdef CONFIG_X86_64
		unsigned which;
		u64 base;

	case MSR_FS_BASE:		which = SEGBASE_FS; goto set;
	case MSR_KERNEL_GS_BASE:	which = SEGBASE_GS_USER; goto set;
	case MSR_GS_BASE:		which = SEGBASE_GS_KERNEL; goto set;

	set:
		base = ((u64)high << 32) | low;
		if (HYPERVISOR_set_segment_base(which, base) != 0)
			ret = -EFAULT;
		break;
#endif
	default:
		ret = native_write_msr_safe(msr, low, high);
			ret = -EIO;
		break;
#endif

	case MSR_STAR:
	case MSR_CSTAR:
	case MSR_LSTAR:
	case MSR_SYSCALL_MASK:
	case MSR_IA32_SYSENTER_CS:
	case MSR_IA32_SYSENTER_ESP:
	case MSR_IA32_SYSENTER_EIP:
		/* Fast syscall setup is all done in hypercalls, so
		   these are all ignored.  Stub them out here to stop
		   Xen console noise. */
		break;

	default:
		if (!pmu_msr_write(msr, low, high, &ret))
			ret = native_write_msr_safe(msr, low, high);
	}

	return ret;
}

/* Early in boot, while setting up the initial pagetable, assume
   everything is pinned. */
static __init void xen_alloc_pte_init(struct mm_struct *mm, u32 pfn)
{
#ifdef CONFIG_FLATMEM
	BUG_ON(mem_map);	/* should only be used early */
#endif
	make_lowmem_page_readonly(__va(PFN_PHYS(pfn)));
}

/* Early release_pte assumes that all pts are pinned, since there's
   only init_mm and anything attached to that is pinned. */
static void xen_release_pte_init(u32 pfn)
{
	make_lowmem_page_readwrite(__va(PFN_PHYS(pfn)));
}

static void pin_pagetable_pfn(unsigned cmd, unsigned long pfn)
{
	struct mmuext_op op;
	op.cmd = cmd;
	op.arg1.mfn = pfn_to_mfn(pfn);
	if (HYPERVISOR_mmuext_op(&op, 1, NULL, DOMID_SELF))
		BUG();
}

/* This needs to make sure the new pte page is pinned iff its being
   attached to a pinned pagetable. */
static void xen_alloc_ptpage(struct mm_struct *mm, u32 pfn, unsigned level)
{
	struct page *page = pfn_to_page(pfn);

	if (PagePinned(virt_to_page(mm->pgd))) {
		SetPagePinned(page);

		if (!PageHighMem(page)) {
			make_lowmem_page_readonly(__va(PFN_PHYS(pfn)));
			if (level == PT_PTE)
				pin_pagetable_pfn(MMUEXT_PIN_L1_TABLE, pfn);
		} else
			/* make sure there are no stray mappings of
			   this page */
			kmap_flush_unused();
	}
}

static void xen_alloc_pte(struct mm_struct *mm, u32 pfn)
{
	xen_alloc_ptpage(mm, pfn, PT_PTE);
}

static void xen_alloc_pmd(struct mm_struct *mm, u32 pfn)
{
	xen_alloc_ptpage(mm, pfn, PT_PMD);
}

static int xen_pgd_alloc(struct mm_struct *mm)
{
	pgd_t *pgd = mm->pgd;
	int ret = 0;

	BUG_ON(PagePinned(virt_to_page(pgd)));

#ifdef CONFIG_X86_64
	{
		struct page *page = virt_to_page(pgd);
		pgd_t *user_pgd;

		BUG_ON(page->private != 0);

		ret = -ENOMEM;

		user_pgd = (pgd_t *)__get_free_page(GFP_KERNEL | __GFP_ZERO);
		page->private = (unsigned long)user_pgd;

		if (user_pgd != NULL) {
			user_pgd[pgd_index(VSYSCALL_START)] =
				__pgd(__pa(level3_user_vsyscall) | _PAGE_TABLE);
			ret = 0;
		}

		BUG_ON(PagePinned(virt_to_page(xen_get_user_pgd(pgd))));
	}
#endif

	return ret;
}

static void xen_pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
#ifdef CONFIG_X86_64
	pgd_t *user_pgd = xen_get_user_pgd(pgd);

	if (user_pgd)
		free_page((unsigned long)user_pgd);
#endif
}

/* This should never happen until we're OK to use struct page */
static void xen_release_ptpage(u32 pfn, unsigned level)
{
	struct page *page = pfn_to_page(pfn);

	if (PagePinned(page)) {
		if (!PageHighMem(page)) {
			if (level == PT_PTE)
				pin_pagetable_pfn(MMUEXT_UNPIN_TABLE, pfn);
			make_lowmem_page_readwrite(__va(PFN_PHYS(pfn)));
		}
		ClearPagePinned(page);
	}
}

static void xen_release_pte(u32 pfn)
{
	xen_release_ptpage(pfn, PT_PTE);
}

static void xen_release_pmd(u32 pfn)
{
	xen_release_ptpage(pfn, PT_PMD);
}

#if PAGETABLE_LEVELS == 4
static void xen_alloc_pud(struct mm_struct *mm, u32 pfn)
{
	xen_alloc_ptpage(mm, pfn, PT_PUD);
}

static void xen_release_pud(u32 pfn)
{
	xen_release_ptpage(pfn, PT_PUD);
}
#endif

#ifdef CONFIG_HIGHPTE
static void *xen_kmap_atomic_pte(struct page *page, enum km_type type)
{
	pgprot_t prot = PAGE_KERNEL;

	if (PagePinned(page))
		prot = PAGE_KERNEL_RO;

	if (0 && PageHighMem(page))
		printk("mapping highpte %lx type %d prot %s\n",
		       page_to_pfn(page), type,
		       (unsigned long)pgprot_val(prot) & _PAGE_RW ? "WRITE" : "READ");

	return kmap_atomic_prot(page, type, prot);
}
#endif

static __init pte_t mask_rw_pte(pte_t *ptep, pte_t pte)
{
	/* If there's an existing pte, then don't allow _PAGE_RW to be set */
	if (pte_val_ma(*ptep) & _PAGE_PRESENT)
		pte = __pte_ma(((pte_val_ma(*ptep) & _PAGE_RW) | ~_PAGE_RW) &
			       pte_val_ma(pte));

	return pte;
}

/* Init-time set_pte while constructing initial pagetables, which
   doesn't allow RO pagetable pages to be remapped RW */
static __init void xen_set_pte_init(pte_t *ptep, pte_t pte)
{
	pte = mask_rw_pte(ptep, pte);

	xen_set_pte(ptep, pte);
}

static __init void xen_pagetable_setup_start(pgd_t *base)
{
}

void xen_setup_shared_info(void)
{
	if (!xen_feature(XENFEAT_auto_translated_physmap)) {
		set_fixmap(FIX_PARAVIRT_BOOTMAP,
			   xen_start_info->shared_info);

		HYPERVISOR_shared_info =
			(struct shared_info *)fix_to_virt(FIX_PARAVIRT_BOOTMAP);
	} else
		HYPERVISOR_shared_info =
			(struct shared_info *)__va(xen_start_info->shared_info);

#ifndef CONFIG_SMP
	/* In UP this is as good a place as any to set up shared info */
	xen_setup_vcpu_info_placement();
#endif

	xen_setup_mfn_list_list();
}

static __init void xen_pagetable_setup_done(pgd_t *base)
{
	xen_setup_shared_info();
}

static __init void xen_post_allocator_init(void)
{
	pv_mmu_ops.set_pte = xen_set_pte;
	pv_mmu_ops.set_pmd = xen_set_pmd;
	pv_mmu_ops.set_pud = xen_set_pud;
#if PAGETABLE_LEVELS == 4
	pv_mmu_ops.set_pgd = xen_set_pgd;
#endif

	/* This will work as long as patching hasn't happened yet
	   (which it hasn't) */
	pv_mmu_ops.alloc_pte = xen_alloc_pte;
	pv_mmu_ops.alloc_pmd = xen_alloc_pmd;
	pv_mmu_ops.release_pte = xen_release_pte;
	pv_mmu_ops.release_pmd = xen_release_pmd;
#if PAGETABLE_LEVELS == 4
	pv_mmu_ops.alloc_pud = xen_alloc_pud;
	pv_mmu_ops.release_pud = xen_release_pud;
#endif

#ifdef CONFIG_X86_64
	SetPagePinned(virt_to_page(level3_user_vsyscall));
#endif
	xen_mark_init_mm_pinned();
}

/* This is called once we have the cpu_possible_map */
/* This is called once we have the cpu_possible_mask */
void xen_setup_vcpu_info_placement(void)
{
	int cpu;

	for_each_possible_cpu(cpu)
		xen_vcpu_setup(cpu);

	/* xen_vcpu_setup managed to place the vcpu_info within the
	   percpu area for all cpus, so make use of it */
#ifdef CONFIG_X86_32
	if (have_vcpu_info_placement) {
		printk(KERN_INFO "Xen: using vcpu_info placement\n");

		pv_irq_ops.save_fl = xen_save_fl_direct;
		pv_irq_ops.restore_fl = xen_restore_fl_direct;
		pv_irq_ops.irq_disable = xen_irq_disable_direct;
		pv_irq_ops.irq_enable = xen_irq_enable_direct;
		pv_mmu_ops.read_cr2 = xen_read_cr2_direct;
	}
#endif
	 * percpu area for all cpus, so make use of it. Note that for
	 * PVH we want to use native IRQ mechanism. */
	if (have_vcpu_info_placement && !xen_pvh_domain()) {
		pv_irq_ops.save_fl = __PV_IS_CALLEE_SAVE(xen_save_fl_direct);
		pv_irq_ops.restore_fl = __PV_IS_CALLEE_SAVE(xen_restore_fl_direct);
		pv_irq_ops.irq_disable = __PV_IS_CALLEE_SAVE(xen_irq_disable_direct);
		pv_irq_ops.irq_enable = __PV_IS_CALLEE_SAVE(xen_irq_enable_direct);
		pv_mmu_ops.read_cr2 = xen_read_cr2_direct;
	}
}

static unsigned xen_patch(u8 type, u16 clobbers, void *insnbuf,
			  unsigned long addr, unsigned len)
{
	char *start, *end, *reloc;
	unsigned ret;

	start = end = reloc = NULL;

#define SITE(op, x)							\
	case PARAVIRT_PATCH(op.x):					\
	if (have_vcpu_info_placement) {					\
		start = (char *)xen_##x##_direct;			\
		end = xen_##x##_direct_end;				\
		reloc = xen_##x##_direct_reloc;				\
	}								\
	goto patch_site

	switch (type) {
#ifdef CONFIG_X86_32
		SITE(pv_irq_ops, irq_enable);
		SITE(pv_irq_ops, irq_disable);
		SITE(pv_irq_ops, save_fl);
		SITE(pv_irq_ops, restore_fl);
#endif /* CONFIG_X86_32 */
#undef SITE

	patch_site:
		if (start == NULL || (end-start) > len)
			goto default_patch;

		ret = paravirt_patch_insns(insnbuf, len, start, end);

		/* Note: because reloc is assigned from something that
		   appears to be an array, gcc assumes it's non-null,
		   but doesn't know its relationship with start and
		   end. */
		if (reloc > start && reloc < end) {
			int reloc_off = reloc - start;
			long *relocp = (long *)(insnbuf + reloc_off);
			long delta = start - (char *)addr;

			*relocp += delta;
		}
		break;

	default_patch:
	default:
		ret = paravirt_patch_default(type, clobbers, insnbuf,
					     addr, len);
		break;
	}

	return ret;
}

static void xen_set_fixmap(unsigned idx, unsigned long phys, pgprot_t prot)
{
	pte_t pte;

	phys >>= PAGE_SHIFT;

	switch (idx) {
	case FIX_BTMAP_END ... FIX_BTMAP_BEGIN:
#ifdef CONFIG_X86_F00F_BUG
	case FIX_F00F_IDT:
#endif
#ifdef CONFIG_X86_32
	case FIX_WP_TEST:
	case FIX_VDSO:
# ifdef CONFIG_HIGHMEM
	case FIX_KMAP_BEGIN ... FIX_KMAP_END:
# endif
#else
	case VSYSCALL_LAST_PAGE ... VSYSCALL_FIRST_PAGE:
#endif
#ifdef CONFIG_X86_LOCAL_APIC
	case FIX_APIC_BASE:	/* maps dummy local APIC */
#endif
		pte = pfn_pte(phys, prot);
		break;

	default:
		pte = mfn_pte(phys, prot);
		break;
	}

	__native_set_fixmap(idx, pte);

#ifdef CONFIG_X86_64
	/* Replicate changes to map the vsyscall page into the user
	   pagetable vsyscall mapping. */
	if (idx >= VSYSCALL_LAST_PAGE && idx <= VSYSCALL_FIRST_PAGE) {
		unsigned long vaddr = __fix_to_virt(idx);
		set_pte_vaddr_pud(level3_user_vsyscall, vaddr, pte);
	}
#endif
}

static const struct pv_info xen_info __initdata = {
	.paravirt_enabled = 1,
	.shared_kernel_pmd = 0,

	.name = "Xen",
};

static const struct pv_init_ops xen_init_ops __initdata = {
	.patch = xen_patch,

	.banner = xen_banner,
	.memory_setup = xen_memory_setup,
	.arch_setup = xen_arch_setup,
	.post_allocator_init = xen_post_allocator_init,
};

static const struct pv_time_ops xen_time_ops __initdata = {
	.time_init = xen_time_init,

	.set_wallclock = xen_set_wallclock,
	.get_wallclock = xen_get_wallclock,
	.get_tsc_khz = xen_tsc_khz,
	.sched_clock = xen_sched_clock,
};

static const struct pv_cpu_ops xen_cpu_ops __initdata = {
static const struct pv_info xen_info __initconst = {
	.paravirt_enabled = 1,
	.shared_kernel_pmd = 0,

#ifdef CONFIG_X86_64
	.extra_user_64bit_cs = FLAT_USER_CS64,
#endif
	.features = 0,
	.name = "Xen",
};

static const struct pv_init_ops xen_init_ops __initconst = {
	.patch = xen_patch,
};

static const struct pv_cpu_ops xen_cpu_ops __initconst = {
	.cpuid = xen_cpuid,

	.set_debugreg = xen_set_debugreg,
	.get_debugreg = xen_get_debugreg,

	.clts = xen_clts,

	.read_cr0 = native_read_cr0,
	.read_cr0 = xen_read_cr0,
	.write_cr0 = xen_write_cr0,

	.read_cr4 = native_read_cr4,
	.read_cr4_safe = native_read_cr4_safe,
	.write_cr4 = xen_write_cr4,

	.wbinvd = native_wbinvd,

	.read_msr = native_read_msr_safe,
	.write_msr = xen_write_msr_safe,
	.read_tsc = native_read_tsc,
	.read_pmc = native_read_pmc,

	.iret = xen_iret,
	.irq_enable_sysexit = xen_sysexit,
#ifdef CONFIG_X86_64
	.usergs_sysret32 = xen_sysret32,
	.usergs_sysret64 = xen_sysret64,
#ifdef CONFIG_X86_64
	.read_cr8 = xen_read_cr8,
	.write_cr8 = xen_write_cr8,
#endif

	.wbinvd = native_wbinvd,

	.read_msr = xen_read_msr_safe,
	.write_msr = xen_write_msr_safe,

	.read_pmc = xen_read_pmc,

	.iret = xen_iret,
#ifdef CONFIG_X86_64
	.usergs_sysret64 = xen_sysret64,
#endif

	.load_tr_desc = paravirt_nop,
	.set_ldt = xen_set_ldt,
	.load_gdt = xen_load_gdt,
	.load_idt = xen_load_idt,
	.load_tls = xen_load_tls,
#ifdef CONFIG_X86_64
	.load_gs_index = xen_load_gs_index,
#endif

	.store_gdt = native_store_gdt,
	.alloc_ldt = xen_alloc_ldt,
	.free_ldt = xen_free_ldt,

	.store_idt = native_store_idt,
	.store_tr = xen_store_tr,

	.write_ldt_entry = xen_write_ldt_entry,
	.write_gdt_entry = xen_write_gdt_entry,
	.write_idt_entry = xen_write_idt_entry,
	.load_sp0 = xen_load_sp0,

	.set_iopl_mask = xen_set_iopl_mask,
	.io_delay = xen_io_delay,

	/* Xen takes care of %gs when switching to usermode for us */
	.swapgs = paravirt_nop,

	.lazy_mode = {
		.enter = paravirt_enter_lazy_cpu,
		.leave = xen_leave_lazy,
	},
};

static void __init __xen_init_IRQ(void)
{
#ifdef CONFIG_X86_64
	int i;

	/* Create identity vector->irq map */
	for(i = 0; i < NR_VECTORS; i++) {
		int cpu;

		for_each_possible_cpu(cpu)
			per_cpu(vector_irq, cpu)[i] = i;
	}
#endif	/* CONFIG_X86_64 */

	xen_init_IRQ();
}

static const struct pv_irq_ops xen_irq_ops __initdata = {
	.init_IRQ = __xen_init_IRQ,
	.save_fl = xen_save_fl,
	.restore_fl = xen_restore_fl,
	.irq_disable = xen_irq_disable,
	.irq_enable = xen_irq_enable,
	.safe_halt = xen_safe_halt,
	.halt = xen_halt,
#ifdef CONFIG_X86_64
	.adjust_exception_frame = xen_adjust_exception_frame,
#endif
};

static const struct pv_apic_ops xen_apic_ops __initdata = {
#ifdef CONFIG_X86_LOCAL_APIC
	.apic_write = xen_apic_write,
	.apic_read = xen_apic_read,
	.setup_boot_clock = paravirt_nop,
	.setup_secondary_clock = paravirt_nop,
	.start_context_switch = paravirt_start_context_switch,
	.end_context_switch = xen_end_context_switch,
};

static const struct pv_apic_ops xen_apic_ops __initconst = {
#ifdef CONFIG_X86_LOCAL_APIC
	.startup_ipi_hook = paravirt_nop,
#endif
};

static const struct pv_mmu_ops xen_mmu_ops __initdata = {
	.pagetable_setup_start = xen_pagetable_setup_start,
	.pagetable_setup_done = xen_pagetable_setup_done,

	.read_cr2 = xen_read_cr2,
	.write_cr2 = xen_write_cr2,

	.read_cr3 = xen_read_cr3,
	.write_cr3 = xen_write_cr3,

	.flush_tlb_user = xen_flush_tlb,
	.flush_tlb_kernel = xen_flush_tlb,
	.flush_tlb_single = xen_flush_tlb_single,
	.flush_tlb_others = xen_flush_tlb_others,

	.pte_update = paravirt_nop,
	.pte_update_defer = paravirt_nop,

	.pgd_alloc = xen_pgd_alloc,
	.pgd_free = xen_pgd_free,

	.alloc_pte = xen_alloc_pte_init,
	.release_pte = xen_release_pte_init,
	.alloc_pmd = xen_alloc_pte_init,
	.alloc_pmd_clone = paravirt_nop,
	.release_pmd = xen_release_pte_init,

#ifdef CONFIG_HIGHPTE
	.kmap_atomic_pte = xen_kmap_atomic_pte,
#endif

#ifdef CONFIG_X86_64
	.set_pte = xen_set_pte,
#else
	.set_pte = xen_set_pte_init,
#endif
	.set_pte_at = xen_set_pte_at,
	.set_pmd = xen_set_pmd_hyper,

	.ptep_modify_prot_start = __ptep_modify_prot_start,
	.ptep_modify_prot_commit = __ptep_modify_prot_commit,

	.pte_val = xen_pte_val,
	.pte_flags = native_pte_flags,
	.pgd_val = xen_pgd_val,

	.make_pte = xen_make_pte,
	.make_pgd = xen_make_pgd,

#ifdef CONFIG_X86_PAE
	.set_pte_atomic = xen_set_pte_atomic,
	.set_pte_present = xen_set_pte_at,
	.pte_clear = xen_pte_clear,
	.pmd_clear = xen_pmd_clear,
#endif	/* CONFIG_X86_PAE */
	.set_pud = xen_set_pud_hyper,

	.make_pmd = xen_make_pmd,
	.pmd_val = xen_pmd_val,

#if PAGETABLE_LEVELS == 4
	.pud_val = xen_pud_val,
	.make_pud = xen_make_pud,
	.set_pgd = xen_set_pgd_hyper,

	.alloc_pud = xen_alloc_pte_init,
	.release_pud = xen_release_pte_init,
#endif	/* PAGETABLE_LEVELS == 4 */

	.activate_mm = xen_activate_mm,
	.dup_mmap = xen_dup_mmap,
	.exit_mmap = xen_exit_mmap,

	.lazy_mode = {
		.enter = paravirt_enter_lazy_mmu,
		.leave = xen_leave_lazy,
	},

	.set_fixmap = xen_set_fixmap,
};

static void xen_reboot(int reason)
{
	struct sched_shutdown r = { .reason = reason };

#ifdef CONFIG_SMP
	smp_send_stop();
#endif
static void xen_reboot(int reason)
{
	struct sched_shutdown r = { .reason = reason };
	int cpu;

	for_each_online_cpu(cpu)
		xen_pmu_finish(cpu);

	if (HYPERVISOR_sched_op(SCHEDOP_shutdown, &r))
		BUG();
}

static void xen_restart(char *msg)
{
	xen_reboot(SHUTDOWN_reboot);
}

static void xen_emergency_restart(void)
{
	xen_reboot(SHUTDOWN_reboot);
}

static void xen_machine_halt(void)
{
	xen_reboot(SHUTDOWN_poweroff);
}

static void xen_machine_power_off(void)
{
	if (pm_power_off)
		pm_power_off();
	xen_reboot(SHUTDOWN_poweroff);
}

static void xen_crash_shutdown(struct pt_regs *regs)
{
	xen_reboot(SHUTDOWN_crash);
}

static const struct machine_ops __initdata xen_machine_ops = {
	.restart = xen_restart,
	.halt = xen_machine_halt,
	.power_off = xen_machine_halt,
static int
xen_panic_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	xen_reboot(SHUTDOWN_crash);
	return NOTIFY_DONE;
}

static struct notifier_block xen_panic_block = {
	.notifier_call= xen_panic_event,
	.priority = INT_MIN
};

int xen_panic_handler_init(void)
{
	atomic_notifier_chain_register(&panic_notifier_list, &xen_panic_block);
	return 0;
}

static const struct machine_ops xen_machine_ops __initconst = {
	.restart = xen_restart,
	.halt = xen_machine_halt,
	.power_off = xen_machine_power_off,
	.shutdown = xen_machine_halt,
	.crash_shutdown = xen_crash_shutdown,
	.emergency_restart = xen_emergency_restart,
};


static void __init xen_reserve_top(void)
{
#ifdef CONFIG_X86_32
	unsigned long top = HYPERVISOR_VIRT_START;
	struct xen_platform_parameters pp;

	if (HYPERVISOR_xen_version(XENVER_platform_parameters, &pp) == 0)
		top = pp.virt_start;

	reserve_top_address(-top + 2 * PAGE_SIZE);
#endif	/* CONFIG_X86_32 */
}

/*
 * Like __va(), but returns address in the kernel mapping (which is
 * all we have until the physical memory mapping has been set up.
 */
static void *__ka(phys_addr_t paddr)
{
#ifdef CONFIG_X86_64
	return (void *)(paddr + __START_KERNEL_map);
#else
	return __va(paddr);
#endif
}

/* Convert a machine address to physical address */
static unsigned long m2p(phys_addr_t maddr)
{
	phys_addr_t paddr;

	maddr &= PTE_PFN_MASK;
	paddr = mfn_to_pfn(maddr >> PAGE_SHIFT) << PAGE_SHIFT;

	return paddr;
}

/* Convert a machine address to kernel virtual */
static void *m2v(phys_addr_t maddr)
{
	return __ka(m2p(maddr));
}

#ifdef CONFIG_X86_64
static void walk(pgd_t *pgd, unsigned long addr)
{
	unsigned l4idx = pgd_index(addr);
	unsigned l3idx = pud_index(addr);
	unsigned l2idx = pmd_index(addr);
	unsigned l1idx = pte_index(addr);
	pgd_t l4;
	pud_t l3;
	pmd_t l2;
	pte_t l1;

	xen_raw_printk("walk %p, %lx -> %d %d %d %d\n",
		       pgd, addr, l4idx, l3idx, l2idx, l1idx);

	l4 = pgd[l4idx];
	xen_raw_printk("  l4: %016lx\n", l4.pgd);
	xen_raw_printk("      %016lx\n", pgd_val(l4));

	l3 = ((pud_t *)(m2v(l4.pgd)))[l3idx];
	xen_raw_printk("  l3: %016lx\n", l3.pud);
	xen_raw_printk("      %016lx\n", pud_val(l3));

	l2 = ((pmd_t *)(m2v(l3.pud)))[l2idx];
	xen_raw_printk("  l2: %016lx\n", l2.pmd);
	xen_raw_printk("      %016lx\n", pmd_val(l2));

	l1 = ((pte_t *)(m2v(l2.pmd)))[l1idx];
	xen_raw_printk("  l1: %016lx\n", l1.pte);
	xen_raw_printk("      %016lx\n", pte_val(l1));
}
#endif

static void set_page_prot(void *addr, pgprot_t prot)
{
	unsigned long pfn = __pa(addr) >> PAGE_SHIFT;
	pte_t pte = pfn_pte(pfn, prot);

	xen_raw_printk("addr=%p pfn=%lx mfn=%lx prot=%016llx pte=%016llx\n",
		       addr, pfn, get_phys_to_machine(pfn),
		       pgprot_val(prot), pte.pte);

	if (HYPERVISOR_update_va_mapping((unsigned long)addr, pte, 0))
		BUG();
}

static __init void xen_map_identity_early(pmd_t *pmd, unsigned long max_pfn)
{
	unsigned pmdidx, pteidx;
	unsigned ident_pte;
	unsigned long pfn;

	ident_pte = 0;
	pfn = 0;
	for(pmdidx = 0; pmdidx < PTRS_PER_PMD && pfn < max_pfn; pmdidx++) {
		pte_t *pte_page;

		/* Reuse or allocate a page of ptes */
		if (pmd_present(pmd[pmdidx]))
			pte_page = m2v(pmd[pmdidx].pmd);
		else {
			/* Check for free pte pages */
			if (ident_pte == ARRAY_SIZE(level1_ident_pgt))
				break;

			pte_page = &level1_ident_pgt[ident_pte];
			ident_pte += PTRS_PER_PTE;

			pmd[pmdidx] = __pmd(__pa(pte_page) | _PAGE_TABLE);
		}

		/* Install mappings */
		for(pteidx = 0; pteidx < PTRS_PER_PTE; pteidx++, pfn++) {
			pte_t pte;

			if (pfn > max_pfn_mapped)
				max_pfn_mapped = pfn;

			if (!pte_none(pte_page[pteidx]))
				continue;

			pte = pfn_pte(pfn, PAGE_KERNEL_EXEC);
			pte_page[pteidx] = pte;
		}
	}

	for(pteidx = 0; pteidx < ident_pte; pteidx += PTRS_PER_PTE)
		set_page_prot(&level1_ident_pgt[pteidx], PAGE_KERNEL_RO);

	set_page_prot(pmd, PAGE_KERNEL_RO);
}

#ifdef CONFIG_X86_64
static void convert_pfn_mfn(void *v)
{
	pte_t *pte = v;
	int i;

	/* All levels are converted the same way, so just treat them
	   as ptes. */
	for(i = 0; i < PTRS_PER_PTE; i++)
		pte[i] = xen_make_pte(pte[i].pte);
}

/*
 * Set up the inital kernel pagetable.
 *
 * We can construct this by grafting the Xen provided pagetable into
 * head_64.S's preconstructed pagetables.  We copy the Xen L2's into
 * level2_ident_pgt, level2_kernel_pgt and level2_fixmap_pgt.  This
 * means that only the kernel has a physical mapping to start with -
 * but that's enough to get __va working.  We need to fill in the rest
 * of the physical mapping once some sort of allocator has been set
 * up.
 */
static __init pgd_t *xen_setup_kernel_pagetable(pgd_t *pgd, unsigned long max_pfn)
{
	pud_t *l3;
	pmd_t *l2;

	/* Zap identity mapping */
	init_level4_pgt[0] = __pgd(0);

	/* Pre-constructed entries are in pfn, so convert to mfn */
	convert_pfn_mfn(init_level4_pgt);
	convert_pfn_mfn(level3_ident_pgt);
	convert_pfn_mfn(level3_kernel_pgt);

	l3 = m2v(pgd[pgd_index(__START_KERNEL_map)].pgd);
	l2 = m2v(l3[pud_index(__START_KERNEL_map)].pud);

	memcpy(level2_ident_pgt, l2, sizeof(pmd_t) * PTRS_PER_PMD);
	memcpy(level2_kernel_pgt, l2, sizeof(pmd_t) * PTRS_PER_PMD);

	l3 = m2v(pgd[pgd_index(__START_KERNEL_map + PMD_SIZE)].pgd);
	l2 = m2v(l3[pud_index(__START_KERNEL_map + PMD_SIZE)].pud);
	memcpy(level2_fixmap_pgt, l2, sizeof(pmd_t) * PTRS_PER_PMD);

	/* Set up identity map */
	xen_map_identity_early(level2_ident_pgt, max_pfn);

	/* Make pagetable pieces RO */
	set_page_prot(init_level4_pgt, PAGE_KERNEL_RO);
	set_page_prot(level3_ident_pgt, PAGE_KERNEL_RO);
	set_page_prot(level3_kernel_pgt, PAGE_KERNEL_RO);
	set_page_prot(level3_user_vsyscall, PAGE_KERNEL_RO);
	set_page_prot(level2_kernel_pgt, PAGE_KERNEL_RO);
	set_page_prot(level2_fixmap_pgt, PAGE_KERNEL_RO);

	/* Pin down new L4 */
	pin_pagetable_pfn(MMUEXT_PIN_L4_TABLE,
			  PFN_DOWN(__pa_symbol(init_level4_pgt)));

	/* Unpin Xen-provided one */
	pin_pagetable_pfn(MMUEXT_UNPIN_TABLE, PFN_DOWN(__pa(pgd)));

	/* Switch over */
	pgd = init_level4_pgt;

	/*
	 * At this stage there can be no user pgd, and no page
	 * structure to attach it to, so make sure we just set kernel
	 * pgd.
	 */
	xen_mc_batch();
	__xen_write_cr3(true, __pa(pgd));
	xen_mc_issue(PARAVIRT_LAZY_CPU);

	reserve_early(__pa(xen_start_info->pt_base),
		      __pa(xen_start_info->pt_base +
			   xen_start_info->nr_pt_frames * PAGE_SIZE),
		      "XEN PAGETABLES");

	return pgd;
}
#else	/* !CONFIG_X86_64 */
static pmd_t level2_kernel_pgt[PTRS_PER_PMD] __page_aligned_bss;

static __init pgd_t *xen_setup_kernel_pagetable(pgd_t *pgd, unsigned long max_pfn)
{
	pmd_t *kernel_pmd;

	init_pg_tables_start = __pa(pgd);
	init_pg_tables_end = __pa(pgd) + xen_start_info->nr_pt_frames*PAGE_SIZE;
	max_pfn_mapped = PFN_DOWN(init_pg_tables_end + 512*1024);

	kernel_pmd = m2v(pgd[KERNEL_PGD_BOUNDARY].pgd);
	memcpy(level2_kernel_pgt, kernel_pmd, sizeof(pmd_t) * PTRS_PER_PMD);

	xen_map_identity_early(level2_kernel_pgt, max_pfn);

	memcpy(swapper_pg_dir, pgd, sizeof(pgd_t) * PTRS_PER_PGD);
	set_pgd(&swapper_pg_dir[KERNEL_PGD_BOUNDARY],
			__pgd(__pa(level2_kernel_pgt) | _PAGE_PRESENT));

	set_page_prot(level2_kernel_pgt, PAGE_KERNEL_RO);
	set_page_prot(swapper_pg_dir, PAGE_KERNEL_RO);
	set_page_prot(empty_zero_page, PAGE_KERNEL_RO);

	pin_pagetable_pfn(MMUEXT_UNPIN_TABLE, PFN_DOWN(__pa(pgd)));

	xen_write_cr3(__pa(swapper_pg_dir));

	pin_pagetable_pfn(MMUEXT_PIN_L3_TABLE, PFN_DOWN(__pa(swapper_pg_dir)));

	return swapper_pg_dir;
}
#endif	/* CONFIG_X86_64 */

/* First C function to be called on Xen boot */
asmlinkage void __init xen_start_kernel(void)
{
	pgd_t *pgd;
static unsigned char xen_get_nmi_reason(void)
{
	unsigned char reason = 0;

	/* Construct a value which looks like it came from port 0x61. */
	if (test_bit(_XEN_NMIREASON_io_error,
		     &HYPERVISOR_shared_info->arch.nmi_reason))
		reason |= NMI_REASON_IOCHK;
	if (test_bit(_XEN_NMIREASON_pci_serr,
		     &HYPERVISOR_shared_info->arch.nmi_reason))
		reason |= NMI_REASON_SERR;

	return reason;
}

static void __init xen_boot_params_init_edd(void)
{
#if IS_ENABLED(CONFIG_EDD)
	struct xen_platform_op op;
	struct edd_info *edd_info;
	u32 *mbr_signature;
	unsigned nr;
	int ret;

	edd_info = boot_params.eddbuf;
	mbr_signature = boot_params.edd_mbr_sig_buffer;

	op.cmd = XENPF_firmware_info;

	op.u.firmware_info.type = XEN_FW_DISK_INFO;
	for (nr = 0; nr < EDDMAXNR; nr++) {
		struct edd_info *info = edd_info + nr;

		op.u.firmware_info.index = nr;
		info->params.length = sizeof(info->params);
		set_xen_guest_handle(op.u.firmware_info.u.disk_info.edd_params,
				     &info->params);
		ret = HYPERVISOR_dom0_op(&op);
		if (ret)
			break;

#define C(x) info->x = op.u.firmware_info.u.disk_info.x
		C(device);
		C(version);
		C(interface_support);
		C(legacy_max_cylinder);
		C(legacy_max_head);
		C(legacy_sectors_per_track);
#undef C
	}
	boot_params.eddbuf_entries = nr;

	op.u.firmware_info.type = XEN_FW_DISK_MBR_SIGNATURE;
	for (nr = 0; nr < EDD_MBR_SIG_MAX; nr++) {
		op.u.firmware_info.index = nr;
		ret = HYPERVISOR_dom0_op(&op);
		if (ret)
			break;
		mbr_signature[nr] = op.u.firmware_info.u.disk_mbr_signature.mbr_signature;
	}
	boot_params.edd_mbr_sig_buf_entries = nr;
#endif
}

/*
 * Set up the GDT and segment registers for -fstack-protector.  Until
 * we do this, we have to be careful not to call any stack-protected
 * function, which is most of the kernel.
 *
 * Note, that it is __ref because the only caller of this after init
 * is PVH which is not going to use xen_load_gdt_boot or other
 * __init functions.
 */
static void __ref xen_setup_gdt(int cpu)
{
	if (xen_feature(XENFEAT_auto_translated_physmap)) {
#ifdef CONFIG_X86_64
		unsigned long dummy;

		load_percpu_segment(cpu); /* We need to access per-cpu area */
		switch_to_new_gdt(cpu); /* GDT and GS set */

		/* We are switching of the Xen provided GDT to our HVM mode
		 * GDT. The new GDT has  __KERNEL_CS with CS.L = 1
		 * and we are jumping to reload it.
		 */
		asm volatile ("pushq %0\n"
			      "leaq 1f(%%rip),%0\n"
			      "pushq %0\n"
			      "lretq\n"
			      "1:\n"
			      : "=&r" (dummy) : "0" (__KERNEL_CS));

		/*
		 * While not needed, we also set the %es, %ds, and %fs
		 * to zero. We don't care about %ss as it is NULL.
		 * Strictly speaking this is not needed as Xen zeros those
		 * out (and also MSR_FS_BASE, MSR_GS_BASE, MSR_KERNEL_GS_BASE)
		 *
		 * Linux zeros them in cpu_init() and in secondary_startup_64
		 * (for BSP).
		 */
		loadsegment(es, 0);
		loadsegment(ds, 0);
		loadsegment(fs, 0);
#else
		/* PVH: TODO Implement. */
		BUG();
#endif
		return; /* PVH does not need any PV GDT ops. */
	}
	pv_cpu_ops.write_gdt_entry = xen_write_gdt_entry_boot;
	pv_cpu_ops.load_gdt = xen_load_gdt_boot;

	setup_stack_canary_segment(0);
	switch_to_new_gdt(0);

	pv_cpu_ops.write_gdt_entry = xen_write_gdt_entry;
	pv_cpu_ops.load_gdt = xen_load_gdt;
}

#ifdef CONFIG_XEN_PVH
/*
 * A PV guest starts with default flags that are not set for PVH, set them
 * here asap.
 */
static void xen_pvh_set_cr_flags(int cpu)
{

	/* Some of these are setup in 'secondary_startup_64'. The others:
	 * X86_CR0_TS, X86_CR0_PE, X86_CR0_ET are set by Xen for HVM guests
	 * (which PVH shared codepaths), while X86_CR0_PG is for PVH. */
	write_cr0(read_cr0() | X86_CR0_MP | X86_CR0_NE | X86_CR0_WP | X86_CR0_AM);

	if (!cpu)
		return;
	/*
	 * For BSP, PSE PGE are set in probe_page_size_mask(), for APs
	 * set them here. For all, OSFXSR OSXMMEXCPT are set in fpu__init_cpu().
	*/
	if (cpu_has_pse)
		cr4_set_bits_and_update_boot(X86_CR4_PSE);

	if (cpu_has_pge)
		cr4_set_bits_and_update_boot(X86_CR4_PGE);
}

/*
 * Note, that it is ref - because the only caller of this after init
 * is PVH which is not going to use xen_load_gdt_boot or other
 * __init functions.
 */
void __ref xen_pvh_secondary_vcpu_init(int cpu)
{
	xen_setup_gdt(cpu);
	xen_pvh_set_cr_flags(cpu);
}

static void __init xen_pvh_early_guest_init(void)
{
	if (!xen_feature(XENFEAT_auto_translated_physmap))
		return;

	if (!xen_feature(XENFEAT_hvm_callback_vector))
		return;

	xen_have_vector_callback = 1;

	xen_pvh_early_cpu_init(0, false);
	xen_pvh_set_cr_flags(0);

#ifdef CONFIG_X86_32
	BUG(); /* PVH: Implement proper support. */
#endif
}
#endif    /* CONFIG_XEN_PVH */

/* First C function to be called on Xen boot */
asmlinkage __visible void __init xen_start_kernel(void)
{
	struct physdev_set_iopl set_iopl;
	unsigned long initrd_start = 0;
	int rc;

	if (!xen_start_info)
		return;

	BUG_ON(memcmp(xen_start_info->magic, "xen-3", 5) != 0);

	xen_setup_features();

	/* Install Xen paravirt ops */
	pv_info = xen_info;
	pv_init_ops = xen_init_ops;
	pv_time_ops = xen_time_ops;
	pv_cpu_ops = xen_cpu_ops;
	pv_irq_ops = xen_irq_ops;
	pv_apic_ops = xen_apic_ops;
	pv_mmu_ops = xen_mmu_ops;
	xen_domain_type = XEN_PV_DOMAIN;

	xen_setup_features();
#ifdef CONFIG_XEN_PVH
	xen_pvh_early_guest_init();
#endif
	xen_setup_machphys_mapping();

	/* Install Xen paravirt ops */
	pv_info = xen_info;
	if (xen_initial_domain())
		pv_info.features |= PV_SUPPORTED_RTC;
	pv_init_ops = xen_init_ops;
	pv_apic_ops = xen_apic_ops;
	if (!xen_pvh_domain()) {
		pv_cpu_ops = xen_cpu_ops;

		x86_platform.get_nmi_reason = xen_get_nmi_reason;
	}

	if (xen_feature(XENFEAT_auto_translated_physmap))
		x86_init.resources.memory_setup = xen_auto_xlated_memory_setup;
	else
		x86_init.resources.memory_setup = xen_memory_setup;
	x86_init.oem.arch_setup = xen_arch_setup;
	x86_init.oem.banner = xen_banner;

	xen_init_time_ops();

	/*
	 * Set up some pagetable state before starting to set any ptes.
	 */

	xen_init_mmu_ops();

	/* Prevent unwanted bits from being set in PTEs. */
	__supported_pte_mask &= ~_PAGE_GLOBAL;

	/*
	 * Prevent page tables from being allocated in highmem, even
	 * if CONFIG_HIGHPTE is enabled.
	 */
	__userpte_alloc_gfp &= ~__GFP_HIGHMEM;

	/* Work out if we support NX */
	x86_configure_nx();

	/* Get mfn list */
	xen_build_dynamic_phys_to_machine();

	/*
	 * Set up kernel GDT and segment registers, mainly so that
	 * -fstack-protector code can be executed.
	 */
	xen_setup_gdt(0);

	xen_init_irq_ops();
	xen_init_cpuid_mask();
	xen_init_capabilities();

#ifdef CONFIG_X86_LOCAL_APIC
	/*
	 * set up the basic apic ops.
	 */
	xen_init_apic();
#endif

	if (xen_feature(XENFEAT_mmu_pt_update_preserve_ad)) {
		pv_mmu_ops.ptep_modify_prot_start = xen_ptep_modify_prot_start;
		pv_mmu_ops.ptep_modify_prot_commit = xen_ptep_modify_prot_commit;
	}

	machine_ops = xen_machine_ops;

#ifdef CONFIG_X86_64
	/* Disable until direct per-cpu data access. */
	have_vcpu_info_placement = 0;
	x86_64_init_pda();
#endif

	xen_smp_init();

	/* Get mfn list */
	if (!xen_feature(XENFEAT_auto_translated_physmap))
		xen_build_dynamic_phys_to_machine();

	pgd = (pgd_t *)xen_start_info->pt_base;

	/* Prevent unwanted bits from being set in PTEs. */
	__supported_pte_mask &= ~_PAGE_GLOBAL;
	if (!is_initial_xendomain())
		__supported_pte_mask &= ~(_PAGE_PWT | _PAGE_PCD);

	/*
	 * The only reliable way to retain the initial address of the
	 * percpu gdt_page is to remember it here, so we can go and
	 * mark it RW later, when the initial percpu area is freed.
	 */
	xen_initial_gdt = &per_cpu(gdt_page, 0);

	xen_smp_init();

#ifdef CONFIG_ACPI_NUMA
	/*
	 * The pages we from Xen are not related to machine pages, so
	 * any NUMA information the kernel tries to get from ACPI will
	 * be meaningless.  Prevent it from trying.
	 */
	acpi_numa = -1;
#endif
	/* Don't do the full vcpu_info placement stuff until we have a
	   possible map and a non-dummy shared_info. */
	per_cpu(xen_vcpu, 0) = &HYPERVISOR_shared_info->vcpu_info[0];

	xen_raw_console_write("mapping kernel into physical memory\n");
	pgd = xen_setup_kernel_pagetable(pgd, xen_start_info->nr_pages);

	init_mm.pgd = pgd;

	/* keep using Xen gdt for now; no urgent need to change it */

	pv_info.kernel_rpl = 1;
	if (xen_feature(XENFEAT_supervisor_mode_kernel))
		pv_info.kernel_rpl = 0;

	/* set the limit of our address space */
	xen_reserve_top();

#ifdef CONFIG_X86_32
	/* set up basic CPUID stuff */
	cpu_detect(&new_cpu_data);
	new_cpu_data.hard_math = 1;
	new_cpu_data.x86_capability[0] = cpuid_edx(1);
#endif

	/* Poke various useful things into boot_params */
	boot_params.hdr.type_of_loader = (9 << 4) | 0;
	boot_params.hdr.ramdisk_image = xen_start_info->mod_start
		? __pa(xen_start_info->mod_start) : 0;
	boot_params.hdr.ramdisk_size = xen_start_info->mod_len;
	boot_params.hdr.cmd_line_ptr = __pa(xen_start_info->cmd_line);

	if (!is_initial_xendomain()) {
		add_preferred_console("xenboot", 0, NULL);
		add_preferred_console("tty", 0, NULL);
		add_preferred_console("hvc", 0, NULL);
	}

	xen_raw_console_write("about to get started...\n");

#if 0
	xen_raw_printk("&boot_params=%p __pa(&boot_params)=%lx __va(__pa(&boot_params))=%lx\n",
		       &boot_params, __pa_symbol(&boot_params),
		       __va(__pa_symbol(&boot_params)));

	walk(pgd, &boot_params);
	walk(pgd, __va(__pa(&boot_params)));
#endif
	local_irq_disable();
	early_boot_irqs_disabled = true;

	xen_raw_console_write("mapping kernel into physical memory\n");
	xen_setup_kernel_pagetable((pgd_t *)xen_start_info->pt_base,
				   xen_start_info->nr_pages);
	xen_reserve_special_pages();

	/* keep using Xen gdt for now; no urgent need to change it */

#ifdef CONFIG_X86_32
	pv_info.kernel_rpl = 1;
	if (xen_feature(XENFEAT_supervisor_mode_kernel))
		pv_info.kernel_rpl = 0;
#else
	pv_info.kernel_rpl = 0;
#endif
	/* set the limit of our address space */
	xen_reserve_top();

	/* PVH: runs at default kernel iopl of 0 */
	if (!xen_pvh_domain()) {
		/*
		 * We used to do this in xen_arch_setup, but that is too late
		 * on AMD were early_cpu_init (run before ->arch_setup()) calls
		 * early_amd_init which pokes 0xcf8 port.
		 */
		set_iopl.iopl = 1;
		rc = HYPERVISOR_physdev_op(PHYSDEVOP_set_iopl, &set_iopl);
		if (rc != 0)
			xen_raw_printk("physdev_op failed %d\n", rc);
	}

#ifdef CONFIG_X86_32
	/* set up basic CPUID stuff */
	cpu_detect(&new_cpu_data);
	set_cpu_cap(&new_cpu_data, X86_FEATURE_FPU);
	new_cpu_data.wp_works_ok = 1;
	new_cpu_data.x86_capability[0] = cpuid_edx(1);
#endif

	if (xen_start_info->mod_start) {
	    if (xen_start_info->flags & SIF_MOD_START_PFN)
		initrd_start = PFN_PHYS(xen_start_info->mod_start);
	    else
		initrd_start = __pa(xen_start_info->mod_start);
	}

	/* Poke various useful things into boot_params */
	boot_params.hdr.type_of_loader = (9 << 4) | 0;
	boot_params.hdr.ramdisk_image = initrd_start;
	boot_params.hdr.ramdisk_size = xen_start_info->mod_len;
	boot_params.hdr.cmd_line_ptr = __pa(xen_start_info->cmd_line);

	if (!xen_initial_domain()) {
		add_preferred_console("xenboot", 0, NULL);
		add_preferred_console("tty", 0, NULL);
		add_preferred_console("hvc", 0, NULL);
		if (pci_xen)
			x86_init.pci.arch_init = pci_xen_init;
	} else {
		const struct dom0_vga_console_info *info =
			(void *)((char *)xen_start_info +
				 xen_start_info->console.dom0.info_off);
		struct xen_platform_op op = {
			.cmd = XENPF_firmware_info,
			.interface_version = XENPF_INTERFACE_VERSION,
			.u.firmware_info.type = XEN_FW_KBD_SHIFT_FLAGS,
		};

		xen_init_vga(info, xen_start_info->console.dom0.info_size);
		xen_start_info->console.domU.mfn = 0;
		xen_start_info->console.domU.evtchn = 0;

		if (HYPERVISOR_dom0_op(&op) == 0)
			boot_params.kbd_status = op.u.firmware_info.u.kbd_shift_flags;

		/* Make sure ACS will be enabled */
		pci_request_acs();

		xen_acpi_sleep_register();

		/* Avoid searching for BIOS MP tables */
		x86_init.mpparse.find_smp_config = x86_init_noop;
		x86_init.mpparse.get_smp_config = x86_init_uint_noop;

		xen_boot_params_init_edd();
	}
#ifdef CONFIG_PCI
	/* PCI BIOS service won't work from a PV guest. */
	pci_probe &= ~PCI_PROBE_BIOS;
#endif
	xen_raw_console_write("about to get started...\n");

	xen_setup_runstate_info(0);

	xen_efi_init();

	/* Start the world */
#ifdef CONFIG_X86_32
	i386_start_kernel();
#else
	x86_64_start_reservations((char *)__pa_symbol(&boot_params));
#endif
}
	cr4_init_shadow(); /* 32b kernel does this in i386_start_kernel() */
	x86_64_start_reservations((char *)__pa_symbol(&boot_params));
#endif
}

void __ref xen_hvm_init_shared_info(void)
{
	int cpu;
	struct xen_add_to_physmap xatp;
	static struct shared_info *shared_info_page = 0;

	if (!shared_info_page)
		shared_info_page = (struct shared_info *)
			extend_brk(PAGE_SIZE, PAGE_SIZE);
	xatp.domid = DOMID_SELF;
	xatp.idx = 0;
	xatp.space = XENMAPSPACE_shared_info;
	xatp.gpfn = __pa(shared_info_page) >> PAGE_SHIFT;
	if (HYPERVISOR_memory_op(XENMEM_add_to_physmap, &xatp))
		BUG();

	HYPERVISOR_shared_info = (struct shared_info *)shared_info_page;

	/* xen_vcpu is a pointer to the vcpu_info struct in the shared_info
	 * page, we use it in the event channel upcall and in some pvclock
	 * related functions. We don't need the vcpu_info placement
	 * optimizations because we don't use any pv_mmu or pv_irq op on
	 * HVM.
	 * When xen_hvm_init_shared_info is run at boot time only vcpu 0 is
	 * online but xen_hvm_init_shared_info is run at resume time too and
	 * in that case multiple vcpus might be online. */
	for_each_online_cpu(cpu) {
		/* Leave it to be NULL. */
		if (cpu >= MAX_VIRT_CPUS)
			continue;
		per_cpu(xen_vcpu, cpu) = &HYPERVISOR_shared_info->vcpu_info[cpu];
	}
}

#ifdef CONFIG_XEN_PVHVM
static void __init init_hvm_pv_info(void)
{
	int major, minor;
	uint32_t eax, ebx, ecx, edx, pages, msr, base;
	u64 pfn;

	base = xen_cpuid_base();
	cpuid(base + 1, &eax, &ebx, &ecx, &edx);

	major = eax >> 16;
	minor = eax & 0xffff;
	printk(KERN_INFO "Xen version %d.%d.\n", major, minor);

	cpuid(base + 2, &pages, &msr, &ecx, &edx);

	pfn = __pa(hypercall_page);
	wrmsr_safe(msr, (u32)pfn, (u32)(pfn >> 32));

	xen_setup_features();

	pv_info.name = "Xen HVM";

	xen_domain_type = XEN_HVM_DOMAIN;
}

static int xen_hvm_cpu_notify(struct notifier_block *self, unsigned long action,
			      void *hcpu)
{
	int cpu = (long)hcpu;
	switch (action) {
	case CPU_UP_PREPARE:
		xen_vcpu_setup(cpu);
		if (xen_have_vector_callback) {
			if (xen_feature(XENFEAT_hvm_safe_pvclock))
				xen_setup_timer(cpu);
		}
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block xen_hvm_cpu_notifier = {
	.notifier_call	= xen_hvm_cpu_notify,
};

#ifdef CONFIG_KEXEC_CORE
static void xen_hvm_shutdown(void)
{
	native_machine_shutdown();
	if (kexec_in_progress)
		xen_reboot(SHUTDOWN_soft_reset);
}

static void xen_hvm_crash_shutdown(struct pt_regs *regs)
{
	native_machine_crash_shutdown(regs);
	xen_reboot(SHUTDOWN_soft_reset);
}
#endif

static void __init xen_hvm_guest_init(void)
{
	if (xen_pv_domain())
		return;

	init_hvm_pv_info();

	xen_hvm_init_shared_info();

	xen_panic_handler_init();

	if (xen_feature(XENFEAT_hvm_callback_vector))
		xen_have_vector_callback = 1;
	xen_hvm_smp_init();
	register_cpu_notifier(&xen_hvm_cpu_notifier);
	xen_unplug_emulated_devices();
	x86_init.irqs.intr_init = xen_init_IRQ;
	xen_hvm_init_time_ops();
	xen_hvm_init_mmu_ops();
#ifdef CONFIG_KEXEC_CORE
	machine_ops.shutdown = xen_hvm_shutdown;
	machine_ops.crash_shutdown = xen_hvm_crash_shutdown;
#endif
}
#endif

static bool xen_nopv = false;
static __init int xen_parse_nopv(char *arg)
{
       xen_nopv = true;
       return 0;
}
early_param("xen_nopv", xen_parse_nopv);

static uint32_t __init xen_platform(void)
{
	if (xen_nopv)
		return 0;

	return xen_cpuid_base();
}

bool xen_hvm_need_lapic(void)
{
	if (xen_nopv)
		return false;
	if (xen_pv_domain())
		return false;
	if (!xen_hvm_domain())
		return false;
	if (xen_feature(XENFEAT_hvm_pirqs) && xen_have_vector_callback)
		return false;
	return true;
}
EXPORT_SYMBOL_GPL(xen_hvm_need_lapic);

const struct hypervisor_x86 x86_hyper_xen = {
	.name			= "Xen",
	.detect			= xen_platform,
#ifdef CONFIG_XEN_PVHVM
	.init_platform		= xen_hvm_guest_init,
#endif
	.x2apic_available	= xen_x2apic_para_available,
};
EXPORT_SYMBOL(x86_hyper_xen);

#ifdef CONFIG_HOTPLUG_CPU
void xen_arch_register_cpu(int num)
{
	arch_register_cpu(num);
}
EXPORT_SYMBOL(xen_arch_register_cpu);

void xen_arch_unregister_cpu(int num)
{
	arch_unregister_cpu(num);
}
EXPORT_SYMBOL(xen_arch_unregister_cpu);
#endif
