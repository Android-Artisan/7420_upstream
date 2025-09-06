/*
 * scan.c - support for transforming the ACPI namespace into individual objects
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/acpi.h>
#include <linux/signal.h>
#include <linux/kthread.h>

#include <acpi/acpi_drivers.h>
#include <acpi/acinterp.h>	/* for acpi_ex_eisa_id_to_string() */

#define _COMPONENT		ACPI_BUS_COMPONENT
ACPI_MODULE_NAME("scan");
#define STRUCT_TO_INT(s)	(*((int*)&s))
#include <linux/dmi.h>
#include <linux/nls.h>
#include <linux/dma-mapping.h>

#include <asm/pgtable.h>

#include "internal.h"

#define _COMPONENT		ACPI_BUS_COMPONENT
ACPI_MODULE_NAME("scan");
extern struct acpi_device *acpi_root;

#define ACPI_BUS_CLASS			"system_bus"
#define ACPI_BUS_HID			"LNXSYBUS"
#define ACPI_BUS_DEVICE_NAME		"System Bus"

static LIST_HEAD(acpi_device_list);
static LIST_HEAD(acpi_bus_id_list);
DEFINE_SPINLOCK(acpi_device_lock);
LIST_HEAD(acpi_wakeup_device_list);
#define ACPI_IS_ROOT_DEVICE(device)    (!(device)->parent)

#define INVALID_ACPI_HANDLE	((acpi_handle)empty_zero_page)

/*
 * If set, devices will be hot-removed even if they cannot be put offline
 * gracefully (from the kernel's standpoint).
 */
bool acpi_force_hot_remove;

static const char *dummy_hid = "device";

static LIST_HEAD(acpi_dep_list);
static DEFINE_MUTEX(acpi_dep_list_lock);
static LIST_HEAD(acpi_bus_id_list);
static DEFINE_MUTEX(acpi_scan_lock);
static LIST_HEAD(acpi_scan_handlers_list);
DEFINE_MUTEX(acpi_device_lock);
LIST_HEAD(acpi_wakeup_device_list);
static DEFINE_MUTEX(acpi_hp_context_lock);

struct acpi_dep_data {
	struct list_head node;
	acpi_handle master;
	acpi_handle slave;
};

struct acpi_device_bus_id{
	char bus_id[15];
	unsigned int instance_no;
	struct list_head node;
};

/*
 * Creates hid/cid(s) string needed for modalias and uevent
 * e.g. on a device with hid:IBM0001 and cid:ACPI0001 you get:
 * char *modalias: "acpi:IBM0001:ACPI0001"
*/
static int create_modalias(struct acpi_device *acpi_dev, char *modalias,
			   int size)
{
	int len;
	int count;

	if (!acpi_dev->flags.hardware_id && !acpi_dev->flags.compatible_ids)
		return -ENODEV;

	len = snprintf(modalias, size, "acpi:");
	size -= len;

	if (acpi_dev->flags.hardware_id) {
		count = snprintf(&modalias[len], size, "%s:",
				 acpi_dev->pnp.hardware_id);
		if (count < 0 || count >= size)
			return -EINVAL;
		len += count;
		size -= count;
	}

	if (acpi_dev->flags.compatible_ids) {
		struct acpi_compatible_id_list *cid_list;
		int i;

		cid_list = acpi_dev->pnp.cid_list;
		for (i = 0; i < cid_list->count; i++) {
			count = snprintf(&modalias[len], size, "%s:",
					 cid_list->id[i].value);
			if (count < 0 || count >= size) {
				printk(KERN_ERR PREFIX "%s cid[%i] exceeds event buffer size",
				       acpi_dev->pnp.device_name, i);
				break;
			}
			len += count;
			size -= count;
		}
	}

	modalias[len] = '\0';
	return len;
}

static ssize_t
acpi_device_modalias_show(struct device *dev, struct device_attribute *attr, char *buf) {
	struct acpi_device *acpi_dev = to_acpi_device(dev);
	int len;

	/* Device has no HID and no CID or string is >1024 */
	len = create_modalias(acpi_dev, buf, 1024);
	if (len <= 0)
		return 0;
	buf[len++] = '\n';
	return len;
}
static DEVICE_ATTR(modalias, 0444, acpi_device_modalias_show, NULL);

static int acpi_bus_hot_remove_device(void *context)
{
	struct acpi_device *device;
	acpi_handle handle = context;
	struct acpi_object_list arg_list;
	union acpi_object arg;
	acpi_status status = AE_OK;

	if (acpi_bus_get_device(handle, &device))
		return 0;

	if (!device)
		return 0;

	ACPI_DEBUG_PRINT((ACPI_DB_INFO,
		"Hot-removing device %s...\n", device->dev.bus_id));


	if (acpi_bus_trim(device, 1)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				"Removing device failed\n"));
		return -1;
	}

	/* power off device */
	status = acpi_evaluate_object(handle, "_PS3", NULL, NULL);
	if (ACPI_FAILURE(status) && status != AE_NOT_FOUND)
		ACPI_DEBUG_PRINT((ACPI_DB_WARN,
				"Power-off device failed\n"));

	if (device->flags.lockable) {
		arg_list.count = 1;
		arg_list.pointer = &arg;
		arg.type = ACPI_TYPE_INTEGER;
		arg.integer.value = 0;
		acpi_evaluate_object(handle, "_LCK", &arg_list, NULL);
	}

	arg_list.count = 1;
	arg_list.pointer = &arg;
	arg.type = ACPI_TYPE_INTEGER;
	arg.integer.value = 1;

	/*
	 * TBD: _EJD support.
	 */
	status = acpi_evaluate_object(handle, "_EJ0", &arg_list, NULL);
	if (ACPI_FAILURE(status))
		return -ENODEV;
void acpi_scan_lock_acquire(void)
{
	mutex_lock(&acpi_scan_lock);
}
EXPORT_SYMBOL_GPL(acpi_scan_lock_acquire);

void acpi_scan_lock_release(void)
{
	mutex_unlock(&acpi_scan_lock);
}
EXPORT_SYMBOL_GPL(acpi_scan_lock_release);

void acpi_lock_hp_context(void)
{
	mutex_lock(&acpi_hp_context_lock);
}

void acpi_unlock_hp_context(void)
{
	mutex_unlock(&acpi_hp_context_lock);
}

void acpi_initialize_hp_context(struct acpi_device *adev,
				struct acpi_hotplug_context *hp,
				int (*notify)(struct acpi_device *, u32),
				void (*uevent)(struct acpi_device *, u32))
{
	acpi_lock_hp_context();
	hp->notify = notify;
	hp->uevent = uevent;
	acpi_set_hp_context(adev, hp);
	acpi_unlock_hp_context();
}
EXPORT_SYMBOL_GPL(acpi_initialize_hp_context);

int acpi_scan_add_handler(struct acpi_scan_handler *handler)
{
	if (!handler)
		return -EINVAL;

	list_add_tail(&handler->list_node, &acpi_scan_handlers_list);
	return 0;
}

int acpi_scan_add_handler_with_hotplug(struct acpi_scan_handler *handler,
				       const char *hotplug_profile_name)
{
	int error;

	error = acpi_scan_add_handler(handler);
	if (error)
		return error;

	acpi_sysfs_add_hotplug_profile(&handler->hotplug, hotplug_profile_name);
	return 0;
}

bool acpi_scan_is_offline(struct acpi_device *adev, bool uevent)
{
	struct acpi_device_physical_node *pn;
	bool offline = true;

	/*
	 * acpi_container_offline() calls this for all of the container's
	 * children under the container's physical_node_lock lock.
	 */
	mutex_lock_nested(&adev->physical_node_lock, SINGLE_DEPTH_NESTING);

	list_for_each_entry(pn, &adev->physical_node_list, node)
		if (device_supports_offline(pn->dev) && !pn->dev->offline) {
			if (uevent)
				kobject_uevent(&pn->dev->kobj, KOBJ_CHANGE);

			offline = false;
			break;
		}

	mutex_unlock(&adev->physical_node_lock);
	return offline;
}

static acpi_status acpi_bus_offline(acpi_handle handle, u32 lvl, void *data,
				    void **ret_p)
{
	struct acpi_device *device = NULL;
	struct acpi_device_physical_node *pn;
	bool second_pass = (bool)data;
	acpi_status status = AE_OK;

	if (acpi_bus_get_device(handle, &device))
		return AE_OK;

	if (device->handler && !device->handler->hotplug.enabled) {
		*ret_p = &device->dev;
		return AE_SUPPORT;
	}

	mutex_lock(&device->physical_node_lock);

	list_for_each_entry(pn, &device->physical_node_list, node) {
		int ret;

		if (second_pass) {
			/* Skip devices offlined by the first pass. */
			if (pn->put_online)
				continue;
		} else {
			pn->put_online = false;
		}
		ret = device_offline(pn->dev);
		if (acpi_force_hot_remove)
			continue;

		if (ret >= 0) {
			pn->put_online = !ret;
		} else {
			*ret_p = pn->dev;
			if (second_pass) {
				status = AE_ERROR;
				break;
			}
		}
	}

	mutex_unlock(&device->physical_node_lock);

	return status;
}

static acpi_status acpi_bus_online(acpi_handle handle, u32 lvl, void *data,
				   void **ret_p)
{
	struct acpi_device *device = NULL;
	struct acpi_device_physical_node *pn;

	if (acpi_bus_get_device(handle, &device))
		return AE_OK;

	mutex_lock(&device->physical_node_lock);

	list_for_each_entry(pn, &device->physical_node_list, node)
		if (pn->put_online) {
			device_online(pn->dev);
			pn->put_online = false;
		}

	mutex_unlock(&device->physical_node_lock);

	return AE_OK;
}

static int acpi_scan_try_to_offline(struct acpi_device *device)
{
	acpi_handle handle = device->handle;
	struct device *errdev = NULL;
	acpi_status status;

	/*
	 * Carry out two passes here and ignore errors in the first pass,
	 * because if the devices in question are memory blocks and
	 * CONFIG_MEMCG is set, one of the blocks may hold data structures
	 * that the other blocks depend on, but it is not known in advance which
	 * block holds them.
	 *
	 * If the first pass is successful, the second one isn't needed, though.
	 */
	status = acpi_walk_namespace(ACPI_TYPE_ANY, handle, ACPI_UINT32_MAX,
				     NULL, acpi_bus_offline, (void *)false,
				     (void **)&errdev);
	if (status == AE_SUPPORT) {
		dev_warn(errdev, "Offline disabled.\n");
		acpi_walk_namespace(ACPI_TYPE_ANY, handle, ACPI_UINT32_MAX,
				    acpi_bus_online, NULL, NULL, NULL);
		return -EPERM;
	}
	acpi_bus_offline(handle, 0, (void *)false, (void **)&errdev);
	if (errdev) {
		errdev = NULL;
		acpi_walk_namespace(ACPI_TYPE_ANY, handle, ACPI_UINT32_MAX,
				    NULL, acpi_bus_offline, (void *)true,
				    (void **)&errdev);
		if (!errdev || acpi_force_hot_remove)
			acpi_bus_offline(handle, 0, (void *)true,
					 (void **)&errdev);

		if (errdev && !acpi_force_hot_remove) {
			dev_warn(errdev, "Offline failed.\n");
			acpi_bus_online(handle, 0, NULL, NULL);
			acpi_walk_namespace(ACPI_TYPE_ANY, handle,
					    ACPI_UINT32_MAX, acpi_bus_online,
					    NULL, NULL, NULL);
			return -EBUSY;
		}
	}
	return 0;
}

static int acpi_scan_hot_remove(struct acpi_device *device)
{
	acpi_handle handle = device->handle;
	unsigned long long sta;
	acpi_status status;

	if (device->handler && device->handler->hotplug.demand_offline
	    && !acpi_force_hot_remove) {
		if (!acpi_scan_is_offline(device, true))
			return -EBUSY;
	} else {
		int error = acpi_scan_try_to_offline(device);
		if (error)
			return error;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO,
		"Hot-removing device %s...\n", dev_name(&device->dev)));

	acpi_bus_trim(device);

	acpi_evaluate_lck(handle, 0);
	/*
	 * TBD: _EJD support.
	 */
	status = acpi_evaluate_ej0(handle);
	if (status == AE_NOT_FOUND)
		return -ENODEV;
	else if (ACPI_FAILURE(status))
		return -EIO;

	/*
	 * Verify if eject was indeed successful.  If not, log an error
	 * message.  No need to call _OST since _EJ0 call was made OK.
	 */
	status = acpi_evaluate_integer(handle, "_STA", NULL, &sta);
	if (ACPI_FAILURE(status)) {
		acpi_handle_warn(handle,
			"Status check after eject failed (0x%x)\n", status);
	} else if (sta & ACPI_STA_DEVICE_ENABLED) {
		acpi_handle_warn(handle,
			"Eject incomplete - status 0x%llx\n", sta);
	}

	return 0;
}

static ssize_t
acpi_eject_store(struct device *d, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int ret = count;
	acpi_status status;
	acpi_object_type type = 0;
	struct acpi_device *acpi_device = to_acpi_device(d);
	struct task_struct *task;

	if ((!count) || (buf[0] != '1')) {
		return -EINVAL;
	}
#ifndef FORCE_EJECT
	if (acpi_device->driver == NULL) {
		ret = -ENODEV;
		goto err;
	}
#endif
	status = acpi_get_type(acpi_device->handle, &type);
	if (ACPI_FAILURE(status) || (!acpi_device->flags.ejectable)) {
		ret = -ENODEV;
		goto err;
	}

	/* remove the device in another thread to fix the deadlock issue */
	task = kthread_run(acpi_bus_hot_remove_device,
				acpi_device->handle, "acpi_hot_remove_device");
	if (IS_ERR(task))
		ret = PTR_ERR(task);
err:
	return ret;
}

static DEVICE_ATTR(eject, 0200, NULL, acpi_eject_store);

static ssize_t
acpi_device_hid_show(struct device *dev, struct device_attribute *attr, char *buf) {
	struct acpi_device *acpi_dev = to_acpi_device(dev);

	return sprintf(buf, "%s\n", acpi_dev->pnp.hardware_id);
}
static DEVICE_ATTR(hid, 0444, acpi_device_hid_show, NULL);

static ssize_t
acpi_device_path_show(struct device *dev, struct device_attribute *attr, char *buf) {
	struct acpi_device *acpi_dev = to_acpi_device(dev);
	struct acpi_buffer path = {ACPI_ALLOCATE_BUFFER, NULL};
	int result;

	result = acpi_get_name(acpi_dev->handle, ACPI_FULL_PATHNAME, &path);
	if(result)
		goto end;

	result = sprintf(buf, "%s\n", (char*)path.pointer);
	kfree(path.pointer);
  end:
	return result;
}
static DEVICE_ATTR(path, 0444, acpi_device_path_show, NULL);

static int acpi_device_setup_files(struct acpi_device *dev)
{
	acpi_status status;
	acpi_handle temp;
	int result = 0;

	/*
	 * Devices gotten from FADT don't have a "path" attribute
	 */
	if(dev->handle) {
		result = device_create_file(&dev->dev, &dev_attr_path);
		if(result)
			goto end;
	}

	if(dev->flags.hardware_id) {
		result = device_create_file(&dev->dev, &dev_attr_hid);
		if(result)
			goto end;
	}

	if (dev->flags.hardware_id || dev->flags.compatible_ids){
		result = device_create_file(&dev->dev, &dev_attr_modalias);
		if(result)
			goto end;
	}

        /*
         * If device has _EJ0, 'eject' file is created that is used to trigger
         * hot-removal function from userland.
         */
	status = acpi_get_handle(dev->handle, "_EJ0", &temp);
	if (ACPI_SUCCESS(status))
		result = device_create_file(&dev->dev, &dev_attr_eject);
  end:
	return result;
}

static void acpi_device_remove_files(struct acpi_device *dev)
{
	acpi_status status;
	acpi_handle temp;

	/*
	 * If device has _EJ0, 'eject' file is created that is used to trigger
	 * hot-removal function from userland.
	 */
	status = acpi_get_handle(dev->handle, "_EJ0", &temp);
	if (ACPI_SUCCESS(status))
		device_remove_file(&dev->dev, &dev_attr_eject);

	if (dev->flags.hardware_id || dev->flags.compatible_ids)
		device_remove_file(&dev->dev, &dev_attr_modalias);

	if(dev->flags.hardware_id)
		device_remove_file(&dev->dev, &dev_attr_hid);
	if(dev->handle)
		device_remove_file(&dev->dev, &dev_attr_path);
}
/* --------------------------------------------------------------------------
			ACPI Bus operations
   -------------------------------------------------------------------------- */

int acpi_match_device_ids(struct acpi_device *device,
			  const struct acpi_device_id *ids)
{
	const struct acpi_device_id *id;

	if (device->flags.hardware_id) {
		for (id = ids; id->id[0]; id++) {
			if (!strcmp((char*)id->id, device->pnp.hardware_id))
				return 0;
		}
	}

	if (device->flags.compatible_ids) {
		struct acpi_compatible_id_list *cid_list = device->pnp.cid_list;
		int i;

		for (id = ids; id->id[0]; id++) {
			/* compare multiple _CID entries against driver ids */
			for (i = 0; i < cid_list->count; i++) {
				if (!strcmp((char*)id->id,
					    cid_list->id[i].value))
					return 0;
			}
		}
	}

	return -ENOENT;
}
EXPORT_SYMBOL(acpi_match_device_ids);
static int acpi_scan_device_not_present(struct acpi_device *adev)
{
	if (!acpi_device_enumerated(adev)) {
		dev_warn(&adev->dev, "Still not present\n");
		return -EALREADY;
	}
	acpi_bus_trim(adev);
	return 0;
}

static int acpi_scan_device_check(struct acpi_device *adev)
{
	int error;

	acpi_bus_get_status(adev);
	if (adev->status.present || adev->status.functional) {
		/*
		 * This function is only called for device objects for which
		 * matching scan handlers exist.  The only situation in which
		 * the scan handler is not attached to this device object yet
		 * is when the device has just appeared (either it wasn't
		 * present at all before or it was removed and then added
		 * again).
		 */
		if (adev->handler) {
			dev_dbg(&adev->dev, "Already enumerated\n");
			return 0;
		}
		error = acpi_bus_scan(adev->handle);
		if (error) {
			dev_warn(&adev->dev, "Namespace scan failure\n");
			return error;
		}
	} else {
		error = acpi_scan_device_not_present(adev);
	}
	return error;
}

static int acpi_scan_bus_check(struct acpi_device *adev)
{
	struct acpi_scan_handler *handler = adev->handler;
	struct acpi_device *child;
	int error;

	acpi_bus_get_status(adev);
	if (!(adev->status.present || adev->status.functional)) {
		acpi_scan_device_not_present(adev);
		return 0;
	}
	if (handler && handler->hotplug.scan_dependent)
		return handler->hotplug.scan_dependent(adev);

	error = acpi_bus_scan(adev->handle);
	if (error) {
		dev_warn(&adev->dev, "Namespace scan failure\n");
		return error;
	}
	list_for_each_entry(child, &adev->children, node) {
		error = acpi_scan_bus_check(child);
		if (error)
			return error;
	}
	return 0;
}

static int acpi_generic_hotplug_event(struct acpi_device *adev, u32 type)
{
	switch (type) {
	case ACPI_NOTIFY_BUS_CHECK:
		return acpi_scan_bus_check(adev);
	case ACPI_NOTIFY_DEVICE_CHECK:
		return acpi_scan_device_check(adev);
	case ACPI_NOTIFY_EJECT_REQUEST:
	case ACPI_OST_EC_OSPM_EJECT:
		if (adev->handler && !adev->handler->hotplug.enabled) {
			dev_info(&adev->dev, "Eject disabled\n");
			return -EPERM;
		}
		acpi_evaluate_ost(adev->handle, ACPI_NOTIFY_EJECT_REQUEST,
				  ACPI_OST_SC_EJECT_IN_PROGRESS, NULL);
		return acpi_scan_hot_remove(adev);
	}
	return -EINVAL;
}

void acpi_device_hotplug(struct acpi_device *adev, u32 src)
{
	u32 ost_code = ACPI_OST_SC_NON_SPECIFIC_FAILURE;
	int error = -ENODEV;

	lock_device_hotplug();
	mutex_lock(&acpi_scan_lock);

	/*
	 * The device object's ACPI handle cannot become invalid as long as we
	 * are holding acpi_scan_lock, but it might have become invalid before
	 * that lock was acquired.
	 */
	if (adev->handle == INVALID_ACPI_HANDLE)
		goto err_out;

	if (adev->flags.is_dock_station) {
		error = dock_notify(adev, src);
	} else if (adev->flags.hotplug_notify) {
		error = acpi_generic_hotplug_event(adev, src);
		if (error == -EPERM) {
			ost_code = ACPI_OST_SC_EJECT_NOT_SUPPORTED;
			goto err_out;
		}
	} else {
		int (*notify)(struct acpi_device *, u32);

		acpi_lock_hp_context();
		notify = adev->hp ? adev->hp->notify : NULL;
		acpi_unlock_hp_context();
		/*
		 * There may be additional notify handlers for device objects
		 * without the .event() callback, so ignore them here.
		 */
		if (notify)
			error = notify(adev, src);
		else
			goto out;
	}
	if (!error)
		ost_code = ACPI_OST_SC_SUCCESS;

 err_out:
	acpi_evaluate_ost(adev->handle, src, ost_code, NULL);

 out:
	acpi_bus_put_acpi_device(adev);
	mutex_unlock(&acpi_scan_lock);
	unlock_device_hotplug();
}

static void acpi_free_power_resources_lists(struct acpi_device *device)
{
	int i;

	if (device->wakeup.flags.valid)
		acpi_power_resources_list_free(&device->wakeup.resources);

	if (!device->power.flags.power_resources)
		return;

	for (i = ACPI_STATE_D0; i <= ACPI_STATE_D3_HOT; i++) {
		struct acpi_device_power_state *ps = &device->power.states[i];
		acpi_power_resources_list_free(&ps->resources);
	}
}

static void acpi_device_release(struct device *dev)
{
	struct acpi_device *acpi_dev = to_acpi_device(dev);

	kfree(acpi_dev->pnp.cid_list);
	kfree(acpi_dev);
}

static int acpi_device_suspend(struct device *dev, pm_message_t state)
{
	struct acpi_device *acpi_dev = to_acpi_device(dev);
	struct acpi_driver *acpi_drv = acpi_dev->driver;

	if (acpi_drv && acpi_drv->ops.suspend)
		return acpi_drv->ops.suspend(acpi_dev, state);
	return 0;
}

static int acpi_device_resume(struct device *dev)
{
	struct acpi_device *acpi_dev = to_acpi_device(dev);
	struct acpi_driver *acpi_drv = acpi_dev->driver;

	if (acpi_drv && acpi_drv->ops.resume)
		return acpi_drv->ops.resume(acpi_dev);
	return 0;
}

static int acpi_bus_match(struct device *dev, struct device_driver *drv)
{
	struct acpi_device *acpi_dev = to_acpi_device(dev);
	struct acpi_driver *acpi_drv = to_acpi_driver(drv);

	return !acpi_match_device_ids(acpi_dev, acpi_drv->ids);
}

static int acpi_device_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct acpi_device *acpi_dev = to_acpi_device(dev);
	int len;

	if (add_uevent_var(env, "MODALIAS="))
		return -ENOMEM;
	len = create_modalias(acpi_dev, &env->buf[env->buflen - 1],
			      sizeof(env->buf) - env->buflen);
	if (len >= (sizeof(env->buf) - env->buflen))
		return -ENOMEM;
	env->buflen += len;
	return 0;
}

static int acpi_bus_driver_init(struct acpi_device *, struct acpi_driver *);
static int acpi_start_single_object(struct acpi_device *);
static int acpi_device_probe(struct device * dev)
{
	struct acpi_device *acpi_dev = to_acpi_device(dev);
	struct acpi_driver *acpi_drv = to_acpi_driver(dev->driver);
	int ret;

	ret = acpi_bus_driver_init(acpi_dev, acpi_drv);
	if (!ret) {
		if (acpi_dev->bus_ops.acpi_op_start)
			acpi_start_single_object(acpi_dev);
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			"Found driver [%s] for device [%s]\n",
			acpi_drv->name, acpi_dev->pnp.bus_id));
		get_device(dev);
	}
	return ret;
}

static int acpi_device_remove(struct device * dev)
{
	struct acpi_device *acpi_dev = to_acpi_device(dev);
	struct acpi_driver *acpi_drv = acpi_dev->driver;

	if (acpi_drv) {
		if (acpi_drv->ops.stop)
			acpi_drv->ops.stop(acpi_dev, acpi_dev->removal_type);
		if (acpi_drv->ops.remove)
			acpi_drv->ops.remove(acpi_dev, acpi_dev->removal_type);
	}
	acpi_dev->driver = NULL;
	acpi_driver_data(dev) = NULL;

	put_device(dev);
	return 0;
}

static void acpi_device_shutdown(struct device *dev)
{
	struct acpi_device *acpi_dev = to_acpi_device(dev);
	struct acpi_driver *acpi_drv = acpi_dev->driver;

	if (acpi_drv && acpi_drv->ops.shutdown)
		acpi_drv->ops.shutdown(acpi_dev);

	return ;
}

struct bus_type acpi_bus_type = {
	.name		= "acpi",
	.suspend	= acpi_device_suspend,
	.resume		= acpi_device_resume,
	.shutdown	= acpi_device_shutdown,
	.match		= acpi_bus_match,
	.probe		= acpi_device_probe,
	.remove		= acpi_device_remove,
	.uevent		= acpi_device_uevent,
};

static int acpi_device_register(struct acpi_device *device,
				 struct acpi_device *parent)
	acpi_free_properties(acpi_dev);
	acpi_free_pnp_ids(&acpi_dev->pnp);
	acpi_free_power_resources_lists(acpi_dev);
	kfree(acpi_dev);
}

static void acpi_device_del(struct acpi_device *device)
{
	mutex_lock(&acpi_device_lock);
	if (device->parent)
		list_del(&device->node);

	list_del(&device->wakeup_list);
	mutex_unlock(&acpi_device_lock);

	acpi_power_add_remove_device(device, false);
	acpi_device_remove_files(device);
	if (device->remove)
		device->remove(device);

	device_del(&device->dev);
}

static LIST_HEAD(acpi_device_del_list);
static DEFINE_MUTEX(acpi_device_del_lock);

static void acpi_device_del_work_fn(struct work_struct *work_not_used)
{
	for (;;) {
		struct acpi_device *adev;

		mutex_lock(&acpi_device_del_lock);

		if (list_empty(&acpi_device_del_list)) {
			mutex_unlock(&acpi_device_del_lock);
			break;
		}
		adev = list_first_entry(&acpi_device_del_list,
					struct acpi_device, del_list);
		list_del(&adev->del_list);

		mutex_unlock(&acpi_device_del_lock);

		acpi_device_del(adev);
		/*
		 * Drop references to all power resources that might have been
		 * used by the device.
		 */
		acpi_power_transition(adev, ACPI_STATE_D3_COLD);
		put_device(&adev->dev);
	}
}

/**
 * acpi_scan_drop_device - Drop an ACPI device object.
 * @handle: Handle of an ACPI namespace node, not used.
 * @context: Address of the ACPI device object to drop.
 *
 * This is invoked by acpi_ns_delete_node() during the removal of the ACPI
 * namespace node the device object pointed to by @context is attached to.
 *
 * The unregistration is carried out asynchronously to avoid running
 * acpi_device_del() under the ACPICA's namespace mutex and the list is used to
 * ensure the correct ordering (the device objects must be unregistered in the
 * same order in which the corresponding namespace nodes are deleted).
 */
static void acpi_scan_drop_device(acpi_handle handle, void *context)
{
	static DECLARE_WORK(work, acpi_device_del_work_fn);
	struct acpi_device *adev = context;

	mutex_lock(&acpi_device_del_lock);

	/*
	 * Use the ACPI hotplug workqueue which is ordered, so this work item
	 * won't run after any hotplug work items submitted subsequently.  That
	 * prevents attempts to register device objects identical to those being
	 * deleted from happening concurrently (such attempts result from
	 * hotplug events handled via the ACPI hotplug workqueue).  It also will
	 * run after all of the work items submitted previosuly, which helps
	 * those work items to ensure that they are not accessing stale device
	 * objects.
	 */
	if (list_empty(&acpi_device_del_list))
		acpi_queue_hotplug_work(&work);

	list_add_tail(&adev->del_list, &acpi_device_del_list);
	/* Make acpi_ns_validate_handle() return NULL for this handle. */
	adev->handle = INVALID_ACPI_HANDLE;

	mutex_unlock(&acpi_device_del_lock);
}

static int acpi_get_device_data(acpi_handle handle, struct acpi_device **device,
				void (*callback)(void *))
{
	acpi_status status;

	if (!device)
		return -EINVAL;

	*device = NULL;

	status = acpi_get_data_full(handle, acpi_scan_drop_device,
				    (void **)device, callback);
	if (ACPI_FAILURE(status) || !*device) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "No context for object [%p]\n",
				  handle));
		return -ENODEV;
	}
	return 0;
}

int acpi_bus_get_device(acpi_handle handle, struct acpi_device **device)
{
	return acpi_get_device_data(handle, device, NULL);
}
EXPORT_SYMBOL(acpi_bus_get_device);

static void get_acpi_device(void *dev)
{
	if (dev)
		get_device(&((struct acpi_device *)dev)->dev);
}

struct acpi_device *acpi_bus_get_acpi_device(acpi_handle handle)
{
	struct acpi_device *adev = NULL;

	acpi_get_device_data(handle, &adev, get_acpi_device);
	return adev;
}

void acpi_bus_put_acpi_device(struct acpi_device *adev)
{
	put_device(&adev->dev);
}

int acpi_device_add(struct acpi_device *device,
		    void (*release)(struct device *))
{
	int result;
	struct acpi_device_bus_id *acpi_device_bus_id, *new_bus_id;
	int found = 0;

	if (device->handle) {
		acpi_status status;

		status = acpi_attach_data(device->handle, acpi_scan_drop_device,
					  device);
		if (ACPI_FAILURE(status)) {
			acpi_handle_err(device->handle,
					"Unable to attach device data\n");
			return -ENODEV;
		}
	}

	/*
	 * Linkage
	 * -------
	 * Link this device to its parent and siblings.
	 */
	INIT_LIST_HEAD(&device->children);
	INIT_LIST_HEAD(&device->node);
	INIT_LIST_HEAD(&device->g_list);
	INIT_LIST_HEAD(&device->wakeup_list);

	new_bus_id = kzalloc(sizeof(struct acpi_device_bus_id), GFP_KERNEL);
	if (!new_bus_id) {
		printk(KERN_ERR PREFIX "Memory allocation error\n");
		return -ENOMEM;
	}

	spin_lock(&acpi_device_lock);
	INIT_LIST_HEAD(&device->wakeup_list);
	INIT_LIST_HEAD(&device->physical_node_list);
	INIT_LIST_HEAD(&device->del_list);
	mutex_init(&device->physical_node_lock);

	new_bus_id = kzalloc(sizeof(struct acpi_device_bus_id), GFP_KERNEL);
	if (!new_bus_id) {
		pr_err(PREFIX "Memory allocation error\n");
		result = -ENOMEM;
		goto err_detach;
	}

	mutex_lock(&acpi_device_lock);
	/*
	 * Find suitable bus_id and instance number in acpi_bus_id_list
	 * If failed, create one and link it into acpi_bus_id_list
	 */
	list_for_each_entry(acpi_device_bus_id, &acpi_bus_id_list, node) {
		if(!strcmp(acpi_device_bus_id->bus_id, device->flags.hardware_id? device->pnp.hardware_id : "device")) {
			acpi_device_bus_id->instance_no ++;
		if (!strcmp(acpi_device_bus_id->bus_id,
			    acpi_device_hid(device))) {
			acpi_device_bus_id->instance_no++;
			found = 1;
			kfree(new_bus_id);
			break;
		}
	}
	if(!found) {
		acpi_device_bus_id = new_bus_id;
		strcpy(acpi_device_bus_id->bus_id, device->flags.hardware_id ? device->pnp.hardware_id : "device");
		acpi_device_bus_id->instance_no = 0;
		list_add_tail(&acpi_device_bus_id->node, &acpi_bus_id_list);
	}
	sprintf(device->dev.bus_id, "%s:%02x", acpi_device_bus_id->bus_id, acpi_device_bus_id->instance_no);

	if (device->parent) {
		list_add_tail(&device->node, &device->parent->children);
		list_add_tail(&device->g_list, &device->parent->g_list);
	} else
		list_add_tail(&device->g_list, &acpi_device_list);
	if (device->wakeup.flags.valid)
		list_add_tail(&device->wakeup_list, &acpi_wakeup_device_list);
	spin_unlock(&acpi_device_lock);

	if (device->parent)
		device->dev.parent = &parent->dev;
	device->dev.bus = &acpi_bus_type;
	device_initialize(&device->dev);
	device->dev.release = &acpi_device_release;
	result = device_add(&device->dev);
	if(result) {
		dev_err(&device->dev, "Error adding device\n");
		goto end;
	}

	result = acpi_device_setup_files(device);
	if(result)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Error creating sysfs interface for device %s\n", device->dev.bus_id));

	device->removal_type = ACPI_BUS_REMOVAL_NORMAL;
	return 0;
  end:
	spin_lock(&acpi_device_lock);
	if (device->parent) {
		list_del(&device->node);
		list_del(&device->g_list);
	} else
		list_del(&device->g_list);
	list_del(&device->wakeup_list);
	spin_unlock(&acpi_device_lock);
	return result;
}

static void acpi_device_unregister(struct acpi_device *device, int type)
{
	spin_lock(&acpi_device_lock);
	if (device->parent) {
		list_del(&device->node);
		list_del(&device->g_list);
	} else
		list_del(&device->g_list);

	list_del(&device->wakeup_list);
	spin_unlock(&acpi_device_lock);

	acpi_detach_data(device->handle, acpi_bus_data_handler);

	acpi_device_remove_files(device);
	device_unregister(&device->dev);
}

/* --------------------------------------------------------------------------
                                 Driver Management
   -------------------------------------------------------------------------- */
/**
 * acpi_bus_driver_init - add a device to a driver
 * @device: the device to add and initialize
 * @driver: driver for the device
 *
 * Used to initialize a device via its device driver.  Called whenever a 
 * driver is bound to a device.  Invokes the driver's add() ops.
 */
static int
acpi_bus_driver_init(struct acpi_device *device, struct acpi_driver *driver)
{
	int result = 0;


	if (!device || !driver)
		return -EINVAL;

	if (!driver->ops.add)
		return -ENOSYS;

	result = driver->ops.add(device);
	if (result) {
		device->driver = NULL;
		acpi_driver_data(device) = NULL;
		return result;
	}

	device->driver = driver;

	/*
	 * TBD - Configuration Management: Assign resources to device based
	 * upon possible configuration and currently allocated resources.
	 */

	ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			  "Driver successfully bound to device\n"));
	return 0;
}

static int acpi_start_single_object(struct acpi_device *device)
{
	int result = 0;
	struct acpi_driver *driver;


	if (!(driver = device->driver))
		return 0;

	if (driver->ops.start) {
		result = driver->ops.start(device);
		if (result && driver->ops.remove)
			driver->ops.remove(device, ACPI_BUS_REMOVAL_NORMAL);
	}

	return result;
}

/**
 * acpi_bus_register_driver - register a driver with the ACPI bus
 * @driver: driver being registered
 *
 * Registers a driver with the ACPI bus.  Searches the namespace for all
 * devices that match the driver's criteria and binds.  Returns zero for
 * success or a negative error status for failure.
 */
int acpi_bus_register_driver(struct acpi_driver *driver)
{
	int ret;

	if (acpi_disabled)
		return -ENODEV;
	driver->drv.name = driver->name;
	driver->drv.bus = &acpi_bus_type;
	driver->drv.owner = driver->owner;

	ret = driver_register(&driver->drv);
	return ret;
}

EXPORT_SYMBOL(acpi_bus_register_driver);

/**
 * acpi_bus_unregister_driver - unregisters a driver with the APIC bus
 * @driver: driver to unregister
 *
 * Unregisters a driver with the ACPI bus.  Searches the namespace for all
 * devices that match the driver's criteria and unbinds.
 */
void acpi_bus_unregister_driver(struct acpi_driver *driver)
{
	driver_unregister(&driver->drv);
}

EXPORT_SYMBOL(acpi_bus_unregister_driver);

/* --------------------------------------------------------------------------
                                 Device Enumeration
   -------------------------------------------------------------------------- */
	if (!found) {
		acpi_device_bus_id = new_bus_id;
		strcpy(acpi_device_bus_id->bus_id, acpi_device_hid(device));
		acpi_device_bus_id->instance_no = 0;
		list_add_tail(&acpi_device_bus_id->node, &acpi_bus_id_list);
	}
	dev_set_name(&device->dev, "%s:%02x", acpi_device_bus_id->bus_id, acpi_device_bus_id->instance_no);

	if (device->parent)
		list_add_tail(&device->node, &device->parent->children);

	if (device->wakeup.flags.valid)
		list_add_tail(&device->wakeup_list, &acpi_wakeup_device_list);
	mutex_unlock(&acpi_device_lock);

	if (device->parent)
		device->dev.parent = &device->parent->dev;
	device->dev.bus = &acpi_bus_type;
	device->dev.release = release;
	result = device_add(&device->dev);
	if (result) {
		dev_err(&device->dev, "Error registering device\n");
		goto err;
	}

	result = acpi_device_setup_files(device);
	if (result)
		printk(KERN_ERR PREFIX "Error creating sysfs interface for device %s\n",
		       dev_name(&device->dev));

	return 0;

 err:
	mutex_lock(&acpi_device_lock);
	if (device->parent)
		list_del(&device->node);
	list_del(&device->wakeup_list);
	mutex_unlock(&acpi_device_lock);

 err_detach:
	acpi_detach_data(device->handle, acpi_scan_drop_device);
	return result;
}

/* --------------------------------------------------------------------------
                                 Device Enumeration
   -------------------------------------------------------------------------- */
static struct acpi_device *acpi_bus_get_parent(acpi_handle handle)
{
	struct acpi_device *device = NULL;
	acpi_status status;

	/*
	 * Fixed hardware devices do not appear in the namespace and do not
	 * have handles, but we fabricate acpi_devices for them, so we have
	 * to deal with them specially.
	 */
	if (!handle)
		return acpi_root;

	do {
		status = acpi_get_parent(handle, &handle);
		if (ACPI_FAILURE(status))
			return status == AE_NULL_ENTRY ? NULL : acpi_root;
	} while (acpi_bus_get_device(handle, &device));
	return device;
}

acpi_status
acpi_bus_get_ejd(acpi_handle handle, acpi_handle *ejd)
{
	acpi_status status;
	acpi_handle tmp;
	struct acpi_buffer buffer = {ACPI_ALLOCATE_BUFFER, NULL};
	union acpi_object *obj;

	status = acpi_get_handle(handle, "_EJD", &tmp);
	if (ACPI_FAILURE(status))
		return status;

	status = acpi_evaluate_object(handle, "_EJD", NULL, &buffer);
	if (ACPI_SUCCESS(status)) {
		obj = buffer.pointer;
		status = acpi_get_handle(ACPI_ROOT_OBJECT, obj->string.pointer,
					 ejd);
		kfree(buffer.pointer);
	}
	return status;
}
EXPORT_SYMBOL_GPL(acpi_bus_get_ejd);

void acpi_bus_data_handler(acpi_handle handle, u32 function, void *context)
{

	/* TBD */

	return;
}

static int acpi_bus_get_perf_flags(struct acpi_device *device)
{
	device->performance.state = ACPI_STATE_UNKNOWN;
	return 0;
}

static acpi_status
acpi_bus_extract_wakeup_device_power_package(struct acpi_device *device,
					     union acpi_object *package)
{
	int i = 0;
	union acpi_object *element = NULL;

	if (!device || !package || (package->package.count < 2))
		return AE_BAD_PARAMETER;

	element = &(package->package.elements[0]);
	if (!element)
		return AE_BAD_PARAMETER;
static int acpi_bus_extract_wakeup_device_power_package(acpi_handle handle,
					struct acpi_device_wakeup *wakeup)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *package = NULL;
	union acpi_object *element = NULL;
	acpi_status status;
	int err = -ENODATA;

	if (!wakeup)
		return -EINVAL;

	INIT_LIST_HEAD(&wakeup->resources);

	/* _PRW */
	status = acpi_evaluate_object(handle, "_PRW", NULL, &buffer);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status, "Evaluating _PRW"));
		return err;
	}

	package = (union acpi_object *)buffer.pointer;

	if (!package || package->package.count < 2)
		goto out;

	element = &(package->package.elements[0]);
	if (!element)
		goto out;

	if (element->type == ACPI_TYPE_PACKAGE) {
		if ((element->package.count < 2) ||
		    (element->package.elements[0].type !=
		     ACPI_TYPE_LOCAL_REFERENCE)
		    || (element->package.elements[1].type != ACPI_TYPE_INTEGER))
			return AE_BAD_DATA;
		device->wakeup.gpe_device =
		    element->package.elements[0].reference.handle;
		device->wakeup.gpe_number =
		    (u32) element->package.elements[1].integer.value;
	} else if (element->type == ACPI_TYPE_INTEGER) {
		device->wakeup.gpe_number = element->integer.value;
	} else
		return AE_BAD_DATA;

	element = &(package->package.elements[1]);
	if (element->type != ACPI_TYPE_INTEGER) {
		return AE_BAD_DATA;
	}
	device->wakeup.sleep_state = element->integer.value;

	if ((package->package.count - 2) > ACPI_MAX_HANDLES) {
		return AE_NO_MEMORY;
	}
	device->wakeup.resources.count = package->package.count - 2;
	for (i = 0; i < device->wakeup.resources.count; i++) {
		element = &(package->package.elements[i + 2]);
		if (element->type != ACPI_TYPE_LOCAL_REFERENCE)
			return AE_BAD_DATA;

		device->wakeup.resources.handles[i] = element->reference.handle;
	}

	return AE_OK;
}

static int acpi_bus_get_wakeup_device_flags(struct acpi_device *device)
{
	acpi_status status = 0;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *package = NULL;
	int psw_error;

	struct acpi_device_id button_device_ids[] = {
		{"PNP0C0D", 0},
		{"PNP0C0C", 0},
		{"PNP0C0E", 0},
		{"", 0},
	};

	/* _PRW */
	status = acpi_evaluate_object(device->handle, "_PRW", NULL, &buffer);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status, "Evaluating _PRW"));
		goto end;
	}

	package = (union acpi_object *)buffer.pointer;
	status = acpi_bus_extract_wakeup_device_power_package(device, package);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status, "Extracting _PRW package"));
		goto end;
	}

	kfree(buffer.pointer);

	device->wakeup.flags.valid = 1;
			goto out;

		wakeup->gpe_device =
		    element->package.elements[0].reference.handle;
		wakeup->gpe_number =
		    (u32) element->package.elements[1].integer.value;
	} else if (element->type == ACPI_TYPE_INTEGER) {
		wakeup->gpe_device = NULL;
		wakeup->gpe_number = element->integer.value;
	} else {
		goto out;
	}

	element = &(package->package.elements[1]);
	if (element->type != ACPI_TYPE_INTEGER)
		goto out;

	wakeup->sleep_state = element->integer.value;

	err = acpi_extract_power_resources(package, 2, &wakeup->resources);
	if (err)
		goto out;

	if (!list_empty(&wakeup->resources)) {
		int sleep_state;

		err = acpi_power_wakeup_list_init(&wakeup->resources,
						  &sleep_state);
		if (err) {
			acpi_handle_warn(handle, "Retrieving current states "
					 "of wakeup power resources failed\n");
			acpi_power_resources_list_free(&wakeup->resources);
			goto out;
		}
		if (sleep_state < wakeup->sleep_state) {
			acpi_handle_warn(handle, "Overriding _PRW sleep state "
					 "(S%d) by S%d from power resources\n",
					 (int)wakeup->sleep_state, sleep_state);
			wakeup->sleep_state = sleep_state;
		}
	}

 out:
	kfree(buffer.pointer);
	return err;
}

static void acpi_wakeup_gpe_init(struct acpi_device *device)
{
	static const struct acpi_device_id button_device_ids[] = {
		{"PNP0C0C", 0},
		{"PNP0C0D", 0},
		{"PNP0C0E", 0},
		{"", 0},
	};
	struct acpi_device_wakeup *wakeup = &device->wakeup;
	acpi_status status;
	acpi_event_status event_status;

	wakeup->flags.notifier_present = 0;

	/* Power button, Lid switch always enable wakeup */
	if (!acpi_match_device_ids(device, button_device_ids)) {
		wakeup->flags.run_wake = 1;
		if (!acpi_match_device_ids(device, &button_device_ids[1])) {
			/* Do not use Lid/sleep button for S5 wakeup */
			if (wakeup->sleep_state == ACPI_STATE_S5)
				wakeup->sleep_state = ACPI_STATE_S4;
		}
		acpi_mark_gpe_for_wake(wakeup->gpe_device, wakeup->gpe_number);
		device_set_wakeup_capable(&device->dev, true);
		return;
	}

	acpi_setup_gpe_for_wake(device->handle, wakeup->gpe_device,
				wakeup->gpe_number);
	status = acpi_get_gpe_status(wakeup->gpe_device, wakeup->gpe_number,
				     &event_status);
	if (ACPI_FAILURE(status))
		return;

	wakeup->flags.run_wake = !!(event_status & ACPI_EVENT_FLAG_HAS_HANDLER);
}

static void acpi_bus_get_wakeup_device_flags(struct acpi_device *device)
{
	int err;

	/* Presence of _PRW indicates wake capable */
	if (!acpi_has_method(device->handle, "_PRW"))
		return;

	err = acpi_bus_extract_wakeup_device_power_package(device->handle,
							   &device->wakeup);
	if (err) {
		dev_err(&device->dev, "_PRW evaluation error: %d\n", err);
		return;
	}

	device->wakeup.flags.valid = 1;
	device->wakeup.prepare_count = 0;
	acpi_wakeup_gpe_init(device);
	/* Call _PSW/_DSW object to disable its ability to wake the sleeping
	 * system for the ACPI device with the _PRW object.
	 * The _PSW object is depreciated in ACPI 3.0 and is replaced by _DSW.
	 * So it is necessary to call _DSW object first. Only when it is not
	 * present will the _PSW object used.
	 */
	psw_error = acpi_device_sleep_wake(device, 0, 0, 0);
	if (psw_error)
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				"error in _DSW or _PSW evaluation\n"));

	/* Power button, Lid switch always enable wakeup */
	if (!acpi_match_device_ids(device, button_device_ids))
		device->wakeup.flags.run_wake = 1;

      end:
	if (ACPI_FAILURE(status))
		device->flags.wake_capable = 0;
	return 0;
}

static int acpi_bus_get_power_flags(struct acpi_device *device)
{
	acpi_status status = 0;
	acpi_handle handle = NULL;
	u32 i = 0;

	err = acpi_device_sleep_wake(device, 0, 0, 0);
	if (err)
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				"error in _DSW or _PSW evaluation\n"));
}

static void acpi_bus_init_power_state(struct acpi_device *device, int state)
{
	struct acpi_device_power_state *ps = &device->power.states[state];
	char pathname[5] = { '_', 'P', 'R', '0' + state, '\0' };
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	acpi_status status;

	INIT_LIST_HEAD(&ps->resources);

	/* Evaluate "_PRx" to get referenced power resources */
	status = acpi_evaluate_object(device->handle, pathname, NULL, &buffer);
	if (ACPI_SUCCESS(status)) {
		union acpi_object *package = buffer.pointer;

		if (buffer.length && package
		    && package->type == ACPI_TYPE_PACKAGE
		    && package->package.count)
			acpi_extract_power_resources(package, 0, &ps->resources);

		ACPI_FREE(buffer.pointer);
	}

	/* Evaluate "_PSx" to see if we can do explicit sets */
	pathname[2] = 'S';
	if (acpi_has_method(device->handle, pathname))
		ps->flags.explicit_set = 1;

	/* State is valid if there are means to put the device into it. */
	if (!list_empty(&ps->resources) || ps->flags.explicit_set)
		ps->flags.valid = 1;

	ps->power = -1;		/* Unknown - driver assigned */
	ps->latency = -1;	/* Unknown - driver assigned */
}

static void acpi_bus_get_power_flags(struct acpi_device *device)
{
	u32 i;

	/* Presence of _PS0|_PR0 indicates 'power manageable' */
	if (!acpi_has_method(device->handle, "_PS0") &&
	    !acpi_has_method(device->handle, "_PR0"))
		return;

	device->flags.power_manageable = 1;

	/*
	 * Power Management Flags
	 */
	status = acpi_get_handle(device->handle, "_PSC", &handle);
	if (ACPI_SUCCESS(status))
		device->power.flags.explicit_get = 1;
	status = acpi_get_handle(device->handle, "_IRC", &handle);
	if (ACPI_SUCCESS(status))
		device->power.flags.inrush_current = 1;

	/*
	 * Enumerate supported power management states
	 */
	for (i = ACPI_STATE_D0; i <= ACPI_STATE_D3; i++) {
		struct acpi_device_power_state *ps = &device->power.states[i];
		char object_name[5] = { '_', 'P', 'R', '0' + i, '\0' };

		/* Evaluate "_PRx" to se if power resources are referenced */
		acpi_evaluate_reference(device->handle, object_name, NULL,
					&ps->resources);
		if (ps->resources.count) {
			device->power.flags.power_resources = 1;
			ps->flags.valid = 1;
		}

		/* Evaluate "_PSx" to see if we can do explicit sets */
		object_name[2] = 'S';
		status = acpi_get_handle(device->handle, object_name, &handle);
		if (ACPI_SUCCESS(status)) {
			ps->flags.explicit_set = 1;
			ps->flags.valid = 1;
		}

		/* State is valid if we have some power control */
		if (ps->resources.count || ps->flags.explicit_set)
			ps->flags.valid = 1;

		ps->power = -1;	/* Unknown - driver assigned */
		ps->latency = -1;	/* Unknown - driver assigned */
	}

	/* Set defaults for D0 and D3 states (always valid) */
	device->power.states[ACPI_STATE_D0].flags.valid = 1;
	device->power.states[ACPI_STATE_D0].power = 100;
	device->power.states[ACPI_STATE_D3].flags.valid = 1;
	device->power.states[ACPI_STATE_D3].power = 0;

	/* TBD: System wake support and resource requirements. */

	device->power.state = ACPI_STATE_UNKNOWN;

	return 0;
}

static int acpi_bus_get_flags(struct acpi_device *device)
{
	acpi_status status = AE_OK;
	acpi_handle temp = NULL;


	/* Presence of _STA indicates 'dynamic_status' */
	status = acpi_get_handle(device->handle, "_STA", &temp);
	if (ACPI_SUCCESS(status))
		device->flags.dynamic_status = 1;

	/* Presence of _CID indicates 'compatible_ids' */
	status = acpi_get_handle(device->handle, "_CID", &temp);
	if (ACPI_SUCCESS(status))
		device->flags.compatible_ids = 1;

	/* Presence of _RMV indicates 'removable' */
	status = acpi_get_handle(device->handle, "_RMV", &temp);
	if (ACPI_SUCCESS(status))
		device->flags.removable = 1;

	/* Presence of _EJD|_EJ0 indicates 'ejectable' */
	status = acpi_get_handle(device->handle, "_EJD", &temp);
	if (ACPI_SUCCESS(status))
		device->flags.ejectable = 1;
	else {
		status = acpi_get_handle(device->handle, "_EJ0", &temp);
		if (ACPI_SUCCESS(status))
			device->flags.ejectable = 1;
	}

	/* Presence of _LCK indicates 'lockable' */
	status = acpi_get_handle(device->handle, "_LCK", &temp);
	if (ACPI_SUCCESS(status))
		device->flags.lockable = 1;

	/* Presence of _PS0|_PR0 indicates 'power manageable' */
	status = acpi_get_handle(device->handle, "_PS0", &temp);
	if (ACPI_FAILURE(status))
		status = acpi_get_handle(device->handle, "_PR0", &temp);
	if (ACPI_SUCCESS(status))
		device->flags.power_manageable = 1;

	/* Presence of _PRW indicates wake capable */
	status = acpi_get_handle(device->handle, "_PRW", &temp);
	if (ACPI_SUCCESS(status))
		device->flags.wake_capable = 1;

	/* TBD: Performance management */

	return 0;
}

static void acpi_device_get_busid(struct acpi_device *device,
				  acpi_handle handle, int type)
	if (acpi_has_method(device->handle, "_PSC"))
		device->power.flags.explicit_get = 1;

	if (acpi_has_method(device->handle, "_IRC"))
		device->power.flags.inrush_current = 1;

	if (acpi_has_method(device->handle, "_DSW"))
		device->power.flags.dsw_present = 1;

	/*
	 * Enumerate supported power management states
	 */
	for (i = ACPI_STATE_D0; i <= ACPI_STATE_D3_HOT; i++)
		acpi_bus_init_power_state(device, i);

	INIT_LIST_HEAD(&device->power.states[ACPI_STATE_D3_COLD].resources);

	/* Set the defaults for D0 and D3hot (always supported). */
	device->power.states[ACPI_STATE_D0].flags.valid = 1;
	device->power.states[ACPI_STATE_D0].power = 100;
	device->power.states[ACPI_STATE_D3_HOT].flags.valid = 1;

	/*
	 * Use power resources only if the D0 list of them is populated, because
	 * some platforms may provide _PR3 only to indicate D3cold support and
	 * in those cases the power resources list returned by it may be bogus.
	 */
	if (!list_empty(&device->power.states[ACPI_STATE_D0].resources)) {
		device->power.flags.power_resources = 1;
		/*
		 * D3cold is supported if the D3hot list of power resources is
		 * not empty.
		 */
		if (!list_empty(&device->power.states[ACPI_STATE_D3_HOT].resources))
			device->power.states[ACPI_STATE_D3_COLD].flags.valid = 1;
	}

	if (acpi_bus_init_power(device))
		device->flags.power_manageable = 0;
}

static void acpi_bus_get_flags(struct acpi_device *device)
{
	/* Presence of _STA indicates 'dynamic_status' */
	if (acpi_has_method(device->handle, "_STA"))
		device->flags.dynamic_status = 1;

	/* Presence of _RMV indicates 'removable' */
	if (acpi_has_method(device->handle, "_RMV"))
		device->flags.removable = 1;

	/* Presence of _EJD|_EJ0 indicates 'ejectable' */
	if (acpi_has_method(device->handle, "_EJD") ||
	    acpi_has_method(device->handle, "_EJ0"))
		device->flags.ejectable = 1;
}

static void acpi_device_get_busid(struct acpi_device *device)
{
	char bus_id[5] = { '?', 0 };
	struct acpi_buffer buffer = { sizeof(bus_id), bus_id };
	int i = 0;

	/*
	 * Bus ID
	 * ------
	 * The device's Bus ID is simply the object name.
	 * TBD: Shouldn't this value be unique (within the ACPI namespace)?
	 */
	switch (type) {
	case ACPI_BUS_TYPE_SYSTEM:
		strcpy(device->pnp.bus_id, "ACPI");
		break;
	if (ACPI_IS_ROOT_DEVICE(device)) {
		strcpy(device->pnp.bus_id, "ACPI");
		return;
	}

	switch (device->device_type) {
	case ACPI_BUS_TYPE_POWER_BUTTON:
		strcpy(device->pnp.bus_id, "PWRF");
		break;
	case ACPI_BUS_TYPE_SLEEP_BUTTON:
		strcpy(device->pnp.bus_id, "SLPF");
		break;
	default:
		acpi_get_name(handle, ACPI_SINGLE_NAME, &buffer);
		acpi_get_name(device->handle, ACPI_SINGLE_NAME, &buffer);
		/* Clean up trailing underscores (if any) */
		for (i = 3; i > 1; i--) {
			if (bus_id[i] == '_')
				bus_id[i] = '\0';
			else
				break;
		}
		strcpy(device->pnp.bus_id, bus_id);
		break;
	}
}

static int
acpi_video_bus_match(struct acpi_device *device)
{
	acpi_handle h_dummy;

	if (!device)
		return -EINVAL;

	/* Since there is no HID, CID for ACPI Video drivers, we have
	 * to check well known required nodes for each feature we support.
	 */

	/* Does this device able to support video switching ? */
	if (ACPI_SUCCESS(acpi_get_handle(device->handle, "_DOD", &h_dummy)) &&
	    ACPI_SUCCESS(acpi_get_handle(device->handle, "_DOS", &h_dummy)))
		return 0;

	/* Does this device able to retrieve a video ROM ? */
	if (ACPI_SUCCESS(acpi_get_handle(device->handle, "_ROM", &h_dummy)))
		return 0;

	/* Does this device able to configure which video head to be POSTed ? */
	if (ACPI_SUCCESS(acpi_get_handle(device->handle, "_VPO", &h_dummy)) &&
	    ACPI_SUCCESS(acpi_get_handle(device->handle, "_GPD", &h_dummy)) &&
	    ACPI_SUCCESS(acpi_get_handle(device->handle, "_SPD", &h_dummy)))
		return 0;

	return -ENODEV;
}

/*
 * acpi_bay_match - see if a device is an ejectable driver bay
/*
 * acpi_ata_match - see if an acpi object is an ATA device
 *
 * If an acpi object has one of the ACPI ATA methods defined,
 * then we can safely call it an ATA device.
 */
bool acpi_ata_match(acpi_handle handle)
{
	return acpi_has_method(handle, "_GTF") ||
	       acpi_has_method(handle, "_GTM") ||
	       acpi_has_method(handle, "_STM") ||
	       acpi_has_method(handle, "_SDD");
}

/*
 * acpi_bay_match - see if an acpi object is an ejectable driver bay
 *
 * If an acpi object is ejectable and has one of the ACPI ATA methods defined,
 * then we can safely call it an ejectable drive bay
 */
static int acpi_bay_match(struct acpi_device *device){
	acpi_status status;
	acpi_handle handle;
	acpi_handle tmp;
	acpi_handle phandle;

	handle = device->handle;

	status = acpi_get_handle(handle, "_EJ0", &tmp);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	if ((ACPI_SUCCESS(acpi_get_handle(handle, "_GTF", &tmp))) ||
		(ACPI_SUCCESS(acpi_get_handle(handle, "_GTM", &tmp))) ||
		(ACPI_SUCCESS(acpi_get_handle(handle, "_STM", &tmp))) ||
		(ACPI_SUCCESS(acpi_get_handle(handle, "_SDD", &tmp))))
		return 0;

	if (acpi_get_parent(handle, &phandle))
		return -ENODEV;

        if ((ACPI_SUCCESS(acpi_get_handle(phandle, "_GTF", &tmp))) ||
                (ACPI_SUCCESS(acpi_get_handle(phandle, "_GTM", &tmp))) ||
                (ACPI_SUCCESS(acpi_get_handle(phandle, "_STM", &tmp))) ||
                (ACPI_SUCCESS(acpi_get_handle(phandle, "_SDD", &tmp))))
                return 0;

	return -ENODEV;
}

/*
 * acpi_dock_match - see if a device has a _DCK method
 */
static int acpi_dock_match(struct acpi_device *device)
{
	acpi_handle tmp;
	return acpi_get_handle(device->handle, "_DCK", &tmp);
}

static void acpi_device_set_id(struct acpi_device *device,
			       struct acpi_device *parent, acpi_handle handle,
			       int type)
{
	struct acpi_device_info *info;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	char *hid = NULL;
	char *uid = NULL;
	struct acpi_compatible_id_list *cid_list = NULL;
	const char *cid_add = NULL;
	acpi_status status;

	switch (type) {
	case ACPI_BUS_TYPE_DEVICE:
		status = acpi_get_object_info(handle, &buffer);
		if (ACPI_FAILURE(status)) {
			printk(KERN_ERR PREFIX "%s: Error reading device info\n", __func__);
			return;
		}

		info = buffer.pointer;
		if (info->valid & ACPI_VALID_HID)
			hid = info->hardware_id.value;
		if (info->valid & ACPI_VALID_UID)
			uid = info->unique_id.value;
		if (info->valid & ACPI_VALID_CID)
			cid_list = &info->compatibility_id;
		if (info->valid & ACPI_VALID_ADR) {
			device->pnp.bus_address = info->address;
			device->flags.bus_address = 1;
		}

		/* If we have a video/bay/dock device, add our selfdefined
		   HID to the CID list. Like that the video/bay/dock drivers
		   will get autoloaded and the device might still match
		   against another driver.
		*/
		if (ACPI_SUCCESS(acpi_video_bus_match(device)))
			cid_add = ACPI_VIDEO_HID;
		else if (ACPI_SUCCESS(acpi_bay_match(device)))
			cid_add = ACPI_BAY_HID;
		else if (ACPI_SUCCESS(acpi_dock_match(device)))
			cid_add = ACPI_DOCK_HID;

		break;
	case ACPI_BUS_TYPE_POWER:
		hid = ACPI_POWER_HID;
		break;
	case ACPI_BUS_TYPE_PROCESSOR:
		hid = ACPI_PROCESSOR_HID;
		break;
	case ACPI_BUS_TYPE_SYSTEM:
		hid = ACPI_SYSTEM_HID;
		break;
	case ACPI_BUS_TYPE_THERMAL:
		hid = ACPI_THERMAL_HID;
		break;
	case ACPI_BUS_TYPE_POWER_BUTTON:
		hid = ACPI_BUTTON_HID_POWERF;
		break;
	case ACPI_BUS_TYPE_SLEEP_BUTTON:
		hid = ACPI_BUTTON_HID_SLEEPF;
		break;
	}

	/* 
	 * \_SB
	 * ----
	 * Fix for the system root bus device -- the only root-level device.
	 */
	if (((acpi_handle)parent == ACPI_ROOT_OBJECT) && (type == ACPI_BUS_TYPE_DEVICE)) {
		hid = ACPI_BUS_HID;
		strcpy(device->pnp.device_name, ACPI_BUS_DEVICE_NAME);
		strcpy(device->pnp.device_class, ACPI_BUS_CLASS);
	}

	if (hid) {
		strcpy(device->pnp.hardware_id, hid);
		device->flags.hardware_id = 1;
	}
	if (uid) {
		strcpy(device->pnp.unique_id, uid);
		device->flags.unique_id = 1;
	}
	if (cid_list || cid_add) {
		struct  acpi_compatible_id_list *list;
		int size = 0;
		int count = 0;

		if (cid_list) {
			size = cid_list->size;
		} else if (cid_add) {
			size = sizeof(struct acpi_compatible_id_list);
			cid_list = ACPI_ALLOCATE_ZEROED((acpi_size) size);
			if (!cid_list) {
				printk(KERN_ERR "Memory allocation error\n");
				kfree(buffer.pointer);
				return;
			} else {
				cid_list->count = 0;
				cid_list->size = size;
			}
		}
		if (cid_add)
			size += sizeof(struct acpi_compatible_id);
		list = kmalloc(size, GFP_KERNEL);

		if (list) {
			if (cid_list) {
				memcpy(list, cid_list, cid_list->size);
				count = cid_list->count;
			}
			if (cid_add) {
				strncpy(list->id[count].value, cid_add,
					ACPI_MAX_CID_LENGTH);
				count++;
				device->flags.compatible_ids = 1;
			}
			list->size = size;
			list->count = count;
			device->pnp.cid_list = list;
		} else
			printk(KERN_ERR PREFIX "Memory allocation error\n");
	}

	kfree(buffer.pointer);
}

static int acpi_device_set_context(struct acpi_device *device, int type)
{
	acpi_status status = AE_OK;
	int result = 0;
	/*
	 * Context
	 * -------
	 * Attach this 'struct acpi_device' to the ACPI object.  This makes
	 * resolutions from handle->device very efficient.  Note that we need
	 * to be careful with fixed-feature devices as they all attach to the
	 * root object.
	 */
	if (type != ACPI_BUS_TYPE_POWER_BUTTON &&
	    type != ACPI_BUS_TYPE_SLEEP_BUTTON) {
		status = acpi_attach_data(device->handle,
					  acpi_bus_data_handler, device);

		if (ACPI_FAILURE(status)) {
			printk(KERN_ERR PREFIX "Error attaching device data\n");
			result = -ENODEV;
		}
	}
	return result;
}

static int acpi_bus_remove(struct acpi_device *dev, int rmdevice)
{
	if (!dev)
		return -EINVAL;

	dev->removal_type = ACPI_BUS_REMOVAL_EJECT;
	device_release_driver(&dev->dev);

	if (!rmdevice)
		return 0;

	/*
	 * unbind _ADR-Based Devices when hot removal
	 */
	if (dev->flags.bus_address) {
		if ((dev->parent) && (dev->parent->ops.unbind))
			dev->parent->ops.unbind(dev);
	}
	acpi_device_unregister(dev, ACPI_BUS_REMOVAL_EJECT);

	return 0;
}

static int
acpi_is_child_device(struct acpi_device *device,
			int (*matcher)(struct acpi_device *))
{
	int result = -ENODEV;

	do {
		if (ACPI_SUCCESS(matcher(device)))
			return AE_OK;
	} while ((device = device->parent));

	return result;
}

static int
acpi_add_single_object(struct acpi_device **child,
		       struct acpi_device *parent, acpi_handle handle, int type,
			struct acpi_bus_ops *ops)
{
	int result = 0;
	struct acpi_device *device = NULL;


	if (!child)
		return -EINVAL;
bool acpi_bay_match(acpi_handle handle)
{
	acpi_handle phandle;

	if (!acpi_has_method(handle, "_EJ0"))
		return false;
	if (acpi_ata_match(handle))
		return true;
	if (ACPI_FAILURE(acpi_get_parent(handle, &phandle)))
		return false;

	return acpi_ata_match(phandle);
}

bool acpi_device_is_battery(struct acpi_device *adev)
{
	struct acpi_hardware_id *hwid;

	list_for_each_entry(hwid, &adev->pnp.ids, list)
		if (!strcmp("PNP0C0A", hwid->id))
			return true;

	return false;
}

static bool is_ejectable_bay(struct acpi_device *adev)
{
	acpi_handle handle = adev->handle;

	if (acpi_has_method(handle, "_EJ0") && acpi_device_is_battery(adev))
		return true;

	return acpi_bay_match(handle);
}

/*
 * acpi_dock_match - see if an acpi object has a _DCK method
 */
bool acpi_dock_match(acpi_handle handle)
{
	return acpi_has_method(handle, "_DCK");
}

static acpi_status
acpi_backlight_cap_match(acpi_handle handle, u32 level, void *context,
			  void **return_value)
{
	long *cap = context;

	if (acpi_has_method(handle, "_BCM") &&
	    acpi_has_method(handle, "_BCL")) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Found generic backlight "
				  "support\n"));
		*cap |= ACPI_VIDEO_BACKLIGHT;
		if (!acpi_has_method(handle, "_BQC"))
			printk(KERN_WARNING FW_BUG PREFIX "No _BQC method, "
				"cannot determine initial brightness\n");
		/* We have backlight support, no need to scan further */
		return AE_CTRL_TERMINATE;
	}
	return 0;
}

/* Returns true if the ACPI object is a video device which can be
 * handled by video.ko.
 * The device will get a Linux specific CID added in scan.c to
 * identify the device as an ACPI graphics device
 * Be aware that the graphics device may not be physically present
 * Use acpi_video_get_capabilities() to detect general ACPI video
 * capabilities of present cards
 */
long acpi_is_video_device(acpi_handle handle)
{
	long video_caps = 0;

	/* Is this device able to support video switching ? */
	if (acpi_has_method(handle, "_DOD") || acpi_has_method(handle, "_DOS"))
		video_caps |= ACPI_VIDEO_OUTPUT_SWITCHING;

	/* Is this device able to retrieve a video ROM ? */
	if (acpi_has_method(handle, "_ROM"))
		video_caps |= ACPI_VIDEO_ROM_AVAILABLE;

	/* Is this device able to configure which video head to be POSTed ? */
	if (acpi_has_method(handle, "_VPO") &&
	    acpi_has_method(handle, "_GPD") &&
	    acpi_has_method(handle, "_SPD"))
		video_caps |= ACPI_VIDEO_DEVICE_POSTING;

	/* Only check for backlight functionality if one of the above hit. */
	if (video_caps)
		acpi_walk_namespace(ACPI_TYPE_DEVICE, handle,
				    ACPI_UINT32_MAX, acpi_backlight_cap_match, NULL,
				    &video_caps, NULL);

	return video_caps;
}
EXPORT_SYMBOL(acpi_is_video_device);

const char *acpi_device_hid(struct acpi_device *device)
{
	struct acpi_hardware_id *hid;

	if (list_empty(&device->pnp.ids))
		return dummy_hid;

	hid = list_first_entry(&device->pnp.ids, struct acpi_hardware_id, list);
	return hid->id;
}
EXPORT_SYMBOL(acpi_device_hid);

static void acpi_add_id(struct acpi_device_pnp *pnp, const char *dev_id)
{
	struct acpi_hardware_id *id;

	id = kmalloc(sizeof(*id), GFP_KERNEL);
	if (!id)
		return;

	id->id = kstrdup_const(dev_id, GFP_KERNEL);
	if (!id->id) {
		kfree(id);
		return;
	}

	list_add_tail(&id->list, &pnp->ids);
	pnp->type.hardware_id = 1;
}

/*
 * Old IBM workstations have a DSDT bug wherein the SMBus object
 * lacks the SMBUS01 HID and the methods do not have the necessary "_"
 * prefix.  Work around this.
 */
static bool acpi_ibm_smbus_match(acpi_handle handle)
{
	char node_name[ACPI_PATH_SEGMENT_LENGTH];
	struct acpi_buffer path = { sizeof(node_name), node_name };

	if (!dmi_name_in_vendors("IBM"))
		return false;

	/* Look for SMBS object */
	if (ACPI_FAILURE(acpi_get_name(handle, ACPI_SINGLE_NAME, &path)) ||
	    strcmp("SMBS", path.pointer))
		return false;

	/* Does it have the necessary (but misnamed) methods? */
	if (acpi_has_method(handle, "SBI") &&
	    acpi_has_method(handle, "SBR") &&
	    acpi_has_method(handle, "SBW"))
		return true;

	return false;
}

static bool acpi_object_is_system_bus(acpi_handle handle)
{
	acpi_handle tmp;

	if (ACPI_SUCCESS(acpi_get_handle(NULL, "\\_SB", &tmp)) &&
	    tmp == handle)
		return true;
	if (ACPI_SUCCESS(acpi_get_handle(NULL, "\\_TZ", &tmp)) &&
	    tmp == handle)
		return true;

	return false;
}

static void acpi_set_pnp_ids(acpi_handle handle, struct acpi_device_pnp *pnp,
				int device_type)
{
	acpi_status status;
	struct acpi_device_info *info;
	struct acpi_pnp_device_id_list *cid_list;
	int i;

	switch (device_type) {
	case ACPI_BUS_TYPE_DEVICE:
		if (handle == ACPI_ROOT_OBJECT) {
			acpi_add_id(pnp, ACPI_SYSTEM_HID);
			break;
		}

		status = acpi_get_object_info(handle, &info);
		if (ACPI_FAILURE(status)) {
			pr_err(PREFIX "%s: Error reading device info\n",
					__func__);
			return;
		}

		if (info->valid & ACPI_VALID_HID) {
			acpi_add_id(pnp, info->hardware_id.string);
			pnp->type.platform_id = 1;
		}
		if (info->valid & ACPI_VALID_CID) {
			cid_list = &info->compatible_id_list;
			for (i = 0; i < cid_list->count; i++)
				acpi_add_id(pnp, cid_list->ids[i].string);
		}
		if (info->valid & ACPI_VALID_ADR) {
			pnp->bus_address = info->address;
			pnp->type.bus_address = 1;
		}
		if (info->valid & ACPI_VALID_UID)
			pnp->unique_id = kstrdup(info->unique_id.string,
							GFP_KERNEL);
		if (info->valid & ACPI_VALID_CLS)
			acpi_add_id(pnp, info->class_code.string);

		kfree(info);

		/*
		 * Some devices don't reliably have _HIDs & _CIDs, so add
		 * synthetic HIDs to make sure drivers can find them.
		 */
		if (acpi_is_video_device(handle))
			acpi_add_id(pnp, ACPI_VIDEO_HID);
		else if (acpi_bay_match(handle))
			acpi_add_id(pnp, ACPI_BAY_HID);
		else if (acpi_dock_match(handle))
			acpi_add_id(pnp, ACPI_DOCK_HID);
		else if (acpi_ibm_smbus_match(handle))
			acpi_add_id(pnp, ACPI_SMBUS_IBM_HID);
		else if (list_empty(&pnp->ids) &&
			 acpi_object_is_system_bus(handle)) {
			/* \_SB, \_TZ, LNXSYBUS */
			acpi_add_id(pnp, ACPI_BUS_HID);
			strcpy(pnp->device_name, ACPI_BUS_DEVICE_NAME);
			strcpy(pnp->device_class, ACPI_BUS_CLASS);
		}

		break;
	case ACPI_BUS_TYPE_POWER:
		acpi_add_id(pnp, ACPI_POWER_HID);
		break;
	case ACPI_BUS_TYPE_PROCESSOR:
		acpi_add_id(pnp, ACPI_PROCESSOR_OBJECT_HID);
		break;
	case ACPI_BUS_TYPE_THERMAL:
		acpi_add_id(pnp, ACPI_THERMAL_HID);
		break;
	case ACPI_BUS_TYPE_POWER_BUTTON:
		acpi_add_id(pnp, ACPI_BUTTON_HID_POWERF);
		break;
	case ACPI_BUS_TYPE_SLEEP_BUTTON:
		acpi_add_id(pnp, ACPI_BUTTON_HID_SLEEPF);
		break;
	}
}

void acpi_free_pnp_ids(struct acpi_device_pnp *pnp)
{
	struct acpi_hardware_id *id, *tmp;

	list_for_each_entry_safe(id, tmp, &pnp->ids, list) {
		kfree_const(id->id);
		kfree(id);
	}
	kfree(pnp->unique_id);
}

/**
 * acpi_dma_supported - Check DMA support for the specified device.
 * @adev: The pointer to acpi device
 *
 * Return false if DMA is not supported. Otherwise, return true
 */
bool acpi_dma_supported(struct acpi_device *adev)
{
	if (!adev)
		return false;

	if (adev->flags.cca_seen)
		return true;

	/*
	* Per ACPI 6.0 sec 6.2.17, assume devices can do cache-coherent
	* DMA on "Intel platforms".  Presumably that includes all x86 and
	* ia64, and other arches will set CONFIG_ACPI_CCA_REQUIRED=y.
	*/
	if (!IS_ENABLED(CONFIG_ACPI_CCA_REQUIRED))
		return true;

	return false;
}

/**
 * acpi_get_dma_attr - Check the supported DMA attr for the specified device.
 * @adev: The pointer to acpi device
 *
 * Return enum dev_dma_attr.
 */
enum dev_dma_attr acpi_get_dma_attr(struct acpi_device *adev)
{
	if (!acpi_dma_supported(adev))
		return DEV_DMA_NOT_SUPPORTED;

	if (adev->flags.coherent_dma)
		return DEV_DMA_COHERENT;
	else
		return DEV_DMA_NON_COHERENT;
}

static void acpi_init_coherency(struct acpi_device *adev)
{
	unsigned long long cca = 0;
	acpi_status status;
	struct acpi_device *parent = adev->parent;

	if (parent && parent->flags.cca_seen) {
		/*
		 * From ACPI spec, OSPM will ignore _CCA if an ancestor
		 * already saw one.
		 */
		adev->flags.cca_seen = 1;
		cca = parent->flags.coherent_dma;
	} else {
		status = acpi_evaluate_integer(adev->handle, "_CCA",
					       NULL, &cca);
		if (ACPI_SUCCESS(status))
			adev->flags.cca_seen = 1;
		else if (!IS_ENABLED(CONFIG_ACPI_CCA_REQUIRED))
			/*
			 * If architecture does not specify that _CCA is
			 * required for DMA-able devices (e.g. x86),
			 * we default to _CCA=1.
			 */
			cca = 1;
		else
			acpi_handle_debug(adev->handle,
					  "ACPI device is missing _CCA.\n");
	}

	adev->flags.coherent_dma = cca;
}

void acpi_init_device_object(struct acpi_device *device, acpi_handle handle,
			     int type, unsigned long long sta)
{
	INIT_LIST_HEAD(&device->pnp.ids);
	device->device_type = type;
	device->handle = handle;
	device->parent = acpi_bus_get_parent(handle);
	device->fwnode.type = FWNODE_ACPI;
	acpi_set_device_status(device, sta);
	acpi_device_get_busid(device);
	acpi_set_pnp_ids(handle, &device->pnp, type);
	acpi_init_properties(device);
	acpi_bus_get_flags(device);
	device->flags.match_driver = false;
	device->flags.initialized = true;
	device->flags.visited = false;
	device_initialize(&device->dev);
	dev_set_uevent_suppress(&device->dev, true);
	acpi_init_coherency(device);
}

void acpi_device_add_finalize(struct acpi_device *device)
{
	dev_set_uevent_suppress(&device->dev, false);
	kobject_uevent(&device->dev.kobj, KOBJ_ADD);
}

static int acpi_add_single_object(struct acpi_device **child,
				  acpi_handle handle, int type,
				  unsigned long long sta)
{
	int result;
	struct acpi_device *device;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };

	device = kzalloc(sizeof(struct acpi_device), GFP_KERNEL);
	if (!device) {
		printk(KERN_ERR PREFIX "Memory allocation error\n");
		return -ENOMEM;
	}

	device->handle = handle;
	device->parent = parent;
	device->bus_ops = *ops; /* workround for not call .start */


	acpi_device_get_busid(device, handle, type);

	/*
	 * Flags
	 * -----
	 * Get prior to calling acpi_bus_get_status() so we know whether
	 * or not _STA is present.  Note that we only look for object
	 * handles -- cannot evaluate objects until we know the device is
	 * present and properly initialized.
	 */
	result = acpi_bus_get_flags(device);
	if (result)
		goto end;

	/*
	 * Status
	 * ------
	 * See if the device is present.  We always assume that non-Device
	 * and non-Processor objects (e.g. thermal zones, power resources,
	 * etc.) are present, functioning, etc. (at least when parent object
	 * is present).  Note that _STA has a different meaning for some
	 * objects (e.g. power resources) so we need to be careful how we use
	 * it.
	 */
	switch (type) {
	case ACPI_BUS_TYPE_PROCESSOR:
	case ACPI_BUS_TYPE_DEVICE:
		result = acpi_bus_get_status(device);
		if (ACPI_FAILURE(result)) {
			result = -ENODEV;
			goto end;
		}
		if (!device->status.present) {
			/* Bay and dock should be handled even if absent */
			if (!ACPI_SUCCESS(
			     acpi_is_child_device(device, acpi_bay_match)) &&
			    !ACPI_SUCCESS(
			     acpi_is_child_device(device, acpi_dock_match))) {
					result = -ENODEV;
					goto end;
			}
		}
		break;
	default:
		STRUCT_TO_INT(device->status) =
		    ACPI_STA_DEVICE_PRESENT | ACPI_STA_DEVICE_ENABLED |
		    ACPI_STA_DEVICE_UI      | ACPI_STA_DEVICE_FUNCTIONING;
		break;
	}

	/*
	 * Initialize Device
	 * -----------------
	 * TBD: Synch with Core's enumeration/initialization process.
	 */

	/*
	 * Hardware ID, Unique ID, & Bus Address
	 * -------------------------------------
	 */
	acpi_device_set_id(device, parent, handle, type);

	/*
	 * Power Management
	 * ----------------
	 */
	if (device->flags.power_manageable) {
		result = acpi_bus_get_power_flags(device);
		if (result)
			goto end;
	}

	/*
	 * Wakeup device management
	 *-----------------------
	 */
	if (device->flags.wake_capable) {
		result = acpi_bus_get_wakeup_device_flags(device);
		if (result)
			goto end;
	}

	/*
	 * Performance Management
	 * ----------------------
	 */
	if (device->flags.performance_manageable) {
		result = acpi_bus_get_perf_flags(device);
		if (result)
			goto end;
	}

	if ((result = acpi_device_set_context(device, type)))
		goto end;

	result = acpi_device_register(device, parent);

	/*
	 * Bind _ADR-Based Devices when hot add
	 */
	if (device->flags.bus_address) {
		if (device->parent && device->parent->ops.bind)
			device->parent->ops.bind(device);
	}

      end:
	if (!result)
		*child = device;
	else {
		kfree(device->pnp.cid_list);
		kfree(device);
	}

	return result;
}

static int acpi_bus_scan(struct acpi_device *start, struct acpi_bus_ops *ops)
{
	acpi_status status = AE_OK;
	struct acpi_device *parent = NULL;
	struct acpi_device *child = NULL;
	acpi_handle phandle = NULL;
	acpi_handle chandle = NULL;
	acpi_object_type type = 0;
	u32 level = 1;


	if (!start)
		return -EINVAL;

	parent = start;
	phandle = start->handle;

	/*
	 * Parse through the ACPI namespace, identify all 'devices', and
	 * create a new 'struct acpi_device' for each.
	 */
	while ((level > 0) && parent) {

		status = acpi_get_next_object(ACPI_TYPE_ANY, phandle,
					      chandle, &chandle);

		/*
		 * If this scope is exhausted then move our way back up.
		 */
		if (ACPI_FAILURE(status)) {
			level--;
			chandle = phandle;
			acpi_get_parent(phandle, &phandle);
			if (parent->parent)
				parent = parent->parent;
			continue;
		}

		status = acpi_get_type(chandle, &type);
		if (ACPI_FAILURE(status))
			continue;

		/*
		 * If this is a scope object then parse it (depth-first).
		 */
		if (type == ACPI_TYPE_LOCAL_SCOPE) {
			level++;
			phandle = chandle;
			chandle = NULL;
			continue;
		}

		/*
		 * We're only interested in objects that we consider 'devices'.
		 */
		switch (type) {
		case ACPI_TYPE_DEVICE:
			type = ACPI_BUS_TYPE_DEVICE;
			break;
		case ACPI_TYPE_PROCESSOR:
			type = ACPI_BUS_TYPE_PROCESSOR;
			break;
		case ACPI_TYPE_THERMAL:
			type = ACPI_BUS_TYPE_THERMAL;
			break;
		case ACPI_TYPE_POWER:
			type = ACPI_BUS_TYPE_POWER;
			break;
		default:
			continue;
		}

		if (ops->acpi_op_add)
			status = acpi_add_single_object(&child, parent,
				chandle, type, ops);
		else
			status = acpi_bus_get_device(chandle, &child);

		if (ACPI_FAILURE(status))
			continue;

		if (ops->acpi_op_start && !(ops->acpi_op_add)) {
			status = acpi_start_single_object(child);
			if (ACPI_FAILURE(status))
				continue;
		}

		/*
		 * If the device is present, enabled, and functioning then
		 * parse its scope (depth-first).  Note that we need to
		 * represent absent devices to facilitate PnP notifications
		 * -- but only the subtree head (not all of its children,
		 * which will be enumerated when the parent is inserted).
		 *
		 * TBD: Need notifications and other detection mechanisms
		 *      in place before we can fully implement this.
		 */
		if (child->status.present) {
			status = acpi_get_next_object(ACPI_TYPE_ANY, chandle,
						      NULL, NULL);
			if (ACPI_SUCCESS(status)) {
				level++;
				phandle = chandle;
				chandle = NULL;
				parent = child;
			}
		}
	acpi_init_device_object(device, handle, type, sta);
	acpi_bus_get_power_flags(device);
	acpi_bus_get_wakeup_device_flags(device);

	result = acpi_device_add(device, acpi_device_release);
	if (result) {
		acpi_device_release(&device->dev);
		return result;
	}

	acpi_power_add_remove_device(device, true);
	acpi_device_add_finalize(device);
	acpi_get_name(handle, ACPI_FULL_PATHNAME, &buffer);
	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Added %s [%s] parent %s\n",
		dev_name(&device->dev), (char *) buffer.pointer,
		device->parent ? dev_name(&device->parent->dev) : "(null)"));
	kfree(buffer.pointer);
	*child = device;
	return 0;
}

static int acpi_bus_type_and_status(acpi_handle handle, int *type,
				    unsigned long long *sta)
{
	acpi_status status;
	acpi_object_type acpi_type;

	status = acpi_get_type(handle, &acpi_type);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	switch (acpi_type) {
	case ACPI_TYPE_ANY:		/* for ACPI_ROOT_OBJECT */
	case ACPI_TYPE_DEVICE:
		*type = ACPI_BUS_TYPE_DEVICE;
		status = acpi_bus_get_status_handle(handle, sta);
		if (ACPI_FAILURE(status))
			return -ENODEV;
		break;
	case ACPI_TYPE_PROCESSOR:
		*type = ACPI_BUS_TYPE_PROCESSOR;
		status = acpi_bus_get_status_handle(handle, sta);
		if (ACPI_FAILURE(status))
			return -ENODEV;
		break;
	case ACPI_TYPE_THERMAL:
		*type = ACPI_BUS_TYPE_THERMAL;
		*sta = ACPI_STA_DEFAULT;
		break;
	case ACPI_TYPE_POWER:
		*type = ACPI_BUS_TYPE_POWER;
		*sta = ACPI_STA_DEFAULT;
		break;
	default:
		return -ENODEV;
	}

	return 0;
}

int
acpi_bus_add(struct acpi_device **child,
	     struct acpi_device *parent, acpi_handle handle, int type)
{
	int result;
	struct acpi_bus_ops ops;

	memset(&ops, 0, sizeof(ops));
	ops.acpi_op_add = 1;

	result = acpi_add_single_object(child, parent, handle, type, &ops);
	if (!result)
		result = acpi_bus_scan(*child, &ops);

	return result;
}

EXPORT_SYMBOL(acpi_bus_add);

int acpi_bus_start(struct acpi_device *device)
{
	int result;
	struct acpi_bus_ops ops;


	if (!device)
		return -EINVAL;

	result = acpi_start_single_object(device);
	if (!result) {
		memset(&ops, 0, sizeof(ops));
		ops.acpi_op_start = 1;
		result = acpi_bus_scan(device, &ops);
	}
	return result;
}

EXPORT_SYMBOL(acpi_bus_start);

int acpi_bus_trim(struct acpi_device *start, int rmdevice)
{
	acpi_status status;
	struct acpi_device *parent, *child;
	acpi_handle phandle, chandle;
	acpi_object_type type;
	u32 level = 1;
	int err = 0;

	parent = start;
	phandle = start->handle;
	child = chandle = NULL;

	while ((level > 0) && parent && (!err)) {
		status = acpi_get_next_object(ACPI_TYPE_ANY, phandle,
					      chandle, &chandle);

		/*
		 * If this scope is exhausted then move our way back up.
		 */
		if (ACPI_FAILURE(status)) {
			level--;
			chandle = phandle;
			acpi_get_parent(phandle, &phandle);
			child = parent;
			parent = parent->parent;

			if (level == 0)
				err = acpi_bus_remove(child, rmdevice);
			else
				err = acpi_bus_remove(child, 1);

			continue;
		}

		status = acpi_get_type(chandle, &type);
		if (ACPI_FAILURE(status)) {
			continue;
		}
		/*
		 * If there is a device corresponding to chandle then
		 * parse it (depth-first).
		 */
		if (acpi_bus_get_device(chandle, &child) == 0) {
			level++;
			phandle = chandle;
			chandle = NULL;
			parent = child;
		}
		continue;
	}
	return err;
}
EXPORT_SYMBOL_GPL(acpi_bus_trim);


static int acpi_bus_scan_fixed(struct acpi_device *root)
{
	int result = 0;
	struct acpi_device *device = NULL;
	struct acpi_bus_ops ops;

	if (!root)
		return -ENODEV;

	memset(&ops, 0, sizeof(ops));
	ops.acpi_op_add = 1;
	ops.acpi_op_start = 1;
bool acpi_device_is_present(struct acpi_device *adev)
{
	if (adev->status.present || adev->status.functional)
		return true;

	adev->flags.initialized = false;
	return false;
}

static bool acpi_scan_handler_matching(struct acpi_scan_handler *handler,
				       const char *idstr,
				       const struct acpi_device_id **matchid)
{
	const struct acpi_device_id *devid;

	if (handler->match)
		return handler->match(idstr, matchid);

	for (devid = handler->ids; devid->id[0]; devid++)
		if (!strcmp((char *)devid->id, idstr)) {
			if (matchid)
				*matchid = devid;

			return true;
		}

	return false;
}

static struct acpi_scan_handler *acpi_scan_match_handler(const char *idstr,
					const struct acpi_device_id **matchid)
{
	struct acpi_scan_handler *handler;

	list_for_each_entry(handler, &acpi_scan_handlers_list, list_node)
		if (acpi_scan_handler_matching(handler, idstr, matchid))
			return handler;

	return NULL;
}

void acpi_scan_hotplug_enabled(struct acpi_hotplug_profile *hotplug, bool val)
{
	if (!!hotplug->enabled == !!val)
		return;

	mutex_lock(&acpi_scan_lock);

	hotplug->enabled = val;

	mutex_unlock(&acpi_scan_lock);
}

static void acpi_scan_init_hotplug(struct acpi_device *adev)
{
	struct acpi_hardware_id *hwid;

	if (acpi_dock_match(adev->handle) || is_ejectable_bay(adev)) {
		acpi_dock_add(adev);
		return;
	}
	list_for_each_entry(hwid, &adev->pnp.ids, list) {
		struct acpi_scan_handler *handler;

		handler = acpi_scan_match_handler(hwid->id, NULL);
		if (handler) {
			adev->flags.hotplug_notify = true;
			break;
		}
	}
}

static void acpi_device_dep_initialize(struct acpi_device *adev)
{
	struct acpi_dep_data *dep;
	struct acpi_handle_list dep_devices;
	acpi_status status;
	int i;

	if (!acpi_has_method(adev->handle, "_DEP"))
		return;

	status = acpi_evaluate_reference(adev->handle, "_DEP", NULL,
					&dep_devices);
	if (ACPI_FAILURE(status)) {
		dev_dbg(&adev->dev, "Failed to evaluate _DEP.\n");
		return;
	}

	for (i = 0; i < dep_devices.count; i++) {
		struct acpi_device_info *info;
		int skip;

		status = acpi_get_object_info(dep_devices.handles[i], &info);
		if (ACPI_FAILURE(status)) {
			dev_dbg(&adev->dev, "Error reading _DEP device info\n");
			continue;
		}

		/*
		 * Skip the dependency of Windows System Power
		 * Management Controller
		 */
		skip = info->valid & ACPI_VALID_HID &&
			!strcmp(info->hardware_id.string, "INT3396");

		kfree(info);

		if (skip)
			continue;

		dep = kzalloc(sizeof(struct acpi_dep_data), GFP_KERNEL);
		if (!dep)
			return;

		dep->master = dep_devices.handles[i];
		dep->slave  = adev->handle;
		adev->dep_unmet++;

		mutex_lock(&acpi_dep_list_lock);
		list_add_tail(&dep->node , &acpi_dep_list);
		mutex_unlock(&acpi_dep_list_lock);
	}
}

static acpi_status acpi_bus_check_add(acpi_handle handle, u32 lvl_not_used,
				      void *not_used, void **return_value)
{
	struct acpi_device *device = NULL;
	int type;
	unsigned long long sta;
	int result;

	acpi_bus_get_device(handle, &device);
	if (device)
		goto out;

	result = acpi_bus_type_and_status(handle, &type, &sta);
	if (result)
		return AE_OK;

	if (type == ACPI_BUS_TYPE_POWER) {
		acpi_add_power_resource(handle);
		return AE_OK;
	}

	acpi_add_single_object(&device, handle, type, sta);
	if (!device)
		return AE_CTRL_DEPTH;

	acpi_scan_init_hotplug(device);
	acpi_device_dep_initialize(device);

 out:
	if (!*return_value)
		*return_value = device;

	return AE_OK;
}

static int acpi_check_spi_i2c_slave(struct acpi_resource *ares, void *data)
{
	bool *is_spi_i2c_slave_p = data;

	if (ares->type != ACPI_RESOURCE_TYPE_SERIAL_BUS)
		return 1;

	/*
	 * devices that are connected to UART still need to be enumerated to
	 * platform bus
	 */
	if (ares->data.common_serial_bus.type != ACPI_RESOURCE_SERIAL_TYPE_UART)
		*is_spi_i2c_slave_p = true;

	 /* no need to do more checking */
	return -1;
}

static void acpi_default_enumeration(struct acpi_device *device)
{
	struct list_head resource_list;
	bool is_spi_i2c_slave = false;

	/*
	 * Do not enemerate SPI/I2C slaves as they will be enuerated by their
	 * respective parents.
	 */
	INIT_LIST_HEAD(&resource_list);
	acpi_dev_get_resources(device, &resource_list, acpi_check_spi_i2c_slave,
			       &is_spi_i2c_slave);
	acpi_dev_free_resource_list(&resource_list);
	if (!is_spi_i2c_slave)
		acpi_create_platform_device(device);
}

static const struct acpi_device_id generic_device_ids[] = {
	{ACPI_DT_NAMESPACE_HID, },
	{"", },
};

static int acpi_generic_device_attach(struct acpi_device *adev,
				      const struct acpi_device_id *not_used)
{
	/*
	 * Since ACPI_DT_NAMESPACE_HID is the only ID handled here, the test
	 * below can be unconditional.
	 */
	if (adev->data.of_compatible)
		acpi_default_enumeration(adev);

	return 1;
}

static struct acpi_scan_handler generic_device_handler = {
	.ids = generic_device_ids,
	.attach = acpi_generic_device_attach,
};

static int acpi_scan_attach_handler(struct acpi_device *device)
{
	struct acpi_hardware_id *hwid;
	int ret = 0;

	list_for_each_entry(hwid, &device->pnp.ids, list) {
		const struct acpi_device_id *devid;
		struct acpi_scan_handler *handler;

		handler = acpi_scan_match_handler(hwid->id, &devid);
		if (handler) {
			if (!handler->attach) {
				device->pnp.type.platform_id = 0;
				continue;
			}
			device->handler = handler;
			ret = handler->attach(device, devid);
			if (ret > 0)
				break;

			device->handler = NULL;
			if (ret < 0)
				break;
		}
	}

	return ret;
}

static void acpi_bus_attach(struct acpi_device *device)
{
	struct acpi_device *child;
	acpi_handle ejd;
	int ret;

	if (ACPI_SUCCESS(acpi_bus_get_ejd(device->handle, &ejd)))
		register_dock_dependent_device(device, ejd);

	acpi_bus_get_status(device);
	/* Skip devices that are not present. */
	if (!acpi_device_is_present(device)) {
		device->flags.visited = false;
		device->flags.power_manageable = 0;
		return;
	}
	if (device->handler)
		goto ok;

	if (!device->flags.initialized) {
		device->flags.power_manageable =
			device->power.states[ACPI_STATE_D0].flags.valid;
		if (acpi_bus_init_power(device))
			device->flags.power_manageable = 0;

		device->flags.initialized = true;
	}
	device->flags.visited = false;
	ret = acpi_scan_attach_handler(device);
	if (ret < 0)
		return;

	device->flags.match_driver = true;
	if (!ret) {
		ret = device_attach(&device->dev);
		if (ret < 0)
			return;

		if (!ret && device->pnp.type.platform_id)
			acpi_default_enumeration(device);
	}
	device->flags.visited = true;

 ok:
	list_for_each_entry(child, &device->children, node)
		acpi_bus_attach(child);

	if (device->handler && device->handler->hotplug.notify_online)
		device->handler->hotplug.notify_online(device);
}

void acpi_walk_dep_device_list(acpi_handle handle)
{
	struct acpi_dep_data *dep, *tmp;
	struct acpi_device *adev;

	mutex_lock(&acpi_dep_list_lock);
	list_for_each_entry_safe(dep, tmp, &acpi_dep_list, node) {
		if (dep->master == handle) {
			acpi_bus_get_device(dep->slave, &adev);
			if (!adev)
				continue;

			adev->dep_unmet--;
			if (!adev->dep_unmet)
				acpi_bus_attach(adev);
			list_del(&dep->node);
			kfree(dep);
		}
	}
	mutex_unlock(&acpi_dep_list_lock);
}
EXPORT_SYMBOL_GPL(acpi_walk_dep_device_list);

/**
 * acpi_bus_scan - Add ACPI device node objects in a given namespace scope.
 * @handle: Root of the namespace scope to scan.
 *
 * Scan a given ACPI tree (probably recently hot-plugged) and create and add
 * found devices.
 *
 * If no devices were found, -ENODEV is returned, but it does not mean that
 * there has been a real error.  There just have been no suitable ACPI objects
 * in the table trunk from which the kernel could create a device and add an
 * appropriate driver.
 *
 * Must be called under acpi_scan_lock.
 */
int acpi_bus_scan(acpi_handle handle)
{
	void *device = NULL;

	if (ACPI_SUCCESS(acpi_bus_check_add(handle, 0, NULL, &device)))
		acpi_walk_namespace(ACPI_TYPE_ANY, handle, ACPI_UINT32_MAX,
				    acpi_bus_check_add, NULL, NULL, &device);

	if (device) {
		acpi_bus_attach(device);
		return 0;
	}
	return -ENODEV;
}
EXPORT_SYMBOL(acpi_bus_scan);

/**
 * acpi_bus_trim - Detach scan handlers and drivers from ACPI device objects.
 * @adev: Root of the ACPI namespace scope to walk.
 *
 * Must be called under acpi_scan_lock.
 */
void acpi_bus_trim(struct acpi_device *adev)
{
	struct acpi_scan_handler *handler = adev->handler;
	struct acpi_device *child;

	list_for_each_entry_reverse(child, &adev->children, node)
		acpi_bus_trim(child);

	adev->flags.match_driver = false;
	if (handler) {
		if (handler->detach)
			handler->detach(adev);

		adev->handler = NULL;
	} else {
		device_release_driver(&adev->dev);
	}
	/*
	 * Most likely, the device is going away, so put it into D3cold before
	 * that.
	 */
	acpi_device_set_power(adev, ACPI_STATE_D3_COLD);
	adev->flags.initialized = false;
	adev->flags.visited = false;
}
EXPORT_SYMBOL_GPL(acpi_bus_trim);

static int acpi_bus_scan_fixed(void)
{
	int result = 0;

	/*
	 * Enumerate all fixed-feature devices.
	 */
	if ((acpi_gbl_FADT.flags & ACPI_FADT_POWER_BUTTON) == 0) {
		result = acpi_add_single_object(&device, acpi_root,
						NULL,
						ACPI_BUS_TYPE_POWER_BUTTON,
						&ops);
	}

	if ((acpi_gbl_FADT.flags & ACPI_FADT_SLEEP_BUTTON) == 0) {
		result = acpi_add_single_object(&device, acpi_root,
						NULL,
						ACPI_BUS_TYPE_SLEEP_BUTTON,
						&ops);
	}

	return result;
}

int __init acpi_boot_ec_enable(void);

static int __init acpi_scan_init(void)
{
	int result;
	struct acpi_bus_ops ops;

	if (!(acpi_gbl_FADT.flags & ACPI_FADT_POWER_BUTTON)) {
		struct acpi_device *device = NULL;

		result = acpi_add_single_object(&device, NULL,
						ACPI_BUS_TYPE_POWER_BUTTON,
						ACPI_STA_DEFAULT);
		if (result)
			return result;

		device->flags.match_driver = true;
		result = device_attach(&device->dev);
		if (result < 0)
			return result;

		device_init_wakeup(&device->dev, true);
	}

	if (!(acpi_gbl_FADT.flags & ACPI_FADT_SLEEP_BUTTON)) {
		struct acpi_device *device = NULL;

		result = acpi_add_single_object(&device, NULL,
						ACPI_BUS_TYPE_SLEEP_BUTTON,
						ACPI_STA_DEFAULT);
		if (result)
			return result;

		device->flags.match_driver = true;
		result = device_attach(&device->dev);
	}

	return result < 0 ? result : 0;
}

int __init acpi_scan_init(void)
{
	int result;

	acpi_pci_root_init();
	acpi_pci_link_init();
	acpi_processor_init();
	acpi_lpss_init();
	acpi_apd_init();
	acpi_cmos_rtc_init();
	acpi_container_init();
	acpi_memory_hotplug_init();
	acpi_pnp_init();
	acpi_int340x_thermal_init();

	acpi_scan_add_handler(&generic_device_handler);

	mutex_lock(&acpi_scan_lock);
	/*
	 * Enumerate devices in the ACPI namespace.
	 */
	result = acpi_bus_scan(ACPI_ROOT_OBJECT);
	if (result)
		goto out;

	result = acpi_bus_get_device(ACPI_ROOT_OBJECT, &acpi_root);
	if (result)
		goto out;

	/* Fixed feature devices do not exist on HW-reduced platform */
	if (!acpi_gbl_reduced_hardware) {
		result = acpi_bus_scan_fixed();
		if (result) {
			acpi_detach_data(acpi_root->handle,
					 acpi_scan_drop_device);
			acpi_device_del(acpi_root);
			put_device(&acpi_root->dev);
			goto out;
		}
	}

	acpi_update_all_gpes();

 out:
	mutex_unlock(&acpi_scan_lock);
	return result;
}

static struct acpi_probe_entry *ape;
static int acpi_probe_count;
static DEFINE_MUTEX(acpi_probe_mutex);

static int __init acpi_match_madt(struct acpi_subtable_header *header,
				  const unsigned long end)
{
	if (!ape->subtable_valid || ape->subtable_valid(header, ape))
		if (!ape->probe_subtbl(header, end))
			acpi_probe_count++;

	return 0;
}

int __init __acpi_probe_device_table(struct acpi_probe_entry *ap_head, int nr)
{
	int count = 0;

	if (acpi_disabled)
		return 0;

	memset(&ops, 0, sizeof(ops));
	ops.acpi_op_add = 1;
	ops.acpi_op_start = 1;

	result = bus_register(&acpi_bus_type);
	if (result) {
		/* We don't want to quit even if we failed to add suspend/resume */
		printk(KERN_ERR PREFIX "Could not register bus type\n");
	}

	/*
	 * Create the root device in the bus's device tree
	 */
	result = acpi_add_single_object(&acpi_root, NULL, ACPI_ROOT_OBJECT,
					ACPI_BUS_TYPE_SYSTEM, &ops);
	if (result)
		goto Done;

	/*
	 * Enumerate devices in the ACPI namespace.
	 */
	result = acpi_bus_scan_fixed(acpi_root);

	/* EC region might be needed at bus_scan, so enable it now */
	acpi_boot_ec_enable();

	if (!result)
		result = acpi_bus_scan(acpi_root, &ops);

	if (result)
		acpi_device_unregister(acpi_root, ACPI_BUS_REMOVAL_NORMAL);

      Done:
	return result;
}

subsys_initcall(acpi_scan_init);
	spin_lock(&acpi_probe_lock);
	mutex_lock(&acpi_probe_mutex);
	for (ape = ap_head; nr; ape++, nr--) {
		if (ACPI_COMPARE_NAME(ACPI_SIG_MADT, ape->id)) {
			acpi_probe_count = 0;
			acpi_table_parse_madt(ape->type, acpi_match_madt, 0);
			count += acpi_probe_count;
		} else {
			int res;
			res = acpi_table_parse(ape->id, ape->probe_table);
			if (!res)
				count++;
		}
	}
	mutex_unlock(&acpi_probe_mutex);

	return count;
}
