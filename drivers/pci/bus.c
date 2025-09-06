/*
 *	drivers/pci/bus.c
 *
 * From setup-res.c, by:
 *	Dave Rusling (david.rusling@reo.mts.dec.com)
 *	David Mosberger (davidm@cs.arizona.edu)
 *	David Miller (davem@redhat.com)
 *	Ivan Kokshaysky (ink@jurassic.park.msu.ru)
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/proc_fs.h>
#include <linux/init.h>

#include "pci.h"

#include <linux/slab.h>

#include "pci.h"

void pci_add_resource_offset(struct list_head *resources, struct resource *res,
			     resource_size_t offset)
{
	struct resource_entry *entry;

	entry = resource_list_create_entry(res, 0);
	if (!entry) {
		printk(KERN_ERR "PCI: can't add host bridge window %pR\n", res);
		return;
	}

	entry->offset = offset;
	resource_list_add_tail(entry, resources);
}
EXPORT_SYMBOL(pci_add_resource_offset);

void pci_add_resource(struct list_head *resources, struct resource *res)
{
	pci_add_resource_offset(resources, res, 0);
}
EXPORT_SYMBOL(pci_add_resource);

void pci_free_resource_list(struct list_head *resources)
{
	resource_list_free(resources);
}
EXPORT_SYMBOL(pci_free_resource_list);

void pci_bus_add_resource(struct pci_bus *bus, struct resource *res,
			  unsigned int flags)
{
	struct pci_bus_resource *bus_res;

	bus_res = kzalloc(sizeof(struct pci_bus_resource), GFP_KERNEL);
	if (!bus_res) {
		dev_err(&bus->dev, "can't add %pR resource\n", res);
		return;
	}

	bus_res->res = res;
	bus_res->flags = flags;
	list_add_tail(&bus_res->list, &bus->resources);
}

struct resource *pci_bus_resource_n(const struct pci_bus *bus, int n)
{
	struct pci_bus_resource *bus_res;

	if (n < PCI_BRIDGE_RESOURCE_NUM)
		return bus->resource[n];

	n -= PCI_BRIDGE_RESOURCE_NUM;
	list_for_each_entry(bus_res, &bus->resources, list) {
		if (n-- == 0)
			return bus_res->res;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(pci_bus_resource_n);

void pci_bus_remove_resources(struct pci_bus *bus)
{
	int i;
	struct pci_bus_resource *bus_res, *tmp;

	for (i = 0; i < PCI_BRIDGE_RESOURCE_NUM; i++)
		bus->resource[i] = NULL;

	list_for_each_entry_safe(bus_res, tmp, &bus->resources, list) {
		list_del(&bus_res->list);
		kfree(bus_res);
	}
}

static struct pci_bus_region pci_32_bit = {0, 0xffffffffULL};
#ifdef CONFIG_PCI_BUS_ADDR_T_64BIT
static struct pci_bus_region pci_64_bit = {0,
				(pci_bus_addr_t) 0xffffffffffffffffULL};
static struct pci_bus_region pci_high = {(pci_bus_addr_t) 0x100000000ULL,
				(pci_bus_addr_t) 0xffffffffffffffffULL};
#endif

/*
 * @res contains CPU addresses.  Clip it so the corresponding bus addresses
 * on @bus are entirely within @region.  This is used to control the bus
 * addresses of resources we allocate, e.g., we may need a resource that
 * can be mapped by a 32-bit BAR.
 */
static void pci_clip_resource_to_region(struct pci_bus *bus,
					struct resource *res,
					struct pci_bus_region *region)
{
	struct pci_bus_region r;

	pcibios_resource_to_bus(bus, &r, res);
	if (r.start < region->start)
		r.start = region->start;
	if (r.end > region->end)
		r.end = region->end;

	if (r.end < r.start)
		res->end = res->start - 1;
	else
		pcibios_bus_to_resource(bus, res, &r);
}

static int pci_bus_alloc_from_region(struct pci_bus *bus, struct resource *res,
		resource_size_t size, resource_size_t align,
		resource_size_t min, unsigned long type_mask,
		resource_size_t (*alignf)(void *,
					  const struct resource *,
					  resource_size_t,
					  resource_size_t),
		void *alignf_data,
		struct pci_bus_region *region)
{
	int i, ret;
	struct resource *r, avail;
	resource_size_t max;

	type_mask |= IORESOURCE_TYPE_BITS;

	pci_bus_for_each_resource(bus, r, i) {
		resource_size_t min_used = min;

		if (!r)
			continue;

		/* type_mask must match */
		if ((res->flags ^ r->flags) & type_mask)
			continue;

		/* We cannot allocate a non-prefetching resource
		   from a pre-fetching area */
		if ((r->flags & IORESOURCE_PREFETCH) &&
		    !(res->flags & IORESOURCE_PREFETCH))
			continue;

		avail = *r;
		pci_clip_resource_to_region(bus, &avail, region);

		/*
		 * "min" is typically PCIBIOS_MIN_IO or PCIBIOS_MIN_MEM to
		 * protect badly documented motherboard resources, but if
		 * this is an already-configured bridge window, its start
		 * overrides "min".
		 */
		if (avail.start)
			min_used = avail.start;

		max = avail.end;

		/* Ok, try it out.. */
		ret = allocate_resource(r, res, size, min_used, max,
					align, alignf, alignf_data);
		if (ret == 0)
			return 0;
	}
	return -ENOMEM;
}

/**
 * pci_bus_alloc_resource - allocate a resource from a parent bus
 * @bus: PCI bus
 * @res: resource to allocate
 * @size: size of resource to allocate
 * @align: alignment of resource to allocate
 * @min: minimum /proc/iomem address to allocate
 * @type_mask: IORESOURCE_* type flags
 * @alignf: resource alignment function
 * @alignf_data: data argument for resource alignment function
 *
 * Given the PCI bus a device resides on, the size, minimum address,
 * alignment and type, try to find an acceptable resource allocation
 * for a specific device resource.
 */
int
pci_bus_alloc_resource(struct pci_bus *bus, struct resource *res,
		resource_size_t size, resource_size_t align,
		resource_size_t min, unsigned int type_mask,
		void (*alignf)(void *, struct resource *, resource_size_t,
				resource_size_t),
		void *alignf_data)
{
	int i, ret = -ENOMEM;

	type_mask |= IORESOURCE_IO | IORESOURCE_MEM;

	for (i = 0; i < PCI_BUS_NUM_RESOURCES; i++) {
		struct resource *r = bus->resource[i];
		if (!r)
			continue;

		/* type_mask must match */
		if ((res->flags ^ r->flags) & type_mask)
			continue;

		/* We cannot allocate a non-prefetching resource
		   from a pre-fetching area */
		if ((r->flags & IORESOURCE_PREFETCH) &&
		    !(res->flags & IORESOURCE_PREFETCH))
			continue;

		/* Ok, try it out.. */
		ret = allocate_resource(r, res, size,
					r->start ? : min,
					-1, align,
					alignf, alignf_data);
		if (ret == 0)
			break;
	}
	return ret;
}

/**
 * add a single device
 * @dev: device to add
 *
 * This adds a single pci device to the global
 * device list and adds sysfs and procfs entries
 */
int pci_bus_add_device(struct pci_dev *dev)
{
	int retval;
	retval = device_add(&dev->dev);
	if (retval)
		return retval;

	dev->is_added = 1;
	pci_proc_attach_device(dev);
	pci_create_sysfs_dev_files(dev);
	return 0;
}

/**
 * pci_bus_add_devices - insert newly discovered PCI devices
 * @bus: bus to check for new devices
 *
 * Add newly discovered PCI devices (which are on the bus->devices
 * list) to the global PCI device list, add the sysfs and procfs
 * entries.  Where a bridge is found, add the discovered bus to
 * the parents list of child buses, and recurse (breadth-first
 * to be compatible with 2.4)
 *
 * Call hotplug for each new devices.
 */
void pci_bus_add_devices(struct pci_bus *bus)
{
	struct pci_dev *dev;
	struct pci_bus *child_bus;
	int retval;
int pci_bus_alloc_resource(struct pci_bus *bus, struct resource *res,
		resource_size_t size, resource_size_t align,
		resource_size_t min, unsigned long type_mask,
		resource_size_t (*alignf)(void *,
					  const struct resource *,
					  resource_size_t,
					  resource_size_t),
		void *alignf_data)
{
#ifdef CONFIG_PCI_BUS_ADDR_T_64BIT
	int rc;

	if (res->flags & IORESOURCE_MEM_64) {
		rc = pci_bus_alloc_from_region(bus, res, size, align, min,
					       type_mask, alignf, alignf_data,
					       &pci_high);
		if (rc == 0)
			return 0;

		return pci_bus_alloc_from_region(bus, res, size, align, min,
						 type_mask, alignf, alignf_data,
						 &pci_64_bit);
	}
#endif

	return pci_bus_alloc_from_region(bus, res, size, align, min,
					 type_mask, alignf, alignf_data,
					 &pci_32_bit);
}
EXPORT_SYMBOL(pci_bus_alloc_resource);

/*
 * The @idx resource of @dev should be a PCI-PCI bridge window.  If this
 * resource fits inside a window of an upstream bridge, do nothing.  If it
 * overlaps an upstream window but extends outside it, clip the resource so
 * it fits completely inside.
 */
bool pci_bus_clip_resource(struct pci_dev *dev, int idx)
{
	struct pci_bus *bus = dev->bus;
	struct resource *res = &dev->resource[idx];
	struct resource orig_res = *res;
	struct resource *r;
	int i;

	pci_bus_for_each_resource(bus, r, i) {
		resource_size_t start, end;

		if (!r)
			continue;

		if (resource_type(res) != resource_type(r))
			continue;

		start = max(r->start, res->start);
		end = min(r->end, res->end);

		if (start > end)
			continue;	/* no overlap */

		if (res->start == start && res->end == end)
			return false;	/* no change */

		res->start = start;
		res->end = end;
		res->flags &= ~IORESOURCE_UNSET;
		orig_res.flags &= ~IORESOURCE_UNSET;
		dev_printk(KERN_DEBUG, &dev->dev, "%pR clipped to %pR\n",
				 &orig_res, res);

		return true;
	}

	return false;
}

void __weak pcibios_resource_survey_bus(struct pci_bus *bus) { }

/**
 * pci_bus_add_device - start driver for a single device
 * @dev: device to add
 *
 * This adds add sysfs entries and start device drivers
 */
void pci_bus_add_device(struct pci_dev *dev)
{
	int retval;

	/*
	 * Can not put in pci_device_add yet because resources
	 * are not assigned yet for some devices.
	 */
	pci_fixup_device(pci_fixup_final, dev);
	pci_create_sysfs_dev_files(dev);
	pci_proc_attach_device(dev);

	dev->match_driver = true;
	retval = device_attach(&dev->dev);
	WARN_ON(retval < 0);

	dev->is_added = 1;
}
EXPORT_SYMBOL_GPL(pci_bus_add_device);

/**
 * pci_bus_add_devices - start driver for PCI devices
 * @bus: bus to check for new devices
 *
 * Start driver for PCI devices and add some sysfs entries.
 */
void pci_bus_add_devices(const struct pci_bus *bus)
{
	struct pci_dev *dev;
	struct pci_bus *child;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		/* Skip already-added devices */
		if (dev->is_added)
			continue;
		retval = pci_bus_add_device(dev);
		if (retval)
			dev_err(&dev->dev, "Error adding device, continuing\n");
		pci_bus_add_device(dev);
	}

	list_for_each_entry(dev, &bus->devices, bus_list) {
		BUG_ON(!dev->is_added);

		/*
		 * If there is an unattached subordinate bus, attach
		 * it and then scan for unattached PCI devices.
		 */
		if (dev->subordinate) {
		       if (list_empty(&dev->subordinate->node)) {
			       down_write(&pci_bus_sem);
			       list_add_tail(&dev->subordinate->node,
					       &dev->bus->children);
			       up_write(&pci_bus_sem);
			}
			pci_bus_add_devices(dev->subordinate);

			/* register the bus with sysfs as the parent is now
			 * properly registered. */
			child_bus = dev->subordinate;
			if (child_bus->is_added)
				continue;
			child_bus->dev.parent = child_bus->bridge;
			retval = device_register(&child_bus->dev);
			if (retval)
				dev_err(&dev->dev, "Error registering pci_bus,"
					" continuing...\n");
			else {
				child_bus->is_added = 1;
				retval = device_create_file(&child_bus->dev,
							&dev_attr_cpuaffinity);
			}
			if (retval)
				dev_err(&dev->dev, "Error creating cpuaffinity"
					" file, continuing...\n");
		}
	}
}

void pci_enable_bridges(struct pci_bus *bus)
{
	struct pci_dev *dev;
	int retval;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		if (dev->subordinate) {
			retval = pci_enable_device(dev);
			pci_set_master(dev);
			pci_enable_bridges(dev->subordinate);
		}
	}
}
		child = dev->subordinate;
		if (child)
			pci_bus_add_devices(child);
	}
}
EXPORT_SYMBOL(pci_bus_add_devices);

/** pci_walk_bus - walk devices on/under bus, calling callback.
 *  @top      bus whose devices should be walked
 *  @cb       callback to be called for each device found
 *  @userdata arbitrary pointer to be passed to callback.
 *
 *  Walk the given bus, including any bridged devices
 *  on buses under this bus.  Call the provided callback
 *  on each device found.
 */
void pci_walk_bus(struct pci_bus *top, void (*cb)(struct pci_dev *, void *),
 *
 *  We check the return of @cb each time. If it returns anything
 *  other than 0, we break out.
 *
 */
void pci_walk_bus(struct pci_bus *top, int (*cb)(struct pci_dev *, void *),
		  void *userdata)
{
	struct pci_dev *dev;
	struct pci_bus *bus;
	struct list_head *next;
	int retval;

	bus = top;
	down_read(&pci_bus_sem);
	next = top->devices.next;
	for (;;) {
		if (next == &bus->devices) {
			/* end of this bus, go up or finish */
			if (bus == top)
				break;
			next = bus->self->bus_list.next;
			bus = bus->self->bus;
			continue;
		}
		dev = list_entry(next, struct pci_dev, bus_list);
		if (dev->subordinate) {
			/* this is a pci-pci bridge, do its devices next */
			next = dev->subordinate->devices.next;
			bus = dev->subordinate;
		} else
			next = dev->bus_list.next;

		/* Run device routines with the device locked */
		down(&dev->dev.sem);
		cb(dev, userdata);
		up(&dev->dev.sem);
	}
	up_read(&pci_bus_sem);
}

EXPORT_SYMBOL(pci_bus_alloc_resource);
EXPORT_SYMBOL_GPL(pci_bus_add_device);
EXPORT_SYMBOL(pci_bus_add_devices);
EXPORT_SYMBOL(pci_enable_bridges);
		retval = cb(dev, userdata);
		if (retval)
			break;
	}
	up_read(&pci_bus_sem);
}
EXPORT_SYMBOL_GPL(pci_walk_bus);

struct pci_bus *pci_bus_get(struct pci_bus *bus)
{
	if (bus)
		get_device(&bus->dev);
	return bus;
}
EXPORT_SYMBOL(pci_bus_get);

void pci_bus_put(struct pci_bus *bus)
{
	if (bus)
		put_device(&bus->dev);
}
EXPORT_SYMBOL(pci_bus_put);

