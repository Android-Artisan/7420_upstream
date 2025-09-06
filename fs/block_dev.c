/*
 *  linux/fs/block_dev.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 2001  Andrea Arcangeli <andrea@suse.de> SuSE
 */

#include <linux/init.h>
#include <linux/mm.h>
#include <linux/fcntl.h>
#include <linux/slab.h>
#include <linux/kmod.h>
#include <linux/major.h>
#include <linux/smp_lock.h>
#include <linux/device_cgroup.h>
#include <linux/highmem.h>
#include <linux/blkdev.h>
#include <linux/module.h>
#include <linux/blkpg.h>
#include <linux/buffer_head.h>
#include <linux/device_cgroup.h>
#include <linux/highmem.h>
#include <linux/blkdev.h>
#include <linux/backing-dev.h>
#include <linux/module.h>
#include <linux/blkpg.h>
#include <linux/magic.h>
#include <linux/buffer_head.h>
#include <linux/swap.h>
#include <linux/pagevec.h>
#include <linux/writeback.h>
#include <linux/mpage.h>
#include <linux/mount.h>
#include <linux/uio.h>
#include <linux/namei.h>
#include <linux/log2.h>
#include <linux/cleancache.h>
#include <linux/dax.h>
#include <asm/uaccess.h>
#include "internal.h"

struct bdev_inode {
	struct block_device bdev;
	struct inode vfs_inode;
};

static const struct address_space_operations def_blk_aops;

static inline struct bdev_inode *BDEV_I(struct inode *inode)
{
	return container_of(inode, struct bdev_inode, vfs_inode);
}

inline struct block_device *I_BDEV(struct inode *inode)
{
	return &BDEV_I(inode)->bdev;
}

EXPORT_SYMBOL(I_BDEV);

static sector_t max_block(struct block_device *bdev)
{
	sector_t retval = ~((sector_t)0);
	loff_t sz = i_size_read(bdev->bd_inode);

	if (sz) {
		unsigned int size = block_size(bdev);
		unsigned int sizebits = blksize_bits(size);
		retval = (sz >> sizebits);
	}
	return retval;
}

/* Kill _all_ buffers and pagecache , dirty or not.. */
static void kill_bdev(struct block_device *bdev)
{
	if (bdev->bd_inode->i_mapping->nrpages == 0)
		return;
	invalidate_bh_lrus();
	truncate_inode_pages(bdev->bd_inode->i_mapping, 0);
}	
struct block_device *I_BDEV(struct inode *inode)
{
	return &BDEV_I(inode)->bdev;
}
EXPORT_SYMBOL(I_BDEV);

static void bdev_write_inode(struct block_device *bdev)
{
	struct inode *inode = bdev->bd_inode;
	int ret;

	spin_lock(&inode->i_lock);
	while (inode->i_state & I_DIRTY) {
		spin_unlock(&inode->i_lock);
		ret = write_inode_now(inode, true);
		if (ret) {
			char name[BDEVNAME_SIZE];
			pr_warn_ratelimited("VFS: Dirty inode writeback failed "
					    "for block device %s (err=%d).\n",
					    bdevname(bdev, name), ret);
		}
		spin_lock(&inode->i_lock);
	}
	spin_unlock(&inode->i_lock);
}

/* Kill _all_ buffers and pagecache , dirty or not.. */
void kill_bdev(struct block_device *bdev)
{
	struct address_space *mapping = bdev->bd_inode->i_mapping;

	if (mapping->nrpages == 0 && mapping->nrshadows == 0)
		return;

	invalidate_bh_lrus();
	truncate_inode_pages(mapping, 0);
}	
EXPORT_SYMBOL(kill_bdev);

/* Invalidate clean unused buffers and pagecache. */
void invalidate_bdev(struct block_device *bdev)
{
	struct address_space *mapping = bdev->bd_inode->i_mapping;

	if (mapping->nrpages) {
		invalidate_bh_lrus();
		lru_add_drain_all();	/* make sure all lru add caches are flushed */
		invalidate_mapping_pages(mapping, 0, -1);
	}
	/* 99% of the time, we don't need to flush the cleancache on the bdev.
	 * But, for the strange corners, lets be cautious
	 */
	cleancache_invalidate_inode(mapping);
}
EXPORT_SYMBOL(invalidate_bdev);

int set_blocksize(struct block_device *bdev, int size)
{
	/* Size must be a power of two, and between 512 and PAGE_SIZE */
	if (size > PAGE_SIZE || size < 512 || !is_power_of_2(size))
		return -EINVAL;

	/* Size cannot be smaller than the size supported by the device */
	if (size < bdev_hardsect_size(bdev))
	if (size < bdev_logical_block_size(bdev))
		return -EINVAL;

	/* Don't change the size if it is same as current */
	if (bdev->bd_block_size != size) {
		sync_blockdev(bdev);
		bdev->bd_block_size = size;
		bdev->bd_inode->i_blkbits = blksize_bits(size);
		kill_bdev(bdev);
	}
	return 0;
}

EXPORT_SYMBOL(set_blocksize);

int sb_set_blocksize(struct super_block *sb, int size)
{
	if (set_blocksize(sb->s_bdev, size))
		return 0;
	/* If we get here, we know size is power of two
	 * and it's value is between 512 and PAGE_SIZE */
	sb->s_blocksize = size;
	sb->s_blocksize_bits = blksize_bits(size);
	return sb->s_blocksize;
}

EXPORT_SYMBOL(sb_set_blocksize);

int sb_min_blocksize(struct super_block *sb, int size)
{
	int minsize = bdev_hardsect_size(sb->s_bdev);
	int minsize = bdev_logical_block_size(sb->s_bdev);
	if (size < minsize)
		size = minsize;
	return sb_set_blocksize(sb, size);
}

EXPORT_SYMBOL(sb_min_blocksize);

static int
blkdev_get_block(struct inode *inode, sector_t iblock,
		struct buffer_head *bh, int create)
{
	if (iblock >= max_block(I_BDEV(inode))) {
		if (create)
			return -EIO;

		/*
		 * for reads, we're just trying to fill a partial page.
		 * return a hole, they will have to call get_block again
		 * before they can fill it, and they will get -EIO at that
		 * time
		 */
		return 0;
	}
	bh->b_bdev = I_BDEV(inode);
	bh->b_blocknr = iblock;
	set_buffer_mapped(bh);
	return 0;
}

static int
blkdev_get_blocks(struct inode *inode, sector_t iblock,
		struct buffer_head *bh, int create)
{
	sector_t end_block = max_block(I_BDEV(inode));
	unsigned long max_blocks = bh->b_size >> inode->i_blkbits;

	if ((iblock + max_blocks) > end_block) {
		max_blocks = end_block - iblock;
		if ((long)max_blocks <= 0) {
			if (create)
				return -EIO;	/* write fully beyond EOF */
			/*
			 * It is a read which is fully beyond EOF.  We return
			 * a !buffer_mapped buffer
			 */
			max_blocks = 0;
		}
	}

	bh->b_bdev = I_BDEV(inode);
	bh->b_blocknr = iblock;
	bh->b_size = max_blocks << inode->i_blkbits;
	if (max_blocks)
		set_buffer_mapped(bh);
	return 0;
}

static ssize_t
blkdev_direct_IO(int rw, struct kiocb *iocb, const struct iovec *iov,
			loff_t offset, unsigned long nr_segs)
static ssize_t
blkdev_direct_IO(struct kiocb *iocb, struct iov_iter *iter, loff_t offset)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_mapping->host;

	return blockdev_direct_IO_no_locking(rw, iocb, inode, I_BDEV(inode),
				iov, offset, nr_segs, blkdev_get_blocks, NULL);
}

	if (IS_DAX(inode))
		return dax_do_io(iocb, inode, iter, offset, blkdev_get_block,
				NULL, DIO_SKIP_DIO_COUNT);
	return __blockdev_direct_IO(iocb, inode, I_BDEV(inode), iter, offset,
				    blkdev_get_block, NULL, NULL,
				    DIO_SKIP_DIO_COUNT);
}

int __sync_blockdev(struct block_device *bdev, int wait)
{
	if (!bdev)
		return 0;
	if (!wait)
		return filemap_flush(bdev->bd_inode->i_mapping);
	return filemap_write_and_wait(bdev->bd_inode->i_mapping);
}

/*
 * Write out and wait upon all the dirty data associated with a block
 * device via its mapping.  Does not take the superblock lock.
 */
int sync_blockdev(struct block_device *bdev)
{
	return __sync_blockdev(bdev, 1);
}
EXPORT_SYMBOL(sync_blockdev);

/*
 * Write out and wait upon all dirty data associated with this
 * device.   Filesystem data as well as the underlying block
 * device.  Takes the superblock lock.
 */
int fsync_bdev(struct block_device *bdev)
{
	struct super_block *sb = get_super(bdev);
	if (sb) {
		int res = sync_filesystem(sb);
		drop_super(sb);
		return res;
	}
	return sync_blockdev(bdev);
}
EXPORT_SYMBOL(fsync_bdev);

/**
 * freeze_bdev  --  lock a filesystem and force it into a consistent state
 * @bdev:	blockdevice to lock
 *
 * If a superblock is found on this device, we take the s_umount semaphore
 * on it to make sure nobody unmounts until the snapshot creation is done.
 * The reference counter (bd_fsfreeze_count) guarantees that only the last
 * unfreeze process can unfreeze the frozen filesystem actually when multiple
 * freeze requests arrive simultaneously. It counts up in freeze_bdev() and
 * count down in thaw_bdev(). When it becomes 0, thaw_bdev() will unfreeze
 * actually.
 */
struct super_block *freeze_bdev(struct block_device *bdev)
{
	struct super_block *sb;
	int error = 0;

	mutex_lock(&bdev->bd_fsfreeze_mutex);
	if (++bdev->bd_fsfreeze_count > 1) {
		/*
		 * We don't even need to grab a reference - the first call
		 * to freeze_bdev grab an active reference and only the last
		 * thaw_bdev drops it.
		 */
		sb = get_super(bdev);
		drop_super(sb);
		mutex_unlock(&bdev->bd_fsfreeze_mutex);
		return sb;
	}

	sb = get_active_super(bdev);
	if (!sb)
		goto out;
	if (sb->s_op->freeze_super)
		error = sb->s_op->freeze_super(sb);
	else
		error = freeze_super(sb);
	if (error) {
		deactivate_super(sb);
		bdev->bd_fsfreeze_count--;
		mutex_unlock(&bdev->bd_fsfreeze_mutex);
		return ERR_PTR(error);
	}
	deactivate_super(sb);
 out:
	sync_blockdev(bdev);
	mutex_unlock(&bdev->bd_fsfreeze_mutex);
	return sb;	/* thaw_bdev releases s->s_umount */
}
EXPORT_SYMBOL(freeze_bdev);

/**
 * thaw_bdev  -- unlock filesystem
 * @bdev:	blockdevice to unlock
 * @sb:		associated superblock
 *
 * Unlocks the filesystem and marks it writeable again after freeze_bdev().
 */
int thaw_bdev(struct block_device *bdev, struct super_block *sb)
{
	int error = -EINVAL;

	mutex_lock(&bdev->bd_fsfreeze_mutex);
	if (!bdev->bd_fsfreeze_count)
		goto out;

	error = 0;
	if (--bdev->bd_fsfreeze_count > 0)
		goto out;

	if (!sb)
		goto out;

	if (sb->s_op->thaw_super)
		error = sb->s_op->thaw_super(sb);
	else
		error = thaw_super(sb);
	if (error) {
		bdev->bd_fsfreeze_count++;
		mutex_unlock(&bdev->bd_fsfreeze_mutex);
		return error;
	}
out:
	mutex_unlock(&bdev->bd_fsfreeze_mutex);
	return 0;
}
EXPORT_SYMBOL(thaw_bdev);

static int blkdev_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page, blkdev_get_block, wbc);
}

static int blkdev_readpage(struct file * file, struct page * page)
{
	return block_read_full_page(page, blkdev_get_block);
}

static int blkdev_readpages(struct file *file, struct address_space *mapping,
			struct list_head *pages, unsigned nr_pages)
{
	return mpage_readpages(mapping, pages, nr_pages, blkdev_get_block);
}

static int blkdev_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned flags,
			struct page **pagep, void **fsdata)
{
	*pagep = NULL;
	return block_write_begin(file, mapping, pos, len, flags, pagep, fsdata,
				blkdev_get_block);
	return block_write_begin(mapping, pos, len, flags, pagep,
				 blkdev_get_block);
}

static int blkdev_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata)
{
	int ret;
	ret = block_write_end(file, mapping, pos, len, copied, page, fsdata);

	unlock_page(page);
	page_cache_release(page);

	return ret;
}

/*
 * private llseek:
 * for a block special file file->f_path.dentry->d_inode->i_size is zero
 * so we compute the size by hand (just as in block_read/write above)
 */
static loff_t block_llseek(struct file *file, loff_t offset, int origin)
{
	struct inode *bd_inode = file->f_mapping->host;
	loff_t size;
	loff_t retval;

	mutex_lock(&bd_inode->i_mutex);
	size = i_size_read(bd_inode);

	switch (origin) {
		case 2:
			offset += size;
			break;
		case 1:
			offset += file->f_pos;
	}
	retval = -EINVAL;
	if (offset >= 0 && offset <= size) {
		if (offset != file->f_pos) {
			file->f_pos = offset;
		}
		retval = offset;
	}
 * for a block special file file_inode(file)->i_size is zero
 * so we compute the size by hand (just as in block_read/write above)
 */
static loff_t block_llseek(struct file *file, loff_t offset, int whence)
{
	struct inode *bd_inode = file->f_mapping->host;
	loff_t retval;

	mutex_lock(&bd_inode->i_mutex);
	retval = fixed_size_llseek(file, offset, whence, i_size_read(bd_inode));
	mutex_unlock(&bd_inode->i_mutex);
	return retval;
}
	
/*
 *	Filp is never NULL; the only case when ->fsync() is called with
 *	NULL first argument is nfsd_sync_dir() and that's not a directory.
 */
 
static int block_fsync(struct file *filp, struct dentry *dentry, int datasync)
{
	return sync_blockdev(I_BDEV(filp->f_mapping->host));
}
int blkdev_fsync(struct file *filp, loff_t start, loff_t end, int datasync)
{
	struct inode *bd_inode = filp->f_mapping->host;
	struct block_device *bdev = I_BDEV(bd_inode);
	int error;
	
	error = filemap_write_and_wait_range(filp->f_mapping, start, end);
	if (error)
		return error;

	/*
	 * There is no need to serialise calls to blkdev_issue_flush with
	 * i_mutex and doing so causes performance issues with concurrent
	 * O_SYNC writers to a block device.
	 */
	error = blkdev_issue_flush(bdev, GFP_KERNEL, NULL);
	if (error == -EOPNOTSUPP)
		error = 0;

	return error;
}
EXPORT_SYMBOL(blkdev_fsync);

/**
 * bdev_read_page() - Start reading a page from a block device
 * @bdev: The device to read the page from
 * @sector: The offset on the device to read the page to (need not be aligned)
 * @page: The page to read
 *
 * On entry, the page should be locked.  It will be unlocked when the page
 * has been read.  If the block driver implements rw_page synchronously,
 * that will be true on exit from this function, but it need not be.
 *
 * Errors returned by this function are usually "soft", eg out of memory, or
 * queue full; callers should try a different route to read this page rather
 * than propagate an error back up the stack.
 *
 * Return: negative errno if an error occurs, 0 if submission was successful.
 */
int bdev_read_page(struct block_device *bdev, sector_t sector,
			struct page *page)
{
	const struct block_device_operations *ops = bdev->bd_disk->fops;
	int result = -EOPNOTSUPP;

	if (!ops->rw_page || bdev_get_integrity(bdev))
		return result;

	result = blk_queue_enter(bdev->bd_queue, GFP_KERNEL);
	if (result)
		return result;
	result = ops->rw_page(bdev, sector + get_start_sect(bdev), page, READ);
	blk_queue_exit(bdev->bd_queue);
	return result;
}
EXPORT_SYMBOL_GPL(bdev_read_page);

/**
 * bdev_write_page() - Start writing a page to a block device
 * @bdev: The device to write the page to
 * @sector: The offset on the device to write the page to (need not be aligned)
 * @page: The page to write
 * @wbc: The writeback_control for the write
 *
 * On entry, the page should be locked and not currently under writeback.
 * On exit, if the write started successfully, the page will be unlocked and
 * under writeback.  If the write failed already (eg the driver failed to
 * queue the page to the device), the page will still be locked.  If the
 * caller is a ->writepage implementation, it will need to unlock the page.
 *
 * Errors returned by this function are usually "soft", eg out of memory, or
 * queue full; callers should try a different route to write this page rather
 * than propagate an error back up the stack.
 *
 * Return: negative errno if an error occurs, 0 if submission was successful.
 */
int bdev_write_page(struct block_device *bdev, sector_t sector,
			struct page *page, struct writeback_control *wbc)
{
	int result;
	int rw = (wbc->sync_mode == WB_SYNC_ALL) ? WRITE_SYNC : WRITE;
	const struct block_device_operations *ops = bdev->bd_disk->fops;

	if (!ops->rw_page || bdev_get_integrity(bdev))
		return -EOPNOTSUPP;
	result = blk_queue_enter(bdev->bd_queue, GFP_KERNEL);
	if (result)
		return result;

	set_page_writeback(page);
	result = ops->rw_page(bdev, sector + get_start_sect(bdev), page, rw);
	if (result)
		end_page_writeback(page);
	else
		unlock_page(page);
	blk_queue_exit(bdev->bd_queue);
	return result;
}
EXPORT_SYMBOL_GPL(bdev_write_page);

/**
 * bdev_direct_access() - Get the address for directly-accessibly memory
 * @bdev: The device containing the memory
 * @sector: The offset within the device
 * @addr: Where to put the address of the memory
 * @pfn: The Page Frame Number for the memory
 * @size: The number of bytes requested
 *
 * If a block device is made up of directly addressable memory, this function
 * will tell the caller the PFN and the address of the memory.  The address
 * may be directly dereferenced within the kernel without the need to call
 * ioremap(), kmap() or similar.  The PFN is suitable for inserting into
 * page tables.
 *
 * Return: negative errno if an error occurs, otherwise the number of bytes
 * accessible at this address.
 */
long bdev_direct_access(struct block_device *bdev, sector_t sector,
			void __pmem **addr, unsigned long *pfn, long size)
{
	long avail;
	const struct block_device_operations *ops = bdev->bd_disk->fops;

	/*
	 * The device driver is allowed to sleep, in order to make the
	 * memory directly accessible.
	 */
	might_sleep();

	if (size < 0)
		return size;
	if (!ops->direct_access)
		return -EOPNOTSUPP;
	if ((sector + DIV_ROUND_UP(size, 512)) >
					part_nr_sects_read(bdev->bd_part))
		return -ERANGE;
	sector += get_start_sect(bdev);
	if (sector % (PAGE_SIZE / 512))
		return -EINVAL;
	avail = ops->direct_access(bdev, sector, addr, pfn);
	if (!avail)
		return -ERANGE;
	return min(avail, size);
}
EXPORT_SYMBOL_GPL(bdev_direct_access);

/*
 * pseudo-fs
 */

static  __cacheline_aligned_in_smp DEFINE_SPINLOCK(bdev_lock);
static struct kmem_cache * bdev_cachep __read_mostly;

static struct inode *bdev_alloc_inode(struct super_block *sb)
{
	struct bdev_inode *ei = kmem_cache_alloc(bdev_cachep, GFP_KERNEL);
	if (!ei)
		return NULL;
	return &ei->vfs_inode;
}

static void bdev_destroy_inode(struct inode *inode)
{
	struct bdev_inode *bdi = BDEV_I(inode);

	bdi->bdev.bd_inode_backing_dev_info = NULL;
	kmem_cache_free(bdev_cachep, bdi);
}

static void bdev_i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	struct bdev_inode *bdi = BDEV_I(inode);

	kmem_cache_free(bdev_cachep, bdi);
}

static void bdev_destroy_inode(struct inode *inode)
{
	call_rcu(&inode->i_rcu, bdev_i_callback);
}

static void init_once(void *foo)
{
	struct bdev_inode *ei = (struct bdev_inode *) foo;
	struct block_device *bdev = &ei->bdev;

	memset(bdev, 0, sizeof(*bdev));
	mutex_init(&bdev->bd_mutex);
	sema_init(&bdev->bd_mount_sem, 1);
	INIT_LIST_HEAD(&bdev->bd_inodes);
	INIT_LIST_HEAD(&bdev->bd_list);
#ifdef CONFIG_SYSFS
	INIT_LIST_HEAD(&bdev->bd_holder_list);
#endif
	inode_init_once(&ei->vfs_inode);
	INIT_LIST_HEAD(&bdev->bd_inodes);
	INIT_LIST_HEAD(&bdev->bd_list);
#ifdef CONFIG_SYSFS
	INIT_LIST_HEAD(&bdev->bd_holder_disks);
#endif
	inode_init_once(&ei->vfs_inode);
	/* Initialize mutex for freeze. */
	mutex_init(&bdev->bd_fsfreeze_mutex);
}

static inline void __bd_forget(struct inode *inode)
{
	list_del_init(&inode->i_devices);
	inode->i_bdev = NULL;
	inode->i_mapping = &inode->i_data;
}

static void bdev_clear_inode(struct inode *inode)
{
	struct block_device *bdev = &BDEV_I(inode)->bdev;
	struct list_head *p;
static void bdev_evict_inode(struct inode *inode)
{
	struct block_device *bdev = &BDEV_I(inode)->bdev;
	struct list_head *p;
	truncate_inode_pages_final(&inode->i_data);
	invalidate_inode_buffers(inode); /* is it needed here? */
	clear_inode(inode);
	spin_lock(&bdev_lock);
	while ( (p = bdev->bd_inodes.next) != &bdev->bd_inodes ) {
		__bd_forget(list_entry(p, struct inode, i_devices));
	}
	list_del_init(&bdev->bd_list);
	spin_unlock(&bdev_lock);
}

static const struct super_operations bdev_sops = {
	.statfs = simple_statfs,
	.alloc_inode = bdev_alloc_inode,
	.destroy_inode = bdev_destroy_inode,
	.drop_inode = generic_delete_inode,
	.clear_inode = bdev_clear_inode,
};

static int bd_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
	return get_sb_pseudo(fs_type, "bdev:", &bdev_sops, 0x62646576, mnt);
	.evict_inode = bdev_evict_inode,
};

static struct dentry *bd_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return mount_pseudo(fs_type, "bdev:", &bdev_sops, NULL, BDEVFS_MAGIC);
}

static struct file_system_type bd_type = {
	.name		= "bdev",
	.get_sb		= bd_get_sb,
	.kill_sb	= kill_anon_super,
};

static struct vfsmount *bd_mnt __read_mostly;
struct super_block *blockdev_superblock;
	.mount		= bd_mount,
	.kill_sb	= kill_anon_super,
};

struct super_block *blockdev_superblock __read_mostly;
EXPORT_SYMBOL_GPL(blockdev_superblock);

void __init bdev_cache_init(void)
{
	int err;
	static struct vfsmount *bd_mnt;

	bdev_cachep = kmem_cache_create("bdev_cache", sizeof(struct bdev_inode),
			0, (SLAB_HWCACHE_ALIGN|SLAB_RECLAIM_ACCOUNT|
				SLAB_MEM_SPREAD|SLAB_PANIC),
			init_once);
	err = register_filesystem(&bd_type);
	if (err)
		panic("Cannot register bdev pseudo-fs");
	bd_mnt = kern_mount(&bd_type);
	if (IS_ERR(bd_mnt))
		panic("Cannot create bdev pseudo-fs");
	blockdev_superblock = bd_mnt->mnt_sb;	/* For writeback */
	blockdev_superblock = bd_mnt->mnt_sb;   /* For writeback */
}

/*
 * Most likely _very_ bad one - but then it's hardly critical for small
 * /dev and can be fixed when somebody will need really large one.
 * Keep in mind that it will be fed through icache hash function too.
 */
static inline unsigned long hash(dev_t dev)
{
	return MAJOR(dev)+MINOR(dev);
}

static int bdev_test(struct inode *inode, void *data)
{
	return BDEV_I(inode)->bdev.bd_dev == *(dev_t *)data;
}

static int bdev_set(struct inode *inode, void *data)
{
	BDEV_I(inode)->bdev.bd_dev = *(dev_t *)data;
	return 0;
}

static LIST_HEAD(all_bdevs);

struct block_device *bdget(dev_t dev)
{
	struct block_device *bdev;
	struct inode *inode;

	inode = iget5_locked(bd_mnt->mnt_sb, hash(dev),
	inode = iget5_locked(blockdev_superblock, hash(dev),
			bdev_test, bdev_set, &dev);

	if (!inode)
		return NULL;

	bdev = &BDEV_I(inode)->bdev;

	if (inode->i_state & I_NEW) {
		bdev->bd_contains = NULL;
		bdev->bd_super = NULL;
		bdev->bd_inode = inode;
		bdev->bd_block_size = (1 << inode->i_blkbits);
		bdev->bd_part_count = 0;
		bdev->bd_invalidated = 0;
		inode->i_mode = S_IFBLK;
		inode->i_rdev = dev;
		inode->i_bdev = bdev;
		inode->i_data.a_ops = &def_blk_aops;
		mapping_set_gfp_mask(&inode->i_data, GFP_USER);
		inode->i_data.backing_dev_info = &default_backing_dev_info;
		spin_lock(&bdev_lock);
		list_add(&bdev->bd_list, &all_bdevs);
		spin_unlock(&bdev_lock);
		unlock_new_inode(inode);
	}
	return bdev;
}

EXPORT_SYMBOL(bdget);

/**
 * bdgrab -- Grab a reference to an already referenced block device
 * @bdev:	Block device to grab a reference to.
 */
struct block_device *bdgrab(struct block_device *bdev)
{
	ihold(bdev->bd_inode);
	return bdev;
}
EXPORT_SYMBOL(bdgrab);

long nr_blockdev_pages(void)
{
	struct block_device *bdev;
	long ret = 0;
	spin_lock(&bdev_lock);
	list_for_each_entry(bdev, &all_bdevs, bd_list) {
		ret += bdev->bd_inode->i_mapping->nrpages;
	}
	spin_unlock(&bdev_lock);
	return ret;
}

void bdput(struct block_device *bdev)
{
	iput(bdev->bd_inode);
}

EXPORT_SYMBOL(bdput);
 
static struct block_device *bd_acquire(struct inode *inode)
{
	struct block_device *bdev;

	spin_lock(&bdev_lock);
	bdev = inode->i_bdev;
	if (bdev) {
		atomic_inc(&bdev->bd_inode->i_count);
		ihold(bdev->bd_inode);
		spin_unlock(&bdev_lock);
		return bdev;
	}
	spin_unlock(&bdev_lock);

	bdev = bdget(inode->i_rdev);
	if (bdev) {
		spin_lock(&bdev_lock);
		if (!inode->i_bdev) {
			/*
			 * We take an additional bd_inode->i_count for inode,
			 * We take an additional reference to bd_inode,
			 * and it's released in clear_inode() of inode.
			 * So, we can access it via ->i_mapping always
			 * without igrab().
			 */
			atomic_inc(&bdev->bd_inode->i_count);
			ihold(bdev->bd_inode);
			inode->i_bdev = bdev;
			inode->i_mapping = bdev->bd_inode->i_mapping;
			list_add(&inode->i_devices, &bdev->bd_inodes);
		}
		spin_unlock(&bdev_lock);
	}
	return bdev;
}

/* Call when you free inode */

void bd_forget(struct inode *inode)
{
	struct block_device *bdev = NULL;

	spin_lock(&bdev_lock);
	if (inode->i_bdev) {
		if (inode->i_sb != blockdev_superblock)
			bdev = inode->i_bdev;
		__bd_forget(inode);
	}
	if (!sb_is_blkdev_sb(inode->i_sb))
		bdev = inode->i_bdev;
	__bd_forget(inode);
	spin_unlock(&bdev_lock);

	if (bdev)
		iput(bdev->bd_inode);
}

int bd_claim(struct block_device *bdev, void *holder)
{
	int res;
	spin_lock(&bdev_lock);

	/* first decide result */
	if (bdev->bd_holder == holder)
		res = 0;	 /* already a holder */
	else if (bdev->bd_holder != NULL)
		res = -EBUSY; 	 /* held by someone else */
	else if (bdev->bd_contains == bdev)
		res = 0;  	 /* is a whole device which isn't held */

	else if (bdev->bd_contains->bd_holder == bd_claim)
		res = 0; 	 /* is a partition of a device that is being partitioned */
	else if (bdev->bd_contains->bd_holder != NULL)
		res = -EBUSY;	 /* is a partition of a held device */
	else
		res = 0;	 /* is a partition of an un-held device */

	/* now impose change */
	if (res==0) {
		/* note that for a whole device bd_holders
		 * will be incremented twice, and bd_holder will
		 * be set to bd_claim before being set to holder
		 */
		bdev->bd_contains->bd_holders ++;
		bdev->bd_contains->bd_holder = bd_claim;
		bdev->bd_holders++;
		bdev->bd_holder = holder;
	}
	spin_unlock(&bdev_lock);
	return res;
}

EXPORT_SYMBOL(bd_claim);

void bd_release(struct block_device *bdev)
{
	spin_lock(&bdev_lock);
	if (!--bdev->bd_contains->bd_holders)
		bdev->bd_contains->bd_holder = NULL;
	if (!--bdev->bd_holders)
		bdev->bd_holder = NULL;
	spin_unlock(&bdev_lock);
}

EXPORT_SYMBOL(bd_release);

#ifdef CONFIG_SYSFS
/*
 * Functions for bd_claim_by_kobject / bd_release_from_kobject
 *
 *     If a kobject is passed to bd_claim_by_kobject()
 *     and the kobject has a parent directory,
 *     following symlinks are created:
 *        o from the kobject to the claimed bdev
 *        o from "holders" directory of the bdev to the parent of the kobject
 *     bd_release_from_kobject() removes these symlinks.
 *
 *     Example:
 *        If /dev/dm-0 maps to /dev/sda, kobject corresponding to
 *        /sys/block/dm-0/slaves is passed to bd_claim_by_kobject(), then:
 *           /sys/block/dm-0/slaves/sda --> /sys/block/sda
 *           /sys/block/sda/holders/dm-0 --> /sys/block/dm-0
 */

static struct kobject *bdev_get_kobj(struct block_device *bdev)
{
	if (bdev->bd_contains != bdev)
		return kobject_get(&bdev->bd_part->dev.kobj);
	else
		return kobject_get(&bdev->bd_disk->dev.kobj);
}

static struct kobject *bdev_get_holder(struct block_device *bdev)
{
	if (bdev->bd_contains != bdev)
		return kobject_get(bdev->bd_part->holder_dir);
	else
		return kobject_get(bdev->bd_disk->holder_dir);
/**
 * bd_may_claim - test whether a block device can be claimed
 * @bdev: block device of interest
 * @whole: whole block device containing @bdev, may equal @bdev
 * @holder: holder trying to claim @bdev
 *
 * Test whether @bdev can be claimed by @holder.
 *
 * CONTEXT:
 * spin_lock(&bdev_lock).
 *
 * RETURNS:
 * %true if @bdev can be claimed, %false otherwise.
 */
static bool bd_may_claim(struct block_device *bdev, struct block_device *whole,
			 void *holder)
{
	if (bdev->bd_holder == holder)
		return true;	 /* already a holder */
	else if (bdev->bd_holder != NULL)
		return false; 	 /* held by someone else */
	else if (whole == bdev)
		return true;  	 /* is a whole device which isn't held */

	else if (whole->bd_holder == bd_may_claim)
		return true; 	 /* is a partition of a device that is being partitioned */
	else if (whole->bd_holder != NULL)
		return false;	 /* is a partition of a held device */
	else
		return true;	 /* is a partition of an un-held device */
}

/**
 * bd_prepare_to_claim - prepare to claim a block device
 * @bdev: block device of interest
 * @whole: the whole device containing @bdev, may equal @bdev
 * @holder: holder trying to claim @bdev
 *
 * Prepare to claim @bdev.  This function fails if @bdev is already
 * claimed by another holder and waits if another claiming is in
 * progress.  This function doesn't actually claim.  On successful
 * return, the caller has ownership of bd_claiming and bd_holder[s].
 *
 * CONTEXT:
 * spin_lock(&bdev_lock).  Might release bdev_lock, sleep and regrab
 * it multiple times.
 *
 * RETURNS:
 * 0 if @bdev can be claimed, -EBUSY otherwise.
 */
static int bd_prepare_to_claim(struct block_device *bdev,
			       struct block_device *whole, void *holder)
{
retry:
	/* if someone else claimed, fail */
	if (!bd_may_claim(bdev, whole, holder))
		return -EBUSY;

	/* if claiming is already in progress, wait for it to finish */
	if (whole->bd_claiming) {
		wait_queue_head_t *wq = bit_waitqueue(&whole->bd_claiming, 0);
		DEFINE_WAIT(wait);

		prepare_to_wait(wq, &wait, TASK_UNINTERRUPTIBLE);
		spin_unlock(&bdev_lock);
		schedule();
		finish_wait(wq, &wait);
		spin_lock(&bdev_lock);
		goto retry;
	}

	/* yay, all mine */
	return 0;
}

/**
 * bd_start_claiming - start claiming a block device
 * @bdev: block device of interest
 * @holder: holder trying to claim @bdev
 *
 * @bdev is about to be opened exclusively.  Check @bdev can be opened
 * exclusively and mark that an exclusive open is in progress.  Each
 * successful call to this function must be matched with a call to
 * either bd_finish_claiming() or bd_abort_claiming() (which do not
 * fail).
 *
 * This function is used to gain exclusive access to the block device
 * without actually causing other exclusive open attempts to fail. It
 * should be used when the open sequence itself requires exclusive
 * access but may subsequently fail.
 *
 * CONTEXT:
 * Might sleep.
 *
 * RETURNS:
 * Pointer to the block device containing @bdev on success, ERR_PTR()
 * value on failure.
 */
static struct block_device *bd_start_claiming(struct block_device *bdev,
					      void *holder)
{
	struct gendisk *disk;
	struct block_device *whole;
	int partno, err;

	might_sleep();

	/*
	 * @bdev might not have been initialized properly yet, look up
	 * and grab the outer block device the hard way.
	 */
	disk = get_gendisk(bdev->bd_dev, &partno);
	if (!disk)
		return ERR_PTR(-ENXIO);

	/*
	 * Normally, @bdev should equal what's returned from bdget_disk()
	 * if partno is 0; however, some drivers (floppy) use multiple
	 * bdev's for the same physical device and @bdev may be one of the
	 * aliases.  Keep @bdev if partno is 0.  This means claimer
	 * tracking is broken for those devices but it has always been that
	 * way.
	 */
	if (partno)
		whole = bdget_disk(disk, 0);
	else
		whole = bdgrab(bdev);

	module_put(disk->fops->owner);
	put_disk(disk);
	if (!whole)
		return ERR_PTR(-ENOMEM);

	/* prepare to claim, if successful, mark claiming in progress */
	spin_lock(&bdev_lock);

	err = bd_prepare_to_claim(bdev, whole, holder);
	if (err == 0) {
		whole->bd_claiming = holder;
		spin_unlock(&bdev_lock);
		return whole;
	} else {
		spin_unlock(&bdev_lock);
		bdput(whole);
		return ERR_PTR(err);
	}
}

#ifdef CONFIG_SYSFS
struct bd_holder_disk {
	struct list_head	list;
	struct gendisk		*disk;
	int			refcnt;
};

static struct bd_holder_disk *bd_find_holder_disk(struct block_device *bdev,
						  struct gendisk *disk)
{
	struct bd_holder_disk *holder;

	list_for_each_entry(holder, &bdev->bd_holder_disks, list)
		if (holder->disk == disk)
			return holder;
	return NULL;
}

static int add_symlink(struct kobject *from, struct kobject *to)
{
	if (!from || !to)
		return 0;
	return sysfs_create_link(from, to, kobject_name(to));
}

static void del_symlink(struct kobject *from, struct kobject *to)
{
	if (!from || !to)
		return;
	sysfs_remove_link(from, kobject_name(to));
}

/*
 * 'struct bd_holder' contains pointers to kobjects symlinked by
 * bd_claim_by_kobject.
 * It's connected to bd_holder_list which is protected by bdev->bd_sem.
 */
struct bd_holder {
	struct list_head list;	/* chain of holders of the bdev */
	int count;		/* references from the holder */
	struct kobject *sdir;	/* holder object, e.g. "/block/dm-0/slaves" */
	struct kobject *hdev;	/* e.g. "/block/dm-0" */
	struct kobject *hdir;	/* e.g. "/block/sda/holders" */
	struct kobject *sdev;	/* e.g. "/block/sda" */
};

/*
 * Get references of related kobjects at once.
 * Returns 1 on success. 0 on failure.
 *
 * Should call bd_holder_release_dirs() after successful use.
 */
static int bd_holder_grab_dirs(struct block_device *bdev,
			struct bd_holder *bo)
{
	if (!bdev || !bo)
		return 0;

	bo->sdir = kobject_get(bo->sdir);
	if (!bo->sdir)
		return 0;

	bo->hdev = kobject_get(bo->sdir->parent);
	if (!bo->hdev)
		goto fail_put_sdir;

	bo->sdev = bdev_get_kobj(bdev);
	if (!bo->sdev)
		goto fail_put_hdev;

	bo->hdir = bdev_get_holder(bdev);
	if (!bo->hdir)
		goto fail_put_sdev;

	return 1;

fail_put_sdev:
	kobject_put(bo->sdev);
fail_put_hdev:
	kobject_put(bo->hdev);
fail_put_sdir:
	kobject_put(bo->sdir);

	return 0;
}

/* Put references of related kobjects at once. */
static void bd_holder_release_dirs(struct bd_holder *bo)
{
	kobject_put(bo->hdir);
	kobject_put(bo->sdev);
	kobject_put(bo->hdev);
	kobject_put(bo->sdir);
}

static struct bd_holder *alloc_bd_holder(struct kobject *kobj)
{
	struct bd_holder *bo;

	bo = kzalloc(sizeof(*bo), GFP_KERNEL);
	if (!bo)
		return NULL;

	bo->count = 1;
	bo->sdir = kobj;

	return bo;
}

static void free_bd_holder(struct bd_holder *bo)
{
	kfree(bo);
}

/**
 * find_bd_holder - find matching struct bd_holder from the block device
 *
 * @bdev:	struct block device to be searched
 * @bo:		target struct bd_holder
 *
 * Returns matching entry with @bo in @bdev->bd_holder_list.
 * If found, increment the reference count and return the pointer.
 * If not found, returns NULL.
 */
static struct bd_holder *find_bd_holder(struct block_device *bdev,
					struct bd_holder *bo)
{
	struct bd_holder *tmp;

	list_for_each_entry(tmp, &bdev->bd_holder_list, list)
		if (tmp->sdir == bo->sdir) {
			tmp->count++;
			return tmp;
		}

	return NULL;
}

/**
 * add_bd_holder - create sysfs symlinks for bd_claim() relationship
 *
 * @bdev:	block device to be bd_claimed
 * @bo:		preallocated and initialized by alloc_bd_holder()
 *
 * Add @bo to @bdev->bd_holder_list, create symlinks.
 *
 * Returns 0 if symlinks are created.
 * Returns -ve if something fails.
 */
static int add_bd_holder(struct block_device *bdev, struct bd_holder *bo)
{
	int err;

	if (!bo)
		return -EINVAL;

	if (!bd_holder_grab_dirs(bdev, bo))
		return -EBUSY;

	err = add_symlink(bo->sdir, bo->sdev);
	if (err)
		return err;

	err = add_symlink(bo->hdir, bo->hdev);
	if (err) {
		del_symlink(bo->sdir, bo->sdev);
		return err;
	}

	list_add_tail(&bo->list, &bdev->bd_holder_list);
	return 0;
}

/**
 * del_bd_holder - delete sysfs symlinks for bd_claim() relationship
 *
 * @bdev:	block device to be bd_claimed
 * @kobj:	holder's kobject
 *
 * If there is matching entry with @kobj in @bdev->bd_holder_list
 * and no other bd_claim() from the same kobject,
 * remove the struct bd_holder from the list, delete symlinks for it.
 *
 * Returns a pointer to the struct bd_holder when it's removed from the list
 * and ready to be freed.
 * Returns NULL if matching claim isn't found or there is other bd_claim()
 * by the same kobject.
 */
static struct bd_holder *del_bd_holder(struct block_device *bdev,
					struct kobject *kobj)
{
	struct bd_holder *bo;

	list_for_each_entry(bo, &bdev->bd_holder_list, list) {
		if (bo->sdir == kobj) {
			bo->count--;
			BUG_ON(bo->count < 0);
			if (!bo->count) {
				list_del(&bo->list);
				del_symlink(bo->sdir, bo->sdev);
				del_symlink(bo->hdir, bo->hdev);
				bd_holder_release_dirs(bo);
				return bo;
			}
			break;
		}
	}

	return NULL;
}

/**
 * bd_claim_by_kobject - bd_claim() with additional kobject signature
 *
 * @bdev:	block device to be claimed
 * @holder:	holder's signature
 * @kobj:	holder's kobject
 *
 * Do bd_claim() and if it succeeds, create sysfs symlinks between
 * the bdev and the holder's kobject.
 * Use bd_release_from_kobject() when relesing the claimed bdev.
 *
 * Returns 0 on success. (same as bd_claim())
 * Returns errno on failure.
 */
static int bd_claim_by_kobject(struct block_device *bdev, void *holder,
				struct kobject *kobj)
{
	int err;
	struct bd_holder *bo, *found;

	if (!kobj)
		return -EINVAL;

	bo = alloc_bd_holder(kobj);
	if (!bo)
		return -ENOMEM;

	mutex_lock(&bdev->bd_mutex);

	err = bd_claim(bdev, holder);
	if (err)
		goto fail;

	found = find_bd_holder(bdev, bo);
	if (found)
		goto fail;

	err = add_bd_holder(bdev, bo);
	if (err)
		bd_release(bdev);
	else
		bo = NULL;
fail:
	mutex_unlock(&bdev->bd_mutex);
	free_bd_holder(bo);
	return err;
}

/**
 * bd_release_from_kobject - bd_release() with additional kobject signature
 *
 * @bdev:	block device to be released
 * @kobj:	holder's kobject
 *
 * Do bd_release() and remove sysfs symlinks created by bd_claim_by_kobject().
 */
static void bd_release_from_kobject(struct block_device *bdev,
					struct kobject *kobj)
{
	if (!kobj)
		return;

	mutex_lock(&bdev->bd_mutex);
	bd_release(bdev);
	free_bd_holder(del_bd_holder(bdev, kobj));
	mutex_unlock(&bdev->bd_mutex);
}

/**
 * bd_claim_by_disk - wrapper function for bd_claim_by_kobject()
 *
 * @bdev:	block device to be claimed
 * @holder:	holder's signature
 * @disk:	holder's gendisk
 *
 * Call bd_claim_by_kobject() with getting @disk->slave_dir.
 */
int bd_claim_by_disk(struct block_device *bdev, void *holder,
			struct gendisk *disk)
{
	return bd_claim_by_kobject(bdev, holder, kobject_get(disk->slave_dir));
}
EXPORT_SYMBOL_GPL(bd_claim_by_disk);

/**
 * bd_release_from_disk - wrapper function for bd_release_from_kobject()
 *
 * @bdev:	block device to be claimed
 * @disk:	holder's gendisk
 *
 * Call bd_release_from_kobject() and put @disk->slave_dir.
 */
void bd_release_from_disk(struct block_device *bdev, struct gendisk *disk)
{
	bd_release_from_kobject(bdev, disk->slave_dir);
	kobject_put(disk->slave_dir);
}
EXPORT_SYMBOL_GPL(bd_release_from_disk);
#endif

/*
 * Tries to open block device by device number.  Use it ONLY if you
 * really do not have anything better - i.e. when you are behind a
 * truly sucky interface and all you are given is a device number.  _Never_
 * to be used for internal purposes.  If you ever need it - reconsider
 * your API.
 */
struct block_device *open_by_devnum(dev_t dev, unsigned mode)
{
	struct block_device *bdev = bdget(dev);
	int err = -ENOMEM;
	int flags = mode & FMODE_WRITE ? O_RDWR : O_RDONLY;
	if (bdev)
		err = blkdev_get(bdev, mode, flags);
	return err ? ERR_PTR(err) : bdev;
}

EXPORT_SYMBOL(open_by_devnum);
	sysfs_remove_link(from, kobject_name(to));
}

/**
 * bd_link_disk_holder - create symlinks between holding disk and slave bdev
 * @bdev: the claimed slave bdev
 * @disk: the holding disk
 *
 * DON'T USE THIS UNLESS YOU'RE ALREADY USING IT.
 *
 * This functions creates the following sysfs symlinks.
 *
 * - from "slaves" directory of the holder @disk to the claimed @bdev
 * - from "holders" directory of the @bdev to the holder @disk
 *
 * For example, if /dev/dm-0 maps to /dev/sda and disk for dm-0 is
 * passed to bd_link_disk_holder(), then:
 *
 *   /sys/block/dm-0/slaves/sda --> /sys/block/sda
 *   /sys/block/sda/holders/dm-0 --> /sys/block/dm-0
 *
 * The caller must have claimed @bdev before calling this function and
 * ensure that both @bdev and @disk are valid during the creation and
 * lifetime of these symlinks.
 *
 * CONTEXT:
 * Might sleep.
 *
 * RETURNS:
 * 0 on success, -errno on failure.
 */
int bd_link_disk_holder(struct block_device *bdev, struct gendisk *disk)
{
	struct bd_holder_disk *holder;
	int ret = 0;

	mutex_lock(&bdev->bd_mutex);

	WARN_ON_ONCE(!bdev->bd_holder);

	/* FIXME: remove the following once add_disk() handles errors */
	if (WARN_ON(!disk->slave_dir || !bdev->bd_part->holder_dir))
		goto out_unlock;

	holder = bd_find_holder_disk(bdev, disk);
	if (holder) {
		holder->refcnt++;
		goto out_unlock;
	}

	holder = kzalloc(sizeof(*holder), GFP_KERNEL);
	if (!holder) {
		ret = -ENOMEM;
		goto out_unlock;
	}

	INIT_LIST_HEAD(&holder->list);
	holder->disk = disk;
	holder->refcnt = 1;

	ret = add_symlink(disk->slave_dir, &part_to_dev(bdev->bd_part)->kobj);
	if (ret)
		goto out_free;

	ret = add_symlink(bdev->bd_part->holder_dir, &disk_to_dev(disk)->kobj);
	if (ret)
		goto out_del;
	/*
	 * bdev could be deleted beneath us which would implicitly destroy
	 * the holder directory.  Hold on to it.
	 */
	kobject_get(bdev->bd_part->holder_dir);

	list_add(&holder->list, &bdev->bd_holder_disks);
	goto out_unlock;

out_del:
	del_symlink(disk->slave_dir, &part_to_dev(bdev->bd_part)->kobj);
out_free:
	kfree(holder);
out_unlock:
	mutex_unlock(&bdev->bd_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(bd_link_disk_holder);

/**
 * bd_unlink_disk_holder - destroy symlinks created by bd_link_disk_holder()
 * @bdev: the calimed slave bdev
 * @disk: the holding disk
 *
 * DON'T USE THIS UNLESS YOU'RE ALREADY USING IT.
 *
 * CONTEXT:
 * Might sleep.
 */
void bd_unlink_disk_holder(struct block_device *bdev, struct gendisk *disk)
{
	struct bd_holder_disk *holder;

	mutex_lock(&bdev->bd_mutex);

	holder = bd_find_holder_disk(bdev, disk);

	if (!WARN_ON_ONCE(holder == NULL) && !--holder->refcnt) {
		del_symlink(disk->slave_dir, &part_to_dev(bdev->bd_part)->kobj);
		del_symlink(bdev->bd_part->holder_dir,
			    &disk_to_dev(disk)->kobj);
		kobject_put(bdev->bd_part->holder_dir);
		list_del_init(&holder->list);
		kfree(holder);
	}

	mutex_unlock(&bdev->bd_mutex);
}
EXPORT_SYMBOL_GPL(bd_unlink_disk_holder);
#endif

/**
 * flush_disk - invalidates all buffer-cache entries on a disk
 *
 * @bdev:      struct block device to be flushed
 * @kill_dirty: flag to guide handling of dirty inodes
 *
 * Invalidates all buffer-cache entries on a disk. It should be called
 * when a disk has been changed -- either by a media change or online
 * resize.
 */
static void flush_disk(struct block_device *bdev, bool kill_dirty)
{
	if (__invalidate_device(bdev, kill_dirty)) {
		char name[BDEVNAME_SIZE] = "";

		if (bdev->bd_disk)
			disk_name(bdev->bd_disk, 0, name);
		printk(KERN_WARNING "VFS: busy inodes on changed media or "
		       "resized disk %s\n", name);
	}

	if (!bdev->bd_disk)
		return;
	if (disk_part_scan_enabled(bdev->bd_disk))
		bdev->bd_invalidated = 1;
}

/**
 * check_disk_size_change - checks for disk size change and adjusts bdev size.
 * @disk: struct gendisk to check
 * @bdev: struct bdev to adjust.
 *
 * This routine checks to see if the bdev size does not match the disk size
 * and adjusts it if it differs.
 */
void check_disk_size_change(struct gendisk *disk, struct block_device *bdev)
{
	loff_t disk_size, bdev_size;

	disk_size = (loff_t)get_capacity(disk) << 9;
	bdev_size = i_size_read(bdev->bd_inode);
	if (disk_size != bdev_size) {
		char name[BDEVNAME_SIZE];

		disk_name(disk, 0, name);
		printk(KERN_INFO
		       "%s: detected capacity change from %lld to %lld\n",
		       name, bdev_size, disk_size);
		i_size_write(bdev->bd_inode, disk_size);
		flush_disk(bdev, false);
	}
}
EXPORT_SYMBOL(check_disk_size_change);

/**
 * revalidate_disk - wrapper for lower-level driver's revalidate_disk call-back
 * @disk: struct gendisk to be revalidated
 *
 * This routine is a wrapper for lower-level driver's revalidate_disk
 * call-backs.  It is used to do common pre and post operations needed
 * for all revalidate_disk operations.
 */
int revalidate_disk(struct gendisk *disk)
{
	struct block_device *bdev;
	int ret = 0;

	if (disk->fops->revalidate_disk)
		ret = disk->fops->revalidate_disk(disk);
	bdev = bdget_disk(disk, 0);
	if (!bdev)
		return ret;

	mutex_lock(&bdev->bd_mutex);
	check_disk_size_change(disk, bdev);
	bdev->bd_invalidated = 0;
	mutex_unlock(&bdev->bd_mutex);
	bdput(bdev);
	return ret;
}
EXPORT_SYMBOL(revalidate_disk);

/*
 * This routine checks whether a removable media has been changed,
 * and invalidates all buffer-cache-entries in that case. This
 * is a relatively slow routine, so we have to try to minimize using
 * it. Thus it is called only upon a 'mount' or 'open'. This
 * is the best way of combining speed and utility, I think.
 * People changing diskettes in the middle of an operation deserve
 * to lose :-)
 */
int check_disk_change(struct block_device *bdev)
{
	struct gendisk *disk = bdev->bd_disk;
	struct block_device_operations * bdops = disk->fops;

	if (!bdops->media_changed)
		return 0;
	if (!bdops->media_changed(bdev->bd_disk))
		return 0;

	if (__invalidate_device(bdev))
		printk("VFS: busy inodes on changed media.\n");

	if (bdops->revalidate_disk)
		bdops->revalidate_disk(bdev->bd_disk);
	if (bdev->bd_disk->minors > 1)
		bdev->bd_invalidated = 1;
	const struct block_device_operations *bdops = disk->fops;
	unsigned int events;

	events = disk_clear_events(disk, DISK_EVENT_MEDIA_CHANGE |
				   DISK_EVENT_EJECT_REQUEST);
	if (!(events & DISK_EVENT_MEDIA_CHANGE))
		return 0;

	flush_disk(bdev, true);
	if (bdops->revalidate_disk)
		bdops->revalidate_disk(bdev->bd_disk);
	return 1;
}

EXPORT_SYMBOL(check_disk_change);

void bd_set_size(struct block_device *bdev, loff_t size)
{
	unsigned bsize = bdev_hardsect_size(bdev);

	bdev->bd_inode->i_size = size;
	unsigned bsize = bdev_logical_block_size(bdev);

	mutex_lock(&bdev->bd_inode->i_mutex);
	i_size_write(bdev->bd_inode, size);
	mutex_unlock(&bdev->bd_inode->i_mutex);
	while (bsize < PAGE_CACHE_SIZE) {
		if (size & bsize)
			break;
		bsize <<= 1;
	}
	bdev->bd_block_size = bsize;
	bdev->bd_inode->i_blkbits = blksize_bits(bsize);
}
EXPORT_SYMBOL(bd_set_size);

static int __blkdev_get(struct block_device *bdev, mode_t mode, unsigned flags,
			int for_part);
static int __blkdev_put(struct block_device *bdev, int for_part);
static void __blkdev_put(struct block_device *bdev, fmode_t mode, int for_part);

/*
 * bd_mutex locking:
 *
 *  mutex_lock(part->bd_mutex)
 *    mutex_lock_nested(whole->bd_mutex, 1)
 */

static int do_open(struct block_device *bdev, struct file *file, int for_part)
{
	struct module *owner = NULL;
	struct gendisk *disk;
	int ret;
	int part;
	int perm = 0;

	if (file->f_mode & FMODE_READ)
		perm |= MAY_READ;
	if (file->f_mode & FMODE_WRITE)
static int __blkdev_get(struct block_device *bdev, fmode_t mode, int for_part)
{
	struct gendisk *disk;
	struct module *owner;
	int ret;
	int partno;
	int perm = 0;

	if (mode & FMODE_READ)
		perm |= MAY_READ;
	if (mode & FMODE_WRITE)
		perm |= MAY_WRITE;
	/*
	 * hooks: /n/, see "layering violations".
	 */
	ret = devcgroup_inode_permission(bdev->bd_inode, perm);
	if (ret != 0) {
		bdput(bdev);
		return ret;
	}

	ret = -ENXIO;
	file->f_mapping = bdev->bd_inode->i_mapping;
	lock_kernel();
	disk = get_gendisk(bdev->bd_dev, &part);
	if (!disk) {
		unlock_kernel();
		bdput(bdev);
		return ret;
	}
	owner = disk->fops->owner;

	mutex_lock_nested(&bdev->bd_mutex, for_part);
	if (!bdev->bd_openers) {
		bdev->bd_disk = disk;
		bdev->bd_contains = bdev;
		if (!part) {
			struct backing_dev_info *bdi;
			if (disk->fops->open) {
				ret = disk->fops->open(bdev->bd_inode, file);
				if (ret)
					goto out_first;
			}
			if (!bdev->bd_openers) {
				bd_set_size(bdev,(loff_t)get_capacity(disk)<<9);
				bdi = blk_get_backing_dev_info(bdev);
				if (bdi == NULL)
					bdi = &default_backing_dev_info;
				bdev->bd_inode->i_data.backing_dev_info = bdi;
			}
			if (bdev->bd_invalidated)
				rescan_partitions(disk, bdev);
		} else {
			struct hd_struct *p;
	if (!for_part) {
		ret = devcgroup_inode_permission(bdev->bd_inode, perm);
		if (ret != 0)
			return ret;
	}

 restart:

	ret = -ENXIO;
	disk = get_gendisk(bdev->bd_dev, &partno);
	if (!disk)
		goto out;
	owner = disk->fops->owner;

	disk_block_events(disk);
	mutex_lock_nested(&bdev->bd_mutex, for_part);
	if (!bdev->bd_openers) {
		bdev->bd_disk = disk;
		bdev->bd_queue = disk->queue;
		bdev->bd_contains = bdev;
		bdev->bd_inode->i_flags = disk->fops->direct_access ? S_DAX : 0;
		if (!partno) {
			ret = -ENXIO;
			bdev->bd_part = disk_get_part(disk, partno);
			if (!bdev->bd_part)
				goto out_clear;

			ret = 0;
			if (disk->fops->open) {
				ret = disk->fops->open(bdev, mode);
				if (ret == -ERESTARTSYS) {
					/* Lost a race with 'disk' being
					 * deleted, try again.
					 * See md.c
					 */
					disk_put_part(bdev->bd_part);
					bdev->bd_part = NULL;
					bdev->bd_disk = NULL;
					bdev->bd_queue = NULL;
					mutex_unlock(&bdev->bd_mutex);
					disk_unblock_events(disk);
					put_disk(disk);
					module_put(owner);
					goto restart;
				}
			}

			if (!ret)
				bd_set_size(bdev,(loff_t)get_capacity(disk)<<9);

			/*
			 * If the device is invalidated, rescan partition
			 * if open succeeded or failed with -ENOMEDIUM.
			 * The latter is necessary to prevent ghost
			 * partitions on a removed medium.
			 */
			if (bdev->bd_invalidated) {
				if (!ret)
					rescan_partitions(disk, bdev);
				else if (ret == -ENOMEDIUM)
					invalidate_partitions(disk, bdev);
			}
			if (ret)
				goto out_clear;
		} else {
			struct block_device *whole;
			whole = bdget_disk(disk, 0);
			ret = -ENOMEM;
			if (!whole)
				goto out_first;
			BUG_ON(for_part);
			ret = __blkdev_get(whole, file->f_mode, file->f_flags, 1);
			if (ret)
				goto out_first;
			bdev->bd_contains = whole;
			p = disk->part[part - 1];
			bdev->bd_inode->i_data.backing_dev_info =
			   whole->bd_inode->i_data.backing_dev_info;
			if (!(disk->flags & GENHD_FL_UP) || !p || !p->nr_sects) {
				ret = -ENXIO;
				goto out_first;
			}
			kobject_get(&p->dev.kobj);
			bdev->bd_part = p;
			bd_set_size(bdev, (loff_t) p->nr_sects << 9);
		}
	} else {
		put_disk(disk);
		module_put(owner);
		if (bdev->bd_contains == bdev) {
			if (bdev->bd_disk->fops->open) {
				ret = bdev->bd_disk->fops->open(bdev->bd_inode, file);
				if (ret)
					goto out;
			}
			if (bdev->bd_invalidated)
				rescan_partitions(bdev->bd_disk, bdev);
		}
				goto out_clear;
			BUG_ON(for_part);
			ret = __blkdev_get(whole, mode, 1);
			if (ret) {
				bdput(whole);
				goto out_clear;
			}
			bdev->bd_contains = whole;
			bdev->bd_part = disk_get_part(disk, partno);
			if (!(disk->flags & GENHD_FL_UP) ||
			    !bdev->bd_part || !bdev->bd_part->nr_sects) {
				ret = -ENXIO;
				goto out_clear;
			}
			bd_set_size(bdev, (loff_t)bdev->bd_part->nr_sects << 9);
			/*
			 * If the partition is not aligned on a page
			 * boundary, we can't do dax I/O to it.
			 */
			if ((bdev->bd_part->start_sect % (PAGE_SIZE / 512)) ||
			    (bdev->bd_part->nr_sects % (PAGE_SIZE / 512)))
				bdev->bd_inode->i_flags &= ~S_DAX;
		}
	} else {
		if (bdev->bd_contains == bdev) {
			ret = 0;
			if (bdev->bd_disk->fops->open)
				ret = bdev->bd_disk->fops->open(bdev, mode);
			/* the same as first opener case, read comment there */
			if (bdev->bd_invalidated) {
				if (!ret)
					rescan_partitions(bdev->bd_disk, bdev);
				else if (ret == -ENOMEDIUM)
					invalidate_partitions(bdev->bd_disk, bdev);
			}
			if (ret)
				goto out_unlock_bdev;
		}
		/* only one opener holds refs to the module and disk */
		put_disk(disk);
		module_put(owner);
	}
	bdev->bd_openers++;
	if (for_part)
		bdev->bd_part_count++;
	mutex_unlock(&bdev->bd_mutex);
	unlock_kernel();
	return 0;

out_first:
	bdev->bd_disk = NULL;
	bdev->bd_inode->i_data.backing_dev_info = &default_backing_dev_info;
	if (bdev != bdev->bd_contains)
		__blkdev_put(bdev->bd_contains, 1);
	bdev->bd_contains = NULL;
	put_disk(disk);
	module_put(owner);
out:
	mutex_unlock(&bdev->bd_mutex);
	unlock_kernel();
	if (ret)
		bdput(bdev);
	return ret;
}

static int __blkdev_get(struct block_device *bdev, mode_t mode, unsigned flags,
			int for_part)
{
	/*
	 * This crockload is due to bad choice of ->open() type.
	 * It will go away.
	 * For now, block device ->open() routine must _not_
	 * examine anything in 'inode' argument except ->i_rdev.
	 */
	struct file fake_file = {};
	struct dentry fake_dentry = {};
	fake_file.f_mode = mode;
	fake_file.f_flags = flags;
	fake_file.f_path.dentry = &fake_dentry;
	fake_dentry.d_inode = bdev->bd_inode;

	return do_open(bdev, &fake_file, for_part);
}

int blkdev_get(struct block_device *bdev, mode_t mode, unsigned flags)
{
	return __blkdev_get(bdev, mode, flags, 0);
}
EXPORT_SYMBOL(blkdev_get);

static int blkdev_open(struct inode * inode, struct file * filp)
{
	struct block_device *bdev;
	int res;
	disk_unblock_events(disk);
	return 0;

 out_clear:
	disk_put_part(bdev->bd_part);
	bdev->bd_disk = NULL;
	bdev->bd_part = NULL;
	bdev->bd_queue = NULL;
	if (bdev != bdev->bd_contains)
		__blkdev_put(bdev->bd_contains, mode, 1);
	bdev->bd_contains = NULL;
 out_unlock_bdev:
	mutex_unlock(&bdev->bd_mutex);
	disk_unblock_events(disk);
	put_disk(disk);
	module_put(owner);
 out:

	return ret;
}

/**
 * blkdev_get - open a block device
 * @bdev: block_device to open
 * @mode: FMODE_* mask
 * @holder: exclusive holder identifier
 *
 * Open @bdev with @mode.  If @mode includes %FMODE_EXCL, @bdev is
 * open with exclusive access.  Specifying %FMODE_EXCL with %NULL
 * @holder is invalid.  Exclusive opens may nest for the same @holder.
 *
 * On success, the reference count of @bdev is unchanged.  On failure,
 * @bdev is put.
 *
 * CONTEXT:
 * Might sleep.
 *
 * RETURNS:
 * 0 on success, -errno on failure.
 */
int blkdev_get(struct block_device *bdev, fmode_t mode, void *holder)
{
	struct block_device *whole = NULL;
	int res;

	WARN_ON_ONCE((mode & FMODE_EXCL) && !holder);

	if ((mode & FMODE_EXCL) && holder) {
		whole = bd_start_claiming(bdev, holder);
		if (IS_ERR(whole)) {
			bdput(bdev);
			return PTR_ERR(whole);
		}
	}

	res = __blkdev_get(bdev, mode, 0);

	if (whole) {
		struct gendisk *disk = whole->bd_disk;

		/* finish claiming */
		mutex_lock(&bdev->bd_mutex);
		spin_lock(&bdev_lock);

		if (!res) {
			BUG_ON(!bd_may_claim(bdev, whole, holder));
			/*
			 * Note that for a whole device bd_holders
			 * will be incremented twice, and bd_holder
			 * will be set to bd_may_claim before being
			 * set to holder
			 */
			whole->bd_holders++;
			whole->bd_holder = bd_may_claim;
			bdev->bd_holders++;
			bdev->bd_holder = holder;
		}

		/* tell others that we're done */
		BUG_ON(whole->bd_claiming != holder);
		whole->bd_claiming = NULL;
		wake_up_bit(&whole->bd_claiming, 0);

		spin_unlock(&bdev_lock);

		/*
		 * Block event polling for write claims if requested.  Any
		 * write holder makes the write_holder state stick until
		 * all are released.  This is good enough and tracking
		 * individual writeable reference is too fragile given the
		 * way @mode is used in blkdev_get/put().
		 */
		if (!res && (mode & FMODE_WRITE) && !bdev->bd_write_holder &&
		    (disk->flags & GENHD_FL_BLOCK_EVENTS_ON_EXCL_WRITE)) {
			bdev->bd_write_holder = true;
			disk_block_events(disk);
		}

		mutex_unlock(&bdev->bd_mutex);
		bdput(whole);
	}

	if (res)
		bdput(bdev);

	return res;
}
EXPORT_SYMBOL(blkdev_get);

/**
 * blkdev_get_by_path - open a block device by name
 * @path: path to the block device to open
 * @mode: FMODE_* mask
 * @holder: exclusive holder identifier
 *
 * Open the blockdevice described by the device file at @path.  @mode
 * and @holder are identical to blkdev_get().
 *
 * On success, the returned block_device has reference count of one.
 *
 * CONTEXT:
 * Might sleep.
 *
 * RETURNS:
 * Pointer to block_device on success, ERR_PTR(-errno) on failure.
 */
struct block_device *blkdev_get_by_path(const char *path, fmode_t mode,
					void *holder)
{
	struct block_device *bdev;
	int err;

	bdev = lookup_bdev(path);
	if (IS_ERR(bdev))
		return bdev;

	err = blkdev_get(bdev, mode, holder);
	if (err)
		return ERR_PTR(err);

	if ((mode & FMODE_WRITE) && bdev_read_only(bdev)) {
		blkdev_put(bdev, mode);
		return ERR_PTR(-EACCES);
	}

	return bdev;
}
EXPORT_SYMBOL(blkdev_get_by_path);

/**
 * blkdev_get_by_dev - open a block device by device number
 * @dev: device number of block device to open
 * @mode: FMODE_* mask
 * @holder: exclusive holder identifier
 *
 * Open the blockdevice described by device number @dev.  @mode and
 * @holder are identical to blkdev_get().
 *
 * Use it ONLY if you really do not have anything better - i.e. when
 * you are behind a truly sucky interface and all you are given is a
 * device number.  _Never_ to be used for internal purposes.  If you
 * ever need it - reconsider your API.
 *
 * On success, the returned block_device has reference count of one.
 *
 * CONTEXT:
 * Might sleep.
 *
 * RETURNS:
 * Pointer to block_device on success, ERR_PTR(-errno) on failure.
 */
struct block_device *blkdev_get_by_dev(dev_t dev, fmode_t mode, void *holder)
{
	struct block_device *bdev;
	int err;

	bdev = bdget(dev);
	if (!bdev)
		return ERR_PTR(-ENOMEM);

	err = blkdev_get(bdev, mode, holder);
	if (err)
		return ERR_PTR(err);

	return bdev;
}
EXPORT_SYMBOL(blkdev_get_by_dev);

static int blkdev_open(struct inode * inode, struct file * filp)
{
	struct block_device *bdev;

	/*
	 * Preserve backwards compatibility and allow large file access
	 * even if userspace doesn't ask for it explicitly. Some mkfs
	 * binary needs it. We might want to drop this workaround
	 * during an unstable branch.
	 */
	filp->f_flags |= O_LARGEFILE;

	if (filp->f_flags & O_NDELAY)
		filp->f_mode |= FMODE_NDELAY;
	if (filp->f_flags & O_EXCL)
		filp->f_mode |= FMODE_EXCL;
	if ((filp->f_flags & O_ACCMODE) == 3)
		filp->f_mode |= FMODE_WRITE_IOCTL;

	bdev = bd_acquire(inode);
	if (bdev == NULL)
		return -ENOMEM;

	res = do_open(bdev, filp, 0);
	if (res)
		return res;

	if (!(filp->f_flags & O_EXCL) )
		return 0;

	if (!(res = bd_claim(bdev, filp)))
		return 0;

	blkdev_put(bdev);
	return res;
}

static int __blkdev_put(struct block_device *bdev, int for_part)
{
	int ret = 0;
	struct inode *bd_inode = bdev->bd_inode;
	filp->f_mapping = bdev->bd_inode->i_mapping;

	return blkdev_get(bdev, filp->f_mode, filp);
}

static void __blkdev_put(struct block_device *bdev, fmode_t mode, int for_part)
{
	struct gendisk *disk = bdev->bd_disk;
	struct block_device *victim = NULL;

	/*
	 * Sync early if it looks like we're the last one.  If someone else
	 * opens the block device between now and the decrement of bd_openers
	 * then we did a sync that we didn't need to, but that's not the end
	 * of the world and we want to avoid long (could be several minute)
	 * syncs while holding the mutex.
	 */
	if (bdev->bd_openers == 1)
		sync_blockdev(bdev);

	mutex_lock_nested(&bdev->bd_mutex, for_part);
	lock_kernel();
	if (for_part)
		bdev->bd_part_count--;

	if (!--bdev->bd_openers) {
		sync_blockdev(bdev);
		kill_bdev(bdev);
	}
	if (bdev->bd_contains == bdev) {
		if (disk->fops->release)
			ret = disk->fops->release(bd_inode, NULL);
		WARN_ON_ONCE(bdev->bd_holders);
		sync_blockdev(bdev);
		kill_bdev(bdev);

		bdev_write_inode(bdev);
		/*
		 * Detaching bdev inode from its wb in __destroy_inode()
		 * is too late: the queue which embeds its bdi (along with
		 * root wb) can be gone as soon as we put_disk() below.
		 */
		inode_detach_wb(bdev->bd_inode);
	}
	if (bdev->bd_contains == bdev) {
		if (disk->fops->release)
			disk->fops->release(disk, mode);
	}
	if (!bdev->bd_openers) {
		struct module *owner = disk->fops->owner;

		put_disk(disk);
		module_put(owner);

		if (bdev->bd_contains != bdev) {
			kobject_put(&bdev->bd_part->dev.kobj);
			bdev->bd_part = NULL;
		}
		bdev->bd_disk = NULL;
		bdev->bd_inode->i_data.backing_dev_info = &default_backing_dev_info;
		if (bdev != bdev->bd_contains)
			victim = bdev->bd_contains;
		bdev->bd_contains = NULL;
	}
	unlock_kernel();
	mutex_unlock(&bdev->bd_mutex);
	bdput(bdev);
	if (victim)
		__blkdev_put(victim, 1);
	return ret;
}

int blkdev_put(struct block_device *bdev)
{
	return __blkdev_put(bdev, 0);
		disk_put_part(bdev->bd_part);
		bdev->bd_part = NULL;
		bdev->bd_disk = NULL;
		if (bdev != bdev->bd_contains)
			victim = bdev->bd_contains;
		bdev->bd_contains = NULL;

		put_disk(disk);
		module_put(owner);
	}
	mutex_unlock(&bdev->bd_mutex);
	bdput(bdev);
	if (victim)
		__blkdev_put(victim, mode, 1);
}

void blkdev_put(struct block_device *bdev, fmode_t mode)
{
	mutex_lock(&bdev->bd_mutex);

	if (mode & FMODE_EXCL) {
		bool bdev_free;

		/*
		 * Release a claim on the device.  The holder fields
		 * are protected with bdev_lock.  bd_mutex is to
		 * synchronize disk_holder unlinking.
		 */
		spin_lock(&bdev_lock);

		WARN_ON_ONCE(--bdev->bd_holders < 0);
		WARN_ON_ONCE(--bdev->bd_contains->bd_holders < 0);

		/* bd_contains might point to self, check in a separate step */
		if ((bdev_free = !bdev->bd_holders))
			bdev->bd_holder = NULL;
		if (!bdev->bd_contains->bd_holders)
			bdev->bd_contains->bd_holder = NULL;

		spin_unlock(&bdev_lock);

		/*
		 * If this was the last claim, remove holder link and
		 * unblock evpoll if it was a write holder.
		 */
		if (bdev_free && bdev->bd_write_holder) {
			disk_unblock_events(bdev->bd_disk);
			bdev->bd_write_holder = false;
		}
	}

	/*
	 * Trigger event checking and tell drivers to flush MEDIA_CHANGE
	 * event.  This is to ensure detection of media removal commanded
	 * from userland - e.g. eject(1).
	 */
	disk_flush_events(bdev->bd_disk, DISK_EVENT_MEDIA_CHANGE);

	mutex_unlock(&bdev->bd_mutex);

	__blkdev_put(bdev, mode, 0);
}
EXPORT_SYMBOL(blkdev_put);

static int blkdev_close(struct inode * inode, struct file * filp)
{
	struct block_device *bdev = I_BDEV(filp->f_mapping->host);
	if (bdev->bd_holder == filp)
		bd_release(bdev);
	return blkdev_put(bdev);
	blkdev_put(bdev, filp->f_mode);
	return 0;
}

static long block_ioctl(struct file *file, unsigned cmd, unsigned long arg)
{
	return blkdev_ioctl(file->f_mapping->host, file, cmd, arg);
	struct block_device *bdev = I_BDEV(file->f_mapping->host);
	fmode_t mode = file->f_mode;

	/*
	 * O_NDELAY can be altered using fcntl(.., F_SETFL, ..), so we have
	 * to updated it before every ioctl.
	 */
	if (file->f_flags & O_NDELAY)
		mode |= FMODE_NDELAY;
	else
		mode &= ~FMODE_NDELAY;

	return blkdev_ioctl(bdev, mode, cmd, arg);
}

/*
 * Write data to the block device.  Only intended for the block device itself
 * and the raw driver which basically is a fake block device.
 *
 * Does not take i_mutex for the write and thus is not for general purpose
 * use.
 */
ssize_t blkdev_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct inode *bd_inode = file->f_mapping->host;
	loff_t size = i_size_read(bd_inode);
	struct blk_plug plug;
	ssize_t ret;

	if (bdev_read_only(I_BDEV(bd_inode)))
		return -EPERM;

	if (!iov_iter_count(from))
		return 0;

	if (iocb->ki_pos >= size)
		return -ENOSPC;

	iov_iter_truncate(from, size - iocb->ki_pos);

	blk_start_plug(&plug);
	ret = __generic_file_write_iter(iocb, from);
	if (ret > 0) {
		ssize_t err;
		err = generic_write_sync(file, iocb->ki_pos - ret, ret);
		if (err < 0)
			ret = err;
	}
	blk_finish_plug(&plug);
	return ret;
}
EXPORT_SYMBOL_GPL(blkdev_write_iter);

ssize_t blkdev_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct file *file = iocb->ki_filp;
	struct inode *bd_inode = file->f_mapping->host;
	loff_t size = i_size_read(bd_inode);
	loff_t pos = iocb->ki_pos;

	if (pos >= size)
		return 0;

	size -= pos;
	iov_iter_truncate(to, size);
	return generic_file_read_iter(iocb, to);
}
EXPORT_SYMBOL_GPL(blkdev_read_iter);

/*
 * Try to release a page associated with block device when the system
 * is under memory pressure.
 */
static int blkdev_releasepage(struct page *page, gfp_t wait)
{
	struct super_block *super = BDEV_I(page->mapping->host)->bdev.bd_super;

	if (super && super->s_op->bdev_try_to_free_page)
		return super->s_op->bdev_try_to_free_page(super, page, wait);

	return try_to_free_buffers(page);
}

static const struct address_space_operations def_blk_aops = {
	.readpage	= blkdev_readpage,
	.writepage	= blkdev_writepage,
	.sync_page	= block_sync_page,
	.write_begin	= blkdev_write_begin,
	.write_end	= blkdev_write_end,
	.writepages	= generic_writepages,
	.direct_IO	= blkdev_direct_IO,
	.readpages	= blkdev_readpages,
	.writepage	= blkdev_writepage,
	.write_begin	= blkdev_write_begin,
	.write_end	= blkdev_write_end,
	.writepages	= generic_writepages,
	.releasepage	= blkdev_releasepage,
	.direct_IO	= blkdev_direct_IO,
	.is_dirty_writeback = buffer_check_dirty_writeback,
};

const struct file_operations def_blk_fops = {
	.open		= blkdev_open,
	.release	= blkdev_close,
	.llseek		= block_llseek,
	.read		= do_sync_read,
	.write		= do_sync_write,
  	.aio_read	= generic_file_aio_read,
  	.aio_write	= generic_file_aio_write_nolock,
	.mmap		= generic_file_mmap,
	.fsync		= block_fsync,
	.read_iter	= blkdev_read_iter,
	.write_iter	= blkdev_write_iter,
	.mmap		= generic_file_mmap,
	.fsync		= blkdev_fsync,
	.unlocked_ioctl	= block_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= compat_blkdev_ioctl,
#endif
	.splice_read	= generic_file_splice_read,
	.splice_write	= generic_file_splice_write,
	.splice_write	= iter_file_splice_write,
};

int ioctl_by_bdev(struct block_device *bdev, unsigned cmd, unsigned long arg)
{
	int res;
	mm_segment_t old_fs = get_fs();
	set_fs(KERNEL_DS);
	res = blkdev_ioctl(bdev->bd_inode, NULL, cmd, arg);
	res = blkdev_ioctl(bdev, 0, cmd, arg);
	set_fs(old_fs);
	return res;
}

EXPORT_SYMBOL(ioctl_by_bdev);

/**
 * lookup_bdev  - lookup a struct block_device by name
 *
 * @path:	special file representing the block device
 *
 * Get a reference to the blockdevice at @path in the current
 * namespace if possible and return it.  Return ERR_PTR(error)
 * otherwise.
 */
struct block_device *lookup_bdev(const char *path)
{
	struct block_device *bdev;
	struct inode *inode;
	struct nameidata nd;
	int error;

	if (!path || !*path)
		return ERR_PTR(-EINVAL);

	error = path_lookup(path, LOOKUP_FOLLOW, &nd);
	if (error)
		return ERR_PTR(error);

	inode = nd.path.dentry->d_inode;
 * @pathname:	special file representing the block device
 *
 * Get a reference to the blockdevice at @pathname in the current
 * namespace if possible and return it.  Return ERR_PTR(error)
 * otherwise.
 */
struct block_device *lookup_bdev(const char *pathname)
{
	struct block_device *bdev;
	struct inode *inode;
	struct path path;
	int error;

	if (!pathname || !*pathname)
		return ERR_PTR(-EINVAL);

	error = kern_path(pathname, LOOKUP_FOLLOW, &path);
	if (error)
		return ERR_PTR(error);

	inode = d_backing_inode(path.dentry);
	error = -ENOTBLK;
	if (!S_ISBLK(inode->i_mode))
		goto fail;
	error = -EACCES;
	if (nd.path.mnt->mnt_flags & MNT_NODEV)
	if (path.mnt->mnt_flags & MNT_NODEV)
		goto fail;
	error = -ENOMEM;
	bdev = bd_acquire(inode);
	if (!bdev)
		goto fail;
out:
	path_put(&nd.path);
	path_put(&path);
	return bdev;
fail:
	bdev = ERR_PTR(error);
	goto out;
}
EXPORT_SYMBOL(lookup_bdev);

/**
 * open_bdev_excl  -  open a block device by name and set it up for use
 *
 * @path:	special file representing the block device
 * @flags:	%MS_RDONLY for opening read-only
 * @holder:	owner for exclusion
 *
 * Open the blockdevice described by the special file at @path, claim it
 * for the @holder.
 */
struct block_device *open_bdev_excl(const char *path, int flags, void *holder)
{
	struct block_device *bdev;
	mode_t mode = FMODE_READ;
	int error = 0;

	bdev = lookup_bdev(path);
	if (IS_ERR(bdev))
		return bdev;

	if (!(flags & MS_RDONLY))
		mode |= FMODE_WRITE;
	error = blkdev_get(bdev, mode, 0);
	if (error)
		return ERR_PTR(error);
	error = -EACCES;
	if (!(flags & MS_RDONLY) && bdev_read_only(bdev))
		goto blkdev_put;
	error = bd_claim(bdev, holder);
	if (error)
		goto blkdev_put;

	return bdev;
	
blkdev_put:
	blkdev_put(bdev);
	return ERR_PTR(error);
}

EXPORT_SYMBOL(open_bdev_excl);

/**
 * close_bdev_excl  -  release a blockdevice openen by open_bdev_excl()
 *
 * @bdev:	blockdevice to close
 *
 * This is the counterpart to open_bdev_excl().
 */
void close_bdev_excl(struct block_device *bdev)
{
	bd_release(bdev);
	blkdev_put(bdev);
}

EXPORT_SYMBOL(close_bdev_excl);

int __invalidate_device(struct block_device *bdev)
int __invalidate_device(struct block_device *bdev, bool kill_dirty)
{
	struct super_block *sb = get_super(bdev);
	int res = 0;

	if (sb) {
		/*
		 * no need to lock the super, get_super holds the
		 * read mutex so the filesystem cannot go away
		 * under us (->put_super runs with the write lock
		 * hold).
		 */
		shrink_dcache_sb(sb);
		res = invalidate_inodes(sb);
		res = invalidate_inodes(sb, kill_dirty);
		drop_super(sb);
	}
	invalidate_bdev(bdev);
	return res;
}
EXPORT_SYMBOL(__invalidate_device);

void iterate_bdevs(void (*func)(struct block_device *, void *), void *arg)
{
	struct inode *inode, *old_inode = NULL;

	spin_lock(&blockdev_superblock->s_inode_list_lock);
	list_for_each_entry(inode, &blockdev_superblock->s_inodes, i_sb_list) {
		struct address_space *mapping = inode->i_mapping;
		struct block_device *bdev;

		spin_lock(&inode->i_lock);
		if (inode->i_state & (I_FREEING|I_WILL_FREE|I_NEW) ||
		    mapping->nrpages == 0) {
			spin_unlock(&inode->i_lock);
			continue;
		}
		__iget(inode);
		spin_unlock(&inode->i_lock);
		spin_unlock(&blockdev_superblock->s_inode_list_lock);
		/*
		 * We hold a reference to 'inode' so it couldn't have been
		 * removed from s_inodes list while we dropped the
		 * s_inode_list_lock  We cannot iput the inode now as we can
		 * be holding the last reference and we cannot iput it under
		 * s_inode_list_lock. So we keep the reference and iput it
		 * later.
		 */
		iput(old_inode);
		old_inode = inode;
		bdev = I_BDEV(inode);

		mutex_lock(&bdev->bd_mutex);
		if (bdev->bd_openers)
			func(bdev, arg);
		mutex_unlock(&bdev->bd_mutex);

		spin_lock(&blockdev_superblock->s_inode_list_lock);
	}
	spin_unlock(&blockdev_superblock->s_inode_list_lock);
	iput(old_inode);
}
