/*
 *  arch/s390/kernel/process.c
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *               Hartmut Penner (hp@de.ibm.com),
 *               Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com),
 *
 *  Derived from "arch/i386/kernel/process.c"
 *    Copyright (C) 1995, Linus Torvalds
 */

/*
 * This file handles the architecture-dependent parts of process handling..
 * This file handles the architecture dependent parts of process handling.
 *
 *    Copyright IBM Corp. 1999, 2009
 *    Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>,
 *		 Hartmut Penner <hp@de.ibm.com>,
 *		 Denis Joseph Barrow,
 */

#include <linux/compiler.h>
#include <linux/cpu.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/smp.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/user.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/utsname.h>
#include <linux/tick.h>
#include <linux/elfcore.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/irq.h>
#include <asm/timer.h>
#include <asm/cpu.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/elfcore.h>
#include <linux/smp.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/tick.h>
#include <linux/personality.h>
#include <linux/syscalls.h>
#include <linux/compat.h>
#include <linux/kprobes.h>
#include <linux/random.h>
#include <linux/module.h>
#include <linux/init_task.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/vtimer.h>
#include <asm/exec.h>
#include <asm/irq.h>
#include <asm/nmi.h>
#include <asm/smp.h>
#include <asm/switch_to.h>
#include <asm/runtime_instr.h>
#include "entry.h"

asmlinkage void ret_from_fork(void) asm ("ret_from_fork");

/* FPU save area for the init task */
__vector128 init_task_fpu_regs[__NUM_VXRS] __init_task_data;

/*
 * Return saved PC of a blocked thread. used in kernel/sched.
 * resume in entry.S does not create a new stack frame, it
 * just stores the registers %r6-%r15 to the frame given by
 * schedule. We want to return the address of the caller of
 * schedule, so we have to walk the backchain one time to
 * find the frame schedule() store its return address.
 */
unsigned long thread_saved_pc(struct task_struct *tsk)
{
	struct stack_frame *sf, *low, *high;

	if (!tsk || !task_stack_page(tsk))
		return 0;
	low = task_stack_page(tsk);
	high = (struct stack_frame *) task_pt_regs(tsk);
	sf = (struct stack_frame *) (tsk->thread.ksp & PSW_ADDR_INSN);
	if (sf <= low || sf > high)
		return 0;
	sf = (struct stack_frame *) (sf->back_chain & PSW_ADDR_INSN);
	if (sf <= low || sf > high)
		return 0;
	return sf->gprs[8];
}

DEFINE_PER_CPU(struct s390_idle_data, s390_idle) = {
	.lock = __SPIN_LOCK_UNLOCKED(s390_idle.lock)
};

static int s390_idle_enter(void)
{
	struct s390_idle_data *idle;

	idle = &__get_cpu_var(s390_idle);
	spin_lock(&idle->lock);
	idle->idle_count++;
	idle->in_idle = 1;
	idle->idle_enter = get_clock();
	spin_unlock(&idle->lock);
	vtime_stop_cpu_timer();
	return NOTIFY_OK;
}

void s390_idle_leave(void)
{
	struct s390_idle_data *idle;

	vtime_start_cpu_timer();
	idle = &__get_cpu_var(s390_idle);
	spin_lock(&idle->lock);
	idle->idle_time += get_clock() - idle->idle_enter;
	idle->in_idle = 0;
	spin_unlock(&idle->lock);
}

extern void s390_handle_mcck(void);
/*
 * The idle loop on a S390...
 */
static void default_idle(void)
{
	/* CPU is going idle. */
	local_irq_disable();
	if (need_resched()) {
		local_irq_enable();
		return;
	}
	if (s390_idle_enter() == NOTIFY_BAD) {
		local_irq_enable();
		return;
	}
#ifdef CONFIG_HOTPLUG_CPU
	if (cpu_is_offline(smp_processor_id())) {
		preempt_enable_no_resched();
		cpu_die();
	}
#endif
	local_mcck_disable();
	if (test_thread_flag(TIF_MCCK_PENDING)) {
		local_mcck_enable();
		s390_idle_leave();
		local_irq_enable();
		s390_handle_mcck();
		return;
	}
	trace_hardirqs_on();
	/* Wait for external, I/O or machine check interrupt. */
	__load_psw_mask(psw_kernel_bits | PSW_MASK_WAIT |
			PSW_MASK_IO | PSW_MASK_EXT);
}

void cpu_idle(void)
{
	for (;;) {
		tick_nohz_stop_sched_tick(1);
		while (!need_resched())
			default_idle();
		tick_nohz_restart_sched_tick();
		preempt_enable_no_resched();
		schedule();
		preempt_disable();
	}
}

extern void kernel_thread_starter(void);

asm(
	".align 4\n"
	"kernel_thread_starter:\n"
	"    la    2,0(10)\n"
	"    basr  14,9\n"
	"    la    2,0\n"
	"    br    11\n");

int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags)
{
	struct pt_regs regs;

	memset(&regs, 0, sizeof(regs));
	regs.psw.mask = psw_kernel_bits | PSW_MASK_IO | PSW_MASK_EXT;
	regs.psw.addr = (unsigned long) kernel_thread_starter | PSW_ADDR_AMODE;
	regs.gprs[9] = (unsigned long) fn;
	regs.gprs[10] = (unsigned long) arg;
	regs.gprs[11] = (unsigned long) do_exit;
	regs.orig_gpr2 = -1;

	/* Ok, create the new process.. */
	return do_fork(flags | CLONE_VM | CLONE_UNTRACED,
		       0, &regs, 0, NULL, NULL);
}

extern void kernel_thread_starter(void);

/*
 * Free current thread data structures etc..
 */
void exit_thread(void)
{
}

void flush_thread(void)
{
	clear_used_math();
	clear_tsk_thread_flag(current, TIF_USEDFPU);
}

void release_thread(struct task_struct *dead_task)
{
}

int copy_thread(int nr, unsigned long clone_flags, unsigned long new_stackp,
	unsigned long unused,
        struct task_struct * p, struct pt_regs * regs)
{
        struct fake_frame
          {
	    struct stack_frame sf;
            struct pt_regs childregs;
          } *frame;

        frame = container_of(task_pt_regs(p), struct fake_frame, childregs);
        p->thread.ksp = (unsigned long) frame;
	/* Store access registers to kernel stack of new process. */
        frame->childregs = *regs;
	frame->childregs.gprs[2] = 0;	/* child returns 0 on fork. */
        frame->childregs.gprs[15] = new_stackp;
        frame->sf.back_chain = 0;

        /* new return point is ret_from_fork */
        frame->sf.gprs[8] = (unsigned long) ret_from_fork;

        /* fake return stack for resume(), don't go back to schedule */
        frame->sf.gprs[9] = (unsigned long) frame;

	/* Save access registers to new thread structure. */
	save_access_regs(&p->thread.acrs[0]);

#ifndef CONFIG_64BIT
        /*
	 * save fprs to current->thread.fp_regs to merge them with
	 * the emulated registers and then copy the result to the child.
	 */
	save_fp_regs(&current->thread.fp_regs);
	memcpy(&p->thread.fp_regs, &current->thread.fp_regs,
	       sizeof(s390_fp_regs));
	/* Set a new TLS ?  */
	if (clone_flags & CLONE_SETTLS)
		p->thread.acrs[0] = regs->gprs[6];
#else /* CONFIG_64BIT */
	/* Save the fpu registers to new thread structure. */
	save_fp_regs(&p->thread.fp_regs);
	/* Set a new TLS ?  */
	if (clone_flags & CLONE_SETTLS) {
		if (test_thread_flag(TIF_31BIT)) {
			p->thread.acrs[0] = (unsigned int) regs->gprs[6];
		} else {
			p->thread.acrs[0] = (unsigned int)(regs->gprs[6] >> 32);
			p->thread.acrs[1] = (unsigned int) regs->gprs[6];
		}
	}
#endif /* CONFIG_64BIT */
	/* start new process with ar4 pointing to the correct address space */
	p->thread.mm_segment = get_fs();
        /* Don't copy debug registers */
        memset(&p->thread.per_info,0,sizeof(p->thread.per_info));

        return 0;
}

asmlinkage long sys_fork(void)
{
	struct pt_regs *regs = task_pt_regs(current);
	return do_fork(SIGCHLD, regs->gprs[15], regs, 0, NULL, NULL);
}

asmlinkage long sys_clone(void)
{
	struct pt_regs *regs = task_pt_regs(current);
	unsigned long clone_flags;
	unsigned long newsp;
	int __user *parent_tidptr, *child_tidptr;

	clone_flags = regs->gprs[3];
	newsp = regs->orig_gpr2;
	parent_tidptr = (int __user *) regs->gprs[4];
	child_tidptr = (int __user *) regs->gprs[5];
	if (!newsp)
		newsp = regs->gprs[15];
	return do_fork(clone_flags, newsp, regs, 0,
		       parent_tidptr, child_tidptr);
}

/*
 * This is trivial, and on the face of it looks like it
 * could equally well be done in user mode.
 *
 * Not so, for quite unobvious reasons - register pressure.
 * In user mode vfork() cannot have a stack frame, and if
 * done by calling the "clone()" system call directly, you
 * do not have enough call-clobbered registers to hold all
 * the information you need.
 */
asmlinkage long sys_vfork(void)
{
	struct pt_regs *regs = task_pt_regs(current);
	return do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD,
		       regs->gprs[15], regs, 0, NULL, NULL);
void arch_release_task_struct(struct task_struct *tsk)
{
	/* Free either the floating-point or the vector register save area */
	kfree(tsk->thread.fpu.regs);
	runtime_instr_release(tsk);
}

int arch_dup_task_struct(struct task_struct *dst, struct task_struct *src)
{
	size_t fpu_regs_size;

	*dst = *src;

	/*
	 * If the vector extension is available, it is enabled for all tasks,
	 * and, thus, the FPU register save area must be allocated accordingly.
	 */
	fpu_regs_size = MACHINE_HAS_VX ? sizeof(__vector128) * __NUM_VXRS
				       : sizeof(freg_t) * __NUM_FPRS;
	dst->thread.fpu.regs = kzalloc(fpu_regs_size, GFP_KERNEL|__GFP_REPEAT);
	if (!dst->thread.fpu.regs)
		return -ENOMEM;

	/*
	 * Save the floating-point or vector register state of the current
	 * task and set the CIF_FPU flag to lazy restore the FPU register
	 * state when returning to user space.
	 */
	save_fpu_regs();
	dst->thread.fpu.fpc = current->thread.fpu.fpc;
	memcpy(dst->thread.fpu.regs, current->thread.fpu.regs, fpu_regs_size);

	return 0;
}

int copy_thread(unsigned long clone_flags, unsigned long new_stackp,
		unsigned long arg, struct task_struct *p)
{
	struct thread_info *ti;
	struct fake_frame
	{
		struct stack_frame sf;
		struct pt_regs childregs;
	} *frame;

	frame = container_of(task_pt_regs(p), struct fake_frame, childregs);
	p->thread.ksp = (unsigned long) frame;
	/* Save access registers to new thread structure. */
	save_access_regs(&p->thread.acrs[0]);
	/* start new process with ar4 pointing to the correct address space */
	p->thread.mm_segment = get_fs();
	/* Don't copy debug registers */
	memset(&p->thread.per_user, 0, sizeof(p->thread.per_user));
	memset(&p->thread.per_event, 0, sizeof(p->thread.per_event));
	clear_tsk_thread_flag(p, TIF_SINGLE_STEP);
	p->thread.per_flags = 0;
	/* Initialize per thread user and system timer values */
	ti = task_thread_info(p);
	ti->user_timer = 0;
	ti->system_timer = 0;

	frame->sf.back_chain = 0;
	/* new return point is ret_from_fork */
	frame->sf.gprs[8] = (unsigned long) ret_from_fork;
	/* fake return stack for resume(), don't go back to schedule */
	frame->sf.gprs[9] = (unsigned long) frame;

	/* Store access registers to kernel stack of new process. */
	if (unlikely(p->flags & PF_KTHREAD)) {
		/* kernel thread */
		memset(&frame->childregs, 0, sizeof(struct pt_regs));
		frame->childregs.psw.mask = PSW_KERNEL_BITS | PSW_MASK_DAT |
				PSW_MASK_IO | PSW_MASK_EXT | PSW_MASK_MCHECK;
		frame->childregs.psw.addr = PSW_ADDR_AMODE |
				(unsigned long) kernel_thread_starter;
		frame->childregs.gprs[9] = new_stackp; /* function */
		frame->childregs.gprs[10] = arg;
		frame->childregs.gprs[11] = (unsigned long) do_exit;
		frame->childregs.orig_gpr2 = -1;

		return 0;
	}
	frame->childregs = *current_pt_regs();
	frame->childregs.gprs[2] = 0;	/* child returns 0 on fork. */
	frame->childregs.flags = 0;
	if (new_stackp)
		frame->childregs.gprs[15] = new_stackp;

	/* Don't copy runtime instrumentation info */
	p->thread.ri_cb = NULL;
	frame->childregs.psw.mask &= ~PSW_MASK_RI;

	/* Set a new TLS ?  */
	if (clone_flags & CLONE_SETTLS) {
		unsigned long tls = frame->childregs.gprs[6];
		if (is_compat_task()) {
			p->thread.acrs[0] = (unsigned int)tls;
		} else {
			p->thread.acrs[0] = (unsigned int)(tls >> 32);
			p->thread.acrs[1] = (unsigned int)tls;
		}
	}
	return 0;
}

asmlinkage void execve_tail(void)
{
	task_lock(current);
	current->ptrace &= ~PT_DTRACE;
	task_unlock(current);
	current->thread.fp_regs.fpc = 0;
	if (MACHINE_HAS_IEEE)
		asm volatile("sfpc %0,%0" : : "d" (0));
}

/*
 * sys_execve() executes a new program.
 */
asmlinkage long sys_execve(void)
{
	struct pt_regs *regs = task_pt_regs(current);
	char *filename;
	unsigned long result;
	int rc;

	filename = getname((char __user *) regs->orig_gpr2);
	if (IS_ERR(filename)) {
		result = PTR_ERR(filename);
		goto out;
	}
	rc = do_execve(filename, (char __user * __user *) regs->gprs[3],
		       (char __user * __user *) regs->gprs[4], regs);
	if (rc) {
		result = rc;
		goto out_putname;
	}
	execve_tail();
	result = regs->gprs[2];
out_putname:
	putname(filename);
out:
	return result;
	current->thread.fpu.fpc = 0;
	asm volatile("sfpc %0" : : "d" (0));
}

/*
 * fill in the FPU structure for a core dump.
 */
int dump_fpu (struct pt_regs * regs, s390_fp_regs *fpregs)
{
#ifndef CONFIG_64BIT
        /*
	 * save fprs to current->thread.fp_regs to merge them with
	 * the emulated registers and then copy the result to the dump.
	 */
	save_fp_regs(&current->thread.fp_regs);
	memcpy(fpregs, &current->thread.fp_regs, sizeof(s390_fp_regs));
#else /* CONFIG_64BIT */
	save_fp_regs(fpregs);
#endif /* CONFIG_64BIT */
	return 1;
}
	save_fpu_regs();
	fpregs->fpc = current->thread.fpu.fpc;
	fpregs->pad = 0;
	if (MACHINE_HAS_VX)
		convert_vx_to_fp((freg_t *)&fpregs->fprs,
				 current->thread.fpu.vxrs);
	else
		memcpy(&fpregs->fprs, current->thread.fpu.fprs,
		       sizeof(fpregs->fprs));
	return 1;
}
EXPORT_SYMBOL(dump_fpu);

unsigned long get_wchan(struct task_struct *p)
{
	struct stack_frame *sf, *low, *high;
	unsigned long return_address;
	int count;

	if (!p || p == current || p->state == TASK_RUNNING || !task_stack_page(p))
		return 0;
	low = task_stack_page(p);
	high = (struct stack_frame *) task_pt_regs(p);
	sf = (struct stack_frame *) (p->thread.ksp & PSW_ADDR_INSN);
	if (sf <= low || sf > high)
		return 0;
	for (count = 0; count < 16; count++) {
		sf = (struct stack_frame *) (sf->back_chain & PSW_ADDR_INSN);
		if (sf <= low || sf > high)
			return 0;
		return_address = sf->gprs[8] & PSW_ADDR_INSN;
		if (!in_sched_functions(return_address))
			return return_address;
	}
	return 0;
}

unsigned long arch_align_stack(unsigned long sp)
{
	if (!(current->personality & ADDR_NO_RANDOMIZE) && randomize_va_space)
		sp -= get_random_int() & ~PAGE_MASK;
	return sp & ~0xf;
}

static inline unsigned long brk_rnd(void)
{
	return (get_random_int() & BRK_RND_MASK) << PAGE_SHIFT;
}

unsigned long arch_randomize_brk(struct mm_struct *mm)
{
	unsigned long ret;

	ret = PAGE_ALIGN(mm->brk + brk_rnd());
	return (ret > mm->brk) ? ret : mm->brk;
}
