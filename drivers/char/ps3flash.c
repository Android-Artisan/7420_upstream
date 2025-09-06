/*
 * PS3 FLASH ROM Storage Driver
 *
 * Copyright (C) 2007 Sony Computer Entertainment Inc.
 * Copyright 2007 Sony Corp.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published
 * by the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/module.h>

#include <asm/lv1call.h>
#include <asm/ps3stor.h>


#define DEVICE_NAME		"ps3flash"

#define FLASH_BLOCK_SIZE	(256*1024)


struct ps3flash_private {
	struct mutex mutex;	/* Bounce buffer mutex */
	u64 chunk_sectors;
	int tag;		/* Start sector of buffer, -1 if invalid */
	bool dirty;
};

static struct ps3_storage_device *ps3flash_dev;

static ssize_t ps3flash_read_write_sectors(struct ps3_storage_device *dev,
					   u64 lpar, u64 start_sector,
					   u64 sectors, int write)
{
	u64 res = ps3stor_read_write_sectors(dev, lpar, start_sector, sectors,
					     write);
	if (res) {
		dev_err(&dev->sbd.core, "%s:%u: %s failed 0x%lx\n", __func__,
			__LINE__, write ? "write" : "read", res);
		return -EIO;
	}
	return sectors;
}

static ssize_t ps3flash_read_sectors(struct ps3_storage_device *dev,
				     u64 start_sector, u64 sectors,
				     unsigned int sector_offset)
{
	u64 max_sectors, lpar;

	max_sectors = dev->bounce_size / dev->blk_size;
	if (sectors > max_sectors) {
		dev_dbg(&dev->sbd.core, "%s:%u Limiting sectors to %lu\n",
			__func__, __LINE__, max_sectors);
		sectors = max_sectors;
	}

	lpar = dev->bounce_lpar + sector_offset * dev->blk_size;
	return ps3flash_read_write_sectors(dev, lpar, start_sector, sectors,
					   0);
}

static ssize_t ps3flash_write_chunk(struct ps3_storage_device *dev,
				    u64 start_sector)
{
       u64 sectors = dev->bounce_size / dev->blk_size;
       return ps3flash_read_write_sectors(dev, dev->bounce_lpar, start_sector,
					  sectors, 1);
static int ps3flash_read_write_sectors(struct ps3_storage_device *dev,
				       u64 start_sector, int write)
{
	struct ps3flash_private *priv = ps3_system_bus_get_drvdata(&dev->sbd);
	u64 res = ps3stor_read_write_sectors(dev, dev->bounce_lpar,
					     start_sector, priv->chunk_sectors,
					     write);
	if (res) {
		dev_err(&dev->sbd.core, "%s:%u: %s failed 0x%llx\n", __func__,
			__LINE__, write ? "write" : "read", res);
		return -EIO;
	}
	return 0;
}

static int ps3flash_writeback(struct ps3_storage_device *dev)
{
	struct ps3flash_private *priv = ps3_system_bus_get_drvdata(&dev->sbd);
	int res;

	if (!priv->dirty || priv->tag < 0)
		return 0;

	res = ps3flash_read_write_sectors(dev, priv->tag, 1);
	if (res)
		return res;

	priv->dirty = false;
	return 0;
}

static int ps3flash_fetch(struct ps3_storage_device *dev, u64 start_sector)
{
	struct ps3flash_private *priv = ps3_system_bus_get_drvdata(&dev->sbd);
	int res;

	if (start_sector == priv->tag)
		return 0;

	res = ps3flash_writeback(dev);
	if (res)
		return res;

	priv->tag = -1;

	res = ps3flash_read_write_sectors(dev, start_sector, 0);
	if (res)
		return res;

	priv->tag = start_sector;
	return 0;
}

static loff_t ps3flash_llseek(struct file *file, loff_t offset, int origin)
{
	struct ps3_storage_device *dev = ps3flash_dev;
	loff_t res;

	mutex_lock(&file->f_mapping->host->i_mutex);
	switch (origin) {
	case 1:
		offset += file->f_pos;
		break;
	case 2:
		offset += dev->regions[dev->region_idx].size*dev->blk_size;
		break;
	}
	if (offset < 0) {
		res = -EINVAL;
		goto out;
	}

	file->f_pos = offset;
	res = file->f_pos;

out:
	mutex_unlock(&file->f_mapping->host->i_mutex);
	return res;
}

static ssize_t ps3flash_read(struct file *file, char __user *buf, size_t count,
			     loff_t *pos)
{
	struct ps3_storage_device *dev = ps3flash_dev;
	struct ps3flash_private *priv = dev->sbd.core.driver_data;
	u64 size, start_sector, end_sector, offset;
	ssize_t sectors_read;
	size_t remaining, n;

	dev_dbg(&dev->sbd.core,
		"%s:%u: Reading %zu bytes at position %lld to user 0x%p\n",
		__func__, __LINE__, count, *pos, buf);
	return generic_file_llseek_size(file, offset, origin, MAX_LFS_FILESIZE,
			dev->regions[dev->region_idx].size*dev->blk_size);
}

static ssize_t ps3flash_read(char __user *userbuf, void *kernelbuf,
			     size_t count, loff_t *pos)
{
	struct ps3_storage_device *dev = ps3flash_dev;
	struct ps3flash_private *priv = ps3_system_bus_get_drvdata(&dev->sbd);
	u64 size, sector, offset;
	int res;
	size_t remaining, n;
	const void *src;

	dev_dbg(&dev->sbd.core,
		"%s:%u: Reading %zu bytes at position %lld to U0x%p/K0x%p\n",
		__func__, __LINE__, count, *pos, userbuf, kernelbuf);

	size = dev->regions[dev->region_idx].size*dev->blk_size;
	if (*pos >= size || !count)
		return 0;

	if (*pos + count > size) {
		dev_dbg(&dev->sbd.core,
			"%s:%u Truncating count from %zu to %llu\n", __func__,
			__LINE__, count, size - *pos);
		count = size - *pos;
	}

	start_sector = *pos / dev->blk_size;
	offset = *pos % dev->blk_size;
	end_sector = DIV_ROUND_UP(*pos + count, dev->blk_size);

	remaining = count;
	do {
		mutex_lock(&priv->mutex);

		sectors_read = ps3flash_read_sectors(dev, start_sector,
						     end_sector-start_sector,
						     0);
		if (sectors_read < 0) {
			mutex_unlock(&priv->mutex);
			goto fail;
		}

		n = min(remaining, sectors_read*dev->blk_size-offset);
		dev_dbg(&dev->sbd.core,
			"%s:%u: copy %lu bytes from 0x%p to user 0x%p\n",
			__func__, __LINE__, n, dev->bounce_buf+offset, buf);
		if (copy_to_user(buf, dev->bounce_buf+offset, n)) {
			mutex_unlock(&priv->mutex);
			sectors_read = -EFAULT;
			goto fail;
		}

		mutex_unlock(&priv->mutex);

		*pos += n;
		buf += n;
		remaining -= n;
		start_sector += sectors_read;
		offset = 0;
	} while (remaining > 0);

	return count;

fail:
	return sectors_read;
}

static ssize_t ps3flash_write(struct file *file, const char __user *buf,
			      size_t count, loff_t *pos)
{
	struct ps3_storage_device *dev = ps3flash_dev;
	struct ps3flash_private *priv = dev->sbd.core.driver_data;
	u64 size, chunk_sectors, start_write_sector, end_write_sector,
	    end_read_sector, start_read_sector, head, tail, offset;
	ssize_t res;
	size_t remaining, n;
	unsigned int sec_off;

	dev_dbg(&dev->sbd.core,
		"%s:%u: Writing %zu bytes at position %lld from user 0x%p\n",
		__func__, __LINE__, count, *pos, buf);

	size = dev->regions[dev->region_idx].size*dev->blk_size;
	if (*pos >= size || !count)
		return 0;

	if (*pos + count > size) {
		dev_dbg(&dev->sbd.core,
			"%s:%u Truncating count from %zu to %llu\n", __func__,
			__LINE__, count, size - *pos);
		count = size - *pos;
	}

	chunk_sectors = dev->bounce_size / dev->blk_size;

	start_write_sector = *pos / dev->bounce_size * chunk_sectors;
	offset = *pos % dev->bounce_size;
	end_write_sector = DIV_ROUND_UP(*pos + count, dev->bounce_size) *
			   chunk_sectors;

	end_read_sector = DIV_ROUND_UP(*pos, dev->blk_size);
	start_read_sector = (*pos + count) / dev->blk_size;

	/*
	 * As we have to write in 256 KiB chunks, while we can read in blk_size
	 * (usually 512 bytes) chunks, we perform the following steps:
	 *   1. Read from start_write_sector to end_read_sector ("head")
	 *   2. Read from start_read_sector to end_write_sector ("tail")
	 *   3. Copy data to buffer
	 *   4. Write from start_write_sector to end_write_sector
	 * All of this is complicated by using only one 256 KiB bounce buffer.
	 */

	head = end_read_sector - start_write_sector;
	tail = end_write_sector - start_read_sector;

	remaining = count;
	do {
		mutex_lock(&priv->mutex);

		if (end_read_sector >= start_read_sector) {
			/* Merge head and tail */
			dev_dbg(&dev->sbd.core,
				"Merged head and tail: %lu sectors at %lu\n",
				chunk_sectors, start_write_sector);
			res = ps3flash_read_sectors(dev, start_write_sector,
						    chunk_sectors, 0);
			if (res < 0)
				goto fail;
		} else {
			if (head) {
				/* Read head */
				dev_dbg(&dev->sbd.core,
					"head: %lu sectors at %lu\n", head,
					start_write_sector);
				res = ps3flash_read_sectors(dev,
							    start_write_sector,
							    head, 0);
				if (res < 0)
					goto fail;
			}
			if (start_read_sector <
			    start_write_sector+chunk_sectors) {
				/* Read tail */
				dev_dbg(&dev->sbd.core,
					"tail: %lu sectors at %lu\n", tail,
					start_read_sector);
				sec_off = start_read_sector-start_write_sector;
				res = ps3flash_read_sectors(dev,
							    start_read_sector,
							    tail, sec_off);
				if (res < 0)
					goto fail;
			}
		}

		n = min(remaining, dev->bounce_size-offset);
		dev_dbg(&dev->sbd.core,
			"%s:%u: copy %lu bytes from user 0x%p to 0x%p\n",
			__func__, __LINE__, n, buf, dev->bounce_buf+offset);
		if (copy_from_user(dev->bounce_buf+offset, buf, n)) {
			res = -EFAULT;
			goto fail;
		}

		res = ps3flash_write_chunk(dev, start_write_sector);
		if (res < 0)
			goto fail;
	sector = *pos / dev->bounce_size * priv->chunk_sectors;
	offset = *pos % dev->bounce_size;

	remaining = count;
	do {
		n = min_t(u64, remaining, dev->bounce_size - offset);
		src = dev->bounce_buf + offset;

		mutex_lock(&priv->mutex);

		res = ps3flash_fetch(dev, sector);
		if (res)
			goto fail;

		dev_dbg(&dev->sbd.core,
			"%s:%u: copy %lu bytes from 0x%p to U0x%p/K0x%p\n",
			__func__, __LINE__, n, src, userbuf, kernelbuf);
		if (userbuf) {
			if (copy_to_user(userbuf, src, n)) {
				res = -EFAULT;
				goto fail;
			}
			userbuf += n;
		}
		if (kernelbuf) {
			memcpy(kernelbuf, src, n);
			kernelbuf += n;
		}

		mutex_unlock(&priv->mutex);

		*pos += n;
		buf += n;
		remaining -= n;
		start_write_sector += chunk_sectors;
		head = 0;
		remaining -= n;
		sector += priv->chunk_sectors;
		offset = 0;
	} while (remaining > 0);

	return count;

fail:
	mutex_unlock(&priv->mutex);
	return res;
}

static ssize_t ps3flash_write(const char __user *userbuf,
			      const void *kernelbuf, size_t count, loff_t *pos)
{
	struct ps3_storage_device *dev = ps3flash_dev;
	struct ps3flash_private *priv = ps3_system_bus_get_drvdata(&dev->sbd);
	u64 size, sector, offset;
	int res = 0;
	size_t remaining, n;
	void *dst;

	dev_dbg(&dev->sbd.core,
		"%s:%u: Writing %zu bytes at position %lld from U0x%p/K0x%p\n",
		__func__, __LINE__, count, *pos, userbuf, kernelbuf);

	size = dev->regions[dev->region_idx].size*dev->blk_size;
	if (*pos >= size || !count)
		return 0;

	if (*pos + count > size) {
		dev_dbg(&dev->sbd.core,
			"%s:%u Truncating count from %zu to %llu\n", __func__,
			__LINE__, count, size - *pos);
		count = size - *pos;
	}

	sector = *pos / dev->bounce_size * priv->chunk_sectors;
	offset = *pos % dev->bounce_size;

	remaining = count;
	do {
		n = min_t(u64, remaining, dev->bounce_size - offset);
		dst = dev->bounce_buf + offset;

		mutex_lock(&priv->mutex);

		if (n != dev->bounce_size)
			res = ps3flash_fetch(dev, sector);
		else if (sector != priv->tag)
			res = ps3flash_writeback(dev);
		if (res)
			goto fail;

		dev_dbg(&dev->sbd.core,
			"%s:%u: copy %lu bytes from U0x%p/K0x%p to 0x%p\n",
			__func__, __LINE__, n, userbuf, kernelbuf, dst);
		if (userbuf) {
			if (copy_from_user(dst, userbuf, n)) {
				res = -EFAULT;
				goto fail;
			}
			userbuf += n;
		}
		if (kernelbuf) {
			memcpy(dst, kernelbuf, n);
			kernelbuf += n;
		}

		priv->tag = sector;
		priv->dirty = true;

		mutex_unlock(&priv->mutex);

		*pos += n;
		remaining -= n;
		sector += priv->chunk_sectors;
		offset = 0;
	} while (remaining > 0);

	return count;

fail:
	mutex_unlock(&priv->mutex);
	return res;
}

static ssize_t ps3flash_user_read(struct file *file, char __user *buf,
				  size_t count, loff_t *pos)
{
	return ps3flash_read(buf, NULL, count, pos);
}

static ssize_t ps3flash_user_write(struct file *file, const char __user *buf,
				   size_t count, loff_t *pos)
{
	return ps3flash_write(buf, NULL, count, pos);
}

static ssize_t ps3flash_kernel_read(void *buf, size_t count, loff_t pos)
{
	return ps3flash_read(NULL, buf, count, &pos);
}

static ssize_t ps3flash_kernel_write(const void *buf, size_t count,
				     loff_t pos)
{
	ssize_t res;
	int wb;

	res = ps3flash_write(NULL, buf, count, &pos);
	if (res < 0)
		return res;

	/* Make kernel writes synchronous */
	wb = ps3flash_writeback(ps3flash_dev);
	if (wb)
		return wb;

	return res;
}

static int ps3flash_flush(struct file *file, fl_owner_t id)
{
	return ps3flash_writeback(ps3flash_dev);
}

static int ps3flash_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	struct inode *inode = file_inode(file);
	int err;
	mutex_lock(&inode->i_mutex);
	err = ps3flash_writeback(ps3flash_dev);
	mutex_unlock(&inode->i_mutex);
	return err;
}

static irqreturn_t ps3flash_interrupt(int irq, void *data)
{
	struct ps3_storage_device *dev = data;
	int res;
	u64 tag, status;

	res = lv1_storage_get_async_status(dev->sbd.dev_id, &tag, &status);

	if (tag != dev->tag)
		dev_err(&dev->sbd.core,
			"%s:%u: tag mismatch, got %lx, expected %lx\n",
			__func__, __LINE__, tag, dev->tag);

	if (res) {
		dev_err(&dev->sbd.core, "%s:%u: res=%d status=0x%lx\n",
			"%s:%u: tag mismatch, got %llx, expected %llx\n",
			__func__, __LINE__, tag, dev->tag);

	if (res) {
		dev_err(&dev->sbd.core, "%s:%u: res=%d status=0x%llx\n",
			__func__, __LINE__, res, status);
	} else {
		dev->lv1_status = status;
		complete(&dev->done);
	}
	return IRQ_HANDLED;
}


static const struct file_operations ps3flash_fops = {
	.owner	= THIS_MODULE,
	.llseek	= ps3flash_llseek,
	.read	= ps3flash_read,
	.write	= ps3flash_write,
static const struct file_operations ps3flash_fops = {
	.owner	= THIS_MODULE,
	.llseek	= ps3flash_llseek,
	.read	= ps3flash_user_read,
	.write	= ps3flash_user_write,
	.flush	= ps3flash_flush,
	.fsync	= ps3flash_fsync,
};

static const struct ps3_os_area_flash_ops ps3flash_kernel_ops = {
	.read	= ps3flash_kernel_read,
	.write	= ps3flash_kernel_write,
};

static struct miscdevice ps3flash_misc = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= DEVICE_NAME,
	.fops	= &ps3flash_fops,
};

static int __devinit ps3flash_probe(struct ps3_system_bus_device *_dev)
static int ps3flash_probe(struct ps3_system_bus_device *_dev)
{
	struct ps3_storage_device *dev = to_ps3_storage_device(&_dev->core);
	struct ps3flash_private *priv;
	int error;
	unsigned long tmp;

	tmp = dev->regions[dev->region_idx].start*dev->blk_size;
	if (tmp % FLASH_BLOCK_SIZE) {
		dev_err(&dev->sbd.core,
			"%s:%u region start %lu is not aligned\n", __func__,
			__LINE__, tmp);
		return -EINVAL;
	}
	tmp = dev->regions[dev->region_idx].size*dev->blk_size;
	if (tmp % FLASH_BLOCK_SIZE) {
		dev_err(&dev->sbd.core,
			"%s:%u region size %lu is not aligned\n", __func__,
			__LINE__, tmp);
		return -EINVAL;
	}

	/* use static buffer, kmalloc cannot allocate 256 KiB */
	if (!ps3flash_bounce_buffer.address)
		return -ENODEV;

	if (ps3flash_dev) {
		dev_err(&dev->sbd.core,
			"Only one FLASH device is supported\n");
		return -EBUSY;
	}

	ps3flash_dev = dev;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		error = -ENOMEM;
		goto fail;
	}

	dev->sbd.core.driver_data = priv;
	mutex_init(&priv->mutex);

	dev->bounce_size = ps3flash_bounce_buffer.size;
	dev->bounce_buf = ps3flash_bounce_buffer.address;
	ps3_system_bus_set_drvdata(&dev->sbd, priv);
	mutex_init(&priv->mutex);
	priv->tag = -1;

	dev->bounce_size = ps3flash_bounce_buffer.size;
	dev->bounce_buf = ps3flash_bounce_buffer.address;
	priv->chunk_sectors = dev->bounce_size / dev->blk_size;

	error = ps3stor_setup(dev, ps3flash_interrupt);
	if (error)
		goto fail_free_priv;

	ps3flash_misc.parent = &dev->sbd.core;
	error = misc_register(&ps3flash_misc);
	if (error) {
		dev_err(&dev->sbd.core, "%s:%u: misc_register failed %d\n",
			__func__, __LINE__, error);
		goto fail_teardown;
	}

	dev_info(&dev->sbd.core, "%s:%u: registered misc device %d\n",
		 __func__, __LINE__, ps3flash_misc.minor);

	ps3_os_area_flash_register(&ps3flash_kernel_ops);
	return 0;

fail_teardown:
	ps3stor_teardown(dev);
fail_free_priv:
	kfree(priv);
	dev->sbd.core.driver_data = NULL;
	ps3_system_bus_set_drvdata(&dev->sbd, NULL);
fail:
	ps3flash_dev = NULL;
	return error;
}

static int ps3flash_remove(struct ps3_system_bus_device *_dev)
{
	struct ps3_storage_device *dev = to_ps3_storage_device(&_dev->core);

	misc_deregister(&ps3flash_misc);
	ps3stor_teardown(dev);
	kfree(dev->sbd.core.driver_data);
	dev->sbd.core.driver_data = NULL;
	ps3_os_area_flash_register(NULL);
	misc_deregister(&ps3flash_misc);
	ps3stor_teardown(dev);
	kfree(ps3_system_bus_get_drvdata(&dev->sbd));
	ps3_system_bus_set_drvdata(&dev->sbd, NULL);
	ps3flash_dev = NULL;
	return 0;
}


static struct ps3_system_bus_driver ps3flash = {
	.match_id	= PS3_MATCH_ID_STOR_FLASH,
	.core.name	= DEVICE_NAME,
	.core.owner	= THIS_MODULE,
	.probe		= ps3flash_probe,
	.remove		= ps3flash_remove,
	.shutdown	= ps3flash_remove,
};


static int __init ps3flash_init(void)
{
	return ps3_system_bus_driver_register(&ps3flash);
}

static void __exit ps3flash_exit(void)
{
	ps3_system_bus_driver_unregister(&ps3flash);
}

module_init(ps3flash_init);
module_exit(ps3flash_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PS3 FLASH ROM Storage Driver");
MODULE_AUTHOR("Sony Corporation");
MODULE_ALIAS(PS3_MODULE_ALIAS_STOR_FLASH);
