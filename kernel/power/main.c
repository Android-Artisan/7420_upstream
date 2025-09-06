/*
 * kernel/power/main.c - PM subsystem core functionality.
 *
 * Copyright (c) 2003 Patrick Mochel
 * Copyright (c) 2003 Open Source Development Lab
 * 
 *
 * This file is released under the GPLv2
 *
 */

#include <linux/module.h>
#include <linux/suspend.h>
#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/cpu.h>
#include <linux/resume-trace.h>
#include <linux/freezer.h>
#include <linux/vmstat.h>
#include <linux/syscalls.h>
#include <linux/ftrace.h>
#include <linux/export.h>
#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/pm-trace.h>
#include <linux/workqueue.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include "power.h"

DEFINE_MUTEX(pm_mutex);

unsigned int pm_flags;
EXPORT_SYMBOL(pm_flags);

#ifdef CONFIG_PM_SLEEP

/* Routines for PM-transition notifications */

static BLOCKING_NOTIFIER_HEAD(pm_chain_head);

int register_pm_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&pm_chain_head, nb);
}
EXPORT_SYMBOL_GPL(register_pm_notifier);

int unregister_pm_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&pm_chain_head, nb);
}
EXPORT_SYMBOL_GPL(unregister_pm_notifier);

int pm_notifier_call_chain(unsigned long val)
{
	return (blocking_notifier_call_chain(&pm_chain_head, val, NULL)
			== NOTIFY_BAD) ? -EINVAL : 0;
}

#ifdef CONFIG_PM_DEBUG
int pm_test_level = TEST_NONE;

static int suspend_test(int level)
{
	if (pm_test_level == level) {
		printk(KERN_INFO "suspend debug: Waiting for 5 seconds.\n");
		mdelay(5000);
		return 1;
	}
	return 0;
}

	int ret = blocking_notifier_call_chain(&pm_chain_head, val, NULL);

	return notifier_to_errno(ret);
}

/* If set, devices may be suspended and resumed asynchronously. */
int pm_async_enabled = 1;

static ssize_t pm_async_show(struct kobject *kobj, struct kobj_attribute *attr,
			     char *buf)
{
	return sprintf(buf, "%d\n", pm_async_enabled);
}

static ssize_t pm_async_store(struct kobject *kobj, struct kobj_attribute *attr,
			      const char *buf, size_t n)
{
	unsigned long val;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	if (val > 1)
		return -EINVAL;

	pm_async_enabled = val;
	return n;
}

power_attr(pm_async);

#ifdef CONFIG_PM_DEBUG
int pm_test_level = TEST_NONE;

static const char * const pm_tests[__TEST_AFTER_LAST] = {
	[TEST_NONE] = "none",
	[TEST_CORE] = "core",
	[TEST_CPUS] = "processors",
	[TEST_PLATFORM] = "platform",
	[TEST_DEVICES] = "devices",
	[TEST_FREEZER] = "freezer",
};

static ssize_t pm_test_show(struct kobject *kobj, struct kobj_attribute *attr,
				char *buf)
{
	char *s = buf;
	int level;

	for (level = TEST_FIRST; level <= TEST_MAX; level++)
		if (pm_tests[level]) {
			if (level == pm_test_level)
				s += sprintf(s, "[%s] ", pm_tests[level]);
			else
				s += sprintf(s, "%s ", pm_tests[level]);
		}

	if (s != buf)
		/* convert the last space to a newline */
		*(s-1) = '\n';

	return (s - buf);
}

static ssize_t pm_test_store(struct kobject *kobj, struct kobj_attribute *attr,
				const char *buf, size_t n)
{
	const char * const *s;
	int level;
	char *p;
	int len;
	int error = -EINVAL;

	p = memchr(buf, '\n', n);
	len = p ? p - buf : n;

	mutex_lock(&pm_mutex);
	lock_system_sleep();

	level = TEST_FIRST;
	for (s = &pm_tests[level]; level <= TEST_MAX; s++, level++)
		if (*s && len == strlen(*s) && !strncmp(buf, *s, len)) {
			pm_test_level = level;
			error = 0;
			break;
		}

	mutex_unlock(&pm_mutex);
	unlock_system_sleep();

	return error ? error : n;
}

power_attr(pm_test);
#else /* !CONFIG_PM_DEBUG */
static inline int suspend_test(int level) { return 0; }
#endif /* !CONFIG_PM_DEBUG */

#endif /* CONFIG_PM_SLEEP */

#ifdef CONFIG_SUSPEND

#ifdef CONFIG_PM_TEST_SUSPEND

/*
 * We test the system suspend code by setting an RTC wakealarm a short
 * time in the future, then suspending.  Suspending the devices won't
 * normally take long ... some systems only need a few milliseconds.
 *
 * The time it takes is system-specific though, so when we test this
 * during system bootup we allow a LOT of time.
 */
#define TEST_SUSPEND_SECONDS	5

static unsigned long suspend_test_start_time;

static void suspend_test_start(void)
{
	/* FIXME Use better timebase than "jiffies", ideally a clocksource.
	 * What we want is a hardware counter that will work correctly even
	 * during the irqs-are-off stages of the suspend/resume cycle...
	 */
	suspend_test_start_time = jiffies;
}

static void suspend_test_finish(const char *label)
{
	long nj = jiffies - suspend_test_start_time;
	unsigned msec;

	msec = jiffies_to_msecs(abs(nj));
	pr_info("PM: %s took %d.%03d seconds\n", label,
			msec / 1000, msec % 1000);

	/* Warning on suspend means the RTC alarm period needs to be
	 * larger -- the system was sooo slooowwww to suspend that the
	 * alarm (should have) fired before the system went to sleep!
	 *
	 * Warning on either suspend or resume also means the system
	 * has some performance issues.  The stack dump of a WARN_ON
	 * is more likely to get the right attention than a printk...
	 */
	WARN_ON(msec > (TEST_SUSPEND_SECONDS * 1000));
}

#else

static void suspend_test_start(void)
{
}

static void suspend_test_finish(const char *label)
{
}

#endif

/* This is just an arbitrary number */
#define FREE_PAGE_NUMBER (100)

static struct platform_suspend_ops *suspend_ops;

/**
 *	suspend_set_ops - Set the global suspend method table.
 *	@ops:	Pointer to ops structure.
 */

void suspend_set_ops(struct platform_suspend_ops *ops)
{
	mutex_lock(&pm_mutex);
	suspend_ops = ops;
	mutex_unlock(&pm_mutex);
}

/**
 * suspend_valid_only_mem - generic memory-only valid callback
 *
 * Platform drivers that implement mem suspend only and only need
 * to check for that in their .valid callback can use this instead
 * of rolling their own .valid callback.
 */
int suspend_valid_only_mem(suspend_state_t state)
{
	return state == PM_SUSPEND_MEM;
}

/**
 *	suspend_prepare - Do prep work before entering low-power state.
 *
 *	This is common code that is called for each state that we're entering.
 *	Run suspend notifiers, allocate a console and stop all processes.
 */
static int suspend_prepare(void)
{
	int error;
	unsigned int free_pages;

	if (!suspend_ops || !suspend_ops->enter)
		return -EPERM;

	pm_prepare_console();

	error = pm_notifier_call_chain(PM_SUSPEND_PREPARE);
	if (error)
		goto Finish;

	if (suspend_freeze_processes()) {
		error = -EAGAIN;
		goto Thaw;
	}

	free_pages = global_page_state(NR_FREE_PAGES);
	if (free_pages < FREE_PAGE_NUMBER) {
		pr_debug("PM: free some memory\n");
		shrink_all_memory(FREE_PAGE_NUMBER - free_pages);
		if (nr_free_pages() < FREE_PAGE_NUMBER) {
			error = -ENOMEM;
			printk(KERN_ERR "PM: No enough memory\n");
		}
	}
	if (!error)
		return 0;

 Thaw:
	suspend_thaw_processes();
 Finish:
	pm_notifier_call_chain(PM_POST_SUSPEND);
	pm_restore_console();
	return error;
}

/* default implementation */
void __attribute__ ((weak)) arch_suspend_disable_irqs(void)
{
	local_irq_disable();
}

/* default implementation */
void __attribute__ ((weak)) arch_suspend_enable_irqs(void)
{
	local_irq_enable();
}

/**
 *	suspend_enter - enter the desired system sleep state.
 *	@state:		state to enter
 *
 *	This function should be called after devices have been suspended.
 */
static int suspend_enter(suspend_state_t state)
{
	int error = 0;

	device_pm_lock();
	arch_suspend_disable_irqs();
	BUG_ON(!irqs_disabled());

	if ((error = device_power_down(PMSG_SUSPEND))) {
		printk(KERN_ERR "PM: Some devices failed to power down\n");
		goto Done;
	}

	if (!suspend_test(TEST_CORE))
		error = suspend_ops->enter(state);

	device_power_up(PMSG_RESUME);
 Done:
	arch_suspend_enable_irqs();
	BUG_ON(irqs_disabled());
	device_pm_unlock();
	return error;
}

/**
 *	suspend_devices_and_enter - suspend devices and enter the desired system
 *				    sleep state.
 *	@state:		  state to enter
 */
int suspend_devices_and_enter(suspend_state_t state)
{
	int error, ftrace_save;

	if (!suspend_ops)
		return -ENOSYS;

	if (suspend_ops->begin) {
		error = suspend_ops->begin(state);
		if (error)
			goto Close;
	}
	suspend_console();
	ftrace_save = __ftrace_enabled_save();
	suspend_test_start();
	error = device_suspend(PMSG_SUSPEND);
	if (error) {
		printk(KERN_ERR "PM: Some devices failed to suspend\n");
		goto Recover_platform;
	}
	suspend_test_finish("suspend devices");
	if (suspend_test(TEST_DEVICES))
		goto Recover_platform;

	if (suspend_ops->prepare) {
		error = suspend_ops->prepare();
		if (error)
			goto Resume_devices;
	}

	if (suspend_test(TEST_PLATFORM))
		goto Finish;

	error = disable_nonboot_cpus();
	if (!error && !suspend_test(TEST_CPUS))
		suspend_enter(state);

	enable_nonboot_cpus();
 Finish:
	if (suspend_ops->finish)
		suspend_ops->finish();
 Resume_devices:
	suspend_test_start();
	device_resume(PMSG_RESUME);
	suspend_test_finish("resume devices");
	__ftrace_enabled_restore(ftrace_save);
	resume_console();
 Close:
	if (suspend_ops->end)
		suspend_ops->end();
	return error;

 Recover_platform:
	if (suspend_ops->recover)
		suspend_ops->recover();
	goto Resume_devices;
}

/**
 *	suspend_finish - Do final work before exiting suspend sequence.
 *
 *	Call platform code to clean up, restart processes, and free the 
 *	console that we've allocated. This is not called for suspend-to-disk.
 */
static void suspend_finish(void)
{
	suspend_thaw_processes();
	pm_notifier_call_chain(PM_POST_SUSPEND);
	pm_restore_console();
}




static const char * const pm_states[PM_SUSPEND_MAX] = {
	[PM_SUSPEND_STANDBY]	= "standby",
	[PM_SUSPEND_MEM]	= "mem",
};

static inline int valid_state(suspend_state_t state)
{
	/* All states need lowlevel support and need to be valid
	 * to the lowlevel implementation, no valid callback
	 * implies that none are valid. */
	if (!suspend_ops || !suspend_ops->valid || !suspend_ops->valid(state))
		return 0;
	return 1;
}


/**
 *	enter_state - Do common work of entering low-power state.
 *	@state:		pm_state structure for state we're entering.
 *
 *	Make sure we're the only ones trying to enter a sleep state. Fail
 *	if someone has beat us to it, since we don't want anything weird to
 *	happen when we wake up.
 *	Then, do the setup for suspend, enter the state, and cleaup (after
 *	we've woken up).
 */
static int enter_state(suspend_state_t state)
{
	int error;

	if (!valid_state(state))
		return -ENODEV;

	if (!mutex_trylock(&pm_mutex))
		return -EBUSY;

	printk(KERN_INFO "PM: Syncing filesystems ... ");
	sys_sync();
	printk("done.\n");

	pr_debug("PM: Preparing system for %s sleep\n", pm_states[state]);
	error = suspend_prepare();
	if (error)
		goto Unlock;

	if (suspend_test(TEST_FREEZER))
		goto Finish;

	pr_debug("PM: Entering %s sleep\n", pm_states[state]);
	error = suspend_devices_and_enter(state);

 Finish:
	pr_debug("PM: Finishing wakeup.\n");
	suspend_finish();
 Unlock:
	mutex_unlock(&pm_mutex);
	return error;
}


/**
 *	pm_suspend - Externally visible function for suspending system.
 *	@state:		Enumerated value of state to enter.
 *
 *	Determine whether or not value is within range, get state 
 *	structure, and enter (above).
 */

int pm_suspend(suspend_state_t state)
{
	if (state > PM_SUSPEND_ON && state <= PM_SUSPEND_MAX)
		return enter_state(state);
	return -EINVAL;
}

EXPORT_SYMBOL(pm_suspend);

#endif /* CONFIG_SUSPEND */
#endif /* CONFIG_PM_DEBUG */

#ifdef CONFIG_DEBUG_FS
static char *suspend_step_name(enum suspend_stat_step step)
{
	switch (step) {
	case SUSPEND_FREEZE:
		return "freeze";
	case SUSPEND_PREPARE:
		return "prepare";
	case SUSPEND_SUSPEND:
		return "suspend";
	case SUSPEND_SUSPEND_NOIRQ:
		return "suspend_noirq";
	case SUSPEND_RESUME_NOIRQ:
		return "resume_noirq";
	case SUSPEND_RESUME:
		return "resume";
	default:
		return "";
	}
}

static int suspend_stats_show(struct seq_file *s, void *unused)
{
	int i, index, last_dev, last_errno, last_step;

	last_dev = suspend_stats.last_failed_dev + REC_FAILED_NUM - 1;
	last_dev %= REC_FAILED_NUM;
	last_errno = suspend_stats.last_failed_errno + REC_FAILED_NUM - 1;
	last_errno %= REC_FAILED_NUM;
	last_step = suspend_stats.last_failed_step + REC_FAILED_NUM - 1;
	last_step %= REC_FAILED_NUM;
	seq_printf(s, "%s: %d\n%s: %d\n%s: %d\n%s: %d\n%s: %d\n"
			"%s: %d\n%s: %d\n%s: %d\n%s: %d\n%s: %d\n",
			"success", suspend_stats.success,
			"fail", suspend_stats.fail,
			"failed_freeze", suspend_stats.failed_freeze,
			"failed_prepare", suspend_stats.failed_prepare,
			"failed_suspend", suspend_stats.failed_suspend,
			"failed_suspend_late",
				suspend_stats.failed_suspend_late,
			"failed_suspend_noirq",
				suspend_stats.failed_suspend_noirq,
			"failed_resume", suspend_stats.failed_resume,
			"failed_resume_early",
				suspend_stats.failed_resume_early,
			"failed_resume_noirq",
				suspend_stats.failed_resume_noirq);
	seq_printf(s,	"failures:\n  last_failed_dev:\t%-s\n",
			suspend_stats.failed_devs[last_dev]);
	for (i = 1; i < REC_FAILED_NUM; i++) {
		index = last_dev + REC_FAILED_NUM - i;
		index %= REC_FAILED_NUM;
		seq_printf(s, "\t\t\t%-s\n",
			suspend_stats.failed_devs[index]);
	}
	seq_printf(s,	"  last_failed_errno:\t%-d\n",
			suspend_stats.errno[last_errno]);
	for (i = 1; i < REC_FAILED_NUM; i++) {
		index = last_errno + REC_FAILED_NUM - i;
		index %= REC_FAILED_NUM;
		seq_printf(s, "\t\t\t%-d\n",
			suspend_stats.errno[index]);
	}
	seq_printf(s,	"  last_failed_step:\t%-s\n",
			suspend_step_name(
				suspend_stats.failed_steps[last_step]));
	for (i = 1; i < REC_FAILED_NUM; i++) {
		index = last_step + REC_FAILED_NUM - i;
		index %= REC_FAILED_NUM;
		seq_printf(s, "\t\t\t%-s\n",
			suspend_step_name(
				suspend_stats.failed_steps[index]));
	}

	return 0;
}

static int suspend_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, suspend_stats_show, NULL);
}

static const struct file_operations suspend_stats_operations = {
	.open           = suspend_stats_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = single_release,
};

static int __init pm_debugfs_init(void)
{
	debugfs_create_file("suspend_stats", S_IFREG | S_IRUGO,
			NULL, NULL, &suspend_stats_operations);
	return 0;
}

late_initcall(pm_debugfs_init);
#endif /* CONFIG_DEBUG_FS */

#endif /* CONFIG_PM_SLEEP */

#ifdef CONFIG_PM_SLEEP_DEBUG
/*
 * pm_print_times: print time taken by devices to suspend and resume.
 *
 * show() returns whether printing of suspend and resume times is enabled.
 * store() accepts 0 or 1.  0 disables printing and 1 enables it.
 */
bool pm_print_times_enabled;

static ssize_t pm_print_times_show(struct kobject *kobj,
				   struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", pm_print_times_enabled);
}

static ssize_t pm_print_times_store(struct kobject *kobj,
				    struct kobj_attribute *attr,
				    const char *buf, size_t n)
{
	unsigned long val;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	if (val > 1)
		return -EINVAL;

	pm_print_times_enabled = !!val;
	return n;
}

power_attr(pm_print_times);

static inline void pm_print_times_init(void)
{
	pm_print_times_enabled = !!initcall_debug;
}

static ssize_t pm_wakeup_irq_show(struct kobject *kobj,
					struct kobj_attribute *attr,
					char *buf)
{
	return pm_wakeup_irq ? sprintf(buf, "%u\n", pm_wakeup_irq) : -ENODATA;
}

static ssize_t pm_wakeup_irq_store(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t n)
{
	return -EINVAL;
}
power_attr(pm_wakeup_irq);

#else /* !CONFIG_PM_SLEEP_DEBUG */
static inline void pm_print_times_init(void) {}
#endif /* CONFIG_PM_SLEEP_DEBUG */

struct kobject *power_kobj;

/**
 *	state - control system power state.
 *
 *	show() returns what states are supported, which is hard-coded to
 *	'standby' (Power-On Suspend), 'mem' (Suspend-to-RAM), and
 *	'disk' (Suspend-to-Disk).
 *
 *	store() accepts one of those strings, translates it into the 
 *	proper enumerated value, and initiates a suspend transition.
 */

 * state - control system sleep states.
 *
 * show() returns available sleep state labels, which may be "mem", "standby",
 * "freeze" and "disk" (hibernation).  See Documentation/power/states.txt for a
 * description of what they mean.
 *
 * store() accepts one of those strings, translates it into the proper
 * enumerated value, and initiates a suspend transition.
 */
static ssize_t state_show(struct kobject *kobj, struct kobj_attribute *attr,
			  char *buf)
{
	char *s = buf;
#ifdef CONFIG_SUSPEND
	int i;

	for (i = 0; i < PM_SUSPEND_MAX; i++) {
		if (pm_states[i] && valid_state(i))
			s += sprintf(s,"%s ", pm_states[i]);
	}
#endif
#ifdef CONFIG_HIBERNATION
	s += sprintf(s, "%s\n", "disk");
#else
	if (s != buf)
		/* convert the last space to a newline */
		*(s-1) = '\n';
#endif
	return (s - buf);
}

static ssize_t state_store(struct kobject *kobj, struct kobj_attribute *attr,
			   const char *buf, size_t n)
{
#ifdef CONFIG_SUSPEND
	suspend_state_t state = PM_SUSPEND_STANDBY;
	const char * const *s;
#endif
	char *p;
	int len;
	int error = -EINVAL;

	p = memchr(buf, '\n', n);
	len = p ? p - buf : n;

	/* First, check if we are requested to hibernate */
	if (len == 4 && !strncmp(buf, "disk", len)) {
		error = hibernate();
  goto Exit;
	}

#ifdef CONFIG_SUSPEND
	for (s = &pm_states[state]; state < PM_SUSPEND_MAX; s++, state++) {
		if (*s && len == strlen(*s) && !strncmp(buf, *s, len))
			break;
	}
	if (state < PM_SUSPEND_MAX && *s)
		error = enter_state(state);
#endif

 Exit:
	suspend_state_t i;

	for (i = PM_SUSPEND_MIN; i < PM_SUSPEND_MAX; i++)
		if (pm_states[i])
			s += sprintf(s,"%s ", pm_states[i]);

#endif
	if (hibernation_available())
		s += sprintf(s, "disk ");
	if (s != buf)
		/* convert the last space to a newline */
		*(s-1) = '\n';
	return (s - buf);
}

static suspend_state_t decode_state(const char *buf, size_t n)
{
#ifdef CONFIG_SUSPEND
	suspend_state_t state;
#endif
	char *p;
	int len;

	p = memchr(buf, '\n', n);
	len = p ? p - buf : n;

	/* Check hibernation first. */
	if (len == 4 && !strncmp(buf, "disk", len))
		return PM_SUSPEND_MAX;

#ifdef CONFIG_SUSPEND
	for (state = PM_SUSPEND_MIN; state < PM_SUSPEND_MAX; state++) {
		const char *label = pm_states[state];

		if (label && len == strlen(label) && !strncmp(buf, label, len))
			return state;
	}
#endif

	return PM_SUSPEND_ON;
}

static ssize_t state_store(struct kobject *kobj, struct kobj_attribute *attr,
			   const char *buf, size_t n)
{
	suspend_state_t state;
	int error;

	error = pm_autosleep_lock();
	if (error)
		return error;

	if (pm_autosleep_state() > PM_SUSPEND_ON) {
		error = -EBUSY;
		goto out;
	}

	state = decode_state(buf, n);
	if (state < PM_SUSPEND_MAX)
		error = pm_suspend(state);
	else if (state == PM_SUSPEND_MAX)
		error = hibernate();
	else
		error = -EINVAL;

 out:
	pm_autosleep_unlock();
	return error ? error : n;
}

power_attr(state);

#ifdef CONFIG_PM_SLEEP
/*
 * The 'wakeup_count' attribute, along with the functions defined in
 * drivers/base/power/wakeup.c, provides a means by which wakeup events can be
 * handled in a non-racy way.
 *
 * If a wakeup event occurs when the system is in a sleep state, it simply is
 * woken up.  In turn, if an event that would wake the system up from a sleep
 * state occurs when it is undergoing a transition to that sleep state, the
 * transition should be aborted.  Moreover, if such an event occurs when the
 * system is in the working state, an attempt to start a transition to the
 * given sleep state should fail during certain period after the detection of
 * the event.  Using the 'state' attribute alone is not sufficient to satisfy
 * these requirements, because a wakeup event may occur exactly when 'state'
 * is being written to and may be delivered to user space right before it is
 * frozen, so the event will remain only partially processed until the system is
 * woken up by another event.  In particular, it won't cause the transition to
 * a sleep state to be aborted.
 *
 * This difficulty may be overcome if user space uses 'wakeup_count' before
 * writing to 'state'.  It first should read from 'wakeup_count' and store
 * the read value.  Then, after carrying out its own preparations for the system
 * transition to a sleep state, it should write the stored value to
 * 'wakeup_count'.  If that fails, at least one wakeup event has occurred since
 * 'wakeup_count' was read and 'state' should not be written to.  Otherwise, it
 * is allowed to write to 'state', but the transition will be aborted if there
 * are any wakeup events detected after 'wakeup_count' was written to.
 */

static ssize_t wakeup_count_show(struct kobject *kobj,
				struct kobj_attribute *attr,
				char *buf)
{
	unsigned int val;

	return pm_get_wakeup_count(&val, true) ?
		sprintf(buf, "%u\n", val) : -EINTR;
}

static ssize_t wakeup_count_store(struct kobject *kobj,
				struct kobj_attribute *attr,
				const char *buf, size_t n)
{
	unsigned int val;
	int error;

	error = pm_autosleep_lock();
	if (error)
		return error;

	if (pm_autosleep_state() > PM_SUSPEND_ON) {
		error = -EBUSY;
		goto out;
	}

	error = -EINVAL;
	if (sscanf(buf, "%u", &val) == 1) {
		if (pm_save_wakeup_count(val))
			error = n;
		else
			pm_print_active_wakeup_sources();
	}

 out:
	pm_autosleep_unlock();
	return error;
}

power_attr(wakeup_count);

#ifdef CONFIG_PM_AUTOSLEEP
static ssize_t autosleep_show(struct kobject *kobj,
			      struct kobj_attribute *attr,
			      char *buf)
{
	suspend_state_t state = pm_autosleep_state();

	if (state == PM_SUSPEND_ON)
		return sprintf(buf, "off\n");

#ifdef CONFIG_SUSPEND
	if (state < PM_SUSPEND_MAX)
		return sprintf(buf, "%s\n", pm_states[state] ?
					pm_states[state] : "error");
#endif
#ifdef CONFIG_HIBERNATION
	return sprintf(buf, "disk\n");
#else
	return sprintf(buf, "error");
#endif
}

static ssize_t autosleep_store(struct kobject *kobj,
			       struct kobj_attribute *attr,
			       const char *buf, size_t n)
{
	suspend_state_t state = decode_state(buf, n);
	int error;

	if (state == PM_SUSPEND_ON
	    && strcmp(buf, "off") && strcmp(buf, "off\n"))
		return -EINVAL;

	error = pm_autosleep_set_state(state);
	return error ? error : n;
}

power_attr(autosleep);
#endif /* CONFIG_PM_AUTOSLEEP */

#ifdef CONFIG_PM_WAKELOCKS
static ssize_t wake_lock_show(struct kobject *kobj,
			      struct kobj_attribute *attr,
			      char *buf)
{
	return pm_show_wakelocks(buf, true);
}

static ssize_t wake_lock_store(struct kobject *kobj,
			       struct kobj_attribute *attr,
			       const char *buf, size_t n)
{
	int error = pm_wake_lock(buf);
	return error ? error : n;
}

power_attr(wake_lock);

static ssize_t wake_unlock_show(struct kobject *kobj,
				struct kobj_attribute *attr,
				char *buf)
{
	return pm_show_wakelocks(buf, false);
}

static ssize_t wake_unlock_store(struct kobject *kobj,
				 struct kobj_attribute *attr,
				 const char *buf, size_t n)
{
	int error = pm_wake_unlock(buf);
	return error ? error : n;
}

power_attr(wake_unlock);

#endif /* CONFIG_PM_WAKELOCKS */
#endif /* CONFIG_PM_SLEEP */

#ifdef CONFIG_PM_TRACE
int pm_trace_enabled;

static ssize_t pm_trace_show(struct kobject *kobj, struct kobj_attribute *attr,
			     char *buf)
{
	return sprintf(buf, "%d\n", pm_trace_enabled);
}

static ssize_t
pm_trace_store(struct kobject *kobj, struct kobj_attribute *attr,
	       const char *buf, size_t n)
{
	int val;

	if (sscanf(buf, "%d", &val) == 1) {
		pm_trace_enabled = !!val;
		if (pm_trace_enabled) {
			pr_warn("PM: Enabling pm_trace changes system date and time during resume.\n"
				"PM: Correct system time has to be restored manually after resume.\n");
		}
		return n;
	}
	return -EINVAL;
}

power_attr(pm_trace);
#endif /* CONFIG_PM_TRACE */


static ssize_t pm_trace_dev_match_show(struct kobject *kobj,
				       struct kobj_attribute *attr,
				       char *buf)
{
	return show_trace_dev_match(buf, PAGE_SIZE);
}

static ssize_t
pm_trace_dev_match_store(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t n)
{
	return -EINVAL;
}

power_attr(pm_trace_dev_match);

#endif /* CONFIG_PM_TRACE */

#ifdef CONFIG_FREEZER
static ssize_t pm_freeze_timeout_show(struct kobject *kobj,
				      struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", freeze_timeout_msecs);
}

static ssize_t pm_freeze_timeout_store(struct kobject *kobj,
				       struct kobj_attribute *attr,
				       const char *buf, size_t n)
{
	unsigned long val;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	freeze_timeout_msecs = val;
	return n;
}

power_attr(pm_freeze_timeout);

#endif	/* CONFIG_FREEZER*/

static struct attribute * g[] = {
	&state_attr.attr,
#ifdef CONFIG_PM_TRACE
	&pm_trace_attr.attr,
#endif
#if defined(CONFIG_PM_SLEEP) && defined(CONFIG_PM_DEBUG)
	&pm_test_attr.attr,
#endif
	&pm_trace_dev_match_attr.attr,
#endif
#ifdef CONFIG_PM_SLEEP
	&pm_async_attr.attr,
	&wakeup_count_attr.attr,
#ifdef CONFIG_PM_AUTOSLEEP
	&autosleep_attr.attr,
#endif
#ifdef CONFIG_PM_WAKELOCKS
	&wake_lock_attr.attr,
	&wake_unlock_attr.attr,
#endif
#ifdef CONFIG_PM_DEBUG
	&pm_test_attr.attr,
#endif
#ifdef CONFIG_PM_SLEEP_DEBUG
	&pm_print_times_attr.attr,
	&pm_wakeup_irq_attr.attr,
#endif
#endif
#ifdef CONFIG_FREEZER
	&pm_freeze_timeout_attr.attr,
#endif
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = g,
};


static int __init pm_init(void)
{
	power_kobj = kobject_create_and_add("power", NULL);
	if (!power_kobj)
		return -ENOMEM;
	return sysfs_create_group(power_kobj, &attr_group);
}

core_initcall(pm_init);


#ifdef CONFIG_PM_TEST_SUSPEND

#include <linux/rtc.h>

/*
 * To test system suspend, we need a hands-off mechanism to resume the
 * system.  RTCs wake alarms are a common self-contained mechanism.
 */

static void __init test_wakealarm(struct rtc_device *rtc, suspend_state_t state)
{
	static char err_readtime[] __initdata =
		KERN_ERR "PM: can't read %s time, err %d\n";
	static char err_wakealarm [] __initdata =
		KERN_ERR "PM: can't set %s wakealarm, err %d\n";
	static char err_suspend[] __initdata =
		KERN_ERR "PM: suspend test failed, error %d\n";
	static char info_test[] __initdata =
		KERN_INFO "PM: test RTC wakeup from '%s' suspend\n";

	unsigned long		now;
	struct rtc_wkalrm	alm;
	int			status;

	/* this may fail if the RTC hasn't been initialized */
	status = rtc_read_time(rtc, &alm.time);
	if (status < 0) {
		printk(err_readtime, rtc->dev.bus_id, status);
		return;
	}
	rtc_tm_to_time(&alm.time, &now);

	memset(&alm, 0, sizeof alm);
	rtc_time_to_tm(now + TEST_SUSPEND_SECONDS, &alm.time);
	alm.enabled = true;

	status = rtc_set_alarm(rtc, &alm);
	if (status < 0) {
		printk(err_wakealarm, rtc->dev.bus_id, status);
		return;
	}

	if (state == PM_SUSPEND_MEM) {
		printk(info_test, pm_states[state]);
		status = pm_suspend(state);
		if (status == -ENODEV)
			state = PM_SUSPEND_STANDBY;
	}
	if (state == PM_SUSPEND_STANDBY) {
		printk(info_test, pm_states[state]);
		status = pm_suspend(state);
	}
	if (status < 0)
		printk(err_suspend, status);

	/* Some platforms can't detect that the alarm triggered the
	 * wakeup, or (accordingly) disable it after it afterwards.
	 * It's supposed to give oneshot behavior; cope.
	 */
	alm.enabled = false;
	rtc_set_alarm(rtc, &alm);
}

static int __init has_wakealarm(struct device *dev, void *name_ptr)
{
	struct rtc_device *candidate = to_rtc_device(dev);

	if (!candidate->ops->set_alarm)
		return 0;
	if (!device_may_wakeup(candidate->dev.parent))
		return 0;

	*(char **)name_ptr = dev->bus_id;
	return 1;
}

/*
 * Kernel options like "test_suspend=mem" force suspend/resume sanity tests
 * at startup time.  They're normally disabled, for faster boot and because
 * we can't know which states really work on this particular system.
 */
static suspend_state_t test_state __initdata = PM_SUSPEND_ON;

static char warn_bad_state[] __initdata =
	KERN_WARNING "PM: can't test '%s' suspend state\n";

static int __init setup_test_suspend(char *value)
{
	unsigned i;

	/* "=mem" ==> "mem" */
	value++;
	for (i = 0; i < PM_SUSPEND_MAX; i++) {
		if (!pm_states[i])
			continue;
		if (strcmp(pm_states[i], value) != 0)
			continue;
		test_state = (__force suspend_state_t) i;
		return 0;
	}
	printk(warn_bad_state, value);
	return 0;
}
__setup("test_suspend", setup_test_suspend);

static int __init test_suspend(void)
{
	static char		warn_no_rtc[] __initdata =
		KERN_WARNING "PM: no wakealarm-capable RTC driver is ready\n";

	char			*pony = NULL;
	struct rtc_device	*rtc = NULL;

	/* PM is initialized by now; is that state testable? */
	if (test_state == PM_SUSPEND_ON)
		goto done;
	if (!valid_state(test_state)) {
		printk(warn_bad_state, pm_states[test_state]);
		goto done;
	}

	/* RTCs have initialized by now too ... can we use one? */
	class_find_device(rtc_class, NULL, &pony, has_wakealarm);
	if (pony)
		rtc = rtc_class_open(pony);
	if (!rtc) {
		printk(warn_no_rtc);
		goto done;
	}

	/* go for it */
	test_wakealarm(rtc, test_state);
	rtc_class_close(rtc);
done:
	return 0;
}
late_initcall(test_suspend);

#endif /* CONFIG_PM_TEST_SUSPEND */
struct workqueue_struct *pm_wq;
EXPORT_SYMBOL_GPL(pm_wq);

static int __init pm_start_workqueue(void)
{
	pm_wq = alloc_workqueue("pm", WQ_FREEZABLE, 0);

	return pm_wq ? 0 : -ENOMEM;
}

static int __init pm_init(void)
{
	int error = pm_start_workqueue();
	if (error)
		return error;
	hibernate_image_size_init();
	hibernate_reserved_size_init();
	power_kobj = kobject_create_and_add("power", NULL);
	if (!power_kobj)
		return -ENOMEM;
	error = sysfs_create_group(power_kobj, &attr_group);
	if (error)
		return error;
	pm_print_times_init();
	return pm_autosleep_init();
}

core_initcall(pm_init);
