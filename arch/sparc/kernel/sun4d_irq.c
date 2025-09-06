/*
 *  arch/sparc/kernel/sun4d_irq.c:
 *			SS1000/SC2000 interrupt handling.
 * SS1000/SC2000 interrupt handling.
 *
 *  Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 *  Heavily based on arch/sparc/kernel/irq.c.
 */

#include <linux/errno.h>
#include <linux/linkage.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/seq_file.h>

#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/psr.h>
#include <asm/smp.h>
#include <asm/vaddrs.h>
#include <asm/timer.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/traps.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/sbus.h>
#include <asm/sbi.h>
#include <asm/cacheflush.h>
#include <asm/irq_regs.h>

#include "irq.h"

/* If you trust current SCSI layer to handle different SCSI IRQs, enable this. I don't trust it... -jj */
/* #define DISTRIBUTE_IRQS */

struct sun4d_timer_regs *sun4d_timers;
#define TIMER_IRQ	10

#define MAX_STATIC_ALLOC	4
extern struct irqaction static_irqaction[MAX_STATIC_ALLOC];
extern int static_irq_count;
unsigned char cpu_leds[32];
#ifdef CONFIG_SMP
static unsigned char sbus_tid[32];
#endif

static struct irqaction *irq_action[NR_IRQS];
extern spinlock_t irq_action_lock;

static struct sbus_action {
	struct irqaction *action;
	/* For SMP this needs to be extended */
} *sbus_actions;

static int pil_to_sbus[] = {
	0, 0, 1, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 7, 0, 0,
};

static int sbus_to_pil[] = {
	0, 2, 3, 5, 7, 9, 11, 13,
};

static int nsbi;
#ifdef CONFIG_SMP
DEFINE_SPINLOCK(sun4d_imsk_lock);
#endif

int show_sun4d_interrupts(struct seq_file *p, void *v)
{
	int i = *(loff_t *) v, j = 0, k = 0, sbusl;
	struct irqaction * action;
	unsigned long flags;
#ifdef CONFIG_SMP
	int x;
#endif

	spin_lock_irqsave(&irq_action_lock, flags);
	if (i < NR_IRQS) {
		sbusl = pil_to_sbus[i];
		if (!sbusl) {
	 		action = *(i + irq_action);
			if (!action) 
		        	goto out_unlock;
		} else {
			for (j = 0; j < nsbi; j++) {
				for (k = 0; k < 4; k++)
					if ((action = sbus_actions [(j << 5) + (sbusl << 2) + k].action))
						goto found_it;
			}
			goto out_unlock;
		}
found_it:	seq_printf(p, "%3d: ", i);
#ifndef CONFIG_SMP
		seq_printf(p, "%10u ", kstat_irqs(i));
#else
		for_each_online_cpu(x)
			seq_printf(p, "%10u ",
			       kstat_cpu(cpu_logical_map(x)).irqs[i]);
#endif
		seq_printf(p, "%c %s",
			(action->flags & IRQF_DISABLED) ? '+' : ' ',
			action->name);
		action = action->next;
		for (;;) {
			for (; action; action = action->next) {
				seq_printf(p, ",%s %s",
					(action->flags & IRQF_DISABLED) ? " +" : "",
					action->name);
			}
			if (!sbusl) break;
			k++;
			if (k < 4)
				action = sbus_actions [(j << 5) + (sbusl << 2) + k].action;
			else {
				j++;
				if (j == nsbi) break;
				k = 0;
				action = sbus_actions [(j << 5) + (sbusl << 2)].action;
			}
		}
		seq_putc(p, '\n');
	}
out_unlock:
	spin_unlock_irqrestore(&irq_action_lock, flags);
	return 0;
}

void sun4d_free_irq(unsigned int irq, void *dev_id)
{
	struct irqaction *action, **actionp;
	struct irqaction *tmp = NULL;
        unsigned long flags;

	spin_lock_irqsave(&irq_action_lock, flags);
	if (irq < 15)
		actionp = irq + irq_action;
	else
		actionp = &(sbus_actions[irq - (1 << 5)].action);
	action = *actionp;
	if (!action) {
		printk("Trying to free free IRQ%d\n",irq);
		goto out_unlock;
	}
	if (dev_id) {
		for (; action; action = action->next) {
			if (action->dev_id == dev_id)
				break;
			tmp = action;
		}
		if (!action) {
			printk("Trying to free free shared IRQ%d\n",irq);
			goto out_unlock;
		}
	} else if (action->flags & IRQF_SHARED) {
		printk("Trying to free shared IRQ%d with NULL device ID\n", irq);
		goto out_unlock;
	}
	if (action->flags & SA_STATIC_ALLOC)
	{
		/* This interrupt is marked as specially allocated
		 * so it is a bad idea to free it.
		 */
		printk("Attempt to free statically allocated IRQ%d (%s)\n",
		       irq, action->name);
		goto out_unlock;
	}
	
	if (action && tmp)
		tmp->next = action->next;
	else
		*actionp = action->next;

	spin_unlock_irqrestore(&irq_action_lock, flags);

	synchronize_irq(irq);

	spin_lock_irqsave(&irq_action_lock, flags);

	kfree(action);

	if (!(*actionp))
		__disable_irq(irq);

out_unlock:
	spin_unlock_irqrestore(&irq_action_lock, flags);
}

extern void unexpected_irq(int, void *, struct pt_regs *);

void sun4d_handler_irq(int irq, struct pt_regs * regs)
{
	struct pt_regs *old_regs;
	struct irqaction * action;
	int cpu = smp_processor_id();
	/* SBUS IRQ level (1 - 7) */
	int sbusl = pil_to_sbus[irq];
	
	/* FIXME: Is this necessary?? */
	cc_get_ipen();
	
	cc_set_iclr(1 << irq);
	
	old_regs = set_irq_regs(regs);
	irq_enter();
	kstat_cpu(cpu).irqs[irq]++;
	if (!sbusl) {
		action = *(irq + irq_action);
		if (!action)
			unexpected_irq(irq, NULL, regs);
		do {
			action->handler(irq, action->dev_id);
			action = action->next;
		} while (action);
	} else {
		int bus_mask = bw_get_intr_mask(sbusl) & 0x3ffff;
		int sbino;
		struct sbus_action *actionp;
		unsigned mask, slot;
		int sbil = (sbusl << 2);
		
		bw_clear_intr_mask(sbusl, bus_mask);
		
		/* Loop for each pending SBI */
		for (sbino = 0; bus_mask; sbino++, bus_mask >>= 1)
			if (bus_mask & 1) {
				mask = acquire_sbi(SBI2DEVID(sbino), 0xf << sbil);
				mask &= (0xf << sbil);
				actionp = sbus_actions + (sbino << 5) + (sbil);
				/* Loop for each pending SBI slot */
				for (slot = (1 << sbil); mask; slot <<= 1, actionp++)
					if (mask & slot) {
						mask &= ~slot;
						action = actionp->action;
						
						if (!action)
							unexpected_irq(irq, NULL, regs);
						do {
							action->handler(irq, action->dev_id);
							action = action->next;
						} while (action);
						release_sbi(SBI2DEVID(sbino), slot);
					}
			}
#include <linux/kernel_stat.h>
#include <linux/slab.h>
#include <linux/seq_file.h>

#include <asm/timer.h>
#include <asm/traps.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/sbi.h>
#include <asm/cacheflush.h>
#include <asm/setup.h>
#include <asm/oplib.h>

#include "kernel.h"
#include "irq.h"

/* Sun4d interrupts fall roughly into two categories.  SBUS and
 * cpu local.  CPU local interrupts cover the timer interrupts
 * and whatnot, and we encode those as normal PILs between
 * 0 and 15.
 * SBUS interrupts are encodes as a combination of board, level and slot.
 */

struct sun4d_handler_data {
	unsigned int cpuid;    /* target cpu */
	unsigned int real_irq; /* interrupt level */
};


static unsigned int sun4d_encode_irq(int board, int lvl, int slot)
{
	return (board + 1) << 5 | (lvl << 2) | slot;
}

struct sun4d_timer_regs {
	u32	l10_timer_limit;
	u32	l10_cur_countx;
	u32	l10_limit_noclear;
	u32	ctrl;
	u32	l10_cur_count;
};

static struct sun4d_timer_regs __iomem *sun4d_timers;

#define SUN4D_TIMER_IRQ        10

/* Specify which cpu handle interrupts from which board.
 * Index is board - value is cpu.
 */
static unsigned char board_to_cpu[32];

static int pil_to_sbus[] = {
	0,
	0,
	1,
	2,
	0,
	3,
	0,
	4,
	0,
	5,
	0,
	6,
	0,
	7,
	0,
	0,
};

/* Exported for sun4d_smp.c */
DEFINE_SPINLOCK(sun4d_imsk_lock);

/* SBUS interrupts are encoded integers including the board number
 * (plus one), the SBUS level, and the SBUS slot number.  Sun4D
 * IRQ dispatch is done by:
 *
 * 1) Reading the BW local interrupt table in order to get the bus
 *    interrupt mask.
 *
 *    This table is indexed by SBUS interrupt level which can be
 *    derived from the PIL we got interrupted on.
 *
 * 2) For each bus showing interrupt pending from #1, read the
 *    SBI interrupt state register.  This will indicate which slots
 *    have interrupts pending for that SBUS interrupt level.
 *
 * 3) Call the genreric IRQ support.
 */
static void sun4d_sbus_handler_irq(int sbusl)
{
	unsigned int bus_mask;
	unsigned int sbino, slot;
	unsigned int sbil;

	bus_mask = bw_get_intr_mask(sbusl) & 0x3ffff;
	bw_clear_intr_mask(sbusl, bus_mask);

	sbil = (sbusl << 2);
	/* Loop for each pending SBI */
	for (sbino = 0; bus_mask; sbino++, bus_mask >>= 1) {
		unsigned int idx, mask;

		if (!(bus_mask & 1))
			continue;
		/* XXX This seems to ACK the irq twice.  acquire_sbi()
		 * XXX uses swap, therefore this writes 0xf << sbil,
		 * XXX then later release_sbi() will write the individual
		 * XXX bits which were set again.
		 */
		mask = acquire_sbi(SBI2DEVID(sbino), 0xf << sbil);
		mask &= (0xf << sbil);

		/* Loop for each pending SBI slot */
		slot = (1 << sbil);
		for (idx = 0; mask != 0; idx++, slot <<= 1) {
			unsigned int pil;
			struct irq_bucket *p;

			if (!(mask & slot))
				continue;

			mask &= ~slot;
			pil = sun4d_encode_irq(sbino, sbusl, idx);

			p = irq_map[pil];
			while (p) {
				struct irq_bucket *next;

				next = p->next;
				generic_handle_irq(p->irq);
				p = next;
			}
			release_sbi(SBI2DEVID(sbino), slot);
		}
	}
}

void sun4d_handler_irq(unsigned int pil, struct pt_regs *regs)
{
	struct pt_regs *old_regs;
	/* SBUS IRQ level (1 - 7) */
	int sbusl = pil_to_sbus[pil];

	/* FIXME: Is this necessary?? */
	cc_get_ipen();

	cc_set_iclr(1 << pil);

#ifdef CONFIG_SMP
	/*
	 * Check IPI data structures after IRQ has been cleared. Hard and Soft
	 * IRQ can happen at the same time, so both cases are always handled.
	 */
	if (pil == SUN4D_IPI_IRQ)
		sun4d_ipi_interrupt();
#endif

	old_regs = set_irq_regs(regs);
	irq_enter();
	if (sbusl == 0) {
		/* cpu interrupt */
		struct irq_bucket *p;

		p = irq_map[pil];
		while (p) {
			struct irq_bucket *next;

			next = p->next;
			generic_handle_irq(p->irq);
			p = next;
		}
	} else {
		/* SBUS interrupt */
		sun4d_sbus_handler_irq(sbusl);
	}
	irq_exit();
	set_irq_regs(old_regs);
}

unsigned int sun4d_build_irq(struct sbus_dev *sdev, int irq)
{
	int sbusl = pil_to_sbus[irq];

	if (sbusl)
		return ((sdev->bus->board + 1) << 5) + (sbusl << 2) + sdev->slot;
	else
		return irq;
}

static unsigned int sun4d_sbint_to_irq(struct sbus_dev *sdev,
				       unsigned int sbint)
{
	if (sbint >= sizeof(sbus_to_pil)) {
		printk(KERN_ERR "%s: bogus SBINT %d\n", sdev->prom_name, sbint);
		BUG();
	}
	return sun4d_build_irq(sdev, sbus_to_pil[sbint]);
}

int sun4d_request_irq(unsigned int irq,
		irq_handler_t handler,
		unsigned long irqflags, const char * devname, void *dev_id)
{
	struct irqaction *action, *tmp = NULL, **actionp;
	unsigned long flags;
	int ret;
	
	if(irq > 14 && irq < (1 << 5)) {
		ret = -EINVAL;
		goto out;
	}

	if (!handler) {
		ret = -EINVAL;
		goto out;
	}

	spin_lock_irqsave(&irq_action_lock, flags);

	if (irq >= (1 << 5))
		actionp = &(sbus_actions[irq - (1 << 5)].action);
	else
		actionp = irq + irq_action;
	action = *actionp;
	
	if (action) {
		if ((action->flags & IRQF_SHARED) && (irqflags & IRQF_SHARED)) {
			for (tmp = action; tmp->next; tmp = tmp->next);
		} else {
			ret = -EBUSY;
			goto out_unlock;
		}
		if ((action->flags & IRQF_DISABLED) ^ (irqflags & IRQF_DISABLED)) {
			printk("Attempt to mix fast and slow interrupts on IRQ%d denied\n", irq);
			ret = -EBUSY;
			goto out_unlock;
		}
		action = NULL;		/* Or else! */
	}

	/* If this is flagged as statically allocated then we use our
	 * private struct which is never freed.
	 */
	if (irqflags & SA_STATIC_ALLOC) {
		if (static_irq_count < MAX_STATIC_ALLOC)
			action = &static_irqaction[static_irq_count++];
		else
			printk("Request for IRQ%d (%s) SA_STATIC_ALLOC failed using kmalloc\n", irq, devname);
	}
	
	if (action == NULL)
		action = kmalloc(sizeof(struct irqaction),
						     GFP_ATOMIC);
	
	if (!action) { 
		ret = -ENOMEM;
		goto out_unlock;
	}

	action->handler = handler;
	action->flags = irqflags;
	cpus_clear(action->mask);
	action->name = devname;
	action->next = NULL;
	action->dev_id = dev_id;

	if (tmp)
		tmp->next = action;
	else
		*actionp = action;
		
	__enable_irq(irq);

	ret = 0;
out_unlock:
	spin_unlock_irqrestore(&irq_action_lock, flags);
out:
	return ret;
}

static void sun4d_disable_irq(unsigned int irq)
{
#ifdef CONFIG_SMP
	int tid = sbus_tid[(irq >> 5) - 1];
	unsigned long flags;
#endif	
	
	if (irq < NR_IRQS) return;
#ifdef CONFIG_SMP
	spin_lock_irqsave(&sun4d_imsk_lock, flags);
	cc_set_imsk_other(tid, cc_get_imsk_other(tid) | (1 << sbus_to_pil[(irq >> 2) & 7]));
	spin_unlock_irqrestore(&sun4d_imsk_lock, flags);
#else		
	cc_set_imsk(cc_get_imsk() | (1 << sbus_to_pil[(irq >> 2) & 7]));
#endif
}

static void sun4d_enable_irq(unsigned int irq)
{
#ifdef CONFIG_SMP
	int tid = sbus_tid[(irq >> 5) - 1];
	unsigned long flags;
#endif	
	
	if (irq < NR_IRQS) return;
#ifdef CONFIG_SMP
	spin_lock_irqsave(&sun4d_imsk_lock, flags);
	cc_set_imsk_other(tid, cc_get_imsk_other(tid) & ~(1 << sbus_to_pil[(irq >> 2) & 7]));
	spin_unlock_irqrestore(&sun4d_imsk_lock, flags);
#else		
	cc_set_imsk(cc_get_imsk() & ~(1 << sbus_to_pil[(irq >> 2) & 7]));
#endif
}

#ifdef CONFIG_SMP
static void sun4d_set_cpu_int(int cpu, int level)
{
	sun4d_send_ipi(cpu, level);
}

static void sun4d_clear_ipi(int cpu, int level)
{
}

static void sun4d_set_udt(int cpu)
{
}

/* Setup IRQ distribution scheme. */
void __init sun4d_distribute_irqs(void)
{
#ifdef DISTRIBUTE_IRQS
	struct sbus_bus *sbus;
	unsigned long sbus_serving_map;

	sbus_serving_map = cpu_present_map;
	for_each_sbus(sbus) {
		if ((sbus->board * 2) == boot_cpu_id && (cpu_present_map & (1 << (sbus->board * 2 + 1))))
			sbus_tid[sbus->board] = (sbus->board * 2 + 1);
		else if (cpu_present_map & (1 << (sbus->board * 2)))
			sbus_tid[sbus->board] = (sbus->board * 2);
		else if (cpu_present_map & (1 << (sbus->board * 2 + 1)))
			sbus_tid[sbus->board] = (sbus->board * 2 + 1);
		else
			sbus_tid[sbus->board] = 0xff;
		if (sbus_tid[sbus->board] != 0xff)
			sbus_serving_map &= ~(1 << sbus_tid[sbus->board]);
	}
	for_each_sbus(sbus)
		if (sbus_tid[sbus->board] == 0xff) {
			int i = 31;
				
			if (!sbus_serving_map)
				sbus_serving_map = cpu_present_map;
			while (!(sbus_serving_map & (1 << i)))
				i--;
			sbus_tid[sbus->board] = i;
			sbus_serving_map &= ~(1 << i);
		}
	for_each_sbus(sbus) {
		printk("sbus%d IRQs directed to CPU%d\n", sbus->board, sbus_tid[sbus->board]);
		set_sbi_tid(sbus->devid, sbus_tid[sbus->board] << 3);
	}
#else
	struct sbus_bus *sbus;

static void sun4d_mask_irq(struct irq_data *data)
{
	struct sun4d_handler_data *handler_data = irq_data_get_irq_handler_data(data);
	unsigned int real_irq;
#ifdef CONFIG_SMP
	int cpuid = handler_data->cpuid;
	unsigned long flags;
#endif
	real_irq = handler_data->real_irq;
#ifdef CONFIG_SMP
	spin_lock_irqsave(&sun4d_imsk_lock, flags);
	cc_set_imsk_other(cpuid, cc_get_imsk_other(cpuid) | (1 << real_irq));
	spin_unlock_irqrestore(&sun4d_imsk_lock, flags);
#else
	cc_set_imsk(cc_get_imsk() | (1 << real_irq));
#endif
}

static void sun4d_unmask_irq(struct irq_data *data)
{
	struct sun4d_handler_data *handler_data = irq_data_get_irq_handler_data(data);
	unsigned int real_irq;
#ifdef CONFIG_SMP
	int cpuid = handler_data->cpuid;
	unsigned long flags;
#endif
	real_irq = handler_data->real_irq;

#ifdef CONFIG_SMP
	spin_lock_irqsave(&sun4d_imsk_lock, flags);
	cc_set_imsk_other(cpuid, cc_get_imsk_other(cpuid) & ~(1 << real_irq));
	spin_unlock_irqrestore(&sun4d_imsk_lock, flags);
#else
	cc_set_imsk(cc_get_imsk() & ~(1 << real_irq));
#endif
}

static unsigned int sun4d_startup_irq(struct irq_data *data)
{
	irq_link(data->irq);
	sun4d_unmask_irq(data);
	return 0;
}

static void sun4d_shutdown_irq(struct irq_data *data)
{
	sun4d_mask_irq(data);
	irq_unlink(data->irq);
}

static struct irq_chip sun4d_irq = {
	.name		= "sun4d",
	.irq_startup	= sun4d_startup_irq,
	.irq_shutdown	= sun4d_shutdown_irq,
	.irq_unmask	= sun4d_unmask_irq,
	.irq_mask	= sun4d_mask_irq,
};

#ifdef CONFIG_SMP
/* Setup IRQ distribution scheme. */
void __init sun4d_distribute_irqs(void)
{
	struct device_node *dp;

	int cpuid = cpu_logical_map(1);

	if (cpuid == -1)
		cpuid = cpu_logical_map(0);
	for_each_sbus(sbus) {
		sbus_tid[sbus->board] = cpuid;
		set_sbi_tid(sbus->devid, cpuid << 3);
	}
	printk("All sbus IRQs directed to CPU%d\n", cpuid);
#endif
}
#endif
 
static void sun4d_clear_clock_irq(void)
{
	volatile unsigned int clear_intr;
	clear_intr = sun4d_timers->l10_timer_limit;
}

static void sun4d_clear_profile_irq(int cpu)
{
	bw_get_prof_limit(cpu);
	for_each_node_by_name(dp, "sbi") {
		int devid = of_getintprop_default(dp, "device-id", 0);
		int board = of_getintprop_default(dp, "board#", 0);
		board_to_cpu[board] = cpuid;
		set_sbi_tid(devid, cpuid << 3);
	}
	printk(KERN_ERR "All sbus IRQs directed to CPU%d\n", cpuid);
}
#endif

static void sun4d_clear_clock_irq(void)
{
	sbus_readl(&sun4d_timers->l10_timer_limit);
}

static void sun4d_load_profile_irq(int cpu, unsigned int limit)
{
	bw_set_prof_limit(cpu, limit);
}

static void __init sun4d_init_timers(irq_handler_t counter_fn)
{
	int irq;
	int cpu;
	struct resource r;
	int mid;

	/* Map the User Timer registers. */
	memset(&r, 0, sizeof(r));
#ifdef CONFIG_SMP
	r.start = CSR_BASE(boot_cpu_id)+BW_TIMER_LIMIT;
#else
	r.start = CSR_BASE(0)+BW_TIMER_LIMIT;
#endif
	r.flags = 0xf;
	sun4d_timers = (struct sun4d_timer_regs *) sbus_ioremap(&r, 0,
	    PAGE_SIZE, "user timer");

	sun4d_timers->l10_timer_limit =  (((1000000/HZ) + 1) << 10);
	master_l10_counter = &sun4d_timers->l10_cur_count;
	master_l10_limit = &sun4d_timers->l10_timer_limit;

	irq = request_irq(TIMER_IRQ,
			  counter_fn,
			  (IRQF_DISABLED | SA_STATIC_ALLOC),
			  "timer", NULL);
	if (irq) {
		prom_printf("time_init: unable to attach IRQ%d\n",TIMER_IRQ);
		prom_halt();
	}
	
	/* Enable user timer free run for CPU 0 in BW */
	/* bw_set_ctrl(0, bw_get_ctrl(0) | BW_CTRL_USER_TIMER); */

	cpu = 0;
	unsigned int value = limit ? timer_value(limit) : 0;
	bw_set_prof_limit(cpu, value);
}

static void __init sun4d_load_profile_irqs(void)
{
	int cpu = 0, mid;

	while (!cpu_find_by_instance(cpu, NULL, &mid)) {
		sun4d_load_profile_irq(mid >> 3, 0);
		cpu++;
	}
		
#ifdef CONFIG_SMP
	{
		unsigned long flags;
		extern unsigned long lvl14_save[4];
		struct tt_entry *trap_table = &sparc_ttable[SP_TRAP_IRQ1 + (14 - 1)];
		extern unsigned int real_irq_entry[], smp4d_ticker[];
		extern unsigned int patchme_maybe_smp_msg[];

		/* Adjust so that we jump directly to smp4d_ticker */
		lvl14_save[2] += smp4d_ticker - real_irq_entry;

		/* For SMP we use the level 14 ticker, however the bootup code
		 * has copied the firmware's level 14 vector into the boot cpu's
		 * trap table, we must fix this now or we get squashed.
		 */
		local_irq_save(flags);
		patchme_maybe_smp_msg[0] = 0x01000000; /* NOP out the branch */
		trap_table->inst_one = lvl14_save[0];
		trap_table->inst_two = lvl14_save[1];
		trap_table->inst_three = lvl14_save[2];
		trap_table->inst_four = lvl14_save[3];
		local_flush_cache_all();
		local_irq_restore(flags);
	}
#endif
}

void __init sun4d_init_sbi_irq(void)
{
	struct sbus_bus *sbus;
	unsigned mask;

	nsbi = 0;
	for_each_sbus(sbus)
		nsbi++;
	sbus_actions = kzalloc (nsbi * 8 * 4 * sizeof(struct sbus_action), GFP_ATOMIC);
	if (!sbus_actions) {
		prom_printf("SUN4D: Cannot allocate sbus_actions, halting.\n");
		prom_halt();
	}
	for_each_sbus(sbus) {
#ifdef CONFIG_SMP	
		extern unsigned char boot_cpu_id;
		
		set_sbi_tid(sbus->devid, boot_cpu_id << 3);
		sbus_tid[sbus->board] = boot_cpu_id;
#endif
		/* Get rid of pending irqs from PROM */
		mask = acquire_sbi(sbus->devid, 0xffffffff);
		if (mask) {
			printk ("Clearing pending IRQs %08x on SBI %d\n", mask, sbus->board);
			release_sbi(sbus->devid, mask);
}

static unsigned int _sun4d_build_device_irq(unsigned int real_irq,
                                            unsigned int pil,
                                            unsigned int board)
{
	struct sun4d_handler_data *handler_data;
	unsigned int irq;

	irq = irq_alloc(real_irq, pil);
	if (irq == 0) {
		prom_printf("IRQ: allocate for %d %d %d failed\n",
			real_irq, pil, board);
		goto err_out;
	}

	handler_data = irq_get_handler_data(irq);
	if (unlikely(handler_data))
		goto err_out;

	handler_data = kzalloc(sizeof(struct sun4d_handler_data), GFP_ATOMIC);
	if (unlikely(!handler_data)) {
		prom_printf("IRQ: kzalloc(sun4d_handler_data) failed.\n");
		prom_halt();
	}
	handler_data->cpuid    = board_to_cpu[board];
	handler_data->real_irq = real_irq;
	irq_set_chip_and_handler_name(irq, &sun4d_irq,
	                              handle_level_irq, "level");
	irq_set_handler_data(irq, handler_data);

err_out:
	return irq;
}



static unsigned int sun4d_build_device_irq(struct platform_device *op,
                                           unsigned int real_irq)
{
	struct device_node *dp = op->dev.of_node;
	struct device_node *board_parent, *bus = dp->parent;
	char *bus_connection;
	const struct linux_prom_registers *regs;
	unsigned int pil;
	unsigned int irq;
	int board, slot;
	int sbusl;

	irq = real_irq;
	while (bus) {
		if (!strcmp(bus->name, "sbi")) {
			bus_connection = "io-unit";
			break;
		}

		if (!strcmp(bus->name, "bootbus")) {
			bus_connection = "cpu-unit";
			break;
		}

		bus = bus->parent;
	}
	if (!bus)
		goto err_out;

	regs = of_get_property(dp, "reg", NULL);
	if (!regs)
		goto err_out;

	slot = regs->which_io;

	/*
	 * If Bus nodes parent is not io-unit/cpu-unit or the io-unit/cpu-unit
	 * lacks a "board#" property, something is very wrong.
	 */
	if (!bus->parent || strcmp(bus->parent->name, bus_connection)) {
		printk(KERN_ERR "%s: Error, parent is not %s.\n",
			bus->full_name, bus_connection);
		goto err_out;
	}
	board_parent = bus->parent;
	board = of_getintprop_default(board_parent, "board#", -1);
	if (board == -1) {
		printk(KERN_ERR "%s: Error, lacks board# property.\n",
			board_parent->full_name);
		goto err_out;
	}

	sbusl = pil_to_sbus[real_irq];
	if (sbusl)
		pil = sun4d_encode_irq(board, sbusl, slot);
	else
		pil = real_irq;

	irq = _sun4d_build_device_irq(real_irq, pil, board);
err_out:
	return irq;
}

static unsigned int sun4d_build_timer_irq(unsigned int board,
                                          unsigned int real_irq)
{
	return _sun4d_build_device_irq(real_irq, real_irq, board);
}


static void __init sun4d_fixup_trap_table(void)
{
#ifdef CONFIG_SMP
	unsigned long flags;
	struct tt_entry *trap_table = &sparc_ttable[SP_TRAP_IRQ1 + (14 - 1)];

	/* Adjust so that we jump directly to smp4d_ticker */
	lvl14_save[2] += smp4d_ticker - real_irq_entry;

	/* For SMP we use the level 14 ticker, however the bootup code
	 * has copied the firmware's level 14 vector into the boot cpu's
	 * trap table, we must fix this now or we get squashed.
	 */
	local_irq_save(flags);
	patchme_maybe_smp_msg[0] = 0x01000000; /* NOP out the branch */
	trap_table->inst_one = lvl14_save[0];
	trap_table->inst_two = lvl14_save[1];
	trap_table->inst_three = lvl14_save[2];
	trap_table->inst_four = lvl14_save[3];
	local_ops->cache_all();
	local_irq_restore(flags);
#endif
}

static void __init sun4d_init_timers(void)
{
	struct device_node *dp;
	struct resource res;
	unsigned int irq;
	const u32 *reg;
	int err;
	int board;

	dp = of_find_node_by_name(NULL, "cpu-unit");
	if (!dp) {
		prom_printf("sun4d_init_timers: Unable to find cpu-unit\n");
		prom_halt();
	}

	/* Which cpu-unit we use is arbitrary, we can view the bootbus timer
	 * registers via any cpu's mapping.  The first 'reg' property is the
	 * bootbus.
	 */
	reg = of_get_property(dp, "reg", NULL);
	if (!reg) {
		prom_printf("sun4d_init_timers: No reg property\n");
		prom_halt();
	}

	board = of_getintprop_default(dp, "board#", -1);
	if (board == -1) {
		prom_printf("sun4d_init_timers: No board# property on cpu-unit\n");
		prom_halt();
	}

	of_node_put(dp);

	res.start = reg[1];
	res.end = reg[2] - 1;
	res.flags = reg[0] & 0xff;
	sun4d_timers = of_ioremap(&res, BW_TIMER_LIMIT,
				  sizeof(struct sun4d_timer_regs), "user timer");
	if (!sun4d_timers) {
		prom_printf("sun4d_init_timers: Can't map timer regs\n");
		prom_halt();
	}

#ifdef CONFIG_SMP
	sparc_config.cs_period = SBUS_CLOCK_RATE * 2;  /* 2 seconds */
#else
	sparc_config.cs_period = SBUS_CLOCK_RATE / HZ; /* 1/HZ sec  */
	sparc_config.features |= FEAT_L10_CLOCKEVENT;
#endif
	sparc_config.features |= FEAT_L10_CLOCKSOURCE;
	sbus_writel(timer_value(sparc_config.cs_period),
		    &sun4d_timers->l10_timer_limit);

	master_l10_counter = &sun4d_timers->l10_cur_count;

	irq = sun4d_build_timer_irq(board, SUN4D_TIMER_IRQ);
	err = request_irq(irq, timer_interrupt, IRQF_TIMER, "timer", NULL);
	if (err) {
		prom_printf("sun4d_init_timers: request_irq() failed with %d\n",
		             err);
		prom_halt();
	}
	sun4d_load_profile_irqs();
	sun4d_fixup_trap_table();
}

void __init sun4d_init_sbi_irq(void)
{
	struct device_node *dp;
	int target_cpu;

	target_cpu = boot_cpu_id;
	for_each_node_by_name(dp, "sbi") {
		int devid = of_getintprop_default(dp, "device-id", 0);
		int board = of_getintprop_default(dp, "board#", 0);
		unsigned int mask;

		set_sbi_tid(devid, target_cpu << 3);
		board_to_cpu[board] = target_cpu;

		/* Get rid of pending irqs from PROM */
		mask = acquire_sbi(devid, 0xffffffff);
		if (mask) {
			printk(KERN_ERR "Clearing pending IRQs %08x on SBI %d\n",
			       mask, board);
			release_sbi(devid, mask);
		}
	}
}

void __init sun4d_init_IRQ(void)
{
	local_irq_disable();

	BTFIXUPSET_CALL(sbint_to_irq, sun4d_sbint_to_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(enable_irq, sun4d_enable_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(disable_irq, sun4d_disable_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(clear_clock_irq, sun4d_clear_clock_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(clear_profile_irq, sun4d_clear_profile_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(load_profile_irq, sun4d_load_profile_irq, BTFIXUPCALL_NORM);
	sparc_init_timers = sun4d_init_timers;
#ifdef CONFIG_SMP
	BTFIXUPSET_CALL(set_cpu_int, sun4d_set_cpu_int, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(clear_cpu_int, sun4d_clear_ipi, BTFIXUPCALL_NOP);
	BTFIXUPSET_CALL(set_irq_udt, sun4d_set_udt, BTFIXUPCALL_NOP);
#endif
	sparc_config.init_timers      = sun4d_init_timers;
	sparc_config.build_device_irq = sun4d_build_device_irq;
	sparc_config.clock_rate       = SBUS_CLOCK_RATE;
	sparc_config.clear_clock_irq  = sun4d_clear_clock_irq;
	sparc_config.load_profile_irq = sun4d_load_profile_irq;

	/* Cannot enable interrupts until OBP ticker is disabled. */
}
