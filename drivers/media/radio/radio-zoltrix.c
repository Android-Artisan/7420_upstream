/* zoltrix radio plus driver for Linux radio support
 * (c) 1998 C. van Schaik <carl@leg.uct.ac.za>
/*
 * Zoltrix Radio Plus driver
 * Copyright 1998 C. van Schaik <carl@leg.uct.ac.za>
 *
 * BUGS
 *  Due to the inconsistency in reading from the signal flags
 *  it is difficult to get an accurate tuned signal.
 *
 *  It seems that the card is not linear to 0 volume. It cuts off
 *  at a low volume, and it is not possible (at least I have not found)
 *  to get fine volume control over the low volume range.
 *
 *  Some code derived from code by Romolo Manfredini
 *				   romolo@bicnet.it
 *
 * 1999-05-06 - (C. van Schaik)
 *	      - Make signal strength and stereo scans
 *		kinder to cpu while in delay
 * 1999-01-05 - (C. van Schaik)
 *	      - Changed tuning to 1/160Mhz accuracy
 *	      - Added stereo support
 *		(card defaults to stereo)
 *		(can explicitly force mono on the card)
 *		(can detect if station is in stereo)
 *	      - Added unmute function
 *	      - Reworked ioctl functions
 * 2002-07-15 - Fix Stereo typo
 *
 * 2006-07-24 - Converted to V4L2 API
 *		by Mauro Carvalho Chehab <mchehab@infradead.org>
 *
 * Converted to the radio-isa framework by Hans Verkuil <hans.verkuil@cisco.com>
 *
 * Note that this is the driver for the Zoltrix Radio Plus.
 * This driver does not work for the Zoltrix Radio Plus 108 or the
 * Zoltrix Radio Plus for Windows.
 *
 * Fully tested with the Keene USB FM Transmitter and the v4l2-compliance tool.
 */

#include <linux/module.h>	/* Modules                        */
#include <linux/init.h>		/* Initdata                       */
#include <linux/ioport.h>	/* request_region		  */
#include <linux/delay.h>	/* udelay, msleep                 */
#include <asm/io.h>		/* outb, outb_p                   */
#include <asm/uaccess.h>	/* copy to/from user              */
#include <linux/videodev2.h>	/* kernel radio structs           */
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>

#include <linux/version.h>      /* for KERNEL_VERSION MACRO     */
#define RADIO_VERSION KERNEL_VERSION(0,0,2)

static struct v4l2_queryctrl radio_qctrl[] = {
	{
		.id            = V4L2_CID_AUDIO_MUTE,
		.name          = "Mute",
		.minimum       = 0,
		.maximum       = 1,
		.default_value = 1,
		.type          = V4L2_CTRL_TYPE_BOOLEAN,
	},{
		.id            = V4L2_CID_AUDIO_VOLUME,
		.name          = "Volume",
		.minimum       = 0,
		.maximum       = 65535,
		.step          = 4096,
		.default_value = 0xff,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	}
};
#include <linux/videodev2.h>	/* kernel radio structs           */
#include <linux/mutex.h>
#include <linux/io.h>		/* outb, outb_p                   */
#include <linux/slab.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include "radio-isa.h"

MODULE_AUTHOR("C. van Schaik");
MODULE_DESCRIPTION("A driver for the Zoltrix Radio Plus.");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1.99");

#ifndef CONFIG_RADIO_ZOLTRIX_PORT
#define CONFIG_RADIO_ZOLTRIX_PORT -1
#endif

static int io = CONFIG_RADIO_ZOLTRIX_PORT;
static int radio_nr = -1;

struct zol_device {
	int port;
	int curvol;
	unsigned long curfreq;
	int muted;
	unsigned int stereo;
	struct mutex lock;
};

static int zol_setvol(struct zol_device *dev, int vol)
{
	dev->curvol = vol;
	if (dev->muted)
		return 0;

	mutex_lock(&dev->lock);
	if (vol == 0) {
		outb(0, io);
		outb(0, io);
		inb(io + 3);    /* Zoltrix needs to be read to confirm */
		mutex_unlock(&dev->lock);
		return 0;
	}

	outb(dev->curvol-1, io);
	msleep(10);
	inb(io + 2);
	mutex_unlock(&dev->lock);
	return 0;
}

static void zol_mute(struct zol_device *dev)
{
	dev->muted = 1;
	mutex_lock(&dev->lock);
	outb(0, io);
	outb(0, io);
	inb(io + 3);            /* Zoltrix needs to be read to confirm */
	mutex_unlock(&dev->lock);
}

static void zol_unmute(struct zol_device *dev)
{
	dev->muted = 0;
	zol_setvol(dev, dev->curvol);
}

static int zol_setfreq(struct zol_device *dev, unsigned long freq)
{
	/* tunes the radio to the desired frequency */
	unsigned long long bitmask, f, m;
	unsigned int stereo = dev->stereo;
	int i;

	if (freq == 0)
		return 1;
	m = (freq / 160 - 8800) * 2;
	f = (unsigned long long) m + 0x4d1c;
#define ZOLTRIX_MAX 2

static int io[ZOLTRIX_MAX] = { [0] = CONFIG_RADIO_ZOLTRIX_PORT,
			       [1 ... (ZOLTRIX_MAX - 1)] = -1 };
static int radio_nr[ZOLTRIX_MAX] = { [0 ... (ZOLTRIX_MAX - 1)] = -1 };

module_param_array(io, int, NULL, 0444);
MODULE_PARM_DESC(io, "I/O addresses of the Zoltrix Radio Plus card (0x20c or 0x30c)");
module_param_array(radio_nr, int, NULL, 0444);
MODULE_PARM_DESC(radio_nr, "Radio device numbers");

struct zoltrix {
	struct radio_isa_card isa;
	int curvol;
	bool muted;
};

static struct radio_isa_card *zoltrix_alloc(void)
{
	struct zoltrix *zol = kzalloc(sizeof(*zol), GFP_KERNEL);

	return zol ? &zol->isa : NULL;
}

static int zoltrix_s_mute_volume(struct radio_isa_card *isa, bool mute, int vol)
{
	struct zoltrix *zol = container_of(isa, struct zoltrix, isa);

	zol->curvol = vol;
	zol->muted = mute;
	if (mute || vol == 0) {
		outb(0, isa->io);
		outb(0, isa->io);
		inb(isa->io + 3);            /* Zoltrix needs to be read to confirm */
		return 0;
	}

	outb(vol - 1, isa->io);
	msleep(10);
	inb(isa->io + 2);
	return 0;
}

/* tunes the radio to the desired frequency */
static int zoltrix_s_frequency(struct radio_isa_card *isa, u32 freq)
{
	struct zoltrix *zol = container_of(isa, struct zoltrix, isa);
	struct v4l2_device *v4l2_dev = &isa->v4l2_dev;
	unsigned long long bitmask, f, m;
	bool stereo = isa->stereo;
	int i;

	if (freq == 0) {
		v4l2_warn(v4l2_dev, "cannot set a frequency of 0.\n");
		return -EINVAL;
	}

	m = (freq / 160 - 8800) * 2;
	f = (unsigned long long)m + 0x4d1c;

	bitmask = 0xc480402c10080000ull;
	i = 45;

	mutex_lock(&dev->lock);

	outb(0, io);
	outb(0, io);
	inb(io + 3);            /* Zoltrix needs to be read to confirm */

	outb(0x40, io);
	outb(0xc0, io);

	bitmask = (bitmask ^ ((f & 0xff) << 47) ^ ((f & 0xff00) << 30) ^ ( stereo << 31));
	while (i--) {
		if ((bitmask & 0x8000000000000000ull) != 0) {
			outb(0x80, io);
			udelay(50);
			outb(0x00, io);
			udelay(50);
			outb(0x80, io);
			udelay(50);
		} else {
			outb(0xc0, io);
			udelay(50);
			outb(0x40, io);
			udelay(50);
			outb(0xc0, io);
	outb(0, isa->io);
	outb(0, isa->io);
	inb(isa->io + 3);            /* Zoltrix needs to be read to confirm */

	outb(0x40, isa->io);
	outb(0xc0, isa->io);

	bitmask = (bitmask ^ ((f & 0xff) << 47) ^ ((f & 0xff00) << 30) ^ (stereo << 31));
	while (i--) {
		if ((bitmask & 0x8000000000000000ull) != 0) {
			outb(0x80, isa->io);
			udelay(50);
			outb(0x00, isa->io);
			udelay(50);
			outb(0x80, isa->io);
			udelay(50);
		} else {
			outb(0xc0, isa->io);
			udelay(50);
			outb(0x40, isa->io);
			udelay(50);
			outb(0xc0, isa->io);
			udelay(50);
		}
		bitmask *= 2;
	}
	/* termination sequence */
	outb(0x80, io);
	outb(0xc0, io);
	outb(0x40, io);
	udelay(1000);
	inb(io+2);

	udelay(1000);

	if (dev->muted)
	{
		outb(0, io);
		outb(0, io);
		inb(io + 3);
		udelay(1000);
	}

	mutex_unlock(&dev->lock);

	if(!dev->muted)
	{
		zol_setvol(dev, dev->curvol);
	}
	return 0;
}

/* Get signal strength */

static int zol_getsigstr(struct zol_device *dev)
{
	int a, b;

	mutex_lock(&dev->lock);
	outb(0x00, io);         /* This stuff I found to do nothing */
	outb(dev->curvol, io);
	msleep(20);

	a = inb(io);
	msleep(10);
	b = inb(io);

	mutex_unlock(&dev->lock);

	if (a != b)
		return (0);

	if ((a == 0xcf) || (a == 0xdf)  /* I found this out by playing */
		|| (a == 0xef))       /* with a binary scanner on the card io */
		return (1);
	return (0);
}

static int zol_is_stereo (struct zol_device *dev)
{
	int x1, x2;

	mutex_lock(&dev->lock);

	outb(0x00, io);
	outb(dev->curvol, io);
	msleep(20);

	x1 = inb(io);
	msleep(10);
	x2 = inb(io);

	mutex_unlock(&dev->lock);

	if ((x1 == x2) && (x1 == 0xcf))
		return 1;
	return 0;
}

static int vidioc_querycap(struct file *file, void  *priv,
					struct v4l2_capability *v)
{
	strlcpy(v->driver, "radio-zoltrix", sizeof(v->driver));
	strlcpy(v->card, "Zoltrix Radio", sizeof(v->card));
	sprintf(v->bus_info, "ISA");
	v->version = RADIO_VERSION;
	v->capabilities = V4L2_CAP_TUNER;
	return 0;
}

static int vidioc_g_tuner(struct file *file, void *priv,
					struct v4l2_tuner *v)
{
	struct video_device *dev = video_devdata(file);
	struct zol_device *zol = dev->priv;

	if (v->index > 0)
		return -EINVAL;

	strcpy(v->name, "FM");
	v->type = V4L2_TUNER_RADIO;
	v->rangelow = (88*16000);
	v->rangehigh = (108*16000);
	v->rxsubchans = V4L2_TUNER_SUB_MONO|V4L2_TUNER_SUB_STEREO;
	v->capability = V4L2_TUNER_CAP_LOW;
	if (zol_is_stereo(zol))
		v->audmode = V4L2_TUNER_MODE_STEREO;
	else
		v->audmode = V4L2_TUNER_MODE_MONO;
	v->signal = 0xFFFF*zol_getsigstr(zol);
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
	struct video_device *dev = video_devdata(file);
	struct zol_device *zol = dev->priv;

	zol->curfreq = f->frequency;
	zol_setfreq(zol, zol->curfreq);
	return 0;
}

static int vidioc_g_frequency(struct file *file, void *priv,
					struct v4l2_frequency *f)
{
	struct video_device *dev = video_devdata(file);
	struct zol_device *zol = dev->priv;

	f->type = V4L2_TUNER_RADIO;
	f->frequency = zol->curfreq;
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
	struct video_device *dev = video_devdata(file);
	struct zol_device *zol = dev->priv;

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		ctrl->value = zol->muted;
		return 0;
	case V4L2_CID_AUDIO_VOLUME:
		ctrl->value = zol->curvol * 4096;
		return 0;
	}
	return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	struct video_device *dev = video_devdata(file);
	struct zol_device *zol = dev->priv;

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		if (ctrl->value)
			zol_mute(zol);
		else {
			zol_unmute(zol);
			zol_setvol(zol,zol->curvol);
		}
		return 0;
	case V4L2_CID_AUDIO_VOLUME:
		zol_setvol(zol,ctrl->value/4096);
		return 0;
	}
	zol->stereo = 1;
	zol_setfreq(zol, zol->curfreq);
#if 0
/* FIXME: Implement stereo/mono switch on V4L2 */
			if (v->mode & VIDEO_SOUND_STEREO) {
				zol->stereo = 1;
				zol_setfreq(zol, zol->curfreq);
			}
			if (v->mode & VIDEO_SOUND_MONO) {
				zol->stereo = 0;
				zol_setfreq(zol, zol->curfreq);
			}
#endif
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

static struct zol_device zoltrix_unit;

static const struct file_operations zoltrix_fops =
{
	.owner		= THIS_MODULE,
	.open           = video_exclusive_open,
	.release        = video_exclusive_release,
	.ioctl		= video_ioctl2,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= v4l_compat_ioctl32,
#endif
	.llseek         = no_llseek,
};

static const struct v4l2_ioctl_ops zoltrix_ioctl_ops = {
	.vidioc_querycap    = vidioc_querycap,
	.vidioc_g_tuner     = vidioc_g_tuner,
	.vidioc_s_tuner     = vidioc_s_tuner,
	.vidioc_g_audio     = vidioc_g_audio,
	.vidioc_s_audio     = vidioc_s_audio,
	.vidioc_g_input     = vidioc_g_input,
	.vidioc_s_input     = vidioc_s_input,
	.vidioc_g_frequency = vidioc_g_frequency,
	.vidioc_s_frequency = vidioc_s_frequency,
	.vidioc_queryctrl   = vidioc_queryctrl,
	.vidioc_g_ctrl      = vidioc_g_ctrl,
	.vidioc_s_ctrl      = vidioc_s_ctrl,
};

static struct video_device zoltrix_radio = {
	.name		= "Zoltrix Radio Plus",
	.fops           = &zoltrix_fops,
	.ioctl_ops 	= &zoltrix_ioctl_ops,
	outb(0x80, isa->io);
	outb(0xc0, isa->io);
	outb(0x40, isa->io);
	udelay(1000);
	inb(isa->io + 2);
	udelay(1000);

	return zoltrix_s_mute_volume(isa, zol->muted, zol->curvol);
}

/* Get signal strength */
static u32 zoltrix_g_rxsubchans(struct radio_isa_card *isa)
{
	struct zoltrix *zol = container_of(isa, struct zoltrix, isa);
	int a, b;

	outb(0x00, isa->io);         /* This stuff I found to do nothing */
	outb(zol->curvol, isa->io);
	msleep(20);

	a = inb(isa->io);
	msleep(10);
	b = inb(isa->io);

	return (a == b && a == 0xcf) ?
		V4L2_TUNER_SUB_STEREO : V4L2_TUNER_SUB_MONO;
}

static u32 zoltrix_g_signal(struct radio_isa_card *isa)
{
	struct zoltrix *zol = container_of(isa, struct zoltrix, isa);
	int a, b;

	outb(0x00, isa->io);         /* This stuff I found to do nothing */
	outb(zol->curvol, isa->io);
	msleep(20);

	a = inb(isa->io);
	msleep(10);
	b = inb(isa->io);

	if (a != b)
		return 0;

	/* I found this out by playing with a binary scanner on the card io */
	return (a == 0xcf || a == 0xdf || a == 0xef) ? 0xffff : 0;
}

static int zoltrix_s_stereo(struct radio_isa_card *isa, bool stereo)
{
	return zoltrix_s_frequency(isa, isa->freq);
}

static const struct radio_isa_ops zoltrix_ops = {
	.alloc = zoltrix_alloc,
	.s_mute_volume = zoltrix_s_mute_volume,
	.s_frequency = zoltrix_s_frequency,
	.s_stereo = zoltrix_s_stereo,
	.g_rxsubchans = zoltrix_g_rxsubchans,
	.g_signal = zoltrix_g_signal,
};

static const int zoltrix_ioports[] = { 0x20c, 0x30c };

static struct radio_isa_driver zoltrix_driver = {
	.driver = {
		.match		= radio_isa_match,
		.probe		= radio_isa_probe,
		.remove		= radio_isa_remove,
		.driver		= {
			.name	= "radio-zoltrix",
		},
	},
	.io_params = io,
	.radio_nr_params = radio_nr,
	.io_ports = zoltrix_ioports,
	.num_of_io_ports = ARRAY_SIZE(zoltrix_ioports),
	.region_size = 2,
	.card = "Zoltrix Radio Plus",
	.ops = &zoltrix_ops,
	.has_stereo = true,
	.max_volume = 15,
};

static int __init zoltrix_init(void)
{
	if (io == -1) {
		printk(KERN_ERR "You must set an I/O address with io=0x???\n");
		return -EINVAL;
	}
	if ((io != 0x20c) && (io != 0x30c)) {
		printk(KERN_ERR "zoltrix: invalid port, try 0x20c or 0x30c\n");
		return -ENXIO;
	}

	zoltrix_radio.priv = &zoltrix_unit;
	if (!request_region(io, 2, "zoltrix")) {
		printk(KERN_ERR "zoltrix: port 0x%x already in use\n", io);
		return -EBUSY;
	}

	if (video_register_device(&zoltrix_radio, VFL_TYPE_RADIO, radio_nr) < 0) {
		release_region(io, 2);
		return -EINVAL;
	}
	printk(KERN_INFO "Zoltrix Radio Plus card driver.\n");

	mutex_init(&zoltrix_unit.lock);

	/* mute card - prevents noisy bootups */

	/* this ensures that the volume is all the way down  */

	outb(0, io);
	outb(0, io);
	msleep(20);
	inb(io + 3);

	zoltrix_unit.curvol = 0;
	zoltrix_unit.stereo = 1;

	return 0;
}

MODULE_AUTHOR("C.van Schaik");
MODULE_DESCRIPTION("A driver for the Zoltrix Radio Plus.");
MODULE_LICENSE("GPL");

module_param(io, int, 0);
MODULE_PARM_DESC(io, "I/O address of the Zoltrix Radio Plus (0x20c or 0x30c)");
module_param(radio_nr, int, 0);

static void __exit zoltrix_cleanup_module(void)
{
	video_unregister_device(&zoltrix_radio);
	release_region(io, 2);
}

module_init(zoltrix_init);
module_exit(zoltrix_cleanup_module);
	return isa_register_driver(&zoltrix_driver.driver, ZOLTRIX_MAX);
}

static void __exit zoltrix_exit(void)
{
	isa_unregister_driver(&zoltrix_driver.driver);
}

module_init(zoltrix_init);
module_exit(zoltrix_exit);

