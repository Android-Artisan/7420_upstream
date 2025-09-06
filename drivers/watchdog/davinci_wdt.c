/*
 * drivers/char/watchdog/davinci_wdt.c
 *
 * Watchdog driver for DaVinci DM644x/DM646x processors
 *
 * Copyright (C) 2006 Texas Instruments.
 * Copyright (C) 2006-2013 Texas Instruments.
 *
 * 2007 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <mach/hardware.h>
#include <linux/watchdog.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/clk.h>
#include <linux/err.h>

#define MODULE_NAME "DAVINCI-WDT: "

#define DEFAULT_HEARTBEAT 60
#define MAX_HEARTBEAT     600	/* really the max margin is 264/27MHz*/

/* Timer register set definition */
#define PID12	(0x0)
#define EMUMGT	(0x4)
#define TIM12	(0x10)
#define TIM34	(0x14)
#define PRD12	(0x18)
#define PRD34	(0x1C)
#define TCR	(0x20)
#define TGCR	(0x24)
#define WDTCR	(0x28)

/* TCR bit definitions */
#define ENAMODE12_DISABLED	(0 << 6)
#define ENAMODE12_ONESHOT	(1 << 6)
#define ENAMODE12_PERIODIC	(2 << 6)

/* TGCR bit definitions */
#define TIM12RS_UNRESET		(1 << 0)
#define TIM34RS_UNRESET		(1 << 1)
#define TIMMODE_64BIT_WDOG      (2 << 2)

/* WDTCR bit definitions */
#define WDEN			(1 << 14)
#define WDFLAG			(1 << 15)
#define WDKEY_SEQ0		(0xa5c6 << 16)
#define WDKEY_SEQ1		(0xda7e << 16)

static int heartbeat = DEFAULT_HEARTBEAT;

static DEFINE_SPINLOCK(io_lock);
static unsigned long wdt_status;
#define WDT_IN_USE        0
#define WDT_OK_TO_CLOSE   1
#define WDT_REGION_INITED 2
#define WDT_DEVICE_INITED 3

static struct resource	*wdt_mem;
static void __iomem	*wdt_base;

static void wdt_service(void)
{
	spin_lock(&io_lock);

	/* put watchdog in service state */
	davinci_writel(WDKEY_SEQ0, wdt_base + WDTCR);
	/* put watchdog in active state */
	davinci_writel(WDKEY_SEQ1, wdt_base + WDTCR);

	spin_unlock(&io_lock);
}

static void wdt_enable(void)
{
	u32 tgcr;
	u32 timer_margin;

	spin_lock(&io_lock);

	/* disable, internal clock source */
	davinci_writel(0, wdt_base + TCR);
	/* reset timer, set mode to 64-bit watchdog, and unreset */
	davinci_writel(0, wdt_base + TGCR);
	tgcr = TIMMODE_64BIT_WDOG | TIM12RS_UNRESET | TIM34RS_UNRESET;
	davinci_writel(tgcr, wdt_base + TGCR);
	/* clear counter regs */
	davinci_writel(0, wdt_base + TIM12);
	davinci_writel(0, wdt_base + TIM34);
	/* set timeout period */
	timer_margin = (((u64)heartbeat * CLOCK_TICK_RATE) & 0xffffffff);
	davinci_writel(timer_margin, wdt_base + PRD12);
	timer_margin = (((u64)heartbeat * CLOCK_TICK_RATE) >> 32);
	davinci_writel(timer_margin, wdt_base + PRD34);
	/* enable run continuously */
	davinci_writel(ENAMODE12_PERIODIC, wdt_base + TCR);
static int heartbeat;

/*
 * struct to hold data for each WDT device
 * @base - base io address of WD device
 * @clk - source clock of WDT
 * @wdd - hold watchdog device as is in WDT core
 */
struct davinci_wdt_device {
	void __iomem		*base;
	struct clk		*clk;
	struct watchdog_device	wdd;
};

static int davinci_wdt_start(struct watchdog_device *wdd)
{
	u32 tgcr;
	u32 timer_margin;
	unsigned long wdt_freq;
	struct davinci_wdt_device *davinci_wdt = watchdog_get_drvdata(wdd);

	wdt_freq = clk_get_rate(davinci_wdt->clk);

	/* disable, internal clock source */
	iowrite32(0, davinci_wdt->base + TCR);
	/* reset timer, set mode to 64-bit watchdog, and unreset */
	iowrite32(0, davinci_wdt->base + TGCR);
	tgcr = TIMMODE_64BIT_WDOG | TIM12RS_UNRESET | TIM34RS_UNRESET;
	iowrite32(tgcr, davinci_wdt->base + TGCR);
	/* clear counter regs */
	iowrite32(0, davinci_wdt->base + TIM12);
	iowrite32(0, davinci_wdt->base + TIM34);
	/* set timeout period */
	timer_margin = (((u64)wdd->timeout * wdt_freq) & 0xffffffff);
	iowrite32(timer_margin, davinci_wdt->base + PRD12);
	timer_margin = (((u64)wdd->timeout * wdt_freq) >> 32);
	iowrite32(timer_margin, davinci_wdt->base + PRD34);
	/* enable run continuously */
	iowrite32(ENAMODE12_PERIODIC, davinci_wdt->base + TCR);
	/* Once the WDT is in pre-active state write to
	 * TIM12, TIM34, PRD12, PRD34, TCR, TGCR, WDTCR are
	 * write protected (except for the WDKEY field)
	 */
	/* put watchdog in pre-active state */
	davinci_writel(WDKEY_SEQ0 | WDEN, wdt_base + WDTCR);
	/* put watchdog in active state */
	davinci_writel(WDKEY_SEQ1 | WDEN, wdt_base + WDTCR);

	spin_unlock(&io_lock);
}

static int davinci_wdt_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(WDT_IN_USE, &wdt_status))
		return -EBUSY;

	wdt_enable();

	return nonseekable_open(inode, file);
}

static ssize_t
davinci_wdt_write(struct file *file, const char *data, size_t len,
		  loff_t *ppos)
{
	if (len)
		wdt_service();

	return len;
}

static struct watchdog_info ident = {
	.options = WDIOF_KEEPALIVEPING,
	.identity = "DaVinci Watchdog",
};

static long davinci_wdt_ioctl(struct file *file,
					unsigned int cmd, unsigned long arg)
{
	int ret = -ENOTTY;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		ret = copy_to_user((struct watchdog_info *)arg, &ident,
				   sizeof(ident)) ? -EFAULT : 0;
		break;

	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		ret = put_user(0, (int *)arg);
		break;

	case WDIOC_KEEPALIVE:
		wdt_service();
		ret = 0;
		break;

	case WDIOC_GETTIMEOUT:
		ret = put_user(heartbeat, (int *)arg);
		break;
	}
	return ret;
}

static int davinci_wdt_release(struct inode *inode, struct file *file)
{
	wdt_service();
	clear_bit(WDT_IN_USE, &wdt_status);

	return 0;
}

static const struct file_operations davinci_wdt_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.write = davinci_wdt_write,
	.unlocked_ioctl = davinci_wdt_ioctl,
	.open = davinci_wdt_open,
	.release = davinci_wdt_release,
};

static struct miscdevice davinci_wdt_miscdev = {
	.minor = WATCHDOG_MINOR,
	.name = "watchdog",
	.fops = &davinci_wdt_fops,
	iowrite32(WDKEY_SEQ0 | WDEN, davinci_wdt->base + WDTCR);
	/* put watchdog in active state */
	iowrite32(WDKEY_SEQ1 | WDEN, davinci_wdt->base + WDTCR);
	return 0;
}

static int davinci_wdt_ping(struct watchdog_device *wdd)
{
	struct davinci_wdt_device *davinci_wdt = watchdog_get_drvdata(wdd);

	/* put watchdog in service state */
	iowrite32(WDKEY_SEQ0, davinci_wdt->base + WDTCR);
	/* put watchdog in active state */
	iowrite32(WDKEY_SEQ1, davinci_wdt->base + WDTCR);
	return 0;
}

static unsigned int davinci_wdt_get_timeleft(struct watchdog_device *wdd)
{
	u64 timer_counter;
	unsigned long freq;
	u32 val;
	struct davinci_wdt_device *davinci_wdt = watchdog_get_drvdata(wdd);

	/* if timeout has occured then return 0 */
	val = ioread32(davinci_wdt->base + WDTCR);
	if (val & WDFLAG)
		return 0;

	freq = clk_get_rate(davinci_wdt->clk);

	if (!freq)
		return 0;

	timer_counter = ioread32(davinci_wdt->base + TIM12);
	timer_counter |= ((u64)ioread32(davinci_wdt->base + TIM34) << 32);

	do_div(timer_counter, freq);

	return wdd->timeout - timer_counter;
}

static const struct watchdog_info davinci_wdt_info = {
	.options = WDIOF_KEEPALIVEPING,
	.identity = "DaVinci/Keystone Watchdog",
};

static const struct watchdog_ops davinci_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= davinci_wdt_start,
	.stop		= davinci_wdt_ping,
	.ping		= davinci_wdt_ping,
	.get_timeleft	= davinci_wdt_get_timeleft,
};

static int davinci_wdt_probe(struct platform_device *pdev)
{
	int ret = 0, size;
	struct resource *res;

	if (heartbeat < 1 || heartbeat > MAX_HEARTBEAT)
		heartbeat = DEFAULT_HEARTBEAT;

	printk(KERN_INFO MODULE_NAME
		"DaVinci Watchdog Timer: heartbeat %d sec\n", heartbeat);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		printk(KERN_INFO MODULE_NAME
			"failed to get memory region resource\n");
		return -ENOENT;
	}

	size = res->end - res->start + 1;
	wdt_mem = request_mem_region(res->start, size, pdev->name);

	if (wdt_mem == NULL) {
		printk(KERN_INFO MODULE_NAME "failed to get memory region\n");
		return -ENOENT;
	}
	wdt_base = (void __iomem *)(res->start);

	ret = misc_register(&davinci_wdt_miscdev);
	if (ret < 0) {
		printk(KERN_ERR MODULE_NAME "cannot register misc device\n");
		release_resource(wdt_mem);
		kfree(wdt_mem);
	} else {
		set_bit(WDT_DEVICE_INITED, &wdt_status);
	}
	int ret = 0;
	struct device *dev = &pdev->dev;
	struct resource  *wdt_mem;
	struct watchdog_device *wdd;
	struct davinci_wdt_device *davinci_wdt;

	davinci_wdt = devm_kzalloc(dev, sizeof(*davinci_wdt), GFP_KERNEL);
	if (!davinci_wdt)
		return -ENOMEM;

	davinci_wdt->clk = devm_clk_get(dev, NULL);
	if (WARN_ON(IS_ERR(davinci_wdt->clk)))
		return PTR_ERR(davinci_wdt->clk);

	clk_prepare_enable(davinci_wdt->clk);

	platform_set_drvdata(pdev, davinci_wdt);

	wdd			= &davinci_wdt->wdd;
	wdd->info		= &davinci_wdt_info;
	wdd->ops		= &davinci_wdt_ops;
	wdd->min_timeout	= 1;
	wdd->max_timeout	= MAX_HEARTBEAT;
	wdd->timeout		= DEFAULT_HEARTBEAT;
	wdd->parent		= &pdev->dev;

	watchdog_init_timeout(wdd, heartbeat, dev);

	dev_info(dev, "heartbeat %d sec\n", wdd->timeout);

	watchdog_set_drvdata(wdd, davinci_wdt);
	watchdog_set_nowayout(wdd, 1);

	wdt_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	davinci_wdt->base = devm_ioremap_resource(dev, wdt_mem);
	if (IS_ERR(davinci_wdt->base))
		return PTR_ERR(davinci_wdt->base);

	ret = watchdog_register_device(wdd);
	if (ret < 0)
		dev_err(dev, "cannot register watchdog device\n");

	return ret;
}

static int davinci_wdt_remove(struct platform_device *pdev)
{
	misc_deregister(&davinci_wdt_miscdev);
	if (wdt_mem) {
		release_resource(wdt_mem);
		kfree(wdt_mem);
		wdt_mem = NULL;
	}
	return 0;
}

static struct platform_driver platform_wdt_driver = {
	.driver = {
		.name = "watchdog",
		.owner	= THIS_MODULE,
	struct davinci_wdt_device *davinci_wdt = platform_get_drvdata(pdev);

	watchdog_unregister_device(&davinci_wdt->wdd);
	clk_disable_unprepare(davinci_wdt->clk);

	return 0;
}

static const struct of_device_id davinci_wdt_of_match[] = {
	{ .compatible = "ti,davinci-wdt", },
	{},
};
MODULE_DEVICE_TABLE(of, davinci_wdt_of_match);

static struct platform_driver platform_wdt_driver = {
	.driver = {
		.name = "davinci-wdt",
		.of_match_table = davinci_wdt_of_match,
	},
	.probe = davinci_wdt_probe,
	.remove = davinci_wdt_remove,
};

static int __init davinci_wdt_init(void)
{
	return platform_driver_register(&platform_wdt_driver);
}

static void __exit davinci_wdt_exit(void)
{
	platform_driver_unregister(&platform_wdt_driver);
}

module_init(davinci_wdt_init);
module_exit(davinci_wdt_exit);
module_platform_driver(platform_wdt_driver);

MODULE_AUTHOR("Texas Instruments");
MODULE_DESCRIPTION("DaVinci Watchdog Driver");

module_param(heartbeat, int, 0);
MODULE_PARM_DESC(heartbeat,
		 "Watchdog heartbeat period in seconds from 1 to "
		 __MODULE_STRING(MAX_HEARTBEAT) ", default "
		 __MODULE_STRING(DEFAULT_HEARTBEAT));

MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
MODULE_ALIAS("platform:watchdog");
MODULE_ALIAS("platform:davinci-wdt");
