#ifndef __LINUX_PERCPU_H
#define __LINUX_PERCPU_H

#include <linux/preempt.h>
#include <linux/slab.h> /* For kmalloc() */
#include <linux/smp.h>
#include <linux/cpumask.h>

#include <asm/percpu.h>

#ifdef CONFIG_SMP
#define DEFINE_PER_CPU(type, name)					\
	__attribute__((__section__(".data.percpu")))			\
	PER_CPU_ATTRIBUTES __typeof__(type) per_cpu__##name

#ifdef MODULE
#define SHARED_ALIGNED_SECTION ".data.percpu"
#else
#define SHARED_ALIGNED_SECTION ".data.percpu.shared_aligned"
#endif

#define DEFINE_PER_CPU_SHARED_ALIGNED(type, name)			\
	__attribute__((__section__(SHARED_ALIGNED_SECTION)))		\
	PER_CPU_ATTRIBUTES __typeof__(type) per_cpu__##name		\
	____cacheline_aligned_in_smp
#else
#define DEFINE_PER_CPU(type, name)					\
	PER_CPU_ATTRIBUTES __typeof__(type) per_cpu__##name

#define DEFINE_PER_CPU_SHARED_ALIGNED(type, name)		      \
	DEFINE_PER_CPU(type, name)
#endif

#define EXPORT_PER_CPU_SYMBOL(var) EXPORT_SYMBOL(per_cpu__##var)
#define EXPORT_PER_CPU_SYMBOL_GPL(var) EXPORT_SYMBOL_GPL(per_cpu__##var)

/* Enough to cover all DEFINE_PER_CPUs in kernel, including modules. */
#ifndef PERCPU_ENOUGH_ROOM
#ifdef CONFIG_MODULES
#define PERCPU_MODULE_RESERVE	8192
#else
#define PERCPU_MODULE_RESERVE	0
#endif

#define PERCPU_ENOUGH_ROOM						\
	(__per_cpu_end - __per_cpu_start + PERCPU_MODULE_RESERVE)
#endif	/* PERCPU_ENOUGH_ROOM */

/*
 * Must be an lvalue. Since @var must be a simple identifier,
 * we force a syntax error here if it isn't.
 */
#define get_cpu_var(var) (*({				\
	extern int simple_identifier_##var(void);	\
	preempt_disable();				\
	&__get_cpu_var(var); }))
#define put_cpu_var(var) preempt_enable()

#ifdef CONFIG_SMP

struct percpu_data {
	void *ptrs[1];
};

#define __percpu_disguise(pdata) (struct percpu_data *)~(unsigned long)(pdata)
/* 
 * Use this to get to a cpu's version of the per-cpu object dynamically
 * allocated. Non-atomic access to the current CPU's version should
 * probably be combined with get_cpu()/put_cpu().
 */ 
#define percpu_ptr(ptr, cpu)                              \
({                                                        \
        struct percpu_data *__p = __percpu_disguise(ptr); \
        (__typeof__(ptr))__p->ptrs[(cpu)];	          \
})

extern void *__percpu_alloc_mask(size_t size, gfp_t gfp, cpumask_t *mask);
extern void percpu_free(void *__pdata);

#else /* CONFIG_SMP */

#define percpu_ptr(ptr, cpu) ({ (void)(cpu); (ptr); })

static __always_inline void *__percpu_alloc_mask(size_t size, gfp_t gfp, cpumask_t *mask)
{
	return kzalloc(size, gfp);
}

static inline void percpu_free(void *__pdata)
{
	kfree(__pdata);
}

#endif /* CONFIG_SMP */

#define percpu_alloc_mask(size, gfp, mask) \
	__percpu_alloc_mask((size), (gfp), &(mask))

#define percpu_alloc(size, gfp) percpu_alloc_mask((size), (gfp), cpu_online_map)

/* (legacy) interface for use without CPU hotplug handling */

#define __alloc_percpu(size)	percpu_alloc_mask((size), GFP_KERNEL, \
						  cpu_possible_map)
#define alloc_percpu(type)	(type *)__alloc_percpu(sizeof(type))
#define free_percpu(ptr)	percpu_free((ptr))
#define per_cpu_ptr(ptr, cpu)	percpu_ptr((ptr), (cpu))
#include <linux/mmdebug.h>
#include <linux/preempt.h>
#include <linux/smp.h>
#include <linux/cpumask.h>
#include <linux/printk.h>
#include <linux/pfn.h>
#include <linux/init.h>

#include <asm/percpu.h>

/* enough to cover all DEFINE_PER_CPUs in modules */
#ifdef CONFIG_MODULES
#define PERCPU_MODULE_RESERVE		(8 << 10)
#else
#define PERCPU_MODULE_RESERVE		0
#endif

#ifndef PERCPU_ENOUGH_ROOM
#define PERCPU_ENOUGH_ROOM						\
	(ALIGN(__per_cpu_end - __per_cpu_start, SMP_CACHE_BYTES) +	\
	 PERCPU_MODULE_RESERVE)
#endif

#ifdef CONFIG_PREEMPT_RT_FULL

#define get_local_var(var) (*({		\
	       migrate_disable();	\
	       this_cpu_ptr(&var);	}))

#define put_local_var(var) do {	\
	(void)&(var);		\
	migrate_enable();	\
} while (0)

# define get_local_ptr(var) ({		\
		migrate_disable();	\
		this_cpu_ptr(var);	})

# define put_local_ptr(var) do {	\
	(void)(var);			\
	migrate_enable();		\
} while (0)

#else

#define get_local_var(var)	get_cpu_var(var)
#define put_local_var(var)	put_cpu_var(var)
#define get_local_ptr(var)	get_cpu_ptr(var)
#define put_local_ptr(var)	put_cpu_ptr(var)

#endif

/* minimum unit size, also is the maximum supported allocation size */
#define PCPU_MIN_UNIT_SIZE		PFN_ALIGN(32 << 10)

/*
 * Percpu allocator can serve percpu allocations before slab is
 * initialized which allows slab to depend on the percpu allocator.
 * The following two parameters decide how much resource to
 * preallocate for this.  Keep PERCPU_DYNAMIC_RESERVE equal to or
 * larger than PERCPU_DYNAMIC_EARLY_SIZE.
 */
#define PERCPU_DYNAMIC_EARLY_SLOTS	128
#define PERCPU_DYNAMIC_EARLY_SIZE	(12 << 10)

/*
 * PERCPU_DYNAMIC_RESERVE indicates the amount of free area to piggy
 * back on the first chunk for dynamic percpu allocation if arch is
 * manually allocating and mapping it for faster access (as a part of
 * large page mapping for example).
 *
 * The following values give between one and two pages of free space
 * after typical minimal boot (2-way SMP, single disk and NIC) with
 * both defconfig and a distro config on x86_64 and 32.  More
 * intelligent way to determine this would be nice.
 */
#if BITS_PER_LONG > 32
#define PERCPU_DYNAMIC_RESERVE		(28 << 10)
#else
#define PERCPU_DYNAMIC_RESERVE		(20 << 10)
#endif

extern void *pcpu_base_addr;
extern const unsigned long *pcpu_unit_offsets;

struct pcpu_group_info {
	int			nr_units;	/* aligned # of units */
	unsigned long		base_offset;	/* base address offset */
	unsigned int		*cpu_map;	/* unit->cpu map, empty
						 * entries contain NR_CPUS */
};

struct pcpu_alloc_info {
	size_t			static_size;
	size_t			reserved_size;
	size_t			dyn_size;
	size_t			unit_size;
	size_t			atom_size;
	size_t			alloc_size;
	size_t			__ai_size;	/* internal, don't use */
	int			nr_groups;	/* 0 if grouping unnecessary */
	struct pcpu_group_info	groups[];
};

enum pcpu_fc {
	PCPU_FC_AUTO,
	PCPU_FC_EMBED,
	PCPU_FC_PAGE,

	PCPU_FC_NR,
};
extern const char * const pcpu_fc_names[PCPU_FC_NR];

extern enum pcpu_fc pcpu_chosen_fc;

typedef void * (*pcpu_fc_alloc_fn_t)(unsigned int cpu, size_t size,
				     size_t align);
typedef void (*pcpu_fc_free_fn_t)(void *ptr, size_t size);
typedef void (*pcpu_fc_populate_pte_fn_t)(unsigned long addr);
typedef int (pcpu_fc_cpu_distance_fn_t)(unsigned int from, unsigned int to);

extern struct pcpu_alloc_info * __init pcpu_alloc_alloc_info(int nr_groups,
							     int nr_units);
extern void __init pcpu_free_alloc_info(struct pcpu_alloc_info *ai);

extern int __init pcpu_setup_first_chunk(const struct pcpu_alloc_info *ai,
					 void *base_addr);

#ifdef CONFIG_NEED_PER_CPU_EMBED_FIRST_CHUNK
extern int __init pcpu_embed_first_chunk(size_t reserved_size, size_t dyn_size,
				size_t atom_size,
				pcpu_fc_cpu_distance_fn_t cpu_distance_fn,
				pcpu_fc_alloc_fn_t alloc_fn,
				pcpu_fc_free_fn_t free_fn);
#endif

#ifdef CONFIG_NEED_PER_CPU_PAGE_FIRST_CHUNK
extern int __init pcpu_page_first_chunk(size_t reserved_size,
				pcpu_fc_alloc_fn_t alloc_fn,
				pcpu_fc_free_fn_t free_fn,
				pcpu_fc_populate_pte_fn_t populate_pte_fn);
#endif

extern void __percpu *__alloc_reserved_percpu(size_t size, size_t align);
extern bool __is_kernel_percpu_address(unsigned long addr, unsigned long *can_addr);
extern bool is_kernel_percpu_address(unsigned long addr);

#if !defined(CONFIG_SMP) || !defined(CONFIG_HAVE_SETUP_PER_CPU_AREA)
extern void __init setup_per_cpu_areas(void);
#endif
extern void __init percpu_init_late(void);

extern void __percpu *__alloc_percpu_gfp(size_t size, size_t align, gfp_t gfp);
extern void __percpu *__alloc_percpu(size_t size, size_t align);
extern void free_percpu(void __percpu *__pdata);
extern phys_addr_t per_cpu_ptr_to_phys(void *addr);

#define alloc_percpu_gfp(type, gfp)					\
	(typeof(type) __percpu *)__alloc_percpu_gfp(sizeof(type),	\
						__alignof__(type), gfp)
#define alloc_percpu(type)						\
	(typeof(type) __percpu *)__alloc_percpu(sizeof(type),		\
						__alignof__(type))

/* To avoid include hell, as printk can not declare this, we declare it here */
DECLARE_PER_CPU(printk_func_t, printk_func);

#endif /* __LINUX_PERCPU_H */
