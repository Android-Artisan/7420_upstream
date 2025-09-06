/*  linux/include/linux/tick.h
 *
 *  This file contains the structure definitions for tick related functions
 *
/*
 * Tick related global functions
 */
#ifndef _LINUX_TICK_H
#define _LINUX_TICK_H

#include <linux/clockchips.h>

#ifdef CONFIG_GENERIC_CLOCKEVENTS

enum tick_device_mode {
	TICKDEV_MODE_PERIODIC,
	TICKDEV_MODE_ONESHOT,
};

struct tick_device {
	struct clock_event_device *evtdev;
	enum tick_device_mode mode;
};

enum tick_nohz_mode {
	NOHZ_MODE_INACTIVE,
	NOHZ_MODE_LOWRES,
	NOHZ_MODE_HIGHRES,
};

/**
 * struct tick_sched - sched tick emulation and no idle tick control/stats
 * @sched_timer:	hrtimer to schedule the periodic tick in high
 *			resolution mode
 * @idle_tick:		Store the last idle tick expiry time when the tick
 *			timer is modified for idle sleeps. This is necessary
 *			to resume the tick timer operation in the timeline
 *			when the CPU returns from idle
 * @tick_stopped:	Indicator that the idle tick has been stopped
 * @idle_jiffies:	jiffies at the entry to idle for idle time accounting
 * @idle_calls:		Total number of idle calls
 * @idle_sleeps:	Number of idle calls, where the sched tick was stopped
 * @idle_entrytime:	Time when the idle call was entered
 * @idle_waketime:	Time when the idle was interrupted
 * @idle_exittime:	Time when the idle state was left
 * @idle_sleeptime:	Sum of the time slept in idle with sched tick stopped
 * @sleep_length:	Duration of the current idle sleep
 */
struct tick_sched {
	struct hrtimer			sched_timer;
	unsigned long			check_clocks;
	enum tick_nohz_mode		nohz_mode;
	ktime_t				idle_tick;
	int				inidle;
	int				tick_stopped;
	unsigned long			idle_jiffies;
	unsigned long			idle_calls;
	unsigned long			idle_sleeps;
	int				idle_active;
	ktime_t				idle_entrytime;
	ktime_t				idle_waketime;
	ktime_t				idle_exittime;
	ktime_t				idle_sleeptime;
	ktime_t				idle_lastupdate;
	ktime_t				sleep_length;
	unsigned long			last_jiffies;
	unsigned long			next_jiffies;
	ktime_t				idle_expires;
};

extern void __init tick_init(void);
extern int tick_is_oneshot_available(void);
extern struct tick_device *tick_get_device(int cpu);

# ifdef CONFIG_HIGH_RES_TIMERS
extern int tick_init_highres(void);
extern int tick_program_event(ktime_t expires, int force);
extern void tick_setup_sched_timer(void);
# endif

# if defined CONFIG_NO_HZ || defined CONFIG_HIGH_RES_TIMERS
extern void tick_cancel_sched_timer(int cpu);
# else
static inline void tick_cancel_sched_timer(int cpu) { }
# endif

# ifdef CONFIG_GENERIC_CLOCKEVENTS_BROADCAST
extern struct tick_device *tick_get_broadcast_device(void);
extern cpumask_t *tick_get_broadcast_mask(void);

#  ifdef CONFIG_TICK_ONESHOT
extern cpumask_t *tick_get_broadcast_oneshot_mask(void);
#  endif

# endif /* BROADCAST */

# ifdef CONFIG_TICK_ONESHOT
extern void tick_clock_notify(void);
extern int tick_check_oneshot_change(int allow_nohz);
extern struct tick_sched *tick_get_tick_sched(int cpu);
# else
static inline void tick_clock_notify(void) { }
static inline int tick_check_oneshot_change(int allow_nohz) { return 0; }
# endif

#else /* CONFIG_GENERIC_CLOCKEVENTS */
static inline void tick_init(void) { }
static inline void tick_cancel_sched_timer(int cpu) { }
static inline void tick_clock_notify(void) { }
static inline int tick_check_oneshot_change(int allow_nohz) { return 0; }
#endif /* !CONFIG_GENERIC_CLOCKEVENTS */

# ifdef CONFIG_NO_HZ
extern void tick_nohz_stop_sched_tick(int inidle);
extern void tick_nohz_restart_sched_tick(void);
extern void tick_nohz_update_jiffies(void);
extern ktime_t tick_nohz_get_sleep_length(void);
extern void tick_nohz_stop_idle(int cpu);
extern u64 get_cpu_idle_time_us(int cpu, u64 *last_update_time);
# else
static inline void tick_nohz_stop_sched_tick(int inidle) { }
static inline void tick_nohz_restart_sched_tick(void) { }
static inline void tick_nohz_update_jiffies(void) { }
#include <linux/irqflags.h>
#include <linux/percpu.h>
#include <linux/context_tracking_state.h>
#include <linux/cpumask.h>
#include <linux/sched.h>

#ifdef CONFIG_GENERIC_CLOCKEVENTS
extern void __init tick_init(void);
/* Should be core only, but ARM BL switcher requires it */
extern void tick_suspend_local(void);
/* Should be core only, but XEN resume magic and ARM BL switcher require it */
extern void tick_resume_local(void);
extern void tick_handover_do_timer(void);
extern void tick_cleanup_dead_cpu(int cpu);
#else /* CONFIG_GENERIC_CLOCKEVENTS */
static inline void tick_init(void) { }
static inline void tick_suspend_local(void) { }
static inline void tick_resume_local(void) { }
static inline void tick_handover_do_timer(void) { }
static inline void tick_cleanup_dead_cpu(int cpu) { }
#endif /* !CONFIG_GENERIC_CLOCKEVENTS */

#if defined(CONFIG_GENERIC_CLOCKEVENTS) && defined(CONFIG_SUSPEND)
extern void tick_freeze(void);
extern void tick_unfreeze(void);
#else
static inline void tick_freeze(void) { }
static inline void tick_unfreeze(void) { }
#endif

#ifdef CONFIG_TICK_ONESHOT
extern void tick_irq_enter(void);
#  ifndef arch_needs_cpu
#   define arch_needs_cpu() (0)
#  endif
# else
static inline void tick_irq_enter(void) { }
#endif

#if defined(CONFIG_GENERIC_CLOCKEVENTS_BROADCAST) && defined(CONFIG_TICK_ONESHOT)
extern void hotplug_cpu__broadcast_tick_pull(int dead_cpu);
#else
static inline void hotplug_cpu__broadcast_tick_pull(int dead_cpu) { }
#endif

enum tick_broadcast_mode {
	TICK_BROADCAST_OFF,
	TICK_BROADCAST_ON,
	TICK_BROADCAST_FORCE,
};

enum tick_broadcast_state {
	TICK_BROADCAST_EXIT,
	TICK_BROADCAST_ENTER,
};

#ifdef CONFIG_GENERIC_CLOCKEVENTS_BROADCAST
extern void tick_broadcast_control(enum tick_broadcast_mode mode);
#else
static inline void tick_broadcast_control(enum tick_broadcast_mode mode) { }
#endif /* BROADCAST */

#ifdef CONFIG_GENERIC_CLOCKEVENTS
extern int tick_broadcast_oneshot_control(enum tick_broadcast_state state);
#else
static inline int tick_broadcast_oneshot_control(enum tick_broadcast_state state)
{
	return 0;
}
#endif

static inline void tick_broadcast_enable(void)
{
	tick_broadcast_control(TICK_BROADCAST_ON);
}
static inline void tick_broadcast_disable(void)
{
	tick_broadcast_control(TICK_BROADCAST_OFF);
}
static inline void tick_broadcast_force(void)
{
	tick_broadcast_control(TICK_BROADCAST_FORCE);
}
static inline int tick_broadcast_enter(void)
{
	return tick_broadcast_oneshot_control(TICK_BROADCAST_ENTER);
}
static inline void tick_broadcast_exit(void)
{
	tick_broadcast_oneshot_control(TICK_BROADCAST_EXIT);
}

#ifdef CONFIG_NO_HZ_COMMON
extern int tick_nohz_tick_stopped(void);
extern void tick_nohz_idle_enter(void);
extern void tick_nohz_idle_exit(void);
extern void tick_nohz_irq_exit(void);
extern ktime_t tick_nohz_get_sleep_length(void);
extern u64 get_cpu_idle_time_us(int cpu, u64 *last_update_time);
extern u64 get_cpu_iowait_time_us(int cpu, u64 *last_update_time);
#else /* !CONFIG_NO_HZ_COMMON */
static inline int tick_nohz_tick_stopped(void) { return 0; }
static inline void tick_nohz_idle_enter(void) { }
static inline void tick_nohz_idle_exit(void) { }

static inline ktime_t tick_nohz_get_sleep_length(void)
{
	ktime_t len = { .tv64 = NSEC_PER_SEC/HZ };

	return len;
}
static inline void tick_nohz_stop_idle(int cpu) { }
static inline u64 get_cpu_idle_time_us(int cpu, u64 *unused) { return 0; }
# endif /* !NO_HZ */
static inline u64 get_cpu_idle_time_us(int cpu, u64 *unused) { return -1; }
static inline u64 get_cpu_iowait_time_us(int cpu, u64 *unused) { return -1; }
#endif /* !CONFIG_NO_HZ_COMMON */

#ifdef CONFIG_NO_HZ_FULL
extern bool tick_nohz_full_running;
extern cpumask_var_t tick_nohz_full_mask;
extern cpumask_var_t housekeeping_mask;

static inline bool tick_nohz_full_enabled(void)
{
	if (!context_tracking_is_enabled())
		return false;

	return tick_nohz_full_running;
}

static inline bool tick_nohz_full_cpu(int cpu)
{
	if (!tick_nohz_full_enabled())
		return false;

	return cpumask_test_cpu(cpu, tick_nohz_full_mask);
}

static inline void tick_nohz_full_add_cpus_to(struct cpumask *mask)
{
	if (tick_nohz_full_enabled())
		cpumask_or(mask, mask, tick_nohz_full_mask);
}

static inline int housekeeping_any_cpu(void)
{
	return cpumask_any_and(housekeeping_mask, cpu_online_mask);
}

extern void tick_nohz_full_kick(void);
extern void tick_nohz_full_kick_cpu(int cpu);
extern void tick_nohz_full_kick_all(void);
extern void __tick_nohz_task_switch(void);
#else
static inline int housekeeping_any_cpu(void)
{
	return smp_processor_id();
}
static inline bool tick_nohz_full_enabled(void) { return false; }
static inline bool tick_nohz_full_cpu(int cpu) { return false; }
static inline void tick_nohz_full_add_cpus_to(struct cpumask *mask) { }
static inline void tick_nohz_full_kick_cpu(int cpu) { }
static inline void tick_nohz_full_kick(void) { }
static inline void tick_nohz_full_kick_all(void) { }
static inline void __tick_nohz_task_switch(void) { }
#endif

static inline const struct cpumask *housekeeping_cpumask(void)
{
#ifdef CONFIG_NO_HZ_FULL
	if (tick_nohz_full_enabled())
		return housekeeping_mask;
#endif
	return cpu_possible_mask;
}

static inline bool is_housekeeping_cpu(int cpu)
{
#ifdef CONFIG_NO_HZ_FULL
	if (tick_nohz_full_enabled())
		return cpumask_test_cpu(cpu, housekeeping_mask);
#endif
	return true;
}

static inline void housekeeping_affine(struct task_struct *t)
{
#ifdef CONFIG_NO_HZ_FULL
	if (tick_nohz_full_enabled())
		set_cpus_allowed_ptr(t, housekeeping_mask);

#endif
}

static inline void tick_nohz_task_switch(void)
{
	if (tick_nohz_full_enabled())
		__tick_nohz_task_switch();
}

#endif
