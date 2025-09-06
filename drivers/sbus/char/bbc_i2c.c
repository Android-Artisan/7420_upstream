/* $Id: bbc_i2c.c,v 1.2 2001/04/02 09:59:08 davem Exp $
 * bbc_i2c.c: I2C low-level driver for BBC device on UltraSPARC-III
 *            platforms.
 *
 * Copyright (C) 2001 David S. Miller (davem@redhat.com)
/* bbc_i2c.c: I2C low-level driver for BBC device on UltraSPARC-III
 *            platforms.
 *
 * Copyright (C) 2001, 2008 David S. Miller (davem@davemloft.net)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/oplib.h>
#include <asm/ebus.h>
#include <asm/spitfire.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <asm/bbc.h>
#include <asm/io.h>

#include "bbc_i2c.h"

/* Convert this driver to use i2c bus layer someday... */
#define I2C_PCF_PIN	0x80
#define I2C_PCF_ESO	0x40
#define I2C_PCF_ES1	0x20
#define I2C_PCF_ES2	0x10
#define I2C_PCF_ENI	0x08
#define I2C_PCF_STA	0x04
#define I2C_PCF_STO	0x02
#define I2C_PCF_ACK	0x01

#define I2C_PCF_START    (I2C_PCF_PIN | I2C_PCF_ESO | I2C_PCF_ENI | I2C_PCF_STA | I2C_PCF_ACK)
#define I2C_PCF_STOP     (I2C_PCF_PIN | I2C_PCF_ESO | I2C_PCF_STO | I2C_PCF_ACK)
#define I2C_PCF_REPSTART (              I2C_PCF_ESO | I2C_PCF_STA | I2C_PCF_ACK)
#define I2C_PCF_IDLE     (I2C_PCF_PIN | I2C_PCF_ESO               | I2C_PCF_ACK)

#define I2C_PCF_INI 0x40   /* 1 if not initialized */
#define I2C_PCF_STS 0x20
#define I2C_PCF_BER 0x10
#define I2C_PCF_AD0 0x08
#define I2C_PCF_LRB 0x08
#define I2C_PCF_AAS 0x04
#define I2C_PCF_LAB 0x02
#define I2C_PCF_BB  0x01

/* The BBC devices have two I2C controllers.  The first I2C controller
 * connects mainly to configuration proms (NVRAM, cpu configuration,
 * dimm types, etc.).  Whereas the second I2C controller connects to
 * environmental control devices such as fans and temperature sensors.
 * The second controller also connects to the smartcard reader, if present.
 */

#define NUM_CHILDREN	8
struct bbc_i2c_bus {
	struct bbc_i2c_bus		*next;
	int				index;
	spinlock_t			lock;
	void				__iomem *i2c_bussel_reg;
	void				__iomem *i2c_control_regs;
	unsigned char			own, clock;

	wait_queue_head_t		wq;
	volatile int			waiting;

	struct linux_ebus_device	*bus_edev;
	struct {
		struct linux_ebus_child	*device;
		int			client_claimed;
	} devs[NUM_CHILDREN];
};

static struct bbc_i2c_bus *all_bbc_i2c;

struct bbc_i2c_client {
	struct bbc_i2c_bus	*bp;
	struct linux_ebus_child	*echild;
	int			bus;
	int			address;
};

static int find_device(struct bbc_i2c_bus *bp, struct linux_ebus_child *echild)
static void set_device_claimage(struct bbc_i2c_bus *bp, struct platform_device *op, int val)
{
	int i;

	for (i = 0; i < NUM_CHILDREN; i++) {
		if (bp->devs[i].device == echild) {
			if (bp->devs[i].client_claimed)
				return 0;
			return 1;
		}
	}
	return 0;
}

static void set_device_claimage(struct bbc_i2c_bus *bp, struct linux_ebus_child *echild, int val)
{
	int i;

	for (i = 0; i < NUM_CHILDREN; i++) {
		if (bp->devs[i].device == echild) {
		if (bp->devs[i].device == op) {
			bp->devs[i].client_claimed = val;
			return;
		}
	}
}

#define claim_device(BP,ECHILD)		set_device_claimage(BP,ECHILD,1)
#define release_device(BP,ECHILD)	set_device_claimage(BP,ECHILD,0)

static struct bbc_i2c_bus *find_bus_for_device(struct linux_ebus_child *echild)
{
	struct bbc_i2c_bus *bp = all_bbc_i2c;

	while (bp != NULL) {
		if (find_device(bp, echild) != 0)
			break;
		bp = bp->next;
	}

	return bp;
}

struct linux_ebus_child *bbc_i2c_getdev(int index)
{
	struct bbc_i2c_bus *bp = all_bbc_i2c;
	struct linux_ebus_child *echild = NULL;
	int curidx = 0;

	while (bp != NULL) {
		struct bbc_i2c_bus *next = bp->next;
		int i;

		for (i = 0; i < NUM_CHILDREN; i++) {
			if (!(echild = bp->devs[i].device))
				break;
			if (curidx == index)
				goto out;
			echild = NULL;
			curidx++;
		}
		bp = next;
	}
out:
	if (curidx == index)
		return echild;
	return NULL;
}

struct bbc_i2c_client *bbc_i2c_attach(struct linux_ebus_child *echild)
{
	struct bbc_i2c_bus *bp = find_bus_for_device(echild);
	struct bbc_i2c_client *client;

	if (!bp)
		return NULL;
struct platform_device *bbc_i2c_getdev(struct bbc_i2c_bus *bp, int index)
{
	struct platform_device *op = NULL;
	int curidx = 0, i;

	for (i = 0; i < NUM_CHILDREN; i++) {
		if (!(op = bp->devs[i].device))
			break;
		if (curidx == index)
			goto out;
		op = NULL;
		curidx++;
	}

out:
	if (curidx == index)
		return op;
	return NULL;
}

struct bbc_i2c_client *bbc_i2c_attach(struct bbc_i2c_bus *bp, struct platform_device *op)
{
	struct bbc_i2c_client *client;
	const u32 *reg;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return NULL;
	client->bp = bp;
	client->echild = echild;
	client->bus = echild->resource[0].start;
	client->address = echild->resource[1].start;

	claim_device(bp, echild);
	client->op = op;

	reg = of_get_property(op->dev.of_node, "reg", NULL);
	if (!reg) {
		kfree(client);
		return NULL;
	}

	client->bus = reg[0];
	client->address = reg[1];

	claim_device(bp, op);

	return client;
}

void bbc_i2c_detach(struct bbc_i2c_client *client)
{
	struct bbc_i2c_bus *bp = client->bp;
	struct linux_ebus_child *echild = client->echild;

	release_device(bp, echild);
	struct platform_device *op = client->op;

	release_device(bp, op);
	kfree(client);
}

static int wait_for_pin(struct bbc_i2c_bus *bp, u8 *status)
{
	DECLARE_WAITQUEUE(wait, current);
	int limit = 32;
	int ret = 1;

	bp->waiting = 1;
	add_wait_queue(&bp->wq, &wait);
	while (limit-- > 0) {
		unsigned long val;
		long val;

		val = wait_event_interruptible_timeout(
				bp->wq,
				(((*status = readb(bp->i2c_control_regs + 0))
				  & I2C_PCF_PIN) == 0),
				msecs_to_jiffies(250));
		if (val > 0) {
			ret = 0;
			break;
		}
	}
	remove_wait_queue(&bp->wq, &wait);
	bp->waiting = 0;

	return ret;
}

int bbc_i2c_writeb(struct bbc_i2c_client *client, unsigned char val, int off)
{
	struct bbc_i2c_bus *bp = client->bp;
	int address = client->address;
	u8 status;
	int ret = -1;

	if (bp->i2c_bussel_reg != NULL)
		writeb(client->bus, bp->i2c_bussel_reg);

	writeb(address, bp->i2c_control_regs + 0x1);
	writeb(I2C_PCF_START, bp->i2c_control_regs + 0x0);
	if (wait_for_pin(bp, &status))
		goto out;

	writeb(off, bp->i2c_control_regs + 0x1);
	if (wait_for_pin(bp, &status) ||
	    (status & I2C_PCF_LRB) != 0)
		goto out;

	writeb(val, bp->i2c_control_regs + 0x1);
	if (wait_for_pin(bp, &status))
		goto out;

	ret = 0;

out:
	writeb(I2C_PCF_STOP, bp->i2c_control_regs + 0x0);
	return ret;
}

int bbc_i2c_readb(struct bbc_i2c_client *client, unsigned char *byte, int off)
{
	struct bbc_i2c_bus *bp = client->bp;
	unsigned char address = client->address, status;
	int ret = -1;

	if (bp->i2c_bussel_reg != NULL)
		writeb(client->bus, bp->i2c_bussel_reg);

	writeb(address, bp->i2c_control_regs + 0x1);
	writeb(I2C_PCF_START, bp->i2c_control_regs + 0x0);
	if (wait_for_pin(bp, &status))
		goto out;

	writeb(off, bp->i2c_control_regs + 0x1);
	if (wait_for_pin(bp, &status) ||
	    (status & I2C_PCF_LRB) != 0)
		goto out;

	writeb(I2C_PCF_STOP, bp->i2c_control_regs + 0x0);

	address |= 0x1; /* READ */

	writeb(address, bp->i2c_control_regs + 0x1);
	writeb(I2C_PCF_START, bp->i2c_control_regs + 0x0);
	if (wait_for_pin(bp, &status))
		goto out;

	/* Set PIN back to one so the device sends the first
	 * byte.
	 */
	(void) readb(bp->i2c_control_regs + 0x1);
	if (wait_for_pin(bp, &status))
		goto out;

	writeb(I2C_PCF_ESO | I2C_PCF_ENI, bp->i2c_control_regs + 0x0);
	*byte = readb(bp->i2c_control_regs + 0x1);
	if (wait_for_pin(bp, &status))
		goto out;

	ret = 0;

out:
	writeb(I2C_PCF_STOP, bp->i2c_control_regs + 0x0);
	(void) readb(bp->i2c_control_regs + 0x1);

	return ret;
}

int bbc_i2c_write_buf(struct bbc_i2c_client *client,
		      char *buf, int len, int off)
{
	int ret = 0;

	while (len > 0) {
		int err = bbc_i2c_writeb(client, *buf, off);

		if (err < 0) {
			ret = err;
			break;
		}

		ret = bbc_i2c_writeb(client, *buf, off);
		if (ret < 0)
			break;
		len--;
		buf++;
		off++;
	}
	return ret;
}

int bbc_i2c_read_buf(struct bbc_i2c_client *client,
		     char *buf, int len, int off)
{
	int ret = 0;

	while (len > 0) {
		int err = bbc_i2c_readb(client, buf, off);
		if (err < 0) {
			ret = err;
			break;
		}
		ret = bbc_i2c_readb(client, buf, off);
		if (ret < 0)
			break;
		len--;
		buf++;
		off++;
	}

	return ret;
}

EXPORT_SYMBOL(bbc_i2c_getdev);
EXPORT_SYMBOL(bbc_i2c_attach);
EXPORT_SYMBOL(bbc_i2c_detach);
EXPORT_SYMBOL(bbc_i2c_writeb);
EXPORT_SYMBOL(bbc_i2c_readb);
EXPORT_SYMBOL(bbc_i2c_write_buf);
EXPORT_SYMBOL(bbc_i2c_read_buf);

static irqreturn_t bbc_i2c_interrupt(int irq, void *dev_id)
{
	struct bbc_i2c_bus *bp = dev_id;

	/* PIN going from set to clear is the only event which
	 * makes the i2c assert an interrupt.
	 */
	if (bp->waiting &&
	    !(readb(bp->i2c_control_regs + 0x0) & I2C_PCF_PIN))
		wake_up_interruptible(&bp->wq);

	return IRQ_HANDLED;
}

static void __init reset_one_i2c(struct bbc_i2c_bus *bp)
static void reset_one_i2c(struct bbc_i2c_bus *bp)
{
	writeb(I2C_PCF_PIN, bp->i2c_control_regs + 0x0);
	writeb(bp->own, bp->i2c_control_regs + 0x1);
	writeb(I2C_PCF_PIN | I2C_PCF_ES1, bp->i2c_control_regs + 0x0);
	writeb(bp->clock, bp->i2c_control_regs + 0x1);
	writeb(I2C_PCF_IDLE, bp->i2c_control_regs + 0x0);
}

static int __init attach_one_i2c(struct linux_ebus_device *edev, int index)
{
	struct bbc_i2c_bus *bp;
	struct linux_ebus_child *echild;
static struct bbc_i2c_bus * attach_one_i2c(struct platform_device *op, int index)
{
	struct bbc_i2c_bus *bp;
	struct device_node *dp;
	int entry;

	bp = kzalloc(sizeof(*bp), GFP_KERNEL);
	if (!bp)
		return -ENOMEM;

	bp->i2c_control_regs = ioremap(edev->resource[0].start, 0x2);
	if (!bp->i2c_control_regs)
		goto fail;

	if (edev->num_addrs == 2) {
		bp->i2c_bussel_reg = ioremap(edev->resource[1].start, 0x1);
		return NULL;

	INIT_LIST_HEAD(&bp->temps);
	INIT_LIST_HEAD(&bp->fans);

	bp->i2c_control_regs = of_ioremap(&op->resource[0], 0, 0x2, "bbc_i2c_regs");
	if (!bp->i2c_control_regs)
		goto fail;

	if (op->num_resources == 2) {
		bp->i2c_bussel_reg = of_ioremap(&op->resource[1], 0, 0x1, "bbc_i2c_bussel");
		if (!bp->i2c_bussel_reg)
			goto fail;
	}

	bp->waiting = 0;
	init_waitqueue_head(&bp->wq);
	if (request_irq(edev->irqs[0], bbc_i2c_interrupt,
	if (request_irq(op->archdata.irqs[0], bbc_i2c_interrupt,
			IRQF_SHARED, "bbc_i2c", bp))
		goto fail;

	bp->index = index;
	bp->bus_edev = edev;

	spin_lock_init(&bp->lock);
	bp->next = all_bbc_i2c;
	all_bbc_i2c = bp;

	entry = 0;
	for (echild = edev->children;
	     echild && entry < 8;
	     echild = echild->next, entry++) {
		bp->devs[entry].device = echild;
	bp->op = op;

	spin_lock_init(&bp->lock);

	entry = 0;
	for (dp = op->dev.of_node->child;
	     dp && entry < 8;
	     dp = dp->sibling, entry++) {
		struct platform_device *child_op;

		child_op = of_find_device_by_node(dp);
		bp->devs[entry].device = child_op;
		bp->devs[entry].client_claimed = 0;
	}

	writeb(I2C_PCF_PIN, bp->i2c_control_regs + 0x0);
	bp->own = readb(bp->i2c_control_regs + 0x01);
	writeb(I2C_PCF_PIN | I2C_PCF_ES1, bp->i2c_control_regs + 0x0);
	bp->clock = readb(bp->i2c_control_regs + 0x01);

	printk(KERN_INFO "i2c-%d: Regs at %p, %d devices, own %02x, clock %02x.\n",
	       bp->index, bp->i2c_control_regs, entry, bp->own, bp->clock);

	reset_one_i2c(bp);

	return 0;

fail:
	if (bp->i2c_bussel_reg)
		iounmap(bp->i2c_bussel_reg);
	if (bp->i2c_control_regs)
		iounmap(bp->i2c_control_regs);
	kfree(bp);
	return -EINVAL;
}

static int __init bbc_present(void)
{
	struct linux_ebus *ebus = NULL;
	struct linux_ebus_device *edev = NULL;

	for_each_ebus(ebus) {
		for_each_ebusdev(edev, ebus) {
			if (!strcmp(edev->prom_node->name, "bbc"))
				return 1;
		}
	}
	return 0;
}

extern int bbc_envctrl_init(void);
extern void bbc_envctrl_cleanup(void);
static void bbc_i2c_cleanup(void);

static int __init bbc_i2c_init(void)
{
	struct linux_ebus *ebus = NULL;
	struct linux_ebus_device *edev = NULL;
	int err, index = 0;

	if ((tlb_type != cheetah && tlb_type != cheetah_plus) ||
	    !bbc_present())
		return -ENODEV;

	for_each_ebus(ebus) {
		for_each_ebusdev(edev, ebus) {
			if (!strcmp(edev->prom_node->name, "i2c")) {
				if (!attach_one_i2c(edev, index))
					index++;
			}
		}
	}

	if (!index)
		return -ENODEV;

	err = bbc_envctrl_init();
	if (err)
		bbc_i2c_cleanup();
	return err;
}

static void bbc_i2c_cleanup(void)
{
	struct bbc_i2c_bus *bp = all_bbc_i2c;

	bbc_envctrl_cleanup();

	while (bp != NULL) {
		struct bbc_i2c_bus *next = bp->next;

		free_irq(bp->bus_edev->irqs[0], bp);

		if (bp->i2c_bussel_reg)
			iounmap(bp->i2c_bussel_reg);
		if (bp->i2c_control_regs)
			iounmap(bp->i2c_control_regs);

		kfree(bp);

		bp = next;
	}
	all_bbc_i2c = NULL;
}

module_init(bbc_i2c_init);
module_exit(bbc_i2c_cleanup);
	return bp;

fail:
	if (bp->i2c_bussel_reg)
		of_iounmap(&op->resource[1], bp->i2c_bussel_reg, 1);
	if (bp->i2c_control_regs)
		of_iounmap(&op->resource[0], bp->i2c_control_regs, 2);
	kfree(bp);
	return NULL;
}

extern int bbc_envctrl_init(struct bbc_i2c_bus *bp);
extern void bbc_envctrl_cleanup(struct bbc_i2c_bus *bp);

static int bbc_i2c_probe(struct platform_device *op)
{
	struct bbc_i2c_bus *bp;
	int err, index = 0;

	bp = attach_one_i2c(op, index);
	if (!bp)
		return -EINVAL;

	err = bbc_envctrl_init(bp);
	if (err) {
		free_irq(op->archdata.irqs[0], bp);
		if (bp->i2c_bussel_reg)
			of_iounmap(&op->resource[0], bp->i2c_bussel_reg, 1);
		if (bp->i2c_control_regs)
			of_iounmap(&op->resource[1], bp->i2c_control_regs, 2);
		kfree(bp);
	} else {
		dev_set_drvdata(&op->dev, bp);
	}

	return err;
}

static int bbc_i2c_remove(struct platform_device *op)
{
	struct bbc_i2c_bus *bp = dev_get_drvdata(&op->dev);

	bbc_envctrl_cleanup(bp);

	free_irq(op->archdata.irqs[0], bp);

	if (bp->i2c_bussel_reg)
		of_iounmap(&op->resource[0], bp->i2c_bussel_reg, 1);
	if (bp->i2c_control_regs)
		of_iounmap(&op->resource[1], bp->i2c_control_regs, 2);

	kfree(bp);

	return 0;
}

static const struct of_device_id bbc_i2c_match[] = {
	{
		.name = "i2c",
		.compatible = "SUNW,bbc-i2c",
	},
	{},
};
MODULE_DEVICE_TABLE(of, bbc_i2c_match);

static struct platform_driver bbc_i2c_driver = {
	.driver = {
		.name = "bbc_i2c",
		.of_match_table = bbc_i2c_match,
	},
	.probe		= bbc_i2c_probe,
	.remove		= bbc_i2c_remove,
};

module_platform_driver(bbc_i2c_driver);

MODULE_LICENSE("GPL");
