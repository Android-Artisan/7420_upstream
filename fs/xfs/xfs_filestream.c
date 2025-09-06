/*
 * Copyright (c) 2006-2007 Silicon Graphics, Inc.
 * Copyright (c) 2014 Christoph Hellwig.
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
#include "xfs.h"
#include "xfs_bmap_btree.h"
#include "xfs_inum.h"
#include "xfs_dir2.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_ag.h"
#include "xfs_dmapi.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_bmap.h"
#include "xfs_alloc.h"
#include "xfs_utils.h"
#include "xfs_mru_cache.h"
#include "xfs_filestream.h"

#ifdef XFS_FILESTREAMS_TRACE

ktrace_t *xfs_filestreams_trace_buf;

STATIC void
xfs_filestreams_trace(
	xfs_mount_t	*mp,	/* mount point */
	int		type,	/* type of trace */
	const char	*func,	/* source function */
	int		line,	/* source line number */
	__psunsigned_t	arg0,
	__psunsigned_t	arg1,
	__psunsigned_t	arg2,
	__psunsigned_t	arg3,
	__psunsigned_t	arg4,
	__psunsigned_t	arg5)
{
	ktrace_enter(xfs_filestreams_trace_buf,
		(void *)(__psint_t)(type | (line << 16)),
		(void *)func,
		(void *)(__psunsigned_t)current_pid(),
		(void *)mp,
		(void *)(__psunsigned_t)arg0,
		(void *)(__psunsigned_t)arg1,
		(void *)(__psunsigned_t)arg2,
		(void *)(__psunsigned_t)arg3,
		(void *)(__psunsigned_t)arg4,
		(void *)(__psunsigned_t)arg5,
		NULL, NULL, NULL, NULL, NULL, NULL);
}

#define TRACE0(mp,t)			TRACE6(mp,t,0,0,0,0,0,0)
#define TRACE1(mp,t,a0)			TRACE6(mp,t,a0,0,0,0,0,0)
#define TRACE2(mp,t,a0,a1)		TRACE6(mp,t,a0,a1,0,0,0,0)
#define TRACE3(mp,t,a0,a1,a2)		TRACE6(mp,t,a0,a1,a2,0,0,0)
#define TRACE4(mp,t,a0,a1,a2,a3)	TRACE6(mp,t,a0,a1,a2,a3,0,0)
#define TRACE5(mp,t,a0,a1,a2,a3,a4)	TRACE6(mp,t,a0,a1,a2,a3,a4,0)
#define TRACE6(mp,t,a0,a1,a2,a3,a4,a5) \
	xfs_filestreams_trace(mp, t, __func__, __LINE__, \
				(__psunsigned_t)a0, (__psunsigned_t)a1, \
				(__psunsigned_t)a2, (__psunsigned_t)a3, \
				(__psunsigned_t)a4, (__psunsigned_t)a5)

#define TRACE_AG_SCAN(mp, ag, ag2) \
		TRACE2(mp, XFS_FSTRM_KTRACE_AGSCAN, ag, ag2);
#define TRACE_AG_PICK1(mp, max_ag, maxfree) \
		TRACE2(mp, XFS_FSTRM_KTRACE_AGPICK1, max_ag, maxfree);
#define TRACE_AG_PICK2(mp, ag, ag2, cnt, free, scan, flag) \
		TRACE6(mp, XFS_FSTRM_KTRACE_AGPICK2, ag, ag2, \
			 cnt, free, scan, flag)
#define TRACE_UPDATE(mp, ip, ag, cnt, ag2, cnt2) \
		TRACE5(mp, XFS_FSTRM_KTRACE_UPDATE, ip, ag, cnt, ag2, cnt2)
#define TRACE_FREE(mp, ip, pip, ag, cnt) \
		TRACE4(mp, XFS_FSTRM_KTRACE_FREE, ip, pip, ag, cnt)
#define TRACE_LOOKUP(mp, ip, pip, ag, cnt) \
		TRACE4(mp, XFS_FSTRM_KTRACE_ITEM_LOOKUP, ip, pip, ag, cnt)
#define TRACE_ASSOCIATE(mp, ip, pip, ag, cnt) \
		TRACE4(mp, XFS_FSTRM_KTRACE_ASSOCIATE, ip, pip, ag, cnt)
#define TRACE_MOVEAG(mp, ip, pip, oag, ocnt, nag, ncnt) \
		TRACE6(mp, XFS_FSTRM_KTRACE_MOVEAG, ip, pip, oag, ocnt, nag, ncnt)
#define TRACE_ORPHAN(mp, ip, ag) \
		TRACE2(mp, XFS_FSTRM_KTRACE_ORPHAN, ip, ag);


#else
#define TRACE_AG_SCAN(mp, ag, ag2)
#define TRACE_AG_PICK1(mp, max_ag, maxfree)
#define TRACE_AG_PICK2(mp, ag, ag2, cnt, free, scan, flag)
#define TRACE_UPDATE(mp, ip, ag, cnt, ag2, cnt2)
#define TRACE_FREE(mp, ip, pip, ag, cnt)
#define TRACE_LOOKUP(mp, ip, pip, ag, cnt)
#define TRACE_ASSOCIATE(mp, ip, pip, ag, cnt)
#define TRACE_MOVEAG(mp, ip, pip, oag, ocnt, nag, ncnt)
#define TRACE_ORPHAN(mp, ip, ag)
#endif

static kmem_zone_t *item_zone;

/*
 * Structure for associating a file or a directory with an allocation group.
 * The parent directory pointer is only needed for files, but since there will
 * generally be vastly more files than directories in the cache, using the same
 * data structure simplifies the code with very little memory overhead.
 */
typedef struct fstrm_item
{
	xfs_agnumber_t	ag;	/* AG currently in use for the file/directory. */
	xfs_inode_t	*ip;	/* inode self-pointer. */
	xfs_inode_t	*pip;	/* Parent directory inode pointer. */
} fstrm_item_t;

#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_bmap.h"
#include "xfs_bmap_util.h"
#include "xfs_alloc.h"
#include "xfs_mru_cache.h"
#include "xfs_filestream.h"
#include "xfs_trace.h"

struct xfs_fstrm_item {
	struct xfs_mru_cache_elem	mru;
	struct xfs_inode		*ip;
	xfs_agnumber_t			ag; /* AG in use for this directory */
};

enum xfs_fstrm_alloc {
	XFS_PICK_USERDATA = 1,
	XFS_PICK_LOWSPACE = 2,
};

/*
 * Allocation group filestream associations are tracked with per-ag atomic
 * counters.  These counters allow xfs_filestream_pick_ag() to tell whether a
 * particular AG already has active filestreams associated with it. The mount
 * point's m_peraglock is used to protect these counters from per-ag array
 * re-allocation during a growfs operation.  When xfs_growfs_data_private() is
 * about to reallocate the array, it calls xfs_filestream_flush() with the
 * m_peraglock held in write mode.
 *
 * Since xfs_mru_cache_flush() guarantees that all the free functions for all
 * the cache elements have finished executing before it returns, it's safe for
 * the free functions to use the atomic counters without m_peraglock protection.
 * This allows the implementation of xfs_fstrm_free_func() to be agnostic about
 * whether it was called with the m_peraglock held in read mode, write mode or
 * not held at all.  The race condition this addresses is the following:
 *
 *  - The work queue scheduler fires and pulls a filestream directory cache
 *    element off the LRU end of the cache for deletion, then gets pre-empted.
 *  - A growfs operation grabs the m_peraglock in write mode, flushes all the
 *    remaining items from the cache and reallocates the mount point's per-ag
 *    array, resetting all the counters to zero.
 *  - The work queue thread resumes and calls the free function for the element
 *    it started cleaning up earlier.  In the process it decrements the
 *    filestreams counter for an AG that now has no references.
 *
 * With a shrinkfs feature, the above scenario could panic the system.
 *
 * All other uses of the following macros should be protected by either the
 * m_peraglock held in read mode, or the cache's internal locking exposed by the
 * interval between a call to xfs_mru_cache_lookup() and a call to
 * xfs_mru_cache_done().  In addition, the m_peraglock must be held in read mode
 * when new elements are added to the cache.
 *
 * Combined, these locking rules ensure that no associations will ever exist in
 * the cache that reference per-ag array elements that have since been
 * reallocated.
 */
int
xfs_filestream_peek_ag(
	xfs_mount_t	*mp,
	xfs_agnumber_t	agno)
{
	struct xfs_perag *pag;
	int		ret;

	pag = xfs_perag_get(mp, agno);
	ret = atomic_read(&pag->pagf_fstrms);
	xfs_perag_put(pag);
	return ret;
}

static int
xfs_filestream_get_ag(
	xfs_mount_t	*mp,
	xfs_agnumber_t	agno)
{
	struct xfs_perag *pag;
	int		ret;

	pag = xfs_perag_get(mp, agno);
	ret = atomic_inc_return(&pag->pagf_fstrms);
	xfs_perag_put(pag);
	return ret;
}

static void
xfs_filestream_put_ag(
	xfs_mount_t	*mp,
	xfs_agnumber_t	agno)
{
	struct xfs_perag *pag;

	pag = xfs_perag_get(mp, agno);
	atomic_dec(&pag->pagf_fstrms);
	xfs_perag_put(pag);
}

static void
xfs_fstrm_free_func(
	struct xfs_mru_cache_elem *mru)
{
	struct xfs_fstrm_item	*item =
		container_of(mru, struct xfs_fstrm_item, mru);

	xfs_filestream_put_ag(item->ip->i_mount, item->ag);

	trace_xfs_filestream_free(item->ip, item->ag);

	kmem_free(item);
}

/*
 * Scan the AGs starting at startag looking for an AG that isn't in use and has
 * at least minlen blocks free.
 */
static int
_xfs_filestream_pick_ag(
	xfs_mount_t	*mp,
	xfs_agnumber_t	startag,
	xfs_agnumber_t	*agp,
	int		flags,
	xfs_extlen_t	minlen)
{
	int		err, trylock, nscan;
	xfs_extlen_t	delta, longest, need, free, minfree, maxfree = 0;
	xfs_agnumber_t	ag, max_ag = NULLAGNUMBER;
	struct xfs_perag *pag;
xfs_filestream_pick_ag(
	struct xfs_inode	*ip,
	xfs_agnumber_t		startag,
	xfs_agnumber_t		*agp,
	int			flags,
	xfs_extlen_t		minlen)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_fstrm_item	*item;
	struct xfs_perag	*pag;
	xfs_extlen_t		longest, free = 0, minfree, maxfree = 0;
	xfs_agnumber_t		ag, max_ag = NULLAGNUMBER;
	int			err, trylock, nscan;

	ASSERT(S_ISDIR(ip->i_d.di_mode));

	/* 2% of an AG's blocks must be free for it to be chosen. */
	minfree = mp->m_sb.sb_agblocks / 50;

	ag = startag;
	*agp = NULLAGNUMBER;

	/* For the first pass, don't sleep trying to init the per-AG. */
	trylock = XFS_ALLOC_FLAG_TRYLOCK;

	for (nscan = 0; 1; nscan++) {

		TRACE_AG_SCAN(mp, ag, xfs_filestream_peek_ag(mp, ag));

		pag = mp->m_perag + ag;

		if (!pag->pagf_init) {
			err = xfs_alloc_pagf_init(mp, NULL, ag, trylock);
			if (err && !trylock)
				return err;
		trace_xfs_filestream_scan(ip, ag);

		pag = xfs_perag_get(mp, ag);

		if (!pag->pagf_init) {
			err = xfs_alloc_pagf_init(mp, NULL, ag, trylock);
			if (err && !trylock) {
				xfs_perag_put(pag);
				return err;
			}
		}

		/* Might fail sometimes during the 1st pass with trylock set. */
		if (!pag->pagf_init)
			goto next_ag;

		/* Keep track of the AG with the most free blocks. */
		if (pag->pagf_freeblks > maxfree) {
			maxfree = pag->pagf_freeblks;
			max_ag = ag;
		}

		/*
		 * The AG reference count does two things: it enforces mutual
		 * exclusion when examining the suitability of an AG in this
		 * loop, and it guards against two filestreams being established
		 * in the same AG as each other.
		 */
		if (xfs_filestream_get_ag(mp, ag) > 1) {
			xfs_filestream_put_ag(mp, ag);
			goto next_ag;
		}

		need = XFS_MIN_FREELIST_PAG(pag, mp);
		delta = need > pag->pagf_flcount ? need - pag->pagf_flcount : 0;
		longest = (pag->pagf_longest > delta) ?
		          (pag->pagf_longest - delta) :
		          (pag->pagf_flcount > 0 || pag->pagf_longest > 0);

		longest = xfs_alloc_longest_free_extent(mp, pag,
					xfs_alloc_min_freelist(mp, pag));
		if (((minlen && longest >= minlen) ||
		     (!minlen && pag->pagf_freeblks >= minfree)) &&
		    (!pag->pagf_metadata || !(flags & XFS_PICK_USERDATA) ||
		     (flags & XFS_PICK_LOWSPACE))) {

			/* Break out, retaining the reference on the AG. */
			free = pag->pagf_freeblks;
			xfs_perag_put(pag);
			*agp = ag;
			break;
		}

		/* Drop the reference on this AG, it's not usable. */
		xfs_filestream_put_ag(mp, ag);
next_ag:
		xfs_perag_put(pag);
		/* Move to the next AG, wrapping to AG 0 if necessary. */
		if (++ag >= mp->m_sb.sb_agcount)
			ag = 0;

		/* If a full pass of the AGs hasn't been done yet, continue. */
		if (ag != startag)
			continue;

		/* Allow sleeping in xfs_alloc_pagf_init() on the 2nd pass. */
		if (trylock != 0) {
			trylock = 0;
			continue;
		}

		/* Finally, if lowspace wasn't set, set it for the 3rd pass. */
		if (!(flags & XFS_PICK_LOWSPACE)) {
			flags |= XFS_PICK_LOWSPACE;
			continue;
		}

		/*
		 * Take the AG with the most free space, regardless of whether
		 * it's already in use by another filestream.
		 */
		if (max_ag != NULLAGNUMBER) {
			xfs_filestream_get_ag(mp, max_ag);
			TRACE_AG_PICK1(mp, max_ag, maxfree);
			free = maxfree;
			*agp = max_ag;
			break;
		}

		/* take AG 0 if none matched */
		TRACE_AG_PICK1(mp, max_ag, maxfree);
		trace_xfs_filestream_pick(ip, *agp, free, nscan);
		*agp = 0;
		return 0;
	}

	TRACE_AG_PICK2(mp, startag, *agp, xfs_filestream_peek_ag(mp, *agp),
			free, nscan, flags);

	return 0;
}

/*
 * Set the allocation group number for a file or a directory, updating inode
 * references and per-AG references as appropriate.  Must be called with the
 * m_peraglock held in read mode.
 */
static int
_xfs_filestream_update_ag(
	xfs_inode_t	*ip,
	xfs_inode_t	*pip,
	xfs_agnumber_t	ag)
{
	int		err = 0;
	xfs_mount_t	*mp;
	xfs_mru_cache_t	*cache;
	fstrm_item_t	*item;
	xfs_agnumber_t	old_ag;
	xfs_inode_t	*old_pip;

	/*
	 * Either ip is a regular file and pip is a directory, or ip is a
	 * directory and pip is NULL.
	 */
	ASSERT(ip && (((ip->i_d.di_mode & S_IFREG) && pip &&
	               (pip->i_d.di_mode & S_IFDIR)) ||
	              ((ip->i_d.di_mode & S_IFDIR) && !pip)));

	mp = ip->i_mount;
	cache = mp->m_filestream;

	item = xfs_mru_cache_lookup(cache, ip->i_ino);
	if (item) {
		ASSERT(item->ip == ip);
		old_ag = item->ag;
		item->ag = ag;
		old_pip = item->pip;
		item->pip = pip;
		xfs_mru_cache_done(cache);

		/*
		 * If the AG has changed, drop the old ref and take a new one,
		 * effectively transferring the reference from old to new AG.
		 */
		if (ag != old_ag) {
			xfs_filestream_put_ag(mp, old_ag);
			xfs_filestream_get_ag(mp, ag);
		}

		/*
		 * If ip is a file and its pip has changed, drop the old ref and
		 * take a new one.
		 */
		if (pip && pip != old_pip) {
			IRELE(old_pip);
			IHOLD(pip);
		}

		TRACE_UPDATE(mp, ip, old_ag, xfs_filestream_peek_ag(mp, old_ag),
				ag, xfs_filestream_peek_ag(mp, ag));
		return 0;
	}

	item = kmem_zone_zalloc(item_zone, KM_MAYFAIL);
	if (!item)
		return ENOMEM;

	item->ag = ag;
	item->ip = ip;
	item->pip = pip;

	err = xfs_mru_cache_insert(cache, ip->i_ino, item);
	if (err) {
		kmem_zone_free(item_zone, item);
		return err;
	}

	/* Take a reference on the AG. */
	xfs_filestream_get_ag(mp, ag);

	/*
	 * Take a reference on the inode itself regardless of whether it's a
	 * regular file or a directory.
	 */
	IHOLD(ip);

	/*
	 * In the case of a regular file, take a reference on the parent inode
	 * as well to ensure it remains in-core.
	 */
	if (pip)
		IHOLD(pip);

	TRACE_UPDATE(mp, ip, ag, xfs_filestream_peek_ag(mp, ag),
			ag, xfs_filestream_peek_ag(mp, ag));

	return 0;
}

/* xfs_fstrm_free_func(): callback for freeing cached stream items. */
STATIC void
xfs_fstrm_free_func(
	unsigned long	ino,
	void		*data)
{
	fstrm_item_t	*item  = (fstrm_item_t *)data;
	xfs_inode_t	*ip = item->ip;
	int ref;

	ASSERT(ip->i_ino == ino);

	xfs_iflags_clear(ip, XFS_IFILESTREAM);

	/* Drop the reference taken on the AG when the item was added. */
	ref = xfs_filestream_put_ag(ip->i_mount, item->ag);

	ASSERT(ref >= 0);
	TRACE_FREE(ip->i_mount, ip, item->pip, item->ag,
		xfs_filestream_peek_ag(ip->i_mount, item->ag));

	/*
	 * _xfs_filestream_update_ag() always takes a reference on the inode
	 * itself, whether it's a file or a directory.  Release it here.
	 * This can result in the inode being freed and so we must
	 * not hold any inode locks when freeing filesstreams objects
	 * otherwise we can deadlock here.
	 */
	IRELE(ip);

	/*
	 * In the case of a regular file, _xfs_filestream_update_ag() also
	 * takes a ref on the parent inode to keep it in-core.  Release that
	 * too.
	 */
	if (item->pip)
		IRELE(item->pip);

	/* Finally, free the memory allocated for the item. */
	kmem_zone_free(item_zone, item);
}

/*
 * xfs_filestream_init() is called at xfs initialisation time to set up the
 * memory zone that will be used for filestream data structure allocation.
 */
int
xfs_filestream_init(void)
{
	item_zone = kmem_zone_init(sizeof(fstrm_item_t), "fstrm_item");
	if (!item_zone)
		return -ENOMEM;
#ifdef XFS_FILESTREAMS_TRACE
	xfs_filestreams_trace_buf = ktrace_alloc(XFS_FSTRM_KTRACE_SIZE, KM_NOFS);
#endif
	return 0;
}

/*
 * xfs_filestream_uninit() is called at xfs termination time to destroy the
 * memory zone that was used for filestream data structure allocation.
 */
void
xfs_filestream_uninit(void)
{
#ifdef XFS_FILESTREAMS_TRACE
	ktrace_free(xfs_filestreams_trace_buf);
#endif
	kmem_zone_destroy(item_zone);
}

/*
 * xfs_filestream_mount() is called when a file system is mounted with the
 * filestream option.  It is responsible for allocating the data structures
 * needed to track the new file system's file streams.
 */
int
xfs_filestream_mount(
	xfs_mount_t	*mp)
{
	int		err;
	unsigned int	lifetime, grp_count;

	/*
	 * The filestream timer tunable is currently fixed within the range of
	 * one second to four minutes, with five seconds being the default.  The
	 * group count is somewhat arbitrary, but it'd be nice to adhere to the
	 * timer tunable to within about 10 percent.  This requires at least 10
	 * groups.
	 */
	lifetime  = xfs_fstrm_centisecs * 10;
	grp_count = 10;

	err = xfs_mru_cache_create(&mp->m_filestream, lifetime, grp_count,
	                     xfs_fstrm_free_func);

	return err;
}

/*
 * xfs_filestream_unmount() is called when a file system that was mounted with
 * the filestream option is unmounted.  It drains the data structures created
 * to track the file system's file streams and frees all the memory that was
 * allocated.
 */
void
xfs_filestream_unmount(
	xfs_mount_t	*mp)
{
	xfs_mru_cache_destroy(mp->m_filestream);
}

/*
 * If the mount point's m_perag array is going to be reallocated, all
 * outstanding cache entries must be flushed to avoid accessing reference count
 * addresses that have been freed.  The call to xfs_filestream_flush() must be
 * made inside the block that holds the m_peraglock in write mode to do the
 * reallocation.
 */
void
xfs_filestream_flush(
	xfs_mount_t	*mp)
{
	xfs_mru_cache_flush(mp->m_filestream);
}

/*
 * Return the AG of the filestream the file or directory belongs to, or
 * NULLAGNUMBER otherwise.
 */
xfs_agnumber_t
xfs_filestream_lookup_ag(
	xfs_inode_t	*ip)
{
	xfs_mru_cache_t	*cache;
	fstrm_item_t	*item;
	xfs_agnumber_t	ag;
	int		ref;

	if (!(ip->i_d.di_mode & (S_IFREG | S_IFDIR))) {
		ASSERT(0);
		return NULLAGNUMBER;
	}

	cache = ip->i_mount->m_filestream;
	item = xfs_mru_cache_lookup(cache, ip->i_ino);
	if (!item) {
		TRACE_LOOKUP(ip->i_mount, ip, NULL, NULLAGNUMBER, 0);
		return NULLAGNUMBER;
	}

	ASSERT(ip == item->ip);
	ag = item->ag;
	ref = xfs_filestream_peek_ag(ip->i_mount, ag);
	xfs_mru_cache_done(cache);

	TRACE_LOOKUP(ip->i_mount, ip, item->pip, ag, ref);
	return ag;
}

/*
 * xfs_filestream_associate() should only be called to associate a regular file
 * with its parent directory.  Calling it with a child directory isn't
 * appropriate because filestreams don't apply to entire directory hierarchies.
 * Creating a file in a child directory of an existing filestream directory
 * starts a new filestream with its own allocation group association.
 *
 * Returns < 0 on error, 0 if successful association occurred, > 0 if
 * we failed to get an association because of locking issues.
 */
int
xfs_filestream_associate(
	xfs_inode_t	*pip,
	xfs_inode_t	*ip)
{
	xfs_mount_t	*mp;
	xfs_mru_cache_t	*cache;
	fstrm_item_t	*item;
	xfs_agnumber_t	ag, rotorstep, startag;
	int		err = 0;

	ASSERT(pip->i_d.di_mode & S_IFDIR);
	ASSERT(ip->i_d.di_mode & S_IFREG);
	if (!(pip->i_d.di_mode & S_IFDIR) || !(ip->i_d.di_mode & S_IFREG))
		return -EINVAL;

	mp = pip->i_mount;
	cache = mp->m_filestream;
	down_read(&mp->m_peraglock);

	/*
	 * We have a problem, Houston.
	 *
	 * Taking the iolock here violates inode locking order - we already
	 * hold the ilock. Hence if we block getting this lock we may never
	 * wake. Unfortunately, that means if we can't get the lock, we're
	 * screwed in terms of getting a stream association - we can't spin
	 * waiting for the lock because someone else is waiting on the lock we
	 * hold and we cannot drop that as we are in a transaction here.
	 *
	 * Lucky for us, this inversion is rarely a problem because it's a
	 * directory inode that we are trying to lock here and that means the
	 * only place that matters is xfs_sync_inodes() and SYNC_DELWRI is
	 * used. i.e. freeze, remount-ro, quotasync or unmount.
	 *
	 * So, if we can't get the iolock without sleeping then just give up
	 */
	if (!xfs_ilock_nowait(pip, XFS_IOLOCK_EXCL)) {
		up_read(&mp->m_peraglock);
		return 1;
	}

	/* If the parent directory is already in the cache, use its AG. */
	item = xfs_mru_cache_lookup(cache, pip->i_ino);
	if (item) {
		ASSERT(item->ip == pip);
		ag = item->ag;
		xfs_mru_cache_done(cache);

		TRACE_LOOKUP(mp, pip, pip, ag, xfs_filestream_peek_ag(mp, ag));
		err = _xfs_filestream_update_ag(ip, pip, ag);

		goto exit;
	trace_xfs_filestream_pick(ip, *agp, free, nscan);

	if (*agp == NULLAGNUMBER)
		return 0;

	err = -ENOMEM;
	item = kmem_alloc(sizeof(*item), KM_MAYFAIL);
	if (!item)
		goto out_put_ag;

	item->ag = *agp;
	item->ip = ip;

	err = xfs_mru_cache_insert(mp->m_filestream, ip->i_ino, &item->mru);
	if (err) {
		if (err == -EEXIST)
			err = 0;
		goto out_free_item;
	}

	return 0;

out_free_item:
	kmem_free(item);
out_put_ag:
	xfs_filestream_put_ag(mp, *agp);
	return err;
}

static struct xfs_inode *
xfs_filestream_get_parent(
	struct xfs_inode	*ip)
{
	struct inode		*inode = VFS_I(ip), *dir = NULL;
	struct dentry		*dentry, *parent;

	dentry = d_find_alias(inode);
	if (!dentry)
		goto out;

	parent = dget_parent(dentry);
	if (!parent)
		goto out_dput;

	dir = igrab(d_inode(parent));
	dput(parent);

out_dput:
	dput(dentry);
out:
	return dir ? XFS_I(dir) : NULL;
}

/*
 * Find the right allocation group for a file, either by finding an
 * existing file stream or creating a new one.
 *
 * Returns NULLAGNUMBER in case of an error.
 */
xfs_agnumber_t
xfs_filestream_lookup_ag(
	struct xfs_inode	*ip)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_inode	*pip = NULL;
	xfs_agnumber_t		startag, ag = NULLAGNUMBER;
	struct xfs_mru_cache_elem *mru;

	ASSERT(S_ISREG(ip->i_d.di_mode));

	pip = xfs_filestream_get_parent(ip);
	if (!pip)
		return NULLAGNUMBER;

	mru = xfs_mru_cache_lookup(mp->m_filestream, pip->i_ino);
	if (mru) {
		ag = container_of(mru, struct xfs_fstrm_item, mru)->ag;
		xfs_mru_cache_done(mp->m_filestream);

		trace_xfs_filestream_lookup(ip, ag);
		goto out;
	}

	/*
	 * Set the starting AG using the rotor for inode32, otherwise
	 * use the directory inode's AG.
	 */
	if (mp->m_flags & XFS_MOUNT_32BITINODES) {
		rotorstep = xfs_rotorstep;
		xfs_agnumber_t	 rotorstep = xfs_rotorstep;
		startag = (mp->m_agfrotor / rotorstep) % mp->m_sb.sb_agcount;
		mp->m_agfrotor = (mp->m_agfrotor + 1) %
		                 (mp->m_sb.sb_agcount * rotorstep);
	} else
		startag = XFS_INO_TO_AGNO(mp, pip->i_ino);

	/* Pick a new AG for the parent inode starting at startag. */
	err = _xfs_filestream_pick_ag(mp, startag, &ag, 0, 0);
	if (err || ag == NULLAGNUMBER)
		goto exit_did_pick;

	/* Associate the parent inode with the AG. */
	err = _xfs_filestream_update_ag(pip, NULL, ag);
	if (err)
		goto exit_did_pick;

	/* Associate the file inode with the AG. */
	err = _xfs_filestream_update_ag(ip, pip, ag);
	if (err)
		goto exit_did_pick;

	TRACE_ASSOCIATE(mp, ip, pip, ag, xfs_filestream_peek_ag(mp, ag));

exit_did_pick:
	/*
	 * If _xfs_filestream_pick_ag() returned a valid AG, remove the
	 * reference it took on it, since the file and directory will have taken
	 * their own now if they were successfully cached.
	 */
	if (ag != NULLAGNUMBER)
		xfs_filestream_put_ag(mp, ag);

exit:
	xfs_iunlock(pip, XFS_IOLOCK_EXCL);
	up_read(&mp->m_peraglock);
	return -err;
}

/*
 * Pick a new allocation group for the current file and its file stream.  This
 * function is called by xfs_bmap_filestreams() with the mount point's per-ag
 * lock held.
 */
int
xfs_filestream_new_ag(
	xfs_bmalloca_t	*ap,
	xfs_agnumber_t	*agp)
{
	int		flags, err;
	xfs_inode_t	*ip, *pip = NULL;
	xfs_mount_t	*mp;
	xfs_mru_cache_t	*cache;
	xfs_extlen_t	minlen;
	fstrm_item_t	*dir, *file;
	xfs_agnumber_t	ag = NULLAGNUMBER;

	ip = ap->ip;
	mp = ip->i_mount;
	cache = mp->m_filestream;
	minlen = ap->alen;
	*agp = NULLAGNUMBER;

	/*
	 * Look for the file in the cache, removing it if it's found.  Doing
	 * this allows it to be held across the dir lookup that follows.
	 */
	file = xfs_mru_cache_remove(cache, ip->i_ino);
	if (file) {
		ASSERT(ip == file->ip);

		/* Save the file's parent inode and old AG number for later. */
		pip = file->pip;
		ag = file->ag;

		/* Look for the file's directory in the cache. */
		dir = xfs_mru_cache_lookup(cache, pip->i_ino);
		if (dir) {
			ASSERT(pip == dir->ip);

			/*
			 * If the directory has already moved on to a new AG,
			 * use that AG as the new AG for the file. Don't
			 * forget to twiddle the AG refcounts to match the
			 * movement.
			 */
			if (dir->ag != file->ag) {
				xfs_filestream_put_ag(mp, file->ag);
				xfs_filestream_get_ag(mp, dir->ag);
				*agp = file->ag = dir->ag;
			}

			xfs_mru_cache_done(cache);
		}

		/*
		 * Put the file back in the cache.  If this fails, the free
		 * function needs to be called to tidy up in the same way as if
		 * the item had simply expired from the cache.
		 */
		err = xfs_mru_cache_insert(cache, ip->i_ino, file);
		if (err) {
			xfs_fstrm_free_func(ip->i_ino, file);
			return err;
		}

		/*
		 * If the file's AG was moved to the directory's new AG, there's
		 * nothing more to be done.
		 */
		if (*agp != NULLAGNUMBER) {
			TRACE_MOVEAG(mp, ip, pip,
					ag, xfs_filestream_peek_ag(mp, ag),
					*agp, xfs_filestream_peek_ag(mp, *agp));
			return 0;
		}
	}

	/*
	 * If the file's parent directory is known, take its iolock in exclusive
	 * mode to prevent two sibling files from racing each other to migrate
	 * themselves and their parent to different AGs.
	 */
	if (pip)
		xfs_ilock(pip, XFS_IOLOCK_EXCL);

	/*
	 * A new AG needs to be found for the file.  If the file's parent
	 * directory is also known, it will be moved to the new AG as well to
	 * ensure that files created inside it in future use the new AG.
	 */
	ag = (ag == NULLAGNUMBER) ? 0 : (ag + 1) % mp->m_sb.sb_agcount;
	flags = (ap->userdata ? XFS_PICK_USERDATA : 0) |
	        (ap->low ? XFS_PICK_LOWSPACE : 0);

	err = _xfs_filestream_pick_ag(mp, ag, agp, flags, minlen);
	if (err || *agp == NULLAGNUMBER)
		goto exit;

	/*
	 * If the file wasn't found in the file cache, then its parent directory
	 * inode isn't known.  For this to have happened, the file must either
	 * be pre-existing, or it was created long enough ago that its cache
	 * entry has expired.  This isn't the sort of usage that the filestreams
	 * allocator is trying to optimise, so there's no point trying to track
	 * its new AG somehow in the filestream data structures.
	 */
	if (!pip) {
		TRACE_ORPHAN(mp, ip, *agp);
		goto exit;
	}

	/* Associate the parent inode with the AG. */
	err = _xfs_filestream_update_ag(pip, NULL, *agp);
	if (err)
		goto exit;

	/* Associate the file inode with the AG. */
	err = _xfs_filestream_update_ag(ip, pip, *agp);
	if (err)
		goto exit;

	TRACE_MOVEAG(mp, ip, pip, NULLAGNUMBER, 0,
			*agp, xfs_filestream_peek_ag(mp, *agp));

exit:
	/*
	 * If _xfs_filestream_pick_ag() returned a valid AG, remove the
	 * reference it took on it, since the file and directory will have taken
	 * their own now if they were successfully cached.
	 */
	if (*agp != NULLAGNUMBER)
		xfs_filestream_put_ag(mp, *agp);
	else
		*agp = 0;

	if (pip)
		xfs_iunlock(pip, XFS_IOLOCK_EXCL);

	return err;
}

/*
 * Remove an association between an inode and a filestream object.
 * Typically this is done on last close of an unlinked file.
 */
void
xfs_filestream_deassociate(
	xfs_inode_t	*ip)
{
	xfs_mru_cache_t	*cache = ip->i_mount->m_filestream;

	xfs_mru_cache_delete(cache, ip->i_ino);
	if (xfs_filestream_pick_ag(pip, startag, &ag, 0, 0))
		ag = NULLAGNUMBER;
out:
	IRELE(pip);
	return ag;
}

/*
 * Pick a new allocation group for the current file and its file stream.
 *
 * This is called when the allocator can't find a suitable extent in the
 * current AG, and we have to move the stream into a new AG with more space.
 */
int
xfs_filestream_new_ag(
	struct xfs_bmalloca	*ap,
	xfs_agnumber_t		*agp)
{
	struct xfs_inode	*ip = ap->ip, *pip;
	struct xfs_mount	*mp = ip->i_mount;
	xfs_extlen_t		minlen = ap->length;
	xfs_agnumber_t		startag = 0;
	int			flags, err = 0;
	struct xfs_mru_cache_elem *mru;

	*agp = NULLAGNUMBER;

	pip = xfs_filestream_get_parent(ip);
	if (!pip)
		goto exit;

	mru = xfs_mru_cache_remove(mp->m_filestream, pip->i_ino);
	if (mru) {
		struct xfs_fstrm_item *item =
			container_of(mru, struct xfs_fstrm_item, mru);
		startag = (item->ag + 1) % mp->m_sb.sb_agcount;
	}

	flags = (ap->userdata ? XFS_PICK_USERDATA : 0) |
	        (ap->flist->xbf_low ? XFS_PICK_LOWSPACE : 0);

	err = xfs_filestream_pick_ag(pip, startag, agp, flags, minlen);

	/*
	 * Only free the item here so we skip over the old AG earlier.
	 */
	if (mru)
		xfs_fstrm_free_func(mru);

	IRELE(pip);
exit:
	if (*agp == NULLAGNUMBER)
		*agp = 0;
	return err;
}

void
xfs_filestream_deassociate(
	struct xfs_inode	*ip)
{
	xfs_mru_cache_delete(ip->i_mount->m_filestream, ip->i_ino);
}

int
xfs_filestream_mount(
	xfs_mount_t	*mp)
{
	/*
	 * The filestream timer tunable is currently fixed within the range of
	 * one second to four minutes, with five seconds being the default.  The
	 * group count is somewhat arbitrary, but it'd be nice to adhere to the
	 * timer tunable to within about 10 percent.  This requires at least 10
	 * groups.
	 */
	return xfs_mru_cache_create(&mp->m_filestream, xfs_fstrm_centisecs * 10,
				    10, xfs_fstrm_free_func);
}

void
xfs_filestream_unmount(
	xfs_mount_t	*mp)
{
	xfs_mru_cache_destroy(mp->m_filestream);
}
