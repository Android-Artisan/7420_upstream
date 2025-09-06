/*
 *  linux/mm/memory.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 */

/*
 * demand-loading started 01.12.91 - seems it is high on the list of
 * things wanted, and it should be easy to implement. - Linus
 */

/*
 * Ok, demand-loading was easy, shared pages a little bit tricker. Shared
 * pages started 02.12.91, seems to work. - Linus.
 *
 * Tested sharing by executing about 30 /bin/sh: under the old kernel it
 * would have taken more than the 6M I have free, but it worked well as
 * far as I could see.
 *
 * Also corrected some "invalidate()"s - I wasn't doing enough of them.
 */

/*
 * Real VM (paging to/from disk) started 18.12.91. Much more work and
 * thought has to go into this. Oh, well..
 * 19.12.91  -  works, somewhat. Sometimes I get faults, don't know why.
 *		Found it. Everything seems to work now.
 * 20.12.91  -  Ok, making the swap-device changeable like the root.
 */

/*
 * 05.04.94  -  Multi-page memory management added for v1.1.
 * 		Idea by Alex Bligh (alex@cconcepts.co.uk)
 *
 * 16.07.99  -  Support of BIGMEM added by Gerhard Wichert, Siemens AG
 *		(Gerhard.Wichert@pdb.siemens.de)
 *
 * Aug/Sep 2004 Changed to four level page tables (Andi Kleen)
 */

#include <linux/kernel_stat.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/mman.h>
#include <linux/swap.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/rmap.h>
#include <linux/module.h>
#include <linux/ksm.h>
#include <linux/rmap.h>
#include <linux/export.h>
#include <linux/delayacct.h>
#include <linux/init.h>
#include <linux/writeback.h>
#include <linux/memcontrol.h>
#include <linux/mmu_notifier.h>

#include <linux/kallsyms.h>
#include <linux/swapops.h>
#include <linux/elf.h>
#include <linux/gfp.h>
#include <linux/migrate.h>
#include <linux/string.h>
#include <linux/dma-debug.h>
#include <linux/debugfs.h>
#include <linux/userfaultfd_k.h>

#include <asm/io.h>
#include <asm/pgalloc.h>
#include <asm/uaccess.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/pgtable.h>

#include <linux/swapops.h>
#include <linux/elf.h>

#include "internal.h"

#include "internal.h"

#if defined(LAST_CPUPID_NOT_IN_PAGE_FLAGS) && !defined(CONFIG_COMPILE_TEST)
#warning Unfortunate NUMA and NUMA Balancing config, growing page-frame for last_cpupid.
#endif

#ifndef CONFIG_NEED_MULTIPLE_NODES
/* use the per-pgdat data instead for discontigmem - mbligh */
unsigned long max_mapnr;
struct page *mem_map;

EXPORT_SYMBOL(max_mapnr);
EXPORT_SYMBOL(mem_map);
#endif

unsigned long num_physpages;
/*
 * A number of key systems in x86 including ioremap() rely on the assumption
 * that high_memory defines the upper bound on direct map memory, then end
 * of ZONE_NORMAL.  Under CONFIG_DISCONTIG this means that max_low_pfn and
 * highstart_pfn must be the same; there must be no gap between ZONE_NORMAL
 * and ZONE_HIGHMEM.
 */
void * high_memory;

EXPORT_SYMBOL(num_physpages);
EXPORT_SYMBOL(high_memory);

/*
 * Randomize the address space (stacks, mmaps, brk, etc.).
 *
 * ( When CONFIG_COMPAT_BRK=y we exclude brk from randomization,
 *   as ancient (libc5 based) binaries can segfault. )
 */
int randomize_va_space __read_mostly =
#ifdef CONFIG_COMPAT_BRK
					1;
#else
					2;
#endif

static int __init disable_randmaps(char *s)
{
	randomize_va_space = 0;
	return 1;
}
__setup("norandmaps", disable_randmaps);


/*
 * If a p?d_bad entry is found while walking page tables, report
 * the error, before resetting entry to p?d_none.  Usually (but
 * very seldom) called out from the p?d_none_or_clear_bad macros.
 */

void pgd_clear_bad(pgd_t *pgd)
{
	pgd_ERROR(*pgd);
	pgd_clear(pgd);
}

void pud_clear_bad(pud_t *pud)
{
	pud_ERROR(*pud);
	pud_clear(pud);
}

void pmd_clear_bad(pmd_t *pmd)
{
	pmd_ERROR(*pmd);
	pmd_clear(pmd);
}

unsigned long zero_pfn __read_mostly;
unsigned long highest_memmap_pfn __read_mostly;

EXPORT_SYMBOL(zero_pfn);

/*
 * CONFIG_MMU architectures set up ZERO_PAGE in their paging_init()
 */
static int __init init_zero_pfn(void)
{
	zero_pfn = page_to_pfn(ZERO_PAGE(0));
	return 0;
}
early_initcall(init_zero_pfn);


#if defined(SPLIT_RSS_COUNTING)

void sync_mm_rss(struct mm_struct *mm)
{
	int i;

	for (i = 0; i < NR_MM_COUNTERS; i++) {
		if (current->rss_stat.count[i]) {
			add_mm_counter(mm, i, current->rss_stat.count[i]);
			current->rss_stat.count[i] = 0;
		}
	}
	current->rss_stat.events = 0;
}

static void add_mm_counter_fast(struct mm_struct *mm, int member, int val)
{
	struct task_struct *task = current;

	if (likely(task->mm == mm))
		task->rss_stat.count[member] += val;
	else
		add_mm_counter(mm, member, val);
}
#define inc_mm_counter_fast(mm, member) add_mm_counter_fast(mm, member, 1)
#define dec_mm_counter_fast(mm, member) add_mm_counter_fast(mm, member, -1)

/* sync counter once per 64 page faults */
#define TASK_RSS_EVENTS_THRESH	(64)
static void check_sync_rss_stat(struct task_struct *task)
{
	if (unlikely(task != current))
		return;
	if (unlikely(task->rss_stat.events++ > TASK_RSS_EVENTS_THRESH))
		sync_mm_rss(task->mm);
}
#else /* SPLIT_RSS_COUNTING */

#define inc_mm_counter_fast(mm, member) inc_mm_counter(mm, member)
#define dec_mm_counter_fast(mm, member) dec_mm_counter(mm, member)

static void check_sync_rss_stat(struct task_struct *task)
{
}

#endif /* SPLIT_RSS_COUNTING */

#ifdef HAVE_GENERIC_MMU_GATHER

static bool tlb_next_batch(struct mmu_gather *tlb)
{
	struct mmu_gather_batch *batch;

	batch = tlb->active;
	if (batch->next) {
		tlb->active = batch->next;
		return true;
	}

	if (tlb->batch_count == MAX_GATHER_BATCH_COUNT)
		return false;

	batch = (void *)__get_free_pages(GFP_NOWAIT | __GFP_NOWARN, 0);
	if (!batch)
		return false;

	tlb->batch_count++;
	batch->next = NULL;
	batch->nr   = 0;
	batch->max  = MAX_GATHER_BATCH;

	tlb->active->next = batch;
	tlb->active = batch;

	return true;
}

/* tlb_gather_mmu
 *	Called to initialize an (on-stack) mmu_gather structure for page-table
 *	tear-down from @mm. The @fullmm argument is used when @mm is without
 *	users and we're going to destroy the full address space (exit/execve).
 */
void tlb_gather_mmu(struct mmu_gather *tlb, struct mm_struct *mm, unsigned long start, unsigned long end)
{
	tlb->mm = mm;

	/* Is it from 0 to ~0? */
	tlb->fullmm     = !(start | (end+1));
	tlb->need_flush_all = 0;
	tlb->local.next = NULL;
	tlb->local.nr   = 0;
	tlb->local.max  = ARRAY_SIZE(tlb->__pages);
	tlb->active     = &tlb->local;
	tlb->batch_count = 0;

#ifdef CONFIG_HAVE_RCU_TABLE_FREE
	tlb->batch = NULL;
#endif

	__tlb_reset_range(tlb);
}

static void tlb_flush_mmu_tlbonly(struct mmu_gather *tlb)
{
	if (!tlb->end)
		return;

	tlb_flush(tlb);
	mmu_notifier_invalidate_range(tlb->mm, tlb->start, tlb->end);
#ifdef CONFIG_HAVE_RCU_TABLE_FREE
	tlb_table_flush(tlb);
#endif
	__tlb_reset_range(tlb);
}

static void tlb_flush_mmu_free(struct mmu_gather *tlb)
{
	struct mmu_gather_batch *batch;

	for (batch = &tlb->local; batch && batch->nr; batch = batch->next) {
		free_pages_and_swap_cache(batch->pages, batch->nr);
		batch->nr = 0;
	}
	tlb->active = &tlb->local;
}

void tlb_flush_mmu(struct mmu_gather *tlb)
{
	tlb_flush_mmu_tlbonly(tlb);
	tlb_flush_mmu_free(tlb);
}

/* tlb_finish_mmu
 *	Called at the end of the shootdown operation to free up any resources
 *	that were required.
 */
void tlb_finish_mmu(struct mmu_gather *tlb, unsigned long start, unsigned long end)
{
	struct mmu_gather_batch *batch, *next;

	tlb_flush_mmu(tlb);

	/* keep the page table cache within bounds */
	check_pgt_cache();

	for (batch = tlb->local.next; batch; batch = next) {
		next = batch->next;
		free_pages((unsigned long)batch, 0);
	}
	tlb->local.next = NULL;
}

/* __tlb_remove_page
 *	Must perform the equivalent to __free_pte(pte_get_and_clear(ptep)), while
 *	handling the additional races in SMP caused by other CPUs caching valid
 *	mappings in their TLBs. Returns the number of free page slots left.
 *	When out of page slots we must call tlb_flush_mmu().
 */
int __tlb_remove_page(struct mmu_gather *tlb, struct page *page)
{
	struct mmu_gather_batch *batch;

	VM_BUG_ON(!tlb->end);

	batch = tlb->active;
	batch->pages[batch->nr++] = page;
	if (batch->nr == batch->max) {
		if (!tlb_next_batch(tlb))
			return 0;
		batch = tlb->active;
	}
	VM_BUG_ON_PAGE(batch->nr > batch->max, page);

	return batch->max - batch->nr;
}

#endif /* HAVE_GENERIC_MMU_GATHER */

#ifdef CONFIG_HAVE_RCU_TABLE_FREE

/*
 * See the comment near struct mmu_table_batch.
 */

static void tlb_remove_table_smp_sync(void *arg)
{
	/* Simply deliver the interrupt */
}

static void tlb_remove_table_one(void *table)
{
	/*
	 * This isn't an RCU grace period and hence the page-tables cannot be
	 * assumed to be actually RCU-freed.
	 *
	 * It is however sufficient for software page-table walkers that rely on
	 * IRQ disabling. See the comment near struct mmu_table_batch.
	 */
	smp_call_function(tlb_remove_table_smp_sync, NULL, 1);
	__tlb_remove_table(table);
}

static void tlb_remove_table_rcu(struct rcu_head *head)
{
	struct mmu_table_batch *batch;
	int i;

	batch = container_of(head, struct mmu_table_batch, rcu);

	for (i = 0; i < batch->nr; i++)
		__tlb_remove_table(batch->tables[i]);

	free_page((unsigned long)batch);
}

void tlb_table_flush(struct mmu_gather *tlb)
{
	struct mmu_table_batch **batch = &tlb->batch;

	if (*batch) {
		call_rcu_sched(&(*batch)->rcu, tlb_remove_table_rcu);
		*batch = NULL;
	}
}

void tlb_remove_table(struct mmu_gather *tlb, void *table)
{
	struct mmu_table_batch **batch = &tlb->batch;

	if (*batch == NULL) {
		*batch = (struct mmu_table_batch *)__get_free_page(GFP_NOWAIT | __GFP_NOWARN);
		if (*batch == NULL) {
			tlb_remove_table_one(table);
			return;
		}
		(*batch)->nr = 0;
	}
	(*batch)->tables[(*batch)->nr++] = table;
	if ((*batch)->nr == MAX_TABLE_BATCH)
		tlb_table_flush(tlb);
}

#endif /* CONFIG_HAVE_RCU_TABLE_FREE */

/*
 * Note: this doesn't free the actual pages themselves. That
 * has been handled earlier when unmapping all the memory regions.
 */
static void free_pte_range(struct mmu_gather *tlb, pmd_t *pmd)
{
	pgtable_t token = pmd_pgtable(*pmd);
	pmd_clear(pmd);
	pte_free_tlb(tlb, token);
	tlb->mm->nr_ptes--;
static void free_pte_range(struct mmu_gather *tlb, pmd_t *pmd,
			   unsigned long addr)
{
	pgtable_t token = pmd_pgtable(*pmd);
	pmd_clear(pmd);
	pte_free_tlb(tlb, token, addr);
	atomic_long_dec(&tlb->mm->nr_ptes);
}

static inline void free_pmd_range(struct mmu_gather *tlb, pud_t *pud,
				unsigned long addr, unsigned long end,
				unsigned long floor, unsigned long ceiling)
{
	pmd_t *pmd;
	unsigned long next;
	unsigned long start;

	start = addr;
	pmd = pmd_offset(pud, addr);
	do {
		next = pmd_addr_end(addr, end);
		if (pmd_none_or_clear_bad(pmd))
			continue;
		free_pte_range(tlb, pmd);
		free_pte_range(tlb, pmd, addr);
	} while (pmd++, addr = next, addr != end);

	start &= PUD_MASK;
	if (start < floor)
		return;
	if (ceiling) {
		ceiling &= PUD_MASK;
		if (!ceiling)
			return;
	}
	if (end - 1 > ceiling - 1)
		return;

	pmd = pmd_offset(pud, start);
	pud_clear(pud);
	pmd_free_tlb(tlb, pmd);
	pmd_free_tlb(tlb, pmd, start);
	mm_dec_nr_pmds(tlb->mm);
}

static inline void free_pud_range(struct mmu_gather *tlb, pgd_t *pgd,
				unsigned long addr, unsigned long end,
				unsigned long floor, unsigned long ceiling)
{
	pud_t *pud;
	unsigned long next;
	unsigned long start;

	start = addr;
	pud = pud_offset(pgd, addr);
	do {
		next = pud_addr_end(addr, end);
		if (pud_none_or_clear_bad(pud))
			continue;
		free_pmd_range(tlb, pud, addr, next, floor, ceiling);
	} while (pud++, addr = next, addr != end);

	start &= PGDIR_MASK;
	if (start < floor)
		return;
	if (ceiling) {
		ceiling &= PGDIR_MASK;
		if (!ceiling)
			return;
	}
	if (end - 1 > ceiling - 1)
		return;

	pud = pud_offset(pgd, start);
	pgd_clear(pgd);
	pud_free_tlb(tlb, pud);
	pud_free_tlb(tlb, pud, start);
}

/*
 * This function frees user-level page tables of a process.
 *
 * Must be called with pagetable lock held.
 */
void free_pgd_range(struct mmu_gather *tlb,
			unsigned long addr, unsigned long end,
			unsigned long floor, unsigned long ceiling)
{
	pgd_t *pgd;
	unsigned long next;
	unsigned long start;

	/*
	 * The next few lines have given us lots of grief...
	 *
	 * Why are we testing PMD* at this top level?  Because often
	 * there will be no work to do at all, and we'd prefer not to
	 * go all the way down to the bottom just to discover that.
	 *
	 * Why all these "- 1"s?  Because 0 represents both the bottom
	 * of the address space and the top of it (using -1 for the
	 * top wouldn't help much: the masks would do the wrong thing).
	 * The rule is that addr 0 and floor 0 refer to the bottom of
	 * the address space, but end 0 and ceiling 0 refer to the top
	 * Comparisons need to use "end - 1" and "ceiling - 1" (though
	 * that end 0 case should be mythical).
	 *
	 * Wherever addr is brought up or ceiling brought down, we must
	 * be careful to reject "the opposite 0" before it confuses the
	 * subsequent tests.  But what about where end is brought down
	 * by PMD_SIZE below? no, end can't go down to 0 there.
	 *
	 * Whereas we round start (addr) and ceiling down, by different
	 * masks at different levels, in order to test whether a table
	 * now has no other vmas using it, so can be freed, we don't
	 * bother to round floor or end up - the tests don't need that.
	 */

	addr &= PMD_MASK;
	if (addr < floor) {
		addr += PMD_SIZE;
		if (!addr)
			return;
	}
	if (ceiling) {
		ceiling &= PMD_MASK;
		if (!ceiling)
			return;
	}
	if (end - 1 > ceiling - 1)
		end -= PMD_SIZE;
	if (addr > end - 1)
		return;

	start = addr;
	pgd = pgd_offset(tlb->mm, addr);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_none_or_clear_bad(pgd))
			continue;
		free_pud_range(tlb, pgd, addr, next, floor, ceiling);
	} while (pgd++, addr = next, addr != end);
}

void free_pgtables(struct mmu_gather *tlb, struct vm_area_struct *vma,
		unsigned long floor, unsigned long ceiling)
{
	while (vma) {
		struct vm_area_struct *next = vma->vm_next;
		unsigned long addr = vma->vm_start;

		/*
		 * Hide vma from rmap and vmtruncate before freeing pgtables
		 */
		anon_vma_unlink(vma);
		 * Hide vma from rmap and truncate_pagecache before freeing
		 * pgtables
		 */
		unlink_anon_vmas(vma);
		unlink_file_vma(vma);

		if (is_vm_hugetlb_page(vma)) {
			hugetlb_free_pgd_range(tlb, addr, vma->vm_end,
				floor, next? next->vm_start: ceiling);
		} else {
			/*
			 * Optimization: gather nearby vmas into one call down
			 */
			while (next && next->vm_start <= vma->vm_end + PMD_SIZE
			       && !is_vm_hugetlb_page(next)) {
				vma = next;
				next = vma->vm_next;
				anon_vma_unlink(vma);
				unlink_anon_vmas(vma);
				unlink_file_vma(vma);
			}
			free_pgd_range(tlb, addr, vma->vm_end,
				floor, next? next->vm_start: ceiling);
		}
		vma = next;
	}
}

int __pte_alloc(struct mm_struct *mm, pmd_t *pmd, unsigned long address)
{
	pgtable_t new = pte_alloc_one(mm, address);
int __pte_alloc(struct mm_struct *mm, struct vm_area_struct *vma,
		pmd_t *pmd, unsigned long address)
{
	spinlock_t *ptl;
	pgtable_t new = pte_alloc_one(mm, address);
	int wait_split_huge_page;
	if (!new)
		return -ENOMEM;

	/*
	 * Ensure all pte setup (eg. pte page lock and page clearing) are
	 * visible before the pte is made visible to other CPUs by being
	 * put into page tables.
	 *
	 * The other side of the story is the pointer chasing in the page
	 * table walking code (when walking the page table without locking;
	 * ie. most of the time). Fortunately, these data accesses consist
	 * of a chain of data-dependent loads, meaning most CPUs (alpha
	 * being the notable exception) will already guarantee loads are
	 * seen in-order. See the alpha page table accessors for the
	 * smp_read_barrier_depends() barriers in page table walking code.
	 */
	smp_wmb(); /* Could be smp_wmb__xxx(before|after)_spin_lock */

	spin_lock(&mm->page_table_lock);
	if (!pmd_present(*pmd)) {	/* Has another populated it ? */
		mm->nr_ptes++;
		pmd_populate(mm, pmd, new);
		new = NULL;
	}
	spin_unlock(&mm->page_table_lock);
	if (new)
		pte_free(mm, new);
	ptl = pmd_lock(mm, pmd);
	wait_split_huge_page = 0;
	if (likely(pmd_none(*pmd))) {	/* Has another populated it ? */
		atomic_long_inc(&mm->nr_ptes);
		pmd_populate(mm, pmd, new);
		new = NULL;
	} else if (unlikely(pmd_trans_splitting(*pmd)))
		wait_split_huge_page = 1;
	spin_unlock(ptl);
	if (new)
		pte_free(mm, new);
	if (wait_split_huge_page)
		wait_split_huge_page(vma->anon_vma, pmd);
	return 0;
}

int __pte_alloc_kernel(pmd_t *pmd, unsigned long address)
{
	pte_t *new = pte_alloc_one_kernel(&init_mm, address);
	if (!new)
		return -ENOMEM;

	smp_wmb(); /* See comment in __pte_alloc */

	spin_lock(&init_mm.page_table_lock);
	if (!pmd_present(*pmd)) {	/* Has another populated it ? */
		pmd_populate_kernel(&init_mm, pmd, new);
		new = NULL;
	}
	if (likely(pmd_none(*pmd))) {	/* Has another populated it ? */
		pmd_populate_kernel(&init_mm, pmd, new);
		new = NULL;
	} else
		VM_BUG_ON(pmd_trans_splitting(*pmd));
	spin_unlock(&init_mm.page_table_lock);
	if (new)
		pte_free_kernel(&init_mm, new);
	return 0;
}

static inline void add_mm_rss(struct mm_struct *mm, int file_rss, int anon_rss)
{
	if (file_rss)
		add_mm_counter(mm, file_rss, file_rss);
	if (anon_rss)
		add_mm_counter(mm, anon_rss, anon_rss);
static inline void init_rss_vec(int *rss)
{
	memset(rss, 0, sizeof(int) * NR_MM_COUNTERS);
}

static inline void add_mm_rss_vec(struct mm_struct *mm, int *rss)
{
	int i;

	if (current->mm == mm)
		sync_mm_rss(mm);
	for (i = 0; i < NR_MM_COUNTERS; i++)
		if (rss[i])
			add_mm_counter(mm, i, rss[i]);
}

/*
 * This function is called to print an error when a bad pte
 * is found. For example, we might have a PFN-mapped pte in
 * a region that doesn't allow it.
 *
 * The calling function must still handle the error.
 */
static void print_bad_pte(struct vm_area_struct *vma, pte_t pte,
			  unsigned long vaddr)
{
	printk(KERN_ERR "Bad pte = %08llx, process = %s, "
			"vm_flags = %lx, vaddr = %lx\n",
		(long long)pte_val(pte),
		(vma->vm_mm == current->mm ? current->comm : "???"),
		vma->vm_flags, vaddr);
	dump_stack();
}

static inline int is_cow_mapping(unsigned int flags)
{
	return (flags & (VM_SHARED | VM_MAYWRITE)) == VM_MAYWRITE;
static void print_bad_pte(struct vm_area_struct *vma, unsigned long addr,
			  pte_t pte, struct page *page)
{
	pgd_t *pgd = pgd_offset(vma->vm_mm, addr);
	pud_t *pud = pud_offset(pgd, addr);
	pmd_t *pmd = pmd_offset(pud, addr);
	struct address_space *mapping;
	pgoff_t index;
	static unsigned long resume;
	static unsigned long nr_shown;
	static unsigned long nr_unshown;

	/*
	 * Allow a burst of 60 reports, then keep quiet for that minute;
	 * or allow a steady drip of one report per second.
	 */
	if (nr_shown == 60) {
		if (time_before(jiffies, resume)) {
			nr_unshown++;
			return;
		}
		if (nr_unshown) {
			printk(KERN_ALERT
				"BUG: Bad page map: %lu messages suppressed\n",
				nr_unshown);
			nr_unshown = 0;
		}
		nr_shown = 0;
	}
	if (nr_shown++ == 0)
		resume = jiffies + 60 * HZ;

	mapping = vma->vm_file ? vma->vm_file->f_mapping : NULL;
	index = linear_page_index(vma, addr);

	printk(KERN_ALERT
		"BUG: Bad page map in process %s  pte:%08llx pmd:%08llx\n",
		current->comm,
		(long long)pte_val(pte), (long long)pmd_val(*pmd));
	if (page)
		dump_page(page, "bad pte");
	printk(KERN_ALERT
		"addr:%p vm_flags:%08lx anon_vma:%p mapping:%p index:%lx\n",
		(void *)addr, vma->vm_flags, vma->anon_vma, mapping, index);
	/*
	 * Choose text because data symbols depend on CONFIG_KALLSYMS_ALL=y
	 */
	pr_alert("file:%pD fault:%pf mmap:%pf readpage:%pf\n",
		 vma->vm_file,
		 vma->vm_ops ? vma->vm_ops->fault : NULL,
		 vma->vm_file ? vma->vm_file->f_op->mmap : NULL,
		 mapping ? mapping->a_ops->readpage : NULL);
	dump_stack();
	add_taint(TAINT_BAD_PAGE, LOCKDEP_NOW_UNRELIABLE);
}

/*
 * vm_normal_page -- This function gets the "struct page" associated with a pte.
 *
 * "Special" mappings do not wish to be associated with a "struct page" (either
 * it doesn't exist, or it exists but they don't want to touch it). In this
 * case, NULL is returned here. "Normal" mappings do have a struct page.
 *
 * There are 2 broad cases. Firstly, an architecture may define a pte_special()
 * pte bit, in which case this function is trivial. Secondly, an architecture
 * may not have a spare pte bit, which requires a more complicated scheme,
 * described below.
 *
 * A raw VM_PFNMAP mapping (ie. one that is not COWed) is always considered a
 * special mapping (even if there are underlying and valid "struct pages").
 * COWed pages of a VM_PFNMAP are always normal.
 *
 * The way we recognize COWed pages within VM_PFNMAP mappings is through the
 * rules set up by "remap_pfn_range()": the vma will have the VM_PFNMAP bit
 * set, and the vm_pgoff will point to the first PFN mapped: thus every special
 * mapping will always honor the rule
 *
 *	pfn_of_page == vma->vm_pgoff + ((addr - vma->vm_start) >> PAGE_SHIFT)
 *
 * And for normal mappings this is false.
 *
 * This restricts such mappings to be a linear translation from virtual address
 * to pfn. To get around this restriction, we allow arbitrary mappings so long
 * as the vma is not a COW mapping; in that case, we know that all ptes are
 * special (because none can have been COWed).
 *
 *
 * In order to support COW of arbitrary special mappings, we have VM_MIXEDMAP.
 *
 * VM_MIXEDMAP mappings can likewise contain memory with or without "struct
 * page" backing, however the difference is that _all_ pages with a struct
 * page (that is, those where pfn_valid is true) are refcounted and considered
 * normal pages by the VM. The disadvantage is that pages are refcounted
 * (which can be slower and simply not an option for some PFNMAP users). The
 * advantage is that we don't have to follow the strict linearity rule of
 * PFNMAP mappings in order to support COWable mappings.
 *
 */
#ifdef __HAVE_ARCH_PTE_SPECIAL
# define HAVE_PTE_SPECIAL 1
#else
# define HAVE_PTE_SPECIAL 0
#endif
struct page *vm_normal_page(struct vm_area_struct *vma, unsigned long addr,
				pte_t pte)
{
	unsigned long pfn;

	if (HAVE_PTE_SPECIAL) {
		if (likely(!pte_special(pte))) {
			VM_BUG_ON(!pfn_valid(pte_pfn(pte)));
			return pte_page(pte);
		}
		VM_BUG_ON(!(vma->vm_flags & (VM_PFNMAP | VM_MIXEDMAP)));
	unsigned long pfn = pte_pfn(pte);

	if (HAVE_PTE_SPECIAL) {
		if (likely(!pte_special(pte)))
			goto check_pfn;
		if (vma->vm_ops && vma->vm_ops->find_special_page)
			return vma->vm_ops->find_special_page(vma, addr);
		if (vma->vm_flags & (VM_PFNMAP | VM_MIXEDMAP))
			return NULL;
		if (!is_zero_pfn(pfn))
			print_bad_pte(vma, addr, pte, NULL);
		return NULL;
	}

	/* !HAVE_PTE_SPECIAL case follows: */

	pfn = pte_pfn(pte);

	if (unlikely(vma->vm_flags & (VM_PFNMAP|VM_MIXEDMAP))) {
		if (vma->vm_flags & VM_MIXEDMAP) {
			if (!pfn_valid(pfn))
				return NULL;
			goto out;
		} else {
			unsigned long off;
			off = (addr - vma->vm_start) >> PAGE_SHIFT;
			if (pfn == vma->vm_pgoff + off)
				return NULL;
			if (!is_cow_mapping(vma->vm_flags))
				return NULL;
		}
	}

	VM_BUG_ON(!pfn_valid(pfn));

	/*
	 * NOTE! We still have PageReserved() pages in the page tables.
	 *
	if (is_zero_pfn(pfn))
		return NULL;
check_pfn:
	if (unlikely(pfn > highest_memmap_pfn)) {
		print_bad_pte(vma, addr, pte, NULL);
		return NULL;
	}

	/*
	 * NOTE! We still have PageReserved() pages in the page tables.
	 * eg. VDSO mappings can cause them to exist.
	 */
out:
	return pfn_to_page(pfn);
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
struct page *vm_normal_page_pmd(struct vm_area_struct *vma, unsigned long addr,
				pmd_t pmd)
{
	unsigned long pfn = pmd_pfn(pmd);

	/*
	 * There is no pmd_special() but there may be special pmds, e.g.
	 * in a direct-access (dax) mapping, so let's just replicate the
	 * !HAVE_PTE_SPECIAL case from vm_normal_page() here.
	 */
	if (unlikely(vma->vm_flags & (VM_PFNMAP|VM_MIXEDMAP))) {
		if (vma->vm_flags & VM_MIXEDMAP) {
			if (!pfn_valid(pfn))
				return NULL;
			goto out;
		} else {
			unsigned long off;
			off = (addr - vma->vm_start) >> PAGE_SHIFT;
			if (pfn == vma->vm_pgoff + off)
				return NULL;
			if (!is_cow_mapping(vma->vm_flags))
				return NULL;
		}
	}

	if (is_zero_pfn(pfn))
		return NULL;
	if (unlikely(pfn > highest_memmap_pfn))
		return NULL;

	/*
	 * NOTE! We still have PageReserved() pages in the page tables.
	 * eg. VDSO mappings can cause them to exist.
	 */
out:
	return pfn_to_page(pfn);
}
#endif

/*
 * copy one vm_area from one task to the other. Assumes the page tables
 * already present in the new task to be cleared in the whole range
 * covered by this vma.
 */

static inline void
static inline unsigned long
copy_one_pte(struct mm_struct *dst_mm, struct mm_struct *src_mm,
		pte_t *dst_pte, pte_t *src_pte, struct vm_area_struct *vma,
		unsigned long addr, int *rss)
{
	unsigned long vm_flags = vma->vm_flags;
	pte_t pte = *src_pte;
	struct page *page;

	/* pte contains position in swap or file, so copy. */
	if (unlikely(!pte_present(pte))) {
		if (!pte_file(pte)) {
			swp_entry_t entry = pte_to_swp_entry(pte);

			swap_duplicate(entry);
		swp_entry_t entry = pte_to_swp_entry(pte);

		if (likely(!non_swap_entry(entry))) {
			if (swap_duplicate(entry) < 0)
				return entry.val;

			/* make sure dst_mm is on swapoff's mmlist. */
			if (unlikely(list_empty(&dst_mm->mmlist))) {
				spin_lock(&mmlist_lock);
				if (list_empty(&dst_mm->mmlist))
					list_add(&dst_mm->mmlist,
						 &src_mm->mmlist);
				spin_unlock(&mmlist_lock);
			}
			if (is_write_migration_entry(entry) &&
					is_cow_mapping(vm_flags)) {
				/*
				 * COW mappings require pages in both parent
				 * and child to be set to read.
				 */
				make_migration_entry_read(&entry);
				pte = swp_entry_to_pte(entry);
							&src_mm->mmlist);
				spin_unlock(&mmlist_lock);
			}
			rss[MM_SWAPENTS]++;
		} else if (is_migration_entry(entry)) {
			page = migration_entry_to_page(entry);

			if (PageAnon(page))
				rss[MM_ANONPAGES]++;
			else
				rss[MM_FILEPAGES]++;

			if (is_write_migration_entry(entry) &&
					is_cow_mapping(vm_flags)) {
				/*
				 * COW mappings require pages in both
				 * parent and child to be set to read.
				 */
				make_migration_entry_read(&entry);
				pte = swp_entry_to_pte(entry);
				if (pte_swp_soft_dirty(*src_pte))
					pte = pte_swp_mksoft_dirty(pte);
				set_pte_at(src_mm, addr, src_pte, pte);
			}
		}
		goto out_set_pte;
	}

	/*
	 * If it's a COW mapping, write protect it both
	 * in the parent and the child
	 */
	if (is_cow_mapping(vm_flags)) {
		ptep_set_wrprotect(src_mm, addr, src_pte);
		pte = pte_wrprotect(pte);
	}

	/*
	 * If it's a shared mapping, mark it clean in
	 * the child
	 */
	if (vm_flags & VM_SHARED)
		pte = pte_mkclean(pte);
	pte = pte_mkold(pte);

	page = vm_normal_page(vma, addr, pte);
	if (page) {
		get_page(page);
		page_dup_rmap(page, vma, addr);
		rss[!!PageAnon(page)]++;
		page_dup_rmap(page);
		if (PageAnon(page))
			rss[MM_ANONPAGES]++;
		else
			rss[MM_FILEPAGES]++;
	}

out_set_pte:
	set_pte_at(dst_mm, addr, dst_pte, pte);
}

static int copy_pte_range(struct mm_struct *dst_mm, struct mm_struct *src_mm,
		pmd_t *dst_pmd, pmd_t *src_pmd, struct vm_area_struct *vma,
		unsigned long addr, unsigned long end)
{
	pte_t *src_pte, *dst_pte;
	spinlock_t *src_ptl, *dst_ptl;
	int progress = 0;
	int rss[2];

again:
	rss[1] = rss[0] = 0;
	dst_pte = pte_alloc_map_lock(dst_mm, dst_pmd, addr, &dst_ptl);
	if (!dst_pte)
		return -ENOMEM;
	src_pte = pte_offset_map_nested(src_pmd, addr);
	src_ptl = pte_lockptr(src_mm, src_pmd);
	spin_lock_nested(src_ptl, SINGLE_DEPTH_NESTING);
	return 0;
}

static int copy_pte_range(struct mm_struct *dst_mm, struct mm_struct *src_mm,
		   pmd_t *dst_pmd, pmd_t *src_pmd, struct vm_area_struct *vma,
		   unsigned long addr, unsigned long end)
{
	pte_t *orig_src_pte, *orig_dst_pte;
	pte_t *src_pte, *dst_pte;
	spinlock_t *src_ptl, *dst_ptl;
	int progress = 0;
	int rss[NR_MM_COUNTERS];
	swp_entry_t entry = (swp_entry_t){0};

again:
	init_rss_vec(rss);

	dst_pte = pte_alloc_map_lock(dst_mm, dst_pmd, addr, &dst_ptl);
	if (!dst_pte)
		return -ENOMEM;
	src_pte = pte_offset_map(src_pmd, addr);
	src_ptl = pte_lockptr(src_mm, src_pmd);
	spin_lock_nested(src_ptl, SINGLE_DEPTH_NESTING);
	orig_src_pte = src_pte;
	orig_dst_pte = dst_pte;
	arch_enter_lazy_mmu_mode();

	do {
		/*
		 * We are holding two locks at this point - either of them
		 * could generate latencies in another task on another CPU.
		 */
		if (progress >= 32) {
			progress = 0;
			if (need_resched() ||
			    spin_needbreak(src_ptl) || spin_needbreak(dst_ptl))
				break;
		}
		if (pte_none(*src_pte)) {
			progress++;
			continue;
		}
		copy_one_pte(dst_mm, src_mm, dst_pte, src_pte, vma, addr, rss);
		entry.val = copy_one_pte(dst_mm, src_mm, dst_pte, src_pte,
							vma, addr, rss);
		if (entry.val)
			break;
		progress += 8;
	} while (dst_pte++, src_pte++, addr += PAGE_SIZE, addr != end);

	arch_leave_lazy_mmu_mode();
	spin_unlock(src_ptl);
	pte_unmap_nested(src_pte - 1);
	add_mm_rss(dst_mm, rss[0], rss[1]);
	pte_unmap_unlock(dst_pte - 1, dst_ptl);
	cond_resched();
	pte_unmap(orig_src_pte);
	add_mm_rss_vec(dst_mm, rss);
	pte_unmap_unlock(orig_dst_pte, dst_ptl);
	cond_resched();

	if (entry.val) {
		if (add_swap_count_continuation(entry, GFP_KERNEL) < 0)
			return -ENOMEM;
		progress = 0;
	}
	if (addr != end)
		goto again;
	return 0;
}

static inline int copy_pmd_range(struct mm_struct *dst_mm, struct mm_struct *src_mm,
		pud_t *dst_pud, pud_t *src_pud, struct vm_area_struct *vma,
		unsigned long addr, unsigned long end)
{
	pmd_t *src_pmd, *dst_pmd;
	unsigned long next;

	dst_pmd = pmd_alloc(dst_mm, dst_pud, addr);
	if (!dst_pmd)
		return -ENOMEM;
	src_pmd = pmd_offset(src_pud, addr);
	do {
		next = pmd_addr_end(addr, end);
		if (pmd_trans_huge(*src_pmd)) {
			int err;
			VM_BUG_ON(next-addr != HPAGE_PMD_SIZE);
			err = copy_huge_pmd(dst_mm, src_mm,
					    dst_pmd, src_pmd, addr, vma);
			if (err == -ENOMEM)
				return -ENOMEM;
			if (!err)
				continue;
			/* fall through */
		}
		if (pmd_none_or_clear_bad(src_pmd))
			continue;
		if (copy_pte_range(dst_mm, src_mm, dst_pmd, src_pmd,
						vma, addr, next))
			return -ENOMEM;
	} while (dst_pmd++, src_pmd++, addr = next, addr != end);
	return 0;
}

static inline int copy_pud_range(struct mm_struct *dst_mm, struct mm_struct *src_mm,
		pgd_t *dst_pgd, pgd_t *src_pgd, struct vm_area_struct *vma,
		unsigned long addr, unsigned long end)
{
	pud_t *src_pud, *dst_pud;
	unsigned long next;

	dst_pud = pud_alloc(dst_mm, dst_pgd, addr);
	if (!dst_pud)
		return -ENOMEM;
	src_pud = pud_offset(src_pgd, addr);
	do {
		next = pud_addr_end(addr, end);
		if (pud_none_or_clear_bad(src_pud))
			continue;
		if (copy_pmd_range(dst_mm, src_mm, dst_pud, src_pud,
						vma, addr, next))
			return -ENOMEM;
	} while (dst_pud++, src_pud++, addr = next, addr != end);
	return 0;
}

int copy_page_range(struct mm_struct *dst_mm, struct mm_struct *src_mm,
		struct vm_area_struct *vma)
{
	pgd_t *src_pgd, *dst_pgd;
	unsigned long next;
	unsigned long addr = vma->vm_start;
	unsigned long end = vma->vm_end;
	unsigned long mmun_start;	/* For mmu_notifiers */
	unsigned long mmun_end;		/* For mmu_notifiers */
	bool is_cow;
	int ret;

	/*
	 * Don't copy ptes where a page fault will fill them correctly.
	 * Fork becomes much lighter when there are big shared or private
	 * readonly mappings. The tradeoff is that copy_page_range is more
	 * efficient than faulting.
	 */
	if (!(vma->vm_flags & (VM_HUGETLB|VM_NONLINEAR|VM_PFNMAP|VM_INSERTPAGE))) {
		if (!vma->anon_vma)
			return 0;
	}
	if (!(vma->vm_flags & (VM_HUGETLB | VM_PFNMAP | VM_MIXEDMAP)) &&
			!vma->anon_vma)
		return 0;

	if (is_vm_hugetlb_page(vma))
		return copy_hugetlb_page_range(dst_mm, src_mm, vma);

	if (unlikely(vma->vm_flags & VM_PFNMAP)) {
		/*
		 * We do not free on error cases below as remove_vma
		 * gets called on error from higher level routine
		 */
		ret = track_pfn_copy(vma);
		if (ret)
			return ret;
	}

	/*
	 * We need to invalidate the secondary MMU mappings only when
	 * there could be a permission downgrade on the ptes of the
	 * parent mm. And a permission downgrade will only happen if
	 * is_cow_mapping() returns true.
	 */
	if (is_cow_mapping(vma->vm_flags))
		mmu_notifier_invalidate_range_start(src_mm, addr, end);
	is_cow = is_cow_mapping(vma->vm_flags);
	mmun_start = addr;
	mmun_end   = end;
	if (is_cow)
		mmu_notifier_invalidate_range_start(src_mm, mmun_start,
						    mmun_end);

	ret = 0;
	dst_pgd = pgd_offset(dst_mm, addr);
	src_pgd = pgd_offset(src_mm, addr);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_none_or_clear_bad(src_pgd))
			continue;
		if (unlikely(copy_pud_range(dst_mm, src_mm, dst_pgd, src_pgd,
					    vma, addr, next))) {
			ret = -ENOMEM;
			break;
		}
	} while (dst_pgd++, src_pgd++, addr = next, addr != end);

	if (is_cow_mapping(vma->vm_flags))
		mmu_notifier_invalidate_range_end(src_mm,
						  vma->vm_start, end);
	if (is_cow)
		mmu_notifier_invalidate_range_end(src_mm, mmun_start, mmun_end);
	return ret;
}

static unsigned long zap_pte_range(struct mmu_gather *tlb,
				struct vm_area_struct *vma, pmd_t *pmd,
				unsigned long addr, unsigned long end,
				long *zap_work, struct zap_details *details)
{
	struct mm_struct *mm = tlb->mm;
	pte_t *pte;
	spinlock_t *ptl;
	int file_rss = 0;
	int anon_rss = 0;

	pte = pte_offset_map_lock(mm, pmd, addr, &ptl);
				struct zap_details *details)
{
	struct mm_struct *mm = tlb->mm;
	int force_flush = 0;
	int rss[NR_MM_COUNTERS];
	spinlock_t *ptl;
	pte_t *start_pte;
	pte_t *pte;
	swp_entry_t entry;

again:
	init_rss_vec(rss);
	start_pte = pte_offset_map_lock(mm, pmd, addr, &ptl);
	pte = start_pte;
	flush_tlb_batched_pending(mm);
	arch_enter_lazy_mmu_mode();
	do {
		pte_t ptent = *pte;
		if (pte_none(ptent)) {
			(*zap_work)--;
			continue;
		}

		(*zap_work) -= PAGE_SIZE;

			continue;
		}

		if (pte_present(ptent)) {
			struct page *page;

			page = vm_normal_page(vma, addr, ptent);
			if (unlikely(details) && page) {
				/*
				 * unmap_shared_mapping_pages() wants to
				 * invalidate cache without truncating:
				 * unmap shared but keep private pages.
				 */
				if (details->check_mapping &&
				    details->check_mapping != page->mapping)
					continue;
				/*
				 * Each page->index must be checked when
				 * invalidating or truncating nonlinear.
				 */
				if (details->nonlinear_vma &&
				    (page->index < details->first_index ||
				     page->index > details->last_index))
					continue;
			}
			ptent = ptep_get_and_clear_full(mm, addr, pte,
							tlb->fullmm);
			tlb_remove_tlb_entry(tlb, pte, addr);
			if (unlikely(!page))
				continue;
			if (unlikely(details) && details->nonlinear_vma
			    && linear_page_index(details->nonlinear_vma,
						addr) != page->index)
				set_pte_at(mm, addr, pte,
					   pgoff_to_pte(page->index));
			if (PageAnon(page))
				anon_rss--;
			else {
				if (pte_dirty(ptent))
					set_page_dirty(page);
				if (pte_young(ptent))
					SetPageReferenced(page);
				file_rss--;
			}
			page_remove_rmap(page, vma);
			tlb_remove_page(tlb, page);
			continue;
		}
		/*
		 * If details->check_mapping, we leave swap entries;
		 * if details->nonlinear_vma, we leave file entries.
		 */
		if (unlikely(details))
			continue;
		if (!pte_file(ptent))
			free_swap_and_cache(pte_to_swp_entry(ptent));
		pte_clear_not_present_full(mm, addr, pte, tlb->fullmm);
	} while (pte++, addr += PAGE_SIZE, (addr != end && *zap_work > 0));

	add_mm_rss(mm, file_rss, anon_rss);
	arch_leave_lazy_mmu_mode();
	pte_unmap_unlock(pte - 1, ptl);
			if (PageAnon(page))
				rss[MM_ANONPAGES]--;
			else {
				if (pte_dirty(ptent)) {
					force_flush = 1;
					set_page_dirty(page);
				}
				if (pte_young(ptent) &&
				    likely(!(vma->vm_flags & VM_SEQ_READ)))
					mark_page_accessed(page);
				rss[MM_FILEPAGES]--;
			}
			page_remove_rmap(page);
			if (unlikely(page_mapcount(page) < 0))
				print_bad_pte(vma, addr, ptent, page);
			if (unlikely(!__tlb_remove_page(tlb, page))) {
				force_flush = 1;
				addr += PAGE_SIZE;
				break;
			}
			continue;
		}
		/* If details->check_mapping, we leave swap entries. */
		if (unlikely(details))
			continue;

		entry = pte_to_swp_entry(ptent);
		if (!non_swap_entry(entry))
			rss[MM_SWAPENTS]--;
		else if (is_migration_entry(entry)) {
			struct page *page;

			page = migration_entry_to_page(entry);

			if (PageAnon(page))
				rss[MM_ANONPAGES]--;
			else
				rss[MM_FILEPAGES]--;
		}
		if (unlikely(!free_swap_and_cache(entry)))
			print_bad_pte(vma, addr, ptent, NULL);
		pte_clear_not_present_full(mm, addr, pte, tlb->fullmm);
	} while (pte++, addr += PAGE_SIZE, addr != end);

	add_mm_rss_vec(mm, rss);
	arch_leave_lazy_mmu_mode();

	/* Do the actual TLB flush before dropping ptl */
	if (force_flush)
		tlb_flush_mmu_tlbonly(tlb);
	pte_unmap_unlock(start_pte, ptl);

	/*
	 * If we forced a TLB flush (either due to running out of
	 * batch buffers or because we needed to flush dirty TLB
	 * entries before releasing the ptl), free the batched
	 * memory too. Restart if we didn't do everything.
	 */
	if (force_flush) {
		force_flush = 0;
		tlb_flush_mmu_free(tlb);

		if (addr != end)
			goto again;
	}

	return addr;
}

static inline unsigned long zap_pmd_range(struct mmu_gather *tlb,
				struct vm_area_struct *vma, pud_t *pud,
				unsigned long addr, unsigned long end,
				long *zap_work, struct zap_details *details)
				struct zap_details *details)
{
	pmd_t *pmd;
	unsigned long next;

	pmd = pmd_offset(pud, addr);
	do {
		next = pmd_addr_end(addr, end);
		if (pmd_none_or_clear_bad(pmd)) {
			(*zap_work)--;
			continue;
		}
		next = zap_pte_range(tlb, vma, pmd, addr, next,
						zap_work, details);
	} while (pmd++, addr = next, (addr != end && *zap_work > 0));
		if (pmd_trans_huge(*pmd)) {
			if (next - addr != HPAGE_PMD_SIZE) {
#ifdef CONFIG_DEBUG_VM
				if (!rwsem_is_locked(&tlb->mm->mmap_sem)) {
					pr_err("%s: mmap_sem is unlocked! addr=0x%lx end=0x%lx vma->vm_start=0x%lx vma->vm_end=0x%lx\n",
						__func__, addr, end,
						vma->vm_start,
						vma->vm_end);
					BUG();
				}
#endif
				split_huge_page_pmd(vma, addr, pmd);
			} else if (zap_huge_pmd(tlb, vma, pmd, addr))
				goto next;
			/* fall through */
		}
		/*
		 * Here there can be other concurrent MADV_DONTNEED or
		 * trans huge page faults running, and if the pmd is
		 * none or trans huge it can change under us. This is
		 * because MADV_DONTNEED holds the mmap_sem in read
		 * mode.
		 */
		if (pmd_none_or_trans_huge_or_clear_bad(pmd))
			goto next;
		next = zap_pte_range(tlb, vma, pmd, addr, next, details);
next:
		cond_resched();
	} while (pmd++, addr = next, addr != end);

	return addr;
}

static inline unsigned long zap_pud_range(struct mmu_gather *tlb,
				struct vm_area_struct *vma, pgd_t *pgd,
				unsigned long addr, unsigned long end,
				long *zap_work, struct zap_details *details)
				struct zap_details *details)
{
	pud_t *pud;
	unsigned long next;

	pud = pud_offset(pgd, addr);
	do {
		next = pud_addr_end(addr, end);
		if (pud_none_or_clear_bad(pud)) {
			(*zap_work)--;
			continue;
		}
		next = zap_pmd_range(tlb, vma, pud, addr, next,
						zap_work, details);
	} while (pud++, addr = next, (addr != end && *zap_work > 0));
		if (pud_none_or_clear_bad(pud))
			continue;
		next = zap_pmd_range(tlb, vma, pud, addr, next, details);
	} while (pud++, addr = next, addr != end);

	return addr;
}

static unsigned long unmap_page_range(struct mmu_gather *tlb,
				struct vm_area_struct *vma,
				unsigned long addr, unsigned long end,
				long *zap_work, struct zap_details *details)
static void unmap_page_range(struct mmu_gather *tlb,
			     struct vm_area_struct *vma,
			     unsigned long addr, unsigned long end,
			     struct zap_details *details)
{
	pgd_t *pgd;
	unsigned long next;

	if (details && !details->check_mapping && !details->nonlinear_vma)
	if (details && !details->check_mapping)
		details = NULL;

	BUG_ON(addr >= end);
	tlb_start_vma(tlb, vma);
	pgd = pgd_offset(vma->vm_mm, addr);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_none_or_clear_bad(pgd)) {
			(*zap_work)--;
			continue;
		}
		next = zap_pud_range(tlb, vma, pgd, addr, next,
						zap_work, details);
	} while (pgd++, addr = next, (addr != end && *zap_work > 0));
	tlb_end_vma(tlb, vma);

	return addr;
}

#ifdef CONFIG_PREEMPT
# define ZAP_BLOCK_SIZE	(8 * PAGE_SIZE)
#else
/* No preempt: go for improved straight-line efficiency */
# define ZAP_BLOCK_SIZE	(1024 * PAGE_SIZE)
#endif

/**
 * unmap_vmas - unmap a range of memory covered by a list of vma's
 * @tlbp: address of the caller's struct mmu_gather
 * @vma: the starting vma
 * @start_addr: virtual address at which to start unmapping
 * @end_addr: virtual address at which to end unmapping
 * @nr_accounted: Place number of unmapped pages in vm-accountable vma's here
 * @details: details of nonlinear truncation or shared cache invalidation
 *
 * Returns the end address of the unmapping (restart addr if interrupted).
 *
 * Unmap all pages in the vma list.
 *
 * We aim to not hold locks for too long (for scheduling latency reasons).
 * So zap pages in ZAP_BLOCK_SIZE bytecounts.  This means we need to
 * return the ending mmu_gather to the caller.
 *
		if (pgd_none_or_clear_bad(pgd))
			continue;
		next = zap_pud_range(tlb, vma, pgd, addr, next, details);
	} while (pgd++, addr = next, addr != end);
	tlb_end_vma(tlb, vma);
}


static void unmap_single_vma(struct mmu_gather *tlb,
		struct vm_area_struct *vma, unsigned long start_addr,
		unsigned long end_addr,
		struct zap_details *details)
{
	unsigned long start = max(vma->vm_start, start_addr);
	unsigned long end;

	if (start >= vma->vm_end)
		return;
	end = min(vma->vm_end, end_addr);
	if (end <= vma->vm_start)
		return;

	if (vma->vm_file)
		uprobe_munmap(vma, start, end);

	if (unlikely(vma->vm_flags & VM_PFNMAP))
		untrack_pfn(vma, 0, 0);

	if (start != end) {
		if (unlikely(is_vm_hugetlb_page(vma))) {
			/*
			 * It is undesirable to test vma->vm_file as it
			 * should be non-null for valid hugetlb area.
			 * However, vm_file will be NULL in the error
			 * cleanup path of mmap_region. When
			 * hugetlbfs ->mmap method fails,
			 * mmap_region() nullifies vma->vm_file
			 * before calling this function to clean up.
			 * Since no pte has actually been setup, it is
			 * safe to do nothing in this case.
			 */
			if (vma->vm_file) {
				i_mmap_lock_write(vma->vm_file->f_mapping);
				__unmap_hugepage_range_final(tlb, vma, start, end, NULL);
				i_mmap_unlock_write(vma->vm_file->f_mapping);
			}
		} else
			unmap_page_range(tlb, vma, start, end, details);
	}
}

/**
 * unmap_vmas - unmap a range of memory covered by a list of vma's
 * @tlb: address of the caller's struct mmu_gather
 * @vma: the starting vma
 * @start_addr: virtual address at which to start unmapping
 * @end_addr: virtual address at which to end unmapping
 *
 * Unmap all pages in the vma list.
 *
 * Only addresses between `start' and `end' will be unmapped.
 *
 * The VMA list must be sorted in ascending virtual address order.
 *
 * unmap_vmas() assumes that the caller will flush the whole unmapped address
 * range after unmap_vmas() returns.  So the only responsibility here is to
 * ensure that any thus-far unmapped pages are flushed before unmap_vmas()
 * drops the lock and schedules.
 */
unsigned long unmap_vmas(struct mmu_gather **tlbp,
		struct vm_area_struct *vma, unsigned long start_addr,
		unsigned long end_addr, unsigned long *nr_accounted,
		struct zap_details *details)
{
	long zap_work = ZAP_BLOCK_SIZE;
	unsigned long tlb_start = 0;	/* For tlb_finish_mmu */
	int tlb_start_valid = 0;
	unsigned long start = start_addr;
	spinlock_t *i_mmap_lock = details? details->i_mmap_lock: NULL;
	int fullmm = (*tlbp)->fullmm;
	struct mm_struct *mm = vma->vm_mm;

	mmu_notifier_invalidate_range_start(mm, start_addr, end_addr);
	for ( ; vma && vma->vm_start < end_addr; vma = vma->vm_next) {
		unsigned long end;

		start = max(vma->vm_start, start_addr);
		if (start >= vma->vm_end)
			continue;
		end = min(vma->vm_end, end_addr);
		if (end <= vma->vm_start)
			continue;

		if (vma->vm_flags & VM_ACCOUNT)
			*nr_accounted += (end - start) >> PAGE_SHIFT;

		while (start != end) {
			if (!tlb_start_valid) {
				tlb_start = start;
				tlb_start_valid = 1;
			}

			if (unlikely(is_vm_hugetlb_page(vma))) {
				/*
				 * It is undesirable to test vma->vm_file as it
				 * should be non-null for valid hugetlb area.
				 * However, vm_file will be NULL in the error
				 * cleanup path of do_mmap_pgoff. When
				 * hugetlbfs ->mmap method fails,
				 * do_mmap_pgoff() nullifies vma->vm_file
				 * before calling this function to clean up.
				 * Since no pte has actually been setup, it is
				 * safe to do nothing in this case.
				 */
				if (vma->vm_file) {
					unmap_hugepage_range(vma, start, end, NULL);
					zap_work -= (end - start) /
					pages_per_huge_page(hstate_vma(vma));
				}

				start = end;
			} else
				start = unmap_page_range(*tlbp, vma,
						start, end, &zap_work, details);

			if (zap_work > 0) {
				BUG_ON(start != end);
				break;
			}

			tlb_finish_mmu(*tlbp, tlb_start, start);

			if (need_resched() ||
				(i_mmap_lock && spin_needbreak(i_mmap_lock))) {
				if (i_mmap_lock) {
					*tlbp = NULL;
					goto out;
				}
				cond_resched();
			}

			*tlbp = tlb_gather_mmu(vma->vm_mm, fullmm);
			tlb_start_valid = 0;
			zap_work = ZAP_BLOCK_SIZE;
		}
	}
out:
	mmu_notifier_invalidate_range_end(mm, start_addr, end_addr);
	return start;	/* which is now the end (or restart) address */
void unmap_vmas(struct mmu_gather *tlb,
		struct vm_area_struct *vma, unsigned long start_addr,
		unsigned long end_addr)
{
	struct mm_struct *mm = vma->vm_mm;

	mmu_notifier_invalidate_range_start(mm, start_addr, end_addr);
	for ( ; vma && vma->vm_start < end_addr; vma = vma->vm_next)
		unmap_single_vma(tlb, vma, start_addr, end_addr, NULL);
	mmu_notifier_invalidate_range_end(mm, start_addr, end_addr);
}

/**
 * zap_page_range - remove user pages in a given range
 * @vma: vm_area_struct holding the applicable pages
 * @address: starting address of pages to zap
 * @size: number of bytes to zap
 * @details: details of nonlinear truncation or shared cache invalidation
 */
unsigned long zap_page_range(struct vm_area_struct *vma, unsigned long address,
		unsigned long size, struct zap_details *details)
{
	struct mm_struct *mm = vma->vm_mm;
	struct mmu_gather *tlb;
	unsigned long end = address + size;
	unsigned long nr_accounted = 0;

	lru_add_drain();
	tlb = tlb_gather_mmu(mm, 0);
	update_hiwater_rss(mm);
	end = unmap_vmas(&tlb, vma, address, end, &nr_accounted, details);
	if (tlb)
		tlb_finish_mmu(tlb, address, end);
	return end;
 * @start: starting address of pages to zap
 * @size: number of bytes to zap
 * @details: details of shared cache invalidation
 *
 * Caller must protect the VMA list
 */
void zap_page_range(struct vm_area_struct *vma, unsigned long start,
		unsigned long size, struct zap_details *details)
{
	struct mm_struct *mm = vma->vm_mm;
	struct mmu_gather tlb;
	unsigned long end = start + size;

	lru_add_drain();
	tlb_gather_mmu(&tlb, mm, start, end);
	update_hiwater_rss(mm);
	mmu_notifier_invalidate_range_start(mm, start, end);
	for ( ; vma && vma->vm_start < end; vma = vma->vm_next)
		unmap_single_vma(&tlb, vma, start, end, details);
	mmu_notifier_invalidate_range_end(mm, start, end);
	tlb_finish_mmu(&tlb, start, end);
}

/**
 * zap_page_range_single - remove user pages in a given range
 * @vma: vm_area_struct holding the applicable pages
 * @address: starting address of pages to zap
 * @size: number of bytes to zap
 * @details: details of shared cache invalidation
 *
 * The range must fit into one VMA.
 */
static void zap_page_range_single(struct vm_area_struct *vma, unsigned long address,
		unsigned long size, struct zap_details *details)
{
	struct mm_struct *mm = vma->vm_mm;
	struct mmu_gather tlb;
	unsigned long end = address + size;

	lru_add_drain();
	tlb_gather_mmu(&tlb, mm, address, end);
	update_hiwater_rss(mm);
	mmu_notifier_invalidate_range_start(mm, address, end);
	unmap_single_vma(&tlb, vma, address, end, details);
	mmu_notifier_invalidate_range_end(mm, address, end);
	tlb_finish_mmu(&tlb, address, end);
}

/**
 * zap_vma_ptes - remove ptes mapping the vma
 * @vma: vm_area_struct holding ptes to be zapped
 * @address: starting address of pages to zap
 * @size: number of bytes to zap
 *
 * This function only unmaps ptes assigned to VM_PFNMAP vmas.
 *
 * The entire address range must be fully contained within the vma.
 *
 * Returns 0 if successful.
 */
int zap_vma_ptes(struct vm_area_struct *vma, unsigned long address,
		unsigned long size)
{
	if (address < vma->vm_start || address + size > vma->vm_end ||
	    		!(vma->vm_flags & VM_PFNMAP))
		return -1;
	zap_page_range(vma, address, size, NULL);
	zap_page_range_single(vma, address, size, NULL);
	return 0;
}
EXPORT_SYMBOL_GPL(zap_vma_ptes);

/*
 * Do a quick page-table lookup for a single page.
 */
struct page *follow_page(struct vm_area_struct *vma, unsigned long address,
			unsigned int flags)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *ptep, pte;
	spinlock_t *ptl;
	struct page *page;
	struct mm_struct *mm = vma->vm_mm;

	page = follow_huge_addr(mm, address, flags & FOLL_WRITE);
	if (!IS_ERR(page)) {
		BUG_ON(flags & FOLL_GET);
		goto out;
	}

	page = NULL;
	pgd = pgd_offset(mm, address);
	if (pgd_none(*pgd) || unlikely(pgd_bad(*pgd)))
		goto no_page_table;

	pud = pud_offset(pgd, address);
	if (pud_none(*pud))
		goto no_page_table;
	if (pud_huge(*pud)) {
		BUG_ON(flags & FOLL_GET);
		page = follow_huge_pud(mm, address, pud, flags & FOLL_WRITE);
		goto out;
	}
	if (unlikely(pud_bad(*pud)))
		goto no_page_table;

	pmd = pmd_offset(pud, address);
	if (pmd_none(*pmd))
		goto no_page_table;
	if (pmd_huge(*pmd)) {
		BUG_ON(flags & FOLL_GET);
		page = follow_huge_pmd(mm, address, pmd, flags & FOLL_WRITE);
		goto out;
	}
	if (unlikely(pmd_bad(*pmd)))
		goto no_page_table;

	ptep = pte_offset_map_lock(mm, pmd, address, &ptl);

	pte = *ptep;
	if (!pte_present(pte))
		goto no_page;
	if ((flags & FOLL_WRITE) && !pte_write(pte))
		goto unlock;
	page = vm_normal_page(vma, address, pte);
	if (unlikely(!page))
		goto bad_page;

	if (flags & FOLL_GET)
		get_page(page);
	if (flags & FOLL_TOUCH) {
		if ((flags & FOLL_WRITE) &&
		    !pte_dirty(pte) && !PageDirty(page))
			set_page_dirty(page);
		mark_page_accessed(page);
	}
unlock:
	pte_unmap_unlock(ptep, ptl);
out:
	return page;

bad_page:
	pte_unmap_unlock(ptep, ptl);
	return ERR_PTR(-EFAULT);

no_page:
	pte_unmap_unlock(ptep, ptl);
	if (!pte_none(pte))
		return page;
	/* Fall through to ZERO_PAGE handling */
no_page_table:
	/*
	 * When core dumping an enormous anonymous area that nobody
	 * has touched so far, we don't want to allocate page tables.
	 */
	if (flags & FOLL_ANON) {
		page = ZERO_PAGE(0);
		if (flags & FOLL_GET)
			get_page(page);
		BUG_ON(flags & FOLL_WRITE);
	}
	return page;
}

/* Can we do the FOLL_ANON optimization? */
static inline int use_zero_page(struct vm_area_struct *vma)
{
	/*
	 * We don't want to optimize FOLL_ANON for make_pages_present()
	 * when it tries to page in a VM_LOCKED region. As to VM_SHARED,
	 * we want to get the page from the page tables to make sure
	 * that we serialize and update with any other user of that
	 * mapping.
	 */
	if (vma->vm_flags & (VM_LOCKED | VM_SHARED))
		return 0;
	/*
	 * And if we have a fault routine, it's not an anonymous region.
	 */
	return !vma->vm_ops || !vma->vm_ops->fault;
}

int get_user_pages(struct task_struct *tsk, struct mm_struct *mm,
		unsigned long start, int len, int write, int force,
		struct page **pages, struct vm_area_struct **vmas)
{
	int i;
	unsigned int vm_flags;

	if (len <= 0)
		return 0;
	/* 
	 * Require read or write permissions.
	 * If 'force' is set, we only require the "MAY" flags.
	 */
	vm_flags  = write ? (VM_WRITE | VM_MAYWRITE) : (VM_READ | VM_MAYREAD);
	vm_flags &= force ? (VM_MAYREAD | VM_MAYWRITE) : (VM_READ | VM_WRITE);
	i = 0;

	do {
		struct vm_area_struct *vma;
		unsigned int foll_flags;

		vma = find_extend_vma(mm, start);
		if (!vma && in_gate_area(tsk, start)) {
			unsigned long pg = start & PAGE_MASK;
			struct vm_area_struct *gate_vma = get_gate_vma(tsk);
			pgd_t *pgd;
			pud_t *pud;
			pmd_t *pmd;
			pte_t *pte;
			if (write) /* user gate pages are read-only */
				return i ? : -EFAULT;
			if (pg > TASK_SIZE)
				pgd = pgd_offset_k(pg);
			else
				pgd = pgd_offset_gate(mm, pg);
			BUG_ON(pgd_none(*pgd));
			pud = pud_offset(pgd, pg);
			BUG_ON(pud_none(*pud));
			pmd = pmd_offset(pud, pg);
			if (pmd_none(*pmd))
				return i ? : -EFAULT;
			pte = pte_offset_map(pmd, pg);
			if (pte_none(*pte)) {
				pte_unmap(pte);
				return i ? : -EFAULT;
			}
			if (pages) {
				struct page *page = vm_normal_page(gate_vma, start, *pte);
				pages[i] = page;
				if (page)
					get_page(page);
			}
			pte_unmap(pte);
			if (vmas)
				vmas[i] = gate_vma;
			i++;
			start += PAGE_SIZE;
			len--;
			continue;
		}

		if (!vma || (vma->vm_flags & (VM_IO | VM_PFNMAP))
				|| !(vm_flags & vma->vm_flags))
			return i ? : -EFAULT;

		if (is_vm_hugetlb_page(vma)) {
			i = follow_hugetlb_page(mm, vma, pages, vmas,
						&start, &len, i, write);
			continue;
		}

		foll_flags = FOLL_TOUCH;
		if (pages)
			foll_flags |= FOLL_GET;
		if (!write && use_zero_page(vma))
			foll_flags |= FOLL_ANON;

		do {
			struct page *page;

			/*
			 * If tsk is ooming, cut off its access to large memory
			 * allocations. It has a pending SIGKILL, but it can't
			 * be processed until returning to user space.
			 */
			if (unlikely(test_tsk_thread_flag(tsk, TIF_MEMDIE)))
				return i ? i : -ENOMEM;

			if (write)
				foll_flags |= FOLL_WRITE;

			cond_resched();
			while (!(page = follow_page(vma, start, foll_flags))) {
				int ret;
				ret = handle_mm_fault(mm, vma, start,
						foll_flags & FOLL_WRITE);
				if (ret & VM_FAULT_ERROR) {
					if (ret & VM_FAULT_OOM)
						return i ? i : -ENOMEM;
					else if (ret & VM_FAULT_SIGBUS)
						return i ? i : -EFAULT;
					BUG();
				}
				if (ret & VM_FAULT_MAJOR)
					tsk->maj_flt++;
				else
					tsk->min_flt++;

				/*
				 * The VM_FAULT_WRITE bit tells us that
				 * do_wp_page has broken COW when necessary,
				 * even if maybe_mkwrite decided not to set
				 * pte_write. We can thus safely do subsequent
				 * page lookups as if they were reads.
				 */
				if (ret & VM_FAULT_WRITE)
					foll_flags &= ~FOLL_WRITE;

				cond_resched();
			}
			if (IS_ERR(page))
				return i ? i : PTR_ERR(page);
			if (pages) {
				pages[i] = page;

				flush_anon_page(vma, page, start);
				flush_dcache_page(page);
			}
			if (vmas)
				vmas[i] = vma;
			i++;
			start += PAGE_SIZE;
			len--;
		} while (len && start < vma->vm_end);
	} while (len);
	return i;
}
EXPORT_SYMBOL(get_user_pages);

pte_t *get_locked_pte(struct mm_struct *mm, unsigned long addr,
pte_t *__get_locked_pte(struct mm_struct *mm, unsigned long addr,
			spinlock_t **ptl)
{
	pgd_t * pgd = pgd_offset(mm, addr);
	pud_t * pud = pud_alloc(mm, pgd, addr);
	if (pud) {
		pmd_t * pmd = pmd_alloc(mm, pud, addr);
		if (pmd)
			return pte_alloc_map_lock(mm, pmd, addr, ptl);
		if (pmd) {
			VM_BUG_ON(pmd_trans_huge(*pmd));
			return pte_alloc_map_lock(mm, pmd, addr, ptl);
		}
	}
	return NULL;
}

/*
 * This is the old fallback for page remapping.
 *
 * For historical reasons, it only allows reserved pages. Only
 * old drivers should use this, and they needed to mark their
 * pages reserved for the old functions anyway.
 */
static int insert_page(struct vm_area_struct *vma, unsigned long addr,
			struct page *page, pgprot_t prot)
{
	struct mm_struct *mm = vma->vm_mm;
	int retval;
	pte_t *pte;
	spinlock_t *ptl;

	retval = mem_cgroup_charge(page, mm, GFP_KERNEL);
	if (retval)
		goto out;

	retval = -EINVAL;
	if (PageAnon(page))
		goto out_uncharge;
	retval = -EINVAL;
	if (PageAnon(page))
		goto out;
	retval = -ENOMEM;
	flush_dcache_page(page);
	pte = get_locked_pte(mm, addr, &ptl);
	if (!pte)
		goto out_uncharge;
		goto out;
	retval = -EBUSY;
	if (!pte_none(*pte))
		goto out_unlock;

	/* Ok, finally just insert the thing.. */
	get_page(page);
	inc_mm_counter(mm, file_rss);
	inc_mm_counter_fast(mm, MM_FILEPAGES);
	page_add_file_rmap(page);
	set_pte_at(mm, addr, pte, mk_pte(page, prot));

	retval = 0;
	pte_unmap_unlock(pte, ptl);
	return retval;
out_unlock:
	pte_unmap_unlock(pte, ptl);
out_uncharge:
	mem_cgroup_uncharge_page(page);
out:
	return retval;
}

/**
 * vm_insert_page - insert single page into user vma
 * @vma: user vma to map to
 * @addr: target user address of this page
 * @page: source kernel page
 *
 * This allows drivers to insert individual pages they've allocated
 * into a user vma.
 *
 * The page has to be a nice clean _individual_ kernel allocation.
 * If you allocate a compound page, you need to have marked it as
 * such (__GFP_COMP), or manually just split the page up yourself
 * (see split_page()).
 *
 * NOTE! Traditionally this was done with "remap_pfn_range()" which
 * took an arbitrary page protection parameter. This doesn't allow
 * that. Your vma protection will have to be set up correctly, which
 * means that if you want a shared writable mapping, you'd better
 * ask for a shared writable mapping!
 *
 * The page does not need to be reserved.
 *
 * Usually this function is called from f_op->mmap() handler
 * under mm->mmap_sem write-lock, so it can change vma->vm_flags.
 * Caller must set VM_MIXEDMAP on vma if it wants to call this
 * function from other places, for example from page-fault handler.
 */
int vm_insert_page(struct vm_area_struct *vma, unsigned long addr,
			struct page *page)
{
	if (addr < vma->vm_start || addr >= vma->vm_end)
		return -EFAULT;
	if (!page_count(page))
		return -EINVAL;
	vma->vm_flags |= VM_INSERTPAGE;
	if (!(vma->vm_flags & VM_MIXEDMAP)) {
		BUG_ON(down_read_trylock(&vma->vm_mm->mmap_sem));
		BUG_ON(vma->vm_flags & VM_PFNMAP);
		vma->vm_flags |= VM_MIXEDMAP;
	}
	return insert_page(vma, addr, page, vma->vm_page_prot);
}
EXPORT_SYMBOL(vm_insert_page);

static int insert_pfn(struct vm_area_struct *vma, unsigned long addr,
			unsigned long pfn, pgprot_t prot)
{
	struct mm_struct *mm = vma->vm_mm;
	int retval;
	pte_t *pte, entry;
	spinlock_t *ptl;

	retval = -ENOMEM;
	pte = get_locked_pte(mm, addr, &ptl);
	if (!pte)
		goto out;
	retval = -EBUSY;
	if (!pte_none(*pte))
		goto out_unlock;

	/* Ok, finally just insert the thing.. */
	entry = pte_mkspecial(pfn_pte(pfn, prot));
	set_pte_at(mm, addr, pte, entry);
	update_mmu_cache(vma, addr, entry); /* XXX: why not for insert_page? */
	update_mmu_cache(vma, addr, pte); /* XXX: why not for insert_page? */

	retval = 0;
out_unlock:
	pte_unmap_unlock(pte, ptl);
out:
	return retval;
}

/**
 * vm_insert_pfn - insert single pfn into user vma
 * @vma: user vma to map to
 * @addr: target user address of this page
 * @pfn: source kernel pfn
 *
 * Similar to vm_inert_page, this allows drivers to insert individual pages
 * Similar to vm_insert_page, this allows drivers to insert individual pages
 * they've allocated into a user vma. Same comments apply.
 *
 * This function should only be called from a vm_ops->fault handler, and
 * in that case the handler should return NULL.
 *
 * vma cannot be a COW mapping.
 *
 * As this is called only for pages that do not currently exist, we
 * do not need to flush old virtual caches or the TLB.
 */
int vm_insert_pfn(struct vm_area_struct *vma, unsigned long addr,
			unsigned long pfn)
{
	return vm_insert_pfn_prot(vma, addr, pfn, vma->vm_page_prot);
}
EXPORT_SYMBOL(vm_insert_pfn);

/**
 * vm_insert_pfn_prot - insert single pfn into user vma with specified pgprot
 * @vma: user vma to map to
 * @addr: target user address of this page
 * @pfn: source kernel pfn
 * @pgprot: pgprot flags for the inserted page
 *
 * This is exactly like vm_insert_pfn, except that it allows drivers to
 * to override pgprot on a per-page basis.
 *
 * This only makes sense for IO mappings, and it makes no sense for
 * cow mappings.  In general, using multiple vmas is preferable;
 * vm_insert_pfn_prot should only be used if using multiple VMAs is
 * impractical.
 */
int vm_insert_pfn_prot(struct vm_area_struct *vma, unsigned long addr,
			unsigned long pfn, pgprot_t pgprot)
{
	int ret;
	/*
	 * Technically, architectures with pte_special can avoid all these
	 * restrictions (same for remap_pfn_range).  However we would like
	 * consistency in testing and feature parity among all, so we should
	 * try to keep these invariants in place for everybody.
	 */
	BUG_ON(!(vma->vm_flags & (VM_PFNMAP|VM_MIXEDMAP)));
	BUG_ON((vma->vm_flags & (VM_PFNMAP|VM_MIXEDMAP)) ==
						(VM_PFNMAP|VM_MIXEDMAP));
	BUG_ON((vma->vm_flags & VM_PFNMAP) && is_cow_mapping(vma->vm_flags));
	BUG_ON((vma->vm_flags & VM_MIXEDMAP) && pfn_valid(pfn));

	if (addr < vma->vm_start || addr >= vma->vm_end)
		return -EFAULT;
	return insert_pfn(vma, addr, pfn, vma->vm_page_prot);
	if (track_pfn_insert(vma, &pgprot, pfn))
		return -EINVAL;

	if (!pfn_modify_allowed(pfn, pgprot))
		return -EACCES;

	ret = insert_pfn(vma, addr, pfn, pgprot);

	return ret;
}
EXPORT_SYMBOL(vm_insert_pfn_prot);

int vm_insert_mixed(struct vm_area_struct *vma, unsigned long addr,
			unsigned long pfn)
{
	pgprot_t pgprot = vma->vm_page_prot;

	BUG_ON(!(vma->vm_flags & VM_MIXEDMAP));

	if (addr < vma->vm_start || addr >= vma->vm_end)
		return -EFAULT;
	if (track_pfn_insert(vma, &pgprot, pfn))
		return -EINVAL;

	if (!pfn_modify_allowed(pfn, pgprot))
		return -EACCES;

	/*
	 * If we don't have pte special, then we have to use the pfn_valid()
	 * based VM_MIXEDMAP scheme (see vm_normal_page), and thus we *must*
	 * refcount the page if pfn_valid is true (hence insert_page rather
	 * than insert_pfn).
	 * than insert_pfn).  If a zero_pfn were inserted into a VM_MIXEDMAP
	 * without pte special, it would there be refcounted as a normal page.
	 */
	if (!HAVE_PTE_SPECIAL && pfn_valid(pfn)) {
		struct page *page;

		page = pfn_to_page(pfn);
		return insert_page(vma, addr, page, pgprot);
	}
	return insert_pfn(vma, addr, pfn, pgprot);
}
EXPORT_SYMBOL(vm_insert_mixed);

/*
 * maps a range of physical memory into the requested pages. the old
 * mappings are removed. any references to nonexistent pages results
 * in null mappings (currently treated as "copy-on-access")
 */
static int remap_pte_range(struct mm_struct *mm, pmd_t *pmd,
			unsigned long addr, unsigned long end,
			unsigned long pfn, pgprot_t prot)
{
	pte_t *pte, *mapped_pte;
	spinlock_t *ptl;
	int err = 0;

	mapped_pte = pte = pte_alloc_map_lock(mm, pmd, addr, &ptl);
	if (!pte)
		return -ENOMEM;
	arch_enter_lazy_mmu_mode();
	do {
		BUG_ON(!pte_none(*pte));
		if (!pfn_modify_allowed(pfn, prot)) {
			err = -EACCES;
			break;
		}
		set_pte_at(mm, addr, pte, pte_mkspecial(pfn_pte(pfn, prot)));
		pfn++;
	} while (pte++, addr += PAGE_SIZE, addr != end);
	arch_leave_lazy_mmu_mode();
	pte_unmap_unlock(mapped_pte, ptl);
	return err;
}

static inline int remap_pmd_range(struct mm_struct *mm, pud_t *pud,
			unsigned long addr, unsigned long end,
			unsigned long pfn, pgprot_t prot)
{
	pmd_t *pmd;
	unsigned long next;
	int err;

	pfn -= addr >> PAGE_SHIFT;
	pmd = pmd_alloc(mm, pud, addr);
	if (!pmd)
		return -ENOMEM;
	VM_BUG_ON(pmd_trans_huge(*pmd));
	do {
		next = pmd_addr_end(addr, end);
		err = remap_pte_range(mm, pmd, addr, next,
				pfn + (addr >> PAGE_SHIFT), prot);
		if (err)
			return err;
	} while (pmd++, addr = next, addr != end);
	return 0;
}

static inline int remap_pud_range(struct mm_struct *mm, pgd_t *pgd,
			unsigned long addr, unsigned long end,
			unsigned long pfn, pgprot_t prot)
{
	pud_t *pud;
	unsigned long next;
	int err;

	pfn -= addr >> PAGE_SHIFT;
	pud = pud_alloc(mm, pgd, addr);
	if (!pud)
		return -ENOMEM;
	do {
		next = pud_addr_end(addr, end);
		err = remap_pmd_range(mm, pud, addr, next,
				pfn + (addr >> PAGE_SHIFT), prot);
		if (err)
			return err;
	} while (pud++, addr = next, addr != end);
	return 0;
}

/**
 * remap_pfn_range - remap kernel memory to userspace
 * @vma: user vma to map to
 * @addr: target user address to start at
 * @pfn: physical address of kernel memory
 * @size: size of map area
 * @prot: page protection flags for this mapping
 *
 *  Note: this is only safe if the mm semaphore is held when called.
 */
int remap_pfn_range(struct vm_area_struct *vma, unsigned long addr,
		    unsigned long pfn, unsigned long size, pgprot_t prot)
{
	pgd_t *pgd;
	unsigned long next;
	unsigned long end = addr + PAGE_ALIGN(size);
	struct mm_struct *mm = vma->vm_mm;
	int err;

	/*
	 * Physically remapped pages are special. Tell the
	 * rest of the world about it:
	 *   VM_IO tells people not to look at these pages
	 *	(accesses can have side effects).
	 *   VM_RESERVED is specified all over the place, because
	 *	in 2.4 it kept swapout's vma scan off this vma; but
	 *	in 2.6 the LRU scan won't even find its pages, so this
	 *	flag means no more than count its pages in reserved_vm,
	 * 	and omit it from core dump, even when VM_IO turned off.
	 *   VM_PFNMAP tells the core MM that the base pages are just
	 *	raw PFN mappings, and do not have a "struct page" associated
	 *	with them.
	 *   VM_PFNMAP tells the core MM that the base pages are just
	 *	raw PFN mappings, and do not have a "struct page" associated
	 *	with them.
	 *   VM_DONTEXPAND
	 *      Disable vma merging and expanding with mremap().
	 *   VM_DONTDUMP
	 *      Omit vma from core dump, even when VM_IO turned off.
	 *
	 * There's a horrible special case to handle copy-on-write
	 * behaviour that some programs depend on. We mark the "original"
	 * un-COW'ed pages by matching them up with "vma->vm_pgoff".
	 * See vm_normal_page() for details.
	 */
	if (is_cow_mapping(vma->vm_flags)) {
		if (addr != vma->vm_start || end != vma->vm_end)
			return -EINVAL;
		vma->vm_pgoff = pfn;
	}

	vma->vm_flags |= VM_IO | VM_RESERVED | VM_PFNMAP;
	err = track_pfn_remap(vma, &prot, pfn, addr, PAGE_ALIGN(size));
	if (err)
		return -EINVAL;

	vma->vm_flags |= VM_IO | VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP;

	BUG_ON(addr >= end);
	pfn -= addr >> PAGE_SHIFT;
	pgd = pgd_offset(mm, addr);
	flush_cache_range(vma, addr, end);
	do {
		next = pgd_addr_end(addr, end);
		err = remap_pud_range(mm, pgd, addr, next,
				pfn + (addr >> PAGE_SHIFT), prot);
		if (err)
			break;
	} while (pgd++, addr = next, addr != end);

	if (err)
		untrack_pfn(vma, pfn, PAGE_ALIGN(size));

	return err;
}
EXPORT_SYMBOL(remap_pfn_range);

/**
 * vm_iomap_memory - remap memory to userspace
 * @vma: user vma to map to
 * @start: start of area
 * @len: size of area
 *
 * This is a simplified io_remap_pfn_range() for common driver use. The
 * driver just needs to give us the physical memory range to be mapped,
 * we'll figure out the rest from the vma information.
 *
 * NOTE! Some drivers might want to tweak vma->vm_page_prot first to get
 * whatever write-combining details or similar.
 */
int vm_iomap_memory(struct vm_area_struct *vma, phys_addr_t start, unsigned long len)
{
	unsigned long vm_len, pfn, pages;

	/* Check that the physical memory area passed in looks valid */
	if (start + len < start)
		return -EINVAL;
	/*
	 * You *really* shouldn't map things that aren't page-aligned,
	 * but we've historically allowed it because IO memory might
	 * just have smaller alignment.
	 */
	len += start & ~PAGE_MASK;
	pfn = start >> PAGE_SHIFT;
	pages = (len + ~PAGE_MASK) >> PAGE_SHIFT;
	if (pfn + pages < pfn)
		return -EINVAL;

	/* We start the mapping 'vm_pgoff' pages into the area */
	if (vma->vm_pgoff > pages)
		return -EINVAL;
	pfn += vma->vm_pgoff;
	pages -= vma->vm_pgoff;

	/* Can we fit all of the mapping? */
	vm_len = vma->vm_end - vma->vm_start;
	if (vm_len >> PAGE_SHIFT > pages)
		return -EINVAL;

	/* Ok, let it rip */
	return io_remap_pfn_range(vma, vma->vm_start, pfn, vm_len, vma->vm_page_prot);
}
EXPORT_SYMBOL(vm_iomap_memory);

static int apply_to_pte_range(struct mm_struct *mm, pmd_t *pmd,
				     unsigned long addr, unsigned long end,
				     pte_fn_t fn, void *data)
{
	pte_t *pte;
	int err;
	pgtable_t token;
	spinlock_t *uninitialized_var(ptl);

	pte = (mm == &init_mm) ?
		pte_alloc_kernel(pmd, addr) :
		pte_alloc_map_lock(mm, pmd, addr, &ptl);
	if (!pte)
		return -ENOMEM;

	BUG_ON(pmd_huge(*pmd));

	token = pmd_pgtable(*pmd);

	do {
		err = fn(pte, token, addr, data);
		if (err)
			break;
	} while (pte++, addr += PAGE_SIZE, addr != end);
	arch_enter_lazy_mmu_mode();

	token = pmd_pgtable(*pmd);

	do {
		err = fn(pte++, token, addr, data);
		if (err)
			break;
	} while (addr += PAGE_SIZE, addr != end);

	arch_leave_lazy_mmu_mode();

	if (mm != &init_mm)
		pte_unmap_unlock(pte-1, ptl);
	return err;
}

static int apply_to_pmd_range(struct mm_struct *mm, pud_t *pud,
				     unsigned long addr, unsigned long end,
				     pte_fn_t fn, void *data)
{
	pmd_t *pmd;
	unsigned long next;
	int err;

	BUG_ON(pud_huge(*pud));

	pmd = pmd_alloc(mm, pud, addr);
	if (!pmd)
		return -ENOMEM;
	do {
		next = pmd_addr_end(addr, end);
		err = apply_to_pte_range(mm, pmd, addr, next, fn, data);
		if (err)
			break;
	} while (pmd++, addr = next, addr != end);
	return err;
}

static int apply_to_pud_range(struct mm_struct *mm, pgd_t *pgd,
				     unsigned long addr, unsigned long end,
				     pte_fn_t fn, void *data)
{
	pud_t *pud;
	unsigned long next;
	int err;

	pud = pud_alloc(mm, pgd, addr);
	if (!pud)
		return -ENOMEM;
	do {
		next = pud_addr_end(addr, end);
		err = apply_to_pmd_range(mm, pud, addr, next, fn, data);
		if (err)
			break;
	} while (pud++, addr = next, addr != end);
	return err;
}

/*
 * Scan a region of virtual memory, filling in page tables as necessary
 * and calling a provided function on each leaf page table.
 */
int apply_to_page_range(struct mm_struct *mm, unsigned long addr,
			unsigned long size, pte_fn_t fn, void *data)
{
	pgd_t *pgd;
	unsigned long next;
	unsigned long start = addr, end = addr + size;
	int err;

	BUG_ON(addr >= end);
	mmu_notifier_invalidate_range_start(mm, start, end);
	unsigned long end = addr + size;
	int err;

	BUG_ON(addr >= end);
	pgd = pgd_offset(mm, addr);
	do {
		next = pgd_addr_end(addr, end);
		err = apply_to_pud_range(mm, pgd, addr, next, fn, data);
		if (err)
			break;
	} while (pgd++, addr = next, addr != end);
	mmu_notifier_invalidate_range_end(mm, start, end);

	return err;
}
EXPORT_SYMBOL_GPL(apply_to_page_range);

/*
 * handle_pte_fault chooses page fault handler according to an entry
 * which was read non-atomically.  Before making any commitment, on
 * those architectures or configurations (e.g. i386 with PAE) which
 * might give a mix of unmatched parts, do_swap_page and do_file_page
 * must check under lock before unmapping the pte and proceeding
 * (but do_wp_page is only called after already making such a check;
 * and do_anonymous_page and do_no_page can safely check later on).
 * handle_pte_fault chooses page fault handler according to an entry which was
 * read non-atomically.  Before making any commitment, on those architectures
 * or configurations (e.g. i386 with PAE) which might give a mix of unmatched
 * parts, do_swap_page must check under lock before unmapping the pte and
 * proceeding (but do_wp_page is only called after already making such a check;
 * and do_anonymous_page can safely check later on).
 */
static inline int pte_unmap_same(struct mm_struct *mm, pmd_t *pmd,
				pte_t *page_table, pte_t orig_pte)
{
	int same = 1;
#if defined(CONFIG_SMP) || defined(CONFIG_PREEMPT)
	if (sizeof(pte_t) > sizeof(unsigned long)) {
		spinlock_t *ptl = pte_lockptr(mm, pmd);
		spin_lock(ptl);
		same = pte_same(*page_table, orig_pte);
		spin_unlock(ptl);
	}
#endif
	pte_unmap(page_table);
	return same;
}

/*
 * Do pte_mkwrite, but only if the vma says VM_WRITE.  We do this when
 * servicing faults for write access.  In the normal case, do always want
 * pte_mkwrite.  But get_user_pages can cause write faults for mappings
 * that do not have writing enabled, when used by access_process_vm.
 */
static inline pte_t maybe_mkwrite(pte_t pte, struct vm_area_struct *vma)
{
	if (likely(vma->vm_flags & VM_WRITE))
		pte = pte_mkwrite(pte);
	return pte;
}

static inline void cow_user_page(struct page *dst, struct page *src, unsigned long va, struct vm_area_struct *vma)
{
static inline void cow_user_page(struct page *dst, struct page *src, unsigned long va, struct vm_area_struct *vma)
{
	debug_dma_assert_idle(src);

	/*
	 * If the source page was a PFN mapping, we don't have
	 * a "struct page" for it. We do a best-effort copy by
	 * just copying from the original user address. If that
	 * fails, we just zero-fill it. Live with it.
	 */
	if (unlikely(!src)) {
		void *kaddr = kmap_atomic(dst, KM_USER0);
		void *kaddr = kmap_atomic(dst);
		void __user *uaddr = (void __user *)(va & PAGE_MASK);

		/*
		 * This really shouldn't fail, because the page is there
		 * in the page tables. But it might just be unreadable,
		 * in which case we just give up and fill the result with
		 * zeroes.
		 */
		if (__copy_from_user_inatomic(kaddr, uaddr, PAGE_SIZE))
			memset(kaddr, 0, PAGE_SIZE);
		kunmap_atomic(kaddr, KM_USER0);
			clear_page(kaddr);
		kunmap_atomic(kaddr);
		flush_dcache_page(dst);
	} else
		copy_user_highpage(dst, src, va, vma);
}

static gfp_t __get_fault_gfp_mask(struct vm_area_struct *vma)
{
	struct file *vm_file = vma->vm_file;

	if (vm_file)
		return mapping_gfp_mask(vm_file->f_mapping) | __GFP_FS | __GFP_IO;

	/*
	 * Special mappings (e.g. VDSO) do not have any file so fake
	 * a default GFP_KERNEL for them.
	 */
	return GFP_KERNEL;
}

/*
 * This routine handles present pages, when users try to write
 * to a shared page. It is done by copying the page to a new address
 * and decrementing the shared-page counter for the old page.
 *
 * Note that this routine assumes that the protection checks have been
 * done by the caller (the low-level page fault routine in most cases).
 * Thus we can safely just mark it writable once we've done any necessary
 * COW.
 *
 * We also mark the page dirty at this point even though the page will
 * change only once the write actually happens. This avoids a few races,
 * and potentially makes it more efficient.
 *
 * We enter with non-exclusive mmap_sem (to exclude vma changes,
 * but allow concurrent faults), with pte both mapped and locked.
 * We return with mmap_sem still held, but pte unmapped and unlocked.
 */
static int do_wp_page(struct mm_struct *mm, struct vm_area_struct *vma,
		unsigned long address, pte_t *page_table, pmd_t *pmd,
		spinlock_t *ptl, pte_t orig_pte)
{
	struct page *old_page, *new_page;
	pte_t entry;
	int reuse = 0, ret = 0;
	int page_mkwrite = 0;
	struct page *dirty_page = NULL;

	old_page = vm_normal_page(vma, address, orig_pte);
	if (!old_page) {
		/*
		 * VM_MIXEDMAP !pfn_valid() case
		 *
		 * We should not cow pages in a shared writeable mapping.
		 * Just mark the pages writable as we can't do any dirty
		 * accounting on raw pfn maps.
		 */
		if ((vma->vm_flags & (VM_WRITE|VM_SHARED)) ==
				     (VM_WRITE|VM_SHARED))
			goto reuse;
		goto gotten;
	}

	/*
	 * Take out anonymous pages first, anonymous shared vmas are
	 * not dirty accountable.
	 */
	if (PageAnon(old_page)) {
		if (trylock_page(old_page)) {
			reuse = can_share_swap_page(old_page);
			unlock_page(old_page);
		}
	} else if (unlikely((vma->vm_flags & (VM_WRITE|VM_SHARED)) ==
					(VM_WRITE|VM_SHARED))) {
		/*
		 * Only catch write-faults on shared writable pages,
		 * read-only shared pages can get COWed by
		 * get_user_pages(.write=1, .force=1).
		 */
		if (vma->vm_ops && vma->vm_ops->page_mkwrite) {
			/*
			 * Notify the address space that the page is about to
			 * become writable so that it can prohibit this or wait
			 * for the page to get into an appropriate state.
			 *
			 * We do this without the lock held, so that it can
			 * sleep if it needs to.
			 */
			page_cache_get(old_page);
			pte_unmap_unlock(page_table, ptl);

			if (vma->vm_ops->page_mkwrite(vma, old_page) < 0)
				goto unwritable_page;

			/*
			 * Since we dropped the lock we need to revalidate
			 * the PTE as someone else may have changed it.  If
			 * they did, we just return, as we can count on the
			 * MMU to tell us if they didn't also make it writable.
			 */
			page_table = pte_offset_map_lock(mm, pmd, address,
							 &ptl);
			page_cache_release(old_page);
			if (!pte_same(*page_table, orig_pte))
				goto unlock;

			page_mkwrite = 1;
		}
		dirty_page = old_page;
		get_page(dirty_page);
		reuse = 1;
	}

	if (reuse) {
reuse:
		flush_cache_page(vma, address, pte_pfn(orig_pte));
		entry = pte_mkyoung(orig_pte);
		entry = maybe_mkwrite(pte_mkdirty(entry), vma);
		if (ptep_set_access_flags(vma, address, page_table, entry,1))
			update_mmu_cache(vma, address, entry);
		ret |= VM_FAULT_WRITE;
		goto unlock;
	}

	/*
	 * Ok, we need to copy. Oh, well..
	 */
	page_cache_get(old_page);
gotten:
	pte_unmap_unlock(page_table, ptl);

	if (unlikely(anon_vma_prepare(vma)))
		goto oom;
	VM_BUG_ON(old_page == ZERO_PAGE(0));
	new_page = alloc_page_vma(GFP_HIGHUSER_MOVABLE, vma, address);
	if (!new_page)
		goto oom;
	cow_user_page(new_page, old_page, address, vma);
	__SetPageUptodate(new_page);

	if (mem_cgroup_charge(new_page, mm, GFP_KERNEL))
		goto oom_free_new;
 * Notify the address space that the page is about to become writable so that
 * it can prohibit this or wait for the page to get into an appropriate state.
 *
 * We do this without the lock held, so that it can sleep if it needs to.
 */
static int do_page_mkwrite(struct vm_area_struct *vma, struct page *page,
	       unsigned long address)
{
	struct vm_fault vmf;
	int ret;

	vmf.virtual_address = (void __user *)(address & PAGE_MASK);
	vmf.pgoff = page->index;
	vmf.flags = FAULT_FLAG_WRITE|FAULT_FLAG_MKWRITE;
	vmf.gfp_mask = __get_fault_gfp_mask(vma);
	vmf.page = page;
	vmf.cow_page = NULL;

	ret = vma->vm_ops->page_mkwrite(vma, &vmf);
	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE)))
		return ret;
	if (unlikely(!(ret & VM_FAULT_LOCKED))) {
		lock_page(page);
		if (!page->mapping) {
			unlock_page(page);
			return 0; /* retry */
		}
		ret |= VM_FAULT_LOCKED;
	} else
		VM_BUG_ON_PAGE(!PageLocked(page), page);
	return ret;
}

/*
 * Handle write page faults for pages that can be reused in the current vma
 *
 * This can happen either due to the mapping being with the VM_SHARED flag,
 * or due to us being the last reference standing to the page. In either
 * case, all we need to do here is to mark the page as writable and update
 * any related book-keeping.
 */
static inline int wp_page_reuse(struct mm_struct *mm,
			struct vm_area_struct *vma, unsigned long address,
			pte_t *page_table, spinlock_t *ptl, pte_t orig_pte,
			struct page *page, int page_mkwrite,
			int dirty_shared)
	__releases(ptl)
{
	pte_t entry;
	/*
	 * Clear the pages cpupid information as the existing
	 * information potentially belongs to a now completely
	 * unrelated process.
	 */
	if (page)
		page_cpupid_xchg_last(page, (1 << LAST_CPUPID_SHIFT) - 1);

	flush_cache_page(vma, address, pte_pfn(orig_pte));
	entry = pte_mkyoung(orig_pte);
	entry = maybe_mkwrite(pte_mkdirty(entry), vma);
	if (ptep_set_access_flags(vma, address, page_table, entry, 1))
		update_mmu_cache(vma, address, page_table);
	pte_unmap_unlock(page_table, ptl);

	if (dirty_shared) {
		struct address_space *mapping;
		int dirtied;

		if (!page_mkwrite)
			lock_page(page);

		dirtied = set_page_dirty(page);
		VM_BUG_ON_PAGE(PageAnon(page), page);
		mapping = page->mapping;
		unlock_page(page);
		page_cache_release(page);

		if ((dirtied || page_mkwrite) && mapping) {
			/*
			 * Some device drivers do not set page.mapping
			 * but still dirty their pages
			 */
			balance_dirty_pages_ratelimited(mapping);
		}

		if (!page_mkwrite)
			file_update_time(vma->vm_file);
	}

	return VM_FAULT_WRITE;
}

/*
 * Handle the case of a page which we actually need to copy to a new page.
 *
 * Called with mmap_sem locked and the old page referenced, but
 * without the ptl held.
 *
 * High level logic flow:
 *
 * - Allocate a page, copy the content of the old page to the new one.
 * - Handle book keeping and accounting - cgroups, mmu-notifiers, etc.
 * - Take the PTL. If the pte changed, bail out and release the allocated page
 * - If the pte is still the way we remember it, update the page table and all
 *   relevant references. This includes dropping the reference the page-table
 *   held to the old page, as well as updating the rmap.
 * - In any case, unlock the PTL and drop the reference we took to the old page.
 */
static int wp_page_copy(struct mm_struct *mm, struct vm_area_struct *vma,
			unsigned long address, pte_t *page_table, pmd_t *pmd,
			pte_t orig_pte, struct page *old_page)
{
	struct page *new_page = NULL;
	spinlock_t *ptl = NULL;
	pte_t entry;
	int page_copied = 0;
	const unsigned long mmun_start = address & PAGE_MASK;	/* For mmu_notifiers */
	const unsigned long mmun_end = mmun_start + PAGE_SIZE;	/* For mmu_notifiers */
	struct mem_cgroup *memcg;

	if (unlikely(anon_vma_prepare(vma)))
		goto oom;

	if (is_zero_pfn(pte_pfn(orig_pte))) {
		new_page = alloc_zeroed_user_highpage_movable(vma, address);
		if (!new_page)
			goto oom;
	} else {
		new_page = alloc_page_vma(GFP_HIGHUSER_MOVABLE, vma, address);
		if (!new_page)
			goto oom;
		cow_user_page(new_page, old_page, address, vma);
	}

	if (mem_cgroup_try_charge(new_page, mm, GFP_KERNEL, &memcg))
		goto oom_free_new;

	__SetPageUptodate(new_page);

	mmu_notifier_invalidate_range_start(mm, mmun_start, mmun_end);

	/*
	 * Re-check the pte - we dropped the lock
	 */
	page_table = pte_offset_map_lock(mm, pmd, address, &ptl);
	if (likely(pte_same(*page_table, orig_pte))) {
		if (old_page) {
			if (!PageAnon(old_page)) {
				dec_mm_counter(mm, file_rss);
				inc_mm_counter(mm, anon_rss);
			}
		} else
			inc_mm_counter(mm, anon_rss);
				dec_mm_counter_fast(mm, MM_FILEPAGES);
				inc_mm_counter_fast(mm, MM_ANONPAGES);
			}
		} else {
			inc_mm_counter_fast(mm, MM_ANONPAGES);
		}
		flush_cache_page(vma, address, pte_pfn(orig_pte));
		entry = mk_pte(new_page, vma->vm_page_prot);
		entry = maybe_mkwrite(pte_mkdirty(entry), vma);
		/*
		 * Clear the pte entry and flush it first, before updating the
		 * pte with the new entry. This will avoid a race condition
		 * seen in the presence of one thread doing SMC and another
		 * thread doing COW.
		 */
		ptep_clear_flush_notify(vma, address, page_table);
		set_pte_at(mm, address, page_table, entry);
		update_mmu_cache(vma, address, entry);
		lru_cache_add_active(new_page);
		page_add_new_anon_rmap(new_page, vma, address);

		page_add_new_anon_rmap(new_page, vma, address);
		mem_cgroup_commit_charge(new_page, memcg, false);
		lru_cache_add_active_or_unevictable(new_page, vma);
		/*
		 * We call the notify macro here because, when using secondary
		 * mmu page tables (such as kvm shadow page tables), we want the
		 * new page to be mapped directly into the secondary page table.
		 */
		set_pte_at_notify(mm, address, page_table, entry);
		update_mmu_cache(vma, address, page_table);
		if (old_page) {
			/*
			 * Only after switching the pte to the new page may
			 * we remove the mapcount here. Otherwise another
			 * process may come and find the rmap count decremented
			 * before the pte is switched to the new page, and
			 * "reuse" the old page writing into it while our pte
			 * here still points into it and can be read by other
			 * threads.
			 *
			 * The critical issue is to order this
			 * page_remove_rmap with the ptp_clear_flush above.
			 * Those stores are ordered by (if nothing else,)
			 * the barrier present in the atomic_add_negative
			 * in page_remove_rmap.
			 *
			 * Then the TLB flush in ptep_clear_flush ensures that
			 * no process can access the old page before the
			 * decremented mapcount is visible. And the old page
			 * cannot be reused until after the decremented
			 * mapcount is visible. So transitively, TLBs to
			 * old page will be flushed before it can be reused.
			 */
			page_remove_rmap(old_page, vma);
			page_remove_rmap(old_page);
		}

		/* Free the old page.. */
		new_page = old_page;
		ret |= VM_FAULT_WRITE;
	} else
		mem_cgroup_uncharge_page(new_page);

	if (new_page)
		page_cache_release(new_page);
	if (old_page)
		page_cache_release(old_page);
unlock:
	pte_unmap_unlock(page_table, ptl);
	if (dirty_page) {
		if (vma->vm_file)
			file_update_time(vma->vm_file);

		/*
		 * Yes, Virginia, this is actually required to prevent a race
		 * with clear_page_dirty_for_io() from clearing the page dirty
		 * bit after it clear all dirty ptes, but before a racing
		 * do_wp_page installs a dirty pte.
		 *
		 * do_no_page is protected similarly.
		 */
		wait_on_page_locked(dirty_page);
		set_page_dirty_balance(dirty_page, page_mkwrite);
		put_page(dirty_page);
	}
	return ret;
		page_copied = 1;
	} else {
		mem_cgroup_cancel_charge(new_page, memcg);
	}

	if (new_page)
		page_cache_release(new_page);

	pte_unmap_unlock(page_table, ptl);
	mmu_notifier_invalidate_range_end(mm, mmun_start, mmun_end);
	if (old_page) {
		/*
		 * Don't let another task, with possibly unlocked vma,
		 * keep the mlocked page.
		 */
		if (page_copied && (vma->vm_flags & VM_LOCKED)) {
			lock_page(old_page);	/* LRU manipulation */
			munlock_vma_page(old_page);
			unlock_page(old_page);
		}
		page_cache_release(old_page);
	}
	return page_copied ? VM_FAULT_WRITE : 0;
oom_free_new:
	page_cache_release(new_page);
oom:
	if (old_page)
		page_cache_release(old_page);
	return VM_FAULT_OOM;

unwritable_page:
	page_cache_release(old_page);
	return VM_FAULT_SIGBUS;
}

/*
 * Helper functions for unmap_mapping_range().
 *
 * __ Notes on dropping i_mmap_lock to reduce latency while unmapping __
 *
 * We have to restart searching the prio_tree whenever we drop the lock,
 * since the iterator is only valid while the lock is held, and anyway
 * a later vma might be split and reinserted earlier while lock dropped.
 *
 * The list of nonlinear vmas could be handled more efficiently, using
 * a placeholder, but handle it in the same way until a need is shown.
 * It is important to search the prio_tree before nonlinear list: a vma
 * may become nonlinear and be shifted from prio_tree to nonlinear list
 * while the lock is dropped; but never shifted from list to prio_tree.
 *
 * In order to make forward progress despite restarting the search,
 * vm_truncate_count is used to mark a vma as now dealt with, so we can
 * quickly skip it next time around.  Since the prio_tree search only
 * shows us those vmas affected by unmapping the range in question, we
 * can't efficiently keep all vmas in step with mapping->truncate_count:
 * so instead reset them all whenever it wraps back to 0 (then go to 1).
 * mapping->truncate_count and vma->vm_truncate_count are protected by
 * i_mmap_lock.
 *
 * In order to make forward progress despite repeatedly restarting some
 * large vma, note the restart_addr from unmap_vmas when it breaks out:
 * and restart from that address when we reach that vma again.  It might
 * have been split or merged, shrunk or extended, but never shifted: so
 * restart_addr remains valid so long as it remains in the vma's range.
 * unmap_mapping_range forces truncate_count to leap over page-aligned
 * values so we can save vma's restart_addr in its truncate_count field.
 */
#define is_restart_addr(truncate_count) (!((truncate_count) & ~PAGE_MASK))

static void reset_vma_truncate_counts(struct address_space *mapping)
{
	struct vm_area_struct *vma;
	struct prio_tree_iter iter;

	vma_prio_tree_foreach(vma, &iter, &mapping->i_mmap, 0, ULONG_MAX)
		vma->vm_truncate_count = 0;
	list_for_each_entry(vma, &mapping->i_mmap_nonlinear, shared.vm_set.list)
		vma->vm_truncate_count = 0;
}

static int unmap_mapping_range_vma(struct vm_area_struct *vma,
		unsigned long start_addr, unsigned long end_addr,
		struct zap_details *details)
{
	unsigned long restart_addr;
	int need_break;

	/*
	 * files that support invalidating or truncating portions of the
	 * file from under mmaped areas must have their ->fault function
	 * return a locked page (and set VM_FAULT_LOCKED in the return).
	 * This provides synchronisation against concurrent unmapping here.
	 */

again:
	restart_addr = vma->vm_truncate_count;
	if (is_restart_addr(restart_addr) && start_addr < restart_addr) {
		start_addr = restart_addr;
		if (start_addr >= end_addr) {
			/* Top of vma has been split off since last time */
			vma->vm_truncate_count = details->truncate_count;
			return 0;
		}
	}

	restart_addr = zap_page_range(vma, start_addr,
					end_addr - start_addr, details);
	need_break = need_resched() || spin_needbreak(details->i_mmap_lock);

	if (restart_addr >= end_addr) {
		/* We have now completed this vma: mark it so */
		vma->vm_truncate_count = details->truncate_count;
		if (!need_break)
			return 0;
	} else {
		/* Note restart_addr in vma's truncate_count field */
		vma->vm_truncate_count = restart_addr;
		if (!need_break)
			goto again;
	}

	spin_unlock(details->i_mmap_lock);
	cond_resched();
	spin_lock(details->i_mmap_lock);
	return -EINTR;
}

static inline void unmap_mapping_range_tree(struct prio_tree_root *root,
					    struct zap_details *details)
{
	struct vm_area_struct *vma;
	struct prio_tree_iter iter;
	pgoff_t vba, vea, zba, zea;

restart:
	vma_prio_tree_foreach(vma, &iter, root,
			details->first_index, details->last_index) {
		/* Skip quickly over those we have already dealt with */
		if (vma->vm_truncate_count == details->truncate_count)
			continue;

		vba = vma->vm_pgoff;
		vea = vba + ((vma->vm_end - vma->vm_start) >> PAGE_SHIFT) - 1;
}

/*
 * Handle write page faults for VM_MIXEDMAP or VM_PFNMAP for a VM_SHARED
 * mapping
 */
static int wp_pfn_shared(struct mm_struct *mm,
			struct vm_area_struct *vma, unsigned long address,
			pte_t *page_table, spinlock_t *ptl, pte_t orig_pte,
			pmd_t *pmd)
{
	if (vma->vm_ops && vma->vm_ops->pfn_mkwrite) {
		struct vm_fault vmf = {
			.page = NULL,
			.pgoff = linear_page_index(vma, address),
			.virtual_address = (void __user *)(address & PAGE_MASK),
			.flags = FAULT_FLAG_WRITE | FAULT_FLAG_MKWRITE,
		};
		int ret;

		pte_unmap_unlock(page_table, ptl);
		ret = vma->vm_ops->pfn_mkwrite(vma, &vmf);
		if (ret & VM_FAULT_ERROR)
			return ret;
		page_table = pte_offset_map_lock(mm, pmd, address, &ptl);
		/*
		 * We might have raced with another page fault while we
		 * released the pte_offset_map_lock.
		 */
		if (!pte_same(*page_table, orig_pte)) {
			pte_unmap_unlock(page_table, ptl);
			return 0;
		}
	}
	return wp_page_reuse(mm, vma, address, page_table, ptl, orig_pte,
			     NULL, 0, 0);
}

static int wp_page_shared(struct mm_struct *mm, struct vm_area_struct *vma,
			  unsigned long address, pte_t *page_table,
			  pmd_t *pmd, spinlock_t *ptl, pte_t orig_pte,
			  struct page *old_page)
	__releases(ptl)
{
	int page_mkwrite = 0;

	page_cache_get(old_page);

	/*
	 * Only catch write-faults on shared writable pages,
	 * read-only shared pages can get COWed by
	 * get_user_pages(.write=1, .force=1).
	 */
	if (vma->vm_ops && vma->vm_ops->page_mkwrite) {
		int tmp;

		pte_unmap_unlock(page_table, ptl);
		tmp = do_page_mkwrite(vma, old_page, address);
		if (unlikely(!tmp || (tmp &
				      (VM_FAULT_ERROR | VM_FAULT_NOPAGE)))) {
			page_cache_release(old_page);
			return tmp;
		}
		/*
		 * Since we dropped the lock we need to revalidate
		 * the PTE as someone else may have changed it.  If
		 * they did, we just return, as we can count on the
		 * MMU to tell us if they didn't also make it writable.
		 */
		page_table = pte_offset_map_lock(mm, pmd, address,
						 &ptl);
		if (!pte_same(*page_table, orig_pte)) {
			unlock_page(old_page);
			pte_unmap_unlock(page_table, ptl);
			page_cache_release(old_page);
			return 0;
		}
		page_mkwrite = 1;
	}

	return wp_page_reuse(mm, vma, address, page_table, ptl,
			     orig_pte, old_page, page_mkwrite, 1);
}

/*
 * This routine handles present pages, when users try to write
 * to a shared page. It is done by copying the page to a new address
 * and decrementing the shared-page counter for the old page.
 *
 * Note that this routine assumes that the protection checks have been
 * done by the caller (the low-level page fault routine in most cases).
 * Thus we can safely just mark it writable once we've done any necessary
 * COW.
 *
 * We also mark the page dirty at this point even though the page will
 * change only once the write actually happens. This avoids a few races,
 * and potentially makes it more efficient.
 *
 * We enter with non-exclusive mmap_sem (to exclude vma changes,
 * but allow concurrent faults), with pte both mapped and locked.
 * We return with mmap_sem still held, but pte unmapped and unlocked.
 */
static int do_wp_page(struct mm_struct *mm, struct vm_area_struct *vma,
		unsigned long address, pte_t *page_table, pmd_t *pmd,
		spinlock_t *ptl, pte_t orig_pte)
	__releases(ptl)
{
	struct page *old_page;

	old_page = vm_normal_page(vma, address, orig_pte);
	if (!old_page) {
		/*
		 * VM_MIXEDMAP !pfn_valid() case, or VM_SOFTDIRTY clear on a
		 * VM_PFNMAP VMA.
		 *
		 * We should not cow pages in a shared writeable mapping.
		 * Just mark the pages writable and/or call ops->pfn_mkwrite.
		 */
		if ((vma->vm_flags & (VM_WRITE|VM_SHARED)) ==
				     (VM_WRITE|VM_SHARED))
			return wp_pfn_shared(mm, vma, address, page_table, ptl,
					     orig_pte, pmd);

		pte_unmap_unlock(page_table, ptl);
		return wp_page_copy(mm, vma, address, page_table, pmd,
				    orig_pte, old_page);
	}

	/*
	 * Take out anonymous pages first, anonymous shared vmas are
	 * not dirty accountable.
	 */
	if (PageAnon(old_page) && !PageKsm(old_page)) {
		if (!trylock_page(old_page)) {
			page_cache_get(old_page);
			pte_unmap_unlock(page_table, ptl);
			lock_page(old_page);
			page_table = pte_offset_map_lock(mm, pmd, address,
							 &ptl);
			if (!pte_same(*page_table, orig_pte)) {
				unlock_page(old_page);
				pte_unmap_unlock(page_table, ptl);
				page_cache_release(old_page);
				return 0;
			}
			page_cache_release(old_page);
		}
		if (reuse_swap_page(old_page)) {
			/*
			 * The page is all ours.  Move it to our anon_vma so
			 * the rmap code will not search our parent or siblings.
			 * Protected against the rmap code by the page lock.
			 */
			page_move_anon_rmap(old_page, vma, address);
			unlock_page(old_page);
			return wp_page_reuse(mm, vma, address, page_table, ptl,
					     orig_pte, old_page, 0, 0);
		}
		unlock_page(old_page);
	} else if (unlikely((vma->vm_flags & (VM_WRITE|VM_SHARED)) ==
					(VM_WRITE|VM_SHARED))) {
		return wp_page_shared(mm, vma, address, page_table, pmd,
				      ptl, orig_pte, old_page);
	}

	/*
	 * Ok, we need to copy. Oh, well..
	 */
	page_cache_get(old_page);

	pte_unmap_unlock(page_table, ptl);
	return wp_page_copy(mm, vma, address, page_table, pmd,
			    orig_pte, old_page);
}

static void unmap_mapping_range_vma(struct vm_area_struct *vma,
		unsigned long start_addr, unsigned long end_addr,
		struct zap_details *details)
{
	zap_page_range_single(vma, start_addr, end_addr - start_addr, details);
}

static inline void unmap_mapping_range_tree(struct rb_root *root,
					    struct zap_details *details)
{
	struct vm_area_struct *vma;
	pgoff_t vba, vea, zba, zea;

	vma_interval_tree_foreach(vma, root,
			details->first_index, details->last_index) {

		vba = vma->vm_pgoff;
		vea = vba + vma_pages(vma) - 1;
		/* Assume for now that PAGE_CACHE_SHIFT == PAGE_SHIFT */
		zba = details->first_index;
		if (zba < vba)
			zba = vba;
		zea = details->last_index;
		if (zea > vea)
			zea = vea;

		if (unmap_mapping_range_vma(vma,
			((zba - vba) << PAGE_SHIFT) + vma->vm_start,
			((zea - vba + 1) << PAGE_SHIFT) + vma->vm_start,
				details) < 0)
			goto restart;
	}
}

static inline void unmap_mapping_range_list(struct list_head *head,
					    struct zap_details *details)
{
	struct vm_area_struct *vma;

	/*
	 * In nonlinear VMAs there is no correspondence between virtual address
	 * offset and file offset.  So we must perform an exhaustive search
	 * across *all* the pages in each nonlinear VMA, not just the pages
	 * whose virtual address lies outside the file truncation point.
	 */
restart:
	list_for_each_entry(vma, head, shared.vm_set.list) {
		/* Skip quickly over those we have already dealt with */
		if (vma->vm_truncate_count == details->truncate_count)
			continue;
		details->nonlinear_vma = vma;
		if (unmap_mapping_range_vma(vma, vma->vm_start,
					vma->vm_end, details) < 0)
			goto restart;
		unmap_mapping_range_vma(vma,
			((zba - vba) << PAGE_SHIFT) + vma->vm_start,
			((zea - vba + 1) << PAGE_SHIFT) + vma->vm_start,
				details);
	}
}

/**
 * unmap_mapping_range - unmap the portion of all mmaps in the specified address_space corresponding to the specified page range in the underlying file.
 * @mapping: the address space containing mmaps to be unmapped.
 * @holebegin: byte in first page to unmap, relative to the start of
 * the underlying file.  This will be rounded down to a PAGE_SIZE
 * boundary.  Note that this is different from vmtruncate(), which
 * unmap_mapping_range - unmap the portion of all mmaps in the specified
 * address_space corresponding to the specified page range in the underlying
 * file.
 *
 * @mapping: the address space containing mmaps to be unmapped.
 * @holebegin: byte in first page to unmap, relative to the start of
 * the underlying file.  This will be rounded down to a PAGE_SIZE
 * boundary.  Note that this is different from truncate_pagecache(), which
 * must keep the partial page.  In contrast, we must get rid of
 * partial pages.
 * @holelen: size of prospective hole in bytes.  This will be rounded
 * up to a PAGE_SIZE boundary.  A holelen of zero truncates to the
 * end of the file.
 * @even_cows: 1 when truncating a file, unmap even private COWed pages;
 * but 0 when invalidating pagecache, don't throw away private data.
 */
void unmap_mapping_range(struct address_space *mapping,
		loff_t const holebegin, loff_t const holelen, int even_cows)
{
	struct zap_details details;
	pgoff_t hba = (pgoff_t)(holebegin) >> PAGE_SHIFT;
	pgoff_t hlen = ((pgoff_t)(holelen) + PAGE_SIZE - 1) >> PAGE_SHIFT;

	/* Check for overflow. */
	if (sizeof(holelen) > sizeof(hlen)) {
		long long holeend =
			(holebegin + holelen + PAGE_SIZE - 1) >> PAGE_SHIFT;
		if (holeend & ~(long long)ULONG_MAX)
			hlen = ULONG_MAX - hba + 1;
	}

	details.check_mapping = even_cows? NULL: mapping;
	details.nonlinear_vma = NULL;
	details.first_index = hba;
	details.last_index = hba + hlen - 1;
	if (details.last_index < details.first_index)
		details.last_index = ULONG_MAX;
	details.i_mmap_lock = &mapping->i_mmap_lock;

	spin_lock(&mapping->i_mmap_lock);

	/* Protect against endless unmapping loops */
	mapping->truncate_count++;
	if (unlikely(is_restart_addr(mapping->truncate_count))) {
		if (mapping->truncate_count == 0)
			reset_vma_truncate_counts(mapping);
		mapping->truncate_count++;
	}
	details.truncate_count = mapping->truncate_count;

	if (unlikely(!prio_tree_empty(&mapping->i_mmap)))
		unmap_mapping_range_tree(&mapping->i_mmap, &details);
	if (unlikely(!list_empty(&mapping->i_mmap_nonlinear)))
		unmap_mapping_range_list(&mapping->i_mmap_nonlinear, &details);
	spin_unlock(&mapping->i_mmap_lock);
}
EXPORT_SYMBOL(unmap_mapping_range);

/**
 * vmtruncate - unmap mappings "freed" by truncate() syscall
 * @inode: inode of the file used
 * @offset: file offset to start truncating
 *
 * NOTE! We have to be ready to update the memory sharing
 * between the file and the memory map for a potential last
 * incomplete page.  Ugly, but necessary.
 */
int vmtruncate(struct inode * inode, loff_t offset)
{
	if (inode->i_size < offset) {
		unsigned long limit;

		limit = current->signal->rlim[RLIMIT_FSIZE].rlim_cur;
		if (limit != RLIM_INFINITY && offset > limit)
			goto out_sig;
		if (offset > inode->i_sb->s_maxbytes)
			goto out_big;
		i_size_write(inode, offset);
	} else {
		struct address_space *mapping = inode->i_mapping;

		/*
		 * truncation of in-use swapfiles is disallowed - it would
		 * cause subsequent swapout to scribble on the now-freed
		 * blocks.
		 */
		if (IS_SWAPFILE(inode))
			return -ETXTBSY;
		i_size_write(inode, offset);

		/*
		 * unmap_mapping_range is called twice, first simply for
		 * efficiency so that truncate_inode_pages does fewer
		 * single-page unmaps.  However after this first call, and
		 * before truncate_inode_pages finishes, it is possible for
		 * private pages to be COWed, which remain after
		 * truncate_inode_pages finishes, hence the second
		 * unmap_mapping_range call must be made for correctness.
		 */
		unmap_mapping_range(mapping, offset + PAGE_SIZE - 1, 0, 1);
		truncate_inode_pages(mapping, offset);
		unmap_mapping_range(mapping, offset + PAGE_SIZE - 1, 0, 1);
	}

	if (inode->i_op && inode->i_op->truncate)
		inode->i_op->truncate(inode);
	return 0;

out_sig:
	send_sig(SIGXFSZ, current, 0);
out_big:
	return -EFBIG;
}
EXPORT_SYMBOL(vmtruncate);

int vmtruncate_range(struct inode *inode, loff_t offset, loff_t end)
{
	struct address_space *mapping = inode->i_mapping;

	/*
	 * If the underlying filesystem is not going to provide
	 * a way to truncate a range of blocks (punch a hole) -
	 * we should return failure right now.
	 */
	if (!inode->i_op || !inode->i_op->truncate_range)
		return -ENOSYS;

	mutex_lock(&inode->i_mutex);
	down_write(&inode->i_alloc_sem);
	unmap_mapping_range(mapping, offset, (end - offset), 1);
	truncate_inode_pages_range(mapping, offset, end);
	unmap_mapping_range(mapping, offset, (end - offset), 1);
	inode->i_op->truncate_range(inode, offset, end);
	up_write(&inode->i_alloc_sem);
	mutex_unlock(&inode->i_mutex);

	return 0;
}

/*
 * We enter with non-exclusive mmap_sem (to exclude vma changes,
 * but allow concurrent faults), and pte mapped but not yet locked.
 * We return with mmap_sem still held, but pte unmapped and unlocked.
 */
static int do_swap_page(struct mm_struct *mm, struct vm_area_struct *vma,
		unsigned long address, pte_t *page_table, pmd_t *pmd,
		int write_access, pte_t orig_pte)
{
	spinlock_t *ptl;
	struct page *page;
	swp_entry_t entry;
	pte_t pte;


	/* DAX uses i_mmap_lock to serialise file truncate vs page fault */
	i_mmap_lock_write(mapping);
	if (unlikely(!RB_EMPTY_ROOT(&mapping->i_mmap)))
		unmap_mapping_range_tree(&mapping->i_mmap, &details);
	i_mmap_unlock_write(mapping);
}
EXPORT_SYMBOL(unmap_mapping_range);

/*
 * We enter with non-exclusive mmap_sem (to exclude vma changes,
 * but allow concurrent faults), and pte mapped but not yet locked.
 * We return with pte unmapped and unlocked.
 *
 * We return with the mmap_sem locked or unlocked in the same cases
 * as does filemap_fault().
 */
static int do_swap_page(struct mm_struct *mm, struct vm_area_struct *vma,
		unsigned long address, pte_t *page_table, pmd_t *pmd,
		unsigned int flags, pte_t orig_pte)
{
	spinlock_t *ptl;
	struct page *page, *swapcache;
	struct mem_cgroup *memcg;
	swp_entry_t entry;
	pte_t pte;
	int locked;
	int exclusive = 0;
	int ret = 0;

	if (!pte_unmap_same(mm, pmd, page_table, orig_pte))
		goto out;

	entry = pte_to_swp_entry(orig_pte);
	if (is_migration_entry(entry)) {
		migration_entry_wait(mm, pmd, address);
	if (unlikely(non_swap_entry(entry))) {
		if (is_migration_entry(entry)) {
			migration_entry_wait(mm, pmd, address);
		} else if (is_hwpoison_entry(entry)) {
			ret = VM_FAULT_HWPOISON;
		} else {
			print_bad_pte(vma, address, orig_pte, NULL);
			ret = VM_FAULT_SIGBUS;
		}
		goto out;
	}
	delayacct_set_flag(DELAYACCT_PF_SWAPIN);
	page = lookup_swap_cache(entry);
	if (!page) {
		grab_swap_token(); /* Contend for token _before_ read-in */
		page = swapin_readahead(entry,
					GFP_HIGHUSER_MOVABLE, vma, address);
		if (!page) {
			/*
			 * Back out if somebody else faulted in this pte
			 * while we released the pte lock.
			 */
			page_table = pte_offset_map_lock(mm, pmd, address, &ptl);
			if (likely(pte_same(*page_table, orig_pte)))
				ret = VM_FAULT_OOM;
			delayacct_clear_flag(DELAYACCT_PF_SWAPIN);
			goto unlock;
		}

		/* Had to read the page from swap area: Major fault */
		ret = VM_FAULT_MAJOR;
		count_vm_event(PGMAJFAULT);
	}

	if (mem_cgroup_charge(page, mm, GFP_KERNEL)) {
		delayacct_clear_flag(DELAYACCT_PF_SWAPIN);
		ret = VM_FAULT_OOM;
		goto out;
	}

	mark_page_accessed(page);
	lock_page(page);
	delayacct_clear_flag(DELAYACCT_PF_SWAPIN);
		mem_cgroup_count_vm_event(mm, PGMAJFAULT);
	} else if (PageHWPoison(page)) {
		/*
		 * hwpoisoned dirty swapcache pages are kept for killing
		 * owner processes (which may be unknown at hwpoison time)
		 */
		ret = VM_FAULT_HWPOISON;
		delayacct_clear_flag(DELAYACCT_PF_SWAPIN);
		swapcache = page;
		goto out_release;
	}

	swapcache = page;
	locked = lock_page_or_retry(page, mm, flags);

	delayacct_clear_flag(DELAYACCT_PF_SWAPIN);
	if (!locked) {
		ret |= VM_FAULT_RETRY;
		goto out_release;
	}

	/*
	 * Make sure try_to_free_swap or reuse_swap_page or swapoff did not
	 * release the swapcache from under us.  The page pin, and pte_same
	 * test below, are not enough to exclude that.  Even if it is still
	 * swapcache, we need to check that the page's swap has not changed.
	 */
	if (unlikely(!PageSwapCache(page) || page_private(page) != entry.val))
		goto out_page;

	page = ksm_might_need_to_copy(page, vma, address);
	if (unlikely(!page)) {
		ret = VM_FAULT_OOM;
		page = swapcache;
		goto out_page;
	}

	if (mem_cgroup_try_charge(page, mm, GFP_KERNEL, &memcg)) {
		ret = VM_FAULT_OOM;
		goto out_page;
	}

	/*
	 * Back out if somebody else already faulted in this pte.
	 */
	page_table = pte_offset_map_lock(mm, pmd, address, &ptl);
	if (unlikely(!pte_same(*page_table, orig_pte)))
		goto out_nomap;

	if (unlikely(!PageUptodate(page))) {
		ret = VM_FAULT_SIGBUS;
		goto out_nomap;
	}

	/* The page isn't present yet, go ahead with the fault. */

	inc_mm_counter(mm, anon_rss);
	pte = mk_pte(page, vma->vm_page_prot);
	if (write_access && can_share_swap_page(page)) {
		pte = maybe_mkwrite(pte_mkdirty(pte), vma);
		write_access = 0;
	}

	flush_icache_page(vma, page);
	set_pte_at(mm, address, page_table, pte);
	page_add_anon_rmap(page, vma, address);

	swap_free(entry);
	if (vm_swap_full())
		remove_exclusive_swap_page(page);
	unlock_page(page);

	if (write_access) {
	/*
	 * The page isn't present yet, go ahead with the fault.
	 *
	 * Be careful about the sequence of operations here.
	 * To get its accounting right, reuse_swap_page() must be called
	 * while the page is counted on swap but not yet in mapcount i.e.
	 * before page_add_anon_rmap() and swap_free(); try_to_free_swap()
	 * must be called after the swap_free(), or it will never succeed.
	 */

	inc_mm_counter_fast(mm, MM_ANONPAGES);
	dec_mm_counter_fast(mm, MM_SWAPENTS);
	pte = mk_pte(page, vma->vm_page_prot);
	if ((flags & FAULT_FLAG_WRITE) && reuse_swap_page(page)) {
		pte = maybe_mkwrite(pte_mkdirty(pte), vma);
		flags &= ~FAULT_FLAG_WRITE;
		ret |= VM_FAULT_WRITE;
		exclusive = 1;
	}
	flush_icache_page(vma, page);
	if (pte_swp_soft_dirty(orig_pte))
		pte = pte_mksoft_dirty(pte);
	set_pte_at(mm, address, page_table, pte);
	if (page == swapcache) {
		do_page_add_anon_rmap(page, vma, address, exclusive);
		mem_cgroup_commit_charge(page, memcg, true);
	} else { /* ksm created a completely new copy */
		page_add_new_anon_rmap(page, vma, address);
		mem_cgroup_commit_charge(page, memcg, false);
		lru_cache_add_active_or_unevictable(page, vma);
	}

	swap_free(entry);
	if (vm_swap_full() || (vma->vm_flags & VM_LOCKED) || PageMlocked(page))
		try_to_free_swap(page);
	unlock_page(page);
	if (page != swapcache) {
		/*
		 * Hold the lock to avoid the swap entry to be reused
		 * until we take the PT lock for the pte_same() check
		 * (to avoid false positives from pte_same). For
		 * further safety release the lock after the swap_free
		 * so that the swap count won't change under a
		 * parallel locked swapcache.
		 */
		unlock_page(swapcache);
		page_cache_release(swapcache);
	}

	if (flags & FAULT_FLAG_WRITE) {
		ret |= do_wp_page(mm, vma, address, page_table, pmd, ptl, pte);
		if (ret & VM_FAULT_ERROR)
			ret &= VM_FAULT_ERROR;
		goto out;
	}

	/* No need to invalidate - it was non-present before */
	update_mmu_cache(vma, address, pte);
	update_mmu_cache(vma, address, page_table);
unlock:
	pte_unmap_unlock(page_table, ptl);
out:
	return ret;
out_nomap:
	mem_cgroup_uncharge_page(page);
	pte_unmap_unlock(page_table, ptl);
	unlock_page(page);
	page_cache_release(page);
	mem_cgroup_cancel_charge(page, memcg);
	pte_unmap_unlock(page_table, ptl);
out_page:
	unlock_page(page);
out_release:
	page_cache_release(page);
	if (page != swapcache) {
		unlock_page(swapcache);
		page_cache_release(swapcache);
	}
	return ret;
}

/*
 * We enter with non-exclusive mmap_sem (to exclude vma changes,
 * but allow concurrent faults), and pte mapped but not yet locked.
 * We return with mmap_sem still held, but pte unmapped and unlocked.
 */
static int do_anonymous_page(struct mm_struct *mm, struct vm_area_struct *vma,
		unsigned long address, pte_t *page_table, pmd_t *pmd,
		int write_access)
{
		unsigned int flags)
{
	struct mem_cgroup *memcg;
	struct page *page;
	spinlock_t *ptl;
	pte_t entry;

	/* Allocate our own private page. */
	pte_unmap(page_table);

	pte_unmap(page_table);

	/* File mapping without ->vm_ops ? */
	if (vma->vm_flags & VM_SHARED)
		return VM_FAULT_SIGBUS;

	/* Use the zero-page for reads */
	if (!(flags & FAULT_FLAG_WRITE) && !mm_forbids_zeropage(mm)) {
		entry = pte_mkspecial(pfn_pte(my_zero_pfn(address),
						vma->vm_page_prot));
		page_table = pte_offset_map_lock(mm, pmd, address, &ptl);
		if (!pte_none(*page_table))
			goto unlock;
		/* Deliver the page fault to userland, check inside PT lock */
		if (userfaultfd_missing(vma)) {
			pte_unmap_unlock(page_table, ptl);
			return handle_userfault(vma, address, flags,
						VM_UFFD_MISSING);
		}
		goto setpte;
	}

	/* Allocate our own private page. */
	if (unlikely(anon_vma_prepare(vma)))
		goto oom;
	page = alloc_zeroed_user_highpage_movable(vma, address);
	if (!page)
		goto oom;
	__SetPageUptodate(page);

	if (mem_cgroup_charge(page, mm, GFP_KERNEL))
		goto oom_free_page;

	entry = mk_pte(page, vma->vm_page_prot);
	entry = maybe_mkwrite(pte_mkdirty(entry), vma);

	if (mem_cgroup_try_charge(page, mm, GFP_KERNEL, &memcg))
		goto oom_free_page;

	/*
	 * The memory barrier inside __SetPageUptodate makes sure that
	 * preceeding stores to the page contents become visible before
	 * the set_pte_at() write.
	 */
	__SetPageUptodate(page);

	entry = mk_pte(page, vma->vm_page_prot);
	if (vma->vm_flags & VM_WRITE)
		entry = pte_mkwrite(pte_mkdirty(entry));

	page_table = pte_offset_map_lock(mm, pmd, address, &ptl);
	if (!pte_none(*page_table))
		goto release;
	inc_mm_counter(mm, anon_rss);
	lru_cache_add_active(page);
	page_add_new_anon_rmap(page, vma, address);
	set_pte_at(mm, address, page_table, entry);

	/* No need to invalidate - it was non-present before */
	update_mmu_cache(vma, address, entry);

	/* Deliver the page fault to userland, check inside PT lock */
	if (userfaultfd_missing(vma)) {
		pte_unmap_unlock(page_table, ptl);
		mem_cgroup_cancel_charge(page, memcg);
		page_cache_release(page);
		return handle_userfault(vma, address, flags,
					VM_UFFD_MISSING);
	}

	inc_mm_counter_fast(mm, MM_ANONPAGES);
	page_add_new_anon_rmap(page, vma, address);
	mem_cgroup_commit_charge(page, memcg, false);
	lru_cache_add_active_or_unevictable(page, vma);
setpte:
	set_pte_at(mm, address, page_table, entry);

	/* No need to invalidate - it was non-present before */
	update_mmu_cache(vma, address, page_table);
unlock:
	pte_unmap_unlock(page_table, ptl);
	return 0;
release:
	mem_cgroup_uncharge_page(page);
	mem_cgroup_cancel_charge(page, memcg);
	page_cache_release(page);
	goto unlock;
oom_free_page:
	page_cache_release(page);
oom:
	return VM_FAULT_OOM;
}

/*
 * __do_fault() tries to create a new page mapping. It aggressively
 * tries to share with existing pages, but makes a separate copy if
 * the FAULT_FLAG_WRITE is set in the flags parameter in order to avoid
 * the next page fault.
 *
 * As this is called only for pages that do not currently exist, we
 * do not need to flush old virtual caches or the TLB.
 *
 * We enter with non-exclusive mmap_sem (to exclude vma changes,
 * but allow concurrent faults), and pte neither mapped nor locked.
 * We return with mmap_sem still held, but pte unmapped and unlocked.
 */
static int __do_fault(struct mm_struct *mm, struct vm_area_struct *vma,
		unsigned long address, pmd_t *pmd,
		pgoff_t pgoff, unsigned int flags, pte_t orig_pte)
{
	pte_t *page_table;
	spinlock_t *ptl;
	struct page *page;
	pte_t entry;
	int anon = 0;
	struct page *dirty_page = NULL;
	struct vm_fault vmf;
	int ret;
	int page_mkwrite = 0;
 * The mmap_sem must have been held on entry, and may have been
 * released depending on flags and vma->vm_ops->fault() return value.
 * See filemap_fault() and __lock_page_retry().
 */
static int __do_fault(struct vm_area_struct *vma, unsigned long address,
			pgoff_t pgoff, unsigned int flags,
			struct page *cow_page, struct page **page)
{
	struct vm_fault vmf;
	int ret;

	vmf.virtual_address = (void __user *)(address & PAGE_MASK);
	vmf.pgoff = pgoff;
	vmf.flags = flags;
	vmf.page = NULL;

	ret = vma->vm_ops->fault(vma, &vmf);
	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE)))
		return ret;

	/*
	 * For consistency in subsequent calls, make the faulted page always
	 * locked.
	 */
	if (unlikely(!(ret & VM_FAULT_LOCKED)))
		lock_page(vmf.page);
	else
		VM_BUG_ON(!PageLocked(vmf.page));

	/*
	 * Should we do an early C-O-W break?
	 */
	page = vmf.page;
	if (flags & FAULT_FLAG_WRITE) {
		if (!(vma->vm_flags & VM_SHARED)) {
			anon = 1;
			if (unlikely(anon_vma_prepare(vma))) {
				ret = VM_FAULT_OOM;
				goto out;
			}
			page = alloc_page_vma(GFP_HIGHUSER_MOVABLE,
						vma, address);
			if (!page) {
				ret = VM_FAULT_OOM;
				goto out;
			}
			copy_user_highpage(page, vmf.page, address, vma);
			__SetPageUptodate(page);
		} else {
			/*
			 * If the page will be shareable, see if the backing
			 * address space wants to know that the page is about
			 * to become writable
			 */
			if (vma->vm_ops->page_mkwrite) {
				unlock_page(page);
				if (vma->vm_ops->page_mkwrite(vma, page) < 0) {
					ret = VM_FAULT_SIGBUS;
					anon = 1; /* no anon but release vmf.page */
					goto out_unlocked;
				}
				lock_page(page);
				/*
				 * XXX: this is not quite right (racy vs
				 * invalidate) to unlock and relock the page
				 * like this, however a better fix requires
				 * reworking page_mkwrite locking API, which
				 * is better done later.
				 */
				if (!page->mapping) {
					ret = 0;
					anon = 1; /* no anon but release vmf.page */
					goto out;
				}
				page_mkwrite = 1;
			}
		}

	}

	if (mem_cgroup_charge(page, mm, GFP_KERNEL)) {
		ret = VM_FAULT_OOM;
		goto out;
	}

	page_table = pte_offset_map_lock(mm, pmd, address, &ptl);

	/*
	 * This silly early PAGE_DIRTY setting removes a race
	 * due to the bad i386 page protection. But it's valid
	 * for other architectures too.
	 *
	 * Note that if write_access is true, we either now have
	 * an exclusive copy of the page, or this is a shared mapping,
	 * so we can make it writable and dirty to avoid having to
	 * handle that later.
	 */
	/* Only go through if we didn't race with anybody else... */
	if (likely(pte_same(*page_table, orig_pte))) {
		flush_icache_page(vma, page);
		entry = mk_pte(page, vma->vm_page_prot);
		if (flags & FAULT_FLAG_WRITE)
			entry = maybe_mkwrite(pte_mkdirty(entry), vma);
		set_pte_at(mm, address, page_table, entry);
		if (anon) {
                        inc_mm_counter(mm, anon_rss);
                        lru_cache_add_active(page);
                        page_add_new_anon_rmap(page, vma, address);
		} else {
			inc_mm_counter(mm, file_rss);
			page_add_file_rmap(page);
			if (flags & FAULT_FLAG_WRITE) {
				dirty_page = page;
				get_page(dirty_page);
			}
		}

		/* no need to invalidate: a not-present page won't be cached */
		update_mmu_cache(vma, address, entry);
	} else {
		mem_cgroup_uncharge_page(page);
		if (anon)
			page_cache_release(page);
		else
			anon = 1; /* no anon but release faulted_page */
	}

	pte_unmap_unlock(page_table, ptl);

out:
	unlock_page(vmf.page);
out_unlocked:
	if (anon)
		page_cache_release(vmf.page);
	else if (dirty_page) {
		if (vma->vm_file)
			file_update_time(vma->vm_file);

		set_page_dirty_balance(dirty_page, page_mkwrite);
		put_page(dirty_page);
	}
	vmf.gfp_mask = __get_fault_gfp_mask(vma);
	vmf.cow_page = cow_page;

	ret = vma->vm_ops->fault(vma, &vmf);
	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE | VM_FAULT_RETRY)))
		return ret;
	if (!vmf.page)
		goto out;

	if (unlikely(PageHWPoison(vmf.page))) {
		if (ret & VM_FAULT_LOCKED)
			unlock_page(vmf.page);
		page_cache_release(vmf.page);
		return VM_FAULT_HWPOISON;
	}

	if (unlikely(!(ret & VM_FAULT_LOCKED)))
		lock_page(vmf.page);
	else
		VM_BUG_ON_PAGE(!PageLocked(vmf.page), vmf.page);

 out:
	*page = vmf.page;
	return ret;
}

/**
 * do_set_pte - setup new PTE entry for given page and add reverse page mapping.
 *
 * @vma: virtual memory area
 * @address: user virtual address
 * @page: page to map
 * @pte: pointer to target page table entry
 * @write: true, if new entry is writable
 * @anon: true, if it's anonymous page
 *
 * Caller must hold page table lock relevant for @pte.
 *
 * Target users are page handler itself and implementations of
 * vm_ops->map_pages.
 */
void do_set_pte(struct vm_area_struct *vma, unsigned long address,
		struct page *page, pte_t *pte, bool write, bool anon)
{
	pte_t entry;

	flush_icache_page(vma, page);
	entry = mk_pte(page, vma->vm_page_prot);
	if (write)
		entry = maybe_mkwrite(pte_mkdirty(entry), vma);
	if (anon) {
		inc_mm_counter_fast(vma->vm_mm, MM_ANONPAGES);
		page_add_new_anon_rmap(page, vma, address);
	} else {
		inc_mm_counter_fast(vma->vm_mm, MM_FILEPAGES);
		page_add_file_rmap(page);
	}
	set_pte_at(vma->vm_mm, address, pte, entry);

	/* no need to invalidate: a not-present page won't be cached */
	update_mmu_cache(vma, address, pte);
}

static unsigned long fault_around_bytes __read_mostly =
	rounddown_pow_of_two(65536);

#ifdef CONFIG_DEBUG_FS
static int fault_around_bytes_get(void *data, u64 *val)
{
	*val = fault_around_bytes;
	return 0;
}

/*
 * fault_around_pages() and fault_around_mask() expects fault_around_bytes
 * rounded down to nearest page order. It's what do_fault_around() expects to
 * see.
 */
static int fault_around_bytes_set(void *data, u64 val)
{
	if (val / PAGE_SIZE > PTRS_PER_PTE)
		return -EINVAL;
	if (val > PAGE_SIZE)
		fault_around_bytes = rounddown_pow_of_two(val);
	else
		fault_around_bytes = PAGE_SIZE; /* rounddown_pow_of_two(0) is undefined */
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(fault_around_bytes_fops,
		fault_around_bytes_get, fault_around_bytes_set, "%llu\n");

static int __init fault_around_debugfs(void)
{
	void *ret;

	ret = debugfs_create_file("fault_around_bytes", 0644, NULL, NULL,
			&fault_around_bytes_fops);
	if (!ret)
		pr_warn("Failed to create fault_around_bytes in debugfs");
	return 0;
}
late_initcall(fault_around_debugfs);
#endif

/*
 * do_fault_around() tries to map few pages around the fault address. The hope
 * is that the pages will be needed soon and this will lower the number of
 * faults to handle.
 *
 * It uses vm_ops->map_pages() to map the pages, which skips the page if it's
 * not ready to be mapped: not up-to-date, locked, etc.
 *
 * This function is called with the page table lock taken. In the split ptlock
 * case the page table lock only protects only those entries which belong to
 * the page table corresponding to the fault address.
 *
 * This function doesn't cross the VMA boundaries, in order to call map_pages()
 * only once.
 *
 * fault_around_pages() defines how many pages we'll try to map.
 * do_fault_around() expects it to return a power of two less than or equal to
 * PTRS_PER_PTE.
 *
 * The virtual address of the area that we map is naturally aligned to the
 * fault_around_pages() value (and therefore to page order).  This way it's
 * easier to guarantee that we don't cross page table boundaries.
 */
static void do_fault_around(struct vm_area_struct *vma, unsigned long address,
		pte_t *pte, pgoff_t pgoff, unsigned int flags)
{
	unsigned long start_addr, nr_pages, mask;
	pgoff_t max_pgoff;
	struct vm_fault vmf;
	int off;

	nr_pages = READ_ONCE(fault_around_bytes) >> PAGE_SHIFT;
	mask = ~(nr_pages * PAGE_SIZE - 1) & PAGE_MASK;

	start_addr = max(address & mask, vma->vm_start);
	off = ((address - start_addr) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
	pte -= off;
	pgoff -= off;

	/*
	 *  max_pgoff is either end of page table or end of vma
	 *  or fault_around_pages() from pgoff, depending what is nearest.
	 */
	max_pgoff = pgoff - ((start_addr >> PAGE_SHIFT) & (PTRS_PER_PTE - 1)) +
		PTRS_PER_PTE - 1;
	max_pgoff = min3(max_pgoff, vma_pages(vma) + vma->vm_pgoff - 1,
			pgoff + nr_pages - 1);

	/* Check if it makes any sense to call ->map_pages */
	while (!pte_none(*pte)) {
		if (++pgoff > max_pgoff)
			return;
		start_addr += PAGE_SIZE;
		if (start_addr >= vma->vm_end)
			return;
		pte++;
	}

	vmf.virtual_address = (void __user *) start_addr;
	vmf.pte = pte;
	vmf.pgoff = pgoff;
	vmf.max_pgoff = max_pgoff;
	vmf.flags = flags;
	vmf.gfp_mask = __get_fault_gfp_mask(vma);
	vma->vm_ops->map_pages(vma, &vmf);
}

static int do_read_fault(struct mm_struct *mm, struct vm_area_struct *vma,
		unsigned long address, pmd_t *pmd,
		pgoff_t pgoff, unsigned int flags, pte_t orig_pte)
{
	struct page *fault_page;
	spinlock_t *ptl;
	pte_t *pte;
	int ret = 0;

	/*
	 * Let's call ->map_pages() first and use ->fault() as fallback
	 * if page by the offset is not ready to be mapped (cold cache or
	 * something).
	 */
	if (vma->vm_ops->map_pages && fault_around_bytes >> PAGE_SHIFT > 1) {
		pte = pte_offset_map_lock(mm, pmd, address, &ptl);
		do_fault_around(vma, address, pte, pgoff, flags);
		if (!pte_same(*pte, orig_pte))
			goto unlock_out;
		pte_unmap_unlock(pte, ptl);
	}

	ret = __do_fault(vma, address, pgoff, flags, NULL, &fault_page);
	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE | VM_FAULT_RETRY)))
		return ret;

	pte = pte_offset_map_lock(mm, pmd, address, &ptl);
	if (unlikely(!pte_same(*pte, orig_pte))) {
		pte_unmap_unlock(pte, ptl);
		unlock_page(fault_page);
		page_cache_release(fault_page);
		return ret;
	}
	do_set_pte(vma, address, fault_page, pte, false, false);
	unlock_page(fault_page);
unlock_out:
	pte_unmap_unlock(pte, ptl);
	return ret;
}

static int do_cow_fault(struct mm_struct *mm, struct vm_area_struct *vma,
		unsigned long address, pmd_t *pmd,
		pgoff_t pgoff, unsigned int flags, pte_t orig_pte)
{
	struct page *fault_page, *new_page;
	struct mem_cgroup *memcg;
	spinlock_t *ptl;
	pte_t *pte;
	int ret;

	if (unlikely(anon_vma_prepare(vma)))
		return VM_FAULT_OOM;

	new_page = alloc_page_vma(GFP_HIGHUSER_MOVABLE, vma, address);
	if (!new_page)
		return VM_FAULT_OOM;

	if (mem_cgroup_try_charge(new_page, mm, GFP_KERNEL, &memcg)) {
		page_cache_release(new_page);
		return VM_FAULT_OOM;
	}

	ret = __do_fault(vma, address, pgoff, flags, new_page, &fault_page);
	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE | VM_FAULT_RETRY)))
		goto uncharge_out;

	if (fault_page)
		copy_user_highpage(new_page, fault_page, address, vma);
	__SetPageUptodate(new_page);

	pte = pte_offset_map_lock(mm, pmd, address, &ptl);
	if (unlikely(!pte_same(*pte, orig_pte))) {
		pte_unmap_unlock(pte, ptl);
		if (fault_page) {
			unlock_page(fault_page);
			page_cache_release(fault_page);
		} else {
			/*
			 * The fault handler has no page to lock, so it holds
			 * i_mmap_lock for read to protect against truncate.
			 */
			i_mmap_unlock_read(vma->vm_file->f_mapping);
		}
		goto uncharge_out;
	}
	do_set_pte(vma, address, new_page, pte, true, true);
	mem_cgroup_commit_charge(new_page, memcg, false);
	lru_cache_add_active_or_unevictable(new_page, vma);
	pte_unmap_unlock(pte, ptl);
	if (fault_page) {
		unlock_page(fault_page);
		page_cache_release(fault_page);
	} else {
		/*
		 * The fault handler has no page to lock, so it holds
		 * i_mmap_lock for read to protect against truncate.
		 */
		i_mmap_unlock_read(vma->vm_file->f_mapping);
	}
	return ret;
uncharge_out:
	mem_cgroup_cancel_charge(new_page, memcg);
	page_cache_release(new_page);
	return ret;
}

static int do_shared_fault(struct mm_struct *mm, struct vm_area_struct *vma,
		unsigned long address, pmd_t *pmd,
		pgoff_t pgoff, unsigned int flags, pte_t orig_pte)
{
	struct page *fault_page;
	struct address_space *mapping;
	spinlock_t *ptl;
	pte_t *pte;
	int dirtied = 0;
	int ret, tmp;

	ret = __do_fault(vma, address, pgoff, flags, NULL, &fault_page);
	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE | VM_FAULT_RETRY)))
		return ret;

	/*
	 * Check if the backing address space wants to know that the page is
	 * about to become writable
	 */
	if (vma->vm_ops->page_mkwrite) {
		unlock_page(fault_page);
		tmp = do_page_mkwrite(vma, fault_page, address);
		if (unlikely(!tmp ||
				(tmp & (VM_FAULT_ERROR | VM_FAULT_NOPAGE)))) {
			page_cache_release(fault_page);
			return tmp;
		}
	}

	pte = pte_offset_map_lock(mm, pmd, address, &ptl);
	if (unlikely(!pte_same(*pte, orig_pte))) {
		pte_unmap_unlock(pte, ptl);
		unlock_page(fault_page);
		page_cache_release(fault_page);
		return ret;
	}
	do_set_pte(vma, address, fault_page, pte, true, false);
	pte_unmap_unlock(pte, ptl);

	if (set_page_dirty(fault_page))
		dirtied = 1;
	/*
	 * Take a local copy of the address_space - page.mapping may be zeroed
	 * by truncate after unlock_page().   The address_space itself remains
	 * pinned by vma->vm_file's reference.  We rely on unlock_page()'s
	 * release semantics to prevent the compiler from undoing this copying.
	 */
	mapping = fault_page->mapping;
	unlock_page(fault_page);
	if ((dirtied || vma->vm_ops->page_mkwrite) && mapping) {
		/*
		 * Some device drivers do not set page.mapping but still
		 * dirty their pages
		 */
		balance_dirty_pages_ratelimited(mapping);
	}

	if (!vma->vm_ops->page_mkwrite)
		file_update_time(vma->vm_file);

	return ret;
}

static int do_linear_fault(struct mm_struct *mm, struct vm_area_struct *vma,
		unsigned long address, pte_t *page_table, pmd_t *pmd,
		int write_access, pte_t orig_pte)
{
	pgoff_t pgoff = (((address & PAGE_MASK)
			- vma->vm_start) >> PAGE_SHIFT) + vma->vm_pgoff;
	unsigned int flags = (write_access ? FAULT_FLAG_WRITE : 0);

	pte_unmap(page_table);
	return __do_fault(mm, vma, address, pmd, pgoff, flags, orig_pte);
}

/*
 * Fault of a previously existing named mapping. Repopulate the pte
 * from the encoded file_pte if possible. This enables swappable
 * nonlinear vmas.
 *
 * We enter with non-exclusive mmap_sem (to exclude vma changes,
 * but allow concurrent faults), and pte mapped but not yet locked.
 * We return with mmap_sem still held, but pte unmapped and unlocked.
 */
static int do_nonlinear_fault(struct mm_struct *mm, struct vm_area_struct *vma,
		unsigned long address, pte_t *page_table, pmd_t *pmd,
		int write_access, pte_t orig_pte)
{
	unsigned int flags = FAULT_FLAG_NONLINEAR |
				(write_access ? FAULT_FLAG_WRITE : 0);
	pgoff_t pgoff;

	if (!pte_unmap_same(mm, pmd, page_table, orig_pte))
		return 0;

	if (unlikely(!(vma->vm_flags & VM_NONLINEAR) ||
			!(vma->vm_flags & VM_CAN_NONLINEAR))) {
		/*
		 * Page table corrupted: show pte and kill process.
		 */
		print_bad_pte(vma, orig_pte, address);
		return VM_FAULT_OOM;
	}

	pgoff = pte_to_pgoff(orig_pte);
	return __do_fault(mm, vma, address, pmd, pgoff, flags, orig_pte);
/*
 * We enter with non-exclusive mmap_sem (to exclude vma changes,
 * but allow concurrent faults).
 * The mmap_sem may have been released depending on flags and our
 * return value.  See filemap_fault() and __lock_page_or_retry().
 */
static int do_fault(struct mm_struct *mm, struct vm_area_struct *vma,
		unsigned long address, pte_t *page_table, pmd_t *pmd,
		unsigned int flags, pte_t orig_pte)
{
	pgoff_t pgoff = (((address & PAGE_MASK)
			- vma->vm_start) >> PAGE_SHIFT) + vma->vm_pgoff;

	pte_unmap(page_table);
	/* The VMA was not fully populated on mmap() or missing VM_DONTEXPAND */
	if (!vma->vm_ops->fault)
		return VM_FAULT_SIGBUS;
	if (!(flags & FAULT_FLAG_WRITE))
		return do_read_fault(mm, vma, address, pmd, pgoff, flags,
				orig_pte);
	if (!(vma->vm_flags & VM_SHARED))
		return do_cow_fault(mm, vma, address, pmd, pgoff, flags,
				orig_pte);
	return do_shared_fault(mm, vma, address, pmd, pgoff, flags, orig_pte);
}

static int numa_migrate_prep(struct page *page, struct vm_area_struct *vma,
				unsigned long addr, int page_nid,
				int *flags)
{
	get_page(page);

	count_vm_numa_event(NUMA_HINT_FAULTS);
	if (page_nid == numa_node_id()) {
		count_vm_numa_event(NUMA_HINT_FAULTS_LOCAL);
		*flags |= TNF_FAULT_LOCAL;
	}

	return mpol_misplaced(page, vma, addr);
}

static int do_numa_page(struct mm_struct *mm, struct vm_area_struct *vma,
		   unsigned long addr, pte_t pte, pte_t *ptep, pmd_t *pmd)
{
	struct page *page = NULL;
	spinlock_t *ptl;
	int page_nid = -1;
	int last_cpupid;
	int target_nid;
	bool migrated = false;
	bool was_writable = pte_write(pte);
	int flags = 0;

	/*
	* The "pte" at this point cannot be used safely without
	* validation through pte_unmap_same(). It's of NUMA type but
	* the pfn may be screwed if the read is non atomic.
	*
	* We can safely just do a "set_pte_at()", because the old
	* page table entry is not accessible, so there would be no
	* concurrent hardware modifications to the PTE.
	*/
	ptl = pte_lockptr(mm, pmd);
	spin_lock(ptl);
	if (unlikely(!pte_same(*ptep, pte))) {
		pte_unmap_unlock(ptep, ptl);
		goto out;
	}

	/* Make it present again */
	pte = pte_modify(pte, vma->vm_page_prot);
	pte = pte_mkyoung(pte);
	if (was_writable)
		pte = pte_mkwrite(pte);
	set_pte_at(mm, addr, ptep, pte);
	update_mmu_cache(vma, addr, ptep);

	page = vm_normal_page(vma, addr, pte);
	if (!page) {
		pte_unmap_unlock(ptep, ptl);
		return 0;
	}

	/*
	 * Avoid grouping on RO pages in general. RO pages shouldn't hurt as
	 * much anyway since they can be in shared cache state. This misses
	 * the case where a mapping is writable but the process never writes
	 * to it but pte_write gets cleared during protection updates and
	 * pte_dirty has unpredictable behaviour between PTE scan updates,
	 * background writeback, dirty balancing and application behaviour.
	 */
	if (!(vma->vm_flags & VM_WRITE))
		flags |= TNF_NO_GROUP;

	/*
	 * Flag if the page is shared between multiple address spaces. This
	 * is later used when determining whether to group tasks together
	 */
	if (page_mapcount(page) > 1 && (vma->vm_flags & VM_SHARED))
		flags |= TNF_SHARED;

	last_cpupid = page_cpupid_last(page);
	page_nid = page_to_nid(page);
	target_nid = numa_migrate_prep(page, vma, addr, page_nid, &flags);
	pte_unmap_unlock(ptep, ptl);
	if (target_nid == -1) {
		put_page(page);
		goto out;
	}

	/* Migrate to the requested node */
	migrated = migrate_misplaced_page(page, vma, target_nid);
	if (migrated) {
		page_nid = target_nid;
		flags |= TNF_MIGRATED;
	} else
		flags |= TNF_MIGRATE_FAIL;

out:
	if (page_nid != -1)
		task_numa_fault(last_cpupid, page_nid, 1, flags);
	return 0;
}

static int create_huge_pmd(struct mm_struct *mm, struct vm_area_struct *vma,
			unsigned long address, pmd_t *pmd, unsigned int flags)
{
	if (vma_is_anonymous(vma))
		return do_huge_pmd_anonymous_page(mm, vma, address, pmd, flags);
	if (vma->vm_ops->pmd_fault)
		return vma->vm_ops->pmd_fault(vma, address, pmd, flags);
	return VM_FAULT_FALLBACK;
}

static int wp_huge_pmd(struct mm_struct *mm, struct vm_area_struct *vma,
			unsigned long address, pmd_t *pmd, pmd_t orig_pmd,
			unsigned int flags)
{
	if (vma_is_anonymous(vma))
		return do_huge_pmd_wp_page(mm, vma, address, pmd, orig_pmd);
	if (vma->vm_ops->pmd_fault)
		return vma->vm_ops->pmd_fault(vma, address, pmd, flags);
	return VM_FAULT_FALLBACK;
}

static inline bool vma_is_accessible(struct vm_area_struct *vma)
{
	return vma->vm_flags & (VM_READ | VM_EXEC | VM_WRITE);
}

/*
 * These routines also need to handle stuff like marking pages dirty
 * and/or accessed for architectures that don't do it in hardware (most
 * RISC architectures).  The early dirtying is also good on the i386.
 *
 * There is also a hook called "update_mmu_cache()" that architectures
 * with external mmu caches can use to update those (ie the Sparc or
 * PowerPC hashed page tables that act as extended TLBs).
 *
 * We enter with non-exclusive mmap_sem (to exclude vma changes,
 * but allow concurrent faults), and pte mapped but not yet locked.
 * We return with mmap_sem still held, but pte unmapped and unlocked.
 */
static inline int handle_pte_fault(struct mm_struct *mm,
		struct vm_area_struct *vma, unsigned long address,
		pte_t *pte, pmd_t *pmd, int write_access)
 * We return with pte unmapped and unlocked.
 *
 * The mmap_sem may have been released depending on flags and our
 * return value.  See filemap_fault() and __lock_page_or_retry().
 */
static int handle_pte_fault(struct mm_struct *mm,
		     struct vm_area_struct *vma, unsigned long address,
		     pte_t *pte, pmd_t *pmd, unsigned int flags)
{
	pte_t entry;
	spinlock_t *ptl;

	entry = *pte;
	if (!pte_present(entry)) {
		if (pte_none(entry)) {
			if (vma->vm_ops) {
				if (likely(vma->vm_ops->fault))
					return do_linear_fault(mm, vma, address,
						pte, pmd, write_access, entry);
			}
			return do_anonymous_page(mm, vma, address,
						 pte, pmd, write_access);
		}
		if (pte_file(entry))
			return do_nonlinear_fault(mm, vma, address,
					pte, pmd, write_access, entry);
		return do_swap_page(mm, vma, address,
					pte, pmd, write_access, entry);
	}

	/*
	 * some architectures can have larger ptes than wordsize,
	 * e.g.ppc44x-defconfig has CONFIG_PTE_64BIT=y and CONFIG_32BIT=y,
	 * so READ_ONCE or ACCESS_ONCE cannot guarantee atomic accesses.
	 * The code below just needs a consistent view for the ifs and
	 * we later double check anyway with the ptl lock held. So here
	 * a barrier will do.
	 */
	entry = *pte;
	barrier();
	if (!pte_present(entry)) {
		if (pte_none(entry)) {
			if (vma_is_anonymous(vma))
				return do_anonymous_page(mm, vma, address,
							 pte, pmd, flags);
			else
				return do_fault(mm, vma, address, pte, pmd,
						flags, entry);
		}
		return do_swap_page(mm, vma, address,
					pte, pmd, flags, entry);
	}

	if (pte_protnone(entry) && vma_is_accessible(vma))
		return do_numa_page(mm, vma, address, entry, pte, pmd);

	ptl = pte_lockptr(mm, pmd);
	spin_lock(ptl);
	if (unlikely(!pte_same(*pte, entry)))
		goto unlock;
	if (write_access) {
	if (flags & FAULT_FLAG_WRITE) {
		if (!pte_write(entry))
			return do_wp_page(mm, vma, address,
					pte, pmd, ptl, entry);
		entry = pte_mkdirty(entry);
	}
	entry = pte_mkyoung(entry);
	if (ptep_set_access_flags(vma, address, pte, entry, write_access)) {
		update_mmu_cache(vma, address, entry);
	if (ptep_set_access_flags(vma, address, pte, entry, flags & FAULT_FLAG_WRITE)) {
		update_mmu_cache(vma, address, pte);
	} else {
		/*
		 * This is needed only for protection faults but the arch code
		 * is not yet telling us if this is a protection fault or not.
		 * This still avoids useless tlb flushes for .text page faults
		 * with threads.
		 */
		if (write_access)
			flush_tlb_page(vma, address);
		if (flags & FAULT_FLAG_WRITE)
			flush_tlb_fix_spurious_fault(vma, address);
	}
unlock:
	pte_unmap_unlock(pte, ptl);
	return 0;
}

/*
 * By the time we get here, we already hold the mm semaphore
 */
int handle_mm_fault(struct mm_struct *mm, struct vm_area_struct *vma,
		unsigned long address, int write_access)
 *
 * The mmap_sem may have been released depending on flags and our
 * return value.  See filemap_fault() and __lock_page_or_retry().
 */
static int __handle_mm_fault(struct mm_struct *mm, struct vm_area_struct *vma,
			     unsigned long address, unsigned int flags)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	__set_current_state(TASK_RUNNING);

	count_vm_event(PGFAULT);

	if (unlikely(is_vm_hugetlb_page(vma)))
		return hugetlb_fault(mm, vma, address, write_access);
	if (unlikely(is_vm_hugetlb_page(vma)))
		return hugetlb_fault(mm, vma, address, flags);

	pgd = pgd_offset(mm, address);
	pud = pud_alloc(mm, pgd, address);
	if (!pud)
		return VM_FAULT_OOM;
	pmd = pmd_alloc(mm, pud, address);
	if (!pmd)
		return VM_FAULT_OOM;
	pte = pte_alloc_map(mm, pmd, address);
	if (!pte)
		return VM_FAULT_OOM;

	return handle_pte_fault(mm, vma, address, pte, pmd, write_access);
}

	if (pmd_none(*pmd) && transparent_hugepage_enabled(vma)) {
		int ret = create_huge_pmd(mm, vma, address, pmd, flags);
		if (!(ret & VM_FAULT_FALLBACK))
			return ret;
	} else {
		pmd_t orig_pmd = *pmd;
		int ret;

		barrier();
		if (pmd_trans_huge(orig_pmd)) {
			unsigned int dirty = flags & FAULT_FLAG_WRITE;

			/*
			 * If the pmd is splitting, return and retry the
			 * the fault.  Alternative: wait until the split
			 * is done, and goto retry.
			 */
			if (pmd_trans_splitting(orig_pmd))
				return 0;

			if (pmd_protnone(orig_pmd) && vma_is_accessible(vma))
				return do_huge_pmd_numa_page(mm, vma, address,
							     orig_pmd, pmd);

			if (dirty && !pmd_write(orig_pmd)) {
				ret = wp_huge_pmd(mm, vma, address, pmd,
							orig_pmd, flags);
				if (!(ret & VM_FAULT_FALLBACK))
					return ret;
			} else {
				huge_pmd_set_accessed(mm, vma, address, pmd,
						      orig_pmd, dirty);
				return 0;
			}
		}
	}

	/*
	 * Use __pte_alloc instead of pte_alloc_map, because we can't
	 * run pte_offset_map on the pmd, if an huge pmd could
	 * materialize from under us from a different thread.
	 */
	if (unlikely(pmd_none(*pmd)) &&
	    unlikely(__pte_alloc(mm, vma, pmd, address)))
		return VM_FAULT_OOM;
	/*
	 * If a huge pmd materialized under us just retry later.  Use
	 * pmd_trans_unstable() instead of pmd_trans_huge() to ensure the pmd
	 * didn't become pmd_trans_huge under us and then back to pmd_none, as
	 * a result of MADV_DONTNEED running immediately after a huge pmd fault
	 * in a different thread of this mm, in turn leading to a misleading
	 * pmd_trans_huge() retval.  All we have to ensure is that it is a
	 * regular pmd that we can walk with pte_offset_map() and we can do that
	 * through an atomic read in C, which is what pmd_trans_unstable()
	 * provides.
	 */
	if (unlikely(pmd_trans_unstable(pmd)))
		return 0;
	/*
	 * A regular pmd is established and it can't morph into a huge pmd
	 * from under us anymore at this point because we hold the mmap_sem
	 * read mode and khugepaged takes it in write mode. So now it's
	 * safe to run pte_offset_map().
	 */
	pte = pte_offset_map(pmd, address);

	return handle_pte_fault(mm, vma, address, pte, pmd, flags);
}

/*
 * By the time we get here, we already hold the mm semaphore
 *
 * The mmap_sem may have been released depending on flags and our
 * return value.  See filemap_fault() and __lock_page_or_retry().
 */
int handle_mm_fault(struct mm_struct *mm, struct vm_area_struct *vma,
		    unsigned long address, unsigned int flags)
{
	int ret;

	__set_current_state(TASK_RUNNING);

	count_vm_event(PGFAULT);
	mem_cgroup_count_vm_event(mm, PGFAULT);

	/* do counter updates before entering really critical section. */
	check_sync_rss_stat(current);

	/*
	 * Enable the memcg OOM handling for faults triggered in user
	 * space.  Kernel faults are handled more gracefully.
	 */
	if (flags & FAULT_FLAG_USER)
		mem_cgroup_oom_enable();

	ret = __handle_mm_fault(mm, vma, address, flags);

	if (flags & FAULT_FLAG_USER) {
		mem_cgroup_oom_disable();
                /*
                 * The task may have entered a memcg OOM situation but
                 * if the allocation error was handled gracefully (no
                 * VM_FAULT_OOM), there is no need to kill anything.
                 * Just clean up the OOM state peacefully.
                 */
                if (task_in_memcg_oom(current) && !(ret & VM_FAULT_OOM))
                        mem_cgroup_oom_synchronize(false);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(handle_mm_fault);

#ifndef __PAGETABLE_PUD_FOLDED
/*
 * Allocate page upper directory.
 * We've already handled the fast-path in-line.
 */
int __pud_alloc(struct mm_struct *mm, pgd_t *pgd, unsigned long address)
{
	pud_t *new = pud_alloc_one(mm, address);
	if (!new)
		return -ENOMEM;

	smp_wmb(); /* See comment in __pte_alloc */

	spin_lock(&mm->page_table_lock);
	if (pgd_present(*pgd))		/* Another has populated it */
		pud_free(mm, new);
	else
		pgd_populate(mm, pgd, new);
	spin_unlock(&mm->page_table_lock);
	return 0;
}
#endif /* __PAGETABLE_PUD_FOLDED */

#ifndef __PAGETABLE_PMD_FOLDED
/*
 * Allocate page middle directory.
 * We've already handled the fast-path in-line.
 */
int __pmd_alloc(struct mm_struct *mm, pud_t *pud, unsigned long address)
{
	pmd_t *new = pmd_alloc_one(mm, address);
	if (!new)
		return -ENOMEM;

	smp_wmb(); /* See comment in __pte_alloc */

	spin_lock(&mm->page_table_lock);
#ifndef __ARCH_HAS_4LEVEL_HACK
	if (pud_present(*pud))		/* Another has populated it */
		pmd_free(mm, new);
	else
		pud_populate(mm, pud, new);
#else
	if (pgd_present(*pud))		/* Another has populated it */
		pmd_free(mm, new);
	else
		pgd_populate(mm, pud, new);
	if (!pud_present(*pud)) {
		mm_inc_nr_pmds(mm);
		pud_populate(mm, pud, new);
	} else	/* Another has populated it */
		pmd_free(mm, new);
#else
	if (!pgd_present(*pud)) {
		mm_inc_nr_pmds(mm);
		pgd_populate(mm, pud, new);
	} else /* Another has populated it */
		pmd_free(mm, new);
#endif /* __ARCH_HAS_4LEVEL_HACK */
	spin_unlock(&mm->page_table_lock);
	return 0;
}
#endif /* __PAGETABLE_PMD_FOLDED */

int make_pages_present(unsigned long addr, unsigned long end)
{
	int ret, len, write;
	struct vm_area_struct * vma;

	vma = find_vma(current->mm, addr);
	if (!vma)
		return -ENOMEM;
	write = (vma->vm_flags & VM_WRITE) != 0;
	BUG_ON(addr >= end);
	BUG_ON(end > vma->vm_end);
	len = DIV_ROUND_UP(end, PAGE_SIZE) - addr/PAGE_SIZE;
	ret = get_user_pages(current, current->mm, addr,
			len, write, 0, NULL, NULL);
	if (ret < 0) {
		/*
		   SUS require strange return value to mlock
		    - invalid addr generate to ENOMEM.
		    - out of memory should generate EAGAIN.
		*/
		if (ret == -EFAULT)
			ret = -ENOMEM;
		else if (ret == -ENOMEM)
			ret = -EAGAIN;
		return ret;
	}
	return ret == len ? 0 : -ENOMEM;
}

#if !defined(__HAVE_ARCH_GATE_AREA)

#if defined(AT_SYSINFO_EHDR)
static struct vm_area_struct gate_vma;

static int __init gate_vma_init(void)
{
	gate_vma.vm_mm = NULL;
	gate_vma.vm_start = FIXADDR_USER_START;
	gate_vma.vm_end = FIXADDR_USER_END;
	gate_vma.vm_flags = VM_READ | VM_MAYREAD | VM_EXEC | VM_MAYEXEC;
	gate_vma.vm_page_prot = __P101;
	/*
	 * Make sure the vDSO gets into every core dump.
	 * Dumping its contents makes post-mortem fully interpretable later
	 * without matching up the same kernel and hardware config to see
	 * what PC values meant.
	 */
	gate_vma.vm_flags |= VM_ALWAYSDUMP;
	return 0;
}
__initcall(gate_vma_init);
#endif

struct vm_area_struct *get_gate_vma(struct task_struct *tsk)
{
#ifdef AT_SYSINFO_EHDR
	return &gate_vma;
#else
	return NULL;
#endif
}

int in_gate_area_no_task(unsigned long addr)
{
#ifdef AT_SYSINFO_EHDR
	if ((addr >= FIXADDR_USER_START) && (addr < FIXADDR_USER_END))
		return 1;
#endif
	return 0;
}

#endif	/* __HAVE_ARCH_GATE_AREA */

#ifdef CONFIG_HAVE_IOREMAP_PROT
static resource_size_t follow_phys(struct vm_area_struct *vma,
			unsigned long address, unsigned int flags,
			unsigned long *prot)
static int __follow_pte(struct mm_struct *mm, unsigned long address,
		pte_t **ptepp, spinlock_t **ptlp)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *ptep, pte;
	spinlock_t *ptl;
	resource_size_t phys_addr = 0;
	struct mm_struct *mm = vma->vm_mm;

	VM_BUG_ON(!(vma->vm_flags & (VM_IO | VM_PFNMAP)));

	pgd = pgd_offset(mm, address);
	if (pgd_none(*pgd) || unlikely(pgd_bad(*pgd)))
		goto no_page_table;

	pud = pud_offset(pgd, address);
	if (pud_none(*pud) || unlikely(pud_bad(*pud)))
		goto no_page_table;

	pmd = pmd_offset(pud, address);
	if (pmd_none(*pmd) || unlikely(pmd_bad(*pmd)))
		goto no_page_table;

	/* We cannot handle huge page PFN maps. Luckily they don't exist. */
	if (pmd_huge(*pmd))
		goto no_page_table;

	ptep = pte_offset_map_lock(mm, pmd, address, &ptl);
	if (!ptep)
		goto out;

	pte = *ptep;
	if (!pte_present(pte))
		goto unlock;
	if ((flags & FOLL_WRITE) && !pte_write(pte))
		goto unlock;
	phys_addr = pte_pfn(pte);
	phys_addr <<= PAGE_SHIFT; /* Shift here to avoid overflow on PAE */

	*prot = pgprot_val(pte_pgprot(pte));

unlock:
	pte_unmap_unlock(ptep, ptl);
out:
	return phys_addr;
no_page_table:
	return 0;
	pte_t *ptep;

	pgd = pgd_offset(mm, address);
	if (pgd_none(*pgd) || unlikely(pgd_bad(*pgd)))
		goto out;

	pud = pud_offset(pgd, address);
	if (pud_none(*pud) || unlikely(pud_bad(*pud)))
		goto out;

	pmd = pmd_offset(pud, address);
	VM_BUG_ON(pmd_trans_huge(*pmd));
	if (pmd_none(*pmd) || unlikely(pmd_bad(*pmd)))
		goto out;

	/* We cannot handle huge page PFN maps. Luckily they don't exist. */
	if (pmd_huge(*pmd))
		goto out;

	ptep = pte_offset_map_lock(mm, pmd, address, ptlp);
	if (!ptep)
		goto out;
	if (!pte_present(*ptep))
		goto unlock;
	*ptepp = ptep;
	return 0;
unlock:
	pte_unmap_unlock(ptep, *ptlp);
out:
	return -EINVAL;
}

static inline int follow_pte(struct mm_struct *mm, unsigned long address,
			     pte_t **ptepp, spinlock_t **ptlp)
{
	int res;

	/* (void) is needed to make gcc happy */
	(void) __cond_lock(*ptlp,
			   !(res = __follow_pte(mm, address, ptepp, ptlp)));
	return res;
}

/**
 * follow_pfn - look up PFN at a user virtual address
 * @vma: memory mapping
 * @address: user virtual address
 * @pfn: location to store found PFN
 *
 * Only IO mappings and raw PFN mappings are allowed.
 *
 * Returns zero and the pfn at @pfn on success, -ve otherwise.
 */
int follow_pfn(struct vm_area_struct *vma, unsigned long address,
	unsigned long *pfn)
{
	int ret = -EINVAL;
	spinlock_t *ptl;
	pte_t *ptep;

	if (!(vma->vm_flags & (VM_IO | VM_PFNMAP)))
		return ret;

	ret = follow_pte(vma->vm_mm, address, &ptep, &ptl);
	if (ret)
		return ret;
	*pfn = pte_pfn(*ptep);
	pte_unmap_unlock(ptep, ptl);
	return 0;
}
EXPORT_SYMBOL(follow_pfn);

#ifdef CONFIG_HAVE_IOREMAP_PROT
int follow_phys(struct vm_area_struct *vma,
		unsigned long address, unsigned int flags,
		unsigned long *prot, resource_size_t *phys)
{
	int ret = -EINVAL;
	pte_t *ptep, pte;
	spinlock_t *ptl;

	if (!(vma->vm_flags & (VM_IO | VM_PFNMAP)))
		goto out;

	if (follow_pte(vma->vm_mm, address, &ptep, &ptl))
		goto out;
	pte = *ptep;

	/* Never return PFNs of anon folios in COW mappings. */
	if (vm_normal_page(vma, address, pte))
		goto unlock;

	if ((flags & FOLL_WRITE) && !pte_write(pte))
		goto unlock;

	*prot = pgprot_val(pte_pgprot(pte));
	*phys = (resource_size_t)pte_pfn(pte) << PAGE_SHIFT;

	ret = 0;
unlock:
	pte_unmap_unlock(ptep, ptl);
out:
	return ret;
}

int generic_access_phys(struct vm_area_struct *vma, unsigned long addr,
			void *buf, int len, int write)
{
	resource_size_t phys_addr;
	unsigned long prot = 0;
	void *maddr;
	int offset = addr & (PAGE_SIZE-1);

	if (!(vma->vm_flags & (VM_IO | VM_PFNMAP)))
		return -EINVAL;

	phys_addr = follow_phys(vma, addr, write, &prot);

	if (!phys_addr)
		return -EINVAL;

	maddr = ioremap_prot(phys_addr, PAGE_SIZE, prot);
	void __iomem *maddr;
	int offset = addr & (PAGE_SIZE-1);

	if (follow_phys(vma, addr, write, &prot, &phys_addr))
		return -EINVAL;

	maddr = ioremap_prot(phys_addr, PAGE_ALIGN(len + offset), prot);
	if (!maddr)
		return -ENOMEM;

	if (write)
		memcpy_toio(maddr + offset, buf, len);
	else
		memcpy_fromio(buf, maddr + offset, len);
	iounmap(maddr);

	return len;
}
#endif

/*
 * Access another process' address space.
 * Source/target buffer must be kernel space,
 * Do not walk the page table directly, use get_user_pages
 */
int access_process_vm(struct task_struct *tsk, unsigned long addr, void *buf, int len, int write)
{
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	void *old_buf = buf;

	mm = get_task_mm(tsk);
	if (!mm)
		return 0;

EXPORT_SYMBOL_GPL(generic_access_phys);
#endif

/*
 * Access another process' address space as given in mm.  If non-NULL, use the
 * given task for page fault accounting.
 */
static int __access_remote_vm(struct task_struct *tsk, struct mm_struct *mm,
		unsigned long addr, void *buf, int len, unsigned int gup_flags)
{
	struct vm_area_struct *vma;
	void *old_buf = buf;
	int write = gup_flags & FOLL_WRITE;

	down_read(&mm->mmap_sem);
	/* ignore errors, just check how much was successfully transferred */
	while (len) {
		int bytes, ret, offset;
		void *maddr;
		struct page *page = NULL;

		ret = get_user_pages(tsk, mm, addr, 1,
				gup_flags, &page, &vma);
		if (ret <= 0) {
#ifndef CONFIG_HAVE_IOREMAP_PROT
			break;
#else
			/*
			 * Check if this is a VM_IO | VM_PFNMAP VMA, which
			 * we can access using slightly different code.
			 */
#ifdef CONFIG_HAVE_IOREMAP_PROT
			vma = find_vma(mm, addr);
			if (!vma)
			vma = find_vma(mm, addr);
			if (!vma || vma->vm_start > addr)
				break;
			if (vma->vm_ops && vma->vm_ops->access)
				ret = vma->vm_ops->access(vma, addr, buf,
							  len, write);
			if (ret <= 0)
#endif
				break;
			bytes = ret;
				break;
			bytes = ret;
#endif
		} else {
			bytes = len;
			offset = addr & (PAGE_SIZE-1);
			if (bytes > PAGE_SIZE-offset)
				bytes = PAGE_SIZE-offset;

			maddr = kmap(page);
			if (write) {
				copy_to_user_page(vma, page, addr,
						  maddr + offset, buf, bytes);
				set_page_dirty_lock(page);
			} else {
				copy_from_user_page(vma, page, addr,
						    buf, maddr + offset, bytes);
			}
			kunmap(page);
			page_cache_release(page);
		}
		len -= bytes;
		buf += bytes;
		addr += bytes;
	}
	up_read(&mm->mmap_sem);
	mmput(mm);

	return buf - old_buf;
}

/**
 * access_remote_vm - access another process' address space
 * @mm:		the mm_struct of the target address space
 * @addr:	start address to access
 * @buf:	source or destination buffer
 * @len:	number of bytes to transfer
 * @gup_flags:	flags modifying lookup behaviour
 *
 * The caller must hold a reference on @mm.
 */
int access_remote_vm(struct mm_struct *mm, unsigned long addr,
		void *buf, int len, unsigned int gup_flags)
{
	return __access_remote_vm(NULL, mm, addr, buf, len, gup_flags);
}

/*
 * Access another process' address space.
 * Source/target buffer must be kernel space,
 * Do not walk the page table directly, use get_user_pages
 */
int access_process_vm(struct task_struct *tsk, unsigned long addr,
		void *buf, int len, int write)
{
	struct mm_struct *mm;
	int ret;
	unsigned int flags = FOLL_FORCE;

	mm = get_task_mm(tsk);
	if (!mm)
		return 0;

	if (write)
		flags |= FOLL_WRITE;

	ret = __access_remote_vm(tsk, mm, addr, buf, len, flags);

	mmput(mm);

	return ret;
}

/*
 * Print the name of a VMA.
 */
void print_vma_addr(char *prefix, unsigned long ip)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;

	/*
	 * Do not print if we are in atomic
	 * contexts (in exception stacks, etc.):
	 */
	if (preempt_count())
		return;

	down_read(&mm->mmap_sem);
	vma = find_vma(mm, ip);
	if (vma && vma->vm_file) {
		struct file *f = vma->vm_file;
		char *buf = (char *)__get_free_page(GFP_KERNEL);
		if (buf) {
			char *p, *s;

			p = d_path(&f->f_path, buf, PAGE_SIZE);
			if (IS_ERR(p))
				p = "?";
			s = strrchr(p, '/');
			if (s)
				p = s+1;
			printk("%s%s[%lx+%lx]", prefix, p,
			char *p;

			p = file_path(f, buf, PAGE_SIZE);
			if (IS_ERR(p))
				p = "?";
			printk("%s%s[%lx+%lx]", prefix, kbasename(p),
					vma->vm_start,
					vma->vm_end - vma->vm_start);
			free_page((unsigned long)buf);
		}
	}
	up_read(&current->mm->mmap_sem);
}
	up_read(&mm->mmap_sem);
}

#if defined(CONFIG_PROVE_LOCKING) || defined(CONFIG_DEBUG_ATOMIC_SLEEP)
void __might_fault(const char *file, int line)
{
	/*
	 * Some code (nfs/sunrpc) uses socket ops on kernel memory while
	 * holding the mmap_sem, this is safe because kernel memory doesn't
	 * get paged out, therefore we'll never actually fault, and the
	 * below annotations will generate false positives.
	 */
	if (segment_eq(get_fs(), KERNEL_DS))
		return;
	if (pagefault_disabled())
		return;
	__might_sleep(file, line, 0);
#if defined(CONFIG_DEBUG_ATOMIC_SLEEP)
	if (current->mm)
		might_lock_read(&current->mm->mmap_sem);
#endif
}
EXPORT_SYMBOL(__might_fault);
#endif

#if defined(CONFIG_TRANSPARENT_HUGEPAGE) || defined(CONFIG_HUGETLBFS)
static void clear_gigantic_page(struct page *page,
				unsigned long addr,
				unsigned int pages_per_huge_page)
{
	int i;
	struct page *p = page;

	might_sleep();
	for (i = 0; i < pages_per_huge_page;
	     i++, p = mem_map_next(p, page, i)) {
		cond_resched();
		clear_user_highpage(p, addr + i * PAGE_SIZE);
	}
}
void clear_huge_page(struct page *page,
		     unsigned long addr, unsigned int pages_per_huge_page)
{
	int i;

	if (unlikely(pages_per_huge_page > MAX_ORDER_NR_PAGES)) {
		clear_gigantic_page(page, addr, pages_per_huge_page);
		return;
	}

	might_sleep();
	for (i = 0; i < pages_per_huge_page; i++) {
		cond_resched();
		clear_user_highpage(page + i, addr + i * PAGE_SIZE);
	}
}

static void copy_user_gigantic_page(struct page *dst, struct page *src,
				    unsigned long addr,
				    struct vm_area_struct *vma,
				    unsigned int pages_per_huge_page)
{
	int i;
	struct page *dst_base = dst;
	struct page *src_base = src;

	for (i = 0; i < pages_per_huge_page; ) {
		cond_resched();
		copy_user_highpage(dst, src, addr + i*PAGE_SIZE, vma);

		i++;
		dst = mem_map_next(dst, dst_base, i);
		src = mem_map_next(src, src_base, i);
	}
}

void copy_user_huge_page(struct page *dst, struct page *src,
			 unsigned long addr, struct vm_area_struct *vma,
			 unsigned int pages_per_huge_page)
{
	int i;

	if (unlikely(pages_per_huge_page > MAX_ORDER_NR_PAGES)) {
		copy_user_gigantic_page(dst, src, addr, vma,
					pages_per_huge_page);
		return;
	}

	might_sleep();
	for (i = 0; i < pages_per_huge_page; i++) {
		cond_resched();
		copy_user_highpage(dst + i, src + i, addr + i*PAGE_SIZE, vma);
	}
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE || CONFIG_HUGETLBFS */

#if USE_SPLIT_PTE_PTLOCKS && ALLOC_SPLIT_PTLOCKS

static struct kmem_cache *page_ptl_cachep;

void __init ptlock_cache_init(void)
{
	page_ptl_cachep = kmem_cache_create("page->ptl", sizeof(spinlock_t), 0,
			SLAB_PANIC, NULL);
}

bool ptlock_alloc(struct page *page)
{
	spinlock_t *ptl;

	ptl = kmem_cache_alloc(page_ptl_cachep, GFP_KERNEL);
	if (!ptl)
		return false;
	page->ptl = ptl;
	return true;
}

void ptlock_free(struct page *page)
{
	kmem_cache_free(page_ptl_cachep, page->ptl);
}
#endif
