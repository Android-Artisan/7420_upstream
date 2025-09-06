/*
 *  linux/arch/arm/kernel/process.c
 *
 *  Copyright (C) 1996-2000 Russell King - Converted to ARM.
 *  Original Copyright (C) 1995  Linus Torvalds
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <stdarg.h>

#include <linux/module.h>
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/interrupt.h>
#include <linux/kallsyms.h>
#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/user.h>
#include <linux/interrupt.h>
#include <linux/kallsyms.h>
#include <linux/init.h>
#include <linux/elfcore.h>
#include <linux/pm.h>
#include <linux/tick.h>
#include <linux/utsname.h>

#include <asm/leds.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/thread_notify.h>
#include <asm/uaccess.h>
#include <asm/mach/time.h>

static const char *processor_modes[] = {
  "USER_26", "FIQ_26" , "IRQ_26" , "SVC_26" , "UK4_26" , "UK5_26" , "UK6_26" , "UK7_26" ,
  "UK8_26" , "UK9_26" , "UK10_26", "UK11_26", "UK12_26", "UK13_26", "UK14_26", "UK15_26",
  "USER_32", "FIQ_32" , "IRQ_32" , "SVC_32" , "UK4_32" , "UK5_32" , "UK6_32" , "ABT_32" ,
  "UK8_32" , "UK9_32" , "UK10_32", "UND_32" , "UK12_32", "UK13_32", "UK14_32", "SYS_32"
};

static const char *isa_modes[] = {
  "ARM" , "Thumb" , "Jazelle", "ThumbEE"
};

extern void setup_mm_for_reboot(char mode);

static volatile int hlt_counter;

#include <mach/system.h>

void disable_hlt(void)
{
	hlt_counter++;
}

EXPORT_SYMBOL(disable_hlt);

void enable_hlt(void)
{
	hlt_counter--;
}

EXPORT_SYMBOL(enable_hlt);

static int __init nohlt_setup(char *__unused)
{
	hlt_counter = 1;
	return 1;
}

static int __init hlt_setup(char *__unused)
{
	hlt_counter = 0;
	return 1;
}

__setup("nohlt", nohlt_setup);
__setup("hlt", hlt_setup);

void arm_machine_restart(char mode)
{
	/*
	 * Clean and disable cache, and turn off interrupts
	 */
	cpu_proc_fin();

	/*
	 * Tell the mm system that we are going to reboot -
	 * we may need it to insert some 1:1 mappings so that
	 * soft boot works.
	 */
	setup_mm_for_reboot(mode);

	/*
	 * Now call the architecture specific reboot code.
	 */
	arch_reset(mode);

	/*
	 * Whoops - the architecture was unable to reboot.
	 * Tell the user!
	 */
	mdelay(1000);
	printk("Reboot failed -- System halted\n");
	while (1);
}

/*
 * Function pointers to optional machine specific functions
 */
void (*pm_idle)(void);
EXPORT_SYMBOL(pm_idle);

void (*pm_power_off)(void);
EXPORT_SYMBOL(pm_power_off);

void (*arm_pm_restart)(char str) = arm_machine_restart;
EXPORT_SYMBOL_GPL(arm_pm_restart);


/*
 * This is our default idle handler.  We need to disable
 * interrupts here to ensure we don't miss a wakeup call.
 */
static void default_idle(void)
{
	if (hlt_counter)
		cpu_relax();
	else {
		local_irq_disable();
		if (!need_resched())
			arch_idle();
		local_irq_enable();
	}
}

/*
 * The idle thread.  We try to conserve power, while trying to keep
 * overall latency low.  The architecture specific idle is passed
 * a value to indicate the level of "idleness" of the system.
 */
void cpu_idle(void)
{
	local_fiq_enable();

	/* endless idle loop with no priority at all */
	while (1) {
		void (*idle)(void) = pm_idle;

#ifdef CONFIG_HOTPLUG_CPU
		if (cpu_is_offline(smp_processor_id())) {
			leds_event(led_idle_start);
			cpu_die();
		}
#endif

		if (!idle)
			idle = default_idle;
		leds_event(led_idle_start);
		tick_nohz_stop_sched_tick(1);
		while (!need_resched())
			idle();
		leds_event(led_idle_end);
		tick_nohz_restart_sched_tick();
		preempt_enable_no_resched();
		schedule();
		preempt_disable();
	}
}

static char reboot_mode = 'h';

int __init reboot_setup(char *str)
{
	reboot_mode = str[0];
	return 1;
}

__setup("reboot=", reboot_setup);

void machine_halt(void)
{
}


void machine_power_off(void)
{
	if (pm_power_off)
		pm_power_off();
}

void machine_restart(char * __unused)
{
	arm_pm_restart(reboot_mode);
#include <linux/uaccess.h>
#include <linux/random.h>
#include <linux/hw_breakpoint.h>
#include <linux/leds.h>

#include <asm/processor.h>
#include <asm/thread_notify.h>
#include <asm/stacktrace.h>
#include <asm/system_misc.h>
#include <asm/mach/time.h>
#include <asm/tls.h>
#include <asm/vdso.h>

#ifdef CONFIG_CC_STACKPROTECTOR
#include <linux/stackprotector.h>
unsigned long __stack_chk_guard __read_mostly;
EXPORT_SYMBOL(__stack_chk_guard);
#endif

static const char *processor_modes[] __maybe_unused = {
  "USER_26", "FIQ_26" , "IRQ_26" , "SVC_26" , "UK4_26" , "UK5_26" , "UK6_26" , "UK7_26" ,
  "UK8_26" , "UK9_26" , "UK10_26", "UK11_26", "UK12_26", "UK13_26", "UK14_26", "UK15_26",
  "USER_32", "FIQ_32" , "IRQ_32" , "SVC_32" , "UK4_32" , "UK5_32" , "MON_32" , "ABT_32" ,
  "UK8_32" , "UK9_32" , "HYP_32", "UND_32" , "UK12_32", "UK13_32", "UK14_32", "SYS_32"
};

static const char *isa_modes[] __maybe_unused = {
  "ARM" , "Thumb" , "Jazelle", "ThumbEE"
};

/*
 * This is our default idle handler.
 */

void (*arm_pm_idle)(void);

/*
 * Called from the core idle loop.
 */

void arch_cpu_idle(void)
{
	if (arm_pm_idle)
		arm_pm_idle();
	else
		cpu_do_idle();
	local_irq_enable();
}

void arch_cpu_idle_prepare(void)
{
	local_fiq_enable();
}

void arch_cpu_idle_enter(void)
{
	ledtrig_cpu(CPU_LED_IDLE_START);
#ifdef CONFIG_PL310_ERRATA_769419
	wmb();
#endif
}

void arch_cpu_idle_exit(void)
{
	ledtrig_cpu(CPU_LED_IDLE_END);
}

void __show_regs(struct pt_regs *regs)
{
	unsigned long flags;
	char buf[64];

	printk("CPU: %d    %s  (%s %.*s)\n",
		smp_processor_id(), print_tainted(), init_utsname()->release,
		(int)strcspn(init_utsname()->version, " "),
		init_utsname()->version);
#ifndef CONFIG_CPU_V7M
	unsigned int domain;
#ifdef CONFIG_CPU_SW_DOMAIN_PAN
	/*
	 * Get the domain register for the parent context. In user
	 * mode, we don't save the DACR, so lets use what it should
	 * be. For other modes, we place it after the pt_regs struct.
	 */
	if (user_mode(regs))
		domain = DACR_UACCESS_ENABLE;
	else
		domain = *(unsigned int *)(regs + 1);
#else
	domain = get_domain();
#endif
#endif

	show_regs_print_info(KERN_DEFAULT);

	print_symbol("PC is at %s\n", instruction_pointer(regs));
	print_symbol("LR is at %s\n", regs->ARM_lr);
	printk("pc : [<%08lx>]    lr : [<%08lx>]    psr: %08lx\n"
	       "sp : %08lx  ip : %08lx  fp : %08lx\n",
		regs->ARM_pc, regs->ARM_lr, regs->ARM_cpsr,
		regs->ARM_sp, regs->ARM_ip, regs->ARM_fp);
	printk("r10: %08lx  r9 : %08lx  r8 : %08lx\n",
		regs->ARM_r10, regs->ARM_r9,
		regs->ARM_r8);
	printk("r7 : %08lx  r6 : %08lx  r5 : %08lx  r4 : %08lx\n",
		regs->ARM_r7, regs->ARM_r6,
		regs->ARM_r5, regs->ARM_r4);
	printk("r3 : %08lx  r2 : %08lx  r1 : %08lx  r0 : %08lx\n",
		regs->ARM_r3, regs->ARM_r2,
		regs->ARM_r1, regs->ARM_r0);

	flags = regs->ARM_cpsr;
	buf[0] = flags & PSR_N_BIT ? 'N' : 'n';
	buf[1] = flags & PSR_Z_BIT ? 'Z' : 'z';
	buf[2] = flags & PSR_C_BIT ? 'C' : 'c';
	buf[3] = flags & PSR_V_BIT ? 'V' : 'v';
	buf[4] = '\0';

	printk("Flags: %s  IRQs o%s  FIQs o%s  Mode %s  ISA %s  Segment %s\n",
		buf, interrupts_enabled(regs) ? "n" : "ff",
		fast_interrupts_enabled(regs) ? "n" : "ff",
		processor_modes[processor_mode(regs)],
		isa_modes[isa_mode(regs)],
		get_fs() == get_ds() ? "kernel" : "user");
#ifndef CONFIG_CPU_V7M
	{
		const char *segment;

		if ((domain & domain_mask(DOMAIN_USER)) ==
		    domain_val(DOMAIN_USER, DOMAIN_NOACCESS))
			segment = "none";
		else if (get_fs() == get_ds())
			segment = "kernel";
		else
			segment = "user";

		printk("Flags: %s  IRQs o%s  FIQs o%s  Mode %s  ISA %s  Segment %s\n",
			buf, interrupts_enabled(regs) ? "n" : "ff",
			fast_interrupts_enabled(regs) ? "n" : "ff",
			processor_modes[processor_mode(regs)],
			isa_modes[isa_mode(regs)], segment);
	}
#else
	printk("xPSR: %08lx\n", regs->ARM_cpsr);
#endif

#ifdef CONFIG_CPU_CP15
	{
		unsigned int ctrl;

		buf[0] = '\0';
#ifdef CONFIG_CPU_CP15_MMU
		{
			unsigned int transbase, dac;
			asm("mrc p15, 0, %0, c2, c0\n\t"
			    "mrc p15, 0, %1, c3, c0\n"
			    : "=r" (transbase), "=r" (dac));
			snprintf(buf, sizeof(buf), "  Table: %08x  DAC: %08x",
			  	transbase, dac);
			unsigned int transbase;
			asm("mrc p15, 0, %0, c2, c0\n\t"
			    : "=r" (transbase));
			snprintf(buf, sizeof(buf), "  Table: %08x  DAC: %08x",
				transbase, domain);
		}
#endif
		asm("mrc p15, 0, %0, c1, c0\n" : "=r" (ctrl));

		printk("Control: %08x%s\n", ctrl, buf);
	}
#endif
}

void show_regs(struct pt_regs * regs)
{
	printk("\n");
	printk("Pid: %d, comm: %20s\n", task_pid_nr(current), current->comm);
	__show_regs(regs);
	__backtrace();
}

void show_fpregs(struct user_fp *regs)
{
	int i;

	for (i = 0; i < 8; i++) {
		unsigned long *p;
		char type;

		p = (unsigned long *)(regs->fpregs + i);

		switch (regs->ftype[i]) {
			case 1: type = 'f'; break;
			case 2: type = 'd'; break;
			case 3: type = 'e'; break;
			default: type = '?'; break;
		}
		if (regs->init_flag)
			type = '?';

		printk("  f%d(%c): %08lx %08lx %08lx%c",
			i, type, p[0], p[1], p[2], i & 1 ? '\n' : ' ');
	}
			

	printk("FPSR: %08lx FPCR: %08lx\n",
		(unsigned long)regs->fpsr,
		(unsigned long)regs->fpcr);
}

/*
 * Free current thread data structures etc..
 */
void exit_thread(void)
{
	__show_regs(regs);
	dump_stack();
}

ATOMIC_NOTIFIER_HEAD(thread_notify_head);

EXPORT_SYMBOL_GPL(thread_notify_head);

/*
 * Free current thread data structures etc..
 */
void exit_thread(void)
{
	thread_notify(THREAD_NOTIFY_EXIT, current_thread_info());
}

void flush_thread(void)
{
	struct thread_info *thread = current_thread_info();
	struct task_struct *tsk = current;

	flush_ptrace_hw_breakpoint(tsk);

	memset(thread->used_cp, 0, sizeof(thread->used_cp));
	memset(&tsk->thread.debug, 0, sizeof(struct debug_info));
	memset(&thread->fpstate, 0, sizeof(union fp_state));

	flush_tls();

	thread_notify(THREAD_NOTIFY_FLUSH, thread);
}

void release_thread(struct task_struct *dead_task)
{
	struct thread_info *thread = task_thread_info(dead_task);

	thread_notify(THREAD_NOTIFY_RELEASE, thread);
}

asmlinkage void ret_from_fork(void) __asm__("ret_from_fork");

int
copy_thread(int nr, unsigned long clone_flags, unsigned long stack_start,
	    unsigned long stk_sz, struct task_struct *p, struct pt_regs *regs)
copy_thread(unsigned long clone_flags, unsigned long stack_start,
	    unsigned long stk_sz, struct task_struct *p)
{
	struct thread_info *thread = task_thread_info(p);
	struct pt_regs *childregs = task_pt_regs(p);

	*childregs = *regs;
	childregs->ARM_r0 = 0;
	childregs->ARM_sp = stack_start;

	memset(&thread->cpu_context, 0, sizeof(struct cpu_context_save));
	thread->cpu_context.sp = (unsigned long)childregs;
	thread->cpu_context.pc = (unsigned long)ret_from_fork;

	if (clone_flags & CLONE_SETTLS)
		thread->tp_value = regs->ARM_r3;
	memset(&thread->cpu_context, 0, sizeof(struct cpu_context_save));

#ifdef CONFIG_CPU_USE_DOMAINS
	/*
	 * Copy the initial value of the domain access control register
	 * from the current thread: thread->addr_limit will have been
	 * copied from the current thread via setup_thread_stack() in
	 * kernel/fork.c
	 */
	thread->cpu_domain = get_domain();
#endif

	if (likely(!(p->flags & PF_KTHREAD))) {
		*childregs = *current_pt_regs();
		childregs->ARM_r0 = 0;
		if (stack_start)
			childregs->ARM_sp = stack_start;
	} else {
		memset(childregs, 0, sizeof(struct pt_regs));
		thread->cpu_context.r4 = stk_sz;
		thread->cpu_context.r5 = stack_start;
		childregs->ARM_cpsr = SVC_MODE;
	}
	thread->cpu_context.pc = (unsigned long)ret_from_fork;
	thread->cpu_context.sp = (unsigned long)childregs;

	clear_ptrace_hw_breakpoint(p);

	if (clone_flags & CLONE_SETTLS)
		thread->tp_value[0] = childregs->ARM_r3;
	thread->tp_value[1] = get_tpuser();

	thread_notify(THREAD_NOTIFY_COPY, thread);

	return 0;
}

/*
 * Fill in the task's elfregs structure for a core dump.
 */
int dump_task_regs(struct task_struct *t, elf_gregset_t *elfregs)
{
	elf_core_copy_regs(elfregs, task_pt_regs(t));
	return 1;
}

/*
 * fill in the fpe structure for a core dump...
 */
int dump_fpu (struct pt_regs *regs, struct user_fp *fp)
{
	struct thread_info *thread = current_thread_info();
	int used_math = thread->used_cp[1] | thread->used_cp[2];

	if (used_math)
		memcpy(fp, &thread->fpstate.soft, sizeof (*fp));

	return used_math != 0;
}
EXPORT_SYMBOL(dump_fpu);

/*
 * Shuffle the argument into the correct register before calling the
 * thread function.  r1 is the thread argument, r2 is the pointer to
 * the thread function, and r3 points to the exit function.
 */
extern void kernel_thread_helper(void);
asm(	".section .text\n"
"	.align\n"
"	.type	kernel_thread_helper, #function\n"
"kernel_thread_helper:\n"
"	mov	r0, r1\n"
"	mov	lr, r3\n"
"	mov	pc, r2\n"
"	.size	kernel_thread_helper, . - kernel_thread_helper\n"
"	.previous");

/*
 * Create a kernel thread.
 */
pid_t kernel_thread(int (*fn)(void *), void *arg, unsigned long flags)
{
	struct pt_regs regs;

	memset(&regs, 0, sizeof(regs));

	regs.ARM_r1 = (unsigned long)arg;
	regs.ARM_r2 = (unsigned long)fn;
	regs.ARM_r3 = (unsigned long)do_exit;
	regs.ARM_pc = (unsigned long)kernel_thread_helper;
	regs.ARM_cpsr = SVC_MODE;

	return do_fork(flags|CLONE_VM|CLONE_UNTRACED, 0, &regs, 0, NULL, NULL);
}
EXPORT_SYMBOL(kernel_thread);

unsigned long get_wchan(struct task_struct *p)
{
	unsigned long fp, lr;
	unsigned long stack_start, stack_end;
unsigned long get_wchan(struct task_struct *p)
{
	struct stackframe frame;
	unsigned long stack_page;
	int count = 0;
	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;

	stack_start = (unsigned long)end_of_stack(p);
	stack_end = (unsigned long)task_stack_page(p) + THREAD_SIZE;

	fp = thread_saved_fp(p);
	do {
		if (fp < stack_start || fp > stack_end)
			return 0;
		lr = pc_pointer (((unsigned long *)fp)[-1]);
		if (!in_sched_functions(lr))
			return lr;
		fp = *(unsigned long *) (fp - 12);
	} while (count ++ < 16);
	return 0;
}
	frame.fp = thread_saved_fp(p);
	frame.sp = thread_saved_sp(p);
	frame.lr = 0;			/* recovered from the stack */
	frame.pc = thread_saved_pc(p);
	stack_page = (unsigned long)task_stack_page(p);
	do {
		if (frame.sp < stack_page ||
		    frame.sp >= stack_page + THREAD_SIZE ||
		    unwind_frame(&frame) < 0)
			return 0;
		if (!in_sched_functions(frame.pc))
			return frame.pc;
	} while (count ++ < 16);
	return 0;
}

unsigned long arch_randomize_brk(struct mm_struct *mm)
{
	unsigned long range_end = mm->brk + 0x02000000;
	return randomize_range(mm->brk, range_end, 0) ? : mm->brk;
}

#ifdef CONFIG_MMU
/*
 * CONFIG_SPLIT_PTLOCK_CPUS results in a page->ptl lock.  If the lock is not
 * initialized by pgtable_page_ctor() then a coredump of the vector page will
 * fail.
 */
static int __init vectors_user_mapping_init_page(void)
{
	struct page *page;
	unsigned long addr = 0xffff0000;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;

	pgd = pgd_offset_k(addr);
	pud = pud_offset(pgd, addr);
	pmd = pmd_offset(pud, addr);
	page = pmd_page(*(pmd));

	pgtable_page_ctor(page);

	return 0;
}
late_initcall(vectors_user_mapping_init_page);

#ifdef CONFIG_KUSER_HELPERS
/*
 * The vectors page is always readable from user space for the
 * atomic helpers. Insert it into the gate_vma so that it is visible
 * through ptrace and /proc/<pid>/mem.
 */
static struct vm_area_struct gate_vma = {
	.vm_start	= 0xffff0000,
	.vm_end		= 0xffff0000 + PAGE_SIZE,
	.vm_flags	= VM_READ | VM_EXEC | VM_MAYREAD | VM_MAYEXEC,
};

static int __init gate_vma_init(void)
{
	gate_vma.vm_page_prot = PAGE_READONLY_EXEC;
	return 0;
}
arch_initcall(gate_vma_init);

struct vm_area_struct *get_gate_vma(struct mm_struct *mm)
{
	return &gate_vma;
}

int in_gate_area(struct mm_struct *mm, unsigned long addr)
{
	return (addr >= gate_vma.vm_start) && (addr < gate_vma.vm_end);
}

int in_gate_area_no_mm(unsigned long addr)
{
	return in_gate_area(NULL, addr);
}
#define is_gate_vma(vma)	((vma) == &gate_vma)
#else
#define is_gate_vma(vma)	0
#endif

const char *arch_vma_name(struct vm_area_struct *vma)
{
	return is_gate_vma(vma) ? "[vectors]" : NULL;
}

/* If possible, provide a placement hint at a random offset from the
 * stack for the sigpage and vdso pages.
 */
static unsigned long sigpage_addr(const struct mm_struct *mm,
				  unsigned int npages)
{
	unsigned long offset;
	unsigned long first;
	unsigned long last;
	unsigned long addr;
	unsigned int slots;

	first = PAGE_ALIGN(mm->start_stack);

	last = TASK_SIZE - (npages << PAGE_SHIFT);

	/* No room after stack? */
	if (first > last)
		return 0;

	/* Just enough room? */
	if (first == last)
		return first;

	slots = ((last - first) >> PAGE_SHIFT) + 1;

	offset = get_random_int() % slots;

	addr = first + (offset << PAGE_SHIFT);

	return addr;
}

static struct page *signal_page;
extern struct page *get_signal_page(void);

static const struct vm_special_mapping sigpage_mapping = {
	.name = "[sigpage]",
	.pages = &signal_page,
};

int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long npages;
	unsigned long addr;
	unsigned long hint;
	int ret = 0;

	if (!signal_page)
		signal_page = get_signal_page();
	if (!signal_page)
		return -ENOMEM;

	npages = 1; /* for sigpage */
	npages += vdso_total_pages;

	down_write(&mm->mmap_sem);
	hint = sigpage_addr(mm, npages);
	addr = get_unmapped_area(NULL, hint, npages << PAGE_SHIFT, 0, 0);
	if (IS_ERR_VALUE(addr)) {
		ret = addr;
		goto up_fail;
	}

	vma = _install_special_mapping(mm, addr, PAGE_SIZE,
		VM_READ | VM_EXEC | VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC,
		&sigpage_mapping);

	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto up_fail;
	}

	mm->context.sigpage = addr;

	/* Unlike the sigpage, failure to install the vdso is unlikely
	 * to be fatal to the process, so no error check needed
	 * here.
	 */
	arm_install_vdso(mm, addr + PAGE_SIZE);

 up_fail:
	up_write(&mm->mmap_sem);
	return ret;
}
#endif
