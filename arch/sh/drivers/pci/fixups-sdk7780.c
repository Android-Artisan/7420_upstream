/*
 * arch/sh/drivers/pci/fixups-sdk7780.c
 *
 * PCI fixups for the SDK7780SE03
 *
 * Copyright (C) 2003  Lineo uSolutions, Inc.
 * Copyright (C) 2004 - 2006  Paul Mundt
 * Copyright (C) 2006  Nobuhiro Iwamatsu
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/pci.h>
#include "pci-sh4.h"
#include <asm/io.h>

int pci_fixup_pcic(void)
{
	ctrl_outl(0x00000001, SH7780_PCI_VCR2);

	/* Enable all interrupts, so we know what to fix */
	pci_write_reg(0x0000C3FF, SH7780_PCIIMR);
	pci_write_reg(0x0000380F, SH7780_PCIAINTM);

	/* Set up standard PCI config registers */
	pci_write_reg(0xFB00, SH7780_PCISTATUS);
	pci_write_reg(0x0047, SH7780_PCICMD);
	pci_write_reg(0x00, SH7780_PCIPIF);
	pci_write_reg(0x00, SH7780_PCISUB);
	pci_write_reg(0x06, SH7780_PCIBCC);
	pci_write_reg(0x1912, SH7780_PCISVID);
	pci_write_reg(0x0001, SH7780_PCISID);

	pci_write_reg(0x08000000, SH7780_PCIMBAR0);	/* PCI */
	pci_write_reg(0x08000000, SH7780_PCILAR0);	/* SHwy */
	pci_write_reg(0x07F00001, SH7780_PCILSR);	/* size 128M w/ MBAR */

	pci_write_reg(0x00000000, SH7780_PCIMBAR1);
	pci_write_reg(0x00000000, SH7780_PCILAR1);
	pci_write_reg(0x00000000, SH7780_PCILSR1);

	pci_write_reg(0xAB000801, SH7780_PCIIBAR);

	/*
	 * Set the MBR so PCI address is one-to-one with window,
	 * meaning all calls go straight through... use ifdef to
	 * catch erroneous assumption.
	 */
	pci_write_reg(0xFD000000 , SH7780_PCIMBR0);
	pci_write_reg(0x00FC0000 , SH7780_PCIMBMR0);	/* 16M */

	/* Set IOBR for window containing area specified in pci.h */
	pci_write_reg(PCIBIOS_MIN_IO & ~(SH7780_PCI_IO_SIZE-1), SH7780_PCIIOBR);
	pci_write_reg((SH7780_PCI_IO_SIZE-1) & (7 << 18), SH7780_PCIIOBMR);

	pci_write_reg(0xA5000C01, SH7780_PCICR);

	return 0;
#include <linux/io.h>
#include <linux/sh_intc.h>
#include "pci-sh4.h"

#define IRQ_INTA	evt2irq(0xa20)
#define IRQ_INTB	evt2irq(0xa40)
#define IRQ_INTC	evt2irq(0xa60)
#define IRQ_INTD	evt2irq(0xa80)

/* IDSEL [16][17][18][19][20][21][22][23][24][25][26][27][28][29][30][31] */
static char sdk7780_irq_tab[4][16] __initdata = {
	/* INTA */
	{ IRQ_INTA, IRQ_INTD, IRQ_INTC, IRQ_INTD, -1, -1, -1, -1, -1, -1,
	  -1, -1, -1, -1, -1, -1 },
	/* INTB */
	{ IRQ_INTB, IRQ_INTA, -1, IRQ_INTA, -1, -1, -1, -1, -1, -1, -1, -1,
	  -1, -1, -1, -1 },
	/* INTC */
	{ IRQ_INTC, IRQ_INTB, -1, IRQ_INTB, -1, -1, -1, -1, -1, -1, -1, -1,
	  -1, -1, -1, -1 },
	/* INTD */
	{ IRQ_INTD, IRQ_INTC, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	  -1, -1, -1 },
};

int __init pcibios_map_platform_irq(const struct pci_dev *pdev, u8 slot, u8 pin)
{
       return sdk7780_irq_tab[pin-1][slot];
}
