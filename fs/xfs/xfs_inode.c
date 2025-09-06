/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
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
#include <linux/log2.h>

#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_types.h"
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_imap.h"
#include "xfs_trans.h"
#include "xfs_trans_priv.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir2.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_buf_item.h"
#include "xfs_inode_item.h"
#include "xfs_btree.h"
#include "xfs_alloc.h"
#include "xfs_ialloc.h"
#include "xfs_bmap.h"
#include "xfs_rw.h"
#include "xfs_error.h"
#include "xfs_utils.h"
#include "xfs_dir2_trace.h"
#include "xfs_quota.h"
#include "xfs_acl.h"
#include "xfs_filestream.h"
#include "xfs_vnodeops.h"

kmem_zone_t *xfs_ifork_zone;
kmem_zone_t *xfs_inode_zone;

/*
 * Used in xfs_itruncate().  This is the maximum number of extents
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"
#include "xfs_dir2.h"
#include "xfs_attr_sf.h"
#include "xfs_attr.h"
#include "xfs_trans_space.h"
#include "xfs_trans.h"
#include "xfs_buf_item.h"
#include "xfs_inode_item.h"
#include "xfs_ialloc.h"
#include "xfs_bmap.h"
#include "xfs_bmap_util.h"
#include "xfs_error.h"
#include "xfs_quota.h"
#include "xfs_filestream.h"
#include "xfs_cksum.h"
#include "xfs_trace.h"
#include "xfs_icache.h"
#include "xfs_symlink.h"
#include "xfs_trans_priv.h"
#include "xfs_log.h"
#include "xfs_bmap_btree.h"

kmem_zone_t *xfs_inode_zone;

/*
 * Used in xfs_itruncate_extents().  This is the maximum number of extents
 * freed from a file in a single transaction.
 */
#define	XFS_ITRUNC_MAX_EXTENTS	2

STATIC int xfs_iflush_int(xfs_inode_t *, xfs_buf_t *);
STATIC int xfs_iformat_local(xfs_inode_t *, xfs_dinode_t *, int, int);
STATIC int xfs_iformat_extents(xfs_inode_t *, xfs_dinode_t *, int);
STATIC int xfs_iformat_btree(xfs_inode_t *, xfs_dinode_t *, int);

#ifdef DEBUG
/*
 * Make sure that the extents in the given memory buffer
 * are valid.
 */
STATIC void
xfs_validate_extents(
	xfs_ifork_t		*ifp,
	int			nrecs,
	xfs_exntfmt_t		fmt)
{
	xfs_bmbt_irec_t		irec;
	xfs_bmbt_rec_host_t	rec;
	int			i;

	for (i = 0; i < nrecs; i++) {
		xfs_bmbt_rec_host_t *ep = xfs_iext_get_ext(ifp, i);
		rec.l0 = get_unaligned(&ep->l0);
		rec.l1 = get_unaligned(&ep->l1);
		xfs_bmbt_get_all(&rec, &irec);
		if (fmt == XFS_EXTFMT_NOSTATE)
			ASSERT(irec.br_state == XFS_EXT_NORM);
	}
}
#else /* DEBUG */
#define xfs_validate_extents(ifp, nrecs, fmt)
#endif /* DEBUG */

/*
 * Check that none of the inode's in the buffer have a next
 * unlinked field of 0.
 */
#if defined(DEBUG)
void
xfs_inobp_check(
	xfs_mount_t	*mp,
	xfs_buf_t	*bp)
{
	int		i;
	int		j;
	xfs_dinode_t	*dip;

	j = mp->m_inode_cluster_size >> mp->m_sb.sb_inodelog;

	for (i = 0; i < j; i++) {
		dip = (xfs_dinode_t *)xfs_buf_offset(bp,
					i * mp->m_sb.sb_inodesize);
		if (!dip->di_next_unlinked)  {
			xfs_fs_cmn_err(CE_ALERT, mp,
				"Detected a bogus zero next_unlinked field in incore inode buffer 0x%p.  About to pop an ASSERT.",
				bp);
			ASSERT(dip->di_next_unlinked);
		}
	}
}
#endif

/*
 * Find the buffer associated with the given inode map
 * We do basic validation checks on the buffer once it has been
 * retrieved from disk.
 */
STATIC int
xfs_imap_to_bp(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_imap_t	*imap,
	xfs_buf_t	**bpp,
	uint		buf_flags,
	uint		imap_flags)
{
	int		error;
	int		i;
	int		ni;
	xfs_buf_t	*bp;

	error = xfs_trans_read_buf(mp, tp, mp->m_ddev_targp, imap->im_blkno,
				   (int)imap->im_len, buf_flags, &bp);
	if (error) {
		if (error != EAGAIN) {
			cmn_err(CE_WARN,
				"xfs_imap_to_bp: xfs_trans_read_buf()returned "
				"an error %d on %s.  Returning error.",
				error, mp->m_fsname);
		} else {
			ASSERT(buf_flags & XFS_BUF_TRYLOCK);
		}
		return error;
	}

	/*
	 * Validate the magic number and version of every inode in the buffer
	 * (if DEBUG kernel) or the first inode in the buffer, otherwise.
	 */
#ifdef DEBUG
	ni = BBTOB(imap->im_len) >> mp->m_sb.sb_inodelog;
#else	/* usual case */
	ni = 1;
#endif

	for (i = 0; i < ni; i++) {
		int		di_ok;
		xfs_dinode_t	*dip;

		dip = (xfs_dinode_t *)xfs_buf_offset(bp,
					(i << mp->m_sb.sb_inodelog));
		di_ok = be16_to_cpu(dip->di_core.di_magic) == XFS_DINODE_MAGIC &&
			    XFS_DINODE_GOOD_VERSION(dip->di_core.di_version);
		if (unlikely(XFS_TEST_ERROR(!di_ok, mp,
						XFS_ERRTAG_ITOBP_INOTOBP,
						XFS_RANDOM_ITOBP_INOTOBP))) {
			if (imap_flags & XFS_IMAP_BULKSTAT) {
				xfs_trans_brelse(tp, bp);
				return XFS_ERROR(EINVAL);
			}
			XFS_CORRUPTION_ERROR("xfs_imap_to_bp",
						XFS_ERRLEVEL_HIGH, mp, dip);
#ifdef DEBUG
			cmn_err(CE_PANIC,
					"Device %s - bad inode magic/vsn "
					"daddr %lld #%d (magic=%x)",
				XFS_BUFTARG_NAME(mp->m_ddev_targp),
				(unsigned long long)imap->im_blkno, i,
				be16_to_cpu(dip->di_core.di_magic));
#endif
			xfs_trans_brelse(tp, bp);
			return XFS_ERROR(EFSCORRUPTED);
		}
	}

	xfs_inobp_check(mp, bp);

	/*
	 * Mark the buffer as an inode buffer now that it looks good
	 */
	XFS_BUF_SET_VTYPE(bp, B_FS_INO);

	*bpp = bp;
	return 0;
}

/*
 * This routine is called to map an inode number within a file
 * system to the buffer containing the on-disk version of the
 * inode.  It returns a pointer to the buffer containing the
 * on-disk inode in the bpp parameter, and in the dip parameter
 * it returns a pointer to the on-disk inode within that buffer.
 *
 * If a non-zero error is returned, then the contents of bpp and
 * dipp are undefined.
 *
 * Use xfs_imap() to determine the size and location of the
 * buffer to read from disk.
 */
STATIC int
xfs_inotobp(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_ino_t	ino,
	xfs_dinode_t	**dipp,
	xfs_buf_t	**bpp,
	int		*offset)
{
	xfs_imap_t	imap;
	xfs_buf_t	*bp;
	int		error;

	imap.im_blkno = 0;
	error = xfs_imap(mp, tp, ino, &imap, XFS_IMAP_LOOKUP);
	if (error)
		return error;

	error = xfs_imap_to_bp(mp, tp, &imap, &bp, XFS_BUF_LOCK, 0);
	if (error)
		return error;

	*dipp = (xfs_dinode_t *)xfs_buf_offset(bp, imap.im_boffset);
	*bpp = bp;
	*offset = imap.im_boffset;
	return 0;
}


/*
 * This routine is called to map an inode to the buffer containing
 * the on-disk version of the inode.  It returns a pointer to the
 * buffer containing the on-disk inode in the bpp parameter, and in
 * the dip parameter it returns a pointer to the on-disk inode within
 * that buffer.
 *
 * If a non-zero error is returned, then the contents of bpp and
 * dipp are undefined.
 *
 * If the inode is new and has not yet been initialized, use xfs_imap()
 * to determine the size and location of the buffer to read from disk.
 * If the inode has already been mapped to its buffer and read in once,
 * then use the mapping information stored in the inode rather than
 * calling xfs_imap().  This allows us to avoid the overhead of looking
 * at the inode btree for small block file systems (see xfs_dilocate()).
 * We can tell whether the inode has been mapped in before by comparing
 * its disk block address to 0.  Only uninitialized inodes will have
 * 0 for the disk block address.
 */
int
xfs_itobp(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_inode_t	*ip,
	xfs_dinode_t	**dipp,
	xfs_buf_t	**bpp,
	xfs_daddr_t	bno,
	uint		imap_flags,
	uint		buf_flags)
{
	xfs_imap_t	imap;
	xfs_buf_t	*bp;
	int		error;

	if (ip->i_blkno == (xfs_daddr_t)0) {
		imap.im_blkno = bno;
		error = xfs_imap(mp, tp, ip->i_ino, &imap,
					XFS_IMAP_LOOKUP | imap_flags);
		if (error)
			return error;

		/*
		 * Fill in the fields in the inode that will be used to
		 * map the inode to its buffer from now on.
		 */
		ip->i_blkno = imap.im_blkno;
		ip->i_len = imap.im_len;
		ip->i_boffset = imap.im_boffset;
	} else {
		/*
		 * We've already mapped the inode once, so just use the
		 * mapping that we saved the first time.
		 */
		imap.im_blkno = ip->i_blkno;
		imap.im_len = ip->i_len;
		imap.im_boffset = ip->i_boffset;
	}
	ASSERT(bno == 0 || bno == imap.im_blkno);

	error = xfs_imap_to_bp(mp, tp, &imap, &bp, buf_flags, imap_flags);
	if (error)
		return error;

	if (!bp) {
		ASSERT(buf_flags & XFS_BUF_TRYLOCK);
		ASSERT(tp == NULL);
		*bpp = NULL;
		return EAGAIN;
	}

	*dipp = (xfs_dinode_t *)xfs_buf_offset(bp, imap.im_boffset);
	*bpp = bp;

STATIC int xfs_iunlink_remove(xfs_trans_t *, xfs_inode_t *);

/*
 * helper function to extract extent size hint from inode
 */
xfs_extlen_t
xfs_get_extsz_hint(
	struct xfs_inode	*ip)
{
	if ((ip->i_d.di_flags & XFS_DIFLAG_EXTSIZE) && ip->i_d.di_extsize)
		return ip->i_d.di_extsize;
	if (XFS_IS_REALTIME_INODE(ip))
		return ip->i_mount->m_sb.sb_rextsize;
	return 0;
}

/*
 * These two are wrapper routines around the xfs_ilock() routine used to
 * centralize some grungy code.  They are used in places that wish to lock the
 * inode solely for reading the extents.  The reason these places can't just
 * call xfs_ilock(ip, XFS_ILOCK_SHARED) is that the inode lock also guards to
 * bringing in of the extents from disk for a file in b-tree format.  If the
 * inode is in b-tree format, then we need to lock the inode exclusively until
 * the extents are read in.  Locking it exclusively all the time would limit
 * our parallelism unnecessarily, though.  What we do instead is check to see
 * if the extents have been read in yet, and only lock the inode exclusively
 * if they have not.
 *
 * The functions return a value which should be given to the corresponding
 * xfs_iunlock() call.
 */
uint
xfs_ilock_data_map_shared(
	struct xfs_inode	*ip)
{
	uint			lock_mode = XFS_ILOCK_SHARED;

	if (ip->i_d.di_format == XFS_DINODE_FMT_BTREE &&
	    (ip->i_df.if_flags & XFS_IFEXTENTS) == 0)
		lock_mode = XFS_ILOCK_EXCL;
	xfs_ilock(ip, lock_mode);
	return lock_mode;
}

uint
xfs_ilock_attr_map_shared(
	struct xfs_inode	*ip)
{
	uint			lock_mode = XFS_ILOCK_SHARED;

	if (ip->i_d.di_aformat == XFS_DINODE_FMT_BTREE &&
	    (ip->i_afp->if_flags & XFS_IFEXTENTS) == 0)
		lock_mode = XFS_ILOCK_EXCL;
	xfs_ilock(ip, lock_mode);
	return lock_mode;
}

/*
 * The xfs inode contains 3 multi-reader locks: the i_iolock the i_mmap_lock and
 * the i_lock.  This routine allows various combinations of the locks to be
 * obtained.
 *
 * The 3 locks should always be ordered so that the IO lock is obtained first,
 * the mmap lock second and the ilock last in order to prevent deadlock.
 *
 * Basic locking order:
 *
 * i_iolock -> i_mmap_lock -> page_lock -> i_ilock
 *
 * mmap_sem locking order:
 *
 * i_iolock -> page lock -> mmap_sem
 * mmap_sem -> i_mmap_lock -> page_lock
 *
 * The difference in mmap_sem locking order mean that we cannot hold the
 * i_mmap_lock over syscall based read(2)/write(2) based IO. These IO paths can
 * fault in pages during copy in/out (for buffered IO) or require the mmap_sem
 * in get_user_pages() to map the user pages into the kernel address space for
 * direct IO. Similarly the i_iolock cannot be taken inside a page fault because
 * page faults already hold the mmap_sem.
 *
 * Hence to serialise fully against both syscall and mmap based IO, we need to
 * take both the i_iolock and the i_mmap_lock. These locks should *only* be both
 * taken in places where we need to invalidate the page cache in a race
 * free manner (e.g. truncate, hole punch and other extent manipulation
 * functions).
 */
void
xfs_ilock(
	xfs_inode_t		*ip,
	uint			lock_flags)
{
	trace_xfs_ilock(ip, lock_flags, _RET_IP_);

	/*
	 * You can't set both SHARED and EXCL for the same lock,
	 * and only XFS_IOLOCK_SHARED, XFS_IOLOCK_EXCL, XFS_ILOCK_SHARED,
	 * and XFS_ILOCK_EXCL are valid values to set in lock_flags.
	 */
	ASSERT((lock_flags & (XFS_IOLOCK_SHARED | XFS_IOLOCK_EXCL)) !=
	       (XFS_IOLOCK_SHARED | XFS_IOLOCK_EXCL));
	ASSERT((lock_flags & (XFS_MMAPLOCK_SHARED | XFS_MMAPLOCK_EXCL)) !=
	       (XFS_MMAPLOCK_SHARED | XFS_MMAPLOCK_EXCL));
	ASSERT((lock_flags & (XFS_ILOCK_SHARED | XFS_ILOCK_EXCL)) !=
	       (XFS_ILOCK_SHARED | XFS_ILOCK_EXCL));
	ASSERT((lock_flags & ~(XFS_LOCK_MASK | XFS_LOCK_SUBCLASS_MASK)) == 0);

	if (lock_flags & XFS_IOLOCK_EXCL)
		mrupdate_nested(&ip->i_iolock, XFS_IOLOCK_DEP(lock_flags));
	else if (lock_flags & XFS_IOLOCK_SHARED)
		mraccess_nested(&ip->i_iolock, XFS_IOLOCK_DEP(lock_flags));

	if (lock_flags & XFS_MMAPLOCK_EXCL)
		mrupdate_nested(&ip->i_mmaplock, XFS_MMAPLOCK_DEP(lock_flags));
	else if (lock_flags & XFS_MMAPLOCK_SHARED)
		mraccess_nested(&ip->i_mmaplock, XFS_MMAPLOCK_DEP(lock_flags));

	if (lock_flags & XFS_ILOCK_EXCL)
		mrupdate_nested(&ip->i_lock, XFS_ILOCK_DEP(lock_flags));
	else if (lock_flags & XFS_ILOCK_SHARED)
		mraccess_nested(&ip->i_lock, XFS_ILOCK_DEP(lock_flags));
}

/*
 * This is just like xfs_ilock(), except that the caller
 * is guaranteed not to sleep.  It returns 1 if it gets
 * the requested locks and 0 otherwise.  If the IO lock is
 * obtained but the inode lock cannot be, then the IO lock
 * is dropped before returning.
 *
 * ip -- the inode being locked
 * lock_flags -- this parameter indicates the inode's locks to be
 *       to be locked.  See the comment for xfs_ilock() for a list
 *	 of valid values.
 */
int
xfs_ilock_nowait(
	xfs_inode_t		*ip,
	uint			lock_flags)
{
	trace_xfs_ilock_nowait(ip, lock_flags, _RET_IP_);

	/*
	 * You can't set both SHARED and EXCL for the same lock,
	 * and only XFS_IOLOCK_SHARED, XFS_IOLOCK_EXCL, XFS_ILOCK_SHARED,
	 * and XFS_ILOCK_EXCL are valid values to set in lock_flags.
	 */
	ASSERT((lock_flags & (XFS_IOLOCK_SHARED | XFS_IOLOCK_EXCL)) !=
	       (XFS_IOLOCK_SHARED | XFS_IOLOCK_EXCL));
	ASSERT((lock_flags & (XFS_MMAPLOCK_SHARED | XFS_MMAPLOCK_EXCL)) !=
	       (XFS_MMAPLOCK_SHARED | XFS_MMAPLOCK_EXCL));
	ASSERT((lock_flags & (XFS_ILOCK_SHARED | XFS_ILOCK_EXCL)) !=
	       (XFS_ILOCK_SHARED | XFS_ILOCK_EXCL));
	ASSERT((lock_flags & ~(XFS_LOCK_MASK | XFS_LOCK_SUBCLASS_MASK)) == 0);

	if (lock_flags & XFS_IOLOCK_EXCL) {
		if (!mrtryupdate(&ip->i_iolock))
			goto out;
	} else if (lock_flags & XFS_IOLOCK_SHARED) {
		if (!mrtryaccess(&ip->i_iolock))
			goto out;
	}

	if (lock_flags & XFS_MMAPLOCK_EXCL) {
		if (!mrtryupdate(&ip->i_mmaplock))
			goto out_undo_iolock;
	} else if (lock_flags & XFS_MMAPLOCK_SHARED) {
		if (!mrtryaccess(&ip->i_mmaplock))
			goto out_undo_iolock;
	}

	if (lock_flags & XFS_ILOCK_EXCL) {
		if (!mrtryupdate(&ip->i_lock))
			goto out_undo_mmaplock;
	} else if (lock_flags & XFS_ILOCK_SHARED) {
		if (!mrtryaccess(&ip->i_lock))
			goto out_undo_mmaplock;
	}
	return 1;

out_undo_mmaplock:
	if (lock_flags & XFS_MMAPLOCK_EXCL)
		mrunlock_excl(&ip->i_mmaplock);
	else if (lock_flags & XFS_MMAPLOCK_SHARED)
		mrunlock_shared(&ip->i_mmaplock);
out_undo_iolock:
	if (lock_flags & XFS_IOLOCK_EXCL)
		mrunlock_excl(&ip->i_iolock);
	else if (lock_flags & XFS_IOLOCK_SHARED)
		mrunlock_shared(&ip->i_iolock);
out:
	return 0;
}

/*
 * Move inode type and inode format specific information from the
 * on-disk inode to the in-core inode.  For fifos, devs, and sockets
 * this means set if_rdev to the proper value.  For files, directories,
 * and symlinks this means to bring in the in-line data or extent
 * pointers.  For a file in B-tree format, only the root is immediately
 * brought in-core.  The rest will be in-lined in if_extents when it
 * is first referenced (see xfs_iread_extents()).
 */
STATIC int
xfs_iformat(
	xfs_inode_t		*ip,
	xfs_dinode_t		*dip)
{
	xfs_attr_shortform_t	*atp;
	int			size;
	int			error;
	xfs_fsize_t             di_size;
	ip->i_df.if_ext_max =
		XFS_IFORK_DSIZE(ip) / (uint)sizeof(xfs_bmbt_rec_t);
	error = 0;

	if (unlikely(be32_to_cpu(dip->di_core.di_nextents) +
		     be16_to_cpu(dip->di_core.di_anextents) >
		     be64_to_cpu(dip->di_core.di_nblocks))) {
		xfs_fs_repair_cmn_err(CE_WARN, ip->i_mount,
			"corrupt dinode %Lu, extent total = %d, nblocks = %Lu.",
			(unsigned long long)ip->i_ino,
			(int)(be32_to_cpu(dip->di_core.di_nextents) +
			      be16_to_cpu(dip->di_core.di_anextents)),
			(unsigned long long)
				be64_to_cpu(dip->di_core.di_nblocks));
		XFS_CORRUPTION_ERROR("xfs_iformat(1)", XFS_ERRLEVEL_LOW,
				     ip->i_mount, dip);
		return XFS_ERROR(EFSCORRUPTED);
	}

	if (unlikely(dip->di_core.di_forkoff > ip->i_mount->m_sb.sb_inodesize)) {
		xfs_fs_repair_cmn_err(CE_WARN, ip->i_mount,
			"corrupt dinode %Lu, forkoff = 0x%x.",
			(unsigned long long)ip->i_ino,
			dip->di_core.di_forkoff);
		XFS_CORRUPTION_ERROR("xfs_iformat(2)", XFS_ERRLEVEL_LOW,
				     ip->i_mount, dip);
		return XFS_ERROR(EFSCORRUPTED);
	}

	switch (ip->i_d.di_mode & S_IFMT) {
	case S_IFIFO:
	case S_IFCHR:
	case S_IFBLK:
	case S_IFSOCK:
		if (unlikely(dip->di_core.di_format != XFS_DINODE_FMT_DEV)) {
			XFS_CORRUPTION_ERROR("xfs_iformat(3)", XFS_ERRLEVEL_LOW,
					      ip->i_mount, dip);
			return XFS_ERROR(EFSCORRUPTED);
		}
		ip->i_d.di_size = 0;
		ip->i_size = 0;
		ip->i_df.if_u2.if_rdev = be32_to_cpu(dip->di_u.di_dev);
		break;

	case S_IFREG:
	case S_IFLNK:
	case S_IFDIR:
		switch (dip->di_core.di_format) {
		case XFS_DINODE_FMT_LOCAL:
			/*
			 * no local regular files yet
			 */
			if (unlikely((be16_to_cpu(dip->di_core.di_mode) & S_IFMT) == S_IFREG)) {
				xfs_fs_repair_cmn_err(CE_WARN, ip->i_mount,
					"corrupt inode %Lu "
					"(local format for regular file).",
					(unsigned long long) ip->i_ino);
				XFS_CORRUPTION_ERROR("xfs_iformat(4)",
						     XFS_ERRLEVEL_LOW,
						     ip->i_mount, dip);
				return XFS_ERROR(EFSCORRUPTED);
			}

			di_size = be64_to_cpu(dip->di_core.di_size);
			if (unlikely(di_size > XFS_DFORK_DSIZE(dip, ip->i_mount))) {
				xfs_fs_repair_cmn_err(CE_WARN, ip->i_mount,
					"corrupt inode %Lu "
					"(bad size %Ld for local inode).",
					(unsigned long long) ip->i_ino,
					(long long) di_size);
				XFS_CORRUPTION_ERROR("xfs_iformat(5)",
						     XFS_ERRLEVEL_LOW,
						     ip->i_mount, dip);
				return XFS_ERROR(EFSCORRUPTED);
			}

			size = (int)di_size;
			error = xfs_iformat_local(ip, dip, XFS_DATA_FORK, size);
			break;
		case XFS_DINODE_FMT_EXTENTS:
			error = xfs_iformat_extents(ip, dip, XFS_DATA_FORK);
			break;
		case XFS_DINODE_FMT_BTREE:
			error = xfs_iformat_btree(ip, dip, XFS_DATA_FORK);
			break;
		default:
			XFS_ERROR_REPORT("xfs_iformat(6)", XFS_ERRLEVEL_LOW,
					 ip->i_mount);
			return XFS_ERROR(EFSCORRUPTED);
		}
		break;

	default:
		XFS_ERROR_REPORT("xfs_iformat(7)", XFS_ERRLEVEL_LOW, ip->i_mount);
		return XFS_ERROR(EFSCORRUPTED);
	}
	if (error) {
		return error;
	}
	if (!XFS_DFORK_Q(dip))
		return 0;
	ASSERT(ip->i_afp == NULL);
	ip->i_afp = kmem_zone_zalloc(xfs_ifork_zone, KM_SLEEP);
	ip->i_afp->if_ext_max =
		XFS_IFORK_ASIZE(ip) / (uint)sizeof(xfs_bmbt_rec_t);
	switch (dip->di_core.di_aformat) {
	case XFS_DINODE_FMT_LOCAL:
		atp = (xfs_attr_shortform_t *)XFS_DFORK_APTR(dip);
		size = be16_to_cpu(atp->hdr.totsize);
		error = xfs_iformat_local(ip, dip, XFS_ATTR_FORK, size);
		break;
	case XFS_DINODE_FMT_EXTENTS:
		error = xfs_iformat_extents(ip, dip, XFS_ATTR_FORK);
		break;
	case XFS_DINODE_FMT_BTREE:
		error = xfs_iformat_btree(ip, dip, XFS_ATTR_FORK);
		break;
	default:
		error = XFS_ERROR(EFSCORRUPTED);
		break;
	}
	if (error) {
		kmem_zone_free(xfs_ifork_zone, ip->i_afp);
		ip->i_afp = NULL;
		xfs_idestroy_fork(ip, XFS_DATA_FORK);
	}
	return error;
}

/*
 * The file is in-lined in the on-disk inode.
 * If it fits into if_inline_data, then copy
 * it there, otherwise allocate a buffer for it
 * and copy the data there.  Either way, set
 * if_data to point at the data.
 * If we allocate a buffer for the data, make
 * sure that its size is a multiple of 4 and
 * record the real size in i_real_bytes.
 */
STATIC int
xfs_iformat_local(
	xfs_inode_t	*ip,
	xfs_dinode_t	*dip,
	int		whichfork,
	int		size)
{
	xfs_ifork_t	*ifp;
	int		real_size;

	/*
	 * If the size is unreasonable, then something
	 * is wrong and we just bail out rather than crash in
	 * kmem_alloc() or memcpy() below.
	 */
	if (unlikely(size > XFS_DFORK_SIZE(dip, ip->i_mount, whichfork))) {
		xfs_fs_repair_cmn_err(CE_WARN, ip->i_mount,
			"corrupt inode %Lu "
			"(bad size %d for local fork, size = %d).",
			(unsigned long long) ip->i_ino, size,
			XFS_DFORK_SIZE(dip, ip->i_mount, whichfork));
		XFS_CORRUPTION_ERROR("xfs_iformat_local", XFS_ERRLEVEL_LOW,
				     ip->i_mount, dip);
		return XFS_ERROR(EFSCORRUPTED);
	}
	ifp = XFS_IFORK_PTR(ip, whichfork);
	real_size = 0;
	if (size == 0)
		ifp->if_u1.if_data = NULL;
	else if (size <= sizeof(ifp->if_u2.if_inline_data))
		ifp->if_u1.if_data = ifp->if_u2.if_inline_data;
	else {
		real_size = roundup(size, 4);
		ifp->if_u1.if_data = kmem_alloc(real_size, KM_SLEEP);
	}
	ifp->if_bytes = size;
	ifp->if_real_bytes = real_size;
	if (size)
		memcpy(ifp->if_u1.if_data, XFS_DFORK_PTR(dip, whichfork), size);
	ifp->if_flags &= ~XFS_IFEXTENTS;
	ifp->if_flags |= XFS_IFINLINE;
	return 0;
}

/*
 * The file consists of a set of extents all
 * of which fit into the on-disk inode.
 * If there are few enough extents to fit into
 * the if_inline_ext, then copy them there.
 * Otherwise allocate a buffer for them and copy
 * them into it.  Either way, set if_extents
 * to point at the extents.
 */
STATIC int
xfs_iformat_extents(
	xfs_inode_t	*ip,
	xfs_dinode_t	*dip,
	int		whichfork)
{
	xfs_bmbt_rec_t	*dp;
	xfs_ifork_t	*ifp;
	int		nex;
	int		size;
	int		i;

	ifp = XFS_IFORK_PTR(ip, whichfork);
	nex = XFS_DFORK_NEXTENTS(dip, whichfork);
	size = nex * (uint)sizeof(xfs_bmbt_rec_t);

	/*
	 * If the number of extents is unreasonable, then something
	 * is wrong and we just bail out rather than crash in
	 * kmem_alloc() or memcpy() below.
	 */
	if (unlikely(size < 0 || size > XFS_DFORK_SIZE(dip, ip->i_mount, whichfork))) {
		xfs_fs_repair_cmn_err(CE_WARN, ip->i_mount,
			"corrupt inode %Lu ((a)extents = %d).",
			(unsigned long long) ip->i_ino, nex);
		XFS_CORRUPTION_ERROR("xfs_iformat_extents(1)", XFS_ERRLEVEL_LOW,
				     ip->i_mount, dip);
		return XFS_ERROR(EFSCORRUPTED);
	}

	ifp->if_real_bytes = 0;
	if (nex == 0)
		ifp->if_u1.if_extents = NULL;
	else if (nex <= XFS_INLINE_EXTS)
		ifp->if_u1.if_extents = ifp->if_u2.if_inline_ext;
	else
		xfs_iext_add(ifp, 0, nex);

	ifp->if_bytes = size;
	if (size) {
		dp = (xfs_bmbt_rec_t *) XFS_DFORK_PTR(dip, whichfork);
		xfs_validate_extents(ifp, nex, XFS_EXTFMT_INODE(ip));
		for (i = 0; i < nex; i++, dp++) {
			xfs_bmbt_rec_host_t *ep = xfs_iext_get_ext(ifp, i);
			ep->l0 = get_unaligned_be64(&dp->l0);
			ep->l1 = get_unaligned_be64(&dp->l1);
		}
		XFS_BMAP_TRACE_EXLIST(ip, nex, whichfork);
		if (whichfork != XFS_DATA_FORK ||
			XFS_EXTFMT_INODE(ip) == XFS_EXTFMT_NOSTATE)
				if (unlikely(xfs_check_nostate_extents(
				    ifp, 0, nex))) {
					XFS_ERROR_REPORT("xfs_iformat_extents(2)",
							 XFS_ERRLEVEL_LOW,
							 ip->i_mount);
					return XFS_ERROR(EFSCORRUPTED);
				}
	}
	ifp->if_flags |= XFS_IFEXTENTS;
	return 0;
}

/*
 * The file has too many extents to fit into
 * the inode, so they are in B-tree format.
 * Allocate a buffer for the root of the B-tree
 * and copy the root into it.  The i_extents
 * field will remain NULL until all of the
 * extents are read in (when they are needed).
 */
STATIC int
xfs_iformat_btree(
	xfs_inode_t		*ip,
	xfs_dinode_t		*dip,
	int			whichfork)
{
	xfs_bmdr_block_t	*dfp;
	xfs_ifork_t		*ifp;
	/* REFERENCED */
	int			nrecs;
	int			size;

	ifp = XFS_IFORK_PTR(ip, whichfork);
	dfp = (xfs_bmdr_block_t *)XFS_DFORK_PTR(dip, whichfork);
	size = XFS_BMAP_BROOT_SPACE(dfp);
	nrecs = XFS_BMAP_BROOT_NUMRECS(dfp);

	/*
	 * blow out if -- fork has less extents than can fit in
	 * fork (fork shouldn't be a btree format), root btree
	 * block has more records than can fit into the fork,
	 * or the number of extents is greater than the number of
	 * blocks.
	 */
	if (unlikely(XFS_IFORK_NEXTENTS(ip, whichfork) <= ifp->if_ext_max
	    || XFS_BMDR_SPACE_CALC(nrecs) >
			XFS_DFORK_SIZE(dip, ip->i_mount, whichfork)
	    || XFS_IFORK_NEXTENTS(ip, whichfork) > ip->i_d.di_nblocks)) {
		xfs_fs_repair_cmn_err(CE_WARN, ip->i_mount,
			"corrupt inode %Lu (btree).",
			(unsigned long long) ip->i_ino);
		XFS_ERROR_REPORT("xfs_iformat_btree", XFS_ERRLEVEL_LOW,
				 ip->i_mount);
		return XFS_ERROR(EFSCORRUPTED);
	}

	ifp->if_broot_bytes = size;
	ifp->if_broot = kmem_alloc(size, KM_SLEEP);
	ASSERT(ifp->if_broot != NULL);
	/*
	 * Copy and convert from the on-disk structure
	 * to the in-memory structure.
	 */
	xfs_bmdr_to_bmbt(dfp, XFS_DFORK_SIZE(dip, ip->i_mount, whichfork),
		ifp->if_broot, size);
	ifp->if_flags &= ~XFS_IFEXTENTS;
	ifp->if_flags |= XFS_IFBROOT;

	return 0;
}

void
xfs_dinode_from_disk(
	xfs_icdinode_t		*to,
	xfs_dinode_core_t	*from)
{
	to->di_magic = be16_to_cpu(from->di_magic);
	to->di_mode = be16_to_cpu(from->di_mode);
	to->di_version = from ->di_version;
	to->di_format = from->di_format;
	to->di_onlink = be16_to_cpu(from->di_onlink);
	to->di_uid = be32_to_cpu(from->di_uid);
	to->di_gid = be32_to_cpu(from->di_gid);
	to->di_nlink = be32_to_cpu(from->di_nlink);
	to->di_projid = be16_to_cpu(from->di_projid);
	memcpy(to->di_pad, from->di_pad, sizeof(to->di_pad));
	to->di_flushiter = be16_to_cpu(from->di_flushiter);
	to->di_atime.t_sec = be32_to_cpu(from->di_atime.t_sec);
	to->di_atime.t_nsec = be32_to_cpu(from->di_atime.t_nsec);
	to->di_mtime.t_sec = be32_to_cpu(from->di_mtime.t_sec);
	to->di_mtime.t_nsec = be32_to_cpu(from->di_mtime.t_nsec);
	to->di_ctime.t_sec = be32_to_cpu(from->di_ctime.t_sec);
	to->di_ctime.t_nsec = be32_to_cpu(from->di_ctime.t_nsec);
	to->di_size = be64_to_cpu(from->di_size);
	to->di_nblocks = be64_to_cpu(from->di_nblocks);
	to->di_extsize = be32_to_cpu(from->di_extsize);
	to->di_nextents = be32_to_cpu(from->di_nextents);
	to->di_anextents = be16_to_cpu(from->di_anextents);
	to->di_forkoff = from->di_forkoff;
	to->di_aformat	= from->di_aformat;
	to->di_dmevmask	= be32_to_cpu(from->di_dmevmask);
	to->di_dmstate	= be16_to_cpu(from->di_dmstate);
	to->di_flags	= be16_to_cpu(from->di_flags);
	to->di_gen	= be32_to_cpu(from->di_gen);
}

void
xfs_dinode_to_disk(
	xfs_dinode_core_t	*to,
	xfs_icdinode_t		*from)
{
	to->di_magic = cpu_to_be16(from->di_magic);
	to->di_mode = cpu_to_be16(from->di_mode);
	to->di_version = from ->di_version;
	to->di_format = from->di_format;
	to->di_onlink = cpu_to_be16(from->di_onlink);
	to->di_uid = cpu_to_be32(from->di_uid);
	to->di_gid = cpu_to_be32(from->di_gid);
	to->di_nlink = cpu_to_be32(from->di_nlink);
	to->di_projid = cpu_to_be16(from->di_projid);
	memcpy(to->di_pad, from->di_pad, sizeof(to->di_pad));
	to->di_flushiter = cpu_to_be16(from->di_flushiter);
	to->di_atime.t_sec = cpu_to_be32(from->di_atime.t_sec);
	to->di_atime.t_nsec = cpu_to_be32(from->di_atime.t_nsec);
	to->di_mtime.t_sec = cpu_to_be32(from->di_mtime.t_sec);
	to->di_mtime.t_nsec = cpu_to_be32(from->di_mtime.t_nsec);
	to->di_ctime.t_sec = cpu_to_be32(from->di_ctime.t_sec);
	to->di_ctime.t_nsec = cpu_to_be32(from->di_ctime.t_nsec);
	to->di_size = cpu_to_be64(from->di_size);
	to->di_nblocks = cpu_to_be64(from->di_nblocks);
	to->di_extsize = cpu_to_be32(from->di_extsize);
	to->di_nextents = cpu_to_be32(from->di_nextents);
	to->di_anextents = cpu_to_be16(from->di_anextents);
	to->di_forkoff = from->di_forkoff;
	to->di_aformat = from->di_aformat;
	to->di_dmevmask = cpu_to_be32(from->di_dmevmask);
	to->di_dmstate = cpu_to_be16(from->di_dmstate);
	to->di_flags = cpu_to_be16(from->di_flags);
	to->di_gen = cpu_to_be32(from->di_gen);
 * xfs_iunlock() is used to drop the inode locks acquired with
 * xfs_ilock() and xfs_ilock_nowait().  The caller must pass
 * in the flags given to xfs_ilock() or xfs_ilock_nowait() so
 * that we know which locks to drop.
 *
 * ip -- the inode being unlocked
 * lock_flags -- this parameter indicates the inode's locks to be
 *       to be unlocked.  See the comment for xfs_ilock() for a list
 *	 of valid values for this parameter.
 *
 */
void
xfs_iunlock(
	xfs_inode_t		*ip,
	uint			lock_flags)
{
	/*
	 * You can't set both SHARED and EXCL for the same lock,
	 * and only XFS_IOLOCK_SHARED, XFS_IOLOCK_EXCL, XFS_ILOCK_SHARED,
	 * and XFS_ILOCK_EXCL are valid values to set in lock_flags.
	 */
	ASSERT((lock_flags & (XFS_IOLOCK_SHARED | XFS_IOLOCK_EXCL)) !=
	       (XFS_IOLOCK_SHARED | XFS_IOLOCK_EXCL));
	ASSERT((lock_flags & (XFS_MMAPLOCK_SHARED | XFS_MMAPLOCK_EXCL)) !=
	       (XFS_MMAPLOCK_SHARED | XFS_MMAPLOCK_EXCL));
	ASSERT((lock_flags & (XFS_ILOCK_SHARED | XFS_ILOCK_EXCL)) !=
	       (XFS_ILOCK_SHARED | XFS_ILOCK_EXCL));
	ASSERT((lock_flags & ~(XFS_LOCK_MASK | XFS_LOCK_SUBCLASS_MASK)) == 0);
	ASSERT(lock_flags != 0);

	if (lock_flags & XFS_IOLOCK_EXCL)
		mrunlock_excl(&ip->i_iolock);
	else if (lock_flags & XFS_IOLOCK_SHARED)
		mrunlock_shared(&ip->i_iolock);

	if (lock_flags & XFS_MMAPLOCK_EXCL)
		mrunlock_excl(&ip->i_mmaplock);
	else if (lock_flags & XFS_MMAPLOCK_SHARED)
		mrunlock_shared(&ip->i_mmaplock);

	if (lock_flags & XFS_ILOCK_EXCL)
		mrunlock_excl(&ip->i_lock);
	else if (lock_flags & XFS_ILOCK_SHARED)
		mrunlock_shared(&ip->i_lock);

	trace_xfs_iunlock(ip, lock_flags, _RET_IP_);
}

/*
 * give up write locks.  the i/o lock cannot be held nested
 * if it is being demoted.
 */
void
xfs_ilock_demote(
	xfs_inode_t		*ip,
	uint			lock_flags)
{
	ASSERT(lock_flags & (XFS_IOLOCK_EXCL|XFS_MMAPLOCK_EXCL|XFS_ILOCK_EXCL));
	ASSERT((lock_flags &
		~(XFS_IOLOCK_EXCL|XFS_MMAPLOCK_EXCL|XFS_ILOCK_EXCL)) == 0);

	if (lock_flags & XFS_ILOCK_EXCL)
		mrdemote(&ip->i_lock);
	if (lock_flags & XFS_MMAPLOCK_EXCL)
		mrdemote(&ip->i_mmaplock);
	if (lock_flags & XFS_IOLOCK_EXCL)
		mrdemote(&ip->i_iolock);

	trace_xfs_ilock_demote(ip, lock_flags, _RET_IP_);
}

#if defined(DEBUG) || defined(XFS_WARN)
int
xfs_isilocked(
	xfs_inode_t		*ip,
	uint			lock_flags)
{
	if (lock_flags & (XFS_ILOCK_EXCL|XFS_ILOCK_SHARED)) {
		if (!(lock_flags & XFS_ILOCK_SHARED))
			return !!ip->i_lock.mr_writer;
		return rwsem_is_locked(&ip->i_lock.mr_lock);
	}

	if (lock_flags & (XFS_MMAPLOCK_EXCL|XFS_MMAPLOCK_SHARED)) {
		if (!(lock_flags & XFS_MMAPLOCK_SHARED))
			return !!ip->i_mmaplock.mr_writer;
		return rwsem_is_locked(&ip->i_mmaplock.mr_lock);
	}

	if (lock_flags & (XFS_IOLOCK_EXCL|XFS_IOLOCK_SHARED)) {
		if (!(lock_flags & XFS_IOLOCK_SHARED))
			return !!ip->i_iolock.mr_writer;
		return rwsem_is_locked(&ip->i_iolock.mr_lock);
	}

	ASSERT(0);
	return 0;
}
#endif

#ifdef DEBUG
int xfs_locked_n;
int xfs_small_retries;
int xfs_middle_retries;
int xfs_lots_retries;
int xfs_lock_delays;
#endif

/*
 * xfs_lockdep_subclass_ok() is only used in an ASSERT, so is only called when
 * DEBUG or XFS_WARN is set. And MAX_LOCKDEP_SUBCLASSES is then only defined
 * when CONFIG_LOCKDEP is set. Hence the complex define below to avoid build
 * errors and warnings.
 */
#if (defined(DEBUG) || defined(XFS_WARN)) && defined(CONFIG_LOCKDEP)
static bool
xfs_lockdep_subclass_ok(
	int subclass)
{
	return subclass < MAX_LOCKDEP_SUBCLASSES;
}
#else
#define xfs_lockdep_subclass_ok(subclass)	(true)
#endif

/*
 * Bump the subclass so xfs_lock_inodes() acquires each lock with a different
 * value. This can be called for any type of inode lock combination, including
 * parent locking. Care must be taken to ensure we don't overrun the subclass
 * storage fields in the class mask we build.
 */
static inline int
xfs_lock_inumorder(int lock_mode, int subclass)
{
	int	class = 0;

	ASSERT(!(lock_mode & (XFS_ILOCK_PARENT | XFS_ILOCK_RTBITMAP |
			      XFS_ILOCK_RTSUM)));
	ASSERT(xfs_lockdep_subclass_ok(subclass));

	if (lock_mode & (XFS_IOLOCK_SHARED|XFS_IOLOCK_EXCL)) {
		ASSERT(subclass <= XFS_IOLOCK_MAX_SUBCLASS);
		ASSERT(xfs_lockdep_subclass_ok(subclass +
						XFS_IOLOCK_PARENT_VAL));
		class += subclass << XFS_IOLOCK_SHIFT;
		if (lock_mode & XFS_IOLOCK_PARENT)
			class += XFS_IOLOCK_PARENT_VAL << XFS_IOLOCK_SHIFT;
	}

	if (lock_mode & (XFS_MMAPLOCK_SHARED|XFS_MMAPLOCK_EXCL)) {
		ASSERT(subclass <= XFS_MMAPLOCK_MAX_SUBCLASS);
		class += subclass << XFS_MMAPLOCK_SHIFT;
	}

	if (lock_mode & (XFS_ILOCK_SHARED|XFS_ILOCK_EXCL)) {
		ASSERT(subclass <= XFS_ILOCK_MAX_SUBCLASS);
		class += subclass << XFS_ILOCK_SHIFT;
	}

	return (lock_mode & ~XFS_LOCK_SUBCLASS_MASK) | class;
}

/*
 * The following routine will lock n inodes in exclusive mode.  We assume the
 * caller calls us with the inodes in i_ino order.
 *
 * We need to detect deadlock where an inode that we lock is in the AIL and we
 * start waiting for another inode that is locked by a thread in a long running
 * transaction (such as truncate). This can result in deadlock since the long
 * running trans might need to wait for the inode we just locked in order to
 * push the tail and free space in the log.
 *
 * xfs_lock_inodes() can only be used to lock one type of lock at a time -
 * the iolock, the mmaplock or the ilock, but not more than one at a time. If we
 * lock more than one at a time, lockdep will report false positives saying we
 * have violated locking orders.
 */
void
xfs_lock_inodes(
	xfs_inode_t	**ips,
	int		inodes,
	uint		lock_mode)
{
	int		attempts = 0, i, j, try_lock;
	xfs_log_item_t	*lp;

	/*
	 * Currently supports between 2 and 5 inodes with exclusive locking.  We
	 * support an arbitrary depth of locking here, but absolute limits on
	 * inodes depend on the the type of locking and the limits placed by
	 * lockdep annotations in xfs_lock_inumorder.  These are all checked by
	 * the asserts.
	 */
	ASSERT(ips && inodes >= 2 && inodes <= 5);
	ASSERT(lock_mode & (XFS_IOLOCK_EXCL | XFS_MMAPLOCK_EXCL |
			    XFS_ILOCK_EXCL));
	ASSERT(!(lock_mode & (XFS_IOLOCK_SHARED | XFS_MMAPLOCK_SHARED |
			      XFS_ILOCK_SHARED)));
	ASSERT(!(lock_mode & XFS_IOLOCK_EXCL) ||
		inodes <= XFS_IOLOCK_MAX_SUBCLASS + 1);
	ASSERT(!(lock_mode & XFS_MMAPLOCK_EXCL) ||
		inodes <= XFS_MMAPLOCK_MAX_SUBCLASS + 1);
	ASSERT(!(lock_mode & XFS_ILOCK_EXCL) ||
		inodes <= XFS_ILOCK_MAX_SUBCLASS + 1);

	if (lock_mode & XFS_IOLOCK_EXCL) {
		ASSERT(!(lock_mode & (XFS_MMAPLOCK_EXCL | XFS_ILOCK_EXCL)));
	} else if (lock_mode & XFS_MMAPLOCK_EXCL)
		ASSERT(!(lock_mode & XFS_ILOCK_EXCL));

	try_lock = 0;
	i = 0;
again:
	for (; i < inodes; i++) {
		ASSERT(ips[i]);

		if (i && (ips[i] == ips[i - 1]))	/* Already locked */
			continue;

		/*
		 * If try_lock is not set yet, make sure all locked inodes are
		 * not in the AIL.  If any are, set try_lock to be used later.
		 */
		if (!try_lock) {
			for (j = (i - 1); j >= 0 && !try_lock; j--) {
				lp = (xfs_log_item_t *)ips[j]->i_itemp;
				if (lp && (lp->li_flags & XFS_LI_IN_AIL))
					try_lock++;
			}
		}

		/*
		 * If any of the previous locks we have locked is in the AIL,
		 * we must TRY to get the second and subsequent locks. If
		 * we can't get any, we must release all we have
		 * and try again.
		 */
		if (!try_lock) {
			xfs_ilock(ips[i], xfs_lock_inumorder(lock_mode, i));
			continue;
		}

		/* try_lock means we have an inode locked that is in the AIL. */
		ASSERT(i != 0);
		if (xfs_ilock_nowait(ips[i], xfs_lock_inumorder(lock_mode, i)))
			continue;

		/*
		 * Unlock all previous guys and try again.  xfs_iunlock will try
		 * to push the tail if the inode is in the AIL.
		 */
		attempts++;
		for (j = i - 1; j >= 0; j--) {
			/*
			 * Check to see if we've already unlocked this one.  Not
			 * the first one going back, and the inode ptr is the
			 * same.
			 */
			if (j != (i - 1) && ips[j] == ips[j + 1])
				continue;

			xfs_iunlock(ips[j], lock_mode);
		}

		if ((attempts % 5) == 0) {
			delay(1); /* Don't just spin the CPU */
#ifdef DEBUG
			xfs_lock_delays++;
#endif
		}
		i = 0;
		try_lock = 0;
		goto again;
	}

#ifdef DEBUG
	if (attempts) {
		if (attempts < 5) xfs_small_retries++;
		else if (attempts < 100) xfs_middle_retries++;
		else xfs_lots_retries++;
	} else {
		xfs_locked_n++;
	}
#endif
}

/*
 * xfs_lock_two_inodes() can only be used to lock one type of lock at a time -
 * the iolock, the mmaplock or the ilock, but not more than one at a time. If we
 * lock more than one at a time, lockdep will report false positives saying we
 * have violated locking orders.
 */
void
xfs_lock_two_inodes(
	xfs_inode_t		*ip0,
	xfs_inode_t		*ip1,
	uint			lock_mode)
{
	xfs_inode_t		*temp;
	int			attempts = 0;
	xfs_log_item_t		*lp;

	if (lock_mode & (XFS_IOLOCK_SHARED|XFS_IOLOCK_EXCL)) {
		ASSERT(!(lock_mode & (XFS_MMAPLOCK_SHARED|XFS_MMAPLOCK_EXCL)));
		ASSERT(!(lock_mode & (XFS_ILOCK_SHARED|XFS_ILOCK_EXCL)));
	} else if (lock_mode & (XFS_MMAPLOCK_SHARED|XFS_MMAPLOCK_EXCL))
		ASSERT(!(lock_mode & (XFS_ILOCK_SHARED|XFS_ILOCK_EXCL)));

	ASSERT(ip0->i_ino != ip1->i_ino);

	if (ip0->i_ino > ip1->i_ino) {
		temp = ip0;
		ip0 = ip1;
		ip1 = temp;
	}

 again:
	xfs_ilock(ip0, xfs_lock_inumorder(lock_mode, 0));

	/*
	 * If the first lock we have locked is in the AIL, we must TRY to get
	 * the second lock. If we can't get it, we must release the first one
	 * and try again.
	 */
	lp = (xfs_log_item_t *)ip0->i_itemp;
	if (lp && (lp->li_flags & XFS_LI_IN_AIL)) {
		if (!xfs_ilock_nowait(ip1, xfs_lock_inumorder(lock_mode, 1))) {
			xfs_iunlock(ip0, lock_mode);
			if ((++attempts % 5) == 0)
				delay(1); /* Don't just spin the CPU */
			goto again;
		}
	} else {
		xfs_ilock(ip1, xfs_lock_inumorder(lock_mode, 1));
	}
}


void
__xfs_iflock(
	struct xfs_inode	*ip)
{
	wait_queue_head_t *wq = bit_waitqueue(&ip->i_flags, __XFS_IFLOCK_BIT);
	DEFINE_WAIT_BIT(wait, &ip->i_flags, __XFS_IFLOCK_BIT);

	do {
		prepare_to_wait_exclusive(wq, &wait.wait, TASK_UNINTERRUPTIBLE);
		if (xfs_isiflocked(ip))
			io_schedule();
	} while (!xfs_iflock_nowait(ip));

	finish_wait(wq, &wait.wait);
}

STATIC uint
_xfs_dic2xflags(
	__uint16_t		di_flags)
{
	uint			flags = 0;

	if (di_flags & XFS_DIFLAG_ANY) {
		if (di_flags & XFS_DIFLAG_REALTIME)
			flags |= XFS_XFLAG_REALTIME;
		if (di_flags & XFS_DIFLAG_PREALLOC)
			flags |= XFS_XFLAG_PREALLOC;
		if (di_flags & XFS_DIFLAG_IMMUTABLE)
			flags |= XFS_XFLAG_IMMUTABLE;
		if (di_flags & XFS_DIFLAG_APPEND)
			flags |= XFS_XFLAG_APPEND;
		if (di_flags & XFS_DIFLAG_SYNC)
			flags |= XFS_XFLAG_SYNC;
		if (di_flags & XFS_DIFLAG_NOATIME)
			flags |= XFS_XFLAG_NOATIME;
		if (di_flags & XFS_DIFLAG_NODUMP)
			flags |= XFS_XFLAG_NODUMP;
		if (di_flags & XFS_DIFLAG_RTINHERIT)
			flags |= XFS_XFLAG_RTINHERIT;
		if (di_flags & XFS_DIFLAG_PROJINHERIT)
			flags |= XFS_XFLAG_PROJINHERIT;
		if (di_flags & XFS_DIFLAG_NOSYMLINKS)
			flags |= XFS_XFLAG_NOSYMLINKS;
		if (di_flags & XFS_DIFLAG_EXTSIZE)
			flags |= XFS_XFLAG_EXTSIZE;
		if (di_flags & XFS_DIFLAG_EXTSZINHERIT)
			flags |= XFS_XFLAG_EXTSZINHERIT;
		if (di_flags & XFS_DIFLAG_NODEFRAG)
			flags |= XFS_XFLAG_NODEFRAG;
		if (di_flags & XFS_DIFLAG_FILESTREAM)
			flags |= XFS_XFLAG_FILESTREAM;
	}

	return flags;
}

uint
xfs_ip2xflags(
	xfs_inode_t		*ip)
{
	xfs_icdinode_t		*dic = &ip->i_d;

	return _xfs_dic2xflags(dic->di_flags) |
				(XFS_IFORK_Q(ip) ? XFS_XFLAG_HASATTR : 0);
}

uint
xfs_dic2xflags(
	xfs_dinode_t		*dip)
{
	xfs_dinode_core_t	*dic = &dip->di_core;

	return _xfs_dic2xflags(be16_to_cpu(dic->di_flags)) |
	return _xfs_dic2xflags(be16_to_cpu(dip->di_flags)) |
				(XFS_DFORK_Q(dip) ? XFS_XFLAG_HASATTR : 0);
}

/*
 * Given a mount structure and an inode number, return a pointer
 * to a newly allocated in-core inode corresponding to the given
 * inode number.
 *
 * Initialize the inode's attributes and extent pointers if it
 * already has them (it will not if the inode has no links).
 */
int
xfs_iread(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_ino_t	ino,
	xfs_inode_t	**ipp,
	xfs_daddr_t	bno,
	uint		imap_flags)
{
	xfs_buf_t	*bp;
	xfs_dinode_t	*dip;
	xfs_inode_t	*ip;
	int		error;

	ASSERT(xfs_inode_zone != NULL);

	ip = kmem_zone_zalloc(xfs_inode_zone, KM_SLEEP);
	ip->i_ino = ino;
	ip->i_mount = mp;
	atomic_set(&ip->i_iocount, 0);
	spin_lock_init(&ip->i_flags_lock);

	/*
	 * Get pointer's to the on-disk inode and the buffer containing it.
	 * If the inode number refers to a block outside the file system
	 * then xfs_itobp() will return NULL.  In this case we should
	 * return NULL as well.  Set i_blkno to 0 so that xfs_itobp() will
	 * know that this is a new incore inode.
	 */
	error = xfs_itobp(mp, tp, ip, &dip, &bp, bno, imap_flags, XFS_BUF_LOCK);
	if (error) {
		kmem_zone_free(xfs_inode_zone, ip);
		return error;
	}

	/*
	 * Initialize inode's trace buffers.
	 * Do this before xfs_iformat in case it adds entries.
	 */
#ifdef	XFS_INODE_TRACE
	ip->i_trace = ktrace_alloc(INODE_TRACE_SIZE, KM_NOFS);
#endif
#ifdef XFS_BMAP_TRACE
	ip->i_xtrace = ktrace_alloc(XFS_BMAP_KTRACE_SIZE, KM_NOFS);
#endif
#ifdef XFS_BMBT_TRACE
	ip->i_btrace = ktrace_alloc(XFS_BMBT_KTRACE_SIZE, KM_NOFS);
#endif
#ifdef XFS_RW_TRACE
	ip->i_rwtrace = ktrace_alloc(XFS_RW_KTRACE_SIZE, KM_NOFS);
#endif
#ifdef XFS_ILOCK_TRACE
	ip->i_lock_trace = ktrace_alloc(XFS_ILOCK_KTRACE_SIZE, KM_NOFS);
#endif
#ifdef XFS_DIR2_TRACE
	ip->i_dir_trace = ktrace_alloc(XFS_DIR2_KTRACE_SIZE, KM_NOFS);
#endif

	/*
	 * If we got something that isn't an inode it means someone
	 * (nfs or dmi) has a stale handle.
	 */
	if (be16_to_cpu(dip->di_core.di_magic) != XFS_DINODE_MAGIC) {
		kmem_zone_free(xfs_inode_zone, ip);
		xfs_trans_brelse(tp, bp);
#ifdef DEBUG
		xfs_fs_cmn_err(CE_ALERT, mp, "xfs_iread: "
				"dip->di_core.di_magic (0x%x) != "
				"XFS_DINODE_MAGIC (0x%x)",
				be16_to_cpu(dip->di_core.di_magic),
				XFS_DINODE_MAGIC);
#endif /* DEBUG */
		return XFS_ERROR(EINVAL);
	}

	/*
	 * If the on-disk inode is already linked to a directory
	 * entry, copy all of the inode into the in-core inode.
	 * xfs_iformat() handles copying in the inode format
	 * specific information.
	 * Otherwise, just get the truly permanent information.
	 */
	if (dip->di_core.di_mode) {
		xfs_dinode_from_disk(&ip->i_d, &dip->di_core);
		error = xfs_iformat(ip, dip);
		if (error)  {
			kmem_zone_free(xfs_inode_zone, ip);
			xfs_trans_brelse(tp, bp);
#ifdef DEBUG
			xfs_fs_cmn_err(CE_ALERT, mp, "xfs_iread: "
					"xfs_iformat() returned error %d",
					error);
#endif /* DEBUG */
			return error;
		}
	} else {
		ip->i_d.di_magic = be16_to_cpu(dip->di_core.di_magic);
		ip->i_d.di_version = dip->di_core.di_version;
		ip->i_d.di_gen = be32_to_cpu(dip->di_core.di_gen);
		ip->i_d.di_flushiter = be16_to_cpu(dip->di_core.di_flushiter);
		/*
		 * Make sure to pull in the mode here as well in
		 * case the inode is released without being used.
		 * This ensures that xfs_inactive() will see that
		 * the inode is already free and not try to mess
		 * with the uninitialized part of it.
		 */
		ip->i_d.di_mode = 0;
		/*
		 * Initialize the per-fork minima and maxima for a new
		 * inode here.  xfs_iformat will do it for old inodes.
		 */
		ip->i_df.if_ext_max =
			XFS_IFORK_DSIZE(ip) / (uint)sizeof(xfs_bmbt_rec_t);
	}

	INIT_LIST_HEAD(&ip->i_reclaim);

	/*
	 * The inode format changed when we moved the link count and
	 * made it 32 bits long.  If this is an old format inode,
	 * convert it in memory to look like a new one.  If it gets
	 * flushed to disk we will convert back before flushing or
	 * logging it.  We zero out the new projid field and the old link
	 * count field.  We'll handle clearing the pad field (the remains
	 * of the old uuid field) when we actually convert the inode to
	 * the new format. We don't change the version number so that we
	 * can distinguish this from a real new format inode.
	 */
	if (ip->i_d.di_version == XFS_DINODE_VERSION_1) {
		ip->i_d.di_nlink = ip->i_d.di_onlink;
		ip->i_d.di_onlink = 0;
		ip->i_d.di_projid = 0;
	}

	ip->i_delayed_blks = 0;
	ip->i_size = ip->i_d.di_size;

	/*
	 * Mark the buffer containing the inode as something to keep
	 * around for a while.  This helps to keep recently accessed
	 * meta-data in-core longer.
	 */
	 XFS_BUF_SET_REF(bp, XFS_INO_REF);

	/*
	 * Use xfs_trans_brelse() to release the buffer containing the
	 * on-disk inode, because it was acquired with xfs_trans_read_buf()
	 * in xfs_itobp() above.  If tp is NULL, this is just a normal
	 * brelse().  If we're within a transaction, then xfs_trans_brelse()
	 * will only release the buffer if it is not dirty within the
	 * transaction.  It will be OK to release the buffer in this case,
	 * because inodes on disk are never destroyed and we will be
	 * locking the new in-core inode before putting it in the hash
	 * table where other processes can find it.  Thus we don't have
	 * to worry about the inode being changed just because we released
	 * the buffer.
	 */
	xfs_trans_brelse(tp, bp);
	*ipp = ip;
	return 0;
}

/*
 * Read in extents from a btree-format inode.
 * Allocate and fill in if_extents.  Real work is done in xfs_bmap.c.
 */
int
xfs_iread_extents(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip,
	int		whichfork)
{
	int		error;
	xfs_ifork_t	*ifp;
	xfs_extnum_t	nextents;
	size_t		size;

	if (unlikely(XFS_IFORK_FORMAT(ip, whichfork) != XFS_DINODE_FMT_BTREE)) {
		XFS_ERROR_REPORT("xfs_iread_extents", XFS_ERRLEVEL_LOW,
				 ip->i_mount);
		return XFS_ERROR(EFSCORRUPTED);
	}
	nextents = XFS_IFORK_NEXTENTS(ip, whichfork);
	size = nextents * sizeof(xfs_bmbt_rec_t);
	ifp = XFS_IFORK_PTR(ip, whichfork);

	/*
	 * We know that the size is valid (it's checked in iformat_btree)
	 */
	ifp->if_lastex = NULLEXTNUM;
	ifp->if_bytes = ifp->if_real_bytes = 0;
	ifp->if_flags |= XFS_IFEXTENTS;
	xfs_iext_add(ifp, 0, nextents);
	error = xfs_bmap_read_extents(tp, ip, whichfork);
	if (error) {
		xfs_iext_destroy(ifp);
		ifp->if_flags &= ~XFS_IFEXTENTS;
		return error;
	}
	xfs_validate_extents(ifp, nextents, XFS_EXTFMT_INODE(ip));
	return 0;
 * Lookups up an inode from "name". If ci_name is not NULL, then a CI match
 * is allowed, otherwise it has to be an exact match. If a CI match is found,
 * ci_name->name will point to a the actual name (caller must free) or
 * will be set to NULL if an exact match is found.
 */
int
xfs_lookup(
	xfs_inode_t		*dp,
	struct xfs_name		*name,
	xfs_inode_t		**ipp,
	struct xfs_name		*ci_name)
{
	xfs_ino_t		inum;
	int			error;

	trace_xfs_lookup(dp, name);

	if (XFS_FORCED_SHUTDOWN(dp->i_mount))
		return -EIO;

	xfs_ilock(dp, XFS_IOLOCK_SHARED);
	error = xfs_dir_lookup(NULL, dp, name, &inum, ci_name);
	if (error)
		goto out_unlock;

	error = xfs_iget(dp->i_mount, NULL, inum, 0, 0, ipp);
	if (error)
		goto out_free_name;

	xfs_iunlock(dp, XFS_IOLOCK_SHARED);
	return 0;

out_free_name:
	if (ci_name)
		kmem_free(ci_name->name);
out_unlock:
	xfs_iunlock(dp, XFS_IOLOCK_SHARED);
	*ipp = NULL;
	return error;
}

/*
 * Allocate an inode on disk and return a copy of its in-core version.
 * The in-core inode is locked exclusively.  Set mode, nlink, and rdev
 * appropriately within the inode.  The uid and gid for the inode are
 * set according to the contents of the given cred structure.
 *
 * Use xfs_dialloc() to allocate the on-disk inode. If xfs_dialloc()
 * has a free inode available, call xfs_iget()
 * to obtain the in-core version of the allocated inode.  Finally,
 * fill in the inode and log its initial contents.  In this case,
 * ialloc_context would be set to NULL and call_again set to false.
 *
 * If xfs_dialloc() does not have an available inode,
 * it will replenish its supply by doing an allocation. Since we can
 * only do one allocation within a transaction without deadlocks, we
 * must commit the current transaction before returning the inode itself.
 * In this case, therefore, we will set call_again to true and return.
 * has a free inode available, call xfs_iget() to obtain the in-core
 * version of the allocated inode.  Finally, fill in the inode and
 * log its initial contents.  In this case, ialloc_context would be
 * set to NULL.
 *
 * If xfs_dialloc() does not have an available inode, it will replenish
 * its supply by doing an allocation. Since we can only do one
 * allocation within a transaction without deadlocks, we must commit
 * the current transaction before returning the inode itself.
 * In this case, therefore, we will set ialloc_context and return.
 * The caller should then commit the current transaction, start a new
 * transaction, and call xfs_ialloc() again to actually get the inode.
 *
 * To ensure that some other process does not grab the inode that
 * was allocated during the first call to xfs_ialloc(), this routine
 * also returns the [locked] bp pointing to the head of the freelist
 * as ialloc_context.  The caller should hold this buffer across
 * the commit and pass it back into this routine on the second call.
 *
 * If we are allocating quota inodes, we do not have a parent inode
 * to attach to or associate with (i.e. pip == NULL) because they
 * are not linked into the directory structure - they are attached
 * directly to the superblock - and so have no parent.
 */
int
xfs_ialloc(
	xfs_trans_t	*tp,
	xfs_inode_t	*pip,
	mode_t		mode,
	xfs_nlink_t	nlink,
	xfs_dev_t	rdev,
	cred_t		*cr,
	xfs_prid_t	prid,
	int		okalloc,
	xfs_buf_t	**ialloc_context,
	boolean_t	*call_again,
	xfs_inode_t	**ipp)
{
	umode_t		mode,
	xfs_nlink_t	nlink,
	xfs_dev_t	rdev,
	prid_t		prid,
	int		okalloc,
	xfs_buf_t	**ialloc_context,
	xfs_inode_t	**ipp)
{
	struct xfs_mount *mp = tp->t_mountp;
	xfs_ino_t	ino;
	xfs_inode_t	*ip;
	uint		flags;
	int		error;
	timespec_t	tv;
	struct timespec	tv;

	/*
	 * Call the space management code to pick
	 * the on-disk inode to be allocated.
	 */
	error = xfs_dialloc(tp, pip ? pip->i_ino : 0, mode, okalloc,
			    ialloc_context, call_again, &ino);
	if (error != 0) {
		return error;
	}
	if (*call_again || ino == NULLFSINO) {
			    ialloc_context, &ino);
	if (error)
		return error;
	if (*ialloc_context || ino == NULLFSINO) {
		*ipp = NULL;
		return 0;
	}
	ASSERT(*ialloc_context == NULL);

	/*
	 * Get the in-core inode with the lock held exclusively.
	 * This is because we're setting fields here we need
	 * to prevent others from looking at until we're done.
	 */
	error = xfs_trans_iget(tp->t_mountp, tp, ino,
				XFS_IGET_CREATE, XFS_ILOCK_EXCL, &ip);
	if (error != 0) {
		return error;
	}
	ASSERT(ip != NULL);

	ip->i_d.di_mode = (__uint16_t)mode;
	ip->i_d.di_onlink = 0;
	ip->i_d.di_nlink = nlink;
	ASSERT(ip->i_d.di_nlink == nlink);
	ip->i_d.di_uid = current_fsuid();
	ip->i_d.di_gid = current_fsgid();
	ip->i_d.di_projid = prid;
	memset(&(ip->i_d.di_pad[0]), 0, sizeof(ip->i_d.di_pad));

	/*
	 * If the superblock version is up to where we support new format
	 * inodes and this is currently an old format inode, then change
	 * the inode version number now.  This way we only do the conversion
	 * here rather than here and in the flush/logging code.
	 */
	if (xfs_sb_version_hasnlink(&tp->t_mountp->m_sb) &&
	    ip->i_d.di_version == XFS_DINODE_VERSION_1) {
		ip->i_d.di_version = XFS_DINODE_VERSION_2;
		/*
		 * We've already zeroed the old link count, the projid field,
		 * and the pad field.
		 */
	}

	/*
	 * Project ids won't be stored on disk if we are using a version 1 inode.
	 */
	if ((prid != 0) && (ip->i_d.di_version == XFS_DINODE_VERSION_1))
		xfs_bump_ino_vers2(tp, ip);

	if (pip && XFS_INHERIT_GID(pip)) {
		ip->i_d.di_gid = pip->i_d.di_gid;
		if ((pip->i_d.di_mode & S_ISGID) && (mode & S_IFMT) == S_IFDIR) {
	error = xfs_iget(mp, tp, ino, XFS_IGET_CREATE,
			 XFS_ILOCK_EXCL, &ip);
	if (error)
		return error;
	ASSERT(ip != NULL);

	/*
	 * We always convert v1 inodes to v2 now - we only support filesystems
	 * with >= v2 inode capability, so there is no reason for ever leaving
	 * an inode in v1 format.
	 */
	if (ip->i_d.di_version == 1)
		ip->i_d.di_version = 2;

	ip->i_d.di_mode = mode;
	ip->i_d.di_onlink = 0;
	ip->i_d.di_nlink = nlink;
	ASSERT(ip->i_d.di_nlink == nlink);
	ip->i_d.di_uid = xfs_kuid_to_uid(current_fsuid());
	ip->i_d.di_gid = xfs_kgid_to_gid(current_fsgid());
	xfs_set_projid(ip, prid);
	memset(&(ip->i_d.di_pad[0]), 0, sizeof(ip->i_d.di_pad));

	if (pip && XFS_INHERIT_GID(pip)) {
		ip->i_d.di_gid = pip->i_d.di_gid;
		if ((pip->i_d.di_mode & S_ISGID) && S_ISDIR(mode)) {
			ip->i_d.di_mode |= S_ISGID;
		}
	}

	/*
	 * If the group ID of the new file does not match the effective group
	 * ID or one of the supplementary group IDs, the S_ISGID bit is cleared
	 * (and only if the irix_sgid_inherit compatibility variable is set).
	 */
	if ((irix_sgid_inherit) &&
	    (ip->i_d.di_mode & S_ISGID) &&
	    (!in_group_p((gid_t)ip->i_d.di_gid))) {
	    (!in_group_p(xfs_gid_to_kgid(ip->i_d.di_gid)))) {
		ip->i_d.di_mode &= ~S_ISGID;
	}

	ip->i_d.di_size = 0;
	ip->i_size = 0;
	ip->i_d.di_nextents = 0;
	ASSERT(ip->i_d.di_nblocks == 0);

	nanotime(&tv);
	ip->i_d.di_nextents = 0;
	ASSERT(ip->i_d.di_nblocks == 0);

	tv = current_fs_time(mp->m_super);
	ip->i_d.di_mtime.t_sec = (__int32_t)tv.tv_sec;
	ip->i_d.di_mtime.t_nsec = (__int32_t)tv.tv_nsec;
	ip->i_d.di_atime = ip->i_d.di_mtime;
	ip->i_d.di_ctime = ip->i_d.di_mtime;

	/*
	 * di_gen will have been taken care of in xfs_iread.
	 */
	ip->i_d.di_extsize = 0;
	ip->i_d.di_dmevmask = 0;
	ip->i_d.di_dmstate = 0;
	ip->i_d.di_flags = 0;

	if (ip->i_d.di_version == 3) {
		ASSERT(ip->i_d.di_ino == ino);
		ASSERT(uuid_equal(&ip->i_d.di_uuid, &mp->m_sb.sb_meta_uuid));
		ip->i_d.di_crc = 0;
		ip->i_d.di_changecount = 1;
		ip->i_d.di_lsn = 0;
		ip->i_d.di_flags2 = 0;
		memset(&(ip->i_d.di_pad2[0]), 0, sizeof(ip->i_d.di_pad2));
		ip->i_d.di_crtime = ip->i_d.di_mtime;
	}


	flags = XFS_ILOG_CORE;
	switch (mode & S_IFMT) {
	case S_IFIFO:
	case S_IFCHR:
	case S_IFBLK:
	case S_IFSOCK:
		ip->i_d.di_format = XFS_DINODE_FMT_DEV;
		ip->i_df.if_u2.if_rdev = rdev;
		ip->i_df.if_flags = 0;
		flags |= XFS_ILOG_DEV;
		break;
	case S_IFREG:
		if (pip && xfs_inode_is_filestream(pip)) {
			error = xfs_filestream_associate(pip, ip);
			if (error < 0)
				return -error;
			if (!error)
				xfs_iflags_set(ip, XFS_IFILESTREAM);
		}
		/* fall through */
	case S_IFDIR:
		if (pip && (pip->i_d.di_flags & XFS_DIFLAG_ANY)) {
			uint	di_flags = 0;

			if ((mode & S_IFMT) == S_IFDIR) {
			if (S_ISDIR(mode)) {
				if (pip->i_d.di_flags & XFS_DIFLAG_RTINHERIT)
					di_flags |= XFS_DIFLAG_RTINHERIT;
				if (pip->i_d.di_flags & XFS_DIFLAG_EXTSZINHERIT) {
					di_flags |= XFS_DIFLAG_EXTSZINHERIT;
					ip->i_d.di_extsize = pip->i_d.di_extsize;
				}
			} else if ((mode & S_IFMT) == S_IFREG) {
				if (pip->i_d.di_flags & XFS_DIFLAG_PROJINHERIT)
					di_flags |= XFS_DIFLAG_PROJINHERIT;
			} else if (S_ISREG(mode)) {
				if (pip->i_d.di_flags & XFS_DIFLAG_RTINHERIT)
					di_flags |= XFS_DIFLAG_REALTIME;
				if (pip->i_d.di_flags & XFS_DIFLAG_EXTSZINHERIT) {
					di_flags |= XFS_DIFLAG_EXTSIZE;
					ip->i_d.di_extsize = pip->i_d.di_extsize;
				}
			}
			if ((pip->i_d.di_flags & XFS_DIFLAG_NOATIME) &&
			    xfs_inherit_noatime)
				di_flags |= XFS_DIFLAG_NOATIME;
			if ((pip->i_d.di_flags & XFS_DIFLAG_NODUMP) &&
			    xfs_inherit_nodump)
				di_flags |= XFS_DIFLAG_NODUMP;
			if ((pip->i_d.di_flags & XFS_DIFLAG_SYNC) &&
			    xfs_inherit_sync)
				di_flags |= XFS_DIFLAG_SYNC;
			if ((pip->i_d.di_flags & XFS_DIFLAG_NOSYMLINKS) &&
			    xfs_inherit_nosymlinks)
				di_flags |= XFS_DIFLAG_NOSYMLINKS;
			if (pip->i_d.di_flags & XFS_DIFLAG_PROJINHERIT)
				di_flags |= XFS_DIFLAG_PROJINHERIT;
			if ((pip->i_d.di_flags & XFS_DIFLAG_NODEFRAG) &&
			    xfs_inherit_nodefrag)
				di_flags |= XFS_DIFLAG_NODEFRAG;
			if (pip->i_d.di_flags & XFS_DIFLAG_FILESTREAM)
				di_flags |= XFS_DIFLAG_FILESTREAM;
			ip->i_d.di_flags |= di_flags;
		}
		/* FALLTHROUGH */
	case S_IFLNK:
		ip->i_d.di_format = XFS_DINODE_FMT_EXTENTS;
		ip->i_df.if_flags = XFS_IFEXTENTS;
		ip->i_df.if_bytes = ip->i_df.if_real_bytes = 0;
		ip->i_df.if_u1.if_extents = NULL;
		break;
	default:
		ASSERT(0);
	}
	/*
	 * Attribute fork settings for new inode.
	 */
	ip->i_d.di_aformat = XFS_DINODE_FMT_EXTENTS;
	ip->i_d.di_anextents = 0;

	/*
	 * Log the new values stuffed into the inode.
	 */
	xfs_trans_log_inode(tp, ip, flags);

	/* now that we have an i_mode we can setup inode ops and unlock */
	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
	xfs_trans_log_inode(tp, ip, flags);

	/* now that we have an i_mode we can setup the inode structure */
	xfs_setup_inode(ip);

	*ipp = ip;
	return 0;
}

/*
 * Check to make sure that there are no blocks allocated to the
 * file beyond the size of the file.  We don't check this for
 * files with fixed size extents or real time extents, but we
 * at least do it for regular files.
 */
#ifdef DEBUG
void
xfs_isize_check(
	xfs_mount_t	*mp,
	xfs_inode_t	*ip,
	xfs_fsize_t	isize)
{
	xfs_fileoff_t	map_first;
	int		nimaps;
	xfs_bmbt_irec_t	imaps[2];

	if ((ip->i_d.di_mode & S_IFMT) != S_IFREG)
		return;

	if (XFS_IS_REALTIME_INODE(ip))
		return;

	if (ip->i_d.di_flags & XFS_DIFLAG_EXTSIZE)
		return;

	nimaps = 2;
	map_first = XFS_B_TO_FSB(mp, (xfs_ufsize_t)isize);
	/*
	 * The filesystem could be shutting down, so bmapi may return
	 * an error.
	 */
	if (xfs_bmapi(NULL, ip, map_first,
			 (XFS_B_TO_FSB(mp,
				       (xfs_ufsize_t)XFS_MAXIOFFSET(mp)) -
			  map_first),
			 XFS_BMAPI_ENTIRE, NULL, 0, imaps, &nimaps,
			 NULL, NULL))
	    return;
	ASSERT(nimaps == 1);
	ASSERT(imaps[0].br_startblock == HOLESTARTBLOCK);
}
#endif	/* DEBUG */

/*
 * Calculate the last possible buffered byte in a file.  This must
 * include data that was buffered beyond the EOF by the write code.
 * This also needs to deal with overflowing the xfs_fsize_t type
 * which can happen for sizes near the limit.
 *
 * We also need to take into account any blocks beyond the EOF.  It
 * may be the case that they were buffered by a write which failed.
 * In that case the pages will still be in memory, but the inode size
 * will never have been updated.
 */
xfs_fsize_t
xfs_file_last_byte(
	xfs_inode_t	*ip)
{
	xfs_mount_t	*mp;
	xfs_fsize_t	last_byte;
	xfs_fileoff_t	last_block;
	xfs_fileoff_t	size_last_block;
	int		error;

	ASSERT(xfs_isilocked(ip, XFS_IOLOCK_EXCL|XFS_IOLOCK_SHARED));

	mp = ip->i_mount;
	/*
	 * Only check for blocks beyond the EOF if the extents have
	 * been read in.  This eliminates the need for the inode lock,
	 * and it also saves us from looking when it really isn't
	 * necessary.
	 */
	if (ip->i_df.if_flags & XFS_IFEXTENTS) {
		error = xfs_bmap_last_offset(NULL, ip, &last_block,
			XFS_DATA_FORK);
		if (error) {
			last_block = 0;
		}
	} else {
		last_block = 0;
	}
	size_last_block = XFS_B_TO_FSB(mp, (xfs_ufsize_t)ip->i_size);
	last_block = XFS_FILEOFF_MAX(last_block, size_last_block);

	last_byte = XFS_FSB_TO_B(mp, last_block);
	if (last_byte < 0) {
		return XFS_MAXIOFFSET(mp);
	}
	last_byte += (1 << mp->m_writeio_log);
	if (last_byte < 0) {
		return XFS_MAXIOFFSET(mp);
	}
	return last_byte;
}

#if defined(XFS_RW_TRACE)
STATIC void
xfs_itrunc_trace(
	int		tag,
	xfs_inode_t	*ip,
	int		flag,
	xfs_fsize_t	new_size,
	xfs_off_t	toss_start,
	xfs_off_t	toss_finish)
{
	if (ip->i_rwtrace == NULL) {
		return;
	}

	ktrace_enter(ip->i_rwtrace,
		     (void*)((long)tag),
		     (void*)ip,
		     (void*)(unsigned long)((ip->i_d.di_size >> 32) & 0xffffffff),
		     (void*)(unsigned long)(ip->i_d.di_size & 0xffffffff),
		     (void*)((long)flag),
		     (void*)(unsigned long)((new_size >> 32) & 0xffffffff),
		     (void*)(unsigned long)(new_size & 0xffffffff),
		     (void*)(unsigned long)((toss_start >> 32) & 0xffffffff),
		     (void*)(unsigned long)(toss_start & 0xffffffff),
		     (void*)(unsigned long)((toss_finish >> 32) & 0xffffffff),
		     (void*)(unsigned long)(toss_finish & 0xffffffff),
		     (void*)(unsigned long)current_cpu(),
		     (void*)(unsigned long)current_pid(),
		     (void*)NULL,
		     (void*)NULL,
		     (void*)NULL);
}
#else
#define	xfs_itrunc_trace(tag, ip, flag, new_size, toss_start, toss_finish)
#endif

/*
 * Start the truncation of the file to new_size.  The new size
 * must be smaller than the current size.  This routine will
 * clear the buffer and page caches of file data in the removed
 * range, and xfs_itruncate_finish() will remove the underlying
 * disk blocks.
 *
 * The inode must have its I/O lock locked EXCLUSIVELY, and it
 * must NOT have the inode lock held at all.  This is because we're
 * calling into the buffer/page cache code and we can't hold the
 * inode lock when we do so.
 *
 * We need to wait for any direct I/Os in flight to complete before we
 * proceed with the truncate. This is needed to prevent the extents
 * being read or written by the direct I/Os from being removed while the
 * I/O is in flight as there is no other method of synchronising
 * direct I/O with the truncate operation.  Also, because we hold
 * the IOLOCK in exclusive mode, we prevent new direct I/Os from being
 * started until the truncate completes and drops the lock. Essentially,
 * the vn_iowait() call forms an I/O barrier that provides strict ordering
 * between direct I/Os and the truncate operation.
 *
 * The flags parameter can have either the value XFS_ITRUNC_DEFINITE
 * or XFS_ITRUNC_MAYBE.  The XFS_ITRUNC_MAYBE value should be used
 * in the case that the caller is locking things out of order and
 * may not be able to call xfs_itruncate_finish() with the inode lock
 * held without dropping the I/O lock.  If the caller must drop the
 * I/O lock before calling xfs_itruncate_finish(), then xfs_itruncate_start()
 * must be called again with all the same restrictions as the initial
 * call.
 */
int
xfs_itruncate_start(
	xfs_inode_t	*ip,
	uint		flags,
	xfs_fsize_t	new_size)
{
	xfs_fsize_t	last_byte;
	xfs_off_t	toss_start;
	xfs_mount_t	*mp;
	int		error = 0;

	ASSERT(xfs_isilocked(ip, XFS_IOLOCK_EXCL));
	ASSERT((new_size == 0) || (new_size <= ip->i_size));
	ASSERT((flags == XFS_ITRUNC_DEFINITE) ||
	       (flags == XFS_ITRUNC_MAYBE));

	mp = ip->i_mount;

	/* wait for the completion of any pending DIOs */
	if (new_size < ip->i_size)
		vn_iowait(ip);

	/*
	 * Call toss_pages or flushinval_pages to get rid of pages
	 * overlapping the region being removed.  We have to use
	 * the less efficient flushinval_pages in the case that the
	 * caller may not be able to finish the truncate without
	 * dropping the inode's I/O lock.  Make sure
	 * to catch any pages brought in by buffers overlapping
	 * the EOF by searching out beyond the isize by our
	 * block size. We round new_size up to a block boundary
	 * so that we don't toss things on the same block as
	 * new_size but before it.
	 *
	 * Before calling toss_page or flushinval_pages, make sure to
	 * call remapf() over the same region if the file is mapped.
	 * This frees up mapped file references to the pages in the
	 * given range and for the flushinval_pages case it ensures
	 * that we get the latest mapped changes flushed out.
	 */
	toss_start = XFS_B_TO_FSB(mp, (xfs_ufsize_t)new_size);
	toss_start = XFS_FSB_TO_B(mp, toss_start);
	if (toss_start < 0) {
		/*
		 * The place to start tossing is beyond our maximum
		 * file size, so there is no way that the data extended
		 * out there.
		 */
		return 0;
	}
	last_byte = xfs_file_last_byte(ip);
	xfs_itrunc_trace(XFS_ITRUNC_START, ip, flags, new_size, toss_start,
			 last_byte);
	if (last_byte > toss_start) {
		if (flags & XFS_ITRUNC_DEFINITE) {
			xfs_tosspages(ip, toss_start,
					-1, FI_REMAPF_LOCKED);
		} else {
			error = xfs_flushinval_pages(ip, toss_start,
					-1, FI_REMAPF_LOCKED);
		}
	}

#ifdef DEBUG
	if (new_size == 0) {
		ASSERT(VN_CACHED(VFS_I(ip)) == 0);
	}
#endif
 * Allocates a new inode from disk and return a pointer to the
 * incore copy. This routine will internally commit the current
 * transaction and allocate a new one if the Space Manager needed
 * to do an allocation to replenish the inode free-list.
 *
 * This routine is designed to be called from xfs_create and
 * xfs_create_dir.
 *
 */
int
xfs_dir_ialloc(
	xfs_trans_t	**tpp,		/* input: current transaction;
					   output: may be a new transaction. */
	xfs_inode_t	*dp,		/* directory within whose allocate
					   the inode. */
	umode_t		mode,
	xfs_nlink_t	nlink,
	xfs_dev_t	rdev,
	prid_t		prid,		/* project id */
	int		okalloc,	/* ok to allocate new space */
	xfs_inode_t	**ipp,		/* pointer to inode; it will be
					   locked. */
	int		*committed)

{
	xfs_trans_t	*tp;
	xfs_inode_t	*ip;
	xfs_buf_t	*ialloc_context = NULL;
	int		code;
	void		*dqinfo;
	uint		tflags;

	tp = *tpp;
	ASSERT(tp->t_flags & XFS_TRANS_PERM_LOG_RES);

	/*
	 * xfs_ialloc will return a pointer to an incore inode if
	 * the Space Manager has an available inode on the free
	 * list. Otherwise, it will do an allocation and replenish
	 * the freelist.  Since we can only do one allocation per
	 * transaction without deadlocks, we will need to commit the
	 * current transaction and start a new one.  We will then
	 * need to call xfs_ialloc again to get the inode.
	 *
	 * If xfs_ialloc did an allocation to replenish the freelist,
	 * it returns the bp containing the head of the freelist as
	 * ialloc_context. We will hold a lock on it across the
	 * transaction commit so that no other process can steal
	 * the inode(s) that we've just allocated.
	 */
	code = xfs_ialloc(tp, dp, mode, nlink, rdev, prid, okalloc,
			  &ialloc_context, &ip);

	/*
	 * Return an error if we were unable to allocate a new inode.
	 * This should only happen if we run out of space on disk or
	 * encounter a disk error.
	 */
	if (code) {
		*ipp = NULL;
		return code;
	}
	if (!ialloc_context && !ip) {
		*ipp = NULL;
		return -ENOSPC;
	}

	/*
	 * If the AGI buffer is non-NULL, then we were unable to get an
	 * inode in one operation.  We need to commit the current
	 * transaction and call xfs_ialloc() again.  It is guaranteed
	 * to succeed the second time.
	 */
	if (ialloc_context) {
		/*
		 * Normally, xfs_trans_commit releases all the locks.
		 * We call bhold to hang on to the ialloc_context across
		 * the commit.  Holding this buffer prevents any other
		 * processes from doing any allocations in this
		 * allocation group.
		 */
		xfs_trans_bhold(tp, ialloc_context);

		/*
		 * We want the quota changes to be associated with the next
		 * transaction, NOT this one. So, detach the dqinfo from this
		 * and attach it to the next transaction.
		 */
		dqinfo = NULL;
		tflags = 0;
		if (tp->t_dqinfo) {
			dqinfo = (void *)tp->t_dqinfo;
			tp->t_dqinfo = NULL;
			tflags = tp->t_flags & XFS_TRANS_DQ_DIRTY;
			tp->t_flags &= ~(XFS_TRANS_DQ_DIRTY);
		}

		code = xfs_trans_roll(&tp, 0);
		if (committed != NULL)
			*committed = 1;

		/*
		 * Re-attach the quota info that we detached from prev trx.
		 */
		if (dqinfo) {
			tp->t_dqinfo = dqinfo;
			tp->t_flags |= tflags;
		}

		if (code) {
			xfs_buf_relse(ialloc_context);
			*tpp = tp;
			*ipp = NULL;
			return code;
		}
		xfs_trans_bjoin(tp, ialloc_context);

		/*
		 * Call ialloc again. Since we've locked out all
		 * other allocations in this allocation group,
		 * this call should always succeed.
		 */
		code = xfs_ialloc(tp, dp, mode, nlink, rdev, prid,
				  okalloc, &ialloc_context, &ip);

		/*
		 * If we get an error at this point, return to the caller
		 * so that the current transaction can be aborted.
		 */
		if (code) {
			*tpp = tp;
			*ipp = NULL;
			return code;
		}
		ASSERT(!ialloc_context && ip);

	} else {
		if (committed != NULL)
			*committed = 0;
	}

	*ipp = ip;
	*tpp = tp;

	return 0;
}

/*
 * Decrement the link count on an inode & log the change.
 * If this causes the link count to go to zero, initiate the
 * logging activity required to truncate a file.
 */
int				/* error */
xfs_droplink(
	xfs_trans_t *tp,
	xfs_inode_t *ip)
{
	int	error;

	xfs_trans_ichgtime(tp, ip, XFS_ICHGTIME_CHG);

	ASSERT (ip->i_d.di_nlink > 0);
	ip->i_d.di_nlink--;
	drop_nlink(VFS_I(ip));
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);

	error = 0;
	if (ip->i_d.di_nlink == 0) {
		/*
		 * We're dropping the last link to this file.
		 * Move the on-disk inode to the AGI unlinked list.
		 * From xfs_inactive() we will pull the inode from
		 * the list and free it.
		 */
		error = xfs_iunlink(tp, ip);
	}
	return error;
}

/*
 * Shrink the file to the given new_size.  The new size must be smaller than
 * the current size.  This will free up the underlying blocks in the removed
 * range after a call to xfs_itruncate_start() or xfs_atruncate_start().
 * Increment the link count on an inode & log the change.
 */
int
xfs_bumplink(
	xfs_trans_t *tp,
	xfs_inode_t *ip)
{
	xfs_trans_ichgtime(tp, ip, XFS_ICHGTIME_CHG);

	ASSERT(ip->i_d.di_version > 1);
	ASSERT(ip->i_d.di_nlink > 0 || (VFS_I(ip)->i_state & I_LINKABLE));
	ip->i_d.di_nlink++;
	inc_nlink(VFS_I(ip));
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	return 0;
}

int
xfs_create(
	xfs_inode_t		*dp,
	struct xfs_name		*name,
	umode_t			mode,
	xfs_dev_t		rdev,
	xfs_inode_t		**ipp)
{
	int			is_dir = S_ISDIR(mode);
	struct xfs_mount	*mp = dp->i_mount;
	struct xfs_inode	*ip = NULL;
	struct xfs_trans	*tp = NULL;
	int			error;
	xfs_bmap_free_t		free_list;
	xfs_fsblock_t		first_block;
	bool                    unlock_dp_on_error = false;
	int			committed;
	prid_t			prid;
	struct xfs_dquot	*udqp = NULL;
	struct xfs_dquot	*gdqp = NULL;
	struct xfs_dquot	*pdqp = NULL;
	struct xfs_trans_res	*tres;
	uint			resblks;

	trace_xfs_create(dp, name);

	if (XFS_FORCED_SHUTDOWN(mp))
		return -EIO;

	prid = xfs_get_initial_prid(dp);

	/*
	 * Make sure that we have allocated dquot(s) on disk.
	 */
	error = xfs_qm_vop_dqalloc(dp, xfs_kuid_to_uid(current_fsuid()),
					xfs_kgid_to_gid(current_fsgid()), prid,
					XFS_QMOPT_QUOTALL | XFS_QMOPT_INHERIT,
					&udqp, &gdqp, &pdqp);
	if (error)
		return error;

	if (is_dir) {
		rdev = 0;
		resblks = XFS_MKDIR_SPACE_RES(mp, name->len);
		tres = &M_RES(mp)->tr_mkdir;
		tp = xfs_trans_alloc(mp, XFS_TRANS_MKDIR);
	} else {
		resblks = XFS_CREATE_SPACE_RES(mp, name->len);
		tres = &M_RES(mp)->tr_create;
		tp = xfs_trans_alloc(mp, XFS_TRANS_CREATE);
	}

	/*
	 * Initially assume that the file does not exist and
	 * reserve the resources for that case.  If that is not
	 * the case we'll drop the one we have and get a more
	 * appropriate transaction later.
	 */
	error = xfs_trans_reserve(tp, tres, resblks, 0);
	if (error == -ENOSPC) {
		/* flush outstanding delalloc blocks and retry */
		xfs_flush_inodes(mp);
		error = xfs_trans_reserve(tp, tres, resblks, 0);
	}
	if (error == -ENOSPC) {
		/* No space at all so try a "no-allocation" reservation */
		resblks = 0;
		error = xfs_trans_reserve(tp, tres, 0, 0);
	}
	if (error)
		goto out_trans_cancel;


	xfs_ilock(dp, XFS_IOLOCK_EXCL | XFS_ILOCK_EXCL |
		      XFS_IOLOCK_PARENT | XFS_ILOCK_PARENT);
	unlock_dp_on_error = true;

	xfs_bmap_init(&free_list, &first_block);

	/*
	 * Reserve disk quota and the inode.
	 */
	error = xfs_trans_reserve_quota(tp, mp, udqp, gdqp,
						pdqp, resblks, 1, 0);
	if (error)
		goto out_trans_cancel;

	if (!resblks) {
		error = xfs_dir_canenter(tp, dp, name);
		if (error)
			goto out_trans_cancel;
	}

	/*
	 * A newly created regular or special file just has one directory
	 * entry pointing to them, but a directory also the "." entry
	 * pointing to itself.
	 */
	error = xfs_dir_ialloc(&tp, dp, mode, is_dir ? 2 : 1, rdev,
			       prid, resblks > 0, &ip, &committed);
	if (error)
		goto out_trans_cancel;

	/*
	 * Now we join the directory inode to the transaction.  We do not do it
	 * earlier because xfs_dir_ialloc might commit the previous transaction
	 * (and release all the locks).  An error from here on will result in
	 * the transaction cancel unlocking dp so don't do it explicitly in the
	 * error path.
	 */
	xfs_trans_ijoin(tp, dp, XFS_IOLOCK_EXCL | XFS_ILOCK_EXCL);
	unlock_dp_on_error = false;

	error = xfs_dir_createname(tp, dp, name, ip->i_ino,
					&first_block, &free_list, resblks ?
					resblks - XFS_IALLOC_SPACE_RES(mp) : 0);
	if (error) {
		ASSERT(error != -ENOSPC);
		goto out_trans_cancel;
	}
	xfs_trans_ichgtime(tp, dp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
	xfs_trans_log_inode(tp, dp, XFS_ILOG_CORE);

	if (is_dir) {
		error = xfs_dir_init(tp, ip, dp);
		if (error)
			goto out_bmap_cancel;

		error = xfs_bumplink(tp, dp);
		if (error)
			goto out_bmap_cancel;
	}

	/*
	 * If this is a synchronous mount, make sure that the
	 * create transaction goes to disk before returning to
	 * the user.
	 */
	if (mp->m_flags & (XFS_MOUNT_WSYNC|XFS_MOUNT_DIRSYNC))
		xfs_trans_set_sync(tp);

	/*
	 * Attach the dquot(s) to the inodes and modify them incore.
	 * These ids of the inode couldn't have changed since the new
	 * inode has been locked ever since it was created.
	 */
	xfs_qm_vop_create_dqattach(tp, ip, udqp, gdqp, pdqp);

	error = xfs_bmap_finish(&tp, &free_list, &committed);
	if (error)
		goto out_bmap_cancel;

	error = xfs_trans_commit(tp);
	if (error)
		goto out_release_inode;

	xfs_qm_dqrele(udqp);
	xfs_qm_dqrele(gdqp);
	xfs_qm_dqrele(pdqp);

	*ipp = ip;
	return 0;

 out_bmap_cancel:
	xfs_bmap_cancel(&free_list);
 out_trans_cancel:
	xfs_trans_cancel(tp);
 out_release_inode:
	/*
	 * Wait until after the current transaction is aborted to finish the
	 * setup of the inode and release the inode.  This prevents recursive
	 * transactions and deadlocks from xfs_inactive.
	 */
	if (ip) {
		xfs_finish_inode_setup(ip);
		IRELE(ip);
	}

	xfs_qm_dqrele(udqp);
	xfs_qm_dqrele(gdqp);
	xfs_qm_dqrele(pdqp);

	if (unlock_dp_on_error)
		xfs_iunlock(dp, XFS_IOLOCK_EXCL | XFS_ILOCK_EXCL);
	return error;
}

int
xfs_create_tmpfile(
	struct xfs_inode	*dp,
	struct dentry		*dentry,
	umode_t			mode,
	struct xfs_inode	**ipp)
{
	struct xfs_mount	*mp = dp->i_mount;
	struct xfs_inode	*ip = NULL;
	struct xfs_trans	*tp = NULL;
	int			error;
	prid_t                  prid;
	struct xfs_dquot	*udqp = NULL;
	struct xfs_dquot	*gdqp = NULL;
	struct xfs_dquot	*pdqp = NULL;
	struct xfs_trans_res	*tres;
	uint			resblks;

	if (XFS_FORCED_SHUTDOWN(mp))
		return -EIO;

	prid = xfs_get_initial_prid(dp);

	/*
	 * Make sure that we have allocated dquot(s) on disk.
	 */
	error = xfs_qm_vop_dqalloc(dp, xfs_kuid_to_uid(current_fsuid()),
				xfs_kgid_to_gid(current_fsgid()), prid,
				XFS_QMOPT_QUOTALL | XFS_QMOPT_INHERIT,
				&udqp, &gdqp, &pdqp);
	if (error)
		return error;

	resblks = XFS_IALLOC_SPACE_RES(mp);
	tp = xfs_trans_alloc(mp, XFS_TRANS_CREATE_TMPFILE);

	tres = &M_RES(mp)->tr_create_tmpfile;
	error = xfs_trans_reserve(tp, tres, resblks, 0);
	if (error == -ENOSPC) {
		/* No space at all so try a "no-allocation" reservation */
		resblks = 0;
		error = xfs_trans_reserve(tp, tres, 0, 0);
	}
	if (error)
		goto out_trans_cancel;

	error = xfs_trans_reserve_quota(tp, mp, udqp, gdqp,
						pdqp, resblks, 1, 0);
	if (error)
		goto out_trans_cancel;

	error = xfs_dir_ialloc(&tp, dp, mode, 1, 0,
				prid, resblks > 0, &ip, NULL);
	if (error)
		goto out_trans_cancel;

	if (mp->m_flags & XFS_MOUNT_WSYNC)
		xfs_trans_set_sync(tp);

	/*
	 * Attach the dquot(s) to the inodes and modify them incore.
	 * These ids of the inode couldn't have changed since the new
	 * inode has been locked ever since it was created.
	 */
	xfs_qm_vop_create_dqattach(tp, ip, udqp, gdqp, pdqp);

	ip->i_d.di_nlink--;
	error = xfs_iunlink(tp, ip);
	if (error)
		goto out_trans_cancel;

	error = xfs_trans_commit(tp);
	if (error)
		goto out_release_inode;

	xfs_qm_dqrele(udqp);
	xfs_qm_dqrele(gdqp);
	xfs_qm_dqrele(pdqp);

	*ipp = ip;
	return 0;

 out_trans_cancel:
	xfs_trans_cancel(tp);
 out_release_inode:
	/*
	 * Wait until after the current transaction is aborted to finish the
	 * setup of the inode and release the inode.  This prevents recursive
	 * transactions and deadlocks from xfs_inactive.
	 */
	if (ip) {
		xfs_finish_inode_setup(ip);
		IRELE(ip);
	}

	xfs_qm_dqrele(udqp);
	xfs_qm_dqrele(gdqp);
	xfs_qm_dqrele(pdqp);

	return error;
}

int
xfs_link(
	xfs_inode_t		*tdp,
	xfs_inode_t		*sip,
	struct xfs_name		*target_name)
{
	xfs_mount_t		*mp = tdp->i_mount;
	xfs_trans_t		*tp;
	int			error;
	xfs_bmap_free_t         free_list;
	xfs_fsblock_t           first_block;
	int			committed;
	int			resblks;

	trace_xfs_link(tdp, target_name);

	ASSERT(!S_ISDIR(sip->i_d.di_mode));

	if (XFS_FORCED_SHUTDOWN(mp))
		return -EIO;

	error = xfs_qm_dqattach(sip, 0);
	if (error)
		goto std_return;

	error = xfs_qm_dqattach(tdp, 0);
	if (error)
		goto std_return;

	tp = xfs_trans_alloc(mp, XFS_TRANS_LINK);
	resblks = XFS_LINK_SPACE_RES(mp, target_name->len);
	error = xfs_trans_reserve(tp, &M_RES(mp)->tr_link, resblks, 0);
	if (error == -ENOSPC) {
		resblks = 0;
		error = xfs_trans_reserve(tp, &M_RES(mp)->tr_link, 0, 0);
	}
	if (error)
		goto error_return;

	xfs_ilock(tdp, XFS_IOLOCK_EXCL | XFS_IOLOCK_PARENT);
	xfs_lock_two_inodes(sip, tdp, XFS_ILOCK_EXCL);

	xfs_trans_ijoin(tp, sip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, tdp, XFS_IOLOCK_EXCL | XFS_ILOCK_EXCL);

	/*
	 * If we are using project inheritance, we only allow hard link
	 * creation in our tree when the project IDs are the same; else
	 * the tree quota mechanism could be circumvented.
	 */
	if (unlikely((tdp->i_d.di_flags & XFS_DIFLAG_PROJINHERIT) &&
		     (xfs_get_projid(tdp) != xfs_get_projid(sip)))) {
		error = -EXDEV;
		goto error_return;
	}

	if (!resblks) {
		error = xfs_dir_canenter(tp, tdp, target_name);
		if (error)
			goto error_return;
	}

	xfs_bmap_init(&free_list, &first_block);

	if (sip->i_d.di_nlink == 0) {
		error = xfs_iunlink_remove(tp, sip);
		if (error)
			goto error_return;
	}

	error = xfs_dir_createname(tp, tdp, target_name, sip->i_ino,
					&first_block, &free_list, resblks);
	if (error)
		goto error_return;
	xfs_trans_ichgtime(tp, tdp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
	xfs_trans_log_inode(tp, tdp, XFS_ILOG_CORE);

	error = xfs_bumplink(tp, sip);
	if (error)
		goto error_return;

	/*
	 * If this is a synchronous mount, make sure that the
	 * link transaction goes to disk before returning to
	 * the user.
	 */
	if (mp->m_flags & (XFS_MOUNT_WSYNC|XFS_MOUNT_DIRSYNC)) {
		xfs_trans_set_sync(tp);
	}

	error = xfs_bmap_finish (&tp, &free_list, &committed);
	if (error) {
		xfs_bmap_cancel(&free_list);
		goto error_return;
	}

	return xfs_trans_commit(tp);

 error_return:
	xfs_trans_cancel(tp);
 std_return:
	return error;
}

/*
 * Free up the underlying blocks past new_size.  The new size must be smaller
 * than the current size.  This routine can be used both for the attribute and
 * data fork, and does not modify the inode size, which is left to the caller.
 *
 * The transaction passed to this routine must have made a permanent log
 * reservation of at least XFS_ITRUNCATE_LOG_RES.  This routine may commit the
 * given transaction and start new ones, so make sure everything involved in
 * the transaction is tidy before calling here.  Some transaction will be
 * returned to the caller to be committed.  The incoming transaction must
 * already include the inode, and both inode locks must be held exclusively.
 * The inode must also be "held" within the transaction.  On return the inode
 * will be "held" within the returned transaction.  This routine does NOT
 * require any disk space to be reserved for it within the transaction.
 *
 * The fork parameter must be either xfs_attr_fork or xfs_data_fork, and it
 * indicates the fork which is to be truncated.  For the attribute fork we only
 * support truncation to size 0.
 *
 * We use the sync parameter to indicate whether or not the first transaction
 * we perform might have to be synchronous.  For the attr fork, it needs to be
 * so if the unlink of the inode is not yet known to be permanent in the log.
 * This keeps us from freeing and reusing the blocks of the attribute fork
 * before the unlink of the inode becomes permanent.
 *
 * For the data fork, we normally have to run synchronously if we're being
 * called out of the inactive path or we're being called out of the create path
 * where we're truncating an existing file.  Either way, the truncate needs to
 * be sync so blocks don't reappear in the file with altered data in case of a
 * crash.  wsync filesystems can run the first case async because anything that
 * shrinks the inode has to run sync so by the time we're called here from
 * inactive, the inode size is permanently set to 0.
 *
 * Calls from the truncate path always need to be sync unless we're in a wsync
 * filesystem and the file has already been unlinked.
 *
 * The caller is responsible for correctly setting the sync parameter.  It gets
 * too hard for us to guess here which path we're being called out of just
 * based on inode state.
 *
 * If we get an error, we must return with the inode locked and linked into the
 * current transaction. This keeps things simple for the higher level code,
 * because it always knows that the inode is locked and held in the transaction
 * that returns to it whether errors occur or not.  We don't mark the inode
 * dirty on error so that transactions can be easily aborted if possible.
 */
int
xfs_itruncate_finish(
	xfs_trans_t	**tp,
	xfs_inode_t	*ip,
	xfs_fsize_t	new_size,
	int		fork,
	int		sync)
{
	xfs_fsblock_t	first_block;
	xfs_fileoff_t	first_unmap_block;
	xfs_fileoff_t	last_block;
	xfs_filblks_t	unmap_len=0;
	xfs_mount_t	*mp;
	xfs_trans_t	*ntp;
	int		done;
	int		committed;
	xfs_bmap_free_t	free_list;
	int		error;

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL|XFS_IOLOCK_EXCL));
	ASSERT((new_size == 0) || (new_size <= ip->i_size));
	ASSERT(*tp != NULL);
	ASSERT((*tp)->t_flags & XFS_TRANS_PERM_LOG_RES);
	ASSERT(ip->i_transp == *tp);
	ASSERT(ip->i_itemp != NULL);
	ASSERT(ip->i_itemp->ili_flags & XFS_ILI_HOLD);


	ntp = *tp;
	mp = (ntp)->t_mountp;
	ASSERT(! XFS_NOT_DQATTACHED(mp, ip));

	/*
	 * We only support truncating the entire attribute fork.
	 */
	if (fork == XFS_ATTR_FORK) {
		new_size = 0LL;
	}
	first_unmap_block = XFS_B_TO_FSB(mp, (xfs_ufsize_t)new_size);
	xfs_itrunc_trace(XFS_ITRUNC_FINISH1, ip, 0, new_size, 0, 0);
	/*
	 * The first thing we do is set the size to new_size permanently
	 * on disk.  This way we don't have to worry about anyone ever
	 * being able to look at the data being freed even in the face
	 * of a crash.  What we're getting around here is the case where
	 * we free a block, it is allocated to another file, it is written
	 * to, and then we crash.  If the new data gets written to the
	 * file but the log buffers containing the free and reallocation
	 * don't, then we'd end up with garbage in the blocks being freed.
	 * As long as we make the new_size permanent before actually
	 * freeing any blocks it doesn't matter if they get writtten to.
	 *
	 * The callers must signal into us whether or not the size
	 * setting here must be synchronous.  There are a few cases
	 * where it doesn't have to be synchronous.  Those cases
	 * occur if the file is unlinked and we know the unlink is
	 * permanent or if the blocks being truncated are guaranteed
	 * to be beyond the inode eof (regardless of the link count)
	 * and the eof value is permanent.  Both of these cases occur
	 * only on wsync-mounted filesystems.  In those cases, we're
	 * guaranteed that no user will ever see the data in the blocks
	 * that are being truncated so the truncate can run async.
	 * In the free beyond eof case, the file may wind up with
	 * more blocks allocated to it than it needs if we crash
	 * and that won't get fixed until the next time the file
	 * is re-opened and closed but that's ok as that shouldn't
	 * be too many blocks.
	 *
	 * However, we can't just make all wsync xactions run async
	 * because there's one call out of the create path that needs
	 * to run sync where it's truncating an existing file to size
	 * 0 whose size is > 0.
	 *
	 * It's probably possible to come up with a test in this
	 * routine that would correctly distinguish all the above
	 * cases from the values of the function parameters and the
	 * inode state but for sanity's sake, I've decided to let the
	 * layers above just tell us.  It's simpler to correctly figure
	 * out in the layer above exactly under what conditions we
	 * can run async and I think it's easier for others read and
	 * follow the logic in case something has to be changed.
	 * cscope is your friend -- rcc.
	 *
	 * The attribute fork is much simpler.
	 *
	 * For the attribute fork we allow the caller to tell us whether
	 * the unlink of the inode that led to this call is yet permanent
	 * in the on disk log.  If it is not and we will be freeing extents
	 * in this inode then we make the first transaction synchronous
	 * to make sure that the unlink is permanent by the time we free
	 * the blocks.
	 */
	if (fork == XFS_DATA_FORK) {
		if (ip->i_d.di_nextents > 0) {
			/*
			 * If we are not changing the file size then do
			 * not update the on-disk file size - we may be
			 * called from xfs_inactive_free_eofblocks().  If we
			 * update the on-disk file size and then the system
			 * crashes before the contents of the file are
			 * flushed to disk then the files may be full of
			 * holes (ie NULL files bug).
			 */
			if (ip->i_size != new_size) {
				ip->i_d.di_size = new_size;
				ip->i_size = new_size;
				xfs_trans_log_inode(ntp, ip, XFS_ILOG_CORE);
			}
		}
	} else if (sync) {
		ASSERT(!(mp->m_flags & XFS_MOUNT_WSYNC));
		if (ip->i_d.di_anextents > 0)
			xfs_trans_set_sync(ntp);
	}
	ASSERT(fork == XFS_DATA_FORK ||
		(fork == XFS_ATTR_FORK &&
			((sync && !(mp->m_flags & XFS_MOUNT_WSYNC)) ||
			 (sync == 0 && (mp->m_flags & XFS_MOUNT_WSYNC)))));
xfs_itruncate_extents(
	struct xfs_trans	**tpp,
	struct xfs_inode	*ip,
	int			whichfork,
	xfs_fsize_t		new_size)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_trans	*tp = *tpp;
	xfs_bmap_free_t		free_list;
	xfs_fsblock_t		first_block;
	xfs_fileoff_t		first_unmap_block;
	xfs_fileoff_t		last_block;
	xfs_filblks_t		unmap_len;
	int			committed;
	int			error = 0;
	int			done = 0;

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));
	ASSERT(!atomic_read(&VFS_I(ip)->i_count) ||
	       xfs_isilocked(ip, XFS_IOLOCK_EXCL));
	ASSERT(new_size <= XFS_ISIZE(ip));
	ASSERT(tp->t_flags & XFS_TRANS_PERM_LOG_RES);
	ASSERT(ip->i_itemp != NULL);
	ASSERT(ip->i_itemp->ili_lock_flags == 0);
	ASSERT(!XFS_NOT_DQATTACHED(mp, ip));

	trace_xfs_itruncate_extents_start(ip, new_size);

	/*
	 * Since it is possible for space to become allocated beyond
	 * the end of the file (in a crash where the space is allocated
	 * but the inode size is not yet updated), simply remove any
	 * blocks which show up between the new EOF and the maximum
	 * possible file size.  If the first block to be removed is
	 * beyond the maximum file size (ie it is the same as last_block),
	 * then there is nothing to do.
	 */
	last_block = XFS_B_TO_FSB(mp, (xfs_ufsize_t)XFS_MAXIOFFSET(mp));
	ASSERT(first_unmap_block <= last_block);
	done = 0;
	if (last_block == first_unmap_block) {
		done = 1;
	} else {
		unmap_len = last_block - first_unmap_block + 1;
	}
	while (!done) {
		/*
		 * Free up up to XFS_ITRUNC_MAX_EXTENTS.  xfs_bunmapi()
		 * will tell us whether it freed the entire range or
		 * not.  If this is a synchronous mount (wsync),
		 * then we can tell bunmapi to keep all the
		 * transactions asynchronous since the unlink
		 * transaction that made this inode inactive has
		 * already hit the disk.  There's no danger of
		 * the freed blocks being reused, there being a
		 * crash, and the reused blocks suddenly reappearing
		 * in this file with garbage in them once recovery
		 * runs.
		 */
		XFS_BMAP_INIT(&free_list, &first_block);
		error = xfs_bunmapi(ntp, ip,
				    first_unmap_block, unmap_len,
				    XFS_BMAPI_AFLAG(fork) |
				      (sync ? 0 : XFS_BMAPI_ASYNC),
				    XFS_ITRUNC_MAX_EXTENTS,
				    &first_block, &free_list,
				    NULL, &done);
		if (error) {
			/*
			 * If the bunmapi call encounters an error,
			 * return to the caller where the transaction
			 * can be properly aborted.  We just need to
			 * make sure we're not holding any resources
			 * that we were not when we came in.
			 */
			xfs_bmap_cancel(&free_list);
			return error;
		}
	first_unmap_block = XFS_B_TO_FSB(mp, (xfs_ufsize_t)new_size);
	last_block = XFS_B_TO_FSB(mp, mp->m_super->s_maxbytes);
	if (first_unmap_block == last_block)
		return 0;

	ASSERT(first_unmap_block < last_block);
	unmap_len = last_block - first_unmap_block + 1;
	while (!done) {
		xfs_bmap_init(&free_list, &first_block);
		error = xfs_bunmapi(tp, ip,
				    first_unmap_block, unmap_len,
				    xfs_bmapi_aflag(whichfork),
				    XFS_ITRUNC_MAX_EXTENTS,
				    &first_block, &free_list,
				    &done);
		if (error)
			goto out_bmap_cancel;

		/*
		 * Duplicate the transaction that has the permanent
		 * reservation and commit the old transaction.
		 */
		error = xfs_bmap_finish(tp, &free_list, &committed);
		ntp = *tp;
		if (committed) {
			/* link the inode into the next xact in the chain */
			xfs_trans_ijoin(ntp, ip,
					XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);
			xfs_trans_ihold(ntp, ip);
		}

		if (error) {
			/*
			 * If the bmap finish call encounters an error, return
			 * to the caller where the transaction can be properly
			 * aborted.  We just need to make sure we're not
			 * holding any resources that we were not when we came
			 * in.
			 *
			 * Aborting from this point might lose some blocks in
			 * the file system, but oh well.
			 */
			xfs_bmap_cancel(&free_list);
			return error;
		}

		if (committed) {
			/*
			 * Mark the inode dirty so it will be logged and
			 * moved forward in the log as part of every commit.
			 */
			xfs_trans_log_inode(ntp, ip, XFS_ILOG_CORE);
		}

		ntp = xfs_trans_dup(ntp);
		error = xfs_trans_commit(*tp, 0);
		*tp = ntp;

		/* link the inode into the next transaction in the chain */
		xfs_trans_ijoin(ntp, ip, XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);
		xfs_trans_ihold(ntp, ip);

		if (!error)
			error = xfs_trans_reserve(ntp, 0,
					XFS_ITRUNCATE_LOG_RES(mp), 0,
					XFS_TRANS_PERM_LOG_RES,
					XFS_ITRUNCATE_LOG_COUNT);
		if (error)
			return error;
	}
	/*
	 * Only update the size in the case of the data fork, but
	 * always re-log the inode so that our permanent transaction
	 * can keep on rolling it forward in the log.
	 */
	if (fork == XFS_DATA_FORK) {
		xfs_isize_check(mp, ip, new_size);
		/*
		 * If we are not changing the file size then do
		 * not update the on-disk file size - we may be
		 * called from xfs_inactive_free_eofblocks().  If we
		 * update the on-disk file size and then the system
		 * crashes before the contents of the file are
		 * flushed to disk then the files may be full of
		 * holes (ie NULL files bug).
		 */
		if (ip->i_size != new_size) {
			ip->i_d.di_size = new_size;
			ip->i_size = new_size;
		}
	}
	xfs_trans_log_inode(ntp, ip, XFS_ILOG_CORE);
	ASSERT((new_size != 0) ||
	       (fork == XFS_ATTR_FORK) ||
	       (ip->i_delayed_blks == 0));
	ASSERT((new_size != 0) ||
	       (fork == XFS_ATTR_FORK) ||
	       (ip->i_d.di_nextents == 0));
	xfs_itrunc_trace(XFS_ITRUNC_FINISH2, ip, 0, new_size, 0, 0);
		error = xfs_bmap_finish(&tp, &free_list, &committed);
		if (committed)
			xfs_trans_ijoin(tp, ip, 0);
		if (error)
			goto out_bmap_cancel;

		error = xfs_trans_roll(&tp, ip);
		if (error)
			goto out;
	}

	/*
	 * Always re-log the inode so that our permanent transaction can keep
	 * on rolling it forward in the log.
	 */
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);

	trace_xfs_itruncate_extents_end(ip, new_size);

out:
	*tpp = tp;
	return error;
out_bmap_cancel:
	/*
	 * If the bunmapi call encounters an error, return to the caller where
	 * the transaction can be properly aborted.  We just need to make sure
	 * we're not holding any resources that we were not when we came in.
	 */
	xfs_bmap_cancel(&free_list);
	goto out;
}

int
xfs_release(
	xfs_inode_t	*ip)
{
	xfs_mount_t	*mp = ip->i_mount;
	int		error;

	if (!S_ISREG(ip->i_d.di_mode) || (ip->i_d.di_mode == 0))
		return 0;

	/* If this is a read-only mount, don't do this (would generate I/O) */
	if (mp->m_flags & XFS_MOUNT_RDONLY)
		return 0;

	if (!XFS_FORCED_SHUTDOWN(mp)) {
		int truncated;

		/*
		 * If we previously truncated this file and removed old data
		 * in the process, we want to initiate "early" writeout on
		 * the last close.  This is an attempt to combat the notorious
		 * NULL files problem which is particularly noticeable from a
		 * truncate down, buffered (re-)write (delalloc), followed by
		 * a crash.  What we are effectively doing here is
		 * significantly reducing the time window where we'd otherwise
		 * be exposed to that problem.
		 */
		truncated = xfs_iflags_test_and_clear(ip, XFS_ITRUNCATED);
		if (truncated) {
			xfs_iflags_clear(ip, XFS_IDIRTY_RELEASE);
			if (ip->i_delayed_blks > 0) {
				error = filemap_flush(VFS_I(ip)->i_mapping);
				if (error)
					return error;
			}
		}
	}

	if (ip->i_d.di_nlink == 0)
		return 0;

	if (xfs_can_free_eofblocks(ip, false)) {

		/*
		 * If we can't get the iolock just skip truncating the blocks
		 * past EOF because we could deadlock with the mmap_sem
		 * otherwise.  We'll get another chance to drop them once the
		 * last reference to the inode is dropped, so we'll never leak
		 * blocks permanently.
		 *
		 * Further, check if the inode is being opened, written and
		 * closed frequently and we have delayed allocation blocks
		 * outstanding (e.g. streaming writes from the NFS server),
		 * truncating the blocks past EOF will cause fragmentation to
		 * occur.
		 *
		 * In this case don't do the truncation, either, but we have to
		 * be careful how we detect this case. Blocks beyond EOF show
		 * up as i_delayed_blks even when the inode is clean, so we
		 * need to truncate them away first before checking for a dirty
		 * release. Hence on the first dirty close we will still remove
		 * the speculative allocation, but after that we will leave it
		 * in place.
		 */
		if (xfs_iflags_test(ip, XFS_IDIRTY_RELEASE))
			return 0;

		error = xfs_free_eofblocks(mp, ip, true);
		if (error && error != -EAGAIN)
			return error;

		/* delalloc blocks after truncation means it really is dirty */
		if (ip->i_delayed_blks)
			xfs_iflags_set(ip, XFS_IDIRTY_RELEASE);
	}
	return 0;
}

/*
 * xfs_inactive_truncate
 *
 * Called to perform a truncate when an inode becomes unlinked.
 */
STATIC int
xfs_inactive_truncate(
	struct xfs_inode *ip)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_trans	*tp;
	int			error;

	tp = xfs_trans_alloc(mp, XFS_TRANS_INACTIVE);
	error = xfs_trans_reserve(tp, &M_RES(mp)->tr_itruncate, 0, 0);
	if (error) {
		ASSERT(XFS_FORCED_SHUTDOWN(mp));
		xfs_trans_cancel(tp);
		return error;
	}

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, 0);

	/*
	 * Log the inode size first to prevent stale data exposure in the event
	 * of a system crash before the truncate completes. See the related
	 * comment in xfs_setattr_size() for details.
	 */
	ip->i_d.di_size = 0;
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);

	error = xfs_itruncate_extents(&tp, ip, XFS_DATA_FORK, 0);
	if (error)
		goto error_trans_cancel;

	ASSERT(ip->i_d.di_nextents == 0);

	error = xfs_trans_commit(tp);
	if (error)
		goto error_unlock;

	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return 0;

error_trans_cancel:
	xfs_trans_cancel(tp);
error_unlock:
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return error;
}

/*
 * xfs_inactive_ifree()
 *
 * Perform the inode free when an inode is unlinked.
 */
STATIC int
xfs_inactive_ifree(
	struct xfs_inode *ip)
{
	xfs_bmap_free_t		free_list;
	xfs_fsblock_t		first_block;
	int			committed;
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_trans	*tp;
	int			error;

	tp = xfs_trans_alloc(mp, XFS_TRANS_INACTIVE);

	/*
	 * The ifree transaction might need to allocate blocks for record
	 * insertion to the finobt. We don't want to fail here at ENOSPC, so
	 * allow ifree to dip into the reserved block pool if necessary.
	 *
	 * Freeing large sets of inodes generally means freeing inode chunks,
	 * directory and file data blocks, so this should be relatively safe.
	 * Only under severe circumstances should it be possible to free enough
	 * inodes to exhaust the reserve block pool via finobt expansion while
	 * at the same time not creating free space in the filesystem.
	 *
	 * Send a warning if the reservation does happen to fail, as the inode
	 * now remains allocated and sits on the unlinked list until the fs is
	 * repaired.
	 */
	tp->t_flags |= XFS_TRANS_RESERVE;
	error = xfs_trans_reserve(tp, &M_RES(mp)->tr_ifree,
				  XFS_IFREE_SPACE_RES(mp), 0);
	if (error) {
		if (error == -ENOSPC) {
			xfs_warn_ratelimited(mp,
			"Failed to remove inode(s) from unlinked list. "
			"Please free space, unmount and run xfs_repair.");
		} else {
			ASSERT(XFS_FORCED_SHUTDOWN(mp));
		}
		xfs_trans_cancel(tp);
		return error;
	}

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, 0);

	xfs_bmap_init(&free_list, &first_block);
	error = xfs_ifree(tp, ip, &free_list);
	if (error) {
		/*
		 * If we fail to free the inode, shut down.  The cancel
		 * might do that, we need to make sure.  Otherwise the
		 * inode might be lost for a long time or forever.
		 */
		if (!XFS_FORCED_SHUTDOWN(mp)) {
			xfs_notice(mp, "%s: xfs_ifree returned error %d",
				__func__, error);
			xfs_force_shutdown(mp, SHUTDOWN_META_IO_ERROR);
		}
		xfs_trans_cancel(tp);
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		return error;
	}

	/*
	 * Credit the quota account(s). The inode is gone.
	 */
	xfs_trans_mod_dquot_byino(tp, ip, XFS_TRANS_DQ_ICOUNT, -1);

	/*
	 * Just ignore errors at this point.  There is nothing we can do except
	 * to try to keep going. Make sure it's not a silent error.
	 */
	error = xfs_bmap_finish(&tp,  &free_list, &committed);
	if (error) {
		xfs_notice(mp, "%s: xfs_bmap_finish returned error %d",
			__func__, error);
		xfs_bmap_cancel(&free_list);
	}
	error = xfs_trans_commit(tp);
	if (error)
		xfs_notice(mp, "%s: xfs_trans_commit returned error %d",
			__func__, error);

	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return 0;
}

/*
 * xfs_inactive
 *
 * This is called when the vnode reference count for the vnode
 * goes to zero.  If the file has been unlinked, then it must
 * now be truncated.  Also, we clear all of the read-ahead state
 * kept for the inode here since the file is now closed.
 */
void
xfs_inactive(
	xfs_inode_t	*ip)
{
	struct xfs_mount	*mp;
	int			error;
	int			truncate = 0;

	/*
	 * If the inode is already free, then there can be nothing
	 * to clean up here.
	 */
	if (ip->i_d.di_mode == 0) {
		ASSERT(ip->i_df.if_real_bytes == 0);
		ASSERT(ip->i_df.if_broot_bytes == 0);
		return;
	}

	mp = ip->i_mount;

	/* If this is a read-only mount, don't do this (would generate I/O) */
	if (mp->m_flags & XFS_MOUNT_RDONLY)
		return;

	if (ip->i_d.di_nlink != 0) {
		/*
		 * force is true because we are evicting an inode from the
		 * cache. Post-eof blocks must be freed, lest we end up with
		 * broken free space accounting.
		 */
		if (xfs_can_free_eofblocks(ip, true))
			xfs_free_eofblocks(mp, ip, false);

		return;
	}

	if (S_ISREG(ip->i_d.di_mode) &&
	    (ip->i_d.di_size != 0 || XFS_ISIZE(ip) != 0 ||
	     ip->i_d.di_nextents > 0 || ip->i_delayed_blks > 0))
		truncate = 1;

	error = xfs_qm_dqattach(ip, 0);
	if (error)
		return;

	if (S_ISLNK(ip->i_d.di_mode))
		error = xfs_inactive_symlink(ip);
	else if (truncate)
		error = xfs_inactive_truncate(ip);
	if (error)
		return;

	/*
	 * If there are attributes associated with the file then blow them away
	 * now.  The code calls a routine that recursively deconstructs the
	 * attribute fork. If also blows away the in-core attribute fork.
	 */
	if (XFS_IFORK_Q(ip)) {
		error = xfs_attr_inactive(ip);
		if (error)
			return;
	}

	ASSERT(!ip->i_afp);
	ASSERT(ip->i_d.di_anextents == 0);
	ASSERT(ip->i_d.di_forkoff == 0);

	/*
	 * Free the inode.
	 */
	error = xfs_inactive_ifree(ip);
	if (error)
		return;

	/*
	 * Release the dquots held by inode, if any.
	 */
	xfs_qm_dqdetach(ip);
}

/*
 * This is called when the inode's link count goes to 0.
 * We place the on-disk inode on a list in the AGI.  It
 * will be pulled from this list when the inode is freed.
 */
int
xfs_iunlink(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip)
{
	xfs_mount_t	*mp;
	xfs_agi_t	*agi;
	xfs_dinode_t	*dip;
	xfs_buf_t	*agibp;
	xfs_buf_t	*ibp;
	xfs_agnumber_t	agno;
	xfs_daddr_t	agdaddr;
	xfs_agino_t	agino;
	short		bucket_index;
	int		offset;
	int		error;
	int		agi_ok;

	ASSERT(ip->i_d.di_nlink == 0);
	ASSERT(ip->i_d.di_mode != 0);
	ASSERT(ip->i_transp == tp);

	mp = tp->t_mountp;

	agno = XFS_INO_TO_AGNO(mp, ip->i_ino);
	agdaddr = XFS_AG_DADDR(mp, agno, XFS_AGI_DADDR(mp));


	ASSERT(ip->i_d.di_nlink == 0);
	ASSERT(ip->i_d.di_mode != 0);

	mp = tp->t_mountp;

	/*
	 * Get the agi buffer first.  It ensures lock ordering
	 * on the list.
	 */
	error = xfs_trans_read_buf(mp, tp, mp->m_ddev_targp, agdaddr,
				   XFS_FSS_TO_BB(mp, 1), 0, &agibp);
	if (error)
		return error;

	/*
	 * Validate the magic number of the agi block.
	 */
	agi = XFS_BUF_TO_AGI(agibp);
	agi_ok =
		be32_to_cpu(agi->agi_magicnum) == XFS_AGI_MAGIC &&
		XFS_AGI_GOOD_VERSION(be32_to_cpu(agi->agi_versionnum));
	if (unlikely(XFS_TEST_ERROR(!agi_ok, mp, XFS_ERRTAG_IUNLINK,
			XFS_RANDOM_IUNLINK))) {
		XFS_CORRUPTION_ERROR("xfs_iunlink", XFS_ERRLEVEL_LOW, mp, agi);
		xfs_trans_brelse(tp, agibp);
		return XFS_ERROR(EFSCORRUPTED);
	}
	error = xfs_read_agi(mp, tp, XFS_INO_TO_AGNO(mp, ip->i_ino), &agibp);
	if (error)
		return error;
	agi = XFS_BUF_TO_AGI(agibp);

	/*
	 * Get the index into the agi hash table for the
	 * list this inode will go on.
	 */
	agino = XFS_INO_TO_AGINO(mp, ip->i_ino);
	ASSERT(agino != 0);
	bucket_index = agino % XFS_AGI_UNLINKED_BUCKETS;
	ASSERT(agi->agi_unlinked[bucket_index]);
	ASSERT(be32_to_cpu(agi->agi_unlinked[bucket_index]) != agino);

	if (be32_to_cpu(agi->agi_unlinked[bucket_index]) != NULLAGINO) {
	if (agi->agi_unlinked[bucket_index] != cpu_to_be32(NULLAGINO)) {
		/*
		 * There is already another inode in the bucket we need
		 * to add ourselves to.  Add us at the front of the list.
		 * Here we put the head pointer into our next pointer,
		 * and then we fall through to point the head at us.
		 */
		error = xfs_itobp(mp, tp, ip, &dip, &ibp, 0, 0, XFS_BUF_LOCK);
		if (error)
			return error;

		ASSERT(be32_to_cpu(dip->di_next_unlinked) == NULLAGINO);
		/* both on-disk, don't endian flip twice */
		dip->di_next_unlinked = agi->agi_unlinked[bucket_index];
		offset = ip->i_boffset +
			offsetof(xfs_dinode_t, di_next_unlinked);
		error = xfs_imap_to_bp(mp, tp, &ip->i_imap, &dip, &ibp,
				       0, 0);
		if (error)
			return error;

		ASSERT(dip->di_next_unlinked == cpu_to_be32(NULLAGINO));
		dip->di_next_unlinked = agi->agi_unlinked[bucket_index];
		offset = ip->i_imap.im_boffset +
			offsetof(xfs_dinode_t, di_next_unlinked);

		/* need to recalc the inode CRC if appropriate */
		xfs_dinode_calc_crc(mp, dip);

		xfs_trans_inode_buf(tp, ibp);
		xfs_trans_log_buf(tp, ibp, offset,
				  (offset + sizeof(xfs_agino_t) - 1));
		xfs_inobp_check(mp, ibp);
	}

	/*
	 * Point the bucket head pointer at the inode being inserted.
	 */
	ASSERT(agino != 0);
	agi->agi_unlinked[bucket_index] = cpu_to_be32(agino);
	offset = offsetof(xfs_agi_t, agi_unlinked) +
		(sizeof(xfs_agino_t) * bucket_index);
	xfs_trans_buf_set_type(tp, agibp, XFS_BLFT_AGI_BUF);
	xfs_trans_log_buf(tp, agibp, offset,
			  (offset + sizeof(xfs_agino_t) - 1));
	return 0;
}

/*
 * Pull the on-disk inode from the AGI unlinked list.
 */
STATIC int
xfs_iunlink_remove(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip)
{
	xfs_ino_t	next_ino;
	xfs_mount_t	*mp;
	xfs_agi_t	*agi;
	xfs_dinode_t	*dip;
	xfs_buf_t	*agibp;
	xfs_buf_t	*ibp;
	xfs_agnumber_t	agno;
	xfs_daddr_t	agdaddr;
	xfs_agino_t	agino;
	xfs_agino_t	next_agino;
	xfs_buf_t	*last_ibp;
	xfs_dinode_t	*last_dip = NULL;
	short		bucket_index;
	int		offset, last_offset = 0;
	int		error;
	int		agi_ok;

	/*
	 * First pull the on-disk inode from the AGI unlinked list.
	 */
	mp = tp->t_mountp;

	agno = XFS_INO_TO_AGNO(mp, ip->i_ino);
	agdaddr = XFS_AG_DADDR(mp, agno, XFS_AGI_DADDR(mp));

	mp = tp->t_mountp;
	agno = XFS_INO_TO_AGNO(mp, ip->i_ino);

	/*
	 * Get the agi buffer first.  It ensures lock ordering
	 * on the list.
	 */
	error = xfs_trans_read_buf(mp, tp, mp->m_ddev_targp, agdaddr,
				   XFS_FSS_TO_BB(mp, 1), 0, &agibp);
	if (error) {
		cmn_err(CE_WARN,
			"xfs_iunlink_remove: xfs_trans_read_buf()  returned an error %d on %s.  Returning error.",
			error, mp->m_fsname);
		return error;
	}
	/*
	 * Validate the magic number of the agi block.
	 */
	agi = XFS_BUF_TO_AGI(agibp);
	agi_ok =
		be32_to_cpu(agi->agi_magicnum) == XFS_AGI_MAGIC &&
		XFS_AGI_GOOD_VERSION(be32_to_cpu(agi->agi_versionnum));
	if (unlikely(XFS_TEST_ERROR(!agi_ok, mp, XFS_ERRTAG_IUNLINK_REMOVE,
			XFS_RANDOM_IUNLINK_REMOVE))) {
		XFS_CORRUPTION_ERROR("xfs_iunlink_remove", XFS_ERRLEVEL_LOW,
				     mp, agi);
		xfs_trans_brelse(tp, agibp);
		cmn_err(CE_WARN,
			"xfs_iunlink_remove: XFS_TEST_ERROR()  returned an error on %s.  Returning EFSCORRUPTED.",
			 mp->m_fsname);
		return XFS_ERROR(EFSCORRUPTED);
	}
	error = xfs_read_agi(mp, tp, agno, &agibp);
	if (error)
		return error;

	agi = XFS_BUF_TO_AGI(agibp);

	/*
	 * Get the index into the agi hash table for the
	 * list this inode will go on.
	 */
	agino = XFS_INO_TO_AGINO(mp, ip->i_ino);
	ASSERT(agino != 0);
	bucket_index = agino % XFS_AGI_UNLINKED_BUCKETS;
	ASSERT(be32_to_cpu(agi->agi_unlinked[bucket_index]) != NULLAGINO);
	ASSERT(agi->agi_unlinked[bucket_index] != cpu_to_be32(NULLAGINO));
	ASSERT(agi->agi_unlinked[bucket_index]);

	if (be32_to_cpu(agi->agi_unlinked[bucket_index]) == agino) {
		/*
		 * We're at the head of the list.  Get the inode's
		 * on-disk buffer to see if there is anyone after us
		 * on the list.  Only modify our next pointer if it
		 * is not already NULLAGINO.  This saves us the overhead
		 * of dealing with the buffer when there is no need to
		 * change it.
		 */
		error = xfs_itobp(mp, tp, ip, &dip, &ibp, 0, 0, XFS_BUF_LOCK);
		if (error) {
			cmn_err(CE_WARN,
				"xfs_iunlink_remove: xfs_itobp()  returned an error %d on %s.  Returning error.",
				error, mp->m_fsname);
		 * We're at the head of the list.  Get the inode's on-disk
		 * buffer to see if there is anyone after us on the list.
		 * Only modify our next pointer if it is not already NULLAGINO.
		 * This saves us the overhead of dealing with the buffer when
		 * there is no need to change it.
		 */
		error = xfs_imap_to_bp(mp, tp, &ip->i_imap, &dip, &ibp,
				       0, 0);
		if (error) {
			xfs_warn(mp, "%s: xfs_imap_to_bp returned error %d.",
				__func__, error);
			return error;
		}
		next_agino = be32_to_cpu(dip->di_next_unlinked);
		ASSERT(next_agino != 0);
		if (next_agino != NULLAGINO) {
			dip->di_next_unlinked = cpu_to_be32(NULLAGINO);
			offset = ip->i_boffset +
				offsetof(xfs_dinode_t, di_next_unlinked);
			offset = ip->i_imap.im_boffset +
				offsetof(xfs_dinode_t, di_next_unlinked);

			/* need to recalc the inode CRC if appropriate */
			xfs_dinode_calc_crc(mp, dip);

			xfs_trans_inode_buf(tp, ibp);
			xfs_trans_log_buf(tp, ibp, offset,
					  (offset + sizeof(xfs_agino_t) - 1));
			xfs_inobp_check(mp, ibp);
		} else {
			xfs_trans_brelse(tp, ibp);
		}
		/*
		 * Point the bucket head pointer at the next inode.
		 */
		ASSERT(next_agino != 0);
		ASSERT(next_agino != agino);
		agi->agi_unlinked[bucket_index] = cpu_to_be32(next_agino);
		offset = offsetof(xfs_agi_t, agi_unlinked) +
			(sizeof(xfs_agino_t) * bucket_index);
		xfs_trans_buf_set_type(tp, agibp, XFS_BLFT_AGI_BUF);
		xfs_trans_log_buf(tp, agibp, offset,
				  (offset + sizeof(xfs_agino_t) - 1));
	} else {
		/*
		 * We need to search the list for the inode being freed.
		 */
		next_agino = be32_to_cpu(agi->agi_unlinked[bucket_index]);
		last_ibp = NULL;
		while (next_agino != agino) {
			/*
			 * If the last inode wasn't the one pointing to
			 * us, then release its buffer since we're not
			 * going to do anything with it.
			 */
			if (last_ibp != NULL) {
				xfs_trans_brelse(tp, last_ibp);
			}
			next_ino = XFS_AGINO_TO_INO(mp, agno, next_agino);
			error = xfs_inotobp(mp, tp, next_ino, &last_dip,
					    &last_ibp, &last_offset);
			if (error) {
				cmn_err(CE_WARN,
			"xfs_iunlink_remove: xfs_inotobp()  returned an error %d on %s.  Returning error.",
					error, mp->m_fsname);
				return error;
			}
			struct xfs_imap	imap;

			if (last_ibp)
				xfs_trans_brelse(tp, last_ibp);

			imap.im_blkno = 0;
			next_ino = XFS_AGINO_TO_INO(mp, agno, next_agino);

			error = xfs_imap(mp, tp, next_ino, &imap, 0);
			if (error) {
				xfs_warn(mp,
	"%s: xfs_imap returned error %d.",
					 __func__, error);
				return error;
			}

			error = xfs_imap_to_bp(mp, tp, &imap, &last_dip,
					       &last_ibp, 0, 0);
			if (error) {
				xfs_warn(mp,
	"%s: xfs_imap_to_bp returned error %d.",
					__func__, error);
				return error;
			}

			last_offset = imap.im_boffset;
			next_agino = be32_to_cpu(last_dip->di_next_unlinked);
			ASSERT(next_agino != NULLAGINO);
			ASSERT(next_agino != 0);
		}
		/*
		 * Now last_ibp points to the buffer previous to us on
		 * the unlinked list.  Pull us from the list.
		 */
		error = xfs_itobp(mp, tp, ip, &dip, &ibp, 0, 0, XFS_BUF_LOCK);
		if (error) {
			cmn_err(CE_WARN,
				"xfs_iunlink_remove: xfs_itobp()  returned an error %d on %s.  Returning error.",
				error, mp->m_fsname);

		/*
		 * Now last_ibp points to the buffer previous to us on the
		 * unlinked list.  Pull us from the list.
		 */
		error = xfs_imap_to_bp(mp, tp, &ip->i_imap, &dip, &ibp,
				       0, 0);
		if (error) {
			xfs_warn(mp, "%s: xfs_imap_to_bp(2) returned error %d.",
				__func__, error);
			return error;
		}
		next_agino = be32_to_cpu(dip->di_next_unlinked);
		ASSERT(next_agino != 0);
		ASSERT(next_agino != agino);
		if (next_agino != NULLAGINO) {
			dip->di_next_unlinked = cpu_to_be32(NULLAGINO);
			offset = ip->i_boffset +
				offsetof(xfs_dinode_t, di_next_unlinked);
			offset = ip->i_imap.im_boffset +
				offsetof(xfs_dinode_t, di_next_unlinked);

			/* need to recalc the inode CRC if appropriate */
			xfs_dinode_calc_crc(mp, dip);

			xfs_trans_inode_buf(tp, ibp);
			xfs_trans_log_buf(tp, ibp, offset,
					  (offset + sizeof(xfs_agino_t) - 1));
			xfs_inobp_check(mp, ibp);
		} else {
			xfs_trans_brelse(tp, ibp);
		}
		/*
		 * Point the previous inode on the list to the next inode.
		 */
		last_dip->di_next_unlinked = cpu_to_be32(next_agino);
		ASSERT(next_agino != 0);
		offset = last_offset + offsetof(xfs_dinode_t, di_next_unlinked);

		/* need to recalc the inode CRC if appropriate */
		xfs_dinode_calc_crc(mp, last_dip);

		xfs_trans_inode_buf(tp, last_ibp);
		xfs_trans_log_buf(tp, last_ibp, offset,
				  (offset + sizeof(xfs_agino_t) - 1));
		xfs_inobp_check(mp, last_ibp);
	}
	return 0;
}

STATIC void
xfs_ifree_cluster(
	xfs_inode_t	*free_ip,
	xfs_trans_t	*tp,
	xfs_ino_t	inum)
{
	xfs_mount_t		*mp = free_ip->i_mount;
	int			blks_per_cluster;
	int			nbufs;
	int			ninodes;
	int			i, j, found, pre_flushed;
	xfs_daddr_t		blkno;
	xfs_buf_t		*bp;
	xfs_inode_t		*ip, **ip_found;
	xfs_inode_log_item_t	*iip;
	xfs_log_item_t		*lip;
	xfs_perag_t		*pag = xfs_get_perag(mp, inum);

	if (mp->m_sb.sb_blocksize >= XFS_INODE_CLUSTER_SIZE(mp)) {
		blks_per_cluster = 1;
		ninodes = mp->m_sb.sb_inopblock;
		nbufs = XFS_IALLOC_BLOCKS(mp);
	} else {
		blks_per_cluster = XFS_INODE_CLUSTER_SIZE(mp) /
					mp->m_sb.sb_blocksize;
		ninodes = blks_per_cluster * mp->m_sb.sb_inopblock;
		nbufs = XFS_IALLOC_BLOCKS(mp) / blks_per_cluster;
	}

	ip_found = kmem_alloc(ninodes * sizeof(xfs_inode_t *), KM_NOFS);

	for (j = 0; j < nbufs; j++, inum += ninodes) {
		blkno = XFS_AGB_TO_DADDR(mp, XFS_INO_TO_AGNO(mp, inum),
					 XFS_INO_TO_AGBNO(mp, inum));


		/*
		 * Look for each inode in memory and attempt to lock it,
		 * we can be racing with flush and tail pushing here.
		 * any inode we get the locks on, add to an array of
		 * inode items to process later.
		 *
		 * The get the buffer lock, we could beat a flush
		 * or tail pushing thread to the lock here, in which
		 * case they will go looking for the inode buffer
		 * and fail, we need some other form of interlock
		 * here.
		 */
		found = 0;
		for (i = 0; i < ninodes; i++) {
			read_lock(&pag->pag_ici_lock);
			ip = radix_tree_lookup(&pag->pag_ici_root,
					XFS_INO_TO_AGINO(mp, (inum + i)));

			/* Inode not in memory or we found it already,
			 * nothing to do
			 */
			if (!ip || xfs_iflags_test(ip, XFS_ISTALE)) {
				read_unlock(&pag->pag_ici_lock);
				continue;
			}

			if (xfs_inode_clean(ip)) {
				read_unlock(&pag->pag_ici_lock);
				continue;
			}

			/* If we can get the locks then add it to the
			 * list, otherwise by the time we get the bp lock
			 * below it will already be attached to the
			 * inode buffer.
			 */

			/* This inode will already be locked - by us, lets
			 * keep it that way.
			 */

			if (ip == free_ip) {
				if (xfs_iflock_nowait(ip)) {
					xfs_iflags_set(ip, XFS_ISTALE);
					if (xfs_inode_clean(ip)) {
						xfs_ifunlock(ip);
					} else {
						ip_found[found++] = ip;
					}
				}
				read_unlock(&pag->pag_ici_lock);
				continue;
			}

			if (xfs_ilock_nowait(ip, XFS_ILOCK_EXCL)) {
				if (xfs_iflock_nowait(ip)) {
					xfs_iflags_set(ip, XFS_ISTALE);

					if (xfs_inode_clean(ip)) {
						xfs_ifunlock(ip);
						xfs_iunlock(ip, XFS_ILOCK_EXCL);
					} else {
						ip_found[found++] = ip;
					}
				} else {
					xfs_iunlock(ip, XFS_ILOCK_EXCL);
				}
			}
			read_unlock(&pag->pag_ici_lock);
		}

		bp = xfs_trans_get_buf(tp, mp->m_ddev_targp, blkno, 
					mp->m_bsize * blks_per_cluster,
					XFS_BUF_LOCK);

		pre_flushed = 0;
		lip = XFS_BUF_FSPRIVATE(bp, xfs_log_item_t *);
/*
 * A big issue when freeing the inode cluster is that we _cannot_ skip any
 * inodes that are in memory - they all must be marked stale and attached to
 * the cluster buffer.
 */
STATIC int
xfs_ifree_cluster(
	xfs_inode_t		*free_ip,
	xfs_trans_t		*tp,
	struct xfs_icluster	*xic)
{
	xfs_mount_t		*mp = free_ip->i_mount;
	int			blks_per_cluster;
	int			inodes_per_cluster;
	int			nbufs;
	int			i, j;
	int			ioffset;
	xfs_daddr_t		blkno;
	xfs_buf_t		*bp;
	xfs_inode_t		*ip;
	xfs_inode_log_item_t	*iip;
	xfs_log_item_t		*lip;
	struct xfs_perag	*pag;
	xfs_ino_t		inum;

	inum = xic->first_ino;
	pag = xfs_perag_get(mp, XFS_INO_TO_AGNO(mp, inum));
	blks_per_cluster = xfs_icluster_size_fsb(mp);
	inodes_per_cluster = blks_per_cluster << mp->m_sb.sb_inopblog;
	nbufs = mp->m_ialloc_blks / blks_per_cluster;

	for (j = 0; j < nbufs; j++, inum += inodes_per_cluster) {
		/*
		 * The allocation bitmap tells us which inodes of the chunk were
		 * physically allocated. Skip the cluster if an inode falls into
		 * a sparse region.
		 */
		ioffset = inum - xic->first_ino;
		if ((xic->alloc & XFS_INOBT_MASK(ioffset)) == 0) {
			ASSERT(do_mod(ioffset, inodes_per_cluster) == 0);
			continue;
		}

		blkno = XFS_AGB_TO_DADDR(mp, XFS_INO_TO_AGNO(mp, inum),
					 XFS_INO_TO_AGBNO(mp, inum));

		/*
		 * We obtain and lock the backing buffer first in the process
		 * here, as we have to ensure that any dirty inode that we
		 * can't get the flush lock on is attached to the buffer.
		 * If we scan the in-memory inodes first, then buffer IO can
		 * complete before we get a lock on it, and hence we may fail
		 * to mark all the active inodes on the buffer stale.
		 */
		bp = xfs_trans_get_buf(tp, mp->m_ddev_targp, blkno,
					mp->m_bsize * blks_per_cluster,
					XBF_UNMAPPED);

		if (!bp)
			return -ENOMEM;

		/*
		 * This buffer may not have been correctly initialised as we
		 * didn't read it from disk. That's not important because we are
		 * only using to mark the buffer as stale in the log, and to
		 * attach stale cached inodes on it. That means it will never be
		 * dispatched for IO. If it is, we want to know about it, and we
		 * want it to fail. We can acheive this by adding a write
		 * verifier to the buffer.
		 */
		 bp->b_ops = &xfs_inode_buf_ops;

		/*
		 * Walk the inodes already attached to the buffer and mark them
		 * stale. These will all have the flush locks held, so an
		 * in-memory inode walk can't lock them. By marking them all
		 * stale first, we will not attempt to lock them in the loop
		 * below as the XFS_ISTALE flag will be set.
		 */
		lip = bp->b_fspriv;
		while (lip) {
			if (lip->li_type == XFS_LI_INODE) {
				iip = (xfs_inode_log_item_t *)lip;
				ASSERT(iip->ili_logged == 1);
				lip->li_cb = (void(*)(xfs_buf_t*,xfs_log_item_t*)) xfs_istale_done;
				spin_lock(&mp->m_ail_lock);
				iip->ili_flush_lsn = iip->ili_item.li_lsn;
				spin_unlock(&mp->m_ail_lock);
				xfs_iflags_set(iip->ili_inode, XFS_ISTALE);
				pre_flushed++;
				lip->li_cb = xfs_istale_done;
				xfs_trans_ail_copy_lsn(mp->m_ail,
							&iip->ili_flush_lsn,
							&iip->ili_item.li_lsn);
				xfs_iflags_set(iip->ili_inode, XFS_ISTALE);
			}
			lip = lip->li_bio_list;
		}

		for (i = 0; i < found; i++) {
			ip = ip_found[i];
			iip = ip->i_itemp;

			if (!iip) {
				ip->i_update_core = 0;

		/*
		 * For each inode in memory attempt to add it to the inode
		 * buffer and set it up for being staled on buffer IO
		 * completion.  This is safe as we've locked out tail pushing
		 * and flushing by locking the buffer.
		 *
		 * We have already marked every inode that was part of a
		 * transaction stale above, which means there is no point in
		 * even trying to lock them.
		 */
		for (i = 0; i < inodes_per_cluster; i++) {
retry:
			rcu_read_lock();
			ip = radix_tree_lookup(&pag->pag_ici_root,
					XFS_INO_TO_AGINO(mp, (inum + i)));

			/* Inode not in memory, nothing to do */
			if (!ip) {
				rcu_read_unlock();
				continue;
			}

			/*
			 * because this is an RCU protected lookup, we could
			 * find a recently freed or even reallocated inode
			 * during the lookup. We need to check under the
			 * i_flags_lock for a valid inode here. Skip it if it
			 * is not valid, the wrong inode or stale.
			 */
			spin_lock(&ip->i_flags_lock);
			if (ip->i_ino != inum + i ||
			    __xfs_iflags_test(ip, XFS_ISTALE)) {
				spin_unlock(&ip->i_flags_lock);
				rcu_read_unlock();
				continue;
			}
			spin_unlock(&ip->i_flags_lock);

			/*
			 * Don't try to lock/unlock the current inode, but we
			 * _cannot_ skip the other inodes that we did not find
			 * in the list attached to the buffer and are not
			 * already marked stale. If we can't lock it, back off
			 * and retry.
			 */
			if (ip != free_ip &&
			    !xfs_ilock_nowait(ip, XFS_ILOCK_EXCL)) {
				rcu_read_unlock();
				delay(1);
				goto retry;
			}
			rcu_read_unlock();

			xfs_iflock(ip);
			xfs_iflags_set(ip, XFS_ISTALE);

			/*
			 * we don't need to attach clean inodes or those only
			 * with unlogged changes (which we throw away, anyway).
			 */
			iip = ip->i_itemp;
			if (!iip || xfs_inode_clean(ip)) {
				ASSERT(ip != free_ip);
				xfs_ifunlock(ip);
				xfs_iunlock(ip, XFS_ILOCK_EXCL);
				continue;
			}

			iip->ili_last_fields = iip->ili_format.ilf_fields;
			iip->ili_format.ilf_fields = 0;
			iip->ili_logged = 1;
			spin_lock(&mp->m_ail_lock);
			iip->ili_flush_lsn = iip->ili_item.li_lsn;
			spin_unlock(&mp->m_ail_lock);

			xfs_buf_attach_iodone(bp,
				(void(*)(xfs_buf_t*,xfs_log_item_t*))
				xfs_istale_done, (xfs_log_item_t *)iip);
			if (ip != free_ip) {
				xfs_iunlock(ip, XFS_ILOCK_EXCL);
			}
		}

		if (found || pre_flushed)
			xfs_trans_stale_inode_buf(tp, bp);
		xfs_trans_binval(tp, bp);
	}

	kmem_free(ip_found);
	xfs_put_perag(mp, pag);
			iip->ili_last_fields = iip->ili_fields;
			iip->ili_fields = 0;
			iip->ili_fsync_fields = 0;
			iip->ili_logged = 1;
			xfs_trans_ail_copy_lsn(mp->m_ail, &iip->ili_flush_lsn,
						&iip->ili_item.li_lsn);

			xfs_buf_attach_iodone(bp, xfs_istale_done,
						  &iip->ili_item);

			if (ip != free_ip)
				xfs_iunlock(ip, XFS_ILOCK_EXCL);
		}

		xfs_trans_stale_inode_buf(tp, bp);
		xfs_trans_binval(tp, bp);
	}

	xfs_perag_put(pag);
	return 0;
}

/*
 * This is called to return an inode to the inode free list.
 * The inode should already be truncated to 0 length and have
 * no pages associated with it.  This routine also assumes that
 * the inode is already a part of the transaction.
 *
 * The on-disk copy of the inode will have been added to the list
 * of unlinked inodes in the AGI. We need to remove the inode from
 * that list atomically with respect to freeing it here.
 */
int
xfs_ifree(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip,
	xfs_bmap_free_t	*flist)
{
	int			error;
	int			delete;
	xfs_ino_t		first_ino;
	xfs_dinode_t    	*dip;
	xfs_buf_t       	*ibp;

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));
	ASSERT(ip->i_transp == tp);
	ASSERT(ip->i_d.di_nlink == 0);
	ASSERT(ip->i_d.di_nextents == 0);
	ASSERT(ip->i_d.di_anextents == 0);
	ASSERT((ip->i_d.di_size == 0 && ip->i_size == 0) ||
	       ((ip->i_d.di_mode & S_IFMT) != S_IFREG));
	struct xfs_icluster	xic = { 0 };

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));
	ASSERT(ip->i_d.di_nlink == 0);
	ASSERT(ip->i_d.di_nextents == 0);
	ASSERT(ip->i_d.di_anextents == 0);
	ASSERT(ip->i_d.di_size == 0 || !S_ISREG(ip->i_d.di_mode));
	ASSERT(ip->i_d.di_nblocks == 0);

	/*
	 * Pull the on-disk inode from the AGI unlinked list.
	 */
	error = xfs_iunlink_remove(tp, ip);
	if (error != 0) {
		return error;
	}

	error = xfs_difree(tp, ip->i_ino, flist, &delete, &first_ino);
	if (error != 0) {
		return error;
	}
	if (error)
		return error;

	error = xfs_difree(tp, ip->i_ino, flist, &xic);
	if (error)
		return error;

	ip->i_d.di_mode = 0;		/* mark incore inode as free */
	ip->i_d.di_flags = 0;
	ip->i_d.di_dmevmask = 0;
	ip->i_d.di_forkoff = 0;		/* mark the attr fork not in use */
	ip->i_df.if_ext_max =
		XFS_IFORK_DSIZE(ip) / (uint)sizeof(xfs_bmbt_rec_t);
	ip->i_d.di_format = XFS_DINODE_FMT_EXTENTS;
	ip->i_d.di_aformat = XFS_DINODE_FMT_EXTENTS;
	/*
	 * Bump the generation count so no one will be confused
	 * by reincarnations of this inode.
	 */
	ip->i_d.di_gen++;

	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);

	error = xfs_itobp(ip->i_mount, tp, ip, &dip, &ibp, 0, 0, XFS_BUF_LOCK);
	if (error)
		return error;

        /*
	* Clear the on-disk di_mode. This is to prevent xfs_bulkstat
	* from picking up this inode when it is reclaimed (its incore state
	* initialzed but not flushed to disk yet). The in-core di_mode is
	* already cleared  and a corresponding transaction logged.
	* The hack here just synchronizes the in-core to on-disk
	* di_mode value in advance before the actual inode sync to disk.
	* This is OK because the inode is already unlinked and would never
	* change its di_mode again for this inode generation.
	* This is a temporary hack that would require a proper fix
	* in the future.
	*/
	dip->di_core.di_mode = 0;

	if (delete) {
		xfs_ifree_cluster(ip, tp, first_ino);
	}

	return 0;
}

/*
 * Reallocate the space for if_broot based on the number of records
 * being added or deleted as indicated in rec_diff.  Move the records
 * and pointers in if_broot to fit the new size.  When shrinking this
 * will eliminate holes between the records and pointers created by
 * the caller.  When growing this will create holes to be filled in
 * by the caller.
 *
 * The caller must not request to add more records than would fit in
 * the on-disk inode root.  If the if_broot is currently NULL, then
 * if we adding records one will be allocated.  The caller must also
 * not request that the number of records go below zero, although
 * it can go to zero.
 *
 * ip -- the inode whose if_broot area is changing
 * ext_diff -- the change in the number of records, positive or negative,
 *	 requested for the if_broot array.
 */
void
xfs_iroot_realloc(
	xfs_inode_t		*ip,
	int			rec_diff,
	int			whichfork)
{
	int			cur_max;
	xfs_ifork_t		*ifp;
	xfs_bmbt_block_t	*new_broot;
	int			new_max;
	size_t			new_size;
	char			*np;
	char			*op;

	/*
	 * Handle the degenerate case quietly.
	 */
	if (rec_diff == 0) {
		return;
	}

	ifp = XFS_IFORK_PTR(ip, whichfork);
	if (rec_diff > 0) {
		/*
		 * If there wasn't any memory allocated before, just
		 * allocate it now and get out.
		 */
		if (ifp->if_broot_bytes == 0) {
			new_size = (size_t)XFS_BMAP_BROOT_SPACE_CALC(rec_diff);
			ifp->if_broot = (xfs_bmbt_block_t*)kmem_alloc(new_size,
								     KM_SLEEP);
			ifp->if_broot_bytes = (int)new_size;
			return;
		}

		/*
		 * If there is already an existing if_broot, then we need
		 * to realloc() it and shift the pointers to their new
		 * location.  The records don't change location because
		 * they are kept butted up against the btree block header.
		 */
		cur_max = XFS_BMAP_BROOT_MAXRECS(ifp->if_broot_bytes);
		new_max = cur_max + rec_diff;
		new_size = (size_t)XFS_BMAP_BROOT_SPACE_CALC(new_max);
		ifp->if_broot = (xfs_bmbt_block_t *)
		  kmem_realloc(ifp->if_broot,
				new_size,
				(size_t)XFS_BMAP_BROOT_SPACE_CALC(cur_max), /* old size */
				KM_SLEEP);
		op = (char *)XFS_BMAP_BROOT_PTR_ADDR(ifp->if_broot, 1,
						      ifp->if_broot_bytes);
		np = (char *)XFS_BMAP_BROOT_PTR_ADDR(ifp->if_broot, 1,
						      (int)new_size);
		ifp->if_broot_bytes = (int)new_size;
		ASSERT(ifp->if_broot_bytes <=
			XFS_IFORK_SIZE(ip, whichfork) + XFS_BROOT_SIZE_ADJ);
		memmove(np, op, cur_max * (uint)sizeof(xfs_dfsbno_t));
		return;
	}

	/*
	 * rec_diff is less than 0.  In this case, we are shrinking the
	 * if_broot buffer.  It must already exist.  If we go to zero
	 * records, just get rid of the root and clear the status bit.
	 */
	ASSERT((ifp->if_broot != NULL) && (ifp->if_broot_bytes > 0));
	cur_max = XFS_BMAP_BROOT_MAXRECS(ifp->if_broot_bytes);
	new_max = cur_max + rec_diff;
	ASSERT(new_max >= 0);
	if (new_max > 0)
		new_size = (size_t)XFS_BMAP_BROOT_SPACE_CALC(new_max);
	else
		new_size = 0;
	if (new_size > 0) {
		new_broot = (xfs_bmbt_block_t *)kmem_alloc(new_size, KM_SLEEP);
		/*
		 * First copy over the btree block header.
		 */
		memcpy(new_broot, ifp->if_broot, sizeof(xfs_bmbt_block_t));
	} else {
		new_broot = NULL;
		ifp->if_flags &= ~XFS_IFBROOT;
	}

	/*
	 * Only copy the records and pointers if there are any.
	 */
	if (new_max > 0) {
		/*
		 * First copy the records.
		 */
		op = (char *)XFS_BMAP_BROOT_REC_ADDR(ifp->if_broot, 1,
						     ifp->if_broot_bytes);
		np = (char *)XFS_BMAP_BROOT_REC_ADDR(new_broot, 1,
						     (int)new_size);
		memcpy(np, op, new_max * (uint)sizeof(xfs_bmbt_rec_t));

		/*
		 * Then copy the pointers.
		 */
		op = (char *)XFS_BMAP_BROOT_PTR_ADDR(ifp->if_broot, 1,
						     ifp->if_broot_bytes);
		np = (char *)XFS_BMAP_BROOT_PTR_ADDR(new_broot, 1,
						     (int)new_size);
		memcpy(np, op, new_max * (uint)sizeof(xfs_dfsbno_t));
	}
	kmem_free(ifp->if_broot);
	ifp->if_broot = new_broot;
	ifp->if_broot_bytes = (int)new_size;
	ASSERT(ifp->if_broot_bytes <=
		XFS_IFORK_SIZE(ip, whichfork) + XFS_BROOT_SIZE_ADJ);
	return;
}


/*
 * This is called when the amount of space needed for if_data
 * is increased or decreased.  The change in size is indicated by
 * the number of bytes that need to be added or deleted in the
 * byte_diff parameter.
 *
 * If the amount of space needed has decreased below the size of the
 * inline buffer, then switch to using the inline buffer.  Otherwise,
 * use kmem_realloc() or kmem_alloc() to adjust the size of the buffer
 * to what is needed.
 *
 * ip -- the inode whose if_data area is changing
 * byte_diff -- the change in the number of bytes, positive or negative,
 *	 requested for the if_data array.
 */
void
xfs_idata_realloc(
	xfs_inode_t	*ip,
	int		byte_diff,
	int		whichfork)
{
	xfs_ifork_t	*ifp;
	int		new_size;
	int		real_size;

	if (byte_diff == 0) {
		return;
	}

	ifp = XFS_IFORK_PTR(ip, whichfork);
	new_size = (int)ifp->if_bytes + byte_diff;
	ASSERT(new_size >= 0);

	if (new_size == 0) {
		if (ifp->if_u1.if_data != ifp->if_u2.if_inline_data) {
			kmem_free(ifp->if_u1.if_data);
		}
		ifp->if_u1.if_data = NULL;
		real_size = 0;
	} else if (new_size <= sizeof(ifp->if_u2.if_inline_data)) {
		/*
		 * If the valid extents/data can fit in if_inline_ext/data,
		 * copy them from the malloc'd vector and free it.
		 */
		if (ifp->if_u1.if_data == NULL) {
			ifp->if_u1.if_data = ifp->if_u2.if_inline_data;
		} else if (ifp->if_u1.if_data != ifp->if_u2.if_inline_data) {
			ASSERT(ifp->if_real_bytes != 0);
			memcpy(ifp->if_u2.if_inline_data, ifp->if_u1.if_data,
			      new_size);
			kmem_free(ifp->if_u1.if_data);
			ifp->if_u1.if_data = ifp->if_u2.if_inline_data;
		}
		real_size = 0;
	} else {
		/*
		 * Stuck with malloc/realloc.
		 * For inline data, the underlying buffer must be
		 * a multiple of 4 bytes in size so that it can be
		 * logged and stay on word boundaries.  We enforce
		 * that here.
		 */
		real_size = roundup(new_size, 4);
		if (ifp->if_u1.if_data == NULL) {
			ASSERT(ifp->if_real_bytes == 0);
			ifp->if_u1.if_data = kmem_alloc(real_size, KM_SLEEP);
		} else if (ifp->if_u1.if_data != ifp->if_u2.if_inline_data) {
			/*
			 * Only do the realloc if the underlying size
			 * is really changing.
			 */
			if (ifp->if_real_bytes != real_size) {
				ifp->if_u1.if_data =
					kmem_realloc(ifp->if_u1.if_data,
							real_size,
							ifp->if_real_bytes,
							KM_SLEEP);
			}
		} else {
			ASSERT(ifp->if_real_bytes == 0);
			ifp->if_u1.if_data = kmem_alloc(real_size, KM_SLEEP);
			memcpy(ifp->if_u1.if_data, ifp->if_u2.if_inline_data,
				ifp->if_bytes);
		}
	}
	ifp->if_real_bytes = real_size;
	ifp->if_bytes = new_size;
	ASSERT(ifp->if_bytes <= XFS_IFORK_SIZE(ip, whichfork));
}




/*
 * Map inode to disk block and offset.
 *
 * mp -- the mount point structure for the current file system
 * tp -- the current transaction
 * ino -- the inode number of the inode to be located
 * imap -- this structure is filled in with the information necessary
 *	 to retrieve the given inode from disk
 * flags -- flags to pass to xfs_dilocate indicating whether or not
 *	 lookups in the inode btree were OK or not
 */
int
xfs_imap(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_ino_t	ino,
	xfs_imap_t	*imap,
	uint		flags)
{
	xfs_fsblock_t	fsbno;
	int		len;
	int		off;
	int		error;

	fsbno = imap->im_blkno ?
		XFS_DADDR_TO_FSB(mp, imap->im_blkno) : NULLFSBLOCK;
	error = xfs_dilocate(mp, tp, ino, &fsbno, &len, &off, flags);
	if (error)
		return error;

	imap->im_blkno = XFS_FSB_TO_DADDR(mp, fsbno);
	imap->im_len = XFS_FSB_TO_BB(mp, len);
	imap->im_agblkno = XFS_FSB_TO_AGBNO(mp, fsbno);
	imap->im_ioffset = (ushort)off;
	imap->im_boffset = (ushort)(off << mp->m_sb.sb_inodelog);

	/*
	 * If the inode number maps to a block outside the bounds
	 * of the file system then return NULL rather than calling
	 * read_buf and panicing when we get an error from the
	 * driver.
	 */
	if ((imap->im_blkno + imap->im_len) >
	    XFS_FSB_TO_BB(mp, mp->m_sb.sb_dblocks)) {
		xfs_fs_cmn_err(CE_ALERT, mp, "xfs_imap: "
			"(imap->im_blkno (0x%llx) + imap->im_len (0x%llx)) > "
			" XFS_FSB_TO_BB(mp, mp->m_sb.sb_dblocks) (0x%llx)",
			(unsigned long long) imap->im_blkno,
			(unsigned long long) imap->im_len,
			XFS_FSB_TO_BB(mp, mp->m_sb.sb_dblocks));
		return EINVAL;
	}
	return 0;
}

void
xfs_idestroy_fork(
	xfs_inode_t	*ip,
	int		whichfork)
{
	xfs_ifork_t	*ifp;

	ifp = XFS_IFORK_PTR(ip, whichfork);
	if (ifp->if_broot != NULL) {
		kmem_free(ifp->if_broot);
		ifp->if_broot = NULL;
	}

	/*
	 * If the format is local, then we can't have an extents
	 * array so just look for an inline data array.  If we're
	 * not local then we may or may not have an extents list,
	 * so check and free it up if we do.
	 */
	if (XFS_IFORK_FORMAT(ip, whichfork) == XFS_DINODE_FMT_LOCAL) {
		if ((ifp->if_u1.if_data != ifp->if_u2.if_inline_data) &&
		    (ifp->if_u1.if_data != NULL)) {
			ASSERT(ifp->if_real_bytes != 0);
			kmem_free(ifp->if_u1.if_data);
			ifp->if_u1.if_data = NULL;
			ifp->if_real_bytes = 0;
		}
	} else if ((ifp->if_flags & XFS_IFEXTENTS) &&
		   ((ifp->if_flags & XFS_IFEXTIREC) ||
		    ((ifp->if_u1.if_extents != NULL) &&
		     (ifp->if_u1.if_extents != ifp->if_u2.if_inline_ext)))) {
		ASSERT(ifp->if_real_bytes != 0);
		xfs_iext_destroy(ifp);
	}
	ASSERT(ifp->if_u1.if_extents == NULL ||
	       ifp->if_u1.if_extents == ifp->if_u2.if_inline_ext);
	ASSERT(ifp->if_real_bytes == 0);
	if (whichfork == XFS_ATTR_FORK) {
		kmem_zone_free(xfs_ifork_zone, ip->i_afp);
		ip->i_afp = NULL;
	}
}

/*
 * This is called free all the memory associated with an inode.
 * It must free the inode itself and any buffers allocated for
 * if_extents/if_data and if_broot.  It must also free the lock
 * associated with the inode.
 */
void
xfs_idestroy(
	xfs_inode_t	*ip)
{
	switch (ip->i_d.di_mode & S_IFMT) {
	case S_IFREG:
	case S_IFDIR:
	case S_IFLNK:
		xfs_idestroy_fork(ip, XFS_DATA_FORK);
		break;
	}
	if (ip->i_afp)
		xfs_idestroy_fork(ip, XFS_ATTR_FORK);
	mrfree(&ip->i_lock);
	mrfree(&ip->i_iolock);

#ifdef XFS_INODE_TRACE
	ktrace_free(ip->i_trace);
#endif
#ifdef XFS_BMAP_TRACE
	ktrace_free(ip->i_xtrace);
#endif
#ifdef XFS_BMBT_TRACE
	ktrace_free(ip->i_btrace);
#endif
#ifdef XFS_RW_TRACE
	ktrace_free(ip->i_rwtrace);
#endif
#ifdef XFS_ILOCK_TRACE
	ktrace_free(ip->i_lock_trace);
#endif
#ifdef XFS_DIR2_TRACE
	ktrace_free(ip->i_dir_trace);
#endif
	if (ip->i_itemp) {
		/*
		 * Only if we are shutting down the fs will we see an
		 * inode still in the AIL. If it is there, we should remove
		 * it to prevent a use-after-free from occurring.
		 */
		xfs_mount_t	*mp = ip->i_mount;
		xfs_log_item_t	*lip = &ip->i_itemp->ili_item;

		ASSERT(((lip->li_flags & XFS_LI_IN_AIL) == 0) ||
				       XFS_FORCED_SHUTDOWN(ip->i_mount));
		if (lip->li_flags & XFS_LI_IN_AIL) {
			spin_lock(&mp->m_ail_lock);
			if (lip->li_flags & XFS_LI_IN_AIL)
				xfs_trans_delete_ail(mp, lip);
			else
				spin_unlock(&mp->m_ail_lock);
		}
		xfs_inode_item_destroy(ip);
	}
	kmem_zone_free(xfs_inode_zone, ip);
}


/*
 * Increment the pin count of the given buffer.
 * This value is protected by ipinlock spinlock in the mount structure.
 */
void
xfs_ipin(
	xfs_inode_t	*ip)
{
	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));

	atomic_inc(&ip->i_pincount);
}

/*
 * Decrement the pin count of the given inode, and wake up
 * anyone in xfs_iwait_unpin() if the count goes to 0.  The
 * inode must have been previously pinned with a call to xfs_ipin().
 */
void
xfs_iunpin(
	xfs_inode_t	*ip)
{
	ASSERT(atomic_read(&ip->i_pincount) > 0);

	if (atomic_dec_and_test(&ip->i_pincount))
		wake_up(&ip->i_ipin_wait);
}

/*
 * This is called to unpin an inode. It can be directed to wait or to return
 * immediately without waiting for the inode to be unpinned.  The caller must
 * have the inode locked in at least shared mode so that the buffer cannot be
 * subsequently pinned once someone is waiting for it to be unpinned.
 */
STATIC void
__xfs_iunpin_wait(
	xfs_inode_t	*ip,
	int		wait)
{
	xfs_inode_log_item_t	*iip = ip->i_itemp;

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL|XFS_ILOCK_SHARED));
	if (atomic_read(&ip->i_pincount) == 0)
		return;

	/* Give the log a push to start the unpinning I/O */
	xfs_log_force(ip->i_mount, (iip && iip->ili_last_lsn) ?
				iip->ili_last_lsn : 0, XFS_LOG_FORCE);
	if (wait)
		wait_event(ip->i_ipin_wait, (atomic_read(&ip->i_pincount) == 0));
}

static inline void
xfs_iunpin_wait(
	xfs_inode_t	*ip)
{
	__xfs_iunpin_wait(ip, 1);
}

static inline void
xfs_iunpin_nowait(
	xfs_inode_t	*ip)
{
	__xfs_iunpin_wait(ip, 0);
}


/*
 * xfs_iextents_copy()
 *
 * This is called to copy the REAL extents (as opposed to the delayed
 * allocation extents) from the inode into the given buffer.  It
 * returns the number of bytes copied into the buffer.
 *
 * If there are no delayed allocation extents, then we can just
 * memcpy() the extents into the buffer.  Otherwise, we need to
 * examine each extent in turn and skip those which are delayed.
 */
int
xfs_iextents_copy(
	xfs_inode_t		*ip,
	xfs_bmbt_rec_t		*dp,
	int			whichfork)
{
	int			copied;
	int			i;
	xfs_ifork_t		*ifp;
	int			nrecs;
	xfs_fsblock_t		start_block;

	ifp = XFS_IFORK_PTR(ip, whichfork);
	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL|XFS_ILOCK_SHARED));
	ASSERT(ifp->if_bytes > 0);

	nrecs = ifp->if_bytes / (uint)sizeof(xfs_bmbt_rec_t);
	XFS_BMAP_TRACE_EXLIST(ip, nrecs, whichfork);
	ASSERT(nrecs > 0);

	/*
	 * There are some delayed allocation extents in the
	 * inode, so copy the extents one at a time and skip
	 * the delayed ones.  There must be at least one
	 * non-delayed extent.
	 */
	copied = 0;
	for (i = 0; i < nrecs; i++) {
		xfs_bmbt_rec_host_t *ep = xfs_iext_get_ext(ifp, i);
		start_block = xfs_bmbt_get_startblock(ep);
		if (ISNULLSTARTBLOCK(start_block)) {
			/*
			 * It's a delayed allocation extent, so skip it.
			 */
			continue;
		}

		/* Translate to on disk format */
		put_unaligned(cpu_to_be64(ep->l0), &dp->l0);
		put_unaligned(cpu_to_be64(ep->l1), &dp->l1);
		dp++;
		copied++;
	}
	ASSERT(copied != 0);
	xfs_validate_extents(ifp, copied, XFS_EXTFMT_INODE(ip));

	return (copied * (uint)sizeof(xfs_bmbt_rec_t));
}

/*
 * Each of the following cases stores data into the same region
 * of the on-disk inode, so only one of them can be valid at
 * any given time. While it is possible to have conflicting formats
 * and log flags, e.g. having XFS_ILOG_?DATA set when the fork is
 * in EXTENTS format, this can only happen when the fork has
 * changed formats after being modified but before being flushed.
 * In these cases, the format always takes precedence, because the
 * format indicates the current state of the fork.
 */
/*ARGSUSED*/
STATIC void
xfs_iflush_fork(
	xfs_inode_t		*ip,
	xfs_dinode_t		*dip,
	xfs_inode_log_item_t	*iip,
	int			whichfork,
	xfs_buf_t		*bp)
{
	char			*cp;
	xfs_ifork_t		*ifp;
	xfs_mount_t		*mp;
#ifdef XFS_TRANS_DEBUG
	int			first;
#endif
	static const short	brootflag[2] =
		{ XFS_ILOG_DBROOT, XFS_ILOG_ABROOT };
	static const short	dataflag[2] =
		{ XFS_ILOG_DDATA, XFS_ILOG_ADATA };
	static const short	extflag[2] =
		{ XFS_ILOG_DEXT, XFS_ILOG_AEXT };

	if (!iip)
		return;
	ifp = XFS_IFORK_PTR(ip, whichfork);
	/*
	 * This can happen if we gave up in iformat in an error path,
	 * for the attribute fork.
	 */
	if (!ifp) {
		ASSERT(whichfork == XFS_ATTR_FORK);
		return;
	}
	cp = XFS_DFORK_PTR(dip, whichfork);
	mp = ip->i_mount;
	switch (XFS_IFORK_FORMAT(ip, whichfork)) {
	case XFS_DINODE_FMT_LOCAL:
		if ((iip->ili_format.ilf_fields & dataflag[whichfork]) &&
		    (ifp->if_bytes > 0)) {
			ASSERT(ifp->if_u1.if_data != NULL);
			ASSERT(ifp->if_bytes <= XFS_IFORK_SIZE(ip, whichfork));
			memcpy(cp, ifp->if_u1.if_data, ifp->if_bytes);
		}
		break;

	case XFS_DINODE_FMT_EXTENTS:
		ASSERT((ifp->if_flags & XFS_IFEXTENTS) ||
		       !(iip->ili_format.ilf_fields & extflag[whichfork]));
		ASSERT((xfs_iext_get_ext(ifp, 0) != NULL) ||
			(ifp->if_bytes == 0));
		ASSERT((xfs_iext_get_ext(ifp, 0) == NULL) ||
			(ifp->if_bytes > 0));
		if ((iip->ili_format.ilf_fields & extflag[whichfork]) &&
		    (ifp->if_bytes > 0)) {
			ASSERT(XFS_IFORK_NEXTENTS(ip, whichfork) > 0);
			(void)xfs_iextents_copy(ip, (xfs_bmbt_rec_t *)cp,
				whichfork);
		}
		break;

	case XFS_DINODE_FMT_BTREE:
		if ((iip->ili_format.ilf_fields & brootflag[whichfork]) &&
		    (ifp->if_broot_bytes > 0)) {
			ASSERT(ifp->if_broot != NULL);
			ASSERT(ifp->if_broot_bytes <=
			       (XFS_IFORK_SIZE(ip, whichfork) +
				XFS_BROOT_SIZE_ADJ));
			xfs_bmbt_to_bmdr(ifp->if_broot, ifp->if_broot_bytes,
				(xfs_bmdr_block_t *)cp,
				XFS_DFORK_SIZE(dip, mp, whichfork));
		}
		break;

	case XFS_DINODE_FMT_DEV:
		if (iip->ili_format.ilf_fields & XFS_ILOG_DEV) {
			ASSERT(whichfork == XFS_DATA_FORK);
			dip->di_u.di_dev = cpu_to_be32(ip->i_df.if_u2.if_rdev);
		}
		break;

	case XFS_DINODE_FMT_UUID:
		if (iip->ili_format.ilf_fields & XFS_ILOG_UUID) {
			ASSERT(whichfork == XFS_DATA_FORK);
			memcpy(&dip->di_u.di_muuid, &ip->i_df.if_u2.if_uuid,
				sizeof(uuid_t));
		}
		break;

	default:
		ASSERT(0);
		break;
	}
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);

	if (xic.deleted)
		error = xfs_ifree_cluster(ip, tp, &xic);

	return error;
}

/*
 * This is called to unpin an inode.  The caller must have the inode locked
 * in at least shared mode so that the buffer cannot be subsequently pinned
 * once someone is waiting for it to be unpinned.
 */
static void
xfs_iunpin(
	struct xfs_inode	*ip)
{
	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL|XFS_ILOCK_SHARED));

	trace_xfs_inode_unpin_nowait(ip, _RET_IP_);

	/* Give the log a push to start the unpinning I/O */
	xfs_log_force_lsn(ip->i_mount, ip->i_itemp->ili_last_lsn, 0);

}

static void
__xfs_iunpin_wait(
	struct xfs_inode	*ip)
{
	wait_queue_head_t *wq = bit_waitqueue(&ip->i_flags, __XFS_IPINNED_BIT);
	DEFINE_WAIT_BIT(wait, &ip->i_flags, __XFS_IPINNED_BIT);

	xfs_iunpin(ip);

	do {
		prepare_to_wait(wq, &wait.wait, TASK_UNINTERRUPTIBLE);
		if (xfs_ipincount(ip))
			io_schedule();
	} while (xfs_ipincount(ip));
	finish_wait(wq, &wait.wait);
}

void
xfs_iunpin_wait(
	struct xfs_inode	*ip)
{
	if (xfs_ipincount(ip))
		__xfs_iunpin_wait(ip);
}

/*
 * Removing an inode from the namespace involves removing the directory entry
 * and dropping the link count on the inode. Removing the directory entry can
 * result in locking an AGF (directory blocks were freed) and removing a link
 * count can result in placing the inode on an unlinked list which results in
 * locking an AGI.
 *
 * The big problem here is that we have an ordering constraint on AGF and AGI
 * locking - inode allocation locks the AGI, then can allocate a new extent for
 * new inodes, locking the AGF after the AGI. Similarly, freeing the inode
 * removes the inode from the unlinked list, requiring that we lock the AGI
 * first, and then freeing the inode can result in an inode chunk being freed
 * and hence freeing disk space requiring that we lock an AGF.
 *
 * Hence the ordering that is imposed by other parts of the code is AGI before
 * AGF. This means we cannot remove the directory entry before we drop the inode
 * reference count and put it on the unlinked list as this results in a lock
 * order of AGF then AGI, and this can deadlock against inode allocation and
 * freeing. Therefore we must drop the link counts before we remove the
 * directory entry.
 *
 * This is still safe from a transactional point of view - it is not until we
 * get to xfs_bmap_finish() that we have the possibility of multiple
 * transactions in this operation. Hence as long as we remove the directory
 * entry and drop the link count in the first transaction of the remove
 * operation, there are no transactional constraints on the ordering here.
 */
int
xfs_remove(
	xfs_inode_t             *dp,
	struct xfs_name		*name,
	xfs_inode_t		*ip)
{
	xfs_mount_t		*mp = dp->i_mount;
	xfs_trans_t             *tp = NULL;
	int			is_dir = S_ISDIR(ip->i_d.di_mode);
	int                     error = 0;
	xfs_bmap_free_t         free_list;
	xfs_fsblock_t           first_block;
	int			committed;
	uint			resblks;

	trace_xfs_remove(dp, name);

	if (XFS_FORCED_SHUTDOWN(mp))
		return -EIO;

	error = xfs_qm_dqattach(dp, 0);
	if (error)
		goto std_return;

	error = xfs_qm_dqattach(ip, 0);
	if (error)
		goto std_return;

	if (is_dir)
		tp = xfs_trans_alloc(mp, XFS_TRANS_RMDIR);
	else
		tp = xfs_trans_alloc(mp, XFS_TRANS_REMOVE);

	/*
	 * We try to get the real space reservation first,
	 * allowing for directory btree deletion(s) implying
	 * possible bmap insert(s).  If we can't get the space
	 * reservation then we use 0 instead, and avoid the bmap
	 * btree insert(s) in the directory code by, if the bmap
	 * insert tries to happen, instead trimming the LAST
	 * block from the directory.
	 */
	resblks = XFS_REMOVE_SPACE_RES(mp);
	error = xfs_trans_reserve(tp, &M_RES(mp)->tr_remove, resblks, 0);
	if (error == -ENOSPC) {
		resblks = 0;
		error = xfs_trans_reserve(tp, &M_RES(mp)->tr_remove, 0, 0);
	}
	if (error) {
		ASSERT(error != -ENOSPC);
		goto out_trans_cancel;
	}

	xfs_ilock(dp, XFS_IOLOCK_EXCL | XFS_IOLOCK_PARENT);
	xfs_lock_two_inodes(dp, ip, XFS_ILOCK_EXCL);

	xfs_trans_ijoin(tp, dp, XFS_IOLOCK_EXCL | XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);

	/*
	 * If we're removing a directory perform some additional validation.
	 */
	if (is_dir) {
		ASSERT(ip->i_d.di_nlink >= 2);
		if (ip->i_d.di_nlink != 2) {
			error = -ENOTEMPTY;
			goto out_trans_cancel;
		}
		if (!xfs_dir_isempty(ip)) {
			error = -ENOTEMPTY;
			goto out_trans_cancel;
		}

		/* Drop the link from ip's "..".  */
		error = xfs_droplink(tp, dp);
		if (error)
			goto out_trans_cancel;

		/* Drop the "." link from ip to self.  */
		error = xfs_droplink(tp, ip);
		if (error)
			goto out_trans_cancel;
	} else {
		/*
		 * When removing a non-directory we need to log the parent
		 * inode here.  For a directory this is done implicitly
		 * by the xfs_droplink call for the ".." entry.
		 */
		xfs_trans_log_inode(tp, dp, XFS_ILOG_CORE);
	}
	xfs_trans_ichgtime(tp, dp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);

	/* Drop the link from dp to ip. */
	error = xfs_droplink(tp, ip);
	if (error)
		goto out_trans_cancel;

	xfs_bmap_init(&free_list, &first_block);
	error = xfs_dir_removename(tp, dp, name, ip->i_ino,
					&first_block, &free_list, resblks);
	if (error) {
		ASSERT(error != -ENOENT);
		goto out_bmap_cancel;
	}

	/*
	 * If this is a synchronous mount, make sure that the
	 * remove transaction goes to disk before returning to
	 * the user.
	 */
	if (mp->m_flags & (XFS_MOUNT_WSYNC|XFS_MOUNT_DIRSYNC))
		xfs_trans_set_sync(tp);

	error = xfs_bmap_finish(&tp, &free_list, &committed);
	if (error)
		goto out_bmap_cancel;

	error = xfs_trans_commit(tp);
	if (error)
		goto std_return;

	if (is_dir && xfs_inode_is_filestream(ip))
		xfs_filestream_deassociate(ip);

	return 0;

 out_bmap_cancel:
	xfs_bmap_cancel(&free_list);
 out_trans_cancel:
	xfs_trans_cancel(tp);
 std_return:
	return error;
}

/*
 * Enter all inodes for a rename transaction into a sorted array.
 */
#define __XFS_SORT_INODES	5
STATIC void
xfs_sort_for_rename(
	struct xfs_inode	*dp1,	/* in: old (source) directory inode */
	struct xfs_inode	*dp2,	/* in: new (target) directory inode */
	struct xfs_inode	*ip1,	/* in: inode of old entry */
	struct xfs_inode	*ip2,	/* in: inode of new entry */
	struct xfs_inode	*wip,	/* in: whiteout inode */
	struct xfs_inode	**i_tab,/* out: sorted array of inodes */
	int			*num_inodes)  /* in/out: inodes in array */
{
	int			i, j;

	ASSERT(*num_inodes == __XFS_SORT_INODES);
	memset(i_tab, 0, *num_inodes * sizeof(struct xfs_inode *));

	/*
	 * i_tab contains a list of pointers to inodes.  We initialize
	 * the table here & we'll sort it.  We will then use it to
	 * order the acquisition of the inode locks.
	 *
	 * Note that the table may contain duplicates.  e.g., dp1 == dp2.
	 */
	i = 0;
	i_tab[i++] = dp1;
	i_tab[i++] = dp2;
	i_tab[i++] = ip1;
	if (ip2)
		i_tab[i++] = ip2;
	if (wip)
		i_tab[i++] = wip;
	*num_inodes = i;

	/*
	 * Sort the elements via bubble sort.  (Remember, there are at
	 * most 5 elements to sort, so this is adequate.)
	 */
	for (i = 0; i < *num_inodes; i++) {
		for (j = 1; j < *num_inodes; j++) {
			if (i_tab[j]->i_ino < i_tab[j-1]->i_ino) {
				struct xfs_inode *temp = i_tab[j];
				i_tab[j] = i_tab[j-1];
				i_tab[j-1] = temp;
			}
		}
	}
}

static int
xfs_finish_rename(
	struct xfs_trans	*tp,
	struct xfs_bmap_free	*free_list)
{
	int			committed = 0;
	int			error;

	/*
	 * If this is a synchronous mount, make sure that the rename transaction
	 * goes to disk before returning to the user.
	 */
	if (tp->t_mountp->m_flags & (XFS_MOUNT_WSYNC|XFS_MOUNT_DIRSYNC))
		xfs_trans_set_sync(tp);

	error = xfs_bmap_finish(&tp, free_list, &committed);
	if (error) {
		xfs_bmap_cancel(free_list);
		xfs_trans_cancel(tp);
		return error;
	}

	return xfs_trans_commit(tp);
}

/*
 * xfs_cross_rename()
 *
 * responsible for handling RENAME_EXCHANGE flag in renameat2() sytemcall
 */
STATIC int
xfs_cross_rename(
	struct xfs_trans	*tp,
	struct xfs_inode	*dp1,
	struct xfs_name		*name1,
	struct xfs_inode	*ip1,
	struct xfs_inode	*dp2,
	struct xfs_name		*name2,
	struct xfs_inode	*ip2,
	struct xfs_bmap_free	*free_list,
	xfs_fsblock_t		*first_block,
	int			spaceres)
{
	int		error = 0;
	int		ip1_flags = 0;
	int		ip2_flags = 0;
	int		dp2_flags = 0;

	/* Swap inode number for dirent in first parent */
	error = xfs_dir_replace(tp, dp1, name1,
				ip2->i_ino,
				first_block, free_list, spaceres);
	if (error)
		goto out_trans_abort;

	/* Swap inode number for dirent in second parent */
	error = xfs_dir_replace(tp, dp2, name2,
				ip1->i_ino,
				first_block, free_list, spaceres);
	if (error)
		goto out_trans_abort;

	/*
	 * If we're renaming one or more directories across different parents,
	 * update the respective ".." entries (and link counts) to match the new
	 * parents.
	 */
	if (dp1 != dp2) {
		dp2_flags = XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG;

		if (S_ISDIR(ip2->i_d.di_mode)) {
			error = xfs_dir_replace(tp, ip2, &xfs_name_dotdot,
						dp1->i_ino, first_block,
						free_list, spaceres);
			if (error)
				goto out_trans_abort;

			/* transfer ip2 ".." reference to dp1 */
			if (!S_ISDIR(ip1->i_d.di_mode)) {
				error = xfs_droplink(tp, dp2);
				if (error)
					goto out_trans_abort;
				error = xfs_bumplink(tp, dp1);
				if (error)
					goto out_trans_abort;
			}

			/*
			 * Although ip1 isn't changed here, userspace needs
			 * to be warned about the change, so that applications
			 * relying on it (like backup ones), will properly
			 * notify the change
			 */
			ip1_flags |= XFS_ICHGTIME_CHG;
			ip2_flags |= XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG;
		}

		if (S_ISDIR(ip1->i_d.di_mode)) {
			error = xfs_dir_replace(tp, ip1, &xfs_name_dotdot,
						dp2->i_ino, first_block,
						free_list, spaceres);
			if (error)
				goto out_trans_abort;

			/* transfer ip1 ".." reference to dp2 */
			if (!S_ISDIR(ip2->i_d.di_mode)) {
				error = xfs_droplink(tp, dp1);
				if (error)
					goto out_trans_abort;
				error = xfs_bumplink(tp, dp2);
				if (error)
					goto out_trans_abort;
			}

			/*
			 * Although ip2 isn't changed here, userspace needs
			 * to be warned about the change, so that applications
			 * relying on it (like backup ones), will properly
			 * notify the change
			 */
			ip1_flags |= XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG;
			ip2_flags |= XFS_ICHGTIME_CHG;
		}
	}

	if (ip1_flags) {
		xfs_trans_ichgtime(tp, ip1, ip1_flags);
		xfs_trans_log_inode(tp, ip1, XFS_ILOG_CORE);
	}
	if (ip2_flags) {
		xfs_trans_ichgtime(tp, ip2, ip2_flags);
		xfs_trans_log_inode(tp, ip2, XFS_ILOG_CORE);
	}
	if (dp2_flags) {
		xfs_trans_ichgtime(tp, dp2, dp2_flags);
		xfs_trans_log_inode(tp, dp2, XFS_ILOG_CORE);
	}
	xfs_trans_ichgtime(tp, dp1, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
	xfs_trans_log_inode(tp, dp1, XFS_ILOG_CORE);
	return xfs_finish_rename(tp, free_list);

out_trans_abort:
	xfs_bmap_cancel(free_list);
	xfs_trans_cancel(tp);
	return error;
}

/*
 * xfs_rename_alloc_whiteout()
 *
 * Return a referenced, unlinked, unlocked inode that that can be used as a
 * whiteout in a rename transaction. We use a tmpfile inode here so that if we
 * crash between allocating the inode and linking it into the rename transaction
 * recovery will free the inode and we won't leak it.
 */
static int
xfs_rename_alloc_whiteout(
	struct xfs_inode	*dp,
	struct xfs_inode	**wip)
{
	struct xfs_inode	*tmpfile;
	int			error;

	error = xfs_create_tmpfile(dp, NULL, S_IFCHR | WHITEOUT_MODE, &tmpfile);
	if (error)
		return error;

	/*
	 * Prepare the tmpfile inode as if it were created through the VFS.
	 * Otherwise, the link increment paths will complain about nlink 0->1.
	 * Drop the link count as done by d_tmpfile(), complete the inode setup
	 * and flag it as linkable.
	 */
	drop_nlink(VFS_I(tmpfile));
	xfs_finish_inode_setup(tmpfile);
	VFS_I(tmpfile)->i_state |= I_LINKABLE;

	*wip = tmpfile;
	return 0;
}

/*
 * xfs_rename
 */
int
xfs_rename(
	struct xfs_inode	*src_dp,
	struct xfs_name		*src_name,
	struct xfs_inode	*src_ip,
	struct xfs_inode	*target_dp,
	struct xfs_name		*target_name,
	struct xfs_inode	*target_ip,
	unsigned int		flags)
{
	struct xfs_mount	*mp = src_dp->i_mount;
	struct xfs_trans	*tp;
	struct xfs_bmap_free	free_list;
	xfs_fsblock_t		first_block;
	struct xfs_inode	*wip = NULL;		/* whiteout inode */
	struct xfs_inode	*inodes[__XFS_SORT_INODES];
	int			num_inodes = __XFS_SORT_INODES;
	bool			new_parent = (src_dp != target_dp);
	bool			src_is_directory = S_ISDIR(src_ip->i_d.di_mode);
	int			spaceres;
	int			error;

	trace_xfs_rename(src_dp, target_dp, src_name, target_name);

	if ((flags & RENAME_EXCHANGE) && !target_ip)
		return -EINVAL;

	/*
	 * If we are doing a whiteout operation, allocate the whiteout inode
	 * we will be placing at the target and ensure the type is set
	 * appropriately.
	 */
	if (flags & RENAME_WHITEOUT) {
		error = xfs_rename_alloc_whiteout(target_dp, &wip);
		if (error)
			return error;

		/* setup target dirent info as whiteout */
		src_name->type = XFS_DIR3_FT_CHRDEV;
	}

	xfs_sort_for_rename(src_dp, target_dp, src_ip, target_ip, wip,
				inodes, &num_inodes);

	tp = xfs_trans_alloc(mp, XFS_TRANS_RENAME);
	spaceres = XFS_RENAME_SPACE_RES(mp, target_name->len);
	error = xfs_trans_reserve(tp, &M_RES(mp)->tr_rename, spaceres, 0);
	if (error == -ENOSPC) {
		spaceres = 0;
		error = xfs_trans_reserve(tp, &M_RES(mp)->tr_rename, 0, 0);
	}
	if (error)
		goto out_trans_cancel;

	/*
	 * Attach the dquots to the inodes
	 */
	error = xfs_qm_vop_rename_dqattach(inodes);
	if (error)
		goto out_trans_cancel;

	/*
	 * Lock all the participating inodes. Depending upon whether
	 * the target_name exists in the target directory, and
	 * whether the target directory is the same as the source
	 * directory, we can lock from 2 to 4 inodes.
	 */
	if (!new_parent)
		xfs_ilock(src_dp, XFS_IOLOCK_EXCL | XFS_IOLOCK_PARENT);
	else
		xfs_lock_two_inodes(src_dp, target_dp,
				    XFS_IOLOCK_EXCL | XFS_IOLOCK_PARENT);

	xfs_lock_inodes(inodes, num_inodes, XFS_ILOCK_EXCL);

	/*
	 * Join all the inodes to the transaction. From this point on,
	 * we can rely on either trans_commit or trans_cancel to unlock
	 * them.
	 */
	xfs_trans_ijoin(tp, src_dp, XFS_IOLOCK_EXCL | XFS_ILOCK_EXCL);
	if (new_parent)
		xfs_trans_ijoin(tp, target_dp, XFS_IOLOCK_EXCL | XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, src_ip, XFS_ILOCK_EXCL);
	if (target_ip)
		xfs_trans_ijoin(tp, target_ip, XFS_ILOCK_EXCL);
	if (wip)
		xfs_trans_ijoin(tp, wip, XFS_ILOCK_EXCL);

	/*
	 * If we are using project inheritance, we only allow renames
	 * into our tree when the project IDs are the same; else the
	 * tree quota mechanism would be circumvented.
	 */
	if (unlikely((target_dp->i_d.di_flags & XFS_DIFLAG_PROJINHERIT) &&
		     (xfs_get_projid(target_dp) != xfs_get_projid(src_ip)))) {
		error = -EXDEV;
		goto out_trans_cancel;
	}

	xfs_bmap_init(&free_list, &first_block);

	/* RENAME_EXCHANGE is unique from here on. */
	if (flags & RENAME_EXCHANGE)
		return xfs_cross_rename(tp, src_dp, src_name, src_ip,
					target_dp, target_name, target_ip,
					&free_list, &first_block, spaceres);

	/*
	 * Set up the target.
	 */
	if (target_ip == NULL) {
		/*
		 * If there's no space reservation, check the entry will
		 * fit before actually inserting it.
		 */
		if (!spaceres) {
			error = xfs_dir_canenter(tp, target_dp, target_name);
			if (error)
				goto out_trans_cancel;
		}
		/*
		 * If target does not exist and the rename crosses
		 * directories, adjust the target directory link count
		 * to account for the ".." reference from the new entry.
		 */
		error = xfs_dir_createname(tp, target_dp, target_name,
						src_ip->i_ino, &first_block,
						&free_list, spaceres);
		if (error)
			goto out_bmap_cancel;

		xfs_trans_ichgtime(tp, target_dp,
					XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);

		if (new_parent && src_is_directory) {
			error = xfs_bumplink(tp, target_dp);
			if (error)
				goto out_bmap_cancel;
		}
	} else { /* target_ip != NULL */
		/*
		 * If target exists and it's a directory, check that both
		 * target and source are directories and that target can be
		 * destroyed, or that neither is a directory.
		 */
		if (S_ISDIR(target_ip->i_d.di_mode)) {
			/*
			 * Make sure target dir is empty.
			 */
			if (!(xfs_dir_isempty(target_ip)) ||
			    (target_ip->i_d.di_nlink > 2)) {
				error = -EEXIST;
				goto out_trans_cancel;
			}
		}

		/*
		 * Link the source inode under the target name.
		 * If the source inode is a directory and we are moving
		 * it across directories, its ".." entry will be
		 * inconsistent until we replace that down below.
		 *
		 * In case there is already an entry with the same
		 * name at the destination directory, remove it first.
		 */
		error = xfs_dir_replace(tp, target_dp, target_name,
					src_ip->i_ino,
					&first_block, &free_list, spaceres);
		if (error)
			goto out_bmap_cancel;

		xfs_trans_ichgtime(tp, target_dp,
					XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);

		/*
		 * Decrement the link count on the target since the target
		 * dir no longer points to it.
		 */
		error = xfs_droplink(tp, target_ip);
		if (error)
			goto out_bmap_cancel;

		if (src_is_directory) {
			/*
			 * Drop the link from the old "." entry.
			 */
			error = xfs_droplink(tp, target_ip);
			if (error)
				goto out_bmap_cancel;
		}
	} /* target_ip != NULL */

	/*
	 * Remove the source.
	 */
	if (new_parent && src_is_directory) {
		/*
		 * Rewrite the ".." entry to point to the new
		 * directory.
		 */
		error = xfs_dir_replace(tp, src_ip, &xfs_name_dotdot,
					target_dp->i_ino,
					&first_block, &free_list, spaceres);
		ASSERT(error != -EEXIST);
		if (error)
			goto out_bmap_cancel;
	}

	/*
	 * We always want to hit the ctime on the source inode.
	 *
	 * This isn't strictly required by the standards since the source
	 * inode isn't really being changed, but old unix file systems did
	 * it and some incremental backup programs won't work without it.
	 */
	xfs_trans_ichgtime(tp, src_ip, XFS_ICHGTIME_CHG);
	xfs_trans_log_inode(tp, src_ip, XFS_ILOG_CORE);

	/*
	 * Adjust the link count on src_dp.  This is necessary when
	 * renaming a directory, either within one parent when
	 * the target existed, or across two parent directories.
	 */
	if (src_is_directory && (new_parent || target_ip != NULL)) {

		/*
		 * Decrement link count on src_directory since the
		 * entry that's moved no longer points to it.
		 */
		error = xfs_droplink(tp, src_dp);
		if (error)
			goto out_bmap_cancel;
	}

	/*
	 * For whiteouts, we only need to update the source dirent with the
	 * inode number of the whiteout inode rather than removing it
	 * altogether.
	 */
	if (wip) {
		error = xfs_dir_replace(tp, src_dp, src_name, wip->i_ino,
					&first_block, &free_list, spaceres);
	} else
		error = xfs_dir_removename(tp, src_dp, src_name, src_ip->i_ino,
					   &first_block, &free_list, spaceres);
	if (error)
		goto out_bmap_cancel;

	/*
	 * For whiteouts, we need to bump the link count on the whiteout inode.
	 * This means that failures all the way up to this point leave the inode
	 * on the unlinked list and so cleanup is a simple matter of dropping
	 * the remaining reference to it. If we fail here after bumping the link
	 * count, we're shutting down the filesystem so we'll never see the
	 * intermediate state on disk.
	 */
	if (wip) {
		ASSERT(VFS_I(wip)->i_nlink == 0 && wip->i_d.di_nlink == 0);
		error = xfs_bumplink(tp, wip);
		if (error)
			goto out_bmap_cancel;
		error = xfs_iunlink_remove(tp, wip);
		if (error)
			goto out_bmap_cancel;
		xfs_trans_log_inode(tp, wip, XFS_ILOG_CORE);

		/*
		 * Now we have a real link, clear the "I'm a tmpfile" state
		 * flag from the inode so it doesn't accidentally get misused in
		 * future.
		 */
		VFS_I(wip)->i_state &= ~I_LINKABLE;
	}

	xfs_trans_ichgtime(tp, src_dp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
	xfs_trans_log_inode(tp, src_dp, XFS_ILOG_CORE);
	if (new_parent)
		xfs_trans_log_inode(tp, target_dp, XFS_ILOG_CORE);

	error = xfs_finish_rename(tp, &free_list);
	if (wip)
		IRELE(wip);
	return error;

out_bmap_cancel:
	xfs_bmap_cancel(&free_list);
out_trans_cancel:
	xfs_trans_cancel(tp);
	if (wip)
		IRELE(wip);
	return error;
}

STATIC int
xfs_iflush_cluster(
	xfs_inode_t	*ip,
	xfs_buf_t	*bp)
{
	xfs_mount_t		*mp = ip->i_mount;
	xfs_perag_t		*pag = xfs_get_perag(mp, ip->i_ino);
	struct xfs_perag	*pag;
	unsigned long		first_index, mask;
	unsigned long		inodes_per_cluster;
	int			ilist_size;
	xfs_inode_t		**ilist;
	xfs_inode_t		*iq;
	int			nr_found;
	int			clcount = 0;
	int			bufwasdelwri;
	int			i;

	ASSERT(pag->pagi_inodeok);
	ASSERT(pag->pag_ici_init);

	inodes_per_cluster = XFS_INODE_CLUSTER_SIZE(mp) >> mp->m_sb.sb_inodelog;
	ilist_size = inodes_per_cluster * sizeof(xfs_inode_t *);
	ilist = kmem_alloc(ilist_size, KM_MAYFAIL|KM_NOFS);
	if (!ilist)
		return 0;

	mask = ~(((XFS_INODE_CLUSTER_SIZE(mp) >> mp->m_sb.sb_inodelog)) - 1);
	first_index = XFS_INO_TO_AGINO(mp, ip->i_ino) & mask;
	read_lock(&pag->pag_ici_lock);
	pag = xfs_perag_get(mp, XFS_INO_TO_AGNO(mp, ip->i_ino));

	inodes_per_cluster = mp->m_inode_cluster_size >> mp->m_sb.sb_inodelog;
	ilist_size = inodes_per_cluster * sizeof(xfs_inode_t *);
	ilist = kmem_alloc(ilist_size, KM_MAYFAIL|KM_NOFS);
	if (!ilist)
		goto out_put;

	mask = ~(((mp->m_inode_cluster_size >> mp->m_sb.sb_inodelog)) - 1);
	first_index = XFS_INO_TO_AGINO(mp, ip->i_ino) & mask;
	rcu_read_lock();
	/* really need a gang lookup range call here */
	nr_found = radix_tree_gang_lookup(&pag->pag_ici_root, (void**)ilist,
					first_index, inodes_per_cluster);
	if (nr_found == 0)
		goto out_free;

	for (i = 0; i < nr_found; i++) {
		iq = ilist[i];
		if (iq == ip)
			continue;
		/* if the inode lies outside this cluster, we're done. */
		if ((XFS_INO_TO_AGINO(mp, iq->i_ino) & mask) != first_index)
			break;

		/*
		 * because this is an RCU protected lookup, we could find a
		 * recently freed or even reallocated inode during the lookup.
		 * We need to check under the i_flags_lock for a valid inode
		 * here. Skip it if it is not valid or the wrong inode.
		 */
		spin_lock(&iq->i_flags_lock);
		if (!iq->i_ino ||
		    __xfs_iflags_test(iq, XFS_ISTALE) ||
		    (XFS_INO_TO_AGINO(mp, iq->i_ino) & mask) != first_index) {
			spin_unlock(&iq->i_flags_lock);
			continue;
		}
		spin_unlock(&iq->i_flags_lock);

		/*
		 * Do an un-protected check to see if the inode is dirty and
		 * is a candidate for flushing.  These checks will be repeated
		 * later after the appropriate locks are acquired.
		 */
		if (xfs_inode_clean(iq) && xfs_ipincount(iq) == 0)
			continue;

		/*
		 * Try to get locks.  If any are unavailable or it is pinned,
		 * then this inode cannot be flushed and is skipped.
		 */

		if (!xfs_ilock_nowait(iq, XFS_ILOCK_SHARED))
			continue;
		if (!xfs_iflock_nowait(iq)) {
			xfs_iunlock(iq, XFS_ILOCK_SHARED);
			continue;
		}
		if (xfs_ipincount(iq)) {
			xfs_ifunlock(iq);
			xfs_iunlock(iq, XFS_ILOCK_SHARED);
			continue;
		}

		/*
		 * arriving here means that this inode can be flushed.  First
		 * re-check that it's dirty before flushing.
		 */
		if (!xfs_inode_clean(iq)) {
			int	error;
			error = xfs_iflush_int(iq, bp);
			if (error) {
				xfs_iunlock(iq, XFS_ILOCK_SHARED);
				goto cluster_corrupt_out;
			}
			clcount++;
		} else {
			xfs_ifunlock(iq);
		}
		xfs_iunlock(iq, XFS_ILOCK_SHARED);
	}

	if (clcount) {
		XFS_STATS_INC(xs_icluster_flushcnt);
		XFS_STATS_ADD(xs_icluster_flushinode, clcount);
	}

out_free:
	read_unlock(&pag->pag_ici_lock);
	kmem_free(ilist);
		XFS_STATS_INC(mp, xs_icluster_flushcnt);
		XFS_STATS_ADD(mp, xs_icluster_flushinode, clcount);
	}

out_free:
	rcu_read_unlock();
	kmem_free(ilist);
out_put:
	xfs_perag_put(pag);
	return 0;


cluster_corrupt_out:
	/*
	 * Corruption detected in the clustering loop.  Invalidate the
	 * inode buffer and shut down the filesystem.
	 */
	read_unlock(&pag->pag_ici_lock);
	/*
	 * Clean up the buffer.  If it was B_DELWRI, just release it --
	 * brelse can handle it with no problems.  If not, shut down the
	 * filesystem before releasing the buffer.
	 */
	bufwasdelwri = XFS_BUF_ISDELAYWRITE(bp);
	rcu_read_unlock();
	/*
	 * Clean up the buffer.  If it was delwri, just release it --
	 * brelse can handle it with no problems.  If not, shut down the
	 * filesystem before releasing the buffer.
	 */
	bufwasdelwri = (bp->b_flags & _XBF_DELWRI_Q);
	if (bufwasdelwri)
		xfs_buf_relse(bp);

	xfs_force_shutdown(mp, SHUTDOWN_CORRUPT_INCORE);

	if (!bufwasdelwri) {
		/*
		 * Just like incore_relse: if we have b_iodone functions,
		 * mark the buffer as an error and call them.  Otherwise
		 * mark it as stale and brelse.
		 */
		if (XFS_BUF_IODONE_FUNC(bp)) {
			XFS_BUF_CLR_BDSTRAT_FUNC(bp);
			XFS_BUF_UNDONE(bp);
			XFS_BUF_STALE(bp);
			XFS_BUF_SHUT(bp);
			XFS_BUF_ERROR(bp,EIO);
			xfs_biodone(bp);
		} else {
			XFS_BUF_STALE(bp);
		if (bp->b_iodone) {
			XFS_BUF_UNDONE(bp);
			xfs_buf_stale(bp);
			xfs_buf_ioerror(bp, -EIO);
			xfs_buf_ioend(bp);
		} else {
			xfs_buf_stale(bp);
			xfs_buf_relse(bp);
		}
	}

	/*
	 * Unlocks the flush lock
	 */
	xfs_iflush_abort(iq);
	kmem_free(ilist);
	return XFS_ERROR(EFSCORRUPTED);
}

/*
 * xfs_iflush() will write a modified inode's changes out to the
 * inode's on disk home.  The caller must have the inode lock held
 * in at least shared mode and the inode flush completion must be
 * active as well.  The inode lock will still be held upon return from
 * the call and the caller is free to unlock it.
 * The inode flush will be completed when the inode reaches the disk.
 * The flags indicate how the inode's buffer should be written out.
 */
int
xfs_iflush(
	xfs_inode_t		*ip,
	uint			flags)
{
	xfs_inode_log_item_t	*iip;
	xfs_buf_t		*bp;
	xfs_dinode_t		*dip;
	xfs_mount_t		*mp;
	int			error;
	int			noblock = (flags == XFS_IFLUSH_ASYNC_NOBLOCK);
	enum { INT_DELWRI = (1 << 0), INT_ASYNC = (1 << 1) };

	XFS_STATS_INC(xs_iflush_count);

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL|XFS_ILOCK_SHARED));
	ASSERT(!completion_done(&ip->i_flush));
	ASSERT(ip->i_d.di_format != XFS_DINODE_FMT_BTREE ||
	       ip->i_d.di_nextents > ip->i_df.if_ext_max);

	iip = ip->i_itemp;
	mp = ip->i_mount;

	/*
	 * If the inode isn't dirty, then just release the inode
	 * flush lock and do nothing.
	 */
	if (xfs_inode_clean(ip)) {
	xfs_iflush_abort(iq, false);
	kmem_free(ilist);
	xfs_perag_put(pag);
	return -EFSCORRUPTED;
}

/*
 * Flush dirty inode metadata into the backing buffer.
 *
 * The caller must have the inode lock and the inode flush lock held.  The
 * inode lock will still be held upon return to the caller, and the inode
 * flush lock will be released after the inode has reached the disk.
 *
 * The caller must write out the buffer returned in *bpp and release it.
 */
int
xfs_iflush(
	struct xfs_inode	*ip,
	struct xfs_buf		**bpp)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_buf		*bp = NULL;
	struct xfs_dinode	*dip;
	int			error;

	XFS_STATS_INC(mp, xs_iflush_count);

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL|XFS_ILOCK_SHARED));
	ASSERT(xfs_isiflocked(ip));
	ASSERT(ip->i_d.di_format != XFS_DINODE_FMT_BTREE ||
	       ip->i_d.di_nextents > XFS_IFORK_MAXEXT(ip, XFS_DATA_FORK));

	*bpp = NULL;

	xfs_iunpin_wait(ip);

	/*
	 * For stale inodes we cannot rely on the backing buffer remaining
	 * stale in cache for the remaining life of the stale inode and so
	 * xfs_imap_to_bp() below may give us a buffer that no longer contains
	 * inodes below. We have to check this after ensuring the inode is
	 * unpinned so that it is safe to reclaim the stale inode after the
	 * flush call.
	 */
	if (xfs_iflags_test(ip, XFS_ISTALE)) {
		xfs_ifunlock(ip);
		return 0;
	}

	/*
	 * We can't flush the inode until it is unpinned, so wait for it if we
	 * are allowed to block.  We know noone new can pin it, because we are
	 * holding the inode lock shared and you need to hold it exclusively to
	 * pin the inode.
	 *
	 * If we are not allowed to block, force the log out asynchronously so
	 * that when we come back the inode will be unpinned. If other inodes
	 * in the same cluster are dirty, they will probably write the inode
	 * out for us if they occur after the log force completes.
	 */
	if (noblock && xfs_ipincount(ip)) {
		xfs_iunpin_nowait(ip);
		xfs_ifunlock(ip);
		return EAGAIN;
	}
	xfs_iunpin_wait(ip);

	/*
	 * This may have been unpinned because the filesystem is shutting
	 * down forcibly. If that's the case we must not write this inode
	 * to disk, because the log record didn't make it to disk!
	 */
	if (XFS_FORCED_SHUTDOWN(mp)) {
		ip->i_update_core = 0;
		if (iip)
			iip->ili_format.ilf_fields = 0;
		xfs_ifunlock(ip);
		return XFS_ERROR(EIO);
	}

	/*
	 * Decide how buffer will be flushed out.  This is done before
	 * the call to xfs_iflush_int because this field is zeroed by it.
	 */
	if (iip != NULL && iip->ili_format.ilf_fields != 0) {
		/*
		 * Flush out the inode buffer according to the directions
		 * of the caller.  In the cases where the caller has given
		 * us a choice choose the non-delwri case.  This is because
		 * the inode is in the AIL and we need to get it out soon.
		 */
		switch (flags) {
		case XFS_IFLUSH_SYNC:
		case XFS_IFLUSH_DELWRI_ELSE_SYNC:
			flags = 0;
			break;
		case XFS_IFLUSH_ASYNC_NOBLOCK:
		case XFS_IFLUSH_ASYNC:
		case XFS_IFLUSH_DELWRI_ELSE_ASYNC:
			flags = INT_ASYNC;
			break;
		case XFS_IFLUSH_DELWRI:
			flags = INT_DELWRI;
			break;
		default:
			ASSERT(0);
			flags = 0;
			break;
		}
	} else {
		switch (flags) {
		case XFS_IFLUSH_DELWRI_ELSE_SYNC:
		case XFS_IFLUSH_DELWRI_ELSE_ASYNC:
		case XFS_IFLUSH_DELWRI:
			flags = INT_DELWRI;
			break;
		case XFS_IFLUSH_ASYNC_NOBLOCK:
		case XFS_IFLUSH_ASYNC:
			flags = INT_ASYNC;
			break;
		case XFS_IFLUSH_SYNC:
			flags = 0;
			break;
		default:
			ASSERT(0);
			flags = 0;
			break;
		}
	 * This may have been unpinned because the filesystem is shutting
	 * down forcibly. If that's the case we must not write this inode
	 * to disk, because the log record didn't make it to disk.
	 *
	 * We also have to remove the log item from the AIL in this case,
	 * as we wait for an empty AIL as part of the unmount process.
	 */
	if (XFS_FORCED_SHUTDOWN(mp)) {
		error = -EIO;
		goto abort_out;
	}

	/*
	 * Get the buffer containing the on-disk inode. We are doing a try-lock
	 * operation here, so we may get  an EAGAIN error. In that case, we
	 * simply want to return with the inode still dirty.
	 *
	 * If we get any other error, we effectively have a corruption situation
	 * and we cannot flush the inode, so we treat it the same as failing
	 * xfs_iflush_int().
	 */
	error = xfs_itobp(mp, NULL, ip, &dip, &bp, 0, 0,
				noblock ? XFS_BUF_TRYLOCK : XFS_BUF_LOCK);
	error = xfs_imap_to_bp(mp, NULL, &ip->i_imap, &dip, &bp, XBF_TRYLOCK,
			       0);
	if (error == -EAGAIN) {
		xfs_ifunlock(ip);
		return error;
	}
	if (error)
		goto corrupt_out;

	/*
	 * First flush out the inode that xfs_iflush was called with.
	 */
	error = xfs_iflush_int(ip, bp);
	if (error)
		goto corrupt_out;

	/*
	 * If the buffer is pinned then push on the log now so we won't
	 * get stuck waiting in the write for too long.
	 */
	if (XFS_BUF_ISPINNED(bp))
		xfs_log_force(mp, (xfs_lsn_t)0, XFS_LOG_FORCE);
	if (xfs_buf_ispinned(bp))
		xfs_log_force(mp, 0);

	/*
	 * inode clustering:
	 * see if other inodes can be gathered into this write
	 */
	error = xfs_iflush_cluster(ip, bp);
	if (error)
		goto cluster_corrupt_out;

	if (flags & INT_DELWRI) {
		xfs_bdwrite(mp, bp);
	} else if (flags & INT_ASYNC) {
		error = xfs_bawrite(mp, bp);
	} else {
		error = xfs_bwrite(mp, bp);
	}
	return error;
	*bpp = bp;
	return 0;

corrupt_out:
	if (bp)
		xfs_buf_relse(bp);
	xfs_force_shutdown(mp, SHUTDOWN_CORRUPT_INCORE);
cluster_corrupt_out:
	/*
	 * Unlocks the flush lock
	 */
	xfs_iflush_abort(ip);
	return XFS_ERROR(EFSCORRUPTED);
}


STATIC int
xfs_iflush_int(
	xfs_inode_t		*ip,
	xfs_buf_t		*bp)
{
	xfs_inode_log_item_t	*iip;
	xfs_dinode_t		*dip;
	xfs_mount_t		*mp;
#ifdef XFS_TRANS_DEBUG
	int			first;
#endif

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL|XFS_ILOCK_SHARED));
	ASSERT(!completion_done(&ip->i_flush));
	ASSERT(ip->i_d.di_format != XFS_DINODE_FMT_BTREE ||
	       ip->i_d.di_nextents > ip->i_df.if_ext_max);

	iip = ip->i_itemp;
	mp = ip->i_mount;


	/*
	 * If the inode isn't dirty, then just release the inode
	 * flush lock and do nothing.
	 */
	if (xfs_inode_clean(ip)) {
		xfs_ifunlock(ip);
		return 0;
	}

	/* set *dip = inode's place in the buffer */
	dip = (xfs_dinode_t *)xfs_buf_offset(bp, ip->i_boffset);

	/*
	 * Clear i_update_core before copying out the data.
	 * This is for coordination with our timestamp updates
	 * that don't hold the inode lock. They will always
	 * update the timestamps BEFORE setting i_update_core,
	 * so if we clear i_update_core after they set it we
	 * are guaranteed to see their updates to the timestamps.
	 * I believe that this depends on strongly ordered memory
	 * semantics, but we have that.  We use the SYNCHRONIZE
	 * macro to make sure that the compiler does not reorder
	 * the i_update_core access below the data copy below.
	 */
	ip->i_update_core = 0;
	SYNCHRONIZE();

	/*
	 * Make sure to get the latest atime from the Linux inode.
	 */
	xfs_synchronize_atime(ip);

	if (XFS_TEST_ERROR(be16_to_cpu(dip->di_core.di_magic) != XFS_DINODE_MAGIC,
			       mp, XFS_ERRTAG_IFLUSH_1, XFS_RANDOM_IFLUSH_1)) {
		xfs_cmn_err(XFS_PTAG_IFLUSH, CE_ALERT, mp,
		    "xfs_iflush: Bad inode %Lu magic number 0x%x, ptr 0x%p",
			ip->i_ino, be16_to_cpu(dip->di_core.di_magic), dip);
	error = -EFSCORRUPTED;
abort_out:
	/*
	 * Unlocks the flush lock
	 */
	xfs_iflush_abort(ip, false);
	return error;
}

STATIC int
xfs_iflush_int(
	struct xfs_inode	*ip,
	struct xfs_buf		*bp)
{
	struct xfs_inode_log_item *iip = ip->i_itemp;
	struct xfs_dinode	*dip;
	struct xfs_mount	*mp = ip->i_mount;

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL|XFS_ILOCK_SHARED));
	ASSERT(xfs_isiflocked(ip));
	ASSERT(ip->i_d.di_format != XFS_DINODE_FMT_BTREE ||
	       ip->i_d.di_nextents > XFS_IFORK_MAXEXT(ip, XFS_DATA_FORK));
	ASSERT(iip != NULL && iip->ili_fields != 0);
	ASSERT(ip->i_d.di_version > 1);

	/* set *dip = inode's place in the buffer */
	dip = xfs_buf_offset(bp, ip->i_imap.im_boffset);

	if (XFS_TEST_ERROR(dip->di_magic != cpu_to_be16(XFS_DINODE_MAGIC),
			       mp, XFS_ERRTAG_IFLUSH_1, XFS_RANDOM_IFLUSH_1)) {
		xfs_alert_tag(mp, XFS_PTAG_IFLUSH,
			"%s: Bad inode %Lu magic number 0x%x, ptr 0x%p",
			__func__, ip->i_ino, be16_to_cpu(dip->di_magic), dip);
		goto corrupt_out;
	}
	if (XFS_TEST_ERROR(ip->i_d.di_magic != XFS_DINODE_MAGIC,
				mp, XFS_ERRTAG_IFLUSH_2, XFS_RANDOM_IFLUSH_2)) {
		xfs_cmn_err(XFS_PTAG_IFLUSH, CE_ALERT, mp,
			"xfs_iflush: Bad inode %Lu, ptr 0x%p, magic number 0x%x",
			ip->i_ino, ip, ip->i_d.di_magic);
		goto corrupt_out;
	}
	if ((ip->i_d.di_mode & S_IFMT) == S_IFREG) {
		xfs_alert_tag(mp, XFS_PTAG_IFLUSH,
			"%s: Bad inode %Lu, ptr 0x%p, magic number 0x%x",
			__func__, ip->i_ino, ip, ip->i_d.di_magic);
		goto corrupt_out;
	}
	if (S_ISREG(ip->i_d.di_mode)) {
		if (XFS_TEST_ERROR(
		    (ip->i_d.di_format != XFS_DINODE_FMT_EXTENTS) &&
		    (ip->i_d.di_format != XFS_DINODE_FMT_BTREE),
		    mp, XFS_ERRTAG_IFLUSH_3, XFS_RANDOM_IFLUSH_3)) {
			xfs_cmn_err(XFS_PTAG_IFLUSH, CE_ALERT, mp,
				"xfs_iflush: Bad regular inode %Lu, ptr 0x%p",
				ip->i_ino, ip);
			goto corrupt_out;
		}
	} else if ((ip->i_d.di_mode & S_IFMT) == S_IFDIR) {
			xfs_alert_tag(mp, XFS_PTAG_IFLUSH,
				"%s: Bad regular inode %Lu, ptr 0x%p",
				__func__, ip->i_ino, ip);
			goto corrupt_out;
		}
	} else if (S_ISDIR(ip->i_d.di_mode)) {
		if (XFS_TEST_ERROR(
		    (ip->i_d.di_format != XFS_DINODE_FMT_EXTENTS) &&
		    (ip->i_d.di_format != XFS_DINODE_FMT_BTREE) &&
		    (ip->i_d.di_format != XFS_DINODE_FMT_LOCAL),
		    mp, XFS_ERRTAG_IFLUSH_4, XFS_RANDOM_IFLUSH_4)) {
			xfs_cmn_err(XFS_PTAG_IFLUSH, CE_ALERT, mp,
				"xfs_iflush: Bad directory inode %Lu, ptr 0x%p",
				ip->i_ino, ip);
			xfs_alert_tag(mp, XFS_PTAG_IFLUSH,
				"%s: Bad directory inode %Lu, ptr 0x%p",
				__func__, ip->i_ino, ip);
			goto corrupt_out;
		}
	}
	if (XFS_TEST_ERROR(ip->i_d.di_nextents + ip->i_d.di_anextents >
				ip->i_d.di_nblocks, mp, XFS_ERRTAG_IFLUSH_5,
				XFS_RANDOM_IFLUSH_5)) {
		xfs_cmn_err(XFS_PTAG_IFLUSH, CE_ALERT, mp,
			"xfs_iflush: detected corrupt incore inode %Lu, total extents = %d, nblocks = %Ld, ptr 0x%p",
			ip->i_ino,
			ip->i_d.di_nextents + ip->i_d.di_anextents,
			ip->i_d.di_nblocks,
			ip);
		xfs_alert_tag(mp, XFS_PTAG_IFLUSH,
			"%s: detected corrupt incore inode %Lu, "
			"total extents = %d, nblocks = %Ld, ptr 0x%p",
			__func__, ip->i_ino,
			ip->i_d.di_nextents + ip->i_d.di_anextents,
			ip->i_d.di_nblocks, ip);
		goto corrupt_out;
	}
	if (XFS_TEST_ERROR(ip->i_d.di_forkoff > mp->m_sb.sb_inodesize,
				mp, XFS_ERRTAG_IFLUSH_6, XFS_RANDOM_IFLUSH_6)) {
		xfs_cmn_err(XFS_PTAG_IFLUSH, CE_ALERT, mp,
			"xfs_iflush: bad inode %Lu, forkoff 0x%x, ptr 0x%p",
			ip->i_ino, ip->i_d.di_forkoff, ip);
		goto corrupt_out;
	}
	/*
	 * bump the flush iteration count, used to detect flushes which
	 * postdate a log record during recovery.
	 */

	ip->i_d.di_flushiter++;
		xfs_alert_tag(mp, XFS_PTAG_IFLUSH,
			"%s: bad inode %Lu, forkoff 0x%x, ptr 0x%p",
			__func__, ip->i_ino, ip->i_d.di_forkoff, ip);
		goto corrupt_out;
	}

	/*
	 * Inode item log recovery for v2 inodes are dependent on the
	 * di_flushiter count for correct sequencing. We bump the flush
	 * iteration count so we can detect flushes which postdate a log record
	 * during recovery. This is redundant as we now log every change and
	 * hence this can't happen but we need to still do it to ensure
	 * backwards compatibility with old kernels that predate logging all
	 * inode changes.
	 */
	if (ip->i_d.di_version < 3)
		ip->i_d.di_flushiter++;

	/*
	 * Copy the dirty parts of the inode into the on-disk
	 * inode.  We always copy out the core of the inode,
	 * because if the inode is dirty at all the core must
	 * be.
	 */
	xfs_dinode_to_disk(&dip->di_core, &ip->i_d);
	xfs_dinode_to_disk(dip, &ip->i_d);

	/* Wrap, we never let the log put out DI_MAX_FLUSH */
	if (ip->i_d.di_flushiter == DI_MAX_FLUSH)
		ip->i_d.di_flushiter = 0;

	/*
	 * If this is really an old format inode and the superblock version
	 * has not been updated to support only new format inodes, then
	 * convert back to the old inode format.  If the superblock version
	 * has been updated, then make the conversion permanent.
	 */
	ASSERT(ip->i_d.di_version == XFS_DINODE_VERSION_1 ||
	       xfs_sb_version_hasnlink(&mp->m_sb));
	if (ip->i_d.di_version == XFS_DINODE_VERSION_1) {
		if (!xfs_sb_version_hasnlink(&mp->m_sb)) {
			/*
			 * Convert it back.
			 */
			ASSERT(ip->i_d.di_nlink <= XFS_MAXLINK_1);
			dip->di_core.di_onlink = cpu_to_be16(ip->i_d.di_nlink);
		} else {
			/*
			 * The superblock version has already been bumped,
			 * so just make the conversion to the new inode
			 * format permanent.
			 */
			ip->i_d.di_version = XFS_DINODE_VERSION_2;
			dip->di_core.di_version =  XFS_DINODE_VERSION_2;
			ip->i_d.di_onlink = 0;
			dip->di_core.di_onlink = 0;
			memset(&(ip->i_d.di_pad[0]), 0, sizeof(ip->i_d.di_pad));
			memset(&(dip->di_core.di_pad[0]), 0,
			      sizeof(dip->di_core.di_pad));
			ASSERT(ip->i_d.di_projid == 0);
		}
	}

	xfs_iflush_fork(ip, dip, iip, XFS_DATA_FORK, bp);
	if (XFS_IFORK_Q(ip))
		xfs_iflush_fork(ip, dip, iip, XFS_ATTR_FORK, bp);
	xfs_inobp_check(mp, bp);

	/*
	 * We've recorded everything logged in the inode, so we'd
	 * like to clear the ilf_fields bits so we don't log and
	 * flush things unnecessarily.  However, we can't stop
	 * logging all this information until the data we've copied
	 * into the disk buffer is written to disk.  If we did we might
	 * overwrite the copy of the inode in the log with all the
	 * data after re-logging only part of it, and in the face of
	 * a crash we wouldn't have all the data we need to recover.
	 *
	 * What we do is move the bits to the ili_last_fields field.
	 * When logging the inode, these bits are moved back to the
	 * ilf_fields field.  In the xfs_iflush_done() routine we
	 * clear ili_last_fields, since we know that the information
	 * those bits represent is permanently on disk.  As long as
	 * the flush completes before the inode is logged again, then
	 * both ilf_fields and ili_last_fields will be cleared.
	 *
	 * We can play with the ilf_fields bits here, because the inode
	 * lock must be held exclusively in order to set bits there
	 * and the flush lock protects the ili_last_fields bits.
	 * Set ili_logged so the flush done
	 * routine can tell whether or not to look in the AIL.
	 * Also, store the current LSN of the inode so that we can tell
	 * whether the item has moved in the AIL from xfs_iflush_done().
	 * In order to read the lsn we need the AIL lock, because
	 * it is a 64 bit value that cannot be read atomically.
	 */
	if (iip != NULL && iip->ili_format.ilf_fields != 0) {
		iip->ili_last_fields = iip->ili_format.ilf_fields;
		iip->ili_format.ilf_fields = 0;
		iip->ili_logged = 1;

		ASSERT(sizeof(xfs_lsn_t) == 8);	/* don't lock if it shrinks */
		spin_lock(&mp->m_ail_lock);
		iip->ili_flush_lsn = iip->ili_item.li_lsn;
		spin_unlock(&mp->m_ail_lock);

		/*
		 * Attach the function xfs_iflush_done to the inode's
		 * buffer.  This will remove the inode from the AIL
		 * and unlock the inode's flush lock when the inode is
		 * completely written to disk.
		 */
		xfs_buf_attach_iodone(bp, (void(*)(xfs_buf_t*,xfs_log_item_t*))
				      xfs_iflush_done, (xfs_log_item_t *)iip);

		ASSERT(XFS_BUF_FSPRIVATE(bp, void *) != NULL);
		ASSERT(XFS_BUF_IODONE_FUNC(bp) != NULL);
	} else {
		/*
		 * We're flushing an inode which is not in the AIL and has
		 * not been logged but has i_update_core set.  For this
		 * case we can use a B_DELWRI flush and immediately drop
		 * the inode flush lock because we can avoid the whole
		 * AIL state thing.  It's OK to drop the flush lock now,
		 * because we've already locked the buffer and to do anything
		 * you really need both.
		 */
		if (iip != NULL) {
			ASSERT(iip->ili_logged == 0);
			ASSERT(iip->ili_last_fields == 0);
			ASSERT((iip->ili_item.li_flags & XFS_LI_IN_AIL) == 0);
		}
		xfs_ifunlock(ip);
	}

	return 0;

corrupt_out:
	return XFS_ERROR(EFSCORRUPTED);
}


/*
 * Flush all inactive inodes in mp.
 */
void
xfs_iflush_all(
	xfs_mount_t	*mp)
{
	xfs_inode_t	*ip;

 again:
	XFS_MOUNT_ILOCK(mp);
	ip = mp->m_inodes;
	if (ip == NULL)
		goto out;

	do {
		/* Make sure we skip markers inserted by sync */
		if (ip->i_mount == NULL) {
			ip = ip->i_mnext;
			continue;
		}

		if (!VFS_I(ip)) {
			XFS_MOUNT_IUNLOCK(mp);
			xfs_finish_reclaim(ip, 0, XFS_IFLUSH_ASYNC);
			goto again;
		}

		ASSERT(vn_count(VFS_I(ip)) == 0);

		ip = ip->i_mnext;
	} while (ip != mp->m_inodes);
 out:
	XFS_MOUNT_IUNLOCK(mp);
}

#ifdef XFS_ILOCK_TRACE
ktrace_t	*xfs_ilock_trace_buf;

void
xfs_ilock_trace(xfs_inode_t *ip, int lock, unsigned int lockflags, inst_t *ra)
{
	ktrace_enter(ip->i_lock_trace,
		     (void *)ip,
		     (void *)(unsigned long)lock, /* 1 = LOCK, 3=UNLOCK, etc */
		     (void *)(unsigned long)lockflags, /* XFS_ILOCK_EXCL etc */
		     (void *)ra,		/* caller of ilock */
		     (void *)(unsigned long)current_cpu(),
		     (void *)(unsigned long)current_pid(),
		     NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL);
}
#endif

/*
 * Return a pointer to the extent record at file index idx.
 */
xfs_bmbt_rec_host_t *
xfs_iext_get_ext(
	xfs_ifork_t	*ifp,		/* inode fork pointer */
	xfs_extnum_t	idx)		/* index of target extent */
{
	ASSERT(idx >= 0);
	if ((ifp->if_flags & XFS_IFEXTIREC) && (idx == 0)) {
		return ifp->if_u1.if_ext_irec->er_extbuf;
	} else if (ifp->if_flags & XFS_IFEXTIREC) {
		xfs_ext_irec_t	*erp;		/* irec pointer */
		int		erp_idx = 0;	/* irec index */
		xfs_extnum_t	page_idx = idx;	/* ext index in target list */

		erp = xfs_iext_idx_to_irec(ifp, &page_idx, &erp_idx, 0);
		return &erp->er_extbuf[page_idx];
	} else if (ifp->if_bytes) {
		return &ifp->if_u1.if_extents[idx];
	} else {
		return NULL;
	}
}

/*
 * Insert new item(s) into the extent records for incore inode
 * fork 'ifp'.  'count' new items are inserted at index 'idx'.
 */
void
xfs_iext_insert(
	xfs_ifork_t	*ifp,		/* inode fork pointer */
	xfs_extnum_t	idx,		/* starting index of new items */
	xfs_extnum_t	count,		/* number of inserted items */
	xfs_bmbt_irec_t	*new)		/* items to insert */
{
	xfs_extnum_t	i;		/* extent record index */

	ASSERT(ifp->if_flags & XFS_IFEXTENTS);
	xfs_iext_add(ifp, idx, count);
	for (i = idx; i < idx + count; i++, new++)
		xfs_bmbt_set_all(xfs_iext_get_ext(ifp, i), new);
}

/*
 * This is called when the amount of space required for incore file
 * extents needs to be increased. The ext_diff parameter stores the
 * number of new extents being added and the idx parameter contains
 * the extent index where the new extents will be added. If the new
 * extents are being appended, then we just need to (re)allocate and
 * initialize the space. Otherwise, if the new extents are being
 * inserted into the middle of the existing entries, a bit more work
 * is required to make room for the new extents to be inserted. The
 * caller is responsible for filling in the new extent entries upon
 * return.
 */
void
xfs_iext_add(
	xfs_ifork_t	*ifp,		/* inode fork pointer */
	xfs_extnum_t	idx,		/* index to begin adding exts */
	int		ext_diff)	/* number of extents to add */
{
	int		byte_diff;	/* new bytes being added */
	int		new_size;	/* size of extents after adding */
	xfs_extnum_t	nextents;	/* number of extents in file */

	nextents = ifp->if_bytes / (uint)sizeof(xfs_bmbt_rec_t);
	ASSERT((idx >= 0) && (idx <= nextents));
	byte_diff = ext_diff * sizeof(xfs_bmbt_rec_t);
	new_size = ifp->if_bytes + byte_diff;
	/*
	 * If the new number of extents (nextents + ext_diff)
	 * fits inside the inode, then continue to use the inline
	 * extent buffer.
	 */
	if (nextents + ext_diff <= XFS_INLINE_EXTS) {
		if (idx < nextents) {
			memmove(&ifp->if_u2.if_inline_ext[idx + ext_diff],
				&ifp->if_u2.if_inline_ext[idx],
				(nextents - idx) * sizeof(xfs_bmbt_rec_t));
			memset(&ifp->if_u2.if_inline_ext[idx], 0, byte_diff);
		}
		ifp->if_u1.if_extents = ifp->if_u2.if_inline_ext;
		ifp->if_real_bytes = 0;
		ifp->if_lastex = nextents + ext_diff;
	}
	/*
	 * Otherwise use a linear (direct) extent list.
	 * If the extents are currently inside the inode,
	 * xfs_iext_realloc_direct will switch us from
	 * inline to direct extent allocation mode.
	 */
	else if (nextents + ext_diff <= XFS_LINEAR_EXTS) {
		xfs_iext_realloc_direct(ifp, new_size);
		if (idx < nextents) {
			memmove(&ifp->if_u1.if_extents[idx + ext_diff],
				&ifp->if_u1.if_extents[idx],
				(nextents - idx) * sizeof(xfs_bmbt_rec_t));
			memset(&ifp->if_u1.if_extents[idx], 0, byte_diff);
		}
	}
	/* Indirection array */
	else {
		xfs_ext_irec_t	*erp;
		int		erp_idx = 0;
		int		page_idx = idx;

		ASSERT(nextents + ext_diff > XFS_LINEAR_EXTS);
		if (ifp->if_flags & XFS_IFEXTIREC) {
			erp = xfs_iext_idx_to_irec(ifp, &page_idx, &erp_idx, 1);
		} else {
			xfs_iext_irec_init(ifp);
			ASSERT(ifp->if_flags & XFS_IFEXTIREC);
			erp = ifp->if_u1.if_ext_irec;
		}
		/* Extents fit in target extent page */
		if (erp && erp->er_extcount + ext_diff <= XFS_LINEAR_EXTS) {
			if (page_idx < erp->er_extcount) {
				memmove(&erp->er_extbuf[page_idx + ext_diff],
					&erp->er_extbuf[page_idx],
					(erp->er_extcount - page_idx) *
					sizeof(xfs_bmbt_rec_t));
				memset(&erp->er_extbuf[page_idx], 0, byte_diff);
			}
			erp->er_extcount += ext_diff;
			xfs_iext_irec_update_extoffs(ifp, erp_idx + 1, ext_diff);
		}
		/* Insert a new extent page */
		else if (erp) {
			xfs_iext_add_indirect_multi(ifp,
				erp_idx, page_idx, ext_diff);
		}
		/*
		 * If extent(s) are being appended to the last page in
		 * the indirection array and the new extent(s) don't fit
		 * in the page, then erp is NULL and erp_idx is set to
		 * the next index needed in the indirection array.
		 */
		else {
			int	count = ext_diff;

			while (count) {
				erp = xfs_iext_irec_new(ifp, erp_idx);
				erp->er_extcount = count;
				count -= MIN(count, (int)XFS_LINEAR_EXTS);
				if (count) {
					erp_idx++;
				}
			}
		}
	}
	ifp->if_bytes = new_size;
}

/*
 * This is called when incore extents are being added to the indirection
 * array and the new extents do not fit in the target extent list. The
 * erp_idx parameter contains the irec index for the target extent list
 * in the indirection array, and the idx parameter contains the extent
 * index within the list. The number of extents being added is stored
 * in the count parameter.
 *
 *    |-------|   |-------|
 *    |       |   |       |    idx - number of extents before idx
 *    |  idx  |   | count |
 *    |       |   |       |    count - number of extents being inserted at idx
 *    |-------|   |-------|
 *    | count |   | nex2  |    nex2 - number of extents after idx + count
 *    |-------|   |-------|
 */
void
xfs_iext_add_indirect_multi(
	xfs_ifork_t	*ifp,			/* inode fork pointer */
	int		erp_idx,		/* target extent irec index */
	xfs_extnum_t	idx,			/* index within target list */
	int		count)			/* new extents being added */
{
	int		byte_diff;		/* new bytes being added */
	xfs_ext_irec_t	*erp;			/* pointer to irec entry */
	xfs_extnum_t	ext_diff;		/* number of extents to add */
	xfs_extnum_t	ext_cnt;		/* new extents still needed */
	xfs_extnum_t	nex2;			/* extents after idx + count */
	xfs_bmbt_rec_t	*nex2_ep = NULL;	/* temp list for nex2 extents */
	int		nlists;			/* number of irec's (lists) */

	ASSERT(ifp->if_flags & XFS_IFEXTIREC);
	erp = &ifp->if_u1.if_ext_irec[erp_idx];
	nex2 = erp->er_extcount - idx;
	nlists = ifp->if_real_bytes / XFS_IEXT_BUFSZ;

	/*
	 * Save second part of target extent list
	 * (all extents past */
	if (nex2) {
		byte_diff = nex2 * sizeof(xfs_bmbt_rec_t);
		nex2_ep = (xfs_bmbt_rec_t *) kmem_alloc(byte_diff, KM_NOFS);
		memmove(nex2_ep, &erp->er_extbuf[idx], byte_diff);
		erp->er_extcount -= nex2;
		xfs_iext_irec_update_extoffs(ifp, erp_idx + 1, -nex2);
		memset(&erp->er_extbuf[idx], 0, byte_diff);
	}

	/*
	 * Add the new extents to the end of the target
	 * list, then allocate new irec record(s) and
	 * extent buffer(s) as needed to store the rest
	 * of the new extents.
	 */
	ext_cnt = count;
	ext_diff = MIN(ext_cnt, (int)XFS_LINEAR_EXTS - erp->er_extcount);
	if (ext_diff) {
		erp->er_extcount += ext_diff;
		xfs_iext_irec_update_extoffs(ifp, erp_idx + 1, ext_diff);
		ext_cnt -= ext_diff;
	}
	while (ext_cnt) {
		erp_idx++;
		erp = xfs_iext_irec_new(ifp, erp_idx);
		ext_diff = MIN(ext_cnt, (int)XFS_LINEAR_EXTS);
		erp->er_extcount = ext_diff;
		xfs_iext_irec_update_extoffs(ifp, erp_idx + 1, ext_diff);
		ext_cnt -= ext_diff;
	}

	/* Add nex2 extents back to indirection array */
	if (nex2) {
		xfs_extnum_t	ext_avail;
		int		i;

		byte_diff = nex2 * sizeof(xfs_bmbt_rec_t);
		ext_avail = XFS_LINEAR_EXTS - erp->er_extcount;
		i = 0;
		/*
		 * If nex2 extents fit in the current page, append
		 * nex2_ep after the new extents.
		 */
		if (nex2 <= ext_avail) {
			i = erp->er_extcount;
		}
		/*
		 * Otherwise, check if space is available in the
		 * next page.
		 */
		else if ((erp_idx < nlists - 1) &&
			 (nex2 <= (ext_avail = XFS_LINEAR_EXTS -
			  ifp->if_u1.if_ext_irec[erp_idx+1].er_extcount))) {
			erp_idx++;
			erp++;
			/* Create a hole for nex2 extents */
			memmove(&erp->er_extbuf[nex2], erp->er_extbuf,
				erp->er_extcount * sizeof(xfs_bmbt_rec_t));
		}
		/*
		 * Final choice, create a new extent page for
		 * nex2 extents.
		 */
		else {
			erp_idx++;
			erp = xfs_iext_irec_new(ifp, erp_idx);
		}
		memmove(&erp->er_extbuf[i], nex2_ep, byte_diff);
		kmem_free(nex2_ep);
		erp->er_extcount += nex2;
		xfs_iext_irec_update_extoffs(ifp, erp_idx + 1, nex2);
	}
}

/*
 * This is called when the amount of space required for incore file
 * extents needs to be decreased. The ext_diff parameter stores the
 * number of extents to be removed and the idx parameter contains
 * the extent index where the extents will be removed from.
 *
 * If the amount of space needed has decreased below the linear
 * limit, XFS_IEXT_BUFSZ, then switch to using the contiguous
 * extent array.  Otherwise, use kmem_realloc() to adjust the
 * size to what is needed.
 */
void
xfs_iext_remove(
	xfs_ifork_t	*ifp,		/* inode fork pointer */
	xfs_extnum_t	idx,		/* index to begin removing exts */
	int		ext_diff)	/* number of extents to remove */
{
	xfs_extnum_t	nextents;	/* number of extents in file */
	int		new_size;	/* size of extents after removal */

	ASSERT(ext_diff > 0);
	nextents = ifp->if_bytes / (uint)sizeof(xfs_bmbt_rec_t);
	new_size = (nextents - ext_diff) * sizeof(xfs_bmbt_rec_t);

	if (new_size == 0) {
		xfs_iext_destroy(ifp);
	} else if (ifp->if_flags & XFS_IFEXTIREC) {
		xfs_iext_remove_indirect(ifp, idx, ext_diff);
	} else if (ifp->if_real_bytes) {
		xfs_iext_remove_direct(ifp, idx, ext_diff);
	} else {
		xfs_iext_remove_inline(ifp, idx, ext_diff);
	}
	ifp->if_bytes = new_size;
}

/*
 * This removes ext_diff extents from the inline buffer, beginning
 * at extent index idx.
 */
void
xfs_iext_remove_inline(
	xfs_ifork_t	*ifp,		/* inode fork pointer */
	xfs_extnum_t	idx,		/* index to begin removing exts */
	int		ext_diff)	/* number of extents to remove */
{
	int		nextents;	/* number of extents in file */

	ASSERT(!(ifp->if_flags & XFS_IFEXTIREC));
	ASSERT(idx < XFS_INLINE_EXTS);
	nextents = ifp->if_bytes / (uint)sizeof(xfs_bmbt_rec_t);
	ASSERT(((nextents - ext_diff) > 0) &&
		(nextents - ext_diff) < XFS_INLINE_EXTS);

	if (idx + ext_diff < nextents) {
		memmove(&ifp->if_u2.if_inline_ext[idx],
			&ifp->if_u2.if_inline_ext[idx + ext_diff],
			(nextents - (idx + ext_diff)) *
			 sizeof(xfs_bmbt_rec_t));
		memset(&ifp->if_u2.if_inline_ext[nextents - ext_diff],
			0, ext_diff * sizeof(xfs_bmbt_rec_t));
	} else {
		memset(&ifp->if_u2.if_inline_ext[idx], 0,
			ext_diff * sizeof(xfs_bmbt_rec_t));
	}
}

/*
 * This removes ext_diff extents from a linear (direct) extent list,
 * beginning at extent index idx. If the extents are being removed
 * from the end of the list (ie. truncate) then we just need to re-
 * allocate the list to remove the extra space. Otherwise, if the
 * extents are being removed from the middle of the existing extent
 * entries, then we first need to move the extent records beginning
 * at idx + ext_diff up in the list to overwrite the records being
 * removed, then remove the extra space via kmem_realloc.
 */
void
xfs_iext_remove_direct(
	xfs_ifork_t	*ifp,		/* inode fork pointer */
	xfs_extnum_t	idx,		/* index to begin removing exts */
	int		ext_diff)	/* number of extents to remove */
{
	xfs_extnum_t	nextents;	/* number of extents in file */
	int		new_size;	/* size of extents after removal */

	ASSERT(!(ifp->if_flags & XFS_IFEXTIREC));
	new_size = ifp->if_bytes -
		(ext_diff * sizeof(xfs_bmbt_rec_t));
	nextents = ifp->if_bytes / (uint)sizeof(xfs_bmbt_rec_t);

	if (new_size == 0) {
		xfs_iext_destroy(ifp);
		return;
	}
	/* Move extents up in the list (if needed) */
	if (idx + ext_diff < nextents) {
		memmove(&ifp->if_u1.if_extents[idx],
			&ifp->if_u1.if_extents[idx + ext_diff],
			(nextents - (idx + ext_diff)) *
			 sizeof(xfs_bmbt_rec_t));
	}
	memset(&ifp->if_u1.if_extents[nextents - ext_diff],
		0, ext_diff * sizeof(xfs_bmbt_rec_t));
	/*
	 * Reallocate the direct extent list. If the extents
	 * will fit inside the inode then xfs_iext_realloc_direct
	 * will switch from direct to inline extent allocation
	 * mode for us.
	 */
	xfs_iext_realloc_direct(ifp, new_size);
	ifp->if_bytes = new_size;
}

/*
 * This is called when incore extents are being removed from the
 * indirection array and the extents being removed span multiple extent
 * buffers. The idx parameter contains the file extent index where we
 * want to begin removing extents, and the count parameter contains
 * how many extents need to be removed.
 *
 *    |-------|   |-------|
 *    | nex1  |   |       |    nex1 - number of extents before idx
 *    |-------|   | count |
 *    |       |   |       |    count - number of extents being removed at idx
 *    | count |   |-------|
 *    |       |   | nex2  |    nex2 - number of extents after idx + count
 *    |-------|   |-------|
 */
void
xfs_iext_remove_indirect(
	xfs_ifork_t	*ifp,		/* inode fork pointer */
	xfs_extnum_t	idx,		/* index to begin removing extents */
	int		count)		/* number of extents to remove */
{
	xfs_ext_irec_t	*erp;		/* indirection array pointer */
	int		erp_idx = 0;	/* indirection array index */
	xfs_extnum_t	ext_cnt;	/* extents left to remove */
	xfs_extnum_t	ext_diff;	/* extents to remove in current list */
	xfs_extnum_t	nex1;		/* number of extents before idx */
	xfs_extnum_t	nex2;		/* extents after idx + count */
	int		nlists;		/* entries in indirection array */
	int		page_idx = idx;	/* index in target extent list */

	ASSERT(ifp->if_flags & XFS_IFEXTIREC);
	erp = xfs_iext_idx_to_irec(ifp,  &page_idx, &erp_idx, 0);
	ASSERT(erp != NULL);
	nlists = ifp->if_real_bytes / XFS_IEXT_BUFSZ;
	nex1 = page_idx;
	ext_cnt = count;
	while (ext_cnt) {
		nex2 = MAX((erp->er_extcount - (nex1 + ext_cnt)), 0);
		ext_diff = MIN(ext_cnt, (erp->er_extcount - nex1));
		/*
		 * Check for deletion of entire list;
		 * xfs_iext_irec_remove() updates extent offsets.
		 */
		if (ext_diff == erp->er_extcount) {
			xfs_iext_irec_remove(ifp, erp_idx);
			ext_cnt -= ext_diff;
			nex1 = 0;
			if (ext_cnt) {
				ASSERT(erp_idx < ifp->if_real_bytes /
					XFS_IEXT_BUFSZ);
				erp = &ifp->if_u1.if_ext_irec[erp_idx];
				nex1 = 0;
				continue;
			} else {
				break;
			}
		}
		/* Move extents up (if needed) */
		if (nex2) {
			memmove(&erp->er_extbuf[nex1],
				&erp->er_extbuf[nex1 + ext_diff],
				nex2 * sizeof(xfs_bmbt_rec_t));
		}
		/* Zero out rest of page */
		memset(&erp->er_extbuf[nex1 + nex2], 0, (XFS_IEXT_BUFSZ -
			((nex1 + nex2) * sizeof(xfs_bmbt_rec_t))));
		/* Update remaining counters */
		erp->er_extcount -= ext_diff;
		xfs_iext_irec_update_extoffs(ifp, erp_idx + 1, -ext_diff);
		ext_cnt -= ext_diff;
		nex1 = 0;
		erp_idx++;
		erp++;
	}
	ifp->if_bytes -= count * sizeof(xfs_bmbt_rec_t);
	xfs_iext_irec_compact(ifp);
}

/*
 * Create, destroy, or resize a linear (direct) block of extents.
 */
void
xfs_iext_realloc_direct(
	xfs_ifork_t	*ifp,		/* inode fork pointer */
	int		new_size)	/* new size of extents */
{
	int		rnew_size;	/* real new size of extents */

	rnew_size = new_size;

	ASSERT(!(ifp->if_flags & XFS_IFEXTIREC) ||
		((new_size >= 0) && (new_size <= XFS_IEXT_BUFSZ) &&
		 (new_size != ifp->if_real_bytes)));

	/* Free extent records */
	if (new_size == 0) {
		xfs_iext_destroy(ifp);
	}
	/* Resize direct extent list and zero any new bytes */
	else if (ifp->if_real_bytes) {
		/* Check if extents will fit inside the inode */
		if (new_size <= XFS_INLINE_EXTS * sizeof(xfs_bmbt_rec_t)) {
			xfs_iext_direct_to_inline(ifp, new_size /
				(uint)sizeof(xfs_bmbt_rec_t));
			ifp->if_bytes = new_size;
			return;
		}
		if (!is_power_of_2(new_size)){
			rnew_size = roundup_pow_of_two(new_size);
		}
		if (rnew_size != ifp->if_real_bytes) {
			ifp->if_u1.if_extents =
				kmem_realloc(ifp->if_u1.if_extents,
						rnew_size,
						ifp->if_real_bytes, KM_NOFS);
		}
		if (rnew_size > ifp->if_real_bytes) {
			memset(&ifp->if_u1.if_extents[ifp->if_bytes /
				(uint)sizeof(xfs_bmbt_rec_t)], 0,
				rnew_size - ifp->if_real_bytes);
		}
	}
	/*
	 * Switch from the inline extent buffer to a direct
	 * extent list. Be sure to include the inline extent
	 * bytes in new_size.
	 */
	else {
		new_size += ifp->if_bytes;
		if (!is_power_of_2(new_size)) {
			rnew_size = roundup_pow_of_two(new_size);
		}
		xfs_iext_inline_to_direct(ifp, rnew_size);
	}
	ifp->if_real_bytes = rnew_size;
	ifp->if_bytes = new_size;
}

/*
 * Switch from linear (direct) extent records to inline buffer.
 */
void
xfs_iext_direct_to_inline(
	xfs_ifork_t	*ifp,		/* inode fork pointer */
	xfs_extnum_t	nextents)	/* number of extents in file */
{
	ASSERT(ifp->if_flags & XFS_IFEXTENTS);
	ASSERT(nextents <= XFS_INLINE_EXTS);
	/*
	 * The inline buffer was zeroed when we switched
	 * from inline to direct extent allocation mode,
	 * so we don't need to clear it here.
	 */
	memcpy(ifp->if_u2.if_inline_ext, ifp->if_u1.if_extents,
		nextents * sizeof(xfs_bmbt_rec_t));
	kmem_free(ifp->if_u1.if_extents);
	ifp->if_u1.if_extents = ifp->if_u2.if_inline_ext;
	ifp->if_real_bytes = 0;
}

/*
 * Switch from inline buffer to linear (direct) extent records.
 * new_size should already be rounded up to the next power of 2
 * by the caller (when appropriate), so use new_size as it is.
 * However, since new_size may be rounded up, we can't update
 * if_bytes here. It is the caller's responsibility to update
 * if_bytes upon return.
 */
void
xfs_iext_inline_to_direct(
	xfs_ifork_t	*ifp,		/* inode fork pointer */
	int		new_size)	/* number of extents in file */
{
	ifp->if_u1.if_extents = kmem_alloc(new_size, KM_NOFS);
	memset(ifp->if_u1.if_extents, 0, new_size);
	if (ifp->if_bytes) {
		memcpy(ifp->if_u1.if_extents, ifp->if_u2.if_inline_ext,
			ifp->if_bytes);
		memset(ifp->if_u2.if_inline_ext, 0, XFS_INLINE_EXTS *
			sizeof(xfs_bmbt_rec_t));
	}
	ifp->if_real_bytes = new_size;
}

/*
 * Resize an extent indirection array to new_size bytes.
 */
void
xfs_iext_realloc_indirect(
	xfs_ifork_t	*ifp,		/* inode fork pointer */
	int		new_size)	/* new indirection array size */
{
	int		nlists;		/* number of irec's (ex lists) */
	int		size;		/* current indirection array size */

	ASSERT(ifp->if_flags & XFS_IFEXTIREC);
	nlists = ifp->if_real_bytes / XFS_IEXT_BUFSZ;
	size = nlists * sizeof(xfs_ext_irec_t);
	ASSERT(ifp->if_real_bytes);
	ASSERT((new_size >= 0) && (new_size != size));
	if (new_size == 0) {
		xfs_iext_destroy(ifp);
	} else {
		ifp->if_u1.if_ext_irec = (xfs_ext_irec_t *)
			kmem_realloc(ifp->if_u1.if_ext_irec,
				new_size, size, KM_NOFS);
	}
}

/*
 * Switch from indirection array to linear (direct) extent allocations.
 */
void
xfs_iext_indirect_to_direct(
	 xfs_ifork_t	*ifp)		/* inode fork pointer */
{
	xfs_bmbt_rec_host_t *ep;	/* extent record pointer */
	xfs_extnum_t	nextents;	/* number of extents in file */
	int		size;		/* size of file extents */

	ASSERT(ifp->if_flags & XFS_IFEXTIREC);
	nextents = ifp->if_bytes / (uint)sizeof(xfs_bmbt_rec_t);
	ASSERT(nextents <= XFS_LINEAR_EXTS);
	size = nextents * sizeof(xfs_bmbt_rec_t);

	xfs_iext_irec_compact_pages(ifp);
	ASSERT(ifp->if_real_bytes == XFS_IEXT_BUFSZ);

	ep = ifp->if_u1.if_ext_irec->er_extbuf;
	kmem_free(ifp->if_u1.if_ext_irec);
	ifp->if_flags &= ~XFS_IFEXTIREC;
	ifp->if_u1.if_extents = ep;
	ifp->if_bytes = size;
	if (nextents < XFS_LINEAR_EXTS) {
		xfs_iext_realloc_direct(ifp, size);
	}
}

/*
 * Free incore file extents.
 */
void
xfs_iext_destroy(
	xfs_ifork_t	*ifp)		/* inode fork pointer */
{
	if (ifp->if_flags & XFS_IFEXTIREC) {
		int	erp_idx;
		int	nlists;

		nlists = ifp->if_real_bytes / XFS_IEXT_BUFSZ;
		for (erp_idx = nlists - 1; erp_idx >= 0 ; erp_idx--) {
			xfs_iext_irec_remove(ifp, erp_idx);
		}
		ifp->if_flags &= ~XFS_IFEXTIREC;
	} else if (ifp->if_real_bytes) {
		kmem_free(ifp->if_u1.if_extents);
	} else if (ifp->if_bytes) {
		memset(ifp->if_u2.if_inline_ext, 0, XFS_INLINE_EXTS *
			sizeof(xfs_bmbt_rec_t));
	}
	ifp->if_u1.if_extents = NULL;
	ifp->if_real_bytes = 0;
	ifp->if_bytes = 0;
}

/*
 * Return a pointer to the extent record for file system block bno.
 */
xfs_bmbt_rec_host_t *			/* pointer to found extent record */
xfs_iext_bno_to_ext(
	xfs_ifork_t	*ifp,		/* inode fork pointer */
	xfs_fileoff_t	bno,		/* block number to search for */
	xfs_extnum_t	*idxp)		/* index of target extent */
{
	xfs_bmbt_rec_host_t *base;	/* pointer to first extent */
	xfs_filblks_t	blockcount = 0;	/* number of blocks in extent */
	xfs_bmbt_rec_host_t *ep = NULL;	/* pointer to target extent */
	xfs_ext_irec_t	*erp = NULL;	/* indirection array pointer */
	int		high;		/* upper boundary in search */
	xfs_extnum_t	idx = 0;	/* index of target extent */
	int		low;		/* lower boundary in search */
	xfs_extnum_t	nextents;	/* number of file extents */
	xfs_fileoff_t	startoff = 0;	/* start offset of extent */

	nextents = ifp->if_bytes / (uint)sizeof(xfs_bmbt_rec_t);
	if (nextents == 0) {
		*idxp = 0;
		return NULL;
	}
	low = 0;
	if (ifp->if_flags & XFS_IFEXTIREC) {
		/* Find target extent list */
		int	erp_idx = 0;
		erp = xfs_iext_bno_to_irec(ifp, bno, &erp_idx);
		base = erp->er_extbuf;
		high = erp->er_extcount - 1;
	} else {
		base = ifp->if_u1.if_extents;
		high = nextents - 1;
	}
	/* Binary search extent records */
	while (low <= high) {
		idx = (low + high) >> 1;
		ep = base + idx;
		startoff = xfs_bmbt_get_startoff(ep);
		blockcount = xfs_bmbt_get_blockcount(ep);
		if (bno < startoff) {
			high = idx - 1;
		} else if (bno >= startoff + blockcount) {
			low = idx + 1;
		} else {
			/* Convert back to file-based extent index */
			if (ifp->if_flags & XFS_IFEXTIREC) {
				idx += erp->er_extoff;
			}
			*idxp = idx;
			return ep;
		}
	}
	/* Convert back to file-based extent index */
	if (ifp->if_flags & XFS_IFEXTIREC) {
		idx += erp->er_extoff;
	}
	if (bno >= startoff + blockcount) {
		if (++idx == nextents) {
			ep = NULL;
		} else {
			ep = xfs_iext_get_ext(ifp, idx);
		}
	}
	*idxp = idx;
	return ep;
}

/*
 * Return a pointer to the indirection array entry containing the
 * extent record for filesystem block bno. Store the index of the
 * target irec in *erp_idxp.
 */
xfs_ext_irec_t *			/* pointer to found extent record */
xfs_iext_bno_to_irec(
	xfs_ifork_t	*ifp,		/* inode fork pointer */
	xfs_fileoff_t	bno,		/* block number to search for */
	int		*erp_idxp)	/* irec index of target ext list */
{
	xfs_ext_irec_t	*erp = NULL;	/* indirection array pointer */
	xfs_ext_irec_t	*erp_next;	/* next indirection array entry */
	int		erp_idx;	/* indirection array index */
	int		nlists;		/* number of extent irec's (lists) */
	int		high;		/* binary search upper limit */
	int		low;		/* binary search lower limit */

	ASSERT(ifp->if_flags & XFS_IFEXTIREC);
	nlists = ifp->if_real_bytes / XFS_IEXT_BUFSZ;
	erp_idx = 0;
	low = 0;
	high = nlists - 1;
	while (low <= high) {
		erp_idx = (low + high) >> 1;
		erp = &ifp->if_u1.if_ext_irec[erp_idx];
		erp_next = erp_idx < nlists - 1 ? erp + 1 : NULL;
		if (bno < xfs_bmbt_get_startoff(erp->er_extbuf)) {
			high = erp_idx - 1;
		} else if (erp_next && bno >=
			   xfs_bmbt_get_startoff(erp_next->er_extbuf)) {
			low = erp_idx + 1;
		} else {
			break;
		}
	}
	*erp_idxp = erp_idx;
	return erp;
}

/*
 * Return a pointer to the indirection array entry containing the
 * extent record at file extent index *idxp. Store the index of the
 * target irec in *erp_idxp and store the page index of the target
 * extent record in *idxp.
 */
xfs_ext_irec_t *
xfs_iext_idx_to_irec(
	xfs_ifork_t	*ifp,		/* inode fork pointer */
	xfs_extnum_t	*idxp,		/* extent index (file -> page) */
	int		*erp_idxp,	/* pointer to target irec */
	int		realloc)	/* new bytes were just added */
{
	xfs_ext_irec_t	*prev;		/* pointer to previous irec */
	xfs_ext_irec_t	*erp = NULL;	/* pointer to current irec */
	int		erp_idx;	/* indirection array index */
	int		nlists;		/* number of irec's (ex lists) */
	int		high;		/* binary search upper limit */
	int		low;		/* binary search lower limit */
	xfs_extnum_t	page_idx = *idxp; /* extent index in target list */

	ASSERT(ifp->if_flags & XFS_IFEXTIREC);
	ASSERT(page_idx >= 0 && page_idx <=
		ifp->if_bytes / (uint)sizeof(xfs_bmbt_rec_t));
	nlists = ifp->if_real_bytes / XFS_IEXT_BUFSZ;
	erp_idx = 0;
	low = 0;
	high = nlists - 1;

	/* Binary search extent irec's */
	while (low <= high) {
		erp_idx = (low + high) >> 1;
		erp = &ifp->if_u1.if_ext_irec[erp_idx];
		prev = erp_idx > 0 ? erp - 1 : NULL;
		if (page_idx < erp->er_extoff || (page_idx == erp->er_extoff &&
		     realloc && prev && prev->er_extcount < XFS_LINEAR_EXTS)) {
			high = erp_idx - 1;
		} else if (page_idx > erp->er_extoff + erp->er_extcount ||
			   (page_idx == erp->er_extoff + erp->er_extcount &&
			    !realloc)) {
			low = erp_idx + 1;
		} else if (page_idx == erp->er_extoff + erp->er_extcount &&
			   erp->er_extcount == XFS_LINEAR_EXTS) {
			ASSERT(realloc);
			page_idx = 0;
			erp_idx++;
			erp = erp_idx < nlists ? erp + 1 : NULL;
			break;
		} else {
			page_idx -= erp->er_extoff;
			break;
		}
	}
	*idxp = page_idx;
	*erp_idxp = erp_idx;
	return(erp);
}

/*
 * Allocate and initialize an indirection array once the space needed
 * for incore extents increases above XFS_IEXT_BUFSZ.
 */
void
xfs_iext_irec_init(
	xfs_ifork_t	*ifp)		/* inode fork pointer */
{
	xfs_ext_irec_t	*erp;		/* indirection array pointer */
	xfs_extnum_t	nextents;	/* number of extents in file */

	ASSERT(!(ifp->if_flags & XFS_IFEXTIREC));
	nextents = ifp->if_bytes / (uint)sizeof(xfs_bmbt_rec_t);
	ASSERT(nextents <= XFS_LINEAR_EXTS);

	erp = kmem_alloc(sizeof(xfs_ext_irec_t), KM_NOFS);

	if (nextents == 0) {
		ifp->if_u1.if_extents = kmem_alloc(XFS_IEXT_BUFSZ, KM_NOFS);
	} else if (!ifp->if_real_bytes) {
		xfs_iext_inline_to_direct(ifp, XFS_IEXT_BUFSZ);
	} else if (ifp->if_real_bytes < XFS_IEXT_BUFSZ) {
		xfs_iext_realloc_direct(ifp, XFS_IEXT_BUFSZ);
	}
	erp->er_extbuf = ifp->if_u1.if_extents;
	erp->er_extcount = nextents;
	erp->er_extoff = 0;

	ifp->if_flags |= XFS_IFEXTIREC;
	ifp->if_real_bytes = XFS_IEXT_BUFSZ;
	ifp->if_bytes = nextents * sizeof(xfs_bmbt_rec_t);
	ifp->if_u1.if_ext_irec = erp;

	return;
}

/*
 * Allocate and initialize a new entry in the indirection array.
 */
xfs_ext_irec_t *
xfs_iext_irec_new(
	xfs_ifork_t	*ifp,		/* inode fork pointer */
	int		erp_idx)	/* index for new irec */
{
	xfs_ext_irec_t	*erp;		/* indirection array pointer */
	int		i;		/* loop counter */
	int		nlists;		/* number of irec's (ex lists) */

	ASSERT(ifp->if_flags & XFS_IFEXTIREC);
	nlists = ifp->if_real_bytes / XFS_IEXT_BUFSZ;

	/* Resize indirection array */
	xfs_iext_realloc_indirect(ifp, ++nlists *
				  sizeof(xfs_ext_irec_t));
	/*
	 * Move records down in the array so the
	 * new page can use erp_idx.
	 */
	erp = ifp->if_u1.if_ext_irec;
	for (i = nlists - 1; i > erp_idx; i--) {
		memmove(&erp[i], &erp[i-1], sizeof(xfs_ext_irec_t));
	}
	ASSERT(i == erp_idx);

	/* Initialize new extent record */
	erp = ifp->if_u1.if_ext_irec;
	erp[erp_idx].er_extbuf = kmem_alloc(XFS_IEXT_BUFSZ, KM_NOFS);
	ifp->if_real_bytes = nlists * XFS_IEXT_BUFSZ;
	memset(erp[erp_idx].er_extbuf, 0, XFS_IEXT_BUFSZ);
	erp[erp_idx].er_extcount = 0;
	erp[erp_idx].er_extoff = erp_idx > 0 ?
		erp[erp_idx-1].er_extoff + erp[erp_idx-1].er_extcount : 0;
	return (&erp[erp_idx]);
}

/*
 * Remove a record from the indirection array.
 */
void
xfs_iext_irec_remove(
	xfs_ifork_t	*ifp,		/* inode fork pointer */
	int		erp_idx)	/* irec index to remove */
{
	xfs_ext_irec_t	*erp;		/* indirection array pointer */
	int		i;		/* loop counter */
	int		nlists;		/* number of irec's (ex lists) */

	ASSERT(ifp->if_flags & XFS_IFEXTIREC);
	nlists = ifp->if_real_bytes / XFS_IEXT_BUFSZ;
	erp = &ifp->if_u1.if_ext_irec[erp_idx];
	if (erp->er_extbuf) {
		xfs_iext_irec_update_extoffs(ifp, erp_idx + 1,
			-erp->er_extcount);
		kmem_free(erp->er_extbuf);
	}
	/* Compact extent records */
	erp = ifp->if_u1.if_ext_irec;
	for (i = erp_idx; i < nlists - 1; i++) {
		memmove(&erp[i], &erp[i+1], sizeof(xfs_ext_irec_t));
	}
	/*
	 * Manually free the last extent record from the indirection
	 * array.  A call to xfs_iext_realloc_indirect() with a size
	 * of zero would result in a call to xfs_iext_destroy() which
	 * would in turn call this function again, creating a nasty
	 * infinite loop.
	 */
	if (--nlists) {
		xfs_iext_realloc_indirect(ifp,
			nlists * sizeof(xfs_ext_irec_t));
	} else {
		kmem_free(ifp->if_u1.if_ext_irec);
	}
	ifp->if_real_bytes = nlists * XFS_IEXT_BUFSZ;
}

/*
 * This is called to clean up large amounts of unused memory allocated
 * by the indirection array.  Before compacting anything though, verify
 * that the indirection array is still needed and switch back to the
 * linear extent list (or even the inline buffer) if possible.  The
 * compaction policy is as follows:
 *
 *    Full Compaction: Extents fit into a single page (or inline buffer)
 * Partial Compaction: Extents occupy less than 50% of allocated space
 *      No Compaction: Extents occupy at least 50% of allocated space
 */
void
xfs_iext_irec_compact(
	xfs_ifork_t	*ifp)		/* inode fork pointer */
{
	xfs_extnum_t	nextents;	/* number of extents in file */
	int		nlists;		/* number of irec's (ex lists) */

	ASSERT(ifp->if_flags & XFS_IFEXTIREC);
	nlists = ifp->if_real_bytes / XFS_IEXT_BUFSZ;
	nextents = ifp->if_bytes / (uint)sizeof(xfs_bmbt_rec_t);

	if (nextents == 0) {
		xfs_iext_destroy(ifp);
	} else if (nextents <= XFS_INLINE_EXTS) {
		xfs_iext_indirect_to_direct(ifp);
		xfs_iext_direct_to_inline(ifp, nextents);
	} else if (nextents <= XFS_LINEAR_EXTS) {
		xfs_iext_indirect_to_direct(ifp);
	} else if (nextents < (nlists * XFS_LINEAR_EXTS) >> 1) {
		xfs_iext_irec_compact_pages(ifp);
	}
}

/*
 * Combine extents from neighboring extent pages.
 */
void
xfs_iext_irec_compact_pages(
	xfs_ifork_t	*ifp)		/* inode fork pointer */
{
	xfs_ext_irec_t	*erp, *erp_next;/* pointers to irec entries */
	int		erp_idx = 0;	/* indirection array index */
	int		nlists;		/* number of irec's (ex lists) */

	ASSERT(ifp->if_flags & XFS_IFEXTIREC);
	nlists = ifp->if_real_bytes / XFS_IEXT_BUFSZ;
	while (erp_idx < nlists - 1) {
		erp = &ifp->if_u1.if_ext_irec[erp_idx];
		erp_next = erp + 1;
		if (erp_next->er_extcount <=
		    (XFS_LINEAR_EXTS - erp->er_extcount)) {
			memcpy(&erp->er_extbuf[erp->er_extcount],
				erp_next->er_extbuf, erp_next->er_extcount *
				sizeof(xfs_bmbt_rec_t));
			erp->er_extcount += erp_next->er_extcount;
			/*
			 * Free page before removing extent record
			 * so er_extoffs don't get modified in
			 * xfs_iext_irec_remove.
			 */
			kmem_free(erp_next->er_extbuf);
			erp_next->er_extbuf = NULL;
			xfs_iext_irec_remove(ifp, erp_idx + 1);
			nlists = ifp->if_real_bytes / XFS_IEXT_BUFSZ;
		} else {
			erp_idx++;
		}
	}
}

/*
 * This is called to update the er_extoff field in the indirection
 * array when extents have been added or removed from one of the
 * extent lists. erp_idx contains the irec index to begin updating
 * at and ext_diff contains the number of extents that were added
 * or removed.
 */
void
xfs_iext_irec_update_extoffs(
	xfs_ifork_t	*ifp,		/* inode fork pointer */
	int		erp_idx,	/* irec index to update */
	int		ext_diff)	/* number of new extents */
{
	int		i;		/* loop counter */
	int		nlists;		/* number of irec's (ex lists */

	ASSERT(ifp->if_flags & XFS_IFEXTIREC);
	nlists = ifp->if_real_bytes / XFS_IEXT_BUFSZ;
	for (i = erp_idx; i < nlists; i++) {
		ifp->if_u1.if_ext_irec[i].er_extoff += ext_diff;
	}
	xfs_iflush_fork(ip, dip, iip, XFS_DATA_FORK);
	if (XFS_IFORK_Q(ip))
		xfs_iflush_fork(ip, dip, iip, XFS_ATTR_FORK);
	xfs_inobp_check(mp, bp);

	/*
	 * We've recorded everything logged in the inode, so we'd like to clear
	 * the ili_fields bits so we don't log and flush things unnecessarily.
	 * However, we can't stop logging all this information until the data
	 * we've copied into the disk buffer is written to disk.  If we did we
	 * might overwrite the copy of the inode in the log with all the data
	 * after re-logging only part of it, and in the face of a crash we
	 * wouldn't have all the data we need to recover.
	 *
	 * What we do is move the bits to the ili_last_fields field.  When
	 * logging the inode, these bits are moved back to the ili_fields field.
	 * In the xfs_iflush_done() routine we clear ili_last_fields, since we
	 * know that the information those bits represent is permanently on
	 * disk.  As long as the flush completes before the inode is logged
	 * again, then both ili_fields and ili_last_fields will be cleared.
	 *
	 * We can play with the ili_fields bits here, because the inode lock
	 * must be held exclusively in order to set bits there and the flush
	 * lock protects the ili_last_fields bits.  Set ili_logged so the flush
	 * done routine can tell whether or not to look in the AIL.  Also, store
	 * the current LSN of the inode so that we can tell whether the item has
	 * moved in the AIL from xfs_iflush_done().  In order to read the lsn we
	 * need the AIL lock, because it is a 64 bit value that cannot be read
	 * atomically.
	 */
	iip->ili_last_fields = iip->ili_fields;
	iip->ili_fields = 0;
	iip->ili_fsync_fields = 0;
	iip->ili_logged = 1;

	xfs_trans_ail_copy_lsn(mp->m_ail, &iip->ili_flush_lsn,
				&iip->ili_item.li_lsn);

	/*
	 * Attach the function xfs_iflush_done to the inode's
	 * buffer.  This will remove the inode from the AIL
	 * and unlock the inode's flush lock when the inode is
	 * completely written to disk.
	 */
	xfs_buf_attach_iodone(bp, xfs_iflush_done, &iip->ili_item);

	/* update the lsn in the on disk inode if required */
	if (ip->i_d.di_version == 3)
		dip->di_lsn = cpu_to_be64(iip->ili_item.li_lsn);

	/* generate the checksum. */
	xfs_dinode_calc_crc(mp, dip);

	ASSERT(bp->b_fspriv != NULL);
	ASSERT(bp->b_iodone != NULL);
	return 0;

corrupt_out:
	return -EFSCORRUPTED;
}
