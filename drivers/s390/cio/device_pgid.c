/*
 * drivers/s390/cio/device_pgid.c
 *
 *    Copyright (C) 2002 IBM Deutschland Entwicklung GmbH,
 *			 IBM Corporation
 *    Author(s): Cornelia Huck (cornelia.huck@de.ibm.com)
 *		 Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 * Path Group ID functions.
 */

#include <linux/module.h>
#include <linux/init.h>

#include <asm/ccwdev.h>
#include <asm/cio.h>
#include <asm/delay.h>
#include <asm/lowcore.h>

#include "cio.h"
#include "cio_debug.h"
#include "css.h"
#include "device.h"
#include "ioasm.h"
#include "io_sch.h"

/*
 * Helper function called from interrupt context to decide whether an
 * operation should be tried again.
 */
static int __ccw_device_should_retry(union scsw *scsw)
{
	/* CC is only valid if start function bit is set. */
	if ((scsw->cmd.fctl & SCSW_FCTL_START_FUNC) && scsw->cmd.cc == 1)
		return 1;
	/* No more activity. For sense and set PGID we stubbornly try again. */
	if (!scsw->cmd.actl)
		return 1;
	return 0;
}

/*
 * Start Sense Path Group ID helper function. Used in ccw_device_recog
 * and ccw_device_sense_pgid.
 */
static int
__ccw_device_sense_pgid_start(struct ccw_device *cdev)
{
	struct subchannel *sch;
	struct ccw1 *ccw;
	int ret;
	int i;

	sch = to_subchannel(cdev->dev.parent);
	/* Return if we already checked on all paths. */
	if (cdev->private->imask == 0)
		return (sch->lpm == 0) ? -ENODEV : -EACCES;
	i = 8 - ffs(cdev->private->imask);

	/* Setup sense path group id channel program. */
	ccw = cdev->private->iccws;
	ccw->cmd_code = CCW_CMD_SENSE_PGID;
	ccw->count = sizeof (struct pgid);
	ccw->flags = CCW_FLAG_SLI;

	/* Reset device status. */
	memset(&cdev->private->irb, 0, sizeof(struct irb));
	/* Try on every path. */
	ret = -ENODEV;
	while (cdev->private->imask != 0) {
		/* Try every path multiple times. */
		ccw->cda = (__u32) __pa (&cdev->private->pgid[i]);
		if (cdev->private->iretry > 0) {
			cdev->private->iretry--;
			/* Reset internal retry indication. */
			cdev->private->flags.intretry = 0;
			ret = cio_start (sch, cdev->private->iccws, 
					 cdev->private->imask);
			/* ret is 0, -EBUSY, -EACCES or -ENODEV */
			if (ret != -EACCES)
				return ret;
			CIO_MSG_EVENT(3, "SNID - Device %04x on Subchannel "
				      "0.%x.%04x, lpm %02X, became 'not "
				      "operational'\n",
				      cdev->private->dev_id.devno,
				      sch->schid.ssid,
				      sch->schid.sch_no, cdev->private->imask);

		}
		cdev->private->imask >>= 1;
		cdev->private->iretry = 5;
		i++;
	}

	return ret;
}

void
ccw_device_sense_pgid_start(struct ccw_device *cdev)
{
	int ret;

	/* Set a timeout of 60s */
	ccw_device_set_timeout(cdev, 60*HZ);

	cdev->private->state = DEV_STATE_SENSE_PGID;
	cdev->private->imask = 0x80;
	cdev->private->iretry = 5;
	memset (&cdev->private->pgid, 0, sizeof (cdev->private->pgid));
	ret = __ccw_device_sense_pgid_start(cdev);
	if (ret && ret != -EBUSY)
		ccw_device_sense_pgid_done(cdev, ret);
}

/*
 * Called from interrupt context to check if a valid answer
 * to Sense Path Group ID was received.
 */
static int
__ccw_device_check_sense_pgid(struct ccw_device *cdev)
{
	struct subchannel *sch;
	struct irb *irb;
	int i;

	sch = to_subchannel(cdev->dev.parent);
	irb = &cdev->private->irb;
	if (irb->scsw.cmd.fctl & (SCSW_FCTL_HALT_FUNC | SCSW_FCTL_CLEAR_FUNC)) {
		/* Retry Sense PGID if requested. */
		if (cdev->private->flags.intretry) {
			cdev->private->flags.intretry = 0;
			return -EAGAIN;
		}
		return -ETIME;
	}
	if (irb->esw.esw0.erw.cons &&
	    (irb->ecw[0]&(SNS0_CMD_REJECT|SNS0_INTERVENTION_REQ))) {
		/*
		 * If the device doesn't support the Sense Path Group ID
		 *  command further retries wouldn't help ...
		 */
		return -EOPNOTSUPP;
	}
	if (irb->esw.esw0.erw.cons) {
		CIO_MSG_EVENT(2, "SNID - device 0.%x.%04x, unit check, "
			      "lpum %02X, cnt %02d, sns : "
			      "%02X%02X%02X%02X %02X%02X%02X%02X ...\n",
			      cdev->private->dev_id.ssid,
			      cdev->private->dev_id.devno,
			      irb->esw.esw0.sublog.lpum,
			      irb->esw.esw0.erw.scnt,
			      irb->ecw[0], irb->ecw[1],
			      irb->ecw[2], irb->ecw[3],
			      irb->ecw[4], irb->ecw[5],
			      irb->ecw[6], irb->ecw[7]);
		return -EAGAIN;
	}
	if (irb->scsw.cmd.cc == 3) {
		u8 lpm;

		lpm = to_io_private(sch)->orb.cmd.lpm;
		CIO_MSG_EVENT(3, "SNID - Device %04x on Subchannel 0.%x.%04x,"
			      " lpm %02X, became 'not operational'\n",
			      cdev->private->dev_id.devno, sch->schid.ssid,
			      sch->schid.sch_no, lpm);
		return -EACCES;
	}
	i = 8 - ffs(cdev->private->imask);
	if (cdev->private->pgid[i].inf.ps.state2 == SNID_STATE2_RESVD_ELSE) {
		CIO_MSG_EVENT(2, "SNID - Device %04x on Subchannel 0.%x.%04x "
			      "is reserved by someone else\n",
			      cdev->private->dev_id.devno, sch->schid.ssid,
			      sch->schid.sch_no);
		return -EUSERS;
	}
	return 0;
}

/*
 * Got interrupt for Sense Path Group ID.
 */
void
ccw_device_sense_pgid_irq(struct ccw_device *cdev, enum dev_event dev_event)
{
	struct subchannel *sch;
	struct irb *irb;
	int ret;

	irb = (struct irb *) __LC_IRB;

	if (irb->scsw.cmd.stctl ==
	    (SCSW_STCTL_STATUS_PEND | SCSW_STCTL_ALERT_STATUS)) {
		if (__ccw_device_should_retry(&irb->scsw)) {
			ret = __ccw_device_sense_pgid_start(cdev);
			if (ret && ret != -EBUSY)
				ccw_device_sense_pgid_done(cdev, ret);
		}
		return;
	}
	if (ccw_device_accumulate_and_sense(cdev, irb) != 0)
		return;
	sch = to_subchannel(cdev->dev.parent);
	ret = __ccw_device_check_sense_pgid(cdev);
	memset(&cdev->private->irb, 0, sizeof(struct irb));
	switch (ret) {
	/* 0, -ETIME, -EOPNOTSUPP, -EAGAIN, -EACCES or -EUSERS */
	case -EOPNOTSUPP:	/* Sense Path Group ID not supported */
		ccw_device_sense_pgid_done(cdev, -EOPNOTSUPP);
		break;
	case -ETIME:		/* Sense path group id stopped by timeout. */
		ccw_device_sense_pgid_done(cdev, -ETIME);
		break;
	case -EACCES:		/* channel is not operational. */
		sch->lpm &= ~cdev->private->imask;
		/* Fall through. */
	case 0:			/* Sense Path Group ID successful. */
		cdev->private->imask >>= 1;
		cdev->private->iretry = 5;
		/* Fall through. */
	case -EAGAIN:		/* Try again. */
		ret = __ccw_device_sense_pgid_start(cdev);
		if (ret != 0 && ret != -EBUSY)
			ccw_device_sense_pgid_done(cdev, ret);
		break;
	case -EUSERS:		/* device is reserved for someone else. */
		ccw_device_sense_pgid_done(cdev, -EUSERS);
		break;
	}
}

/*
 * Path Group ID helper function.
 */
static int
__ccw_device_do_pgid(struct ccw_device *cdev, __u8 func)
{
	struct subchannel *sch;
	struct ccw1 *ccw;
	int ret;

	sch = to_subchannel(cdev->dev.parent);

	/* Setup sense path group id channel program. */
	cdev->private->pgid[0].inf.fc = func;
	ccw = cdev->private->iccws;
	if (cdev->private->flags.pgid_single)
		cdev->private->pgid[0].inf.fc |= SPID_FUNC_SINGLE_PATH;
	else
		cdev->private->pgid[0].inf.fc |= SPID_FUNC_MULTI_PATH;
	ccw->cmd_code = CCW_CMD_SET_PGID;
	ccw->cda = (__u32) __pa (&cdev->private->pgid[0]);
	ccw->count = sizeof (struct pgid);
	ccw->flags = CCW_FLAG_SLI;

	/* Reset device status. */
	memset(&cdev->private->irb, 0, sizeof(struct irb));

	/* Try multiple times. */
	ret = -EACCES;
	if (cdev->private->iretry > 0) {
		cdev->private->iretry--;
		/* Reset internal retry indication. */
		cdev->private->flags.intretry = 0;
		ret = cio_start (sch, cdev->private->iccws,
				 cdev->private->imask);
		/* We expect an interrupt in case of success or busy
		 * indication. */
		if ((ret == 0) || (ret == -EBUSY))
			return ret;
	}
	/* PGID command failed on this path. */
	CIO_MSG_EVENT(3, "SPID - Device %04x on Subchannel "
		      "0.%x.%04x, lpm %02X, became 'not operational'\n",
		      cdev->private->dev_id.devno, sch->schid.ssid,
		      sch->schid.sch_no, cdev->private->imask);
	return ret;
}

/*
 * Helper function to send a nop ccw down a path.
 */
static int __ccw_device_do_nop(struct ccw_device *cdev)
{
	struct subchannel *sch;
	struct ccw1 *ccw;
	int ret;

	sch = to_subchannel(cdev->dev.parent);

	/* Setup nop channel program. */
	ccw = cdev->private->iccws;
	ccw->cmd_code = CCW_CMD_NOOP;
	ccw->cda = 0;
	ccw->count = 0;
	ccw->flags = CCW_FLAG_SLI;

	/* Reset device status. */
	memset(&cdev->private->irb, 0, sizeof(struct irb));

	/* Try multiple times. */
	ret = -EACCES;
	if (cdev->private->iretry > 0) {
		cdev->private->iretry--;
		/* Reset internal retry indication. */
		cdev->private->flags.intretry = 0;
		ret = cio_start (sch, cdev->private->iccws,
				 cdev->private->imask);
		/* We expect an interrupt in case of success or busy
		 * indication. */
		if ((ret == 0) || (ret == -EBUSY))
			return ret;
	}
	/* nop command failed on this path. */
	CIO_MSG_EVENT(3, "NOP - Device %04x on Subchannel "
		      "0.%x.%04x, lpm %02X, became 'not operational'\n",
		      cdev->private->dev_id.devno, sch->schid.ssid,
		      sch->schid.sch_no, cdev->private->imask);
	return ret;
}


/*
 * Called from interrupt context to check if a valid answer
 * to Set Path Group ID was received.
 */
static int
__ccw_device_check_pgid(struct ccw_device *cdev)
{
	struct subchannel *sch;
	struct irb *irb;

	sch = to_subchannel(cdev->dev.parent);
	irb = &cdev->private->irb;
	if (irb->scsw.cmd.fctl & (SCSW_FCTL_HALT_FUNC | SCSW_FCTL_CLEAR_FUNC)) {
		/* Retry Set PGID if requested. */
		if (cdev->private->flags.intretry) {
			cdev->private->flags.intretry = 0;
			return -EAGAIN;
		}
		return -ETIME;
	}
	if (irb->esw.esw0.erw.cons) {
		if (irb->ecw[0] & SNS0_CMD_REJECT)
			return -EOPNOTSUPP;
		/* Hmm, whatever happened, try again. */
		CIO_MSG_EVENT(2, "SPID - device 0.%x.%04x, unit check, "
			      "cnt %02d, "
			      "sns : %02X%02X%02X%02X %02X%02X%02X%02X ...\n",
			      cdev->private->dev_id.ssid,
			      cdev->private->dev_id.devno,
			      irb->esw.esw0.erw.scnt,
			      irb->ecw[0], irb->ecw[1],
			      irb->ecw[2], irb->ecw[3],
			      irb->ecw[4], irb->ecw[5],
			      irb->ecw[6], irb->ecw[7]);
		return -EAGAIN;
	}
	if (irb->scsw.cmd.cc == 3) {
		CIO_MSG_EVENT(3, "SPID - Device %04x on Subchannel 0.%x.%04x,"
			      " lpm %02X, became 'not operational'\n",
			      cdev->private->dev_id.devno, sch->schid.ssid,
			      sch->schid.sch_no, cdev->private->imask);
		return -EACCES;
	}
	return 0;
}

/*
 * Called from interrupt context to check the path status after a nop has
 * been send.
 */
static int __ccw_device_check_nop(struct ccw_device *cdev)
{
	struct subchannel *sch;
	struct irb *irb;

	sch = to_subchannel(cdev->dev.parent);
	irb = &cdev->private->irb;
	if (irb->scsw.cmd.fctl & (SCSW_FCTL_HALT_FUNC | SCSW_FCTL_CLEAR_FUNC)) {
		/* Retry NOP if requested. */
		if (cdev->private->flags.intretry) {
			cdev->private->flags.intretry = 0;
			return -EAGAIN;
		}
		return -ETIME;
	}
	if (irb->scsw.cmd.cc == 3) {
		CIO_MSG_EVENT(3, "NOP - Device %04x on Subchannel 0.%x.%04x,"
			      " lpm %02X, became 'not operational'\n",
			      cdev->private->dev_id.devno, sch->schid.ssid,
			      sch->schid.sch_no, cdev->private->imask);
		return -EACCES;
	}
	return 0;
}

static void
__ccw_device_verify_start(struct ccw_device *cdev)
{
	struct subchannel *sch;
	__u8 func;
	int ret;

	sch = to_subchannel(cdev->dev.parent);
	/* Repeat for all paths. */
	for (; cdev->private->imask; cdev->private->imask >>= 1,
				     cdev->private->iretry = 5) {
		if ((cdev->private->imask & sch->schib.pmcw.pam) == 0)
			/* Path not available, try next. */
			continue;
		if (cdev->private->options.pgroup) {
			if (sch->opm & cdev->private->imask)
				func = SPID_FUNC_ESTABLISH;
			else
				func = SPID_FUNC_RESIGN;
			ret = __ccw_device_do_pgid(cdev, func);
		} else
			ret = __ccw_device_do_nop(cdev);
		/* We expect an interrupt in case of success or busy
		 * indication. */
		if (ret == 0 || ret == -EBUSY)
			return;
		/* Permanent path failure, try next. */
	}
	/* Done with all paths. */
	ccw_device_verify_done(cdev, (sch->vpm != 0) ? 0 : -ENODEV);
}
		
/*
 * Got interrupt for Set Path Group ID.
 */
void
ccw_device_verify_irq(struct ccw_device *cdev, enum dev_event dev_event)
{
	struct subchannel *sch;
	struct irb *irb;
	int ret;

	irb = (struct irb *) __LC_IRB;

	if (irb->scsw.cmd.stctl ==
	    (SCSW_STCTL_STATUS_PEND | SCSW_STCTL_ALERT_STATUS)) {
		if (__ccw_device_should_retry(&irb->scsw))
			__ccw_device_verify_start(cdev);
		return;
	}
	if (ccw_device_accumulate_and_sense(cdev, irb) != 0)
		return;
	sch = to_subchannel(cdev->dev.parent);
	if (cdev->private->options.pgroup)
		ret = __ccw_device_check_pgid(cdev);
	else
		ret = __ccw_device_check_nop(cdev);
	memset(&cdev->private->irb, 0, sizeof(struct irb));

	switch (ret) {
	/* 0, -ETIME, -EAGAIN, -EOPNOTSUPP or -EACCES */
	case 0:
		/* Path verification ccw finished successfully, update lpm. */
		sch->vpm |= sch->opm & cdev->private->imask;
		/* Go on with next path. */
		cdev->private->imask >>= 1;
		cdev->private->iretry = 5;
		__ccw_device_verify_start(cdev);
		break;
	case -EOPNOTSUPP:
		/*
		 * One of those strange devices which claim to be able
		 * to do multipathing but not for Set Path Group ID.
		 */
		if (cdev->private->flags.pgid_single)
			cdev->private->options.pgroup = 0;
		else
			cdev->private->flags.pgid_single = 1;
		/* Retry */
		sch->vpm = 0;
		cdev->private->imask = 0x80;
		cdev->private->iretry = 5;
		/* fall through. */
	case -EAGAIN:		/* Try again. */
		__ccw_device_verify_start(cdev);
		break;
	case -ETIME:		/* Set path group id stopped by timeout. */
		ccw_device_verify_done(cdev, -ETIME);
		break;
	case -EACCES:		/* channel is not operational. */
		cdev->private->imask >>= 1;
		cdev->private->iretry = 5;
		__ccw_device_verify_start(cdev);
		break;
	}
}

void
ccw_device_verify_start(struct ccw_device *cdev)
{
	struct subchannel *sch = to_subchannel(cdev->dev.parent);

	cdev->private->flags.pgid_single = 0;
	cdev->private->imask = 0x80;
	cdev->private->iretry = 5;

	/* Start with empty vpm. */
	sch->vpm = 0;

	/* Get current pam. */
	if (stsch(sch->schid, &sch->schib)) {
		ccw_device_verify_done(cdev, -ENODEV);
		return;
	}
	/* After 60s path verification is considered to have failed. */
	ccw_device_set_timeout(cdev, 60*HZ);
	__ccw_device_verify_start(cdev);
}

static void
__ccw_device_disband_start(struct ccw_device *cdev)
{
	struct subchannel *sch;
	int ret;

	sch = to_subchannel(cdev->dev.parent);
	while (cdev->private->imask != 0) {
		if (sch->lpm & cdev->private->imask) {
			ret = __ccw_device_do_pgid(cdev, SPID_FUNC_DISBAND);
			if (ret == 0)
				return;
		}
		cdev->private->iretry = 5;
		cdev->private->imask >>= 1;
	}
	ccw_device_disband_done(cdev, (sch->lpm != 0) ? 0 : -ENODEV);
}

/*
 * Got interrupt for Unset Path Group ID.
 */
void
ccw_device_disband_irq(struct ccw_device *cdev, enum dev_event dev_event)
{
	struct subchannel *sch;
	struct irb *irb;
	int ret;

	irb = (struct irb *) __LC_IRB;

	if (irb->scsw.cmd.stctl ==
	    (SCSW_STCTL_STATUS_PEND | SCSW_STCTL_ALERT_STATUS)) {
		if (__ccw_device_should_retry(&irb->scsw))
			__ccw_device_disband_start(cdev);
		return;
	}
	if (ccw_device_accumulate_and_sense(cdev, irb) != 0)
		return;
	sch = to_subchannel(cdev->dev.parent);
	ret = __ccw_device_check_pgid(cdev);
	memset(&cdev->private->irb, 0, sizeof(struct irb));
	switch (ret) {
	/* 0, -ETIME, -EAGAIN, -EOPNOTSUPP or -EACCES */
	case 0:			/* disband successful. */
		ccw_device_disband_done(cdev, ret);
		break;
	case -EOPNOTSUPP:
		/*
		 * One of those strange devices which claim to be able
		 * to do multipathing but not for Unset Path Group ID.
		 */
		cdev->private->flags.pgid_single = 1;
		/* fall through. */
	case -EAGAIN:		/* Try again. */
		__ccw_device_disband_start(cdev);
		break;
	case -ETIME:		/* Set path group id stopped by timeout. */
		ccw_device_disband_done(cdev, -ETIME);
		break;
	case -EACCES:		/* channel is not operational. */
		cdev->private->imask >>= 1;
		cdev->private->iretry = 5;
		__ccw_device_disband_start(cdev);
		break;
	}
}

void
ccw_device_disband_start(struct ccw_device *cdev)
{
	/* After 60s disbanding is considered to have failed. */
	ccw_device_set_timeout(cdev, 60*HZ);

	cdev->private->flags.pgid_single = 0;
	cdev->private->iretry = 5;
	cdev->private->imask = 0x80;
	__ccw_device_disband_start(cdev);
 *  CCW device PGID and path verification I/O handling.
 *
 *    Copyright IBM Corp. 2002, 2009
 *    Author(s): Cornelia Huck <cornelia.huck@de.ibm.com>
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>
 *		 Peter Oberparleiter <peter.oberparleiter@de.ibm.com>
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/bitops.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <asm/ccwdev.h>
#include <asm/cio.h>

#include "cio.h"
#include "cio_debug.h"
#include "device.h"
#include "io_sch.h"

#define PGID_RETRIES	256
#define PGID_TIMEOUT	(10 * HZ)

static void verify_start(struct ccw_device *cdev);

/*
 * Process path verification data and report result.
 */
static void verify_done(struct ccw_device *cdev, int rc)
{
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	struct ccw_dev_id *id = &cdev->private->dev_id;
	int mpath = cdev->private->flags.mpath;
	int pgroup = cdev->private->flags.pgroup;

	if (rc)
		goto out;
	/* Ensure consistent multipathing state at device and channel. */
	if (sch->config.mp != mpath) {
		sch->config.mp = mpath;
		rc = cio_commit_config(sch);
	}
out:
	CIO_MSG_EVENT(2, "vrfy: device 0.%x.%04x: rc=%d pgroup=%d mpath=%d "
			 "vpm=%02x\n", id->ssid, id->devno, rc, pgroup, mpath,
			 sch->vpm);
	ccw_device_verify_done(cdev, rc);
}

/*
 * Create channel program to perform a NOOP.
 */
static void nop_build_cp(struct ccw_device *cdev)
{
	struct ccw_request *req = &cdev->private->req;
	struct ccw1 *cp = cdev->private->iccws;

	cp->cmd_code	= CCW_CMD_NOOP;
	cp->cda		= 0;
	cp->count	= 0;
	cp->flags	= CCW_FLAG_SLI;
	req->cp		= cp;
}

/*
 * Perform NOOP on a single path.
 */
static void nop_do(struct ccw_device *cdev)
{
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	struct ccw_request *req = &cdev->private->req;

	req->lpm = lpm_adjust(req->lpm, sch->schib.pmcw.pam & sch->opm &
			      ~cdev->private->path_noirq_mask);
	if (!req->lpm)
		goto out_nopath;
	nop_build_cp(cdev);
	ccw_request_start(cdev);
	return;

out_nopath:
	verify_done(cdev, sch->vpm ? 0 : -EACCES);
}

/*
 * Adjust NOOP I/O status.
 */
static enum io_status nop_filter(struct ccw_device *cdev, void *data,
				 struct irb *irb, enum io_status status)
{
	/* Only subchannel status might indicate a path error. */
	if (status == IO_STATUS_ERROR && irb->scsw.cmd.cstat == 0)
		return IO_DONE;
	return status;
}

/*
 * Process NOOP request result for a single path.
 */
static void nop_callback(struct ccw_device *cdev, void *data, int rc)
{
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	struct ccw_request *req = &cdev->private->req;

	switch (rc) {
	case 0:
		sch->vpm |= req->lpm;
		break;
	case -ETIME:
		cdev->private->path_noirq_mask |= req->lpm;
		break;
	case -EACCES:
		cdev->private->path_notoper_mask |= req->lpm;
		break;
	default:
		goto err;
	}
	/* Continue on the next path. */
	req->lpm >>= 1;
	nop_do(cdev);
	return;

err:
	verify_done(cdev, rc);
}

/*
 * Create channel program to perform SET PGID on a single path.
 */
static void spid_build_cp(struct ccw_device *cdev, u8 fn)
{
	struct ccw_request *req = &cdev->private->req;
	struct ccw1 *cp = cdev->private->iccws;
	int i = pathmask_to_pos(req->lpm);
	struct pgid *pgid = &cdev->private->pgid[i];

	pgid->inf.fc	= fn;
	cp->cmd_code	= CCW_CMD_SET_PGID;
	cp->cda		= (u32) (addr_t) pgid;
	cp->count	= sizeof(*pgid);
	cp->flags	= CCW_FLAG_SLI;
	req->cp		= cp;
}

static void pgid_wipeout_callback(struct ccw_device *cdev, void *data, int rc)
{
	if (rc) {
		/* We don't know the path groups' state. Abort. */
		verify_done(cdev, rc);
		return;
	}
	/*
	 * Path groups have been reset. Restart path verification but
	 * leave paths in path_noirq_mask out.
	 */
	cdev->private->flags.pgid_unknown = 0;
	verify_start(cdev);
}

/*
 * Reset pathgroups and restart path verification, leave unusable paths out.
 */
static void pgid_wipeout_start(struct ccw_device *cdev)
{
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	struct ccw_dev_id *id = &cdev->private->dev_id;
	struct ccw_request *req = &cdev->private->req;
	u8 fn;

	CIO_MSG_EVENT(2, "wipe: device 0.%x.%04x: pvm=%02x nim=%02x\n",
		      id->ssid, id->devno, cdev->private->pgid_valid_mask,
		      cdev->private->path_noirq_mask);

	/* Initialize request data. */
	memset(req, 0, sizeof(*req));
	req->timeout	= PGID_TIMEOUT;
	req->maxretries	= PGID_RETRIES;
	req->lpm	= sch->schib.pmcw.pam;
	req->callback	= pgid_wipeout_callback;
	fn = SPID_FUNC_DISBAND;
	if (cdev->private->flags.mpath)
		fn |= SPID_FUNC_MULTI_PATH;
	spid_build_cp(cdev, fn);
	ccw_request_start(cdev);
}

/*
 * Perform establish/resign SET PGID on a single path.
 */
static void spid_do(struct ccw_device *cdev)
{
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	struct ccw_request *req = &cdev->private->req;
	u8 fn;

	/* Use next available path that is not already in correct state. */
	req->lpm = lpm_adjust(req->lpm, cdev->private->pgid_todo_mask);
	if (!req->lpm)
		goto out_nopath;
	/* Channel program setup. */
	if (req->lpm & sch->opm)
		fn = SPID_FUNC_ESTABLISH;
	else
		fn = SPID_FUNC_RESIGN;
	if (cdev->private->flags.mpath)
		fn |= SPID_FUNC_MULTI_PATH;
	spid_build_cp(cdev, fn);
	ccw_request_start(cdev);
	return;

out_nopath:
	if (cdev->private->flags.pgid_unknown) {
		/* At least one SPID could be partially done. */
		pgid_wipeout_start(cdev);
		return;
	}
	verify_done(cdev, sch->vpm ? 0 : -EACCES);
}

/*
 * Process SET PGID request result for a single path.
 */
static void spid_callback(struct ccw_device *cdev, void *data, int rc)
{
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	struct ccw_request *req = &cdev->private->req;

	switch (rc) {
	case 0:
		sch->vpm |= req->lpm & sch->opm;
		break;
	case -ETIME:
		cdev->private->flags.pgid_unknown = 1;
		cdev->private->path_noirq_mask |= req->lpm;
		break;
	case -EACCES:
		cdev->private->path_notoper_mask |= req->lpm;
		break;
	case -EOPNOTSUPP:
		if (cdev->private->flags.mpath) {
			/* Try without multipathing. */
			cdev->private->flags.mpath = 0;
			goto out_restart;
		}
		/* Try without pathgrouping. */
		cdev->private->flags.pgroup = 0;
		goto out_restart;
	default:
		goto err;
	}
	req->lpm >>= 1;
	spid_do(cdev);
	return;

out_restart:
	verify_start(cdev);
	return;
err:
	verify_done(cdev, rc);
}

static void spid_start(struct ccw_device *cdev)
{
	struct ccw_request *req = &cdev->private->req;

	/* Initialize request data. */
	memset(req, 0, sizeof(*req));
	req->timeout	= PGID_TIMEOUT;
	req->maxretries	= PGID_RETRIES;
	req->lpm	= 0x80;
	req->singlepath	= 1;
	req->callback	= spid_callback;
	spid_do(cdev);
}

static int pgid_is_reset(struct pgid *p)
{
	char *c;

	for (c = (char *)p + 1; c < (char *)(p + 1); c++) {
		if (*c != 0)
			return 0;
	}
	return 1;
}

static int pgid_cmp(struct pgid *p1, struct pgid *p2)
{
	return memcmp((char *) p1 + 1, (char *) p2 + 1,
		      sizeof(struct pgid) - 1);
}

/*
 * Determine pathgroup state from PGID data.
 */
static void pgid_analyze(struct ccw_device *cdev, struct pgid **p,
			 int *mismatch, u8 *reserved, u8 *reset)
{
	struct pgid *pgid = &cdev->private->pgid[0];
	struct pgid *first = NULL;
	int lpm;
	int i;

	*mismatch = 0;
	*reserved = 0;
	*reset = 0;
	for (i = 0, lpm = 0x80; i < 8; i++, pgid++, lpm >>= 1) {
		if ((cdev->private->pgid_valid_mask & lpm) == 0)
			continue;
		if (pgid->inf.ps.state2 == SNID_STATE2_RESVD_ELSE)
			*reserved |= lpm;
		if (pgid_is_reset(pgid)) {
			*reset |= lpm;
			continue;
		}
		if (!first) {
			first = pgid;
			continue;
		}
		if (pgid_cmp(pgid, first) != 0)
			*mismatch = 1;
	}
	if (!first)
		first = &channel_subsystems[0]->global_pgid;
	*p = first;
}

static u8 pgid_to_donepm(struct ccw_device *cdev)
{
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	struct pgid *pgid;
	int i;
	int lpm;
	u8 donepm = 0;

	/* Set bits for paths which are already in the target state. */
	for (i = 0; i < 8; i++) {
		lpm = 0x80 >> i;
		if ((cdev->private->pgid_valid_mask & lpm) == 0)
			continue;
		pgid = &cdev->private->pgid[i];
		if (sch->opm & lpm) {
			if (pgid->inf.ps.state1 != SNID_STATE1_GROUPED)
				continue;
		} else {
			if (pgid->inf.ps.state1 != SNID_STATE1_UNGROUPED)
				continue;
		}
		if (cdev->private->flags.mpath) {
			if (pgid->inf.ps.state3 != SNID_STATE3_MULTI_PATH)
				continue;
		} else {
			if (pgid->inf.ps.state3 != SNID_STATE3_SINGLE_PATH)
				continue;
		}
		donepm |= lpm;
	}

	return donepm;
}

static void pgid_fill(struct ccw_device *cdev, struct pgid *pgid)
{
	int i;

	for (i = 0; i < 8; i++)
		memcpy(&cdev->private->pgid[i], pgid, sizeof(struct pgid));
}

/*
 * Process SENSE PGID data and report result.
 */
static void snid_done(struct ccw_device *cdev, int rc)
{
	struct ccw_dev_id *id = &cdev->private->dev_id;
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	struct pgid *pgid;
	int mismatch = 0;
	u8 reserved = 0;
	u8 reset = 0;
	u8 donepm;

	if (rc)
		goto out;
	pgid_analyze(cdev, &pgid, &mismatch, &reserved, &reset);
	if (reserved == cdev->private->pgid_valid_mask)
		rc = -EUSERS;
	else if (mismatch)
		rc = -EOPNOTSUPP;
	else {
		donepm = pgid_to_donepm(cdev);
		sch->vpm = donepm & sch->opm;
		cdev->private->pgid_reset_mask |= reset;
		cdev->private->pgid_todo_mask &=
			~(donepm | cdev->private->path_noirq_mask);
		pgid_fill(cdev, pgid);
	}
out:
	CIO_MSG_EVENT(2, "snid: device 0.%x.%04x: rc=%d pvm=%02x vpm=%02x "
		      "todo=%02x mism=%d rsvd=%02x reset=%02x\n", id->ssid,
		      id->devno, rc, cdev->private->pgid_valid_mask, sch->vpm,
		      cdev->private->pgid_todo_mask, mismatch, reserved, reset);
	switch (rc) {
	case 0:
		if (cdev->private->flags.pgid_unknown) {
			pgid_wipeout_start(cdev);
			return;
		}
		/* Anything left to do? */
		if (cdev->private->pgid_todo_mask == 0) {
			verify_done(cdev, sch->vpm == 0 ? -EACCES : 0);
			return;
		}
		/* Perform path-grouping. */
		spid_start(cdev);
		break;
	case -EOPNOTSUPP:
		/* Path-grouping not supported. */
		cdev->private->flags.pgroup = 0;
		cdev->private->flags.mpath = 0;
		verify_start(cdev);
		break;
	default:
		verify_done(cdev, rc);
	}
}

/*
 * Create channel program to perform a SENSE PGID on a single path.
 */
static void snid_build_cp(struct ccw_device *cdev)
{
	struct ccw_request *req = &cdev->private->req;
	struct ccw1 *cp = cdev->private->iccws;
	int i = pathmask_to_pos(req->lpm);

	/* Channel program setup. */
	cp->cmd_code	= CCW_CMD_SENSE_PGID;
	cp->cda		= (u32) (addr_t) &cdev->private->pgid[i];
	cp->count	= sizeof(struct pgid);
	cp->flags	= CCW_FLAG_SLI;
	req->cp		= cp;
}

/*
 * Perform SENSE PGID on a single path.
 */
static void snid_do(struct ccw_device *cdev)
{
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	struct ccw_request *req = &cdev->private->req;
	int ret;

	req->lpm = lpm_adjust(req->lpm, sch->schib.pmcw.pam &
			      ~cdev->private->path_noirq_mask);
	if (!req->lpm)
		goto out_nopath;
	snid_build_cp(cdev);
	ccw_request_start(cdev);
	return;

out_nopath:
	if (cdev->private->pgid_valid_mask)
		ret = 0;
	else if (cdev->private->path_noirq_mask)
		ret = -ETIME;
	else
		ret = -EACCES;
	snid_done(cdev, ret);
}

/*
 * Process SENSE PGID request result for single path.
 */
static void snid_callback(struct ccw_device *cdev, void *data, int rc)
{
	struct ccw_request *req = &cdev->private->req;

	switch (rc) {
	case 0:
		cdev->private->pgid_valid_mask |= req->lpm;
		break;
	case -ETIME:
		cdev->private->flags.pgid_unknown = 1;
		cdev->private->path_noirq_mask |= req->lpm;
		break;
	case -EACCES:
		cdev->private->path_notoper_mask |= req->lpm;
		break;
	default:
		goto err;
	}
	/* Continue on the next path. */
	req->lpm >>= 1;
	snid_do(cdev);
	return;

err:
	snid_done(cdev, rc);
}

/*
 * Perform path verification.
 */
static void verify_start(struct ccw_device *cdev)
{
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	struct ccw_request *req = &cdev->private->req;
	struct ccw_dev_id *devid = &cdev->private->dev_id;

	sch->vpm = 0;
	sch->lpm = sch->schib.pmcw.pam;

	/* Initialize PGID data. */
	memset(cdev->private->pgid, 0, sizeof(cdev->private->pgid));
	cdev->private->pgid_valid_mask = 0;
	cdev->private->pgid_todo_mask = sch->schib.pmcw.pam;
	cdev->private->path_notoper_mask = 0;

	/* Initialize request data. */
	memset(req, 0, sizeof(*req));
	req->timeout	= PGID_TIMEOUT;
	req->maxretries	= PGID_RETRIES;
	req->lpm	= 0x80;
	req->singlepath	= 1;
	if (cdev->private->flags.pgroup) {
		CIO_TRACE_EVENT(4, "snid");
		CIO_HEX_EVENT(4, devid, sizeof(*devid));
		req->callback	= snid_callback;
		snid_do(cdev);
	} else {
		CIO_TRACE_EVENT(4, "nop");
		CIO_HEX_EVENT(4, devid, sizeof(*devid));
		req->filter	= nop_filter;
		req->callback	= nop_callback;
		nop_do(cdev);
	}
}

/**
 * ccw_device_verify_start - perform path verification
 * @cdev: ccw device
 *
 * Perform an I/O on each available channel path to @cdev to determine which
 * paths are operational. The resulting path mask is stored in sch->vpm.
 * If device options specify pathgrouping, establish a pathgroup for the
 * operational paths. When finished, call ccw_device_verify_done with a
 * return code specifying the result.
 */
void ccw_device_verify_start(struct ccw_device *cdev)
{
	CIO_TRACE_EVENT(4, "vrfy");
	CIO_HEX_EVENT(4, &cdev->private->dev_id, sizeof(cdev->private->dev_id));
	/*
	 * Initialize pathgroup and multipath state with target values.
	 * They may change in the course of path verification.
	 */
	cdev->private->flags.pgroup = cdev->private->options.pgroup;
	cdev->private->flags.mpath = cdev->private->options.mpath;
	cdev->private->flags.doverify = 0;
	cdev->private->path_noirq_mask = 0;
	verify_start(cdev);
}

/*
 * Process disband SET PGID request result.
 */
static void disband_callback(struct ccw_device *cdev, void *data, int rc)
{
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	struct ccw_dev_id *id = &cdev->private->dev_id;

	if (rc)
		goto out;
	/* Ensure consistent multipathing state at device and channel. */
	cdev->private->flags.mpath = 0;
	if (sch->config.mp) {
		sch->config.mp = 0;
		rc = cio_commit_config(sch);
	}
out:
	CIO_MSG_EVENT(0, "disb: device 0.%x.%04x: rc=%d\n", id->ssid, id->devno,
		      rc);
	ccw_device_disband_done(cdev, rc);
}

/**
 * ccw_device_disband_start - disband pathgroup
 * @cdev: ccw device
 *
 * Execute a SET PGID channel program on @cdev to disband a previously
 * established pathgroup. When finished, call ccw_device_disband_done with
 * a return code specifying the result.
 */
void ccw_device_disband_start(struct ccw_device *cdev)
{
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	struct ccw_request *req = &cdev->private->req;
	u8 fn;

	CIO_TRACE_EVENT(4, "disb");
	CIO_HEX_EVENT(4, &cdev->private->dev_id, sizeof(cdev->private->dev_id));
	/* Request setup. */
	memset(req, 0, sizeof(*req));
	req->timeout	= PGID_TIMEOUT;
	req->maxretries	= PGID_RETRIES;
	req->lpm	= sch->schib.pmcw.pam & sch->opm;
	req->singlepath	= 1;
	req->callback	= disband_callback;
	fn = SPID_FUNC_DISBAND;
	if (cdev->private->flags.mpath)
		fn |= SPID_FUNC_MULTI_PATH;
	spid_build_cp(cdev, fn);
	ccw_request_start(cdev);
}

struct stlck_data {
	struct completion done;
	int rc;
};

static void stlck_build_cp(struct ccw_device *cdev, void *buf1, void *buf2)
{
	struct ccw_request *req = &cdev->private->req;
	struct ccw1 *cp = cdev->private->iccws;

	cp[0].cmd_code = CCW_CMD_STLCK;
	cp[0].cda = (u32) (addr_t) buf1;
	cp[0].count = 32;
	cp[0].flags = CCW_FLAG_CC;
	cp[1].cmd_code = CCW_CMD_RELEASE;
	cp[1].cda = (u32) (addr_t) buf2;
	cp[1].count = 32;
	cp[1].flags = 0;
	req->cp = cp;
}

static void stlck_callback(struct ccw_device *cdev, void *data, int rc)
{
	struct stlck_data *sdata = data;

	sdata->rc = rc;
	complete(&sdata->done);
}

/**
 * ccw_device_stlck_start - perform unconditional release
 * @cdev: ccw device
 * @data: data pointer to be passed to ccw_device_stlck_done
 * @buf1: data pointer used in channel program
 * @buf2: data pointer used in channel program
 *
 * Execute a channel program on @cdev to release an existing PGID reservation.
 */
static void ccw_device_stlck_start(struct ccw_device *cdev, void *data,
				   void *buf1, void *buf2)
{
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	struct ccw_request *req = &cdev->private->req;

	CIO_TRACE_EVENT(4, "stlck");
	CIO_HEX_EVENT(4, &cdev->private->dev_id, sizeof(cdev->private->dev_id));
	/* Request setup. */
	memset(req, 0, sizeof(*req));
	req->timeout	= PGID_TIMEOUT;
	req->maxretries	= PGID_RETRIES;
	req->lpm	= sch->schib.pmcw.pam & sch->opm;
	req->data	= data;
	req->callback	= stlck_callback;
	stlck_build_cp(cdev, buf1, buf2);
	ccw_request_start(cdev);
}

/*
 * Perform unconditional reserve + release.
 */
int ccw_device_stlck(struct ccw_device *cdev)
{
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	struct stlck_data data;
	u8 *buffer;
	int rc;

	/* Check if steal lock operation is valid for this device. */
	if (cdev->drv) {
		if (!cdev->private->options.force)
			return -EINVAL;
	}
	buffer = kzalloc(64, GFP_DMA | GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;
	init_completion(&data.done);
	data.rc = -EIO;
	spin_lock_irq(sch->lock);
	rc = cio_enable_subchannel(sch, (u32) (addr_t) sch);
	if (rc)
		goto out_unlock;
	/* Perform operation. */
	cdev->private->state = DEV_STATE_STEAL_LOCK;
	ccw_device_stlck_start(cdev, &data, &buffer[0], &buffer[32]);
	spin_unlock_irq(sch->lock);
	/* Wait for operation to finish. */
	if (wait_for_completion_interruptible(&data.done)) {
		/* Got a signal. */
		spin_lock_irq(sch->lock);
		ccw_request_cancel(cdev);
		spin_unlock_irq(sch->lock);
		wait_for_completion(&data.done);
	}
	rc = data.rc;
	/* Check results. */
	spin_lock_irq(sch->lock);
	cio_disable_subchannel(sch);
	cdev->private->state = DEV_STATE_BOXED;
out_unlock:
	spin_unlock_irq(sch->lock);
	kfree(buffer);

	return rc;
}
