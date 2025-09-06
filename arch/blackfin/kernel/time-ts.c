/*
 * linux/arch/kernel/time-ts.c
 *
 * Based on arm clockevents implementation and old bfin time tick.
 *
 * Copyright(C) 2008, GeoTechnologies, Vitja Makarov
 *
 * This code is licenced under the GPL version 2. For details see
 * kernel-base/COPYING.
 */
 * Based on arm clockevents implementation and old bfin time tick.
 *
 * Copyright 2008-2009 Analog Devics Inc.
 *                2008 GeoTechnologies
 *                     Vitja Makarov
 *
 * Licensed under the GPL-2
 */

#include <linux/module.h>
#include <linux/profile.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/irq.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/cpufreq.h>

#include <asm/blackfin.h>
#include <asm/time.h>

#ifdef CONFIG_CYCLES_CLOCKSOURCE

/* Accelerators for sched_clock()
 * convert from cycles(64bits) => nanoseconds (64bits)
 *  basic equation:
 *		ns = cycles / (freq / ns_per_sec)
 *		ns = cycles * (ns_per_sec / freq)
 *		ns = cycles * (10^9 / (cpu_khz * 10^3))
 *		ns = cycles * (10^6 / cpu_khz)
 *
 *	Then we use scaling math (suggested by george@mvista.com) to get:
 *		ns = cycles * (10^6 * SC / cpu_khz) / SC
 *		ns = cycles * cyc2ns_scale / SC
 *
 *	And since SC is a constant power of two, we can convert the div
 *  into a shift.
 *
 *  We can use khz divisor instead of mhz to keep a better precision, since
 *  cyc2ns_scale is limited to 10^6 * 2^10, which fits in 32 bits.
 *  (mathieu.desnoyers@polymtl.ca)
 *
 *			-johnstul@us.ibm.com "math is hard, lets go shopping!"
 */

static unsigned long cyc2ns_scale;
#define CYC2NS_SCALE_FACTOR 10 /* 2^10, carefully chosen */

static inline void set_cyc2ns_scale(unsigned long cpu_khz)
{
	cyc2ns_scale = (1000000 << CYC2NS_SCALE_FACTOR) / cpu_khz;
}

static inline unsigned long long cycles_2_ns(cycle_t cyc)
{
	return (cyc * cyc2ns_scale) >> CYC2NS_SCALE_FACTOR;
}

static cycle_t read_cycles(void)
{
	return __bfin_cycles_off + (get_cycles() << __bfin_cycles_mod);
}

unsigned long long sched_clock(void)
{
	return cycles_2_ns(read_cycles());
}

static struct clocksource clocksource_bfin = {
	.name		= "bfin_cycles",
	.rating		= 350,
	.read		= read_cycles,
	.mask		= CLOCKSOURCE_MASK(64),
	.shift		= 22,
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static int __init bfin_clocksource_init(void)
{
	set_cyc2ns_scale(get_cclk() / 1000);

	clocksource_bfin.mult = clocksource_hz2mult(get_cclk(), clocksource_bfin.shift);

	if (clocksource_register(&clocksource_bfin))
#include <asm/gptimers.h>
#include <asm/nmi.h>


#if defined(CONFIG_CYCLES_CLOCKSOURCE)

static notrace cycle_t bfin_read_cycles(struct clocksource *cs)
{
#ifdef CONFIG_CPU_FREQ
	return __bfin_cycles_off + (get_cycles() << __bfin_cycles_mod);
#else
	return get_cycles();
#endif
}

static struct clocksource bfin_cs_cycles = {
	.name		= "bfin_cs_cycles",
	.rating		= 400,
	.read		= bfin_read_cycles,
	.mask		= CLOCKSOURCE_MASK(64),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static inline unsigned long long bfin_cs_cycles_sched_clock(void)
{
	return clocksource_cyc2ns(bfin_read_cycles(&bfin_cs_cycles),
		bfin_cs_cycles.mult, bfin_cs_cycles.shift);
}

static int __init bfin_cs_cycles_init(void)
{
	if (clocksource_register_hz(&bfin_cs_cycles, get_cclk()))
		panic("failed to register clocksource");

	return 0;
}

#else
# define bfin_clocksource_init()
#endif

static int bfin_timer_set_next_event(unsigned long cycles,
                                     struct clock_event_device *evt)
{
	bfin_write_TCOUNT(cycles);
	CSYNC();
	return 0;
}

static void bfin_timer_set_mode(enum clock_event_mode mode,
                                struct clock_event_device *evt)
{
	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC: {
		unsigned long tcount = ((get_cclk() / (HZ * TIME_SCALE)) - 1);
		bfin_write_TCNTL(TMPWR);
		bfin_write_TSCALE(TIME_SCALE - 1);
		CSYNC();
		bfin_write_TPERIOD(tcount);
		bfin_write_TCOUNT(tcount);
		bfin_write_TCNTL(TMPWR | TMREN | TAUTORLD);
		CSYNC();
		break;
	}
	case CLOCK_EVT_MODE_ONESHOT:
		bfin_write_TSCALE(TIME_SCALE - 1);
		bfin_write_TCOUNT(0);
		bfin_write_TCNTL(TMPWR | TMREN);
		CSYNC();
		break;
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		bfin_write_TCNTL(0);
		CSYNC();
		break;
	case CLOCK_EVT_MODE_RESUME:
		break;
	}
}

static void __init bfin_timer_init(void)
#else
# define bfin_cs_cycles_init()
#endif

#ifdef CONFIG_GPTMR0_CLOCKSOURCE

void __init setup_gptimer0(void)
{
	disable_gptimers(TIMER0bit);

#ifdef CONFIG_BF60x
	bfin_write16(TIMER_DATA_IMSK, 0);
	set_gptimer_config(TIMER0_id,  TIMER_OUT_DIS
		| TIMER_MODE_PWM_CONT | TIMER_PULSE_HI | TIMER_IRQ_PER);
#else
	set_gptimer_config(TIMER0_id, \
		TIMER_OUT_DIS | TIMER_PERIOD_CNT | TIMER_MODE_PWM);
#endif
	set_gptimer_period(TIMER0_id, -1);
	set_gptimer_pwidth(TIMER0_id, -2);
	SSYNC();
	enable_gptimers(TIMER0bit);
}

static cycle_t bfin_read_gptimer0(struct clocksource *cs)
{
	return bfin_read_TIMER0_COUNTER();
}

static struct clocksource bfin_cs_gptimer0 = {
	.name		= "bfin_cs_gptimer0",
	.rating		= 350,
	.read		= bfin_read_gptimer0,
	.mask		= CLOCKSOURCE_MASK(32),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static inline unsigned long long bfin_cs_gptimer0_sched_clock(void)
{
	return clocksource_cyc2ns(bfin_read_TIMER0_COUNTER(),
		bfin_cs_gptimer0.mult, bfin_cs_gptimer0.shift);
}

static int __init bfin_cs_gptimer0_init(void)
{
	setup_gptimer0();

	if (clocksource_register_hz(&bfin_cs_gptimer0, get_sclk()))
		panic("failed to register clocksource");

	return 0;
}
#else
# define bfin_cs_gptimer0_init()
#endif

#if defined(CONFIG_GPTMR0_CLOCKSOURCE) || defined(CONFIG_CYCLES_CLOCKSOURCE)
/* prefer to use cycles since it has higher rating */
notrace unsigned long long sched_clock(void)
{
#if defined(CONFIG_CYCLES_CLOCKSOURCE)
	return bfin_cs_cycles_sched_clock();
#else
	return bfin_cs_gptimer0_sched_clock();
#endif
}
#endif

#if defined(CONFIG_TICKSOURCE_GPTMR0)
static int bfin_gptmr0_set_next_event(unsigned long cycles,
                                     struct clock_event_device *evt)
{
	disable_gptimers(TIMER0bit);

	/* it starts counting three SCLK cycles after the TIMENx bit is set */
	set_gptimer_pwidth(TIMER0_id, cycles - 3);
	enable_gptimers(TIMER0bit);
	return 0;
}

static int bfin_gptmr0_set_periodic(struct clock_event_device *evt)
{
#ifndef CONFIG_BF60x
	set_gptimer_config(TIMER0_id,
			   TIMER_OUT_DIS | TIMER_IRQ_ENA |
			   TIMER_PERIOD_CNT | TIMER_MODE_PWM);
#else
	set_gptimer_config(TIMER0_id,
			   TIMER_OUT_DIS | TIMER_MODE_PWM_CONT |
			   TIMER_PULSE_HI | TIMER_IRQ_PER);
#endif

	set_gptimer_period(TIMER0_id, get_sclk() / HZ);
	set_gptimer_pwidth(TIMER0_id, get_sclk() / HZ - 1);
	enable_gptimers(TIMER0bit);
	return 0;
}

static int bfin_gptmr0_set_oneshot(struct clock_event_device *evt)
{
	disable_gptimers(TIMER0bit);
#ifndef CONFIG_BF60x
	set_gptimer_config(TIMER0_id,
			   TIMER_OUT_DIS | TIMER_IRQ_ENA | TIMER_MODE_PWM);
#else
	set_gptimer_config(TIMER0_id,
			   TIMER_OUT_DIS | TIMER_MODE_PWM | TIMER_PULSE_HI |
			   TIMER_IRQ_WID_DLY);
#endif

	set_gptimer_period(TIMER0_id, 0);
	return 0;
}

static int bfin_gptmr0_shutdown(struct clock_event_device *evt)
{
	disable_gptimers(TIMER0bit);
	return 0;
}

static void bfin_gptmr0_ack(void)
{
	clear_gptimer_intr(TIMER0_id);
}

static void __init bfin_gptmr0_init(void)
{
	disable_gptimers(TIMER0bit);
}

#ifdef CONFIG_CORE_TIMER_IRQ_L1
__attribute__((l1_text))
#endif
irqreturn_t bfin_gptmr0_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;
	smp_mb();
	/*
	 * We want to ACK before we handle so that we can handle smaller timer
	 * intervals.  This way if the timer expires again while we're handling
	 * things, we're more likely to see that 2nd int rather than swallowing
	 * it by ACKing the int at the end of this handler.
	 */
	bfin_gptmr0_ack();
	evt->event_handler(evt);
	return IRQ_HANDLED;
}

static struct irqaction gptmr0_irq = {
	.name		= "Blackfin GPTimer0",
	.flags		= IRQF_TIMER | IRQF_IRQPOLL | IRQF_PERCPU,
	.handler	= bfin_gptmr0_interrupt,
};

static struct clock_event_device clockevent_gptmr0 = {
	.name			= "bfin_gptimer0",
	.rating			= 300,
	.irq			= IRQ_TIMER0,
	.shift			= 32,
	.features		= CLOCK_EVT_FEAT_PERIODIC |
				  CLOCK_EVT_FEAT_ONESHOT,
	.set_next_event		= bfin_gptmr0_set_next_event,
	.set_state_shutdown	= bfin_gptmr0_shutdown,
	.set_state_periodic	= bfin_gptmr0_set_periodic,
	.set_state_oneshot	= bfin_gptmr0_set_oneshot,
};

static void __init bfin_gptmr0_clockevent_init(struct clock_event_device *evt)
{
	unsigned long clock_tick;

	clock_tick = get_sclk();
	evt->mult = div_sc(clock_tick, NSEC_PER_SEC, evt->shift);
	evt->max_delta_ns = clockevent_delta2ns(-1, evt);
	evt->min_delta_ns = clockevent_delta2ns(100, evt);

	evt->cpumask = cpumask_of(0);

	clockevents_register_device(evt);
}
#endif /* CONFIG_TICKSOURCE_GPTMR0 */

#if defined(CONFIG_TICKSOURCE_CORETMR)
/* per-cpu local core timer */
DEFINE_PER_CPU(struct clock_event_device, coretmr_events);

static int bfin_coretmr_set_next_event(unsigned long cycles,
				struct clock_event_device *evt)
{
	bfin_write_TCNTL(TMPWR);
	CSYNC();
	bfin_write_TCOUNT(cycles);
	CSYNC();
	bfin_write_TCNTL(TMPWR | TMREN);
	return 0;
}

static int bfin_coretmr_set_periodic(struct clock_event_device *evt)
{
	unsigned long tcount = ((get_cclk() / (HZ * TIME_SCALE)) - 1);

	bfin_write_TCNTL(TMPWR);
	CSYNC();
	bfin_write_TSCALE(TIME_SCALE - 1);
	bfin_write_TPERIOD(tcount);
	bfin_write_TCOUNT(tcount);
	CSYNC();
	bfin_write_TCNTL(TMPWR | TMREN | TAUTORLD);
	return 0;
}

static int bfin_coretmr_set_oneshot(struct clock_event_device *evt)
{
	bfin_write_TCNTL(TMPWR);
	CSYNC();
	bfin_write_TSCALE(TIME_SCALE - 1);
	bfin_write_TPERIOD(0);
	bfin_write_TCOUNT(0);
	return 0;
}

static int bfin_coretmr_shutdown(struct clock_event_device *evt)
{
	bfin_write_TCNTL(0);
	CSYNC();
	return 0;
}

void bfin_coretmr_init(void)
{
	/* power up the timer, but don't enable it just yet */
	bfin_write_TCNTL(TMPWR);
	CSYNC();

	/*
	 * the TSCALE prescaler counter.
	 */
	/* the TSCALE prescaler counter. */
	bfin_write_TSCALE(TIME_SCALE - 1);
	bfin_write_TPERIOD(0);
	bfin_write_TCOUNT(0);

	/* now enable the timer */
	CSYNC();
}

/*
 * timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "do_timer()" routine every clocktick
 */
#ifdef CONFIG_CORE_TIMER_IRQ_L1
__attribute__((l1_text))
#endif
irqreturn_t timer_interrupt(int irq, void *dev_id);

static struct clock_event_device clockevent_bfin = {
	.name		= "bfin_core_timer",
	.features	= CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.shift		= 32,
	.cpumask	= CPU_MASK_CPU0,
	.set_next_event = bfin_timer_set_next_event,
	.set_mode	= bfin_timer_set_mode,
};

static struct irqaction bfin_timer_irq = {
	.name		= "Blackfin Core Timer",
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= timer_interrupt,
	.dev_id		= &clockevent_bfin,
};

irqreturn_t timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;
	evt->event_handler(evt);
	return IRQ_HANDLED;
}

static int __init bfin_clockevent_init(void)
{
	unsigned long timer_clk;

	timer_clk = get_cclk() / TIME_SCALE;

	setup_irq(IRQ_CORETMR, &bfin_timer_irq);
	bfin_timer_init();

	clockevent_bfin.mult = div_sc(timer_clk, NSEC_PER_SEC, clockevent_bfin.shift);
	clockevent_bfin.max_delta_ns = clockevent_delta2ns(-1, &clockevent_bfin);
	clockevent_bfin.min_delta_ns = clockevent_delta2ns(100, &clockevent_bfin);
	clockevents_register_device(&clockevent_bfin);

	return 0;
	CSYNC();
}

#ifdef CONFIG_CORE_TIMER_IRQ_L1
__attribute__((l1_text))
#endif

irqreturn_t bfin_coretmr_interrupt(int irq, void *dev_id)
{
	int cpu = smp_processor_id();
	struct clock_event_device *evt = &per_cpu(coretmr_events, cpu);

	smp_mb();
	evt->event_handler(evt);

	touch_nmi_watchdog();

	return IRQ_HANDLED;
}

static struct irqaction coretmr_irq = {
	.name		= "Blackfin CoreTimer",
	.flags		= IRQF_TIMER | IRQF_IRQPOLL | IRQF_PERCPU,
	.handler	= bfin_coretmr_interrupt,
};

void bfin_coretmr_clockevent_init(void)
{
	unsigned long clock_tick;
	unsigned int cpu = smp_processor_id();
	struct clock_event_device *evt = &per_cpu(coretmr_events, cpu);

#ifdef CONFIG_SMP
	evt->broadcast = smp_timer_broadcast;
#endif

	evt->name = "bfin_core_timer";
	evt->rating = 350;
	evt->irq = -1;
	evt->shift = 32;
	evt->features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT;
	evt->set_next_event = bfin_coretmr_set_next_event;
	evt->set_state_shutdown = bfin_coretmr_shutdown;
	evt->set_state_periodic = bfin_coretmr_set_periodic;
	evt->set_state_oneshot = bfin_coretmr_set_oneshot;

	clock_tick = get_cclk() / TIME_SCALE;
	evt->mult = div_sc(clock_tick, NSEC_PER_SEC, evt->shift);
	evt->max_delta_ns = clockevent_delta2ns(-1, evt);
	evt->min_delta_ns = clockevent_delta2ns(100, evt);

	evt->cpumask = cpumask_of(cpu);

	clockevents_register_device(evt);
}
#endif /* CONFIG_TICKSOURCE_CORETMR */


void read_persistent_clock(struct timespec *ts)
{
	time_t secs_since_1970 = (365 * 37 + 9) * 24 * 60 * 60;	/* 1 Jan 2007 */
	ts->tv_sec = secs_since_1970;
	ts->tv_nsec = 0;
}

void __init time_init(void)
{
	time_t secs_since_1970 = (365 * 37 + 9) * 24 * 60 * 60;	/* 1 Jan 2007 */

#ifdef CONFIG_RTC_DRV_BFIN
	/* [#2663] hack to filter junk RTC values that would cause
	 * userspace to have to deal with time values greater than
	 * 2^31 seconds (which uClibc cannot cope with yet)
	 */
	if ((bfin_read_RTC_STAT() & 0xC0000000) == 0xC0000000) {
		printk(KERN_NOTICE "bfin-rtc: invalid date; resetting\n");
		bfin_write_RTC_STAT(0);
	}
#endif

	/* Initialize xtime. From now on, xtime is updated with timer interrupts */
	xtime.tv_sec = secs_since_1970;
	xtime.tv_nsec = 0;
	set_normalized_timespec(&wall_to_monotonic, -xtime.tv_sec, -xtime.tv_nsec);

	bfin_clocksource_init();
	bfin_clockevent_init();
	bfin_cs_cycles_init();
	bfin_cs_gptimer0_init();

#if defined(CONFIG_TICKSOURCE_CORETMR)
	bfin_coretmr_init();
	setup_irq(IRQ_CORETMR, &coretmr_irq);
	bfin_coretmr_clockevent_init();
#endif

#if defined(CONFIG_TICKSOURCE_GPTMR0)
	bfin_gptmr0_init();
	setup_irq(IRQ_TIMER0, &gptmr0_irq);
	gptmr0_irq.dev_id = &clockevent_gptmr0;
	bfin_gptmr0_clockevent_init(&clockevent_gptmr0);
#endif

#if !defined(CONFIG_TICKSOURCE_CORETMR) && !defined(CONFIG_TICKSOURCE_GPTMR0)
# error at least one clock event device is required
#endif
}
