#ifndef _SPARC64_TLBFLUSH_H
#define _SPARC64_TLBFLUSH_H

#include <linux/mm.h>
#include <asm/mmu_context.h>

/* TSB flush operations. */
struct mmu_gather;
extern void flush_tsb_kernel_range(unsigned long start, unsigned long end);
extern void flush_tsb_user(struct mmu_gather *mp);

/* TLB flush operations. */

extern void flush_tlb_pending(void);

#define flush_tlb_range(vma,start,end)	\
	do { (void)(start); flush_tlb_pending(); } while (0)
#define flush_tlb_page(vma,addr)	flush_tlb_pending()
#define flush_tlb_mm(mm)		flush_tlb_pending()

/* Local cpu only.  */
extern void __flush_tlb_all(void);

extern void __flush_tlb_kernel_range(unsigned long start, unsigned long end);

#ifndef CONFIG_SMP

#define flush_tlb_kernel_range(start,end) \
do {	flush_tsb_kernel_range(start,end); \
	__flush_tlb_kernel_range(start,end); \
} while (0)

#else /* CONFIG_SMP */

extern void smp_flush_tlb_kernel_range(unsigned long start, unsigned long end);

#define flush_tlb_kernel_range(start, end) \
do {	flush_tsb_kernel_range(start,end); \
	smp_flush_tlb_kernel_range(start, end); \
} while (0)
#include <asm/mmu_context.h>

/* TSB flush operations. */

#define TLB_BATCH_NR	192

struct tlb_batch {
	bool huge;
	struct mm_struct *mm;
	unsigned long tlb_nr;
	unsigned long active;
	unsigned long vaddrs[TLB_BATCH_NR];
};

void flush_tsb_kernel_range(unsigned long start, unsigned long end);
void flush_tsb_user(struct tlb_batch *tb);
void flush_tsb_user_page(struct mm_struct *mm, unsigned long vaddr, bool huge);

/* TLB flush operations. */

static inline void flush_tlb_mm(struct mm_struct *mm)
{
}

static inline void flush_tlb_page(struct vm_area_struct *vma,
				  unsigned long vmaddr)
{
}

static inline void flush_tlb_range(struct vm_area_struct *vma,
				   unsigned long start, unsigned long end)
{
}

void flush_tlb_kernel_range(unsigned long start, unsigned long end);

#define __HAVE_ARCH_ENTER_LAZY_MMU_MODE

void flush_tlb_pending(void);
void arch_enter_lazy_mmu_mode(void);
void arch_leave_lazy_mmu_mode(void);
#define arch_flush_lazy_mmu_mode()      do {} while (0)

/* Local cpu only.  */
void __flush_tlb_all(void);
void __flush_tlb_page(unsigned long context, unsigned long vaddr);
void __flush_tlb_kernel_range(unsigned long start, unsigned long end);

#ifndef CONFIG_SMP

static inline void global_flush_tlb_page(struct mm_struct *mm, unsigned long vaddr)
{
	__flush_tlb_page(CTX_HWBITS(mm->context), vaddr);
}

#else /* CONFIG_SMP */

void smp_flush_tlb_kernel_range(unsigned long start, unsigned long end);
void smp_flush_tlb_page(struct mm_struct *mm, unsigned long vaddr);

#define global_flush_tlb_page(mm, vaddr) \
	smp_flush_tlb_page(mm, vaddr)

#endif /* ! CONFIG_SMP */

#endif /* _SPARC64_TLBFLUSH_H */
