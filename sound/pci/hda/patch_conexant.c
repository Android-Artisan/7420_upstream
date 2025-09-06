/*
 * HD audio interface patch for Conexant HDA audio codec
 *
 * Copyright (c) 2006 Pototskiy Akex <alex.pototskiy@gmail.com>
 * 		      Takashi Iwai <tiwai@suse.de>
 * 		      Tobin Davis  <tdavis@dsl-only.net>
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

#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <sound/core.h>
#include "hda_codec.h"
#include "hda_local.h"
#include "hda_patch.h"

#define CXT_PIN_DIR_IN              0x00
#define CXT_PIN_DIR_OUT             0x01
#define CXT_PIN_DIR_INOUT           0x02
#define CXT_PIN_DIR_IN_NOMICBIAS    0x03
#define CXT_PIN_DIR_INOUT_NOMICBIAS 0x04

#define CONEXANT_HP_EVENT	0x37
#define CONEXANT_MIC_EVENT	0x38



struct conexant_spec {

	struct snd_kcontrol_new *mixers[5];
	int num_mixers;

	const struct hda_verb *init_verbs[5];	/* initialization verbs
						 * don't forget NULL
						 * termination!
						 */
	unsigned int num_init_verbs;

	/* playback */
	struct hda_multi_out multiout;	/* playback set-up
					 * max_channels, dacs must be set
					 * dig_out_nid and hp_nid are optional
					 */
	unsigned int cur_eapd;
	unsigned int hp_present;
	unsigned int need_dac_fix;

	/* capture */
	unsigned int num_adc_nids;
	hda_nid_t *adc_nids;
	hda_nid_t dig_in_nid;		/* digital-in NID; optional */

	unsigned int cur_adc_idx;
	hda_nid_t cur_adc;
	unsigned int cur_adc_stream_tag;
	unsigned int cur_adc_format;

	/* capture source */
	const struct hda_input_mux *input_mux;
	hda_nid_t *capsrc_nids;
	unsigned int cur_mux[3];

	/* channel model */
	const struct hda_channel_mode *channel_mode;
	int num_channel_mode;

	/* PCM information */
	struct hda_pcm pcm_rec[2];	/* used in build_pcms() */

	unsigned int spdif_route;

	/* dynamic controls, init_verbs and input_mux */
	struct auto_pin_cfg autocfg;
	unsigned int num_kctl_alloc, num_kctl_used;
	struct snd_kcontrol_new *kctl_alloc;
	struct hda_input_mux private_imux;
	hda_nid_t private_dac_nids[AUTO_CFG_MAX_OUTS];

};

static int conexant_playback_pcm_open(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      struct snd_pcm_substream *substream)
{
	struct conexant_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_open(codec, &spec->multiout, substream,
					     hinfo);
}

static int conexant_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
					 struct hda_codec *codec,
					 unsigned int stream_tag,
					 unsigned int format,
					 struct snd_pcm_substream *substream)
{
	struct conexant_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_prepare(codec, &spec->multiout,
						stream_tag,
						format, substream);
}

static int conexant_playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
					 struct hda_codec *codec,
					 struct snd_pcm_substream *substream)
{
	struct conexant_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_cleanup(codec, &spec->multiout);
}

/*
 * Digital out
 */
static int conexant_dig_playback_pcm_open(struct hda_pcm_stream *hinfo,
					  struct hda_codec *codec,
					  struct snd_pcm_substream *substream)
{
	struct conexant_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_open(codec, &spec->multiout);
}

static int conexant_dig_playback_pcm_close(struct hda_pcm_stream *hinfo,
					 struct hda_codec *codec,
					 struct snd_pcm_substream *substream)
{
	struct conexant_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_close(codec, &spec->multiout);
}

static int conexant_dig_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
					 struct hda_codec *codec,
					 unsigned int stream_tag,
					 unsigned int format,
					 struct snd_pcm_substream *substream)
{
	struct conexant_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_prepare(codec, &spec->multiout,
					     stream_tag,
					     format, substream);
}

/*
 * Analog capture
 */
static int conexant_capture_pcm_prepare(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      unsigned int stream_tag,
				      unsigned int format,
				      struct snd_pcm_substream *substream)
{
	struct conexant_spec *spec = codec->spec;
	snd_hda_codec_setup_stream(codec, spec->adc_nids[substream->number],
				   stream_tag, 0, format);
	return 0;
}

static int conexant_capture_pcm_cleanup(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      struct snd_pcm_substream *substream)
{
	struct conexant_spec *spec = codec->spec;
	snd_hda_codec_cleanup_stream(codec, spec->adc_nids[substream->number]);
	return 0;
}



static struct hda_pcm_stream conexant_pcm_analog_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0, /* fill later */
	.ops = {
		.open = conexant_playback_pcm_open,
		.prepare = conexant_playback_pcm_prepare,
		.cleanup = conexant_playback_pcm_cleanup
	},
};

static struct hda_pcm_stream conexant_pcm_analog_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0, /* fill later */
	.ops = {
		.prepare = conexant_capture_pcm_prepare,
		.cleanup = conexant_capture_pcm_cleanup
	},
};


static struct hda_pcm_stream conexant_pcm_digital_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0, /* fill later */
	.ops = {
		.open = conexant_dig_playback_pcm_open,
		.close = conexant_dig_playback_pcm_close,
		.prepare = conexant_dig_playback_pcm_prepare
	},
};

static struct hda_pcm_stream conexant_pcm_digital_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in alc_build_pcms */
};

static int cx5051_capture_pcm_prepare(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      unsigned int stream_tag,
				      unsigned int format,
				      struct snd_pcm_substream *substream)
{
	struct conexant_spec *spec = codec->spec;
	spec->cur_adc = spec->adc_nids[spec->cur_adc_idx];
	spec->cur_adc_stream_tag = stream_tag;
	spec->cur_adc_format = format;
	snd_hda_codec_setup_stream(codec, spec->cur_adc, stream_tag, 0, format);
	return 0;
}

static int cx5051_capture_pcm_cleanup(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      struct snd_pcm_substream *substream)
{
	struct conexant_spec *spec = codec->spec;
	snd_hda_codec_cleanup_stream(codec, spec->cur_adc);
	spec->cur_adc = 0;
	return 0;
}

static struct hda_pcm_stream cx5051_pcm_analog_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0, /* fill later */
	.ops = {
		.prepare = cx5051_capture_pcm_prepare,
		.cleanup = cx5051_capture_pcm_cleanup
	},
};

static int conexant_build_pcms(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;
	struct hda_pcm *info = spec->pcm_rec;

	codec->num_pcms = 1;
	codec->pcm_info = info;

	info->name = "CONEXANT Analog";
	info->stream[SNDRV_PCM_STREAM_PLAYBACK] = conexant_pcm_analog_playback;
	info->stream[SNDRV_PCM_STREAM_PLAYBACK].channels_max =
		spec->multiout.max_channels;
	info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid =
		spec->multiout.dac_nids[0];
	if (codec->vendor_id == 0x14f15051)
		info->stream[SNDRV_PCM_STREAM_CAPTURE] =
			cx5051_pcm_analog_capture;
	else
		info->stream[SNDRV_PCM_STREAM_CAPTURE] =
			conexant_pcm_analog_capture;
	info->stream[SNDRV_PCM_STREAM_CAPTURE].substreams = spec->num_adc_nids;
	info->stream[SNDRV_PCM_STREAM_CAPTURE].nid = spec->adc_nids[0];

	if (spec->multiout.dig_out_nid) {
		info++;
		codec->num_pcms++;
		info->name = "Conexant Digital";
		info->pcm_type = HDA_PCM_TYPE_SPDIF;
		info->stream[SNDRV_PCM_STREAM_PLAYBACK] =
			conexant_pcm_digital_playback;
		info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid =
			spec->multiout.dig_out_nid;
		if (spec->dig_in_nid) {
			info->stream[SNDRV_PCM_STREAM_CAPTURE] =
				conexant_pcm_digital_capture;
			info->stream[SNDRV_PCM_STREAM_CAPTURE].nid =
				spec->dig_in_nid;
		}
	}
#include <linux/module.h>
#include <sound/core.h>
#include <sound/jack.h>

#include "hda_codec.h"
#include "hda_local.h"
#include "hda_auto_parser.h"
#include "hda_beep.h"
#include "hda_jack.h"
#include "hda_generic.h"

struct conexant_spec {
	struct hda_gen_spec gen;

	unsigned int beep_amp;

	/* extra EAPD pins */
	unsigned int num_eapds;
	hda_nid_t eapds[4];
	bool dynamic_eapd;
	hda_nid_t mute_led_eapd;

	unsigned int parse_flags; /* flag for snd_hda_parse_pin_defcfg() */

	/* OPLC XO specific */
	bool recording;
	bool dc_enable;
	unsigned int dc_input_bias; /* offset into olpc_xo_dc_bias */
	struct nid_path *dc_mode_path;
};


#ifdef CONFIG_SND_HDA_INPUT_BEEP
static inline void set_beep_amp(struct conexant_spec *spec, hda_nid_t nid,
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
	struct conexant_spec *spec = codec->spec;
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
#else
#define set_beep_amp(spec, nid, idx, dir) /* NOP */
#define add_beep_ctls(codec)	0
#endif

/*
 * Automatic parser for CX20641 & co
 */

#ifdef CONFIG_SND_HDA_INPUT_BEEP
static void cx_auto_parse_beep(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;
	hda_nid_t nid;

	for_each_hda_codec_node(nid, codec)
		if (get_wcaps_type(get_wcaps(codec, nid)) == AC_WID_BEEP) {
			set_beep_amp(spec, nid, 0, HDA_OUTPUT);
			break;
		}
}
#else
#define cx_auto_parse_beep(codec)
#endif

/* parse EAPDs */
static void cx_auto_parse_eapd(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;
	hda_nid_t nid;

	for_each_hda_codec_node(nid, codec) {
		if (get_wcaps_type(get_wcaps(codec, nid)) != AC_WID_PIN)
			continue;
		if (!(snd_hda_query_pin_caps(codec, nid) & AC_PINCAP_EAPD))
			continue;
		spec->eapds[spec->num_eapds++] = nid;
		if (spec->num_eapds >= ARRAY_SIZE(spec->eapds))
			break;
	}

	/* NOTE: below is a wild guess; if we have more than two EAPDs,
	 * it's a new chip, where EAPDs are supposed to be associated to
	 * pins, and we can control EAPD per pin.
	 * OTOH, if only one or two EAPDs are found, it's an old chip,
	 * thus it might control over all pins.
	 */
	if (spec->num_eapds > 2)
		spec->dynamic_eapd = 1;
}

static void cx_auto_turn_eapd(struct hda_codec *codec, int num_pins,
			      hda_nid_t *pins, bool on)
{
	int i;
	for (i = 0; i < num_pins; i++) {
		if (snd_hda_query_pin_caps(codec, pins[i]) & AC_PINCAP_EAPD)
			snd_hda_codec_write(codec, pins[i], 0,
					    AC_VERB_SET_EAPD_BTLENABLE,
					    on ? 0x02 : 0);
	}
}

/* turn on/off EAPD according to Master switch */
static void cx_auto_vmaster_hook(void *private_data, int enabled)
{
	struct hda_codec *codec = private_data;
	struct conexant_spec *spec = codec->spec;

	cx_auto_turn_eapd(codec, spec->num_eapds, spec->eapds, enabled);
}

/* turn on/off EAPD according to Master switch (inversely!) for mute LED */
static void cx_auto_vmaster_hook_mute_led(void *private_data, int enabled)
{
	struct hda_codec *codec = private_data;
	struct conexant_spec *spec = codec->spec;

	snd_hda_codec_write(codec, spec->mute_led_eapd, 0,
			    AC_VERB_SET_EAPD_BTLENABLE,
			    enabled ? 0x00 : 0x02);
}

static int cx_auto_build_controls(struct hda_codec *codec)
{
	int err;

	err = snd_hda_gen_build_controls(codec);
	if (err < 0)
		return err;

	err = add_beep_ctls(codec);
	if (err < 0)
		return err;

	return 0;
}

static int conexant_mux_enum_info(struct snd_kcontrol *kcontrol,
	       			  struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct conexant_spec *spec = codec->spec;

	return snd_hda_input_mux_info(spec->input_mux, uinfo);
}

static int conexant_mux_enum_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct conexant_spec *spec = codec->spec;
	unsigned int adc_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);

	ucontrol->value.enumerated.item[0] = spec->cur_mux[adc_idx];
	return 0;
}

static int conexant_mux_enum_put(struct snd_kcontrol *kcontrol,
static int cx_auto_init(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;
	snd_hda_gen_init(codec);
	if (!spec->dynamic_eapd)
		cx_auto_turn_eapd(codec, spec->num_eapds, spec->eapds, true);

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_INIT);

	return 0;
}

static void cx_auto_reboot_notify(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;

	switch (codec->core.vendor_id) {
	case 0x14f12008: /* CX8200 */
	case 0x14f150f2: /* CX20722 */
	case 0x14f150f4: /* CX20724 */
		break;
	default:
		return;
	}

	/* Turn the problematic codec into D3 to avoid spurious noises
	   from the internal speaker during (and after) reboot */
	cx_auto_turn_eapd(codec, spec->num_eapds, spec->eapds, false);

	snd_hda_codec_set_power_to_all(codec, codec->core.afg, AC_PWRST_D3);
	snd_hda_codec_write(codec, codec->core.afg, 0,
			    AC_VERB_SET_POWER_STATE, AC_PWRST_D3);
	msleep(10);
}

static void cx_auto_free(struct hda_codec *codec)
{
	cx_auto_reboot_notify(codec);
	snd_hda_gen_free(codec);
}

static const struct hda_codec_ops cx_auto_patch_ops = {
	.build_controls = cx_auto_build_controls,
	.build_pcms = snd_hda_gen_build_pcms,
	.init = cx_auto_init,
	.reboot_notify = cx_auto_reboot_notify,
	.free = cx_auto_free,
	.unsol_event = snd_hda_jack_unsol_event,
#ifdef CONFIG_PM
	.check_power_status = snd_hda_gen_check_power_status,
#endif
};

/*
 * pin fix-up
 */
enum {
	CXT_PINCFG_LENOVO_X200,
	CXT_PINCFG_LENOVO_TP410,
	CXT_PINCFG_LEMOTE_A1004,
	CXT_PINCFG_LEMOTE_A1205,
	CXT_PINCFG_COMPAQ_CQ60,
	CXT_FIXUP_STEREO_DMIC,
	CXT_PINCFG_LENOVO_NOTEBOOK,
	CXT_FIXUP_INC_MIC_BOOST,
	CXT_FIXUP_HEADPHONE_MIC_PIN,
	CXT_FIXUP_HEADPHONE_MIC,
	CXT_FIXUP_GPIO1,
	CXT_FIXUP_ASPIRE_DMIC,
	CXT_FIXUP_THINKPAD_ACPI,
	CXT_FIXUP_OLPC_XO,
	CXT_FIXUP_CAP_MIX_AMP,
	CXT_FIXUP_TOSHIBA_P105,
	CXT_FIXUP_HP_530,
	CXT_FIXUP_CAP_MIX_AMP_5047,
	CXT_FIXUP_MUTE_LED_EAPD,
	CXT_FIXUP_HP_DOCK,
	CXT_FIXUP_HP_SPECTRE,
	CXT_FIXUP_HP_GATE_MIC,
};

/* for hda_fixup_thinkpad_acpi() */
#include "thinkpad_helper.c"

static void cxt_fixup_stereo_dmic(struct hda_codec *codec,
				  const struct hda_fixup *fix, int action)
{
	struct conexant_spec *spec = codec->spec;
	spec->gen.inv_dmic_split = 1;
}

static void cxt5066_increase_mic_boost(struct hda_codec *codec,
				   const struct hda_fixup *fix, int action)
{
	if (action != HDA_FIXUP_ACT_PRE_PROBE)
		return;

	snd_hda_override_amp_caps(codec, 0x17, HDA_OUTPUT,
				  (0x3 << AC_AMPCAP_OFFSET_SHIFT) |
				  (0x4 << AC_AMPCAP_NUM_STEPS_SHIFT) |
				  (0x27 << AC_AMPCAP_STEP_SIZE_SHIFT) |
				  (0 << AC_AMPCAP_MUTE_SHIFT));
}

static void cxt_update_headset_mode(struct hda_codec *codec)
{
	/* The verbs used in this function were tested on a Conexant CX20751/2 codec. */
	int i;
	bool mic_mode = false;
	struct conexant_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->gen.autocfg;

	hda_nid_t mux_pin = spec->gen.imux_pins[spec->gen.cur_mux[0]];

	for (i = 0; i < cfg->num_inputs; i++)
		if (cfg->inputs[i].pin == mux_pin) {
			mic_mode = !!cfg->inputs[i].is_headphone_mic;
			break;
		}

	if (mic_mode) {
		snd_hda_codec_write_cache(codec, 0x1c, 0, 0x410, 0x7c); /* enable merged mode for analog int-mic */
		spec->gen.hp_jack_present = false;
	} else {
		snd_hda_codec_write_cache(codec, 0x1c, 0, 0x410, 0x54); /* disable merged mode for analog int-mic */
		spec->gen.hp_jack_present = snd_hda_jack_detect(codec, spec->gen.autocfg.hp_pins[0]);
	}

	snd_hda_gen_update_outputs(codec);
}

static void cxt_update_headset_mode_hook(struct hda_codec *codec,
					 struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	cxt_update_headset_mode(codec);
}

static void cxt_fixup_headphone_mic(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	struct conexant_spec *spec = codec->spec;

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		spec->parse_flags |= HDA_PINCFG_HEADPHONE_MIC;
		snd_hdac_regmap_add_vendor_verb(&codec->core, 0x410);
		break;
	case HDA_FIXUP_ACT_PROBE:
		spec->gen.cap_sync_hook = cxt_update_headset_mode_hook;
		spec->gen.automute_hook = cxt_update_headset_mode;
		break;
	case HDA_FIXUP_ACT_INIT:
		cxt_update_headset_mode(codec);
		break;
	}
}

/* OPLC XO 1.5 fixup */

/* OLPC XO-1.5 supports DC input mode (e.g. for use with analog sensors)
 * through the microphone jack.
 * When the user enables this through a mixer switch, both internal and
 * external microphones are disabled. Gain is fixed at 0dB. In this mode,
 * we also allow the bias to be configured through a separate mixer
 * control. */

#define update_mic_pin(codec, nid, val)					\
	snd_hda_codec_update_cache(codec, nid, 0,			\
				   AC_VERB_SET_PIN_WIDGET_CONTROL, val)

static const struct hda_input_mux olpc_xo_dc_bias = {
	.num_items = 3,
	.items = {
		{ "Off", PIN_IN },
		{ "50%", PIN_VREF50 },
		{ "80%", PIN_VREF80 },
	},
};

static void olpc_xo_update_mic_boost(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;
	int ch, val;

	for (ch = 0; ch < 2; ch++) {
		val = AC_AMP_SET_OUTPUT |
			(ch ? AC_AMP_SET_RIGHT : AC_AMP_SET_LEFT);
		if (!spec->dc_enable)
			val |= snd_hda_codec_amp_read(codec, 0x17, ch, HDA_OUTPUT, 0);
		snd_hda_codec_write(codec, 0x17, 0,
				    AC_VERB_SET_AMP_GAIN_MUTE, val);
	}
}

static void olpc_xo_update_mic_pins(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;
	int cur_input, val;
	struct nid_path *path;

	cur_input = spec->gen.input_paths[0][spec->gen.cur_mux[0]];

	/* Set up mic pins for port-B, C and F dynamically as the recording
	 * LED is turned on/off by these pin controls
	 */
	if (!spec->dc_enable) {
		/* disable DC bias path and pin for port F */
		update_mic_pin(codec, 0x1e, 0);
		snd_hda_activate_path(codec, spec->dc_mode_path, false, false);

		/* update port B (ext mic) and C (int mic) */
		/* OLPC defers mic widget control until when capture is
		 * started because the microphone LED comes on as soon as
		 * these settings are put in place. if we did this before
		 * recording, it would give the false indication that
		 * recording is happening when it is not.
		 */
		update_mic_pin(codec, 0x1a, spec->recording ?
			       snd_hda_codec_get_pin_target(codec, 0x1a) : 0);
		update_mic_pin(codec, 0x1b, spec->recording ?
			       snd_hda_codec_get_pin_target(codec, 0x1b) : 0);
		/* enable normal mic path */
		path = snd_hda_get_path_from_idx(codec, cur_input);
		if (path)
			snd_hda_activate_path(codec, path, true, false);
	} else {
		/* disable normal mic path */
		path = snd_hda_get_path_from_idx(codec, cur_input);
		if (path)
			snd_hda_activate_path(codec, path, false, false);

		/* Even though port F is the DC input, the bias is controlled
		 * on port B.  We also leave that port as an active input (but
		 * unselected) in DC mode just in case that is necessary to
		 * make the bias setting take effect.
		 */
		if (spec->recording)
			val = olpc_xo_dc_bias.items[spec->dc_input_bias].index;
		else
			val = 0;
		update_mic_pin(codec, 0x1a, val);
		update_mic_pin(codec, 0x1b, 0);
		/* enable DC bias path and pin */
		update_mic_pin(codec, 0x1e, spec->recording ? PIN_IN : 0);
		snd_hda_activate_path(codec, spec->dc_mode_path, true, false);
	}
}

/* mic_autoswitch hook */
static void olpc_xo_automic(struct hda_codec *codec,
			    struct hda_jack_callback *jack)
{
	struct conexant_spec *spec = codec->spec;

	/* in DC mode, we don't handle automic */
	if (!spec->dc_enable)
		snd_hda_gen_mic_autoswitch(codec, jack);
	olpc_xo_update_mic_pins(codec);
	if (spec->dc_enable)
		olpc_xo_update_mic_boost(codec);
}

/* pcm_capture hook */
static void olpc_xo_capture_hook(struct hda_pcm_stream *hinfo,
				 struct hda_codec *codec,
				 struct snd_pcm_substream *substream,
				 int action)
{
	struct conexant_spec *spec = codec->spec;

	/* toggle spec->recording flag and update mic pins accordingly
	 * for turning on/off LED
	 */
	switch (action) {
	case HDA_GEN_PCM_ACT_PREPARE:
		spec->recording = 1;
		olpc_xo_update_mic_pins(codec);
		break;
	case HDA_GEN_PCM_ACT_CLEANUP:
		spec->recording = 0;
		olpc_xo_update_mic_pins(codec);
		break;
	}
}

static int olpc_xo_dc_mode_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct conexant_spec *spec = codec->spec;
	ucontrol->value.integer.value[0] = spec->dc_enable;
	return 0;
}

static int olpc_xo_dc_mode_put(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct conexant_spec *spec = codec->spec;
	int dc_enable = !!ucontrol->value.integer.value[0];

	if (dc_enable == spec->dc_enable)
		return 0;

	spec->dc_enable = dc_enable;
	olpc_xo_update_mic_pins(codec);
	olpc_xo_update_mic_boost(codec);
	return 1;
}

static int olpc_xo_dc_bias_enum_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct conexant_spec *spec = codec->spec;
	ucontrol->value.enumerated.item[0] = spec->dc_input_bias;
	return 0;
}

static int olpc_xo_dc_bias_enum_info(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_info *uinfo)
{
	return snd_hda_input_mux_info(&olpc_xo_dc_bias, uinfo);
}

static int olpc_xo_dc_bias_enum_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct conexant_spec *spec = codec->spec;
	const struct hda_input_mux *imux = &olpc_xo_dc_bias;
	unsigned int idx;

	idx = ucontrol->value.enumerated.item[0];
	if (idx >= imux->num_items)
		idx = imux->num_items - 1;
	if (spec->dc_input_bias == idx)
		return 0;

	spec->dc_input_bias = idx;
	if (spec->dc_enable)
		olpc_xo_update_mic_pins(codec);
	return 1;
}

static const struct snd_kcontrol_new olpc_xo_mixers[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "DC Mode Enable Switch",
		.info = snd_ctl_boolean_mono_info,
		.get = olpc_xo_dc_mode_get,
		.put = olpc_xo_dc_mode_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "DC Input Bias Enum",
		.info = olpc_xo_dc_bias_enum_info,
		.get = olpc_xo_dc_bias_enum_get,
		.put = olpc_xo_dc_bias_enum_put,
	},
	{}
};

/* overriding mic boost put callback; update mic boost volume only when
 * DC mode is disabled
 */
static int olpc_xo_mic_boost_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct conexant_spec *spec = codec->spec;
	unsigned int adc_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);

	return snd_hda_input_mux_put(codec, spec->input_mux, ucontrol,
				     spec->capsrc_nids[adc_idx],
				     &spec->cur_mux[adc_idx]);
}

static int conexant_init(struct hda_codec *codec)
	int ret = snd_hda_mixer_amp_volume_put(kcontrol, ucontrol);
	if (ret > 0 && spec->dc_enable)
		olpc_xo_update_mic_boost(codec);
	return ret;
}

static void cxt_fixup_olpc_xo(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	struct conexant_spec *spec = codec->spec;
	int i;

	for (i = 0; i < spec->num_init_verbs; i++)
		snd_hda_sequence_write(codec, spec->init_verbs[i]);
	return 0;
}

static void conexant_free(struct hda_codec *codec)
{
        struct conexant_spec *spec = codec->spec;
        unsigned int i;

        if (spec->kctl_alloc) {
                for (i = 0; i < spec->num_kctl_used; i++)
                        kfree(spec->kctl_alloc[i].name);
                kfree(spec->kctl_alloc);
        }

	kfree(codec->spec);
}

static int conexant_build_controls(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;
	unsigned int i;
	int err;

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
		err = snd_hda_create_spdif_in_ctls(codec,spec->dig_in_nid);
		if (err < 0)
			return err;
	}
	return 0;
}

static struct hda_codec_ops conexant_patch_ops = {
	.build_controls = conexant_build_controls,
	.build_pcms = conexant_build_pcms,
	.init = conexant_init,
	.free = conexant_free,
};

/*
 * EAPD control
 * the private value = nid | (invert << 8)
 */

#define cxt_eapd_info		snd_ctl_boolean_mono_info

static int cxt_eapd_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct conexant_spec *spec = codec->spec;
	int invert = (kcontrol->private_value >> 8) & 1;
	if (invert)
		ucontrol->value.integer.value[0] = !spec->cur_eapd;
	else
		ucontrol->value.integer.value[0] = spec->cur_eapd;
	return 0;

}

static int cxt_eapd_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct conexant_spec *spec = codec->spec;
	int invert = (kcontrol->private_value >> 8) & 1;
	hda_nid_t nid = kcontrol->private_value & 0xff;
	unsigned int eapd;

	eapd = !!ucontrol->value.integer.value[0];
	if (invert)
		eapd = !eapd;
	if (eapd == spec->cur_eapd)
		return 0;
	
	spec->cur_eapd = eapd;
	snd_hda_codec_write_cache(codec, nid,
				  0, AC_VERB_SET_EAPD_BTLENABLE,
				  eapd ? 0x02 : 0x00);
	return 1;
}

/* controls for test mode */
#ifdef CONFIG_SND_DEBUG

#define CXT_EAPD_SWITCH(xname, nid, mask) \
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = 0,  \
	  .info = cxt_eapd_info, \
	  .get = cxt_eapd_get, \
	  .put = cxt_eapd_put, \
	  .private_value = nid | (mask<<16) }



static int conexant_ch_mode_info(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct conexant_spec *spec = codec->spec;
	return snd_hda_ch_mode_info(codec, uinfo, spec->channel_mode,
				    spec->num_channel_mode);
}

static int conexant_ch_mode_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct conexant_spec *spec = codec->spec;
	return snd_hda_ch_mode_get(codec, ucontrol, spec->channel_mode,
				   spec->num_channel_mode,
				   spec->multiout.max_channels);
}

static int conexant_ch_mode_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct conexant_spec *spec = codec->spec;
	int err = snd_hda_ch_mode_put(codec, ucontrol, spec->channel_mode,
				      spec->num_channel_mode,
				      &spec->multiout.max_channels);
	if (err >= 0 && spec->need_dac_fix)
		spec->multiout.num_dacs = spec->multiout.max_channels / 2;
	return err;
}

#define CXT_PIN_MODE(xname, nid, dir) \
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = 0,  \
	  .info = conexant_ch_mode_info, \
	  .get = conexant_ch_mode_get, \
	  .put = conexant_ch_mode_put, \
	  .private_value = nid | (dir<<16) }

#endif /* CONFIG_SND_DEBUG */

/* Conexant 5045 specific */

static hda_nid_t cxt5045_dac_nids[1] = { 0x19 };
static hda_nid_t cxt5045_adc_nids[1] = { 0x1a };
static hda_nid_t cxt5045_capsrc_nids[1] = { 0x1a };
#define CXT5045_SPDIF_OUT	0x18

static struct hda_channel_mode cxt5045_modes[1] = {
	{ 2, NULL },
};

static struct hda_input_mux cxt5045_capture_source = {
	.num_items = 2,
	.items = {
		{ "IntMic", 0x1 },
		{ "ExtMic", 0x2 },
	}
};

static struct hda_input_mux cxt5045_capture_source_benq = {
	.num_items = 3,
	.items = {
		{ "IntMic", 0x1 },
		{ "ExtMic", 0x2 },
		{ "LineIn", 0x3 },
	}
};

static struct hda_input_mux cxt5045_capture_source_hp530 = {
	.num_items = 2,
	.items = {
		{ "ExtMic", 0x1 },
		{ "IntMic", 0x2 },
	}
};

/* turn on/off EAPD (+ mute HP) as a master switch */
static int cxt5045_hp_master_sw_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct conexant_spec *spec = codec->spec;
	unsigned int bits;

	if (!cxt_eapd_put(kcontrol, ucontrol))
		return 0;

	/* toggle internal speakers mute depending of presence of
	 * the headphone jack
	 */
	bits = (!spec->hp_present && spec->cur_eapd) ? 0 : HDA_AMP_MUTE;
	snd_hda_codec_amp_stereo(codec, 0x10, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);

	bits = spec->cur_eapd ? 0 : HDA_AMP_MUTE;
	snd_hda_codec_amp_stereo(codec, 0x11, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
	return 1;
}

/* bind volumes of both NID 0x10 and 0x11 */
static struct hda_bind_ctls cxt5045_hp_bind_master_vol = {
	.ops = &snd_hda_bind_vol,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x10, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x11, 3, 0, HDA_OUTPUT),
		0
	},
};

/* toggle input of built-in and mic jack appropriately */
static void cxt5045_hp_automic(struct hda_codec *codec)
{
	static struct hda_verb mic_jack_on[] = {
		{0x14, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
		{0x12, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
		{}
	};
	static struct hda_verb mic_jack_off[] = {
		{0x12, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
		{0x14, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
		{}
	};
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x12, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	if (present)
		snd_hda_sequence_write(codec, mic_jack_on);
	else
		snd_hda_sequence_write(codec, mic_jack_off);
}


/* mute internal speaker if HP is plugged */
static void cxt5045_hp_automute(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;
	unsigned int bits;

	spec->hp_present = snd_hda_codec_read(codec, 0x11, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;

	bits = (spec->hp_present || !spec->cur_eapd) ? HDA_AMP_MUTE : 0; 
	snd_hda_codec_amp_stereo(codec, 0x10, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
}

/* unsolicited event for HP jack sensing */
static void cxt5045_hp_unsol_event(struct hda_codec *codec,
				   unsigned int res)
{
	res >>= 26;
	switch (res) {
	case CONEXANT_HP_EVENT:
		cxt5045_hp_automute(codec);
		break;
	case CONEXANT_MIC_EVENT:
		cxt5045_hp_automic(codec);
		break;

	}
}

static struct snd_kcontrol_new cxt5045_mixers[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.info = conexant_mux_enum_info,
		.get = conexant_mux_enum_get,
		.put = conexant_mux_enum_put
	},
	HDA_CODEC_VOLUME("Int Mic Capture Volume", 0x1a, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Int Mic Capture Switch", 0x1a, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("Ext Mic Capture Volume", 0x1a, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Ext Mic Capture Switch", 0x1a, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("PCM Playback Volume", 0x17, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("PCM Playback Switch", 0x17, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Int Mic Playback Volume", 0x17, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Int Mic Playback Switch", 0x17, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Ext Mic Playback Volume", 0x17, 0x2, HDA_INPUT),
	HDA_CODEC_MUTE("Ext Mic Playback Switch", 0x17, 0x2, HDA_INPUT),
	HDA_BIND_VOL("Master Playback Volume", &cxt5045_hp_bind_master_vol),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = cxt_eapd_info,
		.get = cxt_eapd_get,
		.put = cxt5045_hp_master_sw_put,
		.private_value = 0x10,
	},

	{}
};

static struct snd_kcontrol_new cxt5045_benq_mixers[] = {
	HDA_CODEC_VOLUME("Line In Capture Volume", 0x1a, 0x03, HDA_INPUT),
	HDA_CODEC_MUTE("Line In Capture Switch", 0x1a, 0x03, HDA_INPUT),
	HDA_CODEC_VOLUME("Line In Playback Volume", 0x17, 0x3, HDA_INPUT),
	HDA_CODEC_MUTE("Line In Playback Switch", 0x17, 0x3, HDA_INPUT),

	{}
};

static struct snd_kcontrol_new cxt5045_mixers_hp530[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.info = conexant_mux_enum_info,
		.get = conexant_mux_enum_get,
		.put = conexant_mux_enum_put
	},
	HDA_CODEC_VOLUME("Int Mic Capture Volume", 0x1a, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Int Mic Capture Switch", 0x1a, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Ext Mic Capture Volume", 0x1a, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Ext Mic Capture Switch", 0x1a, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("PCM Playback Volume", 0x17, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("PCM Playback Switch", 0x17, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Int Mic Playback Volume", 0x17, 0x2, HDA_INPUT),
	HDA_CODEC_MUTE("Int Mic Playback Switch", 0x17, 0x2, HDA_INPUT),
	HDA_CODEC_VOLUME("Ext Mic Playback Volume", 0x17, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Ext Mic Playback Switch", 0x17, 0x1, HDA_INPUT),
	HDA_BIND_VOL("Master Playback Volume", &cxt5045_hp_bind_master_vol),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = cxt_eapd_info,
		.get = cxt_eapd_get,
		.put = cxt5045_hp_master_sw_put,
		.private_value = 0x10,
	},

	{}
};

static struct hda_verb cxt5045_init_verbs[] = {
	/* Line in, Mic */
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN|AC_PINCTL_VREF_80 },
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN|AC_PINCTL_VREF_80 },
	/* HP, Amp  */
	{0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x10, AC_VERB_SET_CONNECT_SEL, 0x1},
	{0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x11, AC_VERB_SET_CONNECT_SEL, 0x1},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* Record selector: Int mic */
	{0x1a, AC_VERB_SET_CONNECT_SEL,0x1},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE,
	 AC_AMP_SET_INPUT|AC_AMP_SET_RIGHT|AC_AMP_SET_LEFT|0x17},
	/* SPDIF route: PCM */
	{0x13, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{ 0x13, AC_VERB_SET_CONNECT_SEL, 0x0 },
	/* EAPD */
	{0x10, AC_VERB_SET_EAPD_BTLENABLE, 0x2 }, /* default on */ 
	{ } /* end */
};

static struct hda_verb cxt5045_benq_init_verbs[] = {
	/* Int Mic, Mic */
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN|AC_PINCTL_VREF_80 },
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN|AC_PINCTL_VREF_80 },
	/* Line In,HP, Amp  */
	{0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x10, AC_VERB_SET_CONNECT_SEL, 0x1},
	{0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x11, AC_VERB_SET_CONNECT_SEL, 0x1},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* Record selector: Int mic */
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x1},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE,
	 AC_AMP_SET_INPUT|AC_AMP_SET_RIGHT|AC_AMP_SET_LEFT|0x17},
	/* SPDIF route: PCM */
	{0x13, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x13, AC_VERB_SET_CONNECT_SEL, 0x0},
	/* EAPD */
	{0x10, AC_VERB_SET_EAPD_BTLENABLE, 0x2}, /* default on */
	{ } /* end */
};

static struct hda_verb cxt5045_hp_sense_init_verbs[] = {
	/* pin sensing on HP jack */
	{0x11, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | CONEXANT_HP_EVENT},
	{ } /* end */
};

static struct hda_verb cxt5045_mic_sense_init_verbs[] = {
	/* pin sensing on HP jack */
	{0x12, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | CONEXANT_MIC_EVENT},
	{ } /* end */
};

#ifdef CONFIG_SND_DEBUG
/* Test configuration for debugging, modelled after the ALC260 test
 * configuration.
 */
static struct hda_input_mux cxt5045_test_capture_source = {
	.num_items = 5,
	.items = {
		{ "MIXER", 0x0 },
		{ "MIC1 pin", 0x1 },
		{ "LINE1 pin", 0x2 },
		{ "HP-OUT pin", 0x3 },
		{ "CD pin", 0x4 },
        },
};

static struct snd_kcontrol_new cxt5045_test_mixer[] = {

	/* Output controls */
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x10, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x10, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Node 11 Playback Volume", 0x11, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Node 11 Playback Switch", 0x11, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Node 12 Playback Volume", 0x12, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Node 12 Playback Switch", 0x12, 0x0, HDA_OUTPUT),
	
	/* Modes for retasking pin widgets */
	CXT_PIN_MODE("HP-OUT pin mode", 0x11, CXT_PIN_DIR_INOUT),
	CXT_PIN_MODE("LINE1 pin mode", 0x12, CXT_PIN_DIR_INOUT),

	/* EAPD Switch Control */
	CXT_EAPD_SWITCH("External Amplifier", 0x10, 0x0),

	/* Loopback mixer controls */

	HDA_CODEC_VOLUME("Mixer-1 Volume", 0x17, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mixer-1 Switch", 0x17, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mixer-2 Volume", 0x17, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Mixer-2 Switch", 0x17, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Mixer-3 Volume", 0x17, 0x2, HDA_INPUT),
	HDA_CODEC_MUTE("Mixer-3 Switch", 0x17, 0x2, HDA_INPUT),
	HDA_CODEC_VOLUME("Mixer-4 Volume", 0x17, 0x3, HDA_INPUT),
	HDA_CODEC_MUTE("Mixer-4 Switch", 0x17, 0x3, HDA_INPUT),
	HDA_CODEC_VOLUME("Mixer-5 Volume", 0x17, 0x4, HDA_INPUT),
	HDA_CODEC_MUTE("Mixer-5 Switch", 0x17, 0x4, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Input Source",
		.info = conexant_mux_enum_info,
		.get = conexant_mux_enum_get,
		.put = conexant_mux_enum_put,
	},
	/* Audio input controls */
	HDA_CODEC_VOLUME("Input-1 Volume", 0x1a, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Input-1 Switch", 0x1a, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Input-2 Volume", 0x1a, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Input-2 Switch", 0x1a, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Input-3 Volume", 0x1a, 0x2, HDA_INPUT),
	HDA_CODEC_MUTE("Input-3 Switch", 0x1a, 0x2, HDA_INPUT),
	HDA_CODEC_VOLUME("Input-4 Volume", 0x1a, 0x3, HDA_INPUT),
	HDA_CODEC_MUTE("Input-4 Switch", 0x1a, 0x3, HDA_INPUT),
	HDA_CODEC_VOLUME("Input-5 Volume", 0x1a, 0x4, HDA_INPUT),
	HDA_CODEC_MUTE("Input-5 Switch", 0x1a, 0x4, HDA_INPUT),
	{ } /* end */
};

static struct hda_verb cxt5045_test_init_verbs[] = {
	/* Set connections */
	{ 0x10, AC_VERB_SET_CONNECT_SEL, 0x0 },
	{ 0x11, AC_VERB_SET_CONNECT_SEL, 0x0 },
	{ 0x12, AC_VERB_SET_CONNECT_SEL, 0x0 },
	/* Enable retasking pins as output, initially without power amp */
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},

	/* Disable digital (SPDIF) pins initially, but users can enable
	 * them via a mixer switch.  In the case of SPDIF-out, this initverb
	 * payload also sets the generation to 0, output to be in "consumer"
	 * PCM format, copyright asserted, no pre-emphasis and no validity
	 * control.
	 */
	{0x13, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x18, AC_VERB_SET_DIGI_CONVERT_1, 0},

	/* Start with output sum widgets muted and their output gains at min */
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},

	/* Unmute retasking pin widget output buffers since the default
	 * state appears to be output.  As the pin mode is changed by the
	 * user the pin mode control will take care of enabling the pin's
	 * input/output buffers as needed.
	 */
	{0x12, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x11, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	/* Mute capture amp left and right */
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},

	/* Set ADC connection select to match default mixer setting (mic1
	 * pin)
	 */
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x17, AC_VERB_SET_CONNECT_SEL, 0x00},

	/* Mute all inputs to mixer widget (even unconnected ones) */
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)}, /* Mixer pin */
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)}, /* Mic1 pin */
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)}, /* Line pin */
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)}, /* HP pin */
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)}, /* CD pin */

	{ }
};
#endif


/* initialize jack-sensing, too */
static int cxt5045_init(struct hda_codec *codec)
{
	conexant_init(codec);
	cxt5045_hp_automute(codec);
	return 0;
}


enum {
	CXT5045_LAPTOP_HPSENSE,
	CXT5045_LAPTOP_MICSENSE,
	CXT5045_LAPTOP_HPMICSENSE,
	CXT5045_BENQ,
	CXT5045_LAPTOP_HP530,
#ifdef CONFIG_SND_DEBUG
	CXT5045_TEST,
#endif
	CXT5045_MODELS
};

static const char *cxt5045_models[CXT5045_MODELS] = {
	[CXT5045_LAPTOP_HPSENSE]	= "laptop-hpsense",
	[CXT5045_LAPTOP_MICSENSE]	= "laptop-micsense",
	[CXT5045_LAPTOP_HPMICSENSE]	= "laptop-hpmicsense",
	[CXT5045_BENQ]			= "benq",
	[CXT5045_LAPTOP_HP530]		= "laptop-hp530",
#ifdef CONFIG_SND_DEBUG
	[CXT5045_TEST]		= "test",
#endif
};

static struct snd_pci_quirk cxt5045_cfg_tbl[] = {
	SND_PCI_QUIRK(0x103c, 0x30a5, "HP", CXT5045_LAPTOP_HPSENSE),
	SND_PCI_QUIRK(0x103c, 0x30b2, "HP DV Series", CXT5045_LAPTOP_HPSENSE),
	SND_PCI_QUIRK(0x103c, 0x30b5, "HP DV2120", CXT5045_LAPTOP_HPSENSE),
	SND_PCI_QUIRK(0x103c, 0x30b7, "HP DV6000Z", CXT5045_LAPTOP_HPSENSE),
	SND_PCI_QUIRK(0x103c, 0x30bb, "HP DV8000", CXT5045_LAPTOP_HPSENSE),
	SND_PCI_QUIRK(0x103c, 0x30cd, "HP DV Series", CXT5045_LAPTOP_HPSENSE),
	SND_PCI_QUIRK(0x103c, 0x30cf, "HP DV9533EG", CXT5045_LAPTOP_HPSENSE),
	SND_PCI_QUIRK(0x103c, 0x30d5, "HP 530", CXT5045_LAPTOP_HP530),
	SND_PCI_QUIRK(0x103c, 0x30d9, "HP Spartan", CXT5045_LAPTOP_HPSENSE),
	SND_PCI_QUIRK(0x1179, 0xff31, "Toshiba P105", CXT5045_LAPTOP_MICSENSE),
	SND_PCI_QUIRK(0x152d, 0x0753, "Benq R55E", CXT5045_BENQ),
	SND_PCI_QUIRK(0x1734, 0x10ad, "Fujitsu Si1520", CXT5045_LAPTOP_MICSENSE),
	SND_PCI_QUIRK(0x1734, 0x10cb, "Fujitsu Si3515", CXT5045_LAPTOP_HPMICSENSE),
	SND_PCI_QUIRK(0x1734, 0x110e, "Fujitsu V5505",
		      CXT5045_LAPTOP_HPMICSENSE),
	SND_PCI_QUIRK(0x1509, 0x1e40, "FIC", CXT5045_LAPTOP_HPMICSENSE),
	SND_PCI_QUIRK(0x1509, 0x2f05, "FIC", CXT5045_LAPTOP_HPMICSENSE),
	SND_PCI_QUIRK(0x1509, 0x2f06, "FIC", CXT5045_LAPTOP_HPMICSENSE),
	SND_PCI_QUIRK(0x1631, 0xc106, "Packard Bell", CXT5045_LAPTOP_HPMICSENSE),
	SND_PCI_QUIRK(0x1631, 0xc107, "Packard Bell", CXT5045_LAPTOP_HPMICSENSE),
	SND_PCI_QUIRK(0x8086, 0x2111, "Conexant Reference board", CXT5045_LAPTOP_HPSENSE),
	{}
};

static int patch_cxt5045(struct hda_codec *codec)
{
	struct conexant_spec *spec;
	int board_config;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;
	codec->spec = spec;

	spec->multiout.max_channels = 2;
	spec->multiout.num_dacs = ARRAY_SIZE(cxt5045_dac_nids);
	spec->multiout.dac_nids = cxt5045_dac_nids;
	spec->multiout.dig_out_nid = CXT5045_SPDIF_OUT;
	spec->num_adc_nids = 1;
	spec->adc_nids = cxt5045_adc_nids;
	spec->capsrc_nids = cxt5045_capsrc_nids;
	spec->input_mux = &cxt5045_capture_source;
	spec->num_mixers = 1;
	spec->mixers[0] = cxt5045_mixers;
	spec->num_init_verbs = 1;
	spec->init_verbs[0] = cxt5045_init_verbs;
	spec->spdif_route = 0;
	spec->num_channel_mode = ARRAY_SIZE(cxt5045_modes),
	spec->channel_mode = cxt5045_modes,


	codec->patch_ops = conexant_patch_ops;

	board_config = snd_hda_check_board_config(codec, CXT5045_MODELS,
						  cxt5045_models,
						  cxt5045_cfg_tbl);
	switch (board_config) {
	case CXT5045_LAPTOP_HPSENSE:
		codec->patch_ops.unsol_event = cxt5045_hp_unsol_event;
		spec->input_mux = &cxt5045_capture_source;
		spec->num_init_verbs = 2;
		spec->init_verbs[1] = cxt5045_hp_sense_init_verbs;
		spec->mixers[0] = cxt5045_mixers;
		codec->patch_ops.init = cxt5045_init;
		break;
	case CXT5045_LAPTOP_MICSENSE:
		codec->patch_ops.unsol_event = cxt5045_hp_unsol_event;
		spec->input_mux = &cxt5045_capture_source;
		spec->num_init_verbs = 2;
		spec->init_verbs[1] = cxt5045_mic_sense_init_verbs;
		spec->mixers[0] = cxt5045_mixers;
		codec->patch_ops.init = cxt5045_init;
		break;
	default:
	case CXT5045_LAPTOP_HPMICSENSE:
		codec->patch_ops.unsol_event = cxt5045_hp_unsol_event;
		spec->input_mux = &cxt5045_capture_source;
		spec->num_init_verbs = 3;
		spec->init_verbs[1] = cxt5045_hp_sense_init_verbs;
		spec->init_verbs[2] = cxt5045_mic_sense_init_verbs;
		spec->mixers[0] = cxt5045_mixers;
		codec->patch_ops.init = cxt5045_init;
		break;
	case CXT5045_BENQ:
		codec->patch_ops.unsol_event = cxt5045_hp_unsol_event;
		spec->input_mux = &cxt5045_capture_source_benq;
		spec->num_init_verbs = 1;
		spec->init_verbs[0] = cxt5045_benq_init_verbs;
		spec->mixers[0] = cxt5045_mixers;
		spec->mixers[1] = cxt5045_benq_mixers;
		spec->num_mixers = 2;
		codec->patch_ops.init = cxt5045_init;
		break;
	case CXT5045_LAPTOP_HP530:
		codec->patch_ops.unsol_event = cxt5045_hp_unsol_event;
		spec->input_mux = &cxt5045_capture_source_hp530;
		spec->num_init_verbs = 2;
		spec->init_verbs[1] = cxt5045_hp_sense_init_verbs;
		spec->mixers[0] = cxt5045_mixers_hp530;
		codec->patch_ops.init = cxt5045_init;
		break;
#ifdef CONFIG_SND_DEBUG
	case CXT5045_TEST:
		spec->input_mux = &cxt5045_test_capture_source;
		spec->mixers[0] = cxt5045_test_mixer;
		spec->init_verbs[0] = cxt5045_test_init_verbs;
		break;
		
#endif	
	}

	switch (codec->subsystem_id >> 16) {
	case 0x103c:
		/* HP laptop has a really bad sound over 0dB on NID 0x17.
		 * Fix max PCM level to 0 dB
		 * (originall it has 0x2b steps with 0dB offset 0x14)
		 */
		snd_hda_override_amp_caps(codec, 0x17, HDA_INPUT,
					  (0x14 << AC_AMPCAP_OFFSET_SHIFT) |
					  (0x14 << AC_AMPCAP_NUM_STEPS_SHIFT) |
					  (0x05 << AC_AMPCAP_STEP_SIZE_SHIFT) |
					  (1 << AC_AMPCAP_MUTE_SHIFT));
		break;
	}

	return 0;
}


/* Conexant 5047 specific */
#define CXT5047_SPDIF_OUT	0x11

static hda_nid_t cxt5047_dac_nids[2] = { 0x10, 0x1c };
static hda_nid_t cxt5047_adc_nids[1] = { 0x12 };
static hda_nid_t cxt5047_capsrc_nids[1] = { 0x1a };

static struct hda_channel_mode cxt5047_modes[1] = {
	{ 2, NULL },
};

static struct hda_input_mux cxt5047_capture_source = {
	.num_items = 1,
	.items = {
		{ "Mic", 0x2 },
	}
};

static struct hda_input_mux cxt5047_hp_capture_source = {
	.num_items = 1,
	.items = {
		{ "ExtMic", 0x2 },
	}
};

static struct hda_input_mux cxt5047_toshiba_capture_source = {
	.num_items = 2,
	.items = {
		{ "ExtMic", 0x2 },
		{ "Line-In", 0x1 },
	}
};

/* turn on/off EAPD (+ mute HP) as a master switch */
static int cxt5047_hp_master_sw_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct conexant_spec *spec = codec->spec;
	unsigned int bits;

	if (!cxt_eapd_put(kcontrol, ucontrol))
		return 0;

	/* toggle internal speakers mute depending of presence of
	 * the headphone jack
	 */
	bits = (!spec->hp_present && spec->cur_eapd) ? 0 : HDA_AMP_MUTE;
	snd_hda_codec_amp_stereo(codec, 0x1d, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
	bits = spec->cur_eapd ? 0 : HDA_AMP_MUTE;
	snd_hda_codec_amp_stereo(codec, 0x13, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
	return 1;
}

/* bind volumes of both NID 0x13 (Headphones) and 0x1d (Speakers) */
static struct hda_bind_ctls cxt5047_bind_master_vol = {
	.ops = &snd_hda_bind_vol,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x13, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x1d, 3, 0, HDA_OUTPUT),
		0
	},
};

/* mute internal speaker if HP is plugged */
static void cxt5047_hp_automute(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;
	unsigned int bits;

	spec->hp_present = snd_hda_codec_read(codec, 0x13, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;

	bits = (spec->hp_present || !spec->cur_eapd) ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x1d, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
	/* Mute/Unmute PCM 2 for good measure - some systems need this */
	snd_hda_codec_amp_stereo(codec, 0x1c, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
}

/* mute internal speaker if HP is plugged */
static void cxt5047_hp2_automute(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;
	unsigned int bits;

	spec->hp_present = snd_hda_codec_read(codec, 0x13, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;

	bits = spec->hp_present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x1d, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
	/* Mute/Unmute PCM 2 for good measure - some systems need this */
	snd_hda_codec_amp_stereo(codec, 0x1c, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
}

/* toggle input of built-in and mic jack appropriately */
static void cxt5047_hp_automic(struct hda_codec *codec)
{
	static struct hda_verb mic_jack_on[] = {
		{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
		{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
		{}
	};
	static struct hda_verb mic_jack_off[] = {
		{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
		{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
		{}
	};
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x15, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	if (present)
		snd_hda_sequence_write(codec, mic_jack_on);
	else
		snd_hda_sequence_write(codec, mic_jack_off);
}

/* unsolicited event for HP jack sensing */
static void cxt5047_hp_unsol_event(struct hda_codec *codec,
				  unsigned int res)
{
	switch (res >> 26) {
	case CONEXANT_HP_EVENT:
		cxt5047_hp_automute(codec);
		break;
	case CONEXANT_MIC_EVENT:
		cxt5047_hp_automic(codec);
		break;
	}
}

/* unsolicited event for HP jack sensing - non-EAPD systems */
static void cxt5047_hp2_unsol_event(struct hda_codec *codec,
				  unsigned int res)
{
	res >>= 26;
	switch (res) {
	case CONEXANT_HP_EVENT:
		cxt5047_hp2_automute(codec);
		break;
	case CONEXANT_MIC_EVENT:
		cxt5047_hp_automic(codec);
		break;
	}
}

static struct snd_kcontrol_new cxt5047_mixers[] = {
	HDA_CODEC_VOLUME("Mic Bypass Capture Volume", 0x19, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Bypass Capture Switch", 0x19, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Gain Volume", 0x1a, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Mic Gain Switch", 0x1a, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x12, 0x03, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x12, 0x03, HDA_INPUT),
	HDA_CODEC_VOLUME("PCM Volume", 0x10, 0x00, HDA_OUTPUT),
	HDA_CODEC_MUTE("PCM Switch", 0x10, 0x00, HDA_OUTPUT),
	HDA_CODEC_VOLUME("PCM-2 Volume", 0x1c, 0x00, HDA_OUTPUT),
	HDA_CODEC_MUTE("PCM-2 Switch", 0x1c, 0x00, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x1d, 0x00, HDA_OUTPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x1d, 0x00, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x13, 0x00, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x13, 0x00, HDA_OUTPUT),

	{}
};

static struct snd_kcontrol_new cxt5047_toshiba_mixers[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.info = conexant_mux_enum_info,
		.get = conexant_mux_enum_get,
		.put = conexant_mux_enum_put
	},
	HDA_CODEC_VOLUME("Mic Bypass Capture Volume", 0x19, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Bypass Capture Switch", 0x19, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x12, 0x03, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x12, 0x03, HDA_INPUT),
	HDA_CODEC_VOLUME("PCM Volume", 0x10, 0x00, HDA_OUTPUT),
	HDA_CODEC_MUTE("PCM Switch", 0x10, 0x00, HDA_OUTPUT),
	HDA_BIND_VOL("Master Playback Volume", &cxt5047_bind_master_vol),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = cxt_eapd_info,
		.get = cxt_eapd_get,
		.put = cxt5047_hp_master_sw_put,
		.private_value = 0x13,
	},

	{}
};

static struct snd_kcontrol_new cxt5047_hp_mixers[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.info = conexant_mux_enum_info,
		.get = conexant_mux_enum_get,
		.put = conexant_mux_enum_put
	},
	HDA_CODEC_VOLUME("Mic Bypass Capture Volume", 0x19, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Bypass Capture Switch", 0x19,0x02,HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x12, 0x03, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x12, 0x03, HDA_INPUT),
	HDA_CODEC_VOLUME("PCM Volume", 0x10, 0x00, HDA_OUTPUT),
	HDA_CODEC_MUTE("PCM Switch", 0x10, 0x00, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Master Playback Volume", 0x13, 0x00, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = cxt_eapd_info,
		.get = cxt_eapd_get,
		.put = cxt5047_hp_master_sw_put,
		.private_value = 0x13,
	},
	{ } /* end */
};

static struct hda_verb cxt5047_init_verbs[] = {
	/* Line in, Mic, Built-in Mic */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN|AC_PINCTL_VREF_50 },
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN|AC_PINCTL_VREF_50 },
	/* HP, Speaker  */
	{0x13, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP },
	{0x13, AC_VERB_SET_CONNECT_SEL,0x1},
	{0x1d, AC_VERB_SET_CONNECT_SEL,0x0},
	/* Record selector: Mic */
	{0x12, AC_VERB_SET_CONNECT_SEL,0x03},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE,
	 AC_AMP_SET_INPUT|AC_AMP_SET_RIGHT|AC_AMP_SET_LEFT|0x17},
	{0x1A, AC_VERB_SET_CONNECT_SEL,0x02},
	{0x1A, AC_VERB_SET_AMP_GAIN_MUTE,
	 AC_AMP_SET_OUTPUT|AC_AMP_SET_RIGHT|AC_AMP_SET_LEFT|0x00},
	{0x1A, AC_VERB_SET_AMP_GAIN_MUTE,
	 AC_AMP_SET_OUTPUT|AC_AMP_SET_RIGHT|AC_AMP_SET_LEFT|0x03},
	/* SPDIF route: PCM */
	{ 0x18, AC_VERB_SET_CONNECT_SEL, 0x0 },
	/* Enable unsolicited events */
	{0x13, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | CONEXANT_HP_EVENT},
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | CONEXANT_MIC_EVENT},
	{ } /* end */
};

/* configuration for Toshiba Laptops */
static struct hda_verb cxt5047_toshiba_init_verbs[] = {
	{0x13, AC_VERB_SET_EAPD_BTLENABLE, 0x0 }, /* default on */
	/* pin sensing on HP and Mic jacks */
	{0x13, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | CONEXANT_HP_EVENT},
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | CONEXANT_MIC_EVENT},
	/* Speaker routing */
	{0x1d, AC_VERB_SET_CONNECT_SEL,0x1},
	{}
};

/* configuration for HP Laptops */
static struct hda_verb cxt5047_hp_init_verbs[] = {
	/* pin sensing on HP jack */
	{0x13, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | CONEXANT_HP_EVENT},
	/* 0x13 is actually shared by both HP and speaker;
	 * setting the connection to 0 (=0x19) makes the master volume control
	 * working mysteriouslly...
	 */
	{0x13, AC_VERB_SET_CONNECT_SEL, 0x0},
	/* Record selector: Ext Mic */
	{0x12, AC_VERB_SET_CONNECT_SEL,0x03},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE,
	 AC_AMP_SET_INPUT|AC_AMP_SET_RIGHT|AC_AMP_SET_LEFT|0x17},
	/* Speaker routing */
	{0x1d, AC_VERB_SET_CONNECT_SEL,0x1},
	{}
};

/* Test configuration for debugging, modelled after the ALC260 test
 * configuration.
 */
#ifdef CONFIG_SND_DEBUG
static struct hda_input_mux cxt5047_test_capture_source = {
	.num_items = 4,
	.items = {
		{ "LINE1 pin", 0x0 },
		{ "MIC1 pin", 0x1 },
		{ "MIC2 pin", 0x2 },
		{ "CD pin", 0x3 },
        },
};

static struct snd_kcontrol_new cxt5047_test_mixer[] = {

	/* Output only controls */
	HDA_CODEC_VOLUME("OutAmp-1 Volume", 0x10, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("OutAmp-1 Switch", 0x10,0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("OutAmp-2 Volume", 0x1c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("OutAmp-2 Switch", 0x1c, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x1d, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x1d, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("HeadPhone Playback Volume", 0x13, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("HeadPhone Playback Switch", 0x13, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line1-Out Playback Volume", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Line1-Out Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line2-Out Playback Volume", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Line2-Out Playback Switch", 0x15, 0x0, HDA_OUTPUT),

	/* Modes for retasking pin widgets */
	CXT_PIN_MODE("LINE1 pin mode", 0x14, CXT_PIN_DIR_INOUT),
	CXT_PIN_MODE("MIC1 pin mode", 0x15, CXT_PIN_DIR_INOUT),

	/* EAPD Switch Control */
	CXT_EAPD_SWITCH("External Amplifier", 0x13, 0x0),

	/* Loopback mixer controls */
	HDA_CODEC_VOLUME("MIC1 Playback Volume", 0x12, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("MIC1 Playback Switch", 0x12, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("MIC2 Playback Volume", 0x12, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("MIC2 Playback Switch", 0x12, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("LINE Playback Volume", 0x12, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("LINE Playback Switch", 0x12, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x12, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x12, 0x04, HDA_INPUT),

	HDA_CODEC_VOLUME("Capture-1 Volume", 0x19, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture-1 Switch", 0x19, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture-2 Volume", 0x19, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Capture-2 Switch", 0x19, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture-3 Volume", 0x19, 0x2, HDA_INPUT),
	HDA_CODEC_MUTE("Capture-3 Switch", 0x19, 0x2, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture-4 Volume", 0x19, 0x3, HDA_INPUT),
	HDA_CODEC_MUTE("Capture-4 Switch", 0x19, 0x3, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Input Source",
		.info = conexant_mux_enum_info,
		.get = conexant_mux_enum_get,
		.put = conexant_mux_enum_put,
	},
	HDA_CODEC_VOLUME("Input-1 Volume", 0x1a, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Input-1 Switch", 0x1a, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Input-2 Volume", 0x1a, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Input-2 Switch", 0x1a, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Input-3 Volume", 0x1a, 0x2, HDA_INPUT),
	HDA_CODEC_MUTE("Input-3 Switch", 0x1a, 0x2, HDA_INPUT),
	HDA_CODEC_VOLUME("Input-4 Volume", 0x1a, 0x3, HDA_INPUT),
	HDA_CODEC_MUTE("Input-4 Switch", 0x1a, 0x3, HDA_INPUT),
	HDA_CODEC_VOLUME("Input-5 Volume", 0x1a, 0x4, HDA_INPUT),
	HDA_CODEC_MUTE("Input-5 Switch", 0x1a, 0x4, HDA_INPUT),

	{ } /* end */
};

static struct hda_verb cxt5047_test_init_verbs[] = {
	/* Enable retasking pins as output, initially without power amp */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x13, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},

	/* Disable digital (SPDIF) pins initially, but users can enable
	 * them via a mixer switch.  In the case of SPDIF-out, this initverb
	 * payload also sets the generation to 0, output to be in "consumer"
	 * PCM format, copyright asserted, no pre-emphasis and no validity
	 * control.
	 */
	{0x18, AC_VERB_SET_DIGI_CONVERT_1, 0},

	/* Ensure mic1, mic2, line1 pin widgets take input from the 
	 * OUT1 sum bus when acting as an output.
	 */
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0},
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0},

	/* Start with output sum widgets muted and their output gains at min */
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},

	/* Unmute retasking pin widget output buffers since the default
	 * state appears to be output.  As the pin mode is changed by the
	 * user the pin mode control will take care of enabling the pin's
	 * input/output buffers as needed.
	 */
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x13, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	/* Mute capture amp left and right */
	{0x12, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},

	/* Set ADC connection select to match default mixer setting (mic1
	 * pin)
	 */
	{0x12, AC_VERB_SET_CONNECT_SEL, 0x00},

	/* Mute all inputs to mixer widget (even unconnected ones) */
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)}, /* mic1 pin */
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)}, /* mic2 pin */
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)}, /* line1 pin */
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)}, /* line2 pin */
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)}, /* CD pin */
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(5)}, /* Beep-gen pin */
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(6)}, /* Line-out pin */
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(7)}, /* HP-pin pin */

	{ }
};
#endif


/* initialize jack-sensing, too */
static int cxt5047_hp_init(struct hda_codec *codec)
{
	conexant_init(codec);
	cxt5047_hp_automute(codec);
	return 0;
}


enum {
	CXT5047_LAPTOP,		/* Laptops w/o EAPD support */
	CXT5047_LAPTOP_HP,	/* Some HP laptops */
	CXT5047_LAPTOP_EAPD,	/* Laptops with EAPD support */
#ifdef CONFIG_SND_DEBUG
	CXT5047_TEST,
#endif
	CXT5047_MODELS
};

static const char *cxt5047_models[CXT5047_MODELS] = {
	[CXT5047_LAPTOP]	= "laptop",
	[CXT5047_LAPTOP_HP]	= "laptop-hp",
	[CXT5047_LAPTOP_EAPD]	= "laptop-eapd",
#ifdef CONFIG_SND_DEBUG
	[CXT5047_TEST]		= "test",
#endif
};

static struct snd_pci_quirk cxt5047_cfg_tbl[] = {
	SND_PCI_QUIRK(0x103c, 0x30a0, "HP DV1000", CXT5047_LAPTOP),
	SND_PCI_QUIRK(0x103c, 0x30a5, "HP DV5200T/DV8000T", CXT5047_LAPTOP_HP),
	SND_PCI_QUIRK(0x103c, 0x30b2, "HP DV2000T/DV3000T", CXT5047_LAPTOP),
	SND_PCI_QUIRK(0x103c, 0x30b5, "HP DV2000Z", CXT5047_LAPTOP),
	SND_PCI_QUIRK(0x1179, 0xff31, "Toshiba P100", CXT5047_LAPTOP_EAPD),
	{}
};

static int patch_cxt5047(struct hda_codec *codec)
{
	struct conexant_spec *spec;
	int board_config;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;
	codec->spec = spec;

	spec->multiout.max_channels = 2;
	spec->multiout.num_dacs = ARRAY_SIZE(cxt5047_dac_nids);
	spec->multiout.dac_nids = cxt5047_dac_nids;
	spec->multiout.dig_out_nid = CXT5047_SPDIF_OUT;
	spec->num_adc_nids = 1;
	spec->adc_nids = cxt5047_adc_nids;
	spec->capsrc_nids = cxt5047_capsrc_nids;
	spec->input_mux = &cxt5047_capture_source;
	spec->num_mixers = 1;
	spec->mixers[0] = cxt5047_mixers;
	spec->num_init_verbs = 1;
	spec->init_verbs[0] = cxt5047_init_verbs;
	spec->spdif_route = 0;
	spec->num_channel_mode = ARRAY_SIZE(cxt5047_modes),
	spec->channel_mode = cxt5047_modes,

	codec->patch_ops = conexant_patch_ops;

	board_config = snd_hda_check_board_config(codec, CXT5047_MODELS,
						  cxt5047_models,
						  cxt5047_cfg_tbl);
	switch (board_config) {
	case CXT5047_LAPTOP:
		codec->patch_ops.unsol_event = cxt5047_hp2_unsol_event;
		break;
	case CXT5047_LAPTOP_HP:
		spec->input_mux = &cxt5047_hp_capture_source;
		spec->num_init_verbs = 2;
		spec->init_verbs[1] = cxt5047_hp_init_verbs;
		spec->mixers[0] = cxt5047_hp_mixers;
		codec->patch_ops.unsol_event = cxt5047_hp_unsol_event;
		codec->patch_ops.init = cxt5047_hp_init;
		break;
	case CXT5047_LAPTOP_EAPD:
		spec->input_mux = &cxt5047_toshiba_capture_source;
		spec->num_init_verbs = 2;
		spec->init_verbs[1] = cxt5047_toshiba_init_verbs;
		spec->mixers[0] = cxt5047_toshiba_mixers;
		codec->patch_ops.unsol_event = cxt5047_hp_unsol_event;
		break;
#ifdef CONFIG_SND_DEBUG
	case CXT5047_TEST:
		spec->input_mux = &cxt5047_test_capture_source;
		spec->mixers[0] = cxt5047_test_mixer;
		spec->init_verbs[0] = cxt5047_test_init_verbs;
		codec->patch_ops.unsol_event = cxt5047_hp_unsol_event;
#endif	
	}
	return 0;
}

/* Conexant 5051 specific */
static hda_nid_t cxt5051_dac_nids[1] = { 0x10 };
static hda_nid_t cxt5051_adc_nids[2] = { 0x14, 0x15 };
#define CXT5051_SPDIF_OUT	0x1C
#define CXT5051_PORTB_EVENT	0x38
#define CXT5051_PORTC_EVENT	0x39

static struct hda_channel_mode cxt5051_modes[1] = {
	{ 2, NULL },
};

static void cxt5051_update_speaker(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;
	unsigned int pinctl;
	pinctl = (!spec->hp_present && spec->cur_eapd) ? PIN_OUT : 0;
	snd_hda_codec_write(codec, 0x1a, 0, AC_VERB_SET_PIN_WIDGET_CONTROL,
			    pinctl);
}

/* turn on/off EAPD (+ mute HP) as a master switch */
static int cxt5051_hp_master_sw_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);

	if (!cxt_eapd_put(kcontrol, ucontrol))
		return 0;
	cxt5051_update_speaker(codec);
	return 1;
}

/* toggle input of built-in and mic jack appropriately */
static void cxt5051_portb_automic(struct hda_codec *codec)
{
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x17, 0,
				     AC_VERB_GET_PIN_SENSE, 0) &
		AC_PINSENSE_PRESENCE;
	snd_hda_codec_write(codec, 0x14, 0,
			    AC_VERB_SET_CONNECT_SEL,
			    present ? 0x01 : 0x00);
}

/* switch the current ADC according to the jack state */
static void cxt5051_portc_automic(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;
	unsigned int present;
	hda_nid_t new_adc;

	present = snd_hda_codec_read(codec, 0x18, 0,
				     AC_VERB_GET_PIN_SENSE, 0) &
		AC_PINSENSE_PRESENCE;
	if (present)
		spec->cur_adc_idx = 1;
	else
		spec->cur_adc_idx = 0;
	new_adc = spec->adc_nids[spec->cur_adc_idx];
	if (spec->cur_adc && spec->cur_adc != new_adc) {
		/* stream is running, let's swap the current ADC */
		snd_hda_codec_cleanup_stream(codec, spec->cur_adc);
		spec->cur_adc = new_adc;
		snd_hda_codec_setup_stream(codec, new_adc,
					   spec->cur_adc_stream_tag, 0,
					   spec->cur_adc_format);
	}
}

/* mute internal speaker if HP is plugged */
static void cxt5051_hp_automute(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;

	spec->hp_present = snd_hda_codec_read(codec, 0x16, 0,
				     AC_VERB_GET_PIN_SENSE, 0) &
		AC_PINSENSE_PRESENCE;
	cxt5051_update_speaker(codec);
}

/* unsolicited event for HP jack sensing */
static void cxt5051_hp_unsol_event(struct hda_codec *codec,
				   unsigned int res)
{
	switch (res >> 26) {
	case CONEXANT_HP_EVENT:
		cxt5051_hp_automute(codec);
		break;
	case CXT5051_PORTB_EVENT:
		cxt5051_portb_automic(codec);
		break;
	case CXT5051_PORTC_EVENT:
		cxt5051_portc_automic(codec);
		break;
	}
}

static struct snd_kcontrol_new cxt5051_mixers[] = {
	HDA_CODEC_VOLUME("Internal Mic Volume", 0x14, 0x00, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Switch", 0x14, 0x00, HDA_INPUT),
	HDA_CODEC_VOLUME("External Mic Volume", 0x14, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("External Mic Switch", 0x14, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("Docking Mic Volume", 0x15, 0x00, HDA_INPUT),
	HDA_CODEC_MUTE("Docking Mic Switch", 0x15, 0x00, HDA_INPUT),
	HDA_CODEC_VOLUME("Master Playback Volume", 0x10, 0x00, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = cxt_eapd_info,
		.get = cxt_eapd_get,
		.put = cxt5051_hp_master_sw_put,
		.private_value = 0x1a,
	},

	{}
};

static struct snd_kcontrol_new cxt5051_hp_mixers[] = {
	HDA_CODEC_VOLUME("Internal Mic Volume", 0x14, 0x00, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Switch", 0x14, 0x00, HDA_INPUT),
	HDA_CODEC_VOLUME("External Mic Volume", 0x15, 0x00, HDA_INPUT),
	HDA_CODEC_MUTE("External Mic Switch", 0x15, 0x00, HDA_INPUT),
	HDA_CODEC_VOLUME("Master Playback Volume", 0x10, 0x00, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = cxt_eapd_info,
		.get = cxt_eapd_get,
		.put = cxt5051_hp_master_sw_put,
		.private_value = 0x1a,
	},

	{}
};

static struct hda_verb cxt5051_init_verbs[] = {
	/* Line in, Mic */
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0) | 0x03},
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0) | 0x03},
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x1d, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0) | 0x03},
	/* SPK  */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* HP, Amp  */
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x16, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* DAC1 */	
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Record selector: Int mic */
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0) | 0x44},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1) | 0x44},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0) | 0x44},
	/* SPDIF route: PCM */
	{0x1c, AC_VERB_SET_CONNECT_SEL, 0x0},
	/* EAPD */
	{0x1a, AC_VERB_SET_EAPD_BTLENABLE, 0x2}, /* default on */ 
	{0x16, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN|CONEXANT_HP_EVENT},
	{0x17, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN|CXT5051_PORTB_EVENT},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN|CXT5051_PORTC_EVENT},
	{ } /* end */
};

/* initialize jack-sensing, too */
static int cxt5051_init(struct hda_codec *codec)
{
	conexant_init(codec);
	if (codec->patch_ops.unsol_event) {
		cxt5051_hp_automute(codec);
		cxt5051_portb_automic(codec);
		cxt5051_portc_automic(codec);
	}
	return 0;
}


enum {
	CXT5051_LAPTOP,	 /* Laptops w/ EAPD support */
	CXT5051_HP,	/* no docking */
	CXT5051_MODELS
};

static const char *cxt5051_models[CXT5051_MODELS] = {
	[CXT5051_LAPTOP]	= "laptop",
	[CXT5051_HP]		= "hp",
};

static struct snd_pci_quirk cxt5051_cfg_tbl[] = {
	SND_PCI_QUIRK(0x14f1, 0x0101, "Conexant Reference board",
		      CXT5051_LAPTOP),
	SND_PCI_QUIRK(0x14f1, 0x5051, "HP Spartan 1.1", CXT5051_HP),
	{}
};

static int patch_cxt5051(struct hda_codec *codec)
{
	struct conexant_spec *spec;
	int board_config;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;
	codec->spec = spec;

	codec->patch_ops = conexant_patch_ops;
	codec->patch_ops.init = cxt5051_init;

	spec->multiout.max_channels = 2;
	spec->multiout.num_dacs = ARRAY_SIZE(cxt5051_dac_nids);
	spec->multiout.dac_nids = cxt5051_dac_nids;
	spec->multiout.dig_out_nid = CXT5051_SPDIF_OUT;
	spec->num_adc_nids = 1; /* not 2; via auto-mic switch */
	spec->adc_nids = cxt5051_adc_nids;
	spec->num_mixers = 1;
	spec->mixers[0] = cxt5051_mixers;
	spec->num_init_verbs = 1;
	spec->init_verbs[0] = cxt5051_init_verbs;
	spec->spdif_route = 0;
	spec->num_channel_mode = ARRAY_SIZE(cxt5051_modes);
	spec->channel_mode = cxt5051_modes;
	spec->cur_adc = 0;
	spec->cur_adc_idx = 0;

	board_config = snd_hda_check_board_config(codec, CXT5051_MODELS,
						  cxt5051_models,
						  cxt5051_cfg_tbl);
	switch (board_config) {
	case CXT5051_HP:
		codec->patch_ops.unsol_event = cxt5051_hp_unsol_event;
		spec->mixers[0] = cxt5051_hp_mixers;
		break;
	default:
	case CXT5051_LAPTOP:
		codec->patch_ops.unsol_event = cxt5051_hp_unsol_event;
		break;
	}

	return 0;
}


/*
 */

struct hda_codec_preset snd_hda_preset_conexant[] = {
	{ .id = 0x14f15045, .name = "CX20549 (Venice)",
	  .patch = patch_cxt5045 },
	{ .id = 0x14f15047, .name = "CX20551 (Waikiki)",
	  .patch = patch_cxt5047 },
	{ .id = 0x14f15051, .name = "CX20561 (Hermosa)",
	  .patch = patch_cxt5051 },
	{} /* terminator */
};
	if (action != HDA_FIXUP_ACT_PROBE)
		return;

	spec->gen.mic_autoswitch_hook = olpc_xo_automic;
	spec->gen.pcm_capture_hook = olpc_xo_capture_hook;
	spec->dc_mode_path = snd_hda_add_new_path(codec, 0x1e, 0x14, 0);

	snd_hda_add_new_ctls(codec, olpc_xo_mixers);

	/* OLPC's microphone port is DC coupled for use with external sensors,
	 * therefore we use a 50% mic bias in order to center the input signal
	 * with the DC input range of the codec.
	 */
	snd_hda_codec_set_pin_target(codec, 0x1a, PIN_VREF50);

	/* override mic boost control */
	for (i = 0; i < spec->gen.kctls.used; i++) {
		struct snd_kcontrol_new *kctl =
			snd_array_elem(&spec->gen.kctls, i);
		if (!strcmp(kctl->name, "Mic Boost Volume")) {
			kctl->put = olpc_xo_mic_boost_put;
			break;
		}
	}
}

static void cxt_fixup_mute_led_eapd(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	struct conexant_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->mute_led_eapd = 0x1b;
		spec->dynamic_eapd = 1;
		spec->gen.vmaster_mute.hook = cx_auto_vmaster_hook_mute_led;
	}
}

/*
 * Fix max input level on mixer widget to 0dB
 * (originally it has 0x2b steps with 0dB offset 0x14)
 */
static void cxt_fixup_cap_mix_amp(struct hda_codec *codec,
				  const struct hda_fixup *fix, int action)
{
	snd_hda_override_amp_caps(codec, 0x17, HDA_INPUT,
				  (0x14 << AC_AMPCAP_OFFSET_SHIFT) |
				  (0x14 << AC_AMPCAP_NUM_STEPS_SHIFT) |
				  (0x05 << AC_AMPCAP_STEP_SIZE_SHIFT) |
				  (1 << AC_AMPCAP_MUTE_SHIFT));
}

/*
 * Fix max input level on mixer widget to 0dB
 * (originally it has 0x1e steps with 0 dB offset 0x17)
 */
static void cxt_fixup_cap_mix_amp_5047(struct hda_codec *codec,
				  const struct hda_fixup *fix, int action)
{
	snd_hda_override_amp_caps(codec, 0x10, HDA_INPUT,
				  (0x17 << AC_AMPCAP_OFFSET_SHIFT) |
				  (0x17 << AC_AMPCAP_NUM_STEPS_SHIFT) |
				  (0x05 << AC_AMPCAP_STEP_SIZE_SHIFT) |
				  (1 << AC_AMPCAP_MUTE_SHIFT));
}

static void cxt_fixup_hp_gate_mic_jack(struct hda_codec *codec,
				       const struct hda_fixup *fix,
				       int action)
{
	/* the mic pin (0x19) doesn't give an unsolicited event;
	 * probe the mic pin together with the headphone pin (0x16)
	 */
	if (action == HDA_FIXUP_ACT_PROBE)
		snd_hda_jack_set_gating_jack(codec, 0x19, 0x16);
}

/* ThinkPad X200 & co with cxt5051 */
static const struct hda_pintbl cxt_pincfg_lenovo_x200[] = {
	{ 0x16, 0x042140ff }, /* HP (seq# overridden) */
	{ 0x17, 0x21a11000 }, /* dock-mic */
	{ 0x19, 0x2121103f }, /* dock-HP */
	{ 0x1c, 0x21440100 }, /* dock SPDIF out */
	{}
};

/* ThinkPad 410/420/510/520, X201 & co with cxt5066 */
static const struct hda_pintbl cxt_pincfg_lenovo_tp410[] = {
	{ 0x19, 0x042110ff }, /* HP (seq# overridden) */
	{ 0x1a, 0x21a190f0 }, /* dock-mic */
	{ 0x1c, 0x212140ff }, /* dock-HP */
	{}
};

/* Lemote A1004/A1205 with cxt5066 */
static const struct hda_pintbl cxt_pincfg_lemote[] = {
	{ 0x1a, 0x90a10020 }, /* Internal mic */
	{ 0x1b, 0x03a11020 }, /* External mic */
	{ 0x1d, 0x400101f0 }, /* Not used */
	{ 0x1e, 0x40a701f0 }, /* Not used */
	{ 0x20, 0x404501f0 }, /* Not used */
	{ 0x22, 0x404401f0 }, /* Not used */
	{ 0x23, 0x40a701f0 }, /* Not used */
	{}
};

static const struct hda_fixup cxt_fixups[] = {
	[CXT_PINCFG_LENOVO_X200] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = cxt_pincfg_lenovo_x200,
	},
	[CXT_PINCFG_LENOVO_TP410] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = cxt_pincfg_lenovo_tp410,
		.chained = true,
		.chain_id = CXT_FIXUP_THINKPAD_ACPI,
	},
	[CXT_PINCFG_LEMOTE_A1004] = {
		.type = HDA_FIXUP_PINS,
		.chained = true,
		.chain_id = CXT_FIXUP_INC_MIC_BOOST,
		.v.pins = cxt_pincfg_lemote,
	},
	[CXT_PINCFG_LEMOTE_A1205] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = cxt_pincfg_lemote,
	},
	[CXT_PINCFG_COMPAQ_CQ60] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			/* 0x17 was falsely set up as a mic, it should 0x1d */
			{ 0x17, 0x400001f0 },
			{ 0x1d, 0x97a70120 },
			{ }
		}
	},
	[CXT_FIXUP_STEREO_DMIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cxt_fixup_stereo_dmic,
	},
	[CXT_PINCFG_LENOVO_NOTEBOOK] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1a, 0x05d71030 },
			{ }
		},
		.chain_id = CXT_FIXUP_STEREO_DMIC,
	},
	[CXT_FIXUP_INC_MIC_BOOST] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cxt5066_increase_mic_boost,
	},
	[CXT_FIXUP_HEADPHONE_MIC_PIN] = {
		.type = HDA_FIXUP_PINS,
		.chained = true,
		.chain_id = CXT_FIXUP_HEADPHONE_MIC,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x18, 0x03a1913d }, /* use as headphone mic, without its own jack detect */
			{ }
		}
	},
	[CXT_FIXUP_HEADPHONE_MIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cxt_fixup_headphone_mic,
	},
	[CXT_FIXUP_GPIO1] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x01, AC_VERB_SET_GPIO_MASK, 0x01 },
			{ 0x01, AC_VERB_SET_GPIO_DIRECTION, 0x01 },
			{ 0x01, AC_VERB_SET_GPIO_DATA, 0x01 },
			{ }
		},
	},
	[CXT_FIXUP_ASPIRE_DMIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cxt_fixup_stereo_dmic,
		.chained = true,
		.chain_id = CXT_FIXUP_GPIO1,
	},
	[CXT_FIXUP_THINKPAD_ACPI] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = hda_fixup_thinkpad_acpi,
	},
	[CXT_FIXUP_OLPC_XO] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cxt_fixup_olpc_xo,
	},
	[CXT_FIXUP_CAP_MIX_AMP] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cxt_fixup_cap_mix_amp,
	},
	[CXT_FIXUP_TOSHIBA_P105] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x10, 0x961701f0 }, /* speaker/hp */
			{ 0x12, 0x02a1901e }, /* ext mic */
			{ 0x14, 0x95a70110 }, /* int mic */
			{}
		},
	},
	[CXT_FIXUP_HP_530] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x12, 0x90a60160 }, /* int mic */
			{}
		},
		.chained = true,
		.chain_id = CXT_FIXUP_CAP_MIX_AMP,
	},
	[CXT_FIXUP_CAP_MIX_AMP_5047] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cxt_fixup_cap_mix_amp_5047,
	},
	[CXT_FIXUP_MUTE_LED_EAPD] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cxt_fixup_mute_led_eapd,
	},
	[CXT_FIXUP_HP_DOCK] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x16, 0x21011020 }, /* line-out */
			{ 0x18, 0x2181103f }, /* line-in */
			{ }
		}
	},
	[CXT_FIXUP_HP_SPECTRE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			/* enable NID 0x1d for the speaker on top */
			{ 0x1d, 0x91170111 },
			{ }
		}
	},
	[CXT_FIXUP_HP_GATE_MIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cxt_fixup_hp_gate_mic_jack,
	},
};

static const struct snd_pci_quirk cxt5045_fixups[] = {
	SND_PCI_QUIRK(0x103c, 0x30d5, "HP 530", CXT_FIXUP_HP_530),
	SND_PCI_QUIRK(0x1179, 0xff31, "Toshiba P105", CXT_FIXUP_TOSHIBA_P105),
	/* HP, Packard Bell, Fujitsu-Siemens & Lenovo laptops have
	 * really bad sound over 0dB on NID 0x17.
	 */
	SND_PCI_QUIRK_VENDOR(0x103c, "HP", CXT_FIXUP_CAP_MIX_AMP),
	SND_PCI_QUIRK_VENDOR(0x1631, "Packard Bell", CXT_FIXUP_CAP_MIX_AMP),
	SND_PCI_QUIRK_VENDOR(0x1734, "Fujitsu", CXT_FIXUP_CAP_MIX_AMP),
	SND_PCI_QUIRK_VENDOR(0x17aa, "Lenovo", CXT_FIXUP_CAP_MIX_AMP),
	{}
};

static const struct hda_model_fixup cxt5045_fixup_models[] = {
	{ .id = CXT_FIXUP_CAP_MIX_AMP, .name = "cap-mix-amp" },
	{ .id = CXT_FIXUP_TOSHIBA_P105, .name = "toshiba-p105" },
	{ .id = CXT_FIXUP_HP_530, .name = "hp-530" },
	{}
};

static const struct snd_pci_quirk cxt5047_fixups[] = {
	/* HP laptops have really bad sound over 0 dB on NID 0x10.
	 */
	SND_PCI_QUIRK_VENDOR(0x103c, "HP", CXT_FIXUP_CAP_MIX_AMP_5047),
	{}
};

static const struct hda_model_fixup cxt5047_fixup_models[] = {
	{ .id = CXT_FIXUP_CAP_MIX_AMP_5047, .name = "cap-mix-amp" },
	{}
};

static const struct snd_pci_quirk cxt5051_fixups[] = {
	SND_PCI_QUIRK(0x103c, 0x360b, "Compaq CQ60", CXT_PINCFG_COMPAQ_CQ60),
	SND_PCI_QUIRK(0x17aa, 0x20f2, "Lenovo X200", CXT_PINCFG_LENOVO_X200),
	{}
};

static const struct hda_model_fixup cxt5051_fixup_models[] = {
	{ .id = CXT_PINCFG_LENOVO_X200, .name = "lenovo-x200" },
	{}
};

static const struct snd_pci_quirk cxt5066_fixups[] = {
	SND_PCI_QUIRK(0x1025, 0x0543, "Acer Aspire One 522", CXT_FIXUP_STEREO_DMIC),
	SND_PCI_QUIRK(0x1025, 0x054c, "Acer Aspire 3830TG", CXT_FIXUP_ASPIRE_DMIC),
	SND_PCI_QUIRK(0x1025, 0x054f, "Acer Aspire 4830T", CXT_FIXUP_ASPIRE_DMIC),
	SND_PCI_QUIRK(0x103c, 0x8079, "HP EliteBook 840 G3", CXT_FIXUP_HP_DOCK),
	SND_PCI_QUIRK(0x103c, 0x807C, "HP EliteBook 820 G3", CXT_FIXUP_HP_DOCK),
	SND_PCI_QUIRK(0x103c, 0x80FD, "HP ProBook 640 G2", CXT_FIXUP_HP_DOCK),
	SND_PCI_QUIRK(0x103c, 0x828c, "HP EliteBook 840 G4", CXT_FIXUP_HP_DOCK),
	SND_PCI_QUIRK(0x103c, 0x83b2, "HP EliteBook 840 G5", CXT_FIXUP_HP_DOCK),
	SND_PCI_QUIRK(0x103c, 0x83b3, "HP EliteBook 830 G5", CXT_FIXUP_HP_DOCK),
	SND_PCI_QUIRK(0x103c, 0x83d3, "HP ProBook 640 G4", CXT_FIXUP_HP_DOCK),
	SND_PCI_QUIRK(0x103c, 0x8174, "HP Spectre x360", CXT_FIXUP_HP_SPECTRE),
	SND_PCI_QUIRK(0x103c, 0x8115, "HP Z1 Gen3", CXT_FIXUP_HP_GATE_MIC),
	SND_PCI_QUIRK(0x1043, 0x138d, "Asus", CXT_FIXUP_HEADPHONE_MIC_PIN),
	SND_PCI_QUIRK(0x152d, 0x0833, "OLPC XO-1.5", CXT_FIXUP_OLPC_XO),
	SND_PCI_QUIRK(0x17aa, 0x20f2, "Lenovo T400", CXT_PINCFG_LENOVO_TP410),
	SND_PCI_QUIRK(0x17aa, 0x215e, "Lenovo T410", CXT_PINCFG_LENOVO_TP410),
	SND_PCI_QUIRK(0x17aa, 0x215f, "Lenovo T510", CXT_PINCFG_LENOVO_TP410),
	SND_PCI_QUIRK(0x17aa, 0x21ce, "Lenovo T420", CXT_PINCFG_LENOVO_TP410),
	SND_PCI_QUIRK(0x17aa, 0x21cf, "Lenovo T520", CXT_PINCFG_LENOVO_TP410),
	SND_PCI_QUIRK(0x17aa, 0x21d2, "Lenovo T420s", CXT_PINCFG_LENOVO_TP410),
	SND_PCI_QUIRK(0x17aa, 0x21da, "Lenovo X220", CXT_PINCFG_LENOVO_TP410),
	SND_PCI_QUIRK(0x17aa, 0x21db, "Lenovo X220-tablet", CXT_PINCFG_LENOVO_TP410),
	SND_PCI_QUIRK(0x17aa, 0x38af, "Lenovo IdeaPad Z560", CXT_FIXUP_MUTE_LED_EAPD),
	SND_PCI_QUIRK(0x17aa, 0x3905, "Lenovo G50-30", CXT_FIXUP_STEREO_DMIC),
	SND_PCI_QUIRK(0x17aa, 0x390b, "Lenovo G50-80", CXT_FIXUP_STEREO_DMIC),
	SND_PCI_QUIRK(0x17aa, 0x3975, "Lenovo U300s", CXT_FIXUP_STEREO_DMIC),
	/* NOTE: we'd need to extend the quirk for 17aa:3977 as the same
	 * PCI SSID is used on multiple Lenovo models
	 */
	SND_PCI_QUIRK(0x17aa, 0x3977, "Lenovo IdeaPad U310", CXT_FIXUP_STEREO_DMIC),
	SND_PCI_QUIRK(0x17aa, 0x3978, "Lenovo G50-70", CXT_FIXUP_STEREO_DMIC),
	SND_PCI_QUIRK(0x17aa, 0x397b, "Lenovo S205", CXT_FIXUP_STEREO_DMIC),
	SND_PCI_QUIRK_VENDOR(0x17aa, "Thinkpad", CXT_FIXUP_THINKPAD_ACPI),
	SND_PCI_QUIRK(0x1c06, 0x2011, "Lemote A1004", CXT_PINCFG_LEMOTE_A1004),
	SND_PCI_QUIRK(0x1c06, 0x2012, "Lemote A1205", CXT_PINCFG_LEMOTE_A1205),
	{}
};

static const struct hda_model_fixup cxt5066_fixup_models[] = {
	{ .id = CXT_FIXUP_STEREO_DMIC, .name = "stereo-dmic" },
	{ .id = CXT_FIXUP_GPIO1, .name = "gpio1" },
	{ .id = CXT_FIXUP_HEADPHONE_MIC_PIN, .name = "headphone-mic-pin" },
	{ .id = CXT_PINCFG_LENOVO_TP410, .name = "tp410" },
	{ .id = CXT_FIXUP_THINKPAD_ACPI, .name = "thinkpad" },
	{ .id = CXT_PINCFG_LEMOTE_A1004, .name = "lemote-a1004" },
	{ .id = CXT_PINCFG_LEMOTE_A1205, .name = "lemote-a1205" },
	{ .id = CXT_FIXUP_OLPC_XO, .name = "olpc-xo" },
	{ .id = CXT_FIXUP_MUTE_LED_EAPD, .name = "mute-led-eapd" },
	{ .id = CXT_FIXUP_HP_DOCK, .name = "hp-dock" },
	{ .id = CXT_PINCFG_LENOVO_NOTEBOOK, .name = "lenovo-20149" },
	{}
};

/* add "fake" mute amp-caps to DACs on cx5051 so that mixer mute switches
 * can be created (bko#42825)
 */
static void add_cx5051_fake_mutes(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;
	static hda_nid_t out_nids[] = {
		0x10, 0x11, 0
	};
	hda_nid_t *p;

	for (p = out_nids; *p; p++)
		snd_hda_override_amp_caps(codec, *p, HDA_OUTPUT,
					  AC_AMPCAP_MIN_MUTE |
					  query_amp_caps(codec, *p, HDA_OUTPUT));
	spec->gen.dac_min_mute = true;
}

static int patch_conexant_auto(struct hda_codec *codec)
{
	struct conexant_spec *spec;
	int err;

	codec_info(codec, "%s: BIOS auto-probing.\n", codec->core.chip_name);

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;
	snd_hda_gen_spec_init(&spec->gen);
	codec->spec = spec;
	codec->patch_ops = cx_auto_patch_ops;

	cx_auto_parse_beep(codec);
	cx_auto_parse_eapd(codec);
	spec->gen.own_eapd_ctl = 1;
	if (spec->dynamic_eapd)
		spec->gen.vmaster_mute.hook = cx_auto_vmaster_hook;

	switch (codec->core.vendor_id) {
	case 0x14f15045:
		codec->single_adc_amp = 1;
		spec->gen.mixer_nid = 0x17;
		spec->gen.add_stereo_mix_input = HDA_HINT_STEREO_MIX_AUTO;
		snd_hda_pick_fixup(codec, cxt5045_fixup_models,
				   cxt5045_fixups, cxt_fixups);
		break;
	case 0x14f15047:
		codec->pin_amp_workaround = 1;
		spec->gen.mixer_nid = 0x19;
		spec->gen.add_stereo_mix_input = HDA_HINT_STEREO_MIX_AUTO;
		snd_hda_pick_fixup(codec, cxt5047_fixup_models,
				   cxt5047_fixups, cxt_fixups);
		break;
	case 0x14f15051:
		add_cx5051_fake_mutes(codec);
		codec->pin_amp_workaround = 1;
		snd_hda_pick_fixup(codec, cxt5051_fixup_models,
				   cxt5051_fixups, cxt_fixups);
		break;
	default:
		codec->pin_amp_workaround = 1;
		snd_hda_pick_fixup(codec, cxt5066_fixup_models,
				   cxt5066_fixups, cxt_fixups);
		break;
	}

	/* Show mute-led control only on HP laptops
	 * This is a sort of white-list: on HP laptops, EAPD corresponds
	 * only to the mute-LED without actualy amp function.  Meanwhile,
	 * others may use EAPD really as an amp switch, so it might be
	 * not good to expose it blindly.
	 */
	switch (codec->core.subsystem_id >> 16) {
	case 0x103c:
		spec->gen.vmaster_mute_enum = 1;
		break;
	}

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	err = snd_hda_parse_pin_defcfg(codec, &spec->gen.autocfg, NULL,
				       spec->parse_flags);
	if (err < 0)
		goto error;

	err = snd_hda_gen_parse_auto_config(codec, &spec->gen.autocfg);
	if (err < 0)
		goto error;

	/* Some laptops with Conexant chips show stalls in S3 resume,
	 * which falls into the single-cmd mode.
	 * Better to make reset, then.
	 */
	if (!codec->bus->core.sync_write) {
		codec_info(codec,
			   "Enable sync_write for stable communication\n");
		codec->bus->core.sync_write = 1;
		codec->bus->allow_bus_reset = 1;
	}

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;

 error:
	cx_auto_free(codec);
	return err;
}

/*
 */

static const struct hda_device_id snd_hda_id_conexant[] = {
	HDA_CODEC_ENTRY(0x14f11f86, "CX8070", patch_conexant_auto),
	HDA_CODEC_ENTRY(0x14f12008, "CX8200", patch_conexant_auto),
	HDA_CODEC_ENTRY(0x14f120d0, "CX11970", patch_conexant_auto),
	HDA_CODEC_ENTRY(0x14f120d1, "SN6180", patch_conexant_auto),
	HDA_CODEC_ENTRY(0x14f15045, "CX20549 (Venice)", patch_conexant_auto),
	HDA_CODEC_ENTRY(0x14f15047, "CX20551 (Waikiki)", patch_conexant_auto),
	HDA_CODEC_ENTRY(0x14f15051, "CX20561 (Hermosa)", patch_conexant_auto),
	HDA_CODEC_ENTRY(0x14f15066, "CX20582 (Pebble)", patch_conexant_auto),
	HDA_CODEC_ENTRY(0x14f15067, "CX20583 (Pebble HSF)", patch_conexant_auto),
	HDA_CODEC_ENTRY(0x14f15068, "CX20584", patch_conexant_auto),
	HDA_CODEC_ENTRY(0x14f15069, "CX20585", patch_conexant_auto),
	HDA_CODEC_ENTRY(0x14f1506c, "CX20588", patch_conexant_auto),
	HDA_CODEC_ENTRY(0x14f1506e, "CX20590", patch_conexant_auto),
	HDA_CODEC_ENTRY(0x14f15097, "CX20631", patch_conexant_auto),
	HDA_CODEC_ENTRY(0x14f15098, "CX20632", patch_conexant_auto),
	HDA_CODEC_ENTRY(0x14f150a1, "CX20641", patch_conexant_auto),
	HDA_CODEC_ENTRY(0x14f150a2, "CX20642", patch_conexant_auto),
	HDA_CODEC_ENTRY(0x14f150ab, "CX20651", patch_conexant_auto),
	HDA_CODEC_ENTRY(0x14f150ac, "CX20652", patch_conexant_auto),
	HDA_CODEC_ENTRY(0x14f150b8, "CX20664", patch_conexant_auto),
	HDA_CODEC_ENTRY(0x14f150b9, "CX20665", patch_conexant_auto),
	HDA_CODEC_ENTRY(0x14f150f1, "CX21722", patch_conexant_auto),
	HDA_CODEC_ENTRY(0x14f150f2, "CX20722", patch_conexant_auto),
	HDA_CODEC_ENTRY(0x14f150f3, "CX21724", patch_conexant_auto),
	HDA_CODEC_ENTRY(0x14f150f4, "CX20724", patch_conexant_auto),
	HDA_CODEC_ENTRY(0x14f1510f, "CX20751/2", patch_conexant_auto),
	HDA_CODEC_ENTRY(0x14f15110, "CX20751/2", patch_conexant_auto),
	HDA_CODEC_ENTRY(0x14f15111, "CX20753/4", patch_conexant_auto),
	HDA_CODEC_ENTRY(0x14f15113, "CX20755", patch_conexant_auto),
	HDA_CODEC_ENTRY(0x14f15114, "CX20756", patch_conexant_auto),
	HDA_CODEC_ENTRY(0x14f15115, "CX20757", patch_conexant_auto),
	HDA_CODEC_ENTRY(0x14f151d7, "CX20952", patch_conexant_auto),
	{} /* terminator */
};
MODULE_DEVICE_TABLE(hdaudio, snd_hda_id_conexant);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Conexant HD-audio codec");

static struct hda_codec_driver conexant_driver = {
	.id = snd_hda_id_conexant,
};

module_hda_codec_driver(conexant_driver);
