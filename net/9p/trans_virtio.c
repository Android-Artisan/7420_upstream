/*
 * The Guest 9p transport driver
 * The Virtio 9p transport driver
 *
 * This is a block based transport driver based on the lguest block driver
 * code.
 *
 */
/*
 *  Copyright (C) 2007 Eric Van Hensbergen, IBM Corporation
 *  Copyright (C) 2007, 2008 Eric Van Hensbergen, IBM Corporation
 *
 *  Based on virtio console driver
 *  Copyright (C) 2006, 2007 Rusty Russell, IBM Corporation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to:
 *  Free Software Foundation
 *  51 Franklin Street, Fifth Floor
 *  Boston, MA  02111-1301  USA
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/in.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/ipv6.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/un.h>
#include <linux/uaccess.h>
#include <linux/inet.h>
#include <linux/idr.h>
#include <linux/file.h>
#include <net/9p/9p.h>
#include <linux/parser.h>
#include <net/9p/transport.h>
#include <linux/scatterlist.h>
#include <linux/virtio.h>
#include <linux/virtio_9p.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <net/9p/9p.h>
#include <linux/parser.h>
#include <net/9p/client.h>
#include <net/9p/transport.h>
#include <linux/scatterlist.h>
#include <linux/swap.h>
#include <linux/virtio.h>
#include <linux/virtio_9p.h>
#include "trans_common.h"

#define VIRTQUEUE_NUM	128

/* a single mutex to manage channel initialization and attachment */
static DEFINE_MUTEX(virtio_9p_lock);
/* global which tracks highest initialized channel */
static int chan_index;

#define P9_INIT_MAXTAG	16


/**
 * enum p9_req_status_t - virtio request status
 * @REQ_STATUS_IDLE: request slot unused
 * @REQ_STATUS_SENT: request sent to server
 * @REQ_STATUS_RCVD: response received from server
 * @REQ_STATUS_FLSH: request has been flushed
 *
 * The @REQ_STATUS_IDLE state is used to mark a request slot as unused
 * but use is actually tracked by the idpool structure which handles tag
 * id allocation.
 *
 */

enum p9_req_status_t {
	REQ_STATUS_IDLE,
	REQ_STATUS_SENT,
	REQ_STATUS_RCVD,
	REQ_STATUS_FLSH,
};

/**
 * struct p9_req_t - virtio request slots
 * @status: status of this request slot
 * @wq: wait_queue for the client to block on for this request
 *
 * The virtio transport uses an array to track outstanding requests
 * instead of a list.  While this may incurr overhead during initial
 * allocation or expansion, it makes request lookup much easier as the
 * tag id is a index into an array.  (We use tag+1 so that we can accomodate
 * the -1 tag for the T_VERSION request).
 * This also has the nice effect of only having to allocate wait_queues
 * once, instead of constantly allocating and freeing them.  Its possible
 * other resources could benefit from this scheme as well.
 *
 */

struct p9_req_t {
	int status;
	wait_queue_head_t *wq;
};
static DECLARE_WAIT_QUEUE_HEAD(vp_wq);
static atomic_t vp_pinned = ATOMIC_INIT(0);

/**
 * struct virtio_chan - per-instance transport information
 * @initialized: whether the channel is initialized
 * @inuse: whether the channel is in use
 * @lock: protects multiple elements within this structure
 * @vdev: virtio dev associated with this channel
 * @vq: virtio queue associated with this channel
 * @tagpool: accounting for tag ids (and request slots)
 * @reqs: array of request slots
 * @max_tag: current number of request_slots allocated
 * @client: client instance
 * @vdev: virtio dev associated with this channel
 * @vq: virtio queue associated with this channel
 * @sg: scatter gather list which is used to pack a request (protected?)
 *
 * We keep all per-channel information in a structure.
 * This structure is allocated within the devices dev->mem space.
 * A pointer to the structure will get put in the transport private.
 *
 */

static struct virtio_chan {
	bool initialized;
struct virtio_chan {
	bool inuse;

	spinlock_t lock;

	struct virtio_device *vdev;
	struct virtqueue *vq;

	struct p9_idpool *tagpool;
	struct p9_req_t *reqs;
	int max_tag;

	/* Scatterlist: can be too big for stack. */
	struct scatterlist sg[VIRTQUEUE_NUM];
} channels[MAX_9P_CHAN];

/**
 * p9_lookup_tag - Lookup requests by tag
 * @c: virtio channel to lookup tag within
 * @tag: numeric id for transaction
 *
 * this is a simple array lookup, but will grow the
 * request_slots as necessary to accomodate transaction
 * ids which did not previously have a slot.
 *
 * Bugs: there is currently no upper limit on request slots set
 * here, but that should be constrained by the id accounting.
 */

static struct p9_req_t *p9_lookup_tag(struct virtio_chan *c, u16 tag)
{
	/* This looks up the original request by tag so we know which
	 * buffer to read the data into */
	tag++;

	while (tag >= c->max_tag) {
		int old_max = c->max_tag;
		int count;

		if (c->max_tag)
			c->max_tag *= 2;
		else
			c->max_tag = P9_INIT_MAXTAG;

		c->reqs = krealloc(c->reqs, sizeof(struct p9_req_t)*c->max_tag,
								GFP_ATOMIC);
		if (!c->reqs) {
			printk(KERN_ERR "Couldn't grow tag array\n");
			BUG();
		}
		for (count = old_max; count < c->max_tag; count++) {
			c->reqs[count].status = REQ_STATUS_IDLE;
			c->reqs[count].wq = kmalloc(sizeof(wait_queue_head_t),
								GFP_ATOMIC);
			if (!c->reqs[count].wq) {
				printk(KERN_ERR "Couldn't grow tag array\n");
				BUG();
			}
			init_waitqueue_head(c->reqs[count].wq);
		}
	}

	return &c->reqs[tag];
}

	struct p9_client *client;
	struct virtio_device *vdev;
	struct virtqueue *vq;
	int ring_bufs_avail;
	wait_queue_head_t *vc_wq;
	/* This is global limit. Since we don't have a global structure,
	 * will be placing it in each channel.
	 */
	unsigned long p9_max_pages;
	/* Scatterlist: can be too big for stack. */
	struct scatterlist sg[VIRTQUEUE_NUM];

	int tag_len;
	/*
	 * tag name to identify a mount Non-null terminated
	 */
	char *tag;

	struct list_head chan_list;
};

static struct list_head virtio_chan_list;

/* How many bytes left in this page. */
static unsigned int rest_of_page(void *data)
{
	return PAGE_SIZE - ((unsigned long)data % PAGE_SIZE);
}

/**
 * p9_virtio_close - reclaim resources of a channel
 * @trans: transport state
 * @client: client instance
 *
 * This reclaims a channel by freeing its resources and
 * reseting its inuse flag.
 *
 */

static void p9_virtio_close(struct p9_trans *trans)
{
	struct virtio_chan *chan = trans->priv;
	int count;
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);
	p9_idpool_destroy(chan->tagpool);
	for (count = 0; count < chan->max_tag; count++)
		kfree(chan->reqs[count].wq);
	kfree(chan->reqs);
	chan->max_tag = 0;
	spin_unlock_irqrestore(&chan->lock, flags);

	mutex_lock(&virtio_9p_lock);
	chan->inuse = false;
	mutex_unlock(&virtio_9p_lock);

	kfree(trans);
static void p9_virtio_close(struct p9_client *client)
{
	struct virtio_chan *chan = client->trans;

	mutex_lock(&virtio_9p_lock);
	if (chan)
		chan->inuse = false;
	mutex_unlock(&virtio_9p_lock);
}

/**
 * req_done - callback which signals activity from the server
 * @vq: virtio queue activity was received on
 *
 * This notifies us that the server has triggered some activity
 * on the virtio channel - most likely a response to request we
 * sent.  Figure out which requests now have responses and wake up
 * those threads.
 *
 * Bugs: could do with some additional sanity checking, but appears to work.
 *
 */

static void req_done(struct virtqueue *vq)
{
	struct virtio_chan *chan = vq->vdev->priv;
	struct p9_fcall *rc;
	unsigned int len;
	unsigned long flags;
	struct p9_req_t *req;

	spin_lock_irqsave(&chan->lock, flags);
	while ((rc = chan->vq->vq_ops->get_buf(chan->vq, &len)) != NULL) {
		req = p9_lookup_tag(chan, rc->tag);
		req->status = REQ_STATUS_RCVD;
		wake_up(req->wq);
	}
	/* In case queue is stopped waiting for more buffers. */
	spin_unlock_irqrestore(&chan->lock, flags);
	struct p9_req_t *req;
	unsigned long flags;

	p9_debug(P9_DEBUG_TRANS, ": request done\n");

	while (1) {
		spin_lock_irqsave(&chan->lock, flags);
		rc = virtqueue_get_buf(chan->vq, &len);
		if (rc == NULL) {
			spin_unlock_irqrestore(&chan->lock, flags);
			break;
		}
		chan->ring_bufs_avail = 1;
		spin_unlock_irqrestore(&chan->lock, flags);
		/* Wakeup if anyone waiting for VirtIO ring space. */
		wake_up(chan->vc_wq);
		p9_debug(P9_DEBUG_TRANS, ": rc %p\n", rc);
		p9_debug(P9_DEBUG_TRANS, ": lookup tag %d\n", rc->tag);
		req = p9_tag_lookup(chan->client, rc->tag);
		p9_client_cb(chan->client, req, REQ_STATUS_RCVD);
	}
}

/**
 * pack_sg_list - pack a scatter gather list from a linear buffer
 * @sg: scatter/gather list to pack into
 * @start: which segment of the sg_list to start at
 * @limit: maximum segment to pack data to
 * @data: data to pack into scatter/gather list
 * @count: amount of data to pack into the scatter/gather list
 *
 * sg_lists have multiple segments of various sizes.  This will pack
 * arbitrary data into an existing scatter gather list, segmenting the
 * data as necessary within constraints.
 *
 */

static int
pack_sg_list(struct scatterlist *sg, int start, int limit, char *data,
								int count)
static int pack_sg_list(struct scatterlist *sg, int start,
			int limit, char *data, int count)
{
	int s;
	int index = start;

	while (count) {
		s = rest_of_page(data);
		if (s > count)
			s = count;
		sg_set_buf(&sg[index++], data, s);
		count -= s;
		data += s;
		BUG_ON(index > limit);
	}

	return index-start;
}

/**
 * p9_virtio_rpc - issue a request and wait for a response
 * @t: transport state
 * @tc: &p9_fcall request to transmit
 * @rc: &p9_fcall to put reponse into
		BUG_ON(index > limit);
		BUG_ON(index >= limit);
		/* Make sure we don't terminate early. */
		sg_unmark_end(&sg[index]);
		sg_set_buf(&sg[index++], data, s);
		count -= s;
		data += s;
	}
	if (index-start)
		sg_mark_end(&sg[index - 1]);
	return index-start;
}

/* We don't currently allow canceling of virtio requests */
static int p9_virtio_cancel(struct p9_client *client, struct p9_req_t *req)
{
	return 1;
}

/**
 * pack_sg_list_p - Just like pack_sg_list. Instead of taking a buffer,
 * this takes a list of pages.
 * @sg: scatter/gather list to pack into
 * @start: which segment of the sg_list to start at
 * @pdata: a list of pages to add into sg.
 * @nr_pages: number of pages to pack into the scatter/gather list
 * @offs: amount of data in the beginning of first page _not_ to pack
 * @count: amount of data to pack into the scatter/gather list
 */
static int
pack_sg_list_p(struct scatterlist *sg, int start, int limit,
	       struct page **pdata, int nr_pages, size_t offs, int count)
{
	int i = 0, s;
	int data_off = offs;
	int index = start;

	BUG_ON(nr_pages > (limit - start));
	/*
	 * if the first page doesn't start at
	 * page boundary find the offset
	 */
	while (nr_pages) {
		s = PAGE_SIZE - data_off;
		if (s > count)
			s = count;
		BUG_ON(index >= limit);
		/* Make sure we don't terminate early. */
		sg_unmark_end(&sg[index]);
		sg_set_page(&sg[index++], pdata[i++], s, data_off);
		data_off = 0;
		count -= s;
		nr_pages--;
	}

	if (index-start)
		sg_mark_end(&sg[index - 1]);
	return index - start;
}

/**
 * p9_virtio_request - issue a request
 * @client: client instance issuing the request
 * @req: request to be issued
 *
 */

static int
p9_virtio_rpc(struct p9_trans *t, struct p9_fcall *tc, struct p9_fcall **rc)
{
	int in, out;
	int n, err, size;
	struct virtio_chan *chan = t->priv;
	char *rdata;
	struct p9_req_t *req;
	unsigned long flags;

	if (*rc == NULL) {
		*rc = kmalloc(sizeof(struct p9_fcall) + t->msize, GFP_KERNEL);
		if (!*rc)
			return -ENOMEM;
	}

	rdata = (char *)*rc+sizeof(struct p9_fcall);

	n = P9_NOTAG;
	if (tc->id != P9_TVERSION) {
		n = p9_idpool_get(chan->tagpool);
		if (n < 0)
			return -ENOMEM;
	}

	spin_lock_irqsave(&chan->lock, flags);
	req = p9_lookup_tag(chan, n);
	spin_unlock_irqrestore(&chan->lock, flags);

	p9_set_tag(tc, n);

	P9_DPRINTK(P9_DEBUG_TRANS, "9p debug: virtio rpc tag %d\n", n);

	out = pack_sg_list(chan->sg, 0, VIRTQUEUE_NUM, tc->sdata, tc->size);
	in = pack_sg_list(chan->sg, out, VIRTQUEUE_NUM-out, rdata, t->msize);

	req->status = REQ_STATUS_SENT;

	if (chan->vq->vq_ops->add_buf(chan->vq, chan->sg, out, in, tc)) {
		P9_DPRINTK(P9_DEBUG_TRANS,
			"9p debug: virtio rpc add_buf returned failure");
		return -EIO;
	}

	chan->vq->vq_ops->kick(chan->vq);

	wait_event(*req->wq, req->status == REQ_STATUS_RCVD);

	size = le32_to_cpu(*(__le32 *) rdata);

	err = p9_deserialize_fcall(rdata, size, *rc, t->extended);
	if (err < 0) {
		P9_DPRINTK(P9_DEBUG_TRANS,
			"9p debug: virtio rpc deserialize returned %d\n", err);
		return err;
	}

#ifdef CONFIG_NET_9P_DEBUG
	if ((p9_debug_level&P9_DEBUG_FCALL) == P9_DEBUG_FCALL) {
		char buf[150];

		p9_printfcall(buf, sizeof(buf), *rc, t->extended);
		printk(KERN_NOTICE ">>> %p %s\n", t, buf);
	}
#endif

	if (n != P9_NOTAG && p9_idpool_check(n, chan->tagpool))
		p9_idpool_put(n, chan->tagpool);

	req->status = REQ_STATUS_IDLE;

	return 0;
}

p9_virtio_request(struct p9_client *client, struct p9_req_t *req)
{
	int err;
	int in, out, out_sgs, in_sgs;
	unsigned long flags;
	struct virtio_chan *chan = client->trans;
	struct scatterlist *sgs[2];

	p9_debug(P9_DEBUG_TRANS, "9p debug: virtio request\n");

	req->status = REQ_STATUS_SENT;
req_retry:
	spin_lock_irqsave(&chan->lock, flags);

	out_sgs = in_sgs = 0;
	/* Handle out VirtIO ring buffers */
	out = pack_sg_list(chan->sg, 0,
			   VIRTQUEUE_NUM, req->tc->sdata, req->tc->size);
	if (out)
		sgs[out_sgs++] = chan->sg;

	in = pack_sg_list(chan->sg, out,
			  VIRTQUEUE_NUM, req->rc->sdata, req->rc->capacity);
	if (in)
		sgs[out_sgs + in_sgs++] = chan->sg + out;

	err = virtqueue_add_sgs(chan->vq, sgs, out_sgs, in_sgs, req->tc,
				GFP_ATOMIC);
	if (err < 0) {
		if (err == -ENOSPC) {
			chan->ring_bufs_avail = 0;
			spin_unlock_irqrestore(&chan->lock, flags);
			err = wait_event_killable(*chan->vc_wq,
						  chan->ring_bufs_avail);
			if (err  == -ERESTARTSYS)
				return err;

			p9_debug(P9_DEBUG_TRANS, "Retry virtio request\n");
			goto req_retry;
		} else {
			spin_unlock_irqrestore(&chan->lock, flags);
			p9_debug(P9_DEBUG_TRANS,
				 "virtio rpc add_sgs returned failure\n");
			return -EIO;
		}
	}
	virtqueue_kick(chan->vq);
	spin_unlock_irqrestore(&chan->lock, flags);

	p9_debug(P9_DEBUG_TRANS, "virtio request kicked\n");
	return 0;
}

static int p9_get_mapped_pages(struct virtio_chan *chan,
			       struct page ***pages,
			       struct iov_iter *data,
			       int count,
			       size_t *offs,
			       int *need_drop)
{
	int nr_pages;
	int err;

	if (!iov_iter_count(data))
		return 0;

	if (!(data->type & ITER_KVEC)) {
		int n;
		/*
		 * We allow only p9_max_pages pinned. We wait for the
		 * Other zc request to finish here
		 */
		if (atomic_read(&vp_pinned) >= chan->p9_max_pages) {
			err = wait_event_killable(vp_wq,
			      (atomic_read(&vp_pinned) < chan->p9_max_pages));
			if (err == -ERESTARTSYS)
				return err;
		}
		n = iov_iter_get_pages_alloc(data, pages, count, offs);
		if (n < 0)
			return n;
		*need_drop = 1;
		nr_pages = DIV_ROUND_UP(n + *offs, PAGE_SIZE);
		atomic_add(nr_pages, &vp_pinned);
		return n;
	} else {
		/* kernel buffer, no need to pin pages */
		int index;
		size_t len;
		void *p;

		/* we'd already checked that it's non-empty */
		while (1) {
			len = iov_iter_single_seg_count(data);
			if (likely(len)) {
				p = data->kvec->iov_base + data->iov_offset;
				break;
			}
			iov_iter_advance(data, 0);
		}
		if (len > count)
			len = count;

		nr_pages = DIV_ROUND_UP((unsigned long)p + len, PAGE_SIZE) -
			   (unsigned long)p / PAGE_SIZE;

		*pages = kmalloc(sizeof(struct page *) * nr_pages, GFP_NOFS);
		if (!*pages)
			return -ENOMEM;

		*need_drop = 0;
		p -= (*offs = (unsigned long)p % PAGE_SIZE);
		for (index = 0; index < nr_pages; index++) {
			if (is_vmalloc_addr(p))
				(*pages)[index] = vmalloc_to_page(p);
			else
				(*pages)[index] = kmap_to_page(p);
			p += PAGE_SIZE;
		}
		return len;
	}
}

/**
 * p9_virtio_zc_request - issue a zero copy request
 * @client: client instance issuing the request
 * @req: request to be issued
 * @uidata: user bffer that should be ued for zero copy read
 * @uodata: user buffer that shoud be user for zero copy write
 * @inlen: read buffer size
 * @olen: write buffer size
 * @hdrlen: reader header size, This is the size of response protocol data
 *
 */
static int
p9_virtio_zc_request(struct p9_client *client, struct p9_req_t *req,
		     struct iov_iter *uidata, struct iov_iter *uodata,
		     int inlen, int outlen, int in_hdr_len)
{
	int in, out, err, out_sgs, in_sgs;
	unsigned long flags;
	int in_nr_pages = 0, out_nr_pages = 0;
	struct page **in_pages = NULL, **out_pages = NULL;
	struct virtio_chan *chan = client->trans;
	struct scatterlist *sgs[4];
	size_t offs = 0;
	int need_drop = 0;

	p9_debug(P9_DEBUG_TRANS, "virtio request\n");

	if (uodata) {
		__le32 sz;
		int n = p9_get_mapped_pages(chan, &out_pages, uodata,
					    outlen, &offs, &need_drop);
		if (n < 0)
			return n;
		out_nr_pages = DIV_ROUND_UP(n + offs, PAGE_SIZE);
		if (n != outlen) {
			__le32 v = cpu_to_le32(n);
			memcpy(&req->tc->sdata[req->tc->size - 4], &v, 4);
			outlen = n;
		}
		/* The size field of the message must include the length of the
		 * header and the length of the data.  We didn't actually know
		 * the length of the data until this point so add it in now.
		 */
		sz = cpu_to_le32(req->tc->size + outlen);
		memcpy(&req->tc->sdata[0], &sz, sizeof(sz));
	} else if (uidata) {
		int n = p9_get_mapped_pages(chan, &in_pages, uidata,
					    inlen, &offs, &need_drop);
		if (n < 0)
			return n;
		in_nr_pages = DIV_ROUND_UP(n + offs, PAGE_SIZE);
		if (n != inlen) {
			__le32 v = cpu_to_le32(n);
			memcpy(&req->tc->sdata[req->tc->size - 4], &v, 4);
			inlen = n;
		}
	}
	req->status = REQ_STATUS_SENT;
req_retry_pinned:
	spin_lock_irqsave(&chan->lock, flags);

	out_sgs = in_sgs = 0;

	/* out data */
	out = pack_sg_list(chan->sg, 0,
			   VIRTQUEUE_NUM, req->tc->sdata, req->tc->size);

	if (out)
		sgs[out_sgs++] = chan->sg;

	if (out_pages) {
		sgs[out_sgs++] = chan->sg + out;
		out += pack_sg_list_p(chan->sg, out, VIRTQUEUE_NUM,
				      out_pages, out_nr_pages, offs, outlen);
	}
		
	/*
	 * Take care of in data
	 * For example TREAD have 11.
	 * 11 is the read/write header = PDU Header(7) + IO Size (4).
	 * Arrange in such a way that server places header in the
	 * alloced memory and payload onto the user buffer.
	 */
	in = pack_sg_list(chan->sg, out,
			  VIRTQUEUE_NUM, req->rc->sdata, in_hdr_len);
	if (in)
		sgs[out_sgs + in_sgs++] = chan->sg + out;

	if (in_pages) {
		sgs[out_sgs + in_sgs++] = chan->sg + out + in;
		in += pack_sg_list_p(chan->sg, out + in, VIRTQUEUE_NUM,
				     in_pages, in_nr_pages, offs, inlen);
	}

	BUG_ON(out_sgs + in_sgs > ARRAY_SIZE(sgs));
	err = virtqueue_add_sgs(chan->vq, sgs, out_sgs, in_sgs, req->tc,
				GFP_ATOMIC);
	if (err < 0) {
		if (err == -ENOSPC) {
			chan->ring_bufs_avail = 0;
			spin_unlock_irqrestore(&chan->lock, flags);
			err = wait_event_killable(*chan->vc_wq,
						  chan->ring_bufs_avail);
			if (err  == -ERESTARTSYS)
				goto err_out;

			p9_debug(P9_DEBUG_TRANS, "Retry virtio request\n");
			goto req_retry_pinned;
		} else {
			spin_unlock_irqrestore(&chan->lock, flags);
			p9_debug(P9_DEBUG_TRANS,
				 "virtio rpc add_sgs returned failure\n");
			err = -EIO;
			goto err_out;
		}
	}
	virtqueue_kick(chan->vq);
	spin_unlock_irqrestore(&chan->lock, flags);
	p9_debug(P9_DEBUG_TRANS, "virtio request kicked\n");
	err = wait_event_killable(*req->wq, req->status >= REQ_STATUS_RCVD);
	/*
	 * Non kernel buffers are pinned, unpin them
	 */
err_out:
	if (need_drop) {
		if (in_pages) {
			p9_release_pages(in_pages, in_nr_pages);
			atomic_sub(in_nr_pages, &vp_pinned);
		}
		if (out_pages) {
			p9_release_pages(out_pages, out_nr_pages);
			atomic_sub(out_nr_pages, &vp_pinned);
		}
		/* wakeup anybody waiting for slots to pin pages */
		wake_up(&vp_wq);
	}
	kfree(in_pages);
	kfree(out_pages);
	return err;
}

static ssize_t p9_mount_tag_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct virtio_chan *chan;
	struct virtio_device *vdev;

	vdev = dev_to_virtio(dev);
	chan = vdev->priv;

	memcpy(buf, chan->tag, chan->tag_len);
	buf[chan->tag_len] = 0;

	return chan->tag_len + 1;
}

static DEVICE_ATTR(mount_tag, 0444, p9_mount_tag_show, NULL);

/**
 * p9_virtio_probe - probe for existence of 9P virtio channels
 * @vdev: virtio device to probe
 *
 * This probes for existing virtio channels.  At present only
 * a single channel is in use, so in the future more work may need
 * to be done here.
 * This probes for existing virtio channels.
 *
 */

static int p9_virtio_probe(struct virtio_device *vdev)
{
	int err;
	struct virtio_chan *chan;
	int index;

	mutex_lock(&virtio_9p_lock);
	index = chan_index++;
	chan = &channels[index];
	mutex_unlock(&virtio_9p_lock);

	if (chan_index > MAX_9P_CHAN) {
		printk(KERN_ERR "9p: virtio: Maximum channels exceeded\n");
		BUG();
	__u16 tag_len;
	char *tag;
	int err;
	struct virtio_chan *chan;

	if (!vdev->config->get) {
		dev_err(&vdev->dev, "%s failure: config access disabled\n",
			__func__);
		return -EINVAL;
	}

	chan = kmalloc(sizeof(struct virtio_chan), GFP_KERNEL);
	if (!chan) {
		pr_err("Failed to allocate virtio 9P channel\n");
		err = -ENOMEM;
		goto fail;
	}

	chan->vdev = vdev;

	/* We expect one virtqueue, for requests. */
	chan->vq = vdev->config->find_vq(vdev, 0, req_done);
	chan->vq = virtio_find_single_vq(vdev, req_done, "requests");
	if (IS_ERR(chan->vq)) {
		err = PTR_ERR(chan->vq);
		goto out_free_chan;
	}
	chan->vq->vdev->priv = chan;
	spin_lock_init(&chan->lock);

	sg_init_table(chan->sg, VIRTQUEUE_NUM);

	chan->inuse = false;
	chan->initialized = true;
	return 0;

out_free_vq:
	vdev->config->del_vq(chan->vq);
fail:
	mutex_lock(&virtio_9p_lock);
	chan_index--;
	mutex_unlock(&virtio_9p_lock);
	if (virtio_has_feature(vdev, VIRTIO_9P_MOUNT_TAG)) {
		virtio_cread(vdev, struct virtio_9p_config, tag_len, &tag_len);
	} else {
		err = -EINVAL;
		goto out_free_vq;
	}
	tag = kmalloc(tag_len, GFP_KERNEL);
	if (!tag) {
		err = -ENOMEM;
		goto out_free_vq;
	}

	virtio_cread_bytes(vdev, offsetof(struct virtio_9p_config, tag),
			   tag, tag_len);
	chan->tag = tag;
	chan->tag_len = tag_len;
	err = sysfs_create_file(&(vdev->dev.kobj), &dev_attr_mount_tag.attr);
	if (err) {
		goto out_free_tag;
	}
	chan->vc_wq = kmalloc(sizeof(wait_queue_head_t), GFP_KERNEL);
	if (!chan->vc_wq) {
		err = -ENOMEM;
		goto out_remove_file;
	}
	init_waitqueue_head(chan->vc_wq);
	chan->ring_bufs_avail = 1;
	/* Ceiling limit to avoid denial of service attacks */
	chan->p9_max_pages = nr_free_buffer_pages()/4;

	virtio_device_ready(vdev);

	mutex_lock(&virtio_9p_lock);
	list_add_tail(&chan->chan_list, &virtio_chan_list);
	mutex_unlock(&virtio_9p_lock);

	/* Let udev rules use the new mount_tag attribute. */
	kobject_uevent(&(vdev->dev.kobj), KOBJ_CHANGE);

	return 0;

out_remove_file:
	sysfs_remove_file(&vdev->dev.kobj, &dev_attr_mount_tag.attr);
out_free_tag:
	kfree(tag);
out_free_vq:
	vdev->config->del_vqs(vdev);
out_free_chan:
	kfree(chan);
fail:
	return err;
}


/**
 * p9_virtio_create - allocate a new virtio channel
 * @devname: string identifying the channel to connect to (unused)
 * @args: args passed from sys_mount() for per-transport options (unused)
 * @msize: requested maximum packet size
 * @extended: 9p2000.u enabled flag
 * @client: client instance invoking this transport
 * @devname: string identifying the channel to connect to (unused)
 * @args: args passed from sys_mount() for per-transport options (unused)
 *
 * This sets up a transport channel for 9p communication.  Right now
 * we only match the first available channel, but eventually we couldlook up
 * alternate channels by matching devname versus a virtio_config entry.
 * We use a simple reference count mechanism to ensure that only a single
 * mount has a channel open at a time.
 *
 * Bugs: doesn't allow identification of a specific channel
 * to allocate, channels are allocated sequentially. This was
 * a pragmatic decision to get things rolling, but ideally some
 * way of identifying the channel to attach to would be nice
 * if we are going to support multiple channels.
 *
 */

static struct p9_trans *
p9_virtio_create(const char *devname, char *args, int msize,
							unsigned char extended)
{
	struct p9_trans *trans;
	struct virtio_chan *chan = channels;
	int index = 0;

	mutex_lock(&virtio_9p_lock);
	while (index < MAX_9P_CHAN) {
		if (chan->initialized && !chan->inuse) {
			chan->inuse = true;
			break;
		} else {
			index++;
			chan = &channels[index];
 */

static int
p9_virtio_create(struct p9_client *client, const char *devname, char *args)
{
	struct virtio_chan *chan;
	int ret = -ENOENT;
	int found = 0;

	if (devname == NULL)
		return -EINVAL;

	mutex_lock(&virtio_9p_lock);
	list_for_each_entry(chan, &virtio_chan_list, chan_list) {
		if (!strncmp(devname, chan->tag, chan->tag_len) &&
		    strlen(devname) == chan->tag_len) {
			if (!chan->inuse) {
				chan->inuse = true;
				found = 1;
				break;
			}
			ret = -EBUSY;
		}
	}
	mutex_unlock(&virtio_9p_lock);

	if (index >= MAX_9P_CHAN) {
		printk(KERN_ERR "9p: no channels available\n");
		return ERR_PTR(-ENODEV);
	}

	chan->tagpool = p9_idpool_create();
	if (IS_ERR(chan->tagpool)) {
		printk(KERN_ERR "9p: couldn't allocate tagpool\n");
		return ERR_PTR(-ENOMEM);
	}
	p9_idpool_get(chan->tagpool); /* reserve tag 0 */
	chan->max_tag = 0;
	chan->reqs = NULL;

	trans = kmalloc(sizeof(struct p9_trans), GFP_KERNEL);
	if (!trans) {
		printk(KERN_ERR "9p: couldn't allocate transport\n");
		return ERR_PTR(-ENOMEM);
	}
	trans->extended = extended;
	trans->msize = msize;
	trans->close = p9_virtio_close;
	trans->rpc = p9_virtio_rpc;
	trans->priv = chan;

	return trans;
	if (!found) {
		pr_err("no channels available\n");
		return ret;
	}

	client->trans = (void *)chan;
	client->status = Connected;
	chan->client = client;

	return 0;
}

/**
 * p9_virtio_remove - clean up resources associated with a virtio device
 * @vdev: virtio device to remove
 *
 */

static void p9_virtio_remove(struct virtio_device *vdev)
{
	struct virtio_chan *chan = vdev->priv;

	BUG_ON(chan->inuse);

	if (chan->initialized) {
		vdev->config->del_vq(chan->vq);
		chan->initialized = false;
	}
}

#define VIRTIO_ID_9P 9
	unsigned long warning_time;

	mutex_lock(&virtio_9p_lock);

	/* Remove self from list so we don't get new users. */
	list_del(&chan->chan_list);
	warning_time = jiffies;

	/* Wait for existing users to close. */
	while (chan->inuse) {
		mutex_unlock(&virtio_9p_lock);
		msleep(250);
		if (time_after(jiffies, warning_time + 10 * HZ)) {
			dev_emerg(&vdev->dev,
				  "p9_virtio_remove: waiting for device in use.\n");
			warning_time = jiffies;
		}
		mutex_lock(&virtio_9p_lock);
	}

	mutex_unlock(&virtio_9p_lock);

	vdev->config->reset(vdev);
	vdev->config->del_vqs(vdev);

	sysfs_remove_file(&(vdev->dev.kobj), &dev_attr_mount_tag.attr);
	kobject_uevent(&(vdev->dev.kobj), KOBJ_CHANGE);
	kfree(chan->tag);
	kfree(chan->vc_wq);
	kfree(chan);

}

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_9P, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

/* The standard "struct lguest_driver": */
static struct virtio_driver p9_virtio_drv = {
	.driver.name = 	KBUILD_MODNAME,
	.driver.owner = THIS_MODULE,
	.id_table =	id_table,
	.probe = 	p9_virtio_probe,
	.remove =	p9_virtio_remove,
static unsigned int features[] = {
	VIRTIO_9P_MOUNT_TAG,
};

/* The standard "struct lguest_driver": */
static struct virtio_driver p9_virtio_drv = {
	.feature_table  = features,
	.feature_table_size = ARRAY_SIZE(features),
	.driver.name    = KBUILD_MODNAME,
	.driver.owner	= THIS_MODULE,
	.id_table	= id_table,
	.probe		= p9_virtio_probe,
	.remove		= p9_virtio_remove,
};

static struct p9_trans_module p9_virtio_trans = {
	.name = "virtio",
	.create = p9_virtio_create,
	.maxsize = PAGE_SIZE*16,
	.def = 0,
	.close = p9_virtio_close,
	.request = p9_virtio_request,
	.zc_request = p9_virtio_zc_request,
	.cancel = p9_virtio_cancel,
	/*
	 * We leave one entry for input and one entry for response
	 * headers. We also skip one more entry to accomodate, address
	 * that are not at page boundary, that can result in an extra
	 * page in zero copy.
	 */
	.maxsize = PAGE_SIZE * (VIRTQUEUE_NUM - 3),
	.def = 1,
	.owner = THIS_MODULE,
};

/* The standard init function */
static int __init p9_virtio_init(void)
{
	int count;

	for (count = 0; count < MAX_9P_CHAN; count++)
		channels[count].initialized = false;
	int rc;

	INIT_LIST_HEAD(&virtio_chan_list);

	v9fs_register_trans(&p9_virtio_trans);
	rc = register_virtio_driver(&p9_virtio_drv);
	if (rc)
		v9fs_unregister_trans(&p9_virtio_trans);

	return rc;
}

static void __exit p9_virtio_cleanup(void)
{
	unregister_virtio_driver(&p9_virtio_drv);
	v9fs_unregister_trans(&p9_virtio_trans);
}

module_init(p9_virtio_init);
module_exit(p9_virtio_cleanup);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_AUTHOR("Eric Van Hensbergen <ericvh@gmail.com>");
MODULE_DESCRIPTION("Virtio 9p Transport");
MODULE_LICENSE("GPL");
