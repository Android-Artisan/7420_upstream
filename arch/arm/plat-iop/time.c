/*
 * arch/arm/plat-iop/time.c
 *
 * Timer code for IOP32x and IOP33x based systems
 *
 * Author: Deepak Saxena <dsaxena@mvista.com>
 *
 * Copyright 2002-2003 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/timex.h>
#include <mach/hardware.h>
#include <asm/io.h>
#include <linux/io.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/export.h>
#include <linux/sched_clock.h>
#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/mach/irq.h>
#include <asm/mach/time.h>
#include <mach/time.h>

static unsigned long ticks_per_jiffy;
static unsigned long ticks_per_usec;
static unsigned long next_jiffy_time;

unsigned long iop_gettimeoffset(void)
{
	unsigned long offset, temp;

	/* enable cp6, if necessary, to avoid taking the overhead of an
	 * undefined instruction trap
	 */
	asm volatile (
	"mrc	p15, 0, %0, c15, c1, 0\n\t"
	"tst	%0, #(1 << 6)\n\t"
	"orreq	%0, %0, #(1 << 6)\n\t"
	"mcreq	p15, 0, %0, c15, c1, 0\n\t"
#ifdef CONFIG_CPU_XSCALE
	"mrceq	p15, 0, %0, c15, c1, 0\n\t"
	"moveq	%0, %0\n\t"
	"subeq	pc, pc, #4\n\t"
#endif
	: "=r"(temp) : : "cc");

	offset = next_jiffy_time - read_tcr1();

	return offset / ticks_per_usec;
}

static irqreturn_t
iop_timer_interrupt(int irq, void *dev_id)
{
	write_tisr(1);

	while ((signed long)(next_jiffy_time - read_tcr1())
		>= ticks_per_jiffy) {
		timer_tick();
		next_jiffy_time -= ticks_per_jiffy;
	}

/*
 * Minimum clocksource/clockevent timer range in seconds
 */
#define IOP_MIN_RANGE 4

/*
 * IOP clocksource (free-running timer 1).
 */
static cycle_t notrace iop_clocksource_read(struct clocksource *unused)
{
	return 0xffffffffu - read_tcr1();
}

static struct clocksource iop_clocksource = {
	.name 		= "iop_timer1",
	.rating		= 300,
	.read		= iop_clocksource_read,
	.mask		= CLOCKSOURCE_MASK(32),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

/*
 * IOP sched_clock() implementation via its clocksource.
 */
static u64 notrace iop_read_sched_clock(void)
{
	return 0xffffffffu - read_tcr1();
}

/*
 * IOP clockevents (interrupting timer 0).
 */
static int iop_set_next_event(unsigned long delta,
			      struct clock_event_device *unused)
{
	u32 tmr = IOP_TMR_PRIVILEGED | IOP_TMR_RATIO_1_1;

	BUG_ON(delta == 0);
	write_tmr0(tmr & ~(IOP_TMR_EN | IOP_TMR_RELOAD));
	write_tcr0(delta);
	write_tmr0((tmr & ~IOP_TMR_RELOAD) | IOP_TMR_EN);

	return 0;
}

static unsigned long ticks_per_jiffy;

static int iop_set_periodic(struct clock_event_device *evt)
{
	u32 tmr = read_tmr0();

	write_tmr0(tmr & ~IOP_TMR_EN);
	write_tcr0(ticks_per_jiffy - 1);
	write_trr0(ticks_per_jiffy - 1);
	tmr |= (IOP_TMR_RELOAD | IOP_TMR_EN);

	write_tmr0(tmr);
	return 0;
}

static int iop_set_oneshot(struct clock_event_device *evt)
{
	u32 tmr = read_tmr0();

	/* ->set_next_event sets period and enables timer */
	tmr &= ~(IOP_TMR_RELOAD | IOP_TMR_EN);
	write_tmr0(tmr);
	return 0;
}

static int iop_shutdown(struct clock_event_device *evt)
{
	u32 tmr = read_tmr0();

	tmr &= ~IOP_TMR_EN;
	write_tmr0(tmr);
	return 0;
}

static int iop_resume(struct clock_event_device *evt)
{
	u32 tmr = read_tmr0();

	tmr |= IOP_TMR_EN;
	write_tmr0(tmr);
	return 0;
}

static struct clock_event_device iop_clockevent = {
	.name			= "iop_timer0",
	.features		= CLOCK_EVT_FEAT_PERIODIC |
				  CLOCK_EVT_FEAT_ONESHOT,
	.rating			= 300,
	.set_next_event		= iop_set_next_event,
	.set_state_shutdown	= iop_shutdown,
	.set_state_periodic	= iop_set_periodic,
	.tick_resume		= iop_resume,
	.set_state_oneshot	= iop_set_oneshot,
};

static irqreturn_t
iop_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	write_tisr(1);
	evt->event_handler(evt);
	return IRQ_HANDLED;
}

static struct irqaction iop_timer_irq = {
	.name		= "IOP Timer Tick",
	.handler	= iop_timer_interrupt,
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.flags		= IRQF_TIMER | IRQF_IRQPOLL,
	.dev_id		= &iop_clockevent,
};

static unsigned long iop_tick_rate;
unsigned long get_iop_tick_rate(void)
{
	return iop_tick_rate;
}
EXPORT_SYMBOL(get_iop_tick_rate);

void __init iop_init_time(unsigned long tick_rate)
{
	u32 timer_ctl;

	ticks_per_jiffy = (tick_rate + HZ/2) / HZ;
	ticks_per_usec = tick_rate / 1000000;
	next_jiffy_time = 0xffffffff;
	sched_clock_register(iop_read_sched_clock, 32, tick_rate);

	ticks_per_jiffy = DIV_ROUND_CLOSEST(tick_rate, HZ);
	iop_tick_rate = tick_rate;

	timer_ctl = IOP_TMR_EN | IOP_TMR_PRIVILEGED |
			IOP_TMR_RELOAD | IOP_TMR_RATIO_1_1;

	/*
	 * We use timer 0 for our timer interrupt, and timer 1 as
	 * monotonic counter for tracking missed jiffies.
	 */
	write_trr0(ticks_per_jiffy - 1);
	write_tmr0(timer_ctl);
	write_trr1(0xffffffff);
	write_tmr1(timer_ctl);

	setup_irq(IRQ_IOP_TIMER0, &iop_timer_irq);
	 * Set up interrupting clockevent timer 0.
	 */
	write_tmr0(timer_ctl & ~IOP_TMR_EN);
	write_tisr(1);
	setup_irq(IRQ_IOP_TIMER0, &iop_timer_irq);
	iop_clockevent.cpumask = cpumask_of(0);
	clockevents_config_and_register(&iop_clockevent, tick_rate,
					0xf, 0xfffffffe);

	/*
	 * Set up free-running clocksource timer 1.
	 */
	write_trr1(0xffffffff);
	write_tcr1(0xffffffff);
	write_tmr1(timer_ctl);
	clocksource_register_hz(&iop_clocksource, tick_rate);
}
