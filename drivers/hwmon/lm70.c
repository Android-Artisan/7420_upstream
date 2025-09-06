/*
 * lm70.c
 *
 * The LM70 is a temperature sensor chip from National Semiconductor (NS).
 * Copyright (C) 2006 Kaiwan N Billimoria <kaiwan@designergraphix.com>
 *
 * The LM70 communicates with a host processor via an SPI/Microwire Bus
 * interface. The complete datasheet is available at National's website
 * here:
 * http://www.national.com/pf/LM/LM70.html
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/sysfs.h>
#include <linux/hwmon.h>
#include <linux/mutex.h>
#include <linux/spi/spi.h>
#include <linux/mod_devicetable.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/of_device.h>


#define DRVNAME		"lm70"

struct lm70 {
	struct device *hwmon_dev;
	struct mutex lock;
#define LM70_CHIP_LM70		0	/* original NS LM70 */
#define LM70_CHIP_TMP121	1	/* TI TMP121/TMP123 */
#define LM70_CHIP_LM71		2	/* NS LM71 */
#define LM70_CHIP_LM74		3	/* NS LM74 */

struct lm70 {
	struct spi_device *spi;
	struct mutex lock;
	unsigned int chip;
};

/* sysfs hook function */
static ssize_t lm70_sense_temp(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct spi_device *spi = to_spi_device(dev);
	int status, val;
	u8 rxbuf[2];
	s16 raw=0;
	struct lm70 *p_lm70 = dev_get_drvdata(&spi->dev);
	struct lm70 *p_lm70 = dev_get_drvdata(dev);
	struct spi_device *spi = p_lm70->spi;
	int status, val = 0;
	u8 rxbuf[2];
	s16 raw = 0;

	if (mutex_lock_interruptible(&p_lm70->lock))
		return -ERESTARTSYS;

	/*
	 * spi_read() requires a DMA-safe buffer; so we use
	 * spi_write_then_read(), transmitting 0 bytes.
	 */
	status = spi_write_then_read(spi, NULL, 0, &rxbuf[0], 2);
	if (status < 0) {
		printk(KERN_WARNING
		"spi_write_then_read failed with status %d\n", status);
		goto out;
	}
	dev_dbg(dev, "rxbuf[1] : 0x%x rxbuf[0] : 0x%x\n", rxbuf[1], rxbuf[0]);

	raw = (rxbuf[1] << 8) + rxbuf[0];
	dev_dbg(dev, "raw=0x%x\n", raw);

	/*
		pr_warn("spi_write_then_read failed with status %d\n", status);
		goto out;
	}
	raw = (rxbuf[0] << 8) + rxbuf[1];
	dev_dbg(dev, "rxbuf[0] : 0x%02x rxbuf[1] : 0x%02x raw=0x%04x\n",
		rxbuf[0], rxbuf[1], raw);

	/*
	 * LM70:
	 * The "raw" temperature read into rxbuf[] is a 16-bit signed 2's
	 * complement value. Only the MSB 11 bits (1 sign + 10 temperature
	 * bits) are meaningful; the LSB 5 bits are to be discarded.
	 * See the datasheet.
	 *
	 * Further, each bit represents 0.25 degrees Celsius; so, multiply
	 * by 0.25. Also multiply by 1000 to represent in millidegrees
	 * Celsius.
	 * So it's equivalent to multiplying by 0.25 * 1000 = 250.
	 */
	val = ((int)raw/32) * 250;
	 *
	 * LM74 and TMP121/TMP123:
	 * 13 bits of 2's complement data, discard LSB 3 bits,
	 * resolution 0.0625 degrees celsius.
	 *
	 * LM71:
	 * 14 bits of 2's complement data, discard LSB 2 bits,
	 * resolution 0.0312 degrees celsius.
	 */
	switch (p_lm70->chip) {
	case LM70_CHIP_LM70:
		val = ((int)raw / 32) * 250;
		break;

	case LM70_CHIP_TMP121:
	case LM70_CHIP_LM74:
		val = ((int)raw / 8) * 625 / 10;
		break;

	case LM70_CHIP_LM71:
		val = ((int)raw / 4) * 3125 / 100;
		break;
	}

	status = sprintf(buf, "%d\n", val); /* millidegrees Celsius */
out:
	mutex_unlock(&p_lm70->lock);
	return status;
}

static DEVICE_ATTR(temp1_input, S_IRUGO, lm70_sense_temp, NULL);

static ssize_t lm70_show_name(struct device *dev, struct device_attribute
			      *devattr, char *buf)
{
	return sprintf(buf, "lm70\n");
}

static DEVICE_ATTR(name, S_IRUGO, lm70_show_name, NULL);

/*----------------------------------------------------------------------*/

static int __devinit lm70_probe(struct spi_device *spi)
{
	struct lm70 *p_lm70;
	int status;

	/* signaling is SPI_MODE_0 on a 3-wire link (shared SI/SO) */
	if ((spi->mode & (SPI_CPOL|SPI_CPHA)) || !(spi->mode & SPI_3WIRE))
		return -EINVAL;

	p_lm70 = kzalloc(sizeof *p_lm70, GFP_KERNEL);
static struct attribute *lm70_attrs[] = {
	&dev_attr_temp1_input.attr,
	NULL
};

ATTRIBUTE_GROUPS(lm70);

/*----------------------------------------------------------------------*/

#ifdef CONFIG_OF
static const struct of_device_id lm70_of_ids[] = {
	{
		.compatible = "ti,lm70",
		.data = (void *) LM70_CHIP_LM70,
	},
	{
		.compatible = "ti,tmp121",
		.data = (void *) LM70_CHIP_TMP121,
	},
	{
		.compatible = "ti,lm71",
		.data = (void *) LM70_CHIP_LM71,
	},
	{
		.compatible = "ti,lm74",
		.data = (void *) LM70_CHIP_LM74,
	},
	{},
};
MODULE_DEVICE_TABLE(of, lm70_of_ids);
#endif

static int lm70_probe(struct spi_device *spi)
{
	const struct of_device_id *match;
	struct device *hwmon_dev;
	struct lm70 *p_lm70;
	int chip;

	match = of_match_device(lm70_of_ids, &spi->dev);
	if (match)
		chip = (int)(uintptr_t)match->data;
	else
		chip = spi_get_device_id(spi)->driver_data;

	/* signaling is SPI_MODE_0 */
	if (spi->mode & (SPI_CPOL | SPI_CPHA))
		return -EINVAL;

	/* NOTE:  we assume 8-bit words, and convert to 16 bits manually */

	p_lm70 = devm_kzalloc(&spi->dev, sizeof(*p_lm70), GFP_KERNEL);
	if (!p_lm70)
		return -ENOMEM;

	mutex_init(&p_lm70->lock);

	/* sysfs hook */
	p_lm70->hwmon_dev = hwmon_device_register(&spi->dev);
	if (IS_ERR(p_lm70->hwmon_dev)) {
		dev_dbg(&spi->dev, "hwmon_device_register failed.\n");
		status = PTR_ERR(p_lm70->hwmon_dev);
		goto out_dev_reg_failed;
	}
	dev_set_drvdata(&spi->dev, p_lm70);

	if ((status = device_create_file(&spi->dev, &dev_attr_temp1_input))
	 || (status = device_create_file(&spi->dev, &dev_attr_name))) {
		dev_dbg(&spi->dev, "device_create_file failure.\n");
		goto out_dev_create_file_failed;
	}

	return 0;

out_dev_create_file_failed:
	device_remove_file(&spi->dev, &dev_attr_temp1_input);
	hwmon_device_unregister(p_lm70->hwmon_dev);
out_dev_reg_failed:
	dev_set_drvdata(&spi->dev, NULL);
	kfree(p_lm70);
	return status;
}

static int __devexit lm70_remove(struct spi_device *spi)
{
	struct lm70 *p_lm70 = dev_get_drvdata(&spi->dev);

	device_remove_file(&spi->dev, &dev_attr_temp1_input);
	device_remove_file(&spi->dev, &dev_attr_name);
	hwmon_device_unregister(p_lm70->hwmon_dev);
	dev_set_drvdata(&spi->dev, NULL);
	kfree(p_lm70);

	return 0;
}
	p_lm70->chip = chip;
	p_lm70->spi = spi;

	hwmon_dev = devm_hwmon_device_register_with_groups(&spi->dev,
							   spi->modalias,
							   p_lm70, lm70_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct spi_device_id lm70_ids[] = {
	{ "lm70",   LM70_CHIP_LM70 },
	{ "tmp121", LM70_CHIP_TMP121 },
	{ "lm71",   LM70_CHIP_LM71 },
	{ "lm74",   LM70_CHIP_LM74 },
	{ },
};
MODULE_DEVICE_TABLE(spi, lm70_ids);

static struct spi_driver lm70_driver = {
	.driver = {
		.name	= "lm70",
		.owner	= THIS_MODULE,
	},
	.probe	= lm70_probe,
	.remove	= __devexit_p(lm70_remove),
};

static int __init init_lm70(void)
{
	return spi_register_driver(&lm70_driver);
}

static void __exit cleanup_lm70(void)
{
	spi_unregister_driver(&lm70_driver);
}

module_init(init_lm70);
module_exit(cleanup_lm70);

MODULE_AUTHOR("Kaiwan N Billimoria");
MODULE_DESCRIPTION("National Semiconductor LM70 Linux driver");
		.of_match_table	= of_match_ptr(lm70_of_ids),
	},
	.id_table = lm70_ids,
	.probe	= lm70_probe,
};

module_spi_driver(lm70_driver);

MODULE_AUTHOR("Kaiwan N Billimoria");
MODULE_DESCRIPTION("NS LM70 and compatibles Linux driver");
MODULE_LICENSE("GPL");
