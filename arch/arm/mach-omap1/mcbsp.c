/*
 * linux/arch/arm/mach-omap1/mcbsp.c
 *
 * Copyright (C) 2008 Instituto Nokia de Tecnologia
 * Contact: Eduardo Valentin <eduardo.valentin@indt.org.br>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Multichannel mode not supported.
 */
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/platform_device.h>

#include <mach/dma.h>
#include <mach/mux.h>
#include <mach/cpu.h>
#include <mach/mcbsp.h>
#include <mach/dsp_common.h>
#include <linux/slab.h>

#include <linux/omap-dma.h>
#include <mach/mux.h>
#include "soc.h"
#include <linux/platform_data/asoc-ti-mcbsp.h>

#include <mach/irqs.h>

#include "iomap.h"

#define DPS_RSTCT2_PER_EN	(1 << 0)
#define DSP_RSTCT2_WD_PER_EN	(1 << 1)

struct mcbsp_internal_clk {
	struct clk clk;
	struct clk **childs;
	int n_childs;
};

#if defined(CONFIG_ARCH_OMAP15XX) || defined(CONFIG_ARCH_OMAP16XX)
static void omap_mcbsp_clk_init(struct mcbsp_internal_clk *mclk)
{
	const char *clk_names[] = { "dsp_ck", "api_ck", "dspxor_ck" };
	int i;

	mclk->n_childs = ARRAY_SIZE(clk_names);
	mclk->childs = kzalloc(mclk->n_childs * sizeof(struct clk *),
				GFP_KERNEL);

	for (i = 0; i < mclk->n_childs; i++) {
		/* We fake a platform device to get correct device id */
		struct platform_device pdev;

		pdev.dev.bus = &platform_bus_type;
		pdev.id = mclk->clk.id;
		mclk->childs[i] = clk_get(&pdev.dev, clk_names[i]);
		if (IS_ERR(mclk->childs[i]))
			printk(KERN_ERR "Could not get clock %s (%d).\n",
				clk_names[i], mclk->clk.id);
	}
}

static int omap_mcbsp_clk_enable(struct clk *clk)
{
	struct mcbsp_internal_clk *mclk = container_of(clk,
					struct mcbsp_internal_clk, clk);
	int i;

	for (i = 0; i < mclk->n_childs; i++)
		clk_enable(mclk->childs[i]);
	return 0;
}

static void omap_mcbsp_clk_disable(struct clk *clk)
{
	struct mcbsp_internal_clk *mclk = container_of(clk,
					struct mcbsp_internal_clk, clk);
	int i;

	for (i = 0; i < mclk->n_childs; i++)
		clk_disable(mclk->childs[i]);
}

static struct mcbsp_internal_clk omap_mcbsp_clks[] = {
	{
		.clk = {
			.name 		= "mcbsp_clk",
			.id		= 1,
			.enable		= omap_mcbsp_clk_enable,
			.disable	= omap_mcbsp_clk_disable,
		},
	},
	{
		.clk = {
			.name 		= "mcbsp_clk",
			.id		= 3,
			.enable		= omap_mcbsp_clk_enable,
			.disable	= omap_mcbsp_clk_disable,
		},
	},
};

#define omap_mcbsp_clks_size	ARRAY_SIZE(omap_mcbsp_clks)
#else
#define omap_mcbsp_clks_size	0
static struct mcbsp_internal_clk __initdata *omap_mcbsp_clks;
static inline void omap_mcbsp_clk_init(struct mcbsp_internal_clk *mclk)
{ }
#endif

static int omap1_mcbsp_check(unsigned int id)
{
	/* REVISIT: Check correctly for number of registered McBSPs */
	if (cpu_is_omap730()) {
		if (id > OMAP_MAX_MCBSP_COUNT - 2) {
		       printk(KERN_ERR "OMAP-McBSP: McBSP%d doesn't exist\n",
				id + 1);
		       return -ENODEV;
		}
		return 0;
	}

	if (cpu_is_omap15xx() || cpu_is_omap16xx()) {
		if (id > OMAP_MAX_MCBSP_COUNT - 1) {
			printk(KERN_ERR "OMAP-McBSP: McBSP%d doesn't exist\n",
				id + 1);
			return -ENODEV;
		}
		return 0;
	}

	return -ENODEV;
}
static int dsp_use;
static struct clk *api_clk;
static struct clk *dsp_clk;
static struct platform_device **omap_mcbsp_devices;

static void omap1_mcbsp_request(unsigned int id)
{
	/*
	 * On 1510, 1610 and 1710, McBSP1 and McBSP3
	 * are DSP public peripherals.
	 */
	if (id == OMAP_MCBSP1 || id == OMAP_MCBSP3) {
		omap_dsp_request_mem();
		/*
		 * DSP external peripheral reset
		 * FIXME: This should be moved to dsp code
		 */
		__raw_writew(__raw_readw(DSP_RSTCT2) | DPS_RSTCT2_PER_EN |
				DSP_RSTCT2_WD_PER_EN, DSP_RSTCT2);
	if (id == 0 || id == 2) {
		if (dsp_use++ == 0) {
			api_clk = clk_get(NULL, "api_ck");
			dsp_clk = clk_get(NULL, "dsp_ck");
			if (!IS_ERR(api_clk) && !IS_ERR(dsp_clk)) {
				clk_enable(api_clk);
				clk_enable(dsp_clk);

				/*
				 * DSP external peripheral reset
				 * FIXME: This should be moved to dsp code
				 */
				__raw_writew(__raw_readw(DSP_RSTCT2) | DPS_RSTCT2_PER_EN |
						DSP_RSTCT2_WD_PER_EN, DSP_RSTCT2);
			}
		}
	}
}

static void omap1_mcbsp_free(unsigned int id)
{
	if (id == OMAP_MCBSP1 || id == OMAP_MCBSP3)
		omap_dsp_release_mem();
}

static struct omap_mcbsp_ops omap1_mcbsp_ops = {
	.check		= omap1_mcbsp_check,
	if (id == 0 || id == 2) {
		if (--dsp_use == 0) {
			if (!IS_ERR(api_clk)) {
				clk_disable(api_clk);
				clk_put(api_clk);
			}
			if (!IS_ERR(dsp_clk)) {
				clk_disable(dsp_clk);
				clk_put(dsp_clk);
			}
		}
	}
}

static struct omap_mcbsp_ops omap1_mcbsp_ops = {
	.request	= omap1_mcbsp_request,
	.free		= omap1_mcbsp_free,
};

#ifdef CONFIG_ARCH_OMAP730
static struct omap_mcbsp_platform_data omap730_mcbsp_pdata[] = {
	{
		.phys_base	= OMAP730_MCBSP1_BASE,
		.virt_base	= io_p2v(OMAP730_MCBSP1_BASE),
		.dma_rx_sync	= OMAP_DMA_MCBSP1_RX,
		.dma_tx_sync	= OMAP_DMA_MCBSP1_TX,
		.rx_irq		= INT_730_McBSP1RX,
		.tx_irq		= INT_730_McBSP1TX,
		.ops		= &omap1_mcbsp_ops,
	},
	{
		.phys_base	= OMAP730_MCBSP2_BASE,
		.virt_base	= io_p2v(OMAP730_MCBSP2_BASE),
		.dma_rx_sync	= OMAP_DMA_MCBSP3_RX,
		.dma_tx_sync	= OMAP_DMA_MCBSP3_TX,
		.rx_irq		= INT_730_McBSP2RX,
		.tx_irq		= INT_730_McBSP2TX,
		.ops		= &omap1_mcbsp_ops,
	},
};
#define OMAP730_MCBSP_PDATA_SZ		ARRAY_SIZE(omap730_mcbsp_pdata)
#else
#define omap730_mcbsp_pdata		NULL
#define OMAP730_MCBSP_PDATA_SZ		0
#endif

#ifdef CONFIG_ARCH_OMAP15XX
static struct omap_mcbsp_platform_data omap15xx_mcbsp_pdata[] = {
	{
		.phys_base	= OMAP1510_MCBSP1_BASE,
		.virt_base	= OMAP1510_MCBSP1_BASE,
		.dma_rx_sync	= OMAP_DMA_MCBSP1_RX,
		.dma_tx_sync	= OMAP_DMA_MCBSP1_TX,
		.rx_irq		= INT_McBSP1RX,
		.tx_irq		= INT_McBSP1TX,
		.ops		= &omap1_mcbsp_ops,
		.clk_name	= "mcbsp_clk",
		},
	{
		.phys_base	= OMAP1510_MCBSP2_BASE,
		.virt_base	= io_p2v(OMAP1510_MCBSP2_BASE),
		.dma_rx_sync	= OMAP_DMA_MCBSP2_RX,
		.dma_tx_sync	= OMAP_DMA_MCBSP2_TX,
		.rx_irq		= INT_1510_SPI_RX,
		.tx_irq		= INT_1510_SPI_TX,
		.ops		= &omap1_mcbsp_ops,
	},
	{
		.phys_base	= OMAP1510_MCBSP3_BASE,
		.virt_base	= OMAP1510_MCBSP3_BASE,
		.dma_rx_sync	= OMAP_DMA_MCBSP3_RX,
		.dma_tx_sync	= OMAP_DMA_MCBSP3_TX,
		.rx_irq		= INT_McBSP3RX,
		.tx_irq		= INT_McBSP3TX,
		.ops		= &omap1_mcbsp_ops,
		.clk_name	= "mcbsp_clk",
	},
};
#define OMAP15XX_MCBSP_PDATA_SZ		ARRAY_SIZE(omap15xx_mcbsp_pdata)
#else
#define omap15xx_mcbsp_pdata		NULL
#define OMAP15XX_MCBSP_PDATA_SZ		0
#endif

#ifdef CONFIG_ARCH_OMAP16XX
static struct omap_mcbsp_platform_data omap16xx_mcbsp_pdata[] = {
	{
		.phys_base	= OMAP1610_MCBSP1_BASE,
		.virt_base	= OMAP1610_MCBSP1_BASE,
		.dma_rx_sync	= OMAP_DMA_MCBSP1_RX,
		.dma_tx_sync	= OMAP_DMA_MCBSP1_TX,
		.rx_irq		= INT_McBSP1RX,
		.tx_irq		= INT_McBSP1TX,
		.ops		= &omap1_mcbsp_ops,
		.clk_name	= "mcbsp_clk",
	},
	{
		.phys_base	= OMAP1610_MCBSP2_BASE,
		.virt_base	= io_p2v(OMAP1610_MCBSP2_BASE),
		.dma_rx_sync	= OMAP_DMA_MCBSP2_RX,
		.dma_tx_sync	= OMAP_DMA_MCBSP2_TX,
		.rx_irq		= INT_1610_McBSP2_RX,
		.tx_irq		= INT_1610_McBSP2_TX,
		.ops		= &omap1_mcbsp_ops,
	},
	{
		.phys_base	= OMAP1610_MCBSP3_BASE,
		.virt_base	= OMAP1610_MCBSP3_BASE,
		.dma_rx_sync	= OMAP_DMA_MCBSP3_RX,
		.dma_tx_sync	= OMAP_DMA_MCBSP3_TX,
		.rx_irq		= INT_McBSP3RX,
		.tx_irq		= INT_McBSP3TX,
		.ops		= &omap1_mcbsp_ops,
		.clk_name	= "mcbsp_clk",
	},
};
#define OMAP16XX_MCBSP_PDATA_SZ		ARRAY_SIZE(omap16xx_mcbsp_pdata)
#else
#define omap16xx_mcbsp_pdata		NULL
#define OMAP16XX_MCBSP_PDATA_SZ		0
#endif

int __init omap1_mcbsp_init(void)
{
	int i;

	for (i = 0; i < omap_mcbsp_clks_size; i++) {
		if (cpu_is_omap15xx() || cpu_is_omap16xx()) {
			omap_mcbsp_clk_init(&omap_mcbsp_clks[i]);
			clk_register(&omap_mcbsp_clks[i].clk);
		}
	}

	if (cpu_is_omap730())
		omap_mcbsp_register_board_cfg(omap730_mcbsp_pdata,
						OMAP730_MCBSP_PDATA_SZ);

	if (cpu_is_omap15xx())
		omap_mcbsp_register_board_cfg(omap15xx_mcbsp_pdata,
						OMAP15XX_MCBSP_PDATA_SZ);

	if (cpu_is_omap16xx())
		omap_mcbsp_register_board_cfg(omap16xx_mcbsp_pdata,
						OMAP16XX_MCBSP_PDATA_SZ);

	return omap_mcbsp_init();
#define OMAP7XX_MCBSP1_BASE	0xfffb1000
#define OMAP7XX_MCBSP2_BASE	0xfffb1800

#define OMAP1510_MCBSP1_BASE	0xe1011800
#define OMAP1510_MCBSP2_BASE	0xfffb1000
#define OMAP1510_MCBSP3_BASE	0xe1017000

#define OMAP1610_MCBSP1_BASE	0xe1011800
#define OMAP1610_MCBSP2_BASE	0xfffb1000
#define OMAP1610_MCBSP3_BASE	0xe1017000

#if defined(CONFIG_ARCH_OMAP730) || defined(CONFIG_ARCH_OMAP850)
struct resource omap7xx_mcbsp_res[][6] = {
	{
		{
			.start = OMAP7XX_MCBSP1_BASE,
			.end   = OMAP7XX_MCBSP1_BASE + SZ_256,
			.flags = IORESOURCE_MEM,
		},
		{
			.name  = "rx",
			.start = INT_7XX_McBSP1RX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "tx",
			.start = INT_7XX_McBSP1TX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "rx",
			.start = 9,
			.flags = IORESOURCE_DMA,
		},
		{
			.name  = "tx",
			.start = 8,
			.flags = IORESOURCE_DMA,
		},
	},
	{
		{
			.start = OMAP7XX_MCBSP2_BASE,
			.end   = OMAP7XX_MCBSP2_BASE + SZ_256,
			.flags = IORESOURCE_MEM,
		},
		{
			.name  = "rx",
			.start = INT_7XX_McBSP2RX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "tx",
			.start = INT_7XX_McBSP2TX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "rx",
			.start = 11,
			.flags = IORESOURCE_DMA,
		},
		{
			.name  = "tx",
			.start = 10,
			.flags = IORESOURCE_DMA,
		},
	},
};

#define omap7xx_mcbsp_res_0		omap7xx_mcbsp_res[0]

static struct omap_mcbsp_platform_data omap7xx_mcbsp_pdata[] = {
	{
		.ops		= &omap1_mcbsp_ops,
	},
	{
		.ops		= &omap1_mcbsp_ops,
	},
};
#define OMAP7XX_MCBSP_RES_SZ		ARRAY_SIZE(omap7xx_mcbsp_res[1])
#define OMAP7XX_MCBSP_COUNT		ARRAY_SIZE(omap7xx_mcbsp_res)
#else
#define omap7xx_mcbsp_res_0		NULL
#define omap7xx_mcbsp_pdata		NULL
#define OMAP7XX_MCBSP_RES_SZ		0
#define OMAP7XX_MCBSP_COUNT		0
#endif

#ifdef CONFIG_ARCH_OMAP15XX
struct resource omap15xx_mcbsp_res[][6] = {
	{
		{
			.start = OMAP1510_MCBSP1_BASE,
			.end   = OMAP1510_MCBSP1_BASE + SZ_256,
			.flags = IORESOURCE_MEM,
		},
		{
			.name  = "rx",
			.start = INT_McBSP1RX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "tx",
			.start = INT_McBSP1TX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "rx",
			.start = 9,
			.flags = IORESOURCE_DMA,
		},
		{
			.name  = "tx",
			.start = 8,
			.flags = IORESOURCE_DMA,
		},
	},
	{
		{
			.start = OMAP1510_MCBSP2_BASE,
			.end   = OMAP1510_MCBSP2_BASE + SZ_256,
			.flags = IORESOURCE_MEM,
		},
		{
			.name  = "rx",
			.start = INT_1510_SPI_RX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "tx",
			.start = INT_1510_SPI_TX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "rx",
			.start = 17,
			.flags = IORESOURCE_DMA,
		},
		{
			.name  = "tx",
			.start = 16,
			.flags = IORESOURCE_DMA,
		},
	},
	{
		{
			.start = OMAP1510_MCBSP3_BASE,
			.end   = OMAP1510_MCBSP3_BASE + SZ_256,
			.flags = IORESOURCE_MEM,
		},
		{
			.name  = "rx",
			.start = INT_McBSP3RX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "tx",
			.start = INT_McBSP3TX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "rx",
			.start = 11,
			.flags = IORESOURCE_DMA,
		},
		{
			.name  = "tx",
			.start = 10,
			.flags = IORESOURCE_DMA,
		},
	},
};

#define omap15xx_mcbsp_res_0		omap15xx_mcbsp_res[0]

static struct omap_mcbsp_platform_data omap15xx_mcbsp_pdata[] = {
	{
		.ops		= &omap1_mcbsp_ops,
	},
	{
		.ops		= &omap1_mcbsp_ops,
	},
	{
		.ops		= &omap1_mcbsp_ops,
	},
};
#define OMAP15XX_MCBSP_RES_SZ		ARRAY_SIZE(omap15xx_mcbsp_res[1])
#define OMAP15XX_MCBSP_COUNT		ARRAY_SIZE(omap15xx_mcbsp_res)
#else
#define omap15xx_mcbsp_res_0		NULL
#define omap15xx_mcbsp_pdata		NULL
#define OMAP15XX_MCBSP_RES_SZ		0
#define OMAP15XX_MCBSP_COUNT		0
#endif

#ifdef CONFIG_ARCH_OMAP16XX
struct resource omap16xx_mcbsp_res[][6] = {
	{
		{
			.start = OMAP1610_MCBSP1_BASE,
			.end   = OMAP1610_MCBSP1_BASE + SZ_256,
			.flags = IORESOURCE_MEM,
		},
		{
			.name  = "rx",
			.start = INT_McBSP1RX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "tx",
			.start = INT_McBSP1TX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "rx",
			.start = 9,
			.flags = IORESOURCE_DMA,
		},
		{
			.name  = "tx",
			.start = 8,
			.flags = IORESOURCE_DMA,
		},
	},
	{
		{
			.start = OMAP1610_MCBSP2_BASE,
			.end   = OMAP1610_MCBSP2_BASE + SZ_256,
			.flags = IORESOURCE_MEM,
		},
		{
			.name  = "rx",
			.start = INT_1610_McBSP2_RX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "tx",
			.start = INT_1610_McBSP2_TX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "rx",
			.start = 17,
			.flags = IORESOURCE_DMA,
		},
		{
			.name  = "tx",
			.start = 16,
			.flags = IORESOURCE_DMA,
		},
	},
	{
		{
			.start = OMAP1610_MCBSP3_BASE,
			.end   = OMAP1610_MCBSP3_BASE + SZ_256,
			.flags = IORESOURCE_MEM,
		},
		{
			.name  = "rx",
			.start = INT_McBSP3RX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "tx",
			.start = INT_McBSP3TX,
			.flags = IORESOURCE_IRQ,
		},
		{
			.name  = "rx",
			.start = 11,
			.flags = IORESOURCE_DMA,
		},
		{
			.name  = "tx",
			.start = 10,
			.flags = IORESOURCE_DMA,
		},
	},
};

#define omap16xx_mcbsp_res_0		omap16xx_mcbsp_res[0]

static struct omap_mcbsp_platform_data omap16xx_mcbsp_pdata[] = {
	{
		.ops		= &omap1_mcbsp_ops,
	},
	{
		.ops		= &omap1_mcbsp_ops,
	},
	{
		.ops		= &omap1_mcbsp_ops,
	},
};
#define OMAP16XX_MCBSP_RES_SZ		ARRAY_SIZE(omap16xx_mcbsp_res[1])
#define OMAP16XX_MCBSP_COUNT		ARRAY_SIZE(omap16xx_mcbsp_res)
#else
#define omap16xx_mcbsp_res_0		NULL
#define omap16xx_mcbsp_pdata		NULL
#define OMAP16XX_MCBSP_RES_SZ		0
#define OMAP16XX_MCBSP_COUNT		0
#endif

static void omap_mcbsp_register_board_cfg(struct resource *res, int res_count,
			struct omap_mcbsp_platform_data *config, int size)
{
	int i;

	omap_mcbsp_devices = kzalloc(size * sizeof(struct platform_device *),
				     GFP_KERNEL);
	if (!omap_mcbsp_devices) {
		printk(KERN_ERR "Could not register McBSP devices\n");
		return;
	}

	for (i = 0; i < size; i++) {
		struct platform_device *new_mcbsp;
		int ret;

		new_mcbsp = platform_device_alloc("omap-mcbsp", i + 1);
		if (!new_mcbsp)
			continue;
		platform_device_add_resources(new_mcbsp, &res[i * res_count],
					res_count);
		config[i].reg_size = 2;
		config[i].reg_step = 2;
		new_mcbsp->dev.platform_data = &config[i];
		ret = platform_device_add(new_mcbsp);
		if (ret) {
			platform_device_put(new_mcbsp);
			continue;
		}
		omap_mcbsp_devices[i] = new_mcbsp;
	}
}

static int __init omap1_mcbsp_init(void)
{
	if (!cpu_class_is_omap1())
		return -ENODEV;

	if (cpu_is_omap7xx())
		omap_mcbsp_register_board_cfg(omap7xx_mcbsp_res_0,
					OMAP7XX_MCBSP_RES_SZ,
					omap7xx_mcbsp_pdata,
					OMAP7XX_MCBSP_COUNT);

	if (cpu_is_omap15xx())
		omap_mcbsp_register_board_cfg(omap15xx_mcbsp_res_0,
					OMAP15XX_MCBSP_RES_SZ,
					omap15xx_mcbsp_pdata,
					OMAP15XX_MCBSP_COUNT);

	if (cpu_is_omap16xx())
		omap_mcbsp_register_board_cfg(omap16xx_mcbsp_res_0,
					OMAP16XX_MCBSP_RES_SZ,
					omap16xx_mcbsp_pdata,
					OMAP16XX_MCBSP_COUNT);

	return 0;
}

arch_initcall(omap1_mcbsp_init);
