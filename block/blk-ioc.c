/*
 * Functions related to io context handling
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/bootmem.h>	/* for max_pfn/max_low_pfn */
#include <linux/slab.h>
#include <linux/delay.h>

#include "blk.h"

/*
 * For io context allocations
 */
static struct kmem_cache *iocontext_cachep;

static void cfq_dtor(struct io_context *ioc)
{
	if (!hlist_empty(&ioc->cic_list)) {
		struct cfq_io_context *cic;

		cic = list_entry(ioc->cic_list.first, struct cfq_io_context,
								cic_list);
		cic->dtor(ioc);
	}
}

/*
 * IO Context helper functions. put_io_context() returns 1 if there are no
 * more users of this io context, 0 otherwise.
 */
int put_io_context(struct io_context *ioc)
{
	if (ioc == NULL)
		return 1;

	BUG_ON(atomic_read(&ioc->refcount) == 0);

	if (atomic_dec_and_test(&ioc->refcount)) {
		rcu_read_lock();
		if (ioc->aic && ioc->aic->dtor)
			ioc->aic->dtor(ioc->aic);
		cfq_dtor(ioc);
		rcu_read_unlock();

		kmem_cache_free(iocontext_cachep, ioc);
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL(put_io_context);

static void cfq_exit(struct io_context *ioc)
{
	rcu_read_lock();

	if (!hlist_empty(&ioc->cic_list)) {
		struct cfq_io_context *cic;

		cic = list_entry(ioc->cic_list.first, struct cfq_io_context,
								cic_list);
		cic->exit(ioc);
	}
	rcu_read_unlock();
}

/* Called by the exitting task */
void exit_io_context(void)
{
	struct io_context *ioc;

	task_lock(current);
	ioc = current->io_context;
	current->io_context = NULL;
	task_unlock(current);

	if (atomic_dec_and_test(&ioc->nr_tasks)) {
		if (ioc->aic && ioc->aic->exit)
			ioc->aic->exit(ioc->aic);
		cfq_exit(ioc);

		put_io_context(ioc);
	}
}

struct io_context *alloc_io_context(gfp_t gfp_flags, int node)
{
	struct io_context *ret;

	ret = kmem_cache_alloc_node(iocontext_cachep, gfp_flags, node);
	if (ret) {
		atomic_set(&ret->refcount, 1);
		atomic_set(&ret->nr_tasks, 1);
		spin_lock_init(&ret->lock);
		ret->ioprio_changed = 0;
		ret->ioprio = 0;
		ret->last_waited = jiffies; /* doesn't matter... */
		ret->nr_batch_requests = 0; /* because this is 0 */
		ret->aic = NULL;
		INIT_RADIX_TREE(&ret->radix_root, GFP_ATOMIC | __GFP_HIGH);
		INIT_HLIST_HEAD(&ret->cic_list);
		ret->ioc_data = NULL;
	}

	return ret;
}

/*
 * If the current task has no IO context then create one and initialise it.
 * Otherwise, return its existing IO context.
 *
 * This returned IO context doesn't have a specifically elevated refcount,
 * but since the current task itself holds a reference, the context can be
 * used in general code, so long as it stays within `current` context.
 */
struct io_context *current_io_context(gfp_t gfp_flags, int node)
{
	struct task_struct *tsk = current;
	struct io_context *ret;

	ret = tsk->io_context;
	if (likely(ret))
		return ret;

	ret = alloc_io_context(gfp_flags, node);
	if (ret) {
		/* make sure set_task_ioprio() sees the settings above */
		smp_wmb();
		tsk->io_context = ret;
	}

	return ret;
}

/*
 * If the current task has no IO context then create one and initialise it.
 * If it does have a context, take a ref on it.
 *
 * This is always called in the context of the task which submitted the I/O.
 */
struct io_context *get_io_context(gfp_t gfp_flags, int node)
{
	struct io_context *ret = NULL;

	/*
	 * Check for unlikely race with exiting task. ioc ref count is
	 * zero when ioc is being detached.
	 */
	do {
		ret = current_io_context(gfp_flags, node);
		if (unlikely(!ret))
			break;
	} while (!atomic_inc_not_zero(&ret->refcount));

	return ret;
}
EXPORT_SYMBOL(get_io_context);

void copy_io_context(struct io_context **pdst, struct io_context **psrc)
{
	struct io_context *src = *psrc;
	struct io_context *dst = *pdst;

	if (src) {
		BUG_ON(atomic_read(&src->refcount) == 0);
		atomic_inc(&src->refcount);
		put_io_context(dst);
		*pdst = src;
	}
}
EXPORT_SYMBOL(copy_io_context);
/**
 * get_io_context - increment reference count to io_context
 * @ioc: io_context to get
 *
 * Increment reference count to @ioc.
 */
void get_io_context(struct io_context *ioc)
{
	BUG_ON(atomic_long_read(&ioc->refcount) <= 0);
	atomic_long_inc(&ioc->refcount);
}
EXPORT_SYMBOL(get_io_context);

static void icq_free_icq_rcu(struct rcu_head *head)
{
	struct io_cq *icq = container_of(head, struct io_cq, __rcu_head);

	kmem_cache_free(icq->__rcu_icq_cache, icq);
}

/* Exit an icq. Called with both ioc and q locked. */
static void ioc_exit_icq(struct io_cq *icq)
{
	struct elevator_type *et = icq->q->elevator->type;

	if (icq->flags & ICQ_EXITED)
		return;

	if (et->ops.elevator_exit_icq_fn)
		et->ops.elevator_exit_icq_fn(icq);

	icq->flags |= ICQ_EXITED;
}

/* Release an icq.  Called with both ioc and q locked. */
static void ioc_destroy_icq(struct io_cq *icq)
{
	struct io_context *ioc = icq->ioc;
	struct request_queue *q = icq->q;
	struct elevator_type *et = q->elevator->type;

	lockdep_assert_held(&ioc->lock);
	lockdep_assert_held(q->queue_lock);

	radix_tree_delete(&ioc->icq_tree, icq->q->id);
	hlist_del_init(&icq->ioc_node);
	list_del_init(&icq->q_node);

	/*
	 * Both setting lookup hint to and clearing it from @icq are done
	 * under queue_lock.  If it's not pointing to @icq now, it never
	 * will.  Hint assignment itself can race safely.
	 */
	if (rcu_access_pointer(ioc->icq_hint) == icq)
		rcu_assign_pointer(ioc->icq_hint, NULL);

	ioc_exit_icq(icq);

	/*
	 * @icq->q might have gone away by the time RCU callback runs
	 * making it impossible to determine icq_cache.  Record it in @icq.
	 */
	icq->__rcu_icq_cache = et->icq_cache;
	call_rcu(&icq->__rcu_head, icq_free_icq_rcu);
}

/*
 * Slow path for ioc release in put_io_context().  Performs double-lock
 * dancing to unlink all icq's and then frees ioc.
 */
static void ioc_release_fn(struct work_struct *work)
{
	struct io_context *ioc = container_of(work, struct io_context,
					      release_work);
	unsigned long flags;

	/*
	 * Exiting icq may call into put_io_context() through elevator
	 * which will trigger lockdep warning.  The ioc's are guaranteed to
	 * be different, use a different locking subclass here.  Use
	 * irqsave variant as there's no spin_lock_irq_nested().
	 */
	spin_lock_irqsave_nested(&ioc->lock, flags, 1);

	while (!hlist_empty(&ioc->icq_list)) {
		struct io_cq *icq = hlist_entry(ioc->icq_list.first,
						struct io_cq, ioc_node);
		struct request_queue *q = icq->q;

		if (spin_trylock(q->queue_lock)) {
			ioc_destroy_icq(icq);
			spin_unlock(q->queue_lock);
		} else {
			spin_unlock_irqrestore(&ioc->lock, flags);
			cpu_chill();
			spin_lock_irqsave_nested(&ioc->lock, flags, 1);
		}
	}

	spin_unlock_irqrestore(&ioc->lock, flags);

	kmem_cache_free(iocontext_cachep, ioc);
}

/**
 * put_io_context - put a reference of io_context
 * @ioc: io_context to put
 *
 * Decrement reference count of @ioc and release it if the count reaches
 * zero.
 */
void put_io_context(struct io_context *ioc)
{
	unsigned long flags;
	bool free_ioc = false;

	if (ioc == NULL)
		return;

	BUG_ON(atomic_long_read(&ioc->refcount) <= 0);

	/*
	 * Releasing ioc requires reverse order double locking and we may
	 * already be holding a queue_lock.  Do it asynchronously from wq.
	 */
	if (atomic_long_dec_and_test(&ioc->refcount)) {
		spin_lock_irqsave(&ioc->lock, flags);
		if (!hlist_empty(&ioc->icq_list))
			queue_work(system_power_efficient_wq,
					&ioc->release_work);
		else
			free_ioc = true;
		spin_unlock_irqrestore(&ioc->lock, flags);
	}

	if (free_ioc)
		kmem_cache_free(iocontext_cachep, ioc);
}
EXPORT_SYMBOL(put_io_context);

/**
 * put_io_context_active - put active reference on ioc
 * @ioc: ioc of interest
 *
 * Undo get_io_context_active().  If active reference reaches zero after
 * put, @ioc can never issue further IOs and ioscheds are notified.
 */
void put_io_context_active(struct io_context *ioc)
{
	unsigned long flags;
	struct io_cq *icq;

	if (!atomic_dec_and_test(&ioc->active_ref)) {
		put_io_context(ioc);
		return;
	}

	/*
	 * Need ioc lock to walk icq_list and q lock to exit icq.  Perform
	 * reverse double locking.  Read comment in ioc_release_fn() for
	 * explanation on the nested locking annotation.
	 */
retry:
	spin_lock_irqsave_nested(&ioc->lock, flags, 1);
	hlist_for_each_entry(icq, &ioc->icq_list, ioc_node) {
		if (icq->flags & ICQ_EXITED)
			continue;
		if (spin_trylock(icq->q->queue_lock)) {
			ioc_exit_icq(icq);
			spin_unlock(icq->q->queue_lock);
		} else {
			spin_unlock_irqrestore(&ioc->lock, flags);
			cpu_chill();
			goto retry;
		}
	}
	spin_unlock_irqrestore(&ioc->lock, flags);

	put_io_context(ioc);
}

/* Called by the exiting task */
void exit_io_context(struct task_struct *task)
{
	struct io_context *ioc;

	task_lock(task);
	ioc = task->io_context;
	task->io_context = NULL;
	task_unlock(task);

	atomic_dec(&ioc->nr_tasks);
	put_io_context_active(ioc);
}

/**
 * ioc_clear_queue - break any ioc association with the specified queue
 * @q: request_queue being cleared
 *
 * Walk @q->icq_list and exit all io_cq's.  Must be called with @q locked.
 */
void ioc_clear_queue(struct request_queue *q)
{
	lockdep_assert_held(q->queue_lock);

	while (!list_empty(&q->icq_list)) {
		struct io_cq *icq = list_entry(q->icq_list.next,
					       struct io_cq, q_node);
		struct io_context *ioc = icq->ioc;

		spin_lock(&ioc->lock);
		ioc_destroy_icq(icq);
		spin_unlock(&ioc->lock);
	}
}

int create_task_io_context(struct task_struct *task, gfp_t gfp_flags, int node)
{
	struct io_context *ioc;
	int ret;

	ioc = kmem_cache_alloc_node(iocontext_cachep, gfp_flags | __GFP_ZERO,
				    node);
	if (unlikely(!ioc))
		return -ENOMEM;

	/* initialize */
	atomic_long_set(&ioc->refcount, 1);
	atomic_set(&ioc->nr_tasks, 1);
	atomic_set(&ioc->active_ref, 1);
	spin_lock_init(&ioc->lock);
	INIT_RADIX_TREE(&ioc->icq_tree, GFP_ATOMIC | __GFP_HIGH);
	INIT_HLIST_HEAD(&ioc->icq_list);
	INIT_WORK(&ioc->release_work, ioc_release_fn);

	/*
	 * Try to install.  ioc shouldn't be installed if someone else
	 * already did or @task, which isn't %current, is exiting.  Note
	 * that we need to allow ioc creation on exiting %current as exit
	 * path may issue IOs from e.g. exit_files().  The exit path is
	 * responsible for not issuing IO after exit_io_context().
	 */
	task_lock(task);
	if (!task->io_context &&
	    (task == current || !(task->flags & PF_EXITING)))
		task->io_context = ioc;
	else
		kmem_cache_free(iocontext_cachep, ioc);

	ret = task->io_context ? 0 : -EBUSY;

	task_unlock(task);

	return ret;
}

/**
 * get_task_io_context - get io_context of a task
 * @task: task of interest
 * @gfp_flags: allocation flags, used if allocation is necessary
 * @node: allocation node, used if allocation is necessary
 *
 * Return io_context of @task.  If it doesn't exist, it is created with
 * @gfp_flags and @node.  The returned io_context has its reference count
 * incremented.
 *
 * This function always goes through task_lock() and it's better to use
 * %current->io_context + get_io_context() for %current.
 */
struct io_context *get_task_io_context(struct task_struct *task,
				       gfp_t gfp_flags, int node)
{
	struct io_context *ioc;

	might_sleep_if(gfpflags_allow_blocking(gfp_flags));

	do {
		task_lock(task);
		ioc = task->io_context;
		if (likely(ioc)) {
			get_io_context(ioc);
			task_unlock(task);
			return ioc;
		}
		task_unlock(task);
	} while (!create_task_io_context(task, gfp_flags, node));

	return NULL;
}
EXPORT_SYMBOL(get_task_io_context);

/**
 * ioc_lookup_icq - lookup io_cq from ioc
 * @ioc: the associated io_context
 * @q: the associated request_queue
 *
 * Look up io_cq associated with @ioc - @q pair from @ioc.  Must be called
 * with @q->queue_lock held.
 */
struct io_cq *ioc_lookup_icq(struct io_context *ioc, struct request_queue *q)
{
	struct io_cq *icq;

	lockdep_assert_held(q->queue_lock);

	/*
	 * icq's are indexed from @ioc using radix tree and hint pointer,
	 * both of which are protected with RCU.  All removals are done
	 * holding both q and ioc locks, and we're holding q lock - if we
	 * find a icq which points to us, it's guaranteed to be valid.
	 */
	rcu_read_lock();
	icq = rcu_dereference(ioc->icq_hint);
	if (icq && icq->q == q)
		goto out;

	icq = radix_tree_lookup(&ioc->icq_tree, q->id);
	if (icq && icq->q == q)
		rcu_assign_pointer(ioc->icq_hint, icq);	/* allowed to race */
	else
		icq = NULL;
out:
	rcu_read_unlock();
	return icq;
}
EXPORT_SYMBOL(ioc_lookup_icq);

/**
 * ioc_create_icq - create and link io_cq
 * @ioc: io_context of interest
 * @q: request_queue of interest
 * @gfp_mask: allocation mask
 *
 * Make sure io_cq linking @ioc and @q exists.  If icq doesn't exist, they
 * will be created using @gfp_mask.
 *
 * The caller is responsible for ensuring @ioc won't go away and @q is
 * alive and will stay alive until this function returns.
 */
struct io_cq *ioc_create_icq(struct io_context *ioc, struct request_queue *q,
			     gfp_t gfp_mask)
{
	struct elevator_type *et = q->elevator->type;
	struct io_cq *icq;

	/* allocate stuff */
	icq = kmem_cache_alloc_node(et->icq_cache, gfp_mask | __GFP_ZERO,
				    q->node);
	if (!icq)
		return NULL;

	if (radix_tree_maybe_preload(gfp_mask) < 0) {
		kmem_cache_free(et->icq_cache, icq);
		return NULL;
	}

	icq->ioc = ioc;
	icq->q = q;
	INIT_LIST_HEAD(&icq->q_node);
	INIT_HLIST_NODE(&icq->ioc_node);

	/* lock both q and ioc and try to link @icq */
	spin_lock_irq(q->queue_lock);
	spin_lock(&ioc->lock);

	if (likely(!radix_tree_insert(&ioc->icq_tree, q->id, icq))) {
		hlist_add_head(&icq->ioc_node, &ioc->icq_list);
		list_add(&icq->q_node, &q->icq_list);
		if (et->ops.elevator_init_icq_fn)
			et->ops.elevator_init_icq_fn(icq);
	} else {
		kmem_cache_free(et->icq_cache, icq);
		icq = ioc_lookup_icq(ioc, q);
		if (!icq)
			printk(KERN_ERR "cfq: icq link failed!\n");
	}

	spin_unlock(&ioc->lock);
	spin_unlock_irq(q->queue_lock);
	radix_tree_preload_end();
	return icq;
}

static int __init blk_ioc_init(void)
{
	iocontext_cachep = kmem_cache_create("blkdev_ioc",
			sizeof(struct io_context), 0, SLAB_PANIC, NULL);
	return 0;
}
subsys_initcall(blk_ioc_init);
