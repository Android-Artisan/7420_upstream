/*
 *
 * Definitions for mount interface. This describes the in the kernel build 
 * linkedlist with mounted filesystems.
 *
 * Author:  Marco van Wieringen <mvw@planets.elm.net>
 *
 * Version: $Id: mount.h,v 2.0 1996/11/17 16:48:14 mvw Exp mvw $
 *
 */
#ifndef _LINUX_MOUNT_H
#define _LINUX_MOUNT_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/nodemask.h>
#include <linux/spinlock.h>
#include <asm/atomic.h>
#include <linux/seqlock.h>
#include <linux/atomic.h>

struct super_block;
struct vfsmount;
struct dentry;
struct mnt_namespace;

#define MNT_NOSUID	0x01
#define MNT_NODEV	0x02
#define MNT_NOEXEC	0x04
#define MNT_NOATIME	0x08
#define MNT_NODIRATIME	0x10
#define MNT_RELATIME	0x20
#define MNT_READONLY	0x40	/* does the user want this to be r/o? */

#define MNT_SHRINKABLE	0x100
#define MNT_IMBALANCED_WRITE_COUNT	0x200 /* just for debugging */

#define MNT_SHARED	0x1000	/* if the vfsmount is a shared mount */
#define MNT_UNBINDABLE	0x2000	/* if the vfsmount is a unbindable mount */
#define MNT_PNODE_MASK	0x3000	/* propagation flag mask */

struct vfsmount {
	struct list_head mnt_hash;
	struct vfsmount *mnt_parent;	/* fs we are mounted on */
	struct dentry *mnt_mountpoint;	/* dentry of mountpoint */
	struct dentry *mnt_root;	/* root of the mounted tree */
	struct super_block *mnt_sb;	/* pointer to superblock */
	struct list_head mnt_mounts;	/* list of children, anchored here */
	struct list_head mnt_child;	/* and going through their mnt_child */
	int mnt_flags;
	/* 4 bytes hole on 64bits arches */
	const char *mnt_devname;	/* Name of device e.g. /dev/dsk/hda1 */
	struct list_head mnt_list;
	struct list_head mnt_expire;	/* link in fs-specific expiry list */
	struct list_head mnt_share;	/* circular list of shared mounts */
	struct list_head mnt_slave_list;/* list of slave mounts */
	struct list_head mnt_slave;	/* slave list entry */
	struct vfsmount *mnt_master;	/* slave is on master->mnt_slave_list */
	struct mnt_namespace *mnt_ns;	/* containing namespace */
	int mnt_id;			/* mount identifier */
	int mnt_group_id;		/* peer group identifier */
	/*
	 * We put mnt_count & mnt_expiry_mark at the end of struct vfsmount
	 * to let these frequently modified fields in a separate cache line
	 * (so that reads of mnt_flags wont ping-pong on SMP machines)
	 */
	atomic_t mnt_count;
	int mnt_expiry_mark;		/* true if marked for expiry */
	int mnt_pinned;
	int mnt_ghosts;
	/*
	 * This value is not stable unless all of the mnt_writers[] spinlocks
	 * are held, and all mnt_writer[]s on this mount have 0 as their ->count
	 */
	atomic_t __mnt_writers;
};

static inline struct vfsmount *mntget(struct vfsmount *mnt)
{
	if (mnt)
		atomic_inc(&mnt->mnt_count);
	return mnt;
}

extern int mnt_want_write(struct vfsmount *mnt);
extern void mnt_drop_write(struct vfsmount *mnt);
extern void mntput_no_expire(struct vfsmount *mnt);
extern void mnt_pin(struct vfsmount *mnt);
extern void mnt_unpin(struct vfsmount *mnt);
extern int __mnt_is_readonly(struct vfsmount *mnt);

static inline void mntput(struct vfsmount *mnt)
{
	if (mnt) {
		mnt->mnt_expiry_mark = 0;
		mntput_no_expire(mnt);
	}
}

extern struct vfsmount *do_kern_mount(const char *fstype, int flags,
				      const char *name, void *data);
#define MNT_WRITE_HOLD	0x200

#define MNT_SHARED	0x1000	/* if the vfsmount is a shared mount */
#define MNT_UNBINDABLE	0x2000	/* if the vfsmount is a unbindable mount */
/*
 * MNT_SHARED_MASK is the set of flags that should be cleared when a
 * mount becomes shared.  Currently, this is only the flag that says a
 * mount cannot be bind mounted, since this is how we create a mount
 * that shares events with another mount.  If you add a new MNT_*
 * flag, consider how it interacts with shared mounts.
 */
#define MNT_SHARED_MASK	(MNT_UNBINDABLE)
#define MNT_USER_SETTABLE_MASK  (MNT_NOSUID | MNT_NODEV | MNT_NOEXEC \
				 | MNT_NOATIME | MNT_NODIRATIME | MNT_RELATIME \
				 | MNT_READONLY)
#define MNT_ATIME_MASK (MNT_NOATIME | MNT_NODIRATIME | MNT_RELATIME )

#define MNT_INTERNAL_FLAGS (MNT_SHARED | MNT_WRITE_HOLD | MNT_INTERNAL | \
			    MNT_DOOMED | MNT_SYNC_UMOUNT | MNT_MARKED)

#define MNT_INTERNAL	0x4000

#define MNT_LOCK_ATIME		0x040000
#define MNT_LOCK_NOEXEC		0x080000
#define MNT_LOCK_NOSUID		0x100000
#define MNT_LOCK_NODEV		0x200000
#define MNT_LOCK_READONLY	0x400000
#define MNT_LOCKED		0x800000
#define MNT_DOOMED		0x1000000
#define MNT_SYNC_UMOUNT		0x2000000
#define MNT_MARKED		0x4000000
#define MNT_UMOUNT		0x8000000

struct vfsmount {
	struct dentry *mnt_root;	/* root of the mounted tree */
	struct super_block *mnt_sb;	/* pointer to superblock */
	int mnt_flags;
};

struct file; /* forward dec */
struct path;

extern int mnt_want_write(struct vfsmount *mnt);
extern int mnt_want_write_file(struct file *file);
extern int mnt_clone_write(struct vfsmount *mnt);
extern void mnt_drop_write(struct vfsmount *mnt);
extern void mnt_drop_write_file(struct file *file);
extern void mntput(struct vfsmount *mnt);
extern struct vfsmount *mntget(struct vfsmount *mnt);
extern struct vfsmount *mnt_clone_internal(struct path *path);
extern int __mnt_is_readonly(struct vfsmount *mnt);

struct path;
extern struct vfsmount *clone_private_mount(struct path *path);

struct file_system_type;
extern struct vfsmount *vfs_kern_mount(struct file_system_type *type,
				      int flags, const char *name,
				      void *data);

struct nameidata;

struct path;
extern int do_add_mount(struct vfsmount *newmnt, struct path *path,
			int mnt_flags, struct list_head *fslist);

extern void mark_mounts_for_expiry(struct list_head *mounts);

extern spinlock_t vfsmount_lock;
extern dev_t name_to_dev_t(char *name);
extern void mnt_set_expiry(struct vfsmount *mnt, struct list_head *expiry_list);
extern void mark_mounts_for_expiry(struct list_head *mounts);

extern dev_t name_to_dev_t(const char *name);

extern unsigned int sysctl_mount_max;

#endif /* _LINUX_MOUNT_H */
