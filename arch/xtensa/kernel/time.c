/*
 * arch/xtensa/kernel/time.c
 *
 * Timer and clock support.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2005 Tensilica Inc.
 *
 * Chris Zankel <chris@zankel.net>
 */

#include <linux/errno.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/profile.h>
#include <linux/delay.h>
#include <linux/irqdomain.h>
#include <linux/sched_clock.h>

#include <asm/timex.h>
#include <asm/platform.h>


DEFINE_SPINLOCK(rtc_lock);
EXPORT_SYMBOL(rtc_lock);


#ifdef CONFIG_XTENSA_CALIBRATE_CCOUNT
unsigned long ccount_per_jiffy;		/* per 1/HZ */
unsigned long nsec_per_ccount;		/* nsec per ccount increment */
#endif

static long last_rtc_update = 0;

/*
 * Scheduler clock - returns current tim in nanosec units.
 */

unsigned long long sched_clock(void)
{
	return (unsigned long long)jiffies * (1000000000 / HZ);
unsigned long ccount_freq;		/* ccount Hz */
EXPORT_SYMBOL(ccount_freq);

static cycle_t ccount_read(struct clocksource *cs)
{
	return (cycle_t)get_ccount();
}

static u64 notrace ccount_sched_clock_read(void)
{
	return get_ccount();
}

static struct clocksource ccount_clocksource = {
	.name = "ccount",
	.rating = 200,
	.read = ccount_read,
	.mask = CLOCKSOURCE_MASK(32),
	.flags = CLOCK_SOURCE_IS_CONTINUOUS,
};

static int ccount_timer_set_next_event(unsigned long delta,
		struct clock_event_device *dev);
struct ccount_timer {
	struct clock_event_device evt;
	int irq_enabled;
	char name[24];
};
static DEFINE_PER_CPU(struct ccount_timer, ccount_timer);

static int ccount_timer_set_next_event(unsigned long delta,
		struct clock_event_device *dev)
{
	unsigned long flags, next;
	int ret = 0;

	local_irq_save(flags);
	next = get_ccount() + delta;
	set_linux_timer(next);
	if (next - get_ccount() > delta)
		ret = -ETIME;
	local_irq_restore(flags);

	return ret;
}

/*
 * There is no way to disable the timer interrupt at the device level,
 * only at the intenable register itself. Since enable_irq/disable_irq
 * calls are nested, we need to make sure that these calls are
 * balanced.
 */
static int ccount_timer_shutdown(struct clock_event_device *evt)
{
	struct ccount_timer *timer =
		container_of(evt, struct ccount_timer, evt);

	if (timer->irq_enabled) {
		disable_irq_nosync(evt->irq);
		timer->irq_enabled = 0;
	}
	return 0;
}

static int ccount_timer_set_oneshot(struct clock_event_device *evt)
{
	struct ccount_timer *timer =
		container_of(evt, struct ccount_timer, evt);

	if (!timer->irq_enabled) {
		enable_irq(evt->irq);
		timer->irq_enabled = 1;
	}
	return 0;
}

static irqreturn_t timer_interrupt(int irq, void *dev_id);
static struct irqaction timer_irqaction = {
	.handler =	timer_interrupt,
	.flags =	IRQF_DISABLED,
	.name =		"timer",
};

void __init time_init(void)
{
	time_t sec_o, sec_n = 0;

	/* The platform must provide a function to calibrate the processor
	 * speed for the CALIBRATE.
	 */

#ifdef CONFIG_XTENSA_CALIBRATE_CCOUNT
	printk("Calibrating CPU frequency ");
	platform_calibrate_ccount();
	printk("%d.%02d MHz\n", (int)ccount_per_jiffy/(1000000/HZ),
			(int)(ccount_per_jiffy/(10000/HZ))%100);
#endif

	/* Set time from RTC (if provided) */

	if (platform_get_rtc_time(&sec_o) == 0)
		while (platform_get_rtc_time(&sec_n))
			if (sec_o != sec_n)
				break;

	xtime.tv_nsec = 0;
	last_rtc_update = xtime.tv_sec = sec_n;

	set_normalized_timespec(&wall_to_monotonic,
		-xtime.tv_sec, -xtime.tv_nsec);

	/* Initialize the linux timer interrupt. */

	setup_irq(LINUX_TIMER_INT, &timer_irqaction);
	set_linux_timer(get_ccount() + CCOUNT_PER_JIFFY);
}


int do_settimeofday(struct timespec *tv)
{
	time_t wtm_sec, sec = tv->tv_sec;
	long wtm_nsec, nsec = tv->tv_nsec;
	unsigned long delta;

	if ((unsigned long)tv->tv_nsec >= NSEC_PER_SEC)
		return -EINVAL;

	write_seqlock_irq(&xtime_lock);

	/* This is revolting. We need to set "xtime" correctly. However, the
	 * value in this location is the value at the most recent update of
	 * wall time.  Discover what correction gettimeofday() would have
	 * made, and then undo it!
	 */

	delta = CCOUNT_PER_JIFFY;
	delta += get_ccount() - get_linux_timer();
	nsec -= delta * NSEC_PER_CCOUNT;

	wtm_sec  = wall_to_monotonic.tv_sec + (xtime.tv_sec - sec);
	wtm_nsec = wall_to_monotonic.tv_nsec + (xtime.tv_nsec - nsec);

	set_normalized_timespec(&xtime, sec, nsec);
	set_normalized_timespec(&wall_to_monotonic, wtm_sec, wtm_nsec);

	ntp_clear();
	write_sequnlock_irq(&xtime_lock);
	return 0;
}

EXPORT_SYMBOL(do_settimeofday);


void do_gettimeofday(struct timeval *tv)
{
	unsigned long flags;
	unsigned long volatile sec, usec, delta, seq;

	do {
		seq = read_seqbegin_irqsave(&xtime_lock, flags);

		sec = xtime.tv_sec;
		usec = (xtime.tv_nsec / NSEC_PER_USEC);

		delta = get_linux_timer() - get_ccount();

	} while (read_seqretry_irqrestore(&xtime_lock, seq, flags));

	usec += (((unsigned long) CCOUNT_PER_JIFFY - delta)
		 * (unsigned long) NSEC_PER_CCOUNT) / NSEC_PER_USEC;

	for (; usec >= 1000000; sec++, usec -= 1000000)
		;

	tv->tv_sec = sec;
	tv->tv_usec = usec;
}

EXPORT_SYMBOL(do_gettimeofday);

	.flags =	IRQF_TIMER,
	.name =		"timer",
};

void local_timer_setup(unsigned cpu)
{
	struct ccount_timer *timer = &per_cpu(ccount_timer, cpu);
	struct clock_event_device *clockevent = &timer->evt;

	timer->irq_enabled = 1;
	clockevent->name = timer->name;
	snprintf(timer->name, sizeof(timer->name), "ccount_clockevent_%u", cpu);
	clockevent->features = CLOCK_EVT_FEAT_ONESHOT;
	clockevent->rating = 300;
	clockevent->set_next_event = ccount_timer_set_next_event;
	clockevent->set_state_shutdown = ccount_timer_shutdown;
	clockevent->set_state_oneshot = ccount_timer_set_oneshot;
	clockevent->tick_resume = ccount_timer_set_oneshot;
	clockevent->cpumask = cpumask_of(cpu);
	clockevent->irq = irq_create_mapping(NULL, LINUX_TIMER_INT);
	if (WARN(!clockevent->irq, "error: can't map timer irq"))
		return;
	clockevents_config_and_register(clockevent, ccount_freq,
					0xf, 0xffffffff);
}

void __init time_init(void)
{
#ifdef CONFIG_XTENSA_CALIBRATE_CCOUNT
	printk("Calibrating CPU frequency ");
	platform_calibrate_ccount();
	printk("%d.%02d MHz\n", (int)ccount_freq/1000000,
			(int)(ccount_freq/10000)%100);
#else
	ccount_freq = CONFIG_XTENSA_CPU_CLOCK*1000000UL;
#endif
	clocksource_register_hz(&ccount_clocksource, ccount_freq);
	local_timer_setup(0);
	setup_irq(this_cpu_ptr(&ccount_timer)->evt.irq, &timer_irqaction);
	sched_clock_register(ccount_sched_clock_read, 32, ccount_freq);
	clocksource_probe();
}

/*
 * The timer interrupt is called HZ times per second.
 */

irqreturn_t timer_interrupt (int irq, void *dev_id)
{

	unsigned long next;

	next = get_linux_timer();

again:
	while ((signed long)(get_ccount() - next) > 0) {

		profile_tick(CPU_PROFILING);
#ifndef CONFIG_SMP
		update_process_times(user_mode(get_irq_regs()));
#endif

		write_seqlock(&xtime_lock);

		do_timer(1); /* Linux handler in kernel/timer.c */

		/* Note that writing CCOMPARE clears the interrupt. */

		next += CCOUNT_PER_JIFFY;
		set_linux_timer(next);

		if (ntp_synced() &&
		    xtime.tv_sec - last_rtc_update >= 659 &&
		    abs((xtime.tv_nsec/1000)-(1000000-1000000/HZ))<5000000/HZ) {

			if (platform_set_rtc_time(xtime.tv_sec+1) == 0)
				last_rtc_update = xtime.tv_sec+1;
			else
				/* Do it again in 60 s */
				last_rtc_update += 60;
		}
		write_sequnlock(&xtime_lock);
	}

	/* Allow platform to do something useful (Wdog). */

	platform_heartbeat();

	/* Make sure we didn't miss any tick... */

	if ((signed long)(get_ccount() - next) > 0)
		goto again;

irqreturn_t timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = &this_cpu_ptr(&ccount_timer)->evt;

	set_linux_timer(get_linux_timer());
	evt->event_handler(evt);

	/* Allow platform to do something useful (Wdog). */
	platform_heartbeat();

	return IRQ_HANDLED;
}

#ifndef CONFIG_GENERIC_CALIBRATE_DELAY
void __cpuinit calibrate_delay(void)
{
	loops_per_jiffy = CCOUNT_PER_JIFFY;
void calibrate_delay(void)
{
	loops_per_jiffy = ccount_freq / HZ;
	printk("Calibrating delay loop (skipped)... "
	       "%lu.%02lu BogoMIPS preset\n",
	       loops_per_jiffy/(1000000/HZ),
	       (loops_per_jiffy/(10000/HZ)) % 100);
}
#endif

