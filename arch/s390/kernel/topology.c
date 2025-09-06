/*
 *    Copyright IBM Corp. 2007
 *    Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/bootmem.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/cpu.h>
#include <linux/smp.h>
#include <asm/delay.h>
#include <asm/s390_ext.h>
#include <asm/sysinfo.h>

#define CPU_BITS 64
#define NR_MAG 6
 *    Copyright IBM Corp. 2007, 2011
 *    Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>
 */

#define KMSG_COMPONENT "cpu"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/workqueue.h>
#include <linux/cpuset.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/smp.h>
#include <linux/mm.h>
#include <linux/nodemask.h>
#include <linux/node.h>
#include <asm/sysinfo.h>
#include <asm/numa.h>

#define PTF_HORIZONTAL	(0UL)
#define PTF_VERTICAL	(1UL)
#define PTF_CHECK	(2UL)

struct tl_cpu {
	unsigned char reserved0[4];
	unsigned char :6;
	unsigned char pp:2;
	unsigned char reserved1;
	unsigned short origin;
	unsigned long mask[CPU_BITS / BITS_PER_LONG];
};

struct tl_container {
	unsigned char reserved[8];
};

union tl_entry {
	unsigned char nl;
	struct tl_cpu cpu;
	struct tl_container container;
};

struct tl_info {
	unsigned char reserved0[2];
	unsigned short length;
	unsigned char mag[NR_MAG];
	unsigned char reserved1;
	unsigned char mnest;
	unsigned char reserved2[4];
	union tl_entry tle[0];
};

struct core_info {
	struct core_info *next;
	cpumask_t mask;
};

static void topology_work_fn(struct work_struct *work);
static struct tl_info *tl_info;
static struct core_info core_info;
static int machine_has_topology;
static int machine_has_topology_irq;
static struct timer_list topology_timer;
static void set_topology_timer(void);
static DECLARE_WORK(topology_work, topology_work_fn);

cpumask_t cpu_core_map[NR_CPUS];

cpumask_t cpu_coregroup_map(unsigned int cpu)
{
	struct core_info *core = &core_info;
	cpumask_t mask;

	cpus_clear(mask);
	if (!machine_has_topology)
		return cpu_present_map;
	mutex_lock(&smp_cpu_state_mutex);
	while (core) {
		if (cpu_isset(cpu, core->mask)) {
			mask = core->mask;
			break;
		}
		core = core->next;
	}
	mutex_unlock(&smp_cpu_state_mutex);
	if (cpus_empty(mask))
		mask = cpumask_of_cpu(cpu);
	return mask;
}

static void add_cpus_to_core(struct tl_cpu *tl_cpu, struct core_info *core)
{
	unsigned int cpu;

	for (cpu = find_first_bit(&tl_cpu->mask[0], CPU_BITS);
	     cpu < CPU_BITS;
	     cpu = find_next_bit(&tl_cpu->mask[0], CPU_BITS, cpu + 1))
	{
		unsigned int rcpu, lcpu;

		rcpu = CPU_BITS - 1 - cpu + tl_cpu->origin;
		for_each_present_cpu(lcpu) {
			if (__cpu_logical_map[lcpu] == rcpu) {
				cpu_set(lcpu, core->mask);
				smp_cpu_polarization[lcpu] = tl_cpu->pp;
			}
		}
	}
}

static void clear_cores(void)
{
	struct core_info *core = &core_info;

	while (core) {
		cpus_clear(core->mask);
		core = core->next;
	}
}

static union tl_entry *next_tle(union tl_entry *tle)
{
	if (tle->nl)
		return (union tl_entry *)((struct tl_container *)tle + 1);
	else
		return (union tl_entry *)((struct tl_cpu *)tle + 1);
}

static void tl_to_cores(struct tl_info *info)
{
	union tl_entry *tle, *end;
	struct core_info *core = &core_info;

	mutex_lock(&smp_cpu_state_mutex);
	clear_cores();
	tle = info->tle;
	end = (union tl_entry *)((unsigned long)info + info->length);
	while (tle < end) {
		switch (tle->nl) {
		case 5:
		case 4:
		case 3:
		case 2:
			break;
		case 1:
			core = core->next;
			break;
		case 0:
			add_cpus_to_core(&tle->cpu, core);
			break;
		default:
			clear_cores();
			machine_has_topology = 0;
struct mask_info {
	struct mask_info *next;
	unsigned char id;
	cpumask_t mask;
};

static void set_topology_timer(void);
static void topology_work_fn(struct work_struct *work);
static struct sysinfo_15_1_x *tl_info;

static int topology_enabled = 1;
static DECLARE_WORK(topology_work, topology_work_fn);

/*
 * Socket/Book linked lists and per_cpu(cpu_topology) updates are
 * protected by "sched_domains_mutex".
 */
static struct mask_info socket_info;
static struct mask_info book_info;

DEFINE_PER_CPU(struct cpu_topology_s390, cpu_topology);
EXPORT_PER_CPU_SYMBOL_GPL(cpu_topology);

static cpumask_t cpu_group_map(struct mask_info *info, unsigned int cpu)
{
	cpumask_t mask;

	cpumask_copy(&mask, cpumask_of(cpu));
	if (!topology_enabled || !MACHINE_HAS_TOPOLOGY)
		return mask;
	for (; info; info = info->next) {
		if (cpumask_test_cpu(cpu, &info->mask))
			return info->mask;
	}
	return mask;
}

static cpumask_t cpu_thread_map(unsigned int cpu)
{
	cpumask_t mask;
	int i;

	cpumask_copy(&mask, cpumask_of(cpu));
	if (!topology_enabled || !MACHINE_HAS_TOPOLOGY)
		return mask;
	cpu -= cpu % (smp_cpu_mtid + 1);
	for (i = 0; i <= smp_cpu_mtid; i++)
		if (cpu_present(cpu + i))
			cpumask_set_cpu(cpu + i, &mask);
	return mask;
}

static struct mask_info *add_cpus_to_mask(struct topology_core *tl_core,
					  struct mask_info *book,
					  struct mask_info *socket,
					  int one_socket_per_cpu)
{
	struct cpu_topology_s390 *topo;
	unsigned int core;

	for_each_set_bit(core, &tl_core->mask[0], TOPOLOGY_CORE_BITS) {
		unsigned int rcore;
		int lcpu, i;

		rcore = TOPOLOGY_CORE_BITS - 1 - core + tl_core->origin;
		lcpu = smp_find_processor_id(rcore << smp_cpu_mt_shift);
		if (lcpu < 0)
			continue;
		for (i = 0; i <= smp_cpu_mtid; i++) {
			topo = &per_cpu(cpu_topology, lcpu + i);
			topo->book_id = book->id;
			topo->core_id = rcore;
			topo->thread_id = lcpu + i;
			cpumask_set_cpu(lcpu + i, &book->mask);
			cpumask_set_cpu(lcpu + i, &socket->mask);
			if (one_socket_per_cpu)
				topo->socket_id = rcore;
			else
				topo->socket_id = socket->id;
			smp_cpu_set_polarization(lcpu + i, tl_core->pp);
		}
		if (one_socket_per_cpu)
			socket = socket->next;
	}
	return socket;
}

static void clear_masks(void)
{
	struct mask_info *info;

	info = &socket_info;
	while (info) {
		cpumask_clear(&info->mask);
		info = info->next;
	}
	info = &book_info;
	while (info) {
		cpumask_clear(&info->mask);
		info = info->next;
	}
}

static union topology_entry *next_tle(union topology_entry *tle)
{
	if (!tle->nl)
		return (union topology_entry *)((struct topology_core *)tle + 1);
	return (union topology_entry *)((struct topology_container *)tle + 1);
}

static void __tl_to_masks_generic(struct sysinfo_15_1_x *info)
{
	struct mask_info *socket = &socket_info;
	struct mask_info *book = &book_info;
	union topology_entry *tle, *end;

	tle = info->tle;
	end = (union topology_entry *)((unsigned long)info + info->length);
	while (tle < end) {
		switch (tle->nl) {
		case 2:
			book = book->next;
			book->id = tle->container.id;
			break;
		case 1:
			socket = socket->next;
			socket->id = tle->container.id;
			break;
		case 0:
			add_cpus_to_mask(&tle->cpu, book, socket, 0);
			break;
		default:
			clear_masks();
			return;
		}
		tle = next_tle(tle);
	}
	mutex_unlock(&smp_cpu_state_mutex);
}

static void __tl_to_masks_z10(struct sysinfo_15_1_x *info)
{
	struct mask_info *socket = &socket_info;
	struct mask_info *book = &book_info;
	union topology_entry *tle, *end;

	tle = info->tle;
	end = (union topology_entry *)((unsigned long)info + info->length);
	while (tle < end) {
		switch (tle->nl) {
		case 1:
			book = book->next;
			book->id = tle->container.id;
			break;
		case 0:
			socket = add_cpus_to_mask(&tle->cpu, book, socket, 1);
			break;
		default:
			clear_masks();
			return;
		}
		tle = next_tle(tle);
	}
}

static void tl_to_masks(struct sysinfo_15_1_x *info)
{
	struct cpuid cpu_id;

	get_cpu_id(&cpu_id);
	clear_masks();
	switch (cpu_id.machine) {
	case 0x2097:
	case 0x2098:
		__tl_to_masks_z10(info);
		break;
	default:
		__tl_to_masks_generic(info);
	}
}

static void topology_update_polarization_simple(void)
{
	int cpu;

	mutex_lock(&smp_cpu_state_mutex);
	for_each_present_cpu(cpu)
		smp_cpu_polarization[cpu] = POLARIZATION_HRZ;
	for_each_possible_cpu(cpu)
		smp_cpu_set_polarization(cpu, POLARIZATION_HRZ);
	mutex_unlock(&smp_cpu_state_mutex);
}

static int ptf(unsigned long fc)
{
	int rc;

	asm volatile(
		"	.insn	rre,0xb9a20000,%1,%1\n"
		"	ipm	%0\n"
		"	srl	%0,28\n"
		: "=d" (rc)
		: "d" (fc)  : "cc");
	return rc;
}

int topology_set_cpu_management(int fc)
{
	int cpu;
	int rc;

	if (!machine_has_topology)
	int cpu, rc;

	if (!MACHINE_HAS_TOPOLOGY)
		return -EOPNOTSUPP;
	if (fc)
		rc = ptf(PTF_VERTICAL);
	else
		rc = ptf(PTF_HORIZONTAL);
	if (rc)
		return -EBUSY;
	for_each_present_cpu(cpu)
		smp_cpu_polarization[cpu] = POLARIZATION_UNKNWN;
	return rc;
}

static void update_cpu_core_map(void)
{
	int cpu;

	for_each_present_cpu(cpu)
		cpu_core_map[cpu] = cpu_coregroup_map(cpu);
}

void arch_update_cpu_topology(void)
{
	struct tl_info *info = tl_info;
	struct sys_device *sysdev;
	int cpu;

	if (!machine_has_topology) {
		update_cpu_core_map();
		topology_update_polarization_simple();
		return;
	}
	stsi(info, 15, 1, 2);
	tl_to_cores(info);
	update_cpu_core_map();
	for_each_online_cpu(cpu) {
		sysdev = get_cpu_sysdev(cpu);
		kobject_uevent(&sysdev->kobj, KOBJ_CHANGE);
	}
	for_each_possible_cpu(cpu)
		smp_cpu_set_polarization(cpu, POLARIZATION_UNKNOWN);
	return rc;
}

static void update_cpu_masks(void)
{
	struct cpu_topology_s390 *topo;
	int cpu;

	for_each_possible_cpu(cpu) {
		topo = &per_cpu(cpu_topology, cpu);
		topo->thread_mask = cpu_thread_map(cpu);
		topo->core_mask = cpu_group_map(&socket_info, cpu);
		topo->book_mask = cpu_group_map(&book_info, cpu);
		if (!MACHINE_HAS_TOPOLOGY) {
			topo->thread_id = cpu;
			topo->core_id = cpu;
			topo->socket_id = cpu;
			topo->book_id = cpu;
		}
	}
	numa_update_cpu_topology();
}

void store_topology(struct sysinfo_15_1_x *info)
{
	if (topology_max_mnest >= 3)
		stsi(info, 15, 1, 3);
	else
		stsi(info, 15, 1, 2);
}

int arch_update_cpu_topology(void)
{
	struct sysinfo_15_1_x *info = tl_info;
	struct device *dev;
	int cpu, rc = 0;

	if (MACHINE_HAS_TOPOLOGY) {
		rc = 1;
		store_topology(info);
		tl_to_masks(info);
	}
	update_cpu_masks();
	if (!MACHINE_HAS_TOPOLOGY)
		topology_update_polarization_simple();
	for_each_online_cpu(cpu) {
		dev = get_cpu_device(cpu);
		if (dev)
			kobject_uevent(&dev->kobj, KOBJ_CHANGE);
	}
	return rc;
}

static void topology_work_fn(struct work_struct *work)
{
	arch_reinit_sched_domains();
	rebuild_sched_domains();
}

void topology_schedule_update(void)
{
	schedule_work(&topology_work);
}

static void topology_timer_fn(unsigned long ignored)
{
	if (ptf(PTF_CHECK))
		topology_schedule_update();
	set_topology_timer();
}

static void set_topology_timer(void)
{
	topology_timer.function = topology_timer_fn;
	topology_timer.data = 0;
	topology_timer.expires = jiffies + 60 * HZ;
	add_timer(&topology_timer);
}

static void topology_interrupt(__u16 code)
{
	schedule_work(&topology_work);
}

static int __init init_topology_update(void)
{
	int rc;

	rc = 0;
	if (!machine_has_topology) {
		topology_update_polarization_simple();
		goto out;
	}
	init_timer_deferrable(&topology_timer);
	if (machine_has_topology_irq) {
		rc = register_external_interrupt(0x2005, topology_interrupt);
		if (rc)
			goto out;
		ctl_set_bit(0, 8);
	}
	else
		set_topology_timer();
out:
	update_cpu_core_map();
	return rc;
}
__initcall(init_topology_update);

void __init s390_init_cpu_topology(void)
{
	unsigned long long facility_bits;
	struct tl_info *info;
	struct core_info *core;
	int nr_cores;
	int i;

	if (stfle(&facility_bits, 1) <= 0)
		return;
	if (!(facility_bits & (1ULL << 52)) || !(facility_bits & (1ULL << 61)))
		return;
	machine_has_topology = 1;

	if (facility_bits & (1ULL << 51))
		machine_has_topology_irq = 1;

	tl_info = alloc_bootmem_pages(PAGE_SIZE);
	info = tl_info;
	stsi(info, 15, 1, 2);

	nr_cores = info->mag[NR_MAG - 2];
	for (i = 0; i < info->mnest - 2; i++)
		nr_cores *= info->mag[NR_MAG - 3 - i];

	printk(KERN_INFO "CPU topology:");
	for (i = 0; i < NR_MAG; i++)
		printk(" %d", info->mag[i]);
	printk(" / %d\n", info->mnest);

	core = &core_info;
	for (i = 0; i < nr_cores; i++) {
		core->next = alloc_bootmem(sizeof(struct core_info));
		core = core->next;
		if (!core)
			goto error;
	}
	return;
error:
	machine_has_topology = 0;
	machine_has_topology_irq = 0;
}
static struct timer_list topology_timer =
	TIMER_DEFERRED_INITIALIZER(topology_timer_fn, 0, 0);

static atomic_t topology_poll = ATOMIC_INIT(0);

static void set_topology_timer(void)
{
	if (atomic_add_unless(&topology_poll, -1, 0))
		mod_timer(&topology_timer, jiffies + HZ / 10);
	else
		mod_timer(&topology_timer, jiffies + HZ * 60);
}

void topology_expect_change(void)
{
	if (!MACHINE_HAS_TOPOLOGY)
		return;
	/* This is racy, but it doesn't matter since it is just a heuristic.
	 * Worst case is that we poll in a higher frequency for a bit longer.
	 */
	if (atomic_read(&topology_poll) > 60)
		return;
	atomic_add(60, &topology_poll);
	set_topology_timer();
}

static int cpu_management;

static ssize_t dispatching_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	ssize_t count;

	mutex_lock(&smp_cpu_state_mutex);
	count = sprintf(buf, "%d\n", cpu_management);
	mutex_unlock(&smp_cpu_state_mutex);
	return count;
}

static ssize_t dispatching_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf,
				 size_t count)
{
	int val, rc;
	char delim;

	if (sscanf(buf, "%d %c", &val, &delim) != 1)
		return -EINVAL;
	if (val != 0 && val != 1)
		return -EINVAL;
	rc = 0;
	get_online_cpus();
	mutex_lock(&smp_cpu_state_mutex);
	if (cpu_management == val)
		goto out;
	rc = topology_set_cpu_management(val);
	if (rc)
		goto out;
	cpu_management = val;
	topology_expect_change();
out:
	mutex_unlock(&smp_cpu_state_mutex);
	put_online_cpus();
	return rc ? rc : count;
}
static DEVICE_ATTR(dispatching, 0644, dispatching_show,
			 dispatching_store);

static ssize_t cpu_polarization_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	int cpu = dev->id;
	ssize_t count;

	mutex_lock(&smp_cpu_state_mutex);
	switch (smp_cpu_get_polarization(cpu)) {
	case POLARIZATION_HRZ:
		count = sprintf(buf, "horizontal\n");
		break;
	case POLARIZATION_VL:
		count = sprintf(buf, "vertical:low\n");
		break;
	case POLARIZATION_VM:
		count = sprintf(buf, "vertical:medium\n");
		break;
	case POLARIZATION_VH:
		count = sprintf(buf, "vertical:high\n");
		break;
	default:
		count = sprintf(buf, "unknown\n");
		break;
	}
	mutex_unlock(&smp_cpu_state_mutex);
	return count;
}
static DEVICE_ATTR(polarization, 0444, cpu_polarization_show, NULL);

static struct attribute *topology_cpu_attrs[] = {
	&dev_attr_polarization.attr,
	NULL,
};

static struct attribute_group topology_cpu_attr_group = {
	.attrs = topology_cpu_attrs,
};

int topology_cpu_init(struct cpu *cpu)
{
	return sysfs_create_group(&cpu->dev.kobj, &topology_cpu_attr_group);
}

static const struct cpumask *cpu_thread_mask(int cpu)
{
	return &per_cpu(cpu_topology, cpu).thread_mask;
}


const struct cpumask *cpu_coregroup_mask(int cpu)
{
	return &per_cpu(cpu_topology, cpu).core_mask;
}

static const struct cpumask *cpu_book_mask(int cpu)
{
	return &per_cpu(cpu_topology, cpu).book_mask;
}

static int __init early_parse_topology(char *p)
{
	if (strncmp(p, "off", 3))
		return 0;
	topology_enabled = 0;
	return 0;
}
early_param("topology", early_parse_topology);

static struct sched_domain_topology_level s390_topology[] = {
	{ cpu_thread_mask, cpu_smt_flags, SD_INIT_NAME(SMT) },
	{ cpu_coregroup_mask, cpu_core_flags, SD_INIT_NAME(MC) },
	{ cpu_book_mask, SD_INIT_NAME(BOOK) },
	{ cpu_cpu_mask, SD_INIT_NAME(DIE) },
	{ NULL, },
};

static void __init alloc_masks(struct sysinfo_15_1_x *info,
			       struct mask_info *mask, int offset)
{
	int i, nr_masks;

	nr_masks = info->mag[TOPOLOGY_NR_MAG - offset];
	for (i = 0; i < info->mnest - offset; i++)
		nr_masks *= info->mag[TOPOLOGY_NR_MAG - offset - 1 - i];
	nr_masks = max(nr_masks, 1);
	for (i = 0; i < nr_masks; i++) {
		mask->next = kzalloc(sizeof(*mask->next), GFP_KERNEL);
		mask = mask->next;
	}
}

static int __init s390_topology_init(void)
{
	struct sysinfo_15_1_x *info;
	int i;

	if (!MACHINE_HAS_TOPOLOGY)
		return 0;
	tl_info = (struct sysinfo_15_1_x *)__get_free_page(GFP_KERNEL);
	info = tl_info;
	store_topology(info);
	pr_info("The CPU configuration topology of the machine is:");
	for (i = 0; i < TOPOLOGY_NR_MAG; i++)
		printk(KERN_CONT " %d", info->mag[i]);
	printk(KERN_CONT " / %d\n", info->mnest);
	alloc_masks(info, &socket_info, 1);
	alloc_masks(info, &book_info, 2);
	set_sched_topology(s390_topology);
	return 0;
}
early_initcall(s390_topology_init);

static int __init topology_init(void)
{
	if (MACHINE_HAS_TOPOLOGY)
		set_topology_timer();
	else
		topology_update_polarization_simple();
	return device_create_file(cpu_subsys.dev_root, &dev_attr_dispatching);
}
device_initcall(topology_init);
