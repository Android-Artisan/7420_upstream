/******************************************************************************
 * balloon.c
 *
 * Xen balloon driver - enables returning/claiming memory to/from Xen.
 *
 * Copyright (c) 2003, B Dragovic
 * Copyright (c) 2003-2004, M Williamson, K Fraser
 * Copyright (c) 2005 Dan M. Smith, IBM Corporation
 * Copyright (c) 2010 Daniel Kiper
 *
 * Memory hotplug support was written by Daniel Kiper. Work on
 * it was sponsored by Google under Google Summer of Code 2010
 * program. Jeremy Fitzhardinge from Citrix was the mentor for
 * this project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/errno.h>
#define pr_fmt(fmt) "xen:" KBUILD_MODNAME ": " fmt

#include <linux/cpu.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/bootmem.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/mutex.h>
#include <linux/highmem.h>
#include <linux/list.h>
#include <linux/sysdev.h>

#include <asm/xen/hypervisor.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/tlb.h>

#include <xen/interface/memory.h>
#include <xen/balloon.h>
#include <xen/xenbus.h>
#include <xen/features.h>
#include <xen/page.h>

#define PAGES2KB(_p) ((_p)<<(PAGE_SHIFT-10))

#define BALLOON_CLASS_NAME "xen_memory"

struct balloon_stats {
	/* We aim for 'current allocation' == 'target allocation'. */
	unsigned long current_pages;
	unsigned long target_pages;
	/* We may hit the hard limit in Xen. If we do then we remember it. */
	unsigned long hard_limit;
	/*
	 * Drivers may alter the memory reservation independently, but they
	 * must inform the balloon driver so we avoid hitting the hard limit.
	 */
	unsigned long driver_pages;
	/* Number of pages in high- and low-memory balloons. */
	unsigned long balloon_low;
	unsigned long balloon_high;
};

static DEFINE_MUTEX(balloon_mutex);

static struct sys_device balloon_sysdev;

static int register_balloon(struct sys_device *sysdev);

/*
 * Protects atomic reservation decrease/increase against concurrent increases.
 * Also protects non-atomic updates of current_pages and driver_pages, and
 * balloon lists.
 */
static DEFINE_SPINLOCK(balloon_lock);

static struct balloon_stats balloon_stats;

/* We increase/decrease in batches which fit in a page */
static unsigned long frame_list[PAGE_SIZE / sizeof(unsigned long)];

/* VM /proc information for memory */
extern unsigned long totalram_pages;

#ifdef CONFIG_HIGHMEM
extern unsigned long totalhigh_pages;
#define inc_totalhigh_pages() (totalhigh_pages++)
#define dec_totalhigh_pages() (totalhigh_pages--)
#else
#define inc_totalhigh_pages() do {} while(0)
#define dec_totalhigh_pages() do {} while(0)
#endif

/* List of ballooned pages, threaded through the mem_map array. */
static LIST_HEAD(ballooned_pages);

/* Main work function, always executed in process context. */
static void balloon_process(struct work_struct *work);
static DECLARE_WORK(balloon_worker, balloon_process);
static struct timer_list balloon_timer;
#include <linux/list.h>
#include <linux/gfp.h>
#include <linux/notifier.h>
#include <linux/memory.h>
#include <linux/memory_hotplug.h>
#include <linux/percpu-defs.h>
#include <linux/slab.h>
#include <linux/sysctl.h>

#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/tlb.h>

#include <asm/xen/hypervisor.h>
#include <asm/xen/hypercall.h>

#include <xen/xen.h>
#include <xen/interface/xen.h>
#include <xen/interface/memory.h>
#include <xen/balloon.h>
#include <xen/features.h>
#include <xen/page.h>

static int xen_hotplug_unpopulated;

#ifdef CONFIG_XEN_BALLOON_MEMORY_HOTPLUG

static int zero;
static int one = 1;

static struct ctl_table balloon_table[] = {
	{
		.procname	= "hotplug_unpopulated",
		.data		= &xen_hotplug_unpopulated,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1         = &zero,
		.extra2         = &one,
	},
	{ }
};

static struct ctl_table balloon_root[] = {
	{
		.procname	= "balloon",
		.mode		= 0555,
		.child		= balloon_table,
	},
	{ }
};

static struct ctl_table xen_root[] = {
	{
		.procname	= "xen",
		.mode		= 0555,
		.child		= balloon_root,
	},
	{ }
};

#endif

/*
 * Use one extent per PAGE_SIZE to avoid to break down the page into
 * multiple frame.
 */
#define EXTENT_ORDER (fls(XEN_PFN_PER_PAGE) - 1)

/*
 * balloon_process() state:
 *
 * BP_DONE: done or nothing to do,
 * BP_WAIT: wait to be rescheduled,
 * BP_EAGAIN: error, go to sleep,
 * BP_ECANCELED: error, balloon operation canceled.
 */

enum bp_state {
	BP_DONE,
	BP_WAIT,
	BP_EAGAIN,
	BP_ECANCELED
};


static DEFINE_MUTEX(balloon_mutex);

struct balloon_stats balloon_stats;
EXPORT_SYMBOL_GPL(balloon_stats);

/* We increase/decrease in batches which fit in a page */
static xen_pfn_t frame_list[PAGE_SIZE / sizeof(xen_pfn_t)];


/* List of ballooned pages, threaded through the mem_map array. */
static LIST_HEAD(ballooned_pages);
static DECLARE_WAIT_QUEUE_HEAD(balloon_wq);

/* Main work function, always executed in process context. */
static void balloon_process(struct work_struct *work);
static DECLARE_DELAYED_WORK(balloon_worker, balloon_process);

/* When ballooning out (allocating memory to return to Xen) we don't really
   want the kernel to try too hard since that can trigger the oom killer. */
#define GFP_BALLOON \
	(GFP_HIGHUSER | __GFP_NOWARN | __GFP_NORETRY | __GFP_NOMEMALLOC)

static void scrub_page(struct page *page)
{
#ifdef CONFIG_XEN_SCRUB_PAGES
	if (PageHighMem(page)) {
		void *v = kmap(page);
		clear_page(v);
		kunmap(v);
	} else {
		void *v = page_address(page);
		clear_page(v);
	}
	clear_highpage(page);
#endif
}

/* balloon_append: add the given page to the balloon. */
static void balloon_append(struct page *page)
static void __balloon_append(struct page *page)
{
	/* Lowmem is re-populated first, so highmem pages go at list tail. */
	if (PageHighMem(page)) {
		list_add_tail(&page->lru, &ballooned_pages);
		balloon_stats.balloon_high++;
		dec_totalhigh_pages();
	} else {
		list_add(&page->lru, &ballooned_pages);
		balloon_stats.balloon_low++;
	}
}

/* balloon_retrieve: rescue a page from the balloon, if it is not empty. */
static struct page *balloon_retrieve(void)
	wake_up(&balloon_wq);
}

static void balloon_append(struct page *page)
{
	__balloon_append(page);
	adjust_managed_page_count(page, -1);
}

/* balloon_retrieve: rescue a page from the balloon, if it is not empty. */
static struct page *balloon_retrieve(bool require_lowmem)
{
	struct page *page;

	if (list_empty(&ballooned_pages))
		return NULL;

	page = list_entry(ballooned_pages.next, struct page, lru);
	list_del(&page->lru);

	if (PageHighMem(page)) {
		balloon_stats.balloon_high--;
		inc_totalhigh_pages();
	}
	else
		balloon_stats.balloon_low--;

	return page;
}

static struct page *balloon_first_page(void)
{
	if (list_empty(&ballooned_pages))
		return NULL;
	return list_entry(ballooned_pages.next, struct page, lru);
	if (require_lowmem && PageHighMem(page))
		return NULL;
	list_del(&page->lru);

	if (PageHighMem(page))
		balloon_stats.balloon_high--;
	else
		balloon_stats.balloon_low--;

	adjust_managed_page_count(page, 1);

	return page;
}

static struct page *balloon_next_page(struct page *page)
{
	struct list_head *next = page->lru.next;
	if (next == &ballooned_pages)
		return NULL;
	return list_entry(next, struct page, lru);
}

static void balloon_alarm(unsigned long unused)
{
	schedule_work(&balloon_worker);
}

static unsigned long current_target(void)
{
	unsigned long target = min(balloon_stats.target_pages, balloon_stats.hard_limit);

	target = min(target,
		     balloon_stats.current_pages +
		     balloon_stats.balloon_low +
		     balloon_stats.balloon_high);

	return target;
}

static int increase_reservation(unsigned long nr_pages)
{
	unsigned long  pfn, i, flags;
	struct page   *page;
	long           rc;
	struct xen_memory_reservation reservation = {
		.address_bits = 0,
		.extent_order = 0,
static enum bp_state update_schedule(enum bp_state state)
{
	if (state == BP_WAIT)
		return BP_WAIT;

	if (state == BP_ECANCELED)
		return BP_ECANCELED;

	if (state == BP_DONE) {
		balloon_stats.schedule_delay = 1;
		balloon_stats.retry_count = 1;
		return BP_DONE;
	}

	++balloon_stats.retry_count;

	if (balloon_stats.max_retry_count != RETRY_UNLIMITED &&
			balloon_stats.retry_count > balloon_stats.max_retry_count) {
		balloon_stats.schedule_delay = 1;
		balloon_stats.retry_count = 1;
		return BP_ECANCELED;
	}

	balloon_stats.schedule_delay <<= 1;

	if (balloon_stats.schedule_delay > balloon_stats.max_schedule_delay)
		balloon_stats.schedule_delay = balloon_stats.max_schedule_delay;

	return BP_EAGAIN;
}

#ifdef CONFIG_XEN_BALLOON_MEMORY_HOTPLUG
static void release_memory_resource(struct resource *resource)
{
	if (!resource)
		return;

	/*
	 * No need to reset region to identity mapped since we now
	 * know that no I/O can be in this region
	 */
	release_resource(resource);
	kfree(resource);
}

static struct resource *additional_memory_resource(phys_addr_t size)
{
	struct resource *res;
	int ret;

	res = kzalloc(sizeof(*res), GFP_KERNEL);
	if (!res)
		return NULL;

	res->name = "System RAM";
	res->flags = IORESOURCE_MEM | IORESOURCE_BUSY;

	ret = allocate_resource(&iomem_resource, res,
				size, 0, -1,
				PAGES_PER_SECTION * PAGE_SIZE, NULL, NULL);
	if (ret < 0) {
		pr_err("Cannot allocate new System RAM resource\n");
		kfree(res);
		return NULL;
	}

#ifdef CONFIG_SPARSEMEM
	{
		unsigned long limit = 1UL << (MAX_PHYSMEM_BITS - PAGE_SHIFT);
		unsigned long pfn = res->start >> PAGE_SHIFT;

		if (pfn > limit) {
			pr_err("New System RAM resource outside addressable RAM (%lu > %lu)\n",
			       pfn, limit);
			release_memory_resource(res);
			return NULL;
		}
	}
#endif

	return res;
}

static enum bp_state reserve_additional_memory(void)
{
	long credit;
	struct resource *resource;
	int nid, rc;
	unsigned long balloon_hotplug;

	credit = balloon_stats.target_pages + balloon_stats.target_unpopulated
		- balloon_stats.total_pages;

	/*
	 * Already hotplugged enough pages?  Wait for them to be
	 * onlined.
	 */
	if (credit <= 0)
		return BP_WAIT;

	balloon_hotplug = round_up(credit, PAGES_PER_SECTION);

	resource = additional_memory_resource(balloon_hotplug * PAGE_SIZE);
	if (!resource)
		goto err;

	nid = memory_add_physaddr_to_nid(resource->start);

#ifdef CONFIG_XEN_HAVE_PVMMU
	/*
	 * We don't support PV MMU when Linux and Xen is using
	 * different page granularity.
	 */
	BUILD_BUG_ON(XEN_PAGE_SIZE != PAGE_SIZE);

        /*
         * add_memory() will build page tables for the new memory so
         * the p2m must contain invalid entries so the correct
         * non-present PTEs will be written.
         *
         * If a failure occurs, the original (identity) p2m entries
         * are not restored since this region is now known not to
         * conflict with any devices.
         */ 
	if (!xen_feature(XENFEAT_auto_translated_physmap)) {
		unsigned long pfn, i;

		pfn = PFN_DOWN(resource->start);
		for (i = 0; i < balloon_hotplug; i++) {
			if (!set_phys_to_machine(pfn + i, INVALID_P2M_ENTRY)) {
				pr_warn("set_phys_to_machine() failed, no memory added\n");
				goto err;
			}
                }
	}
#endif

	rc = add_memory_resource(nid, resource);
	if (rc) {
		pr_warn("Cannot add additional memory (%i)\n", rc);
		goto err;
	}

	balloon_stats.total_pages += balloon_hotplug;

	return BP_WAIT;
  err:
	release_memory_resource(resource);
	return BP_ECANCELED;
}

static void xen_online_page(struct page *page)
{
	__online_page_set_limits(page);

	mutex_lock(&balloon_mutex);

	__balloon_append(page);

	mutex_unlock(&balloon_mutex);
}

static int xen_memory_notifier(struct notifier_block *nb, unsigned long val, void *v)
{
	if (val == MEM_ONLINE)
		schedule_delayed_work(&balloon_worker, 0);

	return NOTIFY_OK;
}

static struct notifier_block xen_memory_nb = {
	.notifier_call = xen_memory_notifier,
	.priority = 0
};
#else
static enum bp_state reserve_additional_memory(void)
{
	balloon_stats.target_pages = balloon_stats.current_pages +
				     balloon_stats.target_unpopulated;
	return BP_ECANCELED;
}
#endif /* CONFIG_XEN_BALLOON_MEMORY_HOTPLUG */

static long current_credit(void)
{
	return balloon_stats.target_pages - balloon_stats.current_pages;
}

static bool balloon_is_inflated(void)
{
	return balloon_stats.balloon_low || balloon_stats.balloon_high;
}

static enum bp_state increase_reservation(unsigned long nr_pages)
{
	int rc;
	unsigned long i;
	struct page   *page;
	struct xen_memory_reservation reservation = {
		.address_bits = 0,
		.extent_order = EXTENT_ORDER,
		.domid        = DOMID_SELF
	};

	if (nr_pages > ARRAY_SIZE(frame_list))
		nr_pages = ARRAY_SIZE(frame_list);

	spin_lock_irqsave(&balloon_lock, flags);

	page = balloon_first_page();
	for (i = 0; i < nr_pages; i++) {
		BUG_ON(page == NULL);
		frame_list[i] = page_to_pfn(page);;
	page = list_first_entry_or_null(&ballooned_pages, struct page, lru);
	for (i = 0; i < nr_pages; i++) {
		if (!page) {
			nr_pages = i;
			break;
		}

		/* XENMEM_populate_physmap requires a PFN based on Xen
		 * granularity.
		 */
		frame_list[i] = page_to_xen_pfn(page);
		page = balloon_next_page(page);
	}

	set_xen_guest_handle(reservation.extent_start, frame_list);
	reservation.nr_extents   = nr_pages;
	rc = HYPERVISOR_memory_op(
		XENMEM_populate_physmap, &reservation);
	if (rc < nr_pages) {
		if (rc > 0) {
			int ret;

			/* We hit the Xen hard limit: reprobe. */
			reservation.nr_extents = rc;
			ret = HYPERVISOR_memory_op(XENMEM_decrease_reservation,
					&reservation);
			BUG_ON(ret != rc);
		}
		if (rc >= 0)
			balloon_stats.hard_limit = (balloon_stats.current_pages + rc -
						    balloon_stats.driver_pages);
		goto out;
	}

	for (i = 0; i < nr_pages; i++) {
		page = balloon_retrieve();
		BUG_ON(page == NULL);

		pfn = page_to_pfn(page);
		BUG_ON(!xen_feature(XENFEAT_auto_translated_physmap) &&
		       phys_to_machine_mapping_valid(pfn));

		set_phys_to_machine(pfn, frame_list[i]);

		/* Link back into the page tables if not highmem. */
		if (pfn < max_low_pfn) {
			int ret;
			ret = HYPERVISOR_update_va_mapping(
				(unsigned long)__va(pfn << PAGE_SHIFT),
				mfn_pte(frame_list[i], PAGE_KERNEL),
				0);
			BUG_ON(ret);
		}

		/* Relinquish the page back to the allocator. */
		ClearPageReserved(page);
		init_page_count(page);
		__free_page(page);
	}

	balloon_stats.current_pages += nr_pages;
	totalram_pages = balloon_stats.current_pages;

 out:
	spin_unlock_irqrestore(&balloon_lock, flags);

	return 0;
}

static int decrease_reservation(unsigned long nr_pages)
{
	unsigned long  pfn, i, flags;
	struct page   *page;
	int            need_sleep = 0;
	int ret;
	struct xen_memory_reservation reservation = {
		.address_bits = 0,
		.extent_order = 0,
		.domid        = DOMID_SELF
	};
	reservation.nr_extents = nr_pages;
	rc = HYPERVISOR_memory_op(XENMEM_populate_physmap, &reservation);
	if (rc <= 0)
		return BP_EAGAIN;

	for (i = 0; i < rc; i++) {
		page = balloon_retrieve(false);
		BUG_ON(page == NULL);

#ifdef CONFIG_XEN_HAVE_PVMMU
		/*
		 * We don't support PV MMU when Linux and Xen is using
		 * different page granularity.
		 */
		BUILD_BUG_ON(XEN_PAGE_SIZE != PAGE_SIZE);

		if (!xen_feature(XENFEAT_auto_translated_physmap)) {
			unsigned long pfn = page_to_pfn(page);

			set_phys_to_machine(pfn, frame_list[i]);

			/* Link back into the page tables if not highmem. */
			if (!PageHighMem(page)) {
				int ret;
				ret = HYPERVISOR_update_va_mapping(
						(unsigned long)__va(pfn << PAGE_SHIFT),
						mfn_pte(frame_list[i], PAGE_KERNEL),
						0);
				BUG_ON(ret);
			}
		}
#endif

		/* Relinquish the page back to the allocator. */
		__free_reserved_page(page);
	}

	balloon_stats.current_pages += rc;

	return BP_DONE;
}

static enum bp_state decrease_reservation(unsigned long nr_pages, gfp_t gfp)
{
	enum bp_state state = BP_DONE;
	unsigned long i;
	struct page *page, *tmp;
	int ret;
	struct xen_memory_reservation reservation = {
		.address_bits = 0,
		.extent_order = EXTENT_ORDER,
		.domid        = DOMID_SELF
	};
	LIST_HEAD(pages);

	if (nr_pages > ARRAY_SIZE(frame_list))
		nr_pages = ARRAY_SIZE(frame_list);

	for (i = 0; i < nr_pages; i++) {
		if ((page = alloc_page(GFP_BALLOON)) == NULL) {
			nr_pages = i;
			need_sleep = 1;
			break;
		}

		pfn = page_to_pfn(page);
		frame_list[i] = pfn_to_mfn(pfn);

		scrub_page(page);
	}

	/* Ensure that ballooned highmem pages don't have kmaps. */
	kmap_flush_unused();
	flush_tlb_all();

	spin_lock_irqsave(&balloon_lock, flags);

	/* No more mappings: invalidate P2M and add to balloon. */
	for (i = 0; i < nr_pages; i++) {
		pfn = mfn_to_pfn(frame_list[i]);
		set_phys_to_machine(pfn, INVALID_P2M_ENTRY);
		balloon_append(pfn_to_page(pfn));
	}

		page = alloc_page(gfp);
		if (page == NULL) {
			nr_pages = i;
			state = BP_EAGAIN;
			break;
		}
		scrub_page(page);
		list_add(&page->lru, &pages);
	}

	/*
	 * Ensure that ballooned highmem pages don't have kmaps.
	 *
	 * Do this before changing the p2m as kmap_flush_unused()
	 * reads PTEs to obtain pages (and hence needs the original
	 * p2m entry).
	 */
	kmap_flush_unused();

	/*
	 * Setup the frame, update direct mapping, invalidate P2M,
	 * and add to balloon.
	 */
	i = 0;
	list_for_each_entry_safe(page, tmp, &pages, lru) {
		/* XENMEM_decrease_reservation requires a GFN */
		frame_list[i++] = xen_page_to_gfn(page);

#ifdef CONFIG_XEN_HAVE_PVMMU
		/*
		 * We don't support PV MMU when Linux and Xen is using
		 * different page granularity.
		 */
		BUILD_BUG_ON(XEN_PAGE_SIZE != PAGE_SIZE);

		if (!xen_feature(XENFEAT_auto_translated_physmap)) {
			unsigned long pfn = page_to_pfn(page);

			if (!PageHighMem(page)) {
				ret = HYPERVISOR_update_va_mapping(
						(unsigned long)__va(pfn << PAGE_SHIFT),
						__pte_ma(0), 0);
				BUG_ON(ret);
			}
			__set_phys_to_machine(pfn, INVALID_P2M_ENTRY);
		}
#endif
		list_del(&page->lru);

		balloon_append(page);
	}

	flush_tlb_all();

	set_xen_guest_handle(reservation.extent_start, frame_list);
	reservation.nr_extents   = nr_pages;
	ret = HYPERVISOR_memory_op(XENMEM_decrease_reservation, &reservation);
	BUG_ON(ret != nr_pages);

	balloon_stats.current_pages -= nr_pages;
	totalram_pages = balloon_stats.current_pages;

	spin_unlock_irqrestore(&balloon_lock, flags);

	return need_sleep;
}

/*
 * We avoid multiple worker processes conflicting via the balloon mutex.

	return state;
}

/*
 * As this is a work item it is guaranteed to run as a single instance only.
 * We may of course race updates of the target counts (which are protected
 * by the balloon lock), or with changes to the Xen hard limit, but we will
 * recover from these in time.
 */
static void balloon_process(struct work_struct *work)
{
	int need_sleep = 0;
	long credit;

	mutex_lock(&balloon_mutex);

	do {
		credit = current_target() - balloon_stats.current_pages;
		if (credit > 0)
			need_sleep = (increase_reservation(credit) != 0);
		if (credit < 0)
			need_sleep = (decrease_reservation(-credit) != 0);

#ifndef CONFIG_PREEMPT
		if (need_resched())
			schedule();
#endif
	} while ((credit != 0) && !need_sleep);

	/* Schedule more work if there is some still to be done. */
	if (current_target() != balloon_stats.current_pages)
		mod_timer(&balloon_timer, jiffies + HZ);

	mutex_unlock(&balloon_mutex);
}

/* Resets the Xen limit, sets new target, and kicks off processing. */
static void balloon_set_new_target(unsigned long target)
{
	/* No need for lock. Not read-modify-write updates. */
	balloon_stats.hard_limit   = ~0UL;
	balloon_stats.target_pages = target;
	schedule_work(&balloon_worker);
}

static struct xenbus_watch target_watch =
{
	.node = "memory/target"
};

/* React to a change in the target key */
static void watch_target(struct xenbus_watch *watch,
			 const char **vec, unsigned int len)
{
	unsigned long long new_target;
	int err;

	err = xenbus_scanf(XBT_NIL, "memory", "target", "%llu", &new_target);
	if (err != 1) {
		/* This is ok (for domain0 at least) - so just return */
		return;
	}

	/* The given memory/target value is in KiB, so it needs converting to
	 * pages. PAGE_SHIFT converts bytes to pages, hence PAGE_SHIFT - 10.
	 */
	balloon_set_new_target(new_target >> (PAGE_SHIFT - 10));
}

static int balloon_init_watcher(struct notifier_block *notifier,
				unsigned long event,
				void *data)
{
	int err;

	err = register_xenbus_watch(&target_watch);
	if (err)
		printk(KERN_ERR "Failed to set balloon watcher\n");

	return NOTIFY_DONE;
}

static struct notifier_block xenstore_notifier;

static int __init balloon_init(void)
{
	unsigned long pfn;
	struct page *page;

	if (!is_running_on_xen())
		return -ENODEV;

	pr_info("xen_balloon: Initialising balloon driver.\n");

	balloon_stats.current_pages = min(xen_start_info->nr_pages, max_pfn);
	totalram_pages   = balloon_stats.current_pages;
	balloon_stats.target_pages  = balloon_stats.current_pages;
	balloon_stats.balloon_low   = 0;
	balloon_stats.balloon_high  = 0;
	balloon_stats.driver_pages  = 0UL;
	balloon_stats.hard_limit    = ~0UL;

	init_timer(&balloon_timer);
	balloon_timer.data = 0;
	balloon_timer.function = balloon_alarm;

	register_balloon(&balloon_sysdev);

	/* Initialise the balloon with excess memory space. */
	for (pfn = xen_start_info->nr_pages; pfn < max_pfn; pfn++) {
		page = pfn_to_page(pfn);
		if (!PageReserved(page))
			balloon_append(page);
	}

	target_watch.callback = watch_target;
	xenstore_notifier.notifier_call = balloon_init_watcher;

	register_xenstore_notifier(&xenstore_notifier);
	enum bp_state state = BP_DONE;
	long credit;


	do {
		mutex_lock(&balloon_mutex);

		credit = current_credit();

		if (credit > 0) {
			if (balloon_is_inflated())
				state = increase_reservation(credit);
			else
				state = reserve_additional_memory();
		}

		if (credit < 0) {
			long n_pages;

			n_pages = min(-credit, si_mem_available());
			state = decrease_reservation(n_pages, GFP_BALLOON);
			if (state == BP_DONE && n_pages != -credit &&
			    n_pages < totalreserve_pages)
				state = BP_EAGAIN;
		}

		state = update_schedule(state);

		mutex_unlock(&balloon_mutex);

		cond_resched();

	} while (credit && state == BP_DONE);

	/* Schedule more work if there is some still to be done. */
	if (state == BP_EAGAIN)
		schedule_delayed_work(&balloon_worker, balloon_stats.schedule_delay * HZ);
}

/* Resets the Xen limit, sets new target, and kicks off processing. */
void balloon_set_new_target(unsigned long target)
{
	/* No need for lock. Not read-modify-write updates. */
	balloon_stats.target_pages = target;
	schedule_delayed_work(&balloon_worker, 0);
}
EXPORT_SYMBOL_GPL(balloon_set_new_target);

static int add_ballooned_pages(int nr_pages)
{
	enum bp_state st;

	if (xen_hotplug_unpopulated) {
		st = reserve_additional_memory();
		if (st != BP_ECANCELED) {
			int rc;

			mutex_unlock(&balloon_mutex);
			rc = wait_event_interruptible(balloon_wq,
				   !list_empty(&ballooned_pages));
			mutex_lock(&balloon_mutex);
			return rc ? -ENOMEM : 0;
		}
	}

	if (si_mem_available() < nr_pages)
		return -ENOMEM;

	st = decrease_reservation(nr_pages, GFP_USER);
	if (st != BP_DONE)
		return -ENOMEM;

	return 0;
}

/**
 * alloc_xenballooned_pages - get pages that have been ballooned out
 * @nr_pages: Number of pages to get
 * @pages: pages returned
 * @return 0 on success, error otherwise
 */
int alloc_xenballooned_pages(int nr_pages, struct page **pages)
{
	int pgno = 0;
	struct page *page;
	int ret;

	mutex_lock(&balloon_mutex);

	balloon_stats.target_unpopulated += nr_pages;

	while (pgno < nr_pages) {
		page = balloon_retrieve(true);
		if (page) {
			pages[pgno++] = page;
#ifdef CONFIG_XEN_HAVE_PVMMU
			/*
			 * We don't support PV MMU when Linux and Xen is using
			 * different page granularity.
			 */
			BUILD_BUG_ON(XEN_PAGE_SIZE != PAGE_SIZE);

			ret = xen_alloc_p2m_entry(page_to_pfn(page));
			if (ret < 0)
				goto out_undo;
#endif
		} else {
			ret = add_ballooned_pages(nr_pages - pgno);
			if (ret < 0)
				goto out_undo;
		}
	}
	mutex_unlock(&balloon_mutex);
	return 0;
 out_undo:
	mutex_unlock(&balloon_mutex);
	free_xenballooned_pages(pgno, pages);
	/*
	 * NB: free_xenballooned_pages will only subtract pgno pages, but since
	 * target_unpopulated is incremented with nr_pages at the start we need
	 * to remove the remaining ones also, or accounting will be screwed.
	 */
	balloon_stats.target_unpopulated -= nr_pages - pgno;
	return ret;
}
EXPORT_SYMBOL(alloc_xenballooned_pages);

/**
 * free_xenballooned_pages - return pages retrieved with get_ballooned_pages
 * @nr_pages: Number of pages
 * @pages: pages to return
 */
void free_xenballooned_pages(int nr_pages, struct page **pages)
{
	int i;

	mutex_lock(&balloon_mutex);

	for (i = 0; i < nr_pages; i++) {
		if (pages[i])
			balloon_append(pages[i]);
	}

	balloon_stats.target_unpopulated -= nr_pages;

	/* The balloon may be too large now. Shrink it if needed. */
	if (current_credit())
		schedule_delayed_work(&balloon_worker, 0);

	mutex_unlock(&balloon_mutex);
}
EXPORT_SYMBOL(free_xenballooned_pages);

static void __init balloon_add_region(unsigned long start_pfn,
				      unsigned long pages)
{
	unsigned long pfn, extra_pfn_end;
	struct page *page;

	/*
	 * If the amount of usable memory has been limited (e.g., with
	 * the 'mem' command line parameter), don't add pages beyond
	 * this limit.
	 */
	extra_pfn_end = min(max_pfn, start_pfn + pages);

	for (pfn = start_pfn; pfn < extra_pfn_end; pfn++) {
		page = pfn_to_page(pfn);
		/* totalram_pages and totalhigh_pages do not
		   include the boot-time balloon extension, so
		   don't subtract from it. */
		__balloon_append(page);
	}

	balloon_stats.total_pages += extra_pfn_end - start_pfn;
}

static int __init balloon_init(void)
{
	int i;

	if (!xen_domain())
		return -ENODEV;

	pr_info("Initialising balloon driver\n");

	balloon_stats.current_pages = xen_pv_domain()
		? min(xen_start_info->nr_pages - xen_released_pages, max_pfn)
		: get_num_physpages();
	balloon_stats.target_pages  = balloon_stats.current_pages;
	balloon_stats.balloon_low   = 0;
	balloon_stats.balloon_high  = 0;
	balloon_stats.total_pages   = balloon_stats.current_pages;

	balloon_stats.schedule_delay = 1;
	balloon_stats.max_schedule_delay = 32;
	balloon_stats.retry_count = 1;
	balloon_stats.max_retry_count = 4;

#ifdef CONFIG_XEN_BALLOON_MEMORY_HOTPLUG
	set_online_page_callback(&xen_online_page);
	register_memory_notifier(&xen_memory_nb);
	register_sysctl_table(xen_root);
#endif

	/*
	 * Initialize the balloon with pages from the extra memory
	 * regions (see arch/x86/xen/setup.c).
	 */
	for (i = 0; i < XEN_EXTRA_MEM_MAX_REGIONS; i++)
		if (xen_extra_mem[i].n_pfns)
			balloon_add_region(xen_extra_mem[i].start_pfn,
					   xen_extra_mem[i].n_pfns);

	return 0;
}

subsys_initcall(balloon_init);

static void balloon_exit(void)
{
    /* XXX - release balloon here */
    return;
}

module_exit(balloon_exit);

static void balloon_update_driver_allowance(long delta)
{
	unsigned long flags;

	spin_lock_irqsave(&balloon_lock, flags);
	balloon_stats.driver_pages += delta;
	spin_unlock_irqrestore(&balloon_lock, flags);
}

static int dealloc_pte_fn(
	pte_t *pte, struct page *pmd_page, unsigned long addr, void *data)
{
	unsigned long mfn = pte_mfn(*pte);
	int ret;
	struct xen_memory_reservation reservation = {
		.nr_extents   = 1,
		.extent_order = 0,
		.domid        = DOMID_SELF
	};
	set_xen_guest_handle(reservation.extent_start, &mfn);
	set_pte_at(&init_mm, addr, pte, __pte_ma(0ull));
	set_phys_to_machine(__pa(addr) >> PAGE_SHIFT, INVALID_P2M_ENTRY);
	ret = HYPERVISOR_memory_op(XENMEM_decrease_reservation, &reservation);
	BUG_ON(ret != 1);
	return 0;
}

static struct page **alloc_empty_pages_and_pagevec(int nr_pages)
{
	unsigned long vaddr, flags;
	struct page *page, **pagevec;
	int i, ret;

	pagevec = kmalloc(sizeof(page) * nr_pages, GFP_KERNEL);
	if (pagevec == NULL)
		return NULL;

	for (i = 0; i < nr_pages; i++) {
		page = pagevec[i] = alloc_page(GFP_KERNEL);
		if (page == NULL)
			goto err;

		vaddr = (unsigned long)page_address(page);

		scrub_page(page);

		spin_lock_irqsave(&balloon_lock, flags);

		if (xen_feature(XENFEAT_auto_translated_physmap)) {
			unsigned long gmfn = page_to_pfn(page);
			struct xen_memory_reservation reservation = {
				.nr_extents   = 1,
				.extent_order = 0,
				.domid        = DOMID_SELF
			};
			set_xen_guest_handle(reservation.extent_start, &gmfn);
			ret = HYPERVISOR_memory_op(XENMEM_decrease_reservation,
						   &reservation);
			if (ret == 1)
				ret = 0; /* success */
		} else {
			ret = apply_to_page_range(&init_mm, vaddr, PAGE_SIZE,
						  dealloc_pte_fn, NULL);
		}

		if (ret != 0) {
			spin_unlock_irqrestore(&balloon_lock, flags);
			__free_page(page);
			goto err;
		}

		totalram_pages = --balloon_stats.current_pages;

		spin_unlock_irqrestore(&balloon_lock, flags);
	}

 out:
	schedule_work(&balloon_worker);
	flush_tlb_all();
	return pagevec;

 err:
	spin_lock_irqsave(&balloon_lock, flags);
	while (--i >= 0)
		balloon_append(pagevec[i]);
	spin_unlock_irqrestore(&balloon_lock, flags);
	kfree(pagevec);
	pagevec = NULL;
	goto out;
}

static void free_empty_pages_and_pagevec(struct page **pagevec, int nr_pages)
{
	unsigned long flags;
	int i;

	if (pagevec == NULL)
		return;

	spin_lock_irqsave(&balloon_lock, flags);
	for (i = 0; i < nr_pages; i++) {
		BUG_ON(page_count(pagevec[i]) != 1);
		balloon_append(pagevec[i]);
	}
	spin_unlock_irqrestore(&balloon_lock, flags);

	kfree(pagevec);

	schedule_work(&balloon_worker);
}

static void balloon_release_driver_page(struct page *page)
{
	unsigned long flags;

	spin_lock_irqsave(&balloon_lock, flags);
	balloon_append(page);
	balloon_stats.driver_pages--;
	spin_unlock_irqrestore(&balloon_lock, flags);

	schedule_work(&balloon_worker);
}


#define BALLOON_SHOW(name, format, args...)			\
	static ssize_t show_##name(struct sys_device *dev,	\
				   char *buf)			\
	{							\
		return sprintf(buf, format, ##args);		\
	}							\
	static SYSDEV_ATTR(name, S_IRUGO, show_##name, NULL)

BALLOON_SHOW(current_kb, "%lu\n", PAGES2KB(balloon_stats.current_pages));
BALLOON_SHOW(low_kb, "%lu\n", PAGES2KB(balloon_stats.balloon_low));
BALLOON_SHOW(high_kb, "%lu\n", PAGES2KB(balloon_stats.balloon_high));
BALLOON_SHOW(hard_limit_kb,
	     (balloon_stats.hard_limit!=~0UL) ? "%lu\n" : "???\n",
	     (balloon_stats.hard_limit!=~0UL) ? PAGES2KB(balloon_stats.hard_limit) : 0);
BALLOON_SHOW(driver_kb, "%lu\n", PAGES2KB(balloon_stats.driver_pages));

static ssize_t show_target_kb(struct sys_device *dev, char *buf)
{
	return sprintf(buf, "%lu\n", PAGES2KB(balloon_stats.target_pages));
}

static ssize_t store_target_kb(struct sys_device *dev,
			       struct sysdev_attribute *attr,
			       const char *buf,
			       size_t count)
{
	char memstring[64], *endchar;
	unsigned long long target_bytes;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (count <= 1)
		return -EBADMSG; /* runt */
	if (count > sizeof(memstring))
		return -EFBIG;   /* too long */
	strcpy(memstring, buf);

	target_bytes = memparse(memstring, &endchar);
	balloon_set_new_target(target_bytes >> PAGE_SHIFT);

	return count;
}

static SYSDEV_ATTR(target_kb, S_IRUGO | S_IWUSR,
		   show_target_kb, store_target_kb);

static struct sysdev_attribute *balloon_attrs[] = {
	&attr_target_kb,
};

static struct attribute *balloon_info_attrs[] = {
	&attr_current_kb.attr,
	&attr_low_kb.attr,
	&attr_high_kb.attr,
	&attr_hard_limit_kb.attr,
	&attr_driver_kb.attr,
	NULL
};

static struct attribute_group balloon_info_group = {
	.name = "info",
	.attrs = balloon_info_attrs,
};

static struct sysdev_class balloon_sysdev_class = {
	.name = BALLOON_CLASS_NAME,
};

static int register_balloon(struct sys_device *sysdev)
{
	int i, error;

	error = sysdev_class_register(&balloon_sysdev_class);
	if (error)
		return error;

	sysdev->id = 0;
	sysdev->cls = &balloon_sysdev_class;

	error = sysdev_register(sysdev);
	if (error) {
		sysdev_class_unregister(&balloon_sysdev_class);
		return error;
	}

	for (i = 0; i < ARRAY_SIZE(balloon_attrs); i++) {
		error = sysdev_create_file(sysdev, balloon_attrs[i]);
		if (error)
			goto fail;
	}

	error = sysfs_create_group(&sysdev->kobj, &balloon_info_group);
	if (error)
		goto fail;

	return 0;

 fail:
	while (--i >= 0)
		sysdev_remove_file(sysdev, balloon_attrs[i]);
	sysdev_unregister(sysdev);
	sysdev_class_unregister(&balloon_sysdev_class);
	return error;
}

static void unregister_balloon(struct sys_device *sysdev)
{
	int i;

	sysfs_remove_group(&sysdev->kobj, &balloon_info_group);
	for (i = 0; i < ARRAY_SIZE(balloon_attrs); i++)
		sysdev_remove_file(sysdev, balloon_attrs[i]);
	sysdev_unregister(sysdev);
	sysdev_class_unregister(&balloon_sysdev_class);
}

static void balloon_sysfs_exit(void)
{
	unregister_balloon(&balloon_sysdev);
}

MODULE_LICENSE("GPL");
