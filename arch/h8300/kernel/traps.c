/*
 * linux/arch/h8300/boot/traps.c -- general exception handling code
 * H8/300 support Yoshinori Sato <ysato@users.sourceforge.jp>
 * 
 *
 * Cloned from Linux/m68k.
 *
 * No original Copyright holder listed,
 * Probable original (C) Roman Zippel (assigned DJD, 1999)
 *
 * Copyright 1999-2000 D. Jeff Dionne, <jeff@rt-control.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <asm/traps.h>
#include <asm/page.h>
#include <asm/gpio.h>
#include <linux/bug.h>

#include <asm/irq.h>
#include <asm/traps.h>
#include <asm/page.h>

static DEFINE_SPINLOCK(die_lock);

/*
 * this must be called very early as the kernel might
 * use some instruction that are emulated on the 060
 */

void __init base_trap_init(void)
{
}

void __init trap_init (void)
{
}

asmlinkage void set_esp0 (unsigned long ssp)
void __init trap_init(void)
{
}

asmlinkage void set_esp0(unsigned long ssp)
{
	current->thread.esp0 = ssp;
}

/*
 *	Generic dumping code. Used for panic and debug.
 */

static void dump(struct pt_regs *fp)
{
	unsigned long	*sp;
	unsigned char	*tp;
	int		i;

	printk("\nCURRENT PROCESS:\n\n");
	printk("COMM=%s PID=%d\n", current->comm, current->pid);
	if (current->mm) {
		printk("TEXT=%08x-%08x DATA=%08x-%08x BSS=%08x-%08x\n",
	pr_info("\nCURRENT PROCESS:\n\n");
	pr_info("COMM=%s PID=%d\n", current->comm, current->pid);
	if (current->mm) {
		pr_info("TEXT=%08x-%08x DATA=%08x-%08x BSS=%08x-%08x\n",
			(int) current->mm->start_code,
			(int) current->mm->end_code,
			(int) current->mm->start_data,
			(int) current->mm->end_data,
			(int) current->mm->end_data,
			(int) current->mm->brk);
		printk("USER-STACK=%08x  KERNEL-STACK=%08lx\n\n",
		pr_info("USER-STACK=%08x  KERNEL-STACK=%08lx\n\n",
			(int) current->mm->start_stack,
			(int) PAGE_SIZE+(unsigned long)current);
	}

	show_regs(fp);
	printk("\nCODE:");
	tp = ((unsigned char *) fp->pc) - 0x20;
	for (sp = (unsigned long *) tp, i = 0; (i < 0x40);  i += 4) {
		if ((i % 0x10) == 0)
			printk("\n%08x: ", (int) (tp + i));
		printk("%08x ", (int) *sp++);
	}
	printk("\n");

	printk("\nKERNEL STACK:");
	tp = ((unsigned char *) fp) - 0x40;
	for (sp = (unsigned long *) tp, i = 0; (i < 0xc0); i += 4) {
		if ((i % 0x10) == 0)
			printk("\n%08x: ", (int) (tp + i));
		printk("%08x ", (int) *sp++);
	}
	printk("\n");
	if (STACK_MAGIC != *(unsigned long *)((unsigned long)current+PAGE_SIZE))
                printk("(Possibly corrupted stack page??)\n");

	printk("\n\n");
}

void die_if_kernel (char *str, struct pt_regs *fp, int nr)
{
	extern int console_loglevel;

	if (!(fp->ccr & PS_S))
		return;

	console_loglevel = 15;
	dump(fp);

	do_exit(SIGSEGV);
}

extern char _start, _etext;
#define check_kernel_text(addr) \
        ((addr >= (unsigned long)(&_start)) && \
         (addr <  (unsigned long)(&_etext))) 

	pr_info("\nCODE:");
	tp = ((unsigned char *) fp->pc) - 0x20;
	for (sp = (unsigned long *) tp, i = 0; (i < 0x40);  i += 4) {
		if ((i % 0x10) == 0)
			pr_info("\n%08x: ", (int) (tp + i));
		pr_info("%08x ", (int) *sp++);
	}
	pr_info("\n");

	pr_info("\nKERNEL STACK:");
	tp = ((unsigned char *) fp) - 0x40;
	for (sp = (unsigned long *) tp, i = 0; (i < 0xc0); i += 4) {
		if ((i % 0x10) == 0)
			pr_info("\n%08x: ", (int) (tp + i));
		pr_info("%08x ", (int) *sp++);
	}
	pr_info("\n");
	if (STACK_MAGIC != *(unsigned long *)((unsigned long)current+PAGE_SIZE))
		pr_info("(Possibly corrupted stack page??)\n");

	pr_info("\n\n");
}

void die(const char *str, struct pt_regs *fp, unsigned long err)
{
	static int diecount;

	oops_enter();

	console_verbose();
	spin_lock_irq(&die_lock);
	report_bug(fp->pc, fp);
	pr_crit("%s: %04lx [#%d] ", str, err & 0xffff, ++diecount);
	dump(fp);

	spin_unlock_irq(&die_lock);
	do_exit(SIGSEGV);
}

static int kstack_depth_to_print = 24;

void show_stack(struct task_struct *task, unsigned long *esp)
{
	unsigned long *stack,  addr;
	int i;

	if (esp == NULL)
		esp = (unsigned long *) &esp;

	stack = esp;

	printk("Stack from %08lx:", (unsigned long)stack);
	pr_info("Stack from %08lx:", (unsigned long)stack);
	for (i = 0; i < kstack_depth_to_print; i++) {
		if (((unsigned long)stack & (THREAD_SIZE - 1)) == 0)
			break;
		if (i % 8 == 0)
			printk("\n       ");
		printk(" %08lx", *stack++);
	}

	printk("\nCall Trace:");
			pr_info("\n       ");
		pr_info(" %08lx", *stack++);
	}

	pr_info("\nCall Trace:");
	i = 0;
	stack = esp;
	while (((unsigned long)stack & (THREAD_SIZE - 1)) != 0) {
		addr = *stack++;
		/*
		 * If the address is either in the text segment of the
		 * kernel, or in the region which contains vmalloc'ed
		 * memory, it *may* be the address of a calling
		 * routine; if so, print it so that someone tracing
		 * down the cause of the crash will be able to figure
		 * out the call path that was taken.
		 */
		if (check_kernel_text(addr)) {
			if (i % 4 == 0)
				printk("\n       ");
			printk(" [<%08lx>]", addr);
			i++;
		}
	}
	printk("\n");
				pr_info("\n       ");
			pr_info(" [<%08lx>]", addr);
			i++;
		}
	}
	pr_info("\n");
}

void show_trace_task(struct task_struct *tsk)
{
	show_stack(tsk,(unsigned long *)tsk->thread.esp0);
}

void dump_stack(void)
{
	show_stack(NULL,NULL);
}

EXPORT_SYMBOL(dump_stack);
	show_stack(tsk, (unsigned long *)tsk->thread.esp0);
}
