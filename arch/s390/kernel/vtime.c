/*
 *  arch/s390/kernel/vtime.c
 *    Virtual cpu timer based timer functions.
 *
 *  S390 version
 *    Copyright (C) 2004 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Jan Glauber <jan.glauber@de.ibm.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/types.h>
#include <linux/timex.h>
#include <linux/notifier.h>
#include <linux/kernel_stat.h>
#include <linux/rcupdate.h>
#include <linux/posix-timers.h>

#include <asm/s390_ext.h>
#include <asm/timer.h>
#include <asm/irq_regs.h>

static ext_int_info_t ext_int_info_timer;
static DEFINE_PER_CPU(struct vtimer_queue, virt_cpu_timer);

#ifdef CONFIG_VIRT_CPU_ACCOUNTING
 *    Virtual cpu timer based timer functions.
 *
 *    Copyright IBM Corp. 2004, 2012
 *    Author(s): Jan Glauber <jan.glauber@de.ibm.com>
 */

#include <linux/kernel_stat.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/timex.h>
#include <linux/types.h>
#include <linux/time.h>

#include <asm/cputime.h>
#include <asm/vtimer.h>
#include <asm/vtime.h>
#include <asm/cpu_mf.h>
#include <asm/smp.h>

static void virt_timer_expire(void);

static LIST_HEAD(virt_timer_list);
static DEFINE_SPINLOCK(virt_timer_lock);
static atomic64_t virt_timer_current;
static atomic64_t virt_timer_elapsed;

DEFINE_PER_CPU(u64, mt_cycles[8]);
static DEFINE_PER_CPU(u64, mt_scaling_mult) = { 1 };
static DEFINE_PER_CPU(u64, mt_scaling_div) = { 1 };
static DEFINE_PER_CPU(u64, mt_scaling_jiffies);

static inline u64 get_vtimer(void)
{
	u64 timer;

	asm volatile("stpt %0" : "=m" (timer));
	return timer;
}

static inline void set_vtimer(u64 expires)
{
	u64 timer;

	asm volatile(
		"	stpt	%0\n"	/* Store current cpu timer value */
		"	spt	%1"	/* Set new value imm. afterwards */
		: "=m" (timer) : "m" (expires));
	S390_lowcore.system_timer += S390_lowcore.last_update_timer - timer;
	S390_lowcore.last_update_timer = expires;
}

static inline int virt_timer_forward(u64 elapsed)
{
	BUG_ON(!irqs_disabled());

	if (list_empty(&virt_timer_list))
		return 0;
	elapsed = atomic64_add_return(elapsed, &virt_timer_elapsed);
	return elapsed >= atomic64_read(&virt_timer_current);
}

static void update_mt_scaling(void)
{
	u64 cycles_new[8], *cycles_old;
	u64 delta, fac, mult, div;
	int i;

	stcctm5(smp_cpu_mtid + 1, cycles_new);
	cycles_old = this_cpu_ptr(mt_cycles);
	fac = 1;
	mult = div = 0;
	for (i = 0; i <= smp_cpu_mtid; i++) {
		delta = cycles_new[i] - cycles_old[i];
		div += delta;
		mult *= i + 1;
		mult += delta * fac;
		fac *= i + 1;
	}
	div *= fac;
	if (div > 0) {
		/* Update scaling factor */
		__this_cpu_write(mt_scaling_mult, mult);
		__this_cpu_write(mt_scaling_div, div);
		memcpy(cycles_old, cycles_new,
		       sizeof(u64) * (smp_cpu_mtid + 1));
	}
	__this_cpu_write(mt_scaling_jiffies, jiffies_64);
}

/*
 * Update process times based on virtual cpu times stored by entry.S
 * to the lowcore fields user_timer, system_timer & steal_clock.
 */
void account_process_tick(struct task_struct *tsk, int user_tick)
{
	cputime_t cputime;
	__u64 timer, clock;
	int rcu_user_flag;

	timer = S390_lowcore.last_update_timer;
	clock = S390_lowcore.last_update_clock;
	asm volatile ("  STPT %0\n"    /* Store current cpu timer value */
		      "  STCK %1"      /* Store current tod clock value */
		      : "=m" (S390_lowcore.last_update_timer),
		        "=m" (S390_lowcore.last_update_clock) );
	S390_lowcore.system_timer += timer - S390_lowcore.last_update_timer;
	S390_lowcore.steal_clock += S390_lowcore.last_update_clock - clock;

	cputime = S390_lowcore.user_timer >> 12;
	rcu_user_flag = cputime != 0;
	S390_lowcore.user_timer -= cputime << 12;
	S390_lowcore.steal_clock -= cputime << 12;
	account_user_time(tsk, cputime);

	cputime =  S390_lowcore.system_timer >> 12;
	S390_lowcore.system_timer -= cputime << 12;
	S390_lowcore.steal_clock -= cputime << 12;
	account_system_time(tsk, HARDIRQ_OFFSET, cputime);

	cputime = S390_lowcore.steal_clock;
	if ((__s64) cputime > 0) {
		cputime >>= 12;
		S390_lowcore.steal_clock -= cputime << 12;
		account_steal_time(tsk, cputime);
	}
}

/*
 * Update process times based on virtual cpu times stored by entry.S
 * to the lowcore fields user_timer, system_timer & steal_clock.
 */
void account_vtime(struct task_struct *tsk)
{
	cputime_t cputime;
	__u64 timer;

	timer = S390_lowcore.last_update_timer;
	asm volatile ("  STPT %0"    /* Store current cpu timer value */
		      : "=m" (S390_lowcore.last_update_timer) );
	S390_lowcore.system_timer += timer - S390_lowcore.last_update_timer;

	cputime = S390_lowcore.user_timer >> 12;
	S390_lowcore.user_timer -= cputime << 12;
	S390_lowcore.steal_clock -= cputime << 12;
	account_user_time(tsk, cputime);

	cputime =  S390_lowcore.system_timer >> 12;
	S390_lowcore.system_timer -= cputime << 12;
	S390_lowcore.steal_clock -= cputime << 12;
	account_system_time(tsk, 0, cputime);
}

/*
 * Update process times based on virtual cpu times stored by entry.S
 * to the lowcore fields user_timer, system_timer & steal_clock.
 */
void account_system_vtime(struct task_struct *tsk)
{
	cputime_t cputime;
	__u64 timer;

	timer = S390_lowcore.last_update_timer;
	asm volatile ("  STPT %0"    /* Store current cpu timer value */
		      : "=m" (S390_lowcore.last_update_timer) );
	S390_lowcore.system_timer += timer - S390_lowcore.last_update_timer;

	cputime =  S390_lowcore.system_timer >> 12;
	S390_lowcore.system_timer -= cputime << 12;
	S390_lowcore.steal_clock -= cputime << 12;
	account_system_time(tsk, 0, cputime);
}
EXPORT_SYMBOL_GPL(account_system_vtime);

static inline void set_vtimer(__u64 expires)
{
	__u64 timer;

	asm volatile ("  STPT %0\n"  /* Store current cpu timer value */
		      "  SPT %1"     /* Set new value immediatly afterwards */
		      : "=m" (timer) : "m" (expires) );
	S390_lowcore.system_timer += S390_lowcore.last_update_timer - timer;
	S390_lowcore.last_update_timer = expires;

	/* store expire time for this CPU timer */
	__get_cpu_var(virt_cpu_timer).to_expire = expires;
}
#else
static inline void set_vtimer(__u64 expires)
{
	S390_lowcore.last_update_timer = expires;
	asm volatile ("SPT %0" : : "m" (S390_lowcore.last_update_timer));

	/* store expire time for this CPU timer */
	__get_cpu_var(virt_cpu_timer).to_expire = expires;
}
#endif

void vtime_start_cpu_timer(void)
{
	struct vtimer_queue *vt_list;

	vt_list = &__get_cpu_var(virt_cpu_timer);

	/* CPU timer interrupt is pending, don't reprogramm it */
	if (vt_list->idle & 1LL<<63)
		return;

	if (!list_empty(&vt_list->list))
		set_vtimer(vt_list->idle);
}

void vtime_stop_cpu_timer(void)
{
	struct vtimer_queue *vt_list;

	vt_list = &__get_cpu_var(virt_cpu_timer);

	/* nothing to do */
	if (list_empty(&vt_list->list)) {
		vt_list->idle = VTIMER_MAX_SLICE;
		goto fire;
	}

	/* store the actual expire value */
	asm volatile ("STPT %0" : "=m" (vt_list->idle));

	/*
	 * If the CPU timer is negative we don't reprogramm
	 * it because we will get instantly an interrupt.
	 */
	if (vt_list->idle & 1LL<<63)
		return;

	vt_list->offset += vt_list->to_expire - vt_list->idle;

	/*
	 * We cannot halt the CPU timer, we just write a value that
	 * nearly never expires (only after 71 years) and re-write
	 * the stored expire value if we continue the timer
	 */
 fire:
	set_vtimer(VTIMER_MAX_SLICE);
}

static int do_account_vtime(struct task_struct *tsk, int hardirq_offset)
{
	struct thread_info *ti = task_thread_info(tsk);
	u64 timer, clock, user, system, steal;
	u64 user_scaled, system_scaled;

	timer = S390_lowcore.last_update_timer;
	clock = S390_lowcore.last_update_clock;
	asm volatile(
		"	stpt	%0\n"	/* Store current cpu timer value */
#ifdef CONFIG_HAVE_MARCH_Z9_109_FEATURES
		"	stckf	%1"	/* Store current tod clock value */
#else
		"	stck	%1"	/* Store current tod clock value */
#endif
		: "=m" (S390_lowcore.last_update_timer),
		  "=m" (S390_lowcore.last_update_clock));
	S390_lowcore.system_timer += timer - S390_lowcore.last_update_timer;
	S390_lowcore.steal_timer += S390_lowcore.last_update_clock - clock;

	/* Update MT utilization calculation */
	if (smp_cpu_mtid &&
	    time_after64(jiffies_64, this_cpu_read(mt_scaling_jiffies)))
		update_mt_scaling();

	user = S390_lowcore.user_timer - ti->user_timer;
	S390_lowcore.steal_timer -= user;
	ti->user_timer = S390_lowcore.user_timer;

	system = S390_lowcore.system_timer - ti->system_timer;
	S390_lowcore.steal_timer -= system;
	ti->system_timer = S390_lowcore.system_timer;

	user_scaled = user;
	system_scaled = system;
	/* Do MT utilization scaling */
	if (smp_cpu_mtid) {
		u64 mult = __this_cpu_read(mt_scaling_mult);
		u64 div = __this_cpu_read(mt_scaling_div);

		user_scaled = (user_scaled * mult) / div;
		system_scaled = (system_scaled * mult) / div;
	}
	account_user_time(tsk, user, user_scaled);
	account_system_time(tsk, hardirq_offset, system, system_scaled);

	steal = S390_lowcore.steal_timer;
	if ((s64) steal > 0) {
		S390_lowcore.steal_timer = 0;
		account_steal_time(steal);
	}

	return virt_timer_forward(user + system);
}

void vtime_task_switch(struct task_struct *prev)
{
	struct thread_info *ti;

	do_account_vtime(prev, 0);
	ti = task_thread_info(prev);
	ti->user_timer = S390_lowcore.user_timer;
	ti->system_timer = S390_lowcore.system_timer;
	ti = task_thread_info(current);
	S390_lowcore.user_timer = ti->user_timer;
	S390_lowcore.system_timer = ti->system_timer;
}

/*
 * In s390, accounting pending user time also implies
 * accounting system time in order to correctly compute
 * the stolen time accounting.
 */
void vtime_account_user(struct task_struct *tsk)
{
	if (do_account_vtime(tsk, HARDIRQ_OFFSET))
		virt_timer_expire();
}

/*
 * Update process times based on virtual cpu times stored by entry.S
 * to the lowcore fields user_timer, system_timer & steal_clock.
 */
void vtime_account_irq_enter(struct task_struct *tsk)
{
	struct thread_info *ti = task_thread_info(tsk);
	u64 timer, system, system_scaled;

	timer = S390_lowcore.last_update_timer;
	S390_lowcore.last_update_timer = get_vtimer();
	S390_lowcore.system_timer += timer - S390_lowcore.last_update_timer;

	/* Update MT utilization calculation */
	if (smp_cpu_mtid &&
	    time_after64(jiffies_64, this_cpu_read(mt_scaling_jiffies)))
		update_mt_scaling();

	system = S390_lowcore.system_timer - ti->system_timer;
	S390_lowcore.steal_timer -= system;
	ti->system_timer = S390_lowcore.system_timer;
	system_scaled = system;
	/* Do MT utilization scaling */
	if (smp_cpu_mtid) {
		u64 mult = __this_cpu_read(mt_scaling_mult);
		u64 div = __this_cpu_read(mt_scaling_div);

		system_scaled = (system_scaled * mult) / div;
	}
	account_system_time(tsk, 0, system, system_scaled);

	virt_timer_forward(system);
}
EXPORT_SYMBOL_GPL(vtime_account_irq_enter);

void vtime_account_system(struct task_struct *tsk)
__attribute__((alias("vtime_account_irq_enter")));
EXPORT_SYMBOL_GPL(vtime_account_system);

/*
 * Sorted add to a list. List is linear searched until first bigger
 * element is found.
 */
static void list_add_sorted(struct vtimer_list *timer, struct list_head *head)
{
	struct vtimer_list *event;

	list_for_each_entry(event, head, entry) {
		if (event->expires > timer->expires) {
			list_add_tail(&timer->entry, &event->entry);
	struct vtimer_list *tmp;

	list_for_each_entry(tmp, head, entry) {
		if (tmp->expires > timer->expires) {
			list_add_tail(&timer->entry, &tmp->entry);
			return;
		}
	}
	list_add_tail(&timer->entry, head);
}

/*
 * Do the callback functions of expired vtimer events.
 * Called from within the interrupt handler.
 */
static void do_callbacks(struct list_head *cb_list)
{
	struct vtimer_queue *vt_list;
	struct vtimer_list *event, *tmp;
	void (*fn)(unsigned long);
	unsigned long data;

	if (list_empty(cb_list))
		return;

	vt_list = &__get_cpu_var(virt_cpu_timer);

	list_for_each_entry_safe(event, tmp, cb_list, entry) {
		fn = event->function;
		data = event->data;
		fn(data);

		if (!event->interval)
			/* delete one shot timer */
			list_del_init(&event->entry);
		else {
			/* move interval timer back to list */
			spin_lock(&vt_list->lock);
			list_del_init(&event->entry);
			list_add_sorted(event, &vt_list->list);
			spin_unlock(&vt_list->lock);
 * Handler for expired virtual CPU timer.
 */
static void virt_timer_expire(void)
{
	struct vtimer_list *timer, *tmp;
	unsigned long elapsed;
	LIST_HEAD(cb_list);

	/* walk timer list, fire all expired timers */
	spin_lock(&virt_timer_lock);
	elapsed = atomic64_read(&virt_timer_elapsed);
	list_for_each_entry_safe(timer, tmp, &virt_timer_list, entry) {
		if (timer->expires < elapsed)
			/* move expired timer to the callback queue */
			list_move_tail(&timer->entry, &cb_list);
		else
			timer->expires -= elapsed;
	}
	if (!list_empty(&virt_timer_list)) {
		timer = list_first_entry(&virt_timer_list,
					 struct vtimer_list, entry);
		atomic64_set(&virt_timer_current, timer->expires);
	}
	atomic64_sub(elapsed, &virt_timer_elapsed);
	spin_unlock(&virt_timer_lock);

	/* Do callbacks and recharge periodic timers */
	list_for_each_entry_safe(timer, tmp, &cb_list, entry) {
		list_del_init(&timer->entry);
		timer->function(timer->data);
		if (timer->interval) {
			/* Recharge interval timer */
			timer->expires = timer->interval +
				atomic64_read(&virt_timer_elapsed);
			spin_lock(&virt_timer_lock);
			list_add_sorted(timer, &virt_timer_list);
			spin_unlock(&virt_timer_lock);
		}
	}
}

/*
 * Handler for the virtual CPU timer.
 */
static void do_cpu_timer_interrupt(__u16 error_code)
{
	__u64 next, delta;
	struct vtimer_queue *vt_list;
	struct vtimer_list *event, *tmp;
	struct list_head *ptr;
	/* the callback queue */
	struct list_head cb_list;

	INIT_LIST_HEAD(&cb_list);
	vt_list = &__get_cpu_var(virt_cpu_timer);

	/* walk timer list, fire all expired events */
	spin_lock(&vt_list->lock);

	if (vt_list->to_expire < VTIMER_MAX_SLICE)
		vt_list->offset += vt_list->to_expire;

	list_for_each_entry_safe(event, tmp, &vt_list->list, entry) {
		if (event->expires > vt_list->offset)
			/* found first unexpired event, leave */
			break;

		/* re-charge interval timer, we have to add the offset */
		if (event->interval)
			event->expires = event->interval + vt_list->offset;

		/* move expired timer to the callback queue */
		list_move_tail(&event->entry, &cb_list);
	}
	spin_unlock(&vt_list->lock);
	do_callbacks(&cb_list);

	/* next event is first in list */
	spin_lock(&vt_list->lock);
	if (!list_empty(&vt_list->list)) {
		ptr = vt_list->list.next;
		event = list_entry(ptr, struct vtimer_list, entry);
		next = event->expires - vt_list->offset;

		/* add the expired time from this interrupt handler
		 * and the callback functions
		 */
		asm volatile ("STPT %0" : "=m" (delta));
		delta = 0xffffffffffffffffLL - delta + 1;
		vt_list->offset += delta;
		next -= delta;
	} else {
		vt_list->offset = 0;
		next = VTIMER_MAX_SLICE;
	}
	spin_unlock(&vt_list->lock);
	set_vtimer(next);
}

void init_virt_timer(struct vtimer_list *timer)
{
	timer->function = NULL;
	INIT_LIST_HEAD(&timer->entry);
	spin_lock_init(&timer->lock);
}
EXPORT_SYMBOL(init_virt_timer);

static inline int vtimer_pending(struct vtimer_list *timer)
{
	return (!list_empty(&timer->entry));
}

/*
 * this function should only run on the specified CPU
 */
static void internal_add_vtimer(struct vtimer_list *timer)
{
	unsigned long flags;
	__u64 done;
	struct vtimer_list *event;
	struct vtimer_queue *vt_list;

	vt_list = &per_cpu(virt_cpu_timer, timer->cpu);
	spin_lock_irqsave(&vt_list->lock, flags);

	BUG_ON(timer->cpu != smp_processor_id());

	/* if list is empty we only have to set the timer */
	if (list_empty(&vt_list->list)) {
		/* reset the offset, this may happen if the last timer was
		 * just deleted by mod_virt_timer and the interrupt
		 * didn't happen until here
		 */
		vt_list->offset = 0;
		goto fire;
	}

	/* save progress */
	asm volatile ("STPT %0" : "=m" (done));

	/* calculate completed work */
	done = vt_list->to_expire - done + vt_list->offset;
	vt_list->offset = 0;

	list_for_each_entry(event, &vt_list->list, entry)
		event->expires -= done;

 fire:
	list_add_sorted(timer, &vt_list->list);

	/* get first element, which is the next vtimer slice */
	event = list_entry(vt_list->list.next, struct vtimer_list, entry);

	set_vtimer(event->expires);
	spin_unlock_irqrestore(&vt_list->lock, flags);
	/* release CPU acquired in prepare_vtimer or mod_virt_timer() */
	put_cpu();
}

static inline void prepare_vtimer(struct vtimer_list *timer)
{
	BUG_ON(!timer->function);
	BUG_ON(!timer->expires || timer->expires > VTIMER_MAX_SLICE);
	BUG_ON(vtimer_pending(timer));
	timer->cpu = get_cpu();
	return !list_empty(&timer->entry);
}

static void internal_add_vtimer(struct vtimer_list *timer)
{
	if (list_empty(&virt_timer_list)) {
		/* First timer, just program it. */
		atomic64_set(&virt_timer_current, timer->expires);
		atomic64_set(&virt_timer_elapsed, 0);
		list_add(&timer->entry, &virt_timer_list);
	} else {
		/* Update timer against current base. */
		timer->expires += atomic64_read(&virt_timer_elapsed);
		if (likely((s64) timer->expires <
			   (s64) atomic64_read(&virt_timer_current)))
			/* The new timer expires before the current timer. */
			atomic64_set(&virt_timer_current, timer->expires);
		/* Insert new timer into the list. */
		list_add_sorted(timer, &virt_timer_list);
	}
}

static void __add_vtimer(struct vtimer_list *timer, int periodic)
{
	unsigned long flags;

	timer->interval = periodic ? timer->expires : 0;
	spin_lock_irqsave(&virt_timer_lock, flags);
	internal_add_vtimer(timer);
	spin_unlock_irqrestore(&virt_timer_lock, flags);
}

/*
 * add_virt_timer - add an oneshot virtual CPU timer
 */
void add_virt_timer(void *new)
{
	struct vtimer_list *timer;

	timer = (struct vtimer_list *)new;
	prepare_vtimer(timer);
	timer->interval = 0;
	internal_add_vtimer(timer);
void add_virt_timer(struct vtimer_list *timer)
{
	__add_vtimer(timer, 0);
}
EXPORT_SYMBOL(add_virt_timer);

/*
 * add_virt_timer_int - add an interval virtual CPU timer
 */
void add_virt_timer_periodic(void *new)
{
	struct vtimer_list *timer;

	timer = (struct vtimer_list *)new;
	prepare_vtimer(timer);
	timer->interval = timer->expires;
	internal_add_vtimer(timer);
}
EXPORT_SYMBOL(add_virt_timer_periodic);

/*
 * If we change a pending timer the function must be called on the CPU
 * where the timer is running on, e.g. by smp_call_function_single()
 *
 * The original mod_timer adds the timer if it is not pending. For compatibility
 * we do the same. The timer will be added on the current CPU as a oneshot timer.
 *
 * returns whether it has modified a pending timer (1) or not (0)
 */
int mod_virt_timer(struct vtimer_list *timer, __u64 expires)
{
	struct vtimer_queue *vt_list;
	unsigned long flags;
	int cpu;

	BUG_ON(!timer->function);
	BUG_ON(!expires || expires > VTIMER_MAX_SLICE);

	/*
	 * This is a common optimization triggered by the
	 * networking code - if the timer is re-modified
	 * to be the same thing then just return:
	 */
	if (timer->expires == expires && vtimer_pending(timer))
		return 1;

	cpu = get_cpu();
	vt_list = &per_cpu(virt_cpu_timer, cpu);

	/* check if we run on the right CPU */
	BUG_ON(timer->cpu != cpu);

	/* disable interrupts before test if timer is pending */
	spin_lock_irqsave(&vt_list->lock, flags);

	/* if timer isn't pending add it on the current CPU */
	if (!vtimer_pending(timer)) {
		spin_unlock_irqrestore(&vt_list->lock, flags);
		/* we do not activate an interval timer with mod_virt_timer */
		timer->interval = 0;
		timer->expires = expires;
		timer->cpu = cpu;
		internal_add_vtimer(timer);
		return 0;
	}

	list_del_init(&timer->entry);
	timer->expires = expires;

	/* also change the interval if we have an interval timer */
	if (timer->interval)
		timer->interval = expires;

	/* the timer can't expire anymore so we can release the lock */
	spin_unlock_irqrestore(&vt_list->lock, flags);
	internal_add_vtimer(timer);
	return 1;
void add_virt_timer_periodic(struct vtimer_list *timer)
{
	__add_vtimer(timer, 1);
}
EXPORT_SYMBOL(add_virt_timer_periodic);

static int __mod_vtimer(struct vtimer_list *timer, u64 expires, int periodic)
{
	unsigned long flags;
	int rc;

	BUG_ON(!timer->function);

	if (timer->expires == expires && vtimer_pending(timer))
		return 1;
	spin_lock_irqsave(&virt_timer_lock, flags);
	rc = vtimer_pending(timer);
	if (rc)
		list_del_init(&timer->entry);
	timer->interval = periodic ? expires : 0;
	timer->expires = expires;
	internal_add_vtimer(timer);
	spin_unlock_irqrestore(&virt_timer_lock, flags);
	return rc;
}

/*
 * returns whether it has modified a pending timer (1) or not (0)
 */
int mod_virt_timer(struct vtimer_list *timer, u64 expires)
{
	return __mod_vtimer(timer, expires, 0);
}
EXPORT_SYMBOL(mod_virt_timer);

/*
 * delete a virtual timer
 * returns whether it has modified a pending timer (1) or not (0)
 */
int mod_virt_timer_periodic(struct vtimer_list *timer, u64 expires)
{
	return __mod_vtimer(timer, expires, 1);
}
EXPORT_SYMBOL(mod_virt_timer_periodic);

/*
 * Delete a virtual timer.
 *
 * returns whether the deleted timer was pending (1) or not (0)
 */
int del_virt_timer(struct vtimer_list *timer)
{
	unsigned long flags;
	struct vtimer_queue *vt_list;

	/* check if timer is pending */
	if (!vtimer_pending(timer))
		return 0;

	vt_list = &per_cpu(virt_cpu_timer, timer->cpu);
	spin_lock_irqsave(&vt_list->lock, flags);

	/* we don't interrupt a running timer, just let it expire! */
	list_del_init(&timer->entry);

	/* last timer removed */
	if (list_empty(&vt_list->list)) {
		vt_list->to_expire = 0;
		vt_list->offset = 0;
	}

	spin_unlock_irqrestore(&vt_list->lock, flags);

	if (!vtimer_pending(timer))
		return 0;
	spin_lock_irqsave(&virt_timer_lock, flags);
	list_del_init(&timer->entry);
	spin_unlock_irqrestore(&virt_timer_lock, flags);
	return 1;
}
EXPORT_SYMBOL(del_virt_timer);

/*
 * Start the virtual CPU timer on the current CPU.
 */
void init_cpu_vtimer(void)
{
	struct vtimer_queue *vt_list;

	/* kick the virtual timer */
	S390_lowcore.exit_timer = VTIMER_MAX_SLICE;
	S390_lowcore.last_update_timer = VTIMER_MAX_SLICE;
	asm volatile ("SPT %0" : : "m" (S390_lowcore.last_update_timer));
	asm volatile ("STCK %0" : "=m" (S390_lowcore.last_update_clock));

	/* enable cpu timer interrupts */
	__ctl_set_bit(0,10);

	vt_list = &__get_cpu_var(virt_cpu_timer);
	INIT_LIST_HEAD(&vt_list->list);
	spin_lock_init(&vt_list->lock);
	vt_list->to_expire = 0;
	vt_list->offset = 0;
	vt_list->idle = 0;

}

void __init vtime_init(void)
{
	/* request the cpu timer external interrupt */
	if (register_early_external_interrupt(0x1005, do_cpu_timer_interrupt,
					      &ext_int_info_timer) != 0)
		panic("Couldn't request external interrupt 0x1005");

	/* Enable cpu timer interrupts on the boot cpu. */
	init_cpu_vtimer();
}

void vtime_init(void)
{
	/* set initial cpu timer */
	set_vtimer(VTIMER_MAX_SLICE);
	/* Setup initial MT scaling values */
	if (smp_cpu_mtid) {
		__this_cpu_write(mt_scaling_jiffies, jiffies);
		__this_cpu_write(mt_scaling_mult, 1);
		__this_cpu_write(mt_scaling_div, 1);
		stcctm5(smp_cpu_mtid + 1, this_cpu_ptr(mt_cycles));
	}
}
