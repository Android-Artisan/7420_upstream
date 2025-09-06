/*
 *  arch/arm/include/asm/smp.h
 *
 *  Copyright (C) 2004-2005 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_ARM_SMP_H
#define __ASM_ARM_SMP_H

#include <linux/threads.h>
#include <linux/cpumask.h>
#include <linux/thread_info.h>

#include <mach/smp.h>

#ifndef CONFIG_SMP
# error "<asm/smp.h> included in non-SMP build"
#endif

#define raw_smp_processor_id() (current_thread_info()->cpu)

/*
 * at the moment, there's not a big penalty for changing CPUs
 * (the >big< penalty is running SMP in the first place)
 */
#define PROC_CHANGE_PENALTY		15

struct seq_file;

/*
 * generate IPI list text
 */
extern void show_ipi_list(struct seq_file *p);
extern void show_ipi_list(struct seq_file *, int);

/*
 * Called from assembly code, this handles an IPI.
 */
asmlinkage void do_IPI(struct pt_regs *regs);

/*
 * Setup the SMP cpu_possible_map
 */
extern void smp_init_cpus(void);

/*
 * Move global data into per-processor storage.
 */
extern void smp_store_cpu_info(unsigned int cpuid);

/*
 * Raise an IPI cross call on CPUs in callmap.
 */
extern void smp_cross_call(cpumask_t callmap);

/*
 * Broadcast a timer interrupt to the other CPUs.
 */
extern void smp_send_timer(void);

/*
 * Broadcast a clock event to other CPUs.
 */
extern void smp_timer_broadcast(cpumask_t mask);

/*
 * Boot a secondary CPU, and assign it the specified idle task.
 * This also gives us the initial stack to use for this CPU.
 */
extern int boot_secondary(unsigned int cpu, struct task_struct *);
asmlinkage void do_IPI(int ipinr, struct pt_regs *regs);

/*
 * Called from C code, this handles an IPI.
 */
void handle_IPI(int ipinr, struct pt_regs *regs);

/*
 * Setup the set of possible CPUs (via set_cpu_possible)
 */
extern void smp_init_cpus(void);


/*
 * Provide a function to raise an IPI cross call on CPUs in callmap.
 */
extern void set_smp_cross_call(void (*)(const struct cpumask *, unsigned int));

/*
 * Called from platform specific assembly code, this is the
 * secondary CPU entry point.
 */
asmlinkage void secondary_start_kernel(void);

/*
 * Perform platform specific initialisation of the specified CPU.
 */
extern void platform_secondary_init(unsigned int cpu);

/*
 * Initial data for bringing up a secondary CPU.
 */
struct secondary_data {
	unsigned long pgdir;
	void *stack;
};
extern struct secondary_data secondary_data;

extern int __cpu_disable(void);
extern int mach_cpu_disable(unsigned int cpu);

extern void __cpu_die(unsigned int cpu);
extern void cpu_die(void);

extern void platform_cpu_die(unsigned int cpu);
extern int platform_cpu_kill(unsigned int cpu);
extern void platform_cpu_enable(unsigned int cpu);

extern void arch_send_call_function_single_ipi(int cpu);
extern void arch_send_call_function_ipi(cpumask_t mask);

/*
 * Local timer interrupt handling function (can be IPI'ed).
 */
extern void local_timer_interrupt(void);

#ifdef CONFIG_LOCAL_TIMERS

/*
 * Stop a local timer interrupt.
 */
extern void local_timer_stop(unsigned int cpu);

/*
 * Platform provides this to acknowledge a local timer IRQ
 */
extern int local_timer_ack(void);

#else

static inline void local_timer_stop(unsigned int cpu)
{
}

#endif

/*
 * Setup a local timer interrupt for a CPU.
 */
extern void local_timer_setup(unsigned int cpu);

/*
 * show local interrupt info
 */
extern void show_local_irqs(struct seq_file *);

/*
 * Called from assembly, this is the local timer IRQ handler
 */
asmlinkage void do_local_timer(struct pt_regs *);
	union {
		unsigned long mpu_rgn_szr;
		u64 pgdir;
	};
	unsigned long swapper_pg_dir;
	void *stack;
};
extern struct secondary_data secondary_data;
extern volatile int pen_release;
extern void secondary_startup(void);
extern void secondary_startup_arm(void);

extern int __cpu_disable(void);

extern void __cpu_die(unsigned int cpu);

extern void arch_send_call_function_single_ipi(int cpu);
extern void arch_send_call_function_ipi_mask(const struct cpumask *mask);
extern void arch_send_wakeup_ipi_mask(const struct cpumask *mask);

extern int register_ipi_completion(struct completion *completion, int cpu);

struct smp_operations {
#ifdef CONFIG_SMP
	/*
	 * Setup the set of possible CPUs (via set_cpu_possible)
	 */
	void (*smp_init_cpus)(void);
	/*
	 * Initialize cpu_possible map, and enable coherency
	 */
	void (*smp_prepare_cpus)(unsigned int max_cpus);

	/*
	 * Perform platform specific initialisation of the specified CPU.
	 */
	void (*smp_secondary_init)(unsigned int cpu);
	/*
	 * Boot a secondary CPU, and assign it the specified idle task.
	 * This also gives us the initial stack to use for this CPU.
	 */
	int  (*smp_boot_secondary)(unsigned int cpu, struct task_struct *idle);
#ifdef CONFIG_HOTPLUG_CPU
	int  (*cpu_kill)(unsigned int cpu);
	void (*cpu_die)(unsigned int cpu);
	bool  (*cpu_can_disable)(unsigned int cpu);
	int  (*cpu_disable)(unsigned int cpu);
#endif
#endif
};

struct of_cpu_method {
	const char *method;
	const struct smp_operations *ops;
};

#define CPU_METHOD_OF_DECLARE(name, _method, _ops)			\
	static const struct of_cpu_method __cpu_method_of_table_##name	\
		__used __section(__cpu_method_of_table)			\
		= { .method = _method, .ops = _ops }
/*
 * set platform specific SMP operations
 */
extern void smp_set_ops(const struct smp_operations *);

#endif /* ifndef __ASM_ARM_SMP_H */
