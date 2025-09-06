/*
 * drivers/power/process.c - Functions for saving/restoring console.
 * Functions for saving/restoring console.
 *
 * Originally from swsusp.
 */

#include <linux/vt_kern.h>
#include <linux/kbd_kern.h>
#include <linux/console.h>
#include <linux/module.h>
#include "power.h"

#if defined(CONFIG_VT) && defined(CONFIG_VT_CONSOLE)
#define SUSPEND_CONSOLE	(MAX_NR_CONSOLES-1)

static int orig_fgconsole, orig_kmsg;
static int disable_vt_switch;

/*
 * Normally during a suspend, we allocate a new console and switch to it.
 * When we resume, we switch back to the original console.  This switch
 * can be slow, so on systems where the framebuffer can handle restoration
 * of video registers anyways, there's little point in doing the console
 * switch.  This function allows you to disable it by passing it '0'.
 */
void pm_set_vt_switch(int do_switch)
{
	acquire_console_sem();
	disable_vt_switch = !do_switch;
	release_console_sem();
}
EXPORT_SYMBOL(pm_set_vt_switch);

int pm_prepare_console(void)
{
	acquire_console_sem();

	if (disable_vt_switch) {
		release_console_sem();
		return 0;
	}

	orig_fgconsole = fg_console;

	if (vc_allocate(SUSPEND_CONSOLE)) {
	  /* we can't have a free VC for now. Too bad,
	   * we don't want to mess the screen for now. */
		release_console_sem();
		return 1;
	}

	if (set_console(SUSPEND_CONSOLE)) {
		/*
		 * We're unable to switch to the SUSPEND_CONSOLE.
		 * Let the calling function know so it can decide
		 * what to do.
		 */
		release_console_sem();
		return 1;
	}
	release_console_sem();

	if (vt_waitactive(SUSPEND_CONSOLE)) {
		pr_debug("Suspend: Can't switch VCs.");
		return 1;
	}
	orig_kmsg = kmsg_redirect;
	kmsg_redirect = SUSPEND_CONSOLE;
#include <linux/console.h>
#include <linux/vt_kern.h>
#include <linux/kbd_kern.h>
#include <linux/vt.h>
#include <linux/module.h>
#include <linux/slab.h>
#include "power.h"

#define SUSPEND_CONSOLE	(MAX_NR_CONSOLES-1)

static int orig_fgconsole, orig_kmsg;

static DEFINE_MUTEX(vt_switch_mutex);

struct pm_vt_switch {
	struct list_head head;
	struct device *dev;
	bool required;
};

static LIST_HEAD(pm_vt_switch_list);


/**
 * pm_vt_switch_required - indicate VT switch at suspend requirements
 * @dev: device
 * @required: if true, caller needs VT switch at suspend/resume time
 *
 * The different console drivers may or may not require VT switches across
 * suspend/resume, depending on how they handle restoring video state and
 * what may be running.
 *
 * Drivers can indicate support for switchless suspend/resume, which can
 * save time and flicker, by using this routine and passing 'false' as
 * the argument.  If any loaded driver needs VT switching, or the
 * no_console_suspend argument has been passed on the command line, VT
 * switches will occur.
 */
void pm_vt_switch_required(struct device *dev, bool required)
{
	struct pm_vt_switch *entry, *tmp;

	mutex_lock(&vt_switch_mutex);
	list_for_each_entry(tmp, &pm_vt_switch_list, head) {
		if (tmp->dev == dev) {
			/* already registered, update requirement */
			tmp->required = required;
			goto out;
		}
	}

	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		goto out;

	entry->required = required;
	entry->dev = dev;

	list_add(&entry->head, &pm_vt_switch_list);
out:
	mutex_unlock(&vt_switch_mutex);
}
EXPORT_SYMBOL(pm_vt_switch_required);

/**
 * pm_vt_switch_unregister - stop tracking a device's VT switching needs
 * @dev: device
 *
 * Remove @dev from the vt switch list.
 */
void pm_vt_switch_unregister(struct device *dev)
{
	struct pm_vt_switch *tmp;

	mutex_lock(&vt_switch_mutex);
	list_for_each_entry(tmp, &pm_vt_switch_list, head) {
		if (tmp->dev == dev) {
			list_del(&tmp->head);
			kfree(tmp);
			break;
		}
	}
	mutex_unlock(&vt_switch_mutex);
}
EXPORT_SYMBOL(pm_vt_switch_unregister);

/*
 * There are three cases when a VT switch on suspend/resume are required:
 *   1) no driver has indicated a requirement one way or another, so preserve
 *      the old behavior
 *   2) console suspend is disabled, we want to see debug messages across
 *      suspend/resume
 *   3) any registered driver indicates it needs a VT switch
 *
 * If none of these conditions is present, meaning we have at least one driver
 * that doesn't need the switch, and none that do, we can avoid it to make
 * resume look a little prettier (and suspend too, but that's usually hidden,
 * e.g. when closing the lid on a laptop).
 */
static bool pm_vt_switch(void)
{
	struct pm_vt_switch *entry;
	bool ret = true;

	mutex_lock(&vt_switch_mutex);
	if (list_empty(&pm_vt_switch_list))
		goto out;

	if (!console_suspend_enabled)
		goto out;

	list_for_each_entry(entry, &pm_vt_switch_list, head) {
		if (entry->required)
			goto out;
	}

	ret = false;
out:
	mutex_unlock(&vt_switch_mutex);
	return ret;
}

int pm_prepare_console(void)
{
	if (!pm_vt_switch())
		return 0;

	orig_fgconsole = vt_move_to_console(SUSPEND_CONSOLE, 1);
	if (orig_fgconsole < 0)
		return 1;

	orig_kmsg = vt_kmsg_redirect(SUSPEND_CONSOLE);
	return 0;
}

void pm_restore_console(void)
{
	acquire_console_sem();
	if (disable_vt_switch) {
		release_console_sem();
		return;
	}
	set_console(orig_fgconsole);
	release_console_sem();
	kmsg_redirect = orig_kmsg;
}
#endif
	if (!pm_vt_switch())
		return;

	if (orig_fgconsole >= 0) {
		vt_move_to_console(orig_fgconsole, 0);
		vt_kmsg_redirect(orig_kmsg);
	}
}
