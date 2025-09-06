/*
 * raid5.c : Multiple Devices driver for Linux
 *	   Copyright (C) 1996, 1997 Ingo Molnar, Miguel de Icaza, Gadi Oxman
 *	   Copyright (C) 1999, 2000 Ingo Molnar
 *	   Copyright (C) 2002, 2003 H. Peter Anvin
 *
 * RAID-4/5/6 management functions.
 * Thanks to Penguin Computing for making the RAID-6 development possible
 * by donating a test server!
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * You should have received a copy of the GNU General Public License
 * (for example /usr/src/linux/COPYING); if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * BITMAP UNPLUGGING:
 *
 * The sequencing for updating the bitmap reliably is a little
 * subtle (and I got it wrong the first time) so it deserves some
 * explanation.
 *
 * We group bitmap updates into batches.  Each batch has a number.
 * We may write out several batches at once, but that isn't very important.
 * conf->bm_write is the number of the last batch successfully written.
 * conf->bm_flush is the number of the last batch that was closed to
 *    new additions.
 * When we discover that we will need to write to any block in a stripe
 * (in add_stripe_bio) we update the in-memory bitmap and record in sh->bm_seq
 * the number of the batch it will be in. This is bm_flush+1.
 * conf->seq_write is the number of the last batch successfully written.
 * conf->seq_flush is the number of the last batch that was closed to
 *    new additions.
 * When we discover that we will need to write to any block in a stripe
 * (in add_stripe_bio) we update the in-memory bitmap and record in sh->bm_seq
 * the number of the batch it will be in. This is seq_flush+1.
 * When we are ready to do a write, if that batch hasn't been written yet,
 *   we plug the array and queue the stripe for later.
 * When an unplug happens, we increment bm_flush, thus closing the current
 *   batch.
 * When we notice that bm_flush > bm_write, we write out all pending updates
 * to the bitmap, and advance bm_write to where bm_flush was.
 * This may occasionally write a bit out twice, but is sure never to
 * miss any bits.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/bitops.h>
#include <linux/kthread.h>
#include <asm/atomic.h>
#include "raid6.h"

#include <linux/raid/bitmap.h>
#include <linux/async_tx.h>

#include <linux/blkdev.h>
#include <linux/kthread.h>
#include <linux/raid/pq.h>
#include <linux/async_tx.h>
#include <linux/module.h>
#include <linux/async.h>
#include <linux/seq_file.h>
#include <linux/cpu.h>
#include <linux/slab.h>
#include <linux/ratelimit.h>
#include <linux/nodemask.h>
#include <linux/flex_array.h>
#include <trace/events/block.h>

#include "md.h"
#include "raid5.h"
#include "raid0.h"
#include "bitmap.h"

#define cpu_to_group(cpu) cpu_to_node(cpu)
#define ANY_GROUP NUMA_NO_NODE

static bool devices_handle_discard_safely = false;
module_param(devices_handle_discard_safely, bool, 0644);
MODULE_PARM_DESC(devices_handle_discard_safely,
		 "Set to Y if all devices in each array reliably return zeroes on reads from discarded regions");
static struct workqueue_struct *raid5_wq;
/*
 * Stripe cache
 */

#define NR_STRIPES		256
#define STRIPE_SIZE		PAGE_SIZE
#define STRIPE_SHIFT		(PAGE_SHIFT - 9)
#define STRIPE_SECTORS		(STRIPE_SIZE>>9)
#define	IO_THRESHOLD		1
#define BYPASS_THRESHOLD	1
#define NR_HASH			(PAGE_SIZE / sizeof(struct hlist_head))
#define HASH_MASK		(NR_HASH - 1)

#define stripe_hash(conf, sect)	(&((conf)->stripe_hashtbl[((sect) >> STRIPE_SHIFT) & HASH_MASK]))
#define MAX_STRIPE_BATCH	8

static inline struct hlist_head *stripe_hash(struct r5conf *conf, sector_t sect)
{
	int hash = (sect >> STRIPE_SHIFT) & HASH_MASK;
	return &conf->stripe_hashtbl[hash];
}

static inline int stripe_hash_locks_hash(sector_t sect)
{
	return (sect >> STRIPE_SHIFT) & STRIPE_HASH_LOCKS_MASK;
}

static inline void lock_device_hash_lock(struct r5conf *conf, int hash)
{
	spin_lock_irq(conf->hash_locks + hash);
	spin_lock(&conf->device_lock);
}

static inline void unlock_device_hash_lock(struct r5conf *conf, int hash)
{
	spin_unlock(&conf->device_lock);
	spin_unlock_irq(conf->hash_locks + hash);
}

static inline void lock_all_device_hash_locks_irq(struct r5conf *conf)
{
	int i;
	spin_lock_irq(conf->hash_locks);
	for (i = 1; i < NR_STRIPE_HASH_LOCKS; i++)
		spin_lock_nest_lock(conf->hash_locks + i, conf->hash_locks);
	spin_lock(&conf->device_lock);
}

static inline void unlock_all_device_hash_locks_irq(struct r5conf *conf)
{
	int i;
	spin_unlock(&conf->device_lock);
	for (i = NR_STRIPE_HASH_LOCKS - 1; i; i--)
		spin_unlock(conf->hash_locks + i);
	spin_unlock_irq(conf->hash_locks);
}

/* bio's attached to a stripe+device for I/O are linked together in bi_sector
 * order without overlap.  There may be several bio's per stripe+device, and
 * a bio could span several devices.
 * When walking this list for a particular stripe+device, we must never proceed
 * beyond a bio that extends past this device, as the next bio might no longer
 * be valid.
 * This macro is used to determine the 'next' bio in the list, given the sector
 * of the current stripe+device
 */
#define r5_next_bio(bio, sect) ( ( (bio)->bi_sector + ((bio)->bi_size>>9) < sect + STRIPE_SECTORS) ? (bio)->bi_next : NULL)
/*
 * The following can be used to debug the driver
 */
#define RAID5_PARANOIA	1
#if RAID5_PARANOIA && defined(CONFIG_SMP)
# define CHECK_DEVLOCK() assert_spin_locked(&conf->device_lock)
#else
# define CHECK_DEVLOCK()
#endif

#ifdef DEBUG
#define inline
#define __inline__
#endif

#define printk_rl(args...) ((void) (printk_ratelimit() && printk(args)))

#if !RAID6_USE_EMPTY_ZERO_PAGE
/* In .bss so it's zeroed */
const char raid6_empty_zero_page[PAGE_SIZE] __attribute__((aligned(256)));
#endif

 * This function is used to determine the 'next' bio in the list, given the sector
 * of the current stripe+device
 */
static inline struct bio *r5_next_bio(struct bio *bio, sector_t sector)
{
	int sectors = bio_sectors(bio);
	if (bio->bi_iter.bi_sector + sectors < sector + STRIPE_SECTORS)
		return bio->bi_next;
	else
		return NULL;
}

/*
 * We maintain a biased count of active stripes in the bottom 16 bits of
 * bi_phys_segments, and a count of processed stripes in the upper 16 bits
 */
static inline int raid5_bi_processed_stripes(struct bio *bio)
{
	atomic_t *segments = (atomic_t *)&bio->bi_phys_segments;
	return (atomic_read(segments) >> 16) & 0xffff;
}

static inline int raid5_dec_bi_active_stripes(struct bio *bio)
{
	atomic_t *segments = (atomic_t *)&bio->bi_phys_segments;
	return atomic_sub_return(1, segments) & 0xffff;
}

static inline void raid5_inc_bi_active_stripes(struct bio *bio)
{
	atomic_t *segments = (atomic_t *)&bio->bi_phys_segments;
	atomic_inc(segments);
}

static inline void raid5_set_bi_processed_stripes(struct bio *bio,
	unsigned int cnt)
{
	atomic_t *segments = (atomic_t *)&bio->bi_phys_segments;
	int old, new;

	do {
		old = atomic_read(segments);
		new = (old & 0xffff) | (cnt << 16);
	} while (atomic_cmpxchg(segments, old, new) != old);
}

static inline void raid5_set_bi_stripes(struct bio *bio, unsigned int cnt)
{
	atomic_t *segments = (atomic_t *)&bio->bi_phys_segments;
	atomic_set(segments, cnt);
}

/* Find first data disk in a raid6 stripe */
static inline int raid6_d0(struct stripe_head *sh)
{
	if (sh->ddf_layout)
		/* ddf always start from first device */
		return 0;
	/* md starts just after Q block */
	if (sh->qd_idx == sh->disks - 1)
		return 0;
	else
		return sh->qd_idx + 1;
}
static inline int raid6_next_disk(int disk, int raid_disks)
{
	disk++;
	return (disk < raid_disks) ? disk : 0;
}

static void return_io(struct bio *return_bi)
{
	struct bio *bi = return_bi;
	while (bi) {

		return_bi = bi->bi_next;
		bi->bi_next = NULL;
		bi->bi_size = 0;
		bio_endio(bi, 0);
		bi = return_bi;
	}
}

static void print_raid5_conf (raid5_conf_t *conf);
/* When walking through the disks in a raid5, starting at raid6_d0,
 * We need to map each disk to a 'slot', where the data disks are slot
 * 0 .. raid_disks-3, the parity disk is raid_disks-2 and the Q disk
 * is raid_disks-1.  This help does that mapping.
 */
static int raid6_idx_to_slot(int idx, struct stripe_head *sh,
			     int *count, int syndrome_disks)
{
	int slot = *count;

	if (sh->ddf_layout)
		(*count)++;
	if (idx == sh->pd_idx)
		return syndrome_disks;
	if (idx == sh->qd_idx)
		return syndrome_disks + 1;
	if (!sh->ddf_layout)
		(*count)++;
	return slot;
}

static void return_io(struct bio_list *return_bi)
{
	struct bio *bi;
	while ((bi = bio_list_pop(return_bi)) != NULL) {
		bi->bi_iter.bi_size = 0;
		trace_block_bio_complete(bdev_get_queue(bi->bi_bdev),
					 bi, 0);
		bio_endio(bi);
	}
}

static void print_raid5_conf (struct r5conf *conf);

static int stripe_operations_active(struct stripe_head *sh)
{
	return sh->check_state || sh->reconstruct_state ||
	       test_bit(STRIPE_BIOFILL_RUN, &sh->state) ||
	       test_bit(STRIPE_COMPUTE_RUN, &sh->state);
}

static void __release_stripe(raid5_conf_t *conf, struct stripe_head *sh)
{
	if (atomic_dec_and_test(&sh->count)) {
		BUG_ON(!list_empty(&sh->lru));
		BUG_ON(atomic_read(&conf->active_stripes)==0);
		if (test_bit(STRIPE_HANDLE, &sh->state)) {
			if (test_bit(STRIPE_DELAYED, &sh->state)) {
				list_add_tail(&sh->lru, &conf->delayed_list);
				blk_plug_device(conf->mddev->queue);
			} else if (test_bit(STRIPE_BIT_DELAY, &sh->state) &&
				   sh->bm_seq - conf->seq_write > 0) {
				list_add_tail(&sh->lru, &conf->bitmap_list);
				blk_plug_device(conf->mddev->queue);
			} else {
				clear_bit(STRIPE_BIT_DELAY, &sh->state);
				list_add_tail(&sh->lru, &conf->handle_list);
			}
			md_wakeup_thread(conf->mddev->thread);
		} else {
			BUG_ON(stripe_operations_active(sh));
			if (test_and_clear_bit(STRIPE_PREREAD_ACTIVE, &sh->state)) {
				atomic_dec(&conf->preread_active_stripes);
				if (atomic_read(&conf->preread_active_stripes) < IO_THRESHOLD)
					md_wakeup_thread(conf->mddev->thread);
			}
			atomic_dec(&conf->active_stripes);
			if (!test_bit(STRIPE_EXPANDING, &sh->state)) {
				list_add_tail(&sh->lru, &conf->inactive_list);
				wake_up(&conf->wait_for_stripe);
				if (conf->retry_read_aligned)
					md_wakeup_thread(conf->mddev->thread);
			}
		}
	}
}
static void release_stripe(struct stripe_head *sh)
{
	raid5_conf_t *conf = sh->raid_conf;
	unsigned long flags;

	spin_lock_irqsave(&conf->device_lock, flags);
	__release_stripe(conf, sh);
	spin_unlock_irqrestore(&conf->device_lock, flags);
static void raid5_wakeup_stripe_thread(struct stripe_head *sh)
{
	struct r5conf *conf = sh->raid_conf;
	struct r5worker_group *group;
	int thread_cnt;
	int i, cpu = sh->cpu;

	if (!cpu_online(cpu)) {
		cpu = cpumask_any(cpu_online_mask);
		sh->cpu = cpu;
	}

	if (list_empty(&sh->lru)) {
		struct r5worker_group *group;
		group = conf->worker_groups + cpu_to_group(cpu);
		list_add_tail(&sh->lru, &group->handle_list);
		group->stripes_cnt++;
		sh->group = group;
	}

	if (conf->worker_cnt_per_group == 0) {
		md_wakeup_thread(conf->mddev->thread);
		return;
	}

	group = conf->worker_groups + cpu_to_group(sh->cpu);

	group->workers[0].working = true;
	/* at least one worker should run to avoid race */
	queue_work_on(sh->cpu, raid5_wq, &group->workers[0].work);

	thread_cnt = group->stripes_cnt / MAX_STRIPE_BATCH - 1;
	/* wakeup more workers */
	for (i = 1; i < conf->worker_cnt_per_group && thread_cnt > 0; i++) {
		if (group->workers[i].working == false) {
			group->workers[i].working = true;
			queue_work_on(sh->cpu, raid5_wq,
				      &group->workers[i].work);
			thread_cnt--;
		}
	}
}

static void do_release_stripe(struct r5conf *conf, struct stripe_head *sh,
			      struct list_head *temp_inactive_list)
{
	BUG_ON(!list_empty(&sh->lru));
	BUG_ON(atomic_read(&conf->active_stripes)==0);
	if (test_bit(STRIPE_HANDLE, &sh->state)) {
		if (test_bit(STRIPE_DELAYED, &sh->state) &&
		    !test_bit(STRIPE_PREREAD_ACTIVE, &sh->state))
			list_add_tail(&sh->lru, &conf->delayed_list);
		else if (test_bit(STRIPE_BIT_DELAY, &sh->state) &&
			   sh->bm_seq - conf->seq_write > 0)
			list_add_tail(&sh->lru, &conf->bitmap_list);
		else {
			clear_bit(STRIPE_DELAYED, &sh->state);
			clear_bit(STRIPE_BIT_DELAY, &sh->state);
			if (conf->worker_cnt_per_group == 0) {
				list_add_tail(&sh->lru, &conf->handle_list);
			} else {
				raid5_wakeup_stripe_thread(sh);
				return;
			}
		}
		md_wakeup_thread(conf->mddev->thread);
	} else {
		BUG_ON(stripe_operations_active(sh));
		if (test_and_clear_bit(STRIPE_PREREAD_ACTIVE, &sh->state))
			if (atomic_dec_return(&conf->preread_active_stripes)
			    < IO_THRESHOLD)
				md_wakeup_thread(conf->mddev->thread);
		atomic_dec(&conf->active_stripes);
		if (!test_bit(STRIPE_EXPANDING, &sh->state))
			list_add_tail(&sh->lru, temp_inactive_list);
	}
}

static void __release_stripe(struct r5conf *conf, struct stripe_head *sh,
			     struct list_head *temp_inactive_list)
{
	if (atomic_dec_and_test(&sh->count))
		do_release_stripe(conf, sh, temp_inactive_list);
}

/*
 * @hash could be NR_STRIPE_HASH_LOCKS, then we have a list of inactive_list
 *
 * Be careful: Only one task can add/delete stripes from temp_inactive_list at
 * given time. Adding stripes only takes device lock, while deleting stripes
 * only takes hash lock.
 */
static void release_inactive_stripe_list(struct r5conf *conf,
					 struct list_head *temp_inactive_list,
					 int hash)
{
	int size;
	bool do_wakeup = false;
	unsigned long flags;

	if (hash == NR_STRIPE_HASH_LOCKS) {
		size = NR_STRIPE_HASH_LOCKS;
		hash = NR_STRIPE_HASH_LOCKS - 1;
	} else
		size = 1;
	while (size) {
		struct list_head *list = &temp_inactive_list[size - 1];

		/*
		 * We don't hold any lock here yet, raid5_get_active_stripe() might
		 * remove stripes from the list
		 */
		if (!list_empty_careful(list)) {
			spin_lock_irqsave(conf->hash_locks + hash, flags);
			if (list_empty(conf->inactive_list + hash) &&
			    !list_empty(list))
				atomic_dec(&conf->empty_inactive_list_nr);
			list_splice_tail_init(list, conf->inactive_list + hash);
			do_wakeup = true;
			spin_unlock_irqrestore(conf->hash_locks + hash, flags);
		}
		size--;
		hash--;
	}

	if (do_wakeup) {
		wake_up(&conf->wait_for_stripe);
		if (atomic_read(&conf->active_stripes) == 0)
			wake_up(&conf->wait_for_quiescent);
		if (conf->retry_read_aligned)
			md_wakeup_thread(conf->mddev->thread);
	}
}

/* should hold conf->device_lock already */
static int release_stripe_list(struct r5conf *conf,
			       struct list_head *temp_inactive_list)
{
	struct stripe_head *sh;
	int count = 0;
	struct llist_node *head;

	head = llist_del_all(&conf->released_stripes);
	head = llist_reverse_order(head);
	while (head) {
		int hash;

		sh = llist_entry(head, struct stripe_head, release_list);
		head = llist_next(head);
		/* sh could be readded after STRIPE_ON_RELEASE_LIST is cleard */
		smp_mb();
		clear_bit(STRIPE_ON_RELEASE_LIST, &sh->state);
		/*
		 * Don't worry the bit is set here, because if the bit is set
		 * again, the count is always > 1. This is true for
		 * STRIPE_ON_UNPLUG_LIST bit too.
		 */
		hash = sh->hash_lock_index;
		__release_stripe(conf, sh, &temp_inactive_list[hash]);
		count++;
	}

	return count;
}

void raid5_release_stripe(struct stripe_head *sh)
{
	struct r5conf *conf = sh->raid_conf;
	unsigned long flags;
	struct list_head list;
	int hash;
	bool wakeup;

	/* Avoid release_list until the last reference.
	 */
	if (atomic_add_unless(&sh->count, -1, 1))
		return;

	if (unlikely(!conf->mddev->thread) ||
		test_and_set_bit(STRIPE_ON_RELEASE_LIST, &sh->state))
		goto slow_path;
	wakeup = llist_add(&sh->release_list, &conf->released_stripes);
	if (wakeup)
		md_wakeup_thread(conf->mddev->thread);
	return;
slow_path:
	local_irq_save_nort(flags);
	/* we are ok here if STRIPE_ON_RELEASE_LIST is set or not */
	if (atomic_dec_and_lock(&sh->count, &conf->device_lock)) {
		INIT_LIST_HEAD(&list);
		hash = sh->hash_lock_index;
		do_release_stripe(conf, sh, &list);
		spin_unlock(&conf->device_lock);
		release_inactive_stripe_list(conf, &list, hash);
	}
	local_irq_restore_nort(flags);
}

static inline void remove_hash(struct stripe_head *sh)
{
	pr_debug("remove_hash(), stripe %llu\n",
		(unsigned long long)sh->sector);

	hlist_del_init(&sh->hash);
}

static inline void insert_hash(raid5_conf_t *conf, struct stripe_head *sh)
static inline void insert_hash(struct r5conf *conf, struct stripe_head *sh)
{
	struct hlist_head *hp = stripe_hash(conf, sh->sector);

	pr_debug("insert_hash(), stripe %llu\n",
		(unsigned long long)sh->sector);

	CHECK_DEVLOCK();
	hlist_add_head(&sh->hash, hp);
}


/* find an idle stripe, make sure it is unhashed, and return it. */
static struct stripe_head *get_free_stripe(raid5_conf_t *conf)
	hlist_add_head(&sh->hash, hp);
}

/* find an idle stripe, make sure it is unhashed, and return it. */
static struct stripe_head *get_free_stripe(struct r5conf *conf, int hash)
{
	struct stripe_head *sh = NULL;
	struct list_head *first;

	CHECK_DEVLOCK();
	if (list_empty(&conf->inactive_list))
		goto out;
	first = conf->inactive_list.next;
	if (list_empty(conf->inactive_list + hash))
		goto out;
	first = (conf->inactive_list + hash)->next;
	sh = list_entry(first, struct stripe_head, lru);
	list_del_init(first);
	remove_hash(sh);
	atomic_inc(&conf->active_stripes);
	BUG_ON(hash != sh->hash_lock_index);
	if (list_empty(conf->inactive_list + hash))
		atomic_inc(&conf->empty_inactive_list_nr);
out:
	return sh;
}

static void shrink_buffers(struct stripe_head *sh, int num)
{
	struct page *p;
	int i;

	for (i=0; i<num ; i++) {
static void shrink_buffers(struct stripe_head *sh)
{
	struct page *p;
	int i;
	int num = sh->raid_conf->pool_size;

	for (i = 0; i < num ; i++) {
		WARN_ON(sh->dev[i].page != sh->dev[i].orig_page);
		p = sh->dev[i].page;
		if (!p)
			continue;
		sh->dev[i].page = NULL;
		put_page(p);
	}
}

static int grow_buffers(struct stripe_head *sh, int num)
{
	int i;

	for (i=0; i<num; i++) {
		struct page *page;

		if (!(page = alloc_page(GFP_KERNEL))) {
			return 1;
		}
		sh->dev[i].page = page;
static int grow_buffers(struct stripe_head *sh, gfp_t gfp)
{
	int i;
	int num = sh->raid_conf->pool_size;

	for (i = 0; i < num; i++) {
		struct page *page;

		if (!(page = alloc_page(gfp))) {
			return 1;
		}
		sh->dev[i].page = page;
		sh->dev[i].orig_page = page;
	}
	return 0;
}

static void raid5_build_block (struct stripe_head *sh, int i);

static void init_stripe(struct stripe_head *sh, sector_t sector, int pd_idx, int disks)
{
	raid5_conf_t *conf = sh->raid_conf;
	int i;
static void raid5_build_block(struct stripe_head *sh, int i, int previous);
static void stripe_set_idx(sector_t stripe, struct r5conf *conf, int previous,
			    struct stripe_head *sh);

static void init_stripe(struct stripe_head *sh, sector_t sector, int previous)
{
	struct r5conf *conf = sh->raid_conf;
	int i, seq;

	BUG_ON(atomic_read(&sh->count) != 0);
	BUG_ON(test_bit(STRIPE_HANDLE, &sh->state));
	BUG_ON(stripe_operations_active(sh));

	CHECK_DEVLOCK();
	pr_debug("init_stripe called, stripe %llu\n",
		(unsigned long long)sh->sector);

	remove_hash(sh);

	sh->sector = sector;
	sh->pd_idx = pd_idx;
	sh->state = 0;

	sh->disks = disks;

	BUG_ON(sh->batch_head);

	pr_debug("init_stripe called, stripe %llu\n",
		(unsigned long long)sector);
retry:
	seq = read_seqcount_begin(&conf->gen_lock);
	sh->generation = conf->generation - previous;
	sh->disks = previous ? conf->previous_raid_disks : conf->raid_disks;
	sh->sector = sector;
	stripe_set_idx(sector, conf, previous, sh);
	sh->state = 0;

	for (i = sh->disks; i--; ) {
		struct r5dev *dev = &sh->dev[i];

		if (dev->toread || dev->read || dev->towrite || dev->written ||
		    test_bit(R5_LOCKED, &dev->flags)) {
			printk(KERN_ERR "sector=%llx i=%d %p %p %p %p %d\n",
			       (unsigned long long)sh->sector, i, dev->toread,
			       dev->read, dev->towrite, dev->written,
			       test_bit(R5_LOCKED, &dev->flags));
			BUG();
		}
		dev->flags = 0;
		raid5_build_block(sh, i);
	}
	insert_hash(conf, sh);
}

static struct stripe_head *__find_stripe(raid5_conf_t *conf, sector_t sector, int disks)
{
	struct stripe_head *sh;
	struct hlist_node *hn;

	CHECK_DEVLOCK();
	pr_debug("__find_stripe, sector %llu\n", (unsigned long long)sector);
	hlist_for_each_entry(sh, hn, stripe_hash(conf, sector), hash)
		if (sh->sector == sector && sh->disks == disks)
			WARN_ON(1);
		}
		dev->flags = 0;
		raid5_build_block(sh, i, previous);
	}
	if (read_seqcount_retry(&conf->gen_lock, seq))
		goto retry;
	sh->overwrite_disks = 0;
	insert_hash(conf, sh);
	sh->cpu = smp_processor_id();
	set_bit(STRIPE_BATCH_READY, &sh->state);
}

static struct stripe_head *__find_stripe(struct r5conf *conf, sector_t sector,
					 short generation)
{
	struct stripe_head *sh;

	pr_debug("__find_stripe, sector %llu\n", (unsigned long long)sector);
	hlist_for_each_entry(sh, stripe_hash(conf, sector), hash)
		if (sh->sector == sector && sh->generation == generation)
			return sh;
	pr_debug("__stripe %llu not in cache\n", (unsigned long long)sector);
	return NULL;
}

static void unplug_slaves(mddev_t *mddev);
static void raid5_unplug_device(struct request_queue *q);

static struct stripe_head *get_active_stripe(raid5_conf_t *conf, sector_t sector, int disks,
					     int pd_idx, int noblock)
{
	struct stripe_head *sh;

	pr_debug("get_stripe, sector %llu\n", (unsigned long long)sector);

	spin_lock_irq(&conf->device_lock);

	do {
		wait_event_lock_irq(conf->wait_for_stripe,
				    conf->quiesce == 0,
				    conf->device_lock, /* nothing */);
		sh = __find_stripe(conf, sector, disks);
		if (!sh) {
			if (!conf->inactive_blocked)
				sh = get_free_stripe(conf);
			if (noblock && sh == NULL)
				break;
			if (!sh) {
				conf->inactive_blocked = 1;
				wait_event_lock_irq(conf->wait_for_stripe,
						    !list_empty(&conf->inactive_list) &&
						    (atomic_read(&conf->active_stripes)
						     < (conf->max_nr_stripes *3/4)
						     || !conf->inactive_blocked),
						    conf->device_lock,
						    raid5_unplug_device(conf->mddev->queue)
					);
				conf->inactive_blocked = 0;
			} else
				init_stripe(sh, sector, pd_idx, disks);
		} else {
			if (atomic_read(&sh->count)) {
			  BUG_ON(!list_empty(&sh->lru));
			} else {
				if (!test_bit(STRIPE_HANDLE, &sh->state))
					atomic_inc(&conf->active_stripes);
				if (list_empty(&sh->lru) &&
				    !test_bit(STRIPE_EXPANDING, &sh->state))
					BUG();
				list_del_init(&sh->lru);
			}
		}
	} while (sh == NULL);

	if (sh)
		atomic_inc(&sh->count);

	spin_unlock_irq(&conf->device_lock);
	return sh;
}

static void
raid5_end_read_request(struct bio *bi, int error);
static void
raid5_end_write_request(struct bio *bi, int error);

static void ops_run_io(struct stripe_head *sh, struct stripe_head_state *s)
{
	raid5_conf_t *conf = sh->raid_conf;
	int i, disks = sh->disks;

	might_sleep();

	for (i = disks; i--; ) {
		int rw;
		struct bio *bi;
		mdk_rdev_t *rdev;
		if (test_and_clear_bit(R5_Wantwrite, &sh->dev[i].flags))
			rw = WRITE;
		else if (test_and_clear_bit(R5_Wantread, &sh->dev[i].flags))
			rw = READ;
		else
			continue;

		bi = &sh->dev[i].req;

		bi->bi_rw = rw;
		if (rw == WRITE)
			bi->bi_end_io = raid5_end_write_request;
		else
			bi->bi_end_io = raid5_end_read_request;

		rcu_read_lock();
		rdev = rcu_dereference(conf->disks[i].rdev);
/*
 * Need to check if array has failed when deciding whether to:
 *  - start an array
 *  - remove non-faulty devices
 *  - add a spare
 *  - allow a reshape
 * This determination is simple when no reshape is happening.
 * However if there is a reshape, we need to carefully check
 * both the before and after sections.
 * This is because some failed devices may only affect one
 * of the two sections, and some non-in_sync devices may
 * be insync in the section most affected by failed devices.
 */
static int calc_degraded(struct r5conf *conf)
{
	int degraded, degraded2;
	int i;

	rcu_read_lock();
	degraded = 0;
	for (i = 0; i < conf->previous_raid_disks; i++) {
		struct md_rdev *rdev = rcu_dereference(conf->disks[i].rdev);
		if (rdev && test_bit(Faulty, &rdev->flags))
			rdev = rcu_dereference(conf->disks[i].replacement);
		if (!rdev || test_bit(Faulty, &rdev->flags))
			degraded++;
		else if (test_bit(In_sync, &rdev->flags))
			;
		else
			/* not in-sync or faulty.
			 * If the reshape increases the number of devices,
			 * this is being recovered by the reshape, so
			 * this 'previous' section is not in_sync.
			 * If the number of devices is being reduced however,
			 * the device can only be part of the array if
			 * we are reverting a reshape, so this section will
			 * be in-sync.
			 */
			if (conf->raid_disks >= conf->previous_raid_disks)
				degraded++;
	}
	rcu_read_unlock();
	if (conf->raid_disks == conf->previous_raid_disks)
		return degraded;
	rcu_read_lock();
	degraded2 = 0;
	for (i = 0; i < conf->raid_disks; i++) {
		struct md_rdev *rdev = rcu_dereference(conf->disks[i].rdev);
		if (rdev && test_bit(Faulty, &rdev->flags))
			rdev = rcu_dereference(conf->disks[i].replacement);
		if (!rdev || test_bit(Faulty, &rdev->flags))
			degraded2++;
		else if (test_bit(In_sync, &rdev->flags))
			;
		else
			/* not in-sync or faulty.
			 * If reshape increases the number of devices, this
			 * section has already been recovered, else it
			 * almost certainly hasn't.
			 */
			if (conf->raid_disks <= conf->previous_raid_disks)
				degraded2++;
	}
	rcu_read_unlock();
	if (degraded2 > degraded)
		return degraded2;
	return degraded;
}

static int has_failed(struct r5conf *conf)
{
	int degraded;

	if (conf->mddev->reshape_position == MaxSector)
		return conf->mddev->degraded > conf->max_degraded;

	degraded = calc_degraded(conf);
	if (degraded > conf->max_degraded)
		return 1;
	return 0;
}

struct stripe_head *
raid5_get_active_stripe(struct r5conf *conf, sector_t sector,
			int previous, int noblock, int noquiesce)
{
	struct stripe_head *sh;
	int hash = stripe_hash_locks_hash(sector);

	pr_debug("get_stripe, sector %llu\n", (unsigned long long)sector);

	spin_lock_irq(conf->hash_locks + hash);

	do {
		wait_event_lock_irq(conf->wait_for_quiescent,
				    conf->quiesce == 0 || noquiesce,
				    *(conf->hash_locks + hash));
		sh = __find_stripe(conf, sector, conf->generation - previous);
		if (!sh) {
			if (!test_bit(R5_INACTIVE_BLOCKED, &conf->cache_state)) {
				sh = get_free_stripe(conf, hash);
				if (!sh && !test_bit(R5_DID_ALLOC,
						     &conf->cache_state))
					set_bit(R5_ALLOC_MORE,
						&conf->cache_state);
			}
			if (noblock && sh == NULL)
				break;
			if (!sh) {
				set_bit(R5_INACTIVE_BLOCKED,
					&conf->cache_state);
				wait_event_lock_irq(
					conf->wait_for_stripe,
					!list_empty(conf->inactive_list + hash) &&
					(atomic_read(&conf->active_stripes)
					 < (conf->max_nr_stripes * 3 / 4)
					 || !test_bit(R5_INACTIVE_BLOCKED,
						      &conf->cache_state)),
					*(conf->hash_locks + hash));
				clear_bit(R5_INACTIVE_BLOCKED,
					  &conf->cache_state);
			} else {
				init_stripe(sh, sector, previous);
				atomic_inc(&sh->count);
			}
		} else if (!atomic_inc_not_zero(&sh->count)) {
			spin_lock(&conf->device_lock);
			if (!atomic_read(&sh->count)) {
				if (!test_bit(STRIPE_HANDLE, &sh->state))
					atomic_inc(&conf->active_stripes);
				BUG_ON(list_empty(&sh->lru) &&
				       !test_bit(STRIPE_EXPANDING, &sh->state));
				list_del_init(&sh->lru);
				if (sh->group) {
					sh->group->stripes_cnt--;
					sh->group = NULL;
				}
			}
			atomic_inc(&sh->count);
			spin_unlock(&conf->device_lock);
		}
	} while (sh == NULL);

	spin_unlock_irq(conf->hash_locks + hash);
	return sh;
}

static bool is_full_stripe_write(struct stripe_head *sh)
{
	BUG_ON(sh->overwrite_disks > (sh->disks - sh->raid_conf->max_degraded));
	return sh->overwrite_disks == (sh->disks - sh->raid_conf->max_degraded);
}

static void lock_two_stripes(struct stripe_head *sh1, struct stripe_head *sh2)
{
	if (sh1 > sh2) {
		spin_lock_irq(&sh2->stripe_lock);
		spin_lock_nested(&sh1->stripe_lock, 1);
	} else {
		spin_lock_irq(&sh1->stripe_lock);
		spin_lock_nested(&sh2->stripe_lock, 1);
	}
}

static void unlock_two_stripes(struct stripe_head *sh1, struct stripe_head *sh2)
{
	spin_unlock(&sh1->stripe_lock);
	spin_unlock_irq(&sh2->stripe_lock);
}

/* Only freshly new full stripe normal write stripe can be added to a batch list */
static bool stripe_can_batch(struct stripe_head *sh)
{
	struct r5conf *conf = sh->raid_conf;

	if (conf->log)
		return false;
	return test_bit(STRIPE_BATCH_READY, &sh->state) &&
		!test_bit(STRIPE_BITMAP_PENDING, &sh->state) &&
		is_full_stripe_write(sh);
}

/* we only do back search */
static void stripe_add_to_batch_list(struct r5conf *conf, struct stripe_head *sh)
{
	struct stripe_head *head;
	sector_t head_sector, tmp_sec;
	int hash;
	int dd_idx;

	if (!stripe_can_batch(sh))
		return;
	/* Don't cross chunks, so stripe pd_idx/qd_idx is the same */
	tmp_sec = sh->sector;
	if (!sector_div(tmp_sec, conf->chunk_sectors))
		return;
	head_sector = sh->sector - STRIPE_SECTORS;

	hash = stripe_hash_locks_hash(head_sector);
	spin_lock_irq(conf->hash_locks + hash);
	head = __find_stripe(conf, head_sector, conf->generation);
	if (head && !atomic_inc_not_zero(&head->count)) {
		spin_lock(&conf->device_lock);
		if (!atomic_read(&head->count)) {
			if (!test_bit(STRIPE_HANDLE, &head->state))
				atomic_inc(&conf->active_stripes);
			BUG_ON(list_empty(&head->lru) &&
			       !test_bit(STRIPE_EXPANDING, &head->state));
			list_del_init(&head->lru);
			if (head->group) {
				head->group->stripes_cnt--;
				head->group = NULL;
			}
		}
		atomic_inc(&head->count);
		spin_unlock(&conf->device_lock);
	}
	spin_unlock_irq(conf->hash_locks + hash);

	if (!head)
		return;
	if (!stripe_can_batch(head))
		goto out;

	lock_two_stripes(head, sh);
	/* clear_batch_ready clear the flag */
	if (!stripe_can_batch(head) || !stripe_can_batch(sh))
		goto unlock_out;

	if (sh->batch_head)
		goto unlock_out;

	dd_idx = 0;
	while (dd_idx == sh->pd_idx || dd_idx == sh->qd_idx)
		dd_idx++;
	if (head->dev[dd_idx].towrite->bi_rw != sh->dev[dd_idx].towrite->bi_rw)
		goto unlock_out;

	if (head->batch_head) {
		spin_lock(&head->batch_head->batch_lock);
		/* This batch list is already running */
		if (!stripe_can_batch(head)) {
			spin_unlock(&head->batch_head->batch_lock);
			goto unlock_out;
		}
		/*
		 * We must assign batch_head of this stripe within the
		 * batch_lock, otherwise clear_batch_ready of batch head
		 * stripe could clear BATCH_READY bit of this stripe and
		 * this stripe->batch_head doesn't get assigned, which
		 * could confuse clear_batch_ready for this stripe
		 */
		sh->batch_head = head->batch_head;

		/*
		 * at this point, head's BATCH_READY could be cleared, but we
		 * can still add the stripe to batch list
		 */
		list_add(&sh->batch_list, &head->batch_list);
		spin_unlock(&head->batch_head->batch_lock);
	} else {
		head->batch_head = head;
		sh->batch_head = head->batch_head;
		spin_lock(&head->batch_lock);
		list_add_tail(&sh->batch_list, &head->batch_list);
		spin_unlock(&head->batch_lock);
	}

	if (test_and_clear_bit(STRIPE_PREREAD_ACTIVE, &sh->state))
		if (atomic_dec_return(&conf->preread_active_stripes)
		    < IO_THRESHOLD)
			md_wakeup_thread(conf->mddev->thread);

	if (test_and_clear_bit(STRIPE_BIT_DELAY, &sh->state)) {
		int seq = sh->bm_seq;
		if (test_bit(STRIPE_BIT_DELAY, &sh->batch_head->state) &&
		    sh->batch_head->bm_seq > seq)
			seq = sh->batch_head->bm_seq;
		set_bit(STRIPE_BIT_DELAY, &sh->batch_head->state);
		sh->batch_head->bm_seq = seq;
	}

	atomic_inc(&sh->count);
unlock_out:
	unlock_two_stripes(head, sh);
out:
	raid5_release_stripe(head);
}

/* Determine if 'data_offset' or 'new_data_offset' should be used
 * in this stripe_head.
 */
static int use_new_offset(struct r5conf *conf, struct stripe_head *sh)
{
	sector_t progress = conf->reshape_progress;
	/* Need a memory barrier to make sure we see the value
	 * of conf->generation, or ->data_offset that was set before
	 * reshape_progress was updated.
	 */
	smp_rmb();
	if (progress == MaxSector)
		return 0;
	if (sh->generation == conf->generation - 1)
		return 0;
	/* We are in a reshape, and this is a new-generation stripe,
	 * so use new_data_offset.
	 */
	return 1;
}

static void
raid5_end_read_request(struct bio *bi);
static void
raid5_end_write_request(struct bio *bi);

static void ops_run_io(struct stripe_head *sh, struct stripe_head_state *s)
{
	struct r5conf *conf = sh->raid_conf;
	int i, disks = sh->disks;
	struct stripe_head *head_sh = sh;

	might_sleep();

	if (r5l_write_stripe(conf->log, sh) == 0)
		return;
	for (i = disks; i--; ) {
		int rw;
		int replace_only = 0;
		struct bio *bi, *rbi;
		struct md_rdev *rdev, *rrdev = NULL;

		sh = head_sh;
		if (test_and_clear_bit(R5_Wantwrite, &sh->dev[i].flags)) {
			if (test_and_clear_bit(R5_WantFUA, &sh->dev[i].flags))
				rw = WRITE_FUA;
			else
				rw = WRITE;
			if (test_bit(R5_Discard, &sh->dev[i].flags))
				rw |= REQ_DISCARD;
		} else if (test_and_clear_bit(R5_Wantread, &sh->dev[i].flags))
			rw = READ;
		else if (test_and_clear_bit(R5_WantReplace,
					    &sh->dev[i].flags)) {
			rw = WRITE;
			replace_only = 1;
		} else
			continue;
		if (test_and_clear_bit(R5_SyncIO, &sh->dev[i].flags))
			rw |= REQ_SYNC;

again:
		bi = &sh->dev[i].req;
		rbi = &sh->dev[i].rreq; /* For writing to replacement */

		rcu_read_lock();
		rrdev = rcu_dereference(conf->disks[i].replacement);
		smp_mb(); /* Ensure that if rrdev is NULL, rdev won't be */
		rdev = rcu_dereference(conf->disks[i].rdev);
		if (!rdev) {
			rdev = rrdev;
			rrdev = NULL;
		}
		if (rw & WRITE) {
			if (replace_only)
				rdev = NULL;
			if (rdev == rrdev)
				/* We raced and saw duplicates */
				rrdev = NULL;
		} else {
			if (test_bit(R5_ReadRepl, &head_sh->dev[i].flags) && rrdev)
				rdev = rrdev;
			rrdev = NULL;
		}

		if (rdev && test_bit(Faulty, &rdev->flags))
			rdev = NULL;
		if (rdev)
			atomic_inc(&rdev->nr_pending);
		rcu_read_unlock();

		if (rdev) {
			if (s->syncing || s->expanding || s->expanded)
		if (rrdev && test_bit(Faulty, &rrdev->flags))
			rrdev = NULL;
		if (rrdev)
			atomic_inc(&rrdev->nr_pending);
		rcu_read_unlock();

		/* We have already checked bad blocks for reads.  Now
		 * need to check for writes.  We never accept write errors
		 * on the replacement, so we don't to check rrdev.
		 */
		while ((rw & WRITE) && rdev &&
		       test_bit(WriteErrorSeen, &rdev->flags)) {
			sector_t first_bad;
			int bad_sectors;
			int bad = is_badblock(rdev, sh->sector, STRIPE_SECTORS,
					      &first_bad, &bad_sectors);
			if (!bad)
				break;

			if (bad < 0) {
				set_bit(BlockedBadBlocks, &rdev->flags);
				if (!conf->mddev->external &&
				    conf->mddev->flags) {
					/* It is very unlikely, but we might
					 * still need to write out the
					 * bad block log - better give it
					 * a chance*/
					md_check_recovery(conf->mddev);
				}
				/*
				 * Because md_wait_for_blocked_rdev
				 * will dec nr_pending, we must
				 * increment it first.
				 */
				atomic_inc(&rdev->nr_pending);
				md_wait_for_blocked_rdev(rdev, conf->mddev);
			} else {
				/* Acknowledged bad block - skip the write */
				rdev_dec_pending(rdev, conf->mddev);
				rdev = NULL;
			}
		}

		if (rdev) {
			if (s->syncing || s->expanding || s->expanded
			    || s->replacing)
				md_sync_acct(rdev->bdev, STRIPE_SECTORS);

			set_bit(STRIPE_IO_STARTED, &sh->state);

			bi->bi_bdev = rdev->bdev;
			bio_reset(bi);
			bi->bi_bdev = rdev->bdev;
			bi->bi_rw = rw;
			bi->bi_end_io = (rw & WRITE)
				? raid5_end_write_request
				: raid5_end_read_request;
			bi->bi_private = sh;

			pr_debug("%s: for %llu schedule op %ld on disc %d\n",
				__func__, (unsigned long long)sh->sector,
				bi->bi_rw, i);
			atomic_inc(&sh->count);
			bi->bi_sector = sh->sector + rdev->data_offset;
			bi->bi_flags = 1 << BIO_UPTODATE;
			bi->bi_vcnt = 1;
			bi->bi_max_vecs = 1;
			bi->bi_idx = 0;
			bi->bi_io_vec = &sh->dev[i].vec;
			bi->bi_io_vec[0].bv_len = STRIPE_SIZE;
			bi->bi_io_vec[0].bv_offset = 0;
			bi->bi_size = STRIPE_SIZE;
			bi->bi_next = NULL;
			if (rw == WRITE &&
			    test_bit(R5_ReWrite, &sh->dev[i].flags))
				atomic_add(STRIPE_SECTORS,
					&rdev->corrected_errors);
			generic_make_request(bi);
		} else {
			if (rw == WRITE)
			if (sh != head_sh)
				atomic_inc(&head_sh->count);
			if (use_new_offset(conf, sh))
				bi->bi_iter.bi_sector = (sh->sector
						 + rdev->new_data_offset);
			else
				bi->bi_iter.bi_sector = (sh->sector
						 + rdev->data_offset);
			if (test_bit(R5_ReadNoMerge, &head_sh->dev[i].flags))
				bi->bi_rw |= REQ_NOMERGE;

			if (test_bit(R5_SkipCopy, &sh->dev[i].flags))
				WARN_ON(test_bit(R5_UPTODATE, &sh->dev[i].flags));
			sh->dev[i].vec.bv_page = sh->dev[i].page;
			bi->bi_vcnt = 1;
			bi->bi_io_vec[0].bv_len = STRIPE_SIZE;
			bi->bi_io_vec[0].bv_offset = 0;
			bi->bi_iter.bi_size = STRIPE_SIZE;
			/*
			 * If this is discard request, set bi_vcnt 0. We don't
			 * want to confuse SCSI because SCSI will replace payload
			 */
			if (rw & REQ_DISCARD)
				bi->bi_vcnt = 0;
			if (rrdev)
				set_bit(R5_DOUBLE_LOCKED, &sh->dev[i].flags);

			if (conf->mddev->gendisk)
				trace_block_bio_remap(bdev_get_queue(bi->bi_bdev),
						      bi, disk_devt(conf->mddev->gendisk),
						      sh->dev[i].sector);
			generic_make_request(bi);
		}
		if (rrdev) {
			if (s->syncing || s->expanding || s->expanded
			    || s->replacing)
				md_sync_acct(rrdev->bdev, STRIPE_SECTORS);

			set_bit(STRIPE_IO_STARTED, &sh->state);

			bio_reset(rbi);
			rbi->bi_bdev = rrdev->bdev;
			rbi->bi_rw = rw;
			BUG_ON(!(rw & WRITE));
			rbi->bi_end_io = raid5_end_write_request;
			rbi->bi_private = sh;

			pr_debug("%s: for %llu schedule op %ld on "
				 "replacement disc %d\n",
				__func__, (unsigned long long)sh->sector,
				rbi->bi_rw, i);
			atomic_inc(&sh->count);
			if (sh != head_sh)
				atomic_inc(&head_sh->count);
			if (use_new_offset(conf, sh))
				rbi->bi_iter.bi_sector = (sh->sector
						  + rrdev->new_data_offset);
			else
				rbi->bi_iter.bi_sector = (sh->sector
						  + rrdev->data_offset);
			if (test_bit(R5_SkipCopy, &sh->dev[i].flags))
				WARN_ON(test_bit(R5_UPTODATE, &sh->dev[i].flags));
			sh->dev[i].rvec.bv_page = sh->dev[i].page;
			rbi->bi_vcnt = 1;
			rbi->bi_io_vec[0].bv_len = STRIPE_SIZE;
			rbi->bi_io_vec[0].bv_offset = 0;
			rbi->bi_iter.bi_size = STRIPE_SIZE;
			/*
			 * If this is discard request, set bi_vcnt 0. We don't
			 * want to confuse SCSI because SCSI will replace payload
			 */
			if (rw & REQ_DISCARD)
				rbi->bi_vcnt = 0;
			if (conf->mddev->gendisk)
				trace_block_bio_remap(bdev_get_queue(rbi->bi_bdev),
						      rbi, disk_devt(conf->mddev->gendisk),
						      sh->dev[i].sector);
			generic_make_request(rbi);
		}
		if (!rdev && !rrdev) {
			if (rw & WRITE)
				set_bit(STRIPE_DEGRADED, &sh->state);
			pr_debug("skip op %ld on disc %d for sector %llu\n",
				bi->bi_rw, i, (unsigned long long)sh->sector);
			clear_bit(R5_LOCKED, &sh->dev[i].flags);
			set_bit(STRIPE_HANDLE, &sh->state);
		}

		if (!head_sh->batch_head)
			continue;
		sh = list_first_entry(&sh->batch_list, struct stripe_head,
				      batch_list);
		if (sh != head_sh)
			goto again;
	}
}

static struct dma_async_tx_descriptor *
async_copy_data(int frombio, struct bio *bio, struct page *page,
	sector_t sector, struct dma_async_tx_descriptor *tx)
{
	struct bio_vec *bvl;
	struct page *bio_page;
	int i;
	int page_offset;

	if (bio->bi_sector >= sector)
		page_offset = (signed)(bio->bi_sector - sector) * 512;
	else
		page_offset = (signed)(sector - bio->bi_sector) * -512;
	bio_for_each_segment(bvl, bio, i) {
		int len = bio_iovec_idx(bio, i)->bv_len;
async_copy_data(int frombio, struct bio *bio, struct page **page,
	sector_t sector, struct dma_async_tx_descriptor *tx,
	struct stripe_head *sh)
{
	struct bio_vec bvl;
	struct bvec_iter iter;
	struct page *bio_page;
	int page_offset;
	struct async_submit_ctl submit;
	enum async_tx_flags flags = 0;

	if (bio->bi_iter.bi_sector >= sector)
		page_offset = (signed)(bio->bi_iter.bi_sector - sector) * 512;
	else
		page_offset = (signed)(sector - bio->bi_iter.bi_sector) * -512;

	if (frombio)
		flags |= ASYNC_TX_FENCE;
	init_async_submit(&submit, flags, tx, NULL, NULL, NULL);

	bio_for_each_segment(bvl, bio, iter) {
		int len = bvl.bv_len;
		int clen;
		int b_offset = 0;

		if (page_offset < 0) {
			b_offset = -page_offset;
			page_offset += b_offset;
			len -= b_offset;
		}

		if (len > 0 && page_offset + len > STRIPE_SIZE)
			clen = STRIPE_SIZE - page_offset;
		else
			clen = len;

		if (clen > 0) {
			b_offset += bio_iovec_idx(bio, i)->bv_offset;
			bio_page = bio_iovec_idx(bio, i)->bv_page;
			if (frombio)
				tx = async_memcpy(page, bio_page, page_offset,
					b_offset, clen,
					ASYNC_TX_DEP_ACK,
					tx, NULL, NULL);
			else
				tx = async_memcpy(bio_page, page, b_offset,
					page_offset, clen,
					ASYNC_TX_DEP_ACK,
					tx, NULL, NULL);
		}
			b_offset += bvl.bv_offset;
			bio_page = bvl.bv_page;
			if (frombio) {
				if (sh->raid_conf->skip_copy &&
				    b_offset == 0 && page_offset == 0 &&
				    clen == STRIPE_SIZE)
					*page = bio_page;
				else
					tx = async_memcpy(*page, bio_page, page_offset,
						  b_offset, clen, &submit);
			} else
				tx = async_memcpy(bio_page, *page, b_offset,
						  page_offset, clen, &submit);
		}
		/* chain the operations */
		submit.depend_tx = tx;

		if (clen < len) /* hit end of page */
			break;
		page_offset +=  len;
	}

	return tx;
}

static void ops_complete_biofill(void *stripe_head_ref)
{
	struct stripe_head *sh = stripe_head_ref;
	struct bio *return_bi = NULL;
	raid5_conf_t *conf = sh->raid_conf;
	struct bio_list return_bi = BIO_EMPTY_LIST;
	int i;

	pr_debug("%s: stripe %llu\n", __func__,
		(unsigned long long)sh->sector);

	/* clear completed biofills */
	spin_lock_irq(&conf->device_lock);
	for (i = sh->disks; i--; ) {
		struct r5dev *dev = &sh->dev[i];

		/* acknowledge completion of a biofill operation */
		/* and check if we need to reply to a read request,
		 * new R5_Wantfill requests are held off until
		 * !STRIPE_BIOFILL_RUN
		 */
		if (test_and_clear_bit(R5_Wantfill, &dev->flags)) {
			struct bio *rbi, *rbi2;

			BUG_ON(!dev->read);
			rbi = dev->read;
			dev->read = NULL;
			while (rbi && rbi->bi_sector <
				dev->sector + STRIPE_SECTORS) {
				rbi2 = r5_next_bio(rbi, dev->sector);
				if (--rbi->bi_phys_segments == 0) {
					rbi->bi_next = return_bi;
					return_bi = rbi;
				}
			while (rbi && rbi->bi_iter.bi_sector <
				dev->sector + STRIPE_SECTORS) {
				rbi2 = r5_next_bio(rbi, dev->sector);
				if (!raid5_dec_bi_active_stripes(rbi))
					bio_list_add(&return_bi, rbi);
				rbi = rbi2;
			}
		}
	}
	spin_unlock_irq(&conf->device_lock);
	clear_bit(STRIPE_BIOFILL_RUN, &sh->state);

	return_io(return_bi);

	set_bit(STRIPE_HANDLE, &sh->state);
	release_stripe(sh);
	clear_bit(STRIPE_BIOFILL_RUN, &sh->state);

	return_io(&return_bi);

	set_bit(STRIPE_HANDLE, &sh->state);
	raid5_release_stripe(sh);
}

static void ops_run_biofill(struct stripe_head *sh)
{
	struct dma_async_tx_descriptor *tx = NULL;
	raid5_conf_t *conf = sh->raid_conf;
	int i;

	struct async_submit_ctl submit;
	int i;

	BUG_ON(sh->batch_head);
	pr_debug("%s: stripe %llu\n", __func__,
		(unsigned long long)sh->sector);

	for (i = sh->disks; i--; ) {
		struct r5dev *dev = &sh->dev[i];
		if (test_bit(R5_Wantfill, &dev->flags)) {
			struct bio *rbi;
			spin_lock_irq(&conf->device_lock);
			dev->read = rbi = dev->toread;
			dev->toread = NULL;
			spin_unlock_irq(&conf->device_lock);
			while (rbi && rbi->bi_sector <
				dev->sector + STRIPE_SECTORS) {
				tx = async_copy_data(0, rbi, dev->page,
					dev->sector, tx);
			spin_lock_irq(&sh->stripe_lock);
			dev->read = rbi = dev->toread;
			dev->toread = NULL;
			spin_unlock_irq(&sh->stripe_lock);
			while (rbi && rbi->bi_iter.bi_sector <
				dev->sector + STRIPE_SECTORS) {
				tx = async_copy_data(0, rbi, &dev->page,
					dev->sector, tx, sh);
				rbi = r5_next_bio(rbi, dev->sector);
			}
		}
	}

	atomic_inc(&sh->count);
	async_trigger_callback(ASYNC_TX_DEP_ACK | ASYNC_TX_ACK, tx,
		ops_complete_biofill, sh);
}

static void ops_complete_compute5(void *stripe_head_ref)
{
	struct stripe_head *sh = stripe_head_ref;
	int target = sh->ops.target;
	struct r5dev *tgt = &sh->dev[target];
	init_async_submit(&submit, ASYNC_TX_ACK, tx, ops_complete_biofill, sh, NULL);
	async_trigger_callback(&submit);
}

static void mark_target_uptodate(struct stripe_head *sh, int target)
{
	struct r5dev *tgt;

	if (target < 0)
		return;

	tgt = &sh->dev[target];
	set_bit(R5_UPTODATE, &tgt->flags);
	BUG_ON(!test_bit(R5_Wantcompute, &tgt->flags));
	clear_bit(R5_Wantcompute, &tgt->flags);
}

static void ops_complete_compute(void *stripe_head_ref)
{
	struct stripe_head *sh = stripe_head_ref;

	pr_debug("%s: stripe %llu\n", __func__,
		(unsigned long long)sh->sector);

	set_bit(R5_UPTODATE, &tgt->flags);
	BUG_ON(!test_bit(R5_Wantcompute, &tgt->flags));
	clear_bit(R5_Wantcompute, &tgt->flags);
	/* mark the computed target(s) as uptodate */
	mark_target_uptodate(sh, sh->ops.target);
	mark_target_uptodate(sh, sh->ops.target2);

	clear_bit(STRIPE_COMPUTE_RUN, &sh->state);
	if (sh->check_state == check_state_compute_run)
		sh->check_state = check_state_compute_result;
	set_bit(STRIPE_HANDLE, &sh->state);
	release_stripe(sh);
}

static struct dma_async_tx_descriptor *ops_run_compute5(struct stripe_head *sh)
{
	/* kernel stack size limits the total number of disks */
	int disks = sh->disks;
	struct page *xor_srcs[disks];
	raid5_release_stripe(sh);
}

/* return a pointer to the address conversion region of the scribble buffer */
static addr_conv_t *to_addr_conv(struct stripe_head *sh,
				 struct raid5_percpu *percpu, int i)
{
	void *addr;

	addr = flex_array_get(percpu->scribble, i);
	return addr + sizeof(struct page *) * (sh->disks + 2);
}

/* return a pointer to the address conversion region of the scribble buffer */
static struct page **to_addr_page(struct raid5_percpu *percpu, int i)
{
	void *addr;

	addr = flex_array_get(percpu->scribble, i);
	return addr;
}

static struct dma_async_tx_descriptor *
ops_run_compute5(struct stripe_head *sh, struct raid5_percpu *percpu)
{
	int disks = sh->disks;
	struct page **xor_srcs = to_addr_page(percpu, 0);
	int target = sh->ops.target;
	struct r5dev *tgt = &sh->dev[target];
	struct page *xor_dest = tgt->page;
	int count = 0;
	struct dma_async_tx_descriptor *tx;
	int i;

	struct async_submit_ctl submit;
	int i;

	BUG_ON(sh->batch_head);

	pr_debug("%s: stripe %llu block: %d\n",
		__func__, (unsigned long long)sh->sector, target);
	BUG_ON(!test_bit(R5_Wantcompute, &tgt->flags));

	for (i = disks; i--; )
		if (i != target)
			xor_srcs[count++] = sh->dev[i].page;

	atomic_inc(&sh->count);

	if (unlikely(count == 1))
		tx = async_memcpy(xor_dest, xor_srcs[0], 0, 0, STRIPE_SIZE,
			0, NULL, ops_complete_compute5, sh);
	else
		tx = async_xor(xor_dest, xor_srcs, 0, count, STRIPE_SIZE,
			ASYNC_TX_XOR_ZERO_DST, NULL,
			ops_complete_compute5, sh);
	init_async_submit(&submit, ASYNC_TX_FENCE|ASYNC_TX_XOR_ZERO_DST, NULL,
			  ops_complete_compute, sh, to_addr_conv(sh, percpu, 0));
	if (unlikely(count == 1))
		tx = async_memcpy(xor_dest, xor_srcs[0], 0, 0, STRIPE_SIZE, &submit);
	else
		tx = async_xor(xor_dest, xor_srcs, 0, count, STRIPE_SIZE, &submit);

	return tx;
}

/* set_syndrome_sources - populate source buffers for gen_syndrome
 * @srcs - (struct page *) array of size sh->disks
 * @sh - stripe_head to parse
 *
 * Populates srcs in proper layout order for the stripe and returns the
 * 'count' of sources to be used in a call to async_gen_syndrome.  The P
 * destination buffer is recorded in srcs[count] and the Q destination
 * is recorded in srcs[count+1]].
 */
static int set_syndrome_sources(struct page **srcs,
				struct stripe_head *sh,
				int srctype)
{
	int disks = sh->disks;
	int syndrome_disks = sh->ddf_layout ? disks : (disks - 2);
	int d0_idx = raid6_d0(sh);
	int count;
	int i;

	for (i = 0; i < disks; i++)
		srcs[i] = NULL;

	count = 0;
	i = d0_idx;
	do {
		int slot = raid6_idx_to_slot(i, sh, &count, syndrome_disks);
		struct r5dev *dev = &sh->dev[i];

		if (i == sh->qd_idx || i == sh->pd_idx ||
		    (srctype == SYNDROME_SRC_ALL) ||
		    (srctype == SYNDROME_SRC_WANT_DRAIN &&
		     test_bit(R5_Wantdrain, &dev->flags)) ||
		    (srctype == SYNDROME_SRC_WRITTEN &&
		     dev->written))
			srcs[slot] = sh->dev[i].page;
		i = raid6_next_disk(i, disks);
	} while (i != d0_idx);

	return syndrome_disks;
}

static struct dma_async_tx_descriptor *
ops_run_compute6_1(struct stripe_head *sh, struct raid5_percpu *percpu)
{
	int disks = sh->disks;
	struct page **blocks = to_addr_page(percpu, 0);
	int target;
	int qd_idx = sh->qd_idx;
	struct dma_async_tx_descriptor *tx;
	struct async_submit_ctl submit;
	struct r5dev *tgt;
	struct page *dest;
	int i;
	int count;

	BUG_ON(sh->batch_head);
	if (sh->ops.target < 0)
		target = sh->ops.target2;
	else if (sh->ops.target2 < 0)
		target = sh->ops.target;
	else
		/* we should only have one valid target */
		BUG();
	BUG_ON(target < 0);
	pr_debug("%s: stripe %llu block: %d\n",
		__func__, (unsigned long long)sh->sector, target);

	tgt = &sh->dev[target];
	BUG_ON(!test_bit(R5_Wantcompute, &tgt->flags));
	dest = tgt->page;

	atomic_inc(&sh->count);

	if (target == qd_idx) {
		count = set_syndrome_sources(blocks, sh, SYNDROME_SRC_ALL);
		blocks[count] = NULL; /* regenerating p is not necessary */
		BUG_ON(blocks[count+1] != dest); /* q should already be set */
		init_async_submit(&submit, ASYNC_TX_FENCE, NULL,
				  ops_complete_compute, sh,
				  to_addr_conv(sh, percpu, 0));
		tx = async_gen_syndrome(blocks, 0, count+2, STRIPE_SIZE, &submit);
	} else {
		/* Compute any data- or p-drive using XOR */
		count = 0;
		for (i = disks; i-- ; ) {
			if (i == target || i == qd_idx)
				continue;
			blocks[count++] = sh->dev[i].page;
		}

		init_async_submit(&submit, ASYNC_TX_FENCE|ASYNC_TX_XOR_ZERO_DST,
				  NULL, ops_complete_compute, sh,
				  to_addr_conv(sh, percpu, 0));
		tx = async_xor(dest, blocks, 0, count, STRIPE_SIZE, &submit);
	}

	return tx;
}

static struct dma_async_tx_descriptor *
ops_run_compute6_2(struct stripe_head *sh, struct raid5_percpu *percpu)
{
	int i, count, disks = sh->disks;
	int syndrome_disks = sh->ddf_layout ? disks : disks-2;
	int d0_idx = raid6_d0(sh);
	int faila = -1, failb = -1;
	int target = sh->ops.target;
	int target2 = sh->ops.target2;
	struct r5dev *tgt = &sh->dev[target];
	struct r5dev *tgt2 = &sh->dev[target2];
	struct dma_async_tx_descriptor *tx;
	struct page **blocks = to_addr_page(percpu, 0);
	struct async_submit_ctl submit;

	BUG_ON(sh->batch_head);
	pr_debug("%s: stripe %llu block1: %d block2: %d\n",
		 __func__, (unsigned long long)sh->sector, target, target2);
	BUG_ON(target < 0 || target2 < 0);
	BUG_ON(!test_bit(R5_Wantcompute, &tgt->flags));
	BUG_ON(!test_bit(R5_Wantcompute, &tgt2->flags));

	/* we need to open-code set_syndrome_sources to handle the
	 * slot number conversion for 'faila' and 'failb'
	 */
	for (i = 0; i < disks ; i++)
		blocks[i] = NULL;
	count = 0;
	i = d0_idx;
	do {
		int slot = raid6_idx_to_slot(i, sh, &count, syndrome_disks);

		blocks[slot] = sh->dev[i].page;

		if (i == target)
			faila = slot;
		if (i == target2)
			failb = slot;
		i = raid6_next_disk(i, disks);
	} while (i != d0_idx);

	BUG_ON(faila == failb);
	if (failb < faila)
		swap(faila, failb);
	pr_debug("%s: stripe: %llu faila: %d failb: %d\n",
		 __func__, (unsigned long long)sh->sector, faila, failb);

	atomic_inc(&sh->count);

	if (failb == syndrome_disks+1) {
		/* Q disk is one of the missing disks */
		if (faila == syndrome_disks) {
			/* Missing P+Q, just recompute */
			init_async_submit(&submit, ASYNC_TX_FENCE, NULL,
					  ops_complete_compute, sh,
					  to_addr_conv(sh, percpu, 0));
			return async_gen_syndrome(blocks, 0, syndrome_disks+2,
						  STRIPE_SIZE, &submit);
		} else {
			struct page *dest;
			int data_target;
			int qd_idx = sh->qd_idx;

			/* Missing D+Q: recompute D from P, then recompute Q */
			if (target == qd_idx)
				data_target = target2;
			else
				data_target = target;

			count = 0;
			for (i = disks; i-- ; ) {
				if (i == data_target || i == qd_idx)
					continue;
				blocks[count++] = sh->dev[i].page;
			}
			dest = sh->dev[data_target].page;
			init_async_submit(&submit,
					  ASYNC_TX_FENCE|ASYNC_TX_XOR_ZERO_DST,
					  NULL, NULL, NULL,
					  to_addr_conv(sh, percpu, 0));
			tx = async_xor(dest, blocks, 0, count, STRIPE_SIZE,
				       &submit);

			count = set_syndrome_sources(blocks, sh, SYNDROME_SRC_ALL);
			init_async_submit(&submit, ASYNC_TX_FENCE, tx,
					  ops_complete_compute, sh,
					  to_addr_conv(sh, percpu, 0));
			return async_gen_syndrome(blocks, 0, count+2,
						  STRIPE_SIZE, &submit);
		}
	} else {
		init_async_submit(&submit, ASYNC_TX_FENCE, NULL,
				  ops_complete_compute, sh,
				  to_addr_conv(sh, percpu, 0));
		if (failb == syndrome_disks) {
			/* We're missing D+P. */
			return async_raid6_datap_recov(syndrome_disks+2,
						       STRIPE_SIZE, faila,
						       blocks, &submit);
		} else {
			/* We're missing D+D. */
			return async_raid6_2data_recov(syndrome_disks+2,
						       STRIPE_SIZE, faila, failb,
						       blocks, &submit);
		}
	}
}

static void ops_complete_prexor(void *stripe_head_ref)
{
	struct stripe_head *sh = stripe_head_ref;

	pr_debug("%s: stripe %llu\n", __func__,
		(unsigned long long)sh->sector);
}

static struct dma_async_tx_descriptor *
ops_run_prexor(struct stripe_head *sh, struct dma_async_tx_descriptor *tx)
{
	/* kernel stack size limits the total number of disks */
	int disks = sh->disks;
	struct page *xor_srcs[disks];
	int count = 0, pd_idx = sh->pd_idx, i;
ops_run_prexor5(struct stripe_head *sh, struct raid5_percpu *percpu,
		struct dma_async_tx_descriptor *tx)
{
	int disks = sh->disks;
	struct page **xor_srcs = to_addr_page(percpu, 0);
	int count = 0, pd_idx = sh->pd_idx, i;
	struct async_submit_ctl submit;

	/* existing parity data subtracted */
	struct page *xor_dest = xor_srcs[count++] = sh->dev[pd_idx].page;

	BUG_ON(sh->batch_head);
	pr_debug("%s: stripe %llu\n", __func__,
		(unsigned long long)sh->sector);

	for (i = disks; i--; ) {
		struct r5dev *dev = &sh->dev[i];
		/* Only process blocks that are known to be uptodate */
		if (test_bit(R5_Wantdrain, &dev->flags))
			xor_srcs[count++] = dev->page;
	}

	tx = async_xor(xor_dest, xor_srcs, 0, count, STRIPE_SIZE,
		ASYNC_TX_DEP_ACK | ASYNC_TX_XOR_DROP_DST, tx,
		ops_complete_prexor, sh);
	init_async_submit(&submit, ASYNC_TX_FENCE|ASYNC_TX_XOR_DROP_DST, tx,
			  ops_complete_prexor, sh, to_addr_conv(sh, percpu, 0));
	tx = async_xor(xor_dest, xor_srcs, 0, count, STRIPE_SIZE, &submit);

	return tx;
}

static struct dma_async_tx_descriptor *
ops_run_prexor6(struct stripe_head *sh, struct raid5_percpu *percpu,
		struct dma_async_tx_descriptor *tx)
{
	struct page **blocks = to_addr_page(percpu, 0);
	int count;
	struct async_submit_ctl submit;

	pr_debug("%s: stripe %llu\n", __func__,
		(unsigned long long)sh->sector);

	count = set_syndrome_sources(blocks, sh, SYNDROME_SRC_WANT_DRAIN);

	init_async_submit(&submit, ASYNC_TX_FENCE|ASYNC_TX_PQ_XOR_DST, tx,
			  ops_complete_prexor, sh, to_addr_conv(sh, percpu, 0));
	tx = async_gen_syndrome(blocks, 0, count+2, STRIPE_SIZE,  &submit);

	return tx;
}

static struct dma_async_tx_descriptor *
ops_run_biodrain(struct stripe_head *sh, struct dma_async_tx_descriptor *tx)
{
	int disks = sh->disks;
	int i;
	struct stripe_head *head_sh = sh;

	pr_debug("%s: stripe %llu\n", __func__,
		(unsigned long long)sh->sector);

	for (i = disks; i--; ) {
		struct r5dev *dev = &sh->dev[i];
		struct bio *chosen;

		if (test_and_clear_bit(R5_Wantdrain, &dev->flags)) {
			struct bio *wbi;

			spin_lock(&sh->lock);
			chosen = dev->towrite;
			dev->towrite = NULL;
			BUG_ON(dev->written);
			wbi = dev->written = chosen;
			spin_unlock(&sh->lock);

			while (wbi && wbi->bi_sector <
				dev->sector + STRIPE_SECTORS) {
				tx = async_copy_data(1, wbi, dev->page,
					dev->sector, tx);
				wbi = r5_next_bio(wbi, dev->sector);
			}
		struct r5dev *dev;
		struct bio *chosen;

		sh = head_sh;
		if (test_and_clear_bit(R5_Wantdrain, &head_sh->dev[i].flags)) {
			struct bio *wbi;

again:
			dev = &sh->dev[i];
			spin_lock_irq(&sh->stripe_lock);
			chosen = dev->towrite;
			dev->towrite = NULL;
			sh->overwrite_disks = 0;
			BUG_ON(dev->written);
			wbi = dev->written = chosen;
			spin_unlock_irq(&sh->stripe_lock);
			WARN_ON(dev->page != dev->orig_page);

			while (wbi && wbi->bi_iter.bi_sector <
				dev->sector + STRIPE_SECTORS) {
				if (wbi->bi_rw & REQ_FUA)
					set_bit(R5_WantFUA, &dev->flags);
				if (wbi->bi_rw & REQ_SYNC)
					set_bit(R5_SyncIO, &dev->flags);
				if (wbi->bi_rw & REQ_DISCARD)
					set_bit(R5_Discard, &dev->flags);
				else {
					tx = async_copy_data(1, wbi, &dev->page,
						dev->sector, tx, sh);
					if (dev->page != dev->orig_page) {
						set_bit(R5_SkipCopy, &dev->flags);
						clear_bit(R5_UPTODATE, &dev->flags);
						clear_bit(R5_OVERWRITE, &dev->flags);
					}
				}
				wbi = r5_next_bio(wbi, dev->sector);
			}

			if (head_sh->batch_head) {
				sh = list_first_entry(&sh->batch_list,
						      struct stripe_head,
						      batch_list);
				if (sh == head_sh)
					continue;
				goto again;
			}
		}
	}

	return tx;
}

static void ops_complete_postxor(void *stripe_head_ref)
{
	struct stripe_head *sh = stripe_head_ref;
	int disks = sh->disks, i, pd_idx = sh->pd_idx;
static void ops_complete_reconstruct(void *stripe_head_ref)
{
	struct stripe_head *sh = stripe_head_ref;
	int disks = sh->disks;
	int pd_idx = sh->pd_idx;
	int qd_idx = sh->qd_idx;
	int i;
	bool fua = false, sync = false, discard = false;

	pr_debug("%s: stripe %llu\n", __func__,
		(unsigned long long)sh->sector);

	for (i = disks; i--; ) {
		struct r5dev *dev = &sh->dev[i];
		if (dev->written || i == pd_idx)
			set_bit(R5_UPTODATE, &dev->flags);
		fua |= test_bit(R5_WantFUA, &sh->dev[i].flags);
		sync |= test_bit(R5_SyncIO, &sh->dev[i].flags);
		discard |= test_bit(R5_Discard, &sh->dev[i].flags);
	}

	for (i = disks; i--; ) {
		struct r5dev *dev = &sh->dev[i];

		if (dev->written || i == pd_idx || i == qd_idx) {
			if (!discard && !test_bit(R5_SkipCopy, &dev->flags)) {
				set_bit(R5_UPTODATE, &dev->flags);
				if (test_bit(STRIPE_EXPAND_READY, &sh->state))
					set_bit(R5_Expanded, &dev->flags);
			}
			if (fua)
				set_bit(R5_WantFUA, &dev->flags);
			if (sync)
				set_bit(R5_SyncIO, &dev->flags);
		}
	}

	if (sh->reconstruct_state == reconstruct_state_drain_run)
		sh->reconstruct_state = reconstruct_state_drain_result;
	else if (sh->reconstruct_state == reconstruct_state_prexor_drain_run)
		sh->reconstruct_state = reconstruct_state_prexor_drain_result;
	else {
		BUG_ON(sh->reconstruct_state != reconstruct_state_run);
		sh->reconstruct_state = reconstruct_state_result;
	}

	set_bit(STRIPE_HANDLE, &sh->state);
	release_stripe(sh);
}

static void
ops_run_postxor(struct stripe_head *sh, struct dma_async_tx_descriptor *tx)
{
	/* kernel stack size limits the total number of disks */
	int disks = sh->disks;
	struct page *xor_srcs[disks];

	int count = 0, pd_idx = sh->pd_idx, i;
	struct page *xor_dest;
	int prexor = 0;
	unsigned long flags;
	raid5_release_stripe(sh);
}

static void
ops_run_reconstruct5(struct stripe_head *sh, struct raid5_percpu *percpu,
		     struct dma_async_tx_descriptor *tx)
{
	int disks = sh->disks;
	struct page **xor_srcs;
	struct async_submit_ctl submit;
	int count, pd_idx = sh->pd_idx, i;
	struct page *xor_dest;
	int prexor = 0;
	unsigned long flags;
	int j = 0;
	struct stripe_head *head_sh = sh;
	int last_stripe;

	pr_debug("%s: stripe %llu\n", __func__,
		(unsigned long long)sh->sector);

	/* check if prexor is active which means only process blocks
	 * that are part of a read-modify-write (written)
	 */
	if (sh->reconstruct_state == reconstruct_state_prexor_drain_run) {
	for (i = 0; i < sh->disks; i++) {
		if (pd_idx == i)
			continue;
		if (!test_bit(R5_Discard, &sh->dev[i].flags))
			break;
	}
	if (i >= sh->disks) {
		atomic_inc(&sh->count);
		set_bit(R5_Discard, &sh->dev[pd_idx].flags);
		ops_complete_reconstruct(sh);
		return;
	}
again:
	count = 0;
	xor_srcs = to_addr_page(percpu, j);
	/* check if prexor is active which means only process blocks
	 * that are part of a read-modify-write (written)
	 */
	if (head_sh->reconstruct_state == reconstruct_state_prexor_drain_run) {
		prexor = 1;
		xor_dest = xor_srcs[count++] = sh->dev[pd_idx].page;
		for (i = disks; i--; ) {
			struct r5dev *dev = &sh->dev[i];
			if (dev->written)
			if (head_sh->dev[i].written)
				xor_srcs[count++] = dev->page;
		}
	} else {
		xor_dest = sh->dev[pd_idx].page;
		for (i = disks; i--; ) {
			struct r5dev *dev = &sh->dev[i];
			if (i != pd_idx)
				xor_srcs[count++] = dev->page;
		}
	}

	/* 1/ if we prexor'd then the dest is reused as a source
	 * 2/ if we did not prexor then we are redoing the parity
	 * set ASYNC_TX_XOR_DROP_DST and ASYNC_TX_XOR_ZERO_DST
	 * for the synchronous xor case
	 */
	flags = ASYNC_TX_DEP_ACK | ASYNC_TX_ACK |
		(prexor ? ASYNC_TX_XOR_DROP_DST : ASYNC_TX_XOR_ZERO_DST);

	atomic_inc(&sh->count);

	if (unlikely(count == 1)) {
		flags &= ~(ASYNC_TX_XOR_DROP_DST | ASYNC_TX_XOR_ZERO_DST);
		tx = async_memcpy(xor_dest, xor_srcs[0], 0, 0, STRIPE_SIZE,
			flags, tx, ops_complete_postxor, sh);
	} else
		tx = async_xor(xor_dest, xor_srcs, 0, count, STRIPE_SIZE,
			flags, tx, ops_complete_postxor, sh);
	last_stripe = !head_sh->batch_head ||
		list_first_entry(&sh->batch_list,
				 struct stripe_head, batch_list) == head_sh;
	if (last_stripe) {
		flags = ASYNC_TX_ACK |
			(prexor ? ASYNC_TX_XOR_DROP_DST : ASYNC_TX_XOR_ZERO_DST);

		atomic_inc(&head_sh->count);
		init_async_submit(&submit, flags, tx, ops_complete_reconstruct, head_sh,
				  to_addr_conv(sh, percpu, j));
	} else {
		flags = prexor ? ASYNC_TX_XOR_DROP_DST : ASYNC_TX_XOR_ZERO_DST;
		init_async_submit(&submit, flags, tx, NULL, NULL,
				  to_addr_conv(sh, percpu, j));
	}

	if (unlikely(count == 1))
		tx = async_memcpy(xor_dest, xor_srcs[0], 0, 0, STRIPE_SIZE, &submit);
	else
		tx = async_xor(xor_dest, xor_srcs, 0, count, STRIPE_SIZE, &submit);
	if (!last_stripe) {
		j++;
		sh = list_first_entry(&sh->batch_list, struct stripe_head,
				      batch_list);
		goto again;
	}
}

static void
ops_run_reconstruct6(struct stripe_head *sh, struct raid5_percpu *percpu,
		     struct dma_async_tx_descriptor *tx)
{
	struct async_submit_ctl submit;
	struct page **blocks;
	int count, i, j = 0;
	struct stripe_head *head_sh = sh;
	int last_stripe;
	int synflags;
	unsigned long txflags;

	pr_debug("%s: stripe %llu\n", __func__, (unsigned long long)sh->sector);

	for (i = 0; i < sh->disks; i++) {
		if (sh->pd_idx == i || sh->qd_idx == i)
			continue;
		if (!test_bit(R5_Discard, &sh->dev[i].flags))
			break;
	}
	if (i >= sh->disks) {
		atomic_inc(&sh->count);
		set_bit(R5_Discard, &sh->dev[sh->pd_idx].flags);
		set_bit(R5_Discard, &sh->dev[sh->qd_idx].flags);
		ops_complete_reconstruct(sh);
		return;
	}

again:
	blocks = to_addr_page(percpu, j);

	if (sh->reconstruct_state == reconstruct_state_prexor_drain_run) {
		synflags = SYNDROME_SRC_WRITTEN;
		txflags = ASYNC_TX_ACK | ASYNC_TX_PQ_XOR_DST;
	} else {
		synflags = SYNDROME_SRC_ALL;
		txflags = ASYNC_TX_ACK;
	}

	count = set_syndrome_sources(blocks, sh, synflags);
	last_stripe = !head_sh->batch_head ||
		list_first_entry(&sh->batch_list,
				 struct stripe_head, batch_list) == head_sh;

	if (last_stripe) {
		atomic_inc(&head_sh->count);
		init_async_submit(&submit, txflags, tx, ops_complete_reconstruct,
				  head_sh, to_addr_conv(sh, percpu, j));
	} else
		init_async_submit(&submit, 0, tx, NULL, NULL,
				  to_addr_conv(sh, percpu, j));
	tx = async_gen_syndrome(blocks, 0, count+2, STRIPE_SIZE,  &submit);
	if (!last_stripe) {
		j++;
		sh = list_first_entry(&sh->batch_list, struct stripe_head,
				      batch_list);
		goto again;
	}
}

static void ops_complete_check(void *stripe_head_ref)
{
	struct stripe_head *sh = stripe_head_ref;

	pr_debug("%s: stripe %llu\n", __func__,
		(unsigned long long)sh->sector);

	sh->check_state = check_state_check_result;
	set_bit(STRIPE_HANDLE, &sh->state);
	release_stripe(sh);
}

static void ops_run_check(struct stripe_head *sh)
{
	/* kernel stack size limits the total number of disks */
	int disks = sh->disks;
	struct page *xor_srcs[disks];
	struct dma_async_tx_descriptor *tx;

	int count = 0, pd_idx = sh->pd_idx, i;
	struct page *xor_dest = xor_srcs[count++] = sh->dev[pd_idx].page;
	raid5_release_stripe(sh);
}

static void ops_run_check_p(struct stripe_head *sh, struct raid5_percpu *percpu)
{
	int disks = sh->disks;
	int pd_idx = sh->pd_idx;
	int qd_idx = sh->qd_idx;
	struct page *xor_dest;
	struct page **xor_srcs = to_addr_page(percpu, 0);
	struct dma_async_tx_descriptor *tx;
	struct async_submit_ctl submit;
	int count;
	int i;

	pr_debug("%s: stripe %llu\n", __func__,
		(unsigned long long)sh->sector);

	for (i = disks; i--; ) {
		struct r5dev *dev = &sh->dev[i];
		if (i != pd_idx)
			xor_srcs[count++] = dev->page;
	}

	tx = async_xor_zero_sum(xor_dest, xor_srcs, 0, count, STRIPE_SIZE,
		&sh->ops.zero_sum_result, 0, NULL, NULL, NULL);

	atomic_inc(&sh->count);
	tx = async_trigger_callback(ASYNC_TX_DEP_ACK | ASYNC_TX_ACK, tx,
		ops_complete_check, sh);
}

static void raid5_run_ops(struct stripe_head *sh, unsigned long ops_request)
{
	int overlap_clear = 0, i, disks = sh->disks;
	struct dma_async_tx_descriptor *tx = NULL;

	BUG_ON(sh->batch_head);
	count = 0;
	xor_dest = sh->dev[pd_idx].page;
	xor_srcs[count++] = xor_dest;
	for (i = disks; i--; ) {
		if (i == pd_idx || i == qd_idx)
			continue;
		xor_srcs[count++] = sh->dev[i].page;
	}

	init_async_submit(&submit, 0, NULL, NULL, NULL,
			  to_addr_conv(sh, percpu, 0));
	tx = async_xor_val(xor_dest, xor_srcs, 0, count, STRIPE_SIZE,
			   &sh->ops.zero_sum_result, &submit);

	atomic_inc(&sh->count);
	init_async_submit(&submit, ASYNC_TX_ACK, tx, ops_complete_check, sh, NULL);
	tx = async_trigger_callback(&submit);
}

static void ops_run_check_pq(struct stripe_head *sh, struct raid5_percpu *percpu, int checkp)
{
	struct page **srcs = to_addr_page(percpu, 0);
	struct async_submit_ctl submit;
	int count;

	pr_debug("%s: stripe %llu checkp: %d\n", __func__,
		(unsigned long long)sh->sector, checkp);

	BUG_ON(sh->batch_head);
	count = set_syndrome_sources(srcs, sh, SYNDROME_SRC_ALL);
	if (!checkp)
		srcs[count] = NULL;

	atomic_inc(&sh->count);
	init_async_submit(&submit, ASYNC_TX_ACK, NULL, ops_complete_check,
			  sh, to_addr_conv(sh, percpu, 0));
	async_syndrome_val(srcs, 0, count+2, STRIPE_SIZE,
			   &sh->ops.zero_sum_result, percpu->spare_page, &submit);
}

static void raid_run_ops(struct stripe_head *sh, unsigned long ops_request)
{
	int overlap_clear = 0, i, disks = sh->disks;
	struct dma_async_tx_descriptor *tx = NULL;
	struct r5conf *conf = sh->raid_conf;
	int level = conf->level;
	struct raid5_percpu *percpu;
	unsigned long cpu;

	cpu = get_cpu_light();
	percpu = per_cpu_ptr(conf->percpu, cpu);
	spin_lock(&percpu->lock);
	if (test_bit(STRIPE_OP_BIOFILL, &ops_request)) {
		ops_run_biofill(sh);
		overlap_clear++;
	}

	if (test_bit(STRIPE_OP_COMPUTE_BLK, &ops_request)) {
		tx = ops_run_compute5(sh);
		/* terminate the chain if postxor is not set to be run */
		if (tx && !test_bit(STRIPE_OP_POSTXOR, &ops_request))
			async_tx_ack(tx);
	}

	if (test_bit(STRIPE_OP_PREXOR, &ops_request))
		tx = ops_run_prexor(sh, tx);
		if (level < 6)
			tx = ops_run_compute5(sh, percpu);
		else {
			if (sh->ops.target2 < 0 || sh->ops.target < 0)
				tx = ops_run_compute6_1(sh, percpu);
			else
				tx = ops_run_compute6_2(sh, percpu);
		}
		/* terminate the chain if reconstruct is not set to be run */
		if (tx && !test_bit(STRIPE_OP_RECONSTRUCT, &ops_request))
			async_tx_ack(tx);
	}

	if (test_bit(STRIPE_OP_PREXOR, &ops_request)) {
		if (level < 6)
			tx = ops_run_prexor5(sh, percpu, tx);
		else
			tx = ops_run_prexor6(sh, percpu, tx);
	}

	if (test_bit(STRIPE_OP_BIODRAIN, &ops_request)) {
		tx = ops_run_biodrain(sh, tx);
		overlap_clear++;
	}

	if (test_bit(STRIPE_OP_POSTXOR, &ops_request))
		ops_run_postxor(sh, tx);

	if (test_bit(STRIPE_OP_CHECK, &ops_request))
		ops_run_check(sh);

	if (overlap_clear)
	if (test_bit(STRIPE_OP_RECONSTRUCT, &ops_request)) {
		if (level < 6)
			ops_run_reconstruct5(sh, percpu, tx);
		else
			ops_run_reconstruct6(sh, percpu, tx);
	}

	if (test_bit(STRIPE_OP_CHECK, &ops_request)) {
		if (sh->check_state == check_state_run)
			ops_run_check_p(sh, percpu);
		else if (sh->check_state == check_state_run_q)
			ops_run_check_pq(sh, percpu, 0);
		else if (sh->check_state == check_state_run_pq)
			ops_run_check_pq(sh, percpu, 1);
		else
			BUG();
	}

	if (overlap_clear && !sh->batch_head)
		for (i = disks; i--; ) {
			struct r5dev *dev = &sh->dev[i];
			if (test_and_clear_bit(R5_Overlap, &dev->flags))
				wake_up(&sh->raid_conf->wait_for_overlap);
		}
}

static int grow_one_stripe(raid5_conf_t *conf)
{
	struct stripe_head *sh;
	sh = kmem_cache_alloc(conf->slab_cache, GFP_KERNEL);
	if (!sh)
		return 0;
	memset(sh, 0, sizeof(*sh) + (conf->raid_disks-1)*sizeof(struct r5dev));
	sh->raid_conf = conf;
	spin_lock_init(&sh->lock);

	if (grow_buffers(sh, conf->raid_disks)) {
		shrink_buffers(sh, conf->raid_disks);
		kmem_cache_free(conf->slab_cache, sh);
		return 0;
	}
	sh->disks = conf->raid_disks;
	/* we just created an active stripe so... */
	atomic_set(&sh->count, 1);
	atomic_inc(&conf->active_stripes);
	INIT_LIST_HEAD(&sh->lru);
	release_stripe(sh);
	return 1;
}

static int grow_stripes(raid5_conf_t *conf, int num)
{
	struct kmem_cache *sc;
	int devs = conf->raid_disks;

	sprintf(conf->cache_name[0], "raid5-%s", mdname(conf->mddev));
	sprintf(conf->cache_name[1], "raid5-%s-alt", mdname(conf->mddev));
	put_cpu();
	spin_unlock(&percpu->lock);
	put_cpu_light();
}

static struct stripe_head *alloc_stripe(struct kmem_cache *sc, gfp_t gfp)
{
	struct stripe_head *sh;

	sh = kmem_cache_zalloc(sc, gfp);
	if (sh) {
		spin_lock_init(&sh->stripe_lock);
		spin_lock_init(&sh->batch_lock);
		INIT_LIST_HEAD(&sh->batch_list);
		INIT_LIST_HEAD(&sh->lru);
		atomic_set(&sh->count, 1);
	}
	return sh;
}
static int grow_one_stripe(struct r5conf *conf, gfp_t gfp)
{
	struct stripe_head *sh;

	sh = alloc_stripe(conf->slab_cache, gfp);
	if (!sh)
		return 0;

	sh->raid_conf = conf;

	if (grow_buffers(sh, gfp)) {
		shrink_buffers(sh);
		kmem_cache_free(conf->slab_cache, sh);
		return 0;
	}
	sh->hash_lock_index =
		conf->max_nr_stripes % NR_STRIPE_HASH_LOCKS;
	/* we just created an active stripe so... */
	atomic_inc(&conf->active_stripes);

	raid5_release_stripe(sh);
	conf->max_nr_stripes++;
	return 1;
}

static int grow_stripes(struct r5conf *conf, int num)
{
	struct kmem_cache *sc;
	size_t namelen = sizeof(conf->cache_name[0]);
	int devs = max(conf->raid_disks, conf->previous_raid_disks);

	if (conf->mddev->gendisk)
		snprintf(conf->cache_name[0], namelen,
			"raid%d-%s", conf->level, mdname(conf->mddev));
	else
		snprintf(conf->cache_name[0], namelen,
			"raid%d-%p", conf->level, conf->mddev);
	snprintf(conf->cache_name[1], namelen, "%.27s-alt", conf->cache_name[0]);

	conf->active_name = 0;
	sc = kmem_cache_create(conf->cache_name[conf->active_name],
			       sizeof(struct stripe_head)+(devs-1)*sizeof(struct r5dev),
			       0, 0, NULL);
	if (!sc)
		return 1;
	conf->slab_cache = sc;
	conf->pool_size = devs;
	while (num--)
		if (!grow_one_stripe(conf))
			return 1;
	return 0;
}

#ifdef CONFIG_MD_RAID5_RESHAPE
static int resize_stripes(raid5_conf_t *conf, int newsize)
		if (!grow_one_stripe(conf, GFP_KERNEL))
			return 1;

	return 0;
}

/**
 * scribble_len - return the required size of the scribble region
 * @num - total number of disks in the array
 *
 * The size must be enough to contain:
 * 1/ a struct page pointer for each device in the array +2
 * 2/ room to convert each entry in (1) to its corresponding dma
 *    (dma_map_page()) or page (page_address()) address.
 *
 * Note: the +2 is for the destination buffers of the ddf/raid6 case where we
 * calculate over all devices (not just the data blocks), using zeros in place
 * of the P and Q blocks.
 */
static struct flex_array *scribble_alloc(int num, int cnt, gfp_t flags)
{
	struct flex_array *ret;
	size_t len;

	len = sizeof(struct page *) * (num+2) + sizeof(addr_conv_t) * (num+2);
	ret = flex_array_alloc(len, cnt, flags);
	if (!ret)
		return NULL;
	/* always prealloc all elements, so no locking is required */
	if (flex_array_prealloc(ret, 0, cnt, flags)) {
		flex_array_free(ret);
		return NULL;
	}
	return ret;
}

static int resize_chunks(struct r5conf *conf, int new_disks, int new_sectors)
{
	unsigned long cpu;
	int err = 0;

	/*
	 * Never shrink. And mddev_suspend() could deadlock if this is called
	 * from raid5d. In that case, scribble_disks and scribble_sectors
	 * should equal to new_disks and new_sectors
	 */
	if (conf->scribble_disks >= new_disks &&
	    conf->scribble_sectors >= new_sectors)
		return 0;
	mddev_suspend(conf->mddev);
	get_online_cpus();
	for_each_present_cpu(cpu) {
		struct raid5_percpu *percpu;
		struct flex_array *scribble;

		percpu = per_cpu_ptr(conf->percpu, cpu);
		scribble = scribble_alloc(new_disks,
					  new_sectors / STRIPE_SECTORS,
					  GFP_NOIO);

		if (scribble) {
			flex_array_free(percpu->scribble);
			percpu->scribble = scribble;
		} else {
			err = -ENOMEM;
			break;
		}
	}
	put_online_cpus();
	mddev_resume(conf->mddev);
	if (!err) {
		conf->scribble_disks = new_disks;
		conf->scribble_sectors = new_sectors;
	}
	return err;
}

static int resize_stripes(struct r5conf *conf, int newsize)
{
	/* Make all the stripes able to hold 'newsize' devices.
	 * New slots in each stripe get 'page' set to a new page.
	 *
	 * This happens in stages:
	 * 1/ create a new kmem_cache and allocate the required number of
	 *    stripe_heads.
	 * 2/ gather all the old stripe_heads and tranfer the pages across
	 * 2/ gather all the old stripe_heads and transfer the pages across
	 *    to the new stripe_heads.  This will have the side effect of
	 *    freezing the array as once all stripe_heads have been collected,
	 *    no IO will be possible.  Old stripe heads are freed once their
	 *    pages have been transferred over, and the old kmem_cache is
	 *    freed when all stripes are done.
	 * 3/ reallocate conf->disks to be suitable bigger.  If this fails,
	 *    we simple return a failre status - no need to clean anything up.
	 * 4/ allocate new pages for the new slots in the new stripe_heads.
	 *    If this fails, we don't bother trying the shrink the
	 *    stripe_heads down again, we just leave them as they are.
	 *    As each stripe_head is processed the new one is released into
	 *    active service.
	 *
	 * Once step2 is started, we cannot afford to wait for a write,
	 * so we use GFP_NOIO allocations.
	 */
	struct stripe_head *osh, *nsh;
	LIST_HEAD(newstripes);
	struct disk_info *ndisks;
	int err;
	struct kmem_cache *sc;
	int i;
	int hash, cnt;

	if (newsize <= conf->pool_size)
		return 0; /* never bother to shrink */

	err = md_allow_write(conf->mddev);
	if (err)
		return err;

	/* Step 1 */
	sc = kmem_cache_create(conf->cache_name[1-conf->active_name],
			       sizeof(struct stripe_head)+(newsize-1)*sizeof(struct r5dev),
			       0, 0, NULL);
	if (!sc)
		return -ENOMEM;

	for (i = conf->max_nr_stripes; i; i--) {
		nsh = kmem_cache_alloc(sc, GFP_KERNEL);
		if (!nsh)
			break;

		memset(nsh, 0, sizeof(*nsh) + (newsize-1)*sizeof(struct r5dev));

		nsh->raid_conf = conf;
		spin_lock_init(&nsh->lock);

	/* Need to ensure auto-resizing doesn't interfere */
	mutex_lock(&conf->cache_size_mutex);

	for (i = conf->max_nr_stripes; i; i--) {
		nsh = alloc_stripe(sc, GFP_KERNEL);
		if (!nsh)
			break;

		nsh->raid_conf = conf;
		list_add(&nsh->lru, &newstripes);
	}
	if (i) {
		/* didn't get enough, give up */
		while (!list_empty(&newstripes)) {
			nsh = list_entry(newstripes.next, struct stripe_head, lru);
			list_del(&nsh->lru);
			kmem_cache_free(sc, nsh);
		}
		kmem_cache_destroy(sc);
		mutex_unlock(&conf->cache_size_mutex);
		return -ENOMEM;
	}
	/* Step 2 - Must use GFP_NOIO now.
	 * OK, we have enough stripes, start collecting inactive
	 * stripes and copying them over
	 */
	list_for_each_entry(nsh, &newstripes, lru) {
		spin_lock_irq(&conf->device_lock);
		wait_event_lock_irq(conf->wait_for_stripe,
				    !list_empty(&conf->inactive_list),
				    conf->device_lock,
				    unplug_slaves(conf->mddev)
			);
		osh = get_free_stripe(conf);
		spin_unlock_irq(&conf->device_lock);
		atomic_set(&nsh->count, 1);
		for(i=0; i<conf->pool_size; i++)
			nsh->dev[i].page = osh->dev[i].page;
		for( ; i<newsize; i++)
			nsh->dev[i].page = NULL;
		kmem_cache_free(conf->slab_cache, osh);
	hash = 0;
	cnt = 0;
	list_for_each_entry(nsh, &newstripes, lru) {
		lock_device_hash_lock(conf, hash);
		wait_event_cmd(conf->wait_for_stripe,
				    !list_empty(conf->inactive_list + hash),
				    unlock_device_hash_lock(conf, hash),
				    lock_device_hash_lock(conf, hash));
		osh = get_free_stripe(conf, hash);
		unlock_device_hash_lock(conf, hash);

		for(i=0; i<conf->pool_size; i++) {
			nsh->dev[i].page = osh->dev[i].page;
			nsh->dev[i].orig_page = osh->dev[i].page;
		}
		nsh->hash_lock_index = hash;
		kmem_cache_free(conf->slab_cache, osh);
		cnt++;
		if (cnt >= conf->max_nr_stripes / NR_STRIPE_HASH_LOCKS +
		    !!((conf->max_nr_stripes % NR_STRIPE_HASH_LOCKS) > hash)) {
			hash++;
			cnt = 0;
		}
	}
	kmem_cache_destroy(conf->slab_cache);

	/* Step 3.
	 * At this point, we are holding all the stripes so the array
	 * is completely stalled, so now is a good time to resize
	 * conf->disks.
	 * conf->disks and the scribble region
	 */
	ndisks = kzalloc(newsize * sizeof(struct disk_info), GFP_NOIO);
	if (ndisks) {
		for (i=0; i<conf->raid_disks; i++)
			ndisks[i] = conf->disks[i];
		kfree(conf->disks);
		conf->disks = ndisks;
	} else
		err = -ENOMEM;

	conf->slab_cache = sc;
	conf->active_name = 1-conf->active_name;

	/* Step 4, return new stripes to service */
	while(!list_empty(&newstripes)) {
		nsh = list_entry(newstripes.next, struct stripe_head, lru);
		list_del_init(&nsh->lru);

		for (i=conf->raid_disks; i < newsize; i++)
			if (nsh->dev[i].page == NULL) {
				struct page *p = alloc_page(GFP_NOIO);
				nsh->dev[i].page = p;
				if (!p)
					err = -ENOMEM;
			}
		release_stripe(nsh);
				nsh->dev[i].orig_page = p;
				if (!p)
					err = -ENOMEM;
			}
		raid5_release_stripe(nsh);
	}
	/* critical section pass, GFP_NOIO no longer needed */

	conf->slab_cache = sc;
	conf->active_name = 1-conf->active_name;
	conf->pool_size = newsize;
	return err;
}
#endif

static int drop_one_stripe(raid5_conf_t *conf)
{
	struct stripe_head *sh;

	spin_lock_irq(&conf->device_lock);
	sh = get_free_stripe(conf);
	spin_unlock_irq(&conf->device_lock);
	if (!sh)
		return 0;
	BUG_ON(atomic_read(&sh->count));
	shrink_buffers(sh, conf->pool_size);
	kmem_cache_free(conf->slab_cache, sh);
	atomic_dec(&conf->active_stripes);
	return 1;
}

static void shrink_stripes(raid5_conf_t *conf)
{
	while (drop_one_stripe(conf))
		;

	if (conf->slab_cache)
		kmem_cache_destroy(conf->slab_cache);
	conf->slab_cache = NULL;
}

static void raid5_end_read_request(struct bio * bi, int error)
{
 	struct stripe_head *sh = bi->bi_private;
	raid5_conf_t *conf = sh->raid_conf;
	int disks = sh->disks, i;
	int uptodate = test_bit(BIO_UPTODATE, &bi->bi_flags);
	char b[BDEVNAME_SIZE];
	mdk_rdev_t *rdev;

	if (!err)
		conf->pool_size = newsize;
	mutex_unlock(&conf->cache_size_mutex);

	return err;
}

static int drop_one_stripe(struct r5conf *conf)
{
	struct stripe_head *sh;
	int hash = (conf->max_nr_stripes - 1) & STRIPE_HASH_LOCKS_MASK;

	spin_lock_irq(conf->hash_locks + hash);
	sh = get_free_stripe(conf, hash);
	spin_unlock_irq(conf->hash_locks + hash);
	if (!sh)
		return 0;
	BUG_ON(atomic_read(&sh->count));
	shrink_buffers(sh);
	kmem_cache_free(conf->slab_cache, sh);
	atomic_dec(&conf->active_stripes);
	conf->max_nr_stripes--;
	return 1;
}

static void shrink_stripes(struct r5conf *conf)
{
	while (conf->max_nr_stripes &&
	       drop_one_stripe(conf))
		;

	kmem_cache_destroy(conf->slab_cache);
	conf->slab_cache = NULL;
}

static void raid5_end_read_request(struct bio * bi)
{
	struct stripe_head *sh = bi->bi_private;
	struct r5conf *conf = sh->raid_conf;
	int disks = sh->disks, i;
	char b[BDEVNAME_SIZE];
	struct md_rdev *rdev = NULL;
	sector_t s;

	for (i=0 ; i<disks; i++)
		if (bi == &sh->dev[i].req)
			break;

	pr_debug("end_read_request %llu/%d, count: %d, uptodate %d.\n",
		(unsigned long long)sh->sector, i, atomic_read(&sh->count),
		uptodate);
	pr_debug("end_read_request %llu/%d, count: %d, error %d.\n",
		(unsigned long long)sh->sector, i, atomic_read(&sh->count),
		bi->bi_error);
	if (i == disks) {
		BUG();
		return;
	}

	if (uptodate) {
		set_bit(R5_UPTODATE, &sh->dev[i].flags);
		if (test_bit(R5_ReadError, &sh->dev[i].flags)) {
			rdev = conf->disks[i].rdev;
			printk_rl(KERN_INFO "raid5:%s: read error corrected"
				  " (%lu sectors at %llu on %s)\n",
				  mdname(conf->mddev), STRIPE_SECTORS,
				  (unsigned long long)(sh->sector
						       + rdev->data_offset),
				  bdevname(rdev->bdev, b));
			clear_bit(R5_ReadError, &sh->dev[i].flags);
			clear_bit(R5_ReWrite, &sh->dev[i].flags);
		}
		if (atomic_read(&conf->disks[i].rdev->read_errors))
			atomic_set(&conf->disks[i].rdev->read_errors, 0);
	} else {
		const char *bdn = bdevname(conf->disks[i].rdev->bdev, b);
		int retry = 0;
		rdev = conf->disks[i].rdev;

		clear_bit(R5_UPTODATE, &sh->dev[i].flags);
		atomic_inc(&rdev->read_errors);
		if (conf->mddev->degraded)
			printk_rl(KERN_WARNING
				  "raid5:%s: read error not correctable "
				  "(sector %llu on %s).\n",
				  mdname(conf->mddev),
				  (unsigned long long)(sh->sector
						       + rdev->data_offset),
				  bdn);
		else if (test_bit(R5_ReWrite, &sh->dev[i].flags))
			/* Oh, no!!! */
			printk_rl(KERN_WARNING
				  "raid5:%s: read error NOT corrected!! "
				  "(sector %llu on %s).\n",
				  mdname(conf->mddev),
				  (unsigned long long)(sh->sector
						       + rdev->data_offset),
				  bdn);
		else if (atomic_read(&rdev->read_errors)
			 > conf->max_nr_stripes)
			printk(KERN_WARNING
			       "raid5:%s: Too many read errors, failing device %s.\n",
			       mdname(conf->mddev), bdn);
		else
			retry = 1;
		if (retry)
			set_bit(R5_ReadError, &sh->dev[i].flags);
		else {
			clear_bit(R5_ReadError, &sh->dev[i].flags);
			clear_bit(R5_ReWrite, &sh->dev[i].flags);
			md_error(conf->mddev, rdev);
		}
	}
	rdev_dec_pending(conf->disks[i].rdev, conf->mddev);
	clear_bit(R5_LOCKED, &sh->dev[i].flags);
	set_bit(STRIPE_HANDLE, &sh->state);
	release_stripe(sh);
}

static void raid5_end_write_request (struct bio *bi, int error)
{
 	struct stripe_head *sh = bi->bi_private;
	raid5_conf_t *conf = sh->raid_conf;
	int disks = sh->disks, i;
	int uptodate = test_bit(BIO_UPTODATE, &bi->bi_flags);

	for (i=0 ; i<disks; i++)
		if (bi == &sh->dev[i].req)
			break;

	pr_debug("end_write_request %llu/%d, count %d, uptodate: %d.\n",
		(unsigned long long)sh->sector, i, atomic_read(&sh->count),
		uptodate);
	if (test_bit(R5_ReadRepl, &sh->dev[i].flags))
		/* If replacement finished while this request was outstanding,
		 * 'replacement' might be NULL already.
		 * In that case it moved down to 'rdev'.
		 * rdev is not removed until all requests are finished.
		 */
		rdev = conf->disks[i].replacement;
	if (!rdev)
		rdev = conf->disks[i].rdev;

	if (use_new_offset(conf, sh))
		s = sh->sector + rdev->new_data_offset;
	else
		s = sh->sector + rdev->data_offset;
	if (!bi->bi_error) {
		set_bit(R5_UPTODATE, &sh->dev[i].flags);
		if (test_bit(R5_ReadError, &sh->dev[i].flags)) {
			/* Note that this cannot happen on a
			 * replacement device.  We just fail those on
			 * any error
			 */
			printk_ratelimited(
				KERN_INFO
				"md/raid:%s: read error corrected"
				" (%lu sectors at %llu on %s)\n",
				mdname(conf->mddev), STRIPE_SECTORS,
				(unsigned long long)s,
				bdevname(rdev->bdev, b));
			atomic_add(STRIPE_SECTORS, &rdev->corrected_errors);
			clear_bit(R5_ReadError, &sh->dev[i].flags);
			clear_bit(R5_ReWrite, &sh->dev[i].flags);
		} else if (test_bit(R5_ReadNoMerge, &sh->dev[i].flags))
			clear_bit(R5_ReadNoMerge, &sh->dev[i].flags);

		if (atomic_read(&rdev->read_errors))
			atomic_set(&rdev->read_errors, 0);
	} else {
		const char *bdn = bdevname(rdev->bdev, b);
		int retry = 0;
		int set_bad = 0;

		clear_bit(R5_UPTODATE, &sh->dev[i].flags);
		atomic_inc(&rdev->read_errors);
		if (test_bit(R5_ReadRepl, &sh->dev[i].flags))
			printk_ratelimited(
				KERN_WARNING
				"md/raid:%s: read error on replacement device "
				"(sector %llu on %s).\n",
				mdname(conf->mddev),
				(unsigned long long)s,
				bdn);
		else if (conf->mddev->degraded >= conf->max_degraded) {
			set_bad = 1;
			printk_ratelimited(
				KERN_WARNING
				"md/raid:%s: read error not correctable "
				"(sector %llu on %s).\n",
				mdname(conf->mddev),
				(unsigned long long)s,
				bdn);
		} else if (test_bit(R5_ReWrite, &sh->dev[i].flags)) {
			/* Oh, no!!! */
			set_bad = 1;
			printk_ratelimited(
				KERN_WARNING
				"md/raid:%s: read error NOT corrected!! "
				"(sector %llu on %s).\n",
				mdname(conf->mddev),
				(unsigned long long)s,
				bdn);
		} else if (atomic_read(&rdev->read_errors)
			 > conf->max_nr_stripes)
			printk(KERN_WARNING
			       "md/raid:%s: Too many read errors, failing device %s.\n",
			       mdname(conf->mddev), bdn);
		else
			retry = 1;
		if (set_bad && test_bit(In_sync, &rdev->flags)
		    && !test_bit(R5_ReadNoMerge, &sh->dev[i].flags))
			retry = 1;
		if (retry)
			if (sh->qd_idx >= 0 && sh->pd_idx == i)
				set_bit(R5_ReadError, &sh->dev[i].flags);
			else if (test_bit(R5_ReadNoMerge, &sh->dev[i].flags)) {
				set_bit(R5_ReadError, &sh->dev[i].flags);
				clear_bit(R5_ReadNoMerge, &sh->dev[i].flags);
			} else
				set_bit(R5_ReadNoMerge, &sh->dev[i].flags);
		else {
			clear_bit(R5_ReadError, &sh->dev[i].flags);
			clear_bit(R5_ReWrite, &sh->dev[i].flags);
			if (!(set_bad
			      && test_bit(In_sync, &rdev->flags)
			      && rdev_set_badblocks(
				      rdev, sh->sector, STRIPE_SECTORS, 0)))
				md_error(conf->mddev, rdev);
		}
	}
	rdev_dec_pending(rdev, conf->mddev);
	clear_bit(R5_LOCKED, &sh->dev[i].flags);
	set_bit(STRIPE_HANDLE, &sh->state);
	raid5_release_stripe(sh);
}

static void raid5_end_write_request(struct bio *bi)
{
	struct stripe_head *sh = bi->bi_private;
	struct r5conf *conf = sh->raid_conf;
	int disks = sh->disks, i;
	struct md_rdev *uninitialized_var(rdev);
	sector_t first_bad;
	int bad_sectors;
	int replacement = 0;

	for (i = 0 ; i < disks; i++) {
		if (bi == &sh->dev[i].req) {
			rdev = conf->disks[i].rdev;
			break;
		}
		if (bi == &sh->dev[i].rreq) {
			rdev = conf->disks[i].replacement;
			if (rdev)
				replacement = 1;
			else
				/* rdev was removed and 'replacement'
				 * replaced it.  rdev is not removed
				 * until all requests are finished.
				 */
				rdev = conf->disks[i].rdev;
			break;
		}
	}
	pr_debug("end_write_request %llu/%d, count %d, error: %d.\n",
		(unsigned long long)sh->sector, i, atomic_read(&sh->count),
		bi->bi_error);
	if (i == disks) {
		BUG();
		return;
	}

	if (!uptodate)
		md_error(conf->mddev, conf->disks[i].rdev);

	rdev_dec_pending(conf->disks[i].rdev, conf->mddev);
	
	clear_bit(R5_LOCKED, &sh->dev[i].flags);
	set_bit(STRIPE_HANDLE, &sh->state);
	release_stripe(sh);
}


static sector_t compute_blocknr(struct stripe_head *sh, int i);
	
static void raid5_build_block (struct stripe_head *sh, int i)
	if (replacement) {
		if (bi->bi_error)
			md_error(conf->mddev, rdev);
		else if (is_badblock(rdev, sh->sector,
				     STRIPE_SECTORS,
				     &first_bad, &bad_sectors))
			set_bit(R5_MadeGoodRepl, &sh->dev[i].flags);
	} else {
		if (bi->bi_error) {
			set_bit(STRIPE_DEGRADED, &sh->state);
			set_bit(WriteErrorSeen, &rdev->flags);
			set_bit(R5_WriteError, &sh->dev[i].flags);
			if (!test_and_set_bit(WantReplacement, &rdev->flags))
				set_bit(MD_RECOVERY_NEEDED,
					&rdev->mddev->recovery);
		} else if (is_badblock(rdev, sh->sector,
				       STRIPE_SECTORS,
				       &first_bad, &bad_sectors)) {
			set_bit(R5_MadeGood, &sh->dev[i].flags);
			if (test_bit(R5_ReadError, &sh->dev[i].flags))
				/* That was a successful write so make
				 * sure it looks like we already did
				 * a re-write.
				 */
				set_bit(R5_ReWrite, &sh->dev[i].flags);
		}
	}
	rdev_dec_pending(rdev, conf->mddev);

	if (sh->batch_head && bi->bi_error && !replacement)
		set_bit(STRIPE_BATCH_ERR, &sh->batch_head->state);

	if (!test_and_clear_bit(R5_DOUBLE_LOCKED, &sh->dev[i].flags))
		clear_bit(R5_LOCKED, &sh->dev[i].flags);
	set_bit(STRIPE_HANDLE, &sh->state);

	if (sh->batch_head && sh != sh->batch_head)
		raid5_release_stripe(sh->batch_head);
	raid5_release_stripe(sh);
}

static void raid5_build_block(struct stripe_head *sh, int i, int previous)
{
	struct r5dev *dev = &sh->dev[i];

	bio_init(&dev->req);
	dev->req.bi_io_vec = &dev->vec;
	dev->req.bi_vcnt++;
	dev->req.bi_max_vecs++;
	dev->vec.bv_page = dev->page;
	dev->vec.bv_len = STRIPE_SIZE;
	dev->vec.bv_offset = 0;

	dev->req.bi_sector = sh->sector;
	dev->req.bi_private = sh;

	dev->flags = 0;
	dev->sector = compute_blocknr(sh, i);
}

static void error(mddev_t *mddev, mdk_rdev_t *rdev)
{
	char b[BDEVNAME_SIZE];
	raid5_conf_t *conf = (raid5_conf_t *) mddev->private;
	pr_debug("raid5: error called\n");

	if (!test_bit(Faulty, &rdev->flags)) {
		set_bit(MD_CHANGE_DEVS, &mddev->flags);
		if (test_and_clear_bit(In_sync, &rdev->flags)) {
			unsigned long flags;
			spin_lock_irqsave(&conf->device_lock, flags);
			mddev->degraded++;
			spin_unlock_irqrestore(&conf->device_lock, flags);
			/*
			 * if recovery was running, make sure it aborts.
			 */
			set_bit(MD_RECOVERY_INTR, &mddev->recovery);
		}
		set_bit(Faulty, &rdev->flags);
		printk (KERN_ALERT
			"raid5: Disk failure on %s, disabling device.\n"
			"raid5: Operation continuing on %d devices.\n",
			bdevname(rdev->bdev,b), conf->raid_disks - mddev->degraded);
	}
	dev->req.bi_max_vecs = 1;
	dev->req.bi_private = sh;

	bio_init(&dev->rreq);
	dev->rreq.bi_io_vec = &dev->rvec;
	dev->rreq.bi_max_vecs = 1;
	dev->rreq.bi_private = sh;

	dev->flags = 0;
	dev->sector = raid5_compute_blocknr(sh, i, previous);
}

static void error(struct mddev *mddev, struct md_rdev *rdev)
{
	char b[BDEVNAME_SIZE];
	struct r5conf *conf = mddev->private;
	unsigned long flags;
	pr_debug("raid456: error called\n");

	spin_lock_irqsave(&conf->device_lock, flags);
	clear_bit(In_sync, &rdev->flags);
	mddev->degraded = calc_degraded(conf);
	spin_unlock_irqrestore(&conf->device_lock, flags);
	set_bit(MD_RECOVERY_INTR, &mddev->recovery);

	set_bit(Blocked, &rdev->flags);
	set_bit(Faulty, &rdev->flags);
	set_bit(MD_CHANGE_DEVS, &mddev->flags);
	set_bit(MD_CHANGE_PENDING, &mddev->flags);
	printk(KERN_ALERT
	       "md/raid:%s: Disk failure on %s, disabling device.\n"
	       "md/raid:%s: Operation continuing on %d devices.\n",
	       mdname(mddev),
	       bdevname(rdev->bdev, b),
	       mdname(mddev),
	       conf->raid_disks - mddev->degraded);
}

/*
 * Input: a 'big' sector number,
 * Output: index of the data and parity disk, and the sector # in them.
 */
static sector_t raid5_compute_sector(sector_t r_sector, unsigned int raid_disks,
			unsigned int data_disks, unsigned int * dd_idx,
			unsigned int * pd_idx, raid5_conf_t *conf)
{
	long stripe;
	unsigned long chunk_number;
	unsigned int chunk_offset;
	sector_t new_sector;
	int sectors_per_chunk = conf->chunk_size >> 9;
sector_t raid5_compute_sector(struct r5conf *conf, sector_t r_sector,
			      int previous, int *dd_idx,
			      struct stripe_head *sh)
{
	sector_t stripe, stripe2;
	sector_t chunk_number;
	unsigned int chunk_offset;
	int pd_idx, qd_idx;
	int ddf_layout = 0;
	sector_t new_sector;
	int algorithm = previous ? conf->prev_algo
				 : conf->algorithm;
	int sectors_per_chunk = previous ? conf->prev_chunk_sectors
					 : conf->chunk_sectors;
	int raid_disks = previous ? conf->previous_raid_disks
				  : conf->raid_disks;
	int data_disks = raid_disks - conf->max_degraded;

	/* First compute the information on this sector */

	/*
	 * Compute the chunk number and the sector offset inside the chunk
	 */
	chunk_offset = sector_div(r_sector, sectors_per_chunk);
	chunk_number = r_sector;
	BUG_ON(r_sector != chunk_number);

	/*
	 * Compute the stripe number
	 */
	stripe = chunk_number / data_disks;

	/*
	 * Compute the data disk and parity disk indexes inside the stripe
	 */
	*dd_idx = chunk_number % data_disks;

	/*
	 * Select the parity disk based on the user selected algorithm.
	 */
	switch(conf->level) {
	case 4:
		*pd_idx = data_disks;
		break;
	case 5:
		switch (conf->algorithm) {
		case ALGORITHM_LEFT_ASYMMETRIC:
			*pd_idx = data_disks - stripe % raid_disks;
			if (*dd_idx >= *pd_idx)
				(*dd_idx)++;
			break;
		case ALGORITHM_RIGHT_ASYMMETRIC:
			*pd_idx = stripe % raid_disks;
			if (*dd_idx >= *pd_idx)
				(*dd_idx)++;
			break;
		case ALGORITHM_LEFT_SYMMETRIC:
			*pd_idx = data_disks - stripe % raid_disks;
			*dd_idx = (*pd_idx + 1 + *dd_idx) % raid_disks;
			break;
		case ALGORITHM_RIGHT_SYMMETRIC:
			*pd_idx = stripe % raid_disks;
			*dd_idx = (*pd_idx + 1 + *dd_idx) % raid_disks;
			break;
		default:
			printk(KERN_ERR "raid5: unsupported algorithm %d\n",
				conf->algorithm);
	stripe = chunk_number;
	*dd_idx = sector_div(stripe, data_disks);
	stripe2 = stripe;
	/*
	 * Select the parity disk based on the user selected algorithm.
	 */
	pd_idx = qd_idx = -1;
	switch(conf->level) {
	case 4:
		pd_idx = data_disks;
		break;
	case 5:
		switch (algorithm) {
		case ALGORITHM_LEFT_ASYMMETRIC:
			pd_idx = data_disks - sector_div(stripe2, raid_disks);
			if (*dd_idx >= pd_idx)
				(*dd_idx)++;
			break;
		case ALGORITHM_RIGHT_ASYMMETRIC:
			pd_idx = sector_div(stripe2, raid_disks);
			if (*dd_idx >= pd_idx)
				(*dd_idx)++;
			break;
		case ALGORITHM_LEFT_SYMMETRIC:
			pd_idx = data_disks - sector_div(stripe2, raid_disks);
			*dd_idx = (pd_idx + 1 + *dd_idx) % raid_disks;
			break;
		case ALGORITHM_RIGHT_SYMMETRIC:
			pd_idx = sector_div(stripe2, raid_disks);
			*dd_idx = (pd_idx + 1 + *dd_idx) % raid_disks;
			break;
		case ALGORITHM_PARITY_0:
			pd_idx = 0;
			(*dd_idx)++;
			break;
		case ALGORITHM_PARITY_N:
			pd_idx = data_disks;
			break;
		default:
			BUG();
		}
		break;
	case 6:

		/**** FIX THIS ****/
		switch (conf->algorithm) {
		case ALGORITHM_LEFT_ASYMMETRIC:
			*pd_idx = raid_disks - 1 - (stripe % raid_disks);
			if (*pd_idx == raid_disks-1)
				(*dd_idx)++; 	/* Q D D D P */
			else if (*dd_idx >= *pd_idx)
				(*dd_idx) += 2; /* D D P Q D */
			break;
		case ALGORITHM_RIGHT_ASYMMETRIC:
			*pd_idx = stripe % raid_disks;
			if (*pd_idx == raid_disks-1)
				(*dd_idx)++; 	/* Q D D D P */
			else if (*dd_idx >= *pd_idx)
				(*dd_idx) += 2; /* D D P Q D */
			break;
		case ALGORITHM_LEFT_SYMMETRIC:
			*pd_idx = raid_disks - 1 - (stripe % raid_disks);
			*dd_idx = (*pd_idx + 2 + *dd_idx) % raid_disks;
			break;
		case ALGORITHM_RIGHT_SYMMETRIC:
			*pd_idx = stripe % raid_disks;
			*dd_idx = (*pd_idx + 2 + *dd_idx) % raid_disks;
			break;
		default:
			printk (KERN_CRIT "raid6: unsupported algorithm %d\n",
				conf->algorithm);
		switch (algorithm) {
		case ALGORITHM_LEFT_ASYMMETRIC:
			pd_idx = raid_disks - 1 - sector_div(stripe2, raid_disks);
			qd_idx = pd_idx + 1;
			if (pd_idx == raid_disks-1) {
				(*dd_idx)++;	/* Q D D D P */
				qd_idx = 0;
			} else if (*dd_idx >= pd_idx)
				(*dd_idx) += 2; /* D D P Q D */
			break;
		case ALGORITHM_RIGHT_ASYMMETRIC:
			pd_idx = sector_div(stripe2, raid_disks);
			qd_idx = pd_idx + 1;
			if (pd_idx == raid_disks-1) {
				(*dd_idx)++;	/* Q D D D P */
				qd_idx = 0;
			} else if (*dd_idx >= pd_idx)
				(*dd_idx) += 2; /* D D P Q D */
			break;
		case ALGORITHM_LEFT_SYMMETRIC:
			pd_idx = raid_disks - 1 - sector_div(stripe2, raid_disks);
			qd_idx = (pd_idx + 1) % raid_disks;
			*dd_idx = (pd_idx + 2 + *dd_idx) % raid_disks;
			break;
		case ALGORITHM_RIGHT_SYMMETRIC:
			pd_idx = sector_div(stripe2, raid_disks);
			qd_idx = (pd_idx + 1) % raid_disks;
			*dd_idx = (pd_idx + 2 + *dd_idx) % raid_disks;
			break;

		case ALGORITHM_PARITY_0:
			pd_idx = 0;
			qd_idx = 1;
			(*dd_idx) += 2;
			break;
		case ALGORITHM_PARITY_N:
			pd_idx = data_disks;
			qd_idx = data_disks + 1;
			break;

		case ALGORITHM_ROTATING_ZERO_RESTART:
			/* Exactly the same as RIGHT_ASYMMETRIC, but or
			 * of blocks for computing Q is different.
			 */
			pd_idx = sector_div(stripe2, raid_disks);
			qd_idx = pd_idx + 1;
			if (pd_idx == raid_disks-1) {
				(*dd_idx)++;	/* Q D D D P */
				qd_idx = 0;
			} else if (*dd_idx >= pd_idx)
				(*dd_idx) += 2; /* D D P Q D */
			ddf_layout = 1;
			break;

		case ALGORITHM_ROTATING_N_RESTART:
			/* Same a left_asymmetric, by first stripe is
			 * D D D P Q  rather than
			 * Q D D D P
			 */
			stripe2 += 1;
			pd_idx = raid_disks - 1 - sector_div(stripe2, raid_disks);
			qd_idx = pd_idx + 1;
			if (pd_idx == raid_disks-1) {
				(*dd_idx)++;	/* Q D D D P */
				qd_idx = 0;
			} else if (*dd_idx >= pd_idx)
				(*dd_idx) += 2; /* D D P Q D */
			ddf_layout = 1;
			break;

		case ALGORITHM_ROTATING_N_CONTINUE:
			/* Same as left_symmetric but Q is before P */
			pd_idx = raid_disks - 1 - sector_div(stripe2, raid_disks);
			qd_idx = (pd_idx + raid_disks - 1) % raid_disks;
			*dd_idx = (pd_idx + 1 + *dd_idx) % raid_disks;
			ddf_layout = 1;
			break;

		case ALGORITHM_LEFT_ASYMMETRIC_6:
			/* RAID5 left_asymmetric, with Q on last device */
			pd_idx = data_disks - sector_div(stripe2, raid_disks-1);
			if (*dd_idx >= pd_idx)
				(*dd_idx)++;
			qd_idx = raid_disks - 1;
			break;

		case ALGORITHM_RIGHT_ASYMMETRIC_6:
			pd_idx = sector_div(stripe2, raid_disks-1);
			if (*dd_idx >= pd_idx)
				(*dd_idx)++;
			qd_idx = raid_disks - 1;
			break;

		case ALGORITHM_LEFT_SYMMETRIC_6:
			pd_idx = data_disks - sector_div(stripe2, raid_disks-1);
			*dd_idx = (pd_idx + 1 + *dd_idx) % (raid_disks-1);
			qd_idx = raid_disks - 1;
			break;

		case ALGORITHM_RIGHT_SYMMETRIC_6:
			pd_idx = sector_div(stripe2, raid_disks-1);
			*dd_idx = (pd_idx + 1 + *dd_idx) % (raid_disks-1);
			qd_idx = raid_disks - 1;
			break;

		case ALGORITHM_PARITY_0_6:
			pd_idx = 0;
			(*dd_idx)++;
			qd_idx = raid_disks - 1;
			break;

		default:
			BUG();
		}
		break;
	}

	if (sh) {
		sh->pd_idx = pd_idx;
		sh->qd_idx = qd_idx;
		sh->ddf_layout = ddf_layout;
	}
	/*
	 * Finally, compute the new sector number
	 */
	new_sector = (sector_t)stripe * sectors_per_chunk + chunk_offset;
	return new_sector;
}


static sector_t compute_blocknr(struct stripe_head *sh, int i)
{
	raid5_conf_t *conf = sh->raid_conf;
	int raid_disks = sh->disks;
	int data_disks = raid_disks - conf->max_degraded;
	sector_t new_sector = sh->sector, check;
	int sectors_per_chunk = conf->chunk_size >> 9;
	sector_t stripe;
	int chunk_offset;
	int chunk_number, dummy1, dummy2, dd_idx = i;
	sector_t r_sector;


	chunk_offset = sector_div(new_sector, sectors_per_chunk);
	stripe = new_sector;
	BUG_ON(new_sector != stripe);
sector_t raid5_compute_blocknr(struct stripe_head *sh, int i, int previous)
{
	struct r5conf *conf = sh->raid_conf;
	int raid_disks = sh->disks;
	int data_disks = raid_disks - conf->max_degraded;
	sector_t new_sector = sh->sector, check;
	int sectors_per_chunk = previous ? conf->prev_chunk_sectors
					 : conf->chunk_sectors;
	int algorithm = previous ? conf->prev_algo
				 : conf->algorithm;
	sector_t stripe;
	int chunk_offset;
	sector_t chunk_number;
	int dummy1, dd_idx = i;
	sector_t r_sector;
	struct stripe_head sh2;

	chunk_offset = sector_div(new_sector, sectors_per_chunk);
	stripe = new_sector;

	if (i == sh->pd_idx)
		return 0;
	switch(conf->level) {
	case 4: break;
	case 5:
		switch (conf->algorithm) {
		switch (algorithm) {
		case ALGORITHM_LEFT_ASYMMETRIC:
		case ALGORITHM_RIGHT_ASYMMETRIC:
			if (i > sh->pd_idx)
				i--;
			break;
		case ALGORITHM_LEFT_SYMMETRIC:
		case ALGORITHM_RIGHT_SYMMETRIC:
			if (i < sh->pd_idx)
				i += raid_disks;
			i -= (sh->pd_idx + 1);
			break;
		default:
			printk(KERN_ERR "raid5: unsupported algorithm %d\n",
			       conf->algorithm);
		}
		break;
	case 6:
		if (i == raid6_next_disk(sh->pd_idx, raid_disks))
			return 0; /* It is the Q disk */
		switch (conf->algorithm) {
		case ALGORITHM_LEFT_ASYMMETRIC:
		case ALGORITHM_RIGHT_ASYMMETRIC:
		  	if (sh->pd_idx == raid_disks-1)
				i--; 	/* Q D D D P */
		case ALGORITHM_PARITY_0:
			i -= 1;
			break;
		case ALGORITHM_PARITY_N:
			break;
		default:
			BUG();
		}
		break;
	case 6:
		if (i == sh->qd_idx)
			return 0; /* It is the Q disk */
		switch (algorithm) {
		case ALGORITHM_LEFT_ASYMMETRIC:
		case ALGORITHM_RIGHT_ASYMMETRIC:
		case ALGORITHM_ROTATING_ZERO_RESTART:
		case ALGORITHM_ROTATING_N_RESTART:
			if (sh->pd_idx == raid_disks-1)
				i--;	/* Q D D D P */
			else if (i > sh->pd_idx)
				i -= 2; /* D D P Q D */
			break;
		case ALGORITHM_LEFT_SYMMETRIC:
		case ALGORITHM_RIGHT_SYMMETRIC:
			if (sh->pd_idx == raid_disks-1)
				i--; /* Q D D D P */
			else {
				/* D D P Q D */
				if (i < sh->pd_idx)
					i += raid_disks;
				i -= (sh->pd_idx + 2);
			}
			break;
		default:
			printk (KERN_CRIT "raid6: unsupported algorithm %d\n",
				conf->algorithm);
		case ALGORITHM_PARITY_0:
			i -= 2;
			break;
		case ALGORITHM_PARITY_N:
			break;
		case ALGORITHM_ROTATING_N_CONTINUE:
			/* Like left_symmetric, but P is before Q */
			if (sh->pd_idx == 0)
				i--;	/* P D D D Q */
			else {
				/* D D Q P D */
				if (i < sh->pd_idx)
					i += raid_disks;
				i -= (sh->pd_idx + 1);
			}
			break;
		case ALGORITHM_LEFT_ASYMMETRIC_6:
		case ALGORITHM_RIGHT_ASYMMETRIC_6:
			if (i > sh->pd_idx)
				i--;
			break;
		case ALGORITHM_LEFT_SYMMETRIC_6:
		case ALGORITHM_RIGHT_SYMMETRIC_6:
			if (i < sh->pd_idx)
				i += data_disks + 1;
			i -= (sh->pd_idx + 1);
			break;
		case ALGORITHM_PARITY_0_6:
			i -= 1;
			break;
		default:
			BUG();
		}
		break;
	}

	chunk_number = stripe * data_disks + i;
	r_sector = (sector_t)chunk_number * sectors_per_chunk + chunk_offset;

	check = raid5_compute_sector (r_sector, raid_disks, data_disks, &dummy1, &dummy2, conf);
	if (check != sh->sector || dummy1 != dd_idx || dummy2 != sh->pd_idx) {
		printk(KERN_ERR "compute_blocknr: map not correct\n");
	r_sector = chunk_number * sectors_per_chunk + chunk_offset;

	check = raid5_compute_sector(conf, r_sector,
				     previous, &dummy1, &sh2);
	if (check != sh->sector || dummy1 != dd_idx || sh2.pd_idx != sh->pd_idx
		|| sh2.qd_idx != sh->qd_idx) {
		printk(KERN_ERR "md/raid:%s: compute_blocknr: map not correct\n",
		       mdname(conf->mddev));
		return 0;
	}
	return r_sector;
}



/*
 * Copy data between a page in the stripe cache, and one or more bion
 * The page could align with the middle of the bio, or there could be
 * several bion, each with several bio_vecs, which cover part of the page
 * Multiple bion are linked together on bi_next.  There may be extras
 * at the end of this list.  We ignore them.
 */
static void copy_data(int frombio, struct bio *bio,
		     struct page *page,
		     sector_t sector)
{
	char *pa = page_address(page);
	struct bio_vec *bvl;
	int i;
	int page_offset;

	if (bio->bi_sector >= sector)
		page_offset = (signed)(bio->bi_sector - sector) * 512;
	else
		page_offset = (signed)(sector - bio->bi_sector) * -512;
	bio_for_each_segment(bvl, bio, i) {
		int len = bio_iovec_idx(bio,i)->bv_len;
		int clen;
		int b_offset = 0;

		if (page_offset < 0) {
			b_offset = -page_offset;
			page_offset += b_offset;
			len -= b_offset;
		}

		if (len > 0 && page_offset + len > STRIPE_SIZE)
			clen = STRIPE_SIZE - page_offset;
		else clen = len;

		if (clen > 0) {
			char *ba = __bio_kmap_atomic(bio, i, KM_USER0);
			if (frombio)
				memcpy(pa+page_offset, ba+b_offset, clen);
			else
				memcpy(ba+b_offset, pa+page_offset, clen);
			__bio_kunmap_atomic(ba, KM_USER0);
		}
		if (clen < len) /* hit end of page */
			break;
		page_offset +=  len;
	}
}

#define check_xor()	do {						  \
				if (count == MAX_XOR_BLOCKS) {		  \
				xor_blocks(count, STRIPE_SIZE, dest, ptr);\
				count = 0;				  \
			   }						  \
			} while(0)

static void compute_parity6(struct stripe_head *sh, int method)
{
	raid6_conf_t *conf = sh->raid_conf;
	int i, pd_idx = sh->pd_idx, qd_idx, d0_idx, disks = sh->disks, count;
	struct bio *chosen;
	/**** FIX THIS: This could be very bad if disks is close to 256 ****/
	void *ptrs[disks];

	qd_idx = raid6_next_disk(pd_idx, disks);
	d0_idx = raid6_next_disk(qd_idx, disks);

	pr_debug("compute_parity, stripe %llu, method %d\n",
		(unsigned long long)sh->sector, method);

	switch(method) {
	case READ_MODIFY_WRITE:
		BUG();		/* READ_MODIFY_WRITE N/A for RAID-6 */
	case RECONSTRUCT_WRITE:
		for (i= disks; i-- ;)
			if ( i != pd_idx && i != qd_idx && sh->dev[i].towrite ) {
				chosen = sh->dev[i].towrite;
				sh->dev[i].towrite = NULL;

				if (test_and_clear_bit(R5_Overlap, &sh->dev[i].flags))
					wake_up(&conf->wait_for_overlap);

				BUG_ON(sh->dev[i].written);
				sh->dev[i].written = chosen;
			}
		break;
	case CHECK_PARITY:
		BUG();		/* Not implemented yet */
	}

	for (i = disks; i--;)
		if (sh->dev[i].written) {
			sector_t sector = sh->dev[i].sector;
			struct bio *wbi = sh->dev[i].written;
			while (wbi && wbi->bi_sector < sector + STRIPE_SECTORS) {
				copy_data(1, wbi, sh->dev[i].page, sector);
				wbi = r5_next_bio(wbi, sector);
			}

			set_bit(R5_LOCKED, &sh->dev[i].flags);
			set_bit(R5_UPTODATE, &sh->dev[i].flags);
		}

//	switch(method) {
//	case RECONSTRUCT_WRITE:
//	case CHECK_PARITY:
//	case UPDATE_PARITY:
		/* Note that unlike RAID-5, the ordering of the disks matters greatly. */
		/* FIX: Is this ordering of drives even remotely optimal? */
		count = 0;
		i = d0_idx;
		do {
			ptrs[count++] = page_address(sh->dev[i].page);
			if (count <= disks-2 && !test_bit(R5_UPTODATE, &sh->dev[i].flags))
				printk("block %d/%d not uptodate on parity calc\n", i,count);
			i = raid6_next_disk(i, disks);
		} while ( i != d0_idx );
//		break;
//	}

	raid6_call.gen_syndrome(disks, STRIPE_SIZE, ptrs);

	switch(method) {
	case RECONSTRUCT_WRITE:
		set_bit(R5_UPTODATE, &sh->dev[pd_idx].flags);
		set_bit(R5_UPTODATE, &sh->dev[qd_idx].flags);
		set_bit(R5_LOCKED,   &sh->dev[pd_idx].flags);
		set_bit(R5_LOCKED,   &sh->dev[qd_idx].flags);
		break;
	case UPDATE_PARITY:
		set_bit(R5_UPTODATE, &sh->dev[pd_idx].flags);
		set_bit(R5_UPTODATE, &sh->dev[qd_idx].flags);
		break;
	}
}


/* Compute one missing block */
static void compute_block_1(struct stripe_head *sh, int dd_idx, int nozero)
{
	int i, count, disks = sh->disks;
	void *ptr[MAX_XOR_BLOCKS], *dest, *p;
	int pd_idx = sh->pd_idx;
	int qd_idx = raid6_next_disk(pd_idx, disks);

	pr_debug("compute_block_1, stripe %llu, idx %d\n",
		(unsigned long long)sh->sector, dd_idx);

	if ( dd_idx == qd_idx ) {
		/* We're actually computing the Q drive */
		compute_parity6(sh, UPDATE_PARITY);
	} else {
		dest = page_address(sh->dev[dd_idx].page);
		if (!nozero) memset(dest, 0, STRIPE_SIZE);
		count = 0;
		for (i = disks ; i--; ) {
			if (i == dd_idx || i == qd_idx)
				continue;
			p = page_address(sh->dev[i].page);
			if (test_bit(R5_UPTODATE, &sh->dev[i].flags))
				ptr[count++] = p;
			else
				printk("compute_block() %d, stripe %llu, %d"
				       " not present\n", dd_idx,
				       (unsigned long long)sh->sector, i);

			check_xor();
		}
		if (count)
			xor_blocks(count, STRIPE_SIZE, dest, ptr);
		if (!nozero) set_bit(R5_UPTODATE, &sh->dev[dd_idx].flags);
		else clear_bit(R5_UPTODATE, &sh->dev[dd_idx].flags);
	}
}

/* Compute two missing blocks */
static void compute_block_2(struct stripe_head *sh, int dd_idx1, int dd_idx2)
{
	int i, count, disks = sh->disks;
	int pd_idx = sh->pd_idx;
	int qd_idx = raid6_next_disk(pd_idx, disks);
	int d0_idx = raid6_next_disk(qd_idx, disks);
	int faila, failb;

	/* faila and failb are disk numbers relative to d0_idx */
	/* pd_idx become disks-2 and qd_idx become disks-1 */
	faila = (dd_idx1 < d0_idx) ? dd_idx1+(disks-d0_idx) : dd_idx1-d0_idx;
	failb = (dd_idx2 < d0_idx) ? dd_idx2+(disks-d0_idx) : dd_idx2-d0_idx;

	BUG_ON(faila == failb);
	if ( failb < faila ) { int tmp = faila; faila = failb; failb = tmp; }

	pr_debug("compute_block_2, stripe %llu, idx %d,%d (%d,%d)\n",
	       (unsigned long long)sh->sector, dd_idx1, dd_idx2, faila, failb);

	if ( failb == disks-1 ) {
		/* Q disk is one of the missing disks */
		if ( faila == disks-2 ) {
			/* Missing P+Q, just recompute */
			compute_parity6(sh, UPDATE_PARITY);
			return;
		} else {
			/* We're missing D+Q; recompute D from P */
			compute_block_1(sh, (dd_idx1 == qd_idx) ? dd_idx2 : dd_idx1, 0);
			compute_parity6(sh, UPDATE_PARITY); /* Is this necessary? */
			return;
		}
	}

	/* We're missing D+P or D+D; build pointer table */
	{
		/**** FIX THIS: This could be very bad if disks is close to 256 ****/
		void *ptrs[disks];

		count = 0;
		i = d0_idx;
		do {
			ptrs[count++] = page_address(sh->dev[i].page);
			i = raid6_next_disk(i, disks);
			if (i != dd_idx1 && i != dd_idx2 &&
			    !test_bit(R5_UPTODATE, &sh->dev[i].flags))
				printk("compute_2 with missing block %d/%d\n", count, i);
		} while ( i != d0_idx );

		if ( failb == disks-2 ) {
			/* We're missing D+P. */
			raid6_datap_recov(disks, STRIPE_SIZE, faila, ptrs);
		} else {
			/* We're missing D+D. */
			raid6_2data_recov(disks, STRIPE_SIZE, faila, failb, ptrs);
		}

		/* Both the above update both missing blocks */
		set_bit(R5_UPTODATE, &sh->dev[dd_idx1].flags);
		set_bit(R5_UPTODATE, &sh->dev[dd_idx2].flags);
	}
}

static void
schedule_reconstruction5(struct stripe_head *sh, struct stripe_head_state *s,
			 int rcw, int expand)
{
	int i, pd_idx = sh->pd_idx, disks = sh->disks;

	if (rcw) {
		/* if we are not expanding this is a proper write request, and
		 * there will be bios with new data to be drained into the
		 * stripe cache
		 */
		if (!expand) {
			sh->reconstruct_state = reconstruct_state_drain_run;
			set_bit(STRIPE_OP_BIODRAIN, &s->ops_request);
		} else
			sh->reconstruct_state = reconstruct_state_run;

		set_bit(STRIPE_OP_POSTXOR, &s->ops_request);
static void
schedule_reconstruction(struct stripe_head *sh, struct stripe_head_state *s,
			 int rcw, int expand)
{
	int i, pd_idx = sh->pd_idx, qd_idx = sh->qd_idx, disks = sh->disks;
	struct r5conf *conf = sh->raid_conf;
	int level = conf->level;

	if (rcw) {

		for (i = disks; i--; ) {
			struct r5dev *dev = &sh->dev[i];

			if (dev->towrite) {
				set_bit(R5_LOCKED, &dev->flags);
				set_bit(R5_Wantdrain, &dev->flags);
				if (!expand)
					clear_bit(R5_UPTODATE, &dev->flags);
				s->locked++;
			}
		}
		if (s->locked + 1 == disks)
			if (!test_and_set_bit(STRIPE_FULL_WRITE, &sh->state))
				atomic_inc(&sh->raid_conf->pending_full_writes);
	} else {
		BUG_ON(!(test_bit(R5_UPTODATE, &sh->dev[pd_idx].flags) ||
			test_bit(R5_Wantcompute, &sh->dev[pd_idx].flags)));

		sh->reconstruct_state = reconstruct_state_prexor_drain_run;
		set_bit(STRIPE_OP_PREXOR, &s->ops_request);
		set_bit(STRIPE_OP_BIODRAIN, &s->ops_request);
		set_bit(STRIPE_OP_POSTXOR, &s->ops_request);

		for (i = disks; i--; ) {
			struct r5dev *dev = &sh->dev[i];
			if (i == pd_idx)
		/* if we are not expanding this is a proper write request, and
		 * there will be bios with new data to be drained into the
		 * stripe cache
		 */
		if (!expand) {
			if (!s->locked)
				/* False alarm, nothing to do */
				return;
			sh->reconstruct_state = reconstruct_state_drain_run;
			set_bit(STRIPE_OP_BIODRAIN, &s->ops_request);
		} else
			sh->reconstruct_state = reconstruct_state_run;

		set_bit(STRIPE_OP_RECONSTRUCT, &s->ops_request);

		if (s->locked + conf->max_degraded == disks)
			if (!test_and_set_bit(STRIPE_FULL_WRITE, &sh->state))
				atomic_inc(&conf->pending_full_writes);
	} else {
		BUG_ON(!(test_bit(R5_UPTODATE, &sh->dev[pd_idx].flags) ||
			test_bit(R5_Wantcompute, &sh->dev[pd_idx].flags)));
		BUG_ON(level == 6 &&
			(!(test_bit(R5_UPTODATE, &sh->dev[qd_idx].flags) ||
			   test_bit(R5_Wantcompute, &sh->dev[qd_idx].flags))));

		for (i = disks; i--; ) {
			struct r5dev *dev = &sh->dev[i];
			if (i == pd_idx || i == qd_idx)
				continue;

			if (dev->towrite &&
			    (test_bit(R5_UPTODATE, &dev->flags) ||
			     test_bit(R5_Wantcompute, &dev->flags))) {
				set_bit(R5_Wantdrain, &dev->flags);
				set_bit(R5_LOCKED, &dev->flags);
				clear_bit(R5_UPTODATE, &dev->flags);
				s->locked++;
			}
		}
	}

	/* keep the parity disk locked while asynchronous operations
		if (!s->locked)
			/* False alarm - nothing to do */
			return;
		sh->reconstruct_state = reconstruct_state_prexor_drain_run;
		set_bit(STRIPE_OP_PREXOR, &s->ops_request);
		set_bit(STRIPE_OP_BIODRAIN, &s->ops_request);
		set_bit(STRIPE_OP_RECONSTRUCT, &s->ops_request);
	}

	/* keep the parity disk(s) locked while asynchronous operations
	 * are in flight
	 */
	set_bit(R5_LOCKED, &sh->dev[pd_idx].flags);
	clear_bit(R5_UPTODATE, &sh->dev[pd_idx].flags);
	s->locked++;

	if (level == 6) {
		int qd_idx = sh->qd_idx;
		struct r5dev *dev = &sh->dev[qd_idx];

		set_bit(R5_LOCKED, &dev->flags);
		clear_bit(R5_UPTODATE, &dev->flags);
		s->locked++;
	}

	pr_debug("%s: stripe %llu locked: %d ops_request: %lx\n",
		__func__, (unsigned long long)sh->sector,
		s->locked, s->ops_request);
}

/*
 * Each stripe/dev can have one or more bion attached.
 * toread/towrite point to the first in a chain.
 * The bi_next chain must be in order.
 */
static int add_stripe_bio(struct stripe_head *sh, struct bio *bi, int dd_idx, int forwrite)
{
	struct bio **bip;
	raid5_conf_t *conf = sh->raid_conf;
	int firstwrite=0;

	pr_debug("adding bh b#%llu to stripe s#%llu\n",
		(unsigned long long)bi->bi_sector,
		(unsigned long long)sh->sector);


	spin_lock(&sh->lock);
	spin_lock_irq(&conf->device_lock);
	if (forwrite) {
		bip = &sh->dev[dd_idx].towrite;
		if (*bip == NULL && sh->dev[dd_idx].written == NULL)
			firstwrite = 1;
	} else
		bip = &sh->dev[dd_idx].toread;
	while (*bip && (*bip)->bi_sector < bi->bi_sector) {
		if ((*bip)->bi_sector + ((*bip)->bi_size >> 9) > bi->bi_sector)
			goto overlap;
		bip = & (*bip)->bi_next;
	}
	if (*bip && (*bip)->bi_sector < bi->bi_sector + ((bi->bi_size)>>9))
		goto overlap;

static int add_stripe_bio(struct stripe_head *sh, struct bio *bi, int dd_idx,
			  int forwrite, int previous)
{
	struct bio **bip;
	struct r5conf *conf = sh->raid_conf;
	int firstwrite=0;

	pr_debug("adding bi b#%llu to stripe s#%llu\n",
		(unsigned long long)bi->bi_iter.bi_sector,
		(unsigned long long)sh->sector);

	/*
	 * If several bio share a stripe. The bio bi_phys_segments acts as a
	 * reference count to avoid race. The reference count should already be
	 * increased before this function is called (for example, in
	 * make_request()), so other bio sharing this stripe will not free the
	 * stripe. If a stripe is owned by one stripe, the stripe lock will
	 * protect it.
	 */
	spin_lock_irq(&sh->stripe_lock);
	/* Don't allow new IO added to stripes in batch list */
	if (sh->batch_head)
		goto overlap;
	if (forwrite) {
		bip = &sh->dev[dd_idx].towrite;
		if (*bip == NULL)
			firstwrite = 1;
	} else
		bip = &sh->dev[dd_idx].toread;
	while (*bip && (*bip)->bi_iter.bi_sector < bi->bi_iter.bi_sector) {
		if (bio_end_sector(*bip) > bi->bi_iter.bi_sector)
			goto overlap;
		bip = & (*bip)->bi_next;
	}
	if (*bip && (*bip)->bi_iter.bi_sector < bio_end_sector(bi))
		goto overlap;

	if (!forwrite || previous)
		clear_bit(STRIPE_BATCH_READY, &sh->state);

	BUG_ON(*bip && bi->bi_next && (*bip) != bi->bi_next);
	if (*bip)
		bi->bi_next = *bip;
	*bip = bi;
	bi->bi_phys_segments ++;
	spin_unlock_irq(&conf->device_lock);
	spin_unlock(&sh->lock);

	pr_debug("added bi b#%llu to stripe s#%llu, disk %d.\n",
		(unsigned long long)bi->bi_sector,
		(unsigned long long)sh->sector, dd_idx);

	if (conf->mddev->bitmap && firstwrite) {
		bitmap_startwrite(conf->mddev->bitmap, sh->sector,
				  STRIPE_SECTORS, 0);
		sh->bm_seq = conf->seq_flush+1;
		set_bit(STRIPE_BIT_DELAY, &sh->state);
	}
	raid5_inc_bi_active_stripes(bi);

	if (forwrite) {
		/* check if page is covered */
		sector_t sector = sh->dev[dd_idx].sector;
		for (bi=sh->dev[dd_idx].towrite;
		     sector < sh->dev[dd_idx].sector + STRIPE_SECTORS &&
			     bi && bi->bi_sector <= sector;
		     bi = r5_next_bio(bi, sh->dev[dd_idx].sector)) {
			if (bi->bi_sector + (bi->bi_size>>9) >= sector)
				sector = bi->bi_sector + (bi->bi_size>>9);
		}
		if (sector >= sh->dev[dd_idx].sector + STRIPE_SECTORS)
			set_bit(R5_OVERWRITE, &sh->dev[dd_idx].flags);
	}
			     bi && bi->bi_iter.bi_sector <= sector;
		     bi = r5_next_bio(bi, sh->dev[dd_idx].sector)) {
			if (bio_end_sector(bi) >= sector)
				sector = bio_end_sector(bi);
		}
		if (sector >= sh->dev[dd_idx].sector + STRIPE_SECTORS)
			if (!test_and_set_bit(R5_OVERWRITE, &sh->dev[dd_idx].flags))
				sh->overwrite_disks++;
	}

	pr_debug("added bi b#%llu to stripe s#%llu, disk %d.\n",
		(unsigned long long)(*bip)->bi_iter.bi_sector,
		(unsigned long long)sh->sector, dd_idx);

	if (conf->mddev->bitmap && firstwrite) {
		/* Cannot hold spinlock over bitmap_startwrite,
		 * but must ensure this isn't added to a batch until
		 * we have added to the bitmap and set bm_seq.
		 * So set STRIPE_BITMAP_PENDING to prevent
		 * batching.
		 * If multiple add_stripe_bio() calls race here they
		 * much all set STRIPE_BITMAP_PENDING.  So only the first one
		 * to complete "bitmap_startwrite" gets to set
		 * STRIPE_BIT_DELAY.  This is important as once a stripe
		 * is added to a batch, STRIPE_BIT_DELAY cannot be changed
		 * any more.
		 */
		set_bit(STRIPE_BITMAP_PENDING, &sh->state);
		spin_unlock_irq(&sh->stripe_lock);
		bitmap_startwrite(conf->mddev->bitmap, sh->sector,
				  STRIPE_SECTORS, 0);
		spin_lock_irq(&sh->stripe_lock);
		clear_bit(STRIPE_BITMAP_PENDING, &sh->state);
		if (!sh->batch_head) {
			sh->bm_seq = conf->seq_flush+1;
			set_bit(STRIPE_BIT_DELAY, &sh->state);
		}
	}
	spin_unlock_irq(&sh->stripe_lock);

	if (stripe_can_batch(sh))
		stripe_add_to_batch_list(conf, sh);
	return 1;

 overlap:
	set_bit(R5_Overlap, &sh->dev[dd_idx].flags);
	spin_unlock_irq(&conf->device_lock);
	spin_unlock(&sh->lock);
	return 0;
}

static void end_reshape(raid5_conf_t *conf);

static int page_is_zero(struct page *p)
{
	char *a = page_address(p);
	return ((*(u32*)a) == 0 &&
		memcmp(a, a+4, STRIPE_SIZE-4)==0);
}

static int stripe_to_pdidx(sector_t stripe, raid5_conf_t *conf, int disks)
{
	int sectors_per_chunk = conf->chunk_size >> 9;
	int pd_idx, dd_idx;
	int chunk_offset = sector_div(stripe, sectors_per_chunk);

	raid5_compute_sector(stripe * (disks - conf->max_degraded)
			     *sectors_per_chunk + chunk_offset,
			     disks, disks - conf->max_degraded,
			     &dd_idx, &pd_idx, conf);
	return pd_idx;
}

static void
handle_failed_stripe(raid5_conf_t *conf, struct stripe_head *sh,
				struct stripe_head_state *s, int disks,
				struct bio **return_bi)
{
	int i;
	spin_unlock_irq(&sh->stripe_lock);
	return 0;
}

static void end_reshape(struct r5conf *conf);

static void stripe_set_idx(sector_t stripe, struct r5conf *conf, int previous,
			    struct stripe_head *sh)
{
	int sectors_per_chunk =
		previous ? conf->prev_chunk_sectors : conf->chunk_sectors;
	int dd_idx;
	int chunk_offset = sector_div(stripe, sectors_per_chunk);
	int disks = previous ? conf->previous_raid_disks : conf->raid_disks;

	raid5_compute_sector(conf,
			     stripe * (disks - conf->max_degraded)
			     *sectors_per_chunk + chunk_offset,
			     previous,
			     &dd_idx, sh);
}

static void
handle_failed_stripe(struct r5conf *conf, struct stripe_head *sh,
				struct stripe_head_state *s, int disks,
				struct bio_list *return_bi)
{
	int i;
	BUG_ON(sh->batch_head);
	for (i = disks; i--; ) {
		struct bio *bi;
		int bitmap_end = 0;

		if (test_bit(R5_ReadError, &sh->dev[i].flags)) {
			mdk_rdev_t *rdev;
			rcu_read_lock();
			rdev = rcu_dereference(conf->disks[i].rdev);
			if (rdev && test_bit(In_sync, &rdev->flags))
				/* multiple read failures in one stripe */
				md_error(conf->mddev, rdev);
			rcu_read_unlock();
		}
		spin_lock_irq(&conf->device_lock);
		/* fail all writes first */
		bi = sh->dev[i].towrite;
		sh->dev[i].towrite = NULL;
		if (bi) {
			s->to_write--;
			bitmap_end = 1;
		}
			struct md_rdev *rdev;
			rcu_read_lock();
			rdev = rcu_dereference(conf->disks[i].rdev);
			if (rdev && test_bit(In_sync, &rdev->flags))
				atomic_inc(&rdev->nr_pending);
			else
				rdev = NULL;
			rcu_read_unlock();
			if (rdev) {
				if (!rdev_set_badblocks(
					    rdev,
					    sh->sector,
					    STRIPE_SECTORS, 0))
					md_error(conf->mddev, rdev);
				rdev_dec_pending(rdev, conf->mddev);
			}
		}
		spin_lock_irq(&sh->stripe_lock);
		/* fail all writes first */
		bi = sh->dev[i].towrite;
		sh->dev[i].towrite = NULL;
		sh->overwrite_disks = 0;
		spin_unlock_irq(&sh->stripe_lock);
		if (bi)
			bitmap_end = 1;

		r5l_stripe_write_finished(sh);

		if (test_and_clear_bit(R5_Overlap, &sh->dev[i].flags))
			wake_up(&conf->wait_for_overlap);

		while (bi && bi->bi_sector <
			sh->dev[i].sector + STRIPE_SECTORS) {
			struct bio *nextbi = r5_next_bio(bi, sh->dev[i].sector);
			clear_bit(BIO_UPTODATE, &bi->bi_flags);
			if (--bi->bi_phys_segments == 0) {
				md_write_end(conf->mddev);
				bi->bi_next = *return_bi;
				*return_bi = bi;
			}
			bi = nextbi;
		}
		/* and fail all 'written' */
		bi = sh->dev[i].written;
		sh->dev[i].written = NULL;
		if (bi) bitmap_end = 1;
		while (bi && bi->bi_sector <
		       sh->dev[i].sector + STRIPE_SECTORS) {
			struct bio *bi2 = r5_next_bio(bi, sh->dev[i].sector);
			clear_bit(BIO_UPTODATE, &bi->bi_flags);
			if (--bi->bi_phys_segments == 0) {
				md_write_end(conf->mddev);
				bi->bi_next = *return_bi;
				*return_bi = bi;
		while (bi && bi->bi_iter.bi_sector <
			sh->dev[i].sector + STRIPE_SECTORS) {
			struct bio *nextbi = r5_next_bio(bi, sh->dev[i].sector);

			bi->bi_error = -EIO;
			if (!raid5_dec_bi_active_stripes(bi)) {
				md_write_end(conf->mddev);
				bio_list_add(return_bi, bi);
			}
			bi = nextbi;
		}
		if (bitmap_end)
			bitmap_endwrite(conf->mddev->bitmap, sh->sector,
				STRIPE_SECTORS, 0, 0);
		bitmap_end = 0;
		/* and fail all 'written' */
		bi = sh->dev[i].written;
		sh->dev[i].written = NULL;
		if (test_and_clear_bit(R5_SkipCopy, &sh->dev[i].flags)) {
			WARN_ON(test_bit(R5_UPTODATE, &sh->dev[i].flags));
			sh->dev[i].page = sh->dev[i].orig_page;
		}

		if (bi) bitmap_end = 1;
		while (bi && bi->bi_iter.bi_sector <
		       sh->dev[i].sector + STRIPE_SECTORS) {
			struct bio *bi2 = r5_next_bio(bi, sh->dev[i].sector);

			bi->bi_error = -EIO;
			if (!raid5_dec_bi_active_stripes(bi)) {
				md_write_end(conf->mddev);
				bio_list_add(return_bi, bi);
			}
			bi = bi2;
		}

		/* fail any reads if this device is non-operational and
		 * the data has not reached the cache yet.
		 */
		if (!test_bit(R5_Wantfill, &sh->dev[i].flags) &&
		    (!test_bit(R5_Insync, &sh->dev[i].flags) ||
		      test_bit(R5_ReadError, &sh->dev[i].flags))) {
			bi = sh->dev[i].toread;
			sh->dev[i].toread = NULL;
			if (test_and_clear_bit(R5_Overlap, &sh->dev[i].flags))
				wake_up(&conf->wait_for_overlap);
			if (bi) s->to_read--;
			while (bi && bi->bi_sector <
			       sh->dev[i].sector + STRIPE_SECTORS) {
				struct bio *nextbi =
					r5_next_bio(bi, sh->dev[i].sector);
				clear_bit(BIO_UPTODATE, &bi->bi_flags);
				if (--bi->bi_phys_segments == 0) {
					bi->bi_next = *return_bi;
					*return_bi = bi;
				}
				bi = nextbi;
			}
		}
		spin_unlock_irq(&conf->device_lock);
		if (bitmap_end)
			bitmap_endwrite(conf->mddev->bitmap, sh->sector,
					STRIPE_SECTORS, 0, 0);
	}
		    s->failed > conf->max_degraded &&
		    (!test_bit(R5_Insync, &sh->dev[i].flags) ||
		      test_bit(R5_ReadError, &sh->dev[i].flags))) {
			spin_lock_irq(&sh->stripe_lock);
			bi = sh->dev[i].toread;
			sh->dev[i].toread = NULL;
			spin_unlock_irq(&sh->stripe_lock);
			if (test_and_clear_bit(R5_Overlap, &sh->dev[i].flags))
				wake_up(&conf->wait_for_overlap);
			if (bi)
				s->to_read--;
			while (bi && bi->bi_iter.bi_sector <
			       sh->dev[i].sector + STRIPE_SECTORS) {
				struct bio *nextbi =
					r5_next_bio(bi, sh->dev[i].sector);

				bi->bi_error = -EIO;
				if (!raid5_dec_bi_active_stripes(bi))
					bio_list_add(return_bi, bi);
				bi = nextbi;
			}
		}
		if (bitmap_end)
			bitmap_endwrite(conf->mddev->bitmap, sh->sector,
					STRIPE_SECTORS, 0, 0);
		/* If we were in the middle of a write the parity block might
		 * still be locked - so just clear all R5_LOCKED flags
		 */
		clear_bit(R5_LOCKED, &sh->dev[i].flags);
	}
	s->to_write = 0;
	s->written = 0;

	if (test_and_clear_bit(STRIPE_FULL_WRITE, &sh->state))
		if (atomic_dec_and_test(&conf->pending_full_writes))
			md_wakeup_thread(conf->mddev->thread);
}

/* fetch_block5 - checks the given member device to see if its data needs
 * to be read or computed to satisfy a request.
 *
 * Returns 1 when no more member devices need to be checked, otherwise returns
 * 0 to tell the loop in handle_stripe_fill5 to continue
 */
static int fetch_block5(struct stripe_head *sh, struct stripe_head_state *s,
			int disk_idx, int disks)
{
	struct r5dev *dev = &sh->dev[disk_idx];
	struct r5dev *failed_dev = &sh->dev[s->failed_num];

	/* is the data in this block needed, and can we get it? */
	if (!test_bit(R5_LOCKED, &dev->flags) &&
	    !test_bit(R5_UPTODATE, &dev->flags) &&
	    (dev->toread ||
	     (dev->towrite && !test_bit(R5_OVERWRITE, &dev->flags)) ||
	     s->syncing || s->expanding ||
	     (s->failed &&
	      (failed_dev->toread ||
	       (failed_dev->towrite &&
		!test_bit(R5_OVERWRITE, &failed_dev->flags)))))) {
		/* We would like to get this block, possibly by computing it,
		 * otherwise read it if the backing disk is insync
		 */
		if ((s->uptodate == disks - 1) &&
		    (s->failed && disk_idx == s->failed_num)) {
static void
handle_failed_sync(struct r5conf *conf, struct stripe_head *sh,
		   struct stripe_head_state *s)
{
	int abort = 0;
	int i;

	BUG_ON(sh->batch_head);
	clear_bit(STRIPE_SYNCING, &sh->state);
	if (test_and_clear_bit(R5_Overlap, &sh->dev[sh->pd_idx].flags))
		wake_up(&conf->wait_for_overlap);
	s->syncing = 0;
	s->replacing = 0;
	/* There is nothing more to do for sync/check/repair.
	 * Don't even need to abort as that is handled elsewhere
	 * if needed, and not always wanted e.g. if there is a known
	 * bad block here.
	 * For recover/replace we need to record a bad block on all
	 * non-sync devices, or abort the recovery
	 */
	if (test_bit(MD_RECOVERY_RECOVER, &conf->mddev->recovery)) {
		/* During recovery devices cannot be removed, so
		 * locking and refcounting of rdevs is not needed
		 */
		for (i = 0; i < conf->raid_disks; i++) {
			struct md_rdev *rdev = conf->disks[i].rdev;
			if (rdev
			    && !test_bit(Faulty, &rdev->flags)
			    && !test_bit(In_sync, &rdev->flags)
			    && !rdev_set_badblocks(rdev, sh->sector,
						   STRIPE_SECTORS, 0))
				abort = 1;
			rdev = conf->disks[i].replacement;
			if (rdev
			    && !test_bit(Faulty, &rdev->flags)
			    && !test_bit(In_sync, &rdev->flags)
			    && !rdev_set_badblocks(rdev, sh->sector,
						   STRIPE_SECTORS, 0))
				abort = 1;
		}
		if (abort)
			conf->recovery_disabled =
				conf->mddev->recovery_disabled;
	}
	md_done_sync(conf->mddev, STRIPE_SECTORS, !abort);
}

static int want_replace(struct stripe_head *sh, int disk_idx)
{
	struct md_rdev *rdev;
	int rv = 0;
	/* Doing recovery so rcu locking not required */
	rdev = sh->raid_conf->disks[disk_idx].replacement;
	if (rdev
	    && !test_bit(Faulty, &rdev->flags)
	    && !test_bit(In_sync, &rdev->flags)
	    && (rdev->recovery_offset <= sh->sector
		|| rdev->mddev->recovery_cp <= sh->sector))
		rv = 1;

	return rv;
}

/* fetch_block - checks the given member device to see if its data needs
 * to be read or computed to satisfy a request.
 *
 * Returns 1 when no more member devices need to be checked, otherwise returns
 * 0 to tell the loop in handle_stripe_fill to continue
 */

static int need_this_block(struct stripe_head *sh, struct stripe_head_state *s,
			   int disk_idx, int disks)
{
	struct r5dev *dev = &sh->dev[disk_idx];
	struct r5dev *fdev[2] = { &sh->dev[s->failed_num[0]],
				  &sh->dev[s->failed_num[1]] };
	int i;


	if (test_bit(R5_LOCKED, &dev->flags) ||
	    test_bit(R5_UPTODATE, &dev->flags))
		/* No point reading this as we already have it or have
		 * decided to get it.
		 */
		return 0;

	if (dev->toread ||
	    (dev->towrite && !test_bit(R5_OVERWRITE, &dev->flags)))
		/* We need this block to directly satisfy a request */
		return 1;

	if (s->syncing || s->expanding ||
	    (s->replacing && want_replace(sh, disk_idx)))
		/* When syncing, or expanding we read everything.
		 * When replacing, we need the replaced block.
		 */
		return 1;

	if ((s->failed >= 1 && fdev[0]->toread) ||
	    (s->failed >= 2 && fdev[1]->toread))
		/* If we want to read from a failed device, then
		 * we need to actually read every other device.
		 */
		return 1;

	/* Sometimes neither read-modify-write nor reconstruct-write
	 * cycles can work.  In those cases we read every block we
	 * can.  Then the parity-update is certain to have enough to
	 * work with.
	 * This can only be a problem when we need to write something,
	 * and some device has failed.  If either of those tests
	 * fail we need look no further.
	 */
	if (!s->failed || !s->to_write)
		return 0;

	if (test_bit(R5_Insync, &dev->flags) &&
	    !test_bit(STRIPE_PREREAD_ACTIVE, &sh->state))
		/* Pre-reads at not permitted until after short delay
		 * to gather multiple requests.  However if this
		 * device is no Insync, the block could only be be computed
		 * and there is no need to delay that.
		 */
		return 0;

	for (i = 0; i < s->failed && i < 2; i++) {
		if (fdev[i]->towrite &&
		    !test_bit(R5_UPTODATE, &fdev[i]->flags) &&
		    !test_bit(R5_OVERWRITE, &fdev[i]->flags))
			/* If we have a partial write to a failed
			 * device, then we will need to reconstruct
			 * the content of that device, so all other
			 * devices must be read.
			 */
			return 1;
	}

	/* If we are forced to do a reconstruct-write, either because
	 * the current RAID6 implementation only supports that, or
	 * or because parity cannot be trusted and we are currently
	 * recovering it, there is extra need to be careful.
	 * If one of the devices that we would need to read, because
	 * it is not being overwritten (and maybe not written at all)
	 * is missing/faulty, then we need to read everything we can.
	 */
	if (sh->raid_conf->level != 6 &&
	    sh->raid_conf->rmw_level != PARITY_DISABLE_RMW &&
	    sh->sector < sh->raid_conf->mddev->recovery_cp)
		/* reconstruct-write isn't being forced */
		return 0;
	for (i = 0; i < s->failed && i < 2; i++) {
		if (s->failed_num[i] != sh->pd_idx &&
		    s->failed_num[i] != sh->qd_idx &&
		    !test_bit(R5_UPTODATE, &fdev[i]->flags) &&
		    !test_bit(R5_OVERWRITE, &fdev[i]->flags))
			return 1;
	}

	return 0;
}

static int fetch_block(struct stripe_head *sh, struct stripe_head_state *s,
		       int disk_idx, int disks)
{
	struct r5dev *dev = &sh->dev[disk_idx];

	/* is the data in this block needed, and can we get it? */
	if (need_this_block(sh, s, disk_idx, disks)) {
		/* we would like to get this block, possibly by computing it,
		 * otherwise read it if the backing disk is insync
		 */
		BUG_ON(test_bit(R5_Wantcompute, &dev->flags));
		BUG_ON(test_bit(R5_Wantread, &dev->flags));
		BUG_ON(sh->batch_head);

		/*
		 * In the raid6 case if the only non-uptodate disk is P
		 * then we already trusted P to compute the other failed
		 * drives. It is safe to compute rather than re-read P.
		 * In other cases we only compute blocks from failed
		 * devices, otherwise check/repair might fail to detect
		 * a real inconsistency.
		 */

		if ((s->uptodate == disks - 1) &&
		    ((sh->qd_idx >= 0 && sh->pd_idx == disk_idx) ||
		    (s->failed && (disk_idx == s->failed_num[0] ||
				   disk_idx == s->failed_num[1])))) {
			/* have disk failed, and we're requested to fetch it;
			 * do compute it
			 */
			pr_debug("Computing stripe %llu block %d\n",
			       (unsigned long long)sh->sector, disk_idx);
			set_bit(STRIPE_COMPUTE_RUN, &sh->state);
			set_bit(STRIPE_OP_COMPUTE_BLK, &s->ops_request);
			set_bit(R5_Wantcompute, &dev->flags);
			sh->ops.target = disk_idx;
			s->req_compute = 1;
			/* Careful: from this point on 'uptodate' is in the eye
			 * of raid5_run_ops which services 'compute' operations
			sh->ops.target2 = -1; /* no 2nd target */
			s->req_compute = 1;
			/* Careful: from this point on 'uptodate' is in the eye
			 * of raid_run_ops which services 'compute' operations
			 * before writes. R5_Wantcompute flags a block that will
			 * be R5_UPTODATE by the time it is needed for a
			 * subsequent operation.
			 */
			s->uptodate++;
			return 1; /* uptodate + compute == disks */
			return 1;
		} else if (s->uptodate == disks-2 && s->failed >= 2) {
			/* Computing 2-failure is *very* expensive; only
			 * do it if failed >= 2
			 */
			int other;
			for (other = disks; other--; ) {
				if (other == disk_idx)
					continue;
				if (!test_bit(R5_UPTODATE,
				      &sh->dev[other].flags))
					break;
			}
			BUG_ON(other < 0);
			pr_debug("Computing stripe %llu blocks %d,%d\n",
			       (unsigned long long)sh->sector,
			       disk_idx, other);
			set_bit(STRIPE_COMPUTE_RUN, &sh->state);
			set_bit(STRIPE_OP_COMPUTE_BLK, &s->ops_request);
			set_bit(R5_Wantcompute, &sh->dev[disk_idx].flags);
			set_bit(R5_Wantcompute, &sh->dev[other].flags);
			sh->ops.target = disk_idx;
			sh->ops.target2 = other;
			s->uptodate += 2;
			s->req_compute = 1;
			return 1;
		} else if (test_bit(R5_Insync, &dev->flags)) {
			set_bit(R5_LOCKED, &dev->flags);
			set_bit(R5_Wantread, &dev->flags);
			s->locked++;
			pr_debug("Reading block %d (sync=%d)\n", disk_idx,
				s->syncing);
			pr_debug("Reading block %d (sync=%d)\n",
				disk_idx, s->syncing);
		}
	}

	return 0;
}

/**
 * handle_stripe_fill5 - read or compute data to satisfy pending requests.
 */
static void handle_stripe_fill5(struct stripe_head *sh,
			struct stripe_head_state *s, int disks)
 * handle_stripe_fill - read or compute data to satisfy pending requests.
 */
static void handle_stripe_fill(struct stripe_head *sh,
			       struct stripe_head_state *s,
			       int disks)
{
	int i;

	/* look for blocks to read/compute, skip this if a compute
	 * is already in flight, or if the stripe contents are in the
	 * midst of changing due to a write
	 */
	if (!test_bit(STRIPE_COMPUTE_RUN, &sh->state) && !sh->check_state &&
	    !sh->reconstruct_state)
		for (i = disks; i--; )
			if (fetch_block5(sh, s, i, disks))
			if (fetch_block(sh, s, i, disks))
				break;
	set_bit(STRIPE_HANDLE, &sh->state);
}

static void handle_stripe_fill6(struct stripe_head *sh,
			struct stripe_head_state *s, struct r6_state *r6s,
			int disks)
{
	int i;
	for (i = disks; i--; ) {
		struct r5dev *dev = &sh->dev[i];
		if (!test_bit(R5_LOCKED, &dev->flags) &&
		    !test_bit(R5_UPTODATE, &dev->flags) &&
		    (dev->toread || (dev->towrite &&
		     !test_bit(R5_OVERWRITE, &dev->flags)) ||
		     s->syncing || s->expanding ||
		     (s->failed >= 1 &&
		      (sh->dev[r6s->failed_num[0]].toread ||
		       s->to_write)) ||
		     (s->failed >= 2 &&
		      (sh->dev[r6s->failed_num[1]].toread ||
		       s->to_write)))) {
			/* we would like to get this block, possibly
			 * by computing it, but we might not be able to
			 */
			if ((s->uptodate == disks - 1) &&
			    (s->failed && (i == r6s->failed_num[0] ||
					   i == r6s->failed_num[1]))) {
				pr_debug("Computing stripe %llu block %d\n",
				       (unsigned long long)sh->sector, i);
				compute_block_1(sh, i, 0);
				s->uptodate++;
			} else if ( s->uptodate == disks-2 && s->failed >= 2 ) {
				/* Computing 2-failure is *very* expensive; only
				 * do it if failed >= 2
				 */
				int other;
				for (other = disks; other--; ) {
					if (other == i)
						continue;
					if (!test_bit(R5_UPTODATE,
					      &sh->dev[other].flags))
						break;
				}
				BUG_ON(other < 0);
				pr_debug("Computing stripe %llu blocks %d,%d\n",
				       (unsigned long long)sh->sector,
				       i, other);
				compute_block_2(sh, i, other);
				s->uptodate += 2;
			} else if (test_bit(R5_Insync, &dev->flags)) {
				set_bit(R5_LOCKED, &dev->flags);
				set_bit(R5_Wantread, &dev->flags);
				s->locked++;
				pr_debug("Reading block %d (sync=%d)\n",
					i, s->syncing);
			}
		}
	}
	set_bit(STRIPE_HANDLE, &sh->state);
}


static void break_stripe_batch_list(struct stripe_head *head_sh,
				    unsigned long handle_flags);
/* handle_stripe_clean_event
 * any written block on an uptodate or failed drive can be returned.
 * Note that if we 'wrote' to a failed drive, it will be UPTODATE, but
 * never LOCKED, so we don't need to test 'failed' directly.
 */
static void handle_stripe_clean_event(raid5_conf_t *conf,
	struct stripe_head *sh, int disks, struct bio **return_bi)
{
	int i;
	struct r5dev *dev;
static void handle_stripe_clean_event(struct r5conf *conf,
	struct stripe_head *sh, int disks, struct bio_list *return_bi)
{
	int i;
	struct r5dev *dev;
	int discard_pending = 0;
	struct stripe_head *head_sh = sh;
	bool do_endio = false;

	for (i = disks; i--; )
		if (sh->dev[i].written) {
			dev = &sh->dev[i];
			if (!test_bit(R5_LOCKED, &dev->flags) &&
				test_bit(R5_UPTODATE, &dev->flags)) {
				/* We can return any write requests */
				struct bio *wbi, *wbi2;
				int bitmap_end = 0;
				pr_debug("Return write for disc %d\n", i);
				spin_lock_irq(&conf->device_lock);
				wbi = dev->written;
				dev->written = NULL;
				while (wbi && wbi->bi_sector <
					dev->sector + STRIPE_SECTORS) {
					wbi2 = r5_next_bio(wbi, dev->sector);
					if (--wbi->bi_phys_segments == 0) {
						md_write_end(conf->mddev);
						wbi->bi_next = *return_bi;
						*return_bi = wbi;
					}
					wbi = wbi2;
				}
				if (dev->towrite == NULL)
					bitmap_end = 1;
				spin_unlock_irq(&conf->device_lock);
				if (bitmap_end)
					bitmap_endwrite(conf->mddev->bitmap,
							sh->sector,
							STRIPE_SECTORS,
					 !test_bit(STRIPE_DEGRADED, &sh->state),
							0);
			}
		}

	if (test_and_clear_bit(STRIPE_FULL_WRITE, &sh->state))
		if (atomic_dec_and_test(&conf->pending_full_writes))
			md_wakeup_thread(conf->mddev->thread);
}

static void handle_stripe_dirtying5(raid5_conf_t *conf,
		struct stripe_head *sh,	struct stripe_head_state *s, int disks)
{
	int rmw = 0, rcw = 0, i;
	for (i = disks; i--; ) {
		/* would I have to read this buffer for read_modify_write */
		struct r5dev *dev = &sh->dev[i];
		if ((dev->towrite || i == sh->pd_idx) &&
			    (test_bit(R5_UPTODATE, &dev->flags) ||
			     test_bit(R5_Discard, &dev->flags) ||
			     test_bit(R5_SkipCopy, &dev->flags))) {
				/* We can return any write requests */
				struct bio *wbi, *wbi2;
				pr_debug("Return write for disc %d\n", i);
				if (test_and_clear_bit(R5_Discard, &dev->flags))
					clear_bit(R5_UPTODATE, &dev->flags);
				if (test_and_clear_bit(R5_SkipCopy, &dev->flags)) {
					WARN_ON(test_bit(R5_UPTODATE, &dev->flags));
				}
				do_endio = true;

returnbi:
				dev->page = dev->orig_page;
				wbi = dev->written;
				dev->written = NULL;
				while (wbi && wbi->bi_iter.bi_sector <
					dev->sector + STRIPE_SECTORS) {
					wbi2 = r5_next_bio(wbi, dev->sector);
					if (!raid5_dec_bi_active_stripes(wbi)) {
						md_write_end(conf->mddev);
						bio_list_add(return_bi, wbi);
					}
					wbi = wbi2;
				}
				bitmap_endwrite(conf->mddev->bitmap, sh->sector,
						STRIPE_SECTORS,
					 !test_bit(STRIPE_DEGRADED, &sh->state),
						0);
				if (head_sh->batch_head) {
					sh = list_first_entry(&sh->batch_list,
							      struct stripe_head,
							      batch_list);
					if (sh != head_sh) {
						dev = &sh->dev[i];
						goto returnbi;
					}
				}
				sh = head_sh;
				dev = &sh->dev[i];
			} else if (test_bit(R5_Discard, &dev->flags))
				discard_pending = 1;
			WARN_ON(test_bit(R5_SkipCopy, &dev->flags));
			WARN_ON(dev->page != dev->orig_page);
		}

	r5l_stripe_write_finished(sh);

	if (!discard_pending &&
	    test_bit(R5_Discard, &sh->dev[sh->pd_idx].flags)) {
		int hash;
		clear_bit(R5_Discard, &sh->dev[sh->pd_idx].flags);
		clear_bit(R5_UPTODATE, &sh->dev[sh->pd_idx].flags);
		if (sh->qd_idx >= 0) {
			clear_bit(R5_Discard, &sh->dev[sh->qd_idx].flags);
			clear_bit(R5_UPTODATE, &sh->dev[sh->qd_idx].flags);
		}
		/* now that discard is done we can proceed with any sync */
		clear_bit(STRIPE_DISCARD, &sh->state);
		/*
		 * SCSI discard will change some bio fields and the stripe has
		 * no updated data, so remove it from hash list and the stripe
		 * will be reinitialized
		 */
unhash:
		hash = sh->hash_lock_index;
		spin_lock_irq(conf->hash_locks + hash);
		remove_hash(sh);
		spin_unlock_irq(conf->hash_locks + hash);
		if (head_sh->batch_head) {
			sh = list_first_entry(&sh->batch_list,
					      struct stripe_head, batch_list);
			if (sh != head_sh)
					goto unhash;
		}
		sh = head_sh;

		if (test_bit(STRIPE_SYNC_REQUESTED, &sh->state))
			set_bit(STRIPE_HANDLE, &sh->state);

	}

	if (test_and_clear_bit(STRIPE_FULL_WRITE, &sh->state))
		if (atomic_dec_and_test(&conf->pending_full_writes))
			md_wakeup_thread(conf->mddev->thread);

	if (head_sh->batch_head && do_endio)
		break_stripe_batch_list(head_sh, STRIPE_EXPAND_SYNC_FLAGS);
}

static void handle_stripe_dirtying(struct r5conf *conf,
				   struct stripe_head *sh,
				   struct stripe_head_state *s,
				   int disks)
{
	int rmw = 0, rcw = 0, i;
	sector_t recovery_cp = conf->mddev->recovery_cp;

	/* Check whether resync is now happening or should start.
	 * If yes, then the array is dirty (after unclean shutdown or
	 * initial creation), so parity in some stripes might be inconsistent.
	 * In this case, we need to always do reconstruct-write, to ensure
	 * that in case of drive failure or read-error correction, we
	 * generate correct data from the parity.
	 */
	if (conf->rmw_level == PARITY_DISABLE_RMW ||
	    (recovery_cp < MaxSector && sh->sector >= recovery_cp &&
	     s->failed == 0)) {
		/* Calculate the real rcw later - for now make it
		 * look like rcw is cheaper
		 */
		rcw = 1; rmw = 2;
		pr_debug("force RCW rmw_level=%u, recovery_cp=%llu sh->sector=%llu\n",
			 conf->rmw_level, (unsigned long long)recovery_cp,
			 (unsigned long long)sh->sector);
	} else for (i = disks; i--; ) {
		/* would I have to read this buffer for read_modify_write */
		struct r5dev *dev = &sh->dev[i];
		if ((dev->towrite || i == sh->pd_idx || i == sh->qd_idx) &&
		    !test_bit(R5_LOCKED, &dev->flags) &&
		    !(test_bit(R5_UPTODATE, &dev->flags) ||
		      test_bit(R5_Wantcompute, &dev->flags))) {
			if (test_bit(R5_Insync, &dev->flags))
				rmw++;
			else
				rmw += 2*disks;  /* cannot read it */
		}
		/* Would I have to read this buffer for reconstruct_write */
		if (!test_bit(R5_OVERWRITE, &dev->flags) && i != sh->pd_idx &&
		    !test_bit(R5_LOCKED, &dev->flags) &&
		    !(test_bit(R5_UPTODATE, &dev->flags) ||
		    test_bit(R5_Wantcompute, &dev->flags))) {
			if (test_bit(R5_Insync, &dev->flags)) rcw++;
		if (!test_bit(R5_OVERWRITE, &dev->flags) &&
		    i != sh->pd_idx && i != sh->qd_idx &&
		    !test_bit(R5_LOCKED, &dev->flags) &&
		    !(test_bit(R5_UPTODATE, &dev->flags) ||
		    test_bit(R5_Wantcompute, &dev->flags))) {
			if (test_bit(R5_Insync, &dev->flags))
				rcw++;
			else
				rcw += 2*disks;
		}
	}
	pr_debug("for sector %llu, rmw=%d rcw=%d\n",
		(unsigned long long)sh->sector, rmw, rcw);
	set_bit(STRIPE_HANDLE, &sh->state);
	if (rmw < rcw && rmw > 0)
		/* prefer read-modify-write, but need to get some data */
		for (i = disks; i--; ) {
			struct r5dev *dev = &sh->dev[i];
			if ((dev->towrite || i == sh->pd_idx) &&
	if ((rmw < rcw || (rmw == rcw && conf->rmw_level == PARITY_ENABLE_RMW)) && rmw > 0) {
		/* prefer read-modify-write, but need to get some data */
		if (conf->mddev->queue)
			blk_add_trace_msg(conf->mddev->queue,
					  "raid5 rmw %llu %d",
					  (unsigned long long)sh->sector, rmw);
		for (i = disks; i--; ) {
			struct r5dev *dev = &sh->dev[i];
			if ((dev->towrite || i == sh->pd_idx || i == sh->qd_idx) &&
			    !test_bit(R5_LOCKED, &dev->flags) &&
			    !(test_bit(R5_UPTODATE, &dev->flags) ||
			    test_bit(R5_Wantcompute, &dev->flags)) &&
			    test_bit(R5_Insync, &dev->flags)) {
				if (
				  test_bit(STRIPE_PREREAD_ACTIVE, &sh->state)) {
					pr_debug("Read_old block "
						"%d for r-m-w\n", i);
				if (test_bit(STRIPE_PREREAD_ACTIVE,
					     &sh->state)) {
					pr_debug("Read_old block %d for r-m-w\n",
						 i);
					set_bit(R5_LOCKED, &dev->flags);
					set_bit(R5_Wantread, &dev->flags);
					s->locked++;
				} else {
					set_bit(STRIPE_DELAYED, &sh->state);
					set_bit(STRIPE_HANDLE, &sh->state);
				}
			}
		}
	if (rcw <= rmw && rcw > 0)
		/* want reconstruct write, but need to get some data */
		for (i = disks; i--; ) {
			struct r5dev *dev = &sh->dev[i];
			if (!test_bit(R5_OVERWRITE, &dev->flags) &&
			    i != sh->pd_idx &&
			    !test_bit(R5_LOCKED, &dev->flags) &&
			    !(test_bit(R5_UPTODATE, &dev->flags) ||
			    test_bit(R5_Wantcompute, &dev->flags)) &&
			    test_bit(R5_Insync, &dev->flags)) {
				if (
				  test_bit(STRIPE_PREREAD_ACTIVE, &sh->state)) {
	}
	if ((rcw < rmw || (rcw == rmw && conf->rmw_level != PARITY_ENABLE_RMW)) && rcw > 0) {
		/* want reconstruct write, but need to get some data */
		int qread =0;
		rcw = 0;
		for (i = disks; i--; ) {
			struct r5dev *dev = &sh->dev[i];
			if (!test_bit(R5_OVERWRITE, &dev->flags) &&
			    i != sh->pd_idx && i != sh->qd_idx &&
			    !test_bit(R5_LOCKED, &dev->flags) &&
			    !(test_bit(R5_UPTODATE, &dev->flags) ||
			      test_bit(R5_Wantcompute, &dev->flags))) {
				rcw++;
				if (test_bit(R5_Insync, &dev->flags) &&
				    test_bit(STRIPE_PREREAD_ACTIVE,
					     &sh->state)) {
					pr_debug("Read_old block "
						"%d for Reconstruct\n", i);
					set_bit(R5_LOCKED, &dev->flags);
					set_bit(R5_Wantread, &dev->flags);
					s->locked++;
					qread++;
				} else {
					set_bit(STRIPE_DELAYED, &sh->state);
					set_bit(STRIPE_HANDLE, &sh->state);
				}
			}
		}
		if (rcw && conf->mddev->queue)
			blk_add_trace_msg(conf->mddev->queue, "raid5 rcw %llu %d %d %d",
					  (unsigned long long)sh->sector,
					  rcw, qread, test_bit(STRIPE_DELAYED, &sh->state));
	}

	if (rcw > disks && rmw > disks &&
	    !test_bit(STRIPE_PREREAD_ACTIVE, &sh->state))
		set_bit(STRIPE_DELAYED, &sh->state);

	/* now if nothing is locked, and if we have enough data,
	 * we can start a write request
	 */
	/* since handle_stripe can be called at any time we need to handle the
	 * case where a compute block operation has been submitted and then a
	 * subsequent call wants to start a write request.  raid5_run_ops only
	 * handles the case where compute block and postxor are requested
	 * subsequent call wants to start a write request.  raid_run_ops only
	 * handles the case where compute block and reconstruct are requested
	 * simultaneously.  If this is not the case then new writes need to be
	 * held off until the compute completes.
	 */
	if ((s->req_compute || !test_bit(STRIPE_COMPUTE_RUN, &sh->state)) &&
	    (s->locked == 0 && (rcw == 0 || rmw == 0) &&
	    !test_bit(STRIPE_BIT_DELAY, &sh->state)))
		schedule_reconstruction5(sh, s, rcw == 0, 0);
}

static void handle_stripe_dirtying6(raid5_conf_t *conf,
		struct stripe_head *sh,	struct stripe_head_state *s,
		struct r6_state *r6s, int disks)
{
	int rcw = 0, must_compute = 0, pd_idx = sh->pd_idx, i;
	int qd_idx = r6s->qd_idx;
	for (i = disks; i--; ) {
		struct r5dev *dev = &sh->dev[i];
		/* Would I have to read this buffer for reconstruct_write */
		if (!test_bit(R5_OVERWRITE, &dev->flags)
		    && i != pd_idx && i != qd_idx
		    && (!test_bit(R5_LOCKED, &dev->flags)
			    ) &&
		    !test_bit(R5_UPTODATE, &dev->flags)) {
			if (test_bit(R5_Insync, &dev->flags)) rcw++;
			else {
				pr_debug("raid6: must_compute: "
					"disk %d flags=%#lx\n", i, dev->flags);
				must_compute++;
			}
		}
	}
	pr_debug("for sector %llu, rcw=%d, must_compute=%d\n",
	       (unsigned long long)sh->sector, rcw, must_compute);
	set_bit(STRIPE_HANDLE, &sh->state);

	if (rcw > 0)
		/* want reconstruct write, but need to get some data */
		for (i = disks; i--; ) {
			struct r5dev *dev = &sh->dev[i];
			if (!test_bit(R5_OVERWRITE, &dev->flags)
			    && !(s->failed == 0 && (i == pd_idx || i == qd_idx))
			    && !test_bit(R5_LOCKED, &dev->flags) &&
			    !test_bit(R5_UPTODATE, &dev->flags) &&
			    test_bit(R5_Insync, &dev->flags)) {
				if (
				  test_bit(STRIPE_PREREAD_ACTIVE, &sh->state)) {
					pr_debug("Read_old stripe %llu "
						"block %d for Reconstruct\n",
					     (unsigned long long)sh->sector, i);
					set_bit(R5_LOCKED, &dev->flags);
					set_bit(R5_Wantread, &dev->flags);
					s->locked++;
				} else {
					pr_debug("Request delayed stripe %llu "
						"block %d for Reconstruct\n",
					     (unsigned long long)sh->sector, i);
					set_bit(STRIPE_DELAYED, &sh->state);
					set_bit(STRIPE_HANDLE, &sh->state);
				}
			}
		}
	/* now if nothing is locked, and if we have enough data, we can start a
	 * write request
	 */
	if (s->locked == 0 && rcw == 0 &&
	    !test_bit(STRIPE_BIT_DELAY, &sh->state)) {
		if (must_compute > 0) {
			/* We have failed blocks and need to compute them */
			switch (s->failed) {
			case 0:
				BUG();
			case 1:
				compute_block_1(sh, r6s->failed_num[0], 0);
				break;
			case 2:
				compute_block_2(sh, r6s->failed_num[0],
						r6s->failed_num[1]);
				break;
			default: /* This request should have been failed? */
				BUG();
			}
		}

		pr_debug("Computing parity for stripe %llu\n",
			(unsigned long long)sh->sector);
		compute_parity6(sh, RECONSTRUCT_WRITE);
		/* now every locked buffer is ready to be written */
		for (i = disks; i--; )
			if (test_bit(R5_LOCKED, &sh->dev[i].flags)) {
				pr_debug("Writing stripe %llu block %d\n",
				       (unsigned long long)sh->sector, i);
				s->locked++;
				set_bit(R5_Wantwrite, &sh->dev[i].flags);
			}
		if (s->locked == disks)
			if (!test_and_set_bit(STRIPE_FULL_WRITE, &sh->state))
				atomic_inc(&conf->pending_full_writes);
		/* after a RECONSTRUCT_WRITE, the stripe MUST be in-sync */
		set_bit(STRIPE_INSYNC, &sh->state);

		if (test_and_clear_bit(STRIPE_PREREAD_ACTIVE, &sh->state)) {
			atomic_dec(&conf->preread_active_stripes);
			if (atomic_read(&conf->preread_active_stripes) <
			    IO_THRESHOLD)
				md_wakeup_thread(conf->mddev->thread);
		}
	}
}

static void handle_parity_checks5(raid5_conf_t *conf, struct stripe_head *sh,
		schedule_reconstruction(sh, s, rcw == 0, 0);
}

static void handle_parity_checks5(struct r5conf *conf, struct stripe_head *sh,
				struct stripe_head_state *s, int disks)
{
	struct r5dev *dev = NULL;

	BUG_ON(sh->batch_head);
	set_bit(STRIPE_HANDLE, &sh->state);

	switch (sh->check_state) {
	case check_state_idle:
		/* start a new check operation if there are no failures */
		if (s->failed == 0) {
			BUG_ON(s->uptodate != disks);
			sh->check_state = check_state_run;
			set_bit(STRIPE_OP_CHECK, &s->ops_request);
			clear_bit(R5_UPTODATE, &sh->dev[sh->pd_idx].flags);
			s->uptodate--;
			break;
		}
		dev = &sh->dev[s->failed_num];
		dev = &sh->dev[s->failed_num[0]];
		/* fall through */
	case check_state_compute_result:
		sh->check_state = check_state_idle;
		if (!dev)
			dev = &sh->dev[sh->pd_idx];

		/* check that a write has not made the stripe insync */
		if (test_bit(STRIPE_INSYNC, &sh->state))
			break;

		/* either failed parity check, or recovery is happening */
		BUG_ON(!test_bit(R5_UPTODATE, &dev->flags));
		BUG_ON(s->uptodate != disks);

		set_bit(R5_LOCKED, &dev->flags);
		s->locked++;
		set_bit(R5_Wantwrite, &dev->flags);

		clear_bit(STRIPE_DEGRADED, &sh->state);
		set_bit(STRIPE_INSYNC, &sh->state);
		break;
	case check_state_run:
		break; /* we will be called again upon completion */
	case check_state_check_result:
		sh->check_state = check_state_idle;

		/* if a failure occurred during the check operation, leave
		 * STRIPE_INSYNC not set and let the stripe be handled again
		 */
		if (s->failed)
			break;

		/* handle a successful check operation, if parity is correct
		 * we are done.  Otherwise update the mismatch count and repair
		 * parity if !MD_RECOVERY_CHECK
		 */
		if (sh->ops.zero_sum_result == 0)
		if ((sh->ops.zero_sum_result & SUM_CHECK_P_RESULT) == 0)
			/* parity is correct (on disc,
			 * not in buffer any more)
			 */
			set_bit(STRIPE_INSYNC, &sh->state);
		else {
			conf->mddev->resync_mismatches += STRIPE_SECTORS;
			atomic64_add(STRIPE_SECTORS, &conf->mddev->resync_mismatches);
			if (test_bit(MD_RECOVERY_CHECK, &conf->mddev->recovery))
				/* don't try to repair!! */
				set_bit(STRIPE_INSYNC, &sh->state);
			else {
				sh->check_state = check_state_compute_run;
				set_bit(STRIPE_COMPUTE_RUN, &sh->state);
				set_bit(STRIPE_OP_COMPUTE_BLK, &s->ops_request);
				set_bit(R5_Wantcompute,
					&sh->dev[sh->pd_idx].flags);
				sh->ops.target = sh->pd_idx;
				sh->ops.target2 = -1;
				s->uptodate++;
			}
		}
		break;
	case check_state_compute_run:
		break;
	default:
		printk(KERN_ERR "%s: unknown check_state: %d sector: %llu\n",
		       __func__, sh->check_state,
		       (unsigned long long) sh->sector);
		BUG();
	}
}


static void handle_parity_checks6(raid5_conf_t *conf, struct stripe_head *sh,
				struct stripe_head_state *s,
				struct r6_state *r6s, struct page *tmp_page,
				int disks)
{
	int update_p = 0, update_q = 0;
	struct r5dev *dev;
	int pd_idx = sh->pd_idx;
	int qd_idx = r6s->qd_idx;

	set_bit(STRIPE_HANDLE, &sh->state);

	BUG_ON(s->failed > 2);
	BUG_ON(s->uptodate < disks);
static void handle_parity_checks6(struct r5conf *conf, struct stripe_head *sh,
				  struct stripe_head_state *s,
				  int disks)
{
	int pd_idx = sh->pd_idx;
	int qd_idx = sh->qd_idx;
	struct r5dev *dev;

	BUG_ON(sh->batch_head);
	set_bit(STRIPE_HANDLE, &sh->state);

	BUG_ON(s->failed > 2);

	/* Want to check and possibly repair P and Q.
	 * However there could be one 'failed' device, in which
	 * case we can only check one of them, possibly using the
	 * other to generate missing data
	 */

	/* If !tmp_page, we cannot do the calculations,
	 * but as we have set STRIPE_HANDLE, we will soon be called
	 * by stripe_handle with a tmp_page - just wait until then.
	 */
	if (tmp_page) {
		if (s->failed == r6s->q_failed) {
			/* The only possible failed device holds 'Q', so it
			 * makes sense to check P (If anything else were failed,
			 * we would have used P to recreate it).
			 */
			compute_block_1(sh, pd_idx, 1);
			if (!page_is_zero(sh->dev[pd_idx].page)) {
				compute_block_1(sh, pd_idx, 0);
				update_p = 1;
			}
		}
		if (!r6s->q_failed && s->failed < 2) {
			/* q is not failed, and we didn't use it to generate
			 * anything, so it makes sense to check it
			 */
			memcpy(page_address(tmp_page),
			       page_address(sh->dev[qd_idx].page),
			       STRIPE_SIZE);
			compute_parity6(sh, UPDATE_PARITY);
			if (memcmp(page_address(tmp_page),
				   page_address(sh->dev[qd_idx].page),
				   STRIPE_SIZE) != 0) {
				clear_bit(STRIPE_INSYNC, &sh->state);
				update_q = 1;
			}
		}
		if (update_p || update_q) {
			conf->mddev->resync_mismatches += STRIPE_SECTORS;
			if (test_bit(MD_RECOVERY_CHECK, &conf->mddev->recovery))
				/* don't try to repair!! */
				update_p = update_q = 0;
		}

		/* now write out any block on a failed drive,
		 * or P or Q if they need it
		 */

		if (s->failed == 2) {
			dev = &sh->dev[r6s->failed_num[1]];
	switch (sh->check_state) {
	case check_state_idle:
		/* start a new check operation if there are < 2 failures */
		if (s->failed == s->q_failed) {
			/* The only possible failed device holds Q, so it
			 * makes sense to check P (If anything else were failed,
			 * we would have used P to recreate it).
			 */
			sh->check_state = check_state_run;
		}
		if (!s->q_failed && s->failed < 2) {
			/* Q is not failed, and we didn't use it to generate
			 * anything, so it makes sense to check it
			 */
			if (sh->check_state == check_state_run)
				sh->check_state = check_state_run_pq;
			else
				sh->check_state = check_state_run_q;
		}

		/* discard potentially stale zero_sum_result */
		sh->ops.zero_sum_result = 0;

		if (sh->check_state == check_state_run) {
			/* async_xor_zero_sum destroys the contents of P */
			clear_bit(R5_UPTODATE, &sh->dev[pd_idx].flags);
			s->uptodate--;
		}
		if (sh->check_state >= check_state_run &&
		    sh->check_state <= check_state_run_pq) {
			/* async_syndrome_zero_sum preserves P and Q, so
			 * no need to mark them !uptodate here
			 */
			set_bit(STRIPE_OP_CHECK, &s->ops_request);
			break;
		}

		/* we have 2-disk failure */
		BUG_ON(s->failed != 2);
		/* fall through */
	case check_state_compute_result:
		sh->check_state = check_state_idle;

		/* check that a write has not made the stripe insync */
		if (test_bit(STRIPE_INSYNC, &sh->state))
			break;

		/* now write out any block on a failed drive,
		 * or P or Q if they were recomputed
		 */
		dev = NULL;
		if (s->failed == 2) {
			dev = &sh->dev[s->failed_num[1]];
			s->locked++;
			set_bit(R5_LOCKED, &dev->flags);
			set_bit(R5_Wantwrite, &dev->flags);
		}
		if (s->failed >= 1) {
			dev = &sh->dev[r6s->failed_num[0]];
			dev = &sh->dev[s->failed_num[0]];
			s->locked++;
			set_bit(R5_LOCKED, &dev->flags);
			set_bit(R5_Wantwrite, &dev->flags);
		}

		if (update_p) {
		if (sh->ops.zero_sum_result & SUM_CHECK_P_RESULT) {
			dev = &sh->dev[pd_idx];
			s->locked++;
			set_bit(R5_LOCKED, &dev->flags);
			set_bit(R5_Wantwrite, &dev->flags);
		}
		if (update_q) {
		if (sh->ops.zero_sum_result & SUM_CHECK_Q_RESULT) {
			dev = &sh->dev[qd_idx];
			s->locked++;
			set_bit(R5_LOCKED, &dev->flags);
			set_bit(R5_Wantwrite, &dev->flags);
		}
		if (WARN_ONCE(dev && !test_bit(R5_UPTODATE, &dev->flags),
			      "%s: disk%td not up to date\n",
			      mdname(conf->mddev),
			      dev - (struct r5dev *) &sh->dev)) {
			clear_bit(R5_LOCKED, &dev->flags);
			clear_bit(R5_Wantwrite, &dev->flags);
			s->locked--;
		}
		clear_bit(STRIPE_DEGRADED, &sh->state);

		set_bit(STRIPE_INSYNC, &sh->state);
	}
}

static void handle_stripe_expansion(raid5_conf_t *conf, struct stripe_head *sh,
				struct r6_state *r6s)
		break;
	case check_state_run:
	case check_state_run_q:
	case check_state_run_pq:
		break; /* we will be called again upon completion */
	case check_state_check_result:
		sh->check_state = check_state_idle;

		/* handle a successful check operation, if parity is correct
		 * we are done.  Otherwise update the mismatch count and repair
		 * parity if !MD_RECOVERY_CHECK
		 */
		if (sh->ops.zero_sum_result == 0) {
			/* both parities are correct */
			if (!s->failed)
				set_bit(STRIPE_INSYNC, &sh->state);
			else {
				/* in contrast to the raid5 case we can validate
				 * parity, but still have a failure to write
				 * back
				 */
				sh->check_state = check_state_compute_result;
				/* Returning at this point means that we may go
				 * off and bring p and/or q uptodate again so
				 * we make sure to check zero_sum_result again
				 * to verify if p or q need writeback
				 */
			}
		} else {
			atomic64_add(STRIPE_SECTORS, &conf->mddev->resync_mismatches);
			if (test_bit(MD_RECOVERY_CHECK, &conf->mddev->recovery))
				/* don't try to repair!! */
				set_bit(STRIPE_INSYNC, &sh->state);
			else {
				int *target = &sh->ops.target;

				sh->ops.target = -1;
				sh->ops.target2 = -1;
				sh->check_state = check_state_compute_run;
				set_bit(STRIPE_COMPUTE_RUN, &sh->state);
				set_bit(STRIPE_OP_COMPUTE_BLK, &s->ops_request);
				if (sh->ops.zero_sum_result & SUM_CHECK_P_RESULT) {
					set_bit(R5_Wantcompute,
						&sh->dev[pd_idx].flags);
					*target = pd_idx;
					target = &sh->ops.target2;
					s->uptodate++;
				}
				if (sh->ops.zero_sum_result & SUM_CHECK_Q_RESULT) {
					set_bit(R5_Wantcompute,
						&sh->dev[qd_idx].flags);
					*target = qd_idx;
					s->uptodate++;
				}
			}
		}
		break;
	case check_state_compute_run:
		break;
	default:
		printk(KERN_ERR "%s: unknown check_state: %d sector: %llu\n",
		       __func__, sh->check_state,
		       (unsigned long long) sh->sector);
		BUG();
	}
}

static void handle_stripe_expansion(struct r5conf *conf, struct stripe_head *sh)
{
	int i;

	/* We have read all the blocks in this stripe and now we need to
	 * copy some of them into a target stripe for expand.
	 */
	struct dma_async_tx_descriptor *tx = NULL;
	clear_bit(STRIPE_EXPAND_SOURCE, &sh->state);
	for (i = 0; i < sh->disks; i++)
		if (i != sh->pd_idx && (!r6s || i != r6s->qd_idx)) {
			int dd_idx, pd_idx, j;
			struct stripe_head *sh2;

			sector_t bn = compute_blocknr(sh, i);
			sector_t s = raid5_compute_sector(bn, conf->raid_disks,
						conf->raid_disks -
						conf->max_degraded, &dd_idx,
						&pd_idx, conf);
			sh2 = get_active_stripe(conf, s, conf->raid_disks,
						pd_idx, 1);
	BUG_ON(sh->batch_head);
	clear_bit(STRIPE_EXPAND_SOURCE, &sh->state);
	for (i = 0; i < sh->disks; i++)
		if (i != sh->pd_idx && i != sh->qd_idx) {
			int dd_idx, j;
			struct stripe_head *sh2;
			struct async_submit_ctl submit;

			sector_t bn = raid5_compute_blocknr(sh, i, 1);
			sector_t s = raid5_compute_sector(conf, bn, 0,
							  &dd_idx, NULL);
			sh2 = raid5_get_active_stripe(conf, s, 0, 1, 1);
			if (sh2 == NULL)
				/* so far only the early blocks of this stripe
				 * have been requested.  When later blocks
				 * get requested, we will try again
				 */
				continue;
			if (!test_bit(STRIPE_EXPANDING, &sh2->state) ||
			   test_bit(R5_Expanded, &sh2->dev[dd_idx].flags)) {
				/* must have already done this block */
				release_stripe(sh2);
				raid5_release_stripe(sh2);
				continue;
			}

			/* place all the copies on one channel */
			tx = async_memcpy(sh2->dev[dd_idx].page,
				sh->dev[i].page, 0, 0, STRIPE_SIZE,
				ASYNC_TX_DEP_ACK, tx, NULL, NULL);
			init_async_submit(&submit, 0, tx, NULL, NULL, NULL);
			tx = async_memcpy(sh2->dev[dd_idx].page,
					  sh->dev[i].page, 0, 0, STRIPE_SIZE,
					  &submit);

			set_bit(R5_Expanded, &sh2->dev[dd_idx].flags);
			set_bit(R5_UPTODATE, &sh2->dev[dd_idx].flags);
			for (j = 0; j < conf->raid_disks; j++)
				if (j != sh2->pd_idx &&
				    (!r6s || j != raid6_next_disk(sh2->pd_idx,
								 sh2->disks)) &&
				    j != sh2->qd_idx &&
				    !test_bit(R5_Expanded, &sh2->dev[j].flags))
					break;
			if (j == conf->raid_disks) {
				set_bit(STRIPE_EXPAND_READY, &sh2->state);
				set_bit(STRIPE_HANDLE, &sh2->state);
			}
			release_stripe(sh2);

		}
	/* done submitting copies, wait for them to complete */
	if (tx) {
		async_tx_ack(tx);
		dma_wait_for_async_tx(tx);
	}
}


/*
 * handle_stripe - do things to a stripe.
 *
 * We lock the stripe and then examine the state of various bits
 * to see what needs to be done.
 * Possible results:
 *    return some read request which now have data
 *    return some write requests which are safely on disc
			raid5_release_stripe(sh2);

		}
	/* done submitting copies, wait for them to complete */
	async_tx_quiesce(&tx);
}

/*
 * handle_stripe - do things to a stripe.
 *
 * We lock the stripe by setting STRIPE_ACTIVE and then examine the
 * state of various bits to see what needs to be done.
 * Possible results:
 *    return some read requests which now have data
 *    return some write requests which are safely on storage
 *    schedule a read on some buffers
 *    schedule a write of some buffers
 *    return confirmation of parity correctness
 *
 * buffers are taken off read_list or write_list, and bh_cache buffers
 * get BH_Lock set before the stripe lock is released.
 *
 */

static bool handle_stripe5(struct stripe_head *sh)
{
	raid5_conf_t *conf = sh->raid_conf;
	int disks = sh->disks, i;
	struct bio *return_bi = NULL;
	struct stripe_head_state s;
	struct r5dev *dev;
	mdk_rdev_t *blocked_rdev = NULL;
	int prexor;

	memset(&s, 0, sizeof(s));
	pr_debug("handling stripe %llu, state=%#lx cnt=%d, pd_idx=%d check:%d "
		 "reconstruct:%d\n", (unsigned long long)sh->sector, sh->state,
		 atomic_read(&sh->count), sh->pd_idx, sh->check_state,
		 sh->reconstruct_state);

	spin_lock(&sh->lock);
	clear_bit(STRIPE_HANDLE, &sh->state);
	clear_bit(STRIPE_DELAYED, &sh->state);

	s.syncing = test_bit(STRIPE_SYNCING, &sh->state);
	s.expanding = test_bit(STRIPE_EXPAND_SOURCE, &sh->state);
	s.expanded = test_bit(STRIPE_EXPAND_READY, &sh->state);
 */

static void analyse_stripe(struct stripe_head *sh, struct stripe_head_state *s)
{
	struct r5conf *conf = sh->raid_conf;
	int disks = sh->disks;
	struct r5dev *dev;
	int i;
	int do_recovery = 0;

	memset(s, 0, sizeof(*s));

	s->expanding = test_bit(STRIPE_EXPAND_SOURCE, &sh->state) && !sh->batch_head;
	s->expanded = test_bit(STRIPE_EXPAND_READY, &sh->state) && !sh->batch_head;
	s->failed_num[0] = -1;
	s->failed_num[1] = -1;
	s->log_failed = r5l_log_disk_error(conf);

	/* Now to look around and see what can be done */
	rcu_read_lock();
	for (i=disks; i--; ) {
		mdk_rdev_t *rdev;
		struct r5dev *dev = &sh->dev[i];
		clear_bit(R5_Insync, &dev->flags);

		pr_debug("check %d: state 0x%lx toread %p read %p write %p "
			"written %p\n",	i, dev->flags, dev->toread, dev->read,
			dev->towrite, dev->written);

		/* maybe we can request a biofill operation
		struct md_rdev *rdev;
		sector_t first_bad;
		int bad_sectors;
		int is_bad = 0;

		dev = &sh->dev[i];

		pr_debug("check %d: state 0x%lx read %p write %p written %p\n",
			 i, dev->flags,
			 dev->toread, dev->towrite, dev->written);
		/* maybe we can reply to a read
		 *
		 * new wantfill requests are only permitted while
		 * ops_complete_biofill is guaranteed to be inactive
		 */
		if (test_bit(R5_UPTODATE, &dev->flags) && dev->toread &&
		    !test_bit(STRIPE_BIOFILL_RUN, &sh->state))
			set_bit(R5_Wantfill, &dev->flags);

		/* now count some things */
		if (test_bit(R5_LOCKED, &dev->flags)) s.locked++;
		if (test_bit(R5_UPTODATE, &dev->flags)) s.uptodate++;
		if (test_bit(R5_Wantcompute, &dev->flags)) s.compute++;

		if (test_bit(R5_Wantfill, &dev->flags))
			s.to_fill++;
		else if (dev->toread)
			s.to_read++;
		if (dev->towrite) {
			s.to_write++;
			if (!test_bit(R5_OVERWRITE, &dev->flags))
				s.non_overwrite++;
		}
		if (dev->written)
			s.written++;
		rdev = rcu_dereference(conf->disks[i].rdev);
		if (blocked_rdev == NULL &&
		    rdev && unlikely(test_bit(Blocked, &rdev->flags))) {
			blocked_rdev = rdev;
			atomic_inc(&rdev->nr_pending);
		}
		if (!rdev || !test_bit(In_sync, &rdev->flags)) {
		if (test_bit(R5_LOCKED, &dev->flags))
			s->locked++;
		if (test_bit(R5_UPTODATE, &dev->flags))
			s->uptodate++;
		if (test_bit(R5_Wantcompute, &dev->flags)) {
			s->compute++;
			BUG_ON(s->compute > 2);
		}

		if (test_bit(R5_Wantfill, &dev->flags))
			s->to_fill++;
		else if (dev->toread)
			s->to_read++;
		if (dev->towrite) {
			s->to_write++;
			if (!test_bit(R5_OVERWRITE, &dev->flags))
				s->non_overwrite++;
		}
		if (dev->written)
			s->written++;
		/* Prefer to use the replacement for reads, but only
		 * if it is recovered enough and has no bad blocks.
		 */
		rdev = rcu_dereference(conf->disks[i].replacement);
		if (rdev && !test_bit(Faulty, &rdev->flags) &&
		    rdev->recovery_offset >= sh->sector + STRIPE_SECTORS &&
		    !is_badblock(rdev, sh->sector, STRIPE_SECTORS,
				 &first_bad, &bad_sectors))
			set_bit(R5_ReadRepl, &dev->flags);
		else {
			if (rdev && !test_bit(Faulty, &rdev->flags))
				set_bit(R5_NeedReplace, &dev->flags);
			else
				clear_bit(R5_NeedReplace, &dev->flags);
			rdev = rcu_dereference(conf->disks[i].rdev);
			clear_bit(R5_ReadRepl, &dev->flags);
		}
		if (rdev && test_bit(Faulty, &rdev->flags))
			rdev = NULL;
		if (rdev) {
			is_bad = is_badblock(rdev, sh->sector, STRIPE_SECTORS,
					     &first_bad, &bad_sectors);
			if (s->blocked_rdev == NULL
			    && (test_bit(Blocked, &rdev->flags)
				|| is_bad < 0)) {
				if (is_bad < 0)
					set_bit(BlockedBadBlocks,
						&rdev->flags);
				s->blocked_rdev = rdev;
				atomic_inc(&rdev->nr_pending);
			}
		}
		clear_bit(R5_Insync, &dev->flags);
		if (!rdev)
			/* Not in-sync */;
		else if (is_bad) {
			/* also not in-sync */
			if (!test_bit(WriteErrorSeen, &rdev->flags) &&
			    test_bit(R5_UPTODATE, &dev->flags)) {
				/* treat as in-sync, but with a read error
				 * which we can now try to correct
				 */
				set_bit(R5_Insync, &dev->flags);
				set_bit(R5_ReadError, &dev->flags);
			}
		} else if (test_bit(In_sync, &rdev->flags))
			set_bit(R5_Insync, &dev->flags);
		else if (sh->sector + STRIPE_SECTORS <= rdev->recovery_offset)
			/* in sync if before recovery_offset */
			set_bit(R5_Insync, &dev->flags);
		else if (test_bit(R5_UPTODATE, &dev->flags) &&
			 test_bit(R5_Expanded, &dev->flags))
			/* If we've reshaped into here, we assume it is Insync.
			 * We will shortly update recovery_offset to make
			 * it official.
			 */
			set_bit(R5_Insync, &dev->flags);

		if (test_bit(R5_WriteError, &dev->flags)) {
			/* This flag does not apply to '.replacement'
			 * only to .rdev, so make sure to check that*/
			struct md_rdev *rdev2 = rcu_dereference(
				conf->disks[i].rdev);
			if (rdev2 == rdev)
				clear_bit(R5_Insync, &dev->flags);
			if (rdev2 && !test_bit(Faulty, &rdev2->flags)) {
				s->handle_bad_blocks = 1;
				atomic_inc(&rdev2->nr_pending);
			} else
				clear_bit(R5_WriteError, &dev->flags);
		}
		if (test_bit(R5_MadeGood, &dev->flags)) {
			/* This flag does not apply to '.replacement'
			 * only to .rdev, so make sure to check that*/
			struct md_rdev *rdev2 = rcu_dereference(
				conf->disks[i].rdev);
			if (rdev2 && !test_bit(Faulty, &rdev2->flags)) {
				s->handle_bad_blocks = 1;
				atomic_inc(&rdev2->nr_pending);
			} else
				clear_bit(R5_MadeGood, &dev->flags);
		}
		if (test_bit(R5_MadeGoodRepl, &dev->flags)) {
			struct md_rdev *rdev2 = rcu_dereference(
				conf->disks[i].replacement);
			if (rdev2 && !test_bit(Faulty, &rdev2->flags)) {
				s->handle_bad_blocks = 1;
				atomic_inc(&rdev2->nr_pending);
			} else
				clear_bit(R5_MadeGoodRepl, &dev->flags);
		}
		if (!test_bit(R5_Insync, &dev->flags)) {
			/* The ReadError flag will just be confusing now */
			clear_bit(R5_ReadError, &dev->flags);
			clear_bit(R5_ReWrite, &dev->flags);
		}
		if (!rdev || !test_bit(In_sync, &rdev->flags)
		    || test_bit(R5_ReadError, &dev->flags)) {
			s.failed++;
			s.failed_num = i;
		} else
			set_bit(R5_Insync, &dev->flags);
	}
	rcu_read_unlock();

	if (unlikely(blocked_rdev)) {
		if (s.syncing || s.expanding || s.expanded ||
		    s.to_write || s.written) {
			set_bit(STRIPE_HANDLE, &sh->state);
			goto unlock;
		}
		/* There is nothing for the blocked_rdev to block */
		rdev_dec_pending(blocked_rdev, conf->mddev);
		blocked_rdev = NULL;
		if (test_bit(R5_ReadError, &dev->flags))
			clear_bit(R5_Insync, &dev->flags);
		if (!test_bit(R5_Insync, &dev->flags)) {
			if (s->failed < 2)
				s->failed_num[s->failed] = i;
			s->failed++;
			if (rdev && !test_bit(Faulty, &rdev->flags))
				do_recovery = 1;
			else if (!rdev) {
				rdev = rcu_dereference(
				    conf->disks[i].replacement);
				if (rdev && !test_bit(Faulty, &rdev->flags))
					do_recovery = 1;
			}
		}
	}
	if (test_bit(STRIPE_SYNCING, &sh->state)) {
		/* If there is a failed device being replaced,
		 *     we must be recovering.
		 * else if we are after recovery_cp, we must be syncing
		 * else if MD_RECOVERY_REQUESTED is set, we also are syncing.
		 * else we can only be replacing
		 * sync and recovery both need to read all devices, and so
		 * use the same flag.
		 */
		if (do_recovery ||
		    sh->sector >= conf->mddev->recovery_cp ||
		    test_bit(MD_RECOVERY_REQUESTED, &(conf->mddev->recovery)))
			s->syncing = 1;
		else
			s->replacing = 1;
	}
	rcu_read_unlock();
}

static int clear_batch_ready(struct stripe_head *sh)
{
	/* Return '1' if this is a member of batch, or
	 * '0' if it is a lone stripe or a head which can now be
	 * handled.
	 */
	struct stripe_head *tmp;
	if (!test_and_clear_bit(STRIPE_BATCH_READY, &sh->state))
		return (sh->batch_head && sh->batch_head != sh);
	spin_lock(&sh->stripe_lock);
	if (!sh->batch_head) {
		spin_unlock(&sh->stripe_lock);
		return 0;
	}

	/*
	 * this stripe could be added to a batch list before we check
	 * BATCH_READY, skips it
	 */
	if (sh->batch_head != sh) {
		spin_unlock(&sh->stripe_lock);
		return 1;
	}
	spin_lock(&sh->batch_lock);
	list_for_each_entry(tmp, &sh->batch_list, batch_list)
		clear_bit(STRIPE_BATCH_READY, &tmp->state);
	spin_unlock(&sh->batch_lock);
	spin_unlock(&sh->stripe_lock);

	/*
	 * BATCH_READY is cleared, no new stripes can be added.
	 * batch_list can be accessed without lock
	 */
	return 0;
}

static void break_stripe_batch_list(struct stripe_head *head_sh,
				    unsigned long handle_flags)
{
	struct stripe_head *sh, *next;
	int i;
	int do_wakeup = 0;

	list_for_each_entry_safe(sh, next, &head_sh->batch_list, batch_list) {

		list_del_init(&sh->batch_list);

		WARN_ON_ONCE(sh->state & ((1 << STRIPE_ACTIVE) |
					  (1 << STRIPE_SYNCING) |
					  (1 << STRIPE_REPLACED) |
					  (1 << STRIPE_DELAYED) |
					  (1 << STRIPE_BIT_DELAY) |
					  (1 << STRIPE_FULL_WRITE) |
					  (1 << STRIPE_BIOFILL_RUN) |
					  (1 << STRIPE_COMPUTE_RUN)  |
					  (1 << STRIPE_OPS_REQ_PENDING) |
					  (1 << STRIPE_DISCARD) |
					  (1 << STRIPE_BATCH_READY) |
					  (1 << STRIPE_BATCH_ERR) |
					  (1 << STRIPE_BITMAP_PENDING)));
		WARN_ON_ONCE(head_sh->state & ((1 << STRIPE_DISCARD) |
					      (1 << STRIPE_REPLACED)));

		set_mask_bits(&sh->state, ~(STRIPE_EXPAND_SYNC_FLAGS |
					    (1 << STRIPE_PREREAD_ACTIVE) |
					    (1 << STRIPE_DEGRADED) |
					    (1 << STRIPE_ON_UNPLUG_LIST)),
			      head_sh->state & (1 << STRIPE_INSYNC));

		sh->check_state = head_sh->check_state;
		sh->reconstruct_state = head_sh->reconstruct_state;
		for (i = 0; i < sh->disks; i++) {
			if (test_and_clear_bit(R5_Overlap, &sh->dev[i].flags))
				do_wakeup = 1;
			sh->dev[i].flags = head_sh->dev[i].flags &
				(~((1 << R5_WriteError) | (1 << R5_Overlap)));
		}
		spin_lock_irq(&sh->stripe_lock);
		sh->batch_head = NULL;
		spin_unlock_irq(&sh->stripe_lock);
		if (handle_flags == 0 ||
		    sh->state & handle_flags)
			set_bit(STRIPE_HANDLE, &sh->state);
		raid5_release_stripe(sh);
	}
	spin_lock_irq(&head_sh->stripe_lock);
	head_sh->batch_head = NULL;
	spin_unlock_irq(&head_sh->stripe_lock);
	for (i = 0; i < head_sh->disks; i++)
		if (test_and_clear_bit(R5_Overlap, &head_sh->dev[i].flags))
			do_wakeup = 1;
	if (head_sh->state & handle_flags)
		set_bit(STRIPE_HANDLE, &head_sh->state);

	if (do_wakeup)
		wake_up(&head_sh->raid_conf->wait_for_overlap);
}

static void handle_stripe(struct stripe_head *sh)
{
	struct stripe_head_state s;
	struct r5conf *conf = sh->raid_conf;
	int i;
	int prexor;
	int disks = sh->disks;
	struct r5dev *pdev, *qdev;

	clear_bit(STRIPE_HANDLE, &sh->state);
	if (test_and_set_bit_lock(STRIPE_ACTIVE, &sh->state)) {
		/* already being handled, ensure it gets handled
		 * again when current action finishes */
		set_bit(STRIPE_HANDLE, &sh->state);
		return;
	}

	if (clear_batch_ready(sh) ) {
		clear_bit_unlock(STRIPE_ACTIVE, &sh->state);
		return;
	}

	if (test_and_clear_bit(STRIPE_BATCH_ERR, &sh->state))
		break_stripe_batch_list(sh, 0);

	if (test_bit(STRIPE_SYNC_REQUESTED, &sh->state) && !sh->batch_head) {
		spin_lock(&sh->stripe_lock);
		/* Cannot process 'sync' concurrently with 'discard' */
		if (!test_bit(STRIPE_DISCARD, &sh->state) &&
		    test_and_clear_bit(STRIPE_SYNC_REQUESTED, &sh->state)) {
			set_bit(STRIPE_SYNCING, &sh->state);
			clear_bit(STRIPE_INSYNC, &sh->state);
			clear_bit(STRIPE_REPLACED, &sh->state);
		}
		spin_unlock(&sh->stripe_lock);
	}
	clear_bit(STRIPE_DELAYED, &sh->state);

	pr_debug("handling stripe %llu, state=%#lx cnt=%d, "
		"pd_idx=%d, qd_idx=%d\n, check:%d, reconstruct:%d\n",
	       (unsigned long long)sh->sector, sh->state,
	       atomic_read(&sh->count), sh->pd_idx, sh->qd_idx,
	       sh->check_state, sh->reconstruct_state);

	analyse_stripe(sh, &s);

	if (test_bit(STRIPE_LOG_TRAPPED, &sh->state))
		goto finish;

	if (s.handle_bad_blocks) {
		set_bit(STRIPE_HANDLE, &sh->state);
		goto finish;
	}

	if (unlikely(s.blocked_rdev)) {
		if (s.syncing || s.expanding || s.expanded ||
		    s.replacing || s.to_write || s.written) {
			set_bit(STRIPE_HANDLE, &sh->state);
			goto finish;
		}
		/* There is nothing for the blocked_rdev to block */
		rdev_dec_pending(s.blocked_rdev, conf->mddev);
		s.blocked_rdev = NULL;
	}

	if (s.to_fill && !test_bit(STRIPE_BIOFILL_RUN, &sh->state)) {
		set_bit(STRIPE_OP_BIOFILL, &s.ops_request);
		set_bit(STRIPE_BIOFILL_RUN, &sh->state);
	}

	pr_debug("locked=%d uptodate=%d to_read=%d"
		" to_write=%d failed=%d failed_num=%d\n",
		s.locked, s.uptodate, s.to_read, s.to_write,
		s.failed, s.failed_num);
	/* check if the array has lost two devices and, if so, some requests might
	 * need to be failed
	 */
	if (s.failed > 1 && s.to_read+s.to_write+s.written)
		handle_failed_stripe(conf, sh, &s, disks, &return_bi);
	if (s.failed > 1 && s.syncing) {
		md_done_sync(conf->mddev, STRIPE_SECTORS,0);
		clear_bit(STRIPE_SYNCING, &sh->state);
		s.syncing = 0;
	}

	/* might be able to return some write requests if the parity block
	 * is safe, or on a failed drive
	 */
	dev = &sh->dev[sh->pd_idx];
	if ( s.written &&
	     ((test_bit(R5_Insync, &dev->flags) &&
	       !test_bit(R5_LOCKED, &dev->flags) &&
	       test_bit(R5_UPTODATE, &dev->flags)) ||
	       (s.failed == 1 && s.failed_num == sh->pd_idx)))
		handle_stripe_clean_event(conf, sh, disks, &return_bi);

	/* Now we might consider reading some blocks, either to check/generate
	 * parity, or to satisfy requests
	 * or to load a block that is being partially written.
	 */
	if (s.to_read || s.non_overwrite ||
	    (s.syncing && (s.uptodate + s.compute < disks)) || s.expanding)
		handle_stripe_fill5(sh, &s, disks);

	       " to_write=%d failed=%d failed_num=%d,%d\n",
	       s.locked, s.uptodate, s.to_read, s.to_write, s.failed,
	       s.failed_num[0], s.failed_num[1]);
	/* check if the array has lost more than max_degraded devices and,
	 * if so, some requests might need to be failed.
	 */
	if (s.failed > conf->max_degraded || s.log_failed) {
		sh->check_state = 0;
		sh->reconstruct_state = 0;
		break_stripe_batch_list(sh, 0);
		if (s.to_read+s.to_write+s.written)
			handle_failed_stripe(conf, sh, &s, disks, &s.return_bi);
		if (s.syncing + s.replacing)
			handle_failed_sync(conf, sh, &s);
	}

	/* Now we check to see if any write operations have recently
	 * completed
	 */
	prexor = 0;
	if (sh->reconstruct_state == reconstruct_state_prexor_drain_result)
		prexor = 1;
	if (sh->reconstruct_state == reconstruct_state_drain_result ||
	    sh->reconstruct_state == reconstruct_state_prexor_drain_result) {
		sh->reconstruct_state = reconstruct_state_idle;

		/* All the 'written' buffers and the parity block are ready to
		 * be written back to disk
		 */
		BUG_ON(!test_bit(R5_UPTODATE, &sh->dev[sh->pd_idx].flags));
		for (i = disks; i--; ) {
			dev = &sh->dev[i];
			if (test_bit(R5_LOCKED, &dev->flags) &&
				(i == sh->pd_idx || dev->written)) {
		BUG_ON(!test_bit(R5_UPTODATE, &sh->dev[sh->pd_idx].flags) &&
		       !test_bit(R5_Discard, &sh->dev[sh->pd_idx].flags));
		BUG_ON(sh->qd_idx >= 0 &&
		       !test_bit(R5_UPTODATE, &sh->dev[sh->qd_idx].flags) &&
		       !test_bit(R5_Discard, &sh->dev[sh->qd_idx].flags));
		for (i = disks; i--; ) {
			struct r5dev *dev = &sh->dev[i];
			if (test_bit(R5_LOCKED, &dev->flags) &&
				(i == sh->pd_idx || i == sh->qd_idx ||
				 dev->written)) {
				pr_debug("Writing block %d\n", i);
				set_bit(R5_Wantwrite, &dev->flags);
				if (prexor)
					continue;
				if (!test_bit(R5_Insync, &dev->flags) ||
				    (i == sh->pd_idx && s.failed == 0))
					set_bit(STRIPE_INSYNC, &sh->state);
			}
		}
		if (test_and_clear_bit(STRIPE_PREREAD_ACTIVE, &sh->state)) {
			atomic_dec(&conf->preread_active_stripes);
			if (atomic_read(&conf->preread_active_stripes) <
				IO_THRESHOLD)
				md_wakeup_thread(conf->mddev->thread);
		}
	}

				if (s.failed > 1)
					continue;
				if (!test_bit(R5_Insync, &dev->flags) ||
				    ((i == sh->pd_idx || i == sh->qd_idx)  &&
				     s.failed == 0))
					set_bit(STRIPE_INSYNC, &sh->state);
			}
		}
		if (test_and_clear_bit(STRIPE_PREREAD_ACTIVE, &sh->state))
			s.dec_preread_active = 1;
	}

	/*
	 * might be able to return some write requests if the parity blocks
	 * are safe, or on a failed drive
	 */
	pdev = &sh->dev[sh->pd_idx];
	s.p_failed = (s.failed >= 1 && s.failed_num[0] == sh->pd_idx)
		|| (s.failed >= 2 && s.failed_num[1] == sh->pd_idx);
	qdev = &sh->dev[sh->qd_idx];
	s.q_failed = (s.failed >= 1 && s.failed_num[0] == sh->qd_idx)
		|| (s.failed >= 2 && s.failed_num[1] == sh->qd_idx)
		|| conf->level < 6;

	if (s.written &&
	    (s.p_failed || ((test_bit(R5_Insync, &pdev->flags)
			     && !test_bit(R5_LOCKED, &pdev->flags)
			     && (test_bit(R5_UPTODATE, &pdev->flags) ||
				 test_bit(R5_Discard, &pdev->flags))))) &&
	    (s.q_failed || ((test_bit(R5_Insync, &qdev->flags)
			     && !test_bit(R5_LOCKED, &qdev->flags)
			     && (test_bit(R5_UPTODATE, &qdev->flags) ||
				 test_bit(R5_Discard, &qdev->flags))))))
		handle_stripe_clean_event(conf, sh, disks, &s.return_bi);

	/* Now we might consider reading some blocks, either to check/generate
	 * parity, or to satisfy requests
	 * or to load a block that is being partially written.
	 */
	if (s.to_read || s.non_overwrite
	    || (s.to_write && s.failed)
	    || (s.syncing && (s.uptodate + s.compute < disks))
	    || s.replacing
	    || s.expanding)
		handle_stripe_fill(sh, &s, disks);

	/* Now to consider new write requests and what else, if anything
	 * should be read.  We do not handle new writes when:
	 * 1/ A 'write' operation (copy+xor) is already in flight.
	 * 2/ A 'check' operation is in flight, as it may clobber the parity
	 *    block.
	 */
	if (s.to_write && !sh->reconstruct_state && !sh->check_state)
		handle_stripe_dirtying5(conf, sh, &s, disks);
		handle_stripe_dirtying(conf, sh, &s, disks);

	/* maybe we need to check and possibly fix the parity for this stripe
	 * Any reads will already have been scheduled, so we just see if enough
	 * data is available.  The parity check is held off while parity
	 * dependent operations are in flight.
	 */
	if (sh->check_state ||
	    (s.syncing && s.locked == 0 &&
	     !test_bit(STRIPE_COMPUTE_RUN, &sh->state) &&
	     !test_bit(STRIPE_INSYNC, &sh->state)))
		handle_parity_checks5(conf, sh, &s, disks);

	if (s.syncing && s.locked == 0 && test_bit(STRIPE_INSYNC, &sh->state)) {
		md_done_sync(conf->mddev, STRIPE_SECTORS,1);
		clear_bit(STRIPE_SYNCING, &sh->state);
	}

	/* If the failed drive is just a ReadError, then we might need to progress
	 * the repair/check process
	 */
	if (s.failed == 1 && !conf->mddev->ro &&
	    test_bit(R5_ReadError, &sh->dev[s.failed_num].flags)
	    && !test_bit(R5_LOCKED, &sh->dev[s.failed_num].flags)
	    && test_bit(R5_UPTODATE, &sh->dev[s.failed_num].flags)
		) {
		dev = &sh->dev[s.failed_num];
		if (!test_bit(R5_ReWrite, &dev->flags)) {
			set_bit(R5_Wantwrite, &dev->flags);
			set_bit(R5_ReWrite, &dev->flags);
			set_bit(R5_LOCKED, &dev->flags);
			s.locked++;
		} else {
			/* let's read it back */
			set_bit(R5_Wantread, &dev->flags);
			set_bit(R5_LOCKED, &dev->flags);
			s.locked++;
		}
	}

	/* Finish reconstruct operations initiated by the expansion process */
	if (sh->reconstruct_state == reconstruct_state_result) {
	     !test_bit(STRIPE_INSYNC, &sh->state))) {
		if (conf->level == 6)
			handle_parity_checks6(conf, sh, &s, disks);
		else
			handle_parity_checks5(conf, sh, &s, disks);
	}

	if ((s.replacing || s.syncing) && s.locked == 0
	    && !test_bit(STRIPE_COMPUTE_RUN, &sh->state)
	    && !test_bit(STRIPE_REPLACED, &sh->state)) {
		/* Write out to replacement devices where possible */
		for (i = 0; i < conf->raid_disks; i++)
			if (test_bit(R5_NeedReplace, &sh->dev[i].flags)) {
				WARN_ON(!test_bit(R5_UPTODATE, &sh->dev[i].flags));
				set_bit(R5_WantReplace, &sh->dev[i].flags);
				set_bit(R5_LOCKED, &sh->dev[i].flags);
				s.locked++;
			}
		if (s.replacing)
			set_bit(STRIPE_INSYNC, &sh->state);
		set_bit(STRIPE_REPLACED, &sh->state);
	}
	if ((s.syncing || s.replacing) && s.locked == 0 &&
	    !test_bit(STRIPE_COMPUTE_RUN, &sh->state) &&
	    test_bit(STRIPE_INSYNC, &sh->state)) {
		md_done_sync(conf->mddev, STRIPE_SECTORS, 1);
		clear_bit(STRIPE_SYNCING, &sh->state);
		if (test_and_clear_bit(R5_Overlap, &sh->dev[sh->pd_idx].flags))
			wake_up(&conf->wait_for_overlap);
	}

	/* If the failed drives are just a ReadError, then we might need
	 * to progress the repair/check process
	 */
	if (s.failed <= conf->max_degraded && !conf->mddev->ro)
		for (i = 0; i < s.failed; i++) {
			struct r5dev *dev = &sh->dev[s.failed_num[i]];
			if (test_bit(R5_ReadError, &dev->flags)
			    && !test_bit(R5_LOCKED, &dev->flags)
			    && test_bit(R5_UPTODATE, &dev->flags)
				) {
				if (!test_bit(R5_ReWrite, &dev->flags)) {
					set_bit(R5_Wantwrite, &dev->flags);
					set_bit(R5_ReWrite, &dev->flags);
					set_bit(R5_LOCKED, &dev->flags);
					s.locked++;
				} else {
					/* let's read it back */
					set_bit(R5_Wantread, &dev->flags);
					set_bit(R5_LOCKED, &dev->flags);
					s.locked++;
				}
			}
		}

	/* Finish reconstruct operations initiated by the expansion process */
	if (sh->reconstruct_state == reconstruct_state_result) {
		struct stripe_head *sh_src
			= raid5_get_active_stripe(conf, sh->sector, 1, 1, 1);
		if (sh_src && test_bit(STRIPE_EXPAND_SOURCE, &sh_src->state)) {
			/* sh cannot be written until sh_src has been read.
			 * so arrange for sh to be delayed a little
			 */
			set_bit(STRIPE_DELAYED, &sh->state);
			set_bit(STRIPE_HANDLE, &sh->state);
			if (!test_and_set_bit(STRIPE_PREREAD_ACTIVE,
					      &sh_src->state))
				atomic_inc(&conf->preread_active_stripes);
			raid5_release_stripe(sh_src);
			goto finish;
		}
		if (sh_src)
			raid5_release_stripe(sh_src);

		sh->reconstruct_state = reconstruct_state_idle;
		clear_bit(STRIPE_EXPANDING, &sh->state);
		for (i = conf->raid_disks; i--; ) {
			set_bit(R5_Wantwrite, &sh->dev[i].flags);
			set_bit(R5_LOCKED, &sh->dev[i].flags);
			s.locked++;
		}
	}

	if (s.expanded && test_bit(STRIPE_EXPANDING, &sh->state) &&
	    !sh->reconstruct_state) {
		/* Need to write out all blocks after computing parity */
		sh->disks = conf->raid_disks;
		sh->pd_idx = stripe_to_pdidx(sh->sector, conf,
			conf->raid_disks);
		schedule_reconstruction5(sh, &s, 1, 1);
		stripe_set_idx(sh->sector, conf, 0, sh);
		schedule_reconstruction(sh, &s, 1, 1);
	} else if (s.expanded && !sh->reconstruct_state && s.locked == 0) {
		clear_bit(STRIPE_EXPAND_READY, &sh->state);
		atomic_dec(&conf->reshape_stripes);
		wake_up(&conf->wait_for_overlap);
		md_done_sync(conf->mddev, STRIPE_SECTORS, 1);
	}

	if (s.expanding && s.locked == 0 &&
	    !test_bit(STRIPE_COMPUTE_RUN, &sh->state))
		handle_stripe_expansion(conf, sh, NULL);

 unlock:
	spin_unlock(&sh->lock);

	/* wait for this device to become unblocked */
	if (unlikely(blocked_rdev))
		md_wait_for_blocked_rdev(blocked_rdev, conf->mddev);

	if (s.ops_request)
		raid5_run_ops(sh, s.ops_request);

	ops_run_io(sh, &s);

	return_io(return_bi);

	return blocked_rdev == NULL;
}

static bool handle_stripe6(struct stripe_head *sh, struct page *tmp_page)
{
	raid6_conf_t *conf = sh->raid_conf;
	int disks = sh->disks;
	struct bio *return_bi = NULL;
	int i, pd_idx = sh->pd_idx;
	struct stripe_head_state s;
	struct r6_state r6s;
	struct r5dev *dev, *pdev, *qdev;
	mdk_rdev_t *blocked_rdev = NULL;

	r6s.qd_idx = raid6_next_disk(pd_idx, disks);
	pr_debug("handling stripe %llu, state=%#lx cnt=%d, "
		"pd_idx=%d, qd_idx=%d\n",
	       (unsigned long long)sh->sector, sh->state,
	       atomic_read(&sh->count), pd_idx, r6s.qd_idx);
	memset(&s, 0, sizeof(s));

	spin_lock(&sh->lock);
	clear_bit(STRIPE_HANDLE, &sh->state);
	clear_bit(STRIPE_DELAYED, &sh->state);

	s.syncing = test_bit(STRIPE_SYNCING, &sh->state);
	s.expanding = test_bit(STRIPE_EXPAND_SOURCE, &sh->state);
	s.expanded = test_bit(STRIPE_EXPAND_READY, &sh->state);
	/* Now to look around and see what can be done */

	rcu_read_lock();
	for (i=disks; i--; ) {
		mdk_rdev_t *rdev;
		dev = &sh->dev[i];
		clear_bit(R5_Insync, &dev->flags);

		pr_debug("check %d: state 0x%lx read %p write %p written %p\n",
			i, dev->flags, dev->toread, dev->towrite, dev->written);
		/* maybe we can reply to a read */
		if (test_bit(R5_UPTODATE, &dev->flags) && dev->toread) {
			struct bio *rbi, *rbi2;
			pr_debug("Return read for disc %d\n", i);
			spin_lock_irq(&conf->device_lock);
			rbi = dev->toread;
			dev->toread = NULL;
			if (test_and_clear_bit(R5_Overlap, &dev->flags))
				wake_up(&conf->wait_for_overlap);
			spin_unlock_irq(&conf->device_lock);
			while (rbi && rbi->bi_sector < dev->sector + STRIPE_SECTORS) {
				copy_data(0, rbi, dev->page, dev->sector);
				rbi2 = r5_next_bio(rbi, dev->sector);
				spin_lock_irq(&conf->device_lock);
				if (--rbi->bi_phys_segments == 0) {
					rbi->bi_next = return_bi;
					return_bi = rbi;
				}
				spin_unlock_irq(&conf->device_lock);
				rbi = rbi2;
			}
		}

		/* now count some things */
		if (test_bit(R5_LOCKED, &dev->flags)) s.locked++;
		if (test_bit(R5_UPTODATE, &dev->flags)) s.uptodate++;


		if (dev->toread)
			s.to_read++;
		if (dev->towrite) {
			s.to_write++;
			if (!test_bit(R5_OVERWRITE, &dev->flags))
				s.non_overwrite++;
		}
		if (dev->written)
			s.written++;
		rdev = rcu_dereference(conf->disks[i].rdev);
		if (blocked_rdev == NULL &&
		    rdev && unlikely(test_bit(Blocked, &rdev->flags))) {
			blocked_rdev = rdev;
			atomic_inc(&rdev->nr_pending);
		}
		if (!rdev || !test_bit(In_sync, &rdev->flags)) {
			/* The ReadError flag will just be confusing now */
			clear_bit(R5_ReadError, &dev->flags);
			clear_bit(R5_ReWrite, &dev->flags);
		}
		if (!rdev || !test_bit(In_sync, &rdev->flags)
		    || test_bit(R5_ReadError, &dev->flags)) {
			if (s.failed < 2)
				r6s.failed_num[s.failed] = i;
			s.failed++;
		} else
			set_bit(R5_Insync, &dev->flags);
	}
	rcu_read_unlock();

	if (unlikely(blocked_rdev)) {
		if (s.syncing || s.expanding || s.expanded ||
		    s.to_write || s.written) {
			set_bit(STRIPE_HANDLE, &sh->state);
			goto unlock;
		}
		/* There is nothing for the blocked_rdev to block */
		rdev_dec_pending(blocked_rdev, conf->mddev);
		blocked_rdev = NULL;
	}

	pr_debug("locked=%d uptodate=%d to_read=%d"
	       " to_write=%d failed=%d failed_num=%d,%d\n",
	       s.locked, s.uptodate, s.to_read, s.to_write, s.failed,
	       r6s.failed_num[0], r6s.failed_num[1]);
	/* check if the array has lost >2 devices and, if so, some requests
	 * might need to be failed
	 */
	if (s.failed > 2 && s.to_read+s.to_write+s.written)
		handle_failed_stripe(conf, sh, &s, disks, &return_bi);
	if (s.failed > 2 && s.syncing) {
		md_done_sync(conf->mddev, STRIPE_SECTORS,0);
		clear_bit(STRIPE_SYNCING, &sh->state);
		s.syncing = 0;
	}

	/*
	 * might be able to return some write requests if the parity blocks
	 * are safe, or on a failed drive
	 */
	pdev = &sh->dev[pd_idx];
	r6s.p_failed = (s.failed >= 1 && r6s.failed_num[0] == pd_idx)
		|| (s.failed >= 2 && r6s.failed_num[1] == pd_idx);
	qdev = &sh->dev[r6s.qd_idx];
	r6s.q_failed = (s.failed >= 1 && r6s.failed_num[0] == r6s.qd_idx)
		|| (s.failed >= 2 && r6s.failed_num[1] == r6s.qd_idx);

	if ( s.written &&
	     ( r6s.p_failed || ((test_bit(R5_Insync, &pdev->flags)
			     && !test_bit(R5_LOCKED, &pdev->flags)
			     && test_bit(R5_UPTODATE, &pdev->flags)))) &&
	     ( r6s.q_failed || ((test_bit(R5_Insync, &qdev->flags)
			     && !test_bit(R5_LOCKED, &qdev->flags)
			     && test_bit(R5_UPTODATE, &qdev->flags)))))
		handle_stripe_clean_event(conf, sh, disks, &return_bi);

	/* Now we might consider reading some blocks, either to check/generate
	 * parity, or to satisfy requests
	 * or to load a block that is being partially written.
	 */
	if (s.to_read || s.non_overwrite || (s.to_write && s.failed) ||
	    (s.syncing && (s.uptodate < disks)) || s.expanding)
		handle_stripe_fill6(sh, &s, &r6s, disks);

	/* now to consider writing and what else, if anything should be read */
	if (s.to_write)
		handle_stripe_dirtying6(conf, sh, &s, &r6s, disks);

	/* maybe we need to check and possibly fix the parity for this stripe
	 * Any reads will already have been scheduled, so we just see if enough
	 * data is available
	 */
	if (s.syncing && s.locked == 0 && !test_bit(STRIPE_INSYNC, &sh->state))
		handle_parity_checks6(conf, sh, &s, &r6s, tmp_page, disks);

	if (s.syncing && s.locked == 0 && test_bit(STRIPE_INSYNC, &sh->state)) {
		md_done_sync(conf->mddev, STRIPE_SECTORS,1);
		clear_bit(STRIPE_SYNCING, &sh->state);
	}

	/* If the failed drives are just a ReadError, then we might need
	 * to progress the repair/check process
	 */
	if (s.failed <= 2 && !conf->mddev->ro)
		for (i = 0; i < s.failed; i++) {
			dev = &sh->dev[r6s.failed_num[i]];
			if (test_bit(R5_ReadError, &dev->flags)
			    && !test_bit(R5_LOCKED, &dev->flags)
			    && test_bit(R5_UPTODATE, &dev->flags)
				) {
				if (!test_bit(R5_ReWrite, &dev->flags)) {
					set_bit(R5_Wantwrite, &dev->flags);
					set_bit(R5_ReWrite, &dev->flags);
					set_bit(R5_LOCKED, &dev->flags);
				} else {
					/* let's read it back */
					set_bit(R5_Wantread, &dev->flags);
					set_bit(R5_LOCKED, &dev->flags);
				}
			}
		}

	if (s.expanded && test_bit(STRIPE_EXPANDING, &sh->state)) {
		/* Need to write out all blocks after computing P&Q */
		sh->disks = conf->raid_disks;
		sh->pd_idx = stripe_to_pdidx(sh->sector, conf,
					     conf->raid_disks);
		compute_parity6(sh, RECONSTRUCT_WRITE);
		for (i = conf->raid_disks ; i-- ;  ) {
			set_bit(R5_LOCKED, &sh->dev[i].flags);
			s.locked++;
			set_bit(R5_Wantwrite, &sh->dev[i].flags);
		}
		clear_bit(STRIPE_EXPANDING, &sh->state);
	} else if (s.expanded) {
		clear_bit(STRIPE_EXPAND_READY, &sh->state);
		atomic_dec(&conf->reshape_stripes);
		wake_up(&conf->wait_for_overlap);
		md_done_sync(conf->mddev, STRIPE_SECTORS, 1);
	}

	if (s.expanding && s.locked == 0 &&
	    !test_bit(STRIPE_COMPUTE_RUN, &sh->state))
		handle_stripe_expansion(conf, sh, &r6s);

 unlock:
	spin_unlock(&sh->lock);

	/* wait for this device to become unblocked */
	if (unlikely(blocked_rdev))
		md_wait_for_blocked_rdev(blocked_rdev, conf->mddev);

	ops_run_io(sh, &s);

	return_io(return_bi);

	return blocked_rdev == NULL;
}

/* returns true if the stripe was handled */
static bool handle_stripe(struct stripe_head *sh, struct page *tmp_page)
{
	if (sh->raid_conf->level == 6)
		return handle_stripe6(sh, tmp_page);
	else
		return handle_stripe5(sh);
}



static void raid5_activate_delayed(raid5_conf_t *conf)
		handle_stripe_expansion(conf, sh);

finish:
	/* wait for this device to become unblocked */
	if (unlikely(s.blocked_rdev)) {
		if (conf->mddev->external)
			md_wait_for_blocked_rdev(s.blocked_rdev,
						 conf->mddev);
		else
			/* Internal metadata will immediately
			 * be written by raid5d, so we don't
			 * need to wait here.
			 */
			rdev_dec_pending(s.blocked_rdev,
					 conf->mddev);
	}

	if (s.handle_bad_blocks)
		for (i = disks; i--; ) {
			struct md_rdev *rdev;
			struct r5dev *dev = &sh->dev[i];
			if (test_and_clear_bit(R5_WriteError, &dev->flags)) {
				/* We own a safe reference to the rdev */
				rdev = conf->disks[i].rdev;
				if (!rdev_set_badblocks(rdev, sh->sector,
							STRIPE_SECTORS, 0))
					md_error(conf->mddev, rdev);
				rdev_dec_pending(rdev, conf->mddev);
			}
			if (test_and_clear_bit(R5_MadeGood, &dev->flags)) {
				rdev = conf->disks[i].rdev;
				rdev_clear_badblocks(rdev, sh->sector,
						     STRIPE_SECTORS, 0);
				rdev_dec_pending(rdev, conf->mddev);
			}
			if (test_and_clear_bit(R5_MadeGoodRepl, &dev->flags)) {
				rdev = conf->disks[i].replacement;
				if (!rdev)
					/* rdev have been moved down */
					rdev = conf->disks[i].rdev;
				rdev_clear_badblocks(rdev, sh->sector,
						     STRIPE_SECTORS, 0);
				rdev_dec_pending(rdev, conf->mddev);
			}
		}

	if (s.ops_request)
		raid_run_ops(sh, s.ops_request);

	ops_run_io(sh, &s);

	if (s.dec_preread_active) {
		/* We delay this until after ops_run_io so that if make_request
		 * is waiting on a flush, it won't continue until the writes
		 * have actually been submitted.
		 */
		atomic_dec(&conf->preread_active_stripes);
		if (atomic_read(&conf->preread_active_stripes) <
		    IO_THRESHOLD)
			md_wakeup_thread(conf->mddev->thread);
	}

	if (!bio_list_empty(&s.return_bi)) {
		if (test_bit(MD_CHANGE_PENDING, &conf->mddev->flags)) {
			spin_lock_irq(&conf->device_lock);
			bio_list_merge(&conf->return_bi, &s.return_bi);
			spin_unlock_irq(&conf->device_lock);
			md_wakeup_thread(conf->mddev->thread);
		} else
			return_io(&s.return_bi);
	}

	clear_bit_unlock(STRIPE_ACTIVE, &sh->state);
}

static void raid5_activate_delayed(struct r5conf *conf)
{
	if (atomic_read(&conf->preread_active_stripes) < IO_THRESHOLD) {
		while (!list_empty(&conf->delayed_list)) {
			struct list_head *l = conf->delayed_list.next;
			struct stripe_head *sh;
			sh = list_entry(l, struct stripe_head, lru);
			list_del_init(l);
			clear_bit(STRIPE_DELAYED, &sh->state);
			if (!test_and_set_bit(STRIPE_PREREAD_ACTIVE, &sh->state))
				atomic_inc(&conf->preread_active_stripes);
			list_add_tail(&sh->lru, &conf->hold_list);
		}
	} else
		blk_plug_device(conf->mddev->queue);
}

static void activate_bit_delay(raid5_conf_t *conf)
			raid5_wakeup_stripe_thread(sh);
		}
	}
}

static void activate_bit_delay(struct r5conf *conf,
	struct list_head *temp_inactive_list)
{
	/* device_lock is held */
	struct list_head head;
	list_add(&head, &conf->bitmap_list);
	list_del_init(&conf->bitmap_list);
	while (!list_empty(&head)) {
		struct stripe_head *sh = list_entry(head.next, struct stripe_head, lru);
		list_del_init(&sh->lru);
		atomic_inc(&sh->count);
		__release_stripe(conf, sh);
	}
}

static void unplug_slaves(mddev_t *mddev)
{
	raid5_conf_t *conf = mddev_to_conf(mddev);
	int i;

	rcu_read_lock();
	for (i=0; i<mddev->raid_disks; i++) {
		mdk_rdev_t *rdev = rcu_dereference(conf->disks[i].rdev);
		if (rdev && !test_bit(Faulty, &rdev->flags) && atomic_read(&rdev->nr_pending)) {
			struct request_queue *r_queue = bdev_get_queue(rdev->bdev);

			atomic_inc(&rdev->nr_pending);
			rcu_read_unlock();

			blk_unplug(r_queue);

			rdev_dec_pending(rdev, mddev);
			rcu_read_lock();
		}
	}
	rcu_read_unlock();
}

static void raid5_unplug_device(struct request_queue *q)
{
	mddev_t *mddev = q->queuedata;
	raid5_conf_t *conf = mddev_to_conf(mddev);
	unsigned long flags;

	spin_lock_irqsave(&conf->device_lock, flags);

	if (blk_remove_plug(q)) {
		conf->seq_flush++;
		raid5_activate_delayed(conf);
	}
	md_wakeup_thread(mddev->thread);

	spin_unlock_irqrestore(&conf->device_lock, flags);

	unplug_slaves(mddev);
}

static int raid5_congested(void *data, int bits)
{
	mddev_t *mddev = data;
	raid5_conf_t *conf = mddev_to_conf(mddev);
		int hash;
		list_del_init(&sh->lru);
		atomic_inc(&sh->count);
		hash = sh->hash_lock_index;
		__release_stripe(conf, sh, &temp_inactive_list[hash]);
	}
}

static int raid5_congested(struct mddev *mddev, int bits)
{
	struct r5conf *conf = mddev->private;

	/* No difference between reads and writes.  Just check
	 * how busy the stripe_cache is
	 */
	if (conf->inactive_blocked)
		return 1;
	if (conf->quiesce)
		return 1;
	if (list_empty_careful(&conf->inactive_list))

	if (test_bit(R5_INACTIVE_BLOCKED, &conf->cache_state))
		return 1;
	if (conf->quiesce)
		return 1;
	if (atomic_read(&conf->empty_inactive_list_nr))
		return 1;

	return 0;
}

/* We want read requests to align with chunks where possible,
 * but write requests don't need to.
 */
static int raid5_mergeable_bvec(struct request_queue *q,
				struct bvec_merge_data *bvm,
				struct bio_vec *biovec)
{
	mddev_t *mddev = q->queuedata;
	sector_t sector = bvm->bi_sector + get_start_sect(bvm->bi_bdev);
	int max;
	unsigned int chunk_sectors = mddev->chunk_size >> 9;
	unsigned int bio_sectors = bvm->bi_size >> 9;

	if ((bvm->bi_rw & 1) == WRITE)
		return biovec->bv_len; /* always allow writes to be mergeable */

	max =  (chunk_sectors - ((sector & (chunk_sectors - 1)) + bio_sectors)) << 9;
	if (max < 0) max = 0;
	if (max <= biovec->bv_len && bio_sectors == 0)
		return biovec->bv_len;
	else
		return max;
}


static int in_chunk_boundary(mddev_t *mddev, struct bio *bio)
{
	sector_t sector = bio->bi_sector + get_start_sect(bio->bi_bdev);
	unsigned int chunk_sectors = mddev->chunk_size >> 9;
	unsigned int bio_sectors = bio->bi_size >> 9;

static int in_chunk_boundary(struct mddev *mddev, struct bio *bio)
{
	struct r5conf *conf = mddev->private;
	sector_t sector = bio->bi_iter.bi_sector + get_start_sect(bio->bi_bdev);
	unsigned int chunk_sectors;
	unsigned int bio_sectors = bio_sectors(bio);

	chunk_sectors = min(conf->chunk_sectors, conf->prev_chunk_sectors);
	return  chunk_sectors >=
		((sector & (chunk_sectors - 1)) + bio_sectors);
}

/*
 *  add bio to the retry LIFO  ( in O(1) ... we are in interrupt )
 *  later sampled by raid5d.
 */
static void add_bio_to_retry(struct bio *bi,raid5_conf_t *conf)
static void add_bio_to_retry(struct bio *bi,struct r5conf *conf)
{
	unsigned long flags;

	spin_lock_irqsave(&conf->device_lock, flags);

	bi->bi_next = conf->retry_read_aligned_list;
	conf->retry_read_aligned_list = bi;

	spin_unlock_irqrestore(&conf->device_lock, flags);
	md_wakeup_thread(conf->mddev->thread);
}


static struct bio *remove_bio_from_retry(raid5_conf_t *conf)
static struct bio *remove_bio_from_retry(struct r5conf *conf)
{
	struct bio *bi;

	bi = conf->retry_read_aligned;
	if (bi) {
		conf->retry_read_aligned = NULL;
		return bi;
	}
	bi = conf->retry_read_aligned_list;
	if(bi) {
		conf->retry_read_aligned_list = bi->bi_next;
		bi->bi_next = NULL;
		bi->bi_phys_segments = 1; /* biased count of active stripes */
		bi->bi_hw_segments = 0; /* count of processed stripes */
		/*
		 * this sets the active strip count to 1 and the processed
		 * strip count to zero (upper 8 bits)
		 */
		raid5_set_bi_stripes(bi, 1); /* biased count of active stripes */
	}

	return bi;
}


/*
 *  The "raid5_align_endio" should check if the read succeeded and if it
 *  did, call bio_endio on the original bio (having bio_put the new bio
 *  first).
 *  If the read failed..
 */
static void raid5_align_endio(struct bio *bi, int error)
{
	struct bio* raid_bi  = bi->bi_private;
	mddev_t *mddev;
	raid5_conf_t *conf;
	int uptodate = test_bit(BIO_UPTODATE, &bi->bi_flags);
	mdk_rdev_t *rdev;

	bio_put(bi);

	mddev = raid_bi->bi_bdev->bd_disk->queue->queuedata;
	conf = mddev_to_conf(mddev);
	rdev = (void*)raid_bi->bi_next;
	raid_bi->bi_next = NULL;

	rdev_dec_pending(rdev, conf->mddev);

	if (!error && uptodate) {
		bio_endio(raid_bi, 0);
		if (atomic_dec_and_test(&conf->active_aligned_reads))
			wake_up(&conf->wait_for_stripe);
		return;
	}


static void raid5_align_endio(struct bio *bi)
{
	struct bio* raid_bi  = bi->bi_private;
	struct mddev *mddev;
	struct r5conf *conf;
	struct md_rdev *rdev;
	int error = bi->bi_error;

	bio_put(bi);

	rdev = (void*)raid_bi->bi_next;
	raid_bi->bi_next = NULL;
	mddev = rdev->mddev;
	conf = mddev->private;

	rdev_dec_pending(rdev, conf->mddev);

	if (!error) {
		trace_block_bio_complete(bdev_get_queue(raid_bi->bi_bdev),
					 raid_bi, 0);
		bio_endio(raid_bi);
		if (atomic_dec_and_test(&conf->active_aligned_reads))
			wake_up(&conf->wait_for_quiescent);
		return;
	}

	pr_debug("raid5_align_endio : io error...handing IO for a retry\n");

	add_bio_to_retry(raid_bi, conf);
}

static int bio_fits_rdev(struct bio *bi)
{
	struct request_queue *q = bdev_get_queue(bi->bi_bdev);

	if ((bi->bi_size>>9) > q->max_sectors)
		return 0;
	blk_recount_segments(q, bi);
	if (bi->bi_phys_segments > q->max_phys_segments ||
	    bi->bi_hw_segments > q->max_hw_segments)
		return 0;

	if (q->merge_bvec_fn)
		/* it's too hard to apply the merge_bvec_fn at this stage,
		 * just just give up
		 */
		return 0;

	return 1;
}


static int chunk_aligned_read(struct request_queue *q, struct bio * raid_bio)
{
	mddev_t *mddev = q->queuedata;
	raid5_conf_t *conf = mddev_to_conf(mddev);
	const unsigned int raid_disks = conf->raid_disks;
	const unsigned int data_disks = raid_disks - conf->max_degraded;
	unsigned int dd_idx, pd_idx;
	struct bio* align_bi;
	mdk_rdev_t *rdev;

	if (!in_chunk_boundary(mddev, raid_bio)) {
		pr_debug("chunk_aligned_read : non aligned\n");
		return 0;
	}
	/*
 	 * use bio_clone to make a copy of the bio
	 */
	align_bi = bio_clone(raid_bio, GFP_NOIO);
static int raid5_read_one_chunk(struct mddev *mddev, struct bio *raid_bio)
{
	struct r5conf *conf = mddev->private;
	int dd_idx;
	struct bio* align_bi;
	struct md_rdev *rdev;
	sector_t end_sector;

	if (!in_chunk_boundary(mddev, raid_bio)) {
		pr_debug("%s: non aligned\n", __func__);
		return 0;
	}
	/*
	 * use bio_clone_mddev to make a copy of the bio
	 */
	align_bi = bio_clone_mddev(raid_bio, GFP_NOIO, mddev);
	if (!align_bi)
		return 0;
	/*
	 *   set bi_end_io to a new function, and set bi_private to the
	 *     original bio.
	 */
	align_bi->bi_end_io  = raid5_align_endio;
	align_bi->bi_private = raid_bio;
	/*
	 *	compute position
	 */
	align_bi->bi_sector =  raid5_compute_sector(raid_bio->bi_sector,
					raid_disks,
					data_disks,
					&dd_idx,
					&pd_idx,
					conf);

	rcu_read_lock();
	rdev = rcu_dereference(conf->disks[dd_idx].rdev);
	if (rdev && test_bit(In_sync, &rdev->flags)) {
	align_bi->bi_iter.bi_sector =
		raid5_compute_sector(conf, raid_bio->bi_iter.bi_sector,
				     0, &dd_idx, NULL);

	end_sector = bio_end_sector(align_bi);
	rcu_read_lock();
	rdev = rcu_dereference(conf->disks[dd_idx].replacement);
	if (!rdev || test_bit(Faulty, &rdev->flags) ||
	    rdev->recovery_offset < end_sector) {
		rdev = rcu_dereference(conf->disks[dd_idx].rdev);
		if (rdev &&
		    (test_bit(Faulty, &rdev->flags) ||
		    !(test_bit(In_sync, &rdev->flags) ||
		      rdev->recovery_offset >= end_sector)))
			rdev = NULL;
	}
	if (rdev) {
		sector_t first_bad;
		int bad_sectors;

		atomic_inc(&rdev->nr_pending);
		rcu_read_unlock();
		raid_bio->bi_next = (void*)rdev;
		align_bi->bi_bdev =  rdev->bdev;
		align_bi->bi_flags &= ~(1 << BIO_SEG_VALID);
		align_bi->bi_sector += rdev->data_offset;

		if (!bio_fits_rdev(align_bi)) {
			/* too big in some way */
		bio_clear_flag(align_bi, BIO_SEG_VALID);

		if (is_badblock(rdev, align_bi->bi_iter.bi_sector,
				bio_sectors(align_bi),
				&first_bad, &bad_sectors)) {
			bio_put(align_bi);
			rdev_dec_pending(rdev, mddev);
			return 0;
		}

		spin_lock_irq(&conf->device_lock);
		wait_event_lock_irq(conf->wait_for_stripe,
				    conf->quiesce == 0,
				    conf->device_lock, /* nothing */);
		atomic_inc(&conf->active_aligned_reads);
		spin_unlock_irq(&conf->device_lock);

		/* No reshape active, so we can trust rdev->data_offset */
		align_bi->bi_iter.bi_sector += rdev->data_offset;

		spin_lock_irq(&conf->device_lock);
		wait_event_lock_irq(conf->wait_for_quiescent,
				    conf->quiesce == 0,
				    conf->device_lock);
		atomic_inc(&conf->active_aligned_reads);
		spin_unlock_irq(&conf->device_lock);

		if (mddev->gendisk)
			trace_block_bio_remap(bdev_get_queue(align_bi->bi_bdev),
					      align_bi, disk_devt(mddev->gendisk),
					      raid_bio->bi_iter.bi_sector);
		generic_make_request(align_bi);
		return 1;
	} else {
		rcu_read_unlock();
		bio_put(align_bi);
		return 0;
	}
}

static struct bio *chunk_aligned_read(struct mddev *mddev, struct bio *raid_bio)
{
	struct bio *split;

	do {
		sector_t sector = raid_bio->bi_iter.bi_sector;
		unsigned chunk_sects = mddev->chunk_sectors;
		unsigned sectors = chunk_sects - (sector & (chunk_sects-1));

		if (sectors < bio_sectors(raid_bio)) {
			split = bio_split(raid_bio, sectors, GFP_NOIO, fs_bio_set);
			bio_chain(split, raid_bio);
		} else
			split = raid_bio;

		if (!raid5_read_one_chunk(mddev, split)) {
			if (split != raid_bio)
				generic_make_request(raid_bio);
			return split;
		}
	} while (split != raid_bio);

	return NULL;
}

/* __get_priority_stripe - get the next stripe to process
 *
 * Full stripe writes are allowed to pass preread active stripes up until
 * the bypass_threshold is exceeded.  In general the bypass_count
 * increments when the handle_list is handled before the hold_list; however, it
 * will not be incremented when STRIPE_IO_STARTED is sampled set signifying a
 * stripe with in flight i/o.  The bypass_count will be reset when the
 * head of the hold_list has changed, i.e. the head was promoted to the
 * handle_list.
 */
static struct stripe_head *__get_priority_stripe(raid5_conf_t *conf)
{
	struct stripe_head *sh;

	pr_debug("%s: handle: %s hold: %s full_writes: %d bypass_count: %d\n",
		  __func__,
		  list_empty(&conf->handle_list) ? "empty" : "busy",
		  list_empty(&conf->hold_list) ? "empty" : "busy",
		  atomic_read(&conf->pending_full_writes), conf->bypass_count);

	if (!list_empty(&conf->handle_list)) {
		sh = list_entry(conf->handle_list.next, typeof(*sh), lru);
static struct stripe_head *__get_priority_stripe(struct r5conf *conf, int group)
{
	struct stripe_head *sh = NULL, *tmp;
	struct list_head *handle_list = NULL;
	struct r5worker_group *wg = NULL;

	if (conf->worker_cnt_per_group == 0) {
		handle_list = &conf->handle_list;
	} else if (group != ANY_GROUP) {
		handle_list = &conf->worker_groups[group].handle_list;
		wg = &conf->worker_groups[group];
	} else {
		int i;
		for (i = 0; i < conf->group_cnt; i++) {
			handle_list = &conf->worker_groups[i].handle_list;
			wg = &conf->worker_groups[i];
			if (!list_empty(handle_list))
				break;
		}
	}

	pr_debug("%s: handle: %s hold: %s full_writes: %d bypass_count: %d\n",
		  __func__,
		  list_empty(handle_list) ? "empty" : "busy",
		  list_empty(&conf->hold_list) ? "empty" : "busy",
		  atomic_read(&conf->pending_full_writes), conf->bypass_count);

	if (!list_empty(handle_list)) {
		sh = list_entry(handle_list->next, typeof(*sh), lru);

		if (list_empty(&conf->hold_list))
			conf->bypass_count = 0;
		else if (!test_bit(STRIPE_IO_STARTED, &sh->state)) {
			if (conf->hold_list.next == conf->last_hold)
				conf->bypass_count++;
			else {
				conf->last_hold = conf->hold_list.next;
				conf->bypass_count -= conf->bypass_threshold;
				if (conf->bypass_count < 0)
					conf->bypass_count = 0;
			}
		}
	} else if (!list_empty(&conf->hold_list) &&
		   ((conf->bypass_threshold &&
		     conf->bypass_count > conf->bypass_threshold) ||
		    atomic_read(&conf->pending_full_writes) == 0)) {
		sh = list_entry(conf->hold_list.next,
				typeof(*sh), lru);
		conf->bypass_count -= conf->bypass_threshold;
		if (conf->bypass_count < 0)
			conf->bypass_count = 0;
	} else
		return NULL;

	list_del_init(&sh->lru);
	atomic_inc(&sh->count);
	BUG_ON(atomic_read(&sh->count) != 1);
	return sh;
}

static int make_request(struct request_queue *q, struct bio * bi)
{
	mddev_t *mddev = q->queuedata;
	raid5_conf_t *conf = mddev_to_conf(mddev);
	unsigned int dd_idx, pd_idx;

		list_for_each_entry(tmp, &conf->hold_list,  lru) {
			if (conf->worker_cnt_per_group == 0 ||
			    group == ANY_GROUP ||
			    !cpu_online(tmp->cpu) ||
			    cpu_to_group(tmp->cpu) == group) {
				sh = tmp;
				break;
			}
		}

		if (sh) {
			conf->bypass_count -= conf->bypass_threshold;
			if (conf->bypass_count < 0)
				conf->bypass_count = 0;
		}
		wg = NULL;
	}

	if (!sh)
		return NULL;

	if (wg) {
		wg->stripes_cnt--;
		sh->group = NULL;
	}
	list_del_init(&sh->lru);
	BUG_ON(atomic_inc_return(&sh->count) != 1);
	return sh;
}

struct raid5_plug_cb {
	struct blk_plug_cb	cb;
	struct list_head	list;
	struct list_head	temp_inactive_list[NR_STRIPE_HASH_LOCKS];
};

static void raid5_unplug(struct blk_plug_cb *blk_cb, bool from_schedule)
{
	struct raid5_plug_cb *cb = container_of(
		blk_cb, struct raid5_plug_cb, cb);
	struct stripe_head *sh;
	struct mddev *mddev = cb->cb.data;
	struct r5conf *conf = mddev->private;
	int cnt = 0;
	int hash;

	if (cb->list.next && !list_empty(&cb->list)) {
		spin_lock_irq(&conf->device_lock);
		while (!list_empty(&cb->list)) {
			sh = list_first_entry(&cb->list, struct stripe_head, lru);
			list_del_init(&sh->lru);
			/*
			 * avoid race release_stripe_plug() sees
			 * STRIPE_ON_UNPLUG_LIST clear but the stripe
			 * is still in our list
			 */
			smp_mb__before_atomic();
			clear_bit(STRIPE_ON_UNPLUG_LIST, &sh->state);
			/*
			 * STRIPE_ON_RELEASE_LIST could be set here. In that
			 * case, the count is always > 1 here
			 */
			hash = sh->hash_lock_index;
			__release_stripe(conf, sh, &cb->temp_inactive_list[hash]);
			cnt++;
		}
		spin_unlock_irq(&conf->device_lock);
	}
	release_inactive_stripe_list(conf, cb->temp_inactive_list,
				     NR_STRIPE_HASH_LOCKS);
	if (mddev->queue)
		trace_block_unplug(mddev->queue, cnt, !from_schedule);
	kfree(cb);
}

static void release_stripe_plug(struct mddev *mddev,
				struct stripe_head *sh)
{
	struct blk_plug_cb *blk_cb = blk_check_plugged(
		raid5_unplug, mddev,
		sizeof(struct raid5_plug_cb));
	struct raid5_plug_cb *cb;

	if (!blk_cb) {
		raid5_release_stripe(sh);
		return;
	}

	cb = container_of(blk_cb, struct raid5_plug_cb, cb);

	if (cb->list.next == NULL) {
		int i;
		INIT_LIST_HEAD(&cb->list);
		for (i = 0; i < NR_STRIPE_HASH_LOCKS; i++)
			INIT_LIST_HEAD(cb->temp_inactive_list + i);
	}

	if (!test_and_set_bit(STRIPE_ON_UNPLUG_LIST, &sh->state))
		list_add_tail(&sh->lru, &cb->list);
	else
		raid5_release_stripe(sh);
}

static void make_discard_request(struct mddev *mddev, struct bio *bi)
{
	struct r5conf *conf = mddev->private;
	sector_t logical_sector, last_sector;
	struct stripe_head *sh;
	int remaining;
	int stripe_sectors;

	if (mddev->reshape_position != MaxSector)
		/* Skip discard while reshape is happening */
		return;

	logical_sector = bi->bi_iter.bi_sector & ~((sector_t)STRIPE_SECTORS-1);
	last_sector = bi->bi_iter.bi_sector + (bi->bi_iter.bi_size>>9);

	bi->bi_next = NULL;
	bi->bi_phys_segments = 1; /* over-loaded to count active stripes */

	stripe_sectors = conf->chunk_sectors *
		(conf->raid_disks - conf->max_degraded);
	logical_sector = DIV_ROUND_UP_SECTOR_T(logical_sector,
					       stripe_sectors);
	sector_div(last_sector, stripe_sectors);

	logical_sector *= conf->chunk_sectors;
	last_sector *= conf->chunk_sectors;

	for (; logical_sector < last_sector;
	     logical_sector += STRIPE_SECTORS) {
		DEFINE_WAIT(w);
		int d;
	again:
		sh = raid5_get_active_stripe(conf, logical_sector, 0, 0, 0);
		prepare_to_wait(&conf->wait_for_overlap, &w,
				TASK_UNINTERRUPTIBLE);
		set_bit(R5_Overlap, &sh->dev[sh->pd_idx].flags);
		if (test_bit(STRIPE_SYNCING, &sh->state)) {
			raid5_release_stripe(sh);
			schedule();
			goto again;
		}
		clear_bit(R5_Overlap, &sh->dev[sh->pd_idx].flags);
		spin_lock_irq(&sh->stripe_lock);
		for (d = 0; d < conf->raid_disks; d++) {
			if (d == sh->pd_idx || d == sh->qd_idx)
				continue;
			if (sh->dev[d].towrite || sh->dev[d].toread) {
				set_bit(R5_Overlap, &sh->dev[d].flags);
				spin_unlock_irq(&sh->stripe_lock);
				raid5_release_stripe(sh);
				schedule();
				goto again;
			}
		}
		set_bit(STRIPE_DISCARD, &sh->state);
		finish_wait(&conf->wait_for_overlap, &w);
		sh->overwrite_disks = 0;
		for (d = 0; d < conf->raid_disks; d++) {
			if (d == sh->pd_idx || d == sh->qd_idx)
				continue;
			sh->dev[d].towrite = bi;
			set_bit(R5_OVERWRITE, &sh->dev[d].flags);
			raid5_inc_bi_active_stripes(bi);
			sh->overwrite_disks++;
		}
		spin_unlock_irq(&sh->stripe_lock);
		if (conf->mddev->bitmap) {
			for (d = 0;
			     d < conf->raid_disks - conf->max_degraded;
			     d++)
				bitmap_startwrite(mddev->bitmap,
						  sh->sector,
						  STRIPE_SECTORS,
						  0);
			sh->bm_seq = conf->seq_flush + 1;
			set_bit(STRIPE_BIT_DELAY, &sh->state);
		}

		set_bit(STRIPE_HANDLE, &sh->state);
		clear_bit(STRIPE_DELAYED, &sh->state);
		if (!test_and_set_bit(STRIPE_PREREAD_ACTIVE, &sh->state))
			atomic_inc(&conf->preread_active_stripes);
		release_stripe_plug(mddev, sh);
	}

	remaining = raid5_dec_bi_active_stripes(bi);
	if (remaining == 0) {
		md_write_end(mddev);
		bio_endio(bi);
	}
}

static void make_request(struct mddev *mddev, struct bio * bi)
{
	struct r5conf *conf = mddev->private;
	int dd_idx;
	sector_t new_sector;
	sector_t logical_sector, last_sector;
	struct stripe_head *sh;
	const int rw = bio_data_dir(bi);
	int remaining;

	if (unlikely(bio_barrier(bi))) {
		bio_endio(bi, -EOPNOTSUPP);
		return 0;
	DEFINE_WAIT(w);
	bool do_prepare;

	if (unlikely(bi->bi_rw & REQ_FLUSH)) {
		int ret = r5l_handle_flush_request(conf->log, bi);

		if (ret == 0)
			return;
		if (ret == -ENODEV) {
			md_flush_request(mddev, bi);
			return;
		}
		/* ret == -EAGAIN, fallback */
	}

	md_write_start(mddev, bi);

	disk_stat_inc(mddev->gendisk, ios[rw]);
	disk_stat_add(mddev->gendisk, sectors[rw], bio_sectors(bi));

	if (rw == READ &&
	     mddev->reshape_position == MaxSector &&
	     chunk_aligned_read(q,bi))
            	return 0;

	logical_sector = bi->bi_sector & ~((sector_t)STRIPE_SECTORS-1);
	last_sector = bi->bi_sector + (bi->bi_size>>9);
	bi->bi_next = NULL;
	bi->bi_phys_segments = 1;	/* over-loaded to count active stripes */

	for (;logical_sector < last_sector; logical_sector += STRIPE_SECTORS) {
		DEFINE_WAIT(w);
		int disks, data_disks;

	retry:
		prepare_to_wait(&conf->wait_for_overlap, &w, TASK_UNINTERRUPTIBLE);
		if (likely(conf->expand_progress == MaxSector))
			disks = conf->raid_disks;
		else {
			/* spinlock is needed as expand_progress may be
			 * 64bit on a 32bit platform, and so it might be
			 * possible to see a half-updated value
			 * Ofcourse expand_progress could change after
	/*
	 * If array is degraded, better not do chunk aligned read because
	 * later we might have to read it again in order to reconstruct
	 * data on failed drives.
	 */
	if (rw == READ && mddev->degraded == 0 &&
	    mddev->reshape_position == MaxSector) {
		bi = chunk_aligned_read(mddev, bi);
		if (!bi)
			return;
	}

	if (unlikely(bi->bi_rw & REQ_DISCARD)) {
		make_discard_request(mddev, bi);
		return;
	}

	logical_sector = bi->bi_iter.bi_sector & ~((sector_t)STRIPE_SECTORS-1);
	last_sector = bio_end_sector(bi);
	bi->bi_next = NULL;
	bi->bi_phys_segments = 1;	/* over-loaded to count active stripes */

	prepare_to_wait(&conf->wait_for_overlap, &w, TASK_UNINTERRUPTIBLE);
	for (;logical_sector < last_sector; logical_sector += STRIPE_SECTORS) {
		int previous;
		int seq;

		do_prepare = false;
	retry:
		seq = read_seqcount_begin(&conf->gen_lock);
		previous = 0;
		if (do_prepare)
			prepare_to_wait(&conf->wait_for_overlap, &w,
				TASK_UNINTERRUPTIBLE);
		if (unlikely(conf->reshape_progress != MaxSector)) {
			/* spinlock is needed as reshape_progress may be
			 * 64bit on a 32bit platform, and so it might be
			 * possible to see a half-updated value
			 * Of course reshape_progress could change after
			 * the lock is dropped, so once we get a reference
			 * to the stripe that we think it is, we will have
			 * to check again.
			 */
			spin_lock_irq(&conf->device_lock);
			disks = conf->raid_disks;
			if (logical_sector >= conf->expand_progress)
				disks = conf->previous_raid_disks;
			else {
				if (logical_sector >= conf->expand_lo) {
					spin_unlock_irq(&conf->device_lock);
					schedule();
			if (mddev->reshape_backwards
			    ? logical_sector < conf->reshape_progress
			    : logical_sector >= conf->reshape_progress) {
				previous = 1;
			} else {
				if (mddev->reshape_backwards
				    ? logical_sector < conf->reshape_safe
				    : logical_sector >= conf->reshape_safe) {
					spin_unlock_irq(&conf->device_lock);
					schedule();
					do_prepare = true;
					goto retry;
				}
			}
			spin_unlock_irq(&conf->device_lock);
		}
		data_disks = disks - conf->max_degraded;

 		new_sector = raid5_compute_sector(logical_sector, disks, data_disks,
						  &dd_idx, &pd_idx, conf);
		pr_debug("raid5: make_request, sector %llu logical %llu\n",
			(unsigned long long)new_sector, 
			(unsigned long long)logical_sector);

		sh = get_active_stripe(conf, new_sector, disks, pd_idx, (bi->bi_rw&RWA_MASK));
		if (sh) {
			if (unlikely(conf->expand_progress != MaxSector)) {

		new_sector = raid5_compute_sector(conf, logical_sector,
						  previous,
						  &dd_idx, NULL);
		pr_debug("raid456: make_request, sector %llu logical %llu\n",
			(unsigned long long)new_sector,
			(unsigned long long)logical_sector);

		sh = raid5_get_active_stripe(conf, new_sector, previous,
				       (bi->bi_rw&RWA_MASK), 0);
		if (sh) {
			if (unlikely(previous)) {
				/* expansion might have moved on while waiting for a
				 * stripe, so we must do the range check again.
				 * Expansion could still move past after this
				 * test, but as we are holding a reference to
				 * 'sh', we know that if that happens,
				 *  STRIPE_EXPANDING will get set and the expansion
				 * won't proceed until we finish with the stripe.
				 */
				int must_retry = 0;
				spin_lock_irq(&conf->device_lock);
				if (logical_sector <  conf->expand_progress &&
				    disks == conf->previous_raid_disks)
				if (mddev->reshape_backwards
				    ? logical_sector >= conf->reshape_progress
				    : logical_sector < conf->reshape_progress)
					/* mismatch, need to try again */
					must_retry = 1;
				spin_unlock_irq(&conf->device_lock);
				if (must_retry) {
					release_stripe(sh);
					goto retry;
				}
			}
			/* FIXME what if we get a false positive because these
			 * are being updated.
			 */
			if (logical_sector >= mddev->suspend_lo &&
			    logical_sector < mddev->suspend_hi) {
				release_stripe(sh);
				schedule();
					raid5_release_stripe(sh);
					schedule();
					do_prepare = true;
					goto retry;
				}
			}
			if (read_seqcount_retry(&conf->gen_lock, seq)) {
				/* Might have got the wrong stripe_head
				 * by accident
				 */
				raid5_release_stripe(sh);
				goto retry;
			}

			if (rw == WRITE &&
			    logical_sector >= mddev->suspend_lo &&
			    logical_sector < mddev->suspend_hi) {
				raid5_release_stripe(sh);
				/* As the suspend_* range is controlled by
				 * userspace, we want an interruptible
				 * wait.
				 */
				prepare_to_wait(&conf->wait_for_overlap,
						&w, TASK_INTERRUPTIBLE);
				if (logical_sector >= mddev->suspend_lo &&
				    logical_sector < mddev->suspend_hi) {
					sigset_t full, old;
					sigfillset(&full);
					sigprocmask(SIG_BLOCK, &full, &old);
					schedule();
					sigprocmask(SIG_SETMASK, &old, NULL);
					do_prepare = true;
				}
				goto retry;
			}

			if (test_bit(STRIPE_EXPANDING, &sh->state) ||
			    !add_stripe_bio(sh, bi, dd_idx, (bi->bi_rw&RW_MASK))) {
			    !add_stripe_bio(sh, bi, dd_idx, rw, previous)) {
				/* Stripe is busy expanding or
				 * add failed due to overlap.  Flush everything
				 * and wait a while
				 */
				raid5_unplug_device(mddev->queue);
				release_stripe(sh);
				schedule();
				goto retry;
			}
			finish_wait(&conf->wait_for_overlap, &w);
			set_bit(STRIPE_HANDLE, &sh->state);
			clear_bit(STRIPE_DELAYED, &sh->state);
			release_stripe(sh);
		} else {
			/* cannot get stripe for read-ahead, just give-up */
			clear_bit(BIO_UPTODATE, &bi->bi_flags);
			finish_wait(&conf->wait_for_overlap, &w);
			break;
		}
			
	}
	spin_lock_irq(&conf->device_lock);
	remaining = --bi->bi_phys_segments;
	spin_unlock_irq(&conf->device_lock);
				md_wakeup_thread(mddev->thread);
				raid5_release_stripe(sh);
				schedule();
				do_prepare = true;
				goto retry;
			}
			set_bit(STRIPE_HANDLE, &sh->state);
			clear_bit(STRIPE_DELAYED, &sh->state);
			if ((!sh->batch_head || sh == sh->batch_head) &&
			    (bi->bi_rw & REQ_SYNC) &&
			    !test_and_set_bit(STRIPE_PREREAD_ACTIVE, &sh->state))
				atomic_inc(&conf->preread_active_stripes);
			release_stripe_plug(mddev, sh);
		} else {
			/* cannot get stripe for read-ahead, just give-up */
			bi->bi_error = -EIO;
			break;
		}
	}
	finish_wait(&conf->wait_for_overlap, &w);

	remaining = raid5_dec_bi_active_stripes(bi);
	if (remaining == 0) {

		if ( rw == WRITE )
			md_write_end(mddev);

		bio_endio(bi, 0);
	}
	return 0;
}

static sector_t reshape_request(mddev_t *mddev, sector_t sector_nr, int *skipped)
		trace_block_bio_complete(bdev_get_queue(bi->bi_bdev),
					 bi, 0);
		bio_endio(bi);
	}
}

static sector_t raid5_size(struct mddev *mddev, sector_t sectors, int raid_disks);

static sector_t reshape_request(struct mddev *mddev, sector_t sector_nr, int *skipped)
{
	/* reshaping is quite different to recovery/resync so it is
	 * handled quite separately ... here.
	 *
	 * On each call to sync_request, we gather one chunk worth of
	 * destination stripes and flag them as expanding.
	 * Then we find all the source stripes and request reads.
	 * As the reads complete, handle_stripe will copy the data
	 * into the destination stripe and release that stripe.
	 */
	raid5_conf_t *conf = (raid5_conf_t *) mddev->private;
	struct stripe_head *sh;
	int pd_idx;
	struct r5conf *conf = mddev->private;
	struct stripe_head *sh;
	sector_t first_sector, last_sector;
	int raid_disks = conf->previous_raid_disks;
	int data_disks = raid_disks - conf->max_degraded;
	int new_data_disks = conf->raid_disks - conf->max_degraded;
	int i;
	int dd_idx;
	sector_t writepos, safepos, gap;

	if (sector_nr == 0 &&
	    conf->expand_progress != 0) {
		/* restarting in the middle, skip the initial sectors */
		sector_nr = conf->expand_progress;
		sector_div(sector_nr, new_data_disks);
		*skipped = 1;
		return sector_nr;
	}

	/* we update the metadata when there is more than 3Meg
	 * in the block range (that is rather arbitrary, should
	 * probably be time based) or when the data about to be
	 * copied would over-write the source of the data at
	 * the front of the range.
	 * i.e. one new_stripe forward from expand_progress new_maps
	 * to after where expand_lo old_maps to
	 */
	writepos = conf->expand_progress +
		conf->chunk_size/512*(new_data_disks);
	sector_div(writepos, new_data_disks);
	safepos = conf->expand_lo;
	sector_div(safepos, data_disks);
	gap = conf->expand_progress - conf->expand_lo;

	if (writepos >= safepos ||
	    gap > (new_data_disks)*3000*2 /*3Meg*/) {
		/* Cannot proceed until we've updated the superblock... */
		wait_event(conf->wait_for_overlap,
			   atomic_read(&conf->reshape_stripes)==0);
		mddev->reshape_position = conf->expand_progress;
		set_bit(MD_CHANGE_DEVS, &mddev->flags);
		md_wakeup_thread(mddev->thread);
		wait_event(mddev->sb_wait, mddev->flags == 0 ||
			   kthread_should_stop());
		spin_lock_irq(&conf->device_lock);
		conf->expand_lo = mddev->reshape_position;
		spin_unlock_irq(&conf->device_lock);
		wake_up(&conf->wait_for_overlap);
	}

	for (i=0; i < conf->chunk_size/512; i+= STRIPE_SECTORS) {
		int j;
		int skipped = 0;
		pd_idx = stripe_to_pdidx(sector_nr+i, conf, conf->raid_disks);
		sh = get_active_stripe(conf, sector_nr+i,
				       conf->raid_disks, pd_idx, 0);
	sector_t writepos, readpos, safepos;
	sector_t stripe_addr;
	int reshape_sectors;
	struct list_head stripes;
	sector_t retn;

	if (sector_nr == 0) {
		/* If restarting in the middle, skip the initial sectors */
		if (mddev->reshape_backwards &&
		    conf->reshape_progress < raid5_size(mddev, 0, 0)) {
			sector_nr = raid5_size(mddev, 0, 0)
				- conf->reshape_progress;
		} else if (mddev->reshape_backwards &&
			   conf->reshape_progress == MaxSector) {
			/* shouldn't happen, but just in case, finish up.*/
			sector_nr = MaxSector;
		} else if (!mddev->reshape_backwards &&
			   conf->reshape_progress > 0)
			sector_nr = conf->reshape_progress;
		sector_div(sector_nr, new_data_disks);
		if (sector_nr) {
			mddev->curr_resync_completed = sector_nr;
			sysfs_notify(&mddev->kobj, NULL, "sync_completed");
			*skipped = 1;
			retn = sector_nr;
			goto finish;
		}
	}

	/* We need to process a full chunk at a time.
	 * If old and new chunk sizes differ, we need to process the
	 * largest of these
	 */

	reshape_sectors = max(conf->chunk_sectors, conf->prev_chunk_sectors);

	/* We update the metadata at least every 10 seconds, or when
	 * the data about to be copied would over-write the source of
	 * the data at the front of the range.  i.e. one new_stripe
	 * along from reshape_progress new_maps to after where
	 * reshape_safe old_maps to
	 */
	writepos = conf->reshape_progress;
	sector_div(writepos, new_data_disks);
	readpos = conf->reshape_progress;
	sector_div(readpos, data_disks);
	safepos = conf->reshape_safe;
	sector_div(safepos, data_disks);
	if (mddev->reshape_backwards) {
		if (WARN_ON(writepos < reshape_sectors))
			return MaxSector;

		writepos -= reshape_sectors;
		readpos += reshape_sectors;
		safepos += reshape_sectors;
	} else {
		writepos += reshape_sectors;
		/* readpos and safepos are worst-case calculations.
		 * A negative number is overly pessimistic, and causes
		 * obvious problems for unsigned storage.  So clip to 0.
		 */
		readpos -= min_t(sector_t, reshape_sectors, readpos);
		safepos -= min_t(sector_t, reshape_sectors, safepos);
	}

	/* Having calculated the 'writepos' possibly use it
	 * to set 'stripe_addr' which is where we will write to.
	 */
	if (mddev->reshape_backwards) {
		if (WARN_ON(conf->reshape_progress == 0))
			return MaxSector;

		stripe_addr = writepos;
		if (WARN_ON((mddev->dev_sectors &
		    ~((sector_t)reshape_sectors - 1)) -
		    reshape_sectors - stripe_addr != sector_nr))
			return MaxSector;
	} else {
		if (WARN_ON(writepos != sector_nr + reshape_sectors))
			return MaxSector;

		stripe_addr = sector_nr;
	}

	/* 'writepos' is the most advanced device address we might write.
	 * 'readpos' is the least advanced device address we might read.
	 * 'safepos' is the least address recorded in the metadata as having
	 *     been reshaped.
	 * If there is a min_offset_diff, these are adjusted either by
	 * increasing the safepos/readpos if diff is negative, or
	 * increasing writepos if diff is positive.
	 * If 'readpos' is then behind 'writepos', there is no way that we can
	 * ensure safety in the face of a crash - that must be done by userspace
	 * making a backup of the data.  So in that case there is no particular
	 * rush to update metadata.
	 * Otherwise if 'safepos' is behind 'writepos', then we really need to
	 * update the metadata to advance 'safepos' to match 'readpos' so that
	 * we can be safe in the event of a crash.
	 * So we insist on updating metadata if safepos is behind writepos and
	 * readpos is beyond writepos.
	 * In any case, update the metadata every 10 seconds.
	 * Maybe that number should be configurable, but I'm not sure it is
	 * worth it.... maybe it could be a multiple of safemode_delay???
	 */
	if (conf->min_offset_diff < 0) {
		safepos += -conf->min_offset_diff;
		readpos += -conf->min_offset_diff;
	} else
		writepos += conf->min_offset_diff;

	if ((mddev->reshape_backwards
	     ? (safepos > writepos && readpos < writepos)
	     : (safepos < writepos && readpos > writepos)) ||
	    time_after(jiffies, conf->reshape_checkpoint + 10*HZ)) {
		/* Cannot proceed until we've updated the superblock... */
		wait_event(conf->wait_for_overlap,
			   atomic_read(&conf->reshape_stripes)==0
			   || test_bit(MD_RECOVERY_INTR, &mddev->recovery));
		if (atomic_read(&conf->reshape_stripes) != 0)
			return 0;
		mddev->reshape_position = conf->reshape_progress;
		mddev->curr_resync_completed = sector_nr;
		conf->reshape_checkpoint = jiffies;
		set_bit(MD_CHANGE_DEVS, &mddev->flags);
		md_wakeup_thread(mddev->thread);
		wait_event(mddev->sb_wait, mddev->flags == 0 ||
			   test_bit(MD_RECOVERY_INTR, &mddev->recovery));
		if (test_bit(MD_RECOVERY_INTR, &mddev->recovery))
			return 0;
		spin_lock_irq(&conf->device_lock);
		conf->reshape_safe = mddev->reshape_position;
		spin_unlock_irq(&conf->device_lock);
		wake_up(&conf->wait_for_overlap);
		sysfs_notify(&mddev->kobj, NULL, "sync_completed");
	}

	INIT_LIST_HEAD(&stripes);
	for (i = 0; i < reshape_sectors; i += STRIPE_SECTORS) {
		int j;
		int skipped_disk = 0;
		sh = raid5_get_active_stripe(conf, stripe_addr+i, 0, 0, 1);
		set_bit(STRIPE_EXPANDING, &sh->state);
		atomic_inc(&conf->reshape_stripes);
		/* If any of this stripe is beyond the end of the old
		 * array, then we need to zero those blocks
		 */
		for (j=sh->disks; j--;) {
			sector_t s;
			if (j == sh->pd_idx)
				continue;
			if (conf->level == 6 &&
			    j == raid6_next_disk(sh->pd_idx, sh->disks))
				continue;
			s = compute_blocknr(sh, j);
			if (s < mddev->array_sectors) {
				skipped = 1;
			    j == sh->qd_idx)
				continue;
			s = raid5_compute_blocknr(sh, j, 0);
			if (s < raid5_size(mddev, 0, 0)) {
				skipped_disk = 1;
				continue;
			}
			memset(page_address(sh->dev[j].page), 0, STRIPE_SIZE);
			set_bit(R5_Expanded, &sh->dev[j].flags);
			set_bit(R5_UPTODATE, &sh->dev[j].flags);
		}
		if (!skipped) {
			set_bit(STRIPE_EXPAND_READY, &sh->state);
			set_bit(STRIPE_HANDLE, &sh->state);
		}
		release_stripe(sh);
	}
	spin_lock_irq(&conf->device_lock);
	conf->expand_progress = (sector_nr + i) * new_data_disks;
		if (!skipped_disk) {
			set_bit(STRIPE_EXPAND_READY, &sh->state);
			set_bit(STRIPE_HANDLE, &sh->state);
		}
		list_add(&sh->lru, &stripes);
	}
	spin_lock_irq(&conf->device_lock);
	if (mddev->reshape_backwards)
		conf->reshape_progress -= reshape_sectors * new_data_disks;
	else
		conf->reshape_progress += reshape_sectors * new_data_disks;
	spin_unlock_irq(&conf->device_lock);
	/* Ok, those stripe are ready. We can start scheduling
	 * reads on the source stripes.
	 * The source stripes are determined by mapping the first and last
	 * block on the destination stripes.
	 */
	first_sector =
		raid5_compute_sector(sector_nr*(new_data_disks),
				     raid_disks, data_disks,
				     &dd_idx, &pd_idx, conf);
	last_sector =
		raid5_compute_sector((sector_nr+conf->chunk_size/512)
				     *(new_data_disks) -1,
				     raid_disks, data_disks,
				     &dd_idx, &pd_idx, conf);
	if (last_sector >= (mddev->size<<1))
		last_sector = (mddev->size<<1)-1;
	while (first_sector <= last_sector) {
		pd_idx = stripe_to_pdidx(first_sector, conf,
					 conf->previous_raid_disks);
		sh = get_active_stripe(conf, first_sector,
				       conf->previous_raid_disks, pd_idx, 0);
		set_bit(STRIPE_EXPAND_SOURCE, &sh->state);
		set_bit(STRIPE_HANDLE, &sh->state);
		release_stripe(sh);
		first_sector += STRIPE_SECTORS;
	}
	/* If this takes us to the resync_max point where we have to pause,
	 * then we need to write out the superblock.
	 */
	sector_nr += conf->chunk_size>>9;
	if (sector_nr >= mddev->resync_max) {
		/* Cannot proceed until we've updated the superblock... */
		wait_event(conf->wait_for_overlap,
			   atomic_read(&conf->reshape_stripes) == 0);
		mddev->reshape_position = conf->expand_progress;
		raid5_compute_sector(conf, stripe_addr*(new_data_disks),
				     1, &dd_idx, NULL);
	last_sector =
		raid5_compute_sector(conf, ((stripe_addr+reshape_sectors)
					    * new_data_disks - 1),
				     1, &dd_idx, NULL);
	if (last_sector >= mddev->dev_sectors)
		last_sector = mddev->dev_sectors - 1;
	while (first_sector <= last_sector) {
		sh = raid5_get_active_stripe(conf, first_sector, 1, 0, 1);
		set_bit(STRIPE_EXPAND_SOURCE, &sh->state);
		set_bit(STRIPE_HANDLE, &sh->state);
		raid5_release_stripe(sh);
		first_sector += STRIPE_SECTORS;
	}
	/* Now that the sources are clearly marked, we can release
	 * the destination stripes
	 */
	while (!list_empty(&stripes)) {
		sh = list_entry(stripes.next, struct stripe_head, lru);
		list_del_init(&sh->lru);
		raid5_release_stripe(sh);
	}
	/* If this takes us to the resync_max point where we have to pause,
	 * then we need to write out the superblock.
	 */
	sector_nr += reshape_sectors;
	retn = reshape_sectors;
finish:
	if (mddev->curr_resync_completed > mddev->resync_max ||
	    (sector_nr - mddev->curr_resync_completed) * 2
	    >= mddev->resync_max - mddev->curr_resync_completed) {
		/* Cannot proceed until we've updated the superblock... */
		wait_event(conf->wait_for_overlap,
			   atomic_read(&conf->reshape_stripes) == 0
			   || test_bit(MD_RECOVERY_INTR, &mddev->recovery));
		if (atomic_read(&conf->reshape_stripes) != 0)
			goto ret;
		mddev->reshape_position = conf->reshape_progress;
		mddev->curr_resync_completed = sector_nr;
		conf->reshape_checkpoint = jiffies;
		set_bit(MD_CHANGE_DEVS, &mddev->flags);
		md_wakeup_thread(mddev->thread);
		wait_event(mddev->sb_wait,
			   !test_bit(MD_CHANGE_DEVS, &mddev->flags)
			   || kthread_should_stop());
		spin_lock_irq(&conf->device_lock);
		conf->expand_lo = mddev->reshape_position;
		spin_unlock_irq(&conf->device_lock);
		wake_up(&conf->wait_for_overlap);
	}
	return conf->chunk_size>>9;
}

/* FIXME go_faster isn't used */
static inline sector_t sync_request(mddev_t *mddev, sector_t sector_nr, int *skipped, int go_faster)
{
	raid5_conf_t *conf = (raid5_conf_t *) mddev->private;
	struct stripe_head *sh;
	int pd_idx;
	int raid_disks = conf->raid_disks;
	sector_t max_sector = mddev->size << 1;
	int sync_blocks;
			   || test_bit(MD_RECOVERY_INTR, &mddev->recovery));
		if (test_bit(MD_RECOVERY_INTR, &mddev->recovery))
			goto ret;
		spin_lock_irq(&conf->device_lock);
		conf->reshape_safe = mddev->reshape_position;
		spin_unlock_irq(&conf->device_lock);
		wake_up(&conf->wait_for_overlap);
		sysfs_notify(&mddev->kobj, NULL, "sync_completed");
	}
ret:
	return retn;
}

static inline sector_t sync_request(struct mddev *mddev, sector_t sector_nr, int *skipped)
{
	struct r5conf *conf = mddev->private;
	struct stripe_head *sh;
	sector_t max_sector = mddev->dev_sectors;
	sector_t sync_blocks;
	int still_degraded = 0;
	int i;

	if (sector_nr >= max_sector) {
		/* just being told to finish up .. nothing much to do */
		unplug_slaves(mddev);

		if (test_bit(MD_RECOVERY_RESHAPE, &mddev->recovery)) {
			end_reshape(conf);
			return 0;
		}

		if (mddev->curr_resync < max_sector) /* aborted */
			bitmap_end_sync(mddev->bitmap, mddev->curr_resync,
					&sync_blocks, 1);
		else /* completed sync */
			conf->fullsync = 0;
		bitmap_close_sync(mddev->bitmap);

		return 0;
	}

	/* Allow raid5_quiesce to complete */
	wait_event(conf->wait_for_overlap, conf->quiesce != 2);

	if (test_bit(MD_RECOVERY_RESHAPE, &mddev->recovery))
		return reshape_request(mddev, sector_nr, skipped);

	/* No need to check resync_max as we never do more than one
	 * stripe, and as resync_max will always be on a chunk boundary,
	 * if the check in md_do_sync didn't fire, there is no chance
	 * of overstepping resync_max here
	 */

	/* if there is too many failed drives and we are trying
	 * to resync, then assert that we are finished, because there is
	 * nothing we can do.
	 */
	if (mddev->degraded >= conf->max_degraded &&
	    test_bit(MD_RECOVERY_SYNC, &mddev->recovery)) {
		sector_t rv = (mddev->size << 1) - sector_nr;
		*skipped = 1;
		return rv;
	}
	if (!bitmap_start_sync(mddev->bitmap, sector_nr, &sync_blocks, 1) &&
	    !test_bit(MD_RECOVERY_REQUESTED, &mddev->recovery) &&
	    !conf->fullsync && sync_blocks >= STRIPE_SECTORS) {
		sector_t rv = mddev->dev_sectors - sector_nr;
		*skipped = 1;
		return rv;
	}
	if (!test_bit(MD_RECOVERY_REQUESTED, &mddev->recovery) &&
	    !conf->fullsync &&
	    !bitmap_start_sync(mddev->bitmap, sector_nr, &sync_blocks, 1) &&
	    sync_blocks >= STRIPE_SECTORS) {
		/* we can skip this block, and probably more */
		sync_blocks /= STRIPE_SECTORS;
		*skipped = 1;
		return sync_blocks * STRIPE_SECTORS; /* keep things rounded to whole stripes */
	}


	bitmap_cond_end_sync(mddev->bitmap, sector_nr);

	pd_idx = stripe_to_pdidx(sector_nr, conf, raid_disks);
	sh = get_active_stripe(conf, sector_nr, raid_disks, pd_idx, 1);
	if (sh == NULL) {
		sh = get_active_stripe(conf, sector_nr, raid_disks, pd_idx, 0);
	bitmap_cond_end_sync(mddev->bitmap, sector_nr, false);

	sh = raid5_get_active_stripe(conf, sector_nr, 0, 1, 0);
	if (sh == NULL) {
		sh = raid5_get_active_stripe(conf, sector_nr, 0, 0, 0);
		/* make sure we don't swamp the stripe cache if someone else
		 * is trying to get access
		 */
		schedule_timeout_uninterruptible(1);
	}
	/* Need to check if array will still be degraded after recovery/resync
	 * We don't need to check the 'failed' flag as when that gets set,
	 * recovery aborts.
	 */
	for (i=0; i<mddev->raid_disks; i++)
		if (conf->disks[i].rdev == NULL)
			still_degraded = 1;

	bitmap_start_sync(mddev->bitmap, sector_nr, &sync_blocks, still_degraded);

	spin_lock(&sh->lock);
	set_bit(STRIPE_SYNCING, &sh->state);
	clear_bit(STRIPE_INSYNC, &sh->state);
	spin_unlock(&sh->lock);

	/* wait for any blocked device to be handled */
	while(unlikely(!handle_stripe(sh, NULL)))
		;
	release_stripe(sh);
	 * Note in case of > 1 drive failures it's possible we're rebuilding
	 * one drive while leaving another faulty drive in array.
	 */
	rcu_read_lock();
	for (i = 0; i < conf->raid_disks; i++) {
		struct md_rdev *rdev = ACCESS_ONCE(conf->disks[i].rdev);

		if (rdev == NULL || test_bit(Faulty, &rdev->flags))
			still_degraded = 1;
	}
	rcu_read_unlock();

	bitmap_start_sync(mddev->bitmap, sector_nr, &sync_blocks, still_degraded);

	set_bit(STRIPE_SYNC_REQUESTED, &sh->state);
	set_bit(STRIPE_HANDLE, &sh->state);

	raid5_release_stripe(sh);

	return STRIPE_SECTORS;
}

static int  retry_aligned_read(raid5_conf_t *conf, struct bio *raid_bio)
static int  retry_aligned_read(struct r5conf *conf, struct bio *raid_bio)
{
	/* We may not be able to submit a whole bio at once as there
	 * may not be enough stripe_heads available.
	 * We cannot pre-allocate enough stripe_heads as we may need
	 * more than exist in the cache (if we allow ever large chunks).
	 * So we do one stripe head at a time and record in
	 * ->bi_hw_segments how many have been done.
	 *
	 * We *know* that this entire raid_bio is in one chunk, so
	 * it will be only one 'dd_idx' and only need one call to raid5_compute_sector.
	 */
	struct stripe_head *sh;
	int dd_idx, pd_idx;
	int dd_idx;
	sector_t sector, logical_sector, last_sector;
	int scnt = 0;
	int remaining;
	int handled = 0;

	logical_sector = raid_bio->bi_sector & ~((sector_t)STRIPE_SECTORS-1);
	sector = raid5_compute_sector(	logical_sector,
					conf->raid_disks,
					conf->raid_disks - conf->max_degraded,
					&dd_idx,
					&pd_idx,
					conf);
	last_sector = raid_bio->bi_sector + (raid_bio->bi_size>>9);
	logical_sector = raid_bio->bi_iter.bi_sector &
		~((sector_t)STRIPE_SECTORS-1);
	sector = raid5_compute_sector(conf, logical_sector,
				      0, &dd_idx, NULL);
	last_sector = bio_end_sector(raid_bio);

	for (; logical_sector < last_sector;
	     logical_sector += STRIPE_SECTORS,
		     sector += STRIPE_SECTORS,
		     scnt++) {

		if (scnt < raid_bio->bi_hw_segments)
			/* already done this stripe */
			continue;

		sh = get_active_stripe(conf, sector, conf->raid_disks, pd_idx, 1);

		if (!sh) {
			/* failed to get a stripe - must wait */
			raid_bio->bi_hw_segments = scnt;
		if (scnt < raid5_bi_processed_stripes(raid_bio))
			/* already done this stripe */
			continue;

		sh = raid5_get_active_stripe(conf, sector, 0, 1, 1);

		if (!sh) {
			/* failed to get a stripe - must wait */
			raid5_set_bi_processed_stripes(raid_bio, scnt);
			conf->retry_read_aligned = raid_bio;
			return handled;
		}

		set_bit(R5_ReadError, &sh->dev[dd_idx].flags);
		if (!add_stripe_bio(sh, raid_bio, dd_idx, 0)) {
			release_stripe(sh);
			raid_bio->bi_hw_segments = scnt;
		if (!add_stripe_bio(sh, raid_bio, dd_idx, 0, 0)) {
			raid5_release_stripe(sh);
			raid5_set_bi_processed_stripes(raid_bio, scnt);
			conf->retry_read_aligned = raid_bio;
			return handled;
		}

		handle_stripe(sh, NULL);
		release_stripe(sh);
		handled++;
	}
	spin_lock_irq(&conf->device_lock);
	remaining = --raid_bio->bi_phys_segments;
	spin_unlock_irq(&conf->device_lock);
	if (remaining == 0)
		bio_endio(raid_bio, 0);
	if (atomic_dec_and_test(&conf->active_aligned_reads))
		wake_up(&conf->wait_for_stripe);
	return handled;
}


		set_bit(R5_ReadNoMerge, &sh->dev[dd_idx].flags);
		handle_stripe(sh);
		raid5_release_stripe(sh);
		handled++;
	}
	remaining = raid5_dec_bi_active_stripes(raid_bio);
	if (remaining == 0) {
		trace_block_bio_complete(bdev_get_queue(raid_bio->bi_bdev),
					 raid_bio, 0);
		bio_endio(raid_bio);
	}
	if (atomic_dec_and_test(&conf->active_aligned_reads))
		wake_up(&conf->wait_for_quiescent);
	return handled;
}

static int handle_active_stripes(struct r5conf *conf, int group,
				 struct r5worker *worker,
				 struct list_head *temp_inactive_list)
{
	struct stripe_head *batch[MAX_STRIPE_BATCH], *sh;
	int i, batch_size = 0, hash;
	bool release_inactive = false;

	while (batch_size < MAX_STRIPE_BATCH &&
			(sh = __get_priority_stripe(conf, group)) != NULL)
		batch[batch_size++] = sh;

	if (batch_size == 0) {
		for (i = 0; i < NR_STRIPE_HASH_LOCKS; i++)
			if (!list_empty(temp_inactive_list + i))
				break;
		if (i == NR_STRIPE_HASH_LOCKS) {
			spin_unlock_irq(&conf->device_lock);
			r5l_flush_stripe_to_raid(conf->log);
			spin_lock_irq(&conf->device_lock);
			return batch_size;
		}
		release_inactive = true;
	}
	spin_unlock_irq(&conf->device_lock);

	release_inactive_stripe_list(conf, temp_inactive_list,
				     NR_STRIPE_HASH_LOCKS);

	r5l_flush_stripe_to_raid(conf->log);
	if (release_inactive) {
		spin_lock_irq(&conf->device_lock);
		return 0;
	}

	for (i = 0; i < batch_size; i++)
		handle_stripe(batch[i]);
	r5l_write_stripe_run(conf->log);

	cond_resched();

	spin_lock_irq(&conf->device_lock);
	for (i = 0; i < batch_size; i++) {
		hash = batch[i]->hash_lock_index;
		__release_stripe(conf, batch[i], &temp_inactive_list[hash]);
	}
	return batch_size;
}

static void raid5_do_work(struct work_struct *work)
{
	struct r5worker *worker = container_of(work, struct r5worker, work);
	struct r5worker_group *group = worker->group;
	struct r5conf *conf = group->conf;
	int group_id = group - conf->worker_groups;
	int handled;
	struct blk_plug plug;

	pr_debug("+++ raid5worker active\n");

	blk_start_plug(&plug);
	handled = 0;
	spin_lock_irq(&conf->device_lock);
	while (1) {
		int batch_size, released;

		released = release_stripe_list(conf, worker->temp_inactive_list);

		batch_size = handle_active_stripes(conf, group_id, worker,
						   worker->temp_inactive_list);
		worker->working = false;
		if (!batch_size && !released)
			break;
		handled += batch_size;
	}
	pr_debug("%d stripes handled\n", handled);

	spin_unlock_irq(&conf->device_lock);

	r5l_flush_stripe_to_raid(conf->log);

	async_tx_issue_pending_all();
	blk_finish_plug(&plug);

	pr_debug("--- raid5worker inactive\n");
}

/*
 * This is our raid5 kernel thread.
 *
 * We scan the hash table for stripes which can be handled now.
 * During the scan, completed stripes are saved for us by the interrupt
 * handler, so that they will not have to wait for our next wakeup.
 */
static void raid5d(mddev_t *mddev)
{
	struct stripe_head *sh;
	raid5_conf_t *conf = mddev_to_conf(mddev);
	int handled;
static void raid5d(struct md_thread *thread)
{
	struct mddev *mddev = thread->mddev;
	struct r5conf *conf = mddev->private;
	int handled;
	struct blk_plug plug;

	pr_debug("+++ raid5d active\n");

	md_check_recovery(mddev);

	if (!bio_list_empty(&conf->return_bi) &&
	    !test_bit(MD_CHANGE_PENDING, &mddev->flags)) {
		struct bio_list tmp = BIO_EMPTY_LIST;
		spin_lock_irq(&conf->device_lock);
		if (!test_bit(MD_CHANGE_PENDING, &mddev->flags)) {
			bio_list_merge(&tmp, &conf->return_bi);
			bio_list_init(&conf->return_bi);
		}
		spin_unlock_irq(&conf->device_lock);
		return_io(&tmp);
	}

	blk_start_plug(&plug);
	handled = 0;
	spin_lock_irq(&conf->device_lock);
	while (1) {
		struct bio *bio;

		if (conf->seq_flush != conf->seq_write) {
			int seq = conf->seq_flush;
			spin_unlock_irq(&conf->device_lock);
			bitmap_unplug(mddev->bitmap);
			spin_lock_irq(&conf->device_lock);
			conf->seq_write = seq;
			activate_bit_delay(conf);
		}
		int batch_size, released;

		released = release_stripe_list(conf, conf->temp_inactive_list);
		if (released)
			clear_bit(R5_DID_ALLOC, &conf->cache_state);

		if (
		    !list_empty(&conf->bitmap_list)) {
			/* Now is a good time to flush some bitmap updates */
			conf->seq_flush++;
			spin_unlock_irq(&conf->device_lock);
			bitmap_unplug(mddev->bitmap);
			spin_lock_irq(&conf->device_lock);
			conf->seq_write = conf->seq_flush;
			activate_bit_delay(conf, conf->temp_inactive_list);
		}
		raid5_activate_delayed(conf);

		while ((bio = remove_bio_from_retry(conf))) {
			int ok;
			spin_unlock_irq(&conf->device_lock);
			ok = retry_aligned_read(conf, bio);
			spin_lock_irq(&conf->device_lock);
			if (!ok)
				break;
			handled++;
		}

		sh = __get_priority_stripe(conf);

		if (!sh)
			break;
		spin_unlock_irq(&conf->device_lock);
		
		handled++;
		handle_stripe(sh, conf->spare_page);
		release_stripe(sh);

		spin_lock_irq(&conf->device_lock);
		batch_size = handle_active_stripes(conf, ANY_GROUP, NULL,
						   conf->temp_inactive_list);
		if (!batch_size && !released)
			break;
		handled += batch_size;

		if (mddev->flags & ~(1<<MD_CHANGE_PENDING)) {
			spin_unlock_irq(&conf->device_lock);
			md_check_recovery(mddev);
			spin_lock_irq(&conf->device_lock);
		}
	}
	pr_debug("%d stripes handled\n", handled);

	spin_unlock_irq(&conf->device_lock);

	async_tx_issue_pending_all();
	unplug_slaves(mddev);
	if (test_and_clear_bit(R5_ALLOC_MORE, &conf->cache_state) &&
	    mutex_trylock(&conf->cache_size_mutex)) {
		grow_one_stripe(conf, __GFP_NOWARN);
		/* Set flag even if allocation failed.  This helps
		 * slow down allocation requests when mem is short
		 */
		set_bit(R5_DID_ALLOC, &conf->cache_state);
		mutex_unlock(&conf->cache_size_mutex);
	}

	r5l_flush_stripe_to_raid(conf->log);

	async_tx_issue_pending_all();
	blk_finish_plug(&plug);

	pr_debug("--- raid5d inactive\n");
}

static ssize_t
raid5_show_stripe_cache_size(mddev_t *mddev, char *page)
{
	raid5_conf_t *conf = mddev_to_conf(mddev);
	if (conf)
		return sprintf(page, "%d\n", conf->max_nr_stripes);
	else
		return 0;
}

static ssize_t
raid5_store_stripe_cache_size(mddev_t *mddev, const char *page, size_t len)
{
	raid5_conf_t *conf = mddev_to_conf(mddev);
raid5_show_stripe_cache_size(struct mddev *mddev, char *page)
{
	struct r5conf *conf;
	int ret = 0;
	spin_lock(&mddev->lock);
	conf = mddev->private;
	if (conf)
		ret = sprintf(page, "%d\n", conf->min_nr_stripes);
	spin_unlock(&mddev->lock);
	return ret;
}

int
raid5_set_cache_size(struct mddev *mddev, int size)
{
	struct r5conf *conf = mddev->private;
	int err;

	if (size <= 16 || size > 32768)
		return -EINVAL;

	conf->min_nr_stripes = size;
	mutex_lock(&conf->cache_size_mutex);
	while (size < conf->max_nr_stripes &&
	       drop_one_stripe(conf))
		;
	mutex_unlock(&conf->cache_size_mutex);


	err = md_allow_write(mddev);
	if (err)
		return err;

	mutex_lock(&conf->cache_size_mutex);
	while (size > conf->max_nr_stripes)
		if (!grow_one_stripe(conf, GFP_KERNEL))
			break;
	mutex_unlock(&conf->cache_size_mutex);

	return 0;
}
EXPORT_SYMBOL(raid5_set_cache_size);

static ssize_t
raid5_store_stripe_cache_size(struct mddev *mddev, const char *page, size_t len)
{
	struct r5conf *conf;
	unsigned long new;
	int err;

	if (len >= PAGE_SIZE)
		return -EINVAL;
	if (!conf)
		return -ENODEV;

	if (strict_strtoul(page, 10, &new))
		return -EINVAL;
	if (new <= 16 || new > 32768)
		return -EINVAL;
	while (new < conf->max_nr_stripes) {
		if (drop_one_stripe(conf))
			conf->max_nr_stripes--;
		else
			break;
	}
	err = md_allow_write(mddev);
	if (err)
		return err;
	while (new > conf->max_nr_stripes) {
		if (grow_one_stripe(conf))
			conf->max_nr_stripes++;
		else break;
	}
	return len;
	if (kstrtoul(page, 10, &new))
		return -EINVAL;
	err = mddev_lock(mddev);
	if (err)
		return err;
	conf = mddev->private;
	if (!conf)
		err = -ENODEV;
	else
		err = raid5_set_cache_size(mddev, new);
	mddev_unlock(mddev);

	return err ?: len;
}

static struct md_sysfs_entry
raid5_stripecache_size = __ATTR(stripe_cache_size, S_IRUGO | S_IWUSR,
				raid5_show_stripe_cache_size,
				raid5_store_stripe_cache_size);

static ssize_t
raid5_show_preread_threshold(mddev_t *mddev, char *page)
{
	raid5_conf_t *conf = mddev_to_conf(mddev);
	if (conf)
		return sprintf(page, "%d\n", conf->bypass_threshold);
raid5_show_rmw_level(struct mddev  *mddev, char *page)
{
	struct r5conf *conf = mddev->private;
	if (conf)
		return sprintf(page, "%d\n", conf->rmw_level);
	else
		return 0;
}

static ssize_t
raid5_store_preread_threshold(mddev_t *mddev, const char *page, size_t len)
{
	raid5_conf_t *conf = mddev_to_conf(mddev);
	unsigned long new;
	if (len >= PAGE_SIZE)
		return -EINVAL;
	if (!conf)
		return -ENODEV;

	if (strict_strtoul(page, 10, &new))
		return -EINVAL;
	if (new > conf->max_nr_stripes)
		return -EINVAL;
	conf->bypass_threshold = new;
raid5_store_rmw_level(struct mddev  *mddev, const char *page, size_t len)
{
	struct r5conf *conf = mddev->private;
	unsigned long new;

	if (!conf)
		return -ENODEV;

	if (len >= PAGE_SIZE)
		return -EINVAL;

	if (kstrtoul(page, 10, &new))
		return -EINVAL;

	if (new != PARITY_DISABLE_RMW && !raid6_call.xor_syndrome)
		return -EINVAL;

	if (new != PARITY_DISABLE_RMW &&
	    new != PARITY_ENABLE_RMW &&
	    new != PARITY_PREFER_RMW)
		return -EINVAL;

	conf->rmw_level = new;
	return len;
}

static struct md_sysfs_entry
raid5_rmw_level = __ATTR(rmw_level, S_IRUGO | S_IWUSR,
			 raid5_show_rmw_level,
			 raid5_store_rmw_level);


static ssize_t
raid5_show_preread_threshold(struct mddev *mddev, char *page)
{
	struct r5conf *conf;
	int ret = 0;
	spin_lock(&mddev->lock);
	conf = mddev->private;
	if (conf)
		ret = sprintf(page, "%d\n", conf->bypass_threshold);
	spin_unlock(&mddev->lock);
	return ret;
}

static ssize_t
raid5_store_preread_threshold(struct mddev *mddev, const char *page, size_t len)
{
	struct r5conf *conf;
	unsigned long new;
	int err;

	if (len >= PAGE_SIZE)
		return -EINVAL;
	if (kstrtoul(page, 10, &new))
		return -EINVAL;

	err = mddev_lock(mddev);
	if (err)
		return err;
	conf = mddev->private;
	if (!conf)
		err = -ENODEV;
	else if (new > conf->min_nr_stripes)
		err = -EINVAL;
	else
		conf->bypass_threshold = new;
	mddev_unlock(mddev);
	return err ?: len;
}

static struct md_sysfs_entry
raid5_preread_bypass_threshold = __ATTR(preread_bypass_threshold,
					S_IRUGO | S_IWUSR,
					raid5_show_preread_threshold,
					raid5_store_preread_threshold);

static ssize_t
stripe_cache_active_show(mddev_t *mddev, char *page)
{
	raid5_conf_t *conf = mddev_to_conf(mddev);
raid5_show_skip_copy(struct mddev *mddev, char *page)
{
	struct r5conf *conf;
	int ret = 0;
	spin_lock(&mddev->lock);
	conf = mddev->private;
	if (conf)
		ret = sprintf(page, "%d\n", conf->skip_copy);
	spin_unlock(&mddev->lock);
	return ret;
}

static ssize_t
raid5_store_skip_copy(struct mddev *mddev, const char *page, size_t len)
{
	struct r5conf *conf;
	unsigned long new;
	int err;

	if (len >= PAGE_SIZE)
		return -EINVAL;
	if (kstrtoul(page, 10, &new))
		return -EINVAL;
	new = !!new;

	err = mddev_lock(mddev);
	if (err)
		return err;
	conf = mddev->private;
	if (!conf)
		err = -ENODEV;
	else if (new != conf->skip_copy) {
		mddev_suspend(mddev);
		conf->skip_copy = new;
		if (new)
			mddev->queue->backing_dev_info.capabilities |=
				BDI_CAP_STABLE_WRITES;
		else
			mddev->queue->backing_dev_info.capabilities &=
				~BDI_CAP_STABLE_WRITES;
		mddev_resume(mddev);
	}
	mddev_unlock(mddev);
	return err ?: len;
}

static struct md_sysfs_entry
raid5_skip_copy = __ATTR(skip_copy, S_IRUGO | S_IWUSR,
					raid5_show_skip_copy,
					raid5_store_skip_copy);

static ssize_t
stripe_cache_active_show(struct mddev *mddev, char *page)
{
	struct r5conf *conf = mddev->private;
	if (conf)
		return sprintf(page, "%d\n", atomic_read(&conf->active_stripes));
	else
		return 0;
}

static struct md_sysfs_entry
raid5_stripecache_active = __ATTR_RO(stripe_cache_active);

static ssize_t
raid5_show_group_thread_cnt(struct mddev *mddev, char *page)
{
	struct r5conf *conf;
	int ret = 0;
	spin_lock(&mddev->lock);
	conf = mddev->private;
	if (conf)
		ret = sprintf(page, "%d\n", conf->worker_cnt_per_group);
	spin_unlock(&mddev->lock);
	return ret;
}

static int alloc_thread_groups(struct r5conf *conf, int cnt,
			       int *group_cnt,
			       int *worker_cnt_per_group,
			       struct r5worker_group **worker_groups);
static ssize_t
raid5_store_group_thread_cnt(struct mddev *mddev, const char *page, size_t len)
{
	struct r5conf *conf;
	unsigned long new;
	int err;
	struct r5worker_group *new_groups, *old_groups;
	int group_cnt, worker_cnt_per_group;

	if (len >= PAGE_SIZE)
		return -EINVAL;
	if (kstrtoul(page, 10, &new))
		return -EINVAL;

	err = mddev_lock(mddev);
	if (err)
		return err;
	conf = mddev->private;
	if (!conf)
		err = -ENODEV;
	else if (new != conf->worker_cnt_per_group) {
		mddev_suspend(mddev);

		old_groups = conf->worker_groups;
		if (old_groups)
			flush_workqueue(raid5_wq);

		err = alloc_thread_groups(conf, new,
					  &group_cnt, &worker_cnt_per_group,
					  &new_groups);
		if (!err) {
			spin_lock_irq(&conf->device_lock);
			conf->group_cnt = group_cnt;
			conf->worker_cnt_per_group = worker_cnt_per_group;
			conf->worker_groups = new_groups;
			spin_unlock_irq(&conf->device_lock);

			if (old_groups)
				kfree(old_groups[0].workers);
			kfree(old_groups);
		}
		mddev_resume(mddev);
	}
	mddev_unlock(mddev);

	return err ?: len;
}

static struct md_sysfs_entry
raid5_group_thread_cnt = __ATTR(group_thread_cnt, S_IRUGO | S_IWUSR,
				raid5_show_group_thread_cnt,
				raid5_store_group_thread_cnt);

static struct attribute *raid5_attrs[] =  {
	&raid5_stripecache_size.attr,
	&raid5_stripecache_active.attr,
	&raid5_preread_bypass_threshold.attr,
	&raid5_group_thread_cnt.attr,
	&raid5_skip_copy.attr,
	&raid5_rmw_level.attr,
	NULL,
};
static struct attribute_group raid5_attrs_group = {
	.name = NULL,
	.attrs = raid5_attrs,
};

static int run(mddev_t *mddev)
{
	raid5_conf_t *conf;
	int raid_disk, memory;
	mdk_rdev_t *rdev;
	struct disk_info *disk;
	struct list_head *tmp;
	int working_disks = 0;

	if (mddev->level != 5 && mddev->level != 4 && mddev->level != 6) {
		printk(KERN_ERR "raid5: %s: raid level not set to 4/5/6 (%d)\n",
		       mdname(mddev), mddev->level);
		return -EIO;
	}

	if (mddev->reshape_position != MaxSector) {
		/* Check that we can continue the reshape.
		 * Currently only disks can change, it must
		 * increase, and we must be past the point where
		 * a stripe over-writes itself
		 */
		sector_t here_new, here_old;
		int old_disks;
		int max_degraded = (mddev->level == 5 ? 1 : 2);

		if (mddev->new_level != mddev->level ||
		    mddev->new_layout != mddev->layout ||
		    mddev->new_chunk != mddev->chunk_size) {
			printk(KERN_ERR "raid5: %s: unsupported reshape "
			       "required - aborting.\n",
			       mdname(mddev));
			return -EINVAL;
		}
		if (mddev->delta_disks <= 0) {
			printk(KERN_ERR "raid5: %s: unsupported reshape "
			       "(reduce disks) required - aborting.\n",
			       mdname(mddev));
			return -EINVAL;
		}
		old_disks = mddev->raid_disks - mddev->delta_disks;
		/* reshape_position must be on a new-stripe boundary, and one
		 * further up in new geometry must map after here in old
		 * geometry.
		 */
		here_new = mddev->reshape_position;
		if (sector_div(here_new, (mddev->chunk_size>>9)*
			       (mddev->raid_disks - max_degraded))) {
			printk(KERN_ERR "raid5: reshape_position not "
			       "on a stripe boundary\n");
			return -EINVAL;
		}
		/* here_new is the stripe we will write to */
		here_old = mddev->reshape_position;
		sector_div(here_old, (mddev->chunk_size>>9)*
			   (old_disks-max_degraded));
		/* here_old is the first stripe that we might need to read
		 * from */
		if (here_new >= here_old) {
			/* Reading from the same stripe as writing to - bad */
			printk(KERN_ERR "raid5: reshape_position too early for "
			       "auto-recovery - aborting.\n");
			return -EINVAL;
		}
		printk(KERN_INFO "raid5: reshape will continue\n");
		/* OK, we should be able to continue; */
	}


	mddev->private = kzalloc(sizeof (raid5_conf_t), GFP_KERNEL);
	if ((conf = mddev->private) == NULL)
		goto abort;
	if (mddev->reshape_position == MaxSector) {
		conf->previous_raid_disks = conf->raid_disks = mddev->raid_disks;
	} else {
		conf->raid_disks = mddev->raid_disks;
		conf->previous_raid_disks = mddev->raid_disks - mddev->delta_disks;
	}

	conf->disks = kzalloc(conf->raid_disks * sizeof(struct disk_info),
static int alloc_thread_groups(struct r5conf *conf, int cnt,
			       int *group_cnt,
			       int *worker_cnt_per_group,
			       struct r5worker_group **worker_groups)
{
	int i, j, k;
	ssize_t size;
	struct r5worker *workers;

	*worker_cnt_per_group = cnt;
	if (cnt == 0) {
		*group_cnt = 0;
		*worker_groups = NULL;
		return 0;
	}
	*group_cnt = num_possible_nodes();
	size = sizeof(struct r5worker) * cnt;
	workers = kzalloc(size * *group_cnt, GFP_NOIO);
	*worker_groups = kzalloc(sizeof(struct r5worker_group) *
				*group_cnt, GFP_NOIO);
	if (!*worker_groups || !workers) {
		kfree(workers);
		kfree(*worker_groups);
		return -ENOMEM;
	}

	for (i = 0; i < *group_cnt; i++) {
		struct r5worker_group *group;

		group = &(*worker_groups)[i];
		INIT_LIST_HEAD(&group->handle_list);
		group->conf = conf;
		group->workers = workers + i * cnt;

		for (j = 0; j < cnt; j++) {
			struct r5worker *worker = group->workers + j;
			worker->group = group;
			INIT_WORK(&worker->work, raid5_do_work);

			for (k = 0; k < NR_STRIPE_HASH_LOCKS; k++)
				INIT_LIST_HEAD(worker->temp_inactive_list + k);
		}
	}

	return 0;
}

static void free_thread_groups(struct r5conf *conf)
{
	if (conf->worker_groups)
		kfree(conf->worker_groups[0].workers);
	kfree(conf->worker_groups);
	conf->worker_groups = NULL;
}

static sector_t
raid5_size(struct mddev *mddev, sector_t sectors, int raid_disks)
{
	struct r5conf *conf = mddev->private;

	if (!sectors)
		sectors = mddev->dev_sectors;
	if (!raid_disks)
		/* size is defined by the smallest of previous and new size */
		raid_disks = min(conf->raid_disks, conf->previous_raid_disks);

	sectors &= ~((sector_t)conf->chunk_sectors - 1);
	sectors &= ~((sector_t)conf->prev_chunk_sectors - 1);
	return sectors * (raid_disks - conf->max_degraded);
}

static void free_scratch_buffer(struct r5conf *conf, struct raid5_percpu *percpu)
{
	safe_put_page(percpu->spare_page);
	if (percpu->scribble)
		flex_array_free(percpu->scribble);
	percpu->spare_page = NULL;
	percpu->scribble = NULL;
}

static int alloc_scratch_buffer(struct r5conf *conf, struct raid5_percpu *percpu)
{
	if (conf->level == 6 && !percpu->spare_page)
		percpu->spare_page = alloc_page(GFP_KERNEL);
	if (!percpu->scribble)
		percpu->scribble = scribble_alloc(max(conf->raid_disks,
						      conf->previous_raid_disks),
						  max(conf->chunk_sectors,
						      conf->prev_chunk_sectors)
						   / STRIPE_SECTORS,
						  GFP_KERNEL);

	if (!percpu->scribble || (conf->level == 6 && !percpu->spare_page)) {
		free_scratch_buffer(conf, percpu);
		return -ENOMEM;
	}

	return 0;
}

static void raid5_free_percpu(struct r5conf *conf)
{
	unsigned long cpu;

	if (!conf->percpu)
		return;

#ifdef CONFIG_HOTPLUG_CPU
	unregister_cpu_notifier(&conf->cpu_notify);
#endif

	get_online_cpus();
	for_each_possible_cpu(cpu)
		free_scratch_buffer(conf, per_cpu_ptr(conf->percpu, cpu));
	put_online_cpus();

	free_percpu(conf->percpu);
}

static void free_conf(struct r5conf *conf)
{
	if (conf->log)
		r5l_exit_log(conf->log);
	if (conf->shrinker.seeks)
		unregister_shrinker(&conf->shrinker);

	free_thread_groups(conf);
	shrink_stripes(conf);
	raid5_free_percpu(conf);
	kfree(conf->disks);
	kfree(conf->stripe_hashtbl);
	kfree(conf);
}

#ifdef CONFIG_HOTPLUG_CPU
static int raid456_cpu_notify(struct notifier_block *nfb, unsigned long action,
			      void *hcpu)
{
	struct r5conf *conf = container_of(nfb, struct r5conf, cpu_notify);
	long cpu = (long)hcpu;
	struct raid5_percpu *percpu = per_cpu_ptr(conf->percpu, cpu);

	switch (action) {
	case CPU_UP_PREPARE:
	case CPU_UP_PREPARE_FROZEN:
		if (alloc_scratch_buffer(conf, percpu)) {
			pr_err("%s: failed memory allocation for cpu%ld\n",
			       __func__, cpu);
			return notifier_from_errno(-ENOMEM);
		}
		break;
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		free_scratch_buffer(conf, per_cpu_ptr(conf->percpu, cpu));
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}
#endif

static int raid5_alloc_percpu(struct r5conf *conf)
{
	unsigned long cpu;
	int err = 0;

	conf->percpu = alloc_percpu(struct raid5_percpu);
	if (!conf->percpu)
		return -ENOMEM;

#ifdef CONFIG_HOTPLUG_CPU
	conf->cpu_notify.notifier_call = raid456_cpu_notify;
	conf->cpu_notify.priority = 0;
	err = register_cpu_notifier(&conf->cpu_notify);
	if (err)
		return err;
#endif

	get_online_cpus();
	for_each_present_cpu(cpu) {
		err = alloc_scratch_buffer(conf, per_cpu_ptr(conf->percpu, cpu));
		if (err) {
			pr_err("%s: failed memory allocation for cpu%ld\n",
			       __func__, cpu);
			break;
		}
		spin_lock_init(&per_cpu_ptr(conf->percpu, cpu)->lock);
	}
	put_online_cpus();

	if (!err) {
		conf->scribble_disks = max(conf->raid_disks,
			conf->previous_raid_disks);
		conf->scribble_sectors = max(conf->chunk_sectors,
			conf->prev_chunk_sectors);
	}
	return err;
}

static unsigned long raid5_cache_scan(struct shrinker *shrink,
				      struct shrink_control *sc)
{
	struct r5conf *conf = container_of(shrink, struct r5conf, shrinker);
	unsigned long ret = SHRINK_STOP;

	if (mutex_trylock(&conf->cache_size_mutex)) {
		ret= 0;
		while (ret < sc->nr_to_scan &&
		       conf->max_nr_stripes > conf->min_nr_stripes) {
			if (drop_one_stripe(conf) == 0) {
				ret = SHRINK_STOP;
				break;
			}
			ret++;
		}
		mutex_unlock(&conf->cache_size_mutex);
	}
	return ret;
}

static unsigned long raid5_cache_count(struct shrinker *shrink,
				       struct shrink_control *sc)
{
	struct r5conf *conf = container_of(shrink, struct r5conf, shrinker);

	if (conf->max_nr_stripes < conf->min_nr_stripes)
		/* unlikely, but not impossible */
		return 0;
	return conf->max_nr_stripes - conf->min_nr_stripes;
}

static struct r5conf *setup_conf(struct mddev *mddev)
{
	struct r5conf *conf;
	int raid_disk, memory, max_disks;
	struct md_rdev *rdev;
	struct disk_info *disk;
	char pers_name[6];
	int i;
	int group_cnt, worker_cnt_per_group;
	struct r5worker_group *new_group;

	if (mddev->new_level != 5
	    && mddev->new_level != 4
	    && mddev->new_level != 6) {
		printk(KERN_ERR "md/raid:%s: raid level not set to 4/5/6 (%d)\n",
		       mdname(mddev), mddev->new_level);
		return ERR_PTR(-EIO);
	}
	if ((mddev->new_level == 5
	     && !algorithm_valid_raid5(mddev->new_layout)) ||
	    (mddev->new_level == 6
	     && !algorithm_valid_raid6(mddev->new_layout))) {
		printk(KERN_ERR "md/raid:%s: layout %d not supported\n",
		       mdname(mddev), mddev->new_layout);
		return ERR_PTR(-EIO);
	}
	if (mddev->new_level == 6 && mddev->raid_disks < 4) {
		printk(KERN_ERR "md/raid:%s: not enough configured devices (%d, minimum 4)\n",
		       mdname(mddev), mddev->raid_disks);
		return ERR_PTR(-EINVAL);
	}

	if (!mddev->new_chunk_sectors ||
	    (mddev->new_chunk_sectors << 9) % PAGE_SIZE ||
	    !is_power_of_2(mddev->new_chunk_sectors)) {
		printk(KERN_ERR "md/raid:%s: invalid chunk size %d\n",
		       mdname(mddev), mddev->new_chunk_sectors << 9);
		return ERR_PTR(-EINVAL);
	}

	conf = kzalloc(sizeof(struct r5conf), GFP_KERNEL);
	if (conf == NULL)
		goto abort;
	/* Don't enable multi-threading by default*/
	if (!alloc_thread_groups(conf, 0, &group_cnt, &worker_cnt_per_group,
				 &new_group)) {
		conf->group_cnt = group_cnt;
		conf->worker_cnt_per_group = worker_cnt_per_group;
		conf->worker_groups = new_group;
	} else
		goto abort;
	spin_lock_init(&conf->device_lock);
	seqcount_init(&conf->gen_lock);
	mutex_init(&conf->cache_size_mutex);
	init_waitqueue_head(&conf->wait_for_quiescent);
	init_waitqueue_head(&conf->wait_for_stripe);
	init_waitqueue_head(&conf->wait_for_overlap);
	INIT_LIST_HEAD(&conf->handle_list);
	INIT_LIST_HEAD(&conf->hold_list);
	INIT_LIST_HEAD(&conf->delayed_list);
	INIT_LIST_HEAD(&conf->bitmap_list);
	bio_list_init(&conf->return_bi);
	init_llist_head(&conf->released_stripes);
	atomic_set(&conf->active_stripes, 0);
	atomic_set(&conf->preread_active_stripes, 0);
	atomic_set(&conf->active_aligned_reads, 0);
	conf->bypass_threshold = BYPASS_THRESHOLD;
	conf->recovery_disabled = mddev->recovery_disabled - 1;

	conf->raid_disks = mddev->raid_disks;
	if (mddev->reshape_position == MaxSector)
		conf->previous_raid_disks = mddev->raid_disks;
	else
		conf->previous_raid_disks = mddev->raid_disks - mddev->delta_disks;
	max_disks = max(conf->raid_disks, conf->previous_raid_disks);

	conf->disks = kzalloc(max_disks * sizeof(struct disk_info),
			      GFP_KERNEL);
	if (!conf->disks)
		goto abort;

	conf->mddev = mddev;

	if ((conf->stripe_hashtbl = kzalloc(PAGE_SIZE, GFP_KERNEL)) == NULL)
		goto abort;

	if (mddev->level == 6) {
		conf->spare_page = alloc_page(GFP_KERNEL);
		if (!conf->spare_page)
			goto abort;
	}
	spin_lock_init(&conf->device_lock);
	mddev->queue->queue_lock = &conf->device_lock;
	init_waitqueue_head(&conf->wait_for_stripe);
	init_waitqueue_head(&conf->wait_for_overlap);
	INIT_LIST_HEAD(&conf->handle_list);
	INIT_LIST_HEAD(&conf->hold_list);
	INIT_LIST_HEAD(&conf->delayed_list);
	INIT_LIST_HEAD(&conf->bitmap_list);
	INIT_LIST_HEAD(&conf->inactive_list);
	atomic_set(&conf->active_stripes, 0);
	atomic_set(&conf->preread_active_stripes, 0);
	atomic_set(&conf->active_aligned_reads, 0);
	conf->bypass_threshold = BYPASS_THRESHOLD;

	pr_debug("raid5: run(%s) called.\n", mdname(mddev));

	rdev_for_each(rdev, tmp, mddev) {
		raid_disk = rdev->raid_disk;
		if (raid_disk >= conf->raid_disks
		    || raid_disk < 0)
			continue;
		disk = conf->disks + raid_disk;

		disk->rdev = rdev;

		if (test_bit(In_sync, &rdev->flags)) {
			char b[BDEVNAME_SIZE];
			printk(KERN_INFO "raid5: device %s operational as raid"
				" disk %d\n", bdevname(rdev->bdev,b),
				raid_disk);
			working_disks++;
		} else
	/* We init hash_locks[0] separately to that it can be used
	 * as the reference lock in the spin_lock_nest_lock() call
	 * in lock_all_device_hash_locks_irq in order to convince
	 * lockdep that we know what we are doing.
	 */
	spin_lock_init(conf->hash_locks);
	for (i = 1; i < NR_STRIPE_HASH_LOCKS; i++)
		spin_lock_init(conf->hash_locks + i);

	for (i = 0; i < NR_STRIPE_HASH_LOCKS; i++)
		INIT_LIST_HEAD(conf->inactive_list + i);

	for (i = 0; i < NR_STRIPE_HASH_LOCKS; i++)
		INIT_LIST_HEAD(conf->temp_inactive_list + i);

	conf->level = mddev->new_level;
	conf->chunk_sectors = mddev->new_chunk_sectors;
	if (raid5_alloc_percpu(conf) != 0)
		goto abort;

	pr_debug("raid456: run(%s) called.\n", mdname(mddev));

	rdev_for_each(rdev, mddev) {
		raid_disk = rdev->raid_disk;
		if (raid_disk >= max_disks
		    || raid_disk < 0 || test_bit(Journal, &rdev->flags))
			continue;
		disk = conf->disks + raid_disk;

		if (test_bit(Replacement, &rdev->flags)) {
			if (disk->replacement)
				goto abort;
			disk->replacement = rdev;
		} else {
			if (disk->rdev)
				goto abort;
			disk->rdev = rdev;
		}

		if (test_bit(In_sync, &rdev->flags)) {
			char b[BDEVNAME_SIZE];
			printk(KERN_INFO "md/raid:%s: device %s operational as raid"
			       " disk %d\n",
			       mdname(mddev), bdevname(rdev->bdev, b), raid_disk);
		} else if (rdev->saved_raid_disk != raid_disk)
			/* Cannot rely on bitmap to complete recovery */
			conf->fullsync = 1;
	}

	/*
	 * 0 for a fully functional array, 1 or 2 for a degraded array.
	 */
	mddev->degraded = conf->raid_disks - working_disks;
	conf->mddev = mddev;
	conf->chunk_size = mddev->chunk_size;
	conf->level = mddev->level;
	if (conf->level == 6)
		conf->max_degraded = 2;
	else
		conf->max_degraded = 1;
	conf->algorithm = mddev->layout;
	conf->max_nr_stripes = NR_STRIPES;
	conf->expand_progress = mddev->reshape_position;

	/* device size must be a multiple of chunk size */
	mddev->size &= ~(mddev->chunk_size/1024 -1);
	mddev->resync_max_sectors = mddev->size << 1;

	if (conf->level == 6 && conf->raid_disks < 4) {
		printk(KERN_ERR "raid6: not enough configured devices for %s (%d, minimum 4)\n",
		       mdname(mddev), conf->raid_disks);
		goto abort;
	}
	if (!conf->chunk_size || conf->chunk_size % 4) {
		printk(KERN_ERR "raid5: invalid chunk size %d for %s\n",
			conf->chunk_size, mdname(mddev));
		goto abort;
	}
	if (conf->algorithm > ALGORITHM_RIGHT_SYMMETRIC) {
		printk(KERN_ERR 
			"raid5: unsupported parity algorithm %d for %s\n",
			conf->algorithm, mdname(mddev));
		goto abort;
	}
	if (mddev->degraded > conf->max_degraded) {
		printk(KERN_ERR "raid5: not enough operational devices for %s"
	conf->level = mddev->new_level;
	if (conf->level == 6) {
		conf->max_degraded = 2;
		if (raid6_call.xor_syndrome)
			conf->rmw_level = PARITY_ENABLE_RMW;
		else
			conf->rmw_level = PARITY_DISABLE_RMW;
	} else {
		conf->max_degraded = 1;
		conf->rmw_level = PARITY_ENABLE_RMW;
	}
	conf->algorithm = mddev->new_layout;
	conf->reshape_progress = mddev->reshape_position;
	if (conf->reshape_progress != MaxSector) {
		conf->prev_chunk_sectors = mddev->chunk_sectors;
		conf->prev_algo = mddev->layout;
	} else {
		conf->prev_chunk_sectors = conf->chunk_sectors;
		conf->prev_algo = conf->algorithm;
	}

	conf->min_nr_stripes = NR_STRIPES;
	memory = conf->min_nr_stripes * (sizeof(struct stripe_head) +
		 max_disks * ((sizeof(struct bio) + PAGE_SIZE))) / 1024;
	atomic_set(&conf->empty_inactive_list_nr, NR_STRIPE_HASH_LOCKS);
	if (grow_stripes(conf, conf->min_nr_stripes)) {
		printk(KERN_ERR
		       "md/raid:%s: couldn't allocate %dkB for buffers\n",
		       mdname(mddev), memory);
		goto abort;
	} else
		printk(KERN_INFO "md/raid:%s: allocated %dkB\n",
		       mdname(mddev), memory);
	/*
	 * Losing a stripe head costs more than the time to refill it,
	 * it reduces the queue depth and so can hurt throughput.
	 * So set it rather large, scaled by number of devices.
	 */
	conf->shrinker.seeks = DEFAULT_SEEKS * conf->raid_disks * 4;
	conf->shrinker.scan_objects = raid5_cache_scan;
	conf->shrinker.count_objects = raid5_cache_count;
	conf->shrinker.batch = 128;
	conf->shrinker.flags = 0;
	register_shrinker(&conf->shrinker);

	sprintf(pers_name, "raid%d", mddev->new_level);
	conf->thread = md_register_thread(raid5d, mddev, pers_name);
	if (!conf->thread) {
		printk(KERN_ERR
		       "md/raid:%s: couldn't allocate thread.\n",
		       mdname(mddev));
		goto abort;
	}

	return conf;

 abort:
	if (conf) {
		free_conf(conf);
		return ERR_PTR(-EIO);
	} else
		return ERR_PTR(-ENOMEM);
}

static int only_parity(int raid_disk, int algo, int raid_disks, int max_degraded)
{
	switch (algo) {
	case ALGORITHM_PARITY_0:
		if (raid_disk < max_degraded)
			return 1;
		break;
	case ALGORITHM_PARITY_N:
		if (raid_disk >= raid_disks - max_degraded)
			return 1;
		break;
	case ALGORITHM_PARITY_0_6:
		if (raid_disk == 0 ||
		    raid_disk == raid_disks - 1)
			return 1;
		break;
	case ALGORITHM_LEFT_ASYMMETRIC_6:
	case ALGORITHM_RIGHT_ASYMMETRIC_6:
	case ALGORITHM_LEFT_SYMMETRIC_6:
	case ALGORITHM_RIGHT_SYMMETRIC_6:
		if (raid_disk == raid_disks - 1)
			return 1;
	}
	return 0;
}

static int run(struct mddev *mddev)
{
	struct r5conf *conf;
	int working_disks = 0;
	int dirty_parity_disks = 0;
	struct md_rdev *rdev;
	struct md_rdev *journal_dev = NULL;
	sector_t reshape_offset = 0;
	int i;
	long long min_offset_diff = 0;
	int first = 1;

	if (mddev->recovery_cp != MaxSector)
		printk(KERN_NOTICE "md/raid:%s: not clean"
		       " -- starting background reconstruction\n",
		       mdname(mddev));

	rdev_for_each(rdev, mddev) {
		long long diff;

		if (test_bit(Journal, &rdev->flags)) {
			journal_dev = rdev;
			continue;
		}
		if (rdev->raid_disk < 0)
			continue;
		diff = (rdev->new_data_offset - rdev->data_offset);
		if (first) {
			min_offset_diff = diff;
			first = 0;
		} else if (mddev->reshape_backwards &&
			 diff < min_offset_diff)
			min_offset_diff = diff;
		else if (!mddev->reshape_backwards &&
			 diff > min_offset_diff)
			min_offset_diff = diff;
	}

	if (mddev->reshape_position != MaxSector) {
		/* Check that we can continue the reshape.
		 * Difficulties arise if the stripe we would write to
		 * next is at or after the stripe we would read from next.
		 * For a reshape that changes the number of devices, this
		 * is only possible for a very short time, and mdadm makes
		 * sure that time appears to have past before assembling
		 * the array.  So we fail if that time hasn't passed.
		 * For a reshape that keeps the number of devices the same
		 * mdadm must be monitoring the reshape can keeping the
		 * critical areas read-only and backed up.  It will start
		 * the array in read-only mode, so we check for that.
		 */
		sector_t here_new, here_old;
		int old_disks;
		int max_degraded = (mddev->level == 6 ? 2 : 1);
		int chunk_sectors;
		int new_data_disks;

		if (journal_dev) {
			printk(KERN_ERR "md/raid:%s: don't support reshape with journal - aborting.\n",
			       mdname(mddev));
			return -EINVAL;
		}

		if (mddev->new_level != mddev->level) {
			printk(KERN_ERR "md/raid:%s: unsupported reshape "
			       "required - aborting.\n",
			       mdname(mddev));
			return -EINVAL;
		}
		old_disks = mddev->raid_disks - mddev->delta_disks;
		/* reshape_position must be on a new-stripe boundary, and one
		 * further up in new geometry must map after here in old
		 * geometry.
		 * If the chunk sizes are different, then as we perform reshape
		 * in units of the largest of the two, reshape_position needs
		 * be a multiple of the largest chunk size times new data disks.
		 */
		here_new = mddev->reshape_position;
		chunk_sectors = max(mddev->chunk_sectors, mddev->new_chunk_sectors);
		new_data_disks = mddev->raid_disks - max_degraded;
		if (sector_div(here_new, chunk_sectors * new_data_disks)) {
			printk(KERN_ERR "md/raid:%s: reshape_position not "
			       "on a stripe boundary\n", mdname(mddev));
			return -EINVAL;
		}
		reshape_offset = here_new * chunk_sectors;
		/* here_new is the stripe we will write to */
		here_old = mddev->reshape_position;
		sector_div(here_old, chunk_sectors * (old_disks-max_degraded));
		/* here_old is the first stripe that we might need to read
		 * from */
		if (mddev->delta_disks == 0) {
			/* We cannot be sure it is safe to start an in-place
			 * reshape.  It is only safe if user-space is monitoring
			 * and taking constant backups.
			 * mdadm always starts a situation like this in
			 * readonly mode so it can take control before
			 * allowing any writes.  So just check for that.
			 */
			if (abs(min_offset_diff) >= mddev->chunk_sectors &&
			    abs(min_offset_diff) >= mddev->new_chunk_sectors)
				/* not really in-place - so OK */;
			else if (mddev->ro == 0) {
				printk(KERN_ERR "md/raid:%s: in-place reshape "
				       "must be started in read-only mode "
				       "- aborting\n",
				       mdname(mddev));
				return -EINVAL;
			}
		} else if (mddev->reshape_backwards
		    ? (here_new * chunk_sectors + min_offset_diff <=
		       here_old * chunk_sectors)
		    : (here_new * chunk_sectors >=
		       here_old * chunk_sectors + (-min_offset_diff))) {
			/* Reading from the same stripe as writing to - bad */
			printk(KERN_ERR "md/raid:%s: reshape_position too early for "
			       "auto-recovery - aborting.\n",
			       mdname(mddev));
			return -EINVAL;
		}
		printk(KERN_INFO "md/raid:%s: reshape will continue\n",
		       mdname(mddev));
		/* OK, we should be able to continue; */
	} else {
		BUG_ON(mddev->level != mddev->new_level);
		BUG_ON(mddev->layout != mddev->new_layout);
		BUG_ON(mddev->chunk_sectors != mddev->new_chunk_sectors);
		BUG_ON(mddev->delta_disks != 0);
	}

	if (mddev->private == NULL)
		conf = setup_conf(mddev);
	else
		conf = mddev->private;

	if (IS_ERR(conf))
		return PTR_ERR(conf);

	if (test_bit(MD_HAS_JOURNAL, &mddev->flags) && !journal_dev) {
		printk(KERN_ERR "md/raid:%s: journal disk is missing, force array readonly\n",
		       mdname(mddev));
		mddev->ro = 1;
		set_disk_ro(mddev->gendisk, 1);
	}

	conf->min_offset_diff = min_offset_diff;
	mddev->thread = conf->thread;
	conf->thread = NULL;
	mddev->private = conf;

	for (i = 0; i < conf->raid_disks && conf->previous_raid_disks;
	     i++) {
		rdev = conf->disks[i].rdev;
		if (!rdev && conf->disks[i].replacement) {
			/* The replacement is all we have yet */
			rdev = conf->disks[i].replacement;
			conf->disks[i].replacement = NULL;
			clear_bit(Replacement, &rdev->flags);
			conf->disks[i].rdev = rdev;
		}
		if (!rdev)
			continue;
		if (conf->disks[i].replacement &&
		    conf->reshape_progress != MaxSector) {
			/* replacements and reshape simply do not mix. */
			printk(KERN_ERR "md: cannot handle concurrent "
			       "replacement and reshape.\n");
			goto abort;
		}
		if (test_bit(In_sync, &rdev->flags)) {
			working_disks++;
			continue;
		}
		/* This disc is not fully in-sync.  However if it
		 * just stored parity (beyond the recovery_offset),
		 * when we don't need to be concerned about the
		 * array being dirty.
		 * When reshape goes 'backwards', we never have
		 * partially completed devices, so we only need
		 * to worry about reshape going forwards.
		 */
		/* Hack because v0.91 doesn't store recovery_offset properly. */
		if (mddev->major_version == 0 &&
		    mddev->minor_version > 90)
			rdev->recovery_offset = reshape_offset;

		if (rdev->recovery_offset < reshape_offset) {
			/* We need to check old and new layout */
			if (!only_parity(rdev->raid_disk,
					 conf->algorithm,
					 conf->raid_disks,
					 conf->max_degraded))
				continue;
		}
		if (!only_parity(rdev->raid_disk,
				 conf->prev_algo,
				 conf->previous_raid_disks,
				 conf->max_degraded))
			continue;
		dirty_parity_disks++;
	}

	/*
	 * 0 for a fully functional array, 1 or 2 for a degraded array.
	 */
	mddev->degraded = calc_degraded(conf);

	if (has_failed(conf)) {
		printk(KERN_ERR "md/raid:%s: not enough operational devices"
			" (%d/%d failed)\n",
			mdname(mddev), mddev->degraded, conf->raid_disks);
		goto abort;
	}

	if (mddev->degraded > 0 &&
	    mddev->recovery_cp != MaxSector) {
		if (mddev->ok_start_degraded)
			printk(KERN_WARNING
			       "raid5: starting dirty degraded array: %s"
			       "- data corruption possible.\n",
			       mdname(mddev));
		else {
			printk(KERN_ERR
			       "raid5: cannot start dirty degraded array for %s\n",
	/* device size must be a multiple of chunk size */
	mddev->dev_sectors &= ~(mddev->chunk_sectors - 1);
	mddev->resync_max_sectors = mddev->dev_sectors;

	if (mddev->degraded > dirty_parity_disks &&
	    mddev->recovery_cp != MaxSector) {
		if (mddev->ok_start_degraded)
			printk(KERN_WARNING
			       "md/raid:%s: starting dirty degraded array"
			       " - data corruption possible.\n",
			       mdname(mddev));
		else {
			printk(KERN_ERR
			       "md/raid:%s: cannot start dirty degraded array.\n",
			       mdname(mddev));
			goto abort;
		}
	}

	{
		mddev->thread = md_register_thread(raid5d, mddev, "%s_raid5");
		if (!mddev->thread) {
			printk(KERN_ERR 
				"raid5: couldn't allocate thread for %s\n",
				mdname(mddev));
			goto abort;
		}
	}
	memory = conf->max_nr_stripes * (sizeof(struct stripe_head) +
		 conf->raid_disks * ((sizeof(struct bio) + PAGE_SIZE))) / 1024;
	if (grow_stripes(conf, conf->max_nr_stripes)) {
		printk(KERN_ERR 
			"raid5: couldn't allocate %dkB for buffers\n", memory);
		shrink_stripes(conf);
		md_unregister_thread(mddev->thread);
		goto abort;
	} else
		printk(KERN_INFO "raid5: allocated %dkB for %s\n",
			memory, mdname(mddev));

	if (mddev->degraded == 0)
		printk("raid5: raid level %d set %s active with %d out of %d"
			" devices, algorithm %d\n", conf->level, mdname(mddev), 
			mddev->raid_disks-mddev->degraded, mddev->raid_disks,
			conf->algorithm);
	else
		printk(KERN_ALERT "raid5: raid level %d set %s active with %d"
			" out of %d devices, algorithm %d\n", conf->level,
			mdname(mddev), mddev->raid_disks - mddev->degraded,
			mddev->raid_disks, conf->algorithm);

	print_raid5_conf(conf);

	if (conf->expand_progress != MaxSector) {
		printk("...ok start reshape thread\n");
		conf->expand_lo = conf->expand_progress;
	if (mddev->degraded == 0)
		printk(KERN_INFO "md/raid:%s: raid level %d active with %d out of %d"
		       " devices, algorithm %d\n", mdname(mddev), conf->level,
		       mddev->raid_disks-mddev->degraded, mddev->raid_disks,
		       mddev->new_layout);
	else
		printk(KERN_ALERT "md/raid:%s: raid level %d active with %d"
		       " out of %d devices, algorithm %d\n",
		       mdname(mddev), conf->level,
		       mddev->raid_disks - mddev->degraded,
		       mddev->raid_disks, mddev->new_layout);

	print_raid5_conf(conf);

	if (conf->reshape_progress != MaxSector) {
		conf->reshape_safe = conf->reshape_progress;
		atomic_set(&conf->reshape_stripes, 0);
		clear_bit(MD_RECOVERY_SYNC, &mddev->recovery);
		clear_bit(MD_RECOVERY_CHECK, &mddev->recovery);
		set_bit(MD_RECOVERY_RESHAPE, &mddev->recovery);
		set_bit(MD_RECOVERY_RUNNING, &mddev->recovery);
		mddev->sync_thread = md_register_thread(md_do_sync, mddev,
							"%s_reshape");
	}

	/* read-ahead size must cover two whole stripes, which is
	 * 2 * (datadisks) * chunksize where 'n' is the number of raid devices
	 */
	{
		int data_disks = conf->previous_raid_disks - conf->max_degraded;
		int stripe = data_disks *
			(mddev->chunk_size / PAGE_SIZE);
		if (mddev->queue->backing_dev_info.ra_pages < 2 * stripe)
			mddev->queue->backing_dev_info.ra_pages = 2 * stripe;
	}

	/* Ok, everything is just fine now */
	if (sysfs_create_group(&mddev->kobj, &raid5_attrs_group))
		printk(KERN_WARNING
		       "raid5: failed to create sysfs attributes for %s\n",
		       mdname(mddev));

	mddev->queue->unplug_fn = raid5_unplug_device;
	mddev->queue->backing_dev_info.congested_data = mddev;
	mddev->queue->backing_dev_info.congested_fn = raid5_congested;

	mddev->array_sectors = 2 * mddev->size * (conf->previous_raid_disks -
					    conf->max_degraded);

	blk_queue_merge_bvec(mddev->queue, raid5_mergeable_bvec);

	return 0;
abort:
	if (conf) {
		print_raid5_conf(conf);
		safe_put_page(conf->spare_page);
		kfree(conf->disks);
		kfree(conf->stripe_hashtbl);
		kfree(conf);
	}
	mddev->private = NULL;
	printk(KERN_ALERT "raid5: failed to run raid set %s\n", mdname(mddev));
	return -EIO;
}



static int stop(mddev_t *mddev)
{
	raid5_conf_t *conf = (raid5_conf_t *) mddev->private;

	md_unregister_thread(mddev->thread);
	mddev->thread = NULL;
	shrink_stripes(conf);
	kfree(conf->stripe_hashtbl);
	mddev->queue->backing_dev_info.congested_fn = NULL;
	blk_sync_queue(mddev->queue); /* the unplug fn references 'conf'*/
	sysfs_remove_group(&mddev->kobj, &raid5_attrs_group);
	kfree(conf->disks);
	kfree(conf);
	mddev->private = NULL;
	return 0;
}

#ifdef DEBUG
static void print_sh (struct seq_file *seq, struct stripe_head *sh)
{
	int i;

	seq_printf(seq, "sh %llu, pd_idx %d, state %ld.\n",
		   (unsigned long long)sh->sector, sh->pd_idx, sh->state);
	seq_printf(seq, "sh %llu,  count %d.\n",
		   (unsigned long long)sh->sector, atomic_read(&sh->count));
	seq_printf(seq, "sh %llu, ", (unsigned long long)sh->sector);
	for (i = 0; i < sh->disks; i++) {
		seq_printf(seq, "(cache%d: %p %ld) ",
			   i, sh->dev[i].page, sh->dev[i].flags);
	}
	seq_printf(seq, "\n");
}

static void printall (struct seq_file *seq, raid5_conf_t *conf)
{
	struct stripe_head *sh;
	struct hlist_node *hn;
	int i;

	spin_lock_irq(&conf->device_lock);
	for (i = 0; i < NR_HASH; i++) {
		hlist_for_each_entry(sh, hn, &conf->stripe_hashtbl[i], hash) {
			if (sh->raid_conf != conf)
				continue;
			print_sh(seq, sh);
		}
	}
	spin_unlock_irq(&conf->device_lock);
}
#endif

static void status (struct seq_file *seq, mddev_t *mddev)
{
	raid5_conf_t *conf = (raid5_conf_t *) mddev->private;
	int i;

	seq_printf (seq, " level %d, %dk chunk, algorithm %d", mddev->level, mddev->chunk_size >> 10, mddev->layout);
							"reshape");
		if (!mddev->sync_thread)
			goto abort;
	}

	/* Ok, everything is just fine now */
	if (mddev->to_remove == &raid5_attrs_group)
		mddev->to_remove = NULL;
	else if (mddev->kobj.sd &&
	    sysfs_create_group(&mddev->kobj, &raid5_attrs_group))
		printk(KERN_WARNING
		       "raid5: failed to create sysfs attributes for %s\n",
		       mdname(mddev));
	md_set_array_sectors(mddev, raid5_size(mddev, 0, 0));

	if (mddev->queue) {
		int chunk_size;
		bool discard_supported = true;
		/* read-ahead size must cover two whole stripes, which
		 * is 2 * (datadisks) * chunksize where 'n' is the
		 * number of raid devices
		 */
		int data_disks = conf->previous_raid_disks - conf->max_degraded;
		int stripe = data_disks *
			((mddev->chunk_sectors << 9) / PAGE_SIZE);
		if (mddev->queue->backing_dev_info.ra_pages < 2 * stripe)
			mddev->queue->backing_dev_info.ra_pages = 2 * stripe;

		chunk_size = mddev->chunk_sectors << 9;
		blk_queue_io_min(mddev->queue, chunk_size);
		blk_queue_io_opt(mddev->queue, chunk_size *
				 (conf->raid_disks - conf->max_degraded));
		mddev->queue->limits.raid_partial_stripes_expensive = 1;
		/*
		 * We can only discard a whole stripe. It doesn't make sense to
		 * discard data disk but write parity disk
		 */
		stripe = stripe * PAGE_SIZE;
		/* Round up to power of 2, as discard handling
		 * currently assumes that */
		while ((stripe-1) & stripe)
			stripe = (stripe | (stripe-1)) + 1;
		mddev->queue->limits.discard_alignment = stripe;
		mddev->queue->limits.discard_granularity = stripe;

		/*
		 * We use 16-bit counter of active stripes in bi_phys_segments
		 * (minus one for over-loaded initialization)
		 */
		blk_queue_max_hw_sectors(mddev->queue, 0xfffe * STRIPE_SECTORS);
		blk_queue_max_discard_sectors(mddev->queue,
					      0xfffe * STRIPE_SECTORS);

		/*
		 * unaligned part of discard request will be ignored, so can't
		 * guarantee discard_zeroes_data
		 */
		mddev->queue->limits.discard_zeroes_data = 0;

		blk_queue_max_write_same_sectors(mddev->queue, 0);

		rdev_for_each(rdev, mddev) {
			disk_stack_limits(mddev->gendisk, rdev->bdev,
					  rdev->data_offset << 9);
			disk_stack_limits(mddev->gendisk, rdev->bdev,
					  rdev->new_data_offset << 9);
			/*
			 * discard_zeroes_data is required, otherwise data
			 * could be lost. Consider a scenario: discard a stripe
			 * (the stripe could be inconsistent if
			 * discard_zeroes_data is 0); write one disk of the
			 * stripe (the stripe could be inconsistent again
			 * depending on which disks are used to calculate
			 * parity); the disk is broken; The stripe data of this
			 * disk is lost.
			 */
			if (!blk_queue_discard(bdev_get_queue(rdev->bdev)) ||
			    !bdev_get_queue(rdev->bdev)->
						limits.discard_zeroes_data)
				discard_supported = false;
			/* Unfortunately, discard_zeroes_data is not currently
			 * a guarantee - just a hint.  So we only allow DISCARD
			 * if the sysadmin has confirmed that only safe devices
			 * are in use by setting a module parameter.
			 */
			if (!devices_handle_discard_safely) {
				if (discard_supported) {
					pr_info("md/raid456: discard support disabled due to uncertainty.\n");
					pr_info("Set raid456.devices_handle_discard_safely=Y to override.\n");
				}
				discard_supported = false;
			}
		}

		if (discard_supported &&
		    mddev->queue->limits.max_discard_sectors >= (stripe >> 9) &&
		    mddev->queue->limits.discard_granularity >= stripe)
			queue_flag_set_unlocked(QUEUE_FLAG_DISCARD,
						mddev->queue);
		else
			queue_flag_clear_unlocked(QUEUE_FLAG_DISCARD,
						mddev->queue);
	}

	if (journal_dev) {
		char b[BDEVNAME_SIZE];

		printk(KERN_INFO"md/raid:%s: using device %s as journal\n",
		       mdname(mddev), bdevname(journal_dev->bdev, b));
		r5l_init_log(conf, journal_dev);
	}

	return 0;
abort:
	md_unregister_thread(&mddev->thread);
	print_raid5_conf(conf);
	free_conf(conf);
	mddev->private = NULL;
	printk(KERN_ALERT "md/raid:%s: failed to run raid set.\n", mdname(mddev));
	return -EIO;
}

static void raid5_free(struct mddev *mddev, void *priv)
{
	struct r5conf *conf = priv;

	free_conf(conf);
	mddev->to_remove = &raid5_attrs_group;
}

static void status(struct seq_file *seq, struct mddev *mddev)
{
	struct r5conf *conf = mddev->private;
	int i;

	seq_printf(seq, " level %d, %dk chunk, algorithm %d", mddev->level,
		conf->chunk_sectors / 2, mddev->layout);
	seq_printf (seq, " [%d/%d] [", conf->raid_disks, conf->raid_disks - mddev->degraded);
	for (i = 0; i < conf->raid_disks; i++)
		seq_printf (seq, "%s",
			       conf->disks[i].rdev &&
			       test_bit(In_sync, &conf->disks[i].rdev->flags) ? "U" : "_");
	seq_printf (seq, "]");
#ifdef DEBUG
	seq_printf (seq, "\n");
	printall(seq, conf);
#endif
}

static void print_raid5_conf (raid5_conf_t *conf)
}

static void print_raid5_conf (struct r5conf *conf)
{
	int i;
	struct disk_info *tmp;

	printk("RAID5 conf printout:\n");
	printk(KERN_DEBUG "RAID conf printout:\n");
	if (!conf) {
		printk("(conf==NULL)\n");
		return;
	}
	printk(" --- rd:%d wd:%d\n", conf->raid_disks,
		 conf->raid_disks - conf->mddev->degraded);
	printk(KERN_DEBUG " --- level:%d rd:%d wd:%d\n", conf->level,
	       conf->raid_disks,
	       conf->raid_disks - conf->mddev->degraded);

	for (i = 0; i < conf->raid_disks; i++) {
		char b[BDEVNAME_SIZE];
		tmp = conf->disks + i;
		if (tmp->rdev)
		printk(" disk %d, o:%d, dev:%s\n",
			i, !test_bit(Faulty, &tmp->rdev->flags),
			bdevname(tmp->rdev->bdev,b));
	}
}

static int raid5_spare_active(mddev_t *mddev)
{
	int i;
	raid5_conf_t *conf = mddev->private;
	struct disk_info *tmp;

	for (i = 0; i < conf->raid_disks; i++) {
		tmp = conf->disks + i;
		if (tmp->rdev
		    && !test_bit(Faulty, &tmp->rdev->flags)
		    && !test_and_set_bit(In_sync, &tmp->rdev->flags)) {
			unsigned long flags;
			spin_lock_irqsave(&conf->device_lock, flags);
			mddev->degraded--;
			spin_unlock_irqrestore(&conf->device_lock, flags);
		}
	}
	print_raid5_conf(conf);
	return 0;
}

static int raid5_remove_disk(mddev_t *mddev, int number)
{
	raid5_conf_t *conf = mddev->private;
	int err = 0;
	mdk_rdev_t *rdev;
	struct disk_info *p = conf->disks + number;

	print_raid5_conf(conf);
	rdev = p->rdev;
	if (rdev) {
		if (test_bit(In_sync, &rdev->flags) ||
		    atomic_read(&rdev->nr_pending)) {
			err = -EBUSY;
			goto abort;
		}
		/* Only remove non-faulty devices if recovery
		 * isn't possible.
		 */
		if (!test_bit(Faulty, &rdev->flags) &&
		    mddev->degraded <= conf->max_degraded) {
			err = -EBUSY;
			goto abort;
		}
		p->rdev = NULL;
		synchronize_rcu();
		if (atomic_read(&rdev->nr_pending)) {
			/* lost the race, try later */
			err = -EBUSY;
			p->rdev = rdev;
		}
	}
			printk(KERN_DEBUG " disk %d, o:%d, dev:%s\n",
			       i, !test_bit(Faulty, &tmp->rdev->flags),
			       bdevname(tmp->rdev->bdev, b));
	}
}

static int raid5_spare_active(struct mddev *mddev)
{
	int i;
	struct r5conf *conf = mddev->private;
	struct disk_info *tmp;
	int count = 0;
	unsigned long flags;

	for (i = 0; i < conf->raid_disks; i++) {
		tmp = conf->disks + i;
		if (tmp->replacement
		    && tmp->replacement->recovery_offset == MaxSector
		    && !test_bit(Faulty, &tmp->replacement->flags)
		    && !test_and_set_bit(In_sync, &tmp->replacement->flags)) {
			/* Replacement has just become active. */
			if (!tmp->rdev
			    || !test_and_clear_bit(In_sync, &tmp->rdev->flags))
				count++;
			if (tmp->rdev) {
				/* Replaced device not technically faulty,
				 * but we need to be sure it gets removed
				 * and never re-added.
				 */
				set_bit(Faulty, &tmp->rdev->flags);
				sysfs_notify_dirent_safe(
					tmp->rdev->sysfs_state);
			}
			sysfs_notify_dirent_safe(tmp->replacement->sysfs_state);
		} else if (tmp->rdev
		    && tmp->rdev->recovery_offset == MaxSector
		    && !test_bit(Faulty, &tmp->rdev->flags)
		    && !test_and_set_bit(In_sync, &tmp->rdev->flags)) {
			count++;
			sysfs_notify_dirent_safe(tmp->rdev->sysfs_state);
		}
	}
	spin_lock_irqsave(&conf->device_lock, flags);
	mddev->degraded = calc_degraded(conf);
	spin_unlock_irqrestore(&conf->device_lock, flags);
	print_raid5_conf(conf);
	return count;
}

static int raid5_remove_disk(struct mddev *mddev, struct md_rdev *rdev)
{
	struct r5conf *conf = mddev->private;
	int err = 0;
	int number = rdev->raid_disk;
	struct md_rdev **rdevp;
	struct disk_info *p = conf->disks + number;

	print_raid5_conf(conf);
	if (test_bit(Journal, &rdev->flags)) {
		/*
		 * journal disk is not removable, but we need give a chance to
		 * update superblock of other disks. Otherwise journal disk
		 * will be considered as 'fresh'
		 */
		set_bit(MD_CHANGE_DEVS, &mddev->flags);
		return -EINVAL;
	}
	if (rdev == p->rdev)
		rdevp = &p->rdev;
	else if (rdev == p->replacement)
		rdevp = &p->replacement;
	else
		return 0;

	if (number >= conf->raid_disks &&
	    conf->reshape_progress == MaxSector)
		clear_bit(In_sync, &rdev->flags);

	if (test_bit(In_sync, &rdev->flags) ||
	    atomic_read(&rdev->nr_pending)) {
		err = -EBUSY;
		goto abort;
	}
	/* Only remove non-faulty devices if recovery
	 * isn't possible.
	 */
	if (!test_bit(Faulty, &rdev->flags) &&
	    mddev->recovery_disabled != conf->recovery_disabled &&
	    !has_failed(conf) &&
	    (!p->replacement || p->replacement == rdev) &&
	    number < conf->raid_disks) {
		err = -EBUSY;
		goto abort;
	}
	*rdevp = NULL;
	synchronize_rcu();
	if (atomic_read(&rdev->nr_pending)) {
		/* lost the race, try later */
		err = -EBUSY;
		*rdevp = rdev;
	} else if (p->replacement) {
		/* We must have just cleared 'rdev' */
		p->rdev = p->replacement;
		clear_bit(Replacement, &p->replacement->flags);
		smp_mb(); /* Make sure other CPUs may see both as identical
			   * but will never see neither - if they are careful
			   */
		p->replacement = NULL;
		clear_bit(WantReplacement, &rdev->flags);
	} else
		/* We might have just removed the Replacement as faulty-
		 * clear the bit just in case
		 */
		clear_bit(WantReplacement, &rdev->flags);
abort:

	print_raid5_conf(conf);
	return err;
}

static int raid5_add_disk(mddev_t *mddev, mdk_rdev_t *rdev)
{
	raid5_conf_t *conf = mddev->private;
static int raid5_add_disk(struct mddev *mddev, struct md_rdev *rdev)
{
	struct r5conf *conf = mddev->private;
	int err = -EEXIST;
	int disk;
	struct disk_info *p;
	int first = 0;
	int last = conf->raid_disks - 1;

	if (mddev->degraded > conf->max_degraded)
	if (test_bit(Journal, &rdev->flags))
		return -EINVAL;
	if (mddev->recovery_disabled == conf->recovery_disabled)
		return -EBUSY;

	if (rdev->saved_raid_disk < 0 && has_failed(conf))
		/* no point adding a device */
		return -EINVAL;

	if (rdev->raid_disk >= 0)
		first = last = rdev->raid_disk;

	/*
	 * find the disk ... but prefer rdev->saved_raid_disk
	 * if possible.
	 */
	if (rdev->saved_raid_disk >= 0 &&
	    rdev->saved_raid_disk >= first &&
	    rdev->saved_raid_disk <= last &&
	    conf->disks[rdev->saved_raid_disk].rdev == NULL)
		disk = rdev->saved_raid_disk;
	else
		disk = first;
	for ( ; disk <= last ; disk++)
		if ((p=conf->disks + disk)->rdev == NULL) {
		first = rdev->saved_raid_disk;

	for (disk = first; disk <= last; disk++) {
		p = conf->disks + disk;
		if (p->rdev == NULL) {
			clear_bit(In_sync, &rdev->flags);
			rdev->raid_disk = disk;
			err = 0;
			if (rdev->saved_raid_disk != disk)
				conf->fullsync = 1;
			rcu_assign_pointer(p->rdev, rdev);
			break;
		}
			goto out;
		}
	}
	for (disk = first; disk <= last; disk++) {
		p = conf->disks + disk;
		if (test_bit(WantReplacement, &p->rdev->flags) &&
		    p->replacement == NULL) {
			clear_bit(In_sync, &rdev->flags);
			set_bit(Replacement, &rdev->flags);
			rdev->raid_disk = disk;
			err = 0;
			conf->fullsync = 1;
			rcu_assign_pointer(p->replacement, rdev);
			break;
		}
	}
out:
	print_raid5_conf(conf);
	return err;
}

static int raid5_resize(mddev_t *mddev, sector_t sectors)
static int raid5_resize(struct mddev *mddev, sector_t sectors)
{
	/* no resync is happening, and there is enough space
	 * on all devices, so we can resize.
	 * We need to make sure resync covers any new space.
	 * If the array is shrinking we should possibly wait until
	 * any io in the removed space completes, but it hardly seems
	 * worth it.
	 */
	raid5_conf_t *conf = mddev_to_conf(mddev);

	sectors &= ~((sector_t)mddev->chunk_size/512 - 1);
	mddev->array_sectors = sectors * (mddev->raid_disks
					  - conf->max_degraded);
	set_capacity(mddev->gendisk, mddev->array_sectors);
	mddev->changed = 1;
	if (sectors/2  > mddev->size && mddev->recovery_cp == MaxSector) {
		mddev->recovery_cp = mddev->size << 1;
		set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
	}
	mddev->size = sectors /2;
	sector_t newsize;
	struct r5conf *conf = mddev->private;

	if (conf->log)
		return -EINVAL;
	sectors &= ~((sector_t)conf->chunk_sectors - 1);
	newsize = raid5_size(mddev, sectors, mddev->raid_disks);
	if (mddev->external_size &&
	    mddev->array_sectors > newsize)
		return -EINVAL;
	if (mddev->bitmap) {
		int ret = bitmap_resize(mddev->bitmap, sectors, 0, 0);
		if (ret)
			return ret;
	}
	md_set_array_sectors(mddev, newsize);
	set_capacity(mddev->gendisk, mddev->array_sectors);
	revalidate_disk(mddev->gendisk);
	if (sectors > mddev->dev_sectors &&
	    mddev->recovery_cp > mddev->dev_sectors) {
		mddev->recovery_cp = mddev->dev_sectors;
		set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
	}
	mddev->dev_sectors = sectors;
	mddev->resync_max_sectors = sectors;
	return 0;
}

#ifdef CONFIG_MD_RAID5_RESHAPE
static int raid5_check_reshape(mddev_t *mddev)
{
	raid5_conf_t *conf = mddev_to_conf(mddev);
	int err;

	if (mddev->delta_disks < 0 ||
	    mddev->new_level != mddev->level)
		return -EINVAL; /* Cannot shrink array or change level yet */
	if (mddev->delta_disks == 0)
		return 0; /* nothing to do */
	if (mddev->bitmap)
		/* Cannot grow a bitmap yet */
		return -EBUSY;

static int check_stripe_cache(struct mddev *mddev)
{
	/* Can only proceed if there are plenty of stripe_heads.
	 * We need a minimum of one full stripe,, and for sensible progress
	 * it is best to have about 4 times that.
	 * If we require 4 times, then the default 256 4K stripe_heads will
	 * allow for chunk sizes up to 256K, which is probably OK.
	 * If the chunk size is greater, user-space should request more
	 * stripe_heads first.
	 */
	if ((mddev->chunk_size / STRIPE_SIZE) * 4 > conf->max_nr_stripes ||
	    (mddev->new_chunk / STRIPE_SIZE) * 4 > conf->max_nr_stripes) {
		printk(KERN_WARNING "raid5: reshape: not enough stripes.  Needed %lu\n",
		       (mddev->chunk_size / STRIPE_SIZE)*4);
		return -ENOSPC;
	}

	err = resize_stripes(conf, conf->raid_disks + mddev->delta_disks);
	if (err)
		return err;

	if (mddev->degraded > conf->max_degraded)
		return -EINVAL;
	/* looks like we might be able to manage this */
	return 0;
}

static int raid5_start_reshape(mddev_t *mddev)
{
	raid5_conf_t *conf = mddev_to_conf(mddev);
	mdk_rdev_t *rdev;
	struct list_head *rtmp;
	int spares = 0;
	int added_devices = 0;
	struct r5conf *conf = mddev->private;
	if (((mddev->chunk_sectors << 9) / STRIPE_SIZE) * 4
	    > conf->min_nr_stripes ||
	    ((mddev->new_chunk_sectors << 9) / STRIPE_SIZE) * 4
	    > conf->min_nr_stripes) {
		printk(KERN_WARNING "md/raid:%s: reshape: not enough stripes.  Needed %lu\n",
		       mdname(mddev),
		       ((max(mddev->chunk_sectors, mddev->new_chunk_sectors) << 9)
			/ STRIPE_SIZE)*4);
		return 0;
	}
	return 1;
}

static int check_reshape(struct mddev *mddev)
{
	struct r5conf *conf = mddev->private;

	if (conf->log)
		return -EINVAL;
	if (mddev->delta_disks == 0 &&
	    mddev->new_layout == mddev->layout &&
	    mddev->new_chunk_sectors == mddev->chunk_sectors)
		return 0; /* nothing to do */
	if (has_failed(conf))
		return -EINVAL;
	if (mddev->delta_disks < 0 && mddev->reshape_position == MaxSector) {
		/* We might be able to shrink, but the devices must
		 * be made bigger first.
		 * For raid6, 4 is the minimum size.
		 * Otherwise 2 is the minimum
		 */
		int min = 2;
		if (mddev->level == 6)
			min = 4;
		if (mddev->raid_disks + mddev->delta_disks < min)
			return -EINVAL;
	}

	if (!check_stripe_cache(mddev))
		return -ENOSPC;

	if (mddev->new_chunk_sectors > mddev->chunk_sectors ||
	    mddev->delta_disks > 0)
		if (resize_chunks(conf,
				  conf->previous_raid_disks
				  + max(0, mddev->delta_disks),
				  max(mddev->new_chunk_sectors,
				      mddev->chunk_sectors)
			    ) < 0)
			return -ENOMEM;
	return resize_stripes(conf, (conf->previous_raid_disks
				     + mddev->delta_disks));
}

static int raid5_start_reshape(struct mddev *mddev)
{
	struct r5conf *conf = mddev->private;
	struct md_rdev *rdev;
	int spares = 0;
	unsigned long flags;

	if (test_bit(MD_RECOVERY_RUNNING, &mddev->recovery))
		return -EBUSY;

	rdev_for_each(rdev, rtmp, mddev)
		if (rdev->raid_disk < 0 &&
		    !test_bit(Faulty, &rdev->flags))
			spares++;
	if (!check_stripe_cache(mddev))
		return -ENOSPC;

	if (has_failed(conf))
		return -EINVAL;

	rdev_for_each(rdev, mddev) {
		if (!test_bit(In_sync, &rdev->flags)
		    && !test_bit(Faulty, &rdev->flags))
			spares++;
	}

	if (spares - mddev->degraded < mddev->delta_disks - conf->max_degraded)
		/* Not enough devices even to make a degraded array
		 * of that size
		 */
		return -EINVAL;

	atomic_set(&conf->reshape_stripes, 0);
	spin_lock_irq(&conf->device_lock);
	conf->previous_raid_disks = conf->raid_disks;
	conf->raid_disks += mddev->delta_disks;
	conf->expand_progress = 0;
	conf->expand_lo = 0;
	spin_unlock_irq(&conf->device_lock);

	/* Add some new drives, as many as will fit.
	 * We know there are enough to make the newly sized array work.
	 */
	rdev_for_each(rdev, rtmp, mddev)
		if (rdev->raid_disk < 0 &&
		    !test_bit(Faulty, &rdev->flags)) {
			if (raid5_add_disk(mddev, rdev) == 0) {
				char nm[20];
				set_bit(In_sync, &rdev->flags);
				added_devices++;
				rdev->recovery_offset = 0;
				sprintf(nm, "rd%d", rdev->raid_disk);
				if (sysfs_create_link(&mddev->kobj,
						      &rdev->kobj, nm))
					printk(KERN_WARNING
					       "raid5: failed to create "
					       " link %s for %s\n",
					       nm, mdname(mddev));
			} else
				break;
		}

	spin_lock_irqsave(&conf->device_lock, flags);
	mddev->degraded = (conf->raid_disks - conf->previous_raid_disks) - added_devices;
	spin_unlock_irqrestore(&conf->device_lock, flags);
	mddev->raid_disks = conf->raid_disks;
	mddev->reshape_position = 0;
	/* Refuse to reduce size of the array.  Any reductions in
	 * array size must be through explicit setting of array_size
	 * attribute.
	 */
	if (raid5_size(mddev, 0, conf->raid_disks + mddev->delta_disks)
	    < mddev->array_sectors) {
		printk(KERN_ERR "md/raid:%s: array size must be reduced "
		       "before number of disks\n", mdname(mddev));
		return -EINVAL;
	}

	atomic_set(&conf->reshape_stripes, 0);
	spin_lock_irq(&conf->device_lock);
	write_seqcount_begin(&conf->gen_lock);
	conf->previous_raid_disks = conf->raid_disks;
	conf->raid_disks += mddev->delta_disks;
	conf->prev_chunk_sectors = conf->chunk_sectors;
	conf->chunk_sectors = mddev->new_chunk_sectors;
	conf->prev_algo = conf->algorithm;
	conf->algorithm = mddev->new_layout;
	conf->generation++;
	/* Code that selects data_offset needs to see the generation update
	 * if reshape_progress has been set - so a memory barrier needed.
	 */
	smp_mb();
	if (mddev->reshape_backwards)
		conf->reshape_progress = raid5_size(mddev, 0, 0);
	else
		conf->reshape_progress = 0;
	conf->reshape_safe = conf->reshape_progress;
	write_seqcount_end(&conf->gen_lock);
	spin_unlock_irq(&conf->device_lock);

	/* Now make sure any requests that proceeded on the assumption
	 * the reshape wasn't running - like Discard or Read - have
	 * completed.
	 */
	mddev_suspend(mddev);
	mddev_resume(mddev);

	/* Add some new drives, as many as will fit.
	 * We know there are enough to make the newly sized array work.
	 * Don't add devices if we are reducing the number of
	 * devices in the array.  This is because it is not possible
	 * to correctly record the "partially reconstructed" state of
	 * such devices during the reshape and confusion could result.
	 */
	if (mddev->delta_disks >= 0) {
		rdev_for_each(rdev, mddev)
			if (rdev->raid_disk < 0 &&
			    !test_bit(Faulty, &rdev->flags)) {
				if (raid5_add_disk(mddev, rdev) == 0) {
					if (rdev->raid_disk
					    >= conf->previous_raid_disks)
						set_bit(In_sync, &rdev->flags);
					else
						rdev->recovery_offset = 0;

					if (sysfs_link_rdev(mddev, rdev))
						/* Failure here is OK */;
				}
			} else if (rdev->raid_disk >= conf->previous_raid_disks
				   && !test_bit(Faulty, &rdev->flags)) {
				/* This is a spare that was manually added */
				set_bit(In_sync, &rdev->flags);
			}

		/* When a reshape changes the number of devices,
		 * ->degraded is measured against the larger of the
		 * pre and post number of devices.
		 */
		spin_lock_irqsave(&conf->device_lock, flags);
		mddev->degraded = calc_degraded(conf);
		spin_unlock_irqrestore(&conf->device_lock, flags);
	}
	mddev->raid_disks = conf->raid_disks;
	mddev->reshape_position = conf->reshape_progress;
	set_bit(MD_CHANGE_DEVS, &mddev->flags);

	clear_bit(MD_RECOVERY_SYNC, &mddev->recovery);
	clear_bit(MD_RECOVERY_CHECK, &mddev->recovery);
	set_bit(MD_RECOVERY_RESHAPE, &mddev->recovery);
	set_bit(MD_RECOVERY_RUNNING, &mddev->recovery);
	mddev->sync_thread = md_register_thread(md_do_sync, mddev,
						"%s_reshape");
	if (!mddev->sync_thread) {
		mddev->recovery = 0;
		spin_lock_irq(&conf->device_lock);
		mddev->raid_disks = conf->raid_disks = conf->previous_raid_disks;
		conf->expand_progress = MaxSector;
		spin_unlock_irq(&conf->device_lock);
		return -EAGAIN;
	}
	clear_bit(MD_RECOVERY_DONE, &mddev->recovery);
	set_bit(MD_RECOVERY_RESHAPE, &mddev->recovery);
	set_bit(MD_RECOVERY_RUNNING, &mddev->recovery);
	mddev->sync_thread = md_register_thread(md_do_sync, mddev,
						"reshape");
	if (!mddev->sync_thread) {
		mddev->recovery = 0;
		spin_lock_irq(&conf->device_lock);
		write_seqcount_begin(&conf->gen_lock);
		mddev->raid_disks = conf->raid_disks = conf->previous_raid_disks;
		mddev->new_chunk_sectors =
			conf->chunk_sectors = conf->prev_chunk_sectors;
		mddev->new_layout = conf->algorithm = conf->prev_algo;
		rdev_for_each(rdev, mddev)
			rdev->new_data_offset = rdev->data_offset;
		smp_wmb();
		conf->generation --;
		conf->reshape_progress = MaxSector;
		mddev->reshape_position = MaxSector;
		write_seqcount_end(&conf->gen_lock);
		spin_unlock_irq(&conf->device_lock);
		return -EAGAIN;
	}
	conf->reshape_checkpoint = jiffies;
	md_wakeup_thread(mddev->sync_thread);
	md_new_event(mddev);
	return 0;
}
#endif

static void end_reshape(raid5_conf_t *conf)
{
	struct block_device *bdev;

	if (!test_bit(MD_RECOVERY_INTR, &conf->mddev->recovery)) {
		conf->mddev->array_sectors = 2 * conf->mddev->size *
			(conf->raid_disks - conf->max_degraded);
		set_capacity(conf->mddev->gendisk, conf->mddev->array_sectors);
		conf->mddev->changed = 1;

		bdev = bdget_disk(conf->mddev->gendisk, 0);
		if (bdev) {
			mutex_lock(&bdev->bd_inode->i_mutex);
			i_size_write(bdev->bd_inode,
				     (loff_t)conf->mddev->array_sectors << 9);
			mutex_unlock(&bdev->bd_inode->i_mutex);
			bdput(bdev);
		}
		spin_lock_irq(&conf->device_lock);
		conf->expand_progress = MaxSector;
		spin_unlock_irq(&conf->device_lock);
		conf->mddev->reshape_position = MaxSector;

/* This is called from the reshape thread and should make any
 * changes needed in 'conf'
 */
static void end_reshape(struct r5conf *conf)
{

	if (!test_bit(MD_RECOVERY_INTR, &conf->mddev->recovery)) {

		spin_lock_irq(&conf->device_lock);
		conf->previous_raid_disks = conf->raid_disks;
		md_finish_reshape(conf->mddev);
		smp_wmb();
		conf->reshape_progress = MaxSector;
		conf->mddev->reshape_position = MaxSector;
		spin_unlock_irq(&conf->device_lock);
		wake_up(&conf->wait_for_overlap);

		/* read-ahead size must cover two whole stripes, which is
		 * 2 * (datadisks) * chunksize where 'n' is the number of raid devices
		 */
		{
			int data_disks = conf->previous_raid_disks - conf->max_degraded;
			int stripe = data_disks *
				(conf->mddev->chunk_size / PAGE_SIZE);
		if (conf->mddev->queue) {
			int data_disks = conf->raid_disks - conf->max_degraded;
			int stripe = data_disks * ((conf->chunk_sectors << 9)
						   / PAGE_SIZE);
			if (conf->mddev->queue->backing_dev_info.ra_pages < 2 * stripe)
				conf->mddev->queue->backing_dev_info.ra_pages = 2 * stripe;
		}
	}
}

static void raid5_quiesce(mddev_t *mddev, int state)
{
	raid5_conf_t *conf = mddev_to_conf(mddev);
/* This is called from the raid5d thread with mddev_lock held.
 * It makes config changes to the device.
 */
static void raid5_finish_reshape(struct mddev *mddev)
{
	struct r5conf *conf = mddev->private;

	if (!test_bit(MD_RECOVERY_INTR, &mddev->recovery)) {

		if (mddev->delta_disks > 0) {
			md_set_array_sectors(mddev, raid5_size(mddev, 0, 0));
			set_capacity(mddev->gendisk, mddev->array_sectors);
			revalidate_disk(mddev->gendisk);
		} else {
			int d;
			spin_lock_irq(&conf->device_lock);
			mddev->degraded = calc_degraded(conf);
			spin_unlock_irq(&conf->device_lock);
			for (d = conf->raid_disks ;
			     d < conf->raid_disks - mddev->delta_disks;
			     d++) {
				struct md_rdev *rdev = conf->disks[d].rdev;
				if (rdev)
					clear_bit(In_sync, &rdev->flags);
				rdev = conf->disks[d].replacement;
				if (rdev)
					clear_bit(In_sync, &rdev->flags);
			}
		}
		mddev->layout = conf->algorithm;
		mddev->chunk_sectors = conf->chunk_sectors;
		mddev->reshape_position = MaxSector;
		mddev->delta_disks = 0;
		mddev->reshape_backwards = 0;
	}
}

static void raid5_quiesce(struct mddev *mddev, int state)
{
	struct r5conf *conf = mddev->private;

	switch(state) {
	case 2: /* resume for a suspend */
		wake_up(&conf->wait_for_overlap);
		break;

	case 1: /* stop all writes */
		spin_lock_irq(&conf->device_lock);
		conf->quiesce = 1;
		wait_event_lock_irq(conf->wait_for_stripe,
				    atomic_read(&conf->active_stripes) == 0 &&
				    atomic_read(&conf->active_aligned_reads) == 0,
				    conf->device_lock, /* nothing */);
		spin_unlock_irq(&conf->device_lock);
		break;

	case 0: /* re-enable writes */
		spin_lock_irq(&conf->device_lock);
		conf->quiesce = 0;
		wake_up(&conf->wait_for_stripe);
		wake_up(&conf->wait_for_overlap);
		spin_unlock_irq(&conf->device_lock);
		break;
	}
}

static struct mdk_personality raid6_personality =
		lock_all_device_hash_locks_irq(conf);
		/* '2' tells resync/reshape to pause so that all
		 * active stripes can drain
		 */
		conf->quiesce = 2;
		wait_event_cmd(conf->wait_for_quiescent,
				    atomic_read(&conf->active_stripes) == 0 &&
				    atomic_read(&conf->active_aligned_reads) == 0,
				    unlock_all_device_hash_locks_irq(conf),
				    lock_all_device_hash_locks_irq(conf));
		conf->quiesce = 1;
		unlock_all_device_hash_locks_irq(conf);
		/* allow reshape to continue */
		wake_up(&conf->wait_for_overlap);
		break;

	case 0: /* re-enable writes */
		lock_all_device_hash_locks_irq(conf);
		conf->quiesce = 0;
		wake_up(&conf->wait_for_quiescent);
		wake_up(&conf->wait_for_overlap);
		unlock_all_device_hash_locks_irq(conf);
		break;
	}
	r5l_quiesce(conf->log, state);
}

static void *raid45_takeover_raid0(struct mddev *mddev, int level)
{
	struct r0conf *raid0_conf = mddev->private;
	sector_t sectors;

	/* for raid0 takeover only one zone is supported */
	if (raid0_conf->nr_strip_zones > 1) {
		printk(KERN_ERR "md/raid:%s: cannot takeover raid0 with more than one zone.\n",
		       mdname(mddev));
		return ERR_PTR(-EINVAL);
	}

	sectors = raid0_conf->strip_zone[0].zone_end;
	sector_div(sectors, raid0_conf->strip_zone[0].nb_dev);
	mddev->dev_sectors = sectors;
	mddev->new_level = level;
	mddev->new_layout = ALGORITHM_PARITY_N;
	mddev->new_chunk_sectors = mddev->chunk_sectors;
	mddev->raid_disks += 1;
	mddev->delta_disks = 1;
	/* make sure it will be not marked as dirty */
	mddev->recovery_cp = MaxSector;

	return setup_conf(mddev);
}

static void *raid5_takeover_raid1(struct mddev *mddev)
{
	int chunksect;

	if (mddev->raid_disks != 2 ||
	    mddev->degraded > 1)
		return ERR_PTR(-EINVAL);

	/* Should check if there are write-behind devices? */

	chunksect = 64*2; /* 64K by default */

	/* The array must be an exact multiple of chunksize */
	while (chunksect && (mddev->array_sectors & (chunksect-1)))
		chunksect >>= 1;

	if ((chunksect<<9) < STRIPE_SIZE)
		/* array size does not allow a suitable chunk size */
		return ERR_PTR(-EINVAL);

	mddev->new_level = 5;
	mddev->new_layout = ALGORITHM_LEFT_SYMMETRIC;
	mddev->new_chunk_sectors = chunksect;

	return setup_conf(mddev);
}

static void *raid5_takeover_raid6(struct mddev *mddev)
{
	int new_layout;

	switch (mddev->layout) {
	case ALGORITHM_LEFT_ASYMMETRIC_6:
		new_layout = ALGORITHM_LEFT_ASYMMETRIC;
		break;
	case ALGORITHM_RIGHT_ASYMMETRIC_6:
		new_layout = ALGORITHM_RIGHT_ASYMMETRIC;
		break;
	case ALGORITHM_LEFT_SYMMETRIC_6:
		new_layout = ALGORITHM_LEFT_SYMMETRIC;
		break;
	case ALGORITHM_RIGHT_SYMMETRIC_6:
		new_layout = ALGORITHM_RIGHT_SYMMETRIC;
		break;
	case ALGORITHM_PARITY_0_6:
		new_layout = ALGORITHM_PARITY_0;
		break;
	case ALGORITHM_PARITY_N:
		new_layout = ALGORITHM_PARITY_N;
		break;
	default:
		return ERR_PTR(-EINVAL);
	}
	mddev->new_level = 5;
	mddev->new_layout = new_layout;
	mddev->delta_disks = -1;
	mddev->raid_disks -= 1;
	return setup_conf(mddev);
}

static int raid5_check_reshape(struct mddev *mddev)
{
	/* For a 2-drive array, the layout and chunk size can be changed
	 * immediately as not restriping is needed.
	 * For larger arrays we record the new value - after validation
	 * to be used by a reshape pass.
	 */
	struct r5conf *conf = mddev->private;
	int new_chunk = mddev->new_chunk_sectors;

	if (mddev->new_layout >= 0 && !algorithm_valid_raid5(mddev->new_layout))
		return -EINVAL;
	if (new_chunk > 0) {
		if (!is_power_of_2(new_chunk))
			return -EINVAL;
		if (new_chunk < (PAGE_SIZE>>9))
			return -EINVAL;
		if (mddev->array_sectors & (new_chunk-1))
			/* not factor of array size */
			return -EINVAL;
	}

	/* They look valid */

	if (mddev->raid_disks == 2) {
		/* can make the change immediately */
		if (mddev->new_layout >= 0) {
			conf->algorithm = mddev->new_layout;
			mddev->layout = mddev->new_layout;
		}
		if (new_chunk > 0) {
			conf->chunk_sectors = new_chunk ;
			mddev->chunk_sectors = new_chunk;
		}
		set_bit(MD_CHANGE_DEVS, &mddev->flags);
		md_wakeup_thread(mddev->thread);
	}
	return check_reshape(mddev);
}

static int raid6_check_reshape(struct mddev *mddev)
{
	int new_chunk = mddev->new_chunk_sectors;

	if (mddev->new_layout >= 0 && !algorithm_valid_raid6(mddev->new_layout))
		return -EINVAL;
	if (new_chunk > 0) {
		if (!is_power_of_2(new_chunk))
			return -EINVAL;
		if (new_chunk < (PAGE_SIZE >> 9))
			return -EINVAL;
		if (mddev->array_sectors & (new_chunk-1))
			/* not factor of array size */
			return -EINVAL;
	}

	/* They look valid */
	return check_reshape(mddev);
}

static void *raid5_takeover(struct mddev *mddev)
{
	/* raid5 can take over:
	 *  raid0 - if there is only one strip zone - make it a raid4 layout
	 *  raid1 - if there are two drives.  We need to know the chunk size
	 *  raid4 - trivial - just use a raid4 layout.
	 *  raid6 - Providing it is a *_6 layout
	 */
	if (mddev->level == 0)
		return raid45_takeover_raid0(mddev, 5);
	if (mddev->level == 1)
		return raid5_takeover_raid1(mddev);
	if (mddev->level == 4) {
		mddev->new_layout = ALGORITHM_PARITY_N;
		mddev->new_level = 5;
		return setup_conf(mddev);
	}
	if (mddev->level == 6)
		return raid5_takeover_raid6(mddev);

	return ERR_PTR(-EINVAL);
}

static void *raid4_takeover(struct mddev *mddev)
{
	/* raid4 can take over:
	 *  raid0 - if there is only one strip zone
	 *  raid5 - if layout is right
	 */
	if (mddev->level == 0)
		return raid45_takeover_raid0(mddev, 4);
	if (mddev->level == 5 &&
	    mddev->layout == ALGORITHM_PARITY_N) {
		mddev->new_layout = 0;
		mddev->new_level = 4;
		return setup_conf(mddev);
	}
	return ERR_PTR(-EINVAL);
}

static struct md_personality raid5_personality;

static void *raid6_takeover(struct mddev *mddev)
{
	/* Currently can only take over a raid5.  We map the
	 * personality to an equivalent raid6 personality
	 * with the Q block at the end.
	 */
	int new_layout;

	if (mddev->pers != &raid5_personality)
		return ERR_PTR(-EINVAL);
	if (mddev->degraded > 1)
		return ERR_PTR(-EINVAL);
	if (mddev->raid_disks > 253)
		return ERR_PTR(-EINVAL);
	if (mddev->raid_disks < 3)
		return ERR_PTR(-EINVAL);

	switch (mddev->layout) {
	case ALGORITHM_LEFT_ASYMMETRIC:
		new_layout = ALGORITHM_LEFT_ASYMMETRIC_6;
		break;
	case ALGORITHM_RIGHT_ASYMMETRIC:
		new_layout = ALGORITHM_RIGHT_ASYMMETRIC_6;
		break;
	case ALGORITHM_LEFT_SYMMETRIC:
		new_layout = ALGORITHM_LEFT_SYMMETRIC_6;
		break;
	case ALGORITHM_RIGHT_SYMMETRIC:
		new_layout = ALGORITHM_RIGHT_SYMMETRIC_6;
		break;
	case ALGORITHM_PARITY_0:
		new_layout = ALGORITHM_PARITY_0_6;
		break;
	case ALGORITHM_PARITY_N:
		new_layout = ALGORITHM_PARITY_N;
		break;
	default:
		return ERR_PTR(-EINVAL);
	}
	mddev->new_level = 6;
	mddev->new_layout = new_layout;
	mddev->delta_disks = 1;
	mddev->raid_disks += 1;
	return setup_conf(mddev);
}

static struct md_personality raid6_personality =
{
	.name		= "raid6",
	.level		= 6,
	.owner		= THIS_MODULE,
	.make_request	= make_request,
	.run		= run,
	.stop		= stop,
	.free		= raid5_free,
	.status		= status,
	.error_handler	= error,
	.hot_add_disk	= raid5_add_disk,
	.hot_remove_disk= raid5_remove_disk,
	.spare_active	= raid5_spare_active,
	.sync_request	= sync_request,
	.resize		= raid5_resize,
#ifdef CONFIG_MD_RAID5_RESHAPE
	.check_reshape	= raid5_check_reshape,
	.start_reshape  = raid5_start_reshape,
#endif
	.quiesce	= raid5_quiesce,
};
static struct mdk_personality raid5_personality =
	.size		= raid5_size,
	.check_reshape	= raid6_check_reshape,
	.start_reshape  = raid5_start_reshape,
	.finish_reshape = raid5_finish_reshape,
	.quiesce	= raid5_quiesce,
	.takeover	= raid6_takeover,
	.congested	= raid5_congested,
};
static struct md_personality raid5_personality =
{
	.name		= "raid5",
	.level		= 5,
	.owner		= THIS_MODULE,
	.make_request	= make_request,
	.run		= run,
	.stop		= stop,
	.free		= raid5_free,
	.status		= status,
	.error_handler	= error,
	.hot_add_disk	= raid5_add_disk,
	.hot_remove_disk= raid5_remove_disk,
	.spare_active	= raid5_spare_active,
	.sync_request	= sync_request,
	.resize		= raid5_resize,
#ifdef CONFIG_MD_RAID5_RESHAPE
	.check_reshape	= raid5_check_reshape,
	.start_reshape  = raid5_start_reshape,
#endif
	.quiesce	= raid5_quiesce,
};

static struct mdk_personality raid4_personality =
	.size		= raid5_size,
	.check_reshape	= raid5_check_reshape,
	.start_reshape  = raid5_start_reshape,
	.finish_reshape = raid5_finish_reshape,
	.quiesce	= raid5_quiesce,
	.takeover	= raid5_takeover,
	.congested	= raid5_congested,
};

static struct md_personality raid4_personality =
{
	.name		= "raid4",
	.level		= 4,
	.owner		= THIS_MODULE,
	.make_request	= make_request,
	.run		= run,
	.stop		= stop,
	.free		= raid5_free,
	.status		= status,
	.error_handler	= error,
	.hot_add_disk	= raid5_add_disk,
	.hot_remove_disk= raid5_remove_disk,
	.spare_active	= raid5_spare_active,
	.sync_request	= sync_request,
	.resize		= raid5_resize,
#ifdef CONFIG_MD_RAID5_RESHAPE
	.check_reshape	= raid5_check_reshape,
	.start_reshape  = raid5_start_reshape,
#endif
	.quiesce	= raid5_quiesce,
	.size		= raid5_size,
	.check_reshape	= raid5_check_reshape,
	.start_reshape  = raid5_start_reshape,
	.finish_reshape = raid5_finish_reshape,
	.quiesce	= raid5_quiesce,
	.takeover	= raid4_takeover,
	.congested	= raid5_congested,
};

static int __init raid5_init(void)
{
	int e;

	e = raid6_select_algo();
	if ( e )
		return e;
	raid5_wq = alloc_workqueue("raid5wq",
		WQ_UNBOUND|WQ_MEM_RECLAIM|WQ_CPU_INTENSIVE|WQ_SYSFS, 0);
	if (!raid5_wq)
		return -ENOMEM;
	register_md_personality(&raid6_personality);
	register_md_personality(&raid5_personality);
	register_md_personality(&raid4_personality);
	return 0;
}

static void raid5_exit(void)
{
	unregister_md_personality(&raid6_personality);
	unregister_md_personality(&raid5_personality);
	unregister_md_personality(&raid4_personality);
	destroy_workqueue(raid5_wq);
}

module_init(raid5_init);
module_exit(raid5_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RAID4/5/6 (striping with parity) personality for MD");
MODULE_ALIAS("md-personality-4"); /* RAID5 */
MODULE_ALIAS("md-raid5");
MODULE_ALIAS("md-raid4");
MODULE_ALIAS("md-level-5");
MODULE_ALIAS("md-level-4");
MODULE_ALIAS("md-personality-8"); /* RAID6 */
MODULE_ALIAS("md-raid6");
MODULE_ALIAS("md-level-6");

/* This used to be two separate modules, they were: */
MODULE_ALIAS("raid5");
MODULE_ALIAS("raid6");
