/*
 *  linux/arch/arm/mach-ep93xx/micro9.c
 *
 * Copyright (C) 2006 Contec Steuerungstechnik & Automation GmbH
 *                   Manfred Gruber <manfred.gruber@contec.at>
 *                    Manfred Gruber <m.gruber@tirol.com>
 * Copyright (C) 2009 Contec Steuerungstechnik & Automation GmbH
 *                    Hubert Feurstein <hubert.feurstein@contec.at>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/platform_device.h>
#include <linux/sched.h>

#include <linux/mtd/physmap.h>

#include <asm/io.h>
#include <mach/hardware.h>

#include <asm/mach/arch.h>
#include <asm/mach-types.h>

static struct ep93xx_eth_data micro9_eth_data = {
       .phy_id                 = 0x1f,
};

static struct resource micro9_eth_resource[] = {
       {
               .start  = EP93XX_ETHERNET_PHYS_BASE,
               .end    = EP93XX_ETHERNET_PHYS_BASE + 0xffff,
               .flags  = IORESOURCE_MEM,
       }, {
               .start  = IRQ_EP93XX_ETHERNET,
               .end    = IRQ_EP93XX_ETHERNET,
               .flags  = IORESOURCE_IRQ,
       }
};

static struct platform_device micro9_eth_device = {
       .name           = "ep93xx-eth",
       .id             = -1,
       .dev            = {
               .platform_data  = &micro9_eth_data,
       },
       .num_resources = ARRAY_SIZE(micro9_eth_resource),
       .resource       = micro9_eth_resource,
};

static void __init micro9_eth_init(void)
{
       memcpy(micro9_eth_data.dev_addr,
               (void *)(EP93XX_ETHERNET_BASE + 0x50), 6);
       platform_device_register(&micro9_eth_device);
}

static void __init micro9_init(void)
{
       micro9_eth_init();
}

/*
 * Micro9-H
 */
#ifdef CONFIG_MACH_MICRO9H
static struct physmap_flash_data micro9h_flash_data = {
       .width          = 4,
};

static struct resource micro9h_flash_resource = {
       .start          = 0x10000000,
       .end            = 0x13ffffff,
       .flags          = IORESOURCE_MEM,
};

static struct platform_device micro9h_flash = {
       .name           = "physmap-flash",
       .id             = 0,
       .dev            = {
               .platform_data  = &micro9h_flash_data,
       },
       .num_resources  = 1,
       .resource       = &micro9h_flash_resource,
};

static void __init micro9h_init(void)
{
       platform_device_register(&micro9h_flash);
}

static void __init micro9h_init_machine(void)
{
       ep93xx_init_devices();
       micro9_init();
       micro9h_init();
}

MACHINE_START(MICRO9, "Contec Hypercontrol Micro9-H")
       /* Maintainer: Manfred Gruber <manfred.gruber@contec.at> */
       .phys_io        = EP93XX_APB_PHYS_BASE,
       .io_pg_offst    = ((EP93XX_APB_VIRT_BASE) >> 18) & 0xfffc,
       .boot_params    = 0x00000100,
       .map_io         = ep93xx_map_io,
       .init_irq       = ep93xx_init_irq,
       .timer          = &ep93xx_timer,
       .init_machine   = micro9h_init_machine,
MACHINE_END
#endif

/*
 * Micro9-M
 */
#ifdef CONFIG_MACH_MICRO9M
static void __init micro9m_init_machine(void)
{
       ep93xx_init_devices();
       micro9_init();
}

MACHINE_START(MICRO9M, "Contec Hypercontrol Micro9-M")
       /* Maintainer: Manfred Gruber <manfred.gruber@contec.at> */
       .phys_io        = EP93XX_APB_PHYS_BASE,
       .io_pg_offst    = ((EP93XX_APB_VIRT_BASE) >> 18) & 0xfffc,
       .boot_params    = 0x00000100,
       .map_io         = ep93xx_map_io,
       .init_irq       = ep93xx_init_irq,
       .timer          = &ep93xx_timer,
       .init_machine   = micro9m_init_machine,
MACHINE_END
#endif

/*
 * Micro9-L
 */
#ifdef CONFIG_MACH_MICRO9L
static void __init micro9l_init_machine(void)
{
       ep93xx_init_devices();
       micro9_init();
}

MACHINE_START(MICRO9L, "Contec Hypercontrol Micro9-L")
       /* Maintainer: Manfred Gruber <manfred.gruber@contec.at> */
       .phys_io        = EP93XX_APB_PHYS_BASE,
       .io_pg_offst    = ((EP93XX_APB_VIRT_BASE) >> 18) & 0xfffc,
       .boot_params    = 0x00000100,
       .map_io         = ep93xx_map_io,
       .init_irq       = ep93xx_init_irq,
       .timer          = &ep93xx_timer,
       .init_machine   = micro9l_init_machine,
MACHINE_END
#endif

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#include <mach/hardware.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include "soc.h"

/*************************************************************************
 * Micro9 NOR Flash
 *
 * Micro9-High has up to 64MB of 32-bit flash on CS1
 * Micro9-Mid has up to 64MB of either 32-bit or 16-bit flash on CS1
 * Micro9-Lite uses a separate MTD map driver for flash support
 * Micro9-Slim has up to 64MB of either 32-bit or 16-bit flash on CS1
 *************************************************************************/
static unsigned int __init micro9_detect_bootwidth(void)
{
	u32 v;

	/* Detect the bus width of the external flash memory */
	v = __raw_readl(EP93XX_SYSCON_SYSCFG);
	if (v & EP93XX_SYSCON_SYSCFG_LCSN7)
		return 4; /* 32-bit */
	else
		return 2; /* 16-bit */
}

static void __init micro9_register_flash(void)
{
	unsigned int width;

	if (machine_is_micro9())
		width = 4;
	else if (machine_is_micro9m() || machine_is_micro9s())
		width = micro9_detect_bootwidth();
	else
		width = 0;

	if (width)
		ep93xx_register_flash(width, EP93XX_CS1_PHYS_BASE, SZ_64M);
}


/*************************************************************************
 * Micro9 Ethernet
 *************************************************************************/
static struct ep93xx_eth_data __initdata micro9_eth_data = {
	.phy_id		= 0x1f,
};


static void __init micro9_init_machine(void)
{
	ep93xx_init_devices();
	ep93xx_register_eth(&micro9_eth_data, 1);
	micro9_register_flash();
}


#ifdef CONFIG_MACH_MICRO9H
MACHINE_START(MICRO9, "Contec Micro9-High")
	/* Maintainer: Hubert Feurstein <hubert.feurstein@contec.at> */
	.atag_offset	= 0x100,
	.map_io		= ep93xx_map_io,
	.init_irq	= ep93xx_init_irq,
	.init_time	= ep93xx_timer_init,
	.init_machine	= micro9_init_machine,
	.init_late	= ep93xx_init_late,
	.restart	= ep93xx_restart,
MACHINE_END
#endif

#ifdef CONFIG_MACH_MICRO9M
MACHINE_START(MICRO9M, "Contec Micro9-Mid")
	/* Maintainer: Hubert Feurstein <hubert.feurstein@contec.at> */
	.atag_offset	= 0x100,
	.map_io		= ep93xx_map_io,
	.init_irq	= ep93xx_init_irq,
	.init_time	= ep93xx_timer_init,
	.init_machine	= micro9_init_machine,
	.init_late	= ep93xx_init_late,
	.restart	= ep93xx_restart,
MACHINE_END
#endif

#ifdef CONFIG_MACH_MICRO9L
MACHINE_START(MICRO9L, "Contec Micro9-Lite")
	/* Maintainer: Hubert Feurstein <hubert.feurstein@contec.at> */
	.atag_offset	= 0x100,
	.map_io		= ep93xx_map_io,
	.init_irq	= ep93xx_init_irq,
	.init_time	= ep93xx_timer_init,
	.init_machine	= micro9_init_machine,
	.init_late	= ep93xx_init_late,
	.restart	= ep93xx_restart,
MACHINE_END
#endif

#ifdef CONFIG_MACH_MICRO9S
MACHINE_START(MICRO9S, "Contec Micro9-Slim")
	/* Maintainer: Hubert Feurstein <hubert.feurstein@contec.at> */
	.atag_offset	= 0x100,
	.map_io		= ep93xx_map_io,
	.init_irq	= ep93xx_init_irq,
	.init_time	= ep93xx_timer_init,
	.init_machine	= micro9_init_machine,
	.init_late	= ep93xx_init_late,
	.restart	= ep93xx_restart,
MACHINE_END
#endif
