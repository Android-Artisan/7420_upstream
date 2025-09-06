/*
 * arch/sh/kernel/cpu/sh4/clock-shx3.c
 *
 * SH-X3 support for the clock framework
 *
 *  Copyright (C) 2006-2007  Renesas Technology Corp.
 *  Copyright (C) 2006-2007  Renesas Solutions Corp.
 *  Copyright (C) 2006-2007  Paul Mundt
 *  Copyright (C) 2006-2010  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <asm/clock.h>
#include <asm/freq.h>
#include <asm/io.h>

static int ifc_divisors[] = { 1, 2, 4 ,6 };
static int bfc_divisors[] = { 1, 1, 1, 1, 1, 12, 16, 18, 24, 32, 36, 48 };
static int pfc_divisors[] = { 1, 1, 1, 1, 1, 1, 1, 18, 24, 32, 36, 48 };
static int cfc_divisors[] = { 1, 1, 4, 6 };

#define IFC_POS		28
#define IFC_MSK		0x0003
#define BFC_MSK		0x000f
#define PFC_MSK		0x000f
#define CFC_MSK		0x0003
#define BFC_POS		16
#define PFC_POS		0
#define CFC_POS		20

static void master_clk_init(struct clk *clk)
{
	clk->rate *= pfc_divisors[(ctrl_inl(FRQCR) >> PFC_POS) & PFC_MSK];
}

static struct clk_ops shx3_master_clk_ops = {
	.init		= master_clk_init,
};

static void module_clk_recalc(struct clk *clk)
{
	int idx = ((ctrl_inl(FRQCR) >> PFC_POS) & PFC_MSK);
	clk->rate = clk->parent->rate / pfc_divisors[idx];
}

static struct clk_ops shx3_module_clk_ops = {
	.recalc		= module_clk_recalc,
};

static void bus_clk_recalc(struct clk *clk)
{
	int idx = ((ctrl_inl(FRQCR) >> BFC_POS) & BFC_MSK);
	clk->rate = clk->parent->rate / bfc_divisors[idx];
}

static struct clk_ops shx3_bus_clk_ops = {
	.recalc		= bus_clk_recalc,
};

static void cpu_clk_recalc(struct clk *clk)
{
	int idx = ((ctrl_inl(FRQCR) >> IFC_POS) & IFC_MSK);
	clk->rate = clk->parent->rate / ifc_divisors[idx];
}

static struct clk_ops shx3_cpu_clk_ops = {
	.recalc		= cpu_clk_recalc,
};

static struct clk_ops *shx3_clk_ops[] = {
	&shx3_master_clk_ops,
	&shx3_module_clk_ops,
	&shx3_bus_clk_ops,
	&shx3_cpu_clk_ops,
};

void __init arch_init_clk_ops(struct clk_ops **ops, int idx)
{
	if (idx < ARRAY_SIZE(shx3_clk_ops))
		*ops = shx3_clk_ops[idx];
}

static void shyway_clk_recalc(struct clk *clk)
{
	int idx = ((ctrl_inl(FRQCR) >> CFC_POS) & CFC_MSK);
	clk->rate = clk->parent->rate / cfc_divisors[idx];
}

static struct clk_ops shx3_shyway_clk_ops = {
	.recalc		= shyway_clk_recalc,
};

static struct clk shx3_shyway_clk = {
	.name		= "shyway_clk",
	.flags		= CLK_ALWAYS_ENABLED,
	.ops		= &shx3_shyway_clk_ops,
};

/*
 * Additional SHx3-specific on-chip clocks that aren't already part of the
 * clock framework
 */
static struct clk *shx3_onchip_clocks[] = {
	&shx3_shyway_clk,
};

static int __init shx3_clk_init(void)
{
	struct clk *clk = clk_get(NULL, "master_clk");
	int i;

	for (i = 0; i < ARRAY_SIZE(shx3_onchip_clocks); i++) {
		struct clk *clkp = shx3_onchip_clocks[i];

		clkp->parent = clk;
		clk_register(clkp);
		clk_enable(clkp);
	}

	/*
	 * Now that we have the rest of the clocks registered, we need to
	 * force the parent clock to propagate so that these clocks will
	 * automatically figure out their rate. We cheat by handing the
	 * parent clock its current rate and forcing child propagation.
	 */
	clk_set_rate(clk, clk_get_rate(clk));

	clk_put(clk);

	return 0;
}
arch_initcall(shx3_clk_init);
#include <linux/io.h>
#include <linux/clkdev.h>
#include <asm/clock.h>
#include <asm/freq.h>

/*
 * Default rate for the root input clock, reset this with clk_set_rate()
 * from the platform code.
 */
static struct clk extal_clk = {
	.rate		= 16666666,
};

static unsigned long pll_recalc(struct clk *clk)
{
	/* PLL1 has a fixed x72 multiplier.  */
	return clk->parent->rate * 72;
}

static struct sh_clk_ops pll_clk_ops = {
	.recalc		= pll_recalc,
};

static struct clk pll_clk = {
	.ops		= &pll_clk_ops,
	.parent		= &extal_clk,
	.flags		= CLK_ENABLE_ON_INIT,
};

static struct clk *clks[] = {
	&extal_clk,
	&pll_clk,
};

static unsigned int div2[] = { 1, 2, 4, 6, 8, 12, 16, 18,
			       24, 32, 36, 48 };

static struct clk_div_mult_table div4_div_mult_table = {
	.divisors = div2,
	.nr_divisors = ARRAY_SIZE(div2),
};

static struct clk_div4_table div4_table = {
	.div_mult_table = &div4_div_mult_table,
};

enum { DIV4_I, DIV4_SH, DIV4_B, DIV4_DDR, DIV4_SHA, DIV4_P, DIV4_NR };

#define DIV4(_bit, _mask, _flags) \
  SH_CLK_DIV4(&pll_clk, FRQMR1, _bit, _mask, _flags)

struct clk div4_clks[DIV4_NR] = {
	[DIV4_P] = DIV4(0, 0x0f80, 0),
	[DIV4_SHA] = DIV4(4, 0x0ff0, 0),
	[DIV4_DDR] = DIV4(12, 0x000c, CLK_ENABLE_ON_INIT),
	[DIV4_B] = DIV4(16, 0x0fe0, CLK_ENABLE_ON_INIT),
	[DIV4_SH] = DIV4(20, 0x000c, CLK_ENABLE_ON_INIT),
	[DIV4_I] = DIV4(28, 0x000e, CLK_ENABLE_ON_INIT),
};

#define MSTPCR0		0xffc00030
#define MSTPCR1		0xffc00034

enum { MSTP027, MSTP026, MSTP025, MSTP024,
       MSTP009, MSTP008, MSTP003, MSTP002,
       MSTP001, MSTP000, MSTP119, MSTP105,
       MSTP104, MSTP_NR };

static struct clk mstp_clks[MSTP_NR] = {
	/* MSTPCR0 */
	[MSTP027] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 27, 0),
	[MSTP026] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 26, 0),
	[MSTP025] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 25, 0),
	[MSTP024] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 24, 0),
	[MSTP009] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 9, 0),
	[MSTP008] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 8, 0),
	[MSTP003] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 3, 0),
	[MSTP002] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 2, 0),
	[MSTP001] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 1, 0),
	[MSTP000] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 0, 0),

	/* MSTPCR1 */
	[MSTP119] = SH_CLK_MSTP32(NULL, MSTPCR1, 19, 0),
	[MSTP105] = SH_CLK_MSTP32(NULL, MSTPCR1, 5, 0),
	[MSTP104] = SH_CLK_MSTP32(NULL, MSTPCR1, 4, 0),
};

static struct clk_lookup lookups[] = {
	/* main clocks */
	CLKDEV_CON_ID("extal", &extal_clk),
	CLKDEV_CON_ID("pll_clk", &pll_clk),

	/* DIV4 clocks */
	CLKDEV_CON_ID("peripheral_clk", &div4_clks[DIV4_P]),
	CLKDEV_CON_ID("shywaya_clk", &div4_clks[DIV4_SHA]),
	CLKDEV_CON_ID("ddr_clk", &div4_clks[DIV4_DDR]),
	CLKDEV_CON_ID("bus_clk", &div4_clks[DIV4_B]),
	CLKDEV_CON_ID("shyway_clk", &div4_clks[DIV4_SH]),
	CLKDEV_CON_ID("cpu_clk", &div4_clks[DIV4_I]),

	/* MSTP32 clocks */
	CLKDEV_ICK_ID("sci_fck", "sh-sci.3", &mstp_clks[MSTP027]),
	CLKDEV_ICK_ID("sci_fck", "sh-sci.2", &mstp_clks[MSTP026]),
	CLKDEV_ICK_ID("sci_fck", "sh-sci.1", &mstp_clks[MSTP025]),
	CLKDEV_ICK_ID("sci_fck", "sh-sci.0", &mstp_clks[MSTP024]),

	CLKDEV_CON_ID("h8ex_fck", &mstp_clks[MSTP003]),
	CLKDEV_CON_ID("csm_fck", &mstp_clks[MSTP002]),
	CLKDEV_CON_ID("fe1_fck", &mstp_clks[MSTP001]),
	CLKDEV_CON_ID("fe0_fck", &mstp_clks[MSTP000]),

	CLKDEV_ICK_ID("fck", "sh-tmu.0", &mstp_clks[MSTP008]),
	CLKDEV_ICK_ID("fck", "sh-tmu.1", &mstp_clks[MSTP009]),

	CLKDEV_CON_ID("hudi_fck", &mstp_clks[MSTP119]),
	CLKDEV_CON_ID("dmac_11_6_fck", &mstp_clks[MSTP105]),
	CLKDEV_CON_ID("dmac_5_0_fck", &mstp_clks[MSTP104]),
};

int __init arch_clk_init(void)
{
	int i, ret = 0;

	for (i = 0; i < ARRAY_SIZE(clks); i++)
		ret |= clk_register(clks[i]);

	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	if (!ret)
		ret = sh_clk_div4_register(div4_clks, ARRAY_SIZE(div4_clks),
					   &div4_table);
	if (!ret)
		ret = sh_clk_mstp_register(mstp_clks, MSTP_NR);

	return ret;
}
