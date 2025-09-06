/*
 * drivers/hwmon/applesmc.c - driver for Apple's SMC (accelerometer, temperature
 * sensors, fan control, keyboard backlight control) used in Intel-based Apple
 * computers.
 *
 * Copyright (C) 2007 Nicolas Boichat <nicolas@boichat.ch>
 *
 * Based on hdaps.c driver:
 * Copyright (C) 2005 Robert Love <rml@novell.com>
 * Copyright (C) 2005 Jesper Juhl <jesper.juhl@gmail.com>
 * Copyright (C) 2010 Henrik Rydberg <rydberg@euromail.se>
 *
 * Based on hdaps.c driver:
 * Copyright (C) 2005 Robert Love <rml@novell.com>
 * Copyright (C) 2005 Jesper Juhl <jj@chaosbits.net>
 *
 * Fan control based on smcFanControl:
 * Copyright (C) 2006 Hendrik Holtmann <holtmann@mac.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License v2 as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input-polldev.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/dmi.h>
#include <linux/mutex.h>
#include <linux/hwmon-sysfs.h>
#include <asm/io.h>
#include <linux/leds.h>
#include <linux/hwmon.h>
#include <linux/workqueue.h>
#include <linux/io.h>
#include <linux/leds.h>
#include <linux/hwmon.h>
#include <linux/workqueue.h>
#include <linux/err.h>

/* data port used by Apple SMC */
#define APPLESMC_DATA_PORT	0x300
/* command/status port used by Apple SMC */
#define APPLESMC_CMD_PORT	0x304

#define APPLESMC_NR_PORTS	32 /* 0x300-0x31f */

#define APPLESMC_MAX_DATA_LENGTH 32

#define APPLESMC_STATUS_MASK	0x0f
/* wait up to 128 ms for a status change. */
#define APPLESMC_MIN_WAIT	0x0010
#define APPLESMC_RETRY_WAIT	0x0100
#define APPLESMC_MAX_WAIT	0x20000

#define APPLESMC_READ_CMD	0x10
#define APPLESMC_WRITE_CMD	0x11
#define APPLESMC_GET_KEY_BY_INDEX_CMD	0x12
#define APPLESMC_GET_KEY_TYPE_CMD	0x13

#define KEY_COUNT_KEY		"#KEY" /* r-o ui32 */

#define LIGHT_SENSOR_LEFT_KEY	"ALV0" /* r-o {alv (6 bytes) */
#define LIGHT_SENSOR_RIGHT_KEY	"ALV1" /* r-o {alv (6 bytes) */
#define LIGHT_SENSOR_LEFT_KEY	"ALV0" /* r-o {alv (6-10 bytes) */
#define LIGHT_SENSOR_RIGHT_KEY	"ALV1" /* r-o {alv (6-10 bytes) */
#define BACKLIGHT_KEY		"LKSB" /* w-o {lkb (2 bytes) */

#define CLAMSHELL_KEY		"MSLD" /* r-o ui8 (unused) */

#define MOTION_SENSOR_X_KEY	"MO_X" /* r-o sp78 (2 bytes) */
#define MOTION_SENSOR_Y_KEY	"MO_Y" /* r-o sp78 (2 bytes) */
#define MOTION_SENSOR_Z_KEY	"MO_Z" /* r-o sp78 (2 bytes) */
#define MOTION_SENSOR_KEY	"MOCN" /* r/w ui16 */

#define FANS_COUNT		"FNum" /* r-o ui8 */
#define FANS_MANUAL		"FS! " /* r-w ui16 */
#define FAN_ACTUAL_SPEED	"F0Ac" /* r-o fpe2 (2 bytes) */
#define FAN_MIN_SPEED		"F0Mn" /* r-o fpe2 (2 bytes) */
#define FAN_MAX_SPEED		"F0Mx" /* r-o fpe2 (2 bytes) */
#define FAN_SAFE_SPEED		"F0Sf" /* r-o fpe2 (2 bytes) */
#define FAN_TARGET_SPEED	"F0Tg" /* r-w fpe2 (2 bytes) */
#define FAN_POSITION		"F0ID" /* r-o char[16] */

/*
 * Temperature sensors keys (sp78 - 2 bytes).
 */
static const char* temperature_sensors_sets[][36] = {
/* Set 0: Macbook Pro */
	{ "TA0P", "TB0T", "TC0D", "TC0P", "TG0H", "TG0P", "TG0T", "Th0H",
	  "Th1H", "Tm0P", "Ts0P", "Ts1P", NULL },
/* Set 1: Macbook2 set */
	{ "TB0T", "TC0D", "TC0P", "TM0P", "TN0P", "TN1P", "TTF0", "Th0H",
	  "Th0S", "Th1H", NULL },
/* Set 2: Macbook set */
	{ "TB0T", "TC0D", "TC0P", "TM0P", "TN0P", "TN1P", "Th0H", "Th0S",
	  "Th1H", "Ts0P", NULL },
/* Set 3: Macmini set */
	{ "TC0D", "TC0P", NULL },
/* Set 4: Mac Pro (2 x Quad-Core) */
	{ "TA0P", "TCAG", "TCAH", "TCBG", "TCBH", "TC0C", "TC0D", "TC0P",
	  "TC1C", "TC1D", "TC2C", "TC2D", "TC3C", "TC3D", "THTG", "TH0P",
	  "TH1P", "TH2P", "TH3P", "TMAP", "TMAS", "TMBS", "TM0P", "TM0S",
	  "TM1P", "TM1S", "TM2P", "TM2S", "TM3S", "TM8P", "TM8S", "TM9P",
	  "TM9S", "TN0H", "TS0C", NULL },
/* Set 5: iMac */
	{ "TC0D", "TA0P", "TG0P", "TG0D", "TG0H", "TH0P", "Tm0P", "TO0P",
	  "Tp0C", NULL },
/* Set 6: Macbook3 set */
	{ "TB0T", "TC0D", "TC0P", "TM0P", "TN0P", "TTF0", "TW0P", "Th0H",
	  "Th0S", "Th1H", NULL },
};

/* List of keys used to read/write fan speeds */
static const char* fan_speed_keys[] = {
	FAN_ACTUAL_SPEED,
	FAN_MIN_SPEED,
	FAN_MAX_SPEED,
	FAN_SAFE_SPEED,
	FAN_TARGET_SPEED
#define FAN_ID_FMT		"F%dID" /* r-o char[16] */

#define TEMP_SENSOR_TYPE	"sp78"

/* List of keys used to read/write fan speeds */
static const char *const fan_speed_fmt[] = {
	"F%dAc",		/* actual speed */
	"F%dMn",		/* minimum speed (rw) */
	"F%dMx",		/* maximum speed */
	"F%dSf",		/* safe speed - not all models */
	"F%dTg",		/* target speed (manual: rw) */
};

#define INIT_TIMEOUT_MSECS	5000	/* wait up to 5s for device init ... */
#define INIT_WAIT_MSECS		50	/* ... in 50ms increments */

#define APPLESMC_POLL_INTERVAL	50	/* msecs */
#define APPLESMC_INPUT_FUZZ	4	/* input event threshold */
#define APPLESMC_INPUT_FLAT	4

#define SENSOR_X 0
#define SENSOR_Y 1
#define SENSOR_Z 2

/* Structure to be passed to DMI_MATCH function */
struct dmi_match_data {
/* Indicates whether this computer has an accelerometer. */
	int accelerometer;
/* Indicates whether this computer has light sensors and keyboard backlight. */
	int light;
/* Indicates which temperature sensors set to use. */
	int temperature_set;
#define to_index(attr) (to_sensor_dev_attr(attr)->index & 0xffff)
#define to_option(attr) (to_sensor_dev_attr(attr)->index >> 16)

/* Dynamic device node attributes */
struct applesmc_dev_attr {
	struct sensor_device_attribute sda;	/* hwmon attributes */
	char name[32];				/* room for node file name */
};

/* Dynamic device node group */
struct applesmc_node_group {
	char *format;				/* format string */
	void *show;				/* show function */
	void *store;				/* store function */
	int option;				/* function argument */
	struct applesmc_dev_attr *nodes;	/* dynamic node array */
};

/* AppleSMC entry - cached register information */
struct applesmc_entry {
	char key[5];		/* four-letter key code */
	u8 valid;		/* set when entry is successfully read once */
	u8 len;			/* bounded by APPLESMC_MAX_DATA_LENGTH */
	char type[5];		/* four-letter type code */
	u8 flags;		/* 0x10: func; 0x40: write; 0x80: read */
};

/* Register lookup and registers common to all SMCs */
static struct applesmc_registers {
	struct mutex mutex;		/* register read/write mutex */
	unsigned int key_count;		/* number of SMC registers */
	unsigned int fan_count;		/* number of fans */
	unsigned int temp_count;	/* number of temperature registers */
	unsigned int temp_begin;	/* temperature lower index bound */
	unsigned int temp_end;		/* temperature upper index bound */
	unsigned int index_count;	/* size of temperature index array */
	int num_light_sensors;		/* number of light sensors */
	bool has_accelerometer;		/* has motion sensor */
	bool has_key_backlight;		/* has keyboard backlight */
	bool init_complete;		/* true when fully initialized */
	struct applesmc_entry *cache;	/* cached key entries */
	const char **index;		/* temperature key index */
} smcreg = {
	.mutex = __MUTEX_INITIALIZER(smcreg.mutex),
};

static const int debug;
static struct platform_device *pdev;
static s16 rest_x;
static s16 rest_y;
static struct device *hwmon_dev;
static struct input_polled_dev *applesmc_idev;

/* Indicates whether this computer has an accelerometer. */
static unsigned int applesmc_accelerometer;

/* Indicates whether this computer has light sensors and keyboard backlight. */
static unsigned int applesmc_light;

/* Indicates which temperature sensors set to use. */
static unsigned int applesmc_temperature_set;

static DEFINE_MUTEX(applesmc_lock);

static u8 backlight_state[2];

static struct device *hwmon_dev;
static struct input_polled_dev *applesmc_idev;

/*
 * Last index written to key_at_index sysfs file, and value to use for all other
 * key_at_index_* sysfs files.
 */
static unsigned int key_at_index;

static struct workqueue_struct *applesmc_led_wq;

/*
 * __wait_status - Wait up to 2ms for the status port to get a certain value
 * (masked with 0x0f), returning zero if the value is obtained.  Callers must
 * hold applesmc_lock.
 */
static int __wait_status(u8 val)
{
	unsigned int i;

	val = val & APPLESMC_STATUS_MASK;

	for (i = 0; i < 200; i++) {
		if ((inb(APPLESMC_CMD_PORT) & APPLESMC_STATUS_MASK) == val) {
			if (debug)
				printk(KERN_DEBUG
						"Waited %d us for status %x\n",
						i*10, val);
			return 0;
		}
		udelay(10);
	}

	printk(KERN_WARNING "applesmc: wait status failed: %x != %x\n",
						val, inb(APPLESMC_CMD_PORT));

 * wait_read - Wait for a byte to appear on SMC port. Callers must
 * hold applesmc_lock.
 */
static int wait_read(void)
{
	u8 status;
	int us;
	for (us = APPLESMC_MIN_WAIT; us < APPLESMC_MAX_WAIT; us <<= 1) {
		udelay(us);
		status = inb(APPLESMC_CMD_PORT);
		/* read: wait for smc to settle */
		if (status & 0x01)
			return 0;
	}

	pr_warn("wait_read() fail: 0x%02x\n", status);
	return -EIO;
}

/*
 * applesmc_read_key - reads len bytes from a given key, and put them in buffer.
 * Returns zero on success or a negative error on failure. Callers must
 * hold applesmc_lock.
 */
static int applesmc_read_key(const char* key, u8* buffer, u8 len)
{
	int i;

	if (len > APPLESMC_MAX_DATA_LENGTH) {
		printk(KERN_ERR	"applesmc_read_key: cannot read more than "
					"%d bytes\n", APPLESMC_MAX_DATA_LENGTH);
		return -EINVAL;
	}

	outb(APPLESMC_READ_CMD, APPLESMC_CMD_PORT);
	if (__wait_status(0x0c))
		return -EIO;

	for (i = 0; i < 4; i++) {
		outb(key[i], APPLESMC_DATA_PORT);
		if (__wait_status(0x04))
			return -EIO;
	}
	if (debug)
		printk(KERN_DEBUG "<%s", key);

	outb(len, APPLESMC_DATA_PORT);
	if (debug)
		printk(KERN_DEBUG ">%x", len);

	for (i = 0; i < len; i++) {
		if (__wait_status(0x05))
			return -EIO;
		buffer[i] = inb(APPLESMC_DATA_PORT);
		if (debug)
			printk(KERN_DEBUG "<%x", buffer[i]);
	}
	if (debug)
		printk(KERN_DEBUG "\n");

	return 0;
}

/*
 * applesmc_write_key - writes len bytes from buffer to a given key.
 * Returns zero on success or a negative error on failure. Callers must
 * hold applesmc_lock.
 */
static int applesmc_write_key(const char* key, u8* buffer, u8 len)
{
	int i;

	if (len > APPLESMC_MAX_DATA_LENGTH) {
		printk(KERN_ERR	"applesmc_write_key: cannot write more than "
					"%d bytes\n", APPLESMC_MAX_DATA_LENGTH);
		return -EINVAL;
	}

	outb(APPLESMC_WRITE_CMD, APPLESMC_CMD_PORT);
	if (__wait_status(0x0c))
		return -EIO;

	for (i = 0; i < 4; i++) {
		outb(key[i], APPLESMC_DATA_PORT);
		if (__wait_status(0x04))
			return -EIO;
	}

	outb(len, APPLESMC_DATA_PORT);

	for (i = 0; i < len; i++) {
		if (__wait_status(0x04))
			return -EIO;
		outb(buffer[i], APPLESMC_DATA_PORT);
	}

	return 0;
}

/*
 * applesmc_get_key_at_index - get key at index, and put the result in key
 * (char[6]). Returns zero on success or a negative error on failure. Callers
 * must hold applesmc_lock.
 */
static int applesmc_get_key_at_index(int index, char* key)
{
	int i;
	u8 readkey[4];
	readkey[0] = index >> 24;
	readkey[1] = index >> 16;
	readkey[2] = index >> 8;
	readkey[3] = index;

	outb(APPLESMC_GET_KEY_BY_INDEX_CMD, APPLESMC_CMD_PORT);
	if (__wait_status(0x0c))
		return -EIO;

	for (i = 0; i < 4; i++) {
		outb(readkey[i], APPLESMC_DATA_PORT);
		if (__wait_status(0x04))
			return -EIO;
	}

	outb(4, APPLESMC_DATA_PORT);

	for (i = 0; i < 4; i++) {
		if (__wait_status(0x05))
			return -EIO;
		key[i] = inb(APPLESMC_DATA_PORT);
	}
	key[4] = 0;
 * send_byte - Write to SMC port, retrying when necessary. Callers
 * must hold applesmc_lock.
 */
static int send_byte(u8 cmd, u16 port)
{
	u8 status;
	int us;

	outb(cmd, port);
	for (us = APPLESMC_MIN_WAIT; us < APPLESMC_MAX_WAIT; us <<= 1) {
		udelay(us);
		status = inb(APPLESMC_CMD_PORT);
		/* write: wait for smc to settle */
		if (status & 0x02)
			continue;
		/* ready: cmd accepted, return */
		if (status & 0x04)
			return 0;
		/* timeout: give up */
		if (us << 1 == APPLESMC_MAX_WAIT)
			break;
		/* busy: long wait and resend */
		udelay(APPLESMC_RETRY_WAIT);
		outb(cmd, port);
	}

	pr_warn("send_byte(0x%02x, 0x%04x) fail: 0x%02x\n", cmd, port, status);
	return -EIO;
}

static int send_command(u8 cmd)
{
	return send_byte(cmd, APPLESMC_CMD_PORT);
}

static int send_argument(const char *key)
{
	int i;

	for (i = 0; i < 4; i++)
		if (send_byte(key[i], APPLESMC_DATA_PORT))
			return -EIO;
	return 0;
}

static int read_smc(u8 cmd, const char *key, u8 *buffer, u8 len)
{
	u8 status, data = 0;
	int i;

	if (send_command(cmd) || send_argument(key)) {
		pr_warn("%.4s: read arg fail\n", key);
		return -EIO;
	}

	/* This has no effect on newer (2012) SMCs */
	if (send_byte(len, APPLESMC_DATA_PORT)) {
		pr_warn("%.4s: read len fail\n", key);
		return -EIO;
	}

	for (i = 0; i < len; i++) {
		if (wait_read()) {
			pr_warn("%.4s: read data[%d] fail\n", key, i);
			return -EIO;
		}
		buffer[i] = inb(APPLESMC_DATA_PORT);
	}

	/* Read the data port until bit0 is cleared */
	for (i = 0; i < 16; i++) {
		udelay(APPLESMC_MIN_WAIT);
		status = inb(APPLESMC_CMD_PORT);
		if (!(status & 0x01))
			break;
		data = inb(APPLESMC_DATA_PORT);
	}
	if (i)
		pr_warn("flushed %d bytes, last value is: %d\n", i, data);

	return 0;
}

/*
 * applesmc_get_key_type - get key type, and put the result in type (char[6]).
 * Returns zero on success or a negative error on failure. Callers must
 * hold applesmc_lock.
 */
static int applesmc_get_key_type(char* key, char* type)
{
	int i;

	outb(APPLESMC_GET_KEY_TYPE_CMD, APPLESMC_CMD_PORT);
	if (__wait_status(0x0c))
		return -EIO;

	for (i = 0; i < 4; i++) {
		outb(key[i], APPLESMC_DATA_PORT);
		if (__wait_status(0x04))
			return -EIO;
	}

	outb(5, APPLESMC_DATA_PORT);

	for (i = 0; i < 6; i++) {
		if (__wait_status(0x05))
			return -EIO;
		type[i] = inb(APPLESMC_DATA_PORT);
	}
	type[5] = 0;
static int write_smc(u8 cmd, const char *key, const u8 *buffer, u8 len)
{
	int i;

	if (send_command(cmd) || send_argument(key)) {
		pr_warn("%s: write arg fail\n", key);
		return -EIO;
	}

	if (send_byte(len, APPLESMC_DATA_PORT)) {
		pr_warn("%.4s: write len fail\n", key);
		return -EIO;
	}

	for (i = 0; i < len; i++) {
		if (send_byte(buffer[i], APPLESMC_DATA_PORT)) {
			pr_warn("%s: write data fail\n", key);
			return -EIO;
		}
	}

	return 0;
}

/*
 * applesmc_read_motion_sensor - Read motion sensor (X, Y or Z). Callers must
 * hold applesmc_lock.
 */
static int applesmc_read_motion_sensor(int index, s16* value)
{
	u8 buffer[2];
	int ret;

	switch (index) {
	case SENSOR_X:
		ret = applesmc_read_key(MOTION_SENSOR_X_KEY, buffer, 2);
		break;
	case SENSOR_Y:
		ret = applesmc_read_key(MOTION_SENSOR_Y_KEY, buffer, 2);
		break;
	case SENSOR_Z:
		ret = applesmc_read_key(MOTION_SENSOR_Z_KEY, buffer, 2);
		break;
	default:
		ret = -EINVAL;
	}

	*value = ((s16)buffer[0] << 8) | buffer[1];
static int read_register_count(unsigned int *count)
{
	__be32 be;
	int ret;

	ret = read_smc(APPLESMC_READ_CMD, KEY_COUNT_KEY, (u8 *)&be, 4);
	if (ret)
		return ret;

	*count = be32_to_cpu(be);
	return 0;
}

/*
 * Serialized I/O
 *
 * Returns zero on success or a negative error on failure.
 * All functions below are concurrency safe - callers should NOT hold lock.
 */

static int applesmc_read_entry(const struct applesmc_entry *entry,
			       u8 *buf, u8 len)
{
	int ret;

	if (entry->len != len)
		return -EINVAL;
	mutex_lock(&smcreg.mutex);
	ret = read_smc(APPLESMC_READ_CMD, entry->key, buf, len);
	mutex_unlock(&smcreg.mutex);

	return ret;
}

/*
 * applesmc_device_init - initialize the accelerometer.  Returns zero on success
 * and negative error code on failure.  Can sleep.
 */
static int applesmc_device_init(void)
{
	int total, ret = -ENXIO;
	u8 buffer[2];

	if (!applesmc_accelerometer)
		return 0;

	mutex_lock(&applesmc_lock);

	for (total = INIT_TIMEOUT_MSECS; total > 0; total -= INIT_WAIT_MSECS) {
		if (debug)
			printk(KERN_DEBUG "applesmc try %d\n", total);
		if (!applesmc_read_key(MOTION_SENSOR_KEY, buffer, 2) &&
				(buffer[0] != 0x00 || buffer[1] != 0x00)) {
			if (total == INIT_TIMEOUT_MSECS) {
				printk(KERN_DEBUG "applesmc: device has"
						" already been initialized"
						" (0x%02x, 0x%02x).\n",
						buffer[0], buffer[1]);
			} else {
				printk(KERN_DEBUG "applesmc: device"
						" successfully initialized"
						" (0x%02x, 0x%02x).\n",
						buffer[0], buffer[1]);
			}
			ret = 0;
			goto out;
		}
static int applesmc_write_entry(const struct applesmc_entry *entry,
				const u8 *buf, u8 len)
{
	int ret;

	if (entry->len != len)
		return -EINVAL;
	mutex_lock(&smcreg.mutex);
	ret = write_smc(APPLESMC_WRITE_CMD, entry->key, buf, len);
	mutex_unlock(&smcreg.mutex);
	return ret;
}

static const struct applesmc_entry *applesmc_get_entry_by_index(int index)
{
	struct applesmc_entry *cache = &smcreg.cache[index];
	u8 key[4], info[6];
	__be32 be;
	int ret = 0;

	if (cache->valid)
		return cache;

	mutex_lock(&smcreg.mutex);

	if (cache->valid)
		goto out;
	be = cpu_to_be32(index);
	ret = read_smc(APPLESMC_GET_KEY_BY_INDEX_CMD, (u8 *)&be, key, 4);
	if (ret)
		goto out;
	ret = read_smc(APPLESMC_GET_KEY_TYPE_CMD, key, info, 6);
	if (ret)
		goto out;

	memcpy(cache->key, key, 4);
	cache->len = info[0];
	memcpy(cache->type, &info[1], 4);
	cache->flags = info[5];
	cache->valid = 1;

out:
	mutex_unlock(&smcreg.mutex);
	if (ret)
		return ERR_PTR(ret);
	return cache;
}

static int applesmc_get_lower_bound(unsigned int *lo, const char *key)
{
	int begin = 0, end = smcreg.key_count;
	const struct applesmc_entry *entry;

	while (begin != end) {
		int middle = begin + (end - begin) / 2;
		entry = applesmc_get_entry_by_index(middle);
		if (IS_ERR(entry)) {
			*lo = 0;
			return PTR_ERR(entry);
		}
		if (strcmp(entry->key, key) < 0)
			begin = middle + 1;
		else
			end = middle;
	}

	*lo = begin;
	return 0;
}

static int applesmc_get_upper_bound(unsigned int *hi, const char *key)
{
	int begin = 0, end = smcreg.key_count;
	const struct applesmc_entry *entry;

	while (begin != end) {
		int middle = begin + (end - begin) / 2;
		entry = applesmc_get_entry_by_index(middle);
		if (IS_ERR(entry)) {
			*hi = smcreg.key_count;
			return PTR_ERR(entry);
		}
		if (strcmp(key, entry->key) < 0)
			end = middle;
		else
			begin = middle + 1;
	}

	*hi = begin;
	return 0;
}

static const struct applesmc_entry *applesmc_get_entry_by_key(const char *key)
{
	int begin, end;
	int ret;

	ret = applesmc_get_lower_bound(&begin, key);
	if (ret)
		return ERR_PTR(ret);
	ret = applesmc_get_upper_bound(&end, key);
	if (ret)
		return ERR_PTR(ret);
	if (end - begin != 1)
		return ERR_PTR(-EINVAL);

	return applesmc_get_entry_by_index(begin);
}

static int applesmc_read_key(const char *key, u8 *buffer, u8 len)
{
	const struct applesmc_entry *entry;

	entry = applesmc_get_entry_by_key(key);
	if (IS_ERR(entry))
		return PTR_ERR(entry);

	return applesmc_read_entry(entry, buffer, len);
}

static int applesmc_write_key(const char *key, const u8 *buffer, u8 len)
{
	const struct applesmc_entry *entry;

	entry = applesmc_get_entry_by_key(key);
	if (IS_ERR(entry))
		return PTR_ERR(entry);

	return applesmc_write_entry(entry, buffer, len);
}

static int applesmc_has_key(const char *key, bool *value)
{
	const struct applesmc_entry *entry;

	entry = applesmc_get_entry_by_key(key);
	if (IS_ERR(entry) && PTR_ERR(entry) != -EINVAL)
		return PTR_ERR(entry);

	*value = !IS_ERR(entry);
	return 0;
}

/*
 * applesmc_read_s16 - Read 16-bit signed big endian register
 */
static int applesmc_read_s16(const char *key, s16 *value)
{
	u8 buffer[2];
	int ret;

	ret = applesmc_read_key(key, buffer, 2);
	if (ret)
		return ret;

	*value = ((s16)buffer[0] << 8) | buffer[1];
	return 0;
}

/*
 * applesmc_device_init - initialize the accelerometer.  Can sleep.
 */
static void applesmc_device_init(void)
{
	int total;
	u8 buffer[2];

	if (!smcreg.has_accelerometer)
		return;

	for (total = INIT_TIMEOUT_MSECS; total > 0; total -= INIT_WAIT_MSECS) {
		if (!applesmc_read_key(MOTION_SENSOR_KEY, buffer, 2) &&
				(buffer[0] != 0x00 || buffer[1] != 0x00))
			return;
		buffer[0] = 0xe0;
		buffer[1] = 0x00;
		applesmc_write_key(MOTION_SENSOR_KEY, buffer, 2);
		msleep(INIT_WAIT_MSECS);
	}

	printk(KERN_WARNING "applesmc: failed to init the device\n");

out:
	mutex_unlock(&applesmc_lock);
	return ret;
}

/*
 * applesmc_get_fan_count - get the number of fans. Callers must NOT hold
 * applesmc_lock.
 */
static int applesmc_get_fan_count(void)
{
	int ret;
	u8 buffer[1];

	mutex_lock(&applesmc_lock);

	ret = applesmc_read_key(FANS_COUNT, buffer, 1);

	mutex_unlock(&applesmc_lock);
	if (ret)
		return ret;
	else
		return buffer[0];
	pr_warn("failed to init the device\n");
}

static int applesmc_init_index(struct applesmc_registers *s)
{
	const struct applesmc_entry *entry;
	unsigned int i;

	if (s->index)
		return 0;

	s->index = kcalloc(s->temp_count, sizeof(s->index[0]), GFP_KERNEL);
	if (!s->index)
		return -ENOMEM;

	for (i = s->temp_begin; i < s->temp_end; i++) {
		entry = applesmc_get_entry_by_index(i);
		if (IS_ERR(entry))
			continue;
		if (strcmp(entry->type, TEMP_SENSOR_TYPE))
			continue;
		s->index[s->index_count++] = entry->key;
	}

	return 0;
}

/*
 * applesmc_init_smcreg_try - Try to initialize register cache. Idempotent.
 */
static int applesmc_init_smcreg_try(void)
{
	struct applesmc_registers *s = &smcreg;
	bool left_light_sensor = 0, right_light_sensor = 0;
	unsigned int count;
	u8 tmp[1];
	int ret;

	if (s->init_complete)
		return 0;

	ret = read_register_count(&count);
	if (ret)
		return ret;

	if (s->cache && s->key_count != count) {
		pr_warn("key count changed from %d to %d\n",
			s->key_count, count);
		kfree(s->cache);
		s->cache = NULL;
	}
	s->key_count = count;

	if (!s->cache)
		s->cache = kcalloc(s->key_count, sizeof(*s->cache), GFP_KERNEL);
	if (!s->cache)
		return -ENOMEM;

	ret = applesmc_read_key(FANS_COUNT, tmp, 1);
	if (ret)
		return ret;
	s->fan_count = tmp[0];

	ret = applesmc_get_lower_bound(&s->temp_begin, "T");
	if (ret)
		return ret;
	ret = applesmc_get_lower_bound(&s->temp_end, "U");
	if (ret)
		return ret;
	s->temp_count = s->temp_end - s->temp_begin;

	ret = applesmc_init_index(s);
	if (ret)
		return ret;

	ret = applesmc_has_key(LIGHT_SENSOR_LEFT_KEY, &left_light_sensor);
	if (ret)
		return ret;
	ret = applesmc_has_key(LIGHT_SENSOR_RIGHT_KEY, &right_light_sensor);
	if (ret)
		return ret;
	ret = applesmc_has_key(MOTION_SENSOR_KEY, &s->has_accelerometer);
	if (ret)
		return ret;
	ret = applesmc_has_key(BACKLIGHT_KEY, &s->has_key_backlight);
	if (ret)
		return ret;

	s->num_light_sensors = left_light_sensor + right_light_sensor;
	s->init_complete = true;

	pr_info("key=%d fan=%d temp=%d index=%d acc=%d lux=%d kbd=%d\n",
	       s->key_count, s->fan_count, s->temp_count, s->index_count,
	       s->has_accelerometer,
	       s->num_light_sensors,
	       s->has_key_backlight);

	return 0;
}

static void applesmc_destroy_smcreg(void)
{
	kfree(smcreg.index);
	smcreg.index = NULL;
	kfree(smcreg.cache);
	smcreg.cache = NULL;
	smcreg.init_complete = false;
}

/*
 * applesmc_init_smcreg - Initialize register cache.
 *
 * Retries until initialization is successful, or the operation times out.
 *
 */
static int applesmc_init_smcreg(void)
{
	int ms, ret;

	for (ms = 0; ms < INIT_TIMEOUT_MSECS; ms += INIT_WAIT_MSECS) {
		ret = applesmc_init_smcreg_try();
		if (!ret) {
			if (ms)
				pr_info("init_smcreg() took %d ms\n", ms);
			return 0;
		}
		msleep(INIT_WAIT_MSECS);
	}

	applesmc_destroy_smcreg();

	return ret;
}

/* Device model stuff */
static int applesmc_probe(struct platform_device *dev)
{
	int ret;

	ret = applesmc_device_init();
	if (ret)
		return ret;

	printk(KERN_INFO "applesmc: device successfully initialized.\n");
	return 0;
}

static int applesmc_resume(struct platform_device *dev)
{
	return applesmc_device_init();
}

static struct platform_driver applesmc_driver = {
	.probe = applesmc_probe,
	.resume = applesmc_resume,
	.driver	= {
		.name = "applesmc",
		.owner = THIS_MODULE,
	ret = applesmc_init_smcreg();
	if (ret)
		return ret;

	applesmc_device_init();

	return 0;
}

/* Synchronize device with memorized backlight state */
static int applesmc_pm_resume(struct device *dev)
{
	if (smcreg.has_key_backlight)
		applesmc_write_key(BACKLIGHT_KEY, backlight_state, 2);
	return 0;
}

/* Reinitialize device on resume from hibernation */
static int applesmc_pm_restore(struct device *dev)
{
	applesmc_device_init();
	return applesmc_pm_resume(dev);
}

static const struct dev_pm_ops applesmc_pm_ops = {
	.resume = applesmc_pm_resume,
	.restore = applesmc_pm_restore,
};

static struct platform_driver applesmc_driver = {
	.probe = applesmc_probe,
	.driver	= {
		.name = "applesmc",
		.pm = &applesmc_pm_ops,
	},
};

/*
 * applesmc_calibrate - Set our "resting" values.  Callers must
 * hold applesmc_lock.
 */
static void applesmc_calibrate(void)
{
	applesmc_read_motion_sensor(SENSOR_X, &rest_x);
	applesmc_read_motion_sensor(SENSOR_Y, &rest_y);
	applesmc_read_s16(MOTION_SENSOR_X_KEY, &rest_x);
	applesmc_read_s16(MOTION_SENSOR_Y_KEY, &rest_y);
	rest_x = -rest_x;
}

static void applesmc_idev_poll(struct input_polled_dev *dev)
{
	struct input_dev *idev = dev->input;
	s16 x, y;

	mutex_lock(&applesmc_lock);

	if (applesmc_read_motion_sensor(SENSOR_X, &x))
		goto out;
	if (applesmc_read_motion_sensor(SENSOR_Y, &y))
		goto out;
	if (applesmc_read_s16(MOTION_SENSOR_X_KEY, &x))
		return;
	if (applesmc_read_s16(MOTION_SENSOR_Y_KEY, &y))
		return;

	x = -x;
	input_report_abs(idev, ABS_X, x - rest_x);
	input_report_abs(idev, ABS_Y, y - rest_y);
	input_sync(idev);

out:
	mutex_unlock(&applesmc_lock);
}

/* Sysfs Files */

static ssize_t applesmc_name_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "applesmc\n");
}

static ssize_t applesmc_position_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	int ret;
	s16 x, y, z;

	mutex_lock(&applesmc_lock);

	ret = applesmc_read_motion_sensor(SENSOR_X, &x);
	if (ret)
		goto out;
	ret = applesmc_read_motion_sensor(SENSOR_Y, &y);
	if (ret)
		goto out;
	ret = applesmc_read_motion_sensor(SENSOR_Z, &z);
	ret = applesmc_read_s16(MOTION_SENSOR_X_KEY, &x);
	if (ret)
		goto out;
	ret = applesmc_read_s16(MOTION_SENSOR_Y_KEY, &y);
	if (ret)
		goto out;
	ret = applesmc_read_s16(MOTION_SENSOR_Z_KEY, &z);
	if (ret)
		goto out;

out:
	mutex_unlock(&applesmc_lock);
	if (ret)
		return ret;
	else
		return snprintf(buf, PAGE_SIZE, "(%d,%d,%d)\n", x, y, z);
}

static ssize_t applesmc_light_show(struct device *dev,
				struct device_attribute *attr, char *sysfsbuf)
{
	int ret;
	u8 left = 0, right = 0;
	u8 buffer[6];

	mutex_lock(&applesmc_lock);

	ret = applesmc_read_key(LIGHT_SENSOR_LEFT_KEY, buffer, 6);
	left = buffer[2];
	if (ret)
		goto out;
	ret = applesmc_read_key(LIGHT_SENSOR_RIGHT_KEY, buffer, 6);
	right = buffer[2];

out:
	mutex_unlock(&applesmc_lock);
	const struct applesmc_entry *entry;
	static int data_length;
	int ret;
	u8 left = 0, right = 0;
	u8 buffer[10];

	if (!data_length) {
		entry = applesmc_get_entry_by_key(LIGHT_SENSOR_LEFT_KEY);
		if (IS_ERR(entry))
			return PTR_ERR(entry);
		if (entry->len > 10)
			return -ENXIO;
		data_length = entry->len;
		pr_info("light sensor data length set to %d\n", data_length);
	}

	ret = applesmc_read_key(LIGHT_SENSOR_LEFT_KEY, buffer, data_length);
	if (ret)
		goto out;
	/* newer macbooks report a single 10-bit bigendian value */
	if (data_length == 10) {
		left = be16_to_cpu(*(__be16 *)(buffer + 6)) >> 2;
		goto out;
	}
	left = buffer[2];

	ret = applesmc_read_key(LIGHT_SENSOR_RIGHT_KEY, buffer, data_length);
	if (ret)
		goto out;
	right = buffer[2];

out:
	if (ret)
		return ret;
	else
		return snprintf(sysfsbuf, PAGE_SIZE, "(%d,%d)\n", left, right);
}

/* Displays sensor key as label */
static ssize_t applesmc_show_sensor_label(struct device *dev,
			struct device_attribute *devattr, char *sysfsbuf)
{
	const char *key = smcreg.index[to_index(devattr)];

	return snprintf(sysfsbuf, PAGE_SIZE, "%s\n", key);
}

/* Displays degree Celsius * 1000 */
static ssize_t applesmc_show_temperature(struct device *dev,
			struct device_attribute *devattr, char *sysfsbuf)
{
	int ret;
	u8 buffer[2];
	unsigned int temp;
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	const char* key =
		temperature_sensors_sets[applesmc_temperature_set][attr->index];

	mutex_lock(&applesmc_lock);

	ret = applesmc_read_key(key, buffer, 2);
	temp = buffer[0]*1000;
	temp += (buffer[1] >> 6) * 250;

	mutex_unlock(&applesmc_lock);

	if (ret)
		return ret;
	else
		return snprintf(sysfsbuf, PAGE_SIZE, "%u\n", temp);
	const char *key = smcreg.index[to_index(devattr)];
	int ret;
	s16 value;
	int temp;

	ret = applesmc_read_s16(key, &value);
	if (ret)
		return ret;

	temp = 250 * (value >> 6);

	return snprintf(sysfsbuf, PAGE_SIZE, "%d\n", temp);
}

static ssize_t applesmc_show_fan_speed(struct device *dev,
				struct device_attribute *attr, char *sysfsbuf)
{
	int ret;
	unsigned int speed = 0;
	char newkey[5];
	u8 buffer[2];
	struct sensor_device_attribute_2 *sensor_attr =
						to_sensor_dev_attr_2(attr);

	newkey[0] = fan_speed_keys[sensor_attr->nr][0];
	newkey[1] = '0' + sensor_attr->index;
	newkey[2] = fan_speed_keys[sensor_attr->nr][2];
	newkey[3] = fan_speed_keys[sensor_attr->nr][3];
	newkey[4] = 0;

	mutex_lock(&applesmc_lock);

	sprintf(newkey, fan_speed_fmt[to_option(attr)], to_index(attr));

	ret = applesmc_read_key(newkey, buffer, 2);
	speed = ((buffer[0] << 8 | buffer[1]) >> 2);

	mutex_unlock(&applesmc_lock);
	if (ret)
		return ret;

	speed = ((buffer[0] << 8 | buffer[1]) >> 2);
	return snprintf(sysfsbuf, PAGE_SIZE, "%u\n", speed);
}

static ssize_t applesmc_store_fan_speed(struct device *dev,
					struct device_attribute *attr,
					const char *sysfsbuf, size_t count)
{
	int ret;
	u32 speed;
	char newkey[5];
	u8 buffer[2];
	struct sensor_device_attribute_2 *sensor_attr =
						to_sensor_dev_attr_2(attr);

	speed = simple_strtoul(sysfsbuf, NULL, 10);

	if (speed > 0x4000) /* Bigger than a 14-bit value */
		return -EINVAL;

	newkey[0] = fan_speed_keys[sensor_attr->nr][0];
	newkey[1] = '0' + sensor_attr->index;
	newkey[2] = fan_speed_keys[sensor_attr->nr][2];
	newkey[3] = fan_speed_keys[sensor_attr->nr][3];
	newkey[4] = 0;

	mutex_lock(&applesmc_lock);
	unsigned long speed;
	char newkey[5];
	u8 buffer[2];

	if (kstrtoul(sysfsbuf, 10, &speed) < 0 || speed >= 0x4000)
		return -EINVAL;		/* Bigger than a 14-bit value */

	sprintf(newkey, fan_speed_fmt[to_option(attr)], to_index(attr));

	buffer[0] = (speed >> 6) & 0xff;
	buffer[1] = (speed << 2) & 0xff;
	ret = applesmc_write_key(newkey, buffer, 2);

	mutex_unlock(&applesmc_lock);
	if (ret)
		return ret;
	else
		return count;
}

static ssize_t applesmc_show_fan_manual(struct device *dev,
			struct device_attribute *devattr, char *sysfsbuf)
			struct device_attribute *attr, char *sysfsbuf)
{
	int ret;
	u16 manual = 0;
	u8 buffer[2];
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);

	mutex_lock(&applesmc_lock);

	ret = applesmc_read_key(FANS_MANUAL, buffer, 2);
	manual = ((buffer[0] << 8 | buffer[1]) >> attr->index) & 0x01;

	mutex_unlock(&applesmc_lock);

	ret = applesmc_read_key(FANS_MANUAL, buffer, 2);
	if (ret)
		return ret;

	manual = ((buffer[0] << 8 | buffer[1]) >> to_index(attr)) & 0x01;
	return snprintf(sysfsbuf, PAGE_SIZE, "%d\n", manual);
}

static ssize_t applesmc_store_fan_manual(struct device *dev,
					 struct device_attribute *devattr,
					 struct device_attribute *attr,
					 const char *sysfsbuf, size_t count)
{
	int ret;
	u8 buffer[2];
	u32 input;
	u16 val;
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);

	input = simple_strtoul(sysfsbuf, NULL, 10);

	mutex_lock(&applesmc_lock);
	unsigned long input;
	u16 val;

	if (kstrtoul(sysfsbuf, 10, &input) < 0)
		return -EINVAL;

	ret = applesmc_read_key(FANS_MANUAL, buffer, 2);
	if (ret)
		goto out;

	val = (buffer[0] << 8 | buffer[1]);

	if (input)
		val = val | (0x01 << attr->index);
	else
		val = val & ~(0x01 << attr->index);
		val = val | (0x01 << to_index(attr));
	else
		val = val & ~(0x01 << to_index(attr));

	buffer[0] = (val >> 8) & 0xFF;
	buffer[1] = val & 0xFF;

	ret = applesmc_write_key(FANS_MANUAL, buffer, 2);

out:
	mutex_unlock(&applesmc_lock);
	if (ret)
		return ret;
	else
		return count;
}

static ssize_t applesmc_show_fan_position(struct device *dev,
				struct device_attribute *attr, char *sysfsbuf)
{
	int ret;
	char newkey[5];
	u8 buffer[17];
	struct sensor_device_attribute_2 *sensor_attr =
						to_sensor_dev_attr_2(attr);

	newkey[0] = FAN_POSITION[0];
	newkey[1] = '0' + sensor_attr->index;
	newkey[2] = FAN_POSITION[2];
	newkey[3] = FAN_POSITION[3];
	newkey[4] = 0;

	mutex_lock(&applesmc_lock);

	sprintf(newkey, FAN_ID_FMT, to_index(attr));

	ret = applesmc_read_key(newkey, buffer, 16);
	buffer[16] = 0;

	mutex_unlock(&applesmc_lock);
	if (ret)
		return ret;
	else
		return snprintf(sysfsbuf, PAGE_SIZE, "%s\n", buffer+4);
}

static ssize_t applesmc_calibrate_show(struct device *dev,
				struct device_attribute *attr, char *sysfsbuf)
{
	return snprintf(sysfsbuf, PAGE_SIZE, "(%d,%d)\n", rest_x, rest_y);
}

static ssize_t applesmc_calibrate_store(struct device *dev,
	struct device_attribute *attr, const char *sysfsbuf, size_t count)
{
	mutex_lock(&applesmc_lock);
	applesmc_calibrate();
	mutex_unlock(&applesmc_lock);
	applesmc_calibrate();

	return count;
}

/* Store the next backlight value to be written by the work */
static unsigned int backlight_value;

static void applesmc_backlight_set(struct work_struct *work)
{
	u8 buffer[2];

	mutex_lock(&applesmc_lock);
	buffer[0] = backlight_value;
	buffer[1] = 0x00;
	applesmc_write_key(BACKLIGHT_KEY, buffer, 2);
	mutex_unlock(&applesmc_lock);
static void applesmc_backlight_set(struct work_struct *work)
{
	applesmc_write_key(BACKLIGHT_KEY, backlight_state, 2);
}
static DECLARE_WORK(backlight_work, &applesmc_backlight_set);

static void applesmc_brightness_set(struct led_classdev *led_cdev,
						enum led_brightness value)
{
	int ret;

	backlight_value = value;
	ret = queue_work(applesmc_led_wq, &backlight_work);

	if (debug && (!ret))
		printk(KERN_DEBUG "applesmc: work was already on the queue.\n");
	backlight_state[0] = value;
	ret = queue_work(applesmc_led_wq, &backlight_work);

	if (debug && (!ret))
		dev_dbg(led_cdev->dev, "work was already on the queue.\n");
}

static ssize_t applesmc_key_count_show(struct device *dev,
				struct device_attribute *attr, char *sysfsbuf)
{
	int ret;
	u8 buffer[4];
	u32 count;

	mutex_lock(&applesmc_lock);

	ret = applesmc_read_key(KEY_COUNT_KEY, buffer, 4);
	count = ((u32)buffer[0]<<24) + ((u32)buffer[1]<<16) +
						((u32)buffer[2]<<8) + buffer[3];

	mutex_unlock(&applesmc_lock);
	if (ret)
		return ret;

	count = ((u32)buffer[0]<<24) + ((u32)buffer[1]<<16) +
						((u32)buffer[2]<<8) + buffer[3];
	return snprintf(sysfsbuf, PAGE_SIZE, "%d\n", count);
}

static ssize_t applesmc_key_at_index_read_show(struct device *dev,
				struct device_attribute *attr, char *sysfsbuf)
{
	char key[5];
	char info[6];
	int ret;

	mutex_lock(&applesmc_lock);

	ret = applesmc_get_key_at_index(key_at_index, key);

	if (ret || !key[0]) {
		mutex_unlock(&applesmc_lock);

		return -EINVAL;
	}

	ret = applesmc_get_key_type(key, info);

	if (ret) {
		mutex_unlock(&applesmc_lock);

		return ret;
	}

	/*
	 * info[0] maximum value (APPLESMC_MAX_DATA_LENGTH) is much lower than
	 * PAGE_SIZE, so we don't need any checks before writing to sysfsbuf.
	 */
	ret = applesmc_read_key(key, sysfsbuf, info[0]);

	mutex_unlock(&applesmc_lock);

	if (!ret) {
		return info[0];
	} else {
		return ret;
	}
	const struct applesmc_entry *entry;
	int ret;

	entry = applesmc_get_entry_by_index(key_at_index);
	if (IS_ERR(entry))
		return PTR_ERR(entry);
	ret = applesmc_read_entry(entry, sysfsbuf, entry->len);
	if (ret)
		return ret;

	return entry->len;
}

static ssize_t applesmc_key_at_index_data_length_show(struct device *dev,
				struct device_attribute *attr, char *sysfsbuf)
{
	char key[5];
	char info[6];
	int ret;

	mutex_lock(&applesmc_lock);

	ret = applesmc_get_key_at_index(key_at_index, key);

	if (ret || !key[0]) {
		mutex_unlock(&applesmc_lock);

		return -EINVAL;
	}

	ret = applesmc_get_key_type(key, info);

	mutex_unlock(&applesmc_lock);

	if (!ret)
		return snprintf(sysfsbuf, PAGE_SIZE, "%d\n", info[0]);
	else
		return ret;
	const struct applesmc_entry *entry;

	entry = applesmc_get_entry_by_index(key_at_index);
	if (IS_ERR(entry))
		return PTR_ERR(entry);

	return snprintf(sysfsbuf, PAGE_SIZE, "%d\n", entry->len);
}

static ssize_t applesmc_key_at_index_type_show(struct device *dev,
				struct device_attribute *attr, char *sysfsbuf)
{
	char key[5];
	char info[6];
	int ret;

	mutex_lock(&applesmc_lock);

	ret = applesmc_get_key_at_index(key_at_index, key);

	if (ret || !key[0]) {
		mutex_unlock(&applesmc_lock);

		return -EINVAL;
	}

	ret = applesmc_get_key_type(key, info);

	mutex_unlock(&applesmc_lock);

	if (!ret)
		return snprintf(sysfsbuf, PAGE_SIZE, "%s\n", info+1);
	else
		return ret;
	const struct applesmc_entry *entry;

	entry = applesmc_get_entry_by_index(key_at_index);
	if (IS_ERR(entry))
		return PTR_ERR(entry);

	return snprintf(sysfsbuf, PAGE_SIZE, "%s\n", entry->type);
}

static ssize_t applesmc_key_at_index_name_show(struct device *dev,
				struct device_attribute *attr, char *sysfsbuf)
{
	char key[5];
	int ret;

	mutex_lock(&applesmc_lock);

	ret = applesmc_get_key_at_index(key_at_index, key);

	mutex_unlock(&applesmc_lock);

	if (!ret && key[0])
		return snprintf(sysfsbuf, PAGE_SIZE, "%s\n", key);
	else
		return -EINVAL;
	const struct applesmc_entry *entry;

	entry = applesmc_get_entry_by_index(key_at_index);
	if (IS_ERR(entry))
		return PTR_ERR(entry);

	return snprintf(sysfsbuf, PAGE_SIZE, "%s\n", entry->key);
}

static ssize_t applesmc_key_at_index_show(struct device *dev,
				struct device_attribute *attr, char *sysfsbuf)
{
	return snprintf(sysfsbuf, PAGE_SIZE, "%d\n", key_at_index);
}

static ssize_t applesmc_key_at_index_store(struct device *dev,
	struct device_attribute *attr, const char *sysfsbuf, size_t count)
{
	mutex_lock(&applesmc_lock);

	key_at_index = simple_strtoul(sysfsbuf, NULL, 10);

	mutex_unlock(&applesmc_lock);

	unsigned long newkey;

	if (kstrtoul(sysfsbuf, 10, &newkey) < 0
	    || newkey >= smcreg.key_count)
		return -EINVAL;

	key_at_index = newkey;
	return count;
}

static struct led_classdev applesmc_backlight = {
	.name			= "smc::kbd_backlight",
	.default_trigger	= "nand-disk",
	.brightness_set		= applesmc_brightness_set,
};

static DEVICE_ATTR(name, 0444, applesmc_name_show, NULL);

static DEVICE_ATTR(position, 0444, applesmc_position_show, NULL);
static DEVICE_ATTR(calibrate, 0644,
			applesmc_calibrate_show, applesmc_calibrate_store);

static struct attribute *accelerometer_attributes[] = {
	&dev_attr_position.attr,
	&dev_attr_calibrate.attr,
	NULL
};

static const struct attribute_group accelerometer_attributes_group =
	{ .attrs = accelerometer_attributes };

static DEVICE_ATTR(light, 0444, applesmc_light_show, NULL);

static DEVICE_ATTR(key_count, 0444, applesmc_key_count_show, NULL);
static DEVICE_ATTR(key_at_index, 0644,
		applesmc_key_at_index_show, applesmc_key_at_index_store);
static DEVICE_ATTR(key_at_index_name, 0444,
					applesmc_key_at_index_name_show, NULL);
static DEVICE_ATTR(key_at_index_type, 0444,
					applesmc_key_at_index_type_show, NULL);
static DEVICE_ATTR(key_at_index_data_length, 0444,
				applesmc_key_at_index_data_length_show, NULL);
static DEVICE_ATTR(key_at_index_data, 0444,
				applesmc_key_at_index_read_show, NULL);

static struct attribute *key_enumeration_attributes[] = {
	&dev_attr_key_count.attr,
	&dev_attr_key_at_index.attr,
	&dev_attr_key_at_index_name.attr,
	&dev_attr_key_at_index_type.attr,
	&dev_attr_key_at_index_data_length.attr,
	&dev_attr_key_at_index_data.attr,
	NULL
};

static const struct attribute_group key_enumeration_group =
	{ .attrs = key_enumeration_attributes };

/*
 * Macro defining SENSOR_DEVICE_ATTR for a fan sysfs entries.
 *  - show actual speed
 *  - show/store minimum speed
 *  - show maximum speed
 *  - show safe speed
 *  - show/store target speed
 *  - show/store manual mode
 */
#define sysfs_fan_speeds_offset(offset) \
static SENSOR_DEVICE_ATTR_2(fan##offset##_input, S_IRUGO, \
			applesmc_show_fan_speed, NULL, 0, offset-1); \
\
static SENSOR_DEVICE_ATTR_2(fan##offset##_min, S_IRUGO | S_IWUSR, \
	applesmc_show_fan_speed, applesmc_store_fan_speed, 1, offset-1); \
\
static SENSOR_DEVICE_ATTR_2(fan##offset##_max, S_IRUGO, \
			applesmc_show_fan_speed, NULL, 2, offset-1); \
\
static SENSOR_DEVICE_ATTR_2(fan##offset##_safe, S_IRUGO, \
			applesmc_show_fan_speed, NULL, 3, offset-1); \
\
static SENSOR_DEVICE_ATTR_2(fan##offset##_output, S_IRUGO | S_IWUSR, \
	applesmc_show_fan_speed, applesmc_store_fan_speed, 4, offset-1); \
\
static SENSOR_DEVICE_ATTR(fan##offset##_manual, S_IRUGO | S_IWUSR, \
	applesmc_show_fan_manual, applesmc_store_fan_manual, offset-1); \
\
static SENSOR_DEVICE_ATTR(fan##offset##_label, S_IRUGO, \
	applesmc_show_fan_position, NULL, offset-1); \
\
static struct attribute *fan##offset##_attributes[] = { \
	&sensor_dev_attr_fan##offset##_input.dev_attr.attr, \
	&sensor_dev_attr_fan##offset##_min.dev_attr.attr, \
	&sensor_dev_attr_fan##offset##_max.dev_attr.attr, \
	&sensor_dev_attr_fan##offset##_safe.dev_attr.attr, \
	&sensor_dev_attr_fan##offset##_output.dev_attr.attr, \
	&sensor_dev_attr_fan##offset##_manual.dev_attr.attr, \
	&sensor_dev_attr_fan##offset##_label.dev_attr.attr, \
	NULL \
};

/*
 * Create the needed functions for each fan using the macro defined above
 * (4 fans are supported)
 */
sysfs_fan_speeds_offset(1);
sysfs_fan_speeds_offset(2);
sysfs_fan_speeds_offset(3);
sysfs_fan_speeds_offset(4);

static const struct attribute_group fan_attribute_groups[] = {
	{ .attrs = fan1_attributes },
	{ .attrs = fan2_attributes },
	{ .attrs = fan3_attributes },
	{ .attrs = fan4_attributes },
};

/*
 * Temperature sensors sysfs entries.
 */
static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO,
					applesmc_show_temperature, NULL, 0);
static SENSOR_DEVICE_ATTR(temp2_input, S_IRUGO,
					applesmc_show_temperature, NULL, 1);
static SENSOR_DEVICE_ATTR(temp3_input, S_IRUGO,
					applesmc_show_temperature, NULL, 2);
static SENSOR_DEVICE_ATTR(temp4_input, S_IRUGO,
					applesmc_show_temperature, NULL, 3);
static SENSOR_DEVICE_ATTR(temp5_input, S_IRUGO,
					applesmc_show_temperature, NULL, 4);
static SENSOR_DEVICE_ATTR(temp6_input, S_IRUGO,
					applesmc_show_temperature, NULL, 5);
static SENSOR_DEVICE_ATTR(temp7_input, S_IRUGO,
					applesmc_show_temperature, NULL, 6);
static SENSOR_DEVICE_ATTR(temp8_input, S_IRUGO,
					applesmc_show_temperature, NULL, 7);
static SENSOR_DEVICE_ATTR(temp9_input, S_IRUGO,
					applesmc_show_temperature, NULL, 8);
static SENSOR_DEVICE_ATTR(temp10_input, S_IRUGO,
					applesmc_show_temperature, NULL, 9);
static SENSOR_DEVICE_ATTR(temp11_input, S_IRUGO,
					applesmc_show_temperature, NULL, 10);
static SENSOR_DEVICE_ATTR(temp12_input, S_IRUGO,
					applesmc_show_temperature, NULL, 11);
static SENSOR_DEVICE_ATTR(temp13_input, S_IRUGO,
					applesmc_show_temperature, NULL, 12);
static SENSOR_DEVICE_ATTR(temp14_input, S_IRUGO,
					applesmc_show_temperature, NULL, 13);
static SENSOR_DEVICE_ATTR(temp15_input, S_IRUGO,
					applesmc_show_temperature, NULL, 14);
static SENSOR_DEVICE_ATTR(temp16_input, S_IRUGO,
					applesmc_show_temperature, NULL, 15);
static SENSOR_DEVICE_ATTR(temp17_input, S_IRUGO,
					applesmc_show_temperature, NULL, 16);
static SENSOR_DEVICE_ATTR(temp18_input, S_IRUGO,
					applesmc_show_temperature, NULL, 17);
static SENSOR_DEVICE_ATTR(temp19_input, S_IRUGO,
					applesmc_show_temperature, NULL, 18);
static SENSOR_DEVICE_ATTR(temp20_input, S_IRUGO,
					applesmc_show_temperature, NULL, 19);
static SENSOR_DEVICE_ATTR(temp21_input, S_IRUGO,
					applesmc_show_temperature, NULL, 20);
static SENSOR_DEVICE_ATTR(temp22_input, S_IRUGO,
					applesmc_show_temperature, NULL, 21);
static SENSOR_DEVICE_ATTR(temp23_input, S_IRUGO,
					applesmc_show_temperature, NULL, 22);
static SENSOR_DEVICE_ATTR(temp24_input, S_IRUGO,
					applesmc_show_temperature, NULL, 23);
static SENSOR_DEVICE_ATTR(temp25_input, S_IRUGO,
					applesmc_show_temperature, NULL, 24);
static SENSOR_DEVICE_ATTR(temp26_input, S_IRUGO,
					applesmc_show_temperature, NULL, 25);
static SENSOR_DEVICE_ATTR(temp27_input, S_IRUGO,
					applesmc_show_temperature, NULL, 26);
static SENSOR_DEVICE_ATTR(temp28_input, S_IRUGO,
					applesmc_show_temperature, NULL, 27);
static SENSOR_DEVICE_ATTR(temp29_input, S_IRUGO,
					applesmc_show_temperature, NULL, 28);
static SENSOR_DEVICE_ATTR(temp30_input, S_IRUGO,
					applesmc_show_temperature, NULL, 29);
static SENSOR_DEVICE_ATTR(temp31_input, S_IRUGO,
					applesmc_show_temperature, NULL, 30);
static SENSOR_DEVICE_ATTR(temp32_input, S_IRUGO,
					applesmc_show_temperature, NULL, 31);
static SENSOR_DEVICE_ATTR(temp33_input, S_IRUGO,
					applesmc_show_temperature, NULL, 32);
static SENSOR_DEVICE_ATTR(temp34_input, S_IRUGO,
					applesmc_show_temperature, NULL, 33);
static SENSOR_DEVICE_ATTR(temp35_input, S_IRUGO,
					applesmc_show_temperature, NULL, 34);

static struct attribute *temperature_attributes[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp3_input.dev_attr.attr,
	&sensor_dev_attr_temp4_input.dev_attr.attr,
	&sensor_dev_attr_temp5_input.dev_attr.attr,
	&sensor_dev_attr_temp6_input.dev_attr.attr,
	&sensor_dev_attr_temp7_input.dev_attr.attr,
	&sensor_dev_attr_temp8_input.dev_attr.attr,
	&sensor_dev_attr_temp9_input.dev_attr.attr,
	&sensor_dev_attr_temp10_input.dev_attr.attr,
	&sensor_dev_attr_temp11_input.dev_attr.attr,
	&sensor_dev_attr_temp12_input.dev_attr.attr,
	&sensor_dev_attr_temp13_input.dev_attr.attr,
	&sensor_dev_attr_temp14_input.dev_attr.attr,
	&sensor_dev_attr_temp15_input.dev_attr.attr,
	&sensor_dev_attr_temp16_input.dev_attr.attr,
	&sensor_dev_attr_temp17_input.dev_attr.attr,
	&sensor_dev_attr_temp18_input.dev_attr.attr,
	&sensor_dev_attr_temp19_input.dev_attr.attr,
	&sensor_dev_attr_temp20_input.dev_attr.attr,
	&sensor_dev_attr_temp21_input.dev_attr.attr,
	&sensor_dev_attr_temp22_input.dev_attr.attr,
	&sensor_dev_attr_temp23_input.dev_attr.attr,
	&sensor_dev_attr_temp24_input.dev_attr.attr,
	&sensor_dev_attr_temp25_input.dev_attr.attr,
	&sensor_dev_attr_temp26_input.dev_attr.attr,
	&sensor_dev_attr_temp27_input.dev_attr.attr,
	&sensor_dev_attr_temp28_input.dev_attr.attr,
	&sensor_dev_attr_temp29_input.dev_attr.attr,
	&sensor_dev_attr_temp30_input.dev_attr.attr,
	&sensor_dev_attr_temp31_input.dev_attr.attr,
	&sensor_dev_attr_temp32_input.dev_attr.attr,
	&sensor_dev_attr_temp33_input.dev_attr.attr,
	&sensor_dev_attr_temp34_input.dev_attr.attr,
	&sensor_dev_attr_temp35_input.dev_attr.attr,
	NULL
};

static const struct attribute_group temperature_attributes_group =
	{ .attrs = temperature_attributes };

/* Module stuff */

/*
 * applesmc_dmi_match - found a match.  return one, short-circuiting the hunt.
 */
static int applesmc_dmi_match(const struct dmi_system_id *id)
{
	int i = 0;
	struct dmi_match_data* dmi_data = id->driver_data;
	printk(KERN_INFO "applesmc: %s detected:\n", id->ident);
	applesmc_accelerometer = dmi_data->accelerometer;
	printk(KERN_INFO "applesmc:  - Model %s accelerometer\n",
				applesmc_accelerometer ? "with" : "without");
	applesmc_light = dmi_data->light;
	printk(KERN_INFO "applesmc:  - Model %s light sensors and backlight\n",
					applesmc_light ? "with" : "without");

	applesmc_temperature_set =  dmi_data->temperature_set;
	while (temperature_sensors_sets[applesmc_temperature_set][i] != NULL)
		i++;
	printk(KERN_INFO "applesmc:  - Model with %d temperature sensors\n", i);
	return 1;
}

/* Create accelerometer ressources */
static struct applesmc_node_group info_group[] = {
	{ "name", applesmc_name_show },
	{ "key_count", applesmc_key_count_show },
	{ "key_at_index", applesmc_key_at_index_show, applesmc_key_at_index_store },
	{ "key_at_index_name", applesmc_key_at_index_name_show },
	{ "key_at_index_type", applesmc_key_at_index_type_show },
	{ "key_at_index_data_length", applesmc_key_at_index_data_length_show },
	{ "key_at_index_data", applesmc_key_at_index_read_show },
	{ }
};

static struct applesmc_node_group accelerometer_group[] = {
	{ "position", applesmc_position_show },
	{ "calibrate", applesmc_calibrate_show, applesmc_calibrate_store },
	{ }
};

static struct applesmc_node_group light_sensor_group[] = {
	{ "light", applesmc_light_show },
	{ }
};

static struct applesmc_node_group fan_group[] = {
	{ "fan%d_label", applesmc_show_fan_position },
	{ "fan%d_input", applesmc_show_fan_speed, NULL, 0 },
	{ "fan%d_min", applesmc_show_fan_speed, applesmc_store_fan_speed, 1 },
	{ "fan%d_max", applesmc_show_fan_speed, NULL, 2 },
	{ "fan%d_safe", applesmc_show_fan_speed, NULL, 3 },
	{ "fan%d_output", applesmc_show_fan_speed, applesmc_store_fan_speed, 4 },
	{ "fan%d_manual", applesmc_show_fan_manual, applesmc_store_fan_manual },
	{ }
};

static struct applesmc_node_group temp_group[] = {
	{ "temp%d_label", applesmc_show_sensor_label },
	{ "temp%d_input", applesmc_show_temperature },
	{ }
};

/* Module stuff */

/*
 * applesmc_destroy_nodes - remove files and free associated memory
 */
static void applesmc_destroy_nodes(struct applesmc_node_group *groups)
{
	struct applesmc_node_group *grp;
	struct applesmc_dev_attr *node;

	for (grp = groups; grp->nodes; grp++) {
		for (node = grp->nodes; node->sda.dev_attr.attr.name; node++)
			sysfs_remove_file(&pdev->dev.kobj,
					  &node->sda.dev_attr.attr);
		kfree(grp->nodes);
		grp->nodes = NULL;
	}
}

/*
 * applesmc_create_nodes - create a two-dimensional group of sysfs files
 */
static int applesmc_create_nodes(struct applesmc_node_group *groups, int num)
{
	struct applesmc_node_group *grp;
	struct applesmc_dev_attr *node;
	struct attribute *attr;
	int ret, i;

	for (grp = groups; grp->format; grp++) {
		grp->nodes = kcalloc(num + 1, sizeof(*node), GFP_KERNEL);
		if (!grp->nodes) {
			ret = -ENOMEM;
			goto out;
		}
		for (i = 0; i < num; i++) {
			node = &grp->nodes[i];
			sprintf(node->name, grp->format, i + 1);
			node->sda.index = (grp->option << 16) | (i & 0xffff);
			node->sda.dev_attr.show = grp->show;
			node->sda.dev_attr.store = grp->store;
			attr = &node->sda.dev_attr.attr;
			sysfs_attr_init(attr);
			attr->name = node->name;
			attr->mode = S_IRUGO | (grp->store ? S_IWUSR : 0);
			ret = sysfs_create_file(&pdev->dev.kobj, attr);
			if (ret) {
				attr->name = NULL;
				goto out;
			}
		}
	}

	return 0;
out:
	applesmc_destroy_nodes(groups);
	return ret;
}

/* Create accelerometer resources */
static int applesmc_create_accelerometer(void)
{
	struct input_dev *idev;
	int ret;

	ret = sysfs_create_group(&pdev->dev.kobj,
					&accelerometer_attributes_group);
	if (!smcreg.has_accelerometer)
		return 0;

	ret = applesmc_create_nodes(accelerometer_group, 1);
	if (ret)
		goto out;

	applesmc_idev = input_allocate_polled_device();
	if (!applesmc_idev) {
		ret = -ENOMEM;
		goto out_sysfs;
	}

	applesmc_idev->poll = applesmc_idev_poll;
	applesmc_idev->poll_interval = APPLESMC_POLL_INTERVAL;

	/* initial calibrate for the input device */
	applesmc_calibrate();

	/* initialize the input device */
	idev = applesmc_idev->input;
	idev->name = "applesmc";
	idev->id.bustype = BUS_HOST;
	idev->dev.parent = &pdev->dev;
	idev->evbit[0] = BIT_MASK(EV_ABS);
	input_set_abs_params(idev, ABS_X,
			-256, 256, APPLESMC_INPUT_FUZZ, APPLESMC_INPUT_FLAT);
	input_set_abs_params(idev, ABS_Y,
			-256, 256, APPLESMC_INPUT_FUZZ, APPLESMC_INPUT_FLAT);

	ret = input_register_polled_device(applesmc_idev);
	if (ret)
		goto out_idev;

	return 0;

out_idev:
	input_free_polled_device(applesmc_idev);

out_sysfs:
	sysfs_remove_group(&pdev->dev.kobj, &accelerometer_attributes_group);

out:
	printk(KERN_WARNING "applesmc: driver init failed (ret=%d)!\n", ret);
	return ret;
}

/* Release all ressources used by the accelerometer */
static void applesmc_release_accelerometer(void)
{
	input_unregister_polled_device(applesmc_idev);
	input_free_polled_device(applesmc_idev);
	sysfs_remove_group(&pdev->dev.kobj, &accelerometer_attributes_group);
}

static __initdata struct dmi_match_data applesmc_dmi_data[] = {
/* MacBook Pro: accelerometer, backlight and temperature set 0 */
	{ .accelerometer = 1, .light = 1, .temperature_set = 0 },
/* MacBook2: accelerometer and temperature set 1 */
	{ .accelerometer = 1, .light = 0, .temperature_set = 1 },
/* MacBook: accelerometer and temperature set 2 */
	{ .accelerometer = 1, .light = 0, .temperature_set = 2 },
/* MacMini: temperature set 3 */
	{ .accelerometer = 0, .light = 0, .temperature_set = 3 },
/* MacPro: temperature set 4 */
	{ .accelerometer = 0, .light = 0, .temperature_set = 4 },
/* iMac: temperature set 5 */
	{ .accelerometer = 0, .light = 0, .temperature_set = 5 },
/* MacBook3: accelerometer and temperature set 6 */
	{ .accelerometer = 1, .light = 0, .temperature_set = 6 },
};

/* Note that DMI_MATCH(...,"MacBook") will match "MacBookPro1,1".
 * So we need to put "Apple MacBook Pro" before "Apple MacBook". */
static __initdata struct dmi_system_id applesmc_whitelist[] = {
	{ applesmc_dmi_match, "Apple MacBook Pro", {
	  DMI_MATCH(DMI_BOARD_VENDOR,"Apple"),
	  DMI_MATCH(DMI_PRODUCT_NAME,"MacBookPro") },
		(void*)&applesmc_dmi_data[0]},
	{ applesmc_dmi_match, "Apple MacBook (v2)", {
	  DMI_MATCH(DMI_BOARD_VENDOR,"Apple"),
	  DMI_MATCH(DMI_PRODUCT_NAME,"MacBook2") },
		(void*)&applesmc_dmi_data[1]},
	{ applesmc_dmi_match, "Apple MacBook (v3)", {
	  DMI_MATCH(DMI_BOARD_VENDOR,"Apple"),
	  DMI_MATCH(DMI_PRODUCT_NAME,"MacBook3") },
		(void*)&applesmc_dmi_data[6]},
	{ applesmc_dmi_match, "Apple MacBook", {
	  DMI_MATCH(DMI_BOARD_VENDOR,"Apple"),
	  DMI_MATCH(DMI_PRODUCT_NAME,"MacBook") },
		(void*)&applesmc_dmi_data[2]},
	{ applesmc_dmi_match, "Apple Macmini", {
	  DMI_MATCH(DMI_BOARD_VENDOR,"Apple"),
	  DMI_MATCH(DMI_PRODUCT_NAME,"Macmini") },
		(void*)&applesmc_dmi_data[3]},
	{ applesmc_dmi_match, "Apple MacPro2", {
	  DMI_MATCH(DMI_BOARD_VENDOR,"Apple"),
	  DMI_MATCH(DMI_PRODUCT_NAME,"MacPro2") },
		(void*)&applesmc_dmi_data[4]},
	{ applesmc_dmi_match, "Apple iMac", {
	  DMI_MATCH(DMI_BOARD_VENDOR,"Apple"),
	  DMI_MATCH(DMI_PRODUCT_NAME,"iMac") },
		(void*)&applesmc_dmi_data[5]},
	applesmc_destroy_nodes(accelerometer_group);

out:
	pr_warn("driver init failed (ret=%d)!\n", ret);
	return ret;
}

/* Release all resources used by the accelerometer */
static void applesmc_release_accelerometer(void)
{
	if (!smcreg.has_accelerometer)
		return;
	input_unregister_polled_device(applesmc_idev);
	input_free_polled_device(applesmc_idev);
	applesmc_destroy_nodes(accelerometer_group);
}

static int applesmc_create_light_sensor(void)
{
	if (!smcreg.num_light_sensors)
		return 0;
	return applesmc_create_nodes(light_sensor_group, 1);
}

static void applesmc_release_light_sensor(void)
{
	if (!smcreg.num_light_sensors)
		return;
	applesmc_destroy_nodes(light_sensor_group);
}

static int applesmc_create_key_backlight(void)
{
	if (!smcreg.has_key_backlight)
		return 0;
	applesmc_led_wq = create_singlethread_workqueue("applesmc-led");
	if (!applesmc_led_wq)
		return -ENOMEM;
	return led_classdev_register(&pdev->dev, &applesmc_backlight);
}

static void applesmc_release_key_backlight(void)
{
	if (!smcreg.has_key_backlight)
		return;
	led_classdev_unregister(&applesmc_backlight);
	destroy_workqueue(applesmc_led_wq);
}

static int applesmc_dmi_match(const struct dmi_system_id *id)
{
	return 1;
}

/*
 * Note that DMI_MATCH(...,"MacBook") will match "MacBookPro1,1".
 * So we need to put "Apple MacBook Pro" before "Apple MacBook".
 */
static __initdata struct dmi_system_id applesmc_whitelist[] = {
	{ applesmc_dmi_match, "Apple MacBook Air", {
	  DMI_MATCH(DMI_BOARD_VENDOR, "Apple"),
	  DMI_MATCH(DMI_PRODUCT_NAME, "MacBookAir") },
	},
	{ applesmc_dmi_match, "Apple MacBook Pro", {
	  DMI_MATCH(DMI_BOARD_VENDOR, "Apple"),
	  DMI_MATCH(DMI_PRODUCT_NAME, "MacBookPro") },
	},
	{ applesmc_dmi_match, "Apple MacBook", {
	  DMI_MATCH(DMI_BOARD_VENDOR, "Apple"),
	  DMI_MATCH(DMI_PRODUCT_NAME, "MacBook") },
	},
	{ applesmc_dmi_match, "Apple Macmini", {
	  DMI_MATCH(DMI_BOARD_VENDOR, "Apple"),
	  DMI_MATCH(DMI_PRODUCT_NAME, "Macmini") },
	},
	{ applesmc_dmi_match, "Apple MacPro", {
	  DMI_MATCH(DMI_BOARD_VENDOR, "Apple"),
	  DMI_MATCH(DMI_PRODUCT_NAME, "MacPro") },
	},
	{ applesmc_dmi_match, "Apple iMac", {
	  DMI_MATCH(DMI_BOARD_VENDOR, "Apple"),
	  DMI_MATCH(DMI_PRODUCT_NAME, "iMac") },
	},
	{ .ident = NULL }
};

static int __init applesmc_init(void)
{
	int ret;
	int count;
	int i;

	if (!dmi_check_system(applesmc_whitelist)) {
		printk(KERN_WARNING "applesmc: supported laptop not found!\n");

	if (!dmi_check_system(applesmc_whitelist)) {
		pr_warn("supported laptop not found!\n");
		ret = -ENODEV;
		goto out;
	}

	if (!request_region(APPLESMC_DATA_PORT, APPLESMC_NR_PORTS,
								"applesmc")) {
		ret = -ENXIO;
		goto out;
	}

	ret = platform_driver_register(&applesmc_driver);
	if (ret)
		goto out_region;

	pdev = platform_device_register_simple("applesmc", APPLESMC_DATA_PORT,
					       NULL, 0);
	if (IS_ERR(pdev)) {
		ret = PTR_ERR(pdev);
		goto out_driver;
	}

	ret = sysfs_create_file(&pdev->dev.kobj, &dev_attr_name.attr);
	if (ret)
		goto out_device;

	/* Create key enumeration sysfs files */
	ret = sysfs_create_group(&pdev->dev.kobj, &key_enumeration_group);
	if (ret)
		goto out_name;

	/* create fan files */
	count = applesmc_get_fan_count();
	if (count < 0) {
		printk(KERN_ERR "applesmc: Cannot get the number of fans.\n");
	} else {
		printk(KERN_INFO "applesmc: %d fans found.\n", count);

		switch (count) {
		default:
			printk(KERN_WARNING "applesmc: More than 4 fans found,"
					" but at most 4 fans are supported"
						" by the driver.\n");
		case 4:
			ret = sysfs_create_group(&pdev->dev.kobj,
						 &fan_attribute_groups[3]);
			if (ret)
				goto out_key_enumeration;
		case 3:
			ret = sysfs_create_group(&pdev->dev.kobj,
						 &fan_attribute_groups[2]);
			if (ret)
				goto out_key_enumeration;
		case 2:
			ret = sysfs_create_group(&pdev->dev.kobj,
						 &fan_attribute_groups[1]);
			if (ret)
				goto out_key_enumeration;
		case 1:
			ret = sysfs_create_group(&pdev->dev.kobj,
						 &fan_attribute_groups[0]);
			if (ret)
				goto out_fan_1;
		case 0:
			;
		}
	}

	for (i = 0;
	     temperature_sensors_sets[applesmc_temperature_set][i] != NULL;
	     i++) {
		if (temperature_attributes[i] == NULL) {
			printk(KERN_ERR "applesmc: More temperature sensors "
				"in temperature_sensors_sets (at least %i)"
				"than available sysfs files in "
				"temperature_attributes (%i), please report "
				"this bug.\n", i, i-1);
			goto out_temperature;
		}
		ret = sysfs_create_file(&pdev->dev.kobj,
						temperature_attributes[i]);
		if (ret)
			goto out_temperature;
	}

	if (applesmc_accelerometer) {
		ret = applesmc_create_accelerometer();
		if (ret)
			goto out_temperature;
	}

	if (applesmc_light) {
		/* Add light sensor file */
		ret = sysfs_create_file(&pdev->dev.kobj, &dev_attr_light.attr);
		if (ret)
			goto out_accelerometer;

		/* Create the workqueue */
		applesmc_led_wq = create_singlethread_workqueue("applesmc-led");
		if (!applesmc_led_wq) {
			ret = -ENOMEM;
			goto out_light_sysfs;
		}

		/* register as a led device */
		ret = led_classdev_register(&pdev->dev, &applesmc_backlight);
		if (ret < 0)
			goto out_light_wq;
	}
	/* create register cache */
	ret = applesmc_init_smcreg();
	if (ret)
		goto out_device;

	ret = applesmc_create_nodes(info_group, 1);
	if (ret)
		goto out_smcreg;

	ret = applesmc_create_nodes(fan_group, smcreg.fan_count);
	if (ret)
		goto out_info;

	ret = applesmc_create_nodes(temp_group, smcreg.index_count);
	if (ret)
		goto out_fans;

	ret = applesmc_create_accelerometer();
	if (ret)
		goto out_temperature;

	ret = applesmc_create_light_sensor();
	if (ret)
		goto out_accelerometer;

	ret = applesmc_create_key_backlight();
	if (ret)
		goto out_light_sysfs;

	hwmon_dev = hwmon_device_register(&pdev->dev);
	if (IS_ERR(hwmon_dev)) {
		ret = PTR_ERR(hwmon_dev);
		goto out_light_ledclass;
	}

	printk(KERN_INFO "applesmc: driver successfully loaded.\n");

	return 0;

out_light_ledclass:
	if (applesmc_light)
		led_classdev_unregister(&applesmc_backlight);
out_light_wq:
	if (applesmc_light)
		destroy_workqueue(applesmc_led_wq);
out_light_sysfs:
	if (applesmc_light)
		sysfs_remove_file(&pdev->dev.kobj, &dev_attr_light.attr);
out_accelerometer:
	if (applesmc_accelerometer)
		applesmc_release_accelerometer();
out_temperature:
	sysfs_remove_group(&pdev->dev.kobj, &temperature_attributes_group);
	sysfs_remove_group(&pdev->dev.kobj, &fan_attribute_groups[0]);
out_fan_1:
	sysfs_remove_group(&pdev->dev.kobj, &fan_attribute_groups[1]);
out_key_enumeration:
	sysfs_remove_group(&pdev->dev.kobj, &key_enumeration_group);
out_name:
	sysfs_remove_file(&pdev->dev.kobj, &dev_attr_name.attr);
	return 0;

out_light_ledclass:
	applesmc_release_key_backlight();
out_light_sysfs:
	applesmc_release_light_sensor();
out_accelerometer:
	applesmc_release_accelerometer();
out_temperature:
	applesmc_destroy_nodes(temp_group);
out_fans:
	applesmc_destroy_nodes(fan_group);
out_info:
	applesmc_destroy_nodes(info_group);
out_smcreg:
	applesmc_destroy_smcreg();
out_device:
	platform_device_unregister(pdev);
out_driver:
	platform_driver_unregister(&applesmc_driver);
out_region:
	release_region(APPLESMC_DATA_PORT, APPLESMC_NR_PORTS);
out:
	printk(KERN_WARNING "applesmc: driver init failed (ret=%d)!\n", ret);
	pr_warn("driver init failed (ret=%d)!\n", ret);
	return ret;
}

static void __exit applesmc_exit(void)
{
	hwmon_device_unregister(hwmon_dev);
	if (applesmc_light) {
		led_classdev_unregister(&applesmc_backlight);
		destroy_workqueue(applesmc_led_wq);
		sysfs_remove_file(&pdev->dev.kobj, &dev_attr_light.attr);
	}
	if (applesmc_accelerometer)
		applesmc_release_accelerometer();
	sysfs_remove_group(&pdev->dev.kobj, &temperature_attributes_group);
	sysfs_remove_group(&pdev->dev.kobj, &fan_attribute_groups[0]);
	sysfs_remove_group(&pdev->dev.kobj, &fan_attribute_groups[1]);
	sysfs_remove_group(&pdev->dev.kobj, &key_enumeration_group);
	sysfs_remove_file(&pdev->dev.kobj, &dev_attr_name.attr);
	platform_device_unregister(pdev);
	platform_driver_unregister(&applesmc_driver);
	release_region(APPLESMC_DATA_PORT, APPLESMC_NR_PORTS);

	printk(KERN_INFO "applesmc: driver unloaded.\n");
	applesmc_release_key_backlight();
	applesmc_release_light_sensor();
	applesmc_release_accelerometer();
	applesmc_destroy_nodes(temp_group);
	applesmc_destroy_nodes(fan_group);
	applesmc_destroy_nodes(info_group);
	applesmc_destroy_smcreg();
	platform_device_unregister(pdev);
	platform_driver_unregister(&applesmc_driver);
	release_region(APPLESMC_DATA_PORT, APPLESMC_NR_PORTS);
}

module_init(applesmc_init);
module_exit(applesmc_exit);

MODULE_AUTHOR("Nicolas Boichat");
MODULE_DESCRIPTION("Apple SMC");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(dmi, applesmc_whitelist);
