/*
 *  linux/arch/arm/kernel/signal.c
 *
 *  Copyright (C) 1995-2002 Russell King
 *  Copyright (C) 1995-2009 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/personality.h>
#include <linux/freezer.h>

#include <asm/elf.h>
#include <asm/cacheflush.h>
#include <asm/ucontext.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>

#include "ptrace.h"
#include "signal.h"

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

/*
 * For ARM syscalls, we encode the syscall number into the instruction.
 */
#define SWI_SYS_SIGRETURN	(0xef000000|(__NR_sigreturn)|(__NR_OABI_SYSCALL_BASE))
#define SWI_SYS_RT_SIGRETURN	(0xef000000|(__NR_rt_sigreturn)|(__NR_OABI_SYSCALL_BASE))

/*
 * With EABI, the syscall number has to be loaded into r7.
 */
#define MOV_R7_NR_SIGRETURN	(0xe3a07000 | (__NR_sigreturn - __NR_SYSCALL_BASE))
#define MOV_R7_NR_RT_SIGRETURN	(0xe3a07000 | (__NR_rt_sigreturn - __NR_SYSCALL_BASE))

/*
 * For Thumb syscalls, we pass the syscall number via r7.  We therefore
 * need two 16-bit instructions.
 */
#define SWI_THUMB_SIGRETURN	(0xdf00 << 16 | 0x2700 | (__NR_sigreturn - __NR_SYSCALL_BASE))
#define SWI_THUMB_RT_SIGRETURN	(0xdf00 << 16 | 0x2700 | (__NR_rt_sigreturn - __NR_SYSCALL_BASE))

const unsigned long sigreturn_codes[7] = {
	MOV_R7_NR_SIGRETURN,    SWI_SYS_SIGRETURN,    SWI_THUMB_SIGRETURN,
	MOV_R7_NR_RT_SIGRETURN, SWI_SYS_RT_SIGRETURN, SWI_THUMB_RT_SIGRETURN,
};

static int do_signal(sigset_t *oldset, struct pt_regs * regs, int syscall);

/*
 * atomically swap in the new signal mask, and wait for a signal.
 */
asmlinkage int sys_sigsuspend(int restart, unsigned long oldmask, old_sigset_t mask, struct pt_regs *regs)
{
	sigset_t saveset;

	mask &= _BLOCKABLE;
	spin_lock_irq(&current->sighand->siglock);
	saveset = current->blocked;
	siginitset(&current->blocked, mask);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);
	regs->ARM_r0 = -EINTR;

	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(&saveset, regs, 0))
			return regs->ARM_r0;
	}
}

asmlinkage int
sys_rt_sigsuspend(sigset_t __user *unewset, size_t sigsetsize, struct pt_regs *regs)
{
	sigset_t saveset, newset;

	/* XXX: Don't preclude handling different sized sigset_t's. */
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	if (copy_from_user(&newset, unewset, sizeof(newset)))
		return -EFAULT;
	sigdelsetmask(&newset, ~_BLOCKABLE);

	spin_lock_irq(&current->sighand->siglock);
	saveset = current->blocked;
	current->blocked = newset;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);
	regs->ARM_r0 = -EINTR;

	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(&saveset, regs, 0))
			return regs->ARM_r0;
	}
}

asmlinkage int 
sys_sigaction(int sig, const struct old_sigaction __user *act,
	      struct old_sigaction __user *oact)
{
	struct k_sigaction new_ka, old_ka;
	int ret;

	if (act) {
		old_sigset_t mask;
		if (!access_ok(VERIFY_READ, act, sizeof(*act)) ||
		    __get_user(new_ka.sa.sa_handler, &act->sa_handler) ||
		    __get_user(new_ka.sa.sa_restorer, &act->sa_restorer))
			return -EFAULT;
		__get_user(new_ka.sa.sa_flags, &act->sa_flags);
		__get_user(mask, &act->sa_mask);
		siginitset(&new_ka.sa.sa_mask, mask);
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		if (!access_ok(VERIFY_WRITE, oact, sizeof(*oact)) ||
		    __put_user(old_ka.sa.sa_handler, &oact->sa_handler) ||
		    __put_user(old_ka.sa.sa_restorer, &oact->sa_restorer))
			return -EFAULT;
		__put_user(old_ka.sa.sa_flags, &oact->sa_flags);
		__put_user(old_ka.sa.sa_mask.sig[0], &oact->sa_mask);
	}

	return ret;
}

#ifdef CONFIG_CRUNCH
static int preserve_crunch_context(struct crunch_sigframe *frame)
#include <linux/random.h>
#include <linux/signal.h>
#include <linux/personality.h>
#include <linux/uaccess.h>
#include <linux/tracehook.h>
#include <linux/uprobes.h>

#include <asm/elf.h>
#include <asm/cacheflush.h>
#include <asm/traps.h>
#include <asm/ucontext.h>
#include <asm/unistd.h>
#include <asm/vfp.h>

extern const unsigned long sigreturn_codes[7];

static unsigned long signal_return_offset;

#ifdef CONFIG_CRUNCH
static int preserve_crunch_context(struct crunch_sigframe __user *frame)
{
	char kbuf[sizeof(*frame) + 8];
	struct crunch_sigframe *kframe;

	/* the crunch context must be 64 bit aligned */
	kframe = (struct crunch_sigframe *)((unsigned long)(kbuf + 8) & ~7);
	kframe->magic = CRUNCH_MAGIC;
	kframe->size = CRUNCH_STORAGE_SIZE;
	crunch_task_copy(current_thread_info(), &kframe->storage);
	return __copy_to_user(frame, kframe, sizeof(*frame));
}

static int restore_crunch_context(struct crunch_sigframe *frame)
static int restore_crunch_context(struct crunch_sigframe __user *frame)
{
	char kbuf[sizeof(*frame) + 8];
	struct crunch_sigframe *kframe;

	/* the crunch context must be 64 bit aligned */
	kframe = (struct crunch_sigframe *)((unsigned long)(kbuf + 8) & ~7);
	if (__copy_from_user(kframe, frame, sizeof(*frame)))
		return -1;
	if (kframe->magic != CRUNCH_MAGIC ||
	    kframe->size != CRUNCH_STORAGE_SIZE)
		return -1;
	crunch_task_restore(current_thread_info(), &kframe->storage);
	return 0;
}
#endif

#ifdef CONFIG_IWMMXT

static int preserve_iwmmxt_context(struct iwmmxt_sigframe *frame)
{
	char kbuf[sizeof(*frame) + 8];
	struct iwmmxt_sigframe *kframe;

	/* the iWMMXt context must be 64 bit aligned */
	kframe = (struct iwmmxt_sigframe *)((unsigned long)(kbuf + 8) & ~7);
	kframe->magic = IWMMXT_MAGIC;
	kframe->size = IWMMXT_STORAGE_SIZE;
	iwmmxt_task_copy(current_thread_info(), &kframe->storage);
	return __copy_to_user(frame, kframe, sizeof(*frame));
}

static int restore_iwmmxt_context(struct iwmmxt_sigframe *frame)
{
	char kbuf[sizeof(*frame) + 8];
	struct iwmmxt_sigframe *kframe;

	/* the iWMMXt context must be 64 bit aligned */
	kframe = (struct iwmmxt_sigframe *)((unsigned long)(kbuf + 8) & ~7);
	if (__copy_from_user(kframe, frame, sizeof(*frame)))
		return -1;
	if (kframe->magic != IWMMXT_MAGIC ||
	    kframe->size != IWMMXT_STORAGE_SIZE)
		return -1;
	iwmmxt_task_restore(current_thread_info(), &kframe->storage);
	return 0;
}

#endif

#ifdef CONFIG_VFP

static int preserve_vfp_context(struct vfp_sigframe __user *frame)
{
	struct vfp_sigframe kframe;
	int err = 0;

	memset(&kframe, 0, sizeof(kframe));
	kframe.magic = VFP_MAGIC;
	kframe.size = VFP_STORAGE_SIZE;

	err = vfp_preserve_user_clear_hwstate(&kframe.ufp, &kframe.ufp_exc);
	if (err)
		return err;

	return __copy_to_user(frame, &kframe, sizeof(kframe));
}

static int restore_vfp_context(struct vfp_sigframe __user *auxp)
{
	struct vfp_sigframe frame;
	int err;

	err = __copy_from_user(&frame, (char __user *) auxp, sizeof(frame));

	if (err)
		return err;

	if (frame.magic != VFP_MAGIC || frame.size != VFP_STORAGE_SIZE)
		return -EINVAL;

	return vfp_restore_user_hwstate(&frame.ufp, &frame.ufp_exc);
}

#endif

/*
 * Do a signal return; undo the signal stack.  These are aligned to 64-bit.
 */
struct sigframe {
	struct ucontext uc;
	unsigned long retcode[2];
};

struct rt_sigframe {
	struct siginfo info;
	struct sigframe sig;
};

static int restore_sigframe(struct pt_regs *regs, struct sigframe __user *sf)
{
	struct sigcontext context;
	struct aux_sigframe __user *aux;
	sigset_t set;
	int err;

	err = __copy_from_user(&set, &sf->uc.uc_sigmask, sizeof(set));
	if (err == 0) {
		sigdelsetmask(&set, ~_BLOCKABLE);
		spin_lock_irq(&current->sighand->siglock);
		current->blocked = set;
		recalc_sigpending();
		spin_unlock_irq(&current->sighand->siglock);
	}
	if (err == 0)
		set_current_blocked(&set);

	err |= __copy_from_user(&context, &sf->uc.uc_mcontext, sizeof(context));
	if (err == 0) {
		regs->ARM_r0 = context.arm_r0;
		regs->ARM_r1 = context.arm_r1;
		regs->ARM_r2 = context.arm_r2;
		regs->ARM_r3 = context.arm_r3;
		regs->ARM_r4 = context.arm_r4;
		regs->ARM_r5 = context.arm_r5;
		regs->ARM_r6 = context.arm_r6;
		regs->ARM_r7 = context.arm_r7;
		regs->ARM_r8 = context.arm_r8;
		regs->ARM_r9 = context.arm_r9;
		regs->ARM_r10 = context.arm_r10;
		regs->ARM_fp = context.arm_fp;
		regs->ARM_ip = context.arm_ip;
		regs->ARM_sp = context.arm_sp;
		regs->ARM_lr = context.arm_lr;
		regs->ARM_pc = context.arm_pc;
		regs->ARM_cpsr = context.arm_cpsr;
	}

	err |= !valid_user_regs(regs);

	aux = (struct aux_sigframe __user *) sf->uc.uc_regspace;
#ifdef CONFIG_CRUNCH
	if (err == 0)
		err |= restore_crunch_context(&aux->crunch);
#endif
#ifdef CONFIG_IWMMXT
	if (err == 0 && test_thread_flag(TIF_USING_IWMMXT))
		err |= restore_iwmmxt_context(&aux->iwmmxt);
#endif
#ifdef CONFIG_VFP
//	if (err == 0)
//		err |= vfp_restore_state(&sf->aux.vfp);
	if (err == 0)
		err |= restore_vfp_context(&aux->vfp);
#endif

	return err;
}

asmlinkage int sys_sigreturn(struct pt_regs *regs)
{
	struct sigframe __user *frame;

	/* Always make any pending restarted system calls return -EINTR */
	current_thread_info()->restart_block.fn = do_no_restart_syscall;
	current->restart_block.fn = do_no_restart_syscall;

	/*
	 * Since we stacked the signal on a 64-bit boundary,
	 * then 'sp' should be word aligned here.  If it's
	 * not, then the user is trying to mess with us.
	 */
	if (regs->ARM_sp & 7)
		goto badframe;

	frame = (struct sigframe __user *)regs->ARM_sp;

	if (!access_ok(VERIFY_READ, frame, sizeof (*frame)))
		goto badframe;

	if (restore_sigframe(regs, frame))
		goto badframe;

	single_step_trap(current);

	return regs->ARM_r0;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

asmlinkage int sys_rt_sigreturn(struct pt_regs *regs)
{
	struct rt_sigframe __user *frame;

	/* Always make any pending restarted system calls return -EINTR */
	current_thread_info()->restart_block.fn = do_no_restart_syscall;
	current->restart_block.fn = do_no_restart_syscall;

	/*
	 * Since we stacked the signal on a 64-bit boundary,
	 * then 'sp' should be word aligned here.  If it's
	 * not, then the user is trying to mess with us.
	 */
	if (regs->ARM_sp & 7)
		goto badframe;

	frame = (struct rt_sigframe __user *)regs->ARM_sp;

	if (!access_ok(VERIFY_READ, frame, sizeof (*frame)))
		goto badframe;

	if (restore_sigframe(regs, &frame->sig))
		goto badframe;

	if (do_sigaltstack(&frame->sig.uc.uc_stack, NULL, regs->ARM_sp) == -EFAULT)
		goto badframe;

	single_step_trap(current);

	if (restore_altstack(&frame->sig.uc.uc_stack))
		goto badframe;

	return regs->ARM_r0;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

static int
setup_sigframe(struct sigframe __user *sf, struct pt_regs *regs, sigset_t *set)
{
	struct aux_sigframe __user *aux;
	struct sigcontext context;
	int err = 0;

	context = (struct sigcontext) {
		.arm_r0        = regs->ARM_r0,
		.arm_r1        = regs->ARM_r1,
		.arm_r2        = regs->ARM_r2,
		.arm_r3        = regs->ARM_r3,
		.arm_r4        = regs->ARM_r4,
		.arm_r5        = regs->ARM_r5,
		.arm_r6        = regs->ARM_r6,
		.arm_r7        = regs->ARM_r7,
		.arm_r8        = regs->ARM_r8,
		.arm_r9        = regs->ARM_r9,
		.arm_r10       = regs->ARM_r10,
		.arm_fp        = regs->ARM_fp,
		.arm_ip        = regs->ARM_ip,
		.arm_sp        = regs->ARM_sp,
		.arm_lr        = regs->ARM_lr,
		.arm_pc        = regs->ARM_pc,
		.arm_cpsr      = regs->ARM_cpsr,

		.trap_no       = current->thread.trap_no,
		.error_code    = current->thread.error_code,
		.fault_address = current->thread.address,
		.oldmask       = set->sig[0],
	};

	err |= __copy_to_user(&sf->uc.uc_mcontext, &context, sizeof(context));

	err |= __copy_to_user(&sf->uc.uc_sigmask, set, sizeof(*set));

	aux = (struct aux_sigframe __user *) sf->uc.uc_regspace;
#ifdef CONFIG_CRUNCH
	if (err == 0)
		err |= preserve_crunch_context(&aux->crunch);
#endif
#ifdef CONFIG_IWMMXT
	if (err == 0 && test_thread_flag(TIF_USING_IWMMXT))
		err |= preserve_iwmmxt_context(&aux->iwmmxt);
#endif
#ifdef CONFIG_VFP
//	if (err == 0)
//		err |= vfp_save_state(&sf->aux.vfp);
	if (err == 0)
		err |= preserve_vfp_context(&aux->vfp);
#endif
	err |= __put_user(0, &aux->end_magic);

	return err;
}

static inline void __user *
get_sigframe(struct k_sigaction *ka, struct pt_regs *regs, int framesize)
{
	unsigned long sp = regs->ARM_sp;
	void __user *frame;

	/*
	 * This is the X/Open sanctioned signal stack switching.
	 */
	if ((ka->sa.sa_flags & SA_ONSTACK) && !sas_ss_flags(sp))
		sp = current->sas_ss_sp + current->sas_ss_size;

	/*
get_sigframe(struct ksignal *ksig, struct pt_regs *regs, int framesize)
{
	unsigned long sp = sigsp(regs->ARM_sp, ksig);
	void __user *frame;

	/*
	 * ATPCS B01 mandates 8-byte alignment
	 */
	frame = (void __user *)((sp - framesize) & ~7);

	/*
	 * Check that we can actually write to the signal frame.
	 */
	if (!access_ok(VERIFY_WRITE, frame, framesize))
		frame = NULL;

	return frame;
}

static int
setup_return(struct pt_regs *regs, struct k_sigaction *ka,
	     unsigned long __user *rc, void __user *frame, int usig)
{
	unsigned long handler = (unsigned long)ka->sa.sa_handler;
	unsigned long retcode;
	int thumb = 0;
	unsigned long cpsr = regs->ARM_cpsr & ~PSR_f;
setup_return(struct pt_regs *regs, struct ksignal *ksig,
	     unsigned long __user *rc, void __user *frame)
{
	unsigned long handler = (unsigned long)ksig->ka.sa.sa_handler;
	unsigned long retcode;
	int thumb = 0;
	unsigned long cpsr = regs->ARM_cpsr & ~(PSR_f | PSR_E_BIT);

	cpsr |= PSR_ENDSTATE;

	/*
	 * Maybe we need to deliver a 32-bit signal to a 26-bit task.
	 */
	if (ka->sa.sa_flags & SA_THIRTYTWO)
	if (ksig->ka.sa.sa_flags & SA_THIRTYTWO)
		cpsr = (cpsr & ~MODE_MASK) | USR_MODE;

#ifdef CONFIG_ARM_THUMB
	if (elf_hwcap & HWCAP_THUMB) {
		/*
		 * The LSB of the handler determines if we're going to
		 * be using THUMB or ARM mode for this signal handler.
		 */
		thumb = handler & 1;

		if (thumb)
			cpsr |= PSR_T_BIT;
		else
		/*
		 * Clear the If-Then Thumb-2 execution state.  ARM spec
		 * requires this to be all 000s in ARM mode.  Snapdragon
		 * S4/Krait misbehaves on a Thumb=>ARM signal transition
		 * without this.
		 *
		 * We must do this whenever we are running on a Thumb-2
		 * capable CPU, which includes ARMv6T2.  However, we elect
		 * to always do this to simplify the code; this field is
		 * marked UNK/SBZP for older architectures.
		 */
		cpsr &= ~PSR_IT_MASK;

		if (thumb) {
			cpsr |= PSR_T_BIT;
		} else
			cpsr &= ~PSR_T_BIT;
	}
#endif

	if (ka->sa.sa_flags & SA_RESTORER) {
		retcode = (unsigned long)ka->sa.sa_restorer;
	} else {
		unsigned int idx = thumb << 1;

		if (ka->sa.sa_flags & SA_SIGINFO)
			idx += 3;

	if (ksig->ka.sa.sa_flags & SA_RESTORER) {
		retcode = (unsigned long)ksig->ka.sa.sa_restorer;
	} else {
		unsigned int idx = thumb << 1;

		if (ksig->ka.sa.sa_flags & SA_SIGINFO)
			idx += 3;

		/*
		 * Put the sigreturn code on the stack no matter which return
		 * mechanism we use in order to remain ABI compliant
		 */
		if (__put_user(sigreturn_codes[idx],   rc) ||
		    __put_user(sigreturn_codes[idx+1], rc+1))
			return 1;

		if (cpsr & MODE32_BIT) {
			/*
			 * 32-bit code can use the new high-page
			 * signal return code support.
			 */
			retcode = KERN_SIGRETURN_CODE + (idx << 2) + thumb;
		} else {
#ifdef CONFIG_MMU
		if (cpsr & MODE32_BIT) {
			struct mm_struct *mm = current->mm;

			/*
			 * 32-bit code can use the signal return page
			 * except when the MPU has protected the vectors
			 * page from PL0
			 */
			retcode = mm->context.sigpage + signal_return_offset +
				  (idx << 2) + thumb;
		} else
#endif
		{
			/*
			 * Ensure that the instruction cache sees
			 * the return code written onto the stack.
			 */
			flush_icache_range((unsigned long)rc,
					   (unsigned long)(rc + 2));

			retcode = ((unsigned long)rc) + thumb;
		}
	}

	regs->ARM_r0 = usig;
	regs->ARM_r0 = ksig->sig;
	regs->ARM_sp = (unsigned long)frame;
	regs->ARM_lr = retcode;
	regs->ARM_pc = handler;
	regs->ARM_cpsr = cpsr;

	return 0;
}

static int
setup_frame(int usig, struct k_sigaction *ka, sigset_t *set, struct pt_regs *regs)
{
	struct sigframe __user *frame = get_sigframe(ka, regs, sizeof(*frame));
setup_frame(struct ksignal *ksig, sigset_t *set, struct pt_regs *regs)
{
	struct sigframe __user *frame = get_sigframe(ksig, regs, sizeof(*frame));
	int err = 0;

	if (!frame)
		return 1;

	/*
	 * Set uc.uc_flags to a value which sc.trap_no would never have.
	 */
	err = __put_user(0x5ac3c35a, &frame->uc.uc_flags);

	err |= setup_sigframe(frame, regs, set);
	if (err == 0)
		err = setup_return(regs, ka, frame->retcode, frame, usig);
		err = setup_return(regs, ksig, frame->retcode, frame);

	return err;
}

static int
setup_rt_frame(int usig, struct k_sigaction *ka, siginfo_t *info,
	       sigset_t *set, struct pt_regs *regs)
{
	struct rt_sigframe __user *frame = get_sigframe(ka, regs, sizeof(*frame));
	stack_t stack;
setup_rt_frame(struct ksignal *ksig, sigset_t *set, struct pt_regs *regs)
{
	struct rt_sigframe __user *frame = get_sigframe(ksig, regs, sizeof(*frame));
	int err = 0;

	if (!frame)
		return 1;

	err |= copy_siginfo_to_user(&frame->info, info);
	err |= copy_siginfo_to_user(&frame->info, &ksig->info);

	err |= __put_user(0, &frame->sig.uc.uc_flags);
	err |= __put_user(NULL, &frame->sig.uc.uc_link);

	memset(&stack, 0, sizeof(stack));
	stack.ss_sp = (void __user *)current->sas_ss_sp;
	stack.ss_flags = sas_ss_flags(regs->ARM_sp);
	stack.ss_size = current->sas_ss_size;
	err |= __copy_to_user(&frame->sig.uc.uc_stack, &stack, sizeof(stack));

	err |= setup_sigframe(&frame->sig, regs, set);
	if (err == 0)
		err = setup_return(regs, ka, frame->sig.retcode, frame, usig);
	err |= __save_altstack(&frame->sig.uc.uc_stack, regs->ARM_sp);
	err |= setup_sigframe(&frame->sig, regs, set);
	if (err == 0)
		err = setup_return(regs, ksig, frame->sig.retcode, frame);

	if (err == 0) {
		/*
		 * For realtime signals we must also set the second and third
		 * arguments for the signal handler.
		 *   -- Peter Maydell <pmaydell@chiark.greenend.org.uk> 2000-12-06
		 */
		regs->ARM_r1 = (unsigned long)&frame->info;
		regs->ARM_r2 = (unsigned long)&frame->sig.uc;
	}

	return err;
}

static inline void restart_syscall(struct pt_regs *regs)
{
	regs->ARM_r0 = regs->ARM_ORIG_r0;
	regs->ARM_pc -= thumb_mode(regs) ? 2 : 4;
}

/*
 * OK, we're invoking a handler
 */	
static void
handle_signal(unsigned long sig, struct k_sigaction *ka,
	      siginfo_t *info, sigset_t *oldset,
	      struct pt_regs * regs, int syscall)
{
	struct thread_info *thread = current_thread_info();
	struct task_struct *tsk = current;
	int usig = sig;
	int ret;

	/*
	 * If we were from a system call, check for system call restarting...
	 */
	if (syscall) {
		switch (regs->ARM_r0) {
		case -ERESTART_RESTARTBLOCK:
		case -ERESTARTNOHAND:
			regs->ARM_r0 = -EINTR;
			break;
		case -ERESTARTSYS:
			if (!(ka->sa.sa_flags & SA_RESTART)) {
				regs->ARM_r0 = -EINTR;
				break;
			}
			/* fallthrough */
		case -ERESTARTNOINTR:
			restart_syscall(regs);
		}
	}

	/*
	 * translate the signal
	 */
	if (usig < 32 && thread->exec_domain && thread->exec_domain->signal_invmap)
		usig = thread->exec_domain->signal_invmap[usig];

	/*
	 * Set up the stack frame
	 */
	if (ka->sa.sa_flags & SA_SIGINFO)
		ret = setup_rt_frame(usig, ka, info, oldset, regs);
	else
		ret = setup_frame(usig, ka, oldset, regs);
/*
 * OK, we're invoking a handler
 */	
static void handle_signal(struct ksignal *ksig, struct pt_regs *regs)
{
	sigset_t *oldset = sigmask_to_save();
	int ret;

	/*
	 * Set up the stack frame
	 */
	if (ksig->ka.sa.sa_flags & SA_SIGINFO)
		ret = setup_rt_frame(ksig, oldset, regs);
	else
		ret = setup_frame(ksig, oldset, regs);

	/*
	 * Check that the resulting registers are actually sane.
	 */
	ret |= !valid_user_regs(regs);

	if (ret != 0) {
		force_sigsegv(sig, tsk);
		return;
	}

	/*
	 * Block the signal if we were successful.
	 */
	spin_lock_irq(&tsk->sighand->siglock);
	sigorsets(&tsk->blocked, &tsk->blocked,
		  &ka->sa.sa_mask);
	if (!(ka->sa.sa_flags & SA_NODEFER))
		sigaddset(&tsk->blocked, sig);
	recalc_sigpending();
	spin_unlock_irq(&tsk->sighand->siglock);

	signal_setup_done(ret, ksig, 0);
}

/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 *
 * Note that we go through the signals twice: once to check the signals that
 * the kernel can handle, and then we build all the user-level signal handling
 * stack-frames in one go after that.
 */
static int do_signal(sigset_t *oldset, struct pt_regs *regs, int syscall)
{
	struct k_sigaction ka;
	siginfo_t info;
	int signr;

	/*
	 * We want the common case to go fast, which
	 * is why we may in certain cases get here from
	 * kernel mode. Just return without doing anything
	 * if so.
	 */
	if (!user_mode(regs))
		return 0;

	if (try_to_freeze())
		goto no_signal;

	single_step_clear(current);

	signr = get_signal_to_deliver(&info, &ka, regs, NULL);
	if (signr > 0) {
		handle_signal(signr, &ka, &info, oldset, regs, syscall);
		single_step_set(current);
		return 1;
	}

 no_signal:
	/*
	 * No signal to deliver to the process - restart the syscall.
	 */
	if (syscall) {
		if (regs->ARM_r0 == -ERESTART_RESTARTBLOCK) {
			if (thumb_mode(regs)) {
				regs->ARM_r7 = __NR_restart_syscall - __NR_SYSCALL_BASE;
				regs->ARM_pc -= 2;
			} else {
#if defined(CONFIG_AEABI) && !defined(CONFIG_OABI_COMPAT)
				regs->ARM_r7 = __NR_restart_syscall;
				regs->ARM_pc -= 4;
#else
				u32 __user *usp;
				u32 swival = __NR_restart_syscall;

				regs->ARM_sp -= 12;
				usp = (u32 __user *)regs->ARM_sp;

				/*
				 * Either we supports OABI only, or we have
				 * EABI with the OABI compat layer enabled.
				 * In the later case we don't know if user
				 * space is EABI or not, and if not we must
				 * not clobber r7.  Always using the OABI
				 * syscall solves that issue and works for
				 * all those cases.
				 */
				swival = swival - __NR_SYSCALL_BASE + __NR_OABI_SYSCALL_BASE;

				put_user(regs->ARM_pc, &usp[0]);
				/* swi __NR_restart_syscall */
				put_user(0xef000000 | swival, &usp[1]);
				/* ldr	pc, [sp], #12 */
				put_user(0xe49df00c, &usp[2]);

				flush_icache_range((unsigned long)usp,
						   (unsigned long)(usp + 3));

				regs->ARM_pc = regs->ARM_sp + 4;
#endif
			}
		}
		if (regs->ARM_r0 == -ERESTARTNOHAND ||
		    regs->ARM_r0 == -ERESTARTSYS ||
		    regs->ARM_r0 == -ERESTARTNOINTR) {
			restart_syscall(regs);
		}
	}
	single_step_set(current);
	return 0;
}

asmlinkage void
do_notify_resume(struct pt_regs *regs, unsigned int thread_flags, int syscall)
{
	if (thread_flags & _TIF_SIGPENDING)
		do_signal(&current->blocked, regs, syscall);
static int do_signal(struct pt_regs *regs, int syscall)
{
	unsigned int retval = 0, continue_addr = 0, restart_addr = 0;
	struct ksignal ksig;
	int restart = 0;

	/*
	 * If we were from a system call, check for system call restarting...
	 */
	if (syscall) {
		continue_addr = regs->ARM_pc;
		restart_addr = continue_addr - (thumb_mode(regs) ? 2 : 4);
		retval = regs->ARM_r0;

		/*
		 * Prepare for system call restart.  We do this here so that a
		 * debugger will see the already changed PSW.
		 */
		switch (retval) {
		case -ERESTART_RESTARTBLOCK:
			restart -= 2;
		case -ERESTARTNOHAND:
		case -ERESTARTSYS:
		case -ERESTARTNOINTR:
			restart++;
			regs->ARM_r0 = regs->ARM_ORIG_r0;
			regs->ARM_pc = restart_addr;
			break;
		}
	}

	/*
	 * Get the signal to deliver.  When running under ptrace, at this
	 * point the debugger may change all our registers ...
	 */
	/*
	 * Depending on the signal settings we may need to revert the
	 * decision to restart the system call.  But skip this if a
	 * debugger has chosen to restart at a different PC.
	 */
	if (get_signal(&ksig)) {
		/* handler */
		if (unlikely(restart) && regs->ARM_pc == restart_addr) {
			if (retval == -ERESTARTNOHAND ||
			    retval == -ERESTART_RESTARTBLOCK
			    || (retval == -ERESTARTSYS
				&& !(ksig.ka.sa.sa_flags & SA_RESTART))) {
				regs->ARM_r0 = -EINTR;
				regs->ARM_pc = continue_addr;
			}
		}
		handle_signal(&ksig, regs);
	} else {
		/* no handler */
		restore_saved_sigmask();
		if (unlikely(restart) && regs->ARM_pc == restart_addr) {
			regs->ARM_pc = continue_addr;
			return restart;
		}
	}
	return 0;
}

asmlinkage int
do_work_pending(struct pt_regs *regs, unsigned int thread_flags, int syscall)
{
	/*
	 * The assembly code enters us with IRQs off, but it hasn't
	 * informed the tracing code of that for efficiency reasons.
	 * Update the trace code with the current status.
	 */
	trace_hardirqs_off();
	do {
		if (likely(thread_flags & (_TIF_NEED_RESCHED |
					   _TIF_NEED_RESCHED_LAZY))) {
			schedule();
		} else {
			if (unlikely(!user_mode(regs)))
				return 0;
			local_irq_enable();
			if (thread_flags & _TIF_SIGPENDING) {
				int restart = do_signal(regs, syscall);
				if (unlikely(restart)) {
					/*
					 * Restart without handlers.
					 * Deal with it without leaving
					 * the kernel space.
					 */
					return restart;
				}
				syscall = 0;
			} else if (thread_flags & _TIF_UPROBE) {
				uprobe_notify_resume(regs);
			} else {
				clear_thread_flag(TIF_NOTIFY_RESUME);
				tracehook_notify_resume(regs);
			}
		}
		local_irq_disable();
		thread_flags = current_thread_info()->flags;
	} while (thread_flags & _TIF_WORK_MASK);
	return 0;
}

struct page *get_signal_page(void)
{
	unsigned long ptr;
	unsigned offset;
	struct page *page;
	void *addr;

	page = alloc_pages(GFP_KERNEL, 0);

	if (!page)
		return NULL;

	addr = page_address(page);

	/* Poison the entire page */
	memset32(addr, __opcode_to_mem_arm(0xe7fddef1),
		 PAGE_SIZE / sizeof(u32));

	/* Give the signal return code some randomness */
	offset = 0x200 + (get_random_int() & 0x7fc);
	signal_return_offset = offset;

	/* Copy signal return handlers into the page */
	memcpy(addr + offset, sigreturn_codes, sizeof(sigreturn_codes));

	/* Flush out all instructions in this page */
	ptr = (unsigned long)addr;
	flush_icache_range(ptr, ptr + PAGE_SIZE);

	return page;
}
