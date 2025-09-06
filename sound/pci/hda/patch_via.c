/*
 * Universal Interface for Intel High Definition Audio Codec
 *
 * HD audio interface patch for VIA VT1708 codec
 *
 * Copyright (c) 2006 Lydia Wang <lydiawang@viatech.com>
 *                    Takashi Iwai <tiwai@suse.de>
 * HD audio interface patch for VIA VT17xx/VT18xx/VT20xx codec
 *
 *  (C) 2006-2009 VIA Technology, Inc.
 *  (C) 2006-2008 Takashi Iwai <tiwai@suse.de>
 *
 *  This driver is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This driver is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

/* * * * * * * * * * * * * * Release History * * * * * * * * * * * * * * * * */
/*                                                                           */
/* 2006-03-03  Lydia Wang  Create the basic patch to support VT1708 codec    */
/* 2006-03-14  Lydia Wang  Modify hard code for some pin widget nid          */
/* 2006-08-02  Lydia Wang  Add support to VT1709 codec                       */
/* 2006-09-08  Lydia Wang  Fix internal loopback recording source select bug */
/* 2007-09-12  Lydia Wang  Add EAPD enable during driver initialization      */
/* 2007-09-17  Lydia Wang  Add VT1708B codec support                        */
/*                                                                           */
/*									     */
/* 2006-03-03  Lydia Wang  Create the basic patch to support VT1708 codec    */
/* 2006-03-14  Lydia Wang  Modify hard code for some pin widget nid	     */
/* 2006-08-02  Lydia Wang  Add support to VT1709 codec			     */
/* 2006-09-08  Lydia Wang  Fix internal loopback recording source select bug */
/* 2007-09-12  Lydia Wang  Add EAPD enable during driver initialization	     */
/* 2007-09-17  Lydia Wang  Add VT1708B codec support			    */
/* 2007-11-14  Lydia Wang  Add VT1708A codec HP and CD pin connect config    */
/* 2008-02-03  Lydia Wang  Fix Rear channels and Back channels inverse issue */
/* 2008-03-06  Lydia Wang  Add VT1702 codec and VT1708S codec support	     */
/* 2008-04-09  Lydia Wang  Add mute front speaker when HP plugin	     */
/* 2008-04-09  Lydia Wang  Add Independent HP feature			     */
/* 2008-05-28  Lydia Wang  Add second S/PDIF Out support for VT1702	     */
/* 2008-09-15  Logan Li	   Add VT1708S Mic Boost workaround/backdoor	     */
/* 2009-02-16  Logan Li	   Add support for VT1718S			     */
/* 2009-03-13  Logan Li	   Add support for VT1716S			     */
/* 2009-04-14  Lydai Wang  Add support for VT1828S and VT2020		     */
/* 2009-07-08  Lydia Wang  Add support for VT2002P			     */
/* 2009-07-21  Lydia Wang  Add support for VT1812			     */
/* 2009-09-19  Lydia Wang  Add support for VT1818S			     */
/*									     */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */


#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <sound/core.h>
#include "hda_codec.h"
#include "hda_local.h"
#include "hda_patch.h"

/* amp values */
#define AMP_VAL_IDX_SHIFT	19
#define AMP_VAL_IDX_MASK	(0x0f<<19)

#define NUM_CONTROL_ALLOC	32
#define NUM_VERB_ALLOC		32

/* Pin Widget NID */
#define VT1708_HP_NID		0x13
#define VT1708_DIGOUT_NID	0x14
#define VT1708_DIGIN_NID	0x16
#define VT1708_DIGIN_PIN	0x26

#define VT1709_HP_DAC_NID	0x28
#define VT1709_DIGOUT_NID	0x13
#define VT1709_DIGIN_NID	0x17
#define VT1709_DIGIN_PIN	0x25

#define VT1708B_HP_NID		0x25
#define VT1708B_DIGOUT_NID	0x12
#define VT1708B_DIGIN_NID	0x15
#define VT1708B_DIGIN_PIN	0x21

#define IS_VT1708_VENDORID(x)		((x) >= 0x11061708 && (x) <= 0x1106170b)
#define IS_VT1709_10CH_VENDORID(x)	((x) >= 0x1106e710 && (x) <= 0x1106e713)
#define IS_VT1709_6CH_VENDORID(x)	((x) >= 0x1106e714 && (x) <= 0x1106e717)
#define IS_VT1708B_8CH_VENDORID(x)	((x) >= 0x1106e720 && (x) <= 0x1106e723)
#define IS_VT1708B_4CH_VENDORID(x)	((x) >= 0x1106e724 && (x) <= 0x1106e727)


enum {
	VIA_CTL_WIDGET_VOL,
	VIA_CTL_WIDGET_MUTE,
};

enum {
	AUTO_SEQ_FRONT,
	AUTO_SEQ_SURROUND,
	AUTO_SEQ_CENLFE,
	AUTO_SEQ_SIDE
};

static struct snd_kcontrol_new vt1708_control_templates[] = {
	HDA_CODEC_VOLUME(NULL, 0, 0, 0),
	HDA_CODEC_MUTE(NULL, 0, 0, 0),
};


struct via_spec {
	/* codec parameterization */
	struct snd_kcontrol_new *mixers[3];
	unsigned int num_mixers;

	struct hda_verb *init_verbs;

	char *stream_name_analog;
	struct hda_pcm_stream *stream_analog_playback;
	struct hda_pcm_stream *stream_analog_capture;

	char *stream_name_digital;
	struct hda_pcm_stream *stream_digital_playback;
	struct hda_pcm_stream *stream_digital_capture;

	/* playback */
	struct hda_multi_out multiout;

	/* capture */
	unsigned int num_adc_nids;
	hda_nid_t *adc_nids;
	hda_nid_t dig_in_nid;

	/* capture source */
	const struct hda_input_mux *input_mux;
	unsigned int cur_mux[3];

	/* PCM information */
	struct hda_pcm pcm_rec[2];

	/* dynamic controls, init_verbs and input_mux */
	struct auto_pin_cfg autocfg;
	unsigned int num_kctl_alloc, num_kctl_used;
	struct snd_kcontrol_new *kctl_alloc;
	struct hda_input_mux private_imux;
	hda_nid_t private_dac_nids[AUTO_CFG_MAX_OUTS];

#ifdef CONFIG_SND_HDA_POWER_SAVE
	struct hda_loopback_check loopback;
#endif
};

static hda_nid_t vt1708_adc_nids[2] = {
	/* ADC1-2 */
	0x15, 0x27
};

static hda_nid_t vt1709_adc_nids[3] = {
	/* ADC1-2 */
	0x14, 0x15, 0x16
};

static hda_nid_t vt1708B_adc_nids[2] = {
	/* ADC1-2 */
	0x13, 0x14
};

/* add dynamic controls */
static int via_add_control(struct via_spec *spec, int type, const char *name,
			   unsigned long val)
{
	struct snd_kcontrol_new *knew;

	if (spec->num_kctl_used >= spec->num_kctl_alloc) {
		int num = spec->num_kctl_alloc + NUM_CONTROL_ALLOC;

		/* array + terminator */
		knew = kcalloc(num + 1, sizeof(*knew), GFP_KERNEL);
		if (!knew)
			return -ENOMEM;
		if (spec->kctl_alloc) {
			memcpy(knew, spec->kctl_alloc,
			       sizeof(*knew) * spec->num_kctl_alloc);
			kfree(spec->kctl_alloc);
		}
		spec->kctl_alloc = knew;
		spec->num_kctl_alloc = num;
	}

	knew = &spec->kctl_alloc[spec->num_kctl_used];
	*knew = vt1708_control_templates[type];
	knew->name = kstrdup(name, GFP_KERNEL);

	if (!knew->name)
		return -ENOMEM;
	knew->private_value = val;
	spec->num_kctl_used++;
	return 0;
}

/* create input playback/capture controls for the given pin */
static int via_new_analog_input(struct via_spec *spec, hda_nid_t pin,
				const char *ctlname, int idx, int mix_nid)
{
	char name[32];
	int err;

	sprintf(name, "%s Playback Volume", ctlname);
	err = via_add_control(spec, VIA_CTL_WIDGET_VOL, name,
			      HDA_COMPOSE_AMP_VAL(mix_nid, 3, idx, HDA_INPUT));
	if (err < 0)
		return err;
	sprintf(name, "%s Playback Switch", ctlname);
	err = via_add_control(spec, VIA_CTL_WIDGET_MUTE, name,
			      HDA_COMPOSE_AMP_VAL(mix_nid, 3, idx, HDA_INPUT));
	if (err < 0)
		return err;
	return 0;
}

static void via_auto_set_output_and_unmute(struct hda_codec *codec,
					   hda_nid_t nid, int pin_type,
					   int dac_idx)
{
	/* set as output */
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_PIN_WIDGET_CONTROL,
			    pin_type);
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_AMP_GAIN_MUTE,
			    AMP_OUT_UNMUTE);
}


static void via_auto_init_multi_out(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int i;

	for (i = 0; i <= AUTO_SEQ_SIDE; i++) {
		hda_nid_t nid = spec->autocfg.line_out_pins[i];
		if (nid)
			via_auto_set_output_and_unmute(codec, nid, PIN_OUT, i);
	}
}

static void via_auto_init_hp_out(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	hda_nid_t pin;

	pin = spec->autocfg.hp_pins[0];
	if (pin) /* connect to front */
		via_auto_set_output_and_unmute(codec, pin, PIN_HP, 0);
}

static void via_auto_init_analog_input(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int i;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		hda_nid_t nid = spec->autocfg.input_pins[i];

		snd_hda_codec_write(codec, nid, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL,
				    (i <= AUTO_PIN_FRONT_MIC ?
				     PIN_VREF50 : PIN_IN));

	}
}
/*
 * input MUX handling
 */
static int via_mux_enum_info(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct via_spec *spec = codec->spec;
	return snd_hda_input_mux_info(spec->input_mux, uinfo);
}

static int via_mux_enum_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct via_spec *spec = codec->spec;
	unsigned int adc_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);

	ucontrol->value.enumerated.item[0] = spec->cur_mux[adc_idx];
	return 0;
}

static int via_mux_enum_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct via_spec *spec = codec->spec;
	unsigned int adc_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	unsigned int vendor_id = codec->vendor_id;

	/* AIW0  lydia 060801 add for correct sw0 input select */
	if (IS_VT1708_VENDORID(vendor_id) && (adc_idx == 0))
		return snd_hda_input_mux_put(codec, spec->input_mux, ucontrol,
					     0x18, &spec->cur_mux[adc_idx]);
	else if ((IS_VT1709_10CH_VENDORID(vendor_id) ||
		  IS_VT1709_6CH_VENDORID(vendor_id)) && adc_idx == 0)
		return snd_hda_input_mux_put(codec, spec->input_mux, ucontrol,
					     0x19, &spec->cur_mux[adc_idx]);
	else if ((IS_VT1708B_8CH_VENDORID(vendor_id) ||
		  IS_VT1708B_4CH_VENDORID(vendor_id)) && adc_idx == 0)
		return snd_hda_input_mux_put(codec, spec->input_mux, ucontrol,
					     0x17, &spec->cur_mux[adc_idx]);
	else
		return snd_hda_input_mux_put(codec, spec->input_mux, ucontrol,
					     spec->adc_nids[adc_idx],
					     &spec->cur_mux[adc_idx]);
}

/* capture mixer elements */
static struct snd_kcontrol_new vt1708_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x27, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x27, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 1,
		.info = via_mux_enum_info,
		.get = via_mux_enum_get,
		.put = via_mux_enum_put,
	},
	{ } /* end */
};
/*
 * generic initialization of ADC, input mixers and output mixers
 */
static struct hda_verb vt1708_volume_init_verbs[] = {
	/*
	 * Unmute ADC0-1 and set the default input to mic-in
	 */
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x27, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},


	/* Unmute input amps (CD, Line In, Mic 1 & Mic 2) of the analog-loopback
	 * mixer widget
	 */
	/* Amp Indices: CD = 1, Mic1 = 2, Line = 3, Mic2 = 4 */
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(3)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(4)},

	/*
	 * Set up output mixers (0x19 - 0x1b)
	 */
	/* set vol=0 to output mixers */
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	
	/* Setup default input to PW4 */
	{0x20, AC_VERB_SET_CONNECT_SEL, 0x1},
	/* PW9 Output enable */
	{0x25, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	{ }
};

static int via_playback_pcm_open(struct hda_pcm_stream *hinfo,
				 struct hda_codec *codec,
				 struct snd_pcm_substream *substream)
{
	struct via_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_open(codec, &spec->multiout, substream,
					     hinfo);
}

static int via_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
				    struct hda_codec *codec,
				    unsigned int stream_tag,
				    unsigned int format,
				    struct snd_pcm_substream *substream)
{
	struct via_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_prepare(codec, &spec->multiout,
						stream_tag, format, substream);
}

static int via_playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
				    struct hda_codec *codec,
				    struct snd_pcm_substream *substream)
{
	struct via_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_cleanup(codec, &spec->multiout);
}

/*
 * Digital out
 */
static int via_dig_playback_pcm_open(struct hda_pcm_stream *hinfo,
				     struct hda_codec *codec,
				     struct snd_pcm_substream *substream)
{
	struct via_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_open(codec, &spec->multiout);
}

static int via_dig_playback_pcm_close(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      struct snd_pcm_substream *substream)
{
	struct via_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_close(codec, &spec->multiout);
}

static int via_dig_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
					struct hda_codec *codec,
					unsigned int stream_tag,
					unsigned int format,
					struct snd_pcm_substream *substream)
{
	struct via_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_prepare(codec, &spec->multiout,
					     stream_tag, format, substream);
}

/*
 * Analog capture
 */
static int via_capture_pcm_prepare(struct hda_pcm_stream *hinfo,
				   struct hda_codec *codec,
				   unsigned int stream_tag,
				   unsigned int format,
				   struct snd_pcm_substream *substream)
{
	struct via_spec *spec = codec->spec;

	snd_hda_codec_setup_stream(codec, spec->adc_nids[substream->number],
				   stream_tag, 0, format);
	return 0;
}

static int via_capture_pcm_cleanup(struct hda_pcm_stream *hinfo,
				   struct hda_codec *codec,
				   struct snd_pcm_substream *substream)
{
	struct via_spec *spec = codec->spec;
	snd_hda_codec_cleanup_stream(codec, spec->adc_nids[substream->number]);
	return 0;
}

static struct hda_pcm_stream vt1708_pcm_analog_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 8,
	.nid = 0x10, /* NID to query formats and rates */
	.ops = {
		.open = via_playback_pcm_open,
		.prepare = via_playback_pcm_prepare,
		.cleanup = via_playback_pcm_cleanup
	},
};

static struct hda_pcm_stream vt1708_pcm_analog_s16_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 8,
	.nid = 0x10, /* NID to query formats and rates */
	/* We got noisy outputs on the right channel on VT1708 when
	 * 24bit samples are used.  Until any workaround is found,
	 * disable the 24bit format, so far.
	 */
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.ops = {
		.open = via_playback_pcm_open,
		.prepare = via_playback_pcm_prepare,
		.cleanup = via_playback_pcm_cleanup
	},
};

static struct hda_pcm_stream vt1708_pcm_analog_capture = {
	.substreams = 2,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0x15, /* NID to query formats and rates */
	.ops = {
		.prepare = via_capture_pcm_prepare,
		.cleanup = via_capture_pcm_cleanup
	},
};

static struct hda_pcm_stream vt1708_pcm_digital_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in via_build_pcms */
	.ops = {
		.open = via_dig_playback_pcm_open,
		.close = via_dig_playback_pcm_close,
		.prepare = via_dig_playback_pcm_prepare
	},
};

static struct hda_pcm_stream vt1708_pcm_digital_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
};
#include <linux/module.h>
#include <sound/core.h>
#include <sound/asoundef.h>
#include "hda_codec.h"
#include "hda_local.h"
#include "hda_auto_parser.h"
#include "hda_jack.h"
#include "hda_generic.h"

/* Pin Widget NID */
#define VT1708_HP_PIN_NID	0x20
#define VT1708_CD_PIN_NID	0x24

enum VIA_HDA_CODEC {
	UNKNOWN = -1,
	VT1708,
	VT1709_10CH,
	VT1709_6CH,
	VT1708B_8CH,
	VT1708B_4CH,
	VT1708S,
	VT1708BCE,
	VT1702,
	VT1718S,
	VT1716S,
	VT2002P,
	VT1812,
	VT1802,
	VT1705CF,
	VT1808,
	CODEC_TYPES,
};

#define VT2002P_COMPATIBLE(spec) \
	((spec)->codec_type == VT2002P ||\
	 (spec)->codec_type == VT1812 ||\
	 (spec)->codec_type == VT1802)

struct via_spec {
	struct hda_gen_spec gen;

	/* codec parameterization */
	const struct snd_kcontrol_new *mixers[6];
	unsigned int num_mixers;

	const struct hda_verb *init_verbs[5];
	unsigned int num_iverbs;

	/* HP mode source */
	unsigned int dmic_enabled;
	enum VIA_HDA_CODEC codec_type;

	/* analog low-power control */
	bool alc_mode;

	/* work to check hp jack state */
	int hp_work_active;
	int vt1708_jack_detect;

	unsigned int beep_amp;
};

static enum VIA_HDA_CODEC get_codec_type(struct hda_codec *codec);
static void via_playback_pcm_hook(struct hda_pcm_stream *hinfo,
				  struct hda_codec *codec,
				  struct snd_pcm_substream *substream,
				  int action);

static const struct hda_codec_ops via_patch_ops; /* defined below */

static struct via_spec *via_new_spec(struct hda_codec *codec)
{
	struct via_spec *spec;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return NULL;

	codec->spec = spec;
	snd_hda_gen_spec_init(&spec->gen);
	spec->codec_type = get_codec_type(codec);
	/* VT1708BCE & VT1708S are almost same */
	if (spec->codec_type == VT1708BCE)
		spec->codec_type = VT1708S;
	spec->gen.indep_hp = 1;
	spec->gen.keep_eapd_on = 1;
	spec->gen.dac_min_mute = 1;
	spec->gen.pcm_playback_hook = via_playback_pcm_hook;
	spec->gen.add_stereo_mix_input = HDA_HINT_STEREO_MIX_AUTO;
	codec->power_save_node = 1;
	spec->gen.power_down_unused = 1;
	codec->patch_ops = via_patch_ops;
	return spec;
}

static enum VIA_HDA_CODEC get_codec_type(struct hda_codec *codec)
{
	u32 vendor_id = codec->core.vendor_id;
	u16 ven_id = vendor_id >> 16;
	u16 dev_id = vendor_id & 0xffff;
	enum VIA_HDA_CODEC codec_type;

	/* get codec type */
	if (ven_id != 0x1106)
		codec_type = UNKNOWN;
	else if (dev_id >= 0x1708 && dev_id <= 0x170b)
		codec_type = VT1708;
	else if (dev_id >= 0xe710 && dev_id <= 0xe713)
		codec_type = VT1709_10CH;
	else if (dev_id >= 0xe714 && dev_id <= 0xe717)
		codec_type = VT1709_6CH;
	else if (dev_id >= 0xe720 && dev_id <= 0xe723) {
		codec_type = VT1708B_8CH;
		if (snd_hda_param_read(codec, 0x16, AC_PAR_CONNLIST_LEN) == 0x7)
			codec_type = VT1708BCE;
	} else if (dev_id >= 0xe724 && dev_id <= 0xe727)
		codec_type = VT1708B_4CH;
	else if ((dev_id & 0xfff) == 0x397
		 && (dev_id >> 12) < 8)
		codec_type = VT1708S;
	else if ((dev_id & 0xfff) == 0x398
		 && (dev_id >> 12) < 8)
		codec_type = VT1702;
	else if ((dev_id & 0xfff) == 0x428
		 && (dev_id >> 12) < 8)
		codec_type = VT1718S;
	else if (dev_id == 0x0433 || dev_id == 0xa721)
		codec_type = VT1716S;
	else if (dev_id == 0x0441 || dev_id == 0x4441)
		codec_type = VT1718S;
	else if (dev_id == 0x0438 || dev_id == 0x4438)
		codec_type = VT2002P;
	else if (dev_id == 0x0448)
		codec_type = VT1812;
	else if (dev_id == 0x0440)
		codec_type = VT1708S;
	else if ((dev_id & 0xfff) == 0x446)
		codec_type = VT1802;
	else if (dev_id == 0x4760)
		codec_type = VT1705CF;
	else if (dev_id == 0x4761 || dev_id == 0x4762)
		codec_type = VT1808;
	else
		codec_type = UNKNOWN;
	return codec_type;
};

static void analog_low_current_mode(struct hda_codec *codec);
static bool is_aa_path_mute(struct hda_codec *codec);

#define hp_detect_with_aa(codec) \
	(snd_hda_get_bool_hint(codec, "analog_loopback_hp_detect") == 1 && \
	 !is_aa_path_mute(codec))

static void vt1708_stop_hp_work(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	if (spec->codec_type != VT1708 || !spec->gen.autocfg.hp_outs)
		return;
	if (spec->hp_work_active) {
		snd_hda_codec_write(codec, 0x1, 0, 0xf81, 1);
		codec->jackpoll_interval = 0;
		cancel_delayed_work_sync(&codec->jackpoll_work);
		spec->hp_work_active = false;
	}
}

static void vt1708_update_hp_work(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	if (spec->codec_type != VT1708 || !spec->gen.autocfg.hp_outs)
		return;
	if (spec->vt1708_jack_detect) {
		if (!spec->hp_work_active) {
			codec->jackpoll_interval = msecs_to_jiffies(100);
			snd_hda_codec_write(codec, 0x1, 0, 0xf81, 0);
			schedule_delayed_work(&codec->jackpoll_work, 0);
			spec->hp_work_active = true;
		}
	} else if (!hp_detect_with_aa(codec))
		vt1708_stop_hp_work(codec);
}

static int via_pin_power_ctl_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	return snd_hda_enum_bool_helper_info(kcontrol, uinfo);
}

static int via_pin_power_ctl_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct via_spec *spec = codec->spec;

	ucontrol->value.enumerated.item[0] = spec->gen.power_down_unused;
	return 0;
}

static int via_pin_power_ctl_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct via_spec *spec = codec->spec;
	bool val = !!ucontrol->value.enumerated.item[0];

	if (val == spec->gen.power_down_unused)
		return 0;
	/* codec->power_save_node = val; */ /* widget PM seems yet broken */
	spec->gen.power_down_unused = val;
	analog_low_current_mode(codec);
	return 1;
}

static const struct snd_kcontrol_new via_pin_power_ctl_enum[] = {
	{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Dynamic Power-Control",
	.info = via_pin_power_ctl_info,
	.get = via_pin_power_ctl_get,
	.put = via_pin_power_ctl_put,
	},
	{} /* terminator */
};

#ifdef CONFIG_SND_HDA_INPUT_BEEP
static inline void set_beep_amp(struct via_spec *spec, hda_nid_t nid,
				int idx, int dir)
{
	spec->gen.beep_nid = nid;
	spec->beep_amp = HDA_COMPOSE_AMP_VAL(nid, 1, idx, dir);
}

/* additional beep mixers; the actual parameters are overwritten at build */
static const struct snd_kcontrol_new cxt_beep_mixer[] = {
	HDA_CODEC_VOLUME_MONO("Beep Playback Volume", 0, 1, 0, HDA_OUTPUT),
	HDA_CODEC_MUTE_BEEP_MONO("Beep Playback Switch", 0, 1, 0, HDA_OUTPUT),
	{ } /* end */
};

/* create beep controls if needed */
static int add_beep_ctls(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int err;

	if (spec->beep_amp) {
		const struct snd_kcontrol_new *knew;
		for (knew = cxt_beep_mixer; knew->name; knew++) {
			struct snd_kcontrol *kctl;
			kctl = snd_ctl_new1(knew, codec);
			if (!kctl)
				return -ENOMEM;
			kctl->private_value = spec->beep_amp;
			err = snd_hda_ctl_add(codec, 0, kctl);
			if (err < 0)
				return err;
		}
	}
	return 0;
}

static void auto_parse_beep(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	hda_nid_t nid;

	for_each_hda_codec_node(nid, codec)
		if (get_wcaps_type(get_wcaps(codec, nid)) == AC_WID_BEEP) {
			set_beep_amp(spec, nid, 0, HDA_OUTPUT);
			break;
		}
}
#else
#define set_beep_amp(spec, nid, idx, dir) /* NOP */
#define add_beep_ctls(codec)	0
#define auto_parse_beep(codec)
#endif

/* check AA path's mute status */
static bool is_aa_path_mute(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	const struct hda_amp_list *p;
	int ch, v;

	p = spec->gen.loopback.amplist;
	if (!p)
		return true;
	for (; p->nid; p++) {
		for (ch = 0; ch < 2; ch++) {
			v = snd_hda_codec_amp_read(codec, p->nid, ch, p->dir,
						   p->idx);
			if (!(v & HDA_AMP_MUTE) && v > 0)
				return false;
		}
	}
	return true;
}

/* enter/exit analog low-current mode */
static void __analog_low_current_mode(struct hda_codec *codec, bool force)
{
	struct via_spec *spec = codec->spec;
	bool enable;
	unsigned int verb, parm;

	if (!codec->power_save_node)
		enable = false;
	else
		enable = is_aa_path_mute(codec) && !spec->gen.active_streams;
	if (enable == spec->alc_mode && !force)
		return;
	spec->alc_mode = enable;

	/* decide low current mode's verb & parameter */
	switch (spec->codec_type) {
	case VT1708B_8CH:
	case VT1708B_4CH:
		verb = 0xf70;
		parm = enable ? 0x02 : 0x00; /* 0x02: 2/3x, 0x00: 1x */
		break;
	case VT1708S:
	case VT1718S:
	case VT1716S:
		verb = 0xf73;
		parm = enable ? 0x51 : 0xe1; /* 0x51: 4/28x, 0xe1: 1x */
		break;
	case VT1702:
		verb = 0xf73;
		parm = enable ? 0x01 : 0x1d; /* 0x01: 4/40x, 0x1d: 1x */
		break;
	case VT2002P:
	case VT1812:
	case VT1802:
		verb = 0xf93;
		parm = enable ? 0x00 : 0xe0; /* 0x00: 4/40x, 0xe0: 1x */
		break;
	case VT1705CF:
	case VT1808:
		verb = 0xf82;
		parm = enable ? 0x00 : 0xe0;  /* 0x00: 4/40x, 0xe0: 1x */
		break;
	default:
		return;		/* other codecs are not supported */
	}
	/* send verb */
	snd_hda_codec_write(codec, codec->core.afg, 0, verb, parm);
}

static void analog_low_current_mode(struct hda_codec *codec)
{
	return __analog_low_current_mode(codec, false);
}

static int via_build_controls(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int err;
	int i;
	int err, i;

	err = snd_hda_gen_build_controls(codec);
	if (err < 0)
		return err;

	err = add_beep_ctls(codec);
	if (err < 0)
		return err;

	spec->mixers[spec->num_mixers++] = via_pin_power_ctl_enum;

	for (i = 0; i < spec->num_mixers; i++) {
		err = snd_hda_add_new_ctls(codec, spec->mixers[i]);
		if (err < 0)
			return err;
	}

	if (spec->multiout.dig_out_nid) {
		err = snd_hda_create_spdif_out_ctls(codec,
						    spec->multiout.dig_out_nid);
		if (err < 0)
			return err;
		err = snd_hda_create_spdif_share_sw(codec,
						    &spec->multiout);
		if (err < 0)
			return err;
		spec->multiout.share_spdif = 1;
	}
	if (spec->dig_in_nid) {
		err = snd_hda_create_spdif_in_ctls(codec, spec->dig_in_nid);
		if (err < 0)
			return err;
	}
	return 0;
}

static int via_build_pcms(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	struct hda_pcm *info = spec->pcm_rec;

	codec->num_pcms = 1;
	codec->pcm_info = info;

	info->name = spec->stream_name_analog;
	info->stream[SNDRV_PCM_STREAM_PLAYBACK] = *(spec->stream_analog_playback);
	info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid = spec->multiout.dac_nids[0];
	info->stream[SNDRV_PCM_STREAM_CAPTURE] = *(spec->stream_analog_capture);
	info->stream[SNDRV_PCM_STREAM_CAPTURE].nid = spec->adc_nids[0];

	info->stream[SNDRV_PCM_STREAM_PLAYBACK].channels_max =
		spec->multiout.max_channels;

	if (spec->multiout.dig_out_nid || spec->dig_in_nid) {
		codec->num_pcms++;
		info++;
		info->name = spec->stream_name_digital;
		info->pcm_type = HDA_PCM_TYPE_SPDIF;
		if (spec->multiout.dig_out_nid) {
			info->stream[SNDRV_PCM_STREAM_PLAYBACK] =
				*(spec->stream_digital_playback);
			info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid =
				spec->multiout.dig_out_nid;
		}
		if (spec->dig_in_nid) {
			info->stream[SNDRV_PCM_STREAM_CAPTURE] =
				*(spec->stream_digital_capture);
			info->stream[SNDRV_PCM_STREAM_CAPTURE].nid =
				spec->dig_in_nid;
		}
	}

	return 0;
	return 0;
}

static void via_playback_pcm_hook(struct hda_pcm_stream *hinfo,
				  struct hda_codec *codec,
				  struct snd_pcm_substream *substream,
				  int action)
{
	analog_low_current_mode(codec);
	vt1708_update_hp_work(codec);
}

static void via_free(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	unsigned int i;

	if (!spec)
		return;

	if (spec->kctl_alloc) {
		for (i = 0; i < spec->num_kctl_used; i++)
			kfree(spec->kctl_alloc[i].name);
		kfree(spec->kctl_alloc);
	}

	kfree(codec->spec);
}

static int via_init(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	snd_hda_sequence_write(codec, spec->init_verbs);
	/* Lydia Add for EAPD enable */
	if (!spec->dig_in_nid) { /* No Digital In connection */
		if (IS_VT1708_VENDORID(codec->vendor_id)) {
			snd_hda_codec_write(codec, VT1708_DIGIN_PIN, 0,
					    AC_VERB_SET_PIN_WIDGET_CONTROL,
					    PIN_OUT);
			snd_hda_codec_write(codec, VT1708_DIGIN_PIN, 0,
					    AC_VERB_SET_EAPD_BTLENABLE, 0x02);
		} else if (IS_VT1709_10CH_VENDORID(codec->vendor_id) ||
			   IS_VT1709_6CH_VENDORID(codec->vendor_id)) {
			snd_hda_codec_write(codec, VT1709_DIGIN_PIN, 0,
					    AC_VERB_SET_PIN_WIDGET_CONTROL,
					    PIN_OUT);
			snd_hda_codec_write(codec, VT1709_DIGIN_PIN, 0,
					    AC_VERB_SET_EAPD_BTLENABLE, 0x02);
		} else if (IS_VT1708B_8CH_VENDORID(codec->vendor_id) ||
			   IS_VT1708B_4CH_VENDORID(codec->vendor_id)) {
			snd_hda_codec_write(codec, VT1708B_DIGIN_PIN, 0,
					    AC_VERB_SET_PIN_WIDGET_CONTROL,
					    PIN_OUT);
			snd_hda_codec_write(codec, VT1708B_DIGIN_PIN, 0,
					    AC_VERB_SET_EAPD_BTLENABLE, 0x02);
		}
	} else /* enable SPDIF-input pin */
		snd_hda_codec_write(codec, spec->autocfg.dig_in_pin, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN);

 	return 0;
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
static int via_check_power_status(struct hda_codec *codec, hda_nid_t nid)
{
	struct via_spec *spec = codec->spec;
	return snd_hda_check_amp_list_power(codec, &spec->loopback, nid);
	vt1708_stop_hp_work(codec);
	snd_hda_gen_free(codec);
}

#ifdef CONFIG_PM
static int via_suspend(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	vt1708_stop_hp_work(codec);

	/* Fix pop noise on headphones */
	if (spec->codec_type == VT1802)
		snd_hda_shutup_pins(codec);

	return 0;
}

static int via_resume(struct hda_codec *codec)
{
	/* some delay here to make jack detection working (bko#98921) */
	msleep(10);
	codec->patch_ops.init(codec);
	regcache_sync(codec->core.regmap);
	return 0;
}
#endif

#ifdef CONFIG_PM
static int via_check_power_status(struct hda_codec *codec, hda_nid_t nid)
{
	struct via_spec *spec = codec->spec;
	analog_low_current_mode(codec);
	vt1708_update_hp_work(codec);
	return snd_hda_check_amp_list_power(codec, &spec->gen.loopback, nid);
}
#endif

/*
 */
static struct hda_codec_ops via_patch_ops = {
	.build_controls = via_build_controls,
	.build_pcms = via_build_pcms,
	.init = via_init,
	.free = via_free,
#ifdef CONFIG_SND_HDA_POWER_SAVE

static int via_init(struct hda_codec *codec);

static const struct hda_codec_ops via_patch_ops = {
	.build_controls = via_build_controls,
	.build_pcms = snd_hda_gen_build_pcms,
	.init = via_init,
	.free = via_free,
	.unsol_event = snd_hda_jack_unsol_event,
#ifdef CONFIG_PM
	.suspend = via_suspend,
	.resume = via_resume,
	.check_power_status = via_check_power_status,
#endif
};

/* fill in the dac_nids table from the parsed pin configuration */
static int vt1708_auto_fill_dac_nids(struct via_spec *spec,
				     const struct auto_pin_cfg *cfg)
{
	int i;
	hda_nid_t nid;

	spec->multiout.num_dacs = cfg->line_outs;

	spec->multiout.dac_nids = spec->private_dac_nids;
 	
	for(i = 0; i < 4; i++) {
		nid = cfg->line_out_pins[i];
		if (nid) {
			/* config dac list */
			switch (i) {
			case AUTO_SEQ_FRONT:
				spec->multiout.dac_nids[i] = 0x10;
				break;
			case AUTO_SEQ_CENLFE:
				spec->multiout.dac_nids[i] = 0x12;
				break;
			case AUTO_SEQ_SURROUND:
				spec->multiout.dac_nids[i] = 0x13;
				break;
			case AUTO_SEQ_SIDE:
				spec->multiout.dac_nids[i] = 0x11;
				break;
			}
		}
	}

	return 0;
}

/* add playback controls from the parsed DAC table */
static int vt1708_auto_create_multi_out_ctls(struct via_spec *spec,
					     const struct auto_pin_cfg *cfg)
{
	char name[32];
	static const char *chname[4] = { "Front", "Surround", "C/LFE", "Side" };
	hda_nid_t nid, nid_vol = 0;
	int i, err;

	for (i = 0; i <= AUTO_SEQ_SIDE; i++) {
		nid = cfg->line_out_pins[i];

		if (!nid)
			continue;
		
		if (i != AUTO_SEQ_FRONT)
			nid_vol = 0x1b - i + 1;

		if (i == AUTO_SEQ_CENLFE) {
			/* Center/LFE */
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
					"Center Playback Volume",
					HDA_COMPOSE_AMP_VAL(nid_vol, 1, 0,
							    HDA_OUTPUT));
			if (err < 0)
				return err;
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
					      "LFE Playback Volume",
					      HDA_COMPOSE_AMP_VAL(nid_vol, 2, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE,
					      "Center Playback Switch",
					      HDA_COMPOSE_AMP_VAL(nid_vol, 1, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE,
					      "LFE Playback Switch",
					      HDA_COMPOSE_AMP_VAL(nid_vol, 2, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
		} else if (i == AUTO_SEQ_FRONT){
			/* add control to mixer index 0 */
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
					      "Master Front Playback Volume",
					      HDA_COMPOSE_AMP_VAL(0x17, 3, 0,
								  HDA_INPUT));
			if (err < 0)
				return err;
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE,
					      "Master Front Playback Switch",
					      HDA_COMPOSE_AMP_VAL(0x17, 3, 0,
								  HDA_INPUT));
			if (err < 0)
				return err;
			
			/* add control to PW3 */
			sprintf(name, "%s Playback Volume", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL, name,
					      HDA_COMPOSE_AMP_VAL(nid, 3, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
			sprintf(name, "%s Playback Switch", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE, name,
					      HDA_COMPOSE_AMP_VAL(nid, 3, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
		} else {
			sprintf(name, "%s Playback Volume", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL, name,
					      HDA_COMPOSE_AMP_VAL(nid_vol, 3, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
			sprintf(name, "%s Playback Switch", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE, name,
					      HDA_COMPOSE_AMP_VAL(nid_vol, 3, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
		}
	}

	return 0;
}

static int vt1708_auto_create_hp_ctls(struct via_spec *spec, hda_nid_t pin)
{
	int err;

	if (!pin)
		return 0;

	spec->multiout.hp_nid = VT1708_HP_NID; /* AOW3 */

	err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
			      "Headphone Playback Volume",
			      HDA_COMPOSE_AMP_VAL(pin, 3, 0, HDA_OUTPUT));
	if (err < 0)
		return err;
	err = via_add_control(spec, VIA_CTL_WIDGET_MUTE,
			      "Headphone Playback Switch",
			      HDA_COMPOSE_AMP_VAL(pin, 3, 0, HDA_OUTPUT));
	if (err < 0)
		return err;

	return 0;
}

/* create playback/capture controls for input pins */
static int vt1708_auto_create_analog_input_ctls(struct via_spec *spec,
						const struct auto_pin_cfg *cfg)
{
	static char *labels[] = {
		"Mic", "Front Mic", "Line", "Front Line", "CD", "Aux", NULL
	};
	struct hda_input_mux *imux = &spec->private_imux;
	int i, err, idx = 0;

	/* for internal loopback recording select */
	imux->items[imux->num_items].label = "Stereo Mixer";
	imux->items[imux->num_items].index = idx;
	imux->num_items++;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		if (!cfg->input_pins[i])
			continue;

		switch (cfg->input_pins[i]) {
		case 0x1d: /* Mic */
			idx = 2;
			break;
				
		case 0x1e: /* Line In */
			idx = 3;
			break;

		case 0x21: /* Front Mic */
			idx = 4;
			break;

		case 0x24: /* CD */
			idx = 1;
			break;
		}
		err = via_new_analog_input(spec, cfg->input_pins[i], labels[i],
					   idx, 0x17);
		if (err < 0)
			return err;
		imux->items[imux->num_items].label = labels[i];
		imux->items[imux->num_items].index = idx;
		imux->num_items++;
	}
	return 0;
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
static struct hda_amp_list vt1708_loopbacks[] = {
	{ 0x17, HDA_INPUT, 1 },
	{ 0x17, HDA_INPUT, 2 },
	{ 0x17, HDA_INPUT, 3 },
	{ 0x17, HDA_INPUT, 4 },
	{ } /* end */
};
#endif

static int vt1708_parse_auto_config(struct hda_codec *codec)

static const struct hda_verb vt1708_init_verbs[] = {
	/* power down jack detect function */
	{0x1, 0xf81, 0x1},
	{ }
};
static void vt1708_set_pinconfig_connect(struct hda_codec *codec, hda_nid_t nid)
{
	unsigned int def_conf;
	unsigned char seqassoc;

	def_conf = snd_hda_codec_get_pincfg(codec, nid);
	seqassoc = (unsigned char) get_defcfg_association(def_conf);
	seqassoc = (seqassoc << 4) | get_defcfg_sequence(def_conf);
	if (get_defcfg_connect(def_conf) == AC_JACK_PORT_NONE
	    && (seqassoc == 0xf0 || seqassoc == 0xff)) {
		def_conf = def_conf & (~(AC_JACK_PORT_BOTH << 30));
		snd_hda_codec_set_pincfg(codec, nid, def_conf);
	}

	return;
}

static int vt1708_jack_detect_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct via_spec *spec = codec->spec;

	if (spec->codec_type != VT1708)
		return 0;
	ucontrol->value.integer.value[0] = spec->vt1708_jack_detect;
	return 0;
}

static int vt1708_jack_detect_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct via_spec *spec = codec->spec;
	int val;

	if (spec->codec_type != VT1708)
		return 0;
	val = !!ucontrol->value.integer.value[0];
	if (spec->vt1708_jack_detect == val)
		return 0;
	spec->vt1708_jack_detect = val;
	vt1708_update_hp_work(codec);
	return 1;
}

static const struct snd_kcontrol_new vt1708_jack_detect_ctl[] = {
	{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Jack Detect",
	.count = 1,
	.info = snd_ctl_boolean_mono_info,
	.get = vt1708_jack_detect_get,
	.put = vt1708_jack_detect_put,
	},
	{} /* terminator */
};

static const struct badness_table via_main_out_badness = {
	.no_primary_dac = 0x10000,
	.no_dac = 0x4000,
	.shared_primary = 0x10000,
	.shared_surr = 0x20,
	.shared_clfe = 0x20,
	.shared_surr_main = 0x20,
};
static const struct badness_table via_extra_out_badness = {
	.no_primary_dac = 0x4000,
	.no_dac = 0x4000,
	.shared_primary = 0x12,
	.shared_surr = 0x20,
	.shared_clfe = 0x20,
	.shared_surr_main = 0x10,
};

static int via_parse_auto_config(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int err;

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg, NULL);
	if (err < 0)
		return err;
	err = vt1708_auto_fill_dac_nids(spec, &spec->autocfg);
	if (err < 0)
		return err;
	if (!spec->autocfg.line_outs && !spec->autocfg.hp_pins[0])
		return 0; /* can't find valid BIOS pin config */

	err = vt1708_auto_create_multi_out_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = vt1708_auto_create_hp_ctls(spec, spec->autocfg.hp_pins[0]);
	if (err < 0)
		return err;
	err = vt1708_auto_create_analog_input_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;

	if (spec->autocfg.dig_out_pin)
		spec->multiout.dig_out_nid = VT1708_DIGOUT_NID;
	if (spec->autocfg.dig_in_pin)
		spec->dig_in_nid = VT1708_DIGIN_NID;

	if (spec->kctl_alloc)
		spec->mixers[spec->num_mixers++] = spec->kctl_alloc;

	spec->init_verbs = vt1708_volume_init_verbs;	

	spec->input_mux = &spec->private_imux;

	return 1;
}

/* init callback for auto-configuration model -- overriding the default init */
static int via_auto_init(struct hda_codec *codec)
{
	via_init(codec);
	via_auto_init_multi_out(codec);
	via_auto_init_hp_out(codec);
	via_auto_init_analog_input(codec);
	spec->gen.main_out_badness = &via_main_out_badness;
	spec->gen.extra_out_badness = &via_extra_out_badness;

	err = snd_hda_parse_pin_defcfg(codec, &spec->gen.autocfg, NULL, 0);
	if (err < 0)
		return err;

	auto_parse_beep(codec);

	err = snd_hda_gen_parse_auto_config(codec, &spec->gen.autocfg);
	if (err < 0)
		return err;

	/* disable widget PM at start for compatibility */
	codec->power_save_node = 0;
	spec->gen.power_down_unused = 0;
	return 0;
}

static int via_init(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int i;

	for (i = 0; i < spec->num_iverbs; i++)
		snd_hda_sequence_write(codec, spec->init_verbs[i]);

	/* init power states */
	__analog_low_current_mode(codec, true);

	snd_hda_gen_init(codec);

	vt1708_update_hp_work(codec);

	return 0;
}

static int vt1708_build_controls(struct hda_codec *codec)
{
	/* In order not to create "Phantom Jack" controls,
	   temporary enable jackpoll */
	int err;
	int old_interval = codec->jackpoll_interval;
	codec->jackpoll_interval = msecs_to_jiffies(100);
	err = via_build_controls(codec);
	codec->jackpoll_interval = old_interval;
	return err;
}

static int vt1708_build_pcms(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int i, err;

	err = snd_hda_gen_build_pcms(codec);
	if (err < 0 || codec->core.vendor_id != 0x11061708)
		return err;

	/* We got noisy outputs on the right channel on VT1708 when
	 * 24bit samples are used.  Until any workaround is found,
	 * disable the 24bit format, so far.
	 */
	for (i = 0; i < ARRAY_SIZE(spec->gen.pcm_rec); i++) {
		struct hda_pcm *info = spec->gen.pcm_rec[i];
		if (!info)
			continue;
		if (!info->stream[SNDRV_PCM_STREAM_PLAYBACK].substreams ||
		    info->pcm_type != HDA_PCM_TYPE_AUDIO)
			continue;
		info->stream[SNDRV_PCM_STREAM_PLAYBACK].formats =
			SNDRV_PCM_FMTBIT_S16_LE;
	}

	return 0;
}

static int patch_vt1708(struct hda_codec *codec)
{
	struct via_spec *spec;
	int err;

	/* create a codec specific record */
	spec = kcalloc(1, sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	/* automatic parse from the BIOS config */
	err = vt1708_parse_auto_config(codec);
	if (err < 0) {
		via_free(codec);
		return err;
	} else if (!err) {
		printk(KERN_INFO "hda_codec: Cannot set up configuration "
		       "from BIOS.  Using genenic mode...\n");
	}

	
	spec->stream_name_analog = "VT1708 Analog";
	spec->stream_analog_playback = &vt1708_pcm_analog_playback;
	/* disable 32bit format on VT1708 */
	if (codec->vendor_id == 0x11061708)
		spec->stream_analog_playback = &vt1708_pcm_analog_s16_playback;
	spec->stream_analog_capture = &vt1708_pcm_analog_capture;

	spec->stream_name_digital = "VT1708 Digital";
	spec->stream_digital_playback = &vt1708_pcm_digital_playback;
	spec->stream_digital_capture = &vt1708_pcm_digital_capture;

	
	if (!spec->adc_nids && spec->input_mux) {
		spec->adc_nids = vt1708_adc_nids;
		spec->num_adc_nids = ARRAY_SIZE(vt1708_adc_nids);
		spec->mixers[spec->num_mixers] = vt1708_capture_mixer;
		spec->num_mixers++;
	}

	codec->patch_ops = via_patch_ops;

	codec->patch_ops.init = via_auto_init;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	spec->loopback.amplist = vt1708_loopbacks;
#endif
	spec = via_new_spec(codec);
	if (spec == NULL)
		return -ENOMEM;

	/* override some patch_ops */
	codec->patch_ops.build_controls = vt1708_build_controls;
	codec->patch_ops.build_pcms = vt1708_build_pcms;
	spec->gen.mixer_nid = 0x17;

	/* set jackpoll_interval while parsing the codec */
	codec->jackpoll_interval = msecs_to_jiffies(100);
	spec->vt1708_jack_detect = 1;

	/* don't support the input jack switching due to lack of unsol event */
	/* (it may work with polling, though, but it needs testing) */
	spec->gen.suppress_auto_mic = 1;
	/* Some machines show the broken speaker mute */
	spec->gen.auto_mute_via_amp = 1;

	/* Add HP and CD pin config connect bit re-config action */
	vt1708_set_pinconfig_connect(codec, VT1708_HP_PIN_NID);
	vt1708_set_pinconfig_connect(codec, VT1708_CD_PIN_NID);

	/* automatic parse from the BIOS config */
	err = via_parse_auto_config(codec);
	if (err < 0) {
		via_free(codec);
		return err;
	}

	/* add jack detect on/off control */
	spec->mixers[spec->num_mixers++] = vt1708_jack_detect_ctl;

	spec->init_verbs[spec->num_iverbs++] = vt1708_init_verbs;

	/* clear jackpoll_interval again; it's set dynamically */
	codec->jackpoll_interval = 0;

	return 0;
}

/* capture mixer elements */
static struct snd_kcontrol_new vt1709_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x14, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x14, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 2, 0x16, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 2, 0x16, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 1,
		.info = via_mux_enum_info,
		.get = via_mux_enum_get,
		.put = via_mux_enum_put,
	},
	{ } /* end */
};

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static struct hda_verb vt1709_10ch_volume_init_verbs[] = {
	/*
	 * Unmute ADC0-2 and set the default input to mic-in
	 */
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},


	/* Unmute input amps (CD, Line In, Mic 1 & Mic 2) of the analog-loopback
	 * mixer widget
	 */
	/* Amp Indices: AOW0=0, CD = 1, Mic1 = 2, Line = 3, Mic2 = 4 */
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(3)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(4)},

	/*
	 * Set up output selector (0x1a, 0x1b, 0x29)
	 */
	/* set vol=0 to output mixers */
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x29, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},

	/*
	 *  Unmute PW3 and PW4
	 */
	{0x1f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},

	/* Set input of PW4 as AOW4 */
	{0x20, AC_VERB_SET_CONNECT_SEL, 0x1},
	/* PW9 Output enable */
	{0x24, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	{ }
};

static struct hda_pcm_stream vt1709_10ch_pcm_analog_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 10,
	.nid = 0x10, /* NID to query formats and rates */
	.ops = {
		.open = via_playback_pcm_open,
		.prepare = via_playback_pcm_prepare,
		.cleanup = via_playback_pcm_cleanup
	},
};

static struct hda_pcm_stream vt1709_6ch_pcm_analog_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 6,
	.nid = 0x10, /* NID to query formats and rates */
	.ops = {
		.open = via_playback_pcm_open,
		.prepare = via_playback_pcm_prepare,
		.cleanup = via_playback_pcm_cleanup
	},
};

static struct hda_pcm_stream vt1709_pcm_analog_capture = {
	.substreams = 2,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0x14, /* NID to query formats and rates */
	.ops = {
		.prepare = via_capture_pcm_prepare,
		.cleanup = via_capture_pcm_cleanup
	},
};

static struct hda_pcm_stream vt1709_pcm_digital_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in via_build_pcms */
	.ops = {
		.open = via_dig_playback_pcm_open,
		.close = via_dig_playback_pcm_close
	},
};

static struct hda_pcm_stream vt1709_pcm_digital_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
};

static int vt1709_auto_fill_dac_nids(struct via_spec *spec,
				     const struct auto_pin_cfg *cfg)
{
	int i;
	hda_nid_t nid;

	if (cfg->line_outs == 4)  /* 10 channels */
		spec->multiout.num_dacs = cfg->line_outs+1; /* AOW0~AOW4 */
	else if (cfg->line_outs == 3) /* 6 channels */
		spec->multiout.num_dacs = cfg->line_outs; /* AOW0~AOW2 */

	spec->multiout.dac_nids = spec->private_dac_nids;

	if (cfg->line_outs == 4) { /* 10 channels */
		for (i = 0; i < cfg->line_outs; i++) {
			nid = cfg->line_out_pins[i];
			if (nid) {
				/* config dac list */
				switch (i) {
				case AUTO_SEQ_FRONT:
					/* AOW0 */
					spec->multiout.dac_nids[i] = 0x10;
					break;
				case AUTO_SEQ_CENLFE:
					/* AOW2 */
					spec->multiout.dac_nids[i] = 0x12;
					break;
				case AUTO_SEQ_SURROUND:
					/* AOW3 */
					spec->multiout.dac_nids[i] = 0x27;
					break;
				case AUTO_SEQ_SIDE:
					/* AOW1 */
					spec->multiout.dac_nids[i] = 0x11;
					break;
				default:
					break;
				}
			}
		}
		spec->multiout.dac_nids[cfg->line_outs] = 0x28; /* AOW4 */

	} else if (cfg->line_outs == 3) { /* 6 channels */
		for(i = 0; i < cfg->line_outs; i++) {
			nid = cfg->line_out_pins[i];
			if (nid) {
				/* config dac list */
				switch(i) {
				case AUTO_SEQ_FRONT:
					/* AOW0 */
					spec->multiout.dac_nids[i] = 0x10;
					break;
				case AUTO_SEQ_CENLFE:
					/* AOW2 */
					spec->multiout.dac_nids[i] = 0x12;
					break;
				case AUTO_SEQ_SURROUND:
					/* AOW1 */
					spec->multiout.dac_nids[i] = 0x11;
					break;
				default:
					break;
				}
			}
		}
	}

	return 0;
}

/* add playback controls from the parsed DAC table */
static int vt1709_auto_create_multi_out_ctls(struct via_spec *spec,
					     const struct auto_pin_cfg *cfg)
{
	char name[32];
	static const char *chname[4] = { "Front", "Surround", "C/LFE", "Side" };
	hda_nid_t nid = 0;
	int i, err;

	for (i = 0; i <= AUTO_SEQ_SIDE; i++) {
		nid = cfg->line_out_pins[i];

		if (!nid)	
			continue;

		if (i == AUTO_SEQ_CENLFE) {
			/* Center/LFE */
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
					      "Center Playback Volume",
					      HDA_COMPOSE_AMP_VAL(0x1b, 1, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
					      "LFE Playback Volume",
					      HDA_COMPOSE_AMP_VAL(0x1b, 2, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE,
					      "Center Playback Switch",
					      HDA_COMPOSE_AMP_VAL(0x1b, 1, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE,
					      "LFE Playback Switch",
					      HDA_COMPOSE_AMP_VAL(0x1b, 2, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
		} else if (i == AUTO_SEQ_FRONT){
			/* add control to mixer index 0 */
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
					      "Master Front Playback Volume",
					      HDA_COMPOSE_AMP_VAL(0x18, 3, 0,
								  HDA_INPUT));
			if (err < 0)
				return err;
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE,
					      "Master Front Playback Switch",
					      HDA_COMPOSE_AMP_VAL(0x18, 3, 0,
								  HDA_INPUT));
			if (err < 0)
				return err;
			
			/* add control to PW3 */
			sprintf(name, "%s Playback Volume", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL, name,
					      HDA_COMPOSE_AMP_VAL(nid, 3, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
			sprintf(name, "%s Playback Switch", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE, name,
					      HDA_COMPOSE_AMP_VAL(nid, 3, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
		} else if (i == AUTO_SEQ_SURROUND) {
			sprintf(name, "%s Playback Volume", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL, name,
					      HDA_COMPOSE_AMP_VAL(0x29, 3, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
			sprintf(name, "%s Playback Switch", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE, name,
					      HDA_COMPOSE_AMP_VAL(0x29, 3, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
		} else if (i == AUTO_SEQ_SIDE) {
			sprintf(name, "%s Playback Volume", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL, name,
					      HDA_COMPOSE_AMP_VAL(0x1a, 3, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
			sprintf(name, "%s Playback Switch", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE, name,
					      HDA_COMPOSE_AMP_VAL(0x1a, 3, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
		}
	}

	return 0;
}

static int vt1709_auto_create_hp_ctls(struct via_spec *spec, hda_nid_t pin)
{
	int err;

	if (!pin)
		return 0;

	if (spec->multiout.num_dacs == 5) /* 10 channels */
		spec->multiout.hp_nid = VT1709_HP_DAC_NID;
	else if (spec->multiout.num_dacs == 3) /* 6 channels */
		spec->multiout.hp_nid = 0;

	err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
			      "Headphone Playback Volume",
			      HDA_COMPOSE_AMP_VAL(pin, 3, 0, HDA_OUTPUT));
	if (err < 0)
		return err;
	err = via_add_control(spec, VIA_CTL_WIDGET_MUTE,
			      "Headphone Playback Switch",
			      HDA_COMPOSE_AMP_VAL(pin, 3, 0, HDA_OUTPUT));
	if (err < 0)
		return err;

	return 0;
}

/* create playback/capture controls for input pins */
static int vt1709_auto_create_analog_input_ctls(struct via_spec *spec,
						const struct auto_pin_cfg *cfg)
{
	static char *labels[] = {
		"Mic", "Front Mic", "Line", "Front Line", "CD", "Aux", NULL
	};
	struct hda_input_mux *imux = &spec->private_imux;
	int i, err, idx = 0;

	/* for internal loopback recording select */
	imux->items[imux->num_items].label = "Stereo Mixer";
	imux->items[imux->num_items].index = idx;
	imux->num_items++;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		if (!cfg->input_pins[i])
			continue;

		switch (cfg->input_pins[i]) {
		case 0x1d: /* Mic */
			idx = 2;
			break;
				
		case 0x1e: /* Line In */
			idx = 3;
			break;

		case 0x21: /* Front Mic */
			idx = 4;
			break;

		case 0x23: /* CD */
			idx = 1;
			break;
		}
		err = via_new_analog_input(spec, cfg->input_pins[i], labels[i],
					   idx, 0x18);
		if (err < 0)
			return err;
		imux->items[imux->num_items].label = labels[i];
		imux->items[imux->num_items].index = idx;
		imux->num_items++;
	}
	return 0;
}

static int vt1709_parse_auto_config(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int err;

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg, NULL);
	if (err < 0)
		return err;
	err = vt1709_auto_fill_dac_nids(spec, &spec->autocfg);
	if (err < 0)
		return err;
	if (!spec->autocfg.line_outs && !spec->autocfg.hp_pins[0])
		return 0; /* can't find valid BIOS pin config */

	err = vt1709_auto_create_multi_out_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = vt1709_auto_create_hp_ctls(spec, spec->autocfg.hp_pins[0]);
	if (err < 0)
		return err;
	err = vt1709_auto_create_analog_input_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;

	if (spec->autocfg.dig_out_pin)
		spec->multiout.dig_out_nid = VT1709_DIGOUT_NID;
	if (spec->autocfg.dig_in_pin)
		spec->dig_in_nid = VT1709_DIGIN_NID;

	if (spec->kctl_alloc)
		spec->mixers[spec->num_mixers++] = spec->kctl_alloc;

	spec->input_mux = &spec->private_imux;

	return 1;
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
static struct hda_amp_list vt1709_loopbacks[] = {
	{ 0x18, HDA_INPUT, 1 },
	{ 0x18, HDA_INPUT, 2 },
	{ 0x18, HDA_INPUT, 3 },
	{ 0x18, HDA_INPUT, 4 },
	{ } /* end */
};
#endif

static int patch_vt1709_10ch(struct hda_codec *codec)
{
	struct via_spec *spec;
	int err;

	/* create a codec specific record */
	spec = kcalloc(1, sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	err = vt1709_parse_auto_config(codec);
	if (err < 0) {
		via_free(codec);
		return err;
	} else if (!err) {
		printk(KERN_INFO "hda_codec: Cannot set up configuration.  "
		       "Using genenic mode...\n");
	}

	spec->init_verbs = vt1709_10ch_volume_init_verbs;	

	spec->stream_name_analog = "VT1709 Analog";
	spec->stream_analog_playback = &vt1709_10ch_pcm_analog_playback;
	spec->stream_analog_capture = &vt1709_pcm_analog_capture;

	spec->stream_name_digital = "VT1709 Digital";
	spec->stream_digital_playback = &vt1709_pcm_digital_playback;
	spec->stream_digital_capture = &vt1709_pcm_digital_capture;

	
	if (!spec->adc_nids && spec->input_mux) {
		spec->adc_nids = vt1709_adc_nids;
		spec->num_adc_nids = ARRAY_SIZE(vt1709_adc_nids);
		spec->mixers[spec->num_mixers] = vt1709_capture_mixer;
		spec->num_mixers++;
	}

	codec->patch_ops = via_patch_ops;

	codec->patch_ops.init = via_auto_init;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	spec->loopback.amplist = vt1709_loopbacks;
#endif

	return 0;
}
/*
 * generic initialization of ADC, input mixers and output mixers
 */
static struct hda_verb vt1709_6ch_volume_init_verbs[] = {
	/*
	 * Unmute ADC0-2 and set the default input to mic-in
	 */
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},


	/* Unmute input amps (CD, Line In, Mic 1 & Mic 2) of the analog-loopback
	 * mixer widget
	 */
	/* Amp Indices: AOW0=0, CD = 1, Mic1 = 2, Line = 3, Mic2 = 4 */
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(3)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(4)},

	/*
	 * Set up output selector (0x1a, 0x1b, 0x29)
	 */
	/* set vol=0 to output mixers */
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x29, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},

	/*
	 *  Unmute PW3 and PW4
	 */
	{0x1f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},

	/* Set input of PW4 as MW0 */
	{0x20, AC_VERB_SET_CONNECT_SEL, 0},
	/* PW9 Output enable */
	{0x24, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	{ }
};

static int patch_vt1709_6ch(struct hda_codec *codec)
{
	struct via_spec *spec;
	int err;

	/* create a codec specific record */
	spec = kcalloc(1, sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	err = vt1709_parse_auto_config(codec);
	if (err < 0) {
		via_free(codec);
		return err;
	} else if (!err) {
		printk(KERN_INFO "hda_codec: Cannot set up configuration.  "
		       "Using genenic mode...\n");
	}

	spec->init_verbs = vt1709_6ch_volume_init_verbs;	

	spec->stream_name_analog = "VT1709 Analog";
	spec->stream_analog_playback = &vt1709_6ch_pcm_analog_playback;
	spec->stream_analog_capture = &vt1709_pcm_analog_capture;

	spec->stream_name_digital = "VT1709 Digital";
	spec->stream_digital_playback = &vt1709_pcm_digital_playback;
	spec->stream_digital_capture = &vt1709_pcm_digital_capture;

	
	if (!spec->adc_nids && spec->input_mux) {
		spec->adc_nids = vt1709_adc_nids;
		spec->num_adc_nids = ARRAY_SIZE(vt1709_adc_nids);
		spec->mixers[spec->num_mixers] = vt1709_capture_mixer;
		spec->num_mixers++;
	}

	codec->patch_ops = via_patch_ops;

	codec->patch_ops.init = via_auto_init;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	spec->loopback.amplist = vt1709_loopbacks;
#endif
	return 0;
}

/* capture mixer elements */
static struct snd_kcontrol_new vt1708B_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x13, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x13, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x14, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x14, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 1,
		.info = via_mux_enum_info,
		.get = via_mux_enum_get,
		.put = via_mux_enum_put,
	},
	{ } /* end */
};
/*
 * generic initialization of ADC, input mixers and output mixers
 */
static struct hda_verb vt1708B_8ch_volume_init_verbs[] = {
	/*
	 * Unmute ADC0-1 and set the default input to mic-in
	 */
	{0x13, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},


	/* Unmute input amps (CD, Line In, Mic 1 & Mic 2) of the analog-loopback
	 * mixer widget
	 */
	/* Amp Indices: CD = 1, Mic1 = 2, Line = 3, Mic2 = 4 */
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(3)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(4)},

	/*
	 * Set up output mixers
	 */
	/* set vol=0 to output mixers */
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x26, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x27, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},

	/* Setup default input to PW4 */
	{0x1d, AC_VERB_SET_CONNECT_SEL, 0x1},
	/* PW9 Output enable */
	{0x20, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	/* PW10 Input enable */
	{0x21, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},
	{ }
};

static struct hda_verb vt1708B_4ch_volume_init_verbs[] = {
	/*
	 * Unmute ADC0-1 and set the default input to mic-in
	 */
	{0x13, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},


	/* Unmute input amps (CD, Line In, Mic 1 & Mic 2) of the analog-loopback
	 * mixer widget
	 */
	/* Amp Indices: CD = 1, Mic1 = 2, Line = 3, Mic2 = 4 */
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(3)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(4)},

	/*
	 * Set up output mixers
	 */
	/* set vol=0 to output mixers */
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x26, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x27, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},

	/* Setup default input of PW4 to MW0 */
	{0x1d, AC_VERB_SET_CONNECT_SEL, 0x0},
	/* PW9 Output enable */
	{0x20, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	/* PW10 Input enable */
	{0x21, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},
	{ }
};

static struct hda_pcm_stream vt1708B_8ch_pcm_analog_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 8,
	.nid = 0x10, /* NID to query formats and rates */
	.ops = {
		.open = via_playback_pcm_open,
		.prepare = via_playback_pcm_prepare,
		.cleanup = via_playback_pcm_cleanup
	},
};

static struct hda_pcm_stream vt1708B_4ch_pcm_analog_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 4,
	.nid = 0x10, /* NID to query formats and rates */
	.ops = {
		.open = via_playback_pcm_open,
		.prepare = via_playback_pcm_prepare,
		.cleanup = via_playback_pcm_cleanup
	},
};

static struct hda_pcm_stream vt1708B_pcm_analog_capture = {
	.substreams = 2,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0x13, /* NID to query formats and rates */
	.ops = {
		.prepare = via_capture_pcm_prepare,
		.cleanup = via_capture_pcm_cleanup
	},
};

static struct hda_pcm_stream vt1708B_pcm_digital_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in via_build_pcms */
	.ops = {
		.open = via_dig_playback_pcm_open,
		.close = via_dig_playback_pcm_close,
		.prepare = via_dig_playback_pcm_prepare
	},
};

static struct hda_pcm_stream vt1708B_pcm_digital_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
};

/* fill in the dac_nids table from the parsed pin configuration */
static int vt1708B_auto_fill_dac_nids(struct via_spec *spec,
				     const struct auto_pin_cfg *cfg)
{
	int i;
	hda_nid_t nid;

	spec->multiout.num_dacs = cfg->line_outs;

	spec->multiout.dac_nids = spec->private_dac_nids;

	for (i = 0; i < 4; i++) {
		nid = cfg->line_out_pins[i];
		if (nid) {
			/* config dac list */
			switch (i) {
			case AUTO_SEQ_FRONT:
				spec->multiout.dac_nids[i] = 0x10;
				break;
			case AUTO_SEQ_CENLFE:
				spec->multiout.dac_nids[i] = 0x24;
				break;
			case AUTO_SEQ_SURROUND:
				spec->multiout.dac_nids[i] = 0x25;
				break;
			case AUTO_SEQ_SIDE:
				spec->multiout.dac_nids[i] = 0x11;
				break;
			}
		}
	}

	return 0;
}

/* add playback controls from the parsed DAC table */
static int vt1708B_auto_create_multi_out_ctls(struct via_spec *spec,
					     const struct auto_pin_cfg *cfg)
{
	char name[32];
	static const char *chname[4] = { "Front", "Surround", "C/LFE", "Side" };
	hda_nid_t nid_vols[] = {0x16, 0x27, 0x26, 0x18};
	hda_nid_t nid, nid_vol = 0;
	int i, err;

	for (i = 0; i <= AUTO_SEQ_SIDE; i++) {
		nid = cfg->line_out_pins[i];

		if (!nid)
			continue;

		nid_vol = nid_vols[i];

		if (i == AUTO_SEQ_CENLFE) {
			/* Center/LFE */
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
					      "Center Playback Volume",
					      HDA_COMPOSE_AMP_VAL(nid_vol, 1, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
					      "LFE Playback Volume",
					      HDA_COMPOSE_AMP_VAL(nid_vol, 2, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE,
					      "Center Playback Switch",
					      HDA_COMPOSE_AMP_VAL(nid_vol, 1, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE,
					      "LFE Playback Switch",
					      HDA_COMPOSE_AMP_VAL(nid_vol, 2, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
		} else if (i == AUTO_SEQ_FRONT) {
			/* add control to mixer index 0 */
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
					      "Master Front Playback Volume",
					      HDA_COMPOSE_AMP_VAL(nid_vol, 3, 0,
								  HDA_INPUT));
			if (err < 0)
				return err;
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE,
					      "Master Front Playback Switch",
					      HDA_COMPOSE_AMP_VAL(nid_vol, 3, 0,
								  HDA_INPUT));
			if (err < 0)
				return err;

			/* add control to PW3 */
			sprintf(name, "%s Playback Volume", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL, name,
					      HDA_COMPOSE_AMP_VAL(nid, 3, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
			sprintf(name, "%s Playback Switch", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE, name,
					      HDA_COMPOSE_AMP_VAL(nid, 3, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
		} else {
			sprintf(name, "%s Playback Volume", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL, name,
					      HDA_COMPOSE_AMP_VAL(nid_vol, 3, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
			sprintf(name, "%s Playback Switch", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE, name,
					      HDA_COMPOSE_AMP_VAL(nid_vol, 3, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
		}
	}

	return 0;
}

static int vt1708B_auto_create_hp_ctls(struct via_spec *spec, hda_nid_t pin)
{
	int err;

	if (!pin)
		return 0;

	spec->multiout.hp_nid = VT1708B_HP_NID; /* AOW3 */

	err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
			      "Headphone Playback Volume",
			      HDA_COMPOSE_AMP_VAL(pin, 3, 0, HDA_OUTPUT));
	if (err < 0)
		return err;
	err = via_add_control(spec, VIA_CTL_WIDGET_MUTE,
			      "Headphone Playback Switch",
			      HDA_COMPOSE_AMP_VAL(pin, 3, 0, HDA_OUTPUT));
	if (err < 0)
		return err;

	return 0;
}

/* create playback/capture controls for input pins */
static int vt1708B_auto_create_analog_input_ctls(struct via_spec *spec,
						const struct auto_pin_cfg *cfg)
{
	static char *labels[] = {
		"Mic", "Front Mic", "Line", "Front Line", "CD", "Aux", NULL
	};
	struct hda_input_mux *imux = &spec->private_imux;
	int i, err, idx = 0;

	/* for internal loopback recording select */
	imux->items[imux->num_items].label = "Stereo Mixer";
	imux->items[imux->num_items].index = idx;
	imux->num_items++;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		if (!cfg->input_pins[i])
			continue;

		switch (cfg->input_pins[i]) {
		case 0x1a: /* Mic */
			idx = 2;
			break;

		case 0x1b: /* Line In */
			idx = 3;
			break;

		case 0x1e: /* Front Mic */
			idx = 4;
			break;

		case 0x1f: /* CD */
			idx = 1;
			break;
		}
		err = via_new_analog_input(spec, cfg->input_pins[i], labels[i],
					   idx, 0x16);
		if (err < 0)
			return err;
		imux->items[imux->num_items].label = labels[i];
		imux->items[imux->num_items].index = idx;
		imux->num_items++;
	}
	return 0;
}

static int vt1708B_parse_auto_config(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int err;

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg, NULL);
	if (err < 0)
		return err;
	err = vt1708B_auto_fill_dac_nids(spec, &spec->autocfg);
	if (err < 0)
		return err;
	if (!spec->autocfg.line_outs && !spec->autocfg.hp_pins[0])
		return 0; /* can't find valid BIOS pin config */

	err = vt1708B_auto_create_multi_out_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = vt1708B_auto_create_hp_ctls(spec, spec->autocfg.hp_pins[0]);
	if (err < 0)
		return err;
	err = vt1708B_auto_create_analog_input_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;

	if (spec->autocfg.dig_out_pin)
		spec->multiout.dig_out_nid = VT1708B_DIGOUT_NID;
	if (spec->autocfg.dig_in_pin)
		spec->dig_in_nid = VT1708B_DIGIN_NID;

	if (spec->kctl_alloc)
		spec->mixers[spec->num_mixers++] = spec->kctl_alloc;

	spec->input_mux = &spec->private_imux;

	return 1;
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
static struct hda_amp_list vt1708B_loopbacks[] = {
	{ 0x16, HDA_INPUT, 1 },
	{ 0x16, HDA_INPUT, 2 },
	{ 0x16, HDA_INPUT, 3 },
	{ 0x16, HDA_INPUT, 4 },
	{ } /* end */
};
#endif

static int patch_vt1708B_8ch(struct hda_codec *codec)
static int patch_vt1709(struct hda_codec *codec)
{
	struct via_spec *spec;
	int err;

	/* create a codec specific record */
	spec = via_new_spec(codec);
	if (spec == NULL)
		return -ENOMEM;

	spec->gen.mixer_nid = 0x18;

	err = via_parse_auto_config(codec);
	if (err < 0) {
		via_free(codec);
		return err;
	}

	return 0;
}

static int patch_vt1708S(struct hda_codec *codec);
static int patch_vt1708B(struct hda_codec *codec)
{
	struct via_spec *spec;
	int err;

	if (get_codec_type(codec) == VT1708BCE)
		return patch_vt1708S(codec);

	/* create a codec specific record */
	spec = via_new_spec(codec);
	if (spec == NULL)
		return -ENOMEM;

	spec->gen.mixer_nid = 0x16;

	/* automatic parse from the BIOS config */
	err = via_parse_auto_config(codec);
	if (err < 0) {
		via_free(codec);
		return err;
	}

	return 0;
}

/* Patch for VT1708S */
static const struct hda_verb vt1708S_init_verbs[] = {
	/* Enable Mic Boost Volume backdoor */
	{0x1, 0xf98, 0x1},
	/* don't bybass mixer */
	{0x1, 0xf88, 0xc0},
	{ }
};

static void override_mic_boost(struct hda_codec *codec, hda_nid_t pin,
			       int offset, int num_steps, int step_size)
{
	snd_hda_override_wcaps(codec, pin,
			       get_wcaps(codec, pin) | AC_WCAP_IN_AMP);
	snd_hda_override_amp_caps(codec, pin, HDA_INPUT,
				  (offset << AC_AMPCAP_OFFSET_SHIFT) |
				  (num_steps << AC_AMPCAP_NUM_STEPS_SHIFT) |
				  (step_size << AC_AMPCAP_STEP_SIZE_SHIFT) |
				  (0 << AC_AMPCAP_MUTE_SHIFT));
}

static int patch_vt1708S(struct hda_codec *codec)
{
	struct via_spec *spec;
	int err;

	/* create a codec specific record */
	spec = via_new_spec(codec);
	if (spec == NULL)
		return -ENOMEM;

	spec->gen.mixer_nid = 0x16;
	override_mic_boost(codec, 0x1a, 0, 3, 40);
	override_mic_boost(codec, 0x1e, 0, 3, 40);

	/* correct names for VT1708BCE */
	if (get_codec_type(codec) == VT1708BCE)
		snd_hda_codec_set_name(codec, "VT1708BCE");
	/* correct names for VT1705 */
	if (codec->core.vendor_id == 0x11064397)
		snd_hda_codec_set_name(codec, "VT1705");

	/* automatic parse from the BIOS config */
	err = via_parse_auto_config(codec);
	if (err < 0) {
		via_free(codec);
		return err;
	}

	spec->init_verbs[spec->num_iverbs++] = vt1708S_init_verbs;

	return 0;
}

/* Patch for VT1702 */

static const struct hda_verb vt1702_init_verbs[] = {
	/* mixer enable */
	{0x1, 0xF88, 0x3},
	/* GPIO 0~2 */
	{0x1, 0xF82, 0x3F},
	{ }
};

static int patch_vt1702(struct hda_codec *codec)
{
	struct via_spec *spec;
	int err;

	/* create a codec specific record */
	spec = via_new_spec(codec);
	if (spec == NULL)
		return -ENOMEM;

	spec->gen.mixer_nid = 0x1a;

	/* limit AA path volume to 0 dB */
	snd_hda_override_amp_caps(codec, 0x1A, HDA_INPUT,
				  (0x17 << AC_AMPCAP_OFFSET_SHIFT) |
				  (0x17 << AC_AMPCAP_NUM_STEPS_SHIFT) |
				  (0x5 << AC_AMPCAP_STEP_SIZE_SHIFT) |
				  (1 << AC_AMPCAP_MUTE_SHIFT));

	/* automatic parse from the BIOS config */
	err = via_parse_auto_config(codec);
	if (err < 0) {
		via_free(codec);
		return err;
	}

	spec->init_verbs[spec->num_iverbs++] = vt1702_init_verbs;

	return 0;
}

/* Patch for VT1718S */

static const struct hda_verb vt1718S_init_verbs[] = {
	/* Enable MW0 adjust Gain 5 */
	{0x1, 0xfb2, 0x10},
	/* Enable Boost Volume backdoor */
	{0x1, 0xf88, 0x8},

	{ }
};

/* Add a connection to the primary DAC from AA-mixer for some codecs
 * This isn't listed from the raw info, but the chip has a secret connection.
 */
static int add_secret_dac_path(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int i, nums;
	hda_nid_t conn[8];
	hda_nid_t nid;

	if (!spec->gen.mixer_nid)
		return 0;
	nums = snd_hda_get_connections(codec, spec->gen.mixer_nid, conn,
				       ARRAY_SIZE(conn) - 1);
	if (nums < 0)
		return nums;

	for (i = 0; i < nums; i++) {
		if (get_wcaps_type(get_wcaps(codec, conn[i])) == AC_WID_AUD_OUT)
			return 0;
	}

	/* find the primary DAC and add to the connection list */
	for_each_hda_codec_node(nid, codec) {
		unsigned int caps = get_wcaps(codec, nid);
		if (get_wcaps_type(caps) == AC_WID_AUD_OUT &&
		    !(caps & AC_WCAP_DIGITAL)) {
			conn[nums++] = nid;
			return snd_hda_override_conn_list(codec,
							  spec->gen.mixer_nid,
							  nums, conn);
		}
	}
	return 0;
}


static int patch_vt1718S(struct hda_codec *codec)
{
	struct via_spec *spec;
	int err;

	/* create a codec specific record */
	spec = via_new_spec(codec);
	if (spec == NULL)
		return -ENOMEM;

	spec->gen.mixer_nid = 0x21;
	override_mic_boost(codec, 0x2b, 0, 3, 40);
	override_mic_boost(codec, 0x29, 0, 3, 40);
	add_secret_dac_path(codec);

	/* automatic parse from the BIOS config */
	err = via_parse_auto_config(codec);
	if (err < 0) {
		via_free(codec);
		return err;
	}

	spec->init_verbs[spec->num_iverbs++] = vt1718S_init_verbs;

	return 0;
}

/* Patch for VT1716S */

static int vt1716s_dmic_info(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int vt1716s_dmic_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	int index = 0;

	index = snd_hda_codec_read(codec, 0x26, 0,
					       AC_VERB_GET_CONNECT_SEL, 0);
	if (index != -1)
		*ucontrol->value.integer.value = index;

	return 0;
}

static int vt1716s_dmic_put(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct via_spec *spec = codec->spec;
	int index = *ucontrol->value.integer.value;

	snd_hda_codec_write(codec, 0x26, 0,
					       AC_VERB_SET_CONNECT_SEL, index);
	spec->dmic_enabled = index;
	return 1;
}

static const struct snd_kcontrol_new vt1716s_dmic_mixer[] = {
	HDA_CODEC_VOLUME("Digital Mic Capture Volume", 0x22, 0x0, HDA_INPUT),
	{
	 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	 .name = "Digital Mic Capture Switch",
	 .subdevice = HDA_SUBDEV_NID_FLAG | 0x26,
	 .count = 1,
	 .info = vt1716s_dmic_info,
	 .get = vt1716s_dmic_get,
	 .put = vt1716s_dmic_put,
	 },
	{}			/* end */
};


/* mono-out mixer elements */
static const struct snd_kcontrol_new vt1716S_mono_out_mixer[] = {
	HDA_CODEC_MUTE("Mono Playback Switch", 0x2a, 0x0, HDA_OUTPUT),
	{ } /* end */
};

static const struct hda_verb vt1716S_init_verbs[] = {
	/* Enable Boost Volume backdoor */
	{0x1, 0xf8a, 0x80},
	/* don't bybass mixer */
	{0x1, 0xf88, 0xc0},
	/* Enable mono output */
	{0x1, 0xf90, 0x08},
	{ }
};

static int patch_vt1716S(struct hda_codec *codec)
{
	struct via_spec *spec;
	int err;

	/* create a codec specific record */
	spec = via_new_spec(codec);
	if (spec == NULL)
		return -ENOMEM;

	spec->gen.mixer_nid = 0x16;
	override_mic_boost(codec, 0x1a, 0, 3, 40);
	override_mic_boost(codec, 0x1e, 0, 3, 40);

	/* automatic parse from the BIOS config */
	err = via_parse_auto_config(codec);
	if (err < 0) {
		via_free(codec);
		return err;
	}

	spec->init_verbs[spec->num_iverbs++]  = vt1716S_init_verbs;

	spec->mixers[spec->num_mixers++] = vt1716s_dmic_mixer;
	spec->mixers[spec->num_mixers++] = vt1716S_mono_out_mixer;

	return 0;
}

/* for vt2002P */

static const struct hda_verb vt2002P_init_verbs[] = {
	/* Class-D speaker related verbs */
	{0x1, 0xfe0, 0x4},
	{0x1, 0xfe9, 0x80},
	{0x1, 0xfe2, 0x22},
	/* Enable Boost Volume backdoor */
	{0x1, 0xfb9, 0x24},
	/* Enable AOW0 to MW9 */
	{0x1, 0xfb8, 0x88},
	{ }
};

static const struct hda_verb vt1802_init_verbs[] = {
	/* Enable Boost Volume backdoor */
	{0x1, 0xfb9, 0x24},
	/* Enable AOW0 to MW9 */
	{0x1, 0xfb8, 0x88},
	{ }
};

/*
 * pin fix-up
 */
enum {
	VIA_FIXUP_INTMIC_BOOST,
	VIA_FIXUP_ASUS_G75,
};

static void via_fixup_intmic_boost(struct hda_codec *codec,
				  const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		override_mic_boost(codec, 0x30, 0, 2, 40);
}

static const struct hda_fixup via_fixups[] = {
	[VIA_FIXUP_INTMIC_BOOST] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = via_fixup_intmic_boost,
	},
	[VIA_FIXUP_ASUS_G75] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			/* set 0x24 and 0x33 as speakers */
			{ 0x24, 0x991301f0 },
			{ 0x33, 0x991301f1 }, /* subwoofer */
			{ }
		}
	},
};

static const struct snd_pci_quirk vt2002p_fixups[] = {
	SND_PCI_QUIRK(0x1043, 0x1487, "Asus G75", VIA_FIXUP_ASUS_G75),
	SND_PCI_QUIRK(0x1043, 0x8532, "Asus X202E", VIA_FIXUP_INTMIC_BOOST),
	{}
};

/* NIDs 0x24 and 0x33 on VT1802 have connections to non-existing NID 0x3e
 * Replace this with mixer NID 0x1c
 */
static void fix_vt1802_connections(struct hda_codec *codec)
{
	static hda_nid_t conn_24[] = { 0x14, 0x1c };
	static hda_nid_t conn_33[] = { 0x1c };

	snd_hda_override_conn_list(codec, 0x24, ARRAY_SIZE(conn_24), conn_24);
	snd_hda_override_conn_list(codec, 0x33, ARRAY_SIZE(conn_33), conn_33);
}

/* patch for vt2002P */
static int patch_vt2002P(struct hda_codec *codec)
{
	struct via_spec *spec;
	int err;

	/* create a codec specific record */
	spec = kcalloc(1, sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	/* automatic parse from the BIOS config */
	err = vt1708B_parse_auto_config(codec);
	if (err < 0) {
		via_free(codec);
		return err;
	} else if (!err) {
		printk(KERN_INFO "hda_codec: Cannot set up configuration "
		       "from BIOS.  Using genenic mode...\n");
	}

	spec->init_verbs = vt1708B_8ch_volume_init_verbs;

	spec->stream_name_analog = "VT1708B Analog";
	spec->stream_analog_playback = &vt1708B_8ch_pcm_analog_playback;
	spec->stream_analog_capture = &vt1708B_pcm_analog_capture;

	spec->stream_name_digital = "VT1708B Digital";
	spec->stream_digital_playback = &vt1708B_pcm_digital_playback;
	spec->stream_digital_capture = &vt1708B_pcm_digital_capture;

	if (!spec->adc_nids && spec->input_mux) {
		spec->adc_nids = vt1708B_adc_nids;
		spec->num_adc_nids = ARRAY_SIZE(vt1708B_adc_nids);
		spec->mixers[spec->num_mixers] = vt1708B_capture_mixer;
		spec->num_mixers++;
	}

	codec->patch_ops = via_patch_ops;

	codec->patch_ops.init = via_auto_init;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	spec->loopback.amplist = vt1708B_loopbacks;
#endif
	spec = via_new_spec(codec);
	if (spec == NULL)
		return -ENOMEM;

	spec->gen.mixer_nid = 0x21;
	override_mic_boost(codec, 0x2b, 0, 3, 40);
	override_mic_boost(codec, 0x29, 0, 3, 40);
	if (spec->codec_type == VT1802)
		fix_vt1802_connections(codec);
	add_secret_dac_path(codec);

	snd_hda_pick_fixup(codec, NULL, vt2002p_fixups, via_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	/* automatic parse from the BIOS config */
	err = via_parse_auto_config(codec);
	if (err < 0) {
		via_free(codec);
		return err;
	}

	if (spec->codec_type == VT1802)
		spec->init_verbs[spec->num_iverbs++] = vt1802_init_verbs;
	else
		spec->init_verbs[spec->num_iverbs++] = vt2002P_init_verbs;

	return 0;
}

static int patch_vt1708B_4ch(struct hda_codec *codec)
/* for vt1812 */

static const struct hda_verb vt1812_init_verbs[] = {
	/* Enable Boost Volume backdoor */
	{0x1, 0xfb9, 0x24},
	/* Enable AOW0 to MW9 */
	{0x1, 0xfb8, 0xa8},
	{ }
};

/* patch for vt1812 */
static int patch_vt1812(struct hda_codec *codec)
{
	struct via_spec *spec;
	int err;

	/* create a codec specific record */
	spec = kcalloc(1, sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	/* automatic parse from the BIOS config */
	err = vt1708B_parse_auto_config(codec);
	if (err < 0) {
		via_free(codec);
		return err;
	} else if (!err) {
		printk(KERN_INFO "hda_codec: Cannot set up configuration "
		       "from BIOS.  Using genenic mode...\n");
	}

	spec->init_verbs = vt1708B_4ch_volume_init_verbs;

	spec->stream_name_analog = "VT1708B Analog";
	spec->stream_analog_playback = &vt1708B_4ch_pcm_analog_playback;
	spec->stream_analog_capture = &vt1708B_pcm_analog_capture;

	spec->stream_name_digital = "VT1708B Digital";
	spec->stream_digital_playback = &vt1708B_pcm_digital_playback;
	spec->stream_digital_capture = &vt1708B_pcm_digital_capture;

	if (!spec->adc_nids && spec->input_mux) {
		spec->adc_nids = vt1708B_adc_nids;
		spec->num_adc_nids = ARRAY_SIZE(vt1708B_adc_nids);
		spec->mixers[spec->num_mixers] = vt1708B_capture_mixer;
		spec->num_mixers++;
	}

	codec->patch_ops = via_patch_ops;

	codec->patch_ops.init = via_auto_init;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	spec->loopback.amplist = vt1708B_loopbacks;
#endif
	spec = via_new_spec(codec);
	if (spec == NULL)
		return -ENOMEM;

	spec->gen.mixer_nid = 0x21;
	override_mic_boost(codec, 0x2b, 0, 3, 40);
	override_mic_boost(codec, 0x29, 0, 3, 40);
	add_secret_dac_path(codec);

	/* automatic parse from the BIOS config */
	err = via_parse_auto_config(codec);
	if (err < 0) {
		via_free(codec);
		return err;
	}

	spec->init_verbs[spec->num_iverbs++]  = vt1812_init_verbs;

	return 0;
}

/* patch for vt3476 */

static const struct hda_verb vt3476_init_verbs[] = {
	/* Enable DMic 8/16/32K */
	{0x1, 0xF7B, 0x30},
	/* Enable Boost Volume backdoor */
	{0x1, 0xFB9, 0x20},
	/* Enable AOW-MW9 path */
	{0x1, 0xFB8, 0x10},
	{ }
};

static int patch_vt3476(struct hda_codec *codec)
{
	struct via_spec *spec;
	int err;

	/* create a codec specific record */
	spec = via_new_spec(codec);
	if (spec == NULL)
		return -ENOMEM;

	spec->gen.mixer_nid = 0x3f;
	add_secret_dac_path(codec);

	/* automatic parse from the BIOS config */
	err = via_parse_auto_config(codec);
	if (err < 0) {
		via_free(codec);
		return err;
	}

	spec->init_verbs[spec->num_iverbs++] = vt3476_init_verbs;

	return 0;
}

/*
 * patch entries
 */
struct hda_codec_preset snd_hda_preset_via[] = {
	{ .id = 0x11061708, .name = "VIA VT1708", .patch = patch_vt1708},
	{ .id = 0x11061709, .name = "VIA VT1708", .patch = patch_vt1708},
	{ .id = 0x1106170A, .name = "VIA VT1708", .patch = patch_vt1708},
	{ .id = 0x1106170B, .name = "VIA VT1708", .patch = patch_vt1708},
	{ .id = 0x1106E710, .name = "VIA VT1709 10-Ch",
	  .patch = patch_vt1709_10ch},
	{ .id = 0x1106E711, .name = "VIA VT1709 10-Ch",
	  .patch = patch_vt1709_10ch},
	{ .id = 0x1106E712, .name = "VIA VT1709 10-Ch",
	  .patch = patch_vt1709_10ch},
	{ .id = 0x1106E713, .name = "VIA VT1709 10-Ch",
	  .patch = patch_vt1709_10ch},
	{ .id = 0x1106E714, .name = "VIA VT1709 6-Ch",
	  .patch = patch_vt1709_6ch},
	{ .id = 0x1106E715, .name = "VIA VT1709 6-Ch",
	  .patch = patch_vt1709_6ch},
	{ .id = 0x1106E716, .name = "VIA VT1709 6-Ch",
	  .patch = patch_vt1709_6ch},
	{ .id = 0x1106E717, .name = "VIA VT1709 6-Ch",
	  .patch = patch_vt1709_6ch},
	{ .id = 0x1106E720, .name = "VIA VT1708B 8-Ch",
	  .patch = patch_vt1708B_8ch},
	{ .id = 0x1106E721, .name = "VIA VT1708B 8-Ch",
	  .patch = patch_vt1708B_8ch},
	{ .id = 0x1106E722, .name = "VIA VT1708B 8-Ch",
	  .patch = patch_vt1708B_8ch},
	{ .id = 0x1106E723, .name = "VIA VT1708B 8-Ch",
	  .patch = patch_vt1708B_8ch},
	{ .id = 0x1106E724, .name = "VIA VT1708B 4-Ch",
	  .patch = patch_vt1708B_4ch},
	{ .id = 0x1106E725, .name = "VIA VT1708B 4-Ch",
	  .patch = patch_vt1708B_4ch},
	{ .id = 0x1106E726, .name = "VIA VT1708B 4-Ch",
	  .patch = patch_vt1708B_4ch},
	{ .id = 0x1106E727, .name = "VIA VT1708B 4-Ch",
	  .patch = patch_vt1708B_4ch},
	{} /* terminator */
};
static const struct hda_device_id snd_hda_id_via[] = {
	HDA_CODEC_ENTRY(0x11061708, "VT1708", patch_vt1708),
	HDA_CODEC_ENTRY(0x11061709, "VT1708", patch_vt1708),
	HDA_CODEC_ENTRY(0x1106170a, "VT1708", patch_vt1708),
	HDA_CODEC_ENTRY(0x1106170b, "VT1708", patch_vt1708),
	HDA_CODEC_ENTRY(0x1106e710, "VT1709 10-Ch", patch_vt1709),
	HDA_CODEC_ENTRY(0x1106e711, "VT1709 10-Ch", patch_vt1709),
	HDA_CODEC_ENTRY(0x1106e712, "VT1709 10-Ch", patch_vt1709),
	HDA_CODEC_ENTRY(0x1106e713, "VT1709 10-Ch", patch_vt1709),
	HDA_CODEC_ENTRY(0x1106e714, "VT1709 6-Ch", patch_vt1709),
	HDA_CODEC_ENTRY(0x1106e715, "VT1709 6-Ch", patch_vt1709),
	HDA_CODEC_ENTRY(0x1106e716, "VT1709 6-Ch", patch_vt1709),
	HDA_CODEC_ENTRY(0x1106e717, "VT1709 6-Ch", patch_vt1709),
	HDA_CODEC_ENTRY(0x1106e720, "VT1708B 8-Ch", patch_vt1708B),
	HDA_CODEC_ENTRY(0x1106e721, "VT1708B 8-Ch", patch_vt1708B),
	HDA_CODEC_ENTRY(0x1106e722, "VT1708B 8-Ch", patch_vt1708B),
	HDA_CODEC_ENTRY(0x1106e723, "VT1708B 8-Ch", patch_vt1708B),
	HDA_CODEC_ENTRY(0x1106e724, "VT1708B 4-Ch", patch_vt1708B),
	HDA_CODEC_ENTRY(0x1106e725, "VT1708B 4-Ch", patch_vt1708B),
	HDA_CODEC_ENTRY(0x1106e726, "VT1708B 4-Ch", patch_vt1708B),
	HDA_CODEC_ENTRY(0x1106e727, "VT1708B 4-Ch", patch_vt1708B),
	HDA_CODEC_ENTRY(0x11060397, "VT1708S", patch_vt1708S),
	HDA_CODEC_ENTRY(0x11061397, "VT1708S", patch_vt1708S),
	HDA_CODEC_ENTRY(0x11062397, "VT1708S", patch_vt1708S),
	HDA_CODEC_ENTRY(0x11063397, "VT1708S", patch_vt1708S),
	HDA_CODEC_ENTRY(0x11064397, "VT1705", patch_vt1708S),
	HDA_CODEC_ENTRY(0x11065397, "VT1708S", patch_vt1708S),
	HDA_CODEC_ENTRY(0x11066397, "VT1708S", patch_vt1708S),
	HDA_CODEC_ENTRY(0x11067397, "VT1708S", patch_vt1708S),
	HDA_CODEC_ENTRY(0x11060398, "VT1702", patch_vt1702),
	HDA_CODEC_ENTRY(0x11061398, "VT1702", patch_vt1702),
	HDA_CODEC_ENTRY(0x11062398, "VT1702", patch_vt1702),
	HDA_CODEC_ENTRY(0x11063398, "VT1702", patch_vt1702),
	HDA_CODEC_ENTRY(0x11064398, "VT1702", patch_vt1702),
	HDA_CODEC_ENTRY(0x11065398, "VT1702", patch_vt1702),
	HDA_CODEC_ENTRY(0x11066398, "VT1702", patch_vt1702),
	HDA_CODEC_ENTRY(0x11067398, "VT1702", patch_vt1702),
	HDA_CODEC_ENTRY(0x11060428, "VT1718S", patch_vt1718S),
	HDA_CODEC_ENTRY(0x11064428, "VT1718S", patch_vt1718S),
	HDA_CODEC_ENTRY(0x11060441, "VT2020", patch_vt1718S),
	HDA_CODEC_ENTRY(0x11064441, "VT1828S", patch_vt1718S),
	HDA_CODEC_ENTRY(0x11060433, "VT1716S", patch_vt1716S),
	HDA_CODEC_ENTRY(0x1106a721, "VT1716S", patch_vt1716S),
	HDA_CODEC_ENTRY(0x11060438, "VT2002P", patch_vt2002P),
	HDA_CODEC_ENTRY(0x11064438, "VT2002P", patch_vt2002P),
	HDA_CODEC_ENTRY(0x11060448, "VT1812", patch_vt1812),
	HDA_CODEC_ENTRY(0x11060440, "VT1818S", patch_vt1708S),
	HDA_CODEC_ENTRY(0x11060446, "VT1802", patch_vt2002P),
	HDA_CODEC_ENTRY(0x11068446, "VT1802", patch_vt2002P),
	HDA_CODEC_ENTRY(0x11064760, "VT1705CF", patch_vt3476),
	HDA_CODEC_ENTRY(0x11064761, "VT1708SCE", patch_vt3476),
	HDA_CODEC_ENTRY(0x11064762, "VT1808", patch_vt3476),
	{} /* terminator */
};
MODULE_DEVICE_TABLE(hdaudio, snd_hda_id_via);

static struct hda_codec_driver via_driver = {
	.id = snd_hda_id_via,
};

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("VIA HD-audio codec");

module_hda_codec_driver(via_driver);
