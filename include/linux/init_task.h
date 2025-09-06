#ifndef _LINUX__INIT_TASK_H
#define _LINUX__INIT_TASK_H

#include <linux/rcupdate.h>
#include <linux/irqflags.h>
#include <linux/utsname.h>
#include <linux/lockdep.h>
#include <linux/ftrace.h>
#include <linux/ipc.h>
#include <linux/pid_namespace.h>
#include <linux/user_namespace.h>
#include <linux/securebits.h>
#include <net/net_namespace.h>

extern struct files_struct init_files;

#define INIT_KIOCTX(name, which_mm) \
{							\
	.users		= ATOMIC_INIT(1),		\
	.dead		= 0,				\
	.mm		= &which_mm,			\
	.user_id	= 0,				\
	.next		= NULL,				\
	.wait		= __WAIT_QUEUE_HEAD_INITIALIZER(name.wait), \
	.ctx_lock	= __SPIN_LOCK_UNLOCKED(name.ctx_lock), \
	.reqs_active	= 0U,				\
	.max_reqs	= ~0U,				\
}

#define INIT_MM(name) \
{			 					\
	.mm_rb		= RB_ROOT,				\
	.pgd		= swapper_pg_dir, 			\
	.mm_users	= ATOMIC_INIT(2), 			\
	.mm_count	= ATOMIC_INIT(1), 			\
	.mmap_sem	= __RWSEM_INITIALIZER(name.mmap_sem),	\
	.page_table_lock =  __SPIN_LOCK_UNLOCKED(name.page_table_lock),	\
	.mmlist		= LIST_HEAD_INIT(name.mmlist),		\
	.cpu_vm_mask	= CPU_MASK_ALL,				\
}

#define INIT_SIGNALS(sig) {						\
	.count		= ATOMIC_INIT(1), 				\
#include <linux/seqlock.h>
#include <linux/rbtree.h>
#include <net/net_namespace.h>
#include <linux/sched/rt.h>

#include <asm/thread_info.h>

#ifdef CONFIG_SMP
# define INIT_PUSHABLE_TASKS(tsk)					\
	.pushable_tasks = PLIST_NODE_INIT(tsk.pushable_tasks, MAX_PRIO),
#else
# define INIT_PUSHABLE_TASKS(tsk)
#endif

extern struct files_struct init_files;
extern struct fs_struct init_fs;

#ifdef CONFIG_CPUSETS
#define INIT_CPUSET_SEQ(tsk)							\
	.mems_allowed_seq = SEQCNT_ZERO(tsk.mems_allowed_seq),
#else
#define INIT_CPUSET_SEQ(tsk)
#endif

#ifndef CONFIG_VIRT_CPU_ACCOUNTING_NATIVE
#define INIT_PREV_CPUTIME(x)	.prev_cputime = {			\
	.lock = __RAW_SPIN_LOCK_UNLOCKED(x.prev_cputime.lock),		\
},
#else
#define INIT_PREV_CPUTIME(x)
#endif

#define INIT_SIGNALS(sig) {						\
	.nr_threads	= 1,						\
	.thread_head	= LIST_HEAD_INIT(init_task.thread_node),	\
	.wait_chldexit	= __WAIT_QUEUE_HEAD_INITIALIZER(sig.wait_chldexit),\
	.shared_pending	= { 						\
		.list = LIST_HEAD_INIT(sig.shared_pending.list),	\
		.signal =  {{0}}},					\
	.posix_timers	 = LIST_HEAD_INIT(sig.posix_timers),		\
	.cpu_timers	= INIT_CPU_TIMERS(sig.cpu_timers),		\
	.rlim		= INIT_RLIMITS,					\
}

extern struct nsproxy init_nsproxy;
#define INIT_NSPROXY(nsproxy) {						\
	.pid_ns		= &init_pid_ns,					\
	.count		= ATOMIC_INIT(1),				\
	.uts_ns		= &init_uts_ns,					\
	.mnt_ns		= NULL,						\
	INIT_NET_NS(net_ns)                                             \
	INIT_IPC_NS(ipc_ns)						\
	.user_ns	= &init_user_ns,				\
}

#define INIT_SIGHAND(sighand) {						\
	.count		= ATOMIC_INIT(1), 				\
	.action		= { { { .sa_handler = NULL, } }, },		\
	.cputimer	= { 						\
		.cputime_atomic	= INIT_CPUTIME_ATOMIC,			\
		.running	= false,				\
		.checking_timer = false,				\
	},								\
	INIT_PREV_CPUTIME(sig)						\
	.cred_guard_mutex =						\
		 __MUTEX_INITIALIZER(sig.cred_guard_mutex),		\
}

extern struct nsproxy init_nsproxy;

#define INIT_SIGHAND(sighand) {						\
	.count		= ATOMIC_INIT(1), 				\
	.action		= { { { .sa_handler = SIG_DFL, } }, },		\
	.siglock	= __SPIN_LOCK_UNLOCKED(sighand.siglock),	\
	.signalfd_wqh	= __WAIT_QUEUE_HEAD_INITIALIZER(sighand.signalfd_wqh),	\
}

extern struct group_info init_groups;

#define INIT_STRUCT_PID {						\
	.count 		= ATOMIC_INIT(1),				\
	.tasks		= {						\
		{ .first = &init_task.pids[PIDTYPE_PID].node },		\
		{ .first = &init_task.pids[PIDTYPE_PGID].node },	\
		{ .first = &init_task.pids[PIDTYPE_SID].node },		\
	},								\
	.rcu		= RCU_HEAD_INIT,				\
		{ .first = NULL },					\
		{ .first = NULL },					\
		{ .first = NULL },					\
	},								\
	.level		= 0,						\
	.numbers	= { {						\
		.nr		= 0,					\
		.ns		= &init_pid_ns,				\
		.pid_chain	= { .next = NULL, .pprev = NULL },	\
	}, }								\
}

#define INIT_PID_LINK(type) 					\
{								\
	.node = {						\
		.next = NULL,					\
		.pprev = &init_struct_pid.tasks[type].first,	\
		.pprev = NULL,					\
	},							\
	.pid = &init_struct_pid,				\
}

#ifdef CONFIG_AUDITSYSCALL
#define INIT_IDS \
	.loginuid = -1, \
	.sessionid = -1,
	.loginuid = INVALID_UID, \
	.sessionid = (unsigned int)-1,
#else
#define INIT_IDS
#endif

#ifdef CONFIG_SECURITY_FILE_CAPABILITIES
/*
 * Because of the reduced scope of CAP_SETPCAP when filesystem
 * capabilities are in effect, it is safe to allow CAP_SETPCAP to
 * be available in the default configuration.
 */
# define CAP_INIT_BSET  CAP_FULL_SET
#else
# define CAP_INIT_BSET  CAP_INIT_EFF_SET
#ifdef CONFIG_PREEMPT_RCU
#define INIT_TASK_RCU_TREE_PREEMPT()					\
	.rcu_blocked_node = NULL,
#else
#define INIT_TASK_RCU_TREE_PREEMPT(tsk)
#endif
#ifdef CONFIG_PREEMPT_RCU
#define INIT_TASK_RCU_PREEMPT(tsk)					\
	.rcu_read_lock_nesting = 0,					\
	.rcu_read_unlock_special.s = 0,					\
	.rcu_node_entry = LIST_HEAD_INIT(tsk.rcu_node_entry),		\
	INIT_TASK_RCU_TREE_PREEMPT()
#else
#define INIT_TASK_RCU_PREEMPT(tsk)
#endif
#ifdef CONFIG_TASKS_RCU
#define INIT_TASK_RCU_TASKS(tsk)					\
	.rcu_tasks_holdout = false,					\
	.rcu_tasks_holdout_list =					\
		LIST_HEAD_INIT(tsk.rcu_tasks_holdout_list),		\
	.rcu_tasks_idle_cpu = -1,
#else
#define INIT_TASK_RCU_TASKS(tsk)
#endif

extern struct cred init_cred;

extern struct task_group root_task_group;

#ifdef CONFIG_CGROUP_SCHED
# define INIT_CGROUP_SCHED(tsk)						\
	.sched_task_group = &root_task_group,
#else
# define INIT_CGROUP_SCHED(tsk)
#endif

#ifdef CONFIG_PERF_EVENTS
# define INIT_PERF_EVENTS(tsk)						\
	.perf_event_mutex = 						\
		 __MUTEX_INITIALIZER(tsk.perf_event_mutex),		\
	.perf_event_list = LIST_HEAD_INIT(tsk.perf_event_list),
#else
# define INIT_PERF_EVENTS(tsk)
#endif

#ifdef CONFIG_PREEMPT_RT_BASE
# define INIT_TIMER_LIST		.posix_timer_list = NULL,
#else
# define INIT_TIMER_LIST
#endif

#ifdef CONFIG_VIRT_CPU_ACCOUNTING_GEN
# define INIT_VTIME(tsk)						\
	.vtime_seqcount = SEQCNT_ZERO(tsk.vtime_seqcount),	\
	.vtime_snap = 0,				\
	.vtime_snap_whence = VTIME_SYS,
#else
# define INIT_VTIME(tsk)
#endif

#define INIT_TASK_COMM "swapper"

#ifdef CONFIG_RT_MUTEXES
# define INIT_RT_MUTEXES(tsk)						\
	.pi_waiters = RB_ROOT,						\
	.pi_waiters_leftmost = NULL,
#else
# define INIT_RT_MUTEXES(tsk)
#endif

#ifdef CONFIG_NUMA_BALANCING
# define INIT_NUMA_BALANCING(tsk)					\
	.numa_preferred_nid = -1,					\
	.numa_group = NULL,						\
	.numa_faults = NULL,
#else
# define INIT_NUMA_BALANCING(tsk)
#endif

#ifdef CONFIG_KASAN
# define INIT_KASAN(tsk)						\
	.kasan_depth = 1,
#else
# define INIT_KASAN(tsk)
#endif

#ifdef CONFIG_THREAD_INFO_IN_TASK
# define INIT_TASK_TI(tsk) .thread_info = INIT_THREAD_INFO(tsk),
#else
# define INIT_TASK_TI(tsk)
#endif

/*
 *  INIT_TASK is used to set up the first task table, touch at
 * your own risk!. Base=0, limit=0x1fffff (=2MB)
 */
#define INIT_TASK(tsk)	\
{									\
	INIT_TASK_TI(tsk)						\
	.state		= 0,						\
	.stack		= &init_thread_info,				\
	.usage		= ATOMIC_INIT(2),				\
	.flags		= PF_KTHREAD,					\
	.lock_depth	= -1,						\
	.prio		= MAX_PRIO-20,					\
	.static_prio	= MAX_PRIO-20,					\
	.normal_prio	= MAX_PRIO-20,					\
	.policy		= SCHED_NORMAL,					\
	.cpus_allowed	= CPU_MASK_ALL,					\
	.mm		= NULL,						\
	.active_mm	= &init_mm,					\
	.nr_cpus_allowed= NR_CPUS,					\
	.mm		= NULL,						\
	.active_mm	= &init_mm,					\
	.restart_block = {						\
		.fn = do_no_restart_syscall,				\
	},								\
	.se		= {						\
		.group_node 	= LIST_HEAD_INIT(tsk.se.group_node),	\
	},								\
	.rt		= {						\
		.run_list	= LIST_HEAD_INIT(tsk.rt.run_list),	\
		.time_slice	= HZ, 					\
		.nr_cpus_allowed = NR_CPUS,				\
	},								\
	.tasks		= LIST_HEAD_INIT(tsk.tasks),			\
		.time_slice	= RR_TIMESLICE,				\
	},								\
	.tasks		= LIST_HEAD_INIT(tsk.tasks),			\
	INIT_PUSHABLE_TASKS(tsk)					\
	INIT_CGROUP_SCHED(tsk)						\
	.ptraced	= LIST_HEAD_INIT(tsk.ptraced),			\
	.ptrace_entry	= LIST_HEAD_INIT(tsk.ptrace_entry),		\
	.real_parent	= &tsk,						\
	.parent		= &tsk,						\
	.children	= LIST_HEAD_INIT(tsk.children),			\
	.sibling	= LIST_HEAD_INIT(tsk.sibling),			\
	.group_leader	= &tsk,						\
	.group_info	= &init_groups,					\
	.cap_effective	= CAP_INIT_EFF_SET,				\
	.cap_inheritable = CAP_INIT_INH_SET,				\
	.cap_permitted	= CAP_FULL_SET,					\
	.cap_bset 	= CAP_INIT_BSET,				\
	.securebits     = SECUREBITS_DEFAULT,				\
	.user		= INIT_USER,					\
	.comm		= "swapper",					\
	RCU_POINTER_INITIALIZER(real_cred, &init_cred),			\
	RCU_POINTER_INITIALIZER(cred, &init_cred),			\
	.comm		= INIT_TASK_COMM,				\
	.thread		= INIT_THREAD,					\
	.fs		= &init_fs,					\
	.files		= &init_files,					\
	.signal		= &init_signals,				\
	.sighand	= &init_sighand,				\
	.nsproxy	= &init_nsproxy,				\
	.pending	= {						\
		.list = LIST_HEAD_INIT(tsk.pending.list),		\
		.signal = {{0}}},					\
	.blocked	= {{0}},					\
	.alloc_lock	= __SPIN_LOCK_UNLOCKED(tsk.alloc_lock),		\
	.journal_info	= NULL,						\
	.cpu_timers	= INIT_CPU_TIMERS(tsk.cpu_timers),		\
	.fs_excl	= ATOMIC_INIT(0),				\
	.pi_lock	= __SPIN_LOCK_UNLOCKED(tsk.pi_lock),		\
	.pi_lock	= __RAW_SPIN_LOCK_UNLOCKED(tsk.pi_lock),	\
	.timer_slack_ns = 50000, /* 50 usec default slack */		\
	INIT_TIMER_LIST							\
	.pids = {							\
		[PIDTYPE_PID]  = INIT_PID_LINK(PIDTYPE_PID),		\
		[PIDTYPE_PGID] = INIT_PID_LINK(PIDTYPE_PGID),		\
		[PIDTYPE_SID]  = INIT_PID_LINK(PIDTYPE_SID),		\
	},								\
	.dirties = INIT_PROP_LOCAL_SINGLE(dirties),			\
	INIT_IDS							\
	INIT_TRACE_IRQFLAGS						\
	INIT_LOCKDEP							\
	.thread_group	= LIST_HEAD_INIT(tsk.thread_group),		\
	.thread_node	= LIST_HEAD_INIT(init_signals.thread_head),	\
	INIT_IDS							\
	INIT_PERF_EVENTS(tsk)						\
	INIT_TRACE_IRQFLAGS						\
	INIT_LOCKDEP							\
	INIT_FTRACE_GRAPH						\
	INIT_TRACE_RECURSION						\
	INIT_TASK_RCU_PREEMPT(tsk)					\
	INIT_TASK_RCU_TASKS(tsk)					\
	INIT_CPUSET_SEQ(tsk)						\
	INIT_RT_MUTEXES(tsk)						\
	INIT_PREV_CPUTIME(tsk)						\
	INIT_VTIME(tsk)							\
	INIT_NUMA_BALANCING(tsk)					\
	INIT_KASAN(tsk)							\
}


#define INIT_CPU_TIMERS(cpu_timers)					\
{									\
	LIST_HEAD_INIT(cpu_timers[0]),					\
	LIST_HEAD_INIT(cpu_timers[1]),					\
	LIST_HEAD_INIT(cpu_timers[2]),					\
}

/* Attach to the init_task data structure for proper alignment */
#define __init_task_data __attribute__((__section__(".data..init_task")))


#endif
