/*
 *  linux/fs/hfsplus/inode.c
 *
 * Copyright (C) 2001
 * Brad Boyer (flar@allandria.com)
 * (C) 2003 Ardis Technologies <roman@ardistech.com>
 *
 * Inode handling routines
 */

#include <linux/blkdev.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/mpage.h>
#include <linux/sched.h>

#include "hfsplus_fs.h"
#include "hfsplus_raw.h"
#include <linux/uio.h>

#include "hfsplus_fs.h"
#include "hfsplus_raw.h"
#include "xattr.h"
#include "acl.h"

static int hfsplus_readpage(struct file *file, struct page *page)
{
	return block_read_full_page(page, hfsplus_get_block);
}

static int hfsplus_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page, hfsplus_get_block, wbc);
}

static void hfsplus_write_failed(struct address_space *mapping, loff_t to)
{
	struct inode *inode = mapping->host;

	if (to > inode->i_size) {
		truncate_pagecache(inode, inode->i_size);
		hfsplus_file_truncate(inode);
	}
}

static int hfsplus_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned flags,
			struct page **pagep, void **fsdata)
{
	*pagep = NULL;
	return cont_write_begin(file, mapping, pos, len, flags, pagep, fsdata,
				hfsplus_get_block,
				&HFSPLUS_I(mapping->host).phys_size);
	int ret;

	*pagep = NULL;
	ret = cont_write_begin(file, mapping, pos, len, flags, pagep, fsdata,
				hfsplus_get_block,
				&HFSPLUS_I(mapping->host)->phys_size);
	if (unlikely(ret))
		hfsplus_write_failed(mapping, pos + len);

	return ret;
}

static sector_t hfsplus_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping, block, hfsplus_get_block);
}

static int hfsplus_releasepage(struct page *page, gfp_t mask)
{
	struct inode *inode = page->mapping->host;
	struct super_block *sb = inode->i_sb;
	struct hfs_btree *tree;
	struct hfs_bnode *node;
	u32 nidx;
	int i, res = 1;

	switch (inode->i_ino) {
	case HFSPLUS_EXT_CNID:
		tree = HFSPLUS_SB(sb).ext_tree;
		break;
	case HFSPLUS_CAT_CNID:
		tree = HFSPLUS_SB(sb).cat_tree;
		break;
	case HFSPLUS_ATTR_CNID:
		tree = HFSPLUS_SB(sb).attr_tree;
		tree = HFSPLUS_SB(sb)->ext_tree;
		break;
	case HFSPLUS_CAT_CNID:
		tree = HFSPLUS_SB(sb)->cat_tree;
		break;
	case HFSPLUS_ATTR_CNID:
		tree = HFSPLUS_SB(sb)->attr_tree;
		break;
	default:
		BUG();
		return 0;
	}
	if (!tree)
		return 0;
	if (tree->node_size >= PAGE_CACHE_SIZE) {
		nidx = page->index >> (tree->node_size_shift - PAGE_CACHE_SHIFT);
		nidx = page->index >>
			(tree->node_size_shift - PAGE_CACHE_SHIFT);
		spin_lock(&tree->hash_lock);
		node = hfs_bnode_findhash(tree, nidx);
		if (!node)
			;
		else if (atomic_read(&node->refcnt))
			res = 0;
		if (res && node) {
			hfs_bnode_unhash(node);
			hfs_bnode_free(node);
		}
		spin_unlock(&tree->hash_lock);
	} else {
		nidx = page->index << (PAGE_CACHE_SHIFT - tree->node_size_shift);
		nidx = page->index <<
			(PAGE_CACHE_SHIFT - tree->node_size_shift);
		i = 1 << (PAGE_CACHE_SHIFT - tree->node_size_shift);
		spin_lock(&tree->hash_lock);
		do {
			node = hfs_bnode_findhash(tree, nidx++);
			if (!node)
				continue;
			if (atomic_read(&node->refcnt)) {
				res = 0;
				break;
			}
			hfs_bnode_unhash(node);
			hfs_bnode_free(node);
		} while (--i && nidx < tree->node_count);
		spin_unlock(&tree->hash_lock);
	}
	return res ? try_to_free_buffers(page) : 0;
}

static ssize_t hfsplus_direct_IO(int rw, struct kiocb *iocb,
		const struct iovec *iov, loff_t offset, unsigned long nr_segs)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_path.dentry->d_inode->i_mapping->host;

	return blockdev_direct_IO(rw, iocb, inode, inode->i_sb->s_bdev, iov,
				  offset, nr_segs, hfsplus_get_block, NULL);
static ssize_t hfsplus_direct_IO(struct kiocb *iocb, struct iov_iter *iter,
				 loff_t offset)
{
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = file_inode(file)->i_mapping->host;
	size_t count = iov_iter_count(iter);
	ssize_t ret;

	ret = blockdev_direct_IO(iocb, inode, iter, offset, hfsplus_get_block);

	/*
	 * In case of error extending write may have instantiated a few
	 * blocks outside i_size. Trim these off again.
	 */
	if (unlikely(iov_iter_rw(iter) == WRITE && ret < 0)) {
		loff_t isize = i_size_read(inode);
		loff_t end = offset + count;

		if (end > isize)
			hfsplus_write_failed(mapping, end);
	}

	return ret;
}

static int hfsplus_writepages(struct address_space *mapping,
			      struct writeback_control *wbc)
{
	return mpage_writepages(mapping, wbc, hfsplus_get_block);
}

const struct address_space_operations hfsplus_btree_aops = {
	.readpage	= hfsplus_readpage,
	.writepage	= hfsplus_writepage,
	.sync_page	= block_sync_page,
	.write_begin	= hfsplus_write_begin,
	.write_end	= generic_write_end,
	.bmap		= hfsplus_bmap,
	.releasepage	= hfsplus_releasepage,
};

const struct address_space_operations hfsplus_aops = {
	.readpage	= hfsplus_readpage,
	.writepage	= hfsplus_writepage,
	.sync_page	= block_sync_page,
	.write_begin	= hfsplus_write_begin,
	.write_end	= generic_write_end,
	.bmap		= hfsplus_bmap,
	.direct_IO	= hfsplus_direct_IO,
	.writepages	= hfsplus_writepages,
};

struct dentry_operations hfsplus_dentry_operations = {
const struct dentry_operations hfsplus_dentry_operations = {
	.d_hash       = hfsplus_hash_dentry,
	.d_compare    = hfsplus_compare_dentry,
};

static struct dentry *hfsplus_file_lookup(struct inode *dir, struct dentry *dentry,
					  struct nameidata *nd)
{
	struct hfs_find_data fd;
	struct super_block *sb = dir->i_sb;
	struct inode *inode = NULL;
	int err;

	if (HFSPLUS_IS_RSRC(dir) || strcmp(dentry->d_name.name, "rsrc"))
		goto out;

	inode = HFSPLUS_I(dir).rsrc_inode;
	if (inode)
		goto out;

	inode = new_inode(sb);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	inode->i_ino = dir->i_ino;
	INIT_LIST_HEAD(&HFSPLUS_I(inode).open_dir_list);
	mutex_init(&HFSPLUS_I(inode).extents_lock);
	HFSPLUS_I(inode).flags = HFSPLUS_FLG_RSRC;

	hfs_find_init(HFSPLUS_SB(sb).cat_tree, &fd);
	err = hfsplus_find_cat(sb, dir->i_ino, &fd);
	if (!err)
		err = hfsplus_cat_read_inode(inode, &fd);
	hfs_find_exit(&fd);
	if (err) {
		iput(inode);
		return ERR_PTR(err);
	}
	HFSPLUS_I(inode).rsrc_inode = dir;
	HFSPLUS_I(dir).rsrc_inode = inode;
	igrab(dir);
	hlist_add_head(&inode->i_hash, &HFSPLUS_SB(sb).rsrc_inodes);
	mark_inode_dirty(inode);
out:
	d_add(dentry, inode);
	return NULL;
}

static void hfsplus_get_perms(struct inode *inode, struct hfsplus_perm *perms, int dir)
{
	struct super_block *sb = inode->i_sb;
static void hfsplus_get_perms(struct inode *inode,
		struct hfsplus_perm *perms, int dir)
{
	struct hfsplus_sb_info *sbi = HFSPLUS_SB(inode->i_sb);
	u16 mode;

	mode = be16_to_cpu(perms->mode);

	inode->i_uid = be32_to_cpu(perms->owner);
	if (!inode->i_uid && !mode)
		inode->i_uid = HFSPLUS_SB(sb).uid;

	inode->i_gid = be32_to_cpu(perms->group);
	if (!inode->i_gid && !mode)
		inode->i_gid = HFSPLUS_SB(sb).gid;

	if (dir) {
		mode = mode ? (mode & S_IALLUGO) :
			(S_IRWXUGO & ~(HFSPLUS_SB(sb).umask));
		mode |= S_IFDIR;
	} else if (!mode)
		mode = S_IFREG | ((S_IRUGO|S_IWUGO) &
			~(HFSPLUS_SB(sb).umask));
	inode->i_mode = mode;

	HFSPLUS_I(inode).rootflags = perms->rootflags;
	HFSPLUS_I(inode).userflags = perms->userflags;
	i_uid_write(inode, be32_to_cpu(perms->owner));
	if ((test_bit(HFSPLUS_SB_UID, &sbi->flags)) || (!i_uid_read(inode) && !mode))
		inode->i_uid = sbi->uid;

	i_gid_write(inode, be32_to_cpu(perms->group));
	if ((test_bit(HFSPLUS_SB_GID, &sbi->flags)) || (!i_gid_read(inode) && !mode))
		inode->i_gid = sbi->gid;

	if (dir) {
		mode = mode ? (mode & S_IALLUGO) : (S_IRWXUGO & ~(sbi->umask));
		mode |= S_IFDIR;
	} else if (!mode)
		mode = S_IFREG | ((S_IRUGO|S_IWUGO) & ~(sbi->umask));
	inode->i_mode = mode;

	HFSPLUS_I(inode)->userflags = perms->userflags;
	if (perms->rootflags & HFSPLUS_FLG_IMMUTABLE)
		inode->i_flags |= S_IMMUTABLE;
	else
		inode->i_flags &= ~S_IMMUTABLE;
	if (perms->rootflags & HFSPLUS_FLG_APPEND)
		inode->i_flags |= S_APPEND;
	else
		inode->i_flags &= ~S_APPEND;
}

static void hfsplus_set_perms(struct inode *inode, struct hfsplus_perm *perms)
{
	if (inode->i_flags & S_IMMUTABLE)
		perms->rootflags |= HFSPLUS_FLG_IMMUTABLE;
	else
		perms->rootflags &= ~HFSPLUS_FLG_IMMUTABLE;
	if (inode->i_flags & S_APPEND)
		perms->rootflags |= HFSPLUS_FLG_APPEND;
	else
		perms->rootflags &= ~HFSPLUS_FLG_APPEND;
	perms->userflags = HFSPLUS_I(inode).userflags;
	perms->mode = cpu_to_be16(inode->i_mode);
	perms->owner = cpu_to_be32(inode->i_uid);
	perms->group = cpu_to_be32(inode->i_gid);
	perms->dev = cpu_to_be32(HFSPLUS_I(inode).dev);
}

static int hfsplus_permission(struct inode *inode, int mask)
{
	/* MAY_EXEC is also used for lookup, if no x bit is set allow lookup,
	 * open_exec has the same test, so it's still not executable, if a x bit
	 * is set fall back to standard permission check.
	 */
	if (S_ISREG(inode->i_mode) && mask & MAY_EXEC && !(inode->i_mode & 0111))
		return 0;
	return generic_permission(inode, mask, NULL);
}


static int hfsplus_file_open(struct inode *inode, struct file *file)
{
	if (HFSPLUS_IS_RSRC(inode))
		inode = HFSPLUS_I(inode).rsrc_inode;
	atomic_inc(&HFSPLUS_I(inode).opencnt);
static int hfsplus_file_open(struct inode *inode, struct file *file)
{
	if (HFSPLUS_IS_RSRC(inode))
		inode = HFSPLUS_I(inode)->rsrc_inode;
	if (!(file->f_flags & O_LARGEFILE) && i_size_read(inode) > MAX_NON_LFS)
		return -EOVERFLOW;
	atomic_inc(&HFSPLUS_I(inode)->opencnt);
	return 0;
}

static int hfsplus_file_release(struct inode *inode, struct file *file)
{
	struct super_block *sb = inode->i_sb;

	if (HFSPLUS_IS_RSRC(inode))
		inode = HFSPLUS_I(inode).rsrc_inode;
	if (atomic_dec_and_test(&HFSPLUS_I(inode).opencnt)) {
		mutex_lock(&inode->i_mutex);
		hfsplus_file_truncate(inode);
		if (inode->i_flags & S_DEAD) {
			hfsplus_delete_cat(inode->i_ino, HFSPLUS_SB(sb).hidden_dir, NULL);
		inode = HFSPLUS_I(inode)->rsrc_inode;
	if (atomic_dec_and_test(&HFSPLUS_I(inode)->opencnt)) {
		mutex_lock(&inode->i_mutex);
		hfsplus_file_truncate(inode);
		if (inode->i_flags & S_DEAD) {
			hfsplus_delete_cat(inode->i_ino,
					   HFSPLUS_SB(sb)->hidden_dir, NULL);
			hfsplus_delete_inode(inode);
		}
		mutex_unlock(&inode->i_mutex);
	}
	return 0;
}

static const struct inode_operations hfsplus_file_inode_operations = {
	.lookup		= hfsplus_file_lookup,
	.truncate	= hfsplus_file_truncate,
	.permission	= hfsplus_permission,
	.setxattr	= hfsplus_setxattr,
	.getxattr	= hfsplus_getxattr,
	.listxattr	= hfsplus_listxattr,
};

static const struct file_operations hfsplus_file_operations = {
	.llseek 	= generic_file_llseek,
	.read		= do_sync_read,
	.aio_read	= generic_file_aio_read,
	.write		= do_sync_write,
	.aio_write	= generic_file_aio_write,
	.mmap		= generic_file_mmap,
	.splice_read	= generic_file_splice_read,
	.fsync		= file_fsync,
	.open		= hfsplus_file_open,
	.release	= hfsplus_file_release,
	.ioctl          = hfsplus_ioctl,
};

struct inode *hfsplus_new_inode(struct super_block *sb, int mode)
{
	struct inode *inode = new_inode(sb);
	if (!inode)
		return NULL;

	inode->i_ino = HFSPLUS_SB(sb).next_cnid++;
	inode->i_mode = mode;
	inode->i_uid = current->fsuid;
	inode->i_gid = current->fsgid;
	inode->i_nlink = 1;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME_SEC;
	INIT_LIST_HEAD(&HFSPLUS_I(inode).open_dir_list);
	mutex_init(&HFSPLUS_I(inode).extents_lock);
	atomic_set(&HFSPLUS_I(inode).opencnt, 0);
	HFSPLUS_I(inode).flags = 0;
	memset(HFSPLUS_I(inode).first_extents, 0, sizeof(hfsplus_extent_rec));
	memset(HFSPLUS_I(inode).cached_extents, 0, sizeof(hfsplus_extent_rec));
	HFSPLUS_I(inode).alloc_blocks = 0;
	HFSPLUS_I(inode).first_blocks = 0;
	HFSPLUS_I(inode).cached_start = 0;
	HFSPLUS_I(inode).cached_blocks = 0;
	HFSPLUS_I(inode).phys_size = 0;
	HFSPLUS_I(inode).fs_blocks = 0;
	HFSPLUS_I(inode).rsrc_inode = NULL;
	if (S_ISDIR(inode->i_mode)) {
		inode->i_size = 2;
		HFSPLUS_SB(sb).folder_count++;
		inode->i_op = &hfsplus_dir_inode_operations;
		inode->i_fop = &hfsplus_dir_operations;
	} else if (S_ISREG(inode->i_mode)) {
		HFSPLUS_SB(sb).file_count++;
		inode->i_op = &hfsplus_file_inode_operations;
		inode->i_fop = &hfsplus_file_operations;
		inode->i_mapping->a_ops = &hfsplus_aops;
		HFSPLUS_I(inode).clump_blocks = HFSPLUS_SB(sb).data_clump_blocks;
	} else if (S_ISLNK(inode->i_mode)) {
		HFSPLUS_SB(sb).file_count++;
		inode->i_op = &page_symlink_inode_operations;
		inode->i_mapping->a_ops = &hfsplus_aops;
		HFSPLUS_I(inode).clump_blocks = 1;
	} else
		HFSPLUS_SB(sb).file_count++;
	insert_inode_hash(inode);
	mark_inode_dirty(inode);
	sb->s_dirt = 1;
static int hfsplus_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = d_inode(dentry);
	int error;

	error = inode_change_ok(inode, attr);
	if (error)
		return error;

	if ((attr->ia_valid & ATTR_SIZE) &&
	    attr->ia_size != i_size_read(inode)) {
		inode_dio_wait(inode);
		if (attr->ia_size > inode->i_size) {
			error = generic_cont_expand_simple(inode,
							   attr->ia_size);
			if (error)
				return error;
		}
		truncate_setsize(inode, attr->ia_size);
		hfsplus_file_truncate(inode);
	}

	setattr_copy(inode, attr);
	mark_inode_dirty(inode);

	if (attr->ia_valid & ATTR_MODE) {
		error = posix_acl_chmod(inode, inode->i_mode);
		if (unlikely(error))
			return error;
	}

	return 0;
}

int hfsplus_file_fsync(struct file *file, loff_t start, loff_t end,
		       int datasync)
{
	struct inode *inode = file->f_mapping->host;
	struct hfsplus_inode_info *hip = HFSPLUS_I(inode);
	struct hfsplus_sb_info *sbi = HFSPLUS_SB(inode->i_sb);
	int error = 0, error2;

	error = filemap_write_and_wait_range(inode->i_mapping, start, end);
	if (error)
		return error;
	mutex_lock(&inode->i_mutex);

	/*
	 * Sync inode metadata into the catalog and extent trees.
	 */
	sync_inode_metadata(inode, 1);

	/*
	 * And explicitly write out the btrees.
	 */
	if (test_and_clear_bit(HFSPLUS_I_CAT_DIRTY, &hip->flags))
		error = filemap_write_and_wait(sbi->cat_tree->inode->i_mapping);

	if (test_and_clear_bit(HFSPLUS_I_EXT_DIRTY, &hip->flags)) {
		error2 =
			filemap_write_and_wait(sbi->ext_tree->inode->i_mapping);
		if (!error)
			error = error2;
	}

	if (test_and_clear_bit(HFSPLUS_I_ATTR_DIRTY, &hip->flags)) {
		if (sbi->attr_tree) {
			error2 =
				filemap_write_and_wait(
					    sbi->attr_tree->inode->i_mapping);
			if (!error)
				error = error2;
		} else {
			pr_err("sync non-existent attributes tree\n");
		}
	}

	if (test_and_clear_bit(HFSPLUS_I_ALLOC_DIRTY, &hip->flags)) {
		error2 = filemap_write_and_wait(sbi->alloc_file->i_mapping);
		if (!error)
			error = error2;
	}

	if (!test_bit(HFSPLUS_SB_NOBARRIER, &sbi->flags))
		blkdev_issue_flush(inode->i_sb->s_bdev, GFP_KERNEL, NULL);

	mutex_unlock(&inode->i_mutex);

	return error;
}

static const struct inode_operations hfsplus_file_inode_operations = {
	.setattr	= hfsplus_setattr,
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.listxattr	= hfsplus_listxattr,
	.removexattr	= generic_removexattr,
#ifdef CONFIG_HFSPLUS_FS_POSIX_ACL
	.get_acl	= hfsplus_get_posix_acl,
	.set_acl	= hfsplus_set_posix_acl,
#endif
};

static const struct file_operations hfsplus_file_operations = {
	.llseek		= generic_file_llseek,
	.read_iter	= generic_file_read_iter,
	.write_iter	= generic_file_write_iter,
	.mmap		= generic_file_mmap,
	.splice_read	= generic_file_splice_read,
	.fsync		= hfsplus_file_fsync,
	.open		= hfsplus_file_open,
	.release	= hfsplus_file_release,
	.unlocked_ioctl = hfsplus_ioctl,
};

struct inode *hfsplus_new_inode(struct super_block *sb, umode_t mode)
{
	struct hfsplus_sb_info *sbi = HFSPLUS_SB(sb);
	struct inode *inode = new_inode(sb);
	struct hfsplus_inode_info *hip;

	if (!inode)
		return NULL;

	inode->i_ino = sbi->next_cnid++;
	inode->i_mode = mode;
	inode->i_uid = current_fsuid();
	inode->i_gid = current_fsgid();
	set_nlink(inode, 1);
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME_SEC;

	hip = HFSPLUS_I(inode);
	INIT_LIST_HEAD(&hip->open_dir_list);
	mutex_init(&hip->extents_lock);
	atomic_set(&hip->opencnt, 0);
	hip->extent_state = 0;
	hip->flags = 0;
	hip->userflags = 0;
	hip->subfolders = 0;
	memset(hip->first_extents, 0, sizeof(hfsplus_extent_rec));
	memset(hip->cached_extents, 0, sizeof(hfsplus_extent_rec));
	hip->alloc_blocks = 0;
	hip->first_blocks = 0;
	hip->cached_start = 0;
	hip->cached_blocks = 0;
	hip->phys_size = 0;
	hip->fs_blocks = 0;
	hip->rsrc_inode = NULL;
	if (S_ISDIR(inode->i_mode)) {
		inode->i_size = 2;
		sbi->folder_count++;
		inode->i_op = &hfsplus_dir_inode_operations;
		inode->i_fop = &hfsplus_dir_operations;
	} else if (S_ISREG(inode->i_mode)) {
		sbi->file_count++;
		inode->i_op = &hfsplus_file_inode_operations;
		inode->i_fop = &hfsplus_file_operations;
		inode->i_mapping->a_ops = &hfsplus_aops;
		hip->clump_blocks = sbi->data_clump_blocks;
	} else if (S_ISLNK(inode->i_mode)) {
		sbi->file_count++;
		inode->i_op = &page_symlink_inode_operations;
		inode->i_mapping->a_ops = &hfsplus_aops;
		hip->clump_blocks = 1;
	} else
		sbi->file_count++;
	insert_inode_hash(inode);
	mark_inode_dirty(inode);
	hfsplus_mark_mdb_dirty(sb);

	return inode;
}

void hfsplus_delete_inode(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;

	if (S_ISDIR(inode->i_mode)) {
		HFSPLUS_SB(sb).folder_count--;
		sb->s_dirt = 1;
		return;
	}
	HFSPLUS_SB(sb).file_count--;
		HFSPLUS_SB(sb)->folder_count--;
		hfsplus_mark_mdb_dirty(sb);
		return;
	}
	HFSPLUS_SB(sb)->file_count--;
	if (S_ISREG(inode->i_mode)) {
		if (!inode->i_nlink) {
			inode->i_size = 0;
			hfsplus_file_truncate(inode);
		}
	} else if (S_ISLNK(inode->i_mode)) {
		inode->i_size = 0;
		hfsplus_file_truncate(inode);
	}
	sb->s_dirt = 1;
	hfsplus_mark_mdb_dirty(sb);
}

void hfsplus_inode_read_fork(struct inode *inode, struct hfsplus_fork_raw *fork)
{
	struct super_block *sb = inode->i_sb;
	u32 count;
	int i;

	memcpy(&HFSPLUS_I(inode).first_extents, &fork->extents,
	       sizeof(hfsplus_extent_rec));
	for (count = 0, i = 0; i < 8; i++)
		count += be32_to_cpu(fork->extents[i].block_count);
	HFSPLUS_I(inode).first_blocks = count;
	memset(HFSPLUS_I(inode).cached_extents, 0, sizeof(hfsplus_extent_rec));
	HFSPLUS_I(inode).cached_start = 0;
	HFSPLUS_I(inode).cached_blocks = 0;

	HFSPLUS_I(inode).alloc_blocks = be32_to_cpu(fork->total_blocks);
	inode->i_size = HFSPLUS_I(inode).phys_size = be64_to_cpu(fork->total_size);
	HFSPLUS_I(inode).fs_blocks = (inode->i_size + sb->s_blocksize - 1) >> sb->s_blocksize_bits;
	inode_set_bytes(inode, HFSPLUS_I(inode).fs_blocks << sb->s_blocksize_bits);
	HFSPLUS_I(inode).clump_blocks = be32_to_cpu(fork->clump_size) >> HFSPLUS_SB(sb).alloc_blksz_shift;
	if (!HFSPLUS_I(inode).clump_blocks)
		HFSPLUS_I(inode).clump_blocks = HFSPLUS_IS_RSRC(inode) ? HFSPLUS_SB(sb).rsrc_clump_blocks :
				HFSPLUS_SB(sb).data_clump_blocks;
}

void hfsplus_inode_write_fork(struct inode *inode, struct hfsplus_fork_raw *fork)
{
	memcpy(&fork->extents, &HFSPLUS_I(inode).first_extents,
	       sizeof(hfsplus_extent_rec));
	fork->total_size = cpu_to_be64(inode->i_size);
	fork->total_blocks = cpu_to_be32(HFSPLUS_I(inode).alloc_blocks);
	struct hfsplus_sb_info *sbi = HFSPLUS_SB(sb);
	struct hfsplus_inode_info *hip = HFSPLUS_I(inode);
	u32 count;
	int i;

	memcpy(&hip->first_extents, &fork->extents, sizeof(hfsplus_extent_rec));
	for (count = 0, i = 0; i < 8; i++)
		count += be32_to_cpu(fork->extents[i].block_count);
	hip->first_blocks = count;
	memset(hip->cached_extents, 0, sizeof(hfsplus_extent_rec));
	hip->cached_start = 0;
	hip->cached_blocks = 0;

	hip->alloc_blocks = be32_to_cpu(fork->total_blocks);
	hip->phys_size = inode->i_size = be64_to_cpu(fork->total_size);
	hip->fs_blocks =
		(inode->i_size + sb->s_blocksize - 1) >> sb->s_blocksize_bits;
	inode_set_bytes(inode, hip->fs_blocks << sb->s_blocksize_bits);
	hip->clump_blocks =
		be32_to_cpu(fork->clump_size) >> sbi->alloc_blksz_shift;
	if (!hip->clump_blocks) {
		hip->clump_blocks = HFSPLUS_IS_RSRC(inode) ?
			sbi->rsrc_clump_blocks :
			sbi->data_clump_blocks;
	}
}

void hfsplus_inode_write_fork(struct inode *inode,
		struct hfsplus_fork_raw *fork)
{
	memcpy(&fork->extents, &HFSPLUS_I(inode)->first_extents,
	       sizeof(hfsplus_extent_rec));
	fork->total_size = cpu_to_be64(inode->i_size);
	fork->total_blocks = cpu_to_be32(HFSPLUS_I(inode)->alloc_blocks);
}

int hfsplus_cat_read_inode(struct inode *inode, struct hfs_find_data *fd)
{
	hfsplus_cat_entry entry;
	int res = 0;
	u16 type;

	type = hfs_bnode_read_u16(fd->bnode, fd->entryoffset);

	HFSPLUS_I(inode).dev = 0;
	HFSPLUS_I(inode)->linkid = 0;
	if (type == HFSPLUS_FOLDER) {
		struct hfsplus_cat_folder *folder = &entry.folder;

		if (fd->entrylength < sizeof(struct hfsplus_cat_folder)) {
			pr_err("bad catalog folder entry\n");
			res = -EIO;
			goto out;
		}
		hfs_bnode_read(fd->bnode, &entry, fd->entryoffset,
					sizeof(struct hfsplus_cat_folder));
		hfsplus_get_perms(inode, &folder->permissions, 1);
		inode->i_nlink = 1;
		set_nlink(inode, 1);
		inode->i_size = 2 + be32_to_cpu(folder->valence);
		inode->i_atime = hfsp_mt2ut(folder->access_date);
		inode->i_mtime = hfsp_mt2ut(folder->content_mod_date);
		inode->i_ctime = hfsp_mt2ut(folder->attribute_mod_date);
		HFSPLUS_I(inode).create_date = folder->create_date;
		HFSPLUS_I(inode).fs_blocks = 0;
		HFSPLUS_I(inode)->create_date = folder->create_date;
		HFSPLUS_I(inode)->fs_blocks = 0;
		if (folder->flags & cpu_to_be16(HFSPLUS_HAS_FOLDER_COUNT)) {
			HFSPLUS_I(inode)->subfolders =
				be32_to_cpu(folder->subfolders);
		}
		inode->i_op = &hfsplus_dir_inode_operations;
		inode->i_fop = &hfsplus_dir_operations;
	} else if (type == HFSPLUS_FILE) {
		struct hfsplus_cat_file *file = &entry.file;

		if (fd->entrylength < sizeof(struct hfsplus_cat_file)) {
			pr_err("bad catalog file entry\n");
			res = -EIO;
			goto out;
		}
		hfs_bnode_read(fd->bnode, &entry, fd->entryoffset,
					sizeof(struct hfsplus_cat_file));

		hfsplus_inode_read_fork(inode, HFSPLUS_IS_DATA(inode) ?
					&file->data_fork : &file->rsrc_fork);
		hfsplus_get_perms(inode, &file->permissions, 0);
		inode->i_nlink = 1;
		if (S_ISREG(inode->i_mode)) {
			if (file->permissions.dev)
				inode->i_nlink = be32_to_cpu(file->permissions.dev);
		hfsplus_inode_read_fork(inode, HFSPLUS_IS_RSRC(inode) ?
					&file->rsrc_fork : &file->data_fork);
		hfsplus_get_perms(inode, &file->permissions, 0);
		set_nlink(inode, 1);
		if (S_ISREG(inode->i_mode)) {
			if (file->permissions.dev)
				set_nlink(inode,
					  be32_to_cpu(file->permissions.dev));
			inode->i_op = &hfsplus_file_inode_operations;
			inode->i_fop = &hfsplus_file_operations;
			inode->i_mapping->a_ops = &hfsplus_aops;
		} else if (S_ISLNK(inode->i_mode)) {
			inode->i_op = &page_symlink_inode_operations;
			inode->i_mapping->a_ops = &hfsplus_aops;
		} else {
			init_special_inode(inode, inode->i_mode,
					   be32_to_cpu(file->permissions.dev));
		}
		inode->i_atime = hfsp_mt2ut(file->access_date);
		inode->i_mtime = hfsp_mt2ut(file->content_mod_date);
		inode->i_ctime = hfsp_mt2ut(file->attribute_mod_date);
		HFSPLUS_I(inode).create_date = file->create_date;
	} else {
		printk(KERN_ERR "hfs: bad catalog entry used to create inode\n");
		HFSPLUS_I(inode)->create_date = file->create_date;
	} else {
		pr_err("bad catalog entry used to create inode\n");
		res = -EIO;
	}
out:
	return res;
}

int hfsplus_cat_write_inode(struct inode *inode)
{
	struct inode *main_inode = inode;
	struct hfs_find_data fd;
	hfsplus_cat_entry entry;
	int res = 0;

	if (HFSPLUS_IS_RSRC(inode))
		main_inode = HFSPLUS_I(inode).rsrc_inode;
		main_inode = HFSPLUS_I(inode)->rsrc_inode;

	if (!main_inode->i_nlink)
		return 0;

	if (hfs_find_init(HFSPLUS_SB(main_inode->i_sb).cat_tree, &fd))
	if (hfs_find_init(HFSPLUS_SB(main_inode->i_sb)->cat_tree, &fd))
		/* panic? */
		return -EIO;

	if (hfsplus_find_cat(main_inode->i_sb, main_inode->i_ino, &fd))
		/* panic? */
		goto out;

	if (S_ISDIR(main_inode->i_mode)) {
		struct hfsplus_cat_folder *folder = &entry.folder;

		if (fd.entrylength < sizeof(struct hfsplus_cat_folder)) {
			pr_err("bad catalog folder entry\n");
			res = -EIO;
			goto out;
		}
		hfs_bnode_read(fd.bnode, &entry, fd.entryoffset,
					sizeof(struct hfsplus_cat_folder));
		/* simple node checks? */
		hfsplus_set_perms(inode, &folder->permissions);
		hfsplus_cat_set_perms(inode, &folder->permissions);
		folder->access_date = hfsp_ut2mt(inode->i_atime);
		folder->content_mod_date = hfsp_ut2mt(inode->i_mtime);
		folder->attribute_mod_date = hfsp_ut2mt(inode->i_ctime);
		folder->valence = cpu_to_be32(inode->i_size - 2);
		if (folder->flags & cpu_to_be16(HFSPLUS_HAS_FOLDER_COUNT)) {
			folder->subfolders =
				cpu_to_be32(HFSPLUS_I(inode)->subfolders);
		}
		hfs_bnode_write(fd.bnode, &entry, fd.entryoffset,
					 sizeof(struct hfsplus_cat_folder));
	} else if (HFSPLUS_IS_RSRC(inode)) {
		struct hfsplus_cat_file *file = &entry.file;
		hfs_bnode_read(fd.bnode, &entry, fd.entryoffset,
			       sizeof(struct hfsplus_cat_file));
		hfsplus_inode_write_fork(inode, &file->rsrc_fork);
		hfs_bnode_write(fd.bnode, &entry, fd.entryoffset,
				sizeof(struct hfsplus_cat_file));
	} else {
		struct hfsplus_cat_file *file = &entry.file;

		if (fd.entrylength < sizeof(struct hfsplus_cat_file)) {
			pr_err("bad catalog file entry\n");
			res = -EIO;
			goto out;
		}
		hfs_bnode_read(fd.bnode, &entry, fd.entryoffset,
					sizeof(struct hfsplus_cat_file));
		hfsplus_inode_write_fork(inode, &file->data_fork);
		if (S_ISREG(inode->i_mode))
			HFSPLUS_I(inode).dev = inode->i_nlink;
		if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
			HFSPLUS_I(inode).dev = kdev_t_to_nr(inode->i_rdev);
		hfsplus_set_perms(inode, &file->permissions);
		if ((file->permissions.rootflags | file->permissions.userflags) & HFSPLUS_FLG_IMMUTABLE)
		hfsplus_cat_set_perms(inode, &file->permissions);
		if (HFSPLUS_FLG_IMMUTABLE &
				(file->permissions.rootflags |
					file->permissions.userflags))
			file->flags |= cpu_to_be16(HFSPLUS_FILE_LOCKED);
		else
			file->flags &= cpu_to_be16(~HFSPLUS_FILE_LOCKED);
		file->access_date = hfsp_ut2mt(inode->i_atime);
		file->content_mod_date = hfsp_ut2mt(inode->i_mtime);
		file->attribute_mod_date = hfsp_ut2mt(inode->i_ctime);
		hfs_bnode_write(fd.bnode, &entry, fd.entryoffset,
					 sizeof(struct hfsplus_cat_file));
	}

	set_bit(HFSPLUS_I_CAT_DIRTY, &HFSPLUS_I(inode)->flags);
out:
	hfs_find_exit(&fd);
	return res;
}
