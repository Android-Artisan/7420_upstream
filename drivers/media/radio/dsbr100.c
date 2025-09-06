/* A driver for the D-Link DSB-R100 USB radio.  The R100 plugs
 into both the USB and an analog audio input, so this thing
 only deals with initialisation and frequency setting, the
 audio data has to be handled by a sound driver.

 Major issue: I can't find out where the device reports the signal
 strength, and indeed the windows software appearantly just looks
 at the stereo indicator as well.  So, scanning will only find
 stereo stations.  Sad, but I can't help it.

 Also, the windows program sends oodles of messages over to the
 device, and I couldn't figure out their meaning.  My suspicion
 is that they don't have any:-)

 You might find some interesting stuff about this module at
 http://unimut.fsk.uni-heidelberg.de/unimut/demi/dsbr

 Copyright (c) 2000 Markus Demleitner <msdemlei@cl.uni-heidelberg.de>

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

 History:

 Version 0.43:
	Oliver Neukum: avoided DMA coherency issue

 Version 0.42:
	Converted dsbr100 to use video_ioctl2
	by Douglas Landgraf <dougsland@gmail.com>

 Version 0.41-ac1:
	Alan Cox: Some cleanups and fixes

 Version 0.41:
	Converted to V4L2 API by Mauro Carvalho Chehab <mchehab@infradead.org>

 Version 0.40:
	Markus: Updates for 2.6.x kernels, code layout changes, name sanitizing

 Version 0.30:
	Markus: Updates for 2.5.x kernel and more ISO compliant source

 Version 0.25:
	PSL and Markus: Cleanup, radio now doesn't stop on device close

 Version 0.24:
	Markus: Hope I got these silly VIDEO_TUNER_LOW issues finally
	right.  Some minor cleanup, improved standalone compilation

 Version 0.23:
	Markus: Sign extension bug fixed by declaring transfer_buffer unsigned

 Version 0.22:
	Markus: Some (brown bag) cleanup in what VIDIOCSTUNER returns,
	thanks to Mike Cox for pointing the problem out.

 Version 0.21:
	Markus: Minor cleanup, warnings if something goes wrong, lame attempt
	to adhere to Documentation/CodingStyle

 Version 0.2:
	Brad Hards <bradh@dynamite.com.au>: Fixes to make it work as non-module
	Markus: Copyright clarification

 Version 0.01: Markus: initial release

/* A driver for the D-Link DSB-R100 USB radio and Gemtek USB Radio 21.
 * The device plugs into both the USB and an analog audio input, so this thing
 * only deals with initialisation and frequency setting, the
 * audio data has to be handled by a sound driver.
 *
 * Major issue: I can't find out where the device reports the signal
 * strength, and indeed the windows software appearantly just looks
 * at the stereo indicator as well.  So, scanning will only find
 * stereo stations.  Sad, but I can't help it.
 *
 * Also, the windows program sends oodles of messages over to the
 * device, and I couldn't figure out their meaning.  My suspicion
 * is that they don't have any:-)
 *
 * You might find some interesting stuff about this module at
 * http://unimut.fsk.uni-heidelberg.de/unimut/demi/dsbr
 *
 * Fully tested with the Keene USB FM Transmitter and the v4l2-compliance tool.
 *
 * Copyright (c) 2000 Markus Demleitner <msdemlei@cl.uni-heidelberg.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/videodev2.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <linux/usb.h>
#include <linux/usb.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>

/*
 * Version Information
 */
#include <linux/version.h>	/* for KERNEL_VERSION MACRO	*/

#define DRIVER_VERSION "v0.41"
#define RADIO_VERSION KERNEL_VERSION(0,4,1)

static struct v4l2_queryctrl radio_qctrl[] = {
	{
		.id            = V4L2_CID_AUDIO_MUTE,
		.name          = "Mute",
		.minimum       = 0,
		.maximum       = 1,
		.default_value = 1,
		.type          = V4L2_CTRL_TYPE_BOOLEAN,
	}
};

#define DRIVER_AUTHOR "Markus Demleitner <msdemlei@tucana.harvard.edu>"
#define DRIVER_DESC "D-Link DSB-R100 USB FM radio driver"
MODULE_AUTHOR("Markus Demleitner <msdemlei@tucana.harvard.edu>");
MODULE_DESCRIPTION("D-Link DSB-R100 USB FM radio driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.1.0");

#define DSB100_VENDOR 0x04b4
#define DSB100_PRODUCT 0x1002

/* Commands the device appears to understand */
#define DSB100_TUNE 1
#define DSB100_ONOFF 2

#define TB_LEN 16

/* Frequency limits in MHz -- these are European values.  For Japanese
devices, that would be 76 and 91.  */
#define FREQ_MIN  87.5
#define FREQ_MAX 108.0
#define FREQ_MUL 16000


static int usb_dsbr100_probe(struct usb_interface *intf,
			     const struct usb_device_id *id);
static void usb_dsbr100_disconnect(struct usb_interface *intf);
static int usb_dsbr100_open(struct inode *inode, struct file *file);
static int usb_dsbr100_close(struct inode *inode, struct file *file);
#define v4l2_dev_to_radio(d) container_of(d, struct dsbr100_device, v4l2_dev)

static int radio_nr = -1;
module_param(radio_nr, int, 0);

/* Data for one (physical) device */
struct dsbr100_device {
	struct usb_device *usbdev;
	struct video_device *videodev;
	u8 *transfer_buffer;
	int curfreq;
	int stereo;
	int users;
	int removed;
	int muted;
};


static struct usb_device_id usb_dsbr100_device_table [] = {
	{ USB_DEVICE(DSB100_VENDOR, DSB100_PRODUCT) },
	{ }						/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, usb_dsbr100_device_table);

/* USB subsystem interface */
static struct usb_driver usb_dsbr100_driver = {
	.name =		"dsbr100",
	.probe =	usb_dsbr100_probe,
	.disconnect =	usb_dsbr100_disconnect,
	.id_table =	usb_dsbr100_device_table,
	struct video_device videodev;
	struct v4l2_device v4l2_dev;
	struct v4l2_ctrl_handler hdl;

	u8 *transfer_buffer;
	struct mutex v4l2_lock;
	int curfreq;
	bool stereo;
	bool muted;
};

/* Low-level device interface begins here */

/* switch on radio */
static int dsbr100_start(struct dsbr100_device *radio)
{
	if (usb_control_msg(radio->usbdev, usb_rcvctrlpipe(radio->usbdev, 0),
			USB_REQ_GET_STATUS,
			USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
			0x00, 0xC7, radio->transfer_buffer, 8, 300)<0 ||
	usb_control_msg(radio->usbdev, usb_rcvctrlpipe(radio->usbdev, 0),
			DSB100_ONOFF,
			USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
			0x01, 0x00, radio->transfer_buffer, 8, 300)<0)
		return -1;
	radio->muted=0;
	return (radio->transfer_buffer)[0];
}

/* set a frequency, freq is defined by v4l's TUNER_LOW, i.e. 1/16th kHz */
static int dsbr100_setfreq(struct dsbr100_device *radio, unsigned freq)
{
	unsigned f = (freq / 16 * 80) / 1000 + 856;
	int retval = 0;

	if (!radio->muted) {
		retval = usb_control_msg(radio->usbdev,
				usb_rcvctrlpipe(radio->usbdev, 0),
				DSB100_TUNE,
				USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
				(f >> 8) & 0x00ff, f & 0xff,
				radio->transfer_buffer, 8, 300);
		if (retval >= 0)
			mdelay(1);
	}

	if (retval >= 0) {
		radio->curfreq = freq;
		return 0;
	}
	dev_err(&radio->usbdev->dev,
		"%s - usb_control_msg returned %i, request %i\n",
			__func__, retval, DSB100_TUNE);
	return retval;
}

/* switch on radio */
static int dsbr100_start(struct dsbr100_device *radio)
{
	int retval = usb_control_msg(radio->usbdev,
		usb_rcvctrlpipe(radio->usbdev, 0),
		DSB100_ONOFF,
		USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
		0x01, 0x00, radio->transfer_buffer, 8, 300);

	if (retval >= 0)
		return dsbr100_setfreq(radio, radio->curfreq);
	dev_err(&radio->usbdev->dev,
		"%s - usb_control_msg returned %i, request %i\n",
			__func__, retval, DSB100_ONOFF);
	return retval;

}

/* switch off radio */
static int dsbr100_stop(struct dsbr100_device *radio)
{
	if (usb_control_msg(radio->usbdev, usb_rcvctrlpipe(radio->usbdev, 0),
			USB_REQ_GET_STATUS,
			USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
			0x16, 0x1C, radio->transfer_buffer, 8, 300)<0 ||
	usb_control_msg(radio->usbdev, usb_rcvctrlpipe(radio->usbdev, 0),
			DSB100_ONOFF,
			USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
			0x00, 0x00, radio->transfer_buffer, 8, 300)<0)
		return -1;
	radio->muted=1;
	return (radio->transfer_buffer)[0];
}

/* set a frequency, freq is defined by v4l's TUNER_LOW, i.e. 1/16th kHz */
static int dsbr100_setfreq(struct dsbr100_device *radio, int freq)
{
	freq = (freq/16*80)/1000+856;
	if (usb_control_msg(radio->usbdev, usb_rcvctrlpipe(radio->usbdev, 0),
			DSB100_TUNE,
			USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
			(freq>>8)&0x00ff, freq&0xff,
			radio->transfer_buffer, 8, 300)<0 ||
	   usb_control_msg(radio->usbdev, usb_rcvctrlpipe(radio->usbdev, 0),
			USB_REQ_GET_STATUS,
			USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
			0x96, 0xB7, radio->transfer_buffer, 8, 300)<0 ||
	usb_control_msg(radio->usbdev, usb_rcvctrlpipe(radio->usbdev, 0),
			USB_REQ_GET_STATUS,
			USB_TYPE_VENDOR | USB_RECIP_DEVICE |  USB_DIR_IN,
			0x00, 0x24, radio->transfer_buffer, 8, 300)<0) {
		radio->stereo = -1;
		return -1;
	}
	radio->stereo = ! ((radio->transfer_buffer)[0]&0x01);
	return (radio->transfer_buffer)[0];
	int retval = usb_control_msg(radio->usbdev,
		usb_rcvctrlpipe(radio->usbdev, 0),
		DSB100_ONOFF,
		USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
		0x00, 0x00, radio->transfer_buffer, 8, 300);

	if (retval >= 0)
		return 0;
	dev_err(&radio->usbdev->dev,
		"%s - usb_control_msg returned %i, request %i\n",
			__func__, retval, DSB100_ONOFF);
	return retval;

}

/* return the device status.  This is, in effect, just whether it
sees a stereo signal or not.  Pity. */
static void dsbr100_getstat(struct dsbr100_device *radio)
{
	if (usb_control_msg(radio->usbdev, usb_rcvctrlpipe(radio->usbdev, 0),
		USB_REQ_GET_STATUS,
		USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
		0x00 , 0x24, radio->transfer_buffer, 8, 300)<0)
		radio->stereo = -1;
	else
		radio->stereo = ! (radio->transfer_buffer[0]&0x01);
}


/* USB subsystem interface begins here */

/* handle unplugging of the device, release data structures
if nothing keeps us from doing it.  If something is still
keeping us busy, the release callback of v4l will take care
of releasing it. */
static void usb_dsbr100_disconnect(struct usb_interface *intf)
{
	struct dsbr100_device *radio = usb_get_intfdata(intf);

	usb_set_intfdata (intf, NULL);
	if (radio) {
		video_unregister_device(radio->videodev);
		radio->videodev = NULL;
		if (radio->users) {
			kfree(radio->transfer_buffer);
			kfree(radio);
		} else {
			radio->removed = 1;
		}
	}
}


static int vidioc_querycap(struct file *file, void *priv,
					struct v4l2_capability *v)
{
	strlcpy(v->driver, "dsbr100", sizeof(v->driver));
	strlcpy(v->card, "D-Link R-100 USB FM Radio", sizeof(v->card));
	sprintf(v->bus_info, "ISA");
	v->version = RADIO_VERSION;
	v->capabilities = V4L2_CAP_TUNER;
	int retval = usb_control_msg(radio->usbdev,
		usb_rcvctrlpipe(radio->usbdev, 0),
		USB_REQ_GET_STATUS,
		USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
		0x00, 0x24, radio->transfer_buffer, 8, 300);

	if (retval < 0) {
		radio->stereo = false;
		dev_err(&radio->usbdev->dev,
			"%s - usb_control_msg returned %i, request %i\n",
				__func__, retval, USB_REQ_GET_STATUS);
	} else {
		radio->stereo = !(radio->transfer_buffer[0] & 0x01);
	}
}

static int vidioc_querycap(struct file *file, void *priv,
					struct v4l2_capability *v)
{
	struct dsbr100_device *radio = video_drvdata(file);

	strlcpy(v->driver, "dsbr100", sizeof(v->driver));
	strlcpy(v->card, "D-Link R-100 USB FM Radio", sizeof(v->card));
	usb_make_path(radio->usbdev, v->bus_info, sizeof(v->bus_info));
	v->device_caps = V4L2_CAP_RADIO | V4L2_CAP_TUNER;
	v->capabilities = v->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int vidioc_g_tuner(struct file *file, void *priv,
				struct v4l2_tuner *v)
{
	struct dsbr100_device *radio = video_get_drvdata(video_devdata(file));
	struct dsbr100_device *radio = video_drvdata(file);

	if (v->index > 0)
		return -EINVAL;

	dsbr100_getstat(radio);
	strcpy(v->name, "FM");
	v->type = V4L2_TUNER_RADIO;
	v->rangelow = FREQ_MIN*FREQ_MUL;
	v->rangehigh = FREQ_MAX*FREQ_MUL;
	v->rxsubchans = V4L2_TUNER_SUB_MONO|V4L2_TUNER_SUB_STEREO;
	v->capability = V4L2_TUNER_CAP_LOW;
	if(radio->stereo)
		v->audmode = V4L2_TUNER_MODE_STEREO;
	else
		v->audmode = V4L2_TUNER_MODE_MONO;
	v->signal = 0xffff;     /* We can't get the signal strength */
	v->rangelow = FREQ_MIN * FREQ_MUL;
	v->rangehigh = FREQ_MAX * FREQ_MUL;
	v->rxsubchans = radio->stereo ? V4L2_TUNER_SUB_STEREO :
		V4L2_TUNER_SUB_MONO;
	v->capability = V4L2_TUNER_CAP_LOW | V4L2_TUNER_CAP_STEREO;
	v->audmode = V4L2_TUNER_MODE_STEREO;
	v->signal = radio->stereo ? 0xffff : 0;     /* We can't get the signal strength */
	return 0;
}

static int vidioc_s_tuner(struct file *file, void *priv,
				struct v4l2_tuner *v)
{
	if (v->index > 0)
		return -EINVAL;

	return 0;
}

static int vidioc_s_frequency(struct file *file, void *priv,
				struct v4l2_frequency *f)
{
	struct dsbr100_device *radio = video_get_drvdata(video_devdata(file));

	radio->curfreq = f->frequency;
	if (dsbr100_setfreq(radio, radio->curfreq)==-1)
		warn("Set frequency failed");
	return 0;
				const struct v4l2_tuner *v)
{
	return v->index ? -EINVAL : 0;
}

static int vidioc_s_frequency(struct file *file, void *priv,
				const struct v4l2_frequency *f)
{
	struct dsbr100_device *radio = video_drvdata(file);

	if (f->tuner != 0 || f->type != V4L2_TUNER_RADIO)
		return -EINVAL;

	return dsbr100_setfreq(radio, clamp_t(unsigned, f->frequency,
			FREQ_MIN * FREQ_MUL, FREQ_MAX * FREQ_MUL));
}

static int vidioc_g_frequency(struct file *file, void *priv,
				struct v4l2_frequency *f)
{
	struct dsbr100_device *radio = video_get_drvdata(video_devdata(file));

	struct dsbr100_device *radio = video_drvdata(file);

	if (f->tuner)
		return -EINVAL;
	f->type = V4L2_TUNER_RADIO;
	f->frequency = radio->curfreq;
	return 0;
}

static int vidioc_queryctrl(struct file *file, void *priv,
				struct v4l2_queryctrl *qc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(radio_qctrl); i++) {
		if (qc->id && qc->id == radio_qctrl[i].id) {
			memcpy(qc, &(radio_qctrl[i]),
						sizeof(*qc));
			return 0;
		}
	}
	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	struct dsbr100_device *radio = video_get_drvdata(video_devdata(file));

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		ctrl->value = radio->muted;
		return 0;
static int usb_dsbr100_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct dsbr100_device *radio =
		container_of(ctrl->handler, struct dsbr100_device, hdl);

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		radio->muted = ctrl->val;
		return radio->muted ? dsbr100_stop(radio) : dsbr100_start(radio);
	}
	return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	struct dsbr100_device *radio = video_get_drvdata(video_devdata(file));

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		if (ctrl->value) {
			if (dsbr100_stop(radio)==-1)
				warn("Radio did not respond properly");
		} else {
			if (dsbr100_start(radio)==-1)
				warn("Radio did not respond properly");
		}
		return 0;
	}
	return -EINVAL;
}

static int vidioc_g_audio(struct file *file, void *priv,
				struct v4l2_audio *a)
{
	if (a->index > 1)
		return -EINVAL;

	strcpy(a->name, "Radio");
	a->capability = V4L2_AUDCAP_STEREO;
	return 0;
}

static int vidioc_g_input(struct file *filp, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int vidioc_s_input(struct file *filp, void *priv, unsigned int i)
{
	if (i != 0)
		return -EINVAL;
	return 0;
}

static int vidioc_s_audio(struct file *file, void *priv,
					struct v4l2_audio *a)
{
	if (a->index != 0)
		return -EINVAL;
	return 0;
}

static int usb_dsbr100_open(struct inode *inode, struct file *file)
{
	struct dsbr100_device *radio=video_get_drvdata(video_devdata(file));

	radio->users = 1;
	radio->muted = 1;

	if (dsbr100_start(radio)<0) {
		warn("Radio did not start up properly");
		radio->users = 0;
		return -EIO;
	}
	dsbr100_setfreq(radio, radio->curfreq);
	return 0;
}

static int usb_dsbr100_close(struct inode *inode, struct file *file)
{
	struct dsbr100_device *radio=video_get_drvdata(video_devdata(file));

	if (!radio)
		return -ENODEV;
	radio->users = 0;
	if (radio->removed) {
		kfree(radio->transfer_buffer);
		kfree(radio);
	}
	return 0;
}

/* File system interface */
static const struct file_operations usb_dsbr100_fops = {
	.owner		= THIS_MODULE,
	.open		= usb_dsbr100_open,
	.release	= usb_dsbr100_close,
	.ioctl		= video_ioctl2,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= v4l_compat_ioctl32,
#endif
	.llseek		= no_llseek,

/* USB subsystem interface begins here */

/*
 * Handle unplugging of the device.
 * We call video_unregister_device in any case.
 * The last function called in this procedure is
 * usb_dsbr100_video_device_release
 */
static void usb_dsbr100_disconnect(struct usb_interface *intf)
{
	struct dsbr100_device *radio = usb_get_intfdata(intf);

	mutex_lock(&radio->v4l2_lock);
	/*
	 * Disconnect is also called on unload, and in that case we need to
	 * mute the device. This call will silently fail if it is called
	 * after a physical disconnect.
	 */
	usb_control_msg(radio->usbdev,
		usb_rcvctrlpipe(radio->usbdev, 0),
		DSB100_ONOFF,
		USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
		0x00, 0x00, radio->transfer_buffer, 8, 300);
	usb_set_intfdata(intf, NULL);
	video_unregister_device(&radio->videodev);
	v4l2_device_disconnect(&radio->v4l2_dev);
	mutex_unlock(&radio->v4l2_lock);
	v4l2_device_put(&radio->v4l2_dev);
}


/* Suspend device - stop device. */
static int usb_dsbr100_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct dsbr100_device *radio = usb_get_intfdata(intf);

	mutex_lock(&radio->v4l2_lock);
	if (!radio->muted && dsbr100_stop(radio) < 0)
		dev_warn(&intf->dev, "dsbr100_stop failed\n");
	mutex_unlock(&radio->v4l2_lock);

	dev_info(&intf->dev, "going into suspend..\n");
	return 0;
}

/* Resume device - start device. */
static int usb_dsbr100_resume(struct usb_interface *intf)
{
	struct dsbr100_device *radio = usb_get_intfdata(intf);

	mutex_lock(&radio->v4l2_lock);
	if (!radio->muted && dsbr100_start(radio) < 0)
		dev_warn(&intf->dev, "dsbr100_start failed\n");
	mutex_unlock(&radio->v4l2_lock);

	dev_info(&intf->dev, "coming out of suspend..\n");
	return 0;
}

/* free data structures */
static void usb_dsbr100_release(struct v4l2_device *v4l2_dev)
{
	struct dsbr100_device *radio = v4l2_dev_to_radio(v4l2_dev);

	v4l2_ctrl_handler_free(&radio->hdl);
	v4l2_device_unregister(&radio->v4l2_dev);
	kfree(radio->transfer_buffer);
	kfree(radio);
}

static const struct v4l2_ctrl_ops usb_dsbr100_ctrl_ops = {
	.s_ctrl = usb_dsbr100_s_ctrl,
};

/* File system interface */
static const struct v4l2_file_operations usb_dsbr100_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= video_ioctl2,
	.open           = v4l2_fh_open,
	.release        = v4l2_fh_release,
	.poll		= v4l2_ctrl_poll,
};

static const struct v4l2_ioctl_ops usb_dsbr100_ioctl_ops = {
	.vidioc_querycap    = vidioc_querycap,
	.vidioc_g_tuner     = vidioc_g_tuner,
	.vidioc_s_tuner     = vidioc_s_tuner,
	.vidioc_g_frequency = vidioc_g_frequency,
	.vidioc_s_frequency = vidioc_s_frequency,
	.vidioc_queryctrl   = vidioc_queryctrl,
	.vidioc_g_ctrl      = vidioc_g_ctrl,
	.vidioc_s_ctrl      = vidioc_s_ctrl,
	.vidioc_g_audio     = vidioc_g_audio,
	.vidioc_s_audio     = vidioc_s_audio,
	.vidioc_g_input     = vidioc_g_input,
	.vidioc_s_input     = vidioc_s_input,
};

/* V4L2 interface */
static struct video_device dsbr100_videodev_template = {
	.name		= "D-Link DSB-R 100",
	.fops		= &usb_dsbr100_fops,
	.ioctl_ops 	= &usb_dsbr100_ioctl_ops,
	.release	= video_device_release,
};

/* check if the device is present and register with v4l and
usb if it is */
	.vidioc_log_status  = v4l2_ctrl_log_status,
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

/* check if the device is present and register with v4l and usb if it is */
static int usb_dsbr100_probe(struct usb_interface *intf,
				const struct usb_device_id *id)
{
	struct dsbr100_device *radio;

	if (!(radio = kmalloc(sizeof(struct dsbr100_device), GFP_KERNEL)))
		return -ENOMEM;
	if (!(radio->transfer_buffer = kmalloc(TB_LEN, GFP_KERNEL))) {
		kfree(radio);
		return -ENOMEM;
	}
	if (!(radio->videodev = video_device_alloc())) {
		kfree(radio->transfer_buffer);
		kfree(radio);
		return -ENOMEM;
	}
	memcpy(radio->videodev, &dsbr100_videodev_template,
		sizeof(dsbr100_videodev_template));
	radio->removed = 0;
	radio->users = 0;
	radio->usbdev = interface_to_usbdev(intf);
	radio->curfreq = FREQ_MIN*FREQ_MUL;
	video_set_drvdata(radio->videodev, radio);
	if (video_register_device(radio->videodev, VFL_TYPE_RADIO, radio_nr) < 0) {
		warn("Could not register video device");
		video_device_release(radio->videodev);
		kfree(radio->transfer_buffer);
		kfree(radio);
		return -EIO;
	}
	usb_set_intfdata(intf, radio);
	return 0;
}

static int __init dsbr100_init(void)
{
	int retval = usb_register(&usb_dsbr100_driver);
	info(DRIVER_VERSION ":" DRIVER_DESC);
	return retval;
}

static void __exit dsbr100_exit(void)
{
	usb_deregister(&usb_dsbr100_driver);
}

module_init (dsbr100_init);
module_exit (dsbr100_exit);

MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_LICENSE("GPL");
	struct v4l2_device *v4l2_dev;
	int retval;

	radio = kzalloc(sizeof(struct dsbr100_device), GFP_KERNEL);

	if (!radio)
		return -ENOMEM;

	radio->transfer_buffer = kmalloc(TB_LEN, GFP_KERNEL);

	if (!(radio->transfer_buffer)) {
		kfree(radio);
		return -ENOMEM;
	}

	v4l2_dev = &radio->v4l2_dev;
	v4l2_dev->release = usb_dsbr100_release;

	retval = v4l2_device_register(&intf->dev, v4l2_dev);
	if (retval < 0) {
		v4l2_err(v4l2_dev, "couldn't register v4l2_device\n");
		goto err_reg_dev;
	}

	v4l2_ctrl_handler_init(&radio->hdl, 1);
	v4l2_ctrl_new_std(&radio->hdl, &usb_dsbr100_ctrl_ops,
			  V4L2_CID_AUDIO_MUTE, 0, 1, 1, 1);
	if (radio->hdl.error) {
		retval = radio->hdl.error;
		v4l2_err(v4l2_dev, "couldn't register control\n");
		goto err_reg_ctrl;
	}
	mutex_init(&radio->v4l2_lock);
	strlcpy(radio->videodev.name, v4l2_dev->name, sizeof(radio->videodev.name));
	radio->videodev.v4l2_dev = v4l2_dev;
	radio->videodev.fops = &usb_dsbr100_fops;
	radio->videodev.ioctl_ops = &usb_dsbr100_ioctl_ops;
	radio->videodev.release = video_device_release_empty;
	radio->videodev.lock = &radio->v4l2_lock;
	radio->videodev.ctrl_handler = &radio->hdl;

	radio->usbdev = interface_to_usbdev(intf);
	radio->curfreq = FREQ_MIN * FREQ_MUL;
	radio->muted = true;

	video_set_drvdata(&radio->videodev, radio);
	usb_set_intfdata(intf, radio);

	retval = video_register_device(&radio->videodev, VFL_TYPE_RADIO, radio_nr);
	if (retval == 0)
		return 0;
	v4l2_err(v4l2_dev, "couldn't register video device\n");

err_reg_ctrl:
	v4l2_ctrl_handler_free(&radio->hdl);
	v4l2_device_unregister(v4l2_dev);
err_reg_dev:
	kfree(radio->transfer_buffer);
	kfree(radio);
	return retval;
}

static struct usb_device_id usb_dsbr100_device_table[] = {
	{ USB_DEVICE(DSB100_VENDOR, DSB100_PRODUCT) },
	{ }						/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, usb_dsbr100_device_table);

/* USB subsystem interface */
static struct usb_driver usb_dsbr100_driver = {
	.name			= "dsbr100",
	.probe			= usb_dsbr100_probe,
	.disconnect		= usb_dsbr100_disconnect,
	.id_table		= usb_dsbr100_device_table,
	.suspend		= usb_dsbr100_suspend,
	.resume			= usb_dsbr100_resume,
	.reset_resume		= usb_dsbr100_resume,
};

module_usb_driver(usb_dsbr100_driver);
