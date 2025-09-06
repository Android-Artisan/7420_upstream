/*
 *	w83627hf/thf WDT driver
 *
 *	(c) Copyright 2007 Vlad Drukker <vlad@storewiz.com>
 *		added support for W83627THF.
 *
 *	(c) Copyright 2003,2007 Pádraig Brady <P@draigBrady.com>
 *	(c) Copyright 2013 Guenter Roeck
 *		converted to watchdog infrastructure
 *
 *	(c) Copyright 2007 Vlad Drukker <vlad@storewiz.com>
 *		added support for W83627THF.
 *
 *	(c) Copyright 2003,2007 PÃ¡draig Brady <P@draigBrady.com>
 *
 *	Based on advantechwdt.c which is based on wdt.c.
 *	Original copyright messages:
 *
 *	(c) Copyright 2000-2001 Marek Michalkiewicz <marekm@linux.org.pl>
 *
 *	(c) Copyright 1996 Alan Cox <alan@redhat.com>, All Rights Reserved.
 *				http://www.redhat.com
 *	(c) Copyright 1996 Alan Cox <alan@lxorguk.ukuu.org.uk>,
 *						All Rights Reserved.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	Neither Alan Cox nor CymruNet Ltd. admit liability nor provide
 *	warranty for any of this software. This material is provided
 *	"AS-IS" and at no charge.
 *
 *	(c) Copyright 1995    Alan Cox <alan@redhat.com>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/fs.h>
 *	(c) Copyright 1995    Alan Cox <alan@lxorguk.ukuu.org.uk>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/watchdog.h>
#include <linux/ioport.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/uaccess.h>

#include <asm/system.h>

#define WATCHDOG_NAME "w83627hf/thf/hg WDT"
#define PFX WATCHDOG_NAME ": "
#define WATCHDOG_TIMEOUT 60		/* 60 sec default timeout */

static unsigned long wdt_is_open;
static char expect_close;
static DEFINE_SPINLOCK(io_lock);

/* You must set this - there is no sane way to probe for this board. */
static int wdt_io = 0x2E;
module_param(wdt_io, int, 0);
MODULE_PARM_DESC(wdt_io, "w83627hf/thf WDT io port (default 0x2E)");

static int timeout = WATCHDOG_TIMEOUT;	/* in seconds */
#include <linux/io.h>

#define WATCHDOG_NAME "w83627hf/thf/hg/dhg WDT"
#define WATCHDOG_TIMEOUT 60		/* 60 sec default timeout */

static int wdt_io;
static int cr_wdt_timeout;	/* WDT timeout register */
static int cr_wdt_control;	/* WDT control register */

enum chips { w83627hf, w83627s, w83697hf, w83697ug, w83637hf, w83627thf,
	     w83687thf, w83627ehf, w83627dhg, w83627uhg, w83667hg, w83627dhg_p,
	     w83667hg_b, nct6775, nct6776, nct6779, nct6791, nct6792 };

static int timeout;			/* in seconds */
module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout,
		"Watchdog timeout in seconds. 1 <= timeout <= 255, default="
				__MODULE_STRING(WATCHDOG_TIMEOUT) ".");

static int nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, int, 0);
static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
		"Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static int early_disable;
module_param(early_disable, int, 0);
MODULE_PARM_DESC(early_disable, "Disable watchdog at boot time (default=0)");

/*
 *	Kernel methods.
 */

#define WDT_EFER (wdt_io+0)   /* Extended Function Enable Registers */
#define WDT_EFIR (wdt_io+0)   /* Extended Function Index Register
							(same as EFER) */
#define WDT_EFDR (WDT_EFIR+1) /* Extended Function Data Register */

static void w83627hf_select_wd_register(void)
{
	unsigned char c;
	outb_p(0x87, WDT_EFER); /* Enter extended function mode */
	outb_p(0x87, WDT_EFER); /* Again according to manual */

	outb(0x20, WDT_EFER);	/* check chip version	*/
	c = inb(WDT_EFDR);
	if (c == 0x82) {	/* W83627THF		*/
		outb_p(0x2b, WDT_EFER); /* select GPIO3 */
		c = ((inb_p(WDT_EFDR) & 0xf7) | 0x04); /* select WDT0 */
		outb_p(0x2b, WDT_EFER);
		outb_p(c, WDT_EFDR);	/* set GPIO3 to WDT0 */
	}

	outb_p(0x07, WDT_EFER); /* point to logical device number reg */
	outb_p(0x08, WDT_EFDR); /* select logical device 8 (GPIO2) */
	outb_p(0x30, WDT_EFER); /* select CR30 */
	outb_p(0x01, WDT_EFDR); /* set bit 0 to activate GPIO2 */
}

static void w83627hf_unselect_wd_register(void)
{
	outb_p(0xAA, WDT_EFER); /* Leave extended function mode */
}

/* tyan motherboards seem to set F5 to 0x4C ?
 * So explicitly init to appropriate value. */

static void w83627hf_init(void)
{
	unsigned char t;

	w83627hf_select_wd_register();

	outb_p(0xF6, WDT_EFER); /* Select CRF6 */
	t = inb_p(WDT_EFDR);      /* read CRF6 */
	if (t != 0) {
		printk(KERN_INFO PFX
		     "Watchdog already running. Resetting timeout to %d sec\n",
								timeout);
		outb_p(timeout, WDT_EFDR);    /* Write back to CRF6 */
	}

	outb_p(0xF5, WDT_EFER); /* Select CRF5 */
	t = inb_p(WDT_EFDR);      /* read CRF5 */
	t &= ~0x0C;               /* set second mode & disable keyboard
				    turning off watchdog */
	outb_p(t, WDT_EFDR);    /* Write back to CRF5 */

	outb_p(0xF7, WDT_EFER); /* Select CRF7 */
	t = inb_p(WDT_EFDR);      /* read CRF7 */
	t &= ~0xC0;               /* disable keyboard & mouse turning off
				    watchdog */
	outb_p(t, WDT_EFDR);    /* Write back to CRF7 */

	w83627hf_unselect_wd_register();
}

static void wdt_ctrl(int timeout)
{
	spin_lock(&io_lock);

	w83627hf_select_wd_register();

	outb_p(0xF6, WDT_EFER);    /* Select CRF6 */
	outb_p(timeout, WDT_EFDR); /* Write Timeout counter to CRF6 */

	w83627hf_unselect_wd_register();

	spin_unlock(&io_lock);
}

static int wdt_ping(void)
{
	wdt_ctrl(timeout);
	return 0;
}

static int wdt_disable(void)
{
	wdt_ctrl(0);
	return 0;
}

static int wdt_set_heartbeat(int t)
{
	if (t < 1 || t > 255)
		return -EINVAL;
	timeout = t;
	return 0;
}

static ssize_t wdt_write(struct file *file, const char __user *buf,
						size_t count, loff_t *ppos)
{
	if (count) {
		if (!nowayout) {
			size_t i;

			expect_close = 0;

			for (i = 0; i != count; i++) {
				char c;
				if (get_user(c, buf + i))
					return -EFAULT;
				if (c == 'V')
					expect_close = 42;
			}
		}
		wdt_ping();
	}
	return count;
}

static long wdt_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	int new_timeout;
	static struct watchdog_info ident = {
		.options = WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT |
							WDIOF_MAGICCLOSE,
		.firmware_version = 1,
		.identity = "W83627HF WDT",
	};

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		if (copy_to_user(argp, &ident, sizeof(ident)))
			return -EFAULT;
		break;
	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		return put_user(0, p);
	case WDIOC_SETOPTIONS:
	{
		int options, retval = -EINVAL;

		if (get_user(options, p))
			return -EFAULT;
		if (options & WDIOS_DISABLECARD) {
			wdt_disable();
			retval = 0;
		}
		if (options & WDIOS_ENABLECARD) {
			wdt_ping();
			retval = 0;
		}
		return retval;
	}
	case WDIOC_KEEPALIVE:
		wdt_ping();
		break;
	case WDIOC_SETTIMEOUT:
		if (get_user(new_timeout, p))
			return -EFAULT;
		if (wdt_set_heartbeat(new_timeout))
			return -EINVAL;
		wdt_ping();
		/* Fall */
	case WDIOC_GETTIMEOUT:
		return put_user(timeout, p);
	default:
		return -ENOTTY;
	}
	return 0;
}

static int wdt_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(0, &wdt_is_open))
		return -EBUSY;
	/*
	 *	Activate
	 */

	wdt_ping();
	return nonseekable_open(inode, file);
}

static int wdt_close(struct inode *inode, struct file *file)
{
	if (expect_close == 42)
		wdt_disable();
	else {
		printk(KERN_CRIT PFX
			"Unexpected close, not stopping watchdog!\n");
		wdt_ping();
	}
	expect_close = 0;
	clear_bit(0, &wdt_is_open);
	return 0;
}

/*
 *	Notifier for system down
 */

#define W83627HF_LD_WDT		0x08

#define W83627HF_ID		0x52
#define W83627S_ID		0x59
#define W83697HF_ID		0x60
#define W83697UG_ID		0x68
#define W83637HF_ID		0x70
#define W83627THF_ID		0x82
#define W83687THF_ID		0x85
#define W83627EHF_ID		0x88
#define W83627DHG_ID		0xa0
#define W83627UHG_ID		0xa2
#define W83667HG_ID		0xa5
#define W83627DHG_P_ID		0xb0
#define W83667HG_B_ID		0xb3
#define NCT6775_ID		0xb4
#define NCT6776_ID		0xc3
#define NCT6779_ID		0xc5
#define NCT6791_ID		0xc8
#define NCT6792_ID		0xc9

#define W83627HF_WDT_TIMEOUT	0xf6
#define W83697HF_WDT_TIMEOUT	0xf4

#define W83627HF_WDT_CONTROL	0xf5
#define W83697HF_WDT_CONTROL	0xf3

static void superio_outb(int reg, int val)
{
	outb(reg, WDT_EFER);
	outb(val, WDT_EFDR);
}

static inline int superio_inb(int reg)
{
	outb(reg, WDT_EFER);
	return inb(WDT_EFDR);
}

static int superio_enter(void)
{
	if (!request_muxed_region(wdt_io, 2, WATCHDOG_NAME))
		return -EBUSY;

	outb_p(0x87, WDT_EFER); /* Enter extended function mode */
	outb_p(0x87, WDT_EFER); /* Again according to manual */

	return 0;
}

static void superio_select(int ld)
{
	superio_outb(0x07, ld);
}

static void superio_exit(void)
{
	outb_p(0xAA, WDT_EFER); /* Leave extended function mode */
	release_region(wdt_io, 2);
}

static int w83627hf_init(struct watchdog_device *wdog, enum chips chip)
{
	int ret;
	unsigned char t;

	ret = superio_enter();
	if (ret)
		return ret;

	superio_select(W83627HF_LD_WDT);

	/* set CR30 bit 0 to activate GPIO2 */
	t = superio_inb(0x30);
	if (!(t & 0x01))
		superio_outb(0x30, t | 0x01);

	switch (chip) {
	case w83627hf:
	case w83627s:
		t = superio_inb(0x2B) & ~0x10;
		superio_outb(0x2B, t); /* set GPIO24 to WDT0 */
		break;
	case w83697hf:
		/* Set pin 119 to WDTO# mode (= CR29, WDT0) */
		t = superio_inb(0x29) & ~0x60;
		t |= 0x20;
		superio_outb(0x29, t);
		break;
	case w83697ug:
		/* Set pin 118 to WDTO# mode */
		t = superio_inb(0x2b) & ~0x04;
		superio_outb(0x2b, t);
		break;
	case w83627thf:
		t = (superio_inb(0x2B) & ~0x08) | 0x04;
		superio_outb(0x2B, t); /* set GPIO3 to WDT0 */
		break;
	case w83627dhg:
	case w83627dhg_p:
		t = superio_inb(0x2D) & ~0x01; /* PIN77 -> WDT0# */
		superio_outb(0x2D, t); /* set GPIO5 to WDT0 */
		t = superio_inb(cr_wdt_control);
		t |= 0x02;	/* enable the WDTO# output low pulse
				 * to the KBRST# pin */
		superio_outb(cr_wdt_control, t);
		break;
	case w83637hf:
		break;
	case w83687thf:
		t = superio_inb(0x2C) & ~0x80; /* PIN47 -> WDT0# */
		superio_outb(0x2C, t);
		break;
	case w83627ehf:
	case w83627uhg:
	case w83667hg:
	case w83667hg_b:
	case nct6775:
	case nct6776:
	case nct6779:
	case nct6791:
	case nct6792:
		/*
		 * These chips have a fixed WDTO# output pin (W83627UHG),
		 * or support more than one WDTO# output pin.
		 * Don't touch its configuration, and hope the BIOS
		 * does the right thing.
		 */
		t = superio_inb(cr_wdt_control);
		t |= 0x02;	/* enable the WDTO# output low pulse
				 * to the KBRST# pin */
		superio_outb(cr_wdt_control, t);
		break;
	default:
		break;
	}

	t = superio_inb(cr_wdt_timeout);
	if (t != 0) {
		if (early_disable) {
			pr_warn("Stopping previously enabled watchdog until userland kicks in\n");
			superio_outb(cr_wdt_timeout, 0);
		} else {
			pr_info("Watchdog already running. Resetting timeout to %d sec\n",
				wdog->timeout);
			superio_outb(cr_wdt_timeout, wdog->timeout);
		}
	}

	/* set second mode & disable keyboard turning off watchdog */
	t = superio_inb(cr_wdt_control) & ~0x0C;
	superio_outb(cr_wdt_control, t);

	/* reset trigger, disable keyboard & mouse turning off watchdog */
	t = superio_inb(0xF7) & ~0xD0;
	superio_outb(0xF7, t);

	superio_exit();

	return 0;
}

static int wdt_set_time(unsigned int timeout)
{
	int ret;

	ret = superio_enter();
	if (ret)
		return ret;

	superio_select(W83627HF_LD_WDT);
	superio_outb(cr_wdt_timeout, timeout);
	superio_exit();

	return 0;
}

static int wdt_start(struct watchdog_device *wdog)
{
	return wdt_set_time(wdog->timeout);
}

static int wdt_stop(struct watchdog_device *wdog)
{
	return wdt_set_time(0);
}

static int wdt_set_timeout(struct watchdog_device *wdog, unsigned int timeout)
{
	wdog->timeout = timeout;

	return 0;
}

static unsigned int wdt_get_time(struct watchdog_device *wdog)
{
	unsigned int timeleft;
	int ret;

	ret = superio_enter();
	if (ret)
		return 0;

	superio_select(W83627HF_LD_WDT);
	timeleft = superio_inb(cr_wdt_timeout);
	superio_exit();

	return timeleft;
}

/*
 *	Notifier for system down
 */
static int wdt_notify_sys(struct notifier_block *this, unsigned long code,
	void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT)
		wdt_disable();	/* Turn the WDT off */
		wdt_set_time(0);	/* Turn the WDT off */

	return NOTIFY_DONE;
}

/*
 *	Kernel Interfaces
 */

static const struct file_operations wdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= wdt_write,
	.unlocked_ioctl	= wdt_ioctl,
	.open		= wdt_open,
	.release	= wdt_close,
};

static struct miscdevice wdt_miscdev = {
	.minor = WATCHDOG_MINOR,
	.name = "watchdog",
	.fops = &wdt_fops,
static struct watchdog_info wdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
	.identity = "W83627HF Watchdog",
};

static struct watchdog_ops wdt_ops = {
	.owner = THIS_MODULE,
	.start = wdt_start,
	.stop = wdt_stop,
	.set_timeout = wdt_set_timeout,
	.get_timeleft = wdt_get_time,
};

static struct watchdog_device wdt_dev = {
	.info = &wdt_info,
	.ops = &wdt_ops,
	.timeout = WATCHDOG_TIMEOUT,
	.min_timeout = 1,
	.max_timeout = 255,
};

/*
 *	The WDT needs to learn about soft shutdowns in order to
 *	turn the timebomb registers off.
 */

static struct notifier_block wdt_notifier = {
	.notifier_call = wdt_notify_sys,
};

static int __init wdt_init(void)
{
	int ret;

	printk(KERN_INFO "WDT driver for the Winbond(TM) W83627HF/THF/HG Super I/O chip initialising.\n");

	if (wdt_set_heartbeat(timeout)) {
		wdt_set_heartbeat(WATCHDOG_TIMEOUT);
		printk(KERN_INFO PFX
		     "timeout value must be 1 <= timeout <= 255, using %d\n",
				WATCHDOG_TIMEOUT);
	}

	if (!request_region(wdt_io, 1, WATCHDOG_NAME)) {
		printk(KERN_ERR PFX "I/O address 0x%04x already in use\n",
			wdt_io);
		ret = -EIO;
		goto out;
	}

	w83627hf_init();

	ret = register_reboot_notifier(&wdt_notifier);
	if (ret != 0) {
		printk(KERN_ERR PFX
			"cannot register reboot notifier (err=%d)\n", ret);
		goto unreg_regions;
	}

	ret = misc_register(&wdt_miscdev);
	if (ret != 0) {
		printk(KERN_ERR PFX
			"cannot register miscdev on minor=%d (err=%d)\n",
							WATCHDOG_MINOR, ret);
		goto unreg_reboot;
	}

	printk(KERN_INFO PFX
			"initialized. timeout=%d sec (nowayout=%d)\n",
							timeout, nowayout);

out:
	return ret;
unreg_reboot:
	unregister_reboot_notifier(&wdt_notifier);
unreg_regions:
	release_region(wdt_io, 1);
	goto out;
static int wdt_find(int addr)
{
	u8 val;
	int ret;

	cr_wdt_timeout = W83627HF_WDT_TIMEOUT;
	cr_wdt_control = W83627HF_WDT_CONTROL;

	ret = superio_enter();
	if (ret)
		return ret;
	superio_select(W83627HF_LD_WDT);
	val = superio_inb(0x20);
	switch (val) {
	case W83627HF_ID:
		ret = w83627hf;
		break;
	case W83627S_ID:
		ret = w83627s;
		break;
	case W83697HF_ID:
		ret = w83697hf;
		cr_wdt_timeout = W83697HF_WDT_TIMEOUT;
		cr_wdt_control = W83697HF_WDT_CONTROL;
		break;
	case W83697UG_ID:
		ret = w83697ug;
		cr_wdt_timeout = W83697HF_WDT_TIMEOUT;
		cr_wdt_control = W83697HF_WDT_CONTROL;
		break;
	case W83637HF_ID:
		ret = w83637hf;
		break;
	case W83627THF_ID:
		ret = w83627thf;
		break;
	case W83687THF_ID:
		ret = w83687thf;
		break;
	case W83627EHF_ID:
		ret = w83627ehf;
		break;
	case W83627DHG_ID:
		ret = w83627dhg;
		break;
	case W83627DHG_P_ID:
		ret = w83627dhg_p;
		break;
	case W83627UHG_ID:
		ret = w83627uhg;
		break;
	case W83667HG_ID:
		ret = w83667hg;
		break;
	case W83667HG_B_ID:
		ret = w83667hg_b;
		break;
	case NCT6775_ID:
		ret = nct6775;
		break;
	case NCT6776_ID:
		ret = nct6776;
		break;
	case NCT6779_ID:
		ret = nct6779;
		break;
	case NCT6791_ID:
		ret = nct6791;
		break;
	case NCT6792_ID:
		ret = nct6792;
		break;
	case 0xff:
		ret = -ENODEV;
		break;
	default:
		ret = -ENODEV;
		pr_err("Unsupported chip ID: 0x%02x\n", val);
		break;
	}
	superio_exit();
	return ret;
}

static int __init wdt_init(void)
{
	int ret;
	int chip;
	const char * const chip_name[] = {
		"W83627HF",
		"W83627S",
		"W83697HF",
		"W83697UG",
		"W83637HF",
		"W83627THF",
		"W83687THF",
		"W83627EHF",
		"W83627DHG",
		"W83627UHG",
		"W83667HG",
		"W83667DHG-P",
		"W83667HG-B",
		"NCT6775",
		"NCT6776",
		"NCT6779",
		"NCT6791",
		"NCT6792",
	};

	wdt_io = 0x2e;
	chip = wdt_find(0x2e);
	if (chip < 0) {
		wdt_io = 0x4e;
		chip = wdt_find(0x4e);
		if (chip < 0)
			return chip;
	}

	pr_info("WDT driver for %s Super I/O chip initialising\n",
		chip_name[chip]);

	watchdog_init_timeout(&wdt_dev, timeout, NULL);
	watchdog_set_nowayout(&wdt_dev, nowayout);

	ret = w83627hf_init(&wdt_dev, chip);
	if (ret) {
		pr_err("failed to initialize watchdog (err=%d)\n", ret);
		return ret;
	}

	ret = register_reboot_notifier(&wdt_notifier);
	if (ret != 0) {
		pr_err("cannot register reboot notifier (err=%d)\n", ret);
		return ret;
	}

	ret = watchdog_register_device(&wdt_dev);
	if (ret)
		goto unreg_reboot;

	pr_info("initialized. timeout=%d sec (nowayout=%d)\n",
		wdt_dev.timeout, nowayout);

	return ret;

unreg_reboot:
	unregister_reboot_notifier(&wdt_notifier);
	return ret;
}

static void __exit wdt_exit(void)
{
	misc_deregister(&wdt_miscdev);
	unregister_reboot_notifier(&wdt_notifier);
	release_region(wdt_io, 1);
	watchdog_unregister_device(&wdt_dev);
	unregister_reboot_notifier(&wdt_notifier);
}

module_init(wdt_init);
module_exit(wdt_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pádraig Brady <P@draigBrady.com>");
MODULE_DESCRIPTION("w83627hf/thf WDT driver");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
MODULE_AUTHOR("PÃ¡draig  Brady <P@draigBrady.com>");
MODULE_DESCRIPTION("w83627hf/thf WDT driver");
