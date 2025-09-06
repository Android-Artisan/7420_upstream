/*
 * Copyright(c) 2004 - 2006 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called COPYING.
 */

/*
 * This code implements the DMA subsystem. It provides a HW-neutral interface
 * for other kernel code to use asynchronous memory copy capabilities,
 * if present, and allows different HW DMA drivers to register as providing
 * this capability.
 *
 * Due to the fact we are accelerating what is already a relatively fast
 * operation, the code goes to great lengths to avoid additional overhead,
 * such as locking.
 *
 * LOCKING:
 *
 * The subsystem keeps two global lists, dma_device_list and dma_client_list.
 * Both of these are protected by a mutex, dma_list_mutex.
 * The subsystem keeps a global list of dma_device structs it is protected by a
 * mutex, dma_list_mutex.
 *
 * A subsystem can get access to a channel by calling dmaengine_get() followed
 * by dma_find_channel(), or if it has need for an exclusive channel it can call
 * dma_request_channel().  Once a channel is allocated a reference is taken
 * against its corresponding driver to disable removal.
 *
 * Each device has a channels list, which runs unlocked but is never modified
 * once the device is registered, it's just setup by the driver.
 *
 * Each client is responsible for keeping track of the channels it uses.  See
 * the definition of dma_event_callback in dmaengine.h.
 *
 * Each device has a kref, which is initialized to 1 when the device is
 * registered. A kref_get is done for each device registered.  When the
 * device is released, the corresponding kref_put is done in the release
 * method. Every time one of the device's channels is allocated to a client,
 * a kref_get occurs.  When the channel is freed, the corresponding kref_put
 * happens. The device's release function does a completion, so
 * unregister_device does a remove event, device_unregister, a kref_put
 * for the first reference, then waits on the completion for all other
 * references to finish.
 *
 * Each channel has an open-coded implementation of Rusty Russell's "bigref,"
 * with a kref and a per_cpu local_t.  A dma_chan_get is called when a client
 * signals that it wants to use a channel, and dma_chan_put is called when
 * a channel is removed or a client using it is unregistered.  A client can
 * take extra references per outstanding transaction, as is the case with
 * the NET DMA client.  The release function does a kref_put on the device.
 *	-ChrisL, DanW
 */

 * See Documentation/dmaengine.txt for more details
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/device.h>
#include <linux/dmaengine.h>
#include <linux/hardirq.h>
#include <linux/spinlock.h>
#include <linux/percpu.h>
#include <linux/rcupdate.h>
#include <linux/mutex.h>
#include <linux/jiffies.h>

static DEFINE_MUTEX(dma_list_mutex);
static LIST_HEAD(dma_device_list);
static LIST_HEAD(dma_client_list);

/* --- sysfs implementation --- */

static ssize_t show_memcpy_count(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dma_chan *chan = to_dma_chan(dev);
	unsigned long count = 0;
	int i;

	for_each_possible_cpu(i)
		count += per_cpu_ptr(chan->local, i)->memcpy_count;

	return sprintf(buf, "%lu\n", count);
}

static ssize_t show_bytes_transferred(struct device *dev, struct device_attribute *attr,
				      char *buf)
{
	struct dma_chan *chan = to_dma_chan(dev);
	unsigned long count = 0;
	int i;

	for_each_possible_cpu(i)
		count += per_cpu_ptr(chan->local, i)->bytes_transferred;

	return sprintf(buf, "%lu\n", count);
}

static ssize_t show_in_use(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dma_chan *chan = to_dma_chan(dev);
	int in_use = 0;

	if (unlikely(chan->slow_ref) &&
		atomic_read(&chan->refcount.refcount) > 1)
		in_use = 1;
	else {
		if (local_read(&(per_cpu_ptr(chan->local,
			get_cpu())->refcount)) > 0)
			in_use = 1;
		put_cpu();
	}

	return sprintf(buf, "%d\n", in_use);
}

static struct device_attribute dma_attrs[] = {
	__ATTR(memcpy_count, S_IRUGO, show_memcpy_count, NULL),
	__ATTR(bytes_transferred, S_IRUGO, show_bytes_transferred, NULL),
	__ATTR(in_use, S_IRUGO, show_in_use, NULL),
	__ATTR_NULL
};

static void dma_async_device_cleanup(struct kref *kref);

static void dma_dev_release(struct device *dev)
{
	struct dma_chan *chan = to_dma_chan(dev);
	kref_put(&chan->device->refcount, dma_async_device_cleanup);
#include <linux/rculist.h>
#include <linux/idr.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <linux/acpi_dma.h>
#include <linux/of_dma.h>
#include <linux/mempool.h>

static DEFINE_MUTEX(dma_list_mutex);
static DEFINE_IDR(dma_idr);
static LIST_HEAD(dma_device_list);
static long dmaengine_ref_count;

/* --- sysfs implementation --- */

/**
 * dev_to_dma_chan - convert a device pointer to the its sysfs container object
 * @dev - device node
 *
 * Must be called under dma_list_mutex
 */
static struct dma_chan *dev_to_dma_chan(struct device *dev)
{
	struct dma_chan_dev *chan_dev;

	chan_dev = container_of(dev, typeof(*chan_dev), device);
	return chan_dev->chan;
}

static ssize_t memcpy_count_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct dma_chan *chan;
	unsigned long count = 0;
	int i;
	int err;

	mutex_lock(&dma_list_mutex);
	chan = dev_to_dma_chan(dev);
	if (chan) {
		for_each_possible_cpu(i)
			count += per_cpu_ptr(chan->local, i)->memcpy_count;
		err = sprintf(buf, "%lu\n", count);
	} else
		err = -ENODEV;
	mutex_unlock(&dma_list_mutex);

	return err;
}
static DEVICE_ATTR_RO(memcpy_count);

static ssize_t bytes_transferred_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct dma_chan *chan;
	unsigned long count = 0;
	int i;
	int err;

	mutex_lock(&dma_list_mutex);
	chan = dev_to_dma_chan(dev);
	if (chan) {
		for_each_possible_cpu(i)
			count += per_cpu_ptr(chan->local, i)->bytes_transferred;
		err = sprintf(buf, "%lu\n", count);
	} else
		err = -ENODEV;
	mutex_unlock(&dma_list_mutex);

	return err;
}
static DEVICE_ATTR_RO(bytes_transferred);

static ssize_t in_use_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct dma_chan *chan;
	int err;

	mutex_lock(&dma_list_mutex);
	chan = dev_to_dma_chan(dev);
	if (chan)
		err = sprintf(buf, "%d\n", chan->client_count);
	else
		err = -ENODEV;
	mutex_unlock(&dma_list_mutex);

	return err;
}
static DEVICE_ATTR_RO(in_use);

static struct attribute *dma_dev_attrs[] = {
	&dev_attr_memcpy_count.attr,
	&dev_attr_bytes_transferred.attr,
	&dev_attr_in_use.attr,
	NULL,
};
ATTRIBUTE_GROUPS(dma_dev);

static void chan_dev_release(struct device *dev)
{
	struct dma_chan_dev *chan_dev;

	chan_dev = container_of(dev, typeof(*chan_dev), device);
	if (atomic_dec_and_test(chan_dev->idr_ref)) {
		mutex_lock(&dma_list_mutex);
		idr_remove(&dma_idr, chan_dev->dev_id);
		mutex_unlock(&dma_list_mutex);
		kfree(chan_dev->idr_ref);
	}
	kfree(chan_dev);
}

static struct class dma_devclass = {
	.name		= "dma",
	.dev_attrs	= dma_attrs,
	.dev_release	= dma_dev_release,
	.dev_groups	= dma_dev_groups,
	.dev_release	= chan_dev_release,
};

/* --- client and device registration --- */

#define dma_chan_satisfies_mask(chan, mask) \
	__dma_chan_satisfies_mask((chan), &(mask))
static int
__dma_chan_satisfies_mask(struct dma_chan *chan, dma_cap_mask_t *want)
{
	dma_cap_mask_t has;

	bitmap_and(has.bits, want->bits, chan->device->cap_mask.bits,
#define dma_device_satisfies_mask(device, mask) \
	__dma_device_satisfies_mask((device), &(mask))
static int
__dma_device_satisfies_mask(struct dma_device *device,
			    const dma_cap_mask_t *want)
{
	dma_cap_mask_t has;

	bitmap_and(has.bits, want->bits, device->cap_mask.bits,
		DMA_TX_TYPE_END);
	return bitmap_equal(want->bits, has.bits, DMA_TX_TYPE_END);
}

/**
 * dma_client_chan_alloc - try to allocate channels to a client
 * @client: &dma_client
 *
 * Called with dma_list_mutex held.
 */
static void dma_client_chan_alloc(struct dma_client *client)
{
	struct dma_device *device;
	struct dma_chan *chan;
	int desc;	/* allocated descriptor count */
	enum dma_state_client ack;

	/* Find a channel */
	list_for_each_entry(device, &dma_device_list, global_node) {
		/* Does the client require a specific DMA controller? */
		if (client->slave && client->slave->dma_dev
				&& client->slave->dma_dev != device->dev)
			continue;

		list_for_each_entry(chan, &device->channels, device_node) {
			if (!dma_chan_satisfies_mask(chan, client->cap_mask))
				continue;

			desc = chan->device->device_alloc_chan_resources(
					chan, client);
			if (desc >= 0) {
				ack = client->event_callback(client,
						chan,
						DMA_RESOURCE_AVAILABLE);

				/* we are done once this client rejects
				 * an available resource
				 */
				if (ack == DMA_ACK) {
					dma_chan_get(chan);
					chan->client_count++;
				} else if (ack == DMA_NAK)
					return;
			}
		}
static struct module *dma_chan_to_owner(struct dma_chan *chan)
{
	return chan->device->dev->driver->owner;
}

/**
 * balance_ref_count - catch up the channel reference count
 * @chan - channel to balance ->client_count versus dmaengine_ref_count
 *
 * balance_ref_count must be called under dma_list_mutex
 */
static void balance_ref_count(struct dma_chan *chan)
{
	struct module *owner = dma_chan_to_owner(chan);

	while (chan->client_count < dmaengine_ref_count) {
		__module_get(owner);
		chan->client_count++;
	}
}

/**
 * dma_chan_get - try to grab a dma channel's parent driver module
 * @chan - channel to grab
 *
 * Must be called under dma_list_mutex
 */
static int dma_chan_get(struct dma_chan *chan)
{
	struct module *owner = dma_chan_to_owner(chan);
	int ret;

	/* The channel is already in use, update client count */
	if (chan->client_count) {
		__module_get(owner);
		chan->client_count++;
		return 0;
	}

	if (!try_module_get(owner))
		return -ENODEV;

	/* allocate upon first client reference */
	if (chan->device->device_alloc_chan_resources) {
		ret = chan->device->device_alloc_chan_resources(chan);
		if (ret < 0)
			goto err_out;
	}

	chan->client_count++;

	if (!dma_has_cap(DMA_PRIVATE, chan->device->cap_mask))
		balance_ref_count(chan);

	return 0;

err_out:
	module_put(owner);
	return ret;
}

/**
 * dma_chan_put - drop a reference to a dma channel's parent driver module
 * @chan - channel to release
 *
 * Must be called under dma_list_mutex
 */
static void dma_chan_put(struct dma_chan *chan)
{
	/* This channel is not in use, bail out */
	if (!chan->client_count)
		return;

	chan->client_count--;
	module_put(dma_chan_to_owner(chan));

	/* This channel is not in use anymore, free it */
	if (!chan->client_count && chan->device->device_free_chan_resources)
		chan->device->device_free_chan_resources(chan);

	/* If the channel is used via a DMA request router, free the mapping */
	if (chan->router && chan->router->route_free) {
		chan->router->route_free(chan->router->dev, chan->route_data);
		chan->router = NULL;
		chan->route_data = NULL;
	}
}

enum dma_status dma_sync_wait(struct dma_chan *chan, dma_cookie_t cookie)
{
	enum dma_status status;
	unsigned long dma_sync_wait_timeout = jiffies + msecs_to_jiffies(5000);

	dma_async_issue_pending(chan);
	do {
		status = dma_async_is_tx_complete(chan, cookie, NULL, NULL);
		if (time_after_eq(jiffies, dma_sync_wait_timeout)) {
			printk(KERN_ERR "dma_sync_wait_timeout!\n");
			return DMA_ERROR;
		}
	} while (status == DMA_IN_PROGRESS);
			pr_err("%s: timeout!\n", __func__);
			return DMA_ERROR;
		}
		if (status != DMA_IN_PROGRESS)
			break;
		cpu_relax();
	} while (1);

	return status;
}
EXPORT_SYMBOL(dma_sync_wait);

/**
 * dma_chan_cleanup - release a DMA channel's resources
 * @kref: kernel reference structure that contains the DMA channel device
 */
void dma_chan_cleanup(struct kref *kref)
{
	struct dma_chan *chan = container_of(kref, struct dma_chan, refcount);
	chan->device->device_free_chan_resources(chan);
	kref_put(&chan->device->refcount, dma_async_device_cleanup);
}
EXPORT_SYMBOL(dma_chan_cleanup);

static void dma_chan_free_rcu(struct rcu_head *rcu)
{
	struct dma_chan *chan = container_of(rcu, struct dma_chan, rcu);
	int bias = 0x7FFFFFFF;
	int i;
	for_each_possible_cpu(i)
		bias -= local_read(&per_cpu_ptr(chan->local, i)->refcount);
	atomic_sub(bias, &chan->refcount.refcount);
	kref_put(&chan->refcount, dma_chan_cleanup);
}

static void dma_chan_release(struct dma_chan *chan)
{
	atomic_add(0x7FFFFFFF, &chan->refcount.refcount);
	chan->slow_ref = 1;
	call_rcu(&chan->rcu, dma_chan_free_rcu);
}

/**
 * dma_chans_notify_available - broadcast available channels to the clients
 */
static void dma_clients_notify_available(void)
{
	struct dma_client *client;

	mutex_lock(&dma_list_mutex);

	list_for_each_entry(client, &dma_client_list, global_node)
		dma_client_chan_alloc(client);

	mutex_unlock(&dma_list_mutex);
}

/**
 * dma_chans_notify_available - tell the clients that a channel is going away
 * @chan: channel on its way out
 */
static void dma_clients_notify_removed(struct dma_chan *chan)
{
	struct dma_client *client;
	enum dma_state_client ack;

	mutex_lock(&dma_list_mutex);

	list_for_each_entry(client, &dma_client_list, global_node) {
		ack = client->event_callback(client, chan,
				DMA_RESOURCE_REMOVED);

		/* client was holding resources for this channel so
		 * free it
		 */
		if (ack == DMA_ACK) {
			dma_chan_put(chan);
			chan->client_count--;
 * dma_cap_mask_all - enable iteration over all operation types
 */
static dma_cap_mask_t dma_cap_mask_all;

/**
 * dma_chan_tbl_ent - tracks channel allocations per core/operation
 * @chan - associated channel for this entry
 */
struct dma_chan_tbl_ent {
	struct dma_chan *chan;
};

/**
 * channel_table - percpu lookup table for memory-to-memory offload providers
 */
static struct dma_chan_tbl_ent __percpu *channel_table[DMA_TX_TYPE_END];

static int __init dma_channel_table_init(void)
{
	enum dma_transaction_type cap;
	int err = 0;

	bitmap_fill(dma_cap_mask_all.bits, DMA_TX_TYPE_END);

	/* 'interrupt', 'private', and 'slave' are channel capabilities,
	 * but are not associated with an operation so they do not need
	 * an entry in the channel_table
	 */
	clear_bit(DMA_INTERRUPT, dma_cap_mask_all.bits);
	clear_bit(DMA_PRIVATE, dma_cap_mask_all.bits);
	clear_bit(DMA_SLAVE, dma_cap_mask_all.bits);

	for_each_dma_cap_mask(cap, dma_cap_mask_all) {
		channel_table[cap] = alloc_percpu(struct dma_chan_tbl_ent);
		if (!channel_table[cap]) {
			err = -ENOMEM;
			break;
		}
	}

	if (err) {
		pr_err("initialization failure\n");
		for_each_dma_cap_mask(cap, dma_cap_mask_all)
			free_percpu(channel_table[cap]);
	}

	return err;
}
arch_initcall(dma_channel_table_init);

/**
 * dma_find_channel - find a channel to carry out the operation
 * @tx_type: transaction type
 */
struct dma_chan *dma_find_channel(enum dma_transaction_type tx_type)
{
	return this_cpu_read(channel_table[tx_type]->chan);
}
EXPORT_SYMBOL(dma_find_channel);

/**
 * dma_issue_pending_all - flush all pending operations across all channels
 */
void dma_issue_pending_all(void)
{
	struct dma_device *device;
	struct dma_chan *chan;

	rcu_read_lock();
	list_for_each_entry_rcu(device, &dma_device_list, global_node) {
		if (dma_has_cap(DMA_PRIVATE, device->cap_mask))
			continue;
		list_for_each_entry(chan, &device->channels, device_node)
			if (chan->client_count)
				device->device_issue_pending(chan);
	}
	rcu_read_unlock();
}
EXPORT_SYMBOL(dma_issue_pending_all);

/**
 * dma_chan_is_local - returns true if the channel is in the same numa-node as the cpu
 */
static bool dma_chan_is_local(struct dma_chan *chan, int cpu)
{
	int node = dev_to_node(chan->device->dev);
	return node == -1 || cpumask_test_cpu(cpu, cpumask_of_node(node));
}

/**
 * min_chan - returns the channel with min count and in the same numa-node as the cpu
 * @cap: capability to match
 * @cpu: cpu index which the channel should be close to
 *
 * If some channels are close to the given cpu, the one with the lowest
 * reference count is returned. Otherwise, cpu is ignored and only the
 * reference count is taken into account.
 * Must be called under dma_list_mutex.
 */
static struct dma_chan *min_chan(enum dma_transaction_type cap, int cpu)
{
	struct dma_device *device;
	struct dma_chan *chan;
	struct dma_chan *min = NULL;
	struct dma_chan *localmin = NULL;

	list_for_each_entry(device, &dma_device_list, global_node) {
		if (!dma_has_cap(cap, device->cap_mask) ||
		    dma_has_cap(DMA_PRIVATE, device->cap_mask))
			continue;
		list_for_each_entry(chan, &device->channels, device_node) {
			if (!chan->client_count)
				continue;
			if (!min || chan->table_count < min->table_count)
				min = chan;

			if (dma_chan_is_local(chan, cpu))
				if (!localmin ||
				    chan->table_count < localmin->table_count)
					localmin = chan;
		}
	}

	chan = localmin ? localmin : min;

	if (chan)
		chan->table_count++;

	return chan;
}

/**
 * dma_channel_rebalance - redistribute the available channels
 *
 * Optimize for cpu isolation (each cpu gets a dedicated channel for an
 * operation type) in the SMP case,  and operation isolation (avoid
 * multi-tasking channels) in the non-SMP case.  Must be called under
 * dma_list_mutex.
 */
static void dma_channel_rebalance(void)
{
	struct dma_chan *chan;
	struct dma_device *device;
	int cpu;
	int cap;

	/* undo the last distribution */
	for_each_dma_cap_mask(cap, dma_cap_mask_all)
		for_each_possible_cpu(cpu)
			per_cpu_ptr(channel_table[cap], cpu)->chan = NULL;

	list_for_each_entry(device, &dma_device_list, global_node) {
		if (dma_has_cap(DMA_PRIVATE, device->cap_mask))
			continue;
		list_for_each_entry(chan, &device->channels, device_node)
			chan->table_count = 0;
	}

	/* don't populate the channel_table if no clients are available */
	if (!dmaengine_ref_count)
		return;

	/* redistribute available channels */
	for_each_dma_cap_mask(cap, dma_cap_mask_all)
		for_each_online_cpu(cpu) {
			chan = min_chan(cap, cpu);
			per_cpu_ptr(channel_table[cap], cpu)->chan = chan;
		}
}

int dma_get_slave_caps(struct dma_chan *chan, struct dma_slave_caps *caps)
{
	struct dma_device *device;

	if (!chan || !caps)
		return -EINVAL;

	device = chan->device;

	/* check if the channel supports slave transactions */
	if (!test_bit(DMA_SLAVE, device->cap_mask.bits))
		return -ENXIO;

	/*
	 * Check whether it reports it uses the generic slave
	 * capabilities, if not, that means it doesn't support any
	 * kind of slave capabilities reporting.
	 */
	if (!device->directions)
		return -ENXIO;

	caps->src_addr_widths = device->src_addr_widths;
	caps->dst_addr_widths = device->dst_addr_widths;
	caps->directions = device->directions;
	caps->residue_granularity = device->residue_granularity;

	/*
	 * Some devices implement only pause (e.g. to get residuum) but no
	 * resume. However cmd_pause is advertised as pause AND resume.
	 */
	caps->cmd_pause = !!(device->device_pause && device->device_resume);
	caps->cmd_terminate = !!device->device_terminate_all;

	return 0;
}
EXPORT_SYMBOL_GPL(dma_get_slave_caps);

static struct dma_chan *private_candidate(const dma_cap_mask_t *mask,
					  struct dma_device *dev,
					  dma_filter_fn fn, void *fn_param)
{
	struct dma_chan *chan;

	if (!__dma_device_satisfies_mask(dev, mask)) {
		pr_debug("%s: wrong capabilities\n", __func__);
		return NULL;
	}
	/* devices with multiple channels need special handling as we need to
	 * ensure that all channels are either private or public.
	 */
	if (dev->chancnt > 1 && !dma_has_cap(DMA_PRIVATE, dev->cap_mask))
		list_for_each_entry(chan, &dev->channels, device_node) {
			/* some channels are already publicly allocated */
			if (chan->client_count)
				return NULL;
		}

	list_for_each_entry(chan, &dev->channels, device_node) {
		if (chan->client_count) {
			pr_debug("%s: %s busy\n",
				 __func__, dma_chan_name(chan));
			continue;
		}
		if (fn && !fn(chan, fn_param)) {
			pr_debug("%s: %s filter said false\n",
				 __func__, dma_chan_name(chan));
			continue;
		}
		return chan;
	}

	return NULL;
}

/**
 * dma_get_slave_channel - try to get specific channel exclusively
 * @chan: target channel
 */
struct dma_chan *dma_get_slave_channel(struct dma_chan *chan)
{
	int err = -EBUSY;

	/* lock against __dma_request_channel */
	mutex_lock(&dma_list_mutex);

	if (chan->client_count == 0) {
		struct dma_device *device = chan->device;

		dma_cap_set(DMA_PRIVATE, device->cap_mask);
		device->privatecnt++;
		err = dma_chan_get(chan);
		if (err) {
			pr_debug("%s: failed to get %s: (%d)\n",
				__func__, dma_chan_name(chan), err);
			chan = NULL;
			if (--device->privatecnt == 0)
				dma_cap_clear(DMA_PRIVATE, device->cap_mask);
		}
	} else
		chan = NULL;

	mutex_unlock(&dma_list_mutex);


	return chan;
}
EXPORT_SYMBOL_GPL(dma_get_slave_channel);

struct dma_chan *dma_get_any_slave_channel(struct dma_device *device)
{
	dma_cap_mask_t mask;
	struct dma_chan *chan;
	int err;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	/* lock against __dma_request_channel */
	mutex_lock(&dma_list_mutex);

	chan = private_candidate(&mask, device, NULL, NULL);
	if (chan) {
		dma_cap_set(DMA_PRIVATE, device->cap_mask);
		device->privatecnt++;
		err = dma_chan_get(chan);
		if (err) {
			pr_debug("%s: failed to get %s: (%d)\n",
				__func__, dma_chan_name(chan), err);
			chan = NULL;
			if (--device->privatecnt == 0)
				dma_cap_clear(DMA_PRIVATE, device->cap_mask);
		}
	}

	mutex_unlock(&dma_list_mutex);
}

/**
 * dma_async_client_register - register a &dma_client
 * @client: ptr to a client structure with valid 'event_callback' and 'cap_mask'
 */
void dma_async_client_register(struct dma_client *client)
{
	/* validate client data */
	BUG_ON(dma_has_cap(DMA_SLAVE, client->cap_mask) &&
		!client->slave);

	mutex_lock(&dma_list_mutex);
	list_add_tail(&client->global_node, &dma_client_list);
	mutex_unlock(&dma_list_mutex);
}
EXPORT_SYMBOL(dma_async_client_register);

/**
 * dma_async_client_unregister - unregister a client and free the &dma_client
 * @client: &dma_client to free
 *
 * Force frees any allocated DMA channels, frees the &dma_client memory
 */
void dma_async_client_unregister(struct dma_client *client)
{
	struct dma_device *device;
	struct dma_chan *chan;
	enum dma_state_client ack;

	if (!client)
		return;

	mutex_lock(&dma_list_mutex);
	/* free all channels the client is holding */
	list_for_each_entry(device, &dma_device_list, global_node)
		list_for_each_entry(chan, &device->channels, device_node) {
			ack = client->event_callback(client, chan,
				DMA_RESOURCE_REMOVED);

			if (ack == DMA_ACK) {
				dma_chan_put(chan);
				chan->client_count--;
			}
		}

	list_del(&client->global_node);
	mutex_unlock(&dma_list_mutex);
}
EXPORT_SYMBOL(dma_async_client_unregister);

/**
 * dma_async_client_chan_request - send all available channels to the
 * client that satisfy the capability mask
 * @client - requester
 */
void dma_async_client_chan_request(struct dma_client *client)
{
	mutex_lock(&dma_list_mutex);
	dma_client_chan_alloc(client);
	mutex_unlock(&dma_list_mutex);
}
EXPORT_SYMBOL(dma_async_client_chan_request);

	return chan;
}
EXPORT_SYMBOL_GPL(dma_get_any_slave_channel);

/**
 * __dma_request_channel - try to allocate an exclusive channel
 * @mask: capabilities that the channel must satisfy
 * @fn: optional callback to disposition available channels
 * @fn_param: opaque parameter to pass to dma_filter_fn
 *
 * Returns pointer to appropriate DMA channel on success or NULL.
 */
struct dma_chan *__dma_request_channel(const dma_cap_mask_t *mask,
				       dma_filter_fn fn, void *fn_param)
{
	struct dma_device *device, *_d;
	struct dma_chan *chan = NULL;
	int err;

	/* Find a channel */
	mutex_lock(&dma_list_mutex);
	list_for_each_entry_safe(device, _d, &dma_device_list, global_node) {
		chan = private_candidate(mask, device, fn, fn_param);
		if (chan) {
			/* Found a suitable channel, try to grab, prep, and
			 * return it.  We first set DMA_PRIVATE to disable
			 * balance_ref_count as this channel will not be
			 * published in the general-purpose allocator
			 */
			dma_cap_set(DMA_PRIVATE, device->cap_mask);
			device->privatecnt++;
			err = dma_chan_get(chan);

			if (err == -ENODEV) {
				pr_debug("%s: %s module removed\n",
					 __func__, dma_chan_name(chan));
				list_del_rcu(&device->global_node);
			} else if (err)
				pr_debug("%s: failed to get %s: (%d)\n",
					 __func__, dma_chan_name(chan), err);
			else
				break;
			if (--device->privatecnt == 0)
				dma_cap_clear(DMA_PRIVATE, device->cap_mask);
			chan = NULL;
		}
	}
	mutex_unlock(&dma_list_mutex);

	pr_debug("%s: %s (%s)\n",
		 __func__,
		 chan ? "success" : "fail",
		 chan ? dma_chan_name(chan) : NULL);

	return chan;
}
EXPORT_SYMBOL_GPL(__dma_request_channel);

/**
 * dma_request_slave_channel_reason - try to allocate an exclusive slave channel
 * @dev:	pointer to client device structure
 * @name:	slave channel name
 *
 * Returns pointer to appropriate DMA channel on success or an error pointer.
 */
struct dma_chan *dma_request_slave_channel_reason(struct device *dev,
						  const char *name)
{
	/* If device-tree is present get slave info from here */
	if (dev->of_node)
		return of_dma_request_slave_channel(dev->of_node, name);

	/* If device was enumerated by ACPI get slave info from here */
	if (ACPI_HANDLE(dev))
		return acpi_dma_request_slave_chan_by_name(dev, name);

	return ERR_PTR(-ENODEV);
}
EXPORT_SYMBOL_GPL(dma_request_slave_channel_reason);

/**
 * dma_request_slave_channel - try to allocate an exclusive slave channel
 * @dev:	pointer to client device structure
 * @name:	slave channel name
 *
 * Returns pointer to appropriate DMA channel on success or NULL.
 */
struct dma_chan *dma_request_slave_channel(struct device *dev,
					   const char *name)
{
	struct dma_chan *ch = dma_request_slave_channel_reason(dev, name);
	if (IS_ERR(ch))
		return NULL;

	dma_cap_set(DMA_PRIVATE, ch->device->cap_mask);
	ch->device->privatecnt++;

	return ch;
}
EXPORT_SYMBOL_GPL(dma_request_slave_channel);

void dma_release_channel(struct dma_chan *chan)
{
	mutex_lock(&dma_list_mutex);
	WARN_ONCE(chan->client_count != 1,
		  "chan reference count %d != 1\n", chan->client_count);
	dma_chan_put(chan);
	/* drop PRIVATE cap enabled by __dma_request_channel() */
	if (--chan->device->privatecnt == 0)
		dma_cap_clear(DMA_PRIVATE, chan->device->cap_mask);
	mutex_unlock(&dma_list_mutex);
}
EXPORT_SYMBOL_GPL(dma_release_channel);

/**
 * dmaengine_get - register interest in dma_channels
 */
void dmaengine_get(void)
{
	struct dma_device *device, *_d;
	struct dma_chan *chan;
	int err;

	mutex_lock(&dma_list_mutex);
	dmaengine_ref_count++;

	/* try to grab channels */
	list_for_each_entry_safe(device, _d, &dma_device_list, global_node) {
		if (dma_has_cap(DMA_PRIVATE, device->cap_mask))
			continue;
		list_for_each_entry(chan, &device->channels, device_node) {
			err = dma_chan_get(chan);
			if (err == -ENODEV) {
				/* module removed before we could use it */
				list_del_rcu(&device->global_node);
				break;
			} else if (err)
				pr_debug("%s: failed to get %s: (%d)\n",
				       __func__, dma_chan_name(chan), err);
		}
	}

	/* if this is the first reference and there were channels
	 * waiting we need to rebalance to get those channels
	 * incorporated into the channel table
	 */
	if (dmaengine_ref_count == 1)
		dma_channel_rebalance();
	mutex_unlock(&dma_list_mutex);
}
EXPORT_SYMBOL(dmaengine_get);

/**
 * dmaengine_put - let dma drivers be removed when ref_count == 0
 */
void dmaengine_put(void)
{
	struct dma_device *device;
	struct dma_chan *chan;

	mutex_lock(&dma_list_mutex);
	dmaengine_ref_count--;
	BUG_ON(dmaengine_ref_count < 0);
	/* drop channel references */
	list_for_each_entry(device, &dma_device_list, global_node) {
		if (dma_has_cap(DMA_PRIVATE, device->cap_mask))
			continue;
		list_for_each_entry(chan, &device->channels, device_node)
			dma_chan_put(chan);
	}
	mutex_unlock(&dma_list_mutex);
}
EXPORT_SYMBOL(dmaengine_put);

static bool device_has_all_tx_types(struct dma_device *device)
{
	/* A device that satisfies this test has channels that will never cause
	 * an async_tx channel switch event as all possible operation types can
	 * be handled.
	 */
	#ifdef CONFIG_ASYNC_TX_DMA
	if (!dma_has_cap(DMA_INTERRUPT, device->cap_mask))
		return false;
	#endif

	#if defined(CONFIG_ASYNC_MEMCPY) || defined(CONFIG_ASYNC_MEMCPY_MODULE)
	if (!dma_has_cap(DMA_MEMCPY, device->cap_mask))
		return false;
	#endif

	#if defined(CONFIG_ASYNC_XOR) || defined(CONFIG_ASYNC_XOR_MODULE)
	if (!dma_has_cap(DMA_XOR, device->cap_mask))
		return false;

	#ifndef CONFIG_ASYNC_TX_DISABLE_XOR_VAL_DMA
	if (!dma_has_cap(DMA_XOR_VAL, device->cap_mask))
		return false;
	#endif
	#endif

	#if defined(CONFIG_ASYNC_PQ) || defined(CONFIG_ASYNC_PQ_MODULE)
	if (!dma_has_cap(DMA_PQ, device->cap_mask))
		return false;

	#ifndef CONFIG_ASYNC_TX_DISABLE_PQ_VAL_DMA
	if (!dma_has_cap(DMA_PQ_VAL, device->cap_mask))
		return false;
	#endif
	#endif

	return true;
}

static int get_dma_id(struct dma_device *device)
{
	int rc;

	mutex_lock(&dma_list_mutex);

	rc = idr_alloc(&dma_idr, NULL, 0, 0, GFP_KERNEL);
	if (rc >= 0)
		device->dev_id = rc;

	mutex_unlock(&dma_list_mutex);
	return rc < 0 ? rc : 0;
}

/**
 * dma_async_device_register - registers DMA devices found
 * @device: &dma_device
 */
int dma_async_device_register(struct dma_device *device)
{
	static int id;
	int chancnt = 0, rc;
	struct dma_chan* chan;
	int chancnt = 0, rc;
	struct dma_chan* chan;
	atomic_t *idr_ref;

	if (!device)
		return -ENODEV;

	/* validate device routines */
	BUG_ON(dma_has_cap(DMA_MEMCPY, device->cap_mask) &&
		!device->device_prep_dma_memcpy);
	BUG_ON(dma_has_cap(DMA_XOR, device->cap_mask) &&
		!device->device_prep_dma_xor);
	BUG_ON(dma_has_cap(DMA_ZERO_SUM, device->cap_mask) &&
		!device->device_prep_dma_zero_sum);
	BUG_ON(dma_has_cap(DMA_XOR_VAL, device->cap_mask) &&
		!device->device_prep_dma_xor_val);
	BUG_ON(dma_has_cap(DMA_PQ, device->cap_mask) &&
		!device->device_prep_dma_pq);
	BUG_ON(dma_has_cap(DMA_PQ_VAL, device->cap_mask) &&
		!device->device_prep_dma_pq_val);
	BUG_ON(dma_has_cap(DMA_MEMSET, device->cap_mask) &&
		!device->device_prep_dma_memset);
	BUG_ON(dma_has_cap(DMA_INTERRUPT, device->cap_mask) &&
		!device->device_prep_dma_interrupt);
	BUG_ON(dma_has_cap(DMA_SLAVE, device->cap_mask) &&
		!device->device_prep_slave_sg);
	BUG_ON(dma_has_cap(DMA_SLAVE, device->cap_mask) &&
		!device->device_terminate_all);

	BUG_ON(!device->device_alloc_chan_resources);
	BUG_ON(!device->device_free_chan_resources);
	BUG_ON(!device->device_is_tx_complete);
	BUG_ON(!device->device_issue_pending);
	BUG_ON(!device->dev);

	init_completion(&device->done);
	kref_init(&device->refcount);
	device->dev_id = id++;

	/* represent channels in sysfs. Probably want devs too */
	list_for_each_entry(chan, &device->channels, device_node) {
		chan->local = alloc_percpu(typeof(*chan->local));
		if (chan->local == NULL)
			continue;

		chan->chan_id = chancnt++;
		chan->dev.class = &dma_devclass;
		chan->dev.parent = device->dev;
		snprintf(chan->dev.bus_id, BUS_ID_SIZE, "dma%dchan%d",
		         device->dev_id, chan->chan_id);

		rc = device_register(&chan->dev);
		if (rc) {
			chancnt--;
	BUG_ON(dma_has_cap(DMA_SG, device->cap_mask) &&
		!device->device_prep_dma_sg);
	BUG_ON(dma_has_cap(DMA_CYCLIC, device->cap_mask) &&
		!device->device_prep_dma_cyclic);
	BUG_ON(dma_has_cap(DMA_INTERLEAVE, device->cap_mask) &&
		!device->device_prep_interleaved_dma);

	BUG_ON(!device->device_tx_status);
	BUG_ON(!device->device_issue_pending);
	BUG_ON(!device->dev);

	/* note: this only matters in the
	 * CONFIG_ASYNC_TX_ENABLE_CHANNEL_SWITCH=n case
	 */
	if (device_has_all_tx_types(device))
		dma_cap_set(DMA_ASYNC_TX, device->cap_mask);

	idr_ref = kmalloc(sizeof(*idr_ref), GFP_KERNEL);
	if (!idr_ref)
		return -ENOMEM;
	rc = get_dma_id(device);
	if (rc != 0) {
		kfree(idr_ref);
		return rc;
	}

	atomic_set(idr_ref, 0);

	/* represent channels in sysfs. Probably want devs too */
	list_for_each_entry(chan, &device->channels, device_node) {
		rc = -ENOMEM;
		chan->local = alloc_percpu(typeof(*chan->local));
		if (chan->local == NULL)
			goto err_out;
		chan->dev = kzalloc(sizeof(*chan->dev), GFP_KERNEL);
		if (chan->dev == NULL) {
			free_percpu(chan->local);
			chan->local = NULL;
			goto err_out;
		}

		/* One for the channel, one of the class device */
		kref_get(&device->refcount);
		kref_get(&device->refcount);
		kref_init(&chan->refcount);
		chan->client_count = 0;
		chan->slow_ref = 0;
		INIT_RCU_HEAD(&chan->rcu);
	}

	mutex_lock(&dma_list_mutex);
	list_add_tail(&device->global_node, &dma_device_list);
	mutex_unlock(&dma_list_mutex);

	dma_clients_notify_available();

	return 0;

err_out:
	list_for_each_entry(chan, &device->channels, device_node) {
		if (chan->local == NULL)
			continue;
		kref_put(&device->refcount, dma_async_device_cleanup);
		device_unregister(&chan->dev);
		chancnt--;
		chan->chan_id = chancnt++;
		chan->dev->device.class = &dma_devclass;
		chan->dev->device.parent = device->dev;
		chan->dev->chan = chan;
		chan->dev->idr_ref = idr_ref;
		chan->dev->dev_id = device->dev_id;
		atomic_inc(idr_ref);
		dev_set_name(&chan->dev->device, "dma%dchan%d",
			     device->dev_id, chan->chan_id);

		rc = device_register(&chan->dev->device);
		if (rc) {
			free_percpu(chan->local);
			chan->local = NULL;
			kfree(chan->dev);
			atomic_dec(idr_ref);
			goto err_out;
		}
		chan->client_count = 0;
	}
	device->chancnt = chancnt;

	mutex_lock(&dma_list_mutex);
	/* take references on public channels */
	if (dmaengine_ref_count && !dma_has_cap(DMA_PRIVATE, device->cap_mask))
		list_for_each_entry(chan, &device->channels, device_node) {
			/* if clients are already waiting for channels we need
			 * to take references on their behalf
			 */
			if (dma_chan_get(chan) == -ENODEV) {
				/* note we can only get here for the first
				 * channel as the remaining channels are
				 * guaranteed to get a reference
				 */
				rc = -ENODEV;
				mutex_unlock(&dma_list_mutex);
				goto err_out;
			}
		}
	list_add_tail_rcu(&device->global_node, &dma_device_list);
	if (dma_has_cap(DMA_PRIVATE, device->cap_mask))
		device->privatecnt++;	/* Always private */
	dma_channel_rebalance();
	mutex_unlock(&dma_list_mutex);

	return 0;

err_out:
	/* if we never registered a channel just release the idr */
	if (atomic_read(idr_ref) == 0) {
		mutex_lock(&dma_list_mutex);
		idr_remove(&dma_idr, device->dev_id);
		mutex_unlock(&dma_list_mutex);
		kfree(idr_ref);
		return rc;
	}

	list_for_each_entry(chan, &device->channels, device_node) {
		if (chan->local == NULL)
			continue;
		mutex_lock(&dma_list_mutex);
		chan->dev->chan = NULL;
		mutex_unlock(&dma_list_mutex);
		device_unregister(&chan->dev->device);
		free_percpu(chan->local);
	}
	return rc;
}
EXPORT_SYMBOL(dma_async_device_register);

/**
 * dma_async_device_cleanup - function called when all references are released
 * @kref: kernel reference object
 */
static void dma_async_device_cleanup(struct kref *kref)
{
	struct dma_device *device;

	device = container_of(kref, struct dma_device, refcount);
	complete(&device->done);
}

/**
 * dma_async_device_unregister - unregisters DMA devices
 * @device: &dma_device
 * dma_async_device_unregister - unregister a DMA device
 * @device: &dma_device
 *
 * This routine is called by dma driver exit routines, dmaengine holds module
 * references to prevent it being called while channels are in use.
 */
void dma_async_device_unregister(struct dma_device *device)
{
	struct dma_chan *chan;

	mutex_lock(&dma_list_mutex);
	list_del(&device->global_node);
	mutex_unlock(&dma_list_mutex);

	list_for_each_entry(chan, &device->channels, device_node) {
		dma_clients_notify_removed(chan);
		device_unregister(&chan->dev);
		dma_chan_release(chan);
	}

	kref_put(&device->refcount, dma_async_device_cleanup);
	wait_for_completion(&device->done);
}
EXPORT_SYMBOL(dma_async_device_unregister);

/**
 * dma_async_memcpy_buf_to_buf - offloaded copy between virtual addresses
 * @chan: DMA channel to offload copy to
 * @dest: destination address (virtual)
 * @src: source address (virtual)
 * @len: length
 *
 * Both @dest and @src must be mappable to a bus address according to the
 * DMA mapping API rules for streaming mappings.
 * Both @dest and @src must stay memory resident (kernel memory or locked
 * user space pages).
 */
dma_cookie_t
dma_async_memcpy_buf_to_buf(struct dma_chan *chan, void *dest,
			void *src, size_t len)
{
	struct dma_device *dev = chan->device;
	struct dma_async_tx_descriptor *tx;
	dma_addr_t dma_dest, dma_src;
	dma_cookie_t cookie;
	int cpu;

	dma_src = dma_map_single(dev->dev, src, len, DMA_TO_DEVICE);
	dma_dest = dma_map_single(dev->dev, dest, len, DMA_FROM_DEVICE);
	tx = dev->device_prep_dma_memcpy(chan, dma_dest, dma_src, len,
					 DMA_CTRL_ACK);

	if (!tx) {
		dma_unmap_single(dev->dev, dma_src, len, DMA_TO_DEVICE);
		dma_unmap_single(dev->dev, dma_dest, len, DMA_FROM_DEVICE);
		return -ENOMEM;
	}

	tx->callback = NULL;
	cookie = tx->tx_submit(tx);

	cpu = get_cpu();
	per_cpu_ptr(chan->local, cpu)->bytes_transferred += len;
	per_cpu_ptr(chan->local, cpu)->memcpy_count++;
	put_cpu();

	return cookie;
}
EXPORT_SYMBOL(dma_async_memcpy_buf_to_buf);

/**
 * dma_async_memcpy_buf_to_pg - offloaded copy from address to page
 * @chan: DMA channel to offload copy to
 * @page: destination page
 * @offset: offset in page to copy to
 * @kdata: source address (virtual)
 * @len: length
 *
 * Both @page/@offset and @kdata must be mappable to a bus address according
 * to the DMA mapping API rules for streaming mappings.
 * Both @page/@offset and @kdata must stay memory resident (kernel memory or
 * locked user space pages)
 */
dma_cookie_t
dma_async_memcpy_buf_to_pg(struct dma_chan *chan, struct page *page,
			unsigned int offset, void *kdata, size_t len)
{
	struct dma_device *dev = chan->device;
	struct dma_async_tx_descriptor *tx;
	dma_addr_t dma_dest, dma_src;
	dma_cookie_t cookie;
	int cpu;

	dma_src = dma_map_single(dev->dev, kdata, len, DMA_TO_DEVICE);
	dma_dest = dma_map_page(dev->dev, page, offset, len, DMA_FROM_DEVICE);
	tx = dev->device_prep_dma_memcpy(chan, dma_dest, dma_src, len,
					 DMA_CTRL_ACK);

	if (!tx) {
		dma_unmap_single(dev->dev, dma_src, len, DMA_TO_DEVICE);
		dma_unmap_page(dev->dev, dma_dest, len, DMA_FROM_DEVICE);
		return -ENOMEM;
	}

	tx->callback = NULL;
	cookie = tx->tx_submit(tx);

	cpu = get_cpu();
	per_cpu_ptr(chan->local, cpu)->bytes_transferred += len;
	per_cpu_ptr(chan->local, cpu)->memcpy_count++;
	put_cpu();

	return cookie;
}
EXPORT_SYMBOL(dma_async_memcpy_buf_to_pg);

/**
 * dma_async_memcpy_pg_to_pg - offloaded copy from page to page
 * @chan: DMA channel to offload copy to
 * @dest_pg: destination page
 * @dest_off: offset in page to copy to
 * @src_pg: source page
 * @src_off: offset in page to copy from
 * @len: length
 *
 * Both @dest_page/@dest_off and @src_page/@src_off must be mappable to a bus
 * address according to the DMA mapping API rules for streaming mappings.
 * Both @dest_page/@dest_off and @src_page/@src_off must stay memory resident
 * (kernel memory or locked user space pages).
 */
dma_cookie_t
dma_async_memcpy_pg_to_pg(struct dma_chan *chan, struct page *dest_pg,
	unsigned int dest_off, struct page *src_pg, unsigned int src_off,
	size_t len)
{
	struct dma_device *dev = chan->device;
	struct dma_async_tx_descriptor *tx;
	dma_addr_t dma_dest, dma_src;
	dma_cookie_t cookie;
	int cpu;

	dma_src = dma_map_page(dev->dev, src_pg, src_off, len, DMA_TO_DEVICE);
	dma_dest = dma_map_page(dev->dev, dest_pg, dest_off, len,
				DMA_FROM_DEVICE);
	tx = dev->device_prep_dma_memcpy(chan, dma_dest, dma_src, len,
					 DMA_CTRL_ACK);

	if (!tx) {
		dma_unmap_page(dev->dev, dma_src, len, DMA_TO_DEVICE);
		dma_unmap_page(dev->dev, dma_dest, len, DMA_FROM_DEVICE);
		return -ENOMEM;
	}

	tx->callback = NULL;
	cookie = tx->tx_submit(tx);

	cpu = get_cpu();
	per_cpu_ptr(chan->local, cpu)->bytes_transferred += len;
	per_cpu_ptr(chan->local, cpu)->memcpy_count++;
	put_cpu();

	return cookie;
}
EXPORT_SYMBOL(dma_async_memcpy_pg_to_pg);
	list_del_rcu(&device->global_node);
	dma_channel_rebalance();
	mutex_unlock(&dma_list_mutex);

	list_for_each_entry(chan, &device->channels, device_node) {
		WARN_ONCE(chan->client_count,
			  "%s called while %d clients hold a reference\n",
			  __func__, chan->client_count);
		mutex_lock(&dma_list_mutex);
		chan->dev->chan = NULL;
		mutex_unlock(&dma_list_mutex);
		device_unregister(&chan->dev->device);
		free_percpu(chan->local);
	}
}
EXPORT_SYMBOL(dma_async_device_unregister);

struct dmaengine_unmap_pool {
	struct kmem_cache *cache;
	const char *name;
	mempool_t *pool;
	size_t size;
};

#define __UNMAP_POOL(x) { .size = x, .name = "dmaengine-unmap-" __stringify(x) }
static struct dmaengine_unmap_pool unmap_pool[] = {
	__UNMAP_POOL(2),
	#if IS_ENABLED(CONFIG_DMA_ENGINE_RAID)
	__UNMAP_POOL(16),
	__UNMAP_POOL(128),
	__UNMAP_POOL(256),
	#endif
};

static struct dmaengine_unmap_pool *__get_unmap_pool(int nr)
{
	int order = get_count_order(nr);

	switch (order) {
	case 0 ... 1:
		return &unmap_pool[0];
#if IS_ENABLED(CONFIG_DMA_ENGINE_RAID)
	case 2 ... 4:
		return &unmap_pool[1];
	case 5 ... 7:
		return &unmap_pool[2];
	case 8:
		return &unmap_pool[3];
#endif
	default:
		BUG();
		return NULL;
	}
}

static void dmaengine_unmap(struct kref *kref)
{
	struct dmaengine_unmap_data *unmap = container_of(kref, typeof(*unmap), kref);
	struct device *dev = unmap->dev;
	int cnt, i;

	cnt = unmap->to_cnt;
	for (i = 0; i < cnt; i++)
		dma_unmap_page(dev, unmap->addr[i], unmap->len,
			       DMA_TO_DEVICE);
	cnt += unmap->from_cnt;
	for (; i < cnt; i++)
		dma_unmap_page(dev, unmap->addr[i], unmap->len,
			       DMA_FROM_DEVICE);
	cnt += unmap->bidi_cnt;
	for (; i < cnt; i++) {
		if (unmap->addr[i] == 0)
			continue;
		dma_unmap_page(dev, unmap->addr[i], unmap->len,
			       DMA_BIDIRECTIONAL);
	}
	cnt = unmap->map_cnt;
	mempool_free(unmap, __get_unmap_pool(cnt)->pool);
}

void dmaengine_unmap_put(struct dmaengine_unmap_data *unmap)
{
	if (unmap)
		kref_put(&unmap->kref, dmaengine_unmap);
}
EXPORT_SYMBOL_GPL(dmaengine_unmap_put);

static void dmaengine_destroy_unmap_pool(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(unmap_pool); i++) {
		struct dmaengine_unmap_pool *p = &unmap_pool[i];

		mempool_destroy(p->pool);
		p->pool = NULL;
		kmem_cache_destroy(p->cache);
		p->cache = NULL;
	}
}

static int __init dmaengine_init_unmap_pool(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(unmap_pool); i++) {
		struct dmaengine_unmap_pool *p = &unmap_pool[i];
		size_t size;

		size = sizeof(struct dmaengine_unmap_data) +
		       sizeof(dma_addr_t) * p->size;

		p->cache = kmem_cache_create(p->name, size, 0,
					     SLAB_HWCACHE_ALIGN, NULL);
		if (!p->cache)
			break;
		p->pool = mempool_create_slab_pool(1, p->cache);
		if (!p->pool)
			break;
	}

	if (i == ARRAY_SIZE(unmap_pool))
		return 0;

	dmaengine_destroy_unmap_pool();
	return -ENOMEM;
}

struct dmaengine_unmap_data *
dmaengine_get_unmap_data(struct device *dev, int nr, gfp_t flags)
{
	struct dmaengine_unmap_data *unmap;

	unmap = mempool_alloc(__get_unmap_pool(nr)->pool, flags);
	if (!unmap)
		return NULL;

	memset(unmap, 0, sizeof(*unmap));
	kref_init(&unmap->kref);
	unmap->dev = dev;
	unmap->map_cnt = nr;

	return unmap;
}
EXPORT_SYMBOL(dmaengine_get_unmap_data);

void dma_async_tx_descriptor_init(struct dma_async_tx_descriptor *tx,
	struct dma_chan *chan)
{
	tx->chan = chan;
	spin_lock_init(&tx->lock);
}
EXPORT_SYMBOL(dma_async_tx_descriptor_init);

static int __init dma_bus_init(void)
{
	mutex_init(&dma_list_mutex);
	return class_register(&dma_devclass);
}
subsys_initcall(dma_bus_init);
	#ifdef CONFIG_ASYNC_TX_ENABLE_CHANNEL_SWITCH
	spin_lock_init(&tx->lock);
	#endif
}
EXPORT_SYMBOL(dma_async_tx_descriptor_init);

/* dma_wait_for_async_tx - spin wait for a transaction to complete
 * @tx: in-flight transaction to wait on
 */
enum dma_status
dma_wait_for_async_tx(struct dma_async_tx_descriptor *tx)
{
	unsigned long dma_sync_wait_timeout = jiffies + msecs_to_jiffies(5000);

	if (!tx)
		return DMA_COMPLETE;

	while (tx->cookie == -EBUSY) {
		if (time_after_eq(jiffies, dma_sync_wait_timeout)) {
			pr_err("%s timeout waiting for descriptor submission\n",
			       __func__);
			return DMA_ERROR;
		}
		cpu_relax();
	}
	return dma_sync_wait(tx->chan, tx->cookie);
}
EXPORT_SYMBOL_GPL(dma_wait_for_async_tx);

/* dma_run_dependencies - helper routine for dma drivers to process
 *	(start) dependent operations on their target channel
 * @tx: transaction with dependencies
 */
void dma_run_dependencies(struct dma_async_tx_descriptor *tx)
{
	struct dma_async_tx_descriptor *dep = txd_next(tx);
	struct dma_async_tx_descriptor *dep_next;
	struct dma_chan *chan;

	if (!dep)
		return;

	/* we'll submit tx->next now, so clear the link */
	txd_clear_next(tx);
	chan = dep->chan;

	/* keep submitting up until a channel switch is detected
	 * in that case we will be called again as a result of
	 * processing the interrupt from async_tx_channel_switch
	 */
	for (; dep; dep = dep_next) {
		txd_lock(dep);
		txd_clear_parent(dep);
		dep_next = txd_next(dep);
		if (dep_next && dep_next->chan == chan)
			txd_clear_next(dep); /* ->next will be submitted */
		else
			dep_next = NULL; /* submit current dep and terminate */
		txd_unlock(dep);

		dep->tx_submit(dep);
	}

	chan->device->device_issue_pending(chan);
}
EXPORT_SYMBOL_GPL(dma_run_dependencies);

static int __init dma_bus_init(void)
{
	int err = dmaengine_init_unmap_pool();

	if (err)
		return err;
	return class_register(&dma_devclass);
}
arch_initcall(dma_bus_init);


