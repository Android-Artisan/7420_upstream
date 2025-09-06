#ifndef _LINUX_KERNEL_STAT_H
#define _LINUX_KERNEL_STAT_H

#include <linux/smp.h>
#include <linux/threads.h>
#include <linux/percpu.h>
#include <linux/cpumask.h>
#include <asm/irq.h>
#include <asm/cputime.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/vtime.h>
#include <asm/irq.h>
#include <linux/cputime.h>

/*
 * 'kernel_stat.h' contains the definitions needed for doing
 * some kernel statistics (CPU usage, context switches ...),
 * used by rstatd/perfmeter
 */

struct cpu_usage_stat {
	cputime64_t user;
	cputime64_t nice;
	cputime64_t system;
	cputime64_t softirq;
	cputime64_t irq;
	cputime64_t idle;
	cputime64_t iowait;
	cputime64_t steal;
	cputime64_t guest;
};

struct kernel_stat {
	struct cpu_usage_stat	cpustat;
	unsigned int irqs[NR_IRQS];
};

DECLARE_PER_CPU(struct kernel_stat, kstat);

#define kstat_cpu(cpu)	per_cpu(kstat, cpu)
/* Must have preemption disabled for this to be meaningful. */
#define kstat_this_cpu	__get_cpu_var(kstat)

extern unsigned long long nr_context_switches(void);

/*
 * Number of interrupts per specific IRQ source, since bootup
 */
static inline int kstat_irqs(int irq)
{
	int cpu, sum = 0;

	for_each_possible_cpu(cpu)
		sum += kstat_cpu(cpu).irqs[irq];

	return sum;
}

extern void account_user_time(struct task_struct *, cputime_t);
extern void account_user_time_scaled(struct task_struct *, cputime_t);
extern void account_system_time(struct task_struct *, int, cputime_t);
extern void account_system_time_scaled(struct task_struct *, cputime_t);
extern void account_steal_time(struct task_struct *, cputime_t);
enum cpu_usage_stat {
	CPUTIME_USER,
	CPUTIME_NICE,
	CPUTIME_SYSTEM,
	CPUTIME_SOFTIRQ,
	CPUTIME_IRQ,
	CPUTIME_IDLE,
	CPUTIME_IOWAIT,
	CPUTIME_STEAL,
	CPUTIME_GUEST,
	CPUTIME_GUEST_NICE,
	NR_STATS,
};

struct kernel_cpustat {
	u64 cpustat[NR_STATS];
};

struct kernel_stat {
	unsigned long irqs_sum;
	unsigned int softirqs[NR_SOFTIRQS];
};

DECLARE_PER_CPU(struct kernel_stat, kstat);
DECLARE_PER_CPU(struct kernel_cpustat, kernel_cpustat);

/* Must have preemption disabled for this to be meaningful. */
#define kstat_this_cpu this_cpu_ptr(&kstat)
#define kcpustat_this_cpu this_cpu_ptr(&kernel_cpustat)
#define kstat_cpu(cpu) per_cpu(kstat, cpu)
#define kcpustat_cpu(cpu) per_cpu(kernel_cpustat, cpu)

extern unsigned long long nr_context_switches(void);

extern unsigned int kstat_irqs_cpu(unsigned int irq, int cpu);
extern void kstat_incr_irq_this_cpu(unsigned int irq);

static inline void kstat_incr_softirqs_this_cpu(unsigned int irq)
{
	__this_cpu_inc(kstat.softirqs[irq]);
}

static inline unsigned int kstat_softirqs_cpu(unsigned int irq, int cpu)
{
       return kstat_cpu(cpu).softirqs[irq];
}

/*
 * Number of interrupts per specific IRQ source, since bootup
 */
extern unsigned int kstat_irqs(unsigned int irq);
extern unsigned int kstat_irqs_usr(unsigned int irq);

/*
 * Number of interrupts per cpu, since bootup
 */
static inline unsigned long kstat_cpu_irqs_sum(unsigned int cpu)
{
	return kstat_cpu(cpu).irqs_sum;
}

extern void account_user_time(struct task_struct *, cputime_t, cputime_t);
extern void account_system_time(struct task_struct *, int, cputime_t, cputime_t);
extern void account_steal_time(cputime_t);
extern void account_idle_time(cputime_t);

#ifdef CONFIG_VIRT_CPU_ACCOUNTING_NATIVE
static inline void account_process_tick(struct task_struct *tsk, int user)
{
	vtime_account_user(tsk);
}
#else
extern void account_process_tick(struct task_struct *, int user);
#endif

extern void account_steal_ticks(unsigned long ticks);
extern void account_idle_ticks(unsigned long ticks);

#endif /* _LINUX_KERNEL_STAT_H */
