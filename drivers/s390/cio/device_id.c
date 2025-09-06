/*
 * drivers/s390/cio/device_id.c
 *
 *    Copyright (C) 2002 IBM Deutschland Entwicklung GmbH,
 *			 IBM Corporation
 *    Author(s): Cornelia Huck (cornelia.huck@de.ibm.com)
 *		 Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 * Sense ID functions.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>

#include <asm/ccwdev.h>
#include <asm/delay.h>
#include <asm/cio.h>
#include <asm/lowcore.h>
 *  CCW device SENSE ID I/O handling.
 *
 *    Copyright IBM Corp. 2002, 2009
 *    Author(s): Cornelia Huck <cornelia.huck@de.ibm.com>
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>
 *		 Peter Oberparleiter <peter.oberparleiter@de.ibm.com>
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <asm/ccwdev.h>
#include <asm/setup.h>
#include <asm/cio.h>
#include <asm/diag.h>

#include "cio.h"
#include "cio_debug.h"
#include "css.h"
#include "device.h"
#include "ioasm.h"
#include "io_sch.h"

/**
 * vm_vdev_to_cu_type - Convert vm virtual device into control unit type
 *			for certain devices.
 * @class: virtual device class
 * @type: virtual device type
 *
 * Returns control unit type if a match was made or %0xffff otherwise.
 */
static int vm_vdev_to_cu_type(int class, int type)
#include "device.h"
#include "io_sch.h"

#define SENSE_ID_RETRIES	256
#define SENSE_ID_TIMEOUT	(10 * HZ)
#define SENSE_ID_MIN_LEN	4
#define SENSE_ID_BASIC_LEN	7

/**
 * diag210_to_senseid - convert diag 0x210 data to sense id information
 * @senseid: sense id
 * @diag: diag 0x210 data
 *
 * Return 0 on success, non-zero otherwise.
 */
static int diag210_to_senseid(struct senseid *senseid, struct diag210 *diag)
{
	static struct {
		int class, type, cu_type;
	} vm_devices[] = {
		{ 0x08, 0x01, 0x3480 },
		{ 0x08, 0x02, 0x3430 },
		{ 0x08, 0x10, 0x3420 },
		{ 0x08, 0x42, 0x3424 },
		{ 0x08, 0x44, 0x9348 },
		{ 0x08, 0x81, 0x3490 },
		{ 0x08, 0x82, 0x3422 },
		{ 0x10, 0x41, 0x1403 },
		{ 0x10, 0x42, 0x3211 },
		{ 0x10, 0x43, 0x3203 },
		{ 0x10, 0x45, 0x3800 },
		{ 0x10, 0x47, 0x3262 },
		{ 0x10, 0x48, 0x3820 },
		{ 0x10, 0x49, 0x3800 },
		{ 0x10, 0x4a, 0x4245 },
		{ 0x10, 0x4b, 0x4248 },
		{ 0x10, 0x4d, 0x3800 },
		{ 0x10, 0x4e, 0x3820 },
		{ 0x10, 0x4f, 0x3820 },
		{ 0x10, 0x82, 0x2540 },
		{ 0x10, 0x84, 0x3525 },
		{ 0x20, 0x81, 0x2501 },
		{ 0x20, 0x82, 0x2540 },
		{ 0x20, 0x84, 0x3505 },
		{ 0x40, 0x01, 0x3278 },
		{ 0x40, 0x04, 0x3277 },
		{ 0x40, 0x80, 0x2250 },
		{ 0x40, 0xc0, 0x5080 },
		{ 0x80, 0x00, 0x3215 },
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(vm_devices); i++)
		if (class == vm_devices[i].class && type == vm_devices[i].type)
			return vm_devices[i].cu_type;

	return 0xffff;
}

/**
 * diag_get_dev_info - retrieve device information via DIAG X'210'
 * @devno: device number
 * @ps: pointer to sense ID data area
 *
 * Returns zero on success, non-zero otherwise.
 */
static int diag_get_dev_info(u16 devno, struct senseid *ps)
{
	struct diag210 diag_data;
	int ccode;

	CIO_TRACE_EVENT (4, "VMvdinf");

	diag_data = (struct diag210) {
		.vrdcdvno = devno,
		.vrdclen = sizeof (diag_data),
	};

	ccode = diag210 (&diag_data);
	if ((ccode == 0) || (ccode == 2)) {
		ps->reserved = 0xff;

		/* Special case for osa devices. */
		if (diag_data.vrdcvcla == 0x02 && diag_data.vrdcvtyp == 0x20) {
			ps->cu_type = 0x3088;
			ps->cu_model = 0x60;
			return 0;
		}
		ps->cu_type = vm_vdev_to_cu_type(diag_data.vrdcvcla,
						diag_data.vrdcvtyp);
		if (ps->cu_type != 0xffff)
			return 0;
	}

	CIO_MSG_EVENT(0, "DIAG X'210' for device %04X returned (cc = %d):"
		      "vdev class : %02X, vdev type : %04X \n ...  "
		      "rdev class : %02X, rdev type : %04X, "
		      "rdev model: %02X\n",
		      devno, ccode,
		      diag_data.vrdcvcla, diag_data.vrdcvtyp,
		      diag_data.vrdcrccl, diag_data.vrdccrty,
		      diag_data.vrdccrmd);

	return -ENODEV;
}

/*
 * Start Sense ID helper function.
 * Try to obtain the 'control unit'/'device type' information
 *  associated with the subchannel.
 */
static int
__ccw_device_sense_id_start(struct ccw_device *cdev)
{
	struct subchannel *sch;
	struct ccw1 *ccw;
	int ret;

	sch = to_subchannel(cdev->dev.parent);
	/* Setup sense channel program. */
	ccw = cdev->private->iccws;
	ccw->cmd_code = CCW_CMD_SENSE_ID;
	ccw->cda = (__u32) __pa (&cdev->private->senseid);
	ccw->count = sizeof (struct senseid);
	ccw->flags = CCW_FLAG_SLI;

	/* Reset device status. */
	memset(&cdev->private->irb, 0, sizeof(struct irb));

	/* Try on every path. */
	ret = -ENODEV;
	while (cdev->private->imask != 0) {
		cdev->private->senseid.cu_type = 0xFFFF;
		if ((sch->opm & cdev->private->imask) != 0 &&
		    cdev->private->iretry > 0) {
			cdev->private->iretry--;
			/* Reset internal retry indication. */
			cdev->private->flags.intretry = 0;
			ret = cio_start (sch, cdev->private->iccws,
					 cdev->private->imask);
			/* ret is 0, -EBUSY, -EACCES or -ENODEV */
			if (ret != -EACCES)
				return ret;
		}
		cdev->private->imask >>= 1;
		cdev->private->iretry = 5;
	}
	return ret;
}

void
ccw_device_sense_id_start(struct ccw_device *cdev)
{
	int ret;

	memset (&cdev->private->senseid, 0, sizeof (struct senseid));
	cdev->private->imask = 0x80;
	cdev->private->iretry = 5;
	ret = __ccw_device_sense_id_start(cdev);
	if (ret && ret != -EBUSY)
		ccw_device_sense_id_done(cdev, ret);
}

/*
 * Called from interrupt context to check if a valid answer
 * to Sense ID was received.
 */
static int
ccw_device_check_sense_id(struct ccw_device *cdev)
{
	struct subchannel *sch;
	struct irb *irb;

	sch = to_subchannel(cdev->dev.parent);
	irb = &cdev->private->irb;

	/* Check the error cases. */
	if (irb->scsw.cmd.fctl & (SCSW_FCTL_HALT_FUNC | SCSW_FCTL_CLEAR_FUNC)) {
		/* Retry Sense ID if requested. */
		if (cdev->private->flags.intretry) {
			cdev->private->flags.intretry = 0;
			return -EAGAIN;
		}
		return -ETIME;
	}
	if (irb->esw.esw0.erw.cons && (irb->ecw[0] & SNS0_CMD_REJECT)) {
		/*
		 * if the device doesn't support the SenseID
		 *  command further retries wouldn't help ...
		 * NB: We don't check here for intervention required like we
		 *     did before, because tape devices with no tape inserted
		 *     may present this status *in conjunction with* the
		 *     sense id information. So, for intervention required,
		 *     we use the "whack it until it talks" strategy...
		 */
		CIO_MSG_EVENT(0, "SenseID : device %04x on Subchannel "
			      "0.%x.%04x reports cmd reject\n",
			      cdev->private->dev_id.devno, sch->schid.ssid,
			      sch->schid.sch_no);
		return -EOPNOTSUPP;
	}
	if (irb->esw.esw0.erw.cons) {
		CIO_MSG_EVENT(2, "SenseID : UC on dev 0.%x.%04x, "
			      "lpum %02X, cnt %02d, sns :"
			      " %02X%02X%02X%02X %02X%02X%02X%02X ...\n",
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
		if ((lpm & sch->schib.pmcw.pim & sch->schib.pmcw.pam) != 0)
			CIO_MSG_EVENT(4, "SenseID : path %02X for device %04x "
				      "on subchannel 0.%x.%04x is "
				      "'not operational'\n", lpm,
				      cdev->private->dev_id.devno,
				      sch->schid.ssid, sch->schid.sch_no);
		return -EACCES;
	}

	/* Did we get a proper answer ? */
	if (irb->scsw.cmd.cc == 0 && cdev->private->senseid.cu_type != 0xFFFF &&
	    cdev->private->senseid.reserved == 0xFF) {
		if (irb->scsw.cmd.count < sizeof(struct senseid) - 8)
			cdev->private->flags.esid = 1;
		return 0; /* Success */
	}

	/* Hmm, whatever happened, try again. */
	CIO_MSG_EVENT(2, "SenseID : start_IO() for device %04x on "
		      "subchannel 0.%x.%04x returns status %02X%02X\n",
		      cdev->private->dev_id.devno, sch->schid.ssid,
		      sch->schid.sch_no,
		      irb->scsw.cmd.dstat, irb->scsw.cmd.cstat);
	/* Special case for osa devices. */
	if (diag->vrdcvcla == 0x02 && diag->vrdcvtyp == 0x20) {
		senseid->cu_type = 0x3088;
		senseid->cu_model = 0x60;
		senseid->reserved = 0xff;
		return 0;
	}
	for (i = 0; i < ARRAY_SIZE(vm_devices); i++) {
		if (diag->vrdcvcla == vm_devices[i].class &&
		    diag->vrdcvtyp == vm_devices[i].type) {
			senseid->cu_type = vm_devices[i].cu_type;
			senseid->reserved = 0xff;
			return 0;
		}
	}

	return -ENODEV;
}

/**
 * diag_get_dev_info - retrieve device information via diag 0x210
 * @cdev: ccw device
 *
 * Returns zero on success, non-zero otherwise.
 */
static int diag210_get_dev_info(struct ccw_device *cdev)
{
	struct ccw_dev_id *dev_id = &cdev->private->dev_id;
	struct senseid *senseid = &cdev->private->senseid;
	struct diag210 diag_data;
	int rc;

	if (dev_id->ssid != 0)
		return -ENODEV;
	memset(&diag_data, 0, sizeof(diag_data));
	diag_data.vrdcdvno	= dev_id->devno;
	diag_data.vrdclen	= sizeof(diag_data);
	rc = diag210(&diag_data);
	CIO_TRACE_EVENT(4, "diag210");
	CIO_HEX_EVENT(4, &rc, sizeof(rc));
	CIO_HEX_EVENT(4, &diag_data, sizeof(diag_data));
	if (rc != 0 && rc != 2)
		goto err_failed;
	if (diag210_to_senseid(senseid, &diag_data))
		goto err_unknown;
	return 0;

err_unknown:
	CIO_MSG_EVENT(0, "snsid: device 0.%x.%04x: unknown diag210 data\n",
		      dev_id->ssid, dev_id->devno);
	return -ENODEV;
err_failed:
	CIO_MSG_EVENT(0, "snsid: device 0.%x.%04x: diag210 failed (rc=%d)\n",
		      dev_id->ssid, dev_id->devno, rc);
	return -ENODEV;
}

/*
 * Initialize SENSE ID data.
 */
static void snsid_init(struct ccw_device *cdev)
{
	cdev->private->flags.esid = 0;
	memset(&cdev->private->senseid, 0, sizeof(cdev->private->senseid));
	cdev->private->senseid.cu_type = 0xffff;
}

/*
 * Check for complete SENSE ID data.
 */
static int snsid_check(struct ccw_device *cdev, void *data)
{
	struct cmd_scsw *scsw = &cdev->private->irb.scsw.cmd;
	int len = sizeof(struct senseid) - scsw->count;

	/* Check for incomplete SENSE ID data. */
	if (len < SENSE_ID_MIN_LEN)
		goto out_restart;
	if (cdev->private->senseid.cu_type == 0xffff)
		goto out_restart;
	/* Check for incompatible SENSE ID data. */
	if (cdev->private->senseid.reserved != 0xff)
		return -EOPNOTSUPP;
	/* Check for extended-identification information. */
	if (len > SENSE_ID_BASIC_LEN)
		cdev->private->flags.esid = 1;
	return 0;

out_restart:
	snsid_init(cdev);
	return -EAGAIN;
}

/*
 * Got interrupt for Sense ID.
 */
void
ccw_device_sense_id_irq(struct ccw_device *cdev, enum dev_event dev_event)
{
	struct subchannel *sch;
	struct irb *irb;
	int ret;

	sch = to_subchannel(cdev->dev.parent);
	irb = (struct irb *) __LC_IRB;
	/* Retry sense id, if needed. */
	if (irb->scsw.cmd.stctl ==
	    (SCSW_STCTL_STATUS_PEND | SCSW_STCTL_ALERT_STATUS)) {
		if ((irb->scsw.cmd.cc == 1) || !irb->scsw.cmd.actl) {
			ret = __ccw_device_sense_id_start(cdev);
			if (ret && ret != -EBUSY)
				ccw_device_sense_id_done(cdev, ret);
		}
		return;
	}
	if (ccw_device_accumulate_and_sense(cdev, irb) != 0)
		return;
	ret = ccw_device_check_sense_id(cdev);
	memset(&cdev->private->irb, 0, sizeof(struct irb));
	switch (ret) {
	/* 0, -ETIME, -EOPNOTSUPP, -EAGAIN or -EACCES */
	case 0:			/* Sense id succeeded. */
	case -ETIME:		/* Sense id stopped by timeout. */
		ccw_device_sense_id_done(cdev, ret);
		break;
	case -EACCES:		/* channel is not operational. */
		sch->lpm &= ~cdev->private->imask;
		cdev->private->imask >>= 1;
		cdev->private->iretry = 5;
		/* fall through. */
	case -EAGAIN:		/* try again. */
		ret = __ccw_device_sense_id_start(cdev);
		if (ret == 0 || ret == -EBUSY)
			break;
		/* fall through. */
	default:		/* Sense ID failed. Try asking VM. */
		if (MACHINE_IS_VM)
			ret = diag_get_dev_info(cdev->private->dev_id.devno,
						&cdev->private->senseid);
		else
			/*
			 * If we can't couldn't identify the device type we
			 *  consider the device "not operational".
			 */
			ret = -ENODEV;

		ccw_device_sense_id_done(cdev, ret);
		break;
	}
 * Process SENSE ID request result.
 */
static void snsid_callback(struct ccw_device *cdev, void *data, int rc)
{
	struct ccw_dev_id *id = &cdev->private->dev_id;
	struct senseid *senseid = &cdev->private->senseid;
	int vm = 0;

	if (rc && MACHINE_IS_VM) {
		/* Try diag 0x210 fallback on z/VM. */
		snsid_init(cdev);
		if (diag210_get_dev_info(cdev) == 0) {
			rc = 0;
			vm = 1;
		}
	}
	CIO_MSG_EVENT(2, "snsid: device 0.%x.%04x: rc=%d %04x/%02x "
		      "%04x/%02x%s\n", id->ssid, id->devno, rc,
		      senseid->cu_type, senseid->cu_model, senseid->dev_type,
		      senseid->dev_model, vm ? " (diag210)" : "");
	ccw_device_sense_id_done(cdev, rc);
}

/**
 * ccw_device_sense_id_start - perform SENSE ID
 * @cdev: ccw device
 *
 * Execute a SENSE ID channel program on @cdev to update its sense id
 * information. When finished, call ccw_device_sense_id_done with a
 * return code specifying the result.
 */
void ccw_device_sense_id_start(struct ccw_device *cdev)
{
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	struct ccw_request *req = &cdev->private->req;
	struct ccw1 *cp = cdev->private->iccws;

	CIO_TRACE_EVENT(4, "snsid");
	CIO_HEX_EVENT(4, &cdev->private->dev_id, sizeof(cdev->private->dev_id));
	/* Data setup. */
	snsid_init(cdev);
	/* Channel program setup. */
	cp->cmd_code	= CCW_CMD_SENSE_ID;
	cp->cda		= (u32) (addr_t) &cdev->private->senseid;
	cp->count	= sizeof(struct senseid);
	cp->flags	= CCW_FLAG_SLI;
	/* Request setup. */
	memset(req, 0, sizeof(*req));
	req->cp		= cp;
	req->timeout	= SENSE_ID_TIMEOUT;
	req->maxretries	= SENSE_ID_RETRIES;
	req->lpm	= sch->schib.pmcw.pam & sch->opm;
	req->check	= snsid_check;
	req->callback	= snsid_callback;
	ccw_request_start(cdev);
}
