/*
 *  arch/s390/lib/spinlock.c
 *    Out of line spinlock code.
 *
 *    Copyright (C) IBM Corp. 2004, 2006
 *    Out of line spinlock code.
 *
 *    Copyright IBM Corp. 2004, 2006
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <asm/io.h>

int spin_retry = 1000;
#include <linux/smp.h>
#include <asm/io.h>

int spin_retry = -1;

static int __init spin_retry_init(void)
{
	if (spin_retry < 0)
		spin_retry = MACHINE_HAS_CAD ? 10 : 1000;
	return 0;
}
early_initcall(spin_retry_init);

/**
 * spin_retry= parameter
 */
static int __init spin_retry_setup(char *str)
{
	spin_retry = simple_strtoul(str, &str, 0);
	return 1;
}
__setup("spin_retry=", spin_retry_setup);

static inline void _raw_yield(void)
{
	if (MACHINE_HAS_DIAG44)
		asm volatile("diag 0,0,0x44");
}

static inline void _raw_yield_cpu(int cpu)
{
	if (MACHINE_HAS_DIAG9C)
		asm volatile("diag %0,0,0x9c"
			     : : "d" (__cpu_logical_map[cpu]));
	else
		_raw_yield();
}

void _raw_spin_lock_wait(raw_spinlock_t *lp)
{
	int count = spin_retry;
	unsigned int cpu = ~smp_processor_id();

	while (1) {
		if (count-- <= 0) {
			unsigned int owner = lp->owner_cpu;
			if (owner != 0)
				_raw_yield_cpu(~owner);
			count = spin_retry;
		}
		if (__raw_spin_is_locked(lp))
			continue;
		if (_raw_compare_and_swap(&lp->owner_cpu, 0, cpu) == 0)
			return;
	}
}
EXPORT_SYMBOL(_raw_spin_lock_wait);

void _raw_spin_lock_wait_flags(raw_spinlock_t *lp, unsigned long flags)
{
	int count = spin_retry;
	unsigned int cpu = ~smp_processor_id();

	local_irq_restore(flags);
	while (1) {
		if (count-- <= 0) {
			unsigned int owner = lp->owner_cpu;
			if (owner != 0)
				_raw_yield_cpu(~owner);
			count = spin_retry;
		}
		if (__raw_spin_is_locked(lp))
			continue;
		local_irq_disable();
		if (_raw_compare_and_swap(&lp->owner_cpu, 0, cpu) == 0)
			return;
		local_irq_restore(flags);
	}
}
EXPORT_SYMBOL(_raw_spin_lock_wait_flags);

int _raw_spin_trylock_retry(raw_spinlock_t *lp)
{
	unsigned int cpu = ~smp_processor_id();
	int count;

	for (count = spin_retry; count > 0; count--) {
		if (__raw_spin_is_locked(lp))
			continue;
		if (_raw_compare_and_swap(&lp->owner_cpu, 0, cpu) == 0)
			return 1;
	}
	return 0;
}
EXPORT_SYMBOL(_raw_spin_trylock_retry);

void _raw_spin_relax(raw_spinlock_t *lock)
{
	unsigned int cpu = lock->owner_cpu;
	if (cpu != 0)
		_raw_yield_cpu(~cpu);
}
EXPORT_SYMBOL(_raw_spin_relax);

void _raw_read_lock_wait(raw_rwlock_t *rw)
{
	unsigned int old;
	int count = spin_retry;

	while (1) {
		if (count-- <= 0) {
			_raw_yield();
			count = spin_retry;
		}
		if (!__raw_read_can_lock(rw))
			continue;
		old = rw->lock & 0x7fffffffU;
		if (_raw_compare_and_swap(&rw->lock, old, old + 1) == old)
static inline void _raw_compare_and_delay(unsigned int *lock, unsigned int old)
{
	asm(".insn rsy,0xeb0000000022,%0,0,%1" : : "d" (old), "Q" (*lock));
}

void arch_spin_lock_wait(arch_spinlock_t *lp)
{
	unsigned int cpu = SPINLOCK_LOCKVAL;
	unsigned int owner;
	int count;

	while (1) {
		owner = ACCESS_ONCE(lp->lock);
		/* Try to get the lock if it is free. */
		if (!owner) {
			if (_raw_compare_and_swap(&lp->lock, 0, cpu))
				return;
			continue;
		}
		/* Check if the lock owner is running. */
		if (!smp_vcpu_scheduled(~owner)) {
			smp_yield_cpu(~owner);
			continue;
		}
		/* Loop for a while on the lock value. */
		count = spin_retry;
		do {
			if (MACHINE_HAS_CAD)
				_raw_compare_and_delay(&lp->lock, owner);
			owner = ACCESS_ONCE(lp->lock);
		} while (owner && count-- > 0);
		if (!owner)
			continue;
		/*
		 * For multiple layers of hypervisors, e.g. z/VM + LPAR
		 * yield the CPU if the lock is still unavailable.
		 */
		if (!MACHINE_IS_LPAR)
			smp_yield_cpu(~owner);
	}
}
EXPORT_SYMBOL(arch_spin_lock_wait);

void arch_spin_lock_wait_flags(arch_spinlock_t *lp, unsigned long flags)
{
	unsigned int cpu = SPINLOCK_LOCKVAL;
	unsigned int owner;
	int count;

	local_irq_restore(flags);
	while (1) {
		owner = ACCESS_ONCE(lp->lock);
		/* Try to get the lock if it is free. */
		if (!owner) {
			local_irq_disable();
			if (_raw_compare_and_swap(&lp->lock, 0, cpu))
				return;
			local_irq_restore(flags);
		}
		/* Check if the lock owner is running. */
		if (!smp_vcpu_scheduled(~owner)) {
			smp_yield_cpu(~owner);
			continue;
		}
		/* Loop for a while on the lock value. */
		count = spin_retry;
		do {
			if (MACHINE_HAS_CAD)
				_raw_compare_and_delay(&lp->lock, owner);
			owner = ACCESS_ONCE(lp->lock);
		} while (owner && count-- > 0);
		if (!owner)
			continue;
		/*
		 * For multiple layers of hypervisors, e.g. z/VM + LPAR
		 * yield the CPU if the lock is still unavailable.
		 */
		if (!MACHINE_IS_LPAR)
			smp_yield_cpu(~owner);
	}
}
EXPORT_SYMBOL(arch_spin_lock_wait_flags);

int arch_spin_trylock_retry(arch_spinlock_t *lp)
{
	unsigned int cpu = SPINLOCK_LOCKVAL;
	unsigned int owner;
	int count;

	for (count = spin_retry; count > 0; count--) {
		owner = ACCESS_ONCE(lp->lock);
		/* Try to get the lock if it is free. */
		if (!owner) {
			if (_raw_compare_and_swap(&lp->lock, 0, cpu))
				return 1;
		} else if (MACHINE_HAS_CAD)
			_raw_compare_and_delay(&lp->lock, owner);
	}
	return 0;
}
EXPORT_SYMBOL(arch_spin_trylock_retry);

void _raw_read_lock_wait(arch_rwlock_t *rw)
{
	unsigned int owner, old;
	int count = spin_retry;

#ifdef CONFIG_HAVE_MARCH_Z196_FEATURES
	__RAW_LOCK(&rw->lock, -1, __RAW_OP_ADD);
#endif
	owner = 0;
	while (1) {
		if (count-- <= 0) {
			if (owner && !smp_vcpu_scheduled(~owner))
				smp_yield_cpu(~owner);
			count = spin_retry;
		}
		old = ACCESS_ONCE(rw->lock);
		owner = ACCESS_ONCE(rw->owner);
		if ((int) old < 0) {
			if (MACHINE_HAS_CAD)
				_raw_compare_and_delay(&rw->lock, old);
			continue;
		}
		if (_raw_compare_and_swap(&rw->lock, old, old + 1))
			return;
	}
}
EXPORT_SYMBOL(_raw_read_lock_wait);

int _raw_read_trylock_retry(raw_rwlock_t *rw)
int _raw_read_trylock_retry(arch_rwlock_t *rw)
{
	unsigned int old;
	int count = spin_retry;

	while (count-- > 0) {
		if (!__raw_read_can_lock(rw))
			continue;
		old = rw->lock & 0x7fffffffU;
		if (_raw_compare_and_swap(&rw->lock, old, old + 1) == old)
		old = ACCESS_ONCE(rw->lock);
		if ((int) old < 0) {
			if (MACHINE_HAS_CAD)
				_raw_compare_and_delay(&rw->lock, old);
			continue;
		}
		if (_raw_compare_and_swap(&rw->lock, old, old + 1))
			return 1;
	}
	return 0;
}
EXPORT_SYMBOL(_raw_read_trylock_retry);

void _raw_write_lock_wait(raw_rwlock_t *rw)
{
	int count = spin_retry;

	while (1) {
		if (count-- <= 0) {
			_raw_yield();
			count = spin_retry;
		}
		if (!__raw_write_can_lock(rw))
			continue;
		if (_raw_compare_and_swap(&rw->lock, 0, 0x80000000) == 0)
			return;
#ifdef CONFIG_HAVE_MARCH_Z196_FEATURES

void _raw_write_lock_wait(arch_rwlock_t *rw, unsigned int prev)
{
	unsigned int owner, old;
	int count = spin_retry;

	owner = 0;
	while (1) {
		if (count-- <= 0) {
			if (owner && !smp_vcpu_scheduled(~owner))
				smp_yield_cpu(~owner);
			count = spin_retry;
		}
		old = ACCESS_ONCE(rw->lock);
		owner = ACCESS_ONCE(rw->owner);
		smp_mb();
		if ((int) old >= 0) {
			prev = __RAW_LOCK(&rw->lock, 0x80000000, __RAW_OP_OR);
			old = prev;
		}
		if ((old & 0x7fffffff) == 0 && (int) prev >= 0)
			break;
		if (MACHINE_HAS_CAD)
			_raw_compare_and_delay(&rw->lock, old);
	}
}
EXPORT_SYMBOL(_raw_write_lock_wait);

int _raw_write_trylock_retry(raw_rwlock_t *rw)
{
	int count = spin_retry;

	while (count-- > 0) {
		if (!__raw_write_can_lock(rw))
			continue;
		if (_raw_compare_and_swap(&rw->lock, 0, 0x80000000) == 0)
#else /* CONFIG_HAVE_MARCH_Z196_FEATURES */

void _raw_write_lock_wait(arch_rwlock_t *rw)
{
	unsigned int owner, old, prev;
	int count = spin_retry;

	prev = 0x80000000;
	owner = 0;
	while (1) {
		if (count-- <= 0) {
			if (owner && !smp_vcpu_scheduled(~owner))
				smp_yield_cpu(~owner);
			count = spin_retry;
		}
		old = ACCESS_ONCE(rw->lock);
		owner = ACCESS_ONCE(rw->owner);
		if ((int) old >= 0 &&
		    _raw_compare_and_swap(&rw->lock, old, old | 0x80000000))
			prev = old;
		else
			smp_mb();
		if ((old & 0x7fffffff) == 0 && (int) prev >= 0)
			break;
		if (MACHINE_HAS_CAD)
			_raw_compare_and_delay(&rw->lock, old);
	}
}
EXPORT_SYMBOL(_raw_write_lock_wait);

#endif /* CONFIG_HAVE_MARCH_Z196_FEATURES */

int _raw_write_trylock_retry(arch_rwlock_t *rw)
{
	unsigned int old;
	int count = spin_retry;

	while (count-- > 0) {
		old = ACCESS_ONCE(rw->lock);
		if (old) {
			if (MACHINE_HAS_CAD)
				_raw_compare_and_delay(&rw->lock, old);
			continue;
		}
		if (_raw_compare_and_swap(&rw->lock, 0, 0x80000000))
			return 1;
	}
	return 0;
}
EXPORT_SYMBOL(_raw_write_trylock_retry);

void arch_lock_relax(unsigned int cpu)
{
	if (!cpu)
		return;
	if (MACHINE_IS_LPAR && smp_vcpu_scheduled(~cpu))
		return;
	smp_yield_cpu(~cpu);
}
EXPORT_SYMBOL(arch_lock_relax);
