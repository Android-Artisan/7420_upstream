/*
 * File:         arch/blackfin/kernel/ptrace.c
 * Based on:     Taken from linux/kernel/ptrace.c
 * Author:       linux/kernel/ptrace.c is by Ross Biro 1/23/92, edited by Linus Torvalds
 *
 * Created:      1/23/92
 * Description:
 *
 * Modified:
 *               Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 * linux/kernel/ptrace.c is by Ross Biro 1/23/92, edited by Linus Torvalds
 * these modifications are Copyright 2004-2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/signal.h>
#include <linux/elf.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/regset.h>
#include <linux/signal.h>
#include <linux/tracehook.h>
#include <linux/uaccess.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/processor.h>
#include <asm/asm-offsets.h>
#include <asm/dma.h>
#include <asm/fixed_code.h>

#define MAX_SHARED_LIBS 3
#define TEXT_OFFSET 0
#include <asm/cacheflush.h>
#include <asm/mem_map.h>
#include <asm/mmu_context.h>

/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

/* determines which bits in the SYSCFG reg the user has access to. */
/* 1 = access 0 = no access */
#define SYSCFG_MASK 0x0007	/* SYSCFG reg */
/* sets the trace bits. */
#define TRACE_BITS 0x0001

/* Find the stack offset for a register, relative to thread.esp0. */
#define PT_REG(reg)	((long)&((struct pt_regs *)0)->reg)

/*
 * Get the address of the live pt_regs for the specified task.
 * These are saved onto the top kernel stack when the process
 * is not running.
 *
 * Note: if a user thread is execve'd from kernel space, the
 * kernel stack will not be empty on entry to the kernel, so
 * ptracing these tasks will fail.
 */
static inline struct pt_regs *get_user_regs(struct task_struct *task)
{
	return (struct pt_regs *)
	    ((unsigned long)task_stack_page(task) +
	     (THREAD_SIZE - sizeof(struct pt_regs)));
}

/*
 * Get all user integer registers.
 */
static inline int ptrace_getregs(struct task_struct *tsk, void __user * uregs)
{
	struct pt_regs *regs = get_user_regs(tsk);
	return copy_to_user(uregs, regs, sizeof(struct pt_regs)) ? -EFAULT : 0;
}

/* Mapping from PT_xxx to the stack offset at which the register is
 * saved.  Notice that usp has no stack-slot and needs to be treated
 * specially (see get_reg/put_reg below).
 */

/*
 * Get contents of register REGNO in task TASK.
 */
static inline long get_reg(struct task_struct *task, int regno)
{
	unsigned char *reg_ptr;

	struct pt_regs *regs =
	    (struct pt_regs *)((unsigned long)task_stack_page(task) +
			       (THREAD_SIZE - sizeof(struct pt_regs)));
	reg_ptr = (char *)regs;

	switch (regno) {
	case PT_USP:
		return task->thread.usp;
	default:
		if (regno <= 216)
			return *(long *)(reg_ptr + regno);
	}
	/* slight mystery ... never seems to come here but kernel misbehaves without this code! */

	printk(KERN_WARNING "Request to get for unknown register %d\n", regno);
	return 0;
/*
 * Get contents of register REGNO in task TASK.
 */
static inline long
get_reg(struct task_struct *task, unsigned long regno,
	unsigned long __user *datap)
{
	long tmp;
	struct pt_regs *regs = task_pt_regs(task);

	if (regno & 3 || regno > PT_LAST_PSEUDO)
		return -EIO;

	switch (regno) {
	case PT_TEXT_ADDR:
		tmp = task->mm->start_code;
		break;
	case PT_TEXT_END_ADDR:
		tmp = task->mm->end_code;
		break;
	case PT_DATA_ADDR:
		tmp = task->mm->start_data;
		break;
	case PT_USP:
		tmp = task->thread.usp;
		break;
	default:
		if (regno < sizeof(*regs)) {
			void *reg_ptr = regs;
			tmp = *(long *)(reg_ptr + regno);
		} else
			return -EIO;
	}

	return put_user(tmp, datap);
}

/*
 * Write contents of register REGNO in task TASK.
 */
static inline int
put_reg(struct task_struct *task, int regno, unsigned long data)
{
	char *reg_ptr;

	struct pt_regs *regs =
	    (struct pt_regs *)((unsigned long)task_stack_page(task) +
			       (THREAD_SIZE - sizeof(struct pt_regs)));
	reg_ptr = (char *)regs;
put_reg(struct task_struct *task, unsigned long regno, unsigned long data)
{
	struct pt_regs *regs = task_pt_regs(task);

	if (regno & 3 || regno > PT_LAST_PSEUDO)
		return -EIO;

	switch (regno) {
	case PT_PC:
		/*********************************************************************/
		/* At this point the kernel is most likely in exception.             */
		/* The RETX register will be used to populate the pc of the process. */
		/*********************************************************************/
		regs->retx = data;
		regs->pc = data;
		break;
	case PT_RETX:
		break;		/* regs->retx = data; break; */
	case PT_USP:
		regs->usp = data;
		task->thread.usp = data;
		break;
	default:
		if (regno <= 216)
			*(long *)(reg_ptr + regno) = data;
	}
	case PT_SYSCFG:	/* don't let userspace screw with this */
		if ((data & ~1) != 0x6)
			pr_warning("ptrace: ignore syscfg write of %#lx\n", data);
		break;		/* regs->syscfg = data; break; */
	default:
		if (regno < sizeof(*regs)) {
			void *reg_offset = regs;
			*(long *)(reg_offset + regno) = data;
		}
		/* Ignore writes to pseudo registers */
	}

	return 0;
}

/*
 * check that an address falls within the bounds of the target process's memory mappings
 */
static inline int is_user_addr_valid(struct task_struct *child,
				     unsigned long start, unsigned long len)
{
	struct vm_list_struct *vml;
	struct sram_list_struct *sraml;

	for (vml = child->mm->context.vmlist; vml; vml = vml->next)
		if (start >= vml->vma->vm_start && start + len <= vml->vma->vm_end)
			return 0;

	for (sraml = child->mm->context.sram_list; sraml; sraml = sraml->next)
		if (start >= (unsigned long)sraml->addr
		    && start + len <= (unsigned long)sraml->addr + sraml->length)
			return 0;

	if (start >= FIXED_CODE_START && start + len <= FIXED_CODE_END)
		return 0;

int
is_user_addr_valid(struct task_struct *child, unsigned long start, unsigned long len)
{
	bool valid;
	struct vm_area_struct *vma;
	struct sram_list_struct *sraml;

	/* overflow */
	if (start + len < start)
		return -EIO;

	down_read(&child->mm->mmap_sem);
	vma = find_vma(child->mm, start);
	valid = vma && start >= vma->vm_start && start + len <= vma->vm_end;
	up_read(&child->mm->mmap_sem);
	if (valid)
		return 0;

	for (sraml = child->mm->context.sram_list; sraml; sraml = sraml->next)
		if (start >= (unsigned long)sraml->addr
		    && start + len < (unsigned long)sraml->addr + sraml->length)
			return 0;

	if (start >= FIXED_CODE_START && start + len < FIXED_CODE_END)
		return 0;

#ifdef CONFIG_APP_STACK_L1
	if (child->mm->context.l1_stack_save)
		if (start >= (unsigned long)l1_stack_base &&
			start + len < (unsigned long)l1_stack_base + l1_stack_len)
			return 0;
#endif

	return -EIO;
}

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Make sure the single step bit is not set.
 */
void ptrace_disable(struct task_struct *child)
{
	unsigned long tmp;
	/* make sure the single step bit is not set. */
	tmp = get_reg(child, PT_SYSCFG) & ~TRACE_BITS;
	put_reg(child, PT_SYSCFG, tmp);
}

long arch_ptrace(struct task_struct *child, long request, long addr, long data)
{
	int ret;
	int add = 0;
	unsigned long __user *datap = (unsigned long __user *)data;
 * retrieve the contents of Blackfin userspace general registers
 */
static int genregs_get(struct task_struct *target,
		       const struct user_regset *regset,
		       unsigned int pos, unsigned int count,
		       void *kbuf, void __user *ubuf)
{
	struct pt_regs *regs = task_pt_regs(target);
	int ret;

	/* This sucks ... */
	regs->usp = target->thread.usp;

	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				  regs, 0, sizeof(*regs));
	if (ret < 0)
		return ret;

	return user_regset_copyout_zero(&pos, &count, &kbuf, &ubuf,
					sizeof(*regs), -1);
}

/*
 * update the contents of the Blackfin userspace general registers
 */
static int genregs_set(struct task_struct *target,
		       const struct user_regset *regset,
		       unsigned int pos, unsigned int count,
		       const void *kbuf, const void __user *ubuf)
{
	struct pt_regs *regs = task_pt_regs(target);
	int ret;

	/* Don't let people set SYSCFG (it's at the end of pt_regs) */
	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 regs, 0, PT_SYSCFG);
	if (ret < 0)
		return ret;

	/* This sucks ... */
	target->thread.usp = regs->usp;
	/* regs->retx = regs->pc; */

	return user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf,
					PT_SYSCFG, -1);
}

/*
 * Define the register sets available on the Blackfin under Linux
 */
enum bfin_regset {
	REGSET_GENERAL,
};

static const struct user_regset bfin_regsets[] = {
	[REGSET_GENERAL] = {
		.core_note_type = NT_PRSTATUS,
		.n              = sizeof(struct pt_regs) / sizeof(long),
		.size           = sizeof(long),
		.align          = sizeof(long),
		.get            = genregs_get,
		.set            = genregs_set,
	},
};

static const struct user_regset_view user_bfin_native_view = {
	.name      = "Blackfin",
	.e_machine = EM_BLACKFIN,
	.regsets   = bfin_regsets,
	.n         = ARRAY_SIZE(bfin_regsets),
};

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
	return &user_bfin_native_view;
}

void user_enable_single_step(struct task_struct *child)
{
	struct pt_regs *regs = task_pt_regs(child);
	regs->syscfg |= SYSCFG_SSSTEP;

	set_tsk_thread_flag(child, TIF_SINGLESTEP);
}

void user_disable_single_step(struct task_struct *child)
{
	struct pt_regs *regs = task_pt_regs(child);
	regs->syscfg &= ~SYSCFG_SSSTEP;

	clear_tsk_thread_flag(child, TIF_SINGLESTEP);
}

long arch_ptrace(struct task_struct *child, long request,
		 unsigned long addr, unsigned long data)
{
	int ret;
	unsigned long __user *datap = (unsigned long __user *)data;
	void *paddr = (void *)addr;

	switch (request) {
		/* when I and D space are separate, these will need to be fixed. */
	case PTRACE_PEEKDATA:
		pr_debug("ptrace: PEEKDATA\n");
		add = MAX_SHARED_LIBS * 4;	/* space between text and data */
		/* fall through */
	case PTRACE_PEEKTEXT:	/* read word at location addr. */
		{
			unsigned long tmp = 0;
			int copied;

			ret = -EIO;
			pr_debug("ptrace: PEEKTEXT at addr 0x%08lx + add %d %ld\n", addr, add,
			         sizeof(data));
			if (is_user_addr_valid(child, addr + add, sizeof(tmp)) < 0)
				break;
			pr_debug("ptrace: user address is valid\n");

#if L1_CODE_LENGTH != 0
			if (addr + add >= L1_CODE_START
			    && addr + add + sizeof(tmp) <= L1_CODE_START + L1_CODE_LENGTH) {
				safe_dma_memcpy (&tmp, (const void *)(addr + add), sizeof(tmp));
				copied = sizeof(tmp);
			} else
#endif
#if L1_DATA_A_LENGTH != 0
			if (addr + add >= L1_DATA_A_START
			    && addr + add + sizeof(tmp) <= L1_DATA_A_START + L1_DATA_A_LENGTH) {
				memcpy(&tmp, (const void *)(addr + add), sizeof(tmp));
				copied = sizeof(tmp);
			} else
#endif
#if L1_DATA_B_LENGTH != 0
			if (addr + add >= L1_DATA_B_START
			    && addr + add + sizeof(tmp) <= L1_DATA_B_START + L1_DATA_B_LENGTH) {
				memcpy(&tmp, (const void *)(addr + add), sizeof(tmp));
				copied = sizeof(tmp);
			} else
#endif
			if (addr + add >= FIXED_CODE_START
			    && addr + add + sizeof(tmp) <= FIXED_CODE_END) {
				memcpy(&tmp, (const void *)(addr + add), sizeof(tmp));
				copied = sizeof(tmp);
			} else
				copied = access_process_vm(child, addr + add, &tmp,
							   sizeof(tmp), 0);
			pr_debug("ptrace: copied size %d [0x%08lx]\n", copied, tmp);
			if (copied != sizeof(tmp))
				break;
			ret = put_user(tmp, datap);
			break;
		}

		/* read the word at location addr in the USER area. */
	case PTRACE_PEEKUSR:
		{
			unsigned long tmp;
			ret = -EIO;
			tmp = 0;
			if ((addr & 3) || (addr > (sizeof(struct pt_regs) + 16))) {
				printk(KERN_WARNING "ptrace error : PEEKUSR : temporarily returning "
				                    "0 - %x sizeof(pt_regs) is %lx\n",
				     (int)addr, sizeof(struct pt_regs));
				break;
			}
			if (addr == sizeof(struct pt_regs)) {
				/* PT_TEXT_ADDR */
				tmp = child->mm->start_code + TEXT_OFFSET;
			} else if (addr == (sizeof(struct pt_regs) + 4)) {
				/* PT_TEXT_END_ADDR */
				tmp = child->mm->end_code;
			} else if (addr == (sizeof(struct pt_regs) + 8)) {
				/* PT_DATA_ADDR */
				tmp = child->mm->start_data;
#ifdef CONFIG_BINFMT_ELF_FDPIC
			} else if (addr == (sizeof(struct pt_regs) + 12)) {
				tmp = child->mm->context.exec_fdpic_loadmap;
			} else if (addr == (sizeof(struct pt_regs) + 16)) {
				tmp = child->mm->context.interp_fdpic_loadmap;
#endif
			} else {
				tmp = get_reg(child, addr);
			}
			ret = put_user(tmp, datap);
			int copied = 0, to_copy = sizeof(tmp);

			ret = -EIO;
			pr_debug("ptrace: PEEKTEXT at addr 0x%08lx + %i\n", addr, to_copy);
			if (is_user_addr_valid(child, addr, to_copy) < 0)
				break;
			pr_debug("ptrace: user address is valid\n");

			switch (bfin_mem_access_type(addr, to_copy)) {
			case BFIN_MEM_ACCESS_CORE:
			case BFIN_MEM_ACCESS_CORE_ONLY:
				copied = access_process_vm(child, addr, &tmp,
				                           to_copy, 0);
				if (copied)
					break;

				/* hrm, why didn't that work ... maybe no mapping */
				if (addr >= FIXED_CODE_START &&
				    addr + to_copy <= FIXED_CODE_END) {
					copy_from_user_page(0, 0, 0, &tmp, paddr, to_copy);
					copied = to_copy;
				} else if (addr >= BOOT_ROM_START) {
					memcpy(&tmp, paddr, to_copy);
					copied = to_copy;
				}

				break;
			case BFIN_MEM_ACCESS_DMA:
				if (safe_dma_memcpy(&tmp, paddr, to_copy))
					copied = to_copy;
				break;
			case BFIN_MEM_ACCESS_ITEST:
				if (isram_memcpy(&tmp, paddr, to_copy))
					copied = to_copy;
				break;
			default:
				copied = 0;
				break;
			}

			pr_debug("ptrace: copied size %d [0x%08lx]\n", copied, tmp);
			if (copied == to_copy)
				ret = put_user(tmp, datap);
			break;
		}

		/* when I and D space are separate, this will have to be fixed. */
	case PTRACE_POKEDATA:
		printk(KERN_NOTICE "ptrace: PTRACE_PEEKDATA\n");
		/* fall through */
	case PTRACE_POKETEXT:	/* write the word at location addr. */
		{
			int copied;

			ret = -EIO;
			pr_debug("ptrace: POKETEXT at addr 0x%08lx + add %d %ld bytes %lx\n",
			         addr, add, sizeof(data), data);
			if (is_user_addr_valid(child, addr + add, sizeof(data)) < 0)
				break;
			pr_debug("ptrace: user address is valid\n");

#if L1_CODE_LENGTH != 0
			if (addr + add >= L1_CODE_START
			    && addr + add + sizeof(data) <= L1_CODE_START + L1_CODE_LENGTH) {
				safe_dma_memcpy ((void *)(addr + add), &data, sizeof(data));
				copied = sizeof(data);
			} else
#endif
#if L1_DATA_A_LENGTH != 0
			if (addr + add >= L1_DATA_A_START
			    && addr + add + sizeof(data) <= L1_DATA_A_START + L1_DATA_A_LENGTH) {
				memcpy((void *)(addr + add), &data, sizeof(data));
				copied = sizeof(data);
			} else
#endif
#if L1_DATA_B_LENGTH != 0
			if (addr + add >= L1_DATA_B_START
			    && addr + add + sizeof(data) <= L1_DATA_B_START + L1_DATA_B_LENGTH) {
				memcpy((void *)(addr + add), &data, sizeof(data));
				copied = sizeof(data);
			} else
#endif
			if (addr + add >= FIXED_CODE_START
			    && addr + add + sizeof(data) <= FIXED_CODE_END) {
				memcpy((void *)(addr + add), &data, sizeof(data));
				copied = sizeof(data);
			} else
				copied = access_process_vm(child, addr + add, &data,
							   sizeof(data), 1);
			pr_debug("ptrace: copied size %d\n", copied);
			if (copied != sizeof(data))
				break;
			ret = 0;
			break;
		}

	case PTRACE_POKEUSR:	/* write the word at location addr in the USER area */
		ret = -EIO;
		if ((addr & 3) || (addr > (sizeof(struct pt_regs) + 16))) {
			printk(KERN_WARNING "ptrace error : POKEUSR: temporarily returning 0\n");
			break;
		}

		if (addr >= (sizeof(struct pt_regs))) {
			ret = 0;
			break;
		}
		if (addr == PT_SYSCFG) {
			data &= SYSCFG_MASK;
			data |= get_reg(child, PT_SYSCFG);
		}
		ret = put_reg(child, addr, data);
		break;

	case PTRACE_SYSCALL:	/* continue and stop at next (return from) syscall */
	case PTRACE_CONT:
		{		/* restart after signal. */
			long tmp;

			pr_debug("ptrace_cont\n");

			ret = -EIO;
			if (!valid_signal(data))
				break;
			if (request == PTRACE_SYSCALL)
				set_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
			else
				clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);

			child->exit_code = data;
			/* make sure the single step bit is not set. */
			tmp = get_reg(child, PT_SYSCFG) & ~(TRACE_BITS);
			put_reg(child, PT_SYSCFG, tmp);
			pr_debug("before wake_up_process\n");
			wake_up_process(child);
			ret = 0;
			break;
		}

	/*
	 * make the child exit.  Best I can do is send it a sigkill.
	 * perhaps it should be put in the status that it wants to
	 * exit.
	 */
	case PTRACE_KILL:
		{
			long tmp;
			ret = 0;
			if (child->exit_state == EXIT_ZOMBIE)	/* already dead */
				break;
			child->exit_code = SIGKILL;
			/* make sure the single step bit is not set. */
			tmp = get_reg(child, PT_SYSCFG) & ~(TRACE_BITS);
			put_reg(child, PT_SYSCFG, tmp);
			wake_up_process(child);
			break;
		}

	case PTRACE_SINGLESTEP:
		{		/* set the trap flag. */
			long tmp;

			pr_debug("single step\n");
			ret = -EIO;
			if (!valid_signal(data))
				break;
			clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);

			tmp = get_reg(child, PT_SYSCFG) | (TRACE_BITS);
			put_reg(child, PT_SYSCFG, tmp);

			child->exit_code = data;
			/* give it a chance to run. */
			wake_up_process(child);
			ret = 0;
			break;
		}

	case PTRACE_GETREGS:
		{

			/* Get all gp regs from the child. */
			ret = ptrace_getregs(child, datap);
			break;
		}

	case PTRACE_SETREGS:
		{
			printk(KERN_NOTICE
			       "ptrace: SETREGS: **** NOT IMPLEMENTED ***\n");
			/* Set all gp regs in the child. */
			ret = 0;
			break;
		}
		pr_debug("ptrace: PTRACE_PEEKDATA\n");
		/* fall through */
	case PTRACE_POKETEXT:	/* write the word at location addr. */
		{
			int copied = 0, to_copy = sizeof(data);

			ret = -EIO;
			pr_debug("ptrace: POKETEXT at addr 0x%08lx + %i bytes %lx\n",
			         addr, to_copy, data);
			if (is_user_addr_valid(child, addr, to_copy) < 0)
				break;
			pr_debug("ptrace: user address is valid\n");

			switch (bfin_mem_access_type(addr, to_copy)) {
			case BFIN_MEM_ACCESS_CORE:
			case BFIN_MEM_ACCESS_CORE_ONLY:
				copied = access_process_vm(child, addr, &data,
				                           to_copy, 1);
				break;
			case BFIN_MEM_ACCESS_DMA:
				if (safe_dma_memcpy(paddr, &data, to_copy))
					copied = to_copy;
				break;
			case BFIN_MEM_ACCESS_ITEST:
				if (isram_memcpy(paddr, &data, to_copy))
					copied = to_copy;
				break;
			default:
				copied = 0;
				break;
			}

			pr_debug("ptrace: copied size %d\n", copied);
			if (copied == to_copy)
				ret = 0;
			break;
		}

	case PTRACE_PEEKUSR:
		switch (addr) {
#ifdef CONFIG_BINFMT_ELF_FDPIC	/* backwards compat */
		case PT_FDPIC_EXEC:
			request = PTRACE_GETFDPIC;
			addr = PTRACE_GETFDPIC_EXEC;
			goto case_default;
		case PT_FDPIC_INTERP:
			request = PTRACE_GETFDPIC;
			addr = PTRACE_GETFDPIC_INTERP;
			goto case_default;
#endif
		default:
			ret = get_reg(child, addr, datap);
		}
		pr_debug("ptrace: PEEKUSR reg %li with %#lx = %i\n", addr, data, ret);
		break;

	case PTRACE_POKEUSR:
		ret = put_reg(child, addr, data);
		pr_debug("ptrace: POKEUSR reg %li with %li = %i\n", addr, data, ret);
		break;

	case PTRACE_GETREGS:
		pr_debug("ptrace: PTRACE_GETREGS\n");
		return copy_regset_to_user(child, &user_bfin_native_view,
					   REGSET_GENERAL,
					   0, sizeof(struct pt_regs),
					   datap);

	case PTRACE_SETREGS:
		pr_debug("ptrace: PTRACE_SETREGS\n");
		return copy_regset_from_user(child, &user_bfin_native_view,
					     REGSET_GENERAL,
					     0, sizeof(struct pt_regs),
					     datap);

	case_default:
	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}

	return ret;
}

asmlinkage void syscall_trace(void)
{

	if (!test_thread_flag(TIF_SYSCALL_TRACE))
		return;

	if (!(current->ptrace & PT_PTRACED))
		return;

	/* the 0x80 provides a way for the tracing parent to distinguish
	 * between a syscall stop and SIGTRAP delivery
	 */
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
asmlinkage int syscall_trace_enter(struct pt_regs *regs)
{
	int ret = 0;

	if (test_thread_flag(TIF_SYSCALL_TRACE))
		ret = tracehook_report_syscall_entry(regs);

	return ret;
}

asmlinkage void syscall_trace_leave(struct pt_regs *regs)
{
	int step;

	step = test_thread_flag(TIF_SINGLESTEP);
	if (step || test_thread_flag(TIF_SYSCALL_TRACE))
		tracehook_report_syscall_exit(regs, step);
}
