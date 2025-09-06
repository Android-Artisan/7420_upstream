/*
 *  linux/arch/arm/mach-omap1/clock.h
 *
 *  Copyright (C) 2004 - 2005 Nokia corporation
 *  Copyright (C) 2004 - 2005, 2009 Nokia corporation
 *  Written by Tuukka Tikkanen <tuukka.tikkanen@elektrobit.com>
 *  Based on clocks.h by Tony Lindgren, Gordon McNutt and RidgeRun, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARCH_ARM_MACH_OMAP1_CLOCK_H
#define __ARCH_ARM_MACH_OMAP1_CLOCK_H

static int omap1_clk_enable_generic(struct clk * clk);
static void omap1_clk_disable_generic(struct clk * clk);
static void omap1_ckctl_recalc(struct clk * clk);
static void omap1_watchdog_recalc(struct clk * clk);
static int omap1_set_sossi_rate(struct clk *clk, unsigned long rate);
static void omap1_sossi_recalc(struct clk *clk);
static void omap1_ckctl_recalc_dsp_domain(struct clk * clk);
static int omap1_clk_enable_dsp_domain(struct clk * clk);
static int omap1_clk_set_rate_dsp_domain(struct clk * clk, unsigned long rate);
static void omap1_clk_disable_dsp_domain(struct clk * clk);
static int omap1_set_uart_rate(struct clk * clk, unsigned long rate);
static void omap1_uart_recalc(struct clk * clk);
static int omap1_clk_enable_uart_functional(struct clk * clk);
static void omap1_clk_disable_uart_functional(struct clk * clk);
static int omap1_set_ext_clk_rate(struct clk * clk, unsigned long rate);
static long omap1_round_ext_clk_rate(struct clk * clk, unsigned long rate);
static void omap1_init_ext_clk(struct clk * clk);
static int omap1_select_table_rate(struct clk * clk, unsigned long rate);
static long omap1_round_to_table_rate(struct clk * clk, unsigned long rate);
static int omap1_clk_enable(struct clk *clk);
static void omap1_clk_disable(struct clk *clk);

struct mpu_rate {
	unsigned long		rate;
	unsigned long		xtal;
	unsigned long		pll_rate;
	__u16			ckctl_val;
	__u16			dpllctl_val;
};

#include <linux/clk.h>
#include <linux/list.h>

#include <linux/clkdev.h>

struct module;
struct clk;

struct omap_clk {
	u16				cpu;
	struct clk_lookup		lk;
};

#define CLK(dev, con, ck, cp)		\
	{				\
		 .cpu = cp,		\
		.lk = {			\
			.dev_id = dev,	\
			.con_id = con,	\
			.clk = ck,	\
		},			\
	}

/* Platform flags for the clkdev-OMAP integration code */
#define CK_310		(1 << 0)
#define CK_7XX		(1 << 1)	/* 7xx, 850 */
#define CK_1510		(1 << 2)
#define CK_16XX		(1 << 3)	/* 16xx, 17xx, 5912 */
#define CK_1710		(1 << 4)	/* 1710 extra for rate selection */


/* Temporary, needed during the common clock framework conversion */
#define __clk_get_name(clk)	(clk->name)
#define __clk_get_parent(clk)	(clk->parent)
#define __clk_get_rate(clk)	(clk->rate)

/**
 * struct clkops - some clock function pointers
 * @enable: fn ptr that enables the current clock in hardware
 * @disable: fn ptr that enables the current clock in hardware
 * @find_idlest: function returning the IDLEST register for the clock's IP blk
 * @find_companion: function returning the "companion" clk reg for the clock
 * @allow_idle: fn ptr that enables autoidle for the current clock in hardware
 * @deny_idle: fn ptr that disables autoidle for the current clock in hardware
 *
 * A "companion" clk is an accompanying clock to the one being queried
 * that must be enabled for the IP module connected to the clock to
 * become accessible by the hardware.  Neither @find_idlest nor
 * @find_companion should be needed; that information is IP
 * block-specific; the hwmod code has been created to handle this, but
 * until hwmod data is ready and drivers have been converted to use PM
 * runtime calls in place of clk_enable()/clk_disable(), @find_idlest and
 * @find_companion must, unfortunately, remain.
 */
struct clkops {
	int			(*enable)(struct clk *);
	void			(*disable)(struct clk *);
	void			(*find_idlest)(struct clk *, void __iomem **,
					       u8 *, u8 *);
	void			(*find_companion)(struct clk *, void __iomem **,
						  u8 *);
	void			(*allow_idle)(struct clk *);
	void			(*deny_idle)(struct clk *);
};

/*
 * struct clk.flags possibilities
 *
 * XXX document the rest of the clock flags here
 *
 * CLOCK_CLKOUTX2: (OMAP4 only) DPLL CLKOUT and CLKOUTX2 GATE_CTRL
 *     bits share the same register.  This flag allows the
 *     omap4_dpllmx*() code to determine which GATE_CTRL bit field
 *     should be used.  This is a temporary solution - a better approach
 *     would be to associate clock type-specific data with the clock,
 *     similar to the struct dpll_data approach.
 */
#define ENABLE_REG_32BIT	(1 << 0)	/* Use 32-bit access */
#define CLOCK_IDLE_CONTROL	(1 << 1)
#define CLOCK_NO_IDLE_PARENT	(1 << 2)
#define ENABLE_ON_INIT		(1 << 3)	/* Enable upon framework init */
#define INVERT_ENABLE		(1 << 4)	/* 0 enables, 1 disables */
#define CLOCK_CLKOUTX2		(1 << 5)

/**
 * struct clk - OMAP struct clk
 * @node: list_head connecting this clock into the full clock list
 * @ops: struct clkops * for this clock
 * @name: the name of the clock in the hardware (used in hwmod data and debug)
 * @parent: pointer to this clock's parent struct clk
 * @children: list_head connecting to the child clks' @sibling list_heads
 * @sibling: list_head connecting this clk to its parent clk's @children
 * @rate: current clock rate
 * @enable_reg: register to write to enable the clock (see @enable_bit)
 * @recalc: fn ptr that returns the clock's current rate
 * @set_rate: fn ptr that can change the clock's current rate
 * @round_rate: fn ptr that can round the clock's current rate
 * @init: fn ptr to do clock-specific initialization
 * @enable_bit: bitshift to write to enable/disable the clock (see @enable_reg)
 * @usecount: number of users that have requested this clock to be enabled
 * @fixed_div: when > 0, this clock's rate is its parent's rate / @fixed_div
 * @flags: see "struct clk.flags possibilities" above
 * @rate_offset: bitshift for rate selection bitfield (OMAP1 only)
 * @src_offset: bitshift for source selection bitfield (OMAP1 only)
 *
 * XXX @rate_offset, @src_offset should probably be removed and OMAP1
 * clock code converted to use clksel.
 *
 * XXX @usecount is poorly named.  It should be "enable_count" or
 * something similar.  "users" in the description refers to kernel
 * code (core code or drivers) that have called clk_enable() and not
 * yet called clk_disable(); the usecount of parent clocks is also
 * incremented by the clock code when clk_enable() is called on child
 * clocks and decremented by the clock code when clk_disable() is
 * called on child clocks.
 *
 * XXX @clkdm, @usecount, @children, @sibling should be marked for
 * internal use only.
 *
 * @children and @sibling are used to optimize parent-to-child clock
 * tree traversals.  (child-to-parent traversals use @parent.)
 *
 * XXX The notion of the clock's current rate probably needs to be
 * separated from the clock's target rate.
 */
struct clk {
	struct list_head	node;
	const struct clkops	*ops;
	const char		*name;
	struct clk		*parent;
	struct list_head	children;
	struct list_head	sibling;	/* node for children */
	unsigned long		rate;
	void __iomem		*enable_reg;
	unsigned long		(*recalc)(struct clk *);
	int			(*set_rate)(struct clk *, unsigned long);
	long			(*round_rate)(struct clk *, unsigned long);
	void			(*init)(struct clk *);
	u8			enable_bit;
	s8			usecount;
	u8			fixed_div;
	u8			flags;
	u8			rate_offset;
	u8			src_offset;
#if defined(CONFIG_PM_DEBUG) && defined(CONFIG_DEBUG_FS)
	struct dentry		*dent;	/* For visible tree hierarchy */
#endif
};

struct clk_functions {
	int		(*clk_enable)(struct clk *clk);
	void		(*clk_disable)(struct clk *clk);
	long		(*clk_round_rate)(struct clk *clk, unsigned long rate);
	int		(*clk_set_rate)(struct clk *clk, unsigned long rate);
	int		(*clk_set_parent)(struct clk *clk, struct clk *parent);
	void		(*clk_allow_idle)(struct clk *clk);
	void		(*clk_deny_idle)(struct clk *clk);
	void		(*clk_disable_unused)(struct clk *clk);
};

extern int mpurate;

extern int clk_init(struct clk_functions *custom_clocks);
extern void clk_preinit(struct clk *clk);
extern int clk_register(struct clk *clk);
extern void clk_reparent(struct clk *child, struct clk *parent);
extern void clk_unregister(struct clk *clk);
extern void propagate_rate(struct clk *clk);
extern void recalculate_root_clocks(void);
extern unsigned long followparent_recalc(struct clk *clk);
extern void clk_enable_init_clocks(void);
unsigned long omap_fixed_divisor_recalc(struct clk *clk);
extern struct clk *omap_clk_get_by_name(const char *name);
extern int omap_clk_enable_autoidle_all(void);
extern int omap_clk_disable_autoidle_all(void);

extern const struct clkops clkops_null;

extern struct clk dummy_ck;

int omap1_clk_init(void);
void omap1_clk_late_init(void);
extern int omap1_clk_enable(struct clk *clk);
extern void omap1_clk_disable(struct clk *clk);
extern long omap1_clk_round_rate(struct clk *clk, unsigned long rate);
extern int omap1_clk_set_rate(struct clk *clk, unsigned long rate);
extern unsigned long omap1_ckctl_recalc(struct clk *clk);
extern int omap1_set_sossi_rate(struct clk *clk, unsigned long rate);
extern unsigned long omap1_sossi_recalc(struct clk *clk);
extern unsigned long omap1_ckctl_recalc_dsp_domain(struct clk *clk);
extern int omap1_clk_set_rate_dsp_domain(struct clk *clk, unsigned long rate);
extern int omap1_set_uart_rate(struct clk *clk, unsigned long rate);
extern unsigned long omap1_uart_recalc(struct clk *clk);
extern int omap1_set_ext_clk_rate(struct clk *clk, unsigned long rate);
extern long omap1_round_ext_clk_rate(struct clk *clk, unsigned long rate);
extern void omap1_init_ext_clk(struct clk *clk);
extern int omap1_select_table_rate(struct clk *clk, unsigned long rate);
extern long omap1_round_to_table_rate(struct clk *clk, unsigned long rate);
extern int omap1_clk_set_rate_ckctl_arm(struct clk *clk, unsigned long rate);
extern long omap1_clk_round_rate_ckctl_arm(struct clk *clk, unsigned long rate);
extern unsigned long omap1_watchdog_recalc(struct clk *clk);

#ifdef CONFIG_OMAP_RESET_CLOCKS
extern void omap1_clk_disable_unused(struct clk *clk);
#else
#define omap1_clk_disable_unused	NULL
#endif

struct uart_clk {
	struct clk	clk;
	unsigned long	sysc_addr;
};

/* Provide a method for preventing idling some ARM IDLECT clocks */
struct arm_idlect1_clk {
	struct clk	clk;
	unsigned long	no_idle_count;
	__u8		idlect_shift;
};

/* ARM_CKCTL bit shifts */
#define CKCTL_PERDIV_OFFSET	0
#define CKCTL_LCDDIV_OFFSET	2
#define CKCTL_ARMDIV_OFFSET	4
#define CKCTL_DSPDIV_OFFSET	6
#define CKCTL_TCDIV_OFFSET	8
#define CKCTL_DSPMMUDIV_OFFSET	10
/*#define ARM_TIMXO		12*/
#define EN_DSPCK		13
/*#define ARM_INTHCK_SEL	14*/ /* Divide-by-2 for mpu inth_ck */
/* DSP_CKCTL bit shifts */
#define CKCTL_DSPPERDIV_OFFSET	0

/* ARM_IDLECT2 bit shifts */
#define EN_WDTCK	0
#define EN_XORPCK	1
#define EN_PERCK	2
#define EN_LCDCK	3
#define EN_LBCK		4 /* Not on 1610/1710 */
/*#define EN_HSABCK	5*/
#define EN_APICK	6
#define EN_TIMCK	7
#define DMACK_REQ	8
#define EN_GPIOCK	9 /* Not on 1610/1710 */
/*#define EN_LBFREECK	10*/
#define EN_CKOUT_ARM	11

/* ARM_IDLECT3 bit shifts */
#define EN_OCPI_CK	0
#define EN_TC1_CK	2
#define EN_TC2_CK	4

/* DSP_IDLECT2 bit shifts (0,1,2 are same as for ARM_IDLECT2) */
#define EN_DSPTIMCK	5

/* Various register defines for clock controls scattered around OMAP chip */
#define SDW_MCLK_INV_BIT	2	/* In ULPD_CLKC_CTRL */
#define USB_MCLK_EN_BIT		4	/* In ULPD_CLKC_CTRL */
#define USB_HOST_HHC_UHOST_EN	9	/* In MOD_CONF_CTRL_0 */
#define SWD_ULPD_PLL_CLK_REQ	1	/* In SWD_CLK_DIV_CTRL_SEL */
#define COM_ULPD_PLL_CLK_REQ	1	/* In COM_CLK_DIV_CTRL_SEL */
#define SWD_CLK_DIV_CTRL_SEL	0xfffe0874
#define COM_CLK_DIV_CTRL_SEL	0xfffe0878
#define SOFT_REQ_REG		0xfffe0834
#define SOFT_REQ_REG2		0xfffe0880

/*-------------------------------------------------------------------------
 * Omap1 MPU rate table
 *-------------------------------------------------------------------------*/
static struct mpu_rate rate_table[] = {
	/* MPU MHz, xtal MHz, dpll1 MHz, CKCTL, DPLL_CTL
	 * NOTE: Comment order here is different from bits in CKCTL value:
	 * armdiv, dspdiv, dspmmu, tcdiv, perdiv, lcddiv
	 */
#if defined(CONFIG_OMAP_ARM_216MHZ)
	{ 216000000, 12000000, 216000000, 0x050d, 0x2910 }, /* 1/1/2/2/2/8 */
#endif
#if defined(CONFIG_OMAP_ARM_195MHZ)
	{ 195000000, 13000000, 195000000, 0x050e, 0x2790 }, /* 1/1/2/2/4/8 */
#endif
#if defined(CONFIG_OMAP_ARM_192MHZ)
	{ 192000000, 19200000, 192000000, 0x050f, 0x2510 }, /* 1/1/2/2/8/8 */
	{ 192000000, 12000000, 192000000, 0x050f, 0x2810 }, /* 1/1/2/2/8/8 */
	{  96000000, 12000000, 192000000, 0x055f, 0x2810 }, /* 2/2/2/2/8/8 */
	{  48000000, 12000000, 192000000, 0x0baf, 0x2810 }, /* 4/4/4/8/8/8 */
	{  24000000, 12000000, 192000000, 0x0fff, 0x2810 }, /* 8/8/8/8/8/8 */
#endif
#if defined(CONFIG_OMAP_ARM_182MHZ)
	{ 182000000, 13000000, 182000000, 0x050e, 0x2710 }, /* 1/1/2/2/4/8 */
#endif
#if defined(CONFIG_OMAP_ARM_168MHZ)
	{ 168000000, 12000000, 168000000, 0x010f, 0x2710 }, /* 1/1/1/2/8/8 */
#endif
#if defined(CONFIG_OMAP_ARM_150MHZ)
	{ 150000000, 12000000, 150000000, 0x010a, 0x2cb0 }, /* 1/1/1/2/4/4 */
#endif
#if defined(CONFIG_OMAP_ARM_120MHZ)
	{ 120000000, 12000000, 120000000, 0x010a, 0x2510 }, /* 1/1/1/2/4/4 */
#endif
#if defined(CONFIG_OMAP_ARM_96MHZ)
	{  96000000, 12000000,  96000000, 0x0005, 0x2410 }, /* 1/1/1/1/2/2 */
#endif
#if defined(CONFIG_OMAP_ARM_60MHZ)
	{  60000000, 12000000,  60000000, 0x0005, 0x2290 }, /* 1/1/1/1/2/2 */
#endif
#if defined(CONFIG_OMAP_ARM_30MHZ)
	{  30000000, 12000000,  60000000, 0x0555, 0x2290 }, /* 2/2/2/2/2/2 */
#endif
	{ 0, 0, 0, 0, 0 },
};

/*-------------------------------------------------------------------------
 * Omap1 clocks
 *-------------------------------------------------------------------------*/

static struct clk ck_ref = {
	.name		= "ck_ref",
	.rate		= 12000000,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  CLOCK_IN_OMAP310 | ALWAYS_ENABLED,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk ck_dpll1 = {
	.name		= "ck_dpll1",
	.parent		= &ck_ref,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  CLOCK_IN_OMAP310 | RATE_PROPAGATES | ALWAYS_ENABLED,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct arm_idlect1_clk ck_dpll1out = {
	.clk = {
		.name		= "ck_dpll1out",
		.parent		= &ck_dpll1,
		.flags		= CLOCK_IN_OMAP16XX | CLOCK_IDLE_CONTROL |
				  ENABLE_REG_32BIT | RATE_PROPAGATES,
		.enable_reg	= (void __iomem *)ARM_IDLECT2,
		.enable_bit	= EN_CKOUT_ARM,
		.recalc		= &followparent_recalc,
		.enable		= &omap1_clk_enable_generic,
		.disable	= &omap1_clk_disable_generic,
	},
	.idlect_shift	= 12,
};

static struct clk sossi_ck = {
	.name		= "ck_sossi",
	.parent		= &ck_dpll1out.clk,
	.flags		= CLOCK_IN_OMAP16XX | CLOCK_NO_IDLE_PARENT |
			  ENABLE_REG_32BIT,
	.enable_reg	= (void __iomem *)MOD_CONF_CTRL_1,
	.enable_bit	= 16,
	.recalc		= &omap1_sossi_recalc,
	.set_rate	= &omap1_set_sossi_rate,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk arm_ck = {
	.name		= "arm_ck",
	.parent		= &ck_dpll1,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  CLOCK_IN_OMAP310 | RATE_CKCTL | RATE_PROPAGATES |
			  ALWAYS_ENABLED,
	.rate_offset	= CKCTL_ARMDIV_OFFSET,
	.recalc		= &omap1_ckctl_recalc,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct arm_idlect1_clk armper_ck = {
	.clk = {
		.name		= "armper_ck",
		.parent		= &ck_dpll1,
		.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
				  CLOCK_IN_OMAP310 | RATE_CKCTL |
				  CLOCK_IDLE_CONTROL,
		.enable_reg	= (void __iomem *)ARM_IDLECT2,
		.enable_bit	= EN_PERCK,
		.rate_offset	= CKCTL_PERDIV_OFFSET,
		.recalc		= &omap1_ckctl_recalc,
		.enable		= &omap1_clk_enable_generic,
		.disable	= &omap1_clk_disable_generic,
	},
	.idlect_shift	= 2,
};

static struct clk arm_gpio_ck = {
	.name		= "arm_gpio_ck",
	.parent		= &ck_dpll1,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP310,
	.enable_reg	= (void __iomem *)ARM_IDLECT2,
	.enable_bit	= EN_GPIOCK,
	.recalc		= &followparent_recalc,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct arm_idlect1_clk armxor_ck = {
	.clk = {
		.name		= "armxor_ck",
		.parent		= &ck_ref,
		.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
				  CLOCK_IN_OMAP310 | CLOCK_IDLE_CONTROL,
		.enable_reg	= (void __iomem *)ARM_IDLECT2,
		.enable_bit	= EN_XORPCK,
		.recalc		= &followparent_recalc,
		.enable		= &omap1_clk_enable_generic,
		.disable	= &omap1_clk_disable_generic,
	},
	.idlect_shift	= 1,
};

static struct arm_idlect1_clk armtim_ck = {
	.clk = {
		.name		= "armtim_ck",
		.parent		= &ck_ref,
		.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
				  CLOCK_IN_OMAP310 | CLOCK_IDLE_CONTROL,
		.enable_reg	= (void __iomem *)ARM_IDLECT2,
		.enable_bit	= EN_TIMCK,
		.recalc		= &followparent_recalc,
		.enable		= &omap1_clk_enable_generic,
		.disable	= &omap1_clk_disable_generic,
	},
	.idlect_shift	= 9,
};

static struct arm_idlect1_clk armwdt_ck = {
	.clk = {
		.name		= "armwdt_ck",
		.parent		= &ck_ref,
		.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
				  CLOCK_IN_OMAP310 | CLOCK_IDLE_CONTROL,
		.enable_reg	= (void __iomem *)ARM_IDLECT2,
		.enable_bit	= EN_WDTCK,
		.recalc		= &omap1_watchdog_recalc,
		.enable		= &omap1_clk_enable_generic,
		.disable	= &omap1_clk_disable_generic,
	},
	.idlect_shift	= 0,
};

static struct clk arminth_ck16xx = {
	.name		= "arminth_ck",
	.parent		= &arm_ck,
	.flags		= CLOCK_IN_OMAP16XX | ALWAYS_ENABLED,
	.recalc		= &followparent_recalc,
	/* Note: On 16xx the frequency can be divided by 2 by programming
	 * ARM_CKCTL:ARM_INTHCK_SEL(14) to 1
	 *
	 * 1510 version is in TC clocks.
	 */
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk dsp_ck = {
	.name		= "dsp_ck",
	.parent		= &ck_dpll1,
	.flags		= CLOCK_IN_OMAP310 | CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  RATE_CKCTL,
	.enable_reg	= (void __iomem *)ARM_CKCTL,
	.enable_bit	= EN_DSPCK,
	.rate_offset	= CKCTL_DSPDIV_OFFSET,
	.recalc		= &omap1_ckctl_recalc,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk dspmmu_ck = {
	.name		= "dspmmu_ck",
	.parent		= &ck_dpll1,
	.flags		= CLOCK_IN_OMAP310 | CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  RATE_CKCTL | ALWAYS_ENABLED,
	.rate_offset	= CKCTL_DSPMMUDIV_OFFSET,
	.recalc		= &omap1_ckctl_recalc,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk dspper_ck = {
	.name		= "dspper_ck",
	.parent		= &ck_dpll1,
	.flags		= CLOCK_IN_OMAP310 | CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  RATE_CKCTL | VIRTUAL_IO_ADDRESS,
	.enable_reg	= (void __iomem *)DSP_IDLECT2,
	.enable_bit	= EN_PERCK,
	.rate_offset	= CKCTL_PERDIV_OFFSET,
	.recalc		= &omap1_ckctl_recalc_dsp_domain,
	.set_rate	= &omap1_clk_set_rate_dsp_domain,
	.enable		= &omap1_clk_enable_dsp_domain,
	.disable	= &omap1_clk_disable_dsp_domain,
};

static struct clk dspxor_ck = {
	.name		= "dspxor_ck",
	.parent		= &ck_ref,
	.flags		= CLOCK_IN_OMAP310 | CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  VIRTUAL_IO_ADDRESS,
	.enable_reg	= (void __iomem *)DSP_IDLECT2,
	.enable_bit	= EN_XORPCK,
	.recalc		= &followparent_recalc,
	.enable		= &omap1_clk_enable_dsp_domain,
	.disable	= &omap1_clk_disable_dsp_domain,
};

static struct clk dsptim_ck = {
	.name		= "dsptim_ck",
	.parent		= &ck_ref,
	.flags		= CLOCK_IN_OMAP310 | CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  VIRTUAL_IO_ADDRESS,
	.enable_reg	= (void __iomem *)DSP_IDLECT2,
	.enable_bit	= EN_DSPTIMCK,
	.recalc		= &followparent_recalc,
	.enable		= &omap1_clk_enable_dsp_domain,
	.disable	= &omap1_clk_disable_dsp_domain,
};

/* Tie ARM_IDLECT1:IDLIF_ARM to this logical clock structure */
static struct arm_idlect1_clk tc_ck = {
	.clk = {
		.name		= "tc_ck",
		.parent		= &ck_dpll1,
		.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
				  CLOCK_IN_OMAP730 | CLOCK_IN_OMAP310 |
				  RATE_CKCTL | RATE_PROPAGATES |
				  ALWAYS_ENABLED | CLOCK_IDLE_CONTROL,
		.rate_offset	= CKCTL_TCDIV_OFFSET,
		.recalc		= &omap1_ckctl_recalc,
		.enable		= &omap1_clk_enable_generic,
		.disable	= &omap1_clk_disable_generic,
	},
	.idlect_shift	= 6,
};

static struct clk arminth_ck1510 = {
	.name		= "arminth_ck",
	.parent		= &tc_ck.clk,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP310 |
			  ALWAYS_ENABLED,
	.recalc		= &followparent_recalc,
	/* Note: On 1510 the frequency follows TC_CK
	 *
	 * 16xx version is in MPU clocks.
	 */
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk tipb_ck = {
	/* No-idle controlled by "tc_ck" */
	.name		= "tipb_ck",
	.parent		= &tc_ck.clk,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP310 |
			  ALWAYS_ENABLED,
	.recalc		= &followparent_recalc,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk l3_ocpi_ck = {
	/* No-idle controlled by "tc_ck" */
	.name		= "l3_ocpi_ck",
	.parent		= &tc_ck.clk,
	.flags		= CLOCK_IN_OMAP16XX,
	.enable_reg	= (void __iomem *)ARM_IDLECT3,
	.enable_bit	= EN_OCPI_CK,
	.recalc		= &followparent_recalc,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk tc1_ck = {
	.name		= "tc1_ck",
	.parent		= &tc_ck.clk,
	.flags		= CLOCK_IN_OMAP16XX,
	.enable_reg	= (void __iomem *)ARM_IDLECT3,
	.enable_bit	= EN_TC1_CK,
	.recalc		= &followparent_recalc,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk tc2_ck = {
	.name		= "tc2_ck",
	.parent		= &tc_ck.clk,
	.flags		= CLOCK_IN_OMAP16XX,
	.enable_reg	= (void __iomem *)ARM_IDLECT3,
	.enable_bit	= EN_TC2_CK,
	.recalc		= &followparent_recalc,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk dma_ck = {
	/* No-idle controlled by "tc_ck" */
	.name		= "dma_ck",
	.parent		= &tc_ck.clk,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  CLOCK_IN_OMAP310 | ALWAYS_ENABLED,
	.recalc		= &followparent_recalc,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk dma_lcdfree_ck = {
	.name		= "dma_lcdfree_ck",
	.parent		= &tc_ck.clk,
	.flags		= CLOCK_IN_OMAP16XX | ALWAYS_ENABLED,
	.recalc		= &followparent_recalc,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct arm_idlect1_clk api_ck = {
	.clk = {
		.name		= "api_ck",
		.parent		= &tc_ck.clk,
		.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
				  CLOCK_IN_OMAP310 | CLOCK_IDLE_CONTROL,
		.enable_reg	= (void __iomem *)ARM_IDLECT2,
		.enable_bit	= EN_APICK,
		.recalc		= &followparent_recalc,
		.enable		= &omap1_clk_enable_generic,
		.disable	= &omap1_clk_disable_generic,
	},
	.idlect_shift	= 8,
};

static struct arm_idlect1_clk lb_ck = {
	.clk = {
		.name		= "lb_ck",
		.parent		= &tc_ck.clk,
		.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP310 |
				  CLOCK_IDLE_CONTROL,
		.enable_reg	= (void __iomem *)ARM_IDLECT2,
		.enable_bit	= EN_LBCK,
		.recalc		= &followparent_recalc,
		.enable		= &omap1_clk_enable_generic,
		.disable	= &omap1_clk_disable_generic,
	},
	.idlect_shift	= 4,
};

static struct clk rhea1_ck = {
	.name		= "rhea1_ck",
	.parent		= &tc_ck.clk,
	.flags		= CLOCK_IN_OMAP16XX | ALWAYS_ENABLED,
	.recalc		= &followparent_recalc,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk rhea2_ck = {
	.name		= "rhea2_ck",
	.parent		= &tc_ck.clk,
	.flags		= CLOCK_IN_OMAP16XX | ALWAYS_ENABLED,
	.recalc		= &followparent_recalc,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk lcd_ck_16xx = {
	.name		= "lcd_ck",
	.parent		= &ck_dpll1,
	.flags		= CLOCK_IN_OMAP16XX | CLOCK_IN_OMAP730 | RATE_CKCTL,
	.enable_reg	= (void __iomem *)ARM_IDLECT2,
	.enable_bit	= EN_LCDCK,
	.rate_offset	= CKCTL_LCDDIV_OFFSET,
	.recalc		= &omap1_ckctl_recalc,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct arm_idlect1_clk lcd_ck_1510 = {
	.clk = {
		.name		= "lcd_ck",
		.parent		= &ck_dpll1,
		.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP310 |
				  RATE_CKCTL | CLOCK_IDLE_CONTROL,
		.enable_reg	= (void __iomem *)ARM_IDLECT2,
		.enable_bit	= EN_LCDCK,
		.rate_offset	= CKCTL_LCDDIV_OFFSET,
		.recalc		= &omap1_ckctl_recalc,
		.enable		= &omap1_clk_enable_generic,
		.disable	= &omap1_clk_disable_generic,
	},
	.idlect_shift	= 3,
};

static struct clk uart1_1510 = {
	.name		= "uart1_ck",
	/* Direct from ULPD, no real parent */
	.parent		= &armper_ck.clk,
	.rate		= 12000000,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP310 |
			  ENABLE_REG_32BIT | ALWAYS_ENABLED |
			  CLOCK_NO_IDLE_PARENT,
	.enable_reg	= (void __iomem *)MOD_CONF_CTRL_0,
	.enable_bit	= 29,	/* Chooses between 12MHz and 48MHz */
	.set_rate	= &omap1_set_uart_rate,
	.recalc		= &omap1_uart_recalc,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct uart_clk uart1_16xx = {
	.clk	= {
		.name		= "uart1_ck",
		/* Direct from ULPD, no real parent */
		.parent		= &armper_ck.clk,
		.rate		= 48000000,
		.flags		= CLOCK_IN_OMAP16XX | RATE_FIXED |
				  ENABLE_REG_32BIT | CLOCK_NO_IDLE_PARENT,
		.enable_reg	= (void __iomem *)MOD_CONF_CTRL_0,
		.enable_bit	= 29,
		.enable		= &omap1_clk_enable_uart_functional,
		.disable	= &omap1_clk_disable_uart_functional,
	},
	.sysc_addr	= 0xfffb0054,
};

static struct clk uart2_ck = {
	.name		= "uart2_ck",
	/* Direct from ULPD, no real parent */
	.parent		= &armper_ck.clk,
	.rate		= 12000000,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  CLOCK_IN_OMAP310 | ENABLE_REG_32BIT |
			  ALWAYS_ENABLED | CLOCK_NO_IDLE_PARENT,
	.enable_reg	= (void __iomem *)MOD_CONF_CTRL_0,
	.enable_bit	= 30,	/* Chooses between 12MHz and 48MHz */
	.set_rate	= &omap1_set_uart_rate,
	.recalc		= &omap1_uart_recalc,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk uart3_1510 = {
	.name		= "uart3_ck",
	/* Direct from ULPD, no real parent */
	.parent		= &armper_ck.clk,
	.rate		= 12000000,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP310 |
			  ENABLE_REG_32BIT | ALWAYS_ENABLED |
			  CLOCK_NO_IDLE_PARENT,
	.enable_reg	= (void __iomem *)MOD_CONF_CTRL_0,
	.enable_bit	= 31,	/* Chooses between 12MHz and 48MHz */
	.set_rate	= &omap1_set_uart_rate,
	.recalc		= &omap1_uart_recalc,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct uart_clk uart3_16xx = {
	.clk	= {
		.name		= "uart3_ck",
		/* Direct from ULPD, no real parent */
		.parent		= &armper_ck.clk,
		.rate		= 48000000,
		.flags		= CLOCK_IN_OMAP16XX | RATE_FIXED |
				  ENABLE_REG_32BIT | CLOCK_NO_IDLE_PARENT,
		.enable_reg	= (void __iomem *)MOD_CONF_CTRL_0,
		.enable_bit	= 31,
		.enable		= &omap1_clk_enable_uart_functional,
		.disable	= &omap1_clk_disable_uart_functional,
	},
	.sysc_addr	= 0xfffb9854,
};

static struct clk usb_clko = {	/* 6 MHz output on W4_USB_CLKO */
	.name		= "usb_clko",
	/* Direct from ULPD, no parent */
	.rate		= 6000000,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  CLOCK_IN_OMAP310 | RATE_FIXED | ENABLE_REG_32BIT,
	.enable_reg	= (void __iomem *)ULPD_CLOCK_CTRL,
	.enable_bit	= USB_MCLK_EN_BIT,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk usb_hhc_ck1510 = {
	.name		= "usb_hhc_ck",
	/* Direct from ULPD, no parent */
	.rate		= 48000000, /* Actually 2 clocks, 12MHz and 48MHz */
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP310 |
			  RATE_FIXED | ENABLE_REG_32BIT,
	.enable_reg	= (void __iomem *)MOD_CONF_CTRL_0,
	.enable_bit	= USB_HOST_HHC_UHOST_EN,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk usb_hhc_ck16xx = {
	.name		= "usb_hhc_ck",
	/* Direct from ULPD, no parent */
	.rate		= 48000000,
	/* OTG_SYSCON_2.OTG_PADEN == 0 (not 1510-compatible) */
	.flags		= CLOCK_IN_OMAP16XX |
			  RATE_FIXED | ENABLE_REG_32BIT,
	.enable_reg	= (void __iomem *)OTG_BASE + 0x08 /* OTG_SYSCON_2 */,
	.enable_bit	= 8 /* UHOST_EN */,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk usb_dc_ck = {
	.name		= "usb_dc_ck",
	/* Direct from ULPD, no parent */
	.rate		= 48000000,
	.flags		= CLOCK_IN_OMAP16XX | RATE_FIXED,
	.enable_reg	= (void __iomem *)SOFT_REQ_REG,
	.enable_bit	= 4,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk mclk_1510 = {
	.name		= "mclk",
	/* Direct from ULPD, no parent. May be enabled by ext hardware. */
	.rate		= 12000000,
 	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP310 | RATE_FIXED,
 	.enable_reg	= (void __iomem *)SOFT_REQ_REG,
 	.enable_bit	= 6,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk mclk_16xx = {
	.name		= "mclk",
	/* Direct from ULPD, no parent. May be enabled by ext hardware. */
	.flags		= CLOCK_IN_OMAP16XX,
	.enable_reg	= (void __iomem *)COM_CLK_DIV_CTRL_SEL,
	.enable_bit	= COM_ULPD_PLL_CLK_REQ,
	.set_rate	= &omap1_set_ext_clk_rate,
	.round_rate	= &omap1_round_ext_clk_rate,
	.init		= &omap1_init_ext_clk,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk bclk_1510 = {
	.name		= "bclk",
	/* Direct from ULPD, no parent. May be enabled by ext hardware. */
	.rate		= 12000000,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP310 | RATE_FIXED,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk bclk_16xx = {
	.name		= "bclk",
	/* Direct from ULPD, no parent. May be enabled by ext hardware. */
	.flags		= CLOCK_IN_OMAP16XX,
	.enable_reg	= (void __iomem *)SWD_CLK_DIV_CTRL_SEL,
	.enable_bit	= SWD_ULPD_PLL_CLK_REQ,
	.set_rate	= &omap1_set_ext_clk_rate,
	.round_rate	= &omap1_round_ext_clk_rate,
	.init		= &omap1_init_ext_clk,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk mmc1_ck = {
	.name		= "mmc_ck",
	.id		= 1,
	/* Functional clock is direct from ULPD, interface clock is ARMPER */
	.parent		= &armper_ck.clk,
	.rate		= 48000000,
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  CLOCK_IN_OMAP310 | RATE_FIXED | ENABLE_REG_32BIT |
			  CLOCK_NO_IDLE_PARENT,
	.enable_reg	= (void __iomem *)MOD_CONF_CTRL_0,
	.enable_bit	= 23,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk mmc2_ck = {
	.name		= "mmc_ck",
	.id		= 2,
	/* Functional clock is direct from ULPD, interface clock is ARMPER */
	.parent		= &armper_ck.clk,
	.rate		= 48000000,
	.flags		= CLOCK_IN_OMAP16XX |
			  RATE_FIXED | ENABLE_REG_32BIT | CLOCK_NO_IDLE_PARENT,
	.enable_reg	= (void __iomem *)MOD_CONF_CTRL_0,
	.enable_bit	= 20,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk virtual_ck_mpu = {
	.name		= "mpu",
	.flags		= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  CLOCK_IN_OMAP310 | VIRTUAL_CLOCK | ALWAYS_ENABLED,
	.parent		= &arm_ck, /* Is smarter alias for */
	.recalc		= &followparent_recalc,
	.set_rate	= &omap1_select_table_rate,
	.round_rate	= &omap1_round_to_table_rate,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

/* virtual functional clock domain for I2C. Just for making sure that ARMXOR_CK
remains active during MPU idle whenever this is enabled */
static struct clk i2c_fck = {
	.name		= "i2c_fck",
	.id		= 1,
	.flags		= CLOCK_IN_OMAP310 | CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
			  VIRTUAL_CLOCK | CLOCK_NO_IDLE_PARENT |
			  ALWAYS_ENABLED,
	.parent		= &armxor_ck.clk,
	.recalc		= &followparent_recalc,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk i2c_ick = {
	.name		= "i2c_ick",
	.id		= 1,
	.flags		= CLOCK_IN_OMAP16XX |
			  VIRTUAL_CLOCK | CLOCK_NO_IDLE_PARENT |
			  ALWAYS_ENABLED,
	.parent		= &armper_ck.clk,
	.recalc		= &followparent_recalc,
	.enable		= &omap1_clk_enable_generic,
	.disable	= &omap1_clk_disable_generic,
};

static struct clk * onchip_clks[] = {
	/* non-ULPD clocks */
	&ck_ref,
	&ck_dpll1,
	/* CK_GEN1 clocks */
	&ck_dpll1out.clk,
	&sossi_ck,
	&arm_ck,
	&armper_ck.clk,
	&arm_gpio_ck,
	&armxor_ck.clk,
	&armtim_ck.clk,
	&armwdt_ck.clk,
	&arminth_ck1510,  &arminth_ck16xx,
	/* CK_GEN2 clocks */
	&dsp_ck,
	&dspmmu_ck,
	&dspper_ck,
	&dspxor_ck,
	&dsptim_ck,
	/* CK_GEN3 clocks */
	&tc_ck.clk,
	&tipb_ck,
	&l3_ocpi_ck,
	&tc1_ck,
	&tc2_ck,
	&dma_ck,
	&dma_lcdfree_ck,
	&api_ck.clk,
	&lb_ck.clk,
	&rhea1_ck,
	&rhea2_ck,
	&lcd_ck_16xx,
	&lcd_ck_1510.clk,
	/* ULPD clocks */
	&uart1_1510,
	&uart1_16xx.clk,
	&uart2_ck,
	&uart3_1510,
	&uart3_16xx.clk,
	&usb_clko,
	&usb_hhc_ck1510, &usb_hhc_ck16xx,
	&usb_dc_ck,
	&mclk_1510,  &mclk_16xx,
	&bclk_1510,  &bclk_16xx,
	&mmc1_ck,
	&mmc2_ck,
	/* Virtual clocks */
	&virtual_ck_mpu,
	&i2c_fck,
	&i2c_ick,
};
extern __u32 arm_idlect1_mask;
extern struct clk *api_ck_p, *ck_dpll1_p, *ck_ref_p;

extern const struct clkops clkops_dspck;
extern const struct clkops clkops_dummy;
extern const struct clkops clkops_uart_16xx;
extern const struct clkops clkops_generic;

/* used for passing SoC type to omap1_{select,round_to}_table_rate() */
extern u32 cpu_mask;

#endif
