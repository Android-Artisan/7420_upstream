/* -*- linux-c -*- --------------------------------------------------------- *
 *
 * linux/include/linux/devpts_fs.h
 *
 *  Copyright 1998-2004 H. Peter Anvin -- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ------------------------------------------------------------------------- */

#ifndef _LINUX_DEVPTS_FS_H
#define _LINUX_DEVPTS_FS_H

#include <linux/errno.h>

struct pts_fs_info;

#ifdef CONFIG_UNIX98_PTYS

int devpts_new_index(void);
void devpts_kill_index(int idx);
int devpts_pty_new(struct tty_struct *tty);      /* mknod in devpts */
struct tty_struct *devpts_get_tty(int number);	 /* get tty structure */
void devpts_pty_kill(int number);		 /* unlink */
int devpts_new_index(struct inode *ptmx_inode);
void devpts_kill_index(struct inode *ptmx_inode, int idx);
/* Look up a pts fs info and get a ref to it */
struct pts_fs_info *devpts_get_ref(struct inode *, struct file *);
void devpts_put_ref(struct pts_fs_info *);

int devpts_new_index(struct pts_fs_info *);
void devpts_kill_index(struct pts_fs_info *, int);

/* mknod in devpts */
struct inode *devpts_pty_new(struct pts_fs_info *, dev_t, int, void *);
/* get private structure */
void *devpts_get_priv(struct inode *pts_inode);
/* unlink */
void devpts_pty_kill(struct inode *inode);

#else

/* Dummy stubs in the no-pty case */
static inline int devpts_new_index(void) { return -EINVAL; }
static inline void devpts_kill_index(int idx) { }
static inline int devpts_pty_new(struct tty_struct *tty) { return -EINVAL; }
static inline struct tty_struct *devpts_get_tty(int number) { return NULL; }
static inline void devpts_pty_kill(int number) { }
static inline int devpts_new_index(struct inode *ptmx_inode) { return -EINVAL; }
static inline void devpts_kill_index(struct inode *ptmx_inode, int idx) { }
static inline struct inode *devpts_pty_new(struct inode *ptmx_inode,
		dev_t device, int index, void *priv)
{
	return ERR_PTR(-EINVAL);
}
static inline void *devpts_get_priv(struct inode *pts_inode)
{
	return NULL;
}
static inline void devpts_pty_kill(struct inode *inode) { }

#endif


#endif /* _LINUX_DEVPTS_FS_H */
