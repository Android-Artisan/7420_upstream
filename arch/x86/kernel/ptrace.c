/* By Ross Biro 1/23/92 */
/*
 * Pentium III FXSR, SSE support
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 *
 * BTS tracing
 *	Markus Metzger <markus.t.metzger@intel.com>, Dec 2007
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/regset.h>
#include <linux/slab.h>
#include <linux/ptrace.h>
#include <linux/tracehook.h>
#include <linux/user.h>
#include <linux/elf.h>
#include <linux/security.h>
#include <linux/audit.h>
#include <linux/seccomp.h>
#include <linux/signal.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/processor.h>
#include <asm/i387.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <linux/rcupdate.h>
#include <linux/export.h>
#include <linux/context_tracking.h>
#include <linux/nospec.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/fpu/internal.h>
#include <asm/fpu/signal.h>
#include <asm/fpu/regset.h>
#include <asm/debugreg.h>
#include <asm/ldt.h>
#include <asm/desc.h>
#include <asm/prctl.h>
#include <asm/proto.h>
#include <asm/ds.h>
#include <asm/hw_breakpoint.h>
#include <asm/traps.h>
#include <asm/syscall.h>

#include "tls.h"

enum x86_regset {
	REGSET_GENERAL,
	REGSET_FP,
	REGSET_XFP,
	REGSET_TLS,
	REGSET_IOPERM64 = REGSET_XFP,
	REGSET_XSTATE,
	REGSET_TLS,
	REGSET_IOPERM32,
};

struct pt_regs_offset {
	const char *name;
	int offset;
};

#define REG_OFFSET_NAME(r) {.name = #r, .offset = offsetof(struct pt_regs, r)}
#define REG_OFFSET_END {.name = NULL, .offset = 0}

static const struct pt_regs_offset regoffset_table[] = {
#ifdef CONFIG_X86_64
	REG_OFFSET_NAME(r15),
	REG_OFFSET_NAME(r14),
	REG_OFFSET_NAME(r13),
	REG_OFFSET_NAME(r12),
	REG_OFFSET_NAME(r11),
	REG_OFFSET_NAME(r10),
	REG_OFFSET_NAME(r9),
	REG_OFFSET_NAME(r8),
#endif
	REG_OFFSET_NAME(bx),
	REG_OFFSET_NAME(cx),
	REG_OFFSET_NAME(dx),
	REG_OFFSET_NAME(si),
	REG_OFFSET_NAME(di),
	REG_OFFSET_NAME(bp),
	REG_OFFSET_NAME(ax),
#ifdef CONFIG_X86_32
	REG_OFFSET_NAME(ds),
	REG_OFFSET_NAME(es),
	REG_OFFSET_NAME(fs),
	REG_OFFSET_NAME(gs),
#endif
	REG_OFFSET_NAME(orig_ax),
	REG_OFFSET_NAME(ip),
	REG_OFFSET_NAME(cs),
	REG_OFFSET_NAME(flags),
	REG_OFFSET_NAME(sp),
	REG_OFFSET_NAME(ss),
	REG_OFFSET_END,
};

/**
 * regs_query_register_offset() - query register offset from its name
 * @name:	the name of a register
 *
 * regs_query_register_offset() returns the offset of a register in struct
 * pt_regs from its name. If the name is invalid, this returns -EINVAL;
 */
int regs_query_register_offset(const char *name)
{
	const struct pt_regs_offset *roff;
	for (roff = regoffset_table; roff->name != NULL; roff++)
		if (!strcmp(roff->name, name))
			return roff->offset;
	return -EINVAL;
}

/**
 * regs_query_register_name() - query register name from its offset
 * @offset:	the offset of a register in struct pt_regs.
 *
 * regs_query_register_name() returns the name of a register from its
 * offset in struct pt_regs. If the @offset is invalid, this returns NULL;
 */
const char *regs_query_register_name(unsigned int offset)
{
	const struct pt_regs_offset *roff;
	for (roff = regoffset_table; roff->name != NULL; roff++)
		if (roff->offset == offset)
			return roff->name;
	return NULL;
}

static const int arg_offs_table[] = {
#ifdef CONFIG_X86_32
	[0] = offsetof(struct pt_regs, ax),
	[1] = offsetof(struct pt_regs, dx),
	[2] = offsetof(struct pt_regs, cx)
#else /* CONFIG_X86_64 */
	[0] = offsetof(struct pt_regs, di),
	[1] = offsetof(struct pt_regs, si),
	[2] = offsetof(struct pt_regs, dx),
	[3] = offsetof(struct pt_regs, cx),
	[4] = offsetof(struct pt_regs, r8),
	[5] = offsetof(struct pt_regs, r9)
#endif
};

/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

/*
 * Determines which flags the user has access to [1 = access, 0 = no access].
 */
#define FLAG_MASK_32		((unsigned long)			\
				 (X86_EFLAGS_CF | X86_EFLAGS_PF |	\
				  X86_EFLAGS_AF | X86_EFLAGS_ZF |	\
				  X86_EFLAGS_SF | X86_EFLAGS_TF |	\
				  X86_EFLAGS_DF | X86_EFLAGS_OF |	\
				  X86_EFLAGS_RF | X86_EFLAGS_AC))

/*
 * Determines whether a value may be installed in a segment register.
 */
static inline bool invalid_selector(u16 value)
{
	return unlikely(value != 0 && (value & SEGMENT_RPL_MASK) != USER_RPL);
}

#ifdef CONFIG_X86_32

#define FLAG_MASK		FLAG_MASK_32

static long *pt_regs_access(struct pt_regs *regs, unsigned long regno)
{
	BUILD_BUG_ON(offsetof(struct pt_regs, bx) != 0);
	regno >>= 2;
	if (regno > FS)
		--regno;
	return &regs->bx + regno;
/*
 * X86_32 CPUs don't save ss and esp if the CPU is already in kernel mode
 * when it traps.  The previous stack will be directly underneath the saved
 * registers, and 'sp/ss' won't even have been saved. Thus the '&regs->sp'.
 *
 * Now, if the stack is empty, '&regs->sp' is out of range. In this
 * case we try to take the previous stack. To always return a non-null
 * stack pointer we fall back to regs as stack if no previous stack
 * exists.
 *
 * This is valid only for kernel mode traps.
 */
unsigned long kernel_stack_pointer(struct pt_regs *regs)
{
	unsigned long context = (unsigned long)regs & ~(THREAD_SIZE - 1);
	unsigned long sp = (unsigned long)&regs->sp;
	u32 *prev_esp;

	if (context == (sp & ~(THREAD_SIZE - 1)))
		return sp;

	prev_esp = (u32 *)(context);
	if (*prev_esp)
		return (unsigned long)*prev_esp;

	return (unsigned long)regs;
}
EXPORT_SYMBOL_GPL(kernel_stack_pointer);

static unsigned long *pt_regs_access(struct pt_regs *regs, unsigned long regno)
{
	BUILD_BUG_ON(offsetof(struct pt_regs, bx) != 0);
	return &regs->bx + (regno >> 2);
}

static u16 get_segment_reg(struct task_struct *task, unsigned long offset)
{
	/*
	 * Returning the value truncates it to 16 bits.
	 */
	unsigned int retval;
	if (offset != offsetof(struct user_regs_struct, gs))
		retval = *pt_regs_access(task_pt_regs(task), offset);
	else {
		retval = task->thread.gs;
		if (task == current)
			savesegment(gs, retval);
		if (task == current)
			retval = get_user_gs(task_pt_regs(task));
		else
			retval = task_user_gs(task);
	}
	return retval;
}

static int set_segment_reg(struct task_struct *task,
			   unsigned long offset, u16 value)
{
	/*
	 * The value argument was already truncated to 16 bits.
	 */
	if (invalid_selector(value))
		return -EIO;

	/*
	 * For %cs and %ss we cannot permit a null selector.
	 * We can permit a bogus selector as long as it has USER_RPL.
	 * Null selectors are fine for other segment registers, but
	 * we will never get back to user mode with invalid %cs or %ss
	 * and will take the trap in iret instead.  Much code relies
	 * on user_mode() to distinguish a user trap frame (which can
	 * safely use invalid selectors) from a kernel trap frame.
	 */
	switch (offset) {
	case offsetof(struct user_regs_struct, cs):
	case offsetof(struct user_regs_struct, ss):
		if (unlikely(value == 0))
			return -EIO;

	default:
		*pt_regs_access(task_pt_regs(task), offset) = value;
		break;

	case offsetof(struct user_regs_struct, gs):
		task->thread.gs = value;
		if (task == current)
			/*
			 * The user-mode %gs is not affected by
			 * kernel entry, so we must update the CPU.
			 */
			loadsegment(gs, value);
		if (task == current)
			set_user_gs(task_pt_regs(task), value);
		else
			task_user_gs(task) = value;
	}

	return 0;
}

static unsigned long debugreg_addr_limit(struct task_struct *task)
{
	return TASK_SIZE - 3;
}

#else  /* CONFIG_X86_64 */

#define FLAG_MASK		(FLAG_MASK_32 | X86_EFLAGS_NT)

static unsigned long *pt_regs_access(struct pt_regs *regs, unsigned long offset)
{
	BUILD_BUG_ON(offsetof(struct pt_regs, r15) != 0);
	return &regs->r15 + (offset / sizeof(regs->r15));
}

static u16 get_segment_reg(struct task_struct *task, unsigned long offset)
{
	/*
	 * Returning the value truncates it to 16 bits.
	 */
	unsigned int seg;

	switch (offset) {
	case offsetof(struct user_regs_struct, fs):
		if (task == current) {
			/* Older gas can't assemble movq %?s,%r?? */
			asm("movl %%fs,%0" : "=r" (seg));
			return seg;
		}
		return task->thread.fsindex;
	case offsetof(struct user_regs_struct, gs):
		if (task == current) {
			asm("movl %%gs,%0" : "=r" (seg));
			return seg;
		}
		return task->thread.gsindex;
	case offsetof(struct user_regs_struct, ds):
		if (task == current) {
			asm("movl %%ds,%0" : "=r" (seg));
			return seg;
		}
		return task->thread.ds;
	case offsetof(struct user_regs_struct, es):
		if (task == current) {
			asm("movl %%es,%0" : "=r" (seg));
			return seg;
		}
		return task->thread.es;

	case offsetof(struct user_regs_struct, cs):
	case offsetof(struct user_regs_struct, ss):
		break;
	}
	return *pt_regs_access(task_pt_regs(task), offset);
}

static int set_segment_reg(struct task_struct *task,
			   unsigned long offset, u16 value)
{
	/*
	 * The value argument was already truncated to 16 bits.
	 */
	if (invalid_selector(value))
		return -EIO;

	switch (offset) {
	case offsetof(struct user_regs_struct,fs):
		/*
		 * If this is setting fs as for normal 64-bit use but
		 * setting fs_base has implicitly changed it, leave it.
		 */
		if ((value == FS_TLS_SEL && task->thread.fsindex == 0 &&
		     task->thread.fs != 0) ||
		    (value == 0 && task->thread.fsindex == FS_TLS_SEL &&
		     task->thread.fs == 0))
			break;
		task->thread.fsindex = value;
		if (task == current)
			loadsegment(fs, task->thread.fsindex);
		break;
	case offsetof(struct user_regs_struct,gs):
		/*
		 * If this is setting gs as for normal 64-bit use but
		 * setting gs_base has implicitly changed it, leave it.
		 */
		if ((value == GS_TLS_SEL && task->thread.gsindex == 0 &&
		     task->thread.gs != 0) ||
		    (value == 0 && task->thread.gsindex == GS_TLS_SEL &&
		     task->thread.gs == 0))
			break;
		task->thread.gsindex = value;
		if (task == current)
			load_gs_index(task->thread.gsindex);
		break;
	case offsetof(struct user_regs_struct,ds):
		task->thread.ds = value;
		if (task == current)
			loadsegment(ds, task->thread.ds);
		break;
	case offsetof(struct user_regs_struct,es):
		task->thread.es = value;
		if (task == current)
			loadsegment(es, task->thread.es);
		break;

		/*
		 * Can't actually change these in 64-bit mode.
		 */
	case offsetof(struct user_regs_struct,cs):
		if (unlikely(value == 0))
			return -EIO;
#ifdef CONFIG_IA32_EMULATION
		if (test_tsk_thread_flag(task, TIF_IA32))
			task_pt_regs(task)->cs = value;
#endif
		task_pt_regs(task)->cs = value;
		break;
	case offsetof(struct user_regs_struct,ss):
		if (unlikely(value == 0))
			return -EIO;
#ifdef CONFIG_IA32_EMULATION
		if (test_tsk_thread_flag(task, TIF_IA32))
			task_pt_regs(task)->ss = value;
#endif
		task_pt_regs(task)->ss = value;
		break;
	}

	return 0;
}

static unsigned long debugreg_addr_limit(struct task_struct *task)
{
#ifdef CONFIG_IA32_EMULATION
	if (test_tsk_thread_flag(task, TIF_IA32))
		return IA32_PAGE_OFFSET - 3;
#endif
	return TASK_SIZE64 - 7;
}

#endif	/* CONFIG_X86_32 */

static unsigned long get_flags(struct task_struct *task)
{
	unsigned long retval = task_pt_regs(task)->flags;

	/*
	 * If the debugger set TF, hide it from the readout.
	 */
	if (test_tsk_thread_flag(task, TIF_FORCED_TF))
		retval &= ~X86_EFLAGS_TF;

	return retval;
}

static int set_flags(struct task_struct *task, unsigned long value)
{
	struct pt_regs *regs = task_pt_regs(task);

	/*
	 * If the user value contains TF, mark that
	 * it was not "us" (the debugger) that set it.
	 * If not, make sure it stays set if we had.
	 */
	if (value & X86_EFLAGS_TF)
		clear_tsk_thread_flag(task, TIF_FORCED_TF);
	else if (test_tsk_thread_flag(task, TIF_FORCED_TF))
		value |= X86_EFLAGS_TF;

	regs->flags = (regs->flags & ~FLAG_MASK) | (value & FLAG_MASK);

	return 0;
}

static int putreg(struct task_struct *child,
		  unsigned long offset, unsigned long value)
{
	switch (offset) {
	case offsetof(struct user_regs_struct, cs):
	case offsetof(struct user_regs_struct, ds):
	case offsetof(struct user_regs_struct, es):
	case offsetof(struct user_regs_struct, fs):
	case offsetof(struct user_regs_struct, gs):
	case offsetof(struct user_regs_struct, ss):
		return set_segment_reg(child, offset, value);

	case offsetof(struct user_regs_struct, flags):
		return set_flags(child, value);

#ifdef CONFIG_X86_64
	/*
	 * Orig_ax is really just a flag with small positive and
	 * negative values, so make sure to always sign-extend it
	 * from 32 bits so that it works correctly regardless of
	 * whether we come from a 32-bit environment or not.
	 */
	case offsetof(struct user_regs_struct, orig_ax):
		value = (long) (s32) value;
		break;

	case offsetof(struct user_regs_struct,fs_base):
		if (value >= TASK_SIZE_OF(child))
			return -EIO;
		/*
		 * When changing the segment base, use do_arch_prctl
		 * to set either thread.fs or thread.fsindex and the
		 * corresponding GDT slot.
		 */
		if (child->thread.fs != value)
			return do_arch_prctl(child, ARCH_SET_FS, value);
		return 0;
	case offsetof(struct user_regs_struct,gs_base):
		/*
		 * Exactly the same here as the %fs handling above.
		 */
		if (value >= TASK_SIZE_OF(child))
			return -EIO;
		if (child->thread.gs != value)
			return do_arch_prctl(child, ARCH_SET_GS, value);
		return 0;
#endif
	}

	*pt_regs_access(task_pt_regs(child), offset) = value;
	return 0;
}

static unsigned long getreg(struct task_struct *task, unsigned long offset)
{
	switch (offset) {
	case offsetof(struct user_regs_struct, cs):
	case offsetof(struct user_regs_struct, ds):
	case offsetof(struct user_regs_struct, es):
	case offsetof(struct user_regs_struct, fs):
	case offsetof(struct user_regs_struct, gs):
	case offsetof(struct user_regs_struct, ss):
		return get_segment_reg(task, offset);

	case offsetof(struct user_regs_struct, flags):
		return get_flags(task);

#ifdef CONFIG_X86_64
	case offsetof(struct user_regs_struct, fs_base): {
		/*
		 * do_arch_prctl may have used a GDT slot instead of
		 * the MSR.  To userland, it appears the same either
		 * way, except the %fs segment selector might not be 0.
		 */
		unsigned int seg = task->thread.fsindex;
		if (task->thread.fs != 0)
			return task->thread.fs;
		if (task == current)
			asm("movl %%fs,%0" : "=r" (seg));
		if (seg != FS_TLS_SEL)
			return 0;
		return get_desc_base(&task->thread.tls_array[FS_TLS]);
	}
	case offsetof(struct user_regs_struct, gs_base): {
		/*
		 * Exactly the same here as the %fs handling above.
		 */
		unsigned int seg = task->thread.gsindex;
		if (task->thread.gs != 0)
			return task->thread.gs;
		if (task == current)
			asm("movl %%gs,%0" : "=r" (seg));
		if (seg != GS_TLS_SEL)
			return 0;
		return get_desc_base(&task->thread.tls_array[GS_TLS]);
	}
#endif
	}

	return *pt_regs_access(task_pt_regs(task), offset);
}

static int genregs_get(struct task_struct *target,
		       const struct user_regset *regset,
		       unsigned int pos, unsigned int count,
		       void *kbuf, void __user *ubuf)
{
	if (kbuf) {
		unsigned long *k = kbuf;
		while (count > 0) {
		while (count >= sizeof(*k)) {
			*k++ = getreg(target, pos);
			count -= sizeof(*k);
			pos += sizeof(*k);
		}
	} else {
		unsigned long __user *u = ubuf;
		while (count > 0) {
		while (count >= sizeof(*u)) {
			if (__put_user(getreg(target, pos), u++))
				return -EFAULT;
			count -= sizeof(*u);
			pos += sizeof(*u);
		}
	}

	return 0;
}

static int genregs_set(struct task_struct *target,
		       const struct user_regset *regset,
		       unsigned int pos, unsigned int count,
		       const void *kbuf, const void __user *ubuf)
{
	int ret = 0;
	if (kbuf) {
		const unsigned long *k = kbuf;
		while (count > 0 && !ret) {
		while (count >= sizeof(*k) && !ret) {
			ret = putreg(target, pos, *k++);
			count -= sizeof(*k);
			pos += sizeof(*k);
		}
	} else {
		const unsigned long  __user *u = ubuf;
		while (count > 0 && !ret) {
		while (count >= sizeof(*u) && !ret) {
			unsigned long word;
			ret = __get_user(word, u++);
			if (ret)
				break;
			ret = putreg(target, pos, word);
			count -= sizeof(*u);
			pos += sizeof(*u);
		}
	}
	return ret;
}

/*
 * This function is trivial and will be inlined by the compiler.
 * Having it separates the implementation details of debug
 * registers from the interface details of ptrace.
 */
static unsigned long ptrace_get_debugreg(struct task_struct *child, int n)
{
	switch (n) {
	case 0:		return child->thread.debugreg0;
	case 1:		return child->thread.debugreg1;
	case 2:		return child->thread.debugreg2;
	case 3:		return child->thread.debugreg3;
	case 6:		return child->thread.debugreg6;
	case 7:		return child->thread.debugreg7;
	}
	return 0;
}

static int ptrace_set_debugreg(struct task_struct *child,
			       int n, unsigned long data)
{
	int i;

	if (unlikely(n == 4 || n == 5))
		return -EIO;

	if (n < 4 && unlikely(data >= debugreg_addr_limit(child)))
		return -EIO;

	switch (n) {
	case 0:		child->thread.debugreg0 = data; break;
	case 1:		child->thread.debugreg1 = data; break;
	case 2:		child->thread.debugreg2 = data; break;
	case 3:		child->thread.debugreg3 = data; break;

	case 6:
		if ((data & ~0xffffffffUL) != 0)
			return -EIO;
		child->thread.debugreg6 = data;
		break;

	case 7:
		/*
		 * Sanity-check data. Take one half-byte at once with
		 * check = (val >> (16 + 4*i)) & 0xf. It contains the
		 * R/Wi and LENi bits; bits 0 and 1 are R/Wi, and bits
		 * 2 and 3 are LENi. Given a list of invalid values,
		 * we do mask |= 1 << invalid_value, so that
		 * (mask >> check) & 1 is a correct test for invalid
		 * values.
		 *
		 * R/Wi contains the type of the breakpoint /
		 * watchpoint, LENi contains the length of the watched
		 * data in the watchpoint case.
		 *
		 * The invalid values are:
		 * - LENi == 0x10 (undefined), so mask |= 0x0f00.	[32-bit]
		 * - R/Wi == 0x10 (break on I/O reads or writes), so
		 *   mask |= 0x4444.
		 * - R/Wi == 0x00 && LENi != 0x00, so we have mask |=
		 *   0x1110.
		 *
		 * Finally, mask = 0x0f00 | 0x4444 | 0x1110 == 0x5f54.
		 *
		 * See the Intel Manual "System Programming Guide",
		 * 15.2.4
		 *
		 * Note that LENi == 0x10 is defined on x86_64 in long
		 * mode (i.e. even for 32-bit userspace software, but
		 * 64-bit kernel), so the x86_64 mask value is 0x5454.
		 * See the AMD manual no. 24593 (AMD64 System Programming)
		 */
#ifdef CONFIG_X86_32
#define	DR7_MASK	0x5f54
#else
#define	DR7_MASK	0x5554
#endif
		data &= ~DR_CONTROL_RESERVED;
		for (i = 0; i < 4; i++)
			if ((DR7_MASK >> ((data >> (16 + 4*i)) & 0xf)) & 1)
				return -EIO;
		child->thread.debugreg7 = data;
		if (data)
			set_tsk_thread_flag(child, TIF_DEBUG);
		else
			clear_tsk_thread_flag(child, TIF_DEBUG);
		break;
	}

	return 0;
}

#ifdef X86_BTS

static int ptrace_bts_get_size(struct task_struct *child)
{
	if (!child->thread.ds_area_msr)
		return -ENXIO;

	return ds_get_bts_index((void *)child->thread.ds_area_msr);
}

static int ptrace_bts_read_record(struct task_struct *child,
				  long index,
				  struct bts_struct __user *out)
{
	struct bts_struct ret;
	int retval;
	int bts_end;
	int bts_index;

	if (!child->thread.ds_area_msr)
		return -ENXIO;

	if (index < 0)
		return -EINVAL;

	bts_end = ds_get_bts_end((void *)child->thread.ds_area_msr);
	if (bts_end <= index)
		return -EINVAL;

	/* translate the ptrace bts index into the ds bts index */
	bts_index = ds_get_bts_index((void *)child->thread.ds_area_msr);
	bts_index -= (index + 1);
	if (bts_index < 0)
		bts_index += bts_end;

	retval = ds_read_bts((void *)child->thread.ds_area_msr,
			     bts_index, &ret);
	if (retval < 0)
		return retval;

	if (copy_to_user(out, &ret, sizeof(ret)))
		return -EFAULT;

	return sizeof(ret);
}

static int ptrace_bts_clear(struct task_struct *child)
{
	if (!child->thread.ds_area_msr)
		return -ENXIO;

	return ds_clear((void *)child->thread.ds_area_msr);
}

static int ptrace_bts_drain(struct task_struct *child,
			    long size,
			    struct bts_struct __user *out)
{
	int end, i;
	void *ds = (void *)child->thread.ds_area_msr;

	if (!ds)
		return -ENXIO;

	end = ds_get_bts_index(ds);
	if (end <= 0)
		return end;

	if (size < (end * sizeof(struct bts_struct)))
		return -EIO;

	for (i = 0; i < end; i++, out++) {
		struct bts_struct ret;
		int retval;

		retval = ds_read_bts(ds, i, &ret);
		if (retval < 0)
			return retval;

		if (copy_to_user(out, &ret, sizeof(ret)))
			return -EFAULT;
	}

	ds_clear(ds);

	return end;
}

static int ptrace_bts_config(struct task_struct *child,
			     long cfg_size,
			     const struct ptrace_bts_config __user *ucfg)
{
	struct ptrace_bts_config cfg;
	int bts_size, ret = 0;
	void *ds;

	if (cfg_size < sizeof(cfg))
		return -EIO;

	if (copy_from_user(&cfg, ucfg, sizeof(cfg)))
		return -EFAULT;

	if ((int)cfg.size < 0)
		return -EINVAL;

	bts_size = 0;
	ds = (void *)child->thread.ds_area_msr;
	if (ds) {
		bts_size = ds_get_bts_size(ds);
		if (bts_size < 0)
			return bts_size;
	}
	cfg.size = PAGE_ALIGN(cfg.size);

	if (bts_size != cfg.size) {
		ret = ptrace_bts_realloc(child, cfg.size,
					 cfg.flags & PTRACE_BTS_O_CUT_SIZE);
		if (ret < 0)
			goto errout;

		ds = (void *)child->thread.ds_area_msr;
	}

	if (cfg.flags & PTRACE_BTS_O_SIGNAL)
		ret = ds_set_overflow(ds, DS_O_SIGNAL);
	else
		ret = ds_set_overflow(ds, DS_O_WRAP);
	if (ret < 0)
		goto errout;

	if (cfg.flags & PTRACE_BTS_O_TRACE)
		child->thread.debugctlmsr |= ds_debugctl_mask();
	else
		child->thread.debugctlmsr &= ~ds_debugctl_mask();

	if (cfg.flags & PTRACE_BTS_O_SCHED)
		set_tsk_thread_flag(child, TIF_BTS_TRACE_TS);
	else
		clear_tsk_thread_flag(child, TIF_BTS_TRACE_TS);

	ret = sizeof(cfg);

out:
	if (child->thread.debugctlmsr)
		set_tsk_thread_flag(child, TIF_DEBUGCTLMSR);
	else
		clear_tsk_thread_flag(child, TIF_DEBUGCTLMSR);

	return ret;

errout:
	child->thread.debugctlmsr &= ~ds_debugctl_mask();
	clear_tsk_thread_flag(child, TIF_BTS_TRACE_TS);
	goto out;
}

static int ptrace_bts_status(struct task_struct *child,
			     long cfg_size,
			     struct ptrace_bts_config __user *ucfg)
{
	void *ds = (void *)child->thread.ds_area_msr;
	struct ptrace_bts_config cfg;

	if (cfg_size < sizeof(cfg))
		return -EIO;

	memset(&cfg, 0, sizeof(cfg));

	if (ds) {
		cfg.size = ds_get_bts_size(ds);

		if (ds_get_overflow(ds) == DS_O_SIGNAL)
			cfg.flags |= PTRACE_BTS_O_SIGNAL;

		if (test_tsk_thread_flag(child, TIF_DEBUGCTLMSR) &&
		    child->thread.debugctlmsr & ds_debugctl_mask())
			cfg.flags |= PTRACE_BTS_O_TRACE;

		if (test_tsk_thread_flag(child, TIF_BTS_TRACE_TS))
			cfg.flags |= PTRACE_BTS_O_SCHED;
	}

	cfg.bts_size = sizeof(struct bts_struct);

	if (copy_to_user(ucfg, &cfg, sizeof(cfg)))
		return -EFAULT;

	return sizeof(cfg);
}


static int ptrace_bts_write_record(struct task_struct *child,
				   const struct bts_struct *in)
{
	int retval;

	if (!child->thread.ds_area_msr)
		return -ENXIO;

	retval = ds_write_bts((void *)child->thread.ds_area_msr, in);
	if (retval)
		return retval;

	return sizeof(*in);
}

static int ptrace_bts_realloc(struct task_struct *child,
			      int size, int reduce_size)
{
	unsigned long rlim, vm;
	int ret, old_size;

	if (size < 0)
		return -EINVAL;

	old_size = ds_get_bts_size((void *)child->thread.ds_area_msr);
	if (old_size < 0)
		return old_size;

	ret = ds_free((void **)&child->thread.ds_area_msr);
	if (ret < 0)
		goto out;

	size >>= PAGE_SHIFT;
	old_size >>= PAGE_SHIFT;

	current->mm->total_vm  -= old_size;
	current->mm->locked_vm -= old_size;

	if (size == 0)
		goto out;

	rlim = current->signal->rlim[RLIMIT_AS].rlim_cur >> PAGE_SHIFT;
	vm = current->mm->total_vm  + size;
	if (rlim < vm) {
		ret = -ENOMEM;

		if (!reduce_size)
			goto out;

		size = rlim - current->mm->total_vm;
		if (size <= 0)
			goto out;
	}

	rlim = current->signal->rlim[RLIMIT_MEMLOCK].rlim_cur >> PAGE_SHIFT;
	vm = current->mm->locked_vm  + size;
	if (rlim < vm) {
		ret = -ENOMEM;

		if (!reduce_size)
			goto out;

		size = rlim - current->mm->locked_vm;
		if (size <= 0)
			goto out;
	}

	ret = ds_allocate((void **)&child->thread.ds_area_msr,
			  size << PAGE_SHIFT);
	if (ret < 0)
		goto out;

	current->mm->total_vm  += size;
	current->mm->locked_vm += size;

out:
	if (child->thread.ds_area_msr)
		set_tsk_thread_flag(child, TIF_DS_AREA_MSR);
	else
		clear_tsk_thread_flag(child, TIF_DS_AREA_MSR);

	return ret;
}

void ptrace_bts_take_timestamp(struct task_struct *tsk,
			       enum bts_qualifier qualifier)
{
	struct bts_struct rec = {
		.qualifier = qualifier,
		.variant.jiffies = jiffies_64
	};

	ptrace_bts_write_record(tsk, &rec);
}
#endif /* X86_BTS */

static void ptrace_triggered(struct perf_event *bp,
			     struct perf_sample_data *data,
			     struct pt_regs *regs)
{
	int i;
	struct thread_struct *thread = &(current->thread);

	/*
	 * Store in the virtual DR6 register the fact that the breakpoint
	 * was hit so the thread's debugger will see it.
	 */
	for (i = 0; i < HBP_NUM; i++) {
		if (thread->ptrace_bps[i] == bp)
			break;
	}

	thread->debugreg6 |= (DR_TRAP0 << i);
}

/*
 * Walk through every ptrace breakpoints for this thread and
 * build the dr7 value on top of their attributes.
 *
 */
static unsigned long ptrace_get_dr7(struct perf_event *bp[])
{
	int i;
	int dr7 = 0;
	struct arch_hw_breakpoint *info;

	for (i = 0; i < HBP_NUM; i++) {
		if (bp[i] && !bp[i]->attr.disabled) {
			info = counter_arch_bp(bp[i]);
			dr7 |= encode_dr7(i, info->len, info->type);
		}
	}

	return dr7;
}

static int ptrace_fill_bp_fields(struct perf_event_attr *attr,
					int len, int type, bool disabled)
{
	int err, bp_len, bp_type;

	err = arch_bp_generic_fields(len, type, &bp_len, &bp_type);
	if (!err) {
		attr->bp_len = bp_len;
		attr->bp_type = bp_type;
		attr->disabled = disabled;
	}

	return err;
}

static struct perf_event *
ptrace_register_breakpoint(struct task_struct *tsk, int len, int type,
				unsigned long addr, bool disabled)
{
	struct perf_event_attr attr;
	int err;

	ptrace_breakpoint_init(&attr);
	attr.bp_addr = addr;

	err = ptrace_fill_bp_fields(&attr, len, type, disabled);
	if (err)
		return ERR_PTR(err);

	return register_user_hw_breakpoint(&attr, ptrace_triggered,
						 NULL, tsk);
}

static int ptrace_modify_breakpoint(struct perf_event *bp, int len, int type,
					int disabled)
{
	struct perf_event_attr attr = bp->attr;
	int err;

	err = ptrace_fill_bp_fields(&attr, len, type, disabled);
	if (err)
		return err;

	return modify_user_hw_breakpoint(bp, &attr);
}

/*
 * Handle ptrace writes to debug register 7.
 */
static int ptrace_write_dr7(struct task_struct *tsk, unsigned long data)
{
	struct thread_struct *thread = &tsk->thread;
	unsigned long old_dr7;
	bool second_pass = false;
	int i, rc, ret = 0;

	data &= ~DR_CONTROL_RESERVED;
	old_dr7 = ptrace_get_dr7(thread->ptrace_bps);

restore:
	rc = 0;
	for (i = 0; i < HBP_NUM; i++) {
		unsigned len, type;
		bool disabled = !decode_dr7(data, i, &len, &type);
		struct perf_event *bp = thread->ptrace_bps[i];

		if (!bp) {
			if (disabled)
				continue;

			bp = ptrace_register_breakpoint(tsk,
					len, type, 0, disabled);
			if (IS_ERR(bp)) {
				rc = PTR_ERR(bp);
				break;
			}

			thread->ptrace_bps[i] = bp;
			continue;
		}

		rc = ptrace_modify_breakpoint(bp, len, type, disabled);
		if (rc)
			break;
	}

	/* Restore if the first pass failed, second_pass shouldn't fail. */
	if (rc && !WARN_ON(second_pass)) {
		ret = rc;
		data = old_dr7;
		second_pass = true;
		goto restore;
	}

	return ret;
}

/*
 * Handle PTRACE_PEEKUSR calls for the debug register area.
 */
static unsigned long ptrace_get_debugreg(struct task_struct *tsk, int n)
{
	struct thread_struct *thread = &tsk->thread;
	unsigned long val = 0;

	if (n < HBP_NUM) {
		int index = array_index_nospec(n, HBP_NUM);
		struct perf_event *bp = thread->ptrace_bps[index];

		if (bp)
			val = bp->hw.info.address;
	} else if (n == 6) {
		val = thread->debugreg6;
	} else if (n == 7) {
		val = thread->ptrace_dr7;
	}
	return val;
}

static int ptrace_set_breakpoint_addr(struct task_struct *tsk, int nr,
				      unsigned long addr)
{
	struct thread_struct *t = &tsk->thread;
	struct perf_event *bp = t->ptrace_bps[nr];
	int err = 0;

	if (!bp) {
		/*
		 * Put stub len and type to create an inactive but correct bp.
		 *
		 * CHECKME: the previous code returned -EIO if the addr wasn't
		 * a valid task virtual addr. The new one will return -EINVAL in
		 *  this case.
		 * -EINVAL may be what we want for in-kernel breakpoints users,
		 * but -EIO looks better for ptrace, since we refuse a register
		 * writing for the user. And anyway this is the previous
		 * behaviour.
		 */
		bp = ptrace_register_breakpoint(tsk,
				X86_BREAKPOINT_LEN_1, X86_BREAKPOINT_WRITE,
				addr, true);
		if (IS_ERR(bp))
			err = PTR_ERR(bp);
		else
			t->ptrace_bps[nr] = bp;
	} else {
		struct perf_event_attr attr = bp->attr;

		attr.bp_addr = addr;
		err = modify_user_hw_breakpoint(bp, &attr);
	}

	return err;
}

/*
 * Handle PTRACE_POKEUSR calls for the debug register area.
 */
static int ptrace_set_debugreg(struct task_struct *tsk, int n,
			       unsigned long val)
{
	struct thread_struct *thread = &tsk->thread;
	/* There are no DR4 or DR5 registers */
	int rc = -EIO;

	if (n < HBP_NUM) {
		rc = ptrace_set_breakpoint_addr(tsk, n, val);
	} else if (n == 6) {
		thread->debugreg6 = val;
		rc = 0;
	} else if (n == 7) {
		rc = ptrace_write_dr7(tsk, val);
		if (!rc)
			thread->ptrace_dr7 = val;
	}
	return rc;
}

/*
 * These access the current or another (stopped) task's io permission
 * bitmap for debugging or core dump.
 */
static int ioperm_active(struct task_struct *target,
			 const struct user_regset *regset)
{
	return target->thread.io_bitmap_max / regset->size;
}

static int ioperm_get(struct task_struct *target,
		      const struct user_regset *regset,
		      unsigned int pos, unsigned int count,
		      void *kbuf, void __user *ubuf)
{
	if (!target->thread.io_bitmap_ptr)
		return -ENXIO;

	return user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				   target->thread.io_bitmap_ptr,
				   0, IO_BITMAP_BYTES);
}

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Make sure the single step bit is not set.
 */
void ptrace_disable(struct task_struct *child)
{
	user_disable_single_step(child);
#ifdef TIF_SYSCALL_EMU
	clear_tsk_thread_flag(child, TIF_SYSCALL_EMU);
#endif
	if (child->thread.ds_area_msr) {
#ifdef X86_BTS
		ptrace_bts_realloc(child, 0, 0);
#endif
		child->thread.debugctlmsr &= ~ds_debugctl_mask();
		if (!child->thread.debugctlmsr)
			clear_tsk_thread_flag(child, TIF_DEBUGCTLMSR);
		clear_tsk_thread_flag(child, TIF_BTS_TRACE_TS);
	}
}

#if defined CONFIG_X86_32 || defined CONFIG_IA32_EMULATION
static const struct user_regset_view user_x86_32_view; /* Initialized below. */
#endif

long arch_ptrace(struct task_struct *child, long request, long addr, long data)
long arch_ptrace(struct task_struct *child, long request,
		 unsigned long addr, unsigned long data)
{
	int ret;
	unsigned long __user *datap = (unsigned long __user *)data;

	switch (request) {
	/* read the word at location addr in the USER area. */
	case PTRACE_PEEKUSR: {
		unsigned long tmp;

		ret = -EIO;
		if ((addr & (sizeof(data) - 1)) || addr < 0 ||
		    addr >= sizeof(struct user))
		if ((addr & (sizeof(data) - 1)) || addr >= sizeof(struct user))
			break;

		tmp = 0;  /* Default return condition */
		if (addr < sizeof(struct user_regs_struct))
			tmp = getreg(child, addr);
		else if (addr >= offsetof(struct user, u_debugreg[0]) &&
			 addr <= offsetof(struct user, u_debugreg[7])) {
			addr -= offsetof(struct user, u_debugreg[0]);
			tmp = ptrace_get_debugreg(child, addr / sizeof(data));
		}
		ret = put_user(tmp, datap);
		break;
	}

	case PTRACE_POKEUSR: /* write the word at location addr in the USER area */
		ret = -EIO;
		if ((addr & (sizeof(data) - 1)) || addr < 0 ||
		    addr >= sizeof(struct user))
		if ((addr & (sizeof(data) - 1)) || addr >= sizeof(struct user))
			break;

		if (addr < sizeof(struct user_regs_struct))
			ret = putreg(child, addr, data);
		else if (addr >= offsetof(struct user, u_debugreg[0]) &&
			 addr <= offsetof(struct user, u_debugreg[7])) {
			addr -= offsetof(struct user, u_debugreg[0]);
			ret = ptrace_set_debugreg(child,
						  addr / sizeof(data), data);
		}
		break;

	case PTRACE_GETREGS:	/* Get all gp regs from the child. */
		return copy_regset_to_user(child,
					   task_user_regset_view(current),
					   REGSET_GENERAL,
					   0, sizeof(struct user_regs_struct),
					   datap);

	case PTRACE_SETREGS:	/* Set all gp regs in the child. */
		return copy_regset_from_user(child,
					     task_user_regset_view(current),
					     REGSET_GENERAL,
					     0, sizeof(struct user_regs_struct),
					     datap);

	case PTRACE_GETFPREGS:	/* Get the child FPU state. */
		return copy_regset_to_user(child,
					   task_user_regset_view(current),
					   REGSET_FP,
					   0, sizeof(struct user_i387_struct),
					   datap);

	case PTRACE_SETFPREGS:	/* Set the child FPU state. */
		return copy_regset_from_user(child,
					     task_user_regset_view(current),
					     REGSET_FP,
					     0, sizeof(struct user_i387_struct),
					     datap);

#ifdef CONFIG_X86_32
	case PTRACE_GETFPXREGS:	/* Get the child extended FPU state. */
		return copy_regset_to_user(child, &user_x86_32_view,
					   REGSET_XFP,
					   0, sizeof(struct user_fxsr_struct),
					   datap) ? -EIO : 0;

	case PTRACE_SETFPXREGS:	/* Set the child extended FPU state. */
		return copy_regset_from_user(child, &user_x86_32_view,
					     REGSET_XFP,
					     0, sizeof(struct user_fxsr_struct),
					     datap) ? -EIO : 0;
#endif

#if defined CONFIG_X86_32 || defined CONFIG_IA32_EMULATION
	case PTRACE_GET_THREAD_AREA:
		if (addr < 0)
			return -EIO;
		ret = do_get_thread_area(child, addr,
					 (struct user_desc __user *) data);
		break;

	case PTRACE_SET_THREAD_AREA:
		if (addr < 0)
			return -EIO;
		ret = do_set_thread_area(child, addr,
					 (struct user_desc __user *) data, 0);
		if ((int) addr < 0)
			return -EIO;
		ret = do_get_thread_area(child, addr,
					(struct user_desc __user *)data);
		break;

	case PTRACE_SET_THREAD_AREA:
		if ((int) addr < 0)
			return -EIO;
		ret = do_set_thread_area(child, addr,
					(struct user_desc __user *)data, 0);
		break;
#endif

#ifdef CONFIG_X86_64
		/* normal 64bit interface to access TLS data.
		   Works just like arch_prctl, except that the arguments
		   are reversed. */
	case PTRACE_ARCH_PRCTL:
		ret = do_arch_prctl(child, data, addr);
		break;
#endif

	/*
	 * These bits need more cooking - not enabled yet:
	 */
#ifdef X86_BTS
	case PTRACE_BTS_CONFIG:
		ret = ptrace_bts_config
			(child, data, (struct ptrace_bts_config __user *)addr);
		break;

	case PTRACE_BTS_STATUS:
		ret = ptrace_bts_status
			(child, data, (struct ptrace_bts_config __user *)addr);
		break;

	case PTRACE_BTS_SIZE:
		ret = ptrace_bts_get_size(child);
		break;

	case PTRACE_BTS_GET:
		ret = ptrace_bts_read_record
			(child, data, (struct bts_struct __user *) addr);
		break;

	case PTRACE_BTS_CLEAR:
		ret = ptrace_bts_clear(child);
		break;

	case PTRACE_BTS_DRAIN:
		ret = ptrace_bts_drain
			(child, data, (struct bts_struct __user *) addr);
		break;
#endif

	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}

	return ret;
}

#ifdef CONFIG_IA32_EMULATION

#include <linux/compat.h>
#include <linux/syscalls.h>
#include <asm/ia32.h>
#include <asm/user32.h>

#define R32(l,q)							\
	case offsetof(struct user32, regs.l):				\
		regs->q = value; break

#define SEG32(rs)							\
	case offsetof(struct user32, regs.rs):				\
		return set_segment_reg(child,				\
				       offsetof(struct user_regs_struct, rs), \
				       value);				\
		break

static int putreg32(struct task_struct *child, unsigned regno, u32 value)
{
	struct pt_regs *regs = task_pt_regs(child);

	switch (regno) {

	SEG32(cs);
	SEG32(ds);
	SEG32(es);
	SEG32(fs);
	SEG32(gs);
	SEG32(ss);

	R32(ebx, bx);
	R32(ecx, cx);
	R32(edx, dx);
	R32(edi, di);
	R32(esi, si);
	R32(ebp, bp);
	R32(eax, ax);
	R32(eip, ip);
	R32(esp, sp);

	case offsetof(struct user32, regs.orig_eax):
		/*
		 * Sign-extend the value so that orig_eax = -1
		 * causes (long)orig_ax < 0 tests to fire correctly.
		 */
		regs->orig_ax = (long) (s32) value;
		 * A 32-bit debugger setting orig_eax means to restore
		 * the state of the task restarting a 32-bit syscall.
		 * Make sure we interpret the -ERESTART* codes correctly
		 * in case the task is not actually still sitting at the
		 * exit from a 32-bit syscall with TS_COMPAT still set.
		 */
		regs->orig_ax = value;
		if (syscall_get_nr(child, regs) >= 0)
			task_thread_info(child)->status |= TS_COMPAT;
		break;

	case offsetof(struct user32, regs.eflags):
		return set_flags(child, value);

	case offsetof(struct user32, u_debugreg[0]) ...
		offsetof(struct user32, u_debugreg[7]):
		regno -= offsetof(struct user32, u_debugreg[0]);
		return ptrace_set_debugreg(child, regno / 4, value);

	default:
		if (regno > sizeof(struct user32) || (regno & 3))
			return -EIO;

		/*
		 * Other dummy fields in the virtual user structure
		 * are ignored
		 */
		break;
	}
	return 0;
}

#undef R32
#undef SEG32

#define R32(l,q)							\
	case offsetof(struct user32, regs.l):				\
		*val = regs->q; break

#define SEG32(rs)							\
	case offsetof(struct user32, regs.rs):				\
		*val = get_segment_reg(child,				\
				       offsetof(struct user_regs_struct, rs)); \
		break

static int getreg32(struct task_struct *child, unsigned regno, u32 *val)
{
	struct pt_regs *regs = task_pt_regs(child);

	switch (regno) {

	SEG32(ds);
	SEG32(es);
	SEG32(fs);
	SEG32(gs);

	R32(cs, cs);
	R32(ss, ss);
	R32(ebx, bx);
	R32(ecx, cx);
	R32(edx, dx);
	R32(edi, di);
	R32(esi, si);
	R32(ebp, bp);
	R32(eax, ax);
	R32(orig_eax, orig_ax);
	R32(eip, ip);
	R32(esp, sp);

	case offsetof(struct user32, regs.eflags):
		*val = get_flags(child);
		break;

	case offsetof(struct user32, u_debugreg[0]) ...
		offsetof(struct user32, u_debugreg[7]):
		regno -= offsetof(struct user32, u_debugreg[0]);
		*val = ptrace_get_debugreg(child, regno / 4);
		break;

	default:
		if (regno > sizeof(struct user32) || (regno & 3))
			return -EIO;

		/*
		 * Other dummy fields in the virtual user structure
		 * are ignored
		 */
		*val = 0;
		break;
	}
	return 0;
}

#undef R32
#undef SEG32

static int genregs32_get(struct task_struct *target,
			 const struct user_regset *regset,
			 unsigned int pos, unsigned int count,
			 void *kbuf, void __user *ubuf)
{
	if (kbuf) {
		compat_ulong_t *k = kbuf;
		while (count > 0) {
		while (count >= sizeof(*k)) {
			getreg32(target, pos, k++);
			count -= sizeof(*k);
			pos += sizeof(*k);
		}
	} else {
		compat_ulong_t __user *u = ubuf;
		while (count > 0) {
		while (count >= sizeof(*u)) {
			compat_ulong_t word;
			getreg32(target, pos, &word);
			if (__put_user(word, u++))
				return -EFAULT;
			count -= sizeof(*u);
			pos += sizeof(*u);
		}
	}

	return 0;
}

static int genregs32_set(struct task_struct *target,
			 const struct user_regset *regset,
			 unsigned int pos, unsigned int count,
			 const void *kbuf, const void __user *ubuf)
{
	int ret = 0;
	if (kbuf) {
		const compat_ulong_t *k = kbuf;
		while (count > 0 && !ret) {
		while (count >= sizeof(*k) && !ret) {
			ret = putreg32(target, pos, *k++);
			count -= sizeof(*k);
			pos += sizeof(*k);
		}
	} else {
		const compat_ulong_t __user *u = ubuf;
		while (count > 0 && !ret) {
		while (count >= sizeof(*u) && !ret) {
			compat_ulong_t word;
			ret = __get_user(word, u++);
			if (ret)
				break;
			ret = putreg32(target, pos, word);
			count -= sizeof(*u);
			pos += sizeof(*u);
		}
	}
	return ret;
}

long compat_arch_ptrace(struct task_struct *child, compat_long_t request,
			compat_ulong_t caddr, compat_ulong_t cdata)
static long ia32_arch_ptrace(struct task_struct *child, compat_long_t request,
			     compat_ulong_t caddr, compat_ulong_t cdata)
{
	unsigned long addr = caddr;
	unsigned long data = cdata;
	void __user *datap = compat_ptr(data);
	int ret;
	__u32 val;

	switch (request) {
	case PTRACE_PEEKUSR:
		ret = getreg32(child, addr, &val);
		if (ret == 0)
			ret = put_user(val, (__u32 __user *)datap);
		break;

	case PTRACE_POKEUSR:
		ret = putreg32(child, addr, data);
		break;

	case PTRACE_GETREGS:	/* Get all gp regs from the child. */
		return copy_regset_to_user(child, &user_x86_32_view,
					   REGSET_GENERAL,
					   0, sizeof(struct user_regs_struct32),
					   datap);

	case PTRACE_SETREGS:	/* Set all gp regs in the child. */
		return copy_regset_from_user(child, &user_x86_32_view,
					     REGSET_GENERAL, 0,
					     sizeof(struct user_regs_struct32),
					     datap);

	case PTRACE_GETFPREGS:	/* Get the child FPU state. */
		return copy_regset_to_user(child, &user_x86_32_view,
					   REGSET_FP, 0,
					   sizeof(struct user_i387_ia32_struct),
					   datap);

	case PTRACE_SETFPREGS:	/* Set the child FPU state. */
		return copy_regset_from_user(
			child, &user_x86_32_view, REGSET_FP,
			0, sizeof(struct user_i387_ia32_struct), datap);

	case PTRACE_GETFPXREGS:	/* Get the child extended FPU state. */
		return copy_regset_to_user(child, &user_x86_32_view,
					   REGSET_XFP, 0,
					   sizeof(struct user32_fxsr_struct),
					   datap);

	case PTRACE_SETFPXREGS:	/* Set the child extended FPU state. */
		return copy_regset_from_user(child, &user_x86_32_view,
					     REGSET_XFP, 0,
					     sizeof(struct user32_fxsr_struct),
					     datap);

	case PTRACE_GET_THREAD_AREA:
	case PTRACE_SET_THREAD_AREA:
		return arch_ptrace(child, request, addr, data);

	default:
		return compat_ptrace_request(child, request, addr, data);
	}

	return ret;
}

#endif	/* CONFIG_IA32_EMULATION */

#ifdef CONFIG_X86_64

static const struct user_regset x86_64_regsets[] = {
#endif /* CONFIG_IA32_EMULATION */

#ifdef CONFIG_X86_X32_ABI
static long x32_arch_ptrace(struct task_struct *child,
			    compat_long_t request, compat_ulong_t caddr,
			    compat_ulong_t cdata)
{
	unsigned long addr = caddr;
	unsigned long data = cdata;
	void __user *datap = compat_ptr(data);
	int ret;

	switch (request) {
	/* Read 32bits at location addr in the USER area.  Only allow
	   to return the lower 32bits of segment and debug registers.  */
	case PTRACE_PEEKUSR: {
		u32 tmp;

		ret = -EIO;
		if ((addr & (sizeof(data) - 1)) || addr >= sizeof(struct user) ||
		    addr < offsetof(struct user_regs_struct, cs))
			break;

		tmp = 0;  /* Default return condition */
		if (addr < sizeof(struct user_regs_struct))
			tmp = getreg(child, addr);
		else if (addr >= offsetof(struct user, u_debugreg[0]) &&
			 addr <= offsetof(struct user, u_debugreg[7])) {
			addr -= offsetof(struct user, u_debugreg[0]);
			tmp = ptrace_get_debugreg(child, addr / sizeof(data));
		}
		ret = put_user(tmp, (__u32 __user *)datap);
		break;
	}

	/* Write the word at location addr in the USER area.  Only allow
	   to update segment and debug registers with the upper 32bits
	   zero-extended. */
	case PTRACE_POKEUSR:
		ret = -EIO;
		if ((addr & (sizeof(data) - 1)) || addr >= sizeof(struct user) ||
		    addr < offsetof(struct user_regs_struct, cs))
			break;

		if (addr < sizeof(struct user_regs_struct))
			ret = putreg(child, addr, data);
		else if (addr >= offsetof(struct user, u_debugreg[0]) &&
			 addr <= offsetof(struct user, u_debugreg[7])) {
			addr -= offsetof(struct user, u_debugreg[0]);
			ret = ptrace_set_debugreg(child,
						  addr / sizeof(data), data);
		}
		break;

	case PTRACE_GETREGS:	/* Get all gp regs from the child. */
		return copy_regset_to_user(child,
					   task_user_regset_view(current),
					   REGSET_GENERAL,
					   0, sizeof(struct user_regs_struct),
					   datap);

	case PTRACE_SETREGS:	/* Set all gp regs in the child. */
		return copy_regset_from_user(child,
					     task_user_regset_view(current),
					     REGSET_GENERAL,
					     0, sizeof(struct user_regs_struct),
					     datap);

	case PTRACE_GETFPREGS:	/* Get the child FPU state. */
		return copy_regset_to_user(child,
					   task_user_regset_view(current),
					   REGSET_FP,
					   0, sizeof(struct user_i387_struct),
					   datap);

	case PTRACE_SETFPREGS:	/* Set the child FPU state. */
		return copy_regset_from_user(child,
					     task_user_regset_view(current),
					     REGSET_FP,
					     0, sizeof(struct user_i387_struct),
					     datap);

	default:
		return compat_ptrace_request(child, request, addr, data);
	}

	return ret;
}
#endif

#ifdef CONFIG_COMPAT
long compat_arch_ptrace(struct task_struct *child, compat_long_t request,
			compat_ulong_t caddr, compat_ulong_t cdata)
{
#ifdef CONFIG_X86_X32_ABI
	if (!is_ia32_task())
		return x32_arch_ptrace(child, request, caddr, cdata);
#endif
#ifdef CONFIG_IA32_EMULATION
	return ia32_arch_ptrace(child, request, caddr, cdata);
#else
	return 0;
#endif
}
#endif	/* CONFIG_COMPAT */

#ifdef CONFIG_X86_64

static struct user_regset x86_64_regsets[] __read_mostly = {
	[REGSET_GENERAL] = {
		.core_note_type = NT_PRSTATUS,
		.n = sizeof(struct user_regs_struct) / sizeof(long),
		.size = sizeof(long), .align = sizeof(long),
		.get = genregs_get, .set = genregs_set
	},
	[REGSET_FP] = {
		.core_note_type = NT_PRFPREG,
		.n = sizeof(struct user_i387_struct) / sizeof(long),
		.size = sizeof(long), .align = sizeof(long),
		.active = xfpregs_active, .get = xfpregs_get, .set = xfpregs_set
		.active = regset_xregset_fpregs_active, .get = xfpregs_get, .set = xfpregs_set
	},
	[REGSET_XSTATE] = {
		.core_note_type = NT_X86_XSTATE,
		.size = sizeof(u64), .align = sizeof(u64),
		.active = xstateregs_active, .get = xstateregs_get,
		.set = xstateregs_set
	},
	[REGSET_IOPERM64] = {
		.core_note_type = NT_386_IOPERM,
		.n = IO_BITMAP_LONGS,
		.size = sizeof(long), .align = sizeof(long),
		.active = ioperm_active, .get = ioperm_get
	},
};

static const struct user_regset_view user_x86_64_view = {
	.name = "x86_64", .e_machine = EM_X86_64,
	.regsets = x86_64_regsets, .n = ARRAY_SIZE(x86_64_regsets)
};

#else  /* CONFIG_X86_32 */

#define user_regs_struct32	user_regs_struct
#define genregs32_get		genregs_get
#define genregs32_set		genregs_set

#define user_i387_ia32_struct	user_i387_struct
#define user32_fxsr_struct	user_fxsr_struct

#endif	/* CONFIG_X86_64 */

#if defined CONFIG_X86_32 || defined CONFIG_IA32_EMULATION
static const struct user_regset x86_32_regsets[] = {
#endif	/* CONFIG_X86_64 */

#if defined CONFIG_X86_32 || defined CONFIG_IA32_EMULATION
static struct user_regset x86_32_regsets[] __read_mostly = {
	[REGSET_GENERAL] = {
		.core_note_type = NT_PRSTATUS,
		.n = sizeof(struct user_regs_struct32) / sizeof(u32),
		.size = sizeof(u32), .align = sizeof(u32),
		.get = genregs32_get, .set = genregs32_set
	},
	[REGSET_FP] = {
		.core_note_type = NT_PRFPREG,
		.n = sizeof(struct user_i387_ia32_struct) / sizeof(u32),
		.size = sizeof(u32), .align = sizeof(u32),
		.active = fpregs_active, .get = fpregs_get, .set = fpregs_set
		.active = regset_fpregs_active, .get = fpregs_get, .set = fpregs_set
	},
	[REGSET_XFP] = {
		.core_note_type = NT_PRXFPREG,
		.n = sizeof(struct user32_fxsr_struct) / sizeof(u32),
		.size = sizeof(u32), .align = sizeof(u32),
		.active = xfpregs_active, .get = xfpregs_get, .set = xfpregs_set
		.active = regset_xregset_fpregs_active, .get = xfpregs_get, .set = xfpregs_set
	},
	[REGSET_XSTATE] = {
		.core_note_type = NT_X86_XSTATE,
		.size = sizeof(u64), .align = sizeof(u64),
		.active = xstateregs_active, .get = xstateregs_get,
		.set = xstateregs_set
	},
	[REGSET_TLS] = {
		.core_note_type = NT_386_TLS,
		.n = GDT_ENTRY_TLS_ENTRIES, .bias = GDT_ENTRY_TLS_MIN,
		.size = sizeof(struct user_desc),
		.align = sizeof(struct user_desc),
		.active = regset_tls_active,
		.get = regset_tls_get, .set = regset_tls_set
	},
	[REGSET_IOPERM32] = {
		.core_note_type = NT_386_IOPERM,
		.n = IO_BITMAP_BYTES / sizeof(u32),
		.size = sizeof(u32), .align = sizeof(u32),
		.active = ioperm_active, .get = ioperm_get
	},
};

static const struct user_regset_view user_x86_32_view = {
	.name = "i386", .e_machine = EM_386,
	.regsets = x86_32_regsets, .n = ARRAY_SIZE(x86_32_regsets)
};
#endif

/*
 * This represents bytes 464..511 in the memory layout exported through
 * the REGSET_XSTATE interface.
 */
u64 xstate_fx_sw_bytes[USER_XSTATE_FX_SW_WORDS];

void update_regset_xstate_info(unsigned int size, u64 xstate_mask)
{
#ifdef CONFIG_X86_64
	x86_64_regsets[REGSET_XSTATE].n = size / sizeof(u64);
#endif
#if defined CONFIG_X86_32 || defined CONFIG_IA32_EMULATION
	x86_32_regsets[REGSET_XSTATE].n = size / sizeof(u64);
#endif
	xstate_fx_sw_bytes[USER_XSTATE_XCR0_WORD] = xstate_mask;
}

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
#ifdef CONFIG_IA32_EMULATION
	if (test_tsk_thread_flag(task, TIF_IA32))
#endif
#if defined CONFIG_X86_32 || defined CONFIG_IA32_EMULATION
		return &user_x86_32_view;
#endif
#ifdef CONFIG_X86_64
	return &user_x86_64_view;
#endif
}

void send_sigtrap(struct task_struct *tsk, struct pt_regs *regs, int error_code)
{
	struct siginfo info;

	tsk->thread.trap_no = 1;
	tsk->thread.error_code = error_code;

	memset(&info, 0, sizeof(info));
	info.si_signo = SIGTRAP;
	info.si_code = TRAP_BRKPT;

	/* User-mode ip? */
	info.si_addr = user_mode_vm(regs) ? (void __user *) regs->ip : NULL;

	/* Send us the fake SIGTRAP */
	force_sig_info(SIGTRAP, &info, tsk);
}

static void syscall_trace(struct pt_regs *regs)
{
	if (!(current->ptrace & PT_PTRACED))
		return;

#if 0
	printk("trace %s ip %lx sp %lx ax %d origrax %d caller %lx tiflags %x ptrace %x\n",
	       current->comm,
	       regs->ip, regs->sp, regs->ax, regs->orig_ax, __builtin_return_address(0),
	       current_thread_info()->flags, current->ptrace);
#endif

	ptrace_notify(SIGTRAP | ((current->ptrace & PT_TRACESYSGOOD)
				? 0x80 : 0));
	/*
	 * this isn't the same as continuing with a signal, but it will do
	 * for normal use.  strace only continues with a signal if the
	 * stopping signal is not SIGTRAP.  -brl
	 */
	if (current->exit_code) {
		send_sig(current->exit_code, current, 1);
		current->exit_code = 0;
	}
}

#ifdef CONFIG_X86_32
# define IS_IA32	1
#elif defined CONFIG_IA32_EMULATION
# define IS_IA32	test_thread_flag(TIF_IA32)
#else
# define IS_IA32	0
#endif

/*
 * We must return the syscall number to actually look up in the table.
 * This can be -1L to skip running any syscall at all.
 */
asmregparm long syscall_trace_enter(struct pt_regs *regs)
{
	long ret = 0;

	/*
	 * If we stepped into a sysenter/syscall insn, it trapped in
	 * kernel mode; do_debug() cleared TF and set TIF_SINGLESTEP.
	 * If user-mode had set TF itself, then it's still clear from
	 * do_debug() and we need to set it again to restore the user
	 * state.  If we entered on the slow path, TF was already set.
	 */
	if (test_thread_flag(TIF_SINGLESTEP))
		regs->flags |= X86_EFLAGS_TF;

	/* do the secure computing check first */
	secure_computing(regs->orig_ax);

	if (unlikely(test_thread_flag(TIF_SYSCALL_EMU)))
		ret = -1L;

	if (ret || test_thread_flag(TIF_SYSCALL_TRACE))
		syscall_trace(regs);

	if (unlikely(current->audit_context)) {
		if (IS_IA32)
			audit_syscall_entry(AUDIT_ARCH_I386,
					    regs->orig_ax,
					    regs->bx, regs->cx,
					    regs->dx, regs->si);
#ifdef CONFIG_X86_64
		else
			audit_syscall_entry(AUDIT_ARCH_X86_64,
					    regs->orig_ax,
					    regs->di, regs->si,
					    regs->dx, regs->r10);
#endif
	}

	return ret ?: regs->orig_ax;
}

asmregparm void syscall_trace_leave(struct pt_regs *regs)
{
	if (unlikely(current->audit_context))
		audit_syscall_exit(AUDITSC_RESULT(regs->ax), regs->ax);

	if (test_thread_flag(TIF_SYSCALL_TRACE))
		syscall_trace(regs);

	/*
	 * If TIF_SYSCALL_EMU is set, we only get here because of
	 * TIF_SINGLESTEP (i.e. this is PTRACE_SYSEMU_SINGLESTEP).
	 * We already reported this syscall instruction in
	 * syscall_trace_enter(), so don't do any more now.
	 */
	if (unlikely(test_thread_flag(TIF_SYSCALL_EMU)))
		return;

	/*
	 * If we are single-stepping, synthesize a trap to follow the
	 * system call instruction.
	 */
	if (test_thread_flag(TIF_SINGLESTEP) &&
	    (current->ptrace & PT_PTRACED))
		send_sigtrap(current, regs, 0);
}
static void fill_sigtrap_info(struct task_struct *tsk,
				struct pt_regs *regs,
				int error_code, int si_code,
				struct siginfo *info)
{
	tsk->thread.trap_nr = X86_TRAP_DB;
	tsk->thread.error_code = error_code;

	memset(info, 0, sizeof(*info));
	info->si_signo = SIGTRAP;
	info->si_code = si_code;
	info->si_addr = user_mode(regs) ? (void __user *)regs->ip : NULL;
}

void user_single_step_siginfo(struct task_struct *tsk,
				struct pt_regs *regs,
				struct siginfo *info)
{
	fill_sigtrap_info(tsk, regs, 0, TRAP_BRKPT, info);
}

void send_sigtrap(struct task_struct *tsk, struct pt_regs *regs,
					 int error_code, int si_code)
{
	struct siginfo info;

	fill_sigtrap_info(tsk, regs, error_code, si_code, &info);
	/* Send us the fake SIGTRAP */
	force_sig_info(SIGTRAP, &info, tsk);
}
