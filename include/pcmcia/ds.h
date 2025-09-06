/*
 * ds.h -- 16-bit PCMCIA core support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * The initial developer of the original code is David A. Hinds
 * <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
 * are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.
 *
 * (C) 1999		David A. Hinds
 * (C) 2003 - 2004	Dominik Brodowski
 * (C) 2003 - 2008	Dominik Brodowski
 */

#ifndef _LINUX_DS_H
#define _LINUX_DS_H

#ifdef __KERNEL__
#include <linux/mod_devicetable.h>
#endif

#include <pcmcia/cs_types.h>
#include <pcmcia/device_id.h>

typedef struct tuple_parse_t {
    tuple_t		tuple;
    cisdata_t		data[255];
    cisparse_t		parse;
} tuple_parse_t;

typedef struct win_info_t {
    window_handle_t	handle;
    win_req_t		window;
    memreq_t		map;
} win_info_t;
    
typedef struct bind_info_t {
    dev_info_t		dev_info;
    u_char		function;
    struct pcmcia_device *instance;
    char		name[DEV_NAME_LEN];
    u_short		major, minor;
    void		*next;
} bind_info_t;

typedef struct mtd_info_t {
    dev_info_t		dev_info;
    u_int		Attributes;
    u_int		CardOffset;
} mtd_info_t;

typedef struct region_info_t {
    u_int		Attributes;
    u_int		CardOffset;
    u_int		RegionSize;
    u_int		AccessSpeed;
    u_int		BlockSize;
    u_int		PartMultiple;
    u_char		JedecMfr, JedecInfo;
    memory_handle_t	next;
} region_info_t;
#define REGION_TYPE		0x0001
#define REGION_TYPE_CM		0x0000
#define REGION_TYPE_AM		0x0001
#define REGION_PREFETCH		0x0008
#define REGION_CACHEABLE	0x0010
#define REGION_BAR_MASK		0xe000
#define REGION_BAR_SHIFT	13

typedef union ds_ioctl_arg_t {
    adjust_t		adjust;
    config_info_t	config;
    tuple_t		tuple;
    tuple_parse_t	tuple_parse;
    client_req_t	client_req;
    cs_status_t		status;
    conf_reg_t		conf_reg;
    cisinfo_t		cisinfo;
    region_info_t	region;
    bind_info_t		bind_info;
    mtd_info_t		mtd_info;
    win_info_t		win_info;
    cisdump_t		cisdump;
} ds_ioctl_arg_t;

#define DS_ADJUST_RESOURCE_INFO		_IOWR('d', 2, adjust_t)
#define DS_GET_CONFIGURATION_INFO	_IOWR('d', 3, config_info_t)
#define DS_GET_FIRST_TUPLE		_IOWR('d', 4, tuple_t)
#define DS_GET_NEXT_TUPLE		_IOWR('d', 5, tuple_t)
#define DS_GET_TUPLE_DATA		_IOWR('d', 6, tuple_parse_t)
#define DS_PARSE_TUPLE			_IOWR('d', 7, tuple_parse_t)
#define DS_RESET_CARD			_IO  ('d', 8)
#define DS_GET_STATUS			_IOWR('d', 9, cs_status_t)
#define DS_ACCESS_CONFIGURATION_REGISTER _IOWR('d', 10, conf_reg_t)
#define DS_VALIDATE_CIS			_IOR ('d', 11, cisinfo_t)
#define DS_SUSPEND_CARD			_IO  ('d', 12)
#define DS_RESUME_CARD			_IO  ('d', 13)
#define DS_EJECT_CARD			_IO  ('d', 14)
#define DS_INSERT_CARD			_IO  ('d', 15)
#define DS_GET_FIRST_REGION		_IOWR('d', 16, region_info_t)
#define DS_GET_NEXT_REGION		_IOWR('d', 17, region_info_t)
#define DS_REPLACE_CIS			_IOWR('d', 18, cisdump_t)
#define DS_GET_FIRST_WINDOW		_IOR ('d', 19, win_info_t)
#define DS_GET_NEXT_WINDOW		_IOWR('d', 20, win_info_t)
#define DS_GET_MEM_PAGE			_IOWR('d', 21, win_info_t)

#define DS_BIND_REQUEST			_IOWR('d', 60, bind_info_t)
#define DS_GET_DEVICE_INFO		_IOWR('d', 61, bind_info_t) 
#define DS_GET_NEXT_DEVICE		_IOWR('d', 62, bind_info_t) 
#define DS_UNBIND_REQUEST		_IOW ('d', 63, bind_info_t)
#define DS_BIND_MTD			_IOWR('d', 64, mtd_info_t)

#ifdef __KERNEL__
#include <linux/device.h>
#include <pcmcia/ss.h>

typedef struct dev_node_t {
    char		dev_name[DEV_NAME_LEN];
    u_short		major, minor;
    struct dev_node_t	*next;
} dev_node_t;


struct pcmcia_socket;
struct config_t;

struct pcmcia_dynids {
	spinlock_t		lock;
#include <pcmcia/device_id.h>

#ifdef __KERNEL__
#include <linux/device.h>
#include <linux/interrupt.h>
#include <pcmcia/ss.h>
#include <linux/atomic.h>


/*
 * PCMCIA device drivers (16-bit cards only; 32-bit cards require CardBus
 * a.k.a. PCI drivers
 */
struct pcmcia_socket;
struct pcmcia_device;
struct config_t;
struct net_device;

/* dynamic device IDs for PCMCIA device drivers. See
 * Documentation/pcmcia/driver.txt for details.
*/
struct pcmcia_dynids {
	struct mutex		lock;
	struct list_head	list;
};

struct pcmcia_driver {
	const char		*name;

	int (*probe)		(struct pcmcia_device *dev);
	void (*remove)		(struct pcmcia_device *dev);

	int (*suspend)		(struct pcmcia_device *dev);
	int (*resume)		(struct pcmcia_device *dev);

	struct module		*owner;
	struct pcmcia_device_id	*id_table;
	const struct pcmcia_device_id	*id_table;
	struct device_driver	drv;
	struct pcmcia_dynids	dynids;
};

/* driver registration */
int pcmcia_register_driver(struct pcmcia_driver *driver);
void pcmcia_unregister_driver(struct pcmcia_driver *driver);

/**
 * module_pcmcia_driver() - Helper macro for registering a pcmcia driver
 * @__pcmcia_driver: pcmcia_driver struct
 *
 * Helper macro for pcmcia drivers which do not do anything special in module
 * init/exit. This eliminates a lot of boilerplate. Each module may only use
 * this macro once, and calling it replaces module_init() and module_exit().
 */
#define module_pcmcia_driver(__pcmcia_driver) \
	module_driver(__pcmcia_driver, pcmcia_register_driver, \
			pcmcia_unregister_driver)

/* for struct resource * array embedded in struct pcmcia_device */
enum {
	PCMCIA_IOPORT_0,
	PCMCIA_IOPORT_1,
	PCMCIA_IOMEM_0,
	PCMCIA_IOMEM_1,
	PCMCIA_IOMEM_2,
	PCMCIA_IOMEM_3,
	PCMCIA_NUM_RESOURCES,
};

struct pcmcia_device {
	/* the socket and the device_no [for multifunction devices]
	   uniquely define a pcmcia_device */
	struct pcmcia_socket	*socket;

	char			*devname;

	u8			device_no;

	/* the hardware "function" device; certain subdevices can
	 * share one hardware "function" device. */
	u8			func;
	struct config_t*	function_config;

	struct list_head	socket_device_list;

	/* deprecated, will be cleaned up soon */
	dev_node_t		*dev_node;
	u_int			open;
	io_req_t		io;
	irq_req_t		irq;
	config_req_t		conf;
	window_handle_t		win;

	/* Is the device suspended, or in the process of
	 * being removed? */
	u16			suspended:1;
	u16			_removed:1;
	struct config_t		*function_config;

	struct list_head	socket_device_list;

	/* device setup */
	unsigned int		irq;
	struct resource		*resource[PCMCIA_NUM_RESOURCES];
	resource_size_t		card_addr;	/* for the 1st IOMEM resource */
	unsigned int		vpp;

	unsigned int		config_flags;	/* CONF_ENABLE_ flags below */
	unsigned int		config_base;
	unsigned int		config_index;
	unsigned int		config_regs;	/* PRESENT_ flags below */
	unsigned int		io_lines;	/* number of I/O lines */

	/* Is the device suspended? */
	u16			suspended:1;

	/* Flags whether io, irq, win configurations were
	 * requested, and whether the configuration is "locked" */
	u16			_irq:1;
	u16			_io:1;
	u16			_win:4;
	u16			_locked:1;

	/* Flag whether a "fuzzy" func_id based match is
	 * allowed. */
	u16			allow_func_id_match:1;

	/* information about this device */
	u16			has_manf_id:1;
	u16			has_card_id:1;
	u16			has_func_id:1;

	u16			reserved:3;
	u16			reserved:4;

	u8			func_id;
	u16			manf_id;
	u16			card_id;

	char *			prod_id[4];
	char			*prod_id[4];

	u64			dma_mask;
	struct device		dev;

#ifdef CONFIG_PCMCIA_IOCTL
	/* device driver wanted by cardmgr */
	struct pcmcia_driver *	cardmgr;
#endif

	/* data private to drivers */
	void			*priv;
	/* data private to drivers */
	void			*priv;
	unsigned int		open;
};

#define to_pcmcia_dev(n) container_of(n, struct pcmcia_device, dev)
#define to_pcmcia_drv(n) container_of(n, struct pcmcia_driver, drv)

#define handle_to_dev(handle) (handle->dev)

/* error reporting */
void cs_error(struct pcmcia_device *handle, int func, int ret);

#endif /* __KERNEL__ */

/*
 * CIS access.
 *
 * Please use the following functions to access CIS tuples:
 * - pcmcia_get_tuple()
 * - pcmcia_loop_tuple()
 * - pcmcia_get_mac_from_cis()
 *
 * To parse a tuple_t, pcmcia_parse_tuple() exists. Its interface
 * might change in future.
 */

/* get the very first CIS entry of type @code. Note that buf is pointer
 * to u8 *buf; and that you need to kfree(buf) afterwards. */
size_t pcmcia_get_tuple(struct pcmcia_device *p_dev, cisdata_t code,
			u8 **buf);

/* loop over CIS entries */
int pcmcia_loop_tuple(struct pcmcia_device *p_dev, cisdata_t code,
		      int (*loop_tuple) (struct pcmcia_device *p_dev,
					 tuple_t *tuple,
					 void *priv_data),
		      void *priv_data);

/* get the MAC address from CISTPL_FUNCE */
int pcmcia_get_mac_from_cis(struct pcmcia_device *p_dev,
			    struct net_device *dev);


/* parse a tuple_t */
int pcmcia_parse_tuple(tuple_t *tuple, cisparse_t *parse);

/* loop CIS entries for valid configuration */
int pcmcia_loop_config(struct pcmcia_device *p_dev,
		       int	(*conf_check)	(struct pcmcia_device *p_dev,
						 void *priv_data),
		       void *priv_data);

/* is the device still there? */
struct pcmcia_device *pcmcia_dev_present(struct pcmcia_device *p_dev);

/* low-level interface reset */
int pcmcia_reset_card(struct pcmcia_socket *skt);

/* CIS config */
int pcmcia_read_config_byte(struct pcmcia_device *p_dev, off_t where, u8 *val);
int pcmcia_write_config_byte(struct pcmcia_device *p_dev, off_t where, u8 val);

/* device configuration */
int pcmcia_request_io(struct pcmcia_device *p_dev);

int __must_check
__pcmcia_request_exclusive_irq(struct pcmcia_device *p_dev,
				irq_handler_t handler);
static inline __must_check __deprecated int
pcmcia_request_exclusive_irq(struct pcmcia_device *p_dev,
				irq_handler_t handler)
{
	return __pcmcia_request_exclusive_irq(p_dev, handler);
}

int __must_check pcmcia_request_irq(struct pcmcia_device *p_dev,
				irq_handler_t handler);

int pcmcia_enable_device(struct pcmcia_device *p_dev);

int pcmcia_request_window(struct pcmcia_device *p_dev, struct resource *res,
			unsigned int speed);
int pcmcia_release_window(struct pcmcia_device *p_dev, struct resource *res);
int pcmcia_map_mem_page(struct pcmcia_device *p_dev, struct resource *res,
			unsigned int offset);

int pcmcia_fixup_vpp(struct pcmcia_device *p_dev, unsigned char new_vpp);
int pcmcia_fixup_iowidth(struct pcmcia_device *p_dev);

void pcmcia_disable_device(struct pcmcia_device *p_dev);

/* IO ports */
#define IO_DATA_PATH_WIDTH	0x18
#define IO_DATA_PATH_WIDTH_8	0x00
#define IO_DATA_PATH_WIDTH_16	0x08
#define IO_DATA_PATH_WIDTH_AUTO	0x10

/* IO memory */
#define WIN_MEMORY_TYPE_CM	0x00 /* default */
#define WIN_MEMORY_TYPE_AM	0x20 /* MAP_ATTRIB */
#define WIN_DATA_WIDTH_8	0x00 /* default */
#define WIN_DATA_WIDTH_16	0x02 /* MAP_16BIT */
#define WIN_ENABLE		0x01 /* MAP_ACTIVE */
#define WIN_USE_WAIT		0x40 /* MAP_USE_WAIT */

#define WIN_FLAGS_MAP		0x63 /* MAP_ATTRIB | MAP_16BIT | MAP_ACTIVE |
					MAP_USE_WAIT */
#define WIN_FLAGS_REQ		0x1c /* mapping to socket->win[i]:
					0x04 -> 0
					0x08 -> 1
					0x0c -> 2
					0x10 -> 3 */

/* config_reg{ister}s present for this PCMCIA device */
#define PRESENT_OPTION		0x001
#define PRESENT_STATUS		0x002
#define PRESENT_PIN_REPLACE	0x004
#define PRESENT_COPY		0x008
#define PRESENT_EXT_STATUS	0x010
#define PRESENT_IOBASE_0	0x020
#define PRESENT_IOBASE_1	0x040
#define PRESENT_IOBASE_2	0x080
#define PRESENT_IOBASE_3	0x100
#define PRESENT_IOSIZE		0x200

/* flags to be passed to pcmcia_enable_device() */
#define CONF_ENABLE_IRQ         0x0001
#define CONF_ENABLE_SPKR        0x0002
#define CONF_ENABLE_PULSE_IRQ   0x0004
#define CONF_ENABLE_ESR         0x0008
#define CONF_ENABLE_IOCARD	0x0010 /* auto-enabled if IO resources or IRQ
					* (CONF_ENABLE_IRQ) in use */
#define CONF_ENABLE_ZVCARD	0x0020

/* flags used by pcmcia_loop_config() autoconfiguration */
#define CONF_AUTO_CHECK_VCC	0x0100 /* check for matching Vcc? */
#define CONF_AUTO_SET_VPP	0x0200 /* set Vpp? */
#define CONF_AUTO_AUDIO		0x0400 /* enable audio line? */
#define CONF_AUTO_SET_IO	0x0800 /* set ->resource[0,1] */
#define CONF_AUTO_SET_IOMEM	0x1000 /* set ->resource[2] */

#endif /* __KERNEL__ */

#endif /* _LINUX_DS_H */
