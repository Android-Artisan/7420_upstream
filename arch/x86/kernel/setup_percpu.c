#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/percpu.h>
#include <linux/kexec.h>
#include <linux/crash_dump.h>
#include <asm/smp.h>
#include <asm/percpu.h>
#include <asm/sections.h>
#include <asm/processor.h>
#include <asm/setup.h>
#include <asm/topology.h>
#include <asm/mpspec.h>
#include <asm/apicdef.h>
#include <asm/highmem.h>

#ifdef CONFIG_X86_LOCAL_APIC
unsigned int num_processors;
unsigned disabled_cpus __cpuinitdata;
/* Processor that is doing the boot up */
unsigned int boot_cpu_physical_apicid = -1U;
unsigned int max_physical_apicid;
EXPORT_SYMBOL(boot_cpu_physical_apicid);

/* Bitmask of physically existing CPUs */
physid_mask_t phys_cpu_present_map;
#endif

/* map cpu index to physical APIC ID */
DEFINE_EARLY_PER_CPU(u16, x86_cpu_to_apicid, BAD_APICID);
DEFINE_EARLY_PER_CPU(u16, x86_bios_cpu_apicid, BAD_APICID);
EXPORT_EARLY_PER_CPU_SYMBOL(x86_cpu_to_apicid);
EXPORT_EARLY_PER_CPU_SYMBOL(x86_bios_cpu_apicid);

#if defined(CONFIG_NUMA) && defined(CONFIG_X86_64)
#define	X86_64_NUMA	1

/* map cpu index to node index */
DEFINE_EARLY_PER_CPU(int, x86_cpu_to_node_map, NUMA_NO_NODE);
EXPORT_EARLY_PER_CPU_SYMBOL(x86_cpu_to_node_map);

/* which logical CPUs are on which nodes */
cpumask_t *node_to_cpumask_map;
EXPORT_SYMBOL(node_to_cpumask_map);

/* setup node_to_cpumask_map */
static void __init setup_node_to_cpumask_map(void);

#else
static inline void setup_node_to_cpumask_map(void) { }
#endif

#if defined(CONFIG_HAVE_SETUP_PER_CPU_AREA) && defined(CONFIG_X86_SMP)
/*
 * Copy data used in early init routines from the initial arrays to the
 * per cpu data areas.  These arrays then become expendable and the
 * *_early_ptr's are zeroed indicating that the static arrays are gone.
 */
static void __init setup_per_cpu_maps(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		per_cpu(x86_cpu_to_apicid, cpu) =
				early_per_cpu_map(x86_cpu_to_apicid, cpu);
		per_cpu(x86_bios_cpu_apicid, cpu) =
				early_per_cpu_map(x86_bios_cpu_apicid, cpu);
#ifdef X86_64_NUMA
		per_cpu(x86_cpu_to_node_map, cpu) =
				early_per_cpu_map(x86_cpu_to_node_map, cpu);
#endif
	}

	/* indicate the early static arrays will soon be gone */
	early_per_cpu_ptr(x86_cpu_to_apicid) = NULL;
	early_per_cpu_ptr(x86_bios_cpu_apicid) = NULL;
#ifdef X86_64_NUMA
	early_per_cpu_ptr(x86_cpu_to_node_map) = NULL;
#endif
}

#ifdef CONFIG_X86_32
/*
 * Great future not-so-futuristic plan: make i386 and x86_64 do it
 * the same way
 */
unsigned long __per_cpu_offset[NR_CPUS] __read_mostly;
EXPORT_SYMBOL(__per_cpu_offset);
static inline void setup_cpu_pda_map(void) { }

#elif !defined(CONFIG_SMP)
static inline void setup_cpu_pda_map(void) { }

#else /* CONFIG_SMP && CONFIG_X86_64 */

/*
 * Allocate cpu_pda pointer table and array via alloc_bootmem.
 */
static void __init setup_cpu_pda_map(void)
{
	char *pda;
	struct x8664_pda **new_cpu_pda;
	unsigned long size;
	int cpu;

	size = roundup(sizeof(struct x8664_pda), cache_line_size());

	/* allocate cpu_pda array and pointer table */
	{
		unsigned long tsize = nr_cpu_ids * sizeof(void *);
		unsigned long asize = size * (nr_cpu_ids - 1);

		tsize = roundup(tsize, cache_line_size());
		new_cpu_pda = alloc_bootmem(tsize + asize);
		pda = (char *)new_cpu_pda + tsize;
	}

	/* initialize pointer table to static pda's */
	for_each_possible_cpu(cpu) {
		if (cpu == 0) {
			/* leave boot cpu pda in place */
			new_cpu_pda[0] = cpu_pda(0);
			continue;
		}
		new_cpu_pda[cpu] = (struct x8664_pda *)pda;
		new_cpu_pda[cpu]->in_bootmem = 1;
		pda += size;
	}

	/* point to new pointer table */
	_cpu_pda = new_cpu_pda;
}
#endif

/*
 * Great future plan:
 * Declare PDA itself and support (irqstack,tss,pgd) as per cpu data.
 * Always point %gs to its beginning
 */
void __init setup_per_cpu_areas(void)
{
	ssize_t size = PERCPU_ENOUGH_ROOM;
	char *ptr;
	int cpu;

	/* Setup cpu_pda map */
	setup_cpu_pda_map();

	/* Copy section for each CPU (we discard the original) */
	size = PERCPU_ENOUGH_ROOM;
	printk(KERN_INFO "PERCPU: Allocating %zd bytes of per cpu data\n",
			  size);

	for_each_possible_cpu(cpu) {
#ifndef CONFIG_NEED_MULTIPLE_NODES
		ptr = alloc_bootmem_pages(size);
#else
		int node = early_cpu_to_node(cpu);
		if (!node_online(node) || !NODE_DATA(node)) {
			ptr = alloc_bootmem_pages(size);
			printk(KERN_INFO
			       "cpu %d has no node %d or node-local memory\n",
				cpu, node);
		}
		else
			ptr = alloc_bootmem_pages_node(NODE_DATA(node), size);
#endif
		per_cpu_offset(cpu) = ptr - __per_cpu_start;
		memcpy(ptr, __per_cpu_start, __per_cpu_end - __per_cpu_start);

	}

	printk(KERN_DEBUG "NR_CPUS: %d, nr_cpu_ids: %d, nr_node_ids %d\n",
		NR_CPUS, nr_cpu_ids, nr_node_ids);

	/* Setup percpu data maps */
	setup_per_cpu_maps();

	/* Setup node to cpumask map */
	setup_node_to_cpumask_map();
}

#endif

#ifdef X86_64_NUMA

/*
 * Allocate node_to_cpumask_map based on number of available nodes
 * Requires node_possible_map to be valid.
 *
 * Note: node_to_cpumask() is not valid until after this is done.
 */
static void __init setup_node_to_cpumask_map(void)
{
	unsigned int node, num = 0;
	cpumask_t *map;

	/* setup nr_node_ids if not done yet */
	if (nr_node_ids == MAX_NUMNODES) {
		for_each_node_mask(node, node_possible_map)
			num = node;
		nr_node_ids = num + 1;
	}

	/* allocate the map */
	map = alloc_bootmem_low(nr_node_ids * sizeof(cpumask_t));

	pr_debug(KERN_DEBUG "Node to cpumask map at %p for %d nodes\n",
		 map, nr_node_ids);

	/* node_to_cpumask() will now work */
	node_to_cpumask_map = map;
}

void __cpuinit numa_set_node(int cpu, int node)
{
	int *cpu_to_node_map = early_per_cpu_ptr(x86_cpu_to_node_map);

	if (cpu_pda(cpu) && node != NUMA_NO_NODE)
		cpu_pda(cpu)->nodenumber = node;

	if (cpu_to_node_map)
		cpu_to_node_map[cpu] = node;

	else if (per_cpu_offset(cpu))
		per_cpu(x86_cpu_to_node_map, cpu) = node;

	else
		pr_debug("Setting node for non-present cpu %d\n", cpu);
}

void __cpuinit numa_clear_node(int cpu)
{
	numa_set_node(cpu, NUMA_NO_NODE);
}

#ifndef CONFIG_DEBUG_PER_CPU_MAPS

void __cpuinit numa_add_cpu(int cpu)
{
	cpu_set(cpu, node_to_cpumask_map[early_cpu_to_node(cpu)]);
}

void __cpuinit numa_remove_cpu(int cpu)
{
	cpu_clear(cpu, node_to_cpumask_map[cpu_to_node(cpu)]);
}

#else /* CONFIG_DEBUG_PER_CPU_MAPS */

/*
 * --------- debug versions of the numa functions ---------
 */
static void __cpuinit numa_set_cpumask(int cpu, int enable)
{
	int node = cpu_to_node(cpu);
	cpumask_t *mask;
	char buf[64];

	if (node_to_cpumask_map == NULL) {
		printk(KERN_ERR "node_to_cpumask_map NULL\n");
		dump_stack();
		return;
	}

	mask = &node_to_cpumask_map[node];
	if (enable)
		cpu_set(cpu, *mask);
	else
		cpu_clear(cpu, *mask);

	cpulist_scnprintf(buf, sizeof(buf), *mask);
	printk(KERN_DEBUG "%s cpu %d node %d: mask now %s\n",
		enable? "numa_add_cpu":"numa_remove_cpu", cpu, node, buf);
 }

void __cpuinit numa_add_cpu(int cpu)
{
	numa_set_cpumask(cpu, 1);
}

void __cpuinit numa_remove_cpu(int cpu)
{
	numa_set_cpumask(cpu, 0);
}

int cpu_to_node(int cpu)
{
	if (early_per_cpu_ptr(x86_cpu_to_node_map)) {
		printk(KERN_WARNING
			"cpu_to_node(%d): usage too early!\n", cpu);
		dump_stack();
		return early_per_cpu_ptr(x86_cpu_to_node_map)[cpu];
	}
	return per_cpu(x86_cpu_to_node_map, cpu);
}
EXPORT_SYMBOL(cpu_to_node);

/*
 * Same function as cpu_to_node() but used if called before the
 * per_cpu areas are setup.
 */
int early_cpu_to_node(int cpu)
{
	if (early_per_cpu_ptr(x86_cpu_to_node_map))
		return early_per_cpu_ptr(x86_cpu_to_node_map)[cpu];

	if (!per_cpu_offset(cpu)) {
		printk(KERN_WARNING
			"early_cpu_to_node(%d): no per_cpu area!\n", cpu);
		dump_stack();
		return NUMA_NO_NODE;
	}
	return per_cpu(x86_cpu_to_node_map, cpu);
}


/* empty cpumask */
static const cpumask_t cpu_mask_none;

/*
 * Returns a pointer to the bitmask of CPUs on Node 'node'.
 */
const cpumask_t *_node_to_cpumask_ptr(int node)
{
	if (node_to_cpumask_map == NULL) {
		printk(KERN_WARNING
			"_node_to_cpumask_ptr(%d): no node_to_cpumask_map!\n",
			node);
		dump_stack();
		return (const cpumask_t *)&cpu_online_map;
	}
	if (node >= nr_node_ids) {
		printk(KERN_WARNING
			"_node_to_cpumask_ptr(%d): node > nr_node_ids(%d)\n",
			node, nr_node_ids);
		dump_stack();
		return &cpu_mask_none;
	}
	return &node_to_cpumask_map[node];
}
EXPORT_SYMBOL(_node_to_cpumask_ptr);

/*
 * Returns a bitmask of CPUs on Node 'node'.
 *
 * Side note: this function creates the returned cpumask on the stack
 * so with a high NR_CPUS count, excessive stack space is used.  The
 * node_to_cpumask_ptr function should be used whenever possible.
 */
cpumask_t node_to_cpumask(int node)
{
	if (node_to_cpumask_map == NULL) {
		printk(KERN_WARNING
			"node_to_cpumask(%d): no node_to_cpumask_map!\n", node);
		dump_stack();
		return cpu_online_map;
	}
	if (node >= nr_node_ids) {
		printk(KERN_WARNING
			"node_to_cpumask(%d): node > nr_node_ids(%d)\n",
			node, nr_node_ids);
		dump_stack();
		return cpu_mask_none;
	}
	return node_to_cpumask_map[node];
}
EXPORT_SYMBOL(node_to_cpumask);

/*
 * --------- end of debug versions of the numa functions ---------
 */

#endif /* CONFIG_DEBUG_PER_CPU_MAPS */

#endif /* X86_64_NUMA */

#include <linux/smp.h>
#include <linux/topology.h>
#include <linux/pfn.h>
#include <asm/sections.h>
#include <asm/processor.h>
#include <asm/setup.h>
#include <asm/mpspec.h>
#include <asm/apicdef.h>
#include <asm/highmem.h>
#include <asm/proto.h>
#include <asm/cpumask.h>
#include <asm/cpu.h>
#include <asm/stackprotector.h>

DEFINE_PER_CPU_READ_MOSTLY(int, cpu_number);
EXPORT_PER_CPU_SYMBOL(cpu_number);

#ifdef CONFIG_X86_64
#define BOOT_PERCPU_OFFSET ((unsigned long)__per_cpu_load)
#else
#define BOOT_PERCPU_OFFSET 0
#endif

DEFINE_PER_CPU_READ_MOSTLY(unsigned long, this_cpu_off) = BOOT_PERCPU_OFFSET;
EXPORT_PER_CPU_SYMBOL(this_cpu_off);

unsigned long __per_cpu_offset[NR_CPUS] __read_mostly = {
	[0 ... NR_CPUS-1] = BOOT_PERCPU_OFFSET,
};
EXPORT_SYMBOL(__per_cpu_offset);

/*
 * On x86_64 symbols referenced from code should be reachable using
 * 32bit relocations.  Reserve space for static percpu variables in
 * modules so that they are always served from the first chunk which
 * is located at the percpu segment base.  On x86_32, anything can
 * address anywhere.  No need to reserve space in the first chunk.
 */
#ifdef CONFIG_X86_64
#define PERCPU_FIRST_CHUNK_RESERVE	PERCPU_MODULE_RESERVE
#else
#define PERCPU_FIRST_CHUNK_RESERVE	0
#endif

#ifdef CONFIG_X86_32
/**
 * pcpu_need_numa - determine percpu allocation needs to consider NUMA
 *
 * If NUMA is not configured or there is only one NUMA node available,
 * there is no reason to consider NUMA.  This function determines
 * whether percpu allocation should consider NUMA or not.
 *
 * RETURNS:
 * true if NUMA should be considered; otherwise, false.
 */
static bool __init pcpu_need_numa(void)
{
#ifdef CONFIG_NEED_MULTIPLE_NODES
	pg_data_t *last = NULL;
	unsigned int cpu;

	for_each_possible_cpu(cpu) {
		int node = early_cpu_to_node(cpu);

		if (node_online(node) && NODE_DATA(node) &&
		    last && last != NODE_DATA(node))
			return true;

		last = NODE_DATA(node);
	}
#endif
	return false;
}
#endif

/**
 * pcpu_alloc_bootmem - NUMA friendly alloc_bootmem wrapper for percpu
 * @cpu: cpu to allocate for
 * @size: size allocation in bytes
 * @align: alignment
 *
 * Allocate @size bytes aligned at @align for cpu @cpu.  This wrapper
 * does the right thing for NUMA regardless of the current
 * configuration.
 *
 * RETURNS:
 * Pointer to the allocated area on success, NULL on failure.
 */
static void * __init pcpu_alloc_bootmem(unsigned int cpu, unsigned long size,
					unsigned long align)
{
	const unsigned long goal = __pa(MAX_DMA_ADDRESS);
#ifdef CONFIG_NEED_MULTIPLE_NODES
	int node = early_cpu_to_node(cpu);
	void *ptr;

	if (!node_online(node) || !NODE_DATA(node)) {
		ptr = __alloc_bootmem_nopanic(size, align, goal);
		pr_info("cpu %d has no node %d or node-local memory\n",
			cpu, node);
		pr_debug("per cpu data for cpu%d %lu bytes at %016lx\n",
			 cpu, size, __pa(ptr));
	} else {
		ptr = __alloc_bootmem_node_nopanic(NODE_DATA(node),
						   size, align, goal);
		pr_debug("per cpu data for cpu%d %lu bytes on node%d at %016lx\n",
			 cpu, size, node, __pa(ptr));
	}
	return ptr;
#else
	return __alloc_bootmem_nopanic(size, align, goal);
#endif
}

/*
 * Helpers for first chunk memory allocation
 */
static void * __init pcpu_fc_alloc(unsigned int cpu, size_t size, size_t align)
{
	return pcpu_alloc_bootmem(cpu, size, align);
}

static void __init pcpu_fc_free(void *ptr, size_t size)
{
	free_bootmem(__pa(ptr), size);
}

static int __init pcpu_cpu_distance(unsigned int from, unsigned int to)
{
#ifdef CONFIG_NEED_MULTIPLE_NODES
	if (early_cpu_to_node(from) == early_cpu_to_node(to))
		return LOCAL_DISTANCE;
	else
		return REMOTE_DISTANCE;
#else
	return LOCAL_DISTANCE;
#endif
}

static void __init pcpup_populate_pte(unsigned long addr)
{
	populate_extra_pte(addr);
}

static inline void setup_percpu_segment(int cpu)
{
#ifdef CONFIG_X86_32
	struct desc_struct gdt;

	pack_descriptor(&gdt, per_cpu_offset(cpu), 0xFFFFF,
			0x2 | DESCTYPE_S, 0x8);
	gdt.s = 1;
	write_gdt_entry(get_cpu_gdt_table(cpu),
			GDT_ENTRY_PERCPU, &gdt, DESCTYPE_S);
#endif
}

void __init setup_per_cpu_areas(void)
{
	unsigned int cpu;
	unsigned long delta;
	int rc;

	pr_info("NR_CPUS:%d nr_cpumask_bits:%d nr_cpu_ids:%d nr_node_ids:%d\n",
		NR_CPUS, nr_cpumask_bits, nr_cpu_ids, nr_node_ids);

	/*
	 * Allocate percpu area.  Embedding allocator is our favorite;
	 * however, on NUMA configurations, it can result in very
	 * sparse unit mapping and vmalloc area isn't spacious enough
	 * on 32bit.  Use page in that case.
	 */
#ifdef CONFIG_X86_32
	if (pcpu_chosen_fc == PCPU_FC_AUTO && pcpu_need_numa())
		pcpu_chosen_fc = PCPU_FC_PAGE;
#endif
	rc = -EINVAL;
	if (pcpu_chosen_fc != PCPU_FC_PAGE) {
		const size_t dyn_size = PERCPU_MODULE_RESERVE +
			PERCPU_DYNAMIC_RESERVE - PERCPU_FIRST_CHUNK_RESERVE;
		size_t atom_size;

		/*
		 * On 64bit, use PMD_SIZE for atom_size so that embedded
		 * percpu areas are aligned to PMD.  This, in the future,
		 * can also allow using PMD mappings in vmalloc area.  Use
		 * PAGE_SIZE on 32bit as vmalloc space is highly contended
		 * and large vmalloc area allocs can easily fail.
		 */
#ifdef CONFIG_X86_64
		atom_size = PMD_SIZE;
#else
		atom_size = PAGE_SIZE;
#endif
		rc = pcpu_embed_first_chunk(PERCPU_FIRST_CHUNK_RESERVE,
					    dyn_size, atom_size,
					    pcpu_cpu_distance,
					    pcpu_fc_alloc, pcpu_fc_free);
		if (rc < 0)
			pr_warning("%s allocator failed (%d), falling back to page size\n",
				   pcpu_fc_names[pcpu_chosen_fc], rc);
	}
	if (rc < 0)
		rc = pcpu_page_first_chunk(PERCPU_FIRST_CHUNK_RESERVE,
					   pcpu_fc_alloc, pcpu_fc_free,
					   pcpup_populate_pte);
	if (rc < 0)
		panic("cannot initialize percpu area (err=%d)", rc);

	/* alrighty, percpu areas up and running */
	delta = (unsigned long)pcpu_base_addr - (unsigned long)__per_cpu_start;
	for_each_possible_cpu(cpu) {
		per_cpu_offset(cpu) = delta + pcpu_unit_offsets[cpu];
		per_cpu(this_cpu_off, cpu) = per_cpu_offset(cpu);
		per_cpu(cpu_number, cpu) = cpu;
		setup_percpu_segment(cpu);
		setup_stack_canary_segment(cpu);
		/*
		 * Copy data used in early init routines from the
		 * initial arrays to the per cpu data areas.  These
		 * arrays then become expendable and the *_early_ptr's
		 * are zeroed indicating that the static arrays are
		 * gone.
		 */
#ifdef CONFIG_X86_LOCAL_APIC
		per_cpu(x86_cpu_to_apicid, cpu) =
			early_per_cpu_map(x86_cpu_to_apicid, cpu);
		per_cpu(x86_bios_cpu_apicid, cpu) =
			early_per_cpu_map(x86_bios_cpu_apicid, cpu);
#endif
#ifdef CONFIG_X86_32
		per_cpu(x86_cpu_to_logical_apicid, cpu) =
			early_per_cpu_map(x86_cpu_to_logical_apicid, cpu);
#endif
#ifdef CONFIG_X86_64
		per_cpu(irq_stack_ptr, cpu) =
			per_cpu(irq_stack_union.irq_stack, cpu) +
			IRQ_STACK_SIZE - 64;
#endif
#ifdef CONFIG_NUMA
		per_cpu(x86_cpu_to_node_map, cpu) =
			early_per_cpu_map(x86_cpu_to_node_map, cpu);
		/*
		 * Ensure that the boot cpu numa_node is correct when the boot
		 * cpu is on a node that doesn't have memory installed.
		 * Also cpu_up() will call cpu_to_node() for APs when
		 * MEMORY_HOTPLUG is defined, before per_cpu(numa_node) is set
		 * up later with c_init aka intel_init/amd_init.
		 * So set them all (boot cpu and all APs).
		 */
		set_cpu_numa_node(cpu, early_cpu_to_node(cpu));
#endif
		/*
		 * Up to this point, the boot CPU has been using .init.data
		 * area.  Reload any changed state for the boot CPU.
		 */
		if (!cpu)
			switch_to_new_gdt(cpu);
	}

	/* indicate the early static arrays will soon be gone */
#ifdef CONFIG_X86_LOCAL_APIC
	early_per_cpu_ptr(x86_cpu_to_apicid) = NULL;
	early_per_cpu_ptr(x86_bios_cpu_apicid) = NULL;
#endif
#ifdef CONFIG_X86_32
	early_per_cpu_ptr(x86_cpu_to_logical_apicid) = NULL;
#endif
#ifdef CONFIG_NUMA
	early_per_cpu_ptr(x86_cpu_to_node_map) = NULL;
#endif

	/* Setup node to cpumask map */
	setup_node_to_cpumask_map();

	/* Setup cpu initialized, callin, callout masks */
	setup_cpu_local_masks();
}
