/*
 * linux/arch/arm/mach-omap2/board-generic.c
 *
 * Copyright (C) 2005 Nokia Corporation
 * Author: Paul Mundt <paul.mundt@nokia.com>
 *
 * Modified from mach-omap/omap1/board-generic.c
 *
 * Code for generic OMAP2 board. Should work on many OMAP2 systems where
 * the bootloader passes the board-specific data to the kernel.
 * Do not put any board specific code to this file; create a new machine
 * type if you need custom low-level initializations.
 * Copyright (C) 2005 Nokia Corporation
 * Author: Paul Mundt <paul.mundt@nokia.com>
 *
 * Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com/
 *
 * Modified from the original mach-omap/omap2/board-generic.c did by Paul
 * to support the OMAP2+ device tree boards with an unique board file.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/gpio.h>
#include <mach/mux.h>
#include <mach/usb.h>
#include <mach/board.h>
#include <mach/common.h>

static void __init omap_generic_init_irq(void)
{
	omap2_init_common_hw();
	omap_init_irq();
}

static struct omap_uart_config generic_uart_config __initdata = {
	.enabled_uarts = ((1 << 0) | (1 << 1) | (1 << 2)),
};

static struct omap_mmc_config generic_mmc_config __initdata = {
	.mmc [0] = {
		.enabled 	= 0,
		.wire4		= 0,
		.wp_pin		= -1,
		.power_pin	= -1,
		.switch_pin	= -1,
	},
};

static struct omap_board_config_kernel generic_config[] = {
	{ OMAP_TAG_UART,	&generic_uart_config },
	{ OMAP_TAG_MMC,		&generic_mmc_config },
#include <linux/io.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/irqdomain.h>

#include <asm/mach/arch.h>

#include "common.h"

static const struct of_device_id omap_dt_match_table[] __initconst = {
	{ .compatible = "simple-bus", },
	{ .compatible = "ti,omap-infra", },
	{ }
};

static void __init omap_generic_init(void)
{
	omap_board_config = generic_config;
	omap_board_config_size = ARRAY_SIZE(generic_config);
	omap_serial_init();
}

static void __init omap_generic_map_io(void)
{
	omap2_set_globals_242x(); /* should be 242x, 243x, or 343x */
	omap2_map_common_io();
}

MACHINE_START(OMAP_GENERIC, "Generic OMAP24xx")
	/* Maintainer: Paul Mundt <paul.mundt@nokia.com> */
	.phys_io	= 0x48000000,
	.io_pg_offst	= ((0xd8000000) >> 18) & 0xfffc,
	.boot_params	= 0x80000100,
	.map_io		= omap_generic_map_io,
	.init_irq	= omap_generic_init_irq,
	.init_machine	= omap_generic_init,
	.timer		= &omap_timer,
MACHINE_END
	omapdss_early_init_of();

	pdata_quirks_init(omap_dt_match_table);

	omapdss_init_of();
}

#ifdef CONFIG_SOC_OMAP2420
static const char *const omap242x_boards_compat[] __initconst = {
	"ti,omap2420",
	NULL,
};

DT_MACHINE_START(OMAP242X_DT, "Generic OMAP2420 (Flattened Device Tree)")
	.reserve	= omap_reserve,
	.map_io		= omap242x_map_io,
	.init_early	= omap2420_init_early,
	.init_machine	= omap_generic_init,
	.init_time	= omap_init_time,
	.dt_compat	= omap242x_boards_compat,
	.restart	= omap2xxx_restart,
MACHINE_END
#endif

#ifdef CONFIG_SOC_OMAP2430
static const char *const omap243x_boards_compat[] __initconst = {
	"ti,omap2430",
	NULL,
};

DT_MACHINE_START(OMAP243X_DT, "Generic OMAP2430 (Flattened Device Tree)")
	.reserve	= omap_reserve,
	.map_io		= omap243x_map_io,
	.init_early	= omap2430_init_early,
	.init_machine	= omap_generic_init,
	.init_time	= omap_init_time,
	.dt_compat	= omap243x_boards_compat,
	.restart	= omap2xxx_restart,
MACHINE_END
#endif

#ifdef CONFIG_ARCH_OMAP3
/* Some boards need board name for legacy userspace in /proc/cpuinfo */
static const char *const n900_boards_compat[] __initconst = {
	"nokia,omap3-n900",
	NULL,
};

DT_MACHINE_START(OMAP3_N900_DT, "Nokia RX-51 board")
	.reserve	= omap_reserve,
	.map_io		= omap3_map_io,
	.init_early	= omap3430_init_early,
	.init_machine	= omap_generic_init,
	.init_late	= omap3_init_late,
	.init_time	= omap_init_time,
	.dt_compat	= n900_boards_compat,
	.restart	= omap3xxx_restart,
MACHINE_END

/* Generic omap3 boards, most boards can use these */
static const char *const omap3_boards_compat[] __initconst = {
	"ti,omap3430",
	"ti,omap3",
	NULL,
};

DT_MACHINE_START(OMAP3_DT, "Generic OMAP3 (Flattened Device Tree)")
	.reserve	= omap_reserve,
	.map_io		= omap3_map_io,
	.init_early	= omap3430_init_early,
	.init_machine	= omap_generic_init,
	.init_late	= omap3_init_late,
	.init_time	= omap_init_time,
	.dt_compat	= omap3_boards_compat,
	.restart	= omap3xxx_restart,
MACHINE_END

static const char *const omap36xx_boards_compat[] __initconst = {
	"ti,omap3630",
	"ti,omap36xx",
	NULL,
};

DT_MACHINE_START(OMAP36XX_DT, "Generic OMAP36xx (Flattened Device Tree)")
	.reserve	= omap_reserve,
	.map_io		= omap3_map_io,
	.init_early	= omap3630_init_early,
	.init_machine	= omap_generic_init,
	.init_late	= omap3_init_late,
	.init_time	= omap_init_time,
	.dt_compat	= omap36xx_boards_compat,
	.restart	= omap3xxx_restart,
MACHINE_END

static const char *const omap3_gp_boards_compat[] __initconst = {
	"ti,omap3-beagle",
	"timll,omap3-devkit8000",
	NULL,
};

DT_MACHINE_START(OMAP3_GP_DT, "Generic OMAP3-GP (Flattened Device Tree)")
	.reserve	= omap_reserve,
	.map_io		= omap3_map_io,
	.init_early	= omap3430_init_early,
	.init_machine	= omap_generic_init,
	.init_late	= omap3_init_late,
	.init_time	= omap3_secure_sync32k_timer_init,
	.dt_compat	= omap3_gp_boards_compat,
	.restart	= omap3xxx_restart,
MACHINE_END

static const char *const am3517_boards_compat[] __initconst = {
	"ti,am3517",
	NULL,
};

DT_MACHINE_START(AM3517_DT, "Generic AM3517 (Flattened Device Tree)")
	.reserve	= omap_reserve,
	.map_io		= omap3_map_io,
	.init_early	= am35xx_init_early,
	.init_machine	= omap_generic_init,
	.init_late	= omap3_init_late,
	.init_time	= omap3_gptimer_timer_init,
	.dt_compat	= am3517_boards_compat,
	.restart	= omap3xxx_restart,
MACHINE_END
#endif

#ifdef CONFIG_SOC_TI81XX
static const char *const ti814x_boards_compat[] __initconst = {
	"ti,dm8148",
	"ti,dm814",
	NULL,
};

DT_MACHINE_START(TI814X_DT, "Generic ti814x (Flattened Device Tree)")
	.reserve	= omap_reserve,
	.map_io		= ti81xx_map_io,
	.init_early	= ti814x_init_early,
	.init_machine	= omap_generic_init,
	.init_late	= ti81xx_init_late,
	.init_time	= omap3_gptimer_timer_init,
	.dt_compat	= ti814x_boards_compat,
	.restart	= ti81xx_restart,
MACHINE_END

static const char *const ti816x_boards_compat[] __initconst = {
	"ti,dm8168",
	"ti,dm816",
	NULL,
};

DT_MACHINE_START(TI816X_DT, "Generic ti816x (Flattened Device Tree)")
	.reserve	= omap_reserve,
	.map_io		= ti81xx_map_io,
	.init_early	= ti816x_init_early,
	.init_machine	= omap_generic_init,
	.init_late	= ti81xx_init_late,
	.init_time	= omap3_gptimer_timer_init,
	.dt_compat	= ti816x_boards_compat,
	.restart	= ti81xx_restart,
MACHINE_END
#endif

#ifdef CONFIG_SOC_AM33XX
static const char *const am33xx_boards_compat[] __initconst = {
	"ti,am33xx",
	NULL,
};

DT_MACHINE_START(AM33XX_DT, "Generic AM33XX (Flattened Device Tree)")
	.reserve	= omap_reserve,
	.map_io		= am33xx_map_io,
	.init_early	= am33xx_init_early,
	.init_machine	= omap_generic_init,
	.init_late	= am33xx_init_late,
	.init_time	= omap3_gptimer_timer_init,
	.dt_compat	= am33xx_boards_compat,
	.restart	= am33xx_restart,
MACHINE_END
#endif

#ifdef CONFIG_ARCH_OMAP4
static const char *const omap4_boards_compat[] __initconst = {
	"ti,omap4460",
	"ti,omap4430",
	"ti,omap4",
	NULL,
};

DT_MACHINE_START(OMAP4_DT, "Generic OMAP4 (Flattened Device Tree)")
	.l2c_aux_val	= OMAP_L2C_AUX_CTRL,
	.l2c_aux_mask	= 0xcf9fffff,
	.l2c_write_sec	= omap4_l2c310_write_sec,
	.reserve	= omap_reserve,
	.smp		= smp_ops(omap4_smp_ops),
	.map_io		= omap4_map_io,
	.init_early	= omap4430_init_early,
	.init_irq	= omap_gic_of_init,
	.init_machine	= omap_generic_init,
	.init_late	= omap4430_init_late,
	.init_time	= omap4_local_timer_init,
	.dt_compat	= omap4_boards_compat,
	.restart	= omap44xx_restart,
MACHINE_END
#endif

#ifdef CONFIG_SOC_OMAP5
static const char *const omap5_boards_compat[] __initconst = {
	"ti,omap5432",
	"ti,omap5430",
	"ti,omap5",
	NULL,
};

DT_MACHINE_START(OMAP5_DT, "Generic OMAP5 (Flattened Device Tree)")
#if defined(CONFIG_ZONE_DMA) && defined(CONFIG_ARM_LPAE)
	.dma_zone_size	= SZ_2G,
#endif
	.reserve	= omap_reserve,
	.smp		= smp_ops(omap4_smp_ops),
	.map_io		= omap5_map_io,
	.init_early	= omap5_init_early,
	.init_irq	= omap_gic_of_init,
	.init_machine	= omap_generic_init,
	.init_late	= omap5_init_late,
	.init_time	= omap5_realtime_timer_init,
	.dt_compat	= omap5_boards_compat,
	.restart	= omap44xx_restart,
MACHINE_END
#endif

#ifdef CONFIG_SOC_AM43XX
static const char *const am43_boards_compat[] __initconst = {
	"ti,am4372",
	"ti,am43",
	NULL,
};

DT_MACHINE_START(AM43_DT, "Generic AM43 (Flattened Device Tree)")
	.l2c_aux_val	= OMAP_L2C_AUX_CTRL,
	.l2c_aux_mask	= 0xcf9fffff,
	.l2c_write_sec	= omap4_l2c310_write_sec,
	.map_io		= am33xx_map_io,
	.init_early	= am43xx_init_early,
	.init_late	= am43xx_init_late,
	.init_irq	= omap_gic_of_init,
	.init_machine	= omap_generic_init,
	.init_time	= omap4_local_timer_init,
	.dt_compat	= am43_boards_compat,
	.restart	= omap44xx_restart,
MACHINE_END
#endif

#ifdef CONFIG_SOC_DRA7XX
static const char *const dra74x_boards_compat[] __initconst = {
	"ti,am5728",
	"ti,am5726",
	"ti,dra742",
	"ti,dra7",
	NULL,
};

DT_MACHINE_START(DRA74X_DT, "Generic DRA74X (Flattened Device Tree)")
#if defined(CONFIG_ZONE_DMA) && defined(CONFIG_ARM_LPAE)
	.dma_zone_size	= SZ_2G,
#endif
	.reserve	= omap_reserve,
	.smp		= smp_ops(omap4_smp_ops),
	.map_io		= dra7xx_map_io,
	.init_early	= dra7xx_init_early,
	.init_late	= dra7xx_init_late,
	.init_irq	= omap_gic_of_init,
	.init_machine	= omap_generic_init,
	.init_time	= omap5_realtime_timer_init,
	.dt_compat	= dra74x_boards_compat,
	.restart	= omap44xx_restart,
MACHINE_END

static const char *const dra72x_boards_compat[] __initconst = {
	"ti,am5718",
	"ti,am5716",
	"ti,dra722",
	NULL,
};

DT_MACHINE_START(DRA72X_DT, "Generic DRA72X (Flattened Device Tree)")
#if defined(CONFIG_ZONE_DMA) && defined(CONFIG_ARM_LPAE)
	.dma_zone_size	= SZ_2G,
#endif
	.reserve	= omap_reserve,
	.map_io		= dra7xx_map_io,
	.init_early	= dra7xx_init_early,
	.init_late	= dra7xx_init_late,
	.init_irq	= omap_gic_of_init,
	.init_machine	= omap_generic_init,
	.init_time	= omap5_realtime_timer_init,
	.dt_compat	= dra72x_boards_compat,
	.restart	= omap44xx_restart,
MACHINE_END
#endif
