/*
 * soc-dapm.c  --  ALSA SoC Dynamic Audio Power Management
 *
 * Copyright 2005 Wolfson Microelectronics PLC.
 * Author: Liam Girdwood
 *         liam.girdwood@wolfsonmicro.com or linux@wolfsonmicro.com
 * Author: Liam Girdwood <lrg@slimlogic.co.uk>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  Features:
 *    o Changes power status of internal codec blocks depending on the
 *      dynamic configuration of codec internal audio paths and active
 *      DAC's/ADC's.
 *    o Platform power domain - can support external components i.e. amps and
 *      mic/meadphone insertion events.
 *    o Automatic Mic Bias support
 *    o Jack insertion power event initiation - e.g. hp insertion will enable
 *      sinks, dacs, etc
 *    o Delayed powerdown of audio susbsystem to reduce pops between a quick
 *      device reopen.
 *
 *  Todo:
 *    o DAPM power change sequencing - allow for configurable per
 *      codec sequences.
 *    o Support for analogue bias optimisation.
 *    o Support for reduced codec oversampling rates.
 *    o Support for reduced codec bias currents.
 *      DACs/ADCs.
 *    o Platform power domain - can support external components i.e. amps and
 *      mic/headphone insertion events.
 *    o Automatic Mic Bias support
 *    o Jack insertion power event initiation - e.g. hp insertion will enable
 *      sinks, dacs, etc
 *    o Delayed power down of audio subsystem to reduce pops between a quick
 *      device reopen.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/async.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/bitops.h>
#include <linux/platform_device.h>
#include <linux/jiffies.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>

/* debug */
#ifdef DEBUG
#define dump_dapm(codec, action) dbg_dump_dapm(codec, action)
#else
#define dump_dapm(codec, action)
#endif

/* dapm power sequences - make this per codec in the future */
static int dapm_up_seq[] = {
	snd_soc_dapm_pre, snd_soc_dapm_micbias, snd_soc_dapm_mic,
	snd_soc_dapm_mux, snd_soc_dapm_dac, snd_soc_dapm_mixer, snd_soc_dapm_pga,
	snd_soc_dapm_adc, snd_soc_dapm_hp, snd_soc_dapm_spk, snd_soc_dapm_post
};
static int dapm_down_seq[] = {
	snd_soc_dapm_pre, snd_soc_dapm_adc, snd_soc_dapm_hp, snd_soc_dapm_spk,
	snd_soc_dapm_pga, snd_soc_dapm_mixer, snd_soc_dapm_dac, snd_soc_dapm_mic,
	snd_soc_dapm_micbias, snd_soc_dapm_mux, snd_soc_dapm_post
};

static int dapm_status = 1;
module_param(dapm_status, int, 0);
MODULE_PARM_DESC(dapm_status, "enable DPM sysfs entries");

static unsigned int pop_time;

static void pop_wait(void)
#include <linux/debugfs.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>

#include <trace/events/asoc.h>

#define DAPM_UPDATE_STAT(widget, val) widget->dapm->card->dapm_stats.val++;

#define SND_SOC_DAPM_DIR_REVERSE(x) ((x == SND_SOC_DAPM_DIR_IN) ? \
	SND_SOC_DAPM_DIR_OUT : SND_SOC_DAPM_DIR_IN)

#define snd_soc_dapm_for_each_direction(dir) \
	for ((dir) = SND_SOC_DAPM_DIR_IN; (dir) <= SND_SOC_DAPM_DIR_OUT; \
		(dir)++)

static int snd_soc_dapm_add_path(struct snd_soc_dapm_context *dapm,
	struct snd_soc_dapm_widget *wsource, struct snd_soc_dapm_widget *wsink,
	const char *control,
	int (*connected)(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink));

struct snd_soc_dapm_widget *
snd_soc_dapm_new_control(struct snd_soc_dapm_context *dapm,
			 const struct snd_soc_dapm_widget *widget);

struct snd_soc_dapm_widget *
snd_soc_dapm_new_control_unlocked(struct snd_soc_dapm_context *dapm,
			 const struct snd_soc_dapm_widget *widget);

/* dapm power sequences - make this per codec in the future */
static int dapm_up_seq[] = {
	[snd_soc_dapm_pre] = 0,
	[snd_soc_dapm_regulator_supply] = 1,
	[snd_soc_dapm_clock_supply] = 1,
	[snd_soc_dapm_supply] = 2,
	[snd_soc_dapm_micbias] = 3,
	[snd_soc_dapm_dai_link] = 2,
	[snd_soc_dapm_dai_in] = 4,
	[snd_soc_dapm_dai_out] = 4,
	[snd_soc_dapm_aif_in] = 4,
	[snd_soc_dapm_aif_out] = 4,
	[snd_soc_dapm_mic] = 5,
	[snd_soc_dapm_mux] = 6,
	[snd_soc_dapm_demux] = 6,
	[snd_soc_dapm_dac] = 7,
	[snd_soc_dapm_switch] = 8,
	[snd_soc_dapm_mixer] = 8,
	[snd_soc_dapm_mixer_named_ctl] = 8,
	[snd_soc_dapm_pga] = 9,
	[snd_soc_dapm_adc] = 10,
	[snd_soc_dapm_out_drv] = 11,
	[snd_soc_dapm_hp] = 11,
	[snd_soc_dapm_spk] = 11,
	[snd_soc_dapm_line] = 11,
	[snd_soc_dapm_kcontrol] = 12,
	[snd_soc_dapm_post] = 13,
};

static int dapm_down_seq[] = {
	[snd_soc_dapm_pre] = 0,
	[snd_soc_dapm_kcontrol] = 1,
	[snd_soc_dapm_adc] = 2,
	[snd_soc_dapm_hp] = 3,
	[snd_soc_dapm_spk] = 3,
	[snd_soc_dapm_line] = 3,
	[snd_soc_dapm_out_drv] = 3,
	[snd_soc_dapm_pga] = 4,
	[snd_soc_dapm_switch] = 5,
	[snd_soc_dapm_mixer_named_ctl] = 5,
	[snd_soc_dapm_mixer] = 5,
	[snd_soc_dapm_dac] = 6,
	[snd_soc_dapm_mic] = 7,
	[snd_soc_dapm_micbias] = 8,
	[snd_soc_dapm_mux] = 9,
	[snd_soc_dapm_demux] = 9,
	[snd_soc_dapm_aif_in] = 10,
	[snd_soc_dapm_aif_out] = 10,
	[snd_soc_dapm_dai_in] = 10,
	[snd_soc_dapm_dai_out] = 10,
	[snd_soc_dapm_dai_link] = 11,
	[snd_soc_dapm_supply] = 12,
	[snd_soc_dapm_clock_supply] = 13,
	[snd_soc_dapm_regulator_supply] = 13,
	[snd_soc_dapm_post] = 14,
};

static void dapm_assert_locked(struct snd_soc_dapm_context *dapm)
{
	if (dapm->card && dapm->card->instantiated)
		lockdep_assert_held(&dapm->card->dapm_mutex);
}

static void pop_wait(u32 pop_time)
{
	if (pop_time)
		schedule_timeout_uninterruptible(msecs_to_jiffies(pop_time));
}

static void pop_dbg(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);

	if (pop_time) {
		vprintk(fmt, args);
		pop_wait();
	}

	va_end(args);
}
static void pop_dbg(struct device *dev, u32 pop_time, const char *fmt, ...)
{
	va_list args;
	char *buf;

	if (!pop_time)
		return;

	buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (buf == NULL)
		return;

	va_start(args, fmt);
	vsnprintf(buf, PAGE_SIZE, fmt, args);
	dev_info(dev, "%s", buf);
	va_end(args);

	kfree(buf);
}

static bool dapm_dirty_widget(struct snd_soc_dapm_widget *w)
{
	return !list_empty(&w->dirty);
}

static void dapm_mark_dirty(struct snd_soc_dapm_widget *w, const char *reason)
{
	dapm_assert_locked(w->dapm);

	if (!dapm_dirty_widget(w)) {
		dev_vdbg(w->dapm->dev, "Marking %s dirty due to %s\n",
			 w->name, reason);
		list_add_tail(&w->dirty, &w->dapm->card->dapm_dirty);
	}
}

/*
 * Common implementation for dapm_widget_invalidate_input_paths() and
 * dapm_widget_invalidate_output_paths(). The function is inlined since the
 * combined size of the two specialized functions is only marginally larger then
 * the size of the generic function and at the same time the fast path of the
 * specialized functions is significantly smaller than the generic function.
 */
static __always_inline void dapm_widget_invalidate_paths(
	struct snd_soc_dapm_widget *w, enum snd_soc_dapm_direction dir)
{
	enum snd_soc_dapm_direction rdir = SND_SOC_DAPM_DIR_REVERSE(dir);
	struct snd_soc_dapm_widget *node;
	struct snd_soc_dapm_path *p;
	LIST_HEAD(list);

	dapm_assert_locked(w->dapm);

	if (w->endpoints[dir] == -1)
		return;

	list_add_tail(&w->work_list, &list);
	w->endpoints[dir] = -1;

	list_for_each_entry(w, &list, work_list) {
		snd_soc_dapm_widget_for_each_path(w, dir, p) {
			if (p->is_supply || p->weak || !p->connect)
				continue;
			node = p->node[rdir];
			if (node->endpoints[dir] != -1) {
				node->endpoints[dir] = -1;
				list_add_tail(&node->work_list, &list);
			}
		}
	}
}

/*
 * dapm_widget_invalidate_input_paths() - Invalidate the cached number of
 *  input paths
 * @w: The widget for which to invalidate the cached number of input paths
 *
 * Resets the cached number of inputs for the specified widget and all widgets
 * that can be reached via outcoming paths from the widget.
 *
 * This function must be called if the number of output paths for a widget might
 * have changed. E.g. if the source state of a widget changes or a path is added
 * or activated with the widget as the sink.
 */
static void dapm_widget_invalidate_input_paths(struct snd_soc_dapm_widget *w)
{
	dapm_widget_invalidate_paths(w, SND_SOC_DAPM_DIR_IN);
}

/*
 * dapm_widget_invalidate_output_paths() - Invalidate the cached number of
 *  output paths
 * @w: The widget for which to invalidate the cached number of output paths
 *
 * Resets the cached number of outputs for the specified widget and all widgets
 * that can be reached via incoming paths from the widget.
 *
 * This function must be called if the number of output paths for a widget might
 * have changed. E.g. if the sink state of a widget changes or a path is added
 * or activated with the widget as the source.
 */
static void dapm_widget_invalidate_output_paths(struct snd_soc_dapm_widget *w)
{
	dapm_widget_invalidate_paths(w, SND_SOC_DAPM_DIR_OUT);
}

/*
 * dapm_path_invalidate() - Invalidates the cached number of inputs and outputs
 *  for the widgets connected to a path
 * @p: The path to invalidate
 *
 * Resets the cached number of inputs for the sink of the path and the cached
 * number of outputs for the source of the path.
 *
 * This function must be called when a path is added, removed or the connected
 * state changes.
 */
static void dapm_path_invalidate(struct snd_soc_dapm_path *p)
{
	/*
	 * Weak paths or supply paths do not influence the number of input or
	 * output paths of their neighbors.
	 */
	if (p->weak || p->is_supply)
		return;

	/*
	 * The number of connected endpoints is the sum of the number of
	 * connected endpoints of all neighbors. If a node with 0 connected
	 * endpoints is either connected or disconnected that sum won't change,
	 * so there is no need to re-check the path.
	 */
	if (p->source->endpoints[SND_SOC_DAPM_DIR_IN] != 0)
		dapm_widget_invalidate_input_paths(p->sink);
	if (p->sink->endpoints[SND_SOC_DAPM_DIR_OUT] != 0)
		dapm_widget_invalidate_output_paths(p->source);
}

void dapm_mark_endpoints_dirty(struct snd_soc_card *card)
{
	struct snd_soc_dapm_widget *w;

	mutex_lock(&card->dapm_mutex);

	list_for_each_entry(w, &card->widgets, list) {
		if (w->is_ep) {
			dapm_mark_dirty(w, "Rechecking endpoints");
			if (w->is_ep & SND_SOC_DAPM_EP_SINK)
				dapm_widget_invalidate_output_paths(w);
			if (w->is_ep & SND_SOC_DAPM_EP_SOURCE)
				dapm_widget_invalidate_input_paths(w);
		}
	}

	mutex_unlock(&card->dapm_mutex);
}
EXPORT_SYMBOL_GPL(dapm_mark_endpoints_dirty);

/* create a new dapm widget */
static inline struct snd_soc_dapm_widget *dapm_cnew_widget(
	const struct snd_soc_dapm_widget *_widget)
{
	return kmemdup(_widget, sizeof(*_widget), GFP_KERNEL);
}

/* set up initial codec paths */
static void dapm_set_path_status(struct snd_soc_dapm_widget *w,
	struct snd_soc_dapm_path *p, int i)
{
	switch (w->id) {
	case snd_soc_dapm_switch:
	case snd_soc_dapm_mixer: {
		int val;
		int reg = w->kcontrols[i].private_value & 0xff;
		int shift = (w->kcontrols[i].private_value >> 8) & 0x0f;
		int mask = (w->kcontrols[i].private_value >> 16) & 0xff;
		int invert = (w->kcontrols[i].private_value >> 24) & 0x01;

		val = snd_soc_read(w->codec, reg);
		val = (val >> shift) & mask;

		if ((invert && !val) || (!invert && val))
			p->connect = 1;
		else
			p->connect = 0;
	}
	break;
	case snd_soc_dapm_mux: {
		struct soc_enum *e = (struct soc_enum *)w->kcontrols[i].private_value;
		int val, item, bitmask;

		for (bitmask = 1; bitmask < e->mask; bitmask <<= 1)
		;
		val = snd_soc_read(w->codec, e->reg);
		item = (val >> e->shift_l) & (bitmask - 1);

		p->connect = 0;
		for (i = 0; i < e->mask; i++) {
			if (!(strcmp(p->name, e->texts[i])) && item == i)
				p->connect = 1;
		}
	}
	break;
	/* does not effect routing - always connected */
	case snd_soc_dapm_pga:
	case snd_soc_dapm_output:
	case snd_soc_dapm_adc:
	case snd_soc_dapm_input:
	case snd_soc_dapm_dac:
	case snd_soc_dapm_micbias:
	case snd_soc_dapm_vmid:
		p->connect = 1;
	break;
	/* does effect routing - dynamically connected */
	case snd_soc_dapm_hp:
	case snd_soc_dapm_mic:
	case snd_soc_dapm_spk:
	case snd_soc_dapm_line:
	case snd_soc_dapm_pre:
	case snd_soc_dapm_post:
		p->connect = 0;
	break;
	}
}

/* connect mux widget to it's interconnecting audio paths */
static int dapm_connect_mux(struct snd_soc_codec *codec,
	struct snd_soc_dapm_widget *src, struct snd_soc_dapm_widget *dest,
	struct snd_soc_dapm_path *path, const char *control_name,
	const struct snd_kcontrol_new *kcontrol)
{
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	int i;

	for (i = 0; i < e->mask; i++) {
		if (!(strcmp(control_name, e->texts[i]))) {
			list_add(&path->list, &codec->dapm_paths);
			list_add(&path->list_sink, &dest->sources);
			list_add(&path->list_source, &src->sinks);
			path->name = (char*)e->texts[i];
			dapm_set_path_status(dest, path, 0);
struct dapm_kcontrol_data {
	unsigned int value;
	struct snd_soc_dapm_widget *widget;
	struct list_head paths;
	struct snd_soc_dapm_widget_list *wlist;
};

static int dapm_kcontrol_data_alloc(struct snd_soc_dapm_widget *widget,
	struct snd_kcontrol *kcontrol)
{
	struct dapm_kcontrol_data *data;
	struct soc_mixer_control *mc;
	struct soc_enum *e;
	const char *name;
	int ret;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	INIT_LIST_HEAD(&data->paths);

	switch (widget->id) {
	case snd_soc_dapm_switch:
	case snd_soc_dapm_mixer:
	case snd_soc_dapm_mixer_named_ctl:
		mc = (struct soc_mixer_control *)kcontrol->private_value;

		if (mc->autodisable) {
			struct snd_soc_dapm_widget template;

			name = kasprintf(GFP_KERNEL, "%s %s", kcontrol->id.name,
					 "Autodisable");
			if (!name) {
				ret = -ENOMEM;
				goto err_data;
			}

			memset(&template, 0, sizeof(template));
			template.reg = mc->reg;
			template.mask = (1 << fls(mc->max)) - 1;
			template.shift = mc->shift;
			if (mc->invert)
				template.off_val = mc->max;
			else
				template.off_val = 0;
			template.on_val = template.off_val;
			template.id = snd_soc_dapm_kcontrol;
			template.name = name;

			data->value = template.on_val;

			data->widget =
				snd_soc_dapm_new_control_unlocked(widget->dapm,
				&template);
			kfree(name);
			if (IS_ERR(data->widget)) {
				ret = PTR_ERR(data->widget);
				goto err_data;
			}
			if (!data->widget) {
				ret = -ENOMEM;
				goto err_data;
			}
		}
		break;
	case snd_soc_dapm_demux:
	case snd_soc_dapm_mux:
		e = (struct soc_enum *)kcontrol->private_value;

		if (e->autodisable) {
			struct snd_soc_dapm_widget template;

			name = kasprintf(GFP_KERNEL, "%s %s", kcontrol->id.name,
					 "Autodisable");
			if (!name) {
				ret = -ENOMEM;
				goto err_data;
			}

			memset(&template, 0, sizeof(template));
			template.reg = e->reg;
			template.mask = e->mask;
			template.shift = e->shift_l;
			template.off_val = snd_soc_enum_item_to_val(e, 0);
			template.on_val = template.off_val;
			template.id = snd_soc_dapm_kcontrol;
			template.name = name;

			data->value = template.on_val;

			data->widget = snd_soc_dapm_new_control_unlocked(
						widget->dapm, &template);
			kfree(name);
			if (IS_ERR(data->widget)) {
				ret = PTR_ERR(data->widget);
				goto err_data;
			}
			if (!data->widget) {
				ret = -ENOMEM;
				goto err_data;
			}

			snd_soc_dapm_add_path(widget->dapm, data->widget,
					      widget, NULL, NULL);
		}
		break;
	default:
		break;
	}

	kcontrol->private_data = data;

	return 0;

err_data:
	kfree(data);
	return ret;
}

static void dapm_kcontrol_free(struct snd_kcontrol *kctl)
{
	struct dapm_kcontrol_data *data = snd_kcontrol_chip(kctl);

	list_del(&data->paths);
	kfree(data->wlist);
	kfree(data);
}

static struct snd_soc_dapm_widget_list *dapm_kcontrol_get_wlist(
	const struct snd_kcontrol *kcontrol)
{
	struct dapm_kcontrol_data *data = snd_kcontrol_chip(kcontrol);

	return data->wlist;
}

static int dapm_kcontrol_add_widget(struct snd_kcontrol *kcontrol,
	struct snd_soc_dapm_widget *widget)
{
	struct dapm_kcontrol_data *data = snd_kcontrol_chip(kcontrol);
	struct snd_soc_dapm_widget_list *new_wlist;
	unsigned int n;

	if (data->wlist)
		n = data->wlist->num_widgets + 1;
	else
		n = 1;

	new_wlist = krealloc(data->wlist,
			sizeof(*new_wlist) + sizeof(widget) * n, GFP_KERNEL);
	if (!new_wlist)
		return -ENOMEM;

	new_wlist->widgets[n - 1] = widget;
	new_wlist->num_widgets = n;

	data->wlist = new_wlist;

	return 0;
}

static void dapm_kcontrol_add_path(const struct snd_kcontrol *kcontrol,
	struct snd_soc_dapm_path *path)
{
	struct dapm_kcontrol_data *data = snd_kcontrol_chip(kcontrol);

	list_add_tail(&path->list_kcontrol, &data->paths);
}

static bool dapm_kcontrol_is_powered(const struct snd_kcontrol *kcontrol)
{
	struct dapm_kcontrol_data *data = snd_kcontrol_chip(kcontrol);

	if (!data->widget)
		return true;

	return data->widget->power;
}

static struct list_head *dapm_kcontrol_get_path_list(
	const struct snd_kcontrol *kcontrol)
{
	struct dapm_kcontrol_data *data = snd_kcontrol_chip(kcontrol);

	return &data->paths;
}

#define dapm_kcontrol_for_each_path(path, kcontrol) \
	list_for_each_entry(path, dapm_kcontrol_get_path_list(kcontrol), \
		list_kcontrol)

unsigned int dapm_kcontrol_get_value(const struct snd_kcontrol *kcontrol)
{
	struct dapm_kcontrol_data *data = snd_kcontrol_chip(kcontrol);

	return data->value;
}
EXPORT_SYMBOL_GPL(dapm_kcontrol_get_value);

static bool dapm_kcontrol_set_value(const struct snd_kcontrol *kcontrol,
	unsigned int value)
{
	struct dapm_kcontrol_data *data = snd_kcontrol_chip(kcontrol);

	if (data->value == value)
		return false;

	if (data->widget) {
		switch (dapm_kcontrol_get_wlist(kcontrol)->widgets[0]->id) {
		case snd_soc_dapm_switch:
		case snd_soc_dapm_mixer:
		case snd_soc_dapm_mixer_named_ctl:
			data->widget->on_val = value & data->widget->mask;
			break;
		case snd_soc_dapm_demux:
		case snd_soc_dapm_mux:
			data->widget->on_val = value >> data->widget->shift;
			break;
		default:
			data->widget->on_val = value;
			break;
		}
	}

	data->value = value;

	return true;
}

/**
 * snd_soc_dapm_kcontrol_widget() - Returns the widget associated to a
 *   kcontrol
 * @kcontrol: The kcontrol
 */
struct snd_soc_dapm_widget *snd_soc_dapm_kcontrol_widget(
				struct snd_kcontrol *kcontrol)
{
	return dapm_kcontrol_get_wlist(kcontrol)->widgets[0];
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_kcontrol_widget);

/**
 * snd_soc_dapm_kcontrol_dapm() - Returns the dapm context associated to a
 *  kcontrol
 * @kcontrol: The kcontrol
 *
 * Note: This function must only be used on kcontrols that are known to have
 * been registered for a CODEC. Otherwise the behaviour is undefined.
 */
struct snd_soc_dapm_context *snd_soc_dapm_kcontrol_dapm(
	struct snd_kcontrol *kcontrol)
{
	return dapm_kcontrol_get_wlist(kcontrol)->widgets[0]->dapm;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_kcontrol_dapm);

static void dapm_reset(struct snd_soc_card *card)
{
	struct snd_soc_dapm_widget *w;

	lockdep_assert_held(&card->dapm_mutex);

	memset(&card->dapm_stats, 0, sizeof(card->dapm_stats));

	list_for_each_entry(w, &card->widgets, list) {
		w->new_power = w->power;
		w->power_checked = false;
	}
}

static const char *soc_dapm_prefix(struct snd_soc_dapm_context *dapm)
{
	if (!dapm->component)
		return NULL;
	return dapm->component->name_prefix;
}

static int soc_dapm_read(struct snd_soc_dapm_context *dapm, int reg,
	unsigned int *value)
{
	if (!dapm->component)
		return -EIO;
	return snd_soc_component_read(dapm->component, reg, value);
}

static int soc_dapm_update_bits(struct snd_soc_dapm_context *dapm,
	int reg, unsigned int mask, unsigned int value)
{
	if (!dapm->component)
		return -EIO;
	return snd_soc_component_update_bits(dapm->component, reg,
					     mask, value);
}

static int soc_dapm_test_bits(struct snd_soc_dapm_context *dapm,
	int reg, unsigned int mask, unsigned int value)
{
	if (!dapm->component)
		return -EIO;
	return snd_soc_component_test_bits(dapm->component, reg, mask, value);
}

static void soc_dapm_async_complete(struct snd_soc_dapm_context *dapm)
{
	if (dapm->component)
		snd_soc_component_async_complete(dapm->component);
}

static struct snd_soc_dapm_widget *
dapm_wcache_lookup(struct snd_soc_dapm_wcache *wcache, const char *name)
{
	struct snd_soc_dapm_widget *w = wcache->widget;
	struct list_head *wlist;
	const int depth = 2;
	int i = 0;

	if (w) {
		wlist = &w->dapm->card->widgets;

		list_for_each_entry_from(w, wlist, list) {
			if (!strcmp(name, w->name))
				return w;

			if (++i == depth)
				break;
		}
	}

	return NULL;
}

static inline void dapm_wcache_update(struct snd_soc_dapm_wcache *wcache,
				      struct snd_soc_dapm_widget *w)
{
	wcache->widget = w;
}

/**
 * snd_soc_dapm_force_bias_level() - Sets the DAPM bias level
 * @dapm: The DAPM context for which to set the level
 * @level: The level to set
 *
 * Forces the DAPM bias level to a specific state. It will call the bias level
 * callback of DAPM context with the specified level. This will even happen if
 * the context is already at the same level. Furthermore it will not go through
 * the normal bias level sequencing, meaning any intermediate states between the
 * current and the target state will not be entered.
 *
 * Note that the change in bias level is only temporary and the next time
 * snd_soc_dapm_sync() is called the state will be set to the level as
 * determined by the DAPM core. The function is mainly intended to be used to
 * used during probe or resume from suspend to power up the device so
 * initialization can be done, before the DAPM core takes over.
 */
int snd_soc_dapm_force_bias_level(struct snd_soc_dapm_context *dapm,
	enum snd_soc_bias_level level)
{
	int ret = 0;

	if (dapm->set_bias_level)
		ret = dapm->set_bias_level(dapm, level);

	if (ret == 0)
		dapm->bias_level = level;

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_force_bias_level);

/**
 * snd_soc_dapm_set_bias_level - set the bias level for the system
 * @dapm: DAPM context
 * @level: level to configure
 *
 * Configure the bias (power) levels for the SoC audio device.
 *
 * Returns 0 for success else error.
 */
static int snd_soc_dapm_set_bias_level(struct snd_soc_dapm_context *dapm,
				       enum snd_soc_bias_level level)
{
	struct snd_soc_card *card = dapm->card;
	int ret = 0;

	trace_snd_soc_bias_level_start(card, level);

	if (card && card->set_bias_level)
		ret = card->set_bias_level(card, dapm, level);
	if (ret != 0)
		goto out;

	if (!card || dapm != &card->dapm)
		ret = snd_soc_dapm_force_bias_level(dapm, level);

	if (ret != 0)
		goto out;

	if (card && card->set_bias_level_post)
		ret = card->set_bias_level_post(card, dapm, level);
out:
	trace_snd_soc_bias_level_done(card, level);

	return ret;
}

/* connect mux widget to its interconnecting audio paths */
static int dapm_connect_mux(struct snd_soc_dapm_context *dapm,
	struct snd_soc_dapm_path *path, const char *control_name,
	struct snd_soc_dapm_widget *w)
{
	const struct snd_kcontrol_new *kcontrol = &w->kcontrol_news[0];
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int val, item;
	int i;

	if (e->reg != SND_SOC_NOPM) {
		soc_dapm_read(dapm, e->reg, &val);
		val = (val >> e->shift_l) & e->mask;
		item = snd_soc_enum_val_to_item(e, val);
	} else {
		/* since a virtual mux has no backing registers to
		 * decide which path to connect, it will try to match
		 * with the first enumeration.  This is to ensure
		 * that the default mux choice (the first) will be
		 * correctly powered up during initialization.
		 */
		item = 0;
	}

	for (i = 0; i < e->items; i++) {
		if (!(strcmp(control_name, e->texts[i]))) {
			path->name = e->texts[i];
			if (i == item)
				path->connect = 1;
			else
				path->connect = 0;
			return 0;
		}
	}

	return -ENODEV;
}

/* connect mixer widget to it's interconnecting audio paths */
static int dapm_connect_mixer(struct snd_soc_codec *codec,
	struct snd_soc_dapm_widget *src, struct snd_soc_dapm_widget *dest,
/* set up initial codec paths */
static void dapm_set_mixer_path_status(struct snd_soc_dapm_path *p, int i)
{
	struct soc_mixer_control *mc = (struct soc_mixer_control *)
		p->sink->kcontrol_news[i].private_value;
	unsigned int reg = mc->reg;
	unsigned int shift = mc->shift;
	unsigned int max = mc->max;
	unsigned int mask = (1 << fls(max)) - 1;
	unsigned int invert = mc->invert;
	unsigned int val;

	if (reg != SND_SOC_NOPM) {
		soc_dapm_read(p->sink->dapm, reg, &val);
		val = (val >> shift) & mask;
		if (invert)
			val = max - val;
		p->connect = !!val;
	} else {
		/* since a virtual mixer has no backing registers to
		 * decide which path to connect, it will try to match
		 * with initial state.  This is to ensure
		 * that the default mixer choice will be
		 * correctly powered up during initialization.
		 */
		p->connect = invert;
	}
}

/* connect mixer widget to its interconnecting audio paths */
static int dapm_connect_mixer(struct snd_soc_dapm_context *dapm,
	struct snd_soc_dapm_path *path, const char *control_name)
{
	int i;

	/* search for mixer kcontrol */
	for (i = 0; i < dest->num_kcontrols; i++) {
		if (!strcmp(control_name, dest->kcontrols[i].name)) {
			list_add(&path->list, &codec->dapm_paths);
			list_add(&path->list_sink, &dest->sources);
			list_add(&path->list_source, &src->sinks);
			path->name = dest->kcontrols[i].name;
			dapm_set_path_status(dest, path, i);
	for (i = 0; i < path->sink->num_kcontrols; i++) {
		if (!strcmp(control_name, path->sink->kcontrol_news[i].name)) {
			path->name = path->sink->kcontrol_news[i].name;
			dapm_set_mixer_path_status(path, i);
			return 0;
		}
	}
	return -ENODEV;
}

/* update dapm codec register bits */
static int dapm_update_bits(struct snd_soc_dapm_widget *widget)
{
	int change, power;
	unsigned short old, new;
	struct snd_soc_codec *codec = widget->codec;

	/* check for valid widgets */
	if (widget->reg < 0 || widget->id == snd_soc_dapm_input ||
		widget->id == snd_soc_dapm_output ||
		widget->id == snd_soc_dapm_hp ||
		widget->id == snd_soc_dapm_mic ||
		widget->id == snd_soc_dapm_line ||
		widget->id == snd_soc_dapm_spk)
		return 0;

	power = widget->power;
	if (widget->invert)
		power = (power ? 0:1);

	old = snd_soc_read(codec, widget->reg);
	new = (old & ~(0x1 << widget->shift)) | (power << widget->shift);

	change = old != new;
	if (change) {
		pop_dbg("pop test %s : %s in %d ms\n", widget->name,
			widget->power ? "on" : "off", pop_time);
		snd_soc_write(codec, widget->reg, new);
		pop_wait();
	}
	pr_debug("reg %x old %x new %x change %d\n", widget->reg,
		 old, new, change);
	return change;
}

/* ramps the volume up or down to minimise pops before or after a
 * DAPM power event */
static int dapm_set_pga(struct snd_soc_dapm_widget *widget, int power)
{
	const struct snd_kcontrol_new *k = widget->kcontrols;

	if (widget->muted && !power)
		return 0;
	if (!widget->muted && power)
		return 0;

	if (widget->num_kcontrols && k) {
		int reg = k->private_value & 0xff;
		int shift = (k->private_value >> 8) & 0x0f;
		int mask = (k->private_value >> 16) & 0xff;
		int invert = (k->private_value >> 24) & 0x01;

		if (power) {
			int i;
			/* power up has happended, increase volume to last level */
			if (invert) {
				for (i = mask; i > widget->saved_value; i--)
					snd_soc_update_bits(widget->codec, reg, mask, i);
			} else {
				for (i = 0; i < widget->saved_value; i++)
					snd_soc_update_bits(widget->codec, reg, mask, i);
			}
			widget->muted = 0;
		} else {
			/* power down is about to occur, decrease volume to mute */
			int val = snd_soc_read(widget->codec, reg);
			int i = widget->saved_value = (val >> shift) & mask;
			if (invert) {
				for (; i < mask; i++)
					snd_soc_update_bits(widget->codec, reg, mask, i);
			} else {
				for (; i > 0; i--)
					snd_soc_update_bits(widget->codec, reg, mask, i);
			}
			widget->muted = 1;
		}
	}
	return 0;
}

/* create new dapm mixer control */
static int dapm_new_mixer(struct snd_soc_codec *codec,
	struct snd_soc_dapm_widget *w)
{
	int i, ret = 0;
	char name[32];
	struct snd_soc_dapm_path *path;

	/* add kcontrol */
	for (i = 0; i < w->num_kcontrols; i++) {

		/* match name */
		list_for_each_entry(path, &w->sources, list_sink) {

			/* mixer/mux paths name must match control name */
			if (path->name != (char*)w->kcontrols[i].name)
				continue;

			/* add dapm control with long name */
			snprintf(name, 32, "%s %s", w->name, w->kcontrols[i].name);
			path->long_name = kstrdup (name, GFP_KERNEL);
			if (path->long_name == NULL)
				return -ENOMEM;

			path->kcontrol = snd_soc_cnew(&w->kcontrols[i], w,
				path->long_name);
			ret = snd_ctl_add(codec->card, path->kcontrol);
			if (ret < 0) {
				printk(KERN_ERR "asoc: failed to add dapm kcontrol %s\n",
						path->long_name);
				kfree(path->long_name);
				path->long_name = NULL;
				return ret;
			}
		}
	}
	return ret;
}

/* create new dapm mux control */
static int dapm_new_mux(struct snd_soc_codec *codec,
	struct snd_soc_dapm_widget *w)
{
	struct snd_soc_dapm_path *path = NULL;
	struct snd_kcontrol *kcontrol;
	int ret = 0;

	if (!w->num_kcontrols) {
		printk(KERN_ERR "asoc: mux %s has no controls\n", w->name);
		return -EINVAL;
	}

	kcontrol = snd_soc_cnew(&w->kcontrols[0], w, w->name);
	ret = snd_ctl_add(codec->card, kcontrol);
	if (ret < 0)
		goto err;

	list_for_each_entry(path, &w->sources, list_sink)
		path->kcontrol = kcontrol;

	return ret;

err:
	printk(KERN_ERR "asoc: failed to add kcontrol %s\n", w->name);
	return ret;
}

/* create new dapm volume control */
static int dapm_new_pga(struct snd_soc_codec *codec,
	struct snd_soc_dapm_widget *w)
{
	struct snd_kcontrol *kcontrol;
	int ret = 0;

	if (!w->num_kcontrols)
		return -EINVAL;

	kcontrol = snd_soc_cnew(&w->kcontrols[0], w, w->name);
	ret = snd_ctl_add(codec->card, kcontrol);
	if (ret < 0) {
		printk(KERN_ERR "asoc: failed to add kcontrol %s\n", w->name);
		return ret;
	}

	return ret;
}

/* reset 'walked' bit for each dapm path */
static inline void dapm_clear_walk(struct snd_soc_codec *codec)
{
	struct snd_soc_dapm_path *p;

	list_for_each_entry(p, &codec->dapm_paths, list)
		p->walked = 0;
static int dapm_is_shared_kcontrol(struct snd_soc_dapm_context *dapm,
	struct snd_soc_dapm_widget *kcontrolw,
	const struct snd_kcontrol_new *kcontrol_new,
	struct snd_kcontrol **kcontrol)
{
	struct snd_soc_dapm_widget *w;
	int i;

	*kcontrol = NULL;

	list_for_each_entry(w, &dapm->card->widgets, list) {
		if (w == kcontrolw || w->dapm != kcontrolw->dapm)
			continue;
		for (i = 0; i < w->num_kcontrols; i++) {
			if (&w->kcontrol_news[i] == kcontrol_new) {
				if (w->kcontrols)
					*kcontrol = w->kcontrols[i];
				return 1;
			}
		}
	}

	return 0;
}

/*
 * Determine if a kcontrol is shared. If it is, look it up. If it isn't,
 * create it. Either way, add the widget into the control's widget list
 */
static int dapm_create_or_share_kcontrol(struct snd_soc_dapm_widget *w,
	int kci)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct snd_card *card = dapm->card->snd_card;
	const char *prefix;
	size_t prefix_len;
	int shared;
	struct snd_kcontrol *kcontrol;
	bool wname_in_long_name, kcname_in_long_name;
	char *long_name = NULL;
	const char *name;
	int ret = 0;

	prefix = soc_dapm_prefix(dapm);
	if (prefix)
		prefix_len = strlen(prefix) + 1;
	else
		prefix_len = 0;

	shared = dapm_is_shared_kcontrol(dapm, w, &w->kcontrol_news[kci],
					 &kcontrol);

	if (!kcontrol) {
		if (shared) {
			wname_in_long_name = false;
			kcname_in_long_name = true;
		} else {
			switch (w->id) {
			case snd_soc_dapm_switch:
			case snd_soc_dapm_mixer:
			case snd_soc_dapm_pga:
			case snd_soc_dapm_out_drv:
				wname_in_long_name = true;
				kcname_in_long_name = true;
				break;
			case snd_soc_dapm_mixer_named_ctl:
				wname_in_long_name = false;
				kcname_in_long_name = true;
				break;
			case snd_soc_dapm_demux:
			case snd_soc_dapm_mux:
				wname_in_long_name = true;
				kcname_in_long_name = false;
				break;
			default:
				return -EINVAL;
			}
		}

		if (wname_in_long_name && kcname_in_long_name) {
			/*
			 * The control will get a prefix from the control
			 * creation process but we're also using the same
			 * prefix for widgets so cut the prefix off the
			 * front of the widget name.
			 */
			long_name = kasprintf(GFP_KERNEL, "%s %s",
				 w->name + prefix_len,
				 w->kcontrol_news[kci].name);
			if (long_name == NULL)
				return -ENOMEM;

			name = long_name;
		} else if (wname_in_long_name) {
			long_name = NULL;
			name = w->name + prefix_len;
		} else {
			long_name = NULL;
			name = w->kcontrol_news[kci].name;
		}

		kcontrol = snd_soc_cnew(&w->kcontrol_news[kci], NULL, name,
					prefix);
		if (!kcontrol) {
			ret = -ENOMEM;
			goto exit_free;
		}

		kcontrol->private_free = dapm_kcontrol_free;

		ret = dapm_kcontrol_data_alloc(w, kcontrol);
		if (ret) {
			snd_ctl_free_one(kcontrol);
			goto exit_free;
		}

		ret = snd_ctl_add(card, kcontrol);
		if (ret < 0) {
			dev_err(dapm->dev,
				"ASoC: failed to add widget %s dapm kcontrol %s: %d\n",
				w->name, name, ret);
			goto exit_free;
		}
	}

	ret = dapm_kcontrol_add_widget(kcontrol, w);
	if (ret == 0)
		w->kcontrols[kci] = kcontrol;

exit_free:
	kfree(long_name);

	return ret;
}

/* create new dapm mixer control */
static int dapm_new_mixer(struct snd_soc_dapm_widget *w)
{
	int i, ret;
	struct snd_soc_dapm_path *path;
	struct dapm_kcontrol_data *data;

	/* add kcontrol */
	for (i = 0; i < w->num_kcontrols; i++) {
		/* match name */
		snd_soc_dapm_widget_for_each_source_path(w, path) {
			/* mixer/mux paths name must match control name */
			if (path->name != (char *)w->kcontrol_news[i].name)
				continue;

			if (!w->kcontrols[i]) {
				ret = dapm_create_or_share_kcontrol(w, i);
				if (ret < 0)
					return ret;
			}

			dapm_kcontrol_add_path(w->kcontrols[i], path);

			data = snd_kcontrol_chip(w->kcontrols[i]);
			if (data->widget)
				snd_soc_dapm_add_path(data->widget->dapm,
						      data->widget,
						      path->source,
						      NULL, NULL);
		}
	}

	return 0;
}

/* create new dapm mux control */
static int dapm_new_mux(struct snd_soc_dapm_widget *w)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	enum snd_soc_dapm_direction dir;
	struct snd_soc_dapm_path *path;
	const char *type;
	int ret;

	switch (w->id) {
	case snd_soc_dapm_mux:
		dir = SND_SOC_DAPM_DIR_OUT;
		type = "mux";
		break;
	case snd_soc_dapm_demux:
		dir = SND_SOC_DAPM_DIR_IN;
		type = "demux";
		break;
	default:
		return -EINVAL;
	}

	if (w->num_kcontrols != 1) {
		dev_err(dapm->dev,
			"ASoC: %s %s has incorrect number of controls\n", type,
			w->name);
		return -EINVAL;
	}

	if (list_empty(&w->edges[dir])) {
		dev_err(dapm->dev, "ASoC: %s %s has no paths\n", type, w->name);
		return -EINVAL;
	}

	ret = dapm_create_or_share_kcontrol(w, 0);
	if (ret < 0)
		return ret;

	snd_soc_dapm_widget_for_each_path(w, dir, path) {
		if (path->name)
			dapm_kcontrol_add_path(w->kcontrols[0], path);
	}

	return 0;
}

/* create new dapm volume control */
static int dapm_new_pga(struct snd_soc_dapm_widget *w)
{
	int i, ret;

	for (i = 0; i < w->num_kcontrols; i++) {
		ret = dapm_create_or_share_kcontrol(w, i);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/* create new dapm dai link control */
static int dapm_new_dai_link(struct snd_soc_dapm_widget *w)
{
	int i, ret;
	struct snd_kcontrol *kcontrol;
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct snd_card *card = dapm->card->snd_card;

	/* create control for links with > 1 config */
	if (w->num_params <= 1)
		return 0;

	/* add kcontrol */
	for (i = 0; i < w->num_kcontrols; i++) {
		kcontrol = snd_soc_cnew(&w->kcontrol_news[i], w,
					w->name, NULL);
		ret = snd_ctl_add(card, kcontrol);
		if (ret < 0) {
			dev_err(dapm->dev,
				"ASoC: failed to add widget %s dapm kcontrol %s: %d\n",
				w->name, w->kcontrol_news[i].name, ret);
			return ret;
		}
		kcontrol->private_data = w;
		w->kcontrols[i] = kcontrol;
	}

	return 0;
}

/* We implement power down on suspend by checking the power state of
 * the ALSA card - when we are suspending the ALSA state for the card
 * is set to D3.
 */
static int snd_soc_dapm_suspend_check(struct snd_soc_dapm_widget *widget)
{
	int level = snd_power_get_state(widget->dapm->card->snd_card);

	switch (level) {
	case SNDRV_CTL_POWER_D3hot:
	case SNDRV_CTL_POWER_D3cold:
		if (widget->ignore_suspend)
			dev_dbg(widget->dapm->dev, "ASoC: %s ignoring suspend\n",
				widget->name);
		return widget->ignore_suspend;
	default:
		return 1;
	}
}

static int dapm_widget_list_create(struct snd_soc_dapm_widget_list **list,
	struct list_head *widgets)
{
	struct snd_soc_dapm_widget *w;
	struct list_head *it;
	unsigned int size = 0;
	unsigned int i = 0;

	list_for_each(it, widgets)
		size++;

	*list = kzalloc(sizeof(**list) + size * sizeof(*w), GFP_KERNEL);
	if (*list == NULL)
		return -ENOMEM;

	list_for_each_entry(w, widgets, work_list)
		(*list)->widgets[i++] = w;

	(*list)->num_widgets = i;

	return 0;
}

/*
 * Common implementation for is_connected_output_ep() and
 * is_connected_input_ep(). The function is inlined since the combined size of
 * the two specialized functions is only marginally larger then the size of the
 * generic function and at the same time the fast path of the specialized
 * functions is significantly smaller than the generic function.
 */
static __always_inline int is_connected_ep(struct snd_soc_dapm_widget *widget,
	struct list_head *list, enum snd_soc_dapm_direction dir,
	int (*fn)(struct snd_soc_dapm_widget *, struct list_head *))
{
	enum snd_soc_dapm_direction rdir = SND_SOC_DAPM_DIR_REVERSE(dir);
	struct snd_soc_dapm_path *path;
	int con = 0;

	if (widget->endpoints[dir] >= 0)
		return widget->endpoints[dir];

	DAPM_UPDATE_STAT(widget, path_checks);

	/* do we need to add this widget to the list ? */
	if (list)
		list_add_tail(&widget->work_list, list);

	if ((widget->is_ep & SND_SOC_DAPM_DIR_TO_EP(dir)) && widget->connected) {
		widget->endpoints[dir] = snd_soc_dapm_suspend_check(widget);
		return widget->endpoints[dir];
	}

	snd_soc_dapm_widget_for_each_path(widget, rdir, path) {
		DAPM_UPDATE_STAT(widget, neighbour_checks);

		if (path->weak || path->is_supply)
			continue;

		if (path->walking)
			return 1;

		trace_snd_soc_dapm_path(widget, dir, path);

		if (path->connect) {
			path->walking = 1;
			con += fn(path->node[dir], list);
			path->walking = 0;
		}
	}

	widget->endpoints[dir] = con;

	return con;
}

/*
 * Recursively check for a completed path to an active or physically connected
 * output widget. Returns number of complete paths.
 */
static int is_connected_output_ep(struct snd_soc_dapm_widget *widget)
{
	struct snd_soc_dapm_path *path;
	int con = 0;

	if (widget->id == snd_soc_dapm_adc && widget->active)
		return 1;

	if (widget->connected) {
		/* connected pin ? */
		if (widget->id == snd_soc_dapm_output && !widget->ext)
			return 1;

		/* connected jack or spk ? */
		if (widget->id == snd_soc_dapm_hp || widget->id == snd_soc_dapm_spk ||
			widget->id == snd_soc_dapm_line)
			return 1;
	}

	list_for_each_entry(path, &widget->sinks, list_source) {
		if (path->walked)
			continue;

		if (path->sink && path->connect) {
			path->walked = 1;
			con += is_connected_output_ep(path->sink);
		}
	}

	return con;
static int is_connected_output_ep(struct snd_soc_dapm_widget *widget,
	struct list_head *list)
{
	return is_connected_ep(widget, list, SND_SOC_DAPM_DIR_OUT,
			is_connected_output_ep);
}

/*
 * Recursively check for a completed path to an active or physically connected
 * input widget. Returns number of complete paths.
 */
static int is_connected_input_ep(struct snd_soc_dapm_widget *widget)
{
	struct snd_soc_dapm_path *path;
	int con = 0;

	/* active stream ? */
	if (widget->id == snd_soc_dapm_dac && widget->active)
		return 1;

	if (widget->connected) {
		/* connected pin ? */
		if (widget->id == snd_soc_dapm_input && !widget->ext)
			return 1;

		/* connected VMID/Bias for lower pops */
		if (widget->id == snd_soc_dapm_vmid)
			return 1;

		/* connected jack ? */
		if (widget->id == snd_soc_dapm_mic || widget->id == snd_soc_dapm_line)
			return 1;
	}

	list_for_each_entry(path, &widget->sources, list_sink) {
		if (path->walked)
			continue;

		if (path->source && path->connect) {
			path->walked = 1;
			con += is_connected_input_ep(path->source);
		}
	}

	return con;
}

/*
 * Handler for generic register modifier widget.
 */
int dapm_reg_event(struct snd_soc_dapm_widget *w,
		   struct snd_kcontrol *kcontrol, int event)
{
	unsigned int val;

	if (SND_SOC_DAPM_EVENT_ON(event))
		val = w->on_val;
	else
		val = w->off_val;

	snd_soc_update_bits(w->codec, -(w->reg + 1),
			    w->mask << w->shift, val << w->shift);

	return 0;
}
EXPORT_SYMBOL_GPL(dapm_reg_event);
static int is_connected_input_ep(struct snd_soc_dapm_widget *widget,
	struct list_head *list)
{
	return is_connected_ep(widget, list, SND_SOC_DAPM_DIR_IN,
			is_connected_input_ep);
}

/**
 * snd_soc_dapm_get_connected_widgets - query audio path and it's widgets.
 * @dai: the soc DAI.
 * @stream: stream direction.
 * @list: list of active widgets for this stream.
 *
 * Queries DAPM graph as to whether an valid audio stream path exists for
 * the initial stream specified by name. This takes into account
 * current mixer and mux kcontrol settings. Creates list of valid widgets.
 *
 * Returns the number of valid paths or negative error.
 */
int snd_soc_dapm_dai_get_connected_widgets(struct snd_soc_dai *dai, int stream,
	struct snd_soc_dapm_widget_list **list)
{
	struct snd_soc_card *card = dai->component->card;
	struct snd_soc_dapm_widget *w;
	LIST_HEAD(widgets);
	int paths;
	int ret;

	mutex_lock_nested(&card->dapm_mutex, SND_SOC_DAPM_CLASS_RUNTIME);

	/*
	 * For is_connected_{output,input}_ep fully discover the graph we need
	 * to reset the cached number of inputs and outputs.
	 */
	list_for_each_entry(w, &card->widgets, list) {
		w->endpoints[SND_SOC_DAPM_DIR_IN] = -1;
		w->endpoints[SND_SOC_DAPM_DIR_OUT] = -1;
	}

	if (stream == SNDRV_PCM_STREAM_PLAYBACK)
		paths = is_connected_output_ep(dai->playback_widget, &widgets);
	else
		paths = is_connected_input_ep(dai->capture_widget, &widgets);

	/* Drop starting point */
	list_del(widgets.next);

	ret = dapm_widget_list_create(list, &widgets);
	if (ret)
		paths = ret;

	trace_snd_soc_dapm_connected(paths, stream);
	mutex_unlock(&card->dapm_mutex);

	return paths;
}

/*
 * Handler for regulator supply widget.
 */
int dapm_regulator_event(struct snd_soc_dapm_widget *w,
		   struct snd_kcontrol *kcontrol, int event)
{
	int ret;

	soc_dapm_async_complete(w->dapm);

	if (SND_SOC_DAPM_EVENT_ON(event)) {
		if (w->on_val & SND_SOC_DAPM_REGULATOR_BYPASS) {
			ret = regulator_allow_bypass(w->regulator, false);
			if (ret != 0)
				dev_warn(w->dapm->dev,
					 "ASoC: Failed to unbypass %s: %d\n",
					 w->name, ret);
		}

		return regulator_enable(w->regulator);
	} else {
		if (w->on_val & SND_SOC_DAPM_REGULATOR_BYPASS) {
			ret = regulator_allow_bypass(w->regulator, true);
			if (ret != 0)
				dev_warn(w->dapm->dev,
					 "ASoC: Failed to bypass %s: %d\n",
					 w->name, ret);
		}

		return regulator_disable_deferred(w->regulator, w->shift);
	}
}
EXPORT_SYMBOL_GPL(dapm_regulator_event);

/*
 * Handler for clock supply widget.
 */
int dapm_clock_event(struct snd_soc_dapm_widget *w,
		   struct snd_kcontrol *kcontrol, int event)
{
	if (!w->clk)
		return -EIO;

	soc_dapm_async_complete(w->dapm);

#ifdef CONFIG_HAVE_CLK
	if (SND_SOC_DAPM_EVENT_ON(event)) {
		return clk_prepare_enable(w->clk);
	} else {
		clk_disable_unprepare(w->clk);
		return 0;
	}
#endif
	return 0;
}
EXPORT_SYMBOL_GPL(dapm_clock_event);

static int dapm_widget_power_check(struct snd_soc_dapm_widget *w)
{
	if (w->power_checked)
		return w->new_power;

	if (w->force)
		w->new_power = 1;
	else
		w->new_power = w->power_check(w);

	w->power_checked = true;

	return w->new_power;
}

/* Generic check to see if a widget should be powered.
 */
static int dapm_generic_check_power(struct snd_soc_dapm_widget *w)
{
	int in, out;

	DAPM_UPDATE_STAT(w, power_checks);

	in = is_connected_input_ep(w, NULL);
	out = is_connected_output_ep(w, NULL);
	return out != 0 && in != 0;
}

/* Check to see if a power supply is needed */
static int dapm_supply_check_power(struct snd_soc_dapm_widget *w)
{
	struct snd_soc_dapm_path *path;

	DAPM_UPDATE_STAT(w, power_checks);

	/* Check if one of our outputs is connected */
	snd_soc_dapm_widget_for_each_sink_path(w, path) {
		DAPM_UPDATE_STAT(w, neighbour_checks);

		if (path->weak)
			continue;

		if (path->connected &&
		    !path->connected(path->source, path->sink))
			continue;

		if (dapm_widget_power_check(path->sink))
			return 1;
	}

	return 0;
}

static int dapm_always_on_check_power(struct snd_soc_dapm_widget *w)
{
	return 1;
}

static int dapm_seq_compare(struct snd_soc_dapm_widget *a,
			    struct snd_soc_dapm_widget *b,
			    bool power_up)
{
	int *sort;

	if (power_up)
		sort = dapm_up_seq;
	else
		sort = dapm_down_seq;

	if (sort[a->id] != sort[b->id])
		return sort[a->id] - sort[b->id];
	if (a->subseq != b->subseq) {
		if (power_up)
			return a->subseq - b->subseq;
		else
			return b->subseq - a->subseq;
	}
	if (a->reg != b->reg)
		return a->reg - b->reg;
	if (a->dapm != b->dapm)
		return (unsigned long)a->dapm - (unsigned long)b->dapm;

	return 0;
}

/* Insert a widget in order into a DAPM power sequence. */
static void dapm_seq_insert(struct snd_soc_dapm_widget *new_widget,
			    struct list_head *list,
			    bool power_up)
{
	struct snd_soc_dapm_widget *w;

	list_for_each_entry(w, list, power_list)
		if (dapm_seq_compare(new_widget, w, power_up) < 0) {
			list_add_tail(&new_widget->power_list, &w->power_list);
			return;
		}

	list_add_tail(&new_widget->power_list, list);
}

static void dapm_seq_check_event(struct snd_soc_card *card,
				 struct snd_soc_dapm_widget *w, int event)
{
	const char *ev_name;
	int power, ret;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		ev_name = "PRE_PMU";
		power = 1;
		break;
	case SND_SOC_DAPM_POST_PMU:
		ev_name = "POST_PMU";
		power = 1;
		break;
	case SND_SOC_DAPM_PRE_PMD:
		ev_name = "PRE_PMD";
		power = 0;
		break;
	case SND_SOC_DAPM_POST_PMD:
		ev_name = "POST_PMD";
		power = 0;
		break;
	case SND_SOC_DAPM_WILL_PMU:
		ev_name = "WILL_PMU";
		power = 1;
		break;
	case SND_SOC_DAPM_WILL_PMD:
		ev_name = "WILL_PMD";
		power = 0;
		break;
	default:
		WARN(1, "Unknown event %d\n", event);
		return;
	}

	if (w->new_power != power)
		return;

	if (w->event && (w->event_flags & event)) {
		pop_dbg(w->dapm->dev, card->pop_time, "pop test : %s %s\n",
			w->name, ev_name);
		soc_dapm_async_complete(w->dapm);
		trace_snd_soc_dapm_widget_event_start(w, event);
		ret = w->event(w, NULL, event);
		trace_snd_soc_dapm_widget_event_done(w, event);
		if (ret < 0)
			dev_err(w->dapm->dev, "ASoC: %s: %s event failed: %d\n",
			       ev_name, w->name, ret);
	}
}

/* Apply the coalesced changes from a DAPM sequence */
static void dapm_seq_run_coalesced(struct snd_soc_card *card,
				   struct list_head *pending)
{
	struct snd_soc_dapm_context *dapm;
	struct snd_soc_dapm_widget *w;
	int reg;
	unsigned int value = 0;
	unsigned int mask = 0;

	w = list_first_entry(pending, struct snd_soc_dapm_widget, power_list);
	reg = w->reg;
	dapm = w->dapm;

	list_for_each_entry(w, pending, power_list) {
		WARN_ON(reg != w->reg || dapm != w->dapm);
		w->power = w->new_power;

		mask |= w->mask << w->shift;
		if (w->power)
			value |= w->on_val << w->shift;
		else
			value |= w->off_val << w->shift;

		pop_dbg(dapm->dev, card->pop_time,
			"pop test : Queue %s: reg=0x%x, 0x%x/0x%x\n",
			w->name, reg, value, mask);

		/* Check for events */
		dapm_seq_check_event(card, w, SND_SOC_DAPM_PRE_PMU);
		dapm_seq_check_event(card, w, SND_SOC_DAPM_PRE_PMD);
	}

	if (reg >= 0) {
		/* Any widget will do, they should all be updating the
		 * same register.
		 */

		pop_dbg(dapm->dev, card->pop_time,
			"pop test : Applying 0x%x/0x%x to %x in %dms\n",
			value, mask, reg, card->pop_time);
		pop_wait(card->pop_time);
		soc_dapm_update_bits(dapm, reg, mask, value);
	}

	list_for_each_entry(w, pending, power_list) {
		dapm_seq_check_event(card, w, SND_SOC_DAPM_POST_PMU);
		dapm_seq_check_event(card, w, SND_SOC_DAPM_POST_PMD);
	}
}

/* Apply a DAPM power sequence.
 *
 * We walk over a pre-sorted list of widgets to apply power to.  In
 * order to minimise the number of writes to the device required
 * multiple widgets will be updated in a single write where possible.
 * Currently anything that requires more than a single write is not
 * handled.
 */
static void dapm_seq_run(struct snd_soc_card *card,
	struct list_head *list, int event, bool power_up)
{
	struct snd_soc_dapm_widget *w, *n;
	struct snd_soc_dapm_context *d;
	LIST_HEAD(pending);
	int cur_sort = -1;
	int cur_subseq = -1;
	int cur_reg = SND_SOC_NOPM;
	struct snd_soc_dapm_context *cur_dapm = NULL;
	int ret, i;
	int *sort;

	if (power_up)
		sort = dapm_up_seq;
	else
		sort = dapm_down_seq;

	list_for_each_entry_safe(w, n, list, power_list) {
		ret = 0;

		/* Do we need to apply any queued changes? */
		if (sort[w->id] != cur_sort || w->reg != cur_reg ||
		    w->dapm != cur_dapm || w->subseq != cur_subseq) {
			if (!list_empty(&pending))
				dapm_seq_run_coalesced(card, &pending);

			if (cur_dapm && cur_dapm->seq_notifier) {
				for (i = 0; i < ARRAY_SIZE(dapm_up_seq); i++)
					if (sort[i] == cur_sort)
						cur_dapm->seq_notifier(cur_dapm,
								       i,
								       cur_subseq);
			}

			if (cur_dapm && w->dapm != cur_dapm)
				soc_dapm_async_complete(cur_dapm);

			INIT_LIST_HEAD(&pending);
			cur_sort = -1;
			cur_subseq = INT_MIN;
			cur_reg = SND_SOC_NOPM;
			cur_dapm = NULL;
		}

		switch (w->id) {
		case snd_soc_dapm_pre:
			if (!w->event)
				continue;

			if (event == SND_SOC_DAPM_STREAM_START)
				ret = w->event(w,
					       NULL, SND_SOC_DAPM_PRE_PMU);
			else if (event == SND_SOC_DAPM_STREAM_STOP)
				ret = w->event(w,
					       NULL, SND_SOC_DAPM_PRE_PMD);
			break;

		case snd_soc_dapm_post:
			if (!w->event)
				continue;

			if (event == SND_SOC_DAPM_STREAM_START)
				ret = w->event(w,
					       NULL, SND_SOC_DAPM_POST_PMU);
			else if (event == SND_SOC_DAPM_STREAM_STOP)
				ret = w->event(w,
					       NULL, SND_SOC_DAPM_POST_PMD);
			break;

		default:
			/* Queue it up for application */
			cur_sort = sort[w->id];
			cur_subseq = w->subseq;
			cur_reg = w->reg;
			cur_dapm = w->dapm;
			list_move(&w->power_list, &pending);
			break;
		}

		if (ret < 0)
			dev_err(w->dapm->dev,
				"ASoC: Failed to apply widget power: %d\n", ret);
	}

	if (!list_empty(&pending))
		dapm_seq_run_coalesced(card, &pending);

	if (cur_dapm && cur_dapm->seq_notifier) {
		for (i = 0; i < ARRAY_SIZE(dapm_up_seq); i++)
			if (sort[i] == cur_sort)
				cur_dapm->seq_notifier(cur_dapm,
						       i, cur_subseq);
	}

	list_for_each_entry(d, &card->dapm_list, list) {
		soc_dapm_async_complete(d);
	}
}

static void dapm_widget_update(struct snd_soc_card *card)
{
	struct snd_soc_dapm_update *update = card->update;
	struct snd_soc_dapm_widget_list *wlist;
	struct snd_soc_dapm_widget *w = NULL;
	unsigned int wi;
	int ret;

	if (!update || !dapm_kcontrol_is_powered(update->kcontrol))
		return;

	wlist = dapm_kcontrol_get_wlist(update->kcontrol);

	for (wi = 0; wi < wlist->num_widgets; wi++) {
		w = wlist->widgets[wi];

		if (w->event && (w->event_flags & SND_SOC_DAPM_PRE_REG)) {
			ret = w->event(w, update->kcontrol, SND_SOC_DAPM_PRE_REG);
			if (ret != 0)
				dev_err(w->dapm->dev, "ASoC: %s DAPM pre-event failed: %d\n",
					   w->name, ret);
		}
	}

	if (!w)
		return;

	ret = soc_dapm_update_bits(w->dapm, update->reg, update->mask,
		update->val);
	if (ret < 0)
		dev_err(w->dapm->dev, "ASoC: %s DAPM update failed: %d\n",
			w->name, ret);

	for (wi = 0; wi < wlist->num_widgets; wi++) {
		w = wlist->widgets[wi];

		if (w->event && (w->event_flags & SND_SOC_DAPM_POST_REG)) {
			ret = w->event(w, update->kcontrol, SND_SOC_DAPM_POST_REG);
			if (ret != 0)
				dev_err(w->dapm->dev, "ASoC: %s DAPM post-event failed: %d\n",
					   w->name, ret);
		}
	}
}

/* Async callback run prior to DAPM sequences - brings to _PREPARE if
 * they're changing state.
 */
static void dapm_pre_sequence_async(void *data, async_cookie_t cookie)
{
	struct snd_soc_dapm_context *d = data;
	int ret;

	/* If we're off and we're not supposed to be go into STANDBY */
	if (d->bias_level == SND_SOC_BIAS_OFF &&
	    d->target_bias_level != SND_SOC_BIAS_OFF) {
		if (d->dev)
			pm_runtime_get_sync(d->dev);

		ret = snd_soc_dapm_set_bias_level(d, SND_SOC_BIAS_STANDBY);
		if (ret != 0)
			dev_err(d->dev,
				"ASoC: Failed to turn on bias: %d\n", ret);
	}

	/* Prepare for a transition to ON or away from ON */
	if ((d->target_bias_level == SND_SOC_BIAS_ON &&
	     d->bias_level != SND_SOC_BIAS_ON) ||
	    (d->target_bias_level != SND_SOC_BIAS_ON &&
	     d->bias_level == SND_SOC_BIAS_ON)) {
		ret = snd_soc_dapm_set_bias_level(d, SND_SOC_BIAS_PREPARE);
		if (ret != 0)
			dev_err(d->dev,
				"ASoC: Failed to prepare bias: %d\n", ret);
	}
}

/* Async callback run prior to DAPM sequences - brings to their final
 * state.
 */
static void dapm_post_sequence_async(void *data, async_cookie_t cookie)
{
	struct snd_soc_dapm_context *d = data;
	int ret;

	/* If we just powered the last thing off drop to standby bias */
	if (d->bias_level == SND_SOC_BIAS_PREPARE &&
	    (d->target_bias_level == SND_SOC_BIAS_STANDBY ||
	     d->target_bias_level == SND_SOC_BIAS_OFF)) {
		ret = snd_soc_dapm_set_bias_level(d, SND_SOC_BIAS_STANDBY);
		if (ret != 0)
			dev_err(d->dev, "ASoC: Failed to apply standby bias: %d\n",
				ret);
	}

	/* If we're in standby and can support bias off then do that */
	if (d->bias_level == SND_SOC_BIAS_STANDBY &&
	    d->target_bias_level == SND_SOC_BIAS_OFF) {
		ret = snd_soc_dapm_set_bias_level(d, SND_SOC_BIAS_OFF);
		if (ret != 0)
			dev_err(d->dev, "ASoC: Failed to turn off bias: %d\n",
				ret);

		if (d->dev)
			pm_runtime_put(d->dev);
	}

	/* If we just powered up then move to active bias */
	if (d->bias_level == SND_SOC_BIAS_PREPARE &&
	    d->target_bias_level == SND_SOC_BIAS_ON) {
		ret = snd_soc_dapm_set_bias_level(d, SND_SOC_BIAS_ON);
		if (ret != 0)
			dev_err(d->dev, "ASoC: Failed to apply active bias: %d\n",
				ret);
	}
}

static void dapm_widget_set_peer_power(struct snd_soc_dapm_widget *peer,
				       bool power, bool connect)
{
	/* If a connection is being made or broken then that update
	 * will have marked the peer dirty, otherwise the widgets are
	 * not connected and this update has no impact. */
	if (!connect)
		return;

	/* If the peer is already in the state we're moving to then we
	 * won't have an impact on it. */
	if (power != peer->power)
		dapm_mark_dirty(peer, "peer state change");
}

static void dapm_widget_set_power(struct snd_soc_dapm_widget *w, bool power,
				  struct list_head *up_list,
				  struct list_head *down_list)
{
	struct snd_soc_dapm_path *path;

	if (w->power == power)
		return;

	trace_snd_soc_dapm_widget_power(w, power);

	/* If we changed our power state perhaps our neigbours changed
	 * also.
	 */
	snd_soc_dapm_widget_for_each_source_path(w, path)
		dapm_widget_set_peer_power(path->source, power, path->connect);

	/* Supplies can't affect their outputs, only their inputs */
	if (!w->is_supply) {
		snd_soc_dapm_widget_for_each_sink_path(w, path)
			dapm_widget_set_peer_power(path->sink, power,
						   path->connect);
	}

	if (power)
		dapm_seq_insert(w, up_list, true);
	else
		dapm_seq_insert(w, down_list, false);
}

static void dapm_power_one_widget(struct snd_soc_dapm_widget *w,
				  struct list_head *up_list,
				  struct list_head *down_list)
{
	int power;

	switch (w->id) {
	case snd_soc_dapm_pre:
		dapm_seq_insert(w, down_list, false);
		break;
	case snd_soc_dapm_post:
		dapm_seq_insert(w, up_list, true);
		break;

	default:
		power = dapm_widget_power_check(w);

		dapm_widget_set_power(w, power, up_list, down_list);
		break;
	}
}

static bool dapm_idle_bias_off(struct snd_soc_dapm_context *dapm)
{
	if (dapm->idle_bias_off)
		return true;

	switch (snd_power_get_state(dapm->card->snd_card)) {
	case SNDRV_CTL_POWER_D3hot:
	case SNDRV_CTL_POWER_D3cold:
		return dapm->suspend_bias_off;
	default:
		break;
	}

	return false;
}

/*
 * Scan each dapm widget for complete audio path.
 * A complete path is a route that has valid endpoints i.e.:-
 *
 *  o DAC to output pin.
 *  o Input Pin to ADC.
 *  o Input pin to Output pin (bypass, sidetone)
 *  o DAC to ADC (loopback).
 */
static int dapm_power_widgets(struct snd_soc_codec *codec, int event)
{
	struct snd_soc_dapm_widget *w;
	int in, out, i, c = 1, *seq = NULL, ret = 0, power_change, power;

	/* do we have a sequenced stream event */
	if (event == SND_SOC_DAPM_STREAM_START) {
		c = ARRAY_SIZE(dapm_up_seq);
		seq = dapm_up_seq;
	} else if (event == SND_SOC_DAPM_STREAM_STOP) {
		c = ARRAY_SIZE(dapm_down_seq);
		seq = dapm_down_seq;
	}

	for(i = 0; i < c; i++) {
		list_for_each_entry(w, &codec->dapm_widgets, list) {

			/* is widget in stream order */
			if (seq && seq[i] && w->id != seq[i])
				continue;

			/* vmid - no action */
			if (w->id == snd_soc_dapm_vmid)
				continue;

			/* active ADC */
			if (w->id == snd_soc_dapm_adc && w->active) {
				in = is_connected_input_ep(w);
				dapm_clear_walk(w->codec);
				w->power = (in != 0) ? 1 : 0;
				dapm_update_bits(w);
				continue;
			}

			/* active DAC */
			if (w->id == snd_soc_dapm_dac && w->active) {
				out = is_connected_output_ep(w);
				dapm_clear_walk(w->codec);
				w->power = (out != 0) ? 1 : 0;
				dapm_update_bits(w);
				continue;
			}

			/* pre and post event widgets */
			if (w->id == snd_soc_dapm_pre) {
				if (!w->event)
					continue;

				if (event == SND_SOC_DAPM_STREAM_START) {
					ret = w->event(w,
						NULL, SND_SOC_DAPM_PRE_PMU);
					if (ret < 0)
						return ret;
				} else if (event == SND_SOC_DAPM_STREAM_STOP) {
					ret = w->event(w,
						NULL, SND_SOC_DAPM_PRE_PMD);
					if (ret < 0)
						return ret;
				}
				continue;
			}
			if (w->id == snd_soc_dapm_post) {
				if (!w->event)
					continue;

				if (event == SND_SOC_DAPM_STREAM_START) {
					ret = w->event(w,
						NULL, SND_SOC_DAPM_POST_PMU);
					if (ret < 0)
						return ret;
				} else if (event == SND_SOC_DAPM_STREAM_STOP) {
					ret = w->event(w,
						NULL, SND_SOC_DAPM_POST_PMD);
					if (ret < 0)
						return ret;
				}
				continue;
			}

			/* all other widgets */
			in = is_connected_input_ep(w);
			dapm_clear_walk(w->codec);
			out = is_connected_output_ep(w);
			dapm_clear_walk(w->codec);
			power = (out != 0 && in != 0) ? 1 : 0;
			power_change = (w->power == power) ? 0: 1;
			w->power = power;

			if (!power_change)
				continue;

			/* call any power change event handlers */
			if (w->event)
				pr_debug("power %s event for %s flags %x\n",
					 w->power ? "on" : "off",
					 w->name, w->event_flags);

			/* power up pre event */
			if (power && w->event &&
			    (w->event_flags & SND_SOC_DAPM_PRE_PMU)) {
				ret = w->event(w, NULL, SND_SOC_DAPM_PRE_PMU);
				if (ret < 0)
					return ret;
			}

			/* power down pre event */
			if (!power && w->event &&
			    (w->event_flags & SND_SOC_DAPM_PRE_PMD)) {
				ret = w->event(w, NULL, SND_SOC_DAPM_PRE_PMD);
				if (ret < 0)
					return ret;
			}

			/* Lower PGA volume to reduce pops */
			if (w->id == snd_soc_dapm_pga && !power)
				dapm_set_pga(w, power);

			dapm_update_bits(w);

			/* Raise PGA volume to reduce pops */
			if (w->id == snd_soc_dapm_pga && power)
				dapm_set_pga(w, power);

			/* power up post event */
			if (power && w->event &&
			    (w->event_flags & SND_SOC_DAPM_POST_PMU)) {
				ret = w->event(w,
					       NULL, SND_SOC_DAPM_POST_PMU);
				if (ret < 0)
					return ret;
			}

			/* power down post event */
			if (!power && w->event &&
			    (w->event_flags & SND_SOC_DAPM_POST_PMD)) {
				ret = w->event(w, NULL, SND_SOC_DAPM_POST_PMD);
				if (ret < 0)
					return ret;
			}
		}
	}

	return ret;
}

#ifdef DEBUG
static void dbg_dump_dapm(struct snd_soc_codec* codec, const char *action)
{
	struct snd_soc_dapm_widget *w;
	struct snd_soc_dapm_path *p = NULL;
	int in, out;

	printk("DAPM %s %s\n", codec->name, action);

	list_for_each_entry(w, &codec->dapm_widgets, list) {

		/* only display widgets that effect routing */
		switch (w->id) {
		case snd_soc_dapm_pre:
		case snd_soc_dapm_post:
		case snd_soc_dapm_vmid:
			continue;
		case snd_soc_dapm_mux:
		case snd_soc_dapm_output:
		case snd_soc_dapm_input:
		case snd_soc_dapm_switch:
		case snd_soc_dapm_hp:
		case snd_soc_dapm_mic:
		case snd_soc_dapm_spk:
		case snd_soc_dapm_line:
		case snd_soc_dapm_micbias:
		case snd_soc_dapm_dac:
		case snd_soc_dapm_adc:
		case snd_soc_dapm_pga:
		case snd_soc_dapm_mixer:
			if (w->name) {
				in = is_connected_input_ep(w);
				dapm_clear_walk(w->codec);
				out = is_connected_output_ep(w);
				dapm_clear_walk(w->codec);
				printk("%s: %s  in %d out %d\n", w->name,
					w->power ? "On":"Off",in, out);

				list_for_each_entry(p, &w->sources, list_sink) {
					if (p->connect)
						printk(" in  %s %s\n", p->name ? p->name : "static",
							p->source->name);
				}
				list_for_each_entry(p, &w->sinks, list_source) {
					if (p->connect)
						printk(" out %s %s\n", p->name ? p->name : "static",
							p->sink->name);
				}
			}
		break;
		}
	}
}
#endif

/* test and update the power status of a mux widget */
static int dapm_mux_update_power(struct snd_soc_dapm_widget *widget,
				 struct snd_kcontrol *kcontrol, int mask,
				 int val, struct soc_enum* e)
{
	struct snd_soc_dapm_path *path;
	int found = 0;

	if (widget->id != snd_soc_dapm_mux)
		return -ENODEV;

	if (!snd_soc_test_bits(widget->codec, e->reg, mask, val))
		return 0;

	/* find dapm widget path assoc with kcontrol */
	list_for_each_entry(path, &widget->codec->dapm_paths, list) {
		if (path->kcontrol != kcontrol)
			continue;

		if (!path->name || ! e->texts[val])
			continue;

		found = 1;
		/* we now need to match the string in the enum to the path */
		if (!(strcmp(path->name, e->texts[val])))
			path->connect = 1; /* new connection */
		else
			path->connect = 0; /* old connection must be powered down */
	}

	if (found) {
		dapm_power_widgets(widget->codec, SND_SOC_DAPM_STREAM_NOP);
		dump_dapm(widget->codec, "mux power update");
	}

	return 0;
}

/* test and update the power status of a mixer or switch widget */
static int dapm_mixer_update_power(struct snd_soc_dapm_widget *widget,
				   struct snd_kcontrol *kcontrol, int reg,
				   int val_mask, int val, int invert)
{
	struct snd_soc_dapm_path *path;
	int found = 0;

	if (widget->id != snd_soc_dapm_mixer &&
	    widget->id != snd_soc_dapm_switch)
		return -ENODEV;

	if (!snd_soc_test_bits(widget->codec, reg, val_mask, val))
		return 0;

	/* find dapm widget path assoc with kcontrol */
	list_for_each_entry(path, &widget->codec->dapm_paths, list) {
		if (path->kcontrol != kcontrol)
			continue;

		/* found, now check type */
		found = 1;
		if (val)
			/* new connection */
			path->connect = invert ? 0:1;
		else
			/* old connection must be powered down */
			path->connect = invert ? 1:0;
		break;
	}

	if (found) {
		dapm_power_widgets(widget->codec, SND_SOC_DAPM_STREAM_NOP);
		dump_dapm(widget->codec, "mixer power update");
	}

	return 0;
}

/* show dapm widget status in sys fs */
static ssize_t dapm_widget_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct snd_soc_device *devdata = dev_get_drvdata(dev);
	struct snd_soc_codec *codec = devdata->codec;
static int dapm_power_widgets(struct snd_soc_card *card, int event)
{
	struct snd_soc_dapm_widget *w;
	struct snd_soc_dapm_context *d;
	LIST_HEAD(up_list);
	LIST_HEAD(down_list);
	ASYNC_DOMAIN_EXCLUSIVE(async_domain);
	enum snd_soc_bias_level bias;

	lockdep_assert_held(&card->dapm_mutex);

	trace_snd_soc_dapm_start(card);

	list_for_each_entry(d, &card->dapm_list, list) {
		if (dapm_idle_bias_off(d))
			d->target_bias_level = SND_SOC_BIAS_OFF;
		else
			d->target_bias_level = SND_SOC_BIAS_STANDBY;
	}

	dapm_reset(card);

	/* Check which widgets we need to power and store them in
	 * lists indicating if they should be powered up or down.  We
	 * only check widgets that have been flagged as dirty but note
	 * that new widgets may be added to the dirty list while we
	 * iterate.
	 */
	list_for_each_entry(w, &card->dapm_dirty, dirty) {
		dapm_power_one_widget(w, &up_list, &down_list);
	}

	list_for_each_entry(w, &card->widgets, list) {
		switch (w->id) {
		case snd_soc_dapm_pre:
		case snd_soc_dapm_post:
			/* These widgets always need to be powered */
			break;
		default:
			list_del_init(&w->dirty);
			break;
		}

		if (w->new_power) {
			d = w->dapm;

			/* Supplies and micbiases only bring the
			 * context up to STANDBY as unless something
			 * else is active and passing audio they
			 * generally don't require full power.  Signal
			 * generators are virtual pins and have no
			 * power impact themselves.
			 */
			switch (w->id) {
			case snd_soc_dapm_siggen:
			case snd_soc_dapm_vmid:
				break;
			case snd_soc_dapm_supply:
			case snd_soc_dapm_regulator_supply:
			case snd_soc_dapm_clock_supply:
			case snd_soc_dapm_micbias:
				if (d->target_bias_level < SND_SOC_BIAS_STANDBY)
					d->target_bias_level = SND_SOC_BIAS_STANDBY;
				break;
			default:
				d->target_bias_level = SND_SOC_BIAS_ON;
				break;
			}
		}

	}

	/* Force all contexts in the card to the same bias state if
	 * they're not ground referenced.
	 */
	bias = SND_SOC_BIAS_OFF;
	list_for_each_entry(d, &card->dapm_list, list)
		if (d->target_bias_level > bias)
			bias = d->target_bias_level;
	list_for_each_entry(d, &card->dapm_list, list)
		if (!dapm_idle_bias_off(d))
			d->target_bias_level = bias;

	trace_snd_soc_dapm_walk_done(card);

	/* Run card bias changes at first */
	dapm_pre_sequence_async(&card->dapm, 0);
	/* Run other bias changes in parallel */
	list_for_each_entry(d, &card->dapm_list, list) {
		if (d != &card->dapm)
			async_schedule_domain(dapm_pre_sequence_async, d,
						&async_domain);
	}
	async_synchronize_full_domain(&async_domain);

	list_for_each_entry(w, &down_list, power_list) {
		dapm_seq_check_event(card, w, SND_SOC_DAPM_WILL_PMD);
	}

	list_for_each_entry(w, &up_list, power_list) {
		dapm_seq_check_event(card, w, SND_SOC_DAPM_WILL_PMU);
	}

	/* Power down widgets first; try to avoid amplifying pops. */
	dapm_seq_run(card, &down_list, event, false);

	dapm_widget_update(card);

	/* Now power up. */
	dapm_seq_run(card, &up_list, event, true);

	/* Run all the bias changes in parallel */
	list_for_each_entry(d, &card->dapm_list, list) {
		if (d != &card->dapm)
			async_schedule_domain(dapm_post_sequence_async, d,
						&async_domain);
	}
	async_synchronize_full_domain(&async_domain);
	/* Run card bias changes at last */
	dapm_post_sequence_async(&card->dapm, 0);

	/* do we need to notify any clients that DAPM event is complete */
	list_for_each_entry(d, &card->dapm_list, list) {
		if (d->stream_event)
			d->stream_event(d, event);
	}

	pop_dbg(card->dev, card->pop_time,
		"DAPM sequencing finished, waiting %dms\n", card->pop_time);
	pop_wait(card->pop_time);

	trace_snd_soc_dapm_done(card);

	return 0;
}

#ifdef CONFIG_DEBUG_FS
static ssize_t dapm_widget_power_read_file(struct file *file,
					   char __user *user_buf,
					   size_t count, loff_t *ppos)
{
	struct snd_soc_dapm_widget *w = file->private_data;
	struct snd_soc_card *card = w->dapm->card;
	enum snd_soc_dapm_direction dir, rdir;
	char *buf;
	int in, out;
	ssize_t ret;
	struct snd_soc_dapm_path *p = NULL;

	buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	mutex_lock(&card->dapm_mutex);

	/* Supply widgets are not handled by is_connected_{input,output}_ep() */
	if (w->is_supply) {
		in = 0;
		out = 0;
	} else {
		in = is_connected_input_ep(w, NULL);
		out = is_connected_output_ep(w, NULL);
	}

	ret = scnprintf(buf, PAGE_SIZE, "%s: %s%s  in %d out %d",
		       w->name, w->power ? "On" : "Off",
		       w->force ? " (forced)" : "", in, out);

	if (w->reg >= 0)
		ret += scnprintf(buf + ret, PAGE_SIZE - ret,
				" - R%d(0x%x) mask 0x%x",
				w->reg, w->reg, w->mask << w->shift);

	ret += scnprintf(buf + ret, PAGE_SIZE - ret, "\n");

	if (w->sname)
		ret += scnprintf(buf + ret, PAGE_SIZE - ret, " stream %s %s\n",
				w->sname,
				w->active ? "active" : "inactive");

	snd_soc_dapm_for_each_direction(dir) {
		rdir = SND_SOC_DAPM_DIR_REVERSE(dir);
		snd_soc_dapm_widget_for_each_path(w, dir, p) {
			if (p->connected && !p->connected(w, p->node[rdir]))
				continue;

			if (!p->connect)
				continue;

			ret += scnprintf(buf + ret, PAGE_SIZE - ret,
					" %s  \"%s\" \"%s\"\n",
					(rdir == SND_SOC_DAPM_DIR_IN) ? "in" : "out",
					p->name ? p->name : "static",
					p->node[rdir]->name);
		}
	}

	mutex_unlock(&card->dapm_mutex);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

	kfree(buf);
	return ret;
}

static const struct file_operations dapm_widget_power_fops = {
	.open = simple_open,
	.read = dapm_widget_power_read_file,
	.llseek = default_llseek,
};

static ssize_t dapm_bias_read_file(struct file *file, char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	struct snd_soc_dapm_context *dapm = file->private_data;
	char *level;

	switch (dapm->bias_level) {
	case SND_SOC_BIAS_ON:
		level = "On\n";
		break;
	case SND_SOC_BIAS_PREPARE:
		level = "Prepare\n";
		break;
	case SND_SOC_BIAS_STANDBY:
		level = "Standby\n";
		break;
	case SND_SOC_BIAS_OFF:
		level = "Off\n";
		break;
	default:
		WARN(1, "Unknown bias_level %d\n", dapm->bias_level);
		level = "Unknown\n";
		break;
	}

	return simple_read_from_buffer(user_buf, count, ppos, level,
				       strlen(level));
}

static const struct file_operations dapm_bias_fops = {
	.open = simple_open,
	.read = dapm_bias_read_file,
	.llseek = default_llseek,
};

void snd_soc_dapm_debugfs_init(struct snd_soc_dapm_context *dapm,
	struct dentry *parent)
{
	struct dentry *d;

	if (!parent)
		return;

	dapm->debugfs_dapm = debugfs_create_dir("dapm", parent);

	if (!dapm->debugfs_dapm) {
		dev_warn(dapm->dev,
		       "ASoC: Failed to create DAPM debugfs directory\n");
		return;
	}

	d = debugfs_create_file("bias_level", 0444,
				dapm->debugfs_dapm, dapm,
				&dapm_bias_fops);
	if (!d)
		dev_warn(dapm->dev,
			 "ASoC: Failed to create bias level debugfs file\n");
}

static void dapm_debugfs_add_widget(struct snd_soc_dapm_widget *w)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct dentry *d;

	if (!dapm->debugfs_dapm || !w->name)
		return;

	d = debugfs_create_file(w->name, 0444,
				dapm->debugfs_dapm, w,
				&dapm_widget_power_fops);
	if (!d)
		dev_warn(w->dapm->dev,
			"ASoC: Failed to create %s debugfs file\n",
			w->name);
}

static void dapm_debugfs_cleanup(struct snd_soc_dapm_context *dapm)
{
	debugfs_remove_recursive(dapm->debugfs_dapm);
}

#else
void snd_soc_dapm_debugfs_init(struct snd_soc_dapm_context *dapm,
	struct dentry *parent)
{
}

static inline void dapm_debugfs_add_widget(struct snd_soc_dapm_widget *w)
{
}

static inline void dapm_debugfs_cleanup(struct snd_soc_dapm_context *dapm)
{
}

#endif

/*
 * soc_dapm_connect_path() - Connects or disconnects a path
 * @path: The path to update
 * @connect: The new connect state of the path. True if the path is connected,
 *  false if it is disconneted.
 * @reason: The reason why the path changed (for debugging only)
 */
static void soc_dapm_connect_path(struct snd_soc_dapm_path *path,
	bool connect, const char *reason)
{
	if (path->connect == connect)
		return;

	path->connect = connect;
	dapm_mark_dirty(path->source, reason);
	dapm_mark_dirty(path->sink, reason);
	dapm_path_invalidate(path);
}

/* test and update the power status of a mux widget */
static int soc_dapm_mux_update_power(struct snd_soc_card *card,
				 struct snd_kcontrol *kcontrol, int mux, struct soc_enum *e)
{
	struct snd_soc_dapm_path *path;
	int found = 0;
	bool connect;

	lockdep_assert_held(&card->dapm_mutex);

	/* find dapm widget path assoc with kcontrol */
	dapm_kcontrol_for_each_path(path, kcontrol) {
		found = 1;
		/* we now need to match the string in the enum to the path */
		if (!(strcmp(path->name, e->texts[mux])))
			connect = true;
		else
			connect = false;

		soc_dapm_connect_path(path, connect, "mux update");
	}

	if (found)
		dapm_power_widgets(card, SND_SOC_DAPM_STREAM_NOP);

	return found;
}

int snd_soc_dapm_mux_update_power(struct snd_soc_dapm_context *dapm,
	struct snd_kcontrol *kcontrol, int mux, struct soc_enum *e,
	struct snd_soc_dapm_update *update)
{
	struct snd_soc_card *card = dapm->card;
	int ret;

	mutex_lock_nested(&card->dapm_mutex, SND_SOC_DAPM_CLASS_RUNTIME);
	card->update = update;
	ret = soc_dapm_mux_update_power(card, kcontrol, mux, e);
	card->update = NULL;
	mutex_unlock(&card->dapm_mutex);
	if (ret > 0)
		soc_dpcm_runtime_update(card);
	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_mux_update_power);

/* test and update the power status of a mixer or switch widget */
static int soc_dapm_mixer_update_power(struct snd_soc_card *card,
				   struct snd_kcontrol *kcontrol, int connect)
{
	struct snd_soc_dapm_path *path;
	int found = 0;

	lockdep_assert_held(&card->dapm_mutex);

	/* find dapm widget path assoc with kcontrol */
	dapm_kcontrol_for_each_path(path, kcontrol) {
		found = 1;
		soc_dapm_connect_path(path, connect, "mixer update");
	}

	if (found)
		dapm_power_widgets(card, SND_SOC_DAPM_STREAM_NOP);

	return found;
}

int snd_soc_dapm_mixer_update_power(struct snd_soc_dapm_context *dapm,
	struct snd_kcontrol *kcontrol, int connect,
	struct snd_soc_dapm_update *update)
{
	struct snd_soc_card *card = dapm->card;
	int ret;

	mutex_lock_nested(&card->dapm_mutex, SND_SOC_DAPM_CLASS_RUNTIME);
	card->update = update;
	ret = soc_dapm_mixer_update_power(card, kcontrol, connect);
	card->update = NULL;
	mutex_unlock(&card->dapm_mutex);
	if (ret > 0)
		soc_dpcm_runtime_update(card);
	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_mixer_update_power);

static ssize_t dapm_widget_show_component(struct snd_soc_component *cmpnt,
	char *buf)
{
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(cmpnt);
	struct snd_soc_dapm_widget *w;
	int count = 0;
	char *state = "not set";

	list_for_each_entry(w, &codec->dapm_widgets, list) {
	/* card won't be set for the dummy component, as a spot fix
	 * we're checking for that case specifically here but in future
	 * we will ensure that the dummy component looks like others.
	 */
	if (!cmpnt->card)
		return 0;

	list_for_each_entry(w, &cmpnt->card->widgets, list) {
		if (w->dapm != dapm)
			continue;

		/* only display widgets that burnm power */
		switch (w->id) {
		case snd_soc_dapm_hp:
		case snd_soc_dapm_mic:
		case snd_soc_dapm_spk:
		case snd_soc_dapm_line:
		case snd_soc_dapm_micbias:
		case snd_soc_dapm_dac:
		case snd_soc_dapm_adc:
		case snd_soc_dapm_pga:
		case snd_soc_dapm_mixer:
		case snd_soc_dapm_out_drv:
		case snd_soc_dapm_mixer:
		case snd_soc_dapm_mixer_named_ctl:
		case snd_soc_dapm_supply:
		case snd_soc_dapm_regulator_supply:
		case snd_soc_dapm_clock_supply:
			if (w->name)
				count += sprintf(buf + count, "%s: %s\n",
					w->name, w->power ? "On":"Off");
		break;
		default:
		break;
		}
	}

	switch (codec->bias_level) {
	switch (snd_soc_dapm_get_bias_level(dapm)) {
	case SND_SOC_BIAS_ON:
		state = "On";
		break;
	case SND_SOC_BIAS_PREPARE:
		state = "Prepare";
		break;
	case SND_SOC_BIAS_STANDBY:
		state = "Standby";
		break;
	case SND_SOC_BIAS_OFF:
		state = "Off";
		break;
	}
	count += sprintf(buf + count, "PM State: %s\n", state);

	return count;
}

static DEVICE_ATTR(dapm_widget, 0444, dapm_widget_show, NULL);

/* pop/click delay times */
static ssize_t dapm_pop_time_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", pop_time);
}

static ssize_t dapm_pop_time_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)

{
	unsigned long val;

	if (strict_strtoul(buf, 10, &val) >= 0)
		pop_time = val;
	else
		printk(KERN_ERR "Unable to parse pop_time setting\n");
/* show dapm widget status in sys fs */
static ssize_t dapm_widget_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct snd_soc_pcm_runtime *rtd = dev_get_drvdata(dev);
	int i, count = 0;

	mutex_lock(&rtd->card->dapm_mutex);

	for (i = 0; i < rtd->num_codecs; i++) {
		struct snd_soc_component *cmpnt = rtd->codec_dais[i]->component;

		count += dapm_widget_show_component(cmpnt, buf + count);
	}

	mutex_unlock(&rtd->card->dapm_mutex);

	return count;
}

static DEVICE_ATTR(dapm_pop_time, 0744, dapm_pop_time_show,
		   dapm_pop_time_store);

int snd_soc_dapm_sys_add(struct device *dev)
{
	int ret = 0;

	if (dapm_status) {
		ret = device_create_file(dev, &dev_attr_dapm_widget);

		if (ret == 0)
			ret = device_create_file(dev, &dev_attr_dapm_pop_time);
	}

	return ret;
}

static void snd_soc_dapm_sys_remove(struct device *dev)
{
	if (dapm_status) {
		device_remove_file(dev, &dev_attr_dapm_pop_time);
		device_remove_file(dev, &dev_attr_dapm_widget);
	}
}

/* free all dapm widgets and resources */
static void dapm_free_widgets(struct snd_soc_codec *codec)
{
	struct snd_soc_dapm_widget *w, *next_w;
	struct snd_soc_dapm_path *p, *next_p;

	list_for_each_entry_safe(w, next_w, &codec->dapm_widgets, list) {
		list_del(&w->list);
		kfree(w);
	}

	list_for_each_entry_safe(p, next_p, &codec->dapm_paths, list) {
		list_del(&p->list);
		kfree(p->long_name);
		kfree(p);
	}
}

static int snd_soc_dapm_set_pin(struct snd_soc_codec *codec,
	char *pin, int status)
{
	struct snd_soc_dapm_widget *w;

	list_for_each_entry(w, &codec->dapm_widgets, list) {
		if (!strcmp(w->name, pin)) {
			pr_debug("dapm: %s: pin %s\n", codec->name, pin);
			w->connected = status;
			return 0;
		}
	}

	pr_err("dapm: %s: configuring unknown pin %s\n", codec->name, pin);
	return -EINVAL;
}

/**
 * snd_soc_dapm_sync - scan and power dapm paths
 * @codec: audio codec
static DEVICE_ATTR(dapm_widget, 0444, dapm_widget_show, NULL);

struct attribute *soc_dapm_dev_attrs[] = {
	&dev_attr_dapm_widget.attr,
	NULL
};

static void dapm_free_path(struct snd_soc_dapm_path *path)
{
	list_del(&path->list_node[SND_SOC_DAPM_DIR_IN]);
	list_del(&path->list_node[SND_SOC_DAPM_DIR_OUT]);
	list_del(&path->list_kcontrol);
	list_del(&path->list);
	kfree(path);
}

void snd_soc_dapm_free_widget(struct snd_soc_dapm_widget *w)
{
	struct snd_soc_dapm_path *p, *next_p;
	enum snd_soc_dapm_direction dir;

	list_del(&w->list);
	list_del(&w->dirty);
	/*
	 * remove source and sink paths associated to this widget.
	 * While removing the path, remove reference to it from both
	 * source and sink widgets so that path is removed only once.
	 */
	snd_soc_dapm_for_each_direction(dir) {
		snd_soc_dapm_widget_for_each_path_safe(w, dir, p, next_p)
			dapm_free_path(p);
	}

	kfree(w->kcontrols);
	kfree_const(w->name);
	kfree(w);
}

void snd_soc_dapm_reset_cache(struct snd_soc_dapm_context *dapm)
{
	dapm->path_sink_cache.widget = NULL;
	dapm->path_source_cache.widget = NULL;
}

/* free all dapm widgets and resources */
static void dapm_free_widgets(struct snd_soc_dapm_context *dapm)
{
	struct snd_soc_dapm_widget *w, *next_w;

	list_for_each_entry_safe(w, next_w, &dapm->card->widgets, list) {
		if (w->dapm != dapm)
			continue;
		snd_soc_dapm_free_widget(w);
	}
	snd_soc_dapm_reset_cache(dapm);
}

static struct snd_soc_dapm_widget *dapm_find_widget(
			struct snd_soc_dapm_context *dapm, const char *pin,
			bool search_other_contexts)
{
	struct snd_soc_dapm_widget *w;
	struct snd_soc_dapm_widget *fallback = NULL;

	list_for_each_entry(w, &dapm->card->widgets, list) {
		if (!strcmp(w->name, pin)) {
			if (w->dapm == dapm)
				return w;
			else
				fallback = w;
		}
	}

	if (search_other_contexts)
		return fallback;

	return NULL;
}

/*
 * set the DAPM pin status:
 * returns 1 when the value has been updated, 0 when unchanged, or a negative
 * error code; called from kcontrol put callback
 */
static int __snd_soc_dapm_set_pin(struct snd_soc_dapm_context *dapm,
				  const char *pin, int status)
{
	struct snd_soc_dapm_widget *w = dapm_find_widget(dapm, pin, true);
	int ret = 0;

	dapm_assert_locked(dapm);

	if (!w) {
		dev_err(dapm->dev, "ASoC: DAPM unknown pin %s\n", pin);
		return -EINVAL;
	}

	if (w->connected != status) {
		dapm_mark_dirty(w, "pin configuration");
		dapm_widget_invalidate_input_paths(w);
		dapm_widget_invalidate_output_paths(w);
		ret = 1;
	}

	w->connected = status;
	if (status == 0)
		w->force = 0;

	return ret;
}

/*
 * similar as __snd_soc_dapm_set_pin(), but returns 0 when successful;
 * called from several API functions below
 */
static int snd_soc_dapm_set_pin(struct snd_soc_dapm_context *dapm,
				const char *pin, int status)
{
	int ret = __snd_soc_dapm_set_pin(dapm, pin, status);

	return ret < 0 ? ret : 0;
}

/**
 * snd_soc_dapm_sync_unlocked - scan and power dapm paths
 * @dapm: DAPM context
 *
 * Walks all dapm audio paths and powers widgets according to their
 * stream or path usage.
 *
 * Requires external locking.
 *
 * Returns 0 for success.
 */
int snd_soc_dapm_sync_unlocked(struct snd_soc_dapm_context *dapm)
{
	/*
	 * Suppress early reports (eg, jacks syncing their state) to avoid
	 * silly DAPM runs during card startup.
	 */
	if (!dapm->card || !dapm->card->instantiated)
		return 0;

	return dapm_power_widgets(dapm->card, SND_SOC_DAPM_STREAM_NOP);
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_sync_unlocked);

/**
 * snd_soc_dapm_sync - scan and power dapm paths
 * @dapm: DAPM context
 *
 * Walks all dapm audio paths and powers widgets according to their
 * stream or path usage.
 *
 * Returns 0 for success.
 */
int snd_soc_dapm_sync(struct snd_soc_codec *codec)
{
	int ret = dapm_power_widgets(codec, SND_SOC_DAPM_STREAM_NOP);
	dump_dapm(codec, "sync");
int snd_soc_dapm_sync(struct snd_soc_dapm_context *dapm)
{
	int ret;

	mutex_lock_nested(&dapm->card->dapm_mutex, SND_SOC_DAPM_CLASS_RUNTIME);
	ret = snd_soc_dapm_sync_unlocked(dapm);
	mutex_unlock(&dapm->card->dapm_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_sync);

static int snd_soc_dapm_add_route(struct snd_soc_codec *codec,
	const char *sink, const char *control, const char *source)
{
	struct snd_soc_dapm_path *path;
	struct snd_soc_dapm_widget *wsource = NULL, *wsink = NULL, *w;
	int ret = 0;

	/* find src and dest widgets */
	list_for_each_entry(w, &codec->dapm_widgets, list) {

		if (!wsink && !(strcmp(w->name, sink))) {
			wsink = w;
			continue;
		}
		if (!wsource && !(strcmp(w->name, source))) {
			wsource = w;
		}
	}

	if (wsource == NULL || wsink == NULL)
		return -ENODEV;
/*
 * dapm_update_widget_flags() - Re-compute widget sink and source flags
 * @w: The widget for which to update the flags
 *
 * Some widgets have a dynamic category which depends on which neighbors they
 * are connected to. This function update the category for these widgets.
 *
 * This function must be called whenever a path is added or removed to a widget.
 */
static void dapm_update_widget_flags(struct snd_soc_dapm_widget *w)
{
	enum snd_soc_dapm_direction dir;
	struct snd_soc_dapm_path *p;
	unsigned int ep;

	switch (w->id) {
	case snd_soc_dapm_input:
		/* On a fully routed card a input is never a source */
		if (w->dapm->card->fully_routed)
			return;
		ep = SND_SOC_DAPM_EP_SOURCE;
		snd_soc_dapm_widget_for_each_source_path(w, p) {
			if (p->source->id == snd_soc_dapm_micbias ||
				p->source->id == snd_soc_dapm_mic ||
				p->source->id == snd_soc_dapm_line ||
				p->source->id == snd_soc_dapm_output) {
					ep = 0;
					break;
			}
		}
		break;
	case snd_soc_dapm_output:
		/* On a fully routed card a output is never a sink */
		if (w->dapm->card->fully_routed)
			return;
		ep = SND_SOC_DAPM_EP_SINK;
		snd_soc_dapm_widget_for_each_sink_path(w, p) {
			if (p->sink->id == snd_soc_dapm_spk ||
				p->sink->id == snd_soc_dapm_hp ||
				p->sink->id == snd_soc_dapm_line ||
				p->sink->id == snd_soc_dapm_input) {
					ep = 0;
					break;
			}
		}
		break;
	case snd_soc_dapm_line:
		ep = 0;
		snd_soc_dapm_for_each_direction(dir) {
			if (!list_empty(&w->edges[dir]))
				ep |= SND_SOC_DAPM_DIR_TO_EP(dir);
		}
		break;
	default:
		return;
	}

	w->is_ep = ep;
}

static int snd_soc_dapm_check_dynamic_path(struct snd_soc_dapm_context *dapm,
	struct snd_soc_dapm_widget *source, struct snd_soc_dapm_widget *sink,
	const char *control)
{
	bool dynamic_source = false;
	bool dynamic_sink = false;

	if (!control)
		return 0;

	switch (source->id) {
	case snd_soc_dapm_demux:
		dynamic_source = true;
		break;
	default:
		break;
	}

	switch (sink->id) {
	case snd_soc_dapm_mux:
	case snd_soc_dapm_switch:
	case snd_soc_dapm_mixer:
	case snd_soc_dapm_mixer_named_ctl:
		dynamic_sink = true;
		break;
	default:
		break;
	}

	if (dynamic_source && dynamic_sink) {
		dev_err(dapm->dev,
			"Direct connection between demux and mixer/mux not supported for path %s -> [%s] -> %s\n",
			source->name, control, sink->name);
		return -EINVAL;
	} else if (!dynamic_source && !dynamic_sink) {
		dev_err(dapm->dev,
			"Control not supported for path %s -> [%s] -> %s\n",
			source->name, control, sink->name);
		return -EINVAL;
	}

	return 0;
}

static int snd_soc_dapm_add_path(struct snd_soc_dapm_context *dapm,
	struct snd_soc_dapm_widget *wsource, struct snd_soc_dapm_widget *wsink,
	const char *control,
	int (*connected)(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink))
{
	struct snd_soc_dapm_widget *widgets[2];
	enum snd_soc_dapm_direction dir;
	struct snd_soc_dapm_path *path;
	int ret;

	if (wsink->is_supply && !wsource->is_supply) {
		dev_err(dapm->dev,
			"Connecting non-supply widget to supply widget is not supported (%s -> %s)\n",
			wsource->name, wsink->name);
		return -EINVAL;
	}

	if (connected && !wsource->is_supply) {
		dev_err(dapm->dev,
			"connected() callback only supported for supply widgets (%s -> %s)\n",
			wsource->name, wsink->name);
		return -EINVAL;
	}

	if (wsource->is_supply && control) {
		dev_err(dapm->dev,
			"Conditional paths are not supported for supply widgets (%s -> [%s] -> %s)\n",
			wsource->name, control, wsink->name);
		return -EINVAL;
	}

	ret = snd_soc_dapm_check_dynamic_path(dapm, wsource, wsink, control);
	if (ret)
		return ret;

	path = kzalloc(sizeof(struct snd_soc_dapm_path), GFP_KERNEL);
	if (!path)
		return -ENOMEM;

	path->source = wsource;
	path->sink = wsink;
	INIT_LIST_HEAD(&path->list);
	INIT_LIST_HEAD(&path->list_source);
	INIT_LIST_HEAD(&path->list_sink);

	/* check for external widgets */
	if (wsink->id == snd_soc_dapm_input) {
		if (wsource->id == snd_soc_dapm_micbias ||
			wsource->id == snd_soc_dapm_mic ||
			wsink->id == snd_soc_dapm_line ||
			wsink->id == snd_soc_dapm_output)
			wsink->ext = 1;
	}
	if (wsource->id == snd_soc_dapm_output) {
		if (wsink->id == snd_soc_dapm_spk ||
			wsink->id == snd_soc_dapm_hp ||
			wsink->id == snd_soc_dapm_line ||
			wsink->id == snd_soc_dapm_input)
			wsource->ext = 1;
	}

	/* connect static paths */
	if (control == NULL) {
		list_add(&path->list, &codec->dapm_paths);
		list_add(&path->list_sink, &wsink->sources);
		list_add(&path->list_source, &wsource->sinks);
		path->connect = 1;
		return 0;
	}

	/* connect dynamic paths */
	switch(wsink->id) {
	case snd_soc_dapm_adc:
	case snd_soc_dapm_dac:
	case snd_soc_dapm_pga:
	case snd_soc_dapm_input:
	case snd_soc_dapm_output:
	case snd_soc_dapm_micbias:
	case snd_soc_dapm_vmid:
	case snd_soc_dapm_pre:
	case snd_soc_dapm_post:
		list_add(&path->list, &codec->dapm_paths);
		list_add(&path->list_sink, &wsink->sources);
		list_add(&path->list_source, &wsource->sinks);
		path->connect = 1;
		return 0;
	case snd_soc_dapm_mux:
		ret = dapm_connect_mux(codec, wsource, wsink, path, control,
			&wsink->kcontrols[0]);
		if (ret != 0)
			goto err;
		break;
	case snd_soc_dapm_switch:
	case snd_soc_dapm_mixer:
		ret = dapm_connect_mixer(codec, wsource, wsink, path, control);
		if (ret != 0)
			goto err;
		break;
	case snd_soc_dapm_hp:
	case snd_soc_dapm_mic:
	case snd_soc_dapm_line:
	case snd_soc_dapm_spk:
		list_add(&path->list, &codec->dapm_paths);
		list_add(&path->list_sink, &wsink->sources);
		list_add(&path->list_source, &wsource->sinks);
		path->connect = 0;
		return 0;
	}
	return 0;

err:
	printk(KERN_WARNING "asoc: no dapm match for %s --> %s --> %s\n", source,
		control, sink);
	path->node[SND_SOC_DAPM_DIR_IN] = wsource;
	path->node[SND_SOC_DAPM_DIR_OUT] = wsink;
	widgets[SND_SOC_DAPM_DIR_IN] = wsource;
	widgets[SND_SOC_DAPM_DIR_OUT] = wsink;

	path->connected = connected;
	INIT_LIST_HEAD(&path->list);
	INIT_LIST_HEAD(&path->list_kcontrol);

	if (wsource->is_supply || wsink->is_supply)
		path->is_supply = 1;

	/* connect static paths */
	if (control == NULL) {
		path->connect = 1;
	} else {
		switch (wsource->id) {
		case snd_soc_dapm_demux:
			ret = dapm_connect_mux(dapm, path, control, wsource);
			if (ret)
				goto err;
			break;
		default:
			break;
		}

		switch (wsink->id) {
		case snd_soc_dapm_mux:
			ret = dapm_connect_mux(dapm, path, control, wsink);
			if (ret != 0)
				goto err;
			break;
		case snd_soc_dapm_switch:
		case snd_soc_dapm_mixer:
		case snd_soc_dapm_mixer_named_ctl:
			ret = dapm_connect_mixer(dapm, path, control);
			if (ret != 0)
				goto err;
			break;
		default:
			break;
		}
	}

	list_add(&path->list, &dapm->card->paths);
	snd_soc_dapm_for_each_direction(dir)
		list_add(&path->list_node[dir], &widgets[dir]->edges[dir]);

	snd_soc_dapm_for_each_direction(dir) {
		dapm_update_widget_flags(widgets[dir]);
		dapm_mark_dirty(widgets[dir], "Route added");
	}

	if (dapm->card->instantiated && path->connect)
		dapm_path_invalidate(path);

	return 0;
err:
	kfree(path);
	return ret;
}

/**
 * snd_soc_dapm_connect_input - connect dapm widgets
 * @codec: audio codec
 * @sink: name of target widget
 * @control: mixer control name
 * @source: name of source name
 *
 * Connects 2 dapm widgets together via a named audio path. The sink is
 * the widget receiving the audio signal, whilst the source is the sender
 * of the audio signal.
 *
 * This function has been deprecated in favour of snd_soc_dapm_add_routes().
 *
 * Returns 0 for success else error.
 */
int snd_soc_dapm_connect_input(struct snd_soc_codec *codec, const char *sink,
	const char *control, const char *source)
{
	return snd_soc_dapm_add_route(codec, sink, control, source);
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_connect_input);

/**
 * snd_soc_dapm_add_routes - Add routes between DAPM widgets
 * @codec: codec
static int snd_soc_dapm_add_route(struct snd_soc_dapm_context *dapm,
				  const struct snd_soc_dapm_route *route)
{
	struct snd_soc_dapm_widget *wsource = NULL, *wsink = NULL, *w;
	struct snd_soc_dapm_widget *wtsource = NULL, *wtsink = NULL;
	const char *sink;
	const char *source;
	char prefixed_sink[80];
	char prefixed_source[80];
	const char *prefix;
	int ret;

	prefix = soc_dapm_prefix(dapm);
	if (prefix) {
		snprintf(prefixed_sink, sizeof(prefixed_sink), "%s %s",
			 prefix, route->sink);
		sink = prefixed_sink;
		snprintf(prefixed_source, sizeof(prefixed_source), "%s %s",
			 prefix, route->source);
		source = prefixed_source;
	} else {
		sink = route->sink;
		source = route->source;
	}

	wsource = dapm_wcache_lookup(&dapm->path_source_cache, source);
	wsink = dapm_wcache_lookup(&dapm->path_sink_cache, sink);

	if (wsink && wsource)
		goto skip_search;

	/*
	 * find src and dest widgets over all widgets but favor a widget from
	 * current DAPM context
	 */
	list_for_each_entry(w, &dapm->card->widgets, list) {
		if (!wsink && !(strcmp(w->name, sink))) {
			wtsink = w;
			if (w->dapm == dapm) {
				wsink = w;
				if (wsource)
					break;
			}
			continue;
		}
		if (!wsource && !(strcmp(w->name, source))) {
			wtsource = w;
			if (w->dapm == dapm) {
				wsource = w;
				if (wsink)
					break;
			}
		}
	}
	/* use widget from another DAPM context if not found from this */
	if (!wsink)
		wsink = wtsink;
	if (!wsource)
		wsource = wtsource;

	if (wsource == NULL) {
		dev_err(dapm->dev, "ASoC: no source widget found for %s\n",
			route->source);
		return -ENODEV;
	}
	if (wsink == NULL) {
		dev_err(dapm->dev, "ASoC: no sink widget found for %s\n",
			route->sink);
		return -ENODEV;
	}

skip_search:
	dapm_wcache_update(&dapm->path_sink_cache, wsink);
	dapm_wcache_update(&dapm->path_source_cache, wsource);

	ret = snd_soc_dapm_add_path(dapm, wsource, wsink, route->control,
		route->connected);
	if (ret)
		goto err;

	return 0;
err:
	dev_warn(dapm->dev, "ASoC: no dapm match for %s --> %s --> %s\n",
		 source, route->control, sink);
	return ret;
}

static int snd_soc_dapm_del_route(struct snd_soc_dapm_context *dapm,
				  const struct snd_soc_dapm_route *route)
{
	struct snd_soc_dapm_widget *wsource, *wsink;
	struct snd_soc_dapm_path *path, *p;
	const char *sink;
	const char *source;
	char prefixed_sink[80];
	char prefixed_source[80];
	const char *prefix;

	if (route->control) {
		dev_err(dapm->dev,
			"ASoC: Removal of routes with controls not supported\n");
		return -EINVAL;
	}

	prefix = soc_dapm_prefix(dapm);
	if (prefix) {
		snprintf(prefixed_sink, sizeof(prefixed_sink), "%s %s",
			 prefix, route->sink);
		sink = prefixed_sink;
		snprintf(prefixed_source, sizeof(prefixed_source), "%s %s",
			 prefix, route->source);
		source = prefixed_source;
	} else {
		sink = route->sink;
		source = route->source;
	}

	path = NULL;
	list_for_each_entry(p, &dapm->card->paths, list) {
		if (strcmp(p->source->name, source) != 0)
			continue;
		if (strcmp(p->sink->name, sink) != 0)
			continue;
		path = p;
		break;
	}

	if (path) {
		wsource = path->source;
		wsink = path->sink;

		dapm_mark_dirty(wsource, "Route removed");
		dapm_mark_dirty(wsink, "Route removed");
		if (path->connect)
			dapm_path_invalidate(path);

		dapm_free_path(path);

		/* Update any path related flags */
		dapm_update_widget_flags(wsource);
		dapm_update_widget_flags(wsink);
	} else {
		dev_warn(dapm->dev, "ASoC: Route %s->%s does not exist\n",
			 source, sink);
	}

	return 0;
}

/**
 * snd_soc_dapm_add_routes - Add routes between DAPM widgets
 * @dapm: DAPM context
 * @route: audio routes
 * @num: number of routes
 *
 * Connects 2 dapm widgets together via a named audio path. The sink is
 * the widget receiving the audio signal, whilst the source is the sender
 * of the audio signal.
 *
 * Returns 0 for success else error. On error all resources can be freed
 * with a call to snd_soc_card_free().
 */
int snd_soc_dapm_add_routes(struct snd_soc_codec *codec,
			    const struct snd_soc_dapm_route *route, int num)
{
	int i, ret;

	for (i = 0; i < num; i++) {
		ret = snd_soc_dapm_add_route(codec, route->sink,
					     route->control, route->source);
		if (ret < 0) {
			printk(KERN_ERR "Failed to add route %s->%s\n",
			       route->source,
			       route->sink);
			return ret;
		}
		route++;
	}

	return 0;
int snd_soc_dapm_add_routes(struct snd_soc_dapm_context *dapm,
			    const struct snd_soc_dapm_route *route, int num)
{
	int i, r, ret = 0;

	mutex_lock_nested(&dapm->card->dapm_mutex, SND_SOC_DAPM_CLASS_INIT);
	for (i = 0; i < num; i++) {
		r = snd_soc_dapm_add_route(dapm, route);
		if (r < 0) {
			dev_err(dapm->dev, "ASoC: Failed to add route %s -> %s -> %s\n",
				route->source,
				route->control ? route->control : "direct",
				route->sink);
			ret = r;
		}
		route++;
	}
	mutex_unlock(&dapm->card->dapm_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_add_routes);

/**
 * snd_soc_dapm_new_widgets - add new dapm widgets
 * @codec: audio codec
 * snd_soc_dapm_del_routes - Remove routes between DAPM widgets
 * @dapm: DAPM context
 * @route: audio routes
 * @num: number of routes
 *
 * Removes routes from the DAPM context.
 */
int snd_soc_dapm_del_routes(struct snd_soc_dapm_context *dapm,
			    const struct snd_soc_dapm_route *route, int num)
{
	int i, ret = 0;

	mutex_lock_nested(&dapm->card->dapm_mutex, SND_SOC_DAPM_CLASS_INIT);
	for (i = 0; i < num; i++) {
		snd_soc_dapm_del_route(dapm, route);
		route++;
	}
	mutex_unlock(&dapm->card->dapm_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_del_routes);

static int snd_soc_dapm_weak_route(struct snd_soc_dapm_context *dapm,
				   const struct snd_soc_dapm_route *route)
{
	struct snd_soc_dapm_widget *source = dapm_find_widget(dapm,
							      route->source,
							      true);
	struct snd_soc_dapm_widget *sink = dapm_find_widget(dapm,
							    route->sink,
							    true);
	struct snd_soc_dapm_path *path;
	int count = 0;

	if (!source) {
		dev_err(dapm->dev, "ASoC: Unable to find source %s for weak route\n",
			route->source);
		return -ENODEV;
	}

	if (!sink) {
		dev_err(dapm->dev, "ASoC: Unable to find sink %s for weak route\n",
			route->sink);
		return -ENODEV;
	}

	if (route->control || route->connected)
		dev_warn(dapm->dev, "ASoC: Ignoring control for weak route %s->%s\n",
			 route->source, route->sink);

	snd_soc_dapm_widget_for_each_sink_path(source, path) {
		if (path->sink == sink) {
			path->weak = 1;
			count++;
		}
	}

	if (count == 0)
		dev_err(dapm->dev, "ASoC: No path found for weak route %s->%s\n",
			route->source, route->sink);
	if (count > 1)
		dev_warn(dapm->dev, "ASoC: %d paths found for weak route %s->%s\n",
			 count, route->source, route->sink);

	return 0;
}

/**
 * snd_soc_dapm_weak_routes - Mark routes between DAPM widgets as weak
 * @dapm: DAPM context
 * @route: audio routes
 * @num: number of routes
 *
 * Mark existing routes matching those specified in the passed array
 * as being weak, meaning that they are ignored for the purpose of
 * power decisions.  The main intended use case is for sidetone paths
 * which couple audio between other independent paths if they are both
 * active in order to make the combination work better at the user
 * level but which aren't intended to be "used".
 *
 * Note that CODEC drivers should not use this as sidetone type paths
 * can frequently also be used as bypass paths.
 */
int snd_soc_dapm_weak_routes(struct snd_soc_dapm_context *dapm,
			     const struct snd_soc_dapm_route *route, int num)
{
	int i, err;
	int ret = 0;

	mutex_lock_nested(&dapm->card->dapm_mutex, SND_SOC_DAPM_CLASS_INIT);
	for (i = 0; i < num; i++) {
		err = snd_soc_dapm_weak_route(dapm, route);
		if (err)
			ret = err;
		route++;
	}
	mutex_unlock(&dapm->card->dapm_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_weak_routes);

/**
 * snd_soc_dapm_new_widgets - add new dapm widgets
 * @card: card to be checked for new dapm widgets
 *
 * Checks the codec for any new dapm widgets and creates them if found.
 *
 * Returns 0 for success.
 */
int snd_soc_dapm_new_widgets(struct snd_soc_codec *codec)
{
	struct snd_soc_dapm_widget *w;

	list_for_each_entry(w, &codec->dapm_widgets, list)
int snd_soc_dapm_new_widgets(struct snd_soc_card *card)
{
	struct snd_soc_dapm_widget *w;
	unsigned int val;

	mutex_lock_nested(&card->dapm_mutex, SND_SOC_DAPM_CLASS_INIT);

	list_for_each_entry(w, &card->widgets, list)
	{
		if (w->new)
			continue;

		switch(w->id) {
		case snd_soc_dapm_switch:
		case snd_soc_dapm_mixer:
			dapm_new_mixer(codec, w);
			break;
		case snd_soc_dapm_mux:
			dapm_new_mux(codec, w);
			break;
		case snd_soc_dapm_adc:
		case snd_soc_dapm_dac:
		case snd_soc_dapm_pga:
			dapm_new_pga(codec, w);
			break;
		case snd_soc_dapm_input:
		case snd_soc_dapm_output:
		case snd_soc_dapm_micbias:
		case snd_soc_dapm_spk:
		case snd_soc_dapm_hp:
		case snd_soc_dapm_mic:
		case snd_soc_dapm_line:
		case snd_soc_dapm_vmid:
		case snd_soc_dapm_pre:
		case snd_soc_dapm_post:
			break;
		}
		w->new = 1;
	}

	dapm_power_widgets(codec, SND_SOC_DAPM_STREAM_NOP);
		if (w->num_kcontrols) {
			w->kcontrols = kzalloc(w->num_kcontrols *
						sizeof(struct snd_kcontrol *),
						GFP_KERNEL);
			if (!w->kcontrols) {
				mutex_unlock(&card->dapm_mutex);
				return -ENOMEM;
			}
		}

		switch(w->id) {
		case snd_soc_dapm_switch:
		case snd_soc_dapm_mixer:
		case snd_soc_dapm_mixer_named_ctl:
			dapm_new_mixer(w);
			break;
		case snd_soc_dapm_mux:
		case snd_soc_dapm_demux:
			dapm_new_mux(w);
			break;
		case snd_soc_dapm_pga:
		case snd_soc_dapm_out_drv:
			dapm_new_pga(w);
			break;
		case snd_soc_dapm_dai_link:
			dapm_new_dai_link(w);
			break;
		default:
			break;
		}

		/* Read the initial power state from the device */
		if (w->reg >= 0) {
			soc_dapm_read(w->dapm, w->reg, &val);
			val = val >> w->shift;
			val &= w->mask;
			if (val == w->on_val)
				w->power = 1;
		}

		w->new = 1;

		dapm_mark_dirty(w, "new widget");
		dapm_debugfs_add_widget(w);
	}

	dapm_power_widgets(card, SND_SOC_DAPM_STREAM_NOP);
	mutex_unlock(&card->dapm_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_new_widgets);

/**
 * snd_soc_dapm_get_volsw - dapm mixer get callback
 * @kcontrol: mixer control
 * @uinfo: control element information
 * @ucontrol: control element information
 *
 * Callback to get the value of a dapm mixer control.
 *
 * Returns 0 for success.
 */
int snd_soc_dapm_get_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0x0f;
	int rshift = (kcontrol->private_value >> 12) & 0x0f;
	int max = (kcontrol->private_value >> 16) & 0xff;
	int invert = (kcontrol->private_value >> 24) & 0x01;
	int mask = (1 << fls(max)) - 1;

	/* return the saved value if we are powered down */
	if (widget->id == snd_soc_dapm_pga && !widget->power) {
		ucontrol->value.integer.value[0] = widget->saved_value;
		return 0;
	}

	ucontrol->value.integer.value[0] =
		(snd_soc_read(widget->codec, reg) >> shift) & mask;
	if (shift != rshift)
		ucontrol->value.integer.value[1] =
			(snd_soc_read(widget->codec, reg) >> rshift) & mask;
	if (invert) {
		ucontrol->value.integer.value[0] =
			max - ucontrol->value.integer.value[0];
		if (shift != rshift)
			ucontrol->value.integer.value[1] =
				max - ucontrol->value.integer.value[1];
	}

	return 0;
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct snd_soc_card *card = dapm->card;
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int reg = mc->reg;
	unsigned int shift = mc->shift;
	int max = mc->max;
	unsigned int mask = (1 << fls(max)) - 1;
	unsigned int invert = mc->invert;
	unsigned int val;
	int ret = 0;

	if (snd_soc_volsw_is_stereo(mc))
		dev_warn(dapm->dev,
			 "ASoC: Control '%s' is stereo, which is not supported\n",
			 kcontrol->id.name);

	mutex_lock_nested(&card->dapm_mutex, SND_SOC_DAPM_CLASS_RUNTIME);
	if (dapm_kcontrol_is_powered(kcontrol) && reg != SND_SOC_NOPM) {
		ret = soc_dapm_read(dapm, reg, &val);
		val = (val >> shift) & mask;
	} else {
		val = dapm_kcontrol_get_value(kcontrol);
	}
	mutex_unlock(&card->dapm_mutex);

	if (ret)
		return ret;

	if (invert)
		ucontrol->value.integer.value[0] = max - val;
	else
		ucontrol->value.integer.value[0] = val;

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_get_volsw);

/**
 * snd_soc_dapm_put_volsw - dapm mixer set callback
 * @kcontrol: mixer control
 * @uinfo: control element information
 * @ucontrol: control element information
 *
 * Callback to set the value of a dapm mixer control.
 *
 * Returns 0 for success.
 */
int snd_soc_dapm_put_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0x0f;
	int rshift = (kcontrol->private_value >> 12) & 0x0f;
	int max = (kcontrol->private_value >> 16) & 0xff;
	int mask = (1 << fls(max)) - 1;
	int invert = (kcontrol->private_value >> 24) & 0x01;
	unsigned short val, val2, val_mask;
	int ret;

	val = (ucontrol->value.integer.value[0] & mask);

	if (invert)
		val = max - val;
	val_mask = mask << shift;
	val = val << shift;
	if (shift != rshift) {
		val2 = (ucontrol->value.integer.value[1] & mask);
		if (invert)
			val2 = max - val2;
		val_mask |= mask << rshift;
		val |= val2 << rshift;
	}

	mutex_lock(&widget->codec->mutex);
	widget->value = val;

	/* save volume value if the widget is powered down */
	if (widget->id == snd_soc_dapm_pga && !widget->power) {
		widget->saved_value = val;
		mutex_unlock(&widget->codec->mutex);
		return 1;
	}

	dapm_mixer_update_power(widget, kcontrol, reg, val_mask, val, invert);
	if (widget->event) {
		if (widget->event_flags & SND_SOC_DAPM_PRE_REG) {
			ret = widget->event(widget, kcontrol,
						SND_SOC_DAPM_PRE_REG);
			if (ret < 0) {
				ret = 1;
				goto out;
			}
		}
		ret = snd_soc_update_bits(widget->codec, reg, val_mask, val);
		if (widget->event_flags & SND_SOC_DAPM_POST_REG)
			ret = widget->event(widget, kcontrol,
						SND_SOC_DAPM_POST_REG);
	} else
		ret = snd_soc_update_bits(widget->codec, reg, val_mask, val);

out:
	mutex_unlock(&widget->codec->mutex);
	return ret;
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct snd_soc_card *card = dapm->card;
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int reg = mc->reg;
	unsigned int shift = mc->shift;
	int max = mc->max;
	unsigned int mask = (1 << fls(max)) - 1;
	unsigned int invert = mc->invert;
	unsigned int val;
	int connect, change, reg_change = 0;
	struct snd_soc_dapm_update update;
	int ret = 0;

	if (snd_soc_volsw_is_stereo(mc))
		dev_warn(dapm->dev,
			 "ASoC: Control '%s' is stereo, which is not supported\n",
			 kcontrol->id.name);

	val = (ucontrol->value.integer.value[0] & mask);
	connect = !!val;

	if (invert)
		val = max - val;

	mutex_lock_nested(&card->dapm_mutex, SND_SOC_DAPM_CLASS_RUNTIME);

	change = dapm_kcontrol_set_value(kcontrol, val);

	if (reg != SND_SOC_NOPM) {
		mask = mask << shift;
		val = val << shift;

		reg_change = soc_dapm_test_bits(dapm, reg, mask, val);
	}

	if (change || reg_change) {
		if (reg_change) {
			update.kcontrol = kcontrol;
			update.reg = reg;
			update.mask = mask;
			update.val = val;
			card->update = &update;
		}

		ret = soc_dapm_mixer_update_power(card, kcontrol, connect);

		card->update = NULL;
	}

	mutex_unlock(&card->dapm_mutex);

	if (ret > 0)
		soc_dpcm_runtime_update(card);

	return change;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_put_volsw);

/**
 * snd_soc_dapm_get_enum_double - dapm enumerated double mixer get callback
 * @kcontrol: mixer control
 * @uinfo: control element information
 * @ucontrol: control element information
 *
 * Callback to get the value of a dapm enumerated double mixer control.
 *
 * Returns 0 for success.
 */
int snd_soc_dapm_get_enum_double(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget = snd_kcontrol_chip(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned short val, bitmask;

	for (bitmask = 1; bitmask < e->mask; bitmask <<= 1)
		;
	val = snd_soc_read(widget->codec, e->reg);
	ucontrol->value.enumerated.item[0] = (val >> e->shift_l) & (bitmask - 1);
	if (e->shift_l != e->shift_r)
		ucontrol->value.enumerated.item[1] =
			(val >> e->shift_r) & (bitmask - 1);
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct snd_soc_card *card = dapm->card;
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int reg_val, val;

	mutex_lock_nested(&card->dapm_mutex, SND_SOC_DAPM_CLASS_RUNTIME);
	if (e->reg != SND_SOC_NOPM && dapm_kcontrol_is_powered(kcontrol)) {
		int ret = soc_dapm_read(dapm, e->reg, &reg_val);
		if (ret) {
			mutex_unlock(&card->dapm_mutex);
			return ret;
		}
	} else {
		reg_val = dapm_kcontrol_get_value(kcontrol);
	}
	mutex_unlock(&card->dapm_mutex);

	val = (reg_val >> e->shift_l) & e->mask;
	ucontrol->value.enumerated.item[0] = snd_soc_enum_val_to_item(e, val);
	if (e->shift_l != e->shift_r) {
		val = (reg_val >> e->shift_r) & e->mask;
		val = snd_soc_enum_val_to_item(e, val);
		ucontrol->value.enumerated.item[1] = val;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_get_enum_double);

/**
 * snd_soc_dapm_put_enum_double - dapm enumerated double mixer set callback
 * @kcontrol: mixer control
 * @uinfo: control element information
 * @ucontrol: control element information
 *
 * Callback to set the value of a dapm enumerated double mixer control.
 *
 * Returns 0 for success.
 */
int snd_soc_dapm_put_enum_double(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget = snd_kcontrol_chip(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned short val, mux;
	unsigned short mask, bitmask;
	int ret = 0;

	for (bitmask = 1; bitmask < e->mask; bitmask <<= 1)
		;
	if (ucontrol->value.enumerated.item[0] > e->mask - 1)
		return -EINVAL;
	mux = ucontrol->value.enumerated.item[0];
	val = mux << e->shift_l;
	mask = (bitmask - 1) << e->shift_l;
	if (e->shift_l != e->shift_r) {
		if (ucontrol->value.enumerated.item[1] > e->mask - 1)
			return -EINVAL;
		val |= ucontrol->value.enumerated.item[1] << e->shift_r;
		mask |= (bitmask - 1) << e->shift_r;
	}

	mutex_lock(&widget->codec->mutex);
	widget->value = val;
	dapm_mux_update_power(widget, kcontrol, mask, mux, e);
	if (widget->event) {
		if (widget->event_flags & SND_SOC_DAPM_PRE_REG) {
			ret = widget->event(widget,
				kcontrol, SND_SOC_DAPM_PRE_REG);
			if (ret < 0)
				goto out;
		}
		ret = snd_soc_update_bits(widget->codec, e->reg, mask, val);
		if (widget->event_flags & SND_SOC_DAPM_POST_REG)
			ret = widget->event(widget,
				kcontrol, SND_SOC_DAPM_POST_REG);
	} else
		ret = snd_soc_update_bits(widget->codec, e->reg, mask, val);

out:
	mutex_unlock(&widget->codec->mutex);
	return ret;
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct snd_soc_card *card = dapm->card;
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int *item = ucontrol->value.enumerated.item;
	unsigned int val, change, reg_change = 0;
	unsigned int mask;
	struct snd_soc_dapm_update update;
	int ret = 0;

	if (item[0] >= e->items)
		return -EINVAL;

	val = snd_soc_enum_item_to_val(e, item[0]) << e->shift_l;
	mask = e->mask << e->shift_l;
	if (e->shift_l != e->shift_r) {
		if (item[1] > e->items)
			return -EINVAL;
		val |= snd_soc_enum_item_to_val(e, item[1]) << e->shift_r;
		mask |= e->mask << e->shift_r;
	}

	mutex_lock_nested(&card->dapm_mutex, SND_SOC_DAPM_CLASS_RUNTIME);

	change = dapm_kcontrol_set_value(kcontrol, val);

	if (e->reg != SND_SOC_NOPM)
		reg_change = soc_dapm_test_bits(dapm, e->reg, mask, val);

	if (change || reg_change) {
		if (reg_change) {
			update.kcontrol = kcontrol;
			update.reg = e->reg;
			update.mask = mask;
			update.val = val;
			card->update = &update;
		}

		ret = soc_dapm_mux_update_power(card, kcontrol, item[0], e);

		card->update = NULL;
	}

	mutex_unlock(&card->dapm_mutex);

	if (ret > 0)
		soc_dpcm_runtime_update(card);

	return change;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_put_enum_double);

/**
 * snd_soc_dapm_new_control - create new dapm control
 * @codec: audio codec
 * @widget: widget template
 *
 * Creates a new dapm control based upon the template.
 *
 * Returns 0 for success else error.
 */
int snd_soc_dapm_new_control(struct snd_soc_codec *codec,
 * snd_soc_dapm_info_pin_switch - Info for a pin switch
 *
 * @kcontrol: mixer control
 * @uinfo: control element information
 *
 * Callback to provide information about a pin switch control.
 */
int snd_soc_dapm_info_pin_switch(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_info_pin_switch);

/**
 * snd_soc_dapm_get_pin_switch - Get information for a pin switch
 *
 * @kcontrol: mixer control
 * @ucontrol: Value
 */
int snd_soc_dapm_get_pin_switch(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);
	const char *pin = (const char *)kcontrol->private_value;

	mutex_lock_nested(&card->dapm_mutex, SND_SOC_DAPM_CLASS_RUNTIME);

	ucontrol->value.integer.value[0] =
		snd_soc_dapm_get_pin_status(&card->dapm, pin);

	mutex_unlock(&card->dapm_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_get_pin_switch);

/**
 * snd_soc_dapm_put_pin_switch - Set information for a pin switch
 *
 * @kcontrol: mixer control
 * @ucontrol: Value
 */
int snd_soc_dapm_put_pin_switch(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);
	const char *pin = (const char *)kcontrol->private_value;
	int ret;

	mutex_lock_nested(&card->dapm_mutex, SND_SOC_DAPM_CLASS_RUNTIME);
	ret = __snd_soc_dapm_set_pin(&card->dapm, pin,
				     !!ucontrol->value.integer.value[0]);
	mutex_unlock(&card->dapm_mutex);

	snd_soc_dapm_sync(&card->dapm);
	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_put_pin_switch);

struct snd_soc_dapm_widget *
snd_soc_dapm_new_control(struct snd_soc_dapm_context *dapm,
	const struct snd_soc_dapm_widget *widget)
{
	struct snd_soc_dapm_widget *w;

	if ((w = dapm_cnew_widget(widget)) == NULL)
		return -ENOMEM;

	w->codec = codec;
	INIT_LIST_HEAD(&w->sources);
	INIT_LIST_HEAD(&w->sinks);
	INIT_LIST_HEAD(&w->list);
	list_add(&w->list, &codec->dapm_widgets);

	/* machine layer set ups unconnected pins and insertions */
	w->connected = 1;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_new_control);

/**
 * snd_soc_dapm_new_controls - create new dapm controls
 * @codec: audio codec
	mutex_lock_nested(&dapm->card->dapm_mutex, SND_SOC_DAPM_CLASS_RUNTIME);
	w = snd_soc_dapm_new_control_unlocked(dapm, widget);
	/* Do not nag about probe deferrals */
	if (IS_ERR(w)) {
		int ret = PTR_ERR(w);

		if (ret != -EPROBE_DEFER)
			dev_err(dapm->dev,
				"ASoC: Failed to create DAPM control %s (%d)\n",
				widget->name, ret);
		goto out_unlock;
	}
	if (!w)
		dev_err(dapm->dev,
			"ASoC: Failed to create DAPM control %s\n",
			widget->name);

out_unlock:
	mutex_unlock(&dapm->card->dapm_mutex);
	return w;
}

struct snd_soc_dapm_widget *
snd_soc_dapm_new_control_unlocked(struct snd_soc_dapm_context *dapm,
			 const struct snd_soc_dapm_widget *widget)
{
	enum snd_soc_dapm_direction dir;
	struct snd_soc_dapm_widget *w;
	const char *prefix;
	int ret;

	if ((w = dapm_cnew_widget(widget)) == NULL)
		return NULL;

	switch (w->id) {
	case snd_soc_dapm_regulator_supply:
		w->regulator = devm_regulator_get(dapm->dev, w->name);
		if (IS_ERR(w->regulator)) {
			ret = PTR_ERR(w->regulator);
			if (ret == -EPROBE_DEFER)
				return ERR_PTR(ret);
			dev_err(dapm->dev, "ASoC: Failed to request %s: %d\n",
				w->name, ret);
			return NULL;
		}

		if (w->on_val & SND_SOC_DAPM_REGULATOR_BYPASS) {
			ret = regulator_allow_bypass(w->regulator, true);
			if (ret != 0)
				dev_warn(w->dapm->dev,
					 "ASoC: Failed to bypass %s: %d\n",
					 w->name, ret);
		}
		break;
	case snd_soc_dapm_clock_supply:
#ifdef CONFIG_CLKDEV_LOOKUP
		w->clk = devm_clk_get(dapm->dev, w->name);
		if (IS_ERR(w->clk)) {
			ret = PTR_ERR(w->clk);
			if (ret == -EPROBE_DEFER)
				return ERR_PTR(ret);
			dev_err(dapm->dev, "ASoC: Failed to request %s: %d\n",
				w->name, ret);
			return NULL;
		}
#else
		return NULL;
#endif
		break;
	default:
		break;
	}

	prefix = soc_dapm_prefix(dapm);
	if (prefix)
		w->name = kasprintf(GFP_KERNEL, "%s %s", prefix, widget->name);
	else
		w->name = kstrdup_const(widget->name, GFP_KERNEL);
	if (w->name == NULL) {
		kfree(w);
		return NULL;
	}

	switch (w->id) {
	case snd_soc_dapm_mic:
		w->is_ep = SND_SOC_DAPM_EP_SOURCE;
		w->power_check = dapm_generic_check_power;
		break;
	case snd_soc_dapm_input:
		if (!dapm->card->fully_routed)
			w->is_ep = SND_SOC_DAPM_EP_SOURCE;
		w->power_check = dapm_generic_check_power;
		break;
	case snd_soc_dapm_spk:
	case snd_soc_dapm_hp:
		w->is_ep = SND_SOC_DAPM_EP_SINK;
		w->power_check = dapm_generic_check_power;
		break;
	case snd_soc_dapm_output:
		if (!dapm->card->fully_routed)
			w->is_ep = SND_SOC_DAPM_EP_SINK;
		w->power_check = dapm_generic_check_power;
		break;
	case snd_soc_dapm_vmid:
	case snd_soc_dapm_siggen:
		w->is_ep = SND_SOC_DAPM_EP_SOURCE;
		w->power_check = dapm_always_on_check_power;
		break;
	case snd_soc_dapm_mux:
	case snd_soc_dapm_demux:
	case snd_soc_dapm_switch:
	case snd_soc_dapm_mixer:
	case snd_soc_dapm_mixer_named_ctl:
	case snd_soc_dapm_adc:
	case snd_soc_dapm_aif_out:
	case snd_soc_dapm_dac:
	case snd_soc_dapm_aif_in:
	case snd_soc_dapm_pga:
	case snd_soc_dapm_out_drv:
	case snd_soc_dapm_micbias:
	case snd_soc_dapm_line:
	case snd_soc_dapm_dai_link:
	case snd_soc_dapm_dai_out:
	case snd_soc_dapm_dai_in:
		w->power_check = dapm_generic_check_power;
		break;
	case snd_soc_dapm_supply:
	case snd_soc_dapm_regulator_supply:
	case snd_soc_dapm_clock_supply:
	case snd_soc_dapm_kcontrol:
		w->is_supply = 1;
		w->power_check = dapm_supply_check_power;
		break;
	default:
		w->power_check = dapm_always_on_check_power;
		break;
	}

	w->dapm = dapm;
	INIT_LIST_HEAD(&w->list);
	INIT_LIST_HEAD(&w->dirty);
	list_add_tail(&w->list, &dapm->card->widgets);

	snd_soc_dapm_for_each_direction(dir) {
		INIT_LIST_HEAD(&w->edges[dir]);
		w->endpoints[dir] = -1;
	}

	/* machine layer set ups unconnected pins and insertions */
	w->connected = 1;
	return w;
}

/**
 * snd_soc_dapm_new_controls - create new dapm controls
 * @dapm: DAPM context
 * @widget: widget array
 * @num: number of widgets
 *
 * Creates new DAPM controls based upon the templates.
 *
 * Returns 0 for success else error.
 */
int snd_soc_dapm_new_controls(struct snd_soc_codec *codec,
	const struct snd_soc_dapm_widget *widget,
	int num)
{
	int i, ret;

	for (i = 0; i < num; i++) {
		ret = snd_soc_dapm_new_control(codec, widget);
		if (ret < 0)
			return ret;
		widget++;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_new_controls);


/**
 * snd_soc_dapm_stream_event - send a stream event to the dapm core
 * @codec: audio codec
int snd_soc_dapm_new_controls(struct snd_soc_dapm_context *dapm,
	const struct snd_soc_dapm_widget *widget,
	int num)
{
	struct snd_soc_dapm_widget *w;
	int i;
	int ret = 0;

	mutex_lock_nested(&dapm->card->dapm_mutex, SND_SOC_DAPM_CLASS_INIT);
	for (i = 0; i < num; i++) {
		w = snd_soc_dapm_new_control_unlocked(dapm, widget);
		if (IS_ERR(w)) {
			ret = PTR_ERR(w);
			/* Do not nag about probe deferrals */
			if (ret == -EPROBE_DEFER)
				break;
			dev_err(dapm->dev,
				"ASoC: Failed to create DAPM control %s (%d)\n",
				widget->name, ret);
			break;
		}
		if (!w) {
			dev_err(dapm->dev,
				"ASoC: Failed to create DAPM control %s\n",
				widget->name);
			ret = -ENOMEM;
			break;
		}
		widget++;
	}
	mutex_unlock(&dapm->card->dapm_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_new_controls);

static int snd_soc_dai_link_event(struct snd_soc_dapm_widget *w,
				  struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_dapm_path *source_p, *sink_p;
	struct snd_soc_dai *source, *sink;
	const struct snd_soc_pcm_stream *config = w->params + w->params_select;
	struct snd_pcm_substream substream;
	struct snd_pcm_hw_params *params = NULL;
	u64 fmt;
	int ret;

	if (WARN_ON(!config) ||
	    WARN_ON(list_empty(&w->edges[SND_SOC_DAPM_DIR_OUT]) ||
		    list_empty(&w->edges[SND_SOC_DAPM_DIR_IN])))
		return -EINVAL;

	/* We only support a single source and sink, pick the first */
	source_p = list_first_entry(&w->edges[SND_SOC_DAPM_DIR_OUT],
				    struct snd_soc_dapm_path,
				    list_node[SND_SOC_DAPM_DIR_OUT]);
	sink_p = list_first_entry(&w->edges[SND_SOC_DAPM_DIR_IN],
				    struct snd_soc_dapm_path,
				    list_node[SND_SOC_DAPM_DIR_IN]);

	source = source_p->source->priv;
	sink = sink_p->sink->priv;

	/* Be a little careful as we don't want to overflow the mask array */
	if (config->formats) {
		fmt = ffs(config->formats) - 1;
	} else {
		dev_warn(w->dapm->dev, "ASoC: Invalid format %llx specified\n",
			 config->formats);
		fmt = 0;
	}

	/* Currently very limited parameter selection */
	params = kzalloc(sizeof(*params), GFP_KERNEL);
	if (!params) {
		ret = -ENOMEM;
		goto out;
	}
	snd_mask_set(hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT), fmt);

	hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE)->min =
		config->rate_min;
	hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE)->max =
		config->rate_max;

	hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS)->min
		= config->channels_min;
	hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS)->max
		= config->channels_max;

	memset(&substream, 0, sizeof(substream));

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		substream.stream = SNDRV_PCM_STREAM_CAPTURE;
		if (source->driver->ops && source->driver->ops->startup) {
			ret = source->driver->ops->startup(&substream, source);
			if (ret < 0) {
				dev_err(source->dev,
					"ASoC: startup() failed: %d\n", ret);
				goto out;
			}
			source->active++;
		}
		ret = soc_dai_hw_params(&substream, params, source);
		if (ret < 0)
			goto out;

		substream.stream = SNDRV_PCM_STREAM_PLAYBACK;
		if (sink->driver->ops && sink->driver->ops->startup) {
			ret = sink->driver->ops->startup(&substream, sink);
			if (ret < 0) {
				dev_err(sink->dev,
					"ASoC: startup() failed: %d\n", ret);
				goto out;
			}
			sink->active++;
		}
		ret = soc_dai_hw_params(&substream, params, sink);
		if (ret < 0)
			goto out;
		break;

	case SND_SOC_DAPM_POST_PMU:
		ret = snd_soc_dai_digital_mute(sink, 0,
					       SNDRV_PCM_STREAM_PLAYBACK);
		if (ret != 0 && ret != -ENOTSUPP)
			dev_warn(sink->dev, "ASoC: Failed to unmute: %d\n", ret);
		ret = 0;
		break;

	case SND_SOC_DAPM_PRE_PMD:
		ret = snd_soc_dai_digital_mute(sink, 1,
					       SNDRV_PCM_STREAM_PLAYBACK);
		if (ret != 0 && ret != -ENOTSUPP)
			dev_warn(sink->dev, "ASoC: Failed to mute: %d\n", ret);
		ret = 0;

		source->active--;
		if (source->driver->ops && source->driver->ops->shutdown) {
			substream.stream = SNDRV_PCM_STREAM_CAPTURE;
			source->driver->ops->shutdown(&substream, source);
		}

		sink->active--;
		if (sink->driver->ops && sink->driver->ops->shutdown) {
			substream.stream = SNDRV_PCM_STREAM_PLAYBACK;
			sink->driver->ops->shutdown(&substream, sink);
		}
		break;

	default:
		WARN(1, "Unknown event %d\n", event);
		ret = -EINVAL;
	}

out:
	kfree(params);
	return ret;
}

static int snd_soc_dapm_dai_link_get(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *w = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = w->params_select;

	return 0;
}

static int snd_soc_dapm_dai_link_put(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *w = snd_kcontrol_chip(kcontrol);

	/* Can't change the config when widget is already powered */
	if (w->power)
		return -EBUSY;

	if (ucontrol->value.enumerated.item[0] == w->params_select)
		return 0;

	if (ucontrol->value.enumerated.item[0] >= w->num_params)
		return -EINVAL;

	w->params_select = ucontrol->value.enumerated.item[0];

	return 1;
}

int snd_soc_dapm_new_pcm(struct snd_soc_card *card,
			 const struct snd_soc_pcm_stream *params,
			 unsigned int num_params,
			 struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_dapm_widget template;
	struct snd_soc_dapm_widget *w;
	char *link_name;
	int ret, count;
	unsigned long private_value;
	const char **w_param_text;
	struct soc_enum w_param_enum[] = {
		SOC_ENUM_SINGLE(0, 0, 0, NULL),
	};
	struct snd_kcontrol_new kcontrol_dai_link[] = {
		SOC_ENUM_EXT(NULL, w_param_enum[0],
			     snd_soc_dapm_dai_link_get,
			     snd_soc_dapm_dai_link_put),
	};
	const struct snd_soc_pcm_stream *config = params;

	w_param_text = devm_kcalloc(card->dev, num_params,
					sizeof(char *), GFP_KERNEL);
	if (!w_param_text)
		return -ENOMEM;

	link_name = devm_kasprintf(card->dev, GFP_KERNEL, "%s-%s",
				   source->name, sink->name);
	if (!link_name) {
		ret = -ENOMEM;
		goto outfree_w_param;
	}

	for (count = 0 ; count < num_params; count++) {
		if (!config->stream_name) {
			dev_warn(card->dapm.dev,
				"ASoC: anonymous config %d for dai link %s\n",
				count, link_name);
			w_param_text[count] =
				devm_kasprintf(card->dev, GFP_KERNEL,
					       "Anonymous Configuration %d",
					       count);
			if (!w_param_text[count]) {
				ret = -ENOMEM;
				goto outfree_link_name;
			}
		} else {
			w_param_text[count] = devm_kmemdup(card->dev,
						config->stream_name,
						strlen(config->stream_name) + 1,
						GFP_KERNEL);
			if (!w_param_text[count]) {
				ret = -ENOMEM;
				goto outfree_link_name;
			}
		}
		config++;
	}
	w_param_enum[0].items = num_params;
	w_param_enum[0].texts = w_param_text;

	memset(&template, 0, sizeof(template));
	template.reg = SND_SOC_NOPM;
	template.id = snd_soc_dapm_dai_link;
	template.name = link_name;
	template.event = snd_soc_dai_link_event;
	template.event_flags = SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
		SND_SOC_DAPM_PRE_PMD;
	template.num_kcontrols = 1;
	/* duplicate w_param_enum on heap so that memory persists */
	private_value =
		(unsigned long) devm_kmemdup(card->dev,
			(void *)(kcontrol_dai_link[0].private_value),
			sizeof(struct soc_enum), GFP_KERNEL);
	if (!private_value) {
		dev_err(card->dev, "ASoC: Failed to create control for %s widget\n",
			link_name);
		ret = -ENOMEM;
		goto outfree_link_name;
	}
	kcontrol_dai_link[0].private_value = private_value;
	/* duplicate kcontrol_dai_link on heap so that memory persists */
	template.kcontrol_news =
				devm_kmemdup(card->dev, &kcontrol_dai_link[0],
					sizeof(struct snd_kcontrol_new),
					GFP_KERNEL);
	if (!template.kcontrol_news) {
		dev_err(card->dev, "ASoC: Failed to create control for %s widget\n",
			link_name);
		ret = -ENOMEM;
		goto outfree_private_value;
	}

	dev_dbg(card->dev, "ASoC: adding %s widget\n", link_name);

	w = snd_soc_dapm_new_control_unlocked(&card->dapm, &template);
	if (IS_ERR(w)) {
		ret = PTR_ERR(w);
		/* Do not nag about probe deferrals */
		if (ret != -EPROBE_DEFER)
			dev_err(card->dev,
				"ASoC: Failed to create %s widget (%d)\n",
				link_name, ret);
		goto outfree_kcontrol_news;
	}
	if (!w) {
		dev_err(card->dev, "ASoC: Failed to create %s widget\n",
			link_name);
		ret = -ENOMEM;
		goto outfree_kcontrol_news;
	}

	w->params = params;
	w->num_params = num_params;

	ret = snd_soc_dapm_add_path(&card->dapm, source, w, NULL, NULL);
	if (ret)
		goto outfree_w;
	return snd_soc_dapm_add_path(&card->dapm, w, sink, NULL, NULL);

outfree_w:
	devm_kfree(card->dev, w);
outfree_kcontrol_news:
	devm_kfree(card->dev, (void *)template.kcontrol_news);
outfree_private_value:
	devm_kfree(card->dev, (void *)private_value);
outfree_link_name:
	devm_kfree(card->dev, link_name);
outfree_w_param:
	for (count = 0 ; count < num_params; count++)
		devm_kfree(card->dev, (void *)w_param_text[count]);
	devm_kfree(card->dev, w_param_text);

	return ret;
}

int snd_soc_dapm_new_dai_widgets(struct snd_soc_dapm_context *dapm,
				 struct snd_soc_dai *dai)
{
	struct snd_soc_dapm_widget template;
	struct snd_soc_dapm_widget *w;

	WARN_ON(dapm->dev != dai->dev);

	memset(&template, 0, sizeof(template));
	template.reg = SND_SOC_NOPM;

	if (dai->driver->playback.stream_name) {
		template.id = snd_soc_dapm_dai_in;
		template.name = dai->driver->playback.stream_name;
		template.sname = dai->driver->playback.stream_name;

		dev_dbg(dai->dev, "ASoC: adding %s widget\n",
			template.name);

		w = snd_soc_dapm_new_control_unlocked(dapm, &template);
		if (IS_ERR(w)) {
			int ret = PTR_ERR(w);

			/* Do not nag about probe deferrals */
			if (ret != -EPROBE_DEFER)
				dev_err(dapm->dev,
				"ASoC: Failed to create %s widget (%d)\n",
				dai->driver->playback.stream_name, ret);
			return ret;
		}
		if (!w) {
			dev_err(dapm->dev, "ASoC: Failed to create %s widget\n",
				dai->driver->playback.stream_name);
			return -ENOMEM;
		}

		w->priv = dai;
		dai->playback_widget = w;
	}

	if (dai->driver->capture.stream_name) {
		template.id = snd_soc_dapm_dai_out;
		template.name = dai->driver->capture.stream_name;
		template.sname = dai->driver->capture.stream_name;

		dev_dbg(dai->dev, "ASoC: adding %s widget\n",
			template.name);

		w = snd_soc_dapm_new_control_unlocked(dapm, &template);
		if (IS_ERR(w)) {
			int ret = PTR_ERR(w);

			/* Do not nag about probe deferrals */
			if (ret != -EPROBE_DEFER)
				dev_err(dapm->dev,
				"ASoC: Failed to create %s widget (%d)\n",
				dai->driver->playback.stream_name, ret);
			return ret;
		}
		if (!w) {
			dev_err(dapm->dev, "ASoC: Failed to create %s widget\n",
				dai->driver->capture.stream_name);
			return -ENOMEM;
		}

		w->priv = dai;
		dai->capture_widget = w;
	}

	return 0;
}

int snd_soc_dapm_link_dai_widgets(struct snd_soc_card *card)
{
	struct snd_soc_dapm_widget *dai_w, *w;
	struct snd_soc_dapm_widget *src, *sink;
	struct snd_soc_dai *dai;

	/* For each DAI widget... */
	list_for_each_entry(dai_w, &card->widgets, list) {
		switch (dai_w->id) {
		case snd_soc_dapm_dai_in:
		case snd_soc_dapm_dai_out:
			break;
		default:
			continue;
		}

		/* let users know there is no DAI to link */
		if (!dai_w->priv) {
			dev_dbg(card->dev, "dai widget %s has no DAI\n",
				dai_w->name);
			continue;
		}

		dai = dai_w->priv;

		/* ...find all widgets with the same stream and link them */
		list_for_each_entry(w, &card->widgets, list) {
			if (w->dapm != dai_w->dapm)
				continue;

			switch (w->id) {
			case snd_soc_dapm_dai_in:
			case snd_soc_dapm_dai_out:
				continue;
			default:
				break;
			}

			if (!w->sname || !strstr(w->sname, dai_w->sname))
				continue;

			if (dai_w->id == snd_soc_dapm_dai_in) {
				src = dai_w;
				sink = w;
			} else {
				src = w;
				sink = dai_w;
			}
			dev_dbg(dai->dev, "%s -> %s\n", src->name, sink->name);
			snd_soc_dapm_add_path(w->dapm, src, sink, NULL, NULL);
		}
	}

	return 0;
}

static void dapm_connect_dai_link_widgets(struct snd_soc_card *card,
					  struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dapm_widget *sink, *source;
	int i;

	for (i = 0; i < rtd->num_codecs; i++) {
		struct snd_soc_dai *codec_dai = rtd->codec_dais[i];

		/* connect BE DAI playback if widgets are valid */
		if (codec_dai->playback_widget && cpu_dai->playback_widget) {
			source = cpu_dai->playback_widget;
			sink = codec_dai->playback_widget;
			dev_dbg(rtd->dev, "connected DAI link %s:%s -> %s:%s\n",
				cpu_dai->component->name, source->name,
				codec_dai->component->name, sink->name);

			snd_soc_dapm_add_path(&card->dapm, source, sink,
				NULL, NULL);
		}

		/* connect BE DAI capture if widgets are valid */
		if (codec_dai->capture_widget && cpu_dai->capture_widget) {
			source = codec_dai->capture_widget;
			sink = cpu_dai->capture_widget;
			dev_dbg(rtd->dev, "connected DAI link %s:%s -> %s:%s\n",
				codec_dai->component->name, source->name,
				cpu_dai->component->name, sink->name);

			snd_soc_dapm_add_path(&card->dapm, source, sink,
				NULL, NULL);
		}
	}
}

static void soc_dapm_dai_stream_event(struct snd_soc_dai *dai, int stream,
	int event)
{
	struct snd_soc_dapm_widget *w;
	unsigned int ep;

	if (stream == SNDRV_PCM_STREAM_PLAYBACK)
		w = dai->playback_widget;
	else
		w = dai->capture_widget;

	if (w) {
		dapm_mark_dirty(w, "stream event");

		if (w->id == snd_soc_dapm_dai_in) {
			ep = SND_SOC_DAPM_EP_SOURCE;
			dapm_widget_invalidate_input_paths(w);
		} else {
			ep = SND_SOC_DAPM_EP_SINK;
			dapm_widget_invalidate_output_paths(w);
		}

		switch (event) {
		case SND_SOC_DAPM_STREAM_START:
			w->active = 1;
			w->is_ep = ep;
			break;
		case SND_SOC_DAPM_STREAM_STOP:
			w->active = 0;
			w->is_ep = 0;
			break;
		case SND_SOC_DAPM_STREAM_SUSPEND:
		case SND_SOC_DAPM_STREAM_RESUME:
		case SND_SOC_DAPM_STREAM_PAUSE_PUSH:
		case SND_SOC_DAPM_STREAM_PAUSE_RELEASE:
			break;
		}
	}
}

void snd_soc_dapm_connect_dai_link_widgets(struct snd_soc_card *card)
{
	struct snd_soc_pcm_runtime *rtd = card->rtd;
	int i;

	/* for each BE DAI link... */
	for (i = 0; i < card->num_rtd; i++) {
		rtd = &card->rtd[i];

		/*
		 * dynamic FE links have no fixed DAI mapping.
		 * CODEC<->CODEC links have no direct connection.
		 */
		if (rtd->dai_link->dynamic || rtd->dai_link->params)
			continue;

		dapm_connect_dai_link_widgets(card, rtd);
	}
}

static void soc_dapm_stream_event(struct snd_soc_pcm_runtime *rtd, int stream,
	int event)
{
	int i;

	soc_dapm_dai_stream_event(rtd->cpu_dai, stream, event);
	for (i = 0; i < rtd->num_codecs; i++)
		soc_dapm_dai_stream_event(rtd->codec_dais[i], stream, event);

	dapm_power_widgets(rtd->card, event);
}

/**
 * snd_soc_dapm_stream_event - send a stream event to the dapm core
 * @rtd: PCM runtime data
 * @stream: stream name
 * @event: stream event
 *
 * Sends a stream event to the dapm core. The core then makes any
 * necessary widget power changes.
 *
 * Returns 0 for success else error.
 */
int snd_soc_dapm_stream_event(struct snd_soc_codec *codec,
	char *stream, int event)
{
	struct snd_soc_dapm_widget *w;

	if (stream == NULL)
		return 0;

	mutex_lock(&codec->mutex);
	list_for_each_entry(w, &codec->dapm_widgets, list)
	{
		if (!w->sname)
			continue;
		pr_debug("widget %s\n %s stream %s event %d\n",
			 w->name, w->sname, stream, event);
		if (strstr(w->sname, stream)) {
			switch(event) {
			case SND_SOC_DAPM_STREAM_START:
				w->active = 1;
				break;
			case SND_SOC_DAPM_STREAM_STOP:
				w->active = 0;
				break;
			case SND_SOC_DAPM_STREAM_SUSPEND:
				if (w->active)
					w->suspend = 1;
				w->active = 0;
				break;
			case SND_SOC_DAPM_STREAM_RESUME:
				if (w->suspend) {
					w->active = 1;
					w->suspend = 0;
				}
				break;
			case SND_SOC_DAPM_STREAM_PAUSE_PUSH:
				break;
			case SND_SOC_DAPM_STREAM_PAUSE_RELEASE:
				break;
			}
		}
	}
	mutex_unlock(&codec->mutex);

	dapm_power_widgets(codec, event);
	dump_dapm(codec, __func__);
	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_stream_event);

/**
 * snd_soc_dapm_set_bias_level - set the bias level for the system
 * @socdev: audio device
 * @level: level to configure
 *
 * Configure the bias (power) levels for the SoC audio device.
 *
 * Returns 0 for success else error.
 */
int snd_soc_dapm_set_bias_level(struct snd_soc_device *socdev,
				enum snd_soc_bias_level level)
{
	struct snd_soc_codec *codec = socdev->codec;
	struct snd_soc_machine *machine = socdev->machine;
	int ret = 0;

	if (machine->set_bias_level)
		ret = machine->set_bias_level(machine, level);
	if (ret == 0 && codec->set_bias_level)
		ret = codec->set_bias_level(codec, level);

	return ret;
}

/**
 * snd_soc_dapm_enable_pin - enable pin.
 * @snd_soc_codec: SoC codec
 * @pin: pin name
 *
 * Enables input/output pin and it's parents or children widgets iff there is
 * a valid audio route and active audio stream.
 * NOTE: snd_soc_dapm_sync() needs to be called after this for DAPM to
 * do any widget power switching.
 */
int snd_soc_dapm_enable_pin(struct snd_soc_codec *codec, char *pin)
{
	return snd_soc_dapm_set_pin(codec, pin, 1);
void snd_soc_dapm_stream_event(struct snd_soc_pcm_runtime *rtd, int stream,
			      int event)
{
	struct snd_soc_card *card = rtd->card;

	mutex_lock_nested(&card->dapm_mutex, SND_SOC_DAPM_CLASS_RUNTIME);
	soc_dapm_stream_event(rtd, stream, event);
	mutex_unlock(&card->dapm_mutex);
}

/**
 * snd_soc_dapm_enable_pin_unlocked - enable pin.
 * @dapm: DAPM context
 * @pin: pin name
 *
 * Enables input/output pin and its parents or children widgets iff there is
 * a valid audio route and active audio stream.
 *
 * Requires external locking.
 *
 * NOTE: snd_soc_dapm_sync() needs to be called after this for DAPM to
 * do any widget power switching.
 */
int snd_soc_dapm_enable_pin_unlocked(struct snd_soc_dapm_context *dapm,
				   const char *pin)
{
	return snd_soc_dapm_set_pin(dapm, pin, 1);
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_enable_pin_unlocked);

/**
 * snd_soc_dapm_enable_pin - enable pin.
 * @dapm: DAPM context
 * @pin: pin name
 *
 * Enables input/output pin and its parents or children widgets iff there is
 * a valid audio route and active audio stream.
 *
 * NOTE: snd_soc_dapm_sync() needs to be called after this for DAPM to
 * do any widget power switching.
 */
int snd_soc_dapm_enable_pin(struct snd_soc_dapm_context *dapm, const char *pin)
{
	int ret;

	mutex_lock_nested(&dapm->card->dapm_mutex, SND_SOC_DAPM_CLASS_RUNTIME);

	ret = snd_soc_dapm_set_pin(dapm, pin, 1);

	mutex_unlock(&dapm->card->dapm_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_enable_pin);

/**
 * snd_soc_dapm_disable_pin - disable pin.
 * @codec: SoC codec
 * @pin: pin name
 *
 * Disables input/output pin and it's parents or children widgets.
 * NOTE: snd_soc_dapm_sync() needs to be called after this for DAPM to
 * do any widget power switching.
 */
int snd_soc_dapm_disable_pin(struct snd_soc_codec *codec, char *pin)
{
	return snd_soc_dapm_set_pin(codec, pin, 0);
 * snd_soc_dapm_force_enable_pin_unlocked - force a pin to be enabled
 * @dapm: DAPM context
 * @pin: pin name
 *
 * Enables input/output pin regardless of any other state.  This is
 * intended for use with microphone bias supplies used in microphone
 * jack detection.
 *
 * Requires external locking.
 *
 * NOTE: snd_soc_dapm_sync() needs to be called after this for DAPM to
 * do any widget power switching.
 */
int snd_soc_dapm_force_enable_pin_unlocked(struct snd_soc_dapm_context *dapm,
					 const char *pin)
{
	struct snd_soc_dapm_widget *w = dapm_find_widget(dapm, pin, true);

	if (!w) {
		dev_err(dapm->dev, "ASoC: unknown pin %s\n", pin);
		return -EINVAL;
	}

	dev_dbg(w->dapm->dev, "ASoC: force enable pin %s\n", pin);
	if (!w->connected) {
		/*
		 * w->force does not affect the number of input or output paths,
		 * so we only have to recheck if w->connected is changed
		 */
		dapm_widget_invalidate_input_paths(w);
		dapm_widget_invalidate_output_paths(w);
		w->connected = 1;
	}
	w->force = 1;
	dapm_mark_dirty(w, "force enable");

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_force_enable_pin_unlocked);

/**
 * snd_soc_dapm_force_enable_pin - force a pin to be enabled
 * @dapm: DAPM context
 * @pin: pin name
 *
 * Enables input/output pin regardless of any other state.  This is
 * intended for use with microphone bias supplies used in microphone
 * jack detection.
 *
 * NOTE: snd_soc_dapm_sync() needs to be called after this for DAPM to
 * do any widget power switching.
 */
int snd_soc_dapm_force_enable_pin(struct snd_soc_dapm_context *dapm,
				  const char *pin)
{
	int ret;

	mutex_lock_nested(&dapm->card->dapm_mutex, SND_SOC_DAPM_CLASS_RUNTIME);

	ret = snd_soc_dapm_force_enable_pin_unlocked(dapm, pin);

	mutex_unlock(&dapm->card->dapm_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_force_enable_pin);

/**
 * snd_soc_dapm_disable_pin_unlocked - disable pin.
 * @dapm: DAPM context
 * @pin: pin name
 *
 * Disables input/output pin and its parents or children widgets.
 *
 * Requires external locking.
 *
 * NOTE: snd_soc_dapm_sync() needs to be called after this for DAPM to
 * do any widget power switching.
 */
int snd_soc_dapm_disable_pin_unlocked(struct snd_soc_dapm_context *dapm,
				    const char *pin)
{
	return snd_soc_dapm_set_pin(dapm, pin, 0);
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_disable_pin_unlocked);

/**
 * snd_soc_dapm_disable_pin - disable pin.
 * @dapm: DAPM context
 * @pin: pin name
 *
 * Disables input/output pin and its parents or children widgets.
 *
 * NOTE: snd_soc_dapm_sync() needs to be called after this for DAPM to
 * do any widget power switching.
 */
int snd_soc_dapm_disable_pin(struct snd_soc_dapm_context *dapm,
			     const char *pin)
{
	int ret;

	mutex_lock_nested(&dapm->card->dapm_mutex, SND_SOC_DAPM_CLASS_RUNTIME);

	ret = snd_soc_dapm_set_pin(dapm, pin, 0);

	mutex_unlock(&dapm->card->dapm_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_disable_pin);

/**
 * snd_soc_dapm_get_pin_status - get audio pin status
 * @codec: audio codec
 * snd_soc_dapm_nc_pin_unlocked - permanently disable pin.
 * @dapm: DAPM context
 * @pin: pin name
 *
 * Marks the specified pin as being not connected, disabling it along
 * any parent or child widgets.  At present this is identical to
 * snd_soc_dapm_disable_pin() but in future it will be extended to do
 * additional things such as disabling controls which only affect
 * paths through the pin.
 *
 * Requires external locking.
 *
 * NOTE: snd_soc_dapm_sync() needs to be called after this for DAPM to
 * do any widget power switching.
 */
int snd_soc_dapm_nc_pin_unlocked(struct snd_soc_dapm_context *dapm,
			       const char *pin)
{
	return snd_soc_dapm_set_pin(dapm, pin, 0);
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_nc_pin_unlocked);

/**
 * snd_soc_dapm_nc_pin - permanently disable pin.
 * @dapm: DAPM context
 * @pin: pin name
 *
 * Marks the specified pin as being not connected, disabling it along
 * any parent or child widgets.  At present this is identical to
 * snd_soc_dapm_disable_pin() but in future it will be extended to do
 * additional things such as disabling controls which only affect
 * paths through the pin.
 *
 * NOTE: snd_soc_dapm_sync() needs to be called after this for DAPM to
 * do any widget power switching.
 */
int snd_soc_dapm_nc_pin(struct snd_soc_dapm_context *dapm, const char *pin)
{
	int ret;

	mutex_lock_nested(&dapm->card->dapm_mutex, SND_SOC_DAPM_CLASS_RUNTIME);

	ret = snd_soc_dapm_set_pin(dapm, pin, 0);

	mutex_unlock(&dapm->card->dapm_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_nc_pin);

/**
 * snd_soc_dapm_get_pin_status - get audio pin status
 * @dapm: DAPM context
 * @pin: audio signal pin endpoint (or start point)
 *
 * Get audio pin status - connected or disconnected.
 *
 * Returns 1 for connected otherwise 0.
 */
int snd_soc_dapm_get_pin_status(struct snd_soc_codec *codec, char *pin)
{
	struct snd_soc_dapm_widget *w;

	list_for_each_entry(w, &codec->dapm_widgets, list) {
		if (!strcmp(w->name, pin))
			return w->connected;
	}
int snd_soc_dapm_get_pin_status(struct snd_soc_dapm_context *dapm,
				const char *pin)
{
	struct snd_soc_dapm_widget *w = dapm_find_widget(dapm, pin, true);

	if (w)
		return w->connected;

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_get_pin_status);

/**
 * snd_soc_dapm_free - free dapm resources
 * @socdev: SoC device
 *
 * Free all dapm widgets and resources.
 */
void snd_soc_dapm_free(struct snd_soc_device *socdev)
{
	struct snd_soc_codec *codec = socdev->codec;

	snd_soc_dapm_sys_remove(socdev->dev);
	dapm_free_widgets(codec);
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_free);

/* Module information */
MODULE_AUTHOR("Liam Girdwood, liam.girdwood@wolfsonmicro.com, www.wolfsonmicro.com");
 * snd_soc_dapm_ignore_suspend - ignore suspend status for DAPM endpoint
 * @dapm: DAPM context
 * @pin: audio signal pin endpoint (or start point)
 *
 * Mark the given endpoint or pin as ignoring suspend.  When the
 * system is disabled a path between two endpoints flagged as ignoring
 * suspend will not be disabled.  The path must already be enabled via
 * normal means at suspend time, it will not be turned on if it was not
 * already enabled.
 */
int snd_soc_dapm_ignore_suspend(struct snd_soc_dapm_context *dapm,
				const char *pin)
{
	struct snd_soc_dapm_widget *w = dapm_find_widget(dapm, pin, false);

	if (!w) {
		dev_err(dapm->dev, "ASoC: unknown pin %s\n", pin);
		return -EINVAL;
	}

	w->ignore_suspend = 1;

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_ignore_suspend);

/**
 * snd_soc_dapm_free - free dapm resources
 * @dapm: DAPM context
 *
 * Free all dapm widgets and resources.
 */
void snd_soc_dapm_free(struct snd_soc_dapm_context *dapm)
{
	dapm_debugfs_cleanup(dapm);
	dapm_free_widgets(dapm);
	list_del(&dapm->list);
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_free);

static void soc_dapm_shutdown_dapm(struct snd_soc_dapm_context *dapm)
{
	struct snd_soc_card *card = dapm->card;
	struct snd_soc_dapm_widget *w;
	LIST_HEAD(down_list);
	int powerdown = 0;

	mutex_lock(&card->dapm_mutex);

	list_for_each_entry(w, &dapm->card->widgets, list) {
		if (w->dapm != dapm)
			continue;
		if (w->power) {
			dapm_seq_insert(w, &down_list, false);
			w->new_power = 0;
			powerdown = 1;
		}
	}

	/* If there were no widgets to power down we're already in
	 * standby.
	 */
	if (powerdown) {
		if (dapm->bias_level == SND_SOC_BIAS_ON)
			snd_soc_dapm_set_bias_level(dapm,
						    SND_SOC_BIAS_PREPARE);
		dapm_seq_run(card, &down_list, 0, false);
		if (dapm->bias_level == SND_SOC_BIAS_PREPARE)
			snd_soc_dapm_set_bias_level(dapm,
						    SND_SOC_BIAS_STANDBY);
	}

	mutex_unlock(&card->dapm_mutex);
}

/*
 * snd_soc_dapm_shutdown - callback for system shutdown
 */
void snd_soc_dapm_shutdown(struct snd_soc_card *card)
{
	struct snd_soc_dapm_context *dapm;

	list_for_each_entry(dapm, &card->dapm_list, list) {
		if (dapm != &card->dapm) {
			soc_dapm_shutdown_dapm(dapm);
			if (dapm->bias_level == SND_SOC_BIAS_STANDBY)
				snd_soc_dapm_set_bias_level(dapm,
							    SND_SOC_BIAS_OFF);
		}
	}

	soc_dapm_shutdown_dapm(&card->dapm);
	if (card->dapm.bias_level == SND_SOC_BIAS_STANDBY)
		snd_soc_dapm_set_bias_level(&card->dapm,
					    SND_SOC_BIAS_OFF);
}

/* Module information */
MODULE_AUTHOR("Liam Girdwood, lrg@slimlogic.co.uk");
MODULE_DESCRIPTION("Dynamic Audio Power Management core for ALSA SoC");
MODULE_LICENSE("GPL");
