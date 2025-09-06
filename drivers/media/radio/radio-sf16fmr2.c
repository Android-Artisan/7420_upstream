/* SF16FMR2 radio driver for Linux radio support
 * heavily based on fmi driver...
 * (c) 2000-2002 Ziglio Frediano, freddy77@angelfire.com
 *
 * Notes on the hardware
 *
 *  Frequency control is done digitally -- ie out(port,encodefreq(95.8));
 *  No volume control - only mute/unmute - you have to use line volume
 *
 *  For read stereo/mono you must wait 0.1 sec after set frequency and
 *  card unmuted so I set frequency on unmute
 *  Signal handling seem to work only on autoscanning (not implemented)
 *
 *  Converted to V4L2 API by Mauro Carvalho Chehab <mchehab@infradead.org>
 */

#include <linux/module.h>	/* Modules 			*/
#include <linux/init.h>		/* Initdata			*/
#include <linux/ioport.h>	/* request_region		*/
#include <linux/delay.h>	/* udelay			*/
#include <asm/io.h>		/* outb, outb_p			*/
#include <asm/uaccess.h>	/* copy to/from user		*/
#include <linux/videodev2.h>	/* kernel radio structs		*/
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <linux/mutex.h>

static struct mutex lock;

#include <linux/version.h>      /* for KERNEL_VERSION MACRO     */
#define RADIO_VERSION KERNEL_VERSION(0,0,2)

#define AUD_VOL_INDEX 1

static struct v4l2_queryctrl radio_qctrl[] = {
	{
		.id            = V4L2_CID_AUDIO_MUTE,
		.name          = "Mute",
		.minimum       = 0,
		.maximum       = 1,
		.default_value = 1,
		.type          = V4L2_CTRL_TYPE_BOOLEAN,
	},
	[AUD_VOL_INDEX] = {
		.id            = V4L2_CID_AUDIO_VOLUME,
		.name          = "Volume",
		.minimum       = 0,
		.maximum       = 15,
		.step          = 1,
		.default_value = 0,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	}
};

#undef DEBUG
//#define DEBUG 1

#ifdef DEBUG
# define  debug_print(s) printk s
#else
# define  debug_print(s)
#endif

/* this should be static vars for module size */
struct fmr2_device
{
	int port;
	int curvol; /* 0-15 */
	int mute;
	int stereo; /* card is producing stereo audio */
	unsigned long curfreq; /* freq in kHz */
	int card_type;
	__u32 flags;
};

static int io = 0x384;
static int radio_nr = -1;

/* hw precision is 12.5 kHz
 * It is only useful to give freq in intervall of 200 (=0.0125Mhz),
 * other bits will be truncated
 */
#define RSF16_ENCODE(x)	((x)/200+856)
#define RSF16_MINFREQ 87*16000
#define RSF16_MAXFREQ 108*16000

static inline void wait(int n,int port)
{
	for (;n;--n) inb(port);
}

static void outbits(int bits, unsigned int data, int nWait, int port)
{
	int bit;
	for(;--bits>=0;) {
		bit = (data>>bits) & 1;
		outb(bit,port);
		wait(nWait,port);
		outb(bit|2,port);
		wait(nWait,port);
		outb(bit,port);
		wait(nWait,port);
	}
}

static inline void fmr2_mute(int port)
{
	outb(0x00, port);
	wait(4,port);
}

static inline void fmr2_unmute(int port)
{
	outb(0x04, port);
	wait(4,port);
}

static inline int fmr2_stereo_mode(int port)
{
	int n = inb(port);
	outb(6,port);
	inb(port);
	n = ((n>>3)&1)^1;
	debug_print((KERN_DEBUG "stereo: %d\n", n));
	return n;
}

static int fmr2_product_info(struct fmr2_device *dev)
{
	int n = inb(dev->port);
	n &= 0xC1;
	if (n == 0)
	{
		/* this should support volume set */
		dev->card_type = 12;
		return 0;
	}
	/* not volume (mine is 11) */
	dev->card_type = (n==128)?11:0;
	return n;
}

static inline int fmr2_getsigstr(struct fmr2_device *dev)
{
	/* !!! work only if scanning freq */
	int port = dev->port, res = 0xffff;
	outb(5,port);
	wait(4,port);
	if (!(inb(port)&1)) res = 0;
	debug_print((KERN_DEBUG "signal: %d\n", res));
	return res;
}

/* set frequency and unmute card */
static int fmr2_setfreq(struct fmr2_device *dev)
{
	int port = dev->port;
	unsigned long freq = dev->curfreq;

	fmr2_mute(port);

	/* 0x42 for mono output
	 * 0x102 forward scanning
	 * 0x182 scansione avanti
	 */
	outbits(9,0x2,3,port);
	outbits(16,RSF16_ENCODE(freq),2,port);

	fmr2_unmute(port);

	/* wait 0.11 sec */
	msleep(110);

	/* NOTE if mute this stop radio
	   you must set freq on unmute */
	dev->stereo = fmr2_stereo_mode(port);
	return 0;
}

/* !!! not tested, in my card this does't work !!! */
static int fmr2_setvolume(struct fmr2_device *dev)
{
	int vol[16] = { 0x021, 0x084, 0x090, 0x104,
			0x110, 0x204, 0x210, 0x402,
			0x404, 0x408, 0x410, 0x801,
			0x802, 0x804, 0x808, 0x810 };
	int i, a, port = dev->port;
	int n = vol[dev->curvol & 0x0f];

	if (dev->card_type != 11)
		return 1;

	for (i = 12; --i >= 0; ) {
		a = ((n >> i) & 1) << 6; /* if (a=0) a= 0; else a= 0x40; */
		outb(a | 4, port);
		wait(4, port);
		outb(a | 0x24, port);
		wait(4, port);
		outb(a | 4, port);
		wait(4, port);
	}
	for (i = 6; --i >= 0; ) {
		a = ((0x18 >> i) & 1) << 6;
		outb(a | 4, port);
		wait(4,port);
		outb(a | 0x24, port);
		wait(4,port);
		outb(a|4, port);
		wait(4,port);
	}
	wait(4, port);
	outb(0x14, port);

	return 0;
}

static int vidioc_querycap(struct file *file, void  *priv,
					struct v4l2_capability *v)
{
	strlcpy(v->driver, "radio-sf16fmr2", sizeof(v->driver));
	strlcpy(v->card, "SF16-FMR2 radio", sizeof(v->card));
	sprintf(v->bus_info, "ISA");
	v->version = RADIO_VERSION;
	v->capabilities = V4L2_CAP_TUNER;
	return 0;
}

static int vidioc_g_tuner(struct file *file, void *priv,
					struct v4l2_tuner *v)
{
	int mult;
	struct video_device *dev = video_devdata(file);
	struct fmr2_device *fmr2 = dev->priv;

	if (v->index > 0)
		return -EINVAL;

	strcpy(v->name, "FM");
	v->type = V4L2_TUNER_RADIO;

	mult = (fmr2->flags & V4L2_TUNER_CAP_LOW) ? 1 : 1000;
	v->rangelow = RSF16_MINFREQ/mult;
	v->rangehigh = RSF16_MAXFREQ/mult;
	v->rxsubchans = V4L2_TUNER_SUB_MONO | V4L2_TUNER_MODE_STEREO;
	v->capability = fmr2->flags&V4L2_TUNER_CAP_LOW;
	v->audmode = fmr2->stereo ? V4L2_TUNER_MODE_STEREO:
				V4L2_TUNER_MODE_MONO;
	mutex_lock(&lock);
	v->signal = fmr2_getsigstr(fmr2);
	mutex_unlock(&lock);
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
	struct fmr2_device *fmr2 = dev->priv;

	if (!(fmr2->flags & V4L2_TUNER_CAP_LOW))
		f->frequency *= 1000;
	if (f->frequency < RSF16_MINFREQ ||
			f->frequency > RSF16_MAXFREQ )
		return -EINVAL;
	/*rounding in steps of 200 to match th freq
	that will be used */
	fmr2->curfreq = (f->frequency/200)*200;

	/* set card freq (if not muted) */
	if (fmr2->curvol && !fmr2->mute) {
		mutex_lock(&lock);
		fmr2_setfreq(fmr2);
		mutex_unlock(&lock);
	}
	return 0;
}

static int vidioc_g_frequency(struct file *file, void *priv,
					struct v4l2_frequency *f)
{
	struct video_device *dev = video_devdata(file);
	struct fmr2_device *fmr2 = dev->priv;

	f->type = V4L2_TUNER_RADIO;
	f->frequency = fmr2->curfreq;
	if (!(fmr2->flags & V4L2_TUNER_CAP_LOW))
		f->frequency /= 1000;
	return 0;
}

static int vidioc_queryctrl(struct file *file, void *priv,
					struct v4l2_queryctrl *qc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(radio_qctrl); i++) {
		if (qc->id && qc->id == radio_qctrl[i].id) {
			memcpy(qc, &radio_qctrl[i], sizeof(*qc));
			return 0;
		}
	}
	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
					struct v4l2_control *ctrl)
{
	struct video_device *dev = video_devdata(file);
	struct fmr2_device *fmr2 = dev->priv;

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		ctrl->value = fmr2->mute;
		return 0;
	case V4L2_CID_AUDIO_VOLUME:
		ctrl->value = fmr2->curvol;
		return 0;
	}
	return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
					struct v4l2_control *ctrl)
{
	struct video_device *dev = video_devdata(file);
	struct fmr2_device *fmr2 = dev->priv;

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		fmr2->mute = ctrl->value;
		break;
	case V4L2_CID_AUDIO_VOLUME:
		if (ctrl->value > radio_qctrl[AUD_VOL_INDEX].maximum)
			fmr2->curvol = radio_qctrl[AUD_VOL_INDEX].maximum;
		else
			fmr2->curvol = ctrl->value;

/* SF16-FMR2 and SF16-FMD2 radio driver for Linux
 * Copyright (c) 2011 Ondrej Zary
 *
 * Original driver was (c) 2000-2002 Ziglio Frediano, freddy77@angelfire.com
 * but almost nothing remained here after conversion to generic TEA575x
 * implementation
 */

#include <linux/delay.h>
#include <linux/module.h>	/* Modules 			*/
#include <linux/init.h>		/* Initdata			*/
#include <linux/slab.h>
#include <linux/ioport.h>	/* request_region		*/
#include <linux/io.h>		/* outb, outb_p			*/
#include <linux/isa.h>
#include <linux/pnp.h>
#include <media/tea575x.h>

MODULE_AUTHOR("Ondrej Zary");
MODULE_DESCRIPTION("MediaForte SF16-FMR2 and SF16-FMD2 FM radio card driver");
MODULE_LICENSE("GPL");

/* these cards can only use two different ports (0x384 and 0x284) */
#define FMR2_MAX 2

static int radio_nr[FMR2_MAX] = { [0 ... (FMR2_MAX - 1)] = -1 };
module_param_array(radio_nr, int, NULL, 0444);
MODULE_PARM_DESC(radio_nr, "Radio device numbers");

struct fmr2 {
	int io;
	struct v4l2_device v4l2_dev;
	struct snd_tea575x tea;
	struct v4l2_ctrl *volume;
	struct v4l2_ctrl *balance;
	bool is_fmd2;
};

static int num_fmr2_cards;
static struct fmr2 *fmr2_cards[FMR2_MAX];
static bool isa_registered;
static bool pnp_registered;

/* the port is hardwired on SF16-FMR2 */
#define FMR2_PORT	0x384

/* TEA575x tuner pins */
#define STR_DATA	(1 << 0)
#define STR_CLK		(1 << 1)
#define STR_WREN	(1 << 2)
#define STR_MOST	(1 << 3)
/* PT2254A/TC9154A volume control pins */
#define PT_ST		(1 << 4)
#define PT_CK		(1 << 5)
#define PT_DATA		(1 << 6)
/* volume control presence pin */
#define FMR2_HASVOL	(1 << 7)

static void fmr2_tea575x_set_pins(struct snd_tea575x *tea, u8 pins)
{
	struct fmr2 *fmr2 = tea->private_data;
	u8 bits = 0;

	bits |= (pins & TEA575X_DATA) ? STR_DATA : 0;
	bits |= (pins & TEA575X_CLK)  ? STR_CLK  : 0;
	/* WRITE_ENABLE is inverted, DATA must be high during read */
	bits |= (pins & TEA575X_WREN) ? 0 : STR_WREN | STR_DATA;

	outb(bits, fmr2->io);
}

static u8 fmr2_tea575x_get_pins(struct snd_tea575x *tea)
{
	struct fmr2 *fmr2 = tea->private_data;
	u8 bits = inb(fmr2->io);

	return  ((bits & STR_DATA) ? TEA575X_DATA : 0) |
		((bits & STR_MOST) ? TEA575X_MOST : 0);
}

static void fmr2_tea575x_set_direction(struct snd_tea575x *tea, bool output)
{
}

static struct snd_tea575x_ops fmr2_tea_ops = {
	.set_pins = fmr2_tea575x_set_pins,
	.get_pins = fmr2_tea575x_get_pins,
	.set_direction = fmr2_tea575x_set_direction,
};

/* TC9154A/PT2254A volume control */

/* 18-bit shift register bit definitions */
#define TC9154A_ATT_MAJ_0DB	(1 << 0)
#define TC9154A_ATT_MAJ_10DB	(1 << 1)
#define TC9154A_ATT_MAJ_20DB	(1 << 2)
#define TC9154A_ATT_MAJ_30DB	(1 << 3)
#define TC9154A_ATT_MAJ_40DB	(1 << 4)
#define TC9154A_ATT_MAJ_50DB	(1 << 5)
#define TC9154A_ATT_MAJ_60DB	(1 << 6)

#define TC9154A_ATT_MIN_0DB	(1 << 7)
#define TC9154A_ATT_MIN_2DB	(1 << 8)
#define TC9154A_ATT_MIN_4DB	(1 << 9)
#define TC9154A_ATT_MIN_6DB	(1 << 10)
#define TC9154A_ATT_MIN_8DB	(1 << 11)
/* bit 12 is ignored */
#define TC9154A_CHANNEL_LEFT	(1 << 13)
#define TC9154A_CHANNEL_RIGHT	(1 << 14)
/* bits 15, 16, 17 must be 0 */

#define	TC9154A_ATT_MAJ(x)	(1 << x)
#define TC9154A_ATT_MIN(x)	(1 << (7 + x))

static void tc9154a_set_pins(struct fmr2 *fmr2, u8 pins)
{
	if (!fmr2->tea.mute)
		pins |= STR_WREN;

	outb(pins, fmr2->io);
}

static void tc9154a_set_attenuation(struct fmr2 *fmr2, int att, u32 channel)
{
	int i;
	u32 reg;
	u8 bit;

	reg = TC9154A_ATT_MAJ(att / 10) | TC9154A_ATT_MIN((att % 10) / 2);
	reg |= channel;
	/* write 18-bit shift register, LSB first */
	for (i = 0; i < 18; i++) {
		bit = reg & (1 << i) ? PT_DATA : 0;
		tc9154a_set_pins(fmr2, bit);
		udelay(5);
		tc9154a_set_pins(fmr2, bit | PT_CK);
		udelay(5);
		tc9154a_set_pins(fmr2, bit);
	}

	/* latch register data */
	udelay(5);
	tc9154a_set_pins(fmr2, PT_ST);
	udelay(5);
	tc9154a_set_pins(fmr2, 0);
}

static int fmr2_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct snd_tea575x *tea = container_of(ctrl->handler, struct snd_tea575x, ctrl_handler);
	struct fmr2 *fmr2 = tea->private_data;
	int volume, balance, left, right;

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_VOLUME:
		volume = ctrl->val;
		balance = fmr2->balance->cur.val;
		break;
	case V4L2_CID_AUDIO_BALANCE:
		balance = ctrl->val;
		volume = fmr2->volume->cur.val;
		break;
	default:
		return -EINVAL;
	}

#ifdef DEBUG
	if (fmr2->curvol && !fmr2->mute)
		printk(KERN_DEBUG "unmute\n");
	else
		printk(KERN_DEBUG "mute\n");
#endif

	mutex_lock(&lock);
	if (fmr2->curvol && !fmr2->mute) {
		fmr2_setvolume(fmr2);
		/* Set frequency and unmute card */
		fmr2_setfreq(fmr2);
	} else
		fmr2_mute(fmr2->port);
	mutex_unlock(&lock);
	return 0;
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

static struct fmr2_device fmr2_unit;

static const struct file_operations fmr2_fops = {
	.owner          = THIS_MODULE,
	.open           = video_exclusive_open,
	.release        = video_exclusive_release,
	.ioctl          = video_ioctl2,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= v4l_compat_ioctl32,
#endif
	.llseek         = no_llseek,
};

static const struct v4l2_ioctl_ops fmr2_ioctl_ops = {
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

static struct video_device fmr2_radio = {
	.name		= "SF16FMR2 radio",
	.fops		= &fmr2_fops,
	.ioctl_ops 	= &fmr2_ioctl_ops,
	left = right = volume;
	if (balance < 0)
		right = max(0, right + balance);
	if (balance > 0)
		left = max(0, left - balance);

	tc9154a_set_attenuation(fmr2, abs(left - 68), TC9154A_CHANNEL_LEFT);
	tc9154a_set_attenuation(fmr2, abs(right - 68), TC9154A_CHANNEL_RIGHT);

	return 0;
}

static const struct v4l2_ctrl_ops fmr2_ctrl_ops = {
	.s_ctrl = fmr2_s_ctrl,
};

static int fmr2_tea_ext_init(struct snd_tea575x *tea)
{
	struct fmr2 *fmr2 = tea->private_data;

	/* FMR2 can have volume control, FMD2 can't (uses SB16 mixer) */
	if (!fmr2->is_fmd2 && inb(fmr2->io) & FMR2_HASVOL) {
		fmr2->volume = v4l2_ctrl_new_std(&tea->ctrl_handler, &fmr2_ctrl_ops, V4L2_CID_AUDIO_VOLUME, 0, 68, 2, 56);
		fmr2->balance = v4l2_ctrl_new_std(&tea->ctrl_handler, &fmr2_ctrl_ops, V4L2_CID_AUDIO_BALANCE, -68, 68, 2, 0);
		if (tea->ctrl_handler.error) {
			printk(KERN_ERR "radio-sf16fmr2: can't initialize controls\n");
			return tea->ctrl_handler.error;
		}
	}

	return 0;
}

static struct pnp_device_id fmr2_pnp_ids[] = {
	{ .id = "MFRad13" }, /* tuner subdevice of SF16-FMD2 */
	{ .id = "" }
};
MODULE_DEVICE_TABLE(pnp, fmr2_pnp_ids);

static int fmr2_probe(struct fmr2 *fmr2, struct device *pdev, int io)
{
	int err, i;
	char *card_name = fmr2->is_fmd2 ? "SF16-FMD2" : "SF16-FMR2";

	/* avoid errors if a card was already registered at given port */
	for (i = 0; i < num_fmr2_cards; i++)
		if (io == fmr2_cards[i]->io)
			return -EBUSY;

	strlcpy(fmr2->v4l2_dev.name, "radio-sf16fmr2",
			sizeof(fmr2->v4l2_dev.name)),
	fmr2->io = io;

	if (!request_region(fmr2->io, 2, fmr2->v4l2_dev.name)) {
		printk(KERN_ERR "radio-sf16fmr2: I/O port 0x%x already in use\n", fmr2->io);
		return -EBUSY;
	}

	dev_set_drvdata(pdev, fmr2);
	err = v4l2_device_register(pdev, &fmr2->v4l2_dev);
	if (err < 0) {
		v4l2_err(&fmr2->v4l2_dev, "Could not register v4l2_device\n");
		release_region(fmr2->io, 2);
		return err;
	}
	fmr2->tea.v4l2_dev = &fmr2->v4l2_dev;
	fmr2->tea.private_data = fmr2;
	fmr2->tea.radio_nr = radio_nr[num_fmr2_cards];
	fmr2->tea.ops = &fmr2_tea_ops;
	fmr2->tea.ext_init = fmr2_tea_ext_init;
	strlcpy(fmr2->tea.card, card_name, sizeof(fmr2->tea.card));
	snprintf(fmr2->tea.bus_info, sizeof(fmr2->tea.bus_info), "%s:%s",
			fmr2->is_fmd2 ? "PnP" : "ISA", dev_name(pdev));

	if (snd_tea575x_init(&fmr2->tea, THIS_MODULE)) {
		printk(KERN_ERR "radio-sf16fmr2: Unable to detect TEA575x tuner\n");
		release_region(fmr2->io, 2);
		return -ENODEV;
	}

	printk(KERN_INFO "radio-sf16fmr2: %s radio card at 0x%x.\n",
			card_name, fmr2->io);
	return 0;
}

static int fmr2_isa_match(struct device *pdev, unsigned int ndev)
{
	struct fmr2 *fmr2 = kzalloc(sizeof(*fmr2), GFP_KERNEL);
	if (!fmr2)
		return 0;

	if (fmr2_probe(fmr2, pdev, FMR2_PORT)) {
		kfree(fmr2);
		return 0;
	}
	dev_set_drvdata(pdev, fmr2);
	fmr2_cards[num_fmr2_cards++] = fmr2;

	return 1;
}

static int fmr2_pnp_probe(struct pnp_dev *pdev, const struct pnp_device_id *id)
{
	int ret;
	struct fmr2 *fmr2 = kzalloc(sizeof(*fmr2), GFP_KERNEL);
	if (!fmr2)
		return -ENOMEM;

	fmr2->is_fmd2 = true;
	ret = fmr2_probe(fmr2, &pdev->dev, pnp_port_start(pdev, 0));
	if (ret) {
		kfree(fmr2);
		return ret;
	}
	pnp_set_drvdata(pdev, fmr2);
	fmr2_cards[num_fmr2_cards++] = fmr2;

	return 0;
}

static void fmr2_remove(struct fmr2 *fmr2)
{
	snd_tea575x_exit(&fmr2->tea);
	release_region(fmr2->io, 2);
	v4l2_device_unregister(&fmr2->v4l2_dev);
	kfree(fmr2);
}

static int fmr2_isa_remove(struct device *pdev, unsigned int ndev)
{
	fmr2_remove(dev_get_drvdata(pdev));

	return 0;
}

static void fmr2_pnp_remove(struct pnp_dev *pdev)
{
	fmr2_remove(pnp_get_drvdata(pdev));
	pnp_set_drvdata(pdev, NULL);
}

static struct isa_driver fmr2_isa_driver = {
	.match		= fmr2_isa_match,
	.remove		= fmr2_isa_remove,
	.driver		= {
		.name	= "radio-sf16fmr2",
	},
};

static struct pnp_driver fmr2_pnp_driver = {
	.name		= "radio-sf16fmr2",
	.id_table	= fmr2_pnp_ids,
	.probe		= fmr2_pnp_probe,
	.remove		= fmr2_pnp_remove,
};

static int __init fmr2_init(void)
{
	fmr2_unit.port = io;
	fmr2_unit.curvol = 0;
	fmr2_unit.mute = 0;
	fmr2_unit.curfreq = 0;
	fmr2_unit.stereo = 1;
	fmr2_unit.flags = V4L2_TUNER_CAP_LOW;
	fmr2_unit.card_type = 0;
	fmr2_radio.priv = &fmr2_unit;

	mutex_init(&lock);

	if (!request_region(io, 2, "sf16fmr2")) {
		printk(KERN_ERR "radio-sf16fmr2: request_region failed!\n");
		return -EBUSY;
	}

	if (video_register_device(&fmr2_radio, VFL_TYPE_RADIO, radio_nr) < 0) {
		release_region(io, 2);
		return -EINVAL;
	}

	printk(KERN_INFO "SF16FMR2 radio card driver at 0x%x.\n", io);
	/* mute card - prevents noisy bootups */
	mutex_lock(&lock);
	fmr2_mute(io);
	fmr2_product_info(&fmr2_unit);
	mutex_unlock(&lock);
	debug_print((KERN_DEBUG "card_type %d\n", fmr2_unit.card_type));

	/* Only card_type == 11 implements volume */
	if (fmr2_unit.card_type != 11)
		radio_qctrl[AUD_VOL_INDEX].maximum = 1;

	return 0;
}

MODULE_AUTHOR("Ziglio Frediano, freddy77@angelfire.com");
MODULE_DESCRIPTION("A driver for the SF16FMR2 radio.");
MODULE_LICENSE("GPL");

module_param(io, int, 0);
MODULE_PARM_DESC(io, "I/O address of the SF16FMR2 card (should be 0x384, if do not work try 0x284)");
module_param(radio_nr, int, 0);

static void __exit fmr2_cleanup_module(void)
{
	video_unregister_device(&fmr2_radio);
	release_region(io,2);
}

module_init(fmr2_init);
module_exit(fmr2_cleanup_module);

#ifndef MODULE

static int __init fmr2_setup_io(char *str)
{
	get_option(&str, &io);
	return 1;
}

__setup("sf16fmr2=", fmr2_setup_io);

#endif
	int ret;

	ret = pnp_register_driver(&fmr2_pnp_driver);
	if (!ret)
		pnp_registered = true;
	ret = isa_register_driver(&fmr2_isa_driver, 1);
	if (!ret)
		isa_registered = true;

	return (pnp_registered || isa_registered) ? 0 : ret;
}

static void __exit fmr2_exit(void)
{
	if (pnp_registered)
		pnp_unregister_driver(&fmr2_pnp_driver);
	if (isa_registered)
		isa_unregister_driver(&fmr2_isa_driver);
}

module_init(fmr2_init);
module_exit(fmr2_exit);
