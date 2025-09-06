/*
 *  arch/s390/mm/fault.c
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *  S390 version
 *    Copyright IBM Corp. 1999
 *    Author(s): Hartmut Penner (hp@de.ibm.com)
 *               Ulrich Weigand (uweigand@de.ibm.com)
 *
 *  Derived from "arch/i386/mm/fault.c"
 *    Copyright (C) 1995  Linus Torvalds
 */

#include <linux/kernel_stat.h>
#include <linux/perf_event.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/kdebug.h>
#include <linux/smp_lock.h>
#include <linux/compat.h>
#include <linux/smp.h>
#include <linux/kdebug.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/module.h>
#include <linux/hardirq.h>
#include <linux/kprobes.h>
#include <linux/uaccess.h>
#include <linux/hugetlb.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/s390_ext.h>
#include <asm/mmu_context.h>
#include "../kernel/entry.h"

#ifndef CONFIG_64BIT
#define __FAIL_ADDR_MASK 0x7ffff000
#define __FIXUP_MASK 0x7fffffff
#define __SUBCODE_MASK 0x0200
#define __PF_RES_FIELD 0ULL
#else /* CONFIG_64BIT */
#define __FAIL_ADDR_MASK -4096L
#define __FIXUP_MASK ~0L
#define __SUBCODE_MASK 0x0600
#define __PF_RES_FIELD 0x8000000000000000ULL
#endif /* CONFIG_64BIT */

#ifdef CONFIG_SYSCTL
extern int sysctl_userprocess_debug;
#endif

#ifdef CONFIG_KPROBES
static inline int notify_page_fault(struct pt_regs *regs, long err)
#include <asm/asm-offsets.h>
#include <asm/diag.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/mmu_context.h>
#include <asm/facility.h>
#include "../kernel/entry.h"

#define __FAIL_ADDR_MASK -4096L
#define __SUBCODE_MASK 0x0600
#define __PF_RES_FIELD 0x8000000000000000ULL

#define VM_FAULT_BADCONTEXT	0x010000
#define VM_FAULT_BADMAP		0x020000
#define VM_FAULT_BADACCESS	0x040000
#define VM_FAULT_SIGNAL		0x080000
#define VM_FAULT_PFAULT		0x100000

static unsigned long store_indication __read_mostly;

static int __init fault_init(void)
{
	if (test_facility(75))
		store_indication = 0xc00;
	return 0;
}
early_initcall(fault_init);

static inline int notify_page_fault(struct pt_regs *regs)
{
	int ret = 0;

	/* kprobe_running() needs smp_processor_id() */
	if (!user_mode(regs)) {
	if (kprobes_built_in() && !user_mode(regs)) {
		preempt_disable();
		if (kprobe_running() && kprobe_fault_handler(regs, 14))
			ret = 1;
		preempt_enable();
	}

	return ret;
}
#else
static inline int notify_page_fault(struct pt_regs *regs, long err)
{
	return 0;
}
#endif
	return ret;
}


/*
 * Unlock any spinlocks which will prevent us from getting the
 * message out.
 */
void bust_spinlocks(int yes)
{
	if (yes) {
		oops_in_progress = 1;
	} else {
		int loglevel_save = console_loglevel;
		console_unblank();
		oops_in_progress = 0;
		/*
		 * OK, the message is on the console.  Now we call printk()
		 * without oops_in_progress set so that printk will give klogd
		 * a poke.  Hold onto your hats...
		 */
		console_loglevel = 15;
		printk(" ");
		console_loglevel = loglevel_save;
	}
}

/*
 * Returns the address space associated with the fault.
 * Returns 0 for kernel space, 1 for user space and
 * 2 for code execution in user space with noexec=on.
 */
static inline int check_space(struct task_struct *tsk)
{
	/*
	 * The lowest two bits of S390_lowcore.trans_exc_code
	 * indicate which paging table was used.
	 */
	int desc = S390_lowcore.trans_exc_code & 3;

	if (desc == 3)	/* Home Segment Table Descriptor */
		return switch_amode == 0;
	if (desc == 2)	/* Secondary Segment Table Descriptor */
		return tsk->thread.mm_segment.ar4;
#ifdef CONFIG_S390_SWITCH_AMODE
	if (unlikely(desc == 1)) { /* STD determined via access register */
		/* %a0 always indicates primary space. */
		if (S390_lowcore.exc_access_id != 0) {
			save_access_regs(tsk->thread.acrs);
			/*
			 * An alet of 0 indicates primary space.
			 * An alet of 1 indicates secondary space.
			 * Any other alet values generate an
			 * alen-translation exception.
			 */
			if (tsk->thread.acrs[S390_lowcore.exc_access_id])
				return tsk->thread.mm_segment.ar4;
		}
	}
#endif
	/* Primary Segment Table Descriptor */
	return switch_amode << s390_noexec;
 * Returns 0 for kernel space and 1 for user space.
 */
static inline int user_space_fault(struct pt_regs *regs)
{
	unsigned long trans_exc_code;

	/*
	 * The lowest two bits of the translation exception
	 * identification indicate which paging table was used.
	 */
	trans_exc_code = regs->int_parm_long & 3;
	if (trans_exc_code == 3) /* home space -> kernel */
		return 0;
	if (user_mode(regs))
		return 1;
	if (trans_exc_code == 2) /* secondary space -> set_fs */
		return current->thread.mm_segment.ar4;
	if (current->flags & PF_VCPU)
		return 1;
	return 0;
}

static int bad_address(void *p)
{
	unsigned long dummy;

	return probe_kernel_address((unsigned long *)p, dummy);
}

static void dump_pagetable(unsigned long asce, unsigned long address)
{
	unsigned long *table = __va(asce & PAGE_MASK);

	pr_alert("AS:%016lx ", asce);
	switch (asce & _ASCE_TYPE_MASK) {
	case _ASCE_TYPE_REGION1:
		table = table + ((address >> 53) & 0x7ff);
		if (bad_address(table))
			goto bad;
		pr_cont("R1:%016lx ", *table);
		if (*table & _REGION_ENTRY_INVALID)
			goto out;
		table = (unsigned long *)(*table & _REGION_ENTRY_ORIGIN);
		/* fallthrough */
	case _ASCE_TYPE_REGION2:
		table = table + ((address >> 42) & 0x7ff);
		if (bad_address(table))
			goto bad;
		pr_cont("R2:%016lx ", *table);
		if (*table & _REGION_ENTRY_INVALID)
			goto out;
		table = (unsigned long *)(*table & _REGION_ENTRY_ORIGIN);
		/* fallthrough */
	case _ASCE_TYPE_REGION3:
		table = table + ((address >> 31) & 0x7ff);
		if (bad_address(table))
			goto bad;
		pr_cont("R3:%016lx ", *table);
		if (*table & (_REGION_ENTRY_INVALID | _REGION3_ENTRY_LARGE))
			goto out;
		table = (unsigned long *)(*table & _REGION_ENTRY_ORIGIN);
		/* fallthrough */
	case _ASCE_TYPE_SEGMENT:
		table = table + ((address >> 20) & 0x7ff);
		if (bad_address(table))
			goto bad;
		pr_cont("S:%016lx ", *table);
		if (*table & (_SEGMENT_ENTRY_INVALID | _SEGMENT_ENTRY_LARGE))
			goto out;
		table = (unsigned long *)(*table & _SEGMENT_ENTRY_ORIGIN);
	}
	table = table + ((address >> 12) & 0xff);
	if (bad_address(table))
		goto bad;
	pr_cont("P:%016lx ", *table);
out:
	pr_cont("\n");
	return;
bad:
	pr_cont("BAD\n");
}

static void dump_fault_info(struct pt_regs *regs)
{
	unsigned long asce;

	pr_alert("Fault in ");
	switch (regs->int_parm_long & 3) {
	case 3:
		pr_cont("home space ");
		break;
	case 2:
		pr_cont("secondary space ");
		break;
	case 1:
		pr_cont("access register ");
		break;
	case 0:
		pr_cont("primary space ");
		break;
	}
	pr_cont("mode while using ");
	if (!user_space_fault(regs)) {
		asce = S390_lowcore.kernel_asce;
		pr_cont("kernel ");
	}
#ifdef CONFIG_PGSTE
	else if ((current->flags & PF_VCPU) && S390_lowcore.gmap) {
		struct gmap *gmap = (struct gmap *)S390_lowcore.gmap;
		asce = gmap->asce;
		pr_cont("gmap ");
	}
#endif
	else {
		asce = S390_lowcore.user_asce;
		pr_cont("user ");
	}
	pr_cont("ASCE.\n");
	dump_pagetable(asce, regs->int_parm_long & __FAIL_ADDR_MASK);
}

static inline void report_user_fault(struct pt_regs *regs, long signr)
{
	if ((task_pid_nr(current) > 1) && !show_unhandled_signals)
		return;
	if (!unhandled_signal(current, signr))
		return;
	if (!printk_ratelimit())
		return;
	printk(KERN_ALERT "User process fault: interruption code %04x ilc:%d ",
	       regs->int_code & 0xffff, regs->int_code >> 17);
	print_vma_addr(KERN_CONT "in ", regs->psw.addr & PSW_ADDR_INSN);
	printk(KERN_CONT "\n");
	printk(KERN_ALERT "failing address: %016lx TEID: %016lx\n",
	       regs->int_parm_long & __FAIL_ADDR_MASK, regs->int_parm_long);
	dump_fault_info(regs);
	show_regs(regs);
}

/*
 * Send SIGSEGV to task.  This is an external routine
 * to keep the stack usage of do_page_fault small.
 */
static void do_sigsegv(struct pt_regs *regs, unsigned long error_code,
		       int si_code, unsigned long address)
{
	struct siginfo si;

#if defined(CONFIG_SYSCTL) || defined(CONFIG_PROCESS_DEBUG)
#if defined(CONFIG_SYSCTL)
	if (sysctl_userprocess_debug)
#endif
	{
		printk("User process fault: interruption code 0x%lX\n",
		       error_code);
		printk("failing address: %lX\n", address);
		show_regs(regs);
	}
#endif
	si.si_signo = SIGSEGV;
	si.si_code = si_code;
	si.si_addr = (void __user *) address;
	force_sig_info(SIGSEGV, &si, current);
}

static void do_no_context(struct pt_regs *regs, unsigned long error_code,
			  unsigned long address)
{
	const struct exception_table_entry *fixup;

	/* Are we prepared to handle this kernel fault?  */
	fixup = search_exception_tables(regs->psw.addr & __FIXUP_MASK);
	if (fixup) {
		regs->psw.addr = fixup->fixup | PSW_ADDR_AMODE;
static noinline void do_sigsegv(struct pt_regs *regs, int si_code)
{
	struct siginfo si;

	report_user_fault(regs, SIGSEGV);
	si.si_signo = SIGSEGV;
	si.si_code = si_code;
	si.si_addr = (void __user *)(regs->int_parm_long & __FAIL_ADDR_MASK);
	force_sig_info(SIGSEGV, &si, current);
}

static noinline void do_no_context(struct pt_regs *regs)
{
	const struct exception_table_entry *fixup;
	unsigned long address;

	/* Are we prepared to handle this kernel fault?  */
	fixup = search_exception_tables(regs->psw.addr & PSW_ADDR_INSN);
	if (fixup) {
		regs->psw.addr = extable_fixup(fixup) | PSW_ADDR_AMODE;
		return;
	}

	/*
	 * Oops. The kernel tried to access some bad page. We'll have to
	 * terminate things with extreme prejudice.
	 */
	if (check_space(current) == 0)
		printk(KERN_ALERT "Unable to handle kernel pointer dereference"
		       " at virtual kernel address %p\n", (void *)address);
	else
		printk(KERN_ALERT "Unable to handle kernel paging request"
		       " at virtual user address %p\n", (void *)address);

	die("Oops", regs, error_code);
	do_exit(SIGKILL);
}

static void do_low_address(struct pt_regs *regs, unsigned long error_code)
	address = regs->int_parm_long & __FAIL_ADDR_MASK;
	if (!user_space_fault(regs))
		printk(KERN_ALERT "Unable to handle kernel pointer dereference"
		       " in virtual kernel address space\n");
	else
		printk(KERN_ALERT "Unable to handle kernel paging request"
		       " in virtual user address space\n");
	printk(KERN_ALERT "failing address: %016lx TEID: %016lx\n",
	       regs->int_parm_long & __FAIL_ADDR_MASK, regs->int_parm_long);
	dump_fault_info(regs);
	die(regs, "Oops");
	do_exit(SIGKILL);
}

static noinline void do_low_address(struct pt_regs *regs)
{
	/* Low-address protection hit in kernel mode means
	   NULL pointer write access in kernel mode.  */
	if (regs->psw.mask & PSW_MASK_PSTATE) {
		/* Low-address protection hit in user mode 'cannot happen'. */
		die ("Low-address protection", regs, error_code);
		do_exit(SIGKILL);
	}

	do_no_context(regs, error_code, 0);
}

/*
 * We ran out of memory, or some other thing happened to us that made
 * us unable to handle the page fault gracefully.
 */
static int do_out_of_memory(struct pt_regs *regs, unsigned long error_code,
			    unsigned long address)
{
	struct task_struct *tsk = current;
	struct mm_struct *mm = tsk->mm;

	up_read(&mm->mmap_sem);
	if (is_global_init(tsk)) {
		yield();
		down_read(&mm->mmap_sem);
		return 1;
	}
	printk("VM: killing process %s\n", tsk->comm);
	if (regs->psw.mask & PSW_MASK_PSTATE)
		do_group_exit(SIGKILL);
	do_no_context(regs, error_code, address);
	return 0;
}

static void do_sigbus(struct pt_regs *regs, unsigned long error_code,
		      unsigned long address)
{
	struct task_struct *tsk = current;
	struct mm_struct *mm = tsk->mm;

	up_read(&mm->mmap_sem);
		die (regs, "Low-address protection");
		do_exit(SIGKILL);
	}

	do_no_context(regs);
}

static noinline void do_sigbus(struct pt_regs *regs)
{
	struct task_struct *tsk = current;
	struct siginfo si;

	/*
	 * Send a sigbus, regardless of whether we were in kernel
	 * or user mode.
	 */
	tsk->thread.prot_addr = address;
	tsk->thread.trap_no = error_code;
	force_sig(SIGBUS, tsk);

	/* Kernel mode? Handle exceptions or die */
	if (!(regs->psw.mask & PSW_MASK_PSTATE))
		do_no_context(regs, error_code, address);
}

#ifdef CONFIG_S390_EXEC_PROTECT
static int signal_return(struct mm_struct *mm, struct pt_regs *regs,
			 unsigned long address, unsigned long error_code)
{
	u16 instruction;
	int rc;
#ifdef CONFIG_COMPAT
	int compat;
#endif

	pagefault_disable();
	rc = __get_user(instruction, (u16 __user *) regs->psw.addr);
	pagefault_enable();
	if (rc)
		return -EFAULT;

	up_read(&mm->mmap_sem);
	clear_tsk_thread_flag(current, TIF_SINGLE_STEP);
#ifdef CONFIG_COMPAT
	compat = test_tsk_thread_flag(current, TIF_31BIT);
	if (compat && instruction == 0x0a77)
		sys32_sigreturn();
	else if (compat && instruction == 0x0aad)
		sys32_rt_sigreturn();
	else
#endif
	if (instruction == 0x0a77)
		sys_sigreturn();
	else if (instruction == 0x0aad)
		sys_rt_sigreturn();
	else {
		current->thread.prot_addr = address;
		current->thread.trap_no = error_code;
		do_sigsegv(regs, error_code, SEGV_MAPERR, address);
	}
	return 0;
}
#endif /* CONFIG_S390_EXEC_PROTECT */
	si.si_signo = SIGBUS;
	si.si_errno = 0;
	si.si_code = BUS_ADRERR;
	si.si_addr = (void __user *)(regs->int_parm_long & __FAIL_ADDR_MASK);
	force_sig_info(SIGBUS, &si, tsk);
}

static noinline void do_fault_error(struct pt_regs *regs, int fault)
{
	int si_code;

	switch (fault) {
	case VM_FAULT_BADACCESS:
	case VM_FAULT_BADMAP:
		/* Bad memory access. Check if it is kernel or user space. */
		if (user_mode(regs)) {
			/* User mode accesses just cause a SIGSEGV */
			si_code = (fault == VM_FAULT_BADMAP) ?
				SEGV_MAPERR : SEGV_ACCERR;
			do_sigsegv(regs, si_code);
			return;
		}
	case VM_FAULT_BADCONTEXT:
	case VM_FAULT_PFAULT:
		do_no_context(regs);
		break;
	case VM_FAULT_SIGNAL:
		if (!user_mode(regs))
			do_no_context(regs);
		break;
	default: /* fault & VM_FAULT_ERROR */
		if (fault & VM_FAULT_OOM) {
			if (!user_mode(regs))
				do_no_context(regs);
			else
				pagefault_out_of_memory();
		} else if (fault & VM_FAULT_SIGSEGV) {
			/* Kernel mode? Handle exceptions or die */
			if (!user_mode(regs))
				do_no_context(regs);
			else
				do_sigsegv(regs, SEGV_MAPERR);
		} else if (fault & VM_FAULT_SIGBUS) {
			/* Kernel mode? Handle exceptions or die */
			if (!user_mode(regs))
				do_no_context(regs);
			else
				do_sigbus(regs);
		} else
			BUG();
		break;
	}
}

/*
 * This routine handles page faults.  It determines the address,
 * and the problem, and then passes it off to one of the appropriate
 * routines.
 *
 * error_code:
 * interruption code (int_code):
 *   04       Protection           ->  Write-Protection  (suprression)
 *   10       Segment translation  ->  Not present       (nullification)
 *   11       Page translation     ->  Not present       (nullification)
 *   3b       Region third trans.  ->  Not present       (nullification)
 */
static inline void
do_exception(struct pt_regs *regs, unsigned long error_code, int write)
{
	struct task_struct *tsk;
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	unsigned long address;
	int space;
	int si_code;
	int fault;

	if (notify_page_fault(regs, error_code))
		return;

	tsk = current;
	mm = tsk->mm;

	/* get the failing address and the affected space */
	address = S390_lowcore.trans_exc_code & __FAIL_ADDR_MASK;
	space = check_space(tsk);
static inline int do_exception(struct pt_regs *regs, int access)
{
#ifdef CONFIG_PGSTE
	struct gmap *gmap;
#endif
	struct task_struct *tsk;
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	unsigned long trans_exc_code;
	unsigned long address;
	unsigned int flags;
	int fault;

	tsk = current;
	/*
	 * The instruction that caused the program check has
	 * been nullified. Don't signal single step via SIGTRAP.
	 */
	clear_pt_regs_flag(regs, PIF_PER_TRAP);

	if (notify_page_fault(regs))
		return 0;

	mm = tsk->mm;
	trans_exc_code = regs->int_parm_long;

	/*
	 * Verify that the fault happened in user space, that
	 * we are not in an interrupt and that there is a 
	 * user context.
	 */
	if (unlikely(space == 0 || in_atomic() || !mm))
		goto no_context;

	/*
	 * When we get here, the fault happened in the current
	 * task's user address space, so we can switch on the
	 * interrupts again and then search the VMAs
	 */
	local_irq_enable();

	down_read(&mm->mmap_sem);

	si_code = SEGV_MAPERR;
	vma = find_vma(mm, address);
	if (!vma)
		goto bad_area;

#ifdef CONFIG_S390_EXEC_PROTECT
	if (unlikely((space == 2) && !(vma->vm_flags & VM_EXEC)))
		if (!signal_return(mm, regs, address, error_code))
			/*
			 * signal_return() has done an up_read(&mm->mmap_sem)
			 * if it returns 0.
			 */
			return;
#endif

	if (vma->vm_start <= address)
		goto good_area;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		goto bad_area;
	if (expand_stack(vma, address))
		goto bad_area;
/*
 * Ok, we have a good vm_area for this memory access, so
 * we can handle it..
 */
good_area:
	si_code = SEGV_ACCERR;
	if (!write) {
		/* page not present, check vm flags */
		if (!(vma->vm_flags & (VM_READ | VM_EXEC | VM_WRITE)))
			goto bad_area;
	} else {
		if (!(vma->vm_flags & VM_WRITE))
			goto bad_area;
	}

survive:
	fault = VM_FAULT_BADCONTEXT;
	if (unlikely(!user_space_fault(regs) || faulthandler_disabled() || !mm))
		goto out;

	address = trans_exc_code & __FAIL_ADDR_MASK;
	perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS, 1, regs, address);
	flags = FAULT_FLAG_ALLOW_RETRY | FAULT_FLAG_KILLABLE;
	if (user_mode(regs))
		flags |= FAULT_FLAG_USER;
	if ((trans_exc_code & store_indication) == 0x400)
		access = VM_WRITE;
	if (access == VM_WRITE)
		flags |= FAULT_FLAG_WRITE;
	down_read(&mm->mmap_sem);

#ifdef CONFIG_PGSTE
	gmap = (current->flags & PF_VCPU) ?
		(struct gmap *) S390_lowcore.gmap : NULL;
	if (gmap) {
		current->thread.gmap_addr = address;
		address = __gmap_translate(gmap, address);
		if (address == -EFAULT) {
			fault = VM_FAULT_BADMAP;
			goto out_up;
		}
		if (gmap->pfault_enabled)
			flags |= FAULT_FLAG_RETRY_NOWAIT;
	}
#endif

retry:
	fault = VM_FAULT_BADMAP;
	vma = find_vma(mm, address);
	if (!vma)
		goto out_up;

	if (unlikely(vma->vm_start > address)) {
		if (!(vma->vm_flags & VM_GROWSDOWN))
			goto out_up;
		if (expand_stack(vma, address))
			goto out_up;
	}

	/*
	 * Ok, we have a good vm_area for this memory access, so
	 * we can handle it..
	 */
	fault = VM_FAULT_BADACCESS;
	if (unlikely(!(vma->vm_flags & access)))
		goto out_up;

	if (is_vm_hugetlb_page(vma))
		address &= HPAGE_MASK;
	/*
	 * If for any reason at all we couldn't handle the fault,
	 * make sure we exit gracefully rather than endlessly redo
	 * the fault.
	 */
	fault = handle_mm_fault(mm, vma, address, write);
	if (unlikely(fault & VM_FAULT_ERROR)) {
		if (fault & VM_FAULT_OOM) {
			if (do_out_of_memory(regs, error_code, address))
				goto survive;
			return;
		} else if (fault & VM_FAULT_SIGBUS) {
			do_sigbus(regs, error_code, address);
			return;
		}
		BUG();
	}
	if (fault & VM_FAULT_MAJOR)
		tsk->maj_flt++;
	else
		tsk->min_flt++;

        up_read(&mm->mmap_sem);
	/*
	 * The instruction that caused the program check will
	 * be repeated. Don't signal single step via SIGTRAP.
	 */
	clear_tsk_thread_flag(tsk, TIF_SINGLE_STEP);
        return;

/*
 * Something tried to access memory that isn't in our memory map..
 * Fix it, but check if it's kernel or user first..
 */
bad_area:
	up_read(&mm->mmap_sem);

	/* User mode accesses just cause a SIGSEGV */
	if (regs->psw.mask & PSW_MASK_PSTATE) {
		tsk->thread.prot_addr = address;
		tsk->thread.trap_no = error_code;
		do_sigsegv(regs, error_code, si_code, address);
		return;
	}

no_context:
	do_no_context(regs, error_code, address);
}

void __kprobes do_protection_exception(struct pt_regs *regs,
				       long error_code)
{
	/* Protection exception is supressing, decrement psw address. */
	regs->psw.addr -= (error_code >> 16);
	fault = handle_mm_fault(mm, vma, address, flags);
	/* No reason to continue if interrupted by SIGKILL. */
	if ((fault & VM_FAULT_RETRY) && fatal_signal_pending(current)) {
		fault = VM_FAULT_SIGNAL;
		if (flags & FAULT_FLAG_RETRY_NOWAIT)
			goto out_up;
		goto out;
	}
	if (unlikely(fault & VM_FAULT_ERROR))
		goto out_up;

	/*
	 * Major/minor page fault accounting is only done on the
	 * initial attempt. If we go through a retry, it is extremely
	 * likely that the page will be found in page cache at that point.
	 */
	if (flags & FAULT_FLAG_ALLOW_RETRY) {
		if (fault & VM_FAULT_MAJOR) {
			tsk->maj_flt++;
			perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS_MAJ, 1,
				      regs, address);
		} else {
			tsk->min_flt++;
			perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS_MIN, 1,
				      regs, address);
		}
		if (fault & VM_FAULT_RETRY) {
#ifdef CONFIG_PGSTE
			if (gmap && (flags & FAULT_FLAG_RETRY_NOWAIT)) {
				/* FAULT_FLAG_RETRY_NOWAIT has been set,
				 * mmap_sem has not been released */
				current->thread.gmap_pfault = 1;
				fault = VM_FAULT_PFAULT;
				goto out_up;
			}
#endif
			/* Clear FAULT_FLAG_ALLOW_RETRY to avoid any risk
			 * of starvation. */
			flags &= ~(FAULT_FLAG_ALLOW_RETRY |
				   FAULT_FLAG_RETRY_NOWAIT);
			flags |= FAULT_FLAG_TRIED;
			down_read(&mm->mmap_sem);
			goto retry;
		}
	}
#ifdef CONFIG_PGSTE
	if (gmap) {
		address =  __gmap_link(gmap, current->thread.gmap_addr,
				       address);
		if (address == -EFAULT) {
			fault = VM_FAULT_BADMAP;
			goto out_up;
		}
		if (address == -ENOMEM) {
			fault = VM_FAULT_OOM;
			goto out_up;
		}
	}
#endif
	fault = 0;
out_up:
	up_read(&mm->mmap_sem);
out:
	return fault;
}

void do_protection_exception(struct pt_regs *regs)
{
	unsigned long trans_exc_code;
	int fault;

	trans_exc_code = regs->int_parm_long;
	/*
	 * Protection exceptions are suppressing, decrement psw address.
	 * The exception to this rule are aborted transactions, for these
	 * the PSW already points to the correct location.
	 */
	if (!(regs->int_code & 0x200))
		regs->psw.addr = __rewind_psw(regs->psw, regs->int_code >> 16);
	/*
	 * Check for low-address protection.  This needs to be treated
	 * as a special case because the translation exception code
	 * field is not guaranteed to contain valid data in this case.
	 */
	if (unlikely(!(S390_lowcore.trans_exc_code & 4))) {
		do_low_address(regs, error_code);
		return;
	}
	do_exception(regs, 4, 1);
}

void __kprobes do_dat_exception(struct pt_regs *regs, long error_code)
{
	do_exception(regs, error_code & 0xff, 0);
}

#ifdef CONFIG_64BIT
void __kprobes do_asce_exception(struct pt_regs *regs, unsigned long error_code)
{
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	unsigned long address;
	int space;

	mm = current->mm;
	address = S390_lowcore.trans_exc_code & __FAIL_ADDR_MASK;
	space = check_space(current);

	if (unlikely(space == 0 || in_atomic() || !mm))
		goto no_context;

	local_irq_enable();

	down_read(&mm->mmap_sem);
	vma = find_vma(mm, address);
	up_read(&mm->mmap_sem);

	if (vma) {
		update_mm(mm, current);
		return;
	}

	/* User mode accesses just cause a SIGSEGV */
	if (regs->psw.mask & PSW_MASK_PSTATE) {
		current->thread.prot_addr = address;
		current->thread.trap_no = error_code;
		do_sigsegv(regs, error_code, SEGV_MAPERR, address);
		return;
	}

no_context:
	do_no_context(regs, error_code, address);
}
#endif
	if (unlikely(!(trans_exc_code & 4))) {
		do_low_address(regs);
		return;
	}
	fault = do_exception(regs, VM_WRITE);
	if (unlikely(fault))
		do_fault_error(regs, fault);
}
NOKPROBE_SYMBOL(do_protection_exception);

void do_dat_exception(struct pt_regs *regs)
{
	int access, fault;

	access = VM_READ | VM_EXEC | VM_WRITE;
	fault = do_exception(regs, access);
	if (unlikely(fault))
		do_fault_error(regs, fault);
}
NOKPROBE_SYMBOL(do_dat_exception);

#ifdef CONFIG_PFAULT 
/*
 * 'pfault' pseudo page faults routines.
 */
static ext_int_info_t ext_int_pfault;
static int pfault_disable = 0;
static int pfault_disable;

static int __init nopfault(char *str)
{
	pfault_disable = 1;
	return 1;
}

__setup("nopfault", nopfault);

typedef struct {
	__u16 refdiagc;
	__u16 reffcode;
	__u16 refdwlen;
	__u16 refversn;
	__u64 refgaddr;
	__u64 refselmk;
	__u64 refcmpmk;
	__u64 reserved;
} __attribute__ ((packed, aligned(8))) pfault_refbk_t;

int pfault_init(void)
{
	pfault_refbk_t refbk =
		{ 0x258, 0, 5, 2, __LC_CURRENT, 1ULL << 48, 1ULL << 48,
		  __PF_RES_FIELD };
        int rc;

	if (!MACHINE_IS_VM || pfault_disable)
		return -1;
struct pfault_refbk {
	u16 refdiagc;
	u16 reffcode;
	u16 refdwlen;
	u16 refversn;
	u64 refgaddr;
	u64 refselmk;
	u64 refcmpmk;
	u64 reserved;
} __attribute__ ((packed, aligned(8)));

int pfault_init(void)
{
	struct pfault_refbk refbk = {
		.refdiagc = 0x258,
		.reffcode = 0,
		.refdwlen = 5,
		.refversn = 2,
		.refgaddr = __LC_LPP,
		.refselmk = 1ULL << 48,
		.refcmpmk = 1ULL << 48,
		.reserved = __PF_RES_FIELD };
        int rc;

	if (pfault_disable)
		return -1;
	diag_stat_inc(DIAG_STAT_X258);
	asm volatile(
		"	diag	%1,%0,0x258\n"
		"0:	j	2f\n"
		"1:	la	%0,8\n"
		"2:\n"
		EX_TABLE(0b,1b)
		: "=d" (rc) : "a" (&refbk), "m" (refbk) : "cc");
        __ctl_set_bit(0, 9);
        return rc;
}

void pfault_fini(void)
{
	pfault_refbk_t refbk =
	{ 0x258, 1, 5, 2, 0ULL, 0ULL, 0ULL, 0ULL };

	if (!MACHINE_IS_VM || pfault_disable)
		return;
	__ctl_clear_bit(0,9);
	struct pfault_refbk refbk = {
		.refdiagc = 0x258,
		.reffcode = 1,
		.refdwlen = 5,
		.refversn = 2,
	};

	if (pfault_disable)
		return;
	diag_stat_inc(DIAG_STAT_X258);
	asm volatile(
		"	diag	%0,0,0x258\n"
		"0:\n"
		EX_TABLE(0b,0b)
		: : "a" (&refbk), "m" (refbk) : "cc");
}

static void pfault_interrupt(__u16 error_code)
{
	struct task_struct *tsk;
	__u16 subcode;
static DEFINE_SPINLOCK(pfault_lock);
static LIST_HEAD(pfault_list);

static void pfault_interrupt(struct ext_code ext_code,
			     unsigned int param32, unsigned long param64)
{
	struct task_struct *tsk;
	__u16 subcode;
	pid_t pid;

	/*
	 * Get the external interruption subcode & pfault
	 * initial/completion signal bit. VM stores this 
	 * in the 'cpu address' field associated with the
         * external interrupt. 
	 */
	subcode = S390_lowcore.cpu_addr;
	if ((subcode & 0xff00) != __SUBCODE_MASK)
		return;

	/*
	 * Get the token (= address of the task structure of the affected task).
	 */
	tsk = *(struct task_struct **) __LC_PFAULT_INTPARM;

	if (subcode & 0x0080) {
		/* signal bit is set -> a page has been swapped in by VM */
		if (xchg(&tsk->thread.pfault_wait, -1) != 0) {
	subcode = ext_code.subcode;
	if ((subcode & 0xff00) != __SUBCODE_MASK)
		return;
	inc_irq_stat(IRQEXT_PFL);
	/* Get the token (= pid of the affected task). */
	pid = param64 & LPP_PFAULT_PID_MASK;
	rcu_read_lock();
	tsk = find_task_by_pid_ns(pid, &init_pid_ns);
	if (tsk)
		get_task_struct(tsk);
	rcu_read_unlock();
	if (!tsk)
		return;
	spin_lock(&pfault_lock);
	if (subcode & 0x0080) {
		/* signal bit is set -> a page has been swapped in by VM */
		if (tsk->thread.pfault_wait == 1) {
			/* Initial interrupt was faster than the completion
			 * interrupt. pfault_wait is valid. Set pfault_wait
			 * back to zero and wake up the process. This can
			 * safely be done because the task is still sleeping
			 * and can't produce new pfaults. */
			tsk->thread.pfault_wait = 0;
			wake_up_process(tsk);
			put_task_struct(tsk);
		}
	} else {
		/* signal bit not set -> a real page is missing. */
		get_task_struct(tsk);
		set_task_state(tsk, TASK_UNINTERRUPTIBLE);
		if (xchg(&tsk->thread.pfault_wait, 1) != 0) {
			/* Completion interrupt was faster than the initial
			 * interrupt (swapped in a -1 for pfault_wait). Set
			 * pfault_wait back to zero and exit. This can be
			 * done safely because tsk is running in kernel 
			 * mode and can't produce new pfaults. */
			tsk->thread.pfault_wait = 0;
			set_task_state(tsk, TASK_RUNNING);
			put_task_struct(tsk);
		} else
			set_tsk_need_resched(tsk);
	}
}

void __init pfault_irq_init(void)
{
	if (!MACHINE_IS_VM)
		return;

	/*
	 * Try to get pfault pseudo page faults going.
	 */
	if (register_early_external_interrupt(0x2603, pfault_interrupt,
					      &ext_int_pfault) != 0)
		panic("Couldn't request external interrupt 0x2603");

	if (pfault_init() == 0)
		return;

	/* Tough luck, no pfault. */
	pfault_disable = 1;
	unregister_early_external_interrupt(0x2603, pfault_interrupt,
					    &ext_int_pfault);
}
#endif
			list_del(&tsk->thread.list);
			wake_up_process(tsk);
			put_task_struct(tsk);
		} else {
			/* Completion interrupt was faster than initial
			 * interrupt. Set pfault_wait to -1 so the initial
			 * interrupt doesn't put the task to sleep.
			 * If the task is not running, ignore the completion
			 * interrupt since it must be a leftover of a PFAULT
			 * CANCEL operation which didn't remove all pending
			 * completion interrupts. */
			if (tsk->state == TASK_RUNNING)
				tsk->thread.pfault_wait = -1;
		}
	} else {
		/* signal bit not set -> a real page is missing. */
		if (WARN_ON_ONCE(tsk != current))
			goto out;
		if (tsk->thread.pfault_wait == 1) {
			/* Already on the list with a reference: put to sleep */
			__set_task_state(tsk, TASK_UNINTERRUPTIBLE);
			set_tsk_need_resched(tsk);
		} else if (tsk->thread.pfault_wait == -1) {
			/* Completion interrupt was faster than the initial
			 * interrupt (pfault_wait == -1). Set pfault_wait
			 * back to zero and exit. */
			tsk->thread.pfault_wait = 0;
		} else {
			/* Initial interrupt arrived before completion
			 * interrupt. Let the task sleep.
			 * An extra task reference is needed since a different
			 * cpu may set the task state to TASK_RUNNING again
			 * before the scheduler is reached. */
			get_task_struct(tsk);
			tsk->thread.pfault_wait = 1;
			list_add(&tsk->thread.list, &pfault_list);
			__set_task_state(tsk, TASK_UNINTERRUPTIBLE);
			set_tsk_need_resched(tsk);
		}
	}
out:
	spin_unlock(&pfault_lock);
	put_task_struct(tsk);
}

static int pfault_cpu_notify(struct notifier_block *self, unsigned long action,
			     void *hcpu)
{
	struct thread_struct *thread, *next;
	struct task_struct *tsk;

	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_DEAD:
		spin_lock_irq(&pfault_lock);
		list_for_each_entry_safe(thread, next, &pfault_list, list) {
			thread->pfault_wait = 0;
			list_del(&thread->list);
			tsk = container_of(thread, struct task_struct, thread);
			wake_up_process(tsk);
			put_task_struct(tsk);
		}
		spin_unlock_irq(&pfault_lock);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static int __init pfault_irq_init(void)
{
	int rc;

	rc = register_external_irq(EXT_IRQ_CP_SERVICE, pfault_interrupt);
	if (rc)
		goto out_extint;
	rc = pfault_init() == 0 ? 0 : -EOPNOTSUPP;
	if (rc)
		goto out_pfault;
	irq_subclass_register(IRQ_SUBCLASS_SERVICE_SIGNAL);
	hotcpu_notifier(pfault_cpu_notify, 0);
	return 0;

out_pfault:
	unregister_external_irq(EXT_IRQ_CP_SERVICE, pfault_interrupt);
out_extint:
	pfault_disable = 1;
	return rc;
}
early_initcall(pfault_irq_init);

#endif /* CONFIG_PFAULT */
