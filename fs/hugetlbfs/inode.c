/*
 * hugetlbpage-backed filesystem.  Based on ramfs.
 *
 * William Irwin, 2002
 * Nadia Yvette Chambers, 2002
 *
 * Copyright (C) 2002 Linus Torvalds.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/thread_info.h>
#include <asm/current.h>
#include <linux/sched.h>		/* remove ASAP */
#include <linux/falloc.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/file.h>
#include <linux/kernel.h>
#include <linux/writeback.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/capability.h>
#include <linux/ctype.h>
#include <linux/backing-dev.h>
#include <linux/hugetlb.h>
#include <linux/pagevec.h>
#include <linux/parser.h>
#include <linux/mman.h>
#include <linux/quotaops.h>
#include <linux/slab.h>
#include <linux/dnotify.h>
#include <linux/statfs.h>
#include <linux/security.h>

#include <asm/uaccess.h>

/* some random number */
#define HUGETLBFS_MAGIC	0x958458f6

#include <linux/magic.h>
#include <linux/migrate.h>
#include <linux/uio.h>

#include <asm/uaccess.h>

static const struct super_operations hugetlbfs_ops;
static const struct address_space_operations hugetlbfs_aops;
const struct file_operations hugetlbfs_file_operations;
static const struct inode_operations hugetlbfs_dir_inode_operations;
static const struct inode_operations hugetlbfs_inode_operations;

static struct backing_dev_info hugetlbfs_backing_dev_info = {
	.ra_pages	= 0,	/* No readahead */
	.capabilities	= BDI_CAP_NO_ACCT_AND_WRITEBACK,
};

struct hugetlbfs_config {
	kuid_t   uid;
	kgid_t   gid;
	umode_t mode;
	long	max_hpages;
	long	nr_inodes;
	struct hstate *hstate;
	long    min_hpages;
};

struct hugetlbfs_inode_info {
	struct shared_policy policy;
	struct inode vfs_inode;
};

static inline struct hugetlbfs_inode_info *HUGETLBFS_I(struct inode *inode)
{
	return container_of(inode, struct hugetlbfs_inode_info, vfs_inode);
}

int sysctl_hugetlb_shm_group;

enum {
	Opt_size, Opt_nr_inodes,
	Opt_mode, Opt_uid, Opt_gid,
	Opt_pagesize,
	Opt_err,
};

static match_table_t tokens = {
	Opt_pagesize, Opt_min_size,
	Opt_err,
};

static const match_table_t tokens = {
	{Opt_size,	"size=%s"},
	{Opt_nr_inodes,	"nr_inodes=%s"},
	{Opt_mode,	"mode=%o"},
	{Opt_uid,	"uid=%u"},
	{Opt_gid,	"gid=%u"},
	{Opt_pagesize,	"pagesize=%s"},
	{Opt_err,	NULL},
};

	{Opt_min_size,	"min_size=%s"},
	{Opt_err,	NULL},
};

#ifdef CONFIG_NUMA
static inline void hugetlb_set_vma_policy(struct vm_area_struct *vma,
					struct inode *inode, pgoff_t index)
{
	vma->vm_policy = mpol_shared_policy_lookup(&HUGETLBFS_I(inode)->policy,
							index);
}

static inline void hugetlb_drop_vma_policy(struct vm_area_struct *vma)
{
	mpol_cond_put(vma->vm_policy);
}
#else
static inline void hugetlb_set_vma_policy(struct vm_area_struct *vma,
					struct inode *inode, pgoff_t index)
{
}

static inline void hugetlb_drop_vma_policy(struct vm_area_struct *vma)
{
}
#endif

static void huge_pagevec_release(struct pagevec *pvec)
{
	int i;

	for (i = 0; i < pagevec_count(pvec); ++i)
		put_page(pvec->pages[i]);

	pagevec_reinit(pvec);
}

/*
 * Mask used when checking the page offset value passed in via system
 * calls.  This value will be converted to a loff_t which is signed.
 * Therefore, we want to check the upper PAGE_SHIFT + 1 bits of the
 * value.  The extra bit (- 1 in the shift value) is to take the sign
 * bit into account.
 */
#define PGOFF_LOFFT_MAX \
	(((1UL << (PAGE_SHIFT + 1)) - 1) <<  (BITS_PER_LONG - (PAGE_SHIFT + 1)))

static int hugetlbfs_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct inode *inode = file->f_path.dentry->d_inode;
	struct inode *inode = file_inode(file);
	loff_t len, vma_len;
	int ret;
	struct hstate *h = hstate_file(file);

	/*
	 * vma address alignment (but not the pgoff alignment) has
	 * already been checked by prepare_hugepage_range.  If you add
	 * any error returns here, do so after setting VM_HUGETLB, so
	 * is_vm_hugetlb_page tests below unmap_region go the right
	 * way when do_mmap_pgoff unwinds (may be important on powerpc
	 * and ia64).
	 */
	vma->vm_flags |= VM_HUGETLB | VM_RESERVED;
	vma->vm_ops = &hugetlb_vm_ops;

	if (vma->vm_pgoff & ~(huge_page_mask(h) >> PAGE_SHIFT))
	vma->vm_flags |= VM_HUGETLB | VM_DONTEXPAND;
	vma->vm_ops = &hugetlb_vm_ops;

	/*
	 * page based offset in vm_pgoff could be sufficiently large to
	 * overflow a loff_t when converted to byte offset.  This can
	 * only happen on architectures where sizeof(loff_t) ==
	 * sizeof(unsigned long).  So, only check in those instances.
	 */
	if (sizeof(unsigned long) == sizeof(loff_t)) {
		if (vma->vm_pgoff & PGOFF_LOFFT_MAX)
			return -EINVAL;
	}

	/* must be huge page aligned */
	if (vma->vm_pgoff & (~huge_page_mask(h) >> PAGE_SHIFT))
		return -EINVAL;

	vma_len = (loff_t)(vma->vm_end - vma->vm_start);
	len = vma_len + ((loff_t)vma->vm_pgoff << PAGE_SHIFT);
	/* check for overflow */
	if (len < vma_len)
		return -EINVAL;

	mutex_lock(&inode->i_mutex);
	file_accessed(file);

	ret = -ENOMEM;
	if (hugetlb_reserve_pages(inode,
				vma->vm_pgoff >> huge_page_order(h),
				len >> huge_page_shift(h), vma))
		goto out;

	ret = 0;
	hugetlb_prefault_arch_hook(vma->vm_mm);
				len >> huge_page_shift(h), vma,
				vma->vm_flags))
		goto out;

	ret = 0;
	if (vma->vm_flags & VM_WRITE && inode->i_size < len)
		i_size_write(inode, len);
out:
	mutex_unlock(&inode->i_mutex);

	return ret;
}

/*
 * Called under down_write(mmap_sem).
 */

#ifndef HAVE_ARCH_HUGETLB_UNMAPPED_AREA
static unsigned long
hugetlb_get_unmapped_area(struct file *file, unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long start_addr;
	struct hstate *h = hstate_file(file);
	struct hstate *h = hstate_file(file);
	struct vm_unmapped_area_info info;

	if (len & ~huge_page_mask(h))
		return -EINVAL;
	if (len > TASK_SIZE)
		return -ENOMEM;

	if (flags & MAP_FIXED) {
		if (prepare_hugepage_range(file, addr, len))
			return -EINVAL;
		return addr;
	}

	if (addr) {
		addr = ALIGN(addr, huge_page_size(h));
		vma = find_vma(mm, addr);
		if (TASK_SIZE - len >= addr &&
		    (!vma || addr + len <= vm_start_gap(vma)))
			return addr;
	}

	start_addr = mm->free_area_cache;

	if (len <= mm->cached_hole_size)
		start_addr = TASK_UNMAPPED_BASE;

full_search:
	addr = ALIGN(start_addr, huge_page_size(h));

	for (vma = find_vma(mm, addr); ; vma = vma->vm_next) {
		/* At this point:  (!vma || addr < vma->vm_end). */
		if (TASK_SIZE - len < addr) {
			/*
			 * Start a new search - just in case we missed
			 * some holes.
			 */
			if (start_addr != TASK_UNMAPPED_BASE) {
				start_addr = TASK_UNMAPPED_BASE;
				goto full_search;
			}
			return -ENOMEM;
		}

		if (!vma || addr + len <= vma->vm_start)
			return addr;
		addr = ALIGN(vma->vm_end, huge_page_size(h));
	}
}
#endif

static int
hugetlbfs_read_actor(struct page *page, unsigned long offset,
			char __user *buf, unsigned long count,
			unsigned long size)
{
	char *kaddr;
	unsigned long left, copied = 0;
	int i, chunksize;

	if (size > count)
		size = count;

	info.flags = 0;
	info.length = len;
	info.low_limit = TASK_UNMAPPED_BASE;
	info.high_limit = TASK_SIZE;
	info.align_mask = PAGE_MASK & ~huge_page_mask(h);
	info.align_offset = 0;
	return vm_unmapped_area(&info);
}
#endif

static size_t
hugetlbfs_read_actor(struct page *page, unsigned long offset,
			struct iov_iter *to, unsigned long size)
{
	size_t copied = 0;
	int i, chunksize;

	/* Find which 4k chunk and offset with in that chunk */
	i = offset >> PAGE_CACHE_SHIFT;
	offset = offset & ~PAGE_CACHE_MASK;

	while (size) {
		size_t n;
		chunksize = PAGE_CACHE_SIZE;
		if (offset)
			chunksize -= offset;
		if (chunksize > size)
			chunksize = size;
		kaddr = kmap(&page[i]);
		left = __copy_to_user(buf, kaddr + offset, chunksize);
		kunmap(&page[i]);
		if (left) {
			copied += (chunksize - left);
			break;
		}
		offset = 0;
		size -= chunksize;
		buf += chunksize;
		copied += chunksize;
		i++;
	}
	return copied ? copied : -EFAULT;
		n = copy_page_to_iter(&page[i], offset, chunksize, to);
		copied += n;
		if (n != chunksize)
			return copied;
		offset = 0;
		size -= chunksize;
		i++;
	}
	return copied;
}

/*
 * Support for read() - Find the page attached to f_mapping and copy out the
 * data. Its *very* similar to do_generic_mapping_read(), we can't use that
 * since it has PAGE_CACHE_SIZE assumptions.
 */
static ssize_t hugetlbfs_read(struct file *filp, char __user *buf,
			      size_t len, loff_t *ppos)
{
	struct hstate *h = hstate_file(filp);
	struct address_space *mapping = filp->f_mapping;
	struct inode *inode = mapping->host;
	unsigned long index = *ppos >> huge_page_shift(h);
	unsigned long offset = *ppos & ~huge_page_mask(h);
static ssize_t hugetlbfs_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct file *file = iocb->ki_filp;
	struct hstate *h = hstate_file(file);
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	unsigned long index = iocb->ki_pos >> huge_page_shift(h);
	unsigned long offset = iocb->ki_pos & ~huge_page_mask(h);
	unsigned long end_index;
	loff_t isize;
	ssize_t retval = 0;

	mutex_lock(&inode->i_mutex);

	/* validate length */
	if (len == 0)
		goto out;

	isize = i_size_read(inode);
	if (!isize)
		goto out;

	end_index = (isize - 1) >> huge_page_shift(h);
	for (;;) {
		struct page *page;
		unsigned long nr, ret;

		/* nr is the maximum number of bytes to copy from this page */
		nr = huge_page_size(h);
		if (index >= end_index) {
			if (index > end_index)
				goto out;
			nr = ((isize - 1) & ~huge_page_mask(h)) + 1;
			if (nr <= offset) {
				goto out;
			}
	while (iov_iter_count(to)) {
		struct page *page;
		size_t nr, copied;

		/* nr is the maximum number of bytes to copy from this page */
		nr = huge_page_size(h);
		isize = i_size_read(inode);
		if (!isize)
			break;
		end_index = (isize - 1) >> huge_page_shift(h);
		if (index > end_index)
			break;
		if (index == end_index) {
			nr = ((isize - 1) & ~huge_page_mask(h)) + 1;
			if (nr <= offset)
				break;
		}
		nr = nr - offset;

		/* Find the page */
		page = find_get_page(mapping, index);
		page = find_lock_page(mapping, index);
		if (unlikely(page == NULL)) {
			/*
			 * We have a HOLE, zero out the user-buffer for the
			 * length of the hole or request.
			 */
			ret = len < nr ? len : nr;
			if (clear_user(buf, ret))
				ret = -EFAULT;
		} else {
			/*
			 * We have the page, copy it to user space buffer.
			 */
			ret = hugetlbfs_read_actor(page, offset, buf, len, nr);
		}
		if (ret < 0) {
			if (retval == 0)
				retval = ret;
			if (page)
				page_cache_release(page);
			goto out;
		}

		offset += ret;
		retval += ret;
		len -= ret;
		index += offset >> huge_page_shift(h);
		offset &= ~huge_page_mask(h);

		if (page)
			page_cache_release(page);

		/* short read or no more work */
		if ((ret != nr) || (len == 0))
			break;
	}
out:
	*ppos = ((loff_t)index << huge_page_shift(h)) + offset;
	mutex_unlock(&inode->i_mutex);
	return retval;
}

/*
 * Read a page. Again trivial. If it didn't already exist
 * in the page cache, it is zero-filled.
 */
static int hugetlbfs_readpage(struct file *file, struct page * page)
{
	unlock_page(page);
	return -EINVAL;
}

			copied = iov_iter_zero(nr, to);
		} else {
			unlock_page(page);

			/*
			 * We have the page, copy it to user space buffer.
			 */
			copied = hugetlbfs_read_actor(page, offset, to, nr);
			page_cache_release(page);
		}
		offset += copied;
		retval += copied;
		if (copied != nr && iov_iter_count(to)) {
			if (!retval)
				retval = -EFAULT;
			break;
		}
		index += offset >> huge_page_shift(h);
		offset &= ~huge_page_mask(h);
	}
	iocb->ki_pos = ((loff_t)index << huge_page_shift(h)) + offset;
	return retval;
}

static int hugetlbfs_write_begin(struct file *file,
			struct address_space *mapping,
			loff_t pos, unsigned len, unsigned flags,
			struct page **pagep, void **fsdata)
{
	return -EINVAL;
}

static int hugetlbfs_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata)
{
	BUG();
	return -EINVAL;
}

static void truncate_huge_page(struct page *page)
{
	cancel_dirty_page(page, /* No IO accounting for huge pages? */0);
	ClearPageUptodate(page);
	remove_from_page_cache(page);
	put_page(page);
}

static void truncate_hugepages(struct inode *inode, loff_t lstart)
static void remove_huge_page(struct page *page)
{
	ClearPageDirty(page);
	ClearPageUptodate(page);
	delete_from_page_cache(page);
}


/*
 * remove_inode_hugepages handles two distinct cases: truncation and hole
 * punch.  There are subtle differences in operation for each case.

 * truncation is indicated by end of range being LLONG_MAX
 *	In this case, we first scan the range and release found pages.
 *	After releasing pages, hugetlb_unreserve_pages cleans up region/reserv
 *	maps and global counts.  Page faults can not race with truncation
 *	in this routine.  hugetlb_no_page() prevents page faults in the
 *	truncated range.  It checks i_size before allocation, and again after
 *	with the page table lock for the page held.  The same lock must be
 *	acquired to unmap a page.
 * hole punch is indicated if end is not LLONG_MAX
 *	In the hole punch case we scan the range and release found pages.
 *	Only when releasing a page is the associated region/reserv map
 *	deleted.  The region/reserv map for ranges without associated
 *	pages are not modified.  Page faults can race with hole punch.
 *	This is indicated if we find a mapped page.
 * Note: If the passed end of range value is beyond the end of file, but
 * not LLONG_MAX this routine still performs a hole punch operation.
 */
static void remove_inode_hugepages(struct inode *inode, loff_t lstart,
				   loff_t lend)
{
	struct hstate *h = hstate_inode(inode);
	struct address_space *mapping = &inode->i_data;
	const pgoff_t start = lstart >> huge_page_shift(h);
	struct pagevec pvec;
	pgoff_t next;
	int i, freed = 0;

	pagevec_init(&pvec, 0);
	next = start;
	while (1) {
		if (!pagevec_lookup(&pvec, mapping, next, PAGEVEC_SIZE)) {
			if (next == start)
				break;
			next = start;
			continue;
		}

		for (i = 0; i < pagevec_count(&pvec); ++i) {
			struct page *page = pvec.pages[i];

			lock_page(page);
			if (page->index > next)
				next = page->index;
			++next;
			truncate_huge_page(page);
			unlock_page(page);
			freed++;
		}
		huge_pagevec_release(&pvec);
	}
	BUG_ON(!lstart && mapping->nrpages);
	hugetlb_unreserve_pages(inode, start, freed);
}

static void hugetlbfs_delete_inode(struct inode *inode)
{
	truncate_hugepages(inode, 0);
	clear_inode(inode);
}

static void hugetlbfs_forget_inode(struct inode *inode) __releases(inode_lock)
{
	struct super_block *sb = inode->i_sb;

	if (!hlist_unhashed(&inode->i_hash)) {
		if (!(inode->i_state & (I_DIRTY|I_SYNC)))
			list_move(&inode->i_list, &inode_unused);
		inodes_stat.nr_unused++;
		if (!sb || (sb->s_flags & MS_ACTIVE)) {
			spin_unlock(&inode_lock);
			return;
		}
		inode->i_state |= I_WILL_FREE;
		spin_unlock(&inode_lock);
		/*
		 * write_inode_now is a noop as we set BDI_CAP_NO_WRITEBACK
		 * in our backing_dev_info.
		 */
		write_inode_now(inode, 1);
		spin_lock(&inode_lock);
		inode->i_state &= ~I_WILL_FREE;
		inodes_stat.nr_unused--;
		hlist_del_init(&inode->i_hash);
	}
	list_del_init(&inode->i_list);
	list_del_init(&inode->i_sb_list);
	inode->i_state |= I_FREEING;
	inodes_stat.nr_inodes--;
	spin_unlock(&inode_lock);
	truncate_hugepages(inode, 0);
	clear_inode(inode);
	destroy_inode(inode);
}

static void hugetlbfs_drop_inode(struct inode *inode)
{
	if (!inode->i_nlink)
		generic_delete_inode(inode);
	else
		hugetlbfs_forget_inode(inode);
}

static inline void
hugetlb_vmtruncate_list(struct prio_tree_root *root, pgoff_t pgoff)
{
	struct vm_area_struct *vma;
	struct prio_tree_iter iter;

	vma_prio_tree_foreach(vma, &iter, root, pgoff, ULONG_MAX) {
	const pgoff_t end = lend >> huge_page_shift(h);
	struct vm_area_struct pseudo_vma;
	struct pagevec pvec;
	pgoff_t next;
	int i, freed = 0;
	long lookup_nr = PAGEVEC_SIZE;
	bool truncate_op = (lend == LLONG_MAX);

	memset(&pseudo_vma, 0, sizeof(struct vm_area_struct));
	pseudo_vma.vm_flags = (VM_HUGETLB | VM_MAYSHARE | VM_SHARED);
	pagevec_init(&pvec, 0);
	next = start;
	while (next < end) {
		/*
		 * Don't grab more pages than the number left in the range.
		 */
		if (end - next < lookup_nr)
			lookup_nr = end - next;

		/*
		 * When no more pages are found, we are done.
		 */
		if (!pagevec_lookup(&pvec, mapping, next, lookup_nr))
			break;

		for (i = 0; i < pagevec_count(&pvec); ++i) {
			struct page *page = pvec.pages[i];
			u32 hash;

			/*
			 * The page (index) could be beyond end.  This is
			 * only possible in the punch hole case as end is
			 * max page offset in the truncate case.
			 */
			next = page->index;
			if (next >= end)
				break;

			hash = hugetlb_fault_mutex_hash(h, mapping, next);
			mutex_lock(&hugetlb_fault_mutex_table[hash]);

			lock_page(page);
			if (likely(!page_mapped(page))) {
				bool rsv_on_error = !PagePrivate(page);
				/*
				 * We must free the huge page and remove
				 * from page cache (remove_huge_page) BEFORE
				 * removing the region/reserve map
				 * (hugetlb_unreserve_pages).  In rare out
				 * of memory conditions, removal of the
				 * region/reserve map could fail.  Before
				 * free'ing the page, note PagePrivate which
				 * is used in case of error.
				 */
				remove_huge_page(page);
				freed++;
				if (!truncate_op) {
					if (unlikely(hugetlb_unreserve_pages(
							inode, next,
							next + 1, 1)))
						hugetlb_fix_reserve_counts(
							inode, rsv_on_error);
				}
			} else {
				/*
				 * If page is mapped, it was faulted in after
				 * being unmapped.  It indicates a race between
				 * hole punch and page fault.  Do nothing in
				 * this case.  Getting here in a truncate
				 * operation is a bug.
				 */
				BUG_ON(truncate_op);
			}

			unlock_page(page);
			mutex_unlock(&hugetlb_fault_mutex_table[hash]);
		}
		++next;
		huge_pagevec_release(&pvec);
		cond_resched();
	}

	if (truncate_op)
		(void)hugetlb_unreserve_pages(inode, start, LONG_MAX, freed);
}

static void hugetlbfs_evict_inode(struct inode *inode)
{
	struct resv_map *resv_map;

	remove_inode_hugepages(inode, 0, LLONG_MAX);
	resv_map = (struct resv_map *)inode->i_mapping->private_data;
	/* root inode doesn't have the resv_map, so we should check it */
	if (resv_map)
		resv_map_release(&resv_map->refs);
	clear_inode(inode);
}

static inline void
hugetlb_vmdelete_list(struct rb_root *root, pgoff_t start, pgoff_t end)
{
	struct vm_area_struct *vma;

	/*
	 * end == 0 indicates that the entire range after
	 * start should be unmapped.
	 */
	vma_interval_tree_foreach(vma, root, start, end ? end : ULONG_MAX) {
		unsigned long v_offset;
		unsigned long v_end;

		/*
		 * Can the expression below overflow on 32-bit arches?
		 * No, because the prio_tree returns us only those vmas
		 * which overlap the truncated area starting at pgoff,
		 * and no vma on a 32-bit arch can span beyond the 4GB.
		 */
		if (vma->vm_pgoff < pgoff)
			v_offset = (pgoff - vma->vm_pgoff) << PAGE_SHIFT;
		else
			v_offset = 0;

		__unmap_hugepage_range(vma,
				vma->vm_start + v_offset, vma->vm_end, NULL);
		 * No, because the interval tree returns us only those vmas
		 * which overlap the truncated area starting at pgoff,
		 * and no vma on a 32-bit arch can span beyond the 4GB.
		 */
		if (vma->vm_pgoff < start)
			v_offset = (start - vma->vm_pgoff) << PAGE_SHIFT;
		else
			v_offset = 0;

		if (!end)
			v_end = vma->vm_end;
		else {
			v_end = ((end - vma->vm_pgoff) << PAGE_SHIFT)
							+ vma->vm_start;
			if (v_end > vma->vm_end)
				v_end = vma->vm_end;
		}

		unmap_hugepage_range(vma, vma->vm_start + v_offset, v_end,
									NULL);
	}
}

static int hugetlb_vmtruncate(struct inode *inode, loff_t offset)
{
	pgoff_t pgoff;
	struct address_space *mapping = inode->i_mapping;
	struct hstate *h = hstate_inode(inode);

	BUG_ON(offset & ~huge_page_mask(h));
	pgoff = offset >> PAGE_SHIFT;

	i_size_write(inode, offset);
	spin_lock(&mapping->i_mmap_lock);
	if (!prio_tree_empty(&mapping->i_mmap))
		hugetlb_vmtruncate_list(&mapping->i_mmap, pgoff);
	spin_unlock(&mapping->i_mmap_lock);
	truncate_hugepages(inode, offset);
	return 0;
}

static int hugetlbfs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	i_mmap_lock_write(mapping);
	if (!RB_EMPTY_ROOT(&mapping->i_mmap))
		hugetlb_vmdelete_list(&mapping->i_mmap, pgoff, 0);
	i_mmap_unlock_write(mapping);
	remove_inode_hugepages(inode, offset, LLONG_MAX);
	return 0;
}

static long hugetlbfs_punch_hole(struct inode *inode, loff_t offset, loff_t len)
{
	struct hstate *h = hstate_inode(inode);
	loff_t hpage_size = huge_page_size(h);
	loff_t hole_start, hole_end;

	/*
	 * For hole punch round up the beginning offset of the hole and
	 * round down the end.
	 */
	hole_start = round_up(offset, hpage_size);
	hole_end = round_down(offset + len, hpage_size);

	if (hole_end > hole_start) {
		struct address_space *mapping = inode->i_mapping;

		mutex_lock(&inode->i_mutex);
		i_mmap_lock_write(mapping);
		if (!RB_EMPTY_ROOT(&mapping->i_mmap))
			hugetlb_vmdelete_list(&mapping->i_mmap,
						hole_start >> PAGE_SHIFT,
						hole_end  >> PAGE_SHIFT);
		i_mmap_unlock_write(mapping);
		remove_inode_hugepages(inode, hole_start, hole_end);
		mutex_unlock(&inode->i_mutex);
	}

	return 0;
}

static long hugetlbfs_fallocate(struct file *file, int mode, loff_t offset,
				loff_t len)
{
	struct inode *inode = file_inode(file);
	struct address_space *mapping = inode->i_mapping;
	struct hstate *h = hstate_inode(inode);
	struct vm_area_struct pseudo_vma;
	loff_t hpage_size = huge_page_size(h);
	unsigned long hpage_shift = huge_page_shift(h);
	pgoff_t start, index, end;
	int error;
	u32 hash;

	if (mode & ~(FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE))
		return -EOPNOTSUPP;

	if (mode & FALLOC_FL_PUNCH_HOLE)
		return hugetlbfs_punch_hole(inode, offset, len);

	/*
	 * Default preallocate case.
	 * For this range, start is rounded down and end is rounded up
	 * as well as being converted to page offsets.
	 */
	start = offset >> hpage_shift;
	end = (offset + len + hpage_size - 1) >> hpage_shift;

	mutex_lock(&inode->i_mutex);

	/* We need to check rlimit even when FALLOC_FL_KEEP_SIZE */
	error = inode_newsize_ok(inode, offset + len);
	if (error)
		goto out;

	/*
	 * Initialize a pseudo vma as this is required by the huge page
	 * allocation routines.  If NUMA is configured, use page index
	 * as input to create an allocation policy.
	 */
	memset(&pseudo_vma, 0, sizeof(struct vm_area_struct));
	pseudo_vma.vm_flags = (VM_HUGETLB | VM_MAYSHARE | VM_SHARED);
	pseudo_vma.vm_file = file;

	for (index = start; index < end; index++) {
		/*
		 * This is supposed to be the vaddr where the page is being
		 * faulted in, but we have no vaddr here.
		 */
		struct page *page;
		unsigned long addr;
		int avoid_reserve = 0;

		cond_resched();

		/*
		 * fallocate(2) manpage permits EINTR; we may have been
		 * interrupted because we are using up too much memory.
		 */
		if (signal_pending(current)) {
			error = -EINTR;
			break;
		}

		/* Set numa allocation policy based on index */
		hugetlb_set_vma_policy(&pseudo_vma, inode, index);

		/* addr is the offset within the file (zero based) */
		addr = index * hpage_size;

		/* mutex taken here, fault path and hole punch */
		hash = hugetlb_fault_mutex_hash(h, mapping, index);
		mutex_lock(&hugetlb_fault_mutex_table[hash]);

		/* See if already present in mapping to avoid alloc/free */
		page = find_get_page(mapping, index);
		if (page) {
			put_page(page);
			mutex_unlock(&hugetlb_fault_mutex_table[hash]);
			hugetlb_drop_vma_policy(&pseudo_vma);
			continue;
		}

		/* Allocate page and add to page cache */
		page = alloc_huge_page(&pseudo_vma, addr, avoid_reserve);
		hugetlb_drop_vma_policy(&pseudo_vma);
		if (IS_ERR(page)) {
			mutex_unlock(&hugetlb_fault_mutex_table[hash]);
			error = PTR_ERR(page);
			goto out;
		}
		clear_huge_page(page, addr, pages_per_huge_page(h));
		__SetPageUptodate(page);
		error = huge_add_to_page_cache(page, mapping, index);
		if (unlikely(error)) {
			put_page(page);
			mutex_unlock(&hugetlb_fault_mutex_table[hash]);
			goto out;
		}

		mutex_unlock(&hugetlb_fault_mutex_table[hash]);

		set_page_huge_active(page);
		/*
		 * put_page() due to reference from alloc_huge_page()
		 * unlock_page because locked by add_to_page_cache()
		 */
		put_page(page);
		unlock_page(page);
	}

	if (!(mode & FALLOC_FL_KEEP_SIZE) && offset + len > inode->i_size)
		i_size_write(inode, offset + len);
	inode->i_ctime = CURRENT_TIME;
out:
	mutex_unlock(&inode->i_mutex);
	return error;
}

static int hugetlbfs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = d_inode(dentry);
	struct hstate *h = hstate_inode(inode);
	int error;
	unsigned int ia_valid = attr->ia_valid;

	BUG_ON(!inode);

	error = inode_change_ok(inode, attr);
	if (error)
		goto out;

	if (ia_valid & ATTR_SIZE) {
		error = -EINVAL;
		if (!(attr->ia_size & ~huge_page_mask(h)))
			error = hugetlb_vmtruncate(inode, attr->ia_size);
		if (error)
			goto out;
		attr->ia_valid &= ~ATTR_SIZE;
	}
	error = inode_setattr(inode, attr);
out:
	return error;
}

static struct inode *hugetlbfs_get_inode(struct super_block *sb, uid_t uid, 
					gid_t gid, int mode, dev_t dev)
		return error;

	if (ia_valid & ATTR_SIZE) {
		error = -EINVAL;
		if (attr->ia_size & ~huge_page_mask(h))
			return -EINVAL;
		error = hugetlb_vmtruncate(inode, attr->ia_size);
		if (error)
			return error;
	}

	setattr_copy(inode, attr);
	mark_inode_dirty(inode);
	return 0;
}

static struct inode *hugetlbfs_get_root(struct super_block *sb,
					struct hugetlbfs_config *config)
{
	struct inode *inode;

	inode = new_inode(sb);
	if (inode) {
		struct hugetlbfs_inode_info *info;
		inode->i_mode = mode;
		inode->i_uid = uid;
		inode->i_gid = gid;
		inode->i_blocks = 0;
		inode->i_mapping->a_ops = &hugetlbfs_aops;
		inode->i_mapping->backing_dev_info =&hugetlbfs_backing_dev_info;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		INIT_LIST_HEAD(&inode->i_mapping->private_list);
		info = HUGETLBFS_I(inode);
		mpol_shared_policy_init(&info->policy, NULL);
		inode->i_ino = get_next_ino();
		inode->i_mode = S_IFDIR | config->mode;
		inode->i_uid = config->uid;
		inode->i_gid = config->gid;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		info = HUGETLBFS_I(inode);
		mpol_shared_policy_init(&info->policy, NULL);
		inode->i_op = &hugetlbfs_dir_inode_operations;
		inode->i_fop = &simple_dir_operations;
		/* directory inodes start off with i_nlink == 2 (for "." entry) */
		inc_nlink(inode);
		lockdep_annotate_inode_mutex_key(inode);
	}
	return inode;
}

/*
 * Hugetlbfs is not reclaimable; therefore its i_mmap_rwsem will never
 * be taken from reclaim -- unlike regular filesystems. This needs an
 * annotation because huge_pmd_share() does an allocation under
 * i_mmap_rwsem.
 */
static struct lock_class_key hugetlbfs_i_mmap_rwsem_key;

static struct inode *hugetlbfs_get_inode(struct super_block *sb,
					struct inode *dir,
					umode_t mode, dev_t dev)
{
	struct inode *inode;
	struct resv_map *resv_map = NULL;

	/*
	 * Reserve maps are only needed for inodes that can have associated
	 * page allocations.
	 */
	if (S_ISREG(mode) || S_ISLNK(mode)) {
		resv_map = resv_map_alloc();
		if (!resv_map)
			return NULL;
	}

	inode = new_inode(sb);
	if (inode) {
		struct hugetlbfs_inode_info *info;
		inode->i_ino = get_next_ino();
		inode_init_owner(inode, dir, mode);
		lockdep_set_class(&inode->i_mapping->i_mmap_rwsem,
				&hugetlbfs_i_mmap_rwsem_key);
		inode->i_mapping->a_ops = &hugetlbfs_aops;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		inode->i_mapping->private_data = resv_map;
		info = HUGETLBFS_I(inode);
		/*
		 * The policy is initialized here even if we are creating a
		 * private inode because initialization simply creates an
		 * an empty rb tree and calls spin_lock_init(), later when we
		 * call mpol_free_shared_policy() it will just return because
		 * the rb tree will still be empty.
		 */
		mpol_shared_policy_init(&info->policy, NULL);
		switch (mode & S_IFMT) {
		default:
			init_special_inode(inode, mode, dev);
			break;
		case S_IFREG:
			inode->i_op = &hugetlbfs_inode_operations;
			inode->i_fop = &hugetlbfs_file_operations;
			break;
		case S_IFDIR:
			inode->i_op = &hugetlbfs_dir_inode_operations;
			inode->i_fop = &simple_dir_operations;

			/* directory inodes start off with i_nlink == 2 (for "." entry) */
			inc_nlink(inode);
			break;
		case S_IFLNK:
			inode->i_op = &page_symlink_inode_operations;
			break;
		}
	}
		lockdep_annotate_inode_mutex_key(inode);
	} else {
		if (resv_map)
			kref_put(&resv_map->refs, resv_map_release);
	}

	return inode;
}

/*
 * File creation. Allocate an inode, and we're done..
 */
static int hugetlbfs_mknod(struct inode *dir,
			struct dentry *dentry, int mode, dev_t dev)
{
	struct inode *inode;
	int error = -ENOSPC;
	gid_t gid;

	if (dir->i_mode & S_ISGID) {
		gid = dir->i_gid;
		if (S_ISDIR(mode))
			mode |= S_ISGID;
	} else {
		gid = current->fsgid;
	}
	inode = hugetlbfs_get_inode(dir->i_sb, current->fsuid, gid, mode, dev);
			struct dentry *dentry, umode_t mode, dev_t dev)
{
	struct inode *inode;
	int error = -ENOSPC;

	inode = hugetlbfs_get_inode(dir->i_sb, dir, mode, dev);
	if (inode) {
		dir->i_ctime = dir->i_mtime = CURRENT_TIME;
		d_instantiate(dentry, inode);
		dget(dentry);	/* Extra count - pin the dentry in core */
		error = 0;
	}
	return error;
}

static int hugetlbfs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
static int hugetlbfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	int retval = hugetlbfs_mknod(dir, dentry, mode | S_IFDIR, 0);
	if (!retval)
		inc_nlink(dir);
	return retval;
}

static int hugetlbfs_create(struct inode *dir, struct dentry *dentry, int mode, struct nameidata *nd)
static int hugetlbfs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
	return hugetlbfs_mknod(dir, dentry, mode | S_IFREG, 0);
}

static int hugetlbfs_symlink(struct inode *dir,
			struct dentry *dentry, const char *symname)
{
	struct inode *inode;
	int error = -ENOSPC;
	gid_t gid;

	if (dir->i_mode & S_ISGID)
		gid = dir->i_gid;
	else
		gid = current->fsgid;

	inode = hugetlbfs_get_inode(dir->i_sb, current->fsuid,
					gid, S_IFLNK|S_IRWXUGO, 0);

	inode = hugetlbfs_get_inode(dir->i_sb, dir, S_IFLNK|S_IRWXUGO, 0);
	if (inode) {
		int l = strlen(symname)+1;
		error = page_symlink(inode, symname, l);
		if (!error) {
			d_instantiate(dentry, inode);
			dget(dentry);
		} else
			iput(inode);
	}
	dir->i_ctime = dir->i_mtime = CURRENT_TIME;

	return error;
}

/*
 * mark the head page dirty
 */
static int hugetlbfs_set_page_dirty(struct page *page)
{
	struct page *head = compound_head(page);

	SetPageDirty(head);
	return 0;
}

static int hugetlbfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct hugetlbfs_sb_info *sbinfo = HUGETLBFS_SB(dentry->d_sb);
	struct hstate *h = hstate_inode(dentry->d_inode);
static int hugetlbfs_migrate_page(struct address_space *mapping,
				struct page *newpage, struct page *page,
				enum migrate_mode mode)
{
	int rc;

	rc = migrate_huge_page_move_mapping(mapping, newpage, page);
	if (rc != MIGRATEPAGE_SUCCESS)
		return rc;

	/*
	 * page_private is subpool pointer in hugetlb pages.  Transfer to
	 * new page.  PagePrivate is not associated with page_private for
	 * hugetlb pages and can not be set here as only page_huge_active
	 * pages can be migrated.
	 */
	if (page_private(page)) {
		set_page_private(newpage, page_private(page));
		set_page_private(page, 0);
	}

	migrate_page_copy(newpage, page);

	return MIGRATEPAGE_SUCCESS;
}

static int hugetlbfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct hugetlbfs_sb_info *sbinfo = HUGETLBFS_SB(dentry->d_sb);
	struct hstate *h = hstate_inode(d_inode(dentry));

	buf->f_type = HUGETLBFS_MAGIC;
	buf->f_bsize = huge_page_size(h);
	if (sbinfo) {
		spin_lock(&sbinfo->stat_lock);
		/* If no limits set, just report 0 for max/free/used
		 * blocks, like simple_statfs() */
		if (sbinfo->max_blocks >= 0) {
			buf->f_blocks = sbinfo->max_blocks;
			buf->f_bavail = buf->f_bfree = sbinfo->free_blocks;
		if (sbinfo->spool) {
			long free_pages;

			spin_lock(&sbinfo->spool->lock);
			buf->f_blocks = sbinfo->spool->max_hpages;
			free_pages = sbinfo->spool->max_hpages
				- sbinfo->spool->used_hpages;
			buf->f_bavail = buf->f_bfree = free_pages;
			spin_unlock(&sbinfo->spool->lock);
			buf->f_files = sbinfo->max_inodes;
			buf->f_ffree = sbinfo->free_inodes;
		}
		spin_unlock(&sbinfo->stat_lock);
	}
	buf->f_namelen = NAME_MAX;
	return 0;
}

static void hugetlbfs_put_super(struct super_block *sb)
{
	struct hugetlbfs_sb_info *sbi = HUGETLBFS_SB(sb);

	if (sbi) {
		sb->s_fs_info = NULL;

		if (sbi->spool)
			hugepage_put_subpool(sbi->spool);

		kfree(sbi);
	}
}

static inline int hugetlbfs_dec_free_inodes(struct hugetlbfs_sb_info *sbinfo)
{
	if (sbinfo->free_inodes >= 0) {
		spin_lock(&sbinfo->stat_lock);
		if (unlikely(!sbinfo->free_inodes)) {
			spin_unlock(&sbinfo->stat_lock);
			return 0;
		}
		sbinfo->free_inodes--;
		spin_unlock(&sbinfo->stat_lock);
	}

	return 1;
}

static void hugetlbfs_inc_free_inodes(struct hugetlbfs_sb_info *sbinfo)
{
	if (sbinfo->free_inodes >= 0) {
		spin_lock(&sbinfo->stat_lock);
		sbinfo->free_inodes++;
		spin_unlock(&sbinfo->stat_lock);
	}
}


static struct kmem_cache *hugetlbfs_inode_cachep;

static struct inode *hugetlbfs_alloc_inode(struct super_block *sb)
{
	struct hugetlbfs_sb_info *sbinfo = HUGETLBFS_SB(sb);
	struct hugetlbfs_inode_info *p;

	if (unlikely(!hugetlbfs_dec_free_inodes(sbinfo)))
		return NULL;
	p = kmem_cache_alloc(hugetlbfs_inode_cachep, GFP_KERNEL);
	if (unlikely(!p)) {
		hugetlbfs_inc_free_inodes(sbinfo);
		return NULL;
	}
	return &p->vfs_inode;
}

static void hugetlbfs_i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	kmem_cache_free(hugetlbfs_inode_cachep, HUGETLBFS_I(inode));
}

static void hugetlbfs_destroy_inode(struct inode *inode)
{
	hugetlbfs_inc_free_inodes(HUGETLBFS_SB(inode->i_sb));
	mpol_free_shared_policy(&HUGETLBFS_I(inode)->policy);
	kmem_cache_free(hugetlbfs_inode_cachep, HUGETLBFS_I(inode));
}

static const struct address_space_operations hugetlbfs_aops = {
	.readpage	= hugetlbfs_readpage,
	.write_begin	= hugetlbfs_write_begin,
	.write_end	= hugetlbfs_write_end,
	.set_page_dirty	= hugetlbfs_set_page_dirty,
	call_rcu(&inode->i_rcu, hugetlbfs_i_callback);
}

static const struct address_space_operations hugetlbfs_aops = {
	.write_begin	= hugetlbfs_write_begin,
	.write_end	= hugetlbfs_write_end,
	.set_page_dirty	= hugetlbfs_set_page_dirty,
	.migratepage    = hugetlbfs_migrate_page,
};


static void init_once(void *foo)
{
	struct hugetlbfs_inode_info *ei = (struct hugetlbfs_inode_info *)foo;

	inode_init_once(&ei->vfs_inode);
}

const struct file_operations hugetlbfs_file_operations = {
	.read			= hugetlbfs_read,
	.mmap			= hugetlbfs_file_mmap,
	.fsync			= simple_sync_file,
	.get_unmapped_area	= hugetlb_get_unmapped_area,
	.read_iter		= hugetlbfs_read_iter,
	.mmap			= hugetlbfs_file_mmap,
	.fsync			= noop_fsync,
	.get_unmapped_area	= hugetlb_get_unmapped_area,
	.llseek			= default_llseek,
	.fallocate		= hugetlbfs_fallocate,
};

static const struct inode_operations hugetlbfs_dir_inode_operations = {
	.create		= hugetlbfs_create,
	.lookup		= simple_lookup,
	.link		= simple_link,
	.unlink		= simple_unlink,
	.symlink	= hugetlbfs_symlink,
	.mkdir		= hugetlbfs_mkdir,
	.rmdir		= simple_rmdir,
	.mknod		= hugetlbfs_mknod,
	.rename		= simple_rename,
	.setattr	= hugetlbfs_setattr,
};

static const struct inode_operations hugetlbfs_inode_operations = {
	.setattr	= hugetlbfs_setattr,
};

static const struct super_operations hugetlbfs_ops = {
	.alloc_inode    = hugetlbfs_alloc_inode,
	.destroy_inode  = hugetlbfs_destroy_inode,
	.statfs		= hugetlbfs_statfs,
	.delete_inode	= hugetlbfs_delete_inode,
	.drop_inode	= hugetlbfs_drop_inode,
	.evict_inode	= hugetlbfs_evict_inode,
	.statfs		= hugetlbfs_statfs,
	.put_super	= hugetlbfs_put_super,
	.show_options	= generic_show_options,
};

enum { NO_SIZE, SIZE_STD, SIZE_PERCENT };

/*
 * Convert size option passed from command line to number of huge pages
 * in the pool specified by hstate.  Size option could be in bytes
 * (val_type == SIZE_STD) or percentage of the pool (val_type == SIZE_PERCENT).
 */
static long long
hugetlbfs_size_to_hpages(struct hstate *h, unsigned long long size_opt,
								int val_type)
{
	if (val_type == NO_SIZE)
		return -1;

	if (val_type == SIZE_PERCENT) {
		size_opt <<= huge_page_shift(h);
		size_opt *= h->max_huge_pages;
		do_div(size_opt, 100);
	}

	size_opt >>= huge_page_shift(h);
	return size_opt;
}

static int
hugetlbfs_parse_options(char *options, struct hugetlbfs_config *pconfig)
{
	char *p, *rest;
	substring_t args[MAX_OPT_ARGS];
	int option;
	unsigned long long size = 0;
	enum { NO_SIZE, SIZE_STD, SIZE_PERCENT } setsize = NO_SIZE;
	unsigned long long max_size_opt = 0, min_size_opt = 0;
	int max_val_type = NO_SIZE, min_val_type = NO_SIZE;

	if (!options)
		return 0;

	while ((p = strsep(&options, ",")) != NULL) {
		int token;
		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_uid:
			if (match_int(&args[0], &option))
 				goto bad_val;
			pconfig->uid = option;
			pconfig->uid = make_kuid(current_user_ns(), option);
			if (!uid_valid(pconfig->uid))
				goto bad_val;
			break;

		case Opt_gid:
			if (match_int(&args[0], &option))
 				goto bad_val;
			pconfig->gid = option;
			pconfig->gid = make_kgid(current_user_ns(), option);
			if (!gid_valid(pconfig->gid))
				goto bad_val;
			break;

		case Opt_mode:
			if (match_octal(&args[0], &option))
 				goto bad_val;
			pconfig->mode = option & 01777U;
			break;

		case Opt_size: {
			/* memparse() will accept a K/M/G without a digit */
			if (!isdigit(*args[0].from))
				goto bad_val;
			size = memparse(args[0].from, &rest);
			setsize = SIZE_STD;
			if (*rest == '%')
				setsize = SIZE_PERCENT;
			max_size_opt = memparse(args[0].from, &rest);
			max_val_type = SIZE_STD;
			if (*rest == '%')
				max_val_type = SIZE_PERCENT;
			break;
		}

		case Opt_nr_inodes:
			/* memparse() will accept a K/M/G without a digit */
			if (!isdigit(*args[0].from))
				goto bad_val;
			pconfig->nr_inodes = memparse(args[0].from, &rest);
			break;

		case Opt_pagesize: {
			unsigned long ps;
			ps = memparse(args[0].from, &rest);
			pconfig->hstate = size_to_hstate(ps);
			if (!pconfig->hstate) {
				printk(KERN_ERR
				"hugetlbfs: Unsupported page size %lu MB\n",
				pr_err("Unsupported page size %lu MB\n",
					ps >> 20);
				return -EINVAL;
			}
			break;
		}

		default:
			printk(KERN_ERR "hugetlbfs: Bad mount option: \"%s\"\n",
				 p);
		case Opt_min_size: {
			/* memparse() will accept a K/M/G without a digit */
			if (!isdigit(*args[0].from))
				goto bad_val;
			min_size_opt = memparse(args[0].from, &rest);
			min_val_type = SIZE_STD;
			if (*rest == '%')
				min_val_type = SIZE_PERCENT;
			break;
		}

		default:
			pr_err("Bad mount option: \"%s\"\n", p);
			return -EINVAL;
			break;
		}
	}

	/* Do size after hstate is set up */
	if (setsize > NO_SIZE) {
		struct hstate *h = pconfig->hstate;
		if (setsize == SIZE_PERCENT) {
			size <<= huge_page_shift(h);
			size *= h->max_huge_pages;
			do_div(size, 100);
		}
		pconfig->nr_blocks = (size >> huge_page_shift(h));
	/*
	 * Use huge page pool size (in hstate) to convert the size
	 * options to number of huge pages.  If NO_SIZE, -1 is returned.
	 */
	pconfig->max_hpages = hugetlbfs_size_to_hpages(pconfig->hstate,
						max_size_opt, max_val_type);
	pconfig->min_hpages = hugetlbfs_size_to_hpages(pconfig->hstate,
						min_size_opt, min_val_type);

	/*
	 * If max_size was specified, then min_size must be smaller
	 */
	if (max_val_type > NO_SIZE &&
	    pconfig->min_hpages > pconfig->max_hpages) {
		pr_err("minimum size can not be greater than maximum size\n");
		return -EINVAL;
	}

	return 0;

bad_val:
 	printk(KERN_ERR "hugetlbfs: Bad value '%s' for mount option '%s'\n",
	       args[0].from, p);
 	return 1;
	pr_err("Bad value '%s' for mount option '%s'\n", args[0].from, p);
 	return -EINVAL;
}

static int
hugetlbfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode * inode;
	struct dentry * root;
	int ret;
	struct hugetlbfs_config config;
	struct hugetlbfs_sb_info *sbinfo;

	save_mount_options(sb, data);

	config.nr_blocks = -1; /* No limit on size by default */
	config.nr_inodes = -1; /* No limit on number of inodes by default */
	config.uid = current->fsuid;
	config.gid = current->fsgid;
	config.mode = 0755;
	config.hstate = &default_hstate;
	config.max_hpages = -1; /* No limit on size by default */
	config.nr_inodes = -1; /* No limit on number of inodes by default */
	config.uid = current_fsuid();
	config.gid = current_fsgid();
	config.mode = 0755;
	config.hstate = &default_hstate;
	config.min_hpages = -1; /* No default minimum size */
	ret = hugetlbfs_parse_options(data, &config);
	if (ret)
		return ret;

	sbinfo = kmalloc(sizeof(struct hugetlbfs_sb_info), GFP_KERNEL);
	if (!sbinfo)
		return -ENOMEM;
	sb->s_fs_info = sbinfo;
	sbinfo->hstate = config.hstate;
	spin_lock_init(&sbinfo->stat_lock);
	sbinfo->max_blocks = config.nr_blocks;
	sbinfo->free_blocks = config.nr_blocks;
	sbinfo->max_inodes = config.nr_inodes;
	sbinfo->free_inodes = config.nr_inodes;
	sbinfo->max_inodes = config.nr_inodes;
	sbinfo->free_inodes = config.nr_inodes;
	sbinfo->spool = NULL;
	/*
	 * Allocate and initialize subpool if maximum or minimum size is
	 * specified.  Any needed reservations (for minimim size) are taken
	 * taken when the subpool is created.
	 */
	if (config.max_hpages != -1 || config.min_hpages != -1) {
		sbinfo->spool = hugepage_new_subpool(config.hstate,
							config.max_hpages,
							config.min_hpages);
		if (!sbinfo->spool)
			goto out_free;
	}
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_blocksize = huge_page_size(config.hstate);
	sb->s_blocksize_bits = huge_page_shift(config.hstate);
	sb->s_magic = HUGETLBFS_MAGIC;
	sb->s_op = &hugetlbfs_ops;
	sb->s_time_gran = 1;
	inode = hugetlbfs_get_inode(sb, config.uid, config.gid,
					S_IFDIR | config.mode, 0);
	if (!inode)
		goto out_free;

	root = d_alloc_root(inode);
	if (!root) {
		iput(inode);
		goto out_free;
	}
	sb->s_root = root;
	return 0;
out_free:
	sb->s_root = d_make_root(hugetlbfs_get_root(sb, &config));
	if (!sb->s_root)
		goto out_free;
	return 0;
out_free:
	kfree(sbinfo->spool);
	kfree(sbinfo);
	return -ENOMEM;
}

int hugetlb_get_quota(struct address_space *mapping, long delta)
{
	int ret = 0;
	struct hugetlbfs_sb_info *sbinfo = HUGETLBFS_SB(mapping->host->i_sb);

	if (sbinfo->free_blocks > -1) {
		spin_lock(&sbinfo->stat_lock);
		if (sbinfo->free_blocks - delta >= 0)
			sbinfo->free_blocks -= delta;
		else
			ret = -ENOMEM;
		spin_unlock(&sbinfo->stat_lock);
	}

	return ret;
}

void hugetlb_put_quota(struct address_space *mapping, long delta)
{
	struct hugetlbfs_sb_info *sbinfo = HUGETLBFS_SB(mapping->host->i_sb);

	if (sbinfo->free_blocks > -1) {
		spin_lock(&sbinfo->stat_lock);
		sbinfo->free_blocks += delta;
		spin_unlock(&sbinfo->stat_lock);
	}
}

static int hugetlbfs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
	return get_sb_nodev(fs_type, flags, data, hugetlbfs_fill_super, mnt);
static struct dentry *hugetlbfs_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return mount_nodev(fs_type, flags, data, hugetlbfs_fill_super);
}

static struct file_system_type hugetlbfs_fs_type = {
	.name		= "hugetlbfs",
	.get_sb		= hugetlbfs_get_sb,
	.kill_sb	= kill_litter_super,
};

static struct vfsmount *hugetlbfs_vfsmount;

static int can_do_hugetlb_shm(void)
{
	return likely(capable(CAP_IPC_LOCK) ||
			in_group_p(sysctl_hugetlb_shm_group) ||
			can_do_mlock());
}

struct file *hugetlb_file_setup(const char *name, size_t size)
{
	int error = -ENOMEM;
	struct file *file;
	struct inode *inode;
	struct dentry *dentry, *root;
	struct qstr quick_string;

	if (!hugetlbfs_vfsmount)
		return ERR_PTR(-ENOENT);

	if (!can_do_hugetlb_shm())
		return ERR_PTR(-EPERM);

	if (!user_shm_lock(size, current->user))
		return ERR_PTR(-ENOMEM);

	root = hugetlbfs_vfsmount->mnt_root;
	quick_string.name = name;
	quick_string.len = strlen(quick_string.name);
	quick_string.hash = 0;
	dentry = d_alloc(root, &quick_string);
	if (!dentry)
		goto out_shm_unlock;

	error = -ENOSPC;
	inode = hugetlbfs_get_inode(root->d_sb, current->fsuid,
				current->fsgid, S_IFREG | S_IRWXUGO, 0);
	if (!inode)
		goto out_dentry;

	error = -ENOMEM;
	if (hugetlb_reserve_pages(inode, 0,
			size >> huge_page_shift(hstate_inode(inode)), NULL))
		goto out_inode;

	d_instantiate(dentry, inode);
	inode->i_size = size;
	inode->i_nlink = 0;

	error = -ENFILE;
	file = alloc_file(hugetlbfs_vfsmount, dentry,
			FMODE_WRITE | FMODE_READ,
			&hugetlbfs_file_operations);
	if (!file)
	.mount		= hugetlbfs_mount,
	.kill_sb	= kill_litter_super,
};
MODULE_ALIAS_FS("hugetlbfs");

static struct vfsmount *hugetlbfs_vfsmount[HUGE_MAX_HSTATE];

static int can_do_hugetlb_shm(void)
{
	kgid_t shm_group;
	shm_group = make_kgid(&init_user_ns, sysctl_hugetlb_shm_group);
	return capable(CAP_IPC_LOCK) || in_group_p(shm_group);
}

static int get_hstate_idx(int page_size_log)
{
	struct hstate *h = hstate_sizelog(page_size_log);

	if (!h)
		return -1;
	return h - hstates;
}

static const struct dentry_operations anon_ops = {
	.d_dname = simple_dname
};

/*
 * Note that size should be aligned to proper hugepage size in caller side,
 * otherwise hugetlb_reserve_pages reserves one less hugepages than intended.
 */
struct file *hugetlb_file_setup(const char *name, size_t size,
				vm_flags_t acctflag, struct user_struct **user,
				int creat_flags, int page_size_log)
{
	struct file *file = ERR_PTR(-ENOMEM);
	struct inode *inode;
	struct path path;
	struct super_block *sb;
	struct qstr quick_string;
	int hstate_idx;

	hstate_idx = get_hstate_idx(page_size_log);
	if (hstate_idx < 0)
		return ERR_PTR(-ENODEV);

	*user = NULL;
	if (!hugetlbfs_vfsmount[hstate_idx])
		return ERR_PTR(-ENOENT);

	if (creat_flags == HUGETLB_SHMFS_INODE && !can_do_hugetlb_shm()) {
		*user = current_user();
		if (user_shm_lock(size, *user)) {
			task_lock(current);
			pr_warn_once("%s (%d): Using mlock ulimits for SHM_HUGETLB is deprecated\n",
				current->comm, current->pid);
			task_unlock(current);
		} else {
			*user = NULL;
			return ERR_PTR(-EPERM);
		}
	}

	sb = hugetlbfs_vfsmount[hstate_idx]->mnt_sb;
	quick_string.name = name;
	quick_string.len = strlen(quick_string.name);
	quick_string.hash = 0;
	path.dentry = d_alloc_pseudo(sb, &quick_string);
	if (!path.dentry)
		goto out_shm_unlock;

	d_set_d_op(path.dentry, &anon_ops);
	path.mnt = mntget(hugetlbfs_vfsmount[hstate_idx]);
	file = ERR_PTR(-ENOSPC);
	inode = hugetlbfs_get_inode(sb, NULL, S_IFREG | S_IRWXUGO, 0);
	if (!inode)
		goto out_dentry;
	if (creat_flags == HUGETLB_SHMFS_INODE)
		inode->i_flags |= S_PRIVATE;

	file = ERR_PTR(-ENOMEM);
	if (hugetlb_reserve_pages(inode, 0,
			size >> huge_page_shift(hstate_inode(inode)), NULL,
			acctflag))
		goto out_inode;

	d_instantiate(path.dentry, inode);
	inode->i_size = size;
	clear_nlink(inode);

	file = alloc_file(&path, FMODE_WRITE | FMODE_READ,
			&hugetlbfs_file_operations);
	if (IS_ERR(file))
		goto out_dentry; /* inode is already attached */

	return file;

out_inode:
	iput(inode);
out_dentry:
	dput(dentry);
out_shm_unlock:
	user_shm_unlock(size, current->user);
	return ERR_PTR(error);
	path_put(&path);
out_shm_unlock:
	if (*user) {
		user_shm_unlock(size, *user);
		*user = NULL;
	}
	return file;
}

static int __init init_hugetlbfs_fs(void)
{
	int error;
	struct vfsmount *vfsmount;

	error = bdi_init(&hugetlbfs_backing_dev_info);
	if (error)
		return error;

	struct hstate *h;
	int error;
	int i;

	if (!hugepages_supported()) {
		pr_info("disabling because there are no supported hugepage sizes\n");
		return -ENOTSUPP;
	}

	error = -ENOMEM;
	hugetlbfs_inode_cachep = kmem_cache_create("hugetlbfs_inode_cache",
					sizeof(struct hugetlbfs_inode_info),
					0, 0, init_once);
	if (hugetlbfs_inode_cachep == NULL)
		goto out2;

	error = register_filesystem(&hugetlbfs_fs_type);
	if (error)
		goto out;

	vfsmount = kern_mount(&hugetlbfs_fs_type);

	if (!IS_ERR(vfsmount)) {
		hugetlbfs_vfsmount = vfsmount;
		return 0;
	}

	error = PTR_ERR(vfsmount);

 out:
	if (error)
		kmem_cache_destroy(hugetlbfs_inode_cachep);
 out2:
	bdi_destroy(&hugetlbfs_backing_dev_info);
	i = 0;
	for_each_hstate(h) {
		char buf[50];
		unsigned ps_kb = 1U << (h->order + PAGE_SHIFT - 10);

		snprintf(buf, sizeof(buf), "pagesize=%uK", ps_kb);
		hugetlbfs_vfsmount[i] = kern_mount_data(&hugetlbfs_fs_type,
							buf);

		if (IS_ERR(hugetlbfs_vfsmount[i])) {
			pr_err("Cannot mount internal hugetlbfs for "
				"page size %uK", ps_kb);
			error = PTR_ERR(hugetlbfs_vfsmount[i]);
			hugetlbfs_vfsmount[i] = NULL;
		}
		i++;
	}
	/* Non default hstates are optional */
	if (!IS_ERR_OR_NULL(hugetlbfs_vfsmount[default_hstate_idx]))
		return 0;

 out:
	kmem_cache_destroy(hugetlbfs_inode_cachep);
 out2:
	return error;
}

static void __exit exit_hugetlbfs_fs(void)
{
	kmem_cache_destroy(hugetlbfs_inode_cachep);
	unregister_filesystem(&hugetlbfs_fs_type);
	bdi_destroy(&hugetlbfs_backing_dev_info);
	struct hstate *h;
	int i;


	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(hugetlbfs_inode_cachep);
	i = 0;
	for_each_hstate(h)
		kern_unmount(hugetlbfs_vfsmount[i++]);
	unregister_filesystem(&hugetlbfs_fs_type);
}

module_init(init_hugetlbfs_fs)
module_exit(exit_hugetlbfs_fs)

MODULE_LICENSE("GPL");
