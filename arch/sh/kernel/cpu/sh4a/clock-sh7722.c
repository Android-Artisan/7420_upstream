/*
 * arch/sh/kernel/cpu/sh4a/clock-sh7722.c
 *
 * SH7343, SH7722, SH7723 & SH7366 support for the clock framework
 *
 * Copyright (c) 2006-2007 Nomad Global Solutions Inc
 * Based on code for sh7343 by Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 * SH7722 clock framework support
 *
 * Copyright (C) 2009 Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/errno.h>
#include <linux/stringify.h>
#include <asm/clock.h>
#include <asm/freq.h>

#define N  (-1)
#define NM (-2)
#define ROUND_NEAREST 0
#define ROUND_DOWN -1
#define ROUND_UP   +1

static int adjust_algos[][3] = {
	{},	/* NO_CHANGE */
	{ NM, N, 1 },   /* N:1, N:1 */
	{ 3, 2, 2 },	/* 3:2:2 */
	{ 5, 2, 2 },    /* 5:2:2 */
	{ N, 1, 1 },	/* N:1:1 */

	{ N, 1 },	/* N:1 */

	{ N, 1 },	/* N:1 */
	{ 3, 2 },
	{ 4, 3 },
	{ 5, 4 },

	{ N, 1 }
};

static unsigned long adjust_pair_of_clocks(unsigned long r1, unsigned long r2,
			int m1, int m2, int round_flag)
{
	unsigned long rem, div;
	int the_one = 0;

	pr_debug( "Actual values: r1 = %ld\n", r1);
	pr_debug( "...............r2 = %ld\n", r2);

	if (m1 == m2) {
		r2 = r1;
		pr_debug( "setting equal rates: r2 now %ld\n", r2);
	} else if ((m2 == N  && m1 == 1) ||
		   (m2 == NM && m1 == N)) { /* N:1 or NM:N */
		pr_debug( "Setting rates as 1:N (N:N*M)\n");
		rem = r2 % r1;
		pr_debug( "...remainder = %ld\n", rem);
		if (rem) {
			div = r2 / r1;
			pr_debug( "...div = %ld\n", div);
			switch (round_flag) {
			case ROUND_NEAREST:
				the_one = rem >= r1/2 ? 1 : 0; break;
			case ROUND_UP:
				the_one = 1; break;
			case ROUND_DOWN:
				the_one = 0; break;
			}

			r2 = r1 * (div + the_one);
			pr_debug( "...setting r2 to %ld\n", r2);
		}
	} else if ((m2 == 1  && m1 == N) ||
		   (m2 == N && m1 == NM)) { /* 1:N or N:NM */
		pr_debug( "Setting rates as N:1 (N*M:N)\n");
		rem = r1 % r2;
		pr_debug( "...remainder = %ld\n", rem);
		if (rem) {
			div = r1 / r2;
			pr_debug( "...div = %ld\n", div);
			switch (round_flag) {
			case ROUND_NEAREST:
				the_one = rem > r2/2 ? 1 : 0; break;
			case ROUND_UP:
				the_one = 0; break;
			case ROUND_DOWN:
				the_one = 1; break;
			}

			r2 = r1 / (div + the_one);
			pr_debug( "...setting r2 to %ld\n", r2);
		}
	} else { /* value:value */
		pr_debug( "Setting rates as %d:%d\n", m1, m2);
		div = r1 / m1;
		r2 = div * m2;
		pr_debug( "...div = %ld\n", div);
		pr_debug( "...setting r2 to %ld\n", r2);
	}

	return r2;
}

static void adjust_clocks(int originate, int *l, unsigned long v[],
			  int n_in_line)
{
	int x;

	pr_debug( "Go down from %d...\n", originate);
	/* go up recalculation clocks */
	for (x = originate; x>0; x -- )
		v[x-1] = adjust_pair_of_clocks(v[x], v[x-1],
					l[x], l[x-1],
					ROUND_UP);

	pr_debug( "Go up from %d...\n", originate);
	/* go down recalculation clocks */
	for (x = originate; x<n_in_line - 1; x ++ )
		v[x+1] = adjust_pair_of_clocks(v[x], v[x+1],
					l[x], l[x+1],
					ROUND_UP);
}


/*
 * SH7722 uses a common set of multipliers and divisors, so this
 * is quite simple..
 */

/*
 * Instead of having two separate multipliers/divisors set, like this:
 *
 * static int multipliers[] = { 1, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
 * static int divisors[] = { 1, 3, 2, 5, 3, 4, 5, 6, 8, 10, 12, 16, 20 };
 *
 * I created the divisors2 array, which is used to calculate rate like
 *   rate = parent * 2 / divisors2[ divisor ];
*/
static int divisors2[] = { 2, 3, 4, 5, 6, 8, 10, 12, 16, 20, 24, 32, 40 };

static void master_clk_recalc(struct clk *clk)
{
	unsigned frqcr = ctrl_inl(FRQCR);

	clk->rate = CONFIG_SH_PCLK_FREQ * (((frqcr >> 24) & 0x1f) + 1);
}

static void master_clk_init(struct clk *clk)
{
	clk->parent = NULL;
	clk->flags |= CLK_RATE_PROPAGATES;
	clk->rate = CONFIG_SH_PCLK_FREQ;
	master_clk_recalc(clk);
}


static void module_clk_recalc(struct clk *clk)
{
	unsigned long frqcr = ctrl_inl(FRQCR);

	clk->rate = clk->parent->rate / (((frqcr >> 24) & 0x1f) + 1);
}

static int master_clk_setrate(struct clk *clk, unsigned long rate, int id)
{
	int div = rate / clk->rate;
	int master_divs[] = { 2, 3, 4, 6, 8, 16 };
	int index;
	unsigned long frqcr;

	for (index = 1; index < ARRAY_SIZE(master_divs); index++)
		if (div >= master_divs[index - 1] && div < master_divs[index])
			break;

	if (index >= ARRAY_SIZE(master_divs))
		index = ARRAY_SIZE(master_divs);
	div = master_divs[index - 1];

	frqcr = ctrl_inl(FRQCR);
	frqcr &= ~(0xF << 24);
	frqcr |= ( (div-1) << 24);
	ctrl_outl(frqcr, FRQCR);

	return 0;
}

static struct clk_ops sh7722_master_clk_ops = {
	.init = master_clk_init,
	.recalc = master_clk_recalc,
	.set_rate = master_clk_setrate,
};

static struct clk_ops sh7722_module_clk_ops = {
       .recalc = module_clk_recalc,
};

struct frqcr_context {
	unsigned mask;
	unsigned shift;
};

struct frqcr_context sh7722_get_clk_context(const char *name)
{
	struct frqcr_context ctx = { 0, };

	if (!strcmp(name, "peripheral_clk")) {
		ctx.shift = 0;
		ctx.mask = 0xF;
	} else if (!strcmp(name, "sdram_clk")) {
		ctx.shift = 4;
		ctx.mask = 0xF;
	} else if (!strcmp(name, "bus_clk")) {
		ctx.shift = 8;
		ctx.mask = 0xF;
	} else if (!strcmp(name, "sh_clk")) {
		ctx.shift = 12;
		ctx.mask = 0xF;
	} else if (!strcmp(name, "umem_clk")) {
		ctx.shift = 16;
		ctx.mask = 0xF;
	} else if (!strcmp(name, "cpu_clk")) {
		ctx.shift = 20;
		ctx.mask = 7;
	}
	return ctx;
}

/**
 * sh7722_find_divisors - find divisor for setting rate
 *
 * All sh7722 clocks use the same set of multipliers/divisors. This function
 * chooses correct divisor to set the rate of clock with parent clock that
 * generates frequency of 'parent_rate'
 *
 * @parent_rate: rate of parent clock
 * @rate: requested rate to be set
 */
static int sh7722_find_divisors(unsigned long parent_rate, unsigned rate)
{
	unsigned div2 = parent_rate * 2 / rate;
	int index;

	if (rate > parent_rate)
		return -EINVAL;

	for (index = 1; index < ARRAY_SIZE(divisors2); index++) {
		if (div2 > divisors2[index] && div2 <= divisors2[index])
			break;
	}
	if (index >= ARRAY_SIZE(divisors2))
		index = ARRAY_SIZE(divisors2) - 1;
	return divisors2[index];
}

static void sh7722_frqcr_recalc(struct clk *clk)
{
	struct frqcr_context ctx = sh7722_get_clk_context(clk->name);
	unsigned long frqcr = ctrl_inl(FRQCR);
	int index;

	index = (frqcr >> ctx.shift) & ctx.mask;
	clk->rate = clk->parent->rate * 2 / divisors2[index];
}

static int sh7722_frqcr_set_rate(struct clk *clk, unsigned long rate,
				 int algo_id)
{
	struct frqcr_context ctx = sh7722_get_clk_context(clk->name);
	unsigned long parent_rate = clk->parent->rate;
	int div;
	unsigned long frqcr;
	int err = 0;

	/* pretty invalid */
	if (parent_rate < rate)
		return -EINVAL;

	/* look for multiplier/divisor pair */
	div = sh7722_find_divisors(parent_rate, rate);
	if (div<0)
		return div;

	/* calculate new value of clock rate */
	clk->rate = parent_rate * 2 / div;
	frqcr = ctrl_inl(FRQCR);

	/* FIXME: adjust as algo_id specifies */
	if (algo_id != NO_CHANGE) {
		int originator;
		char *algo_group_1[] = { "cpu_clk", "umem_clk", "sh_clk" };
		char *algo_group_2[] = { "sh_clk", "bus_clk" };
		char *algo_group_3[] = { "sh_clk", "sdram_clk" };
		char *algo_group_4[] = { "bus_clk", "peripheral_clk" };
		char *algo_group_5[] = { "cpu_clk", "peripheral_clk" };
		char **algo_current = NULL;
		/* 3 is the maximum number of clocks in relation */
		struct clk *ck[3];
		unsigned long values[3]; /* the same comment as above */
		int part_length = -1;
		int i;

		/*
		 * all the steps below only required if adjustion was
		 * requested
		 */
		if (algo_id == IUS_N1_N1 ||
		    algo_id == IUS_322 ||
		    algo_id == IUS_522 ||
		    algo_id == IUS_N11) {
			algo_current = algo_group_1;
			part_length = 3;
		}
		if (algo_id == SB_N1) {
			algo_current = algo_group_2;
			part_length = 2;
		}
		if (algo_id == SB3_N1 ||
		    algo_id == SB3_32 ||
		    algo_id == SB3_43 ||
		    algo_id == SB3_54) {
			algo_current = algo_group_3;
			part_length = 2;
		}
		if (algo_id == BP_N1) {
			algo_current = algo_group_4;
			part_length = 2;
		}
		if (algo_id == IP_N1) {
			algo_current = algo_group_5;
			part_length = 2;
		}
		if (!algo_current)
			goto incorrect_algo_id;

		originator = -1;
		for (i = 0; i < part_length; i ++ ) {
			if (originator >= 0 && !strcmp(clk->name,
						       algo_current[i]))
				originator = i;
			ck[i] = clk_get(NULL, algo_current[i]);
			values[i] = clk_get_rate(ck[i]);
		}

		if (originator >= 0)
			adjust_clocks(originator, adjust_algos[algo_id],
				      values, part_length);

		for (i = 0; i < part_length; i ++ ) {
			struct frqcr_context part_ctx;
			int part_div;

			if (likely(!err)) {
				part_div = sh7722_find_divisors(parent_rate,
								rate);
				if (part_div > 0) {
					part_ctx = sh7722_get_clk_context(
								ck[i]->name);
					frqcr &= ~(part_ctx.mask <<
						   part_ctx.shift);
					frqcr |= part_div << part_ctx.shift;
				} else
					err = part_div;
			}

			ck[i]->ops->recalc(ck[i]);
			clk_put(ck[i]);
		}
	}

	/* was there any error during recalculation ? If so, bail out.. */
	if (unlikely(err!=0))
		goto out_err;

	/* clear FRQCR bits */
	frqcr &= ~(ctx.mask << ctx.shift);
	frqcr |= div << ctx.shift;

	/* ...and perform actual change */
	ctrl_outl(frqcr, FRQCR);
	return 0;

incorrect_algo_id:
	return -EINVAL;
out_err:
	return err;
}

static long sh7722_frqcr_round_rate(struct clk *clk, unsigned long rate)
{
	unsigned long parent_rate = clk->parent->rate;
	int div;

	/* look for multiplier/divisor pair */
	div = sh7722_find_divisors(parent_rate, rate);
	if (div < 0)
		return clk->rate;

	/* calculate new value of clock rate */
	return parent_rate * 2 / div;
}

static struct clk_ops sh7722_frqcr_clk_ops = {
	.recalc = sh7722_frqcr_recalc,
	.set_rate = sh7722_frqcr_set_rate,
	.round_rate = sh7722_frqcr_round_rate,
};

/*
 * clock ops methods for SIU A/B and IrDA clock
 *
 */

#ifndef CONFIG_CPU_SUBTYPE_SH7343

static int sh7722_siu_set_rate(struct clk *clk, unsigned long rate, int algo_id)
{
	unsigned long r;
	int div;

	r = ctrl_inl(clk->arch_flags);
	div = sh7722_find_divisors(clk->parent->rate, rate);
	if (div < 0)
		return div;
	r = (r & ~0xF) | div;
	ctrl_outl(r, clk->arch_flags);
	return 0;
}

static void sh7722_siu_recalc(struct clk *clk)
{
	unsigned long r;

	r = ctrl_inl(clk->arch_flags);
	clk->rate = clk->parent->rate * 2 / divisors2[r & 0xF];
}

static int sh7722_siu_start_stop(struct clk *clk, int enable)
{
	unsigned long r;

	r = ctrl_inl(clk->arch_flags);
	if (enable)
		ctrl_outl(r & ~(1 << 8), clk->arch_flags);
	else
		ctrl_outl(r | (1 << 8), clk->arch_flags);
	return 0;
}

static void sh7722_siu_enable(struct clk *clk)
{
	sh7722_siu_start_stop(clk, 1);
}

static void sh7722_siu_disable(struct clk *clk)
{
	sh7722_siu_start_stop(clk, 0);
}

static struct clk_ops sh7722_siu_clk_ops = {
	.recalc = sh7722_siu_recalc,
	.set_rate = sh7722_siu_set_rate,
	.enable = sh7722_siu_enable,
	.disable = sh7722_siu_disable,
};

#endif /* CONFIG_CPU_SUBTYPE_SH7343 */

static void sh7722_video_enable(struct clk *clk)
{
	unsigned long r;

	r = ctrl_inl(VCLKCR);
	ctrl_outl( r & ~(1<<8), VCLKCR);
}

static void sh7722_video_disable(struct clk *clk)
{
	unsigned long r;

	r = ctrl_inl(VCLKCR);
	ctrl_outl( r | (1<<8), VCLKCR);
}

static int sh7722_video_set_rate(struct clk *clk, unsigned long rate,
				 int algo_id)
{
	unsigned long r;

	r = ctrl_inl(VCLKCR);
	r &= ~0x3F;
	r |= ((clk->parent->rate / rate - 1) & 0x3F);
	ctrl_outl(r, VCLKCR);
	return 0;
}

static void sh7722_video_recalc(struct clk *clk)
{
	unsigned long r;

	r = ctrl_inl(VCLKCR);
	clk->rate = clk->parent->rate / ((r & 0x3F) + 1);
}

static struct clk_ops sh7722_video_clk_ops = {
	.recalc = sh7722_video_recalc,
	.set_rate = sh7722_video_set_rate,
	.enable = sh7722_video_enable,
	.disable = sh7722_video_disable,
};
/*
 * and at last, clock definitions themselves
 */
static struct clk sh7722_umem_clock = {
	.name = "umem_clk",
	.ops = &sh7722_frqcr_clk_ops,
};

static struct clk sh7722_sh_clock = {
	.name = "sh_clk",
	.ops = &sh7722_frqcr_clk_ops,
};

static struct clk sh7722_peripheral_clock = {
	.name = "peripheral_clk",
	.ops = &sh7722_frqcr_clk_ops,
};

static struct clk sh7722_sdram_clock = {
	.name = "sdram_clk",
	.ops = &sh7722_frqcr_clk_ops,
};


#ifndef CONFIG_CPU_SUBTYPE_SH7343

/*
 * these three clocks - SIU A, SIU B, IrDA - share the same clk_ops
 * methods of clk_ops determine which register they should access by
 * examining clk->name field
 */
static struct clk sh7722_siu_a_clock = {
	.name = "siu_a_clk",
	.arch_flags = SCLKACR,
	.ops = &sh7722_siu_clk_ops,
};

static struct clk sh7722_siu_b_clock = {
	.name = "siu_b_clk",
	.arch_flags = SCLKBCR,
	.ops = &sh7722_siu_clk_ops,
};

#if defined(CONFIG_CPU_SUBTYPE_SH7722)
static struct clk sh7722_irda_clock = {
	.name = "irda_clk",
	.arch_flags = IrDACLKCR,
	.ops = &sh7722_siu_clk_ops,
};
#endif
#endif /* CONFIG_CPU_SUBTYPE_SH7343 */

static struct clk sh7722_video_clock = {
	.name = "video_clk",
	.ops = &sh7722_video_clk_ops,
};

static int sh7722_mstpcr_start_stop(struct clk *clk, unsigned long reg,
				    int enable)
{
	unsigned long bit = clk->arch_flags;
	unsigned long r;

	r = ctrl_inl(reg);

	if (enable)
		r &= ~(1 << bit);
	else
		r |= (1 << bit);

	ctrl_outl(r, reg);
	return 0;
}

static void sh7722_mstpcr0_enable(struct clk *clk)
{
	sh7722_mstpcr_start_stop(clk, MSTPCR0, 1);
}

static void sh7722_mstpcr0_disable(struct clk *clk)
{
	sh7722_mstpcr_start_stop(clk, MSTPCR0, 0);
}

static void sh7722_mstpcr1_enable(struct clk *clk)
{
	sh7722_mstpcr_start_stop(clk, MSTPCR1, 1);
}

static void sh7722_mstpcr1_disable(struct clk *clk)
{
	sh7722_mstpcr_start_stop(clk, MSTPCR1, 0);
}

static void sh7722_mstpcr2_enable(struct clk *clk)
{
	sh7722_mstpcr_start_stop(clk, MSTPCR2, 1);
}

static void sh7722_mstpcr2_disable(struct clk *clk)
{
	sh7722_mstpcr_start_stop(clk, MSTPCR2, 0);
}

static struct clk_ops sh7722_mstpcr0_clk_ops = {
	.enable = sh7722_mstpcr0_enable,
	.disable = sh7722_mstpcr0_disable,
};

static struct clk_ops sh7722_mstpcr1_clk_ops = {
	.enable = sh7722_mstpcr1_enable,
	.disable = sh7722_mstpcr1_disable,
};

static struct clk_ops sh7722_mstpcr2_clk_ops = {
	.enable = sh7722_mstpcr2_enable,
	.disable = sh7722_mstpcr2_disable,
};

#define DECLARE_MSTPCRN(regnr, bitnr, bitstr)		\
{							\
	.name = "mstp" __stringify(regnr) bitstr,	\
	.arch_flags = bitnr,				\
	.ops = &sh7722_mstpcr ## regnr ## _clk_ops,	\
}

#define DECLARE_MSTPCR(regnr) \
	DECLARE_MSTPCRN(regnr, 31, "31"), \
	DECLARE_MSTPCRN(regnr, 30, "30"), \
	DECLARE_MSTPCRN(regnr, 29, "29"), \
	DECLARE_MSTPCRN(regnr, 28, "28"), \
	DECLARE_MSTPCRN(regnr, 27, "27"), \
	DECLARE_MSTPCRN(regnr, 26, "26"), \
	DECLARE_MSTPCRN(regnr, 25, "25"), \
	DECLARE_MSTPCRN(regnr, 24, "24"), \
	DECLARE_MSTPCRN(regnr, 23, "23"), \
	DECLARE_MSTPCRN(regnr, 22, "22"), \
	DECLARE_MSTPCRN(regnr, 21, "21"), \
	DECLARE_MSTPCRN(regnr, 20, "20"), \
	DECLARE_MSTPCRN(regnr, 19, "19"), \
	DECLARE_MSTPCRN(regnr, 18, "18"), \
	DECLARE_MSTPCRN(regnr, 17, "17"), \
	DECLARE_MSTPCRN(regnr, 16, "16"), \
	DECLARE_MSTPCRN(regnr, 15, "15"), \
	DECLARE_MSTPCRN(regnr, 14, "14"), \
	DECLARE_MSTPCRN(regnr, 13, "13"), \
	DECLARE_MSTPCRN(regnr, 12, "12"), \
	DECLARE_MSTPCRN(regnr, 11, "11"), \
	DECLARE_MSTPCRN(regnr, 10, "10"), \
	DECLARE_MSTPCRN(regnr, 9, "09"), \
	DECLARE_MSTPCRN(regnr, 8, "08"), \
	DECLARE_MSTPCRN(regnr, 7, "07"), \
	DECLARE_MSTPCRN(regnr, 6, "06"), \
	DECLARE_MSTPCRN(regnr, 5, "05"), \
	DECLARE_MSTPCRN(regnr, 4, "04"), \
	DECLARE_MSTPCRN(regnr, 3, "03"), \
	DECLARE_MSTPCRN(regnr, 2, "02"), \
	DECLARE_MSTPCRN(regnr, 1, "01"), \
	DECLARE_MSTPCRN(regnr, 0, "00")

static struct clk sh7722_mstpcr[] = {
	DECLARE_MSTPCR(0),
	DECLARE_MSTPCR(1),
	DECLARE_MSTPCR(2),
};

static struct clk *sh7722_clocks[] = {
	&sh7722_umem_clock,
	&sh7722_sh_clock,
	&sh7722_peripheral_clock,
	&sh7722_sdram_clock,
#ifndef CONFIG_CPU_SUBTYPE_SH7343
	&sh7722_siu_a_clock,
	&sh7722_siu_b_clock,
#if defined(CONFIG_CPU_SUBTYPE_SH7722)
	&sh7722_irda_clock,
#endif
#endif
	&sh7722_video_clock,
};

/*
 * init in order: master, module, bus, cpu
 */
struct clk_ops *onchip_ops[] = {
	&sh7722_master_clk_ops,
	&sh7722_module_clk_ops,
	&sh7722_frqcr_clk_ops,
	&sh7722_frqcr_clk_ops,
};

void __init
arch_init_clk_ops(struct clk_ops **ops, int type)
{
	BUG_ON(type < 0 || type > ARRAY_SIZE(onchip_ops));
	*ops = onchip_ops[type];
}

int __init arch_clk_init(void)
{
	struct clk *master;
	int i;

	master = clk_get(NULL, "master_clk");
	for (i = 0; i < ARRAY_SIZE(sh7722_clocks); i++) {
		pr_debug( "Registering clock '%s'\n", sh7722_clocks[i]->name);
		sh7722_clocks[i]->parent = master;
		clk_register(sh7722_clocks[i]);
	}
	clk_put(master);

	for (i = 0; i < ARRAY_SIZE(sh7722_mstpcr); i++) {
		pr_debug( "Registering mstpcr '%s'\n", sh7722_mstpcr[i].name);
		clk_register(&sh7722_mstpcr[i]);
	}

	return 0;
#include <linux/clkdev.h>
#include <linux/sh_clk.h>
#include <asm/clock.h>
#include <cpu/sh7722.h>

/* SH7722 registers */
#define FRQCR		0xa4150000
#define VCLKCR		0xa4150004
#define SCLKACR		0xa4150008
#define SCLKBCR		0xa415000c
#define IRDACLKCR	0xa4150018
#define PLLCR		0xa4150024
#define MSTPCR0		0xa4150030
#define MSTPCR1		0xa4150034
#define MSTPCR2		0xa4150038
#define DLLFRQ		0xa4150050

/* Fixed 32 KHz root clock for RTC and Power Management purposes */
static struct clk r_clk = {
	.rate           = 32768,
};

/*
 * Default rate for the root input clock, reset this with clk_set_rate()
 * from the platform code.
 */
struct clk extal_clk = {
	.rate		= 33333333,
};

/* The dll block multiplies the 32khz r_clk, may be used instead of extal */
static unsigned long dll_recalc(struct clk *clk)
{
	unsigned long mult;

	if (__raw_readl(PLLCR) & 0x1000)
		mult = __raw_readl(DLLFRQ);
	else
		mult = 0;

	return clk->parent->rate * mult;
}

static struct sh_clk_ops dll_clk_ops = {
	.recalc		= dll_recalc,
};

static struct clk dll_clk = {
	.ops		= &dll_clk_ops,
	.parent		= &r_clk,
	.flags		= CLK_ENABLE_ON_INIT,
};

static unsigned long pll_recalc(struct clk *clk)
{
	unsigned long mult = 1;
	unsigned long div = 1;

	if (__raw_readl(PLLCR) & 0x4000)
		mult = (((__raw_readl(FRQCR) >> 24) & 0x1f) + 1);
	else
		div = 2;

	return (clk->parent->rate * mult) / div;
}

static struct sh_clk_ops pll_clk_ops = {
	.recalc		= pll_recalc,
};

static struct clk pll_clk = {
	.ops		= &pll_clk_ops,
	.flags		= CLK_ENABLE_ON_INIT,
};

struct clk *main_clks[] = {
	&r_clk,
	&extal_clk,
	&dll_clk,
	&pll_clk,
};

static int multipliers[] = { 1, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
static int divisors[] = { 1, 3, 2, 5, 3, 4, 5, 6, 8, 10, 12, 16, 20 };

static struct clk_div_mult_table div4_div_mult_table = {
	.divisors = divisors,
	.nr_divisors = ARRAY_SIZE(divisors),
	.multipliers = multipliers,
	.nr_multipliers = ARRAY_SIZE(multipliers),
};

static struct clk_div4_table div4_table = {
	.div_mult_table = &div4_div_mult_table,
};

#define DIV4(_reg, _bit, _mask, _flags) \
  SH_CLK_DIV4(&pll_clk, _reg, _bit, _mask, _flags)

enum { DIV4_I, DIV4_U, DIV4_SH, DIV4_B, DIV4_B3, DIV4_P, DIV4_NR };

struct clk div4_clks[DIV4_NR] = {
	[DIV4_I] = DIV4(FRQCR, 20, 0x1fef, CLK_ENABLE_ON_INIT),
	[DIV4_U] = DIV4(FRQCR, 16, 0x1fff, CLK_ENABLE_ON_INIT),
	[DIV4_SH] = DIV4(FRQCR, 12, 0x1fff, CLK_ENABLE_ON_INIT),
	[DIV4_B] = DIV4(FRQCR, 8, 0x1fff, CLK_ENABLE_ON_INIT),
	[DIV4_B3] = DIV4(FRQCR, 4, 0x1fff, CLK_ENABLE_ON_INIT),
	[DIV4_P] = DIV4(FRQCR, 0, 0x1fff, 0),
};

enum { DIV4_IRDA, DIV4_ENABLE_NR };

struct clk div4_enable_clks[DIV4_ENABLE_NR] = {
	[DIV4_IRDA] = DIV4(IRDACLKCR, 0, 0x1fff, 0),
};

enum { DIV4_SIUA, DIV4_SIUB, DIV4_REPARENT_NR };

struct clk div4_reparent_clks[DIV4_REPARENT_NR] = {
	[DIV4_SIUA] = DIV4(SCLKACR, 0, 0x1fff, 0),
	[DIV4_SIUB] = DIV4(SCLKBCR, 0, 0x1fff, 0),
};

enum { DIV6_V, DIV6_NR };

struct clk div6_clks[DIV6_NR] = {
	[DIV6_V] = SH_CLK_DIV6(&pll_clk, VCLKCR, 0),
};

static struct clk mstp_clks[HWBLK_NR] = {
	[HWBLK_URAM]  = SH_CLK_MSTP32(&div4_clks[DIV4_U], MSTPCR0, 28, CLK_ENABLE_ON_INIT),
	[HWBLK_XYMEM] = SH_CLK_MSTP32(&div4_clks[DIV4_B], MSTPCR0, 26, CLK_ENABLE_ON_INIT),
	[HWBLK_TMU]   = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 15, 0),
	[HWBLK_CMT]   = SH_CLK_MSTP32(&r_clk,		  MSTPCR0, 14, 0),
	[HWBLK_RWDT]  = SH_CLK_MSTP32(&r_clk,		  MSTPCR0, 13, 0),
	[HWBLK_FLCTL] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 10, 0),
	[HWBLK_SCIF0] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 7, 0),
	[HWBLK_SCIF1] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 6, 0),
	[HWBLK_SCIF2] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 5, 0),

	[HWBLK_IIC]   = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR1, 9, 0),
	[HWBLK_RTC]   = SH_CLK_MSTP32(&r_clk,		  MSTPCR1, 8, 0),

	[HWBLK_SDHI]  = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR2, 18, 0),
	[HWBLK_KEYSC] = SH_CLK_MSTP32(&r_clk,		  MSTPCR2, 14, 0),
	[HWBLK_USBF]  = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR2, 11, 0),
	[HWBLK_2DG]   = SH_CLK_MSTP32(&div4_clks[DIV4_B], MSTPCR2, 9, 0),
	[HWBLK_SIU]   = SH_CLK_MSTP32(&div4_clks[DIV4_B], MSTPCR2, 8, 0),
	[HWBLK_JPU]   = SH_CLK_MSTP32(&div4_clks[DIV4_B], MSTPCR2, 6, 0),
	[HWBLK_VOU]   = SH_CLK_MSTP32(&div4_clks[DIV4_B], MSTPCR2, 5, 0),
	[HWBLK_BEU]   = SH_CLK_MSTP32(&div4_clks[DIV4_B], MSTPCR2, 4, 0),
	[HWBLK_CEU]   = SH_CLK_MSTP32(&div4_clks[DIV4_B], MSTPCR2, 3, 0),
	[HWBLK_VEU]   = SH_CLK_MSTP32(&div4_clks[DIV4_B], MSTPCR2, 2, 0),
	[HWBLK_VPU]   = SH_CLK_MSTP32(&div4_clks[DIV4_B], MSTPCR2, 1, 0),
	[HWBLK_LCDC]  = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR2, 0, 0),
};

static struct clk_lookup lookups[] = {
	/* main clocks */
	CLKDEV_CON_ID("rclk", &r_clk),
	CLKDEV_CON_ID("extal", &extal_clk),
	CLKDEV_CON_ID("dll_clk", &dll_clk),
	CLKDEV_CON_ID("pll_clk", &pll_clk),

	/* DIV4 clocks */
	CLKDEV_CON_ID("cpu_clk", &div4_clks[DIV4_I]),
	CLKDEV_CON_ID("umem_clk", &div4_clks[DIV4_U]),
	CLKDEV_CON_ID("shyway_clk", &div4_clks[DIV4_SH]),
	CLKDEV_CON_ID("bus_clk", &div4_clks[DIV4_B]),
	CLKDEV_CON_ID("b3_clk", &div4_clks[DIV4_B3]),
	CLKDEV_CON_ID("peripheral_clk", &div4_clks[DIV4_P]),
	CLKDEV_CON_ID("irda_clk", &div4_enable_clks[DIV4_IRDA]),
	CLKDEV_CON_ID("siua_clk", &div4_reparent_clks[DIV4_SIUA]),
	CLKDEV_CON_ID("siub_clk", &div4_reparent_clks[DIV4_SIUB]),

	/* DIV6 clocks */
	CLKDEV_CON_ID("video_clk", &div6_clks[DIV6_V]),

	/* MSTP clocks */
	CLKDEV_CON_ID("uram0", &mstp_clks[HWBLK_URAM]),
	CLKDEV_CON_ID("xymem0", &mstp_clks[HWBLK_XYMEM]),

	CLKDEV_ICK_ID("fck", "sh-tmu.0", &mstp_clks[HWBLK_TMU]),

	CLKDEV_ICK_ID("fck", "sh-cmt-32.0", &mstp_clks[HWBLK_CMT]),
	CLKDEV_DEV_ID("sh-wdt.0", &mstp_clks[HWBLK_RWDT]),
	CLKDEV_CON_ID("flctl0", &mstp_clks[HWBLK_FLCTL]),

	CLKDEV_DEV_ID("sh-sci.0", &mstp_clks[HWBLK_SCIF0]),
	CLKDEV_DEV_ID("sh-sci.1", &mstp_clks[HWBLK_SCIF1]),
	CLKDEV_DEV_ID("sh-sci.2", &mstp_clks[HWBLK_SCIF2]),

	CLKDEV_DEV_ID("i2c-sh_mobile.0", &mstp_clks[HWBLK_IIC]),
	CLKDEV_CON_ID("rtc0", &mstp_clks[HWBLK_RTC]),
	CLKDEV_DEV_ID("sh_mobile_sdhi.0", &mstp_clks[HWBLK_SDHI]),
	CLKDEV_DEV_ID("sh_keysc.0", &mstp_clks[HWBLK_KEYSC]),
	CLKDEV_CON_ID("usbf0", &mstp_clks[HWBLK_USBF]),
	CLKDEV_CON_ID("2dg0", &mstp_clks[HWBLK_2DG]),
	CLKDEV_DEV_ID("siu-pcm-audio", &mstp_clks[HWBLK_SIU]),
	CLKDEV_DEV_ID("sh-vou.0", &mstp_clks[HWBLK_VOU]),
	CLKDEV_CON_ID("jpu0", &mstp_clks[HWBLK_JPU]),
	CLKDEV_CON_ID("beu0", &mstp_clks[HWBLK_BEU]),
	CLKDEV_DEV_ID("sh_mobile_ceu.0", &mstp_clks[HWBLK_CEU]),
	CLKDEV_CON_ID("veu0", &mstp_clks[HWBLK_VEU]),
	CLKDEV_CON_ID("vpu0", &mstp_clks[HWBLK_VPU]),
	CLKDEV_DEV_ID("sh_mobile_lcdc_fb.0", &mstp_clks[HWBLK_LCDC]),
};

int __init arch_clk_init(void)
{
	int k, ret = 0;

	/* autodetect extal or dll configuration */
	if (__raw_readl(PLLCR) & 0x1000)
		pll_clk.parent = &dll_clk;
	else
		pll_clk.parent = &extal_clk;

	for (k = 0; !ret && (k < ARRAY_SIZE(main_clks)); k++)
		ret = clk_register(main_clks[k]);

	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	if (!ret)
		ret = sh_clk_div4_register(div4_clks, DIV4_NR, &div4_table);

	if (!ret)
		ret = sh_clk_div4_enable_register(div4_enable_clks,
					DIV4_ENABLE_NR, &div4_table);

	if (!ret)
		ret = sh_clk_div4_reparent_register(div4_reparent_clks,
					DIV4_REPARENT_NR, &div4_table);

	if (!ret)
		ret = sh_clk_div6_register(div6_clks, DIV6_NR);

	if (!ret)
		ret = sh_clk_mstp_register(mstp_clks, HWBLK_NR);

	return ret;
}
