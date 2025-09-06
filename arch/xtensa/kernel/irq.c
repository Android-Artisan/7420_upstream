/*
 * linux/arch/xtensa/kernel/irq.c
 *
 * Xtensa built-in interrupt controller and some generic functions copied
 * from i386.
 *
 * Copyright (C) 2002 - 2006 Tensilica, Inc.
 * Copyright (C) 2002 - 2013 Tensilica, Inc.
 * Copyright (C) 1992, 1998 Linus Torvalds, Ingo Molnar
 *
 *
 * Chris Zankel <chris@zankel.net>
 * Kevin Chea
 *
 */

#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel_stat.h>

#include <asm/uaccess.h>
#include <asm/platform.h>

static unsigned int cached_irq_mask;

atomic_t irq_err_count;

/*
 * 'what should we do if we get a hw irq event on an illegal vector'.
 * each architecture has to answer this themselves.
 */
void ack_bad_irq(unsigned int irq)
{
          printk("unexpected IRQ trap at vector %02x\n", irq);
}

/*
 * do_IRQ handles all normal device IRQ's (the special
 * SMP cross-CPU interrupts have their own specific
 * handlers).
 */

asmlinkage void do_IRQ(int irq, struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);
	struct irq_desc *desc = irq_desc + irq;

	if (irq >= NR_IRQS) {
		printk(KERN_EMERG "%s: cannot handle IRQ %d\n",
				__FUNCTION__, irq);
	}

	irq_enter();

#include <linux/irqchip.h>
#include <linux/irqchip/xtensa-mx.h>
#include <linux/irqchip/xtensa-pic.h>
#include <linux/irqdomain.h>
#include <linux/of.h>

#include <asm/mxregs.h>
#include <asm/uaccess.h>
#include <asm/platform.h>

DECLARE_PER_CPU(unsigned long, nmi_count);

asmlinkage void do_IRQ(int hwirq, struct pt_regs *regs)
{
	int irq = irq_find_mapping(NULL, hwirq);

#ifdef CONFIG_DEBUG_STACKOVERFLOW
	/* Debugging check for stack overflow: is there less than 1KB free? */
	{
		unsigned long sp;

		__asm__ __volatile__ ("mov %0, a1\n" : "=a" (sp));
		sp &= THREAD_SIZE - 1;

		if (unlikely(sp < (sizeof(thread_info) + 1024)))
			printk("Stack overflow in do_IRQ: %ld\n",
			       sp - sizeof(struct thread_info));
	}
#endif
	desc->handle_irq(irq, desc);

	irq_exit();
	set_irq_regs(old_regs);
}

/*
 * Generic, controller-independent functions:
 */

int show_interrupts(struct seq_file *p, void *v)
{
	int i = *(loff_t *) v, j;
	struct irqaction * action;
	unsigned long flags;

	if (i == 0) {
		seq_printf(p, "           ");
		for_each_online_cpu(j)
			seq_printf(p, "CPU%d       ",j);
		seq_putc(p, '\n');
	}

	if (i < NR_IRQS) {
		spin_lock_irqsave(&irq_desc[i].lock, flags);
		action = irq_desc[i].action;
		if (!action)
			goto skip;
		seq_printf(p, "%3d: ",i);
#ifndef CONFIG_SMP
		seq_printf(p, "%10u ", kstat_irqs(i));
#else
		for_each_online_cpu(j)
			seq_printf(p, "%10u ", kstat_cpu(j).irqs[i]);
#endif
		seq_printf(p, " %14s", irq_desc[i].chip->typename);
		seq_printf(p, "  %s", action->name);

		for (action=action->next; action; action = action->next)
			seq_printf(p, ", %s", action->name);

		seq_putc(p, '\n');
skip:
		spin_unlock_irqrestore(&irq_desc[i].lock, flags);
	} else if (i == NR_IRQS) {
		seq_printf(p, "NMI: ");
		for_each_online_cpu(j)
			seq_printf(p, "%10u ", nmi_count(j));
		seq_putc(p, '\n');
		seq_printf(p, "ERR: %10u\n", atomic_read(&irq_err_count));
	generic_handle_irq(irq);
}

int arch_show_interrupts(struct seq_file *p, int prec)
{
	unsigned cpu __maybe_unused;
#ifdef CONFIG_SMP
	show_ipi_list(p, prec);
#endif
#if XTENSA_FAKE_NMI
	seq_printf(p, "%*s:", prec, "NMI");
	for_each_online_cpu(cpu)
		seq_printf(p, " %10lu", per_cpu(nmi_count, cpu));
	seq_puts(p, "   Non-maskable interrupts\n");
#endif
	return 0;
}

int xtensa_irq_domain_xlate(const u32 *intspec, unsigned int intsize,
		unsigned long int_irq, unsigned long ext_irq,
		unsigned long *out_hwirq, unsigned int *out_type)
{
	if (WARN_ON(intsize < 1 || intsize > 2))
		return -EINVAL;
	if (intsize == 2 && intspec[1] == 1) {
		int_irq = xtensa_map_ext_irq(ext_irq);
		if (int_irq < XCHAL_NUM_INTERRUPTS)
			*out_hwirq = int_irq;
		else
			return -EINVAL;
	} else {
		*out_hwirq = int_irq;
	}
	*out_type = IRQ_TYPE_NONE;
	return 0;
}

int xtensa_irq_map(struct irq_domain *d, unsigned int irq,
		irq_hw_number_t hw)
{
	struct irq_chip *irq_chip = d->host_data;
	u32 mask = 1 << hw;

	if (mask & XCHAL_INTTYPE_MASK_SOFTWARE) {
		irq_set_chip_and_handler_name(irq, irq_chip,
				handle_simple_irq, "level");
		irq_set_status_flags(irq, IRQ_LEVEL);
	} else if (mask & XCHAL_INTTYPE_MASK_EXTERN_EDGE) {
		irq_set_chip_and_handler_name(irq, irq_chip,
				handle_edge_irq, "edge");
		irq_clear_status_flags(irq, IRQ_LEVEL);
	} else if (mask & XCHAL_INTTYPE_MASK_EXTERN_LEVEL) {
		irq_set_chip_and_handler_name(irq, irq_chip,
				handle_level_irq, "level");
		irq_set_status_flags(irq, IRQ_LEVEL);
	} else if (mask & XCHAL_INTTYPE_MASK_TIMER) {
		irq_set_chip_and_handler_name(irq, irq_chip,
				handle_percpu_irq, "timer");
		irq_clear_status_flags(irq, IRQ_LEVEL);
#ifdef XCHAL_INTTYPE_MASK_PROFILING
	} else if (mask & XCHAL_INTTYPE_MASK_PROFILING) {
		irq_set_chip_and_handler_name(irq, irq_chip,
				handle_percpu_irq, "profiling");
		irq_set_status_flags(irq, IRQ_LEVEL);
#endif
	} else {/* XCHAL_INTTYPE_MASK_WRITE_ERROR */
		/* XCHAL_INTTYPE_MASK_NMI */
		irq_set_chip_and_handler_name(irq, irq_chip,
				handle_level_irq, "level");
		irq_set_status_flags(irq, IRQ_LEVEL);
	}
	return 0;
}

static void xtensa_irq_mask(unsigned int irq)
{
	cached_irq_mask &= ~(1 << irq);
	set_sr (cached_irq_mask, INTENABLE);
}

static void xtensa_irq_unmask(unsigned int irq)
{
	cached_irq_mask |= 1 << irq;
	set_sr (cached_irq_mask, INTENABLE);
}

static void xtensa_irq_ack(unsigned int irq)
{
	set_sr(1 << irq, INTCLEAR);
}

static int xtensa_irq_retrigger(unsigned int irq)
{
	set_sr (1 << irq, INTSET);
	return 1;
}


static struct irq_chip xtensa_irq_chip = {
	.name		= "xtensa",
	.mask		= xtensa_irq_mask,
	.unmask		= xtensa_irq_unmask,
	.ack		= xtensa_irq_ack,
	.retrigger	= xtensa_irq_retrigger,
};

void __init init_IRQ(void)
{
	int index;

	for (index = 0; index < XTENSA_NR_IRQS; index++) {
		int mask = 1 << index;

		if (mask & XCHAL_INTTYPE_MASK_SOFTWARE)
			set_irq_chip_and_handler(index, &xtensa_irq_chip,
						 handle_simple_irq);

		else if (mask & XCHAL_INTTYPE_MASK_EXTERN_EDGE)
			set_irq_chip_and_handler(index, &xtensa_irq_chip,
						 handle_edge_irq);

		else if (mask & XCHAL_INTTYPE_MASK_EXTERN_LEVEL)
			set_irq_chip_and_handler(index, &xtensa_irq_chip,
						 handle_level_irq);

		else if (mask & XCHAL_INTTYPE_MASK_TIMER)
			set_irq_chip_and_handler(index, &xtensa_irq_chip,
						 handle_edge_irq);

		else	/* XCHAL_INTTYPE_MASK_WRITE_ERROR */
			/* XCHAL_INTTYPE_MASK_NMI */

			set_irq_chip_and_handler(index, &xtensa_irq_chip,
						 handle_level_irq);
	}

	cached_irq_mask = 0;
}
unsigned xtensa_map_ext_irq(unsigned ext_irq)
{
	unsigned mask = XCHAL_INTTYPE_MASK_EXTERN_EDGE |
		XCHAL_INTTYPE_MASK_EXTERN_LEVEL;
	unsigned i;

	for (i = 0; mask; ++i, mask >>= 1) {
		if ((mask & 1) && ext_irq-- == 0)
			return i;
	}
	return XCHAL_NUM_INTERRUPTS;
}

unsigned xtensa_get_ext_irq_no(unsigned irq)
{
	unsigned mask = (XCHAL_INTTYPE_MASK_EXTERN_EDGE |
		XCHAL_INTTYPE_MASK_EXTERN_LEVEL) &
		((1u << irq) - 1);
	return hweight32(mask);
}

void __init init_IRQ(void)
{
#ifdef CONFIG_USE_OF
	irqchip_init();
#else
#ifdef CONFIG_HAVE_SMP
	xtensa_mx_init_legacy(NULL);
#else
	xtensa_pic_init_legacy(NULL);
#endif
#endif

#ifdef CONFIG_SMP
	ipi_init();
#endif
	variant_init_irq();
}

#ifdef CONFIG_HOTPLUG_CPU
/*
 * The CPU has been marked offline.  Migrate IRQs off this CPU.  If
 * the affinity settings do not allow other CPUs, force them onto any
 * available CPU.
 */
void migrate_irqs(void)
{
	unsigned int i, cpu = smp_processor_id();

	for_each_active_irq(i) {
		struct irq_data *data = irq_get_irq_data(i);
		struct cpumask *mask;
		unsigned int newcpu;

		if (irqd_is_per_cpu(data))
			continue;

		mask = irq_data_get_affinity_mask(data);
		if (!cpumask_test_cpu(cpu, mask))
			continue;

		newcpu = cpumask_any_and(mask, cpu_online_mask);

		if (newcpu >= nr_cpu_ids) {
			pr_info_ratelimited("IRQ%u no longer affine to CPU%u\n",
					    i, cpu);

			cpumask_setall(mask);
		}
		irq_set_affinity(i, mask);
	}
}
#endif /* CONFIG_HOTPLUG_CPU */
