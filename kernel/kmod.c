/*
	kmod, the new module loader (replaces kerneld)
	Kirk Petersen

	Reorganized not to be a daemon by Adam Richter, with guidance
	from Greg Zornetzer.

	Modified to avoid chroot and file sharing problems.
	Mikael Pettersson

	Limit the concurrent number of kmod modprobes to catch loops from
	"modprobe needs a service that is in a module".
	Keith Owens <kaos@ocs.com.au> December 1999

	Unblock all signals when we exec a usermode process.
	Shuu Yamaguchi <shuu@wondernetworkresources.com> December 2000

	call_usermodehelper wait flag, and remove exec_usermodehelper.
	Rusty Russell <rusty@rustcorp.com.au>  Jan 2003
*/
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/syscalls.h>
#include <linux/unistd.h>
#include <linux/kmod.h>
#include <linux/slab.h>
#include <linux/mnt_namespace.h>
#include <linux/completion.h>
#include <linux/completion.h>
#include <linux/cred.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/fs_struct.h>
#include <linux/workqueue.h>
#include <linux/security.h>
#include <linux/mount.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/resource.h>
#include <linux/notifier.h>
#include <linux/suspend.h>
#include <asm/uaccess.h>

extern int max_threads;

static struct workqueue_struct *khelper_wq;
#include <linux/rwsem.h>
#include <linux/ptrace.h>
#include <linux/async.h>
#include <asm/uaccess.h>

#include <trace/events/module.h>

extern int max_threads;

#define CAP_BSET	(void *)1
#define CAP_PI		(void *)2

static kernel_cap_t usermodehelper_bset = CAP_FULL_SET;
static kernel_cap_t usermodehelper_inheritable = CAP_FULL_SET;
static DEFINE_SPINLOCK(umh_sysctl_lock);
static DECLARE_RWSEM(umhelper_sem);

#ifdef CONFIG_MODULES

/*
	modprobe_path is set via /proc/sys.
*/
char modprobe_path[KMOD_PATH_LEN] = "/sbin/modprobe";

/**
 * request_module - try to load a kernel module
 * @fmt:     printf style format string for the name of the module
 * @varargs: arguements as specified in the format string
 *
 * Load a module using the user mode module loader. The function returns
 * zero on success or a negative errno code on failure. Note that a
 * successful module load does not mean the module did not then unload
 * and exit on an error of its own. Callers must check that the service
 * they requested is now available not blindly invoke it.
static void free_modprobe_argv(struct subprocess_info *info)
{
	kfree(info->argv[3]); /* check call_modprobe() */
	kfree(info->argv);
}

static int call_modprobe(char *module_name, int wait)
{
	struct subprocess_info *info;
	static char *envp[] = {
		"HOME=/",
		"TERM=linux",
		"PATH=/sbin:/usr/sbin:/bin:/usr/bin",
		NULL
	};

	char **argv = kmalloc(sizeof(char *[5]), GFP_KERNEL);
	if (!argv)
		goto out;

	module_name = kstrdup(module_name, GFP_KERNEL);
	if (!module_name)
		goto free_argv;

	argv[0] = modprobe_path;
	argv[1] = "-q";
	argv[2] = "--";
	argv[3] = module_name;	/* check free_modprobe_argv() */
	argv[4] = NULL;

	info = call_usermodehelper_setup(modprobe_path, argv, envp, GFP_KERNEL,
					 NULL, free_modprobe_argv, NULL);
	if (!info)
		goto free_module_name;

	return call_usermodehelper_exec(info, wait | UMH_KILLABLE);

free_module_name:
	kfree(module_name);
free_argv:
	kfree(argv);
out:
	return -ENOMEM;
}

/**
 * __request_module - try to load a kernel module
 * @wait: wait (or not) for the operation to complete
 * @fmt: printf style format string for the name of the module
 * @...: arguments as specified in the format string
 *
 * Load a module using the user mode module loader. The function returns
 * zero on success or a negative errno code or positive exit code from
 * "modprobe" on failure. Note that a successful module load does not mean
 * the module did not then unload and exit on an error of its own. Callers
 * must check that the service they requested is now available not blindly
 * invoke it.
 *
 * If module auto-loading support is disabled then this function
 * simply returns -ENOENT.
 */
int request_module(const char *fmt, ...)
int __request_module(bool wait, const char *fmt, ...)
{
	va_list args;
	char module_name[MODULE_NAME_LEN];
	unsigned int max_modprobes;
	int ret;
	char *argv[] = { modprobe_path, "-q", "--", module_name, NULL };
	static char *envp[] = { "HOME=/",
				"TERM=linux",
				"PATH=/sbin:/usr/sbin:/bin:/usr/bin",
				NULL };
	static atomic_t kmod_concurrent = ATOMIC_INIT(0);
#define MAX_KMOD_CONCURRENT 50	/* Completely arbitrary value - KAO */
	static int kmod_loop_msg;

	/*
	 * We don't allow synchronous module loading from async.  Module
	 * init may invoke async_synchronize_full() which will end up
	 * waiting for this task which already is waiting for the module
	 * loading to complete, leading to a deadlock.
	 */
	WARN_ON_ONCE(wait && current_is_async());

	if (!modprobe_path[0])
		return -ENOENT;

	va_start(args, fmt);
	ret = vsnprintf(module_name, MODULE_NAME_LEN, fmt, args);
	va_end(args);
	if (ret >= MODULE_NAME_LEN)
		return -ENAMETOOLONG;

	ret = security_kernel_module_request(module_name);
	if (ret)
		return ret;

	/* If modprobe needs a service that is in a module, we get a recursive
	 * loop.  Limit the number of running kmod threads to max_threads/2 or
	 * MAX_KMOD_CONCURRENT, whichever is the smaller.  A cleaner method
	 * would be to run the parents of this process, counting how many times
	 * kmod was invoked.  That would mean accessing the internals of the
	 * process tables to get the command line, proc_pid_cmdline is static
	 * and it is not worth changing the proc code just to handle this case. 
	 * KAO.
	 *
	 * "trace the ppid" is simple, but will fail if someone's
	 * parent exits.  I think this is as good as it gets. --RR
	 */
	max_modprobes = min(max_threads/2, MAX_KMOD_CONCURRENT);
	atomic_inc(&kmod_concurrent);
	if (atomic_read(&kmod_concurrent) > max_modprobes) {
		/* We may be blaming an innocent here, but unlikely */
		if (kmod_loop_msg++ < 5)
			printk(KERN_ERR
			       "request_module: runaway loop modprobe %s\n",
			       module_name);
		if (kmod_loop_msg < 5) {
			printk(KERN_ERR
			       "request_module: runaway loop modprobe %s\n",
			       module_name);
			kmod_loop_msg++;
		}
		atomic_dec(&kmod_concurrent);
		return -ENOMEM;
	}

	ret = call_usermodehelper(modprobe_path, argv, envp, 1);
	atomic_dec(&kmod_concurrent);
	return ret;
}
EXPORT_SYMBOL(request_module);
#endif /* CONFIG_KMOD */

struct subprocess_info {
	struct work_struct work;
	struct completion *complete;
	char *path;
	char **argv;
	char **envp;
	struct key *ring;
	enum umh_wait wait;
	int retval;
	struct file *stdin;
	void (*cleanup)(char **argv, char **envp);
};
	trace_module_request(module_name, wait, _RET_IP_);

	ret = call_modprobe(module_name, wait ? UMH_WAIT_PROC : UMH_WAIT_EXEC);

	atomic_dec(&kmod_concurrent);
	return ret;
}
EXPORT_SYMBOL(__request_module);
#endif /* CONFIG_MODULES */

static void call_usermodehelper_freeinfo(struct subprocess_info *info)
{
	if (info->cleanup)
		(*info->cleanup)(info);
	kfree(info);
}

static void umh_complete(struct subprocess_info *sub_info)
{
	struct completion *comp = xchg(&sub_info->complete, NULL);
	/*
	 * See call_usermodehelper_exec(). If xchg() returns NULL
	 * we own sub_info, the UMH_KILLABLE caller has gone away
	 * or the caller used UMH_NO_WAIT.
	 */
	if (comp)
		complete(comp);
	else
		call_usermodehelper_freeinfo(sub_info);
}

/*
 * This is the task which runs the usermode application
 */
static int ____call_usermodehelper(void *data)
{
	struct subprocess_info *sub_info = data;
	struct key *new_session, *old_session;
	int retval;

	/* Unblock all signals and set the session keyring. */
	new_session = key_get(sub_info->ring);
	spin_lock_irq(&current->sighand->siglock);
	old_session = __install_session_keyring(current, new_session);
	flush_signal_handlers(current, 1);
	sigemptyset(&current->blocked);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	key_put(old_session);

	/* Install input pipe when needed */
	if (sub_info->stdin) {
		struct files_struct *f = current->files;
		struct fdtable *fdt;
		/* no races because files should be private here */
		sys_close(0);
		fd_install(0, sub_info->stdin);
		spin_lock(&f->file_lock);
		fdt = files_fdtable(f);
		FD_SET(0, fdt->open_fds);
		FD_CLR(0, fdt->close_on_exec);
		spin_unlock(&f->file_lock);

		/* and disallow core files too */
		current->signal->rlim[RLIMIT_CORE] = (struct rlimit){0, 0};
	}

	/* We can run anywhere, unlike our parent keventd(). */
	set_cpus_allowed_ptr(current, CPU_MASK_ALL_PTR);

	/*
	 * Our parent is keventd, which runs with elevated scheduling priority.
	 * Avoid propagating that into the userspace child.
	 */
	set_user_nice(current, 0);

	retval = kernel_execve(sub_info->path, sub_info->argv, sub_info->envp);

	/* Exec failed? */
	sub_info->retval = retval;
	do_exit(0);
}

void call_usermodehelper_freeinfo(struct subprocess_info *info)
{
	if (info->cleanup)
		(*info->cleanup)(info->argv, info->envp);
	kfree(info);
}
EXPORT_SYMBOL(call_usermodehelper_freeinfo);

/* Keventd can't block, but this (a child) can. */
static int wait_for_helper(void *data)
{
	struct subprocess_info *sub_info = data;
	pid_t pid;

	/* Install a handler: if SIGCLD isn't handled sys_wait4 won't
	 * populate the status, but will return -ECHILD. */
	allow_signal(SIGCHLD);

	pid = kernel_thread(____call_usermodehelper, sub_info, SIGCHLD);
	if (pid < 0) {
		sub_info->retval = pid;
	} else {
		int ret;

		/*
		 * Normally it is bogus to call wait4() from in-kernel because
		 * wait4() wants to write the exit code to a userspace address.
		 * But wait_for_helper() always runs as keventd, and put_user()
		 * to a kernel address works OK for kernel threads, due to their
		 * having an mm_segment_t which spans the entire address space.
static int call_usermodehelper_exec_async(void *data)
{
	struct subprocess_info *sub_info = data;
	struct cred *new;
	int retval;

	spin_lock_irq(&current->sighand->siglock);
	flush_signal_handlers(current, 1);
	spin_unlock_irq(&current->sighand->siglock);

	/*
	 * Initial kernel threads share ther FS with init, in order to
	 * get the init root directory. But we've now created a new
	 * thread that is going to execve a user process and has its own
	 * 'struct fs_struct'. Reset umask to the default.
	 */
	current->fs->umask = 0022;

	/*
	 * Our parent (unbound workqueue) runs with elevated scheduling
	 * priority. Avoid propagating that into the userspace child.
	 */
	set_user_nice(current, 0);

	retval = -ENOMEM;
	new = prepare_kernel_cred(current);
	if (!new)
		goto out;

	spin_lock(&umh_sysctl_lock);
	new->cap_bset = cap_intersect(usermodehelper_bset, new->cap_bset);
	new->cap_inheritable = cap_intersect(usermodehelper_inheritable,
					     new->cap_inheritable);
	spin_unlock(&umh_sysctl_lock);

	if (sub_info->init) {
		retval = sub_info->init(sub_info, new);
		if (retval) {
			abort_creds(new);
			goto out;
		}
	}

	commit_creds(new);

	retval = do_execve(getname_kernel(sub_info->path),
			   (const char __user *const __user *)sub_info->argv,
			   (const char __user *const __user *)sub_info->envp);
out:
	sub_info->retval = retval;
	/*
	 * call_usermodehelper_exec_sync() will call umh_complete
	 * if UHM_WAIT_PROC.
	 */
	if (!(sub_info->wait & UMH_WAIT_PROC))
		umh_complete(sub_info);
	if (!retval)
		return 0;
	do_exit(0);
}

/* Handles UMH_WAIT_PROC.  */
static void call_usermodehelper_exec_sync(struct subprocess_info *sub_info)
{
	pid_t pid;

	/* If SIGCLD is ignored sys_wait4 won't populate the status. */
	kernel_sigaction(SIGCHLD, SIG_DFL);
	pid = kernel_thread(call_usermodehelper_exec_async, sub_info, SIGCHLD);
	if (pid < 0) {
		sub_info->retval = pid;
	} else {
		int ret = -ECHILD;
		/*
		 * Normally it is bogus to call wait4() from in-kernel because
		 * wait4() wants to write the exit code to a userspace address.
		 * But call_usermodehelper_exec_sync() always runs as kernel
		 * thread (workqueue) and put_user() to a kernel address works
		 * OK for kernel threads, due to their having an mm_segment_t
		 * which spans the entire address space.
		 *
		 * Thus the __user pointer cast is valid here.
		 */
		sys_wait4(pid, (int __user *)&ret, 0, NULL);

		/*
		 * If ret is 0, either ____call_usermodehelper failed and the
		 * real error code is already in sub_info->retval or
		 * If ret is 0, either call_usermodehelper_exec_async failed and
		 * the real error code is already in sub_info->retval or
		 * sub_info->retval is 0 anyway, so don't mess with it then.
		 */
		if (ret)
			sub_info->retval = ret;
	}

	if (sub_info->wait == UMH_NO_WAIT)
		call_usermodehelper_freeinfo(sub_info);
	else
		complete(sub_info->complete);
	return 0;
}

/* This is run by khelper thread  */
static void __call_usermodehelper(struct work_struct *work)
{
	struct subprocess_info *sub_info =
		container_of(work, struct subprocess_info, work);
	pid_t pid;
	enum umh_wait wait = sub_info->wait;

	/* CLONE_VFORK: wait until the usermode helper has execve'd
	 * successfully We need the data structures to stay around
	 * until that is done.  */
	if (wait == UMH_WAIT_PROC || wait == UMH_NO_WAIT)
		pid = kernel_thread(wait_for_helper, sub_info,
				    CLONE_FS | CLONE_FILES | SIGCHLD);
	else
		pid = kernel_thread(____call_usermodehelper, sub_info,
				    CLONE_VFORK | SIGCHLD);

	switch (wait) {
	case UMH_NO_WAIT:
		break;

	case UMH_WAIT_PROC:
		if (pid > 0)
			break;
		sub_info->retval = pid;
		/* FALLTHROUGH */

	case UMH_WAIT_EXEC:
		complete(sub_info->complete);
	}
}

#ifdef CONFIG_PM
	/* Restore default kernel sig handler */
	kernel_sigaction(SIGCHLD, SIG_IGN);

	umh_complete(sub_info);
}

/*
 * We need to create the usermodehelper kernel thread from a task that is affine
 * to an optimized set of CPUs (or nohz housekeeping ones) such that they
 * inherit a widest affinity irrespective of call_usermodehelper() callers with
 * possibly reduced affinity (eg: per-cpu workqueues). We don't want
 * usermodehelper targets to contend a busy CPU.
 *
 * Unbound workqueues provide such wide affinity and allow to block on
 * UMH_WAIT_PROC requests without blocking pending request (up to some limit).
 *
 * Besides, workqueues provide the privilege level that caller might not have
 * to perform the usermodehelper request.
 *
 */
static void call_usermodehelper_exec_work(struct work_struct *work)
{
	struct subprocess_info *sub_info =
		container_of(work, struct subprocess_info, work);

	if (sub_info->wait & UMH_WAIT_PROC) {
		call_usermodehelper_exec_sync(sub_info);
	} else {
		pid_t pid;
		/*
		 * Use CLONE_PARENT to reparent it to kthreadd; we do not
		 * want to pollute current->children, and we need a parent
		 * that always ignores SIGCHLD to ensure auto-reaping.
		 */
		pid = kernel_thread(call_usermodehelper_exec_async, sub_info,
				    CLONE_PARENT | SIGCHLD);
		if (pid < 0) {
			sub_info->retval = pid;
			umh_complete(sub_info);
		}
	}
}

/*
 * If set, call_usermodehelper_exec() will exit immediately returning -EBUSY
 * (used for preventing user land processes from being created after the user
 * land has been frozen during a system-wide hibernation or suspend operation).
 */
static int usermodehelper_disabled;
 * Should always be manipulated under umhelper_sem acquired for write.
 */
static enum umh_disable_depth usermodehelper_disabled = UMH_DISABLED;

/* Number of helpers running */
static atomic_t running_helpers = ATOMIC_INIT(0);

/*
 * Wait queue head used by usermodehelper_pm_callback() to wait for all running
 * Wait queue head used by usermodehelper_disable() to wait for all running
 * helpers to finish.
 */
static DECLARE_WAIT_QUEUE_HEAD(running_helpers_waitq);

/*
 * Time to wait for running_helpers to become zero before the setting of
 * usermodehelper_disabled in usermodehelper_pm_callback() fails
 */
#define RUNNING_HELPERS_TIMEOUT	(5 * HZ)

static int usermodehelper_pm_callback(struct notifier_block *nfb,
					unsigned long action,
					void *ignored)
{
	long retval;

	switch (action) {
	case PM_HIBERNATION_PREPARE:
	case PM_SUSPEND_PREPARE:
		usermodehelper_disabled = 1;
		smp_mb();
		/*
		 * From now on call_usermodehelper_exec() won't start any new
		 * helpers, so it is sufficient if running_helpers turns out to
		 * be zero at one point (it may be increased later, but that
		 * doesn't matter).
		 */
		retval = wait_event_timeout(running_helpers_waitq,
					atomic_read(&running_helpers) == 0,
					RUNNING_HELPERS_TIMEOUT);
		if (retval) {
			return NOTIFY_OK;
		} else {
			usermodehelper_disabled = 0;
			return NOTIFY_BAD;
		}
	case PM_POST_HIBERNATION:
	case PM_POST_SUSPEND:
		usermodehelper_disabled = 0;
		return NOTIFY_OK;
	}

	return NOTIFY_DONE;
 * Used by usermodehelper_read_lock_wait() to wait for usermodehelper_disabled
 * to become 'false'.
 */
static DECLARE_WAIT_QUEUE_HEAD(usermodehelper_disabled_waitq);

/*
 * Time to wait for running_helpers to become zero before the setting of
 * usermodehelper_disabled in usermodehelper_disable() fails
 */
#define RUNNING_HELPERS_TIMEOUT	(5 * HZ)

int usermodehelper_read_trylock(void)
{
	DEFINE_WAIT(wait);
	int ret = 0;

	down_read(&umhelper_sem);
	for (;;) {
		prepare_to_wait(&usermodehelper_disabled_waitq, &wait,
				TASK_INTERRUPTIBLE);
		if (!usermodehelper_disabled)
			break;

		if (usermodehelper_disabled == UMH_DISABLED)
			ret = -EAGAIN;

		up_read(&umhelper_sem);

		if (ret)
			break;

		schedule();
		try_to_freeze();

		down_read(&umhelper_sem);
	}
	finish_wait(&usermodehelper_disabled_waitq, &wait);
	return ret;
}
EXPORT_SYMBOL_GPL(usermodehelper_read_trylock);

long usermodehelper_read_lock_wait(long timeout)
{
	DEFINE_WAIT(wait);

	if (timeout < 0)
		return -EINVAL;

	down_read(&umhelper_sem);
	for (;;) {
		prepare_to_wait(&usermodehelper_disabled_waitq, &wait,
				TASK_UNINTERRUPTIBLE);
		if (!usermodehelper_disabled)
			break;

		up_read(&umhelper_sem);

		timeout = schedule_timeout(timeout);
		if (!timeout)
			break;

		down_read(&umhelper_sem);
	}
	finish_wait(&usermodehelper_disabled_waitq, &wait);
	return timeout;
}
EXPORT_SYMBOL_GPL(usermodehelper_read_lock_wait);

void usermodehelper_read_unlock(void)
{
	up_read(&umhelper_sem);
}
EXPORT_SYMBOL_GPL(usermodehelper_read_unlock);

/**
 * __usermodehelper_set_disable_depth - Modify usermodehelper_disabled.
 * @depth: New value to assign to usermodehelper_disabled.
 *
 * Change the value of usermodehelper_disabled (under umhelper_sem locked for
 * writing) and wakeup tasks waiting for it to change.
 */
void __usermodehelper_set_disable_depth(enum umh_disable_depth depth)
{
	down_write(&umhelper_sem);
	usermodehelper_disabled = depth;
	wake_up(&usermodehelper_disabled_waitq);
	up_write(&umhelper_sem);
}

/**
 * __usermodehelper_disable - Prevent new helpers from being started.
 * @depth: New value to assign to usermodehelper_disabled.
 *
 * Set usermodehelper_disabled to @depth and wait for running helpers to exit.
 */
int __usermodehelper_disable(enum umh_disable_depth depth)
{
	long retval;

	if (!depth)
		return -EINVAL;

	down_write(&umhelper_sem);
	usermodehelper_disabled = depth;
	up_write(&umhelper_sem);

	/*
	 * From now on call_usermodehelper_exec() won't start any new
	 * helpers, so it is sufficient if running_helpers turns out to
	 * be zero at one point (it may be increased later, but that
	 * doesn't matter).
	 */
	retval = wait_event_timeout(running_helpers_waitq,
					atomic_read(&running_helpers) == 0,
					RUNNING_HELPERS_TIMEOUT);
	if (retval)
		return 0;

	__usermodehelper_set_disable_depth(UMH_ENABLED);
	return -EAGAIN;
}

static void helper_lock(void)
{
	atomic_inc(&running_helpers);
	smp_mb__after_atomic_inc();
	smp_mb__after_atomic();
}

static void helper_unlock(void)
{
	if (atomic_dec_and_test(&running_helpers))
		wake_up(&running_helpers_waitq);
}

static void register_pm_notifier_callback(void)
{
	pm_notifier(usermodehelper_pm_callback, 0);
}
#else /* CONFIG_PM */
#define usermodehelper_disabled	0

static inline void helper_lock(void) {}
static inline void helper_unlock(void) {}
static inline void register_pm_notifier_callback(void) {}
#endif /* CONFIG_PM */

/**
 * call_usermodehelper_setup - prepare to call a usermode helper
 * @path: path to usermode executable
 * @argv: arg vector for process
 * @envp: environment for process
 * @gfp_mask: gfp mask for memory allocation
 * @cleanup: a cleanup function
 * @init: an init function
 * @data: arbitrary context sensitive data
 *
 * Returns either %NULL on allocation failure, or a subprocess_info
 * structure.  This should be passed to call_usermodehelper_exec to
 * exec the process and free the structure.
 */
struct subprocess_info *call_usermodehelper_setup(char *path, char **argv,
						  char **envp, gfp_t gfp_mask)
 *
 * The init function is used to customize the helper process prior to
 * exec.  A non-zero return code causes the process to error out, exit,
 * and return the failure to the calling process
 *
 * The cleanup function is just before ethe subprocess_info is about to
 * be freed.  This can be used for freeing the argv and envp.  The
 * Function must be runnable in either a process context or the
 * context in which call_usermodehelper_exec is called.
 */
struct subprocess_info *call_usermodehelper_setup(char *path, char **argv,
		char **envp, gfp_t gfp_mask,
		int (*init)(struct subprocess_info *info, struct cred *new),
		void (*cleanup)(struct subprocess_info *info),
		void *data)
{
	struct subprocess_info *sub_info;
	sub_info = kzalloc(sizeof(struct subprocess_info), gfp_mask);
	if (!sub_info)
		goto out;

	INIT_WORK(&sub_info->work, __call_usermodehelper);
	INIT_WORK(&sub_info->work, call_usermodehelper_exec_work);
	sub_info->path = path;
	sub_info->argv = argv;
	sub_info->envp = envp;

	sub_info->cleanup = cleanup;
	sub_info->init = init;
	sub_info->data = data;
  out:
	return sub_info;
}
EXPORT_SYMBOL(call_usermodehelper_setup);

/**
 * call_usermodehelper_setkeys - set the session keys for usermode helper
 * @info: a subprocess_info returned by call_usermodehelper_setup
 * @session_keyring: the session keyring for the process
 */
void call_usermodehelper_setkeys(struct subprocess_info *info,
				 struct key *session_keyring)
{
	info->ring = session_keyring;
}
EXPORT_SYMBOL(call_usermodehelper_setkeys);

/**
 * call_usermodehelper_setcleanup - set a cleanup function
 * @info: a subprocess_info returned by call_usermodehelper_setup
 * @cleanup: a cleanup function
 *
 * The cleanup function is just befor ethe subprocess_info is about to
 * be freed.  This can be used for freeing the argv and envp.  The
 * Function must be runnable in either a process context or the
 * context in which call_usermodehelper_exec is called.
 */
void call_usermodehelper_setcleanup(struct subprocess_info *info,
				    void (*cleanup)(char **argv, char **envp))
{
	info->cleanup = cleanup;
}
EXPORT_SYMBOL(call_usermodehelper_setcleanup);

/**
 * call_usermodehelper_stdinpipe - set up a pipe to be used for stdin
 * @sub_info: a subprocess_info returned by call_usermodehelper_setup
 * @filp: set to the write-end of a pipe
 *
 * This constructs a pipe, and sets the read end to be the stdin of the
 * subprocess, and returns the write-end in *@filp.
 */
int call_usermodehelper_stdinpipe(struct subprocess_info *sub_info,
				  struct file **filp)
{
	struct file *f;

	f = create_write_pipe(0);
	if (IS_ERR(f))
		return PTR_ERR(f);
	*filp = f;

	f = create_read_pipe(f, 0);
	if (IS_ERR(f)) {
		free_write_pipe(*filp);
		return PTR_ERR(f);
	}
	sub_info->stdin = f;

	return 0;
}
EXPORT_SYMBOL(call_usermodehelper_stdinpipe);

/**
 * call_usermodehelper_exec - start a usermode application
 * @sub_info: information about the subprocessa
 * @wait: wait for the application to finish and return status.
 *        when -1 don't wait at all, but you get no useful error back when
 *        the program couldn't be exec'ed. This makes it safe to call
 *        from interrupt context.
 *
 * Runs a user-space application.  The application is started
 * asynchronously if wait is not set, and runs as a child of keventd.
 * (ie. it runs with full root capabilities).
 */
int call_usermodehelper_exec(struct subprocess_info *sub_info,
			     enum umh_wait wait)
 * call_usermodehelper_exec - start a usermode application
 * @sub_info: information about the subprocessa
 * @wait: wait for the application to finish and return status.
 *        when UMH_NO_WAIT don't wait at all, but you get no useful error back
 *        when the program couldn't be exec'ed. This makes it safe to call
 *        from interrupt context.
 *
 * Runs a user-space application.  The application is started
 * asynchronously if wait is not set, and runs as a child of system workqueues.
 * (ie. it runs with full root capabilities and optimized affinity).
 */
int call_usermodehelper_exec(struct subprocess_info *sub_info, int wait)
{
	DECLARE_COMPLETION_ONSTACK(done);
	int retval = 0;

	helper_lock();
	if (sub_info->path[0] == '\0')
		goto out;

	if (!khelper_wq || usermodehelper_disabled) {
		retval = -EBUSY;
		goto out;
	}

	sub_info->complete = &done;
	sub_info->wait = wait;

	queue_work(khelper_wq, &sub_info->work);
	if (wait == UMH_NO_WAIT)	/* task has freed sub_info */
		goto unlock;
	wait_for_completion(&done);
	retval = sub_info->retval;

	if (!sub_info->path) {
		call_usermodehelper_freeinfo(sub_info);
		return -EINVAL;
	}
	helper_lock();
	if (usermodehelper_disabled) {
		retval = -EBUSY;
		goto out;
	}
	/*
	 * Set the completion pointer only if there is a waiter.
	 * This makes it possible to use umh_complete to free
	 * the data structure in case of UMH_NO_WAIT.
	 */
	sub_info->complete = (wait == UMH_NO_WAIT) ? NULL : &done;
	sub_info->wait = wait;

	queue_work(system_unbound_wq, &sub_info->work);
	if (wait == UMH_NO_WAIT)	/* task has freed sub_info */
		goto unlock;

	if (wait & UMH_KILLABLE) {
		retval = wait_for_completion_killable(&done);
		if (!retval)
			goto wait_done;

		/* umh_complete() will see NULL and free sub_info */
		if (xchg(&sub_info->complete, NULL))
			goto unlock;
		/* fallthrough, umh_complete() was already called */
	}

	wait_for_completion(&done);
wait_done:
	retval = sub_info->retval;
out:
	call_usermodehelper_freeinfo(sub_info);
unlock:
	helper_unlock();
	return retval;
}
EXPORT_SYMBOL(call_usermodehelper_exec);

/**
 * call_usermodehelper_pipe - call a usermode helper process with a pipe stdin
 * @path: path to usermode executable
 * @argv: arg vector for process
 * @envp: environment for process
 * @filp: set to the write-end of a pipe
 *
 * This is a simple wrapper which executes a usermode-helper function
 * with a pipe as stdin.  It is implemented entirely in terms of
 * lower-level call_usermodehelper_* functions.
 */
int call_usermodehelper_pipe(char *path, char **argv, char **envp,
			     struct file **filp)
{
	struct subprocess_info *sub_info;
	int ret;

	sub_info = call_usermodehelper_setup(path, argv, envp, GFP_KERNEL);
	if (sub_info == NULL)
		return -ENOMEM;

	ret = call_usermodehelper_stdinpipe(sub_info, filp);
	if (ret < 0)
		goto out;

	return call_usermodehelper_exec(sub_info, UMH_WAIT_EXEC);

  out:
	call_usermodehelper_freeinfo(sub_info);
	return ret;
}
EXPORT_SYMBOL(call_usermodehelper_pipe);

void __init usermodehelper_init(void)
{
	khelper_wq = create_singlethread_workqueue("khelper");
	BUG_ON(!khelper_wq);
	register_pm_notifier_callback();
}
 * call_usermodehelper() - prepare and start a usermode application
 * @path: path to usermode executable
 * @argv: arg vector for process
 * @envp: environment for process
 * @wait: wait for the application to finish and return status.
 *        when UMH_NO_WAIT don't wait at all, but you get no useful error back
 *        when the program couldn't be exec'ed. This makes it safe to call
 *        from interrupt context.
 *
 * This function is the equivalent to use call_usermodehelper_setup() and
 * call_usermodehelper_exec().
 */
int call_usermodehelper(char *path, char **argv, char **envp, int wait)
{
	struct subprocess_info *info;
	gfp_t gfp_mask = (wait == UMH_NO_WAIT) ? GFP_ATOMIC : GFP_KERNEL;

	info = call_usermodehelper_setup(path, argv, envp, gfp_mask,
					 NULL, NULL, NULL);
	if (info == NULL)
		return -ENOMEM;

	return call_usermodehelper_exec(info, wait);
}
EXPORT_SYMBOL(call_usermodehelper);

static int proc_cap_handler(struct ctl_table *table, int write,
			 void __user *buffer, size_t *lenp, loff_t *ppos)
{
	struct ctl_table t;
	unsigned long cap_array[_KERNEL_CAPABILITY_U32S];
	kernel_cap_t new_cap;
	int err, i;

	if (write && (!capable(CAP_SETPCAP) ||
		      !capable(CAP_SYS_MODULE)))
		return -EPERM;

	/*
	 * convert from the global kernel_cap_t to the ulong array to print to
	 * userspace if this is a read.
	 */
	spin_lock(&umh_sysctl_lock);
	for (i = 0; i < _KERNEL_CAPABILITY_U32S; i++)  {
		if (table->data == CAP_BSET)
			cap_array[i] = usermodehelper_bset.cap[i];
		else if (table->data == CAP_PI)
			cap_array[i] = usermodehelper_inheritable.cap[i];
		else
			BUG();
	}
	spin_unlock(&umh_sysctl_lock);

	t = *table;
	t.data = &cap_array;

	/*
	 * actually read or write and array of ulongs from userspace.  Remember
	 * these are least significant 32 bits first
	 */
	err = proc_doulongvec_minmax(&t, write, buffer, lenp, ppos);
	if (err < 0)
		return err;

	/*
	 * convert from the sysctl array of ulongs to the kernel_cap_t
	 * internal representation
	 */
	for (i = 0; i < _KERNEL_CAPABILITY_U32S; i++)
		new_cap.cap[i] = cap_array[i];

	/*
	 * Drop everything not in the new_cap (but don't add things)
	 */
	spin_lock(&umh_sysctl_lock);
	if (write) {
		if (table->data == CAP_BSET)
			usermodehelper_bset = cap_intersect(usermodehelper_bset, new_cap);
		if (table->data == CAP_PI)
			usermodehelper_inheritable = cap_intersect(usermodehelper_inheritable, new_cap);
	}
	spin_unlock(&umh_sysctl_lock);

	return 0;
}

struct ctl_table usermodehelper_table[] = {
	{
		.procname	= "bset",
		.data		= CAP_BSET,
		.maxlen		= _KERNEL_CAPABILITY_U32S * sizeof(unsigned long),
		.mode		= 0600,
		.proc_handler	= proc_cap_handler,
	},
	{
		.procname	= "inheritable",
		.data		= CAP_PI,
		.maxlen		= _KERNEL_CAPABILITY_U32S * sizeof(unsigned long),
		.mode		= 0600,
		.proc_handler	= proc_cap_handler,
	},
	{ }
};
