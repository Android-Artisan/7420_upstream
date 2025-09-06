/* i915_dma.c -- DMA support for the I915 -*- linux-c -*-
 */
/*
 * Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "drmP.h"
#include "drm.h"
#include "i915_drm.h"
#include "i915_drv.h"

/* Really want an OS-independent resettable timer.  Would like to have
 * this loop run for (eg) 3 sec, but have the timer reset every time
 * the head pointer changes, so that EBUSY only happens if the ring
 * actually stalls for (eg) 3 seconds.
 */
int i915_wait_ring(struct drm_device * dev, int n, const char *caller)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_ring_buffer_t *ring = &(dev_priv->ring);
	u32 last_head = I915_READ(LP_RING + RING_HEAD) & HEAD_ADDR;
	int i;

	for (i = 0; i < 10000; i++) {
		ring->head = I915_READ(LP_RING + RING_HEAD) & HEAD_ADDR;
		ring->space = ring->head - (ring->tail + 8);
		if (ring->space < 0)
			ring->space += ring->Size;
		if (ring->space >= n)
			return 0;

		dev_priv->sarea_priv->perf_boxes |= I915_BOX_WAIT;

		if (ring->head != last_head)
			i = 0;

		last_head = ring->head;
	}

	return -EBUSY;
}

void i915_kernel_lost_context(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_ring_buffer_t *ring = &(dev_priv->ring);

	ring->head = I915_READ(LP_RING + RING_HEAD) & HEAD_ADDR;
	ring->tail = I915_READ(LP_RING + RING_TAIL) & TAIL_ADDR;
	ring->space = ring->head - (ring->tail + 8);
	if (ring->space < 0)
		ring->space += ring->Size;

	if (ring->head == ring->tail)
		dev_priv->sarea_priv->perf_boxes |= I915_BOX_RING_EMPTY;
}

static int i915_dma_cleanup(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	/* Make sure interrupts are disabled here because the uninstall ioctl
	 * may not have been called from userspace and after dev_private
	 * is freed, it's too late.
	 */
	if (dev->irq)
		drm_irq_uninstall(dev);

	if (dev_priv->ring.virtual_start) {
		drm_core_ioremapfree(&dev_priv->ring.map, dev);
		dev_priv->ring.virtual_start = 0;
		dev_priv->ring.map.handle = 0;
		dev_priv->ring.map.size = 0;
	}

	if (dev_priv->status_page_dmah) {
		drm_pci_free(dev, dev_priv->status_page_dmah);
		dev_priv->status_page_dmah = NULL;
		/* Need to rewrite hardware status page */
		I915_WRITE(0x02080, 0x1ffff000);
	}

	if (dev_priv->status_gfx_addr) {
		dev_priv->status_gfx_addr = 0;
		drm_core_ioremapfree(&dev_priv->hws_map, dev);
		I915_WRITE(0x2080, 0x1ffff000);
	}

	return 0;
}

static int i915_initialize(struct drm_device * dev, drm_i915_init_t * init)
{
	drm_i915_private_t *dev_priv = dev->dev_private;

	dev_priv->sarea = drm_getsarea(dev);
	if (!dev_priv->sarea) {
		DRM_ERROR("can not find sarea!\n");
		i915_dma_cleanup(dev);
		return -EINVAL;
	}

	dev_priv->mmio_map = drm_core_findmap(dev, init->mmio_offset);
	if (!dev_priv->mmio_map) {
		i915_dma_cleanup(dev);
		DRM_ERROR("can not find mmio map!\n");
		return -EINVAL;
	}

	dev_priv->sarea_priv = (drm_i915_sarea_t *)
	    ((u8 *) dev_priv->sarea->handle + init->sarea_priv_offset);

	dev_priv->ring.Start = init->ring_start;
	dev_priv->ring.End = init->ring_end;
	dev_priv->ring.Size = init->ring_size;
	dev_priv->ring.tail_mask = dev_priv->ring.Size - 1;

	dev_priv->ring.map.offset = init->ring_start;
	dev_priv->ring.map.size = init->ring_size;
	dev_priv->ring.map.type = 0;
	dev_priv->ring.map.flags = 0;
	dev_priv->ring.map.mtrr = 0;

	drm_core_ioremap(&dev_priv->ring.map, dev);

	if (dev_priv->ring.map.handle == NULL) {
		i915_dma_cleanup(dev);
		DRM_ERROR("can not ioremap virtual address for"
			  " ring buffer\n");
		return -ENOMEM;
	}

	dev_priv->ring.virtual_start = dev_priv->ring.map.handle;

	dev_priv->cpp = init->cpp;
	dev_priv->back_offset = init->back_offset;
	dev_priv->front_offset = init->front_offset;
	dev_priv->current_page = 0;
	dev_priv->sarea_priv->pf_current_page = dev_priv->current_page;

	/* We are using separate values as placeholders for mechanisms for
	 * private backbuffer/depthbuffer usage.
	 */
	dev_priv->use_mi_batchbuffer_start = 0;
	if (IS_I965G(dev)) /* 965 doesn't support older method */
		dev_priv->use_mi_batchbuffer_start = 1;

	/* Allow hardware batchbuffers unless told otherwise.
	 */
	dev_priv->allow_batchbuffer = 1;

	/* Program Hardware Status Page */
	if (!I915_NEED_GFX_HWS(dev)) {
		dev_priv->status_page_dmah =
			drm_pci_alloc(dev, PAGE_SIZE, PAGE_SIZE, 0xffffffff);

		if (!dev_priv->status_page_dmah) {
			i915_dma_cleanup(dev);
			DRM_ERROR("Can not allocate hardware status page\n");
			return -ENOMEM;
		}
		dev_priv->hw_status_page = dev_priv->status_page_dmah->vaddr;
		dev_priv->dma_status_page = dev_priv->status_page_dmah->busaddr;

		memset(dev_priv->hw_status_page, 0, PAGE_SIZE);
		I915_WRITE(0x02080, dev_priv->dma_status_page);
	}
	DRM_DEBUG("Enabled hardware status page\n");
	return 0;
}

static int i915_dma_resume(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;

	DRM_DEBUG("%s\n", __func__);

	if (!dev_priv->sarea) {
		DRM_ERROR("can not find sarea!\n");
		return -EINVAL;
	}

	if (!dev_priv->mmio_map) {
		DRM_ERROR("can not find mmio map!\n");
		return -EINVAL;
	}

	if (dev_priv->ring.map.handle == NULL) {
		DRM_ERROR("can not ioremap virtual address for"
			  " ring buffer\n");
		return -ENOMEM;
	}

	/* Program Hardware Status Page */
	if (!dev_priv->hw_status_page) {
		DRM_ERROR("Can not find hardware status page\n");
		return -EINVAL;
	}
	DRM_DEBUG("hw status page @ %p\n", dev_priv->hw_status_page);

	if (dev_priv->status_gfx_addr != 0)
		I915_WRITE(0x02080, dev_priv->status_gfx_addr);
	else
		I915_WRITE(0x02080, dev_priv->dma_status_page);
	DRM_DEBUG("Enabled hardware status page\n");

	return 0;
}

static int i915_dma_init(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	drm_i915_init_t *init = data;
	int retcode = 0;

	switch (init->func) {
	case I915_INIT_DMA:
		retcode = i915_initialize(dev, init);
		break;
	case I915_CLEANUP_DMA:
		retcode = i915_dma_cleanup(dev);
		break;
	case I915_RESUME_DMA:
		retcode = i915_dma_resume(dev);
		break;
	default:
		retcode = -EINVAL;
		break;
	}

	return retcode;
}

/* Implement basically the same security restrictions as hardware does
 * for MI_BATCH_NON_SECURE.  These can be made stricter at any time.
 *
 * Most of the calculations below involve calculating the size of a
 * particular instruction.  It's important to get the size right as
 * that tells us where the next instruction to check is.  Any illegal
 * instruction detected will be given a size of zero, which is a
 * signal to abort the rest of the buffer.
 */
static int do_validate_cmd(int cmd)
{
	switch (((cmd >> 29) & 0x7)) {
	case 0x0:
		switch ((cmd >> 23) & 0x3f) {
		case 0x0:
			return 1;	/* MI_NOOP */
		case 0x4:
			return 1;	/* MI_FLUSH */
		default:
			return 0;	/* disallow everything else */
		}
		break;
	case 0x1:
		return 0;	/* reserved */
	case 0x2:
		return (cmd & 0xff) + 2;	/* 2d commands */
	case 0x3:
		if (((cmd >> 24) & 0x1f) <= 0x18)
			return 1;

		switch ((cmd >> 24) & 0x1f) {
		case 0x1c:
			return 1;
		case 0x1d:
			switch ((cmd >> 16) & 0xff) {
			case 0x3:
				return (cmd & 0x1f) + 2;
			case 0x4:
				return (cmd & 0xf) + 2;
			default:
				return (cmd & 0xffff) + 2;
			}
		case 0x1e:
			if (cmd & (1 << 23))
				return (cmd & 0xffff) + 1;
			else
				return 1;
		case 0x1f:
			if ((cmd & (1 << 23)) == 0)	/* inline vertices */
				return (cmd & 0x1ffff) + 2;
			else if (cmd & (1 << 17))	/* indirect random */
				if ((cmd & 0xffff) == 0)
					return 0;	/* unknown length, too hard */
				else
					return (((cmd & 0xffff) + 1) / 2) + 1;
			else
				return 2;	/* indirect sequential */
		default:
			return 0;
		}
	default:
		return 0;
	}

	return 0;
}

static int validate_cmd(int cmd)
{
	int ret = do_validate_cmd(cmd);

/*	printk("validate_cmd( %x ): %d\n", cmd, ret); */

	return ret;
}

static int i915_emit_cmds(struct drm_device * dev, int __user * buffer, int dwords)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int i;
	RING_LOCALS;

	if ((dwords+1) * sizeof(int) >= dev_priv->ring.Size - 8)
		return -EINVAL;

	BEGIN_LP_RING((dwords+1)&~1);

	for (i = 0; i < dwords;) {
		int cmd, sz;

		if (DRM_COPY_FROM_USER_UNCHECKED(&cmd, &buffer[i], sizeof(cmd)))
			return -EINVAL;

		if ((sz = validate_cmd(cmd)) == 0 || i + sz > dwords)
			return -EINVAL;

		OUT_RING(cmd);

		while (++i, --sz) {
			if (DRM_COPY_FROM_USER_UNCHECKED(&cmd, &buffer[i],
							 sizeof(cmd))) {
				return -EINVAL;
			}
			OUT_RING(cmd);
		}
	}

	if (dwords & 1)
		OUT_RING(0);

	ADVANCE_LP_RING();

	return 0;
}

static int i915_emit_box(struct drm_device * dev,
			 struct drm_clip_rect __user * boxes,
			 int i, int DR1, int DR4)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_clip_rect box;
	RING_LOCALS;

	if (DRM_COPY_FROM_USER_UNCHECKED(&box, &boxes[i], sizeof(box))) {
		return -EFAULT;
	}

	if (box.y2 <= box.y1 || box.x2 <= box.x1 || box.y2 <= 0 || box.x2 <= 0) {
		DRM_ERROR("Bad box %d,%d..%d,%d\n",
			  box.x1, box.y1, box.x2, box.y2);
		return -EINVAL;
	}

	if (IS_I965G(dev)) {
		BEGIN_LP_RING(4);
		OUT_RING(GFX_OP_DRAWRECT_INFO_I965);
		OUT_RING((box.x1 & 0xffff) | (box.y1 << 16));
		OUT_RING(((box.x2 - 1) & 0xffff) | ((box.y2 - 1) << 16));
		OUT_RING(DR4);
		ADVANCE_LP_RING();
	} else {
		BEGIN_LP_RING(6);
		OUT_RING(GFX_OP_DRAWRECT_INFO);
		OUT_RING(DR1);
		OUT_RING((box.x1 & 0xffff) | (box.y1 << 16));
		OUT_RING(((box.x2 - 1) & 0xffff) | ((box.y2 - 1) << 16));
		OUT_RING(DR4);
		OUT_RING(0);
		ADVANCE_LP_RING();
	}

	return 0;
}

/* XXX: Emitting the counter should really be moved to part of the IRQ
 * emit. For now, do it in both places:
 */

static void i915_emit_breadcrumb(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	RING_LOCALS;

	dev_priv->sarea_priv->last_enqueue = ++dev_priv->counter;

	if (dev_priv->counter > 0x7FFFFFFFUL)
		dev_priv->sarea_priv->last_enqueue = dev_priv->counter = 1;

	BEGIN_LP_RING(4);
	OUT_RING(CMD_STORE_DWORD_IDX);
	OUT_RING(20);
	OUT_RING(dev_priv->counter);
	OUT_RING(0);
	ADVANCE_LP_RING();
}

static int i915_dispatch_cmdbuffer(struct drm_device * dev,
				   drm_i915_cmdbuffer_t * cmd)
{
	int nbox = cmd->num_cliprects;
	int i = 0, count, ret;

	if (cmd->sz & 0x3) {
		DRM_ERROR("alignment");
		return -EINVAL;
	}

	i915_kernel_lost_context(dev);

	count = nbox ? nbox : 1;

	for (i = 0; i < count; i++) {
		if (i < nbox) {
			ret = i915_emit_box(dev, cmd->cliprects, i,
					    cmd->DR1, cmd->DR4);
			if (ret)
				return ret;
		}

		ret = i915_emit_cmds(dev, (int __user *)cmd->buf, cmd->sz / 4);
		if (ret)
			return ret;
	}

	i915_emit_breadcrumb(dev);
	return 0;
}

static int i915_dispatch_batchbuffer(struct drm_device * dev,
				     drm_i915_batchbuffer_t * batch)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_clip_rect __user *boxes = batch->cliprects;
	int nbox = batch->num_cliprects;
	int i = 0, count;
	RING_LOCALS;

	if ((batch->start | batch->used) & 0x7) {
		DRM_ERROR("alignment");
		return -EINVAL;
	}

	i915_kernel_lost_context(dev);

	count = nbox ? nbox : 1;

	for (i = 0; i < count; i++) {
		if (i < nbox) {
			int ret = i915_emit_box(dev, boxes, i,
						batch->DR1, batch->DR4);
			if (ret)
				return ret;
		}

		if (dev_priv->use_mi_batchbuffer_start) {
			BEGIN_LP_RING(2);
			if (IS_I965G(dev)) {
				OUT_RING(MI_BATCH_BUFFER_START | (2 << 6) | MI_BATCH_NON_SECURE_I965);
				OUT_RING(batch->start);
			} else {
				OUT_RING(MI_BATCH_BUFFER_START | (2 << 6));
				OUT_RING(batch->start | MI_BATCH_NON_SECURE);
			}
			ADVANCE_LP_RING();
		} else {
			BEGIN_LP_RING(4);
			OUT_RING(MI_BATCH_BUFFER);
			OUT_RING(batch->start | MI_BATCH_NON_SECURE);
			OUT_RING(batch->start + batch->used - 4);
			OUT_RING(0);
			ADVANCE_LP_RING();
		}
	}

	i915_emit_breadcrumb(dev);

	return 0;
}

static int i915_dispatch_flip(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	RING_LOCALS;

	DRM_DEBUG("%s: page=%d pfCurrentPage=%d\n",
		  __FUNCTION__,
		  dev_priv->current_page,
		  dev_priv->sarea_priv->pf_current_page);

	i915_kernel_lost_context(dev);

	BEGIN_LP_RING(2);
	OUT_RING(INST_PARSER_CLIENT | INST_OP_FLUSH | INST_FLUSH_MAP_CACHE);
	OUT_RING(0);
	ADVANCE_LP_RING();

	BEGIN_LP_RING(6);
	OUT_RING(CMD_OP_DISPLAYBUFFER_INFO | ASYNC_FLIP);
	OUT_RING(0);
	if (dev_priv->current_page == 0) {
		OUT_RING(dev_priv->back_offset);
		dev_priv->current_page = 1;
	} else {
		OUT_RING(dev_priv->front_offset);
		dev_priv->current_page = 0;
	}
	OUT_RING(0);
	ADVANCE_LP_RING();

	BEGIN_LP_RING(2);
	OUT_RING(MI_WAIT_FOR_EVENT | MI_WAIT_FOR_PLANE_A_FLIP);
	OUT_RING(0);
	ADVANCE_LP_RING();

	dev_priv->sarea_priv->last_enqueue = dev_priv->counter++;

	BEGIN_LP_RING(4);
	OUT_RING(CMD_STORE_DWORD_IDX);
	OUT_RING(20);
	OUT_RING(dev_priv->counter);
	OUT_RING(0);
	ADVANCE_LP_RING();

	dev_priv->sarea_priv->pf_current_page = dev_priv->current_page;
	return 0;
}

static int i915_quiescent(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;

	i915_kernel_lost_context(dev);
	return i915_wait_ring(dev, dev_priv->ring.Size - 8, __func__);
}

static int i915_flush_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file_priv)
{
	LOCK_TEST_WITH_RETURN(dev, file_priv);

	return i915_quiescent(dev);
}

static int i915_batchbuffer(struct drm_device *dev, void *data,
			    struct drm_file *file_priv)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	u32 *hw_status = dev_priv->hw_status_page;
	drm_i915_sarea_t *sarea_priv = (drm_i915_sarea_t *)
	    dev_priv->sarea_priv;
	drm_i915_batchbuffer_t *batch = data;
	int ret;

	if (!dev_priv->allow_batchbuffer) {
		DRM_ERROR("Batchbuffer ioctl disabled\n");
		return -EINVAL;
	}

	DRM_DEBUG("i915 batchbuffer, start %x used %d cliprects %d\n",
		  batch->start, batch->used, batch->num_cliprects);

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	if (batch->num_cliprects && DRM_VERIFYAREA_READ(batch->cliprects,
						       batch->num_cliprects *
						       sizeof(struct drm_clip_rect)))
		return -EFAULT;

	ret = i915_dispatch_batchbuffer(dev, batch);

	sarea_priv->last_dispatch = (int)hw_status[5];
	return ret;
}

static int i915_cmdbuffer(struct drm_device *dev, void *data,
			  struct drm_file *file_priv)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	u32 *hw_status = dev_priv->hw_status_page;
	drm_i915_sarea_t *sarea_priv = (drm_i915_sarea_t *)
	    dev_priv->sarea_priv;
	drm_i915_cmdbuffer_t *cmdbuf = data;
	int ret;

	DRM_DEBUG("i915 cmdbuffer, buf %p sz %d cliprects %d\n",
		  cmdbuf->buf, cmdbuf->sz, cmdbuf->num_cliprects);

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	if (cmdbuf->num_cliprects &&
	    DRM_VERIFYAREA_READ(cmdbuf->cliprects,
				cmdbuf->num_cliprects *
				sizeof(struct drm_clip_rect))) {
		DRM_ERROR("Fault accessing cliprects\n");
		return -EFAULT;
	}

	ret = i915_dispatch_cmdbuffer(dev, cmdbuf);
	if (ret) {
		DRM_ERROR("i915_dispatch_cmdbuffer failed\n");
		return ret;
	}

	sarea_priv->last_dispatch = (int)hw_status[5];
	return 0;
}

static int i915_flip_bufs(struct drm_device *dev, void *data,
			  struct drm_file *file_priv)
{
	DRM_DEBUG("%s\n", __FUNCTION__);

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	return i915_dispatch_flip(dev);
}
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/async.h>
#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_legacy.h>
#include "intel_drv.h"
#include <drm/i915_drm.h>
#include "i915_drv.h"
#include "i915_vgpu.h"
#include "i915_trace.h"
#include <linux/pci.h>
#include <linux/console.h>
#include <linux/vt.h>
#include <linux/vgaarb.h>
#include <linux/acpi.h>
#include <linux/pnp.h>
#include <linux/vga_switcheroo.h>
#include <linux/slab.h>
#include <acpi/video.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/oom.h>


static int i915_getparam(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_getparam_t *param = data;
	int value;

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	switch (param->param) {
	case I915_PARAM_IRQ_ACTIVE:
		value = dev->irq ? 1 : 0;
		break;
	case I915_PARAM_ALLOW_BATCHBUFFER:
		value = dev_priv->allow_batchbuffer ? 1 : 0;
		break;
	case I915_PARAM_LAST_DISPATCH:
		value = READ_BREADCRUMB(dev_priv);
		break;
	default:
		DRM_ERROR("Unknown parameter %d\n", param->param);
		return -EINVAL;
	}

	if (DRM_COPY_TO_USER(param->value, &value, sizeof(int))) {
		DRM_ERROR("DRM_COPY_TO_USER failed\n");
	struct drm_i915_private *dev_priv = dev->dev_private;
	drm_i915_getparam_t *param = data;
	int value;

	switch (param->param) {
	case I915_PARAM_IRQ_ACTIVE:
	case I915_PARAM_ALLOW_BATCHBUFFER:
	case I915_PARAM_LAST_DISPATCH:
		/* Reject all old ums/dri params. */
		return -ENODEV;
	case I915_PARAM_CHIPSET_ID:
		value = dev->pdev->device;
		break;
	case I915_PARAM_REVISION:
		value = dev->pdev->revision;
		break;
	case I915_PARAM_HAS_GEM:
		value = 1;
		break;
	case I915_PARAM_NUM_FENCES_AVAIL:
		value = dev_priv->num_fence_regs;
		break;
	case I915_PARAM_HAS_OVERLAY:
		value = dev_priv->overlay ? 1 : 0;
		break;
	case I915_PARAM_HAS_PAGEFLIPPING:
		value = 1;
		break;
	case I915_PARAM_HAS_EXECBUF2:
		/* depends on GEM */
		value = 1;
		break;
	case I915_PARAM_HAS_BSD:
		value = intel_ring_initialized(&dev_priv->ring[VCS]);
		break;
	case I915_PARAM_HAS_BLT:
		value = intel_ring_initialized(&dev_priv->ring[BCS]);
		break;
	case I915_PARAM_HAS_VEBOX:
		value = intel_ring_initialized(&dev_priv->ring[VECS]);
		break;
	case I915_PARAM_HAS_BSD2:
		value = intel_ring_initialized(&dev_priv->ring[VCS2]);
		break;
	case I915_PARAM_HAS_RELAXED_FENCING:
		value = 1;
		break;
	case I915_PARAM_HAS_COHERENT_RINGS:
		value = 1;
		break;
	case I915_PARAM_HAS_EXEC_CONSTANTS:
		value = INTEL_INFO(dev)->gen >= 4;
		break;
	case I915_PARAM_HAS_RELAXED_DELTA:
		value = 1;
		break;
	case I915_PARAM_HAS_GEN7_SOL_RESET:
		value = 1;
		break;
	case I915_PARAM_HAS_LLC:
		value = HAS_LLC(dev);
		break;
	case I915_PARAM_HAS_WT:
		value = HAS_WT(dev);
		break;
	case I915_PARAM_HAS_ALIASING_PPGTT:
		value = USES_PPGTT(dev);
		break;
	case I915_PARAM_HAS_WAIT_TIMEOUT:
		value = 1;
		break;
	case I915_PARAM_HAS_SEMAPHORES:
		value = i915_semaphore_is_enabled(dev);
		break;
	case I915_PARAM_HAS_PRIME_VMAP_FLUSH:
		value = 1;
		break;
	case I915_PARAM_HAS_SECURE_BATCHES:
		value = HAS_SECURE_BATCHES(dev_priv) && capable(CAP_SYS_ADMIN);
		break;
	case I915_PARAM_HAS_PINNED_BATCHES:
		value = 1;
		break;
	case I915_PARAM_HAS_EXEC_NO_RELOC:
		value = 1;
		break;
	case I915_PARAM_HAS_EXEC_HANDLE_LUT:
		value = 1;
		break;
	case I915_PARAM_CMD_PARSER_VERSION:
		value = i915_cmd_parser_get_version(dev_priv);
		break;
	case I915_PARAM_HAS_COHERENT_PHYS_GTT:
		value = 1;
		break;
	case I915_PARAM_MMAP_VERSION:
		value = 1;
		break;
	case I915_PARAM_SUBSLICE_TOTAL:
		value = INTEL_INFO(dev)->subslice_total;
		if (!value)
			return -ENODEV;
		break;
	case I915_PARAM_EU_TOTAL:
		value = INTEL_INFO(dev)->eu_total;
		if (!value)
			return -ENODEV;
		break;
	case I915_PARAM_HAS_GPU_RESET:
		value = i915.enable_hangcheck &&
			intel_has_gpu_reset(dev);
		break;
	case I915_PARAM_HAS_RESOURCE_STREAMER:
		value = HAS_RESOURCE_STREAMER(dev);
		break;
	default:
		DRM_DEBUG("Unknown parameter %d\n", param->param);
		return -EINVAL;
	}

	if (copy_to_user(param->value, &value, sizeof(int))) {
		DRM_ERROR("copy_to_user failed\n");
		return -EFAULT;
	}

	return 0;
}

static int i915_setparam(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_setparam_t *param = data;

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	switch (param->param) {
	case I915_SETPARAM_USE_MI_BATCHBUFFER_START:
		if (!IS_I965G(dev))
			dev_priv->use_mi_batchbuffer_start = param->value;
		break;
	case I915_SETPARAM_TEX_LRU_LOG_GRANULARITY:
		dev_priv->tex_lru_log_granularity = param->value;
		break;
	case I915_SETPARAM_ALLOW_BATCHBUFFER:
		dev_priv->allow_batchbuffer = param->value;
		break;
	default:
		DRM_ERROR("unknown parameter %d\n", param->param);
		return -EINVAL;
	}

	return 0;
}

static int i915_set_status_page(struct drm_device *dev, void *data,
				struct drm_file *file_priv)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_hws_addr_t *hws = data;

	if (!I915_NEED_GFX_HWS(dev))
		return -EINVAL;

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	printk(KERN_DEBUG "set status page addr 0x%08x\n", (u32)hws->addr);

	dev_priv->status_gfx_addr = hws->addr & (0x1ffff<<12);

	dev_priv->hws_map.offset = dev->agp->base + hws->addr;
	dev_priv->hws_map.size = 4*1024;
	dev_priv->hws_map.type = 0;
	dev_priv->hws_map.flags = 0;
	dev_priv->hws_map.mtrr = 0;

	drm_core_ioremap(&dev_priv->hws_map, dev);
	if (dev_priv->hws_map.handle == NULL) {
		i915_dma_cleanup(dev);
		dev_priv->status_gfx_addr = 0;
		DRM_ERROR("can not ioremap virtual address for"
				" G33 hw status page\n");
		return -ENOMEM;
	}
	dev_priv->hw_status_page = dev_priv->hws_map.handle;

	memset(dev_priv->hw_status_page, 0, PAGE_SIZE);
	I915_WRITE(0x02080, dev_priv->status_gfx_addr);
	DRM_DEBUG("load hws 0x2080 with gfx mem 0x%x\n",
			dev_priv->status_gfx_addr);
	DRM_DEBUG("load hws at %p\n", dev_priv->hw_status_page);
	return 0;
}

int i915_driver_load(struct drm_device *dev, unsigned long flags)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long base, size;
	int ret = 0, mmio_bar = IS_I9XX(dev) ? 0 : 1;

	/* i915 has 4 more counters */
	dev->counters += 4;
	dev->types[6] = _DRM_STAT_IRQ;
	dev->types[7] = _DRM_STAT_PRIMARY;
	dev->types[8] = _DRM_STAT_SECONDARY;
	dev->types[9] = _DRM_STAT_DMA;

	dev_priv = drm_alloc(sizeof(drm_i915_private_t), DRM_MEM_DRIVER);
	if (dev_priv == NULL)
		return -ENOMEM;

	memset(dev_priv, 0, sizeof(drm_i915_private_t));

	dev->dev_private = (void *)dev_priv;

	/* Add register map (needed for suspend/resume) */
	base = drm_get_resource_start(dev, mmio_bar);
	size = drm_get_resource_len(dev, mmio_bar);

	ret = drm_addmap(dev, base, size, _DRM_REGISTERS,
			 _DRM_KERNEL | _DRM_DRIVER,
			 &dev_priv->mmio_map);
static int i915_get_bridge_dev(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	dev_priv->bridge_dev = pci_get_bus_and_slot(0, PCI_DEVFN(0, 0));
	if (!dev_priv->bridge_dev) {
		DRM_ERROR("bridge device not found\n");
		return -1;
	}
	return 0;
}

#define MCHBAR_I915 0x44
#define MCHBAR_I965 0x48
#define MCHBAR_SIZE (4*4096)

#define DEVEN_REG 0x54
#define   DEVEN_MCHBAR_EN (1 << 28)

/* Allocate space for the MCH regs if needed, return nonzero on error */
static int
intel_alloc_mchbar_resource(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int reg = INTEL_INFO(dev)->gen >= 4 ? MCHBAR_I965 : MCHBAR_I915;
	u32 temp_lo, temp_hi = 0;
	u64 mchbar_addr;
	int ret;

	if (INTEL_INFO(dev)->gen >= 4)
		pci_read_config_dword(dev_priv->bridge_dev, reg + 4, &temp_hi);
	pci_read_config_dword(dev_priv->bridge_dev, reg, &temp_lo);
	mchbar_addr = ((u64)temp_hi << 32) | temp_lo;

	/* If ACPI doesn't have it, assume we need to allocate it ourselves */
#ifdef CONFIG_PNP
	if (mchbar_addr &&
	    pnp_range_reserved(mchbar_addr, mchbar_addr + MCHBAR_SIZE))
		return 0;
#endif

	/* Get some space for it */
	dev_priv->mch_res.name = "i915 MCHBAR";
	dev_priv->mch_res.flags = IORESOURCE_MEM;
	ret = pci_bus_alloc_resource(dev_priv->bridge_dev->bus,
				     &dev_priv->mch_res,
				     MCHBAR_SIZE, MCHBAR_SIZE,
				     PCIBIOS_MIN_MEM,
				     0, pcibios_align_resource,
				     dev_priv->bridge_dev);
	if (ret) {
		DRM_DEBUG_DRIVER("failed bus alloc: %d\n", ret);
		dev_priv->mch_res.start = 0;
		return ret;
	}

	if (INTEL_INFO(dev)->gen >= 4)
		pci_write_config_dword(dev_priv->bridge_dev, reg + 4,
				       upper_32_bits(dev_priv->mch_res.start));

	pci_write_config_dword(dev_priv->bridge_dev, reg,
			       lower_32_bits(dev_priv->mch_res.start));
	return 0;
}

/* Setup MCHBAR if possible, return true if we should disable it again */
static void
intel_setup_mchbar(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int mchbar_reg = INTEL_INFO(dev)->gen >= 4 ? MCHBAR_I965 : MCHBAR_I915;
	u32 temp;
	bool enabled;

	if (IS_VALLEYVIEW(dev))
		return;

	dev_priv->mchbar_need_disable = false;

	if (IS_I915G(dev) || IS_I915GM(dev)) {
		pci_read_config_dword(dev_priv->bridge_dev, DEVEN_REG, &temp);
		enabled = !!(temp & DEVEN_MCHBAR_EN);
	} else {
		pci_read_config_dword(dev_priv->bridge_dev, mchbar_reg, &temp);
		enabled = temp & 1;
	}

	/* If it's already enabled, don't have to do anything */
	if (enabled)
		return;

	if (intel_alloc_mchbar_resource(dev))
		return;

	dev_priv->mchbar_need_disable = true;

	/* Space is allocated or reserved, so enable it. */
	if (IS_I915G(dev) || IS_I915GM(dev)) {
		pci_write_config_dword(dev_priv->bridge_dev, DEVEN_REG,
				       temp | DEVEN_MCHBAR_EN);
	} else {
		pci_read_config_dword(dev_priv->bridge_dev, mchbar_reg, &temp);
		pci_write_config_dword(dev_priv->bridge_dev, mchbar_reg, temp | 1);
	}
}

static void
intel_teardown_mchbar(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int mchbar_reg = INTEL_INFO(dev)->gen >= 4 ? MCHBAR_I965 : MCHBAR_I915;
	u32 temp;

	if (dev_priv->mchbar_need_disable) {
		if (IS_I915G(dev) || IS_I915GM(dev)) {
			pci_read_config_dword(dev_priv->bridge_dev, DEVEN_REG, &temp);
			temp &= ~DEVEN_MCHBAR_EN;
			pci_write_config_dword(dev_priv->bridge_dev, DEVEN_REG, temp);
		} else {
			pci_read_config_dword(dev_priv->bridge_dev, mchbar_reg, &temp);
			temp &= ~1;
			pci_write_config_dword(dev_priv->bridge_dev, mchbar_reg, temp);
		}
	}

	if (dev_priv->mch_res.start)
		release_resource(&dev_priv->mch_res);
}

/* true = enable decode, false = disable decoder */
static unsigned int i915_vga_set_decode(void *cookie, bool state)
{
	struct drm_device *dev = cookie;

	intel_modeset_vga_set_state(dev, state);
	if (state)
		return VGA_RSRC_LEGACY_IO | VGA_RSRC_LEGACY_MEM |
		       VGA_RSRC_NORMAL_IO | VGA_RSRC_NORMAL_MEM;
	else
		return VGA_RSRC_NORMAL_IO | VGA_RSRC_NORMAL_MEM;
}

static void i915_switcheroo_set_state(struct pci_dev *pdev, enum vga_switcheroo_state state)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	pm_message_t pmm = { .event = PM_EVENT_SUSPEND };

	if (state == VGA_SWITCHEROO_ON) {
		pr_info("switched on\n");
		dev->switch_power_state = DRM_SWITCH_POWER_CHANGING;
		/* i915 resume handler doesn't set to D0 */
		pci_set_power_state(dev->pdev, PCI_D0);
		i915_resume_switcheroo(dev);
		dev->switch_power_state = DRM_SWITCH_POWER_ON;
	} else {
		pr_err("switched off\n");
		dev->switch_power_state = DRM_SWITCH_POWER_CHANGING;
		i915_suspend_switcheroo(dev, pmm);
		dev->switch_power_state = DRM_SWITCH_POWER_OFF;
	}
}

static bool i915_switcheroo_can_switch(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	/*
	 * FIXME: open_count is protected by drm_global_mutex but that would lead to
	 * locking inversion with the driver load path. And the access here is
	 * completely racy anyway. So don't bother with locking for now.
	 */
	return dev->open_count == 0;
}

static const struct vga_switcheroo_client_ops i915_switcheroo_ops = {
	.set_gpu_state = i915_switcheroo_set_state,
	.reprobe = NULL,
	.can_switch = i915_switcheroo_can_switch,
};

static int i915_load_modeset_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;

	ret = intel_parse_bios(dev);
	if (ret)
		DRM_INFO("failed to find VBIOS tables\n");

	/* If we have > 1 VGA cards, then we need to arbitrate access
	 * to the common VGA resources.
	 *
	 * If we are a secondary display controller (!PCI_DISPLAY_CLASS_VGA),
	 * then we do not take part in VGA arbitration and the
	 * vga_client_register() fails with -ENODEV.
	 */
	ret = vga_client_register(dev->pdev, dev, NULL, i915_vga_set_decode);
	if (ret && ret != -ENODEV)
		goto out;

	intel_register_dsm_handler();

	ret = vga_switcheroo_register_client(dev->pdev, &i915_switcheroo_ops, false);
	if (ret)
		goto cleanup_vga_client;

	/* Initialise stolen first so that we may reserve preallocated
	 * objects for the BIOS to KMS transition.
	 */
	ret = i915_gem_init_stolen(dev);
	if (ret)
		goto cleanup_vga_switcheroo;

	intel_power_domains_init_hw(dev_priv);

	ret = intel_irq_install(dev_priv);
	if (ret)
		goto cleanup_gem_stolen;

	intel_setup_gmbus(dev);

	/* Important: The output setup functions called by modeset_init need
	 * working irqs for e.g. gmbus and dp aux transfers. */
	intel_modeset_init(dev);

	intel_guc_ucode_init(dev);

	ret = i915_gem_init(dev);
	if (ret)
		goto cleanup_irq;

	intel_modeset_gem_init(dev);

	/* Always safe in the mode setting case. */
	/* FIXME: do pre/post-mode set stuff in core KMS code */
	dev->vblank_disable_allowed = true;
	if (INTEL_INFO(dev)->num_pipes == 0)
		return 0;

	ret = intel_fbdev_init(dev);
	if (ret)
		goto cleanup_gem;

	/* Only enable hotplug handling once the fbdev is fully set up. */
	intel_hpd_init(dev_priv);

	/*
	 * Some ports require correctly set-up hpd registers for detection to
	 * work properly (leading to ghost connected connector status), e.g. VGA
	 * on gm45.  Hence we can only set up the initial fbdev config after hpd
	 * irqs are fully enabled. Now we should scan for the initial config
	 * only once hotplug handling is enabled, but due to screwed-up locking
	 * around kms/fbdev init we can't protect the fdbev initial config
	 * scanning against hotplug events. Hence do this first and ignore the
	 * tiny window where we will loose hotplug notifactions.
	 */
	async_schedule(intel_fbdev_initial_config, dev_priv);

	drm_kms_helper_poll_init(dev);

	return 0;

cleanup_gem:
	mutex_lock(&dev->struct_mutex);
	i915_gem_cleanup_ringbuffer(dev);
	i915_gem_context_fini(dev);
	mutex_unlock(&dev->struct_mutex);
cleanup_irq:
	intel_guc_ucode_fini(dev);
	drm_irq_uninstall(dev);
	intel_teardown_gmbus(dev);
cleanup_gem_stolen:
	i915_gem_cleanup_stolen(dev);
cleanup_vga_switcheroo:
	vga_switcheroo_unregister_client(dev->pdev);
cleanup_vga_client:
	vga_client_register(dev->pdev, NULL, NULL, NULL);
out:
	return ret;
}

#if IS_ENABLED(CONFIG_FB)
static int i915_kick_out_firmware_fb(struct drm_i915_private *dev_priv)
{
	struct apertures_struct *ap;
	struct pci_dev *pdev = dev_priv->dev->pdev;
	bool primary;
	int ret;

	ap = alloc_apertures(1);
	if (!ap)
		return -ENOMEM;

	ap->ranges[0].base = dev_priv->gtt.mappable_base;
	ap->ranges[0].size = dev_priv->gtt.mappable_end;

	primary =
		pdev->resource[PCI_ROM_RESOURCE].flags & IORESOURCE_ROM_SHADOW;

	ret = remove_conflicting_framebuffers(ap, "inteldrmfb", primary);

	kfree(ap);

	return ret;
}
#else
static int i915_kick_out_firmware_fb(struct drm_i915_private *dev_priv)
{
	return 0;
}
#endif

#if !defined(CONFIG_VGA_CONSOLE)
static int i915_kick_out_vgacon(struct drm_i915_private *dev_priv)
{
	return 0;
}
#elif !defined(CONFIG_DUMMY_CONSOLE)
static int i915_kick_out_vgacon(struct drm_i915_private *dev_priv)
{
	return -ENODEV;
}
#else
static int i915_kick_out_vgacon(struct drm_i915_private *dev_priv)
{
	int ret = 0;

	DRM_INFO("Replacing VGA console driver\n");

	console_lock();
	if (con_is_bound(&vga_con))
		ret = do_take_over_console(&dummy_con, 0, MAX_NR_CONSOLES - 1, 1);
	if (ret == 0) {
		ret = do_unregister_con_driver(&vga_con);

		/* Ignore "already unregistered". */
		if (ret == -ENODEV)
			ret = 0;
	}
	console_unlock();

	return ret;
}
#endif

static void i915_dump_device_info(struct drm_i915_private *dev_priv)
{
	const struct intel_device_info *info = &dev_priv->info;

#define PRINT_S(name) "%s"
#define SEP_EMPTY
#define PRINT_FLAG(name) info->name ? #name "," : ""
#define SEP_COMMA ,
	DRM_DEBUG_DRIVER("i915 device info: gen=%i, pciid=0x%04x rev=0x%02x flags="
			 DEV_INFO_FOR_EACH_FLAG(PRINT_S, SEP_EMPTY),
			 info->gen,
			 dev_priv->dev->pdev->device,
			 dev_priv->dev->pdev->revision,
			 DEV_INFO_FOR_EACH_FLAG(PRINT_FLAG, SEP_COMMA));
#undef PRINT_S
#undef SEP_EMPTY
#undef PRINT_FLAG
#undef SEP_COMMA
}

static void cherryview_sseu_info_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_device_info *info;
	u32 fuse, eu_dis;

	info = (struct intel_device_info *)&dev_priv->info;
	fuse = I915_READ(CHV_FUSE_GT);

	info->slice_total = 1;

	if (!(fuse & CHV_FGT_DISABLE_SS0)) {
		info->subslice_per_slice++;
		eu_dis = fuse & (CHV_FGT_EU_DIS_SS0_R0_MASK |
				 CHV_FGT_EU_DIS_SS0_R1_MASK);
		info->eu_total += 8 - hweight32(eu_dis);
	}

	if (!(fuse & CHV_FGT_DISABLE_SS1)) {
		info->subslice_per_slice++;
		eu_dis = fuse & (CHV_FGT_EU_DIS_SS1_R0_MASK |
				 CHV_FGT_EU_DIS_SS1_R1_MASK);
		info->eu_total += 8 - hweight32(eu_dis);
	}

	info->subslice_total = info->subslice_per_slice;
	/*
	 * CHV expected to always have a uniform distribution of EU
	 * across subslices.
	*/
	info->eu_per_subslice = info->subslice_total ?
				info->eu_total / info->subslice_total :
				0;
	/*
	 * CHV supports subslice power gating on devices with more than
	 * one subslice, and supports EU power gating on devices with
	 * more than one EU pair per subslice.
	*/
	info->has_slice_pg = 0;
	info->has_subslice_pg = (info->subslice_total > 1);
	info->has_eu_pg = (info->eu_per_subslice > 2);
}

static void gen9_sseu_info_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_device_info *info;
	int s_max = 3, ss_max = 4, eu_max = 8;
	int s, ss;
	u32 fuse2, s_enable, ss_disable, eu_disable;
	u8 eu_mask = 0xff;

	info = (struct intel_device_info *)&dev_priv->info;
	fuse2 = I915_READ(GEN8_FUSE2);
	s_enable = (fuse2 & GEN8_F2_S_ENA_MASK) >>
		   GEN8_F2_S_ENA_SHIFT;
	ss_disable = (fuse2 & GEN9_F2_SS_DIS_MASK) >>
		     GEN9_F2_SS_DIS_SHIFT;

	info->slice_total = hweight32(s_enable);
	/*
	 * The subslice disable field is global, i.e. it applies
	 * to each of the enabled slices.
	*/
	info->subslice_per_slice = ss_max - hweight32(ss_disable);
	info->subslice_total = info->slice_total *
			       info->subslice_per_slice;

	/*
	 * Iterate through enabled slices and subslices to
	 * count the total enabled EU.
	*/
	for (s = 0; s < s_max; s++) {
		if (!(s_enable & (0x1 << s)))
			/* skip disabled slice */
			continue;

		eu_disable = I915_READ(GEN9_EU_DISABLE(s));
		for (ss = 0; ss < ss_max; ss++) {
			int eu_per_ss;

			if (ss_disable & (0x1 << ss))
				/* skip disabled subslice */
				continue;

			eu_per_ss = eu_max - hweight8((eu_disable >> (ss*8)) &
						      eu_mask);

			/*
			 * Record which subslice(s) has(have) 7 EUs. we
			 * can tune the hash used to spread work among
			 * subslices if they are unbalanced.
			 */
			if (eu_per_ss == 7)
				info->subslice_7eu[s] |= 1 << ss;

			info->eu_total += eu_per_ss;
		}
	}

	/*
	 * SKL is expected to always have a uniform distribution
	 * of EU across subslices with the exception that any one
	 * EU in any one subslice may be fused off for die
	 * recovery. BXT is expected to be perfectly uniform in EU
	 * distribution.
	*/
	info->eu_per_subslice = info->subslice_total ?
				DIV_ROUND_UP(info->eu_total,
					     info->subslice_total) : 0;
	/*
	 * SKL supports slice power gating on devices with more than
	 * one slice, and supports EU power gating on devices with
	 * more than one EU pair per subslice. BXT supports subslice
	 * power gating on devices with more than one subslice, and
	 * supports EU power gating on devices with more than one EU
	 * pair per subslice.
	*/
	info->has_slice_pg = (IS_SKYLAKE(dev) && (info->slice_total > 1));
	info->has_subslice_pg = (IS_BROXTON(dev) && (info->subslice_total > 1));
	info->has_eu_pg = (info->eu_per_subslice > 2);
}

static void broadwell_sseu_info_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_device_info *info;
	const int s_max = 3, ss_max = 3, eu_max = 8;
	int s, ss;
	u32 fuse2, eu_disable[s_max], s_enable, ss_disable;

	fuse2 = I915_READ(GEN8_FUSE2);
	s_enable = (fuse2 & GEN8_F2_S_ENA_MASK) >> GEN8_F2_S_ENA_SHIFT;
	ss_disable = (fuse2 & GEN8_F2_SS_DIS_MASK) >> GEN8_F2_SS_DIS_SHIFT;

	eu_disable[0] = I915_READ(GEN8_EU_DISABLE0) & GEN8_EU_DIS0_S0_MASK;
	eu_disable[1] = (I915_READ(GEN8_EU_DISABLE0) >> GEN8_EU_DIS0_S1_SHIFT) |
			((I915_READ(GEN8_EU_DISABLE1) & GEN8_EU_DIS1_S1_MASK) <<
			 (32 - GEN8_EU_DIS0_S1_SHIFT));
	eu_disable[2] = (I915_READ(GEN8_EU_DISABLE1) >> GEN8_EU_DIS1_S2_SHIFT) |
			((I915_READ(GEN8_EU_DISABLE2) & GEN8_EU_DIS2_S2_MASK) <<
			 (32 - GEN8_EU_DIS1_S2_SHIFT));


	info = (struct intel_device_info *)&dev_priv->info;
	info->slice_total = hweight32(s_enable);

	/*
	 * The subslice disable field is global, i.e. it applies
	 * to each of the enabled slices.
	 */
	info->subslice_per_slice = ss_max - hweight32(ss_disable);
	info->subslice_total = info->slice_total * info->subslice_per_slice;

	/*
	 * Iterate through enabled slices and subslices to
	 * count the total enabled EU.
	 */
	for (s = 0; s < s_max; s++) {
		if (!(s_enable & (0x1 << s)))
			/* skip disabled slice */
			continue;

		for (ss = 0; ss < ss_max; ss++) {
			u32 n_disabled;

			if (ss_disable & (0x1 << ss))
				/* skip disabled subslice */
				continue;

			n_disabled = hweight8(eu_disable[s] >> (ss * eu_max));

			/*
			 * Record which subslices have 7 EUs.
			 */
			if (eu_max - n_disabled == 7)
				info->subslice_7eu[s] |= 1 << ss;

			info->eu_total += eu_max - n_disabled;
		}
	}

	/*
	 * BDW is expected to always have a uniform distribution of EU across
	 * subslices with the exception that any one EU in any one subslice may
	 * be fused off for die recovery.
	 */
	info->eu_per_subslice = info->subslice_total ?
		DIV_ROUND_UP(info->eu_total, info->subslice_total) : 0;

	/*
	 * BDW supports slice power gating on devices with more than
	 * one slice.
	 */
	info->has_slice_pg = (info->slice_total > 1);
	info->has_subslice_pg = 0;
	info->has_eu_pg = 0;
}

/*
 * Determine various intel_device_info fields at runtime.
 *
 * Use it when either:
 *   - it's judged too laborious to fill n static structures with the limit
 *     when a simple if statement does the job,
 *   - run-time checks (eg read fuse/strap registers) are needed.
 *
 * This function needs to be called:
 *   - after the MMIO has been setup as we are reading registers,
 *   - after the PCH has been detected,
 *   - before the first usage of the fields it can tweak.
 */
static void intel_device_info_runtime_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_device_info *info;
	enum pipe pipe;

	info = (struct intel_device_info *)&dev_priv->info;

	/*
	 * Skylake and Broxton currently don't expose the topmost plane as its
	 * use is exclusive with the legacy cursor and we only want to expose
	 * one of those, not both. Until we can safely expose the topmost plane
	 * as a DRM_PLANE_TYPE_CURSOR with all the features exposed/supported,
	 * we don't expose the topmost plane at all to prevent ABI breakage
	 * down the line.
	 */
	if (IS_BROXTON(dev)) {
		info->num_sprites[PIPE_A] = 2;
		info->num_sprites[PIPE_B] = 2;
		info->num_sprites[PIPE_C] = 1;
	} else if (IS_VALLEYVIEW(dev))
		for_each_pipe(dev_priv, pipe)
			info->num_sprites[pipe] = 2;
	else
		for_each_pipe(dev_priv, pipe)
			info->num_sprites[pipe] = 1;

	if (i915.disable_display) {
		DRM_INFO("Display disabled (module parameter)\n");
		info->num_pipes = 0;
	} else if (info->num_pipes > 0 &&
		   (INTEL_INFO(dev)->gen == 7 || INTEL_INFO(dev)->gen == 8) &&
		   !IS_VALLEYVIEW(dev)) {
		u32 fuse_strap = I915_READ(FUSE_STRAP);
		u32 sfuse_strap = I915_READ(SFUSE_STRAP);

		/*
		 * SFUSE_STRAP is supposed to have a bit signalling the display
		 * is fused off. Unfortunately it seems that, at least in
		 * certain cases, fused off display means that PCH display
		 * reads don't land anywhere. In that case, we read 0s.
		 *
		 * On CPT/PPT, we can detect this case as SFUSE_STRAP_FUSE_LOCK
		 * should be set when taking over after the firmware.
		 */
		if (fuse_strap & ILK_INTERNAL_DISPLAY_DISABLE ||
		    sfuse_strap & SFUSE_STRAP_DISPLAY_DISABLED ||
		    (dev_priv->pch_type == PCH_CPT &&
		     !(sfuse_strap & SFUSE_STRAP_FUSE_LOCK))) {
			DRM_INFO("Display fused off, disabling\n");
			info->num_pipes = 0;
		}
	}

	/* Initialize slice/subslice/EU info */
	if (IS_CHERRYVIEW(dev))
		cherryview_sseu_info_init(dev);
	else if (IS_BROADWELL(dev))
		broadwell_sseu_info_init(dev);
	else if (INTEL_INFO(dev)->gen >= 9)
		gen9_sseu_info_init(dev);

	DRM_DEBUG_DRIVER("slice total: %u\n", info->slice_total);
	DRM_DEBUG_DRIVER("subslice total: %u\n", info->subslice_total);
	DRM_DEBUG_DRIVER("subslice per slice: %u\n", info->subslice_per_slice);
	DRM_DEBUG_DRIVER("EU total: %u\n", info->eu_total);
	DRM_DEBUG_DRIVER("EU per subslice: %u\n", info->eu_per_subslice);
	DRM_DEBUG_DRIVER("has slice power gating: %s\n",
			 info->has_slice_pg ? "y" : "n");
	DRM_DEBUG_DRIVER("has subslice power gating: %s\n",
			 info->has_subslice_pg ? "y" : "n");
	DRM_DEBUG_DRIVER("has EU power gating: %s\n",
			 info->has_eu_pg ? "y" : "n");
}

static void intel_init_dpio(struct drm_i915_private *dev_priv)
{
	if (!IS_VALLEYVIEW(dev_priv))
		return;

	/*
	 * IOSF_PORT_DPIO is used for VLV x2 PHY (DP/HDMI B and C),
	 * CHV x1 PHY (DP/HDMI D)
	 * IOSF_PORT_DPIO_2 is used for CHV x2 PHY (DP/HDMI B and C)
	 */
	if (IS_CHERRYVIEW(dev_priv)) {
		DPIO_PHY_IOSF_PORT(DPIO_PHY0) = IOSF_PORT_DPIO_2;
		DPIO_PHY_IOSF_PORT(DPIO_PHY1) = IOSF_PORT_DPIO;
	} else {
		DPIO_PHY_IOSF_PORT(DPIO_PHY0) = IOSF_PORT_DPIO;
	}
}

/**
 * i915_driver_load - setup chip and create an initial config
 * @dev: DRM device
 * @flags: startup flags
 *
 * The driver load routine has to do several things:
 *   - drive output discovery via intel_modeset_init()
 *   - initialize the memory manager
 *   - allocate initial config memory
 *   - setup the DRM framebuffer with the allocated memory
 */
int i915_driver_load(struct drm_device *dev, unsigned long flags)
{
	struct drm_i915_private *dev_priv;
	struct intel_device_info *info, *device_info;
	int ret = 0, mmio_bar, mmio_size;
	uint32_t aperture_size;

	info = (struct intel_device_info *) flags;

	dev_priv = kzalloc(sizeof(*dev_priv), GFP_KERNEL);
	if (dev_priv == NULL)
		return -ENOMEM;

	dev->dev_private = dev_priv;
	dev_priv->dev = dev;

	/* Setup the write-once "constant" device info */
	device_info = (struct intel_device_info *)&dev_priv->info;
	memcpy(device_info, info, sizeof(dev_priv->info));
	device_info->device_id = dev->pdev->device;

	spin_lock_init(&dev_priv->irq_lock);
	spin_lock_init(&dev_priv->gpu_error.lock);
	mutex_init(&dev_priv->backlight_lock);
	spin_lock_init(&dev_priv->uncore.lock);
	spin_lock_init(&dev_priv->mm.object_stat_lock);
	spin_lock_init(&dev_priv->mmio_flip_lock);
	mutex_init(&dev_priv->sb_lock);
	mutex_init(&dev_priv->modeset_restore_lock);
	mutex_init(&dev_priv->csr_lock);
	mutex_init(&dev_priv->av_mutex);

	intel_pm_setup(dev);

	intel_display_crc_init(dev);

	i915_dump_device_info(dev_priv);

	/* Not all pre-production machines fall into this category, only the
	 * very first ones. Almost everything should work, except for maybe
	 * suspend/resume. And we don't implement workarounds that affect only
	 * pre-production machines. */
	if (IS_HSW_EARLY_SDV(dev))
		DRM_INFO("This is an early pre-production Haswell machine. "
			 "It may not be fully functional.\n");

	if (i915_get_bridge_dev(dev)) {
		ret = -EIO;
		goto free_priv;
	}

	mmio_bar = IS_GEN2(dev) ? 1 : 0;
	/* Before gen4, the registers and the GTT are behind different BARs.
	 * However, from gen4 onwards, the registers and the GTT are shared
	 * in the same BAR, so we want to restrict this ioremap from
	 * clobbering the GTT which we want ioremap_wc instead. Fortunately,
	 * the register BAR remains the same size for all the earlier
	 * generations up to Ironlake.
	 */
	if (info->gen < 5)
		mmio_size = 512*1024;
	else
		mmio_size = 2*1024*1024;

	dev_priv->regs = pci_iomap(dev->pdev, mmio_bar, mmio_size);
	if (!dev_priv->regs) {
		DRM_ERROR("failed to map registers\n");
		ret = -EIO;
		goto put_bridge;
	}

	/* This must be called before any calls to HAS_PCH_* */
	intel_detect_pch(dev);

	intel_uncore_init(dev);

	/* Load CSR Firmware for SKL */
	intel_csr_ucode_init(dev);

	ret = i915_gem_gtt_init(dev);
	if (ret)
		goto out_freecsr;

	/* WARNING: Apparently we must kick fbdev drivers before vgacon,
	 * otherwise the vga fbdev driver falls over. */
	ret = i915_kick_out_firmware_fb(dev_priv);
	if (ret) {
		DRM_ERROR("failed to remove conflicting framebuffer drivers\n");
		goto out_gtt;
	}

	ret = i915_kick_out_vgacon(dev_priv);
	if (ret) {
		DRM_ERROR("failed to remove conflicting VGA console\n");
		goto out_gtt;
	}

	pci_set_master(dev->pdev);

	/* overlay on gen2 is broken and can't address above 1G */
	if (IS_GEN2(dev))
		dma_set_coherent_mask(&dev->pdev->dev, DMA_BIT_MASK(30));

	/* 965GM sometimes incorrectly writes to hardware status page (HWS)
	 * using 32bit addressing, overwriting memory if HWS is located
	 * above 4GB.
	 *
	 * The documentation also mentions an issue with undefined
	 * behaviour if any general state is accessed within a page above 4GB,
	 * which also needs to be handled carefully.
	 */
	if (IS_BROADWATER(dev) || IS_CRESTLINE(dev))
		dma_set_coherent_mask(&dev->pdev->dev, DMA_BIT_MASK(32));

	aperture_size = dev_priv->gtt.mappable_end;

	dev_priv->gtt.mappable =
		io_mapping_create_wc(dev_priv->gtt.mappable_base,
				     aperture_size);
	if (dev_priv->gtt.mappable == NULL) {
		ret = -EIO;
		goto out_gtt;
	}

	dev_priv->gtt.mtrr = arch_phys_wc_add(dev_priv->gtt.mappable_base,
					      aperture_size);

	/* The i915 workqueue is primarily used for batched retirement of
	 * requests (and thus managing bo) once the task has been completed
	 * by the GPU. i915_gem_retire_requests() is called directly when we
	 * need high-priority retirement, such as waiting for an explicit
	 * bo.
	 *
	 * It is also used for periodic low-priority events, such as
	 * idle-timers and recording error state.
	 *
	 * All tasks on the workqueue are expected to acquire the dev mutex
	 * so there is no point in running more than one instance of the
	 * workqueue at any time.  Use an ordered one.
	 */
	dev_priv->wq = alloc_ordered_workqueue("i915", 0);
	if (dev_priv->wq == NULL) {
		DRM_ERROR("Failed to create our workqueue.\n");
		ret = -ENOMEM;
		goto out_mtrrfree;
	}

	dev_priv->hotplug.dp_wq = alloc_ordered_workqueue("i915-dp", 0);
	if (dev_priv->hotplug.dp_wq == NULL) {
		DRM_ERROR("Failed to create our dp workqueue.\n");
		ret = -ENOMEM;
		goto out_freewq;
	}

	dev_priv->gpu_error.hangcheck_wq =
		alloc_ordered_workqueue("i915-hangcheck", 0);
	if (dev_priv->gpu_error.hangcheck_wq == NULL) {
		DRM_ERROR("Failed to create our hangcheck workqueue.\n");
		ret = -ENOMEM;
		goto out_freedpwq;
	}

	intel_irq_init(dev_priv);
	intel_uncore_sanitize(dev);

	/* Try to make sure MCHBAR is enabled before poking at it */
	intel_setup_mchbar(dev);
	intel_opregion_setup(dev);

	i915_gem_load(dev);

	/* On the 945G/GM, the chipset reports the MSI capability on the
	 * integrated graphics even though the support isn't actually there
	 * according to the published specs.  It doesn't appear to function
	 * correctly in testing on 945G.
	 * This may be a side effect of MSI having been made available for PEG
	 * and the registers being closely associated.
	 *
	 * According to chipset errata, on the 965GM, MSI interrupts may
	 * be lost or delayed, but we use them anyways to avoid
	 * stuck interrupts on some machines.
	 */
	if (!IS_I945G(dev) && !IS_I945GM(dev))
		pci_enable_msi(dev->pdev);

	intel_device_info_runtime_init(dev);

	intel_init_dpio(dev_priv);

	if (INTEL_INFO(dev)->num_pipes) {
		ret = drm_vblank_init(dev, INTEL_INFO(dev)->num_pipes);
		if (ret)
			goto out_gem_unload;
	}

	intel_power_domains_init(dev_priv);

	ret = i915_load_modeset_init(dev);
	if (ret < 0) {
		DRM_ERROR("failed to init modeset\n");
		goto out_power_well;
	}

	/*
	 * Notify a valid surface after modesetting,
	 * when running inside a VM.
	 */
	if (intel_vgpu_active(dev))
		I915_WRITE(vgtif_reg(display_ready), VGT_DRV_DISPLAY_READY);

	i915_setup_sysfs(dev);

	if (INTEL_INFO(dev)->num_pipes) {
		/* Must be done after probing outputs */
		intel_opregion_init(dev);
		acpi_video_register();
	}

	if (IS_GEN5(dev))
		intel_gpu_ips_init(dev_priv);

	intel_runtime_pm_enable(dev_priv);

	i915_audio_component_init(dev_priv);

	return 0;

out_power_well:
	intel_power_domains_fini(dev_priv);
	drm_vblank_cleanup(dev);
out_gem_unload:
	WARN_ON(unregister_oom_notifier(&dev_priv->mm.oom_notifier));
	unregister_shrinker(&dev_priv->mm.shrinker);

	if (dev->pdev->msi_enabled)
		pci_disable_msi(dev->pdev);

	intel_teardown_mchbar(dev);
	pm_qos_remove_request(&dev_priv->pm_qos);
	destroy_workqueue(dev_priv->gpu_error.hangcheck_wq);
out_freedpwq:
	destroy_workqueue(dev_priv->hotplug.dp_wq);
out_freewq:
	destroy_workqueue(dev_priv->wq);
out_mtrrfree:
	arch_phys_wc_del(dev_priv->gtt.mtrr);
	io_mapping_free(dev_priv->gtt.mappable);
out_gtt:
	i915_global_gtt_cleanup(dev);
out_freecsr:
	intel_csr_ucode_fini(dev);
	intel_uncore_fini(dev);
	pci_iounmap(dev->pdev, dev_priv->regs);
put_bridge:
	pci_dev_put(dev_priv->bridge_dev);
free_priv:
	kmem_cache_destroy(dev_priv->requests);
	kmem_cache_destroy(dev_priv->vmas);
	kmem_cache_destroy(dev_priv->objects);
	kfree(dev_priv);
	return ret;
}

int i915_driver_unload(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (dev_priv->mmio_map)
		drm_rmmap(dev, dev_priv->mmio_map);

	drm_free(dev->dev_private, sizeof(drm_i915_private_t),
		 DRM_MEM_DRIVER);
	int ret;

	i915_audio_component_cleanup(dev_priv);

	ret = i915_gem_suspend(dev);
	if (ret) {
		DRM_ERROR("failed to idle hardware: %d\n", ret);
		return ret;
	}

	intel_power_domains_fini(dev_priv);

	intel_gpu_ips_teardown();

	i915_teardown_sysfs(dev);

	WARN_ON(unregister_oom_notifier(&dev_priv->mm.oom_notifier));
	unregister_shrinker(&dev_priv->mm.shrinker);

	io_mapping_free(dev_priv->gtt.mappable);
	arch_phys_wc_del(dev_priv->gtt.mtrr);

	acpi_video_unregister();

	intel_fbdev_fini(dev);

	drm_vblank_cleanup(dev);

	intel_modeset_cleanup(dev);

	/*
	 * free the memory space allocated for the child device
	 * config parsed from VBT
	 */
	if (dev_priv->vbt.child_dev && dev_priv->vbt.child_dev_num) {
		kfree(dev_priv->vbt.child_dev);
		dev_priv->vbt.child_dev = NULL;
		dev_priv->vbt.child_dev_num = 0;
	}
	kfree(dev_priv->vbt.sdvo_lvds_vbt_mode);
	dev_priv->vbt.sdvo_lvds_vbt_mode = NULL;
	kfree(dev_priv->vbt.lfp_lvds_vbt_mode);
	dev_priv->vbt.lfp_lvds_vbt_mode = NULL;

	vga_switcheroo_unregister_client(dev->pdev);
	vga_client_register(dev->pdev, NULL, NULL, NULL);

	/* Free error state after interrupts are fully disabled. */
	cancel_delayed_work_sync(&dev_priv->gpu_error.hangcheck_work);
	i915_destroy_error_state(dev);

	if (dev->pdev->msi_enabled)
		pci_disable_msi(dev->pdev);

	intel_opregion_fini(dev);

	/* Flush any outstanding unpin_work. */
	flush_workqueue(dev_priv->wq);

	intel_guc_ucode_fini(dev);
	mutex_lock(&dev->struct_mutex);
	i915_gem_cleanup_ringbuffer(dev);
	i915_gem_context_fini(dev);
	mutex_unlock(&dev->struct_mutex);
	intel_fbc_cleanup_cfb(dev_priv);
	i915_gem_cleanup_stolen(dev);

	intel_csr_ucode_fini(dev);

	intel_teardown_mchbar(dev);

	destroy_workqueue(dev_priv->hotplug.dp_wq);
	destroy_workqueue(dev_priv->wq);
	destroy_workqueue(dev_priv->gpu_error.hangcheck_wq);
	pm_qos_remove_request(&dev_priv->pm_qos);

	i915_global_gtt_cleanup(dev);

	intel_uncore_fini(dev);
	if (dev_priv->regs != NULL)
		pci_iounmap(dev->pdev, dev_priv->regs);

	kmem_cache_destroy(dev_priv->requests);
	kmem_cache_destroy(dev_priv->vmas);
	kmem_cache_destroy(dev_priv->objects);
	pci_dev_put(dev_priv->bridge_dev);
	kfree(dev_priv);

	return 0;
}

void i915_driver_lastclose(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;

	if (!dev_priv)
		return;

	if (dev_priv->agp_heap)
		i915_mem_takedown(&(dev_priv->agp_heap));

	i915_dma_cleanup(dev);
}

void i915_driver_preclose(struct drm_device * dev, struct drm_file *file_priv)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	i915_mem_release(dev, file_priv, dev_priv->agp_heap);
}

struct drm_ioctl_desc i915_ioctls[] = {
	DRM_IOCTL_DEF(DRM_I915_INIT, i915_dma_init, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_I915_FLUSH, i915_flush_ioctl, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I915_FLIP, i915_flip_bufs, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I915_BATCHBUFFER, i915_batchbuffer, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I915_IRQ_EMIT, i915_irq_emit, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I915_IRQ_WAIT, i915_irq_wait, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I915_GETPARAM, i915_getparam, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I915_SETPARAM, i915_setparam, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_I915_ALLOC, i915_mem_alloc, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I915_FREE, i915_mem_free, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I915_INIT_HEAP, i915_mem_init_heap, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_I915_CMDBUFFER, i915_cmdbuffer, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I915_DESTROY_HEAP,  i915_mem_destroy_heap, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY ),
	DRM_IOCTL_DEF(DRM_I915_SET_VBLANK_PIPE,  i915_vblank_pipe_set, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY ),
	DRM_IOCTL_DEF(DRM_I915_GET_VBLANK_PIPE,  i915_vblank_pipe_get, DRM_AUTH ),
	DRM_IOCTL_DEF(DRM_I915_VBLANK_SWAP, i915_vblank_swap, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I915_HWS_ADDR, i915_set_status_page, DRM_AUTH),
};

int i915_max_ioctl = DRM_ARRAY_SIZE(i915_ioctls);

/**
 * Determine if the device really is AGP or not.
 *
 * All Intel graphics chipsets are treated as AGP, even if they are really
 * PCI-e.
 *
 * \param dev   The device to be tested.
 *
 * \returns
 * A value of 1 is always retured to indictate every i9x5 is AGP.
 */
int i915_driver_device_is_agp(struct drm_device * dev)
{
	return 1;
}
int i915_driver_open(struct drm_device *dev, struct drm_file *file)
{
	int ret;

	ret = i915_gem_open(dev, file);
	if (ret)
		return ret;

	return 0;
}

/**
 * i915_driver_lastclose - clean up after all DRM clients have exited
 * @dev: DRM device
 *
 * Take care of cleaning up after all DRM clients have exited.  In the
 * mode setting case, we want to restore the kernel's initial mode (just
 * in case the last client left us in a bad state).
 *
 * Additionally, in the non-mode setting case, we'll tear down the GTT
 * and DMA structures, since the kernel won't be using them, and clea
 * up any GEM state.
 */
void i915_driver_lastclose(struct drm_device *dev)
{
	intel_fbdev_restore_mode(dev);
	vga_switcheroo_process_delayed_switch();
}

void i915_driver_preclose(struct drm_device *dev, struct drm_file *file)
{
	mutex_lock(&dev->struct_mutex);
	i915_gem_context_close(dev, file);
	i915_gem_release(dev, file);
	mutex_unlock(&dev->struct_mutex);

	intel_modeset_preclose(dev, file);
}

void i915_driver_postclose(struct drm_device *dev, struct drm_file *file)
{
	struct drm_i915_file_private *file_priv = file->driver_priv;

	if (file_priv && file_priv->bsd_ring)
		file_priv->bsd_ring = NULL;
	kfree(file_priv);
}

static int
i915_gem_reject_pin_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file)
{
	return -ENODEV;
}

const struct drm_ioctl_desc i915_ioctls[] = {
	DRM_IOCTL_DEF_DRV(I915_INIT, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_FLUSH, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_FLIP, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_BATCHBUFFER, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_IRQ_EMIT, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_IRQ_WAIT, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_GETPARAM, i915_getparam, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_SETPARAM, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_ALLOC, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_FREE, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_INIT_HEAP, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_CMDBUFFER, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_DESTROY_HEAP,  drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_SET_VBLANK_PIPE,  drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_GET_VBLANK_PIPE,  drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_VBLANK_SWAP, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_HWS_ADDR, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_GEM_INIT, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_GEM_EXECBUFFER, i915_gem_execbuffer, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_GEM_EXECBUFFER2, i915_gem_execbuffer2, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_PIN, i915_gem_reject_pin_ioctl, DRM_AUTH|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_GEM_UNPIN, i915_gem_reject_pin_ioctl, DRM_AUTH|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_GEM_BUSY, i915_gem_busy_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_SET_CACHING, i915_gem_set_caching_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_GET_CACHING, i915_gem_get_caching_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_THROTTLE, i915_gem_throttle_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_ENTERVT, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_GEM_LEAVEVT, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_GEM_CREATE, i915_gem_create_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_PREAD, i915_gem_pread_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_PWRITE, i915_gem_pwrite_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_MMAP, i915_gem_mmap_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_MMAP_GTT, i915_gem_mmap_gtt_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_SET_DOMAIN, i915_gem_set_domain_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_SW_FINISH, i915_gem_sw_finish_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_SET_TILING, i915_gem_set_tiling, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_GET_TILING, i915_gem_get_tiling, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_GET_APERTURE, i915_gem_get_aperture_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GET_PIPE_FROM_CRTC_ID, intel_get_pipe_from_crtc_id, 0),
	DRM_IOCTL_DEF_DRV(I915_GEM_MADVISE, i915_gem_madvise_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_OVERLAY_PUT_IMAGE, intel_overlay_put_image, DRM_MASTER|DRM_CONTROL_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_OVERLAY_ATTRS, intel_overlay_attrs, DRM_MASTER|DRM_CONTROL_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_SET_SPRITE_COLORKEY, intel_sprite_set_colorkey, DRM_MASTER|DRM_CONTROL_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GET_SPRITE_COLORKEY, drm_noop, DRM_MASTER|DRM_CONTROL_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_WAIT, i915_gem_wait_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_CONTEXT_CREATE, i915_gem_context_create_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_CONTEXT_DESTROY, i915_gem_context_destroy_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_REG_READ, i915_reg_read_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GET_RESET_STATS, i915_get_reset_stats_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_USERPTR, i915_gem_userptr_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_CONTEXT_GETPARAM, i915_gem_context_getparam_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_CONTEXT_SETPARAM, i915_gem_context_setparam_ioctl, DRM_RENDER_ALLOW),
};

int i915_max_ioctl = ARRAY_SIZE(i915_ioctls);
