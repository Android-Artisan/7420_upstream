/*
 * Watchdog timer for PowerPC Book-E systems
 *
 * Author: Matthew McClintock
 * Maintainer: Kumar Gala <galak@kernel.crashing.org>
 *
 * Copyright 2005, 2008 Freescale Semiconductor Inc.
 * Copyright 2005, 2008, 2010-2011 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/smp.h>
#include <linux/miscdevice.h>
#include <linux/notifier.h>
#include <linux/watchdog.h>
#include <linux/uaccess.h>

#include <asm/reg_booke.h>
#include <asm/system.h>
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/smp.h>
#include <linux/watchdog.h>

#include <asm/reg_booke.h>
#include <asm/time.h>
#include <asm/div64.h>

/* If the kernel parameter wdt=1, the watchdog will be enabled at boot.
 * Also, the wdt_period sets the watchdog timer period timeout.
 * For E500 cpus the wdt_period sets which bit changing from 0->1 will
 * trigger a watchog timeout. This watchdog timeout will occur 3 times, the
 * first time nothing will happen, the second time a watchdog exception will
 * occur, and the final time the board will reset.
 */

#ifdef	CONFIG_FSL_BOOKE
#define WDT_PERIOD_DEFAULT 63	/* Ex. wdt_period=28 bus=333Mhz,reset=~40sec */
#else
#define WDT_PERIOD_DEFAULT 3	/* Refer to the PPC40x and PPC4xx manuals */
#endif				/* for timing information */

u32 booke_wdt_enabled;
u32 booke_wdt_period = WDT_PERIOD_DEFAULT;

#ifdef	CONFIG_FSL_BOOKE
#define WDTP(x)		((((63-x)&0x3)<<30)|(((63-x)&0x3c)<<15))
#else
#define WDTP(x)		(TCR_WP(x))
#endif

static DEFINE_SPINLOCK(booke_wdt_lock);

#ifdef	CONFIG_PPC_FSL_BOOK3E
#define WDTP(x)		((((x)&0x3)<<30)|(((x)&0x3c)<<15))
#define WDTP_MASK	(WDTP(0x3f))
#else
#define WDTP(x)		(TCR_WP(x))
#define WDTP_MASK	(TCR_WP_MASK)
#endif

static bool booke_wdt_enabled;
module_param(booke_wdt_enabled, bool, 0);
static int  booke_wdt_period = CONFIG_BOOKE_WDT_DEFAULT_TIMEOUT;
module_param(booke_wdt_period, int, 0);

#ifdef CONFIG_PPC_FSL_BOOK3E

/* For the specified period, determine the number of seconds
 * corresponding to the reset time.  There will be a watchdog
 * exception at approximately 3/5 of this time.
 *
 * The formula to calculate this is given by:
 * 2.5 * (2^(63-period+1)) / timebase_freq
 *
 * In order to simplify things, we assume that period is
 * at least 1.  This will still result in a very long timeout.
 */
static unsigned long long period_to_sec(unsigned int period)
{
	unsigned long long tmp = 1ULL << (64 - period);
	unsigned long tmp2 = ppc_tb_freq;

	/* tmp may be a very large number and we don't want to overflow,
	 * so divide the timebase freq instead of multiplying tmp
	 */
	tmp2 = tmp2 / 5 * 2;

	do_div(tmp, tmp2);
	return tmp;
}

/*
 * This procedure will find the highest period which will give a timeout
 * greater than the one required. e.g. for a bus speed of 66666666 and
 * and a parameter of 2 secs, then this procedure will return a value of 38.
 */
static unsigned int sec_to_period(unsigned int secs)
{
	unsigned int period;
	for (period = 63; period > 0; period--) {
		if (period_to_sec(period) >= secs)
			return period;
	}
	return 0;
}

#define MAX_WDT_TIMEOUT		period_to_sec(1)

#else /* CONFIG_PPC_FSL_BOOK3E */

static unsigned long long period_to_sec(unsigned int period)
{
	return period;
}

static unsigned int sec_to_period(unsigned int secs)
{
	return secs;
}

#define MAX_WDT_TIMEOUT		3	/* from Kconfig */

#endif /* !CONFIG_PPC_FSL_BOOK3E */

static void __booke_wdt_set(void *data)
{
	u32 val;
	struct watchdog_device *wdog = data;

	val = mfspr(SPRN_TCR);
	val &= ~WDTP_MASK;
	val |= WDTP(sec_to_period(wdog->timeout));

	mtspr(SPRN_TCR, val);
}

static void booke_wdt_set(void *data)
{
	on_each_cpu(__booke_wdt_set, data, 0);
}

static void __booke_wdt_ping(void *data)
{
	mtspr(SPRN_TSR, TSR_ENW|TSR_WIS);
}

static void booke_wdt_ping(void)
{
	on_each_cpu(__booke_wdt_ping, NULL, 0);
static int booke_wdt_ping(struct watchdog_device *wdog)
{
	on_each_cpu(__booke_wdt_ping, NULL, 0);

	return 0;
}

static void __booke_wdt_enable(void *data)
{
	u32 val;
	struct watchdog_device *wdog = data;

	/* clear status before enabling watchdog */
	__booke_wdt_ping(NULL);
	val = mfspr(SPRN_TCR);
	val |= (TCR_WIE|TCR_WRC(WRC_CHIP)|WDTP(booke_wdt_period));
	val &= ~WDTP_MASK;
	val |= (TCR_WIE|TCR_WRC(WRC_CHIP)|WDTP(sec_to_period(wdog->timeout)));

	mtspr(SPRN_TCR, val);
}

static ssize_t booke_wdt_write(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	booke_wdt_ping();
	return count;
}

static struct watchdog_info ident = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING,
	.identity = "PowerPC Book-E Watchdog",
};

static long booke_wdt_ioctl(struct file *file,
				unsigned int cmd, unsigned long arg)
{
	u32 tmp = 0;
	u32 __user *p = (u32 __user *)arg;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		if (copy_to_user(arg, &ident, sizeof(struct watchdog_info)))
			return -EFAULT;
	case WDIOC_GETSTATUS:
		return put_user(ident.options, p);
	case WDIOC_GETBOOTSTATUS:
		/* XXX: something is clearing TSR */
		tmp = mfspr(SPRN_TSR) & TSR_WRS(3);
		/* returns 1 if last reset was caused by the WDT */
		return (tmp ? 1 : 0);
	case WDIOC_SETOPTIONS:
		if (get_user(tmp, p))
			return -EINVAL;
		if (tmp == WDIOS_ENABLECARD) {
			booke_wdt_ping();
			break;
		} else
			return -EINVAL;
		return 0;
	case WDIOC_KEEPALIVE:
		booke_wdt_ping();
		return 0;
	case WDIOC_SETTIMEOUT:
		if (get_user(booke_wdt_period, p))
			return -EFAULT;
		mtspr(SPRN_TCR, (mfspr(SPRN_TCR) & ~WDTP(0)) |
						WDTP(booke_wdt_period));
		return 0;
	case WDIOC_GETTIMEOUT:
		return put_user(booke_wdt_period, p);
	default:
		return -ENOTTY;
	}
/**
 * booke_wdt_disable - disable the watchdog on the given CPU
 *
 * This function is called on each CPU.  It disables the watchdog on that CPU.
 *
 * TCR[WRC] cannot be changed once it has been set to non-zero, but we can
 * effectively disable the watchdog by setting its period to the maximum value.
 */
static void __booke_wdt_disable(void *data)
{
	u32 val;

	val = mfspr(SPRN_TCR);
	val &= ~(TCR_WIE | WDTP_MASK);
	mtspr(SPRN_TCR, val);

	/* clear status to make sure nothing is pending */
	__booke_wdt_ping(NULL);

}

static int booke_wdt_start(struct watchdog_device *wdog)
{
	on_each_cpu(__booke_wdt_enable, wdog, 0);
	pr_debug("watchdog enabled (timeout = %u sec)\n", wdog->timeout);

	return 0;
}

static int booke_wdt_open(struct inode *inode, struct file *file)
{
	spin_lock(&booke_wdt_lock);
	if (booke_wdt_enabled == 0) {
		booke_wdt_enabled = 1;
		on_each_cpu(__booke_wdt_enable, NULL, 0);
		printk(KERN_INFO
		      "PowerPC Book-E Watchdog Timer Enabled (wdt_period=%d)\n",
				booke_wdt_period);
	}
	spin_unlock(&booke_wdt_lock);

	return nonseekable_open(inode, file);
}

static const struct file_operations booke_wdt_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.write = booke_wdt_write,
	.unlocked_ioctl = booke_wdt_ioctl,
	.open = booke_wdt_open,
};

static struct miscdevice booke_wdt_miscdev = {
	.minor = WATCHDOG_MINOR,
	.name = "watchdog",
	.fops = &booke_wdt_fops,
static int booke_wdt_stop(struct watchdog_device *wdog)
{
	on_each_cpu(__booke_wdt_disable, NULL, 0);
	pr_debug("watchdog disabled\n");

	return 0;
}

static int booke_wdt_set_timeout(struct watchdog_device *wdt_dev,
				 unsigned int timeout)
{
	wdt_dev->timeout = timeout;
	booke_wdt_set(wdt_dev);

	return 0;
}

static struct watchdog_info booke_wdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING,
	.identity = "PowerPC Book-E Watchdog",
};

static struct watchdog_ops booke_wdt_ops = {
	.owner = THIS_MODULE,
	.start = booke_wdt_start,
	.stop = booke_wdt_stop,
	.ping = booke_wdt_ping,
	.set_timeout = booke_wdt_set_timeout,
};

static struct watchdog_device booke_wdt_dev = {
	.info = &booke_wdt_info,
	.ops = &booke_wdt_ops,
	.min_timeout = 1,
};

static void __exit booke_wdt_exit(void)
{
	misc_deregister(&booke_wdt_miscdev);
	watchdog_unregister_device(&booke_wdt_dev);
}

static int __init booke_wdt_init(void)
{
	int ret = 0;

	printk(KERN_INFO "PowerPC Book-E Watchdog Timer Loaded\n");
	ident.firmware_version = cur_cpu_spec->pvr_value;

	ret = misc_register(&booke_wdt_miscdev);
	if (ret) {
		printk(KERN_CRIT "Cannot register miscdev on minor=%d: %d\n",
				WATCHDOG_MINOR, ret);
		return ret;
	}

	spin_lock(&booke_wdt_lock);
	if (booke_wdt_enabled == 1) {
		printk(KERN_INFO
		      "PowerPC Book-E Watchdog Timer Enabled (wdt_period=%d)\n",
				booke_wdt_period);
		on_each_cpu(__booke_wdt_enable, NULL, 0);
	}
	spin_unlock(&booke_wdt_lock);

	return ret;
}
device_initcall(booke_wdt_init);
	bool nowayout = WATCHDOG_NOWAYOUT;

	pr_info("powerpc book-e watchdog driver loaded\n");
	booke_wdt_info.firmware_version = cur_cpu_spec->pvr_value;
	booke_wdt_set_timeout(&booke_wdt_dev,
			      period_to_sec(booke_wdt_period));
	watchdog_set_nowayout(&booke_wdt_dev, nowayout);
	booke_wdt_dev.max_timeout = MAX_WDT_TIMEOUT;
	if (booke_wdt_enabled)
		booke_wdt_start(&booke_wdt_dev);

	ret = watchdog_register_device(&booke_wdt_dev);

	return ret;
}

module_init(booke_wdt_init);
module_exit(booke_wdt_exit);

MODULE_ALIAS("booke_wdt");
MODULE_DESCRIPTION("PowerPC Book-E watchdog driver");
MODULE_LICENSE("GPL");
