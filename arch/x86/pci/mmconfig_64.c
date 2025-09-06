/*
 * mmconfig.c - Low-level direct PCI config space access via MMCONFIG
 *
 * This is an 64bit optimized version that always keeps the full mmconfig
 * space mapped. This allows lockless config space operation.
 */

#include <linux/pci.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/bitmap.h>
#include <asm/e820.h>

#include "pci.h"

/* Static virtual mapping of the MMCONFIG aperture */
struct mmcfg_virt {
	struct acpi_mcfg_allocation *cfg;
	char __iomem *virt;
};
static struct mmcfg_virt *pci_mmcfg_virt;

static char __iomem *get_virt(unsigned int seg, unsigned bus)
{
	struct acpi_mcfg_allocation *cfg;
	int cfg_num;

	for (cfg_num = 0; cfg_num < pci_mmcfg_config_num; cfg_num++) {
		cfg = pci_mmcfg_virt[cfg_num].cfg;
		if (cfg->pci_segment == seg &&
		    (cfg->start_bus_number <= bus) &&
		    (cfg->end_bus_number >= bus))
			return pci_mmcfg_virt[cfg_num].virt;
	}

	/* Fall back to type 0 */
	return NULL;
}

static char __iomem *pci_dev_base(unsigned int seg, unsigned int bus, unsigned int devfn)
{
	char __iomem *addr;

	addr = get_virt(seg, bus);
	if (!addr)
		return NULL;
 	return addr + ((bus << 20) | (devfn << 12));
#include <linux/rcupdate.h>
#include <asm/e820.h>
#include <asm/pci_x86.h>

#define PREFIX "PCI: "

static char __iomem *pci_dev_base(unsigned int seg, unsigned int bus, unsigned int devfn)
{
	struct pci_mmcfg_region *cfg = pci_mmconfig_lookup(seg, bus);

	if (cfg && cfg->virt)
		return cfg->virt + (PCI_MMCFG_BUS_OFFSET(bus) | (devfn << 12));
	return NULL;
}

static int pci_mmcfg_read(unsigned int seg, unsigned int bus,
			  unsigned int devfn, int reg, int len, u32 *value)
{
	char __iomem *addr;

	/* Why do we have this when nobody checks it. How about a BUG()!? -AK */
	if (unlikely((bus > 255) || (devfn > 255) || (reg > 4095))) {
err:		*value = -1;
		return -EINVAL;
	}

	addr = pci_dev_base(seg, bus, devfn);
	if (!addr)
		goto err;
	rcu_read_lock();
	addr = pci_dev_base(seg, bus, devfn);
	if (!addr) {
		rcu_read_unlock();
		goto err;
	}

	switch (len) {
	case 1:
		*value = mmio_config_readb(addr + reg);
		break;
	case 2:
		*value = mmio_config_readw(addr + reg);
		break;
	case 4:
		*value = mmio_config_readl(addr + reg);
		break;
	}
	rcu_read_unlock();

	return 0;
}

static int pci_mmcfg_write(unsigned int seg, unsigned int bus,
			   unsigned int devfn, int reg, int len, u32 value)
{
	char __iomem *addr;

	/* Why do we have this when nobody checks it. How about a BUG()!? -AK */
	if (unlikely((bus > 255) || (devfn > 255) || (reg > 4095)))
		return -EINVAL;

	addr = pci_dev_base(seg, bus, devfn);
	if (!addr)
		return -EINVAL;
	rcu_read_lock();
	addr = pci_dev_base(seg, bus, devfn);
	if (!addr) {
		rcu_read_unlock();
		return -EINVAL;
	}

	switch (len) {
	case 1:
		mmio_config_writeb(addr + reg, value);
		break;
	case 2:
		mmio_config_writew(addr + reg, value);
		break;
	case 4:
		mmio_config_writel(addr + reg, value);
		break;
	}
	rcu_read_unlock();

	return 0;
}

static struct pci_raw_ops pci_mmcfg = {
const struct pci_raw_ops pci_mmcfg = {
	.read =		pci_mmcfg_read,
	.write =	pci_mmcfg_write,
};

static void __iomem * __init mcfg_ioremap(struct acpi_mcfg_allocation *cfg)
{
	void __iomem *addr;
	u32 size;

	size = (cfg->end_bus_number + 1) << 20;
	addr = ioremap_nocache(cfg->address, size);
	if (addr) {
		printk(KERN_INFO "PCI: Using MMCONFIG at %Lx - %Lx\n",
		       cfg->address, cfg->address + size - 1);
	}
static void __iomem *mcfg_ioremap(struct pci_mmcfg_region *cfg)
{
	void __iomem *addr;
	u64 start, size;
	int num_buses;

	start = cfg->address + PCI_MMCFG_BUS_OFFSET(cfg->start_bus);
	num_buses = cfg->end_bus - cfg->start_bus + 1;
	size = PCI_MMCFG_BUS_OFFSET(num_buses);
	addr = ioremap_nocache(start, size);
	if (addr)
		addr -= PCI_MMCFG_BUS_OFFSET(cfg->start_bus);
	return addr;
}

int __init pci_mmcfg_arch_init(void)
{
	int i;
	pci_mmcfg_virt = kzalloc(sizeof(*pci_mmcfg_virt) *
				 pci_mmcfg_config_num, GFP_KERNEL);
	if (pci_mmcfg_virt == NULL) {
		printk(KERN_ERR "PCI: Can not allocate memory for mmconfig structures\n");
		return 0;
	}

	for (i = 0; i < pci_mmcfg_config_num; ++i) {
		pci_mmcfg_virt[i].cfg = &pci_mmcfg_config[i];
		pci_mmcfg_virt[i].virt = mcfg_ioremap(&pci_mmcfg_config[i]);
		if (!pci_mmcfg_virt[i].virt) {
			printk(KERN_ERR "PCI: Cannot map mmconfig aperture for "
					"segment %d\n",
				pci_mmcfg_config[i].pci_segment);
			pci_mmcfg_arch_free();
			return 0;
		}
	}
	raw_pci_ext_ops = &pci_mmcfg;
	struct pci_mmcfg_region *cfg;

	list_for_each_entry(cfg, &pci_mmcfg_list, list)
		if (pci_mmcfg_arch_map(cfg)) {
			pci_mmcfg_arch_free();
			return 0;
		}

	raw_pci_ext_ops = &pci_mmcfg;

	return 1;
}

void __init pci_mmcfg_arch_free(void)
{
	int i;

	if (pci_mmcfg_virt == NULL)
		return;

	for (i = 0; i < pci_mmcfg_config_num; ++i) {
		if (pci_mmcfg_virt[i].virt) {
			iounmap(pci_mmcfg_virt[i].virt);
			pci_mmcfg_virt[i].virt = NULL;
			pci_mmcfg_virt[i].cfg = NULL;
		}
	}

	kfree(pci_mmcfg_virt);
	pci_mmcfg_virt = NULL;
	struct pci_mmcfg_region *cfg;

	list_for_each_entry(cfg, &pci_mmcfg_list, list)
		pci_mmcfg_arch_unmap(cfg);
}

int pci_mmcfg_arch_map(struct pci_mmcfg_region *cfg)
{
	cfg->virt = mcfg_ioremap(cfg);
	if (!cfg->virt) {
		pr_err(PREFIX "can't map MMCONFIG at %pR\n", &cfg->res);
		return -ENOMEM;
	}

	return 0;
}

void pci_mmcfg_arch_unmap(struct pci_mmcfg_region *cfg)
{
	if (cfg && cfg->virt) {
		iounmap(cfg->virt + PCI_MMCFG_BUS_OFFSET(cfg->start_bus));
		cfg->virt = NULL;
	}
}
