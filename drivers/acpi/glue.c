/*
 * Link physical devices with ACPI devices support
 *
 * Copyright (c) 2005 David Shaohua Li <shaohua.li@intel.com>
 * Copyright (c) 2005 Intel Corp.
 *
 * This file is released under the GPLv2.
 */
#include <linux/init.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/rwsem.h>
#include <linux/acpi.h>

#define ACPI_GLUE_DEBUG	0
#if ACPI_GLUE_DEBUG
#define DBG(x...) printk(PREFIX x)
#else
#define DBG(x...) do { } while(0)
#include <linux/export.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/rwsem.h>
#include <linux/acpi.h>
#include <linux/dma-mapping.h>

#include "internal.h"

#define ACPI_GLUE_DEBUG	0
#if ACPI_GLUE_DEBUG
#define DBG(fmt, ...)						\
	printk(KERN_DEBUG PREFIX fmt, ##__VA_ARGS__)
#else
#define DBG(fmt, ...)						\
do {								\
	if (0)							\
		printk(KERN_DEBUG PREFIX fmt, ##__VA_ARGS__);	\
} while (0)
#endif
static LIST_HEAD(bus_type_list);
static DECLARE_RWSEM(bus_type_sem);

#define PHYSICAL_NODE_STRING "physical_node"
#define PHYSICAL_NODE_NAME_SIZE (sizeof(PHYSICAL_NODE_STRING) + 10)

int register_acpi_bus_type(struct acpi_bus_type *type)
{
	if (acpi_disabled)
		return -ENODEV;
	if (type && type->bus && type->find_device) {
		down_write(&bus_type_sem);
		list_add_tail(&type->list, &bus_type_list);
		up_write(&bus_type_sem);
		printk(KERN_INFO PREFIX "bus type %s registered\n",
		       type->bus->name);
	if (type && type->match && type->find_companion) {
		down_write(&bus_type_sem);
		list_add_tail(&type->list, &bus_type_list);
		up_write(&bus_type_sem);
		printk(KERN_INFO PREFIX "bus type %s registered\n", type->name);
		return 0;
	}
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(register_acpi_bus_type);

int unregister_acpi_bus_type(struct acpi_bus_type *type)
{
	if (acpi_disabled)
		return 0;
	if (type) {
		down_write(&bus_type_sem);
		list_del_init(&type->list);
		up_write(&bus_type_sem);
		printk(KERN_INFO PREFIX "ACPI bus type %s unregistered\n",
		       type->bus->name);
		printk(KERN_INFO PREFIX "bus type %s unregistered\n",
		       type->name);
		return 0;
	}
	return -ENODEV;
}

static struct acpi_bus_type *acpi_get_bus_type(struct bus_type *type)
EXPORT_SYMBOL_GPL(unregister_acpi_bus_type);

static struct acpi_bus_type *acpi_get_bus_type(struct device *dev)
{
	struct acpi_bus_type *tmp, *ret = NULL;

	down_read(&bus_type_sem);
	list_for_each_entry(tmp, &bus_type_list, list) {
		if (tmp->bus == type) {
		if (tmp->match(dev)) {
			ret = tmp;
			break;
		}
	}
	up_read(&bus_type_sem);
	return ret;
}

static int acpi_find_bridge_device(struct device *dev, acpi_handle * handle)
{
	struct acpi_bus_type *tmp;
	int ret = -ENODEV;

	down_read(&bus_type_sem);
	list_for_each_entry(tmp, &bus_type_list, list) {
		if (tmp->find_bridge && !tmp->find_bridge(dev, handle)) {
			ret = 0;
			break;
		}
	}
	up_read(&bus_type_sem);
	return ret;
}

/* Get device's handler per its address under its parent */
struct acpi_find_child {
	acpi_handle handle;
	acpi_integer address;
};

static acpi_status
do_acpi_find_child(acpi_handle handle, u32 lvl, void *context, void **rv)
{
	acpi_status status;
	struct acpi_device_info *info;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_find_child *find = context;

	status = acpi_get_object_info(handle, &buffer);
	if (ACPI_SUCCESS(status)) {
		info = buffer.pointer;
		if (info->address == find->address)
			find->handle = handle;
		kfree(buffer.pointer);
	}
	return AE_OK;
}

acpi_handle acpi_get_child(acpi_handle parent, acpi_integer address)
{
	struct acpi_find_child find = { NULL, address };

	if (!parent)
		return NULL;
	acpi_walk_namespace(ACPI_TYPE_DEVICE, parent,
			    1, do_acpi_find_child, &find, NULL);
	return find.handle;
}

EXPORT_SYMBOL(acpi_get_child);

/* Link ACPI devices with physical devices */
static void acpi_glue_data_handler(acpi_handle handle,
				   u32 function, void *context)
{
	/* we provide an empty handler */
}

/* Note: a success call will increase reference count by one */
struct device *acpi_get_physical_device(acpi_handle handle)
{
	acpi_status status;
	struct device *dev;

	status = acpi_get_data(handle, acpi_glue_data_handler, (void **)&dev);
	if (ACPI_SUCCESS(status))
		return get_device(dev);
	return NULL;
}

EXPORT_SYMBOL(acpi_get_physical_device);

static int acpi_bind_one(struct device *dev, acpi_handle handle)
{
	struct acpi_device *acpi_dev;
	acpi_status status;

	if (dev->archdata.acpi_handle) {
		dev_warn(dev, "Drivers changed 'acpi_handle'\n");
		return -EINVAL;
	}
	get_device(dev);
	status = acpi_attach_data(handle, acpi_glue_data_handler, dev);
	if (ACPI_FAILURE(status)) {
		put_device(dev);
		return -EINVAL;
	}
	dev->archdata.acpi_handle = handle;

	status = acpi_bus_get_device(handle, &acpi_dev);
	if (!ACPI_FAILURE(status)) {
		int ret;

		ret = sysfs_create_link(&dev->kobj, &acpi_dev->dev.kobj,
				"firmware_node");
		ret = sysfs_create_link(&acpi_dev->dev.kobj, &dev->kobj,
				"physical_node");
		if (acpi_dev->wakeup.flags.valid) {
			device_set_wakeup_capable(dev, true);
			device_set_wakeup_enable(dev,
						acpi_dev->wakeup.state.enabled);
		}
	}

	return 0;
}

static int acpi_unbind_one(struct device *dev)
{
	if (!dev->archdata.acpi_handle)
		return 0;
	if (dev == acpi_get_physical_device(dev->archdata.acpi_handle)) {
		struct acpi_device *acpi_dev;

		/* acpi_get_physical_device increase refcnt by one */
		put_device(dev);

		if (!acpi_bus_get_device(dev->archdata.acpi_handle,
					&acpi_dev)) {
			sysfs_remove_link(&dev->kobj, "firmware_node");
			sysfs_remove_link(&acpi_dev->dev.kobj, "physical_node");
		}

		acpi_detach_data(dev->archdata.acpi_handle,
				 acpi_glue_data_handler);
		dev->archdata.acpi_handle = NULL;
		/* acpi_bind_one increase refcnt by one */
		put_device(dev);
	} else {
		dev_err(dev, "Oops, 'acpi_handle' corrupt\n");
	}
	return 0;
}

static int acpi_platform_notify(struct device *dev)
{
	struct acpi_bus_type *type;
	acpi_handle handle;
	int ret = -EINVAL;

	if (!dev->bus || !dev->parent) {
		/* bridge devices genernally haven't bus or parent */
		ret = acpi_find_bridge_device(dev, &handle);
		goto end;
	}
	type = acpi_get_bus_type(dev->bus);
	if (!type) {
		DBG("No ACPI bus support for %s\n", dev->bus_id);
		ret = -EINVAL;
		goto end;
	}
	if ((ret = type->find_device(dev, &handle)) != 0)
		DBG("Can't get handler for %s\n", dev->bus_id);
      end:
	if (!ret)
		acpi_bind_one(dev, handle);

#define FIND_CHILD_MIN_SCORE	1
#define FIND_CHILD_MAX_SCORE	2

static int find_child_checks(struct acpi_device *adev, bool check_children)
{
	bool sta_present = true;
	unsigned long long sta;
	acpi_status status;

	status = acpi_evaluate_integer(adev->handle, "_STA", NULL, &sta);
	if (status == AE_NOT_FOUND)
		sta_present = false;
	else if (ACPI_FAILURE(status) || !(sta & ACPI_STA_DEVICE_ENABLED))
		return -ENODEV;

	if (check_children && list_empty(&adev->children))
		return -ENODEV;

	/*
	 * If the device has a _HID returning a valid ACPI/PNP device ID, it is
	 * better to make it look less attractive here, so that the other device
	 * with the same _ADR value (that may not have a valid device ID) can be
	 * matched going forward.  [This means a second spec violation in a row,
	 * so whatever we do here is best effort anyway.]
	 */
	return sta_present && !adev->pnp.type.platform_id ?
			FIND_CHILD_MAX_SCORE : FIND_CHILD_MIN_SCORE;
}

struct acpi_device *acpi_find_child_device(struct acpi_device *parent,
					   u64 address, bool check_children)
{
	struct acpi_device *adev, *ret = NULL;
	int ret_score = 0;

	if (!parent)
		return NULL;

	list_for_each_entry(adev, &parent->children, node) {
		unsigned long long addr;
		acpi_status status;
		int score;

		status = acpi_evaluate_integer(adev->handle, METHOD_NAME__ADR,
					       NULL, &addr);
		if (ACPI_FAILURE(status) || addr != address)
			continue;

		if (!ret) {
			/* This is the first matching object.  Save it. */
			ret = adev;
			continue;
		}
		/*
		 * There is more than one matching device object with the same
		 * _ADR value.  That really is unexpected, so we are kind of
		 * beyond the scope of the spec here.  We have to choose which
		 * one to return, though.
		 *
		 * First, check if the previously found object is good enough
		 * and return it if so.  Second, do the same for the object that
		 * we've just found.
		 */
		if (!ret_score) {
			ret_score = find_child_checks(ret, check_children);
			if (ret_score == FIND_CHILD_MAX_SCORE)
				return ret;
		}
		score = find_child_checks(adev, check_children);
		if (score == FIND_CHILD_MAX_SCORE) {
			return adev;
		} else if (score > ret_score) {
			ret = adev;
			ret_score = score;
		}
	}
	return ret;
}
EXPORT_SYMBOL_GPL(acpi_find_child_device);

static void acpi_physnode_link_name(char *buf, unsigned int node_id)
{
	if (node_id > 0)
		snprintf(buf, PHYSICAL_NODE_NAME_SIZE,
			 PHYSICAL_NODE_STRING "%u", node_id);
	else
		strcpy(buf, PHYSICAL_NODE_STRING);
}

int acpi_bind_one(struct device *dev, struct acpi_device *acpi_dev)
{
	struct acpi_device_physical_node *physical_node, *pn;
	char physical_node_name[PHYSICAL_NODE_NAME_SIZE];
	struct list_head *physnode_list;
	unsigned int node_id;
	int retval = -EINVAL;
	enum dev_dma_attr attr;

	if (has_acpi_companion(dev)) {
		if (acpi_dev) {
			dev_warn(dev, "ACPI companion already set\n");
			return -EINVAL;
		} else {
			acpi_dev = ACPI_COMPANION(dev);
		}
	}
	if (!acpi_dev)
		return -EINVAL;

	get_device(&acpi_dev->dev);
	get_device(dev);
	physical_node = kzalloc(sizeof(*physical_node), GFP_KERNEL);
	if (!physical_node) {
		retval = -ENOMEM;
		goto err;
	}

	mutex_lock(&acpi_dev->physical_node_lock);

	/*
	 * Keep the list sorted by node_id so that the IDs of removed nodes can
	 * be recycled easily.
	 */
	physnode_list = &acpi_dev->physical_node_list;
	node_id = 0;
	list_for_each_entry(pn, &acpi_dev->physical_node_list, node) {
		/* Sanity check. */
		if (pn->dev == dev) {
			mutex_unlock(&acpi_dev->physical_node_lock);

			dev_warn(dev, "Already associated with ACPI node\n");
			kfree(physical_node);
			if (ACPI_COMPANION(dev) != acpi_dev)
				goto err;

			put_device(dev);
			put_device(&acpi_dev->dev);
			return 0;
		}
		if (pn->node_id == node_id) {
			physnode_list = &pn->node;
			node_id++;
		}
	}

	physical_node->node_id = node_id;
	physical_node->dev = dev;
	list_add(&physical_node->node, physnode_list);
	acpi_dev->physical_node_count++;

	if (!has_acpi_companion(dev))
		ACPI_COMPANION_SET(dev, acpi_dev);

	attr = acpi_get_dma_attr(acpi_dev);
	if (attr != DEV_DMA_NOT_SUPPORTED)
		arch_setup_dma_ops(dev, 0, 0, NULL,
				   attr == DEV_DMA_COHERENT);

	acpi_physnode_link_name(physical_node_name, node_id);
	retval = sysfs_create_link(&acpi_dev->dev.kobj, &dev->kobj,
				   physical_node_name);
	if (retval)
		dev_err(&acpi_dev->dev, "Failed to create link %s (%d)\n",
			physical_node_name, retval);

	retval = sysfs_create_link(&dev->kobj, &acpi_dev->dev.kobj,
				   "firmware_node");
	if (retval)
		dev_err(dev, "Failed to create link firmware_node (%d)\n",
			retval);

	mutex_unlock(&acpi_dev->physical_node_lock);

	if (acpi_dev->wakeup.flags.valid)
		device_set_wakeup_capable(dev, true);

	return 0;

 err:
	ACPI_COMPANION_SET(dev, NULL);
	put_device(dev);
	put_device(&acpi_dev->dev);
	return retval;
}
EXPORT_SYMBOL_GPL(acpi_bind_one);

int acpi_unbind_one(struct device *dev)
{
	struct acpi_device *acpi_dev = ACPI_COMPANION(dev);
	struct acpi_device_physical_node *entry;

	if (!acpi_dev)
		return 0;

	mutex_lock(&acpi_dev->physical_node_lock);

	list_for_each_entry(entry, &acpi_dev->physical_node_list, node)
		if (entry->dev == dev) {
			char physnode_name[PHYSICAL_NODE_NAME_SIZE];

			list_del(&entry->node);
			acpi_dev->physical_node_count--;

			acpi_physnode_link_name(physnode_name, entry->node_id);
			sysfs_remove_link(&acpi_dev->dev.kobj, physnode_name);
			sysfs_remove_link(&dev->kobj, "firmware_node");
			ACPI_COMPANION_SET(dev, NULL);
			/* Drop references taken by acpi_bind_one(). */
			put_device(dev);
			put_device(&acpi_dev->dev);
			kfree(entry);
			break;
		}

	mutex_unlock(&acpi_dev->physical_node_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(acpi_unbind_one);

static int acpi_platform_notify(struct device *dev)
{
	struct acpi_bus_type *type = acpi_get_bus_type(dev);
	struct acpi_device *adev;
	int ret;

	ret = acpi_bind_one(dev, NULL);
	if (ret && type) {
		struct acpi_device *adev;

		adev = type->find_companion(dev);
		if (!adev) {
			DBG("Unable to get handle for %s\n", dev_name(dev));
			ret = -ENODEV;
			goto out;
		}
		ret = acpi_bind_one(dev, adev);
		if (ret)
			goto out;
	}
	adev = ACPI_COMPANION(dev);
	if (!adev)
		goto out;

	if (type && type->setup)
		type->setup(dev);
	else if (adev->handler && adev->handler->bind)
		adev->handler->bind(dev);

 out:
#if ACPI_GLUE_DEBUG
	if (!ret) {
		struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };

		acpi_get_name(dev->archdata.acpi_handle,
			      ACPI_FULL_PATHNAME, &buffer);
		DBG("Device %s -> %s\n", dev->bus_id, (char *)buffer.pointer);
		kfree(buffer.pointer);
	} else
		DBG("Device %s -> No ACPI support\n", dev->bus_id);
		acpi_get_name(ACPI_HANDLE(dev), ACPI_FULL_PATHNAME, &buffer);
		DBG("Device %s -> %s\n", dev_name(dev), (char *)buffer.pointer);
		kfree(buffer.pointer);
	} else
		DBG("Device %s -> No ACPI support\n", dev_name(dev));
#endif

	return ret;
}

static int acpi_platform_notify_remove(struct device *dev)
{
	struct acpi_device *adev = ACPI_COMPANION(dev);
	struct acpi_bus_type *type;

	if (!adev)
		return 0;

	type = acpi_get_bus_type(dev);
	if (type && type->cleanup)
		type->cleanup(dev);
	else if (adev->handler && adev->handler->unbind)
		adev->handler->unbind(dev);

	acpi_unbind_one(dev);
	return 0;
}

static int __init init_acpi_device_notify(void)
{
	if (acpi_disabled)
		return 0;
	if (platform_notify || platform_notify_remove) {
		printk(KERN_ERR PREFIX "Can't use platform_notify\n");
		return 0;
	}
	platform_notify = acpi_platform_notify;
	platform_notify_remove = acpi_platform_notify_remove;
	return 0;
}

arch_initcall(init_acpi_device_notify);


#if defined(CONFIG_RTC_DRV_CMOS) || defined(CONFIG_RTC_DRV_CMOS_MODULE)

#ifdef CONFIG_PM
static u32 rtc_handler(void *context)
{
	acpi_clear_event(ACPI_EVENT_RTC);
	acpi_disable_event(ACPI_EVENT_RTC, 0);
	return ACPI_INTERRUPT_HANDLED;
}

static inline void rtc_wake_setup(void)
{
	acpi_install_fixed_event_handler(ACPI_EVENT_RTC, rtc_handler, NULL);
	/*
	 * After the RTC handler is installed, the Fixed_RTC event should
	 * be disabled. Only when the RTC alarm is set will it be enabled.
	 */
	acpi_clear_event(ACPI_EVENT_RTC);
	acpi_disable_event(ACPI_EVENT_RTC, 0);
}

static void rtc_wake_on(struct device *dev)
{
	acpi_clear_event(ACPI_EVENT_RTC);
	acpi_enable_event(ACPI_EVENT_RTC, 0);
}

static void rtc_wake_off(struct device *dev)
{
	acpi_disable_event(ACPI_EVENT_RTC, 0);
}
#else
#define rtc_wake_setup()	do{}while(0)
#define rtc_wake_on		NULL
#define rtc_wake_off		NULL
#endif

/* Every ACPI platform has a mc146818 compatible "cmos rtc".  Here we find
 * its device node and pass extra config data.  This helps its driver use
 * capabilities that the now-obsolete mc146818 didn't have, and informs it
 * that this board's RTC is wakeup-capable (per ACPI spec).
 */
#include <linux/mc146818rtc.h>

static struct cmos_rtc_board_info rtc_info;


/* PNP devices are registered in a subsys_initcall();
 * ACPI specifies the PNP IDs to use.
 */
#include <linux/pnp.h>

static int __init pnp_match(struct device *dev, void *data)
{
	static const char *ids[] = { "PNP0b00", "PNP0b01", "PNP0b02", };
	struct pnp_dev *pnp = to_pnp_dev(dev);
	int i;

	for (i = 0; i < ARRAY_SIZE(ids); i++) {
		if (compare_pnp_id(pnp->id, ids[i]) != 0)
			return 1;
	}
	return 0;
}

static struct device *__init get_rtc_dev(void)
{
	return bus_find_device(&pnp_bus_type, NULL, NULL, pnp_match);
}

static int __init acpi_rtc_init(void)
{
	struct device *dev = get_rtc_dev();

	if (acpi_disabled)
		return 0;

	if (dev) {
		rtc_wake_setup();
		rtc_info.wake_on = rtc_wake_on;
		rtc_info.wake_off = rtc_wake_off;

		/* workaround bug in some ACPI tables */
		if (acpi_gbl_FADT.month_alarm && !acpi_gbl_FADT.day_alarm) {
			DBG("bogus FADT month_alarm\n");
			acpi_gbl_FADT.month_alarm = 0;
		}

		rtc_info.rtc_day_alarm = acpi_gbl_FADT.day_alarm;
		rtc_info.rtc_mon_alarm = acpi_gbl_FADT.month_alarm;
		rtc_info.rtc_century = acpi_gbl_FADT.century;

		/* NOTE:  S4_RTC_WAKE is NOT currently useful to Linux */
		if (acpi_gbl_FADT.flags & ACPI_FADT_S4_RTC_WAKE)
			printk(PREFIX "RTC can wake from S4\n");


		dev->platform_data = &rtc_info;

		/* RTC always wakes from S1/S2/S3, and often S4/STD */
		device_init_wakeup(dev, 1);

		put_device(dev);
	} else
		DBG("RTC unavailable?\n");
	return 0;
}
/* do this between RTC subsys_initcall() and rtc_cmos driver_initcall() */
fs_initcall(acpi_rtc_init);

#endif
void __init init_acpi_device_notify(void)
{
	if (platform_notify || platform_notify_remove) {
		printk(KERN_ERR PREFIX "Can't use platform_notify\n");
		return;
	}
	platform_notify = acpi_platform_notify;
	platform_notify_remove = acpi_platform_notify_remove;
}
