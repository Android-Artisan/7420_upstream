/*
 * drivers/pci/slot.c
 * Copyright (C) 2006 Matthew Wilcox <matthew@wil.cx>
 * Copyright (C) 2006-2008 Hewlett-Packard Development Company, L.P.
 * 	Alex Chiang <achiang@hp.com>
 */

#include <linux/kobject.h>
 * Copyright (C) 2006-2009 Hewlett-Packard Development Company, L.P.
 *	Alex Chiang <achiang@hp.com>
 */

#include <linux/kobject.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/err.h>
#include "pci.h"

struct kset *pci_slots_kset;
EXPORT_SYMBOL_GPL(pci_slots_kset);
static DEFINE_MUTEX(pci_slot_mutex);

static ssize_t pci_slot_attr_show(struct kobject *kobj,
					struct attribute *attr, char *buf)
{
	struct pci_slot *slot = to_pci_slot(kobj);
	struct pci_slot_attribute *attribute = to_pci_slot_attr(attr);
	return attribute->show ? attribute->show(slot, buf) : -EIO;
}

static ssize_t pci_slot_attr_store(struct kobject *kobj,
			struct attribute *attr, const char *buf, size_t len)
{
	struct pci_slot *slot = to_pci_slot(kobj);
	struct pci_slot_attribute *attribute = to_pci_slot_attr(attr);
	return attribute->store ? attribute->store(slot, buf, len) : -EIO;
}

static struct sysfs_ops pci_slot_sysfs_ops = {
static const struct sysfs_ops pci_slot_sysfs_ops = {
	.show = pci_slot_attr_show,
	.store = pci_slot_attr_store,
};

static ssize_t address_read_file(struct pci_slot *slot, char *buf)
{
	if (slot->number == 0xff)
		return sprintf(buf, "%04x:%02x\n",
				pci_domain_nr(slot->bus),
				slot->bus->number);
	else
		return sprintf(buf, "%04x:%02x:%02x\n",
				pci_domain_nr(slot->bus),
				slot->bus->number,
				slot->number);
}

static void pci_slot_release(struct kobject *kobj)
{
	struct pci_slot *slot = to_pci_slot(kobj);

	pr_debug("%s: releasing pci_slot on %x:%d\n", __func__,
		 slot->bus->number, slot->number);
/* these strings match up with the values in pci_bus_speed */
static const char *pci_bus_speed_strings[] = {
	"33 MHz PCI",		/* 0x00 */
	"66 MHz PCI",		/* 0x01 */
	"66 MHz PCI-X",		/* 0x02 */
	"100 MHz PCI-X",	/* 0x03 */
	"133 MHz PCI-X",	/* 0x04 */
	NULL,			/* 0x05 */
	NULL,			/* 0x06 */
	NULL,			/* 0x07 */
	NULL,			/* 0x08 */
	"66 MHz PCI-X 266",	/* 0x09 */
	"100 MHz PCI-X 266",	/* 0x0a */
	"133 MHz PCI-X 266",	/* 0x0b */
	"Unknown AGP",		/* 0x0c */
	"1x AGP",		/* 0x0d */
	"2x AGP",		/* 0x0e */
	"4x AGP",		/* 0x0f */
	"8x AGP",		/* 0x10 */
	"66 MHz PCI-X 533",	/* 0x11 */
	"100 MHz PCI-X 533",	/* 0x12 */
	"133 MHz PCI-X 533",	/* 0x13 */
	"2.5 GT/s PCIe",	/* 0x14 */
	"5.0 GT/s PCIe",	/* 0x15 */
	"8.0 GT/s PCIe",	/* 0x16 */
};

static ssize_t bus_speed_read(enum pci_bus_speed speed, char *buf)
{
	const char *speed_string;

	if (speed < ARRAY_SIZE(pci_bus_speed_strings))
		speed_string = pci_bus_speed_strings[speed];
	else
		speed_string = "Unknown";

	return sprintf(buf, "%s\n", speed_string);
}

static ssize_t max_speed_read_file(struct pci_slot *slot, char *buf)
{
	return bus_speed_read(slot->bus->max_bus_speed, buf);
}

static ssize_t cur_speed_read_file(struct pci_slot *slot, char *buf)
{
	return bus_speed_read(slot->bus->cur_bus_speed, buf);
}

static void pci_slot_release(struct kobject *kobj)
{
	struct pci_dev *dev;
	struct pci_slot *slot = to_pci_slot(kobj);

	dev_dbg(&slot->bus->dev, "dev %02x, released physical slot %s\n",
		slot->number, pci_slot_name(slot));

	down_read(&pci_bus_sem);
	list_for_each_entry(dev, &slot->bus->devices, bus_list)
		if (PCI_SLOT(dev->devfn) == slot->number)
			dev->slot = NULL;
	up_read(&pci_bus_sem);

	list_del(&slot->list);
	pci_bus_put(slot->bus);

	kfree(slot);
}

static struct pci_slot_attribute pci_slot_attr_address =
	__ATTR(address, (S_IFREG | S_IRUGO), address_read_file, NULL);

static struct attribute *pci_slot_default_attrs[] = {
	&pci_slot_attr_address.attr,
	__ATTR(address, S_IRUGO, address_read_file, NULL);
static struct pci_slot_attribute pci_slot_attr_max_speed =
	__ATTR(max_bus_speed, S_IRUGO, max_speed_read_file, NULL);
static struct pci_slot_attribute pci_slot_attr_cur_speed =
	__ATTR(cur_bus_speed, S_IRUGO, cur_speed_read_file, NULL);

static struct attribute *pci_slot_default_attrs[] = {
	&pci_slot_attr_address.attr,
	&pci_slot_attr_max_speed.attr,
	&pci_slot_attr_cur_speed.attr,
	NULL,
};

static struct kobj_type pci_slot_ktype = {
	.sysfs_ops = &pci_slot_sysfs_ops,
	.release = &pci_slot_release,
	.default_attrs = pci_slot_default_attrs,
};

static char *make_slot_name(const char *name)
{
	char *new_name;
	int len, max, dup;

	new_name = kstrdup(name, GFP_KERNEL);
	if (!new_name)
		return NULL;

	/*
	 * Make sure we hit the realloc case the first time through the
	 * loop.  'len' will be strlen(name) + 3 at that point which is
	 * enough space for "name-X" and the trailing NUL.
	 */
	len = strlen(name) + 2;
	max = 1;
	dup = 1;

	for (;;) {
		struct kobject *dup_slot;
		dup_slot = kset_find_obj(pci_slots_kset, new_name);
		if (!dup_slot)
			break;
		kobject_put(dup_slot);
		if (dup == max) {
			len++;
			max *= 10;
			kfree(new_name);
			new_name = kmalloc(len, GFP_KERNEL);
			if (!new_name)
				break;
		}
		sprintf(new_name, "%s-%d", name, dup++);
	}

	return new_name;
}

static int rename_slot(struct pci_slot *slot, const char *name)
{
	int result = 0;
	char *slot_name;

	if (strcmp(pci_slot_name(slot), name) == 0)
		return result;

	slot_name = make_slot_name(name);
	if (!slot_name)
		return -ENOMEM;

	result = kobject_rename(&slot->kobj, slot_name);
	kfree(slot_name);

	return result;
}

void pci_dev_assign_slot(struct pci_dev *dev)
{
	struct pci_slot *slot;

	mutex_lock(&pci_slot_mutex);
	list_for_each_entry(slot, &dev->bus->slots, list)
		if (PCI_SLOT(dev->devfn) == slot->number)
			dev->slot = slot;
	mutex_unlock(&pci_slot_mutex);
}

static struct pci_slot *get_slot(struct pci_bus *parent, int slot_nr)
{
	struct pci_slot *slot;

	/* We already hold pci_slot_mutex */
	list_for_each_entry(slot, &parent->slots, list)
		if (slot->number == slot_nr) {
			kobject_get(&slot->kobj);
			return slot;
		}

	return NULL;
}

/**
 * pci_create_slot - create or increment refcount for physical PCI slot
 * @parent: struct pci_bus of parent bridge
 * @slot_nr: PCI_SLOT(pci_dev->devfn) or -1 for placeholder
 * @name: user visible string presented in /sys/bus/pci/slots/<name>
 * @hotplug: set if caller is hotplug driver, NULL otherwise
 *
 * PCI slots have first class attributes such as address, speed, width,
 * and a &struct pci_slot is used to manage them. This interface will
 * either return a new &struct pci_slot to the caller, or if the pci_slot
 * already exists, its refcount will be incremented.
 *
 * Slots are uniquely identified by a @pci_bus, @slot_nr, @name tuple.
 * Slots are uniquely identified by a @pci_bus, @slot_nr tuple.
 *
 * There are known platforms with broken firmware that assign the same
 * name to multiple slots. Workaround these broken platforms by renaming
 * the slots on behalf of the caller. If firmware assigns name N to
 * multiple slots:
 *
 * The first slot is assigned N
 * The second slot is assigned N-1
 * The third slot is assigned N-2
 * etc.
 *
 * Placeholder slots:
 * In most cases, @pci_bus, @slot_nr will be sufficient to uniquely identify
 * a slot. There is one notable exception - pSeries (rpaphp), where the
 * @slot_nr cannot be determined until a device is actually inserted into
 * the slot. In this scenario, the caller may pass -1 for @slot_nr.
 *
 * The following semantics are imposed when the caller passes @slot_nr ==
 * -1. First, the check for existing %struct pci_slot is skipped, as the
 * caller may know about several unpopulated slots on a given %struct
 * pci_bus, and each slot would have a @slot_nr of -1.  Uniqueness for
 * these slots is then determined by the @name parameter. We expect
 * kobject_init_and_add() to warn us if the caller attempts to create
 * multiple slots with the same name. The other change in semantics is
 * -1. First, we no longer check for an existing %struct pci_slot, as there
 * may be many slots with @slot_nr of -1.  The other change in semantics is
 * user-visible, which is the 'address' parameter presented in sysfs will
 * consist solely of a dddd:bb tuple, where dddd is the PCI domain of the
 * %struct pci_bus and bb is the bus number. In other words, the devfn of
 * the 'placeholder' slot will not be displayed.
 */

struct pci_slot *pci_create_slot(struct pci_bus *parent, int slot_nr,
				 const char *name)
{
	struct pci_slot *slot;
	int err;

	down_write(&pci_bus_sem);
struct pci_slot *pci_create_slot(struct pci_bus *parent, int slot_nr,
				 const char *name,
				 struct hotplug_slot *hotplug)
{
	struct pci_dev *dev;
	struct pci_slot *slot;
	int err = 0;
	char *slot_name = NULL;

	mutex_lock(&pci_slot_mutex);

	if (slot_nr == -1)
		goto placeholder;

	/* If we've already created this slot, bump refcount and return. */
	list_for_each_entry(slot, &parent->slots, list) {
		if (slot->number == slot_nr) {
			kobject_get(&slot->kobj);
			pr_debug("%s: inc refcount to %d on %04x:%02x:%02x\n",
				 __func__,
				 atomic_read(&slot->kobj.kref.refcount),
				 pci_domain_nr(parent), parent->number,
				 slot_nr);
			goto out;
		}
	/*
	 * Hotplug drivers are allowed to rename an existing slot,
	 * but only if not already claimed.
	 */
	slot = get_slot(parent, slot_nr);
	if (slot) {
		if (hotplug) {
			if ((err = slot->hotplug ? -EBUSY : 0)
			     || (err = rename_slot(slot, name))) {
				kobject_put(&slot->kobj);
				slot = NULL;
				goto err;
			}
		}
		goto out;
	}

placeholder:
	slot = kzalloc(sizeof(*slot), GFP_KERNEL);
	if (!slot) {
		slot = ERR_PTR(-ENOMEM);
		goto out;
		err = -ENOMEM;
		goto err;
	}

	slot->bus = pci_bus_get(parent);
	slot->number = slot_nr;

	slot->kobj.kset = pci_slots_kset;
	err = kobject_init_and_add(&slot->kobj, &pci_slot_ktype, NULL,
				   "%s", name);
	if (err) {
		printk(KERN_ERR "Unable to register kobject %s\n", name);
		goto err;
	}

	INIT_LIST_HEAD(&slot->list);
	list_add(&slot->list, &parent->slots);

	/* Don't care if debug printk has a -1 for slot_nr */
	pr_debug("%s: created pci_slot on %04x:%02x:%02x\n",
		 __func__, pci_domain_nr(parent), parent->number, slot_nr);

 out:
	up_write(&pci_bus_sem);
	return slot;
 err:

	slot_name = make_slot_name(name);
	if (!slot_name) {
		err = -ENOMEM;
		pci_bus_put(slot->bus);
		kfree(slot);
		goto err;
	}

	INIT_LIST_HEAD(&slot->list);
	list_add(&slot->list, &parent->slots);

	err = kobject_init_and_add(&slot->kobj, &pci_slot_ktype, NULL,
				   "%s", slot_name);
	if (err) {
		kobject_put(&slot->kobj);
		goto err;
	}

	down_read(&pci_bus_sem);
	list_for_each_entry(dev, &parent->devices, bus_list)
		if (PCI_SLOT(dev->devfn) == slot_nr)
			dev->slot = slot;
	up_read(&pci_bus_sem);

	dev_dbg(&parent->dev, "dev %02x, created physical slot %s\n",
		slot_nr, pci_slot_name(slot));

out:
	kfree(slot_name);
	mutex_unlock(&pci_slot_mutex);
	return slot;
err:
	slot = ERR_PTR(err);
	goto out;
}
EXPORT_SYMBOL_GPL(pci_create_slot);

/**
 * pci_update_slot_number - update %struct pci_slot -> number
 * @slot - %struct pci_slot to update
 * @slot_nr - new number for slot
 *
 * The primary purpose of this interface is to allow callers who earlier
 * created a placeholder slot in pci_create_slot() by passing a -1 as
 * slot_nr, to update their %struct pci_slot with the correct @slot_nr.
 */

void pci_update_slot_number(struct pci_slot *slot, int slot_nr)
{
	int name_count = 0;
	struct pci_slot *tmp;

	down_write(&pci_bus_sem);

	list_for_each_entry(tmp, &slot->bus->slots, list) {
		WARN_ON(tmp->number == slot_nr);
		if (!strcmp(kobject_name(&tmp->kobj), kobject_name(&slot->kobj)))
			name_count++;
	}

	if (name_count > 1)
		printk(KERN_WARNING "pci_update_slot_number found %d slots with the same name: %s\n", name_count, kobject_name(&slot->kobj));

	slot->number = slot_nr;
	up_write(&pci_bus_sem);
}
EXPORT_SYMBOL_GPL(pci_update_slot_number);

/**
 * pci_destroy_slot - decrement refcount for physical PCI slot
 * @slot: struct pci_slot to decrement
 *
 * %struct pci_slot is refcounted, so destroying them is really easy; we
 * just call kobject_put on its kobj and let our release methods do the
 * rest.
 */

void pci_destroy_slot(struct pci_slot *slot)
{
	pr_debug("%s: dec refcount to %d on %04x:%02x:%02x\n", __func__,
		 atomic_read(&slot->kobj.kref.refcount) - 1,
		 pci_domain_nr(slot->bus), slot->bus->number, slot->number);

	down_write(&pci_bus_sem);
	kobject_put(&slot->kobj);
	up_write(&pci_bus_sem);
}
EXPORT_SYMBOL_GPL(pci_destroy_slot);

void pci_destroy_slot(struct pci_slot *slot)
{
	dev_dbg(&slot->bus->dev, "dev %02x, dec refcount to %d\n",
		slot->number, atomic_read(&slot->kobj.kref.refcount) - 1);

	mutex_lock(&pci_slot_mutex);
	kobject_put(&slot->kobj);
	mutex_unlock(&pci_slot_mutex);
}
EXPORT_SYMBOL_GPL(pci_destroy_slot);

#if defined(CONFIG_HOTPLUG_PCI) || defined(CONFIG_HOTPLUG_PCI_MODULE)
#include <linux/pci_hotplug.h>
/**
 * pci_hp_create_link - create symbolic link to the hotplug driver module.
 * @pci_slot: struct pci_slot
 *
 * Helper function for pci_hotplug_core.c to create symbolic link to
 * the hotplug driver module.
 */
void pci_hp_create_module_link(struct pci_slot *pci_slot)
{
	struct hotplug_slot *slot = pci_slot->hotplug;
	struct kobject *kobj = NULL;
	int ret;

	if (!slot || !slot->ops)
		return;
	kobj = kset_find_obj(module_kset, slot->ops->mod_name);
	if (!kobj)
		return;
	ret = sysfs_create_link(&pci_slot->kobj, kobj, "module");
	if (ret)
		dev_err(&pci_slot->bus->dev, "Error creating sysfs link (%d)\n",
			ret);
	kobject_put(kobj);
}
EXPORT_SYMBOL_GPL(pci_hp_create_module_link);

/**
 * pci_hp_remove_link - remove symbolic link to the hotplug driver module.
 * @pci_slot: struct pci_slot
 *
 * Helper function for pci_hotplug_core.c to remove symbolic link to
 * the hotplug driver module.
 */
void pci_hp_remove_module_link(struct pci_slot *pci_slot)
{
	sysfs_remove_link(&pci_slot->kobj, "module");
}
EXPORT_SYMBOL_GPL(pci_hp_remove_module_link);
#endif

static int pci_slot_init(void)
{
	struct kset *pci_bus_kset;

	pci_bus_kset = bus_get_kset(&pci_bus_type);
	pci_slots_kset = kset_create_and_add("slots", NULL,
						&pci_bus_kset->kobj);
	if (!pci_slots_kset) {
		printk(KERN_ERR "PCI: Slot initialization failure\n");
		return -ENOMEM;
	}
	return 0;
}

subsys_initcall(pci_slot_init);
