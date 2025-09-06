/*
 * linux/kernel/capability.c
 *
 * Copyright (C) 1997  Andrew Main <zefram@fysh.org>
 *
 * Integrated into 2.1.97+,  Andrew G. Morgan <morgan@kernel.org>
 * 30 May 2002:	Cleanup, Robert M. Love <rml@tech9.net>
 */

#include <linux/capability.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/pid_namespace.h>
#include <asm/uaccess.h>

/*
 * This lock protects task->cap_* for all tasks including current.
 * Locking rule: acquire this prior to tasklist_lock.
 */
static DEFINE_SPINLOCK(task_capability_lock);

/*
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/audit.h>
#include <linux/capability.h>
#include <linux/mm.h>
#include <linux/export.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/pid_namespace.h>
#include <linux/user_namespace.h>
#include <asm/uaccess.h>

/*
 * Leveraged for setting/resetting capabilities
 */

const kernel_cap_t __cap_empty_set = CAP_EMPTY_SET;
const kernel_cap_t __cap_full_set = CAP_FULL_SET;
const kernel_cap_t __cap_init_eff_set = CAP_INIT_EFF_SET;

EXPORT_SYMBOL(__cap_empty_set);
EXPORT_SYMBOL(__cap_full_set);
EXPORT_SYMBOL(__cap_init_eff_set);

EXPORT_SYMBOL(__cap_empty_set);

int file_caps_enabled = 1;

static int __init file_caps_disable(char *str)
{
	file_caps_enabled = 0;
	return 1;
}
__setup("no_file_caps", file_caps_disable);

#ifdef CONFIG_MULTIUSER
/*
 * More recent versions of libcap are available from:
 *
 *   http://www.kernel.org/pub/linux/libs/security/linux-privs/
 */

static void warn_legacy_capability_use(void)
{
	static int warned;
	if (!warned) {
		char name[sizeof(current->comm)];

		printk(KERN_INFO "warning: `%s' uses 32-bit capabilities"
		       " (legacy support in use)\n",
		       get_task_comm(name, current));
		warned = 1;
	}
	char name[sizeof(current->comm)];

	pr_info_once("warning: `%s' uses 32-bit capabilities (legacy support in use)\n",
		     get_task_comm(name, current));
}

/*
 * Version 2 capabilities worked fine, but the linux/capability.h file
 * that accompanied their introduction encouraged their use without
 * the necessary user-space source code changes. As such, we have
 * created a version 3 with equivalent functionality to version 2, but
 * with a header change to protect legacy source code from using
 * version 2 when it wanted to use version 1. If your system has code
 * that trips the following warning, it is using version 2 specific
 * capabilities and may be doing so insecurely.
 *
 * The remedy is to either upgrade your version of libcap (to 2.10+,
 * if the application is linked against it), or recompile your
 * application with modern kernel headers and this warning will go
 * away.
 */

static void warn_deprecated_v2(void)
{
	static int warned;

	if (!warned) {
		char name[sizeof(current->comm)];

		printk(KERN_INFO "warning: `%s' uses deprecated v2"
		       " capabilities in a way that may be insecure.\n",
		       get_task_comm(name, current));
		warned = 1;
	}
	char name[sizeof(current->comm)];

	pr_info_once("warning: `%s' uses deprecated v2 capabilities in a way that may be insecure\n",
		     get_task_comm(name, current));
}

/*
 * Version check. Return the number of u32s in each capability flag
 * array, or a negative value on error.
 */
static int cap_validate_magic(cap_user_header_t header, unsigned *tocopy)
{
	__u32 version;

	if (get_user(version, &header->version))
		return -EFAULT;

	switch (version) {
	case _LINUX_CAPABILITY_VERSION_1:
		warn_legacy_capability_use();
		*tocopy = _LINUX_CAPABILITY_U32S_1;
		break;
	case _LINUX_CAPABILITY_VERSION_2:
		warn_deprecated_v2();
		/*
		 * fall through - v3 is otherwise equivalent to v2.
		 */
	case _LINUX_CAPABILITY_VERSION_3:
		*tocopy = _LINUX_CAPABILITY_U32S_3;
		break;
	default:
		if (put_user((u32)_KERNEL_CAPABILITY_VERSION, &header->version))
			return -EFAULT;
		return -EINVAL;
	}

	return 0;
}

#ifndef CONFIG_SECURITY_FILE_CAPABILITIES

/*
 * Without filesystem capability support, we nominally support one process
 * setting the capabilities of another
 */
static inline int cap_get_target_pid(pid_t pid, kernel_cap_t *pEp,
				     kernel_cap_t *pIp, kernel_cap_t *pPp)
{
	struct task_struct *target;
	int ret;

	spin_lock(&task_capability_lock);
	read_lock(&tasklist_lock);

	if (pid && pid != task_pid_vnr(current)) {
		target = find_task_by_vpid(pid);
		if (!target) {
			ret = -ESRCH;
			goto out;
		}
	} else
		target = current;

	ret = security_capget(target, pEp, pIp, pPp);

out:
	read_unlock(&tasklist_lock);
	spin_unlock(&task_capability_lock);

	return ret;
}

/*
 * cap_set_pg - set capabilities for all processes in a given process
 * group.  We call this holding task_capability_lock and tasklist_lock.
 */
static inline int cap_set_pg(int pgrp_nr, kernel_cap_t *effective,
			     kernel_cap_t *inheritable,
			     kernel_cap_t *permitted)
{
	struct task_struct *g, *target;
	int ret = -EPERM;
	int found = 0;
	struct pid *pgrp;

	spin_lock(&task_capability_lock);
	read_lock(&tasklist_lock);

	pgrp = find_vpid(pgrp_nr);
	do_each_pid_task(pgrp, PIDTYPE_PGID, g) {
		target = g;
		while_each_thread(g, target) {
			if (!security_capset_check(target, effective,
						   inheritable, permitted)) {
				security_capset_set(target, effective,
						    inheritable, permitted);
				ret = 0;
			}
			found = 1;
		}
	} while_each_pid_task(pgrp, PIDTYPE_PGID, g);

	read_unlock(&tasklist_lock);
	spin_unlock(&task_capability_lock);

	if (!found)
		ret = 0;
	return ret;
}

/*
 * cap_set_all - set capabilities for all processes other than init
 * and self.  We call this holding task_capability_lock and tasklist_lock.
 */
static inline int cap_set_all(kernel_cap_t *effective,
			      kernel_cap_t *inheritable,
			      kernel_cap_t *permitted)
{
	struct task_struct *g, *target;
	int ret = -EPERM;
	int found = 0;

	spin_lock(&task_capability_lock);
	read_lock(&tasklist_lock);

	do_each_thread(g, target) {
		if (target == current
		    || is_container_init(target->group_leader))
			continue;
		found = 1;
		if (security_capset_check(target, effective, inheritable,
					  permitted))
			continue;
		ret = 0;
		security_capset_set(target, effective, inheritable, permitted);
	} while_each_thread(g, target);

	read_unlock(&tasklist_lock);
	spin_unlock(&task_capability_lock);

	if (!found)
		ret = 0;

	return ret;
}

/*
 * Given the target pid does not refer to the current process we
 * need more elaborate support... (This support is not present when
 * filesystem capabilities are configured.)
 */
static inline int do_sys_capset_other_tasks(pid_t pid, kernel_cap_t *effective,
					    kernel_cap_t *inheritable,
					    kernel_cap_t *permitted)
{
	struct task_struct *target;
	int ret;

	if (!capable(CAP_SETPCAP))
		return -EPERM;

	if (pid == -1)	          /* all procs other than current and init */
		return cap_set_all(effective, inheritable, permitted);

	else if (pid < 0)                    /* all procs in process group */
		return cap_set_pg(-pid, effective, inheritable, permitted);

	/* target != current */
	spin_lock(&task_capability_lock);
	read_lock(&tasklist_lock);

	target = find_task_by_vpid(pid);
	if (!target)
		ret = -ESRCH;
	else {
		ret = security_capset_check(target, effective, inheritable,
					    permitted);

		/* having verified that the proposed changes are legal,
		   we now put them into effect. */
		if (!ret)
			security_capset_set(target, effective, inheritable,
					    permitted);
	}

	read_unlock(&tasklist_lock);
	spin_unlock(&task_capability_lock);

	return ret;
}

#else /* ie., def CONFIG_SECURITY_FILE_CAPABILITIES */

/*
 * If we have configured with filesystem capability support, then the
 * only thing that can change the capabilities of the current process
 * is the current process. As such, we can't be in this code at the
 * same time as we are in the process of setting capabilities in this
 * process. The net result is that we can limit our use of locks to
 * when we are reading the caps of another process.
/*
 * The only thing that can change the capabilities of the current
 * process is the current process. As such, we can't be in this code
 * at the same time as we are in the process of setting capabilities
 * in this process. The net result is that we can limit our use of
 * locks to when we are reading the caps of another process.
 */
static inline int cap_get_target_pid(pid_t pid, kernel_cap_t *pEp,
				     kernel_cap_t *pIp, kernel_cap_t *pPp)
{
	int ret;

	if (pid && (pid != task_pid_vnr(current))) {
		struct task_struct *target;

		spin_lock(&task_capability_lock);
		read_lock(&tasklist_lock);
		rcu_read_lock();

		target = find_task_by_vpid(pid);
		if (!target)
			ret = -ESRCH;
		else
			ret = security_capget(target, pEp, pIp, pPp);

		read_unlock(&tasklist_lock);
		spin_unlock(&task_capability_lock);
		rcu_read_unlock();
	} else
		ret = security_capget(current, pEp, pIp, pPp);

	return ret;
}

/*
 * With filesystem capability support configured, the kernel does not
 * permit the changing of capabilities in one process by another
 * process. (CAP_SETPCAP has much less broad semantics when configured
 * this way.)
 */
static inline int do_sys_capset_other_tasks(pid_t pid,
					    kernel_cap_t *effective,
					    kernel_cap_t *inheritable,
					    kernel_cap_t *permitted)
{
	return -EPERM;
}

#endif /* ie., ndef CONFIG_SECURITY_FILE_CAPABILITIES */

/*
 * Atomically modify the effective capabilities returning the original
 * value. No permission check is performed here - it is assumed that the
 * caller is permitted to set the desired effective capabilities.
 */
kernel_cap_t cap_set_effective(const kernel_cap_t pE_new)
{
	kernel_cap_t pE_old;

	spin_lock(&task_capability_lock);

	pE_old = current->cap_effective;
	current->cap_effective = pE_new;

	spin_unlock(&task_capability_lock);

	return pE_old;
}

EXPORT_SYMBOL(cap_set_effective);

/**
 * sys_capget - get the capabilities of a given process.
 * @header: pointer to struct that contains capability version and
 *	target pid data
 * @dataptr: pointer to struct that contains the effective, permitted,
 *	and inheritable capabilities that are returned
 *
 * Returns 0 on success and < 0 on error.
 */
asmlinkage long sys_capget(cap_user_header_t header, cap_user_data_t dataptr)
SYSCALL_DEFINE2(capget, cap_user_header_t, header, cap_user_data_t, dataptr)
{
	int ret = 0;
	pid_t pid;
	unsigned tocopy;
	kernel_cap_t pE, pI, pP;

	ret = cap_validate_magic(header, &tocopy);
	if (ret != 0)
		return ret;
	if ((dataptr == NULL) || (ret != 0))
		return ((dataptr == NULL) && (ret == -EINVAL)) ? 0 : ret;

	if (get_user(pid, &header->pid))
		return -EFAULT;

	if (pid < 0)
		return -EINVAL;

	ret = cap_get_target_pid(pid, &pE, &pI, &pP);

	if (!ret) {
		struct __user_cap_data_struct kdata[_KERNEL_CAPABILITY_U32S];
		unsigned i;

		for (i = 0; i < tocopy; i++) {
			kdata[i].effective = pE.cap[i];
			kdata[i].permitted = pP.cap[i];
			kdata[i].inheritable = pI.cap[i];
		}

		/*
		 * Note, in the case, tocopy < _KERNEL_CAPABILITY_U32S,
		 * we silently drop the upper capabilities here. This
		 * has the effect of making older libcap
		 * implementations implicitly drop upper capability
		 * bits when they perform a: capget/modify/capset
		 * sequence.
		 *
		 * This behavior is considered fail-safe
		 * behavior. Upgrading the application to a newer
		 * version of libcap will enable access to the newer
		 * capabilities.
		 *
		 * An alternative would be to return an error here
		 * (-ERANGE), but that causes legacy applications to
		 * unexpectidly fail; the capget/modify/capset aborts
		 * unexpectedly fail; the capget/modify/capset aborts
		 * before modification is attempted and the application
		 * fails.
		 */
		if (copy_to_user(dataptr, kdata, tocopy
				 * sizeof(struct __user_cap_data_struct))) {
			return -EFAULT;
		}
	}

	return ret;
}

/**
 * sys_capset - set capabilities for a process or (*) a group of processes
 * @header: pointer to struct that contains capability version and
 *	target pid data
 * @data: pointer to struct that contains the effective, permitted,
 *	and inheritable capabilities
 *
 * Set capabilities for a given process, all processes, or all
 * processes in a given process group.
 *
 * The restrictions on setting capabilities are specified as:
 *
 * [pid is for the 'target' task.  'current' is the calling task.]
 *
 * I: any raised capabilities must be a subset of the (old current) permitted
 * P: any raised capabilities must be a subset of the (old current) permitted
 * E: must be set to a subset of (new target) permitted
 *
 * Returns 0 on success and < 0 on error.
 */
asmlinkage long sys_capset(cap_user_header_t header, const cap_user_data_t data)
{
	struct __user_cap_data_struct kdata[_KERNEL_CAPABILITY_U32S];
	unsigned i, tocopy;
	kernel_cap_t inheritable, permitted, effective;
 * Set capabilities for the current process only.  The ability to any other
 * process(es) has been deprecated and removed.
 *
 * The restrictions on setting capabilities are specified as:
 *
 * I: any raised capabilities must be a subset of the old permitted
 * P: any raised capabilities must be a subset of the old permitted
 * E: must be set to a subset of new permitted
 *
 * Returns 0 on success and < 0 on error.
 */
SYSCALL_DEFINE2(capset, cap_user_header_t, header, const cap_user_data_t, data)
{
	struct __user_cap_data_struct kdata[_KERNEL_CAPABILITY_U32S];
	unsigned i, tocopy, copybytes;
	kernel_cap_t inheritable, permitted, effective;
	struct cred *new;
	int ret;
	pid_t pid;

	ret = cap_validate_magic(header, &tocopy);
	if (ret != 0)
		return ret;

	if (get_user(pid, &header->pid))
		return -EFAULT;

	if (copy_from_user(&kdata, data, tocopy
			   * sizeof(struct __user_cap_data_struct))) {
		return -EFAULT;
	}
	/* may only affect current now */
	if (pid != 0 && pid != task_pid_vnr(current))
		return -EPERM;

	copybytes = tocopy * sizeof(struct __user_cap_data_struct);
	if (copybytes > sizeof(kdata))
		return -EFAULT;

	if (copy_from_user(&kdata, data, copybytes))
		return -EFAULT;

	for (i = 0; i < tocopy; i++) {
		effective.cap[i] = kdata[i].effective;
		permitted.cap[i] = kdata[i].permitted;
		inheritable.cap[i] = kdata[i].inheritable;
	}
	while (i < _KERNEL_CAPABILITY_U32S) {
		effective.cap[i] = 0;
		permitted.cap[i] = 0;
		inheritable.cap[i] = 0;
		i++;
	}

	if (pid && (pid != task_pid_vnr(current)))
		ret = do_sys_capset_other_tasks(pid, &effective, &inheritable,
						&permitted);
	else {
		/*
		 * This lock is required even when filesystem
		 * capability support is configured - it protects the
		 * sys_capget() call from returning incorrect data in
		 * the case that the targeted process is not the
		 * current one.
		 */
		spin_lock(&task_capability_lock);

		ret = security_capset_check(current, &effective, &inheritable,
					    &permitted);
		/*
		 * Having verified that the proposed changes are
		 * legal, we now put them into effect.
		 */
		if (!ret)
			security_capset_set(current, &effective, &inheritable,
					    &permitted);
		spin_unlock(&task_capability_lock);
	}


	effective.cap[CAP_LAST_U32] &= CAP_LAST_U32_VALID_MASK;
	permitted.cap[CAP_LAST_U32] &= CAP_LAST_U32_VALID_MASK;
	inheritable.cap[CAP_LAST_U32] &= CAP_LAST_U32_VALID_MASK;

	new = prepare_creds();
	if (!new)
		return -ENOMEM;

	ret = security_capset(new, current_cred(),
			      &effective, &inheritable, &permitted);
	if (ret < 0)
		goto error;

	audit_log_capset(new, current_cred());

	return commit_creds(new);

error:
	abort_creds(new);
	return ret;
}

/**
 * has_ns_capability - Does a task have a capability in a specific user ns
 * @t: The task in question
 * @ns: target user namespace
 * @cap: The capability to be tested for
 *
 * Return true if the specified task has the given superior capability
 * currently in effect to the specified user namespace, false if not.
 *
 * Note that this does not set PF_SUPERPRIV on the task.
 */
bool has_ns_capability(struct task_struct *t,
		       struct user_namespace *ns, int cap)
{
	int ret;

	rcu_read_lock();
	ret = security_capable(__task_cred(t), ns, cap);
	rcu_read_unlock();

	return (ret == 0);
}

/**
 * has_capability - Does a task have a capability in init_user_ns
 * @t: The task in question
 * @cap: The capability to be tested for
 *
 * Return true if the specified task has the given superior capability
 * currently in effect to the initial user namespace, false if not.
 *
 * Note that this does not set PF_SUPERPRIV on the task.
 */
bool has_capability(struct task_struct *t, int cap)
{
	return has_ns_capability(t, &init_user_ns, cap);
}

/**
 * has_ns_capability_noaudit - Does a task have a capability (unaudited)
 * in a specific user ns.
 * @t: The task in question
 * @ns: target user namespace
 * @cap: The capability to be tested for
 *
 * Return true if the specified task has the given superior capability
 * currently in effect to the specified user namespace, false if not.
 * Do not write an audit message for the check.
 *
 * Note that this does not set PF_SUPERPRIV on the task.
 */
bool has_ns_capability_noaudit(struct task_struct *t,
			       struct user_namespace *ns, int cap)
{
	int ret;

	rcu_read_lock();
	ret = security_capable_noaudit(__task_cred(t), ns, cap);
	rcu_read_unlock();

	return (ret == 0);
}

/**
 * has_capability_noaudit - Does a task have a capability (unaudited) in the
 * initial user ns
 * @t: The task in question
 * @cap: The capability to be tested for
 *
 * Return true if the specified task has the given superior capability
 * currently in effect to init_user_ns, false if not.  Don't write an
 * audit message for the check.
 *
 * Note that this does not set PF_SUPERPRIV on the task.
 */
bool has_capability_noaudit(struct task_struct *t, int cap)
{
	return has_ns_capability_noaudit(t, &init_user_ns, cap);
}

static bool ns_capable_common(struct user_namespace *ns, int cap, bool audit)
{
	int capable;

	if (unlikely(!cap_valid(cap))) {
		pr_crit("capable() called with invalid cap=%u\n", cap);
		BUG();
	}

	capable = audit ? security_capable(current_cred(), ns, cap) :
			  security_capable_noaudit(current_cred(), ns, cap);
	if (capable == 0) {
		current->flags |= PF_SUPERPRIV;
		return true;
	}
	return false;
}

/**
 * ns_capable - Determine if the current task has a superior capability in effect
 * @ns:  The usernamespace we want the capability in
 * @cap: The capability to be tested for
 *
 * Return true if the current task has the given superior capability currently
 * available for use, false if not.
 *
 * This sets PF_SUPERPRIV on the task if the capability is available on the
 * assumption that it's about to be used.
 */
bool ns_capable(struct user_namespace *ns, int cap)
{
	return ns_capable_common(ns, cap, true);
}
EXPORT_SYMBOL(ns_capable);

/**
 * ns_capable_noaudit - Determine if the current task has a superior capability
 * (unaudited) in effect
 * @ns:  The usernamespace we want the capability in
 * @cap: The capability to be tested for
 *
 * Return true if the current task has the given superior capability currently
 * available for use, false if not.
 *
 * This sets PF_SUPERPRIV on the task if the capability is available on the
 * assumption that it's about to be used.
 */
bool ns_capable_noaudit(struct user_namespace *ns, int cap)
{
	return ns_capable_common(ns, cap, false);
}
EXPORT_SYMBOL(ns_capable_noaudit);

/**
 * capable - Determine if the current task has a superior capability in effect
 * @cap: The capability to be tested for
 *
 * Return true if the current task has the given superior capability currently
 * available for use, false if not.
 *
 * This sets PF_SUPERPRIV on the task if the capability is available on the
 * assumption that it's about to be used.
 */
int capable(int cap)
{
	if (has_capability(current, cap)) {
		current->flags |= PF_SUPERPRIV;
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL(capable);
bool capable(int cap)
{
	return ns_capable(&init_user_ns, cap);
}
EXPORT_SYMBOL(capable);
#endif /* CONFIG_MULTIUSER */

/**
 * file_ns_capable - Determine if the file's opener had a capability in effect
 * @file:  The file we want to check
 * @ns:  The usernamespace we want the capability in
 * @cap: The capability to be tested for
 *
 * Return true if task that opened the file had a capability in effect
 * when the file was opened.
 *
 * This does not set PF_SUPERPRIV because the caller may not
 * actually be privileged.
 */
bool file_ns_capable(const struct file *file, struct user_namespace *ns,
		     int cap)
{
	if (WARN_ON_ONCE(!cap_valid(cap)))
		return false;

	if (security_capable(file->f_cred, ns, cap) == 0)
		return true;

	return false;
}
EXPORT_SYMBOL(file_ns_capable);

/**
 * privileged_wrt_inode_uidgid - Do capabilities in the namespace work over the inode?
 * @ns: The user namespace in question
 * @inode: The inode in question
 *
 * Return true if the inode uid and gid are within the namespace.
 */
bool privileged_wrt_inode_uidgid(struct user_namespace *ns, const struct inode *inode)
{
	return kuid_has_mapping(ns, inode->i_uid) &&
		kgid_has_mapping(ns, inode->i_gid);
}

/**
 * capable_wrt_inode_uidgid - Check nsown_capable and uid and gid mapped
 * @inode: The inode in question
 * @cap: The capability in question
 *
 * Return true if the current task has the given capability targeted at
 * its own user namespace and that the given inode's uid and gid are
 * mapped into the current user namespace.
 */
bool capable_wrt_inode_uidgid(const struct inode *inode, int cap)
{
	struct user_namespace *ns = current_user_ns();

	return ns_capable(ns, cap) && privileged_wrt_inode_uidgid(ns, inode);
}
EXPORT_SYMBOL(capable_wrt_inode_uidgid);

/**
 * ptracer_capable - Determine if the ptracer holds CAP_SYS_PTRACE in the namespace
 * @tsk: The task that may be ptraced
 * @ns: The user namespace to search for CAP_SYS_PTRACE in
 *
 * Return true if the task that is ptracing the current task had CAP_SYS_PTRACE
 * in the specified user namespace.
 */
bool ptracer_capable(struct task_struct *tsk, struct user_namespace *ns)
{
	int ret = 0;  /* An absent tracer adds no restrictions */
	const struct cred *cred;
	rcu_read_lock();
	cred = rcu_dereference(tsk->ptracer_cred);
	if (cred)
		ret = security_capable_noaudit(cred, ns, CAP_SYS_PTRACE);
	rcu_read_unlock();
	return (ret == 0);
}
