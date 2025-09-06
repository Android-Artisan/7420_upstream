/*
 * edac_mc kernel module
 * (C) 2005-2007 Linux Networx (http://lnxi.com)
 *
 * This file may be distributed under the terms of the
 * GNU General Public License.
 *
 * Written Doug Thompson <norsk5@xmission.com> www.softwarebitmaker.com
 *
 */

#include <linux/ctype.h>
#include <linux/bug.h>
 * (c) 2012-2013 - Mauro Carvalho Chehab
 *	The entire API were re-written, and ported to use struct device
 *
 */

#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/edac.h>
#include <linux/bug.h>
#include <linux/pm_runtime.h>
#include <linux/uaccess.h>

#include "edac_core.h"
#include "edac_module.h"


/* MC EDAC Controls, setable by module parameter, and sysfs */
static int edac_mc_log_ue = 1;
static int edac_mc_log_ce = 1;
static int edac_mc_panic_on_ue;
static unsigned int edac_mc_poll_msec = 1000;

/* Getter functions for above */
int edac_mc_get_log_ue(void)
{
	return edac_mc_log_ue;
}

int edac_mc_get_log_ce(void)
{
	return edac_mc_log_ce;
}

int edac_mc_get_panic_on_ue(void)
{
	return edac_mc_panic_on_ue;
}

/* this is temporary */
unsigned int edac_mc_get_poll_msec(void)
{
	return edac_mc_poll_msec;
}

static int edac_set_poll_msec(const char *val, struct kernel_param *kp)
{
	long l;
	unsigned long l;
	unsigned int i;
	int ret;

	if (!val)
		return -EINVAL;

	ret = strict_strtol(val, 0, &l);
	if (ret == -EINVAL || ((int)l != l))
		return -EINVAL;
	*((int *)kp->arg) = l;
	ret = kstrtoul(val, 0, &l);
	ret = kstrtouint(val, 0, &i);
	if (ret)
		return ret;

	if (i < 1000)
		return -EINVAL;

	*((unsigned int *)kp->arg) = i;

	/* notify edac_mc engine to reset the poll period */
	edac_mc_reset_delay_period(i);

	return 0;
}

/* Parameter declarations for above */
module_param(edac_mc_panic_on_ue, int, 0644);
MODULE_PARM_DESC(edac_mc_panic_on_ue, "Panic on uncorrected error: 0=off 1=on");
module_param(edac_mc_log_ue, int, 0644);
MODULE_PARM_DESC(edac_mc_log_ue,
		 "Log uncorrectable error to console: 0=off 1=on");
module_param(edac_mc_log_ce, int, 0644);
MODULE_PARM_DESC(edac_mc_log_ce,
		 "Log correctable error to console: 0=off 1=on");
module_param_call(edac_mc_poll_msec, edac_set_poll_msec, param_get_uint,
		  &edac_mc_poll_msec, 0644);
MODULE_PARM_DESC(edac_mc_poll_msec, "Polling period in milliseconds");

/*
 * various constants for Memory Controllers
 */
static const char *mem_types[] = {
static struct device *mci_pdev;

/*
 * various constants for Memory Controllers
 */
static const char * const mem_types[] = {
	[MEM_EMPTY] = "Empty",
	[MEM_RESERVED] = "Reserved",
	[MEM_UNKNOWN] = "Unknown",
	[MEM_FPM] = "FPM",
	[MEM_EDO] = "EDO",
	[MEM_BEDO] = "BEDO",
	[MEM_SDR] = "Unbuffered-SDR",
	[MEM_RDR] = "Registered-SDR",
	[MEM_DDR] = "Unbuffered-DDR",
	[MEM_RDDR] = "Registered-DDR",
	[MEM_RMBS] = "RMBS",
	[MEM_DDR2] = "Unbuffered-DDR2",
	[MEM_FB_DDR2] = "FullyBuffered-DDR2",
	[MEM_RDDR2] = "Registered-DDR2",
	[MEM_XDR] = "XDR"
};

static const char *dev_types[] = {
	[MEM_XDR] = "XDR",
	[MEM_DDR3] = "Unbuffered-DDR3",
	[MEM_RDDR3] = "Registered-DDR3",
	[MEM_DDR4] = "Unbuffered-DDR4",
	[MEM_RDDR4] = "Registered-DDR4"
};

static const char * const dev_types[] = {
	[DEV_UNKNOWN] = "Unknown",
	[DEV_X1] = "x1",
	[DEV_X2] = "x2",
	[DEV_X4] = "x4",
	[DEV_X8] = "x8",
	[DEV_X16] = "x16",
	[DEV_X32] = "x32",
	[DEV_X64] = "x64"
};

static const char *edac_caps[] = {
static const char * const edac_caps[] = {
	[EDAC_UNKNOWN] = "Unknown",
	[EDAC_NONE] = "None",
	[EDAC_RESERVED] = "Reserved",
	[EDAC_PARITY] = "PARITY",
	[EDAC_EC] = "EC",
	[EDAC_SECDED] = "SECDED",
	[EDAC_S2ECD2ED] = "S2ECD2ED",
	[EDAC_S4ECD4ED] = "S4ECD4ED",
	[EDAC_S8ECD8ED] = "S8ECD8ED",
	[EDAC_S16ECD16ED] = "S16ECD16ED"
};



static ssize_t memctrl_int_store(void *ptr, const char *buffer, size_t count)
{
	int *value = (int *)ptr;

	if (isdigit(*buffer))
		*value = simple_strtoul(buffer, NULL, 0);
#ifdef CONFIG_EDAC_LEGACY_SYSFS
/*
 * EDAC sysfs CSROW data structures and methods
 */

#define to_csrow(k) container_of(k, struct csrow_info, dev)

/*
 * We need it to avoid namespace conflicts between the legacy API
 * and the per-dimm/per-rank one
 */
#define DEVICE_ATTR_LEGACY(_name, _mode, _show, _store) \
	static struct device_attribute dev_attr_legacy_##_name = __ATTR(_name, _mode, _show, _store)

struct dev_ch_attribute {
	struct device_attribute attr;
	int channel;
};

#define DEVICE_CHANNEL(_name, _mode, _show, _store, _var) \
	static struct dev_ch_attribute dev_attr_legacy_##_name = \
		{ __ATTR(_name, _mode, _show, _store), (_var) }

#define to_channel(k) (container_of(k, struct dev_ch_attribute, attr)->channel)

/* Set of more default csrow<id> attribute show/store functions */
static ssize_t csrow_ue_count_show(struct device *dev,
				   struct device_attribute *mattr, char *data)
{
	struct csrow_info *csrow = to_csrow(dev);

	return sprintf(data, "%u\n", csrow->ue_count);
}

static ssize_t csrow_ce_count_show(struct device *dev,
				   struct device_attribute *mattr, char *data)
{
	struct csrow_info *csrow = to_csrow(dev);

	return sprintf(data, "%u\n", csrow->ce_count);
}

static ssize_t csrow_size_show(struct device *dev,
			       struct device_attribute *mattr, char *data)
{
	struct csrow_info *csrow = to_csrow(dev);
	int i;
	u32 nr_pages = 0;

	for (i = 0; i < csrow->nr_channels; i++)
		nr_pages += csrow->channels[i]->dimm->nr_pages;
	return sprintf(data, "%u\n", PAGES_TO_MiB(nr_pages));
}

static ssize_t csrow_mem_type_show(struct device *dev,
				   struct device_attribute *mattr, char *data)
{
	struct csrow_info *csrow = to_csrow(dev);

	return sprintf(data, "%s\n", mem_types[csrow->channels[0]->dimm->mtype]);
}

static ssize_t csrow_dev_type_show(struct device *dev,
				   struct device_attribute *mattr, char *data)
{
	struct csrow_info *csrow = to_csrow(dev);

	return sprintf(data, "%s\n", dev_types[csrow->channels[0]->dimm->dtype]);
}

static ssize_t csrow_edac_mode_show(struct device *dev,
				    struct device_attribute *mattr,
				    char *data)
{
	struct csrow_info *csrow = to_csrow(dev);

	return sprintf(data, "%s\n", edac_caps[csrow->channels[0]->dimm->edac_mode]);
}

/* show/store functions for DIMM Label attributes */
static ssize_t channel_dimm_label_show(struct device *dev,
				       struct device_attribute *mattr,
				       char *data)
{
	struct csrow_info *csrow = to_csrow(dev);
	unsigned chan = to_channel(mattr);
	struct rank_info *rank = csrow->channels[chan];

	/* if field has not been initialized, there is nothing to send */
	if (!rank->dimm->label[0])
		return 0;

	return snprintf(data, sizeof(rank->dimm->label) + 1, "%s\n",
			rank->dimm->label);
}

static ssize_t channel_dimm_label_store(struct device *dev,
					struct device_attribute *mattr,
					const char *data, size_t count)
{
	struct csrow_info *csrow = to_csrow(dev);
	unsigned chan = to_channel(mattr);
	struct rank_info *rank = csrow->channels[chan];
	size_t copy_count = count;

	if (count == 0)
		return -EINVAL;

	if (data[count - 1] == '\0' || data[count - 1] == '\n')
		copy_count -= 1;

	if (copy_count == 0 || copy_count >= sizeof(rank->dimm->label))
		return -EINVAL;

	strncpy(rank->dimm->label, data, copy_count);
	rank->dimm->label[copy_count] = '\0';

	return count;
}


/* EDAC sysfs CSROW data structures and methods
 */

/* Set of more default csrow<id> attribute show/store functions */
static ssize_t csrow_ue_count_show(struct csrow_info *csrow, char *data,
				int private)
{
	return sprintf(data, "%u\n", csrow->ue_count);
}

static ssize_t csrow_ce_count_show(struct csrow_info *csrow, char *data,
				int private)
{
	return sprintf(data, "%u\n", csrow->ce_count);
}

static ssize_t csrow_size_show(struct csrow_info *csrow, char *data,
				int private)
{
	return sprintf(data, "%u\n", PAGES_TO_MiB(csrow->nr_pages));
}

static ssize_t csrow_mem_type_show(struct csrow_info *csrow, char *data,
				int private)
{
	return sprintf(data, "%s\n", mem_types[csrow->mtype]);
}

static ssize_t csrow_dev_type_show(struct csrow_info *csrow, char *data,
				int private)
{
	return sprintf(data, "%s\n", dev_types[csrow->dtype]);
}

static ssize_t csrow_edac_mode_show(struct csrow_info *csrow, char *data,
				int private)
{
	return sprintf(data, "%s\n", edac_caps[csrow->edac_mode]);
}

/* show/store functions for DIMM Label attributes */
static ssize_t channel_dimm_label_show(struct csrow_info *csrow,
				char *data, int channel)
{
	/* if field has not been initialized, there is nothing to send */
	if (!csrow->channels[channel].label[0])
		return 0;

	return snprintf(data, EDAC_MC_LABEL_LEN, "%s\n",
			csrow->channels[channel].label);
}

static ssize_t channel_dimm_label_store(struct csrow_info *csrow,
					const char *data,
					size_t count, int channel)
{
	ssize_t max_size = 0;

	max_size = min((ssize_t) count, (ssize_t) EDAC_MC_LABEL_LEN - 1);
	strncpy(csrow->channels[channel].label, data, max_size);
	csrow->channels[channel].label[max_size] = '\0';

	return max_size;
}

/* show function for dynamic chX_ce_count attribute */
static ssize_t channel_ce_count_show(struct csrow_info *csrow,
				char *data, int channel)
{
	return sprintf(data, "%u\n", csrow->channels[channel].ce_count);
}

/* csrow specific attribute structure */
struct csrowdev_attribute {
	struct attribute attr;
	 ssize_t(*show) (struct csrow_info *, char *, int);
	 ssize_t(*store) (struct csrow_info *, const char *, size_t, int);
	int private;
};

#define to_csrow(k) container_of(k, struct csrow_info, kobj)
#define to_csrowdev_attr(a) container_of(a, struct csrowdev_attribute, attr)

/* Set of show/store higher level functions for default csrow attributes */
static ssize_t csrowdev_show(struct kobject *kobj,
			struct attribute *attr, char *buffer)
{
	struct csrow_info *csrow = to_csrow(kobj);
	struct csrowdev_attribute *csrowdev_attr = to_csrowdev_attr(attr);

	if (csrowdev_attr->show)
		return csrowdev_attr->show(csrow,
					buffer, csrowdev_attr->private);
	return -EIO;
}

static ssize_t csrowdev_store(struct kobject *kobj, struct attribute *attr,
			const char *buffer, size_t count)
{
	struct csrow_info *csrow = to_csrow(kobj);
	struct csrowdev_attribute *csrowdev_attr = to_csrowdev_attr(attr);

	if (csrowdev_attr->store)
		return csrowdev_attr->store(csrow,
					buffer,
					count, csrowdev_attr->private);
	return -EIO;
}

static struct sysfs_ops csrowfs_ops = {
	.show = csrowdev_show,
	.store = csrowdev_store
};

#define CSROWDEV_ATTR(_name,_mode,_show,_store,_private)	\
static struct csrowdev_attribute attr_##_name = {			\
	.attr = {.name = __stringify(_name), .mode = _mode },	\
	.show   = _show,					\
	.store  = _store,					\
	.private = _private,					\
};

/* default cwrow<id>/attribute files */
CSROWDEV_ATTR(size_mb, S_IRUGO, csrow_size_show, NULL, 0);
CSROWDEV_ATTR(dev_type, S_IRUGO, csrow_dev_type_show, NULL, 0);
CSROWDEV_ATTR(mem_type, S_IRUGO, csrow_mem_type_show, NULL, 0);
CSROWDEV_ATTR(edac_mode, S_IRUGO, csrow_edac_mode_show, NULL, 0);
CSROWDEV_ATTR(ue_count, S_IRUGO, csrow_ue_count_show, NULL, 0);
CSROWDEV_ATTR(ce_count, S_IRUGO, csrow_ce_count_show, NULL, 0);

/* default attributes of the CSROW<id> object */
static struct csrowdev_attribute *default_csrow_attr[] = {
	&attr_dev_type,
	&attr_mem_type,
	&attr_edac_mode,
	&attr_size_mb,
	&attr_ue_count,
	&attr_ce_count,
	NULL,
};

/* possible dynamic channel DIMM Label attribute files */
CSROWDEV_ATTR(ch0_dimm_label, S_IRUGO | S_IWUSR,
	channel_dimm_label_show, channel_dimm_label_store, 0);
CSROWDEV_ATTR(ch1_dimm_label, S_IRUGO | S_IWUSR,
	channel_dimm_label_show, channel_dimm_label_store, 1);
CSROWDEV_ATTR(ch2_dimm_label, S_IRUGO | S_IWUSR,
	channel_dimm_label_show, channel_dimm_label_store, 2);
CSROWDEV_ATTR(ch3_dimm_label, S_IRUGO | S_IWUSR,
	channel_dimm_label_show, channel_dimm_label_store, 3);
CSROWDEV_ATTR(ch4_dimm_label, S_IRUGO | S_IWUSR,
	channel_dimm_label_show, channel_dimm_label_store, 4);
CSROWDEV_ATTR(ch5_dimm_label, S_IRUGO | S_IWUSR,
	channel_dimm_label_show, channel_dimm_label_store, 5);

/* Total possible dynamic DIMM Label attribute file table */
static struct csrowdev_attribute *dynamic_csrow_dimm_attr[] = {
	&attr_ch0_dimm_label,
	&attr_ch1_dimm_label,
	&attr_ch2_dimm_label,
	&attr_ch3_dimm_label,
	&attr_ch4_dimm_label,
	&attr_ch5_dimm_label
};

/* possible dynamic channel ce_count attribute files */
CSROWDEV_ATTR(ch0_ce_count, S_IRUGO | S_IWUSR, channel_ce_count_show, NULL, 0);
CSROWDEV_ATTR(ch1_ce_count, S_IRUGO | S_IWUSR, channel_ce_count_show, NULL, 1);
CSROWDEV_ATTR(ch2_ce_count, S_IRUGO | S_IWUSR, channel_ce_count_show, NULL, 2);
CSROWDEV_ATTR(ch3_ce_count, S_IRUGO | S_IWUSR, channel_ce_count_show, NULL, 3);
CSROWDEV_ATTR(ch4_ce_count, S_IRUGO | S_IWUSR, channel_ce_count_show, NULL, 4);
CSROWDEV_ATTR(ch5_ce_count, S_IRUGO | S_IWUSR, channel_ce_count_show, NULL, 5);

/* Total possible dynamic ce_count attribute file table */
static struct csrowdev_attribute *dynamic_csrow_ce_count_attr[] = {
	&attr_ch0_ce_count,
	&attr_ch1_ce_count,
	&attr_ch2_ce_count,
	&attr_ch3_ce_count,
	&attr_ch4_ce_count,
	&attr_ch5_ce_count
};

#define EDAC_NR_CHANNELS	6

/* Create dynamic CHANNEL files, indexed by 'chan',  under specifed CSROW */
static int edac_create_channel_files(struct kobject *kobj, int chan)
{
	int err = -ENODEV;

	if (chan >= EDAC_NR_CHANNELS)
		return err;

	/* create the DIMM label attribute file */
	err = sysfs_create_file(kobj,
				(struct attribute *)
				dynamic_csrow_dimm_attr[chan]);

	if (!err) {
		/* create the CE Count attribute file */
		err = sysfs_create_file(kobj,
					(struct attribute *)
					dynamic_csrow_ce_count_attr[chan]);
	} else {
		debugf1("%s()  dimm labels and ce_count files created",
			__func__);
	}

	return err;
}

/* No memory to release for this kobj */
static void edac_csrow_instance_release(struct kobject *kobj)
{
	struct mem_ctl_info *mci;
	struct csrow_info *cs;

	debugf1("%s()\n", __func__);

	cs = container_of(kobj, struct csrow_info, kobj);
	mci = cs->mci;

	kobject_put(&mci->edac_mci_kobj);
}

/* the kobj_type instance for a CSROW */
static struct kobj_type ktype_csrow = {
	.release = edac_csrow_instance_release,
	.sysfs_ops = &csrowfs_ops,
	.default_attrs = (struct attribute **)default_csrow_attr,
};

/* Create a CSROW object under specifed edac_mc_device */
static int edac_create_csrow_object(struct mem_ctl_info *mci,
					struct csrow_info *csrow, int index)
{
	struct kobject *kobj_mci = &mci->edac_mci_kobj;
	struct kobject *kobj;
	int chan;
	int err;

	/* generate ..../edac/mc/mc<id>/csrow<index>   */
	memset(&csrow->kobj, 0, sizeof(csrow->kobj));
	csrow->mci = mci;	/* include container up link */

	/* bump the mci instance's kobject's ref count */
	kobj = kobject_get(&mci->edac_mci_kobj);
	if (!kobj) {
		err = -ENODEV;
		goto err_out;
	}

	/* Instanstiate the csrow object */
	err = kobject_init_and_add(&csrow->kobj, &ktype_csrow, kobj_mci,
				   "csrow%d", index);
	if (err)
		goto err_release_top_kobj;

	/* At this point, to release a csrow kobj, one must
	 * call the kobject_put and allow that tear down
	 * to work the releasing
	 */

	/* Create the dyanmic attribute files on this csrow,
	 * namely, the DIMM labels and the channel ce_count
	 */
	for (chan = 0; chan < csrow->nr_channels; chan++) {
		err = edac_create_channel_files(&csrow->kobj, chan);
		if (err) {
			/* special case the unregister here */
			kobject_put(&csrow->kobj);
			goto err_out;
		}
	}
	kobject_uevent(&csrow->kobj, KOBJ_ADD);
	return 0;

	/* error unwind stack */
err_release_top_kobj:
	kobject_put(&mci->edac_mci_kobj);

err_out:
	return err;
}

/* default sysfs methods and data structures for the main MCI kobject */

static ssize_t mci_reset_counters_store(struct mem_ctl_info *mci,
					const char *data, size_t count)
{
	int row, chan;

	mci->ue_noinfo_count = 0;
	mci->ce_noinfo_count = 0;
	mci->ue_count = 0;
	mci->ce_count = 0;

	for (row = 0; row < mci->nr_csrows; row++) {
		struct csrow_info *ri = &mci->csrows[row];
/* show function for dynamic chX_ce_count attribute */
static ssize_t channel_ce_count_show(struct device *dev,
				     struct device_attribute *mattr, char *data)
{
	struct csrow_info *csrow = to_csrow(dev);
	unsigned chan = to_channel(mattr);
	struct rank_info *rank = csrow->channels[chan];

	return sprintf(data, "%u\n", rank->ce_count);
}

/* cwrow<id>/attribute files */
DEVICE_ATTR_LEGACY(size_mb, S_IRUGO, csrow_size_show, NULL);
DEVICE_ATTR_LEGACY(dev_type, S_IRUGO, csrow_dev_type_show, NULL);
DEVICE_ATTR_LEGACY(mem_type, S_IRUGO, csrow_mem_type_show, NULL);
DEVICE_ATTR_LEGACY(edac_mode, S_IRUGO, csrow_edac_mode_show, NULL);
DEVICE_ATTR_LEGACY(ue_count, S_IRUGO, csrow_ue_count_show, NULL);
DEVICE_ATTR_LEGACY(ce_count, S_IRUGO, csrow_ce_count_show, NULL);

/* default attributes of the CSROW<id> object */
static struct attribute *csrow_attrs[] = {
	&dev_attr_legacy_dev_type.attr,
	&dev_attr_legacy_mem_type.attr,
	&dev_attr_legacy_edac_mode.attr,
	&dev_attr_legacy_size_mb.attr,
	&dev_attr_legacy_ue_count.attr,
	&dev_attr_legacy_ce_count.attr,
	NULL,
};

static struct attribute_group csrow_attr_grp = {
	.attrs	= csrow_attrs,
};

static const struct attribute_group *csrow_attr_groups[] = {
	&csrow_attr_grp,
	NULL
};

static void csrow_attr_release(struct device *dev)
{
	struct csrow_info *csrow = container_of(dev, struct csrow_info, dev);

	edac_dbg(1, "Releasing csrow device %s\n", dev_name(dev));
	kfree(csrow);
}

static struct device_type csrow_attr_type = {
	.groups		= csrow_attr_groups,
	.release	= csrow_attr_release,
};

/*
 * possible dynamic channel DIMM Label attribute files
 *
 */
DEVICE_CHANNEL(ch0_dimm_label, S_IRUGO | S_IWUSR,
	channel_dimm_label_show, channel_dimm_label_store, 0);
DEVICE_CHANNEL(ch1_dimm_label, S_IRUGO | S_IWUSR,
	channel_dimm_label_show, channel_dimm_label_store, 1);
DEVICE_CHANNEL(ch2_dimm_label, S_IRUGO | S_IWUSR,
	channel_dimm_label_show, channel_dimm_label_store, 2);
DEVICE_CHANNEL(ch3_dimm_label, S_IRUGO | S_IWUSR,
	channel_dimm_label_show, channel_dimm_label_store, 3);
DEVICE_CHANNEL(ch4_dimm_label, S_IRUGO | S_IWUSR,
	channel_dimm_label_show, channel_dimm_label_store, 4);
DEVICE_CHANNEL(ch5_dimm_label, S_IRUGO | S_IWUSR,
	channel_dimm_label_show, channel_dimm_label_store, 5);
DEVICE_CHANNEL(ch6_dimm_label, S_IRUGO | S_IWUSR,
	channel_dimm_label_show, channel_dimm_label_store, 6);
DEVICE_CHANNEL(ch7_dimm_label, S_IRUGO | S_IWUSR,
	channel_dimm_label_show, channel_dimm_label_store, 7);

/* Total possible dynamic DIMM Label attribute file table */
static struct attribute *dynamic_csrow_dimm_attr[] = {
	&dev_attr_legacy_ch0_dimm_label.attr.attr,
	&dev_attr_legacy_ch1_dimm_label.attr.attr,
	&dev_attr_legacy_ch2_dimm_label.attr.attr,
	&dev_attr_legacy_ch3_dimm_label.attr.attr,
	&dev_attr_legacy_ch4_dimm_label.attr.attr,
	&dev_attr_legacy_ch5_dimm_label.attr.attr,
	&dev_attr_legacy_ch6_dimm_label.attr.attr,
	&dev_attr_legacy_ch7_dimm_label.attr.attr,
	NULL
};

/* possible dynamic channel ce_count attribute files */
DEVICE_CHANNEL(ch0_ce_count, S_IRUGO,
		   channel_ce_count_show, NULL, 0);
DEVICE_CHANNEL(ch1_ce_count, S_IRUGO,
		   channel_ce_count_show, NULL, 1);
DEVICE_CHANNEL(ch2_ce_count, S_IRUGO,
		   channel_ce_count_show, NULL, 2);
DEVICE_CHANNEL(ch3_ce_count, S_IRUGO,
		   channel_ce_count_show, NULL, 3);
DEVICE_CHANNEL(ch4_ce_count, S_IRUGO,
		   channel_ce_count_show, NULL, 4);
DEVICE_CHANNEL(ch5_ce_count, S_IRUGO,
		   channel_ce_count_show, NULL, 5);
DEVICE_CHANNEL(ch6_ce_count, S_IRUGO,
		   channel_ce_count_show, NULL, 6);
DEVICE_CHANNEL(ch7_ce_count, S_IRUGO,
		   channel_ce_count_show, NULL, 7);

/* Total possible dynamic ce_count attribute file table */
static struct attribute *dynamic_csrow_ce_count_attr[] = {
	&dev_attr_legacy_ch0_ce_count.attr.attr,
	&dev_attr_legacy_ch1_ce_count.attr.attr,
	&dev_attr_legacy_ch2_ce_count.attr.attr,
	&dev_attr_legacy_ch3_ce_count.attr.attr,
	&dev_attr_legacy_ch4_ce_count.attr.attr,
	&dev_attr_legacy_ch5_ce_count.attr.attr,
	&dev_attr_legacy_ch6_ce_count.attr.attr,
	&dev_attr_legacy_ch7_ce_count.attr.attr,
	NULL
};

static umode_t csrow_dev_is_visible(struct kobject *kobj,
				    struct attribute *attr, int idx)
{
	struct device *dev = kobj_to_dev(kobj);
	struct csrow_info *csrow = container_of(dev, struct csrow_info, dev);

	if (idx >= csrow->nr_channels)
		return 0;

	if (idx >= ARRAY_SIZE(dynamic_csrow_ce_count_attr) - 1) {
		WARN_ONCE(1, "idx: %d\n", idx);
		return 0;
	}

	/* Only expose populated DIMMs */
	if (!csrow->channels[idx]->dimm->nr_pages)
		return 0;

	return attr->mode;
}


static const struct attribute_group csrow_dev_dimm_group = {
	.attrs = dynamic_csrow_dimm_attr,
	.is_visible = csrow_dev_is_visible,
};

static const struct attribute_group csrow_dev_ce_count_group = {
	.attrs = dynamic_csrow_ce_count_attr,
	.is_visible = csrow_dev_is_visible,
};

static const struct attribute_group *csrow_dev_groups[] = {
	&csrow_dev_dimm_group,
	&csrow_dev_ce_count_group,
	NULL
};

static inline int nr_pages_per_csrow(struct csrow_info *csrow)
{
	int chan, nr_pages = 0;

	for (chan = 0; chan < csrow->nr_channels; chan++)
		nr_pages += csrow->channels[chan]->dimm->nr_pages;

	return nr_pages;
}

/* Create a CSROW object under specifed edac_mc_device */
static int edac_create_csrow_object(struct mem_ctl_info *mci,
				    struct csrow_info *csrow, int index)
{
	int err;

	csrow->dev.type = &csrow_attr_type;
	csrow->dev.bus = mci->bus;
	csrow->dev.groups = csrow_dev_groups;
	device_initialize(&csrow->dev);
	csrow->dev.parent = &mci->dev;
	csrow->mci = mci;
	dev_set_name(&csrow->dev, "csrow%d", index);
	dev_set_drvdata(&csrow->dev, csrow);

	edac_dbg(0, "creating (virtual) csrow node %s\n",
		 dev_name(&csrow->dev));

	err = device_add(&csrow->dev);
	if (err)
		put_device(&csrow->dev);

	return err;
}

/* Create a CSROW object under specifed edac_mc_device */
static int edac_create_csrow_objects(struct mem_ctl_info *mci)
{
	int err, i;
	struct csrow_info *csrow;

	for (i = 0; i < mci->nr_csrows; i++) {
		csrow = mci->csrows[i];
		if (!nr_pages_per_csrow(csrow))
			continue;
		err = edac_create_csrow_object(mci, mci->csrows[i], i);
		if (err < 0) {
			edac_dbg(1,
				 "failure: create csrow objects for csrow %d\n",
				 i);
			goto error;
		}
	}
	return 0;

error:
	for (--i; i >= 0; i--) {
		csrow = mci->csrows[i];
		if (!nr_pages_per_csrow(csrow))
			continue;
		put_device(&mci->csrows[i]->dev);
	}

	return err;
}

static void edac_delete_csrow_objects(struct mem_ctl_info *mci)
{
	int i;
	struct csrow_info *csrow;

	for (i = mci->nr_csrows - 1; i >= 0; i--) {
		csrow = mci->csrows[i];
		if (!nr_pages_per_csrow(csrow))
			continue;
		device_unregister(&mci->csrows[i]->dev);
	}
}
#endif

/*
 * Per-dimm (or per-rank) devices
 */

#define to_dimm(k) container_of(k, struct dimm_info, dev)

/* show/store functions for DIMM Label attributes */
static ssize_t dimmdev_location_show(struct device *dev,
				     struct device_attribute *mattr, char *data)
{
	struct dimm_info *dimm = to_dimm(dev);

	return edac_dimm_info_location(dimm, data, PAGE_SIZE);
}

static ssize_t dimmdev_label_show(struct device *dev,
				  struct device_attribute *mattr, char *data)
{
	struct dimm_info *dimm = to_dimm(dev);

	/* if field has not been initialized, there is nothing to send */
	if (!dimm->label[0])
		return 0;

	return snprintf(data, sizeof(dimm->label) + 1, "%s\n", dimm->label);
}

static ssize_t dimmdev_label_store(struct device *dev,
				   struct device_attribute *mattr,
				   const char *data,
				   size_t count)
{
	struct dimm_info *dimm = to_dimm(dev);
	size_t copy_count = count;

	if (count == 0)
		return -EINVAL;

	if (data[count - 1] == '\0' || data[count - 1] == '\n')
		copy_count -= 1;

	if (copy_count == 0 || copy_count >= sizeof(dimm->label))
		return -EINVAL;

	strncpy(dimm->label, data, copy_count);
	dimm->label[copy_count] = '\0';

	return count;
}

static ssize_t dimmdev_size_show(struct device *dev,
				 struct device_attribute *mattr, char *data)
{
	struct dimm_info *dimm = to_dimm(dev);

	return sprintf(data, "%u\n", PAGES_TO_MiB(dimm->nr_pages));
}

static ssize_t dimmdev_mem_type_show(struct device *dev,
				     struct device_attribute *mattr, char *data)
{
	struct dimm_info *dimm = to_dimm(dev);

	return sprintf(data, "%s\n", mem_types[dimm->mtype]);
}

static ssize_t dimmdev_dev_type_show(struct device *dev,
				     struct device_attribute *mattr, char *data)
{
	struct dimm_info *dimm = to_dimm(dev);

	return sprintf(data, "%s\n", dev_types[dimm->dtype]);
}

static ssize_t dimmdev_edac_mode_show(struct device *dev,
				      struct device_attribute *mattr,
				      char *data)
{
	struct dimm_info *dimm = to_dimm(dev);

	return sprintf(data, "%s\n", edac_caps[dimm->edac_mode]);
}

/* dimm/rank attribute files */
static DEVICE_ATTR(dimm_label, S_IRUGO | S_IWUSR,
		   dimmdev_label_show, dimmdev_label_store);
static DEVICE_ATTR(dimm_location, S_IRUGO, dimmdev_location_show, NULL);
static DEVICE_ATTR(size, S_IRUGO, dimmdev_size_show, NULL);
static DEVICE_ATTR(dimm_mem_type, S_IRUGO, dimmdev_mem_type_show, NULL);
static DEVICE_ATTR(dimm_dev_type, S_IRUGO, dimmdev_dev_type_show, NULL);
static DEVICE_ATTR(dimm_edac_mode, S_IRUGO, dimmdev_edac_mode_show, NULL);

/* attributes of the dimm<id>/rank<id> object */
static struct attribute *dimm_attrs[] = {
	&dev_attr_dimm_label.attr,
	&dev_attr_dimm_location.attr,
	&dev_attr_size.attr,
	&dev_attr_dimm_mem_type.attr,
	&dev_attr_dimm_dev_type.attr,
	&dev_attr_dimm_edac_mode.attr,
	NULL,
};

static struct attribute_group dimm_attr_grp = {
	.attrs	= dimm_attrs,
};

static const struct attribute_group *dimm_attr_groups[] = {
	&dimm_attr_grp,
	NULL
};

static void dimm_attr_release(struct device *dev)
{
	struct dimm_info *dimm = container_of(dev, struct dimm_info, dev);

	edac_dbg(1, "Releasing dimm device %s\n", dev_name(dev));
	kfree(dimm);
}

static struct device_type dimm_attr_type = {
	.groups		= dimm_attr_groups,
	.release	= dimm_attr_release,
};

/* Create a DIMM object under specifed memory controller device */
static int edac_create_dimm_object(struct mem_ctl_info *mci,
				   struct dimm_info *dimm,
				   int index)
{
	int err;
	dimm->mci = mci;

	dimm->dev.type = &dimm_attr_type;
	dimm->dev.bus = mci->bus;
	device_initialize(&dimm->dev);

	dimm->dev.parent = &mci->dev;
	if (mci->csbased)
		dev_set_name(&dimm->dev, "rank%d", index);
	else
		dev_set_name(&dimm->dev, "dimm%d", index);
	dev_set_drvdata(&dimm->dev, dimm);
	pm_runtime_forbid(&mci->dev);

	err =  device_add(&dimm->dev);

	edac_dbg(0, "creating rank/dimm device %s\n", dev_name(&dimm->dev));

	return err;
}

/*
 * Memory controller device
 */

#define to_mci(k) container_of(k, struct mem_ctl_info, dev)

static ssize_t mci_reset_counters_store(struct device *dev,
					struct device_attribute *mattr,
					const char *data, size_t count)
{
	struct mem_ctl_info *mci = to_mci(dev);
	int cnt, row, chan, i;
	mci->ue_mc = 0;
	mci->ce_mc = 0;
	mci->ue_noinfo_count = 0;
	mci->ce_noinfo_count = 0;

	for (row = 0; row < mci->nr_csrows; row++) {
		struct csrow_info *ri = mci->csrows[row];

		ri->ue_count = 0;
		ri->ce_count = 0;

		for (chan = 0; chan < ri->nr_channels; chan++)
			ri->channels[chan].ce_count = 0;
			ri->channels[chan]->ce_count = 0;
	}

	cnt = 1;
	for (i = 0; i < mci->n_layers; i++) {
		cnt *= mci->layers[i].size;
		memset(mci->ce_per_layer[i], 0, cnt * sizeof(u32));
		memset(mci->ue_per_layer[i], 0, cnt * sizeof(u32));
	}

	mci->start_time = jiffies;
	return count;
}

/* memory scrubbing */
static ssize_t mci_sdram_scrub_rate_store(struct mem_ctl_info *mci,
					const char *data, size_t count)
{
	u32 bandwidth = -1;

	if (mci->set_sdram_scrub_rate) {

		memctrl_int_store(&bandwidth, data, count);

		if (!(*mci->set_sdram_scrub_rate) (mci, &bandwidth)) {
			edac_printk(KERN_DEBUG, EDAC_MC,
				"Scrub rate set successfully, applied: %d\n",
				bandwidth);
		} else {
			/* FIXME: error codes maybe? */
			edac_printk(KERN_DEBUG, EDAC_MC,
				"Scrub rate set FAILED, could not apply: %d\n",
				bandwidth);
		}
	} else {
		/* FIXME: produce "not implemented" ERROR for user-side. */
		edac_printk(KERN_WARNING, EDAC_MC,
			"Memory scrubbing 'set'control is not implemented!\n");
	}
	return count;
}

static ssize_t mci_sdram_scrub_rate_show(struct mem_ctl_info *mci, char *data)
{
	u32 bandwidth = -1;

	if (mci->get_sdram_scrub_rate) {
		if (!(*mci->get_sdram_scrub_rate) (mci, &bandwidth)) {
			edac_printk(KERN_DEBUG, EDAC_MC,
				"Scrub rate successfully, fetched: %d\n",
				bandwidth);
		} else {
			/* FIXME: error codes maybe? */
			edac_printk(KERN_DEBUG, EDAC_MC,
				"Scrub rate fetch FAILED, got: %d\n",
				bandwidth);
		}
	} else {
		/* FIXME: produce "not implemented" ERROR for user-side.  */
		edac_printk(KERN_WARNING, EDAC_MC,
			"Memory scrubbing 'get' control is not implemented\n");
	}
/* Memory scrubbing interface:
 *
 * A MC driver can limit the scrubbing bandwidth based on the CPU type.
 * Therefore, ->set_sdram_scrub_rate should be made to return the actual
 * bandwidth that is accepted or 0 when scrubbing is to be disabled.
 *
 * Negative value still means that an error has occurred while setting
 * the scrub rate.
 */
static ssize_t mci_sdram_scrub_rate_store(struct device *dev,
					  struct device_attribute *mattr,
					  const char *data, size_t count)
{
	struct mem_ctl_info *mci = to_mci(dev);
	unsigned long bandwidth = 0;
	int new_bw = 0;

	if (kstrtoul(data, 10, &bandwidth) < 0)
		return -EINVAL;

	new_bw = mci->set_sdram_scrub_rate(mci, bandwidth);
	if (new_bw < 0) {
		edac_printk(KERN_WARNING, EDAC_MC,
			    "Error setting scrub rate to: %lu\n", bandwidth);
		return -EINVAL;
	}

	return count;
}

/*
 * ->get_sdram_scrub_rate() return value semantics same as above.
 */
static ssize_t mci_sdram_scrub_rate_show(struct device *dev,
					 struct device_attribute *mattr,
					 char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);
	int bandwidth = 0;

	bandwidth = mci->get_sdram_scrub_rate(mci);
	if (bandwidth < 0) {
		edac_printk(KERN_DEBUG, EDAC_MC, "Error reading scrub rate\n");
		return bandwidth;
	}

	return sprintf(data, "%d\n", bandwidth);
}

/* default attribute files for the MCI object */
static ssize_t mci_ue_count_show(struct mem_ctl_info *mci, char *data)
{
	return sprintf(data, "%d\n", mci->ue_count);
}

static ssize_t mci_ce_count_show(struct mem_ctl_info *mci, char *data)
{
	return sprintf(data, "%d\n", mci->ce_count);
}

static ssize_t mci_ce_noinfo_show(struct mem_ctl_info *mci, char *data)
{
	return sprintf(data, "%d\n", mci->ce_noinfo_count);
}

static ssize_t mci_ue_noinfo_show(struct mem_ctl_info *mci, char *data)
{
	return sprintf(data, "%d\n", mci->ue_noinfo_count);
}

static ssize_t mci_seconds_show(struct mem_ctl_info *mci, char *data)
{
	return sprintf(data, "%ld\n", (jiffies - mci->start_time) / HZ);
}

static ssize_t mci_ctl_name_show(struct mem_ctl_info *mci, char *data)
{
	return sprintf(data, "%s\n", mci->ctl_name);
}

static ssize_t mci_size_mb_show(struct mem_ctl_info *mci, char *data)
{
	int total_pages, csrow_idx;

	for (total_pages = csrow_idx = 0; csrow_idx < mci->nr_csrows;
		csrow_idx++) {
		struct csrow_info *csrow = &mci->csrows[csrow_idx];

		if (!csrow->nr_pages)
			continue;

		total_pages += csrow->nr_pages;
static ssize_t mci_ue_count_show(struct device *dev,
				 struct device_attribute *mattr,
				 char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);

	return sprintf(data, "%d\n", mci->ue_mc);
}

static ssize_t mci_ce_count_show(struct device *dev,
				 struct device_attribute *mattr,
				 char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);

	return sprintf(data, "%d\n", mci->ce_mc);
}

static ssize_t mci_ce_noinfo_show(struct device *dev,
				  struct device_attribute *mattr,
				  char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);

	return sprintf(data, "%d\n", mci->ce_noinfo_count);
}

static ssize_t mci_ue_noinfo_show(struct device *dev,
				  struct device_attribute *mattr,
				  char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);

	return sprintf(data, "%d\n", mci->ue_noinfo_count);
}

static ssize_t mci_seconds_show(struct device *dev,
				struct device_attribute *mattr,
				char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);

	return sprintf(data, "%ld\n", (jiffies - mci->start_time) / HZ);
}

static ssize_t mci_ctl_name_show(struct device *dev,
				 struct device_attribute *mattr,
				 char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);

	return sprintf(data, "%s\n", mci->ctl_name);
}

static ssize_t mci_size_mb_show(struct device *dev,
				struct device_attribute *mattr,
				char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);
	int total_pages = 0, csrow_idx, j;

	for (csrow_idx = 0; csrow_idx < mci->nr_csrows; csrow_idx++) {
		struct csrow_info *csrow = mci->csrows[csrow_idx];

		for (j = 0; j < csrow->nr_channels; j++) {
			struct dimm_info *dimm = csrow->channels[j]->dimm;

			total_pages += dimm->nr_pages;
		}
	}

	return sprintf(data, "%u\n", PAGES_TO_MiB(total_pages));
}

#define to_mci(k) container_of(k, struct mem_ctl_info, edac_mci_kobj)
#define to_mcidev_attr(a) container_of(a,struct mcidev_sysfs_attribute,attr)

/* MCI show/store functions for top most object */
static ssize_t mcidev_show(struct kobject *kobj, struct attribute *attr,
			char *buffer)
{
	struct mem_ctl_info *mem_ctl_info = to_mci(kobj);
	struct mcidev_sysfs_attribute *mcidev_attr = to_mcidev_attr(attr);

	if (mcidev_attr->show)
		return mcidev_attr->show(mem_ctl_info, buffer);

	return -EIO;
}

static ssize_t mcidev_store(struct kobject *kobj, struct attribute *attr,
			const char *buffer, size_t count)
{
	struct mem_ctl_info *mem_ctl_info = to_mci(kobj);
	struct mcidev_sysfs_attribute *mcidev_attr = to_mcidev_attr(attr);

	if (mcidev_attr->store)
		return mcidev_attr->store(mem_ctl_info, buffer, count);

	return -EIO;
}

/* Intermediate show/store table */
static struct sysfs_ops mci_ops = {
	.show = mcidev_show,
	.store = mcidev_store
};

#define MCIDEV_ATTR(_name,_mode,_show,_store)			\
static struct mcidev_sysfs_attribute mci_attr_##_name = {			\
	.attr = {.name = __stringify(_name), .mode = _mode },	\
	.show   = _show,					\
	.store  = _store,					\
};

/* default Control file */
MCIDEV_ATTR(reset_counters, S_IWUSR, NULL, mci_reset_counters_store);

/* default Attribute files */
MCIDEV_ATTR(mc_name, S_IRUGO, mci_ctl_name_show, NULL);
MCIDEV_ATTR(size_mb, S_IRUGO, mci_size_mb_show, NULL);
MCIDEV_ATTR(seconds_since_reset, S_IRUGO, mci_seconds_show, NULL);
MCIDEV_ATTR(ue_noinfo_count, S_IRUGO, mci_ue_noinfo_show, NULL);
MCIDEV_ATTR(ce_noinfo_count, S_IRUGO, mci_ce_noinfo_show, NULL);
MCIDEV_ATTR(ue_count, S_IRUGO, mci_ue_count_show, NULL);
MCIDEV_ATTR(ce_count, S_IRUGO, mci_ce_count_show, NULL);

/* memory scrubber attribute file */
MCIDEV_ATTR(sdram_scrub_rate, S_IRUGO | S_IWUSR, mci_sdram_scrub_rate_show,
	mci_sdram_scrub_rate_store);

static struct mcidev_sysfs_attribute *mci_attr[] = {
	&mci_attr_reset_counters,
	&mci_attr_mc_name,
	&mci_attr_size_mb,
	&mci_attr_seconds_since_reset,
	&mci_attr_ue_noinfo_count,
	&mci_attr_ce_noinfo_count,
	&mci_attr_ue_count,
	&mci_attr_ce_count,
	&mci_attr_sdram_scrub_rate,
	NULL
};


/*
 * Release of a MC controlling instance
 *
 *	each MC control instance has the following resources upon entry:
 *		a) a ref count on the top memctl kobj
 *		b) a ref count on this module
 *
 *	this function must decrement those ref counts and then
 *	issue a free on the instance's memory
 */
static void edac_mci_control_release(struct kobject *kobj)
{
	struct mem_ctl_info *mci;

	mci = to_mci(kobj);

	debugf0("%s() mci instance idx=%d releasing\n", __func__, mci->mc_idx);

	/* decrement the module ref count */
	module_put(mci->owner);

	/* free the mci instance memory here */
	kfree(mci);
}

static struct kobj_type ktype_mci = {
	.release = edac_mci_control_release,
	.sysfs_ops = &mci_ops,
	.default_attrs = (struct attribute **)mci_attr,
};

/* EDAC memory controller sysfs kset:
 *	/sys/devices/system/edac/mc
 */
static struct kset *mc_kset;

/*
 * edac_mc_register_sysfs_main_kobj
 *
 *	setups and registers the main kobject for each mci
 */
int edac_mc_register_sysfs_main_kobj(struct mem_ctl_info *mci)
{
	struct kobject *kobj_mci;
	int err;

	debugf1("%s()\n", __func__);

	kobj_mci = &mci->edac_mci_kobj;

	/* Init the mci's kobject */
	memset(kobj_mci, 0, sizeof(*kobj_mci));

	/* Record which module 'owns' this control structure
	 * and bump the ref count of the module
	 */
	mci->owner = THIS_MODULE;

	/* bump ref count on this module */
	if (!try_module_get(mci->owner)) {
		err = -ENODEV;
		goto fail_out;
	}

	/* this instance become part of the mc_kset */
	kobj_mci->kset = mc_kset;

	/* register the mc<id> kobject to the mc_kset */
	err = kobject_init_and_add(kobj_mci, &ktype_mci, NULL,
				   "mc%d", mci->mc_idx);
	if (err) {
		debugf1("%s()Failed to register '.../edac/mc%d'\n",
			__func__, mci->mc_idx);
		goto kobj_reg_fail;
	}
	kobject_uevent(kobj_mci, KOBJ_ADD);

	/* At this point, to 'free' the control struct,
	 * edac_mc_unregister_sysfs_main_kobj() must be used
	 */

	debugf1("%s() Registered '.../edac/mc%d' kobject\n",
		__func__, mci->mc_idx);

	return 0;

	/* Error exit stack */

kobj_reg_fail:
	module_put(mci->owner);

fail_out:
	return err;
}

/*
 * edac_mc_register_sysfs_main_kobj
 *
 *	tears down and the main mci kobject from the mc_kset
 */
void edac_mc_unregister_sysfs_main_kobj(struct mem_ctl_info *mci)
{
	/* delete the kobj from the mc_kset */
	kobject_put(&mci->edac_mci_kobj);
}

#define EDAC_DEVICE_SYMLINK	"device"

/*
 * edac_create_mci_instance_attributes
 *	create MC driver specific attributes at the topmost level
 *	directory of this mci instance.
 */
static int edac_create_mci_instance_attributes(struct mem_ctl_info *mci)
{
	int err;
	struct mcidev_sysfs_attribute *sysfs_attrib;

	/* point to the start of the array and iterate over it
	 * adding each attribute listed to this mci instance's kobject
	 */
	sysfs_attrib = mci->mc_driver_sysfs_attributes;

	while (sysfs_attrib && sysfs_attrib->attr.name) {
		err = sysfs_create_file(&mci->edac_mci_kobj,
					(struct attribute*) sysfs_attrib);
		if (err) {
			return err;
		}

		sysfs_attrib++;
	}

	return 0;
}

/*
 * edac_remove_mci_instance_attributes
 *	remove MC driver specific attributes at the topmost level
 *	directory of this mci instance.
 */
static void edac_remove_mci_instance_attributes(struct mem_ctl_info *mci)
{
	struct mcidev_sysfs_attribute *sysfs_attrib;

	/* point to the start of the array and iterate over it
	 * adding each attribute listed to this mci instance's kobject
	 */
	sysfs_attrib = mci->mc_driver_sysfs_attributes;

	/* loop if there are attributes and until we hit a NULL entry */
	while (sysfs_attrib && sysfs_attrib->attr.name) {
		sysfs_remove_file(&mci->edac_mci_kobj,
					(struct attribute *) sysfs_attrib);
		sysfs_attrib++;
	}
}


static ssize_t mci_max_location_show(struct device *dev,
				     struct device_attribute *mattr,
				     char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);
	int i;
	char *p = data;

	for (i = 0; i < mci->n_layers; i++) {
		p += sprintf(p, "%s %d ",
			     edac_layer_name[mci->layers[i].type],
			     mci->layers[i].size - 1);
	}

	return p - data;
}

/* default Control file */
static DEVICE_ATTR(reset_counters, S_IWUSR, NULL, mci_reset_counters_store);

/* default Attribute files */
static DEVICE_ATTR(mc_name, S_IRUGO, mci_ctl_name_show, NULL);
static DEVICE_ATTR(size_mb, S_IRUGO, mci_size_mb_show, NULL);
static DEVICE_ATTR(seconds_since_reset, S_IRUGO, mci_seconds_show, NULL);
static DEVICE_ATTR(ue_noinfo_count, S_IRUGO, mci_ue_noinfo_show, NULL);
static DEVICE_ATTR(ce_noinfo_count, S_IRUGO, mci_ce_noinfo_show, NULL);
static DEVICE_ATTR(ue_count, S_IRUGO, mci_ue_count_show, NULL);
static DEVICE_ATTR(ce_count, S_IRUGO, mci_ce_count_show, NULL);
static DEVICE_ATTR(max_location, S_IRUGO, mci_max_location_show, NULL);

/* memory scrubber attribute file */
DEVICE_ATTR(sdram_scrub_rate, 0, mci_sdram_scrub_rate_show,
	    mci_sdram_scrub_rate_store); /* umode set later in is_visible */

static struct attribute *mci_attrs[] = {
	&dev_attr_reset_counters.attr,
	&dev_attr_mc_name.attr,
	&dev_attr_size_mb.attr,
	&dev_attr_seconds_since_reset.attr,
	&dev_attr_ue_noinfo_count.attr,
	&dev_attr_ce_noinfo_count.attr,
	&dev_attr_ue_count.attr,
	&dev_attr_ce_count.attr,
	&dev_attr_max_location.attr,
	&dev_attr_sdram_scrub_rate.attr,
	NULL
};

static umode_t mci_attr_is_visible(struct kobject *kobj,
				   struct attribute *attr, int idx)
{
	struct device *dev = kobj_to_dev(kobj);
	struct mem_ctl_info *mci = to_mci(dev);
	umode_t mode = 0;

	if (attr != &dev_attr_sdram_scrub_rate.attr)
		return attr->mode;
	if (mci->get_sdram_scrub_rate)
		mode |= S_IRUGO;
	if (mci->set_sdram_scrub_rate)
		mode |= S_IWUSR;
	return mode;
}

static struct attribute_group mci_attr_grp = {
	.attrs	= mci_attrs,
	.is_visible = mci_attr_is_visible,
};

static const struct attribute_group *mci_attr_groups[] = {
	&mci_attr_grp,
	NULL
};

static void mci_attr_release(struct device *dev)
{
	struct mem_ctl_info *mci = container_of(dev, struct mem_ctl_info, dev);

	edac_dbg(1, "Releasing csrow device %s\n", dev_name(dev));
	kfree(mci);
}

static struct device_type mci_attr_type = {
	.groups		= mci_attr_groups,
	.release	= mci_attr_release,
};

/*
 * Create a new Memory Controller kobject instance,
 *	mc<id> under the 'mc' directory
 *
 * Return:
 *	0	Success
 *	!0	Failure
 */
int edac_create_sysfs_mci_device(struct mem_ctl_info *mci)
{
	int i;
	int err;
	struct csrow_info *csrow;
	struct kobject *kobj_mci = &mci->edac_mci_kobj;

	debugf0("%s() idx=%d\n", __func__, mci->mc_idx);

	/* create a symlink for the device */
	err = sysfs_create_link(kobj_mci, &mci->dev->kobj,
				EDAC_DEVICE_SYMLINK);
	if (err) {
		debugf1("%s() failure to create symlink\n", __func__);
		goto fail0;
	}

	/* If the low level driver desires some attributes,
	 * then create them now for the driver.
	 */
	if (mci->mc_driver_sysfs_attributes) {
		err = edac_create_mci_instance_attributes(mci);
		if (err) {
			debugf1("%s() failure to create mci attributes\n",
				__func__);
			goto fail0;
		}
	}

	/* Make directories for each CSROW object under the mc<id> kobject
	 */
	for (i = 0; i < mci->nr_csrows; i++) {
		csrow = &mci->csrows[i];

		/* Only expose populated CSROWs */
		if (csrow->nr_pages > 0) {
			err = edac_create_csrow_object(mci, csrow, i);
			if (err) {
				debugf1("%s() failure: create csrow %d obj\n",
					__func__, i);
				goto fail1;
			}
		}
	}

	return 0;

	/* CSROW error: backout what has already been registered,  */
fail1:
	for (i--; i >= 0; i--) {
		if (csrow->nr_pages > 0) {
			kobject_put(&mci->csrows[i].kobj);
		}
	}

	/* remove the mci instance's attributes, if any */
	edac_remove_mci_instance_attributes(mci);

	/* remove the symlink */
	sysfs_remove_link(kobj_mci, EDAC_DEVICE_SYMLINK);

fail0:
int edac_create_sysfs_mci_device(struct mem_ctl_info *mci,
				 const struct attribute_group **groups)
{
	char *name;
	int i, err;

	/*
	 * The memory controller needs its own bus, in order to avoid
	 * namespace conflicts at /sys/bus/edac.
	 */
	name = kasprintf(GFP_KERNEL, "mc%d", mci->mc_idx);
	if (!name)
		return -ENOMEM;

	mci->bus->name = name;

	edac_dbg(0, "creating bus %s\n", mci->bus->name);

	err = bus_register(mci->bus);
	if (err < 0) {
		kfree(name);
		return err;
	}

	/* get the /sys/devices/system/edac subsys reference */
	mci->dev.type = &mci_attr_type;
	device_initialize(&mci->dev);

	mci->dev.parent = mci_pdev;
	mci->dev.bus = mci->bus;
	mci->dev.groups = groups;
	dev_set_name(&mci->dev, "mc%d", mci->mc_idx);
	dev_set_drvdata(&mci->dev, mci);
	pm_runtime_forbid(&mci->dev);

	edac_dbg(0, "creating device %s\n", dev_name(&mci->dev));
	err = device_add(&mci->dev);
	if (err < 0) {
		edac_dbg(1, "failure: create device %s\n", dev_name(&mci->dev));
		goto fail_unregister_bus;
	}

	/*
	 * Create the dimm/rank devices
	 */
	for (i = 0; i < mci->tot_dimms; i++) {
		struct dimm_info *dimm = mci->dimms[i];
		/* Only expose populated DIMMs */
		if (!dimm->nr_pages)
			continue;

#ifdef CONFIG_EDAC_DEBUG
		edac_dbg(1, "creating dimm%d, located at ", i);
		if (edac_debug_level >= 1) {
			int lay;
			for (lay = 0; lay < mci->n_layers; lay++)
				printk(KERN_CONT "%s %d ",
					edac_layer_name[mci->layers[lay].type],
					dimm->location[lay]);
			printk(KERN_CONT "\n");
		}
#endif
		err = edac_create_dimm_object(mci, dimm, i);
		if (err) {
			edac_dbg(1, "failure: create dimm %d obj\n", i);
			goto fail_unregister_dimm;
		}
	}

#ifdef CONFIG_EDAC_LEGACY_SYSFS
	err = edac_create_csrow_objects(mci);
	if (err < 0)
		goto fail_unregister_dimm;
#endif

	edac_create_debugfs_nodes(mci);
	return 0;

fail_unregister_dimm:
	for (i--; i >= 0; i--) {
		struct dimm_info *dimm = mci->dimms[i];
		if (!dimm->nr_pages)
			continue;

		device_unregister(&dimm->dev);
	}
	device_unregister(&mci->dev);
fail_unregister_bus:
	bus_unregister(mci->bus);
	kfree(name);

	return err;
}

/*
 * remove a Memory Controller instance
 */
void edac_remove_sysfs_mci_device(struct mem_ctl_info *mci)
{
	int i;

	debugf0("%s()\n", __func__);

	/* remove all csrow kobjects */
	for (i = 0; i < mci->nr_csrows; i++) {
		if (mci->csrows[i].nr_pages > 0) {
			debugf0("%s()  unreg csrow-%d\n", __func__, i);
			kobject_put(&mci->csrows[i].kobj);
		}
	}

	debugf0("%s()  remove_link\n", __func__);

	/* remove the symlink */
	sysfs_remove_link(&mci->edac_mci_kobj, EDAC_DEVICE_SYMLINK);

	debugf0("%s()  remove_mci_instance\n", __func__);

	/* remove this mci instance's attribtes */
	edac_remove_mci_instance_attributes(mci);

	debugf0("%s()  unregister this mci kobj\n", __func__);

	/* unregister this instance's kobject */
	kobject_put(&mci->edac_mci_kobj);
}




/*
 * edac_setup_sysfs_mc_kset(void)
 *
 * Initialize the mc_kset for the 'mc' entry
 *	This requires creating the top 'mc' directory with a kset
 *	and its controls/attributes.
 *
 *	To this 'mc' kset, instance 'mci' will be grouped as children.
 *
 * Return:  0 SUCCESS
 *         !0 FAILURE error code
 */
int edac_sysfs_setup_mc_kset(void)
{
	int err = 0;
	struct sysdev_class *edac_class;

	debugf1("%s()\n", __func__);

	/* get the /sys/devices/system/edac class reference */
	edac_class = edac_get_edac_class();
	if (edac_class == NULL) {
		debugf1("%s() no edac_class error=%d\n", __func__, err);
		goto fail_out;
	}

	/* Init the MC's kobject */
	mc_kset = kset_create_and_add("mc", NULL, &edac_class->kset.kobj);
	if (!mc_kset) {
		err = -ENOMEM;
		debugf1("%s() Failed to register '.../edac/mc'\n", __func__);
		goto fail_out;
	}

	debugf1("%s() Registered '.../edac/mc' kobject\n", __func__);

	return 0;


	/* error unwind stack */
fail_out:
	return err;
}

/*
 * edac_sysfs_teardown_mc_kset
 *
 *	deconstruct the mc_ket for memory controllers
 */
void edac_sysfs_teardown_mc_kset(void)
{
	kset_unregister(mc_kset);
}

	edac_dbg(0, "\n");

#ifdef CONFIG_EDAC_DEBUG
	edac_debugfs_remove_recursive(mci->debugfs);
#endif
#ifdef CONFIG_EDAC_LEGACY_SYSFS
	edac_delete_csrow_objects(mci);
#endif

	for (i = 0; i < mci->tot_dimms; i++) {
		struct dimm_info *dimm = mci->dimms[i];
		if (dimm->nr_pages == 0)
			continue;
		edac_dbg(0, "removing device %s\n", dev_name(&dimm->dev));
		device_unregister(&dimm->dev);
	}
}

void edac_unregister_sysfs(struct mem_ctl_info *mci)
{
	const char *name = mci->bus->name;

	edac_dbg(1, "Unregistering device %s\n", dev_name(&mci->dev));
	device_unregister(&mci->dev);
	bus_unregister(mci->bus);
	kfree(name);
}

static void mc_attr_release(struct device *dev)
{
	/*
	 * There's no container structure here, as this is just the mci
	 * parent device, used to create the /sys/devices/mc sysfs node.
	 * So, there are no attributes on it.
	 */
	edac_dbg(1, "Releasing device %s\n", dev_name(dev));
	kfree(dev);
}

static struct device_type mc_attr_type = {
	.release	= mc_attr_release,
};
/*
 * Init/exit code for the module. Basically, creates/removes /sys/class/rc
 */
int __init edac_mc_sysfs_init(void)
{
	struct bus_type *edac_subsys;
	int err;

	/* get the /sys/devices/system/edac subsys reference */
	edac_subsys = edac_get_sysfs_subsys();
	if (edac_subsys == NULL) {
		edac_dbg(1, "no edac_subsys\n");
		err = -EINVAL;
		goto out;
	}

	mci_pdev = kzalloc(sizeof(*mci_pdev), GFP_KERNEL);
	if (!mci_pdev) {
		err = -ENOMEM;
		goto out_put_sysfs;
	}

	mci_pdev->bus = edac_subsys;
	mci_pdev->type = &mc_attr_type;
	device_initialize(mci_pdev);
	dev_set_name(mci_pdev, "mc");

	err = device_add(mci_pdev);
	if (err < 0)
		goto out_dev_free;

	edac_dbg(0, "device %s created\n", dev_name(mci_pdev));

	return 0;

 out_dev_free:
	kfree(mci_pdev);
 out_put_sysfs:
	edac_put_sysfs_subsys();
 out:
	return err;
}

void edac_mc_sysfs_exit(void)
{
	device_unregister(mci_pdev);
	edac_put_sysfs_subsys();
}
