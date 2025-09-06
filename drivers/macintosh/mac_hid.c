/*
 * drivers/macintosh/mac_hid.c
 *
 * HID support stuff for Macintosh computers.
 *
 * Copyright (C) 2000 Franz Sirl.
 *
 * This file will soon be removed in favor of an uinput userspace tool.
 */

#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/sysctl.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/kbd_kern.h>


static struct input_dev *emumousebtn;
static int emumousebtn_input_register(void);
static int mouse_emulate_buttons;
static int mouse_button2_keycode = KEY_RIGHTCTRL;	/* right control key */
static int mouse_button3_keycode = KEY_RIGHTALT;	/* right option key */
static int mouse_last_keycode;

#if defined(CONFIG_SYSCTL)
/* file(s) in /proc/sys/dev/mac_hid */
static ctl_table mac_hid_files[] = {
	{
		.ctl_name	= DEV_MAC_HID_MOUSE_BUTTON_EMULATION,
#include <linux/slab.h>

MODULE_LICENSE("GPL");

static int mouse_emulate_buttons;
static int mouse_button2_keycode = KEY_RIGHTCTRL;	/* right control key */
static int mouse_button3_keycode = KEY_RIGHTALT;	/* right option key */

static struct input_dev *mac_hid_emumouse_dev;

static DEFINE_MUTEX(mac_hid_emumouse_mutex);

static int mac_hid_create_emumouse(void)
{
	static struct lock_class_key mac_hid_emumouse_dev_event_class;
	static struct lock_class_key mac_hid_emumouse_dev_mutex_class;
	int err;

	mac_hid_emumouse_dev = input_allocate_device();
	if (!mac_hid_emumouse_dev)
		return -ENOMEM;

	lockdep_set_class(&mac_hid_emumouse_dev->event_lock,
			  &mac_hid_emumouse_dev_event_class);
	lockdep_set_class(&mac_hid_emumouse_dev->mutex,
			  &mac_hid_emumouse_dev_mutex_class);

	mac_hid_emumouse_dev->name = "Macintosh mouse button emulation";
	mac_hid_emumouse_dev->id.bustype = BUS_ADB;
	mac_hid_emumouse_dev->id.vendor = 0x0001;
	mac_hid_emumouse_dev->id.product = 0x0001;
	mac_hid_emumouse_dev->id.version = 0x0100;

	mac_hid_emumouse_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REL);
	mac_hid_emumouse_dev->keybit[BIT_WORD(BTN_MOUSE)] =
		BIT_MASK(BTN_LEFT) | BIT_MASK(BTN_MIDDLE) | BIT_MASK(BTN_RIGHT);
	mac_hid_emumouse_dev->relbit[0] = BIT_MASK(REL_X) | BIT_MASK(REL_Y);

	err = input_register_device(mac_hid_emumouse_dev);
	if (err) {
		input_free_device(mac_hid_emumouse_dev);
		mac_hid_emumouse_dev = NULL;
		return err;
	}

	return 0;
}

static void mac_hid_destroy_emumouse(void)
{
	input_unregister_device(mac_hid_emumouse_dev);
	mac_hid_emumouse_dev = NULL;
}

static bool mac_hid_emumouse_filter(struct input_handle *handle,
				    unsigned int type, unsigned int code,
				    int value)
{
	unsigned int btn;

	if (type != EV_KEY)
		return false;

	if (code == mouse_button2_keycode)
		btn = BTN_MIDDLE;
	else if (code == mouse_button3_keycode)
		btn = BTN_RIGHT;
	else
		return false;

	input_report_key(mac_hid_emumouse_dev, btn, value);
	input_sync(mac_hid_emumouse_dev);

	return true;
}

static int mac_hid_emumouse_connect(struct input_handler *handler,
				    struct input_dev *dev,
				    const struct input_device_id *id)
{
	struct input_handle *handle;
	int error;

	/* Don't bind to ourselves */
	if (dev == mac_hid_emumouse_dev)
		return -ENODEV;

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "mac-button-emul";

	error = input_register_handle(handle);
	if (error) {
		printk(KERN_ERR
			"mac_hid: Failed to register button emulation handle, "
			"error %d\n", error);
		goto err_free;
	}

	error = input_open_device(handle);
	if (error) {
		printk(KERN_ERR
			"mac_hid: Failed to open input device, error %d\n",
			error);
		goto err_unregister;
	}

	return 0;

 err_unregister:
	input_unregister_handle(handle);
 err_free:
	kfree(handle);
	return error;
}

static void mac_hid_emumouse_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id mac_hid_emumouse_ids[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_KEY) },
	},
	{ },
};

MODULE_DEVICE_TABLE(input, mac_hid_emumouse_ids);

static struct input_handler mac_hid_emumouse_handler = {
	.filter		= mac_hid_emumouse_filter,
	.connect	= mac_hid_emumouse_connect,
	.disconnect	= mac_hid_emumouse_disconnect,
	.name		= "mac-button-emul",
	.id_table	= mac_hid_emumouse_ids,
};

static int mac_hid_start_emulation(void)
{
	int err;

	err = mac_hid_create_emumouse();
	if (err)
		return err;

	err = input_register_handler(&mac_hid_emumouse_handler);
	if (err) {
		mac_hid_destroy_emumouse();
		return err;
	}

	return 0;
}

static void mac_hid_stop_emulation(void)
{
	input_unregister_handler(&mac_hid_emumouse_handler);
	mac_hid_destroy_emumouse();
}

static int mac_hid_toggle_emumouse(struct ctl_table *table, int write,
				   void __user *buffer, size_t *lenp,
				   loff_t *ppos)
{
	int *valp = table->data;
	int old_val = *valp;
	int rc;

	rc = mutex_lock_killable(&mac_hid_emumouse_mutex);
	if (rc)
		return rc;

	rc = proc_dointvec(table, write, buffer, lenp, ppos);

	if (rc == 0 && write && *valp != old_val) {
		if (*valp == 1)
			rc = mac_hid_start_emulation();
		else if (*valp == 0)
			mac_hid_stop_emulation();
		else
			rc = -EINVAL;
	}

	/* Restore the old value in case of error */
	if (rc)
		*valp = old_val;

	mutex_unlock(&mac_hid_emumouse_mutex);

	return rc;
}

/* file(s) in /proc/sys/dev/mac_hid */
static struct ctl_table mac_hid_files[] = {
	{
		.procname	= "mouse_button_emulation",
		.data		= &mouse_emulate_buttons,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= DEV_MAC_HID_MOUSE_BUTTON2_KEYCODE,
		.proc_handler	= mac_hid_toggle_emumouse,
	},
	{
		.procname	= "mouse_button2_keycode",
		.data		= &mouse_button2_keycode,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= DEV_MAC_HID_MOUSE_BUTTON3_KEYCODE,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "mouse_button3_keycode",
		.data		= &mouse_button3_keycode,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{ .ctl_name = 0 }
};

/* dir in /proc/sys/dev */
static ctl_table mac_hid_dir[] = {
	{
		.ctl_name	= DEV_MAC_HID,
		.proc_handler	= proc_dointvec,
	},
	{ }
};

/* dir in /proc/sys/dev */
static struct ctl_table mac_hid_dir[] = {
	{
		.procname	= "mac_hid",
		.maxlen		= 0,
		.mode		= 0555,
		.child		= mac_hid_files,
	},
	{ .ctl_name = 0 }
};

/* /proc/sys/dev itself, in case that is not there yet */
static ctl_table mac_hid_root_dir[] = {
	{
		.ctl_name	= CTL_DEV,
	{ }
};

/* /proc/sys/dev itself, in case that is not there yet */
static struct ctl_table mac_hid_root_dir[] = {
	{
		.procname	= "dev",
		.maxlen		= 0,
		.mode		= 0555,
		.child		= mac_hid_dir,
	},
	{ .ctl_name = 0 }
	{ }
};

static struct ctl_table_header *mac_hid_sysctl_header;

#endif /* endif CONFIG_SYSCTL */

int mac_hid_mouse_emulate_buttons(int caller, unsigned int keycode, int down)
{
	switch (caller) {
	case 1:
		/* Called from keyboard.c */
		if (mouse_emulate_buttons
		    && (keycode == mouse_button2_keycode
			|| keycode == mouse_button3_keycode)) {
			if (mouse_emulate_buttons == 1) {
				input_report_key(emumousebtn,
						 keycode == mouse_button2_keycode ? BTN_MIDDLE : BTN_RIGHT,
						 down);
				input_sync(emumousebtn);
				return 1;
			}
			mouse_last_keycode = down ? keycode : 0;
		}
		break;
	}
	return 0;
}

static struct lock_class_key emumousebtn_event_class;
static struct lock_class_key emumousebtn_mutex_class;

static int emumousebtn_input_register(void)
{
	int ret;

	emumousebtn = input_allocate_device();
	if (!emumousebtn)
		return -ENOMEM;

	lockdep_set_class(&emumousebtn->event_lock, &emumousebtn_event_class);
	lockdep_set_class(&emumousebtn->mutex, &emumousebtn_mutex_class);

	emumousebtn->name = "Macintosh mouse button emulation";
	emumousebtn->id.bustype = BUS_ADB;
	emumousebtn->id.vendor = 0x0001;
	emumousebtn->id.product = 0x0001;
	emumousebtn->id.version = 0x0100;

	emumousebtn->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REL);
	emumousebtn->keybit[BIT_WORD(BTN_MOUSE)] = BIT_MASK(BTN_LEFT) |
		BIT_MASK(BTN_MIDDLE) | BIT_MASK(BTN_RIGHT);
	emumousebtn->relbit[0] = BIT_MASK(REL_X) | BIT_MASK(REL_Y);

	ret = input_register_device(emumousebtn);
	if (ret)
		input_free_device(emumousebtn);

	return ret;
}

static int __init mac_hid_init(void)
{
	int err;

	err = emumousebtn_input_register();
	if (err)
		return err;

#if defined(CONFIG_SYSCTL)
	mac_hid_sysctl_header = register_sysctl_table(mac_hid_root_dir);
#endif /* CONFIG_SYSCTL */

	return 0;
}

device_initcall(mac_hid_init);
static int __init mac_hid_init(void)
{
	mac_hid_sysctl_header = register_sysctl_table(mac_hid_root_dir);
	if (!mac_hid_sysctl_header)
		return -ENOMEM;

	return 0;
}
module_init(mac_hid_init);

static void __exit mac_hid_exit(void)
{
	unregister_sysctl_table(mac_hid_sysctl_header);

	if (mouse_emulate_buttons)
		mac_hid_stop_emulation();
}
module_exit(mac_hid_exit);
