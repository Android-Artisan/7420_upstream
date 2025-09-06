/*
 *	Definitions of structures for vfsv0 quota format
 *  Definitions for vfsv0 quota format
 */

#ifndef _LINUX_DQBLK_V2_H
#define _LINUX_DQBLK_V2_H

#include <linux/types.h>

/* id numbers of quota format */
#define QFMT_VFS_V0 2

/* Numbers of blocks needed for updates */
#define V2_INIT_ALLOC 4
#define V2_INIT_REWRITE 2
#define V2_DEL_ALLOC 0
#define V2_DEL_REWRITE 6

/* Inmemory copy of version specific information */
struct v2_mem_dqinfo {
	unsigned int dqi_blocks;
	unsigned int dqi_free_blk;
	unsigned int dqi_free_entry;
};
#include <linux/dqblk_qtree.h>

/* Numbers of blocks needed for updates */
#define V2_INIT_ALLOC QTREE_INIT_ALLOC
#define V2_INIT_REWRITE QTREE_INIT_REWRITE
#define V2_DEL_ALLOC QTREE_DEL_ALLOC
#define V2_DEL_REWRITE QTREE_DEL_REWRITE

#endif /* _LINUX_DQBLK_V2_H */
