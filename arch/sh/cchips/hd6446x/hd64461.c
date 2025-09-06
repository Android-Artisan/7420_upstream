/*
 *	Copyright (C) 2000 YAEGASHI Takeshi
 *	Hitachi HD64461 companion chip support
 */

#include <linux/sched.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <asm/io.h>
#include <linux/io.h>
#include <asm/irq.h>
#include <asm/hd64461.h>

/* This belongs in cpu specific */
#define INTC_ICR1 0xA4140010UL

static void disable_hd64461_irq(unsigned int irq)
{
	unsigned short nimr;
	unsigned short mask = 1 << (irq - HD64461_IRQBASE);

	nimr = inw(HD64461_NIMR);
	nimr |= mask;
	outw(nimr, HD64461_NIMR);
}

static void enable_hd64461_irq(unsigned int irq)
{
	unsigned short nimr;
	unsigned short mask = 1 << (irq - HD64461_IRQBASE);

	nimr = inw(HD64461_NIMR);
	nimr &= ~mask;
	outw(nimr, HD64461_NIMR);
}

static void mask_and_ack_hd64461(unsigned int irq)
{
	disable_hd64461_irq(irq);
#ifdef CONFIG_HD64461_ENABLER
	if (irq == HD64461_IRQBASE + 13)
		outb(0x00, HD64461_PCC1CSCR);
#endif
}

static void end_hd64461_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		enable_hd64461_irq(irq);
}

static unsigned int startup_hd64461_irq(unsigned int irq)
{
	enable_hd64461_irq(irq);
	return 0;
}

static void shutdown_hd64461_irq(unsigned int irq)
{
	disable_hd64461_irq(irq);
}

static struct hw_interrupt_type hd64461_irq_type = {
	.typename	= "HD64461-IRQ",
	.startup	= startup_hd64461_irq,
	.shutdown	= shutdown_hd64461_irq,
	.enable		= enable_hd64461_irq,
	.disable	= disable_hd64461_irq,
	.ack		= mask_and_ack_hd64461,
	.end		= end_hd64461_irq,
};

static irqreturn_t hd64461_interrupt(int irq, void *dev_id)
{
	printk(KERN_INFO
	       "HD64461: spurious interrupt, nirr: 0x%x nimr: 0x%x\n",
	       inw(HD64461_NIRR), inw(HD64461_NIMR));

	return IRQ_NONE;
}

static struct {
	int (*func) (int, void *);
	void *dev;
} hd64461_demux[HD64461_IRQ_NUM];

void hd64461_register_irq_demux(int irq,
				int (*demux) (int irq, void *dev), void *dev)
{
	hd64461_demux[irq - HD64461_IRQBASE].func = demux;
	hd64461_demux[irq - HD64461_IRQBASE].dev = dev;
}

EXPORT_SYMBOL(hd64461_register_irq_demux);

void hd64461_unregister_irq_demux(int irq)
{
	hd64461_demux[irq - HD64461_IRQBASE].func = 0;
}

EXPORT_SYMBOL(hd64461_unregister_irq_demux);

int hd64461_irq_demux(int irq)
{
	if (irq == CONFIG_HD64461_IRQ) {
		unsigned short bit;
		unsigned short nirr = inw(HD64461_NIRR);
		unsigned short nimr = inw(HD64461_NIMR);
		int i;

		nirr &= ~nimr;
		for (bit = 1, i = 0; i < 16; bit <<= 1, i++)
			if (nirr & bit)
				break;
		if (i == 16)
			irq = CONFIG_HD64461_IRQ;
		else {
			irq = HD64461_IRQBASE + i;
			if (hd64461_demux[i].func != 0) {
				irq = hd64461_demux[i].func(irq, hd64461_demux[i].dev);
			}
		}
	}
	return irq;
}

static struct irqaction irq0 = {
	.handler = hd64461_interrupt,
	.flags = IRQF_DISABLED,
	.mask = CPU_MASK_NONE,
	.name = "HD64461",
};

int __init setup_hd64461(void)
{
	int i;

	if (!MACH_HD64461)
		return 0;

	printk(KERN_INFO
	       "HD64461 configured at 0x%x on irq %d(mapped into %d to %d)\n",
	       CONFIG_HD64461_IOBASE, CONFIG_HD64461_IRQ, HD64461_IRQBASE,
	       HD64461_IRQBASE + 15);

#if defined(CONFIG_CPU_SUBTYPE_SH7709)	/* Should be at processor specific part.. */
	outw(0x2240, INTC_ICR1);
#endif
	outw(0xffff, HD64461_NIMR);

	/*  IRQ 80 -> 95 belongs to HD64461  */
	for (i = HD64461_IRQBASE; i < HD64461_IRQBASE + 16; i++) {
		irq_desc[i].chip = &hd64461_irq_type;
	}

	setup_irq(CONFIG_HD64461_IRQ, &irq0);

#ifdef CONFIG_HD64461_ENABLER
	printk(KERN_INFO "HD64461: enabling PCMCIA devices\n");
	outb(0x4c, HD64461_PCC1CSCIER);
	outb(0x00, HD64461_PCC1CSCR);
static void hd64461_mask_irq(struct irq_data *data)
{
	unsigned int irq = data->irq;
	unsigned short nimr;
	unsigned short mask = 1 << (irq - HD64461_IRQBASE);

	nimr = __raw_readw(HD64461_NIMR);
	nimr |= mask;
	__raw_writew(nimr, HD64461_NIMR);
}

static void hd64461_unmask_irq(struct irq_data *data)
{
	unsigned int irq = data->irq;
	unsigned short nimr;
	unsigned short mask = 1 << (irq - HD64461_IRQBASE);

	nimr = __raw_readw(HD64461_NIMR);
	nimr &= ~mask;
	__raw_writew(nimr, HD64461_NIMR);
}

static void hd64461_mask_and_ack_irq(struct irq_data *data)
{
	hd64461_mask_irq(data);

#ifdef CONFIG_HD64461_ENABLER
	if (data->irq == HD64461_IRQBASE + 13)
		__raw_writeb(0x00, HD64461_PCC1CSCR);
#endif
}

static struct irq_chip hd64461_irq_chip = {
	.name		= "HD64461-IRQ",
	.irq_mask	= hd64461_mask_irq,
	.irq_mask_ack	= hd64461_mask_and_ack_irq,
	.irq_unmask	= hd64461_unmask_irq,
};

static void hd64461_irq_demux(struct irq_desc *desc)
{
	unsigned short intv = __raw_readw(HD64461_NIRR);
	unsigned int ext_irq = HD64461_IRQBASE;

	intv &= (1 << HD64461_IRQ_NUM) - 1;

	for (; intv; intv >>= 1, ext_irq++) {
		if (!(intv & 1))
			continue;

		generic_handle_irq(ext_irq);
	}
}

int __init setup_hd64461(void)
{
	int irq_base, i;

	printk(KERN_INFO
	       "HD64461 configured at 0x%x on irq %d(mapped into %d to %d)\n",
	       HD64461_IOBASE, CONFIG_HD64461_IRQ, HD64461_IRQBASE,
	       HD64461_IRQBASE + 15);

/* Should be at processor specific part.. */
#if defined(CONFIG_CPU_SUBTYPE_SH7709)
	__raw_writew(0x2240, INTC_ICR1);
#endif
	__raw_writew(0xffff, HD64461_NIMR);

	irq_base = irq_alloc_descs(HD64461_IRQBASE, HD64461_IRQBASE, 16, -1);
	if (IS_ERR_VALUE(irq_base)) {
		pr_err("%s: failed hooking irqs for HD64461\n", __func__);
		return irq_base;
	}

	for (i = 0; i < 16; i++)
		irq_set_chip_and_handler(irq_base + i, &hd64461_irq_chip,
					 handle_level_irq);

	irq_set_chained_handler(CONFIG_HD64461_IRQ, hd64461_irq_demux);
	irq_set_irq_type(CONFIG_HD64461_IRQ, IRQ_TYPE_LEVEL_LOW);

#ifdef CONFIG_HD64461_ENABLER
	printk(KERN_INFO "HD64461: enabling PCMCIA devices\n");
	__raw_writeb(0x4c, HD64461_PCC1CSCIER);
	__raw_writeb(0x00, HD64461_PCC1CSCR);
#endif

	return 0;
}

module_init(setup_hd64461);
