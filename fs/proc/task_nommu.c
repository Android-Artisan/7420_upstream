
#include <linux/mm.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/mount.h>
#include <linux/ptrace.h>
#include <linux/fs_struct.h>
#include <linux/mount.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include "internal.h"

/*
 * Logic: we've got two memory sums for each process, "shared", and
 * "non-shared". Shared memory may get counted more then once, for
 * "non-shared". Shared memory may get counted more than once, for
 * each process that owns it. Non-shared memory is counted
 * accurately.
 */
void task_mem(struct seq_file *m, struct mm_struct *mm)
{
	struct vm_list_struct *vml;
	unsigned long bytes = 0, sbytes = 0, slack = 0;
        
	down_read(&mm->mmap_sem);
	for (vml = mm->context.vmlist; vml; vml = vml->next) {
		if (!vml->vma)
			continue;

		bytes += kobjsize(vml);
		if (atomic_read(&mm->mm_count) > 1 ||
		    atomic_read(&vml->vma->vm_usage) > 1
		    ) {
			sbytes += kobjsize((void *) vml->vma->vm_start);
			sbytes += kobjsize(vml->vma);
		} else {
			bytes += kobjsize((void *) vml->vma->vm_start);
			bytes += kobjsize(vml->vma);
			slack += kobjsize((void *) vml->vma->vm_start) -
				(vml->vma->vm_end - vml->vma->vm_start);
	struct vm_area_struct *vma;
	struct vm_region *region;
	struct rb_node *p;
	unsigned long bytes = 0, sbytes = 0, slack = 0, size;
        
	down_read(&mm->mmap_sem);
	for (p = rb_first(&mm->mm_rb); p; p = rb_next(p)) {
		vma = rb_entry(p, struct vm_area_struct, vm_rb);

		bytes += kobjsize(vma);

		region = vma->vm_region;
		if (region) {
			size = kobjsize(region);
			size += region->vm_end - region->vm_start;
		} else {
			size = vma->vm_end - vma->vm_start;
		}

		if (atomic_read(&mm->mm_count) > 1 ||
		    vma->vm_flags & VM_MAYSHARE) {
			sbytes += size;
		} else {
			bytes += size;
			if (region)
				slack = region->vm_end - vma->vm_end;
		}
	}

	if (atomic_read(&mm->mm_count) > 1)
		sbytes += kobjsize(mm);
	else
		bytes += kobjsize(mm);
	
	if (current->fs && atomic_read(&current->fs->count) > 1)
	if (current->fs && current->fs->users > 1)
		sbytes += kobjsize(current->fs);
	else
		bytes += kobjsize(current->fs);

	if (current->files && atomic_read(&current->files->count) > 1)
		sbytes += kobjsize(current->files);
	else
		bytes += kobjsize(current->files);

	if (current->sighand && atomic_read(&current->sighand->count) > 1)
		sbytes += kobjsize(current->sighand);
	else
		bytes += kobjsize(current->sighand);

	bytes += kobjsize(current); /* includes kernel stack */

	seq_printf(m,
		"Mem:\t%8lu bytes\n"
		"Slack:\t%8lu bytes\n"
		"Shared:\t%8lu bytes\n",
		bytes, slack, sbytes);

	up_read(&mm->mmap_sem);
}

unsigned long task_vsize(struct mm_struct *mm)
{
	struct vm_list_struct *tbp;
	unsigned long vsize = 0;

	down_read(&mm->mmap_sem);
	for (tbp = mm->context.vmlist; tbp; tbp = tbp->next) {
		if (tbp->vma)
			vsize += kobjsize((void *) tbp->vma->vm_start);
	struct vm_area_struct *vma;
	struct rb_node *p;
	unsigned long vsize = 0;

	down_read(&mm->mmap_sem);
	for (p = rb_first(&mm->mm_rb); p; p = rb_next(p)) {
		vma = rb_entry(p, struct vm_area_struct, vm_rb);
		vsize += vma->vm_end - vma->vm_start;
	}
	up_read(&mm->mmap_sem);
	return vsize;
}

int task_statm(struct mm_struct *mm, int *shared, int *text,
	       int *data, int *resident)
{
	struct vm_list_struct *tbp;
	int size = kobjsize(mm);

	down_read(&mm->mmap_sem);
	for (tbp = mm->context.vmlist; tbp; tbp = tbp->next) {
		size += kobjsize(tbp);
		if (tbp->vma) {
			size += kobjsize(tbp->vma);
			size += kobjsize((void *) tbp->vma->vm_start);
		}
	}

	size += (*text = mm->end_code - mm->start_code);
	size += (*data = mm->start_stack - mm->start_data);
	up_read(&mm->mmap_sem);
unsigned long task_statm(struct mm_struct *mm,
			 unsigned long *shared, unsigned long *text,
			 unsigned long *data, unsigned long *resident)
{
	struct vm_area_struct *vma;
	struct vm_region *region;
	struct rb_node *p;
	unsigned long size = kobjsize(mm);

	down_read(&mm->mmap_sem);
	for (p = rb_first(&mm->mm_rb); p; p = rb_next(p)) {
		vma = rb_entry(p, struct vm_area_struct, vm_rb);
		size += kobjsize(vma);
		region = vma->vm_region;
		if (region) {
			size += kobjsize(region);
			size += region->vm_end - region->vm_start;
		}
	}

	*text = (PAGE_ALIGN(mm->end_code) - (mm->start_code & PAGE_MASK))
		>> PAGE_SHIFT;
	*data = (PAGE_ALIGN(mm->start_stack) - (mm->start_data & PAGE_MASK))
		>> PAGE_SHIFT;
	up_read(&mm->mmap_sem);
	size >>= PAGE_SHIFT;
	size += *text + *data;
	*resident = size;
	return size;
}

/*
 * display mapping lines for a particular process's /proc/pid/maps
 */
static int show_map(struct seq_file *m, void *_vml)
{
	struct vm_list_struct *vml = _vml;
	struct proc_maps_private *priv = m->private;
	struct task_struct *task = priv->task;

	if (maps_protect && !ptrace_may_access(task, PTRACE_MODE_READ))
		return -EACCES;

	return nommu_vma_show(m, vml->vma);
static pid_t pid_of_stack(struct proc_maps_private *priv,
				struct vm_area_struct *vma, bool is_pid)
static int is_stack(struct proc_maps_private *priv,
		    struct vm_area_struct *vma)
{
	struct mm_struct *mm = vma->vm_mm;

	/*
	 * We make no effort to guess what a given thread considers to be
	 * its "stack".  It's not even well-defined for programs written
	 * languages like Go.
	 */
	return vma->vm_start <= mm->start_stack &&
		vma->vm_end >= mm->start_stack;
}

/*
 * display a single VMA to a sequenced file
 */
static int nommu_vma_show(struct seq_file *m, struct vm_area_struct *vma,
			  int is_pid)
{
	struct mm_struct *mm = vma->vm_mm;
	struct proc_maps_private *priv = m->private;
	unsigned long ino = 0;
	struct file *file;
	dev_t dev = 0;
	int flags;
	unsigned long long pgoff = 0;

	flags = vma->vm_flags;
	file = vma->vm_file;

	if (file) {
		struct inode *inode = file_inode(vma->vm_file);
		dev = inode->i_sb->s_dev;
		ino = inode->i_ino;
		pgoff = (loff_t)vma->vm_pgoff << PAGE_SHIFT;
	}

	seq_setwidth(m, 25 + sizeof(void *) * 6 - 1);
	seq_printf(m,
		   "%08lx-%08lx %c%c%c%c %08llx %02x:%02x %lu ",
		   vma->vm_start,
		   vma->vm_end,
		   flags & VM_READ ? 'r' : '-',
		   flags & VM_WRITE ? 'w' : '-',
		   flags & VM_EXEC ? 'x' : '-',
		   flags & VM_MAYSHARE ? flags & VM_SHARED ? 'S' : 's' : 'p',
		   pgoff,
		   MAJOR(dev), MINOR(dev), ino);

	if (file) {
		seq_pad(m, ' ');
		seq_file_path(m, file, "");
	} else if (mm && is_stack(priv, vma)) {
		seq_pad(m, ' ');
		seq_printf(m, "[stack]");
	}

	seq_putc(m, '\n');
	return 0;
}

/*
 * display mapping lines for a particular process's /proc/pid/maps
 */
static int show_map(struct seq_file *m, void *_p, int is_pid)
{
	struct rb_node *p = _p;

	return nommu_vma_show(m, rb_entry(p, struct vm_area_struct, vm_rb),
			      is_pid);
}

static int show_pid_map(struct seq_file *m, void *_p)
{
	return show_map(m, _p, 1);
}

static int show_tid_map(struct seq_file *m, void *_p)
{
	return show_map(m, _p, 0);
}

static void *m_start(struct seq_file *m, loff_t *pos)
{
	struct proc_maps_private *priv = m->private;
	struct vm_list_struct *vml;
	struct mm_struct *mm;
	loff_t n = *pos;

	/* pin the task and mm whilst we play with them */
	priv->task = get_pid_task(priv->pid, PIDTYPE_PID);
	if (!priv->task)
		return NULL;

	mm = mm_for_maps(priv->task);
	if (!mm) {
		put_task_struct(priv->task);
		priv->task = NULL;
		return NULL;
	}

	/* start from the Nth VMA */
	for (vml = mm->context.vmlist; vml; vml = vml->next)
		if (n-- == 0)
			return vml;
	struct mm_struct *mm;
	struct rb_node *p;
	loff_t n = *pos;

	/* pin the task and mm whilst we play with them */
	priv->task = get_proc_task(priv->inode);
	if (!priv->task)
		return ERR_PTR(-ESRCH);

	mm = priv->mm;
	if (!mm || !atomic_inc_not_zero(&mm->mm_users))
		return NULL;

	down_read(&mm->mmap_sem);
	/* start from the Nth VMA */
	for (p = rb_first(&mm->mm_rb); p; p = rb_next(p))
		if (n-- == 0)
			return p;

	up_read(&mm->mmap_sem);
	mmput(mm);
	return NULL;
}

static void m_stop(struct seq_file *m, void *_vml)
{
	struct proc_maps_private *priv = m->private;

	if (priv->task) {
		struct mm_struct *mm = priv->task->mm;
		up_read(&mm->mmap_sem);
		mmput(mm);
		put_task_struct(priv->task);
	}
}

static void *m_next(struct seq_file *m, void *_vml, loff_t *pos)
{
	struct vm_list_struct *vml = _vml;

	(*pos)++;
	return vml ? vml->next : NULL;
	if (!IS_ERR_OR_NULL(_vml)) {
		up_read(&priv->mm->mmap_sem);
		mmput(priv->mm);
	}
	if (priv->task) {
		put_task_struct(priv->task);
		priv->task = NULL;
	}
}

static void *m_next(struct seq_file *m, void *_p, loff_t *pos)
{
	struct rb_node *p = _p;

	(*pos)++;
	return p ? rb_next(p) : NULL;
}

static const struct seq_operations proc_pid_maps_ops = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= show_map
};

static int maps_open(struct inode *inode, struct file *file)
{
	struct proc_maps_private *priv;
	int ret = -ENOMEM;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (priv) {
		priv->pid = proc_pid(inode);
		ret = seq_open(file, &proc_pid_maps_ops);
		if (!ret) {
			struct seq_file *m = file->private_data;
			m->private = priv;
		} else {
			kfree(priv);
		}
	}
	return ret;
}

const struct file_operations proc_maps_operations = {
	.open		= maps_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release_private,
	.show	= show_pid_map
};

static const struct seq_operations proc_tid_maps_ops = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= show_tid_map
};

static int maps_open(struct inode *inode, struct file *file,
		     const struct seq_operations *ops)
{
	struct proc_maps_private *priv;

	priv = __seq_open_private(file, ops, sizeof(*priv));
	if (!priv)
		return -ENOMEM;

	priv->inode = inode;
	priv->mm = proc_mem_open(inode, PTRACE_MODE_READ);
	if (IS_ERR(priv->mm)) {
		int err = PTR_ERR(priv->mm);

		seq_release_private(inode, file);
		return err;
	}

	return 0;
}


static int map_release(struct inode *inode, struct file *file)
{
	struct seq_file *seq = file->private_data;
	struct proc_maps_private *priv = seq->private;

	if (priv->mm)
		mmdrop(priv->mm);

	return seq_release_private(inode, file);
}

static int pid_maps_open(struct inode *inode, struct file *file)
{
	return maps_open(inode, file, &proc_pid_maps_ops);
}

static int tid_maps_open(struct inode *inode, struct file *file)
{
	return maps_open(inode, file, &proc_tid_maps_ops);
}

const struct file_operations proc_pid_maps_operations = {
	.open		= pid_maps_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= map_release,
};

const struct file_operations proc_tid_maps_operations = {
	.open		= tid_maps_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= map_release,
};

