/**
 * @file common.c
 *
 * @remark Copyright 2004 Oprofile Authors
 * @remark Read the file COPYING
 *
 * @author Zwane Mwaikambo
 */

#include <linux/init.h>
#include <linux/oprofile.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/sysdev.h>
#include <linux/mutex.h>

#include "op_counter.h"
#include "op_arm_model.h"

static struct op_arm_model_spec *op_arm_model;
static int op_arm_enabled;
static DEFINE_MUTEX(op_arm_mutex);

struct op_counter_config *counter_config;

static int op_arm_create_files(struct super_block *sb, struct dentry *root)
{
	unsigned int i;

	for (i = 0; i < op_arm_model->num_counters; i++) {
		struct dentry *dir;
		char buf[4];

		snprintf(buf, sizeof buf, "%d", i);
		dir = oprofilefs_mkdir(sb, root, buf);
		oprofilefs_create_ulong(sb, dir, "enabled", &counter_config[i].enabled);
		oprofilefs_create_ulong(sb, dir, "event", &counter_config[i].event);
		oprofilefs_create_ulong(sb, dir, "count", &counter_config[i].count);
		oprofilefs_create_ulong(sb, dir, "unit_mask", &counter_config[i].unit_mask);
		oprofilefs_create_ulong(sb, dir, "kernel", &counter_config[i].kernel);
		oprofilefs_create_ulong(sb, dir, "user", &counter_config[i].user);
	}

	return 0;
}

static int op_arm_setup(void)
{
	int ret;

	spin_lock(&oprofilefs_lock);
	ret = op_arm_model->setup_ctrs();
	spin_unlock(&oprofilefs_lock);
	return ret;
}

static int op_arm_start(void)
{
	int ret = -EBUSY;

	mutex_lock(&op_arm_mutex);
	if (!op_arm_enabled) {
		ret = op_arm_model->start();
		op_arm_enabled = !ret;
	}
	mutex_unlock(&op_arm_mutex);
	return ret;
}

static void op_arm_stop(void)
{
	mutex_lock(&op_arm_mutex);
	if (op_arm_enabled)
		op_arm_model->stop();
	op_arm_enabled = 0;
	mutex_unlock(&op_arm_mutex);
}

#ifdef CONFIG_PM
static int op_arm_suspend(struct sys_device *dev, pm_message_t state)
{
	mutex_lock(&op_arm_mutex);
	if (op_arm_enabled)
		op_arm_model->stop();
	mutex_unlock(&op_arm_mutex);
	return 0;
}

static int op_arm_resume(struct sys_device *dev)
{
	mutex_lock(&op_arm_mutex);
	if (op_arm_enabled && op_arm_model->start())
		op_arm_enabled = 0;
	mutex_unlock(&op_arm_mutex);
	return 0;
}

static struct sysdev_class oprofile_sysclass = {
	.name		= "oprofile",
	.resume		= op_arm_resume,
	.suspend	= op_arm_suspend,
};

static struct sys_device device_oprofile = {
	.id		= 0,
	.cls		= &oprofile_sysclass,
};

static int __init init_driverfs(void)
{
	int ret;

	if (!(ret = sysdev_class_register(&oprofile_sysclass)))
		ret = sysdev_register(&device_oprofile);

	return ret;
}

static void  exit_driverfs(void)
{
	sysdev_unregister(&device_oprofile);
	sysdev_class_unregister(&oprofile_sysclass);
}
#else
#define init_driverfs()	do { } while (0)
#define exit_driverfs() do { } while (0)
#endif /* CONFIG_PM */

int __init oprofile_arch_init(struct oprofile_operations *ops)
{
	struct op_arm_model_spec *spec = NULL;
	int ret = -ENODEV;

	ops->backtrace = arm_backtrace;

#ifdef CONFIG_CPU_XSCALE
	spec = &op_xscale_spec;
#endif

#ifdef CONFIG_OPROFILE_ARMV6
	spec = &op_armv6_spec;
#endif

#ifdef CONFIG_OPROFILE_MPCORE
	spec = &op_mpcore_spec;
#endif

	if (spec) {
		ret = spec->init();
		if (ret < 0)
			return ret;

		counter_config = kcalloc(spec->num_counters, sizeof(struct op_counter_config),
					 GFP_KERNEL);
		if (!counter_config)
			return -ENOMEM;

		op_arm_model = spec;
		init_driverfs();
		ops->create_files = op_arm_create_files;
		ops->setup = op_arm_setup;
		ops->shutdown = op_arm_stop;
		ops->start = op_arm_start;
		ops->stop = op_arm_stop;
		ops->cpu_type = op_arm_model->name;
		printk(KERN_INFO "oprofile: using %s\n", spec->name);
	}

	return ret;
 * @remark Copyright 2010 ARM Ltd.
 * @remark Read the file COPYING
 *
 * @author Zwane Mwaikambo
 * @author Will Deacon [move to perf]
 */

#include <linux/cpumask.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/oprofile.h>
#include <linux/perf_event.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <asm/stacktrace.h>
#include <linux/uaccess.h>

#include <asm/perf_event.h>
#include <asm/ptrace.h>

#ifdef CONFIG_HW_PERF_EVENTS

/*
 * OProfile has a curious naming scheme for the ARM PMUs, but they are
 * part of the user ABI so we need to map from the perf PMU name for
 * supported PMUs.
 */
static struct op_perf_name {
	char *perf_name;
	char *op_name;
} op_perf_name_map[] = {
	{ "armv5_xscale1",	"arm/xscale1"	},
	{ "armv5_xscale2",	"arm/xscale2"	},
	{ "armv6_1136",		"arm/armv6"	},
	{ "armv6_1156",		"arm/armv6"	},
	{ "armv6_1176",		"arm/armv6"	},
	{ "armv6_11mpcore",	"arm/mpcore"	},
	{ "armv7_cortex_a8",	"arm/armv7"	},
	{ "armv7_cortex_a9",	"arm/armv7-ca9"	},
};

char *op_name_from_perf_id(void)
{
	int i;
	struct op_perf_name names;
	const char *perf_name = perf_pmu_name();

	for (i = 0; i < ARRAY_SIZE(op_perf_name_map); ++i) {
		names = op_perf_name_map[i];
		if (!strcmp(names.perf_name, perf_name))
			return names.op_name;
	}

	return NULL;
}
#endif

static int report_trace(struct stackframe *frame, void *d)
{
	unsigned int *depth = d;

	if (*depth) {
		oprofile_add_trace(frame->pc);
		(*depth)--;
	}

	return *depth == 0;
}

/*
 * The registers we're interested in are at the end of the variable
 * length saved register structure. The fp points at the end of this
 * structure so the address of this struct is:
 * (struct frame_tail *)(xxx->fp)-1
 */
struct frame_tail {
	struct frame_tail *fp;
	unsigned long sp;
	unsigned long lr;
} __attribute__((packed));

static struct frame_tail* user_backtrace(struct frame_tail *tail)
{
	struct frame_tail buftail[2];

	/* Also check accessibility of one struct frame_tail beyond */
	if (!access_ok(VERIFY_READ, tail, sizeof(buftail)))
		return NULL;
	if (__copy_from_user_inatomic(buftail, tail, sizeof(buftail)))
		return NULL;

	oprofile_add_trace(buftail[0].lr);

	/* frame pointers should strictly progress back up the stack
	 * (towards higher addresses) */
	if (tail + 1 >= buftail[0].fp)
		return NULL;

	return buftail[0].fp-1;
}

static void arm_backtrace(struct pt_regs * const regs, unsigned int depth)
{
	struct frame_tail *tail = ((struct frame_tail *) regs->ARM_fp) - 1;

	if (!user_mode(regs)) {
		struct stackframe frame;
		arm_get_current_stackframe(regs, &frame);
		walk_stackframe(&frame, report_trace, &depth);
		return;
	}

	while (depth-- && tail && !((unsigned long) tail & 3))
		tail = user_backtrace(tail);
}

int __init oprofile_arch_init(struct oprofile_operations *ops)
{
	/* provide backtrace support also in timer mode: */
	ops->backtrace		= arm_backtrace;

	return oprofile_perf_init(ops);
}

void oprofile_arch_exit(void)
{
	if (op_arm_model) {
		exit_driverfs();
		op_arm_model = NULL;
	}
	kfree(counter_config);
	oprofile_perf_exit();
}
