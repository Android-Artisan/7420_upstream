/*
 * Copyright (C) 2015 Anton Ivanov (aivanov@{brocade.com,kot-begemot.co.uk})
 * Copyright (C) 2015 Thomas Meyer (thomas@m3y3r.de)
 * Copyright (C) 2012-2014 Cisco Systems
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <stddef.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include "kern_constants.h"
#include "kern_util.h"
#include "os.h"
#include "process.h"
#include "user.h"

int set_interval(void)
{
	int usec = UM_USEC_PER_SEC / UM_HZ;
	struct itimerval interval = ((struct itimerval) { { 0, usec },
							  { 0, usec } });

	if (setitimer(ITIMER_VIRTUAL, &interval, NULL) == -1)
		return -errno;

	return 0;
}

int timer_one_shot(int ticks)
{
	unsigned long usec = ticks * UM_USEC_PER_SEC / UM_HZ;
	unsigned long sec = usec / UM_USEC_PER_SEC;
	struct itimerval interval;

	usec %= UM_USEC_PER_SEC;
	interval = ((struct itimerval) { { 0, 0 }, { sec, usec } });

	if (setitimer(ITIMER_VIRTUAL, &interval, NULL) == -1)
		return -errno;

	return 0;
}

/**
 * timeval_to_ns - Convert timeval to nanoseconds
 * @ts:		pointer to the timeval variable to be converted
 *
 * Returns the scalar nanosecond representation of the timeval
 * parameter.
 *
 * Ripped from linux/time.h because it's a kernel header, and thus
 * unusable from here.
 */
#include <kern_util.h>
#include <os.h>
#include <string.h>
#include <timer-internal.h>

static timer_t event_high_res_timer = 0;

static inline long long timeval_to_ns(const struct timeval *tv)
{
	return ((long long) tv->tv_sec * UM_NSEC_PER_SEC) +
		tv->tv_usec * UM_NSEC_PER_USEC;
}

long long disable_timer(void)
{
	struct itimerval time = ((struct itimerval) { { 0, 0 }, { 0, 0 } });
	int remain, max = UM_NSEC_PER_SEC / UM_HZ;

	if (setitimer(ITIMER_VIRTUAL, &time, &time) < 0)
		printk(UM_KERN_ERR "disable_timer - setitimer failed, "
		       "errno = %d\n", errno);

	remain = timeval_to_ns(&time.it_value);
	if (remain > max)
		remain = max;

	return remain;
static inline long long timespec_to_ns(const struct timespec *ts)
{
	return ((long long) ts->tv_sec * UM_NSEC_PER_SEC) +
		ts->tv_nsec;
}

long long os_persistent_clock_emulation (void) {
	struct timespec realtime_tp;

	clock_gettime(CLOCK_REALTIME, &realtime_tp);
	return timespec_to_ns(&realtime_tp);
}

/**
 * os_timer_create() - create an new posix (interval) timer
 */
int os_timer_create(void* timer) {

	timer_t* t = timer;

	if(t == NULL) {
		t = &event_high_res_timer;
	}

	if (timer_create(
		CLOCK_MONOTONIC,
		NULL,
		t) == -1) {
		return -1;
	}
	return 0;
}

int os_timer_set_interval(void* timer, void* i)
{
	struct itimerspec its;
	unsigned long long nsec;
	timer_t* t = timer;
	struct itimerspec* its_in = i;

	if(t == NULL) {
		t = &event_high_res_timer;
	}

	nsec = UM_NSEC_PER_SEC / UM_HZ;

	if(its_in != NULL) {
		its.it_value.tv_sec = its_in->it_value.tv_sec;
		its.it_value.tv_nsec = its_in->it_value.tv_nsec;
	} else {
		its.it_value.tv_sec = 0;
		its.it_value.tv_nsec = nsec;
	}

	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = nsec;

	if(timer_settime(*t, 0, &its, NULL) == -1) {
		return -errno;
	}

	return 0;
}

/**
 * os_timer_remain() - returns the remaining nano seconds of the given interval
 *                     timer
 * Because this is the remaining time of an interval timer, which correspondends
 * to HZ, this value can never be bigger than one second. Just
 * the nanosecond part of the timer is returned.
 * The returned time is relative to the start time of the interval timer.
 * Return an negative value in an error case.
 */
long os_timer_remain(void* timer)
{
	struct itimerspec its;
	timer_t* t = timer;

	if(t == NULL) {
		t = &event_high_res_timer;
	}

	if(timer_gettime(t, &its) == -1) {
		return -errno;
	}

	return its.it_value.tv_nsec;
}

int os_timer_one_shot(int ticks)
{
	struct itimerspec its;
	unsigned long long nsec;
	unsigned long sec;

    nsec = (ticks + 1);
    sec = nsec / UM_NSEC_PER_SEC;
	nsec = nsec % UM_NSEC_PER_SEC;

	its.it_value.tv_sec = nsec / UM_NSEC_PER_SEC;
	its.it_value.tv_nsec = nsec;

	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 0; // we cheat here

	timer_settime(event_high_res_timer, 0, &its, NULL);
	return 0;
}

/**
 * os_timer_disable() - disable the posix (interval) timer
 * Returns the remaining interval timer time in nanoseconds
 */
long long os_timer_disable(void)
{
	struct itimerspec its;

	memset(&its, 0, sizeof(struct itimerspec));
	timer_settime(event_high_res_timer, 0, &its, &its);

	return its.it_value.tv_sec * UM_NSEC_PER_SEC + its.it_value.tv_nsec;
}

long long os_vnsecs(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&ts);
	return timespec_to_ns(&ts);
}

long long os_nsecs(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return timeval_to_ns(&tv);
}

#ifdef UML_CONFIG_NO_HZ
static int after_sleep_interval(struct timespec *ts)
{
	return 0;
}

static void deliver_alarm(void)
{
	alarm_handler(SIGVTALRM, NULL);
}

static unsigned long long sleep_time(unsigned long long nsecs)
{
	return nsecs;
}

#else
unsigned long long last_tick;
unsigned long long skew;

static void deliver_alarm(void)
{
	unsigned long long this_tick = os_nsecs();
	int one_tick = UM_NSEC_PER_SEC / UM_HZ;

	/* Protection against the host's time going backwards */
	if ((last_tick != 0) && (this_tick < last_tick))
		this_tick = last_tick;

	if (last_tick == 0)
		last_tick = this_tick - one_tick;

	skew += this_tick - last_tick;

	while (skew >= one_tick) {
		alarm_handler(SIGVTALRM, NULL);
		skew -= one_tick;
	}

	last_tick = this_tick;
}

static unsigned long long sleep_time(unsigned long long nsecs)
{
	return nsecs > skew ? nsecs - skew : 0;
}

static inline long long timespec_to_us(const struct timespec *ts)
{
	return ((long long) ts->tv_sec * UM_USEC_PER_SEC) +
		ts->tv_nsec / UM_NSEC_PER_USEC;
}

static int after_sleep_interval(struct timespec *ts)
{
	int usec = UM_USEC_PER_SEC / UM_HZ;
	long long start_usecs = timespec_to_us(ts);
	struct timeval tv;
	struct itimerval interval;

	/*
	 * It seems that rounding can increase the value returned from
	 * setitimer to larger than the one passed in.  Over time,
	 * this will cause the remaining time to be greater than the
	 * tick interval.  If this happens, then just reduce the first
	 * tick to the interval value.
	 */
	if (start_usecs > usec)
		start_usecs = usec;

	start_usecs -= skew / UM_NSEC_PER_USEC;
	if (start_usecs < 0)
		start_usecs = 0;

	tv = ((struct timeval) { .tv_sec  = start_usecs / UM_USEC_PER_SEC,
				 .tv_usec = start_usecs % UM_USEC_PER_SEC });
	interval = ((struct itimerval) { { 0, usec }, tv });

	if (setitimer(ITIMER_VIRTUAL, &interval, NULL) == -1)
		return -errno;

	return 0;
}
#endif

void idle_sleep(unsigned long long nsecs)
{
	struct timespec ts;

	/*
	 * nsecs can come in as zero, in which case, this starts a
	 * busy loop.  To prevent this, reset nsecs to the tick
	 * interval if it is zero.
	 */
	if (nsecs == 0)
		nsecs = UM_NSEC_PER_SEC / UM_HZ;

	nsecs = sleep_time(nsecs);
	ts = ((struct timespec) { .tv_sec	= nsecs / UM_NSEC_PER_SEC,
				  .tv_nsec	= nsecs % UM_NSEC_PER_SEC });

	if (nanosleep(&ts, &ts) == 0)
		deliver_alarm();
	after_sleep_interval(&ts);
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC,&ts);
	return timespec_to_ns(&ts);
}

/**
 * os_idle_sleep() - sleep for a given time of nsecs
 * @nsecs: nanoseconds to sleep
 */
void os_idle_sleep(unsigned long long nsecs)
{
	struct timespec ts;

	if (nsecs <= 0) {
		return;
	}

	ts = ((struct timespec) {
			.tv_sec  = nsecs / UM_NSEC_PER_SEC,
			.tv_nsec = nsecs % UM_NSEC_PER_SEC
	});

	/*
	 * Relay the signal if clock_nanosleep is interrupted.
	 */
	if (clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL)) {
		deliver_alarm();
	}
}
