/*
 * cs_internal.h
 * cs_internal.h -- definitions internal to the PCMCIA core modules
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
 * (C) 2003 - 2010	Dominik Brodowski
 *
 *
 * This file contains definitions _only_ needed by the PCMCIA core modules.
 * It must not be included by PCMCIA socket drivers or by PCMCIA device
 * drivers.
 */

#ifndef _LINUX_CS_INTERNAL_H
#define _LINUX_CS_INTERNAL_H

#include <linux/kref.h>

/* Flags in client state */
#define CLIENT_CONFIG_LOCKED	0x0001
#define CLIENT_IRQ_REQ		0x0002
#define CLIENT_IO_REQ		0x0004
#define CLIENT_UNBOUND		0x0008
#define CLIENT_STALE		0x0010
#define CLIENT_WIN_REQ(i)	(0x1<<(i))
#define CLIENT_CARDBUS		0x8000
#define CLIENT_WIN_REQ(i)	(0x1<<(i))

/* Flag to access all functions */
#define BIND_FN_ALL	0xff

/* Each card function gets one of these guys */
typedef struct config_t {
	struct kref	ref;
    u_int		state;
    u_int		Attributes;
    u_int		IntType;
    u_int		ConfigBase;
    u_char		Status, Pin, Copy, Option, ExtStatus;
    u_int		CardValues;
    io_req_t		io;
    struct {
	u_int		Attributes;
    } irq;
} config_t;

	unsigned int	state;

	struct resource io[MAX_IO_WIN]; /* io ports */
	struct resource mem[MAX_WIN];   /* mem areas */
} config_t;


struct cis_cache_entry {
	struct list_head	node;
	unsigned int		addr;
	unsigned int		len;
	unsigned int		attr;
	unsigned char		cache[0];
};

struct pccard_resource_ops {
	int	(*validate_mem)		(struct pcmcia_socket *s);
	int	(*find_io)		(struct pcmcia_socket *s,
					 unsigned int attr,
					 unsigned int *base,
					 unsigned int num,
					 unsigned int align,
					 struct resource **parent);
	struct resource* (*find_mem)	(unsigned long base, unsigned long num,
					 unsigned long align, int low,
					 struct pcmcia_socket *s);
	int	(*init)			(struct pcmcia_socket *s);
	void	(*exit)			(struct pcmcia_socket *s);
};

/* Flags in config state */
#define CONFIG_LOCKED		0x01
#define CONFIG_IRQ_REQ		0x02
#define CONFIG_IO_REQ		0x04

/* Flags in socket state */
#define SOCKET_PRESENT		0x0008
#define SOCKET_INUSE		0x0010
#define SOCKET_SUSPEND		0x0080
#define SOCKET_WIN_REQ(i)	(0x0100<<(i))
#define SOCKET_REGION_INFO	0x4000
#define SOCKET_CARDBUS		0x8000
#define SOCKET_CARDBUS_CONFIG	0x10000

static inline int cs_socket_get(struct pcmcia_socket *skt)
{
	int ret;

	WARN_ON(skt->state & SOCKET_INUSE);

	ret = try_module_get(skt->owner);
	if (ret)
		skt->state |= SOCKET_INUSE;
	return ret;
}

static inline void cs_socket_put(struct pcmcia_socket *skt)
{
	if (skt->state & SOCKET_INUSE) {
		skt->state &= ~SOCKET_INUSE;
		module_put(skt->owner);
	}
}

/* In cardbus.c */
int cb_alloc(struct pcmcia_socket *s);
void cb_free(struct pcmcia_socket *s);
int read_cb_mem(struct pcmcia_socket *s, int space, u_int addr, u_int len, void *ptr);

/* In cistpl.c */
int pcmcia_read_cis_mem(struct pcmcia_socket *s, int attr,
		 u_int addr, u_int len, void *ptr);
void pcmcia_write_cis_mem(struct pcmcia_socket *s, int attr,
		   u_int addr, u_int len, void *ptr);
void release_cis_mem(struct pcmcia_socket *s);
void destroy_cis_cache(struct pcmcia_socket *s);
int verify_cis_cache(struct pcmcia_socket *s);
int pccard_read_tuple(struct pcmcia_socket *s, unsigned int function, cisdata_t code, void *parse);

/* In rsrc_mgr */
int pcmcia_validate_mem(struct pcmcia_socket *s);
struct resource *pcmcia_find_io_region(unsigned long base, int num, unsigned long align,
		   struct pcmcia_socket *s);
int pcmcia_adjust_io_region(struct resource *res, unsigned long r_start,
		     unsigned long r_end, struct pcmcia_socket *s);
struct resource *pcmcia_find_mem_region(u_long base, u_long num, u_long align,
		    int low, struct pcmcia_socket *s);
void release_resource_db(struct pcmcia_socket *s);

/* In socket_sysfs.c */
extern int pccard_sysfs_add_socket(struct device *dev);
extern void pccard_sysfs_remove_socket(struct device *dev);

/* In cs.c */
extern struct rw_semaphore pcmcia_socket_list_rwsem;
extern struct list_head pcmcia_socket_list;
int pcmcia_get_window(struct pcmcia_socket *s, window_handle_t *handle, int idx, win_req_t *req);
int pccard_get_configuration_info(struct pcmcia_socket *s, struct pcmcia_device *p_dev, config_info_t *config);
int pccard_reset_card(struct pcmcia_socket *skt);


struct pcmcia_callback{
	struct module	*owner;
	int		(*event) (struct pcmcia_socket *s, event_t event, int priority);
	void		(*requery) (struct pcmcia_socket *s, int new_cis);
	int		(*suspend) (struct pcmcia_socket *s);
	int		(*resume) (struct pcmcia_socket *s);
};

int pccard_register_pcmcia(struct pcmcia_socket *s, struct pcmcia_callback *c);

#define cs_socket_name(skt)	((skt)->dev.bus_id)

#ifdef DEBUG
extern int cs_debug_level(int);

#define cs_dbg(skt, lvl, fmt, arg...) do {		\
	if (cs_debug_level(lvl))			\
		printk(KERN_DEBUG "cs: %s: " fmt, 	\
		       cs_socket_name(skt) , ## arg);	\
} while (0)

#else
#define cs_dbg(skt, lvl, fmt, arg...) do { } while (0)
#endif

#define cs_err(skt, fmt, arg...) \
	printk(KERN_ERR "cs: %s: " fmt, (skt)->dev.bus_id , ## arg)
#define SOCKET_CARDBUS		0x8000
#define SOCKET_CARDBUS_CONFIG	0x10000


/*
 * Stuff internal to module "pcmcia_rsrc":
 */
extern int static_init(struct pcmcia_socket *s);
extern struct resource *pcmcia_make_resource(resource_size_t start,
					resource_size_t end,
					unsigned long flags, const char *name);

/*
 * Stuff internal to module "pcmcia_core":
 */

/* socket_sysfs.c */
extern int pccard_sysfs_add_socket(struct device *dev);
extern void pccard_sysfs_remove_socket(struct device *dev);

/* cardbus.c */
int cb_alloc(struct pcmcia_socket *s);
void cb_free(struct pcmcia_socket *s);



/*
 * Stuff exported by module "pcmcia_core" to module "pcmcia"
 */

struct pcmcia_callback{
	struct module	*owner;
	int		(*add) (struct pcmcia_socket *s);
	int		(*remove) (struct pcmcia_socket *s);
	void		(*requery) (struct pcmcia_socket *s);
	int		(*validate) (struct pcmcia_socket *s, unsigned int *i);
	int		(*suspend) (struct pcmcia_socket *s);
	int		(*early_resume) (struct pcmcia_socket *s);
	int		(*resume) (struct pcmcia_socket *s);
};

/* cs.c */
extern struct rw_semaphore pcmcia_socket_list_rwsem;
extern struct list_head pcmcia_socket_list;
extern struct class pcmcia_socket_class;

int pccard_register_pcmcia(struct pcmcia_socket *s, struct pcmcia_callback *c);
struct pcmcia_socket *pcmcia_get_socket_by_nr(unsigned int nr);

void pcmcia_parse_uevents(struct pcmcia_socket *socket, unsigned int events);
#define PCMCIA_UEVENT_EJECT	0x0001
#define PCMCIA_UEVENT_INSERT	0x0002
#define PCMCIA_UEVENT_SUSPEND	0x0004
#define PCMCIA_UEVENT_RESUME	0x0008
#define PCMCIA_UEVENT_REQUERY	0x0010

struct pcmcia_socket *pcmcia_get_socket(struct pcmcia_socket *skt);
void pcmcia_put_socket(struct pcmcia_socket *skt);

/*
 * Stuff internal to module "pcmcia".
 */
/* ds.c */
extern struct bus_type pcmcia_bus_type;

struct pcmcia_device;

/* pcmcia_resource.c */
extern int pcmcia_release_configuration(struct pcmcia_device *p_dev);
extern int pcmcia_validate_mem(struct pcmcia_socket *s);
extern struct resource *pcmcia_find_mem_region(u_long base,
					       u_long num,
					       u_long align,
					       int low,
					       struct pcmcia_socket *s);

void pcmcia_cleanup_irq(struct pcmcia_socket *s);
int pcmcia_setup_irq(struct pcmcia_device *p_dev);

/* cistpl.c */
extern struct bin_attribute pccard_cis_attr;

int pcmcia_read_cis_mem(struct pcmcia_socket *s, int attr,
			u_int addr, u_int len, void *ptr);
int pcmcia_write_cis_mem(struct pcmcia_socket *s, int attr,
			u_int addr, u_int len, void *ptr);
void release_cis_mem(struct pcmcia_socket *s);
void destroy_cis_cache(struct pcmcia_socket *s);
int pccard_read_tuple(struct pcmcia_socket *s, unsigned int function,
		      cisdata_t code, void *parse);
int pcmcia_replace_cis(struct pcmcia_socket *s,
		       const u8 *data, const size_t len);
int pccard_validate_cis(struct pcmcia_socket *s, unsigned int *count);
int verify_cis_cache(struct pcmcia_socket *s);

int pccard_loop_tuple(struct pcmcia_socket *s, unsigned int function,
		      cisdata_t code, cisparse_t *parse, void *priv_data,
		      int (*loop_tuple) (tuple_t *tuple,
					 cisparse_t *parse,
					 void *priv_data));

int pccard_get_first_tuple(struct pcmcia_socket *s, unsigned int function,
			tuple_t *tuple);

int pccard_get_next_tuple(struct pcmcia_socket *s, unsigned int function,
			tuple_t *tuple);

int pccard_get_tuple_data(struct pcmcia_socket *s, tuple_t *tuple);

#endif /* _LINUX_CS_INTERNAL_H */
