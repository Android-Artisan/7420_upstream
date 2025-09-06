/*
 *  linux/arch/arm/mach-omap2/clock.h
 *
 *  Copyright (C) 2005-2008 Texas Instruments, Inc.
 *  Copyright (C) 2004-2008 Nokia Corporation
 *  Copyright (C) 2005-2009 Texas Instruments, Inc.
 *  Copyright (C) 2004-2011 Nokia Corporation
 *
 *  Contacts:
 *  Richard Woodruff <r-woodruff2@ti.com>
 *  Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARCH_ARM_MACH_OMAP2_CLOCK_H
#define __ARCH_ARM_MACH_OMAP2_CLOCK_H

#include <mach/clock.h>

/* The maximum error between a target DPLL rate and the rounded rate in Hz */
#define DEFAULT_DPLL_RATE_TOLERANCE	50000

int omap2_clk_enable(struct clk *clk);
void omap2_clk_disable(struct clk *clk);
long omap2_clk_round_rate(struct clk *clk, unsigned long rate);
int omap2_clk_set_rate(struct clk *clk, unsigned long rate);
int omap2_clk_set_parent(struct clk *clk, struct clk *new_parent);
int omap2_dpll_rate_tolerance_set(struct clk *clk, unsigned int tolerance);
long omap2_dpll_round_rate(struct clk *clk, unsigned long target_rate);

#ifdef CONFIG_OMAP_RESET_CLOCKS
void omap2_clk_disable_unused(struct clk *clk);
#else
#define omap2_clk_disable_unused	NULL
#endif

void omap2_clksel_recalc(struct clk *clk);
void omap2_init_clksel_parent(struct clk *clk);
u32 omap2_clksel_get_divisor(struct clk *clk);
u32 omap2_clksel_round_rate_div(struct clk *clk, unsigned long target_rate,
				u32 *new_div);
u32 omap2_clksel_to_divisor(struct clk *clk, u32 field_val);
u32 omap2_divisor_to_clksel(struct clk *clk, u32 div);
void omap2_fixed_divisor_recalc(struct clk *clk);
long omap2_clksel_round_rate(struct clk *clk, unsigned long target_rate);
int omap2_clksel_set_rate(struct clk *clk, unsigned long rate);
u32 omap2_get_dpll_rate(struct clk *clk);
int omap2_wait_clock_ready(void __iomem *reg, u32 cval, const char *name);
void omap2_clk_prepare_for_reboot(void);

extern u8 cpu_mask;

/* clksel_rate data common to 24xx/343x */
static const struct clksel_rate gpt_32k_rates[] = {
	 { .div = 1, .val = 0, .flags = RATE_IN_24XX | RATE_IN_343X | DEFAULT_RATE },
	 { .div = 0 }
};

static const struct clksel_rate gpt_sys_rates[] = {
	 { .div = 1, .val = 1, .flags = RATE_IN_24XX | RATE_IN_343X | DEFAULT_RATE },
	 { .div = 0 }
};

static const struct clksel_rate gfx_l3_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_24XX | RATE_IN_343X },
	{ .div = 2, .val = 2, .flags = RATE_IN_24XX | RATE_IN_343X | DEFAULT_RATE },
	{ .div = 3, .val = 3, .flags = RATE_IN_243X | RATE_IN_343X },
	{ .div = 4, .val = 4, .flags = RATE_IN_243X | RATE_IN_343X },
	{ .div = 0 }
};


#include <linux/kernel.h>
#include <linux/list.h>

#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/clk/ti.h>

/* struct clksel_rate.flags possibilities */
#define RATE_IN_242X		(1 << 0)
#define RATE_IN_243X		(1 << 1)
#define RATE_IN_3430ES1		(1 << 2)	/* 3430ES1 rates only */
#define RATE_IN_3430ES2PLUS	(1 << 3)	/* 3430 ES >= 2 rates only */
#define RATE_IN_36XX		(1 << 4)
#define RATE_IN_4430		(1 << 5)
#define RATE_IN_TI816X		(1 << 6)
#define RATE_IN_4460		(1 << 7)
#define RATE_IN_AM33XX		(1 << 8)
#define RATE_IN_TI814X		(1 << 9)

#define RATE_IN_24XX		(RATE_IN_242X | RATE_IN_243X)
#define RATE_IN_34XX		(RATE_IN_3430ES1 | RATE_IN_3430ES2PLUS)
#define RATE_IN_3XXX		(RATE_IN_34XX | RATE_IN_36XX)
#define RATE_IN_44XX		(RATE_IN_4430 | RATE_IN_4460)

/* RATE_IN_3430ES2PLUS_36XX includes 34xx/35xx with ES >=2, and all 36xx/37xx */
#define RATE_IN_3430ES2PLUS_36XX	(RATE_IN_3430ES2PLUS | RATE_IN_36XX)

/* CM_CLKSEL2_PLL.CORE_CLK_SRC bits (2XXX) */
#define CORE_CLK_SRC_32K		0x0
#define CORE_CLK_SRC_DPLL		0x1
#define CORE_CLK_SRC_DPLL_X2		0x2

/* OMAP2xxx CM_CLKEN_PLL.EN_DPLL bits - for omap2_get_dpll_rate() */
#define OMAP2XXX_EN_DPLL_LPBYPASS		0x1
#define OMAP2XXX_EN_DPLL_FRBYPASS		0x2
#define OMAP2XXX_EN_DPLL_LOCKED			0x3

/* OMAP3xxx CM_CLKEN_PLL*.EN_*_DPLL bits - for omap2_get_dpll_rate() */
#define OMAP3XXX_EN_DPLL_LPBYPASS		0x5
#define OMAP3XXX_EN_DPLL_FRBYPASS		0x6
#define OMAP3XXX_EN_DPLL_LOCKED			0x7

/* OMAP4xxx CM_CLKMODE_DPLL*.EN_*_DPLL bits - for omap2_get_dpll_rate() */
#define OMAP4XXX_EN_DPLL_MNBYPASS		0x4
#define OMAP4XXX_EN_DPLL_LPBYPASS		0x5
#define OMAP4XXX_EN_DPLL_FRBYPASS		0x6
#define OMAP4XXX_EN_DPLL_LOCKED			0x7

void omap2_clk_print_new_rates(const char *hfclkin_ck_name,
			       const char *core_ck_name,
			       const char *mpu_ck_name);

extern u16 cpu_mask;

extern const struct clkops clkops_omap2_dflt_wait;
extern const struct clkops clkops_omap2_dflt;

extern struct clk_functions omap2_clk_functions;

int __init omap2_clk_setup_ll_ops(void);

void __init ti_clk_init_features(void);
#endif
