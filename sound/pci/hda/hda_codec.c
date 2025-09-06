/*
 * Universal Interface for Intel High Definition Audio Codec
 *
 * Copyright (c) 2004 Takashi Iwai <tiwai@suse.de>
 *
 *
 *  This driver is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This driver is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/async.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <sound/core.h>
#include "hda_codec.h"
#include <sound/asoundef.h>
#include <sound/tlv.h>
#include <sound/initval.h>
#include "hda_local.h"
#include <sound/hda_hwdep.h>
#include "hda_patch.h"	/* codec presets */

#ifdef CONFIG_SND_HDA_POWER_SAVE
/* define this option here to hide as static */
static int power_save = CONFIG_SND_HDA_POWER_SAVE_DEFAULT;
module_param(power_save, int, 0644);
MODULE_PARM_DESC(power_save, "Automatic power-saving timeout "
		 "(in second, 0 = disable).");
#endif

/*
 * vendor / preset table
 */

struct hda_vendor_id {
	unsigned int id;
	const char *name;
};

/* codec vendor labels */
static struct hda_vendor_id hda_vendor_ids[] = {
	{ 0x1002, "ATI" },
	{ 0x1057, "Motorola" },
	{ 0x1095, "Silicon Image" },
	{ 0x10ec, "Realtek" },
	{ 0x1106, "VIA" },
	{ 0x111d, "IDT" },
	{ 0x11c1, "LSI" },
	{ 0x11d4, "Analog Devices" },
	{ 0x13f6, "C-Media" },
	{ 0x14f1, "Conexant" },
	{ 0x17e8, "Chrontel" },
	{ 0x1854, "LG" },
	{ 0x434d, "C-Media" },
	{ 0x8384, "SigmaTel" },
	{} /* terminator */
};

static const struct hda_codec_preset *hda_preset_tables[] = {
#ifdef CONFIG_SND_HDA_CODEC_REALTEK
	snd_hda_preset_realtek,
#endif
#ifdef CONFIG_SND_HDA_CODEC_CMEDIA
	snd_hda_preset_cmedia,
#endif
#ifdef CONFIG_SND_HDA_CODEC_ANALOG
	snd_hda_preset_analog,
#endif
#ifdef CONFIG_SND_HDA_CODEC_SIGMATEL
	snd_hda_preset_sigmatel,
#endif
#ifdef CONFIG_SND_HDA_CODEC_SI3054
	snd_hda_preset_si3054,
#endif
#ifdef CONFIG_SND_HDA_CODEC_ATIHDMI
	snd_hda_preset_atihdmi,
#endif
#ifdef CONFIG_SND_HDA_CODEC_CONEXANT
	snd_hda_preset_conexant,
#endif
#ifdef CONFIG_SND_HDA_CODEC_VIA
	snd_hda_preset_via,
#endif
	NULL
};

#ifdef CONFIG_SND_HDA_POWER_SAVE
static void hda_power_work(struct work_struct *work);
static void hda_keep_power_on(struct hda_codec *codec);
#else
static inline void hda_keep_power_on(struct hda_codec *codec) {}
#endif

/**
 * snd_hda_codec_read - send a command and get the response
 * @codec: the HDA codec
 * @nid: NID to send the command
 * @direct: direct flag
 * @verb: the verb to send
 * @parm: the parameter for the verb
 *
 * Send a single command and read the corresponding response.
 *
 * Returns the obtained response value, or -1 for an error.
 */
unsigned int snd_hda_codec_read(struct hda_codec *codec, hda_nid_t nid,
				int direct,
				unsigned int verb, unsigned int parm)
{
	unsigned int res;
	snd_hda_power_up(codec);
	mutex_lock(&codec->bus->cmd_mutex);
	if (!codec->bus->ops.command(codec, nid, direct, verb, parm))
		res = codec->bus->ops.get_response(codec);
	else
		res = (unsigned int)-1;
	mutex_unlock(&codec->bus->cmd_mutex);
	snd_hda_power_down(codec);
	return res;
}

/**
 * snd_hda_codec_write - send a single command without waiting for response
 * @codec: the HDA codec
 * @nid: NID to send the command
 * @direct: direct flag
 * @verb: the verb to send
 * @parm: the parameter for the verb
 *
 * Send a single command without waiting for response.
 *
 * Returns 0 if successful, or a negative error code.
 */
int snd_hda_codec_write(struct hda_codec *codec, hda_nid_t nid, int direct,
			 unsigned int verb, unsigned int parm)
{
	int err;
	snd_hda_power_up(codec);
	mutex_lock(&codec->bus->cmd_mutex);
	err = codec->bus->ops.command(codec, nid, direct, verb, parm);
	mutex_unlock(&codec->bus->cmd_mutex);
	snd_hda_power_down(codec);
#include <sound/jack.h>
#include "hda_local.h"
#include "hda_beep.h"
#include "hda_jack.h"
#include <sound/hda_hwdep.h>

#ifdef CONFIG_PM
#define codec_in_pm(codec)	atomic_read(&(codec)->core.in_pm)
#define hda_codec_is_power_on(codec) \
	(!pm_runtime_suspended(hda_codec_dev(codec)))
#else
#define codec_in_pm(codec)	0
#define hda_codec_is_power_on(codec)	1
#endif

#define codec_has_epss(codec) \
	((codec)->core.power_caps & AC_PWRST_EPSS)
#define codec_has_clkstop(codec) \
	((codec)->core.power_caps & AC_PWRST_CLKSTOP)

/*
 * Send and receive a verb - passed to exec_verb override for hdac_device
 */
static int codec_exec_verb(struct hdac_device *dev, unsigned int cmd,
			   unsigned int flags, unsigned int *res)
{
	struct hda_codec *codec = container_of(dev, struct hda_codec, core);
	struct hda_bus *bus = codec->bus;
	int err;

	if (cmd == ~0)
		return -1;

 again:
	snd_hda_power_up_pm(codec);
	mutex_lock(&bus->core.cmd_mutex);
	if (flags & HDA_RW_NO_RESPONSE_FALLBACK)
		bus->no_response_fallback = 1;
	err = snd_hdac_bus_exec_verb_unlocked(&bus->core, codec->core.addr,
					      cmd, res);
	bus->no_response_fallback = 0;
	mutex_unlock(&bus->core.cmd_mutex);
	snd_hda_power_down_pm(codec);
	if (!codec_in_pm(codec) && res && err == -EAGAIN) {
		if (bus->response_reset) {
			codec_dbg(codec,
				  "resetting BUS due to fatal communication error\n");
			snd_hda_bus_reset(bus);
		}
		goto again;
	}
	/* clear reset-flag when the communication gets recovered */
	if (!err || codec_in_pm(codec))
		bus->response_reset = 0;
	return err;
}

/**
 * snd_hda_sequence_write - sequence writes
 * @codec: the HDA codec
 * @seq: VERB array to send
 *
 * Send the commands sequentially from the given array.
 * The array must be terminated with NID=0.
 */
void snd_hda_sequence_write(struct hda_codec *codec, const struct hda_verb *seq)
{
	for (; seq->nid; seq++)
		snd_hda_codec_write(codec, seq->nid, 0, seq->verb, seq->param);
}

/**
 * snd_hda_get_sub_nodes - get the range of sub nodes
 * @codec: the HDA codec
 * @nid: NID to parse
 * @start_id: the pointer to store the start NID
 *
 * Parse the NID and store the start NID of its sub-nodes.
 * Returns the number of sub-nodes.
 */
int snd_hda_get_sub_nodes(struct hda_codec *codec, hda_nid_t nid,
			  hda_nid_t *start_id)
{
	unsigned int parm;

	parm = snd_hda_param_read(codec, nid, AC_PAR_NODE_COUNT);
	if (parm == -1)
		return 0;
	*start_id = (parm >> 16) & 0x7fff;
	return (int)(parm & 0x7fff);
}

/**
 * snd_hda_get_connections - get connection list
 * @codec: the HDA codec
 * @nid: NID to parse
 * @conn_list: connection list array
EXPORT_SYMBOL_GPL(snd_hda_sequence_write);

/* connection list element */
struct hda_conn_list {
	struct list_head list;
	int len;
	hda_nid_t nid;
	hda_nid_t conns[0];
};

/* look up the cached results */
static struct hda_conn_list *
lookup_conn_list(struct hda_codec *codec, hda_nid_t nid)
{
	struct hda_conn_list *p;
	list_for_each_entry(p, &codec->conn_list, list) {
		if (p->nid == nid)
			return p;
	}
	return NULL;
}

static int add_conn_list(struct hda_codec *codec, hda_nid_t nid, int len,
			 const hda_nid_t *list)
{
	struct hda_conn_list *p;

	p = kmalloc(sizeof(*p) + len * sizeof(hda_nid_t), GFP_KERNEL);
	if (!p)
		return -ENOMEM;
	p->len = len;
	p->nid = nid;
	memcpy(p->conns, list, len * sizeof(hda_nid_t));
	list_add(&p->list, &codec->conn_list);
	return 0;
}

static void remove_conn_list(struct hda_codec *codec)
{
	while (!list_empty(&codec->conn_list)) {
		struct hda_conn_list *p;
		p = list_first_entry(&codec->conn_list, typeof(*p), list);
		list_del(&p->list);
		kfree(p);
	}
}

/* read the connection and add to the cache */
static int read_and_add_raw_conns(struct hda_codec *codec, hda_nid_t nid)
{
	hda_nid_t list[32];
	hda_nid_t *result = list;
	int len;

	len = snd_hda_get_raw_connections(codec, nid, list, ARRAY_SIZE(list));
	if (len == -ENOSPC) {
		len = snd_hda_get_num_raw_conns(codec, nid);
		result = kmalloc(sizeof(hda_nid_t) * len, GFP_KERNEL);
		if (!result)
			return -ENOMEM;
		len = snd_hda_get_raw_connections(codec, nid, result, len);
	}
	if (len >= 0)
		len = snd_hda_override_conn_list(codec, nid, len, result);
	if (result != list)
		kfree(result);
	return len;
}

/**
 * snd_hda_get_conn_list - get connection list
 * @codec: the HDA codec
 * @nid: NID to parse
 * @listp: the pointer to store NID list
 *
 * Parses the connection list of the given widget and stores the pointer
 * to the list of NIDs.
 *
 * Returns the number of connections, or a negative error code.
 *
 * Note that the returned pointer isn't protected against the list
 * modification.  If snd_hda_override_conn_list() might be called
 * concurrently, protect with a mutex appropriately.
 */
int snd_hda_get_conn_list(struct hda_codec *codec, hda_nid_t nid,
			  const hda_nid_t **listp)
{
	bool added = false;

	for (;;) {
		int err;
		const struct hda_conn_list *p;

		/* if the connection-list is already cached, read it */
		p = lookup_conn_list(codec, nid);
		if (p) {
			if (listp)
				*listp = p->conns;
			return p->len;
		}
		if (snd_BUG_ON(added))
			return -EINVAL;

		err = read_and_add_raw_conns(codec, nid);
		if (err < 0)
			return err;
		added = true;
	}
}
EXPORT_SYMBOL_GPL(snd_hda_get_conn_list);

/**
 * snd_hda_get_connections - copy connection list
 * @codec: the HDA codec
 * @nid: NID to parse
 * @conn_list: connection list array; when NULL, checks only the size
 * @max_conns: max. number of connections to store
 *
 * Parses the connection list of the given widget and stores the list
 * of NIDs.
 *
 * Returns the number of connections, or a negative error code.
 */
int snd_hda_get_connections(struct hda_codec *codec, hda_nid_t nid,
			    hda_nid_t *conn_list, int max_conns)
{
	unsigned int parm;
	int i, conn_len, conns;
	unsigned int shift, num_elems, mask;
	hda_nid_t prev_nid;

	snd_assert(conn_list && max_conns > 0, return -EINVAL);

	parm = snd_hda_param_read(codec, nid, AC_PAR_CONNLIST_LEN);
	if (parm & AC_CLIST_LONG) {
		/* long form */
		shift = 16;
		num_elems = 2;
	} else {
		/* short form */
		shift = 8;
		num_elems = 4;
	}
	conn_len = parm & AC_CLIST_LENGTH;
	mask = (1 << (shift-1)) - 1;

	if (!conn_len)
		return 0; /* no connection */

	if (conn_len == 1) {
		/* single connection */
		parm = snd_hda_codec_read(codec, nid, 0,
					  AC_VERB_GET_CONNECT_LIST, 0);
		conn_list[0] = parm & mask;
		return 1;
	}

	/* multi connection */
	conns = 0;
	prev_nid = 0;
	for (i = 0; i < conn_len; i++) {
		int range_val;
		hda_nid_t val, n;

		if (i % num_elems == 0)
			parm = snd_hda_codec_read(codec, nid, 0,
						  AC_VERB_GET_CONNECT_LIST, i);
		range_val = !!(parm & (1 << (shift-1))); /* ranges */
		val = parm & mask;
		parm >>= shift;
		if (range_val) {
			/* ranges between the previous and this one */
			if (!prev_nid || prev_nid >= val) {
				snd_printk(KERN_WARNING "hda_codec: "
					   "invalid dep_range_val %x:%x\n",
					   prev_nid, val);
				continue;
			}
			for (n = prev_nid + 1; n <= val; n++) {
				if (conns >= max_conns) {
					snd_printk(KERN_ERR
						   "Too many connections\n");
					return -EINVAL;
				}
				conn_list[conns++] = n;
			}
		} else {
			if (conns >= max_conns) {
				snd_printk(KERN_ERR "Too many connections\n");
				return -EINVAL;
			}
			conn_list[conns++] = val;
		}
		prev_nid = val;
	}
	return conns;
}


/**
 * snd_hda_queue_unsol_event - add an unsolicited event to queue
 * @bus: the BUS
 * @res: unsolicited event (lower 32bit of RIRB entry)
 * @res_ex: codec addr and flags (upper 32bit or RIRB entry)
 *
 * Adds the given event to the queue.  The events are processed in
 * the workqueue asynchronously.  Call this function in the interrupt
 * hanlder when RIRB receives an unsolicited event.
 *
 * Returns 0 if successful, or a negative error code.
 */
int snd_hda_queue_unsol_event(struct hda_bus *bus, u32 res, u32 res_ex)
{
	struct hda_bus_unsolicited *unsol;
	unsigned int wp;

	unsol = bus->unsol;
	if (!unsol)
		return 0;

	wp = (unsol->wp + 1) % HDA_UNSOL_QUEUE_SIZE;
	unsol->wp = wp;

	wp <<= 1;
	unsol->queue[wp] = res;
	unsol->queue[wp + 1] = res_ex;

	schedule_work(&unsol->work);

	return 0;
}

/*
 * process queueud unsolicited events
 */
static void process_unsol_events(struct work_struct *work)
{
	struct hda_bus_unsolicited *unsol =
		container_of(work, struct hda_bus_unsolicited, work);
	struct hda_bus *bus = unsol->bus;
	struct hda_codec *codec;
	unsigned int rp, caddr, res;

	while (unsol->rp != unsol->wp) {
		rp = (unsol->rp + 1) % HDA_UNSOL_QUEUE_SIZE;
		unsol->rp = rp;
		rp <<= 1;
		res = unsol->queue[rp];
		caddr = unsol->queue[rp + 1];
		if (!(caddr & (1 << 4))) /* no unsolicited event? */
			continue;
		codec = bus->caddr_tbl[caddr & 0x0f];
		if (codec && codec->patch_ops.unsol_event)
			codec->patch_ops.unsol_event(codec, res);
	}
}

/*
 * initialize unsolicited queue
 */
static int __devinit init_unsol_queue(struct hda_bus *bus)
{
	struct hda_bus_unsolicited *unsol;

	if (bus->unsol) /* already initialized */
		return 0;

	unsol = kzalloc(sizeof(*unsol), GFP_KERNEL);
	if (!unsol) {
		snd_printk(KERN_ERR "hda_codec: "
			   "can't allocate unsolicited queue\n");
		return -ENOMEM;
	}
	INIT_WORK(&unsol->work, process_unsol_events);
	unsol->bus = bus;
	bus->unsol = unsol;
	return 0;
}

/*
 * destructor
 */
static void snd_hda_codec_free(struct hda_codec *codec);

static int snd_hda_bus_free(struct hda_bus *bus)
{
	struct hda_codec *codec, *n;

	if (!bus)
		return 0;
	if (bus->unsol) {
		flush_scheduled_work();
		kfree(bus->unsol);
	}
	list_for_each_entry_safe(codec, n, &bus->codec_list, list) {
		snd_hda_codec_free(codec);
	}
	if (bus->ops.private_free)
		bus->ops.private_free(bus);
	kfree(bus);
	return 0;
}

static int snd_hda_bus_dev_free(struct snd_device *device)
{
	struct hda_bus *bus = device->device_data;
	return snd_hda_bus_free(bus);
}

/**
 * snd_hda_bus_new - create a HDA bus
 * @card: the card entry
 * @temp: the template for hda_bus information
 * @busp: the pointer to store the created bus instance
 *
 * Returns 0 if successful, or a negative error code.
 */
int __devinit snd_hda_bus_new(struct snd_card *card,
			      const struct hda_bus_template *temp,
			      struct hda_bus **busp)
{
	struct hda_bus *bus;
	int err;
	static struct snd_device_ops dev_ops = {
		.dev_free = snd_hda_bus_dev_free,
	};

	snd_assert(temp, return -EINVAL);
	snd_assert(temp->ops.command && temp->ops.get_response, return -EINVAL);

	if (busp)
		*busp = NULL;

	bus = kzalloc(sizeof(*bus), GFP_KERNEL);
	if (bus == NULL) {
		snd_printk(KERN_ERR "can't allocate struct hda_bus\n");
		return -ENOMEM;
	}

	bus->card = card;
	bus->private_data = temp->private_data;
	bus->pci = temp->pci;
	bus->modelname = temp->modelname;
	bus->ops = temp->ops;

	mutex_init(&bus->cmd_mutex);
	INIT_LIST_HEAD(&bus->codec_list);

	err = snd_device_new(card, SNDRV_DEV_BUS, bus, &dev_ops);
	if (err < 0) {
		snd_hda_bus_free(bus);
		return err;
	}
	if (busp)
		*busp = bus;
	return 0;
}

#ifdef CONFIG_SND_HDA_GENERIC
#define is_generic_config(codec) \
	(codec->bus->modelname && !strcmp(codec->bus->modelname, "generic"))
#else
#define is_generic_config(codec)	0
#endif

/*
 * find a matching codec preset
 */
static const struct hda_codec_preset __devinit *
find_codec_preset(struct hda_codec *codec)
{
	const struct hda_codec_preset **tbl, *preset;

	if (is_generic_config(codec))
		return NULL; /* use the generic parser */

	for (tbl = hda_preset_tables; *tbl; tbl++) {
		for (preset = *tbl; preset->id; preset++) {
			u32 mask = preset->mask;
			if (preset->afg && preset->afg != codec->afg)
				continue;
			if (preset->mfg && preset->mfg != codec->mfg)
				continue;
			if (!mask)
				mask = ~0;
			if (preset->id == (codec->vendor_id & mask) &&
			    (!preset->rev ||
			     preset->rev == codec->revision_id))
				return preset;
		}
	}
	return NULL;
}

/*
 * snd_hda_get_codec_name - store the codec name
 */
void snd_hda_get_codec_name(struct hda_codec *codec,
			    char *name, int namelen)
{
	const struct hda_vendor_id *c;
	const char *vendor = NULL;
	u16 vendor_id = codec->vendor_id >> 16;
	char tmp[16];

	for (c = hda_vendor_ids; c->id; c++) {
		if (c->id == vendor_id) {
			vendor = c->name;
			break;
		}
	}
	if (!vendor) {
		sprintf(tmp, "Generic %04x", vendor_id);
		vendor = tmp;
	}
	if (codec->preset && codec->preset->name)
		snprintf(name, namelen, "%s %s", vendor, codec->preset->name);
	else
		snprintf(name, namelen, "%s ID %x", vendor,
			 codec->vendor_id & 0xffff);
}

/*
 * look for an AFG and MFG nodes
 */
static void __devinit setup_fg_nodes(struct hda_codec *codec)
{
	int i, total_nodes;
	hda_nid_t nid;

	total_nodes = snd_hda_get_sub_nodes(codec, AC_NODE_ROOT, &nid);
	for (i = 0; i < total_nodes; i++, nid++) {
		unsigned int func;
		func = snd_hda_param_read(codec, nid, AC_PAR_FUNCTION_TYPE);
		switch (func & 0xff) {
		case AC_GRP_AUDIO_FUNCTION:
			codec->afg = nid;
			break;
		case AC_GRP_MODEM_FUNCTION:
			codec->mfg = nid;
			break;
		default:
			break;
		}
	}
	const hda_nid_t *list;
	int len = snd_hda_get_conn_list(codec, nid, &list);

	if (len > 0 && conn_list) {
		if (len > max_conns) {
			codec_err(codec, "Too many connections %d for NID 0x%x\n",
				   len, nid);
			return -EINVAL;
		}
		memcpy(conn_list, list, len * sizeof(hda_nid_t));
	}

	return len;
}
EXPORT_SYMBOL_GPL(snd_hda_get_connections);

/**
 * snd_hda_override_conn_list - add/modify the connection-list to cache
 * @codec: the HDA codec
 * @nid: NID to parse
 * @len: number of connection list entries
 * @list: the list of connection entries
 *
 * Add or modify the given connection-list to the cache.  If the corresponding
 * cache already exists, invalidate it and append a new one.
 *
 * Returns zero or a negative error code.
 */
int snd_hda_override_conn_list(struct hda_codec *codec, hda_nid_t nid, int len,
			       const hda_nid_t *list)
{
	struct hda_conn_list *p;

	p = lookup_conn_list(codec, nid);
	if (p) {
		list_del(&p->list);
		kfree(p);
	}

	return add_conn_list(codec, nid, len, list);
}
EXPORT_SYMBOL_GPL(snd_hda_override_conn_list);

/**
 * snd_hda_get_conn_index - get the connection index of the given NID
 * @codec: the HDA codec
 * @mux: NID containing the list
 * @nid: NID to select
 * @recursive: 1 when searching NID recursively, otherwise 0
 *
 * Parses the connection list of the widget @mux and checks whether the
 * widget @nid is present.  If it is, return the connection index.
 * Otherwise it returns -1.
 */
int snd_hda_get_conn_index(struct hda_codec *codec, hda_nid_t mux,
			   hda_nid_t nid, int recursive)
{
	const hda_nid_t *conn;
	int i, nums;

	nums = snd_hda_get_conn_list(codec, mux, &conn);
	for (i = 0; i < nums; i++)
		if (conn[i] == nid)
			return i;
	if (!recursive)
		return -1;
	if (recursive > 10) {
		codec_dbg(codec, "too deep connection for 0x%x\n", nid);
		return -1;
	}
	recursive++;
	for (i = 0; i < nums; i++) {
		unsigned int type = get_wcaps_type(get_wcaps(codec, conn[i]));
		if (type == AC_WID_PIN || type == AC_WID_AUD_OUT)
			continue;
		if (snd_hda_get_conn_index(codec, conn[i], nid, recursive) >= 0)
			return i;
	}
	return -1;
}
EXPORT_SYMBOL_GPL(snd_hda_get_conn_index);


/* return DEVLIST_LEN parameter of the given widget */
static unsigned int get_num_devices(struct hda_codec *codec, hda_nid_t nid)
{
	unsigned int wcaps = get_wcaps(codec, nid);
	unsigned int parm;

	if (!codec->dp_mst || !(wcaps & AC_WCAP_DIGITAL) ||
	    get_wcaps_type(wcaps) != AC_WID_PIN)
		return 0;

	parm = snd_hdac_read_parm_uncached(&codec->core, nid, AC_PAR_DEVLIST_LEN);
	if (parm == -1)
		parm = 0;
	return parm & AC_DEV_LIST_LEN_MASK;
}

/**
 * snd_hda_get_devices - copy device list without cache
 * @codec: the HDA codec
 * @nid: NID of the pin to parse
 * @dev_list: device list array
 * @max_devices: max. number of devices to store
 *
 * Copy the device list. This info is dynamic and so not cached.
 * Currently called only from hda_proc.c, so not exported.
 */
int snd_hda_get_devices(struct hda_codec *codec, hda_nid_t nid,
			u8 *dev_list, int max_devices)
{
	unsigned int parm;
	int i, dev_len, devices;

	parm = get_num_devices(codec, nid);
	if (!parm)	/* not multi-stream capable */
		return 0;

	dev_len = parm + 1;
	dev_len = dev_len < max_devices ? dev_len : max_devices;

	devices = 0;
	while (devices < dev_len) {
		if (snd_hdac_read(&codec->core, nid,
				  AC_VERB_GET_DEVICE_LIST, devices, &parm))
			break; /* error */

		for (i = 0; i < 8; i++) {
			dev_list[devices] = (u8)parm;
			parm >>= 4;
			devices++;
			if (devices >= dev_len)
				break;
		}
	}
	return devices;
}

/*
 * read widget caps for each widget and store in cache
 */
static int read_widget_caps(struct hda_codec *codec, hda_nid_t fg_node)
{
	int i;
	hda_nid_t nid;

	codec->num_nodes = snd_hda_get_sub_nodes(codec, fg_node,
						 &codec->start_nid);
	codec->wcaps = kmalloc(codec->num_nodes * 4, GFP_KERNEL);
	if (!codec->wcaps)
		return -ENOMEM;
	nid = codec->start_nid;
	for (i = 0; i < codec->num_nodes; i++, nid++)
		codec->wcaps[i] = snd_hda_param_read(codec, nid,
						     AC_PAR_AUDIO_WIDGET_CAP);
	return 0;
}


static void init_hda_cache(struct hda_cache_rec *cache,
			   unsigned int record_size);
static void free_hda_cache(struct hda_cache_rec *cache);
	codec->wcaps = kmalloc(codec->core.num_nodes * 4, GFP_KERNEL);
	if (!codec->wcaps)
		return -ENOMEM;
	nid = codec->core.start_nid;
	for (i = 0; i < codec->core.num_nodes; i++, nid++)
		codec->wcaps[i] = snd_hdac_read_parm_uncached(&codec->core,
					nid, AC_PAR_AUDIO_WIDGET_CAP);
	return 0;
}

/* read all pin default configurations and save codec->init_pins */
static int read_pin_defaults(struct hda_codec *codec)
{
	hda_nid_t nid;

	for_each_hda_codec_node(nid, codec) {
		struct hda_pincfg *pin;
		unsigned int wcaps = get_wcaps(codec, nid);
		unsigned int wid_type = get_wcaps_type(wcaps);
		if (wid_type != AC_WID_PIN)
			continue;
		pin = snd_array_new(&codec->init_pins);
		if (!pin)
			return -ENOMEM;
		pin->nid = nid;
		pin->cfg = snd_hda_codec_read(codec, nid, 0,
					      AC_VERB_GET_CONFIG_DEFAULT, 0);
		pin->ctrl = snd_hda_codec_read(codec, nid, 0,
					       AC_VERB_GET_PIN_WIDGET_CONTROL,
					       0);
	}
	return 0;
}

/* look up the given pin config list and return the item matching with NID */
static struct hda_pincfg *look_up_pincfg(struct hda_codec *codec,
					 struct snd_array *array,
					 hda_nid_t nid)
{
	int i;
	for (i = 0; i < array->used; i++) {
		struct hda_pincfg *pin = snd_array_elem(array, i);
		if (pin->nid == nid)
			return pin;
	}
	return NULL;
}

/* set the current pin config value for the given NID.
 * the value is cached, and read via snd_hda_codec_get_pincfg()
 */
int snd_hda_add_pincfg(struct hda_codec *codec, struct snd_array *list,
		       hda_nid_t nid, unsigned int cfg)
{
	struct hda_pincfg *pin;

	/* the check below may be invalid when pins are added by a fixup
	 * dynamically (e.g. via snd_hda_codec_update_widgets()), so disabled
	 * for now
	 */
	/*
	if (get_wcaps_type(get_wcaps(codec, nid)) != AC_WID_PIN)
		return -EINVAL;
	*/

	pin = look_up_pincfg(codec, list, nid);
	if (!pin) {
		pin = snd_array_new(list);
		if (!pin)
			return -ENOMEM;
		pin->nid = nid;
	}
	pin->cfg = cfg;
	return 0;
}

/**
 * snd_hda_codec_set_pincfg - Override a pin default configuration
 * @codec: the HDA codec
 * @nid: NID to set the pin config
 * @cfg: the pin default config value
 *
 * Override a pin default configuration value in the cache.
 * This value can be read by snd_hda_codec_get_pincfg() in a higher
 * priority than the real hardware value.
 */
int snd_hda_codec_set_pincfg(struct hda_codec *codec,
			     hda_nid_t nid, unsigned int cfg)
{
	return snd_hda_add_pincfg(codec, &codec->driver_pins, nid, cfg);
}
EXPORT_SYMBOL_GPL(snd_hda_codec_set_pincfg);

/**
 * snd_hda_codec_get_pincfg - Obtain a pin-default configuration
 * @codec: the HDA codec
 * @nid: NID to get the pin config
 *
 * Get the current pin config value of the given pin NID.
 * If the pincfg value is cached or overridden via sysfs or driver,
 * returns the cached value.
 */
unsigned int snd_hda_codec_get_pincfg(struct hda_codec *codec, hda_nid_t nid)
{
	struct hda_pincfg *pin;

#ifdef CONFIG_SND_HDA_RECONFIG
	{
		unsigned int cfg = 0;
		mutex_lock(&codec->user_mutex);
		pin = look_up_pincfg(codec, &codec->user_pins, nid);
		if (pin)
			cfg = pin->cfg;
		mutex_unlock(&codec->user_mutex);
		if (cfg)
			return cfg;
	}
#endif
	pin = look_up_pincfg(codec, &codec->driver_pins, nid);
	if (pin)
		return pin->cfg;
	pin = look_up_pincfg(codec, &codec->init_pins, nid);
	if (pin)
		return pin->cfg;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_codec_get_pincfg);

/**
 * snd_hda_codec_set_pin_target - remember the current pinctl target value
 * @codec: the HDA codec
 * @nid: pin NID
 * @val: assigned pinctl value
 *
 * This function stores the given value to a pinctl target value in the
 * pincfg table.  This isn't always as same as the actually written value
 * but can be referred at any time via snd_hda_codec_get_pin_target().
 */
int snd_hda_codec_set_pin_target(struct hda_codec *codec, hda_nid_t nid,
				 unsigned int val)
{
	struct hda_pincfg *pin;

	pin = look_up_pincfg(codec, &codec->init_pins, nid);
	if (!pin)
		return -EINVAL;
	pin->target = val;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_codec_set_pin_target);

/**
 * snd_hda_codec_get_pin_target - return the current pinctl target value
 * @codec: the HDA codec
 * @nid: pin NID
 */
int snd_hda_codec_get_pin_target(struct hda_codec *codec, hda_nid_t nid)
{
	struct hda_pincfg *pin;

	pin = look_up_pincfg(codec, &codec->init_pins, nid);
	if (!pin)
		return 0;
	return pin->target;
}
EXPORT_SYMBOL_GPL(snd_hda_codec_get_pin_target);

/**
 * snd_hda_shutup_pins - Shut up all pins
 * @codec: the HDA codec
 *
 * Clear all pin controls to shup up before suspend for avoiding click noise.
 * The controls aren't cached so that they can be resumed properly.
 */
void snd_hda_shutup_pins(struct hda_codec *codec)
{
	int i;
	/* don't shut up pins when unloading the driver; otherwise it breaks
	 * the default pin setup at the next load of the driver
	 */
	if (codec->bus->shutdown)
		return;
	for (i = 0; i < codec->init_pins.used; i++) {
		struct hda_pincfg *pin = snd_array_elem(&codec->init_pins, i);
		/* use read here for syncing after issuing each verb */
		snd_hda_codec_read(codec, pin->nid, 0,
				   AC_VERB_SET_PIN_WIDGET_CONTROL, 0);
	}
	codec->pins_shutup = 1;
}
EXPORT_SYMBOL_GPL(snd_hda_shutup_pins);

#ifdef CONFIG_PM
/* Restore the pin controls cleared previously via snd_hda_shutup_pins() */
static void restore_shutup_pins(struct hda_codec *codec)
{
	int i;
	if (!codec->pins_shutup)
		return;
	if (codec->bus->shutdown)
		return;
	for (i = 0; i < codec->init_pins.used; i++) {
		struct hda_pincfg *pin = snd_array_elem(&codec->init_pins, i);
		snd_hda_codec_write(codec, pin->nid, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL,
				    pin->ctrl);
	}
	codec->pins_shutup = 0;
}
#endif

static void hda_jackpoll_work(struct work_struct *work)
{
	struct hda_codec *codec =
		container_of(work, struct hda_codec, jackpoll_work.work);

	snd_hda_jack_set_dirty_all(codec);
	snd_hda_jack_poll_all(codec);

	if (!codec->jackpoll_interval)
		return;

	schedule_delayed_work(&codec->jackpoll_work,
			      codec->jackpoll_interval);
}

/* release all pincfg lists */
static void free_init_pincfgs(struct hda_codec *codec)
{
	snd_array_free(&codec->driver_pins);
#ifdef CONFIG_SND_HDA_RECONFIG
	snd_array_free(&codec->user_pins);
#endif
	snd_array_free(&codec->init_pins);
}

/*
 * audio-converter setup caches
 */
struct hda_cvt_setup {
	hda_nid_t nid;
	u8 stream_tag;
	u8 channel_id;
	u16 format_id;
	unsigned char active;	/* cvt is currently used */
	unsigned char dirty;	/* setups should be cleared */
};

/* get or create a cache entry for the given audio converter NID */
static struct hda_cvt_setup *
get_hda_cvt_setup(struct hda_codec *codec, hda_nid_t nid)
{
	struct hda_cvt_setup *p;
	int i;

	for (i = 0; i < codec->cvt_setups.used; i++) {
		p = snd_array_elem(&codec->cvt_setups, i);
		if (p->nid == nid)
			return p;
	}
	p = snd_array_new(&codec->cvt_setups);
	if (p)
		p->nid = nid;
	return p;
}

/*
 * PCM device
 */
static void release_pcm(struct kref *kref)
{
	struct hda_pcm *pcm = container_of(kref, struct hda_pcm, kref);

	if (pcm->pcm)
		snd_device_free(pcm->codec->card, pcm->pcm);
	clear_bit(pcm->device, pcm->codec->bus->pcm_dev_bits);
	kfree(pcm->name);
	kfree(pcm);
}

void snd_hda_codec_pcm_put(struct hda_pcm *pcm)
{
	kref_put(&pcm->kref, release_pcm);
}
EXPORT_SYMBOL_GPL(snd_hda_codec_pcm_put);

struct hda_pcm *snd_hda_codec_pcm_new(struct hda_codec *codec,
				      const char *fmt, ...)
{
	struct hda_pcm *pcm;
	va_list args;

	pcm = kzalloc(sizeof(*pcm), GFP_KERNEL);
	if (!pcm)
		return NULL;

	pcm->codec = codec;
	kref_init(&pcm->kref);
	va_start(args, fmt);
	pcm->name = kvasprintf(GFP_KERNEL, fmt, args);
	va_end(args);
	if (!pcm->name) {
		kfree(pcm);
		return NULL;
	}

	list_add_tail(&pcm->list, &codec->pcm_list_head);
	return pcm;
}
EXPORT_SYMBOL_GPL(snd_hda_codec_pcm_new);

/*
 * codec destructor
 */
static void snd_hda_codec_free(struct hda_codec *codec)
{
	if (!codec)
		return;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	cancel_delayed_work(&codec->power_work);
	flush_scheduled_work();
#endif
	list_del(&codec->list);
	codec->bus->caddr_tbl[codec->addr] = NULL;
	if (codec->patch_ops.free)
		codec->patch_ops.free(codec);
	free_hda_cache(&codec->amp_cache);
	free_hda_cache(&codec->cmd_cache);
static void codec_release_pcms(struct hda_codec *codec)
{
	struct hda_pcm *pcm, *n;

	list_for_each_entry_safe(pcm, n, &codec->pcm_list_head, list) {
		list_del_init(&pcm->list);
		if (pcm->pcm)
			snd_device_disconnect(codec->card, pcm->pcm);
		snd_hda_codec_pcm_put(pcm);
	}
}

void snd_hda_codec_cleanup_for_unbind(struct hda_codec *codec)
{
	if (codec->registered) {
		/* pm_runtime_put() is called in snd_hdac_device_exit() */
		pm_runtime_get_noresume(hda_codec_dev(codec));
		pm_runtime_disable(hda_codec_dev(codec));
		codec->registered = 0;
	}

	cancel_delayed_work_sync(&codec->jackpoll_work);
	if (!codec->in_freeing)
		snd_hda_ctls_clear(codec);
	codec_release_pcms(codec);
	snd_hda_detach_beep_device(codec);
	memset(&codec->patch_ops, 0, sizeof(codec->patch_ops));
	snd_hda_jack_tbl_clear(codec);
	codec->proc_widget_hook = NULL;
	codec->spec = NULL;

	/* free only driver_pins so that init_pins + user_pins are restored */
	snd_array_free(&codec->driver_pins);
	snd_array_free(&codec->cvt_setups);
	snd_array_free(&codec->spdif_out);
	snd_array_free(&codec->verbs);
	codec->preset = NULL;
	codec->slave_dig_outs = NULL;
	codec->spdif_status_reset = 0;
	snd_array_free(&codec->mixers);
	snd_array_free(&codec->nids);
	remove_conn_list(codec);
	snd_hdac_regmap_exit(&codec->core);
}

static unsigned int hda_set_power_state(struct hda_codec *codec,
				unsigned int power_state);

/* also called from hda_bind.c */
void snd_hda_codec_register(struct hda_codec *codec)
{
	if (codec->registered)
		return;
	if (device_is_registered(hda_codec_dev(codec))) {
		snd_hda_register_beep_device(codec);
		snd_hdac_link_power(&codec->core, true);
		pm_runtime_enable(hda_codec_dev(codec));
		/* it was powered up in snd_hda_codec_new(), now all done */
		snd_hda_power_down(codec);
		codec->registered = 1;
	}
}

static int snd_hda_codec_dev_register(struct snd_device *device)
{
	snd_hda_codec_register(device->device_data);
	return 0;
}

static int snd_hda_codec_dev_disconnect(struct snd_device *device)
{
	struct hda_codec *codec = device->device_data;

	snd_hda_detach_beep_device(codec);
	return 0;
}

static int snd_hda_codec_dev_free(struct snd_device *device)
{
	struct hda_codec *codec = device->device_data;

	codec->in_freeing = 1;
	snd_hdac_device_unregister(&codec->core);
	snd_hdac_link_power(&codec->core, false);
	put_device(hda_codec_dev(codec));
	return 0;
}

static void snd_hda_codec_dev_release(struct device *dev)
{
	struct hda_codec *codec = dev_to_hda_codec(dev);

	free_init_pincfgs(codec);
	snd_hdac_device_exit(&codec->core);
	snd_hda_sysfs_clear(codec);
	kfree(codec->modelname);
	kfree(codec->wcaps);
	kfree(codec);
}

/**
 * snd_hda_codec_new - create a HDA codec
 * @bus: the bus to assign
 * @codec_addr: the codec address
 * @codecp: the pointer to store the generated codec
 *
 * Returns 0 if successful, or a negative error code.
 */
int __devinit snd_hda_codec_new(struct hda_bus *bus, unsigned int codec_addr,
				struct hda_codec **codecp)
{
	struct hda_codec *codec;
	char component[13];
	int err;

	snd_assert(bus, return -EINVAL);
	snd_assert(codec_addr <= HDA_MAX_CODEC_ADDRESS, return -EINVAL);

	if (bus->caddr_tbl[codec_addr]) {
		snd_printk(KERN_ERR "hda_codec: "
			   "address 0x%x is already occupied\n", codec_addr);
		return -EBUSY;
	}

	codec = kzalloc(sizeof(*codec), GFP_KERNEL);
	if (codec == NULL) {
		snd_printk(KERN_ERR "can't allocate struct hda_codec\n");
		return -ENOMEM;
	}

	codec->bus = bus;
	codec->addr = codec_addr;
	mutex_init(&codec->spdif_mutex);
	init_hda_cache(&codec->amp_cache, sizeof(struct hda_amp_info));
	init_hda_cache(&codec->cmd_cache, sizeof(struct hda_cache_head));

#ifdef CONFIG_SND_HDA_POWER_SAVE
	INIT_DELAYED_WORK(&codec->power_work, hda_power_work);
	/* snd_hda_codec_new() marks the codec as power-up, and leave it as is.
	 * the caller has to power down appropriatley after initialization
	 * phase.
	 */
	hda_keep_power_on(codec);
#endif

	list_add_tail(&codec->list, &bus->codec_list);
	bus->caddr_tbl[codec_addr] = codec;

	codec->vendor_id = snd_hda_param_read(codec, AC_NODE_ROOT,
					      AC_PAR_VENDOR_ID);
	if (codec->vendor_id == -1)
		/* read again, hopefully the access method was corrected
		 * in the last read...
		 */
		codec->vendor_id = snd_hda_param_read(codec, AC_NODE_ROOT,
						      AC_PAR_VENDOR_ID);
	codec->subsystem_id = snd_hda_param_read(codec, AC_NODE_ROOT,
						 AC_PAR_SUBSYSTEM_ID);
	codec->revision_id = snd_hda_param_read(codec, AC_NODE_ROOT,
						AC_PAR_REV_ID);

	setup_fg_nodes(codec);
	if (!codec->afg && !codec->mfg) {
		snd_printdd("hda_codec: no AFG or MFG node found\n");
		snd_hda_codec_free(codec);
		return -ENODEV;
	}

	if (read_widget_caps(codec, codec->afg ? codec->afg : codec->mfg) < 0) {
		snd_printk(KERN_ERR "hda_codec: cannot malloc\n");
		snd_hda_codec_free(codec);
		return -ENOMEM;
	}

	if (!codec->subsystem_id) {
		hda_nid_t nid = codec->afg ? codec->afg : codec->mfg;
		codec->subsystem_id =
			snd_hda_codec_read(codec, nid, 0,
					   AC_VERB_GET_SUBSYSTEM_ID, 0);
	}

	codec->preset = find_codec_preset(codec);
	/* audio codec should override the mixer name */
	if (codec->afg || !*bus->card->mixername)
		snd_hda_get_codec_name(codec, bus->card->mixername,
				       sizeof(bus->card->mixername));

	if (is_generic_config(codec)) {
		err = snd_hda_parse_generic_codec(codec);
		goto patched;
	}
	if (codec->preset && codec->preset->patch) {
		err = codec->preset->patch(codec);
		goto patched;
	}

	/* call the default parser */
	err = snd_hda_parse_generic_codec(codec);
	if (err < 0)
		printk(KERN_ERR "hda-codec: No codec parser is available\n");

 patched:
	if (err < 0) {
		snd_hda_codec_free(codec);
		return err;
	}

	if (codec->patch_ops.unsol_event)
		init_unsol_queue(bus);

	snd_hda_codec_proc_new(codec);
#ifdef CONFIG_SND_HDA_HWDEP
	snd_hda_create_hwdep(codec);
#endif

	sprintf(component, "HDA:%08x", codec->vendor_id);
	snd_component_add(codec->bus->card, component);
int snd_hda_codec_new(struct hda_bus *bus, struct snd_card *card,
		      unsigned int codec_addr, struct hda_codec **codecp)
{
	struct hda_codec *codec;
	char component[31];
	hda_nid_t fg;
	int err;
	static struct snd_device_ops dev_ops = {
		.dev_register = snd_hda_codec_dev_register,
		.dev_disconnect = snd_hda_codec_dev_disconnect,
		.dev_free = snd_hda_codec_dev_free,
	};

	if (snd_BUG_ON(!bus))
		return -EINVAL;
	if (snd_BUG_ON(codec_addr > HDA_MAX_CODEC_ADDRESS))
		return -EINVAL;

	codec = kzalloc(sizeof(*codec), GFP_KERNEL);
	if (!codec)
		return -ENOMEM;

	sprintf(component, "hdaudioC%dD%d", card->number, codec_addr);
	err = snd_hdac_device_init(&codec->core, &bus->core, component,
				   codec_addr);
	if (err < 0) {
		kfree(codec);
		return err;
	}

	codec->core.dev.release = snd_hda_codec_dev_release;
	codec->core.type = HDA_DEV_LEGACY;
	codec->core.exec_verb = codec_exec_verb;

	codec->bus = bus;
	codec->card = card;
	codec->addr = codec_addr;
	mutex_init(&codec->spdif_mutex);
	mutex_init(&codec->control_mutex);
	snd_array_init(&codec->mixers, sizeof(struct hda_nid_item), 32);
	snd_array_init(&codec->nids, sizeof(struct hda_nid_item), 32);
	snd_array_init(&codec->init_pins, sizeof(struct hda_pincfg), 16);
	snd_array_init(&codec->driver_pins, sizeof(struct hda_pincfg), 16);
	snd_array_init(&codec->cvt_setups, sizeof(struct hda_cvt_setup), 8);
	snd_array_init(&codec->spdif_out, sizeof(struct hda_spdif_out), 16);
	snd_array_init(&codec->jacktbl, sizeof(struct hda_jack_tbl), 16);
	snd_array_init(&codec->verbs, sizeof(struct hda_verb *), 8);
	INIT_LIST_HEAD(&codec->conn_list);
	INIT_LIST_HEAD(&codec->pcm_list_head);

	INIT_DELAYED_WORK(&codec->jackpoll_work, hda_jackpoll_work);
	codec->depop_delay = -1;
	codec->fixup_id = HDA_FIXUP_ID_NOT_SET;

#ifdef CONFIG_PM
	codec->power_jiffies = jiffies;
#endif

	snd_hda_sysfs_init(codec);

	if (codec->bus->modelname) {
		codec->modelname = kstrdup(codec->bus->modelname, GFP_KERNEL);
		if (!codec->modelname) {
			err = -ENOMEM;
			goto error;
		}
	}

	fg = codec->core.afg ? codec->core.afg : codec->core.mfg;
	err = read_widget_caps(codec, fg);
	if (err < 0)
		goto error;
	err = read_pin_defaults(codec);
	if (err < 0)
		goto error;

	/* power-up all before initialization */
	hda_set_power_state(codec, AC_PWRST_D0);
	codec->core.dev.power.power_state = PMSG_ON;

	snd_hda_codec_proc_new(codec);

	snd_hda_create_hwdep(codec);

	sprintf(component, "HDA:%08x,%08x,%08x", codec->core.vendor_id,
		codec->core.subsystem_id, codec->core.revision_id);
	snd_component_add(card, component);

	err = snd_device_new(card, SNDRV_DEV_CODEC, codec, &dev_ops);
	if (err < 0)
		goto error;

	if (codecp)
		*codecp = codec;
	return 0;

 error:
	put_device(hda_codec_dev(codec));
	return err;
}
EXPORT_SYMBOL_GPL(snd_hda_codec_new);

/**
 * snd_hda_codec_update_widgets - Refresh widget caps and pin defaults
 * @codec: the HDA codec
 *
 * Forcibly refresh the all widget caps and the init pin configurations of
 * the given codec.
 */
int snd_hda_codec_update_widgets(struct hda_codec *codec)
{
	hda_nid_t fg;
	int err;

	err = snd_hdac_refresh_widget_sysfs(&codec->core);
	if (err < 0)
		return err;

	/* Assume the function group node does not change,
	 * only the widget nodes may change.
	 */
	kfree(codec->wcaps);
	fg = codec->core.afg ? codec->core.afg : codec->core.mfg;
	err = read_widget_caps(codec, fg);
	if (err < 0)
		return err;

	snd_array_free(&codec->init_pins);
	err = read_pin_defaults(codec);

	return err;
}
EXPORT_SYMBOL_GPL(snd_hda_codec_update_widgets);

/* update the stream-id if changed */
static void update_pcm_stream_id(struct hda_codec *codec,
				 struct hda_cvt_setup *p, hda_nid_t nid,
				 u32 stream_tag, int channel_id)
{
	unsigned int oldval, newval;

	if (p->stream_tag != stream_tag || p->channel_id != channel_id) {
		oldval = snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_CONV, 0);
		newval = (stream_tag << 4) | channel_id;
		if (oldval != newval)
			snd_hda_codec_write(codec, nid, 0,
					    AC_VERB_SET_CHANNEL_STREAMID,
					    newval);
		p->stream_tag = stream_tag;
		p->channel_id = channel_id;
	}
}

/* update the format-id if changed */
static void update_pcm_format(struct hda_codec *codec, struct hda_cvt_setup *p,
			      hda_nid_t nid, int format)
{
	unsigned int oldval;

	if (p->format_id != format) {
		oldval = snd_hda_codec_read(codec, nid, 0,
					    AC_VERB_GET_STREAM_FORMAT, 0);
		if (oldval != format) {
			msleep(1);
			snd_hda_codec_write(codec, nid, 0,
					    AC_VERB_SET_STREAM_FORMAT,
					    format);
		}
		p->format_id = format;
	}
}

/**
 * snd_hda_codec_setup_stream - set up the codec for streaming
 * @codec: the CODEC to set up
 * @nid: the NID to set up
 * @stream_tag: stream tag to pass, it's between 0x1 and 0xf.
 * @channel_id: channel id to pass, zero based.
 * @format: stream format.
 */
void snd_hda_codec_setup_stream(struct hda_codec *codec, hda_nid_t nid,
				u32 stream_tag,
				int channel_id, int format)
{
	if (!nid)
		return;

	snd_printdd("hda_codec_setup_stream: "
		    "NID=0x%x, stream=0x%x, channel=%d, format=0x%x\n",
		    nid, stream_tag, channel_id, format);
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_CHANNEL_STREAMID,
			    (stream_tag << 4) | channel_id);
	msleep(1);
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_STREAM_FORMAT, format);
}

void snd_hda_codec_cleanup_stream(struct hda_codec *codec, hda_nid_t nid)
{
	if (!nid)
		return;

	snd_printdd("hda_codec_cleanup_stream: NID=0x%x\n", nid);
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_CHANNEL_STREAMID, 0);
#if 0 /* keep the format */
	msleep(1);
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_STREAM_FORMAT, 0);
#endif
}
	struct hda_codec *c;
	struct hda_cvt_setup *p;
	int type;
	int i;

	if (!nid)
		return;

	codec_dbg(codec,
		  "hda_codec_setup_stream: NID=0x%x, stream=0x%x, channel=%d, format=0x%x\n",
		  nid, stream_tag, channel_id, format);
	p = get_hda_cvt_setup(codec, nid);
	if (!p)
		return;

	if (codec->patch_ops.stream_pm)
		codec->patch_ops.stream_pm(codec, nid, true);
	if (codec->pcm_format_first)
		update_pcm_format(codec, p, nid, format);
	update_pcm_stream_id(codec, p, nid, stream_tag, channel_id);
	if (!codec->pcm_format_first)
		update_pcm_format(codec, p, nid, format);

	p->active = 1;
	p->dirty = 0;

	/* make other inactive cvts with the same stream-tag dirty */
	type = get_wcaps_type(get_wcaps(codec, nid));
	list_for_each_codec(c, codec->bus) {
		for (i = 0; i < c->cvt_setups.used; i++) {
			p = snd_array_elem(&c->cvt_setups, i);
			if (!p->active && p->stream_tag == stream_tag &&
			    get_wcaps_type(get_wcaps(c, p->nid)) == type)
				p->dirty = 1;
		}
	}
}
EXPORT_SYMBOL_GPL(snd_hda_codec_setup_stream);

static void really_cleanup_stream(struct hda_codec *codec,
				  struct hda_cvt_setup *q);

/**
 * __snd_hda_codec_cleanup_stream - clean up the codec for closing
 * @codec: the CODEC to clean up
 * @nid: the NID to clean up
 * @do_now: really clean up the stream instead of clearing the active flag
 */
void __snd_hda_codec_cleanup_stream(struct hda_codec *codec, hda_nid_t nid,
				    int do_now)
{
	struct hda_cvt_setup *p;

	if (!nid)
		return;

	if (codec->no_sticky_stream)
		do_now = 1;

	codec_dbg(codec, "hda_codec_cleanup_stream: NID=0x%x\n", nid);
	p = get_hda_cvt_setup(codec, nid);
	if (p) {
		/* here we just clear the active flag when do_now isn't set;
		 * actual clean-ups will be done later in
		 * purify_inactive_streams() called from snd_hda_codec_prpapre()
		 */
		if (do_now)
			really_cleanup_stream(codec, p);
		else
			p->active = 0;
	}
}
EXPORT_SYMBOL_GPL(__snd_hda_codec_cleanup_stream);

static void really_cleanup_stream(struct hda_codec *codec,
				  struct hda_cvt_setup *q)
{
	hda_nid_t nid = q->nid;
	if (q->stream_tag || q->channel_id)
		snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_CHANNEL_STREAMID, 0);
	if (q->format_id)
		snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_STREAM_FORMAT, 0
);
	memset(q, 0, sizeof(*q));
	q->nid = nid;
	if (codec->patch_ops.stream_pm)
		codec->patch_ops.stream_pm(codec, nid, false);
}

/* clean up the all conflicting obsolete streams */
static void purify_inactive_streams(struct hda_codec *codec)
{
	struct hda_codec *c;
	int i;

	list_for_each_codec(c, codec->bus) {
		for (i = 0; i < c->cvt_setups.used; i++) {
			struct hda_cvt_setup *p;
			p = snd_array_elem(&c->cvt_setups, i);
			if (p->dirty)
				really_cleanup_stream(c, p);
		}
	}
}

#ifdef CONFIG_PM
/* clean up all streams; called from suspend */
static void hda_cleanup_all_streams(struct hda_codec *codec)
{
	int i;

	for (i = 0; i < codec->cvt_setups.used; i++) {
		struct hda_cvt_setup *p = snd_array_elem(&codec->cvt_setups, i);
		if (p->stream_tag)
			really_cleanup_stream(codec, p);
	}
}
#endif

/*
 * amp access functions
 */

/* FIXME: more better hash key? */
#define HDA_HASH_KEY(nid,dir,idx) (u32)((nid) + ((idx) << 16) + ((dir) << 24))
#define INFO_AMP_CAPS	(1<<0)
#define INFO_AMP_VOL(ch)	(1 << (1 + (ch)))

/* initialize the hash table */
static void __devinit init_hda_cache(struct hda_cache_rec *cache,
				     unsigned int record_size)
{
	memset(cache, 0, sizeof(*cache));
	memset(cache->hash, 0xff, sizeof(cache->hash));
	cache->record_size = record_size;
}

static void free_hda_cache(struct hda_cache_rec *cache)
{
	kfree(cache->buffer);
}

/* query the hash.  allocate an entry if not found. */
static struct hda_cache_head  *get_alloc_hash(struct hda_cache_rec *cache,
					      u32 key)
{
	u16 idx = key % (u16)ARRAY_SIZE(cache->hash);
	u16 cur = cache->hash[idx];
	struct hda_cache_head *info;

	while (cur != 0xffff) {
		info = (struct hda_cache_head *)(cache->buffer +
						 cur * cache->record_size);
		if (info->key == key)
			return info;
		cur = info->next;
	}

	/* add a new hash entry */
	if (cache->num_entries >= cache->size) {
		/* reallocate the array */
		unsigned int new_size = cache->size + 64;
		void *new_buffer;
		new_buffer = kcalloc(new_size, cache->record_size, GFP_KERNEL);
		if (!new_buffer) {
			snd_printk(KERN_ERR "hda_codec: "
				   "can't malloc amp_info\n");
			return NULL;
		}
		if (cache->buffer) {
			memcpy(new_buffer, cache->buffer,
			       cache->size * cache->record_size);
			kfree(cache->buffer);
		}
		cache->size = new_size;
		cache->buffer = new_buffer;
	}
	cur = cache->num_entries++;
	info = (struct hda_cache_head *)(cache->buffer +
					 cur * cache->record_size);
	info->key = key;
	info->val = 0;
	info->next = cache->hash[idx];
	cache->hash[idx] = cur;

	return info;
}

/* query and allocate an amp hash entry */
static inline struct hda_amp_info *
get_alloc_amp_hash(struct hda_codec *codec, u32 key)
{
	return (struct hda_amp_info *)get_alloc_hash(&codec->amp_cache, key);
}

/*
 * query AMP capabilities for the given widget and direction
 */
u32 query_amp_caps(struct hda_codec *codec, hda_nid_t nid, int direction)
{
	struct hda_amp_info *info;

	info = get_alloc_amp_hash(codec, HDA_HASH_KEY(nid, direction, 0));
	if (!info)
		return 0;
	if (!(info->head.val & INFO_AMP_CAPS)) {
		if (!(get_wcaps(codec, nid) & AC_WCAP_AMP_OVRD))
			nid = codec->afg;
		info->amp_caps = snd_hda_param_read(codec, nid,
						    direction == HDA_OUTPUT ?
						    AC_PAR_AMP_OUT_CAP :
						    AC_PAR_AMP_IN_CAP);
		if (info->amp_caps)
			info->head.val |= INFO_AMP_CAPS;
	}
	return info->amp_caps;
}

int snd_hda_override_amp_caps(struct hda_codec *codec, hda_nid_t nid, int dir,
			      unsigned int caps)
{
	struct hda_amp_info *info;

	info = get_alloc_amp_hash(codec, HDA_HASH_KEY(nid, dir, 0));
	if (!info)
		return -EINVAL;
	info->amp_caps = caps;
	info->head.val |= INFO_AMP_CAPS;
	return 0;
}

/*
 * read the current volume to info
 * if the cache exists, read the cache value.
 */
static unsigned int get_vol_mute(struct hda_codec *codec,
				 struct hda_amp_info *info, hda_nid_t nid,
				 int ch, int direction, int index)
{
	u32 val, parm;

	if (info->head.val & INFO_AMP_VOL(ch))
		return info->vol[ch];

	parm = ch ? AC_AMP_GET_RIGHT : AC_AMP_GET_LEFT;
	parm |= direction == HDA_OUTPUT ? AC_AMP_GET_OUTPUT : AC_AMP_GET_INPUT;
	parm |= index;
	val = snd_hda_codec_read(codec, nid, 0,
				 AC_VERB_GET_AMP_GAIN_MUTE, parm);
	info->vol[ch] = val & 0xff;
	info->head.val |= INFO_AMP_VOL(ch);
	return info->vol[ch];
}

/*
 * write the current volume in info to the h/w and update the cache
 */
static void put_vol_mute(struct hda_codec *codec, struct hda_amp_info *info,
			 hda_nid_t nid, int ch, int direction, int index,
			 int val)
{
	u32 parm;

	parm = ch ? AC_AMP_SET_RIGHT : AC_AMP_SET_LEFT;
	parm |= direction == HDA_OUTPUT ? AC_AMP_SET_OUTPUT : AC_AMP_SET_INPUT;
	parm |= index << AC_AMP_SET_INDEX_SHIFT;
	parm |= val;
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_AMP_GAIN_MUTE, parm);
	info->vol[ch] = val;
}

/*
 * read AMP value.  The volume is between 0 to 0x7f, 0x80 = mute bit.
 */
int snd_hda_codec_amp_read(struct hda_codec *codec, hda_nid_t nid, int ch,
			   int direction, int index)
{
	struct hda_amp_info *info;
	info = get_alloc_amp_hash(codec, HDA_HASH_KEY(nid, direction, index));
	if (!info)
		return 0;
	return get_vol_mute(codec, info, nid, ch, direction, index);
}

/*
 * update the AMP value, mask = bit mask to set, val = the value
 */
int snd_hda_codec_amp_update(struct hda_codec *codec, hda_nid_t nid, int ch,
			     int direction, int idx, int mask, int val)
{
	struct hda_amp_info *info;

	info = get_alloc_amp_hash(codec, HDA_HASH_KEY(nid, direction, idx));
	if (!info)
		return 0;
	val &= mask;
	val |= get_vol_mute(codec, info, nid, ch, direction, idx) & ~mask;
	if (info->vol[ch] == val)
		return 0;
	put_vol_mute(codec, info, nid, ch, direction, idx, val);
	return 1;
}

/*
 * update the AMP stereo with the same mask and value
/**
 * query_amp_caps - query AMP capabilities
 * @codec: the HD-auio codec
 * @nid: the NID to query
 * @direction: either #HDA_INPUT or #HDA_OUTPUT
 *
 * Query AMP capabilities for the given widget and direction.
 * Returns the obtained capability bits.
 *
 * When cap bits have been already read, this doesn't read again but
 * returns the cached value.
 */
u32 query_amp_caps(struct hda_codec *codec, hda_nid_t nid, int direction)
{
	if (!(get_wcaps(codec, nid) & AC_WCAP_AMP_OVRD))
		nid = codec->core.afg;
	return snd_hda_param_read(codec, nid,
				  direction == HDA_OUTPUT ?
				  AC_PAR_AMP_OUT_CAP : AC_PAR_AMP_IN_CAP);
}
EXPORT_SYMBOL_GPL(query_amp_caps);

/**
 * snd_hda_check_amp_caps - query AMP capabilities
 * @codec: the HD-audio codec
 * @nid: the NID to query
 * @dir: either #HDA_INPUT or #HDA_OUTPUT
 * @bits: bit mask to check the result
 *
 * Check whether the widget has the given amp capability for the direction.
 */
bool snd_hda_check_amp_caps(struct hda_codec *codec, hda_nid_t nid,
			   int dir, unsigned int bits)
{
	if (!nid)
		return false;
	if (get_wcaps(codec, nid) & (1 << (dir + 1)))
		if (query_amp_caps(codec, nid, dir) & bits)
			return true;
	return false;
}
EXPORT_SYMBOL_GPL(snd_hda_check_amp_caps);

/**
 * snd_hda_override_amp_caps - Override the AMP capabilities
 * @codec: the CODEC to clean up
 * @nid: the NID to clean up
 * @dir: either #HDA_INPUT or #HDA_OUTPUT
 * @caps: the capability bits to set
 *
 * Override the cached AMP caps bits value by the given one.
 * This function is useful if the driver needs to adjust the AMP ranges,
 * e.g. limit to 0dB, etc.
 *
 * Returns zero if successful or a negative error code.
 */
int snd_hda_override_amp_caps(struct hda_codec *codec, hda_nid_t nid, int dir,
			      unsigned int caps)
{
	unsigned int parm;

	snd_hda_override_wcaps(codec, nid,
			       get_wcaps(codec, nid) | AC_WCAP_AMP_OVRD);
	parm = dir == HDA_OUTPUT ? AC_PAR_AMP_OUT_CAP : AC_PAR_AMP_IN_CAP;
	return snd_hdac_override_parm(&codec->core, nid, parm, caps);
}
EXPORT_SYMBOL_GPL(snd_hda_override_amp_caps);

/**
 * snd_hda_codec_amp_update - update the AMP mono value
 * @codec: HD-audio codec
 * @nid: NID to read the AMP value
 * @ch: channel to update (0 or 1)
 * @dir: #HDA_INPUT or #HDA_OUTPUT
 * @idx: the index value (only for input direction)
 * @mask: bit mask to set
 * @val: the bits value to set
 *
 * Update the AMP values for the given channel, direction and index.
 */
int snd_hda_codec_amp_update(struct hda_codec *codec, hda_nid_t nid,
			     int ch, int dir, int idx, int mask, int val)
{
	unsigned int cmd = snd_hdac_regmap_encode_amp(nid, ch, dir, idx);

	/* enable fake mute if no h/w mute but min=mute */
	if ((query_amp_caps(codec, nid, dir) &
	     (AC_AMPCAP_MUTE | AC_AMPCAP_MIN_MUTE)) == AC_AMPCAP_MIN_MUTE)
		cmd |= AC_AMP_FAKE_MUTE;
	return snd_hdac_regmap_update_raw(&codec->core, cmd, mask, val);
}
EXPORT_SYMBOL_GPL(snd_hda_codec_amp_update);

/**
 * snd_hda_codec_amp_stereo - update the AMP stereo values
 * @codec: HD-audio codec
 * @nid: NID to read the AMP value
 * @direction: #HDA_INPUT or #HDA_OUTPUT
 * @idx: the index value (only for input direction)
 * @mask: bit mask to set
 * @val: the bits value to set
 *
 * Update the AMP values like snd_hda_codec_amp_update(), but for a
 * stereo widget with the same mask and value.
 */
int snd_hda_codec_amp_stereo(struct hda_codec *codec, hda_nid_t nid,
			     int direction, int idx, int mask, int val)
{
	int ch, ret = 0;

	if (snd_BUG_ON(mask & ~0xff))
		mask &= 0xff;
	for (ch = 0; ch < 2; ch++)
		ret |= snd_hda_codec_amp_update(codec, nid, ch, direction,
						idx, mask, val);
	return ret;
}

#ifdef SND_HDA_NEEDS_RESUME
/* resume the all amp commands from the cache */
void snd_hda_codec_resume_amp(struct hda_codec *codec)
{
	struct hda_amp_info *buffer = codec->amp_cache.buffer;
	int i;

	for (i = 0; i < codec->amp_cache.size; i++, buffer++) {
		u32 key = buffer->head.key;
		hda_nid_t nid;
		unsigned int idx, dir, ch;
		if (!key)
			continue;
		nid = key & 0xff;
		idx = (key >> 16) & 0xff;
		dir = (key >> 24) & 0xff;
		for (ch = 0; ch < 2; ch++) {
			if (!(buffer->head.val & INFO_AMP_VOL(ch)))
				continue;
			put_vol_mute(codec, buffer, nid, ch, dir, idx,
				     buffer->vol[ch]);
		}
	}
}
#endif /* SND_HDA_NEEDS_RESUME */

/*
 * AMP control callbacks
 */
/* retrieve parameters from private_value */
#define get_amp_nid(kc)		((kc)->private_value & 0xffff)
#define get_amp_channels(kc)	(((kc)->private_value >> 16) & 0x3)
#define get_amp_direction(kc)	(((kc)->private_value >> 18) & 0x1)
#define get_amp_index(kc)	(((kc)->private_value >> 19) & 0xf)

/* volume */
EXPORT_SYMBOL_GPL(snd_hda_codec_amp_stereo);

/**
 * snd_hda_codec_amp_init - initialize the AMP value
 * @codec: the HDA codec
 * @nid: NID to read the AMP value
 * @ch: channel (left=0 or right=1)
 * @dir: #HDA_INPUT or #HDA_OUTPUT
 * @idx: the index value (only for input direction)
 * @mask: bit mask to set
 * @val: the bits value to set
 *
 * Works like snd_hda_codec_amp_update() but it writes the value only at
 * the first access.  If the amp was already initialized / updated beforehand,
 * this does nothing.
 */
int snd_hda_codec_amp_init(struct hda_codec *codec, hda_nid_t nid, int ch,
			   int dir, int idx, int mask, int val)
{
	int orig;

	if (!codec->core.regmap)
		return -EINVAL;
	regcache_cache_only(codec->core.regmap, true);
	orig = snd_hda_codec_amp_read(codec, nid, ch, dir, idx);
	regcache_cache_only(codec->core.regmap, false);
	if (orig >= 0)
		return 0;
	return snd_hda_codec_amp_update(codec, nid, ch, dir, idx, mask, val);
}
EXPORT_SYMBOL_GPL(snd_hda_codec_amp_init);

/**
 * snd_hda_codec_amp_init_stereo - initialize the stereo AMP value
 * @codec: the HDA codec
 * @nid: NID to read the AMP value
 * @dir: #HDA_INPUT or #HDA_OUTPUT
 * @idx: the index value (only for input direction)
 * @mask: bit mask to set
 * @val: the bits value to set
 *
 * Call snd_hda_codec_amp_init() for both stereo channels.
 */
int snd_hda_codec_amp_init_stereo(struct hda_codec *codec, hda_nid_t nid,
				  int dir, int idx, int mask, int val)
{
	int ch, ret = 0;

	if (snd_BUG_ON(mask & ~0xff))
		mask &= 0xff;
	for (ch = 0; ch < 2; ch++)
		ret |= snd_hda_codec_amp_init(codec, nid, ch, dir,
					      idx, mask, val);
	return ret;
}
EXPORT_SYMBOL_GPL(snd_hda_codec_amp_init_stereo);

static u32 get_amp_max_value(struct hda_codec *codec, hda_nid_t nid, int dir,
			     unsigned int ofs)
{
	u32 caps = query_amp_caps(codec, nid, dir);
	/* get num steps */
	caps = (caps & AC_AMPCAP_NUM_STEPS) >> AC_AMPCAP_NUM_STEPS_SHIFT;
	if (ofs < caps)
		caps -= ofs;
	return caps;
}

/**
 * snd_hda_mixer_amp_volume_info - Info callback for a standard AMP mixer
 * @kcontrol: referred ctl element
 * @uinfo: pointer to get/store the data
 *
 * The control element is supposed to have the private_value field
 * set up via HDA_COMPOSE_AMP_VAL*() or related macros.
 */
int snd_hda_mixer_amp_volume_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	u16 nid = get_amp_nid(kcontrol);
	u8 chs = get_amp_channels(kcontrol);
	int dir = get_amp_direction(kcontrol);
	u32 caps;

	caps = query_amp_caps(codec, nid, dir);
	/* num steps */
	caps = (caps & AC_AMPCAP_NUM_STEPS) >> AC_AMPCAP_NUM_STEPS_SHIFT;
	if (!caps) {
		printk(KERN_WARNING "hda_codec: "
		       "num_steps = 0 for NID=0x%x (ctl = %s)\n", nid,
		       kcontrol->id.name);
		return -EINVAL;
	}
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = chs == 3 ? 2 : 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = caps;
	return 0;
}

	unsigned int ofs = get_amp_offset(kcontrol);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = chs == 3 ? 2 : 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = get_amp_max_value(codec, nid, dir, ofs);
	if (!uinfo->value.integer.max) {
		codec_warn(codec,
			   "num_steps = 0 for NID=0x%x (ctl = %s)\n",
			   nid, kcontrol->id.name);
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_mixer_amp_volume_info);


static inline unsigned int
read_amp_value(struct hda_codec *codec, hda_nid_t nid,
	       int ch, int dir, int idx, unsigned int ofs)
{
	unsigned int val;
	val = snd_hda_codec_amp_read(codec, nid, ch, dir, idx);
	val &= HDA_AMP_VOLMASK;
	if (val >= ofs)
		val -= ofs;
	else
		val = 0;
	return val;
}

static inline int
update_amp_value(struct hda_codec *codec, hda_nid_t nid,
		 int ch, int dir, int idx, unsigned int ofs,
		 unsigned int val)
{
	unsigned int maxval;

	if (val > 0)
		val += ofs;
	/* ofs = 0: raw max value */
	maxval = get_amp_max_value(codec, nid, dir, 0);
	if (val > maxval)
		val = maxval;
	return snd_hda_codec_amp_update(codec, nid, ch, dir, idx,
					HDA_AMP_VOLMASK, val);
}

/**
 * snd_hda_mixer_amp_volume_get - Get callback for a standard AMP mixer volume
 * @kcontrol: ctl element
 * @ucontrol: pointer to get/store the data
 *
 * The control element is supposed to have the private_value field
 * set up via HDA_COMPOSE_AMP_VAL*() or related macros.
 */
int snd_hda_mixer_amp_volume_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = get_amp_nid(kcontrol);
	int chs = get_amp_channels(kcontrol);
	int dir = get_amp_direction(kcontrol);
	int idx = get_amp_index(kcontrol);
	long *valp = ucontrol->value.integer.value;

	if (chs & 1)
		*valp++ = snd_hda_codec_amp_read(codec, nid, 0, dir, idx)
			& HDA_AMP_VOLMASK;
	if (chs & 2)
		*valp = snd_hda_codec_amp_read(codec, nid, 1, dir, idx)
			& HDA_AMP_VOLMASK;
	return 0;
}

	unsigned int ofs = get_amp_offset(kcontrol);
	long *valp = ucontrol->value.integer.value;

	if (chs & 1)
		*valp++ = read_amp_value(codec, nid, 0, dir, idx, ofs);
	if (chs & 2)
		*valp = read_amp_value(codec, nid, 1, dir, idx, ofs);
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_mixer_amp_volume_get);

/**
 * snd_hda_mixer_amp_volume_put - Put callback for a standard AMP mixer volume
 * @kcontrol: ctl element
 * @ucontrol: pointer to get/store the data
 *
 * The control element is supposed to have the private_value field
 * set up via HDA_COMPOSE_AMP_VAL*() or related macros.
 */
int snd_hda_mixer_amp_volume_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = get_amp_nid(kcontrol);
	int chs = get_amp_channels(kcontrol);
	int dir = get_amp_direction(kcontrol);
	int idx = get_amp_index(kcontrol);
	long *valp = ucontrol->value.integer.value;
	int change = 0;

	snd_hda_power_up(codec);
	if (chs & 1) {
		change = snd_hda_codec_amp_update(codec, nid, 0, dir, idx,
						  0x7f, *valp);
		valp++;
	}
	if (chs & 2)
		change |= snd_hda_codec_amp_update(codec, nid, 1, dir, idx,
						   0x7f, *valp);
	snd_hda_power_down(codec);
	return change;
}

	unsigned int ofs = get_amp_offset(kcontrol);
	long *valp = ucontrol->value.integer.value;
	int change = 0;

	if (chs & 1) {
		change = update_amp_value(codec, nid, 0, dir, idx, ofs, *valp);
		valp++;
	}
	if (chs & 2)
		change |= update_amp_value(codec, nid, 1, dir, idx, ofs, *valp);
	return change;
}
EXPORT_SYMBOL_GPL(snd_hda_mixer_amp_volume_put);

/**
 * snd_hda_mixer_amp_volume_put - TLV callback for a standard AMP mixer volume
 * @kcontrol: ctl element
 * @op_flag: operation flag
 * @size: byte size of input TLV
 * @_tlv: TLV data
 *
 * The control element is supposed to have the private_value field
 * set up via HDA_COMPOSE_AMP_VAL*() or related macros.
 */
int snd_hda_mixer_amp_tlv(struct snd_kcontrol *kcontrol, int op_flag,
			  unsigned int size, unsigned int __user *_tlv)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = get_amp_nid(kcontrol);
	int dir = get_amp_direction(kcontrol);
	unsigned int ofs = get_amp_offset(kcontrol);
	bool min_mute = get_amp_min_mute(kcontrol);
	u32 caps, val1, val2;

	if (size < 4 * sizeof(unsigned int))
		return -ENOMEM;
	caps = query_amp_caps(codec, nid, dir);
	val2 = (caps & AC_AMPCAP_STEP_SIZE) >> AC_AMPCAP_STEP_SIZE_SHIFT;
	val2 = (val2 + 1) * 25;
	val1 = -((caps & AC_AMPCAP_OFFSET) >> AC_AMPCAP_OFFSET_SHIFT);
	val1 = ((int)val1) * ((int)val2);
	val1 += ofs;
	val1 = ((int)val1) * ((int)val2);
	if (min_mute || (caps & AC_AMPCAP_MIN_MUTE))
		val2 |= TLV_DB_SCALE_MUTE;
	if (put_user(SNDRV_CTL_TLVT_DB_SCALE, _tlv))
		return -EFAULT;
	if (put_user(2 * sizeof(unsigned int), _tlv + 1))
		return -EFAULT;
	if (put_user(val1, _tlv + 2))
		return -EFAULT;
	if (put_user(val2, _tlv + 3))
		return -EFAULT;
	return 0;
}

/*
 * set (static) TLV for virtual master volume; recalculated as max 0dB
EXPORT_SYMBOL_GPL(snd_hda_mixer_amp_tlv);

/**
 * snd_hda_set_vmaster_tlv - Set TLV for a virtual master control
 * @codec: HD-audio codec
 * @nid: NID of a reference widget
 * @dir: #HDA_INPUT or #HDA_OUTPUT
 * @tlv: TLV data to be stored, at least 4 elements
 *
 * Set (static) TLV data for a virtual master volume using the AMP caps
 * obtained from the reference NID.
 * The volume range is recalculated as if the max volume is 0dB.
 */
void snd_hda_set_vmaster_tlv(struct hda_codec *codec, hda_nid_t nid, int dir,
			     unsigned int *tlv)
{
	u32 caps;
	int nums, step;

	caps = query_amp_caps(codec, nid, dir);
	nums = (caps & AC_AMPCAP_NUM_STEPS) >> AC_AMPCAP_NUM_STEPS_SHIFT;
	step = (caps & AC_AMPCAP_STEP_SIZE) >> AC_AMPCAP_STEP_SIZE_SHIFT;
	step = (step + 1) * 25;
	tlv[0] = SNDRV_CTL_TLVT_DB_SCALE;
	tlv[1] = 2 * sizeof(unsigned int);
	tlv[2] = -nums * step;
	tlv[3] = step;
}

/* find a mixer control element with the given name */
static struct snd_kcontrol *
_snd_hda_find_mixer_ctl(struct hda_codec *codec,
			const char *name, int idx)
EXPORT_SYMBOL_GPL(snd_hda_set_vmaster_tlv);

/* find a mixer control element with the given name */
static struct snd_kcontrol *
find_mixer_ctl(struct hda_codec *codec, const char *name, int dev, int idx)
{
	struct snd_ctl_elem_id id;
	memset(&id, 0, sizeof(id));
	id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	id.index = idx;
	strcpy(id.name, name);
	return snd_ctl_find_id(codec->bus->card, &id);
}

struct snd_kcontrol *snd_hda_find_mixer_ctl(struct hda_codec *codec,
					    const char *name)
{
	return _snd_hda_find_mixer_ctl(codec, name, 0);
}

/* create a virtual master control and add slaves */
int snd_hda_add_vmaster(struct hda_codec *codec, char *name,
			unsigned int *tlv, const char **slaves)
{
	struct snd_kcontrol *kctl;
	const char **s;
	int err;

	for (s = slaves; *s && !snd_hda_find_mixer_ctl(codec, *s); s++)
		;
	if (!*s) {
		snd_printdd("No slave found for %s\n", name);
	id.device = dev;
	id.index = idx;
	if (snd_BUG_ON(strlen(name) >= sizeof(id.name)))
		return NULL;
	strcpy(id.name, name);
	return snd_ctl_find_id(codec->card, &id);
}

/**
 * snd_hda_find_mixer_ctl - Find a mixer control element with the given name
 * @codec: HD-audio codec
 * @name: ctl id name string
 *
 * Get the control element with the given id string and IFACE_MIXER.
 */
struct snd_kcontrol *snd_hda_find_mixer_ctl(struct hda_codec *codec,
					    const char *name)
{
	return find_mixer_ctl(codec, name, 0, 0);
}
EXPORT_SYMBOL_GPL(snd_hda_find_mixer_ctl);

static int find_empty_mixer_ctl_idx(struct hda_codec *codec, const char *name,
				    int start_idx)
{
	int i, idx;
	/* 16 ctlrs should be large enough */
	for (i = 0, idx = start_idx; i < 16; i++, idx++) {
		if (!find_mixer_ctl(codec, name, 0, idx))
			return idx;
	}
	return -EBUSY;
}

/**
 * snd_hda_ctl_add - Add a control element and assign to the codec
 * @codec: HD-audio codec
 * @nid: corresponding NID (optional)
 * @kctl: the control element to assign
 *
 * Add the given control element to an array inside the codec instance.
 * All control elements belonging to a codec are supposed to be added
 * by this function so that a proper clean-up works at the free or
 * reconfiguration time.
 *
 * If non-zero @nid is passed, the NID is assigned to the control element.
 * The assignment is shown in the codec proc file.
 *
 * snd_hda_ctl_add() checks the control subdev id field whether
 * #HDA_SUBDEV_NID_FLAG bit is set.  If set (and @nid is zero), the lower
 * bits value is taken as the NID to assign. The #HDA_NID_ITEM_AMP bit
 * specifies if kctl->private_value is a HDA amplifier value.
 */
int snd_hda_ctl_add(struct hda_codec *codec, hda_nid_t nid,
		    struct snd_kcontrol *kctl)
{
	int err;
	unsigned short flags = 0;
	struct hda_nid_item *item;

	if (kctl->id.subdevice & HDA_SUBDEV_AMP_FLAG) {
		flags |= HDA_NID_ITEM_AMP;
		if (nid == 0)
			nid = get_amp_nid_(kctl->private_value);
	}
	if ((kctl->id.subdevice & HDA_SUBDEV_NID_FLAG) != 0 && nid == 0)
		nid = kctl->id.subdevice & 0xffff;
	if (kctl->id.subdevice & (HDA_SUBDEV_NID_FLAG|HDA_SUBDEV_AMP_FLAG))
		kctl->id.subdevice = 0;
	err = snd_ctl_add(codec->card, kctl);
	if (err < 0)
		return err;
	item = snd_array_new(&codec->mixers);
	if (!item)
		return -ENOMEM;
	item->kctl = kctl;
	item->nid = nid;
	item->flags = flags;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_ctl_add);

/**
 * snd_hda_add_nid - Assign a NID to a control element
 * @codec: HD-audio codec
 * @nid: corresponding NID (optional)
 * @kctl: the control element to assign
 * @index: index to kctl
 *
 * Add the given control element to an array inside the codec instance.
 * This function is used when #snd_hda_ctl_add cannot be used for 1:1
 * NID:KCTL mapping - for example "Capture Source" selector.
 */
int snd_hda_add_nid(struct hda_codec *codec, struct snd_kcontrol *kctl,
		    unsigned int index, hda_nid_t nid)
{
	struct hda_nid_item *item;

	if (nid > 0) {
		item = snd_array_new(&codec->nids);
		if (!item)
			return -ENOMEM;
		item->kctl = kctl;
		item->index = index;
		item->nid = nid;
		return 0;
	}
	codec_err(codec, "no NID for mapping control %s:%d:%d\n",
		  kctl->id.name, kctl->id.index, index);
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(snd_hda_add_nid);

/**
 * snd_hda_ctls_clear - Clear all controls assigned to the given codec
 * @codec: HD-audio codec
 */
void snd_hda_ctls_clear(struct hda_codec *codec)
{
	int i;
	struct hda_nid_item *items = codec->mixers.list;

	down_write(&codec->card->controls_rwsem);
	for (i = 0; i < codec->mixers.used; i++)
		snd_ctl_remove(codec->card, items[i].kctl);
	up_write(&codec->card->controls_rwsem);
	snd_array_free(&codec->mixers);
	snd_array_free(&codec->nids);
}

/**
 * snd_hda_lock_devices - pseudo device locking
 * @bus: the BUS
 *
 * toggle card->shutdown to allow/disallow the device access (as a hack)
 */
int snd_hda_lock_devices(struct hda_bus *bus)
{
	struct snd_card *card = bus->card;
	struct hda_codec *codec;

	spin_lock(&card->files_lock);
	if (card->shutdown)
		goto err_unlock;
	card->shutdown = 1;
	if (!list_empty(&card->ctl_files))
		goto err_clear;

	list_for_each_codec(codec, bus) {
		struct hda_pcm *cpcm;
		list_for_each_entry(cpcm, &codec->pcm_list_head, list) {
			if (!cpcm->pcm)
				continue;
			if (cpcm->pcm->streams[0].substream_opened ||
			    cpcm->pcm->streams[1].substream_opened)
				goto err_clear;
		}
	}
	spin_unlock(&card->files_lock);
	return 0;

 err_clear:
	card->shutdown = 0;
 err_unlock:
	spin_unlock(&card->files_lock);
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(snd_hda_lock_devices);

/**
 * snd_hda_unlock_devices - pseudo device unlocking
 * @bus: the BUS
 */
void snd_hda_unlock_devices(struct hda_bus *bus)
{
	struct snd_card *card = bus->card;

	spin_lock(&card->files_lock);
	card->shutdown = 0;
	spin_unlock(&card->files_lock);
}
EXPORT_SYMBOL_GPL(snd_hda_unlock_devices);

/**
 * snd_hda_codec_reset - Clear all objects assigned to the codec
 * @codec: HD-audio codec
 *
 * This frees the all PCM and control elements assigned to the codec, and
 * clears the caches and restores the pin default configurations.
 *
 * When a device is being used, it returns -EBSY.  If successfully freed,
 * returns zero.
 */
int snd_hda_codec_reset(struct hda_codec *codec)
{
	struct hda_bus *bus = codec->bus;

	if (snd_hda_lock_devices(bus) < 0)
		return -EBUSY;

	/* OK, let it free */
	snd_hdac_device_unregister(&codec->core);

	/* allow device access again */
	snd_hda_unlock_devices(bus);
	return 0;
}

typedef int (*map_slave_func_t)(struct hda_codec *, void *, struct snd_kcontrol *);

/* apply the function to all matching slave ctls in the mixer list */
static int map_slaves(struct hda_codec *codec, const char * const *slaves,
		      const char *suffix, map_slave_func_t func, void *data) 
{
	struct hda_nid_item *items;
	const char * const *s;
	int i, err;

	items = codec->mixers.list;
	for (i = 0; i < codec->mixers.used; i++) {
		struct snd_kcontrol *sctl = items[i].kctl;
		if (!sctl || sctl->id.iface != SNDRV_CTL_ELEM_IFACE_MIXER)
			continue;
		for (s = slaves; *s; s++) {
			char tmpname[sizeof(sctl->id.name)];
			const char *name = *s;
			if (suffix) {
				snprintf(tmpname, sizeof(tmpname), "%s %s",
					 name, suffix);
				name = tmpname;
			}
			if (!strcmp(sctl->id.name, name)) {
				err = func(codec, data, sctl);
				if (err)
					return err;
				break;
			}
		}
	}
	return 0;
}

static int check_slave_present(struct hda_codec *codec,
			       void *data, struct snd_kcontrol *sctl)
{
	return 1;
}

/* guess the value corresponding to 0dB */
static int get_kctl_0dB_offset(struct hda_codec *codec,
			       struct snd_kcontrol *kctl, int *step_to_check)
{
	int _tlv[4];
	const int *tlv = NULL;
	int val = -1;

	if (kctl->vd[0].access & SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK) {
		/* FIXME: set_fs() hack for obtaining user-space TLV data */
		mm_segment_t fs = get_fs();
		set_fs(get_ds());
		if (!kctl->tlv.c(kctl, 0, sizeof(_tlv), _tlv))
			tlv = _tlv;
		set_fs(fs);
	} else if (kctl->vd[0].access & SNDRV_CTL_ELEM_ACCESS_TLV_READ)
		tlv = kctl->tlv.p;
	if (tlv && tlv[0] == SNDRV_CTL_TLVT_DB_SCALE) {
		int step = tlv[3];
		step &= ~TLV_DB_SCALE_MUTE;
		if (!step)
			return -1;
		if (*step_to_check && *step_to_check != step) {
			codec_err(codec, "Mismatching dB step for vmaster slave (%d!=%d)\n",
				   *step_to_check, step);
			return -1;
		}
		*step_to_check = step;
		val = -tlv[2] / step;
	}
	return val;
}

/* call kctl->put with the given value(s) */
static int put_kctl_with_value(struct snd_kcontrol *kctl, int val)
{
	struct snd_ctl_elem_value *ucontrol;
	ucontrol = kzalloc(sizeof(*ucontrol), GFP_KERNEL);
	if (!ucontrol)
		return -ENOMEM;
	ucontrol->value.integer.value[0] = val;
	ucontrol->value.integer.value[1] = val;
	kctl->put(kctl, ucontrol);
	kfree(ucontrol);
	return 0;
}

/* initialize the slave volume with 0dB */
static int init_slave_0dB(struct hda_codec *codec,
			  void *data, struct snd_kcontrol *slave)
{
	int offset = get_kctl_0dB_offset(codec, slave, data);
	if (offset > 0)
		put_kctl_with_value(slave, offset);
	return 0;
}

/* unmute the slave */
static int init_slave_unmute(struct hda_codec *codec,
			     void *data, struct snd_kcontrol *slave)
{
	return put_kctl_with_value(slave, 1);
}

static int add_slave(struct hda_codec *codec,
		     void *data, struct snd_kcontrol *slave)
{
	return snd_ctl_add_slave(data, slave);
}

/**
 * __snd_hda_add_vmaster - create a virtual master control and add slaves
 * @codec: HD-audio codec
 * @name: vmaster control name
 * @tlv: TLV data (optional)
 * @slaves: slave control names (optional)
 * @suffix: suffix string to each slave name (optional)
 * @init_slave_vol: initialize slaves to unmute/0dB
 * @ctl_ret: store the vmaster kcontrol in return
 *
 * Create a virtual master control with the given name.  The TLV data
 * must be either NULL or a valid data.
 *
 * @slaves is a NULL-terminated array of strings, each of which is a
 * slave control name.  All controls with these names are assigned to
 * the new virtual master control.
 *
 * This function returns zero if successful or a negative error code.
 */
int __snd_hda_add_vmaster(struct hda_codec *codec, char *name,
			unsigned int *tlv, const char * const *slaves,
			  const char *suffix, bool init_slave_vol,
			  struct snd_kcontrol **ctl_ret)
{
	struct snd_kcontrol *kctl;
	int err;

	if (ctl_ret)
		*ctl_ret = NULL;

	err = map_slaves(codec, slaves, suffix, check_slave_present, NULL);
	if (err != 1) {
		codec_dbg(codec, "No slave found for %s\n", name);
		return 0;
	}
	kctl = snd_ctl_make_virtual_master(name, tlv);
	if (!kctl)
		return -ENOMEM;
	err = snd_ctl_add(codec->bus->card, kctl);
	if (err < 0)
		return err;
	
	for (s = slaves; *s; s++) {
		struct snd_kcontrol *sctl;

		sctl = snd_hda_find_mixer_ctl(codec, *s);
		if (!sctl) {
			snd_printdd("Cannot find slave %s, skipped\n", *s);
			continue;
		}
		err = snd_ctl_add_slave(kctl, sctl);
		if (err < 0)
			return err;
	}
	return 0;
}

/* switch */
	err = snd_hda_ctl_add(codec, 0, kctl);
	if (err < 0)
		return err;

	err = map_slaves(codec, slaves, suffix, add_slave, kctl);
	if (err < 0)
		return err;

	/* init with master mute & zero volume */
	put_kctl_with_value(kctl, 0);
	if (init_slave_vol) {
		int step = 0;
		map_slaves(codec, slaves, suffix,
			   tlv ? init_slave_0dB : init_slave_unmute, &step);
	}

	if (ctl_ret)
		*ctl_ret = kctl;
	return 0;
}
EXPORT_SYMBOL_GPL(__snd_hda_add_vmaster);

/*
 * mute-LED control using vmaster
 */
static int vmaster_mute_mode_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[] = {
		"On", "Off", "Follow Master"
	};

	return snd_ctl_enum_info(uinfo, 1, 3, texts);
}

static int vmaster_mute_mode_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct hda_vmaster_mute_hook *hook = snd_kcontrol_chip(kcontrol);
	ucontrol->value.enumerated.item[0] = hook->mute_mode;
	return 0;
}

static int vmaster_mute_mode_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct hda_vmaster_mute_hook *hook = snd_kcontrol_chip(kcontrol);
	unsigned int old_mode = hook->mute_mode;

	hook->mute_mode = ucontrol->value.enumerated.item[0];
	if (hook->mute_mode > HDA_VMUTE_FOLLOW_MASTER)
		hook->mute_mode = HDA_VMUTE_FOLLOW_MASTER;
	if (old_mode == hook->mute_mode)
		return 0;
	snd_hda_sync_vmaster_hook(hook);
	return 1;
}

static struct snd_kcontrol_new vmaster_mute_mode = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Mute-LED Mode",
	.info = vmaster_mute_mode_info,
	.get = vmaster_mute_mode_get,
	.put = vmaster_mute_mode_put,
};

/* meta hook to call each driver's vmaster hook */
static void vmaster_hook(void *private_data, int enabled)
{
	struct hda_vmaster_mute_hook *hook = private_data;

	if (hook->mute_mode != HDA_VMUTE_FOLLOW_MASTER)
		enabled = hook->mute_mode;
	hook->hook(hook->codec, enabled);
}

/**
 * snd_hda_add_vmaster_hook - Add a vmaster hook for mute-LED
 * @codec: the HDA codec
 * @hook: the vmaster hook object
 * @expose_enum_ctl: flag to create an enum ctl
 *
 * Add a mute-LED hook with the given vmaster switch kctl.
 * When @expose_enum_ctl is set, "Mute-LED Mode" control is automatically
 * created and associated with the given hook.
 */
int snd_hda_add_vmaster_hook(struct hda_codec *codec,
			     struct hda_vmaster_mute_hook *hook,
			     bool expose_enum_ctl)
{
	struct snd_kcontrol *kctl;

	if (!hook->hook || !hook->sw_kctl)
		return 0;
	hook->codec = codec;
	hook->mute_mode = HDA_VMUTE_FOLLOW_MASTER;
	snd_ctl_add_vmaster_hook(hook->sw_kctl, vmaster_hook, hook);
	if (!expose_enum_ctl)
		return 0;
	kctl = snd_ctl_new1(&vmaster_mute_mode, hook);
	if (!kctl)
		return -ENOMEM;
	return snd_hda_ctl_add(codec, 0, kctl);
}
EXPORT_SYMBOL_GPL(snd_hda_add_vmaster_hook);

/**
 * snd_hda_sync_vmaster_hook - Sync vmaster hook
 * @hook: the vmaster hook
 *
 * Call the hook with the current value for synchronization.
 * Should be called in init callback.
 */
void snd_hda_sync_vmaster_hook(struct hda_vmaster_mute_hook *hook)
{
	if (!hook->hook || !hook->codec)
		return;
	/* don't call vmaster hook in the destructor since it might have
	 * been already destroyed
	 */
	if (hook->codec->bus->shutdown)
		return;
	snd_ctl_sync_vmaster_hook(hook->sw_kctl);
}
EXPORT_SYMBOL_GPL(snd_hda_sync_vmaster_hook);


/**
 * snd_hda_mixer_amp_switch_info - Info callback for a standard AMP mixer switch
 * @kcontrol: referred ctl element
 * @uinfo: pointer to get/store the data
 *
 * The control element is supposed to have the private_value field
 * set up via HDA_COMPOSE_AMP_VAL*() or related macros.
 */
int snd_hda_mixer_amp_switch_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	int chs = get_amp_channels(kcontrol);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = chs == 3 ? 2 : 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

EXPORT_SYMBOL_GPL(snd_hda_mixer_amp_switch_info);

/**
 * snd_hda_mixer_amp_switch_get - Get callback for a standard AMP mixer switch
 * @kcontrol: ctl element
 * @ucontrol: pointer to get/store the data
 *
 * The control element is supposed to have the private_value field
 * set up via HDA_COMPOSE_AMP_VAL*() or related macros.
 */
int snd_hda_mixer_amp_switch_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = get_amp_nid(kcontrol);
	int chs = get_amp_channels(kcontrol);
	int dir = get_amp_direction(kcontrol);
	int idx = get_amp_index(kcontrol);
	long *valp = ucontrol->value.integer.value;

	if (chs & 1)
		*valp++ = (snd_hda_codec_amp_read(codec, nid, 0, dir, idx) &
			   HDA_AMP_MUTE) ? 0 : 1;
	if (chs & 2)
		*valp = (snd_hda_codec_amp_read(codec, nid, 1, dir, idx) &
			 HDA_AMP_MUTE) ? 0 : 1;
	return 0;
}

EXPORT_SYMBOL_GPL(snd_hda_mixer_amp_switch_get);

/**
 * snd_hda_mixer_amp_switch_put - Put callback for a standard AMP mixer switch
 * @kcontrol: ctl element
 * @ucontrol: pointer to get/store the data
 *
 * The control element is supposed to have the private_value field
 * set up via HDA_COMPOSE_AMP_VAL*() or related macros.
 */
int snd_hda_mixer_amp_switch_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = get_amp_nid(kcontrol);
	int chs = get_amp_channels(kcontrol);
	int dir = get_amp_direction(kcontrol);
	int idx = get_amp_index(kcontrol);
	long *valp = ucontrol->value.integer.value;
	int change = 0;

	snd_hda_power_up(codec);
	if (chs & 1) {
		change = snd_hda_codec_amp_update(codec, nid, 0, dir, idx,
						  HDA_AMP_MUTE,
						  *valp ? 0 : HDA_AMP_MUTE);
		valp++;
	}
	if (chs & 2)
		change |= snd_hda_codec_amp_update(codec, nid, 1, dir, idx,
						   HDA_AMP_MUTE,
						   *valp ? 0 : HDA_AMP_MUTE);
#ifdef CONFIG_SND_HDA_POWER_SAVE
	if (codec->patch_ops.check_power_status)
		codec->patch_ops.check_power_status(codec, nid);
#endif
	snd_hda_power_down(codec);
	return change;
}
	hda_call_check_power_status(codec, nid);
	return change;
}
EXPORT_SYMBOL_GPL(snd_hda_mixer_amp_switch_put);

/*
 * bound volume controls
 *
 * bind multiple volumes (# indices, from 0)
 */

#define AMP_VAL_IDX_SHIFT	19
#define AMP_VAL_IDX_MASK	(0x0f<<19)

/**
 * snd_hda_mixer_bind_switch_get - Get callback for a bound volume control
 * @kcontrol: ctl element
 * @ucontrol: pointer to get/store the data
 *
 * The control element is supposed to have the private_value field
 * set up via HDA_BIND_MUTE*() macros.
 */
int snd_hda_mixer_bind_switch_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned long pval;
	int err;

	mutex_lock(&codec->spdif_mutex); /* reuse spdif_mutex */
	mutex_lock(&codec->control_mutex);
	pval = kcontrol->private_value;
	kcontrol->private_value = pval & ~AMP_VAL_IDX_MASK; /* index 0 */
	err = snd_hda_mixer_amp_switch_get(kcontrol, ucontrol);
	kcontrol->private_value = pval;
	mutex_unlock(&codec->spdif_mutex);
	return err;
}

	mutex_unlock(&codec->control_mutex);
	return err;
}
EXPORT_SYMBOL_GPL(snd_hda_mixer_bind_switch_get);

/**
 * snd_hda_mixer_bind_switch_put - Put callback for a bound volume control
 * @kcontrol: ctl element
 * @ucontrol: pointer to get/store the data
 *
 * The control element is supposed to have the private_value field
 * set up via HDA_BIND_MUTE*() macros.
 */
int snd_hda_mixer_bind_switch_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned long pval;
	int i, indices, err = 0, change = 0;

	mutex_lock(&codec->spdif_mutex); /* reuse spdif_mutex */
	mutex_lock(&codec->control_mutex);
	pval = kcontrol->private_value;
	indices = (pval & AMP_VAL_IDX_MASK) >> AMP_VAL_IDX_SHIFT;
	for (i = 0; i < indices; i++) {
		kcontrol->private_value = (pval & ~AMP_VAL_IDX_MASK) |
			(i << AMP_VAL_IDX_SHIFT);
		err = snd_hda_mixer_amp_switch_put(kcontrol, ucontrol);
		if (err < 0)
			break;
		change |= err;
	}
	kcontrol->private_value = pval;
	mutex_unlock(&codec->spdif_mutex);
	return err < 0 ? err : change;
}

/*
 * generic bound volume/swtich controls
	mutex_unlock(&codec->control_mutex);
	return err < 0 ? err : change;
}
EXPORT_SYMBOL_GPL(snd_hda_mixer_bind_switch_put);

/**
 * snd_hda_mixer_bind_ctls_info - Info callback for a generic bound control
 * @kcontrol: referred ctl element
 * @uinfo: pointer to get/store the data
 *
 * The control element is supposed to have the private_value field
 * set up via HDA_BIND_VOL() or HDA_BIND_SW() macros.
 */
int snd_hda_mixer_bind_ctls_info(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_bind_ctls *c;
	int err;

	mutex_lock(&codec->spdif_mutex); /* reuse spdif_mutex */
	mutex_lock(&codec->control_mutex);
	c = (struct hda_bind_ctls *)kcontrol->private_value;
	kcontrol->private_value = *c->values;
	err = c->ops->info(kcontrol, uinfo);
	kcontrol->private_value = (long)c;
	mutex_unlock(&codec->spdif_mutex);
	return err;
}

	mutex_unlock(&codec->control_mutex);
	return err;
}
EXPORT_SYMBOL_GPL(snd_hda_mixer_bind_ctls_info);

/**
 * snd_hda_mixer_bind_ctls_get - Get callback for a generic bound control
 * @kcontrol: ctl element
 * @ucontrol: pointer to get/store the data
 *
 * The control element is supposed to have the private_value field
 * set up via HDA_BIND_VOL() or HDA_BIND_SW() macros.
 */
int snd_hda_mixer_bind_ctls_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_bind_ctls *c;
	int err;

	mutex_lock(&codec->spdif_mutex); /* reuse spdif_mutex */
	mutex_lock(&codec->control_mutex);
	c = (struct hda_bind_ctls *)kcontrol->private_value;
	kcontrol->private_value = *c->values;
	err = c->ops->get(kcontrol, ucontrol);
	kcontrol->private_value = (long)c;
	mutex_unlock(&codec->spdif_mutex);
	return err;
}

	mutex_unlock(&codec->control_mutex);
	return err;
}
EXPORT_SYMBOL_GPL(snd_hda_mixer_bind_ctls_get);

/**
 * snd_hda_mixer_bind_ctls_put - Put callback for a generic bound control
 * @kcontrol: ctl element
 * @ucontrol: pointer to get/store the data
 *
 * The control element is supposed to have the private_value field
 * set up via HDA_BIND_VOL() or HDA_BIND_SW() macros.
 */
int snd_hda_mixer_bind_ctls_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_bind_ctls *c;
	unsigned long *vals;
	int err = 0, change = 0;

	mutex_lock(&codec->spdif_mutex); /* reuse spdif_mutex */
	mutex_lock(&codec->control_mutex);
	c = (struct hda_bind_ctls *)kcontrol->private_value;
	for (vals = c->values; *vals; vals++) {
		kcontrol->private_value = *vals;
		err = c->ops->put(kcontrol, ucontrol);
		if (err < 0)
			break;
		change |= err;
	}
	kcontrol->private_value = (long)c;
	mutex_unlock(&codec->spdif_mutex);
	return err < 0 ? err : change;
}

	mutex_unlock(&codec->control_mutex);
	return err < 0 ? err : change;
}
EXPORT_SYMBOL_GPL(snd_hda_mixer_bind_ctls_put);

/**
 * snd_hda_mixer_bind_tlv - TLV callback for a generic bound control
 * @kcontrol: ctl element
 * @op_flag: operation flag
 * @size: byte size of input TLV
 * @tlv: TLV data
 *
 * The control element is supposed to have the private_value field
 * set up via HDA_BIND_VOL() macro.
 */
int snd_hda_mixer_bind_tlv(struct snd_kcontrol *kcontrol, int op_flag,
			   unsigned int size, unsigned int __user *tlv)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_bind_ctls *c;
	int err;

	mutex_lock(&codec->spdif_mutex); /* reuse spdif_mutex */
	mutex_lock(&codec->control_mutex);
	c = (struct hda_bind_ctls *)kcontrol->private_value;
	kcontrol->private_value = *c->values;
	err = c->ops->tlv(kcontrol, op_flag, size, tlv);
	kcontrol->private_value = (long)c;
	mutex_unlock(&codec->spdif_mutex);
	return err;
}
	mutex_unlock(&codec->control_mutex);
	return err;
}
EXPORT_SYMBOL_GPL(snd_hda_mixer_bind_tlv);

struct hda_ctl_ops snd_hda_bind_vol = {
	.info = snd_hda_mixer_amp_volume_info,
	.get = snd_hda_mixer_amp_volume_get,
	.put = snd_hda_mixer_amp_volume_put,
	.tlv = snd_hda_mixer_amp_tlv
};
EXPORT_SYMBOL_GPL(snd_hda_bind_vol);

struct hda_ctl_ops snd_hda_bind_sw = {
	.info = snd_hda_mixer_amp_switch_info,
	.get = snd_hda_mixer_amp_switch_get,
	.put = snd_hda_mixer_amp_switch_put,
	.tlv = snd_hda_mixer_amp_tlv
};
EXPORT_SYMBOL_GPL(snd_hda_bind_sw);

/*
 * SPDIF out controls
 */

static int snd_hda_spdif_mask_info(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_hda_spdif_cmask_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.iec958.status[0] = IEC958_AES0_PROFESSIONAL |
					   IEC958_AES0_NONAUDIO |
					   IEC958_AES0_CON_EMPHASIS_5015 |
					   IEC958_AES0_CON_NOT_COPYRIGHT;
	ucontrol->value.iec958.status[1] = IEC958_AES1_CON_CATEGORY |
					   IEC958_AES1_CON_ORIGINAL;
	return 0;
}

static int snd_hda_spdif_pmask_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.iec958.status[0] = IEC958_AES0_PROFESSIONAL |
					   IEC958_AES0_NONAUDIO |
					   IEC958_AES0_PRO_EMPHASIS_5015;
	return 0;
}

static int snd_hda_spdif_default_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);

	ucontrol->value.iec958.status[0] = codec->spdif_status & 0xff;
	ucontrol->value.iec958.status[1] = (codec->spdif_status >> 8) & 0xff;
	ucontrol->value.iec958.status[2] = (codec->spdif_status >> 16) & 0xff;
	ucontrol->value.iec958.status[3] = (codec->spdif_status >> 24) & 0xff;
	int idx = kcontrol->private_value;
	struct hda_spdif_out *spdif;

	mutex_lock(&codec->spdif_mutex);
	spdif = snd_array_elem(&codec->spdif_out, idx);
	ucontrol->value.iec958.status[0] = spdif->status & 0xff;
	ucontrol->value.iec958.status[1] = (spdif->status >> 8) & 0xff;
	ucontrol->value.iec958.status[2] = (spdif->status >> 16) & 0xff;
	ucontrol->value.iec958.status[3] = (spdif->status >> 24) & 0xff;
	mutex_unlock(&codec->spdif_mutex);

	return 0;
}

/* convert from SPDIF status bits to HDA SPDIF bits
 * bit 0 (DigEn) is always set zero (to be filled later)
 */
static unsigned short convert_from_spdif_status(unsigned int sbits)
{
	unsigned short val = 0;

	if (sbits & IEC958_AES0_PROFESSIONAL)
		val |= AC_DIG1_PROFESSIONAL;
	if (sbits & IEC958_AES0_NONAUDIO)
		val |= AC_DIG1_NONAUDIO;
	if (sbits & IEC958_AES0_PROFESSIONAL) {
		if ((sbits & IEC958_AES0_PRO_EMPHASIS) ==
		    IEC958_AES0_PRO_EMPHASIS_5015)
			val |= AC_DIG1_EMPHASIS;
	} else {
		if ((sbits & IEC958_AES0_CON_EMPHASIS) ==
		    IEC958_AES0_CON_EMPHASIS_5015)
			val |= AC_DIG1_EMPHASIS;
		if (!(sbits & IEC958_AES0_CON_NOT_COPYRIGHT))
			val |= AC_DIG1_COPYRIGHT;
		if (sbits & (IEC958_AES1_CON_ORIGINAL << 8))
			val |= AC_DIG1_LEVEL;
		val |= sbits & (IEC958_AES1_CON_CATEGORY << 8);
	}
	return val;
}

/* convert to SPDIF status bits from HDA SPDIF bits
 */
static unsigned int convert_to_spdif_status(unsigned short val)
{
	unsigned int sbits = 0;

	if (val & AC_DIG1_NONAUDIO)
		sbits |= IEC958_AES0_NONAUDIO;
	if (val & AC_DIG1_PROFESSIONAL)
		sbits |= IEC958_AES0_PROFESSIONAL;
	if (sbits & IEC958_AES0_PROFESSIONAL) {
		if (sbits & AC_DIG1_EMPHASIS)
		if (val & AC_DIG1_EMPHASIS)
			sbits |= IEC958_AES0_PRO_EMPHASIS_5015;
	} else {
		if (val & AC_DIG1_EMPHASIS)
			sbits |= IEC958_AES0_CON_EMPHASIS_5015;
		if (!(val & AC_DIG1_COPYRIGHT))
			sbits |= IEC958_AES0_CON_NOT_COPYRIGHT;
		if (val & AC_DIG1_LEVEL)
			sbits |= (IEC958_AES1_CON_ORIGINAL << 8);
		sbits |= val & (0x7f << 8);
	}
	return sbits;
}

/* set digital convert verbs both for the given NID and its slaves */
static void set_dig_out(struct hda_codec *codec, hda_nid_t nid,
			int mask, int val)
{
	const hda_nid_t *d;

	snd_hdac_regmap_update(&codec->core, nid, AC_VERB_SET_DIGI_CONVERT_1,
			       mask, val);
	d = codec->slave_dig_outs;
	if (!d)
		return;
	for (; *d; d++)
		snd_hdac_regmap_update(&codec->core, *d,
				       AC_VERB_SET_DIGI_CONVERT_1, mask, val);
}

static inline void set_dig_out_convert(struct hda_codec *codec, hda_nid_t nid,
				       int dig1, int dig2)
{
	unsigned int mask = 0;
	unsigned int val = 0;

	if (dig1 != -1) {
		mask |= 0xff;
		val = dig1;
	}
	if (dig2 != -1) {
		mask |= 0xff00;
		val |= dig2 << 8;
	}
	set_dig_out(codec, nid, mask, val);
}

static int snd_hda_spdif_default_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value;
	int idx = kcontrol->private_value;
	struct hda_spdif_out *spdif;
	hda_nid_t nid;
	unsigned short val;
	int change;

	mutex_lock(&codec->spdif_mutex);
	codec->spdif_status = ucontrol->value.iec958.status[0] |
		((unsigned int)ucontrol->value.iec958.status[1] << 8) |
		((unsigned int)ucontrol->value.iec958.status[2] << 16) |
		((unsigned int)ucontrol->value.iec958.status[3] << 24);
	val = convert_from_spdif_status(codec->spdif_status);
	val |= codec->spdif_ctls & 1;
	change = codec->spdif_ctls != val;
	codec->spdif_ctls = val;

	if (change) {
		snd_hda_codec_write_cache(codec, nid, 0,
					  AC_VERB_SET_DIGI_CONVERT_1,
					  val & 0xff);
		snd_hda_codec_write_cache(codec, nid, 0,
					  AC_VERB_SET_DIGI_CONVERT_2,
					  val >> 8);
	}

	spdif = snd_array_elem(&codec->spdif_out, idx);
	nid = spdif->nid;
	spdif->status = ucontrol->value.iec958.status[0] |
		((unsigned int)ucontrol->value.iec958.status[1] << 8) |
		((unsigned int)ucontrol->value.iec958.status[2] << 16) |
		((unsigned int)ucontrol->value.iec958.status[3] << 24);
	val = convert_from_spdif_status(spdif->status);
	val |= spdif->ctls & 1;
	change = spdif->ctls != val;
	spdif->ctls = val;
	if (change && nid != (u16)-1)
		set_dig_out_convert(codec, nid, val & 0xff, (val >> 8) & 0xff);
	mutex_unlock(&codec->spdif_mutex);
	return change;
}

#define snd_hda_spdif_out_switch_info	snd_ctl_boolean_mono_info

static int snd_hda_spdif_out_switch_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = codec->spdif_ctls & AC_DIG1_ENABLE;
	return 0;
}

	int idx = kcontrol->private_value;
	struct hda_spdif_out *spdif;

	mutex_lock(&codec->spdif_mutex);
	spdif = snd_array_elem(&codec->spdif_out, idx);
	ucontrol->value.integer.value[0] = spdif->ctls & AC_DIG1_ENABLE;
	mutex_unlock(&codec->spdif_mutex);
	return 0;
}

static inline void set_spdif_ctls(struct hda_codec *codec, hda_nid_t nid,
				  int dig1, int dig2)
{
	set_dig_out_convert(codec, nid, dig1, dig2);
	/* unmute amp switch (if any) */
	if ((get_wcaps(codec, nid) & AC_WCAP_OUT_AMP) &&
	    (dig1 & AC_DIG1_ENABLE))
		snd_hda_codec_amp_stereo(codec, nid, HDA_OUTPUT, 0,
					    HDA_AMP_MUTE, 0);
}

static int snd_hda_spdif_out_switch_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value;
	int idx = kcontrol->private_value;
	struct hda_spdif_out *spdif;
	hda_nid_t nid;
	unsigned short val;
	int change;

	mutex_lock(&codec->spdif_mutex);
	val = codec->spdif_ctls & ~AC_DIG1_ENABLE;
	if (ucontrol->value.integer.value[0])
		val |= AC_DIG1_ENABLE;
	change = codec->spdif_ctls != val;
	if (change) {
		codec->spdif_ctls = val;
		snd_hda_codec_write_cache(codec, nid, 0,
					  AC_VERB_SET_DIGI_CONVERT_1,
					  val & 0xff);
		/* unmute amp switch (if any) */
		if ((get_wcaps(codec, nid) & AC_WCAP_OUT_AMP) &&
		    (val & AC_DIG1_ENABLE))
			snd_hda_codec_amp_stereo(codec, nid, HDA_OUTPUT, 0,
						 HDA_AMP_MUTE, 0);
	}
	spdif = snd_array_elem(&codec->spdif_out, idx);
	nid = spdif->nid;
	val = spdif->ctls & ~AC_DIG1_ENABLE;
	if (ucontrol->value.integer.value[0])
		val |= AC_DIG1_ENABLE;
	change = spdif->ctls != val;
	spdif->ctls = val;
	if (change && nid != (u16)-1)
		set_spdif_ctls(codec, nid, val & 0xff, -1);
	mutex_unlock(&codec->spdif_mutex);
	return change;
}

static struct snd_kcontrol_new dig_mixes[] = {
	{
		.access = SNDRV_CTL_ELEM_ACCESS_READ,
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = SNDRV_CTL_NAME_IEC958("",PLAYBACK,CON_MASK),
		.name = SNDRV_CTL_NAME_IEC958("", PLAYBACK, CON_MASK),
		.info = snd_hda_spdif_mask_info,
		.get = snd_hda_spdif_cmask_get,
	},
	{
		.access = SNDRV_CTL_ELEM_ACCESS_READ,
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = SNDRV_CTL_NAME_IEC958("",PLAYBACK,PRO_MASK),
		.name = SNDRV_CTL_NAME_IEC958("", PLAYBACK, PRO_MASK),
		.info = snd_hda_spdif_mask_info,
		.get = snd_hda_spdif_pmask_get,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = SNDRV_CTL_NAME_IEC958("",PLAYBACK,DEFAULT),
		.name = SNDRV_CTL_NAME_IEC958("", PLAYBACK, DEFAULT),
		.info = snd_hda_spdif_mask_info,
		.get = snd_hda_spdif_default_get,
		.put = snd_hda_spdif_default_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = SNDRV_CTL_NAME_IEC958("",PLAYBACK,SWITCH),
		.name = SNDRV_CTL_NAME_IEC958("", PLAYBACK, SWITCH),
		.info = snd_hda_spdif_out_switch_info,
		.get = snd_hda_spdif_out_switch_get,
		.put = snd_hda_spdif_out_switch_put,
	},
	{ } /* end */
};

#define SPDIF_MAX_IDX	4	/* 4 instances should be enough to probe */

/**
 * snd_hda_create_spdif_out_ctls - create Output SPDIF-related controls
 * @codec: the HDA codec
 * @nid: audio out widget NID
 *
 * Creates controls related with the SPDIF output.
 * Called from each patch supporting the SPDIF out.
 *
 * Returns 0 if successful, or a negative error code.
 */
int snd_hda_create_spdif_out_ctls(struct hda_codec *codec, hda_nid_t nid)
/**
 * snd_hda_create_dig_out_ctls - create Output SPDIF-related controls
 * @codec: the HDA codec
 * @associated_nid: NID that new ctls associated with
 * @cvt_nid: converter NID
 * @type: HDA_PCM_TYPE_*
 * Creates controls related with the digital output.
 * Called from each patch supporting the digital out.
 *
 * Returns 0 if successful, or a negative error code.
 */
int snd_hda_create_dig_out_ctls(struct hda_codec *codec,
				hda_nid_t associated_nid,
				hda_nid_t cvt_nid,
				int type)
{
	int err;
	struct snd_kcontrol *kctl;
	struct snd_kcontrol_new *dig_mix;
	int idx;

	for (idx = 0; idx < SPDIF_MAX_IDX; idx++) {
		if (!_snd_hda_find_mixer_ctl(codec, "IEC958 Playback Switch",
					     idx))
			break;
	}
	if (idx >= SPDIF_MAX_IDX) {
		printk(KERN_ERR "hda_codec: too many IEC958 outputs\n");
		return -EBUSY;
	}
	for (dig_mix = dig_mixes; dig_mix->name; dig_mix++) {
		kctl = snd_ctl_new1(dig_mix, codec);
		kctl->id.index = idx;
		kctl->private_value = nid;
		err = snd_ctl_add(codec->bus->card, kctl);
		if (err < 0)
			return err;
	}
	codec->spdif_ctls =
		snd_hda_codec_read(codec, nid, 0,
				   AC_VERB_GET_DIGI_CONVERT_1, 0);
	codec->spdif_status = convert_to_spdif_status(codec->spdif_ctls);
	return 0;
}
	int idx = 0;
	int val = 0;
	const int spdif_index = 16;
	struct hda_spdif_out *spdif;
	struct hda_bus *bus = codec->bus;

	if (bus->primary_dig_out_type == HDA_PCM_TYPE_HDMI &&
	    type == HDA_PCM_TYPE_SPDIF) {
		idx = spdif_index;
	} else if (bus->primary_dig_out_type == HDA_PCM_TYPE_SPDIF &&
		   type == HDA_PCM_TYPE_HDMI) {
		/* suppose a single SPDIF device */
		for (dig_mix = dig_mixes; dig_mix->name; dig_mix++) {
			kctl = find_mixer_ctl(codec, dig_mix->name, 0, 0);
			if (!kctl)
				break;
			kctl->id.index = spdif_index;
		}
		bus->primary_dig_out_type = HDA_PCM_TYPE_HDMI;
	}
	if (!bus->primary_dig_out_type)
		bus->primary_dig_out_type = type;

	idx = find_empty_mixer_ctl_idx(codec, "IEC958 Playback Switch", idx);
	if (idx < 0) {
		codec_err(codec, "too many IEC958 outputs\n");
		return -EBUSY;
	}
	spdif = snd_array_new(&codec->spdif_out);
	if (!spdif)
		return -ENOMEM;
	for (dig_mix = dig_mixes; dig_mix->name; dig_mix++) {
		kctl = snd_ctl_new1(dig_mix, codec);
		if (!kctl)
			return -ENOMEM;
		kctl->id.index = idx;
		kctl->private_value = codec->spdif_out.used - 1;
		err = snd_hda_ctl_add(codec, associated_nid, kctl);
		if (err < 0)
			return err;
	}
	spdif->nid = cvt_nid;
	snd_hdac_regmap_read(&codec->core, cvt_nid,
			     AC_VERB_GET_DIGI_CONVERT_1, &val);
	spdif->ctls = val;
	spdif->status = convert_to_spdif_status(spdif->ctls);
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_create_dig_out_ctls);

/**
 * snd_hda_spdif_out_of_nid - get the hda_spdif_out entry from the given NID
 * @codec: the HDA codec
 * @nid: widget NID
 *
 * call within spdif_mutex lock
 */
struct hda_spdif_out *snd_hda_spdif_out_of_nid(struct hda_codec *codec,
					       hda_nid_t nid)
{
	int i;
	for (i = 0; i < codec->spdif_out.used; i++) {
		struct hda_spdif_out *spdif =
				snd_array_elem(&codec->spdif_out, i);
		if (spdif->nid == nid)
			return spdif;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(snd_hda_spdif_out_of_nid);

/**
 * snd_hda_spdif_ctls_unassign - Unassign the given SPDIF ctl
 * @codec: the HDA codec
 * @idx: the SPDIF ctl index
 *
 * Unassign the widget from the given SPDIF control.
 */
void snd_hda_spdif_ctls_unassign(struct hda_codec *codec, int idx)
{
	struct hda_spdif_out *spdif;

	mutex_lock(&codec->spdif_mutex);
	spdif = snd_array_elem(&codec->spdif_out, idx);
	spdif->nid = (u16)-1;
	mutex_unlock(&codec->spdif_mutex);
}
EXPORT_SYMBOL_GPL(snd_hda_spdif_ctls_unassign);

/**
 * snd_hda_spdif_ctls_assign - Assign the SPDIF controls to the given NID
 * @codec: the HDA codec
 * @idx: the SPDIF ctl idx
 * @nid: widget NID
 *
 * Assign the widget to the SPDIF control with the given index.
 */
void snd_hda_spdif_ctls_assign(struct hda_codec *codec, int idx, hda_nid_t nid)
{
	struct hda_spdif_out *spdif;
	unsigned short val;

	mutex_lock(&codec->spdif_mutex);
	spdif = snd_array_elem(&codec->spdif_out, idx);
	if (spdif->nid != nid) {
		spdif->nid = nid;
		val = spdif->ctls;
		set_spdif_ctls(codec, nid, val & 0xff, (val >> 8) & 0xff);
	}
	mutex_unlock(&codec->spdif_mutex);
}
EXPORT_SYMBOL_GPL(snd_hda_spdif_ctls_assign);

/*
 * SPDIF sharing with analog output
 */
static int spdif_share_sw_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct hda_multi_out *mout = snd_kcontrol_chip(kcontrol);
	ucontrol->value.integer.value[0] = mout->share_spdif;
	return 0;
}

static int spdif_share_sw_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct hda_multi_out *mout = snd_kcontrol_chip(kcontrol);
	mout->share_spdif = !!ucontrol->value.integer.value[0];
	return 0;
}

static struct snd_kcontrol_new spdif_share_sw = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "IEC958 Default PCM Playback Switch",
	.info = snd_ctl_boolean_mono_info,
	.get = spdif_share_sw_get,
	.put = spdif_share_sw_put,
};

int snd_hda_create_spdif_share_sw(struct hda_codec *codec,
				  struct hda_multi_out *mout)
{
	if (!mout->dig_out_nid)
		return 0;
	/* ATTENTION: here mout is passed as private_data, instead of codec */
	return snd_ctl_add(codec->bus->card,
			   snd_ctl_new1(&spdif_share_sw, mout));
}
/**
 * snd_hda_create_spdif_share_sw - create Default PCM switch
 * @codec: the HDA codec
 * @mout: multi-out instance
 */
int snd_hda_create_spdif_share_sw(struct hda_codec *codec,
				  struct hda_multi_out *mout)
{
	struct snd_kcontrol *kctl;

	if (!mout->dig_out_nid)
		return 0;

	kctl = snd_ctl_new1(&spdif_share_sw, mout);
	if (!kctl)
		return -ENOMEM;
	/* ATTENTION: here mout is passed as private_data, instead of codec */
	return snd_hda_ctl_add(codec, mout->dig_out_nid, kctl);
}
EXPORT_SYMBOL_GPL(snd_hda_create_spdif_share_sw);

/*
 * SPDIF input
 */

#define snd_hda_spdif_in_switch_info	snd_hda_spdif_out_switch_info

static int snd_hda_spdif_in_switch_get(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = codec->spdif_in_enable;
	return 0;
}

static int snd_hda_spdif_in_switch_put(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value;
	unsigned int val = !!ucontrol->value.integer.value[0];
	int change;

	mutex_lock(&codec->spdif_mutex);
	change = codec->spdif_in_enable != val;
	if (change) {
		codec->spdif_in_enable = val;
		snd_hda_codec_write_cache(codec, nid, 0,
					  AC_VERB_SET_DIGI_CONVERT_1, val);
		snd_hdac_regmap_write(&codec->core, nid,
				      AC_VERB_SET_DIGI_CONVERT_1, val);
	}
	mutex_unlock(&codec->spdif_mutex);
	return change;
}

static int snd_hda_spdif_in_status_get(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value;
	unsigned short val;
	unsigned int sbits;

	val = snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_DIGI_CONVERT_1, 0);
	unsigned int val;
	unsigned int sbits;

	snd_hdac_regmap_read(&codec->core, nid,
			     AC_VERB_GET_DIGI_CONVERT_1, &val);
	sbits = convert_to_spdif_status(val);
	ucontrol->value.iec958.status[0] = sbits;
	ucontrol->value.iec958.status[1] = sbits >> 8;
	ucontrol->value.iec958.status[2] = sbits >> 16;
	ucontrol->value.iec958.status[3] = sbits >> 24;
	return 0;
}

static struct snd_kcontrol_new dig_in_ctls[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = SNDRV_CTL_NAME_IEC958("",CAPTURE,SWITCH),
		.name = SNDRV_CTL_NAME_IEC958("", CAPTURE, SWITCH),
		.info = snd_hda_spdif_in_switch_info,
		.get = snd_hda_spdif_in_switch_get,
		.put = snd_hda_spdif_in_switch_put,
	},
	{
		.access = SNDRV_CTL_ELEM_ACCESS_READ,
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = SNDRV_CTL_NAME_IEC958("",CAPTURE,DEFAULT),
		.name = SNDRV_CTL_NAME_IEC958("", CAPTURE, DEFAULT),
		.info = snd_hda_spdif_mask_info,
		.get = snd_hda_spdif_in_status_get,
	},
	{ } /* end */
};

/**
 * snd_hda_create_spdif_in_ctls - create Input SPDIF-related controls
 * @codec: the HDA codec
 * @nid: audio in widget NID
 *
 * Creates controls related with the SPDIF input.
 * Called from each patch supporting the SPDIF in.
 *
 * Returns 0 if successful, or a negative error code.
 */
int snd_hda_create_spdif_in_ctls(struct hda_codec *codec, hda_nid_t nid)
{
	int err;
	struct snd_kcontrol *kctl;
	struct snd_kcontrol_new *dig_mix;
	int idx;

	for (idx = 0; idx < SPDIF_MAX_IDX; idx++) {
		if (!_snd_hda_find_mixer_ctl(codec, "IEC958 Capture Switch",
					     idx))
			break;
	}
	if (idx >= SPDIF_MAX_IDX) {
		printk(KERN_ERR "hda_codec: too many IEC958 inputs\n");
	idx = find_empty_mixer_ctl_idx(codec, "IEC958 Capture Switch", 0);
	if (idx < 0) {
		codec_err(codec, "too many IEC958 inputs\n");
		return -EBUSY;
	}
	for (dig_mix = dig_in_ctls; dig_mix->name; dig_mix++) {
		kctl = snd_ctl_new1(dig_mix, codec);
		kctl->private_value = nid;
		err = snd_ctl_add(codec->bus->card, kctl);
		if (!kctl)
			return -ENOMEM;
		kctl->private_value = nid;
		err = snd_hda_ctl_add(codec, nid, kctl);
		if (err < 0)
			return err;
	}
	codec->spdif_in_enable =
		snd_hda_codec_read(codec, nid, 0,
				   AC_VERB_GET_DIGI_CONVERT_1, 0) &
		AC_DIG1_ENABLE;
	return 0;
}

#ifdef SND_HDA_NEEDS_RESUME
/*
 * command cache
 */

/* build a 32bit cache key with the widget id and the command parameter */
#define build_cmd_cache_key(nid, verb)	((verb << 8) | nid)
#define get_cmd_cache_nid(key)		((key) & 0xff)
#define get_cmd_cache_cmd(key)		(((key) >> 8) & 0xffff)

/**
 * snd_hda_codec_write_cache - send a single command with caching
 * @codec: the HDA codec
 * @nid: NID to send the command
 * @direct: direct flag
 * @verb: the verb to send
 * @parm: the parameter for the verb
 *
 * Send a single command without waiting for response.
 *
 * Returns 0 if successful, or a negative error code.
 */
int snd_hda_codec_write_cache(struct hda_codec *codec, hda_nid_t nid,
			      int direct, unsigned int verb, unsigned int parm)
{
	int err;
	snd_hda_power_up(codec);
	mutex_lock(&codec->bus->cmd_mutex);
	err = codec->bus->ops.command(codec, nid, direct, verb, parm);
	if (!err) {
		struct hda_cache_head *c;
		u32 key = build_cmd_cache_key(nid, verb);
		c = get_alloc_hash(&codec->cmd_cache, key);
		if (c)
			c->val = parm;
	}
	mutex_unlock(&codec->bus->cmd_mutex);
	snd_hda_power_down(codec);
	return err;
}

/* resume the all commands from the cache */
void snd_hda_codec_resume_cache(struct hda_codec *codec)
{
	struct hda_cache_head *buffer = codec->cmd_cache.buffer;
	int i;

	for (i = 0; i < codec->cmd_cache.size; i++, buffer++) {
		u32 key = buffer->key;
		if (!key)
			continue;
		snd_hda_codec_write(codec, get_cmd_cache_nid(key), 0,
				    get_cmd_cache_cmd(key), buffer->val);
	}
}

/**
 * snd_hda_sequence_write_cache - sequence writes with caching
 * @codec: the HDA codec
 * @seq: VERB array to send
 *
 * Send the commands sequentially from the given array.
 * Thte commands are recorded on cache for power-save and resume.
 * The array must be terminated with NID=0.
 */
void snd_hda_sequence_write_cache(struct hda_codec *codec,
				  const struct hda_verb *seq)
{
	for (; seq->nid; seq++)
		snd_hda_codec_write_cache(codec, seq->nid, 0, seq->verb,
					  seq->param);
}
#endif /* SND_HDA_NEEDS_RESUME */

/*
 * set power state of the codec
 */
static void hda_set_power_state(struct hda_codec *codec, hda_nid_t fg,
				unsigned int power_state)
{
	hda_nid_t nid;
	int i;

	snd_hda_codec_write(codec, fg, 0, AC_VERB_SET_POWER_STATE,
			    power_state);
	msleep(10); /* partial workaround for "azx_get_response timeout" */

	nid = codec->start_nid;
	for (i = 0; i < codec->num_nodes; i++, nid++) {
		unsigned int wcaps = get_wcaps(codec, nid);
		if (wcaps & AC_WCAP_POWER) {
			unsigned int wid_type = (wcaps & AC_WCAP_TYPE) >>
				AC_WCAP_TYPE_SHIFT;
			if (wid_type == AC_WID_PIN) {
				unsigned int pincap;
				/*
				 * don't power down the widget if it controls
				 * eapd and EAPD_BTLENABLE is set.
				 */
				pincap = snd_hda_param_read(codec, nid,
							    AC_PAR_PIN_CAP);
				if (pincap & AC_PINCAP_EAPD) {
					int eapd = snd_hda_codec_read(codec,
						nid, 0,
						AC_VERB_GET_EAPD_BTLENABLE, 0);
					eapd &= 0x02;
					if (power_state == AC_PWRST_D3 && eapd)
						continue;
				}
			}
			snd_hda_codec_write(codec, nid, 0,
					    AC_VERB_SET_POWER_STATE,
					    power_state);
		}
	}

	if (power_state == AC_PWRST_D0) {
		unsigned long end_time;
		int state;
		msleep(10);
		/* wait until the codec reachs to D0 */
		end_time = jiffies + msecs_to_jiffies(500);
		do {
			state = snd_hda_codec_read(codec, fg, 0,
						   AC_VERB_GET_POWER_STATE, 0);
			if (state == power_state)
				break;
			msleep(1);
		} while (time_after_eq(end_time, jiffies));
	}
}

#ifdef SND_HDA_NEEDS_RESUME
/*
 * call suspend and power-down; used both from PM and power-save
 */
static void hda_call_codec_suspend(struct hda_codec *codec)
{
	if (codec->patch_ops.suspend)
		codec->patch_ops.suspend(codec, PMSG_SUSPEND);
	hda_set_power_state(codec,
			    codec->afg ? codec->afg : codec->mfg,
			    AC_PWRST_D3);
#ifdef CONFIG_SND_HDA_POWER_SAVE
	cancel_delayed_work(&codec->power_work);
	codec->power_on = 0;
	codec->power_transition = 0;
#endif
EXPORT_SYMBOL_GPL(snd_hda_create_spdif_in_ctls);

/**
 * snd_hda_codec_set_power_to_all - Set the power state to all widgets
 * @codec: the HDA codec
 * @fg: function group (not used now)
 * @power_state: the power state to set (AC_PWRST_*)
 *
 * Set the given power state to all widgets that have the power control.
 * If the codec has power_filter set, it evaluates the power state and
 * filter out if it's unchanged as D3.
 */
void snd_hda_codec_set_power_to_all(struct hda_codec *codec, hda_nid_t fg,
				    unsigned int power_state)
{
	hda_nid_t nid;

	for_each_hda_codec_node(nid, codec) {
		unsigned int wcaps = get_wcaps(codec, nid);
		unsigned int state = power_state;
		if (!(wcaps & AC_WCAP_POWER))
			continue;
		if (codec->power_filter) {
			state = codec->power_filter(codec, nid, power_state);
			if (state != power_state && power_state == AC_PWRST_D3)
				continue;
		}
		snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_POWER_STATE,
				    state);
	}
}
EXPORT_SYMBOL_GPL(snd_hda_codec_set_power_to_all);

/*
 * wait until the state is reached, returns the current state
 */
static unsigned int hda_sync_power_state(struct hda_codec *codec,
					 hda_nid_t fg,
					 unsigned int power_state)
{
	unsigned long end_time = jiffies + msecs_to_jiffies(500);
	unsigned int state, actual_state;

	for (;;) {
		state = snd_hda_codec_read(codec, fg, 0,
					   AC_VERB_GET_POWER_STATE, 0);
		if (state & AC_PWRST_ERROR)
			break;
		actual_state = (state >> 4) & 0x0f;
		if (actual_state == power_state)
			break;
		if (time_after_eq(jiffies, end_time))
			break;
		/* wait until the codec reachs to the target state */
		msleep(1);
	}
	return state;
}

/**
 * snd_hda_codec_eapd_power_filter - A power filter callback for EAPD
 * @codec: the HDA codec
 * @nid: widget NID
 * @power_state: power state to evalue
 *
 * Don't power down the widget if it controls eapd and EAPD_BTLENABLE is set.
 * This can be used a codec power_filter callback.
 */
unsigned int snd_hda_codec_eapd_power_filter(struct hda_codec *codec,
					     hda_nid_t nid,
					     unsigned int power_state)
{
	if (nid == codec->core.afg || nid == codec->core.mfg)
		return power_state;
	if (power_state == AC_PWRST_D3 &&
	    get_wcaps_type(get_wcaps(codec, nid)) == AC_WID_PIN &&
	    (snd_hda_query_pin_caps(codec, nid) & AC_PINCAP_EAPD)) {
		int eapd = snd_hda_codec_read(codec, nid, 0,
					      AC_VERB_GET_EAPD_BTLENABLE, 0);
		if (eapd & 0x02)
			return AC_PWRST_D0;
	}
	return power_state;
}
EXPORT_SYMBOL_GPL(snd_hda_codec_eapd_power_filter);

/*
 * set power state of the codec, and return the power state
 */
static unsigned int hda_set_power_state(struct hda_codec *codec,
					unsigned int power_state)
{
	hda_nid_t fg = codec->core.afg ? codec->core.afg : codec->core.mfg;
	int count;
	unsigned int state;
	int flags = 0;

	/* this delay seems necessary to avoid click noise at power-down */
	if (power_state == AC_PWRST_D3) {
		if (codec->depop_delay < 0)
			msleep(codec_has_epss(codec) ? 10 : 100);
		else if (codec->depop_delay > 0)
			msleep(codec->depop_delay);
		flags = HDA_RW_NO_RESPONSE_FALLBACK;
	}

	/* repeat power states setting at most 10 times*/
	for (count = 0; count < 10; count++) {
		if (codec->patch_ops.set_power_state)
			codec->patch_ops.set_power_state(codec, fg,
							 power_state);
		else {
			state = power_state;
			if (codec->power_filter)
				state = codec->power_filter(codec, fg, state);
			if (state == power_state || power_state != AC_PWRST_D3)
				snd_hda_codec_read(codec, fg, flags,
						   AC_VERB_SET_POWER_STATE,
						   state);
			snd_hda_codec_set_power_to_all(codec, fg, power_state);
		}
		state = hda_sync_power_state(codec, fg, power_state);
		if (!(state & AC_PWRST_ERROR))
			break;
	}

	return state;
}

/* sync power states of all widgets;
 * this is called at the end of codec parsing
 */
static void sync_power_up_states(struct hda_codec *codec)
{
	hda_nid_t nid;

	/* don't care if no filter is used */
	if (!codec->power_filter)
		return;

	for_each_hda_codec_node(nid, codec) {
		unsigned int wcaps = get_wcaps(codec, nid);
		unsigned int target;
		if (!(wcaps & AC_WCAP_POWER))
			continue;
		target = codec->power_filter(codec, nid, AC_PWRST_D0);
		if (target == AC_PWRST_D0)
			continue;
		if (!snd_hda_check_power_state(codec, nid, target))
			snd_hda_codec_write(codec, nid, 0,
					    AC_VERB_SET_POWER_STATE, target);
	}
}

#ifdef CONFIG_SND_HDA_RECONFIG
/* execute additional init verbs */
static void hda_exec_init_verbs(struct hda_codec *codec)
{
	if (codec->init_verbs.list)
		snd_hda_sequence_write(codec, codec->init_verbs.list);
}
#else
static inline void hda_exec_init_verbs(struct hda_codec *codec) {}
#endif

#ifdef CONFIG_PM
/* update the power on/off account with the current jiffies */
static void update_power_acct(struct hda_codec *codec, bool on)
{
	unsigned long delta = jiffies - codec->power_jiffies;

	if (on)
		codec->power_on_acct += delta;
	else
		codec->power_off_acct += delta;
	codec->power_jiffies += delta;
}

void snd_hda_update_power_acct(struct hda_codec *codec)
{
	update_power_acct(codec, hda_codec_is_power_on(codec));
}

/*
 * call suspend and power-down; used both from PM and power-save
 * this function returns the power state in the end
 */
static unsigned int hda_call_codec_suspend(struct hda_codec *codec)
{
	unsigned int state;

	atomic_inc(&codec->core.in_pm);

	if (codec->patch_ops.suspend)
		codec->patch_ops.suspend(codec);
	hda_cleanup_all_streams(codec);
	state = hda_set_power_state(codec, AC_PWRST_D3);
	update_power_acct(codec, true);
	atomic_dec(&codec->core.in_pm);
	return state;
}

/*
 * kick up codec; used both from PM and power-save
 */
static void hda_call_codec_resume(struct hda_codec *codec)
{
	hda_set_power_state(codec,
			    codec->afg ? codec->afg : codec->mfg,
			    AC_PWRST_D0);
	atomic_inc(&codec->core.in_pm);

	if (codec->core.regmap)
		regcache_mark_dirty(codec->core.regmap);

	codec->power_jiffies = jiffies;

	hda_set_power_state(codec, AC_PWRST_D0);
	restore_shutup_pins(codec);
	hda_exec_init_verbs(codec);
	snd_hda_jack_set_dirty_all(codec);
	if (codec->patch_ops.resume)
		codec->patch_ops.resume(codec);
	else {
		if (codec->patch_ops.init)
			codec->patch_ops.init(codec);
		snd_hda_codec_resume_amp(codec);
		snd_hda_codec_resume_cache(codec);
	}
}
#endif /* SND_HDA_NEEDS_RESUME */


/**
 * snd_hda_build_controls - build mixer controls
 * @bus: the BUS
 *
 * Creates mixer controls for each codec included in the bus.
 *
 * Returns 0 if successful, otherwise a negative error code.
 */
int __devinit snd_hda_build_controls(struct hda_bus *bus)
{
	struct hda_codec *codec;

	list_for_each_entry(codec, &bus->codec_list, list) {
		int err = 0;
		/* fake as if already powered-on */
		hda_keep_power_on(codec);
		/* then fire up */
		hda_set_power_state(codec,
				    codec->afg ? codec->afg : codec->mfg,
				    AC_PWRST_D0);
		/* continue to initialize... */
		if (codec->patch_ops.init)
			err = codec->patch_ops.init(codec);
		if (!err && codec->patch_ops.build_controls)
			err = codec->patch_ops.build_controls(codec);
		snd_hda_power_down(codec);
		if (err < 0)
			return err;
	}

	return 0;
}

/*
 * stream formats
 */
struct hda_rate_tbl {
	unsigned int hz;
	unsigned int alsa_bits;
	unsigned int hda_fmt;
};

static struct hda_rate_tbl rate_bits[] = {
	/* rate in Hz, ALSA rate bitmask, HDA format value */

	/* autodetected value used in snd_hda_query_supported_pcm */
	{ 8000, SNDRV_PCM_RATE_8000, 0x0500 }, /* 1/6 x 48 */
	{ 11025, SNDRV_PCM_RATE_11025, 0x4300 }, /* 1/4 x 44 */
	{ 16000, SNDRV_PCM_RATE_16000, 0x0200 }, /* 1/3 x 48 */
	{ 22050, SNDRV_PCM_RATE_22050, 0x4100 }, /* 1/2 x 44 */
	{ 32000, SNDRV_PCM_RATE_32000, 0x0a00 }, /* 2/3 x 48 */
	{ 44100, SNDRV_PCM_RATE_44100, 0x4000 }, /* 44 */
	{ 48000, SNDRV_PCM_RATE_48000, 0x0000 }, /* 48 */
	{ 88200, SNDRV_PCM_RATE_88200, 0x4800 }, /* 2 x 44 */
	{ 96000, SNDRV_PCM_RATE_96000, 0x0800 }, /* 2 x 48 */
	{ 176400, SNDRV_PCM_RATE_176400, 0x5800 },/* 4 x 44 */
	{ 192000, SNDRV_PCM_RATE_192000, 0x1800 }, /* 4 x 48 */
#define AC_PAR_PCM_RATE_BITS	11
	/* up to bits 10, 384kHZ isn't supported properly */

	/* not autodetected value */
	{ 9600, SNDRV_PCM_RATE_KNOT, 0x0400 }, /* 1/5 x 48 */

	{ 0 } /* terminator */
};

/**
 * snd_hda_calc_stream_format - calculate format bitset
 * @rate: the sample rate
 * @channels: the number of channels
 * @format: the PCM format (SNDRV_PCM_FORMAT_XXX)
 * @maxbps: the max. bps
 *
 * Calculate the format bitset from the given rate, channels and th PCM format.
 *
 * Return zero if invalid.
 */
unsigned int snd_hda_calc_stream_format(unsigned int rate,
					unsigned int channels,
					unsigned int format,
					unsigned int maxbps)
{
	int i;
	unsigned int val = 0;

	for (i = 0; rate_bits[i].hz; i++)
		if (rate_bits[i].hz == rate) {
			val = rate_bits[i].hda_fmt;
			break;
		}
	if (!rate_bits[i].hz) {
		snd_printdd("invalid rate %d\n", rate);
		return 0;
	}

	if (channels == 0 || channels > 8) {
		snd_printdd("invalid channels %d\n", channels);
		return 0;
	}
	val |= channels - 1;

	switch (snd_pcm_format_width(format)) {
	case 8:  val |= 0x00; break;
	case 16: val |= 0x10; break;
	case 20:
	case 24:
	case 32:
		if (maxbps >= 32)
			val |= 0x40;
		else if (maxbps >= 24)
			val |= 0x30;
		else
			val |= 0x20;
		break;
	default:
		snd_printdd("invalid format width %d\n",
			    snd_pcm_format_width(format));
		return 0;
	}

	return val;
}

/**
 * snd_hda_query_supported_pcm - query the supported PCM rates and formats
 * @codec: the HDA codec
 * @nid: NID to query
 * @ratesp: the pointer to store the detected rate bitflags
 * @formatsp: the pointer to store the detected formats
 * @bpsp: the pointer to store the detected format widths
 *
 * Queries the supported PCM rates and formats.  The NULL @ratesp, @formatsp
 * or @bsps argument is ignored.
 *
 * Returns 0 if successful, otherwise a negative error code.
 */
int snd_hda_query_supported_pcm(struct hda_codec *codec, hda_nid_t nid,
				u32 *ratesp, u64 *formatsp, unsigned int *bpsp)
{
	int i;
	unsigned int val, streams;

	val = 0;
	if (nid != codec->afg &&
	    (get_wcaps(codec, nid) & AC_WCAP_FORMAT_OVRD)) {
		val = snd_hda_param_read(codec, nid, AC_PAR_PCM);
		if (val == -1)
			return -EIO;
	}
	if (!val)
		val = snd_hda_param_read(codec, codec->afg, AC_PAR_PCM);

	if (ratesp) {
		u32 rates = 0;
		for (i = 0; i < AC_PAR_PCM_RATE_BITS; i++) {
			if (val & (1 << i))
				rates |= rate_bits[i].alsa_bits;
		}
		*ratesp = rates;
	}

	if (formatsp || bpsp) {
		u64 formats = 0;
		unsigned int bps;
		unsigned int wcaps;

		wcaps = get_wcaps(codec, nid);
		streams = snd_hda_param_read(codec, nid, AC_PAR_STREAM);
		if (streams == -1)
			return -EIO;
		if (!streams) {
			streams = snd_hda_param_read(codec, codec->afg,
						     AC_PAR_STREAM);
			if (streams == -1)
				return -EIO;
		}

		bps = 0;
		if (streams & AC_SUPFMT_PCM) {
			if (val & AC_SUPPCM_BITS_8) {
				formats |= SNDRV_PCM_FMTBIT_U8;
				bps = 8;
			}
			if (val & AC_SUPPCM_BITS_16) {
				formats |= SNDRV_PCM_FMTBIT_S16_LE;
				bps = 16;
			}
			if (wcaps & AC_WCAP_DIGITAL) {
				if (val & AC_SUPPCM_BITS_32)
					formats |= SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_LE;
				if (val & (AC_SUPPCM_BITS_20|AC_SUPPCM_BITS_24))
					formats |= SNDRV_PCM_FMTBIT_S32_LE;
				if (val & AC_SUPPCM_BITS_24)
					bps = 24;
				else if (val & AC_SUPPCM_BITS_20)
					bps = 20;
			} else if (val & (AC_SUPPCM_BITS_20|AC_SUPPCM_BITS_24|
					  AC_SUPPCM_BITS_32)) {
				formats |= SNDRV_PCM_FMTBIT_S32_LE;
				if (val & AC_SUPPCM_BITS_32)
					bps = 32;
				else if (val & AC_SUPPCM_BITS_24)
					bps = 24;
				else if (val & AC_SUPPCM_BITS_20)
					bps = 20;
			}
		}
		else if (streams == AC_SUPFMT_FLOAT32) {
			/* should be exclusive */
			formats |= SNDRV_PCM_FMTBIT_FLOAT_LE;
			bps = 32;
		} else if (streams == AC_SUPFMT_AC3) {
			/* should be exclusive */
			/* temporary hack: we have still no proper support
			 * for the direct AC3 stream...
			 */
			formats |= SNDRV_PCM_FMTBIT_U8;
			bps = 8;
		}
		if (formatsp)
			*formatsp = formats;
		if (bpsp)
			*bpsp = bps;
	}

	return 0;
}

/**
 * snd_hda_is_supported_format - check whether the given node supports
 * the format val
 *
 * Returns 1 if supported, 0 if not.
 */
int snd_hda_is_supported_format(struct hda_codec *codec, hda_nid_t nid,
				unsigned int format)
{
	int i;
	unsigned int val = 0, rate, stream;

	if (nid != codec->afg &&
	    (get_wcaps(codec, nid) & AC_WCAP_FORMAT_OVRD)) {
		val = snd_hda_param_read(codec, nid, AC_PAR_PCM);
		if (val == -1)
			return 0;
	}
	if (!val) {
		val = snd_hda_param_read(codec, codec->afg, AC_PAR_PCM);
		if (val == -1)
			return 0;
	}

	rate = format & 0xff00;
	for (i = 0; i < AC_PAR_PCM_RATE_BITS; i++)
		if (rate_bits[i].hda_fmt == rate) {
			if (val & (1 << i))
				break;
			return 0;
		}
	if (i >= AC_PAR_PCM_RATE_BITS)
		return 0;

	stream = snd_hda_param_read(codec, nid, AC_PAR_STREAM);
	if (stream == -1)
		return 0;
	if (!stream && nid != codec->afg)
		stream = snd_hda_param_read(codec, codec->afg, AC_PAR_STREAM);
	if (!stream || stream == -1)
		return 0;

	if (stream & AC_SUPFMT_PCM) {
		switch (format & 0xf0) {
		case 0x00:
			if (!(val & AC_SUPPCM_BITS_8))
				return 0;
			break;
		case 0x10:
			if (!(val & AC_SUPPCM_BITS_16))
				return 0;
			break;
		case 0x20:
			if (!(val & AC_SUPPCM_BITS_20))
				return 0;
			break;
		case 0x30:
			if (!(val & AC_SUPPCM_BITS_24))
				return 0;
			break;
		case 0x40:
			if (!(val & AC_SUPPCM_BITS_32))
				return 0;
			break;
		default:
			return 0;
		}
	} else {
		/* FIXME: check for float32 and AC3? */
	}

	return 1;
		if (codec->core.regmap)
			regcache_sync(codec->core.regmap);
	}

	if (codec->jackpoll_interval)
		hda_jackpoll_work(&codec->jackpoll_work.work);
	else
		snd_hda_jack_report_sync(codec);
	codec->core.dev.power.power_state = PMSG_ON;
	atomic_dec(&codec->core.in_pm);
}

static int hda_codec_runtime_suspend(struct device *dev)
{
	struct hda_codec *codec = dev_to_hda_codec(dev);
	struct hda_pcm *pcm;
	unsigned int state;

	cancel_delayed_work_sync(&codec->jackpoll_work);
	list_for_each_entry(pcm, &codec->pcm_list_head, list)
		snd_pcm_suspend_all(pcm->pcm);
	state = hda_call_codec_suspend(codec);
	if (codec_has_clkstop(codec) && codec_has_epss(codec) &&
	    (state & AC_PWRST_CLK_STOP_OK))
		snd_hdac_codec_link_down(&codec->core);
	snd_hdac_link_power(&codec->core, false);
	return 0;
}

static int hda_codec_runtime_resume(struct device *dev)
{
	struct hda_codec *codec = dev_to_hda_codec(dev);

	snd_hdac_link_power(&codec->core, true);
	snd_hdac_codec_link_up(&codec->core);
	hda_call_codec_resume(codec);
	pm_runtime_mark_last_busy(dev);
	return 0;
}
#endif /* CONFIG_PM */

#ifdef CONFIG_PM_SLEEP
static int hda_codec_force_resume(struct device *dev)
{
	int ret;

	/* The get/put pair below enforces the runtime resume even if the
	 * device hasn't been used at suspend time.  This trick is needed to
	 * update the jack state change during the sleep.
	 */
	pm_runtime_get_noresume(dev);
	ret = pm_runtime_force_resume(dev);
	pm_runtime_put(dev);
	return ret;
}

static int hda_codec_pm_suspend(struct device *dev)
{
	dev->power.power_state = PMSG_SUSPEND;
	return pm_runtime_force_suspend(dev);
}

static int hda_codec_pm_resume(struct device *dev)
{
	dev->power.power_state = PMSG_RESUME;
	return hda_codec_force_resume(dev);
}

static int hda_codec_pm_freeze(struct device *dev)
{
	dev->power.power_state = PMSG_FREEZE;
	return pm_runtime_force_suspend(dev);
}

static int hda_codec_pm_thaw(struct device *dev)
{
	dev->power.power_state = PMSG_THAW;
	return hda_codec_force_resume(dev);
}

static int hda_codec_pm_restore(struct device *dev)
{
	dev->power.power_state = PMSG_RESTORE;
	return hda_codec_force_resume(dev);
}
#endif /* CONFIG_PM_SLEEP */

/* referred in hda_bind.c */
const struct dev_pm_ops hda_codec_driver_pm = {
#ifdef CONFIG_PM_SLEEP
	.suspend = hda_codec_pm_suspend,
	.resume = hda_codec_pm_resume,
	.freeze = hda_codec_pm_freeze,
	.thaw = hda_codec_pm_thaw,
	.poweroff = hda_codec_pm_suspend,
	.restore = hda_codec_pm_restore,
#endif /* CONFIG_PM_SLEEP */
	SET_RUNTIME_PM_OPS(hda_codec_runtime_suspend, hda_codec_runtime_resume,
			   NULL)
};

/*
 * add standard channel maps if not specified
 */
static int add_std_chmaps(struct hda_codec *codec)
{
	struct hda_pcm *pcm;
	int str, err;

	list_for_each_entry(pcm, &codec->pcm_list_head, list) {
		for (str = 0; str < 2; str++) {
			struct hda_pcm_stream *hinfo = &pcm->stream[str];
			struct snd_pcm_chmap *chmap;
			const struct snd_pcm_chmap_elem *elem;

			if (!pcm->pcm || pcm->own_chmap || !hinfo->substreams)
				continue;
			elem = hinfo->chmap ? hinfo->chmap : snd_pcm_std_chmaps;
			err = snd_pcm_add_chmap_ctls(pcm->pcm, str, elem,
						     hinfo->channels_max,
						     0, &chmap);
			if (err < 0)
				return err;
			chmap->channel_mask = SND_PCM_CHMAP_MASK_2468;
		}
	}
	return 0;
}

/* default channel maps for 2.1 speakers;
 * since HD-audio supports only stereo, odd number channels are omitted
 */
const struct snd_pcm_chmap_elem snd_pcm_2_1_chmaps[] = {
	{ .channels = 2,
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR } },
	{ .channels = 4,
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR,
		   SNDRV_CHMAP_LFE, SNDRV_CHMAP_LFE } },
	{ }
};
EXPORT_SYMBOL_GPL(snd_pcm_2_1_chmaps);

int snd_hda_codec_build_controls(struct hda_codec *codec)
{
	int err = 0;
	hda_exec_init_verbs(codec);
	/* continue to initialize... */
	if (codec->patch_ops.init)
		err = codec->patch_ops.init(codec);
	if (!err && codec->patch_ops.build_controls)
		err = codec->patch_ops.build_controls(codec);
	if (err < 0)
		return err;

	/* we create chmaps here instead of build_pcms */
	err = add_std_chmaps(codec);
	if (err < 0)
		return err;

	if (codec->jackpoll_interval)
		hda_jackpoll_work(&codec->jackpoll_work.work);
	else
		snd_hda_jack_report_sync(codec); /* call at the last init point */
	sync_power_up_states(codec);
	return 0;
}

/*
 * PCM stuff
 */
static int hda_pcm_default_open_close(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      struct snd_pcm_substream *substream)
{
	return 0;
}

static int hda_pcm_default_prepare(struct hda_pcm_stream *hinfo,
				   struct hda_codec *codec,
				   unsigned int stream_tag,
				   unsigned int format,
				   struct snd_pcm_substream *substream)
{
	snd_hda_codec_setup_stream(codec, hinfo->nid, stream_tag, 0, format);
	return 0;
}

static int hda_pcm_default_cleanup(struct hda_pcm_stream *hinfo,
				   struct hda_codec *codec,
				   struct snd_pcm_substream *substream)
{
	snd_hda_codec_cleanup_stream(codec, hinfo->nid);
	return 0;
}

static int __devinit set_pcm_default_values(struct hda_codec *codec,
					    struct hda_pcm_stream *info)
{
	/* query support PCM information from the given NID */
	if (info->nid && (!info->rates || !info->formats)) {
		snd_hda_query_supported_pcm(codec, info->nid,
				info->rates ? NULL : &info->rates,
				info->formats ? NULL : &info->formats,
				info->maxbps ? NULL : &info->maxbps);
static int set_pcm_default_values(struct hda_codec *codec,
				  struct hda_pcm_stream *info)
{
	int err;

	/* query support PCM information from the given NID */
	if (info->nid && (!info->rates || !info->formats)) {
		err = snd_hda_query_supported_pcm(codec, info->nid,
				info->rates ? NULL : &info->rates,
				info->formats ? NULL : &info->formats,
				info->maxbps ? NULL : &info->maxbps);
		if (err < 0)
			return err;
	}
	if (info->ops.open == NULL)
		info->ops.open = hda_pcm_default_open_close;
	if (info->ops.close == NULL)
		info->ops.close = hda_pcm_default_open_close;
	if (info->ops.prepare == NULL) {
		snd_assert(info->nid, return -EINVAL);
		info->ops.prepare = hda_pcm_default_prepare;
	}
	if (info->ops.cleanup == NULL) {
		snd_assert(info->nid, return -EINVAL);
		if (snd_BUG_ON(!info->nid))
			return -EINVAL;
		info->ops.prepare = hda_pcm_default_prepare;
	}
	if (info->ops.cleanup == NULL) {
		if (snd_BUG_ON(!info->nid))
			return -EINVAL;
		info->ops.cleanup = hda_pcm_default_cleanup;
	}
	return 0;
}

/**
 * snd_hda_build_pcms - build PCM information
 * @bus: the BUS
 *
 * Create PCM information for each codec included in the bus.
 *
 * The build_pcms codec patch is requested to set up codec->num_pcms and
 * codec->pcm_info properly.  The array is referred by the top-level driver
 * to create its PCM instances.
 * The allocated codec->pcm_info should be released in codec->patch_ops.free
 * callback.
 *
 * At least, substreams, channels_min and channels_max must be filled for
 * each stream.  substreams = 0 indicates that the stream doesn't exist.
 * When rates and/or formats are zero, the supported values are queried
 * from the given nid.  The nid is used also by the default ops.prepare
 * and ops.cleanup callbacks.
 *
 * The driver needs to call ops.open in its open callback.  Similarly,
 * ops.close is supposed to be called in the close callback.
 * ops.prepare should be called in the prepare or hw_params callback
 * with the proper parameters for set up.
 * ops.cleanup should be called in hw_free for clean up of streams.
 *
 * This function returns 0 if successfull, or a negative error code.
 */
int __devinit snd_hda_build_pcms(struct hda_bus *bus)
{
	struct hda_codec *codec;

	list_for_each_entry(codec, &bus->codec_list, list) {
		unsigned int pcm, s;
		int err;
		if (!codec->patch_ops.build_pcms)
			continue;
		err = codec->patch_ops.build_pcms(codec);
		if (err < 0)
			return err;
		for (pcm = 0; pcm < codec->num_pcms; pcm++) {
			for (s = 0; s < 2; s++) {
				struct hda_pcm_stream *info;
				info = &codec->pcm_info[pcm].stream[s];
				if (!info->substreams)
					continue;
				err = set_pcm_default_values(codec, info);
				if (err < 0)
					return err;
			}
		}
	}
	return 0;
}

/**
 * snd_hda_check_board_config - compare the current codec with the config table
 * @codec: the HDA codec
 * @num_configs: number of config enums
 * @models: array of model name strings
 * @tbl: configuration table, terminated by null entries
 *
 * Compares the modelname or PCI subsystem id of the current codec with the
 * given configuration table.  If a matching entry is found, returns its
 * config value (supposed to be 0 or positive).
 *
 * If no entries are matching, the function returns a negative value.
 */
int snd_hda_check_board_config(struct hda_codec *codec,
			       int num_configs, const char **models,
			       const struct snd_pci_quirk *tbl)
{
	if (codec->bus->modelname && models) {
		int i;
		for (i = 0; i < num_configs; i++) {
			if (models[i] &&
			    !strcmp(codec->bus->modelname, models[i])) {
				snd_printd(KERN_INFO "hda_codec: model '%s' is "
					   "selected\n", models[i]);
				return i;
			}
		}
	}

	if (!codec->bus->pci || !tbl)
		return -1;

	tbl = snd_pci_quirk_lookup(codec->bus->pci, tbl);
	if (!tbl)
		return -1;
	if (tbl->value >= 0 && tbl->value < num_configs) {
#ifdef CONFIG_SND_DEBUG_VERBOSE
		char tmp[10];
		const char *model = NULL;
		if (models)
			model = models[tbl->value];
		if (!model) {
			sprintf(tmp, "#%d", tbl->value);
			model = tmp;
		}
		snd_printdd(KERN_INFO "hda_codec: model '%s' is selected "
			    "for config %x:%x (%s)\n",
			    model, tbl->subvendor, tbl->subdevice,
			    (tbl->name ? tbl->name : "Unknown device"));
#endif
		return tbl->value;
	}
	return -1;
/*
 * codec prepare/cleanup entries
 */
/**
 * snd_hda_codec_prepare - Prepare a stream
 * @codec: the HDA codec
 * @hinfo: PCM information
 * @stream: stream tag to assign
 * @format: format id to assign
 * @substream: PCM substream to assign
 *
 * Calls the prepare callback set by the codec with the given arguments.
 * Clean up the inactive streams when successful.
 */
int snd_hda_codec_prepare(struct hda_codec *codec,
			  struct hda_pcm_stream *hinfo,
			  unsigned int stream,
			  unsigned int format,
			  struct snd_pcm_substream *substream)
{
	int ret;
	mutex_lock(&codec->bus->prepare_mutex);
	if (hinfo->ops.prepare)
		ret = hinfo->ops.prepare(hinfo, codec, stream, format,
					 substream);
	else
		ret = -ENODEV;
	if (ret >= 0)
		purify_inactive_streams(codec);
	mutex_unlock(&codec->bus->prepare_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(snd_hda_codec_prepare);

/**
 * snd_hda_codec_cleanup - Prepare a stream
 * @codec: the HDA codec
 * @hinfo: PCM information
 * @substream: PCM substream
 *
 * Calls the cleanup callback set by the codec with the given arguments.
 */
void snd_hda_codec_cleanup(struct hda_codec *codec,
			   struct hda_pcm_stream *hinfo,
			   struct snd_pcm_substream *substream)
{
	mutex_lock(&codec->bus->prepare_mutex);
	if (hinfo->ops.cleanup)
		hinfo->ops.cleanup(hinfo, codec, substream);
	mutex_unlock(&codec->bus->prepare_mutex);
}
EXPORT_SYMBOL_GPL(snd_hda_codec_cleanup);

/* global */
const char *snd_hda_pcm_type_name[HDA_PCM_NTYPES] = {
	"Audio", "SPDIF", "HDMI", "Modem"
};

/*
 * get the empty PCM device number to assign
 */
static int get_empty_pcm_device(struct hda_bus *bus, unsigned int type)
{
	/* audio device indices; not linear to keep compatibility */
	/* assigned to static slots up to dev#10; if more needed, assign
	 * the later slot dynamically (when CONFIG_SND_DYNAMIC_MINORS=y)
	 */
	static int audio_idx[HDA_PCM_NTYPES][5] = {
		[HDA_PCM_TYPE_AUDIO] = { 0, 2, 4, 5, -1 },
		[HDA_PCM_TYPE_SPDIF] = { 1, -1 },
		[HDA_PCM_TYPE_HDMI]  = { 3, 7, 8, 9, -1 },
		[HDA_PCM_TYPE_MODEM] = { 6, -1 },
	};
	int i;

	if (type >= HDA_PCM_NTYPES) {
		dev_err(bus->card->dev, "Invalid PCM type %d\n", type);
		return -EINVAL;
	}

	for (i = 0; audio_idx[type][i] >= 0; i++) {
#ifndef CONFIG_SND_DYNAMIC_MINORS
		if (audio_idx[type][i] >= 8)
			break;
#endif
		if (!test_and_set_bit(audio_idx[type][i], bus->pcm_dev_bits))
			return audio_idx[type][i];
	}

#ifdef CONFIG_SND_DYNAMIC_MINORS
	/* non-fixed slots starting from 10 */
	for (i = 10; i < 32; i++) {
		if (!test_and_set_bit(i, bus->pcm_dev_bits))
			return i;
	}
#endif

	dev_warn(bus->card->dev, "Too many %s devices\n",
		snd_hda_pcm_type_name[type]);
#ifndef CONFIG_SND_DYNAMIC_MINORS
	dev_warn(bus->card->dev,
		 "Consider building the kernel with CONFIG_SND_DYNAMIC_MINORS=y\n");
#endif
	return -EAGAIN;
}

/* call build_pcms ops of the given codec and set up the default parameters */
int snd_hda_codec_parse_pcms(struct hda_codec *codec)
{
	struct hda_pcm *cpcm;
	int err;

	if (!list_empty(&codec->pcm_list_head))
		return 0; /* already parsed */

	if (!codec->patch_ops.build_pcms)
		return 0;

	err = codec->patch_ops.build_pcms(codec);
	if (err < 0) {
		codec_err(codec, "cannot build PCMs for #%d (error %d)\n",
			  codec->core.addr, err);
		return err;
	}

	list_for_each_entry(cpcm, &codec->pcm_list_head, list) {
		int stream;

		for (stream = 0; stream < 2; stream++) {
			struct hda_pcm_stream *info = &cpcm->stream[stream];

			if (!info->substreams)
				continue;
			err = set_pcm_default_values(codec, info);
			if (err < 0) {
				codec_warn(codec,
					   "fail to setup default for PCM %s\n",
					   cpcm->name);
				return err;
			}
		}
	}

	return 0;
}

/* assign all PCMs of the given codec */
int snd_hda_codec_build_pcms(struct hda_codec *codec)
{
	struct hda_bus *bus = codec->bus;
	struct hda_pcm *cpcm;
	int dev, err;

	err = snd_hda_codec_parse_pcms(codec);
	if (err < 0)
		return err;

	/* attach a new PCM streams */
	list_for_each_entry(cpcm, &codec->pcm_list_head, list) {
		if (cpcm->pcm)
			continue; /* already attached */
		if (!cpcm->stream[0].substreams && !cpcm->stream[1].substreams)
			continue; /* no substreams assigned */

		dev = get_empty_pcm_device(bus, cpcm->pcm_type);
		if (dev < 0)
			continue; /* no fatal error */
		cpcm->device = dev;
		err =  snd_hda_attach_pcm_stream(bus, codec, cpcm);
		if (err < 0) {
			codec_err(codec,
				  "cannot attach PCM stream %d for codec #%d\n",
				  dev, codec->core.addr);
			continue; /* no fatal error */
		}
	}

	return 0;
}

/**
 * snd_hda_add_new_ctls - create controls from the array
 * @codec: the HDA codec
 * @knew: the array of struct snd_kcontrol_new
 *
 * This helper function creates and add new controls in the given array.
 * The array must be terminated with an empty entry as terminator.
 *
 * Returns 0 if successful, or a negative error code.
 */
int snd_hda_add_new_ctls(struct hda_codec *codec, struct snd_kcontrol_new *knew)
{
 	int err;

	for (; knew->name; knew++) {
		struct snd_kcontrol *kctl;
		kctl = snd_ctl_new1(knew, codec);
		if (!kctl)
			return -ENOMEM;
		err = snd_ctl_add(codec->bus->card, kctl);
		if (err < 0) {
			if (!codec->addr)
				return err;
			kctl = snd_ctl_new1(knew, codec);
			if (!kctl)
				return -ENOMEM;
			kctl->id.device = codec->addr;
			err = snd_ctl_add(codec->bus->card, kctl);
			if (err < 0)
int snd_hda_add_new_ctls(struct hda_codec *codec,
			 const struct snd_kcontrol_new *knew)
{
	int err;

	for (; knew->name; knew++) {
		struct snd_kcontrol *kctl;
		int addr = 0, idx = 0;
		if (knew->iface == -1)	/* skip this codec private value */
			continue;
		for (;;) {
			kctl = snd_ctl_new1(knew, codec);
			if (!kctl)
				return -ENOMEM;
			if (addr > 0)
				kctl->id.device = addr;
			if (idx > 0)
				kctl->id.index = idx;
			err = snd_hda_ctl_add(codec, 0, kctl);
			if (!err)
				break;
			/* try first with another device index corresponding to
			 * the codec addr; if it still fails (or it's the
			 * primary codec), then try another control index
			 */
			if (!addr && codec->core.addr)
				addr = codec->core.addr;
			else if (!idx && !knew->index) {
				idx = find_empty_mixer_ctl_idx(codec,
							       knew->name, 0);
				if (idx <= 0)
					return err;
			} else
				return err;
		}
	}
	return 0;
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
static void hda_set_power_state(struct hda_codec *codec, hda_nid_t fg,
				unsigned int power_state);

static void hda_power_work(struct work_struct *work)
{
	struct hda_codec *codec =
		container_of(work, struct hda_codec, power_work.work);

	if (!codec->power_on || codec->power_count) {
		codec->power_transition = 0;
		return;
	}

	hda_call_codec_suspend(codec);
	if (codec->bus->ops.pm_notify)
		codec->bus->ops.pm_notify(codec);
}

static void hda_keep_power_on(struct hda_codec *codec)
{
	codec->power_count++;
	codec->power_on = 1;
}

void snd_hda_power_up(struct hda_codec *codec)
{
	codec->power_count++;
	if (codec->power_on || codec->power_transition)
		return;

	codec->power_on = 1;
	if (codec->bus->ops.pm_notify)
		codec->bus->ops.pm_notify(codec);
	hda_call_codec_resume(codec);
	cancel_delayed_work(&codec->power_work);
	codec->power_transition = 0;
}

void snd_hda_power_down(struct hda_codec *codec)
{
	--codec->power_count;
	if (!codec->power_on || codec->power_count || codec->power_transition)
		return;
	if (power_save) {
		codec->power_transition = 1; /* avoid reentrance */
		schedule_delayed_work(&codec->power_work,
				      msecs_to_jiffies(power_save * 1000));
	}
}

EXPORT_SYMBOL_GPL(snd_hda_add_new_ctls);

#ifdef CONFIG_PM
static void codec_set_power_save(struct hda_codec *codec, int delay)
{
	struct device *dev = hda_codec_dev(codec);

	if (delay == 0 && codec->auto_runtime_pm)
		delay = 3000;

	if (delay > 0) {
		pm_runtime_set_autosuspend_delay(dev, delay);
		pm_runtime_use_autosuspend(dev);
		pm_runtime_allow(dev);
		if (!pm_runtime_suspended(dev))
			pm_runtime_mark_last_busy(dev);
	} else {
		pm_runtime_dont_use_autosuspend(dev);
		pm_runtime_forbid(dev);
	}
}

/**
 * snd_hda_set_power_save - reprogram autosuspend for the given delay
 * @bus: HD-audio bus
 * @delay: autosuspend delay in msec, 0 = off
 *
 * Synchronize the runtime PM autosuspend state from the power_save option.
 */
void snd_hda_set_power_save(struct hda_bus *bus, int delay)
{
	struct hda_codec *c;

	list_for_each_codec(c, bus)
		codec_set_power_save(c, delay);
}
EXPORT_SYMBOL_GPL(snd_hda_set_power_save);

/**
 * snd_hda_check_amp_list_power - Check the amp list and update the power
 * @codec: HD-audio codec
 * @check: the object containing an AMP list and the status
 * @nid: NID to check / update
 *
 * Check whether the given NID is in the amp list.  If it's in the list,
 * check the current AMP status, and update the power-status according
 * to the mute status.
 *
 * This function is supposed to be set or called from the check_power_status
 * patch ops.
 */
int snd_hda_check_amp_list_power(struct hda_codec *codec,
				 struct hda_loopback_check *check,
				 hda_nid_t nid)
{
	struct hda_amp_list *p;
	const struct hda_amp_list *p;
	int ch, v;

	if (!check->amplist)
		return 0;
	for (p = check->amplist; p->nid; p++) {
		if (p->nid == nid)
			break;
	}
	if (!p->nid)
		return 0; /* nothing changed */

	for (p = check->amplist; p->nid; p++) {
		for (ch = 0; ch < 2; ch++) {
			v = snd_hda_codec_amp_read(codec, p->nid, ch, p->dir,
						   p->idx);
			if (!(v & HDA_AMP_MUTE) && v > 0) {
				if (!check->power_on) {
					check->power_on = 1;
					snd_hda_power_up(codec);
					snd_hda_power_up_pm(codec);
				}
				return 1;
			}
		}
	}
	if (check->power_on) {
		check->power_on = 0;
		snd_hda_power_down(codec);
	}
	return 0;
}
#endif

/*
 * Channel mode helper
 */
int snd_hda_ch_mode_info(struct hda_codec *codec,
			 struct snd_ctl_elem_info *uinfo,
			 const struct hda_channel_mode *chmode,
			 int num_chmodes)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = num_chmodes;
	if (uinfo->value.enumerated.item >= num_chmodes)
		uinfo->value.enumerated.item = num_chmodes - 1;
	sprintf(uinfo->value.enumerated.name, "%dch",
		chmode[uinfo->value.enumerated.item].channels);
	return 0;
}

int snd_hda_ch_mode_get(struct hda_codec *codec,
			struct snd_ctl_elem_value *ucontrol,
			const struct hda_channel_mode *chmode,
			int num_chmodes,
			int max_channels)
{
	int i;

	for (i = 0; i < num_chmodes; i++) {
		if (max_channels == chmode[i].channels) {
			ucontrol->value.enumerated.item[0] = i;
			break;
		}
	}
	return 0;
}

int snd_hda_ch_mode_put(struct hda_codec *codec,
			struct snd_ctl_elem_value *ucontrol,
			const struct hda_channel_mode *chmode,
			int num_chmodes,
			int *max_channelsp)
{
	unsigned int mode;

	mode = ucontrol->value.enumerated.item[0];
	if (mode >= num_chmodes)
		return -EINVAL;
	if (*max_channelsp == chmode[mode].channels)
		return 0;
	/* change the current channel setting */
	*max_channelsp = chmode[mode].channels;
	if (chmode[mode].sequence)
		snd_hda_sequence_write_cache(codec, chmode[mode].sequence);
	return 1;
}

/*
 * input MUX helper
 */
		snd_hda_power_down_pm(codec);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_check_amp_list_power);
#endif

/*
 * input MUX helper
 */

/**
 * snd_hda_input_mux_info_info - Info callback helper for the input-mux enum
 * @imux: imux helper object
 * @uinfo: pointer to get/store the data
 */
int snd_hda_input_mux_info(const struct hda_input_mux *imux,
			   struct snd_ctl_elem_info *uinfo)
{
	unsigned int index;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = imux->num_items;
	if (!imux->num_items)
		return 0;
	index = uinfo->value.enumerated.item;
	if (index >= imux->num_items)
		index = imux->num_items - 1;
	strcpy(uinfo->value.enumerated.name, imux->items[index].label);
	return 0;
}

EXPORT_SYMBOL_GPL(snd_hda_input_mux_info);

/**
 * snd_hda_input_mux_info_put - Put callback helper for the input-mux enum
 * @codec: the HDA codec
 * @imux: imux helper object
 * @ucontrol: pointer to get/store the data
 * @nid: input mux NID
 * @cur_val: pointer to get/store the current imux value
 */
int snd_hda_input_mux_put(struct hda_codec *codec,
			  const struct hda_input_mux *imux,
			  struct snd_ctl_elem_value *ucontrol,
			  hda_nid_t nid,
			  unsigned int *cur_val)
{
	unsigned int idx;

	if (!imux->num_items)
		return 0;
	idx = ucontrol->value.enumerated.item[0];
	if (idx >= imux->num_items)
		idx = imux->num_items - 1;
	if (*cur_val == idx)
		return 0;
	snd_hda_codec_write_cache(codec, nid, 0, AC_VERB_SET_CONNECT_SEL,
				  imux->items[idx].index);
	*cur_val = idx;
	return 1;
}


EXPORT_SYMBOL_GPL(snd_hda_input_mux_put);


/**
 * snd_hda_enum_helper_info - Helper for simple enum ctls
 * @kcontrol: ctl element
 * @uinfo: pointer to get/store the data
 * @num_items: number of enum items
 * @texts: enum item string array
 *
 * process kcontrol info callback of a simple string enum array
 * when @num_items is 0 or @texts is NULL, assume a boolean enum array
 */
int snd_hda_enum_helper_info(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_info *uinfo,
			     int num_items, const char * const *texts)
{
	static const char * const texts_default[] = {
		"Disabled", "Enabled"
	};

	if (!texts || !num_items) {
		num_items = 2;
		texts = texts_default;
	}

	return snd_ctl_enum_info(uinfo, 1, num_items, texts);
}
EXPORT_SYMBOL_GPL(snd_hda_enum_helper_info);

/*
 * Multi-channel / digital-out PCM helper functions
 */

/* setup SPDIF output stream */
static void setup_dig_out_stream(struct hda_codec *codec, hda_nid_t nid,
				 unsigned int stream_tag, unsigned int format)
{
	/* turn off SPDIF once; otherwise the IEC958 bits won't be updated */
	if (codec->spdif_ctls & AC_DIG1_ENABLE)
		snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_DIGI_CONVERT_1,
				    codec->spdif_ctls & ~AC_DIG1_ENABLE & 0xff);
	snd_hda_codec_setup_stream(codec, nid, stream_tag, 0, format);
	/* turn on again (if needed) */
	if (codec->spdif_ctls & AC_DIG1_ENABLE)
		snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_DIGI_CONVERT_1,
				    codec->spdif_ctls & 0xff);
}

/*
 * open the digital out in the exclusive mode
	struct hda_spdif_out *spdif;
	unsigned int curr_fmt;
	bool reset;

	spdif = snd_hda_spdif_out_of_nid(codec, nid);
	curr_fmt = snd_hda_codec_read(codec, nid, 0,
				      AC_VERB_GET_STREAM_FORMAT, 0);
	reset = codec->spdif_status_reset &&
		(spdif->ctls & AC_DIG1_ENABLE) &&
		curr_fmt != format;

	/* turn off SPDIF if needed; otherwise the IEC958 bits won't be
	   updated */
	if (reset)
		set_dig_out_convert(codec, nid,
				    spdif->ctls & ~AC_DIG1_ENABLE & 0xff,
				    -1);
	snd_hda_codec_setup_stream(codec, nid, stream_tag, 0, format);
	if (codec->slave_dig_outs) {
		const hda_nid_t *d;
		for (d = codec->slave_dig_outs; *d; d++)
			snd_hda_codec_setup_stream(codec, *d, stream_tag, 0,
						   format);
	}
	/* turn on again (if needed) */
	if (reset)
		set_dig_out_convert(codec, nid,
				    spdif->ctls & 0xff, -1);
}

static void cleanup_dig_out_stream(struct hda_codec *codec, hda_nid_t nid)
{
	snd_hda_codec_cleanup_stream(codec, nid);
	if (codec->slave_dig_outs) {
		const hda_nid_t *d;
		for (d = codec->slave_dig_outs; *d; d++)
			snd_hda_codec_cleanup_stream(codec, *d);
	}
}

/**
 * snd_hda_multi_out_dig_open - open the digital out in the exclusive mode
 * @codec: the HDA codec
 * @mout: hda_multi_out object
 */
int snd_hda_multi_out_dig_open(struct hda_codec *codec,
			       struct hda_multi_out *mout)
{
	mutex_lock(&codec->spdif_mutex);
	if (mout->dig_out_used == HDA_DIG_ANALOG_DUP)
		/* already opened as analog dup; reset it once */
		snd_hda_codec_cleanup_stream(codec, mout->dig_out_nid);
		cleanup_dig_out_stream(codec, mout->dig_out_nid);
	mout->dig_out_used = HDA_DIG_EXCLUSIVE;
	mutex_unlock(&codec->spdif_mutex);
	return 0;
}

EXPORT_SYMBOL_GPL(snd_hda_multi_out_dig_open);

/**
 * snd_hda_multi_out_dig_prepare - prepare the digital out stream
 * @codec: the HDA codec
 * @mout: hda_multi_out object
 * @stream_tag: stream tag to assign
 * @format: format id to assign
 * @substream: PCM substream to assign
 */
int snd_hda_multi_out_dig_prepare(struct hda_codec *codec,
				  struct hda_multi_out *mout,
				  unsigned int stream_tag,
				  unsigned int format,
				  struct snd_pcm_substream *substream)
{
	mutex_lock(&codec->spdif_mutex);
	setup_dig_out_stream(codec, mout->dig_out_nid, stream_tag, format);
	mutex_unlock(&codec->spdif_mutex);
	return 0;
}

/*
 * release the digital out
EXPORT_SYMBOL_GPL(snd_hda_multi_out_dig_prepare);

/**
 * snd_hda_multi_out_dig_cleanup - clean-up the digital out stream
 * @codec: the HDA codec
 * @mout: hda_multi_out object
 */
int snd_hda_multi_out_dig_cleanup(struct hda_codec *codec,
				  struct hda_multi_out *mout)
{
	mutex_lock(&codec->spdif_mutex);
	cleanup_dig_out_stream(codec, mout->dig_out_nid);
	mutex_unlock(&codec->spdif_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_multi_out_dig_cleanup);

/**
 * snd_hda_multi_out_dig_close - release the digital out stream
 * @codec: the HDA codec
 * @mout: hda_multi_out object
 */
int snd_hda_multi_out_dig_close(struct hda_codec *codec,
				struct hda_multi_out *mout)
{
	mutex_lock(&codec->spdif_mutex);
	mout->dig_out_used = 0;
	mutex_unlock(&codec->spdif_mutex);
	return 0;
}

/*
 * set up more restrictions for analog out
EXPORT_SYMBOL_GPL(snd_hda_multi_out_dig_close);

/**
 * snd_hda_multi_out_analog_open - open analog outputs
 * @codec: the HDA codec
 * @mout: hda_multi_out object
 * @substream: PCM substream to assign
 * @hinfo: PCM information to assign
 *
 * Open analog outputs and set up the hw-constraints.
 * If the digital outputs can be opened as slave, open the digital
 * outputs, too.
 */
int snd_hda_multi_out_analog_open(struct hda_codec *codec,
				  struct hda_multi_out *mout,
				  struct snd_pcm_substream *substream,
				  struct hda_pcm_stream *hinfo)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	runtime->hw.channels_max = mout->max_channels;
	if (mout->dig_out_nid) {
		if (!mout->analog_rates) {
			mout->analog_rates = hinfo->rates;
			mout->analog_formats = hinfo->formats;
			mout->analog_maxbps = hinfo->maxbps;
		} else {
			runtime->hw.rates = mout->analog_rates;
			runtime->hw.formats = mout->analog_formats;
			hinfo->maxbps = mout->analog_maxbps;
		}
		if (!mout->spdif_rates) {
			snd_hda_query_supported_pcm(codec, mout->dig_out_nid,
						    &mout->spdif_rates,
						    &mout->spdif_formats,
						    &mout->spdif_maxbps);
		}
		mutex_lock(&codec->spdif_mutex);
		if (mout->share_spdif) {
			runtime->hw.rates &= mout->spdif_rates;
			runtime->hw.formats &= mout->spdif_formats;
			if (mout->spdif_maxbps < hinfo->maxbps)
				hinfo->maxbps = mout->spdif_maxbps;
			if ((runtime->hw.rates & mout->spdif_rates) &&
			    (runtime->hw.formats & mout->spdif_formats)) {
				runtime->hw.rates &= mout->spdif_rates;
				runtime->hw.formats &= mout->spdif_formats;
				if (mout->spdif_maxbps < hinfo->maxbps)
					hinfo->maxbps = mout->spdif_maxbps;
			} else {
				mout->share_spdif = 0;
				/* FIXME: need notify? */
			}
		}
		mutex_unlock(&codec->spdif_mutex);
	}
	return snd_pcm_hw_constraint_step(substream->runtime, 0,
					  SNDRV_PCM_HW_PARAM_CHANNELS, 2);
}

/*
 * set up the i/o for analog out
 * when the digital out is available, copy the front out to digital out, too.
EXPORT_SYMBOL_GPL(snd_hda_multi_out_analog_open);

/**
 * snd_hda_multi_out_analog_prepare - Preapre the analog outputs.
 * @codec: the HDA codec
 * @mout: hda_multi_out object
 * @stream_tag: stream tag to assign
 * @format: format id to assign
 * @substream: PCM substream to assign
 *
 * Set up the i/o for analog out.
 * When the digital out is available, copy the front out to digital out, too.
 */
int snd_hda_multi_out_analog_prepare(struct hda_codec *codec,
				     struct hda_multi_out *mout,
				     unsigned int stream_tag,
				     unsigned int format,
				     struct snd_pcm_substream *substream)
{
	hda_nid_t *nids = mout->dac_nids;
	int chs = substream->runtime->channels;
	int i;

	mutex_lock(&codec->spdif_mutex);
	const hda_nid_t *nids = mout->dac_nids;
	int chs = substream->runtime->channels;
	struct hda_spdif_out *spdif;
	int i;

	mutex_lock(&codec->spdif_mutex);
	spdif = snd_hda_spdif_out_of_nid(codec, mout->dig_out_nid);
	if (mout->dig_out_nid && mout->share_spdif &&
	    mout->dig_out_used != HDA_DIG_EXCLUSIVE) {
		if (chs == 2 &&
		    snd_hda_is_supported_format(codec, mout->dig_out_nid,
						format) &&
		    !(codec->spdif_status & IEC958_AES0_NONAUDIO)) {
		    !(spdif->status & IEC958_AES0_NONAUDIO)) {
			mout->dig_out_used = HDA_DIG_ANALOG_DUP;
			setup_dig_out_stream(codec, mout->dig_out_nid,
					     stream_tag, format);
		} else {
			mout->dig_out_used = 0;
			snd_hda_codec_cleanup_stream(codec, mout->dig_out_nid);
			cleanup_dig_out_stream(codec, mout->dig_out_nid);
		}
	}
	mutex_unlock(&codec->spdif_mutex);

	/* front */
	snd_hda_codec_setup_stream(codec, nids[HDA_FRONT], stream_tag,
				   0, format);
	if (!mout->no_share_stream &&
	    mout->hp_nid && mout->hp_nid != nids[HDA_FRONT])
		/* headphone out will just decode front left/right (stereo) */
		snd_hda_codec_setup_stream(codec, mout->hp_nid, stream_tag,
					   0, format);
	/* extra outputs copied from front */
	for (i = 0; i < ARRAY_SIZE(mout->extra_out_nid); i++)
		if (!mout->no_share_stream && mout->extra_out_nid[i])
			snd_hda_codec_setup_stream(codec,
						   mout->extra_out_nid[i],
	for (i = 0; i < ARRAY_SIZE(mout->hp_out_nid); i++)
		if (!mout->no_share_stream && mout->hp_out_nid[i])
			snd_hda_codec_setup_stream(codec,
						   mout->hp_out_nid[i],
						   stream_tag, 0, format);

	/* surrounds */
	for (i = 1; i < mout->num_dacs; i++) {
		if (chs >= (i + 1) * 2) /* independent out */
			snd_hda_codec_setup_stream(codec, nids[i], stream_tag,
						   i * 2, format);
		else if (!mout->no_share_stream) /* copy front */
			snd_hda_codec_setup_stream(codec, nids[i], stream_tag,
						   0, format);
	}
	return 0;
}

/*
 * clean up the setting for analog out

	/* extra surrounds */
	for (i = 0; i < ARRAY_SIZE(mout->extra_out_nid); i++) {
		int ch = 0;
		if (!mout->extra_out_nid[i])
			break;
		if (chs >= (i + 1) * 2)
			ch = i * 2;
		else if (!mout->no_share_stream)
			break;
		snd_hda_codec_setup_stream(codec, mout->extra_out_nid[i],
					   stream_tag, ch, format);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_multi_out_analog_prepare);

/**
 * snd_hda_multi_out_analog_cleanup - clean up the setting for analog out
 * @codec: the HDA codec
 * @mout: hda_multi_out object
 */
int snd_hda_multi_out_analog_cleanup(struct hda_codec *codec,
				     struct hda_multi_out *mout)
{
	hda_nid_t *nids = mout->dac_nids;
	const hda_nid_t *nids = mout->dac_nids;
	int i;

	for (i = 0; i < mout->num_dacs; i++)
		snd_hda_codec_cleanup_stream(codec, nids[i]);
	if (mout->hp_nid)
		snd_hda_codec_cleanup_stream(codec, mout->hp_nid);
	for (i = 0; i < ARRAY_SIZE(mout->hp_out_nid); i++)
		if (mout->hp_out_nid[i])
			snd_hda_codec_cleanup_stream(codec,
						     mout->hp_out_nid[i]);
	for (i = 0; i < ARRAY_SIZE(mout->extra_out_nid); i++)
		if (mout->extra_out_nid[i])
			snd_hda_codec_cleanup_stream(codec,
						     mout->extra_out_nid[i]);
	mutex_lock(&codec->spdif_mutex);
	if (mout->dig_out_nid && mout->dig_out_used == HDA_DIG_ANALOG_DUP) {
		snd_hda_codec_cleanup_stream(codec, mout->dig_out_nid);
		cleanup_dig_out_stream(codec, mout->dig_out_nid);
		mout->dig_out_used = 0;
	}
	mutex_unlock(&codec->spdif_mutex);
	return 0;
}

/*
 * Helper for automatic ping configuration
 */

static int is_in_nid_list(hda_nid_t nid, hda_nid_t *list)
{
	for (; *list; list++)
		if (*list == nid)
			return 1;
	return 0;
}


/*
 * Sort an associated group of pins according to their sequence numbers.
 */
static void sort_pins_by_sequence(hda_nid_t * pins, short * sequences,
				  int num_pins)
{
	int i, j;
	short seq;
	hda_nid_t nid;
	
	for (i = 0; i < num_pins; i++) {
		for (j = i + 1; j < num_pins; j++) {
			if (sequences[i] > sequences[j]) {
				seq = sequences[i];
				sequences[i] = sequences[j];
				sequences[j] = seq;
				nid = pins[i];
				pins[i] = pins[j];
				pins[j] = nid;
			}
		}
	}
}


/*
 * Parse all pin widgets and store the useful pin nids to cfg
 *
 * The number of line-outs or any primary output is stored in line_outs,
 * and the corresponding output pins are assigned to line_out_pins[],
 * in the order of front, rear, CLFE, side, ...
 *
 * If more extra outputs (speaker and headphone) are found, the pins are
 * assisnged to hp_pins[] and speaker_pins[], respectively.  If no line-out jack
 * is detected, one of speaker of HP pins is assigned as the primary
 * output, i.e. to line_out_pins[0].  So, line_outs is always positive
 * if any analog output exists.
 * 
 * The analog input pins are assigned to input_pins array.
 * The digital input/output pins are assigned to dig_in_pin and dig_out_pin,
 * respectively.
 */
int snd_hda_parse_pin_def_config(struct hda_codec *codec,
				 struct auto_pin_cfg *cfg,
				 hda_nid_t *ignore_nids)
{
	hda_nid_t nid, end_nid;
	short seq, assoc_line_out, assoc_speaker;
	short sequences_line_out[ARRAY_SIZE(cfg->line_out_pins)];
	short sequences_speaker[ARRAY_SIZE(cfg->speaker_pins)];
	short sequences_hp[ARRAY_SIZE(cfg->hp_pins)];

	memset(cfg, 0, sizeof(*cfg));

	memset(sequences_line_out, 0, sizeof(sequences_line_out));
	memset(sequences_speaker, 0, sizeof(sequences_speaker));
	memset(sequences_hp, 0, sizeof(sequences_hp));
	assoc_line_out = assoc_speaker = 0;

	end_nid = codec->start_nid + codec->num_nodes;
	for (nid = codec->start_nid; nid < end_nid; nid++) {
		unsigned int wid_caps = get_wcaps(codec, nid);
		unsigned int wid_type =
			(wid_caps & AC_WCAP_TYPE) >> AC_WCAP_TYPE_SHIFT;
		unsigned int def_conf;
		short assoc, loc;

		/* read all default configuration for pin complex */
		if (wid_type != AC_WID_PIN)
			continue;
		/* ignore the given nids (e.g. pc-beep returns error) */
		if (ignore_nids && is_in_nid_list(nid, ignore_nids))
			continue;

		def_conf = snd_hda_codec_read(codec, nid, 0,
					      AC_VERB_GET_CONFIG_DEFAULT, 0);
		if (get_defcfg_connect(def_conf) == AC_JACK_PORT_NONE)
			continue;
		loc = get_defcfg_location(def_conf);
		switch (get_defcfg_device(def_conf)) {
		case AC_JACK_LINE_OUT:
			seq = get_defcfg_sequence(def_conf);
			assoc = get_defcfg_association(def_conf);

			if (!(wid_caps & AC_WCAP_STEREO))
				if (!cfg->mono_out_pin)
					cfg->mono_out_pin = nid;
			if (!assoc)
				continue;
			if (!assoc_line_out)
				assoc_line_out = assoc;
			else if (assoc_line_out != assoc)
				continue;
			if (cfg->line_outs >= ARRAY_SIZE(cfg->line_out_pins))
				continue;
			cfg->line_out_pins[cfg->line_outs] = nid;
			sequences_line_out[cfg->line_outs] = seq;
			cfg->line_outs++;
			break;
		case AC_JACK_SPEAKER:
			seq = get_defcfg_sequence(def_conf);
			assoc = get_defcfg_association(def_conf);
			if (! assoc)
				continue;
			if (! assoc_speaker)
				assoc_speaker = assoc;
			else if (assoc_speaker != assoc)
				continue;
			if (cfg->speaker_outs >= ARRAY_SIZE(cfg->speaker_pins))
				continue;
			cfg->speaker_pins[cfg->speaker_outs] = nid;
			sequences_speaker[cfg->speaker_outs] = seq;
			cfg->speaker_outs++;
			break;
		case AC_JACK_HP_OUT:
			seq = get_defcfg_sequence(def_conf);
			assoc = get_defcfg_association(def_conf);
			if (cfg->hp_outs >= ARRAY_SIZE(cfg->hp_pins))
				continue;
			cfg->hp_pins[cfg->hp_outs] = nid;
			sequences_hp[cfg->hp_outs] = (assoc << 4) | seq;
			cfg->hp_outs++;
			break;
		case AC_JACK_MIC_IN: {
			int preferred, alt;
			if (loc == AC_JACK_LOC_FRONT) {
				preferred = AUTO_PIN_FRONT_MIC;
				alt = AUTO_PIN_MIC;
			} else {
				preferred = AUTO_PIN_MIC;
				alt = AUTO_PIN_FRONT_MIC;
			}
			if (!cfg->input_pins[preferred])
				cfg->input_pins[preferred] = nid;
			else if (!cfg->input_pins[alt])
				cfg->input_pins[alt] = nid;
			break;
		}
		case AC_JACK_LINE_IN:
			if (loc == AC_JACK_LOC_FRONT)
				cfg->input_pins[AUTO_PIN_FRONT_LINE] = nid;
			else
				cfg->input_pins[AUTO_PIN_LINE] = nid;
			break;
		case AC_JACK_CD:
			cfg->input_pins[AUTO_PIN_CD] = nid;
			break;
		case AC_JACK_AUX:
			cfg->input_pins[AUTO_PIN_AUX] = nid;
			break;
		case AC_JACK_SPDIF_OUT:
			cfg->dig_out_pin = nid;
			break;
		case AC_JACK_SPDIF_IN:
			cfg->dig_in_pin = nid;
			break;
		}
	}

	/* FIX-UP:
	 * If no line-out is defined but multiple HPs are found,
	 * some of them might be the real line-outs.
	 */
	if (!cfg->line_outs && cfg->hp_outs > 1) {
		int i = 0;
		while (i < cfg->hp_outs) {
			/* The real HPs should have the sequence 0x0f */
			if ((sequences_hp[i] & 0x0f) == 0x0f) {
				i++;
				continue;
			}
			/* Move it to the line-out table */
			cfg->line_out_pins[cfg->line_outs] = cfg->hp_pins[i];
			sequences_line_out[cfg->line_outs] = sequences_hp[i];
			cfg->line_outs++;
			cfg->hp_outs--;
			memmove(cfg->hp_pins + i, cfg->hp_pins + i + 1,
				sizeof(cfg->hp_pins[0]) * (cfg->hp_outs - i));
			memmove(sequences_hp + i - 1, sequences_hp + i,
				sizeof(sequences_hp[0]) * (cfg->hp_outs - i));
		}
	}

	/* sort by sequence */
	sort_pins_by_sequence(cfg->line_out_pins, sequences_line_out,
			      cfg->line_outs);
	sort_pins_by_sequence(cfg->speaker_pins, sequences_speaker,
			      cfg->speaker_outs);
	sort_pins_by_sequence(cfg->hp_pins, sequences_hp,
			      cfg->hp_outs);
	
	/* if we have only one mic, make it AUTO_PIN_MIC */
	if (!cfg->input_pins[AUTO_PIN_MIC] &&
	    cfg->input_pins[AUTO_PIN_FRONT_MIC]) {
		cfg->input_pins[AUTO_PIN_MIC] =
			cfg->input_pins[AUTO_PIN_FRONT_MIC];
		cfg->input_pins[AUTO_PIN_FRONT_MIC] = 0;
	}
	/* ditto for line-in */
	if (!cfg->input_pins[AUTO_PIN_LINE] &&
	    cfg->input_pins[AUTO_PIN_FRONT_LINE]) {
		cfg->input_pins[AUTO_PIN_LINE] =
			cfg->input_pins[AUTO_PIN_FRONT_LINE];
		cfg->input_pins[AUTO_PIN_FRONT_LINE] = 0;
	}

	/*
	 * FIX-UP: if no line-outs are detected, try to use speaker or HP pin
	 * as a primary output
	 */
	if (!cfg->line_outs) {
		if (cfg->speaker_outs) {
			cfg->line_outs = cfg->speaker_outs;
			memcpy(cfg->line_out_pins, cfg->speaker_pins,
			       sizeof(cfg->speaker_pins));
			cfg->speaker_outs = 0;
			memset(cfg->speaker_pins, 0, sizeof(cfg->speaker_pins));
			cfg->line_out_type = AUTO_PIN_SPEAKER_OUT;
		} else if (cfg->hp_outs) {
			cfg->line_outs = cfg->hp_outs;
			memcpy(cfg->line_out_pins, cfg->hp_pins,
			       sizeof(cfg->hp_pins));
			cfg->hp_outs = 0;
			memset(cfg->hp_pins, 0, sizeof(cfg->hp_pins));
			cfg->line_out_type = AUTO_PIN_HP_OUT;
		}
	}

	/* Reorder the surround channels
	 * ALSA sequence is front/surr/clfe/side
	 * HDA sequence is:
	 *    4-ch: front/surr  =>  OK as it is
	 *    6-ch: front/clfe/surr
	 *    8-ch: front/clfe/rear/side|fc
	 */
	switch (cfg->line_outs) {
	case 3:
	case 4:
		nid = cfg->line_out_pins[1];
		cfg->line_out_pins[1] = cfg->line_out_pins[2];
		cfg->line_out_pins[2] = nid;
		break;
	}

	/*
	 * debug prints of the parsed results
	 */
	snd_printd("autoconfig: line_outs=%d (0x%x/0x%x/0x%x/0x%x/0x%x)\n",
		   cfg->line_outs, cfg->line_out_pins[0], cfg->line_out_pins[1],
		   cfg->line_out_pins[2], cfg->line_out_pins[3],
		   cfg->line_out_pins[4]);
	snd_printd("   speaker_outs=%d (0x%x/0x%x/0x%x/0x%x/0x%x)\n",
		   cfg->speaker_outs, cfg->speaker_pins[0],
		   cfg->speaker_pins[1], cfg->speaker_pins[2],
		   cfg->speaker_pins[3], cfg->speaker_pins[4]);
	snd_printd("   hp_outs=%d (0x%x/0x%x/0x%x/0x%x/0x%x)\n",
		   cfg->hp_outs, cfg->hp_pins[0],
		   cfg->hp_pins[1], cfg->hp_pins[2],
		   cfg->hp_pins[3], cfg->hp_pins[4]);
	snd_printd("   mono: mono_out=0x%x\n", cfg->mono_out_pin);
	snd_printd("   inputs: mic=0x%x, fmic=0x%x, line=0x%x, fline=0x%x,"
		   " cd=0x%x, aux=0x%x\n",
		   cfg->input_pins[AUTO_PIN_MIC],
		   cfg->input_pins[AUTO_PIN_FRONT_MIC],
		   cfg->input_pins[AUTO_PIN_LINE],
		   cfg->input_pins[AUTO_PIN_FRONT_LINE],
		   cfg->input_pins[AUTO_PIN_CD],
		   cfg->input_pins[AUTO_PIN_AUX]);

	return 0;
}

/* labels for input pins */
const char *auto_pin_cfg_labels[AUTO_PIN_LAST] = {
	"Mic", "Front Mic", "Line", "Front Line", "CD", "Aux"
};


#ifdef CONFIG_PM
/*
 * power management
 */

/**
 * snd_hda_suspend - suspend the codecs
 * @bus: the HDA bus
 * @state: suspsend state
 *
 * Returns 0 if successful.
 */
int snd_hda_suspend(struct hda_bus *bus, pm_message_t state)
{
	struct hda_codec *codec;

	list_for_each_entry(codec, &bus->codec_list, list) {
#ifdef CONFIG_SND_HDA_POWER_SAVE
		if (!codec->power_on)
			continue;
#endif
		hda_call_codec_suspend(codec);
	}
	return 0;
}

/**
 * snd_hda_resume - resume the codecs
 * @bus: the HDA bus
 * @state: resume state
 *
 * Returns 0 if successful.
 *
 * This fucntion is defined only when POWER_SAVE isn't set.
 * In the power-save mode, the codec is resumed dynamically.
 */
int snd_hda_resume(struct hda_bus *bus)
{
	struct hda_codec *codec;

	list_for_each_entry(codec, &bus->codec_list, list) {
		if (snd_hda_codec_needs_resume(codec))
			hda_call_codec_resume(codec);
	}
	return 0;
}
#ifdef CONFIG_SND_HDA_POWER_SAVE
int snd_hda_codecs_inuse(struct hda_bus *bus)
{
	struct hda_codec *codec;

	list_for_each_entry(codec, &bus->codec_list, list) {
		if (snd_hda_codec_needs_resume(codec))
			return 1;
	}
	return 0;
}
#endif
#endif
EXPORT_SYMBOL_GPL(snd_hda_multi_out_analog_cleanup);

/**
 * snd_hda_get_default_vref - Get the default (mic) VREF pin bits
 * @codec: the HDA codec
 * @pin: referred pin NID
 *
 * Guess the suitable VREF pin bits to be set as the pin-control value.
 * Note: the function doesn't set the AC_PINCTL_IN_EN bit.
 */
unsigned int snd_hda_get_default_vref(struct hda_codec *codec, hda_nid_t pin)
{
	unsigned int pincap;
	unsigned int oldval;
	oldval = snd_hda_codec_read(codec, pin, 0,
				    AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
	pincap = snd_hda_query_pin_caps(codec, pin);
	pincap = (pincap & AC_PINCAP_VREF) >> AC_PINCAP_VREF_SHIFT;
	/* Exception: if the default pin setup is vref50, we give it priority */
	if ((pincap & AC_PINCAP_VREF_80) && oldval != PIN_VREF50)
		return AC_PINCTL_VREF_80;
	else if (pincap & AC_PINCAP_VREF_50)
		return AC_PINCTL_VREF_50;
	else if (pincap & AC_PINCAP_VREF_100)
		return AC_PINCTL_VREF_100;
	else if (pincap & AC_PINCAP_VREF_GRD)
		return AC_PINCTL_VREF_GRD;
	return AC_PINCTL_VREF_HIZ;
}
EXPORT_SYMBOL_GPL(snd_hda_get_default_vref);

/**
 * snd_hda_correct_pin_ctl - correct the pin ctl value for matching with the pin cap
 * @codec: the HDA codec
 * @pin: referred pin NID
 * @val: pin ctl value to audit
 */
unsigned int snd_hda_correct_pin_ctl(struct hda_codec *codec,
				     hda_nid_t pin, unsigned int val)
{
	static unsigned int cap_lists[][2] = {
		{ AC_PINCTL_VREF_100, AC_PINCAP_VREF_100 },
		{ AC_PINCTL_VREF_80, AC_PINCAP_VREF_80 },
		{ AC_PINCTL_VREF_50, AC_PINCAP_VREF_50 },
		{ AC_PINCTL_VREF_GRD, AC_PINCAP_VREF_GRD },
	};
	unsigned int cap;

	if (!val)
		return 0;
	cap = snd_hda_query_pin_caps(codec, pin);
	if (!cap)
		return val; /* don't know what to do... */

	if (val & AC_PINCTL_OUT_EN) {
		if (!(cap & AC_PINCAP_OUT))
			val &= ~(AC_PINCTL_OUT_EN | AC_PINCTL_HP_EN);
		else if ((val & AC_PINCTL_HP_EN) && !(cap & AC_PINCAP_HP_DRV))
			val &= ~AC_PINCTL_HP_EN;
	}

	if (val & AC_PINCTL_IN_EN) {
		if (!(cap & AC_PINCAP_IN))
			val &= ~(AC_PINCTL_IN_EN | AC_PINCTL_VREFEN);
		else {
			unsigned int vcap, vref;
			int i;
			vcap = (cap & AC_PINCAP_VREF) >> AC_PINCAP_VREF_SHIFT;
			vref = val & AC_PINCTL_VREFEN;
			for (i = 0; i < ARRAY_SIZE(cap_lists); i++) {
				if (vref == cap_lists[i][0] &&
				    !(vcap & cap_lists[i][1])) {
					if (i == ARRAY_SIZE(cap_lists) - 1)
						vref = AC_PINCTL_VREF_HIZ;
					else
						vref = cap_lists[i + 1][0];
				}
			}
			val &= ~AC_PINCTL_VREFEN;
			val |= vref;
		}
	}

	return val;
}
EXPORT_SYMBOL_GPL(snd_hda_correct_pin_ctl);

/**
 * _snd_hda_pin_ctl - Helper to set pin ctl value
 * @codec: the HDA codec
 * @pin: referred pin NID
 * @val: pin control value to set
 * @cached: access over codec pinctl cache or direct write
 *
 * This function is a helper to set a pin ctl value more safely.
 * It corrects the pin ctl value via snd_hda_correct_pin_ctl(), stores the
 * value in pin target array via snd_hda_codec_set_pin_target(), then
 * actually writes the value via either snd_hda_codec_update_cache() or
 * snd_hda_codec_write() depending on @cached flag.
 */
int _snd_hda_set_pin_ctl(struct hda_codec *codec, hda_nid_t pin,
			 unsigned int val, bool cached)
{
	val = snd_hda_correct_pin_ctl(codec, pin, val);
	snd_hda_codec_set_pin_target(codec, pin, val);
	if (cached)
		return snd_hda_codec_update_cache(codec, pin, 0,
				AC_VERB_SET_PIN_WIDGET_CONTROL, val);
	else
		return snd_hda_codec_write(codec, pin, 0,
					   AC_VERB_SET_PIN_WIDGET_CONTROL, val);
}
EXPORT_SYMBOL_GPL(_snd_hda_set_pin_ctl);

/**
 * snd_hda_add_imux_item - Add an item to input_mux
 * @codec: the HDA codec
 * @imux: imux helper object
 * @label: the name of imux item to assign
 * @index: index number of imux item to assign
 * @type_idx: pointer to store the resultant label index
 *
 * When the same label is used already in the existing items, the number
 * suffix is appended to the label.  This label index number is stored
 * to type_idx when non-NULL pointer is given.
 */
int snd_hda_add_imux_item(struct hda_codec *codec,
			  struct hda_input_mux *imux, const char *label,
			  int index, int *type_idx)
{
	int i, label_idx = 0;
	if (imux->num_items >= HDA_MAX_NUM_INPUTS) {
		codec_err(codec, "hda_codec: Too many imux items!\n");
		return -EINVAL;
	}
	for (i = 0; i < imux->num_items; i++) {
		if (!strncmp(label, imux->items[i].label, strlen(label)))
			label_idx++;
	}
	if (type_idx)
		*type_idx = label_idx;
	if (label_idx > 0)
		snprintf(imux->items[imux->num_items].label,
			 sizeof(imux->items[imux->num_items].label),
			 "%s %d", label, label_idx);
	else
		strlcpy(imux->items[imux->num_items].label, label,
			sizeof(imux->items[imux->num_items].label));
	imux->items[imux->num_items].index = index;
	imux->num_items++;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_add_imux_item);

/**
 * snd_hda_bus_reset_codecs - Reset the bus
 * @bus: HD-audio bus
 */
void snd_hda_bus_reset_codecs(struct hda_bus *bus)
{
	struct hda_codec *codec;

	list_for_each_codec(codec, bus) {
		/* FIXME: maybe a better way needed for forced reset */
		if (current_work() != &codec->jackpoll_work.work)
			cancel_delayed_work_sync(&codec->jackpoll_work);
#ifdef CONFIG_PM
		if (hda_codec_is_power_on(codec)) {
			hda_call_codec_suspend(codec);
			hda_call_codec_resume(codec);
		}
#endif
	}
}

/**
 * snd_print_pcm_bits - Print the supported PCM fmt bits to the string buffer
 * @pcm: PCM caps bits
 * @buf: the string buffer to write
 * @buflen: the max buffer length
 *
 * used by hda_proc.c and hda_eld.c
 */
void snd_print_pcm_bits(int pcm, char *buf, int buflen)
{
	static unsigned int bits[] = { 8, 16, 20, 24, 32 };
	int i, j;

	for (i = 0, j = 0; i < ARRAY_SIZE(bits); i++)
		if (pcm & (AC_SUPPCM_BITS_8 << i))
			j += scnprintf(buf + j, buflen - j,  " %d", bits[i]);

	buf[j] = '\0'; /* necessary when j == 0 */
}
EXPORT_SYMBOL_GPL(snd_print_pcm_bits);

MODULE_DESCRIPTION("HDA codec core");
MODULE_LICENSE("GPL");
