/*
 *  linux/mm/oom_kill.c
 *
 *  Copyright (C)  1998,2000  Rik van Riel
 *	Thanks go out to Claus Fischer for some serious inspiration and
 *	for goading me into coding this file...
 *  Copyright (C)  2010  Google, Inc.
 *	Rewritten by David Rientjes
 *
 *  The routines in this file are used to kill a process when
 *  we're seriously out of memory. This gets called from __alloc_pages()
 *  in mm/page_alloc.c when we really run out of memory.
 *
 *  Since we won't call these routines often (on a well-configured
 *  machine) this file will double as a 'coding guide' and a signpost
 *  for newbie kernel hackers. It features several pointers to major
 *  kernel subsystems and hints as to where to find out what things do.
 */

#include <linux/oom.h>
#include <linux/mm.h>
#include <linux/err.h>
#include <linux/gfp.h>
#include <linux/sched.h>
#include <linux/swap.h>
#include <linux/timex.h>
#include <linux/jiffies.h>
#include <linux/cpuset.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/memcontrol.h>
#include <linux/security.h>

int sysctl_panic_on_oom;
int sysctl_oom_kill_allocating_task;
int sysctl_oom_dump_tasks;
static DEFINE_SPINLOCK(zone_scan_mutex);
/* #define DEBUG */

/**
 * badness - calculate a numeric value for how bad this task has been
 * @p: task struct of which task we should calculate
 * @uptime: current uptime in seconds
 * @mem: target memory controller
 *
 * The formula used is relatively simple and documented inline in the
 * function. The main rationale is that we want to select a good task
 * to kill when we run out of memory.
 *
 * Good in this context means that:
 * 1) we lose the minimum amount of work done
 * 2) we recover a large amount of memory
 * 3) we don't kill anything innocent of eating tons of memory
 * 4) we want to kill the minimum amount of processes (one)
 * 5) we try to kill the process the user expects us to kill, this
 *    algorithm has been meticulously tuned to meet the principle
 *    of least surprise ... (be careful when you change it)
 */

unsigned long badness(struct task_struct *p, unsigned long uptime)
{
	unsigned long points, cpu_time, run_time, s;
	struct mm_struct *mm;
	struct task_struct *child;

	task_lock(p);
	mm = p->mm;
	if (!mm) {
#include <linux/export.h>
#include <linux/notifier.h>
#include <linux/memcontrol.h>
#include <linux/mempolicy.h>
#include <linux/security.h>
#include <linux/ptrace.h>
#include <linux/freezer.h>
#include <linux/ftrace.h>
#include <linux/ratelimit.h>

#define CREATE_TRACE_POINTS
#include <trace/events/oom.h>

int sysctl_panic_on_oom;
int sysctl_oom_kill_allocating_task;
int sysctl_oom_dump_tasks = 1;

DEFINE_MUTEX(oom_lock);

#ifdef CONFIG_NUMA
/**
 * has_intersects_mems_allowed() - check task eligiblity for kill
 * @start: task struct of which task to consider
 * @mask: nodemask passed to page allocator for mempolicy ooms
 *
 * Task eligibility is determined by whether or not a candidate task, @tsk,
 * shares the same mempolicy nodes as current if it is bound by such a policy
 * and whether or not it has the same set of allowed cpuset nodes.
 */
static bool has_intersects_mems_allowed(struct task_struct *start,
					const nodemask_t *mask)
{
	struct task_struct *tsk;
	bool ret = false;

	rcu_read_lock();
	for_each_thread(start, tsk) {
		if (mask) {
			/*
			 * If this is a mempolicy constrained oom, tsk's
			 * cpuset is irrelevant.  Only return true if its
			 * mempolicy intersects current, otherwise it may be
			 * needlessly killed.
			 */
			ret = mempolicy_nodemask_intersects(tsk, mask);
		} else {
			/*
			 * This is not a mempolicy constrained oom, so only
			 * check the mems of tsk's cpuset.
			 */
			ret = cpuset_mems_allowed_intersects(current, tsk);
		}
		if (ret)
			break;
	}
	rcu_read_unlock();

	return ret;
}
#else
static bool has_intersects_mems_allowed(struct task_struct *tsk,
					const nodemask_t *mask)
{
	return true;
}
#endif /* CONFIG_NUMA */

/*
 * The process p may have detached its own ->mm while exiting or through
 * use_mm(), but one or more of its subthreads may still have a valid
 * pointer.  Return p, or any of its subthreads with a valid ->mm, with
 * task_lock() held.
 */
struct task_struct *find_lock_task_mm(struct task_struct *p)
{
	struct task_struct *t;

	rcu_read_lock();

	for_each_thread(p, t) {
		task_lock(t);
		if (likely(t->mm))
			goto found;
		task_unlock(t);
	}
	t = NULL;
found:
	rcu_read_unlock();

	return t;
}

/*
 * order == -1 means the oom kill is required by sysrq, otherwise only
 * for display purposes.
 */
static inline bool is_sysrq_oom(struct oom_control *oc)
{
	return oc->order == -1;
}

/* return true if the task is not adequate as candidate victim task. */
static bool oom_unkillable_task(struct task_struct *p,
		struct mem_cgroup *memcg, const nodemask_t *nodemask)
{
	if (is_global_init(p))
		return true;
	if (p->flags & PF_KTHREAD)
		return true;

	/* When mem_cgroup_out_of_memory() and p is not member of the group */
	if (memcg && !task_in_mem_cgroup(p, memcg))
		return true;

	/* p may not have freeable memory in nodemask */
	if (!has_intersects_mems_allowed(p, nodemask))
		return true;

	return false;
}

/**
 * oom_badness - heuristic function to determine which candidate task to kill
 * @p: task struct of which task we should calculate
 * @totalpages: total present RAM allowed for page allocation
 *
 * The heuristic for determining which task to kill is made to be as simple and
 * predictable as possible.  The goal is to return the highest value for the
 * task consuming the most memory to avoid subsequent oom failures.
 */
unsigned long oom_badness(struct task_struct *p, struct mem_cgroup *memcg,
			  const nodemask_t *nodemask, unsigned long totalpages)
{
	long points;
	long adj;

	if (oom_unkillable_task(p, memcg, nodemask))
		return 0;

	p = find_lock_task_mm(p);
	if (!p)
		return 0;

	adj = (long)p->signal->oom_score_adj;
	if (adj == OOM_SCORE_ADJ_MIN) {
		task_unlock(p);
		return 0;
	}

	/*
	 * The memory size of the process is the basis for the badness.
	 */
	points = mm->total_vm;

	/*
	 * After this unlock we can no longer dereference local variable `mm'
	 */
	task_unlock(p);

	/*
	 * swapoff can easily use up all memory, so kill those first.
	 */
	if (p->flags & PF_SWAPOFF)
		return ULONG_MAX;

	/*
	 * Processes which fork a lot of child processes are likely
	 * a good choice. We add half the vmsize of the children if they
	 * have an own mm. This prevents forking servers to flood the
	 * machine with an endless amount of children. In case a single
	 * child is eating the vast majority of memory, adding only half
	 * to the parents will make the child our kill candidate of choice.
	 */
	list_for_each_entry(child, &p->children, sibling) {
		task_lock(child);
		if (child->mm != mm && child->mm)
			points += child->mm->total_vm/2 + 1;
		task_unlock(child);
	}

	/*
	 * CPU time is in tens of seconds and run time is in thousands
         * of seconds. There is no particular reason for this other than
         * that it turned out to work very well in practice.
	 */
	cpu_time = (cputime_to_jiffies(p->utime) + cputime_to_jiffies(p->stime))
		>> (SHIFT_HZ + 3);

	if (uptime >= p->start_time.tv_sec)
		run_time = (uptime - p->start_time.tv_sec) >> 10;
	else
		run_time = 0;

	s = int_sqrt(cpu_time);
	if (s)
		points /= s;
	s = int_sqrt(int_sqrt(run_time));
	if (s)
		points /= s;

	/*
	 * Niced processes are most likely less important, so double
	 * their badness points.
	 */
	if (task_nice(p) > 0)
		points *= 2;

	/*
	 * Superuser processes are usually more important, so we make it
	 * less likely that we kill those.
	 */
	if (has_capability(p, CAP_SYS_ADMIN) ||
	    has_capability(p, CAP_SYS_RESOURCE))
		points /= 4;

	/*
	 * We don't want to kill a process with direct hardware access.
	 * Not only could that mess up the hardware, but usually users
	 * tend to only have this flag set on applications they think
	 * of as important.
	 */
	if (has_capability(p, CAP_SYS_RAWIO))
		points /= 4;

	/*
	 * If p's nodes don't overlap ours, it may still help to kill p
	 * because p may have allocated or otherwise mapped memory on
	 * this node before. However it will be less likely.
	 */
	if (!cpuset_mems_allowed_intersects(current, p))
		points /= 8;

	/*
	 * Adjust the score by oomkilladj.
	 */
	if (p->oomkilladj) {
		if (p->oomkilladj > 0) {
			if (!points)
				points = 1;
			points <<= p->oomkilladj;
		} else
			points >>= -(p->oomkilladj);
	}

#ifdef DEBUG
	printk(KERN_DEBUG "OOMkill: task %d (%s) got %lu points\n",
	p->pid, p->comm, points);
#endif
	return points;
	 * The baseline for the badness score is the proportion of RAM that each
	 * task's rss, pagetable and swap space use.
	 */
	points = get_mm_rss(p->mm) + get_mm_counter(p->mm, MM_SWAPENTS) +
		atomic_long_read(&p->mm->nr_ptes) + mm_nr_pmds(p->mm);
	task_unlock(p);

	/*
	 * Root processes get 3% bonus, just like the __vm_enough_memory()
	 * implementation used by LSMs.
	 */
	if (has_capability_noaudit(p, CAP_SYS_ADMIN))
		points -= (points * 3) / 100;

	/* Normalize to oom_score_adj units */
	adj *= totalpages / 1000;
	points += adj;

	/*
	 * Never return 0 for an eligible task regardless of the root bonus and
	 * oom_score_adj (oom_score_adj can't be OOM_SCORE_ADJ_MIN here).
	 */
	return points > 0 ? points : 1;
}

/*
 * Determine the type of allocation constraint.
 */
static inline enum oom_constraint constrained_alloc(struct zonelist *zonelist,
						    gfp_t gfp_mask)
{
#ifdef CONFIG_NUMA
	struct zone *zone;
	struct zoneref *z;
	enum zone_type high_zoneidx = gfp_zone(gfp_mask);
	nodemask_t nodes = node_states[N_HIGH_MEMORY];

	for_each_zone_zonelist(zone, z, zonelist, high_zoneidx)
		if (cpuset_zone_allowed_softwall(zone, gfp_mask))
			node_clear(zone_to_nid(zone), nodes);
		else
			return CONSTRAINT_CPUSET;

	if (!nodes_empty(nodes))
		return CONSTRAINT_MEMORY_POLICY;
#endif

	return CONSTRAINT_NONE;
#ifdef CONFIG_NUMA
static enum oom_constraint constrained_alloc(struct oom_control *oc,
					     unsigned long *totalpages)
{
	struct zone *zone;
	struct zoneref *z;
	enum zone_type high_zoneidx = gfp_zone(oc->gfp_mask);
	bool cpuset_limited = false;
	int nid;

	/* Default to all available memory */
	*totalpages = totalram_pages + total_swap_pages;

	if (!oc->zonelist)
		return CONSTRAINT_NONE;
	/*
	 * Reach here only when __GFP_NOFAIL is used. So, we should avoid
	 * to kill current.We have to random task kill in this case.
	 * Hopefully, CONSTRAINT_THISNODE...but no way to handle it, now.
	 */
	if (oc->gfp_mask & __GFP_THISNODE)
		return CONSTRAINT_NONE;

	/*
	 * This is not a __GFP_THISNODE allocation, so a truncated nodemask in
	 * the page allocator means a mempolicy is in effect.  Cpuset policy
	 * is enforced in get_page_from_freelist().
	 */
	if (oc->nodemask &&
	    !nodes_subset(node_states[N_MEMORY], *oc->nodemask)) {
		*totalpages = total_swap_pages;
		for_each_node_mask(nid, *oc->nodemask)
			*totalpages += node_spanned_pages(nid);
		return CONSTRAINT_MEMORY_POLICY;
	}

	/* Check this allocation failure is caused by cpuset's wall function */
	for_each_zone_zonelist_nodemask(zone, z, oc->zonelist,
			high_zoneidx, oc->nodemask)
		if (!cpuset_zone_allowed(zone, oc->gfp_mask))
			cpuset_limited = true;

	if (cpuset_limited) {
		*totalpages = total_swap_pages;
		for_each_node_mask(nid, cpuset_current_mems_allowed)
			*totalpages += node_spanned_pages(nid);
		return CONSTRAINT_CPUSET;
	}
	return CONSTRAINT_NONE;
}
#else
static enum oom_constraint constrained_alloc(struct oom_control *oc,
					     unsigned long *totalpages)
{
	*totalpages = totalram_pages + total_swap_pages;
	return CONSTRAINT_NONE;
}
#endif

enum oom_scan_t oom_scan_process_thread(struct oom_control *oc,
			struct task_struct *task, unsigned long totalpages)
{
	if (oom_unkillable_task(task, NULL, oc->nodemask))
		return OOM_SCAN_CONTINUE;

	/*
	 * This task already has access to memory reserves and is being killed.
	 * Don't allow any other task to have access to the reserves.
	 */
	if (test_tsk_thread_flag(task, TIF_MEMDIE)) {
		if (!is_sysrq_oom(oc))
			return OOM_SCAN_ABORT;
	}
	if (!task->mm)
		return OOM_SCAN_CONTINUE;

	/*
	 * If task is allocating a lot of memory and has been marked to be
	 * killed first if it triggers an oom, then select it.
	 */
	if (oom_task_origin(task))
		return OOM_SCAN_SELECT;

	if (task_will_free_mem(task) && !is_sysrq_oom(oc))
		return OOM_SCAN_ABORT;

	return OOM_SCAN_OK;
}

/*
 * Simple selection loop. We chose the process with the highest
 * number of 'points'. We expect the caller will lock the tasklist.
 *
 * (not docbooked, we don't want this one cluttering up the manual)
 */
static struct task_struct *select_bad_process(unsigned long *ppoints,
						struct mem_cgroup *mem)
{
	struct task_struct *g, *p;
	struct task_struct *chosen = NULL;
	struct timespec uptime;
	*ppoints = 0;

	do_posix_clock_monotonic_gettime(&uptime);
	do_each_thread(g, p) {
		unsigned long points;

		/*
		 * skip kernel threads and tasks which have already released
		 * their mm.
		 */
		if (!p->mm)
			continue;
		/* skip the init task */
		if (is_global_init(p))
			continue;
		if (mem && !task_in_mem_cgroup(p, mem))
			continue;

		/*
		 * This task already has access to memory reserves and is
		 * being killed. Don't allow any other task access to the
		 * memory reserve.
		 *
		 * Note: this may have a chance of deadlock if it gets
		 * blocked waiting for another task which itself is waiting
		 * for memory. Is there a better alternative?
		 */
		if (test_tsk_thread_flag(p, TIF_MEMDIE))
			return ERR_PTR(-1UL);

		/*
		 * This is in the process of releasing memory so wait for it
		 * to finish before killing some other task by mistake.
		 *
		 * However, if p is the current task, we allow the 'kill' to
		 * go ahead if it is exiting: this will simply set TIF_MEMDIE,
		 * which will allow it to gain access to memory reserves in
		 * the process of exiting and releasing its resources.
		 * Otherwise we could get an easy OOM deadlock.
		 */
		if (p->flags & PF_EXITING) {
			if (p != current)
				return ERR_PTR(-1UL);

			chosen = p;
			*ppoints = ULONG_MAX;
		}

		if (p->oomkilladj == OOM_DISABLE)
			continue;

		points = badness(p, uptime.tv_sec);
		if (points > *ppoints || !chosen) {
			chosen = p;
			*ppoints = points;
		}
	} while_each_thread(g, p);

 * number of 'points'.  Returns -1 on scan abort.
 */
static struct task_struct *select_bad_process(struct oom_control *oc,
		unsigned int *ppoints, unsigned long totalpages)
{
	struct task_struct *g, *p;
	struct task_struct *chosen = NULL;
	unsigned long chosen_points = 0;

	rcu_read_lock();
	for_each_process_thread(g, p) {
		unsigned int points;

		switch (oom_scan_process_thread(oc, p, totalpages)) {
		case OOM_SCAN_SELECT:
			chosen = p;
			chosen_points = ULONG_MAX;
			/* fall through */
		case OOM_SCAN_CONTINUE:
			continue;
		case OOM_SCAN_ABORT:
			rcu_read_unlock();
			return (struct task_struct *)(-1UL);
		case OOM_SCAN_OK:
			break;
		};
		points = oom_badness(p, NULL, oc->nodemask, totalpages);
		if (!points || points < chosen_points)
			continue;
		/* Prefer thread group leaders for display purposes */
		if (points == chosen_points && thread_group_leader(chosen))
			continue;

		chosen = p;
		chosen_points = points;
	}
	if (chosen)
		get_task_struct(chosen);
	rcu_read_unlock();

	*ppoints = chosen_points * 1000 / totalpages;
	return chosen;
}

/**
 * dump_tasks - dump current memory state of all system tasks
 * @mem: target memory controller
 *
 * Dumps the current memory state of all system tasks, excluding kernel threads.
 * State information includes task's pid, uid, tgid, vm size, rss, cpu, oom_adj
 * score, and name.
 *
 * If the actual is non-NULL, only tasks that are a member of the mem_cgroup are
 * shown.
 *
 * Call with tasklist_lock read-locked.
 */
static void dump_tasks(const struct mem_cgroup *mem)
{
	struct task_struct *g, *p;

	printk(KERN_INFO "[ pid ]   uid  tgid total_vm      rss cpu oom_adj "
	       "name\n");
	do_each_thread(g, p) {
		/*
		 * total_vm and rss sizes do not exist for tasks with a
		 * detached mm so there's no need to report them.
		 */
		if (!p->mm)
			continue;
		if (mem && !task_in_mem_cgroup(p, mem))
			continue;

		task_lock(p);
		printk(KERN_INFO "[%5d] %5d %5d %8lu %8lu %3d     %3d %s\n",
		       p->pid, p->uid, p->tgid, p->mm->total_vm,
		       get_mm_rss(p->mm), (int)task_cpu(p), p->oomkilladj,
		       p->comm);
		task_unlock(p);
	} while_each_thread(g, p);
}

/*
 * Send SIGKILL to the selected  process irrespective of  CAP_SYS_RAW_IO
 * flag though it's unlikely that  we select a process with CAP_SYS_RAW_IO
 * set.
 */
static void __oom_kill_task(struct task_struct *p, int verbose)
{
	if (is_global_init(p)) {
		WARN_ON(1);
		printk(KERN_WARNING "tried to kill init!\n");
		return;
	}

	if (!p->mm) {
		WARN_ON(1);
		printk(KERN_WARNING "tried to kill an mm-less task!\n");
		return;
	}

	if (verbose)
		printk(KERN_ERR "Killed process %d (%s)\n",
				task_pid_nr(p), p->comm);

	/*
	 * We give our sacrificial lamb high priority and access to
	 * all the memory it needs. That way it should be able to
	 * exit() and clear out its resources quickly...
	 */
	p->rt.time_slice = HZ;
	set_tsk_thread_flag(p, TIF_MEMDIE);

	force_sig(SIGKILL, p);
}

static int oom_kill_task(struct task_struct *p)
{
	struct mm_struct *mm;
	struct task_struct *g, *q;

	mm = p->mm;

	/* WARNING: mm may not be dereferenced since we did not obtain its
	 * value from get_task_mm(p).  This is OK since all we need to do is
	 * compare mm to q->mm below.
	 *
	 * Furthermore, even if mm contains a non-NULL value, p->mm may
	 * change to NULL at any time since we do not hold task_lock(p).
	 * However, this is of no concern to us.
	 */

	if (mm == NULL)
		return 1;

	/*
	 * Don't kill the process if any threads are set to OOM_DISABLE
	 */
	do_each_thread(g, q) {
		if (q->mm == mm && q->oomkilladj == OOM_DISABLE)
			return 1;
	} while_each_thread(g, q);

	__oom_kill_task(p, 1);

	/*
	 * kill all processes that share the ->mm (i.e. all threads),
	 * but are in a different thread group. Don't let them have access
	 * to memory reserves though, otherwise we might deplete all memory.
	 */
	do_each_thread(g, q) {
		if (q->mm == mm && !same_thread_group(q, p))
			force_sig(SIGKILL, q);
	} while_each_thread(g, q);

	return 0;
}

static int oom_kill_process(struct task_struct *p, gfp_t gfp_mask, int order,
			    unsigned long points, struct mem_cgroup *mem,
			    const char *message)
{
	struct task_struct *c;

	if (printk_ratelimit()) {
		printk(KERN_WARNING "%s invoked oom-killer: "
			"gfp_mask=0x%x, order=%d, oomkilladj=%d\n",
			current->comm, gfp_mask, order, current->oomkilladj);
		dump_stack();
		show_mem();
		if (sysctl_oom_dump_tasks)
			dump_tasks(mem);
	}
 * @memcg: current's memory controller, if constrained
 * @nodemask: nodemask passed to page allocator for mempolicy ooms
 *
 * Dumps the current memory state of all eligible tasks.  Tasks not in the same
 * memcg, not in the same cpuset, or bound to a disjoint set of mempolicy nodes
 * are not shown.
 * State information includes task's pid, uid, tgid, vm size, rss, nr_ptes,
 * swapents, oom_score_adj value, and name.
 */
static void dump_tasks(struct mem_cgroup *memcg, const nodemask_t *nodemask)
{
	struct task_struct *p;
	struct task_struct *task;

	pr_info("[ pid ]   uid  tgid total_vm      rss nr_ptes nr_pmds swapents oom_score_adj name\n");
	rcu_read_lock();
	for_each_process(p) {
		if (oom_unkillable_task(p, memcg, nodemask))
			continue;

		task = find_lock_task_mm(p);
		if (!task) {
			/*
			 * This is a kthread or all of p's threads have already
			 * detached their mm's.  There's no need to report
			 * them; they can't be oom killed anyway.
			 */
			continue;
		}

		pr_info("[%5d] %5d %5d %8lu %8lu %7ld %7ld %8lu         %5hd %s\n",
			task->pid, from_kuid(&init_user_ns, task_uid(task)),
			task->tgid, task->mm->total_vm, get_mm_rss(task->mm),
			atomic_long_read(&task->mm->nr_ptes),
			mm_nr_pmds(task->mm),
			get_mm_counter(task->mm, MM_SWAPENTS),
			task->signal->oom_score_adj, task->comm);
		task_unlock(task);
	}
	rcu_read_unlock();
}

static void dump_header(struct oom_control *oc, struct task_struct *p,
			struct mem_cgroup *memcg)
{
	pr_warning("%s invoked oom-killer: gfp_mask=0x%x, order=%d, "
		"oom_score_adj=%hd\n",
		current->comm, oc->gfp_mask, oc->order,
		current->signal->oom_score_adj);
	cpuset_print_current_mems_allowed();
	dump_stack();
	if (memcg)
		mem_cgroup_print_oom_info(memcg, p);
	else
		show_mem(SHOW_MEM_FILTER_NODES);
	if (sysctl_oom_dump_tasks)
		dump_tasks(memcg, oc->nodemask);
}

/*
 * Number of OOM victims in flight
 */
static atomic_t oom_victims = ATOMIC_INIT(0);
static DECLARE_WAIT_QUEUE_HEAD(oom_victims_wait);

bool oom_killer_disabled __read_mostly;

/**
 * mark_oom_victim - mark the given task as OOM victim
 * @tsk: task to mark
 *
 * Has to be called with oom_lock held and never after
 * oom has been disabled already.
 */
void mark_oom_victim(struct task_struct *tsk)
{
	WARN_ON(oom_killer_disabled);
	/* OOM killer might race with memcg OOM */
	if (test_and_set_tsk_thread_flag(tsk, TIF_MEMDIE))
		return;
	/*
	 * Make sure that the task is woken up from uninterruptible sleep
	 * if it is frozen because OOM killer wouldn't be able to free
	 * any memory and livelock. freezing_slow_path will tell the freezer
	 * that TIF_MEMDIE tasks should be ignored.
	 */
	__thaw_task(tsk);
	atomic_inc(&oom_victims);
}

/**
 * exit_oom_victim - note the exit of an OOM victim
 */
void exit_oom_victim(void)
{
	clear_thread_flag(TIF_MEMDIE);

	if (!atomic_dec_return(&oom_victims))
		wake_up_all(&oom_victims_wait);
}

/**
 * oom_killer_disable - disable OOM killer
 *
 * Forces all page allocations to fail rather than trigger OOM killer.
 * Will block and wait until all OOM victims are killed.
 *
 * The function cannot be called when there are runnable user tasks because
 * the userspace would see unexpected allocation failures as a result. Any
 * new usage of this function should be consulted with MM people.
 *
 * Returns true if successful and false if the OOM killer cannot be
 * disabled.
 */
bool oom_killer_disable(void)
{
	/*
	 * Make sure to not race with an ongoing OOM killer
	 * and that the current is not the victim.
	 */
	mutex_lock(&oom_lock);
	if (test_thread_flag(TIF_MEMDIE)) {
		mutex_unlock(&oom_lock);
		return false;
	}

	oom_killer_disabled = true;
	mutex_unlock(&oom_lock);

	wait_event(oom_victims_wait, !atomic_read(&oom_victims));

	return true;
}

/**
 * oom_killer_enable - enable OOM killer
 */
void oom_killer_enable(void)
{
	oom_killer_disabled = false;
}

/*
 * task->mm can be NULL if the task is the exited group leader.  So to
 * determine whether the task is using a particular mm, we examine all the
 * task's threads: if one of those is using this mm then this task was also
 * using it.
 */
static bool process_shares_mm(struct task_struct *p, struct mm_struct *mm)
{
	struct task_struct *t;

	for_each_thread(p, t) {
		struct mm_struct *t_mm = READ_ONCE(t->mm);
		if (t_mm)
			return t_mm == mm;
	}
	return false;
}

#define K(x) ((x) << (PAGE_SHIFT-10))
/*
 * Must be called while holding a reference to p, which will be released upon
 * returning.
 */
void oom_kill_process(struct oom_control *oc, struct task_struct *p,
		      unsigned int points, unsigned long totalpages,
		      struct mem_cgroup *memcg, const char *message)
{
	struct task_struct *victim = p;
	struct task_struct *child;
	struct task_struct *t;
	struct mm_struct *mm;
	unsigned int victim_points = 0;
	static DEFINE_RATELIMIT_STATE(oom_rs, DEFAULT_RATELIMIT_INTERVAL,
					      DEFAULT_RATELIMIT_BURST);

	/*
	 * If the task is already exiting, don't alarm the sysadmin or kill
	 * its children or threads, just set TIF_MEMDIE so it can die quickly
	 */
	if (p->flags & PF_EXITING) {
		__oom_kill_task(p, 0);
		return 0;
	}

	printk(KERN_ERR "%s: kill process %d (%s) score %li or a child\n",
					message, task_pid_nr(p), p->comm, points);

	/* Try to kill a child first */
	list_for_each_entry(c, &p->children, sibling) {
		if (c->mm == p->mm)
			continue;
		if (!oom_kill_task(c))
			return 0;
	}
	return oom_kill_task(p);
}

#ifdef CONFIG_CGROUP_MEM_RES_CTLR
void mem_cgroup_out_of_memory(struct mem_cgroup *mem, gfp_t gfp_mask)
{
	unsigned long points = 0;
	struct task_struct *p;

	cgroup_lock();
	read_lock(&tasklist_lock);
retry:
	p = select_bad_process(&points, mem);
	if (PTR_ERR(p) == -1UL)
		goto out;

	if (!p)
		p = current;

	if (oom_kill_process(p, gfp_mask, 0, points, mem,
				"Memory cgroup out of memory"))
		goto retry;
out:
	read_unlock(&tasklist_lock);
	cgroup_unlock();
}
#endif
	task_lock(p);
	if (p->mm && task_will_free_mem(p)) {
		mark_oom_victim(p);
		task_unlock(p);
		put_task_struct(p);
		return;
	}
	task_unlock(p);

	if (__ratelimit(&oom_rs))
		dump_header(oc, p, memcg);

	pr_err("%s: Kill process %d (%s) score %u or sacrifice child\n",
		message, task_pid_nr(p), p->comm, points);

	/*
	 * If any of p's children has a different mm and is eligible for kill,
	 * the one with the highest oom_badness() score is sacrificed for its
	 * parent.  This attempts to lose the minimal amount of work done while
	 * still freeing memory.
	 */
	read_lock(&tasklist_lock);

	/*
	 * The task 'p' might have already exited before reaching here. The
	 * put_task_struct() will free task_struct 'p' while the loop still try
	 * to access the field of 'p', so, get an extra reference.
	 */
	get_task_struct(p);
	for_each_thread(p, t) {
		list_for_each_entry(child, &t->children, sibling) {
			unsigned int child_points;

			if (process_shares_mm(child, p->mm))
				continue;
			/*
			 * oom_badness() returns 0 if the thread is unkillable
			 */
			child_points = oom_badness(child, memcg, oc->nodemask,
								totalpages);
			if (child_points > victim_points) {
				put_task_struct(victim);
				victim = child;
				victim_points = child_points;
				get_task_struct(victim);
			}
		}
	}
	put_task_struct(p);
	read_unlock(&tasklist_lock);

	p = find_lock_task_mm(victim);
	if (!p) {
		put_task_struct(victim);
		return;
	} else if (victim != p) {
		get_task_struct(p);
		put_task_struct(victim);
		victim = p;
	}

	/* Get a reference to safely compare mm after task_unlock(victim) */
	mm = victim->mm;
	atomic_inc(&mm->mm_count);
	/*
	 * We should send SIGKILL before setting TIF_MEMDIE in order to prevent
	 * the OOM victim from depleting the memory reserves from the user
	 * space under its control.
	 */
	do_send_sig_info(SIGKILL, SEND_SIG_FORCED, victim, true);
	mark_oom_victim(victim);
	pr_err("Killed process %d (%s) total-vm:%lukB, anon-rss:%lukB, file-rss:%lukB\n",
		task_pid_nr(victim), victim->comm, K(victim->mm->total_vm),
		K(get_mm_counter(victim->mm, MM_ANONPAGES)),
		K(get_mm_counter(victim->mm, MM_FILEPAGES)));
	task_unlock(victim);

	/*
	 * Kill all user processes sharing victim->mm in other thread groups, if
	 * any.  They don't get access to memory reserves, though, to avoid
	 * depletion of all memory.  This prevents mm->mmap_sem livelock when an
	 * oom killed thread cannot exit because it requires the semaphore and
	 * its contended by another thread trying to allocate memory itself.
	 * That thread will now get access to memory reserves since it has a
	 * pending fatal signal.
	 */
	rcu_read_lock();
	for_each_process(p) {
		if (!process_shares_mm(p, mm))
			continue;
		if (same_thread_group(p, victim))
			continue;
		if (unlikely(p->flags & PF_KTHREAD))
			continue;
		if (is_global_init(p))
			continue;
		if (p->signal->oom_score_adj == OOM_SCORE_ADJ_MIN)
			continue;

		do_send_sig_info(SIGKILL, SEND_SIG_FORCED, p, true);
	}
	rcu_read_unlock();

	mmdrop(mm);
	put_task_struct(victim);
}
#undef K

/*
 * Determines whether the kernel must panic because of the panic_on_oom sysctl.
 */
void check_panic_on_oom(struct oom_control *oc, enum oom_constraint constraint,
			struct mem_cgroup *memcg)
{
	if (likely(!sysctl_panic_on_oom))
		return;
	if (sysctl_panic_on_oom != 2) {
		/*
		 * panic_on_oom == 1 only affects CONSTRAINT_NONE, the kernel
		 * does not panic for cpuset, mempolicy, or memcg allocation
		 * failures.
		 */
		if (constraint != CONSTRAINT_NONE)
			return;
	}
	/* Do not panic for oom kills triggered by sysrq */
	if (is_sysrq_oom(oc))
		return;
	dump_header(oc, NULL, memcg);
	panic("Out of memory: %s panic_on_oom is enabled\n",
		sysctl_panic_on_oom == 2 ? "compulsory" : "system-wide");
}

static BLOCKING_NOTIFIER_HEAD(oom_notify_list);

int register_oom_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&oom_notify_list, nb);
}
EXPORT_SYMBOL_GPL(register_oom_notifier);

int unregister_oom_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&oom_notify_list, nb);
}
EXPORT_SYMBOL_GPL(unregister_oom_notifier);

/*
 * Try to acquire the OOM killer lock for the zones in zonelist.  Returns zero
 * if a parallel OOM killing is already taking place that includes a zone in
 * the zonelist.  Otherwise, locks all zones in the zonelist and returns 1.
 */
int try_set_zone_oom(struct zonelist *zonelist, gfp_t gfp_mask)
{
	struct zoneref *z;
	struct zone *zone;
	int ret = 1;

	spin_lock(&zone_scan_mutex);
	for_each_zone_zonelist(zone, z, zonelist, gfp_zone(gfp_mask)) {
		if (zone_is_oom_locked(zone)) {
			ret = 0;
			goto out;
		}
	}

	for_each_zone_zonelist(zone, z, zonelist, gfp_zone(gfp_mask)) {
		/*
		 * Lock each zone in the zonelist under zone_scan_mutex so a
		 * parallel invocation of try_set_zone_oom() doesn't succeed
		 * when it shouldn't.
		 */
		zone_set_flag(zone, ZONE_OOM_LOCKED);
	}

out:
	spin_unlock(&zone_scan_mutex);
	return ret;
}

/*
 * Clears the ZONE_OOM_LOCKED flag for all zones in the zonelist so that failed
 * allocation attempts with zonelists containing them may now recall the OOM
 * killer, if necessary.
 */
void clear_zonelist_oom(struct zonelist *zonelist, gfp_t gfp_mask)
{
	struct zoneref *z;
	struct zone *zone;

	spin_lock(&zone_scan_mutex);
	for_each_zone_zonelist(zone, z, zonelist, gfp_zone(gfp_mask)) {
		zone_clear_flag(zone, ZONE_OOM_LOCKED);
	}
	spin_unlock(&zone_scan_mutex);
}

/**
 * out_of_memory - kill the "best" process when we run out of memory
 * @zonelist: zonelist pointer
 * @gfp_mask: memory allocation flags
 * @order: amount of memory being requested as a power of 2
/**
 * out_of_memory - kill the "best" process when we run out of memory
 * @oc: pointer to struct oom_control
 *
 * If we run out of memory, we have the choice between either
 * killing a random task (bad), letting the system crash (worse)
 * OR try to be smart about which process to kill. Note that we
 * don't have to be perfect here, we just have to be good.
 */
void out_of_memory(struct zonelist *zonelist, gfp_t gfp_mask, int order)
{
	struct task_struct *p;
	unsigned long points = 0;
	unsigned long freed = 0;
	enum oom_constraint constraint;
bool out_of_memory(struct oom_control *oc)
{
	struct task_struct *p;
	unsigned long totalpages;
	unsigned long freed = 0;
	unsigned int uninitialized_var(points);
	enum oom_constraint constraint = CONSTRAINT_NONE;

	if (oom_killer_disabled)
		return false;

	blocking_notifier_call_chain(&oom_notify_list, 0, &freed);
	if (freed > 0)
		/* Got some memory back in the last second. */
		return;

	if (sysctl_panic_on_oom == 2)
		panic("out of memory. Compulsory panic_on_oom is selected.\n");
		return true;

	/*
	 * If current has a pending SIGKILL or is exiting, then automatically
	 * select it.  The goal is to allow it to allocate so that it may
	 * quickly exit and free its memory.
	 *
	 * But don't select if current has already released its mm and cleared
	 * TIF_MEMDIE flag at exit_mm(), otherwise an OOM livelock may occur.
	 */
	if (current->mm &&
	    (fatal_signal_pending(current) || task_will_free_mem(current))) {
		mark_oom_victim(current);
		return true;
	}

	/*
	 * Check if there were limitations on the allocation (only relevant for
	 * NUMA) that may require different handling.
	 */
	constraint = constrained_alloc(zonelist, gfp_mask);
	read_lock(&tasklist_lock);

	switch (constraint) {
	case CONSTRAINT_MEMORY_POLICY:
		oom_kill_process(current, gfp_mask, order, points, NULL,
				"No available memory (MPOL_BIND)");
		break;

	case CONSTRAINT_NONE:
		if (sysctl_panic_on_oom)
			panic("out of memory. panic_on_oom is selected\n");
		/* Fall-through */
	case CONSTRAINT_CPUSET:
		if (sysctl_oom_kill_allocating_task) {
			oom_kill_process(current, gfp_mask, order, points, NULL,
					"Out of memory (oom_kill_allocating_task)");
			break;
		}
retry:
		/*
		 * Rambo mode: Shoot down a process and hope it solves whatever
		 * issues we may have.
		 */
		p = select_bad_process(&points, NULL);

		if (PTR_ERR(p) == -1UL)
			goto out;

		/* Found nothing?!?! Either we hang forever, or we panic. */
		if (!p) {
			read_unlock(&tasklist_lock);
			panic("Out of memory and no killable processes...\n");
		}

		if (oom_kill_process(p, gfp_mask, order, points, NULL,
				     "Out of memory"))
			goto retry;

		break;
	}

out:
	read_unlock(&tasklist_lock);

	/*
	 * Give "p" a good chance of killing itself before we
	 * retry to allocate memory unless "p" is current
	 */
	if (!test_thread_flag(TIF_MEMDIE))
		schedule_timeout_uninterruptible(1);
	constraint = constrained_alloc(oc, &totalpages);
	if (constraint != CONSTRAINT_MEMORY_POLICY)
		oc->nodemask = NULL;
	check_panic_on_oom(oc, constraint, NULL);

	if (sysctl_oom_kill_allocating_task && current->mm &&
	    !oom_unkillable_task(current, NULL, oc->nodemask) &&
	    current->signal->oom_score_adj != OOM_SCORE_ADJ_MIN) {
		get_task_struct(current);
		oom_kill_process(oc, current, 0, totalpages, NULL,
				 "Out of memory (oom_kill_allocating_task)");
		return true;
	}

	p = select_bad_process(oc, &points, totalpages);
	/* Found nothing?!?! Either we hang forever, or we panic. */
	if (!p && !is_sysrq_oom(oc)) {
		dump_header(oc, NULL, NULL);
		panic("Out of memory and no killable processes...\n");
	}
	if (p && p != (void *)-1UL) {
		oom_kill_process(oc, p, points, totalpages, NULL,
				 "Out of memory");
		/*
		 * Give the killed process a good chance to exit before trying
		 * to allocate memory again.
		 */
		schedule_timeout_killable(1);
	}
	return true;
}

/*
 * The pagefault handler calls here because some allocation has failed. We have
 * to take care of the memcg OOM here because this is the only safe context without
 * any locks held but let the oom killer triggered from the allocation context care
 * about the global OOM.
 */
void pagefault_out_of_memory(void)
{
	static DEFINE_RATELIMIT_STATE(pfoom_rs, DEFAULT_RATELIMIT_INTERVAL,
				      DEFAULT_RATELIMIT_BURST);

	if (mem_cgroup_oom_synchronize(true))
		return;

	if (fatal_signal_pending(current))
		return;

	if (__ratelimit(&pfoom_rs))
		pr_warn("Huh VM_FAULT_OOM leaked out to the #PF handler. Retrying PF\n");
}
