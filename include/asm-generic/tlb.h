/* include/asm-generic/tlb.h
 *
 *	Generic TLB shootdown code
 *
 * Copyright 2001 Red Hat, Inc.
 * Based on code from mm/memory.c Copyright Linus Torvalds and others.
 *
 * Copyright 2011 Red Hat, Inc., Peter Zijlstra
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _ASM_GENERIC__TLB_H
#define _ASM_GENERIC__TLB_H

#include <linux/swap.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>

/*
 * For UP we don't need to worry about TLB flush
 * and page free order so much..
 */
#ifdef CONFIG_SMP
  #ifdef ARCH_FREE_PTR_NR
    #define FREE_PTR_NR   ARCH_FREE_PTR_NR
  #else
    #define FREE_PTE_NR	506
  #endif
  #define tlb_fast_mode(tlb) ((tlb)->nr == ~0U)
#else
  #define FREE_PTE_NR	1
  #define tlb_fast_mode(tlb) 1
#endif

#ifdef CONFIG_HAVE_RCU_TABLE_FREE
/*
 * Semi RCU freeing of the page directories.
 *
 * This is needed by some architectures to implement software pagetable walkers.
 *
 * gup_fast() and other software pagetable walkers do a lockless page-table
 * walk and therefore needs some synchronization with the freeing of the page
 * directories. The chosen means to accomplish that is by disabling IRQs over
 * the walk.
 *
 * Architectures that use IPIs to flush TLBs will then automagically DTRT,
 * since we unlink the page, flush TLBs, free the page. Since the disabling of
 * IRQs delays the completion of the TLB flush we can never observe an already
 * freed page.
 *
 * Architectures that do not have this (PPC) need to delay the freeing by some
 * other means, this is that means.
 *
 * What we do is batch the freed directory pages (tables) and RCU free them.
 * We use the sched RCU variant, as that guarantees that IRQ/preempt disabling
 * holds off grace periods.
 *
 * However, in order to batch these pages we need to allocate storage, this
 * allocation is deep inside the MM code and can thus easily fail on memory
 * pressure. To guarantee progress we fall back to single table freeing, see
 * the implementation of tlb_remove_table_one().
 *
 */
struct mmu_table_batch {
	struct rcu_head		rcu;
	unsigned int		nr;
	void			*tables[0];
};

#define MAX_TABLE_BATCH		\
	((PAGE_SIZE - sizeof(struct mmu_table_batch)) / sizeof(void *))

extern void tlb_table_flush(struct mmu_gather *tlb);
extern void tlb_remove_table(struct mmu_gather *tlb, void *table);

#endif

/*
 * If we can't allocate a page to make a big batch of page pointers
 * to work on, then just handle a few from the on-stack structure.
 */
#define MMU_GATHER_BUNDLE	8

struct mmu_gather_batch {
	struct mmu_gather_batch	*next;
	unsigned int		nr;
	unsigned int		max;
	struct page		*pages[0];
};

#define MAX_GATHER_BATCH	\
	((PAGE_SIZE - sizeof(struct mmu_gather_batch)) / sizeof(void *))

/*
 * Limit the maximum number of mmu_gather batches to reduce a risk of soft
 * lockups for non-preemptible kernels on huge machines when a lot of memory
 * is zapped during unmapping.
 * 10K pages freed at once should be safe even without a preemption point.
 */
#define MAX_GATHER_BATCH_COUNT	(10000UL/MAX_GATHER_BATCH)

/* struct mmu_gather is an opaque type used by the mm code for passing around
 * any data needed by arch specific code for tlb_remove_page.
 */
struct mmu_gather {
	struct mm_struct	*mm;
	unsigned int		nr;	/* set to ~0U means fast mode */
	unsigned int		need_flush;/* Really unmapped some ptes? */
	unsigned int		fullmm; /* non-zero means full mm flush */
	struct page *		pages[FREE_PTE_NR];
};

/* Users of the generic TLB shootdown code must declare this storage space. */
DECLARE_PER_CPU(struct mmu_gather, mmu_gathers);

/* tlb_gather_mmu
 *	Return a pointer to an initialized struct mmu_gather.
 */
static inline struct mmu_gather *
tlb_gather_mmu(struct mm_struct *mm, unsigned int full_mm_flush)
{
	struct mmu_gather *tlb = &get_cpu_var(mmu_gathers);

	tlb->mm = mm;

	/* Use fast mode if only one CPU is online */
	tlb->nr = num_online_cpus() > 1 ? 0U : ~0U;

	tlb->fullmm = full_mm_flush;

	return tlb;
}

static inline void
tlb_flush_mmu(struct mmu_gather *tlb, unsigned long start, unsigned long end)
{
	if (!tlb->need_flush)
		return;
	tlb->need_flush = 0;
	tlb_flush(tlb);
	if (!tlb_fast_mode(tlb)) {
		free_pages_and_swap_cache(tlb->pages, tlb->nr);
		tlb->nr = 0;
	}
}

/* tlb_finish_mmu
 *	Called at the end of the shootdown operation to free up any resources
 *	that were required.
 */
static inline void
tlb_finish_mmu(struct mmu_gather *tlb, unsigned long start, unsigned long end)
{
	tlb_flush_mmu(tlb, start, end);

	/* keep the page table cache within bounds */
	check_pgt_cache();

	put_cpu_var(mmu_gathers);
}

/* tlb_remove_page
 *	Must perform the equivalent to __free_pte(pte_get_and_clear(ptep)), while
 *	handling the additional races in SMP caused by other CPUs caching valid
 *	mappings in their TLBs.
 */
static inline void tlb_remove_page(struct mmu_gather *tlb, struct page *page)
{
	tlb->need_flush = 1;
	if (tlb_fast_mode(tlb)) {
		free_page_and_swap_cache(page);
		return;
	}
	tlb->pages[tlb->nr++] = page;
	if (tlb->nr >= FREE_PTE_NR)
		tlb_flush_mmu(tlb, 0, 0);
}

/**
 * tlb_remove_tlb_entry - remember a pte unmapping for later tlb invalidation.
 *
 * Record the fact that pte's were really umapped in ->need_flush, so we can
 * later optimise away the tlb invalidate.   This helps when userspace is
 * unmapping already-unmapped pages, which happens quite a lot.
 */
#define tlb_remove_tlb_entry(tlb, ptep, address)		\
	do {							\
		tlb->need_flush = 1;				\
		__tlb_remove_tlb_entry(tlb, ptep, address);	\
	} while (0)

#define pte_free_tlb(tlb, ptep)					\
	do {							\
		tlb->need_flush = 1;				\
		__pte_free_tlb(tlb, ptep);			\
	} while (0)

#ifndef __ARCH_HAS_4LEVEL_HACK
#define pud_free_tlb(tlb, pudp)					\
	do {							\
		tlb->need_flush = 1;				\
		__pud_free_tlb(tlb, pudp);			\
	} while (0)
#endif

#define pmd_free_tlb(tlb, pmdp)					\
	do {							\
		tlb->need_flush = 1;				\
		__pmd_free_tlb(tlb, pmdp);			\
#ifdef CONFIG_HAVE_RCU_TABLE_FREE
	struct mmu_table_batch	*batch;
#endif
	unsigned long		start;
	unsigned long		end;
	/* we are in the middle of an operation to clear
	 * a full mm and can make some optimizations */
	unsigned int		fullmm : 1,
	/* we have performed an operation which
	 * requires a complete flush of the tlb */
				need_flush_all : 1;

	struct mmu_gather_batch *active;
	struct mmu_gather_batch	local;
	struct page		*__pages[MMU_GATHER_BUNDLE];
	unsigned int		batch_count;
};

#define HAVE_GENERIC_MMU_GATHER

void tlb_gather_mmu(struct mmu_gather *tlb, struct mm_struct *mm, unsigned long start, unsigned long end);
void tlb_flush_mmu(struct mmu_gather *tlb);
void tlb_finish_mmu(struct mmu_gather *tlb, unsigned long start,
							unsigned long end);
int __tlb_remove_page(struct mmu_gather *tlb, struct page *page);

/* tlb_remove_page
 *	Similar to __tlb_remove_page but will call tlb_flush_mmu() itself when
 *	required.
 */
static inline void tlb_remove_page(struct mmu_gather *tlb, struct page *page)
{
	if (!__tlb_remove_page(tlb, page))
		tlb_flush_mmu(tlb);
}

static inline void __tlb_adjust_range(struct mmu_gather *tlb,
				      unsigned long address)
{
	tlb->start = min(tlb->start, address);
	tlb->end = max(tlb->end, address + PAGE_SIZE);
}

static inline void __tlb_reset_range(struct mmu_gather *tlb)
{
	if (tlb->fullmm) {
		tlb->start = tlb->end = ~0;
	} else {
		tlb->start = TASK_SIZE;
		tlb->end = 0;
	}
}

/*
 * In the case of tlb vma handling, we can optimise these away in the
 * case where we're doing a full MM flush.  When we're doing a munmap,
 * the vmas are adjusted to only cover the region to be torn down.
 */
#ifndef tlb_start_vma
#define tlb_start_vma(tlb, vma) do { } while (0)
#endif

#define __tlb_end_vma(tlb, vma)					\
	do {							\
		if (!tlb->fullmm && tlb->end) {			\
			tlb_flush(tlb);				\
			__tlb_reset_range(tlb);			\
		}						\
	} while (0)

#ifndef tlb_end_vma
#define tlb_end_vma	__tlb_end_vma
#endif

static inline void tlb_flush_pmd_range(struct mmu_gather *tlb,
				unsigned long address, unsigned long size)
{
	tlb->start = min(tlb->start, address);
	tlb->end = max(tlb->end, address + size);
}

#ifndef __tlb_remove_tlb_entry
#define __tlb_remove_tlb_entry(tlb, ptep, address) do { } while (0)
#endif

/**
 * tlb_remove_tlb_entry - remember a pte unmapping for later tlb invalidation.
 *
 * Record the fact that pte's were really unmapped by updating the range,
 * so we can later optimise away the tlb invalidate.   This helps when
 * userspace is unmapping already-unmapped pages, which happens quite a lot.
 */
#define tlb_remove_tlb_entry(tlb, ptep, address)		\
	do {							\
		__tlb_adjust_range(tlb, address);		\
		__tlb_remove_tlb_entry(tlb, ptep, address);	\
	} while (0)

/**
 * tlb_remove_pmd_tlb_entry - remember a pmd mapping for later tlb invalidation
 * This is a nop so far, because only x86 needs it.
 */
#ifndef __tlb_remove_pmd_tlb_entry
#define __tlb_remove_pmd_tlb_entry(tlb, pmdp, address) do {} while (0)
#endif

#define tlb_remove_pmd_tlb_entry(tlb, pmdp, address)		\
	do {							\
		__tlb_adjust_range(tlb, address);		\
		__tlb_remove_pmd_tlb_entry(tlb, pmdp, address);	\
	} while (0)

#define pte_free_tlb(tlb, ptep, address)			\
	do {							\
		__tlb_adjust_range(tlb, address);		\
		__pte_free_tlb(tlb, ptep, address);		\
	} while (0)

#ifndef __ARCH_HAS_4LEVEL_HACK
#define pud_free_tlb(tlb, pudp, address)			\
	do {							\
		__tlb_adjust_range(tlb, address);		\
		__pud_free_tlb(tlb, pudp, address);		\
	} while (0)
#endif

#define pmd_free_tlb(tlb, pmdp, address)			\
	do {							\
		__tlb_adjust_range(tlb, address);		\
		__pmd_free_tlb(tlb, pmdp, address);		\
	} while (0)

#define tlb_migrate_finish(mm) do {} while (0)

#endif /* _ASM_GENERIC__TLB_H */
