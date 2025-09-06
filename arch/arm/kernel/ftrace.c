/*
 * Dynamic function tracing support.
 *
 * Copyright (C) 2008 Abhishek Sagar <sagar.abhishek@gmail.com>
 * Copyright (C) 2010 Rabin Vincent <rabin@rab.in>
 *
 * For licencing details, see COPYING.
 *
 * Defines low-level handling of mcount calls when the kernel
 * is compiled with the -pg flag. When using dynamic ftrace, the
 * mcount call-sites get patched lazily with NOP till they are
 * enabled. All code mutation routines here take effect atomically.
 */

#include <linux/ftrace.h>

#include <asm/cacheflush.h>
#include <asm/ftrace.h>

#define PC_OFFSET      8
#define BL_OPCODE      0xeb000000
#define BL_OFFSET_MASK 0x00ffffff

static unsigned long bl_insn;
static const unsigned long NOP = 0xe1a00000; /* mov r0, r0 */

unsigned char *ftrace_nop_replace(void)
{
	return (char *)&NOP;
}

/* construct a branch (BL) instruction to addr */
unsigned char *ftrace_call_replace(unsigned long pc, unsigned long addr)
{
	long offset;

	offset = (long)addr - (long)(pc + PC_OFFSET);
	if (unlikely(offset < -33554432 || offset > 33554428)) {
		/* Can't generate branches that far (from ARM ARM). Ftrace
		 * doesn't generate branches outside of kernel text.
		 */
		WARN_ON_ONCE(1);
		return NULL;
	}
	offset = (offset >> 2) & BL_OFFSET_MASK;
	bl_insn = BL_OPCODE | offset;
	return (unsigned char *)&bl_insn;
}

int ftrace_modify_code(unsigned long pc, unsigned char *old_code,
		       unsigned char *new_code)
{
	unsigned long err = 0, replaced = 0, old, new;

	old = *(unsigned long *)old_code;
	new = *(unsigned long *)new_code;

	__asm__ __volatile__ (
		"1:  ldr    %1, [%2]  \n"
		"    cmp    %1, %4    \n"
		"2:  streq  %3, [%2]  \n"
		"    cmpne  %1, %3    \n"
		"    movne  %0, #2    \n"
		"3:\n"

		".section .fixup, \"ax\"\n"
		"4:  mov  %0, #1  \n"
		"    b    3b      \n"
		".previous\n"

		".section __ex_table, \"a\"\n"
		"    .long 1b, 4b \n"
		"    .long 2b, 4b \n"
		".previous\n"

		: "=r"(err), "=r"(replaced)
		: "r"(pc), "r"(new), "r"(old), "0"(err), "1"(replaced)
		: "memory");

	if (!err && (replaced == old))
		flush_icache_range(pc, pc + MCOUNT_INSN_SIZE);

	return err;
 * mcount call-sites get patched with NOP till they are enabled.
 * All code mutation routines here are called under stop_machine().
 */

#include <linux/ftrace.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/stop_machine.h>

#include <asm/cacheflush.h>
#include <asm/opcodes.h>
#include <asm/ftrace.h>
#include <asm/insn.h>

#ifdef CONFIG_THUMB2_KERNEL
#define	NOP		0xf85deb04	/* pop.w {lr} */
#else
#define	NOP		0xe8bd4000	/* pop {lr} */
#endif

#ifdef CONFIG_DYNAMIC_FTRACE

static int __ftrace_modify_code(void *data)
{
	int *command = data;

	set_kernel_text_rw();
	ftrace_modify_all_code(*command);
	set_kernel_text_ro();

	return 0;
}

void arch_ftrace_update_code(int command)
{
	stop_machine(__ftrace_modify_code, &command, NULL);
}

#ifdef CONFIG_OLD_MCOUNT
#define OLD_MCOUNT_ADDR	((unsigned long) mcount)
#define OLD_FTRACE_ADDR ((unsigned long) ftrace_caller_old)

#define	OLD_NOP		0xe1a00000	/* mov r0, r0 */

static unsigned long ftrace_nop_replace(struct dyn_ftrace *rec)
{
	return rec->arch.old_mcount ? OLD_NOP : NOP;
}

static unsigned long adjust_address(struct dyn_ftrace *rec, unsigned long addr)
{
	if (!rec->arch.old_mcount)
		return addr;

	if (addr == MCOUNT_ADDR)
		addr = OLD_MCOUNT_ADDR;
	else if (addr == FTRACE_ADDR)
		addr = OLD_FTRACE_ADDR;

	return addr;
}
#else
static unsigned long ftrace_nop_replace(struct dyn_ftrace *rec)
{
	return NOP;
}

static unsigned long adjust_address(struct dyn_ftrace *rec, unsigned long addr)
{
	return addr;
}
#endif

int ftrace_arch_code_modify_prepare(void)
{
	set_all_modules_text_rw();
	return 0;
}

int ftrace_arch_code_modify_post_process(void)
{
	set_all_modules_text_ro();
	/* Make sure any TLB misses during machine stop are cleared. */
	flush_tlb_all();
	return 0;
}

static unsigned long ftrace_call_replace(unsigned long pc, unsigned long addr)
{
	return arm_gen_branch_link(pc, addr);
}

static int ftrace_modify_code(unsigned long pc, unsigned long old,
			      unsigned long new, bool validate)
{
	unsigned long replaced;

	if (IS_ENABLED(CONFIG_THUMB2_KERNEL)) {
		old = __opcode_to_mem_thumb32(old);
		new = __opcode_to_mem_thumb32(new);
	} else {
		old = __opcode_to_mem_arm(old);
		new = __opcode_to_mem_arm(new);
	}

	if (validate) {
		if (probe_kernel_read(&replaced, (void *)pc, MCOUNT_INSN_SIZE))
			return -EFAULT;

		if (replaced != old)
			return -EINVAL;
	}

	if (probe_kernel_write((void *)pc, &new, MCOUNT_INSN_SIZE))
		return -EPERM;

	flush_icache_range(pc, pc + MCOUNT_INSN_SIZE);

	return 0;
}

int ftrace_update_ftrace_func(ftrace_func_t func)
{
	int ret;
	unsigned long pc, old;
	unsigned char *new;

	pc = (unsigned long)&ftrace_call;
	memcpy(&old, &ftrace_call, MCOUNT_INSN_SIZE);
	new = ftrace_call_replace(pc, (unsigned long)func);
	ret = ftrace_modify_code(pc, (unsigned char *)&old, new);
	return ret;
}

int ftrace_mcount_set(unsigned long *data)
{
	unsigned long pc, old;
	unsigned long *addr = data;
	unsigned char *new;

	pc = (unsigned long)&mcount_call;
	memcpy(&old, &mcount_call, MCOUNT_INSN_SIZE);
	new = ftrace_call_replace(pc, *addr);
	*addr = ftrace_modify_code(pc, (unsigned char *)&old, new);
	return 0;
}

/* run from kstop_machine */
int __init ftrace_dyn_arch_init(void *data)
{
	ftrace_mcount_set(data);
	return 0;
}
	unsigned long pc;
	unsigned long new;
	int ret;

	pc = (unsigned long)&ftrace_call;
	new = ftrace_call_replace(pc, (unsigned long)func);

	ret = ftrace_modify_code(pc, 0, new, false);

#ifdef CONFIG_OLD_MCOUNT
	if (!ret) {
		pc = (unsigned long)&ftrace_call_old;
		new = ftrace_call_replace(pc, (unsigned long)func);

		ret = ftrace_modify_code(pc, 0, new, false);
	}
#endif

	return ret;
}

int ftrace_make_call(struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned long new, old;
	unsigned long ip = rec->ip;

	old = ftrace_nop_replace(rec);
	new = ftrace_call_replace(ip, adjust_address(rec, addr));

	return ftrace_modify_code(rec->ip, old, new, true);
}

int ftrace_make_nop(struct module *mod,
		    struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned long ip = rec->ip;
	unsigned long old;
	unsigned long new;
	int ret;

	old = ftrace_call_replace(ip, adjust_address(rec, addr));
	new = ftrace_nop_replace(rec);
	ret = ftrace_modify_code(ip, old, new, true);

#ifdef CONFIG_OLD_MCOUNT
	if (ret == -EINVAL && addr == MCOUNT_ADDR) {
		rec->arch.old_mcount = true;

		old = ftrace_call_replace(ip, adjust_address(rec, addr));
		new = ftrace_nop_replace(rec);
		ret = ftrace_modify_code(ip, old, new, true);
	}
#endif

	return ret;
}

int __init ftrace_dyn_arch_init(void)
{
	return 0;
}
#endif /* CONFIG_DYNAMIC_FTRACE */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
void prepare_ftrace_return(unsigned long *parent, unsigned long self_addr,
			   unsigned long frame_pointer)
{
	unsigned long return_hooker = (unsigned long) &return_to_handler;
	struct ftrace_graph_ent trace;
	unsigned long old;
	int err;

	if (unlikely(atomic_read(&current->tracing_graph_pause)))
		return;

	old = *parent;
	*parent = return_hooker;

	trace.func = self_addr;
	trace.depth = current->curr_ret_stack + 1;

	/* Only trace if the calling function expects to */
	if (!ftrace_graph_entry(&trace)) {
		*parent = old;
		return;
	}

	err = ftrace_push_return_trace(old, self_addr, &trace.depth,
				       frame_pointer);
	if (err == -EBUSY) {
		*parent = old;
		return;
	}
}

#ifdef CONFIG_DYNAMIC_FTRACE
extern unsigned long ftrace_graph_call;
extern unsigned long ftrace_graph_call_old;
extern void ftrace_graph_caller_old(void);

static int __ftrace_modify_caller(unsigned long *callsite,
				  void (*func) (void), bool enable)
{
	unsigned long caller_fn = (unsigned long) func;
	unsigned long pc = (unsigned long) callsite;
	unsigned long branch = arm_gen_branch(pc, caller_fn);
	unsigned long nop = 0xe1a00000;	/* mov r0, r0 */
	unsigned long old = enable ? nop : branch;
	unsigned long new = enable ? branch : nop;

	return ftrace_modify_code(pc, old, new, true);
}

static int ftrace_modify_graph_caller(bool enable)
{
	int ret;

	ret = __ftrace_modify_caller(&ftrace_graph_call,
				     ftrace_graph_caller,
				     enable);

#ifdef CONFIG_OLD_MCOUNT
	if (!ret)
		ret = __ftrace_modify_caller(&ftrace_graph_call_old,
					     ftrace_graph_caller_old,
					     enable);
#endif

	return ret;
}

int ftrace_enable_ftrace_graph_caller(void)
{
	return ftrace_modify_graph_caller(true);
}

int ftrace_disable_ftrace_graph_caller(void)
{
	return ftrace_modify_graph_caller(false);
}
#endif /* CONFIG_DYNAMIC_FTRACE */
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
