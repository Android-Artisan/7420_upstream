/*
 * File:	portdrv_core.c
 * Purpose:	PCI Express Port Bus Driver's Core Functions
 *
 * Copyright (C) 2004 Intel
 * Copyright (C) Tom Long Nguyen (tom.l.nguyen@intel.com)
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/pm.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/pcieport_if.h>

#include "portdrv.h"

extern int pcie_mch_quirk;	/* MSI-quirk Indicator */

static int pcie_port_probe_service(struct device *dev)
{
	struct pcie_device *pciedev;
	struct pcie_port_service_driver *driver;
	int status;

	if (!dev || !dev->driver)
		return -ENODEV;

 	driver = to_service_driver(dev->driver);
	if (!driver || !driver->probe)
		return -ENODEV;

	pciedev = to_pcie_device(dev);
	status = driver->probe(pciedev, driver->id_table);
	if (!status) {
		dev_printk(KERN_DEBUG, dev, "service driver %s loaded\n",
			driver->name);
		get_device(dev);
	}
	return status;
}

static int pcie_port_remove_service(struct device *dev)
{
	struct pcie_device *pciedev;
	struct pcie_port_service_driver *driver;

	if (!dev || !dev->driver)
		return 0;

	pciedev = to_pcie_device(dev);
 	driver = to_service_driver(dev->driver);
	if (driver && driver->remove) { 
		dev_printk(KERN_DEBUG, dev, "unloading service driver %s\n",
			driver->name);
		driver->remove(pciedev);
		put_device(dev);
	}
	return 0;
}

static void pcie_port_shutdown_service(struct device *dev) {}

static int pcie_port_suspend_service(struct device *dev, pm_message_t state)
{
	struct pcie_device *pciedev;
	struct pcie_port_service_driver *driver;

	if (!dev || !dev->driver)
		return 0;

	pciedev = to_pcie_device(dev);
 	driver = to_service_driver(dev->driver);
	if (driver && driver->suspend)
		driver->suspend(pciedev, state);
	return 0;
}

static int pcie_port_resume_service(struct device *dev)
{
	struct pcie_device *pciedev;
	struct pcie_port_service_driver *driver;

	if (!dev || !dev->driver)
		return 0;

	pciedev = to_pcie_device(dev);
 	driver = to_service_driver(dev->driver);

	if (driver && driver->resume)
		driver->resume(pciedev);
	return 0;
}

/*
 * release_pcie_device
 *	
 *	Being invoked automatically when device is being removed 
 *	in response to device_unregister(dev) call.
 *	Release all resources being claimed.
 */
static void release_pcie_device(struct device *dev)
{
	dev_printk(KERN_DEBUG, dev, "free port service\n");
	kfree(to_pcie_device(dev));			
}

static int is_msi_quirked(struct pci_dev *dev)
{
	int port_type, quirk = 0;
	u16 reg16;

	pci_read_config_word(dev, 
		pci_find_capability(dev, PCI_CAP_ID_EXP) + 
		PCIE_CAPABILITIES_REG, &reg16);
	port_type = (reg16 >> 4) & PORT_TYPE_MASK;
	switch(port_type) {
	case PCIE_RC_PORT:
		if (pcie_mch_quirk == 1)
			quirk = 1;
		break;
	case PCIE_SW_UPSTREAM_PORT:
	case PCIE_SW_DOWNSTREAM_PORT:
	default:
		break;	
	}
	return quirk;
}
	
static int assign_interrupt_mode(struct pci_dev *dev, int *vectors, int mask)
{
	int i, pos, nvec, status = -EINVAL;
	int interrupt_mode = PCIE_PORT_INTx_MODE;

	/* Set INTx as default */
	for (i = 0, nvec = 0; i < PCIE_PORT_DEVICE_MAXSERVICES; i++) {
		if (mask & (1 << i)) 
			nvec++;
		vectors[i] = dev->irq;
	}
	
	/* Check MSI quirk */
	if (is_msi_quirked(dev))
		return interrupt_mode;

	/* Select MSI-X over MSI if supported */		
	pos = pci_find_capability(dev, PCI_CAP_ID_MSIX);
	if (pos) {
		struct msix_entry msix_entries[PCIE_PORT_DEVICE_MAXSERVICES] = 
			{{0, 0}, {0, 1}, {0, 2}, {0, 3}};
		dev_info(&dev->dev, "found MSI-X capability\n");
		status = pci_enable_msix(dev, msix_entries, nvec);
		if (!status) {
			int j = 0;

			interrupt_mode = PCIE_PORT_MSIX_MODE;
			for (i = 0; i < PCIE_PORT_DEVICE_MAXSERVICES; i++) {
				if (mask & (1 << i)) 
					vectors[i] = msix_entries[j++].vector;
			}
		}
	} 
	if (status) {
		pos = pci_find_capability(dev, PCI_CAP_ID_MSI);
		if (pos) {
			dev_info(&dev->dev, "found MSI capability\n");
			status = pci_enable_msi(dev);
			if (!status) {
				interrupt_mode = PCIE_PORT_MSI_MODE;
				for (i = 0;i < PCIE_PORT_DEVICE_MAXSERVICES;i++)
					vectors[i] = dev->irq;
			}
		}
	} 
	return interrupt_mode;
}

static int get_port_device_capability(struct pci_dev *dev)
{
	int services = 0, pos;
	u16 reg16;
	u32 reg32;

	pos = pci_find_capability(dev, PCI_CAP_ID_EXP);
	pci_read_config_word(dev, pos + PCIE_CAPABILITIES_REG, &reg16);
	/* Hot-Plug Capable */
	if (reg16 & PORT_TO_SLOT_MASK) {
		pci_read_config_dword(dev, 
			pos + PCIE_SLOT_CAPABILITIES_REG, &reg32);
		if (reg32 & SLOT_HP_CAPABLE_MASK)
			services |= PCIE_PORT_SERVICE_HP;
	} 
	/* PME Capable - root port capability */
	if (((reg16 >> 4) & PORT_TYPE_MASK) == PCIE_RC_PORT)
		services |= PCIE_PORT_SERVICE_PME;
	
	pos = PCI_CFG_SPACE_SIZE;
	while (pos) {
		pci_read_config_dword(dev, pos, &reg32);
		switch (reg32 & 0xffff) {
		case PCI_EXT_CAP_ID_ERR:
			services |= PCIE_PORT_SERVICE_AER;
			pos = reg32 >> 20;
			break;
		case PCI_EXT_CAP_ID_VC:
			services |= PCIE_PORT_SERVICE_VC;
			pos = reg32 >> 20;
			break;
		default:
			pos = 0;
			break;
		}
	}
#include <linux/aer.h>

#include "../pci.h"
#include "portdrv.h"

bool pciehp_msi_disabled;

static int __init pciehp_setup(char *str)
{
	if (!strncmp(str, "nomsi", 5))
		pciehp_msi_disabled = true;

	return 1;
}
__setup("pcie_hp=", pciehp_setup);

/**
 * release_pcie_device - free PCI Express port service device structure
 * @dev: Port service device to release
 *
 * Invoked automatically when device is being removed in response to
 * device_unregister(dev).  Release all resources being claimed.
 */
static void release_pcie_device(struct device *dev)
{
	kfree(to_pcie_device(dev));
}

/**
 * pcie_port_msix_add_entry - add entry to given array of MSI-X entries
 * @entries: Array of MSI-X entries
 * @new_entry: Index of the entry to add to the array
 * @nr_entries: Number of entries already in the array
 *
 * Return value: Position of the added entry in the array
 */
static int pcie_port_msix_add_entry(
	struct msix_entry *entries, int new_entry, int nr_entries)
{
	int j;

	for (j = 0; j < nr_entries; j++)
		if (entries[j].entry == new_entry)
			return j;

	entries[j].entry = new_entry;
	return j;
}

/**
 * pcie_port_enable_msix - try to set up MSI-X as interrupt mode for given port
 * @dev: PCI Express port to handle
 * @vectors: Array of interrupt vectors to populate
 * @mask: Bitmask of port capabilities returned by get_port_device_capability()
 *
 * Return value: 0 on success, error code on failure
 */
static int pcie_port_enable_msix(struct pci_dev *dev, int *vectors, int mask)
{
	struct msix_entry *msix_entries;
	int idx[PCIE_PORT_DEVICE_MAXSERVICES];
	int nr_entries, status, pos, i, nvec;
	u16 reg16;
	u32 reg32;

	nr_entries = pci_msix_vec_count(dev);
	if (nr_entries < 0)
		return nr_entries;
	BUG_ON(!nr_entries);
	if (nr_entries > PCIE_PORT_MAX_MSIX_ENTRIES)
		nr_entries = PCIE_PORT_MAX_MSIX_ENTRIES;

	msix_entries = kzalloc(sizeof(*msix_entries) * nr_entries, GFP_KERNEL);
	if (!msix_entries)
		return -ENOMEM;

	/*
	 * Allocate as many entries as the port wants, so that we can check
	 * which of them will be useful.  Moreover, if nr_entries is correctly
	 * equal to the number of entries this port actually uses, we'll happily
	 * go through without any tricks.
	 */
	for (i = 0; i < nr_entries; i++)
		msix_entries[i].entry = i;

	status = pci_enable_msix_exact(dev, msix_entries, nr_entries);
	if (status)
		goto Exit;

	for (i = 0; i < PCIE_PORT_DEVICE_MAXSERVICES; i++)
		idx[i] = -1;
	status = -EIO;
	nvec = 0;

	if (mask & (PCIE_PORT_SERVICE_PME | PCIE_PORT_SERVICE_HP)) {
		int entry;

		/*
		 * The code below follows the PCI Express Base Specification 2.0
		 * stating in Section 6.1.6 that "PME and Hot-Plug Event
		 * interrupts (when both are implemented) always share the same
		 * MSI or MSI-X vector, as indicated by the Interrupt Message
		 * Number field in the PCI Express Capabilities register", where
		 * according to Section 7.8.2 of the specification "For MSI-X,
		 * the value in this field indicates which MSI-X Table entry is
		 * used to generate the interrupt message."
		 */
		pcie_capability_read_word(dev, PCI_EXP_FLAGS, &reg16);
		entry = (reg16 & PCI_EXP_FLAGS_IRQ) >> 9;
		if (entry >= nr_entries)
			goto Error;

		i = pcie_port_msix_add_entry(msix_entries, entry, nvec);
		if (i == nvec)
			nvec++;

		idx[PCIE_PORT_SERVICE_PME_SHIFT] = i;
		idx[PCIE_PORT_SERVICE_HP_SHIFT] = i;
	}

	if (mask & PCIE_PORT_SERVICE_AER) {
		int entry;

		/*
		 * The code below follows Section 7.10.10 of the PCI Express
		 * Base Specification 2.0 stating that bits 31-27 of the Root
		 * Error Status Register contain a value indicating which of the
		 * MSI/MSI-X vectors assigned to the port is going to be used
		 * for AER, where "For MSI-X, the value in this register
		 * indicates which MSI-X Table entry is used to generate the
		 * interrupt message."
		 */
		pos = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_ERR);
		pci_read_config_dword(dev, pos + PCI_ERR_ROOT_STATUS, &reg32);
		entry = reg32 >> 27;
		if (entry >= nr_entries)
			goto Error;

		i = pcie_port_msix_add_entry(msix_entries, entry, nvec);
		if (i == nvec)
			nvec++;

		idx[PCIE_PORT_SERVICE_AER_SHIFT] = i;
	}

	/*
	 * If nvec is equal to the allocated number of entries, we can just use
	 * what we have.  Otherwise, the port has some extra entries not for the
	 * services we know and we need to work around that.
	 */
	if (nvec == nr_entries) {
		status = 0;
	} else {
		/* Drop the temporary MSI-X setup */
		pci_disable_msix(dev);

		/* Now allocate the MSI-X vectors for real */
		status = pci_enable_msix_exact(dev, msix_entries, nvec);
		if (status)
			goto Exit;
	}

	for (i = 0; i < PCIE_PORT_DEVICE_MAXSERVICES; i++)
		vectors[i] = idx[i] >= 0 ? msix_entries[idx[i]].vector : -1;

 Exit:
	kfree(msix_entries);
	return status;

 Error:
	pci_disable_msix(dev);
	goto Exit;
}

/**
 * init_service_irqs - initialize irqs for PCI Express port services
 * @dev: PCI Express port to handle
 * @irqs: Array of irqs to populate
 * @mask: Bitmask of port capabilities returned by get_port_device_capability()
 *
 * Return value: Interrupt mode associated with the port
 */
static int init_service_irqs(struct pci_dev *dev, int *irqs, int mask)
{
	int i, irq = -1;

	/*
	 * If MSI cannot be used for PCIe PME or hotplug, we have to use
	 * INTx or other interrupts, e.g. system shared interrupt.
	 */
	if (((mask & PCIE_PORT_SERVICE_PME) && pcie_pme_no_msi()) ||
	    ((mask & PCIE_PORT_SERVICE_HP) && pciehp_no_msi())) {
		if (dev->irq)
			irq = dev->irq;
		goto no_msi;
	}

	/* Try to use MSI-X if supported */
	if (!pcie_port_enable_msix(dev, irqs, mask))
		return 0;

	/*
	 * We're not going to use MSI-X, so try MSI and fall back to INTx.
	 * If neither MSI/MSI-X nor INTx available, try other interrupt.  On
	 * some platforms, root port doesn't support MSI/MSI-X/INTx in RC mode.
	 */
	if (!pci_enable_msi(dev) || dev->irq)
		irq = dev->irq;

 no_msi:
	for (i = 0; i < PCIE_PORT_DEVICE_MAXSERVICES; i++)
		irqs[i] = irq;
	irqs[PCIE_PORT_SERVICE_VC_SHIFT] = -1;

	if (irq < 0)
		return -ENODEV;
	return 0;
}

static void cleanup_service_irqs(struct pci_dev *dev)
{
	if (dev->msix_enabled)
		pci_disable_msix(dev);
	else if (dev->msi_enabled)
		pci_disable_msi(dev);
}

/**
 * get_port_device_capability - discover capabilities of a PCI Express port
 * @dev: PCI Express port to examine
 *
 * The capabilities are read from the port's PCI Express configuration registers
 * as described in PCI Express Base Specification 1.0a sections 7.8.2, 7.8.9 and
 * 7.9 - 7.11.
 *
 * Return value: Bitmask of discovered port capabilities
 */
static int get_port_device_capability(struct pci_dev *dev)
{
	int services = 0;
	u32 reg32;
	int cap_mask = 0;
	int err;

	if (pcie_ports_disabled)
		return 0;

	cap_mask = PCIE_PORT_SERVICE_PME | PCIE_PORT_SERVICE_HP
			| PCIE_PORT_SERVICE_VC;
	if (pci_aer_available())
		cap_mask |= PCIE_PORT_SERVICE_AER;

	if (pcie_ports_auto) {
		err = pcie_port_platform_notify(dev, &cap_mask);
		if (err)
			return 0;
	}

	/* Hot-Plug Capable */
	if ((cap_mask & PCIE_PORT_SERVICE_HP) &&
	    pcie_caps_reg(dev) & PCI_EXP_FLAGS_SLOT) {
		pcie_capability_read_dword(dev, PCI_EXP_SLTCAP, &reg32);
		if (reg32 & PCI_EXP_SLTCAP_HPC) {
			services |= PCIE_PORT_SERVICE_HP;
			/*
			 * Disable hot-plug interrupts in case they have been
			 * enabled by the BIOS and the hot-plug service driver
			 * is not loaded.
			 */
			pcie_capability_clear_word(dev, PCI_EXP_SLTCTL,
				PCI_EXP_SLTCTL_CCIE | PCI_EXP_SLTCTL_HPIE);
		}
	}
	/* AER capable */
	if ((cap_mask & PCIE_PORT_SERVICE_AER)
	    && pci_find_ext_capability(dev, PCI_EXT_CAP_ID_ERR)) {
		services |= PCIE_PORT_SERVICE_AER;
		/*
		 * Disable AER on this port in case it's been enabled by the
		 * BIOS (the AER service driver will enable it when necessary).
		 */
		pci_disable_pcie_error_reporting(dev);
	}
	/* VC support */
	if (pci_find_ext_capability(dev, PCI_EXT_CAP_ID_VC))
		services |= PCIE_PORT_SERVICE_VC;
	/* Root ports are capable of generating PME too */
	if ((cap_mask & PCIE_PORT_SERVICE_PME)
	    && pci_pcie_type(dev) == PCI_EXP_TYPE_ROOT_PORT) {
		services |= PCIE_PORT_SERVICE_PME;
		/*
		 * Disable PME interrupt on this port in case it's been enabled
		 * by the BIOS (the PME service driver will enable it when
		 * necessary).
		 */
		pcie_pme_interrupt_enable(dev, false);
	}

	return services;
}

static void pcie_device_init(struct pci_dev *parent, struct pcie_device *dev, 
	int port_type, int service_type, int irq, int irq_mode)
{
	struct device *device;

	dev->port = parent;
	dev->interrupt_mode = irq_mode;
	dev->irq = irq;
	dev->id.vendor = parent->vendor;
	dev->id.device = parent->device;
	dev->id.port_type = port_type;
	dev->id.service_type = (1 << service_type);

	/* Initialize generic device interface */
	device = &dev->device;
	memset(device, 0, sizeof(struct device));
	device->bus = &pcie_port_bus_type;
	device->driver = NULL;
	device->driver_data = NULL;
	device->release = release_pcie_device;	/* callback to free pcie dev */
	snprintf(device->bus_id, sizeof(device->bus_id), "%s:pcie%02x",
		 pci_name(parent), get_descriptor_id(port_type, service_type));
	device->parent = &parent->dev;
}

static struct pcie_device* alloc_pcie_device(struct pci_dev *parent,
	int port_type, int service_type, int irq, int irq_mode)
{
	struct pcie_device *device;

	device = kzalloc(sizeof(struct pcie_device), GFP_KERNEL);
	if (!device)
		return NULL;

	pcie_device_init(parent, device, port_type, service_type, irq,irq_mode);
	dev_printk(KERN_DEBUG, &device->device, "allocate port service\n");
	return device;
}

int pcie_port_device_probe(struct pci_dev *dev)
{
	int pos, type;
	u16 reg;

	if (!(pos = pci_find_capability(dev, PCI_CAP_ID_EXP)))
		return -ENODEV;

	pci_read_config_word(dev, pos + PCIE_CAPABILITIES_REG, &reg);
	type = (reg >> 4) & PORT_TYPE_MASK;
	if (	type == PCIE_RC_PORT || type == PCIE_SW_UPSTREAM_PORT ||
		type == PCIE_SW_DOWNSTREAM_PORT )
		return 0;

	return -ENODEV;
}

int pcie_port_device_register(struct pci_dev *dev)
{
	struct pcie_port_device_ext *p_ext;
	int status, type, capabilities, irq_mode, i;
	int vectors[PCIE_PORT_DEVICE_MAXSERVICES];
	u16 reg16;

	/* Allocate port device extension */
	if (!(p_ext = kmalloc(sizeof(struct pcie_port_device_ext), GFP_KERNEL)))
		return -ENOMEM;

	pci_set_drvdata(dev, p_ext);

	/* Get port type */
	pci_read_config_word(dev,
		pci_find_capability(dev, PCI_CAP_ID_EXP) +
		PCIE_CAPABILITIES_REG, &reg16);
	type = (reg16 >> 4) & PORT_TYPE_MASK;

	/* Now get port services */
	capabilities = get_port_device_capability(dev);
	irq_mode = assign_interrupt_mode(dev, vectors, capabilities);
	p_ext->interrupt_mode = irq_mode;

	/* Allocate child services if any */
	for (i = 0; i < PCIE_PORT_DEVICE_MAXSERVICES; i++) {
		struct pcie_device *child;

		if (capabilities & (1 << i)) {
			child = alloc_pcie_device(
				dev, 		/* parent */
				type,		/* port type */
				i,		/* service type */
				vectors[i],	/* irq */
				irq_mode	/* interrupt mode */);
			if (child) {
				status = device_register(&child->device);
				if (status) {
					kfree(child);
					continue;
				}
				get_device(&child->device);
			}
		}
	}
	return 0;
/**
 * pcie_device_init - allocate and initialize PCI Express port service device
 * @pdev: PCI Express port to associate the service device with
 * @service: Type of service to associate with the service device
 * @irq: Interrupt vector to associate with the service device
 */
static int pcie_device_init(struct pci_dev *pdev, int service, int irq)
{
	int retval;
	struct pcie_device *pcie;
	struct device *device;

	pcie = kzalloc(sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;
	pcie->port = pdev;
	pcie->irq = irq;
	pcie->service = service;

	/* Initialize generic device interface */
	device = &pcie->device;
	device->bus = &pcie_port_bus_type;
	device->release = release_pcie_device;	/* callback to free pcie dev */
	dev_set_name(device, "%s:pcie%02x",
		     pci_name(pdev),
		     get_descriptor_id(pci_pcie_type(pdev), service));
	device->parent = &pdev->dev;
	device_enable_async_suspend(device);

	retval = device_register(device);
	if (retval) {
		put_device(device);
		return retval;
	}

	return 0;
}

/**
 * pcie_port_device_register - register PCI Express port
 * @dev: PCI Express port to register
 *
 * Allocate the port extension structure and register services associated with
 * the port.
 */
int pcie_port_device_register(struct pci_dev *dev)
{
	int status, capabilities, i, nr_service;
	int irqs[PCIE_PORT_DEVICE_MAXSERVICES];

	/* Enable PCI Express port device */
	status = pci_enable_device(dev);
	if (status)
		return status;

	/* Get and check PCI Express port services */
	capabilities = get_port_device_capability(dev);
	if (!capabilities)
		return 0;

	pci_set_master(dev);
	/*
	 * Initialize service irqs. Don't use service devices that
	 * require interrupts if there is no way to generate them.
	 * However, some drivers may have a polling mode (e.g. pciehp_poll_mode)
	 * that can be used in the absence of irqs.  Allow them to determine
	 * if that is to be used.
	 */
	status = init_service_irqs(dev, irqs, capabilities);
	if (status) {
		capabilities &= PCIE_PORT_SERVICE_VC | PCIE_PORT_SERVICE_HP;
		if (!capabilities)
			goto error_disable;
	}

	/* Allocate child services if any */
	status = -ENODEV;
	nr_service = 0;
	for (i = 0; i < PCIE_PORT_DEVICE_MAXSERVICES; i++) {
		int service = 1 << i;
		if (!(capabilities & service))
			continue;
		if (!pcie_device_init(dev, service, irqs[i]))
			nr_service++;
	}
	if (!nr_service)
		goto error_cleanup_irqs;

	return 0;

error_cleanup_irqs:
	cleanup_service_irqs(dev);
error_disable:
	pci_disable_device(dev);
	return status;
}

#ifdef CONFIG_PM
static int suspend_iter(struct device *dev, void *data)
{
	struct pcie_port_service_driver *service_driver;
	pm_message_t state = * (pm_message_t *) data;

 	if ((dev->bus == &pcie_port_bus_type) &&
 	    (dev->driver)) {
 		service_driver = to_service_driver(dev->driver);
 		if (service_driver->suspend)
 			service_driver->suspend(to_pcie_device(dev), state);
  	}
	return 0;
}

int pcie_port_device_suspend(struct pci_dev *dev, pm_message_t state)
{
	return device_for_each_child(&dev->dev, &state, suspend_iter);

	if ((dev->bus == &pcie_port_bus_type) && dev->driver) {
		service_driver = to_service_driver(dev->driver);
		if (service_driver->suspend)
			service_driver->suspend(to_pcie_device(dev));
	}
	return 0;
}

/**
 * pcie_port_device_suspend - suspend port services associated with a PCIe port
 * @dev: PCI Express port to handle
 */
int pcie_port_device_suspend(struct device *dev)
{
	return device_for_each_child(dev, NULL, suspend_iter);
}

static int resume_iter(struct device *dev, void *data)
{
	struct pcie_port_service_driver *service_driver;

	if ((dev->bus == &pcie_port_bus_type) &&
	    (dev->driver)) {
		service_driver = to_service_driver(dev->driver);
		if (service_driver->resume)
			service_driver->resume(to_pcie_device(dev));
	}
	return 0;
}

int pcie_port_device_resume(struct pci_dev *dev)
{
	return device_for_each_child(&dev->dev, NULL, resume_iter);
}
#endif

static int remove_iter(struct device *dev, void *data)
{
	struct pcie_port_service_driver *service_driver;

	if (dev->bus == &pcie_port_bus_type) {
		if (dev->driver) {
			service_driver = to_service_driver(dev->driver);
			if (service_driver->remove)
				service_driver->remove(to_pcie_device(dev));
		}
		*(unsigned long*)data = (unsigned long)dev;
		return 1;
/**
 * pcie_port_device_resume - resume port services associated with a PCIe port
 * @dev: PCI Express port to handle
 */
int pcie_port_device_resume(struct device *dev)
{
	return device_for_each_child(dev, NULL, resume_iter);
}
#endif /* PM */

static int remove_iter(struct device *dev, void *data)
{
	if (dev->bus == &pcie_port_bus_type)
		device_unregister(dev);
	return 0;
}

/**
 * pcie_port_device_remove - unregister PCI Express port service devices
 * @dev: PCI Express port the service devices to unregister are associated with
 *
 * Remove PCI Express port service devices associated with given port and
 * disable MSI-X or MSI for the port.
 */
void pcie_port_device_remove(struct pci_dev *dev)
{
	device_for_each_child(&dev->dev, NULL, remove_iter);
	cleanup_service_irqs(dev);
	pci_disable_device(dev);
}

/**
 * pcie_port_probe_service - probe driver for given PCI Express port service
 * @dev: PCI Express port service device to probe against
 *
 * If PCI Express port service driver is registered with
 * pcie_port_service_register(), this function will be called by the driver core
 * whenever match is found between the driver and a port service device.
 */
static int pcie_port_probe_service(struct device *dev)
{
	struct pcie_device *pciedev;
	struct pcie_port_service_driver *driver;
	int status;

	if (!dev || !dev->driver)
		return -ENODEV;

	driver = to_service_driver(dev->driver);
	if (!driver || !driver->probe)
		return -ENODEV;

	pciedev = to_pcie_device(dev);
	status = driver->probe(pciedev);
	if (status)
		return status;

	dev_printk(KERN_DEBUG, dev, "service driver %s loaded\n", driver->name);
	get_device(dev);
	return 0;
}

/**
 * pcie_port_remove_service - detach driver from given PCI Express port service
 * @dev: PCI Express port service device to handle
 *
 * If PCI Express port service driver is registered with
 * pcie_port_service_register(), this function will be called by the driver core
 * when device_unregister() is called for the port service device associated
 * with the driver.
 */
static int pcie_port_remove_service(struct device *dev)
{
	struct pcie_device *pciedev;
	struct pcie_port_service_driver *driver;

	if (!dev || !dev->driver)
		return 0;

	pciedev = to_pcie_device(dev);
	driver = to_service_driver(dev->driver);
	if (driver && driver->remove) {
		dev_printk(KERN_DEBUG, dev, "unloading service driver %s\n",
			driver->name);
		driver->remove(pciedev);
		put_device(dev);
	}
	return 0;
}

void pcie_port_device_remove(struct pci_dev *dev)
{
	struct device *device;
	unsigned long device_addr;
	int interrupt_mode = PCIE_PORT_INTx_MODE;
	int status;

	do {
		status = device_for_each_child(&dev->dev, &device_addr, remove_iter);
		if (status) {
			device = (struct device*)device_addr;
			interrupt_mode = (to_pcie_device(device))->interrupt_mode;
			put_device(device);
			device_unregister(device);
		}
	} while (status);
	/* Switch to INTx by default if MSI enabled */
	if (interrupt_mode == PCIE_PORT_MSIX_MODE)
		pci_disable_msix(dev);
	else if (interrupt_mode == PCIE_PORT_MSI_MODE)
		pci_disable_msi(dev);
}

int pcie_port_bus_register(void)
{
	return bus_register(&pcie_port_bus_type);
}

void pcie_port_bus_unregister(void)
{
	bus_unregister(&pcie_port_bus_type);
}

int pcie_port_service_register(struct pcie_port_service_driver *new)
{
	new->driver.name = (char *)new->name;
/**
 * pcie_port_shutdown_service - shut down given PCI Express port service
 * @dev: PCI Express port service device to handle
 *
 * If PCI Express port service driver is registered with
 * pcie_port_service_register(), this function will be called by the driver core
 * when device_shutdown() is called for the port service device associated
 * with the driver.
 */
static void pcie_port_shutdown_service(struct device *dev) {}

/**
 * pcie_port_service_register - register PCI Express port service driver
 * @new: PCI Express port service driver to register
 */
int pcie_port_service_register(struct pcie_port_service_driver *new)
{
	if (pcie_ports_disabled)
		return -ENODEV;

	new->driver.name = new->name;
	new->driver.bus = &pcie_port_bus_type;
	new->driver.probe = pcie_port_probe_service;
	new->driver.remove = pcie_port_remove_service;
	new->driver.shutdown = pcie_port_shutdown_service;
	new->driver.suspend = pcie_port_suspend_service;
	new->driver.resume = pcie_port_resume_service;

	return driver_register(&new->driver);
}

void pcie_port_service_unregister(struct pcie_port_service_driver *new)
{
	driver_unregister(&new->driver);
}

EXPORT_SYMBOL(pcie_port_service_register);

	return driver_register(&new->driver);
}
EXPORT_SYMBOL(pcie_port_service_register);

/**
 * pcie_port_service_unregister - unregister PCI Express port service driver
 * @drv: PCI Express port service driver to unregister
 */
void pcie_port_service_unregister(struct pcie_port_service_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL(pcie_port_service_unregister);
