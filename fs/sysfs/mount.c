/*
 * fs/sysfs/symlink.c - operations for initializing and mounting sysfs
 *
 * Copyright (c) 2001-3 Patrick Mochel
 * Copyright (c) 2007 SUSE Linux Products GmbH
 * Copyright (c) 2007 Tejun Heo <teheo@suse.de>
 *
 * This file is released under the GPLv2.
 *
 * Please see Documentation/filesystems/sysfs.txt for more information.
 */

#define DEBUG 

#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/init.h>

#include "sysfs.h"

/* Random magic number */
#define SYSFS_MAGIC 0x62656572

static struct vfsmount *sysfs_mount;
struct super_block * sysfs_sb = NULL;
struct kmem_cache *sysfs_dir_cachep;

static const struct super_operations sysfs_ops = {
	.statfs		= simple_statfs,
	.drop_inode	= generic_delete_inode,
};

struct sysfs_dirent sysfs_root = {
	.s_name		= "",
	.s_count	= ATOMIC_INIT(1),
	.s_flags	= SYSFS_DIR,
	.s_mode		= S_IFDIR | S_IRWXU | S_IRUGO | S_IXUGO,
	.s_ino		= 1,
};

static int sysfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode *inode;
	struct dentry *root;

	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = SYSFS_MAGIC;
	sb->s_op = &sysfs_ops;
	sb->s_time_gran = 1;
	sysfs_sb = sb;

	/* get root inode, initialize and unlock it */
	inode = sysfs_get_inode(&sysfs_root);
	if (!inode) {
		pr_debug("sysfs: could not get root inode\n");
		return -ENOMEM;
	}

	/* instantiate and link root dentry */
	root = d_alloc_root(inode);
	if (!root) {
		pr_debug("%s: could not get root dentry!\n",__func__);
		iput(inode);
		return -ENOMEM;
	}
	root->d_fsdata = &sysfs_root;
	sb->s_root = root;
	return 0;
}

static int sysfs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
	return get_sb_single(fs_type, flags, data, sysfs_fill_super, mnt);
#define DEBUG

#include <linux/fs.h>
#include <linux/magic.h>
#include <linux/mount.h>
#include <linux/init.h>
#include <linux/user_namespace.h>

#include "sysfs.h"

static struct kernfs_root *sysfs_root;
struct kernfs_node *sysfs_root_kn;

static struct dentry *sysfs_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	struct dentry *root;
	void *ns;
	bool new_sb;

	if (!(flags & MS_KERNMOUNT)) {
		if (!kobj_ns_current_may_mount(KOBJ_NS_TYPE_NET))
			return ERR_PTR(-EPERM);
	}

	ns = kobj_ns_grab_current(KOBJ_NS_TYPE_NET);
	root = kernfs_mount_ns(fs_type, flags, sysfs_root,
				SYSFS_MAGIC, &new_sb, ns);
	if (IS_ERR(root) || !new_sb)
		kobj_ns_drop(KOBJ_NS_TYPE_NET, ns);
	else if (new_sb)
		/* Userspace would break if executables appear on sysfs */
		root->d_sb->s_iflags |= SB_I_NOEXEC;

	return root;
}

static void sysfs_kill_sb(struct super_block *sb)
{
	void *ns = (void *)kernfs_super_ns(sb);

	kernfs_kill_sb(sb);
	kobj_ns_drop(KOBJ_NS_TYPE_NET, ns);
}

static struct file_system_type sysfs_fs_type = {
	.name		= "sysfs",
	.get_sb		= sysfs_get_sb,
	.kill_sb	= kill_anon_super,
	.mount		= sysfs_mount,
	.kill_sb	= sysfs_kill_sb,
	.fs_flags	= FS_USERNS_VISIBLE | FS_USERNS_MOUNT,
};

int __init sysfs_init(void)
{
	int err = -ENOMEM;

	sysfs_dir_cachep = kmem_cache_create("sysfs_dir_cache",
					      sizeof(struct sysfs_dirent),
					      0, 0, NULL);
	if (!sysfs_dir_cachep)
		goto out;

	err = sysfs_inode_init();
	if (err)
		goto out_err;

	err = register_filesystem(&sysfs_fs_type);
	if (!err) {
		sysfs_mount = kern_mount(&sysfs_fs_type);
		if (IS_ERR(sysfs_mount)) {
			printk(KERN_ERR "sysfs: could not mount!\n");
			err = PTR_ERR(sysfs_mount);
			sysfs_mount = NULL;
			unregister_filesystem(&sysfs_fs_type);
			goto out_err;
		}
	} else
		goto out_err;
out:
	return err;
out_err:
	kmem_cache_destroy(sysfs_dir_cachep);
	sysfs_dir_cachep = NULL;
	goto out;
	int err;

	sysfs_root = kernfs_create_root(NULL, KERNFS_ROOT_EXTRA_OPEN_PERM_CHECK,
					NULL);
	if (IS_ERR(sysfs_root))
		return PTR_ERR(sysfs_root);

	sysfs_root_kn = sysfs_root->kn;

	err = register_filesystem(&sysfs_fs_type);
	if (err) {
		kernfs_destroy_root(sysfs_root);
		return err;
	}

	return 0;
}
