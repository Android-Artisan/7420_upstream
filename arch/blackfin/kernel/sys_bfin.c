/*
 * File:         arch/blackfin/kernel/sys_bfin.c
 * Based on:
 * Author:
 *
 * Created:
 * Description:  This file contains various random system calls that
 *               have a non-standard calling sequence on the Linux/bfin
 *               platform.
 *
 * Modified:
 *               Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/smp_lock.h>
 * contains various random system calls that have a non-standard
 * calling sequence on the Linux/Blackfin platform.
 *
 * Copyright 2004-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later
 */

#include <linux/spinlock.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/syscalls.h>
#include <linux/mman.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/ipc.h>
#include <linux/unistd.h>

#include <asm/cacheflush.h>
#include <asm/dma.h>

/* common code for old and new mmaps */
static inline long
do_mmap2(unsigned long addr, unsigned long len,
	 unsigned long prot, unsigned long flags,
	 unsigned long fd, unsigned long pgoff)
{
	int error = -EBADF;
	struct file *file = NULL;

	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	if (!(flags & MAP_ANONYMOUS)) {
		file = fget(fd);
		if (!file)
			goto out;
	}

	down_write(&current->mm->mmap_sem);
	error = do_mmap_pgoff(file, addr, len, prot, flags, pgoff);
	up_write(&current->mm->mmap_sem);

	if (file)
		fput(file);
 out:
	return error;
}

asmlinkage long sys_mmap2(unsigned long addr, unsigned long len,
			  unsigned long prot, unsigned long flags,
			  unsigned long fd, unsigned long pgoff)
{
	return do_mmap2(addr, len, prot, flags, fd, pgoff);
}

asmlinkage int sys_getpagesize(void)
{
	return PAGE_SIZE;
}
#include <asm/cachectl.h>
#include <asm/ptrace.h>

asmlinkage void *sys_sram_alloc(size_t size, unsigned long flags)
{
	return sram_alloc_with_lsl(size, flags);
}

asmlinkage int sys_sram_free(const void *addr)
{
	return sram_free_with_lsl(addr);
}

asmlinkage void *sys_dma_memcpy(void *dest, const void *src, size_t len)
{
	return safe_dma_memcpy(dest, src, len);
}

#if defined(CONFIG_FB) || defined(CONFIG_FB_MODULE)
#include <linux/fb.h>
#include <linux/export.h>
unsigned long get_fb_unmapped_area(struct file *filp, unsigned long orig_addr,
	unsigned long len, unsigned long pgoff, unsigned long flags)
{
	struct fb_info *info = filp->private_data;
	return (unsigned long)info->screen_base;
}
EXPORT_SYMBOL(get_fb_unmapped_area);
#endif

/* Needed for legacy userspace atomic emulation */
static DEFINE_SPINLOCK(bfin_spinlock_lock);

#ifdef CONFIG_SYS_BFIN_SPINLOCK_L1
__attribute__((l1_text))
#endif
asmlinkage int sys_bfin_spinlock(int *p)
{
	int ret, tmp = 0;

	spin_lock(&bfin_spinlock_lock); /* This would also hold kernel preemption. */
	ret = get_user(tmp, p);
	if (likely(ret == 0)) {
		if (unlikely(tmp))
			ret = 1;
		else
			put_user(1, p);
	}
	spin_unlock(&bfin_spinlock_lock);

	return ret;
}

SYSCALL_DEFINE3(cacheflush, unsigned long, addr, unsigned long, len, int, op)
{
	if (is_user_addr_valid(current, addr, len) != 0)
		return -EINVAL;

	if (op & DCACHE)
		blackfin_dcache_flush_range(addr, addr + len);
	if (op & ICACHE)
		blackfin_icache_flush_range(addr, addr + len);

	return 0;
}
