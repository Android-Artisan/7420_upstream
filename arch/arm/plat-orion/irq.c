/*
 * arch/arm/plat-orion/irq.c
 *
 * Marvell Orion SoC IRQ handling.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <plat/irq.h>

static void orion_irq_mask(u32 irq)
{
	void __iomem *maskaddr = get_irq_chip_data(irq);
	u32 mask;

	mask = readl(maskaddr);
	mask &= ~(1 << (irq & 31));
	writel(mask, maskaddr);
}

static void orion_irq_unmask(u32 irq)
{
	void __iomem *maskaddr = get_irq_chip_data(irq);
	u32 mask;

	mask = readl(maskaddr);
	mask |= 1 << (irq & 31);
	writel(mask, maskaddr);
}

static struct irq_chip orion_irq_chip = {
	.name		= "orion_irq",
	.mask		= orion_irq_mask,
	.mask_ack	= orion_irq_mask,
	.unmask		= orion_irq_unmask,
};

void __init orion_irq_init(unsigned int irq_start, void __iomem *maskaddr)
{
	unsigned int i;
#include <linux/irqdomain.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <asm/exception.h>
#include <plat/irq.h>
#include <plat/orion-gpio.h>
#include <mach/bridge-regs.h>

void __init orion_irq_init(unsigned int irq_start, void __iomem *maskaddr)
{
	struct irq_chip_generic *gc;
	struct irq_chip_type *ct;

	/*
	 * Mask all interrupts initially.
	 */
	writel(0, maskaddr);

	/*
	 * Register IRQ sources.
	 */
	for (i = 0; i < 32; i++) {
		unsigned int irq = irq_start + i;

		set_irq_chip(irq, &orion_irq_chip);
		set_irq_chip_data(irq, maskaddr);
		set_irq_handler(irq, handle_level_irq);
		irq_desc[irq].status |= IRQ_LEVEL;
		set_irq_flags(irq, IRQF_VALID);
	}
	gc = irq_alloc_generic_chip("orion_irq", 1, irq_start, maskaddr,
				    handle_level_irq);
	ct = gc->chip_types;
	ct->chip.irq_mask = irq_gc_mask_clr_bit;
	ct->chip.irq_unmask = irq_gc_mask_set_bit;
	irq_setup_generic_chip(gc, IRQ_MSK(32), IRQ_GC_INIT_MASK_CACHE,
			       IRQ_NOREQUEST, IRQ_LEVEL | IRQ_NOPROBE);
}
