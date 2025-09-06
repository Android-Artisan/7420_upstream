/*
 *  linux/arch/h8300/kernel/ptrace.c
 *
 *  Yoshinori Sato <ysato@users.sourceforge.jp>
 *
 *  Based on:
 *  linux/arch/m68k/kernel/ptrace.c
 *
 *  Copyright (C) 1994 by Hamish Macdonald
 *  Taken from linux/kernel/ptrace.c and modified for M680x0.
 *  linux/kernel/ptrace.c is by Ross Biro 1/23/92, edited by Linus Torvalds
 *  Copyright 2015 Yoshinori Sato <ysato@users.sourceforge.jp>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of
 * this archive for more details.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/signal.h>

#include <asm/uaccess.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/processor.h>
#include <asm/signal.h>

/* cpu depend functions */
extern long h8300_get_reg(struct task_struct *task, int regno);
extern int  h8300_put_reg(struct task_struct *task, int regno, unsigned long data);
extern void h8300_disable_trace(struct task_struct *child);
extern void h8300_enable_trace(struct task_struct *child);

/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

inline
static int read_long(struct task_struct * tsk, unsigned long addr,
	unsigned long * result)
{
	*result = *(unsigned long *)addr;
	return 0;
}

void ptrace_disable(struct task_struct *child)
{
	h8300_disable_trace(child);
}

long arch_ptrace(struct task_struct *child, long request, long addr, long data)
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/audit.h>
#include <linux/tracehook.h>
#include <linux/regset.h>
#include <linux/elf.h>

#define CCR_MASK 0x6f    /* mode/imask not set */
#define EXR_MASK 0x80    /* modify only T */

#define PT_REG(r) offsetof(struct pt_regs, r)

extern void user_disable_single_step(struct task_struct *child);

/* Mapping from PT_xxx to the stack offset at which the register is
   saved.  Notice that usp has no stack-slot and needs to be treated
   specially (see get_reg/put_reg below). */
static const int register_offset[] = {
	PT_REG(er1), PT_REG(er2), PT_REG(er3), PT_REG(er4),
	PT_REG(er5), PT_REG(er6), PT_REG(er0), -1,
	PT_REG(orig_er0), PT_REG(ccr), PT_REG(pc),
#if defined(CONFIG_CPU_H8S)
	PT_REG(exr),
#endif
};

/* read register */
long h8300_get_reg(struct task_struct *task, int regno)
{
	switch (regno) {
	case PT_USP:
		return task->thread.usp + sizeof(long)*2;
	case PT_CCR:
	case PT_EXR:
	    return *(unsigned short *)(task->thread.esp0 +
				       register_offset[regno]);
	default:
	    return *(unsigned long *)(task->thread.esp0 +
				      register_offset[regno]);
	}
}

int h8300_put_reg(struct task_struct *task, int regno, unsigned long data)
{
	unsigned short oldccr;
	unsigned short oldexr;

	switch (regno) {
	case PT_USP:
		task->thread.usp = data - sizeof(long)*2;
	case PT_CCR:
		oldccr = *(unsigned short *)(task->thread.esp0 +
					     register_offset[regno]);
		oldccr &= ~CCR_MASK;
		data &= CCR_MASK;
		data |= oldccr;
		*(unsigned short *)(task->thread.esp0 +
				    register_offset[regno]) = data;
		break;
	case PT_EXR:
		oldexr = *(unsigned short *)(task->thread.esp0 +
					     register_offset[regno]);
		oldccr &= ~EXR_MASK;
		data &= EXR_MASK;
		data |= oldexr;
		*(unsigned short *)(task->thread.esp0 +
				    register_offset[regno]) = data;
		break;
	default:
		*(unsigned long *)(task->thread.esp0 +
				   register_offset[regno]) = data;
		break;
	}
	return 0;
}

static int regs_get(struct task_struct *target,
		    const struct user_regset *regset,
		    unsigned int pos, unsigned int count,
		    void *kbuf, void __user *ubuf)
{
	int r;
	struct user_regs_struct regs;
	long *reg = (long *)&regs;

	/* build user regs in buffer */
	BUILD_BUG_ON(sizeof(regs) % sizeof(long) != 0);
	for (r = 0; r < sizeof(regs) / sizeof(long); r++)
		*reg++ = h8300_get_reg(target, r);

	return user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				   &regs, 0, sizeof(regs));
}

static int regs_set(struct task_struct *target,
		    const struct user_regset *regset,
		    unsigned int pos, unsigned int count,
		    const void *kbuf, const void __user *ubuf)
{
	int r;
	int ret;
	struct user_regs_struct regs;
	long *reg;

	/* build user regs in buffer */
	BUILD_BUG_ON(sizeof(regs) % sizeof(long) != 0);
	for (reg = (long *)&regs, r = 0; r < sizeof(regs) / sizeof(long); r++)
		*reg++ = h8300_get_reg(target, r);

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &regs, 0, sizeof(regs));
	if (ret)
		return ret;

	/* write back to pt_regs */
	for (reg = (long *)&regs, r = 0; r < sizeof(regs) / sizeof(long); r++)
		h8300_put_reg(target, r, *reg++);
	return 0;
}

enum h8300_regset {
	REGSET_GENERAL,
};

static const struct user_regset h8300_regsets[] = {
	[REGSET_GENERAL] = {
		.core_note_type	= NT_PRSTATUS,
		.n		= ELF_NGREG,
		.size		= sizeof(long),
		.align		= sizeof(long),
		.get		= regs_get,
		.set		= regs_set,
	},
};

static const struct user_regset_view user_h8300_native_view = {
	.name = "h8300",
	.e_machine = EM_H8_300,
	.regsets = h8300_regsets,
	.n = ARRAY_SIZE(h8300_regsets),
};

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
	return &user_h8300_native_view;
}

void ptrace_disable(struct task_struct *child)
{
	user_disable_single_step(child);
}

long arch_ptrace(struct task_struct *child, long request,
		 unsigned long addr, unsigned long data)
{
	int ret;

	switch (request) {
		case PTRACE_PEEKTEXT: /* read word at location addr. */ 
		case PTRACE_PEEKDATA: {
			unsigned long tmp;

			ret = read_long(child, addr, &tmp);
			if (ret < 0)
				break ;
			ret = put_user(tmp, (unsigned long *) data);
			break ;
		}

	/* read the word at location addr in the USER area. */
		case PTRACE_PEEKUSR: {
			unsigned long tmp = 0;
			
			if ((addr & 3) || addr < 0 || addr >= sizeof(struct user)) {
				ret = -EIO;
				break ;
			}
			
		        ret = 0;  /* Default return condition */
			addr = addr >> 2; /* temporary hack. */

			if (addr < H8300_REGS_NO)
				tmp = h8300_get_reg(child, addr);
			else {
				switch(addr) {
				case 49:
					tmp = child->mm->start_code;
					break ;
				case 50:
					tmp = child->mm->start_data;
					break ;
				case 51:
					tmp = child->mm->end_code;
					break ;
				case 52:
					tmp = child->mm->end_data;
					break ;
				default:
					ret = -EIO;
				}
			}
			if (!ret)
				ret = put_user(tmp,(unsigned long *) data);
			break ;
		}

      /* when I and D space are separate, this will have to be fixed. */
		case PTRACE_POKETEXT: /* write the word at location addr. */
		case PTRACE_POKEDATA:
			ret = generic_ptrace_pokedata(child, addr, data);
			break;

		case PTRACE_POKEUSR: /* write the word at location addr in the USER area */
			if ((addr & 3) || addr < 0 || addr >= sizeof(struct user)) {
				ret = -EIO;
				break ;
			}
			addr = addr >> 2; /* temporary hack. */
			    
			if (addr == PT_ORIG_ER0) {
				ret = -EIO;
				break ;
			}
			if (addr < H8300_REGS_NO) {
				ret = h8300_put_reg(child, addr, data);
				break ;
			}
			ret = -EIO;
			break ;
		case PTRACE_SYSCALL: /* continue and stop at next (return from) syscall */
		case PTRACE_CONT: { /* restart after signal. */
			ret = -EIO;
			if (!valid_signal(data))
				break ;
			if (request == PTRACE_SYSCALL)
				set_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
			else
				clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
			child->exit_code = data;
			wake_up_process(child);
			/* make sure the single step bit is not set. */
			h8300_disable_trace(child);
			ret = 0;
		}

/*
 * make the child exit.  Best I can do is send it a sigkill. 
 * perhaps it should be put in the status that it wants to 
 * exit.
 */
		case PTRACE_KILL: {

			ret = 0;
			if (child->exit_state == EXIT_ZOMBIE) /* already dead */
				break;
			child->exit_code = SIGKILL;
			h8300_disable_trace(child);
			wake_up_process(child);
			break;
		}

		case PTRACE_SINGLESTEP: {  /* set the trap flag. */
			ret = -EIO;
			if (!valid_signal(data))
				break;
			clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
			child->exit_code = data;
			h8300_enable_trace(child);
			wake_up_process(child);
			ret = 0;
			break;
		}

		case PTRACE_DETACH:	/* detach a process that was attached. */
			ret = ptrace_detach(child, data);
			break;

		case PTRACE_GETREGS: { /* Get all gp regs from the child. */
		  	int i;
			unsigned long tmp;
			for (i = 0; i < H8300_REGS_NO; i++) {
			    tmp = h8300_get_reg(child, i);
			    if (put_user(tmp, (unsigned long *) data)) {
				ret = -EFAULT;
				break;
			    }
			    data += sizeof(long);
			}
			ret = 0;
			break;
		}

		case PTRACE_SETREGS: { /* Set all gp regs in the child. */
			int i;
			unsigned long tmp;
			for (i = 0; i < H8300_REGS_NO; i++) {
			    if (get_user(tmp, (unsigned long *) data)) {
				ret = -EFAULT;
				break;
			    }
			    h8300_put_reg(child, i, tmp);
			    data += sizeof(long);
			}
			ret = 0;
			break;
		}

		default:
			ret = -EIO;
			break;
	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}
	return ret;
}

asmlinkage void do_syscall_trace(void)
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
asmlinkage long do_syscall_trace_enter(struct pt_regs *regs)
{
	long ret = 0;

	if (test_thread_flag(TIF_SYSCALL_TRACE) &&
	    tracehook_report_syscall_entry(regs))
		/*
		 * Tracing decided this syscall should not happen.
		 * We'll return a bogus call number to get an ENOSYS
		 * error, but leave the original number in regs->regs[0].
		 */
		ret = -1L;

	audit_syscall_entry(regs->er1, regs->er2, regs->er3,
			    regs->er4, regs->er5);

	return ret ?: regs->er0;
}

asmlinkage void do_syscall_trace_leave(struct pt_regs *regs)
{
	int step;

	audit_syscall_exit(regs);

	step = test_thread_flag(TIF_SINGLESTEP);
	if (step || test_thread_flag(TIF_SYSCALL_TRACE))
		tracehook_report_syscall_exit(regs, step);
}
