/*
 * MSI hooks for standard x86 apic
 */

#include <linux/pci.h>
#include <linux/irq.h>
#include <linux/msi.h>
#include <asm/smp.h>

/*
 * Shifts for APIC-based data
 */

#define MSI_DATA_VECTOR_SHIFT		0
#define	    MSI_DATA_VECTOR(v)		(((u8)v) << MSI_DATA_VECTOR_SHIFT)
#define MSI_DATA_VECTOR_MASK		0xffffff00

#define MSI_DATA_DELIVERY_SHIFT		8
#define     MSI_DATA_DELIVERY_FIXED	(0 << MSI_DATA_DELIVERY_SHIFT)
#define     MSI_DATA_DELIVERY_LOWPRI	(1 << MSI_DATA_DELIVERY_SHIFT)

#define MSI_DATA_LEVEL_SHIFT		14
#define     MSI_DATA_LEVEL_DEASSERT	(0 << MSI_DATA_LEVEL_SHIFT)
#define     MSI_DATA_LEVEL_ASSERT	(1 << MSI_DATA_LEVEL_SHIFT)

#define MSI_DATA_TRIGGER_SHIFT		15
#define     MSI_DATA_TRIGGER_EDGE	(0 << MSI_DATA_TRIGGER_SHIFT)
#define     MSI_DATA_TRIGGER_LEVEL	(1 << MSI_DATA_TRIGGER_SHIFT)

/*
 * Shift/mask fields for APIC-based bus address
 */

#define MSI_TARGET_CPU_SHIFT		4
#define MSI_ADDR_HEADER			0xfee00000

#define MSI_ADDR_DESTID_MASK		0xfff0000f
#define     MSI_ADDR_DESTID_CPU(cpu)	((cpu) << MSI_TARGET_CPU_SHIFT)

#define MSI_ADDR_DESTMODE_SHIFT		2
#define     MSI_ADDR_DESTMODE_PHYS	(0 << MSI_ADDR_DESTMODE_SHIFT)
#define	    MSI_ADDR_DESTMODE_LOGIC	(1 << MSI_ADDR_DESTMODE_SHIFT)

#define MSI_ADDR_REDIRECTION_SHIFT	3
#define     MSI_ADDR_REDIRECTION_CPU	(0 << MSI_ADDR_REDIRECTION_SHIFT)
#define     MSI_ADDR_REDIRECTION_LOWPRI	(1 << MSI_ADDR_REDIRECTION_SHIFT)
#include <linux/dmar.h>
#include <asm/smp.h>
#include <asm/msidef.h>

static struct irq_chip	ia64_msi_chip;

#ifdef CONFIG_SMP
static void ia64_set_msi_irq_affinity(unsigned int irq, cpumask_t cpu_mask)
{
	struct msi_msg msg;
	u32 addr, data;
	int cpu = first_cpu(cpu_mask);

	if (!cpu_online(cpu))
		return;

	if (irq_prepare_move(irq, cpu))
		return;

	read_msi_msg(irq, &msg);

	addr = msg.address_lo;
	addr &= MSI_ADDR_DESTID_MASK;
	addr |= MSI_ADDR_DESTID_CPU(cpu_physical_id(cpu));
static int ia64_set_msi_irq_affinity(struct irq_data *idata,
				     const cpumask_t *cpu_mask, bool force)
{
	struct msi_msg msg;
	u32 addr, data;
	int cpu = cpumask_first_and(cpu_mask, cpu_online_mask);
	unsigned int irq = idata->irq;

	if (irq_prepare_move(irq, cpu))
		return -1;

	__get_cached_msi_msg(irq_data_get_msi_desc(idata), &msg);

	addr = msg.address_lo;
	addr &= MSI_ADDR_DEST_ID_MASK;
	addr |= MSI_ADDR_DEST_ID_CPU(cpu_physical_id(cpu));
	msg.address_lo = addr;

	data = msg.data;
	data &= MSI_DATA_VECTOR_MASK;
	data |= MSI_DATA_VECTOR(irq_to_vector(irq));
	msg.data = data;

	write_msi_msg(irq, &msg);
	irq_desc[irq].affinity = cpumask_of_cpu(cpu);
	pci_write_msi_msg(irq, &msg);
	cpumask_copy(irq_data_get_affinity_mask(idata), cpumask_of(cpu));

	return 0;
}
#endif /* CONFIG_SMP */

int ia64_setup_msi_irq(struct pci_dev *pdev, struct msi_desc *desc)
{
	struct msi_msg	msg;
	unsigned long	dest_phys_id;
	int	irq, vector;
	cpumask_t mask;

	irq = create_irq();
	if (irq < 0)
		return irq;

	set_irq_msi(irq, desc);
	cpus_and(mask, irq_to_domain(irq), cpu_online_map);
	dest_phys_id = cpu_physical_id(first_cpu(mask));
	irq_set_msi_desc(irq, desc);
	dest_phys_id = cpu_physical_id(cpumask_any_and(&(irq_to_domain(irq)),
						       cpu_online_mask));
	vector = irq_to_vector(irq);

	msg.address_hi = 0;
	msg.address_lo =
		MSI_ADDR_HEADER |
		MSI_ADDR_DESTMODE_PHYS |
		MSI_ADDR_REDIRECTION_CPU |
		MSI_ADDR_DESTID_CPU(dest_phys_id);
		MSI_ADDR_DEST_MODE_PHYS |
		MSI_ADDR_REDIRECTION_CPU |
		MSI_ADDR_DEST_ID_CPU(dest_phys_id);

	msg.data =
		MSI_DATA_TRIGGER_EDGE |
		MSI_DATA_LEVEL_ASSERT |
		MSI_DATA_DELIVERY_FIXED |
		MSI_DATA_VECTOR(vector);

	write_msi_msg(irq, &msg);
	set_irq_chip_and_handler(irq, &ia64_msi_chip, handle_edge_irq);
	pci_write_msi_msg(irq, &msg);
	irq_set_chip_and_handler(irq, &ia64_msi_chip, handle_edge_irq);

	return 0;
}

void ia64_teardown_msi_irq(unsigned int irq)
{
	destroy_irq(irq);
}

static void ia64_ack_msi_irq(unsigned int irq)
{
	irq_complete_move(irq);
	move_native_irq(irq);
	ia64_eoi();
}

static int ia64_msi_retrigger_irq(unsigned int irq)
{
	unsigned int vector = irq_to_vector(irq);
static void ia64_ack_msi_irq(struct irq_data *data)
{
	irq_complete_move(data->irq);
	irq_move_irq(data);
	ia64_eoi();
}

static int ia64_msi_retrigger_irq(struct irq_data *data)
{
	unsigned int vector = irq_to_vector(data->irq);
	ia64_resend_irq(vector);

	return 1;
}

/*
 * Generic ops used on most IA64 platforms.
 */
static struct irq_chip ia64_msi_chip = {
	.name		= "PCI-MSI",
	.mask		= mask_msi_irq,
	.unmask		= unmask_msi_irq,
	.ack		= ia64_ack_msi_irq,
#ifdef CONFIG_SMP
	.set_affinity	= ia64_set_msi_irq_affinity,
#endif
	.retrigger	= ia64_msi_retrigger_irq,
	.name			= "PCI-MSI",
	.irq_mask		= pci_msi_mask_irq,
	.irq_unmask		= pci_msi_unmask_irq,
	.irq_ack		= ia64_ack_msi_irq,
#ifdef CONFIG_SMP
	.irq_set_affinity	= ia64_set_msi_irq_affinity,
#endif
	.irq_retrigger		= ia64_msi_retrigger_irq,
};


int arch_setup_msi_irq(struct pci_dev *pdev, struct msi_desc *desc)
{
	if (platform_setup_msi_irq)
		return platform_setup_msi_irq(pdev, desc);

	return ia64_setup_msi_irq(pdev, desc);
}

void arch_teardown_msi_irq(unsigned int irq)
{
	if (platform_teardown_msi_irq)
		return platform_teardown_msi_irq(irq);

	return ia64_teardown_msi_irq(irq);
}

#ifdef CONFIG_INTEL_IOMMU
#ifdef CONFIG_SMP
static int dmar_msi_set_affinity(struct irq_data *data,
				 const struct cpumask *mask, bool force)
{
	unsigned int irq = data->irq;
	struct irq_cfg *cfg = irq_cfg + irq;
	struct msi_msg msg;
	int cpu = cpumask_first_and(mask, cpu_online_mask);

	if (irq_prepare_move(irq, cpu))
		return -1;

	dmar_msi_read(irq, &msg);

	msg.data &= ~MSI_DATA_VECTOR_MASK;
	msg.data |= MSI_DATA_VECTOR(cfg->vector);
	msg.address_lo &= ~MSI_ADDR_DEST_ID_MASK;
	msg.address_lo |= MSI_ADDR_DEST_ID_CPU(cpu_physical_id(cpu));

	dmar_msi_write(irq, &msg);
	cpumask_copy(irq_data_get_affinity_mask(data), mask);

	return 0;
}
#endif /* CONFIG_SMP */

static struct irq_chip dmar_msi_type = {
	.name = "DMAR_MSI",
	.irq_unmask = dmar_msi_unmask,
	.irq_mask = dmar_msi_mask,
	.irq_ack = ia64_ack_msi_irq,
#ifdef CONFIG_SMP
	.irq_set_affinity = dmar_msi_set_affinity,
#endif
	.irq_retrigger = ia64_msi_retrigger_irq,
};

static void
msi_compose_msg(struct pci_dev *pdev, unsigned int irq, struct msi_msg *msg)
{
	struct irq_cfg *cfg = irq_cfg + irq;
	unsigned dest;

	dest = cpu_physical_id(cpumask_first_and(&(irq_to_domain(irq)),
						 cpu_online_mask));

	msg->address_hi = 0;
	msg->address_lo =
		MSI_ADDR_HEADER |
		MSI_ADDR_DEST_MODE_PHYS |
		MSI_ADDR_REDIRECTION_CPU |
		MSI_ADDR_DEST_ID_CPU(dest);

	msg->data =
		MSI_DATA_TRIGGER_EDGE |
		MSI_DATA_LEVEL_ASSERT |
		MSI_DATA_DELIVERY_FIXED |
		MSI_DATA_VECTOR(cfg->vector);
}

int dmar_alloc_hwirq(int id, int node, void *arg)
{
	int irq;
	struct msi_msg msg;

	irq = create_irq();
	if (irq > 0) {
		irq_set_handler_data(irq, arg);
		irq_set_chip_and_handler_name(irq, &dmar_msi_type,
					      handle_edge_irq, "edge");
		msi_compose_msg(NULL, irq, &msg);
		dmar_msi_write(irq, &msg);
	}

	return irq;
}

void dmar_free_hwirq(int irq)
{
	irq_set_handler_data(irq, NULL);
	destroy_irq(irq);
}
#endif /* CONFIG_INTEL_IOMMU */

