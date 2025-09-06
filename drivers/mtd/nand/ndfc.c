/*
 *  drivers/mtd/ndfc.c
 *
 *  Overview:
 *   Platform independend driver for NDFC (NanD Flash Controller)
 *   integrated into EP440 cores
 *
 *  Author: Thomas Gleixner
 *
 *  Copyright 2006 IBM
 *  Overview:
 *   Platform independent driver for NDFC (NanD Flash Controller)
 *   integrated into EP440 cores
 *
 *   Ported to an OF platform driver by Sean MacLennan
 *
 *   The NDFC supports multiple chips, but this driver only supports a
 *   single chip since I do not have access to any boards with
 *   multiple chips.
 *
 *  Author: Thomas Gleixner
 *
 *  Copyright 2006 IBM
 *  Copyright 2008 PIKA Technologies
 *    Sean MacLennan <smaclennan@pikatech.com>
 *
 *  This program is free software; you can redistribute	 it and/or modify it
 *  under  the terms of	 the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the	License, or (at your
 *  option) any later version.
 *
 */
#include <linux/module.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/ndfc.h>
#include <linux/mtd/mtd.h>
#include <linux/platform_device.h>

#include <asm/io.h>
#ifdef CONFIG_40x
#include <asm/ibm405.h>
#else
#include <asm/ibm44x.h>
#endif

struct ndfc_nand_mtd {
	struct mtd_info			mtd;
	struct nand_chip		chip;
	struct platform_nand_chip	*pl_chip;
};

static struct ndfc_nand_mtd ndfc_mtd[NDFC_MAX_BANKS];

struct ndfc_controller {
	void __iomem		*ndfcbase;
	struct nand_hw_control	ndfc_control;
	atomic_t		childs_active;
};

static struct ndfc_controller ndfc_ctrl;
#include <linux/slab.h>
#include <linux/mtd/mtd.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <asm/io.h>

#define NDFC_MAX_CS    4

struct ndfc_controller {
	struct platform_device *ofdev;
	void __iomem *ndfcbase;
	struct mtd_info mtd;
	struct nand_chip chip;
	int chip_select;
	struct nand_hw_control ndfc_control;
};

static struct ndfc_controller ndfc_ctrl[NDFC_MAX_CS];

static void ndfc_select_chip(struct mtd_info *mtd, int chip)
{
	uint32_t ccr;
	struct ndfc_controller *ndfc = &ndfc_ctrl;
	struct nand_chip *nandchip = mtd->priv;
	struct ndfc_nand_mtd *nandmtd = nandchip->priv;
	struct platform_nand_chip *pchip = nandmtd->pl_chip;

	ccr = __raw_readl(ndfc->ndfcbase + NDFC_CCR);
	if (chip >= 0) {
		ccr &= ~NDFC_CCR_BS_MASK;
		ccr |= NDFC_CCR_BS(chip + pchip->chip_offset);
	} else
		ccr |= NDFC_CCR_RESET_CE;
	__raw_writel(ccr, ndfc->ndfcbase + NDFC_CCR);
	struct nand_chip *nchip = mtd->priv;
	struct ndfc_controller *ndfc = nchip->priv;

	ccr = in_be32(ndfc->ndfcbase + NDFC_CCR);
	if (chip >= 0) {
		ccr &= ~NDFC_CCR_BS_MASK;
		ccr |= NDFC_CCR_BS(chip + ndfc->chip_select);
	} else
		ccr |= NDFC_CCR_RESET_CE;
	out_be32(ndfc->ndfcbase + NDFC_CCR, ccr);
}

static void ndfc_hwcontrol(struct mtd_info *mtd, int cmd, unsigned int ctrl)
{
	struct ndfc_controller *ndfc = &ndfc_ctrl;
	struct nand_chip *chip = mtd->priv;
	struct ndfc_controller *ndfc = chip->priv;

	if (cmd == NAND_CMD_NONE)
		return;

	if (ctrl & NAND_CLE)
		writel(cmd & 0xFF, ndfc->ndfcbase + NDFC_CMD);
	else
		writel(cmd & 0xFF, ndfc->ndfcbase + NDFC_ALE);
}

static int ndfc_ready(struct mtd_info *mtd)
{
	struct ndfc_controller *ndfc = &ndfc_ctrl;

	return __raw_readl(ndfc->ndfcbase + NDFC_STAT) & NDFC_STAT_IS_READY;
	struct nand_chip *chip = mtd->priv;
	struct ndfc_controller *ndfc = chip->priv;

	return in_be32(ndfc->ndfcbase + NDFC_STAT) & NDFC_STAT_IS_READY;
}

static void ndfc_enable_hwecc(struct mtd_info *mtd, int mode)
{
	uint32_t ccr;
	struct ndfc_controller *ndfc = &ndfc_ctrl;

	ccr = __raw_readl(ndfc->ndfcbase + NDFC_CCR);
	ccr |= NDFC_CCR_RESET_ECC;
	__raw_writel(ccr, ndfc->ndfcbase + NDFC_CCR);
	struct nand_chip *chip = mtd->priv;
	struct ndfc_controller *ndfc = chip->priv;

	ccr = in_be32(ndfc->ndfcbase + NDFC_CCR);
	ccr |= NDFC_CCR_RESET_ECC;
	out_be32(ndfc->ndfcbase + NDFC_CCR, ccr);
	wmb();
}

static int ndfc_calculate_ecc(struct mtd_info *mtd,
			      const u_char *dat, u_char *ecc_code)
{
	struct ndfc_controller *ndfc = &ndfc_ctrl;
	struct nand_chip *chip = mtd->priv;
	struct ndfc_controller *ndfc = chip->priv;
	uint32_t ecc;
	uint8_t *p = (uint8_t *)&ecc;

	wmb();
	ecc = __raw_readl(ndfc->ndfcbase + NDFC_ECC);
	ecc = in_be32(ndfc->ndfcbase + NDFC_ECC);
	/* The NDFC uses Smart Media (SMC) bytes order */
	ecc_code[0] = p[1];
	ecc_code[1] = p[2];
	ecc_code[2] = p[3];

	return 0;
}

/*
 * Speedups for buffer read/write/verify
 *
 * NDFC allows 32bit read/write of data. So we can speed up the buffer
 * functions. No further checking, as nand_base will always read/write
 * page aligned.
 */
static void ndfc_read_buf(struct mtd_info *mtd, uint8_t *buf, int len)
{
	struct ndfc_controller *ndfc = &ndfc_ctrl;
	uint32_t *p = (uint32_t *) buf;

	for(;len > 0; len -= 4)
		*p++ = __raw_readl(ndfc->ndfcbase + NDFC_DATA);
	struct nand_chip *chip = mtd->priv;
	struct ndfc_controller *ndfc = chip->priv;
	uint32_t *p = (uint32_t *) buf;

	for(;len > 0; len -= 4)
		*p++ = in_be32(ndfc->ndfcbase + NDFC_DATA);
}

static void ndfc_write_buf(struct mtd_info *mtd, const uint8_t *buf, int len)
{
	struct ndfc_controller *ndfc = &ndfc_ctrl;
	uint32_t *p = (uint32_t *) buf;

	for(;len > 0; len -= 4)
		__raw_writel(*p++, ndfc->ndfcbase + NDFC_DATA);
}

static int ndfc_verify_buf(struct mtd_info *mtd, const uint8_t *buf, int len)
{
	struct ndfc_controller *ndfc = &ndfc_ctrl;
	uint32_t *p = (uint32_t *) buf;

	for(;len > 0; len -= 4)
		if (*p++ != __raw_readl(ndfc->ndfcbase + NDFC_DATA))
			return -EFAULT;
	return 0;
	struct nand_chip *chip = mtd->priv;
	struct ndfc_controller *ndfc = chip->priv;
	uint32_t *p = (uint32_t *) buf;

	for(;len > 0; len -= 4)
		out_be32(ndfc->ndfcbase + NDFC_DATA, *p++);
}

/*
 * Initialize chip structure
 */
static void ndfc_chip_init(struct ndfc_nand_mtd *mtd)
{
	struct ndfc_controller *ndfc = &ndfc_ctrl;
	struct nand_chip *chip = &mtd->chip;
static int ndfc_chip_init(struct ndfc_controller *ndfc,
			  struct device_node *node)
{
	struct device_node *flash_np;
	struct nand_chip *chip = &ndfc->chip;
	struct mtd_part_parser_data ppdata;
	int ret;

	chip->IO_ADDR_R = ndfc->ndfcbase + NDFC_DATA;
	chip->IO_ADDR_W = ndfc->ndfcbase + NDFC_DATA;
	chip->cmd_ctrl = ndfc_hwcontrol;
	chip->dev_ready = ndfc_ready;
	chip->select_chip = ndfc_select_chip;
	chip->chip_delay = 50;
	chip->priv = mtd;
	chip->options = mtd->pl_chip->options;
	chip->controller = &ndfc->ndfc_control;
	chip->read_buf = ndfc_read_buf;
	chip->write_buf = ndfc_write_buf;
	chip->verify_buf = ndfc_verify_buf;
	chip->controller = &ndfc->ndfc_control;
	chip->read_buf = ndfc_read_buf;
	chip->write_buf = ndfc_write_buf;
	chip->ecc.correct = nand_correct_data;
	chip->ecc.hwctl = ndfc_enable_hwecc;
	chip->ecc.calculate = ndfc_calculate_ecc;
	chip->ecc.mode = NAND_ECC_HW;
	chip->ecc.size = 256;
	chip->ecc.bytes = 3;
	chip->ecclayout = chip->ecc.layout = mtd->pl_chip->ecclayout;
	mtd->mtd.priv = chip;
	mtd->mtd.owner = THIS_MODULE;
}

static int ndfc_chip_probe(struct platform_device *pdev)
{
	struct platform_nand_chip *nc = pdev->dev.platform_data;
	struct ndfc_chip_settings *settings = nc->priv;
	struct ndfc_controller *ndfc = &ndfc_ctrl;
	struct ndfc_nand_mtd *nandmtd;

	if (nc->chip_offset >= NDFC_MAX_BANKS || nc->nr_chips > NDFC_MAX_BANKS)
		return -EINVAL;

	/* Set the bank settings */
	__raw_writel(settings->bank_settings,
		     ndfc->ndfcbase + NDFC_BCFG0 + (nc->chip_offset << 2));

	nandmtd = &ndfc_mtd[pdev->id];
	if (nandmtd->pl_chip)
		return -EBUSY;

	nandmtd->pl_chip = nc;
	ndfc_chip_init(nandmtd);

	/* Scan for chips */
	if (nand_scan(&nandmtd->mtd, nc->nr_chips)) {
		nandmtd->pl_chip = NULL;
		return -ENODEV;
	}

#ifdef CONFIG_MTD_PARTITIONS
	printk("Number of partitions %d\n", nc->nr_partitions);
	if (nc->nr_partitions) {
		/* Add the full device, so complete dumps can be made */
		add_mtd_device(&nandmtd->mtd);
		add_mtd_partitions(&nandmtd->mtd, nc->partitions,
				   nc->nr_partitions);

	} else
#else
		add_mtd_device(&nandmtd->mtd);
#endif

	atomic_inc(&ndfc->childs_active);
	return 0;
}

static int ndfc_chip_remove(struct platform_device *pdev)
{
	return 0;
}

static int ndfc_nand_probe(struct platform_device *pdev)
{
	struct platform_nand_ctrl *nc = pdev->dev.platform_data;
	struct ndfc_controller_settings *settings = nc->priv;
	struct resource *res = pdev->resource;
	struct ndfc_controller *ndfc = &ndfc_ctrl;
	unsigned long long phys = settings->ndfc_erpn | res->start;

#ifndef CONFIG_PHYS_64BIT
	ndfc->ndfcbase = ioremap((phys_addr_t)phys, res->end - res->start + 1);
#else
	ndfc->ndfcbase = ioremap64(phys, res->end - res->start + 1);
#endif
	if (!ndfc->ndfcbase) {
		printk(KERN_ERR "NDFC: ioremap failed\n");
		return -EIO;
	}

	__raw_writel(settings->ccr_settings, ndfc->ndfcbase + NDFC_CCR);

	spin_lock_init(&ndfc->ndfc_control.lock);
	init_waitqueue_head(&ndfc->ndfc_control.wq);

	platform_set_drvdata(pdev, ndfc);

	printk("NDFC NAND Driver initialized. Chip-Rev: 0x%08x\n",
	       __raw_readl(ndfc->ndfcbase + NDFC_REVID));

	return 0;
}

static int ndfc_nand_remove(struct platform_device *pdev)
{
	struct ndfc_controller *ndfc = platform_get_drvdata(pdev);

	if (atomic_read(&ndfc->childs_active))
		return -EBUSY;

	if (ndfc) {
		platform_set_drvdata(pdev, NULL);
		iounmap(ndfc_ctrl.ndfcbase);
		ndfc_ctrl.ndfcbase = NULL;
	}
	return 0;
}

/* driver device registration */

static struct platform_driver ndfc_chip_driver = {
	.probe		= ndfc_chip_probe,
	.remove		= ndfc_chip_remove,
	.driver		= {
		.name	= "ndfc-chip",
		.owner	= THIS_MODULE,
	},
};

static struct platform_driver ndfc_nand_driver = {
	.probe		= ndfc_nand_probe,
	.remove		= ndfc_nand_remove,
	.driver		= {
		.name	= "ndfc-nand",
		.owner	= THIS_MODULE,
	},
};

static int __init ndfc_nand_init(void)
{
	int ret;

	spin_lock_init(&ndfc_ctrl.ndfc_control.lock);
	init_waitqueue_head(&ndfc_ctrl.ndfc_control.wq);

	ret = platform_driver_register(&ndfc_nand_driver);
	if (!ret)
		ret = platform_driver_register(&ndfc_chip_driver);
	return ret;
}

static void __exit ndfc_nand_exit(void)
{
	platform_driver_unregister(&ndfc_chip_driver);
	platform_driver_unregister(&ndfc_nand_driver);
}

module_init(ndfc_nand_init);
module_exit(ndfc_nand_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Thomas Gleixner <tglx@linutronix.de>");
MODULE_DESCRIPTION("Platform driver for NDFC");
MODULE_ALIAS("platform:ndfc-chip");
MODULE_ALIAS("platform:ndfc-nand");
	chip->ecc.strength = 1;
	chip->priv = ndfc;

	ndfc->mtd.priv = chip;
	ndfc->mtd.dev.parent = &ndfc->ofdev->dev;

	flash_np = of_get_next_child(node, NULL);
	if (!flash_np)
		return -ENODEV;

	ppdata.of_node = flash_np;
	ndfc->mtd.name = kasprintf(GFP_KERNEL, "%s.%s",
			dev_name(&ndfc->ofdev->dev), flash_np->name);
	if (!ndfc->mtd.name) {
		ret = -ENOMEM;
		goto err;
	}

	ret = nand_scan(&ndfc->mtd, 1);
	if (ret)
		goto err;

	ret = mtd_device_parse_register(&ndfc->mtd, NULL, &ppdata, NULL, 0);

err:
	of_node_put(flash_np);
	if (ret)
		kfree(ndfc->mtd.name);
	return ret;
}

static int ndfc_probe(struct platform_device *ofdev)
{
	struct ndfc_controller *ndfc;
	const __be32 *reg;
	u32 ccr;
	u32 cs;
	int err, len;

	/* Read the reg property to get the chip select */
	reg = of_get_property(ofdev->dev.of_node, "reg", &len);
	if (reg == NULL || len != 12) {
		dev_err(&ofdev->dev, "unable read reg property (%d)\n", len);
		return -ENOENT;
	}

	cs = be32_to_cpu(reg[0]);
	if (cs >= NDFC_MAX_CS) {
		dev_err(&ofdev->dev, "invalid CS number (%d)\n", cs);
		return -EINVAL;
	}

	ndfc = &ndfc_ctrl[cs];
	ndfc->chip_select = cs;

	spin_lock_init(&ndfc->ndfc_control.lock);
	init_waitqueue_head(&ndfc->ndfc_control.wq);
	ndfc->ofdev = ofdev;
	dev_set_drvdata(&ofdev->dev, ndfc);

	ndfc->ndfcbase = of_iomap(ofdev->dev.of_node, 0);
	if (!ndfc->ndfcbase) {
		dev_err(&ofdev->dev, "failed to get memory\n");
		return -EIO;
	}

	ccr = NDFC_CCR_BS(ndfc->chip_select);

	/* It is ok if ccr does not exist - just default to 0 */
	reg = of_get_property(ofdev->dev.of_node, "ccr", NULL);
	if (reg)
		ccr |= be32_to_cpup(reg);

	out_be32(ndfc->ndfcbase + NDFC_CCR, ccr);

	/* Set the bank settings if given */
	reg = of_get_property(ofdev->dev.of_node, "bank-settings", NULL);
	if (reg) {
		int offset = NDFC_BCFG0 + (ndfc->chip_select << 2);
		out_be32(ndfc->ndfcbase + offset, be32_to_cpup(reg));
	}

	err = ndfc_chip_init(ndfc, ofdev->dev.of_node);
	if (err) {
		iounmap(ndfc->ndfcbase);
		return err;
	}

	return 0;
}

static int ndfc_remove(struct platform_device *ofdev)
{
	struct ndfc_controller *ndfc = dev_get_drvdata(&ofdev->dev);

	nand_release(&ndfc->mtd);
	kfree(ndfc->mtd.name);

	return 0;
}

static const struct of_device_id ndfc_match[] = {
	{ .compatible = "ibm,ndfc", },
	{}
};
MODULE_DEVICE_TABLE(of, ndfc_match);

static struct platform_driver ndfc_driver = {
	.driver = {
		.name = "ndfc",
		.of_match_table = ndfc_match,
	},
	.probe = ndfc_probe,
	.remove = ndfc_remove,
};

module_platform_driver(ndfc_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Thomas Gleixner <tglx@linutronix.de>");
MODULE_DESCRIPTION("OF Platform driver for NDFC");
