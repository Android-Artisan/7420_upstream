
/**
 * struct bus_type_private - structure to hold the private to the driver core portions of the bus_type structure.
 *
 * @subsys - the struct kset that defines this bus.  This is the main kobject
 * @drivers_kset - the list of drivers associated with this bus
 * @devices_kset - the list of devices associated with this bus
 * @klist_devices - the klist to iterate over the @devices_kset
 * @klist_drivers - the klist to iterate over the @drivers_kset
 * @bus_notifier - the bus notifier list for anything that cares about things
 * on this bus.
 * @bus - pointer back to the struct bus_type that this structure is associated
 * with.
 *
 * This structure is the one that is the actual kobject allowing struct
 * bus_type to be statically allocated safely.  Nothing outside of the driver
 * core should ever touch these fields.
 */
struct bus_type_private {
	struct kset subsys;
	struct kset *drivers_kset;
	struct kset *devices_kset;
#include <linux/notifier.h>

/**
 * struct subsys_private - structure to hold the private to the driver core portions of the bus_type/class structure.
 *
 * @subsys - the struct kset that defines this subsystem
 * @devices_kset - the subsystem's 'devices' directory
 * @interfaces - list of subsystem interfaces associated
 * @mutex - protect the devices, and interfaces lists.
 *
 * @drivers_kset - the list of drivers associated
 * @klist_devices - the klist to iterate over the @devices_kset
 * @klist_drivers - the klist to iterate over the @drivers_kset
 * @bus_notifier - the bus notifier list for anything that cares about things
 *                 on this bus.
 * @bus - pointer back to the struct bus_type that this structure is associated
 *        with.
 *
 * @glue_dirs - "glue" directory to put in-between the parent device to
 *              avoid namespace conflicts
 * @class - pointer back to the struct class that this structure is associated
 *          with.
 *
 * This structure is the one that is the actual kobject allowing struct
 * bus_type/class to be statically allocated safely.  Nothing outside of the
 * driver core should ever touch these fields.
 */
struct subsys_private {
	struct kset subsys;
	struct kset *devices_kset;
	struct list_head interfaces;
	struct mutex mutex;

	struct kset *drivers_kset;
	struct klist klist_devices;
	struct klist klist_drivers;
	struct blocking_notifier_head bus_notifier;
	unsigned int drivers_autoprobe:1;
	struct bus_type *bus;
};

	struct kset glue_dirs;
	struct class *class;
};
#define to_subsys_private(obj) container_of(obj, struct subsys_private, subsys.kobj)

struct driver_private {
	struct kobject kobj;
	struct klist klist_devices;
	struct klist_node knode_bus;
	struct module_kobject *mkobj;
	struct device_driver *driver;
};
#define to_driver(obj) container_of(obj, struct driver_private, kobj)


/**
 * struct class_private - structure to hold the private to the driver core portions of the class structure.
 *
 * @class_subsys - the struct kset that defines this class.  This is the main kobject
 * @class_devices - list of devices associated with this class
 * @class_interfaces - list of class_interfaces associated with this class
 * @class_dirs - "glue" directory for virtual devices associated with this class
 * @class_mutex - mutex to protect the children, devices, and interfaces lists.
 * @class - pointer back to the struct class that this structure is associated
 * with.
 *
 * This structure is the one that is the actual kobject allowing struct
 * class to be statically allocated safely.  Nothing outside of the driver
 * core should ever touch these fields.
 */
struct class_private {
	struct kset class_subsys;
	struct list_head class_devices;
	struct list_head class_interfaces;
	struct kset class_dirs;
	struct mutex class_mutex;
	struct class *class;
};
#define to_class(obj)	\
	container_of(obj, struct class_private, class_subsys.kobj)
/**
 * struct device_private - structure to hold the private to the driver core portions of the device structure.
 *
 * @klist_children - klist containing all children of this device
 * @knode_parent - node in sibling list
 * @knode_driver - node in driver list
 * @knode_bus - node in bus list
 * @deferred_probe - entry in deferred_probe_list which is used to retry the
 *	binding of drivers which were unable to get all the resources needed by
 *	the device; typically because it depends on another driver getting
 *	probed first.
 * @device - pointer back to the struct device that this structure is
 * associated with.
 *
 * Nothing outside of the driver core should ever touch these fields.
 */
struct device_private {
	struct klist klist_children;
	struct klist_node knode_parent;
	struct klist_node knode_driver;
	struct klist_node knode_bus;
	struct list_head deferred_probe;
	struct device *device;
};
#define to_device_private_parent(obj)	\
	container_of(obj, struct device_private, knode_parent)
#define to_device_private_driver(obj)	\
	container_of(obj, struct device_private, knode_driver)
#define to_device_private_bus(obj)	\
	container_of(obj, struct device_private, knode_bus)

extern int device_private_init(struct device *dev);

/* initialisation functions */
extern int devices_init(void);
extern int buses_init(void);
extern int classes_init(void);
extern int firmware_init(void);
#ifdef CONFIG_SYS_HYPERVISOR
extern int hypervisor_init(void);
#else
static inline int hypervisor_init(void) { return 0; }
#endif
extern int platform_bus_init(void);
extern int system_bus_init(void);
extern int cpu_dev_init(void);

extern int bus_add_device(struct device *dev);
extern void bus_attach_device(struct device *dev);
extern void cpu_dev_init(void);
extern void container_dev_init(void);

struct kobject *virtual_device_parent(struct device *dev);

extern int bus_add_device(struct device *dev);
extern void bus_probe_device(struct device *dev);
extern void bus_remove_device(struct device *dev);

extern int bus_add_driver(struct device_driver *drv);
extern void bus_remove_driver(struct device_driver *drv);

extern void driver_detach(struct device_driver *drv);
extern int driver_probe_device(struct device_driver *drv, struct device *dev);

extern void sysdev_shutdown(void);
extern int sysdev_suspend(pm_message_t state);
extern int sysdev_resume(void);
extern void driver_deferred_probe_del(struct device *dev);
static inline int driver_match_device(struct device_driver *drv,
				      struct device *dev)
{
	return drv->bus->match ? drv->bus->match(dev, drv) : 1;
}
extern bool driver_allows_async_probing(struct device_driver *drv);

extern int driver_add_groups(struct device_driver *drv,
			     const struct attribute_group **groups);
extern void driver_remove_groups(struct device_driver *drv,
				 const struct attribute_group **groups);

extern int device_add_groups(struct device *dev,
			     const struct attribute_group **groups);
extern void device_remove_groups(struct device *dev,
				 const struct attribute_group **groups);

extern char *make_class_name(const char *name, struct kobject *kobj);

extern int devres_release_all(struct device *dev);

extern struct kset *devices_kset;
/* /sys/devices directory */
extern struct kset *devices_kset;
extern void devices_kset_move_last(struct device *dev);

#if defined(CONFIG_MODULES) && defined(CONFIG_SYSFS)
extern void module_add_driver(struct module *mod, struct device_driver *drv);
extern void module_remove_driver(struct device_driver *drv);
#else
static inline void module_add_driver(struct module *mod,
				     struct device_driver *drv) { }
static inline void module_remove_driver(struct device_driver *drv) { }
#endif

#ifdef CONFIG_DEVTMPFS
extern int devtmpfs_init(void);
#else
static inline int devtmpfs_init(void) { return 0; }
#endif
