/*
 *  drivers/cpufreq/cpufreq_stats.c
 *
 *  Copyright (C) 2003-2004 Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>.
 *	      (C) 2004 Zou Nan hai <nanhai.zou@intel.com>.
 *  (C) 2004 Zou Nan hai <nanhai.zou@intel.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/sysdev.h>
#include <linux/cpu.h>
#include <linux/sysfs.h>
#include <linux/cpufreq.h>
#include <linux/jiffies.h>
#include <linux/percpu.h>
#include <linux/kobject.h>
#include <linux/spinlock.h>
#include <linux/notifier.h>
#include <asm/cputime.h>

static spinlock_t cpufreq_stats_lock;

#define CPUFREQ_STATDEVICE_ATTR(_name,_mode,_show) \
static struct freq_attr _attr_##_name = {\
	.attr = {.name = __stringify(_name), .mode = _mode, }, \
	.show = _show,\
};

struct cpufreq_stats {
	unsigned int cpu;
	unsigned int total_trans;
	unsigned long long  last_time;
	unsigned int max_state;
	unsigned int state_num;
	unsigned int last_index;
	cputime64_t *time_in_state;
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/cputime.h>

static spinlock_t cpufreq_stats_lock;

struct cpufreq_stats {
	unsigned int total_trans;
	unsigned long long last_time;
	unsigned int max_state;
	unsigned int state_num;
	unsigned int last_index;
	u64 *time_in_state;
	unsigned int *freq_table;
#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
	unsigned int *trans_table;
#endif
};

static DEFINE_PER_CPU(struct cpufreq_stats *, cpufreq_stats_table);

struct cpufreq_stats_attribute {
	struct attribute attr;
	ssize_t(*show) (struct cpufreq_stats *, char *);
};

static int
cpufreq_stats_update (unsigned int cpu)
{
	struct cpufreq_stats *stat;
	unsigned long long cur_time;

	cur_time = get_jiffies_64();
	spin_lock(&cpufreq_stats_lock);
	stat = per_cpu(cpufreq_stats_table, cpu);
	if (stat->time_in_state)
		stat->time_in_state[stat->last_index] =
			cputime64_add(stat->time_in_state[stat->last_index],
				      cputime_sub(cur_time, stat->last_time));
	stat->last_time = cur_time;
static int cpufreq_stats_update(struct cpufreq_stats *stats)
{
	unsigned long long cur_time = get_jiffies_64();

	spin_lock(&cpufreq_stats_lock);
	stats->time_in_state[stats->last_index] += cur_time - stats->last_time;
	stats->last_time = cur_time;
	spin_unlock(&cpufreq_stats_lock);
	return 0;
}

static ssize_t
show_total_trans(struct cpufreq_policy *policy, char *buf)
{
	struct cpufreq_stats *stat = per_cpu(cpufreq_stats_table, policy->cpu);
	if (!stat)
		return 0;
	return sprintf(buf, "%d\n",
			per_cpu(cpufreq_stats_table, stat->cpu)->total_trans);
}

static ssize_t
show_time_in_state(struct cpufreq_policy *policy, char *buf)
{
	ssize_t len = 0;
	int i;
	struct cpufreq_stats *stat = per_cpu(cpufreq_stats_table, policy->cpu);
	if (!stat)
		return 0;
	cpufreq_stats_update(stat->cpu);
	for (i = 0; i < stat->state_num; i++) {
		len += sprintf(buf + len, "%u %llu\n", stat->freq_table[i],
			(unsigned long long)cputime64_to_clock_t(stat->time_in_state[i]));
static ssize_t show_total_trans(struct cpufreq_policy *policy, char *buf)
{
	return sprintf(buf, "%d\n", policy->stats->total_trans);
}

static ssize_t show_time_in_state(struct cpufreq_policy *policy, char *buf)
{
	struct cpufreq_stats *stats = policy->stats;
	ssize_t len = 0;
	int i;

	cpufreq_stats_update(stats);
	for (i = 0; i < stats->state_num; i++) {
		len += sprintf(buf + len, "%u %llu\n", stats->freq_table[i],
			(unsigned long long)
			jiffies_64_to_clock_t(stats->time_in_state[i]));
	}
	return len;
}

#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
static ssize_t
show_trans_table(struct cpufreq_policy *policy, char *buf)
{
	ssize_t len = 0;
	int i, j;

	struct cpufreq_stats *stat = per_cpu(cpufreq_stats_table, policy->cpu);
	if (!stat)
		return 0;
	cpufreq_stats_update(stat->cpu);
	len += snprintf(buf + len, PAGE_SIZE - len, "   From  :    To\n");
	len += snprintf(buf + len, PAGE_SIZE - len, "         : ");
	for (i = 0; i < stat->state_num; i++) {
		if (len >= PAGE_SIZE)
			break;
		len += snprintf(buf + len, PAGE_SIZE - len, "%9u ",
				stat->freq_table[i]);
static ssize_t show_trans_table(struct cpufreq_policy *policy, char *buf)
{
	struct cpufreq_stats *stats = policy->stats;
	ssize_t len = 0;
	int i, j;

	len += snprintf(buf + len, PAGE_SIZE - len, "   From  :    To\n");
	len += snprintf(buf + len, PAGE_SIZE - len, "         : ");
	for (i = 0; i < stats->state_num; i++) {
		if (len >= PAGE_SIZE)
			break;
		len += snprintf(buf + len, PAGE_SIZE - len, "%9u ",
				stats->freq_table[i]);
	}
	if (len >= PAGE_SIZE)
		return PAGE_SIZE;

	len += snprintf(buf + len, PAGE_SIZE - len, "\n");

	for (i = 0; i < stat->state_num; i++) {
	for (i = 0; i < stats->state_num; i++) {
		if (len >= PAGE_SIZE)
			break;

		len += snprintf(buf + len, PAGE_SIZE - len, "%9u: ",
				stat->freq_table[i]);

		for (j = 0; j < stat->state_num; j++)   {
			if (len >= PAGE_SIZE)
				break;
			len += snprintf(buf + len, PAGE_SIZE - len, "%9u ",
					stat->trans_table[i*stat->max_state+j]);
				stats->freq_table[i]);

		for (j = 0; j < stats->state_num; j++) {
			if (len >= PAGE_SIZE)
				break;
			len += snprintf(buf + len, PAGE_SIZE - len, "%9u ",
					stats->trans_table[i*stats->max_state+j]);
		}
		if (len >= PAGE_SIZE)
			break;
		len += snprintf(buf + len, PAGE_SIZE - len, "\n");
	}
	if (len >= PAGE_SIZE)
		return PAGE_SIZE;
	return len;
}
CPUFREQ_STATDEVICE_ATTR(trans_table,0444,show_trans_table);
#endif

CPUFREQ_STATDEVICE_ATTR(total_trans,0444,show_total_trans);
CPUFREQ_STATDEVICE_ATTR(time_in_state,0444,show_time_in_state);

static struct attribute *default_attrs[] = {
	&_attr_total_trans.attr,
	&_attr_time_in_state.attr,
#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
	&_attr_trans_table.attr,
cpufreq_freq_attr_ro(trans_table);
#endif

cpufreq_freq_attr_ro(total_trans);
cpufreq_freq_attr_ro(time_in_state);

static struct attribute *default_attrs[] = {
	&total_trans.attr,
	&time_in_state.attr,
#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
	&trans_table.attr,
#endif
	NULL
};
static struct attribute_group stats_attr_group = {
	.attrs = default_attrs,
	.name = "stats"
};

static int
freq_table_get_index(struct cpufreq_stats *stat, unsigned int freq)
{
	int index;
	for (index = 0; index < stat->max_state; index++)
		if (stat->freq_table[index] == freq)
static int freq_table_get_index(struct cpufreq_stats *stats, unsigned int freq)
{
	int index;
	for (index = 0; index < stats->max_state; index++)
		if (stats->freq_table[index] == freq)
			return index;
	return -1;
}

static void cpufreq_stats_free_table(unsigned int cpu)
{
	struct cpufreq_stats *stat = per_cpu(cpufreq_stats_table, cpu);
	struct cpufreq_policy *policy = cpufreq_cpu_get(cpu);
	if (policy && policy->cpu == cpu)
		sysfs_remove_group(&policy->kobj, &stats_attr_group);
	if (stat) {
		kfree(stat->time_in_state);
		kfree(stat);
	}
	per_cpu(cpufreq_stats_table, cpu) = NULL;
	if (policy)
		cpufreq_cpu_put(policy);
}

static int
cpufreq_stats_create_table (struct cpufreq_policy *policy,
		struct cpufreq_frequency_table *table)
{
	unsigned int i, j, count = 0, ret = 0;
	struct cpufreq_stats *stat;
	struct cpufreq_policy *data;
	unsigned int alloc_size;
	unsigned int cpu = policy->cpu;
	if (per_cpu(cpufreq_stats_table, cpu))
		return -EBUSY;
	if ((stat = kzalloc(sizeof(struct cpufreq_stats), GFP_KERNEL)) == NULL)
		return -ENOMEM;

	data = cpufreq_cpu_get(cpu);
	if (data == NULL) {
		ret = -EINVAL;
		goto error_get_fail;
	}

	if ((ret = sysfs_create_group(&data->kobj, &stats_attr_group)))
		goto error_out;

	stat->cpu = cpu;
	per_cpu(cpufreq_stats_table, cpu) = stat;

	for (i=0; table[i].frequency != CPUFREQ_TABLE_END; i++) {
		unsigned int freq = table[i].frequency;
		if (freq == CPUFREQ_ENTRY_INVALID)
			continue;
		count++;
	}

	alloc_size = count * sizeof(int) + count * sizeof(cputime64_t);
static void __cpufreq_stats_free_table(struct cpufreq_policy *policy)
{
	struct cpufreq_stats *stats = policy->stats;

	/* Already freed */
	if (!stats)
		return;

	pr_debug("%s: Free stats table\n", __func__);

	sysfs_remove_group(&policy->kobj, &stats_attr_group);
	kfree(stats->time_in_state);
	kfree(stats);
	policy->stats = NULL;
}

static void cpufreq_stats_free_table(unsigned int cpu)
{
	struct cpufreq_policy *policy;

	policy = cpufreq_cpu_get(cpu);
	if (!policy)
		return;

	__cpufreq_stats_free_table(policy);

	cpufreq_cpu_put(policy);
}

static int __cpufreq_stats_create_table(struct cpufreq_policy *policy)
{
	unsigned int i = 0, count = 0, ret = -ENOMEM;
	struct cpufreq_stats *stats;
	unsigned int alloc_size;
	unsigned int cpu = policy->cpu;
	struct cpufreq_frequency_table *pos, *table;

	/* We need cpufreq table for creating stats table */
	table = cpufreq_frequency_get_table(cpu);
	if (unlikely(!table))
		return 0;

	/* stats already initialized */
	if (policy->stats)
		return -EEXIST;

	stats = kzalloc(sizeof(*stats), GFP_KERNEL);
	if (!stats)
		return -ENOMEM;

	/* Find total allocation size */
	cpufreq_for_each_valid_entry(pos, table)
		count++;

	alloc_size = count * sizeof(int) + count * sizeof(u64);

#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
	alloc_size += count * count * sizeof(int);
#endif
	stat->max_state = count;
	stat->time_in_state = kzalloc(alloc_size, GFP_KERNEL);
	if (!stat->time_in_state) {
		ret = -ENOMEM;
		goto error_out;
	}
	stat->freq_table = (unsigned int *)(stat->time_in_state + count);

#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
	stat->trans_table = stat->freq_table + count;
#endif
	j = 0;
	for (i = 0; table[i].frequency != CPUFREQ_TABLE_END; i++) {
		unsigned int freq = table[i].frequency;
		if (freq == CPUFREQ_ENTRY_INVALID)
			continue;
		if (freq_table_get_index(stat, freq) == -1)
			stat->freq_table[j++] = freq;
	}
	stat->state_num = j;
	spin_lock(&cpufreq_stats_lock);
	stat->last_time = get_jiffies_64();
	stat->last_index = freq_table_get_index(stat, policy->cur);
	spin_unlock(&cpufreq_stats_lock);
	cpufreq_cpu_put(data);
	return 0;
error_out:
	cpufreq_cpu_put(data);
error_get_fail:
	kfree(stat);
	per_cpu(cpufreq_stats_table, cpu) = NULL;
	return ret;
}

static int
cpufreq_stat_notifier_policy (struct notifier_block *nb, unsigned long val,
		void *data)
{
	int ret;
	struct cpufreq_policy *policy = data;
	struct cpufreq_frequency_table *table;
	unsigned int cpu = policy->cpu;
	if (val != CPUFREQ_NOTIFY)
		return 0;
	table = cpufreq_frequency_get_table(cpu);
	if (!table)
		return 0;
	if ((ret = cpufreq_stats_create_table(policy, table)))
		return ret;
	return 0;
}

static int
cpufreq_stat_notifier_trans (struct notifier_block *nb, unsigned long val,
		void *data)
{
	struct cpufreq_freqs *freq = data;
	struct cpufreq_stats *stat;
	int old_index, new_index;

	if (val != CPUFREQ_POSTCHANGE)
		return 0;

	stat = per_cpu(cpufreq_stats_table, freq->cpu);
	if (!stat)
		return 0;

	old_index = stat->last_index;
	new_index = freq_table_get_index(stat, freq->new);

	cpufreq_stats_update(freq->cpu);
	if (old_index == new_index)
		return 0;

	if (old_index == -1 || new_index == -1)
		return 0;

	spin_lock(&cpufreq_stats_lock);
	stat->last_index = new_index;
#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
	stat->trans_table[old_index * stat->max_state + new_index]++;
#endif
	stat->total_trans++;
	spin_unlock(&cpufreq_stats_lock);
	return 0;
}

static int __cpuinit cpufreq_stat_cpu_callback(struct notifier_block *nfb,
					       unsigned long action,
					       void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;

	switch (action) {
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		cpufreq_update_policy(cpu);
		break;
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		cpufreq_stats_free_table(cpu);
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block cpufreq_stat_cpu_notifier __refdata =
{
	.notifier_call = cpufreq_stat_cpu_callback,
};


	/* Allocate memory for time_in_state/freq_table/trans_table in one go */
	stats->time_in_state = kzalloc(alloc_size, GFP_KERNEL);
	if (!stats->time_in_state)
		goto free_stat;

	stats->freq_table = (unsigned int *)(stats->time_in_state + count);

#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
	stats->trans_table = stats->freq_table + count;
#endif

	stats->max_state = count;

	/* Find valid-unique entries */
	cpufreq_for_each_valid_entry(pos, table)
		if (freq_table_get_index(stats, pos->frequency) == -1)
			stats->freq_table[i++] = pos->frequency;

	stats->state_num = i;
	stats->last_time = get_jiffies_64();
	stats->last_index = freq_table_get_index(stats, policy->cur);

	policy->stats = stats;
	ret = sysfs_create_group(&policy->kobj, &stats_attr_group);
	if (!ret)
		return 0;

	/* We failed, release resources */
	policy->stats = NULL;
	kfree(stats->time_in_state);
free_stat:
	kfree(stats);

	return ret;
}

static void cpufreq_stats_create_table(unsigned int cpu)
{
	struct cpufreq_policy *policy;

	/*
	 * "likely(!policy)" because normally cpufreq_stats will be registered
	 * before cpufreq driver
	 */
	policy = cpufreq_cpu_get(cpu);
	if (likely(!policy))
		return;

	__cpufreq_stats_create_table(policy);

	cpufreq_cpu_put(policy);
}

static int cpufreq_stat_notifier_policy(struct notifier_block *nb,
		unsigned long val, void *data)
{
	int ret = 0;
	struct cpufreq_policy *policy = data;

	if (val == CPUFREQ_CREATE_POLICY)
		ret = __cpufreq_stats_create_table(policy);
	else if (val == CPUFREQ_REMOVE_POLICY)
		__cpufreq_stats_free_table(policy);

	return ret;
}

static int cpufreq_stat_notifier_trans(struct notifier_block *nb,
		unsigned long val, void *data)
{
	struct cpufreq_freqs *freq = data;
	struct cpufreq_policy *policy = cpufreq_cpu_get(freq->cpu);
	struct cpufreq_stats *stats;
	int old_index, new_index;

	if (!policy) {
		pr_err("%s: No policy found\n", __func__);
		return 0;
	}

	if (val != CPUFREQ_POSTCHANGE)
		goto put_policy;

	if (!policy->stats) {
		pr_debug("%s: No stats found\n", __func__);
		goto put_policy;
	}

	stats = policy->stats;

	old_index = stats->last_index;
	new_index = freq_table_get_index(stats, freq->new);

	/* We can't do stats->time_in_state[-1]= .. */
	if (old_index == -1 || new_index == -1)
		goto put_policy;

	if (old_index == new_index)
		goto put_policy;

	cpufreq_stats_update(stats);

	stats->last_index = new_index;
#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
	stats->trans_table[old_index * stats->max_state + new_index]++;
#endif
	stats->total_trans++;

put_policy:
	cpufreq_cpu_put(policy);
	return 0;
}

static struct notifier_block notifier_policy_block = {
	.notifier_call = cpufreq_stat_notifier_policy
};

static struct notifier_block notifier_trans_block = {
	.notifier_call = cpufreq_stat_notifier_trans
};

static int
__init cpufreq_stats_init(void)
static int __init cpufreq_stats_init(void)
{
	int ret;
	unsigned int cpu;

	spin_lock_init(&cpufreq_stats_lock);
	if ((ret = cpufreq_register_notifier(&notifier_policy_block,
				CPUFREQ_POLICY_NOTIFIER)))
		return ret;

	if ((ret = cpufreq_register_notifier(&notifier_trans_block,
				CPUFREQ_TRANSITION_NOTIFIER))) {
		cpufreq_unregister_notifier(&notifier_policy_block,
				CPUFREQ_POLICY_NOTIFIER);
		return ret;
	}

	register_hotcpu_notifier(&cpufreq_stat_cpu_notifier);
	for_each_online_cpu(cpu) {
		cpufreq_update_policy(cpu);
	}
	return 0;
}
static void
__exit cpufreq_stats_exit(void)
	ret = cpufreq_register_notifier(&notifier_policy_block,
				CPUFREQ_POLICY_NOTIFIER);
	if (ret)
		return ret;

	for_each_online_cpu(cpu)
		cpufreq_stats_create_table(cpu);

	ret = cpufreq_register_notifier(&notifier_trans_block,
				CPUFREQ_TRANSITION_NOTIFIER);
	if (ret) {
		cpufreq_unregister_notifier(&notifier_policy_block,
				CPUFREQ_POLICY_NOTIFIER);
		for_each_online_cpu(cpu)
			cpufreq_stats_free_table(cpu);
		return ret;
	}

	return 0;
}
static void __exit cpufreq_stats_exit(void)
{
	unsigned int cpu;

	cpufreq_unregister_notifier(&notifier_policy_block,
			CPUFREQ_POLICY_NOTIFIER);
	cpufreq_unregister_notifier(&notifier_trans_block,
			CPUFREQ_TRANSITION_NOTIFIER);
	unregister_hotcpu_notifier(&cpufreq_stat_cpu_notifier);
	for_each_online_cpu(cpu) {
		cpufreq_stats_free_table(cpu);
	}
}

MODULE_AUTHOR ("Zou Nan hai <nanhai.zou@intel.com>");
MODULE_DESCRIPTION ("'cpufreq_stats' - A driver to export cpufreq stats "
				"through sysfs filesystem");
MODULE_LICENSE ("GPL");
	for_each_online_cpu(cpu)
		cpufreq_stats_free_table(cpu);
}

MODULE_AUTHOR("Zou Nan hai <nanhai.zou@intel.com>");
MODULE_DESCRIPTION("Export cpufreq stats via sysfs");
MODULE_LICENSE("GPL");

module_init(cpufreq_stats_init);
module_exit(cpufreq_stats_exit);
