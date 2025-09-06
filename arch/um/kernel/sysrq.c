/*
 * Copyright (C) 2001 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 * Copyright (C) 2013 Richard Weinberger <richrd@nod.at>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kallsyms.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include "sysrq.h"

/* Catch non-i386 SUBARCH's. */
#if !defined(CONFIG_UML_X86) || defined(CONFIG_64BIT)
void show_trace(struct task_struct *task, unsigned long * stack)
{
	unsigned long addr;

	if (!stack) {
		stack = (unsigned long*) &stack;
		WARN_ON(1);
	}

	printk(KERN_INFO "Call Trace: \n");
	while (((long) stack & (THREAD_SIZE-1)) != 0) {
		addr = *stack;
		if (__kernel_text_address(addr)) {
			printk(KERN_INFO "%08lx:  [<%08lx>]",
			       (unsigned long) stack, addr);
			print_symbol(KERN_CONT " %s", addr);
			printk(KERN_CONT "\n");
		}
		stack++;
	}
	printk(KERN_INFO "\n");
}
#endif

/*
 * stack dumps generator - this is used by arch-independent code.
 * And this is identical to i386 currently.
 */
void dump_stack(void)
{
	unsigned long stack;

	show_trace(current, &stack);
}
EXPORT_SYMBOL(dump_stack);

/*Stolen from arch/i386/kernel/traps.c */
static const int kstack_depth_to_print = 24;

/* This recently started being used in arch-independent code too, as in
 * kernel/sched.c.*/
void show_stack(struct task_struct *task, unsigned long *esp)
{
	unsigned long *stack;
	int i;

	if (esp == NULL) {
		if (task != current && task != NULL) {
			esp = (unsigned long *) KSTK_ESP(task);
		} else {
			esp = (unsigned long *) &esp;
		}
	}

	stack = esp;
	for (i = 0; i < kstack_depth_to_print; i++) {
		if (kstack_end(stack))
			break;
		if (i && ((i % 8) == 0))
			printk("\n" KERN_INFO "       ");
		printk("%08lx ", *stack++);
	}

	show_trace(task, esp);
#include <asm/sysrq.h>
#include <asm/stacktrace.h>
#include <os.h>

static void _print_addr(void *data, unsigned long address, int reliable)
{
	pr_info(" [<%08lx>]", address);
	pr_cont(" %s", reliable ? "" : "? ");
	print_symbol("%s", address);
	pr_cont("\n");
}

static const struct stacktrace_ops stackops = {
	.address = _print_addr
};

void show_stack(struct task_struct *task, unsigned long *stack)
{
	struct pt_regs *segv_regs = current->thread.segv_regs;
	int i;

	if (!segv_regs && os_is_signal_stack()) {
		pr_err("Received SIGSEGV in SIGSEGV handler,"
				" aborting stack trace!\n");
		return;
	}

	if (!stack)
		stack = get_stack_pointer(task, segv_regs);

	pr_info("Stack:\n");
	for (i = 0; i < 3 * STACKSLOTS_PER_LINE; i++) {
		if (kstack_end(stack))
			break;
		if (i && ((i % STACKSLOTS_PER_LINE) == 0))
			pr_cont("\n");
		pr_cont(" %08lx", *stack++);
	}
	pr_cont("\n");

	pr_info("Call Trace:\n");
	dump_trace(current, &stackops, NULL);
	pr_info("\n");
}
