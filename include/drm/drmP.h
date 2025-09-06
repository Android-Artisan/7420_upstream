/**
 * \file drmP.h
 * Private header for Direct Rendering Manager
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All rights reserved.
 *
/*
 * Internal Header for the Direct Rendering Manager
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * Copyright (c) 2009-2010, Code Aurora Forum.
 * All rights reserved.
 *
 * Author: Rickard E. (Rik) Faith <faith@valinux.com>
 * Author: Gareth Hughes <gareth@valinux.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _DRM_P_H_
#define _DRM_P_H_

/* If you want the memory alloc debug functionality, change define below */
/* #define DEBUG_MEMORY */

#ifdef __KERNEL__
#ifdef __alpha__
/* add include of current.h so that "current" is defined
 * before static inline funcs in wait.h. Doing this so we
 * can build the DRM (part of PI DRI). 4/21/2000 S + B */
#include <asm/current.h>
#endif				/* __alpha__ */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/file.h>
#include <linux/pci.h>
#include <linux/jiffies.h>
#include <linux/smp_lock.h>	/* For (un)lock_kernel */
#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <linux/cdev.h>
#include <linux/mutex.h>
#if defined(__alpha__) || defined(__powerpc__)
#include <asm/pgtable.h>	/* For pte_wrprotect */
#endif
#include <asm/io.h>
#include <asm/mman.h>
#include <asm/uaccess.h>
#ifdef CONFIG_MTRR
#include <asm/mtrr.h>
#endif
#if defined(CONFIG_AGP) || defined(CONFIG_AGP_MODULE)
#include <linux/types.h>
#include <linux/agp_backend.h>
#endif
#include <linux/workqueue.h>
#include <linux/poll.h>
#include <asm/pgalloc.h>
#include "drm.h"

#include <linux/idr.h>

#define __OS_HAS_AGP (defined(CONFIG_AGP) || (defined(CONFIG_AGP_MODULE) && defined(MODULE)))
#define __OS_HAS_MTRR (defined(CONFIG_MTRR))

struct drm_file;
struct drm_device;

#include "drm_os_linux.h"
#include "drm_hashtab.h"
#include <linux/agp_backend.h>
#include <linux/cdev.h>
#include <linux/dma-mapping.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/highmem.h>
#include <linux/idr.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/ratelimit.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>

#include <asm/mman.h>
#include <asm/pgalloc.h>
#include <asm/uaccess.h>

#include <uapi/drm/drm.h>
#include <uapi/drm/drm_mode.h>

#include <drm/drm_agpsupport.h>
#include <drm/drm_crtc.h>
#include <drm/drm_global.h>
#include <drm/drm_hashtab.h>
#include <drm/drm_mem_util.h>
#include <drm/drm_mm.h>
#include <drm/drm_os_linux.h>
#include <drm/drm_sarea.h>
#include <drm/drm_vma_manager.h>

struct module;

struct drm_file;
struct drm_device;
struct drm_agp_head;
struct drm_local_map;
struct drm_device_dma;
struct drm_dma_handle;
struct drm_gem_object;

struct device_node;
struct videomode;
struct reservation_object;
struct dma_buf_attachment;

/*
 * 4 debug categories are defined:
 *
 * CORE: Used in the generic drm code: drm_ioctl.c, drm_mm.c, drm_memory.c, ...
 *	 This is the category used by the DRM_DEBUG() macro.
 *
 * DRIVER: Used in the vendor specific part of the driver: i915, radeon, ...
 *	   This is the category used by the DRM_DEBUG_DRIVER() macro.
 *
 * KMS: used in the modesetting code.
 *	This is the category used by the DRM_DEBUG_KMS() macro.
 *
 * PRIME: used in the prime code.
 *	  This is the category used by the DRM_DEBUG_PRIME() macro.
 *
 * ATOMIC: used in the atomic code.
 *	  This is the category used by the DRM_DEBUG_ATOMIC() macro.
 *
 * VBL: used for verbose debug message in the vblank code
 *	  This is the category used by the DRM_DEBUG_VBL() macro.
 *
 * Enabling verbose debug messages is done through the drm.debug parameter,
 * each category being enabled by a bit.
 *
 * drm.debug=0x1 will enable CORE messages
 * drm.debug=0x2 will enable DRIVER messages
 * drm.debug=0x3 will enable CORE and DRIVER messages
 * ...
 * drm.debug=0x3f will enable all messages
 *
 * An interesting feature is that it's possible to enable verbose logging at
 * run-time by echoing the debug value in its sysfs node:
 *   # echo 0xf > /sys/module/drm/parameters/debug
 */
#define DRM_UT_CORE 		0x01
#define DRM_UT_DRIVER		0x02
#define DRM_UT_KMS		0x04
#define DRM_UT_PRIME		0x08
#define DRM_UT_ATOMIC		0x10
#define DRM_UT_VBL		0x20

extern __printf(2, 3)
void drm_ut_debug_printk(const char *function_name,
			 const char *format, ...);
extern __printf(1, 2)
void drm_err(const char *format, ...);

/***********************************************************************/
/** \name DRM template customization defaults */
/*@{*/

/* driver capabilities and requirements mask */
#define DRIVER_USE_AGP     0x1
#define DRIVER_REQUIRE_AGP 0x2
#define DRIVER_USE_MTRR    0x4
#define DRIVER_PCI_DMA     0x8
#define DRIVER_SG          0x10
#define DRIVER_HAVE_DMA    0x20
#define DRIVER_HAVE_IRQ    0x40
#define DRIVER_IRQ_SHARED  0x80
#define DRIVER_IRQ_VBL     0x100
#define DRIVER_DMA_QUEUE   0x200
#define DRIVER_FB_DMA      0x400
#define DRIVER_IRQ_VBL2    0x800

/***********************************************************************/
/** \name Begin the DRM... */
/*@{*/

#define DRM_DEBUG_CODE 2	  /**< Include debugging code if > 1, then
				     also include looping detection. */

#define DRM_MAGIC_HASH_ORDER  4  /**< Size of key hash table. Must be power of 2. */
#define DRM_KERNEL_CONTEXT    0	 /**< Change drm_resctx if changed */
#define DRM_RESERVED_CONTEXTS 1	 /**< Change drm_resctx if changed */
#define DRM_LOOPING_LIMIT     5000000
#define DRM_TIME_SLICE	      (HZ/20)  /**< Time slice for GLXContexts */
#define DRM_LOCK_SLICE	      1	/**< Time slice for lock, in jiffies */

#define DRM_FLAG_DEBUG	  0x01

#define DRM_MEM_DMA	   0
#define DRM_MEM_SAREA	   1
#define DRM_MEM_DRIVER	   2
#define DRM_MEM_MAGIC	   3
#define DRM_MEM_IOCTLS	   4
#define DRM_MEM_MAPS	   5
#define DRM_MEM_VMAS	   6
#define DRM_MEM_BUFS	   7
#define DRM_MEM_SEGS	   8
#define DRM_MEM_PAGES	   9
#define DRM_MEM_FILES	  10
#define DRM_MEM_QUEUES	  11
#define DRM_MEM_CMDS	  12
#define DRM_MEM_MAPPINGS  13
#define DRM_MEM_BUFLISTS  14
#define DRM_MEM_AGPLISTS  15
#define DRM_MEM_TOTALAGP  16
#define DRM_MEM_BOUNDAGP  17
#define DRM_MEM_CTXBITMAP 18
#define DRM_MEM_STUB      19
#define DRM_MEM_SGLISTS   20
#define DRM_MEM_CTXLIST   21
#define DRM_MEM_MM        22
#define DRM_MEM_HASHTAB   23

#define DRM_MAX_CTXBITMAP (PAGE_SIZE * 8)
#define DRM_MAP_HASH_OFFSET 0x10000000

/*@}*/
#define DRIVER_USE_AGP			0x1
#define DRIVER_PCI_DMA			0x8
#define DRIVER_SG			0x10
#define DRIVER_HAVE_DMA			0x20
#define DRIVER_HAVE_IRQ			0x40
#define DRIVER_IRQ_SHARED		0x80
#define DRIVER_GEM			0x1000
#define DRIVER_MODESET			0x2000
#define DRIVER_PRIME			0x4000
#define DRIVER_RENDER			0x8000
#define DRIVER_ATOMIC			0x10000
#define DRIVER_KMS_LEGACY_CONTEXT	0x20000

/***********************************************************************/
/** \name Macros to make printk easier */
/*@{*/

#define _DRM_PRINTK(once, level, fmt, ...)				\
	do {								\
		printk##once(KERN_##level "[" DRM_NAME "] " fmt,	\
			     ##__VA_ARGS__);				\
	} while (0)

#define DRM_INFO(fmt, ...)						\
	_DRM_PRINTK(, INFO, fmt, ##__VA_ARGS__)
#define DRM_NOTE(fmt, ...)						\
	_DRM_PRINTK(, NOTICE, fmt, ##__VA_ARGS__)
#define DRM_WARN(fmt, ...)						\
	_DRM_PRINTK(, WARNING, fmt, ##__VA_ARGS__)

#define DRM_INFO_ONCE(fmt, ...)						\
	_DRM_PRINTK(_once, INFO, fmt, ##__VA_ARGS__)
#define DRM_NOTE_ONCE(fmt, ...)						\
	_DRM_PRINTK(_once, NOTICE, fmt, ##__VA_ARGS__)
#define DRM_WARN_ONCE(fmt, ...)						\
	_DRM_PRINTK(_once, WARNING, fmt, ##__VA_ARGS__)

/**
 * Error output.
 *
 * \param fmt printf() like format string.
 * \param arg arguments
 */
#define DRM_ERROR(fmt, arg...) \
	printk(KERN_ERR "[" DRM_NAME ":%s] *ERROR* " fmt , __func__ , ##arg)

/**
 * Memory error output.
 *
 * \param area memory area where the error occurred.
 * \param fmt printf() like format string.
 * \param arg arguments
 */
#define DRM_MEM_ERROR(area, fmt, arg...) \
	printk(KERN_ERR "[" DRM_NAME ":%s:%s] *ERROR* " fmt , __func__, \
	       drm_mem_stats[area].name , ##arg)

#define DRM_INFO(fmt, arg...)  printk(KERN_INFO "[" DRM_NAME "] " fmt , ##arg)
#define DRM_ERROR(fmt, ...)				\
	drm_err(fmt, ##__VA_ARGS__)

/**
 * Rate limited error output.  Like DRM_ERROR() but won't flood the log.
 *
 * \param fmt printf() like format string.
 * \param arg arguments
 */
#define DRM_ERROR_RATELIMITED(fmt, ...)				\
({									\
	static DEFINE_RATELIMIT_STATE(_rs,				\
				      DEFAULT_RATELIMIT_INTERVAL,	\
				      DEFAULT_RATELIMIT_BURST);		\
									\
	if (__ratelimit(&_rs))						\
		drm_err(fmt, ##__VA_ARGS__);				\
})

/**
 * Debug output.
 *
 * \param fmt printf() like format string.
 * \param arg arguments
 */
#if DRM_DEBUG_CODE
#define DRM_DEBUG(fmt, arg...)						\
	do {								\
		if ( drm_debug )			\
			printk(KERN_DEBUG				\
			       "[" DRM_NAME ":%s] " fmt ,	\
			       __func__ , ##arg);			\
	} while (0)
#else
#define DRM_DEBUG(fmt, arg...)		 do { } while (0)
#endif

#define DRM_PROC_LIMIT (PAGE_SIZE-80)

#define DRM_PROC_PRINT(fmt, arg...)					\
   len += sprintf(&buf[len], fmt , ##arg);				\
   if (len > DRM_PROC_LIMIT) { *eof = 1; return len - offset; }

#define DRM_PROC_PRINT_RET(ret, fmt, arg...)				\
   len += sprintf(&buf[len], fmt , ##arg);				\
   if (len > DRM_PROC_LIMIT) { ret; *eof = 1; return len - offset; }
#define DRM_DEBUG(fmt, args...)						\
	do {								\
		if (unlikely(drm_debug & DRM_UT_CORE))			\
			drm_ut_debug_printk(__func__, fmt, ##args);	\
	} while (0)

#define DRM_DEBUG_DRIVER(fmt, args...)					\
	do {								\
		if (unlikely(drm_debug & DRM_UT_DRIVER))		\
			drm_ut_debug_printk(__func__, fmt, ##args);	\
	} while (0)
#define DRM_DEBUG_KMS(fmt, args...)					\
	do {								\
		if (unlikely(drm_debug & DRM_UT_KMS))			\
			drm_ut_debug_printk(__func__, fmt, ##args);	\
	} while (0)
#define DRM_DEBUG_PRIME(fmt, args...)					\
	do {								\
		if (unlikely(drm_debug & DRM_UT_PRIME))			\
			drm_ut_debug_printk(__func__, fmt, ##args);	\
	} while (0)
#define DRM_DEBUG_ATOMIC(fmt, args...)					\
	do {								\
		if (unlikely(drm_debug & DRM_UT_ATOMIC))		\
			drm_ut_debug_printk(__func__, fmt, ##args);	\
	} while (0)
#define DRM_DEBUG_VBL(fmt, args...)					\
	do {								\
		if (unlikely(drm_debug & DRM_UT_VBL))			\
			drm_ut_debug_printk(__func__, fmt, ##args);	\
	} while (0)

/*@}*/

/***********************************************************************/
/** \name Internal types and structures */
/*@{*/

#define DRM_ARRAY_SIZE(x) ARRAY_SIZE(x)

#define DRM_LEFTCOUNT(x) (((x)->rp + (x)->count - (x)->wp) % ((x)->count + 1))
#define DRM_BUFCOUNT(x) ((x)->count - DRM_LEFTCOUNT(x))
#define DRM_WAITCOUNT(dev,idx) DRM_BUFCOUNT(&dev->queuelist[idx]->waitlist)

#define DRM_IF_VERSION(maj, min) (maj << 16 | min)
/**
 * Get the private SAREA mapping.
 *
 * \param _dev DRM device.
 * \param _ctx context number.
 * \param _map output mapping.
 */
#define DRM_GET_PRIV_SAREA(_dev, _ctx, _map) do {	\
	(_map) = (_dev)->context_sareas[_ctx];		\
} while(0)

/**
 * Test that the hardware lock is held by the caller, returning otherwise.
 *
 * \param dev DRM device.
 * \param filp file pointer of the caller.
 */
#define LOCK_TEST_WITH_RETURN( dev, file_priv )				\
do {									\
	if ( !_DRM_LOCK_IS_HELD( dev->lock.hw_lock->lock ) ||		\
	     dev->lock.file_priv != file_priv )	{			\
		DRM_ERROR( "%s called without lock held, held  %d owner %p %p\n",\
			   __func__, _DRM_LOCK_IS_HELD( dev->lock.hw_lock->lock ),\
			   dev->lock.file_priv, file_priv );		\
		return -EINVAL;						\
	}								\
} while (0)

/**
 * Copy and IOCTL return string to user space
 */
#define DRM_COPY( name, value )						\
	len = strlen( value );						\
	if ( len > name##_len ) len = name##_len;			\
	name##_len = strlen( value );					\
	if ( len && name ) {						\
		if ( copy_to_user( name, value, len ) )			\
			return -EFAULT;					\
	}
#define DRM_IF_VERSION(maj, min) (maj << 16 | min)

/**
 * Ioctl function type.
 *
 * \param inode device inode.
 * \param file_priv DRM file private pointer.
 * \param cmd command.
 * \param arg argument.
 */
typedef int drm_ioctl_t(struct drm_device *dev, void *data,
			struct drm_file *file_priv);

typedef int drm_ioctl_compat_t(struct file *filp, unsigned int cmd,
			       unsigned long arg);

#define DRM_AUTH	0x1
#define	DRM_MASTER	0x2
#define DRM_ROOT_ONLY	0x4

struct drm_ioctl_desc {
	unsigned int cmd;
	drm_ioctl_t *func;
	int flags;
#define DRM_IOCTL_NR(n)                _IOC_NR(n)
#define DRM_MAJOR       226

#define DRM_AUTH	0x1
#define	DRM_MASTER	0x2
#define DRM_ROOT_ONLY	0x4
#define DRM_CONTROL_ALLOW 0x8
#define DRM_UNLOCKED	0x10
#define DRM_RENDER_ALLOW 0x20

struct drm_ioctl_desc {
	unsigned int cmd;
	int flags;
	drm_ioctl_t *func;
	const char *name;
};

/**
 * Creates a driver or general drm_ioctl_desc array entry for the given
 * ioctl, for use by drm_ioctl().
 */
#define DRM_IOCTL_DEF(ioctl, func, flags) \
	[DRM_IOCTL_NR(ioctl)] = {ioctl, func, flags}

struct drm_magic_entry {
	struct list_head head;
	struct drm_hash_item hash_item;
	struct drm_file *priv;
};

struct drm_vma_entry {
	struct list_head head;
	struct vm_area_struct *vma;
	pid_t pid;
};

/**
 * DMA buffer.
 */
struct drm_buf {
	int idx;		       /**< Index into master buflist */
	int total;		       /**< Buffer size */
	int order;		       /**< log-base-2(total) */
	int used;		       /**< Amount of buffer in use (for DMA) */
	unsigned long offset;	       /**< Byte offset (used internally) */
	void *address;		       /**< Address of buffer */
	unsigned long bus_address;     /**< Bus address of buffer */
	struct drm_buf *next;	       /**< Kernel-only: used for free list */
	__volatile__ int waiting;      /**< On kernel DMA queue */
	__volatile__ int pending;      /**< On hardware DMA queue */
	wait_queue_head_t dma_wait;    /**< Processes waiting */
	struct drm_file *file_priv;    /**< Private of holding file descr */
	int context;		       /**< Kernel queue for this buffer */
	int while_locked;	       /**< Dispatch this buffer while locked */
	enum {
		DRM_LIST_NONE = 0,
		DRM_LIST_FREE = 1,
		DRM_LIST_WAIT = 2,
		DRM_LIST_PEND = 3,
		DRM_LIST_PRIO = 4,
		DRM_LIST_RECLAIM = 5
	} list;			       /**< Which list we're on */

	int dev_priv_size;		 /**< Size of buffer private storage */
	void *dev_private;		 /**< Per-buffer private storage */
};

/** bufs is one longer than it has to be */
struct drm_waitlist {
	int count;			/**< Number of possible buffers */
	struct drm_buf **bufs;		/**< List of pointers to buffers */
	struct drm_buf **rp;			/**< Read pointer */
	struct drm_buf **wp;			/**< Write pointer */
	struct drm_buf **end;		/**< End pointer */
	spinlock_t read_lock;
	spinlock_t write_lock;
};

struct drm_freelist {
	int initialized;	       /**< Freelist in use */
	atomic_t count;		       /**< Number of free buffers */
	struct drm_buf *next;	       /**< End pointer */

	wait_queue_head_t waiting;     /**< Processes waiting on free bufs */
	int low_mark;		       /**< Low water mark */
	int high_mark;		       /**< High water mark */
	atomic_t wfh;		       /**< If waiting for high mark */
	spinlock_t lock;
};

typedef struct drm_dma_handle {
	dma_addr_t busaddr;
	void *vaddr;
	size_t size;
} drm_dma_handle_t;

/**
 * Buffer entry.  There is one of this for each buffer size order.
 */
struct drm_buf_entry {
	int buf_size;			/**< size */
	int buf_count;			/**< number of buffers */
	struct drm_buf *buflist;		/**< buffer list */
	int seg_count;
	int page_order;
	struct drm_dma_handle **seglist;

	struct drm_freelist freelist;

#define DRM_IOCTL_DEF_DRV(ioctl, _func, _flags)				\
	[DRM_IOCTL_NR(DRM_IOCTL_##ioctl) - DRM_COMMAND_BASE] = {	\
		.cmd = DRM_IOCTL_##ioctl,				\
		.func = _func,						\
		.flags = _flags,					\
		.name = #ioctl						\
	 }

/* Event queued up for userspace to read */
struct drm_pending_event {
	struct drm_event *event;
	struct list_head link;
	struct drm_file *file_priv;
	pid_t pid; /* pid of requester, no guarantee it's valid by the time
		      we deliver the event, for tracing only */
	void (*destroy)(struct drm_pending_event *event);
};

/* initial implementaton using a linked list - todo hashtab */
struct drm_prime_file_private {
	struct list_head head;
	struct mutex lock;
};

/** File private data */
struct drm_file {
	int authenticated;
	int master;
	pid_t pid;
	uid_t uid;
	drm_magic_t magic;
	unsigned long ioctl_count;
	struct list_head lhead;
	struct drm_minor *minor;
	int remove_auth_on_close;
	unsigned long lock_count;
	struct file *filp;
	void *driver_priv;
};

/** Wait queue */
struct drm_queue {
	atomic_t use_count;		/**< Outstanding uses (+1) */
	atomic_t finalization;		/**< Finalization in progress */
	atomic_t block_count;		/**< Count of processes waiting */
	atomic_t block_read;		/**< Queue blocked for reads */
	wait_queue_head_t read_queue;	/**< Processes waiting on block_read */
	atomic_t block_write;		/**< Queue blocked for writes */
	wait_queue_head_t write_queue;	/**< Processes waiting on block_write */
	atomic_t total_queued;		/**< Total queued statistic */
	atomic_t total_flushed;		/**< Total flushes statistic */
	atomic_t total_locks;		/**< Total locks statistics */
	enum drm_ctx_flags flags;	/**< Context preserving and 2D-only */
	struct drm_waitlist waitlist;	/**< Pending buffers */
	wait_queue_head_t flush_queue;	/**< Processes waiting until flush */
	unsigned authenticated :1;
	/* Whether we're master for a minor. Protected by master_mutex */
	unsigned is_master :1;
	/* true when the client has asked us to expose stereo 3D mode flags */
	unsigned stereo_allowed :1;
	/*
	 * true if client understands CRTC primary planes and cursor planes
	 * in the plane list
	 */
	unsigned universal_planes:1;
	/* true if client understands atomic properties */
	unsigned atomic:1;
	/*
	 * This client is allowed to gain master privileges for @master.
	 * Protected by struct drm_device::master_mutex.
	 */
	unsigned allowed_master:1;

	struct pid *pid;
	kuid_t uid;
	drm_magic_t magic;
	struct list_head lhead;
	struct drm_minor *minor;
	unsigned long lock_count;

	/** Mapping of mm object handles to object pointers. */
	struct idr object_idr;
	/** Lock for synchronization of access to object_idr. */
	spinlock_t table_lock;

	struct file *filp;
	void *driver_priv;

	struct drm_master *master; /* master this node is currently associated with
				      N.B. not always minor->master */
	/**
	 * fbs - List of framebuffers associated with this file.
	 *
	 * Protected by fbs_lock. Note that the fbs list holds a reference on
	 * the fb object to prevent it from untimely disappearing.
	 */
	struct list_head fbs;
	struct mutex fbs_lock;

	/** User-created blob properties; this retains a reference on the
	 *  property. */
	struct list_head blobs;

	wait_queue_head_t event_wait;
	struct list_head event_list;
	int event_space;

	struct drm_prime_file_private prime;
};

/**
 * Lock data.
 */
struct drm_lock_data {
	struct drm_hw_lock *hw_lock;	/**< Hardware lock */
	/** Private of lock holder's file (NULL=kernel) */
	struct drm_file *file_priv;
	wait_queue_head_t lock_queue;	/**< Queue of blocked processes */
	unsigned long lock_time;	/**< Time of last lock in jiffies */
	spinlock_t spinlock;
	uint32_t kernel_waiters;
	uint32_t user_waiters;
	int idle_has_lock;
};

/**
 * DMA data.
 */
struct drm_device_dma {

	struct drm_buf_entry bufs[DRM_MAX_ORDER + 1];	/**< buffers, grouped by their size order */
	int buf_count;			/**< total number of buffers */
	struct drm_buf **buflist;		/**< Vector of pointers into drm_device_dma::bufs */
	int seg_count;
	int page_count;			/**< number of pages */
	unsigned long *pagelist;	/**< page list */
	unsigned long byte_count;
	enum {
		_DRM_DMA_USE_AGP = 0x01,
		_DRM_DMA_USE_SG = 0x02,
		_DRM_DMA_USE_FB = 0x04,
		_DRM_DMA_USE_PCI_RO = 0x08
	} flags;

};

/**
 * AGP memory entry.  Stored as a doubly linked list.
 */
struct drm_agp_mem {
	unsigned long handle;		/**< handle */
	DRM_AGP_MEM *memory;
	unsigned long bound;		/**< address */
	int pages;
	struct list_head head;
};

/**
 * AGP data.
 *
 * \sa drm_agp_init() and drm_device::agp.
 */
struct drm_agp_head {
	DRM_AGP_KERN agp_info;		/**< AGP device information */
	struct list_head memory;
	unsigned long mode;		/**< AGP mode */
	struct agp_bridge_data *bridge;
	int enabled;			/**< whether the AGP bus as been enabled */
	int acquired;			/**< whether the AGP device has been acquired */
	unsigned long base;
	int agp_mtrr;
	int cant_use_aperture;
	unsigned long page_mask;
};

/**
 * Scatter-gather memory.
 */
struct drm_sg_mem {
	unsigned long handle;
	void *virtual;
	int pages;
	struct page **pagelist;
	dma_addr_t *busaddr;
};

struct drm_sigdata {
	int context;
	struct drm_hw_lock *lock;
};


/*
 * Generic memory manager structs
 */

struct drm_mm_node {
	struct list_head fl_entry;
	struct list_head ml_entry;
	int free;
	unsigned long start;
	unsigned long size;
	struct drm_mm *mm;
	void *private;
};

struct drm_mm {
	struct list_head fl_entry;
	struct list_head ml_entry;
};


/**
 * Mappings list
 */
struct drm_map_list {
	struct list_head head;		/**< list head */
	struct drm_hash_item hash;
	struct drm_map *map;			/**< mapping */
	uint64_t user_token;
};

typedef struct drm_map drm_local_map_t;

/**
 * Context handle list
 */
struct drm_ctx_list {
	struct list_head head;		/**< list head */
	drm_context_t handle;		/**< context handle */
	struct drm_file *tag;		/**< associated fd private data */
};

struct drm_vbl_sig {
	struct list_head head;
	unsigned int sequence;
	struct siginfo info;
	struct task_struct *task;
};

/* location of GART table */
#define DRM_ATI_GART_MAIN 1
#define DRM_ATI_GART_FB   2

#define DRM_ATI_GART_PCI 1
#define DRM_ATI_GART_PCIE 2
#define DRM_ATI_GART_IGP 3

struct drm_ati_pcigart_info {
	int gart_table_location;
	int gart_reg_if;
	void *addr;
	dma_addr_t bus_addr;
	dma_addr_t table_mask;
	struct drm_dma_handle *table_handle;
	drm_local_map_t mapping;
	int table_size;
};
 * struct drm_master - drm master structure
 *
 * @refcount: Refcount for this master object.
 * @minor: Link back to minor char device we are master for. Immutable.
 * @unique: Unique identifier: e.g. busid. Protected by drm_global_mutex.
 * @unique_len: Length of unique field. Protected by drm_global_mutex.
 * @magic_map: Map of used authentication tokens. Protected by struct_mutex.
 * @lock: DRI lock information.
 * @driver_priv: Pointer to driver-private information.
 */
struct drm_master {
	struct kref refcount;
	struct drm_minor *minor;
	char *unique;
	int unique_len;
	struct idr magic_map;
	struct drm_lock_data lock;
	void *driver_priv;
};

/* Size of ringbuffer for vblank timestamps. Just double-buffer
 * in initial implementation.
 */
#define DRM_VBLANKTIME_RBSIZE 2

/* Flags and return codes for get_vblank_timestamp() driver function. */
#define DRM_CALLED_FROM_VBLIRQ 1
#define DRM_VBLANKTIME_SCANOUTPOS_METHOD (1 << 0)
#define DRM_VBLANKTIME_IN_VBLANK         (1 << 1)

/* get_scanout_position() return flags */
#define DRM_SCANOUTPOS_VALID        (1 << 0)
#define DRM_SCANOUTPOS_IN_VBLANK    (1 << 1)
#define DRM_SCANOUTPOS_ACCURATE     (1 << 2)

/**
 * DRM driver structure. This structure represent the common code for
 * a family of cards. There will one drm_device for each card present
 * in this family
 */
struct drm_driver {
	int (*load) (struct drm_device *, unsigned long flags);
	int (*firstopen) (struct drm_device *);
	int (*open) (struct drm_device *, struct drm_file *);
	void (*preclose) (struct drm_device *, struct drm_file *file_priv);
	void (*postclose) (struct drm_device *, struct drm_file *);
	void (*lastclose) (struct drm_device *);
	int (*unload) (struct drm_device *);
	int (*suspend) (struct drm_device *, pm_message_t state);
	int (*resume) (struct drm_device *);
	int (*dma_ioctl) (struct drm_device *dev, void *data, struct drm_file *file_priv);
	void (*dma_ready) (struct drm_device *);
	int (*dma_quiescent) (struct drm_device *);
	int (*context_ctor) (struct drm_device *dev, int context);
	int (*context_dtor) (struct drm_device *dev, int context);
	int (*kernel_context_switch) (struct drm_device *dev, int old,
				      int new);
	void (*kernel_context_switch_unlock) (struct drm_device *dev);
	int (*vblank_wait) (struct drm_device *dev, unsigned int *sequence);
	int (*vblank_wait2) (struct drm_device *dev, unsigned int *sequence);
	int (*dri_library_name) (struct drm_device *dev, char *buf);
	int (*dma_quiescent) (struct drm_device *);
	int (*context_dtor) (struct drm_device *dev, int context);
	int (*set_busid)(struct drm_device *dev, struct drm_master *master);

	/**
	 * get_vblank_counter - get raw hardware vblank counter
	 * @dev: DRM device
	 * @pipe: counter to fetch
	 *
	 * Driver callback for fetching a raw hardware vblank counter for @crtc.
	 * If a device doesn't have a hardware counter, the driver can simply
	 * return the value of drm_vblank_count. The DRM core will account for
	 * missed vblank events while interrupts where disabled based on system
	 * timestamps.
	 *
	 * Wraparound handling and loss of events due to modesetting is dealt
	 * with in the DRM core code.
	 *
	 * RETURNS
	 * Raw vblank counter value.
	 */
	u32 (*get_vblank_counter) (struct drm_device *dev, unsigned int pipe);

	/**
	 * enable_vblank - enable vblank interrupt events
	 * @dev: DRM device
	 * @pipe: which irq to enable
	 *
	 * Enable vblank interrupts for @crtc.  If the device doesn't have
	 * a hardware vblank counter, this routine should be a no-op, since
	 * interrupts will have to stay on to keep the count accurate.
	 *
	 * RETURNS
	 * Zero on success, appropriate errno if the given @crtc's vblank
	 * interrupt cannot be enabled.
	 */
	int (*enable_vblank) (struct drm_device *dev, unsigned int pipe);

	/**
	 * disable_vblank - disable vblank interrupt events
	 * @dev: DRM device
	 * @pipe: which irq to enable
	 *
	 * Disable vblank interrupts for @crtc.  If the device doesn't have
	 * a hardware vblank counter, this routine should be a no-op, since
	 * interrupts will have to stay on to keep the count accurate.
	 */
	void (*disable_vblank) (struct drm_device *dev, unsigned int pipe);

	/**
	 * Called by \c drm_device_is_agp.  Typically used to determine if a
	 * card is really attached to AGP or not.
	 *
	 * \param dev  DRM device handle
	 *
	 * \returns
	 * One of three values is returned depending on whether or not the
	 * card is absolutely \b not AGP (return of 0), absolutely \b is AGP
	 * (return of 1), or may or may not be AGP (return of 2).
	 */
	int (*device_is_agp) (struct drm_device *dev);

	/* these have to be filled in */

	irqreturn_t(*irq_handler) (DRM_IRQ_ARGS);
	void (*irq_preinstall) (struct drm_device *dev);
	void (*irq_postinstall) (struct drm_device *dev);
	void (*irq_uninstall) (struct drm_device *dev);
	void (*reclaim_buffers) (struct drm_device *dev,
				 struct drm_file * file_priv);
	void (*reclaim_buffers_locked) (struct drm_device *dev,
					struct drm_file *file_priv);
	void (*reclaim_buffers_idlelocked) (struct drm_device *dev,
					    struct drm_file *file_priv);
	unsigned long (*get_map_ofs) (struct drm_map * map);
	unsigned long (*get_reg_ofs) (struct drm_device *dev);
	void (*set_version) (struct drm_device *dev,
			     struct drm_set_version *sv);
	/**
	 * Called by vblank timestamping code.
	 *
	 * Return the current display scanout position from a crtc, and an
	 * optional accurate ktime_get timestamp of when position was measured.
	 *
	 * \param dev  DRM device.
	 * \param pipe Id of the crtc to query.
	 * \param flags Flags from the caller (DRM_CALLED_FROM_VBLIRQ or 0).
	 * \param *vpos Target location for current vertical scanout position.
	 * \param *hpos Target location for current horizontal scanout position.
	 * \param *stime Target location for timestamp taken immediately before
	 *               scanout position query. Can be NULL to skip timestamp.
	 * \param *etime Target location for timestamp taken immediately after
	 *               scanout position query. Can be NULL to skip timestamp.
	 * \param mode Current display timings.
	 *
	 * Returns vpos as a positive number while in active scanout area.
	 * Returns vpos as a negative number inside vblank, counting the number
	 * of scanlines to go until end of vblank, e.g., -1 means "one scanline
	 * until start of active scanout / end of vblank."
	 *
	 * \return Flags, or'ed together as follows:
	 *
	 * DRM_SCANOUTPOS_VALID = Query successful.
	 * DRM_SCANOUTPOS_INVBL = Inside vblank.
	 * DRM_SCANOUTPOS_ACCURATE = Returned position is accurate. A lack of
	 * this flag means that returned position may be offset by a constant
	 * but unknown small number of scanlines wrt. real scanout position.
	 *
	 */
	int (*get_scanout_position) (struct drm_device *dev, unsigned int pipe,
				     unsigned int flags, int *vpos, int *hpos,
				     ktime_t *stime, ktime_t *etime,
				     const struct drm_display_mode *mode);

	/**
	 * Called by \c drm_get_last_vbltimestamp. Should return a precise
	 * timestamp when the most recent VBLANK interval ended or will end.
	 *
	 * Specifically, the timestamp in @vblank_time should correspond as
	 * closely as possible to the time when the first video scanline of
	 * the video frame after the end of VBLANK will start scanning out,
	 * the time immediately after end of the VBLANK interval. If the
	 * @crtc is currently inside VBLANK, this will be a time in the future.
	 * If the @crtc is currently scanning out a frame, this will be the
	 * past start time of the current scanout. This is meant to adhere
	 * to the OpenML OML_sync_control extension specification.
	 *
	 * \param dev dev DRM device handle.
	 * \param pipe crtc for which timestamp should be returned.
	 * \param *max_error Maximum allowable timestamp error in nanoseconds.
	 *                   Implementation should strive to provide timestamp
	 *                   with an error of at most *max_error nanoseconds.
	 *                   Returns true upper bound on error for timestamp.
	 * \param *vblank_time Target location for returned vblank timestamp.
	 * \param flags 0 = Defaults, no special treatment needed.
	 * \param       DRM_CALLED_FROM_VBLIRQ = Function is called from vblank
	 *	        irq handler. Some drivers need to apply some workarounds
	 *              for gpu-specific vblank irq quirks if flag is set.
	 *
	 * \returns
	 * Zero if timestamping isn't supported in current display mode or a
	 * negative number on failure. A positive status code on success,
	 * which describes how the vblank_time timestamp was computed.
	 */
	int (*get_vblank_timestamp) (struct drm_device *dev, unsigned int pipe,
				     int *max_error,
				     struct timeval *vblank_time,
				     unsigned flags);

	/* these have to be filled in */

	irqreturn_t(*irq_handler) (int irq, void *arg);
	void (*irq_preinstall) (struct drm_device *dev);
	int (*irq_postinstall) (struct drm_device *dev);
	void (*irq_uninstall) (struct drm_device *dev);

	/* Master routines */
	int (*master_create)(struct drm_device *dev, struct drm_master *master);
	void (*master_destroy)(struct drm_device *dev, struct drm_master *master);
	/**
	 * master_set is called whenever the minor master is set.
	 * master_drop is called whenever the minor master is dropped.
	 */

	int (*master_set)(struct drm_device *dev, struct drm_file *file_priv,
			  bool from_open);
	void (*master_drop)(struct drm_device *dev, struct drm_file *file_priv,
			    bool from_release);

	int (*debugfs_init)(struct drm_minor *minor);
	void (*debugfs_cleanup)(struct drm_minor *minor);

	/**
	 * Driver-specific constructor for drm_gem_objects, to set up
	 * obj->driver_private.
	 *
	 * Returns 0 on success.
	 */
	void (*gem_free_object) (struct drm_gem_object *obj);
	int (*gem_open_object) (struct drm_gem_object *, struct drm_file *);
	void (*gem_close_object) (struct drm_gem_object *, struct drm_file *);

	/* prime: */
	/* export handle -> fd (see drm_gem_prime_handle_to_fd() helper) */
	int (*prime_handle_to_fd)(struct drm_device *dev, struct drm_file *file_priv,
				uint32_t handle, uint32_t flags, int *prime_fd);
	/* import fd -> handle (see drm_gem_prime_fd_to_handle() helper) */
	int (*prime_fd_to_handle)(struct drm_device *dev, struct drm_file *file_priv,
				int prime_fd, uint32_t *handle);
	/* export GEM -> dmabuf */
	struct dma_buf * (*gem_prime_export)(struct drm_device *dev,
				struct drm_gem_object *obj, int flags);
	/* import dmabuf -> GEM */
	struct drm_gem_object * (*gem_prime_import)(struct drm_device *dev,
				struct dma_buf *dma_buf);
	/* low-level interface used by drm_gem_prime_{import,export} */
	int (*gem_prime_pin)(struct drm_gem_object *obj);
	void (*gem_prime_unpin)(struct drm_gem_object *obj);
	struct reservation_object * (*gem_prime_res_obj)(
				struct drm_gem_object *obj);
	struct sg_table *(*gem_prime_get_sg_table)(struct drm_gem_object *obj);
	struct drm_gem_object *(*gem_prime_import_sg_table)(
				struct drm_device *dev,
				struct dma_buf_attachment *attach,
				struct sg_table *sgt);
	void *(*gem_prime_vmap)(struct drm_gem_object *obj);
	void (*gem_prime_vunmap)(struct drm_gem_object *obj, void *vaddr);
	int (*gem_prime_mmap)(struct drm_gem_object *obj,
				struct vm_area_struct *vma);

	/* vga arb irq handler */
	void (*vgaarb_irq)(struct drm_device *dev, bool state);

	/* dumb alloc support */
	int (*dumb_create)(struct drm_file *file_priv,
			   struct drm_device *dev,
			   struct drm_mode_create_dumb *args);
	int (*dumb_map_offset)(struct drm_file *file_priv,
			       struct drm_device *dev, uint32_t handle,
			       uint64_t *offset);
	int (*dumb_destroy)(struct drm_file *file_priv,
			    struct drm_device *dev,
			    uint32_t handle);

	/* Driver private ops for this object */
	const struct vm_operations_struct *gem_vm_ops;

	int major;
	int minor;
	int patchlevel;
	char *name;
	char *desc;
	char *date;

	u32 driver_features;
	int dev_priv_size;
	struct drm_ioctl_desc *ioctls;
	int num_ioctls;
	struct file_operations fops;
	struct pci_driver pci_driver;
};

#define DRM_MINOR_UNASSIGNED 0
#define DRM_MINOR_LEGACY 1
	const struct drm_ioctl_desc *ioctls;
	int num_ioctls;
	const struct file_operations *fops;

	/* List of devices hanging off this driver with stealth attach. */
	struct list_head legacy_dev_list;
};

enum drm_minor_type {
	DRM_MINOR_LEGACY,
	DRM_MINOR_CONTROL,
	DRM_MINOR_RENDER,
	DRM_MINOR_CNT,
};

/**
 * Info file list entry. This structure represents a debugfs or proc file to
 * be created by the drm core
 */
struct drm_info_list {
	const char *name; /** file name */
	int (*show)(struct seq_file*, void*); /** show callback */
	u32 driver_features; /**< Required driver features for this entry */
	void *data;
};

/**
 * debugfs node structure. This structure represents a debugfs file.
 */
struct drm_info_node {
	struct list_head list;
	struct drm_minor *minor;
	const struct drm_info_list *info_ent;
	struct dentry *dent;
};

/**
 * DRM minor structure. This structure represents a drm minor number.
 */
struct drm_minor {
	int index;			/**< Minor device number */
	int type;                       /**< Control or render */
	dev_t device;			/**< Device number for mknod */
	struct device kdev;		/**< Linux device */
	struct drm_device *dev;
	struct proc_dir_entry *dev_root;  /**< proc directory entry */
	struct device *kdev;		/**< Linux device */
	struct drm_device *dev;

	struct dentry *debugfs_root;

	struct list_head debugfs_list;
	struct mutex debugfs_lock; /* Protects debugfs_list. */

	/* currently active master for this node. Protected by master_mutex */
	struct drm_master *master;
};


struct drm_pending_vblank_event {
	struct drm_pending_event base;
	unsigned int pipe;
	struct drm_event_vblank event;
};

struct drm_vblank_crtc {
	struct drm_device *dev;		/* pointer to the drm_device */
	wait_queue_head_t queue;	/**< VBLANK wait queue */
	struct timer_list disable_timer;		/* delayed disable timer */

	/* vblank counter, protected by dev->vblank_time_lock for writes */
	u32 count;
	/* vblank timestamps, protected by dev->vblank_time_lock for writes */
	struct timeval time[DRM_VBLANKTIME_RBSIZE];

	atomic_t refcount;		/* number of users of vblank interruptsper crtc */
	u32 last;			/* protected by dev->vbl_lock, used */
					/* for wraparound handling */
	u32 last_wait;			/* Last vblank seqno waited per CRTC */
	unsigned int inmodeset;		/* Display driver is setting mode */
	unsigned int pipe;		/* crtc index */
	int framedur_ns;		/* frame/field duration in ns */
	int linedur_ns;			/* line duration in ns */
	bool enabled;			/* so we don't call enable more than
					   once per disable */
};

/**
 * DRM device structure. This structure represent a complete card that
 * may contain multiple heads.
 */
struct drm_device {
	char *unique;			/**< Unique identifier: e.g., busid */
	int unique_len;			/**< Length of unique field */
	char *devname;			/**< For /proc/interrupts */
	int if_version;			/**< Highest interface version set */

	int blocked;			/**< Blocked due to VC switch? */

	/** \name Locks */
	/*@{ */
	spinlock_t count_lock;		/**< For inuse, drm_device::open_count, drm_device::buf_use */
	struct mutex struct_mutex;	/**< For others */
	struct list_head legacy_dev_list;/**< list of devices per driver for stealth attach cleanup */
	int if_version;			/**< Highest interface version set */

	/** \name Lifetime Management */
	/*@{ */
	struct kref ref;		/**< Object ref-count */
	struct device *dev;		/**< Device structure of bus-device */
	struct drm_driver *driver;	/**< DRM driver managing the device */
	void *dev_private;		/**< DRM driver private data */
	struct drm_minor *control;		/**< Control node */
	struct drm_minor *primary;		/**< Primary node */
	struct drm_minor *render;		/**< Render node */
	atomic_t unplugged;			/**< Flag whether dev is dead */
	struct inode *anon_inode;		/**< inode for private address-space */
	char *unique;				/**< unique name of the device */
	/*@} */

	/** \name Locks */
	/*@{ */
	struct mutex struct_mutex;	/**< For others */
	struct mutex master_mutex;      /**< For drm_minor::master and drm_file::is_master */
	/*@} */

	/** \name Usage Counters */
	/*@{ */
	int open_count;			/**< Outstanding files open */
	atomic_t ioctl_count;		/**< Outstanding IOCTLs pending */
	atomic_t vma_count;		/**< Outstanding vma areas open */
	int open_count;			/**< Outstanding files open, protected by drm_global_mutex. */
	spinlock_t buf_lock;		/**< For drm_device::buf_use and a few other things. */
	int buf_use;			/**< Buffers in use -- cannot alloc */
	atomic_t buf_alloc;		/**< Buffer allocation in progress */
	/*@} */

	/** \name Performance counters */
	/*@{ */
	unsigned long counters;
	enum drm_stat_type types[15];
	atomic_t counts[15];
	/*@} */

	/** \name Authentication */
	/*@{ */
	struct list_head filelist;
	struct drm_open_hash magiclist;	/**< magic hash table */
	struct list_head magicfree;
	/*@} */
	struct list_head filelist;

	/** \name Memory management */
	/*@{ */
	struct list_head maplist;	/**< Linked list of regions */
	int map_count;			/**< Number of mappable regions */
	struct drm_open_hash map_hash;	/**< User token hash table for maps */

	/** \name Context handle management */
	/*@{ */
	struct list_head ctxlist;	/**< Linked list of context handles */
	int ctx_count;			/**< Number of context handles */
	struct mutex ctxlist_mutex;	/**< For ctxlist */

	struct idr ctx_idr;

	struct list_head vmalist;	/**< List of vmas (for debugging) */
	struct drm_lock_data lock;	/**< Information on hardware lock */
	/*@} */

	/** \name DMA queues (contexts) */
	/*@{ */
	int queue_count;		/**< Number of active DMA queues */
	int queue_reserved;		  /**< Number of reserved DMA queues */
	int queue_slots;		/**< Actual length of queuelist */
	struct drm_queue **queuelist;	/**< Vector of pointers to DMA queues */

	/*@} */

	/** \name DMA support */
	/*@{ */
	struct drm_device_dma *dma;		/**< Optional pointer for DMA support */
	/*@} */

	/** \name Context support */
	/*@{ */
	int irq;			/**< Interrupt used by board */
	int irq_enabled;		/**< True if irq handler is enabled */
	__volatile__ long context_flag;	/**< Context swapping flag */
	__volatile__ long interrupt_flag; /**< Interruption handler flag */
	__volatile__ long dma_flag;	/**< DMA dispatch flag */
	struct timer_list timer;	/**< Timer for delaying ctx switch */
	wait_queue_head_t context_wait;	/**< Processes waiting on ctx switch */
	int last_checked;		/**< Last context checked for DMA */
	int last_context;		/**< Last current context */
	unsigned long last_switch;	/**< jiffies at last context switch */
	/*@} */

	struct work_struct work;
	/** \name VBLANK IRQ support */
	/*@{ */

	wait_queue_head_t vbl_queue;	/**< VBLANK wait queue */
	atomic_t vbl_received;
	atomic_t vbl_received2;		/**< number of secondary VBLANK interrupts */
	spinlock_t vbl_lock;
	struct list_head vbl_sigs;		/**< signal list to send on VBLANK */
	struct list_head vbl_sigs2;	/**< signals to send on secondary VBLANK */
	unsigned int vbl_pending;
	spinlock_t tasklet_lock;	/**< For drm_locked_tasklet */
	void (*locked_tasklet_func)(struct drm_device *dev);

	/*@} */
	cycles_t ctx_start;
	cycles_t lck_start;

	struct fasync_struct *buf_async;/**< Processes waiting for SIGIO */
	wait_queue_head_t buf_readers;	/**< Processes waiting to read */
	wait_queue_head_t buf_writers;	/**< Processes waiting to ctx switch */

	__volatile__ long context_flag;	/**< Context swapping flag */
	int last_context;		/**< Last current context */
	/*@} */

	/** \name VBLANK IRQ support */
	/*@{ */
	bool irq_enabled;
	int irq;

	/*
	 * At load time, disabling the vblank interrupt won't be allowed since
	 * old clients may not call the modeset ioctl and therefore misbehave.
	 * Once the modeset ioctl *has* been called though, we can safely
	 * disable them when unused.
	 */
	bool vblank_disable_allowed;

	/*
	 * If true, vblank interrupt will be disabled immediately when the
	 * refcount drops to zero, as opposed to via the vblank disable
	 * timer.
	 * This can be set to true it the hardware has a working vblank
	 * counter and the driver uses drm_vblank_on() and drm_vblank_off()
	 * appropriately.
	 */
	bool vblank_disable_immediate;

	/* array of size num_crtcs */
	struct drm_vblank_crtc *vblank;

	spinlock_t vblank_time_lock;    /**< Protects vblank count and time updates during vblank enable/disable */
	spinlock_t vbl_lock;

	u32 max_vblank_count;           /**< size of vblank counter register */

	/**
	 * List of events
	 */
	struct list_head vblank_event_list;
	spinlock_t event_lock;

	/*@} */

	struct drm_agp_head *agp;	/**< AGP data */

	struct pci_dev *pdev;		/**< PCI device structure */
	int pci_vendor;			/**< PCI vendor id */
	int pci_device;			/**< PCI device id */
#ifdef __alpha__
	struct pci_controller *hose;
#endif
	struct drm_sg_mem *sg;	/**< Scatter gather memory */
	void *dev_private;		/**< device private data */
	struct drm_sigdata sigdata;	   /**< For block_all_signals */
	sigset_t sigmask;

	struct drm_driver *driver;
	drm_local_map_t *agp_buffer_map;
	unsigned int agp_buffer_token;
	struct drm_minor *primary;		/**< render type primary screen head */

	/** \name Drawable information */
	/*@{ */
	spinlock_t drw_lock;
	struct idr drw_idr;
	/*@} */
};

#ifdef __alpha__
	struct pci_controller *hose;
#endif

	struct platform_device *platformdev; /**< Platform device struture */
	struct virtio_device *virtdev;

	struct drm_sg_mem *sg;	/**< Scatter gather memory */
	unsigned int num_crtcs;                  /**< Number of CRTCs on this device */

	struct {
		int context;
		struct drm_hw_lock *lock;
	} sigdata;

	struct drm_local_map *agp_buffer_map;
	unsigned int agp_buffer_token;

	struct drm_mode_config mode_config;	/**< Current mode config */

	/** \name GEM information */
	/*@{ */
	struct mutex object_name_lock;
	struct idr object_name_idr;
	struct drm_vma_offset_manager *vma_offset_manager;
	/*@} */
	int switch_power_state;
};

#define DRM_SWITCH_POWER_ON 0
#define DRM_SWITCH_POWER_OFF 1
#define DRM_SWITCH_POWER_CHANGING 2
#define DRM_SWITCH_POWER_DYNAMIC_OFF 3

static __inline__ int drm_core_check_feature(struct drm_device *dev,
					     int feature)
{
	return ((dev->driver->driver_features & feature) ? 1 : 0);
}

#ifdef __alpha__
#define drm_get_pci_domain(dev) dev->hose->index
#else
#define drm_get_pci_domain(dev) 0
#endif

#if __OS_HAS_AGP
static inline int drm_core_has_AGP(struct drm_device *dev)
{
	return drm_core_check_feature(dev, DRIVER_USE_AGP);
}
#else
#define drm_core_has_AGP(dev) (0)
#endif

#if __OS_HAS_MTRR
static inline int drm_core_has_MTRR(struct drm_device *dev)
{
	return drm_core_check_feature(dev, DRIVER_USE_MTRR);
}

#define DRM_MTRR_WC		MTRR_TYPE_WRCOMB

static inline int drm_mtrr_add(unsigned long offset, unsigned long size,
			       unsigned int flags)
{
	return mtrr_add(offset, size, flags, 1);
}

static inline int drm_mtrr_del(int handle, unsigned long offset,
			       unsigned long size, unsigned int flags)
{
	return mtrr_del(handle, offset, size);
}

#else
#define drm_core_has_MTRR(dev) (0)

#define DRM_MTRR_WC		0

static inline int drm_mtrr_add(unsigned long offset, unsigned long size,
			       unsigned int flags)
{
	return 0;
}

static inline int drm_mtrr_del(int handle, unsigned long offset,
			       unsigned long size, unsigned int flags)
{
	return 0;
}
#endif
static inline void drm_device_set_unplugged(struct drm_device *dev)
{
	smp_wmb();
	atomic_set(&dev->unplugged, 1);
}

static inline int drm_device_is_unplugged(struct drm_device *dev)
{
	int ret = atomic_read(&dev->unplugged);
	smp_rmb();
	return ret;
}

static inline bool drm_is_render_client(const struct drm_file *file_priv)
{
	return file_priv->minor->type == DRM_MINOR_RENDER;
}

static inline bool drm_is_control_client(const struct drm_file *file_priv)
{
	return file_priv->minor->type == DRM_MINOR_CONTROL;
}

static inline bool drm_is_primary_client(const struct drm_file *file_priv)
{
	return file_priv->minor->type == DRM_MINOR_LEGACY;
}

/******************************************************************/
/** \name Internal function definitions */
/*@{*/

				/* Driver support (drm_drv.h) */
extern int drm_init(struct drm_driver *driver);
extern void drm_exit(struct drm_driver *driver);
extern int drm_ioctl(struct inode *inode, struct file *filp,
		     unsigned int cmd, unsigned long arg);
extern long drm_compat_ioctl(struct file *filp,
			     unsigned int cmd, unsigned long arg);
extern int drm_lastclose(struct drm_device *dev);

				/* Device support (drm_fops.h) */
extern int drm_open(struct inode *inode, struct file *filp);
extern int drm_stub_open(struct inode *inode, struct file *filp);
extern int drm_fasync(int fd, struct file *filp, int on);
extern int drm_release(struct inode *inode, struct file *filp);

				/* Mapping support (drm_vm.h) */
extern int drm_mmap(struct file *filp, struct vm_area_struct *vma);
extern unsigned long drm_core_get_map_ofs(struct drm_map * map);
extern unsigned long drm_core_get_reg_ofs(struct drm_device *dev);
extern unsigned int drm_poll(struct file *filp, struct poll_table_struct *wait);

				/* Memory management support (drm_memory.h) */
#include "drm_memory.h"
extern void drm_mem_init(void);
extern int drm_mem_info(char *buf, char **start, off_t offset,
			int request, int *eof, void *data);
extern void *drm_realloc(void *oldpt, size_t oldsize, size_t size, int area);

extern DRM_AGP_MEM *drm_alloc_agp(struct drm_device *dev, int pages, u32 type);
extern int drm_free_agp(DRM_AGP_MEM * handle, int pages);
extern int drm_bind_agp(DRM_AGP_MEM * handle, unsigned int start);
extern int drm_unbind_agp(DRM_AGP_MEM * handle);

				/* Misc. IOCTL support (drm_ioctl.h) */
extern int drm_irq_by_busid(struct drm_device *dev, void *data,
			    struct drm_file *file_priv);
extern int drm_getunique(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);
extern int drm_setunique(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);
extern int drm_getmap(struct drm_device *dev, void *data,
		      struct drm_file *file_priv);
extern int drm_getclient(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);
extern int drm_getstats(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
extern int drm_setversion(struct drm_device *dev, void *data,
			  struct drm_file *file_priv);
extern int drm_noop(struct drm_device *dev, void *data,
		    struct drm_file *file_priv);

				/* Context IOCTL support (drm_context.h) */
extern int drm_resctx(struct drm_device *dev, void *data,
		      struct drm_file *file_priv);
extern int drm_addctx(struct drm_device *dev, void *data,
		      struct drm_file *file_priv);
extern int drm_modctx(struct drm_device *dev, void *data,
		      struct drm_file *file_priv);
extern int drm_getctx(struct drm_device *dev, void *data,
		      struct drm_file *file_priv);
extern int drm_switchctx(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);
extern int drm_newctx(struct drm_device *dev, void *data,
		      struct drm_file *file_priv);
extern int drm_rmctx(struct drm_device *dev, void *data,
		     struct drm_file *file_priv);

extern int drm_ctxbitmap_init(struct drm_device *dev);
extern void drm_ctxbitmap_cleanup(struct drm_device *dev);
extern void drm_ctxbitmap_free(struct drm_device *dev, int ctx_handle);

extern int drm_setsareactx(struct drm_device *dev, void *data,
			   struct drm_file *file_priv);
extern int drm_getsareactx(struct drm_device *dev, void *data,
			   struct drm_file *file_priv);

				/* Drawable IOCTL support (drm_drawable.h) */
extern int drm_adddraw(struct drm_device *dev, void *data,
		       struct drm_file *file_priv);
extern int drm_rmdraw(struct drm_device *dev, void *data,
		      struct drm_file *file_priv);
extern int drm_update_drawable_info(struct drm_device *dev, void *data,
				    struct drm_file *file_priv);
extern struct drm_drawable_info *drm_get_drawable_info(struct drm_device *dev,
						  drm_drawable_t id);
extern void drm_drawable_free_all(struct drm_device *dev);

				/* Authentication IOCTL support (drm_auth.h) */
extern int drm_getmagic(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
extern int drm_authmagic(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);

				/* Locking IOCTL support (drm_lock.h) */
extern int drm_lock(struct drm_device *dev, void *data,
		    struct drm_file *file_priv);
extern int drm_unlock(struct drm_device *dev, void *data,
		      struct drm_file *file_priv);
extern int drm_lock_take(struct drm_lock_data *lock_data, unsigned int context);
extern int drm_lock_free(struct drm_lock_data *lock_data, unsigned int context);
extern void drm_idlelock_take(struct drm_lock_data *lock_data);
extern void drm_idlelock_release(struct drm_lock_data *lock_data);
extern int drm_ioctl_permit(u32 flags, struct drm_file *file_priv);
extern long drm_ioctl(struct file *filp,
		      unsigned int cmd, unsigned long arg);
extern long drm_compat_ioctl(struct file *filp,
			     unsigned int cmd, unsigned long arg);
extern bool drm_ioctl_flags(unsigned int nr, unsigned int *flags);

				/* Device support (drm_fops.h) */
extern int drm_open(struct inode *inode, struct file *filp);
extern ssize_t drm_read(struct file *filp, char __user *buffer,
			size_t count, loff_t *offset);
extern int drm_release(struct inode *inode, struct file *filp);
extern int drm_new_set_master(struct drm_device *dev, struct drm_file *fpriv);

				/* Mapping support (drm_vm.h) */
extern unsigned int drm_poll(struct file *filp, struct poll_table_struct *wait);

/* Misc. IOCTL support (drm_ioctl.c) */
int drm_noop(struct drm_device *dev, void *data,
	     struct drm_file *file_priv);
int drm_invalid_op(struct drm_device *dev, void *data,
		   struct drm_file *file_priv);

/* Cache management (drm_cache.c) */
void drm_clflush_pages(struct page *pages[], unsigned long num_pages);
void drm_clflush_sg(struct sg_table *st);
void drm_clflush_virt_range(void *addr, unsigned long length);

/*
 * These are exported to drivers so that they can implement fencing using
 * DMA quiscent + idle. DMA quiescent usually requires the hardware lock.
 */

extern int drm_i_have_hw_lock(struct drm_device *dev, struct drm_file *file_priv);

				/* Buffer management support (drm_bufs.h) */
extern int drm_addbufs_agp(struct drm_device *dev, struct drm_buf_desc * request);
extern int drm_addbufs_pci(struct drm_device *dev, struct drm_buf_desc * request);
extern int drm_addmap(struct drm_device *dev, unsigned int offset,
		      unsigned int size, enum drm_map_type type,
		      enum drm_map_flags flags, drm_local_map_t ** map_ptr);
extern int drm_addmap_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file_priv);
extern int drm_rmmap(struct drm_device *dev, drm_local_map_t *map);
extern int drm_rmmap_locked(struct drm_device *dev, drm_local_map_t *map);
extern int drm_rmmap_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv);
extern int drm_addbufs(struct drm_device *dev, void *data,
		       struct drm_file *file_priv);
extern int drm_infobufs(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
extern int drm_markbufs(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
extern int drm_freebufs(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
extern int drm_mapbufs(struct drm_device *dev, void *data,
		       struct drm_file *file_priv);
extern int drm_order(unsigned long size);
extern unsigned long drm_get_resource_start(struct drm_device *dev,
					    unsigned int resource);
extern unsigned long drm_get_resource_len(struct drm_device *dev,
					  unsigned int resource);

				/* DMA support (drm_dma.h) */
extern int drm_dma_setup(struct drm_device *dev);
extern void drm_dma_takedown(struct drm_device *dev);
extern void drm_free_buffer(struct drm_device *dev, struct drm_buf * buf);
extern void drm_core_reclaim_buffers(struct drm_device *dev,
				     struct drm_file *filp);

				/* IRQ support (drm_irq.h) */
extern int drm_control(struct drm_device *dev, void *data,
		       struct drm_file *file_priv);
extern irqreturn_t drm_irq_handler(DRM_IRQ_ARGS);
extern int drm_irq_uninstall(struct drm_device *dev);
extern void drm_driver_irq_preinstall(struct drm_device *dev);
extern void drm_driver_irq_postinstall(struct drm_device *dev);
extern void drm_driver_irq_uninstall(struct drm_device *dev);

extern int drm_wait_vblank(struct drm_device *dev, void *data,
			   struct drm_file *file_priv);
extern int drm_vblank_wait(struct drm_device *dev, unsigned int *vbl_seq);
extern void drm_vbl_send_signals(struct drm_device *dev);
extern void drm_locked_tasklet(struct drm_device *dev, void(*func)(struct drm_device*));

				/* AGP/GART support (drm_agpsupport.h) */
extern struct drm_agp_head *drm_agp_init(struct drm_device *dev);
extern int drm_agp_acquire(struct drm_device *dev);
extern int drm_agp_acquire_ioctl(struct drm_device *dev, void *data,
				 struct drm_file *file_priv);
extern int drm_agp_release(struct drm_device *dev);
extern int drm_agp_release_ioctl(struct drm_device *dev, void *data,
				 struct drm_file *file_priv);
extern int drm_agp_enable(struct drm_device *dev, struct drm_agp_mode mode);
extern int drm_agp_enable_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv);
extern int drm_agp_info(struct drm_device *dev, struct drm_agp_info *info);
extern int drm_agp_info_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
extern int drm_agp_alloc(struct drm_device *dev, struct drm_agp_buffer *request);
extern int drm_agp_alloc_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);
extern int drm_agp_free(struct drm_device *dev, struct drm_agp_buffer *request);
extern int drm_agp_free_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
extern int drm_agp_unbind(struct drm_device *dev, struct drm_agp_binding *request);
extern int drm_agp_unbind_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file_priv);
extern int drm_agp_bind(struct drm_device *dev, struct drm_agp_binding *request);
extern int drm_agp_bind_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
extern DRM_AGP_MEM *drm_agp_allocate_memory(struct agp_bridge_data *bridge, size_t pages, u32 type);
extern int drm_agp_free_memory(DRM_AGP_MEM * handle);
extern int drm_agp_bind_memory(DRM_AGP_MEM * handle, off_t start);
extern int drm_agp_unbind_memory(DRM_AGP_MEM * handle);

				/* Stub support (drm_stub.h) */
extern int drm_get_dev(struct pci_dev *pdev, const struct pci_device_id *ent,
		       struct drm_driver *driver);
extern int drm_put_dev(struct drm_device *dev);
extern int drm_put_minor(struct drm_minor **minor);
extern unsigned int drm_debug;

extern struct class *drm_class;
extern struct proc_dir_entry *drm_proc_root;

extern struct idr drm_minors_idr;

extern drm_local_map_t *drm_getsarea(struct drm_device *dev);

				/* Proc support (drm_proc.h) */
extern int drm_proc_init(struct drm_minor *minor, int minor_id,
			 struct proc_dir_entry *root);
extern int drm_proc_cleanup(struct drm_minor *minor, struct proc_dir_entry *root);

				/* Scatter Gather Support (drm_scatter.h) */
extern void drm_sg_cleanup(struct drm_sg_mem * entry);
extern int drm_sg_alloc_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
extern int drm_sg_alloc(struct drm_device *dev, struct drm_scatter_gather * request);
extern int drm_sg_free(struct drm_device *dev, void *data,
		       struct drm_file *file_priv);

			       /* ATI PCIGART support (ati_pcigart.h) */
extern int drm_ati_pcigart_init(struct drm_device *dev,
				struct drm_ati_pcigart_info * gart_info);
extern int drm_ati_pcigart_cleanup(struct drm_device *dev,
				   struct drm_ati_pcigart_info * gart_info);

extern drm_dma_handle_t *drm_pci_alloc(struct drm_device *dev, size_t size,
				       size_t align, dma_addr_t maxaddr);
extern void __drm_pci_free(struct drm_device *dev, drm_dma_handle_t * dmah);
extern void drm_pci_free(struct drm_device *dev, drm_dma_handle_t * dmah);

			       /* sysfs support (drm_sysfs.c) */
struct drm_sysfs_class;
extern struct class *drm_sysfs_create(struct module *owner, char *name);
extern void drm_sysfs_destroy(void);
extern int drm_sysfs_device_add(struct drm_minor *minor);
extern void drm_sysfs_device_remove(struct drm_minor *minor);

/*
 * Basic memory manager support (drm_mm.c)
 */
extern struct drm_mm_node *drm_mm_get_block(struct drm_mm_node * parent,
				       unsigned long size,
				       unsigned alignment);
extern void drm_mm_put_block(struct drm_mm_node * cur);
extern struct drm_mm_node *drm_mm_search_free(const struct drm_mm *mm, unsigned long size,
					 unsigned alignment, int best_match);
extern int drm_mm_init(struct drm_mm *mm, unsigned long start, unsigned long size);
extern void drm_mm_takedown(struct drm_mm *mm);
extern int drm_mm_clean(struct drm_mm *mm);
extern unsigned long drm_mm_tail_space(struct drm_mm *mm);
extern int drm_mm_remove_space_from_tail(struct drm_mm *mm, unsigned long size);
extern int drm_mm_add_space_to_tail(struct drm_mm *mm, unsigned long size);

extern void drm_core_ioremap(struct drm_map *map, struct drm_device *dev);
extern void drm_core_ioremap_wc(struct drm_map *map, struct drm_device *dev);
extern void drm_core_ioremapfree(struct drm_map *map, struct drm_device *dev);

static __inline__ struct drm_map *drm_core_findmap(struct drm_device *dev,
						   unsigned int token)
{
	struct drm_map_list *_entry;
	list_for_each_entry(_entry, &dev->maplist, head)
	    if (_entry->user_token == token)
		return _entry->map;
	return NULL;
}

static __inline__ int drm_device_is_agp(struct drm_device *dev)
				/* IRQ support (drm_irq.h) */
extern int drm_irq_install(struct drm_device *dev, int irq);
extern int drm_irq_uninstall(struct drm_device *dev);

extern int drm_vblank_init(struct drm_device *dev, unsigned int num_crtcs);
extern int drm_wait_vblank(struct drm_device *dev, void *data,
			   struct drm_file *filp);
extern u32 drm_vblank_count(struct drm_device *dev, unsigned int pipe);
extern u32 drm_crtc_vblank_count(struct drm_crtc *crtc);
extern u32 drm_vblank_count_and_time(struct drm_device *dev, unsigned int pipe,
				     struct timeval *vblanktime);
extern u32 drm_crtc_vblank_count_and_time(struct drm_crtc *crtc,
					  struct timeval *vblanktime);
extern void drm_send_vblank_event(struct drm_device *dev, unsigned int pipe,
				  struct drm_pending_vblank_event *e);
extern void drm_crtc_send_vblank_event(struct drm_crtc *crtc,
				       struct drm_pending_vblank_event *e);
extern void drm_arm_vblank_event(struct drm_device *dev, unsigned int pipe,
				 struct drm_pending_vblank_event *e);
extern void drm_crtc_arm_vblank_event(struct drm_crtc *crtc,
				      struct drm_pending_vblank_event *e);
extern bool drm_handle_vblank(struct drm_device *dev, unsigned int pipe);
extern bool drm_crtc_handle_vblank(struct drm_crtc *crtc);
extern int drm_vblank_get(struct drm_device *dev, unsigned int pipe);
extern void drm_vblank_put(struct drm_device *dev, unsigned int pipe);
extern int drm_crtc_vblank_get(struct drm_crtc *crtc);
extern void drm_crtc_vblank_put(struct drm_crtc *crtc);
extern void drm_wait_one_vblank(struct drm_device *dev, unsigned int pipe);
extern void drm_crtc_wait_one_vblank(struct drm_crtc *crtc);
extern void drm_vblank_off(struct drm_device *dev, unsigned int pipe);
extern void drm_vblank_on(struct drm_device *dev, unsigned int pipe);
extern void drm_crtc_vblank_off(struct drm_crtc *crtc);
extern void drm_crtc_vblank_reset(struct drm_crtc *crtc);
extern void drm_crtc_vblank_on(struct drm_crtc *crtc);
extern void drm_vblank_cleanup(struct drm_device *dev);
extern u32 drm_vblank_no_hw_counter(struct drm_device *dev, unsigned int pipe);

extern int drm_calc_vbltimestamp_from_scanoutpos(struct drm_device *dev,
						 unsigned int pipe, int *max_error,
						 struct timeval *vblank_time,
						 unsigned flags,
						 const struct drm_display_mode *mode);
extern void drm_calc_timestamping_constants(struct drm_crtc *crtc,
					    const struct drm_display_mode *mode);

/**
 * drm_crtc_vblank_waitqueue - get vblank waitqueue for the CRTC
 * @crtc: which CRTC's vblank waitqueue to retrieve
 *
 * This function returns a pointer to the vblank waitqueue for the CRTC.
 * Drivers can use this to implement vblank waits using wait_event() & co.
 */
static inline wait_queue_head_t *drm_crtc_vblank_waitqueue(struct drm_crtc *crtc)
{
	return &crtc->dev->vblank[drm_crtc_index(crtc)].queue;
}

/* Modesetting support */
extern void drm_vblank_pre_modeset(struct drm_device *dev, unsigned int pipe);
extern void drm_vblank_post_modeset(struct drm_device *dev, unsigned int pipe);

				/* Stub support (drm_stub.h) */
extern struct drm_master *drm_master_get(struct drm_master *master);
extern void drm_master_put(struct drm_master **master);

extern void drm_put_dev(struct drm_device *dev);
extern void drm_unplug_dev(struct drm_device *dev);
extern unsigned int drm_debug;
extern bool drm_atomic;

				/* Debugfs support */
#if defined(CONFIG_DEBUG_FS)
extern int drm_debugfs_create_files(const struct drm_info_list *files,
				    int count, struct dentry *root,
				    struct drm_minor *minor);
extern int drm_debugfs_remove_files(const struct drm_info_list *files,
				    int count, struct drm_minor *minor);
#else
static inline int drm_debugfs_create_files(const struct drm_info_list *files,
					   int count, struct dentry *root,
					   struct drm_minor *minor)
{
	return 0;
}

static inline int drm_debugfs_remove_files(const struct drm_info_list *files,
					   int count, struct drm_minor *minor)
{
	return 0;
}
#endif

extern struct dma_buf *drm_gem_prime_export(struct drm_device *dev,
					    struct drm_gem_object *obj,
					    int flags);
extern int drm_gem_prime_handle_to_fd(struct drm_device *dev,
		struct drm_file *file_priv, uint32_t handle, uint32_t flags,
		int *prime_fd);
extern struct drm_gem_object *drm_gem_prime_import(struct drm_device *dev,
		struct dma_buf *dma_buf);
extern int drm_gem_prime_fd_to_handle(struct drm_device *dev,
		struct drm_file *file_priv, int prime_fd, uint32_t *handle);
extern void drm_gem_dmabuf_release(struct dma_buf *dma_buf);

extern int drm_prime_sg_to_page_addr_arrays(struct sg_table *sgt, struct page **pages,
					    dma_addr_t *addrs, int max_pages);
extern struct sg_table *drm_prime_pages_to_sg(struct page **pages, unsigned int nr_pages);
extern void drm_prime_gem_destroy(struct drm_gem_object *obj, struct sg_table *sg);


extern struct drm_dma_handle *drm_pci_alloc(struct drm_device *dev, size_t size,
					    size_t align);
extern void drm_pci_free(struct drm_device *dev, struct drm_dma_handle * dmah);

			       /* sysfs support (drm_sysfs.c) */
extern void drm_sysfs_hotplug_event(struct drm_device *dev);


struct drm_device *drm_dev_alloc(struct drm_driver *driver,
				 struct device *parent);
void drm_dev_ref(struct drm_device *dev);
void drm_dev_unref(struct drm_device *dev);
int drm_dev_register(struct drm_device *dev, unsigned long flags);
void drm_dev_unregister(struct drm_device *dev);
int drm_dev_set_unique(struct drm_device *dev, const char *fmt, ...);

struct drm_minor *drm_minor_acquire(unsigned int minor_id);
void drm_minor_release(struct drm_minor *minor);

/*@}*/

/* PCI section */
static __inline__ int drm_pci_device_is_agp(struct drm_device *dev)
{
	if (dev->driver->device_is_agp != NULL) {
		int err = (*dev->driver->device_is_agp) (dev);

		if (err != 2) {
			return err;
		}
	}

	return pci_find_capability(dev->pdev, PCI_CAP_ID_AGP);
}

static __inline__ int drm_device_is_pcie(struct drm_device *dev)
{
	return pci_find_capability(dev->pdev, PCI_CAP_ID_EXP);
}

static __inline__ void drm_core_dropmap(struct drm_map *map)
{
}

#ifndef DEBUG_MEMORY
/** Wrapper around kmalloc() */
static __inline__ void *drm_alloc(size_t size, int area)
{
	return kmalloc(size, GFP_KERNEL);
}

/** Wrapper around kfree() */
static __inline__ void drm_free(void *pt, size_t size, int area)
{
	kfree(pt);
}

/** Wrapper around kcalloc() */
static __inline__ void *drm_calloc(size_t nmemb, size_t size, int area)
{
	return kcalloc(nmemb, size, GFP_KERNEL);
}
#else
extern void *drm_alloc(size_t size, int area);
extern void drm_free(void *pt, size_t size, int area);
extern void *drm_calloc(size_t nmemb, size_t size, int area);
#endif

/*@}*/

#endif				/* __KERNEL__ */
void drm_pci_agp_destroy(struct drm_device *dev);

extern int drm_pci_init(struct drm_driver *driver, struct pci_driver *pdriver);
extern void drm_pci_exit(struct drm_driver *driver, struct pci_driver *pdriver);
#ifdef CONFIG_PCI
extern int drm_get_pci_dev(struct pci_dev *pdev,
			   const struct pci_device_id *ent,
			   struct drm_driver *driver);
extern int drm_pci_set_busid(struct drm_device *dev, struct drm_master *master);
#else
static inline int drm_get_pci_dev(struct pci_dev *pdev,
				  const struct pci_device_id *ent,
				  struct drm_driver *driver)
{
	return -ENOSYS;
}

static inline int drm_pci_set_busid(struct drm_device *dev,
				    struct drm_master *master)
{
	return -ENOSYS;
}
#endif

#define DRM_PCIE_SPEED_25 1
#define DRM_PCIE_SPEED_50 2
#define DRM_PCIE_SPEED_80 4

extern int drm_pcie_get_speed_cap_mask(struct drm_device *dev, u32 *speed_mask);

/* platform section */
extern int drm_platform_init(struct drm_driver *driver, struct platform_device *platform_device);
extern int drm_platform_set_busid(struct drm_device *d, struct drm_master *m);

/* returns true if currently okay to sleep */
static __inline__ bool drm_can_sleep(void)
{
	if (in_atomic() || in_dbg_master() || irqs_disabled())
		return false;
	return true;
}

#endif
