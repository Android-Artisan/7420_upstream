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
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_types.h"
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_buf_item.h"
#include "xfs_trans_priv.h"
#include "xfs_error.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_bit.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_trans.h"
#include "xfs_buf_item.h"
#include "xfs_trans_priv.h"
#include "xfs_error.h"
#include "xfs_trace.h"
#include "xfs_log.h"


kmem_zone_t	*xfs_buf_item_zone;

#ifdef XFS_TRANS_DEBUG
/*
 * This function uses an alternate strategy for tracking the bytes
 * that the user requests to be logged.  This can then be used
 * in conjunction with the bli_orig array in the buf log item to
 * catch bugs in our callers' code.
 *
 * We also double check the bits set in xfs_buf_item_log using a
 * simple algorithm to check that every byte is accounted for.
 */
STATIC void
xfs_buf_item_log_debug(
	xfs_buf_log_item_t	*bip,
	uint			first,
	uint			last)
{
	uint	x;
	uint	byte;
	uint	nbytes;
	uint	chunk_num;
	uint	word_num;
	uint	bit_num;
	uint	bit_set;
	uint	*wordp;

	ASSERT(bip->bli_logged != NULL);
	byte = first;
	nbytes = last - first + 1;
	bfset(bip->bli_logged, first, nbytes);
	for (x = 0; x < nbytes; x++) {
		chunk_num = byte >> XFS_BLI_SHIFT;
		word_num = chunk_num >> BIT_TO_WORD_SHIFT;
		bit_num = chunk_num & (NBWORD - 1);
		wordp = &(bip->bli_format.blf_data_map[word_num]);
		bit_set = *wordp & (1 << bit_num);
		ASSERT(bit_set);
		byte++;
	}
}

/*
 * This function is called when we flush something into a buffer without
 * logging it.  This happens for things like inodes which are logged
 * separately from the buffer.
 */
void
xfs_buf_item_flush_log_debug(
	xfs_buf_t	*bp,
	uint		first,
	uint		last)
{
	xfs_buf_log_item_t	*bip;
	uint			nbytes;

	bip = XFS_BUF_FSPRIVATE(bp, xfs_buf_log_item_t*);
	if ((bip == NULL) || (bip->bli_item.li_type != XFS_LI_BUF)) {
		return;
	}

	ASSERT(bip->bli_logged != NULL);
	nbytes = last - first + 1;
	bfset(bip->bli_logged, first, nbytes);
}

/*
 * This function is called to verify that our callers have logged
 * all the bytes that they changed.
 *
 * It does this by comparing the original copy of the buffer stored in
 * the buf log item's bli_orig array to the current copy of the buffer
 * and ensuring that all bytes which mismatch are set in the bli_logged
 * array of the buf log item.
 */
STATIC void
xfs_buf_item_log_check(
	xfs_buf_log_item_t	*bip)
{
	char		*orig;
	char		*buffer;
	int		x;
	xfs_buf_t	*bp;

	ASSERT(bip->bli_orig != NULL);
	ASSERT(bip->bli_logged != NULL);

	bp = bip->bli_buf;
	ASSERT(XFS_BUF_COUNT(bp) > 0);
	ASSERT(XFS_BUF_PTR(bp) != NULL);
	orig = bip->bli_orig;
	buffer = XFS_BUF_PTR(bp);
	for (x = 0; x < XFS_BUF_COUNT(bp); x++) {
		if (orig[x] != buffer[x] && !btst(bip->bli_logged, x))
			cmn_err(CE_PANIC,
	"xfs_buf_item_log_check bip %x buffer %x orig %x index %d",
				bip, bp, orig, x);
	}
}
#else
#define		xfs_buf_item_log_debug(x,y,z)
#define		xfs_buf_item_log_check(x)
#endif

STATIC void	xfs_buf_error_relse(xfs_buf_t *bp);
STATIC void	xfs_buf_do_callbacks(xfs_buf_t *bp, xfs_log_item_t *lip);

/*
static inline struct xfs_buf_log_item *BUF_ITEM(struct xfs_log_item *lip)
{
	return container_of(lip, struct xfs_buf_log_item, bli_item);
}

STATIC void	xfs_buf_do_callbacks(struct xfs_buf *bp);

static inline int
xfs_buf_log_format_size(
	struct xfs_buf_log_format *blfp)
{
	return offsetof(struct xfs_buf_log_format, blf_data_map) +
			(blfp->blf_map_size * sizeof(blfp->blf_data_map[0]));
}

/*
 * This returns the number of log iovecs needed to log the
 * given buf log item.
 *
 * It calculates this as 1 iovec for the buf log format structure
 * and 1 for each stretch of non-contiguous chunks to be logged.
 * Contiguous chunks are logged in a single iovec.
 *
 * If the XFS_BLI_STALE flag has been set, then log nothing.
 */
STATIC uint
xfs_buf_item_size(
	xfs_buf_log_item_t	*bip)
{
	uint		nvecs;
	int		next_bit;
	int		last_bit;
	xfs_buf_t	*bp;
STATIC void
xfs_buf_item_size_segment(
	struct xfs_buf_log_item	*bip,
	struct xfs_buf_log_format *blfp,
	int			*nvecs,
	int			*nbytes)
{
	struct xfs_buf		*bp = bip->bli_buf;
	int			next_bit;
	int			last_bit;

	last_bit = xfs_next_bit(blfp->blf_data_map, blfp->blf_map_size, 0);
	if (last_bit == -1)
		return;

	/*
	 * initial count for a dirty buffer is 2 vectors - the format structure
	 * and the first dirty region.
	 */
	*nvecs += 2;
	*nbytes += xfs_buf_log_format_size(blfp) + XFS_BLF_CHUNK;

	while (last_bit != -1) {
		/*
		 * This takes the bit number to start looking from and
		 * returns the next set bit from there.  It returns -1
		 * if there are no more bits set or the start bit is
		 * beyond the end of the bitmap.
		 */
		next_bit = xfs_next_bit(blfp->blf_data_map, blfp->blf_map_size,
					last_bit + 1);
		/*
		 * If we run out of bits, leave the loop,
		 * else if we find a new set of bits bump the number of vecs,
		 * else keep scanning the current set of bits.
		 */
		if (next_bit == -1) {
			break;
		} else if (next_bit != last_bit + 1) {
			last_bit = next_bit;
			(*nvecs)++;
		} else if (xfs_buf_offset(bp, next_bit * XFS_BLF_CHUNK) !=
			   (xfs_buf_offset(bp, last_bit * XFS_BLF_CHUNK) +
			    XFS_BLF_CHUNK)) {
			last_bit = next_bit;
			(*nvecs)++;
		} else {
			last_bit++;
		}
		*nbytes += XFS_BLF_CHUNK;
	}
}

/*
 * This returns the number of log iovecs needed to log the given buf log item.
 *
 * It calculates this as 1 iovec for the buf log format structure and 1 for each
 * stretch of non-contiguous chunks to be logged.  Contiguous chunks are logged
 * in a single iovec.
 *
 * Discontiguous buffers need a format structure per region that that is being
 * logged. This makes the changes in the buffer appear to log recovery as though
 * they came from separate buffers, just like would occur if multiple buffers
 * were used instead of a single discontiguous buffer. This enables
 * discontiguous buffers to be in-memory constructs, completely transparent to
 * what ends up on disk.
 *
 * If the XFS_BLI_STALE flag has been set, then log nothing but the buf log
 * format structures.
 */
STATIC void
xfs_buf_item_size(
	struct xfs_log_item	*lip,
	int			*nvecs,
	int			*nbytes)
{
	struct xfs_buf_log_item	*bip = BUF_ITEM(lip);
	int			i;

	ASSERT(atomic_read(&bip->bli_refcount) > 0);
	if (bip->bli_flags & XFS_BLI_STALE) {
		/*
		 * The buffer is stale, so all we need to log
		 * is the buf log format structure with the
		 * cancel flag in it.
		 */
		xfs_buf_item_trace("SIZE STALE", bip);
		ASSERT(bip->bli_format.blf_flags & XFS_BLI_CANCEL);
		return 1;
	}

	bp = bip->bli_buf;
	ASSERT(bip->bli_flags & XFS_BLI_LOGGED);
	nvecs = 1;
	last_bit = xfs_next_bit(bip->bli_format.blf_data_map,
					 bip->bli_format.blf_map_size, 0);
	ASSERT(last_bit != -1);
	nvecs++;
	while (last_bit != -1) {
		trace_xfs_buf_item_size_stale(bip);
		ASSERT(bip->__bli_format.blf_flags & XFS_BLF_CANCEL);
		*nvecs += bip->bli_format_count;
		for (i = 0; i < bip->bli_format_count; i++) {
			*nbytes += xfs_buf_log_format_size(&bip->bli_formats[i]);
		}
		return;
	}

	ASSERT(bip->bli_flags & XFS_BLI_LOGGED);

	if (bip->bli_flags & XFS_BLI_ORDERED) {
		/*
		 * The buffer has been logged just to order it.
		 * It is not being included in the transaction
		 * commit, so no vectors are used at all.
		 */
		trace_xfs_buf_item_size_ordered(bip);
		*nvecs = XFS_LOG_VEC_ORDERED;
		return;
	}

	/*
	 * the vector count is based on the number of buffer vectors we have
	 * dirty bits in. This will only be greater than one when we have a
	 * compound buffer with more than one segment dirty. Hence for compound
	 * buffers we need to track which segment the dirty bits correspond to,
	 * and when we move from one segment to the next increment the vector
	 * count for the extra buf log format structure that will need to be
	 * written.
	 */
	for (i = 0; i < bip->bli_format_count; i++) {
		xfs_buf_item_size_segment(bip, &bip->bli_formats[i],
					  nvecs, nbytes);
	}
	trace_xfs_buf_item_size(bip);
}

static inline void
xfs_buf_item_copy_iovec(
	struct xfs_log_vec	*lv,
	struct xfs_log_iovec	**vecp,
	struct xfs_buf		*bp,
	uint			offset,
	int			first_bit,
	uint			nbits)
{
	offset += first_bit * XFS_BLF_CHUNK;
	xlog_copy_iovec(lv, vecp, XLOG_REG_TYPE_BCHUNK,
			xfs_buf_offset(bp, offset),
			nbits * XFS_BLF_CHUNK);
}

static inline bool
xfs_buf_item_straddle(
	struct xfs_buf		*bp,
	uint			offset,
	int			next_bit,
	int			last_bit)
{
	return xfs_buf_offset(bp, offset + (next_bit << XFS_BLF_SHIFT)) !=
		(xfs_buf_offset(bp, offset + (last_bit << XFS_BLF_SHIFT)) +
		 XFS_BLF_CHUNK);
}

static void
xfs_buf_item_format_segment(
	struct xfs_buf_log_item	*bip,
	struct xfs_log_vec	*lv,
	struct xfs_log_iovec	**vecp,
	uint			offset,
	struct xfs_buf_log_format *blfp)
{
	struct xfs_buf	*bp = bip->bli_buf;
	uint		base_size;
	int		first_bit;
	int		last_bit;
	int		next_bit;
	uint		nbits;

	/* copy the flags across from the base format item */
	blfp->blf_flags = bip->__bli_format.blf_flags;

	/*
	 * Base size is the actual size of the ondisk structure - it reflects
	 * the actual size of the dirty bitmap rather than the size of the in
	 * memory structure.
	 */
	base_size = xfs_buf_log_format_size(blfp);

	first_bit = xfs_next_bit(blfp->blf_data_map, blfp->blf_map_size, 0);
	if (!(bip->bli_flags & XFS_BLI_STALE) && first_bit == -1) {
		/*
		 * If the map is not be dirty in the transaction, mark
		 * the size as zero and do not advance the vector pointer.
		 */
		return;
	}

	blfp = xlog_copy_iovec(lv, vecp, XLOG_REG_TYPE_BFORMAT, blfp, base_size);
	blfp->blf_size = 1;

	if (bip->bli_flags & XFS_BLI_STALE) {
		/*
		 * The buffer is stale, so all we need to log
		 * is the buf log format structure with the
		 * cancel flag in it.
		 */
		trace_xfs_buf_item_format_stale(bip);
		ASSERT(blfp->blf_flags & XFS_BLF_CANCEL);
		return;
	}


	/*
	 * Fill in an iovec for each set of contiguous chunks.
	 */
	last_bit = first_bit;
	nbits = 1;
	for (;;) {
		/*
		 * This takes the bit number to start looking from and
		 * returns the next set bit from there.  It returns -1
		 * if there are no more bits set or the start bit is
		 * beyond the end of the bitmap.
		 */
		next_bit = xfs_next_bit(bip->bli_format.blf_data_map,
						 bip->bli_format.blf_map_size,
						 last_bit + 1);
		/*
		 * If we run out of bits, leave the loop,
		 * else if we find a new set of bits bump the number of vecs,
		 * else keep scanning the current set of bits.
		 */
		if (next_bit == -1) {
			last_bit = -1;
		} else if (next_bit != last_bit + 1) {
			last_bit = next_bit;
			nvecs++;
		} else if (xfs_buf_offset(bp, next_bit * XFS_BLI_CHUNK) !=
			   (xfs_buf_offset(bp, last_bit * XFS_BLI_CHUNK) +
			    XFS_BLI_CHUNK)) {
			last_bit = next_bit;
			nvecs++;
		} else {
			last_bit++;
		}
	}

	xfs_buf_item_trace("SIZE NORM", bip);
	return nvecs;
		next_bit = xfs_next_bit(blfp->blf_data_map, blfp->blf_map_size,
					(uint)last_bit + 1);
		/*
		 * If we run out of bits fill in the last iovec and get out of
		 * the loop.  Else if we start a new set of bits then fill in
		 * the iovec for the series we were looking at and start
		 * counting the bits in the new one.  Else we're still in the
		 * same set of bits so just keep counting and scanning.
		 */
		if (next_bit == -1) {
			xfs_buf_item_copy_iovec(lv, vecp, bp, offset,
						first_bit, nbits);
			blfp->blf_size++;
			break;
		} else if (next_bit != last_bit + 1 ||
		           xfs_buf_item_straddle(bp, offset, next_bit, last_bit)) {
			xfs_buf_item_copy_iovec(lv, vecp, bp, offset,
						first_bit, nbits);
			blfp->blf_size++;
			first_bit = next_bit;
			last_bit = next_bit;
			nbits = 1;
		} else {
			last_bit++;
			nbits++;
		}
	}
}

/*
 * This is called to fill in the vector of log iovecs for the
 * given log buf item.  It fills the first entry with a buf log
 * format structure, and the rest point to contiguous chunks
 * within the buffer.
 */
STATIC void
xfs_buf_item_format(
	xfs_buf_log_item_t	*bip,
	xfs_log_iovec_t		*log_vector)
{
	uint		base_size;
	uint		nvecs;
	xfs_log_iovec_t	*vecp;
	xfs_buf_t	*bp;
	int		first_bit;
	int		last_bit;
	int		next_bit;
	uint		nbits;
	uint		buffer_offset;
	struct xfs_log_item	*lip,
	struct xfs_log_vec	*lv)
{
	struct xfs_buf_log_item	*bip = BUF_ITEM(lip);
	struct xfs_buf		*bp = bip->bli_buf;
	struct xfs_log_iovec	*vecp = NULL;
	uint			offset = 0;
	int			i;

	ASSERT(atomic_read(&bip->bli_refcount) > 0);
	ASSERT((bip->bli_flags & XFS_BLI_LOGGED) ||
	       (bip->bli_flags & XFS_BLI_STALE));
	bp = bip->bli_buf;
	vecp = log_vector;

	/*
	 * The size of the base structure is the size of the
	 * declared structure plus the space for the extra words
	 * of the bitmap.  We subtract one from the map size, because
	 * the first element of the bitmap is accounted for in the
	 * size of the base structure.
	 */
	base_size =
		(uint)(sizeof(xfs_buf_log_format_t) +
		       ((bip->bli_format.blf_map_size - 1) * sizeof(uint)));
	vecp->i_addr = (xfs_caddr_t)&bip->bli_format;
	vecp->i_len = base_size;
	XLOG_VEC_SET_TYPE(vecp, XLOG_REG_TYPE_BFORMAT);
	vecp++;
	nvecs = 1;

	if (bip->bli_flags & XFS_BLI_STALE) {
		/*
		 * The buffer is stale, so all we need to log
		 * is the buf log format structure with the
		 * cancel flag in it.
		 */
		xfs_buf_item_trace("FORMAT STALE", bip);
		ASSERT(bip->bli_format.blf_flags & XFS_BLI_CANCEL);
		bip->bli_format.blf_size = nvecs;
		return;
	}

	/*
	 * Fill in an iovec for each set of contiguous chunks.
	 */
	first_bit = xfs_next_bit(bip->bli_format.blf_data_map,
					 bip->bli_format.blf_map_size, 0);
	ASSERT(first_bit != -1);
	last_bit = first_bit;
	nbits = 1;
	for (;;) {
		/*
		 * This takes the bit number to start looking from and
		 * returns the next set bit from there.  It returns -1
		 * if there are no more bits set or the start bit is
		 * beyond the end of the bitmap.
		 */
		next_bit = xfs_next_bit(bip->bli_format.blf_data_map,
						 bip->bli_format.blf_map_size,
						 (uint)last_bit + 1);
		/*
		 * If we run out of bits fill in the last iovec and get
		 * out of the loop.
		 * Else if we start a new set of bits then fill in the
		 * iovec for the series we were looking at and start
		 * counting the bits in the new one.
		 * Else we're still in the same set of bits so just
		 * keep counting and scanning.
		 */
		if (next_bit == -1) {
			buffer_offset = first_bit * XFS_BLI_CHUNK;
			vecp->i_addr = xfs_buf_offset(bp, buffer_offset);
			vecp->i_len = nbits * XFS_BLI_CHUNK;
			XLOG_VEC_SET_TYPE(vecp, XLOG_REG_TYPE_BCHUNK);
			nvecs++;
			break;
		} else if (next_bit != last_bit + 1) {
			buffer_offset = first_bit * XFS_BLI_CHUNK;
			vecp->i_addr = xfs_buf_offset(bp, buffer_offset);
			vecp->i_len = nbits * XFS_BLI_CHUNK;
			XLOG_VEC_SET_TYPE(vecp, XLOG_REG_TYPE_BCHUNK);
			nvecs++;
			vecp++;
			first_bit = next_bit;
			last_bit = next_bit;
			nbits = 1;
		} else if (xfs_buf_offset(bp, next_bit << XFS_BLI_SHIFT) !=
			   (xfs_buf_offset(bp, last_bit << XFS_BLI_SHIFT) +
			    XFS_BLI_CHUNK)) {
			buffer_offset = first_bit * XFS_BLI_CHUNK;
			vecp->i_addr = xfs_buf_offset(bp, buffer_offset);
			vecp->i_len = nbits * XFS_BLI_CHUNK;
			XLOG_VEC_SET_TYPE(vecp, XLOG_REG_TYPE_BCHUNK);
/* You would think we need to bump the nvecs here too, but we do not
 * this number is used by recovery, and it gets confused by the boundary
 * split here
 *			nvecs++;
 */
			vecp++;
			first_bit = next_bit;
			last_bit = next_bit;
			nbits = 1;
		} else {
			last_bit++;
			nbits++;
		}
	}
	bip->bli_format.blf_size = nvecs;
	ASSERT((bip->bli_flags & XFS_BLI_STALE) ||
	       (xfs_blft_from_flags(&bip->__bli_format) > XFS_BLFT_UNKNOWN_BUF
	        && xfs_blft_from_flags(&bip->__bli_format) < XFS_BLFT_MAX_BUF));


	/*
	 * If it is an inode buffer, transfer the in-memory state to the
	 * format flags and clear the in-memory state.
	 *
	 * For buffer based inode allocation, we do not transfer
	 * this state if the inode buffer allocation has not yet been committed
	 * to the log as setting the XFS_BLI_INODE_BUF flag will prevent
	 * correct replay of the inode allocation.
	 *
	 * For icreate item based inode allocation, the buffers aren't written
	 * to the journal during allocation, and hence we should always tag the
	 * buffer as an inode buffer so that the correct unlinked list replay
	 * occurs during recovery.
	 */
	if (bip->bli_flags & XFS_BLI_INODE_BUF) {
		if (xfs_sb_version_hascrc(&lip->li_mountp->m_sb) ||
		    !((bip->bli_flags & XFS_BLI_INODE_ALLOC_BUF) &&
		      xfs_log_item_in_current_chkpt(lip)))
			bip->__bli_format.blf_flags |= XFS_BLF_INODE_BUF;
		bip->bli_flags &= ~XFS_BLI_INODE_BUF;
	}

	if ((bip->bli_flags & (XFS_BLI_ORDERED|XFS_BLI_STALE)) ==
							XFS_BLI_ORDERED) {
		/*
		 * The buffer has been logged just to order it.  It is not being
		 * included in the transaction commit, so don't format it.
		 */
		trace_xfs_buf_item_format_ordered(bip);
		return;
	}

	for (i = 0; i < bip->bli_format_count; i++) {
		xfs_buf_item_format_segment(bip, lv, &vecp, offset,
					    &bip->bli_formats[i]);
		offset += bp->b_maps[i].bm_len;
	}

	/*
	 * Check to make sure everything is consistent.
	 */
	xfs_buf_item_trace("FORMAT NORM", bip);
	xfs_buf_item_log_check(bip);
}

/*
 * This is called to pin the buffer associated with the buf log
 * item in memory so it cannot be written out.  Simply call bpin()
 * on the buffer to do this.
 */
STATIC void
xfs_buf_item_pin(
	xfs_buf_log_item_t	*bip)
{
	xfs_buf_t	*bp;

	bp = bip->bli_buf;
	ASSERT(XFS_BUF_ISBUSY(bp));
	ASSERT(atomic_read(&bip->bli_refcount) > 0);
	ASSERT((bip->bli_flags & XFS_BLI_LOGGED) ||
	       (bip->bli_flags & XFS_BLI_STALE));
	xfs_buf_item_trace("PIN", bip);
	xfs_buftrace("XFS_PIN", bp);
	xfs_bpin(bp);
}

	trace_xfs_buf_item_format(bip);
}

/*
 * This is called to pin the buffer associated with the buf log item in memory
 * so it cannot be written out.
 *
 * We also always take a reference to the buffer log item here so that the bli
 * is held while the item is pinned in memory. This means that we can
 * unconditionally drop the reference count a transaction holds when the
 * transaction is completed.
 */
STATIC void
xfs_buf_item_pin(
	struct xfs_log_item	*lip)
{
	struct xfs_buf_log_item	*bip = BUF_ITEM(lip);

	ASSERT(atomic_read(&bip->bli_refcount) > 0);
	ASSERT((bip->bli_flags & XFS_BLI_LOGGED) ||
	       (bip->bli_flags & XFS_BLI_ORDERED) ||
	       (bip->bli_flags & XFS_BLI_STALE));

	trace_xfs_buf_item_pin(bip);

	atomic_inc(&bip->bli_refcount);
	atomic_inc(&bip->bli_buf->b_pin_count);
}

/*
 * This is called to unpin the buffer associated with the buf log
 * item which was previously pinned with a call to xfs_buf_item_pin().
 * Just call bunpin() on the buffer to do this.
 *
 * Also drop the reference to the buf item for the current transaction.
 * If the XFS_BLI_STALE flag is set and we are the last reference,
 * then free up the buf log item and unlock the buffer.
 */
STATIC void
xfs_buf_item_unpin(
	xfs_buf_log_item_t	*bip,
	int			stale)
{
	xfs_mount_t	*mp;
	xfs_buf_t	*bp;
	int		freed;

	bp = bip->bli_buf;
	ASSERT(bp != NULL);
	ASSERT(XFS_BUF_FSPRIVATE(bp, xfs_buf_log_item_t *) == bip);
	ASSERT(atomic_read(&bip->bli_refcount) > 0);
	xfs_buf_item_trace("UNPIN", bip);
	xfs_buftrace("XFS_UNPIN", bp);

	freed = atomic_dec_and_test(&bip->bli_refcount);
	mp = bip->bli_item.li_mountp;
	xfs_bunpin(bp);
	if (freed && stale) {
		ASSERT(bip->bli_flags & XFS_BLI_STALE);
		ASSERT(XFS_BUF_VALUSEMA(bp) <= 0);
		ASSERT(!(XFS_BUF_ISDELAYWRITE(bp)));
		ASSERT(XFS_BUF_ISSTALE(bp));
		ASSERT(bip->bli_format.blf_flags & XFS_BLI_CANCEL);
		xfs_buf_item_trace("UNPIN STALE", bip);
		xfs_buftrace("XFS_UNPIN STALE", bp);
		/*
		 * If we get called here because of an IO error, we may
		 * or may not have the item on the AIL. xfs_trans_delete_ail()
		 * will take care of that situation.
		 * xfs_trans_delete_ail() drops the AIL lock.
		 */
		if (bip->bli_flags & XFS_BLI_STALE_INODE) {
			xfs_buf_do_callbacks(bp, (xfs_log_item_t *)bip);
			XFS_BUF_SET_FSPRIVATE(bp, NULL);
			XFS_BUF_CLR_IODONE_FUNC(bp);
		} else {
			spin_lock(&mp->m_ail_lock);
			xfs_trans_delete_ail(mp, (xfs_log_item_t *)bip);
			xfs_buf_item_relse(bp);
			ASSERT(XFS_BUF_FSPRIVATE(bp, void *) == NULL);
		}
		xfs_buf_relse(bp);
 *
 * If the remove flag is set we are called from uncommit in the
 * forced-shutdown path.  If that is true and the reference count on
 * the log item is going to drop to zero we need to free the item's
 * descriptor in the transaction.
 */
STATIC void
xfs_buf_item_unpin(
	struct xfs_log_item	*lip,
	int			remove)
{
	struct xfs_buf_log_item	*bip = BUF_ITEM(lip);
	xfs_buf_t	*bp = bip->bli_buf;
	struct xfs_ail	*ailp = lip->li_ailp;
	int		stale = bip->bli_flags & XFS_BLI_STALE;
	int		freed;

	ASSERT(bp->b_fspriv == bip);
	ASSERT(atomic_read(&bip->bli_refcount) > 0);

	trace_xfs_buf_item_unpin(bip);

	freed = atomic_dec_and_test(&bip->bli_refcount);

	if (atomic_dec_and_test(&bp->b_pin_count))
		wake_up_all(&bp->b_waiters);

	if (freed && stale) {
		ASSERT(bip->bli_flags & XFS_BLI_STALE);
		ASSERT(xfs_buf_islocked(bp));
		ASSERT(XFS_BUF_ISSTALE(bp));
		ASSERT(bip->__bli_format.blf_flags & XFS_BLF_CANCEL);

		trace_xfs_buf_item_unpin_stale(bip);

		if (remove) {
			/*
			 * If we are in a transaction context, we have to
			 * remove the log item from the transaction as we are
			 * about to release our reference to the buffer.  If we
			 * don't, the unlock that occurs later in
			 * xfs_trans_uncommit() will try to reference the
			 * buffer which we no longer have a hold on.
			 */
			if (lip->li_desc)
				xfs_trans_del_item(lip);

			/*
			 * Since the transaction no longer refers to the buffer,
			 * the buffer should no longer refer to the transaction.
			 */
			bp->b_transp = NULL;
		}

		/*
		 * If we get called here because of an IO error, we may
		 * or may not have the item on the AIL. xfs_trans_ail_delete()
		 * will take care of that situation.
		 * xfs_trans_ail_delete() drops the AIL lock.
		 */
		if (bip->bli_flags & XFS_BLI_STALE_INODE) {
			xfs_buf_do_callbacks(bp);
			bp->b_fspriv = NULL;
			bp->b_iodone = NULL;
		} else {
			spin_lock(&ailp->xa_lock);
			xfs_trans_ail_delete(ailp, lip, SHUTDOWN_LOG_IO_ERROR);
			xfs_buf_item_relse(bp);
			ASSERT(bp->b_fspriv == NULL);
		}
		xfs_buf_relse(bp);
	} else if (freed && remove) {
		/*
		 * There are currently two references to the buffer - the active
		 * LRU reference and the buf log item. What we are about to do
		 * here - simulate a failed IO completion - requires 3
		 * references.
		 *
		 * The LRU reference is removed by the xfs_buf_stale() call. The
		 * buf item reference is removed by the xfs_buf_iodone()
		 * callback that is run by xfs_buf_do_callbacks() during ioend
		 * processing (via the bp->b_iodone callback), and then finally
		 * the ioend processing will drop the IO reference if the buffer
		 * is marked XBF_ASYNC.
		 *
		 * Hence we need to take an additional reference here so that IO
		 * completion processing doesn't free the buffer prematurely.
		 */
		xfs_buf_lock(bp);
		xfs_buf_hold(bp);
		bp->b_flags |= XBF_ASYNC;
		xfs_buf_ioerror(bp, -EIO);
		XFS_BUF_UNDONE(bp);
		xfs_buf_stale(bp);
		xfs_buf_ioend(bp);
	}
}

/*
 * this is called from uncommit in the forced-shutdown path.
 * we need to check to see if the reference count on the log item
 * is going to drop to zero.  If so, unpin will free the log item
 * so we need to free the item's descriptor (that points to the item)
 * in the transaction.
 */
STATIC void
xfs_buf_item_unpin_remove(
	xfs_buf_log_item_t	*bip,
	xfs_trans_t		*tp)
{
	xfs_buf_t		*bp;
	xfs_log_item_desc_t	*lidp;
	int			stale = 0;

	bp = bip->bli_buf;
	/*
	 * will xfs_buf_item_unpin() call xfs_buf_item_relse()?
	 */
	if ((atomic_read(&bip->bli_refcount) == 1) &&
	    (bip->bli_flags & XFS_BLI_STALE)) {
		ASSERT(XFS_BUF_VALUSEMA(bip->bli_buf) <= 0);
		xfs_buf_item_trace("UNPIN REMOVE", bip);
		xfs_buftrace("XFS_UNPIN_REMOVE", bp);
		/*
		 * yes -- clear the xaction descriptor in-use flag
		 * and free the chunk if required.  We can safely
		 * do some work here and then call buf_item_unpin
		 * to do the rest because if the if is true, then
		 * we are holding the buffer locked so no one else
		 * will be able to bump up the refcount.
		 */
		lidp = xfs_trans_find_item(tp, (xfs_log_item_t *) bip);
		stale = lidp->lid_flags & XFS_LID_BUF_STALE;
		xfs_trans_free_item(tp, lidp);
		/*
		 * Since the transaction no longer refers to the buffer,
		 * the buffer should no longer refer to the transaction.
		 */
		XFS_BUF_SET_FSPRIVATE2(bp, NULL);
	}

	xfs_buf_item_unpin(bip, stale);

	return;
}

/*
 * This is called to attempt to lock the buffer associated with this
 * buf log item.  Don't sleep on the buffer lock.  If we can't get
 * the lock right away, return 0.  If we can get the lock, pull the
 * buffer from the free list, mark it busy, and return 1.
 */
STATIC uint
xfs_buf_item_trylock(
	xfs_buf_log_item_t	*bip)
{
	xfs_buf_t	*bp;

	bp = bip->bli_buf;

	if (XFS_BUF_ISPINNED(bp)) {
		return XFS_ITEM_PINNED;
	}

	if (!XFS_BUF_CPSEMA(bp)) {
		return XFS_ITEM_LOCKED;
	}

	/*
	 * Remove the buffer from the free list.  Only do this
	 * if it's on the free list.  Private buffers like the
	 * superblock buffer are not.
	 */
	XFS_BUF_HOLD(bp);

	ASSERT(!(bip->bli_flags & XFS_BLI_STALE));
	xfs_buf_item_trace("TRYLOCK SUCCESS", bip);
	return XFS_ITEM_SUCCESS;
}

/*
 * Release the buffer associated with the buf log item.
 * If there is no dirty logged data associated with the
 * buffer recorded in the buf log item, then free the
 * buf log item and remove the reference to it in the
 * buffer.
 *
 * This call ignores the recursion count.  It is only called
 * when the buffer should REALLY be unlocked, regardless
 * of the recursion count.
 *
 * If the XFS_BLI_HOLD flag is set in the buf log item, then
 * free the log item if necessary but do not unlock the buffer.
 * This is for support of xfs_trans_bhold(). Make sure the
 * XFS_BLI_HOLD field is cleared if we don't free the item.
 */
STATIC void
xfs_buf_item_unlock(
	xfs_buf_log_item_t	*bip)
{
	int		aborted;
	xfs_buf_t	*bp;
	uint		hold;

	bp = bip->bli_buf;
	xfs_buftrace("XFS_UNLOCK", bp);

	/*
	 * Clear the buffer's association with this transaction.
	 */
	XFS_BUF_SET_FSPRIVATE2(bp, NULL);

	/*
	 * If this is a transaction abort, don't return early.
	 * Instead, allow the brelse to happen.
	 * Normally it would be done for stale (cancelled) buffers
	 * at unpin time, but we'll never go through the pin/unpin
	 * cycle if we abort inside commit.
	 */
	aborted = (bip->bli_item.li_flags & XFS_LI_ABORTED) != 0;

	/*
	 * If the buf item is marked stale, then don't do anything.
	 * We'll unlock the buffer and free the buf item when the
	 * buffer is unpinned for the last time.
	 */
	if (bip->bli_flags & XFS_BLI_STALE) {
		bip->bli_flags &= ~XFS_BLI_LOGGED;
		xfs_buf_item_trace("UNLOCK STALE", bip);
		ASSERT(bip->bli_format.blf_flags & XFS_BLI_CANCEL);
		if (!aborted)
			return;
	}

	/*
	 * Drop the transaction's reference to the log item if
	 * it was not logged as part of the transaction.  Otherwise
	 * we'll drop the reference in xfs_buf_item_unpin() when
	 * the transaction is really through with the buffer.
	 */
	if (!(bip->bli_flags & XFS_BLI_LOGGED)) {
		atomic_dec(&bip->bli_refcount);
	} else {
		/*
		 * Clear the logged flag since this is per
		 * transaction state.
		 */
		bip->bli_flags &= ~XFS_BLI_LOGGED;
	}

	/*
	 * Before possibly freeing the buf item, determine if we should
	 * release the buffer at the end of this routine.
	 */
	hold = bip->bli_flags & XFS_BLI_HOLD;
	xfs_buf_item_trace("UNLOCK", bip);

	/*
	 * If the buf item isn't tracking any data, free it.
	 * Otherwise, if XFS_BLI_HOLD is set clear it.
	 */
	if (xfs_bitmap_empty(bip->bli_format.blf_data_map,
			     bip->bli_format.blf_map_size)) {
		xfs_buf_item_relse(bp);
	} else if (hold) {
		bip->bli_flags &= ~XFS_BLI_HOLD;
	}

	/*
	 * Release the buffer if XFS_BLI_HOLD was not set.
	 */
	if (!hold) {
		xfs_buf_relse(bp);
	}
 * Buffer IO error rate limiting. Limit it to no more than 10 messages per 30
 * seconds so as to not spam logs too much on repeated detection of the same
 * buffer being bad..
 */

static DEFINE_RATELIMIT_STATE(xfs_buf_write_fail_rl_state, 30 * HZ, 10);

STATIC uint
xfs_buf_item_push(
	struct xfs_log_item	*lip,
	struct list_head	*buffer_list)
{
	struct xfs_buf_log_item	*bip = BUF_ITEM(lip);
	struct xfs_buf		*bp = bip->bli_buf;
	uint			rval = XFS_ITEM_SUCCESS;

	if (xfs_buf_ispinned(bp))
		return XFS_ITEM_PINNED;
	if (!xfs_buf_trylock(bp)) {
		/*
		 * If we have just raced with a buffer being pinned and it has
		 * been marked stale, we could end up stalling until someone else
		 * issues a log force to unpin the stale buffer. Check for the
		 * race condition here so xfsaild recognizes the buffer is pinned
		 * and queues a log force to move it along.
		 */
		if (xfs_buf_ispinned(bp))
			return XFS_ITEM_PINNED;
		return XFS_ITEM_LOCKED;
	}

	ASSERT(!(bip->bli_flags & XFS_BLI_STALE));

	trace_xfs_buf_item_push(bip);

	/* has a previous flush failed due to IO errors? */
	if ((bp->b_flags & XBF_WRITE_FAIL) &&
	    ___ratelimit(&xfs_buf_write_fail_rl_state, "XFS: Failing async write")) {
		xfs_warn(bp->b_target->bt_mount,
"Failing async write on buffer block 0x%llx. Retrying async write.",
			 (long long)bp->b_bn);
	}

	if (!xfs_buf_delwri_queue(bp, buffer_list))
		rval = XFS_ITEM_FLUSHING;
	xfs_buf_unlock(bp);
	return rval;
}

/*
 * Release the buffer associated with the buf log item.  If there is no dirty
 * logged data associated with the buffer recorded in the buf log item, then
 * free the buf log item and remove the reference to it in the buffer.
 *
 * This call ignores the recursion count.  It is only called when the buffer
 * should REALLY be unlocked, regardless of the recursion count.
 *
 * We unconditionally drop the transaction's reference to the log item. If the
 * item was logged, then another reference was taken when it was pinned, so we
 * can safely drop the transaction reference now.  This also allows us to avoid
 * potential races with the unpin code freeing the bli by not referencing the
 * bli after we've dropped the reference count.
 *
 * If the XFS_BLI_HOLD flag is set in the buf log item, then free the log item
 * if necessary but do not unlock the buffer.  This is for support of
 * xfs_trans_bhold(). Make sure the XFS_BLI_HOLD field is cleared if we don't
 * free the item.
 */
STATIC void
xfs_buf_item_unlock(
	struct xfs_log_item	*lip)
{
	struct xfs_buf_log_item	*bip = BUF_ITEM(lip);
	struct xfs_buf		*bp = bip->bli_buf;
	bool			clean;
	bool			aborted;
	int			flags;

	/* Clear the buffer's association with this transaction. */
	bp->b_transp = NULL;

	/*
	 * If this is a transaction abort, don't return early.  Instead, allow
	 * the brelse to happen.  Normally it would be done for stale
	 * (cancelled) buffers at unpin time, but we'll never go through the
	 * pin/unpin cycle if we abort inside commit.
	 */
	aborted = (lip->li_flags & XFS_LI_ABORTED) ? true : false;
	/*
	 * Before possibly freeing the buf item, copy the per-transaction state
	 * so we can reference it safely later after clearing it from the
	 * buffer log item.
	 */
	flags = bip->bli_flags;
	bip->bli_flags &= ~(XFS_BLI_LOGGED | XFS_BLI_HOLD | XFS_BLI_ORDERED);

	/*
	 * If the buf item is marked stale, then don't do anything.  We'll
	 * unlock the buffer and free the buf item when the buffer is unpinned
	 * for the last time.
	 */
	if (flags & XFS_BLI_STALE) {
		trace_xfs_buf_item_unlock_stale(bip);
		ASSERT(bip->__bli_format.blf_flags & XFS_BLF_CANCEL);
		if (!aborted) {
			atomic_dec(&bip->bli_refcount);
			return;
		}
	}

	trace_xfs_buf_item_unlock(bip);

	/*
	 * If the buf item isn't tracking any data, free it, otherwise drop the
	 * reference we hold to it. If we are aborting the transaction, this may
	 * be the only reference to the buf item, so we free it anyway
	 * regardless of whether it is dirty or not. A dirty abort implies a
	 * shutdown, anyway.
	 *
	 * Ordered buffers are dirty but may have no recorded changes, so ensure
	 * we only release clean items here.
	 */
	clean = (flags & XFS_BLI_DIRTY) ? false : true;
	if (clean) {
		int i;
		for (i = 0; i < bip->bli_format_count; i++) {
			if (!xfs_bitmap_empty(bip->bli_formats[i].blf_data_map,
				     bip->bli_formats[i].blf_map_size)) {
				clean = false;
				break;
			}
		}
	}

	/*
	 * Clean buffers, by definition, cannot be in the AIL. However, aborted
	 * buffers may be dirty and hence in the AIL. Therefore if we are
	 * aborting a buffer and we've just taken the last refernce away, we
	 * have to check if it is in the AIL before freeing it. We need to free
	 * it in this case, because an aborted transaction has already shut the
	 * filesystem down and this is the last chance we will have to do so.
	 */
	if (atomic_dec_and_test(&bip->bli_refcount)) {
		if (clean)
			xfs_buf_item_relse(bp);
		else if (aborted) {
			ASSERT(XFS_FORCED_SHUTDOWN(lip->li_mountp));
			xfs_trans_ail_remove(lip, SHUTDOWN_LOG_IO_ERROR);
			xfs_buf_item_relse(bp);
		}
	}

	if (!(flags & XFS_BLI_HOLD))
		xfs_buf_relse(bp);
}

/*
 * This is called to find out where the oldest active copy of the
 * buf log item in the on disk log resides now that the last log
 * write of it completed at the given lsn.
 * We always re-log all the dirty data in a buffer, so usually the
 * latest copy in the on disk log is the only one that matters.  For
 * those cases we simply return the given lsn.
 *
 * The one exception to this is for buffers full of newly allocated
 * inodes.  These buffers are only relogged with the XFS_BLI_INODE_BUF
 * flag set, indicating that only the di_next_unlinked fields from the
 * inodes in the buffers will be replayed during recovery.  If the
 * original newly allocated inode images have not yet been flushed
 * when the buffer is so relogged, then we need to make sure that we
 * keep the old images in the 'active' portion of the log.  We do this
 * by returning the original lsn of that transaction here rather than
 * the current one.
 */
STATIC xfs_lsn_t
xfs_buf_item_committed(
	xfs_buf_log_item_t	*bip,
	xfs_lsn_t		lsn)
{
	xfs_buf_item_trace("COMMITTED", bip);
	if ((bip->bli_flags & XFS_BLI_INODE_ALLOC_BUF) &&
	    (bip->bli_item.li_lsn != 0)) {
		return bip->bli_item.li_lsn;
	}
	return (lsn);
}

/*
 * This is called to asynchronously write the buffer associated with this
 * buf log item out to disk. The buffer will already have been locked by
 * a successful call to xfs_buf_item_trylock().  If the buffer still has
 * B_DELWRI set, then get it going out to disk with a call to bawrite().
 * If not, then just release the buffer.
 */
STATIC void
xfs_buf_item_push(
	xfs_buf_log_item_t	*bip)
{
	xfs_buf_t	*bp;

	ASSERT(!(bip->bli_flags & XFS_BLI_STALE));
	xfs_buf_item_trace("PUSH", bip);

	bp = bip->bli_buf;

	if (XFS_BUF_ISDELAYWRITE(bp)) {
		int	error;
		error = xfs_bawrite(bip->bli_item.li_mountp, bp);
		if (error)
			xfs_fs_cmn_err(CE_WARN, bip->bli_item.li_mountp,
			"xfs_buf_item_push: pushbuf error %d on bip %p, bp %p",
					error, bip, bp);
	} else {
		xfs_buf_relse(bp);
	}
}

/* ARGSUSED */
STATIC void
xfs_buf_item_committing(xfs_buf_log_item_t *bip, xfs_lsn_t commit_lsn)
	struct xfs_log_item	*lip,
	xfs_lsn_t		lsn)
{
	struct xfs_buf_log_item	*bip = BUF_ITEM(lip);

	trace_xfs_buf_item_committed(bip);

	if ((bip->bli_flags & XFS_BLI_INODE_ALLOC_BUF) && lip->li_lsn != 0)
		return lip->li_lsn;
	return lsn;
}

STATIC void
xfs_buf_item_committing(
	struct xfs_log_item	*lip,
	xfs_lsn_t		commit_lsn)
{
}

/*
 * This is the ops vector shared by all buf log items.
 */
static struct xfs_item_ops xfs_buf_item_ops = {
	.iop_size	= (uint(*)(xfs_log_item_t*))xfs_buf_item_size,
	.iop_format	= (void(*)(xfs_log_item_t*, xfs_log_iovec_t*))
					xfs_buf_item_format,
	.iop_pin	= (void(*)(xfs_log_item_t*))xfs_buf_item_pin,
	.iop_unpin	= (void(*)(xfs_log_item_t*, int))xfs_buf_item_unpin,
	.iop_unpin_remove = (void(*)(xfs_log_item_t*, xfs_trans_t *))
					xfs_buf_item_unpin_remove,
	.iop_trylock	= (uint(*)(xfs_log_item_t*))xfs_buf_item_trylock,
	.iop_unlock	= (void(*)(xfs_log_item_t*))xfs_buf_item_unlock,
	.iop_committed	= (xfs_lsn_t(*)(xfs_log_item_t*, xfs_lsn_t))
					xfs_buf_item_committed,
	.iop_push	= (void(*)(xfs_log_item_t*))xfs_buf_item_push,
	.iop_pushbuf	= NULL,
	.iop_committing = (void(*)(xfs_log_item_t*, xfs_lsn_t))
					xfs_buf_item_committing
};

static const struct xfs_item_ops xfs_buf_item_ops = {
	.iop_size	= xfs_buf_item_size,
	.iop_format	= xfs_buf_item_format,
	.iop_pin	= xfs_buf_item_pin,
	.iop_unpin	= xfs_buf_item_unpin,
	.iop_unlock	= xfs_buf_item_unlock,
	.iop_committed	= xfs_buf_item_committed,
	.iop_push	= xfs_buf_item_push,
	.iop_committing = xfs_buf_item_committing
};

STATIC int
xfs_buf_item_get_format(
	struct xfs_buf_log_item	*bip,
	int			count)
{
	ASSERT(bip->bli_formats == NULL);
	bip->bli_format_count = count;

	if (count == 1) {
		bip->bli_formats = &bip->__bli_format;
		return 0;
	}

	bip->bli_formats = kmem_zalloc(count * sizeof(struct xfs_buf_log_format),
				KM_SLEEP);
	if (!bip->bli_formats)
		return -ENOMEM;
	return 0;
}

STATIC void
xfs_buf_item_free_format(
	struct xfs_buf_log_item	*bip)
{
	if (bip->bli_formats != &bip->__bli_format) {
		kmem_free(bip->bli_formats);
		bip->bli_formats = NULL;
	}
}

/*
 * Allocate a new buf log item to go with the given buffer.
 * Set the buffer's b_fsprivate field to point to the new
 * buf log item.  If there are other item's attached to the
 * buffer (see xfs_buf_attach_iodone() below), then put the
 * buf log item at the front.
 */
void
xfs_buf_item_init(
	xfs_buf_t	*bp,
	xfs_mount_t	*mp)
{
	xfs_log_item_t		*lip;
	xfs_buf_log_item_t	*bip;
	int			chunks;
	int			map_size;
int
xfs_buf_item_init(
	struct xfs_buf	*bp,
	struct xfs_mount *mp)
{
	struct xfs_log_item	*lip = bp->b_fspriv;
	struct xfs_buf_log_item	*bip;
	int			chunks;
	int			map_size;
	int			error;
	int			i;

	/*
	 * Check to see if there is already a buf log item for
	 * this buffer.  If there is, it is guaranteed to be
	 * the first.  If we do already have one, there is
	 * nothing to do here so return.
	 */
	if (XFS_BUF_FSPRIVATE3(bp, xfs_mount_t *) != mp)
		XFS_BUF_SET_FSPRIVATE3(bp, mp);
	XFS_BUF_SET_BDSTRAT_FUNC(bp, xfs_bdstrat_cb);
	if (XFS_BUF_FSPRIVATE(bp, void *) != NULL) {
		lip = XFS_BUF_FSPRIVATE(bp, xfs_log_item_t *);
		if (lip->li_type == XFS_LI_BUF) {
			return;
		}
	}

	/*
	 * chunks is the number of XFS_BLI_CHUNK size pieces
	 * the buffer can be divided into. Make sure not to
	 * truncate any pieces.  map_size is the size of the
	 * bitmap needed to describe the chunks of the buffer.
	 */
	chunks = (int)((XFS_BUF_COUNT(bp) + (XFS_BLI_CHUNK - 1)) >> XFS_BLI_SHIFT);
	map_size = (int)((chunks + NBWORD) >> BIT_TO_WORD_SHIFT);

	bip = (xfs_buf_log_item_t*)kmem_zone_zalloc(xfs_buf_item_zone,
						    KM_SLEEP);
	bip->bli_item.li_type = XFS_LI_BUF;
	bip->bli_item.li_ops = &xfs_buf_item_ops;
	bip->bli_item.li_mountp = mp;
	bip->bli_buf = bp;
	xfs_buf_hold(bp);
	bip->bli_format.blf_type = XFS_LI_BUF;
	bip->bli_format.blf_blkno = (__int64_t)XFS_BUF_ADDR(bp);
	bip->bli_format.blf_len = (ushort)BTOBB(XFS_BUF_COUNT(bp));
	bip->bli_format.blf_map_size = map_size;
#ifdef XFS_BLI_TRACE
	bip->bli_trace = ktrace_alloc(XFS_BLI_TRACE_SIZE, KM_NOFS);
#endif

#ifdef XFS_TRANS_DEBUG
	/*
	 * Allocate the arrays for tracking what needs to be logged
	 * and what our callers request to be logged.  bli_orig
	 * holds a copy of the original, clean buffer for comparison
	 * against, and bli_logged keeps a 1 bit flag per byte in
	 * the buffer to indicate which bytes the callers have asked
	 * to have logged.
	 */
	bip->bli_orig = (char *)kmem_alloc(XFS_BUF_COUNT(bp), KM_SLEEP);
	memcpy(bip->bli_orig, XFS_BUF_PTR(bp), XFS_BUF_COUNT(bp));
	bip->bli_logged = (char *)kmem_zalloc(XFS_BUF_COUNT(bp) / NBBY, KM_SLEEP);
#endif
	ASSERT(bp->b_target->bt_mount == mp);
	if (lip != NULL && lip->li_type == XFS_LI_BUF)
		return 0;

	bip = kmem_zone_zalloc(xfs_buf_item_zone, KM_SLEEP);
	xfs_log_item_init(mp, &bip->bli_item, XFS_LI_BUF, &xfs_buf_item_ops);
	bip->bli_buf = bp;

	/*
	 * chunks is the number of XFS_BLF_CHUNK size pieces the buffer
	 * can be divided into. Make sure not to truncate any pieces.
	 * map_size is the size of the bitmap needed to describe the
	 * chunks of the buffer.
	 *
	 * Discontiguous buffer support follows the layout of the underlying
	 * buffer. This makes the implementation as simple as possible.
	 */
	error = xfs_buf_item_get_format(bip, bp->b_map_count);
	ASSERT(error == 0);
	if (error) {	/* to stop gcc throwing set-but-unused warnings */
		kmem_zone_free(xfs_buf_item_zone, bip);
		return error;
	}


	for (i = 0; i < bip->bli_format_count; i++) {
		chunks = DIV_ROUND_UP(BBTOB(bp->b_maps[i].bm_len),
				      XFS_BLF_CHUNK);
		map_size = DIV_ROUND_UP(chunks, NBWORD);

		bip->bli_formats[i].blf_type = XFS_LI_BUF;
		bip->bli_formats[i].blf_blkno = bp->b_maps[i].bm_bn;
		bip->bli_formats[i].blf_len = bp->b_maps[i].bm_len;
		bip->bli_formats[i].blf_map_size = map_size;
	}

	/*
	 * Put the buf item into the list of items attached to the
	 * buffer at the front.
	 */
	if (XFS_BUF_FSPRIVATE(bp, void *) != NULL) {
		bip->bli_item.li_bio_list =
				XFS_BUF_FSPRIVATE(bp, xfs_log_item_t *);
	}
	XFS_BUF_SET_FSPRIVATE(bp, bip);
	if (bp->b_fspriv)
		bip->bli_item.li_bio_list = bp->b_fspriv;
	bp->b_fspriv = bip;
	xfs_buf_hold(bp);
	return 0;
}


/*
 * Mark bytes first through last inclusive as dirty in the buf
 * item's bitmap.
 */
void
xfs_buf_item_log(
	xfs_buf_log_item_t	*bip,
	uint			first,
	uint			last)
static void
xfs_buf_item_log_segment(
	uint			first,
	uint			last,
	uint			*map)
{
	uint		first_bit;
	uint		last_bit;
	uint		bits_to_set;
	uint		bits_set;
	uint		word_num;
	uint		*wordp;
	uint		bit;
	uint		end_bit;
	uint		mask;

	/*
	 * Mark the item as having some dirty data for
	 * quick reference in xfs_buf_item_dirty.
	 */
	bip->bli_flags |= XFS_BLI_DIRTY;

	/*
	 * Convert byte offsets to bit numbers.
	 */
	first_bit = first >> XFS_BLI_SHIFT;
	last_bit = last >> XFS_BLI_SHIFT;
	 * Convert byte offsets to bit numbers.
	 */
	first_bit = first >> XFS_BLF_SHIFT;
	last_bit = last >> XFS_BLF_SHIFT;

	/*
	 * Calculate the total number of bits to be set.
	 */
	bits_to_set = last_bit - first_bit + 1;

	/*
	 * Get a pointer to the first word in the bitmap
	 * to set a bit in.
	 */
	word_num = first_bit >> BIT_TO_WORD_SHIFT;
	wordp = &(bip->bli_format.blf_data_map[word_num]);
	wordp = &map[word_num];

	/*
	 * Calculate the starting bit in the first word.
	 */
	bit = first_bit & (uint)(NBWORD - 1);

	/*
	 * First set any bits in the first word of our range.
	 * If it starts at bit 0 of the word, it will be
	 * set below rather than here.  That is what the variable
	 * bit tells us. The variable bits_set tracks the number
	 * of bits that have been set so far.  End_bit is the number
	 * of the last bit to be set in this word plus one.
	 */
	if (bit) {
		end_bit = MIN(bit + bits_to_set, (uint)NBWORD);
		mask = ((1U << (end_bit - bit)) - 1) << bit;
		*wordp |= mask;
		wordp++;
		bits_set = end_bit - bit;
	} else {
		bits_set = 0;
	}

	/*
	 * Now set bits a whole word at a time that are between
	 * first_bit and last_bit.
	 */
	while ((bits_to_set - bits_set) >= NBWORD) {
		*wordp |= 0xffffffff;
		bits_set += NBWORD;
		wordp++;
	}

	/*
	 * Finally, set any bits left to be set in one last partial word.
	 */
	end_bit = bits_to_set - bits_set;
	if (end_bit) {
		mask = (1U << end_bit) - 1;
		*wordp |= mask;
	}

	xfs_buf_item_log_debug(bip, first, last);
}

/*
 * Mark bytes first through last inclusive as dirty in the buf
 * item's bitmap.
 */
void
xfs_buf_item_log(
	xfs_buf_log_item_t	*bip,
	uint			first,
	uint			last)
{
	int			i;
	uint			start;
	uint			end;
	struct xfs_buf		*bp = bip->bli_buf;

	/*
	 * walk each buffer segment and mark them dirty appropriately.
	 */
	start = 0;
	for (i = 0; i < bip->bli_format_count; i++) {
		if (start > last)
			break;
		end = start + BBTOB(bp->b_maps[i].bm_len);
		if (first > end) {
			start += BBTOB(bp->b_maps[i].bm_len);
			continue;
		}
		if (first < start)
			first = start;
		if (end > last)
			end = last;

		xfs_buf_item_log_segment(first, end,
					 &bip->bli_formats[i].blf_data_map[0]);

		start += bp->b_maps[i].bm_len;
	}
}


/*
 * Return 1 if the buffer has some data that has been logged (at any
 * Return 1 if the buffer has been logged or ordered in a transaction (at any
 * point, not just the current transaction) and 0 if not.
 */
uint
xfs_buf_item_dirty(
	xfs_buf_log_item_t	*bip)
{
	return (bip->bli_flags & XFS_BLI_DIRTY);
}

STATIC void
xfs_buf_item_free(
	xfs_buf_log_item_t	*bip)
{
#ifdef XFS_TRANS_DEBUG
	kmem_free(bip->bli_orig);
	kmem_free(bip->bli_logged);
#endif /* XFS_TRANS_DEBUG */

#ifdef XFS_BLI_TRACE
	ktrace_free(bip->bli_trace);
#endif
	xfs_buf_item_free_format(bip);
	kmem_zone_free(xfs_buf_item_zone, bip);
}

/*
 * This is called when the buf log item is no longer needed.  It should
 * free the buf log item associated with the given buffer and clear
 * the buffer's pointer to the buf log item.  If there are no more
 * items in the list, clear the b_iodone field of the buffer (see
 * xfs_buf_attach_iodone() below).
 */
void
xfs_buf_item_relse(
	xfs_buf_t	*bp)
{
	xfs_buf_log_item_t	*bip;

	xfs_buftrace("XFS_RELSE", bp);
	bip = XFS_BUF_FSPRIVATE(bp, xfs_buf_log_item_t*);
	XFS_BUF_SET_FSPRIVATE(bp, bip->bli_item.li_bio_list);
	if ((XFS_BUF_FSPRIVATE(bp, void *) == NULL) &&
	    (XFS_BUF_IODONE_FUNC(bp) != NULL)) {
		XFS_BUF_CLR_IODONE_FUNC(bp);
	}
	xfs_buf_log_item_t	*bip = bp->b_fspriv;

	trace_xfs_buf_item_relse(bp, _RET_IP_);
	ASSERT(!(bip->bli_item.li_flags & XFS_LI_IN_AIL));

	bp->b_fspriv = bip->bli_item.li_bio_list;
	if (bp->b_fspriv == NULL)
		bp->b_iodone = NULL;

	xfs_buf_rele(bp);
	xfs_buf_item_free(bip);
}


/*
 * Add the given log item with its callback to the list of callbacks
 * to be called when the buffer's I/O completes.  If it is not set
 * already, set the buffer's b_iodone() routine to be
 * xfs_buf_iodone_callbacks() and link the log item into the list of
 * items rooted at b_fsprivate.  Items are always added as the second
 * entry in the list if there is a first, because the buf item code
 * assumes that the buf log item is first.
 */
void
xfs_buf_attach_iodone(
	xfs_buf_t	*bp,
	void		(*cb)(xfs_buf_t *, xfs_log_item_t *),
	xfs_log_item_t	*lip)
{
	xfs_log_item_t	*head_lip;

	ASSERT(XFS_BUF_ISBUSY(bp));
	ASSERT(XFS_BUF_VALUSEMA(bp) <= 0);

	lip->li_cb = cb;
	if (XFS_BUF_FSPRIVATE(bp, void *) != NULL) {
		head_lip = XFS_BUF_FSPRIVATE(bp, xfs_log_item_t *);
		lip->li_bio_list = head_lip->li_bio_list;
		head_lip->li_bio_list = lip;
	} else {
		XFS_BUF_SET_FSPRIVATE(bp, lip);
	}

	ASSERT((XFS_BUF_IODONE_FUNC(bp) == xfs_buf_iodone_callbacks) ||
	       (XFS_BUF_IODONE_FUNC(bp) == NULL));
	XFS_BUF_SET_IODONE_FUNC(bp, xfs_buf_iodone_callbacks);
}

STATIC void
xfs_buf_do_callbacks(
	xfs_buf_t	*bp,
	xfs_log_item_t	*lip)
{
	xfs_log_item_t	*nlip;

	while (lip != NULL) {
		nlip = lip->li_bio_list;
	ASSERT(xfs_buf_islocked(bp));

	lip->li_cb = cb;
	head_lip = bp->b_fspriv;
	if (head_lip) {
		lip->li_bio_list = head_lip->li_bio_list;
		head_lip->li_bio_list = lip;
	} else {
		bp->b_fspriv = lip;
	}

	ASSERT(bp->b_iodone == NULL ||
	       bp->b_iodone == xfs_buf_iodone_callbacks);
	bp->b_iodone = xfs_buf_iodone_callbacks;
}

/*
 * We can have many callbacks on a buffer. Running the callbacks individually
 * can cause a lot of contention on the AIL lock, so we allow for a single
 * callback to be able to scan the remaining lip->li_bio_list for other items
 * of the same type and callback to be processed in the first call.
 *
 * As a result, the loop walking the callback list below will also modify the
 * list. it removes the first item from the list and then runs the callback.
 * The loop then restarts from the new head of the list. This allows the
 * callback to scan and modify the list attached to the buffer and we don't
 * have to care about maintaining a next item pointer.
 */
STATIC void
xfs_buf_do_callbacks(
	struct xfs_buf		*bp)
{
	struct xfs_log_item	*lip;

	while ((lip = bp->b_fspriv) != NULL) {
		bp->b_fspriv = lip->li_bio_list;
		ASSERT(lip->li_cb != NULL);
		/*
		 * Clear the next pointer so we don't have any
		 * confusion if the item is added to another buf.
		 * Don't touch the log item after calling its
		 * callback, because it could have freed itself.
		 */
		lip->li_bio_list = NULL;
		lip->li_cb(bp, lip);
		lip = nlip;
	}
}

/*
 * This is the iodone() function for buffers which have had callbacks
 * attached to them by xfs_buf_attach_iodone().  It should remove each
 * log item from the buffer's list and call the callback of each in turn.
 * When done, the buffer's fsprivate field is set to NULL and the buffer
 * is unlocked with a call to iodone().
 */
void
xfs_buf_iodone_callbacks(
	xfs_buf_t	*bp)
{
	xfs_log_item_t	*lip;
	static ulong	lasttime;
	static xfs_buftarg_t *lasttarg;
	xfs_mount_t	*mp;

	ASSERT(XFS_BUF_FSPRIVATE(bp, void *) != NULL);
	lip = XFS_BUF_FSPRIVATE(bp, xfs_log_item_t *);

	if (XFS_BUF_GETERROR(bp) != 0) {
		/*
		 * If we've already decided to shutdown the filesystem
		 * because of IO errors, there's no point in giving this
		 * a retry.
		 */
		mp = lip->li_mountp;
		if (XFS_FORCED_SHUTDOWN(mp)) {
			ASSERT(XFS_BUF_TARGET(bp) == mp->m_ddev_targp);
			XFS_BUF_SUPER_STALE(bp);
			xfs_buftrace("BUF_IODONE_CB", bp);
			xfs_buf_do_callbacks(bp, lip);
			XFS_BUF_SET_FSPRIVATE(bp, NULL);
			XFS_BUF_CLR_IODONE_FUNC(bp);

			/*
			 * XFS_SHUT flag gets set when we go thru the
			 * entire buffer cache and deliberately start
			 * throwing away delayed write buffers.
			 * Since there's no biowait done on those,
			 * we should just brelse them.
			 */
			if (XFS_BUF_ISSHUT(bp)) {
			    XFS_BUF_UNSHUT(bp);
				xfs_buf_relse(bp);
			} else {
				xfs_biodone(bp);
			}

			return;
		}

		if ((XFS_BUF_TARGET(bp) != lasttarg) ||
		    (time_after(jiffies, (lasttime + 5*HZ)))) {
			lasttime = jiffies;
			cmn_err(CE_ALERT, "Device %s, XFS metadata write error"
					" block 0x%llx in %s",
				XFS_BUFTARG_NAME(XFS_BUF_TARGET(bp)),
			      (__uint64_t)XFS_BUF_ADDR(bp), mp->m_fsname);
		}
		lasttarg = XFS_BUF_TARGET(bp);

		if (XFS_BUF_ISASYNC(bp)) {
			/*
			 * If the write was asynchronous then noone will be
			 * looking for the error.  Clear the error state
			 * and write the buffer out again delayed write.
			 *
			 * XXXsup This is OK, so long as we catch these
			 * before we start the umount; we don't want these
			 * DELWRI metadata bufs to be hanging around.
			 */
			XFS_BUF_ERROR(bp,0); /* errno of 0 unsets the flag */

			if (!(XFS_BUF_ISSTALE(bp))) {
				XFS_BUF_DELAYWRITE(bp);
				XFS_BUF_DONE(bp);
				XFS_BUF_SET_START(bp);
			}
			ASSERT(XFS_BUF_IODONE_FUNC(bp));
			xfs_buftrace("BUF_IODONE ASYNC", bp);
			xfs_buf_relse(bp);
		} else {
			/*
			 * If the write of the buffer was not asynchronous,
			 * then we want to make sure to return the error
			 * to the caller of bwrite().  Because of this we
			 * cannot clear the B_ERROR state at this point.
			 * Instead we install a callback function that
			 * will be called when the buffer is released, and
			 * that routine will clear the error state and
			 * set the buffer to be written out again after
			 * some delay.
			 */
			/* We actually overwrite the existing b-relse
			   function at times, but we're gonna be shutting down
			   anyway. */
			XFS_BUF_SET_BRELSE_FUNC(bp,xfs_buf_error_relse);
			XFS_BUF_DONE(bp);
			XFS_BUF_FINISH_IOWAIT(bp);
		}
		return;
	}
#ifdef XFSERRORDEBUG
	xfs_buftrace("XFS BUFCB NOERR", bp);
#endif
	xfs_buf_do_callbacks(bp, lip);
	XFS_BUF_SET_FSPRIVATE(bp, NULL);
	XFS_BUF_CLR_IODONE_FUNC(bp);
	xfs_biodone(bp);
}

/*
 * This is a callback routine attached to a buffer which gets an error
 * when being written out synchronously.
 */
STATIC void
xfs_buf_error_relse(
	xfs_buf_t	*bp)
{
	xfs_log_item_t	*lip;
	xfs_mount_t	*mp;

	lip = XFS_BUF_FSPRIVATE(bp, xfs_log_item_t *);
	mp = (xfs_mount_t *)lip->li_mountp;
	ASSERT(XFS_BUF_TARGET(bp) == mp->m_ddev_targp);

	XFS_BUF_STALE(bp);
	XFS_BUF_DONE(bp);
	XFS_BUF_UNDELAYWRITE(bp);
	XFS_BUF_ERROR(bp,0);
	xfs_buftrace("BUF_ERROR_RELSE", bp);
	if (! XFS_FORCED_SHUTDOWN(mp))
		xfs_force_shutdown(mp, SHUTDOWN_META_IO_ERROR);
	/*
	 * We have to unpin the pinned buffers so do the
	 * callbacks.
	 */
	xfs_buf_do_callbacks(bp, lip);
	XFS_BUF_SET_FSPRIVATE(bp, NULL);
	XFS_BUF_CLR_IODONE_FUNC(bp);
	XFS_BUF_SET_BRELSE_FUNC(bp,NULL);
	xfs_buf_relse(bp);
}

	struct xfs_buf		*bp)
{
	struct xfs_log_item	*lip = bp->b_fspriv;
	struct xfs_mount	*mp = lip->li_mountp;
	static ulong		lasttime;
	static xfs_buftarg_t	*lasttarg;

	if (likely(!bp->b_error))
		goto do_callbacks;

	/*
	 * If we've already decided to shutdown the filesystem because of
	 * I/O errors, there's no point in giving this a retry.
	 */
	if (XFS_FORCED_SHUTDOWN(mp)) {
		xfs_buf_stale(bp);
		XFS_BUF_DONE(bp);
		trace_xfs_buf_item_iodone(bp, _RET_IP_);
		goto do_callbacks;
	}

	if (bp->b_target != lasttarg ||
	    time_after(jiffies, (lasttime + 5*HZ))) {
		lasttime = jiffies;
		xfs_buf_ioerror_alert(bp, __func__);
	}
	lasttarg = bp->b_target;

	/*
	 * If the write was asynchronous then no one will be looking for the
	 * error.  Clear the error state and write the buffer out again.
	 *
	 * XXX: This helps against transient write errors, but we need to find
	 * a way to shut the filesystem down if the writes keep failing.
	 *
	 * In practice we'll shut the filesystem down soon as non-transient
	 * errors tend to affect the whole device and a failing log write
	 * will make us give up.  But we really ought to do better here.
	 */
	if (XFS_BUF_ISASYNC(bp)) {
		ASSERT(bp->b_iodone != NULL);

		trace_xfs_buf_item_iodone_async(bp, _RET_IP_);

		xfs_buf_ioerror(bp, 0); /* errno of 0 unsets the flag */

		if (!(bp->b_flags & (XBF_STALE|XBF_WRITE_FAIL))) {
			bp->b_flags |= XBF_WRITE | XBF_ASYNC |
				       XBF_DONE | XBF_WRITE_FAIL;
			xfs_buf_submit(bp);
		} else {
			xfs_buf_relse(bp);
		}

		return;
	}

	/*
	 * If the write of the buffer was synchronous, we want to make
	 * sure to return the error to the caller of xfs_bwrite().
	 */
	xfs_buf_stale(bp);
	XFS_BUF_DONE(bp);

	trace_xfs_buf_error_relse(bp, _RET_IP_);

do_callbacks:
	xfs_buf_do_callbacks(bp);
	bp->b_fspriv = NULL;
	bp->b_iodone = NULL;
	xfs_buf_ioend(bp);
}

/*
 * This is the iodone() function for buffers which have been
 * logged.  It is called when they are eventually flushed out.
 * It should remove the buf item from the AIL, and free the buf item.
 * It is called by xfs_buf_iodone_callbacks() above which will take
 * care of cleaning up the buffer itself.
 */
/* ARGSUSED */
void
xfs_buf_iodone(
	xfs_buf_t		*bp,
	xfs_buf_log_item_t	*bip)
{
	struct xfs_mount	*mp;

	ASSERT(bip->bli_buf == bp);

	xfs_buf_rele(bp);
	mp = bip->bli_item.li_mountp;
void
xfs_buf_iodone(
	struct xfs_buf		*bp,
	struct xfs_log_item	*lip)
{
	struct xfs_ail		*ailp = lip->li_ailp;

	ASSERT(BUF_ITEM(lip)->bli_buf == bp);

	xfs_buf_rele(bp);

	/*
	 * If we are forcibly shutting down, this may well be
	 * off the AIL already. That's because we simulate the
	 * log-committed callbacks to unpin these buffers. Or we may never
	 * have put this item on AIL because of the transaction was
	 * aborted forcibly. xfs_trans_delete_ail() takes care of these.
	 *
	 * Either way, AIL is useless if we're forcing a shutdown.
	 */
	spin_lock(&mp->m_ail_lock);
	/*
	 * xfs_trans_delete_ail() drops the AIL lock.
	 */
	xfs_trans_delete_ail(mp, (xfs_log_item_t *)bip);
	xfs_buf_item_free(bip);
}

#if defined(XFS_BLI_TRACE)
void
xfs_buf_item_trace(
	char			*id,
	xfs_buf_log_item_t	*bip)
{
	xfs_buf_t		*bp;
	ASSERT(bip->bli_trace != NULL);

	bp = bip->bli_buf;
	ktrace_enter(bip->bli_trace,
		     (void *)id,
		     (void *)bip->bli_buf,
		     (void *)((unsigned long)bip->bli_flags),
		     (void *)((unsigned long)bip->bli_recur),
		     (void *)((unsigned long)atomic_read(&bip->bli_refcount)),
		     (void *)((unsigned long)
				(0xFFFFFFFF & XFS_BUF_ADDR(bp) >> 32)),
		     (void *)((unsigned long)(0xFFFFFFFF & XFS_BUF_ADDR(bp))),
		     (void *)((unsigned long)XFS_BUF_COUNT(bp)),
		     (void *)((unsigned long)XFS_BUF_BFLAGS(bp)),
		     XFS_BUF_FSPRIVATE(bp, void *),
		     XFS_BUF_FSPRIVATE2(bp, void *),
		     (void *)(unsigned long)XFS_BUF_ISPINNED(bp),
		     (void *)XFS_BUF_IODONE_FUNC(bp),
		     (void *)((unsigned long)(XFS_BUF_VALUSEMA(bp))),
		     (void *)bip->bli_item.li_desc,
		     (void *)((unsigned long)bip->bli_item.li_flags));
}
#endif /* XFS_BLI_TRACE */
	 * aborted forcibly. xfs_trans_ail_delete() takes care of these.
	 *
	 * Either way, AIL is useless if we're forcing a shutdown.
	 */
	spin_lock(&ailp->xa_lock);
	xfs_trans_ail_delete(ailp, lip, SHUTDOWN_CORRUPT_INCORE);
	xfs_buf_item_free(BUF_ITEM(lip));
}
