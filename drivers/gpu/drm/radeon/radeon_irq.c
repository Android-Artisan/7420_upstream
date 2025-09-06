/* radeon_irq.c -- IRQ handling for radeon -*- linux-c -*- */
/*
 * Copyright (C) The Weather Channel, Inc.  2002.  All Rights Reserved.
 *
 * The Weather Channel (TM) funded Tungsten Graphics to develop the
 * initial release of the Radeon 8500 driver under the XFree86 license.
 * This notice must be preserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Keith Whitwell <keith@tungstengraphics.com>
 *    Michel Dänzer <michel@daenzer.net>
 */

#include "drmP.h"
#include "drm.h"
#include "radeon_drm.h"
#include "radeon_drv.h"

static __inline__ u32 radeon_acknowledge_irqs(drm_radeon_private_t * dev_priv,
					      u32 mask)
{
	u32 irqs = RADEON_READ(RADEON_GEN_INT_STATUS) & mask;
	if (irqs)
		RADEON_WRITE(RADEON_GEN_INT_STATUS, irqs);
 *    Michel D�zer <michel@daenzer.net>
 *
 * ------------------------ This file is DEPRECATED! -------------------------
 */

#include <drm/drmP.h>
#include <drm/radeon_drm.h>
#include "radeon_drv.h"

void radeon_irq_set_state(struct drm_device *dev, u32 mask, int state)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;

	if (state)
		dev_priv->irq_enable_reg |= mask;
	else
		dev_priv->irq_enable_reg &= ~mask;

	if (dev->irq_enabled)
		RADEON_WRITE(RADEON_GEN_INT_CNTL, dev_priv->irq_enable_reg);
}

static void r500_vbl_irq_set_state(struct drm_device *dev, u32 mask, int state)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;

	if (state)
		dev_priv->r500_disp_irq_reg |= mask;
	else
		dev_priv->r500_disp_irq_reg &= ~mask;

	if (dev->irq_enabled)
		RADEON_WRITE(R500_DxMODE_INT_MASK, dev_priv->r500_disp_irq_reg);
}

int radeon_enable_vblank(struct drm_device *dev, unsigned int pipe)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;

	if ((dev_priv->flags & RADEON_FAMILY_MASK) >= CHIP_RS600) {
		switch (pipe) {
		case 0:
			r500_vbl_irq_set_state(dev, R500_D1MODE_INT_MASK, 1);
			break;
		case 1:
			r500_vbl_irq_set_state(dev, R500_D2MODE_INT_MASK, 1);
			break;
		default:
			DRM_ERROR("tried to enable vblank on non-existent crtc %u\n",
				  pipe);
			return -EINVAL;
		}
	} else {
		switch (pipe) {
		case 0:
			radeon_irq_set_state(dev, RADEON_CRTC_VBLANK_MASK, 1);
			break;
		case 1:
			radeon_irq_set_state(dev, RADEON_CRTC2_VBLANK_MASK, 1);
			break;
		default:
			DRM_ERROR("tried to enable vblank on non-existent crtc %u\n",
				  pipe);
			return -EINVAL;
		}
	}

	return 0;
}

void radeon_disable_vblank(struct drm_device *dev, unsigned int pipe)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;

	if ((dev_priv->flags & RADEON_FAMILY_MASK) >= CHIP_RS600) {
		switch (pipe) {
		case 0:
			r500_vbl_irq_set_state(dev, R500_D1MODE_INT_MASK, 0);
			break;
		case 1:
			r500_vbl_irq_set_state(dev, R500_D2MODE_INT_MASK, 0);
			break;
		default:
			DRM_ERROR("tried to enable vblank on non-existent crtc %u\n",
				  pipe);
			break;
		}
	} else {
		switch (pipe) {
		case 0:
			radeon_irq_set_state(dev, RADEON_CRTC_VBLANK_MASK, 0);
			break;
		case 1:
			radeon_irq_set_state(dev, RADEON_CRTC2_VBLANK_MASK, 0);
			break;
		default:
			DRM_ERROR("tried to enable vblank on non-existent crtc %u\n",
				  pipe);
			break;
		}
	}
}

static u32 radeon_acknowledge_irqs(drm_radeon_private_t *dev_priv, u32 *r500_disp_int)
{
	u32 irqs = RADEON_READ(RADEON_GEN_INT_STATUS);
	u32 irq_mask = RADEON_SW_INT_TEST;

	*r500_disp_int = 0;
	if ((dev_priv->flags & RADEON_FAMILY_MASK) >= CHIP_RS600) {
		/* vbl interrupts in a different place */

		if (irqs & R500_DISPLAY_INT_STATUS) {
			/* if a display interrupt */
			u32 disp_irq;

			disp_irq = RADEON_READ(R500_DISP_INTERRUPT_STATUS);

			*r500_disp_int = disp_irq;
			if (disp_irq & R500_D1_VBLANK_INTERRUPT)
				RADEON_WRITE(R500_D1MODE_VBLANK_STATUS, R500_VBLANK_ACK);
			if (disp_irq & R500_D2_VBLANK_INTERRUPT)
				RADEON_WRITE(R500_D2MODE_VBLANK_STATUS, R500_VBLANK_ACK);
		}
		irq_mask |= R500_DISPLAY_INT_STATUS;
	} else
		irq_mask |= RADEON_CRTC_VBLANK_STAT | RADEON_CRTC2_VBLANK_STAT;

	irqs &=	irq_mask;

	if (irqs)
		RADEON_WRITE(RADEON_GEN_INT_STATUS, irqs);

	return irqs;
}

/* Interrupts - Used for device synchronization and flushing in the
 * following circumstances:
 *
 * - Exclusive FB access with hw idle:
 *    - Wait for GUI Idle (?) interrupt, then do normal flush.
 *
 * - Frame throttling, NV_fence:
 *    - Drop marker irq's into command stream ahead of time.
 *    - Wait on irq's with lock *not held*
 *    - Check each for termination condition
 *
 * - Internally in cp_getbuffer, etc:
 *    - as above, but wait with lock held???
 *
 * NOTE: These functions are misleadingly named -- the irq's aren't
 * tied to dma at all, this is just a hangover from dri prehistory.
 */

irqreturn_t radeon_driver_irq_handler(DRM_IRQ_ARGS)
irqreturn_t radeon_driver_irq_handler(int irq, void *arg)
{
	struct drm_device *dev = (struct drm_device *) arg;
	drm_radeon_private_t *dev_priv =
	    (drm_radeon_private_t *) dev->dev_private;
	u32 stat;
	u32 r500_disp_int;

	if ((dev_priv->flags & RADEON_FAMILY_MASK) >= CHIP_R600)
		return IRQ_NONE;

	/* Only consider the bits we're interested in - others could be used
	 * outside the DRM
	 */
	stat = radeon_acknowledge_irqs(dev_priv, (RADEON_SW_INT_TEST_ACK |
						  RADEON_CRTC_VBLANK_STAT |
						  RADEON_CRTC2_VBLANK_STAT));
	stat = radeon_acknowledge_irqs(dev_priv, &r500_disp_int);
	if (!stat)
		return IRQ_NONE;

	stat &= dev_priv->irq_enable_reg;

	/* SW interrupt */
	if (stat & RADEON_SW_INT_TEST) {
		DRM_WAKEUP(&dev_priv->swi_queue);
	}

	/* VBLANK interrupt */
	if (stat & (RADEON_CRTC_VBLANK_STAT|RADEON_CRTC2_VBLANK_STAT)) {
		int vblank_crtc = dev_priv->vblank_crtc;

		if ((vblank_crtc &
		     (DRM_RADEON_VBLANK_CRTC1 | DRM_RADEON_VBLANK_CRTC2)) ==
		    (DRM_RADEON_VBLANK_CRTC1 | DRM_RADEON_VBLANK_CRTC2)) {
			if (stat & RADEON_CRTC_VBLANK_STAT)
				atomic_inc(&dev->vbl_received);
			if (stat & RADEON_CRTC2_VBLANK_STAT)
				atomic_inc(&dev->vbl_received2);
		} else if (((stat & RADEON_CRTC_VBLANK_STAT) &&
			   (vblank_crtc & DRM_RADEON_VBLANK_CRTC1)) ||
			   ((stat & RADEON_CRTC2_VBLANK_STAT) &&
			    (vblank_crtc & DRM_RADEON_VBLANK_CRTC2)))
			atomic_inc(&dev->vbl_received);

		DRM_WAKEUP(&dev->vbl_queue);
		drm_vbl_send_signals(dev);
	}

	if (stat & RADEON_SW_INT_TEST)
		wake_up(&dev_priv->swi_queue);

	/* VBLANK interrupt */
	if ((dev_priv->flags & RADEON_FAMILY_MASK) >= CHIP_RS600) {
		if (r500_disp_int & R500_D1_VBLANK_INTERRUPT)
			drm_handle_vblank(dev, 0);
		if (r500_disp_int & R500_D2_VBLANK_INTERRUPT)
			drm_handle_vblank(dev, 1);
	} else {
		if (stat & RADEON_CRTC_VBLANK_STAT)
			drm_handle_vblank(dev, 0);
		if (stat & RADEON_CRTC2_VBLANK_STAT)
			drm_handle_vblank(dev, 1);
	}
	return IRQ_HANDLED;
}

static int radeon_emit_irq(struct drm_device * dev)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	unsigned int ret;
	RING_LOCALS;

	atomic_inc(&dev_priv->swi_emitted);
	ret = atomic_read(&dev_priv->swi_emitted);

	BEGIN_RING(4);
	OUT_RING_REG(RADEON_LAST_SWI_REG, ret);
	OUT_RING_REG(RADEON_GEN_INT_STATUS, RADEON_SW_INT_FIRE);
	ADVANCE_RING();
	COMMIT_RING();

	return ret;
}

static int radeon_wait_irq(struct drm_device * dev, int swi_nr)
{
	drm_radeon_private_t *dev_priv =
	    (drm_radeon_private_t *) dev->dev_private;
	int ret = 0;

	if (RADEON_READ(RADEON_LAST_SWI_REG) >= swi_nr)
		return 0;

	dev_priv->stats.boxes |= RADEON_BOX_WAIT_IDLE;

	DRM_WAIT_ON(ret, dev_priv->swi_queue, 3 * DRM_HZ,
	DRM_WAIT_ON(ret, dev_priv->swi_queue, 3 * HZ,
		    RADEON_READ(RADEON_LAST_SWI_REG) >= swi_nr);

	return ret;
}

static int radeon_driver_vblank_do_wait(struct drm_device * dev,
					unsigned int *sequence, int crtc)
{
	drm_radeon_private_t *dev_priv =
	    (drm_radeon_private_t *) dev->dev_private;
	unsigned int cur_vblank;
	int ret = 0;
	int ack = 0;
	atomic_t *counter;
u32 radeon_get_vblank_counter(struct drm_device *dev, unsigned int pipe)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	if (crtc == DRM_RADEON_VBLANK_CRTC1) {
		counter = &dev->vbl_received;
		ack |= RADEON_CRTC_VBLANK_STAT;
	} else if (crtc == DRM_RADEON_VBLANK_CRTC2) {
		counter = &dev->vbl_received2;
		ack |= RADEON_CRTC2_VBLANK_STAT;
	} else
		return -EINVAL;

	radeon_acknowledge_irqs(dev_priv, ack);

	dev_priv->stats.boxes |= RADEON_BOX_WAIT_IDLE;

	/* Assume that the user has missed the current sequence number
	 * by about a day rather than she wants to wait for years
	 * using vertical blanks...
	 */
	DRM_WAIT_ON(ret, dev->vbl_queue, 3 * DRM_HZ,
		    (((cur_vblank = atomic_read(counter))
		      - *sequence) <= (1 << 23)));

	*sequence = cur_vblank;

	return ret;
}

int radeon_driver_vblank_wait(struct drm_device *dev, unsigned int *sequence)
{
	return radeon_driver_vblank_do_wait(dev, sequence, DRM_RADEON_VBLANK_CRTC1);
}

int radeon_driver_vblank_wait2(struct drm_device *dev, unsigned int *sequence)
{
	return radeon_driver_vblank_do_wait(dev, sequence, DRM_RADEON_VBLANK_CRTC2);
	if (pipe > 1) {
		DRM_ERROR("Invalid crtc %u\n", pipe);
		return -EINVAL;
	}

	if ((dev_priv->flags & RADEON_FAMILY_MASK) >= CHIP_RS600) {
		if (pipe == 0)
			return RADEON_READ(R500_D1CRTC_FRAME_COUNT);
		else
			return RADEON_READ(R500_D2CRTC_FRAME_COUNT);
	} else {
		if (pipe == 0)
			return RADEON_READ(RADEON_CRTC_CRNT_FRAME);
		else
			return RADEON_READ(RADEON_CRTC2_CRNT_FRAME);
	}
}

/* Needs the lock as it touches the ring.
 */
int radeon_irq_emit(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_radeon_irq_emit_t *emit = data;
	int result;

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	result = radeon_emit_irq(dev);

	if (DRM_COPY_TO_USER(emit->irq_seq, &result, sizeof(int))) {
	if ((dev_priv->flags & RADEON_FAMILY_MASK) >= CHIP_R600)
		return -EINVAL;

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	result = radeon_emit_irq(dev);

	if (copy_to_user(emit->irq_seq, &result, sizeof(int))) {
		DRM_ERROR("copy_to_user\n");
		return -EFAULT;
	}

	return 0;
}

/* Doesn't need the hardware lock.
 */
int radeon_irq_wait(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_radeon_irq_wait_t *irqwait = data;

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	return radeon_wait_irq(dev, irqwait->irq_seq);
}

void radeon_enable_interrupt(struct drm_device *dev)
{
	drm_radeon_private_t *dev_priv = (drm_radeon_private_t *) dev->dev_private;

	dev_priv->irq_enable_reg = RADEON_SW_INT_ENABLE;
	if (dev_priv->vblank_crtc & DRM_RADEON_VBLANK_CRTC1)
		dev_priv->irq_enable_reg |= RADEON_CRTC_VBLANK_MASK;

	if (dev_priv->vblank_crtc & DRM_RADEON_VBLANK_CRTC2)
		dev_priv->irq_enable_reg |= RADEON_CRTC2_VBLANK_MASK;

	RADEON_WRITE(RADEON_GEN_INT_CNTL, dev_priv->irq_enable_reg);
	dev_priv->irq_enabled = 1;
}

	if ((dev_priv->flags & RADEON_FAMILY_MASK) >= CHIP_R600)
		return -EINVAL;

	return radeon_wait_irq(dev, irqwait->irq_seq);
}

/* drm_dma.h hooks
*/
void radeon_driver_irq_preinstall(struct drm_device * dev)
{
	drm_radeon_private_t *dev_priv =
	    (drm_radeon_private_t *) dev->dev_private;

	/* Disable *all* interrupts */
	RADEON_WRITE(RADEON_GEN_INT_CNTL, 0);

	/* Clear bits if they're already high */
	radeon_acknowledge_irqs(dev_priv, (RADEON_SW_INT_TEST_ACK |
					   RADEON_CRTC_VBLANK_STAT |
					   RADEON_CRTC2_VBLANK_STAT));
}

void radeon_driver_irq_postinstall(struct drm_device * dev)
	u32 dummy;

	if ((dev_priv->flags & RADEON_FAMILY_MASK) >= CHIP_R600)
		return;

	/* Disable *all* interrupts */
	if ((dev_priv->flags & RADEON_FAMILY_MASK) >= CHIP_RS600)
		RADEON_WRITE(R500_DxMODE_INT_MASK, 0);
	RADEON_WRITE(RADEON_GEN_INT_CNTL, 0);

	/* Clear bits if they're already high */
	radeon_acknowledge_irqs(dev_priv, &dummy);
}

int radeon_driver_irq_postinstall(struct drm_device *dev)
{
	drm_radeon_private_t *dev_priv =
	    (drm_radeon_private_t *) dev->dev_private;

	atomic_set(&dev_priv->swi_emitted, 0);
	DRM_INIT_WAITQUEUE(&dev_priv->swi_queue);

	radeon_enable_interrupt(dev);
	init_waitqueue_head(&dev_priv->swi_queue);

	dev->max_vblank_count = 0x001fffff;

	if ((dev_priv->flags & RADEON_FAMILY_MASK) >= CHIP_R600)
		return 0;

	radeon_irq_set_state(dev, RADEON_SW_INT_ENABLE, 1);

	return 0;
}

void radeon_driver_irq_uninstall(struct drm_device * dev)
{
	drm_radeon_private_t *dev_priv =
	    (drm_radeon_private_t *) dev->dev_private;
	if (!dev_priv)
		return;

	dev_priv->irq_enabled = 0;

	if ((dev_priv->flags & RADEON_FAMILY_MASK) >= CHIP_R600)
		return;

	if ((dev_priv->flags & RADEON_FAMILY_MASK) >= CHIP_RS600)
		RADEON_WRITE(R500_DxMODE_INT_MASK, 0);
	/* Disable *all* interrupts */
	RADEON_WRITE(RADEON_GEN_INT_CNTL, 0);
}


int radeon_vblank_crtc_get(struct drm_device *dev)
{
	drm_radeon_private_t *dev_priv = (drm_radeon_private_t *) dev->dev_private;
	u32 flag;
	u32 value;

	flag = RADEON_READ(RADEON_GEN_INT_CNTL);
	value = 0;

	if (flag & RADEON_CRTC_VBLANK_MASK)
		value |= DRM_RADEON_VBLANK_CRTC1;

	if (flag & RADEON_CRTC2_VBLANK_MASK)
		value |= DRM_RADEON_VBLANK_CRTC2;
	return value;

	return dev_priv->vblank_crtc;
}

int radeon_vblank_crtc_set(struct drm_device *dev, int64_t value)
{
	drm_radeon_private_t *dev_priv = (drm_radeon_private_t *) dev->dev_private;
	if (value & ~(DRM_RADEON_VBLANK_CRTC1 | DRM_RADEON_VBLANK_CRTC2)) {
		DRM_ERROR("called with invalid crtc 0x%x\n", (unsigned int)value);
		return -EINVAL;
	}
	dev_priv->vblank_crtc = (unsigned int)value;
	radeon_enable_interrupt(dev);
	return 0;
}
