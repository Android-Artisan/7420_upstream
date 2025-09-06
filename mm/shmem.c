/*
 * Resizable virtual memory filesystem for Linux.
 *
 * Copyright (C) 2000 Linus Torvalds.
 *		 2000 Transmeta Corp.
 *		 2000-2001 Christoph Rohland
 *		 2000-2001 SAP AG
 *		 2002 Red Hat Inc.
 * Copyright (C) 2002-2005 Hugh Dickins.
 * Copyright (C) 2002-2011 Hugh Dickins.
 * Copyright (C) 2011 Google Inc.
 * Copyright (C) 2002-2005 VERITAS Software Corporation.
 * Copyright (C) 2004 Andi Kleen, SuSE Labs
 *
 * Extended attribute support for tmpfs:
 * Copyright (c) 2004, Luke Kenneth Casson Leighton <lkcl@lkcl.net>
 * Copyright (c) 2004 Red Hat, Inc., James Morris <jmorris@redhat.com>
 *
 * This file is released under the GPL.
 */

 * tiny-shmem:
 * Copyright (c) 2004, 2008 Matt Mackall <mpm@selenic.com>
 *
 * This file is released under the GPL.
 */

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/vfs.h>
#include <linux/mount.h>
#include <linux/ramfs.h>
#include <linux/pagemap.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/export.h>
#include <linux/swap.h>
#include <linux/uio.h>

static struct vfsmount *shm_mnt;

#ifdef CONFIG_SHMEM
/*
 * This virtual memory filesystem is heavily based on the ramfs. It
 * extends ramfs by the ability to use swap and honor resource limits
 * which makes it a completely usable filesystem.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/xattr.h>
#include <linux/exportfs.h>
#include <linux/generic_acl.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/file.h>
#include <linux/swap.h>
#include <linux/pagemap.h>
#include <linux/xattr.h>
#include <linux/exportfs.h>
#include <linux/posix_acl.h>
#include <linux/posix_acl_xattr.h>
#include <linux/mman.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/backing-dev.h>
#include <linux/shmem_fs.h>
#include <linux/mount.h>
#include <linux/writeback.h>
#include <linux/vfs.h>
#include <linux/blkdev.h>
#include <linux/writeback.h>
#include <linux/blkdev.h>
#include <linux/pagevec.h>
#include <linux/percpu_counter.h>
#include <linux/falloc.h>
#include <linux/splice.h>
#include <linux/security.h>
#include <linux/swapops.h>
#include <linux/mempolicy.h>
#include <linux/namei.h>
#include <linux/ctype.h>
#include <linux/migrate.h>
#include <linux/highmem.h>
#include <linux/seq_file.h>

#include <asm/uaccess.h>
#include <asm/div64.h>
#include <asm/pgtable.h>

/* This magic number is used in glibc for posix shared memory */
#define TMPFS_MAGIC	0x01021994

#define ENTRIES_PER_PAGE (PAGE_CACHE_SIZE/sizeof(unsigned long))
#define ENTRIES_PER_PAGEPAGE (ENTRIES_PER_PAGE*ENTRIES_PER_PAGE)
#define BLOCKS_PER_PAGE  (PAGE_CACHE_SIZE/512)

#define SHMEM_MAX_INDEX  (SHMEM_NR_DIRECT + (ENTRIES_PER_PAGEPAGE/2) * (ENTRIES_PER_PAGE+1))
#define SHMEM_MAX_BYTES  ((unsigned long long)SHMEM_MAX_INDEX << PAGE_CACHE_SHIFT)

#define VM_ACCT(size)    (PAGE_CACHE_ALIGN(size) >> PAGE_SHIFT)

/* info->flags needs VM_flags to handle pagein/truncate races efficiently */
#define SHMEM_PAGEIN	 VM_READ
#define SHMEM_TRUNCATE	 VM_WRITE

/* Definition to limit shmem_truncate's steps between cond_rescheds */
#define LATENCY_LIMIT	 64

/* Pretend that each entry is of this size in directory's i_size */
#define BOGO_DIRENT_SIZE 20

/* Flag allocation requirements to shmem_getpage and shmem_swp_alloc */
#include <linux/magic.h>
#include <linux/syscalls.h>
#include <linux/fcntl.h>
#include <uapi/linux/memfd.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>

#include "internal.h"

#define BLOCKS_PER_PAGE  (PAGE_CACHE_SIZE/512)
#define VM_ACCT(size)    (PAGE_CACHE_ALIGN(size) >> PAGE_SHIFT)

/* Pretend that each entry is of this size in directory's i_size */
#define BOGO_DIRENT_SIZE 20

/* Symlink up to this size is kmalloc'ed instead of using a swappable page */
#define SHORT_SYMLINK_LEN 128

/*
 * shmem_fallocate communicates with shmem_fault or shmem_writepage via
 * inode->i_private (with i_mutex making sure that it has only one user at
 * a time): we would prefer not to enlarge the shmem inode just for that.
 */
struct shmem_falloc {
	wait_queue_head_t *waitq; /* faults into hole wait for punch to end */
	pgoff_t start;		/* start of range currently being fallocated */
	pgoff_t next;		/* the next page offset to be fallocated */
	pgoff_t nr_falloced;	/* how many new pages have been fallocated */
	pgoff_t nr_unswapped;	/* how often writepage refused to swap out */
};

/* Flag allocation requirements to shmem_getpage */
enum sgp_type {
	SGP_READ,	/* don't exceed i_size, don't allocate page */
	SGP_CACHE,	/* don't exceed i_size, may allocate page */
	SGP_DIRTY,	/* like SGP_CACHE, but set new page dirty */
	SGP_WRITE,	/* may exceed i_size, may allocate page */
	SGP_WRITE,	/* may exceed i_size, may allocate !Uptodate page */
	SGP_FALLOC,	/* like SGP_WRITE, but make existing page Uptodate */
};

#ifdef CONFIG_TMPFS
static unsigned long shmem_default_max_blocks(void)
{
	return totalram_pages / 2;
}

static unsigned long shmem_default_max_inodes(void)
{
	return min(totalram_pages - totalhigh_pages, totalram_pages / 2);
}
#endif

static int shmem_getpage(struct inode *inode, unsigned long idx,
			 struct page **pagep, enum sgp_type sgp, int *type);

static inline struct page *shmem_dir_alloc(gfp_t gfp_mask)
{
	/*
	 * The above definition of ENTRIES_PER_PAGE, and the use of
	 * BLOCKS_PER_PAGE on indirect pages, assume PAGE_CACHE_SIZE:
	 * might be reconsidered if it ever diverges from PAGE_SIZE.
	 *
	 * Mobility flags are masked out as swap vectors cannot move
	 */
	return alloc_pages((gfp_mask & ~GFP_MOVABLE_MASK) | __GFP_ZERO,
				PAGE_CACHE_SHIFT-PAGE_SHIFT);
}

static inline void shmem_dir_free(struct page *page)
{
	__free_pages(page, PAGE_CACHE_SHIFT-PAGE_SHIFT);
}

static struct page **shmem_dir_map(struct page *page)
{
	return (struct page **)kmap_atomic(page, KM_USER0);
}

static inline void shmem_dir_unmap(struct page **dir)
{
	kunmap_atomic(dir, KM_USER0);
}

static swp_entry_t *shmem_swp_map(struct page *page)
{
	return (swp_entry_t *)kmap_atomic(page, KM_USER1);
}

static inline void shmem_swp_balance_unmap(void)
{
	/*
	 * When passing a pointer to an i_direct entry, to code which
	 * also handles indirect entries and so will shmem_swp_unmap,
	 * we must arrange for the preempt count to remain in balance.
	 * What kmap_atomic of a lowmem page does depends on config
	 * and architecture, so pretend to kmap_atomic some lowmem page.
	 */
	(void) kmap_atomic(ZERO_PAGE(0), KM_USER1);
}

static inline void shmem_swp_unmap(swp_entry_t *entry)
{
	kunmap_atomic(entry, KM_USER1);
static bool shmem_should_replace_page(struct page *page, gfp_t gfp);
static int shmem_replace_page(struct page **pagep, gfp_t gfp,
				struct shmem_inode_info *info, pgoff_t index);
static int shmem_getpage_gfp(struct inode *inode, pgoff_t index,
	struct page **pagep, enum sgp_type sgp, gfp_t gfp, int *fault_type);

static inline int shmem_getpage(struct inode *inode, pgoff_t index,
	struct page **pagep, enum sgp_type sgp, int *fault_type)
{
	return shmem_getpage_gfp(inode, index, pagep, sgp,
			mapping_gfp_mask(inode->i_mapping), fault_type);
}

static inline struct shmem_sb_info *SHMEM_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

/*
 * shmem_file_setup pre-accounts the whole fixed size of a VM object,
 * for shared memory and for shared anonymous (/dev/zero) mappings
 * (unless MAP_NORESERVE and sysctl_overcommit_memory <= 1),
 * consistent with the pre-accounting of private mappings ...
 */
static inline int shmem_acct_size(unsigned long flags, loff_t size)
{
	return (flags & VM_ACCOUNT)?
		security_vm_enough_memory(VM_ACCT(size)): 0;
	return (flags & VM_NORESERVE) ?
		0 : security_vm_enough_memory_mm(current->mm, VM_ACCT(size));
}

static inline void shmem_unacct_size(unsigned long flags, loff_t size)
{
	if (flags & VM_ACCOUNT)
		vm_unacct_memory(VM_ACCT(size));
}

	if (!(flags & VM_NORESERVE))
		vm_unacct_memory(VM_ACCT(size));
}

static inline int shmem_reacct_size(unsigned long flags,
		loff_t oldsize, loff_t newsize)
{
	if (!(flags & VM_NORESERVE)) {
		if (VM_ACCT(newsize) > VM_ACCT(oldsize))
			return security_vm_enough_memory_mm(current->mm,
					VM_ACCT(newsize) - VM_ACCT(oldsize));
		else if (VM_ACCT(newsize) < VM_ACCT(oldsize))
			vm_unacct_memory(VM_ACCT(oldsize) - VM_ACCT(newsize));
	}
	return 0;
}

/*
 * ... whereas tmpfs objects are accounted incrementally as
 * pages are allocated, in order to allow huge sparse files.
 * shmem_getpage reports shmem_acct_block failure as -ENOSPC not -ENOMEM,
 * so that a failure on a sparse tmpfs mapping will give SIGBUS not OOM.
 */
static inline int shmem_acct_block(unsigned long flags)
{
	return (flags & VM_ACCOUNT)?
		0: security_vm_enough_memory(VM_ACCT(PAGE_CACHE_SIZE));
	return (flags & VM_NORESERVE) ?
		security_vm_enough_memory_mm(current->mm, VM_ACCT(PAGE_CACHE_SIZE)) : 0;
}

static inline void shmem_unacct_blocks(unsigned long flags, long pages)
{
	if (!(flags & VM_ACCOUNT))
	if (flags & VM_NORESERVE)
		vm_unacct_memory(pages * VM_ACCT(PAGE_CACHE_SIZE));
}

static const struct super_operations shmem_ops;
static const struct address_space_operations shmem_aops;
static const struct file_operations shmem_file_operations;
static const struct inode_operations shmem_inode_operations;
static const struct inode_operations shmem_dir_inode_operations;
static const struct inode_operations shmem_special_inode_operations;
static struct vm_operations_struct shmem_vm_ops;

static struct backing_dev_info shmem_backing_dev_info  __read_mostly = {
	.ra_pages	= 0,	/* No readahead */
	.capabilities	= BDI_CAP_NO_ACCT_AND_WRITEBACK,
	.unplug_io_fn	= default_unplug_io_fn,
};
static const struct vm_operations_struct shmem_vm_ops;

static LIST_HEAD(shmem_swaplist);
static DEFINE_MUTEX(shmem_swaplist_mutex);

static void shmem_free_blocks(struct inode *inode, long pages)
{
	struct shmem_sb_info *sbinfo = SHMEM_SB(inode->i_sb);
	if (sbinfo->max_blocks) {
		spin_lock(&sbinfo->stat_lock);
		sbinfo->free_blocks += pages;
		inode->i_blocks -= pages*BLOCKS_PER_PAGE;
		spin_unlock(&sbinfo->stat_lock);
	}
}

static int shmem_reserve_inode(struct super_block *sb)
{
	struct shmem_sb_info *sbinfo = SHMEM_SB(sb);
	if (sbinfo->max_inodes) {
		spin_lock(&sbinfo->stat_lock);
		if (!sbinfo->free_inodes) {
			spin_unlock(&sbinfo->stat_lock);
			return -ENOSPC;
		}
		sbinfo->free_inodes--;
		spin_unlock(&sbinfo->stat_lock);
	}
	return 0;
}

static void shmem_free_inode(struct super_block *sb)
{
	struct shmem_sb_info *sbinfo = SHMEM_SB(sb);
	if (sbinfo->max_inodes) {
		spin_lock(&sbinfo->stat_lock);
		sbinfo->free_inodes++;
		spin_unlock(&sbinfo->stat_lock);
	}
}

/**
 * shmem_recalc_inode - recalculate the size of an inode
 * shmem_recalc_inode - recalculate the block usage of an inode
 * @inode: inode to recalc
 *
 * We have to calculate the free blocks since the mm can drop
 * undirtied hole pages behind our back.
 *
 * But normally   info->alloced == inode->i_mapping->nrpages + info->swapped
 * So mm freed is info->alloced - (inode->i_mapping->nrpages + info->swapped)
 *
 * It has to be called with the spinlock held.
 */
static void shmem_recalc_inode(struct inode *inode)
{
	struct shmem_inode_info *info = SHMEM_I(inode);
	long freed;

	freed = info->alloced - info->swapped - inode->i_mapping->nrpages;
	if (freed > 0) {
		info->alloced -= freed;
		shmem_unacct_blocks(info->flags, freed);
		shmem_free_blocks(inode, freed);
	}
}

/**
 * shmem_swp_entry - find the swap vector position in the info structure
 * @info:  info structure for the inode
 * @index: index of the page to find
 * @page:  optional page to add to the structure. Has to be preset to
 *         all zeros
 *
 * If there is no space allocated yet it will return NULL when
 * page is NULL, else it will use the page for the needed block,
 * setting it to NULL on return to indicate that it has been used.
 *
 * The swap vector is organized the following way:
 *
 * There are SHMEM_NR_DIRECT entries directly stored in the
 * shmem_inode_info structure. So small files do not need an addional
 * allocation.
 *
 * For pages with index > SHMEM_NR_DIRECT there is the pointer
 * i_indirect which points to a page which holds in the first half
 * doubly indirect blocks, in the second half triple indirect blocks:
 *
 * For an artificial ENTRIES_PER_PAGE = 4 this would lead to the
 * following layout (for SHMEM_NR_DIRECT == 16):
 *
 * i_indirect -> dir --> 16-19
 * 	      |	     +-> 20-23
 * 	      |
 * 	      +-->dir2 --> 24-27
 * 	      |	       +-> 28-31
 * 	      |	       +-> 32-35
 * 	      |	       +-> 36-39
 * 	      |
 * 	      +-->dir3 --> 40-43
 * 	       	       +-> 44-47
 * 	      	       +-> 48-51
 * 	      	       +-> 52-55
 */
static swp_entry_t *shmem_swp_entry(struct shmem_inode_info *info, unsigned long index, struct page **page)
{
	unsigned long offset;
	struct page **dir;
	struct page *subdir;

	if (index < SHMEM_NR_DIRECT) {
		shmem_swp_balance_unmap();
		return info->i_direct+index;
	}
	if (!info->i_indirect) {
		if (page) {
			info->i_indirect = *page;
			*page = NULL;
		}
		return NULL;			/* need another page */
	}

	index -= SHMEM_NR_DIRECT;
	offset = index % ENTRIES_PER_PAGE;
	index /= ENTRIES_PER_PAGE;
	dir = shmem_dir_map(info->i_indirect);

	if (index >= ENTRIES_PER_PAGE/2) {
		index -= ENTRIES_PER_PAGE/2;
		dir += ENTRIES_PER_PAGE/2 + index/ENTRIES_PER_PAGE;
		index %= ENTRIES_PER_PAGE;
		subdir = *dir;
		if (!subdir) {
			if (page) {
				*dir = *page;
				*page = NULL;
			}
			shmem_dir_unmap(dir);
			return NULL;		/* need another page */
		}
		shmem_dir_unmap(dir);
		dir = shmem_dir_map(subdir);
	}

	dir += index;
	subdir = *dir;
	if (!subdir) {
		if (!page || !(subdir = *page)) {
			shmem_dir_unmap(dir);
			return NULL;		/* need a page */
		}
		*dir = subdir;
		*page = NULL;
	}
	shmem_dir_unmap(dir);
	return shmem_swp_map(subdir) + offset;
}

static void shmem_swp_set(struct shmem_inode_info *info, swp_entry_t *entry, unsigned long value)
{
	long incdec = value? 1: -1;

	entry->val = value;
	info->swapped += incdec;
	if ((unsigned long)(entry - info->i_direct) >= SHMEM_NR_DIRECT) {
		struct page *page = kmap_atomic_to_page(entry);
		set_page_private(page, page_private(page) + incdec);
	}
}

/**
 * shmem_swp_alloc - get the position of the swap entry for the page.
 * @info:	info structure for the inode
 * @index:	index of the page to find
 * @sgp:	check and recheck i_size? skip allocation?
 *
 * If the entry does not exist, allocate it.
 */
static swp_entry_t *shmem_swp_alloc(struct shmem_inode_info *info, unsigned long index, enum sgp_type sgp)
{
	struct inode *inode = &info->vfs_inode;
	struct shmem_sb_info *sbinfo = SHMEM_SB(inode->i_sb);
	struct page *page = NULL;
	swp_entry_t *entry;

	if (sgp != SGP_WRITE &&
	    ((loff_t) index << PAGE_CACHE_SHIFT) >= i_size_read(inode))
		return ERR_PTR(-EINVAL);

	while (!(entry = shmem_swp_entry(info, index, &page))) {
		if (sgp == SGP_READ)
			return shmem_swp_map(ZERO_PAGE(0));
		/*
		 * Test free_blocks against 1 not 0, since we have 1 data
		 * page (and perhaps indirect index pages) yet to allocate:
		 * a waste to allocate index if we cannot allocate data.
		 */
		if (sbinfo->max_blocks) {
			spin_lock(&sbinfo->stat_lock);
			if (sbinfo->free_blocks <= 1) {
				spin_unlock(&sbinfo->stat_lock);
				return ERR_PTR(-ENOSPC);
			}
			sbinfo->free_blocks--;
			inode->i_blocks += BLOCKS_PER_PAGE;
			spin_unlock(&sbinfo->stat_lock);
		}

		spin_unlock(&info->lock);
		page = shmem_dir_alloc(mapping_gfp_mask(inode->i_mapping));
		if (page)
			set_page_private(page, 0);
		spin_lock(&info->lock);

		if (!page) {
			shmem_free_blocks(inode, 1);
			return ERR_PTR(-ENOMEM);
		}
		if (sgp != SGP_WRITE &&
		    ((loff_t) index << PAGE_CACHE_SHIFT) >= i_size_read(inode)) {
			entry = ERR_PTR(-EINVAL);
			break;
		}
		if (info->next_index <= index)
			info->next_index = index + 1;
	}
	if (page) {
		/* another task gave its page, or truncated the file */
		shmem_free_blocks(inode, 1);
		shmem_dir_free(page);
	}
	if (info->next_index <= index && !IS_ERR(entry))
		info->next_index = index + 1;
	return entry;
}

/**
 * shmem_free_swp - free some swap entries in a directory
 * @dir:        pointer to the directory
 * @edir:       pointer after last entry of the directory
 * @punch_lock: pointer to spinlock when needed for the holepunch case
 */
static int shmem_free_swp(swp_entry_t *dir, swp_entry_t *edir,
						spinlock_t *punch_lock)
{
	spinlock_t *punch_unlock = NULL;
	swp_entry_t *ptr;
	int freed = 0;

	for (ptr = dir; ptr < edir; ptr++) {
		if (ptr->val) {
			if (unlikely(punch_lock)) {
				punch_unlock = punch_lock;
				punch_lock = NULL;
				spin_lock(punch_unlock);
				if (!ptr->val)
					continue;
			}
			free_swap_and_cache(*ptr);
			*ptr = (swp_entry_t){0};
			freed++;
		}
	}
	if (punch_unlock)
		spin_unlock(punch_unlock);
	return freed;
}

static int shmem_map_and_free_swp(struct page *subdir, int offset,
		int limit, struct page ***dir, spinlock_t *punch_lock)
{
	swp_entry_t *ptr;
	int freed = 0;

	ptr = shmem_swp_map(subdir);
	for (; offset < limit; offset += LATENCY_LIMIT) {
		int size = limit - offset;
		if (size > LATENCY_LIMIT)
			size = LATENCY_LIMIT;
		freed += shmem_free_swp(ptr+offset, ptr+offset+size,
							punch_lock);
		if (need_resched()) {
			shmem_swp_unmap(ptr);
			if (*dir) {
				shmem_dir_unmap(*dir);
				*dir = NULL;
			}
			cond_resched();
			ptr = shmem_swp_map(subdir);
		}
	}
	shmem_swp_unmap(ptr);
	return freed;
}

static void shmem_free_pages(struct list_head *next)
{
	struct page *page;
	int freed = 0;

	do {
		page = container_of(next, struct page, lru);
		next = next->next;
		shmem_dir_free(page);
		freed++;
		if (freed >= LATENCY_LIMIT) {
			cond_resched();
			freed = 0;
		}
	} while (next);
}

static void shmem_truncate_range(struct inode *inode, loff_t start, loff_t end)
{
	struct shmem_inode_info *info = SHMEM_I(inode);
	unsigned long idx;
	unsigned long size;
	unsigned long limit;
	unsigned long stage;
	unsigned long diroff;
	struct page **dir;
	struct page *topdir;
	struct page *middir;
	struct page *subdir;
	swp_entry_t *ptr;
	LIST_HEAD(pages_to_free);
	long nr_pages_to_free = 0;
	long nr_swaps_freed = 0;
	int offset;
	int freed;
	int punch_hole;
	spinlock_t *needs_lock;
	spinlock_t *punch_lock;
	unsigned long upper_limit;

	inode->i_ctime = inode->i_mtime = CURRENT_TIME;
	idx = (start + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
	if (idx >= info->next_index)
		return;

	spin_lock(&info->lock);
	info->flags |= SHMEM_TRUNCATE;
	if (likely(end == (loff_t) -1)) {
		limit = info->next_index;
		upper_limit = SHMEM_MAX_INDEX;
		info->next_index = idx;
		needs_lock = NULL;
		punch_hole = 0;
	} else {
		if (end + 1 >= inode->i_size) {	/* we may free a little more */
			limit = (inode->i_size + PAGE_CACHE_SIZE - 1) >>
							PAGE_CACHE_SHIFT;
			upper_limit = SHMEM_MAX_INDEX;
		} else {
			limit = (end + 1) >> PAGE_CACHE_SHIFT;
			upper_limit = limit;
		}
		needs_lock = &info->lock;
		punch_hole = 1;
	}

	topdir = info->i_indirect;
	if (topdir && idx <= SHMEM_NR_DIRECT && !punch_hole) {
		info->i_indirect = NULL;
		nr_pages_to_free++;
		list_add(&topdir->lru, &pages_to_free);
	}
	spin_unlock(&info->lock);

	if (info->swapped && idx < SHMEM_NR_DIRECT) {
		ptr = info->i_direct;
		size = limit;
		if (size > SHMEM_NR_DIRECT)
			size = SHMEM_NR_DIRECT;
		nr_swaps_freed = shmem_free_swp(ptr+idx, ptr+size, needs_lock);
	}

	/*
	 * If there are no indirect blocks or we are punching a hole
	 * below indirect blocks, nothing to be done.
	 */
	if (!topdir || limit <= SHMEM_NR_DIRECT)
		goto done2;

	/*
	 * The truncation case has already dropped info->lock, and we're safe
	 * because i_size and next_index have already been lowered, preventing
	 * access beyond.  But in the punch_hole case, we still need to take
	 * the lock when updating the swap directory, because there might be
	 * racing accesses by shmem_getpage(SGP_CACHE), shmem_unuse_inode or
	 * shmem_writepage.  However, whenever we find we can remove a whole
	 * directory page (not at the misaligned start or end of the range),
	 * we first NULLify its pointer in the level above, and then have no
	 * need to take the lock when updating its contents: needs_lock and
	 * punch_lock (either pointing to info->lock or NULL) manage this.
	 */

	upper_limit -= SHMEM_NR_DIRECT;
	limit -= SHMEM_NR_DIRECT;
	idx = (idx > SHMEM_NR_DIRECT)? (idx - SHMEM_NR_DIRECT): 0;
	offset = idx % ENTRIES_PER_PAGE;
	idx -= offset;

	dir = shmem_dir_map(topdir);
	stage = ENTRIES_PER_PAGEPAGE/2;
	if (idx < ENTRIES_PER_PAGEPAGE/2) {
		middir = topdir;
		diroff = idx/ENTRIES_PER_PAGE;
	} else {
		dir += ENTRIES_PER_PAGE/2;
		dir += (idx - ENTRIES_PER_PAGEPAGE/2)/ENTRIES_PER_PAGEPAGE;
		while (stage <= idx)
			stage += ENTRIES_PER_PAGEPAGE;
		middir = *dir;
		if (*dir) {
			diroff = ((idx - ENTRIES_PER_PAGEPAGE/2) %
				ENTRIES_PER_PAGEPAGE) / ENTRIES_PER_PAGE;
			if (!diroff && !offset && upper_limit >= stage) {
				if (needs_lock) {
					spin_lock(needs_lock);
					*dir = NULL;
					spin_unlock(needs_lock);
					needs_lock = NULL;
				} else
					*dir = NULL;
				nr_pages_to_free++;
				list_add(&middir->lru, &pages_to_free);
			}
			shmem_dir_unmap(dir);
			dir = shmem_dir_map(middir);
		} else {
			diroff = 0;
			offset = 0;
			idx = stage;
		}
	}

	for (; idx < limit; idx += ENTRIES_PER_PAGE, diroff++) {
		if (unlikely(idx == stage)) {
			shmem_dir_unmap(dir);
			dir = shmem_dir_map(topdir) +
			    ENTRIES_PER_PAGE/2 + idx/ENTRIES_PER_PAGEPAGE;
			while (!*dir) {
				dir++;
				idx += ENTRIES_PER_PAGEPAGE;
				if (idx >= limit)
					goto done1;
			}
			stage = idx + ENTRIES_PER_PAGEPAGE;
			middir = *dir;
			if (punch_hole)
				needs_lock = &info->lock;
			if (upper_limit >= stage) {
				if (needs_lock) {
					spin_lock(needs_lock);
					*dir = NULL;
					spin_unlock(needs_lock);
					needs_lock = NULL;
				} else
					*dir = NULL;
				nr_pages_to_free++;
				list_add(&middir->lru, &pages_to_free);
			}
			shmem_dir_unmap(dir);
			cond_resched();
			dir = shmem_dir_map(middir);
			diroff = 0;
		}
		punch_lock = needs_lock;
		subdir = dir[diroff];
		if (subdir && !offset && upper_limit-idx >= ENTRIES_PER_PAGE) {
			if (needs_lock) {
				spin_lock(needs_lock);
				dir[diroff] = NULL;
				spin_unlock(needs_lock);
				punch_lock = NULL;
			} else
				dir[diroff] = NULL;
			nr_pages_to_free++;
			list_add(&subdir->lru, &pages_to_free);
		}
		if (subdir && page_private(subdir) /* has swap entries */) {
			size = limit - idx;
			if (size > ENTRIES_PER_PAGE)
				size = ENTRIES_PER_PAGE;
			freed = shmem_map_and_free_swp(subdir,
					offset, size, &dir, punch_lock);
			if (!dir)
				dir = shmem_dir_map(middir);
			nr_swaps_freed += freed;
			if (offset || punch_lock) {
				spin_lock(&info->lock);
				set_page_private(subdir,
					page_private(subdir) - freed);
				spin_unlock(&info->lock);
			} else
				BUG_ON(page_private(subdir) != freed);
		}
		offset = 0;
	}
done1:
	shmem_dir_unmap(dir);
done2:
	if (inode->i_mapping->nrpages && (info->flags & SHMEM_PAGEIN)) {
		/*
		 * Call truncate_inode_pages again: racing shmem_unuse_inode
		 * may have swizzled a page in from swap since vmtruncate or
		 * generic_delete_inode did it, before we lowered next_index.
		 * Also, though shmem_getpage checks i_size before adding to
		 * cache, no recheck after: so fix the narrow window there too.
		 *
		 * Recalling truncate_inode_pages_range and unmap_mapping_range
		 * every time for punch_hole (which never got a chance to clear
		 * SHMEM_PAGEIN at the start of vmtruncate_range) is expensive,
		 * yet hardly ever necessary: try to optimize them out later.
		 */
		truncate_inode_pages_range(inode->i_mapping, start, end);
		if (punch_hole)
			unmap_mapping_range(inode->i_mapping, start,
							end - start, 1);
	}

	spin_lock(&info->lock);
	info->flags &= ~SHMEM_TRUNCATE;
	info->swapped -= nr_swaps_freed;
	if (nr_pages_to_free)
		shmem_free_blocks(inode, nr_pages_to_free);
	shmem_recalc_inode(inode);
	spin_unlock(&info->lock);

	/*
	 * Empty swap vector directory pages to be freed?
	 */
	if (!list_empty(&pages_to_free)) {
		pages_to_free.prev->next = NULL;
		shmem_free_pages(pages_to_free.next);
	}
}

static void shmem_truncate(struct inode *inode)
{
	shmem_truncate_range(inode, inode->i_size, (loff_t)-1);
}

static int shmem_notify_change(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	struct page *page = NULL;
	int error;

	if (S_ISREG(inode->i_mode) && (attr->ia_valid & ATTR_SIZE)) {
		if (attr->ia_size < inode->i_size) {
			/*
			 * If truncating down to a partial page, then
			 * if that page is already allocated, hold it
			 * in memory until the truncation is over, so
			 * truncate_partial_page cannnot miss it were
			 * it assigned to swap.
			 */
			if (attr->ia_size & (PAGE_CACHE_SIZE-1)) {
				(void) shmem_getpage(inode,
					attr->ia_size>>PAGE_CACHE_SHIFT,
						&page, SGP_READ, NULL);
				if (page)
					unlock_page(page);
			}
			/*
			 * Reset SHMEM_PAGEIN flag so that shmem_truncate can
			 * detect if any pages might have been added to cache
			 * after truncate_inode_pages.  But we needn't bother
			 * if it's being fully truncated to zero-length: the
			 * nrpages check is efficient enough in that case.
			 */
			if (attr->ia_size) {
				struct shmem_inode_info *info = SHMEM_I(inode);
				spin_lock(&info->lock);
				info->flags &= ~SHMEM_PAGEIN;
				spin_unlock(&info->lock);
			}
		}
	}

	error = inode_change_ok(inode, attr);
	if (!error)
		error = inode_setattr(inode, attr);
#ifdef CONFIG_TMPFS_POSIX_ACL
	if (!error && (attr->ia_valid & ATTR_MODE))
		error = generic_acl_chmod(inode, &shmem_acl_ops);
#endif
	if (page)
		page_cache_release(page);
	return error;
}

static void shmem_delete_inode(struct inode *inode)
{
	struct shmem_inode_info *info = SHMEM_I(inode);

	if (inode->i_op->truncate == shmem_truncate) {
		truncate_inode_pages(inode->i_mapping, 0);
		shmem_unacct_size(info->flags, inode->i_size);
		inode->i_size = 0;
		shmem_truncate(inode);
		struct shmem_sb_info *sbinfo = SHMEM_SB(inode->i_sb);
		if (sbinfo->max_blocks)
			percpu_counter_add(&sbinfo->used_blocks, -freed);
		info->alloced -= freed;
		inode->i_blocks -= freed * BLOCKS_PER_PAGE;
		shmem_unacct_blocks(info->flags, freed);
	}
}

/*
 * Replace item expected in radix tree by a new item, while holding tree lock.
 */
static int shmem_radix_tree_replace(struct address_space *mapping,
			pgoff_t index, void *expected, void *replacement)
{
	void **pslot;
	void *item;

	VM_BUG_ON(!expected);
	VM_BUG_ON(!replacement);
	pslot = radix_tree_lookup_slot(&mapping->page_tree, index);
	if (!pslot)
		return -ENOENT;
	item = radix_tree_deref_slot_protected(pslot, &mapping->tree_lock);
	if (item != expected)
		return -ENOENT;
	radix_tree_replace_slot(pslot, replacement);
	return 0;
}

/*
 * Sometimes, before we decide whether to proceed or to fail, we must check
 * that an entry was not already brought back from swap by a racing thread.
 *
 * Checking page is not enough: by the time a SwapCache page is locked, it
 * might be reused, and again be SwapCache, using the same swap as before.
 */
static bool shmem_confirm_swap(struct address_space *mapping,
			       pgoff_t index, swp_entry_t swap)
{
	void *item;

	rcu_read_lock();
	item = radix_tree_lookup(&mapping->page_tree, index);
	rcu_read_unlock();
	return item == swp_to_radix_entry(swap);
}

/*
 * Like add_to_page_cache_locked, but error if expected item has gone.
 */
static int shmem_add_to_page_cache(struct page *page,
				   struct address_space *mapping,
				   pgoff_t index, void *expected)
{
	int error;

	VM_BUG_ON_PAGE(!PageLocked(page), page);
	VM_BUG_ON_PAGE(!PageSwapBacked(page), page);

	page_cache_get(page);
	page->mapping = mapping;
	page->index = index;

	spin_lock_irq(&mapping->tree_lock);
	if (!expected)
		error = radix_tree_insert(&mapping->page_tree, index, page);
	else
		error = shmem_radix_tree_replace(mapping, index, expected,
								 page);
	if (!error) {
		mapping->nrpages++;
		__inc_zone_page_state(page, NR_FILE_PAGES);
		__inc_zone_page_state(page, NR_SHMEM);
		spin_unlock_irq(&mapping->tree_lock);
	} else {
		page->mapping = NULL;
		spin_unlock_irq(&mapping->tree_lock);
		page_cache_release(page);
	}
	return error;
}

/*
 * Like delete_from_page_cache, but substitutes swap for page.
 */
static void shmem_delete_from_page_cache(struct page *page, void *radswap)
{
	struct address_space *mapping = page->mapping;
	int error;

	spin_lock_irq(&mapping->tree_lock);
	error = shmem_radix_tree_replace(mapping, page->index, page, radswap);
	page->mapping = NULL;
	mapping->nrpages--;
	__dec_zone_page_state(page, NR_FILE_PAGES);
	__dec_zone_page_state(page, NR_SHMEM);
	spin_unlock_irq(&mapping->tree_lock);
	page_cache_release(page);
	BUG_ON(error);
}

/*
 * Remove swap entry from radix tree, free the swap and its page cache.
 */
static int shmem_free_swap(struct address_space *mapping,
			   pgoff_t index, void *radswap)
{
	void *old;

	spin_lock_irq(&mapping->tree_lock);
	old = radix_tree_delete_item(&mapping->page_tree, index, radswap);
	spin_unlock_irq(&mapping->tree_lock);
	if (old != radswap)
		return -ENOENT;
	free_swap_and_cache(radix_to_swp_entry(radswap));
	return 0;
}

/*
 * SysV IPC SHM_UNLOCK restore Unevictable pages to their evictable lists.
 */
void shmem_unlock_mapping(struct address_space *mapping)
{
	struct pagevec pvec;
	pgoff_t indices[PAGEVEC_SIZE];
	pgoff_t index = 0;

	pagevec_init(&pvec, 0);
	/*
	 * Minor point, but we might as well stop if someone else SHM_LOCKs it.
	 */
	while (!mapping_unevictable(mapping)) {
		/*
		 * Avoid pagevec_lookup(): find_get_pages() returns 0 as if it
		 * has finished, if it hits a row of PAGEVEC_SIZE swap entries.
		 */
		pvec.nr = find_get_entries(mapping, index,
					   PAGEVEC_SIZE, pvec.pages, indices);
		if (!pvec.nr)
			break;
		index = indices[pvec.nr - 1] + 1;
		pagevec_remove_exceptionals(&pvec);
		check_move_unevictable_pages(pvec.pages, pvec.nr);
		pagevec_release(&pvec);
		cond_resched();
	}
}

/*
 * Remove range of pages and swap entries from radix tree, and free them.
 * If !unfalloc, truncate or punch hole; if unfalloc, undo failed fallocate.
 */
static void shmem_undo_range(struct inode *inode, loff_t lstart, loff_t lend,
								 bool unfalloc)
{
	struct address_space *mapping = inode->i_mapping;
	struct shmem_inode_info *info = SHMEM_I(inode);
	pgoff_t start = (lstart + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
	pgoff_t end = (lend + 1) >> PAGE_CACHE_SHIFT;
	unsigned int partial_start = lstart & (PAGE_CACHE_SIZE - 1);
	unsigned int partial_end = (lend + 1) & (PAGE_CACHE_SIZE - 1);
	struct pagevec pvec;
	pgoff_t indices[PAGEVEC_SIZE];
	long nr_swaps_freed = 0;
	pgoff_t index;
	int i;

	if (lend == -1)
		end = -1;	/* unsigned, so actually very big */

	pagevec_init(&pvec, 0);
	index = start;
	while (index < end) {
		pvec.nr = find_get_entries(mapping, index,
			min(end - index, (pgoff_t)PAGEVEC_SIZE),
			pvec.pages, indices);
		if (!pvec.nr)
			break;
		for (i = 0; i < pagevec_count(&pvec); i++) {
			struct page *page = pvec.pages[i];

			index = indices[i];
			if (index >= end)
				break;

			if (radix_tree_exceptional_entry(page)) {
				if (unfalloc)
					continue;
				nr_swaps_freed += !shmem_free_swap(mapping,
								index, page);
				continue;
			}

			if (!trylock_page(page))
				continue;
			if (!unfalloc || !PageUptodate(page)) {
				if (page->mapping == mapping) {
					VM_BUG_ON_PAGE(PageWriteback(page), page);
					truncate_inode_page(mapping, page);
				}
			}
			unlock_page(page);
		}
		pagevec_remove_exceptionals(&pvec);
		pagevec_release(&pvec);
		cond_resched();
		index++;
	}

	if (partial_start) {
		struct page *page = NULL;
		shmem_getpage(inode, start - 1, &page, SGP_READ, NULL);
		if (page) {
			unsigned int top = PAGE_CACHE_SIZE;
			if (start > end) {
				top = partial_end;
				partial_end = 0;
			}
			zero_user_segment(page, partial_start, top);
			set_page_dirty(page);
			unlock_page(page);
			page_cache_release(page);
		}
	}
	if (partial_end) {
		struct page *page = NULL;
		shmem_getpage(inode, end, &page, SGP_READ, NULL);
		if (page) {
			zero_user_segment(page, 0, partial_end);
			set_page_dirty(page);
			unlock_page(page);
			page_cache_release(page);
		}
	}
	if (start >= end)
		return;

	index = start;
	while (index < end) {
		cond_resched();

		pvec.nr = find_get_entries(mapping, index,
				min(end - index, (pgoff_t)PAGEVEC_SIZE),
				pvec.pages, indices);
		if (!pvec.nr) {
			/* If all gone or hole-punch or unfalloc, we're done */
			if (index == start || end != -1)
				break;
			/* But if truncating, restart to make sure all gone */
			index = start;
			continue;
		}
		for (i = 0; i < pagevec_count(&pvec); i++) {
			struct page *page = pvec.pages[i];

			index = indices[i];
			if (index >= end)
				break;

			if (radix_tree_exceptional_entry(page)) {
				if (unfalloc)
					continue;
				if (shmem_free_swap(mapping, index, page)) {
					/* Swap was replaced by page: retry */
					index--;
					break;
				}
				nr_swaps_freed++;
				continue;
			}

			lock_page(page);
			if (!unfalloc || !PageUptodate(page)) {
				if (page->mapping == mapping) {
					VM_BUG_ON_PAGE(PageWriteback(page), page);
					truncate_inode_page(mapping, page);
				} else {
					/* Page was replaced by swap: retry */
					unlock_page(page);
					index--;
					break;
				}
			}
			unlock_page(page);
		}
		pagevec_remove_exceptionals(&pvec);
		pagevec_release(&pvec);
		index++;
	}

	spin_lock(&info->lock);
	info->swapped -= nr_swaps_freed;
	shmem_recalc_inode(inode);
	spin_unlock(&info->lock);
}

void shmem_truncate_range(struct inode *inode, loff_t lstart, loff_t lend)
{
	shmem_undo_range(inode, lstart, lend, false);
	inode->i_ctime = inode->i_mtime = CURRENT_TIME;
}
EXPORT_SYMBOL_GPL(shmem_truncate_range);

static int shmem_getattr(struct vfsmount *mnt, struct dentry *dentry,
			 struct kstat *stat)
{
	struct inode *inode = dentry->d_inode;
	struct shmem_inode_info *info = SHMEM_I(inode);

	if (info->alloced - info->swapped != inode->i_mapping->nrpages) {
		spin_lock(&info->lock);
		shmem_recalc_inode(inode);
		spin_unlock(&info->lock);
	}
	generic_fillattr(inode, stat);
	return 0;
}

static int shmem_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = d_inode(dentry);
	struct shmem_inode_info *info = SHMEM_I(inode);
	int error;

	error = inode_change_ok(inode, attr);
	if (error)
		return error;

	if (S_ISREG(inode->i_mode) && (attr->ia_valid & ATTR_SIZE)) {
		loff_t oldsize = inode->i_size;
		loff_t newsize = attr->ia_size;

		/* protected by i_mutex */
		if ((newsize < oldsize && (info->seals & F_SEAL_SHRINK)) ||
		    (newsize > oldsize && (info->seals & F_SEAL_GROW)))
			return -EPERM;

		if (newsize != oldsize) {
			error = shmem_reacct_size(SHMEM_I(inode)->flags,
					oldsize, newsize);
			if (error)
				return error;
			i_size_write(inode, newsize);
			inode->i_ctime = inode->i_mtime = CURRENT_TIME;
		}
		if (newsize <= oldsize) {
			loff_t holebegin = round_up(newsize, PAGE_SIZE);
			if (oldsize > holebegin)
				unmap_mapping_range(inode->i_mapping,
							holebegin, 0, 1);
			if (info->alloced)
				shmem_truncate_range(inode,
							newsize, (loff_t)-1);
			/* unmap again to remove racily COWed private pages */
			if (oldsize > holebegin)
				unmap_mapping_range(inode->i_mapping,
							holebegin, 0, 1);
		}
	}

	setattr_copy(inode, attr);
	if (attr->ia_valid & ATTR_MODE)
		error = posix_acl_chmod(inode, inode->i_mode);
	return error;
}

static void shmem_evict_inode(struct inode *inode)
{
	struct shmem_inode_info *info = SHMEM_I(inode);

	if (inode->i_mapping->a_ops == &shmem_aops) {
		shmem_unacct_size(info->flags, inode->i_size);
		inode->i_size = 0;
		shmem_truncate_range(inode, 0, (loff_t)-1);
		if (!list_empty(&info->swaplist)) {
			mutex_lock(&shmem_swaplist_mutex);
			list_del_init(&info->swaplist);
			mutex_unlock(&shmem_swaplist_mutex);
		}
	}
	BUG_ON(inode->i_blocks);
	} else
		kfree(info->symlink);

	simple_xattrs_free(&info->xattrs);
	WARN_ON(inode->i_blocks);
	shmem_free_inode(inode->i_sb);
	clear_inode(inode);
}

static inline int shmem_find_swp(swp_entry_t entry, swp_entry_t *dir, swp_entry_t *edir)
{
	swp_entry_t *ptr;

	for (ptr = dir; ptr < edir; ptr++) {
		if (ptr->val == entry.val)
			return ptr - dir;
	}
	return -1;
}

static int shmem_unuse_inode(struct shmem_inode_info *info, swp_entry_t entry, struct page *page)
{
	struct inode *inode;
	unsigned long idx;
	unsigned long size;
	unsigned long limit;
	unsigned long stage;
	struct page **dir;
	struct page *subdir;
	swp_entry_t *ptr;
	int offset;
	int error;

	idx = 0;
	ptr = info->i_direct;
	spin_lock(&info->lock);
	if (!info->swapped) {
		list_del_init(&info->swaplist);
		goto lost2;
	}
	limit = info->next_index;
	size = limit;
	if (size > SHMEM_NR_DIRECT)
		size = SHMEM_NR_DIRECT;
	offset = shmem_find_swp(entry, ptr, ptr+size);
	if (offset >= 0)
		goto found;
	if (!info->i_indirect)
		goto lost2;

	dir = shmem_dir_map(info->i_indirect);
	stage = SHMEM_NR_DIRECT + ENTRIES_PER_PAGEPAGE/2;

	for (idx = SHMEM_NR_DIRECT; idx < limit; idx += ENTRIES_PER_PAGE, dir++) {
		if (unlikely(idx == stage)) {
			shmem_dir_unmap(dir-1);
			if (cond_resched_lock(&info->lock)) {
				/* check it has not been truncated */
				if (limit > info->next_index) {
					limit = info->next_index;
					if (idx >= limit)
						goto lost2;
				}
			}
			dir = shmem_dir_map(info->i_indirect) +
			    ENTRIES_PER_PAGE/2 + idx/ENTRIES_PER_PAGEPAGE;
			while (!*dir) {
				dir++;
				idx += ENTRIES_PER_PAGEPAGE;
				if (idx >= limit)
					goto lost1;
			}
			stage = idx + ENTRIES_PER_PAGEPAGE;
			subdir = *dir;
			shmem_dir_unmap(dir);
			dir = shmem_dir_map(subdir);
		}
		subdir = *dir;
		if (subdir && page_private(subdir)) {
			ptr = shmem_swp_map(subdir);
			size = limit - idx;
			if (size > ENTRIES_PER_PAGE)
				size = ENTRIES_PER_PAGE;
			offset = shmem_find_swp(entry, ptr, ptr+size);
			shmem_swp_unmap(ptr);
			if (offset >= 0) {
				shmem_dir_unmap(dir);
				goto found;
			}
		}
	}
lost1:
	shmem_dir_unmap(dir-1);
lost2:
	spin_unlock(&info->lock);
	return 0;
found:
	idx += offset;
	inode = igrab(&info->vfs_inode);
	spin_unlock(&info->lock);

	/*
	 * Move _head_ to start search for next from here.
	 * But be careful: shmem_delete_inode checks list_empty without taking
	 * mutex, and there's an instant in list_move_tail when info->swaplist
	 * would appear empty, if it were the only one on shmem_swaplist.  We
	 * could avoid doing it if inode NULL; or use this minor optimization.
	 */
	if (shmem_swaplist.next != &info->swaplist)
		list_move_tail(&shmem_swaplist, &info->swaplist);
	mutex_unlock(&shmem_swaplist_mutex);

	error = 1;
	if (!inode)
		goto out;
	/* Precharge page using GFP_KERNEL while we can wait */
	error = mem_cgroup_cache_charge(page, current->mm, GFP_KERNEL);
	if (error)
		goto out;
	error = radix_tree_preload(GFP_KERNEL);
	if (error) {
		mem_cgroup_uncharge_cache_page(page);
		goto out;
	}
	error = 1;

	spin_lock(&info->lock);
	ptr = shmem_swp_entry(info, idx, NULL);
	if (ptr && ptr->val == entry.val) {
		error = add_to_page_cache_locked(page, inode->i_mapping,
						idx, GFP_NOWAIT);
		/* does mem_cgroup_uncharge_cache_page on error */
	} else	/* we must compensate for our precharge above */
		mem_cgroup_uncharge_cache_page(page);

	if (error == -EEXIST) {
		struct page *filepage = find_get_page(inode->i_mapping, idx);
		error = 1;
		if (filepage) {
			/*
			 * There might be a more uptodate page coming down
			 * from a stacked writepage: forget our swappage if so.
			 */
			if (PageUptodate(filepage))
				error = 0;
			page_cache_release(filepage);
		}
	}
	if (!error) {
		delete_from_swap_cache(page);
		set_page_dirty(page);
		info->flags |= SHMEM_PAGEIN;
		shmem_swp_set(info, ptr, 0);
		swap_free(entry);
		error = 1;	/* not an error, but entry was found */
	}
	if (ptr)
		shmem_swp_unmap(ptr);
	spin_unlock(&info->lock);
	radix_tree_preload_end();
out:
	unlock_page(page);
	page_cache_release(page);
	iput(inode);		/* allows for NULL */
/*
 * If swap found in inode, free it and move page from swapcache to filecache.
 */
static int shmem_unuse_inode(struct shmem_inode_info *info,
			     swp_entry_t swap, struct page **pagep)
{
	struct address_space *mapping = info->vfs_inode.i_mapping;
	void *radswap;
	pgoff_t index;
	gfp_t gfp;
	int error = 0;

	radswap = swp_to_radix_entry(swap);
	index = radix_tree_locate_item(&mapping->page_tree, radswap);
	if (index == -1)
		return -EAGAIN;	/* tell shmem_unuse we found nothing */

	/*
	 * Move _head_ to start search for next from here.
	 * But be careful: shmem_evict_inode checks list_empty without taking
	 * mutex, and there's an instant in list_move_tail when info->swaplist
	 * would appear empty, if it were the only one on shmem_swaplist.
	 */
	if (shmem_swaplist.next != &info->swaplist)
		list_move_tail(&shmem_swaplist, &info->swaplist);

	gfp = mapping_gfp_mask(mapping);
	if (shmem_should_replace_page(*pagep, gfp)) {
		mutex_unlock(&shmem_swaplist_mutex);
		error = shmem_replace_page(pagep, gfp, info, index);
		mutex_lock(&shmem_swaplist_mutex);
		/*
		 * We needed to drop mutex to make that restrictive page
		 * allocation, but the inode might have been freed while we
		 * dropped it: although a racing shmem_evict_inode() cannot
		 * complete without emptying the radix_tree, our page lock
		 * on this swapcache page is not enough to prevent that -
		 * free_swap_and_cache() of our swap entry will only
		 * trylock_page(), removing swap from radix_tree whatever.
		 *
		 * We must not proceed to shmem_add_to_page_cache() if the
		 * inode has been freed, but of course we cannot rely on
		 * inode or mapping or info to check that.  However, we can
		 * safely check if our swap entry is still in use (and here
		 * it can't have got reused for another page): if it's still
		 * in use, then the inode cannot have been freed yet, and we
		 * can safely proceed (if it's no longer in use, that tells
		 * nothing about the inode, but we don't need to unuse swap).
		 */
		if (!page_swapcount(*pagep))
			error = -ENOENT;
	}

	/*
	 * We rely on shmem_swaplist_mutex, not only to protect the swaplist,
	 * but also to hold up shmem_evict_inode(): so inode cannot be freed
	 * beneath us (pagelock doesn't help until the page is in pagecache).
	 */
	if (!error)
		error = shmem_add_to_page_cache(*pagep, mapping, index,
						radswap);
	if (error != -ENOMEM) {
		/*
		 * Truncation and eviction use free_swap_and_cache(), which
		 * only does trylock page: if we raced, best clean up here.
		 */
		delete_from_swap_cache(*pagep);
		set_page_dirty(*pagep);
		if (!error) {
			spin_lock(&info->lock);
			info->swapped--;
			spin_unlock(&info->lock);
			swap_free(swap);
		}
	}
	return error;
}

/*
 * shmem_unuse() search for an eventually swapped out shmem page.
 */
int shmem_unuse(swp_entry_t entry, struct page *page)
{
	struct list_head *p, *next;
	struct shmem_inode_info *info;
	int found = 0;

	mutex_lock(&shmem_swaplist_mutex);
	list_for_each_safe(p, next, &shmem_swaplist) {
		info = list_entry(p, struct shmem_inode_info, swaplist);
		found = shmem_unuse_inode(info, entry, page);
		cond_resched();
		if (found)
			goto out;
	}
	mutex_unlock(&shmem_swaplist_mutex);
out:	return found;	/* 0 or 1 or -ENOMEM */
 * Search through swapped inodes to find and replace swap by page.
 */
int shmem_unuse(swp_entry_t swap, struct page *page)
{
	struct list_head *this, *next;
	struct shmem_inode_info *info;
	struct mem_cgroup *memcg;
	int error = 0;

	/*
	 * There's a faint possibility that swap page was replaced before
	 * caller locked it: caller will come back later with the right page.
	 */
	if (unlikely(!PageSwapCache(page) || page_private(page) != swap.val))
		goto out;

	/*
	 * Charge page using GFP_KERNEL while we can wait, before taking
	 * the shmem_swaplist_mutex which might hold up shmem_writepage().
	 * Charged back to the user (not to caller) when swap account is used.
	 */
	error = mem_cgroup_try_charge(page, current->mm, GFP_KERNEL, &memcg);
	if (error)
		goto out;
	/* No radix_tree_preload: swap entry keeps a place for page in tree */
	error = -EAGAIN;

	mutex_lock(&shmem_swaplist_mutex);
	list_for_each_safe(this, next, &shmem_swaplist) {
		info = list_entry(this, struct shmem_inode_info, swaplist);
		if (info->swapped)
			error = shmem_unuse_inode(info, swap, &page);
		else
			list_del_init(&info->swaplist);
		cond_resched();
		if (error != -EAGAIN)
			break;
		/* found nothing in this: move on to search the next */
	}
	mutex_unlock(&shmem_swaplist_mutex);

	if (error) {
		if (error != -ENOMEM)
			error = 0;
		mem_cgroup_cancel_charge(page, memcg);
	} else
		mem_cgroup_commit_charge(page, memcg, true);
out:
	unlock_page(page);
	page_cache_release(page);
	return error;
}

/*
 * Move the page from the page cache to the swap cache.
 */
static int shmem_writepage(struct page *page, struct writeback_control *wbc)
{
	struct shmem_inode_info *info;
	swp_entry_t *entry, swap;
	struct address_space *mapping;
	unsigned long index;
	struct inode *inode;
	struct address_space *mapping;
	struct inode *inode;
	swp_entry_t swap;
	pgoff_t index;

	BUG_ON(!PageLocked(page));
	mapping = page->mapping;
	index = page->index;
	inode = mapping->host;
	info = SHMEM_I(inode);
	if (info->flags & VM_LOCKED)
		goto redirty;
	if (!total_swap_pages)
		goto redirty;

	/*
	 * shmem_backing_dev_info's capabilities prevent regular writeback or
	 * sync from ever calling shmem_writepage; but a stacking filesystem
	 * may use the ->writepage of its underlying filesystem, in which case
	 * tmpfs should write out to swap only in response to memory pressure,
	 * and not for pdflush or sync.  However, in those cases, we do still
	 * want to check if there's a redundant swappage to be discarded.
	 */
	if (wbc->for_reclaim)
		swap = get_swap_page();
	else
		swap.val = 0;

	spin_lock(&info->lock);
	if (index >= info->next_index) {
		BUG_ON(!(info->flags & SHMEM_TRUNCATE));
		goto unlock;
	}
	entry = shmem_swp_entry(info, index, NULL);
	if (entry->val) {
		/*
		 * The more uptodate page coming down from a stacked
		 * writepage should replace our old swappage.
		 */
		free_swap_and_cache(*entry);
		shmem_swp_set(info, entry, 0);
	}
	shmem_recalc_inode(inode);

	if (swap.val && add_to_swap_cache(page, swap, GFP_ATOMIC) == 0) {
		remove_from_page_cache(page);
		shmem_swp_set(info, entry, swap.val);
		shmem_swp_unmap(entry);
		if (list_empty(&info->swaplist))
			inode = igrab(inode);
		else
			inode = NULL;
		spin_unlock(&info->lock);
		swap_duplicate(swap);
		BUG_ON(page_mapped(page));
		page_cache_release(page);	/* pagecache ref */
		set_page_dirty(page);
		unlock_page(page);
		if (inode) {
			mutex_lock(&shmem_swaplist_mutex);
			/* move instead of add in case we're racing */
			list_move_tail(&info->swaplist, &shmem_swaplist);
			mutex_unlock(&shmem_swaplist_mutex);
			iput(inode);
		}
		return 0;
	}

	shmem_swp_unmap(entry);
unlock:
	spin_unlock(&info->lock);
	swap_free(swap);
	 * Our capabilities prevent regular writeback or sync from ever calling
	 * shmem_writepage; but a stacking filesystem might use ->writepage of
	 * its underlying filesystem, in which case tmpfs should write out to
	 * swap only in response to memory pressure, and not for the writeback
	 * threads or sync.
	 */
	if (!wbc->for_reclaim) {
		WARN_ON_ONCE(1);	/* Still happens? Tell us about it! */
		goto redirty;
	}

	/*
	 * This is somewhat ridiculous, but without plumbing a SWAP_MAP_FALLOC
	 * value into swapfile.c, the only way we can correctly account for a
	 * fallocated page arriving here is now to initialize it and write it.
	 *
	 * That's okay for a page already fallocated earlier, but if we have
	 * not yet completed the fallocation, then (a) we want to keep track
	 * of this page in case we have to undo it, and (b) it may not be a
	 * good idea to continue anyway, once we're pushing into swap.  So
	 * reactivate the page, and let shmem_fallocate() quit when too many.
	 */
	if (!PageUptodate(page)) {
		if (inode->i_private) {
			struct shmem_falloc *shmem_falloc;
			spin_lock(&inode->i_lock);
			shmem_falloc = inode->i_private;
			if (shmem_falloc &&
			    !shmem_falloc->waitq &&
			    index >= shmem_falloc->start &&
			    index < shmem_falloc->next)
				shmem_falloc->nr_unswapped++;
			else
				shmem_falloc = NULL;
			spin_unlock(&inode->i_lock);
			if (shmem_falloc)
				goto redirty;
		}
		clear_highpage(page);
		flush_dcache_page(page);
		SetPageUptodate(page);
	}

	swap = get_swap_page();
	if (!swap.val)
		goto redirty;

	/*
	 * Add inode to shmem_unuse()'s list of swapped-out inodes,
	 * if it's not already there.  Do it now before the page is
	 * moved to swap cache, when its pagelock no longer protects
	 * the inode from eviction.  But don't unlock the mutex until
	 * we've incremented swapped, because shmem_unuse_inode() will
	 * prune a !swapped inode from the swaplist under this mutex.
	 */
	mutex_lock(&shmem_swaplist_mutex);
	if (list_empty(&info->swaplist))
		list_add_tail(&info->swaplist, &shmem_swaplist);

	if (add_to_swap_cache(page, swap, GFP_ATOMIC) == 0) {
		spin_lock(&info->lock);
		shmem_recalc_inode(inode);
		info->swapped++;
		spin_unlock(&info->lock);

		swap_shmem_alloc(swap);
		shmem_delete_from_page_cache(page, swp_to_radix_entry(swap));

		mutex_unlock(&shmem_swaplist_mutex);
		BUG_ON(page_mapped(page));
		swap_writepage(page, wbc);
		return 0;
	}

	mutex_unlock(&shmem_swaplist_mutex);
	swapcache_free(swap);
redirty:
	set_page_dirty(page);
	if (wbc->for_reclaim)
		return AOP_WRITEPAGE_ACTIVATE;	/* Return with page locked */
	unlock_page(page);
	return 0;
}

#ifdef CONFIG_NUMA
#ifdef CONFIG_TMPFS
static void shmem_show_mpol(struct seq_file *seq, struct mempolicy *mpol)
{
	char buffer[64];

	if (!mpol || mpol->mode == MPOL_DEFAULT)
		return;		/* show nothing */

	mpol_to_str(buffer, sizeof(buffer), mpol, 1);
	mpol_to_str(buffer, sizeof(buffer), mpol);

	seq_printf(seq, ",mpol=%s", buffer);
}

static struct mempolicy *shmem_get_sbmpol(struct shmem_sb_info *sbinfo)
{
	struct mempolicy *mpol = NULL;
	if (sbinfo->mpol) {
		spin_lock(&sbinfo->stat_lock);	/* prevent replace/use races */
		mpol = sbinfo->mpol;
		mpol_get(mpol);
		spin_unlock(&sbinfo->stat_lock);
	}
	return mpol;
}
#endif /* CONFIG_TMPFS */

static struct page *shmem_swapin(swp_entry_t entry, gfp_t gfp,
			struct shmem_inode_info *info, unsigned long idx)
{
	struct mempolicy mpol, *spol;
	struct vm_area_struct pvma;
	struct page *page;

	spol = mpol_cond_copy(&mpol,
				mpol_shared_policy_lookup(&info->policy, idx));

	/* Create a pseudo vma that just contains the policy */
	pvma.vm_start = 0;
	pvma.vm_pgoff = idx;
	pvma.vm_ops = NULL;
	pvma.vm_policy = spol;
	page = swapin_readahead(entry, gfp, &pvma, 0);
static struct page *shmem_swapin(swp_entry_t swap, gfp_t gfp,
			struct shmem_inode_info *info, pgoff_t index)
{
	struct vm_area_struct pvma;
	struct page *page;

	/* Create a pseudo vma that just contains the policy */
	pvma.vm_start = 0;
	/* Bias interleave by inode number to distribute better across nodes */
	pvma.vm_pgoff = index + info->vfs_inode.i_ino;
	pvma.vm_ops = NULL;
	pvma.vm_policy = mpol_shared_policy_lookup(&info->policy, index);

	page = swapin_readahead(swap, gfp, &pvma, 0);

	/* Drop reference taken by mpol_shared_policy_lookup() */
	mpol_cond_put(pvma.vm_policy);

	return page;
}

static struct page *shmem_alloc_page(gfp_t gfp,
			struct shmem_inode_info *info, unsigned long idx)
{
	struct vm_area_struct pvma;

	/* Create a pseudo vma that just contains the policy */
	pvma.vm_start = 0;
	pvma.vm_pgoff = idx;
	pvma.vm_ops = NULL;
	pvma.vm_policy = mpol_shared_policy_lookup(&info->policy, idx);

	/*
	 * alloc_page_vma() will drop the shared policy reference
	 */
	return alloc_page_vma(gfp, &pvma, 0);
}
#else /* !CONFIG_NUMA */
#ifdef CONFIG_TMPFS
static inline void shmem_show_mpol(struct seq_file *seq, struct mempolicy *p)
			struct shmem_inode_info *info, pgoff_t index)
{
	struct vm_area_struct pvma;
	struct page *page;

	/* Create a pseudo vma that just contains the policy */
	pvma.vm_start = 0;
	/* Bias interleave by inode number to distribute better across nodes */
	pvma.vm_pgoff = index + info->vfs_inode.i_ino;
	pvma.vm_ops = NULL;
	pvma.vm_policy = mpol_shared_policy_lookup(&info->policy, index);

	page = alloc_page_vma(gfp, &pvma, 0);

	/* Drop reference taken by mpol_shared_policy_lookup() */
	mpol_cond_put(pvma.vm_policy);

	return page;
}
#else /* !CONFIG_NUMA */
#ifdef CONFIG_TMPFS
static inline void shmem_show_mpol(struct seq_file *seq, struct mempolicy *mpol)
{
}
#endif /* CONFIG_TMPFS */

static inline struct page *shmem_swapin(swp_entry_t entry, gfp_t gfp,
			struct shmem_inode_info *info, unsigned long idx)
{
	return swapin_readahead(entry, gfp, NULL, 0);
}

static inline struct page *shmem_alloc_page(gfp_t gfp,
			struct shmem_inode_info *info, unsigned long idx)
static inline struct page *shmem_swapin(swp_entry_t swap, gfp_t gfp,
			struct shmem_inode_info *info, pgoff_t index)
{
	return swapin_readahead(swap, gfp, NULL, 0);
}

static inline struct page *shmem_alloc_page(gfp_t gfp,
			struct shmem_inode_info *info, pgoff_t index)
{
	return alloc_page(gfp);
}
#endif /* CONFIG_NUMA */

#if !defined(CONFIG_NUMA) || !defined(CONFIG_TMPFS)
static inline struct mempolicy *shmem_get_sbmpol(struct shmem_sb_info *sbinfo)
{
	return NULL;
}
#endif

/*
 * shmem_getpage - either get the page from swap or allocate a new one
 * When a page is moved from swapcache to shmem filecache (either by the
 * usual swapin of shmem_getpage_gfp(), or by the less common swapoff of
 * shmem_unuse_inode()), it may have been read in earlier from swap, in
 * ignorance of the mapping it belongs to.  If that mapping has special
 * constraints (like the gma500 GEM driver, which requires RAM below 4GB),
 * we may need to copy to a suitable page before moving to filecache.
 *
 * In a future release, this may well be extended to respect cpuset and
 * NUMA mempolicy, and applied also to anonymous pages in do_swap_page();
 * but for now it is a simple matter of zone.
 */
static bool shmem_should_replace_page(struct page *page, gfp_t gfp)
{
	return page_zonenum(page) > gfp_zone(gfp);
}

static int shmem_replace_page(struct page **pagep, gfp_t gfp,
				struct shmem_inode_info *info, pgoff_t index)
{
	struct page *oldpage, *newpage;
	struct address_space *swap_mapping;
	pgoff_t swap_index;
	int error;

	oldpage = *pagep;
	swap_index = page_private(oldpage);
	swap_mapping = page_mapping(oldpage);

	/*
	 * We have arrived here because our zones are constrained, so don't
	 * limit chance of success by further cpuset and node constraints.
	 */
	gfp &= ~GFP_CONSTRAINT_MASK;
	newpage = shmem_alloc_page(gfp, info, index);
	if (!newpage)
		return -ENOMEM;

	page_cache_get(newpage);
	copy_highpage(newpage, oldpage);
	flush_dcache_page(newpage);

	__set_page_locked(newpage);
	SetPageUptodate(newpage);
	SetPageSwapBacked(newpage);
	set_page_private(newpage, swap_index);
	SetPageSwapCache(newpage);

	/*
	 * Our caller will very soon move newpage out of swapcache, but it's
	 * a nice clean interface for us to replace oldpage by newpage there.
	 */
	spin_lock_irq(&swap_mapping->tree_lock);
	error = shmem_radix_tree_replace(swap_mapping, swap_index, oldpage,
								   newpage);
	if (!error) {
		__inc_zone_page_state(newpage, NR_FILE_PAGES);
		__dec_zone_page_state(oldpage, NR_FILE_PAGES);
	}
	spin_unlock_irq(&swap_mapping->tree_lock);

	if (unlikely(error)) {
		/*
		 * Is this possible?  I think not, now that our callers check
		 * both PageSwapCache and page_private after getting page lock;
		 * but be defensive.  Reverse old to newpage for clear and free.
		 */
		oldpage = newpage;
	} else {
		mem_cgroup_replace_page(oldpage, newpage);
		lru_cache_add_anon(newpage);
		*pagep = newpage;
	}

	ClearPageSwapCache(oldpage);
	set_page_private(oldpage, 0);

	unlock_page(oldpage);
	page_cache_release(oldpage);
	page_cache_release(oldpage);
	return error;
}

/*
 * shmem_getpage_gfp - find page in cache, or get from swap, or allocate
 *
 * If we allocate a new one we do not mark it dirty. That's up to the
 * vm. If we swap it in we mark it dirty since we also free the swap
 * entry since a page cannot live in both the swap and page cache
 */
static int shmem_getpage(struct inode *inode, unsigned long idx,
			struct page **pagep, enum sgp_type sgp, int *type)
{
	struct address_space *mapping = inode->i_mapping;
	struct shmem_inode_info *info = SHMEM_I(inode);
	struct shmem_sb_info *sbinfo;
	struct page *filepage = *pagep;
	struct page *swappage;
	swp_entry_t *entry;
	swp_entry_t swap;
	gfp_t gfp;
	int error;

	if (idx >= SHMEM_MAX_INDEX)
		return -EFBIG;

	if (type)
		*type = 0;

	/*
	 * Normally, filepage is NULL on entry, and either found
	 * uptodate immediately, or allocated and zeroed, or read
	 * in under swappage, which is then assigned to filepage.
	 * But shmem_readpage (required for splice) passes in a locked
	 * filepage, which may be found not uptodate by other callers
	 * too, and may need to be copied from the swappage read in.
	 */
repeat:
	if (!filepage)
		filepage = find_lock_page(mapping, idx);
	if (filepage && PageUptodate(filepage))
		goto done;
	error = 0;
	gfp = mapping_gfp_mask(mapping);
	if (!filepage) {
		/*
		 * Try to preload while we can wait, to not make a habit of
		 * draining atomic reserves; but don't latch on to this cpu.
		 */
		error = radix_tree_preload(gfp & ~__GFP_HIGHMEM);
		if (error)
			goto failed;
		radix_tree_preload_end();
	}

	spin_lock(&info->lock);
	shmem_recalc_inode(inode);
	entry = shmem_swp_alloc(info, idx, sgp);
	if (IS_ERR(entry)) {
		spin_unlock(&info->lock);
		error = PTR_ERR(entry);
		goto failed;
	}
	swap = *entry;

	if (swap.val) {
		/* Look it up and read it in.. */
		swappage = lookup_swap_cache(swap);
		if (!swappage) {
			shmem_swp_unmap(entry);
			/* here we actually do the io */
			if (type && !(*type & VM_FAULT_MAJOR)) {
				__count_vm_event(PGMAJFAULT);
				*type |= VM_FAULT_MAJOR;
			}
			spin_unlock(&info->lock);
			swappage = shmem_swapin(swap, gfp, info, idx);
			if (!swappage) {
				spin_lock(&info->lock);
				entry = shmem_swp_alloc(info, idx, sgp);
				if (IS_ERR(entry))
					error = PTR_ERR(entry);
				else {
					if (entry->val == swap.val)
						error = -ENOMEM;
					shmem_swp_unmap(entry);
				}
				spin_unlock(&info->lock);
				if (error)
					goto failed;
				goto repeat;
			}
			wait_on_page_locked(swappage);
			page_cache_release(swappage);
			goto repeat;
		}

		/* We have to do this with page locked to prevent races */
		if (!trylock_page(swappage)) {
			shmem_swp_unmap(entry);
			spin_unlock(&info->lock);
			wait_on_page_locked(swappage);
			page_cache_release(swappage);
			goto repeat;
		}
		if (PageWriteback(swappage)) {
			shmem_swp_unmap(entry);
			spin_unlock(&info->lock);
			wait_on_page_writeback(swappage);
			unlock_page(swappage);
			page_cache_release(swappage);
			goto repeat;
		}
		if (!PageUptodate(swappage)) {
			shmem_swp_unmap(entry);
			spin_unlock(&info->lock);
			unlock_page(swappage);
			page_cache_release(swappage);
			error = -EIO;
			goto failed;
		}

		if (filepage) {
			shmem_swp_set(info, entry, 0);
			shmem_swp_unmap(entry);
			delete_from_swap_cache(swappage);
			spin_unlock(&info->lock);
			copy_highpage(filepage, swappage);
			unlock_page(swappage);
			page_cache_release(swappage);
			flush_dcache_page(filepage);
			SetPageUptodate(filepage);
			set_page_dirty(filepage);
			swap_free(swap);
		} else if (!(error = add_to_page_cache_locked(swappage, mapping,
					idx, GFP_NOWAIT))) {
			info->flags |= SHMEM_PAGEIN;
			shmem_swp_set(info, entry, 0);
			shmem_swp_unmap(entry);
			delete_from_swap_cache(swappage);
			spin_unlock(&info->lock);
			filepage = swappage;
			set_page_dirty(filepage);
			swap_free(swap);
		} else {
			shmem_swp_unmap(entry);
			spin_unlock(&info->lock);
			unlock_page(swappage);
			page_cache_release(swappage);
			if (error == -ENOMEM) {
				/* allow reclaim from this memory cgroup */
				error = mem_cgroup_shrink_usage(current->mm,
								gfp);
				if (error)
					goto failed;
			}
			goto repeat;
		}
	} else if (sgp == SGP_READ && !filepage) {
		shmem_swp_unmap(entry);
		filepage = find_get_page(mapping, idx);
		if (filepage &&
		    (!PageUptodate(filepage) || !trylock_page(filepage))) {
			spin_unlock(&info->lock);
			wait_on_page_locked(filepage);
			page_cache_release(filepage);
			filepage = NULL;
			goto repeat;
		}
		spin_unlock(&info->lock);
	} else {
		shmem_swp_unmap(entry);
		sbinfo = SHMEM_SB(inode->i_sb);
		if (sbinfo->max_blocks) {
			spin_lock(&sbinfo->stat_lock);
			if (sbinfo->free_blocks == 0 ||
			    shmem_acct_block(info->flags)) {
				spin_unlock(&sbinfo->stat_lock);
				spin_unlock(&info->lock);
				error = -ENOSPC;
				goto failed;
			}
			sbinfo->free_blocks--;
			inode->i_blocks += BLOCKS_PER_PAGE;
			spin_unlock(&sbinfo->stat_lock);
		} else if (shmem_acct_block(info->flags)) {
			spin_unlock(&info->lock);
			error = -ENOSPC;
			goto failed;
		}

		if (!filepage) {
			int ret;

			spin_unlock(&info->lock);
			filepage = shmem_alloc_page(gfp, info, idx);
			if (!filepage) {
				shmem_unacct_blocks(info->flags, 1);
				shmem_free_blocks(inode, 1);
				error = -ENOMEM;
				goto failed;
			}

			/* Precharge page while we can wait, compensate after */
			error = mem_cgroup_cache_charge(filepage, current->mm,
							gfp & ~__GFP_HIGHMEM);
			if (error) {
				page_cache_release(filepage);
				shmem_unacct_blocks(info->flags, 1);
				shmem_free_blocks(inode, 1);
				filepage = NULL;
				goto failed;
			}

			spin_lock(&info->lock);
			entry = shmem_swp_alloc(info, idx, sgp);
			if (IS_ERR(entry))
				error = PTR_ERR(entry);
			else {
				swap = *entry;
				shmem_swp_unmap(entry);
			}
			ret = error || swap.val;
			if (ret)
				mem_cgroup_uncharge_cache_page(filepage);
			else
				ret = add_to_page_cache_lru(filepage, mapping,
						idx, GFP_NOWAIT);
			/*
			 * At add_to_page_cache_lru() failure, uncharge will
			 * be done automatically.
			 */
			if (ret) {
				spin_unlock(&info->lock);
				page_cache_release(filepage);
				shmem_unacct_blocks(info->flags, 1);
				shmem_free_blocks(inode, 1);
				filepage = NULL;
				if (error)
					goto failed;
				goto repeat;
			}
			info->flags |= SHMEM_PAGEIN;
		}

		info->alloced++;
		spin_unlock(&info->lock);
		clear_highpage(filepage);
		flush_dcache_page(filepage);
		SetPageUptodate(filepage);
		if (sgp == SGP_DIRTY)
			set_page_dirty(filepage);
	}
done:
	*pagep = filepage;
	return 0;

failed:
	if (*pagep != filepage) {
		unlock_page(filepage);
		page_cache_release(filepage);
	}
static int shmem_getpage_gfp(struct inode *inode, pgoff_t index,
	struct page **pagep, enum sgp_type sgp, gfp_t gfp, int *fault_type)
{
	struct address_space *mapping = inode->i_mapping;
	struct shmem_inode_info *info;
	struct shmem_sb_info *sbinfo;
	struct mem_cgroup *memcg;
	struct page *page;
	swp_entry_t swap;
	int error;
	int once = 0;
	int alloced = 0;

	if (index > (MAX_LFS_FILESIZE >> PAGE_CACHE_SHIFT))
		return -EFBIG;
repeat:
	swap.val = 0;
	page = find_lock_entry(mapping, index);
	if (radix_tree_exceptional_entry(page)) {
		swap = radix_to_swp_entry(page);
		page = NULL;
	}

	if (sgp != SGP_WRITE && sgp != SGP_FALLOC &&
	    ((loff_t)index << PAGE_CACHE_SHIFT) >= i_size_read(inode)) {
		error = -EINVAL;
		goto unlock;
	}

	if (page && sgp == SGP_WRITE)
		mark_page_accessed(page);

	/* fallocated page? */
	if (page && !PageUptodate(page)) {
		if (sgp != SGP_READ)
			goto clear;
		unlock_page(page);
		page_cache_release(page);
		page = NULL;
	}
	if (page || (sgp == SGP_READ && !swap.val)) {
		*pagep = page;
		return 0;
	}

	/*
	 * Fast cache lookup did not find it:
	 * bring it back from swap or allocate.
	 */
	info = SHMEM_I(inode);
	sbinfo = SHMEM_SB(inode->i_sb);

	if (swap.val) {
		/* Look it up and read it in.. */
		page = lookup_swap_cache(swap);
		if (!page) {
			/* here we actually do the io */
			if (fault_type)
				*fault_type |= VM_FAULT_MAJOR;
			page = shmem_swapin(swap, gfp, info, index);
			if (!page) {
				error = -ENOMEM;
				goto failed;
			}
		}

		/* We have to do this with page locked to prevent races */
		lock_page(page);
		if (!PageSwapCache(page) || page_private(page) != swap.val ||
		    !shmem_confirm_swap(mapping, index, swap)) {
			error = -EEXIST;	/* try again */
			goto unlock;
		}
		if (!PageUptodate(page)) {
			error = -EIO;
			goto failed;
		}
		wait_on_page_writeback(page);

		if (shmem_should_replace_page(page, gfp)) {
			error = shmem_replace_page(&page, gfp, info, index);
			if (error)
				goto failed;
		}

		error = mem_cgroup_try_charge(page, current->mm, gfp, &memcg);
		if (!error) {
			error = shmem_add_to_page_cache(page, mapping, index,
						swp_to_radix_entry(swap));
			/*
			 * We already confirmed swap under page lock, and make
			 * no memory allocation here, so usually no possibility
			 * of error; but free_swap_and_cache() only trylocks a
			 * page, so it is just possible that the entry has been
			 * truncated or holepunched since swap was confirmed.
			 * shmem_undo_range() will have done some of the
			 * unaccounting, now delete_from_swap_cache() will do
			 * the rest.
			 * Reset swap.val? No, leave it so "failed" goes back to
			 * "repeat": reading a hole and writing should succeed.
			 */
			if (error) {
				mem_cgroup_cancel_charge(page, memcg);
				delete_from_swap_cache(page);
			}
		}
		if (error)
			goto failed;

		mem_cgroup_commit_charge(page, memcg, true);

		spin_lock(&info->lock);
		info->swapped--;
		shmem_recalc_inode(inode);
		spin_unlock(&info->lock);

		if (sgp == SGP_WRITE)
			mark_page_accessed(page);

		delete_from_swap_cache(page);
		set_page_dirty(page);
		swap_free(swap);

	} else {
		if (shmem_acct_block(info->flags)) {
			error = -ENOSPC;
			goto failed;
		}
		if (sbinfo->max_blocks) {
			if (percpu_counter_compare(&sbinfo->used_blocks,
						sbinfo->max_blocks) >= 0) {
				error = -ENOSPC;
				goto unacct;
			}
			percpu_counter_inc(&sbinfo->used_blocks);
		}

		page = shmem_alloc_page(gfp, info, index);
		if (!page) {
			error = -ENOMEM;
			goto decused;
		}

		__SetPageSwapBacked(page);
		__set_page_locked(page);
		if (sgp == SGP_WRITE)
			__SetPageReferenced(page);

		error = mem_cgroup_try_charge(page, current->mm, gfp, &memcg);
		if (error)
			goto decused;
		error = radix_tree_maybe_preload(gfp & GFP_RECLAIM_MASK);
		if (!error) {
			error = shmem_add_to_page_cache(page, mapping, index,
							NULL);
			radix_tree_preload_end();
		}
		if (error) {
			mem_cgroup_cancel_charge(page, memcg);
			goto decused;
		}
		mem_cgroup_commit_charge(page, memcg, false);
		lru_cache_add_anon(page);

		spin_lock(&info->lock);
		info->alloced++;
		inode->i_blocks += BLOCKS_PER_PAGE;
		shmem_recalc_inode(inode);
		spin_unlock(&info->lock);
		alloced = true;

		/*
		 * Let SGP_FALLOC use the SGP_WRITE optimization on a new page.
		 */
		if (sgp == SGP_FALLOC)
			sgp = SGP_WRITE;
clear:
		/*
		 * Let SGP_WRITE caller clear ends if write does not fill page;
		 * but SGP_FALLOC on a page fallocated earlier must initialize
		 * it now, lest undo on failure cancel our earlier guarantee.
		 */
		if (sgp != SGP_WRITE) {
			clear_highpage(page);
			flush_dcache_page(page);
			SetPageUptodate(page);
		}
		if (sgp == SGP_DIRTY)
			set_page_dirty(page);
	}

	/* Perhaps the file has been truncated since we checked */
	if (sgp != SGP_WRITE && sgp != SGP_FALLOC &&
	    ((loff_t)index << PAGE_CACHE_SHIFT) >= i_size_read(inode)) {
		if (alloced) {
			ClearPageDirty(page);
			delete_from_page_cache(page);
			spin_lock(&info->lock);
			shmem_recalc_inode(inode);
			spin_unlock(&info->lock);
		}
		error = -EINVAL;
		goto unlock;
	}
	*pagep = page;
	return 0;

	/*
	 * Error recovery.
	 */
decused:
	if (sbinfo->max_blocks)
		percpu_counter_add(&sbinfo->used_blocks, -1);
unacct:
	shmem_unacct_blocks(info->flags, 1);
failed:
	if (swap.val && !shmem_confirm_swap(mapping, index, swap))
		error = -EEXIST;
unlock:
	if (page) {
		unlock_page(page);
		page_cache_release(page);
	}
	if (error == -ENOSPC && !once++) {
		info = SHMEM_I(inode);
		spin_lock(&info->lock);
		shmem_recalc_inode(inode);
		spin_unlock(&info->lock);
		goto repeat;
	}
	if (error == -EEXIST)	/* from above or from radix_tree_insert */
		goto repeat;
	return error;
}

static int shmem_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct inode *inode = vma->vm_file->f_path.dentry->d_inode;
	int error;
	int ret;

	if (((loff_t)vmf->pgoff << PAGE_CACHE_SHIFT) >= i_size_read(inode))
		return VM_FAULT_SIGBUS;
	struct inode *inode = file_inode(vma->vm_file);
	int error;
	int ret = VM_FAULT_LOCKED;

	/*
	 * Trinity finds that probing a hole which tmpfs is punching can
	 * prevent the hole-punch from ever completing: which in turn
	 * locks writers out with its hold on i_mutex.  So refrain from
	 * faulting pages into the hole while it's being punched.  Although
	 * shmem_undo_range() does remove the additions, it may be unable to
	 * keep up, as each new page needs its own unmap_mapping_range() call,
	 * and the i_mmap tree grows ever slower to scan if new vmas are added.
	 *
	 * It does not matter if we sometimes reach this check just before the
	 * hole-punch begins, so that one fault then races with the punch:
	 * we just need to make racing faults a rare case.
	 *
	 * The implementation below would be much simpler if we just used a
	 * standard mutex or completion: but we cannot take i_mutex in fault,
	 * and bloating every shmem inode for this unlikely case would be sad.
	 */
	if (unlikely(inode->i_private)) {
		struct shmem_falloc *shmem_falloc;

		spin_lock(&inode->i_lock);
		shmem_falloc = inode->i_private;
		if (shmem_falloc &&
		    shmem_falloc->waitq &&
		    vmf->pgoff >= shmem_falloc->start &&
		    vmf->pgoff < shmem_falloc->next) {
			wait_queue_head_t *shmem_falloc_waitq;
			DEFINE_WAIT(shmem_fault_wait);

			ret = VM_FAULT_NOPAGE;
			if ((vmf->flags & FAULT_FLAG_ALLOW_RETRY) &&
			   !(vmf->flags & FAULT_FLAG_RETRY_NOWAIT)) {
				/* It's polite to up mmap_sem if we can */
				up_read(&vma->vm_mm->mmap_sem);
				ret = VM_FAULT_RETRY;
			}

			shmem_falloc_waitq = shmem_falloc->waitq;
			prepare_to_wait(shmem_falloc_waitq, &shmem_fault_wait,
					TASK_UNINTERRUPTIBLE);
			spin_unlock(&inode->i_lock);
			schedule();

			/*
			 * shmem_falloc_waitq points into the shmem_fallocate()
			 * stack of the hole-punching task: shmem_falloc_waitq
			 * is usually invalid by the time we reach here, but
			 * finish_wait() does not dereference it in that case;
			 * though i_lock needed lest racing with wake_up_all().
			 */
			spin_lock(&inode->i_lock);
			finish_wait(shmem_falloc_waitq, &shmem_fault_wait);
			spin_unlock(&inode->i_lock);
			return ret;
		}
		spin_unlock(&inode->i_lock);
	}

	error = shmem_getpage(inode, vmf->pgoff, &vmf->page, SGP_CACHE, &ret);
	if (error)
		return ((error == -ENOMEM) ? VM_FAULT_OOM : VM_FAULT_SIGBUS);

	mark_page_accessed(vmf->page);
	return ret | VM_FAULT_LOCKED;
}

#ifdef CONFIG_NUMA
static int shmem_set_policy(struct vm_area_struct *vma, struct mempolicy *new)
{
	struct inode *i = vma->vm_file->f_path.dentry->d_inode;
	return mpol_set_shared_policy(&SHMEM_I(i)->policy, vma, new);
	if (ret & VM_FAULT_MAJOR) {
		count_vm_event(PGMAJFAULT);
		mem_cgroup_count_vm_event(vma->vm_mm, PGMAJFAULT);
	}
	return ret;
}

#ifdef CONFIG_NUMA
static int shmem_set_policy(struct vm_area_struct *vma, struct mempolicy *mpol)
{
	struct inode *inode = file_inode(vma->vm_file);
	return mpol_set_shared_policy(&SHMEM_I(inode)->policy, vma, mpol);
}

static struct mempolicy *shmem_get_policy(struct vm_area_struct *vma,
					  unsigned long addr)
{
	struct inode *i = vma->vm_file->f_path.dentry->d_inode;
	unsigned long idx;

	idx = ((addr - vma->vm_start) >> PAGE_SHIFT) + vma->vm_pgoff;
	return mpol_shared_policy_lookup(&SHMEM_I(i)->policy, idx);
	struct inode *inode = file_inode(vma->vm_file);
	pgoff_t index;

	index = ((addr - vma->vm_start) >> PAGE_SHIFT) + vma->vm_pgoff;
	return mpol_shared_policy_lookup(&SHMEM_I(inode)->policy, index);
}
#endif

int shmem_lock(struct file *file, int lock, struct user_struct *user)
{
	struct inode *inode = file->f_path.dentry->d_inode;
	struct inode *inode = file_inode(file);
	struct shmem_inode_info *info = SHMEM_I(inode);
	int retval = -ENOMEM;

	spin_lock(&info->lock);
	if (lock && !(info->flags & VM_LOCKED)) {
		if (!user_shm_lock(inode->i_size, user))
			goto out_nomem;
		info->flags |= VM_LOCKED;
		mapping_set_unevictable(file->f_mapping);
	}
	if (!lock && (info->flags & VM_LOCKED) && user) {
		user_shm_unlock(inode->i_size, user);
		info->flags &= ~VM_LOCKED;
	}
	retval = 0;
		mapping_clear_unevictable(file->f_mapping);
	}
	retval = 0;

out_nomem:
	spin_unlock(&info->lock);
	return retval;
}

static int shmem_mmap(struct file *file, struct vm_area_struct *vma)
{
	file_accessed(file);
	vma->vm_ops = &shmem_vm_ops;
	vma->vm_flags |= VM_CAN_NONLINEAR;
	return 0;
}

static struct inode *
shmem_get_inode(struct super_block *sb, int mode, dev_t dev)
	return 0;
}

static struct inode *shmem_get_inode(struct super_block *sb, const struct inode *dir,
				     umode_t mode, dev_t dev, unsigned long flags)
{
	struct inode *inode;
	struct shmem_inode_info *info;
	struct shmem_sb_info *sbinfo = SHMEM_SB(sb);

	if (shmem_reserve_inode(sb))
		return NULL;

	inode = new_inode(sb);
	if (inode) {
		inode->i_mode = mode;
		inode->i_uid = current->fsuid;
		inode->i_gid = current->fsgid;
		inode->i_blocks = 0;
		inode->i_mapping->backing_dev_info = &shmem_backing_dev_info;
		inode->i_ino = get_next_ino();
		inode_init_owner(inode, dir, mode);
		inode->i_blocks = 0;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		inode->i_generation = get_seconds();
		info = SHMEM_I(inode);
		memset(info, 0, (char *)inode - (char *)info);
		spin_lock_init(&info->lock);
		INIT_LIST_HEAD(&info->swaplist);
		info->seals = F_SEAL_SEAL;
		info->flags = flags & VM_NORESERVE;
		INIT_LIST_HEAD(&info->swaplist);
		simple_xattrs_init(&info->xattrs);
		cache_no_acl(inode);

		switch (mode & S_IFMT) {
		default:
			inode->i_op = &shmem_special_inode_operations;
			init_special_inode(inode, mode, dev);
			break;
		case S_IFREG:
			inode->i_mapping->a_ops = &shmem_aops;
			inode->i_op = &shmem_inode_operations;
			inode->i_fop = &shmem_file_operations;
			mpol_shared_policy_init(&info->policy,
						 shmem_get_sbmpol(sbinfo));
			break;
		case S_IFDIR:
			inc_nlink(inode);
			/* Some things misbehave if size == 0 on a directory */
			inode->i_size = 2 * BOGO_DIRENT_SIZE;
			inode->i_op = &shmem_dir_inode_operations;
			inode->i_fop = &simple_dir_operations;
			break;
		case S_IFLNK:
			/*
			 * Must not load anything in the rbtree,
			 * mpol_free_shared_policy will not be called.
			 */
			mpol_shared_policy_init(&info->policy, NULL);
			break;
		}

		lockdep_annotate_inode_mutex_key(inode);
	} else
		shmem_free_inode(sb);
	return inode;
}

#ifdef CONFIG_TMPFS
static const struct inode_operations shmem_symlink_inode_operations;
static const struct inode_operations shmem_symlink_inline_operations;

/*
 * Normally tmpfs avoids the use of shmem_readpage and shmem_write_begin;
 * but providing them allows a tmpfs file to be used for splice, sendfile, and
 * below the loop driver, in the generic fashion that many filesystems support.
 */
static int shmem_readpage(struct file *file, struct page *page)
{
	struct inode *inode = page->mapping->host;
	int error = shmem_getpage(inode, page->index, &page, SGP_CACHE, NULL);
	unlock_page(page);
	return error;
}
bool shmem_mapping(struct address_space *mapping)
{
	if (!mapping->host)
		return false;

	return mapping->host->i_sb->s_op == &shmem_ops;
}

#ifdef CONFIG_TMPFS
static const struct inode_operations shmem_symlink_inode_operations;
static const struct inode_operations shmem_short_symlink_operations;

#ifdef CONFIG_TMPFS_XATTR
static int shmem_initxattrs(struct inode *, const struct xattr *, void *);
#else
#define shmem_initxattrs NULL
#endif

static int
shmem_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned flags,
			struct page **pagep, void **fsdata)
{
	struct inode *inode = mapping->host;
	pgoff_t index = pos >> PAGE_CACHE_SHIFT;
	*pagep = NULL;
	struct shmem_inode_info *info = SHMEM_I(inode);
	pgoff_t index = pos >> PAGE_CACHE_SHIFT;

	/* i_mutex is held by caller */
	if (unlikely(info->seals)) {
		if (info->seals & F_SEAL_WRITE)
			return -EPERM;
		if ((info->seals & F_SEAL_GROW) && pos + len > inode->i_size)
			return -EPERM;
	}

	return shmem_getpage(inode, index, pagep, SGP_WRITE, NULL);
}

static int
shmem_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata)
{
	struct inode *inode = mapping->host;

	if (pos + copied > inode->i_size)
		i_size_write(inode, pos + copied);

	unlock_page(page);
	set_page_dirty(page);
	if (!PageUptodate(page)) {
		if (copied < PAGE_CACHE_SIZE) {
			unsigned from = pos & (PAGE_CACHE_SIZE - 1);
			zero_user_segments(page, 0, from,
					from + copied, PAGE_CACHE_SIZE);
		}
		SetPageUptodate(page);
	}
	set_page_dirty(page);
	unlock_page(page);
	page_cache_release(page);

	return copied;
}

static void do_shmem_file_read(struct file *filp, loff_t *ppos, read_descriptor_t *desc, read_actor_t actor)
{
	struct inode *inode = filp->f_path.dentry->d_inode;
	struct address_space *mapping = inode->i_mapping;
	unsigned long index, offset;
	enum sgp_type sgp = SGP_READ;
static ssize_t shmem_file_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file_inode(file);
	struct address_space *mapping = inode->i_mapping;
	pgoff_t index;
	unsigned long offset;
	enum sgp_type sgp = SGP_READ;
	int error = 0;
	ssize_t retval = 0;
	loff_t *ppos = &iocb->ki_pos;

	/*
	 * Might this read be for a stacking filesystem?  Then when reading
	 * holes of a sparse file, we actually need to allocate those pages,
	 * and even mark them dirty, so it cannot exceed the max_blocks limit.
	 */
	if (segment_eq(get_fs(), KERNEL_DS))
	if (!iter_is_iovec(to))
		sgp = SGP_DIRTY;

	index = *ppos >> PAGE_CACHE_SHIFT;
	offset = *ppos & ~PAGE_CACHE_MASK;

	for (;;) {
		struct page *page = NULL;
		unsigned long end_index, nr, ret;
		pgoff_t end_index;
		unsigned long nr, ret;
		loff_t i_size = i_size_read(inode);

		end_index = i_size >> PAGE_CACHE_SHIFT;
		if (index > end_index)
			break;
		if (index == end_index) {
			nr = i_size & ~PAGE_CACHE_MASK;
			if (nr <= offset)
				break;
		}

		desc->error = shmem_getpage(inode, index, &page, sgp, NULL);
		if (desc->error) {
			if (desc->error == -EINVAL)
				desc->error = 0;
		error = shmem_getpage(inode, index, &page, sgp, NULL);
		if (error) {
			if (error == -EINVAL)
				error = 0;
			break;
		}
		if (page)
			unlock_page(page);

		/*
		 * We must evaluate after, since reads (unlike writes)
		 * are called without i_mutex protection against truncate
		 */
		nr = PAGE_CACHE_SIZE;
		i_size = i_size_read(inode);
		end_index = i_size >> PAGE_CACHE_SHIFT;
		if (index == end_index) {
			nr = i_size & ~PAGE_CACHE_MASK;
			if (nr <= offset) {
				if (page)
					page_cache_release(page);
				break;
			}
		}
		nr -= offset;

		if (page) {
			/*
			 * If users can be writing to this page using arbitrary
			 * virtual addresses, take care about potential aliasing
			 * before reading the page on the kernel side.
			 */
			if (mapping_writably_mapped(mapping))
				flush_dcache_page(page);
			/*
			 * Mark the page accessed if we read the beginning.
			 */
			if (!offset)
				mark_page_accessed(page);
		} else {
			page = ZERO_PAGE(0);
			page_cache_get(page);
		}

		/*
		 * Ok, we have the page, and it's up-to-date, so
		 * now we can copy it to user space...
		 *
		 * The actor routine returns how many bytes were actually used..
		 * NOTE! This may not be the same as how much of a user buffer
		 * we filled up (we may be padding etc), so we can only update
		 * "pos" here (the actor routine has to update the user buffer
		 * pointers and the remaining count).
		 */
		ret = actor(desc, page, offset, nr);
		 */
		ret = copy_page_to_iter(page, offset, nr, to);
		retval += ret;
		offset += ret;
		index += offset >> PAGE_CACHE_SHIFT;
		offset &= ~PAGE_CACHE_MASK;

		page_cache_release(page);
		if (ret != nr || !desc->count)
			break;

		if (!iov_iter_count(to))
			break;
		if (ret < nr) {
			error = -EFAULT;
			break;
		}
		cond_resched();
	}

	*ppos = ((loff_t) index << PAGE_CACHE_SHIFT) + offset;
	file_accessed(filp);
}

static ssize_t shmem_file_aio_read(struct kiocb *iocb,
		const struct iovec *iov, unsigned long nr_segs, loff_t pos)
{
	struct file *filp = iocb->ki_filp;
	ssize_t retval;
	unsigned long seg;
	size_t count;
	loff_t *ppos = &iocb->ki_pos;

	retval = generic_segment_checks(iov, &nr_segs, &count, VERIFY_WRITE);
	if (retval)
		return retval;

	for (seg = 0; seg < nr_segs; seg++) {
		read_descriptor_t desc;

		desc.written = 0;
		desc.arg.buf = iov[seg].iov_base;
		desc.count = iov[seg].iov_len;
		if (desc.count == 0)
			continue;
		desc.error = 0;
		do_shmem_file_read(filp, ppos, &desc, file_read_actor);
		retval += desc.written;
		if (desc.error) {
			retval = retval ?: desc.error;
			break;
		}
		if (desc.count > 0)
			break;
	}
	return retval;
	file_accessed(file);
	return retval ? retval : error;
}

static ssize_t shmem_file_splice_read(struct file *in, loff_t *ppos,
				struct pipe_inode_info *pipe, size_t len,
				unsigned int flags)
{
	struct address_space *mapping = in->f_mapping;
	struct inode *inode = mapping->host;
	unsigned int loff, nr_pages, req_pages;
	struct page *pages[PIPE_DEF_BUFFERS];
	struct partial_page partial[PIPE_DEF_BUFFERS];
	struct page *page;
	pgoff_t index, end_index;
	loff_t isize, left;
	int error, page_nr;
	struct splice_pipe_desc spd = {
		.pages = pages,
		.partial = partial,
		.nr_pages_max = PIPE_DEF_BUFFERS,
		.flags = flags,
		.ops = &page_cache_pipe_buf_ops,
		.spd_release = spd_release_page,
	};

	isize = i_size_read(inode);
	if (unlikely(*ppos >= isize))
		return 0;

	left = isize - *ppos;
	if (unlikely(left < len))
		len = left;

	if (splice_grow_spd(pipe, &spd))
		return -ENOMEM;

	index = *ppos >> PAGE_CACHE_SHIFT;
	loff = *ppos & ~PAGE_CACHE_MASK;
	req_pages = (len + loff + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
	nr_pages = min(req_pages, spd.nr_pages_max);

	spd.nr_pages = find_get_pages_contig(mapping, index,
						nr_pages, spd.pages);
	index += spd.nr_pages;
	error = 0;

	while (spd.nr_pages < nr_pages) {
		error = shmem_getpage(inode, index, &page, SGP_CACHE, NULL);
		if (error)
			break;
		unlock_page(page);
		spd.pages[spd.nr_pages++] = page;
		index++;
	}

	index = *ppos >> PAGE_CACHE_SHIFT;
	nr_pages = spd.nr_pages;
	spd.nr_pages = 0;

	for (page_nr = 0; page_nr < nr_pages; page_nr++) {
		unsigned int this_len;

		if (!len)
			break;

		this_len = min_t(unsigned long, len, PAGE_CACHE_SIZE - loff);
		page = spd.pages[page_nr];

		if (!PageUptodate(page) || page->mapping != mapping) {
			error = shmem_getpage(inode, index, &page,
							SGP_CACHE, NULL);
			if (error)
				break;
			unlock_page(page);
			page_cache_release(spd.pages[page_nr]);
			spd.pages[page_nr] = page;
		}

		isize = i_size_read(inode);
		end_index = (isize - 1) >> PAGE_CACHE_SHIFT;
		if (unlikely(!isize || index > end_index))
			break;

		if (end_index == index) {
			unsigned int plen;

			plen = ((isize - 1) & ~PAGE_CACHE_MASK) + 1;
			if (plen <= loff)
				break;

			this_len = min(this_len, plen - loff);
			len = this_len;
		}

		spd.partial[page_nr].offset = loff;
		spd.partial[page_nr].len = this_len;
		len -= this_len;
		loff = 0;
		spd.nr_pages++;
		index++;
	}

	while (page_nr < nr_pages)
		page_cache_release(spd.pages[page_nr++]);

	if (spd.nr_pages)
		error = splice_to_pipe(pipe, &spd);

	splice_shrink_spd(&spd);

	if (error > 0) {
		*ppos += error;
		file_accessed(in);
	}
	return error;
}

/*
 * llseek SEEK_DATA or SEEK_HOLE through the radix_tree.
 */
static pgoff_t shmem_seek_hole_data(struct address_space *mapping,
				    pgoff_t index, pgoff_t end, int whence)
{
	struct page *page;
	struct pagevec pvec;
	pgoff_t indices[PAGEVEC_SIZE];
	bool done = false;
	int i;

	pagevec_init(&pvec, 0);
	pvec.nr = 1;		/* start small: we may be there already */
	while (!done) {
		pvec.nr = find_get_entries(mapping, index,
					pvec.nr, pvec.pages, indices);
		if (!pvec.nr) {
			if (whence == SEEK_DATA)
				index = end;
			break;
		}
		for (i = 0; i < pvec.nr; i++, index++) {
			if (index < indices[i]) {
				if (whence == SEEK_HOLE) {
					done = true;
					break;
				}
				index = indices[i];
			}
			page = pvec.pages[i];
			if (page && !radix_tree_exceptional_entry(page)) {
				if (!PageUptodate(page))
					page = NULL;
			}
			if (index >= end ||
			    (page && whence == SEEK_DATA) ||
			    (!page && whence == SEEK_HOLE)) {
				done = true;
				break;
			}
		}
		pagevec_remove_exceptionals(&pvec);
		pagevec_release(&pvec);
		pvec.nr = PAGEVEC_SIZE;
		cond_resched();
	}
	return index;
}

static loff_t shmem_file_llseek(struct file *file, loff_t offset, int whence)
{
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	pgoff_t start, end;
	loff_t new_offset;

	if (whence != SEEK_DATA && whence != SEEK_HOLE)
		return generic_file_llseek_size(file, offset, whence,
					MAX_LFS_FILESIZE, i_size_read(inode));
	mutex_lock(&inode->i_mutex);
	/* We're holding i_mutex so we can access i_size directly */

	if (offset < 0 || offset >= inode->i_size)
		offset = -ENXIO;
	else {
		start = offset >> PAGE_CACHE_SHIFT;
		end = (inode->i_size + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
		new_offset = shmem_seek_hole_data(mapping, start, end, whence);
		new_offset <<= PAGE_CACHE_SHIFT;
		if (new_offset > offset) {
			if (new_offset < inode->i_size)
				offset = new_offset;
			else if (whence == SEEK_DATA)
				offset = -ENXIO;
			else
				offset = inode->i_size;
		}
	}

	if (offset >= 0)
		offset = vfs_setpos(file, offset, MAX_LFS_FILESIZE);
	mutex_unlock(&inode->i_mutex);
	return offset;
}

/*
 * We need a tag: a new tag would expand every radix_tree_node by 8 bytes,
 * so reuse a tag which we firmly believe is never set or cleared on shmem.
 */
#define SHMEM_TAG_PINNED        PAGECACHE_TAG_TOWRITE
#define LAST_SCAN               4       /* about 150ms max */

static void shmem_tag_pins(struct address_space *mapping)
{
	struct radix_tree_iter iter;
	void **slot;
	pgoff_t start;
	struct page *page;
	unsigned int tagged = 0;

	lru_add_drain();
	start = 0;

	spin_lock_irq(&mapping->tree_lock);
restart:
	radix_tree_for_each_slot(slot, &mapping->page_tree, &iter, start) {
		page = radix_tree_deref_slot_protected(slot, &mapping->tree_lock);
		if (!page || radix_tree_exception(page)) {
			if (radix_tree_deref_retry(page))
				goto restart;
		} else if (page_count(page) - page_mapcount(page) > 1) {
			radix_tree_tag_set(&mapping->page_tree, iter.index,
					   SHMEM_TAG_PINNED);
		}

		if (++tagged % 1024)
			continue;

		spin_unlock_irq(&mapping->tree_lock);
		cond_resched();
		start = iter.index + 1;
		spin_lock_irq(&mapping->tree_lock);
		goto restart;
	}
	spin_unlock_irq(&mapping->tree_lock);
}

/*
 * Setting SEAL_WRITE requires us to verify there's no pending writer. However,
 * via get_user_pages(), drivers might have some pending I/O without any active
 * user-space mappings (eg., direct-IO, AIO). Therefore, we look at all pages
 * and see whether it has an elevated ref-count. If so, we tag them and wait for
 * them to be dropped.
 * The caller must guarantee that no new user will acquire writable references
 * to those pages to avoid races.
 */
static int shmem_wait_for_pins(struct address_space *mapping)
{
	struct radix_tree_iter iter;
	void **slot;
	pgoff_t start;
	struct page *page;
	int error, scan;

	shmem_tag_pins(mapping);

	error = 0;
	for (scan = 0; scan <= LAST_SCAN; scan++) {
		if (!radix_tree_tagged(&mapping->page_tree, SHMEM_TAG_PINNED))
			break;

		if (!scan)
			lru_add_drain_all();
		else if (schedule_timeout_killable((HZ << scan) / 200))
			scan = LAST_SCAN;

		start = 0;
		rcu_read_lock();
restart:
		radix_tree_for_each_tagged(slot, &mapping->page_tree, &iter,
					   start, SHMEM_TAG_PINNED) {

			page = radix_tree_deref_slot(slot);
			if (radix_tree_exception(page)) {
				if (radix_tree_deref_retry(page))
					goto restart;

				page = NULL;
			}

			if (page &&
			    page_count(page) - page_mapcount(page) != 1) {
				if (scan < LAST_SCAN)
					goto continue_resched;

				/*
				 * On the last scan, we clean up all those tags
				 * we inserted; but make a note that we still
				 * found pages pinned.
				 */
				error = -EBUSY;
			}

			spin_lock_irq(&mapping->tree_lock);
			radix_tree_tag_clear(&mapping->page_tree,
					     iter.index, SHMEM_TAG_PINNED);
			spin_unlock_irq(&mapping->tree_lock);
continue_resched:
			if (need_resched()) {
				cond_resched_rcu();
				start = iter.index + 1;
				goto restart;
			}
		}
		rcu_read_unlock();
	}

	return error;
}

#define F_ALL_SEALS (F_SEAL_SEAL | \
		     F_SEAL_SHRINK | \
		     F_SEAL_GROW | \
		     F_SEAL_WRITE)

int shmem_add_seals(struct file *file, unsigned int seals)
{
	struct inode *inode = file_inode(file);
	struct shmem_inode_info *info = SHMEM_I(inode);
	int error;

	/*
	 * SEALING
	 * Sealing allows multiple parties to share a shmem-file but restrict
	 * access to a specific subset of file operations. Seals can only be
	 * added, but never removed. This way, mutually untrusted parties can
	 * share common memory regions with a well-defined policy. A malicious
	 * peer can thus never perform unwanted operations on a shared object.
	 *
	 * Seals are only supported on special shmem-files and always affect
	 * the whole underlying inode. Once a seal is set, it may prevent some
	 * kinds of access to the file. Currently, the following seals are
	 * defined:
	 *   SEAL_SEAL: Prevent further seals from being set on this file
	 *   SEAL_SHRINK: Prevent the file from shrinking
	 *   SEAL_GROW: Prevent the file from growing
	 *   SEAL_WRITE: Prevent write access to the file
	 *
	 * As we don't require any trust relationship between two parties, we
	 * must prevent seals from being removed. Therefore, sealing a file
	 * only adds a given set of seals to the file, it never touches
	 * existing seals. Furthermore, the "setting seals"-operation can be
	 * sealed itself, which basically prevents any further seal from being
	 * added.
	 *
	 * Semantics of sealing are only defined on volatile files. Only
	 * anonymous shmem files support sealing. More importantly, seals are
	 * never written to disk. Therefore, there's no plan to support it on
	 * other file types.
	 */

	if (file->f_op != &shmem_file_operations)
		return -EINVAL;
	if (!(file->f_mode & FMODE_WRITE))
		return -EPERM;
	if (seals & ~(unsigned int)F_ALL_SEALS)
		return -EINVAL;

	mutex_lock(&inode->i_mutex);

	if (info->seals & F_SEAL_SEAL) {
		error = -EPERM;
		goto unlock;
	}

	if ((seals & F_SEAL_WRITE) && !(info->seals & F_SEAL_WRITE)) {
		error = mapping_deny_writable(file->f_mapping);
		if (error)
			goto unlock;

		error = shmem_wait_for_pins(file->f_mapping);
		if (error) {
			mapping_allow_writable(file->f_mapping);
			goto unlock;
		}
	}

	info->seals |= seals;
	error = 0;

unlock:
	mutex_unlock(&inode->i_mutex);
	return error;
}
EXPORT_SYMBOL_GPL(shmem_add_seals);

int shmem_get_seals(struct file *file)
{
	if (file->f_op != &shmem_file_operations)
		return -EINVAL;

	return SHMEM_I(file_inode(file))->seals;
}
EXPORT_SYMBOL_GPL(shmem_get_seals);

long shmem_fcntl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long error;

	switch (cmd) {
	case F_ADD_SEALS:
		/* disallow upper 32bit */
		if (arg > UINT_MAX)
			return -EINVAL;

		error = shmem_add_seals(file, arg);
		break;
	case F_GET_SEALS:
		error = shmem_get_seals(file);
		break;
	default:
		error = -EINVAL;
		break;
	}

	return error;
}

static long shmem_fallocate(struct file *file, int mode, loff_t offset,
							 loff_t len)
{
	struct inode *inode = file_inode(file);
	struct shmem_sb_info *sbinfo = SHMEM_SB(inode->i_sb);
	struct shmem_inode_info *info = SHMEM_I(inode);
	struct shmem_falloc shmem_falloc;
	pgoff_t start, index, end;
	int error;

	if (mode & ~(FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE))
		return -EOPNOTSUPP;

	mutex_lock(&inode->i_mutex);

	if (mode & FALLOC_FL_PUNCH_HOLE) {
		struct address_space *mapping = file->f_mapping;
		loff_t unmap_start = round_up(offset, PAGE_SIZE);
		loff_t unmap_end = round_down(offset + len, PAGE_SIZE) - 1;
		DECLARE_WAIT_QUEUE_HEAD_ONSTACK(shmem_falloc_waitq);

		/* protected by i_mutex */
		if (info->seals & F_SEAL_WRITE) {
			error = -EPERM;
			goto out;
		}

		shmem_falloc.waitq = &shmem_falloc_waitq;
		shmem_falloc.start = (u64)unmap_start >> PAGE_SHIFT;
		shmem_falloc.next = (unmap_end + 1) >> PAGE_SHIFT;
		spin_lock(&inode->i_lock);
		inode->i_private = &shmem_falloc;
		spin_unlock(&inode->i_lock);

		if ((u64)unmap_end > (u64)unmap_start)
			unmap_mapping_range(mapping, unmap_start,
					    1 + unmap_end - unmap_start, 0);
		shmem_truncate_range(inode, offset, offset + len - 1);
		/* No need to unmap again: hole-punching leaves COWed pages */

		spin_lock(&inode->i_lock);
		inode->i_private = NULL;
		wake_up_all(&shmem_falloc_waitq);
		spin_unlock(&inode->i_lock);
		error = 0;
		goto out;
	}

	/* We need to check rlimit even when FALLOC_FL_KEEP_SIZE */
	error = inode_newsize_ok(inode, offset + len);
	if (error)
		goto out;

	if ((info->seals & F_SEAL_GROW) && offset + len > inode->i_size) {
		error = -EPERM;
		goto out;
	}

	start = offset >> PAGE_CACHE_SHIFT;
	end = (offset + len + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
	/* Try to avoid a swapstorm if len is impossible to satisfy */
	if (sbinfo->max_blocks && end - start > sbinfo->max_blocks) {
		error = -ENOSPC;
		goto out;
	}

	shmem_falloc.waitq = NULL;
	shmem_falloc.start = start;
	shmem_falloc.next  = start;
	shmem_falloc.nr_falloced = 0;
	shmem_falloc.nr_unswapped = 0;
	spin_lock(&inode->i_lock);
	inode->i_private = &shmem_falloc;
	spin_unlock(&inode->i_lock);

	for (index = start; index < end; index++) {
		struct page *page;

		/*
		 * Good, the fallocate(2) manpage permits EINTR: we may have
		 * been interrupted because we are using up too much memory.
		 */
		if (signal_pending(current))
			error = -EINTR;
		else if (shmem_falloc.nr_unswapped > shmem_falloc.nr_falloced)
			error = -ENOMEM;
		else
			error = shmem_getpage(inode, index, &page, SGP_FALLOC,
									NULL);
		if (error) {
			/* Remove the !PageUptodate pages we added */
			if (index > start) {
				shmem_undo_range(inode,
				 (loff_t)start << PAGE_CACHE_SHIFT,
				 ((loff_t)index << PAGE_CACHE_SHIFT) - 1, true);
			}
			goto undone;
		}

		/*
		 * Inform shmem_writepage() how far we have reached.
		 * No need for lock or barrier: we have the page lock.
		 */
		shmem_falloc.next++;
		if (!PageUptodate(page))
			shmem_falloc.nr_falloced++;

		/*
		 * If !PageUptodate, leave it that way so that freeable pages
		 * can be recognized if we need to rollback on error later.
		 * But set_page_dirty so that memory pressure will swap rather
		 * than free the pages we are allocating (and SGP_CACHE pages
		 * might still be clean: we now need to mark those dirty too).
		 */
		set_page_dirty(page);
		unlock_page(page);
		page_cache_release(page);
		cond_resched();
	}

	if (!(mode & FALLOC_FL_KEEP_SIZE) && offset + len > inode->i_size)
		i_size_write(inode, offset + len);
	inode->i_ctime = CURRENT_TIME;
undone:
	spin_lock(&inode->i_lock);
	inode->i_private = NULL;
	spin_unlock(&inode->i_lock);
out:
	mutex_unlock(&inode->i_mutex);
	return error;
}

static int shmem_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct shmem_sb_info *sbinfo = SHMEM_SB(dentry->d_sb);

	buf->f_type = TMPFS_MAGIC;
	buf->f_bsize = PAGE_CACHE_SIZE;
	buf->f_namelen = NAME_MAX;
	spin_lock(&sbinfo->stat_lock);
	if (sbinfo->max_blocks) {
		buf->f_blocks = sbinfo->max_blocks;
		buf->f_bavail = buf->f_bfree = sbinfo->free_blocks;
	if (sbinfo->max_blocks) {
		buf->f_blocks = sbinfo->max_blocks;
		buf->f_bavail =
		buf->f_bfree  = sbinfo->max_blocks -
				percpu_counter_sum(&sbinfo->used_blocks);
	}
	if (sbinfo->max_inodes) {
		buf->f_files = sbinfo->max_inodes;
		buf->f_ffree = sbinfo->free_inodes;
	}
	/* else leave those fields 0 like simple_statfs */
	spin_unlock(&sbinfo->stat_lock);
	return 0;
}

/*
 * File creation. Allocate an inode, and we're done..
 */
static int
shmem_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t dev)
{
	struct inode *inode = shmem_get_inode(dir->i_sb, mode, dev);
	int error = -ENOSPC;

	if (inode) {
		error = security_inode_init_security(inode, dir, NULL, NULL,
						     NULL);
		if (error) {
			if (error != -EOPNOTSUPP) {
				iput(inode);
				return error;
			}
		}
		error = shmem_acl_init(inode, dir);
		if (error) {
			iput(inode);
			return error;
		}
		if (dir->i_mode & S_ISGID) {
			inode->i_gid = dir->i_gid;
			if (S_ISDIR(mode))
				inode->i_mode |= S_ISGID;
		}
shmem_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev)
{
	struct inode *inode;
	int error = -ENOSPC;

	inode = shmem_get_inode(dir->i_sb, dir, mode, dev, VM_NORESERVE);
	if (inode) {
		error = simple_acl_create(dir, inode);
		if (error)
			goto out_iput;
		error = security_inode_init_security(inode, dir,
						     &dentry->d_name,
						     shmem_initxattrs, NULL);
		if (error && error != -EOPNOTSUPP)
			goto out_iput;

		error = 0;
		dir->i_size += BOGO_DIRENT_SIZE;
		dir->i_ctime = dir->i_mtime = CURRENT_TIME;
		d_instantiate(dentry, inode);
		dget(dentry); /* Extra count - pin the dentry in core */
	}
	return error;
}

static int shmem_mkdir(struct inode *dir, struct dentry *dentry, int mode)
out_iput:
	iput(inode);
	return error;
}

static int
shmem_tmpfile(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	struct inode *inode;
	int error = -ENOSPC;

	inode = shmem_get_inode(dir->i_sb, dir, mode, 0, VM_NORESERVE);
	if (inode) {
		error = security_inode_init_security(inode, dir,
						     NULL,
						     shmem_initxattrs, NULL);
		if (error && error != -EOPNOTSUPP)
			goto out_iput;
		error = simple_acl_create(dir, inode);
		if (error)
			goto out_iput;
		d_tmpfile(dentry, inode);
	}
	return error;
out_iput:
	iput(inode);
	return error;
}

static int shmem_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	int error;

	if ((error = shmem_mknod(dir, dentry, mode | S_IFDIR, 0)))
		return error;
	inc_nlink(dir);
	return 0;
}

static int shmem_create(struct inode *dir, struct dentry *dentry, int mode,
		struct nameidata *nd)
static int shmem_create(struct inode *dir, struct dentry *dentry, umode_t mode,
		bool excl)
{
	return shmem_mknod(dir, dentry, mode | S_IFREG, 0);
}

/*
 * Link a file..
 */
static int shmem_link(struct dentry *old_dentry, struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = old_dentry->d_inode;
	struct inode *inode = d_inode(old_dentry);
	int ret = 0;

	/*
	 * No ordinary (disk based) filesystem counts links as inodes;
	 * but each new link needs a new dentry, pinning lowmem, and
	 * tmpfs dentries cannot be pruned until they are unlinked.
	 * But if an O_TMPFILE file is linked into the tmpfs, the
	 * first link must skip that, to get the accounting right.
	 */
	if (inode->i_nlink) {
		ret = shmem_reserve_inode(inode->i_sb);
		if (ret)
			goto out;
	}

	dir->i_size += BOGO_DIRENT_SIZE;
	inode->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	inc_nlink(inode);
	atomic_inc(&inode->i_count);	/* New dentry reference */
	ihold(inode);	/* New dentry reference */
	dget(dentry);		/* Extra pinning count for the created dentry */
	d_instantiate(dentry, inode);
out:
	return ret;
}

static int shmem_unlink(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	struct inode *inode = d_inode(dentry);

	if (inode->i_nlink > 1 && !S_ISDIR(inode->i_mode))
		shmem_free_inode(inode->i_sb);

	dir->i_size -= BOGO_DIRENT_SIZE;
	inode->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	drop_nlink(inode);
	dput(dentry);	/* Undo the count from "create" - this does all the work */
	return 0;
}

static int shmem_rmdir(struct inode *dir, struct dentry *dentry)
{
	if (!simple_empty(dentry))
		return -ENOTEMPTY;

	drop_nlink(dentry->d_inode);
	drop_nlink(d_inode(dentry));
	drop_nlink(dir);
	return shmem_unlink(dir, dentry);
}

static int shmem_exchange(struct inode *old_dir, struct dentry *old_dentry, struct inode *new_dir, struct dentry *new_dentry)
{
	bool old_is_dir = d_is_dir(old_dentry);
	bool new_is_dir = d_is_dir(new_dentry);

	if (old_dir != new_dir && old_is_dir != new_is_dir) {
		if (old_is_dir) {
			drop_nlink(old_dir);
			inc_nlink(new_dir);
		} else {
			drop_nlink(new_dir);
			inc_nlink(old_dir);
		}
	}
	old_dir->i_ctime = old_dir->i_mtime =
	new_dir->i_ctime = new_dir->i_mtime =
	d_inode(old_dentry)->i_ctime =
	d_inode(new_dentry)->i_ctime = CURRENT_TIME;

	return 0;
}

static int shmem_whiteout(struct inode *old_dir, struct dentry *old_dentry)
{
	struct dentry *whiteout;
	int error;

	whiteout = d_alloc(old_dentry->d_parent, &old_dentry->d_name);
	if (!whiteout)
		return -ENOMEM;

	error = shmem_mknod(old_dir, whiteout,
			    S_IFCHR | WHITEOUT_MODE, WHITEOUT_DEV);
	dput(whiteout);
	if (error)
		return error;

	/*
	 * Cheat and hash the whiteout while the old dentry is still in
	 * place, instead of playing games with FS_RENAME_DOES_D_MOVE.
	 *
	 * d_lookup() will consistently find one of them at this point,
	 * not sure which one, but that isn't even important.
	 */
	d_rehash(whiteout);
	return 0;
}

/*
 * The VFS layer already does all the dentry stuff for rename,
 * we just have to decrement the usage count for the target if
 * it exists so that the VFS layer correctly free's it when it
 * gets overwritten.
 */
static int shmem_rename(struct inode *old_dir, struct dentry *old_dentry, struct inode *new_dir, struct dentry *new_dentry)
{
	struct inode *inode = old_dentry->d_inode;
	int they_are_dirs = S_ISDIR(inode->i_mode);

	if (!simple_empty(new_dentry))
		return -ENOTEMPTY;

	if (new_dentry->d_inode) {
		(void) shmem_unlink(new_dir, new_dentry);
		if (they_are_dirs)
			drop_nlink(old_dir);
static int shmem_rename2(struct inode *old_dir, struct dentry *old_dentry, struct inode *new_dir, struct dentry *new_dentry, unsigned int flags)
{
	struct inode *inode = d_inode(old_dentry);
	int they_are_dirs = S_ISDIR(inode->i_mode);

	if (flags & ~(RENAME_NOREPLACE | RENAME_EXCHANGE | RENAME_WHITEOUT))
		return -EINVAL;

	if (flags & RENAME_EXCHANGE)
		return shmem_exchange(old_dir, old_dentry, new_dir, new_dentry);

	if (!simple_empty(new_dentry))
		return -ENOTEMPTY;

	if (flags & RENAME_WHITEOUT) {
		int error;

		error = shmem_whiteout(old_dir, old_dentry);
		if (error)
			return error;
	}

	if (d_really_is_positive(new_dentry)) {
		(void) shmem_unlink(new_dir, new_dentry);
		if (they_are_dirs) {
			drop_nlink(d_inode(new_dentry));
			drop_nlink(old_dir);
		}
	} else if (they_are_dirs) {
		drop_nlink(old_dir);
		inc_nlink(new_dir);
	}

	old_dir->i_size -= BOGO_DIRENT_SIZE;
	new_dir->i_size += BOGO_DIRENT_SIZE;
	old_dir->i_ctime = old_dir->i_mtime =
	new_dir->i_ctime = new_dir->i_mtime =
	inode->i_ctime = CURRENT_TIME;
	return 0;
}

static int shmem_symlink(struct inode *dir, struct dentry *dentry, const char *symname)
{
	int error;
	int len;
	struct inode *inode;
	struct page *page = NULL;
	struct page *page;
	char *kaddr;
	struct shmem_inode_info *info;

	len = strlen(symname) + 1;
	if (len > PAGE_CACHE_SIZE)
		return -ENAMETOOLONG;

	inode = shmem_get_inode(dir->i_sb, S_IFLNK|S_IRWXUGO, 0);
	if (!inode)
		return -ENOSPC;

	error = security_inode_init_security(inode, dir, NULL, NULL,
					     NULL);
	inode = shmem_get_inode(dir->i_sb, dir, S_IFLNK|S_IRWXUGO, 0, VM_NORESERVE);
	if (!inode)
		return -ENOSPC;

	error = security_inode_init_security(inode, dir, &dentry->d_name,
					     shmem_initxattrs, NULL);
	if (error) {
		if (error != -EOPNOTSUPP) {
			iput(inode);
			return error;
		}
		error = 0;
	}

	info = SHMEM_I(inode);
	inode->i_size = len-1;
	if (len <= (char *)inode - (char *)info) {
		/* do it inline */
		memcpy(info, symname, len);
		inode->i_op = &shmem_symlink_inline_operations;
	if (len <= SHORT_SYMLINK_LEN) {
		inode->i_link = kmemdup(symname, len, GFP_KERNEL);
		if (!inode->i_link) {
			iput(inode);
			return -ENOMEM;
		}
		inode->i_op = &shmem_short_symlink_operations;
	} else {
		error = shmem_getpage(inode, 0, &page, SGP_WRITE, NULL);
		if (error) {
			iput(inode);
			return error;
		}
		unlock_page(page);
		inode->i_mapping->a_ops = &shmem_aops;
		inode->i_op = &shmem_symlink_inode_operations;
		kaddr = kmap_atomic(page, KM_USER0);
		memcpy(kaddr, symname, len);
		kunmap_atomic(kaddr, KM_USER0);
		set_page_dirty(page);
		page_cache_release(page);
	}
	if (dir->i_mode & S_ISGID)
		inode->i_gid = dir->i_gid;
		inode->i_mapping->a_ops = &shmem_aops;
		inode->i_op = &shmem_symlink_inode_operations;
		kaddr = kmap_atomic(page);
		memcpy(kaddr, symname, len);
		kunmap_atomic(kaddr);
		SetPageUptodate(page);
		set_page_dirty(page);
		unlock_page(page);
		page_cache_release(page);
	}
	dir->i_size += BOGO_DIRENT_SIZE;
	dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	d_instantiate(dentry, inode);
	dget(dentry);
	return 0;
}

static void *shmem_follow_link_inline(struct dentry *dentry, struct nameidata *nd)
{
	nd_set_link(nd, (char *)SHMEM_I(dentry->d_inode));
	return NULL;
}

static void *shmem_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	struct page *page = NULL;
	int res = shmem_getpage(dentry->d_inode, 0, &page, SGP_READ, NULL);
	nd_set_link(nd, res ? ERR_PTR(res) : kmap(page));
	if (page)
		unlock_page(page);
	return page;
}

static void shmem_put_link(struct dentry *dentry, struct nameidata *nd, void *cookie)
{
	if (!IS_ERR(nd_get_link(nd))) {
		struct page *page = cookie;
		kunmap(page);
		mark_page_accessed(page);
		page_cache_release(page);
	}
}

static const struct inode_operations shmem_symlink_inline_operations = {
	.readlink	= generic_readlink,
	.follow_link	= shmem_follow_link_inline,
};

static const struct inode_operations shmem_symlink_inode_operations = {
	.truncate	= shmem_truncate,
	.readlink	= generic_readlink,
	.follow_link	= shmem_follow_link,
	.put_link	= shmem_put_link,
};

#ifdef CONFIG_TMPFS_POSIX_ACL
/*
 * Superblocks without xattr inode operations will get security.* xattr
 * support from the VFS "for free". As soon as we have any other xattrs
static const char *shmem_follow_link(struct dentry *dentry, void **cookie)
{
	struct page *page = NULL;
	int error = shmem_getpage(d_inode(dentry), 0, &page, SGP_READ, NULL);
	if (error)
		return ERR_PTR(error);
	unlock_page(page);
	*cookie = page;
	return kmap(page);
}

static void shmem_put_link(struct inode *unused, void *cookie)
{
	struct page *page = cookie;
	kunmap(page);
	mark_page_accessed(page);
	page_cache_release(page);
}

#ifdef CONFIG_TMPFS_XATTR
/*
 * Superblocks without xattr inode operations may get some security.* xattr
 * support from the LSM "for free". As soon as we have any other xattrs
 * like ACLs, we also need to implement the security.* handlers at
 * filesystem level, though.
 */

static size_t shmem_xattr_security_list(struct inode *inode, char *list,
					size_t list_len, const char *name,
					size_t name_len)
{
	return security_inode_listsecurity(inode, list, list_len);
}

static int shmem_xattr_security_get(struct inode *inode, const char *name,
				    void *buffer, size_t size)
{
	if (strcmp(name, "") == 0)
		return -EINVAL;
	return xattr_getsecurity(inode, name, buffer, size);
}

static int shmem_xattr_security_set(struct inode *inode, const char *name,
				    const void *value, size_t size, int flags)
{
	if (strcmp(name, "") == 0)
		return -EINVAL;
	return security_inode_setsecurity(inode, name, value, size, flags);
}

static struct xattr_handler shmem_xattr_security_handler = {
	.prefix = XATTR_SECURITY_PREFIX,
	.list   = shmem_xattr_security_list,
	.get    = shmem_xattr_security_get,
	.set    = shmem_xattr_security_set,
};

static struct xattr_handler *shmem_xattr_handlers[] = {
	&shmem_xattr_acl_access_handler,
	&shmem_xattr_acl_default_handler,
	&shmem_xattr_security_handler,
	NULL
};
#endif
/*
 * Callback for security_inode_init_security() for acquiring xattrs.
 */
static int shmem_initxattrs(struct inode *inode,
			    const struct xattr *xattr_array,
			    void *fs_info)
{
	struct shmem_inode_info *info = SHMEM_I(inode);
	const struct xattr *xattr;
	struct simple_xattr *new_xattr;
	size_t len;

	for (xattr = xattr_array; xattr->name != NULL; xattr++) {
		new_xattr = simple_xattr_alloc(xattr->value, xattr->value_len);
		if (!new_xattr)
			return -ENOMEM;

		len = strlen(xattr->name) + 1;
		new_xattr->name = kmalloc(XATTR_SECURITY_PREFIX_LEN + len,
					  GFP_KERNEL);
		if (!new_xattr->name) {
			kfree(new_xattr);
			return -ENOMEM;
		}

		memcpy(new_xattr->name, XATTR_SECURITY_PREFIX,
		       XATTR_SECURITY_PREFIX_LEN);
		memcpy(new_xattr->name + XATTR_SECURITY_PREFIX_LEN,
		       xattr->name, len);

		simple_xattr_list_add(&info->xattrs, new_xattr);
	}

	return 0;
}

static const struct xattr_handler *shmem_xattr_handlers[] = {
#ifdef CONFIG_TMPFS_POSIX_ACL
	&posix_acl_access_xattr_handler,
	&posix_acl_default_xattr_handler,
#endif
	NULL
};

static int shmem_xattr_validate(const char *name)
{
	struct { const char *prefix; size_t len; } arr[] = {
		{ XATTR_SECURITY_PREFIX, XATTR_SECURITY_PREFIX_LEN },
		{ XATTR_TRUSTED_PREFIX, XATTR_TRUSTED_PREFIX_LEN }
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(arr); i++) {
		size_t preflen = arr[i].len;
		if (strncmp(name, arr[i].prefix, preflen) == 0) {
			if (!name[preflen])
				return -EINVAL;
			return 0;
		}
	}
	return -EOPNOTSUPP;
}

static ssize_t shmem_getxattr(struct dentry *dentry, const char *name,
			      void *buffer, size_t size)
{
	struct shmem_inode_info *info = SHMEM_I(d_inode(dentry));
	int err;

	/*
	 * If this is a request for a synthetic attribute in the system.*
	 * namespace use the generic infrastructure to resolve a handler
	 * for it via sb->s_xattr.
	 */
	if (!strncmp(name, XATTR_SYSTEM_PREFIX, XATTR_SYSTEM_PREFIX_LEN))
		return generic_getxattr(dentry, name, buffer, size);

	err = shmem_xattr_validate(name);
	if (err)
		return err;

	return simple_xattr_get(&info->xattrs, name, buffer, size);
}

static int shmem_setxattr(struct dentry *dentry, const char *name,
			  const void *value, size_t size, int flags)
{
	struct shmem_inode_info *info = SHMEM_I(d_inode(dentry));
	int err;

	/*
	 * If this is a request for a synthetic attribute in the system.*
	 * namespace use the generic infrastructure to resolve a handler
	 * for it via sb->s_xattr.
	 */
	if (!strncmp(name, XATTR_SYSTEM_PREFIX, XATTR_SYSTEM_PREFIX_LEN))
		return generic_setxattr(dentry, name, value, size, flags);

	err = shmem_xattr_validate(name);
	if (err)
		return err;

	return simple_xattr_set(&info->xattrs, name, value, size, flags);
}

static int shmem_removexattr(struct dentry *dentry, const char *name)
{
	struct shmem_inode_info *info = SHMEM_I(d_inode(dentry));
	int err;

	/*
	 * If this is a request for a synthetic attribute in the system.*
	 * namespace use the generic infrastructure to resolve a handler
	 * for it via sb->s_xattr.
	 */
	if (!strncmp(name, XATTR_SYSTEM_PREFIX, XATTR_SYSTEM_PREFIX_LEN))
		return generic_removexattr(dentry, name);

	err = shmem_xattr_validate(name);
	if (err)
		return err;

	return simple_xattr_remove(&info->xattrs, name);
}

static ssize_t shmem_listxattr(struct dentry *dentry, char *buffer, size_t size)
{
	struct shmem_inode_info *info = SHMEM_I(d_inode(dentry));
	return simple_xattr_list(&info->xattrs, buffer, size);
}
#endif /* CONFIG_TMPFS_XATTR */

static const struct inode_operations shmem_short_symlink_operations = {
	.readlink	= generic_readlink,
	.follow_link	= simple_follow_link,
#ifdef CONFIG_TMPFS_XATTR
	.setxattr	= shmem_setxattr,
	.getxattr	= shmem_getxattr,
	.listxattr	= shmem_listxattr,
	.removexattr	= shmem_removexattr,
#endif
};

static const struct inode_operations shmem_symlink_inode_operations = {
	.readlink	= generic_readlink,
	.follow_link	= shmem_follow_link,
	.put_link	= shmem_put_link,
#ifdef CONFIG_TMPFS_XATTR
	.setxattr	= shmem_setxattr,
	.getxattr	= shmem_getxattr,
	.listxattr	= shmem_listxattr,
	.removexattr	= shmem_removexattr,
#endif
};

static struct dentry *shmem_get_parent(struct dentry *child)
{
	return ERR_PTR(-ESTALE);
}

static int shmem_match(struct inode *ino, void *vfh)
{
	__u32 *fh = vfh;
	__u64 inum = fh[2];
	inum = (inum << 32) | fh[1];
	return ino->i_ino == inum && fh[0] == ino->i_generation;
}

static struct dentry *shmem_fh_to_dentry(struct super_block *sb,
		struct fid *fid, int fh_len, int fh_type)
{
	struct inode *inode;
	struct dentry *dentry = NULL;
	u64 inum = fid->raw[2];
	inum = (inum << 32) | fid->raw[1];
	u64 inum;

	if (fh_len < 3)
		return NULL;

	inum = fid->raw[2];
	inum = (inum << 32) | fid->raw[1];

	inode = ilookup5(sb, (unsigned long)(inum + fid->raw[0]),
			shmem_match, fid->raw);
	if (inode) {
		dentry = d_find_alias(inode);
		iput(inode);
	}

	return dentry;
}

static int shmem_encode_fh(struct dentry *dentry, __u32 *fh, int *len,
				int connectable)
{
	struct inode *inode = dentry->d_inode;

	if (*len < 3)
		return 255;

	if (hlist_unhashed(&inode->i_hash)) {
static int shmem_encode_fh(struct inode *inode, __u32 *fh, int *len,
				struct inode *parent)
{
	if (*len < 3) {
		*len = 3;
		return FILEID_INVALID;
	}

	if (inode_unhashed(inode)) {
		/* Unfortunately insert_inode_hash is not idempotent,
		 * so as we hash inodes here rather than at creation
		 * time, we need a lock to ensure we only try
		 * to do it once
		 */
		static DEFINE_SPINLOCK(lock);
		spin_lock(&lock);
		if (hlist_unhashed(&inode->i_hash))
		if (inode_unhashed(inode))
			__insert_inode_hash(inode,
					    inode->i_ino + inode->i_generation);
		spin_unlock(&lock);
	}

	fh[0] = inode->i_generation;
	fh[1] = inode->i_ino;
	fh[2] = ((__u64)inode->i_ino) >> 32;

	*len = 3;
	return 1;
}

static const struct export_operations shmem_export_ops = {
	.get_parent     = shmem_get_parent,
	.encode_fh      = shmem_encode_fh,
	.fh_to_dentry	= shmem_fh_to_dentry,
};

static int shmem_parse_options(char *options, struct shmem_sb_info *sbinfo,
			       bool remount)
{
	char *this_char, *value, *rest;
	struct mempolicy *mpol = NULL;
	uid_t uid;
	gid_t gid;

	while (options != NULL) {
		this_char = options;
		for (;;) {
			/*
			 * NUL-terminate this option: unfortunately,
			 * mount options form a comma-separated list,
			 * but mpol's nodelist may also contain commas.
			 */
			options = strchr(options, ',');
			if (options == NULL)
				break;
			options++;
			if (!isdigit(*options)) {
				options[-1] = '\0';
				break;
			}
		}
		if (!*this_char)
			continue;
		if ((value = strchr(this_char,'=')) != NULL) {
			*value++ = 0;
		} else {
			printk(KERN_ERR
			    "tmpfs: No value for mount option '%s'\n",
			    this_char);
			return 1;
			goto error;
		}

		if (!strcmp(this_char,"size")) {
			unsigned long long size;
			size = memparse(value,&rest);
			if (*rest == '%') {
				size <<= PAGE_SHIFT;
				size *= totalram_pages;
				do_div(size, 100);
				rest++;
			}
			if (*rest)
				goto bad_val;
			sbinfo->max_blocks =
				DIV_ROUND_UP(size, PAGE_CACHE_SIZE);
		} else if (!strcmp(this_char,"nr_blocks")) {
			sbinfo->max_blocks = memparse(value, &rest);
			if (*rest)
				goto bad_val;
		} else if (!strcmp(this_char,"nr_inodes")) {
			sbinfo->max_inodes = memparse(value, &rest);
			if (*rest)
				goto bad_val;
		} else if (!strcmp(this_char,"mode")) {
			if (remount)
				continue;
			sbinfo->mode = simple_strtoul(value, &rest, 8) & 07777;
			if (*rest)
				goto bad_val;
		} else if (!strcmp(this_char,"uid")) {
			if (remount)
				continue;
			sbinfo->uid = simple_strtoul(value, &rest, 0);
			if (*rest)
				goto bad_val;
		} else if (!strcmp(this_char,"gid")) {
			if (remount)
				continue;
			sbinfo->gid = simple_strtoul(value, &rest, 0);
			if (*rest)
				goto bad_val;
		} else if (!strcmp(this_char,"mpol")) {
			if (mpol_parse_str(value, &sbinfo->mpol, 1))
			uid = simple_strtoul(value, &rest, 0);
			if (*rest)
				goto bad_val;
			sbinfo->uid = make_kuid(current_user_ns(), uid);
			if (!uid_valid(sbinfo->uid))
				goto bad_val;
		} else if (!strcmp(this_char,"gid")) {
			if (remount)
				continue;
			gid = simple_strtoul(value, &rest, 0);
			if (*rest)
				goto bad_val;
			sbinfo->gid = make_kgid(current_user_ns(), gid);
			if (!gid_valid(sbinfo->gid))
				goto bad_val;
		} else if (!strcmp(this_char,"mpol")) {
			mpol_put(mpol);
			mpol = NULL;
			if (mpol_parse_str(value, &mpol))
				goto bad_val;
		} else {
			printk(KERN_ERR "tmpfs: Bad mount option %s\n",
			       this_char);
			return 1;
		}
	}
			goto error;
		}
	}
	sbinfo->mpol = mpol;
	return 0;

bad_val:
	printk(KERN_ERR "tmpfs: Bad value '%s' for mount option '%s'\n",
	       value, this_char);
error:
	mpol_put(mpol);
	return 1;

}

static int shmem_remount_fs(struct super_block *sb, int *flags, char *data)
{
	struct shmem_sb_info *sbinfo = SHMEM_SB(sb);
	struct shmem_sb_info config = *sbinfo;
	unsigned long blocks;
	unsigned long inodes;
	int error = -EINVAL;

	unsigned long inodes;
	int error = -EINVAL;

	config.mpol = NULL;
	if (shmem_parse_options(data, &config, true))
		return error;

	spin_lock(&sbinfo->stat_lock);
	blocks = sbinfo->max_blocks - sbinfo->free_blocks;
	inodes = sbinfo->max_inodes - sbinfo->free_inodes;
	if (config.max_blocks < blocks)
	inodes = sbinfo->max_inodes - sbinfo->free_inodes;
	if (percpu_counter_compare(&sbinfo->used_blocks, config.max_blocks) > 0)
		goto out;
	if (config.max_inodes < inodes)
		goto out;
	/*
	 * Those tests also disallow limited->unlimited while any are in
	 * use, so i_blocks will always be zero when max_blocks is zero;
	 * Those tests disallow limited->unlimited while any are in use;
	 * but we must separately disallow unlimited->limited, because
	 * in that case we have no record of how much is already in use.
	 */
	if (config.max_blocks && !sbinfo->max_blocks)
		goto out;
	if (config.max_inodes && !sbinfo->max_inodes)
		goto out;

	error = 0;
	sbinfo->max_blocks  = config.max_blocks;
	sbinfo->free_blocks = config.max_blocks - blocks;
	sbinfo->max_inodes  = config.max_inodes;
	sbinfo->free_inodes = config.max_inodes - inodes;

	mpol_put(sbinfo->mpol);
	sbinfo->mpol        = config.mpol;	/* transfers initial ref */
	sbinfo->max_inodes  = config.max_inodes;
	sbinfo->free_inodes = config.max_inodes - inodes;

	/*
	 * Preserve previous mempolicy unless mpol remount option was specified.
	 */
	if (config.mpol) {
		mpol_put(sbinfo->mpol);
		sbinfo->mpol = config.mpol;	/* transfers initial ref */
	}
out:
	spin_unlock(&sbinfo->stat_lock);
	return error;
}

static int shmem_show_options(struct seq_file *seq, struct vfsmount *vfs)
{
	struct shmem_sb_info *sbinfo = SHMEM_SB(vfs->mnt_sb);
static int shmem_show_options(struct seq_file *seq, struct dentry *root)
{
	struct shmem_sb_info *sbinfo = SHMEM_SB(root->d_sb);

	if (sbinfo->max_blocks != shmem_default_max_blocks())
		seq_printf(seq, ",size=%luk",
			sbinfo->max_blocks << (PAGE_CACHE_SHIFT - 10));
	if (sbinfo->max_inodes != shmem_default_max_inodes())
		seq_printf(seq, ",nr_inodes=%lu", sbinfo->max_inodes);
	if (sbinfo->mode != (S_IRWXUGO | S_ISVTX))
		seq_printf(seq, ",mode=%03o", sbinfo->mode);
	if (sbinfo->uid != 0)
		seq_printf(seq, ",uid=%u", sbinfo->uid);
	if (sbinfo->gid != 0)
		seq_printf(seq, ",gid=%u", sbinfo->gid);
	shmem_show_mpol(seq, sbinfo->mpol);
	return 0;
}
		seq_printf(seq, ",mode=%03ho", sbinfo->mode);
	if (!uid_eq(sbinfo->uid, GLOBAL_ROOT_UID))
		seq_printf(seq, ",uid=%u",
				from_kuid_munged(&init_user_ns, sbinfo->uid));
	if (!gid_eq(sbinfo->gid, GLOBAL_ROOT_GID))
		seq_printf(seq, ",gid=%u",
				from_kgid_munged(&init_user_ns, sbinfo->gid));
	shmem_show_mpol(seq, sbinfo->mpol);
	return 0;
}

#define MFD_NAME_PREFIX "memfd:"
#define MFD_NAME_PREFIX_LEN (sizeof(MFD_NAME_PREFIX) - 1)
#define MFD_NAME_MAX_LEN (NAME_MAX - MFD_NAME_PREFIX_LEN)

#define MFD_ALL_FLAGS (MFD_CLOEXEC | MFD_ALLOW_SEALING)

SYSCALL_DEFINE2(memfd_create,
		const char __user *, uname,
		unsigned int, flags)
{
	struct shmem_inode_info *info;
	struct file *file;
	int fd, error;
	char *name;
	long len;

	if (flags & ~(unsigned int)MFD_ALL_FLAGS)
		return -EINVAL;

	/* length includes terminating zero */
	len = strnlen_user(uname, MFD_NAME_MAX_LEN + 1);
	if (len <= 0)
		return -EFAULT;
	if (len > MFD_NAME_MAX_LEN + 1)
		return -EINVAL;

	name = kmalloc(len + MFD_NAME_PREFIX_LEN, GFP_TEMPORARY);
	if (!name)
		return -ENOMEM;

	strcpy(name, MFD_NAME_PREFIX);
	if (copy_from_user(&name[MFD_NAME_PREFIX_LEN], uname, len)) {
		error = -EFAULT;
		goto err_name;
	}

	/* terminating-zero may have changed after strnlen_user() returned */
	if (name[len + MFD_NAME_PREFIX_LEN - 1]) {
		error = -EFAULT;
		goto err_name;
	}

	fd = get_unused_fd_flags((flags & MFD_CLOEXEC) ? O_CLOEXEC : 0);
	if (fd < 0) {
		error = fd;
		goto err_name;
	}

	file = shmem_file_setup(name, 0, VM_NORESERVE);
	if (IS_ERR(file)) {
		error = PTR_ERR(file);
		goto err_fd;
	}
	info = SHMEM_I(file_inode(file));
	file->f_mode |= FMODE_LSEEK | FMODE_PREAD | FMODE_PWRITE;
	file->f_flags |= O_RDWR | O_LARGEFILE;
	if (flags & MFD_ALLOW_SEALING)
		info->seals &= ~F_SEAL_SEAL;

	fd_install(fd, file);
	kfree(name);
	return fd;

err_fd:
	put_unused_fd(fd);
err_name:
	kfree(name);
	return error;
}

#endif /* CONFIG_TMPFS */

static void shmem_put_super(struct super_block *sb)
{
	kfree(sb->s_fs_info);
	sb->s_fs_info = NULL;
}

static int shmem_fill_super(struct super_block *sb,
			    void *data, int silent)
{
	struct inode *inode;
	struct dentry *root;
	struct shmem_sb_info *sbinfo = SHMEM_SB(sb);

	percpu_counter_destroy(&sbinfo->used_blocks);
	mpol_put(sbinfo->mpol);
	kfree(sbinfo);
	sb->s_fs_info = NULL;
}

int shmem_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode *inode;
	struct shmem_sb_info *sbinfo;
	int err = -ENOMEM;

	/* Round up to L1_CACHE_BYTES to resist false sharing */
	sbinfo = kmalloc(max((int)sizeof(struct shmem_sb_info),
	sbinfo = kzalloc(max((int)sizeof(struct shmem_sb_info),
				L1_CACHE_BYTES), GFP_KERNEL);
	if (!sbinfo)
		return -ENOMEM;

	sbinfo->max_blocks = 0;
	sbinfo->max_inodes = 0;
	sbinfo->mode = S_IRWXUGO | S_ISVTX;
	sbinfo->uid = current->fsuid;
	sbinfo->gid = current->fsgid;
	sbinfo->mpol = NULL;
	sbinfo->mode = S_IRWXUGO | S_ISVTX;
	sbinfo->uid = current_fsuid();
	sbinfo->gid = current_fsgid();
	sb->s_fs_info = sbinfo;

#ifdef CONFIG_TMPFS
	/*
	 * Per default we only allow half of the physical ram per
	 * tmpfs instance, limiting inodes to one per page of lowmem;
	 * but the internal instance is left unlimited.
	 */
	if (!(sb->s_flags & MS_NOUSER)) {
	if (!(sb->s_flags & MS_KERNMOUNT)) {
		sbinfo->max_blocks = shmem_default_max_blocks();
		sbinfo->max_inodes = shmem_default_max_inodes();
		if (shmem_parse_options(data, sbinfo, false)) {
			err = -EINVAL;
			goto failed;
		}
	}
	sb->s_export_op = &shmem_export_ops;
	} else {
		sb->s_flags |= MS_NOUSER;
	}
	sb->s_export_op = &shmem_export_ops;
	sb->s_flags |= MS_NOSEC;
#else
	sb->s_flags |= MS_NOUSER;
#endif

	spin_lock_init(&sbinfo->stat_lock);
	sbinfo->free_blocks = sbinfo->max_blocks;
	sbinfo->free_inodes = sbinfo->max_inodes;

	sb->s_maxbytes = SHMEM_MAX_BYTES;
	if (percpu_counter_init(&sbinfo->used_blocks, 0, GFP_KERNEL))
		goto failed;
	sbinfo->free_inodes = sbinfo->max_inodes;

	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = TMPFS_MAGIC;
	sb->s_op = &shmem_ops;
	sb->s_time_gran = 1;
#ifdef CONFIG_TMPFS_POSIX_ACL
	sb->s_xattr = shmem_xattr_handlers;
	sb->s_flags |= MS_POSIXACL;
#endif

	inode = shmem_get_inode(sb, S_IFDIR | sbinfo->mode, 0);
#ifdef CONFIG_TMPFS_XATTR
	sb->s_xattr = shmem_xattr_handlers;
#endif
#ifdef CONFIG_TMPFS_POSIX_ACL
	sb->s_flags |= MS_POSIXACL;
#endif

	inode = shmem_get_inode(sb, NULL, S_IFDIR | sbinfo->mode, 0, VM_NORESERVE);
	if (!inode)
		goto failed;
	inode->i_uid = sbinfo->uid;
	inode->i_gid = sbinfo->gid;
	root = d_alloc_root(inode);
	if (!root)
		goto failed_iput;
	sb->s_root = root;
	return 0;

failed_iput:
	iput(inode);
	sb->s_root = d_make_root(inode);
	if (!sb->s_root)
		goto failed;
	return 0;

failed:
	shmem_put_super(sb);
	return err;
}

static struct kmem_cache *shmem_inode_cachep;

static struct inode *shmem_alloc_inode(struct super_block *sb)
{
	struct shmem_inode_info *p;
	p = (struct shmem_inode_info *)kmem_cache_alloc(shmem_inode_cachep, GFP_KERNEL);
	if (!p)
		return NULL;
	return &p->vfs_inode;
	struct shmem_inode_info *info;
	info = kmem_cache_alloc(shmem_inode_cachep, GFP_KERNEL);
	if (!info)
		return NULL;
	return &info->vfs_inode;
}

static void shmem_destroy_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	kfree(inode->i_link);
	kmem_cache_free(shmem_inode_cachep, SHMEM_I(inode));
}

static void shmem_destroy_inode(struct inode *inode)
{
	if ((inode->i_mode & S_IFMT) == S_IFREG) {
		/* only struct inode is valid if it's an inline symlink */
		mpol_free_shared_policy(&SHMEM_I(inode)->policy);
	}
	shmem_acl_destroy_inode(inode);
	kmem_cache_free(shmem_inode_cachep, SHMEM_I(inode));
}

static void init_once(void *foo)
{
	struct shmem_inode_info *p = (struct shmem_inode_info *) foo;

	inode_init_once(&p->vfs_inode);
#ifdef CONFIG_TMPFS_POSIX_ACL
	p->i_acl = NULL;
	p->i_default_acl = NULL;
#endif
}

static int init_inodecache(void)
{
	shmem_inode_cachep = kmem_cache_create("shmem_inode_cache",
				sizeof(struct shmem_inode_info),
				0, SLAB_PANIC, init_once);
	return 0;
}

static void destroy_inodecache(void)
	if (S_ISREG(inode->i_mode))
		mpol_free_shared_policy(&SHMEM_I(inode)->policy);
	call_rcu(&inode->i_rcu, shmem_destroy_callback);
}

static void shmem_init_inode(void *foo)
{
	struct shmem_inode_info *info = foo;
	inode_init_once(&info->vfs_inode);
}

static int shmem_init_inodecache(void)
{
	shmem_inode_cachep = kmem_cache_create("shmem_inode_cache",
				sizeof(struct shmem_inode_info),
				0, SLAB_PANIC, shmem_init_inode);
	return 0;
}

static void shmem_destroy_inodecache(void)
{
	kmem_cache_destroy(shmem_inode_cachep);
}

static const struct address_space_operations shmem_aops = {
	.writepage	= shmem_writepage,
	.set_page_dirty	= __set_page_dirty_no_writeback,
#ifdef CONFIG_TMPFS
	.readpage	= shmem_readpage,
	.write_begin	= shmem_write_begin,
	.write_end	= shmem_write_end,
#endif
	.migratepage	= migrate_page,
	.write_begin	= shmem_write_begin,
	.write_end	= shmem_write_end,
#endif
#ifdef CONFIG_MIGRATION
	.migratepage	= migrate_page,
#endif
	.error_remove_page = generic_error_remove_page,
};

static const struct file_operations shmem_file_operations = {
	.mmap		= shmem_mmap,
#ifdef CONFIG_TMPFS
	.llseek		= generic_file_llseek,
	.read		= do_sync_read,
	.write		= do_sync_write,
	.aio_read	= shmem_file_aio_read,
	.aio_write	= generic_file_aio_write,
	.fsync		= simple_sync_file,
	.splice_read	= generic_file_splice_read,
	.splice_write	= generic_file_splice_write,
	.llseek		= shmem_file_llseek,
	.read_iter	= shmem_file_read_iter,
	.write_iter	= generic_file_write_iter,
	.fsync		= noop_fsync,
	.splice_read	= shmem_file_splice_read,
	.splice_write	= iter_file_splice_write,
	.fallocate	= shmem_fallocate,
#endif
};

static const struct inode_operations shmem_inode_operations = {
	.truncate	= shmem_truncate,
	.setattr	= shmem_notify_change,
	.truncate_range	= shmem_truncate_range,
#ifdef CONFIG_TMPFS_POSIX_ACL
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.listxattr	= generic_listxattr,
	.removexattr	= generic_removexattr,
	.permission	= shmem_permission,
#endif

	.getattr	= shmem_getattr,
	.setattr	= shmem_setattr,
#ifdef CONFIG_TMPFS_XATTR
	.setxattr	= shmem_setxattr,
	.getxattr	= shmem_getxattr,
	.listxattr	= shmem_listxattr,
	.removexattr	= shmem_removexattr,
	.set_acl	= simple_set_acl,
#endif
};

static const struct inode_operations shmem_dir_inode_operations = {
#ifdef CONFIG_TMPFS
	.create		= shmem_create,
	.lookup		= simple_lookup,
	.link		= shmem_link,
	.unlink		= shmem_unlink,
	.symlink	= shmem_symlink,
	.mkdir		= shmem_mkdir,
	.rmdir		= shmem_rmdir,
	.mknod		= shmem_mknod,
	.rename		= shmem_rename,
#endif
#ifdef CONFIG_TMPFS_POSIX_ACL
	.setattr	= shmem_notify_change,
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.listxattr	= generic_listxattr,
	.removexattr	= generic_removexattr,
	.permission	= shmem_permission,
	.rename2	= shmem_rename2,
	.tmpfile	= shmem_tmpfile,
#endif
#ifdef CONFIG_TMPFS_XATTR
	.setxattr	= shmem_setxattr,
	.getxattr	= shmem_getxattr,
	.listxattr	= shmem_listxattr,
	.removexattr	= shmem_removexattr,
#endif
#ifdef CONFIG_TMPFS_POSIX_ACL
	.setattr	= shmem_setattr,
	.set_acl	= simple_set_acl,
#endif
};

static const struct inode_operations shmem_special_inode_operations = {
#ifdef CONFIG_TMPFS_POSIX_ACL
	.setattr	= shmem_notify_change,
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.listxattr	= generic_listxattr,
	.removexattr	= generic_removexattr,
	.permission	= shmem_permission,
#ifdef CONFIG_TMPFS_XATTR
	.setxattr	= shmem_setxattr,
	.getxattr	= shmem_getxattr,
	.listxattr	= shmem_listxattr,
	.removexattr	= shmem_removexattr,
#endif
#ifdef CONFIG_TMPFS_POSIX_ACL
	.setattr	= shmem_setattr,
	.set_acl	= simple_set_acl,
#endif
};

static const struct super_operations shmem_ops = {
	.alloc_inode	= shmem_alloc_inode,
	.destroy_inode	= shmem_destroy_inode,
#ifdef CONFIG_TMPFS
	.statfs		= shmem_statfs,
	.remount_fs	= shmem_remount_fs,
	.show_options	= shmem_show_options,
#endif
	.delete_inode	= shmem_delete_inode,
	.evict_inode	= shmem_evict_inode,
	.drop_inode	= generic_delete_inode,
	.put_super	= shmem_put_super,
};

static struct vm_operations_struct shmem_vm_ops = {
	.fault		= shmem_fault,
static const struct vm_operations_struct shmem_vm_ops = {
	.fault		= shmem_fault,
	.map_pages	= filemap_map_pages,
#ifdef CONFIG_NUMA
	.set_policy     = shmem_set_policy,
	.get_policy     = shmem_get_policy,
#endif
};


static int shmem_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
	return get_sb_nodev(fs_type, flags, data, shmem_fill_super, mnt);
}

static struct file_system_type tmpfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "tmpfs",
	.get_sb		= shmem_get_sb,
	.kill_sb	= kill_litter_super,
};
static struct vfsmount *shm_mnt;

static int __init init_tmpfs(void)
{
	int error;

	error = bdi_init(&shmem_backing_dev_info);
	if (error)
		goto out4;

	error = init_inodecache();
	if (error)
		goto out3;

	error = register_filesystem(&tmpfs_fs_type);
static struct dentry *shmem_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return mount_nodev(fs_type, flags, data, shmem_fill_super);
}

static struct file_system_type shmem_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "tmpfs",
	.mount		= shmem_mount,
	.kill_sb	= kill_litter_super,
	.fs_flags	= FS_USERNS_MOUNT,
};

int __init shmem_init(void)
{
	int error;

	/* If rootfs called this, don't re-init */
	if (shmem_inode_cachep)
		return 0;

	error = shmem_init_inodecache();
	if (error)
		goto out3;

	error = register_filesystem(&shmem_fs_type);
	if (error) {
		printk(KERN_ERR "Could not register tmpfs\n");
		goto out2;
	}

	shm_mnt = vfs_kern_mount(&tmpfs_fs_type, MS_NOUSER,
				tmpfs_fs_type.name, NULL);
	shm_mnt = kern_mount(&shmem_fs_type);
	if (IS_ERR(shm_mnt)) {
		error = PTR_ERR(shm_mnt);
		printk(KERN_ERR "Could not kern_mount tmpfs\n");
		goto out1;
	}
	return 0;

out1:
	unregister_filesystem(&tmpfs_fs_type);
out2:
	destroy_inodecache();
out3:
	bdi_destroy(&shmem_backing_dev_info);
out4:
	shm_mnt = ERR_PTR(error);
	return error;
}
module_init(init_tmpfs)

/**
 * shmem_file_setup - get an unlinked file living in tmpfs
 * @name: name for dentry (to be seen in /proc/<pid>/maps
 * @size: size to be set for the file
 * @flags: vm_flags
 */
struct file *shmem_file_setup(char *name, loff_t size, unsigned long flags)
{
	int error;
	struct file *file;
	struct inode *inode;
	struct dentry *dentry, *root;
	struct qstr this;

	if (IS_ERR(shm_mnt))
		return (void *)shm_mnt;

	if (size < 0 || size > SHMEM_MAX_BYTES)
	unregister_filesystem(&shmem_fs_type);
out2:
	shmem_destroy_inodecache();
out3:
	shm_mnt = ERR_PTR(error);
	return error;
}

#else /* !CONFIG_SHMEM */

/*
 * tiny-shmem: simple shmemfs and tmpfs using ramfs code
 *
 * This is intended for small system where the benefits of the full
 * shmem code (swap-backed and resource-limited) are outweighed by
 * their complexity. On systems without swap this code should be
 * effectively equivalent, but much lighter weight.
 */

static struct file_system_type shmem_fs_type = {
	.name		= "tmpfs",
	.mount		= ramfs_mount,
	.kill_sb	= kill_litter_super,
	.fs_flags	= FS_USERNS_MOUNT,
};

int __init shmem_init(void)
{
	BUG_ON(register_filesystem(&shmem_fs_type) != 0);

	shm_mnt = kern_mount(&shmem_fs_type);
	BUG_ON(IS_ERR(shm_mnt));

	return 0;
}

int shmem_unuse(swp_entry_t swap, struct page *page)
{
	return 0;
}

int shmem_lock(struct file *file, int lock, struct user_struct *user)
{
	return 0;
}

void shmem_unlock_mapping(struct address_space *mapping)
{
}

void shmem_truncate_range(struct inode *inode, loff_t lstart, loff_t lend)
{
	truncate_inode_pages_range(inode->i_mapping, lstart, lend);
}
EXPORT_SYMBOL_GPL(shmem_truncate_range);

#define shmem_vm_ops				generic_file_vm_ops
#define shmem_file_operations			ramfs_file_operations
#define shmem_get_inode(sb, dir, mode, dev, flags)	ramfs_get_inode(sb, dir, mode, dev)
#define shmem_acct_size(flags, size)		0
#define shmem_unacct_size(flags, size)		do {} while (0)

#endif /* CONFIG_SHMEM */

/* common code */

static struct dentry_operations anon_ops = {
	.d_dname = simple_dname
};

static struct file *__shmem_file_setup(const char *name, loff_t size,
				       unsigned long flags, unsigned int i_flags)
{
	struct file *res;
	struct inode *inode;
	struct path path;
	struct super_block *sb;
	struct qstr this;

	if (IS_ERR(shm_mnt))
		return ERR_CAST(shm_mnt);

	if (size < 0 || size > MAX_LFS_FILESIZE)
		return ERR_PTR(-EINVAL);

	if (shmem_acct_size(flags, size))
		return ERR_PTR(-ENOMEM);

	error = -ENOMEM;
	this.name = name;
	this.len = strlen(name);
	this.hash = 0; /* will go */
	root = shm_mnt->mnt_root;
	dentry = d_alloc(root, &this);
	if (!dentry)
		goto put_memory;

	error = -ENFILE;
	file = get_empty_filp();
	if (!file)
		goto put_dentry;

	error = -ENOSPC;
	inode = shmem_get_inode(root->d_sb, S_IFREG | S_IRWXUGO, 0);
	if (!inode)
		goto close_file;

	SHMEM_I(inode)->flags = flags & VM_ACCOUNT;
	d_instantiate(dentry, inode);
	inode->i_size = size;
	inode->i_nlink = 0;	/* It is unlinked */
	init_file(file, shm_mnt, dentry, FMODE_WRITE | FMODE_READ,
			&shmem_file_operations);
	return file;

close_file:
	put_filp(file);
put_dentry:
	dput(dentry);
put_memory:
	shmem_unacct_size(flags, size);
	return ERR_PTR(error);
}

/**
	res = ERR_PTR(-ENOMEM);
	this.name = name;
	this.len = strlen(name);
	this.hash = 0; /* will go */
	sb = shm_mnt->mnt_sb;
	path.mnt = mntget(shm_mnt);
	path.dentry = d_alloc_pseudo(sb, &this);
	if (!path.dentry)
		goto put_memory;
	d_set_d_op(path.dentry, &anon_ops);

	res = ERR_PTR(-ENOSPC);
	inode = shmem_get_inode(sb, NULL, S_IFREG | S_IRWXUGO, 0, flags);
	if (!inode)
		goto put_memory;

	inode->i_flags |= i_flags;
	d_instantiate(path.dentry, inode);
	inode->i_size = size;
	clear_nlink(inode);	/* It is unlinked */
	res = ERR_PTR(ramfs_nommu_expand_for_mapping(inode, size));
	if (IS_ERR(res))
		goto put_path;

	res = alloc_file(&path, FMODE_WRITE | FMODE_READ,
		  &shmem_file_operations);
	if (IS_ERR(res))
		goto put_path;

	return res;

put_memory:
	shmem_unacct_size(flags, size);
put_path:
	path_put(&path);
	return res;
}

/**
 * shmem_kernel_file_setup - get an unlinked file living in tmpfs which must be
 * 	kernel internal.  There will be NO LSM permission checks against the
 * 	underlying inode.  So users of this interface must do LSM checks at a
 *	higher layer.  The users are the big_key and shm implementations.  LSM
 *	checks are provided at the key or shm level rather than the inode.
 * @name: name for dentry (to be seen in /proc/<pid>/maps
 * @size: size to be set for the file
 * @flags: VM_NORESERVE suppresses pre-accounting of the entire object size
 */
struct file *shmem_kernel_file_setup(const char *name, loff_t size, unsigned long flags)
{
	return __shmem_file_setup(name, size, flags, S_PRIVATE);
}

/**
 * shmem_file_setup - get an unlinked file living in tmpfs
 * @name: name for dentry (to be seen in /proc/<pid>/maps
 * @size: size to be set for the file
 * @flags: VM_NORESERVE suppresses pre-accounting of the entire object size
 */
struct file *shmem_file_setup(const char *name, loff_t size, unsigned long flags)
{
	return __shmem_file_setup(name, size, flags, 0);
}
EXPORT_SYMBOL_GPL(shmem_file_setup);

/**
 * shmem_zero_setup - setup a shared anonymous mapping
 * @vma: the vma to be mmapped is prepared by do_mmap_pgoff
 */
int shmem_zero_setup(struct vm_area_struct *vma)
{
	struct file *file;
	loff_t size = vma->vm_end - vma->vm_start;

	file = shmem_file_setup("dev/zero", size, vma->vm_flags);
	/*
	 * Cloning a new file under mmap_sem leads to a lock ordering conflict
	 * between XFS directory reading and selinux: since this file is only
	 * accessible to the user through its mapping, use S_PRIVATE flag to
	 * bypass file security, in the same way as shmem_kernel_file_setup().
	 */
	file = __shmem_file_setup("dev/zero", size, vma->vm_flags, S_PRIVATE);
	if (IS_ERR(file))
		return PTR_ERR(file);

	if (vma->vm_file)
		fput(vma->vm_file);
	vma->vm_file = file;
	vma->vm_ops = &shmem_vm_ops;
	return 0;
}

/**
 * shmem_read_mapping_page_gfp - read into page cache, using specified page allocation flags.
 * @mapping:	the page's address_space
 * @index:	the page index
 * @gfp:	the page allocator flags to use if allocating
 *
 * This behaves as a tmpfs "read_cache_page_gfp(mapping, index, gfp)",
 * with any new page allocations done using the specified allocation flags.
 * But read_cache_page_gfp() uses the ->readpage() method: which does not
 * suit tmpfs, since it may have pages in swapcache, and needs to find those
 * for itself; although drivers/gpu/drm i915 and ttm rely upon this support.
 *
 * i915_gem_object_get_pages_gtt() mixes __GFP_NORETRY | __GFP_NOWARN in
 * with the mapping_gfp_mask(), to avoid OOMing the machine unnecessarily.
 */
struct page *shmem_read_mapping_page_gfp(struct address_space *mapping,
					 pgoff_t index, gfp_t gfp)
{
#ifdef CONFIG_SHMEM
	struct inode *inode = mapping->host;
	struct page *page;
	int error;

	BUG_ON(mapping->a_ops != &shmem_aops);
	error = shmem_getpage_gfp(inode, index, &page, SGP_CACHE, gfp, NULL);
	if (error)
		page = ERR_PTR(error);
	else
		unlock_page(page);
	return page;
#else
	/*
	 * The tiny !SHMEM case uses ramfs without swap
	 */
	return read_cache_page_gfp(mapping, index, gfp);
#endif
}
EXPORT_SYMBOL_GPL(shmem_read_mapping_page_gfp);
