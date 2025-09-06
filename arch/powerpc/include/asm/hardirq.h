#ifndef _ASM_POWERPC_HARDIRQ_H
#define _ASM_POWERPC_HARDIRQ_H
#ifdef __KERNEL__

#include <asm/irq.h>
#include <asm/bug.h>

/* The __last_jiffy_stamp field is needed to ensure that no decrementer
 * interrupt is lost on SMP machines. Since on most CPUs it is in the same
 * cache line as local_irq_count, it is cheap to access and is also used on UP
 * for uniformity.
 */
typedef struct {
	unsigned int __softirq_pending;	/* set_bit is used on this */
	unsigned int __last_jiffy_stamp;
} ____cacheline_aligned irq_cpustat_t;

#include <linux/irq_cpustat.h>	/* Standard mappings for irq_cpustat_t above */

#define last_jiffy_stamp(cpu) __IRQ_STAT((cpu), __last_jiffy_stamp)

static inline void ack_bad_irq(int irq)
{
	printk(KERN_CRIT "illegal vector %d received!\n", irq);
	BUG();
}

#endif /* __KERNEL__ */

#include <linux/threads.h>
#include <linux/irq.h>

typedef struct {
	unsigned int __softirq_pending;
	unsigned int timer_irqs_event;
	unsigned int timer_irqs_others;
	unsigned int pmu_irqs;
	unsigned int mce_exceptions;
	unsigned int spurious_irqs;
	unsigned int hmi_exceptions;
#ifdef CONFIG_PPC_DOORBELL
	unsigned int doorbell_irqs;
#endif
} ____cacheline_aligned irq_cpustat_t;

DECLARE_PER_CPU_SHARED_ALIGNED(irq_cpustat_t, irq_stat);

#define __ARCH_IRQ_STAT

#define local_softirq_pending()	__this_cpu_read(irq_stat.__softirq_pending)

#define __ARCH_SET_SOFTIRQ_PENDING

#define set_softirq_pending(x) __this_cpu_write(irq_stat.__softirq_pending, (x))
#define or_softirq_pending(x) __this_cpu_or(irq_stat.__softirq_pending, (x))

static inline void ack_bad_irq(unsigned int irq)
{
	printk(KERN_CRIT "unexpected IRQ trap at vector %02x\n", irq);
}

extern u64 arch_irq_stat_cpu(unsigned int cpu);
#define arch_irq_stat_cpu	arch_irq_stat_cpu

#endif /* _ASM_POWERPC_HARDIRQ_H */
