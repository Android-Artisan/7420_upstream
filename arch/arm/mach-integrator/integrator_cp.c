/*
 *  linux/arch/arm/mach-integrator/integrator_cp.c
 *
 *  Copyright (C) 2003 Deep Blue Solutions Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysdev.h>
#include <linux/amba/bus.h>
#include <linux/amba/kmi.h>
#include <linux/amba/clcd.h>

#include <mach/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/hardware/icst525.h>

#include <mach/cm.h>
#include <mach/lm.h>

#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach/irq.h>
#include <asm/mach/mmc.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>

#include "common.h"
#include "clock.h"

#define INTCP_PA_MMC_BASE		0x1c000000
#define INTCP_PA_AACI_BASE		0x1d000000

#define INTCP_PA_FLASH_BASE		0x24000000
#define INTCP_FLASH_SIZE		SZ_32M

#define INTCP_PA_CLCD_BASE		0xc0000000

#define INTCP_VA_CIC_BASE		0xf1000040
#define INTCP_VA_PIC_BASE		0xf1400000
#define INTCP_VA_SIC_BASE		0xfca00000

#define INTCP_PA_ETH_BASE		0xc8000000
#define INTCP_ETH_SIZE			0x10

#define INTCP_VA_CTRL_BASE		0xfcb00000
#include <linux/string.h>
#include <linux/device.h>
#include <linux/amba/bus.h>
#include <linux/amba/kmi.h>
#include <linux/amba/clcd.h>
#include <linux/platform_data/video-clcd-versatile.h>
#include <linux/amba/mmci.h>
#include <linux/io.h>
#include <linux/irqchip.h>
#include <linux/gfp.h>
#include <linux/mtd/physmap.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/sched_clock.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/irq.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>

#include "hardware.h"
#include "cm.h"
#include "common.h"

/* Base address to the CP controller */
static void __iomem *intcp_con_base;

#define INTCP_PA_FLASH_BASE		0x24000000

#define INTCP_PA_CLCD_BASE		0xc0000000

#define INTCP_FLASHPROG			0x04
#define CINTEGRATOR_FLASHPROG_FLVPPEN	(1 << 0)
#define CINTEGRATOR_FLASHPROG_FLWREN	(1 << 1)

/*
 * Logical      Physical
 * f1000000	10000000	Core module registers
 * f1100000	11000000	System controller registers
 * f1200000	12000000	EBI registers
 * f1300000	13000000	Counter/Timer
 * f1400000	14000000	Interrupt controller
 * f1600000	16000000	UART 0
 * f1700000	17000000	UART 1
 * f1a00000	1a000000	Debug LEDs
 * f1b00000	1b000000	GPIO
 */

static struct map_desc intcp_io_desc[] __initdata = {
 * fc900000	c9000000	GPIO
 * fca00000	ca000000	SIC
 */

static struct map_desc intcp_io_desc[] __initdata __maybe_unused = {
	{
		.virtual	= IO_ADDRESS(INTEGRATOR_HDR_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_HDR_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= IO_ADDRESS(INTEGRATOR_SC_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_SC_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= IO_ADDRESS(INTEGRATOR_EBI_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_EBI_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= IO_ADDRESS(INTEGRATOR_CT_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_CT_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= IO_ADDRESS(INTEGRATOR_IC_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_IC_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= IO_ADDRESS(INTEGRATOR_UART0_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_UART0_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= IO_ADDRESS(INTEGRATOR_UART1_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_UART1_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= IO_ADDRESS(INTEGRATOR_DBG_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_DBG_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= IO_ADDRESS(INTEGRATOR_GPIO_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_GPIO_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= 0xfca00000,
		.pfn		= __phys_to_pfn(0xca000000),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= 0xfcb00000,
		.pfn		= __phys_to_pfn(0xcb000000),
		.virtual	= IO_ADDRESS(INTEGRATOR_CP_GPIO_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_CP_GPIO_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= IO_ADDRESS(INTEGRATOR_CP_SIC_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_CP_SIC_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}
};

static void __init intcp_map_io(void)
{
	iotable_init(intcp_io_desc, ARRAY_SIZE(intcp_io_desc));
}

#define cic_writel	__raw_writel
#define cic_readl	__raw_readl
#define pic_writel	__raw_writel
#define pic_readl	__raw_readl
#define sic_writel	__raw_writel
#define sic_readl	__raw_readl

static void cic_mask_irq(unsigned int irq)
{
	irq -= IRQ_CIC_START;
	cic_writel(1 << irq, INTCP_VA_CIC_BASE + IRQ_ENABLE_CLEAR);
}

static void cic_unmask_irq(unsigned int irq)
{
	irq -= IRQ_CIC_START;
	cic_writel(1 << irq, INTCP_VA_CIC_BASE + IRQ_ENABLE_SET);
}

static struct irq_chip cic_chip = {
	.name	= "CIC",
	.ack	= cic_mask_irq,
	.mask	= cic_mask_irq,
	.unmask	= cic_unmask_irq,
};

static void pic_mask_irq(unsigned int irq)
{
	irq -= IRQ_PIC_START;
	pic_writel(1 << irq, INTCP_VA_PIC_BASE + IRQ_ENABLE_CLEAR);
}

static void pic_unmask_irq(unsigned int irq)
{
	irq -= IRQ_PIC_START;
	pic_writel(1 << irq, INTCP_VA_PIC_BASE + IRQ_ENABLE_SET);
}

static struct irq_chip pic_chip = {
	.name	= "PIC",
	.ack	= pic_mask_irq,
	.mask	= pic_mask_irq,
	.unmask = pic_unmask_irq,
};

static void sic_mask_irq(unsigned int irq)
{
	irq -= IRQ_SIC_START;
	sic_writel(1 << irq, INTCP_VA_SIC_BASE + IRQ_ENABLE_CLEAR);
}

static void sic_unmask_irq(unsigned int irq)
{
	irq -= IRQ_SIC_START;
	sic_writel(1 << irq, INTCP_VA_SIC_BASE + IRQ_ENABLE_SET);
}

static struct irq_chip sic_chip = {
	.name	= "SIC",
	.ack	= sic_mask_irq,
	.mask	= sic_mask_irq,
	.unmask	= sic_unmask_irq,
};

static void
sic_handle_irq(unsigned int irq, struct irq_desc *desc)
{
	unsigned long status = sic_readl(INTCP_VA_SIC_BASE + IRQ_STATUS);

	if (status == 0) {
		do_bad_IRQ(irq, desc);
		return;
	}

	do {
		irq = ffs(status) - 1;
		status &= ~(1 << irq);

		irq += IRQ_SIC_START;

		desc = irq_desc + irq;
		desc_handle_irq(irq, desc);
	} while (status);
}

static void __init intcp_init_irq(void)
{
	unsigned int i;

	/*
	 * Disable all interrupt sources
	 */
	pic_writel(0xffffffff, INTCP_VA_PIC_BASE + IRQ_ENABLE_CLEAR);
	pic_writel(0xffffffff, INTCP_VA_PIC_BASE + FIQ_ENABLE_CLEAR);

	for (i = IRQ_PIC_START; i <= IRQ_PIC_END; i++) {
		if (i == 11)
			i = 22;
		if (i == 29)
			break;
		set_irq_chip(i, &pic_chip);
		set_irq_handler(i, handle_level_irq);
		set_irq_flags(i, IRQF_VALID | IRQF_PROBE);
	}

	cic_writel(0xffffffff, INTCP_VA_CIC_BASE + IRQ_ENABLE_CLEAR);
	cic_writel(0xffffffff, INTCP_VA_CIC_BASE + FIQ_ENABLE_CLEAR);

	for (i = IRQ_CIC_START; i <= IRQ_CIC_END; i++) {
		set_irq_chip(i, &cic_chip);
		set_irq_handler(i, handle_level_irq);
		set_irq_flags(i, IRQF_VALID);
	}

	sic_writel(0x00000fff, INTCP_VA_SIC_BASE + IRQ_ENABLE_CLEAR);
	sic_writel(0x00000fff, INTCP_VA_SIC_BASE + FIQ_ENABLE_CLEAR);

	for (i = IRQ_SIC_START; i <= IRQ_SIC_END; i++) {
		set_irq_chip(i, &sic_chip);
		set_irq_handler(i, handle_level_irq);
		set_irq_flags(i, IRQF_VALID | IRQF_PROBE);
	}

	set_irq_chained_handler(IRQ_CP_CPPLDINT, sic_handle_irq);
}

/*
 * Clock handling
 */
#define CM_LOCK (IO_ADDRESS(INTEGRATOR_HDR_BASE)+INTEGRATOR_HDR_LOCK_OFFSET)
#define CM_AUXOSC (IO_ADDRESS(INTEGRATOR_HDR_BASE)+0x1c)

static const struct icst525_params cp_auxvco_params = {
	.ref		= 24000,
	.vco_max	= 320000,
	.vd_min 	= 8,
	.vd_max 	= 263,
	.rd_min 	= 3,
	.rd_max 	= 65,
};

static void cp_auxvco_set(struct clk *clk, struct icst525_vco vco)
{
	u32 val;

	val = readl(CM_AUXOSC) & ~0x7ffff;
	val |= vco.v | (vco.r << 9) | (vco.s << 16);

	writel(0xa05f, CM_LOCK);
	writel(val, CM_AUXOSC);
	writel(0, CM_LOCK);
}

static struct clk cp_clcd_clk = {
	.name	= "CLCDCLK",
	.params	= &cp_auxvco_params,
	.setvco = cp_auxvco_set,
};

static struct clk cp_mmci_clk = {
	.name	= "MCLK",
	.rate	= 14745600,
};

/*
 * Flash handling.
 */
static int intcp_flash_init(void)
{
	u32 val;

	val = readl(INTCP_VA_CTRL_BASE + INTCP_FLASHPROG);
	val |= CINTEGRATOR_FLASHPROG_FLWREN;
	writel(val, INTCP_VA_CTRL_BASE + INTCP_FLASHPROG);
/*
 * Flash handling.
 */
static int intcp_flash_init(struct platform_device *dev)
{
	u32 val;

	val = readl(intcp_con_base + INTCP_FLASHPROG);
	val |= CINTEGRATOR_FLASHPROG_FLWREN;
	writel(val, intcp_con_base + INTCP_FLASHPROG);

	return 0;
}

static void intcp_flash_exit(void)
{
	u32 val;

	val = readl(INTCP_VA_CTRL_BASE + INTCP_FLASHPROG);
	val &= ~(CINTEGRATOR_FLASHPROG_FLVPPEN|CINTEGRATOR_FLASHPROG_FLWREN);
	writel(val, INTCP_VA_CTRL_BASE + INTCP_FLASHPROG);
}

static void intcp_flash_set_vpp(int on)
{
	u32 val;

	val = readl(INTCP_VA_CTRL_BASE + INTCP_FLASHPROG);
static void intcp_flash_exit(struct platform_device *dev)
{
	u32 val;

	val = readl(intcp_con_base + INTCP_FLASHPROG);
	val &= ~(CINTEGRATOR_FLASHPROG_FLVPPEN|CINTEGRATOR_FLASHPROG_FLWREN);
	writel(val, intcp_con_base + INTCP_FLASHPROG);
}

static void intcp_flash_set_vpp(struct platform_device *pdev, int on)
{
	u32 val;

	val = readl(intcp_con_base + INTCP_FLASHPROG);
	if (on)
		val |= CINTEGRATOR_FLASHPROG_FLVPPEN;
	else
		val &= ~CINTEGRATOR_FLASHPROG_FLVPPEN;
	writel(val, INTCP_VA_CTRL_BASE + INTCP_FLASHPROG);
}

static struct flash_platform_data intcp_flash_data = {
	.map_name	= "cfi_probe",
	writel(val, intcp_con_base + INTCP_FLASHPROG);
}

static struct physmap_flash_data intcp_flash_data = {
	.width		= 4,
	.init		= intcp_flash_init,
	.exit		= intcp_flash_exit,
	.set_vpp	= intcp_flash_set_vpp,
};

static struct resource intcp_flash_resource = {
	.start		= INTCP_PA_FLASH_BASE,
	.end		= INTCP_PA_FLASH_BASE + INTCP_FLASH_SIZE - 1,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device intcp_flash_device = {
	.name		= "armflash",
	.id		= 0,
	.dev		= {
		.platform_data	= &intcp_flash_data,
	},
	.num_resources	= 1,
	.resource	= &intcp_flash_resource,
};

static struct resource smc91x_resources[] = {
	[0] = {
		.start	= INTCP_PA_ETH_BASE,
		.end	= INTCP_PA_ETH_BASE + INTCP_ETH_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_CP_ETHINT,
		.end	= IRQ_CP_ETHINT,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
};

static struct platform_device *intcp_devs[] __initdata = {
	&intcp_flash_device,
	&smc91x_device,
};

/*
 * It seems that the card insertion interrupt remains active after
 * we've acknowledged it.  We therefore ignore the interrupt, and
 * rely on reading it from the SIC.  This also means that we must
 * clear the latched interrupt.
 */
static unsigned int mmc_status(struct device *dev)
{
	unsigned int status = readl(0xfca00004);
	writel(8, 0xfcb00008);
	unsigned int status = readl(__io_address(0xca000000 + 4));
	writel(8, intcp_con_base + 8);

	return status & 8;
}

static struct mmc_platform_data mmc_data = {
	.ocr_mask	= MMC_VDD_32_33|MMC_VDD_33_34,
	.status		= mmc_status,
};

static struct amba_device mmc_device = {
	.dev		= {
		.bus_id	= "mb:1c",
		.platform_data = &mmc_data,
	},
	.res		= {
		.start	= INTCP_PA_MMC_BASE,
		.end	= INTCP_PA_MMC_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	.irq		= { IRQ_CP_MMCIINT0, IRQ_CP_MMCIINT1 },
	.periphid	= 0,
};

static struct amba_device aaci_device = {
	.dev		= {
		.bus_id	= "mb:1d",
	},
	.res		= {
		.start	= INTCP_PA_AACI_BASE,
		.end	= INTCP_PA_AACI_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	.irq		= { IRQ_CP_AACIINT, NO_IRQ },
	.periphid	= 0,
};


/*
 * CLCD support
 */
static struct clcd_panel vga = {
	.mode		= {
		.name		= "VGA",
		.refresh	= 60,
		.xres		= 640,
		.yres		= 480,
		.pixclock	= 39721,
		.left_margin	= 40,
		.right_margin	= 24,
		.upper_margin	= 32,
		.lower_margin	= 11,
		.hsync_len	= 96,
		.vsync_len	= 2,
		.sync		= 0,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
	.width		= -1,
	.height		= -1,
	.tim2		= TIM2_BCD | TIM2_IPC,
	.cntl		= CNTL_LCDTFT | CNTL_LCDVCOMP(1),
	.bpp		= 16,
	.grayscale	= 0,
};

static struct mmci_platform_data mmc_data = {
	.ocr_mask	= MMC_VDD_32_33|MMC_VDD_33_34,
	.status		= mmc_status,
	.gpio_wp	= -1,
	.gpio_cd	= -1,
};

/*
 * CLCD support
 */
/*
 * Ensure VGA is selected.
 */
static void cp_clcd_enable(struct clcd_fb *fb)
{
	u32 val;

	if (fb->fb.var.bits_per_pixel <= 8)
		val = CM_CTRL_LCDMUXSEL_VGA_8421BPP;
	else if (fb->fb.var.bits_per_pixel <= 16)
		val = CM_CTRL_LCDMUXSEL_VGA_16BPP
			| CM_CTRL_LCDEN0 | CM_CTRL_LCDEN1
			| CM_CTRL_STATIC1 | CM_CTRL_STATIC2;
	struct fb_var_screeninfo *var = &fb->fb.var;
	u32 val = CM_CTRL_STATIC1 | CM_CTRL_STATIC2
			| CM_CTRL_LCDEN0 | CM_CTRL_LCDEN1;

	if (var->bits_per_pixel <= 8 ||
	    (var->bits_per_pixel == 16 && var->green.length == 5))
		/* Pseudocolor, RGB555, BGR555 */
		val |= CM_CTRL_LCDMUXSEL_VGA555_TFT555;
	else if (fb->fb.var.bits_per_pixel <= 16)
		/* truecolor RGB565 */
		val |= CM_CTRL_LCDMUXSEL_VGA565_TFT555;
	else
		val = 0; /* no idea for this, don't trust the docs */

	cm_control(CM_CTRL_LCDMUXSEL_MASK|
		   CM_CTRL_LCDEN0|
		   CM_CTRL_LCDEN1|
		   CM_CTRL_STATIC1|
		   CM_CTRL_STATIC2|
		   CM_CTRL_STATIC|
		   CM_CTRL_n24BITEN, val);
}

static unsigned long framesize = SZ_1M;

static int cp_clcd_setup(struct clcd_fb *fb)
{
	dma_addr_t dma;

	fb->panel = &vga;

	fb->fb.screen_base = dma_alloc_writecombine(&fb->dev->dev, framesize,
						    &dma, GFP_KERNEL);
	if (!fb->fb.screen_base) {
		printk(KERN_ERR "CLCD: unable to map framebuffer\n");
		return -ENOMEM;
	}

	fb->fb.fix.smem_start	= dma;
	fb->fb.fix.smem_len	= framesize;

	return 0;
}

static int cp_clcd_mmap(struct clcd_fb *fb, struct vm_area_struct *vma)
{
	return dma_mmap_writecombine(&fb->dev->dev, vma,
				     fb->fb.screen_base,
				     fb->fb.fix.smem_start,
				     fb->fb.fix.smem_len);
}

static void cp_clcd_remove(struct clcd_fb *fb)
{
	dma_free_writecombine(&fb->dev->dev, fb->fb.fix.smem_len,
			      fb->fb.screen_base, fb->fb.fix.smem_start);
static int cp_clcd_setup(struct clcd_fb *fb)
{
	fb->panel = versatile_clcd_get_panel("VGA");
	if (!fb->panel)
		return -EINVAL;

	return versatile_clcd_setup_dma(fb, SZ_1M);
}

static struct clcd_board clcd_data = {
	.name		= "Integrator/CP",
	.caps		= CLCD_CAP_5551 | CLCD_CAP_RGB565 | CLCD_CAP_888,
	.check		= clcdfb_check,
	.decode		= clcdfb_decode,
	.enable		= cp_clcd_enable,
	.setup		= cp_clcd_setup,
	.mmap		= cp_clcd_mmap,
	.remove		= cp_clcd_remove,
};

static struct amba_device clcd_device = {
	.dev		= {
		.bus_id	= "mb:c0",
		.coherent_dma_mask = ~0,
		.platform_data = &clcd_data,
	},
	.res		= {
		.start	= INTCP_PA_CLCD_BASE,
		.end	= INTCP_PA_CLCD_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	.dma_mask	= ~0,
	.irq		= { IRQ_CP_CLCDCINT, NO_IRQ },
	.periphid	= 0,
};

static struct amba_device *amba_devs[] __initdata = {
	&mmc_device,
	&aaci_device,
	&clcd_device,
};

static void __init intcp_init(void)
{
	int i;

	clk_register(&cp_clcd_clk);
	clk_register(&cp_mmci_clk);

	platform_add_devices(intcp_devs, ARRAY_SIZE(intcp_devs));

	for (i = 0; i < ARRAY_SIZE(amba_devs); i++) {
		struct amba_device *d = amba_devs[i];
		amba_device_register(d, &iomem_resource);
	}
}

#define TIMER_CTRL_IE	(1 << 5)			/* Interrupt Enable */

static void __init intcp_timer_init(void)
{
	integrator_time_init(1000000 / HZ, TIMER_CTRL_IE);
}

static struct sys_timer cp_timer = {
	.init		= intcp_timer_init,
	.offset		= integrator_gettimeoffset,
};

MACHINE_START(CINTEGRATOR, "ARM-IntegratorCP")
	/* Maintainer: ARM Ltd/Deep Blue Solutions Ltd */
	.phys_io	= 0x16000000,
	.io_pg_offst	= ((0xf1600000) >> 18) & 0xfffc,
	.boot_params	= 0x00000100,
	.map_io		= intcp_map_io,
	.init_irq	= intcp_init_irq,
	.timer		= &cp_timer,
	.init_machine	= intcp_init,
	.mmap		= versatile_clcd_mmap_dma,
	.remove		= versatile_clcd_remove_dma,
};

#define REFCOUNTER (__io_address(INTEGRATOR_HDR_BASE) + 0x28)

static u64 notrace intcp_read_sched_clock(void)
{
	return readl(REFCOUNTER);
}

static void __init intcp_init_early(void)
{
	sched_clock_register(intcp_read_sched_clock, 32, 24000000);
}

static void __init intcp_init_irq_of(void)
{
	cm_init();
	irqchip_init();
}

/*
 * For the Device Tree, add in the UART, MMC and CLCD specifics as AUXDATA
 * and enforce the bus names since these are used for clock lookups.
 */
static struct of_dev_auxdata intcp_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("arm,primecell", INTEGRATOR_RTC_BASE,
		"rtc", NULL),
	OF_DEV_AUXDATA("arm,primecell", INTEGRATOR_UART0_BASE,
		"uart0", NULL),
	OF_DEV_AUXDATA("arm,primecell", INTEGRATOR_UART1_BASE,
		"uart1", NULL),
	OF_DEV_AUXDATA("arm,primecell", KMI0_BASE,
		"kmi0", NULL),
	OF_DEV_AUXDATA("arm,primecell", KMI1_BASE,
		"kmi1", NULL),
	OF_DEV_AUXDATA("arm,primecell", INTEGRATOR_CP_MMC_BASE,
		"mmci", &mmc_data),
	OF_DEV_AUXDATA("arm,primecell", INTEGRATOR_CP_AACI_BASE,
		"aaci", &mmc_data),
	OF_DEV_AUXDATA("arm,primecell", INTCP_PA_CLCD_BASE,
		"clcd", &clcd_data),
	OF_DEV_AUXDATA("cfi-flash", INTCP_PA_FLASH_BASE,
		"physmap-flash", &intcp_flash_data),
	{ /* sentinel */ },
};

static const struct of_device_id intcp_syscon_match[] = {
	{ .compatible = "arm,integrator-cp-syscon"},
	{ },
};

static void __init intcp_init_of(void)
{
	struct device_node *cpcon;

	cpcon = of_find_matching_node(NULL, intcp_syscon_match);
	if (!cpcon)
		return;

	intcp_con_base = of_iomap(cpcon, 0);
	if (!intcp_con_base)
		return;

	of_platform_populate(NULL, of_default_bus_match_table,
			     intcp_auxdata_lookup, NULL);
}

static const char * intcp_dt_board_compat[] = {
	"arm,integrator-cp",
	NULL,
};

DT_MACHINE_START(INTEGRATOR_CP_DT, "ARM Integrator/CP (Device Tree)")
	.reserve	= integrator_reserve,
	.map_io		= intcp_map_io,
	.init_early	= intcp_init_early,
	.init_irq	= intcp_init_irq_of,
	.init_machine	= intcp_init_of,
	.dt_compat      = intcp_dt_board_compat,
MACHINE_END
