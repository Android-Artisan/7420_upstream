/*
 *	Low-Level PCI Support for the SH7780
 *
 *  Dustin McIntire (dustin@sensoria.com)
 *	Derived from arch/i386/kernel/pci-*.c which bore the message:
 *	(c) 1999--2000 Martin Mares <mj@ucw.cz>
 *
 *  Ported to the new API by Paul Mundt <lethal@linux-sh.org>
 *  With cleanup by Paul van Gool <pvangool@mimotech.com>
 *
 *  May be copied or modified under the terms of the GNU General Public
 *  License.  See linux/COPYING for more information.
 *
 */
#undef DEBUG

 * Low-Level PCI Support for the SH7780
 *
 *  Copyright (C) 2005 - 2010  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include "pci-sh4.h"

#define INTC_BASE	0xffd00000
#define INTC_ICR0	(INTC_BASE+0x0)
#define INTC_ICR1	(INTC_BASE+0x1c)
#define INTC_INTPRI	(INTC_BASE+0x10)
#define INTC_INTREQ	(INTC_BASE+0x24)
#define INTC_INTMSK0	(INTC_BASE+0x44)
#define INTC_INTMSK1	(INTC_BASE+0x48)
#define INTC_INTMSK2	(INTC_BASE+0x40080)
#define INTC_INTMSKCLR0	(INTC_BASE+0x64)
#define INTC_INTMSKCLR1	(INTC_BASE+0x68)
#define INTC_INTMSKCLR2	(INTC_BASE+0x40084)
#define INTC_INT2MSKR	(INTC_BASE+0x40038)
#define INTC_INT2MSKCR	(INTC_BASE+0x4003c)

/*
 * Initialization. Try all known PCI access methods. Note that we support
 * using both PCI BIOS and direct access: in such cases, we use I/O ports
 * to access config space.
 *
 * Note that the platform specific initialization (BSC registers, and memory
 * space mapping) will be called via the platform defined function
 * pcibios_init_platform().
 */
static int __init sh7780_pci_init(void)
{
	unsigned int id;
	int ret, match = 0;

	pr_debug("PCI: Starting intialization.\n");

	ctrl_outl(0x00000001, SH7780_PCI_VCR2); /* Enable PCIC */

	/* check for SH7780/SH7780R hardware */
	id = pci_read_reg(SH7780_PCIVID);
	if ((id & 0xffff) == SH7780_VENDOR_ID) {
		switch ((id >> 16) & 0xffff) {
		case SH7763_DEVICE_ID:
		case SH7780_DEVICE_ID:
		case SH7781_DEVICE_ID:
		case SH7785_DEVICE_ID:
			match = 1;
			break;
		}
	}

	if (unlikely(!match)) {
		printk(KERN_ERR "PCI: This is not an SH7780 (%x)\n", id);
		return -ENODEV;
	}

	/* Setup the INTC */
	if (mach_is_7780se()) {
		/* ICR0: IRL=use separately */
		ctrl_outl(0x00C00020, INTC_ICR0);
		/* ICR1: detect low level(for 2ndcut) */
		ctrl_outl(0xAAAA0000, INTC_ICR1);
		/* INTPRI: priority=3(all) */
		ctrl_outl(0x33333333, INTC_INTPRI);
	}

	if ((ret = sh4_pci_check_direct()) != 0)
		return ret;

	return pcibios_init_platform();
}
core_initcall(sh7780_pci_init);

int __init sh7780_pcic_init(struct sh4_pci_address_map *map)
{
	u32 word;

	/*
	 * This code is unused for some boards as it is done in the
	 * bootloader and doing it here means the MAC addresses loaded
	 * by the bootloader get lost.
	 */
	if (!(map->flags & SH4_PCIC_NO_RESET)) {
		/* toggle PCI reset pin */
		word = SH4_PCICR_PREFIX | SH4_PCICR_PRST;
		pci_write_reg(word, SH4_PCICR);
		/* Wait for a long time... not 1 sec. but long enough */
		mdelay(100);
		word = SH4_PCICR_PREFIX;
		pci_write_reg(word, SH4_PCICR);
	}

	/* set the command/status bits to:
	 * Wait Cycle Control + Parity Enable + Bus Master +
	 * Mem space enable
	 */
	pci_write_reg(0x00000046, SH7780_PCICMD);

	/* define this host as the host bridge */
	word = PCI_BASE_CLASS_BRIDGE << 24;
	pci_write_reg(word, SH7780_PCIRID);

	/* Set IO and Mem windows to local address
	 * Make PCI and local address the same for easy 1 to 1 mapping
	 * Window0 = map->window0.size @ non-cached area base = SDRAM
	 * Window1 = map->window1.size @ cached area base = SDRAM
	 */
	word = ((map->window0.size - 1) & 0x1ff00001) | 0x01;
	pci_write_reg(0x07f00001, SH4_PCILSR0);
	word = ((map->window1.size - 1) & 0x1ff00001) | 0x01;
	pci_write_reg(0x00000001, SH4_PCILSR1);
	/* Set the values on window 0 PCI config registers */
	word = P2SEGADDR(map->window0.base);
	pci_write_reg(0xa8000000, SH4_PCILAR0);
	pci_write_reg(0x08000000, SH7780_PCIMBAR0);
	/* Set the values on window 1 PCI config registers */
	word = P2SEGADDR(map->window1.base);
	pci_write_reg(0x00000000, SH4_PCILAR1);
	pci_write_reg(0x00000000, SH7780_PCIMBAR1);

	/* Map IO space into PCI IO window
	 * The IO window is 64K-PCIBIOS_MIN_IO in size
	 * IO addresses will be translated to the
	 * PCI IO window base address
	 */
	pr_debug("PCI: Mapping IO address 0x%x - 0x%x to base 0x%x\n",
		 PCIBIOS_MIN_IO, (64 << 10),
		 SH7780_PCI_IO_BASE + PCIBIOS_MIN_IO);

	/* NOTE: I'm ignoring the PCI error IRQs for now..
	 * TODO: add support for the internal error interrupts and
	 * DMA interrupts...
	 */

	/* Apply any last-minute PCIC fixups */
	pci_fixup_pcic();

	/* SH7780 init done, set central function init complete */
	/* use round robin mode to stop a device starving/overruning */
	word = SH4_PCICR_PREFIX | SH4_PCICR_CFIN | SH4_PCICR_FTO;
	pci_write_reg(word, SH4_PCICR);

	return 1;
}
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/irq.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/log2.h>
#include "pci-sh4.h"
#include <asm/mmu.h>
#include <asm/sizes.h>

#if defined(CONFIG_CPU_BIG_ENDIAN)
# define PCICR_ENDIANNESS SH4_PCICR_BSWP
#else
# define PCICR_ENDIANNESS 0
#endif


static struct resource sh7785_pci_resources[] = {
	{
		.name	= "PCI IO",
		.start	= 0x1000,
		.end	= SZ_4M - 1,
		.flags	= IORESOURCE_IO,
	}, {
		.name	= "PCI MEM 0",
		.start	= 0xfd000000,
		.end	= 0xfd000000 + SZ_16M - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.name	= "PCI MEM 1",
		.start	= 0x10000000,
		.end	= 0x10000000 + SZ_64M - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		/*
		 * 32-bit only resources must be last.
		 */
		.name	= "PCI MEM 2",
		.start	= 0xc0000000,
		.end	= 0xc0000000 + SZ_512M - 1,
		.flags	= IORESOURCE_MEM | IORESOURCE_MEM_32BIT,
	},
};

static struct pci_channel sh7780_pci_controller = {
	.pci_ops	= &sh4_pci_ops,
	.resources	= sh7785_pci_resources,
	.nr_resources	= ARRAY_SIZE(sh7785_pci_resources),
	.io_offset	= 0,
	.mem_offset	= 0,
	.io_map_base	= 0xfe200000,
	.serr_irq	= evt2irq(0xa00),
	.err_irq	= evt2irq(0xaa0),
};

struct pci_errors {
	unsigned int	mask;
	const char	*str;
} pci_arbiter_errors[] = {
	{ SH4_PCIAINT_MBKN,	"master broken" },
	{ SH4_PCIAINT_TBTO,	"target bus time out" },
	{ SH4_PCIAINT_MBTO,	"master bus time out" },
	{ SH4_PCIAINT_TABT,	"target abort" },
	{ SH4_PCIAINT_MABT,	"master abort" },
	{ SH4_PCIAINT_RDPE,	"read data parity error" },
	{ SH4_PCIAINT_WDPE,	"write data parity error" },
}, pci_interrupt_errors[] = {
	{ SH4_PCIINT_MLCK,	"master lock error" },
	{ SH4_PCIINT_TABT,	"target-target abort" },
	{ SH4_PCIINT_TRET,	"target retry time out" },
	{ SH4_PCIINT_MFDE,	"master function disable error" },
	{ SH4_PCIINT_PRTY,	"address parity error" },
	{ SH4_PCIINT_SERR,	"SERR" },
	{ SH4_PCIINT_TWDP,	"data parity error for target write" },
	{ SH4_PCIINT_TRDP,	"PERR detected for target read" },
	{ SH4_PCIINT_MTABT,	"target abort for master" },
	{ SH4_PCIINT_MMABT,	"master abort for master" },
	{ SH4_PCIINT_MWPD,	"master write data parity error" },
	{ SH4_PCIINT_MRPD,	"master read data parity error" },
};

static irqreturn_t sh7780_pci_err_irq(int irq, void *dev_id)
{
	struct pci_channel *hose = dev_id;
	unsigned long addr;
	unsigned int status;
	unsigned int cmd;
	int i;

	addr = __raw_readl(hose->reg_base + SH4_PCIALR);

	/*
	 * Handle status errors.
	 */
	status = __raw_readw(hose->reg_base + PCI_STATUS);
	if (status & (PCI_STATUS_PARITY |
		      PCI_STATUS_DETECTED_PARITY |
		      PCI_STATUS_SIG_TARGET_ABORT |
		      PCI_STATUS_REC_TARGET_ABORT |
		      PCI_STATUS_REC_MASTER_ABORT)) {
		cmd = pcibios_handle_status_errors(addr, status, hose);
		if (likely(cmd))
			__raw_writew(cmd, hose->reg_base + PCI_STATUS);
	}

	/*
	 * Handle arbiter errors.
	 */
	status = __raw_readl(hose->reg_base + SH4_PCIAINT);
	for (i = cmd = 0; i < ARRAY_SIZE(pci_arbiter_errors); i++) {
		if (status & pci_arbiter_errors[i].mask) {
			printk(KERN_DEBUG "PCI: %s, addr=%08lx\n",
			       pci_arbiter_errors[i].str, addr);
			cmd |= pci_arbiter_errors[i].mask;
		}
	}
	__raw_writel(cmd, hose->reg_base + SH4_PCIAINT);

	/*
	 * Handle the remaining PCI errors.
	 */
	status = __raw_readl(hose->reg_base + SH4_PCIINT);
	for (i = cmd = 0; i < ARRAY_SIZE(pci_interrupt_errors); i++) {
		if (status & pci_interrupt_errors[i].mask) {
			printk(KERN_DEBUG "PCI: %s, addr=%08lx\n",
			       pci_interrupt_errors[i].str, addr);
			cmd |= pci_interrupt_errors[i].mask;
		}
	}
	__raw_writel(cmd, hose->reg_base + SH4_PCIINT);

	return IRQ_HANDLED;
}

static irqreturn_t sh7780_pci_serr_irq(int irq, void *dev_id)
{
	struct pci_channel *hose = dev_id;

	printk(KERN_DEBUG "PCI: system error received: ");
	pcibios_report_status(PCI_STATUS_SIG_SYSTEM_ERROR, 1);
	printk("\n");

	/* Deassert SERR */
	__raw_writel(SH4_PCIINTM_SDIM, hose->reg_base + SH4_PCIINTM);

	/* Back off the IRQ for awhile */
	disable_irq_nosync(irq);
	hose->serr_timer.expires = jiffies + HZ;
	add_timer(&hose->serr_timer);

	return IRQ_HANDLED;
}

static int __init sh7780_pci_setup_irqs(struct pci_channel *hose)
{
	int ret;

	/* Clear out PCI arbiter IRQs */
	__raw_writel(0, hose->reg_base + SH4_PCIAINT);

	/* Clear all error conditions */
	__raw_writew(PCI_STATUS_DETECTED_PARITY  | \
		     PCI_STATUS_SIG_SYSTEM_ERROR | \
		     PCI_STATUS_REC_MASTER_ABORT | \
		     PCI_STATUS_REC_TARGET_ABORT | \
		     PCI_STATUS_SIG_TARGET_ABORT | \
		     PCI_STATUS_PARITY, hose->reg_base + PCI_STATUS);

	ret = request_irq(hose->serr_irq, sh7780_pci_serr_irq, 0,
			  "PCI SERR interrupt", hose);
	if (unlikely(ret)) {
		printk(KERN_ERR "PCI: Failed hooking SERR IRQ\n");
		return ret;
	}

	/*
	 * The PCI ERR IRQ needs to be IRQF_SHARED since all of the power
	 * down IRQ vectors are routed through the ERR IRQ vector. We
	 * only request_irq() once as there is only a single masking
	 * source for multiple events.
	 */
	ret = request_irq(hose->err_irq, sh7780_pci_err_irq, IRQF_SHARED,
			  "PCI ERR interrupt", hose);
	if (unlikely(ret)) {
		free_irq(hose->serr_irq, hose);
		return ret;
	}

	/* Unmask all of the arbiter IRQs. */
	__raw_writel(SH4_PCIAINT_MBKN | SH4_PCIAINT_TBTO | SH4_PCIAINT_MBTO | \
		     SH4_PCIAINT_TABT | SH4_PCIAINT_MABT | SH4_PCIAINT_RDPE | \
		     SH4_PCIAINT_WDPE, hose->reg_base + SH4_PCIAINTM);

	/* Unmask all of the PCI IRQs */
	__raw_writel(SH4_PCIINTM_TTADIM  | SH4_PCIINTM_TMTOIM  | \
		     SH4_PCIINTM_MDEIM   | SH4_PCIINTM_APEDIM  | \
		     SH4_PCIINTM_SDIM    | SH4_PCIINTM_DPEITWM | \
		     SH4_PCIINTM_PEDITRM | SH4_PCIINTM_TADIMM  | \
		     SH4_PCIINTM_MADIMM  | SH4_PCIINTM_MWPDIM  | \
		     SH4_PCIINTM_MRDPEIM, hose->reg_base + SH4_PCIINTM);

	return ret;
}

static inline void __init sh7780_pci_teardown_irqs(struct pci_channel *hose)
{
	free_irq(hose->err_irq, hose);
	free_irq(hose->serr_irq, hose);
}

static void __init sh7780_pci66_init(struct pci_channel *hose)
{
	unsigned int tmp;

	if (!pci_is_66mhz_capable(hose, 0, 0))
		return;

	/* Enable register access */
	tmp = __raw_readl(hose->reg_base + SH4_PCICR);
	tmp |= SH4_PCICR_PREFIX;
	__raw_writel(tmp, hose->reg_base + SH4_PCICR);

	/* Enable 66MHz operation */
	tmp = __raw_readw(hose->reg_base + PCI_STATUS);
	tmp |= PCI_STATUS_66MHZ;
	__raw_writew(tmp, hose->reg_base + PCI_STATUS);

	/* Done */
	tmp = __raw_readl(hose->reg_base + SH4_PCICR);
	tmp |= SH4_PCICR_PREFIX | SH4_PCICR_CFIN;
	__raw_writel(tmp, hose->reg_base + SH4_PCICR);
}

static int __init sh7780_pci_init(void)
{
	struct pci_channel *chan = &sh7780_pci_controller;
	phys_addr_t memphys;
	size_t memsize;
	unsigned int id;
	const char *type;
	int ret, i;

	printk(KERN_NOTICE "PCI: Starting initialization.\n");

	chan->reg_base = 0xfe040000;

	/* Enable CPU access to the PCIC registers. */
	__raw_writel(PCIECR_ENBL, PCIECR);

	/* Reset */
	__raw_writel(SH4_PCICR_PREFIX | SH4_PCICR_PRST | PCICR_ENDIANNESS,
		     chan->reg_base + SH4_PCICR);

	/*
	 * Wait for it to come back up. The spec says to allow for up to
	 * 1 second after toggling the reset pin, but in practice 100ms
	 * is more than enough.
	 */
	mdelay(100);

	id = __raw_readw(chan->reg_base + PCI_VENDOR_ID);
	if (id != PCI_VENDOR_ID_RENESAS) {
		printk(KERN_ERR "PCI: Unknown vendor ID 0x%04x.\n", id);
		return -ENODEV;
	}

	id = __raw_readw(chan->reg_base + PCI_DEVICE_ID);
	type = (id == PCI_DEVICE_ID_RENESAS_SH7763) ? "SH7763" :
	       (id == PCI_DEVICE_ID_RENESAS_SH7780) ? "SH7780" :
	       (id == PCI_DEVICE_ID_RENESAS_SH7781) ? "SH7781" :
	       (id == PCI_DEVICE_ID_RENESAS_SH7785) ? "SH7785" :
					  NULL;
	if (unlikely(!type)) {
		printk(KERN_ERR "PCI: Found an unsupported Renesas host "
		       "controller, device id 0x%04x.\n", id);
		return -EINVAL;
	}

	printk(KERN_NOTICE "PCI: Found a Renesas %s host "
	       "controller, revision %d.\n", type,
	       __raw_readb(chan->reg_base + PCI_REVISION_ID));

	/*
	 * Now throw it in to register initialization mode and
	 * start the real work.
	 */
	__raw_writel(SH4_PCICR_PREFIX | PCICR_ENDIANNESS,
		     chan->reg_base + SH4_PCICR);

	memphys = __pa(memory_start);
	memsize = roundup_pow_of_two(memory_end - memory_start);

	/*
	 * If there's more than 512MB of memory, we need to roll over to
	 * LAR1/LSR1.
	 */
	if (memsize > SZ_512M) {
		__raw_writel(memphys + SZ_512M, chan->reg_base + SH4_PCILAR1);
		__raw_writel((((memsize - SZ_512M) - SZ_1M) & 0x1ff00000) | 1,
			     chan->reg_base + SH4_PCILSR1);
		memsize = SZ_512M;
	} else {
		/*
		 * Otherwise just zero it out and disable it.
		 */
		__raw_writel(0, chan->reg_base + SH4_PCILAR1);
		__raw_writel(0, chan->reg_base + SH4_PCILSR1);
	}

	/*
	 * LAR0/LSR0 covers up to the first 512MB, which is enough to
	 * cover all of lowmem on most platforms.
	 */
	__raw_writel(memphys, chan->reg_base + SH4_PCILAR0);
	__raw_writel(((memsize - SZ_1M) & 0x1ff00000) | 1,
		     chan->reg_base + SH4_PCILSR0);

	/*
	 * Hook up the ERR and SERR IRQs.
	 */
	ret = sh7780_pci_setup_irqs(chan);
	if (unlikely(ret))
		return ret;

	/*
	 * Disable the cache snoop controller for non-coherent DMA.
	 */
	__raw_writel(0, chan->reg_base + SH7780_PCICSCR0);
	__raw_writel(0, chan->reg_base + SH7780_PCICSAR0);
	__raw_writel(0, chan->reg_base + SH7780_PCICSCR1);
	__raw_writel(0, chan->reg_base + SH7780_PCICSAR1);

	/*
	 * Setup the memory BARs
	 */
	for (i = 1; i < chan->nr_resources; i++) {
		struct resource *res = chan->resources + i;
		resource_size_t size;

		if (unlikely(res->flags & IORESOURCE_IO))
			continue;

		/*
		 * Make sure we're in the right physical addressing mode
		 * for dealing with the resource.
		 */
		if ((res->flags & IORESOURCE_MEM_32BIT) && __in_29bit_mode()) {
			chan->nr_resources--;
			continue;
		}

		size = resource_size(res);

		/*
		 * The MBMR mask is calculated in units of 256kB, which
		 * keeps things pretty simple.
		 */
		__raw_writel(((roundup_pow_of_two(size) / SZ_256K) - 1) << 18,
			     chan->reg_base + SH7780_PCIMBMR(i - 1));
		__raw_writel(res->start, chan->reg_base + SH7780_PCIMBR(i - 1));
	}

	/*
	 * And I/O.
	 */
	__raw_writel(0, chan->reg_base + PCI_BASE_ADDRESS_0);
	__raw_writel(0, chan->reg_base + SH7780_PCIIOBR);
	__raw_writel(0, chan->reg_base + SH7780_PCIIOBMR);

	__raw_writew(PCI_COMMAND_SERR   | PCI_COMMAND_WAIT   | \
		     PCI_COMMAND_PARITY | PCI_COMMAND_MASTER | \
		     PCI_COMMAND_MEMORY, chan->reg_base + PCI_COMMAND);

	/*
	 * Initialization mode complete, release the control register and
	 * enable round robin mode to stop device overruns/starvation.
	 */
	__raw_writel(SH4_PCICR_PREFIX | SH4_PCICR_CFIN | SH4_PCICR_FTO |
		     PCICR_ENDIANNESS,
		     chan->reg_base + SH4_PCICR);

	ret = register_pci_controller(chan);
	if (unlikely(ret))
		goto err;

	sh7780_pci66_init(chan);

	printk(KERN_NOTICE "PCI: Running at %dMHz.\n",
	       (__raw_readw(chan->reg_base + PCI_STATUS) & PCI_STATUS_66MHZ) ?
	       66 : 33);

	return 0;

err:
	sh7780_pci_teardown_irqs(chan);
	return ret;
}
arch_initcall(sh7780_pci_init);
