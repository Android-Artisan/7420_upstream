/*
 *  acpi_bus.c - ACPI Bus Driver ($Revision: 80 $)
 *
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/device.h>
#include <linux/proc_fs.h>
#include <linux/acpi.h>
#include <linux/slab.h>
#include <linux/regulator/machine.h>
#ifdef CONFIG_X86
#include <asm/mpspec.h>
#endif
#include <linux/pci.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>
#include <acpi/apei.h>
#include <linux/dmi.h>
#include <linux/suspend.h>

#include "internal.h"

#define _COMPONENT		ACPI_BUS_COMPONENT
ACPI_MODULE_NAME("bus");

struct acpi_device *acpi_root;
struct proc_dir_entry *acpi_root_dir;
EXPORT_SYMBOL(acpi_root_dir);

#define STRUCT_TO_INT(s)	(*((int*)&s))
#ifdef CONFIG_X86
#ifdef CONFIG_ACPI_CUSTOM_DSDT
static inline int set_copy_dsdt(const struct dmi_system_id *id)
{
	return 0;
}
#else
static int set_copy_dsdt(const struct dmi_system_id *id)
{
	printk(KERN_NOTICE "%s detected - "
		"force copy of DSDT to local memory\n", id->ident);
	acpi_gbl_copy_dsdt_locally = 1;
	return 0;
}
#endif

static struct dmi_system_id dsdt_dmi_table[] __initdata = {
	/*
	 * Invoke DSDT corruption work-around on all Toshiba Satellite.
	 * https://bugzilla.kernel.org/show_bug.cgi?id=14679
	 */
	{
	 .callback = set_copy_dsdt,
	 .ident = "TOSHIBA Satellite",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
		DMI_MATCH(DMI_PRODUCT_NAME, "Satellite"),
		},
	},
	{}
};
#else
static struct dmi_system_id dsdt_dmi_table[] __initdata = {
	{}
};
#endif

/* --------------------------------------------------------------------------
                                Device Management
   -------------------------------------------------------------------------- */

int acpi_bus_get_device(acpi_handle handle, struct acpi_device **device)
{
	acpi_status status = AE_OK;


	if (!device)
		return -EINVAL;

	/* TBD: Support fixed-feature devices */

	status = acpi_get_data(handle, acpi_bus_data_handler, (void **)device);
	if (ACPI_FAILURE(status) || !*device) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "No context for object [%p]\n",
				  handle));
		return -ENODEV;
	}

	return 0;
}

EXPORT_SYMBOL(acpi_bus_get_device);

int acpi_bus_get_status(struct acpi_device *device)
{
	acpi_status status = AE_OK;
	unsigned long sta = 0;


	if (!device)
		return -EINVAL;

	/*
	 * Evaluate _STA if present.
	 */
	if (device->flags.dynamic_status) {
		status =
		    acpi_evaluate_integer(device->handle, "_STA", NULL, &sta);
		if (ACPI_FAILURE(status))
			return -ENODEV;
		STRUCT_TO_INT(device->status) = (int)sta;
	}

	/*
	 * Otherwise we assume the status of our parent (unless we don't
	 * have one, in which case status is implied).
	 */
	else if (device->parent)
		device->status = device->parent->status;
	else
		STRUCT_TO_INT(device->status) =
		    ACPI_STA_DEVICE_PRESENT | ACPI_STA_DEVICE_ENABLED |
		    ACPI_STA_DEVICE_UI      | ACPI_STA_DEVICE_FUNCTIONING;

	if (device->status.functional && !device->status.present) {
		printk(KERN_WARNING PREFIX "Device [%s] status [%08x]: "
		       "functional but not present; setting present\n",
		       device->pnp.bus_id, (u32) STRUCT_TO_INT(device->status));
		device->status.present = 1;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Device [%s] status [%08x]\n",
			  device->pnp.bus_id,
			  (u32) STRUCT_TO_INT(device->status)));

	return 0;
}

EXPORT_SYMBOL(acpi_bus_get_status);

void acpi_bus_private_data_handler(acpi_handle handle,
				   u32 function, void *context)
acpi_status acpi_bus_get_status_handle(acpi_handle handle,
				       unsigned long long *sta)
{
	acpi_status status;

	status = acpi_evaluate_integer(handle, "_STA", NULL, sta);
	if (ACPI_SUCCESS(status))
		return AE_OK;

	if (status == AE_NOT_FOUND) {
		*sta = ACPI_STA_DEVICE_PRESENT | ACPI_STA_DEVICE_ENABLED |
		       ACPI_STA_DEVICE_UI      | ACPI_STA_DEVICE_FUNCTIONING;
		return AE_OK;
	}
	return status;
}

int acpi_bus_get_status(struct acpi_device *device)
{
	acpi_status status;
	unsigned long long sta;

	status = acpi_bus_get_status_handle(device->handle, &sta);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	acpi_set_device_status(device, sta);

	if (device->status.functional && !device->status.present) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Device [%s] status [%08x]: "
		       "functional but not present;\n",
			device->pnp.bus_id, (u32)sta));
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Device [%s] status [%08x]\n",
			  device->pnp.bus_id, (u32)sta));
	return 0;
}
EXPORT_SYMBOL(acpi_bus_get_status);

void acpi_bus_private_data_handler(acpi_handle handle,
				   void *context)
{
	return;
}
EXPORT_SYMBOL(acpi_bus_private_data_handler);

int acpi_bus_get_private_data(acpi_handle handle, void **data)
{
	acpi_status status = AE_OK;
int acpi_bus_attach_private_data(acpi_handle handle, void *data)
{
	acpi_status status;

	status = acpi_attach_data(handle,
			acpi_bus_private_data_handler, data);
	if (ACPI_FAILURE(status)) {
		acpi_handle_debug(handle, "Error attaching device data\n");
		return -ENODEV;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(acpi_bus_attach_private_data);

int acpi_bus_get_private_data(acpi_handle handle, void **data)
{
	acpi_status status;

	if (!data)
		return -EINVAL;

	status = acpi_get_data(handle, acpi_bus_private_data_handler, data);
	if (ACPI_FAILURE(status) || !*data) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "No context for object [%p]\n",
				handle));
	if (ACPI_FAILURE(status)) {
		acpi_handle_debug(handle, "No context for object\n");
		return -ENODEV;
	}

	return 0;
}
EXPORT_SYMBOL(acpi_bus_get_private_data);

/* --------------------------------------------------------------------------
                                 Power Management
   -------------------------------------------------------------------------- */

int acpi_bus_get_power(acpi_handle handle, int *state)
{
	int result = 0;
	acpi_status status = 0;
	struct acpi_device *device = NULL;
	unsigned long psc = 0;


	result = acpi_bus_get_device(handle, &device);
	if (result)
		return result;

	*state = ACPI_STATE_UNKNOWN;

	if (!device->flags.power_manageable) {
		/* TBD: Non-recursive algorithm for walking up hierarchy */
		if (device->parent)
			*state = device->parent->power.state;
		else
			*state = ACPI_STATE_D0;
	} else {
		/*
		 * Get the device's power state either directly (via _PSC) or
		 * indirectly (via power resources).
		 */
		if (device->power.flags.explicit_get) {
			status = acpi_evaluate_integer(device->handle, "_PSC",
						       NULL, &psc);
			if (ACPI_FAILURE(status))
				return -ENODEV;
			device->power.state = (int)psc;
		} else if (device->power.flags.power_resources) {
			result = acpi_power_get_inferred_state(device);
			if (result)
				return result;
		}

		*state = device->power.state;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Device [%s] power state is D%d\n",
			  device->pnp.bus_id, device->power.state));

	return 0;
}

EXPORT_SYMBOL(acpi_bus_get_power);

int acpi_bus_set_power(acpi_handle handle, int state)
{
	int result = 0;
	acpi_status status = AE_OK;
	struct acpi_device *device = NULL;
	char object_name[5] = { '_', 'P', 'S', '0' + state, '\0' };


	result = acpi_bus_get_device(handle, &device);
	if (result)
		return result;

	if ((state < ACPI_STATE_D0) || (state > ACPI_STATE_D3))
		return -EINVAL;

	/* Make sure this is a valid target state */

	if (!device->flags.power_manageable) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Device `[%s]' is not power manageable\n",
				kobject_name(&device->dev.kobj)));
		return -ENODEV;
	}
	/*
	 * Get device's current power state
	 */
	acpi_bus_get_power(device->handle, &device->power.state);
	if ((state == device->power.state) && !device->flags.force_power_state) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Device is already at D%d\n",
				  state));
		return 0;
	}

	if (!device->power.states[state].flags.valid) {
		printk(KERN_WARNING PREFIX "Device does not support D%d\n", state);
		return -ENODEV;
	}
	if (device->parent && (state < device->parent->power.state)) {
		printk(KERN_WARNING PREFIX
			      "Cannot set device to a higher-powered"
			      " state than parent\n");
		return -ENODEV;
	}

	/*
	 * Transition Power
	 * ----------------
	 * On transitions to a high-powered state we first apply power (via
	 * power resources) then evalute _PSx.  Conversly for transitions to
	 * a lower-powered state.
	 */
	if (state < device->power.state) {
		if (device->power.flags.power_resources) {
			result = acpi_power_transition(device, state);
			if (result)
				goto end;
		}
		if (device->power.states[state].flags.explicit_set) {
			status = acpi_evaluate_object(device->handle,
						      object_name, NULL, NULL);
			if (ACPI_FAILURE(status)) {
				result = -ENODEV;
				goto end;
			}
		}
	} else {
		if (device->power.states[state].flags.explicit_set) {
			status = acpi_evaluate_object(device->handle,
						      object_name, NULL, NULL);
			if (ACPI_FAILURE(status)) {
				result = -ENODEV;
				goto end;
			}
		}
		if (device->power.flags.power_resources) {
			result = acpi_power_transition(device, state);
			if (result)
				goto end;
		}
	}

      end:
	if (result)
		printk(KERN_WARNING PREFIX
			      "Transitioning device [%s] to D%d\n",
			      device->pnp.bus_id, state);
	else {
		device->power.state = state;
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Device [%s] transitioned to D%d\n",
				  device->pnp.bus_id, state));
	}

	return result;
}

EXPORT_SYMBOL(acpi_bus_set_power);

bool acpi_bus_power_manageable(acpi_handle handle)
{
	struct acpi_device *device;
	int result;

	result = acpi_bus_get_device(handle, &device);
	return result ? false : device->flags.power_manageable;
}

EXPORT_SYMBOL(acpi_bus_power_manageable);

bool acpi_bus_can_wakeup(acpi_handle handle)
{
	struct acpi_device *device;
	int result;

	result = acpi_bus_get_device(handle, &device);
	return result ? false : device->wakeup.flags.valid;
}

EXPORT_SYMBOL(acpi_bus_can_wakeup);

/* --------------------------------------------------------------------------
                                Event Management
   -------------------------------------------------------------------------- */

#ifdef CONFIG_ACPI_PROC_EVENT
static DEFINE_SPINLOCK(acpi_bus_event_lock);

LIST_HEAD(acpi_bus_event_list);
DECLARE_WAIT_QUEUE_HEAD(acpi_bus_event_queue);

extern int event_is_open;

int acpi_bus_generate_proc_event4(const char *device_class, const char *bus_id, u8 type, int data)
{
	struct acpi_bus_event *event;
	unsigned long flags = 0;

	/* drop event on the floor if no one's listening */
	if (!event_is_open)
		return 0;

	event = kmalloc(sizeof(struct acpi_bus_event), GFP_ATOMIC);
	if (!event)
		return -ENOMEM;

	strcpy(event->device_class, device_class);
	strcpy(event->bus_id, bus_id);
	event->type = type;
	event->data = data;

	spin_lock_irqsave(&acpi_bus_event_lock, flags);
	list_add_tail(&event->node, &acpi_bus_event_list);
	spin_unlock_irqrestore(&acpi_bus_event_lock, flags);

	wake_up_interruptible(&acpi_bus_event_queue);

	return 0;

}

EXPORT_SYMBOL_GPL(acpi_bus_generate_proc_event4);

int acpi_bus_generate_proc_event(struct acpi_device *device, u8 type, int data)
{
	if (!device)
		return -EINVAL;
	return acpi_bus_generate_proc_event4(device->pnp.device_class,
					     device->pnp.bus_id, type, data);
}

EXPORT_SYMBOL(acpi_bus_generate_proc_event);

int acpi_bus_receive_event(struct acpi_bus_event *event)
{
	unsigned long flags = 0;
	struct acpi_bus_event *entry = NULL;

	DECLARE_WAITQUEUE(wait, current);


	if (!event)
		return -EINVAL;

	if (list_empty(&acpi_bus_event_list)) {

		set_current_state(TASK_INTERRUPTIBLE);
		add_wait_queue(&acpi_bus_event_queue, &wait);

		if (list_empty(&acpi_bus_event_list))
			schedule();

		remove_wait_queue(&acpi_bus_event_queue, &wait);
		set_current_state(TASK_RUNNING);

		if (signal_pending(current))
			return -ERESTARTSYS;
	}

	spin_lock_irqsave(&acpi_bus_event_lock, flags);
	if (!list_empty(&acpi_bus_event_list)) {
		entry = list_entry(acpi_bus_event_list.next,
				   struct acpi_bus_event, node);
		list_del(&entry->node);
	}
	spin_unlock_irqrestore(&acpi_bus_event_lock, flags);

	if (!entry)
		return -ENODEV;

	memcpy(event, entry, sizeof(struct acpi_bus_event));

	kfree(entry);

	return 0;
}

#endif	/* CONFIG_ACPI_PROC_EVENT */
EXPORT_SYMBOL_GPL(acpi_bus_get_private_data);

void acpi_bus_detach_private_data(acpi_handle handle)
{
	acpi_detach_data(handle, acpi_bus_private_data_handler);
}
EXPORT_SYMBOL_GPL(acpi_bus_detach_private_data);

static void acpi_print_osc_error(acpi_handle handle,
	struct acpi_osc_context *context, char *error)
{
	struct acpi_buffer buffer = {ACPI_ALLOCATE_BUFFER};
	int i;

	if (ACPI_FAILURE(acpi_get_name(handle, ACPI_FULL_PATHNAME, &buffer)))
		printk(KERN_DEBUG "%s\n", error);
	else {
		printk(KERN_DEBUG "%s:%s\n", (char *)buffer.pointer, error);
		kfree(buffer.pointer);
	}
	printk(KERN_DEBUG"_OSC request data:");
	for (i = 0; i < context->cap.length; i += sizeof(u32))
		printk("%x ", *((u32 *)(context->cap.pointer + i)));
	printk("\n");
}

acpi_status acpi_str_to_uuid(char *str, u8 *uuid)
{
	int i;
	static int opc_map_to_uuid[16] = {6, 4, 2, 0, 11, 9, 16, 14, 19, 21,
		24, 26, 28, 30, 32, 34};

	if (strlen(str) != 36)
		return AE_BAD_PARAMETER;
	for (i = 0; i < 36; i++) {
		if (i == 8 || i == 13 || i == 18 || i == 23) {
			if (str[i] != '-')
				return AE_BAD_PARAMETER;
		} else if (!isxdigit(str[i]))
			return AE_BAD_PARAMETER;
	}
	for (i = 0; i < 16; i++) {
		uuid[i] = hex_to_bin(str[opc_map_to_uuid[i]]) << 4;
		uuid[i] |= hex_to_bin(str[opc_map_to_uuid[i] + 1]);
	}
	return AE_OK;
}
EXPORT_SYMBOL_GPL(acpi_str_to_uuid);

acpi_status acpi_run_osc(acpi_handle handle, struct acpi_osc_context *context)
{
	acpi_status status;
	struct acpi_object_list input;
	union acpi_object in_params[4];
	union acpi_object *out_obj;
	u8 uuid[16];
	u32 errors;
	struct acpi_buffer output = {ACPI_ALLOCATE_BUFFER, NULL};

	if (!context)
		return AE_ERROR;
	if (ACPI_FAILURE(acpi_str_to_uuid(context->uuid_str, uuid)))
		return AE_ERROR;
	context->ret.length = ACPI_ALLOCATE_BUFFER;
	context->ret.pointer = NULL;

	/* Setting up input parameters */
	input.count = 4;
	input.pointer = in_params;
	in_params[0].type 		= ACPI_TYPE_BUFFER;
	in_params[0].buffer.length 	= 16;
	in_params[0].buffer.pointer	= uuid;
	in_params[1].type 		= ACPI_TYPE_INTEGER;
	in_params[1].integer.value 	= context->rev;
	in_params[2].type 		= ACPI_TYPE_INTEGER;
	in_params[2].integer.value	= context->cap.length/sizeof(u32);
	in_params[3].type		= ACPI_TYPE_BUFFER;
	in_params[3].buffer.length 	= context->cap.length;
	in_params[3].buffer.pointer 	= context->cap.pointer;

	status = acpi_evaluate_object(handle, "_OSC", &input, &output);
	if (ACPI_FAILURE(status))
		return status;

	if (!output.length)
		return AE_NULL_OBJECT;

	out_obj = output.pointer;
	if (out_obj->type != ACPI_TYPE_BUFFER
		|| out_obj->buffer.length != context->cap.length) {
		acpi_print_osc_error(handle, context,
			"_OSC evaluation returned wrong type");
		status = AE_TYPE;
		goto out_kfree;
	}
	/* Need to ignore the bit0 in result code */
	errors = *((u32 *)out_obj->buffer.pointer) & ~(1 << 0);
	if (errors) {
		if (errors & OSC_REQUEST_ERROR)
			acpi_print_osc_error(handle, context,
				"_OSC request failed");
		if (errors & OSC_INVALID_UUID_ERROR)
			acpi_print_osc_error(handle, context,
				"_OSC invalid UUID");
		if (errors & OSC_INVALID_REVISION_ERROR)
			acpi_print_osc_error(handle, context,
				"_OSC invalid revision");
		if (errors & OSC_CAPABILITIES_MASK_ERROR) {
			if (((u32 *)context->cap.pointer)[OSC_QUERY_DWORD]
			    & OSC_QUERY_ENABLE)
				goto out_success;
			status = AE_SUPPORT;
			goto out_kfree;
		}
		status = AE_ERROR;
		goto out_kfree;
	}
out_success:
	context->ret.length = out_obj->buffer.length;
	context->ret.pointer = kmemdup(out_obj->buffer.pointer,
				       context->ret.length, GFP_KERNEL);
	if (!context->ret.pointer) {
		status =  AE_NO_MEMORY;
		goto out_kfree;
	}
	status =  AE_OK;

out_kfree:
	kfree(output.pointer);
	if (status != AE_OK)
		context->ret.pointer = NULL;
	return status;
}
EXPORT_SYMBOL(acpi_run_osc);

bool osc_sb_apei_support_acked;
static u8 sb_uuid_str[] = "0811B06E-4A27-44F9-8D60-3CBBC22E7B48";
static void acpi_bus_osc_support(void)
{
	u32 capbuf[2];
	struct acpi_osc_context context = {
		.uuid_str = sb_uuid_str,
		.rev = 1,
		.cap.length = 8,
		.cap.pointer = capbuf,
	};
	acpi_handle handle;

	capbuf[OSC_QUERY_DWORD] = OSC_QUERY_ENABLE;
	capbuf[OSC_SUPPORT_DWORD] = OSC_SB_PR3_SUPPORT; /* _PR3 is in use */
	if (IS_ENABLED(CONFIG_ACPI_PROCESSOR_AGGREGATOR))
		capbuf[OSC_SUPPORT_DWORD] |= OSC_SB_PAD_SUPPORT;
	if (IS_ENABLED(CONFIG_ACPI_PROCESSOR))
		capbuf[OSC_SUPPORT_DWORD] |= OSC_SB_PPC_OST_SUPPORT;

	capbuf[OSC_SUPPORT_DWORD] |= OSC_SB_HOTPLUG_OST_SUPPORT;

	if (!ghes_disable)
		capbuf[OSC_SUPPORT_DWORD] |= OSC_SB_APEI_SUPPORT;
	if (ACPI_FAILURE(acpi_get_handle(NULL, "\\_SB", &handle)))
		return;
	if (ACPI_SUCCESS(acpi_run_osc(handle, &context))) {
		u32 *capbuf_ret = context.ret.pointer;
		if (context.ret.length > OSC_SUPPORT_DWORD)
			osc_sb_apei_support_acked =
				capbuf_ret[OSC_SUPPORT_DWORD] & OSC_SB_APEI_SUPPORT;
		kfree(context.ret.pointer);
	}
	/* do we need to check other returned cap? Sounds no */
}

/* --------------------------------------------------------------------------
                             Notification Handling
   -------------------------------------------------------------------------- */

static int
acpi_bus_check_device(struct acpi_device *device, int *status_changed)
{
	acpi_status status = 0;
	struct acpi_device_status old_status;


	if (!device)
		return -EINVAL;

	if (status_changed)
		*status_changed = 0;

	old_status = device->status;

	/*
	 * Make sure this device's parent is present before we go about
	 * messing with the device.
	 */
	if (device->parent && !device->parent->status.present) {
		device->status = device->parent->status;
		if (STRUCT_TO_INT(old_status) != STRUCT_TO_INT(device->status)) {
			if (status_changed)
				*status_changed = 1;
		}
		return 0;
	}

	status = acpi_bus_get_status(device);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	if (STRUCT_TO_INT(old_status) == STRUCT_TO_INT(device->status))
		return 0;

	if (status_changed)
		*status_changed = 1;

	/*
	 * Device Insertion/Removal
	 */
	if ((device->status.present) && !(old_status.present)) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Device insertion detected\n"));
		/* TBD: Handle device insertion */
	} else if (!(device->status.present) && (old_status.present)) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Device removal detected\n"));
		/* TBD: Handle device removal */
	}

	return 0;
}

static int acpi_bus_check_scope(struct acpi_device *device)
{
	int result = 0;
	int status_changed = 0;


	if (!device)
		return -EINVAL;

	/* Status Change? */
	result = acpi_bus_check_device(device, &status_changed);
	if (result)
		return result;

	if (!status_changed)
		return 0;

	/*
	 * TBD: Enumerate child devices within this device's scope and
	 *       run acpi_bus_check_device()'s on them.
	 */

	return 0;
}

/**
 * acpi_bus_notify
 * ---------------
 * Callback for all 'system-level' device notifications (values 0x00-0x7F).
 */
static void acpi_bus_notify(acpi_handle handle, u32 type, void *data)
{
	int result = 0;
	struct acpi_device *device = NULL;


	if (acpi_bus_get_device(handle, &device))
		return;

	switch (type) {

	case ACPI_NOTIFY_BUS_CHECK:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Received BUS CHECK notification for device [%s]\n",
				  device->pnp.bus_id));
		result = acpi_bus_check_scope(device);
		/*
		 * TBD: We'll need to outsource certain events to non-ACPI
		 *      drivers via the device manager (device.c).
		 */
		break;

	case ACPI_NOTIFY_DEVICE_CHECK:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Received DEVICE CHECK notification for device [%s]\n",
				  device->pnp.bus_id));
		result = acpi_bus_check_device(device, NULL);
		/*
		 * TBD: We'll need to outsource certain events to non-ACPI
		 *      drivers via the device manager (device.c).
		 */
		break;

	case ACPI_NOTIFY_DEVICE_WAKE:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Received DEVICE WAKE notification for device [%s]\n",
				  device->pnp.bus_id));
		/* TBD */
		break;

	case ACPI_NOTIFY_EJECT_REQUEST:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Received EJECT REQUEST notification for device [%s]\n",
				  device->pnp.bus_id));
		/* TBD */
		break;

	case ACPI_NOTIFY_DEVICE_CHECK_LIGHT:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Received DEVICE CHECK LIGHT notification for device [%s]\n",
				  device->pnp.bus_id));
	struct acpi_device *adev;
	struct acpi_driver *driver;
	u32 ost_code = ACPI_OST_SC_NON_SPECIFIC_FAILURE;
	bool hotplug_event = false;

	switch (type) {
	case ACPI_NOTIFY_BUS_CHECK:
		acpi_handle_debug(handle, "ACPI_NOTIFY_BUS_CHECK event\n");
		hotplug_event = true;
		break;

	case ACPI_NOTIFY_DEVICE_CHECK:
		acpi_handle_debug(handle, "ACPI_NOTIFY_DEVICE_CHECK event\n");
		hotplug_event = true;
		break;

	case ACPI_NOTIFY_DEVICE_WAKE:
		acpi_handle_debug(handle, "ACPI_NOTIFY_DEVICE_WAKE event\n");
		break;

	case ACPI_NOTIFY_EJECT_REQUEST:
		acpi_handle_debug(handle, "ACPI_NOTIFY_EJECT_REQUEST event\n");
		hotplug_event = true;
		break;

	case ACPI_NOTIFY_DEVICE_CHECK_LIGHT:
		acpi_handle_debug(handle, "ACPI_NOTIFY_DEVICE_CHECK_LIGHT event\n");
		/* TBD: Exactly what does 'light' mean? */
		break;

	case ACPI_NOTIFY_FREQUENCY_MISMATCH:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Received FREQUENCY MISMATCH notification for device [%s]\n",
				  device->pnp.bus_id));
		/* TBD */
		break;

	case ACPI_NOTIFY_BUS_MODE_MISMATCH:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Received BUS MODE MISMATCH notification for device [%s]\n",
				  device->pnp.bus_id));
		/* TBD */
		break;

	case ACPI_NOTIFY_POWER_FAULT:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Received POWER FAULT notification for device [%s]\n",
				  device->pnp.bus_id));
		/* TBD */
		break;

	default:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Received unknown/unsupported notification [%08x]\n",
				  type));
		break;
	}

	return;
}

		acpi_handle_err(handle, "Device cannot be configured due "
				"to a frequency mismatch\n");
		break;

	case ACPI_NOTIFY_BUS_MODE_MISMATCH:
		acpi_handle_err(handle, "Device cannot be configured due "
				"to a bus mode mismatch\n");
		break;

	case ACPI_NOTIFY_POWER_FAULT:
		acpi_handle_err(handle, "Device has suffered a power fault\n");
		break;

	default:
		acpi_handle_debug(handle, "Unknown event type 0x%x\n", type);
		break;
	}

	adev = acpi_bus_get_acpi_device(handle);
	if (!adev)
		goto err;

	driver = adev->driver;
	if (driver && driver->ops.notify &&
	    (driver->flags & ACPI_DRIVER_ALL_NOTIFY_EVENTS))
		driver->ops.notify(adev, type);

	if (hotplug_event && ACPI_SUCCESS(acpi_hotplug_schedule(adev, type)))
		return;

	acpi_bus_put_acpi_device(adev);
	return;

 err:
	acpi_evaluate_ost(handle, type, ost_code, NULL);
}

static void acpi_device_notify(acpi_handle handle, u32 event, void *data)
{
	struct acpi_device *device = data;

	device->driver->ops.notify(device, event);
}

static void acpi_device_notify_fixed(void *data)
{
	struct acpi_device *device = data;

	/* Fixed hardware devices have no handles */
	acpi_device_notify(NULL, ACPI_FIXED_HARDWARE_EVENT, device);
}

static u32 acpi_device_fixed_event(void *data)
{
	acpi_os_execute(OSL_NOTIFY_HANDLER, acpi_device_notify_fixed, data);
	return ACPI_INTERRUPT_HANDLED;
}

static int acpi_device_install_notify_handler(struct acpi_device *device)
{
	acpi_status status;

	if (device->device_type == ACPI_BUS_TYPE_POWER_BUTTON)
		status =
		    acpi_install_fixed_event_handler(ACPI_EVENT_POWER_BUTTON,
						     acpi_device_fixed_event,
						     device);
	else if (device->device_type == ACPI_BUS_TYPE_SLEEP_BUTTON)
		status =
		    acpi_install_fixed_event_handler(ACPI_EVENT_SLEEP_BUTTON,
						     acpi_device_fixed_event,
						     device);
	else
		status = acpi_install_notify_handler(device->handle,
						     ACPI_DEVICE_NOTIFY,
						     acpi_device_notify,
						     device);

	if (ACPI_FAILURE(status))
		return -EINVAL;
	return 0;
}

static void acpi_device_remove_notify_handler(struct acpi_device *device)
{
	if (device->device_type == ACPI_BUS_TYPE_POWER_BUTTON)
		acpi_remove_fixed_event_handler(ACPI_EVENT_POWER_BUTTON,
						acpi_device_fixed_event);
	else if (device->device_type == ACPI_BUS_TYPE_SLEEP_BUTTON)
		acpi_remove_fixed_event_handler(ACPI_EVENT_SLEEP_BUTTON,
						acpi_device_fixed_event);
	else
		acpi_remove_notify_handler(device->handle, ACPI_DEVICE_NOTIFY,
					   acpi_device_notify);
}

/* --------------------------------------------------------------------------
                             Device Matching
   -------------------------------------------------------------------------- */

static struct acpi_device *acpi_primary_dev_companion(struct acpi_device *adev,
						      const struct device *dev)
{
	struct mutex *physical_node_lock = &adev->physical_node_lock;

	mutex_lock(physical_node_lock);
	if (list_empty(&adev->physical_node_list)) {
		adev = NULL;
	} else {
		const struct acpi_device_physical_node *node;

		node = list_first_entry(&adev->physical_node_list,
					struct acpi_device_physical_node, node);
		if (node->dev != dev)
			adev = NULL;
	}
	mutex_unlock(physical_node_lock);
	return adev;
}

/**
 * acpi_device_is_first_physical_node - Is given dev first physical node
 * @adev: ACPI companion device
 * @dev: Physical device to check
 *
 * Function checks if given @dev is the first physical devices attached to
 * the ACPI companion device. This distinction is needed in some cases
 * where the same companion device is shared between many physical devices.
 *
 * Note that the caller have to provide valid @adev pointer.
 */
bool acpi_device_is_first_physical_node(struct acpi_device *adev,
					const struct device *dev)
{
	return !!acpi_primary_dev_companion(adev, dev);
}

/*
 * acpi_companion_match() - Can we match via ACPI companion device
 * @dev: Device in question
 *
 * Check if the given device has an ACPI companion and if that companion has
 * a valid list of PNP IDs, and if the device is the first (primary) physical
 * device associated with it.  Return the companion pointer if that's the case
 * or NULL otherwise.
 *
 * If multiple physical devices are attached to a single ACPI companion, we need
 * to be careful.  The usage scenario for this kind of relationship is that all
 * of the physical devices in question use resources provided by the ACPI
 * companion.  A typical case is an MFD device where all the sub-devices share
 * the parent's ACPI companion.  In such cases we can only allow the primary
 * (first) physical device to be matched with the help of the companion's PNP
 * IDs.
 *
 * Additional physical devices sharing the ACPI companion can still use
 * resources available from it but they will be matched normally using functions
 * provided by their bus types (and analogously for their modalias).
 */
struct acpi_device *acpi_companion_match(const struct device *dev)
{
	struct acpi_device *adev;

	adev = ACPI_COMPANION(dev);
	if (!adev)
		return NULL;

	if (list_empty(&adev->pnp.ids))
		return NULL;

	return acpi_primary_dev_companion(adev, dev);
}

/**
 * acpi_of_match_device - Match device object using the "compatible" property.
 * @adev: ACPI device object to match.
 * @of_match_table: List of device IDs to match against.
 *
 * If @dev has an ACPI companion which has ACPI_DT_NAMESPACE_HID in its list of
 * identifiers and a _DSD object with the "compatible" property, use that
 * property to match against the given list of identifiers.
 */
static bool acpi_of_match_device(struct acpi_device *adev,
				 const struct of_device_id *of_match_table)
{
	const union acpi_object *of_compatible, *obj;
	int i, nval;

	if (!adev)
		return false;

	of_compatible = adev->data.of_compatible;
	if (!of_match_table || !of_compatible)
		return false;

	if (of_compatible->type == ACPI_TYPE_PACKAGE) {
		nval = of_compatible->package.count;
		obj = of_compatible->package.elements;
	} else { /* Must be ACPI_TYPE_STRING. */
		nval = 1;
		obj = of_compatible;
	}
	/* Now we can look for the driver DT compatible strings */
	for (i = 0; i < nval; i++, obj++) {
		const struct of_device_id *id;

		for (id = of_match_table; id->compatible[0]; id++)
			if (!strcasecmp(obj->string.pointer, id->compatible))
				return true;
	}

	return false;
}

static bool __acpi_match_device_cls(const struct acpi_device_id *id,
				    struct acpi_hardware_id *hwid)
{
	int i, msk, byte_shift;
	char buf[3];

	if (!id->cls)
		return false;

	/* Apply class-code bitmask, before checking each class-code byte */
	for (i = 1; i <= 3; i++) {
		byte_shift = 8 * (3 - i);
		msk = (id->cls_msk >> byte_shift) & 0xFF;
		if (!msk)
			continue;

		sprintf(buf, "%02x", (id->cls >> byte_shift) & msk);
		if (strncmp(buf, &hwid->id[(i - 1) * 2], 2))
			return false;
	}
	return true;
}

static const struct acpi_device_id *__acpi_match_device(
	struct acpi_device *device,
	const struct acpi_device_id *ids,
	const struct of_device_id *of_ids)
{
	const struct acpi_device_id *id;
	struct acpi_hardware_id *hwid;

	/*
	 * If the device is not present, it is unnecessary to load device
	 * driver for it.
	 */
	if (!device || !device->status.present)
		return NULL;

	list_for_each_entry(hwid, &device->pnp.ids, list) {
		/* First, check the ACPI/PNP IDs provided by the caller. */
		for (id = ids; id->id[0] || id->cls; id++) {
			if (id->id[0] && !strcmp((char *) id->id, hwid->id))
				return id;
			else if (id->cls && __acpi_match_device_cls(id, hwid))
				return id;
		}

		/*
		 * Next, check ACPI_DT_NAMESPACE_HID and try to match the
		 * "compatible" property if found.
		 *
		 * The id returned by the below is not valid, but the only
		 * caller passing non-NULL of_ids here is only interested in
		 * whether or not the return value is NULL.
		 */
		if (!strcmp(ACPI_DT_NAMESPACE_HID, hwid->id)
		    && acpi_of_match_device(device, of_ids))
			return id;
	}
	return NULL;
}

/**
 * acpi_match_device - Match a struct device against a given list of ACPI IDs
 * @ids: Array of struct acpi_device_id object to match against.
 * @dev: The device structure to match.
 *
 * Check if @dev has a valid ACPI handle and if there is a struct acpi_device
 * object for that handle and use that object to match against a given list of
 * device IDs.
 *
 * Return a pointer to the first matching ID on success or %NULL on failure.
 */
const struct acpi_device_id *acpi_match_device(const struct acpi_device_id *ids,
					       const struct device *dev)
{
	return __acpi_match_device(acpi_companion_match(dev), ids, NULL);
}
EXPORT_SYMBOL_GPL(acpi_match_device);

int acpi_match_device_ids(struct acpi_device *device,
			  const struct acpi_device_id *ids)
{
	return __acpi_match_device(device, ids, NULL) ? 0 : -ENOENT;
}
EXPORT_SYMBOL(acpi_match_device_ids);

bool acpi_driver_match_device(struct device *dev,
			      const struct device_driver *drv)
{
	if (!drv->acpi_match_table)
		return acpi_of_match_device(ACPI_COMPANION(dev),
					    drv->of_match_table);

	return !!__acpi_match_device(acpi_companion_match(dev),
				     drv->acpi_match_table, drv->of_match_table);
}
EXPORT_SYMBOL_GPL(acpi_driver_match_device);

/* --------------------------------------------------------------------------
                              ACPI Driver Management
   -------------------------------------------------------------------------- */

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
 * acpi_bus_unregister_driver - unregisters a driver with the ACPI bus
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
                              ACPI Bus operations
   -------------------------------------------------------------------------- */

static int acpi_bus_match(struct device *dev, struct device_driver *drv)
{
	struct acpi_device *acpi_dev = to_acpi_device(dev);
	struct acpi_driver *acpi_drv = to_acpi_driver(drv);

	return acpi_dev->flags.match_driver
		&& !acpi_match_device_ids(acpi_dev, acpi_drv->ids);
}

static int acpi_device_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	return __acpi_device_uevent_modalias(to_acpi_device(dev), env);
}

static int acpi_device_probe(struct device *dev)
{
	struct acpi_device *acpi_dev = to_acpi_device(dev);
	struct acpi_driver *acpi_drv = to_acpi_driver(dev->driver);
	int ret;

	if (acpi_dev->handler && !acpi_is_pnp_device(acpi_dev))
		return -EINVAL;

	if (!acpi_drv->ops.add)
		return -ENOSYS;

	ret = acpi_drv->ops.add(acpi_dev);
	if (ret)
		return ret;

	acpi_dev->driver = acpi_drv;
	ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			  "Driver [%s] successfully bound to device [%s]\n",
			  acpi_drv->name, acpi_dev->pnp.bus_id));

	if (acpi_drv->ops.notify) {
		ret = acpi_device_install_notify_handler(acpi_dev);
		if (ret) {
			if (acpi_drv->ops.remove)
				acpi_drv->ops.remove(acpi_dev);

			acpi_dev->driver = NULL;
			acpi_dev->driver_data = NULL;
			return ret;
		}
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Found driver [%s] for device [%s]\n",
			  acpi_drv->name, acpi_dev->pnp.bus_id));
	get_device(dev);
	return 0;
}

static int acpi_device_remove(struct device * dev)
{
	struct acpi_device *acpi_dev = to_acpi_device(dev);
	struct acpi_driver *acpi_drv = acpi_dev->driver;

	if (acpi_drv) {
		if (acpi_drv->ops.notify)
			acpi_device_remove_notify_handler(acpi_dev);
		if (acpi_drv->ops.remove)
			acpi_drv->ops.remove(acpi_dev);
	}
	acpi_dev->driver = NULL;
	acpi_dev->driver_data = NULL;

	put_device(dev);
	return 0;
}

struct bus_type acpi_bus_type = {
	.name		= "acpi",
	.match		= acpi_bus_match,
	.probe		= acpi_device_probe,
	.remove		= acpi_device_remove,
	.uevent		= acpi_device_uevent,
};

/* --------------------------------------------------------------------------
                             Initialization/Cleanup
   -------------------------------------------------------------------------- */

static int __init acpi_bus_init_irq(void)
{
	acpi_status status = AE_OK;
	union acpi_object arg = { ACPI_TYPE_INTEGER };
	struct acpi_object_list arg_list = { 1, &arg };
	acpi_status status;
	char *message = NULL;


	/*
	 * Let the system know what interrupt model we are using by
	 * evaluating the \_PIC object, if exists.
	 */

	switch (acpi_irq_model) {
	case ACPI_IRQ_MODEL_PIC:
		message = "PIC";
		break;
	case ACPI_IRQ_MODEL_IOAPIC:
		message = "IOAPIC";
		break;
	case ACPI_IRQ_MODEL_IOSAPIC:
		message = "IOSAPIC";
		break;
	case ACPI_IRQ_MODEL_GIC:
		message = "GIC";
		break;
	case ACPI_IRQ_MODEL_PLATFORM:
		message = "platform specific model";
		break;
	default:
		printk(KERN_WARNING PREFIX "Unknown interrupt routing model\n");
		return -ENODEV;
	}

	printk(KERN_INFO PREFIX "Using %s for interrupt routing\n", message);

	arg.integer.value = acpi_irq_model;

	status = acpi_evaluate_object(NULL, "\\_PIC", &arg_list, NULL);
	status = acpi_execute_simple_method(NULL, "\\_PIC", acpi_irq_model);
	if (ACPI_FAILURE(status) && (status != AE_NOT_FOUND)) {
		ACPI_EXCEPTION((AE_INFO, status, "Evaluating _PIC"));
		return -ENODEV;
	}

	return 0;
}

u8 acpi_gbl_permanent_mmap;


void __init acpi_early_init(void)
{
	acpi_status status = AE_OK;
/**
 * acpi_early_init - Initialize ACPICA and populate the ACPI namespace.
 *
 * The ACPI tables are accessible after this, but the handling of events has not
 * been initialized and the global lock is not available yet, so AML should not
 * be executed at this point.
 *
 * Doing this before switching the EFI runtime services to virtual mode allows
 * the EfiBootServices memory to be freed slightly earlier on boot.
 */
void __init acpi_early_init(void)
{
	acpi_status status;

	if (acpi_disabled)
		return;

	printk(KERN_INFO PREFIX "Core revision %08x\n", ACPI_CA_VERSION);

	/* It's safe to verify table checksums during late stage */
	acpi_gbl_verify_table_checksum = TRUE;

	/* enable workarounds, unless strict ACPI spec. compliance */
	if (!acpi_strict)
		acpi_gbl_enable_interpreter_slack = TRUE;

	acpi_gbl_permanent_mmap = 1;

	/*
	 * If the machine falls into the DMI check table,
	 * DSDT will be copied to memory
	 */
	dmi_check_system(dsdt_dmi_table);

	status = acpi_reallocate_root_table();
	if (ACPI_FAILURE(status)) {
		printk(KERN_ERR PREFIX
		       "Unable to reallocate ACPI tables\n");
		goto error0;
	}

	status = acpi_initialize_subsystem();
	if (ACPI_FAILURE(status)) {
		printk(KERN_ERR PREFIX
		       "Unable to initialize the ACPI Interpreter\n");
		goto error0;
	}

	status = acpi_load_tables();
	if (ACPI_FAILURE(status)) {
		printk(KERN_ERR PREFIX
		       "Unable to load the System Description Tables\n");
		goto error0;
	}

#ifdef CONFIG_X86
	if (!acpi_ioapic) {
		/* compatible (0) means level (3) */
		if (!(acpi_sci_flags & ACPI_MADT_TRIGGER_MASK)) {
			acpi_sci_flags &= ~ACPI_MADT_TRIGGER_MASK;
			acpi_sci_flags |= ACPI_MADT_TRIGGER_LEVEL;
		}
		/* Set PIC-mode SCI trigger type */
		acpi_pic_sci_set_trigger(acpi_gbl_FADT.sci_interrupt,
					 (acpi_sci_flags & ACPI_MADT_TRIGGER_MASK) >> 2);
	} else {
		/*
		 * now that acpi_gbl_FADT is initialized,
		 * update it with result from INT_SRC_OVR parsing
		 */
		acpi_gbl_FADT.sci_interrupt = acpi_sci_override_gsi;
	}
#endif

	status =
	    acpi_enable_subsystem(~
				  (ACPI_NO_HARDWARE_INIT |
				   ACPI_NO_ACPI_ENABLE));
	if (ACPI_FAILURE(status)) {
		printk(KERN_ERR PREFIX "Unable to enable ACPI\n");
		goto error0;
	}

	return;

      error0:
	disable_acpi();
	return;
	return;

 error0:
	disable_acpi();
}

/**
 * acpi_subsystem_init - Finalize the early initialization of ACPI.
 *
 * Switch over the platform to the ACPI mode (if possible), initialize the
 * handling of ACPI events, install the interrupt and global lock handlers.
 *
 * Doing this too early is generally unsafe, but at the same time it needs to be
 * done before all things that really depend on ACPI.  The right spot appears to
 * be before finalizing the EFI initialization.
 */
void __init acpi_subsystem_init(void)
{
	acpi_status status;

	if (acpi_disabled)
		return;

	status = acpi_enable_subsystem(~ACPI_NO_ACPI_ENABLE);
	if (ACPI_FAILURE(status)) {
		printk(KERN_ERR PREFIX "Unable to enable ACPI\n");
		disable_acpi();
	} else {
		/*
		 * If the system is using ACPI then we can be reasonably
		 * confident that any regulators are managed by the firmware
		 * so tell the regulator core it has everything it needs to
		 * know.
		 */
		regulator_has_full_constraints();
	}
}

static int __init acpi_bus_init(void)
{
	int result = 0;
	acpi_status status = AE_OK;
	extern acpi_status acpi_os_initialize1(void);


	status = acpi_os_initialize1();

	status =
	    acpi_enable_subsystem(ACPI_NO_HARDWARE_INIT | ACPI_NO_ACPI_ENABLE);
	int result;
	acpi_status status;

	acpi_os_initialize1();

	status = acpi_enable_subsystem(ACPI_NO_ACPI_ENABLE);
	if (ACPI_FAILURE(status)) {
		printk(KERN_ERR PREFIX
		       "Unable to start the ACPI Interpreter\n");
		goto error1;
	}

	if (ACPI_FAILURE(status)) {
		printk(KERN_ERR PREFIX
		       "Unable to initialize ACPI OS objects\n");
		goto error1;
	}
#ifdef CONFIG_ACPI_EC
	/*
	 * ACPI 2.0 requires the EC driver to be loaded and work before
	 * the EC device is found in the namespace (i.e. before acpi_initialize_objects()
	 * is called).
	 *
	 * This is accomplished by looking for the ECDT table, and getting
	 * the EC parameters out of that.
	 */
	status = acpi_ec_ecdt_probe();
	/* Ignore result. Not having an ECDT is not fatal. */
#endif

	status = acpi_initialize_objects(ACPI_FULL_INITIALIZATION);
	if (ACPI_FAILURE(status)) {
		printk(KERN_ERR PREFIX "Unable to initialize ACPI objects\n");
		goto error1;
	}

	/* Set capability bits for _OSC under processor scope */
	acpi_early_processor_osc();

	/*
	 * _OSC method may exist in module level code,
	 * so it must be run after ACPI_FULL_INITIALIZATION
	 */
	acpi_bus_osc_support();

	/*
	 * _PDC control method may load dynamic SSDT tables,
	 * and we need to install the table handler before that.
	 */
	acpi_sysfs_init();

	acpi_early_processor_set_pdc();

	/*
	 * Maybe EC region is required at bus_scan/acpi_get_devices. So it
	 * is necessary to enable it as early as possible.
	 */
	acpi_boot_ec_enable();

	printk(KERN_INFO PREFIX "Interpreter enabled\n");

	/* Initialize sleep structures */
	acpi_sleep_init();

	/*
	 * Get the system interrupt model and evaluate \_PIC.
	 */
	result = acpi_bus_init_irq();
	if (result)
		goto error1;

	/*
	 * Register the for all standard device notifications.
	 */
	status =
	    acpi_install_notify_handler(ACPI_ROOT_OBJECT, ACPI_SYSTEM_NOTIFY,
					&acpi_bus_notify, NULL);
	if (ACPI_FAILURE(status)) {
		printk(KERN_ERR PREFIX
		       "Unable to register for device notifications\n");
		goto error1;
	}

	/*
	 * Create the top ACPI proc directory
	 */
	acpi_root_dir = proc_mkdir(ACPI_BUS_FILE_ROOT, NULL);

	return 0;
	result = bus_register(&acpi_bus_type);
	if (!result)
		return 0;

	/* Mimic structured exception handling */
      error1:
	acpi_terminate();
	return -ENODEV;
}

struct kobject *acpi_kobj;

static int __init acpi_init(void)
{
	int result = 0;

EXPORT_SYMBOL_GPL(acpi_kobj);

static int __init acpi_init(void)
{
	int result;

	if (acpi_disabled) {
		printk(KERN_INFO PREFIX "Interpreter disabled.\n");
		return -ENODEV;
	}

	acpi_kobj = kobject_create_and_add("acpi", firmware_kobj);
	if (!acpi_kobj) {
		printk(KERN_WARNING "%s: kset create error\n", __func__);
		acpi_kobj = NULL;
	}

	result = acpi_bus_init();

	if (!result) {
		pci_mmcfg_late_init();
		if (!(pm_flags & PM_APM))
			pm_flags |= PM_ACPI;
		else {
			printk(KERN_INFO PREFIX
			       "APM is already active, exiting\n");
			disable_acpi();
			result = -ENODEV;
		}
	} else
		disable_acpi();

	return result;
	init_acpi_device_notify();
	result = acpi_bus_init();
	if (result) {
		kobject_put(acpi_kobj);
		disable_acpi();
		return result;
	}

	pci_mmcfg_late_init();
	acpi_scan_init();
	acpi_ec_init();
	acpi_debugfs_init();
	acpi_sleep_proc_init();
	acpi_wakeup_device_init();
	return 0;
}

subsys_initcall(acpi_init);
