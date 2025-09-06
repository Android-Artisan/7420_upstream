/*
 *  include/asm-s390/thread_info.h
 *
 *  S390 version
 *    Copyright (C) IBM Corp. 2002,2006
 *  S390 version
 *    Copyright IBM Corp. 2002, 2006
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 */

#ifndef _ASM_THREAD_INFO_H
#define _ASM_THREAD_INFO_H

#ifdef __KERNEL__
#include <linux/const.h>

/*
 * Size of kernel stack for each process
 */
#ifndef __s390x__
#ifndef __SMALL_STACK
#define THREAD_ORDER 1
#define ASYNC_ORDER  1
#else
#define THREAD_ORDER 0
#define ASYNC_ORDER  0
#endif
#else /* __s390x__ */
#ifndef __SMALL_STACK
#define THREAD_ORDER 2
#define ASYNC_ORDER  2
#else
#define THREAD_ORDER 1
#define ASYNC_ORDER  1
#endif
#endif /* __s390x__ */
#define THREAD_ORDER 2
#define ASYNC_ORDER  2

#define THREAD_SIZE (PAGE_SIZE << THREAD_ORDER)
#define ASYNC_SIZE  (PAGE_SIZE << ASYNC_ORDER)

#ifndef __ASSEMBLY__
#include <asm/processor.h>
#include <asm/lowcore.h>
#include <asm/lowcore.h>
#include <asm/page.h>
#include <asm/processor.h>

/*
 * low level task data that entry.S needs immediate access to
 * - this struct should fit entirely inside of one cache line
 * - this struct shares the supervisor stack pages
 * - if the contents of this structure are changed, the assembly constants must also be changed
 */
struct thread_info {
	struct task_struct	*task;		/* main task structure */
	struct exec_domain	*exec_domain;	/* execution domain */
	unsigned long		flags;		/* low level flags */
	unsigned int		cpu;		/* current CPU */
	int			preempt_count;	/* 0 => preemptable, <0 => BUG */
	struct restart_block	restart_block;
	unsigned long		flags;		/* low level flags */
	unsigned long		sys_call_table;	/* System call table address */
	unsigned int		cpu;		/* current CPU */
	int			preempt_count;	/* 0 => preemptable, <0 => BUG */
	unsigned int		system_call;
	__u64			user_timer;
	__u64			system_timer;
	unsigned long		last_break;	/* last breaking-event-address. */
};

/*
 * macros/functions for gaining access to the thread information structure
 */
#define INIT_THREAD_INFO(tsk)			\
{						\
	.task		= &tsk,			\
	.exec_domain	= &default_exec_domain,	\
	.flags		= 0,			\
	.cpu		= 0,			\
	.preempt_count	= 1,			\
	.restart_block	= {			\
		.fn = do_no_restart_syscall,	\
	},					\
	.flags		= 0,			\
	.cpu		= 0,			\
	.preempt_count	= INIT_PREEMPT_COUNT,	\
}

#define init_thread_info	(init_thread_union.thread_info)
#define init_stack		(init_thread_union.stack)

/* how to get the thread information struct from C */
static inline struct thread_info *current_thread_info(void)
{
	return (struct thread_info *)((*(unsigned long *) __LC_KERNEL_STACK)-THREAD_SIZE);
}

	return (struct thread_info *) S390_lowcore.thread_info;
}

void arch_release_task_struct(struct task_struct *tsk);

#define THREAD_SIZE_ORDER THREAD_ORDER

#endif

/*
 * thread information flags bit numbers
 */
#define TIF_SYSCALL_TRACE	0	/* syscall trace active */
#define TIF_SIGPENDING		2	/* signal pending */
#define TIF_NEED_RESCHED	3	/* rescheduling necessary */
#define TIF_RESTART_SVC		4	/* restart svc with new svc number */
#define TIF_SYSCALL_AUDIT	5	/* syscall auditing active */
#define TIF_SINGLE_STEP		6	/* deliver sigtrap on return to user */
#define TIF_MCCK_PENDING	7	/* machine check handling is pending */
#define TIF_USEDFPU		16	/* FPU was used by this task this quantum (SMP) */
#define TIF_POLLING_NRFLAG	17	/* true if poll_idle() is polling 
					   TIF_NEED_RESCHED */
#define TIF_31BIT		18	/* 32bit process */ 
#define TIF_MEMDIE		19
#define TIF_RESTORE_SIGMASK	20	/* restore signal mask in do_signal() */

#define _TIF_SYSCALL_TRACE	(1<<TIF_SYSCALL_TRACE)
#define _TIF_RESTORE_SIGMASK	(1<<TIF_RESTORE_SIGMASK)
#define _TIF_SIGPENDING		(1<<TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1<<TIF_NEED_RESCHED)
#define _TIF_RESTART_SVC	(1<<TIF_RESTART_SVC)
#define _TIF_SYSCALL_AUDIT	(1<<TIF_SYSCALL_AUDIT)
#define _TIF_SINGLE_STEP	(1<<TIF_SINGLE_STEP)
#define _TIF_MCCK_PENDING	(1<<TIF_MCCK_PENDING)
#define _TIF_USEDFPU		(1<<TIF_USEDFPU)
#define _TIF_POLLING_NRFLAG	(1<<TIF_POLLING_NRFLAG)
#define _TIF_31BIT		(1<<TIF_31BIT)

#endif /* __KERNEL__ */

#define PREEMPT_ACTIVE		0x4000000
#define TIF_NOTIFY_RESUME	0	/* callback before returning to user */
#define TIF_SIGPENDING		1	/* signal pending */
#define TIF_NEED_RESCHED	2	/* rescheduling necessary */
#define TIF_SYSCALL_TRACE	3	/* syscall trace active */
#define TIF_SYSCALL_AUDIT	4	/* syscall auditing active */
#define TIF_SECCOMP		5	/* secure computing */
#define TIF_SYSCALL_TRACEPOINT	6	/* syscall tracepoint instrumentation */
#define TIF_UPROBE		7	/* breakpointed or single-stepping */
#define TIF_ISOLATE_BP		8	/* Run process with isolated BP */
#define TIF_ISOLATE_BP_GUEST	9	/* Run KVM guests with isolated BP */
#define TIF_31BIT		16	/* 32bit process */
#define TIF_MEMDIE		17	/* is terminating due to OOM killer */
#define TIF_RESTORE_SIGMASK	18	/* restore signal mask in do_signal() */
#define TIF_SINGLE_STEP		19	/* This task is single stepped */
#define TIF_BLOCK_STEP		20	/* This task is block stepped */
#define TIF_UPROBE_SINGLESTEP	21	/* This task is uprobe single stepped */

#define _TIF_NOTIFY_RESUME	_BITUL(TIF_NOTIFY_RESUME)
#define _TIF_SIGPENDING		_BITUL(TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	_BITUL(TIF_NEED_RESCHED)
#define _TIF_SYSCALL_TRACE	_BITUL(TIF_SYSCALL_TRACE)
#define _TIF_SYSCALL_AUDIT	_BITUL(TIF_SYSCALL_AUDIT)
#define _TIF_SECCOMP		_BITUL(TIF_SECCOMP)
#define _TIF_SYSCALL_TRACEPOINT	_BITUL(TIF_SYSCALL_TRACEPOINT)
#define _TIF_UPROBE		_BITUL(TIF_UPROBE)
#define _TIF_ISOLATE_BP		_BITUL(TIF_ISOLATE_BP)
#define _TIF_ISOLATE_BP_GUEST	_BITUL(TIF_ISOLATE_BP_GUEST)
#define _TIF_31BIT		_BITUL(TIF_31BIT)
#define _TIF_SINGLE_STEP	_BITUL(TIF_SINGLE_STEP)

#define is_32bit_task()		(test_thread_flag(TIF_31BIT))

#endif /* _ASM_THREAD_INFO_H */
