/*
 * PCI Express PCI Hot Plug Driver
 *
 * Copyright (C) 1995,2001 Compaq Computer Corporation
 * Copyright (C) 2001 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2001 IBM Corp.
 * Copyright (C) 2003-2004 Intel Corporation
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Send feedback to <greg@kroah.com>,<kristen.c.accardi@intel.com>
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/signal.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/slab.h>

#include "../pci.h"
#include "pciehp.h"

static atomic_t pciehp_num_controllers = ATOMIC_INIT(0);

struct ctrl_reg {
	u8 cap_id;
	u8 nxt_ptr;
	u16 cap_reg;
	u32 dev_cap;
	u16 dev_ctrl;
	u16 dev_status;
	u32 lnk_cap;
	u16 lnk_ctrl;
	u16 lnk_status;
	u32 slot_cap;
	u16 slot_ctrl;
	u16 slot_status;
	u16 root_ctrl;
	u16 rsvp;
	u32 root_status;
} __attribute__ ((packed));

/* offsets to the controller registers based on the above structure layout */
enum ctrl_offsets {
	PCIECAPID	=	offsetof(struct ctrl_reg, cap_id),
	NXTCAPPTR	=	offsetof(struct ctrl_reg, nxt_ptr),
	CAPREG		=	offsetof(struct ctrl_reg, cap_reg),
	DEVCAP		=	offsetof(struct ctrl_reg, dev_cap),
	DEVCTRL		=	offsetof(struct ctrl_reg, dev_ctrl),
	DEVSTATUS	=	offsetof(struct ctrl_reg, dev_status),
	LNKCAP		=	offsetof(struct ctrl_reg, lnk_cap),
	LNKCTRL		=	offsetof(struct ctrl_reg, lnk_ctrl),
	LNKSTATUS	=	offsetof(struct ctrl_reg, lnk_status),
	SLOTCAP		=	offsetof(struct ctrl_reg, slot_cap),
	SLOTCTRL	=	offsetof(struct ctrl_reg, slot_ctrl),
	SLOTSTATUS	=	offsetof(struct ctrl_reg, slot_status),
	ROOTCTRL	=	offsetof(struct ctrl_reg, root_ctrl),
	ROOTSTATUS	=	offsetof(struct ctrl_reg, root_status),
};

static inline int pciehp_readw(struct controller *ctrl, int reg, u16 *value)
{
	struct pci_dev *dev = ctrl->pci_dev;
	return pci_read_config_word(dev, ctrl->cap_base + reg, value);
}

static inline int pciehp_readl(struct controller *ctrl, int reg, u32 *value)
{
	struct pci_dev *dev = ctrl->pci_dev;
	return pci_read_config_dword(dev, ctrl->cap_base + reg, value);
}

static inline int pciehp_writew(struct controller *ctrl, int reg, u16 value)
{
	struct pci_dev *dev = ctrl->pci_dev;
	return pci_write_config_word(dev, ctrl->cap_base + reg, value);
}

static inline int pciehp_writel(struct controller *ctrl, int reg, u32 value)
{
	struct pci_dev *dev = ctrl->pci_dev;
	return pci_write_config_dword(dev, ctrl->cap_base + reg, value);
}

/* Field definitions in PCI Express Capabilities Register */
#define CAP_VER			0x000F
#define DEV_PORT_TYPE		0x00F0
#define SLOT_IMPL		0x0100
#define MSG_NUM			0x3E00

/* Device or Port Type */
#define NAT_ENDPT		0x00
#define LEG_ENDPT		0x01
#define ROOT_PORT		0x04
#define UP_STREAM		0x05
#define	DN_STREAM		0x06
#define PCIE_PCI_BRDG		0x07
#define PCI_PCIE_BRDG		0x10

/* Field definitions in Device Capabilities Register */
#define DATTN_BUTTN_PRSN	0x1000
#define DATTN_LED_PRSN		0x2000
#define DPWR_LED_PRSN		0x4000

/* Field definitions in Link Capabilities Register */
#define MAX_LNK_SPEED		0x000F
#define MAX_LNK_WIDTH		0x03F0

/* Link Width Encoding */
#define LNK_X1		0x01
#define LNK_X2		0x02
#define LNK_X4		0x04
#define LNK_X8		0x08
#define LNK_X12		0x0C
#define LNK_X16		0x10
#define LNK_X32		0x20

/*Field definitions of Link Status Register */
#define LNK_SPEED	0x000F
#define NEG_LINK_WD	0x03F0
#define LNK_TRN_ERR	0x0400
#define	LNK_TRN		0x0800
#define SLOT_CLK_CONF	0x1000

/* Field definitions in Slot Capabilities Register */
#define ATTN_BUTTN_PRSN	0x00000001
#define	PWR_CTRL_PRSN	0x00000002
#define MRL_SENS_PRSN	0x00000004
#define ATTN_LED_PRSN	0x00000008
#define PWR_LED_PRSN	0x00000010
#define HP_SUPR_RM_SUP	0x00000020
#define HP_CAP		0x00000040
#define SLOT_PWR_VALUE	0x000003F8
#define SLOT_PWR_LIMIT	0x00000C00
#define PSN		0xFFF80000	/* PSN: Physical Slot Number */

/* Field definitions in Slot Control Register */
#define ATTN_BUTTN_ENABLE		0x0001
#define PWR_FAULT_DETECT_ENABLE		0x0002
#define MRL_DETECT_ENABLE		0x0004
#define PRSN_DETECT_ENABLE		0x0008
#define CMD_CMPL_INTR_ENABLE		0x0010
#define HP_INTR_ENABLE			0x0020
#define ATTN_LED_CTRL			0x00C0
#define PWR_LED_CTRL			0x0300
#define PWR_CTRL			0x0400
#define EMI_CTRL			0x0800

/* Attention indicator and Power indicator states */
#define LED_ON		0x01
#define LED_BLINK	0x10
#define LED_OFF		0x11

/* Power Control Command */
#define POWER_ON	0
#define POWER_OFF	0x0400

/* EMI Status defines */
#define EMI_DISENGAGED	0
#define EMI_ENGAGED	1

/* Field definitions in Slot Status Register */
#define ATTN_BUTTN_PRESSED	0x0001
#define PWR_FAULT_DETECTED	0x0002
#define MRL_SENS_CHANGED	0x0004
#define PRSN_DETECT_CHANGED	0x0008
#define CMD_COMPLETED		0x0010
#define MRL_STATE		0x0020
#define PRSN_STATE		0x0040
#define EMI_STATE		0x0080
#define EMI_STATUS_BIT		7

static inline struct pci_dev *ctrl_dev(struct controller *ctrl)
{
	return ctrl->pcie->port;
}

static irqreturn_t pcie_isr(int irq, void *dev_id);
static void start_int_poll_timer(struct controller *ctrl, int sec);

/* This is the interrupt polling timeout function. */
static void int_poll_timeout(unsigned long data)
{
	struct controller *ctrl = (struct controller *)data;

	/* Poll for interrupt events.  regs == NULL => polling */
	pcie_isr(0, ctrl);

	init_timer(&ctrl->poll_timer);
	if (!pciehp_poll_time)
		pciehp_poll_time = 2; /* default polling interval is 2 sec */

	start_int_poll_timer(ctrl, pciehp_poll_time);
}

/* This function starts the interrupt polling timer. */
static void start_int_poll_timer(struct controller *ctrl, int sec)
{
	/* Clamp to sane value */
	if ((sec <= 0) || (sec > 60))
        	sec = 2;
		sec = 2;

	ctrl->poll_timer.function = &int_poll_timeout;
	ctrl->poll_timer.data = (unsigned long)ctrl;
	ctrl->poll_timer.expires = jiffies + sec * HZ;
	add_timer(&ctrl->poll_timer);
}

static inline int pciehp_request_irq(struct controller *ctrl)
{
	int retval, irq = ctrl->pci_dev->irq;
	int retval, irq = ctrl->pcie->irq;

	/* Install interrupt polling timer. Start with 10 sec delay */
	if (pciehp_poll_mode) {
		init_timer(&ctrl->poll_timer);
		start_int_poll_timer(ctrl, 10);
		return 0;
	}

	/* Installs the interrupt handler */
	retval = request_irq(irq, pcie_isr, IRQF_SHARED, MY_NAME, ctrl);
	if (retval)
		err("Cannot get irq %d for the hotplug controller\n", irq);
		ctrl_err(ctrl, "Cannot get irq %d for the hotplug controller\n",
			 irq);
	return retval;
}

static inline void pciehp_free_irq(struct controller *ctrl)
{
	if (pciehp_poll_mode)
		del_timer_sync(&ctrl->poll_timer);
	else
		free_irq(ctrl->pci_dev->irq, ctrl);
}

static int pcie_poll_cmd(struct controller *ctrl)
{
	u16 slot_status;
	int timeout = 1000;

	if (!pciehp_readw(ctrl, SLOTSTATUS, &slot_status)) {
		if (slot_status & CMD_COMPLETED) {
			pciehp_writew(ctrl, SLOTSTATUS, CMD_COMPLETED);
			return 1;
		}
	}
	while (timeout > 0) {
		msleep(10);
		timeout -= 10;
		if (!pciehp_readw(ctrl, SLOTSTATUS, &slot_status)) {
			if (slot_status & CMD_COMPLETED) {
				pciehp_writew(ctrl, SLOTSTATUS, CMD_COMPLETED);
				return 1;
			}
		}
		free_irq(ctrl->pcie->irq, ctrl);
}

static int pcie_poll_cmd(struct controller *ctrl, int timeout)
{
	struct pci_dev *pdev = ctrl_dev(ctrl);
	u16 slot_status;

	while (true) {
		pcie_capability_read_word(pdev, PCI_EXP_SLTSTA, &slot_status);
		if (slot_status == (u16) ~0) {
			ctrl_info(ctrl, "%s: no response from device\n",
				  __func__);
			return 0;
		}

		if (slot_status & PCI_EXP_SLTSTA_CC) {
			pcie_capability_write_word(pdev, PCI_EXP_SLTSTA,
						   PCI_EXP_SLTSTA_CC);
			ctrl->cmd_busy = 0;
			smp_mb();
			return 1;
		}
		if (timeout < 0)
			break;
		msleep(10);
		timeout -= 10;
	}
	return 0;	/* timeout */
}

static void pcie_wait_cmd(struct controller *ctrl, int poll)
{
	unsigned int msecs = pciehp_poll_mode ? 2500 : 1000;
	unsigned long timeout = msecs_to_jiffies(msecs);
	int rc;

	if (poll)
		rc = pcie_poll_cmd(ctrl);
	else
		rc = wait_event_timeout(ctrl->queue, !ctrl->cmd_busy, timeout);
	if (!rc)
		dbg("Command not completed in 1000 msec\n");
static void pcie_wait_cmd(struct controller *ctrl)
{
	unsigned int msecs = pciehp_poll_mode ? 2500 : 1000;
	unsigned long duration = msecs_to_jiffies(msecs);
	unsigned long cmd_timeout = ctrl->cmd_started + duration;
	unsigned long now, timeout;
	int rc;

	/*
	 * If the controller does not generate notifications for command
	 * completions, we never need to wait between writes.
	 */
	if (NO_CMD_CMPL(ctrl))
		return;

	if (!ctrl->cmd_busy)
		return;

	/*
	 * Even if the command has already timed out, we want to call
	 * pcie_poll_cmd() so it can clear PCI_EXP_SLTSTA_CC.
	 */
	now = jiffies;
	if (time_before_eq(cmd_timeout, now))
		timeout = 1;
	else
		timeout = cmd_timeout - now;

	if (ctrl->slot_ctrl & PCI_EXP_SLTCTL_HPIE &&
	    ctrl->slot_ctrl & PCI_EXP_SLTCTL_CCIE)
		rc = wait_event_timeout(ctrl->queue, !ctrl->cmd_busy, timeout);
	else
		rc = pcie_poll_cmd(ctrl, jiffies_to_msecs(timeout));

	/*
	 * Controllers with errata like Intel CF118 don't generate
	 * completion notifications unless the power/indicator/interlock
	 * control bits are changed.  On such controllers, we'll emit this
	 * timeout message when we wait for completion of commands that
	 * don't change those bits, e.g., commands that merely enable
	 * interrupts.
	 */
	if (!rc)
		ctrl_info(ctrl, "Timeout on hotplug command %#06x (issued %u msec ago)\n",
			  ctrl->slot_ctrl,
			  jiffies_to_msecs(jiffies - ctrl->cmd_started));
}

static void pcie_do_write_cmd(struct controller *ctrl, u16 cmd,
			      u16 mask, bool wait)
{
	struct pci_dev *pdev = ctrl_dev(ctrl);
	u16 slot_ctrl;

	mutex_lock(&ctrl->ctrl_lock);

	/*
	 * Always wait for any previous command that might still be in progress
	 */
	pcie_wait_cmd(ctrl);

	pcie_capability_read_word(pdev, PCI_EXP_SLTCTL, &slot_ctrl);
	if (slot_ctrl == (u16) ~0) {
		ctrl_info(ctrl, "%s: no response from device\n", __func__);
		goto out;
	}

	slot_ctrl &= ~mask;
	slot_ctrl |= (cmd & mask);
	ctrl->cmd_busy = 1;
	smp_mb();
	pcie_capability_write_word(pdev, PCI_EXP_SLTCTL, slot_ctrl);
	ctrl->cmd_started = jiffies;
	ctrl->slot_ctrl = slot_ctrl;

	/*
	 * Optionally wait for the hardware to be ready for a new command,
	 * indicating completion of the above issued command.
	 */
	if (wait)
		pcie_wait_cmd(ctrl);

out:
	mutex_unlock(&ctrl->ctrl_lock);
}

/**
 * pcie_write_cmd - Issue controller command
 * @ctrl: controller to which the command is issued
 * @cmd:  command value written to slot control register
 * @mask: bitmask of slot control register to be modified
 */
static int pcie_write_cmd(struct controller *ctrl, u16 cmd, u16 mask)
{
	int retval = 0;
	u16 slot_status;
	u16 slot_ctrl;

	mutex_lock(&ctrl->ctrl_lock);

	retval = pciehp_readw(ctrl, SLOTSTATUS, &slot_status);
	if (retval) {
		err("%s: Cannot read SLOTSTATUS register\n", __func__);
		goto out;
	}

	if (slot_status & CMD_COMPLETED) {
		if (!ctrl->no_cmd_complete) {
			/*
			 * After 1 sec and CMD_COMPLETED still not set, just
			 * proceed forward to issue the next command according
			 * to spec. Just print out the error message.
			 */
			dbg("%s: CMD_COMPLETED not clear after 1 sec.\n",
			    __func__);
		} else if (!NO_CMD_CMPL(ctrl)) {
			/*
			 * This controller semms to notify of command completed
			 * event even though it supports none of power
			 * controller, attention led, power led and EMI.
			 */
			dbg("%s: Unexpected CMD_COMPLETED. Need to wait for "
			    "command completed event.\n", __func__);
			ctrl->no_cmd_complete = 0;
		} else {
			dbg("%s: Unexpected CMD_COMPLETED. Maybe the "
			    "controller is broken.\n", __func__);
		}
	}

	retval = pciehp_readw(ctrl, SLOTCTRL, &slot_ctrl);
	if (retval) {
		err("%s: Cannot read SLOTCTRL register\n", __func__);
		goto out;
	}

	slot_ctrl &= ~mask;
	slot_ctrl |= (cmd & mask);
	ctrl->cmd_busy = 1;
	smp_mb();
	retval = pciehp_writew(ctrl, SLOTCTRL, slot_ctrl);
	if (retval)
		err("%s: Cannot write to SLOTCTRL register\n", __func__);

	/*
	 * Wait for command completion.
	 */
	if (!retval && !ctrl->no_cmd_complete) {
		int poll = 0;
		/*
		 * if hotplug interrupt is not enabled or command
		 * completed interrupt is not enabled, we need to poll
		 * command completed event.
		 */
		if (!(slot_ctrl & HP_INTR_ENABLE) ||
		    !(slot_ctrl & CMD_CMPL_INTR_ENABLE))
			poll = 1;
                pcie_wait_cmd(ctrl, poll);
	}
 out:
	mutex_unlock(&ctrl->ctrl_lock);
	return retval;
}

static int hpc_check_lnk_status(struct controller *ctrl)
{
	u16 lnk_status;
	int retval = 0;

	retval = pciehp_readw(ctrl, LNKSTATUS, &lnk_status);
	if (retval) {
		err("%s: Cannot read LNKSTATUS register\n", __func__);
		return retval;
	}

	dbg("%s: lnk_status = %x\n", __func__, lnk_status);
	if ( (lnk_status & LNK_TRN) || (lnk_status & LNK_TRN_ERR) ||
		!(lnk_status & NEG_LINK_WD)) {
		err("%s : Link Training Error occurs \n", __func__);
		retval = -1;
		return retval;
	}

	return retval;
}

static int hpc_get_attention_status(struct slot *slot, u8 *status)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_ctrl;
	u8 atten_led_state;
	int retval = 0;

	retval = pciehp_readw(ctrl, SLOTCTRL, &slot_ctrl);
	if (retval) {
		err("%s: Cannot read SLOTCTRL register\n", __func__);
		return retval;
	}

	dbg("%s: SLOTCTRL %x, value read %x\n",
	    __func__, ctrl->cap_base + SLOTCTRL, slot_ctrl);

	atten_led_state = (slot_ctrl & ATTN_LED_CTRL) >> 6;

	switch (atten_led_state) {
	case 0:
		*status = 0xFF;	/* Reserved */
		break;
	case 1:
		*status = 1;	/* On */
		break;
	case 2:
		*status = 2;	/* Blink */
		break;
	case 3:
static void pcie_write_cmd(struct controller *ctrl, u16 cmd, u16 mask)
{
	pcie_do_write_cmd(ctrl, cmd, mask, true);
}

/* Same as above without waiting for the hardware to latch */
static void pcie_write_cmd_nowait(struct controller *ctrl, u16 cmd, u16 mask)
{
	pcie_do_write_cmd(ctrl, cmd, mask, false);
}

bool pciehp_check_link_active(struct controller *ctrl)
{
	struct pci_dev *pdev = ctrl_dev(ctrl);
	u16 lnk_status;
	bool ret;

	pcie_capability_read_word(pdev, PCI_EXP_LNKSTA, &lnk_status);
	ret = !!(lnk_status & PCI_EXP_LNKSTA_DLLLA);

	if (ret)
		ctrl_dbg(ctrl, "%s: lnk_status = %x\n", __func__, lnk_status);

	return ret;
}

static void __pcie_wait_link_active(struct controller *ctrl, bool active)
{
	int timeout = 1000;

	if (pciehp_check_link_active(ctrl) == active)
		return;
	while (timeout > 0) {
		msleep(10);
		timeout -= 10;
		if (pciehp_check_link_active(ctrl) == active)
			return;
	}
	ctrl_dbg(ctrl, "Data Link Layer Link Active not %s in 1000 msec\n",
			active ? "set" : "cleared");
}

static void pcie_wait_link_active(struct controller *ctrl)
{
	__pcie_wait_link_active(ctrl, true);
}

static bool pci_bus_check_dev(struct pci_bus *bus, int devfn)
{
	u32 l;
	int count = 0;
	int delay = 1000, step = 20;
	bool found = false;

	do {
		found = pci_bus_read_dev_vendor_id(bus, devfn, &l, 0);
		count++;

		if (found)
			break;

		msleep(step);
		delay -= step;
	} while (delay > 0);

	if (count > 1 && pciehp_debug)
		printk(KERN_DEBUG "pci %04x:%02x:%02x.%d id reading try %d times with interval %d ms to get %08x\n",
			pci_domain_nr(bus), bus->number, PCI_SLOT(devfn),
			PCI_FUNC(devfn), count, step, l);

	return found;
}

int pciehp_check_link_status(struct controller *ctrl)
{
	struct pci_dev *pdev = ctrl_dev(ctrl);
	bool found;
	u16 lnk_status;

	/*
	 * Data Link Layer Link Active Reporting must be capable for
	 * hot-plug capable downstream port. But old controller might
	 * not implement it. In this case, we wait for 1000 ms.
	*/
	if (ctrl->link_active_reporting)
		pcie_wait_link_active(ctrl);
	else
		msleep(1000);

	/* wait 100ms before read pci conf, and try in 1s */
	msleep(100);
	found = pci_bus_check_dev(ctrl->pcie->port->subordinate,
					PCI_DEVFN(0, 0));

	pcie_capability_read_word(pdev, PCI_EXP_LNKSTA, &lnk_status);
	ctrl_dbg(ctrl, "%s: lnk_status = %x\n", __func__, lnk_status);
	if ((lnk_status & PCI_EXP_LNKSTA_LT) ||
	    !(lnk_status & PCI_EXP_LNKSTA_NLW)) {
		ctrl_err(ctrl, "link training error: status %#06x\n",
			 lnk_status);
		return -1;
	}

	pcie_update_link_speed(ctrl->pcie->port->subordinate, lnk_status);

	if (!found)
		return -1;

	return 0;
}

static int __pciehp_link_set(struct controller *ctrl, bool enable)
{
	struct pci_dev *pdev = ctrl_dev(ctrl);

	pcie_capability_clear_and_set_word(pdev, PCI_EXP_LNKCTL,
					   PCI_EXP_LNKCTL_LD,
					   enable ? 0 : PCI_EXP_LNKCTL_LD);

	return 0;
}

static int pciehp_link_enable(struct controller *ctrl)
{
	return __pciehp_link_set(ctrl, true);
}

void pciehp_get_attention_status(struct slot *slot, u8 *status)
{
	struct controller *ctrl = slot->ctrl;
	struct pci_dev *pdev = ctrl_dev(ctrl);
	u16 slot_ctrl;

	pcie_capability_read_word(pdev, PCI_EXP_SLTCTL, &slot_ctrl);
	ctrl_dbg(ctrl, "%s: SLOTCTRL %x, value read %x\n", __func__,
		 pci_pcie_cap(ctrl->pcie->port) + PCI_EXP_SLTCTL, slot_ctrl);

	switch (slot_ctrl & PCI_EXP_SLTCTL_AIC) {
	case PCI_EXP_SLTCTL_ATTN_IND_ON:
		*status = 1;	/* On */
		break;
	case PCI_EXP_SLTCTL_ATTN_IND_BLINK:
		*status = 2;	/* Blink */
		break;
	case PCI_EXP_SLTCTL_ATTN_IND_OFF:
		*status = 0;	/* Off */
		break;
	default:
		*status = 0xFF;
		break;
	}

	return 0;
}

static int hpc_get_power_status(struct slot *slot, u8 *status)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_ctrl;
	u8 pwr_state;
	int	retval = 0;

	retval = pciehp_readw(ctrl, SLOTCTRL, &slot_ctrl);
	if (retval) {
		err("%s: Cannot read SLOTCTRL register\n", __func__);
		return retval;
	}
	dbg("%s: SLOTCTRL %x value read %x\n",
	    __func__, ctrl->cap_base + SLOTCTRL, slot_ctrl);

	pwr_state = (slot_ctrl & PWR_CTRL) >> 10;

	switch (pwr_state) {
	case 0:
		*status = 1;
		break;
	case 1:
		*status = 0;
}

void pciehp_get_power_status(struct slot *slot, u8 *status)
{
	struct controller *ctrl = slot->ctrl;
	struct pci_dev *pdev = ctrl_dev(ctrl);
	u16 slot_ctrl;

	pcie_capability_read_word(pdev, PCI_EXP_SLTCTL, &slot_ctrl);
	ctrl_dbg(ctrl, "%s: SLOTCTRL %x value read %x\n", __func__,
		 pci_pcie_cap(ctrl->pcie->port) + PCI_EXP_SLTCTL, slot_ctrl);

	switch (slot_ctrl & PCI_EXP_SLTCTL_PCC) {
	case PCI_EXP_SLTCTL_PWR_ON:
		*status = 1;	/* On */
		break;
	case PCI_EXP_SLTCTL_PWR_OFF:
		*status = 0;	/* Off */
		break;
	default:
		*status = 0xFF;
		break;
	}

	return retval;
}

static int hpc_get_latch_status(struct slot *slot, u8 *status)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_status;
	int retval = 0;

	retval = pciehp_readw(ctrl, SLOTSTATUS, &slot_status);
	if (retval) {
		err("%s: Cannot read SLOTSTATUS register\n", __func__);
		return retval;
	}

	*status = (((slot_status & MRL_STATE) >> 5) == 0) ? 0 : 1;

	return 0;
}

static int hpc_get_adapter_status(struct slot *slot, u8 *status)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_status;
	u8 card_state;
	int retval = 0;

	retval = pciehp_readw(ctrl, SLOTSTATUS, &slot_status);
	if (retval) {
		err("%s: Cannot read SLOTSTATUS register\n", __func__);
		return retval;
	}
	card_state = (u8)((slot_status & PRSN_STATE) >> 6);
	*status = (card_state == 1) ? 1 : 0;

	return 0;
}

static int hpc_query_power_fault(struct slot *slot)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_status;
	u8 pwr_fault;
	int retval = 0;

	retval = pciehp_readw(ctrl, SLOTSTATUS, &slot_status);
	if (retval) {
		err("%s: Cannot check for power fault\n", __func__);
		return retval;
	}
	pwr_fault = (u8)((slot_status & PWR_FAULT_DETECTED) >> 1);

	return pwr_fault;
}

static int hpc_get_emi_status(struct slot *slot, u8 *status)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_status;
	int retval = 0;

	retval = pciehp_readw(ctrl, SLOTSTATUS, &slot_status);
	if (retval) {
		err("%s : Cannot check EMI status\n", __func__);
		return retval;
	}
	*status = (slot_status & EMI_STATE) >> EMI_STATUS_BIT;

	return retval;
}

static int hpc_toggle_emi(struct slot *slot)
{
	u16 slot_cmd;
	u16 cmd_mask;
	int rc;

	slot_cmd = EMI_CTRL;
	cmd_mask = EMI_CTRL;
	rc = pcie_write_cmd(slot->ctrl, slot_cmd, cmd_mask);
	slot->last_emi_toggle = get_seconds();

	return rc;
}

static int hpc_set_attention_status(struct slot *slot, u8 value)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_cmd;
	u16 cmd_mask;
	int rc;

	cmd_mask = ATTN_LED_CTRL;
	switch (value) {
		case 0 :	/* turn off */
			slot_cmd = 0x00C0;
			break;
		case 1:		/* turn on */
			slot_cmd = 0x0040;
			break;
		case 2:		/* turn blink */
			slot_cmd = 0x0080;
			break;
		default:
			return -1;
	}
	rc = pcie_write_cmd(ctrl, slot_cmd, cmd_mask);
	dbg("%s: SLOTCTRL %x write cmd %x\n",
	    __func__, ctrl->cap_base + SLOTCTRL, slot_cmd);

	return rc;
}

static void hpc_set_green_led_on(struct slot *slot)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_cmd;
	u16 cmd_mask;

	slot_cmd = 0x0100;
	cmd_mask = PWR_LED_CTRL;
	pcie_write_cmd(ctrl, slot_cmd, cmd_mask);
	dbg("%s: SLOTCTRL %x write cmd %x\n",
	    __func__, ctrl->cap_base + SLOTCTRL, slot_cmd);
}

static void hpc_set_green_led_off(struct slot *slot)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_cmd;
	u16 cmd_mask;

	slot_cmd = 0x0300;
	cmd_mask = PWR_LED_CTRL;
	pcie_write_cmd(ctrl, slot_cmd, cmd_mask);
	dbg("%s: SLOTCTRL %x write cmd %x\n",
	    __func__, ctrl->cap_base + SLOTCTRL, slot_cmd);
}

static void hpc_set_green_led_blink(struct slot *slot)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_cmd;
	u16 cmd_mask;

	slot_cmd = 0x0200;
	cmd_mask = PWR_LED_CTRL;
	pcie_write_cmd(ctrl, slot_cmd, cmd_mask);
	dbg("%s: SLOTCTRL %x write cmd %x\n",
	    __func__, ctrl->cap_base + SLOTCTRL, slot_cmd);
}

static int hpc_power_on_slot(struct slot * slot)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_cmd;
	u16 cmd_mask;
	u16 slot_status;
	int retval = 0;

	dbg("%s: slot->hp_slot %x\n", __func__, slot->hp_slot);

	/* Clear sticky power-fault bit from previous power failures */
	retval = pciehp_readw(ctrl, SLOTSTATUS, &slot_status);
	if (retval) {
		err("%s: Cannot read SLOTSTATUS register\n", __func__);
		return retval;
	}
	slot_status &= PWR_FAULT_DETECTED;
	if (slot_status) {
		retval = pciehp_writew(ctrl, SLOTSTATUS, slot_status);
		if (retval) {
			err("%s: Cannot write to SLOTSTATUS register\n",
			    __func__);
			return retval;
		}
	}

	slot_cmd = POWER_ON;
	cmd_mask = PWR_CTRL;
	/* Enable detection that we turned off at slot power-off time */
	if (!pciehp_poll_mode) {
		slot_cmd |= (PWR_FAULT_DETECT_ENABLE | MRL_DETECT_ENABLE |
			     PRSN_DETECT_ENABLE);
		cmd_mask |= (PWR_FAULT_DETECT_ENABLE | MRL_DETECT_ENABLE |
			     PRSN_DETECT_ENABLE);
	}

	retval = pcie_write_cmd(ctrl, slot_cmd, cmd_mask);

	if (retval) {
		err("%s: Write %x command failed!\n", __func__, slot_cmd);
		return -1;
	}
	dbg("%s: SLOTCTRL %x write cmd %x\n",
	    __func__, ctrl->cap_base + SLOTCTRL, slot_cmd);
}

void pciehp_get_latch_status(struct slot *slot, u8 *status)
{
	struct pci_dev *pdev = ctrl_dev(slot->ctrl);
	u16 slot_status;

	pcie_capability_read_word(pdev, PCI_EXP_SLTSTA, &slot_status);
	*status = !!(slot_status & PCI_EXP_SLTSTA_MRLSS);
}

void pciehp_get_adapter_status(struct slot *slot, u8 *status)
{
	struct pci_dev *pdev = ctrl_dev(slot->ctrl);
	u16 slot_status;

	pcie_capability_read_word(pdev, PCI_EXP_SLTSTA, &slot_status);
	*status = !!(slot_status & PCI_EXP_SLTSTA_PDS);
}

int pciehp_query_power_fault(struct slot *slot)
{
	struct pci_dev *pdev = ctrl_dev(slot->ctrl);
	u16 slot_status;

	pcie_capability_read_word(pdev, PCI_EXP_SLTSTA, &slot_status);
	return !!(slot_status & PCI_EXP_SLTSTA_PFD);
}

void pciehp_set_attention_status(struct slot *slot, u8 value)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_cmd;

	if (!ATTN_LED(ctrl))
		return;

	switch (value) {
	case 0:		/* turn off */
		slot_cmd = PCI_EXP_SLTCTL_ATTN_IND_OFF;
		break;
	case 1:		/* turn on */
		slot_cmd = PCI_EXP_SLTCTL_ATTN_IND_ON;
		break;
	case 2:		/* turn blink */
		slot_cmd = PCI_EXP_SLTCTL_ATTN_IND_BLINK;
		break;
	default:
		return;
	}
	pcie_write_cmd_nowait(ctrl, slot_cmd, PCI_EXP_SLTCTL_AIC);
	ctrl_dbg(ctrl, "%s: SLOTCTRL %x write cmd %x\n", __func__,
		 pci_pcie_cap(ctrl->pcie->port) + PCI_EXP_SLTCTL, slot_cmd);
}

void pciehp_green_led_on(struct slot *slot)
{
	struct controller *ctrl = slot->ctrl;

	if (!PWR_LED(ctrl))
		return;

	pcie_write_cmd_nowait(ctrl, PCI_EXP_SLTCTL_PWR_IND_ON,
			      PCI_EXP_SLTCTL_PIC);
	ctrl_dbg(ctrl, "%s: SLOTCTRL %x write cmd %x\n", __func__,
		 pci_pcie_cap(ctrl->pcie->port) + PCI_EXP_SLTCTL,
		 PCI_EXP_SLTCTL_PWR_IND_ON);
}

void pciehp_green_led_off(struct slot *slot)
{
	struct controller *ctrl = slot->ctrl;

	if (!PWR_LED(ctrl))
		return;

	pcie_write_cmd_nowait(ctrl, PCI_EXP_SLTCTL_PWR_IND_OFF,
			      PCI_EXP_SLTCTL_PIC);
	ctrl_dbg(ctrl, "%s: SLOTCTRL %x write cmd %x\n", __func__,
		 pci_pcie_cap(ctrl->pcie->port) + PCI_EXP_SLTCTL,
		 PCI_EXP_SLTCTL_PWR_IND_OFF);
}

void pciehp_green_led_blink(struct slot *slot)
{
	struct controller *ctrl = slot->ctrl;

	if (!PWR_LED(ctrl))
		return;

	pcie_write_cmd_nowait(ctrl, PCI_EXP_SLTCTL_PWR_IND_BLINK,
			      PCI_EXP_SLTCTL_PIC);
	ctrl_dbg(ctrl, "%s: SLOTCTRL %x write cmd %x\n", __func__,
		 pci_pcie_cap(ctrl->pcie->port) + PCI_EXP_SLTCTL,
		 PCI_EXP_SLTCTL_PWR_IND_BLINK);
}

int pciehp_power_on_slot(struct slot *slot)
{
	struct controller *ctrl = slot->ctrl;
	struct pci_dev *pdev = ctrl_dev(ctrl);
	u16 slot_status;
	int retval;

	/* Clear sticky power-fault bit from previous power failures */
	pcie_capability_read_word(pdev, PCI_EXP_SLTSTA, &slot_status);
	if (slot_status & PCI_EXP_SLTSTA_PFD)
		pcie_capability_write_word(pdev, PCI_EXP_SLTSTA,
					   PCI_EXP_SLTSTA_PFD);
	ctrl->power_fault_detected = 0;

	pcie_write_cmd(ctrl, PCI_EXP_SLTCTL_PWR_ON, PCI_EXP_SLTCTL_PCC);
	ctrl_dbg(ctrl, "%s: SLOTCTRL %x write cmd %x\n", __func__,
		 pci_pcie_cap(ctrl->pcie->port) + PCI_EXP_SLTCTL,
		 PCI_EXP_SLTCTL_PWR_ON);

	retval = pciehp_link_enable(ctrl);
	if (retval)
		ctrl_err(ctrl, "%s: Can not enable the link!\n", __func__);

	return retval;
}

static inline int pcie_mask_bad_dllp(struct controller *ctrl)
{
	struct pci_dev *dev = ctrl->pci_dev;
	int pos;
	u32 reg;

	pos = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_ERR);
	if (!pos)
		return 0;
	pci_read_config_dword(dev, pos + PCI_ERR_COR_MASK, &reg);
	if (reg & PCI_ERR_COR_BAD_DLLP)
		return 0;
	reg |= PCI_ERR_COR_BAD_DLLP;
	pci_write_config_dword(dev, pos + PCI_ERR_COR_MASK, reg);
	return 1;
}

static inline void pcie_unmask_bad_dllp(struct controller *ctrl)
{
	struct pci_dev *dev = ctrl->pci_dev;
	u32 reg;
	int pos;

	pos = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_ERR);
	if (!pos)
		return;
	pci_read_config_dword(dev, pos + PCI_ERR_COR_MASK, &reg);
	if (!(reg & PCI_ERR_COR_BAD_DLLP))
		return;
	reg &= ~PCI_ERR_COR_BAD_DLLP;
	pci_write_config_dword(dev, pos + PCI_ERR_COR_MASK, reg);
}

static int hpc_power_off_slot(struct slot * slot)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_cmd;
	u16 cmd_mask;
	int retval = 0;
	int changed;

	dbg("%s: slot->hp_slot %x\n", __func__, slot->hp_slot);

	/*
	 * Set Bad DLLP Mask bit in Correctable Error Mask
	 * Register. This is the workaround against Bad DLLP error
	 * that sometimes happens during turning power off the slot
	 * which conforms to PCI Express 1.0a spec.
	 */
	changed = pcie_mask_bad_dllp(ctrl);

	slot_cmd = POWER_OFF;
	cmd_mask = PWR_CTRL;
	/*
	 * If we get MRL or presence detect interrupts now, the isr
	 * will notice the sticky power-fault bit too and issue power
	 * indicator change commands. This will lead to an endless loop
	 * of command completions, since the power-fault bit remains on
	 * till the slot is powered on again.
	 */
	if (!pciehp_poll_mode) {
		slot_cmd &= ~(PWR_FAULT_DETECT_ENABLE | MRL_DETECT_ENABLE |
			      PRSN_DETECT_ENABLE);
		cmd_mask |= (PWR_FAULT_DETECT_ENABLE | MRL_DETECT_ENABLE |
			     PRSN_DETECT_ENABLE);
	}

	retval = pcie_write_cmd(ctrl, slot_cmd, cmd_mask);
	if (retval) {
		err("%s: Write command failed!\n", __func__);
		retval = -1;
		goto out;
	}
	dbg("%s: SLOTCTRL %x write cmd %x\n",
	    __func__, ctrl->cap_base + SLOTCTRL, slot_cmd);
 out:
	if (changed)
		pcie_unmask_bad_dllp(ctrl);

	return retval;
void pciehp_power_off_slot(struct slot *slot)
{
	struct controller *ctrl = slot->ctrl;

	pcie_write_cmd(ctrl, PCI_EXP_SLTCTL_PWR_OFF, PCI_EXP_SLTCTL_PCC);
	ctrl_dbg(ctrl, "%s: SLOTCTRL %x write cmd %x\n", __func__,
		 pci_pcie_cap(ctrl->pcie->port) + PCI_EXP_SLTCTL,
		 PCI_EXP_SLTCTL_PWR_OFF);
}

static irqreturn_t pcie_isr(int irq, void *dev_id)
{
	struct controller *ctrl = (struct controller *)dev_id;
	u16 detected, intr_loc;
	struct slot *p_slot;
	struct pci_dev *pdev = ctrl_dev(ctrl);
	struct pci_bus *subordinate = pdev->subordinate;
	struct pci_dev *dev;
	struct slot *slot = ctrl->slot;
	u16 detected, intr_loc;
	u8 present;
	bool link;

	/*
	 * In order to guarantee that all interrupt events are
	 * serviced, we need to re-inspect Slot Status register after
	 * clearing what is presumed to be the last pending interrupt.
	 */
	intr_loc = 0;
	do {
		if (pciehp_readw(ctrl, SLOTSTATUS, &detected)) {
			err("%s: Cannot read SLOTSTATUS\n", __func__);
			return IRQ_NONE;
		}

		detected &= (ATTN_BUTTN_PRESSED | PWR_FAULT_DETECTED |
			     MRL_SENS_CHANGED | PRSN_DETECT_CHANGED |
			     CMD_COMPLETED);
		intr_loc |= detected;
		if (!intr_loc)
			return IRQ_NONE;
		if (detected && pciehp_writew(ctrl, SLOTSTATUS, detected)) {
			err("%s: Cannot write to SLOTSTATUS\n", __func__);
			return IRQ_NONE;
		}
	} while (detected);

	dbg("%s: intr_loc %x\n", __FUNCTION__, intr_loc);

	/* Check Command Complete Interrupt Pending */
	if (intr_loc & CMD_COMPLETED) {
		pcie_capability_read_word(pdev, PCI_EXP_SLTSTA, &detected);
		if (detected == (u16) ~0) {
			ctrl_info(ctrl, "%s: no response from device\n",
				  __func__);
			return IRQ_HANDLED;
		}

		detected &= (PCI_EXP_SLTSTA_ABP | PCI_EXP_SLTSTA_PFD |
			     PCI_EXP_SLTSTA_PDC |
			     PCI_EXP_SLTSTA_CC | PCI_EXP_SLTSTA_DLLSC);
		detected &= ~intr_loc;
		intr_loc |= detected;
		if (!intr_loc)
			return IRQ_NONE;
		if (detected)
			pcie_capability_write_word(pdev, PCI_EXP_SLTSTA,
						   intr_loc);
	} while (detected);

	ctrl_dbg(ctrl, "pending interrupts %#06x from Slot Status\n", intr_loc);

	/* Check Command Complete Interrupt Pending */
	if (intr_loc & PCI_EXP_SLTSTA_CC) {
		ctrl->cmd_busy = 0;
		smp_mb();
		wake_up(&ctrl->queue);
	}

	if (!(intr_loc & ~CMD_COMPLETED))
		return IRQ_HANDLED;

	p_slot = pciehp_find_slot(ctrl, ctrl->slot_device_offset);

	/* Check MRL Sensor Changed */
	if (intr_loc & MRL_SENS_CHANGED)
		pciehp_handle_switch_change(p_slot);

	/* Check Attention Button Pressed */
	if (intr_loc & ATTN_BUTTN_PRESSED)
		pciehp_handle_attention_button(p_slot);

	/* Check Presence Detect Changed */
	if (intr_loc & PRSN_DETECT_CHANGED)
		pciehp_handle_presence_change(p_slot);

	/* Check Power Fault Detected */
	if (intr_loc & PWR_FAULT_DETECTED)
		pciehp_handle_power_fault(p_slot);
	if (subordinate) {
		list_for_each_entry(dev, &subordinate->devices, bus_list) {
			if (dev->ignore_hotplug) {
				ctrl_dbg(ctrl, "ignoring hotplug event %#06x (%s requested no hotplug)\n",
					 intr_loc, pci_name(dev));
				return IRQ_HANDLED;
			}
		}
	}

	if (!(intr_loc & ~PCI_EXP_SLTSTA_CC))
		return IRQ_HANDLED;

	/* Check Attention Button Pressed */
	if (intr_loc & PCI_EXP_SLTSTA_ABP) {
		ctrl_info(ctrl, "Button pressed on Slot(%s)\n",
			  slot_name(slot));
		pciehp_queue_interrupt_event(slot, INT_BUTTON_PRESS);
	}

	/* Check Presence Detect Changed */
	if (intr_loc & PCI_EXP_SLTSTA_PDC) {
		pciehp_get_adapter_status(slot, &present);
		ctrl_info(ctrl, "Card %spresent on Slot(%s)\n",
			  present ? "" : "not ", slot_name(slot));
		pciehp_queue_interrupt_event(slot, present ? INT_PRESENCE_ON :
					     INT_PRESENCE_OFF);
	}

	/* Check Power Fault Detected */
	if ((intr_loc & PCI_EXP_SLTSTA_PFD) && !ctrl->power_fault_detected) {
		ctrl->power_fault_detected = 1;
		ctrl_err(ctrl, "Power fault on slot %s\n", slot_name(slot));
		pciehp_queue_interrupt_event(slot, INT_POWER_FAULT);
	}

	if (intr_loc & PCI_EXP_SLTSTA_DLLSC) {
		link = pciehp_check_link_active(ctrl);
		ctrl_info(ctrl, "slot(%s): Link %s event\n",
			  slot_name(slot), link ? "Up" : "Down");
		pciehp_queue_interrupt_event(slot, link ? INT_LINK_UP :
					     INT_LINK_DOWN);
	}

	return IRQ_HANDLED;
}

static int hpc_get_max_lnk_speed(struct slot *slot, enum pci_bus_speed *value)
{
	struct controller *ctrl = slot->ctrl;
	enum pcie_link_speed lnk_speed;
	u32	lnk_cap;
	int retval = 0;

	retval = pciehp_readl(ctrl, LNKCAP, &lnk_cap);
	if (retval) {
		err("%s: Cannot read LNKCAP register\n", __func__);
		return retval;
	}

	switch (lnk_cap & 0x000F) {
	case 1:
		lnk_speed = PCIE_2PT5GB;
		break;
	default:
		lnk_speed = PCIE_LNK_SPEED_UNKNOWN;
		break;
	}

	*value = lnk_speed;
	dbg("Max link speed = %d\n", lnk_speed);

	return retval;
}

static int hpc_get_max_lnk_width(struct slot *slot,
				 enum pcie_link_width *value)
{
	struct controller *ctrl = slot->ctrl;
	enum pcie_link_width lnk_wdth;
	u32	lnk_cap;
	int retval = 0;

	retval = pciehp_readl(ctrl, LNKCAP, &lnk_cap);
	if (retval) {
		err("%s: Cannot read LNKCAP register\n", __func__);
		return retval;
	}

	switch ((lnk_cap & 0x03F0) >> 4){
	case 0:
		lnk_wdth = PCIE_LNK_WIDTH_RESRV;
		break;
	case 1:
		lnk_wdth = PCIE_LNK_X1;
		break;
	case 2:
		lnk_wdth = PCIE_LNK_X2;
		break;
	case 4:
		lnk_wdth = PCIE_LNK_X4;
		break;
	case 8:
		lnk_wdth = PCIE_LNK_X8;
		break;
	case 12:
		lnk_wdth = PCIE_LNK_X12;
		break;
	case 16:
		lnk_wdth = PCIE_LNK_X16;
		break;
	case 32:
		lnk_wdth = PCIE_LNK_X32;
		break;
	default:
		lnk_wdth = PCIE_LNK_WIDTH_UNKNOWN;
		break;
	}

	*value = lnk_wdth;
	dbg("Max link width = %d\n", lnk_wdth);

	return retval;
}

static int hpc_get_cur_lnk_speed(struct slot *slot, enum pci_bus_speed *value)
{
	struct controller *ctrl = slot->ctrl;
	enum pcie_link_speed lnk_speed = PCI_SPEED_UNKNOWN;
	int retval = 0;
	u16 lnk_status;

	retval = pciehp_readw(ctrl, LNKSTATUS, &lnk_status);
	if (retval) {
		err("%s: Cannot read LNKSTATUS register\n", __func__);
		return retval;
	}

	switch (lnk_status & 0x0F) {
	case 1:
		lnk_speed = PCIE_2PT5GB;
		break;
	default:
		lnk_speed = PCIE_LNK_SPEED_UNKNOWN;
		break;
	}

	*value = lnk_speed;
	dbg("Current link speed = %d\n", lnk_speed);

	return retval;
}

static int hpc_get_cur_lnk_width(struct slot *slot,
				 enum pcie_link_width *value)
{
	struct controller *ctrl = slot->ctrl;
	enum pcie_link_width lnk_wdth = PCIE_LNK_WIDTH_UNKNOWN;
	int retval = 0;
	u16 lnk_status;

	retval = pciehp_readw(ctrl, LNKSTATUS, &lnk_status);
	if (retval) {
		err("%s: Cannot read LNKSTATUS register\n", __func__);
		return retval;
	}

	switch ((lnk_status & 0x03F0) >> 4){
	case 0:
		lnk_wdth = PCIE_LNK_WIDTH_RESRV;
		break;
	case 1:
		lnk_wdth = PCIE_LNK_X1;
		break;
	case 2:
		lnk_wdth = PCIE_LNK_X2;
		break;
	case 4:
		lnk_wdth = PCIE_LNK_X4;
		break;
	case 8:
		lnk_wdth = PCIE_LNK_X8;
		break;
	case 12:
		lnk_wdth = PCIE_LNK_X12;
		break;
	case 16:
		lnk_wdth = PCIE_LNK_X16;
		break;
	case 32:
		lnk_wdth = PCIE_LNK_X32;
		break;
	default:
		lnk_wdth = PCIE_LNK_WIDTH_UNKNOWN;
		break;
	}

	*value = lnk_wdth;
	dbg("Current link width = %d\n", lnk_wdth);

	return retval;
}

static void pcie_release_ctrl(struct controller *ctrl);
static struct hpc_ops pciehp_hpc_ops = {
	.power_on_slot			= hpc_power_on_slot,
	.power_off_slot			= hpc_power_off_slot,
	.set_attention_status		= hpc_set_attention_status,
	.get_power_status		= hpc_get_power_status,
	.get_attention_status		= hpc_get_attention_status,
	.get_latch_status		= hpc_get_latch_status,
	.get_adapter_status		= hpc_get_adapter_status,
	.get_emi_status			= hpc_get_emi_status,
	.toggle_emi			= hpc_toggle_emi,

	.get_max_bus_speed		= hpc_get_max_lnk_speed,
	.get_cur_bus_speed		= hpc_get_cur_lnk_speed,
	.get_max_lnk_width		= hpc_get_max_lnk_width,
	.get_cur_lnk_width		= hpc_get_cur_lnk_width,

	.query_power_fault		= hpc_query_power_fault,
	.green_led_on			= hpc_set_green_led_on,
	.green_led_off			= hpc_set_green_led_off,
	.green_led_blink		= hpc_set_green_led_blink,

	.release_ctlr			= pcie_release_ctrl,
	.check_lnk_status		= hpc_check_lnk_status,
};

int pcie_enable_notification(struct controller *ctrl)
{
	u16 cmd, mask;

	cmd = PRSN_DETECT_ENABLE;
	if (ATTN_BUTTN(ctrl))
		cmd |= ATTN_BUTTN_ENABLE;
	if (POWER_CTRL(ctrl))
		cmd |= PWR_FAULT_DETECT_ENABLE;
	if (MRL_SENS(ctrl))
		cmd |= MRL_DETECT_ENABLE;
	if (!pciehp_poll_mode)
		cmd |= HP_INTR_ENABLE | CMD_CMPL_INTR_ENABLE;

	mask = PRSN_DETECT_ENABLE | ATTN_BUTTN_ENABLE | MRL_DETECT_ENABLE |
	       PWR_FAULT_DETECT_ENABLE | HP_INTR_ENABLE | CMD_CMPL_INTR_ENABLE;

	if (pcie_write_cmd(ctrl, cmd, mask)) {
		err("%s: Cannot enable software notification\n", __func__);
		return -1;
	}
	return 0;
void pcie_enable_notification(struct controller *ctrl)
static void pcie_enable_notification(struct controller *ctrl)
{
	u16 cmd, mask;

	/*
	 * TBD: Power fault detected software notification support.
	 *
	 * Power fault detected software notification is not enabled
	 * now, because it caused power fault detected interrupt storm
	 * on some machines. On those machines, power fault detected
	 * bit in the slot status register was set again immediately
	 * when it is cleared in the interrupt service routine, and
	 * next power fault detected interrupt was notified again.
	 */

	/*
	 * Always enable link events: thus link-up and link-down shall
	 * always be treated as hotplug and unplug respectively. Enable
	 * presence detect only if Attention Button is not present.
	 */
	cmd = PCI_EXP_SLTCTL_DLLSCE;
	if (ATTN_BUTTN(ctrl))
		cmd |= PCI_EXP_SLTCTL_ABPE;
	else
		cmd |= PCI_EXP_SLTCTL_PDCE;
	if (!pciehp_poll_mode)
		cmd |= PCI_EXP_SLTCTL_HPIE | PCI_EXP_SLTCTL_CCIE;

	mask = (PCI_EXP_SLTCTL_PDCE | PCI_EXP_SLTCTL_ABPE |
		PCI_EXP_SLTCTL_PFDE |
		PCI_EXP_SLTCTL_HPIE | PCI_EXP_SLTCTL_CCIE |
		PCI_EXP_SLTCTL_DLLSCE);

	pcie_write_cmd_nowait(ctrl, cmd, mask);
	ctrl_dbg(ctrl, "%s: SLOTCTRL %x write cmd %x\n", __func__,
		 pci_pcie_cap(ctrl->pcie->port) + PCI_EXP_SLTCTL, cmd);
}

void pcie_reenable_notification(struct controller *ctrl)
{
	/*
	 * Clear both Presence and Data Link Layer Changed to make sure
	 * those events still fire after we have re-enabled them.
	 */
	pcie_capability_write_word(ctrl->pcie->port, PCI_EXP_SLTSTA,
				   PCI_EXP_SLTSTA_PDC | PCI_EXP_SLTSTA_DLLSC);
	pcie_enable_notification(ctrl);
}

static void pcie_disable_notification(struct controller *ctrl)
{
	u16 mask;
	mask = PRSN_DETECT_ENABLE | ATTN_BUTTN_ENABLE | MRL_DETECT_ENABLE |
	       PWR_FAULT_DETECT_ENABLE | HP_INTR_ENABLE | CMD_CMPL_INTR_ENABLE;
	if (pcie_write_cmd(ctrl, 0, mask))
		warn("%s: Cannot disable software notification\n", __func__);
}

static int pcie_init_notification(struct controller *ctrl)
{
	if (pciehp_request_irq(ctrl))
		return -1;
	if (pcie_enable_notification(ctrl)) {
		pciehp_free_irq(ctrl);
		return -1;
	}

	mask = (PCI_EXP_SLTCTL_PDCE | PCI_EXP_SLTCTL_ABPE |
		PCI_EXP_SLTCTL_MRLSCE | PCI_EXP_SLTCTL_PFDE |
		PCI_EXP_SLTCTL_HPIE | PCI_EXP_SLTCTL_CCIE |
		PCI_EXP_SLTCTL_DLLSCE);
	pcie_write_cmd(ctrl, 0, mask);
	ctrl_dbg(ctrl, "%s: SLOTCTRL %x write cmd %x\n", __func__,
		 pci_pcie_cap(ctrl->pcie->port) + PCI_EXP_SLTCTL, 0);
}

/*
 * pciehp has a 1:1 bus:slot relationship so we ultimately want a secondary
 * bus reset of the bridge, but at the same time we want to ensure that it is
 * not seen as a hot-unplug, followed by the hot-plug of the device. Thus,
 * disable link state notification and presence detection change notification
 * momentarily, if we see that they could interfere. Also, clear any spurious
 * events after.
 */
int pciehp_reset_slot(struct slot *slot, int probe)
{
	struct controller *ctrl = slot->ctrl;
	struct pci_dev *pdev = ctrl_dev(ctrl);
	u16 stat_mask = 0, ctrl_mask = 0;

	if (probe)
		return 0;

	if (!ATTN_BUTTN(ctrl)) {
		ctrl_mask |= PCI_EXP_SLTCTL_PDCE;
		stat_mask |= PCI_EXP_SLTSTA_PDC;
	}
	ctrl_mask |= PCI_EXP_SLTCTL_DLLSCE;
	stat_mask |= PCI_EXP_SLTSTA_DLLSC;

	pcie_write_cmd(ctrl, 0, ctrl_mask);
	ctrl_dbg(ctrl, "%s: SLOTCTRL %x write cmd %x\n", __func__,
		 pci_pcie_cap(ctrl->pcie->port) + PCI_EXP_SLTCTL, 0);
	if (pciehp_poll_mode)
		del_timer_sync(&ctrl->poll_timer);

	pci_reset_bridge_secondary_bus(ctrl->pcie->port);

	pcie_capability_write_word(pdev, PCI_EXP_SLTSTA, stat_mask);
	pcie_write_cmd_nowait(ctrl, ctrl_mask, ctrl_mask);
	ctrl_dbg(ctrl, "%s: SLOTCTRL %x write cmd %x\n", __func__,
		 pci_pcie_cap(ctrl->pcie->port) + PCI_EXP_SLTCTL, ctrl_mask);
	if (pciehp_poll_mode)
		int_poll_timeout(ctrl->poll_timer.data);

	return 0;
}

int pcie_init_notification(struct controller *ctrl)
{
	if (pciehp_request_irq(ctrl))
		return -1;
	pcie_enable_notification(ctrl);
	ctrl->notification_enabled = 1;
	return 0;
}

void pcie_shutdown_notification(struct controller *ctrl)
{
	pcie_disable_notification(ctrl);
	pciehp_free_irq(ctrl);
	if (ctrl->notification_enabled) {
		pcie_disable_notification(ctrl);
		pciehp_free_irq(ctrl);
		ctrl->notification_enabled = 0;
	}
}

static int pcie_init_slot(struct controller *ctrl)
{
	struct slot *slot;

	slot = kzalloc(sizeof(*slot), GFP_KERNEL);
	if (!slot)
		return -ENOMEM;

	slot->hp_slot = 0;
	slot->ctrl = ctrl;
	slot->bus = ctrl->pci_dev->subordinate->number;
	slot->device = ctrl->slot_device_offset + slot->hp_slot;
	slot->hpc_ops = ctrl->hpc_ops;
	slot->number = ctrl->first_slot;
	snprintf(slot->name, SLOT_NAME_SIZE, "%d", slot->number);
	mutex_init(&slot->lock);
	INIT_DELAYED_WORK(&slot->work, pciehp_queue_pushbutton_work);
	list_add(&slot->slot_list, &ctrl->slot_list);
	return 0;
	slot->wq = alloc_workqueue("pciehp-%u", 0, 0, PSN(ctrl));
	if (!slot->wq)
		goto abort;

	slot->ctrl = ctrl;
	mutex_init(&slot->lock);
	mutex_init(&slot->hotplug_lock);
	INIT_DELAYED_WORK(&slot->work, pciehp_queue_pushbutton_work);
	ctrl->slot = slot;
	return 0;
abort:
	kfree(slot);
	return -ENOMEM;
}

static void pcie_cleanup_slot(struct controller *ctrl)
{
	struct slot *slot;
	slot = list_first_entry(&ctrl->slot_list, struct slot, slot_list);
	list_del(&slot->slot_list);
	cancel_delayed_work(&slot->work);
	flush_scheduled_work();
	flush_workqueue(pciehp_wq);
	struct slot *slot = ctrl->slot;

	destroy_workqueue(slot->wq);
	kfree(slot);
}

static inline void dbg_ctrl(struct controller *ctrl)
{
	int i;
	u16 reg16;
	struct pci_dev *pdev = ctrl->pci_dev;
	struct pci_dev *pdev = ctrl->pcie->port;
	u16 reg16;

	if (!pciehp_debug)
		return;

	dbg("Hotplug Controller:\n");
	dbg("  Seg/Bus/Dev/Func/IRQ : %s IRQ %d\n", pci_name(pdev), pdev->irq);
	dbg("  Vendor ID            : 0x%04x\n", pdev->vendor);
	dbg("  Device ID            : 0x%04x\n", pdev->device);
	dbg("  Subsystem ID         : 0x%04x\n", pdev->subsystem_device);
	dbg("  Subsystem Vendor ID  : 0x%04x\n", pdev->subsystem_vendor);
	dbg("  PCIe Cap offset      : 0x%02x\n", ctrl->cap_base);
	for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
		if (!pci_resource_len(pdev, i))
			continue;
		dbg("  PCI resource [%d]     : 0x%llx@0x%llx\n", i,
		    (unsigned long long)pci_resource_len(pdev, i),
		    (unsigned long long)pci_resource_start(pdev, i));
	}
	dbg("Slot Capabilities      : 0x%08x\n", ctrl->slot_cap);
	dbg("  Physical Slot Number : %d\n", ctrl->first_slot);
	dbg("  Attention Button     : %3s\n", ATTN_BUTTN(ctrl) ? "yes" : "no");
	dbg("  Power Controller     : %3s\n", POWER_CTRL(ctrl) ? "yes" : "no");
	dbg("  MRL Sensor           : %3s\n", MRL_SENS(ctrl)   ? "yes" : "no");
	dbg("  Attention Indicator  : %3s\n", ATTN_LED(ctrl)   ? "yes" : "no");
	dbg("  Power Indicator      : %3s\n", PWR_LED(ctrl)    ? "yes" : "no");
	dbg("  Hot-Plug Surprise    : %3s\n", HP_SUPR_RM(ctrl) ? "yes" : "no");
	dbg("  EMI Present          : %3s\n", EMI(ctrl)        ? "yes" : "no");
	dbg("  Command Completed    : %3s\n", NO_CMD_CMPL(ctrl)? "no" : "yes");
	pciehp_readw(ctrl, SLOTSTATUS, &reg16);
	dbg("Slot Status            : 0x%04x\n", reg16);
	pciehp_readw(ctrl, SLOTCTRL, &reg16);
	dbg("Slot Control           : 0x%04x\n", reg16);
}

struct controller *pcie_init(struct pcie_device *dev)
{
	struct controller *ctrl;
	u32 slot_cap;
	ctrl_info(ctrl, "Slot Capabilities      : 0x%08x\n", ctrl->slot_cap);
	pcie_capability_read_word(pdev, PCI_EXP_SLTSTA, &reg16);
	ctrl_info(ctrl, "Slot Status            : 0x%04x\n", reg16);
	pcie_capability_read_word(pdev, PCI_EXP_SLTCTL, &reg16);
	ctrl_info(ctrl, "Slot Control           : 0x%04x\n", reg16);
}

#define FLAG(x, y)	(((x) & (y)) ? '+' : '-')

struct controller *pcie_init(struct pcie_device *dev)
{
	struct controller *ctrl;
	u32 slot_cap, link_cap;
	struct pci_dev *pdev = dev->port;

	ctrl = kzalloc(sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl) {
		err("%s : out of memory\n", __func__);
		goto abort;
	}
	INIT_LIST_HEAD(&ctrl->slot_list);

	ctrl->pci_dev = pdev;
	ctrl->cap_base = pci_find_capability(pdev, PCI_CAP_ID_EXP);
	if (!ctrl->cap_base) {
		err("%s: Cannot find PCI Express capability\n", __func__);
		goto abort;
	}
	if (pciehp_readl(ctrl, SLOTCAP, &slot_cap)) {
		err("%s: Cannot read SLOTCAP register\n", __func__);
		goto abort;
	}

	ctrl->slot_cap = slot_cap;
	ctrl->first_slot = slot_cap >> 19;
	ctrl->slot_device_offset = 0;
	ctrl->num_slots = 1;
	ctrl->hpc_ops = &pciehp_hpc_ops;
	mutex_init(&ctrl->crit_sect);
	mutex_init(&ctrl->ctrl_lock);
	init_waitqueue_head(&ctrl->queue);
	dbg_ctrl(ctrl);
	/*
	 * Controller doesn't notify of command completion if the "No
	 * Command Completed Support" bit is set in Slot Capability
	 * register or the controller supports none of power
	 * controller, attention led, power led and EMI.
	 */
	if (NO_CMD_CMPL(ctrl) ||
	    !(POWER_CTRL(ctrl) | ATTN_LED(ctrl) | PWR_LED(ctrl) | EMI(ctrl)))
	    ctrl->no_cmd_complete = 1;

	/* Clear all remaining event bits in Slot Status register */
	if (pciehp_writew(ctrl, SLOTSTATUS, 0x1f))
		goto abort_ctrl;

	/* Disable sotfware notification */
	pcie_disable_notification(ctrl);

	/*
	 * If this is the first controller to be initialized,
	 * initialize the pciehp work queue
	 */
	if (atomic_add_return(1, &pciehp_num_controllers) == 1) {
		pciehp_wq = create_singlethread_workqueue("pciehpd");
		if (!pciehp_wq)
			goto abort_ctrl;
	}

	info("HPC vendor_id %x device_id %x ss_vid %x ss_did %x\n",
	     pdev->vendor, pdev->device,
	     pdev->subsystem_vendor, pdev->subsystem_device);
		dev_err(&dev->device, "%s: Out of memory\n", __func__);
		goto abort;
	}
	ctrl->pcie = dev;
	pcie_capability_read_dword(pdev, PCI_EXP_SLTCAP, &slot_cap);
	ctrl->slot_cap = slot_cap;
	mutex_init(&ctrl->ctrl_lock);
	init_waitqueue_head(&ctrl->queue);
	dbg_ctrl(ctrl);

	/* Check if Data Link Layer Link Active Reporting is implemented */
	pcie_capability_read_dword(pdev, PCI_EXP_LNKCAP, &link_cap);
	if (link_cap & PCI_EXP_LNKCAP_DLLLARC)
		ctrl->link_active_reporting = 1;

	/* Clear all remaining event bits in Slot Status register */
	pcie_capability_write_word(pdev, PCI_EXP_SLTSTA,
		PCI_EXP_SLTSTA_ABP | PCI_EXP_SLTSTA_PFD |
		PCI_EXP_SLTSTA_MRLSC | PCI_EXP_SLTSTA_PDC |
		PCI_EXP_SLTSTA_CC | PCI_EXP_SLTSTA_DLLSC);

	ctrl_info(ctrl, "Slot #%d AttnBtn%c PwrCtrl%c MRL%c AttnInd%c PwrInd%c HotPlug%c Surprise%c Interlock%c NoCompl%c LLActRep%c\n",
		(slot_cap & PCI_EXP_SLTCAP_PSN) >> 19,
		FLAG(slot_cap, PCI_EXP_SLTCAP_ABP),
		FLAG(slot_cap, PCI_EXP_SLTCAP_PCP),
		FLAG(slot_cap, PCI_EXP_SLTCAP_MRLSP),
		FLAG(slot_cap, PCI_EXP_SLTCAP_AIP),
		FLAG(slot_cap, PCI_EXP_SLTCAP_PIP),
		FLAG(slot_cap, PCI_EXP_SLTCAP_HPC),
		FLAG(slot_cap, PCI_EXP_SLTCAP_HPS),
		FLAG(slot_cap, PCI_EXP_SLTCAP_EIP),
		FLAG(slot_cap, PCI_EXP_SLTCAP_NCCS),
		FLAG(link_cap, PCI_EXP_LNKCAP_DLLLARC));

	if (pcie_init_slot(ctrl))
		goto abort_ctrl;

	if (pcie_init_notification(ctrl))
		goto abort_slot;

	return ctrl;

abort_slot:
	pcie_cleanup_slot(ctrl);
	return ctrl;

abort_ctrl:
	kfree(ctrl);
abort:
	return NULL;
}

void pcie_release_ctrl(struct controller *ctrl)
{
	pcie_shutdown_notification(ctrl);
	pcie_cleanup_slot(ctrl);
	/*
	 * If this is the last controller to be released, destroy the
	 * pciehp work queue
	 */
	if (atomic_dec_and_test(&pciehp_num_controllers))
		destroy_workqueue(pciehp_wq);
void pciehp_release_ctrl(struct controller *ctrl)
{
	pcie_cleanup_slot(ctrl);
	kfree(ctrl);
}
