/*
 * PCMCIA 16-bit resource management functions
 *
 * The initial developer of the original code is David A. Hinds
 * <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
 * are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.
 *
 * Copyright (C) 1999	     David A. Hinds
 * Copyright (C) 2004-2005   Dominik Brodowski
 * Copyright (C) 2004-2010   Dominik Brodowski
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/device.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/ss.h>
#include <pcmcia/cs.h>
#include <linux/netdevice.h>
#include <linux/slab.h>

#include <asm/irq.h>

#include <pcmcia/ss.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>

#include "cs_internal.h"
#include "ds_internal.h"


/* Access speed for IO windows */
static int io_speed = 0;
module_param(io_speed, int, 0444);


#ifdef CONFIG_PCMCIA_PROBE
#include <asm/irq.h>
/* mask of IRQs already reserved by other cards, we should avoid using them */
static u8 pcmcia_used_irq[NR_IRQS];
#endif


#ifdef DEBUG
extern int ds_pc_debug;

#define ds_dbg(skt, lvl, fmt, arg...) do {			\
	if (ds_pc_debug >= lvl)					\
		printk(KERN_DEBUG "pcmcia_resource: %s: " fmt,	\
			cs_socket_name(skt) , ## arg);		\
} while (0)
#else
#define ds_dbg(lvl, fmt, arg...) do { } while (0)
#endif



/** alloc_io_space
 *
 * Special stuff for managing IO windows, because they are scarce
 */

static int alloc_io_space(struct pcmcia_socket *s, u_int attr,
			  unsigned int *base, unsigned int num, u_int lines)
{
	int i;
	unsigned int try, align;

	align = (*base) ? (lines ? 1<<lines : 0) : 1;
	if (align && (align < num)) {
		if (*base) {
			ds_dbg(s, 0, "odd IO request: num %#x align %#x\n",
			       num, align);
			align = 0;
		} else
			while (align && (align < num)) align <<= 1;
	}
	if (*base & ~(align-1)) {
		ds_dbg(s, 0, "odd IO request: base %#x align %#x\n",
		       *base, align);
		align = 0;
	}
	if ((s->features & SS_CAP_STATIC_MAP) && s->io_offset) {
		*base = s->io_offset | (*base & 0x0fff);
		return 0;
	}
	/* Check for an already-allocated window that must conflict with
	 * what was asked for.  It is a hack because it does not catch all
	 * potential conflicts, just the most obvious ones.
	 */
	for (i = 0; i < MAX_IO_WIN; i++)
		if ((s->io[i].res) && *base &&
		    ((s->io[i].res->start & (align-1)) == *base))
			return 1;
	for (i = 0; i < MAX_IO_WIN; i++) {
		if (!s->io[i].res) {
			s->io[i].res = pcmcia_find_io_region(*base, num, align, s);
			if (s->io[i].res) {
				*base = s->io[i].res->start;
				s->io[i].res->flags = (s->io[i].res->flags & ~IORESOURCE_BITS) | (attr & IORESOURCE_BITS);
				s->io[i].InUse = num;
				break;
			} else
				return 1;
		} else if ((s->io[i].res->flags & IORESOURCE_BITS) != (attr & IORESOURCE_BITS))
			continue;
		/* Try to extend top of window */
		try = s->io[i].res->end + 1;
		if ((*base == 0) || (*base == try))
			if (pcmcia_adjust_io_region(s->io[i].res, s->io[i].res->start,
						    s->io[i].res->end + num, s) == 0) {
				*base = try;
				s->io[i].InUse += num;
				break;
			}
		/* Try to extend bottom of window */
		try = s->io[i].res->start - num;
		if ((*base == 0) || (*base == try))
			if (pcmcia_adjust_io_region(s->io[i].res, s->io[i].res->start - num,
						    s->io[i].res->end, s) == 0) {
				*base = try;
				s->io[i].InUse += num;
				break;
			}
	}
	return (i == MAX_IO_WIN);
} /* alloc_io_space */


static void release_io_space(struct pcmcia_socket *s, unsigned int base,
			     unsigned int num)
{
	int i;


/* Access speed for IO windows */
static int io_speed;
module_param(io_speed, int, 0444);


int pcmcia_validate_mem(struct pcmcia_socket *s)
{
	if (s->resource_ops->validate_mem)
		return s->resource_ops->validate_mem(s);
	/* if there is no callback, we can assume that everything is OK */
	return 0;
}

struct resource *pcmcia_find_mem_region(u_long base, u_long num, u_long align,
				 int low, struct pcmcia_socket *s)
{
	if (s->resource_ops->find_mem)
		return s->resource_ops->find_mem(base, num, align, low, s);
	return NULL;
}


/**
 * release_io_space() - release IO ports allocated with alloc_io_space()
 * @s: pcmcia socket
 * @res: resource to release
 *
 */
static void release_io_space(struct pcmcia_socket *s, struct resource *res)
{
	resource_size_t num = resource_size(res);
	int i;

	dev_dbg(&s->dev, "release_io_space for %pR\n", res);

	for (i = 0; i < MAX_IO_WIN; i++) {
		if (!s->io[i].res)
			continue;
		if ((s->io[i].res->start <= base) &&
		    (s->io[i].res->end >= base+num-1)) {
			s->io[i].InUse -= num;
		if ((s->io[i].res->start <= res->start) &&
		    (s->io[i].res->end >= res->end)) {
			s->io[i].InUse -= num;
			if (res->parent)
				release_resource(res);
			res->start = res->end = 0;
			res->flags = IORESOURCE_IO;
			/* Free the window if no one else is using it */
			if (s->io[i].InUse == 0) {
				release_resource(s->io[i].res);
				kfree(s->io[i].res);
				s->io[i].res = NULL;
			}
		}
	}
} /* release_io_space */


/** pccard_access_configuration_register
 *
 * Access_configuration_register() reads and writes configuration
 * registers in attribute memory.  Memory window 0 is reserved for
 * this and the tuple reading services.
 */

int pcmcia_access_configuration_register(struct pcmcia_device *p_dev,
					 conf_reg_t *reg)
}


/**
 * alloc_io_space() - allocate IO ports for use by a PCMCIA device
 * @s: pcmcia socket
 * @res: resource to allocate (begin: begin, end: size)
 * @lines: number of IO lines decoded by the PCMCIA card
 *
 * Special stuff for managing IO windows, because they are scarce
 */
static int alloc_io_space(struct pcmcia_socket *s, struct resource *res,
			unsigned int lines)
{
	unsigned int align;
	unsigned int base = res->start;
	unsigned int num = res->end;
	int ret;

	res->flags |= IORESOURCE_IO;

	dev_dbg(&s->dev, "alloc_io_space request for %pR, %d lines\n",
		res, lines);

	align = base ? (lines ? 1<<lines : 0) : 1;
	if (align && (align < num)) {
		if (base) {
			dev_dbg(&s->dev, "odd IO request\n");
			align = 0;
		} else
			while (align && (align < num))
				align <<= 1;
	}
	if (base & ~(align-1)) {
		dev_dbg(&s->dev, "odd IO request\n");
		align = 0;
	}

	ret = s->resource_ops->find_io(s, res->flags, &base, num, align,
				&res->parent);
	if (ret) {
		dev_dbg(&s->dev, "alloc_io_space request failed (%d)\n", ret);
		return -EINVAL;
	}

	res->start = base;
	res->end = res->start + num - 1;

	if (res->parent) {
		ret = request_resource(res->parent, res);
		if (ret) {
			dev_warn(&s->dev,
				"request_resource %pR failed: %d\n", res, ret);
			res->parent = NULL;
			release_io_space(s, res);
		}
	}
	dev_dbg(&s->dev, "alloc_io_space request result %d: %pR\n", ret, res);
	return ret;
}


/**
 * pcmcia_access_config() - read or write card configuration registers
 *
 * pcmcia_access_config() reads and writes configuration registers in
 * attribute memory.  Memory window 0 is reserved for this and the tuple
 * reading services. Drivers must use pcmcia_read_config_byte() or
 * pcmcia_write_config_byte().
 */
static int pcmcia_access_config(struct pcmcia_device *p_dev,
				off_t where, u8 *val,
				int (*accessf) (struct pcmcia_socket *s,
						int attr, unsigned int addr,
						unsigned int len, void *ptr))
{
	struct pcmcia_socket *s;
	config_t *c;
	int addr;
	u_char val;

	if (!p_dev || !p_dev->function_config)
		return CS_NO_CARD;

	s = p_dev->socket;
	c = p_dev->function_config;

	if (!(c->state & CONFIG_LOCKED))
		return CS_CONFIGURATION_LOCKED;

	addr = (c->ConfigBase + reg->Offset) >> 1;

	switch (reg->Action) {
	case CS_READ:
		pcmcia_read_cis_mem(s, 1, addr, 1, &val);
		reg->Value = val;
		break;
	case CS_WRITE:
		val = reg->Value;
		pcmcia_write_cis_mem(s, 1, addr, 1, &val);
		break;
	default:
		return CS_BAD_ARGS;
		break;
	}
	return CS_SUCCESS;
} /* pcmcia_access_configuration_register */
EXPORT_SYMBOL(pcmcia_access_configuration_register);


int pccard_get_configuration_info(struct pcmcia_socket *s,
				  struct pcmcia_device *p_dev,
				  config_info_t *config)
{
	config_t *c;

	if (!(s->state & SOCKET_PRESENT))
		return CS_NO_CARD;


#ifdef CONFIG_CARDBUS
	if (s->state & SOCKET_CARDBUS) {
		memset(config, 0, sizeof(config_info_t));
		config->Vcc = s->socket.Vcc;
		config->Vpp1 = config->Vpp2 = s->socket.Vpp;
		config->Option = s->cb_dev->subordinate->number;
		if (s->state & SOCKET_CARDBUS_CONFIG) {
			config->Attributes = CONF_VALID_CLIENT;
			config->IntType = INT_CARDBUS;
			config->AssignedIRQ = s->irq.AssignedIRQ;
			if (config->AssignedIRQ)
				config->Attributes |= CONF_ENABLE_IRQ;
			if (s->io[0].res) {
				config->BasePort1 = s->io[0].res->start;
				config->NumPorts1 = s->io[0].res->end - config->BasePort1 + 1;
			}
		}
		return CS_SUCCESS;
	}
#endif

	if (p_dev) {
		c = p_dev->function_config;
		config->Function = p_dev->func;
	} else {
		c = NULL;
		config->Function = 0;
	}

	if ((c == NULL) || !(c->state & CONFIG_LOCKED)) {
		config->Attributes = 0;
		config->Vcc = s->socket.Vcc;
		config->Vpp1 = config->Vpp2 = s->socket.Vpp;
		return CS_SUCCESS;
	}

	config->Attributes = c->Attributes | CONF_VALID_CLIENT;
	config->Vcc = s->socket.Vcc;
	config->Vpp1 = config->Vpp2 = s->socket.Vpp;
	config->IntType = c->IntType;
	config->ConfigBase = c->ConfigBase;
	config->Status = c->Status;
	config->Pin = c->Pin;
	config->Copy = c->Copy;
	config->Option = c->Option;
	config->ExtStatus = c->ExtStatus;
	config->Present = config->CardValues = c->CardValues;
	config->IRQAttributes = c->irq.Attributes;
	config->AssignedIRQ = s->irq.AssignedIRQ;
	config->BasePort1 = c->io.BasePort1;
	config->NumPorts1 = c->io.NumPorts1;
	config->Attributes1 = c->io.Attributes1;
	config->BasePort2 = c->io.BasePort2;
	config->NumPorts2 = c->io.NumPorts2;
	config->Attributes2 = c->io.Attributes2;
	config->IOAddrLines = c->io.IOAddrLines;

	return CS_SUCCESS;
} /* pccard_get_configuration_info */

int pcmcia_get_configuration_info(struct pcmcia_device *p_dev,
				  config_info_t *config)
{
	return pccard_get_configuration_info(p_dev->socket, p_dev,
					     config);
}
EXPORT_SYMBOL(pcmcia_get_configuration_info);


/** pcmcia_get_window
 */
int pcmcia_get_window(struct pcmcia_socket *s, window_handle_t *handle,
		      int idx, win_req_t *req)
{
	window_t *win;
	int w;

	if (!s || !(s->state & SOCKET_PRESENT))
		return CS_NO_CARD;
	for (w = idx; w < MAX_WIN; w++)
		if (s->state & SOCKET_WIN_REQ(w))
			break;
	if (w == MAX_WIN)
		return CS_NO_MORE_ITEMS;
	win = &s->win[w];
	req->Base = win->ctl.res->start;
	req->Size = win->ctl.res->end - win->ctl.res->start + 1;
	req->AccessSpeed = win->ctl.speed;
	req->Attributes = 0;
	if (win->ctl.flags & MAP_ATTRIB)
		req->Attributes |= WIN_MEMORY_TYPE_AM;
	if (win->ctl.flags & MAP_ACTIVE)
		req->Attributes |= WIN_ENABLE;
	if (win->ctl.flags & MAP_16BIT)
		req->Attributes |= WIN_DATA_WIDTH_16;
	if (win->ctl.flags & MAP_USE_WAIT)
		req->Attributes |= WIN_USE_WAIT;
	*handle = win;
	return CS_SUCCESS;
} /* pcmcia_get_window */
EXPORT_SYMBOL(pcmcia_get_window);


/** pcmcia_get_mem_page
 *
 * Change the card address of an already open memory window.
 */
int pcmcia_get_mem_page(window_handle_t win, memreq_t *req)
{
	if ((win == NULL) || (win->magic != WINDOW_MAGIC))
		return CS_BAD_HANDLE;
	req->Page = 0;
	req->CardOffset = win->ctl.card_start;
	return CS_SUCCESS;
} /* pcmcia_get_mem_page */
EXPORT_SYMBOL(pcmcia_get_mem_page);


int pcmcia_map_mem_page(window_handle_t win, memreq_t *req)
{
	struct pcmcia_socket *s;
	if ((win == NULL) || (win->magic != WINDOW_MAGIC))
		return CS_BAD_HANDLE;
	if (req->Page != 0)
		return CS_BAD_PAGE;
	s = win->sock;
	win->ctl.card_start = req->CardOffset;
	if (s->ops->set_mem_map(s, &win->ctl) != 0)
		return CS_BAD_OFFSET;
	return CS_SUCCESS;
} /* pcmcia_map_mem_page */
EXPORT_SYMBOL(pcmcia_map_mem_page);


/** pcmcia_modify_configuration
 *
 * Modify a locked socket configuration
 */
int pcmcia_modify_configuration(struct pcmcia_device *p_dev,
				modconf_t *mod)
{
	struct pcmcia_socket *s;
	config_t *c;

	s = p_dev->socket;
	c = p_dev->function_config;

	if (!(s->state & SOCKET_PRESENT))
		return CS_NO_CARD;
	if (!(c->state & CONFIG_LOCKED))
		return CS_CONFIGURATION_LOCKED;

	if (mod->Attributes & CONF_IRQ_CHANGE_VALID) {
		if (mod->Attributes & CONF_ENABLE_IRQ) {
			c->Attributes |= CONF_ENABLE_IRQ;
			s->socket.io_irq = s->irq.AssignedIRQ;
		} else {
			c->Attributes &= ~CONF_ENABLE_IRQ;
			s->socket.io_irq = 0;
		}
		s->ops->set_socket(s, &s->socket);
	}

	if (mod->Attributes & CONF_VCC_CHANGE_VALID)
		return CS_BAD_VCC;

	/* We only allow changing Vpp1 and Vpp2 to the same value */
	if ((mod->Attributes & CONF_VPP1_CHANGE_VALID) &&
	    (mod->Attributes & CONF_VPP2_CHANGE_VALID)) {
		if (mod->Vpp1 != mod->Vpp2)
			return CS_BAD_VPP;
		s->socket.Vpp = mod->Vpp1;
		if (s->ops->set_socket(s, &s->socket))
			return CS_BAD_VPP;
	} else if ((mod->Attributes & CONF_VPP1_CHANGE_VALID) ||
		   (mod->Attributes & CONF_VPP2_CHANGE_VALID))
		return CS_BAD_VPP;

	if (mod->Attributes & CONF_IO_CHANGE_WIDTH) {
		pccard_io_map io_off = { 0, 0, 0, 0, 1 };
		pccard_io_map io_on;
		int i;

		io_on.speed = io_speed;
		for (i = 0; i < MAX_IO_WIN; i++) {
			if (!s->io[i].res)
				continue;
			io_off.map = i;
			io_on.map = i;

			io_on.flags = MAP_ACTIVE | IO_DATA_PATH_WIDTH_8;
			io_on.start = s->io[i].res->start;
			io_on.stop = s->io[i].res->end;

			s->ops->set_io_map(s, &io_off);
			mdelay(40);
			s->ops->set_io_map(s, &io_on);
		}
	}

	return CS_SUCCESS;
} /* modify_configuration */
EXPORT_SYMBOL(pcmcia_modify_configuration);


	int ret = 0;

	s = p_dev->socket;

	mutex_lock(&s->ops_mutex);
	c = p_dev->function_config;

	if (!(c->state & CONFIG_LOCKED)) {
		dev_dbg(&p_dev->dev, "Configuration isn't locked\n");
		mutex_unlock(&s->ops_mutex);
		return -EACCES;
	}

	addr = (p_dev->config_base + where) >> 1;

	ret = accessf(s, 1, addr, 1, val);

	mutex_unlock(&s->ops_mutex);

	return ret;
}


/**
 * pcmcia_read_config_byte() - read a byte from a card configuration register
 *
 * pcmcia_read_config_byte() reads a byte from a configuration register in
 * attribute memory.
 */
int pcmcia_read_config_byte(struct pcmcia_device *p_dev, off_t where, u8 *val)
{
	return pcmcia_access_config(p_dev, where, val, pcmcia_read_cis_mem);
}
EXPORT_SYMBOL(pcmcia_read_config_byte);


/**
 * pcmcia_write_config_byte() - write a byte to a card configuration register
 *
 * pcmcia_write_config_byte() writes a byte to a configuration register in
 * attribute memory.
 */
int pcmcia_write_config_byte(struct pcmcia_device *p_dev, off_t where, u8 val)
{
	return pcmcia_access_config(p_dev, where, &val, pcmcia_write_cis_mem);
}
EXPORT_SYMBOL(pcmcia_write_config_byte);


/**
 * pcmcia_map_mem_page() - modify iomem window to point to a different offset
 * @p_dev: pcmcia device
 * @res: iomem resource already enabled by pcmcia_request_window()
 * @offset: card_offset to map
 *
 * pcmcia_map_mem_page() modifies what can be read and written by accessing
 * an iomem range previously enabled by pcmcia_request_window(), by setting
 * the card_offset value to @offset.
 */
int pcmcia_map_mem_page(struct pcmcia_device *p_dev, struct resource *res,
			unsigned int offset)
{
	struct pcmcia_socket *s = p_dev->socket;
	unsigned int w;
	int ret;

	w = ((res->flags & IORESOURCE_BITS & WIN_FLAGS_REQ) >> 2) - 1;
	if (w >= MAX_WIN)
		return -EINVAL;

	mutex_lock(&s->ops_mutex);
	s->win[w].card_start = offset;
	ret = s->ops->set_mem_map(s, &s->win[w]);
	if (ret)
		dev_warn(&p_dev->dev, "failed to set_mem_map\n");
	mutex_unlock(&s->ops_mutex);
	return ret;
}
EXPORT_SYMBOL(pcmcia_map_mem_page);


/**
 * pcmcia_fixup_iowidth() - reduce io width to 8bit
 * @p_dev: pcmcia device
 *
 * pcmcia_fixup_iowidth() allows a PCMCIA device driver to reduce the
 * IO width to 8bit after having called pcmcia_enable_device()
 * previously.
 */
int pcmcia_fixup_iowidth(struct pcmcia_device *p_dev)
{
	struct pcmcia_socket *s = p_dev->socket;
	pccard_io_map io_off = { 0, 0, 0, 0, 1 };
	pccard_io_map io_on;
	int i, ret = 0;

	mutex_lock(&s->ops_mutex);

	dev_dbg(&p_dev->dev, "fixup iowidth to 8bit\n");

	if (!(s->state & SOCKET_PRESENT) ||
		!(p_dev->function_config->state & CONFIG_LOCKED)) {
		dev_dbg(&p_dev->dev, "No card? Config not locked?\n");
		ret = -EACCES;
		goto unlock;
	}

	io_on.speed = io_speed;
	for (i = 0; i < MAX_IO_WIN; i++) {
		if (!s->io[i].res)
			continue;
		io_off.map = i;
		io_on.map = i;

		io_on.flags = MAP_ACTIVE | IO_DATA_PATH_WIDTH_8;
		io_on.start = s->io[i].res->start;
		io_on.stop = s->io[i].res->end;

		s->ops->set_io_map(s, &io_off);
		mdelay(40);
		s->ops->set_io_map(s, &io_on);
	}
unlock:
	mutex_unlock(&s->ops_mutex);

	return ret;
}
EXPORT_SYMBOL(pcmcia_fixup_iowidth);


/**
 * pcmcia_fixup_vpp() - set Vpp to a new voltage level
 * @p_dev: pcmcia device
 * @new_vpp: new Vpp voltage
 *
 * pcmcia_fixup_vpp() allows a PCMCIA device driver to set Vpp to
 * a new voltage level between calls to pcmcia_enable_device()
 * and pcmcia_disable_device().
 */
int pcmcia_fixup_vpp(struct pcmcia_device *p_dev, unsigned char new_vpp)
{
	struct pcmcia_socket *s = p_dev->socket;
	int ret = 0;

	mutex_lock(&s->ops_mutex);

	dev_dbg(&p_dev->dev, "fixup Vpp to %d\n", new_vpp);

	if (!(s->state & SOCKET_PRESENT) ||
		!(p_dev->function_config->state & CONFIG_LOCKED)) {
		dev_dbg(&p_dev->dev, "No card? Config not locked?\n");
		ret = -EACCES;
		goto unlock;
	}

	s->socket.Vpp = new_vpp;
	if (s->ops->set_socket(s, &s->socket)) {
		dev_warn(&p_dev->dev, "Unable to set VPP\n");
		ret = -EIO;
		goto unlock;
	}
	p_dev->vpp = new_vpp;

unlock:
	mutex_unlock(&s->ops_mutex);

	return ret;
}
EXPORT_SYMBOL(pcmcia_fixup_vpp);


/**
 * pcmcia_release_configuration() - physically disable a PCMCIA device
 * @p_dev: pcmcia device
 *
 * pcmcia_release_configuration() is the 1:1 counterpart to
 * pcmcia_enable_device(): If a PCMCIA device is no longer used by any
 * driver, the Vpp voltage is set to 0, IRQs will no longer be generated,
 * and I/O ranges will be disabled. As pcmcia_release_io() and
 * pcmcia_release_window() still need to be called, device drivers are
 * expected to call pcmcia_disable_device() instead.
 */
int pcmcia_release_configuration(struct pcmcia_device *p_dev)
{
	pccard_io_map io = { 0, 0, 0, 0, 1 };
	struct pcmcia_socket *s = p_dev->socket;
	config_t *c = p_dev->function_config;
	int i;

	if (p_dev->_locked) {
		p_dev->_locked = 0;
		if (--(s->lock_count) == 0) {
			s->socket.flags = SS_OUTPUT_ENA;   /* Is this correct? */
	config_t *c;
	int i;

	mutex_lock(&s->ops_mutex);
	c = p_dev->function_config;
	if (p_dev->_locked) {
		p_dev->_locked = 0;
		if (--(s->lock_count) == 0) {
			s->socket.flags = SS_OUTPUT_ENA; /* Is this correct? */
			s->socket.Vpp = 0;
			s->socket.io_irq = 0;
			s->ops->set_socket(s, &s->socket);
		}
	}
	if (c->state & CONFIG_LOCKED) {
		c->state &= ~CONFIG_LOCKED;
		if (c->state & CONFIG_IO_REQ)
			for (i = 0; i < MAX_IO_WIN; i++) {
				if (!s->io[i].res)
					continue;
				s->io[i].Config--;
				if (s->io[i].Config != 0)
					continue;
				io.map = i;
				s->ops->set_io_map(s, &io);
			}
	}

	return CS_SUCCESS;
} /* pcmcia_release_configuration */


/** pcmcia_release_io
 *
 * Release_io() releases the I/O ranges allocated by a client.  This
 * may be invoked some time after a card ejection has already dumped
 * the actual socket configuration, so if the client is "stale", we
 * don't bother checking the port ranges against the current socket
 * values.
 */
static int pcmcia_release_io(struct pcmcia_device *p_dev, io_req_t *req)
{
	struct pcmcia_socket *s = p_dev->socket;
	config_t *c = p_dev->function_config;

	if (!p_dev->_io )
		return CS_BAD_HANDLE;

	p_dev->_io = 0;

	if ((c->io.BasePort1 != req->BasePort1) ||
	    (c->io.NumPorts1 != req->NumPorts1) ||
	    (c->io.BasePort2 != req->BasePort2) ||
	    (c->io.NumPorts2 != req->NumPorts2))
		return CS_BAD_ARGS;

	c->state &= ~CONFIG_IO_REQ;

	release_io_space(s, req->BasePort1, req->NumPorts1);
	if (req->NumPorts2)
		release_io_space(s, req->BasePort2, req->NumPorts2);

	return CS_SUCCESS;
} /* pcmcia_release_io */


static int pcmcia_release_irq(struct pcmcia_device *p_dev, irq_req_t *req)
{
	struct pcmcia_socket *s = p_dev->socket;
	config_t *c= p_dev->function_config;

	if (!p_dev->_irq)
		return CS_BAD_HANDLE;
	p_dev->_irq = 0;

	if (c->state & CONFIG_LOCKED)
		return CS_CONFIGURATION_LOCKED;
	if (c->irq.Attributes != req->Attributes)
		return CS_BAD_ATTRIBUTE;
	if (s->irq.AssignedIRQ != req->AssignedIRQ)
		return CS_BAD_IRQ;
	if (--s->irq.Config == 0) {
		c->state &= ~CONFIG_IRQ_REQ;
		s->irq.AssignedIRQ = 0;
	}

	if (req->Attributes & IRQ_HANDLE_PRESENT) {
		free_irq(req->AssignedIRQ, req->Instance);
	}

#ifdef CONFIG_PCMCIA_PROBE
	pcmcia_used_irq[req->AssignedIRQ]--;
#endif

	return CS_SUCCESS;
} /* pcmcia_release_irq */


int pcmcia_release_window(window_handle_t win)
{
	struct pcmcia_socket *s;

	if ((win == NULL) || (win->magic != WINDOW_MAGIC))
		return CS_BAD_HANDLE;
	s = win->sock;
	if (!(win->handle->_win & CLIENT_WIN_REQ(win->index)))
		return CS_BAD_HANDLE;

	/* Shut down memory window */
	win->ctl.flags &= ~MAP_ACTIVE;
	s->ops->set_mem_map(s, &win->ctl);
	s->state &= ~SOCKET_WIN_REQ(win->index);

	/* Release system memory */
	if (win->ctl.res) {
		release_resource(win->ctl.res);
		kfree(win->ctl.res);
		win->ctl.res = NULL;
	}
	win->handle->_win &= ~CLIENT_WIN_REQ(win->index);

	win->magic = 0;

	return CS_SUCCESS;
	mutex_unlock(&s->ops_mutex);

	return 0;
}


/**
 * pcmcia_release_io() - release I/O allocated by a PCMCIA device
 * @p_dev: pcmcia device
 *
 * pcmcia_release_io() releases the I/O ranges allocated by a PCMCIA
 * device.  This may be invoked some time after a card ejection has
 * already dumped the actual socket configuration, so if the client is
 * "stale", we don't bother checking the port ranges against the
 * current socket values.
 */
static int pcmcia_release_io(struct pcmcia_device *p_dev)
{
	struct pcmcia_socket *s = p_dev->socket;
	int ret = -EINVAL;
	config_t *c;

	mutex_lock(&s->ops_mutex);
	if (!p_dev->_io)
		goto out;

	c = p_dev->function_config;

	release_io_space(s, &c->io[0]);

	if (c->io[1].end)
		release_io_space(s, &c->io[1]);

	p_dev->_io = 0;
	c->state &= ~CONFIG_IO_REQ;

out:
	mutex_unlock(&s->ops_mutex);

	return ret;
} /* pcmcia_release_io */


/**
 * pcmcia_release_window() - release reserved iomem for PCMCIA devices
 * @p_dev: pcmcia device
 * @res: iomem resource to release
 *
 * pcmcia_release_window() releases &struct resource *res which was
 * previously reserved by calling pcmcia_request_window().
 */
int pcmcia_release_window(struct pcmcia_device *p_dev, struct resource *res)
{
	struct pcmcia_socket *s = p_dev->socket;
	pccard_mem_map *win;
	unsigned int w;

	dev_dbg(&p_dev->dev, "releasing window %pR\n", res);

	w = ((res->flags & IORESOURCE_BITS & WIN_FLAGS_REQ) >> 2) - 1;
	if (w >= MAX_WIN)
		return -EINVAL;

	mutex_lock(&s->ops_mutex);
	win = &s->win[w];

	if (!(p_dev->_win & CLIENT_WIN_REQ(w))) {
		dev_dbg(&p_dev->dev, "not releasing unknown window\n");
		mutex_unlock(&s->ops_mutex);
		return -EINVAL;
	}

	/* Shut down memory window */
	win->flags &= ~MAP_ACTIVE;
	s->ops->set_mem_map(s, win);
	s->state &= ~SOCKET_WIN_REQ(w);

	/* Release system memory */
	if (win->res) {
		release_resource(res);
		release_resource(win->res);
		kfree(win->res);
		win->res = NULL;
	}
	res->start = res->end = 0;
	res->flags = IORESOURCE_MEM;
	p_dev->_win &= ~CLIENT_WIN_REQ(w);
	mutex_unlock(&s->ops_mutex);

	return 0;
} /* pcmcia_release_window */
EXPORT_SYMBOL(pcmcia_release_window);


int pcmcia_request_configuration(struct pcmcia_device *p_dev,
				 config_req_t *req)
{
	int i;
	u_int base;
	struct pcmcia_socket *s = p_dev->socket;
	config_t *c;
	pccard_io_map iomap;

	if (!(s->state & SOCKET_PRESENT))
		return CS_NO_CARD;

	if (req->IntType & INT_CARDBUS)
		return CS_UNSUPPORTED_MODE;
	c = p_dev->function_config;
	if (c->state & CONFIG_LOCKED)
		return CS_CONFIGURATION_LOCKED;

	/* Do power control.  We don't allow changes in Vcc. */
	s->socket.Vpp = req->Vpp;
	if (s->ops->set_socket(s, &s->socket))
		return CS_BAD_VPP;

	/* Pick memory or I/O card, DMA mode, interrupt */
	c->IntType = req->IntType;
	c->Attributes = req->Attributes;
	if (req->IntType & INT_MEMORY_AND_IO)
		s->socket.flags |= SS_IOCARD;
	if (req->IntType & INT_ZOOMED_VIDEO)
		s->socket.flags |= SS_ZVCARD | SS_IOCARD;
	if (req->Attributes & CONF_ENABLE_DMA)
		s->socket.flags |= SS_DMA_MODE;
	if (req->Attributes & CONF_ENABLE_SPKR)
		s->socket.flags |= SS_SPKR_ENA;
	if (req->Attributes & CONF_ENABLE_IRQ)
		s->socket.io_irq = s->irq.AssignedIRQ;
	else
		s->socket.io_irq = 0;
	s->ops->set_socket(s, &s->socket);
	s->lock_count++;

	/* Set up CIS configuration registers */
	base = c->ConfigBase = req->ConfigBase;
	c->CardValues = req->Present;
	if (req->Present & PRESENT_COPY) {
		c->Copy = req->Copy;
		pcmcia_write_cis_mem(s, 1, (base + CISREG_SCR)>>1, 1, &c->Copy);
	}
	if (req->Present & PRESENT_OPTION) {
		if (s->functions == 1) {
			c->Option = req->ConfigIndex & COR_CONFIG_MASK;
		} else {
			c->Option = req->ConfigIndex & COR_MFC_CONFIG_MASK;
			c->Option |= COR_FUNC_ENA|COR_IREQ_ENA;
			if (req->Present & PRESENT_IOBASE_0)
				c->Option |= COR_ADDR_DECODE;
		}
		if (c->state & CONFIG_IRQ_REQ)
			if (!(c->irq.Attributes & IRQ_FORCED_PULSE))
				c->Option |= COR_LEVEL_REQ;
		pcmcia_write_cis_mem(s, 1, (base + CISREG_COR)>>1, 1, &c->Option);
		mdelay(40);
	}
	if (req->Present & PRESENT_STATUS) {
		c->Status = req->Status;
		pcmcia_write_cis_mem(s, 1, (base + CISREG_CCSR)>>1, 1, &c->Status);
	}
	if (req->Present & PRESENT_PIN_REPLACE) {
		c->Pin = req->Pin;
		pcmcia_write_cis_mem(s, 1, (base + CISREG_PRR)>>1, 1, &c->Pin);
	}
	if (req->Present & PRESENT_EXT_STATUS) {
		c->ExtStatus = req->ExtStatus;
		pcmcia_write_cis_mem(s, 1, (base + CISREG_ESR)>>1, 1, &c->ExtStatus);
	}
	if (req->Present & PRESENT_IOBASE_0) {
		u_char b = c->io.BasePort1 & 0xff;
		pcmcia_write_cis_mem(s, 1, (base + CISREG_IOBASE_0)>>1, 1, &b);
		b = (c->io.BasePort1 >> 8) & 0xff;
		pcmcia_write_cis_mem(s, 1, (base + CISREG_IOBASE_1)>>1, 1, &b);
	}
	if (req->Present & PRESENT_IOSIZE) {
		u_char b = c->io.NumPorts1 + c->io.NumPorts2 - 1;
/**
 * pcmcia_enable_device() - set up and activate a PCMCIA device
 * @p_dev: the associated PCMCIA device
 *
 * pcmcia_enable_device() physically enables a PCMCIA device. It parses
 * the flags passed to in @flags and stored in @p_dev->flags and sets up
 * the Vpp voltage, enables the speaker line, I/O ports and store proper
 * values to configuration registers.
 */
int pcmcia_enable_device(struct pcmcia_device *p_dev)
{
	int i;
	unsigned int base;
	struct pcmcia_socket *s = p_dev->socket;
	config_t *c;
	pccard_io_map iomap;
	unsigned char status = 0;
	unsigned char ext_status = 0;
	unsigned char option = 0;
	unsigned int flags = p_dev->config_flags;

	if (!(s->state & SOCKET_PRESENT))
		return -ENODEV;

	mutex_lock(&s->ops_mutex);
	c = p_dev->function_config;
	if (c->state & CONFIG_LOCKED) {
		mutex_unlock(&s->ops_mutex);
		dev_dbg(&p_dev->dev, "Configuration is locked\n");
		return -EACCES;
	}

	/* Do power control.  We don't allow changes in Vcc. */
	s->socket.Vpp = p_dev->vpp;
	if (s->ops->set_socket(s, &s->socket)) {
		mutex_unlock(&s->ops_mutex);
		dev_warn(&p_dev->dev, "Unable to set socket state\n");
		return -EINVAL;
	}

	/* Pick memory or I/O card, DMA mode, interrupt */
	if (p_dev->_io || flags & CONF_ENABLE_IRQ)
		flags |= CONF_ENABLE_IOCARD;
	if (flags & CONF_ENABLE_IOCARD)
		s->socket.flags |= SS_IOCARD;
	if (flags & CONF_ENABLE_ZVCARD)
		s->socket.flags |= SS_ZVCARD | SS_IOCARD;
	if (flags & CONF_ENABLE_SPKR) {
		s->socket.flags |= SS_SPKR_ENA;
		status = CCSR_AUDIO_ENA;
		if (!(p_dev->config_regs & PRESENT_STATUS))
			dev_warn(&p_dev->dev, "speaker requested, but "
					      "PRESENT_STATUS not set!\n");
	}
	if (flags & CONF_ENABLE_IRQ)
		s->socket.io_irq = s->pcmcia_irq;
	else
		s->socket.io_irq = 0;
	if (flags & CONF_ENABLE_ESR) {
		p_dev->config_regs |= PRESENT_EXT_STATUS;
		ext_status = ESR_REQ_ATTN_ENA;
	}
	s->ops->set_socket(s, &s->socket);
	s->lock_count++;

	dev_dbg(&p_dev->dev,
		"enable_device: V %d, flags %x, base %x, regs %x, idx %x\n",
		p_dev->vpp, flags, p_dev->config_base, p_dev->config_regs,
		p_dev->config_index);

	/* Set up CIS configuration registers */
	base = p_dev->config_base;
	if (p_dev->config_regs & PRESENT_COPY) {
		u16 tmp = 0;
		dev_dbg(&p_dev->dev, "clearing CISREG_SCR\n");
		pcmcia_write_cis_mem(s, 1, (base + CISREG_SCR)>>1, 1, &tmp);
	}
	if (p_dev->config_regs & PRESENT_PIN_REPLACE) {
		u16 tmp = 0;
		dev_dbg(&p_dev->dev, "clearing CISREG_PRR\n");
		pcmcia_write_cis_mem(s, 1, (base + CISREG_PRR)>>1, 1, &tmp);
	}
	if (p_dev->config_regs & PRESENT_OPTION) {
		if (s->functions == 1) {
			option = p_dev->config_index & COR_CONFIG_MASK;
		} else {
			option = p_dev->config_index & COR_MFC_CONFIG_MASK;
			option |= COR_FUNC_ENA|COR_IREQ_ENA;
			if (p_dev->config_regs & PRESENT_IOBASE_0)
				option |= COR_ADDR_DECODE;
		}
		if ((flags & CONF_ENABLE_IRQ) &&
			!(flags & CONF_ENABLE_PULSE_IRQ))
			option |= COR_LEVEL_REQ;
		pcmcia_write_cis_mem(s, 1, (base + CISREG_COR)>>1, 1, &option);
		mdelay(40);
	}
	if (p_dev->config_regs & PRESENT_STATUS)
		pcmcia_write_cis_mem(s, 1, (base + CISREG_CCSR)>>1, 1, &status);

	if (p_dev->config_regs & PRESENT_EXT_STATUS)
		pcmcia_write_cis_mem(s, 1, (base + CISREG_ESR)>>1, 1,
					&ext_status);

	if (p_dev->config_regs & PRESENT_IOBASE_0) {
		u8 b = c->io[0].start & 0xff;
		pcmcia_write_cis_mem(s, 1, (base + CISREG_IOBASE_0)>>1, 1, &b);
		b = (c->io[0].start >> 8) & 0xff;
		pcmcia_write_cis_mem(s, 1, (base + CISREG_IOBASE_1)>>1, 1, &b);
	}
	if (p_dev->config_regs & PRESENT_IOSIZE) {
		u8 b = resource_size(&c->io[0]) + resource_size(&c->io[1]) - 1;
		pcmcia_write_cis_mem(s, 1, (base + CISREG_IOSIZE)>>1, 1, &b);
	}

	/* Configure I/O windows */
	if (c->state & CONFIG_IO_REQ) {
		iomap.speed = io_speed;
		for (i = 0; i < MAX_IO_WIN; i++)
			if (s->io[i].res) {
				iomap.map = i;
				iomap.flags = MAP_ACTIVE;
				switch (s->io[i].res->flags & IO_DATA_PATH_WIDTH) {
				case IO_DATA_PATH_WIDTH_16:
					iomap.flags |= MAP_16BIT; break;
				case IO_DATA_PATH_WIDTH_AUTO:
					iomap.flags |= MAP_AUTOSZ; break;
				default:
					break;
				}
				iomap.start = s->io[i].res->start;
				iomap.stop = s->io[i].res->end;
				s->ops->set_io_map(s, &iomap);
				s->io[i].Config++;
			}
	}

	c->state |= CONFIG_LOCKED;
	p_dev->_locked = 1;
	return CS_SUCCESS;
} /* pcmcia_request_configuration */
EXPORT_SYMBOL(pcmcia_request_configuration);


/** pcmcia_request_io
 *
 * Request_io() reserves ranges of port addresses for a socket.
 * I have not implemented range sharing or alias addressing.
 */
int pcmcia_request_io(struct pcmcia_device *p_dev, io_req_t *req)
{
	struct pcmcia_socket *s = p_dev->socket;
	config_t *c;

	if (!(s->state & SOCKET_PRESENT))
		return CS_NO_CARD;

	if (!req)
		return CS_UNSUPPORTED_MODE;
	c = p_dev->function_config;
	if (c->state & CONFIG_LOCKED)
		return CS_CONFIGURATION_LOCKED;
	if (c->state & CONFIG_IO_REQ)
		return CS_IN_USE;
	if (req->Attributes1 & (IO_SHARED | IO_FORCE_ALIAS_ACCESS))
		return CS_BAD_ATTRIBUTE;
	if ((req->NumPorts2 > 0) &&
	    (req->Attributes2 & (IO_SHARED | IO_FORCE_ALIAS_ACCESS)))
		return CS_BAD_ATTRIBUTE;

	if (alloc_io_space(s, req->Attributes1, &req->BasePort1,
			   req->NumPorts1, req->IOAddrLines))
		return CS_IN_USE;

	if (req->NumPorts2) {
		if (alloc_io_space(s, req->Attributes2, &req->BasePort2,
				   req->NumPorts2, req->IOAddrLines)) {
			release_io_space(s, req->BasePort1, req->NumPorts1);
			return CS_IN_USE;
		}
	}

	c->io = *req;
	c->state |= CONFIG_IO_REQ;
	p_dev->_io = 1;
	return CS_SUCCESS;
	mutex_unlock(&s->ops_mutex);
	return 0;
} /* pcmcia_enable_device */
EXPORT_SYMBOL(pcmcia_enable_device);


/**
 * pcmcia_request_io() - attempt to reserve port ranges for PCMCIA devices
 * @p_dev: the associated PCMCIA device
 *
 * pcmcia_request_io() attempts to reserve the IO port ranges specified in
 * &struct pcmcia_device @p_dev->resource[0] and @p_dev->resource[1]. The
 * "start" value is the requested start of the IO port resource; "end"
 * reflects the number of ports requested. The number of IO lines requested
 * is specified in &struct pcmcia_device @p_dev->io_lines.
 */
int pcmcia_request_io(struct pcmcia_device *p_dev)
{
	struct pcmcia_socket *s = p_dev->socket;
	config_t *c = p_dev->function_config;
	int ret = -EINVAL;

	mutex_lock(&s->ops_mutex);
	dev_dbg(&p_dev->dev, "pcmcia_request_io: %pR , %pR",
		&c->io[0], &c->io[1]);

	if (!(s->state & SOCKET_PRESENT)) {
		dev_dbg(&p_dev->dev, "pcmcia_request_io: No card present\n");
		goto out;
	}

	if (c->state & CONFIG_LOCKED) {
		dev_dbg(&p_dev->dev, "Configuration is locked\n");
		goto out;
	}
	if (c->state & CONFIG_IO_REQ) {
		dev_dbg(&p_dev->dev, "IO already configured\n");
		goto out;
	}

	ret = alloc_io_space(s, &c->io[0], p_dev->io_lines);
	if (ret)
		goto out;

	if (c->io[1].end) {
		ret = alloc_io_space(s, &c->io[1], p_dev->io_lines);
		if (ret) {
			struct resource tmp = c->io[0];
			/* release the previously allocated resource */
			release_io_space(s, &c->io[0]);
			/* but preserve the settings, for they worked... */
			c->io[0].end = resource_size(&tmp);
			c->io[0].start = tmp.start;
			c->io[0].flags = tmp.flags;
			goto out;
		}
	} else
		c->io[1].start = 0;

	c->state |= CONFIG_IO_REQ;
	p_dev->_io = 1;

	dev_dbg(&p_dev->dev, "pcmcia_request_io succeeded: %pR , %pR",
		&c->io[0], &c->io[1]);
out:
	mutex_unlock(&s->ops_mutex);

	return ret;
} /* pcmcia_request_io */
EXPORT_SYMBOL(pcmcia_request_io);


/** pcmcia_request_irq
 *
 * Request_irq() reserves an irq for this client.
 *
 * Also, since Linux only reserves irq's when they are actually
 * hooked, we don't guarantee that an irq will still be available
 * when the configuration is locked.  Now that I think about it,
 * there might be a way to fix this using a dummy handler.
 */

#ifdef CONFIG_PCMCIA_PROBE
/**
 * pcmcia_request_irq() - attempt to request a IRQ for a PCMCIA device
 * @p_dev: the associated PCMCIA device
 * @handler: IRQ handler to register
 *
 * pcmcia_request_irq() is a wrapper around request_irq() which allows
 * the PCMCIA core to clean up the registration in pcmcia_disable_device().
 * Drivers are free to use request_irq() directly, but then they need to
 * call free_irq() themselfves, too. Also, only %IRQF_SHARED capable IRQ
 * handlers are allowed.
 */
int __must_check pcmcia_request_irq(struct pcmcia_device *p_dev,
				    irq_handler_t handler)
{
	int ret;

	if (!p_dev->irq)
		return -EINVAL;

	ret = request_irq(p_dev->irq, handler, IRQF_SHARED,
			p_dev->devname, p_dev->priv);
	if (!ret)
		p_dev->_irq = 1;

	return ret;
}
EXPORT_SYMBOL(pcmcia_request_irq);


/**
 * pcmcia_request_exclusive_irq() - attempt to request an exclusive IRQ first
 * @p_dev: the associated PCMCIA device
 * @handler: IRQ handler to register
 *
 * pcmcia_request_exclusive_irq() is a wrapper around request_irq() which
 * attempts first to request an exclusive IRQ. If it fails, it also accepts
 * a shared IRQ, but prints out a warning. PCMCIA drivers should allow for
 * IRQ sharing and either use request_irq directly (then they need to call
 * free_irq() themselves, too), or the pcmcia_request_irq() function.
 */
int __must_check
__pcmcia_request_exclusive_irq(struct pcmcia_device *p_dev,
			irq_handler_t handler)
{
	int ret;

	if (!p_dev->irq)
		return -EINVAL;

	ret = request_irq(p_dev->irq, handler, 0, p_dev->devname, p_dev->priv);
	if (ret) {
		ret = pcmcia_request_irq(p_dev, handler);
		dev_warn(&p_dev->dev, "pcmcia: request for exclusive IRQ could not be fulfilled\n");
		dev_warn(&p_dev->dev, "pcmcia: the driver needs updating to supported shared IRQ lines\n");
	}
	if (ret)
		dev_info(&p_dev->dev, "request_irq() failed\n");
	else
		p_dev->_irq = 1;

	return ret;
} /* pcmcia_request_exclusive_irq */
EXPORT_SYMBOL(__pcmcia_request_exclusive_irq);


#ifdef CONFIG_PCMCIA_PROBE

/* mask of IRQs already reserved by other cards, we should avoid using them */
static u8 pcmcia_used_irq[32];

static irqreturn_t test_action(int cpl, void *dev_id)
{
	return IRQ_NONE;
}
#endif

int pcmcia_request_irq(struct pcmcia_device *p_dev, irq_req_t *req)
{
	struct pcmcia_socket *s = p_dev->socket;
	config_t *c;
	int ret = CS_IN_USE, irq = 0;
	int type;

	if (!(s->state & SOCKET_PRESENT))
		return CS_NO_CARD;
	c = p_dev->function_config;
	if (c->state & CONFIG_LOCKED)
		return CS_CONFIGURATION_LOCKED;
	if (c->state & CONFIG_IRQ_REQ)
		return CS_IN_USE;

	/* Decide what type of interrupt we are registering */
	type = 0;
	if (s->functions > 1)		/* All of this ought to be handled higher up */
		type = IRQF_SHARED;
	if (req->Attributes & IRQ_TYPE_DYNAMIC_SHARING)
		type = IRQF_SHARED;

#ifdef CONFIG_PCMCIA_PROBE

#ifdef IRQ_NOAUTOEN
	/* if the underlying IRQ infrastructure allows for it, only allocate
	 * the IRQ, but do not enable it
	 */
	if (!(req->Attributes & IRQ_HANDLE_PRESENT))
		type |= IRQ_NOAUTOEN;
#endif /* IRQ_NOAUTOEN */

	if (s->irq.AssignedIRQ != 0) {
		/* If the interrupt is already assigned, it must be the same */
		irq = s->irq.AssignedIRQ;
	} else {
		int try;
		u32 mask = s->irq_mask;
		void *data = &p_dev->dev.driver; /* something unique to this device */

		for (try = 0; try < 64; try++) {
			irq = try % 32;

			/* marked as available by driver, and not blocked by userspace? */
			if (!((mask >> irq) & 1))
				continue;

			/* avoid an IRQ which is already used by a PCMCIA card */
			if ((try < 32) && pcmcia_used_irq[irq])
				continue;

			/* register the correct driver, if possible, of check whether
			 * registering a dummy handle works, i.e. if the IRQ isn't
			 * marked as used by the kernel resource management core */
			ret = request_irq(irq,
					  (req->Attributes & IRQ_HANDLE_PRESENT) ? req->Handler : test_action,
					  type,
					  p_dev->devname,
					  (req->Attributes & IRQ_HANDLE_PRESENT) ? req->Instance : data);
			if (!ret) {
				if (!(req->Attributes & IRQ_HANDLE_PRESENT))
					free_irq(irq, data);
				break;
			}
		}
	}
#endif
	/* only assign PCI irq if no IRQ already assigned */
	if (ret && !s->irq.AssignedIRQ) {
		if (!s->pci_irq)
			return ret;
		type = IRQF_SHARED;
		irq = s->pci_irq;
	}

	if (ret && (req->Attributes & IRQ_HANDLE_PRESENT)) {
		if (request_irq(irq, req->Handler, type,  p_dev->devname, req->Instance))
			return CS_IN_USE;
	}

	/* Make sure the fact the request type was overridden is passed back */
	if (type == IRQF_SHARED && !(req->Attributes & IRQ_TYPE_DYNAMIC_SHARING)) {
		req->Attributes |= IRQ_TYPE_DYNAMIC_SHARING;
		printk(KERN_WARNING "pcmcia: request for exclusive IRQ could not be fulfilled.\n");
		printk(KERN_WARNING "pcmcia: the driver needs updating to supported shared IRQ lines.\n");
	}
	c->irq.Attributes = req->Attributes;
	s->irq.AssignedIRQ = req->AssignedIRQ = irq;
	s->irq.Config++;

	c->state |= CONFIG_IRQ_REQ;
	p_dev->_irq = 1;

#ifdef CONFIG_PCMCIA_PROBE
	pcmcia_used_irq[irq]++;
#endif

	return CS_SUCCESS;
} /* pcmcia_request_irq */
EXPORT_SYMBOL(pcmcia_request_irq);


/** pcmcia_request_window
 *
 * Request_window() establishes a mapping between card memory space
 * and system memory space.
 */
int pcmcia_request_window(struct pcmcia_device **p_dev, win_req_t *req, window_handle_t *wh)
{
	struct pcmcia_socket *s = (*p_dev)->socket;
	window_t *win;
	u_long align;
	int w;

	if (!(s->state & SOCKET_PRESENT))
		return CS_NO_CARD;
	if (req->Attributes & (WIN_PAGED | WIN_SHARED))
		return CS_BAD_ATTRIBUTE;

	/* Window size defaults to smallest available */
	if (req->Size == 0)
		req->Size = s->map_size;
	align = (((s->features & SS_CAP_MEM_ALIGN) ||
		  (req->Attributes & WIN_STRICT_ALIGN)) ?
		 req->Size : s->map_size);
	if (req->Size & (s->map_size-1))
		return CS_BAD_SIZE;
	if ((req->Base && (s->features & SS_CAP_STATIC_MAP)) ||
	    (req->Base & (align-1)))
		return CS_BAD_BASE;
	if (req->Base)
		align = 0;

	/* Allocate system memory window */
	for (w = 0; w < MAX_WIN; w++)
		if (!(s->state & SOCKET_WIN_REQ(w))) break;
	if (w == MAX_WIN)
		return CS_OUT_OF_RESOURCE;

	win = &s->win[w];
	win->magic = WINDOW_MAGIC;
	win->index = w;
	win->handle = *p_dev;
	win->sock = s;

	if (!(s->features & SS_CAP_STATIC_MAP)) {
		win->ctl.res = pcmcia_find_mem_region(req->Base, req->Size, align,
						      (req->Attributes & WIN_MAP_BELOW_1MB), s);
		if (!win->ctl.res)
			return CS_IN_USE;
	}
	(*p_dev)->_win |= CLIENT_WIN_REQ(w);

	/* Configure the socket controller */
	win->ctl.map = w+1;
	win->ctl.flags = 0;
	win->ctl.speed = req->AccessSpeed;
	if (req->Attributes & WIN_MEMORY_TYPE)
		win->ctl.flags |= MAP_ATTRIB;
	if (req->Attributes & WIN_ENABLE)
		win->ctl.flags |= MAP_ACTIVE;
	if (req->Attributes & WIN_DATA_WIDTH_16)
		win->ctl.flags |= MAP_16BIT;
	if (req->Attributes & WIN_USE_WAIT)
		win->ctl.flags |= MAP_USE_WAIT;
	win->ctl.card_start = 0;
	if (s->ops->set_mem_map(s, &win->ctl) != 0)
		return CS_BAD_ARGS;
	s->state |= SOCKET_WIN_REQ(w);

	/* Return window handle */
	if (s->features & SS_CAP_STATIC_MAP) {
		req->Base = win->ctl.static_start;
	} else {
		req->Base = win->ctl.res->start;
	}
	*wh = win;

	return CS_SUCCESS;
} /* pcmcia_request_window */
EXPORT_SYMBOL(pcmcia_request_window);

void pcmcia_disable_device(struct pcmcia_device *p_dev) {
	pcmcia_release_configuration(p_dev);
	pcmcia_release_io(p_dev, &p_dev->io);
	pcmcia_release_irq(p_dev, &p_dev->irq);
	if (p_dev->win)
		pcmcia_release_window(p_dev->win);

/**
 * pcmcia_setup_isa_irq() - determine whether an ISA IRQ can be used
 * @p_dev - the associated PCMCIA device
 *
 * locking note: must be called with ops_mutex locked.
 */
static int pcmcia_setup_isa_irq(struct pcmcia_device *p_dev, int type)
{
	struct pcmcia_socket *s = p_dev->socket;
	unsigned int try, irq;
	u32 mask = s->irq_mask;
	int ret = -ENODEV;

	for (try = 0; try < 64; try++) {
		irq = try % 32;

		if (irq > NR_IRQS)
			continue;

		/* marked as available by driver, not blocked by userspace? */
		if (!((mask >> irq) & 1))
			continue;

		/* avoid an IRQ which is already used by another PCMCIA card */
		if ((try < 32) && pcmcia_used_irq[irq])
			continue;

		/* register the correct driver, if possible, to check whether
		 * registering a dummy handle works, i.e. if the IRQ isn't
		 * marked as used by the kernel resource management core */
		ret = request_irq(irq, test_action, type, p_dev->devname,
				  p_dev);
		if (!ret) {
			free_irq(irq, p_dev);
			p_dev->irq = s->pcmcia_irq = irq;
			pcmcia_used_irq[irq]++;
			break;
		}
	}

	return ret;
}

void pcmcia_cleanup_irq(struct pcmcia_socket *s)
{
	pcmcia_used_irq[s->pcmcia_irq]--;
	s->pcmcia_irq = 0;
}

#else /* CONFIG_PCMCIA_PROBE */

static int pcmcia_setup_isa_irq(struct pcmcia_device *p_dev, int type)
{
	return -EINVAL;
}

void pcmcia_cleanup_irq(struct pcmcia_socket *s)
{
	s->pcmcia_irq = 0;
	return;
}

#endif  /* CONFIG_PCMCIA_PROBE */


/**
 * pcmcia_setup_irq() - determine IRQ to be used for device
 * @p_dev - the associated PCMCIA device
 *
 * locking note: must be called with ops_mutex locked.
 */
int pcmcia_setup_irq(struct pcmcia_device *p_dev)
{
	struct pcmcia_socket *s = p_dev->socket;

	if (p_dev->irq)
		return 0;

	/* already assigned? */
	if (s->pcmcia_irq) {
		p_dev->irq = s->pcmcia_irq;
		return 0;
	}

	/* prefer an exclusive ISA irq */
	if (!pcmcia_setup_isa_irq(p_dev, 0))
		return 0;

	/* but accept a shared ISA irq */
	if (!pcmcia_setup_isa_irq(p_dev, IRQF_SHARED))
		return 0;

	/* but use the PCI irq otherwise */
	if (s->pci_irq) {
		p_dev->irq = s->pcmcia_irq = s->pci_irq;
		return 0;
	}

	return -EINVAL;
}


/**
 * pcmcia_request_window() - attempt to reserve iomem for PCMCIA devices
 * @p_dev: the associated PCMCIA device
 * @res: &struct resource pointing to p_dev->resource[2..5]
 * @speed: access speed
 *
 * pcmcia_request_window() attepts to reserve an iomem ranges specified in
 * &struct resource @res pointing to one of the entries in
 * &struct pcmcia_device @p_dev->resource[2..5]. The "start" value is the
 * requested start of the IO mem resource; "end" reflects the size
 * requested.
 */
int pcmcia_request_window(struct pcmcia_device *p_dev, struct resource *res,
			unsigned int speed)
{
	struct pcmcia_socket *s = p_dev->socket;
	pccard_mem_map *win;
	u_long align;
	int w;

	dev_dbg(&p_dev->dev, "request_window %pR %d\n", res, speed);

	if (!(s->state & SOCKET_PRESENT)) {
		dev_dbg(&p_dev->dev, "No card present\n");
		return -ENODEV;
	}

	/* Window size defaults to smallest available */
	if (res->end == 0)
		res->end = s->map_size;
	align = (s->features & SS_CAP_MEM_ALIGN) ? res->end : s->map_size;
	if (res->end & (s->map_size-1)) {
		dev_dbg(&p_dev->dev, "invalid map size\n");
		return -EINVAL;
	}
	if ((res->start && (s->features & SS_CAP_STATIC_MAP)) ||
	    (res->start & (align-1))) {
		dev_dbg(&p_dev->dev, "invalid base address\n");
		return -EINVAL;
	}
	if (res->start)
		align = 0;

	/* Allocate system memory window */
	mutex_lock(&s->ops_mutex);
	for (w = 0; w < MAX_WIN; w++)
		if (!(s->state & SOCKET_WIN_REQ(w)))
			break;
	if (w == MAX_WIN) {
		dev_dbg(&p_dev->dev, "all windows are used already\n");
		mutex_unlock(&s->ops_mutex);
		return -EINVAL;
	}

	win = &s->win[w];

	if (!(s->features & SS_CAP_STATIC_MAP)) {
		win->res = pcmcia_find_mem_region(res->start, res->end, align,
						0, s);
		if (!win->res) {
			dev_dbg(&p_dev->dev, "allocating mem region failed\n");
			mutex_unlock(&s->ops_mutex);
			return -EINVAL;
		}
	}
	p_dev->_win |= CLIENT_WIN_REQ(w);

	/* Configure the socket controller */
	win->map = w+1;
	win->flags = res->flags & WIN_FLAGS_MAP;
	win->speed = speed;
	win->card_start = 0;

	if (s->ops->set_mem_map(s, win) != 0) {
		dev_dbg(&p_dev->dev, "failed to set memory mapping\n");
		mutex_unlock(&s->ops_mutex);
		return -EIO;
	}
	s->state |= SOCKET_WIN_REQ(w);

	/* Return window handle */
	if (s->features & SS_CAP_STATIC_MAP)
		res->start = win->static_start;
	else
		res->start = win->res->start;

	/* convert to new-style resources */
	res->end += res->start - 1;
	res->flags &= ~WIN_FLAGS_REQ;
	res->flags |= (win->map << 2) | IORESOURCE_MEM;
	res->parent = win->res;
	if (win->res)
		request_resource(&iomem_resource, res);

	dev_dbg(&p_dev->dev, "request_window results in %pR\n", res);

	mutex_unlock(&s->ops_mutex);

	return 0;
} /* pcmcia_request_window */
EXPORT_SYMBOL(pcmcia_request_window);


/**
 * pcmcia_disable_device() - disable and clean up a PCMCIA device
 * @p_dev: the associated PCMCIA device
 *
 * pcmcia_disable_device() is the driver-callable counterpart to
 * pcmcia_enable_device(): If a PCMCIA device is no longer used,
 * drivers are expected to clean up and disable the device by calling
 * this function. Any I/O ranges (iomem and ioports) will be released,
 * the Vpp voltage will be set to 0, and IRQs will no longer be
 * generated -- at least if there is no other card function (of
 * multifunction devices) being used.
 */
void pcmcia_disable_device(struct pcmcia_device *p_dev)
{
	int i;

	dev_dbg(&p_dev->dev, "disabling device\n");

	for (i = 0; i < MAX_WIN; i++) {
		struct resource *res = p_dev->resource[MAX_IO_WIN + i];
		if (res->flags & WIN_FLAGS_REQ)
			pcmcia_release_window(p_dev, res);
	}

	pcmcia_release_configuration(p_dev);
	pcmcia_release_io(p_dev);
	if (p_dev->_irq) {
		free_irq(p_dev->irq, p_dev->priv);
		p_dev->_irq = 0;
	}
}
EXPORT_SYMBOL(pcmcia_disable_device);
