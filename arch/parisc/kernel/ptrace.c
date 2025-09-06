/*
 * Kernel support for the ptrace() and syscall tracing interfaces.
 *
 * Copyright (C) 2000 Hewlett-Packard Co, Linuxcare Inc.
 * Copyright (C) 2000 Matthew Wilcox <matthew@wil.cx>
 * Copyright (C) 2000 David Huggins-Daines <dhd@debian.org>
 * Copyright (C) 2008 Helge Deller <deller@gmx.de>
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/personality.h>
#include <linux/security.h>
#include <linux/compat.h>
#include <linux/signal.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <linux/tracehook.h>
#include <linux/user.h>
#include <linux/personality.h>
#include <linux/security.h>
#include <linux/seccomp.h>
#include <linux/compat.h>
#include <linux/signal.h>
#include <linux/audit.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/asm-offsets.h>

/* PSW bits we allow the debugger to modify */
#define USER_PSW_BITS	(PSW_N | PSW_V | PSW_CB)

#undef DEBUG_PTRACE

#ifdef DEBUG_PTRACE
#define DBG(x...)	printk(x)
#else
#define DBG(x...)
#endif

#ifdef CONFIG_64BIT
#define USER_PSW_BITS	(PSW_N | PSW_B | PSW_V | PSW_CB)

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Make sure single step bits etc are not set.
 */
void ptrace_disable(struct task_struct *task)
{
	clear_tsk_thread_flag(task, TIF_SINGLESTEP);
	clear_tsk_thread_flag(task, TIF_BLOCKSTEP);

	/* make sure the trap bits are not set */
	pa_psw(task)->r = 0;
	pa_psw(task)->t = 0;
	pa_psw(task)->h = 0;
	pa_psw(task)->l = 0;
}

/*
 * The following functions are called by ptrace_resume() when
 * enabling or disabling single/block tracing.
 */
void user_disable_single_step(struct task_struct *task)
{
	ptrace_disable(task);
}

void user_enable_single_step(struct task_struct *task)
{
	clear_tsk_thread_flag(task, TIF_BLOCKSTEP);
	set_tsk_thread_flag(task, TIF_SINGLESTEP);

	if (pa_psw(task)->n) {
		struct siginfo si;

		/* Nullified, just crank over the queue. */
		task_regs(task)->iaoq[0] = task_regs(task)->iaoq[1];
		task_regs(task)->iasq[0] = task_regs(task)->iasq[1];
		task_regs(task)->iaoq[1] = task_regs(task)->iaoq[0] + 4;
		pa_psw(task)->n = 0;
		pa_psw(task)->x = 0;
		pa_psw(task)->y = 0;
		pa_psw(task)->z = 0;
		pa_psw(task)->b = 0;
		ptrace_disable(task);
		/* Don't wake up the task, but let the
		   parent know something happened. */
		si.si_code = TRAP_TRACE;
		si.si_addr = (void __user *) (task_regs(task)->iaoq[0] & ~3);
		si.si_signo = SIGTRAP;
		si.si_errno = 0;
		force_sig_info(SIGTRAP, &si, task);
		/* notify_parent(task, SIGCHLD); */
		return;
	}

	/* Enable recovery counter traps.  The recovery counter
	 * itself will be set to zero on a task switch.  If the
	 * task is suspended on a syscall then the syscall return
	 * path will overwrite the recovery counter with a suitable
	 * value such that it traps once back in user space.  We
	 * disable interrupts in the tasks PSW here also, to avoid
	 * interrupts while the recovery counter is decrementing.
	 */
	pa_psw(task)->r = 1;
	pa_psw(task)->t = 0;
	pa_psw(task)->h = 0;
	pa_psw(task)->l = 0;
}

void user_enable_block_step(struct task_struct *task)
{
	clear_tsk_thread_flag(task, TIF_SINGLESTEP);
	set_tsk_thread_flag(task, TIF_BLOCKSTEP);

	/* Enable taken branch trap. */
	pa_psw(task)->r = 0;
	pa_psw(task)->t = 1;
	pa_psw(task)->h = 0;
	pa_psw(task)->l = 0;
}

long arch_ptrace(struct task_struct *child, long request,
		 unsigned long addr, unsigned long data)
{
	unsigned long tmp;
	long ret = -EIO;

	switch (request) {

	/* Read the word at location addr in the USER area.  For ptraced
	   processes, the kernel saves all regs on a syscall. */
	case PTRACE_PEEKUSR:
		if ((addr & (sizeof(unsigned long)-1)) ||
		     addr >= sizeof(struct pt_regs))
			break;
		tmp = *(unsigned long *) ((char *) task_regs(child) + addr);
		ret = put_user(tmp, (unsigned long __user *) data);
		break;

	/* Write the word at location addr in the USER area.  This will need
	   to change when the kernel no longer saves all regs on a syscall.
	   FIXME.  There is a problem at the moment in that r3-r18 are only
	   saved if the process is ptraced on syscall entry, and even then
	   those values are overwritten by actual register values on syscall
	   exit. */
	case PTRACE_POKEUSR:
		/* Some register values written here may be ignored in
		 * entry.S:syscall_restore_rfi; e.g. iaoq is written with
		 * r31/r31+4, and not with the values in pt_regs.
		 */
		if (addr == PT_PSW) {
			/* Allow writing to Nullify, Divide-step-correction,
			 * and carry/borrow bits.
			 * BEWARE, if you set N, and then single step, it won't
			 * stop on the nullified instruction.
			 */
			data &= USER_PSW_BITS;
			task_regs(child)->gr[0] &= ~USER_PSW_BITS;
			task_regs(child)->gr[0] |= data;
			ret = 0;
			break;
		}

		if ((addr & (sizeof(unsigned long)-1)) ||
		     addr >= sizeof(struct pt_regs))
			break;
		if (addr == PT_IAOQ0 || addr == PT_IAOQ1) {
			data |= 3; /* ensure userspace privilege */
		}
		if ((addr >= PT_GR1 && addr <= PT_GR31) ||
				addr == PT_IAOQ0 || addr == PT_IAOQ1 ||
				(addr >= PT_FR0 && addr <= PT_FR31 + 4) ||
				addr == PT_SAR) {
			*(unsigned long *) ((char *) task_regs(child) + addr) = data;
			ret = 0;
		}
		break;

	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}

	return ret;
}


#ifdef CONFIG_COMPAT

/* This function is needed to translate 32 bit pt_regs offsets in to
 * 64 bit pt_regs offsets.  For example, a 32 bit gdb under a 64 bit kernel
 * will request offset 12 if it wants gr3, but the lower 32 bits of
 * the 64 bit kernels view of gr3 will be at offset 28 (3*8 + 4).
 * This code relies on a 32 bit pt_regs being comprised of 32 bit values
 * except for the fp registers which (a) are 64 bits, and (b) follow
 * the gr registers at the start of pt_regs.  The 32 bit pt_regs should
 * be half the size of the 64 bit pt_regs, plus 32*4 to allow for fr[]
 * being 64 bit in both cases.
 */

static long translate_usr_offset(long offset)
{
	if (offset < 0)
		return -1;
static compat_ulong_t translate_usr_offset(compat_ulong_t offset)
{
	compat_ulong_t pos;

	if (offset < 32*4)	/* gr[0..31] */
		pos = offset * 2 + 4;
	else if (offset < 32*4+32*8)	/* fr[0] ... fr[31] */
		pos = (offset - 32*4) + PT_FR0;
	else if (offset < sizeof(struct pt_regs)/2 + 32*4) /* sr[0] ... ipsw */
		pos = (offset - 32*4 - 32*8) * 2 + PT_SR0 + 4;
	else
		return -1;
}
#endif

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Make sure single step bits etc are not set.
 */
void ptrace_disable(struct task_struct *child)
{
	/* make sure the trap bits are not set */
	pa_psw(child)->r = 0;
	pa_psw(child)->t = 0;
	pa_psw(child)->h = 0;
	pa_psw(child)->l = 0;
}

long arch_ptrace(struct task_struct *child, long request, long addr, long data)
{
	long ret;
#ifdef DEBUG_PTRACE
	long oaddr=addr, odata=data;
#endif

	switch (request) {
	case PTRACE_PEEKTEXT: /* read word at location addr. */ 
	case PTRACE_PEEKDATA: {
#ifdef CONFIG_64BIT
		if (__is_compat_task(child)) {
			int copied;
			unsigned int tmp;

			addr &= 0xffffffffL;
			copied = access_process_vm(child, addr, &tmp, sizeof(tmp), 0);
			ret = -EIO;
			if (copied != sizeof(tmp))
				goto out_tsk;
			ret = put_user(tmp,(unsigned int *) data);
			DBG("sys_ptrace(PEEK%s, %d, %lx, %lx) returning %ld, data %x\n",
				request == PTRACE_PEEKTEXT ? "TEXT" : "DATA",
				pid, oaddr, odata, ret, tmp);
		}
		else
#endif
			ret = generic_ptrace_peekdata(child, addr, data);
		goto out_tsk;
	}

	/* when I and D space are separate, this will have to be fixed. */
	case PTRACE_POKETEXT: /* write the word at location addr. */
	case PTRACE_POKEDATA:
		ret = 0;
#ifdef CONFIG_64BIT
		if (__is_compat_task(child)) {
			unsigned int tmp = (unsigned int)data;
			DBG("sys_ptrace(POKE%s, %d, %lx, %lx)\n",
				request == PTRACE_POKETEXT ? "TEXT" : "DATA",
				pid, oaddr, odata);
			addr &= 0xffffffffL;
			if (access_process_vm(child, addr, &tmp, sizeof(tmp), 1) == sizeof(tmp))
				goto out_tsk;
		}
		else
#endif
		{
			if (access_process_vm(child, addr, &data, sizeof(data), 1) == sizeof(data))
				goto out_tsk;
		}
		ret = -EIO;
		goto out_tsk;

	/* Read the word at location addr in the USER area.  For ptraced
	   processes, the kernel saves all regs on a syscall. */
	case PTRACE_PEEKUSR: {
		ret = -EIO;
#ifdef CONFIG_64BIT
		if (__is_compat_task(child)) {
			unsigned int tmp;

			if (addr & (sizeof(int)-1))
				goto out_tsk;
			if ((addr = translate_usr_offset(addr)) < 0)
				goto out_tsk;

			tmp = *(unsigned int *) ((char *) task_regs(child) + addr);
			ret = put_user(tmp, (unsigned int *) data);
			DBG("sys_ptrace(PEEKUSR, %d, %lx, %lx) returning %ld, addr %lx, data %x\n",
				pid, oaddr, odata, ret, addr, tmp);
		}
		else
#endif
		{
			unsigned long tmp;

			if ((addr & (sizeof(long)-1)) || (unsigned long) addr >= sizeof(struct pt_regs))
				goto out_tsk;
			tmp = *(unsigned long *) ((char *) task_regs(child) + addr);
			ret = put_user(tmp, (unsigned long *) data);
		}
		goto out_tsk;
	}
		return sizeof(struct pt_regs);
		pos = sizeof(struct pt_regs);

	return pos;
}

long compat_arch_ptrace(struct task_struct *child, compat_long_t request,
			compat_ulong_t addr, compat_ulong_t data)
{
	compat_uint_t tmp;
	long ret = -EIO;

	switch (request) {

	case PTRACE_PEEKUSR:
		if (addr & (sizeof(compat_uint_t)-1))
			break;
		addr = translate_usr_offset(addr);
		if (addr >= sizeof(struct pt_regs))
			break;

		tmp = *(compat_uint_t *) ((char *) task_regs(child) + addr);
		ret = put_user(tmp, (compat_uint_t *) (unsigned long) data);
		break;

	/* Write the word at location addr in the USER area.  This will need
	   to change when the kernel no longer saves all regs on a syscall.
	   FIXME.  There is a problem at the moment in that r3-r18 are only
	   saved if the process is ptraced on syscall entry, and even then
	   those values are overwritten by actual register values on syscall
	   exit. */
	case PTRACE_POKEUSR:
		ret = -EIO;
		/* Some register values written here may be ignored in
		 * entry.S:syscall_restore_rfi; e.g. iaoq is written with
		 * r31/r31+4, and not with the values in pt_regs.
		 */
		 /* PT_PSW=0, so this is valid for 32 bit processes under 64
		 * bit kernels.
		 */
		if (addr == PT_PSW) {
			/* PT_PSW=0, so this is valid for 32 bit processes
			 * under 64 bit kernels.
			 *
			 * Allow writing to Nullify, Divide-step-correction,
			 * and carry/borrow bits.
			 * BEWARE, if you set N, and then single step, it won't
			 * stop on the nullified instruction.
			 */
			DBG("sys_ptrace(POKEUSR, %d, %lx, %lx)\n",
				pid, oaddr, odata);
			data &= USER_PSW_BITS;
			task_regs(child)->gr[0] &= ~USER_PSW_BITS;
			task_regs(child)->gr[0] |= data;
			ret = 0;
			goto out_tsk;
		}
#ifdef CONFIG_64BIT
		if (__is_compat_task(child)) {
			if (addr & (sizeof(int)-1))
				goto out_tsk;
			if ((addr = translate_usr_offset(addr)) < 0)
				goto out_tsk;
			DBG("sys_ptrace(POKEUSR, %d, %lx, %lx) addr %lx\n",
				pid, oaddr, odata, addr);
			if (addr >= PT_FR0 && addr <= PT_FR31 + 4) {
				/* Special case, fp regs are 64 bits anyway */
				*(unsigned int *) ((char *) task_regs(child) + addr) = data;
		if (addr == PT_PSW) {
			/* Since PT_PSW==0, it is valid for 32 bit processes
			 * under 64 bit kernels as well.
			 */
			ret = arch_ptrace(child, request, addr, data);
		} else {
			if (addr & (sizeof(compat_uint_t)-1))
				break;
			addr = translate_usr_offset(addr);
			if (addr >= sizeof(struct pt_regs))
				break;
			if (addr == PT_IAOQ0+4 || addr == PT_IAOQ1+4) {
				data |= 3; /* ensure userspace privilege */
			}
			if (addr >= PT_FR0 && addr <= PT_FR31 + 4) {
				/* Special case, fp regs are 64 bits anyway */
				*(__u32 *) ((char *) task_regs(child) + addr) = data;
				ret = 0;
			}
			else if ((addr >= PT_GR1+4 && addr <= PT_GR31+4) ||
					addr == PT_IAOQ0+4 || addr == PT_IAOQ1+4 ||
					addr == PT_SAR+4) {
				/* Zero the top 32 bits */
				*(unsigned int *) ((char *) task_regs(child) + addr - 4) = 0;
				*(unsigned int *) ((char *) task_regs(child) + addr) = data;
				ret = 0;
			}
			goto out_tsk;
		}
		else
#endif
		{
			if ((addr & (sizeof(long)-1)) || (unsigned long) addr >= sizeof(struct pt_regs))
				goto out_tsk;
			if ((addr >= PT_GR1 && addr <= PT_GR31) ||
					addr == PT_IAOQ0 || addr == PT_IAOQ1 ||
					(addr >= PT_FR0 && addr <= PT_FR31 + 4) ||
					addr == PT_SAR) {
				*(unsigned long *) ((char *) task_regs(child) + addr) = data;
				ret = 0;
			}
			goto out_tsk;
		}

	case PTRACE_SYSCALL: /* continue and stop at next (return from) syscall */
	case PTRACE_CONT:
		ret = -EIO;
		DBG("sys_ptrace(%s)\n",
			request == PTRACE_SYSCALL ? "SYSCALL" : "CONT");
		if (!valid_signal(data))
			goto out_tsk;
		child->ptrace &= ~(PT_SINGLESTEP|PT_BLOCKSTEP);
		if (request == PTRACE_SYSCALL) {
			set_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		} else {
			clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		}		
		child->exit_code = data;
		goto out_wake_notrap;

	case PTRACE_KILL:
		/*
		 * make the child exit.  Best I can do is send it a
		 * sigkill.  perhaps it should be put in the status
		 * that it wants to exit.
		 */
		ret = 0;
		DBG("sys_ptrace(KILL)\n");
		if (child->exit_state == EXIT_ZOMBIE)	/* already dead */
			goto out_tsk;
		child->exit_code = SIGKILL;
		goto out_wake_notrap;

	case PTRACE_SINGLEBLOCK:
		DBG("sys_ptrace(SINGLEBLOCK)\n");
		ret = -EIO;
		if (!valid_signal(data))
			goto out_tsk;
		clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		child->ptrace &= ~PT_SINGLESTEP;
		child->ptrace |= PT_BLOCKSTEP;
		child->exit_code = data;

		/* Enable taken branch trap. */
		pa_psw(child)->r = 0;
		pa_psw(child)->t = 1;
		pa_psw(child)->h = 0;
		pa_psw(child)->l = 0;
		goto out_wake;

	case PTRACE_SINGLESTEP:
		DBG("sys_ptrace(SINGLESTEP)\n");
		ret = -EIO;
		if (!valid_signal(data))
			goto out_tsk;

		clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		child->ptrace &= ~PT_BLOCKSTEP;
		child->ptrace |= PT_SINGLESTEP;
		child->exit_code = data;

		if (pa_psw(child)->n) {
			struct siginfo si;

			/* Nullified, just crank over the queue. */
			task_regs(child)->iaoq[0] = task_regs(child)->iaoq[1];
			task_regs(child)->iasq[0] = task_regs(child)->iasq[1];
			task_regs(child)->iaoq[1] = task_regs(child)->iaoq[0] + 4;
			pa_psw(child)->n = 0;
			pa_psw(child)->x = 0;
			pa_psw(child)->y = 0;
			pa_psw(child)->z = 0;
			pa_psw(child)->b = 0;
			ptrace_disable(child);
			/* Don't wake up the child, but let the
			   parent know something happened. */
			si.si_code = TRAP_TRACE;
			si.si_addr = (void __user *) (task_regs(child)->iaoq[0] & ~3);
			si.si_signo = SIGTRAP;
			si.si_errno = 0;
			force_sig_info(SIGTRAP, &si, child);
			//notify_parent(child, SIGCHLD);
			//ret = 0;
			goto out_wake;
		}

		/* Enable recovery counter traps.  The recovery counter
		 * itself will be set to zero on a task switch.  If the
		 * task is suspended on a syscall then the syscall return
		 * path will overwrite the recovery counter with a suitable
		 * value such that it traps once back in user space.  We
		 * disable interrupts in the childs PSW here also, to avoid
		 * interrupts while the recovery counter is decrementing.
		 */
		pa_psw(child)->r = 1;
		pa_psw(child)->t = 0;
		pa_psw(child)->h = 0;
		pa_psw(child)->l = 0;
		/* give it a chance to run. */
		goto out_wake;

	case PTRACE_GETEVENTMSG:
                ret = put_user(child->ptrace_message, (unsigned int __user *) data);
		goto out_tsk;

	default:
		ret = ptrace_request(child, request, addr, data);
		goto out_tsk;
	}

out_wake_notrap:
	ptrace_disable(child);
out_wake:
	wake_up_process(child);
	ret = 0;
out_tsk:
	DBG("arch_ptrace(%ld, %d, %lx, %lx) returning %ld\n",
		request, pid, oaddr, odata, ret);
	return ret;
}

void syscall_trace(void)
{
	if (!test_thread_flag(TIF_SYSCALL_TRACE))
		return;
	if (!(current->ptrace & PT_PTRACED))
		return;
	ptrace_notify(SIGTRAP | ((current->ptrace & PT_TRACESYSGOOD)
				 ? 0x80 : 0));
	/*
	 * this isn't the same as continuing with a signal, but it will do
	 * for normal use.  strace only continues with a signal if the
	 * stopping signal is not SIGTRAP.  -brl
	 */
	if (current->exit_code) {
		send_sig(current->exit_code, current, 1);
		current->exit_code = 0;
	}
				*(__u32 *) ((char *) task_regs(child) + addr - 4) = 0;
				*(__u32 *) ((char *) task_regs(child) + addr) = data;
				ret = 0;
			}
		}
		break;

	default:
		ret = compat_ptrace_request(child, request, addr, data);
		break;
	}

	return ret;
}
#endif

long do_syscall_trace_enter(struct pt_regs *regs)
{
	/* Do the secure computing check first. */
	secure_computing_strict(regs->gr[20]);

	if (test_thread_flag(TIF_SYSCALL_TRACE) &&
	    tracehook_report_syscall_entry(regs)) {
		/*
		 * Tracing decided this syscall should not happen or the
		 * debugger stored an invalid system call number. Skip
		 * the system call and the system call restart handling.
		 */
		regs->gr[20] = -1UL;
		goto out;
	}

#ifdef CONFIG_64BIT
	if (!is_compat_task())
		audit_syscall_entry(regs->gr[20], regs->gr[26], regs->gr[25],
				    regs->gr[24], regs->gr[23]);
	else
#endif
		audit_syscall_entry(regs->gr[20] & 0xffffffff,
			regs->gr[26] & 0xffffffff,
			regs->gr[25] & 0xffffffff,
			regs->gr[24] & 0xffffffff,
			regs->gr[23] & 0xffffffff);

out:
	return regs->gr[20];
}

void do_syscall_trace_exit(struct pt_regs *regs)
{
	int stepping = test_thread_flag(TIF_SINGLESTEP) ||
		test_thread_flag(TIF_BLOCKSTEP);

	audit_syscall_exit(regs);

	if (stepping || test_thread_flag(TIF_SYSCALL_TRACE))
		tracehook_report_syscall_exit(regs, stepping);
}
