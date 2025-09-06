/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifndef __XFS_MOUNT_H__
#define	__XFS_MOUNT_H__


typedef struct xfs_trans_reservations {
	uint	tr_write;	/* extent alloc trans */
	uint	tr_itruncate;	/* truncate trans */
	uint	tr_rename;	/* rename trans */
	uint	tr_link;	/* link trans */
	uint	tr_remove;	/* unlink trans */
	uint	tr_symlink;	/* symlink trans */
	uint	tr_create;	/* create trans */
	uint	tr_mkdir;	/* mkdir trans */
	uint	tr_ifree;	/* inode free trans */
	uint	tr_ichange;	/* inode update trans */
	uint	tr_growdata;	/* fs data section grow trans */
	uint	tr_swrite;	/* sync write inode trans */
	uint	tr_addafork;	/* cvt inode to attributed trans */
	uint	tr_writeid;	/* write setuid/setgid file */
	uint	tr_attrinval;	/* attr fork buffer invalidation */
	uint	tr_attrset;	/* set/create an attribute */
	uint	tr_attrrm;	/* remove an attribute */
	uint	tr_clearagi;	/* clear bad agi unlinked ino bucket */
	uint	tr_growrtalloc;	/* grow realtime allocations */
	uint	tr_growrtzero;	/* grow realtime zeroing */
	uint	tr_growrtfree;	/* grow realtime freeing */
} xfs_trans_reservations_t;

#ifndef __KERNEL__
/*
 * Moved here from xfs_ag.h to avoid reordering header files
 */
#define XFS_DADDR_TO_AGNO(mp,d) \
	((xfs_agnumber_t)(XFS_BB_TO_FSBT(mp, d) / (mp)->m_sb.sb_agblocks))
#define XFS_DADDR_TO_AGBNO(mp,d) \
	((xfs_agblock_t)(XFS_BB_TO_FSBT(mp, d) % (mp)->m_sb.sb_agblocks))
#else
struct cred;
struct log;
struct xfs_mount_args;
struct xfs_inode;
struct xfs_bmbt_irec;
struct xfs_bmap_free;
struct xfs_extdelta;
struct xfs_swapext;
struct xfs_mru_cache;
struct xfs_nameops;

/*
 * Prototypes and functions for the Data Migration subsystem.
 */

typedef int	(*xfs_send_data_t)(int, struct xfs_inode *,
			xfs_off_t, size_t, int, int *);
typedef int	(*xfs_send_mmap_t)(struct vm_area_struct *, uint);
typedef int	(*xfs_send_destroy_t)(struct xfs_inode *, dm_right_t);
typedef int	(*xfs_send_namesp_t)(dm_eventtype_t, struct xfs_mount *,
			struct xfs_inode *, dm_right_t,
			struct xfs_inode *, dm_right_t,
			const char *, const char *, mode_t, int, int);
typedef int	(*xfs_send_mount_t)(struct xfs_mount *, dm_right_t,
			char *, char *);
typedef void	(*xfs_send_unmount_t)(struct xfs_mount *, struct xfs_inode *,
			dm_right_t, mode_t, int, int);

typedef struct xfs_dmops {
	xfs_send_data_t		xfs_send_data;
	xfs_send_mmap_t		xfs_send_mmap;
	xfs_send_destroy_t	xfs_send_destroy;
	xfs_send_namesp_t	xfs_send_namesp;
	xfs_send_mount_t	xfs_send_mount;
	xfs_send_unmount_t	xfs_send_unmount;
} xfs_dmops_t;

#define XFS_SEND_DATA(mp, ev,ip,off,len,fl,lock) \
	(*(mp)->m_dm_ops->xfs_send_data)(ev,ip,off,len,fl,lock)
#define XFS_SEND_MMAP(mp, vma,fl) \
	(*(mp)->m_dm_ops->xfs_send_mmap)(vma,fl)
#define XFS_SEND_DESTROY(mp, ip,right) \
	(*(mp)->m_dm_ops->xfs_send_destroy)(ip,right)
#define XFS_SEND_NAMESP(mp, ev,b1,r1,b2,r2,n1,n2,mode,rval,fl) \
	(*(mp)->m_dm_ops->xfs_send_namesp)(ev,NULL,b1,r1,b2,r2,n1,n2,mode,rval,fl)
#define XFS_SEND_PREUNMOUNT(mp,b1,r1,b2,r2,n1,n2,mode,rval,fl) \
	(*(mp)->m_dm_ops->xfs_send_namesp)(DM_EVENT_PREUNMOUNT,mp,b1,r1,b2,r2,n1,n2,mode,rval,fl)
#define XFS_SEND_MOUNT(mp,right,path,name) \
	(*(mp)->m_dm_ops->xfs_send_mount)(mp,right,path,name)
#define XFS_SEND_UNMOUNT(mp, ip,right,mode,rval,fl) \
	(*(mp)->m_dm_ops->xfs_send_unmount)(mp,ip,right,mode,rval,fl)


/*
 * Prototypes and functions for the Quota Management subsystem.
 */

struct xfs_dquot;
struct xfs_dqtrxops;
struct xfs_quotainfo;

typedef int	(*xfs_qminit_t)(struct xfs_mount *, uint *, uint *);
typedef int	(*xfs_qmmount_t)(struct xfs_mount *, uint, uint);
typedef int	(*xfs_qmunmount_t)(struct xfs_mount *);
typedef void	(*xfs_qmdone_t)(struct xfs_mount *);
typedef void	(*xfs_dqrele_t)(struct xfs_dquot *);
typedef int	(*xfs_dqattach_t)(struct xfs_inode *, uint);
typedef void	(*xfs_dqdetach_t)(struct xfs_inode *);
typedef int	(*xfs_dqpurgeall_t)(struct xfs_mount *, uint);
typedef int	(*xfs_dqvopalloc_t)(struct xfs_mount *,
			struct xfs_inode *, uid_t, gid_t, prid_t, uint,
			struct xfs_dquot **, struct xfs_dquot **);
typedef void	(*xfs_dqvopcreate_t)(struct xfs_trans *, struct xfs_inode *,
			struct xfs_dquot *, struct xfs_dquot *);
typedef int	(*xfs_dqvoprename_t)(struct xfs_inode **);
typedef struct xfs_dquot * (*xfs_dqvopchown_t)(
			struct xfs_trans *, struct xfs_inode *,
			struct xfs_dquot **, struct xfs_dquot *);
typedef int	(*xfs_dqvopchownresv_t)(struct xfs_trans *, struct xfs_inode *,
			struct xfs_dquot *, struct xfs_dquot *, uint);
typedef void	(*xfs_dqstatvfs_t)(struct xfs_inode *, bhv_statvfs_t *);
typedef int	(*xfs_dqsync_t)(struct xfs_mount *, int flags);
typedef int	(*xfs_quotactl_t)(struct xfs_mount *, int, int, xfs_caddr_t);

typedef struct xfs_qmops {
	xfs_qminit_t		xfs_qminit;
	xfs_qmdone_t		xfs_qmdone;
	xfs_qmmount_t		xfs_qmmount;
	xfs_qmunmount_t		xfs_qmunmount;
	xfs_dqrele_t		xfs_dqrele;
	xfs_dqattach_t		xfs_dqattach;
	xfs_dqdetach_t		xfs_dqdetach;
	xfs_dqpurgeall_t	xfs_dqpurgeall;
	xfs_dqvopalloc_t	xfs_dqvopalloc;
	xfs_dqvopcreate_t	xfs_dqvopcreate;
	xfs_dqvoprename_t	xfs_dqvoprename;
	xfs_dqvopchown_t	xfs_dqvopchown;
	xfs_dqvopchownresv_t	xfs_dqvopchownresv;
	xfs_dqstatvfs_t		xfs_dqstatvfs;
	xfs_dqsync_t		xfs_dqsync;
	xfs_quotactl_t		xfs_quotactl;
	struct xfs_dqtrxops	*xfs_dqtrxops;
} xfs_qmops_t;

#define XFS_QM_INIT(mp, mnt, fl) \
	(*(mp)->m_qm_ops->xfs_qminit)(mp, mnt, fl)
#define XFS_QM_MOUNT(mp, mnt, fl) \
	(*(mp)->m_qm_ops->xfs_qmmount)(mp, mnt, fl)
#define XFS_QM_UNMOUNT(mp) \
	(*(mp)->m_qm_ops->xfs_qmunmount)(mp)
#define XFS_QM_DONE(mp) \
	(*(mp)->m_qm_ops->xfs_qmdone)(mp)
#define XFS_QM_DQRELE(mp, dq) \
	(*(mp)->m_qm_ops->xfs_dqrele)(dq)
#define XFS_QM_DQATTACH(mp, ip, fl) \
	(*(mp)->m_qm_ops->xfs_dqattach)(ip, fl)
#define XFS_QM_DQDETACH(mp, ip) \
	(*(mp)->m_qm_ops->xfs_dqdetach)(ip)
#define XFS_QM_DQPURGEALL(mp, fl) \
	(*(mp)->m_qm_ops->xfs_dqpurgeall)(mp, fl)
#define XFS_QM_DQVOPALLOC(mp, ip, uid, gid, prid, fl, dq1, dq2) \
	(*(mp)->m_qm_ops->xfs_dqvopalloc)(mp, ip, uid, gid, prid, fl, dq1, dq2)
#define XFS_QM_DQVOPCREATE(mp, tp, ip, dq1, dq2) \
	(*(mp)->m_qm_ops->xfs_dqvopcreate)(tp, ip, dq1, dq2)
#define XFS_QM_DQVOPRENAME(mp, ip) \
	(*(mp)->m_qm_ops->xfs_dqvoprename)(ip)
#define XFS_QM_DQVOPCHOWN(mp, tp, ip, dqp, dq) \
	(*(mp)->m_qm_ops->xfs_dqvopchown)(tp, ip, dqp, dq)
#define XFS_QM_DQVOPCHOWNRESV(mp, tp, ip, dq1, dq2, fl) \
	(*(mp)->m_qm_ops->xfs_dqvopchownresv)(tp, ip, dq1, dq2, fl)
#define XFS_QM_DQSTATVFS(ip, statp) \
	(*(ip)->i_mount->m_qm_ops->xfs_dqstatvfs)(ip, statp)
#define XFS_QM_DQSYNC(mp, flags) \
	(*(mp)->m_qm_ops->xfs_dqsync)(mp, flags)
#define XFS_QM_QUOTACTL(mp, cmd, id, addr) \
	(*(mp)->m_qm_ops->xfs_quotactl)(mp, cmd, id, addr)

#ifdef HAVE_PERCPU_SB

/*
 * Valid per-cpu incore superblock counters. Note that if you add new counters,
 * you may need to define new counter disabled bit field descriptors as there
 * are more possible fields in the superblock that can fit in a bitfield on a
 * 32 bit platform. The XFS_SBS_* values for the current current counters just
 * fit.
 */
typedef struct xfs_icsb_cnts {
	uint64_t	icsb_fdblocks;
	uint64_t	icsb_ifree;
	uint64_t	icsb_icount;
	unsigned long	icsb_flags;
} xfs_icsb_cnts_t;

#define XFS_ICSB_FLAG_LOCK	(1 << 0)	/* counter lock bit */

#define XFS_ICSB_LAZY_COUNT	(1 << 1)	/* accuracy not needed */

extern int	xfs_icsb_init_counters(struct xfs_mount *);
extern void	xfs_icsb_reinit_counters(struct xfs_mount *);
extern void	xfs_icsb_destroy_counters(struct xfs_mount *);
extern void	xfs_icsb_sync_counters(struct xfs_mount *, int);
extern void	xfs_icsb_sync_counters_locked(struct xfs_mount *, int);

#else
#define xfs_icsb_init_counters(mp)		(0)
#define xfs_icsb_destroy_counters(mp)		do { } while (0)
#define xfs_icsb_reinit_counters(mp)		do { } while (0)
#define xfs_icsb_sync_counters(mp, flags)	do { } while (0)
#define xfs_icsb_sync_counters_locked(mp, flags) do { } while (0)
#endif

typedef struct xfs_ail {
	struct list_head	xa_ail;
	uint			xa_gen;
	struct task_struct	*xa_task;
	xfs_lsn_t		xa_target;
} xfs_ail_t;
struct xlog;
struct xfs_inode;
struct xfs_mru_cache;
struct xfs_nameops;
struct xfs_ail;
struct xfs_quotainfo;
struct xfs_dir_ops;
struct xfs_da_geometry;

/* dynamic preallocation free space thresholds, 5% down to 1% */
enum {
	XFS_LOWSP_1_PCNT = 0,
	XFS_LOWSP_2_PCNT,
	XFS_LOWSP_3_PCNT,
	XFS_LOWSP_4_PCNT,
	XFS_LOWSP_5_PCNT,
	XFS_LOWSP_MAX,
};

typedef struct xfs_mount {
	struct super_block	*m_super;
	xfs_tid_t		m_tid;		/* next unused tid for fs */
	spinlock_t		m_ail_lock;	/* fs AIL mutex */
	xfs_ail_t		m_ail;		/* fs active log item list */
	xfs_sb_t		m_sb;		/* copy of fs superblock */
	spinlock_t		m_sb_lock;	/* sb counter lock */
	struct xfs_ail		*m_ail;		/* fs active log item list */

	struct xfs_sb		m_sb;		/* copy of fs superblock */
	spinlock_t		m_sb_lock;	/* sb counter lock */
	struct percpu_counter	m_icount;	/* allocated inodes counter */
	struct percpu_counter	m_ifree;	/* free inodes counter */
	struct percpu_counter	m_fdblocks;	/* free block counter */

	struct xfs_buf		*m_sb_bp;	/* buffer for superblock */
	char			*m_fsname;	/* filesystem name */
	int			m_fsname_len;	/* strlen of fs name */
	char			*m_rtname;	/* realtime device name */
	char			*m_logname;	/* external log device name */
	int			m_bsize;	/* fs logical block size */
	xfs_agnumber_t		m_agfrotor;	/* last ag where space found */
	xfs_agnumber_t		m_agirotor;	/* last ag dir inode alloced */
	spinlock_t		m_agirotor_lock;/* .. and lock protecting it */
	xfs_agnumber_t		m_maxagi;	/* highest inode alloc group */
	struct xfs_inode	*m_inodes;	/* active inode list */
	struct list_head	m_del_inodes;	/* inodes to reclaim */
	mutex_t			m_ilock;	/* inode list mutex */
	uint			m_ireclaims;	/* count of calls to reclaim*/
	uint			m_readio_log;	/* min read size log bytes */
	uint			m_readio_blocks; /* min read size blocks */
	uint			m_writeio_log;	/* min write size log bytes */
	uint			m_writeio_blocks; /* min write size blocks */
	struct log		*m_log;		/* log specific stuff */
	struct xfs_da_geometry	*m_dir_geo;	/* directory block geometry */
	struct xfs_da_geometry	*m_attr_geo;	/* attribute block geometry */
	struct xlog		*m_log;		/* log specific stuff */
	int			m_logbufs;	/* number of log buffers */
	int			m_logbsize;	/* size of each log buffer */
	uint			m_rsumlevels;	/* rt summary levels */
	uint			m_rsumsize;	/* size of rt summary, bytes */
	struct xfs_inode	*m_rbmip;	/* pointer to bitmap inode */
	struct xfs_inode	*m_rsumip;	/* pointer to summary inode */
	struct xfs_inode	*m_rootip;	/* pointer to root directory */
	struct xfs_quotainfo	*m_quotainfo;	/* disk quota information */
	xfs_buftarg_t		*m_ddev_targp;	/* saves taking the address */
	xfs_buftarg_t		*m_logdev_targp;/* ptr to log device */
	xfs_buftarg_t		*m_rtdev_targp;	/* ptr to rt device */
	__uint8_t		m_dircook_elog;	/* log d-cookie entry bits */
	__uint8_t		m_blkbit_log;	/* blocklog + NBBY */
	__uint8_t		m_blkbb_log;	/* blocklog - BBSHIFT */
	__uint8_t		m_agno_log;	/* log #ag's */
	__uint8_t		m_agino_log;	/* #bits for agino in inum */
	__uint16_t		m_inode_cluster_size;/* min inode buf size */
	uint			m_blockmask;	/* sb_blocksize-1 */
	uint			m_blockwsize;	/* sb_blocksize in words */
	uint			m_blockwmask;	/* blockwsize-1 */
	uint			m_alloc_mxr[2];	/* XFS_ALLOC_BLOCK_MAXRECS */
	uint			m_alloc_mnr[2];	/* XFS_ALLOC_BLOCK_MINRECS */
	uint			m_bmap_dmxr[2];	/* XFS_BMAP_BLOCK_DMAXRECS */
	uint			m_bmap_dmnr[2];	/* XFS_BMAP_BLOCK_DMINRECS */
	uint			m_inobt_mxr[2];	/* XFS_INOBT_BLOCK_MAXRECS */
	uint			m_inobt_mnr[2];	/* XFS_INOBT_BLOCK_MINRECS */
	uint			m_ag_maxlevels;	/* XFS_AG_MAXLEVELS */
	uint			m_bm_maxlevels[2]; /* XFS_BM_MAXLEVELS */
	uint			m_in_maxlevels;	/* XFS_IN_MAXLEVELS */
	struct xfs_perag	*m_perag;	/* per-ag accounting info */
	struct rw_semaphore	m_peraglock;	/* lock for m_perag (pointer) */
	uint			m_inode_cluster_size;/* min inode buf size */
	uint			m_blockmask;	/* sb_blocksize-1 */
	uint			m_blockwsize;	/* sb_blocksize in words */
	uint			m_blockwmask;	/* blockwsize-1 */
	uint			m_alloc_mxr[2];	/* max alloc btree records */
	uint			m_alloc_mnr[2];	/* min alloc btree records */
	uint			m_bmap_dmxr[2];	/* max bmap btree records */
	uint			m_bmap_dmnr[2];	/* min bmap btree records */
	uint			m_inobt_mxr[2];	/* max inobt btree records */
	uint			m_inobt_mnr[2];	/* min inobt btree records */
	uint			m_ag_maxlevels;	/* XFS_AG_MAXLEVELS */
	uint			m_bm_maxlevels[2]; /* XFS_BM_MAXLEVELS */
	uint			m_in_maxlevels;	/* max inobt btree levels. */
	struct radix_tree_root	m_perag_tree;	/* per-ag accounting info */
	spinlock_t		m_perag_lock;	/* lock for m_perag_tree */
	struct mutex		m_growlock;	/* growfs mutex */
	int			m_fixedfsid[2];	/* unchanged for life of FS */
	uint			m_dmevmask;	/* DMI events for this FS */
	__uint64_t		m_flags;	/* global mount flags */
	uint			m_attroffset;	/* inode attribute offset */
	uint			m_dir_node_ents; /* #entries in a dir danode */
	uint			m_attr_node_ents; /* #entries in attr danode */
	int			m_ialloc_inos;	/* inodes in inode allocation */
	int			m_ialloc_blks;	/* blocks in inode allocation */
	int			m_litino;	/* size of inode union area */
	int			m_inoalign_mask;/* mask sb_inoalignmt if used */
	uint			m_qflags;	/* quota status flags */
	xfs_trans_reservations_t m_reservations;/* precomputed res values */
	__uint64_t		m_maxicount;	/* maximum inode count */
	__uint64_t		m_maxioffset;	/* maximum inode offset */
	__uint64_t		m_resblks;	/* total reserved blocks */
	__uint64_t		m_resblks_avail;/* available reserved blocks */
#if XFS_BIG_INUMS
	xfs_ino_t		m_inoadd;	/* add value for ino64_offset */
#endif
	int			m_dalign;	/* stripe unit */
	int			m_swidth;	/* stripe width */
	int			m_sinoalign;	/* stripe unit inode alignment */
	int			m_attr_magicpct;/* 37% of the blocksize */
	int			m_dir_magicpct;	/* 37% of the dir blocksize */
	__uint8_t		m_mk_sharedro;	/* mark shared ro on unmount */
	__uint8_t		m_inode_quiesce;/* call quiesce on new inodes.
						   field governed by m_ilock */
	__uint8_t		m_sectbb_log;	/* sectlog - BBSHIFT */
	const struct xfs_nameops *m_dirnameops;	/* vector of dir name ops */
	int			m_dirblksize;	/* directory block sz--bytes */
	int			m_dirblkfsbs;	/* directory block sz--fsbs */
	xfs_dablk_t		m_dirdatablk;	/* blockno of dir data v2 */
	xfs_dablk_t		m_dirleafblk;	/* blockno of dir non-data v2 */
	xfs_dablk_t		m_dirfreeblk;	/* blockno of dirfreeindex v2 */
	uint			m_chsize;	/* size of next field */
	struct xfs_chash	*m_chash;	/* fs private inode per-cluster
						 * hash table */
	struct xfs_dmops	*m_dm_ops;	/* vector of DMI ops */
	struct xfs_qmops	*m_qm_ops;	/* vector of XQM ops */
	atomic_t		m_active_trans;	/* number trans frozen */
#ifdef HAVE_PERCPU_SB
	xfs_icsb_cnts_t		*m_sb_cnts;	/* per-cpu superblock counters */
	unsigned long		m_icsb_counters; /* disabled per-cpu counters */
	struct notifier_block	m_icsb_notifier; /* hotplug cpu notifier */
	struct mutex		m_icsb_mutex;	/* balancer sync lock */
#endif
	struct xfs_mru_cache	*m_filestream;  /* per-mount filestream data */
	struct task_struct	*m_sync_task;	/* generalised sync thread */
	bhv_vfs_sync_work_t	m_sync_work;	/* work item for VFS_SYNC */
	struct list_head	m_sync_list;	/* sync thread work item list */
	spinlock_t		m_sync_lock;	/* work item list lock */
	int			m_sync_seq;	/* sync thread generation no. */
	wait_queue_head_t	m_wait_single_sync_task;
	int			m_ialloc_inos;	/* inodes in inode allocation */
	int			m_ialloc_blks;	/* blocks in inode allocation */
	int			m_ialloc_min_blks;/* min blocks in sparse inode
						   * allocation */
	int			m_inoalign_mask;/* mask sb_inoalignmt if used */
	uint			m_qflags;	/* quota status flags */
	struct xfs_trans_resv	m_resv;		/* precomputed res values */
	__uint64_t		m_maxicount;	/* maximum inode count */
	__uint64_t		m_resblks;	/* total reserved blocks */
	__uint64_t		m_resblks_avail;/* available reserved blocks */
	__uint64_t		m_resblks_save;	/* reserved blks @ remount,ro */
	int			m_dalign;	/* stripe unit */
	int			m_swidth;	/* stripe width */
	int			m_sinoalign;	/* stripe unit inode alignment */
	__uint8_t		m_sectbb_log;	/* sectlog - BBSHIFT */
	const struct xfs_nameops *m_dirnameops;	/* vector of dir name ops */
	const struct xfs_dir_ops *m_dir_inode_ops; /* vector of dir inode ops */
	const struct xfs_dir_ops *m_nondir_inode_ops; /* !dir inode ops */
	uint			m_chsize;	/* size of next field */
	atomic_t		m_active_trans;	/* number trans frozen */
	struct xfs_mru_cache	*m_filestream;  /* per-mount filestream data */
	struct delayed_work	m_reclaim_work;	/* background inode reclaim */
	struct delayed_work	m_eofblocks_work; /* background eof blocks
						     trimming */
	bool			m_update_sb;	/* sb needs update in mount */
	int64_t			m_low_space[XFS_LOWSP_MAX];
						/* low free space thresholds */
	struct xfs_kobj		m_kobj;
	struct xstats		m_stats;	/* per-fs stats */

	struct workqueue_struct *m_buf_workqueue;
	struct workqueue_struct	*m_data_workqueue;
	struct workqueue_struct	*m_unwritten_workqueue;
	struct workqueue_struct	*m_cil_workqueue;
	struct workqueue_struct	*m_reclaim_workqueue;
	struct workqueue_struct	*m_log_workqueue;
	struct workqueue_struct *m_eofblocks_workqueue;

	/*
	 * Generation of the filesysyem layout.  This is incremented by each
	 * growfs, and used by the pNFS server to ensure the client updates
	 * its view of the block device once it gets a layout that might
	 * reference the newly added blocks.  Does not need to be persistent
	 * as long as we only allow file system size increments, but if we
	 * ever support shrinks it would have to be persisted in addition
	 * to various other kinds of pain inflicted on the pNFS server.
	 */
	__uint32_t		m_generation;
} xfs_mount_t;

/*
 * Flags for m_flags.
 */
#define XFS_MOUNT_WSYNC		(1ULL << 0)	/* for nfs - all metadata ops
						   must be synchronous except
						   for space allocations */
#define XFS_MOUNT_INO64		(1ULL << 1)
#define XFS_MOUNT_DMAPI		(1ULL << 2)	/* dmapi is enabled */
#define XFS_MOUNT_WAS_CLEAN	(1ULL << 3)
#define XFS_MOUNT_FS_SHUTDOWN	(1ULL << 4)	/* atomic stop of all filesystem
						   operations, typically for
						   disk errors in metadata */
#define XFS_MOUNT_RETERR	(1ULL << 6)     /* return alignment errors to
						   user */
#define XFS_MOUNT_DISCARD	(1ULL << 5)	/* discard unused blocks */
#define XFS_MOUNT_NOALIGN	(1ULL << 7)	/* turn off stripe alignment
						   allocations */
#define XFS_MOUNT_ATTR2		(1ULL << 8)	/* allow use of attr2 format */
#define XFS_MOUNT_GRPID		(1ULL << 9)	/* group-ID assigned from directory */
#define XFS_MOUNT_NORECOVERY	(1ULL << 10)	/* no recovery - dirty fs */
#define XFS_MOUNT_SHARED	(1ULL << 11)	/* shared mount */
#define XFS_MOUNT_DFLT_IOSIZE	(1ULL << 12)	/* set default i/o size */
#define XFS_MOUNT_OSYNCISOSYNC	(1ULL << 13)	/* o_sync is REALLY o_sync */
						/* osyncisdsync is now default*/
#define XFS_MOUNT_DFLT_IOSIZE	(1ULL << 12)	/* set default i/o size */
#define XFS_MOUNT_32BITINODES	(1ULL << 14)	/* do not create inodes above
						 * 32 bits in size */
#define XFS_MOUNT_SMALL_INUMS	(1ULL << 15)	/* users wants 32bit inodes */
#define XFS_MOUNT_NOUUID	(1ULL << 16)	/* ignore uuid during mount */
#define XFS_MOUNT_BARRIER	(1ULL << 17)
#define XFS_MOUNT_IKEEP		(1ULL << 18)	/* keep empty inode clusters*/
#define XFS_MOUNT_SWALLOC	(1ULL << 19)	/* turn on stripe width
						 * allocation */
#define XFS_MOUNT_RDONLY	(1ULL << 20)	/* read-only fs */
#define XFS_MOUNT_DIRSYNC	(1ULL << 21)	/* synchronous directory ops */
#define XFS_MOUNT_COMPAT_IOSIZE	(1ULL << 22)	/* don't report large preferred
						 * I/O size in stat() */
#define XFS_MOUNT_NO_PERCPU_SB	(1ULL << 23)	/* don't use per-cpu superblock
						   counters */
#define XFS_MOUNT_FILESTREAMS	(1ULL << 24)	/* enable the filestreams
						   allocator */
#define XFS_MOUNT_NOATTR2	(1ULL << 25)	/* disable use of attr2 format */

#define XFS_MOUNT_DAX		(1ULL << 62)	/* TEST ONLY! */


/*
 * Default minimum read and write sizes.
 */
#define XFS_READIO_LOG_LARGE	16
#define XFS_WRITEIO_LOG_LARGE	16

/*
 * Max and min values for mount-option defined I/O
 * preallocation sizes.
 */
#define XFS_MAX_IO_LOG		30	/* 1G */
#define XFS_MIN_IO_LOG		PAGE_SHIFT

/*
 * Synchronous read and write sizes.  This should be
 * better for NFSv2 wsync filesystems.
 */
#define	XFS_WSYNC_READIO_LOG	15	/* 32K */
#define	XFS_WSYNC_WRITEIO_LOG	14	/* 16K */
#define	XFS_WSYNC_READIO_LOG	15	/* 32k */
#define	XFS_WSYNC_WRITEIO_LOG	14	/* 16k */

/*
 * Allow large block sizes to be reported to userspace programs if the
 * "largeio" mount option is used.
 *
 * If compatibility mode is specified, simply return the basic unit of caching
 * so that we don't get inefficient read/modify/write I/O from user apps.
 * Otherwise....
 *
 * If the underlying volume is a stripe, then return the stripe width in bytes
 * as the recommended I/O size. It is not a stripe and we've set a default
 * buffered I/O size, return that, otherwise return the compat default.
 */
static inline unsigned long
xfs_preferred_iosize(xfs_mount_t *mp)
{
	if (mp->m_flags & XFS_MOUNT_COMPAT_IOSIZE)
		return PAGE_CACHE_SIZE;
	return (mp->m_swidth ?
		(mp->m_swidth << mp->m_sb.sb_blocklog) :
		((mp->m_flags & XFS_MOUNT_DFLT_IOSIZE) ?
			(1 << (int)MAX(mp->m_readio_log, mp->m_writeio_log)) :
			PAGE_CACHE_SIZE));
}

#define XFS_MAXIOFFSET(mp)	((mp)->m_maxioffset)

#define XFS_LAST_UNMOUNT_WAS_CLEAN(mp)	\
				((mp)->m_flags & XFS_MOUNT_WAS_CLEAN)
#define XFS_FORCED_SHUTDOWN(mp)	((mp)->m_flags & XFS_MOUNT_FS_SHUTDOWN)
void xfs_do_force_shutdown(struct xfs_mount *mp, int flags, char *fname,
		int lnnum);
#define xfs_force_shutdown(m,f)	\
	xfs_do_force_shutdown(m, f, __FILE__, __LINE__)

#define SHUTDOWN_META_IO_ERROR	0x0001	/* write attempt to metadata failed */
#define SHUTDOWN_LOG_IO_ERROR	0x0002	/* write attempt to the log failed */
#define SHUTDOWN_FORCE_UMOUNT	0x0004	/* shutdown from a forced unmount */
#define SHUTDOWN_CORRUPT_INCORE	0x0008	/* corrupt in-memory data structures */
#define SHUTDOWN_REMOTE_REQ	0x0010	/* shutdown came from remote cell */
#define SHUTDOWN_DEVICE_REQ	0x0020	/* failed all paths to the device */

/*
 * Flags for xfs_mountfs
 */
#define XFS_MFSI_QUIET		0x40	/* Be silent if mount errors found */

#define XFS_DADDR_TO_AGNO(mp,d)         xfs_daddr_to_agno(mp,d)
static inline xfs_agnumber_t
xfs_daddr_to_agno(struct xfs_mount *mp, xfs_daddr_t d)
{
	xfs_daddr_t ld = XFS_BB_TO_FSBT(mp, d);
	do_div(ld, mp->m_sb.sb_agblocks);
	return (xfs_agnumber_t) ld;
}

#define XFS_DADDR_TO_AGBNO(mp,d)        xfs_daddr_to_agbno(mp,d)
static inline xfs_agblock_t
xfs_daddr_to_agbno(struct xfs_mount *mp, xfs_daddr_t d)
{
	xfs_daddr_t ld = XFS_BB_TO_FSBT(mp, d);
	return (xfs_agblock_t) do_div(ld, mp->m_sb.sb_agblocks);
}

/*
 * perag get/put wrappers for eventual ref counting
 */
static inline xfs_perag_t *
xfs_get_perag(struct xfs_mount *mp, xfs_ino_t ino)
{
	return &mp->m_perag[XFS_INO_TO_AGNO(mp, ino)];
}

static inline void
xfs_put_perag(struct xfs_mount *mp, xfs_perag_t *pag)
{
	/* nothing to see here, move along */
}

/*
 * Per-cpu superblock locking functions
 */
#ifdef HAVE_PERCPU_SB
STATIC_INLINE void
xfs_icsb_lock(xfs_mount_t *mp)
{
	mutex_lock(&mp->m_icsb_mutex);
}

STATIC_INLINE void
xfs_icsb_unlock(xfs_mount_t *mp)
{
	mutex_unlock(&mp->m_icsb_mutex);
}
#else
#define xfs_icsb_lock(mp)
#define xfs_icsb_unlock(mp)
#endif

/*
 * This structure is for use by the xfs_mod_incore_sb_batch() routine.
 * xfs_growfs can specify a few fields which are more than int limit
 */
typedef struct xfs_mod_sb {
	xfs_sb_field_t	msb_field;	/* Field to modify, see below */
	int64_t		msb_delta;	/* Change to make to specified field */
} xfs_mod_sb_t;

#define	XFS_MOUNT_ILOCK(mp)	mutex_lock(&((mp)->m_ilock))
#define	XFS_MOUNT_IUNLOCK(mp)	mutex_unlock(&((mp)->m_ilock))

extern void	xfs_mod_sb(xfs_trans_t *, __int64_t);
extern int	xfs_log_sbcount(xfs_mount_t *, uint);
extern int	xfs_mountfs(xfs_mount_t *mp);
extern void	xfs_mountfs_check_barriers(xfs_mount_t *mp);

extern void	xfs_unmountfs(xfs_mount_t *);
extern int	xfs_unmountfs_writesb(xfs_mount_t *);
extern int	xfs_unmount_flush(xfs_mount_t *, int);
extern int	xfs_mod_incore_sb(xfs_mount_t *, xfs_sb_field_t, int64_t, int);
extern int	xfs_mod_incore_sb_unlocked(xfs_mount_t *, xfs_sb_field_t,
			int64_t, int);
extern int	xfs_mod_incore_sb_batch(xfs_mount_t *, xfs_mod_sb_t *,
			uint, int);
extern struct xfs_buf *xfs_getsb(xfs_mount_t *, int);
extern int	xfs_readsb(xfs_mount_t *, int);
extern void	xfs_freesb(xfs_mount_t *);
extern int	xfs_fs_writable(xfs_mount_t *);
extern int	xfs_syncsub(xfs_mount_t *, int, int *);
extern int	xfs_sync_inodes(xfs_mount_t *, int, int *);
extern xfs_agnumber_t	xfs_initialize_perag(xfs_mount_t *, xfs_agnumber_t);
extern void	xfs_sb_from_disk(struct xfs_sb *, struct xfs_dsb *);
extern void	xfs_sb_to_disk(struct xfs_dsb *, struct xfs_sb *, __int64_t);
extern int	xfs_sb_validate_fsb_count(struct xfs_sb *, __uint64_t);

extern int	xfs_dmops_get(struct xfs_mount *, struct xfs_mount_args *);
extern void	xfs_dmops_put(struct xfs_mount *);
extern int	xfs_qmops_get(struct xfs_mount *, struct xfs_mount_args *);
extern void	xfs_qmops_put(struct xfs_mount *);

extern struct xfs_dmops xfs_dmcore_xfs;

#endif	/* __KERNEL__ */
 * Per-ag incore structure, copies of information in agf and agi, to improve the
 * performance of allocation group selection.
 */
typedef struct xfs_perag {
	struct xfs_mount *pag_mount;	/* owner filesystem */
	xfs_agnumber_t	pag_agno;	/* AG this structure belongs to */
	atomic_t	pag_ref;	/* perag reference count */
	char		pagf_init;	/* this agf's entry is initialized */
	char		pagi_init;	/* this agi's entry is initialized */
	char		pagf_metadata;	/* the agf is preferred to be metadata */
	char		pagi_inodeok;	/* The agi is ok for inodes */
	__uint8_t	pagf_levels[XFS_BTNUM_AGF];
					/* # of levels in bno & cnt btree */
	bool		pagf_agflreset; /* agfl requires reset before use */
	__uint32_t	pagf_flcount;	/* count of blocks in freelist */
	xfs_extlen_t	pagf_freeblks;	/* total free blocks */
	xfs_extlen_t	pagf_longest;	/* longest free space */
	__uint32_t	pagf_btreeblks;	/* # of blocks held in AGF btrees */
	xfs_agino_t	pagi_freecount;	/* number of free inodes */
	xfs_agino_t	pagi_count;	/* number of allocated inodes */

	/*
	 * Inode allocation search lookup optimisation.
	 * If the pagino matches, the search for new inodes
	 * doesn't need to search the near ones again straight away
	 */
	xfs_agino_t	pagl_pagino;
	xfs_agino_t	pagl_leftrec;
	xfs_agino_t	pagl_rightrec;
	spinlock_t	pagb_lock;	/* lock for pagb_tree */
	struct rb_root	pagb_tree;	/* ordered tree of busy extents */

	atomic_t        pagf_fstrms;    /* # of filestreams active in this AG */

	spinlock_t	pag_ici_lock;	/* incore inode cache lock */
	struct radix_tree_root pag_ici_root;	/* incore inode cache root */
	int		pag_ici_reclaimable;	/* reclaimable inodes */
	struct mutex	pag_ici_reclaim_lock;	/* serialisation point */
	unsigned long	pag_ici_reclaim_cursor;	/* reclaim restart point */

	/* buffer cache index */
	spinlock_t	pag_buf_lock;	/* lock for pag_buf_tree */
	struct rb_root	pag_buf_tree;	/* ordered tree of active buffers */

	/* for rcu-safe freeing */
	struct rcu_head	rcu_head;
	int		pagb_count;	/* pagb slots in use */
} xfs_perag_t;

extern void	xfs_uuid_table_free(void);
extern int	xfs_log_sbcount(xfs_mount_t *);
extern __uint64_t xfs_default_resblks(xfs_mount_t *mp);
extern int	xfs_mountfs(xfs_mount_t *mp);
extern int	xfs_initialize_perag(xfs_mount_t *mp, xfs_agnumber_t agcount,
				     xfs_agnumber_t *maxagi);
extern void	xfs_unmountfs(xfs_mount_t *);

extern int	xfs_mod_icount(struct xfs_mount *mp, int64_t delta);
extern int	xfs_mod_ifree(struct xfs_mount *mp, int64_t delta);
extern int	xfs_mod_fdblocks(struct xfs_mount *mp, int64_t delta,
				 bool reserved);
extern int	xfs_mod_frextents(struct xfs_mount *mp, int64_t delta);

extern int	xfs_mount_log_sb(xfs_mount_t *);
extern struct xfs_buf *xfs_getsb(xfs_mount_t *, int);
extern int	xfs_readsb(xfs_mount_t *, int);
extern void	xfs_freesb(xfs_mount_t *);
extern bool	xfs_fs_writable(struct xfs_mount *mp, int level);
extern int	xfs_sb_validate_fsb_count(struct xfs_sb *, __uint64_t);

extern int	xfs_dev_is_read_only(struct xfs_mount *, char *);

extern void	xfs_set_low_space_thresholds(struct xfs_mount *);

int	xfs_zero_extent(struct xfs_inode *ip, xfs_fsblock_t start_fsb,
			xfs_off_t count_fsb);

#endif	/* __XFS_MOUNT_H__ */
