/*
 * PPC64 (POWER4) Huge TLB Page Support for Kernel.
 *
 * Copyright (C) 2003 David Gibson, IBM Corporation.
 * PPC Huge TLB Page Support for Kernel.
 *
 * Copyright (C) 2003 David Gibson, IBM Corporation.
 * Copyright (C) 2011 Becky Bruce, Freescale Semiconductor
 *
 * Based on the IA-32 version:
 * Copyright (C) 2002, Rohit Seth <rohit.seth@intel.com>
 */

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/sysctl.h>
#include <asm/mman.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/mmu_context.h>
#include <asm/machdep.h>
#include <asm/cputable.h>
#include <asm/spu.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/hugetlb.h>
#include <linux/export.h>
#include <linux/of_fdt.h>
#include <linux/memblock.h>
#include <linux/bootmem.h>
#include <linux/moduleparam.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>
#include <asm/setup.h>
#include <asm/hugetlb.h>

#ifdef CONFIG_HUGETLB_PAGE

#define PAGE_SHIFT_64K	16
#define PAGE_SHIFT_16M	24
#define PAGE_SHIFT_16G	34

#define NUM_LOW_AREAS	(0x100000000UL >> SID_SHIFT)
#define NUM_HIGH_AREAS	(PGTABLE_RANGE >> HTLB_AREA_SHIFT)
#define MAX_NUMBER_GPAGES	1024

/* Tracks the 16G pages after the device tree is scanned and before the
 * huge_boot_pages list is ready.  */
static unsigned long gpage_freearray[MAX_NUMBER_GPAGES];
static unsigned nr_gpages;

/* Array of valid huge page sizes - non-zero value(hugepte_shift) is
 * stored for the huge page sizes that are valid.
 */
unsigned int mmu_huge_psizes[MMU_PAGE_COUNT] = { }; /* initialize all to 0 */

#define hugepte_shift			mmu_huge_psizes
#define PTRS_PER_HUGEPTE(psize)		(1 << hugepte_shift[psize])
#define HUGEPTE_TABLE_SIZE(psize)	(sizeof(pte_t) << hugepte_shift[psize])

#define HUGEPD_SHIFT(psize)		(mmu_psize_to_shift(psize) \
						+ hugepte_shift[psize])
#define HUGEPD_SIZE(psize)		(1UL << HUGEPD_SHIFT(psize))
#define HUGEPD_MASK(psize)		(~(HUGEPD_SIZE(psize)-1))

/* Subtract one from array size because we don't need a cache for 4K since
 * is not a huge page size */
#define huge_pgtable_cache(psize)	(pgtable_cache[HUGEPTE_CACHE_NUM \
							+ psize-1])
#define HUGEPTE_CACHE_NAME(psize)	(huge_pgtable_cache_name[psize])

static const char *huge_pgtable_cache_name[MMU_PAGE_COUNT] = {
	"unused_4K", "hugepte_cache_64K", "unused_64K_AP",
	"hugepte_cache_1M", "hugepte_cache_16M", "hugepte_cache_16G"
};

/* Flag to mark huge PD pointers.  This means pmd_bad() and pud_bad()
 * will choke on pointers to hugepte tables, which is handy for
 * catching screwups early. */
#define HUGEPD_OK	0x1

typedef struct { unsigned long pd; } hugepd_t;

#define hugepd_none(hpd)	((hpd).pd == 0)

static inline int shift_to_mmu_psize(unsigned int shift)
{
	switch (shift) {
#ifndef CONFIG_PPC_64K_PAGES
	case PAGE_SHIFT_64K:
	    return MMU_PAGE_64K;
#endif
	case PAGE_SHIFT_16M:
	    return MMU_PAGE_16M;
	case PAGE_SHIFT_16G:
	    return MMU_PAGE_16G;
	}
	return -1;
}

static inline unsigned int mmu_psize_to_shift(unsigned int mmu_psize)
{
	if (mmu_psize_defs[mmu_psize].shift)
		return mmu_psize_defs[mmu_psize].shift;
	BUG();
}

static inline pte_t *hugepd_page(hugepd_t hpd)
{
	BUG_ON(!(hpd.pd & HUGEPD_OK));
	return (pte_t *)(hpd.pd & ~HUGEPD_OK);
}

static inline pte_t *hugepte_offset(hugepd_t *hpdp, unsigned long addr,
				    struct hstate *hstate)
{
	unsigned int shift = huge_page_shift(hstate);
	int psize = shift_to_mmu_psize(shift);
	unsigned long idx = ((addr >> shift) & (PTRS_PER_HUGEPTE(psize)-1));
	pte_t *dir = hugepd_page(*hpdp);

	return dir + idx;
}

static int __hugepte_alloc(struct mm_struct *mm, hugepd_t *hpdp,
			   unsigned long address, unsigned int psize)
{
	pte_t *new = kmem_cache_zalloc(huge_pgtable_cache(psize),
				      GFP_KERNEL|__GFP_REPEAT);
unsigned int HPAGE_SHIFT;

/*
 * Tracks gpages after the device tree is scanned and before the
 * huge_boot_pages list is ready.  On non-Freescale implementations, this is
 * just used to track 16G pages and so is a single array.  FSL-based
 * implementations may have more than one gpage size, so we need multiple
 * arrays
 */
#ifdef CONFIG_PPC_FSL_BOOK3E
#define MAX_NUMBER_GPAGES	128
struct psize_gpages {
	u64 gpage_list[MAX_NUMBER_GPAGES];
	unsigned int nr_gpages;
};
static struct psize_gpages gpage_freearray[MMU_PAGE_COUNT];
#else
#define MAX_NUMBER_GPAGES	1024
static u64 gpage_freearray[MAX_NUMBER_GPAGES];
static unsigned nr_gpages;
#endif

#define hugepd_none(hpd)	((hpd).pd == 0)

#ifdef CONFIG_PPC_BOOK3S_64
/*
 * At this point we do the placement change only for BOOK3S 64. This would
 * possibly work on other subarchs.
 */

/*
 * We have PGD_INDEX_SIZ = 12 and PTE_INDEX_SIZE = 8, so that we can have
 * 16GB hugepage pte in PGD and 16MB hugepage pte at PMD;
 *
 * Defined in such a way that we can optimize away code block at build time
 * if CONFIG_HUGETLB_PAGE=n.
 */
int pmd_huge(pmd_t pmd)
{
	/*
	 * leaf pte for huge page, bottom two bits != 00
	 */
	return ((pmd_val(pmd) & 0x3) != 0x0);
}

int pud_huge(pud_t pud)
{
	/*
	 * leaf pte for huge page, bottom two bits != 00
	 */
	return ((pud_val(pud) & 0x3) != 0x0);
}

int pgd_huge(pgd_t pgd)
{
	/*
	 * leaf pte for huge page, bottom two bits != 00
	 */
	return ((pgd_val(pgd) & 0x3) != 0x0);
}

#if defined(CONFIG_PPC_64K_PAGES) && defined(CONFIG_DEBUG_VM)
/*
 * This enables us to catch the wrong page directory format
 * Moved here so that we can use WARN() in the call.
 */
int hugepd_ok(hugepd_t hpd)
{
	bool is_hugepd;

	/*
	 * We should not find this format in page directory, warn otherwise.
	 */
	is_hugepd = (((hpd.pd & 0x3) == 0x0) && ((hpd.pd & HUGEPD_SHIFT_MASK) != 0));
	WARN(is_hugepd, "Found wrong page directory format\n");
	return 0;
}
#endif

#else
int pmd_huge(pmd_t pmd)
{
	return 0;
}

int pud_huge(pud_t pud)
{
	return 0;
}

int pgd_huge(pgd_t pgd)
{
	return 0;
}
#endif

pte_t *huge_pte_offset(struct mm_struct *mm, unsigned long addr)
{
	/* Only called for hugetlbfs pages, hence can ignore THP */
	return __find_linux_pte_or_hugepte(mm->pgd, addr, NULL, NULL);
}

static int __hugepte_alloc(struct mm_struct *mm, hugepd_t *hpdp,
			   unsigned long address, unsigned pdshift, unsigned pshift)
{
	struct kmem_cache *cachep;
	pte_t *new;

#ifdef CONFIG_PPC_FSL_BOOK3E
	int i;
	int num_hugepd = 1 << (pshift - pdshift);
	cachep = hugepte_cache;
#else
	cachep = PGT_CACHE(pdshift - pshift);
#endif

	new = kmem_cache_zalloc(cachep, GFP_KERNEL|__GFP_REPEAT);

	BUG_ON(pshift > HUGEPD_SHIFT_MASK);
	BUG_ON((unsigned long)new & HUGEPD_SHIFT_MASK);

	if (! new)
		return -ENOMEM;

	spin_lock(&mm->page_table_lock);
	if (!hugepd_none(*hpdp))
		kmem_cache_free(huge_pgtable_cache(psize), new);
	else
		hpdp->pd = (unsigned long)new | HUGEPD_OK;
#ifdef CONFIG_PPC_FSL_BOOK3E
	/*
	 * We have multiple higher-level entries that point to the same
	 * actual pte location.  Fill in each as we go and backtrack on error.
	 * We need all of these so the DTLB pgtable walk code can find the
	 * right higher-level entry without knowing if it's a hugepage or not.
	 */
	for (i = 0; i < num_hugepd; i++, hpdp++) {
		if (unlikely(!hugepd_none(*hpdp)))
			break;
		else
			/* We use the old format for PPC_FSL_BOOK3E */
			hpdp->pd = ((unsigned long)new & ~PD_HUGE) | pshift;
	}
	/* If we bailed from the for loop early, an error occurred, clean up */
	if (i < num_hugepd) {
		for (i = i - 1 ; i >= 0; i--, hpdp--)
			hpdp->pd = 0;
		kmem_cache_free(cachep, new);
	}
#else
	if (!hugepd_none(*hpdp))
		kmem_cache_free(cachep, new);
	else {
#ifdef CONFIG_PPC_BOOK3S_64
		hpdp->pd = (unsigned long)new |
			    (shift_to_mmu_psize(pshift) << 2);
#else
		hpdp->pd = ((unsigned long)new & ~PD_HUGE) | pshift;
#endif
	}
#endif
	spin_unlock(&mm->page_table_lock);
	return 0;
}

/* Base page size affects how we walk hugetlb page tables */
#ifdef CONFIG_PPC_64K_PAGES
#define hpmd_offset(pud, addr, h)	pmd_offset(pud, addr)
#define hpmd_alloc(mm, pud, addr, h)	pmd_alloc(mm, pud, addr)
#else
static inline
pmd_t *hpmd_offset(pud_t *pud, unsigned long addr, struct hstate *hstate)
{
	if (huge_page_shift(hstate) == PAGE_SHIFT_64K)
		return pmd_offset(pud, addr);
	else
		return (pmd_t *) pud;
}
static inline
pmd_t *hpmd_alloc(struct mm_struct *mm, pud_t *pud, unsigned long addr,
		  struct hstate *hstate)
{
	if (huge_page_shift(hstate) == PAGE_SHIFT_64K)
		return pmd_alloc(mm, pud, addr);
	else
		return (pmd_t *) pud;
}
#endif

/* Build list of addresses of gigantic pages.  This function is used in early
 * boot before the buddy or bootmem allocator is setup.
 */
void add_gpage(unsigned long addr, unsigned long page_size,
	unsigned long number_of_pages)
/*
 * These macros define how to determine which level of the page table holds
 * the hpdp.
 */
#ifdef CONFIG_PPC_FSL_BOOK3E
#define HUGEPD_PGD_SHIFT PGDIR_SHIFT
#define HUGEPD_PUD_SHIFT PUD_SHIFT
#else
#define HUGEPD_PGD_SHIFT PUD_SHIFT
#define HUGEPD_PUD_SHIFT PMD_SHIFT
#endif

#ifdef CONFIG_PPC_BOOK3S_64
/*
 * At this point we do the placement change only for BOOK3S 64. This would
 * possibly work on other subarchs.
 */
pte_t *huge_pte_alloc(struct mm_struct *mm, unsigned long addr, unsigned long sz)
{
	pgd_t *pg;
	pud_t *pu;
	pmd_t *pm;
	hugepd_t *hpdp = NULL;
	unsigned pshift = __ffs(sz);
	unsigned pdshift = PGDIR_SHIFT;

	addr &= ~(sz-1);
	pg = pgd_offset(mm, addr);

	if (pshift == PGDIR_SHIFT)
		/* 16GB huge page */
		return (pte_t *) pg;
	else if (pshift > PUD_SHIFT)
		/*
		 * We need to use hugepd table
		 */
		hpdp = (hugepd_t *)pg;
	else {
		pdshift = PUD_SHIFT;
		pu = pud_alloc(mm, pg, addr);
		if (pshift == PUD_SHIFT)
			return (pte_t *)pu;
		else if (pshift > PMD_SHIFT)
			hpdp = (hugepd_t *)pu;
		else {
			pdshift = PMD_SHIFT;
			pm = pmd_alloc(mm, pu, addr);
			if (pshift == PMD_SHIFT)
				/* 16MB hugepage */
				return (pte_t *)pm;
			else
				hpdp = (hugepd_t *)pm;
		}
	}
	if (!hpdp)
		return NULL;

	BUG_ON(!hugepd_none(*hpdp) && !hugepd_ok(*hpdp));

	if (hugepd_none(*hpdp) && __hugepte_alloc(mm, hpdp, addr, pdshift, pshift))
		return NULL;

	return hugepte_offset(*hpdp, addr, pdshift);
}

#else

pte_t *huge_pte_alloc(struct mm_struct *mm, unsigned long addr, unsigned long sz)
{
	pgd_t *pg;
	pud_t *pu;
	pmd_t *pm;
	hugepd_t *hpdp = NULL;
	unsigned pshift = __ffs(sz);
	unsigned pdshift = PGDIR_SHIFT;

	addr &= ~(sz-1);

	pg = pgd_offset(mm, addr);

	if (pshift >= HUGEPD_PGD_SHIFT) {
		hpdp = (hugepd_t *)pg;
	} else {
		pdshift = PUD_SHIFT;
		pu = pud_alloc(mm, pg, addr);
		if (pshift >= HUGEPD_PUD_SHIFT) {
			hpdp = (hugepd_t *)pu;
		} else {
			pdshift = PMD_SHIFT;
			pm = pmd_alloc(mm, pu, addr);
			hpdp = (hugepd_t *)pm;
		}
	}

	if (!hpdp)
		return NULL;

	BUG_ON(!hugepd_none(*hpdp) && !hugepd_ok(*hpdp));

	if (hugepd_none(*hpdp) && __hugepte_alloc(mm, hpdp, addr, pdshift, pshift))
		return NULL;

	return hugepte_offset(*hpdp, addr, pdshift);
}
#endif

#ifdef CONFIG_PPC_FSL_BOOK3E
/* Build list of addresses of gigantic pages.  This function is used in early
 * boot before the buddy allocator is setup.
 */
void add_gpage(u64 addr, u64 page_size, unsigned long number_of_pages)
{
	unsigned int idx = shift_to_mmu_psize(__ffs(page_size));
	int i;

	if (addr == 0)
		return;

	gpage_freearray[idx].nr_gpages = number_of_pages;

	for (i = 0; i < number_of_pages; i++) {
		gpage_freearray[idx].gpage_list[i] = addr;
		addr += page_size;
	}
}

/*
 * Moves the gigantic page addresses from the temporary list to the
 * huge_boot_pages list.
 */
int alloc_bootmem_huge_page(struct hstate *hstate)
{
	struct huge_bootmem_page *m;
	int idx = shift_to_mmu_psize(huge_page_shift(hstate));
	int nr_gpages = gpage_freearray[idx].nr_gpages;

	if (nr_gpages == 0)
		return 0;

#ifdef CONFIG_HIGHMEM
	/*
	 * If gpages can be in highmem we can't use the trick of storing the
	 * data structure in the page; allocate space for this
	 */
	m = memblock_virt_alloc(sizeof(struct huge_bootmem_page), 0);
	m->phys = gpage_freearray[idx].gpage_list[--nr_gpages];
#else
	m = phys_to_virt(gpage_freearray[idx].gpage_list[--nr_gpages]);
#endif

	list_add(&m->list, &huge_boot_pages);
	gpage_freearray[idx].nr_gpages = nr_gpages;
	gpage_freearray[idx].gpage_list[nr_gpages] = 0;
	m->hstate = hstate;

	return 1;
}
/*
 * Scan the command line hugepagesz= options for gigantic pages; store those in
 * a list that we use to allocate the memory once all options are parsed.
 */

unsigned long gpage_npages[MMU_PAGE_COUNT];

static int __init do_gpage_early_setup(char *param, char *val,
				       const char *unused, void *arg)
{
	static phys_addr_t size;
	unsigned long npages;

	/*
	 * The hugepagesz and hugepages cmdline options are interleaved.  We
	 * use the size variable to keep track of whether or not this was done
	 * properly and skip over instances where it is incorrect.  Other
	 * command-line parsing code will issue warnings, so we don't need to.
	 *
	 */
	if ((strcmp(param, "default_hugepagesz") == 0) ||
	    (strcmp(param, "hugepagesz") == 0)) {
		size = memparse(val, NULL);
	} else if (strcmp(param, "hugepages") == 0) {
		if (size != 0) {
			if (sscanf(val, "%lu", &npages) <= 0)
				npages = 0;
			if (npages > MAX_NUMBER_GPAGES) {
				pr_warn("MMU: %lu pages requested for page "
					"size %llu KB, limiting to "
					__stringify(MAX_NUMBER_GPAGES) "\n",
					npages, size / 1024);
				npages = MAX_NUMBER_GPAGES;
			}
			gpage_npages[shift_to_mmu_psize(__ffs(size))] = npages;
			size = 0;
		}
	}
	return 0;
}


/*
 * This function allocates physical space for pages that are larger than the
 * buddy allocator can handle.  We want to allocate these in highmem because
 * the amount of lowmem is limited.  This means that this function MUST be
 * called before lowmem_end_addr is set up in MMU_init() in order for the lmb
 * allocate to grab highmem.
 */
void __init reserve_hugetlb_gpages(void)
{
	static __initdata char cmdline[COMMAND_LINE_SIZE];
	phys_addr_t size, base;
	int i;

	strlcpy(cmdline, boot_command_line, COMMAND_LINE_SIZE);
	parse_args("hugetlb gpages", cmdline, NULL, 0, 0, 0,
			NULL, &do_gpage_early_setup);

	/*
	 * Walk gpage list in reverse, allocating larger page sizes first.
	 * Skip over unsupported sizes, or sizes that have 0 gpages allocated.
	 * When we reach the point in the list where pages are no longer
	 * considered gpages, we're done.
	 */
	for (i = MMU_PAGE_COUNT-1; i >= 0; i--) {
		if (mmu_psize_defs[i].shift == 0 || gpage_npages[i] == 0)
			continue;
		else if (mmu_psize_to_shift(i) < (MAX_ORDER + PAGE_SHIFT))
			break;

		size = (phys_addr_t)(1ULL << mmu_psize_to_shift(i));
		base = memblock_alloc_base(size * gpage_npages[i], size,
					   MEMBLOCK_ALLOC_ANYWHERE);
		add_gpage(base, size, gpage_npages[i]);
	}
}

#else /* !PPC_FSL_BOOK3E */

/* Build list of addresses of gigantic pages.  This function is used in early
 * boot before the buddy allocator is setup.
 */
void add_gpage(u64 addr, u64 page_size, unsigned long number_of_pages)
{
	if (!addr)
		return;
	while (number_of_pages > 0) {
		gpage_freearray[nr_gpages] = addr;
		nr_gpages++;
		number_of_pages--;
		addr += page_size;
	}
}

/* Moves the gigantic page addresses from the temporary list to the
 * huge_boot_pages list.
 */
int alloc_bootmem_huge_page(struct hstate *hstate)
{
	struct huge_bootmem_page *m;
	if (nr_gpages == 0)
		return 0;
	m = phys_to_virt(gpage_freearray[--nr_gpages]);
	gpage_freearray[nr_gpages] = 0;
	list_add(&m->list, &huge_boot_pages);
	m->hstate = hstate;
	return 1;
}


/* Modelled after find_linux_pte() */
pte_t *huge_pte_offset(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pg;
	pud_t *pu;
	pmd_t *pm;

	unsigned int psize;
	unsigned int shift;
	unsigned long sz;
	struct hstate *hstate;
	psize = get_slice_psize(mm, addr);
	shift = mmu_psize_to_shift(psize);
	sz = ((1UL) << shift);
	hstate = size_to_hstate(sz);

	addr &= hstate->mask;

	pg = pgd_offset(mm, addr);
	if (!pgd_none(*pg)) {
		pu = pud_offset(pg, addr);
		if (!pud_none(*pu)) {
			pm = hpmd_offset(pu, addr, hstate);
			if (!pmd_none(*pm))
				return hugepte_offset((hugepd_t *)pm, addr,
						      hstate);
		}
	}

	return NULL;
}

pte_t *huge_pte_alloc(struct mm_struct *mm,
			unsigned long addr, unsigned long sz)
{
	pgd_t *pg;
	pud_t *pu;
	pmd_t *pm;
	hugepd_t *hpdp = NULL;
	struct hstate *hstate;
	unsigned int psize;
	hstate = size_to_hstate(sz);

	psize = get_slice_psize(mm, addr);
	BUG_ON(!mmu_huge_psizes[psize]);

	addr &= hstate->mask;

	pg = pgd_offset(mm, addr);
	pu = pud_alloc(mm, pg, addr);

	if (pu) {
		pm = hpmd_alloc(mm, pu, addr, hstate);
		if (pm)
			hpdp = (hugepd_t *)pm;
	}

	if (! hpdp)
		return NULL;

	if (hugepd_none(*hpdp) && __hugepte_alloc(mm, hpdp, addr, psize))
		return NULL;

	return hugepte_offset(hpdp, addr, hstate);
}

int huge_pmd_unshare(struct mm_struct *mm, unsigned long *addr, pte_t *ptep)
{
	return 0;
}

static void free_hugepte_range(struct mmu_gather *tlb, hugepd_t *hpdp,
			       unsigned int psize)
{
	pte_t *hugepte = hugepd_page(*hpdp);

	hpdp->pd = 0;
	tlb->need_flush = 1;
	pgtable_free_tlb(tlb, pgtable_free_cache(hugepte,
						 HUGEPTE_CACHE_NUM+psize-1,
						 PGF_CACHENUM_MASK));
#endif

#ifdef CONFIG_PPC_FSL_BOOK3E
#define HUGEPD_FREELIST_SIZE \
	((PAGE_SIZE - sizeof(struct hugepd_freelist)) / sizeof(pte_t))

struct hugepd_freelist {
	struct rcu_head	rcu;
	unsigned int index;
	void *ptes[0];
};

static DEFINE_PER_CPU(struct hugepd_freelist *, hugepd_freelist_cur);

static void hugepd_free_rcu_callback(struct rcu_head *head)
{
	struct hugepd_freelist *batch =
		container_of(head, struct hugepd_freelist, rcu);
	unsigned int i;

	for (i = 0; i < batch->index; i++)
		kmem_cache_free(hugepte_cache, batch->ptes[i]);

	free_page((unsigned long)batch);
}

static void hugepd_free(struct mmu_gather *tlb, void *hugepte)
{
	struct hugepd_freelist **batchp;

	batchp = &get_cpu_var(hugepd_freelist_cur);

	if (atomic_read(&tlb->mm->mm_users) < 2 ||
	    cpumask_equal(mm_cpumask(tlb->mm),
			  cpumask_of(smp_processor_id()))) {
		kmem_cache_free(hugepte_cache, hugepte);
		put_cpu_var(hugepd_freelist_cur);
		return;
	}

	if (*batchp == NULL) {
		*batchp = (struct hugepd_freelist *)__get_free_page(GFP_ATOMIC);
		(*batchp)->index = 0;
	}

	(*batchp)->ptes[(*batchp)->index++] = hugepte;
	if ((*batchp)->index == HUGEPD_FREELIST_SIZE) {
		call_rcu_sched(&(*batchp)->rcu, hugepd_free_rcu_callback);
		*batchp = NULL;
	}
	put_cpu_var(hugepd_freelist_cur);
}
#endif

static void free_hugepd_range(struct mmu_gather *tlb, hugepd_t *hpdp, int pdshift,
			      unsigned long start, unsigned long end,
			      unsigned long floor, unsigned long ceiling)
{
	pte_t *hugepte = hugepd_page(*hpdp);
	int i;

	unsigned long pdmask = ~((1UL << pdshift) - 1);
	unsigned int num_hugepd = 1;

#ifdef CONFIG_PPC_FSL_BOOK3E
	/* Note: On fsl the hpdp may be the first of several */
	num_hugepd = (1 << (hugepd_shift(*hpdp) - pdshift));
#else
	unsigned int shift = hugepd_shift(*hpdp);
#endif

	start &= pdmask;
	if (start < floor)
		return;
	if (ceiling) {
		ceiling &= pdmask;
		if (! ceiling)
			return;
	}
	if (end - 1 > ceiling - 1)
		return;

	for (i = 0; i < num_hugepd; i++, hpdp++)
		hpdp->pd = 0;

#ifdef CONFIG_PPC_FSL_BOOK3E
	hugepd_free(tlb, hugepte);
#else
	pgtable_free_tlb(tlb, hugepte, pdshift - shift);
#endif
}

static void hugetlb_free_pmd_range(struct mmu_gather *tlb, pud_t *pud,
				   unsigned long addr, unsigned long end,
				   unsigned long floor, unsigned long ceiling,
				   unsigned int psize)
				   unsigned long floor, unsigned long ceiling)
{
	pmd_t *pmd;
	unsigned long next;
	unsigned long start;

	start = addr;
	pmd = pmd_offset(pud, addr);
	do {
		next = pmd_addr_end(addr, end);
		if (pmd_none(*pmd))
			continue;
		free_hugepte_range(tlb, (hugepd_t *)pmd, psize);
	} while (pmd++, addr = next, addr != end);
	do {
		pmd = pmd_offset(pud, addr);
		next = pmd_addr_end(addr, end);
		if (!is_hugepd(__hugepd(pmd_val(*pmd)))) {
			/*
			 * if it is not hugepd pointer, we should already find
			 * it cleared.
			 */
			WARN_ON(!pmd_none_or_clear_bad(pmd));
			continue;
		}
#ifdef CONFIG_PPC_FSL_BOOK3E
		/*
		 * Increment next by the size of the huge mapping since
		 * there may be more than one entry at this level for a
		 * single hugepage, but all of them point to
		 * the same kmem cache that holds the hugepte.
		 */
		next = addr + (1 << hugepd_shift(*(hugepd_t *)pmd));
#endif
		free_hugepd_range(tlb, (hugepd_t *)pmd, PMD_SHIFT,
				  addr, next, floor, ceiling);
	} while (addr = next, addr != end);

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

static void hugetlb_free_pud_range(struct mmu_gather *tlb, pgd_t *pgd,
				   unsigned long addr, unsigned long end,
				   unsigned long floor, unsigned long ceiling)
{
	pud_t *pud;
	unsigned long next;
	unsigned long start;
	unsigned int shift;
	unsigned int psize = get_slice_psize(tlb->mm, addr);
	shift = mmu_psize_to_shift(psize);

	start = addr;
	pud = pud_offset(pgd, addr);
	do {
		next = pud_addr_end(addr, end);
#ifdef CONFIG_PPC_64K_PAGES
		if (pud_none_or_clear_bad(pud))
			continue;
		hugetlb_free_pmd_range(tlb, pud, addr, next, floor, ceiling,
				       psize);
#else
		if (shift == PAGE_SHIFT_64K) {
			if (pud_none_or_clear_bad(pud))
				continue;
			hugetlb_free_pmd_range(tlb, pud, addr, next, floor,
					       ceiling, psize);
		} else {
			if (pud_none(*pud))
				continue;
			free_hugepte_range(tlb, (hugepd_t *)pud, psize);
		}
#endif
	} while (pud++, addr = next, addr != end);

	start = addr;
	do {
		pud = pud_offset(pgd, addr);
		next = pud_addr_end(addr, end);
		if (!is_hugepd(__hugepd(pud_val(*pud)))) {
			if (pud_none_or_clear_bad(pud))
				continue;
			hugetlb_free_pmd_range(tlb, pud, addr, next, floor,
					       ceiling);
		} else {
#ifdef CONFIG_PPC_FSL_BOOK3E
			/*
			 * Increment next by the size of the huge mapping since
			 * there may be more than one entry at this level for a
			 * single hugepage, but all of them point to
			 * the same kmem cache that holds the hugepte.
			 */
			next = addr + (1 << hugepd_shift(*(hugepd_t *)pud));
#endif
			free_hugepd_range(tlb, (hugepd_t *)pud, PUD_SHIFT,
					  addr, next, floor, ceiling);
		}
	} while (addr = next, addr != end);

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
void hugetlb_free_pgd_range(struct mmu_gather *tlb,
			    unsigned long addr, unsigned long end,
			    unsigned long floor, unsigned long ceiling)
{
	pgd_t *pgd;
	unsigned long next;
	unsigned long start;

	/*
	 * Comments below take from the normal free_pgd_range().  They
	 * apply here too.  The tests against HUGEPD_MASK below are
	 * essential, because we *don't* test for this at the bottom
	 * level.  Without them we'll attempt to free a hugepte table
	 * when we unmap just part of it, even if there are other
	 * active mappings using it.
	 *
	 * The next few lines have given us lots of grief...
	 *
	 * Why are we testing HUGEPD* at this top level?  Because
	 * often there will be no work to do at all, and we'd prefer
	 * not to go all the way down to the bottom just to discover
	 * that.
	 *
	 * Why all these "- 1"s?  Because 0 represents both the bottom
	 * of the address space and the top of it (using -1 for the
	 * top wouldn't help much: the masks would do the wrong thing).
	 * The rule is that addr 0 and floor 0 refer to the bottom of
	 * the address space, but end 0 and ceiling 0 refer to the top
	 * Comparisons need to use "end - 1" and "ceiling - 1" (though
	 * that end 0 case should be mythical).
	 *
	 * Wherever addr is brought up or ceiling brought down, we
	 * must be careful to reject "the opposite 0" before it
	 * confuses the subsequent tests.  But what about where end is
	 * brought down by HUGEPD_SIZE below? no, end can't go down to
	 * 0 there.
	 *
	 * Whereas we round start (addr) and ceiling down, by different
	 * masks at different levels, in order to test whether a table
	 * now has no other vmas using it, so can be freed, we don't
	 * bother to round floor or end up - the tests don't need that.
	 */
	unsigned int psize = get_slice_psize(tlb->mm, addr);

	addr &= HUGEPD_MASK(psize);
	if (addr < floor) {
		addr += HUGEPD_SIZE(psize);
		if (!addr)
			return;
	}
	if (ceiling) {
		ceiling &= HUGEPD_MASK(psize);
		if (!ceiling)
			return;
	}
	if (end - 1 > ceiling - 1)
		end -= HUGEPD_SIZE(psize);
	if (addr > end - 1)
		return;

	start = addr;
	pgd = pgd_offset(tlb->mm, addr);
	do {
		psize = get_slice_psize(tlb->mm, addr);
		BUG_ON(!mmu_huge_psizes[psize]);
		next = pgd_addr_end(addr, end);
		if (pgd_none_or_clear_bad(pgd))
			continue;
		hugetlb_free_pud_range(tlb, pgd, addr, next, floor, ceiling);
	} while (pgd++, addr = next, addr != end);
}

void set_huge_pte_at(struct mm_struct *mm, unsigned long addr,
		     pte_t *ptep, pte_t pte)
{
	if (pte_present(*ptep)) {
		/* We open-code pte_clear because we need to pass the right
		 * argument to hpte_need_flush (huge / !huge). Might not be
		 * necessary anymore if we make hpte_need_flush() get the
		 * page size from the slices
		 */
		unsigned int psize = get_slice_psize(mm, addr);
		unsigned int shift = mmu_psize_to_shift(psize);
		unsigned long sz = ((1UL) << shift);
		struct hstate *hstate = size_to_hstate(sz);
		pte_update(mm, addr & hstate->mask, ptep, ~0UL, 1);
	}
	*ptep = __pte(pte_val(pte) & ~_PAGE_HPTEFLAGS);
}

pte_t huge_ptep_get_and_clear(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep)
{
	unsigned long old = pte_update(mm, addr, ptep, ~0UL, 1);
	return __pte(old);
}

struct page *
follow_huge_addr(struct mm_struct *mm, unsigned long address, int write)
{
	pte_t *ptep;
	struct page *page;
	unsigned int mmu_psize = get_slice_psize(mm, address);

	/* Verify it is a huge page else bail. */
	if (!mmu_huge_psizes[mmu_psize])
		return ERR_PTR(-EINVAL);

	ptep = huge_pte_offset(mm, address);
	page = pte_page(*ptep);
	if (page) {
		unsigned int shift = mmu_psize_to_shift(mmu_psize);
		unsigned long sz = ((1UL) << shift);
		page += (address % sz) / PAGE_SIZE;
	}

	return page;
}

int pmd_huge(pmd_t pmd)
{
	return 0;
}

int pud_huge(pud_t pud)
{
	return 0;
}


	/*
	 * Because there are a number of different possible pagetable
	 * layouts for hugepage ranges, we limit knowledge of how
	 * things should be laid out to the allocation path
	 * (huge_pte_alloc(), above).  Everything else works out the
	 * structure as it goes from information in the hugepd
	 * pointers.  That means that we can't here use the
	 * optimization used in the normal page free_pgd_range(), of
	 * checking whether we're actually covering a large enough
	 * range to have to do anything at the top level of the walk
	 * instead of at the bottom.
	 *
	 * To make sense of this, you should probably go read the big
	 * block comment at the top of the normal free_pgd_range(),
	 * too.
	 */

	do {
		next = pgd_addr_end(addr, end);
		pgd = pgd_offset(tlb->mm, addr);
		if (!is_hugepd(__hugepd(pgd_val(*pgd)))) {
			if (pgd_none_or_clear_bad(pgd))
				continue;
			hugetlb_free_pud_range(tlb, pgd, addr, next, floor, ceiling);
		} else {
#ifdef CONFIG_PPC_FSL_BOOK3E
			/*
			 * Increment next by the size of the huge mapping since
			 * there may be more than one entry at the pgd level
			 * for a single hugepage, but all of them point to the
			 * same kmem cache that holds the hugepte.
			 */
			next = addr + (1 << hugepd_shift(*(hugepd_t *)pgd));
#endif
			free_hugepd_range(tlb, (hugepd_t *)pgd, PGDIR_SHIFT,
					  addr, next, floor, ceiling);
		}
	} while (addr = next, addr != end);
}

/*
 * We are holding mmap_sem, so a parallel huge page collapse cannot run.
 * To prevent hugepage split, disable irq.
 */
struct page *
follow_huge_addr(struct mm_struct *mm, unsigned long address, int write)
{
	bool is_thp;
	pte_t *ptep, pte;
	unsigned shift;
	unsigned long mask, flags;
	struct page *page = ERR_PTR(-EINVAL);

	local_irq_save(flags);
	ptep = find_linux_pte_or_hugepte(mm->pgd, address, &is_thp, &shift);
	if (!ptep)
		goto no_page;
	pte = READ_ONCE(*ptep);
	/*
	 * Verify it is a huge page else bail.
	 * Transparent hugepages are handled by generic code. We can skip them
	 * here.
	 */
	if (!shift || is_thp)
		goto no_page;

	if (!pte_present(pte)) {
		page = NULL;
		goto no_page;
	}
	mask = (1UL << shift) - 1;
	page = pte_page(pte);
	if (page)
		page += (address & mask) / PAGE_SIZE;

no_page:
	local_irq_restore(flags);
	return page;
}

struct page *
follow_huge_pmd(struct mm_struct *mm, unsigned long address,
		pmd_t *pmd, int write)
{
	BUG();
	return NULL;
}


struct page *
follow_huge_pud(struct mm_struct *mm, unsigned long address,
		pud_t *pud, int write)
{
	BUG();
	return NULL;
}

static unsigned long hugepte_addr_end(unsigned long addr, unsigned long end,
				      unsigned long sz)
{
	unsigned long __boundary = (addr + sz) & ~(sz-1);
	return (__boundary - 1 < end - 1) ? __boundary : end;
}

int gup_huge_pd(hugepd_t hugepd, unsigned long addr, unsigned pdshift,
		unsigned long end, int write, struct page **pages, int *nr)
{
	pte_t *ptep;
	unsigned long sz = 1UL << hugepd_shift(hugepd);
	unsigned long next;

	ptep = hugepte_offset(hugepd, addr, pdshift);
	do {
		next = hugepte_addr_end(addr, end, sz);
		if (!gup_hugepte(ptep, sz, addr, end, write, pages, nr))
			return 0;
	} while (ptep++, addr = next, addr != end);

	return 1;
}

#ifdef CONFIG_PPC_MM_SLICES
unsigned long hugetlb_get_unmapped_area(struct file *file, unsigned long addr,
					unsigned long len, unsigned long pgoff,
					unsigned long flags)
{
	struct hstate *hstate = hstate_file(file);
	int mmu_psize = shift_to_mmu_psize(huge_page_shift(hstate));
	return slice_get_unmapped_area(addr, len, flags, mmu_psize, 1, 0);
}

/*
 * Called by asm hashtable.S for doing lazy icache flush
 */
static unsigned int hash_huge_page_do_lazy_icache(unsigned long rflags,
					pte_t pte, int trap, unsigned long sz)
{
	struct page *page;
	int i;

	if (!pfn_valid(pte_pfn(pte)))
		return rflags;

	page = pte_page(pte);

	/* page is dirty */
	if (!test_bit(PG_arch_1, &page->flags) && !PageReserved(page)) {
		if (trap == 0x400) {
			for (i = 0; i < (sz / PAGE_SIZE); i++)
				__flush_dcache_icache(page_address(page+i));
			set_bit(PG_arch_1, &page->flags);
		} else {
			rflags |= HPTE_R_N;
		}
	}
	return rflags;
}

int hash_huge_page(struct mm_struct *mm, unsigned long access,
		   unsigned long ea, unsigned long vsid, int local,
		   unsigned long trap)
{
	pte_t *ptep;
	unsigned long old_pte, new_pte;
	unsigned long va, rflags, pa, sz;
	long slot;
	int err = 1;
	int ssize = user_segment_size(ea);
	unsigned int mmu_psize;
	int shift;
	mmu_psize = get_slice_psize(mm, ea);

	if (!mmu_huge_psizes[mmu_psize])
		goto out;
	ptep = huge_pte_offset(mm, ea);

	/* Search the Linux page table for a match with va */
	va = hpt_va(ea, vsid, ssize);

	/*
	 * If no pte found or not present, send the problem up to
	 * do_page_fault
	 */
	if (unlikely(!ptep || pte_none(*ptep)))
		goto out;

	/* 
	 * Check the user's access rights to the page.  If access should be
	 * prevented then send the problem up to do_page_fault.
	 */
	if (unlikely(access & ~pte_val(*ptep)))
		goto out;
	/*
	 * At this point, we have a pte (old_pte) which can be used to build
	 * or update an HPTE. There are 2 cases:
	 *
	 * 1. There is a valid (present) pte with no associated HPTE (this is 
	 *	the most common case)
	 * 2. There is a valid (present) pte with an associated HPTE. The
	 *	current values of the pp bits in the HPTE prevent access
	 *	because we are doing software DIRTY bit management and the
	 *	page is currently not DIRTY. 
	 */


	do {
		old_pte = pte_val(*ptep);
		if (old_pte & _PAGE_BUSY)
			goto out;
		new_pte = old_pte | _PAGE_BUSY | _PAGE_ACCESSED;
	} while(old_pte != __cmpxchg_u64((unsigned long *)ptep,
					 old_pte, new_pte));

	rflags = 0x2 | (!(new_pte & _PAGE_RW));
 	/* _PAGE_EXEC -> HW_NO_EXEC since it's inverted */
	rflags |= ((new_pte & _PAGE_EXEC) ? 0 : HPTE_R_N);
	shift = mmu_psize_to_shift(mmu_psize);
	sz = ((1UL) << shift);
	if (!cpu_has_feature(CPU_FTR_COHERENT_ICACHE))
		/* No CPU has hugepages but lacks no execute, so we
		 * don't need to worry about that case */
		rflags = hash_huge_page_do_lazy_icache(rflags, __pte(old_pte),
						       trap, sz);

	/* Check if pte already has an hpte (case 2) */
	if (unlikely(old_pte & _PAGE_HASHPTE)) {
		/* There MIGHT be an HPTE for this pte */
		unsigned long hash, slot;

		hash = hpt_hash(va, shift, ssize);
		if (old_pte & _PAGE_F_SECOND)
			hash = ~hash;
		slot = (hash & htab_hash_mask) * HPTES_PER_GROUP;
		slot += (old_pte & _PAGE_F_GIX) >> 12;

		if (ppc_md.hpte_updatepp(slot, rflags, va, mmu_psize,
					 ssize, local) == -1)
			old_pte &= ~_PAGE_HPTEFLAGS;
	}

	if (likely(!(old_pte & _PAGE_HASHPTE))) {
		unsigned long hash = hpt_hash(va, shift, ssize);
		unsigned long hpte_group;

		pa = pte_pfn(__pte(old_pte)) << PAGE_SHIFT;

repeat:
		hpte_group = ((hash & htab_hash_mask) *
			      HPTES_PER_GROUP) & ~0x7UL;

		/* clear HPTE slot informations in new PTE */
#ifdef CONFIG_PPC_64K_PAGES
		new_pte = (new_pte & ~_PAGE_HPTEFLAGS) | _PAGE_HPTE_SUB0;
#else
		new_pte = (new_pte & ~_PAGE_HPTEFLAGS) | _PAGE_HASHPTE;
#endif
		/* Add in WIMG bits */
		rflags |= (new_pte & (_PAGE_WRITETHRU | _PAGE_NO_CACHE |
				      _PAGE_COHERENT | _PAGE_GUARDED));

		/* Insert into the hash table, primary slot */
		slot = ppc_md.hpte_insert(hpte_group, va, pa, rflags, 0,
					  mmu_psize, ssize);

		/* Primary is full, try the secondary */
		if (unlikely(slot == -1)) {
			hpte_group = ((~hash & htab_hash_mask) *
				      HPTES_PER_GROUP) & ~0x7UL; 
			slot = ppc_md.hpte_insert(hpte_group, va, pa, rflags,
						  HPTE_V_SECONDARY,
						  mmu_psize, ssize);
			if (slot == -1) {
				if (mftb() & 0x1)
					hpte_group = ((hash & htab_hash_mask) *
						      HPTES_PER_GROUP)&~0x7UL;

				ppc_md.hpte_remove(hpte_group);
				goto repeat;
                        }
		}

		if (unlikely(slot == -2))
			panic("hash_huge_page: pte_insert failed\n");

		new_pte |= (slot << 12) & (_PAGE_F_SECOND | _PAGE_F_GIX);
	}

	/*
	 * No need to use ldarx/stdcx here
	 */
	*ptep = __pte(new_pte & ~_PAGE_BUSY);

	err = 0;

 out:
	return err;
}

void set_huge_psize(int psize)
{
	/* Check that it is a page size supported by the hardware and
	 * that it fits within pagetable limits. */
	if (mmu_psize_defs[psize].shift &&
		mmu_psize_defs[psize].shift < SID_SHIFT_1T &&
		(mmu_psize_defs[psize].shift > MIN_HUGEPTE_SHIFT ||
		 mmu_psize_defs[psize].shift == PAGE_SHIFT_64K ||
		 mmu_psize_defs[psize].shift == PAGE_SHIFT_16G)) {
		/* Return if huge page size has already been setup or is the
		 * same as the base page size. */
		if (mmu_huge_psizes[psize] ||
		   mmu_psize_defs[psize].shift == PAGE_SHIFT)
			return;
		hugetlb_add_hstate(mmu_psize_defs[psize].shift - PAGE_SHIFT);

		switch (mmu_psize_defs[psize].shift) {
		case PAGE_SHIFT_64K:
		    /* We only allow 64k hpages with 4k base page,
		     * which was checked above, and always put them
		     * at the PMD */
		    hugepte_shift[psize] = PMD_SHIFT;
		    break;
		case PAGE_SHIFT_16M:
		    /* 16M pages can be at two different levels
		     * of pagestables based on base page size */
		    if (PAGE_SHIFT == PAGE_SHIFT_64K)
			    hugepte_shift[psize] = PMD_SHIFT;
		    else /* 4k base page */
			    hugepte_shift[psize] = PUD_SHIFT;
		    break;
		case PAGE_SHIFT_16G:
		    /* 16G pages are always at PGD level */
		    hugepte_shift[psize] = PGDIR_SHIFT;
		    break;
		}
		hugepte_shift[psize] -= mmu_psize_defs[psize].shift;
	} else
		hugepte_shift[psize] = 0;

	return slice_get_unmapped_area(addr, len, flags, mmu_psize, 1);
}
#endif

unsigned long vma_mmu_pagesize(struct vm_area_struct *vma)
{
#ifdef CONFIG_PPC_MM_SLICES
	unsigned int psize = get_slice_psize(vma->vm_mm, vma->vm_start);

	return 1UL << mmu_psize_to_shift(psize);
#else
	if (!is_vm_hugetlb_page(vma))
		return PAGE_SIZE;

	return huge_page_size(hstate_vma(vma));
#endif
}

static inline bool is_power_of_4(unsigned long x)
{
	if (is_power_of_2(x))
		return (__ilog2(x) % 2) ? false : true;
	return false;
}

static int __init add_huge_page_size(unsigned long long size)
{
	int shift = __ffs(size);
	int mmu_psize;

	/* Check that it is a page size supported by the hardware and
	 * that it fits within pagetable and slice limits. */
#ifdef CONFIG_PPC_FSL_BOOK3E
	if ((size < PAGE_SIZE) || !is_power_of_4(size))
		return -EINVAL;
#else
	if (!is_power_of_2(size)
	    || (shift > SLICE_HIGH_SHIFT) || (shift <= PAGE_SHIFT))
		return -EINVAL;
#endif

	if ((mmu_psize = shift_to_mmu_psize(shift)) < 0)
		return -EINVAL;

	BUG_ON(mmu_psize_defs[mmu_psize].shift != shift);

	/* Return if huge page size has already been setup */
	if (size_to_hstate(size))
		return 0;

	hugetlb_add_hstate(shift - PAGE_SHIFT);

	return 0;
}

static int __init hugepage_setup_sz(char *str)
{
	unsigned long long size;
	int mmu_psize;
	int shift;

	size = memparse(str, &str);

	shift = __ffs(size);
	mmu_psize = shift_to_mmu_psize(shift);
	if (mmu_psize >= 0 && mmu_psize_defs[mmu_psize].shift)
		set_huge_psize(mmu_psize);
	else

	size = memparse(str, &str);

	if (add_huge_page_size(size) != 0)
		printk(KERN_WARNING "Invalid huge page size specified(%llu)\n", size);

	return 1;
}
__setup("hugepagesz=", hugepage_setup_sz);

static int __init hugetlbpage_init(void)
{
	unsigned int psize;

	if (!cpu_has_feature(CPU_FTR_16M_PAGE))
		return -ENODEV;

	/* Add supported huge page sizes.  Need to change HUGE_MAX_HSTATE
	 * and adjust PTE_NONCACHE_NUM if the number of supported huge page
	 * sizes changes.
	 */
	set_huge_psize(MMU_PAGE_16M);
	set_huge_psize(MMU_PAGE_16G);

	/* Temporarily disable support for 64K huge pages when 64K SPU local
	 * store support is enabled as the current implementation conflicts.
	 */
#ifndef CONFIG_SPU_FS_64K_LS
	set_huge_psize(MMU_PAGE_64K);
#endif

	for (psize = 0; psize < MMU_PAGE_COUNT; ++psize) {
		if (mmu_huge_psizes[psize]) {
			huge_pgtable_cache(psize) = kmem_cache_create(
						HUGEPTE_CACHE_NAME(psize),
						HUGEPTE_TABLE_SIZE(psize),
						HUGEPTE_TABLE_SIZE(psize),
						0,
						NULL);
			if (!huge_pgtable_cache(psize))
				panic("hugetlbpage_init(): could not create %s"\
				      "\n", HUGEPTE_CACHE_NAME(psize));
		}
	}

	return 0;
}

module_init(hugetlbpage_init);
#ifdef CONFIG_PPC_FSL_BOOK3E
struct kmem_cache *hugepte_cache;
static int __init hugetlbpage_init(void)
{
	int psize;

	for (psize = 0; psize < MMU_PAGE_COUNT; ++psize) {
		unsigned shift;

		if (!mmu_psize_defs[psize].shift)
			continue;

		shift = mmu_psize_to_shift(psize);

		/* Don't treat normal page sizes as huge... */
		if (shift != PAGE_SHIFT)
			if (add_huge_page_size(1ULL << shift) < 0)
				continue;
	}

	/*
	 * Create a kmem cache for hugeptes.  The bottom bits in the pte have
	 * size information encoded in them, so align them to allow this
	 */
	hugepte_cache =  kmem_cache_create("hugepte-cache", sizeof(pte_t),
					   HUGEPD_SHIFT_MASK + 1, 0, NULL);
	if (hugepte_cache == NULL)
		panic("%s: Unable to create kmem cache for hugeptes\n",
		      __func__);

	/* Default hpage size = 4M */
	if (mmu_psize_defs[MMU_PAGE_4M].shift)
		HPAGE_SHIFT = mmu_psize_defs[MMU_PAGE_4M].shift;
	else
		panic("%s: Unable to set default huge page size\n", __func__);


	return 0;
}
#else
static int __init hugetlbpage_init(void)
{
	int psize;

	if (!mmu_has_feature(MMU_FTR_16M_PAGE))
		return -ENODEV;

	for (psize = 0; psize < MMU_PAGE_COUNT; ++psize) {
		unsigned shift;
		unsigned pdshift;

		if (!mmu_psize_defs[psize].shift)
			continue;

		shift = mmu_psize_to_shift(psize);

		if (add_huge_page_size(1ULL << shift) < 0)
			continue;

		if (shift < PMD_SHIFT)
			pdshift = PMD_SHIFT;
		else if (shift < PUD_SHIFT)
			pdshift = PUD_SHIFT;
		else
			pdshift = PGDIR_SHIFT;
		/*
		 * if we have pdshift and shift value same, we don't
		 * use pgt cache for hugepd.
		 */
		if (pdshift != shift) {
			pgtable_cache_add(pdshift - shift, NULL);
			if (!PGT_CACHE(pdshift - shift))
				panic("hugetlbpage_init(): could not create "
				      "pgtable cache for %d bit pagesize\n", shift);
		}
	}

	/* Set default large page size. Currently, we pick 16M or 1M
	 * depending on what is available
	 */
	if (mmu_psize_defs[MMU_PAGE_16M].shift)
		HPAGE_SHIFT = mmu_psize_defs[MMU_PAGE_16M].shift;
	else if (mmu_psize_defs[MMU_PAGE_1M].shift)
		HPAGE_SHIFT = mmu_psize_defs[MMU_PAGE_1M].shift;

	return 0;
}
#endif
arch_initcall(hugetlbpage_init);

void flush_dcache_icache_hugepage(struct page *page)
{
	int i;
	void *start;

	BUG_ON(!PageCompound(page));

	for (i = 0; i < (1UL << compound_order(page)); i++) {
		if (!PageHighMem(page)) {
			__flush_dcache_icache(page_address(page+i));
		} else {
			start = kmap_atomic(page+i);
			__flush_dcache_icache(start);
			kunmap_atomic(start);
		}
	}
}

#endif /* CONFIG_HUGETLB_PAGE */

/*
 * We have 4 cases for pgds and pmds:
 * (1) invalid (all zeroes)
 * (2) pointer to next table, as normal; bottom 6 bits == 0
 * (3) leaf pte for huge page, bottom two bits != 00
 * (4) hugepd pointer, bottom two bits == 00, next 4 bits indicate size of table
 *
 * So long as we atomically load page table pointers we are safe against teardown,
 * we can follow the address down to the the page and take a ref on it.
 * This function need to be called with interrupts disabled. We use this variant
 * when we have MSR[EE] = 0 but the paca->soft_enabled = 1
 */

pte_t *__find_linux_pte_or_hugepte(pgd_t *pgdir, unsigned long ea,
				   bool *is_thp, unsigned *shift)
{
	pgd_t pgd, *pgdp;
	pud_t pud, *pudp;
	pmd_t pmd, *pmdp;
	pte_t *ret_pte;
	hugepd_t *hpdp = NULL;
	unsigned pdshift = PGDIR_SHIFT;

	if (shift)
		*shift = 0;

	if (is_thp)
		*is_thp = false;

	pgdp = pgdir + pgd_index(ea);
	pgd  = READ_ONCE(*pgdp);
	/*
	 * Always operate on the local stack value. This make sure the
	 * value don't get updated by a parallel THP split/collapse,
	 * page fault or a page unmap. The return pte_t * is still not
	 * stable. So should be checked there for above conditions.
	 */
	if (pgd_none(pgd))
		return NULL;
	else if (pgd_huge(pgd)) {
		ret_pte = (pte_t *) pgdp;
		goto out;
	} else if (is_hugepd(__hugepd(pgd_val(pgd))))
		hpdp = (hugepd_t *)&pgd;
	else {
		/*
		 * Even if we end up with an unmap, the pgtable will not
		 * be freed, because we do an rcu free and here we are
		 * irq disabled
		 */
		pdshift = PUD_SHIFT;
		pudp = pud_offset(&pgd, ea);
		pud  = READ_ONCE(*pudp);

		if (pud_none(pud))
			return NULL;
		else if (pud_huge(pud)) {
			ret_pte = (pte_t *) pudp;
			goto out;
		} else if (is_hugepd(__hugepd(pud_val(pud))))
			hpdp = (hugepd_t *)&pud;
		else {
			pdshift = PMD_SHIFT;
			pmdp = pmd_offset(&pud, ea);
			pmd  = READ_ONCE(*pmdp);
			/*
			 * A hugepage collapse is captured by pmd_none, because
			 * it mark the pmd none and do a hpte invalidate.
			 *
			 * We don't worry about pmd_trans_splitting here, The
			 * caller if it needs to handle the splitting case
			 * should check for that.
			 */
			if (pmd_none(pmd))
				return NULL;

			if (pmd_trans_huge(pmd)) {
				if (is_thp)
					*is_thp = true;
				ret_pte = (pte_t *) pmdp;
				goto out;
			}

			if (pmd_huge(pmd)) {
				ret_pte = (pte_t *) pmdp;
				goto out;
			} else if (is_hugepd(__hugepd(pmd_val(pmd))))
				hpdp = (hugepd_t *)&pmd;
			else
				return pte_offset_kernel(&pmd, ea);
		}
	}
	if (!hpdp)
		return NULL;

	ret_pte = hugepte_offset(*hpdp, ea, pdshift);
	pdshift = hugepd_shift(*hpdp);
out:
	if (shift)
		*shift = pdshift;
	return ret_pte;
}
EXPORT_SYMBOL_GPL(__find_linux_pte_or_hugepte);

int gup_hugepte(pte_t *ptep, unsigned long sz, unsigned long addr,
		unsigned long end, int write, struct page **pages, int *nr)
{
	unsigned long mask;
	unsigned long pte_end;
	struct page *head, *page, *tail;
	pte_t pte;
	int refs;

	pte_end = (addr + sz) & ~(sz-1);
	if (pte_end < end)
		end = pte_end;

	pte = READ_ONCE(*ptep);
	mask = _PAGE_PRESENT | _PAGE_USER;
	if (write)
		mask |= _PAGE_RW;

	if ((pte_val(pte) & mask) != mask)
		return 0;

	/* hugepages are never "special" */
	VM_BUG_ON(!pfn_valid(pte_pfn(pte)));

	refs = 0;
	head = pte_page(pte);

	page = head + ((addr & (sz-1)) >> PAGE_SHIFT);
	tail = page;
	do {
		VM_BUG_ON(compound_head(page) != head);
		pages[*nr] = page;
		(*nr)++;
		page++;
		refs++;
	} while (addr += PAGE_SIZE, addr != end);

	if (!page_cache_add_speculative(head, refs)) {
		*nr -= refs;
		return 0;
	}

	if (unlikely(pte_val(pte) != pte_val(*ptep))) {
		/* Could be optimized better */
		*nr -= refs;
		while (refs--)
			put_page(head);
		return 0;
	}

	/*
	 * Any tail page need their mapcount reference taken before we
	 * return.
	 */
	while (refs--) {
		if (PageTail(tail))
			get_huge_page_tail(tail);
		tail++;
	}

	return 1;
}
