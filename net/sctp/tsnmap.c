/* SCTP kernel implementation
 * (C) Copyright IBM Corp. 2001, 2004
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001 Intel Corp.
 *
 * This file is part of the SCTP kernel implementation
 *
 * These functions manipulate sctp tsn mapping array.
 *
 * This SCTP implementation is free software;
 * you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This SCTP implementation is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *                 ************************
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <lksctp-developers@lists.sourceforge.net>
 *
 * Or submit a bug report through the following website:
 *    http://www.sf.net/projects/lksctp
 * along with GNU CC; see the file COPYING.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <linux-sctp@vger.kernel.org>
 *
 * Written or modified by:
 *    La Monte H.P. Yarroll <piggy@acm.org>
 *    Jon Grimm             <jgrimm@us.ibm.com>
 *    Karl Knutson          <karl@athena.chicago.il.us>
 *    Sridhar Samudrala     <sri@us.ibm.com>
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#include <linux/types.h>
 */

#include <linux/slab.h>
#include <linux/types.h>
#include <linux/bitmap.h>
#include <net/sctp/sctp.h>
#include <net/sctp/sm.h>

static void sctp_tsnmap_update(struct sctp_tsnmap *map);
static void sctp_tsnmap_find_gap_ack(__u8 *map, __u16 off,
				     __u16 len, __u16 base,
				     int *started, __u16 *start,
				     int *ended, __u16 *end);

/* Initialize a block of memory as a tsnmap.  */
struct sctp_tsnmap *sctp_tsnmap_init(struct sctp_tsnmap *map, __u16 len,
				     __u32 initial_tsn)
{
	map->tsn_map = map->raw_map;
	map->overflow_map = map->tsn_map + len;
	map->len = len;

	/* Clear out a TSN ack status.  */
	memset(map->tsn_map, 0x00, map->len + map->len);

	/* Keep track of TSNs represented by tsn_map.  */
	map->base_tsn = initial_tsn;
	map->overflow_tsn = initial_tsn + map->len;
	map->cumulative_tsn_ack_point = initial_tsn - 1;
	map->max_tsn_seen = map->cumulative_tsn_ack_point;
	map->malloced = 0;
static void sctp_tsnmap_find_gap_ack(unsigned long *map, __u16 off,
				     __u16 len, __u16 *start, __u16 *end);
static int sctp_tsnmap_grow(struct sctp_tsnmap *map, u16 size);

/* Initialize a block of memory as a tsnmap.  */
struct sctp_tsnmap *sctp_tsnmap_init(struct sctp_tsnmap *map, __u16 len,
				     __u32 initial_tsn, gfp_t gfp)
{
	if (!map->tsn_map) {
		map->tsn_map = kzalloc(len>>3, gfp);
		if (map->tsn_map == NULL)
			return NULL;

		map->len = len;
	} else {
		bitmap_zero(map->tsn_map, map->len);
	}

	/* Keep track of TSNs represented by tsn_map.  */
	map->base_tsn = initial_tsn;
	map->cumulative_tsn_ack_point = initial_tsn - 1;
	map->max_tsn_seen = map->cumulative_tsn_ack_point;
	map->num_dup_tsns = 0;

	return map;
}

void sctp_tsnmap_free(struct sctp_tsnmap *map)
{
	map->len = 0;
	kfree(map->tsn_map);
}

/* Test the tracking state of this TSN.
 * Returns:
 *   0 if the TSN has not yet been seen
 *  >0 if the TSN has been seen (duplicate)
 *  <0 if the TSN is invalid (too large to track)
 */
int sctp_tsnmap_check(const struct sctp_tsnmap *map, __u32 tsn)
{
	__s32 gap;
	int dup;
	u32 gap;

	/* Check to see if this is an old TSN */
	if (TSN_lte(tsn, map->cumulative_tsn_ack_point))
		return 1;

	/* Verify that we can hold this TSN and that it will not
	 * overlfow our map
	 */
	if (!TSN_lt(tsn, map->base_tsn + SCTP_TSN_MAP_SIZE))
		return -1;

	/* Calculate the index into the mapping arrays.  */
	gap = tsn - map->base_tsn;

	/* Verify that we can hold this TSN.  */
	if (gap >= (/* base */ map->len + /* overflow */ map->len)) {
		dup = -1;
		goto out;
	}

	/* Honk if we've already seen this TSN.
	 * We have three cases:
	 *	1. The TSN is ancient or belongs to a previous tsn_map.
	 *	2. The TSN is already marked in the tsn_map.
	 *	3. The TSN is already marked in the tsn_map_overflow.
	 */
	if (gap < 0 ||
	    (gap < map->len && map->tsn_map[gap]) ||
	    (gap >= map->len && map->overflow_map[gap - map->len]))
		dup = 1;
	else
		dup = 0;

out:
	return dup;
	/* Check to see if TSN has already been recorded.  */
	if (gap < map->len && test_bit(gap, map->tsn_map))
		return 1;
	else
		return 0;
}


/* Mark this TSN as seen.  */
void sctp_tsnmap_mark(struct sctp_tsnmap *map, __u32 tsn)
{
	__s32 gap;

	/* Vacuously mark any TSN which precedes the map base or
	 * exceeds the end of the map.
	 */
	if (TSN_lt(tsn, map->base_tsn))
		return;
	if (!TSN_lt(tsn, map->base_tsn + map->len + map->len))
		return;

	/* Bump the max.  */
	if (TSN_lt(map->max_tsn_seen, tsn))
		map->max_tsn_seen = tsn;

	/* Assert: TSN is in range.  */
	gap = tsn - map->base_tsn;

	/* Mark the TSN as received.  */
	if (gap < map->len)
		map->tsn_map[gap]++;
	else
		map->overflow_map[gap - map->len]++;

	/* Go fixup any internal TSN mapping variables including
	 * cumulative_tsn_ack_point.
	 */
	sctp_tsnmap_update(map);
int sctp_tsnmap_mark(struct sctp_tsnmap *map, __u32 tsn,
		     struct sctp_transport *trans)
{
	u16 gap;

	if (TSN_lt(tsn, map->base_tsn))
		return 0;

	gap = tsn - map->base_tsn;

	if (gap >= map->len && !sctp_tsnmap_grow(map, gap + 1))
		return -ENOMEM;

	if (!sctp_tsnmap_has_gap(map) && gap == 0) {
		/* In this case the map has no gaps and the tsn we are
		 * recording is the next expected tsn.  We don't touch
		 * the map but simply bump the values.
		 */
		map->max_tsn_seen++;
		map->cumulative_tsn_ack_point++;
		if (trans)
			trans->sack_generation =
				trans->asoc->peer.sack_generation;
		map->base_tsn++;
	} else {
		/* Either we already have a gap, or about to record a gap, so
		 * have work to do.
		 *
		 * Bump the max.
		 */
		if (TSN_lt(map->max_tsn_seen, tsn))
			map->max_tsn_seen = tsn;

		/* Mark the TSN as received.  */
		set_bit(gap, map->tsn_map);

		/* Go fixup any internal TSN mapping variables including
		 * cumulative_tsn_ack_point.
		 */
		sctp_tsnmap_update(map);
	}

	return 0;
}


/* Initialize a Gap Ack Block iterator from memory being provided.  */
SCTP_STATIC void sctp_tsnmap_iter_init(const struct sctp_tsnmap *map,
				       struct sctp_tsnmap_iter *iter)
static void sctp_tsnmap_iter_init(const struct sctp_tsnmap *map,
				  struct sctp_tsnmap_iter *iter)
{
	/* Only start looking one past the Cumulative TSN Ack Point.  */
	iter->start = map->cumulative_tsn_ack_point + 1;
}

/* Get the next Gap Ack Blocks. Returns 0 if there was not another block
 * to get.
 */
SCTP_STATIC int sctp_tsnmap_next_gap_ack(const struct sctp_tsnmap *map,
					 struct sctp_tsnmap_iter *iter,
					 __u16 *start, __u16 *end)
{
	int started, ended;
	__u16 start_, end_, offset;

	/* We haven't found a gap yet.  */
	started = ended = 0;
static int sctp_tsnmap_next_gap_ack(const struct sctp_tsnmap *map,
				    struct sctp_tsnmap_iter *iter,
				    __u16 *start, __u16 *end)
{
	int ended = 0;
	__u16 start_ = 0, end_ = 0, offset;

	/* If there are no more gap acks possible, get out fast.  */
	if (TSN_lte(map->max_tsn_seen, iter->start))
		return 0;

	/* Search the first mapping array.  */
	if (iter->start - map->base_tsn < map->len) {

		offset = iter->start - map->base_tsn;
		sctp_tsnmap_find_gap_ack(map->tsn_map, offset, map->len, 0,
					 &started, &start_, &ended, &end_);
	}

	/* Do we need to check the overflow map? */
	if (!ended) {
		/* Fix up where we'd like to start searching in the
		 * overflow map.
		 */
		if (iter->start - map->base_tsn < map->len)
			offset = 0;
		else
			offset = iter->start - map->base_tsn - map->len;

		/* Search the overflow map.  */
		sctp_tsnmap_find_gap_ack(map->overflow_map,
					 offset,
					 map->len,
					 map->len,
					 &started, &start_,
					 &ended, &end_);
	}

	/* The Gap Ack Block happens to end at the end of the
	 * overflow map.
	 */
	if (started && !ended) {
		ended++;
		end_ = map->len + map->len - 1;
	}
	offset = iter->start - map->base_tsn;
	sctp_tsnmap_find_gap_ack(map->tsn_map, offset, map->len,
				 &start_, &end_);

	/* The Gap Ack Block happens to end at the end of the map. */
	if (start_ && !end_)
		end_ = map->len - 1;

	/* If we found a Gap Ack Block, return the start and end and
	 * bump the iterator forward.
	 */
	if (ended) {
		/* Fix up the start and end based on the
		 * Cumulative TSN Ack offset into the map.
		 */
		int gap = map->cumulative_tsn_ack_point -
			map->base_tsn;

		*start = start_ - gap;
		*end = end_ - gap;

		/* Move the iterator forward.  */
		iter->start = map->cumulative_tsn_ack_point + *end + 1;
	if (end_) {
		/* Fix up the start and end based on the
		 * Cumulative TSN Ack which is always 1 behind base.
		 */
		*start = start_ + 1;
		*end = end_ + 1;

		/* Move the iterator forward.  */
		iter->start = map->cumulative_tsn_ack_point + *end + 1;
		ended = 1;
	}

	return ended;
}

/* Mark this and any lower TSN as seen.  */
void sctp_tsnmap_skip(struct sctp_tsnmap *map, __u32 tsn)
{
	__s32 gap;

	/* Vacuously mark any TSN which precedes the map base or
	 * exceeds the end of the map.
	 */
	if (TSN_lt(tsn, map->base_tsn))
		return;
	if (!TSN_lt(tsn, map->base_tsn + map->len + map->len))
	u32 gap;

	if (TSN_lt(tsn, map->base_tsn))
		return;
	if (!TSN_lt(tsn, map->base_tsn + SCTP_TSN_MAP_SIZE))
		return;

	/* Bump the max.  */
	if (TSN_lt(map->max_tsn_seen, tsn))
		map->max_tsn_seen = tsn;

	/* Assert: TSN is in range.  */
	gap = tsn - map->base_tsn + 1;

	/* Mark the TSNs as received.  */
	if (gap <= map->len)
		memset(map->tsn_map, 0x01, gap);
	else {
		memset(map->tsn_map, 0x01, map->len);
		memset(map->overflow_map, 0x01, (gap - map->len));
	}

	/* Go fixup any internal TSN mapping variables including
	 * cumulative_tsn_ack_point.
	 */
	sctp_tsnmap_update(map);
	gap = tsn - map->base_tsn + 1;

	map->base_tsn += gap;
	map->cumulative_tsn_ack_point += gap;
	if (gap >= map->len) {
		/* If our gap is larger then the map size, just
		 * zero out the map.
		 */
		bitmap_zero(map->tsn_map, map->len);
	} else {
		/* If the gap is smaller than the map size,
		 * shift the map by 'gap' bits and update further.
		 */
		bitmap_shift_right(map->tsn_map, map->tsn_map, gap, map->len);
		sctp_tsnmap_update(map);
	}
}

/********************************************************************
 * 2nd Level Abstractions
 ********************************************************************/

/* This private helper function updates the tsnmap buffers and
 * the Cumulative TSN Ack Point.
 */
static void sctp_tsnmap_update(struct sctp_tsnmap *map)
{
	__u32 ctsn;

	ctsn = map->cumulative_tsn_ack_point;
	do {
		ctsn++;
		if (ctsn == map->overflow_tsn) {
			/* Now tsn_map must have been all '1's,
			 * so we swap the map and check the overflow table
			 */
			__u8 *tmp = map->tsn_map;
			memset(tmp, 0, map->len);
			map->tsn_map = map->overflow_map;
			map->overflow_map = tmp;

			/* Update the tsn_map boundaries.  */
			map->base_tsn += map->len;
			map->overflow_tsn += map->len;
		}
	} while (map->tsn_map[ctsn - map->base_tsn]);

	map->cumulative_tsn_ack_point = ctsn - 1; /* Back up one. */
	u16 len;
	unsigned long zero_bit;


	len = map->max_tsn_seen - map->cumulative_tsn_ack_point;
	zero_bit = find_first_zero_bit(map->tsn_map, len);
	if (!zero_bit)
		return;		/* The first 0-bit is bit 0.  nothing to do */

	map->base_tsn += zero_bit;
	map->cumulative_tsn_ack_point += zero_bit;

	bitmap_shift_right(map->tsn_map, map->tsn_map, zero_bit, map->len);
}

/* How many data chunks  are we missing from our peer?
 */
__u16 sctp_tsnmap_pending(struct sctp_tsnmap *map)
{
	__u32 cum_tsn = map->cumulative_tsn_ack_point;
	__u32 max_tsn = map->max_tsn_seen;
	__u32 base_tsn = map->base_tsn;
	__u16 pending_data;
	__s32 gap, start, end, i;
	u32 gap;

	pending_data = max_tsn - cum_tsn;
	gap = max_tsn - base_tsn;

	if (gap <= 0 || gap >= (map->len + map->len))
		goto out;

	start = ((cum_tsn >= base_tsn) ? (cum_tsn - base_tsn + 1) : 0);
	end = ((gap > map->len ) ? map->len : gap + 1);

	for (i = start; i < end; i++) {
		if (map->tsn_map[i])
			pending_data--;
	}

	if (gap >= map->len) {
		start = 0;
		end = gap - map->len + 1;
		for (i = start; i < end; i++) {
			if (map->overflow_map[i])
				pending_data--;
		}
	}

	if (gap == 0 || gap >= map->len)
		goto out;

	pending_data -= bitmap_weight(map->tsn_map, gap + 1);
out:
	return pending_data;
}

/* This is a private helper for finding Gap Ack Blocks.  It searches a
 * single array for the start and end of a Gap Ack Block.
 *
 * The flags "started" and "ended" tell is if we found the beginning
 * or (respectively) the end of a Gap Ack Block.
 */
static void sctp_tsnmap_find_gap_ack(__u8 *map, __u16 off,
				     __u16 len, __u16 base,
				     int *started, __u16 *start,
				     int *ended, __u16 *end)
static void sctp_tsnmap_find_gap_ack(unsigned long *map, __u16 off,
				     __u16 len, __u16 *start, __u16 *end)
{
	int i = off;

	/* Look through the entire array, but break out
	 * early if we have found the end of the Gap Ack Block.
	 */

	/* Also, stop looking past the maximum TSN seen. */

	/* Look for the start. */
	if (!(*started)) {
		for (; i < len; i++) {
			if (map[i]) {
				(*started)++;
				*start = base + i;
				break;
			}
		}
	}

	/* Look for the end.  */
	if (*started) {
		/* We have found the start, let's find the
		 * end.  If we find the end, break out.
		 */
		for (; i < len; i++) {
			if (!map[i]) {
				(*ended)++;
				*end = base + i - 1;
				break;
			}
		}
	i = find_next_bit(map, len, off);
	if (i < len)
		*start = i;

	/* Look for the end.  */
	if (*start) {
		/* We have found the start, let's find the
		 * end.  If we find the end, break out.
		 */
		i = find_next_zero_bit(map, len, i);
		if (i < len)
			*end = i - 1;
	}
}

/* Renege that we have seen a TSN.  */
void sctp_tsnmap_renege(struct sctp_tsnmap *map, __u32 tsn)
{
	__s32 gap;

	if (TSN_lt(tsn, map->base_tsn))
		return;
	if (!TSN_lt(tsn, map->base_tsn + map->len + map->len))
		return;

	/* Assert: TSN is in range.  */
	gap = tsn - map->base_tsn;

	/* Pretend we never saw the TSN.  */
	if (gap < map->len)
		map->tsn_map[gap] = 0;
	else
		map->overflow_map[gap - map->len] = 0;
}

/* How many gap ack blocks do we have recorded? */
__u16 sctp_tsnmap_num_gabs(struct sctp_tsnmap *map)
{
	struct sctp_tsnmap_iter iter;
	int gabs = 0;

	/* Refresh the gap ack information. */
	if (sctp_tsnmap_has_gap(map)) {
		__u16 start, end;
	u32 gap;

	if (TSN_lt(tsn, map->base_tsn))
		return;
	/* Assert: TSN is in range.  */
	if (!TSN_lt(tsn, map->base_tsn + map->len))
		return;

	gap = tsn - map->base_tsn;

	/* Pretend we never saw the TSN.  */
	clear_bit(gap, map->tsn_map);
}

/* How many gap ack blocks do we have recorded? */
__u16 sctp_tsnmap_num_gabs(struct sctp_tsnmap *map,
			   struct sctp_gap_ack_block *gabs)
{
	struct sctp_tsnmap_iter iter;
	int ngaps = 0;

	/* Refresh the gap ack information. */
	if (sctp_tsnmap_has_gap(map)) {
		__u16 start = 0, end = 0;
		sctp_tsnmap_iter_init(map, &iter);
		while (sctp_tsnmap_next_gap_ack(map, &iter,
						&start,
						&end)) {

			map->gabs[gabs].start = htons(start);
			map->gabs[gabs].end = htons(end);
			gabs++;
			if (gabs >= SCTP_MAX_GABS)
				break;
		}
	}
	return gabs;
			gabs[ngaps].start = htons(start);
			gabs[ngaps].end = htons(end);
			ngaps++;
			if (ngaps >= SCTP_MAX_GABS)
				break;
		}
	}
	return ngaps;
}

static int sctp_tsnmap_grow(struct sctp_tsnmap *map, u16 size)
{
	unsigned long *new;
	unsigned long inc;
	u16  len;

	if (size > SCTP_TSN_MAP_SIZE)
		return 0;

	inc = ALIGN((size - map->len), BITS_PER_LONG) + SCTP_TSN_MAP_INCREMENT;
	len = min_t(u16, map->len + inc, SCTP_TSN_MAP_SIZE);

	new = kzalloc(len>>3, GFP_ATOMIC);
	if (!new)
		return 0;

	bitmap_copy(new, map->tsn_map,
		map->max_tsn_seen - map->cumulative_tsn_ack_point);
	kfree(map->tsn_map);
	map->tsn_map = new;
	map->len = len;

	return 1;
}
