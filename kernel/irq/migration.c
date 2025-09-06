
#include <linux/irq.h>

void set_pending_irq(unsigned int irq, cpumask_t mask)
{
	struct irq_desc *desc = irq_desc + irq;
	unsigned long flags;

	spin_lock_irqsave(&desc->lock, flags);
	desc->status |= IRQ_MOVE_PENDING;
	irq_desc[irq].pending_mask = mask;
	spin_unlock_irqrestore(&desc->lock, flags);
}

void move_masked_irq(int irq)
{
	struct irq_desc *desc = irq_desc + irq;
	cpumask_t tmp;

	if (likely(!(desc->status & IRQ_MOVE_PENDING)))
		return;

	/*
	 * Paranoia: cpu-local interrupts shouldn't be calling in here anyway.
	 */
	if (CHECK_IRQ_PER_CPU(desc->status)) {
#include <linux/interrupt.h>

#include "internals.h"

void irq_move_masked_irq(struct irq_data *idata)
{
	struct irq_desc *desc = irq_data_to_desc(idata);
	struct irq_data *data = &desc->irq_data;
	struct irq_chip *chip = data->chip;

	if (likely(!irqd_is_setaffinity_pending(data)))
		return;

	irqd_clr_move_pending(data);

	/*
	 * Paranoia: cpu-local interrupts shouldn't be calling in here anyway.
	 */
	if (irqd_is_per_cpu(data)) {
		WARN_ON(1);
		return;
	}

	desc->status &= ~IRQ_MOVE_PENDING;

	if (unlikely(cpus_empty(irq_desc[irq].pending_mask)))
		return;

	if (!desc->chip->set_affinity)
		return;

	assert_spin_locked(&desc->lock);

	cpus_and(tmp, irq_desc[irq].pending_mask, cpu_online_map);
	if (unlikely(cpumask_empty(desc->pending_mask)))
		return;

	if (!chip->irq_set_affinity)
		return;

	assert_raw_spin_locked(&desc->lock);

	/*
	 * If there was a valid mask to work with, please
	 * do the disable, re-program, enable sequence.
	 * This is *not* particularly important for level triggered
	 * but in a edge trigger case, we might be setting rte
	 * when an active trigger is comming in. This could
	 * when an active trigger is coming in. This could
	 * cause some ioapics to mal-function.
	 * Being paranoid i guess!
	 *
	 * For correct operation this depends on the caller
	 * masking the irqs.
	 */
	if (likely(!cpus_empty(tmp))) {
		desc->chip->set_affinity(irq,tmp);
	}
	cpus_clear(irq_desc[irq].pending_mask);
}

void move_native_irq(int irq)
{
	struct irq_desc *desc = irq_desc + irq;

	if (likely(!(desc->status & IRQ_MOVE_PENDING)))
		return;

	if (unlikely(desc->status & IRQ_DISABLED))
		return;

	desc->chip->mask(irq);
	move_masked_irq(irq);
	desc->chip->unmask(irq);
}

	if (cpumask_any_and(desc->pending_mask, cpu_online_mask) < nr_cpu_ids)
		irq_do_set_affinity(&desc->irq_data, desc->pending_mask, false);
	if (cpumask_any_and(desc->pending_mask, cpu_online_mask) < nr_cpu_ids) {
		int ret;

		ret = irq_do_set_affinity(data, desc->pending_mask, false);
		/*
		 * If the there is a cleanup pending in the underlying
		 * vector management, reschedule the move for the next
		 * interrupt. Leave desc->pending_mask intact.
		 */
		if (ret == -EBUSY) {
			irqd_set_move_pending(data);
			return;
		}
	}
	cpumask_clear(desc->pending_mask);
}

void irq_move_irq(struct irq_data *idata)
{
	bool masked;

	/*
	 * Get top level irq_data when CONFIG_IRQ_DOMAIN_HIERARCHY is enabled,
	 * and it should be optimized away when CONFIG_IRQ_DOMAIN_HIERARCHY is
	 * disabled. So we avoid an "#ifdef CONFIG_IRQ_DOMAIN_HIERARCHY" here.
	 */
	idata = irq_desc_get_irq_data(irq_data_to_desc(idata));

	if (likely(!irqd_is_setaffinity_pending(idata)))
		return;

	if (unlikely(irqd_irq_disabled(idata)))
		return;

	/*
	 * Be careful vs. already masked interrupts. If this is a
	 * threaded interrupt with ONESHOT set, we can end up with an
	 * interrupt storm.
	 */
	masked = irqd_irq_masked(idata);
	if (!masked)
		idata->chip->irq_mask(idata);
	irq_move_masked_irq(idata);
	if (!masked)
		idata->chip->irq_unmask(idata);
}
