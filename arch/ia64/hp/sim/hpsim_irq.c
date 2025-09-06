/*
 * Platform dependent support for HP simulator.
 *
 * Copyright (C) 1998-2001 Hewlett-Packard Co
 * Copyright (C) 1998-2001 David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/irq.h>

static unsigned int
hpsim_irq_startup (unsigned int irq)
#include "hpsim_ssc.h"

static unsigned int
hpsim_irq_startup(struct irq_data *data)
{
	return 0;
}

static void
hpsim_irq_noop (unsigned int irq)
{
}

static void
hpsim_set_affinity_noop (unsigned int a, cpumask_t b)
{
}

static struct hw_interrupt_type irq_type_hp_sim = {
	.name =		"hpsim",
	.startup =	hpsim_irq_startup,
	.shutdown =	hpsim_irq_noop,
	.enable =	hpsim_irq_noop,
	.disable =	hpsim_irq_noop,
	.ack =		hpsim_irq_noop,
	.end =		hpsim_irq_noop,
	.set_affinity =	hpsim_set_affinity_noop,
};

void __init
hpsim_irq_init (void)
{
	irq_desc_t *idesc;
	int i;

	for (i = 0; i < NR_IRQS; ++i) {
		idesc = irq_desc + i;
		if (idesc->chip == &no_irq_type)
			idesc->chip = &irq_type_hp_sim;
	}
hpsim_irq_noop(struct irq_data *data)
{
}

static int
hpsim_set_affinity_noop(struct irq_data *d, const struct cpumask *b, bool f)
{
	return 0;
}

static struct irq_chip irq_type_hp_sim = {
	.name =			"hpsim",
	.irq_startup =		hpsim_irq_startup,
	.irq_shutdown =		hpsim_irq_noop,
	.irq_enable =		hpsim_irq_noop,
	.irq_disable =		hpsim_irq_noop,
	.irq_ack =		hpsim_irq_noop,
	.irq_set_affinity =	hpsim_set_affinity_noop,
};

static void hpsim_irq_set_chip(int irq)
{
	struct irq_chip *chip = irq_get_chip(irq);

	if (chip == &no_irq_chip)
		irq_set_chip(irq, &irq_type_hp_sim);
}

static void hpsim_connect_irq(int intr, int irq)
{
	ia64_ssc(intr, irq, 0, 0, SSC_CONNECT_INTERRUPT);
}

int hpsim_get_irq(int intr)
{
	int irq = assign_irq_vector(AUTO_ASSIGN);

	if (irq >= 0) {
		hpsim_irq_set_chip(irq);
		irq_set_handler(irq, handle_simple_irq);
		hpsim_connect_irq(intr, irq);
	}

	return irq;
}

void __init
hpsim_irq_init (void)
{
	int i;

	for_each_active_irq(i)
		hpsim_irq_set_chip(i);
}
