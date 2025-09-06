/*
 * Copyright (C) 1992 Krishna Balasubramanian and Linus Torvalds
 * Copyright (C) 1999 Ingo Molnar <mingo@redhat.com>
 * Copyright (C) 2002 Andi Kleen
 *
 * This handles calls from both 32bit and 64bit mode.
 */

#include <linux/errno.h>
#include <linux/gfp.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/vmalloc.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/ldt.h>
#include <asm/desc.h>
#include <asm/mmu_context.h>

#ifdef CONFIG_SMP
static void flush_ldt(void *current_mm)
{
	if (current->active_mm == current_mm)
		load_LDT(&current->active_mm->context);
}
#endif

static int alloc_ldt(mm_context_t *pc, int mincount, int reload)
{
	void *oldldt, *newldt;
	int oldsize;

	if (mincount <= pc->size)
		return 0;
	oldsize = pc->size;
	mincount = (mincount + (PAGE_SIZE / LDT_ENTRY_SIZE - 1)) &
			(~(PAGE_SIZE / LDT_ENTRY_SIZE - 1));
	if (mincount * LDT_ENTRY_SIZE > PAGE_SIZE)
		newldt = vmalloc(mincount * LDT_ENTRY_SIZE);
	else
		newldt = (void *)__get_free_page(GFP_KERNEL);

	if (!newldt)
		return -ENOMEM;

	if (oldsize)
		memcpy(newldt, pc->ldt, oldsize * LDT_ENTRY_SIZE);
	oldldt = pc->ldt;
	memset(newldt + oldsize * LDT_ENTRY_SIZE, 0,
	       (mincount - oldsize) * LDT_ENTRY_SIZE);

#ifdef CONFIG_X86_64
	/* CHECKME: Do we really need this ? */
	wmb();
#endif
	pc->ldt = newldt;
	wmb();
	pc->size = mincount;
	wmb();

	if (reload) {
#ifdef CONFIG_SMP
		preempt_disable();
		load_LDT(pc);
		if (!cpus_equal(current->mm->cpu_vm_mask,
				cpumask_of_cpu(smp_processor_id())))
			smp_call_function(flush_ldt, current->mm, 1);
		preempt_enable();
#else
		load_LDT(pc);
#endif
	}
	if (oldsize) {
		if (oldsize * LDT_ENTRY_SIZE > PAGE_SIZE)
			vfree(oldldt);
		else
			put_page(virt_to_page(oldldt));
	}
	return 0;
}

static inline int copy_ldt(mm_context_t *new, mm_context_t *old)
{
	int err = alloc_ldt(new, old->size, 0);

	if (err < 0)
		return err;
	memcpy(new->ldt, old->ldt, old->size * LDT_ENTRY_SIZE);
	return 0;
#include <linux/syscalls.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <linux/kaiser.h>

#include <asm/ldt.h>
#include <asm/desc.h>
#include <asm/mmu_context.h>
#include <asm/syscalls.h>

/* context.lock is held for us, so we don't need any locking. */
static void flush_ldt(void *current_mm)
{
	mm_context_t *pc;

	if (current->active_mm != current_mm)
		return;

	pc = &current->active_mm->context;
	set_ldt(pc->ldt->entries, pc->ldt->size);
}

static void __free_ldt_struct(struct ldt_struct *ldt)
{
	if (ldt->size * LDT_ENTRY_SIZE > PAGE_SIZE)
		vfree(ldt->entries);
	else
		free_page((unsigned long)ldt->entries);
	kfree(ldt);
}

/* The caller must call finalize_ldt_struct on the result. LDT starts zeroed. */
static struct ldt_struct *alloc_ldt_struct(int size)
{
	struct ldt_struct *new_ldt;
	int alloc_size;
	int ret;

	if (size > LDT_ENTRIES)
		return NULL;

	new_ldt = kmalloc(sizeof(struct ldt_struct), GFP_KERNEL);
	if (!new_ldt)
		return NULL;

	BUILD_BUG_ON(LDT_ENTRY_SIZE != sizeof(struct desc_struct));
	alloc_size = size * LDT_ENTRY_SIZE;

	/*
	 * Xen is very picky: it requires a page-aligned LDT that has no
	 * trailing nonzero bytes in any page that contains LDT descriptors.
	 * Keep it simple: zero the whole allocation and never allocate less
	 * than PAGE_SIZE.
	 */
	if (alloc_size > PAGE_SIZE)
		new_ldt->entries = vzalloc(alloc_size);
	else
		new_ldt->entries = (void *)get_zeroed_page(GFP_KERNEL);

	if (!new_ldt->entries) {
		kfree(new_ldt);
		return NULL;
	}

	ret = kaiser_add_mapping((unsigned long)new_ldt->entries, alloc_size,
				 __PAGE_KERNEL);
	new_ldt->size = size;
	if (ret) {
		__free_ldt_struct(new_ldt);
		return NULL;
	}
	return new_ldt;
}

/* After calling this, the LDT is immutable. */
static void finalize_ldt_struct(struct ldt_struct *ldt)
{
	paravirt_alloc_ldt(ldt->entries, ldt->size);
}

/* context.lock is held */
static void install_ldt(struct mm_struct *current_mm,
			struct ldt_struct *ldt)
{
	/* Synchronizes with lockless_dereference in load_mm_ldt. */
	smp_store_release(&current_mm->context.ldt, ldt);

	/* Activate the LDT for all CPUs using current_mm. */
	on_each_cpu_mask(mm_cpumask(current_mm), flush_ldt, current_mm, true);
}

static void free_ldt_struct(struct ldt_struct *ldt)
{
	if (likely(!ldt))
		return;

	kaiser_remove_mapping((unsigned long)ldt->entries,
			      ldt->size * LDT_ENTRY_SIZE);
	paravirt_free_ldt(ldt->entries, ldt->size);
	__free_ldt_struct(ldt);
}

/*
 * we do not have to muck with descriptors here, that is
 * done in switch_mm() as needed.
 */
int init_new_context_ldt(struct task_struct *tsk, struct mm_struct *mm)
{
	struct ldt_struct *new_ldt;
	struct mm_struct *old_mm;
	int retval = 0;

	mutex_init(&mm->context.lock);
	mm->context.size = 0;
	old_mm = current->mm;
	if (old_mm && old_mm->context.size > 0) {
		mutex_lock(&old_mm->context.lock);
		retval = copy_ldt(&mm->context, &old_mm->context);
		mutex_unlock(&old_mm->context.lock);
	}
	old_mm = current->mm;
	if (!old_mm) {
		mm->context.ldt = NULL;
		return 0;
	}

	mutex_lock(&old_mm->context.lock);
	if (!old_mm->context.ldt) {
		mm->context.ldt = NULL;
		goto out_unlock;
	}

	new_ldt = alloc_ldt_struct(old_mm->context.ldt->size);
	if (!new_ldt) {
		retval = -ENOMEM;
		goto out_unlock;
	}

	memcpy(new_ldt->entries, old_mm->context.ldt->entries,
	       new_ldt->size * LDT_ENTRY_SIZE);
	finalize_ldt_struct(new_ldt);

	mm->context.ldt = new_ldt;

out_unlock:
	mutex_unlock(&old_mm->context.lock);
	return retval;
}

/*
 * No need to lock the MM as we are the last user
 *
 * 64bit: Don't touch the LDT register - we're already in the next thread.
 */
void destroy_context_ldt(struct mm_struct *mm)
{
	if (mm->context.size) {
#ifdef CONFIG_X86_32
		/* CHECKME: Can this ever happen ? */
		if (mm == current->active_mm)
			clear_LDT();
#endif
		if (mm->context.size * LDT_ENTRY_SIZE > PAGE_SIZE)
			vfree(mm->context.ldt);
		else
			put_page(virt_to_page(mm->context.ldt));
		mm->context.size = 0;
	}
	free_ldt_struct(mm->context.ldt);
	mm->context.ldt = NULL;
}

static int read_ldt(void __user *ptr, unsigned long bytecount)
{
	int err;
	unsigned long size;
	struct mm_struct *mm = current->mm;

	if (!mm->context.size)
		return 0;
	if (bytecount > LDT_ENTRY_SIZE * LDT_ENTRIES)
		bytecount = LDT_ENTRY_SIZE * LDT_ENTRIES;

	mutex_lock(&mm->context.lock);
	size = mm->context.size * LDT_ENTRY_SIZE;
	if (size > bytecount)
		size = bytecount;

	err = 0;
	if (copy_to_user(ptr, mm->context.ldt, size))
		err = -EFAULT;
	mutex_unlock(&mm->context.lock);
	if (err < 0)
		goto error_return;
	if (size != bytecount) {
		/* zero-fill the rest */
		if (clear_user(ptr + size, bytecount - size) != 0) {
			err = -EFAULT;
			goto error_return;
		}
	}
	return bytecount;
error_return:
	return err;
	int retval;
	unsigned long size;
	struct mm_struct *mm = current->mm;

	mutex_lock(&mm->context.lock);

	if (!mm->context.ldt) {
		retval = 0;
		goto out_unlock;
	}

	if (bytecount > LDT_ENTRY_SIZE * LDT_ENTRIES)
		bytecount = LDT_ENTRY_SIZE * LDT_ENTRIES;

	size = mm->context.ldt->size * LDT_ENTRY_SIZE;
	if (size > bytecount)
		size = bytecount;

	if (copy_to_user(ptr, mm->context.ldt->entries, size)) {
		retval = -EFAULT;
		goto out_unlock;
	}

	if (size != bytecount) {
		/* Zero-fill the rest and pretend we read bytecount bytes. */
		if (clear_user(ptr + size, bytecount - size)) {
			retval = -EFAULT;
			goto out_unlock;
		}
	}
	retval = bytecount;

out_unlock:
	mutex_unlock(&mm->context.lock);
	return retval;
}

static int read_default_ldt(void __user *ptr, unsigned long bytecount)
{
	/* CHECKME: Can we use _one_ random number ? */
#ifdef CONFIG_X86_32
	unsigned long size = 5 * sizeof(struct desc_struct);
#else
	unsigned long size = 128;
#endif
	if (bytecount > size)
		bytecount = size;
	if (clear_user(ptr, bytecount))
		return -EFAULT;
	return bytecount;
}

static int write_ldt(void __user *ptr, unsigned long bytecount, int oldmode)
{
	struct mm_struct *mm = current->mm;
	struct desc_struct ldt;
	int error;
	struct user_desc ldt_info;
	int oldsize, newsize;
	struct ldt_struct *new_ldt, *old_ldt;

	error = -EINVAL;
	if (bytecount != sizeof(ldt_info))
		goto out;
	error = -EFAULT;
	if (copy_from_user(&ldt_info, ptr, sizeof(ldt_info)))
		goto out;

	error = -EINVAL;
	if (ldt_info.entry_number >= LDT_ENTRIES)
		goto out;
	if (ldt_info.contents == 3) {
		if (oldmode)
			goto out;
		if (ldt_info.seg_not_present == 0)
			goto out;
	}

	mutex_lock(&mm->context.lock);
	if (ldt_info.entry_number >= mm->context.size) {
		error = alloc_ldt(&current->mm->context,
				  ldt_info.entry_number + 1, 1);
		if (error < 0)
			goto out_unlock;
	}

	/* Allow LDTs to be cleared by the user. */
	if (ldt_info.base_addr == 0 && ldt_info.limit == 0) {
		if (oldmode || LDT_empty(&ldt_info)) {
			memset(&ldt, 0, sizeof(ldt));
			goto install;
		}
	}

	fill_ldt(&ldt, &ldt_info);
	if (oldmode)
		ldt.avl = 0;

	/* Install the new entry ...  */
install:
	write_ldt_entry(mm->context.ldt, ldt_info.entry_number, &ldt);
	if ((oldmode && !ldt_info.base_addr && !ldt_info.limit) ||
	    LDT_empty(&ldt_info)) {
		/* The user wants to clear the entry. */
		memset(&ldt, 0, sizeof(ldt));
	} else {
		if (!IS_ENABLED(CONFIG_X86_16BIT) && !ldt_info.seg_32bit) {
			error = -EINVAL;
			goto out;
		}

		fill_ldt(&ldt, &ldt_info);
		if (oldmode)
			ldt.avl = 0;
	}

	mutex_lock(&mm->context.lock);

	old_ldt = mm->context.ldt;
	oldsize = old_ldt ? old_ldt->size : 0;
	newsize = max((int)(ldt_info.entry_number + 1), oldsize);

	error = -ENOMEM;
	new_ldt = alloc_ldt_struct(newsize);
	if (!new_ldt)
		goto out_unlock;

	if (old_ldt)
		memcpy(new_ldt->entries, old_ldt->entries, oldsize * LDT_ENTRY_SIZE);
	new_ldt->entries[ldt_info.entry_number] = ldt;
	finalize_ldt_struct(new_ldt);

	install_ldt(mm, new_ldt);
	free_ldt_struct(old_ldt);
	error = 0;

out_unlock:
	mutex_unlock(&mm->context.lock);
out:
	return error;
}

SYSCALL_DEFINE3(modify_ldt, int , func , void __user * , ptr ,
		unsigned long , bytecount)
{
	int ret = -ENOSYS;

	switch (func) {
	case 0:
		ret = read_ldt(ptr, bytecount);
		break;
	case 1:
		ret = write_ldt(ptr, bytecount, 1);
		break;
	case 2:
		ret = read_default_ldt(ptr, bytecount);
		break;
	case 0x11:
		ret = write_ldt(ptr, bytecount, 0);
		break;
	}
	/*
	 * The SYSCALL_DEFINE() macros give us an 'unsigned long'
	 * return type, but tht ABI for sys_modify_ldt() expects
	 * 'int'.  This cast gives us an int-sized value in %rax
	 * for the return code.  The 'unsigned' is necessary so
	 * the compiler does not try to sign-extend the negative
	 * return codes into the high half of the register when
	 * taking the value from int->long.
	 */
	return (unsigned int)ret;
}
