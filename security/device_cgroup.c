/*
 * dev_cgroup.c - device cgroup subsystem
 * device_cgroup.c - device cgroup subsystem
 *
 * Copyright 2007 IBM Corp
 */

#include <linux/device_cgroup.h>
#include <linux/cgroup.h>
#include <linux/ctype.h>
#include <linux/list.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/rcupdate.h>
#include <linux/mutex.h>

#define ACC_MKNOD 1
#define ACC_READ  2
#define ACC_WRITE 4
#define ACC_MASK (ACC_MKNOD | ACC_READ | ACC_WRITE)

#define DEV_BLOCK 1
#define DEV_CHAR  2
#define DEV_ALL   4  /* this represents all devices */

/*
 * whitelist locking rules:
 * cgroup_lock() cannot be taken under dev_cgroup->lock.
 * dev_cgroup->lock can be taken with or without cgroup_lock().
 *
 * modifications always require cgroup_lock
 * modifications to a list which is visible require the
 *   dev_cgroup->lock *and* cgroup_lock()
 * walking the list requires dev_cgroup->lock or cgroup_lock().
 *
 * reasoning: dev_whitelist_copy() needs to kmalloc, so needs
 *   a mutex, which the cgroup_lock() is.  Since modifying
 *   a visible list requires both locks, either lock can be
 *   taken for walking the list.
 */

struct dev_whitelist_item {
static DEFINE_MUTEX(devcgroup_mutex);

enum devcg_behavior {
	DEVCG_DEFAULT_NONE,
	DEVCG_DEFAULT_ALLOW,
	DEVCG_DEFAULT_DENY,
};

/*
 * exception list locking rules:
 * hold devcgroup_mutex for update/read.
 * hold rcu_read_lock() for read.
 */

struct dev_exception_item {
	u32 major, minor;
	short type;
	short access;
	struct list_head list;
	struct rcu_head rcu;
};

struct dev_cgroup {
	struct cgroup_subsys_state css;
	struct list_head whitelist;
	spinlock_t lock;
	struct list_head exceptions;
	enum devcg_behavior behavior;
};

static inline struct dev_cgroup *css_to_devcgroup(struct cgroup_subsys_state *s)
{
	return container_of(s, struct dev_cgroup, css);
}

static inline struct dev_cgroup *cgroup_to_devcgroup(struct cgroup *cgroup)
{
	return css_to_devcgroup(cgroup_subsys_state(cgroup, devices_subsys_id));
	return s ? container_of(s, struct dev_cgroup, css) : NULL;
}

static inline struct dev_cgroup *task_devcgroup(struct task_struct *task)
{
	return css_to_devcgroup(task_subsys_state(task, devices_subsys_id));
}

struct cgroup_subsys devices_subsys;

static int devcgroup_can_attach(struct cgroup_subsys *ss,
		struct cgroup *new_cgroup, struct task_struct *task)
{
	if (current != task && !capable(CAP_SYS_ADMIN))
			return -EPERM;

	return 0;
}

/*
 * called under cgroup_lock()
 */
static int dev_whitelist_copy(struct list_head *dest, struct list_head *orig)
{
	struct dev_whitelist_item *wh, *tmp, *new;

	list_for_each_entry(wh, orig, list) {
		new = kmalloc(sizeof(*wh), GFP_KERNEL);
		if (!new)
			goto free_and_exit;
		new->major = wh->major;
		new->minor = wh->minor;
		new->type = wh->type;
		new->access = wh->access;
	return css_to_devcgroup(task_css(task, devices_cgrp_id));
}

/*
 * called under devcgroup_mutex
 */
static int dev_exceptions_copy(struct list_head *dest, struct list_head *orig)
{
	struct dev_exception_item *ex, *tmp, *new;

	lockdep_assert_held(&devcgroup_mutex);

	list_for_each_entry(ex, orig, list) {
		new = kmemdup(ex, sizeof(*ex), GFP_KERNEL);
		if (!new)
			goto free_and_exit;
		list_add_tail(&new->list, dest);
	}

	return 0;

free_and_exit:
	list_for_each_entry_safe(wh, tmp, dest, list) {
		list_del(&wh->list);
		kfree(wh);
	list_for_each_entry_safe(ex, tmp, dest, list) {
		list_del(&ex->list);
		kfree(ex);
	}
	return -ENOMEM;
}

/* Stupid prototype - don't bother combining existing entries */
/*
 * called under cgroup_lock()
 * since the list is visible to other tasks, we need the spinlock also
 */
static int dev_whitelist_add(struct dev_cgroup *dev_cgroup,
			struct dev_whitelist_item *wh)
{
	struct dev_whitelist_item *whcopy, *walk;

	whcopy = kmalloc(sizeof(*whcopy), GFP_KERNEL);
	if (!whcopy)
		return -ENOMEM;

	memcpy(whcopy, wh, sizeof(*whcopy));
	spin_lock(&dev_cgroup->lock);
	list_for_each_entry(walk, &dev_cgroup->whitelist, list) {
		if (walk->type != wh->type)
			continue;
		if (walk->major != wh->major)
			continue;
		if (walk->minor != wh->minor)
			continue;

		walk->access |= wh->access;
		kfree(whcopy);
		whcopy = NULL;
	}

	if (whcopy != NULL)
		list_add_tail_rcu(&whcopy->list, &dev_cgroup->whitelist);
	spin_unlock(&dev_cgroup->lock);
	return 0;
}

static void whitelist_item_free(struct rcu_head *rcu)
{
	struct dev_whitelist_item *item;

	item = container_of(rcu, struct dev_whitelist_item, rcu);
	kfree(item);
}

/*
 * called under cgroup_lock()
 * since the list is visible to other tasks, we need the spinlock also
 */
static void dev_whitelist_rm(struct dev_cgroup *dev_cgroup,
			struct dev_whitelist_item *wh)
{
	struct dev_whitelist_item *walk, *tmp;

	spin_lock(&dev_cgroup->lock);
	list_for_each_entry_safe(walk, tmp, &dev_cgroup->whitelist, list) {
		if (walk->type == DEV_ALL)
			goto remove;
		if (walk->type != wh->type)
			continue;
		if (walk->major != ~0 && walk->major != wh->major)
			continue;
		if (walk->minor != ~0 && walk->minor != wh->minor)
			continue;

remove:
		walk->access &= ~wh->access;
		if (!walk->access) {
			list_del_rcu(&walk->list);
			call_rcu(&walk->rcu, whitelist_item_free);
		}
	}
	spin_unlock(&dev_cgroup->lock);
static void dev_exceptions_move(struct list_head *dest, struct list_head *orig)
{
	struct dev_exception_item *ex, *tmp;

	lockdep_assert_held(&devcgroup_mutex);

	list_for_each_entry_safe(ex, tmp, orig, list) {
		list_move_tail(&ex->list, dest);
	}
}

/*
 * called under devcgroup_mutex
 */
static int dev_exception_add(struct dev_cgroup *dev_cgroup,
			     struct dev_exception_item *ex)
{
	struct dev_exception_item *excopy, *walk;

	lockdep_assert_held(&devcgroup_mutex);

	excopy = kmemdup(ex, sizeof(*ex), GFP_KERNEL);
	if (!excopy)
		return -ENOMEM;

	list_for_each_entry(walk, &dev_cgroup->exceptions, list) {
		if (walk->type != ex->type)
			continue;
		if (walk->major != ex->major)
			continue;
		if (walk->minor != ex->minor)
			continue;

		walk->access |= ex->access;
		kfree(excopy);
		excopy = NULL;
	}

	if (excopy != NULL)
		list_add_tail_rcu(&excopy->list, &dev_cgroup->exceptions);
	return 0;
}

/*
 * called under devcgroup_mutex
 */
static void dev_exception_rm(struct dev_cgroup *dev_cgroup,
			     struct dev_exception_item *ex)
{
	struct dev_exception_item *walk, *tmp;

	lockdep_assert_held(&devcgroup_mutex);

	list_for_each_entry_safe(walk, tmp, &dev_cgroup->exceptions, list) {
		if (walk->type != ex->type)
			continue;
		if (walk->major != ex->major)
			continue;
		if (walk->minor != ex->minor)
			continue;

		walk->access &= ~ex->access;
		if (!walk->access) {
			list_del_rcu(&walk->list);
			kfree_rcu(walk, rcu);
		}
	}
}

static void __dev_exception_clean(struct dev_cgroup *dev_cgroup)
{
	struct dev_exception_item *ex, *tmp;

	list_for_each_entry_safe(ex, tmp, &dev_cgroup->exceptions, list) {
		list_del_rcu(&ex->list);
		kfree_rcu(ex, rcu);
	}
}

/**
 * dev_exception_clean - frees all entries of the exception list
 * @dev_cgroup: dev_cgroup with the exception list to be cleaned
 *
 * called under devcgroup_mutex
 */
static void dev_exception_clean(struct dev_cgroup *dev_cgroup)
{
	lockdep_assert_held(&devcgroup_mutex);

	__dev_exception_clean(dev_cgroup);
}

static inline bool is_devcg_online(const struct dev_cgroup *devcg)
{
	return (devcg->behavior != DEVCG_DEFAULT_NONE);
}

/**
 * devcgroup_online - initializes devcgroup's behavior and exceptions based on
 * 		      parent's
 * @css: css getting online
 * returns 0 in case of success, error code otherwise
 */
static int devcgroup_online(struct cgroup_subsys_state *css)
{
	struct dev_cgroup *dev_cgroup = css_to_devcgroup(css);
	struct dev_cgroup *parent_dev_cgroup = css_to_devcgroup(css->parent);
	int ret = 0;

	mutex_lock(&devcgroup_mutex);

	if (parent_dev_cgroup == NULL)
		dev_cgroup->behavior = DEVCG_DEFAULT_ALLOW;
	else {
		ret = dev_exceptions_copy(&dev_cgroup->exceptions,
					  &parent_dev_cgroup->exceptions);
		if (!ret)
			dev_cgroup->behavior = parent_dev_cgroup->behavior;
	}
	mutex_unlock(&devcgroup_mutex);

	return ret;
}

static void devcgroup_offline(struct cgroup_subsys_state *css)
{
	struct dev_cgroup *dev_cgroup = css_to_devcgroup(css);

	mutex_lock(&devcgroup_mutex);
	dev_cgroup->behavior = DEVCG_DEFAULT_NONE;
	mutex_unlock(&devcgroup_mutex);
}

/*
 * called from kernel/cgroup.c with cgroup_lock() held.
 */
static struct cgroup_subsys_state *devcgroup_create(struct cgroup_subsys *ss,
						struct cgroup *cgroup)
{
	struct dev_cgroup *dev_cgroup, *parent_dev_cgroup;
	struct cgroup *parent_cgroup;
	int ret;
static struct cgroup_subsys_state *
devcgroup_css_alloc(struct cgroup_subsys_state *parent_css)
{
	struct dev_cgroup *dev_cgroup;

	dev_cgroup = kzalloc(sizeof(*dev_cgroup), GFP_KERNEL);
	if (!dev_cgroup)
		return ERR_PTR(-ENOMEM);
	INIT_LIST_HEAD(&dev_cgroup->whitelist);
	parent_cgroup = cgroup->parent;

	if (parent_cgroup == NULL) {
		struct dev_whitelist_item *wh;
		wh = kmalloc(sizeof(*wh), GFP_KERNEL);
		if (!wh) {
			kfree(dev_cgroup);
			return ERR_PTR(-ENOMEM);
		}
		wh->minor = wh->major = ~0;
		wh->type = DEV_ALL;
		wh->access = ACC_MASK;
		list_add(&wh->list, &dev_cgroup->whitelist);
	} else {
		parent_dev_cgroup = cgroup_to_devcgroup(parent_cgroup);
		ret = dev_whitelist_copy(&dev_cgroup->whitelist,
				&parent_dev_cgroup->whitelist);
		if (ret) {
			kfree(dev_cgroup);
			return ERR_PTR(ret);
		}
	}

	spin_lock_init(&dev_cgroup->lock);
	return &dev_cgroup->css;
}

static void devcgroup_destroy(struct cgroup_subsys *ss,
			struct cgroup *cgroup)
{
	struct dev_cgroup *dev_cgroup;
	struct dev_whitelist_item *wh, *tmp;

	dev_cgroup = cgroup_to_devcgroup(cgroup);
	list_for_each_entry_safe(wh, tmp, &dev_cgroup->whitelist, list) {
		list_del(&wh->list);
		kfree(wh);
	}
	INIT_LIST_HEAD(&dev_cgroup->exceptions);
	dev_cgroup->behavior = DEVCG_DEFAULT_NONE;

	return &dev_cgroup->css;
}

static void devcgroup_css_free(struct cgroup_subsys_state *css)
{
	struct dev_cgroup *dev_cgroup = css_to_devcgroup(css);

	__dev_exception_clean(dev_cgroup);
	kfree(dev_cgroup);
}

#define DEVCG_ALLOW 1
#define DEVCG_DENY 2
#define DEVCG_LIST 3

#define MAJMINLEN 13
#define ACCLEN 4

static void set_access(char *acc, short access)
{
	int idx = 0;
	memset(acc, 0, ACCLEN);
	if (access & ACC_READ)
		acc[idx++] = 'r';
	if (access & ACC_WRITE)
		acc[idx++] = 'w';
	if (access & ACC_MKNOD)
		acc[idx++] = 'm';
}

static char type_to_char(short type)
{
	if (type == DEV_ALL)
		return 'a';
	if (type == DEV_CHAR)
		return 'c';
	if (type == DEV_BLOCK)
		return 'b';
	return 'X';
}

static void set_majmin(char *str, unsigned m)
{
	if (m == ~0)
		strcpy(str, "*");
	else
		sprintf(str, "%u", m);
}

static int devcgroup_seq_read(struct cgroup *cgroup, struct cftype *cft,
				struct seq_file *m)
{
	struct dev_cgroup *devcgroup = cgroup_to_devcgroup(cgroup);
	struct dev_whitelist_item *wh;
	char maj[MAJMINLEN], min[MAJMINLEN], acc[ACCLEN];

	rcu_read_lock();
	list_for_each_entry_rcu(wh, &devcgroup->whitelist, list) {
		set_access(acc, wh->access);
		set_majmin(maj, wh->major);
		set_majmin(min, wh->minor);
		seq_printf(m, "%c %s:%s %s\n", type_to_char(wh->type),
			   maj, min, acc);
static int devcgroup_seq_show(struct seq_file *m, void *v)
{
	struct dev_cgroup *devcgroup = css_to_devcgroup(seq_css(m));
	struct dev_exception_item *ex;
	char maj[MAJMINLEN], min[MAJMINLEN], acc[ACCLEN];

	rcu_read_lock();
	/*
	 * To preserve the compatibility:
	 * - Only show the "all devices" when the default policy is to allow
	 * - List the exceptions in case the default policy is to deny
	 * This way, the file remains as a "whitelist of devices"
	 */
	if (devcgroup->behavior == DEVCG_DEFAULT_ALLOW) {
		set_access(acc, ACC_MASK);
		set_majmin(maj, ~0);
		set_majmin(min, ~0);
		seq_printf(m, "%c %s:%s %s\n", type_to_char(DEV_ALL),
			   maj, min, acc);
	} else {
		list_for_each_entry_rcu(ex, &devcgroup->exceptions, list) {
			set_access(acc, ex->access);
			set_majmin(maj, ex->major);
			set_majmin(min, ex->minor);
			seq_printf(m, "%c %s:%s %s\n", type_to_char(ex->type),
				   maj, min, acc);
		}
	}
	rcu_read_unlock();

	return 0;
}

/*
 * may_access_whitelist:
 * does the access granted to dev_cgroup c contain the access
 * requested in whitelist item refwh.
 * return 1 if yes, 0 if no.
 * call with c->lock held
 */
static int may_access_whitelist(struct dev_cgroup *c,
				       struct dev_whitelist_item *refwh)
{
	struct dev_whitelist_item *whitem;

	list_for_each_entry(whitem, &c->whitelist, list) {
		if (whitem->type & DEV_ALL)
			return 1;
		if ((refwh->type & DEV_BLOCK) && !(whitem->type & DEV_BLOCK))
			continue;
		if ((refwh->type & DEV_CHAR) && !(whitem->type & DEV_CHAR))
			continue;
		if (whitem->major != ~0 && whitem->major != refwh->major)
			continue;
		if (whitem->minor != ~0 && whitem->minor != refwh->minor)
			continue;
		if (refwh->access & (~whitem->access))
			continue;
		return 1;
	}
	return 0;
/**
 * match_exception	- iterates the exception list trying to find a complete match
 * @exceptions: list of exceptions
 * @type: device type (DEV_BLOCK or DEV_CHAR)
 * @major: device file major number, ~0 to match all
 * @minor: device file minor number, ~0 to match all
 * @access: permission mask (ACC_READ, ACC_WRITE, ACC_MKNOD)
 *
 * It is considered a complete match if an exception is found that will
 * contain the entire range of provided parameters.
 *
 * Return: true in case it matches an exception completely
 */
static bool match_exception(struct list_head *exceptions, short type,
			    u32 major, u32 minor, short access)
{
	struct dev_exception_item *ex;

	list_for_each_entry_rcu(ex, exceptions, list) {
		if ((type & DEV_BLOCK) && !(ex->type & DEV_BLOCK))
			continue;
		if ((type & DEV_CHAR) && !(ex->type & DEV_CHAR))
			continue;
		if (ex->major != ~0 && ex->major != major)
			continue;
		if (ex->minor != ~0 && ex->minor != minor)
			continue;
		/* provided access cannot have more than the exception rule */
		if (access & (~ex->access))
			continue;
		return true;
	}
	return false;
}

/**
 * match_exception_partial - iterates the exception list trying to find a partial match
 * @exceptions: list of exceptions
 * @type: device type (DEV_BLOCK or DEV_CHAR)
 * @major: device file major number, ~0 to match all
 * @minor: device file minor number, ~0 to match all
 * @access: permission mask (ACC_READ, ACC_WRITE, ACC_MKNOD)
 *
 * It is considered a partial match if an exception's range is found to
 * contain *any* of the devices specified by provided parameters. This is
 * used to make sure no extra access is being granted that is forbidden by
 * any of the exception list.
 *
 * Return: true in case the provided range mat matches an exception completely
 */
static bool match_exception_partial(struct list_head *exceptions, short type,
				    u32 major, u32 minor, short access)
{
	struct dev_exception_item *ex;

	list_for_each_entry_rcu(ex, exceptions, list) {
		if ((type & DEV_BLOCK) && !(ex->type & DEV_BLOCK))
			continue;
		if ((type & DEV_CHAR) && !(ex->type & DEV_CHAR))
			continue;
		/*
		 * We must be sure that both the exception and the provided
		 * range aren't masking all devices
		 */
		if (ex->major != ~0 && major != ~0 && ex->major != major)
			continue;
		if (ex->minor != ~0 && minor != ~0 && ex->minor != minor)
			continue;
		/*
		 * In order to make sure the provided range isn't matching
		 * an exception, all its access bits shouldn't match the
		 * exception's access bits
		 */
		if (!(access & ex->access))
			continue;
		return true;
	}
	return false;
}

/**
 * verify_new_ex - verifies if a new exception is allowed by parent cgroup's permissions
 * @dev_cgroup: dev cgroup to be tested against
 * @refex: new exception
 * @behavior: behavior of the exception's dev_cgroup
 *
 * This is used to make sure a child cgroup won't have more privileges
 * than its parent
 */
static bool verify_new_ex(struct dev_cgroup *dev_cgroup,
		          struct dev_exception_item *refex,
		          enum devcg_behavior behavior)
{
	bool match = false;

	RCU_LOCKDEP_WARN(!rcu_read_lock_held() &&
			 !lockdep_is_held(&devcgroup_mutex),
			 "device_cgroup:verify_new_ex called without proper synchronization");

	if (dev_cgroup->behavior == DEVCG_DEFAULT_ALLOW) {
		if (behavior == DEVCG_DEFAULT_ALLOW) {
			/*
			 * new exception in the child doesn't matter, only
			 * adding extra restrictions
			 */ 
			return true;
		} else {
			/*
			 * new exception in the child will add more devices
			 * that can be acessed, so it can't match any of
			 * parent's exceptions, even slightly
			 */ 
			match = match_exception_partial(&dev_cgroup->exceptions,
							refex->type,
							refex->major,
							refex->minor,
							refex->access);

			if (match)
				return false;
			return true;
		}
	} else {
		/*
		 * Only behavior == DEVCG_DEFAULT_DENY allowed here, therefore
		 * the new exception will add access to more devices and must
		 * be contained completely in an parent's exception to be
		 * allowed
		 */
		match = match_exception(&dev_cgroup->exceptions, refex->type,
					refex->major, refex->minor,
					refex->access);

		if (match)
			/* parent has an exception that matches the proposed */
			return true;
		else
			return false;
	}
	return false;
}

/*
 * parent_has_perm:
 * when adding a new allow rule to a device whitelist, the rule
 * must be allowed in the parent device
 */
static int parent_has_perm(struct dev_cgroup *childcg,
				  struct dev_whitelist_item *wh)
{
	struct cgroup *pcg = childcg->css.cgroup->parent;
	struct dev_cgroup *parent;
	int ret;

	if (!pcg)
		return 1;
	parent = cgroup_to_devcgroup(pcg);
	spin_lock(&parent->lock);
	ret = may_access_whitelist(parent, wh);
	spin_unlock(&parent->lock);
	return ret;
}

/*
 * Modify the whitelist using allow/deny rules.
 * CAP_SYS_ADMIN is needed for this.  It's at least separate from CAP_MKNOD
 * so we can give a container CAP_MKNOD to let it create devices but not
 * modify the whitelist.
 * It seems likely we'll want to add a CAP_CONTAINER capability to allow
 * us to also grant CAP_SYS_ADMIN to containers without giving away the
 * device whitelist controls, but for now we'll stick with CAP_SYS_ADMIN
 * when adding a new allow rule to a device exception list, the rule
 * must be allowed in the parent device
 */
static int parent_has_perm(struct dev_cgroup *childcg,
				  struct dev_exception_item *ex)
{
	struct dev_cgroup *parent = css_to_devcgroup(childcg->css.parent);

	if (!parent)
		return 1;
	return verify_new_ex(parent, ex, childcg->behavior);
}

/**
 * parent_allows_removal - verify if it's ok to remove an exception
 * @childcg: child cgroup from where the exception will be removed
 * @ex: exception being removed
 *
 * When removing an exception in cgroups with default ALLOW policy, it must
 * be checked if removing it will give the child cgroup more access than the
 * parent.
 *
 * Return: true if it's ok to remove exception, false otherwise
 */
static bool parent_allows_removal(struct dev_cgroup *childcg,
				  struct dev_exception_item *ex)
{
	struct dev_cgroup *parent = css_to_devcgroup(childcg->css.parent);

	if (!parent)
		return true;

	/* It's always allowed to remove access to devices */
	if (childcg->behavior == DEVCG_DEFAULT_DENY)
		return true;

	/*
	 * Make sure you're not removing part or a whole exception existing in
	 * the parent cgroup
	 */
	return !match_exception_partial(&parent->exceptions, ex->type,
					ex->major, ex->minor, ex->access);
}

/**
 * may_allow_all - checks if it's possible to change the behavior to
 *		   allow based on parent's rules.
 * @parent: device cgroup's parent
 * returns: != 0 in case it's allowed, 0 otherwise
 */
static inline int may_allow_all(struct dev_cgroup *parent)
{
	if (!parent)
		return 1;
	return parent->behavior == DEVCG_DEFAULT_ALLOW;
}

/**
 * revalidate_active_exceptions - walks through the active exception list and
 * 				  revalidates the exceptions based on parent's
 * 				  behavior and exceptions. The exceptions that
 * 				  are no longer valid will be removed.
 * 				  Called with devcgroup_mutex held.
 * @devcg: cgroup which exceptions will be checked
 *
 * This is one of the three key functions for hierarchy implementation.
 * This function is responsible for re-evaluating all the cgroup's active
 * exceptions due to a parent's exception change.
 * Refer to Documentation/cgroups/devices.txt for more details.
 */
static void revalidate_active_exceptions(struct dev_cgroup *devcg)
{
	struct dev_exception_item *ex;
	struct list_head *this, *tmp;

	list_for_each_safe(this, tmp, &devcg->exceptions) {
		ex = container_of(this, struct dev_exception_item, list);
		if (!parent_has_perm(devcg, ex))
			dev_exception_rm(devcg, ex);
	}
}

/**
 * propagate_exception - propagates a new exception to the children
 * @devcg_root: device cgroup that added a new exception
 * @ex: new exception to be propagated
 *
 * returns: 0 in case of success, != 0 in case of error
 */
static int propagate_exception(struct dev_cgroup *devcg_root,
			       struct dev_exception_item *ex)
{
	struct cgroup_subsys_state *pos;
	int rc = 0;

	rcu_read_lock();

	css_for_each_descendant_pre(pos, &devcg_root->css) {
		struct dev_cgroup *devcg = css_to_devcgroup(pos);

		/*
		 * Because devcgroup_mutex is held, no devcg will become
		 * online or offline during the tree walk (see on/offline
		 * methods), and online ones are safe to access outside RCU
		 * read lock without bumping refcnt.
		 */
		if (pos == &devcg_root->css || !is_devcg_online(devcg))
			continue;

		rcu_read_unlock();

		/*
		 * in case both root's behavior and devcg is allow, a new
		 * restriction means adding to the exception list
		 */
		if (devcg_root->behavior == DEVCG_DEFAULT_ALLOW &&
		    devcg->behavior == DEVCG_DEFAULT_ALLOW) {
			rc = dev_exception_add(devcg, ex);
			if (rc)
				return rc;
		} else {
			/*
			 * in the other possible cases:
			 * root's behavior: allow, devcg's: deny
			 * root's behavior: deny, devcg's: deny
			 * the exception will be removed
			 */
			dev_exception_rm(devcg, ex);
		}
		revalidate_active_exceptions(devcg);

		rcu_read_lock();
	}

	rcu_read_unlock();
	return rc;
}

/*
 * Modify the exception list using allow/deny rules.
 * CAP_SYS_ADMIN is needed for this.  It's at least separate from CAP_MKNOD
 * so we can give a container CAP_MKNOD to let it create devices but not
 * modify the exception list.
 * It seems likely we'll want to add a CAP_CONTAINER capability to allow
 * us to also grant CAP_SYS_ADMIN to containers without giving away the
 * device exception list controls, but for now we'll stick with CAP_SYS_ADMIN
 *
 * Taking rules away is always allowed (given CAP_SYS_ADMIN).  Granting
 * new access is only allowed if you're in the top-level cgroup, or your
 * parent cgroup has the access you're asking for.
 */
static int devcgroup_update_access(struct dev_cgroup *devcgroup,
				   int filetype, const char *buffer)
{
	struct dev_cgroup *cur_devcgroup;
	const char *b;
	char *endp;
	int retval = 0, count;
	struct dev_whitelist_item wh;
				   int filetype, char *buffer)
{
	const char *b;
	char temp[12];		/* 11 + 1 characters needed for a u32 */
	int count, rc = 0;
	struct dev_exception_item ex;
	struct dev_cgroup *parent = css_to_devcgroup(devcgroup->css.parent);
	struct dev_cgroup tmp_devcgrp;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	cur_devcgroup = task_devcgroup(current);

	memset(&wh, 0, sizeof(wh));
	memset(&ex, 0, sizeof(ex));
	memset(&tmp_devcgrp, 0, sizeof(tmp_devcgrp));
	b = buffer;

	switch (*b) {
	case 'a':
		wh.type = DEV_ALL;
		wh.access = ACC_MASK;
		wh.major = ~0;
		wh.minor = ~0;
		goto handle;
	case 'b':
		wh.type = DEV_BLOCK;
		break;
	case 'c':
		wh.type = DEV_CHAR;
		switch (filetype) {
		case DEVCG_ALLOW:
			if (css_has_online_children(&devcgroup->css))
				return -EINVAL;

			if (!may_allow_all(parent))
				return -EPERM;
			if (!parent) {
				devcgroup->behavior = DEVCG_DEFAULT_ALLOW;
				dev_exception_clean(devcgroup);
				break;
			}

			INIT_LIST_HEAD(&tmp_devcgrp.exceptions);
			rc = dev_exceptions_copy(&tmp_devcgrp.exceptions,
						 &devcgroup->exceptions);
			if (rc)
				return rc;
			dev_exception_clean(devcgroup);
			rc = dev_exceptions_copy(&devcgroup->exceptions,
						 &parent->exceptions);
			if (rc) {
				dev_exceptions_move(&devcgroup->exceptions,
						    &tmp_devcgrp.exceptions);
				return rc;
			}
			devcgroup->behavior = DEVCG_DEFAULT_ALLOW;
			dev_exception_clean(&tmp_devcgrp);
			break;
		case DEVCG_DENY:
			if (css_has_online_children(&devcgroup->css))
				return -EINVAL;

			dev_exception_clean(devcgroup);
			devcgroup->behavior = DEVCG_DEFAULT_DENY;
			break;
		default:
			return -EINVAL;
		}
		return 0;
	case 'b':
		ex.type = DEV_BLOCK;
		break;
	case 'c':
		ex.type = DEV_CHAR;
		break;
	default:
		return -EINVAL;
	}
	b++;
	if (!isspace(*b))
		return -EINVAL;
	b++;
	if (*b == '*') {
		wh.major = ~0;
		b++;
	} else if (isdigit(*b)) {
		wh.major = simple_strtoul(b, &endp, 10);
		b = endp;
		ex.major = ~0;
		b++;
	} else if (isdigit(*b)) {
		memset(temp, 0, sizeof(temp));
		for (count = 0; count < sizeof(temp) - 1; count++) {
			temp[count] = *b;
			b++;
			if (!isdigit(*b))
				break;
		}
		rc = kstrtou32(temp, 10, &ex.major);
		if (rc)
			return -EINVAL;
	} else {
		return -EINVAL;
	}
	if (*b != ':')
		return -EINVAL;
	b++;

	/* read minor */
	if (*b == '*') {
		wh.minor = ~0;
		b++;
	} else if (isdigit(*b)) {
		wh.minor = simple_strtoul(b, &endp, 10);
		b = endp;
		ex.minor = ~0;
		b++;
	} else if (isdigit(*b)) {
		memset(temp, 0, sizeof(temp));
		for (count = 0; count < sizeof(temp) - 1; count++) {
			temp[count] = *b;
			b++;
			if (!isdigit(*b))
				break;
		}
		rc = kstrtou32(temp, 10, &ex.minor);
		if (rc)
			return -EINVAL;
	} else {
		return -EINVAL;
	}
	if (!isspace(*b))
		return -EINVAL;
	for (b++, count = 0; count < 3; count++, b++) {
		switch (*b) {
		case 'r':
			wh.access |= ACC_READ;
			break;
		case 'w':
			wh.access |= ACC_WRITE;
			break;
		case 'm':
			wh.access |= ACC_MKNOD;
			ex.access |= ACC_READ;
			break;
		case 'w':
			ex.access |= ACC_WRITE;
			break;
		case 'm':
			ex.access |= ACC_MKNOD;
			break;
		case '\n':
		case '\0':
			count = 3;
			break;
		default:
			return -EINVAL;
		}
	}

handle:
	retval = 0;
	switch (filetype) {
	case DEVCG_ALLOW:
		if (!parent_has_perm(devcgroup, &wh))
			return -EPERM;
		return dev_whitelist_add(devcgroup, &wh);
	case DEVCG_DENY:
		dev_whitelist_rm(devcgroup, &wh);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int devcgroup_access_write(struct cgroup *cgrp, struct cftype *cft,
				  const char *buffer)
{
	int retval;
	if (!cgroup_lock_live_group(cgrp))
		return -ENODEV;
	retval = devcgroup_update_access(cgroup_to_devcgroup(cgrp),
					 cft->private, buffer);
	cgroup_unlock();
	return retval;
	switch (filetype) {
	case DEVCG_ALLOW:
		/*
		 * If the default policy is to allow by default, try to remove
		 * an matching exception instead. And be silent about it: we
		 * don't want to break compatibility
		 */
		if (devcgroup->behavior == DEVCG_DEFAULT_ALLOW) {
			/* Check if the parent allows removing it first */
			if (!parent_allows_removal(devcgroup, &ex))
				return -EPERM;
			dev_exception_rm(devcgroup, &ex);
			break;
		}

		if (!parent_has_perm(devcgroup, &ex))
			return -EPERM;
		rc = dev_exception_add(devcgroup, &ex);
		break;
	case DEVCG_DENY:
		/*
		 * If the default policy is to deny by default, try to remove
		 * an matching exception instead. And be silent about it: we
		 * don't want to break compatibility
		 */
		if (devcgroup->behavior == DEVCG_DEFAULT_DENY)
			dev_exception_rm(devcgroup, &ex);
		else
			rc = dev_exception_add(devcgroup, &ex);

		if (rc)
			break;
		/* we only propagate new restrictions */
		rc = propagate_exception(devcgroup, &ex);
		break;
	default:
		rc = -EINVAL;
	}
	return rc;
}

static ssize_t devcgroup_access_write(struct kernfs_open_file *of,
				      char *buf, size_t nbytes, loff_t off)
{
	int retval;

	mutex_lock(&devcgroup_mutex);
	retval = devcgroup_update_access(css_to_devcgroup(of_css(of)),
					 of_cft(of)->private, strstrip(buf));
	mutex_unlock(&devcgroup_mutex);
	return retval ?: nbytes;
}

static struct cftype dev_cgroup_files[] = {
	{
		.name = "allow",
		.write_string  = devcgroup_access_write,
		.write = devcgroup_access_write,
		.private = DEVCG_ALLOW,
	},
	{
		.name = "deny",
		.write_string = devcgroup_access_write,
		.write = devcgroup_access_write,
		.private = DEVCG_DENY,
	},
	{
		.name = "list",
		.read_seq_string = devcgroup_seq_read,
		.private = DEVCG_LIST,
	},
};

static int devcgroup_populate(struct cgroup_subsys *ss,
				struct cgroup *cgroup)
{
	return cgroup_add_files(cgroup, ss, dev_cgroup_files,
					ARRAY_SIZE(dev_cgroup_files));
}

struct cgroup_subsys devices_subsys = {
	.name = "devices",
	.can_attach = devcgroup_can_attach,
	.create = devcgroup_create,
	.destroy  = devcgroup_destroy,
	.populate = devcgroup_populate,
	.subsys_id = devices_subsys_id,
};

int devcgroup_inode_permission(struct inode *inode, int mask)
{
	struct dev_cgroup *dev_cgroup;
	struct dev_whitelist_item *wh;

	dev_t device = inode->i_rdev;
	if (!device)
		return 0;
	if (!S_ISBLK(inode->i_mode) && !S_ISCHR(inode->i_mode))
		return 0;

	rcu_read_lock();

	dev_cgroup = task_devcgroup(current);

	list_for_each_entry_rcu(wh, &dev_cgroup->whitelist, list) {
		if (wh->type & DEV_ALL)
			goto acc_check;
		if ((wh->type & DEV_BLOCK) && !S_ISBLK(inode->i_mode))
			continue;
		if ((wh->type & DEV_CHAR) && !S_ISCHR(inode->i_mode))
			continue;
		if (wh->major != ~0 && wh->major != imajor(inode))
			continue;
		if (wh->minor != ~0 && wh->minor != iminor(inode))
			continue;
acc_check:
		if ((mask & MAY_WRITE) && !(wh->access & ACC_WRITE))
			continue;
		if ((mask & MAY_READ) && !(wh->access & ACC_READ))
			continue;
		rcu_read_unlock();
		return 0;
	}

	rcu_read_unlock();

	return -EPERM;
		.seq_show = devcgroup_seq_show,
		.private = DEVCG_LIST,
	},
	{ }	/* terminate */
};

struct cgroup_subsys devices_cgrp_subsys = {
	.css_alloc = devcgroup_css_alloc,
	.css_free = devcgroup_css_free,
	.css_online = devcgroup_online,
	.css_offline = devcgroup_offline,
	.legacy_cftypes = dev_cgroup_files,
};

/**
 * __devcgroup_check_permission - checks if an inode operation is permitted
 * @dev_cgroup: the dev cgroup to be tested against
 * @type: device type
 * @major: device major number
 * @minor: device minor number
 * @access: combination of ACC_WRITE, ACC_READ and ACC_MKNOD
 *
 * returns 0 on success, -EPERM case the operation is not permitted
 */
static int __devcgroup_check_permission(short type, u32 major, u32 minor,
				        short access)
{
	struct dev_cgroup *dev_cgroup;
	bool rc;

	rcu_read_lock();
	dev_cgroup = task_devcgroup(current);
	if (dev_cgroup->behavior == DEVCG_DEFAULT_ALLOW)
		/* Can't match any of the exceptions, even partially */
		rc = !match_exception_partial(&dev_cgroup->exceptions,
					      type, major, minor, access);
	else
		/* Need to match completely one exception to be allowed */
		rc = match_exception(&dev_cgroup->exceptions, type, major,
				     minor, access);
	rcu_read_unlock();

	if (!rc)
		return -EPERM;

	return 0;
}

int __devcgroup_inode_permission(struct inode *inode, int mask)
{
	short type, access = 0;

	if (S_ISBLK(inode->i_mode))
		type = DEV_BLOCK;
	if (S_ISCHR(inode->i_mode))
		type = DEV_CHAR;
	if (mask & MAY_WRITE)
		access |= ACC_WRITE;
	if (mask & MAY_READ)
		access |= ACC_READ;

	return __devcgroup_check_permission(type, imajor(inode), iminor(inode),
			access);
}

int devcgroup_inode_mknod(int mode, dev_t dev)
{
	struct dev_cgroup *dev_cgroup;
	struct dev_whitelist_item *wh;

	rcu_read_lock();

	dev_cgroup = task_devcgroup(current);

	list_for_each_entry(wh, &dev_cgroup->whitelist, list) {
		if (wh->type & DEV_ALL)
			goto acc_check;
		if ((wh->type & DEV_BLOCK) && !S_ISBLK(mode))
			continue;
		if ((wh->type & DEV_CHAR) && !S_ISCHR(mode))
			continue;
		if (wh->major != ~0 && wh->major != MAJOR(dev))
			continue;
		if (wh->minor != ~0 && wh->minor != MINOR(dev))
			continue;
acc_check:
		if (!(wh->access & ACC_MKNOD))
			continue;
		rcu_read_unlock();
		return 0;
	}

	rcu_read_unlock();

	return -EPERM;
	short type;

	if (!S_ISBLK(mode) && !S_ISCHR(mode))
		return 0;

	if (S_ISBLK(mode))
		type = DEV_BLOCK;
	else
		type = DEV_CHAR;

	return __devcgroup_check_permission(type, MAJOR(dev), MINOR(dev),
			ACC_MKNOD);

}
