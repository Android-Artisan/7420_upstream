/*
 * USB Serial Converter Bus specific functions
 *
 * Copyright (C) 2002 Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License version
 *	2 as published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>

static int usb_serial_device_match(struct device *dev,
						struct device_driver *drv)
{
	struct usb_serial_driver *driver;
	const struct usb_serial_port *port;

	/*
	 * drivers are already assigned to ports in serial_probe so it's
	 * a simple check here.
	 */
	port = to_usb_serial_port(dev);
	if (!port)
		return 0;

	driver = to_usb_serial_driver(drv);

	if (driver == port->serial->type)
		return 1;

	return 0;
}

static ssize_t show_port_number(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct usb_serial_port *port = to_usb_serial_port(dev);

	return sprintf(buf, "%d\n", port->number - port->serial->minor);
}

static DEVICE_ATTR(port_number, S_IRUGO, show_port_number, NULL);

static int usb_serial_device_probe(struct device *dev)
{
	struct usb_serial_driver *driver;
	struct usb_serial_port *port;
	struct device *tty_dev;
	int retval = 0;
	int minor;

	port = to_usb_serial_port(dev);
	if (!port) {
		retval = -ENODEV;
		goto exit;
	}

	driver = port->serial->type;
	if (driver->port_probe) {
		if (!try_module_get(driver->driver.owner)) {
			dev_err(dev, "module get failed, exiting\n");
			retval = -EIO;
			goto exit;
		}
		retval = driver->port_probe(port);
		module_put(driver->driver.owner);
		if (retval)
			goto exit;
	}

	retval = device_create_file(dev, &dev_attr_port_number);
	if (retval)
		goto exit;

	minor = port->number;
	tty_register_device(usb_serial_tty_driver, minor, dev);
	if (!port)
		return -ENODEV;

	/* make sure suspend/resume doesn't race against port_probe */
	retval = usb_autopm_get_interface(port->serial->interface);
	if (retval)
		return retval;

	driver = port->serial->type;
	if (driver->port_probe) {
		retval = driver->port_probe(port);
		if (retval)
			goto err_autopm_put;
	}

	minor = port->minor;
	tty_dev = tty_register_device(usb_serial_tty_driver, minor, dev);
	if (IS_ERR(tty_dev)) {
		retval = PTR_ERR(tty_dev);
		goto err_port_remove;
	}

	usb_autopm_put_interface(port->serial->interface);

	dev_info(&port->serial->dev->dev,
		 "%s converter now attached to ttyUSB%d\n",
		 driver->description, minor);

exit:
	return 0;

err_port_remove:
	if (driver->port_remove)
		driver->port_remove(port);
err_autopm_put:
	usb_autopm_put_interface(port->serial->interface);

	return retval;
}

static int usb_serial_device_remove(struct device *dev)
{
	struct usb_serial_driver *driver;
	struct usb_serial_port *port;
	int retval = 0;
	int minor;
	int autopm_err;

	port = to_usb_serial_port(dev);
	if (!port)
		return -ENODEV;

	device_remove_file(&port->dev, &dev_attr_port_number);

	driver = port->serial->type;
	if (driver->port_remove) {
		if (!try_module_get(driver->driver.owner)) {
			dev_err(dev, "module get failed, exiting\n");
			retval = -EIO;
			goto exit;
		}
		retval = driver->port_remove(port);
		module_put(driver->driver.owner);
	}
exit:
	minor = port->number;
	tty_unregister_device(usb_serial_tty_driver, minor);
	dev_info(dev, "%s converter now disconnected from ttyUSB%d\n",
		 driver->description, minor);

	return retval;
}

#ifdef CONFIG_HOTPLUG
static ssize_t store_new_id(struct device_driver *driver,
			    const char *buf, size_t count)
{
	struct usb_serial_driver *usb_drv = to_usb_serial_driver(driver);
	ssize_t retval = usb_store_new_id(&usb_drv->dynids, driver, buf, count);

	if (retval >= 0 && usb_drv->usb_driver != NULL)
		retval = usb_store_new_id(&usb_drv->usb_driver->dynids,
	/*
	 * Make sure suspend/resume doesn't race against port_remove.
	 *
	 * Note that no further runtime PM callbacks will be made if
	 * autopm_get fails.
	 */
	autopm_err = usb_autopm_get_interface(port->serial->interface);

	minor = port->minor;
	tty_unregister_device(usb_serial_tty_driver, minor);

	driver = port->serial->type;
	if (driver->port_remove)
		retval = driver->port_remove(port);

	dev_info(dev, "%s converter now disconnected from ttyUSB%d\n",
		 driver->description, minor);

	if (!autopm_err)
		usb_autopm_put_interface(port->serial->interface);

	return retval;
}

static ssize_t new_id_store(struct device_driver *driver,
			    const char *buf, size_t count)
{
	struct usb_serial_driver *usb_drv = to_usb_serial_driver(driver);
	ssize_t retval = usb_store_new_id(&usb_drv->dynids, usb_drv->id_table,
					 driver, buf, count);

	if (retval >= 0 && usb_drv->usb_driver != NULL)
		retval = usb_store_new_id(&usb_drv->usb_driver->dynids,
					  usb_drv->usb_driver->id_table,
					  &usb_drv->usb_driver->drvwrap.driver,
					  buf, count);
	return retval;
}

static struct driver_attribute drv_attrs[] = {
	__ATTR(new_id, S_IWUSR, NULL, store_new_id),
	__ATTR_NULL,
};
static ssize_t new_id_show(struct device_driver *driver, char *buf)
{
	struct usb_serial_driver *usb_drv = to_usb_serial_driver(driver);

	return usb_show_dynids(&usb_drv->dynids, buf);
}
static DRIVER_ATTR_RW(new_id);

static struct attribute *usb_serial_drv_attrs[] = {
	&driver_attr_new_id.attr,
	NULL,
};
ATTRIBUTE_GROUPS(usb_serial_drv);

static void free_dynids(struct usb_serial_driver *drv)
{
	struct usb_dynid *dynid, *n;

	spin_lock(&drv->dynids.lock);
	list_for_each_entry_safe(dynid, n, &drv->dynids.list, node) {
		list_del(&dynid->node);
		kfree(dynid);
	}
	spin_unlock(&drv->dynids.lock);
}

#else
static struct driver_attribute drv_attrs[] = {
	__ATTR_NULL,
};
static inline void free_dynids(struct usb_serial_driver *drv)
{
}
#endif

struct bus_type usb_serial_bus_type = {
	.name =		"usb-serial",
	.match =	usb_serial_device_match,
	.probe =	usb_serial_device_probe,
	.remove =	usb_serial_device_remove,
	.drv_attrs = 	drv_attrs,
	.drv_groups = 	usb_serial_drv_groups,
};

int usb_serial_bus_register(struct usb_serial_driver *driver)
{
	int retval;

	driver->driver.bus = &usb_serial_bus_type;
	spin_lock_init(&driver->dynids.lock);
	INIT_LIST_HEAD(&driver->dynids.list);

	retval = driver_register(&driver->driver);

	return retval;
}

void usb_serial_bus_deregister(struct usb_serial_driver *driver)
{
	free_dynids(driver);
	driver_unregister(&driver->driver);
}

