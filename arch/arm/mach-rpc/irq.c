#include <linux/init.h>
#include <linux/list.h>
#include <linux/io.h>

#include <asm/mach/irq.h>
#include <asm/hardware/iomd.h>
#include <asm/irq.h>
#include <asm/io.h>

static void iomd_ack_irq_a(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << irq;
#include <asm/fiq.h>

static void iomd_ack_irq_a(struct irq_data *d)
{
	unsigned int val, mask;

	mask = 1 << d->irq;
	val = iomd_readb(IOMD_IRQMASKA);
	iomd_writeb(val & ~mask, IOMD_IRQMASKA);
	iomd_writeb(mask, IOMD_IRQCLRA);
}

static void iomd_mask_irq_a(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << irq;
static void iomd_mask_irq_a(struct irq_data *d)
{
	unsigned int val, mask;

	mask = 1 << d->irq;
	val = iomd_readb(IOMD_IRQMASKA);
	iomd_writeb(val & ~mask, IOMD_IRQMASKA);
}

static void iomd_unmask_irq_a(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << irq;
static void iomd_unmask_irq_a(struct irq_data *d)
{
	unsigned int val, mask;

	mask = 1 << d->irq;
	val = iomd_readb(IOMD_IRQMASKA);
	iomd_writeb(val | mask, IOMD_IRQMASKA);
}

static struct irq_chip iomd_a_chip = {
	.ack	= iomd_ack_irq_a,
	.mask	= iomd_mask_irq_a,
	.unmask = iomd_unmask_irq_a,
};

static void iomd_mask_irq_b(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << (irq & 7);
	.irq_ack	= iomd_ack_irq_a,
	.irq_mask	= iomd_mask_irq_a,
	.irq_unmask	= iomd_unmask_irq_a,
};

static void iomd_mask_irq_b(struct irq_data *d)
{
	unsigned int val, mask;

	mask = 1 << (d->irq & 7);
	val = iomd_readb(IOMD_IRQMASKB);
	iomd_writeb(val & ~mask, IOMD_IRQMASKB);
}

static void iomd_unmask_irq_b(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << (irq & 7);
static void iomd_unmask_irq_b(struct irq_data *d)
{
	unsigned int val, mask;

	mask = 1 << (d->irq & 7);
	val = iomd_readb(IOMD_IRQMASKB);
	iomd_writeb(val | mask, IOMD_IRQMASKB);
}

static struct irq_chip iomd_b_chip = {
	.ack	= iomd_mask_irq_b,
	.mask	= iomd_mask_irq_b,
	.unmask = iomd_unmask_irq_b,
};

static void iomd_mask_irq_dma(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << (irq & 7);
	.irq_ack	= iomd_mask_irq_b,
	.irq_mask	= iomd_mask_irq_b,
	.irq_unmask	= iomd_unmask_irq_b,
};

static void iomd_mask_irq_dma(struct irq_data *d)
{
	unsigned int val, mask;

	mask = 1 << (d->irq & 7);
	val = iomd_readb(IOMD_DMAMASK);
	iomd_writeb(val & ~mask, IOMD_DMAMASK);
}

static void iomd_unmask_irq_dma(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << (irq & 7);
static void iomd_unmask_irq_dma(struct irq_data *d)
{
	unsigned int val, mask;

	mask = 1 << (d->irq & 7);
	val = iomd_readb(IOMD_DMAMASK);
	iomd_writeb(val | mask, IOMD_DMAMASK);
}

static struct irq_chip iomd_dma_chip = {
	.ack	= iomd_mask_irq_dma,
	.mask	= iomd_mask_irq_dma,
	.unmask = iomd_unmask_irq_dma,
};

static void iomd_mask_irq_fiq(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << (irq & 7);
	.irq_ack	= iomd_mask_irq_dma,
	.irq_mask	= iomd_mask_irq_dma,
	.irq_unmask	= iomd_unmask_irq_dma,
};

static void iomd_mask_irq_fiq(struct irq_data *d)
{
	unsigned int val, mask;

	mask = 1 << (d->irq & 7);
	val = iomd_readb(IOMD_FIQMASK);
	iomd_writeb(val & ~mask, IOMD_FIQMASK);
}

static void iomd_unmask_irq_fiq(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << (irq & 7);
static void iomd_unmask_irq_fiq(struct irq_data *d)
{
	unsigned int val, mask;

	mask = 1 << (d->irq & 7);
	val = iomd_readb(IOMD_FIQMASK);
	iomd_writeb(val | mask, IOMD_FIQMASK);
}

static struct irq_chip iomd_fiq_chip = {
	.ack	= iomd_mask_irq_fiq,
	.mask	= iomd_mask_irq_fiq,
	.unmask = iomd_unmask_irq_fiq,
};

void __init rpc_init_irq(void)
{
	unsigned int irq, flags;
	.irq_ack	= iomd_mask_irq_fiq,
	.irq_mask	= iomd_mask_irq_fiq,
	.irq_unmask	= iomd_unmask_irq_fiq,
};

extern unsigned char rpc_default_fiq_start, rpc_default_fiq_end;

void __init rpc_init_irq(void)
{
	unsigned int irq, clr, set;

	iomd_writeb(0, IOMD_IRQMASKA);
	iomd_writeb(0, IOMD_IRQMASKB);
	iomd_writeb(0, IOMD_FIQMASK);
	iomd_writeb(0, IOMD_DMAMASK);

	for (irq = 0; irq < NR_IRQS; irq++) {
		flags = IRQF_VALID;

		if (irq <= 6 || (irq >= 9 && irq <= 15))
			flags |= IRQF_PROBE;

		if (irq == 21 || (irq >= 16 && irq <= 19) ||
		    irq == IRQ_KEYBOARDTX)
			flags |= IRQF_NOAUTOEN;

		switch (irq) {
		case 0 ... 7:
			set_irq_chip(irq, &iomd_a_chip);
			set_irq_handler(irq, handle_level_irq);
			set_irq_flags(irq, flags);
			break;

		case 8 ... 15:
			set_irq_chip(irq, &iomd_b_chip);
			set_irq_handler(irq, handle_level_irq);
			set_irq_flags(irq, flags);
			break;

		case 16 ... 21:
			set_irq_chip(irq, &iomd_dma_chip);
			set_irq_handler(irq, handle_level_irq);
			set_irq_flags(irq, flags);
			break;

		case 64 ... 71:
			set_irq_chip(irq, &iomd_fiq_chip);
			set_irq_flags(irq, IRQF_VALID);
	set_fiq_handler(&rpc_default_fiq_start,
		&rpc_default_fiq_end - &rpc_default_fiq_start);

	for (irq = 0; irq < NR_IRQS; irq++) {
		clr = IRQ_NOREQUEST;
		set = 0;

		if (irq <= 6 || (irq >= 9 && irq <= 15))
			clr |= IRQ_NOPROBE;

		if (irq == 21 || (irq >= 16 && irq <= 19) ||
		    irq == IRQ_KEYBOARDTX)
			set |= IRQ_NOAUTOEN;

		switch (irq) {
		case 0 ... 7:
			irq_set_chip_and_handler(irq, &iomd_a_chip,
						 handle_level_irq);
			irq_modify_status(irq, clr, set);
			break;

		case 8 ... 15:
			irq_set_chip_and_handler(irq, &iomd_b_chip,
						 handle_level_irq);
			irq_modify_status(irq, clr, set);
			break;

		case 16 ... 21:
			irq_set_chip_and_handler(irq, &iomd_dma_chip,
						 handle_level_irq);
			irq_modify_status(irq, clr, set);
			break;

		case 64 ... 71:
			irq_set_chip(irq, &iomd_fiq_chip);
			irq_modify_status(irq, clr, set);
			break;
		}
	}

	init_FIQ();
	init_FIQ(FIQ_START);
}

