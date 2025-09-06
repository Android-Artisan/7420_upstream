/*
 * Universal Interface for Intel High Definition Audio Codec
 *
 * HD audio interface patch for ALC 260/880/882 codecs
 * HD audio interface patch for Realtek ALC codecs
 *
 * Copyright (c) 2004 Kailang Yang <kailang@realtek.com.tw>
 *                    PeiSen Hou <pshou@realtek.com.tw>
 *                    Takashi Iwai <tiwai@suse.de>
 *                    Jonathan Woithe <jwoithe@physics.adelaide.edu.au>
 *                    Jonathan Woithe <jwoithe@just42.net>
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

#define ALC880_FRONT_EVENT		0x01
#define ALC880_DCVOL_EVENT		0x02
#define ALC880_HP_EVENT			0x04
#define ALC880_MIC_EVENT		0x08

/* ALC880 board config type */
enum {
	ALC880_3ST,
	ALC880_3ST_DIG,
	ALC880_5ST,
	ALC880_5ST_DIG,
	ALC880_W810,
	ALC880_Z71V,
	ALC880_6ST,
	ALC880_6ST_DIG,
	ALC880_F1734,
	ALC880_ASUS,
	ALC880_ASUS_DIG,
	ALC880_ASUS_W1V,
	ALC880_ASUS_DIG2,
	ALC880_FUJITSU,
	ALC880_UNIWILL_DIG,
	ALC880_UNIWILL,
	ALC880_UNIWILL_P53,
	ALC880_CLEVO,
	ALC880_TCL_S700,
	ALC880_LG,
	ALC880_LG_LW,
	ALC880_MEDION_RIM,
#ifdef CONFIG_SND_DEBUG
	ALC880_TEST,
#endif
	ALC880_AUTO,
	ALC880_MODEL_LAST /* last tag */
};

/* ALC260 models */
enum {
	ALC260_BASIC,
	ALC260_HP,
	ALC260_HP_3013,
	ALC260_FUJITSU_S702X,
	ALC260_ACER,
	ALC260_WILL,
	ALC260_REPLACER_672V,
#ifdef CONFIG_SND_DEBUG
	ALC260_TEST,
#endif
	ALC260_AUTO,
	ALC260_MODEL_LAST /* last tag */
};

/* ALC262 models */
enum {
	ALC262_BASIC,
	ALC262_HIPPO,
	ALC262_HIPPO_1,
	ALC262_FUJITSU,
	ALC262_HP_BPC,
	ALC262_HP_BPC_D7000_WL,
	ALC262_HP_BPC_D7000_WF,
	ALC262_HP_TC_T5735,
	ALC262_HP_RP5700,
	ALC262_BENQ_ED8,
	ALC262_SONY_ASSAMD,
	ALC262_BENQ_T31,
	ALC262_ULTRA,
	ALC262_LENOVO_3000,
	ALC262_AUTO,
	ALC262_MODEL_LAST /* last tag */
};

/* ALC268 models */
enum {
	ALC267_QUANTA_IL1,
	ALC268_3ST,
	ALC268_TOSHIBA,
	ALC268_ACER,
	ALC268_DELL,
	ALC268_ZEPTO,
#ifdef CONFIG_SND_DEBUG
	ALC268_TEST,
#endif
	ALC268_AUTO,
	ALC268_MODEL_LAST /* last tag */
};

/* ALC269 models */
enum {
	ALC269_BASIC,
	ALC269_ASUS_EEEPC_P703,
	ALC269_ASUS_EEEPC_P901,
	ALC269_AUTO,
	ALC269_MODEL_LAST /* last tag */
};

/* ALC861 models */
enum {
	ALC861_3ST,
	ALC660_3ST,
	ALC861_3ST_DIG,
	ALC861_6ST_DIG,
	ALC861_UNIWILL_M31,
	ALC861_TOSHIBA,
	ALC861_ASUS,
	ALC861_ASUS_LAPTOP,
	ALC861_AUTO,
	ALC861_MODEL_LAST,
};

/* ALC861-VD models */
enum {
	ALC660VD_3ST,
	ALC660VD_3ST_DIG,
	ALC861VD_3ST,
	ALC861VD_3ST_DIG,
	ALC861VD_6ST_DIG,
	ALC861VD_LENOVO,
	ALC861VD_DALLAS,
	ALC861VD_HP,
	ALC861VD_AUTO,
	ALC861VD_MODEL_LAST,
};

/* ALC662 models */
enum {
	ALC662_3ST_2ch_DIG,
	ALC662_3ST_6ch_DIG,
	ALC662_3ST_6ch,
	ALC662_5ST_DIG,
	ALC662_LENOVO_101E,
	ALC662_ASUS_EEEPC_P701,
	ALC662_ASUS_EEEPC_EP20,
	ALC663_ASUS_M51VA,
	ALC663_ASUS_G71V,
	ALC663_ASUS_H13,
	ALC663_ASUS_G50V,
	ALC662_AUTO,
	ALC662_MODEL_LAST,
};

/* ALC882 models */
enum {
	ALC882_3ST_DIG,
	ALC882_6ST_DIG,
	ALC882_ARIMA,
	ALC882_W2JC,
	ALC882_TARGA,
	ALC882_ASUS_A7J,
	ALC882_ASUS_A7M,
	ALC885_MACPRO,
	ALC885_MBP3,
	ALC885_IMAC24,
	ALC882_AUTO,
	ALC882_MODEL_LAST,
};

/* ALC883 models */
enum {
	ALC883_3ST_2ch_DIG,
	ALC883_3ST_6ch_DIG,
	ALC883_3ST_6ch,
	ALC883_6ST_DIG,
	ALC883_TARGA_DIG,
	ALC883_TARGA_2ch_DIG,
	ALC883_ACER,
	ALC883_ACER_ASPIRE,
	ALC883_MEDION,
	ALC883_MEDION_MD2,	
	ALC883_LAPTOP_EAPD,
	ALC883_LENOVO_101E_2ch,
	ALC883_LENOVO_NB0763,
	ALC888_LENOVO_MS7195_DIG,
	ALC883_HAIER_W66,		
	ALC888_3ST_HP,
	ALC888_6ST_DELL,
	ALC883_MITAC,
	ALC883_CLEVO_M720,
	ALC883_FUJITSU_PI2515,
	ALC883_3ST_6ch_INTEL,
	ALC883_AUTO,
	ALC883_MODEL_LAST,
};
#include <linux/dmi.h>
#include <linux/module.h>
#include <linux/input.h>
#include <sound/core.h>
#include <sound/jack.h>
#include "hda_codec.h"
#include "hda_local.h"
#include "hda_auto_parser.h"
#include "hda_jack.h"
#include "hda_generic.h"

/* keep halting ALC5505 DSP, for power saving */
#define HALT_REALTEK_ALC5505

/* for GPIO Poll */
#define GPIO_MASK	0x03

struct alc_spec {
	/* codec parameterization */
	struct snd_kcontrol_new *mixers[5];	/* mixer arrays */
	unsigned int num_mixers;

	const struct hda_verb *init_verbs[5];	/* initialization verbs
						 * don't forget NULL
						 * termination!
						 */
	unsigned int num_init_verbs;

	char *stream_name_analog;	/* analog PCM stream */
	struct hda_pcm_stream *stream_analog_playback;
	struct hda_pcm_stream *stream_analog_capture;
	struct hda_pcm_stream *stream_analog_alt_playback;
	struct hda_pcm_stream *stream_analog_alt_capture;

	char *stream_name_digital;	/* digital PCM stream */
	struct hda_pcm_stream *stream_digital_playback;
	struct hda_pcm_stream *stream_digital_capture;

	/* playback */
	struct hda_multi_out multiout;	/* playback set-up
					 * max_channels, dacs must be set
					 * dig_out_nid and hp_nid are optional
					 */
	hda_nid_t alt_dac_nid;

	/* capture */
	unsigned int num_adc_nids;
	hda_nid_t *adc_nids;
	hda_nid_t *capsrc_nids;
	hda_nid_t dig_in_nid;		/* digital-in NID; optional */

	/* capture source */
	unsigned int num_mux_defs;
	const struct hda_input_mux *input_mux;
	unsigned int cur_mux[3];

	/* channel model */
	const struct hda_channel_mode *channel_mode;
	int num_channel_mode;
	int need_dac_fix;

	/* PCM information */
	struct hda_pcm pcm_rec[3];	/* used in alc_build_pcms() */

	/* dynamic controls, init_verbs and input_mux */
	struct auto_pin_cfg autocfg;
	unsigned int num_kctl_alloc, num_kctl_used;
	struct snd_kcontrol_new *kctl_alloc;
	struct hda_input_mux private_imux;
	hda_nid_t private_dac_nids[AUTO_CFG_MAX_OUTS];

	/* hooks */
	void (*init_hook)(struct hda_codec *codec);
	void (*unsol_event)(struct hda_codec *codec, unsigned int res);

	/* for pin sensing */
	unsigned int sense_updated: 1;
	unsigned int jack_present: 1;
	unsigned int master_sw: 1;

	/* for virtual master */
	hda_nid_t vmaster_nid;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	struct hda_loopback_check loopback;
#endif
/* extra amp-initialization sequence types */
enum {
	ALC_INIT_NONE,
	ALC_INIT_DEFAULT,
	ALC_INIT_GPIO1,
	ALC_INIT_GPIO2,
	ALC_INIT_GPIO3,
};

enum {
	ALC_HEADSET_MODE_UNKNOWN,
	ALC_HEADSET_MODE_UNPLUGGED,
	ALC_HEADSET_MODE_HEADSET,
	ALC_HEADSET_MODE_MIC,
	ALC_HEADSET_MODE_HEADPHONE,
};

enum {
	ALC_HEADSET_TYPE_UNKNOWN,
	ALC_HEADSET_TYPE_CTIA,
	ALC_HEADSET_TYPE_OMTP,
};

enum {
	ALC_KEY_MICMUTE_INDEX,
};

struct alc_customize_define {
	unsigned int  sku_cfg;
	unsigned char port_connectivity;
	unsigned char check_sum;
	unsigned char customization;
	unsigned char external_amp;
	unsigned int  enable_pcbeep:1;
	unsigned int  platform_type:1;
	unsigned int  swap:1;
	unsigned int  override:1;
	unsigned int  fixup:1; /* Means that this sku is set by driver, not read from hw */
};

struct alc_spec {
	struct hda_gen_spec gen; /* must be at head */

	/* codec parameterization */
	const struct snd_kcontrol_new *mixers[5];	/* mixer arrays */
	unsigned int num_mixers;
	unsigned int beep_amp;	/* beep amp value, set via set_beep_amp() */

	struct alc_customize_define cdefine;
	unsigned int parse_flags; /* flag for snd_hda_parse_pin_defcfg() */

	/* mute LED for HP laptops, see alc269_fixup_mic_mute_hook() */
	int mute_led_polarity;
	hda_nid_t mute_led_nid;
	hda_nid_t cap_mute_led_nid;

	unsigned int gpio_led; /* used for alc269_fixup_hp_gpio_led() */
	unsigned int gpio_mute_led_mask;
	unsigned int gpio_mic_led_mask;

	hda_nid_t headset_mic_pin;
	hda_nid_t headphone_mic_pin;
	int current_headset_mode;
	int current_headset_type;

	/* hooks */
	void (*init_hook)(struct hda_codec *codec);
#ifdef CONFIG_PM
	void (*power_hook)(struct hda_codec *codec);
#endif
	void (*shutup)(struct hda_codec *codec);
	void (*reboot_notify)(struct hda_codec *codec);

	int init_amp;
	int codec_variant;	/* flag for other variants */
	unsigned int has_alc5505_dsp:1;
	unsigned int no_depop_delay:1;

	/* for PLL fix */
	hda_nid_t pll_nid;
	unsigned int pll_coef_idx, pll_coef_bit;
};

/*
 * configuration template - to be copied to the spec instance
 */
struct alc_config_preset {
	struct snd_kcontrol_new *mixers[5]; /* should be identical size
					     * with spec
					     */
	const struct hda_verb *init_verbs[5];
	unsigned int num_dacs;
	hda_nid_t *dac_nids;
	hda_nid_t dig_out_nid;		/* optional */
	hda_nid_t hp_nid;		/* optional */
	unsigned int num_adc_nids;
	hda_nid_t *adc_nids;
	hda_nid_t *capsrc_nids;
	hda_nid_t dig_in_nid;
	unsigned int num_channel_mode;
	const struct hda_channel_mode *channel_mode;
	int need_dac_fix;
	unsigned int num_mux_defs;
	const struct hda_input_mux *input_mux;
	void (*unsol_event)(struct hda_codec *, unsigned int);
	void (*init_hook)(struct hda_codec *);
#ifdef CONFIG_SND_HDA_POWER_SAVE
	struct hda_amp_list *loopbacks;
#endif
};


/*
 * input MUX handling
 */
static int alc_mux_enum_info(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	unsigned int mux_idx = snd_ctl_get_ioffidx(kcontrol, &uinfo->id);
	if (mux_idx >= spec->num_mux_defs)
		mux_idx = 0;
	return snd_hda_input_mux_info(&spec->input_mux[mux_idx], uinfo);
}

static int alc_mux_enum_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	unsigned int adc_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);

	ucontrol->value.enumerated.item[0] = spec->cur_mux[adc_idx];
	return 0;
}

static int alc_mux_enum_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	unsigned int adc_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	unsigned int mux_idx = adc_idx >= spec->num_mux_defs ? 0 : adc_idx;
	hda_nid_t nid = spec->capsrc_nids ?
		spec->capsrc_nids[adc_idx] : spec->adc_nids[adc_idx];
	return snd_hda_input_mux_put(codec, &spec->input_mux[mux_idx], ucontrol,
				     nid, &spec->cur_mux[adc_idx]);
}


/*
 * channel mode setting
 */
static int alc_ch_mode_info(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	return snd_hda_ch_mode_info(codec, uinfo, spec->channel_mode,
				    spec->num_channel_mode);
}

static int alc_ch_mode_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	return snd_hda_ch_mode_get(codec, ucontrol, spec->channel_mode,
				   spec->num_channel_mode,
				   spec->multiout.max_channels);
}

static int alc_ch_mode_put(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	int err = snd_hda_ch_mode_put(codec, ucontrol, spec->channel_mode,
				      spec->num_channel_mode,
				      &spec->multiout.max_channels);
	if (err >= 0 && spec->need_dac_fix)
		spec->multiout.num_dacs = spec->multiout.max_channels / 2;
	return err;
}

/*
 * Control the mode of pin widget settings via the mixer.  "pc" is used
 * instead of "%" to avoid consequences of accidently treating the % as 
 * being part of a format specifier.  Maximum allowed length of a value is
 * 63 characters plus NULL terminator.
 *
 * Note: some retasking pin complexes seem to ignore requests for input
 * states other than HiZ (eg: PIN_VREFxx) and revert to HiZ if any of these
 * are requested.  Therefore order this list so that this behaviour will not
 * cause problems when mixer clients move through the enum sequentially.
 * NIDs 0x0f and 0x10 have been observed to have this behaviour as of
 * March 2006.
 */
static char *alc_pin_mode_names[] = {
	"Mic 50pc bias", "Mic 80pc bias",
	"Line in", "Line out", "Headphone out",
};
static unsigned char alc_pin_mode_values[] = {
	PIN_VREF50, PIN_VREF80, PIN_IN, PIN_OUT, PIN_HP,
};
/* The control can present all 5 options, or it can limit the options based
 * in the pin being assumed to be exclusively an input or an output pin.  In
 * addition, "input" pins may or may not process the mic bias option
 * depending on actual widget capability (NIDs 0x0f and 0x10 don't seem to
 * accept requests for bias as of chip versions up to March 2006) and/or
 * wiring in the computer.
 */
#define ALC_PIN_DIR_IN              0x00
#define ALC_PIN_DIR_OUT             0x01
#define ALC_PIN_DIR_INOUT           0x02
#define ALC_PIN_DIR_IN_NOMICBIAS    0x03
#define ALC_PIN_DIR_INOUT_NOMICBIAS 0x04

/* Info about the pin modes supported by the different pin direction modes. 
 * For each direction the minimum and maximum values are given.
 */
static signed char alc_pin_mode_dir_info[5][2] = {
	{ 0, 2 },    /* ALC_PIN_DIR_IN */
	{ 3, 4 },    /* ALC_PIN_DIR_OUT */
	{ 0, 4 },    /* ALC_PIN_DIR_INOUT */
	{ 2, 2 },    /* ALC_PIN_DIR_IN_NOMICBIAS */
	{ 2, 4 },    /* ALC_PIN_DIR_INOUT_NOMICBIAS */
};
#define alc_pin_mode_min(_dir) (alc_pin_mode_dir_info[_dir][0])
#define alc_pin_mode_max(_dir) (alc_pin_mode_dir_info[_dir][1])
#define alc_pin_mode_n_items(_dir) \
	(alc_pin_mode_max(_dir)-alc_pin_mode_min(_dir)+1)

static int alc_pin_mode_info(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_info *uinfo)
{
	unsigned int item_num = uinfo->value.enumerated.item;
	unsigned char dir = (kcontrol->private_value >> 16) & 0xff;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = alc_pin_mode_n_items(dir);

	if (item_num<alc_pin_mode_min(dir) || item_num>alc_pin_mode_max(dir))
		item_num = alc_pin_mode_min(dir);
	strcpy(uinfo->value.enumerated.name, alc_pin_mode_names[item_num]);
	return 0;
}

static int alc_pin_mode_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	unsigned int i;
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value & 0xffff;
	unsigned char dir = (kcontrol->private_value >> 16) & 0xff;
	long *valp = ucontrol->value.integer.value;
	unsigned int pinctl = snd_hda_codec_read(codec, nid, 0,
						 AC_VERB_GET_PIN_WIDGET_CONTROL,
						 0x00);

	/* Find enumerated value for current pinctl setting */
	i = alc_pin_mode_min(dir);
	while (alc_pin_mode_values[i] != pinctl && i <= alc_pin_mode_max(dir))
		i++;
	*valp = i <= alc_pin_mode_max(dir) ? i: alc_pin_mode_min(dir);
	return 0;
}

static int alc_pin_mode_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	signed int change;
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value & 0xffff;
	unsigned char dir = (kcontrol->private_value >> 16) & 0xff;
	long val = *ucontrol->value.integer.value;
	unsigned int pinctl = snd_hda_codec_read(codec, nid, 0,
						 AC_VERB_GET_PIN_WIDGET_CONTROL,
						 0x00);

	if (val < alc_pin_mode_min(dir) || val > alc_pin_mode_max(dir))
		val = alc_pin_mode_min(dir);

	change = pinctl != alc_pin_mode_values[val];
	if (change) {
		/* Set pin mode to that requested */
		snd_hda_codec_write_cache(codec, nid, 0,
					  AC_VERB_SET_PIN_WIDGET_CONTROL,
					  alc_pin_mode_values[val]);

		/* Also enable the retasking pin's input/output as required 
		 * for the requested pin mode.  Enum values of 2 or less are
		 * input modes.
		 *
		 * Dynamically switching the input/output buffers probably
		 * reduces noise slightly (particularly on input) so we'll
		 * do it.  However, having both input and output buffers
		 * enabled simultaneously doesn't seem to be problematic if
		 * this turns out to be necessary in the future.
		 */
		if (val <= 2) {
			snd_hda_codec_amp_stereo(codec, nid, HDA_OUTPUT, 0,
						 HDA_AMP_MUTE, HDA_AMP_MUTE);
			snd_hda_codec_amp_stereo(codec, nid, HDA_INPUT, 0,
						 HDA_AMP_MUTE, 0);
		} else {
			snd_hda_codec_amp_stereo(codec, nid, HDA_INPUT, 0,
						 HDA_AMP_MUTE, HDA_AMP_MUTE);
			snd_hda_codec_amp_stereo(codec, nid, HDA_OUTPUT, 0,
						 HDA_AMP_MUTE, 0);
		}
	}
	return change;
}

#define ALC_PIN_MODE(xname, nid, dir) \
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = 0,  \
	  .info = alc_pin_mode_info, \
	  .get = alc_pin_mode_get, \
	  .put = alc_pin_mode_put, \
	  .private_value = nid | (dir<<16) }

/* A switch control for ALC260 GPIO pins.  Multiple GPIOs can be ganged
 * together using a mask with more than one bit set.  This control is
 * currently used only by the ALC260 test model.  At this stage they are not
 * needed for any "production" models.
 */
#ifdef CONFIG_SND_DEBUG
#define alc_gpio_data_info	snd_ctl_boolean_mono_info

static int alc_gpio_data_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value & 0xffff;
	unsigned char mask = (kcontrol->private_value >> 16) & 0xff;
	long *valp = ucontrol->value.integer.value;
	unsigned int val = snd_hda_codec_read(codec, nid, 0,
					      AC_VERB_GET_GPIO_DATA, 0x00);

	*valp = (val & mask) != 0;
	return 0;
}
static int alc_gpio_data_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	signed int change;
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value & 0xffff;
	unsigned char mask = (kcontrol->private_value >> 16) & 0xff;
	long val = *ucontrol->value.integer.value;
	unsigned int gpio_data = snd_hda_codec_read(codec, nid, 0,
						    AC_VERB_GET_GPIO_DATA,
						    0x00);

	/* Set/unset the masked GPIO bit(s) as needed */
	change = (val == 0 ? 0 : mask) != (gpio_data & mask);
	if (val == 0)
		gpio_data &= ~mask;
	else
		gpio_data |= mask;
	snd_hda_codec_write_cache(codec, nid, 0,
				  AC_VERB_SET_GPIO_DATA, gpio_data);

	return change;
}
#define ALC_GPIO_DATA_SWITCH(xname, nid, mask) \
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = 0,  \
	  .info = alc_gpio_data_info, \
	  .get = alc_gpio_data_get, \
	  .put = alc_gpio_data_put, \
	  .private_value = nid | (mask<<16) }
#endif   /* CONFIG_SND_DEBUG */

/* A switch control to allow the enabling of the digital IO pins on the
 * ALC260.  This is incredibly simplistic; the intention of this control is
 * to provide something in the test model allowing digital outputs to be
 * identified if present.  If models are found which can utilise these
 * outputs a more complete mixer control can be devised for those models if
 * necessary.
 */
#ifdef CONFIG_SND_DEBUG
#define alc_spdif_ctrl_info	snd_ctl_boolean_mono_info

static int alc_spdif_ctrl_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value & 0xffff;
	unsigned char mask = (kcontrol->private_value >> 16) & 0xff;
	long *valp = ucontrol->value.integer.value;
	unsigned int val = snd_hda_codec_read(codec, nid, 0,
					      AC_VERB_GET_DIGI_CONVERT_1, 0x00);

	*valp = (val & mask) != 0;
	return 0;
}
static int alc_spdif_ctrl_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	signed int change;
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value & 0xffff;
	unsigned char mask = (kcontrol->private_value >> 16) & 0xff;
	long val = *ucontrol->value.integer.value;
	unsigned int ctrl_data = snd_hda_codec_read(codec, nid, 0,
						    AC_VERB_GET_DIGI_CONVERT_1,
						    0x00);

	/* Set/unset the masked control bit(s) as needed */
	change = (val == 0 ? 0 : mask) != (ctrl_data & mask);
	if (val==0)
		ctrl_data &= ~mask;
	else
		ctrl_data |= mask;
	snd_hda_codec_write_cache(codec, nid, 0, AC_VERB_SET_DIGI_CONVERT_1,
				  ctrl_data);

	return change;
}
#define ALC_SPDIF_CTRL_SWITCH(xname, nid, mask) \
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = 0,  \
	  .info = alc_spdif_ctrl_info, \
	  .get = alc_spdif_ctrl_get, \
	  .put = alc_spdif_ctrl_put, \
	  .private_value = nid | (mask<<16) }
#endif   /* CONFIG_SND_DEBUG */

/* A switch control to allow the enabling EAPD digital outputs on the ALC26x.
 * Again, this is only used in the ALC26x test models to help identify when
 * the EAPD line must be asserted for features to work.
 */
#ifdef CONFIG_SND_DEBUG
#define alc_eapd_ctrl_info	snd_ctl_boolean_mono_info

static int alc_eapd_ctrl_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value & 0xffff;
	unsigned char mask = (kcontrol->private_value >> 16) & 0xff;
	long *valp = ucontrol->value.integer.value;
	unsigned int val = snd_hda_codec_read(codec, nid, 0,
					      AC_VERB_GET_EAPD_BTLENABLE, 0x00);

	*valp = (val & mask) != 0;
	return 0;
}

static int alc_eapd_ctrl_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	int change;
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value & 0xffff;
	unsigned char mask = (kcontrol->private_value >> 16) & 0xff;
	long val = *ucontrol->value.integer.value;
	unsigned int ctrl_data = snd_hda_codec_read(codec, nid, 0,
						    AC_VERB_GET_EAPD_BTLENABLE,
						    0x00);

	/* Set/unset the masked control bit(s) as needed */
	change = (!val ? 0 : mask) != (ctrl_data & mask);
	if (!val)
		ctrl_data &= ~mask;
	else
		ctrl_data |= mask;
	snd_hda_codec_write_cache(codec, nid, 0, AC_VERB_SET_EAPD_BTLENABLE,
				  ctrl_data);

	return change;
}

#define ALC_EAPD_CTRL_SWITCH(xname, nid, mask) \
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = 0,  \
	  .info = alc_eapd_ctrl_info, \
	  .get = alc_eapd_ctrl_get, \
	  .put = alc_eapd_ctrl_put, \
	  .private_value = nid | (mask<<16) }
#endif   /* CONFIG_SND_DEBUG */

/*
 * set up from the preset table
 */
static void setup_preset(struct alc_spec *spec,
			 const struct alc_config_preset *preset)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(preset->mixers) && preset->mixers[i]; i++)
		spec->mixers[spec->num_mixers++] = preset->mixers[i];
	for (i = 0; i < ARRAY_SIZE(preset->init_verbs) && preset->init_verbs[i];
	     i++)
		spec->init_verbs[spec->num_init_verbs++] =
			preset->init_verbs[i];
	
	spec->channel_mode = preset->channel_mode;
	spec->num_channel_mode = preset->num_channel_mode;
	spec->need_dac_fix = preset->need_dac_fix;

	spec->multiout.max_channels = spec->channel_mode[0].channels;

	spec->multiout.num_dacs = preset->num_dacs;
	spec->multiout.dac_nids = preset->dac_nids;
	spec->multiout.dig_out_nid = preset->dig_out_nid;
	spec->multiout.hp_nid = preset->hp_nid;
	
	spec->num_mux_defs = preset->num_mux_defs;
	if (!spec->num_mux_defs)
		spec->num_mux_defs = 1;
	spec->input_mux = preset->input_mux;

	spec->num_adc_nids = preset->num_adc_nids;
	spec->adc_nids = preset->adc_nids;
	spec->capsrc_nids = preset->capsrc_nids;
	spec->dig_in_nid = preset->dig_in_nid;

	spec->unsol_event = preset->unsol_event;
	spec->init_hook = preset->init_hook;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	spec->loopback.amplist = preset->loopbacks;
#endif
}

/* Enable GPIO mask and set output */
static struct hda_verb alc_gpio1_init_verbs[] = {
	unsigned int coef0;
	struct input_dev *kb_dev;
	u8 alc_mute_keycode_map[1];
};

/*
 * COEF access helper functions
 */

static int alc_read_coefex_idx(struct hda_codec *codec, hda_nid_t nid,
			       unsigned int coef_idx)
{
	unsigned int val;

	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_COEF_INDEX, coef_idx);
	val = snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_PROC_COEF, 0);
	return val;
}

#define alc_read_coef_idx(codec, coef_idx) \
	alc_read_coefex_idx(codec, 0x20, coef_idx)

static void alc_write_coefex_idx(struct hda_codec *codec, hda_nid_t nid,
				 unsigned int coef_idx, unsigned int coef_val)
{
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_COEF_INDEX, coef_idx);
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_PROC_COEF, coef_val);
}

#define alc_write_coef_idx(codec, coef_idx, coef_val) \
	alc_write_coefex_idx(codec, 0x20, coef_idx, coef_val)

static void alc_update_coefex_idx(struct hda_codec *codec, hda_nid_t nid,
				  unsigned int coef_idx, unsigned int mask,
				  unsigned int bits_set)
{
	unsigned int val = alc_read_coefex_idx(codec, nid, coef_idx);

	if (val != -1)
		alc_write_coefex_idx(codec, nid, coef_idx,
				     (val & ~mask) | bits_set);
}

#define alc_update_coef_idx(codec, coef_idx, mask, bits_set)	\
	alc_update_coefex_idx(codec, 0x20, coef_idx, mask, bits_set)

/* a special bypass for COEF 0; read the cached value at the second time */
static unsigned int alc_get_coef0(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	if (!spec->coef0)
		spec->coef0 = alc_read_coef_idx(codec, 0);
	return spec->coef0;
}

/* coef writes/updates batch */
struct coef_fw {
	unsigned char nid;
	unsigned char idx;
	unsigned short mask;
	unsigned short val;
};

#define UPDATE_COEFEX(_nid, _idx, _mask, _val) \
	{ .nid = (_nid), .idx = (_idx), .mask = (_mask), .val = (_val) }
#define WRITE_COEFEX(_nid, _idx, _val) UPDATE_COEFEX(_nid, _idx, -1, _val)
#define WRITE_COEF(_idx, _val) WRITE_COEFEX(0x20, _idx, _val)
#define UPDATE_COEF(_idx, _mask, _val) UPDATE_COEFEX(0x20, _idx, _mask, _val)

static void alc_process_coef_fw(struct hda_codec *codec,
				const struct coef_fw *fw)
{
	for (; fw->nid; fw++) {
		if (fw->mask == (unsigned short)-1)
			alc_write_coefex_idx(codec, fw->nid, fw->idx, fw->val);
		else
			alc_update_coefex_idx(codec, fw->nid, fw->idx,
					      fw->mask, fw->val);
	}
}

/*
 * Append the given mixer and verb elements for the later use
 * The mixer array is referred in build_controls(), and init_verbs are
 * called in init().
 */
static void add_mixer(struct alc_spec *spec, const struct snd_kcontrol_new *mix)
{
	if (snd_BUG_ON(spec->num_mixers >= ARRAY_SIZE(spec->mixers)))
		return;
	spec->mixers[spec->num_mixers++] = mix;
}

/*
 * GPIO setup tables, used in initialization
 */
/* Enable GPIO mask and set output */
static const struct hda_verb alc_gpio1_init_verbs[] = {
	{0x01, AC_VERB_SET_GPIO_MASK, 0x01},
	{0x01, AC_VERB_SET_GPIO_DIRECTION, 0x01},
	{0x01, AC_VERB_SET_GPIO_DATA, 0x01},
	{ }
};

static struct hda_verb alc_gpio2_init_verbs[] = {
static const struct hda_verb alc_gpio2_init_verbs[] = {
	{0x01, AC_VERB_SET_GPIO_MASK, 0x02},
	{0x01, AC_VERB_SET_GPIO_DIRECTION, 0x02},
	{0x01, AC_VERB_SET_GPIO_DATA, 0x02},
	{ }
};

static struct hda_verb alc_gpio3_init_verbs[] = {
static const struct hda_verb alc_gpio3_init_verbs[] = {
	{0x01, AC_VERB_SET_GPIO_MASK, 0x03},
	{0x01, AC_VERB_SET_GPIO_DIRECTION, 0x03},
	{0x01, AC_VERB_SET_GPIO_DATA, 0x03},
	{ }
};

/*
 * Fix hardware PLL issue
 * On some codecs, the analog PLL gating control must be off while
 * the default value is 1.
 */
static void alc_fix_pll(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	unsigned int val;

	if (!spec->pll_nid)
		return;
	snd_hda_codec_write(codec, spec->pll_nid, 0, AC_VERB_SET_COEF_INDEX,
			    spec->pll_coef_idx);
	val = snd_hda_codec_read(codec, spec->pll_nid, 0,
				 AC_VERB_GET_PROC_COEF, 0);
	snd_hda_codec_write(codec, spec->pll_nid, 0, AC_VERB_SET_COEF_INDEX,
			    spec->pll_coef_idx);
	snd_hda_codec_write(codec, spec->pll_nid, 0, AC_VERB_SET_PROC_COEF,
			    val & ~(1 << spec->pll_coef_bit));

	if (spec->pll_nid)
		alc_update_coefex_idx(codec, spec->pll_nid, spec->pll_coef_idx,
				      1 << spec->pll_coef_bit, 0);
}

static void alc_fix_pll_init(struct hda_codec *codec, hda_nid_t nid,
			     unsigned int coef_idx, unsigned int coef_bit)
{
	struct alc_spec *spec = codec->spec;
	spec->pll_nid = nid;
	spec->pll_coef_idx = coef_idx;
	spec->pll_coef_bit = coef_bit;
	alc_fix_pll(codec);
}

static void alc_sku_automute(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	unsigned int present;
	unsigned int hp_nid = spec->autocfg.hp_pins[0];
	unsigned int sp_nid = spec->autocfg.speaker_pins[0];

	/* need to execute and sync at first */
	snd_hda_codec_read(codec, hp_nid, 0, AC_VERB_SET_PIN_SENSE, 0);
	present = snd_hda_codec_read(codec, hp_nid, 0,
				     AC_VERB_GET_PIN_SENSE, 0);
	spec->jack_present = (present & 0x80000000) != 0;
	snd_hda_codec_write(codec, sp_nid, 0, AC_VERB_SET_PIN_WIDGET_CONTROL,
			    spec->jack_present ? 0 : PIN_OUT);
}

/* unsolicited event for HP jack sensing */
static void alc_sku_unsol_event(struct hda_codec *codec, unsigned int res)
{
	if (codec->vendor_id == 0x10ec0880)
		res >>= 28;
	else
		res >>= 26;
	if (res != ALC880_HP_EVENT)
		return;

	alc_sku_automute(codec);
/* update the master volume per volume-knob's unsol event */
static void alc_update_knob_master(struct hda_codec *codec,
				   struct hda_jack_callback *jack)
{
	unsigned int val;
	struct snd_kcontrol *kctl;
	struct snd_ctl_elem_value *uctl;

	kctl = snd_hda_find_mixer_ctl(codec, "Master Playback Volume");
	if (!kctl)
		return;
	uctl = kzalloc(sizeof(*uctl), GFP_KERNEL);
	if (!uctl)
		return;
	val = snd_hda_codec_read(codec, jack->nid, 0,
				 AC_VERB_GET_VOLUME_KNOB_CONTROL, 0);
	val &= HDA_AMP_VOLMASK;
	uctl->value.integer.value[0] = val;
	uctl->value.integer.value[1] = val;
	kctl->put(kctl, uctl);
	kfree(uctl);
}

static void alc880_unsol_event(struct hda_codec *codec, unsigned int res)
{
	/* For some reason, the res given from ALC880 is broken.
	   Here we adjust it properly. */
	snd_hda_jack_unsol_event(codec, res >> 2);
}

/* Change EAPD to verb control */
static void alc_fill_eapd_coef(struct hda_codec *codec)
{
	int coef;

	coef = alc_get_coef0(codec);

	switch (codec->core.vendor_id) {
	case 0x10ec0262:
		alc_update_coef_idx(codec, 0x7, 0, 1<<5);
		break;
	case 0x10ec0267:
	case 0x10ec0268:
		alc_update_coef_idx(codec, 0x7, 0, 1<<13);
		break;
	case 0x10ec0269:
		if ((coef & 0x00f0) == 0x0010)
			alc_update_coef_idx(codec, 0xd, 0, 1<<14);
		if ((coef & 0x00f0) == 0x0020)
			alc_update_coef_idx(codec, 0x4, 1<<15, 0);
		if ((coef & 0x00f0) == 0x0030)
			alc_update_coef_idx(codec, 0x10, 1<<9, 0);
		break;
	case 0x10ec0280:
	case 0x10ec0284:
	case 0x10ec0290:
	case 0x10ec0292:
		alc_update_coef_idx(codec, 0x4, 1<<15, 0);
		break;
	case 0x10ec0225:
	case 0x10ec0233:
	case 0x10ec0235:
	case 0x10ec0255:
	case 0x10ec0282:
	case 0x10ec0283:
	case 0x10ec0286:
	case 0x10ec0288:
	case 0x10ec0295:
	case 0x10ec0298:
	case 0x10ec0299:
		alc_update_coef_idx(codec, 0x10, 1<<9, 0);
		break;
	case 0x10ec0236:
	case 0x10ec0256:
		alc_write_coef_idx(codec, 0x36, 0x5757);
		alc_update_coef_idx(codec, 0x10, 1<<9, 0);
		break;
	case 0x10ec0285:
	case 0x10ec0293:
		alc_update_coef_idx(codec, 0xa, 1<<13, 0);
		break;
	case 0x10ec0234:
	case 0x10ec0274:
		alc_write_coef_idx(codec, 0x6e, 0x0c25);
		/* fallthrough */
	case 0x10ec0294:
	case 0x10ec0700:
	case 0x10ec0701:
	case 0x10ec0703:
		alc_update_coef_idx(codec, 0x10, 1<<15, 0);
		break;
	case 0x10ec0662:
		if ((coef & 0x00f0) == 0x0030)
			alc_update_coef_idx(codec, 0x4, 1<<10, 0); /* EAPD Ctrl */
		break;
	case 0x10ec0272:
	case 0x10ec0273:
	case 0x10ec0663:
	case 0x10ec0665:
	case 0x10ec0670:
	case 0x10ec0671:
	case 0x10ec0672:
		alc_update_coef_idx(codec, 0xd, 0, 1<<14); /* EAPD Ctrl */
		break;
	case 0x10ec0668:
		alc_update_coef_idx(codec, 0x7, 3<<13, 0);
		break;
	case 0x10ec0867:
		alc_update_coef_idx(codec, 0x4, 1<<10, 0);
		break;
	case 0x10ec0888:
		if ((coef & 0x00f0) == 0x0020 || (coef & 0x00f0) == 0x0030)
			alc_update_coef_idx(codec, 0x7, 1<<5, 0);
		break;
	case 0x10ec0892:
	case 0x10ec0897:
		alc_update_coef_idx(codec, 0x7, 1<<5, 0);
		break;
	case 0x10ec0899:
	case 0x10ec0900:
		alc_update_coef_idx(codec, 0x7, 1<<1, 0);
		break;
	}
}

/* additional initialization for ALC888 variants */
static void alc888_coef_init(struct hda_codec *codec)
{
	unsigned int tmp;

	snd_hda_codec_write(codec, 0x20, 0, AC_VERB_SET_COEF_INDEX, 0);
	tmp = snd_hda_codec_read(codec, 0x20, 0, AC_VERB_GET_PROC_COEF, 0);
	snd_hda_codec_write(codec, 0x20, 0, AC_VERB_SET_COEF_INDEX, 7);
	if ((tmp & 0xf0) == 2)
		/* alc888S-VC */
		snd_hda_codec_read(codec, 0x20, 0,
				   AC_VERB_SET_PROC_COEF, 0x830);
	 else
		 /* alc888-VB */
		 snd_hda_codec_read(codec, 0x20, 0,
				    AC_VERB_SET_PROC_COEF, 0x3030);
}

/* 32-bit subsystem ID for BIOS loading in HD Audio codec.
 *	31 ~ 16 :	Manufacture ID
 *	15 ~ 8	:	SKU ID
 *	7  ~ 0	:	Assembly ID
 *	port-A --> pin 39/41, port-E --> pin 14/15, port-D --> pin 35/36
 */
static void alc_subsystem_id(struct hda_codec *codec,
			     unsigned int porta, unsigned int porte,
			     unsigned int portd)
{
	unsigned int ass, tmp, i;
	unsigned nid;
	struct alc_spec *spec = codec->spec;

	ass = codec->subsystem_id & 0xffff;
	if ((ass != codec->bus->pci->subsystem_device) && (ass & 1))
		goto do_sku;

	/*	
	 * 31~30	: port conetcivity
	 * 29~21	: reserve
	 * 20		: PCBEEP input
	 * 19~16	: Check sum (15:1)
	 * 15~1		: Custom
	 * 0		: override
	*/
	nid = 0x1d;
	if (codec->vendor_id == 0x10ec0260)
		nid = 0x17;
	ass = snd_hda_codec_read(codec, nid, 0,
				 AC_VERB_GET_CONFIG_DEFAULT, 0);
	if (!(ass & 1) && !(ass & 0x100000))
		return;
	if ((ass >> 30) != 1)	/* no physical connection */
		return;
	switch (alc_get_coef0(codec) & 0x00f0) {
	/* alc888-VA */
	case 0x00:
	/* alc888-VB */
	case 0x10:
		alc_update_coef_idx(codec, 7, 0, 0x2030); /* Turn EAPD to High */
		break;
	}
}

/* turn on/off EAPD control (only if available) */
static void set_eapd(struct hda_codec *codec, hda_nid_t nid, int on)
{
	if (get_wcaps_type(get_wcaps(codec, nid)) != AC_WID_PIN)
		return;
	if (snd_hda_query_pin_caps(codec, nid) & AC_PINCAP_EAPD)
		snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_EAPD_BTLENABLE,
				    on ? 2 : 0);
}

/* turn on/off EAPD controls of the codec */
static void alc_auto_setup_eapd(struct hda_codec *codec, bool on)
{
	/* We currently only handle front, HP */
	static hda_nid_t pins[] = {
		0x0f, 0x10, 0x14, 0x15, 0x17, 0
	};
	hda_nid_t *p;
	for (p = pins; *p; p++)
		set_eapd(codec, *p, on);
}

/* generic shutup callback;
 * just turning off EPAD and a little pause for avoiding pop-noise
 */
static void alc_eapd_shutup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	alc_auto_setup_eapd(codec, false);
	if (!spec->no_depop_delay)
		msleep(200);
	snd_hda_shutup_pins(codec);
}

/* generic EAPD initialization */
static void alc_auto_init_amp(struct hda_codec *codec, int type)
{
	alc_fill_eapd_coef(codec);
	alc_auto_setup_eapd(codec, true);
	switch (type) {
	case ALC_INIT_GPIO1:
		snd_hda_sequence_write(codec, alc_gpio1_init_verbs);
		break;
	case ALC_INIT_GPIO2:
		snd_hda_sequence_write(codec, alc_gpio2_init_verbs);
		break;
	case ALC_INIT_GPIO3:
		snd_hda_sequence_write(codec, alc_gpio3_init_verbs);
		break;
	case ALC_INIT_DEFAULT:
		switch (codec->core.vendor_id) {
		case 0x10ec0260:
			alc_update_coefex_idx(codec, 0x1a, 7, 0, 0x2010);
			break;
		case 0x10ec0880:
		case 0x10ec0882:
		case 0x10ec0883:
		case 0x10ec0885:
			alc_update_coef_idx(codec, 7, 0, 0x2030);
			break;
		case 0x10ec0888:
			alc888_coef_init(codec);
			break;
		}
		break;
	}
}


/*
 * Realtek SSID verification
 */

/* Could be any non-zero and even value. When used as fixup, tells
 * the driver to ignore any present sku defines.
 */
#define ALC_FIXUP_SKU_IGNORE (2)

static void alc_fixup_sku_ignore(struct hda_codec *codec,
				 const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->cdefine.fixup = 1;
		spec->cdefine.sku_cfg = ALC_FIXUP_SKU_IGNORE;
	}
}

static void alc_fixup_no_depop_delay(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PROBE) {
		spec->no_depop_delay = 1;
		codec->depop_delay = 0;
	}
}

static int alc_auto_parse_customize_define(struct hda_codec *codec)
{
	unsigned int ass, tmp, i;
	unsigned nid = 0;
	struct alc_spec *spec = codec->spec;

	spec->cdefine.enable_pcbeep = 1; /* assume always enabled */

	if (spec->cdefine.fixup) {
		ass = spec->cdefine.sku_cfg;
		if (ass == ALC_FIXUP_SKU_IGNORE)
			return -1;
		goto do_sku;
	}

	if (!codec->bus->pci)
		return -1;
	ass = codec->core.subsystem_id & 0xffff;
	if (ass != codec->bus->pci->subsystem_device && (ass & 1))
		goto do_sku;

	nid = 0x1d;
	if (codec->core.vendor_id == 0x10ec0260)
		nid = 0x17;
	ass = snd_hda_codec_get_pincfg(codec, nid);

	if (!(ass & 1)) {
		codec_info(codec, "%s: SKU not ready 0x%08x\n",
			   codec->core.chip_name, ass);
		return -1;
	}

	/* check sum */
	tmp = 0;
	for (i = 1; i < 16; i++) {
		if ((ass >> i) & 1)
			tmp++;
	}
	if (((ass >> 16) & 0xf) != tmp)
		return;
do_sku:
		return -1;

	spec->cdefine.port_connectivity = ass >> 30;
	spec->cdefine.enable_pcbeep = (ass & 0x100000) >> 20;
	spec->cdefine.check_sum = (ass >> 16) & 0xf;
	spec->cdefine.customization = ass >> 8;
do_sku:
	spec->cdefine.sku_cfg = ass;
	spec->cdefine.external_amp = (ass & 0x38) >> 3;
	spec->cdefine.platform_type = (ass & 0x4) >> 2;
	spec->cdefine.swap = (ass & 0x2) >> 1;
	spec->cdefine.override = ass & 0x1;

	codec_dbg(codec, "SKU: Nid=0x%x sku_cfg=0x%08x\n",
		   nid, spec->cdefine.sku_cfg);
	codec_dbg(codec, "SKU: port_connectivity=0x%x\n",
		   spec->cdefine.port_connectivity);
	codec_dbg(codec, "SKU: enable_pcbeep=0x%x\n", spec->cdefine.enable_pcbeep);
	codec_dbg(codec, "SKU: check_sum=0x%08x\n", spec->cdefine.check_sum);
	codec_dbg(codec, "SKU: customization=0x%08x\n", spec->cdefine.customization);
	codec_dbg(codec, "SKU: external_amp=0x%x\n", spec->cdefine.external_amp);
	codec_dbg(codec, "SKU: platform_type=0x%x\n", spec->cdefine.platform_type);
	codec_dbg(codec, "SKU: swap=0x%x\n", spec->cdefine.swap);
	codec_dbg(codec, "SKU: override=0x%x\n", spec->cdefine.override);

	return 0;
}

/* return the position of NID in the list, or -1 if not found */
static int find_idx_in_nid_list(hda_nid_t nid, const hda_nid_t *list, int nums)
{
	int i;
	for (i = 0; i < nums; i++)
		if (list[i] == nid)
			return i;
	return -1;
}
/* return true if the given NID is found in the list */
static bool found_in_nid_list(hda_nid_t nid, const hda_nid_t *list, int nums)
{
	return find_idx_in_nid_list(nid, list, nums) >= 0;
}

/* check subsystem ID and set up device-specific initialization;
 * return 1 if initialized, 0 if invalid SSID
 */
/* 32-bit subsystem ID for BIOS loading in HD Audio codec.
 *	31 ~ 16 :	Manufacture ID
 *	15 ~ 8	:	SKU ID
 *	7  ~ 0	:	Assembly ID
 *	port-A --> pin 39/41, port-E --> pin 14/15, port-D --> pin 35/36
 */
static int alc_subsystem_id(struct hda_codec *codec, const hda_nid_t *ports)
{
	unsigned int ass, tmp, i;
	unsigned nid;
	struct alc_spec *spec = codec->spec;

	if (spec->cdefine.fixup) {
		ass = spec->cdefine.sku_cfg;
		if (ass == ALC_FIXUP_SKU_IGNORE)
			return 0;
		goto do_sku;
	}

	ass = codec->core.subsystem_id & 0xffff;
	if (codec->bus->pci &&
	    ass != codec->bus->pci->subsystem_device && (ass & 1))
		goto do_sku;

	/* invalid SSID, check the special NID pin defcfg instead */
	/*
	 * 31~30	: port connectivity
	 * 29~21	: reserve
	 * 20		: PCBEEP input
	 * 19~16	: Check sum (15:1)
	 * 15~1		: Custom
	 * 0		: override
	*/
	nid = 0x1d;
	if (codec->core.vendor_id == 0x10ec0260)
		nid = 0x17;
	ass = snd_hda_codec_get_pincfg(codec, nid);
	codec_dbg(codec,
		  "realtek: No valid SSID, checking pincfg 0x%08x for NID 0x%x\n",
		   ass, nid);
	if (!(ass & 1))
		return 0;
	if ((ass >> 30) != 1)	/* no physical connection */
		return 0;

	/* check sum */
	tmp = 0;
	for (i = 1; i < 16; i++) {
		if ((ass >> i) & 1)
			tmp++;
	}
	if (((ass >> 16) & 0xf) != tmp)
		return 0;
do_sku:
	codec_dbg(codec, "realtek: Enabling init ASM_ID=0x%04x CODEC_ID=%08x\n",
		   ass & 0xffff, codec->core.vendor_id);
	/*
	 * 0 : override
	 * 1 :	Swap Jack
	 * 2 : 0 --> Desktop, 1 --> Laptop
	 * 3~5 : External Amplifier control
	 * 7~6 : Reserved
	*/
	tmp = (ass & 0x38) >> 3;	/* external Amp control */
	switch (tmp) {
	case 1:
		snd_hda_sequence_write(codec, alc_gpio1_init_verbs);
		break;
	case 3:
		snd_hda_sequence_write(codec, alc_gpio2_init_verbs);
		break;
	case 7:
		snd_hda_sequence_write(codec, alc_gpio3_init_verbs);
		break;
	case 5:	/* set EAPD output high */
		switch (codec->vendor_id) {
		case 0x10ec0260:
			snd_hda_codec_write(codec, 0x0f, 0,
					    AC_VERB_SET_EAPD_BTLENABLE, 2);
			snd_hda_codec_write(codec, 0x10, 0,
					    AC_VERB_SET_EAPD_BTLENABLE, 2);
			break;
		case 0x10ec0262:
		case 0x10ec0267:
		case 0x10ec0268:
		case 0x10ec0269:
		case 0x10ec0660:
		case 0x10ec0662:
		case 0x10ec0663:
		case 0x10ec0862:
		case 0x10ec0889:
			snd_hda_codec_write(codec, 0x14, 0,
					    AC_VERB_SET_EAPD_BTLENABLE, 2);
			snd_hda_codec_write(codec, 0x15, 0,
					    AC_VERB_SET_EAPD_BTLENABLE, 2);
			break;
		}
		switch (codec->vendor_id) {
		case 0x10ec0260:
			snd_hda_codec_write(codec, 0x1a, 0,
					    AC_VERB_SET_COEF_INDEX, 7);
			tmp = snd_hda_codec_read(codec, 0x1a, 0,
						 AC_VERB_GET_PROC_COEF, 0);
			snd_hda_codec_write(codec, 0x1a, 0,
					    AC_VERB_SET_COEF_INDEX, 7);
			snd_hda_codec_write(codec, 0x1a, 0,
					    AC_VERB_SET_PROC_COEF,
					    tmp | 0x2010);
			break;
		case 0x10ec0262:
		case 0x10ec0880:
		case 0x10ec0882:
		case 0x10ec0883:
		case 0x10ec0885:
		case 0x10ec0889:
			snd_hda_codec_write(codec, 0x20, 0,
					    AC_VERB_SET_COEF_INDEX, 7);
			tmp = snd_hda_codec_read(codec, 0x20, 0,
						 AC_VERB_GET_PROC_COEF, 0);
			snd_hda_codec_write(codec, 0x20, 0,
					    AC_VERB_SET_COEF_INDEX, 7);	
			snd_hda_codec_write(codec, 0x20, 0,
					    AC_VERB_SET_PROC_COEF,
					    tmp | 0x2010);
			break;
		case 0x10ec0888:
			/*alc888_coef_init(codec);*/ /* called in alc_init() */
			break;
		case 0x10ec0267:
		case 0x10ec0268:
			snd_hda_codec_write(codec, 0x20, 0,
					    AC_VERB_SET_COEF_INDEX, 7);
			tmp = snd_hda_codec_read(codec, 0x20, 0,
						 AC_VERB_GET_PROC_COEF, 0);
			snd_hda_codec_write(codec, 0x20, 0,
					    AC_VERB_SET_COEF_INDEX, 7);	
			snd_hda_codec_write(codec, 0x20, 0,
					    AC_VERB_SET_PROC_COEF,
					    tmp | 0x3000);
			break;
		}
	default:
		break;
	}
	
		spec->init_amp = ALC_INIT_GPIO1;
		break;
	case 3:
		spec->init_amp = ALC_INIT_GPIO2;
		break;
	case 7:
		spec->init_amp = ALC_INIT_GPIO3;
		break;
	case 5:
	default:
		spec->init_amp = ALC_INIT_DEFAULT;
		break;
	}

	/* is laptop or Desktop and enable the function "Mute internal speaker
	 * when the external headphone out jack is plugged"
	 */
	if (!(ass & 0x8000))
		return;
		return 1;
	/*
	 * 10~8 : Jack location
	 * 12~11: Headphone out -> 00: PortA, 01: PortE, 02: PortD, 03: Resvered
	 * 14~13: Resvered
	 * 15   : 1 --> enable the function "Mute internal speaker
	 *	        when the external headphone out jack is plugged"
	 */
	if (!spec->autocfg.speaker_pins[0]) {
		if (spec->autocfg.line_out_pins[0])
			spec->autocfg.speaker_pins[0] =
				spec->autocfg.line_out_pins[0];
		else
			return;
	}

	if (!spec->autocfg.hp_pins[0]) {
		tmp = (ass >> 11) & 0x3;	/* HP to chassis */
		if (tmp == 0)
			spec->autocfg.hp_pins[0] = porta;
		else if (tmp == 1)
			spec->autocfg.hp_pins[0] = porte;
		else if (tmp == 2)
			spec->autocfg.hp_pins[0] = portd;
		else
			return;
	}

	snd_hda_codec_write(codec, spec->autocfg.hp_pins[0], 0,
			    AC_VERB_SET_UNSOLICITED_ENABLE,
			    AC_USRSP_EN | ALC880_HP_EVENT);
	spec->unsol_event = alc_sku_unsol_event;
}

/*
 * Fix-up pin default configurations
 */

struct alc_pincfg {
	hda_nid_t nid;
	u32 val;
};

static void alc_fix_pincfg(struct hda_codec *codec,
			   const struct snd_pci_quirk *quirk,
			   const struct alc_pincfg **pinfix)
{
	const struct alc_pincfg *cfg;

	quirk = snd_pci_quirk_lookup(codec->bus->pci, quirk);
	if (!quirk)
		return;

	cfg = pinfix[quirk->value];
	for (; cfg->nid; cfg++) {
		int i;
		u32 val = cfg->val;
		for (i = 0; i < 4; i++) {
			snd_hda_codec_write(codec, cfg->nid, 0,
				    AC_VERB_SET_CONFIG_DEFAULT_BYTES_0 + i,
				    val & 0xff);
			val >>= 8;
		}
	if (!spec->gen.autocfg.hp_pins[0] &&
	    !(spec->gen.autocfg.line_out_pins[0] &&
	      spec->gen.autocfg.line_out_type == AUTO_PIN_HP_OUT)) {
		hda_nid_t nid;
		tmp = (ass >> 11) & 0x3;	/* HP to chassis */
		nid = ports[tmp];
		if (found_in_nid_list(nid, spec->gen.autocfg.line_out_pins,
				      spec->gen.autocfg.line_outs))
			return 1;
		spec->gen.autocfg.hp_pins[0] = nid;
	}
	return 1;
}

/* Check the validity of ALC subsystem-id
 * ports contains an array of 4 pin NIDs for port-A, E, D and I */
static void alc_ssid_check(struct hda_codec *codec, const hda_nid_t *ports)
{
	if (!alc_subsystem_id(codec, ports)) {
		struct alc_spec *spec = codec->spec;
		codec_dbg(codec,
			  "realtek: Enable default setup for auto mode as fallback\n");
		spec->init_amp = ALC_INIT_DEFAULT;
	}
}

/*
 * ALC880 3-stack model
 *
 * DAC: Front = 0x02 (0x0c), Surr = 0x05 (0x0f), CLFE = 0x04 (0x0e)
 * Pin assignment: Front = 0x14, Line-In/Surr = 0x1a, Mic/CLFE = 0x18,
 *                 F-Mic = 0x1b, HP = 0x19
 */

static hda_nid_t alc880_dac_nids[4] = {
	/* front, rear, clfe, rear_surr */
	0x02, 0x05, 0x04, 0x03
};

static hda_nid_t alc880_adc_nids[3] = {
	/* ADC0-2 */
	0x07, 0x08, 0x09,
};

/* The datasheet says the node 0x07 is connected from inputs,
 * but it shows zero connection in the real implementation on some devices.
 * Note: this is a 915GAV bug, fixed on 915GLV
 */
static hda_nid_t alc880_adc_nids_alt[2] = {
	/* ADC1-2 */
	0x08, 0x09,
};

#define ALC880_DIGOUT_NID	0x06
#define ALC880_DIGIN_NID	0x0a

static struct hda_input_mux alc880_capture_source = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x3 },
		{ "Line", 0x2 },
		{ "CD", 0x4 },
	},
};

/* channel source setting (2/6 channel selection for 3-stack) */
/* 2ch mode */
static struct hda_verb alc880_threestack_ch2_init[] = {
	/* set line-in to input, mute it */
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	/* set mic-in to input vref 80%, mute it */
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ } /* end */
};

/* 6ch mode */
static struct hda_verb alc880_threestack_ch6_init[] = {
	/* set line-in to output, unmute it */
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	/* set mic-in to output, unmute it */
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ } /* end */
};

static struct hda_channel_mode alc880_threestack_modes[2] = {
	{ 2, alc880_threestack_ch2_init },
	{ 6, alc880_threestack_ch6_init },
};

static struct snd_kcontrol_new alc880_three_stack_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0f, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0f, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x3, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x3, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x19, 0x0, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
	},
	{ } /* end */
};

/* capture mixer elements */
static struct snd_kcontrol_new alc880_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x07, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x07, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 2, 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 2, 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 3,
		.info = alc_mux_enum_info,
		.get = alc_mux_enum_get,
		.put = alc_mux_enum_put,
	},
	{ } /* end */
};

/* capture mixer elements (in case NID 0x07 not available) */
static struct snd_kcontrol_new alc880_capture_alt_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = alc_mux_enum_info,
		.get = alc_mux_enum_get,
		.put = alc_mux_enum_put,
	},
	{ } /* end */
};



/*
 * ALC880 5-stack model
 *
 * DAC: Front = 0x02 (0x0c), Surr = 0x05 (0x0f), CLFE = 0x04 (0x0d),
 *      Side = 0x02 (0xd)
 * Pin assignment: Front = 0x14, Surr = 0x17, CLFE = 0x16
 *                 Line-In/Side = 0x1a, Mic = 0x18, F-Mic = 0x1b, HP = 0x19
 */

/* additional mixers to alc880_three_stack_mixer */
static struct snd_kcontrol_new alc880_five_stack_mixer[] = {
	HDA_CODEC_VOLUME("Side Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Side Playback Switch", 0x0d, 2, HDA_INPUT),
	{ } /* end */
};

/* channel source setting (6/8 channel selection for 5-stack) */
/* 6ch mode */
static struct hda_verb alc880_fivestack_ch6_init[] = {
	/* set line-in to input, mute it */
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ } /* end */
};

/* 8ch mode */
static struct hda_verb alc880_fivestack_ch8_init[] = {
	/* set line-in to output, unmute it */
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ } /* end */
};

static struct hda_channel_mode alc880_fivestack_modes[2] = {
	{ 6, alc880_fivestack_ch6_init },
	{ 8, alc880_fivestack_ch8_init },
};


/*
 * ALC880 6-stack model
 *
 * DAC: Front = 0x02 (0x0c), Surr = 0x03 (0x0d), CLFE = 0x04 (0x0e),
 *      Side = 0x05 (0x0f)
 * Pin assignment: Front = 0x14, Surr = 0x15, CLFE = 0x16, Side = 0x17,
 *   Mic = 0x18, F-Mic = 0x19, Line = 0x1a, HP = 0x1b
 */

static hda_nid_t alc880_6st_dac_nids[4] = {
	/* front, rear, clfe, rear_surr */
	0x02, 0x03, 0x04, 0x05
};

static struct hda_input_mux alc880_6stack_capture_source = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x1 },
		{ "Line", 0x2 },
		{ "CD", 0x4 },
	},
};

/* fixed 8-channels */
static struct hda_channel_mode alc880_sixstack_modes[1] = {
	{ 8, NULL },
};

static struct snd_kcontrol_new alc880_six_stack_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Side Playback Volume", 0x0f, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Side Playback Switch", 0x0f, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x0b, 0x05, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
	},
	{ } /* end */
};


/*
 * ALC880 W810 model
 *
 * W810 has rear IO for:
 * Front (DAC 02)
 * Surround (DAC 03)
 * Center/LFE (DAC 04)
 * Digital out (06)
 *
 * The system also has a pair of internal speakers, and a headphone jack.
 * These are both connected to Line2 on the codec, hence to DAC 02.
 * 
 * There is a variable resistor to control the speaker or headphone
 * volume. This is a hardware-only device without a software API.
 *
 * Plugging headphones in will disable the internal speakers. This is
 * implemented in hardware, not via the driver using jack sense. In
 * a similar fashion, plugging into the rear socket marked "front" will
 * disable both the speakers and headphones.
 *
 * For input, there's a microphone jack, and an "audio in" jack.
 * These may not do anything useful with this driver yet, because I
 * haven't setup any initialization verbs for these yet...
 */

static hda_nid_t alc880_w810_dac_nids[3] = {
	/* front, rear/surround, clfe */
	0x02, 0x03, 0x04
};

/* fixed 6 channels */
static struct hda_channel_mode alc880_w810_modes[1] = {
	{ 6, NULL }
};

/* Pin assignment: Front = 0x14, Surr = 0x15, CLFE = 0x16, HP = 0x1b */
static struct snd_kcontrol_new alc880_w810_base_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	{ } /* end */
};


/*
 * Z710V model
 *
 * DAC: Front = 0x02 (0x0c), HP = 0x03 (0x0d)
 * Pin assignment: Front = 0x14, HP = 0x15, Mic = 0x18, Mic2 = 0x19(?),
 *                 Line = 0x1a
 */

static hda_nid_t alc880_z71v_dac_nids[1] = {
	0x02
};
#define ALC880_Z71V_HP_DAC	0x03

/* fixed 2 channels */
static struct hda_channel_mode alc880_2_jack_modes[1] = {
	{ 2, NULL }
};

static struct snd_kcontrol_new alc880_z71v_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};


/*
 * ALC880 F1734 model
 *
 * DAC: HP = 0x02 (0x0c), Front = 0x03 (0x0d)
 * Pin assignment: HP = 0x14, Front = 0x15, Mic = 0x18
 */

static hda_nid_t alc880_f1734_dac_nids[1] = {
	0x03
};
#define ALC880_F1734_HP_DAC	0x02

static struct snd_kcontrol_new alc880_f1734_mixer[] = {
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static struct hda_input_mux alc880_f1734_capture_source = {
	.num_items = 2,
	.items = {
		{ "Mic", 0x1 },
		{ "CD", 0x4 },
	},
};


/*
 * ALC880 ASUS model
 *
 * DAC: HP/Front = 0x02 (0x0c), Surr = 0x03 (0x0d), CLFE = 0x04 (0x0e)
 * Pin assignment: HP/Front = 0x14, Surr = 0x15, CLFE = 0x16,
 *  Mic = 0x18, Line = 0x1a
 */

#define alc880_asus_dac_nids	alc880_w810_dac_nids	/* identical with w810 */
#define alc880_asus_modes	alc880_threestack_modes	/* 2/6 channel mode */

static struct snd_kcontrol_new alc880_asus_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
	},
	{ } /* end */
};

/*
 * ALC880 ASUS W1V model
 *
 * DAC: HP/Front = 0x02 (0x0c), Surr = 0x03 (0x0d), CLFE = 0x04 (0x0e)
 * Pin assignment: HP/Front = 0x14, Surr = 0x15, CLFE = 0x16,
 *  Mic = 0x18, Line = 0x1a, Line2 = 0x1b
 */

/* additional mixers to alc880_asus_mixer */
static struct snd_kcontrol_new alc880_asus_w1v_mixer[] = {
	HDA_CODEC_VOLUME("Line2 Playback Volume", 0x0b, 0x03, HDA_INPUT),
	HDA_CODEC_MUTE("Line2 Playback Switch", 0x0b, 0x03, HDA_INPUT),
	{ } /* end */
};

/* additional mixers to alc880_asus_mixer */
static struct snd_kcontrol_new alc880_pcbeep_mixer[] = {
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x0b, 0x05, HDA_INPUT),
	{ } /* end */
};

/* TCL S700 */
static struct snd_kcontrol_new alc880_tcl_s700_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0B, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0B, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0B, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0B, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 1,
		.info = alc_mux_enum_info,
		.get = alc_mux_enum_get,
		.put = alc_mux_enum_put,
	},
	{ } /* end */
};

/* Uniwill */
static struct snd_kcontrol_new alc880_uniwill_mixer[] = {
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x0b, 0x05, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc880_fujitsu_mixer[] = {
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Ext Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Ext Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Int Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Int Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc880_uniwill_p53_mixer[] = {
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};

/*
 * virtual master controls
 */

/*
 * slave controls for virtual master
 */
static const char *alc_slave_vols[] = {
	"Front Playback Volume",
	"Surround Playback Volume",
	"Center Playback Volume",
	"LFE Playback Volume",
	"Side Playback Volume",
	"Headphone Playback Volume",
	"Speaker Playback Volume",
	"Mono Playback Volume",
	"Line-Out Playback Volume",
	NULL,
};

static const char *alc_slave_sws[] = {
	"Front Playback Switch",
	"Surround Playback Switch",
	"Center Playback Switch",
	"LFE Playback Switch",
	"Side Playback Switch",
	"Headphone Playback Switch",
	"Speaker Playback Switch",
	"Mono Playback Switch",
	"IEC958 Playback Switch",
	NULL,
};

/*
 * build control elements
 */
static int alc_build_controls(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int err;
	int i;
 */

static void alc_fixup_inv_dmic(struct hda_codec *codec,
			       const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	spec->gen.inv_dmic_split = 1;
}


#ifdef CONFIG_SND_HDA_INPUT_BEEP
/* additional beep mixers; the actual parameters are overwritten at build */
static const struct snd_kcontrol_new alc_beep_mixer[] = {
	HDA_CODEC_VOLUME("Beep Playback Volume", 0, 0, HDA_INPUT),
	HDA_CODEC_MUTE_BEEP("Beep Playback Switch", 0, 0, HDA_INPUT),
	{ } /* end */
};
#endif

static int alc_build_controls(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i, err;

	err = snd_hda_gen_build_controls(codec);
	if (err < 0)
		return err;

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

	/* if we have no master control, let's create it */
	if (!snd_hda_find_mixer_ctl(codec, "Master Playback Volume")) {
		unsigned int vmaster_tlv[4];
		snd_hda_set_vmaster_tlv(codec, spec->vmaster_nid,
					HDA_OUTPUT, vmaster_tlv);
		err = snd_hda_add_vmaster(codec, "Master Playback Volume",
					  vmaster_tlv, alc_slave_vols);
		if (err < 0)
			return err;
	}
	if (!snd_hda_find_mixer_ctl(codec, "Master Playback Switch")) {
		err = snd_hda_add_vmaster(codec, "Master Playback Switch",
					  NULL, alc_slave_sws);
		if (err < 0)
			return err;
	}

#ifdef CONFIG_SND_HDA_INPUT_BEEP
	/* create beep controls if needed */
	if (spec->beep_amp) {
		const struct snd_kcontrol_new *knew;
		for (knew = alc_beep_mixer; knew->name; knew++) {
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
#endif

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_BUILD);
	return 0;
}


/*
 * initialize the codec volumes, etc
 */

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static struct hda_verb alc880_volume_init_verbs[] = {
	/*
	 * Unmute ADC0-2 and set the default input to mic-in
	 */
	{0x07, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	/* Unmute input amps (CD, Line In, Mic 1 & Mic 2) of the analog-loopback
	 * mixer widget
	 * Note: PASD motherboards uses the Line In 2 as the input for front
	 * panel mic (mic 2)
	 */
	/* Amp Indices: Mic1 = 0, Mic2 = 1, Line1 = 2, Line2 = 3, CD = 4 */
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(6)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(7)},

	/*
	 * Set up output mixers (0x0c - 0x0f)
	 */
	/* set vol=0 to output mixers */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	/* set up input amps for analog loopback */
	/* Amp Indices: DAC = 0, mixer = 1 */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},

	{ }
};

/*
 * 3-stack pin configuration:
 * front = 0x14, mic/clfe = 0x18, HP = 0x19, line/surr = 0x1a, f-mic = 0x1b
 */
static struct hda_verb alc880_pin_3stack_init_verbs[] = {
	/*
	 * preset connection lists of input pins
	 * 0 = front, 1 = rear_surr, 2 = CLFE, 3 = surround
	 */
	{0x10, AC_VERB_SET_CONNECT_SEL, 0x02}, /* mic/clfe */
	{0x11, AC_VERB_SET_CONNECT_SEL, 0x00}, /* HP */
	{0x12, AC_VERB_SET_CONNECT_SEL, 0x03}, /* line/surround */

	/*
	 * Set pin mode and muting
	 */
	/* set front pin widgets 0x14 for output */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Mic1 (rear panel) pin widget for input and vref at 80% */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Mic2 (as headphone out) for HP output */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Line In pin widget for input */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Line2 (as front mic) pin widget for input and vref at 80% */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* CD pin widget for input */
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

	{ }
};

/*
 * 5-stack pin configuration:
 * front = 0x14, surround = 0x17, clfe = 0x16, mic = 0x18, HP = 0x19,
 * line-in/side = 0x1a, f-mic = 0x1b
 */
static struct hda_verb alc880_pin_5stack_init_verbs[] = {
	/*
	 * preset connection lists of input pins
	 * 0 = front, 1 = rear_surr, 2 = CLFE, 3 = surround
	 */
	{0x11, AC_VERB_SET_CONNECT_SEL, 0x00}, /* HP */
	{0x12, AC_VERB_SET_CONNECT_SEL, 0x01}, /* line/side */

	/*
	 * Set pin mode and muting
	 */
	/* set pin widgets 0x14-0x17 for output */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	/* unmute pins for output (no gain on this amp) */
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	/* Mic1 (rear panel) pin widget for input and vref at 80% */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Mic2 (as headphone out) for HP output */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Line In pin widget for input */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Line2 (as front mic) pin widget for input and vref at 80% */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* CD pin widget for input */
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

	{ }
};

/*
 * W810 pin configuration:
 * front = 0x14, surround = 0x15, clfe = 0x16, HP = 0x1b
 */
static struct hda_verb alc880_pin_w810_init_verbs[] = {
	/* hphone/speaker input selector: front DAC */
	{0x13, AC_VERB_SET_CONNECT_SEL, 0x0},

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},

	{ }
};

/*
 * Z71V pin configuration:
 * Speaker-out = 0x14, HP = 0x15, Mic = 0x18, Line-in = 0x1a, Mic2 = 0x1b (?)
 */
static struct hda_verb alc880_pin_z71v_init_verbs[] = {
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

	{ }
};

/*
 * 6-stack pin configuration:
 * front = 0x14, surr = 0x15, clfe = 0x16, side = 0x17, mic = 0x18,
 * f-mic = 0x19, line = 0x1a, HP = 0x1b
 */
static struct hda_verb alc880_pin_6stack_init_verbs[] = {
	{0x13, AC_VERB_SET_CONNECT_SEL, 0x00}, /* HP */

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	
	{ }
};

/*
 * Uniwill pin configuration:
 * HP = 0x14, InternalSpeaker = 0x15, mic = 0x18, internal mic = 0x19,
 * line = 0x1a
 */
static struct hda_verb alc880_uniwill_init_verbs[] = {
	{0x13, AC_VERB_SET_CONNECT_SEL, 0x00}, /* HP */

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},

	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* {0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP}, */
	/* {0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE}, */
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},

	{ }
};

/*
* Uniwill P53
* HP = 0x14, InternalSpeaker = 0x15, mic = 0x19, 
 */
static struct hda_verb alc880_uniwill_p53_init_verbs[] = {
	{0x13, AC_VERB_SET_CONNECT_SEL, 0x00}, /* HP */

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},

	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},

	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{0x21, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_DCVOL_EVENT},

	{ }
};

static struct hda_verb alc880_beep_init_verbs[] = {
	{ 0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(5) },
	{ }
};

/* toggle speaker-output according to the hp-jack state */
static void alc880_uniwill_hp_automute(struct hda_codec *codec)
{
 	unsigned int present;
	unsigned char bits;

 	present = snd_hda_codec_read(codec, 0x14, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x15, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
	snd_hda_codec_amp_stereo(codec, 0x16, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
}

/* auto-toggle front mic */
static void alc880_uniwill_mic_automute(struct hda_codec *codec)
{
 	unsigned int present;
	unsigned char bits;

	present = snd_hda_codec_read(codec, 0x18, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x0b, HDA_INPUT, 1, HDA_AMP_MUTE, bits);
}

static void alc880_uniwill_automute(struct hda_codec *codec)
{
	alc880_uniwill_hp_automute(codec);
	alc880_uniwill_mic_automute(codec);
}

static void alc880_uniwill_unsol_event(struct hda_codec *codec,
				       unsigned int res)
{
	/* Looks like the unsol event is incompatible with the standard
	 * definition.  4bit tag is placed at 28 bit!
	 */
	switch (res >> 28) {
	case ALC880_HP_EVENT:
		alc880_uniwill_hp_automute(codec);
		break;
	case ALC880_MIC_EVENT:
		alc880_uniwill_mic_automute(codec);
		break;
	}
}

static void alc880_uniwill_p53_hp_automute(struct hda_codec *codec)
{
 	unsigned int present;
	unsigned char bits;

 	present = snd_hda_codec_read(codec, 0x14, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x15, HDA_OUTPUT, 0, HDA_AMP_MUTE, bits);
}

static void alc880_uniwill_p53_dcvol_automute(struct hda_codec *codec)
{
	unsigned int present;
	
	present = snd_hda_codec_read(codec, 0x21, 0,
				     AC_VERB_GET_VOLUME_KNOB_CONTROL, 0);
	present &= HDA_AMP_VOLMASK;
	snd_hda_codec_amp_stereo(codec, 0x0c, HDA_OUTPUT, 0,
				 HDA_AMP_VOLMASK, present);
	snd_hda_codec_amp_stereo(codec, 0x0d, HDA_OUTPUT, 0,
				 HDA_AMP_VOLMASK, present);
}

static void alc880_uniwill_p53_unsol_event(struct hda_codec *codec,
					   unsigned int res)
{
	/* Looks like the unsol event is incompatible with the standard
	 * definition.  4bit tag is placed at 28 bit!
	 */
	if ((res >> 28) == ALC880_HP_EVENT)
		alc880_uniwill_p53_hp_automute(codec);
	if ((res >> 28) == ALC880_DCVOL_EVENT)
		alc880_uniwill_p53_dcvol_automute(codec);
}

/*
 * F1734 pin configuration:
 * HP = 0x14, speaker-out = 0x15, mic = 0x18
 */
static struct hda_verb alc880_pin_f1734_init_verbs[] = {
	{0x07, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x10, AC_VERB_SET_CONNECT_SEL, 0x02},
	{0x11, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x12, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x13, AC_VERB_SET_CONNECT_SEL, 0x00},

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF50},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN|ALC880_HP_EVENT},
	{0x21, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN|ALC880_DCVOL_EVENT},

	{ }
};

/*
 * ASUS pin configuration:
 * HP/front = 0x14, surr = 0x15, clfe = 0x16, mic = 0x18, line = 0x1a
 */
static struct hda_verb alc880_pin_asus_init_verbs[] = {
	{0x10, AC_VERB_SET_CONNECT_SEL, 0x02},
	{0x11, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x12, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x13, AC_VERB_SET_CONNECT_SEL, 0x00},

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	
	{ }
};

/* Enable GPIO mask and set output */
#define alc880_gpio1_init_verbs	alc_gpio1_init_verbs
#define alc880_gpio2_init_verbs	alc_gpio2_init_verbs

/* Clevo m520g init */
static struct hda_verb alc880_pin_clevo_init_verbs[] = {
	/* headphone output */
	{0x11, AC_VERB_SET_CONNECT_SEL, 0x01},
	/* line-out */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Line-in */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* CD */
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Mic1 (rear panel) */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Mic2 (front panel) */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* headphone */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
        /* change to EAPD mode */
	{0x20, AC_VERB_SET_COEF_INDEX, 0x07},
	{0x20, AC_VERB_SET_PROC_COEF,  0x3060},

	{ }
};

static struct hda_verb alc880_pin_tcl_S700_init_verbs[] = {
	/* change to EAPD mode */
	{0x20, AC_VERB_SET_COEF_INDEX, 0x07},
	{0x20, AC_VERB_SET_PROC_COEF,  0x3060},

	/* Headphone output */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	/* Front output*/
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00},

	/* Line In pin widget for input */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	/* CD pin widget for input */
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	/* Mic1 (rear panel) pin widget for input and vref at 80% */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},

	/* change to EAPD mode */
	{0x20, AC_VERB_SET_COEF_INDEX, 0x07},
	{0x20, AC_VERB_SET_PROC_COEF,  0x3070},

	{ }
};

/*
 * LG m1 express dual
 *
 * Pin assignment:
 *   Rear Line-In/Out (blue): 0x14
 *   Build-in Mic-In: 0x15
 *   Speaker-out: 0x17
 *   HP-Out (green): 0x1b
 *   Mic-In/Out (red): 0x19
 *   SPDIF-Out: 0x1e
 */

/* To make 5.1 output working (green=Front, blue=Surr, red=CLFE) */
static hda_nid_t alc880_lg_dac_nids[3] = {
	0x05, 0x02, 0x03
};

/* seems analog CD is not working */
static struct hda_input_mux alc880_lg_capture_source = {
	.num_items = 3,
	.items = {
		{ "Mic", 0x1 },
		{ "Line", 0x5 },
		{ "Internal Mic", 0x6 },
	},
};

/* 2,4,6 channel modes */
static struct hda_verb alc880_lg_ch2_init[] = {
	/* set line-in and mic-in to input */
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ }
};

static struct hda_verb alc880_lg_ch4_init[] = {
	/* set line-in to out and mic-in to input */
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP },
	{ 0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ }
};

static struct hda_verb alc880_lg_ch6_init[] = {
	/* set line-in and mic-in to output */
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP },
	{ 0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP },
	{ }
};

static struct hda_channel_mode alc880_lg_ch_modes[3] = {
	{ 2, alc880_lg_ch2_init },
	{ 4, alc880_lg_ch4_init },
	{ 6, alc880_lg_ch6_init },
};

static struct snd_kcontrol_new alc880_lg_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0f, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0f, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0d, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0d, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0d, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0d, 2, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x06, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x06, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x0b, 0x07, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Playback Switch", 0x0b, 0x07, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
	},
	{ } /* end */
};

static struct hda_verb alc880_lg_init_verbs[] = {
	/* set capture source to mic-in */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	/* mute all amp mixer inputs */
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(5)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(6)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(7)},
	/* line-in to input */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* built-in mic */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* speaker-out */
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* mic-in to input */
	{0x11, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* HP-out */
	{0x13, AC_VERB_SET_CONNECT_SEL, 0x03},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* jack sense */
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | 0x1},
	{ }
};

/* toggle speaker-output according to the hp-jack state */
static void alc880_lg_automute(struct hda_codec *codec)
{
	unsigned int present;
	unsigned char bits;

	present = snd_hda_codec_read(codec, 0x1b, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x17, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
}

static void alc880_lg_unsol_event(struct hda_codec *codec, unsigned int res)
{
	/* Looks like the unsol event is incompatible with the standard
	 * definition.  4bit tag is placed at 28 bit!
	 */
	if ((res >> 28) == 0x01)
		alc880_lg_automute(codec);
}

/*
 * LG LW20
 *
 * Pin assignment:
 *   Speaker-out: 0x14
 *   Mic-In: 0x18
 *   Built-in Mic-In: 0x19
 *   Line-In: 0x1b
 *   HP-Out: 0x1a
 *   SPDIF-Out: 0x1e
 */

static struct hda_input_mux alc880_lg_lw_capture_source = {
	.num_items = 3,
	.items = {
		{ "Mic", 0x0 },
		{ "Internal Mic", 0x1 },
		{ "Line In", 0x2 },
	},
};

#define alc880_lg_lw_modes alc880_threestack_modes

static struct snd_kcontrol_new alc880_lg_lw_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0f, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0f, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Playback Switch", 0x0b, 0x01, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
	},
	{ } /* end */
};

static struct hda_verb alc880_lg_lw_init_verbs[] = {
	{0x13, AC_VERB_SET_CONNECT_SEL, 0x00}, /* HP */
	{0x10, AC_VERB_SET_CONNECT_SEL, 0x02}, /* mic/clfe */
	{0x12, AC_VERB_SET_CONNECT_SEL, 0x03}, /* line/surround */

	/* set capture source to mic-in */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(7)},
	/* speaker-out */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* HP-out */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* mic-in to input */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* built-in mic */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* jack sense */
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | 0x1},
	{ }
};

/* toggle speaker-output according to the hp-jack state */
static void alc880_lg_lw_automute(struct hda_codec *codec)
{
	unsigned int present;
	unsigned char bits;

	present = snd_hda_codec_read(codec, 0x1b, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
}

static void alc880_lg_lw_unsol_event(struct hda_codec *codec, unsigned int res)
{
	/* Looks like the unsol event is incompatible with the standard
	 * definition.  4bit tag is placed at 28 bit!
	 */
	if ((res >> 28) == 0x01)
		alc880_lg_lw_automute(codec);
}

static struct snd_kcontrol_new alc880_medion_rim_mixer[] = {
	HDA_CODEC_VOLUME("Master Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Master Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static struct hda_input_mux alc880_medion_rim_capture_source = {
	.num_items = 2,
	.items = {
		{ "Mic", 0x0 },
		{ "Internal Mic", 0x1 },
	},
};

static struct hda_verb alc880_medion_rim_init_verbs[] = {
	{0x13, AC_VERB_SET_CONNECT_SEL, 0x00}, /* HP */

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	/* Mic1 (rear panel) pin widget for input and vref at 80% */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Mic2 (as headphone out) for HP output */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Internal Speaker */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	{0x20, AC_VERB_SET_COEF_INDEX, 0x07},
	{0x20, AC_VERB_SET_PROC_COEF,  0x3060},

	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{ }
};

/* toggle speaker-output according to the hp-jack state */
static void alc880_medion_rim_automute(struct hda_codec *codec)
{
	unsigned int present;
	unsigned char bits;

	present = snd_hda_codec_read(codec, 0x14, 0,
				     AC_VERB_GET_PIN_SENSE, 0)
		& AC_PINSENSE_PRESENCE;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x1b, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
	if (present)
		snd_hda_codec_write(codec, 0x01, 0, AC_VERB_SET_GPIO_DATA, 0);
	else
		snd_hda_codec_write(codec, 0x01, 0, AC_VERB_SET_GPIO_DATA, 2);
}

static void alc880_medion_rim_unsol_event(struct hda_codec *codec,
					  unsigned int res)
{
	/* Looks like the unsol event is incompatible with the standard
	 * definition.  4bit tag is placed at 28 bit!
	 */
	if ((res >> 28) == ALC880_HP_EVENT)
		alc880_medion_rim_automute(codec);
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
static struct hda_amp_list alc880_loopbacks[] = {
	{ 0x0b, HDA_INPUT, 0 },
	{ 0x0b, HDA_INPUT, 1 },
	{ 0x0b, HDA_INPUT, 2 },
	{ 0x0b, HDA_INPUT, 3 },
	{ 0x0b, HDA_INPUT, 4 },
	{ } /* end */
};

static struct hda_amp_list alc880_lg_loopbacks[] = {
	{ 0x0b, HDA_INPUT, 1 },
	{ 0x0b, HDA_INPUT, 6 },
	{ 0x0b, HDA_INPUT, 7 },
	{ } /* end */
};
#endif

/*
 * Common callbacks
 */

static int alc_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	unsigned int i;

	alc_fix_pll(codec);
	if (codec->vendor_id == 0x10ec0888)
		alc888_coef_init(codec);

	for (i = 0; i < spec->num_init_verbs; i++)
		snd_hda_sequence_write(codec, spec->init_verbs[i]);

	if (spec->init_hook)
		spec->init_hook(codec);

	return 0;
}

static void alc_unsol_event(struct hda_codec *codec, unsigned int res)
{
	struct alc_spec *spec = codec->spec;

	if (spec->unsol_event)
		spec->unsol_event(codec, res);
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
static int alc_check_power_status(struct hda_codec *codec, hda_nid_t nid)
{
	struct alc_spec *spec = codec->spec;
	return snd_hda_check_amp_list_power(codec, &spec->loopback, nid);
	spec->gen.skip_verbs = 1; /* applied in below */
	snd_hda_gen_init(codec);
	alc_fix_pll(codec);
	alc_auto_init_amp(codec, spec->init_amp);
	snd_hda_apply_verbs(codec); /* apply verbs here after own init */

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_INIT);

	return 0;
}

static inline void alc_shutup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	if (spec && spec->shutup)
		spec->shutup(codec);
	else
		snd_hda_shutup_pins(codec);
}

static void alc_reboot_notify(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	if (spec && spec->reboot_notify)
		spec->reboot_notify(codec);
	else
		alc_shutup(codec);
}

/* power down codec to D3 at reboot/shutdown; set as reboot_notify ops */
static void alc_d3_at_reboot(struct hda_codec *codec)
{
	snd_hda_codec_set_power_to_all(codec, codec->core.afg, AC_PWRST_D3);
	snd_hda_codec_write(codec, codec->core.afg, 0,
			    AC_VERB_SET_POWER_STATE, AC_PWRST_D3);
	msleep(10);
}

#define alc_free	snd_hda_gen_free

#ifdef CONFIG_PM
static void alc_power_eapd(struct hda_codec *codec)
{
	alc_auto_setup_eapd(codec, false);
}

static int alc_suspend(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	alc_shutup(codec);
	if (spec && spec->power_hook)
		spec->power_hook(codec);
	return 0;
}
#endif

#ifdef CONFIG_PM
static int alc_resume(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	if (!spec->no_depop_delay)
		msleep(150); /* to avoid pop noise */
	codec->patch_ops.init(codec);
	regcache_sync(codec->core.regmap);
	hda_call_check_power_status(codec, 0x01);
	return 0;
}
#endif

/*
 * Analog playback callbacks
 */
static int alc880_playback_pcm_open(struct hda_pcm_stream *hinfo,
				    struct hda_codec *codec,
				    struct snd_pcm_substream *substream)
{
	struct alc_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_open(codec, &spec->multiout, substream,
					     hinfo);
}

static int alc880_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
				       struct hda_codec *codec,
				       unsigned int stream_tag,
				       unsigned int format,
				       struct snd_pcm_substream *substream)
{
	struct alc_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_prepare(codec, &spec->multiout,
						stream_tag, format, substream);
}

static int alc880_playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
				       struct hda_codec *codec,
				       struct snd_pcm_substream *substream)
{
	struct alc_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_cleanup(codec, &spec->multiout);
}

/*
 * Digital out
 */
static int alc880_dig_playback_pcm_open(struct hda_pcm_stream *hinfo,
					struct hda_codec *codec,
					struct snd_pcm_substream *substream)
{
	struct alc_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_open(codec, &spec->multiout);
}

static int alc880_dig_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
					   struct hda_codec *codec,
					   unsigned int stream_tag,
					   unsigned int format,
					   struct snd_pcm_substream *substream)
{
	struct alc_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_prepare(codec, &spec->multiout,
					     stream_tag, format, substream);
}

static int alc880_dig_playback_pcm_close(struct hda_pcm_stream *hinfo,
					 struct hda_codec *codec,
					 struct snd_pcm_substream *substream)
{
	struct alc_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_close(codec, &spec->multiout);
}

/*
 * Analog capture
 */
static int alc880_alt_capture_pcm_prepare(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      unsigned int stream_tag,
				      unsigned int format,
				      struct snd_pcm_substream *substream)
{
	struct alc_spec *spec = codec->spec;

	snd_hda_codec_setup_stream(codec, spec->adc_nids[substream->number + 1],
				   stream_tag, 0, format);
	return 0;
}

static int alc880_alt_capture_pcm_cleanup(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      struct snd_pcm_substream *substream)
{
	struct alc_spec *spec = codec->spec;

	snd_hda_codec_cleanup_stream(codec,
				     spec->adc_nids[substream->number + 1]);
	return 0;
}


/*
 */
static struct hda_pcm_stream alc880_pcm_analog_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 8,
	/* NID is set in alc_build_pcms */
	.ops = {
		.open = alc880_playback_pcm_open,
		.prepare = alc880_playback_pcm_prepare,
		.cleanup = alc880_playback_pcm_cleanup
	},
};

static struct hda_pcm_stream alc880_pcm_analog_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in alc_build_pcms */
};

static struct hda_pcm_stream alc880_pcm_analog_alt_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in alc_build_pcms */
};

static struct hda_pcm_stream alc880_pcm_analog_alt_capture = {
	.substreams = 2, /* can be overridden */
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in alc_build_pcms */
	.ops = {
		.prepare = alc880_alt_capture_pcm_prepare,
		.cleanup = alc880_alt_capture_pcm_cleanup
	},
};

static struct hda_pcm_stream alc880_pcm_digital_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in alc_build_pcms */
	.ops = {
		.open = alc880_dig_playback_pcm_open,
		.close = alc880_dig_playback_pcm_close,
		.prepare = alc880_dig_playback_pcm_prepare
	},
};

static struct hda_pcm_stream alc880_pcm_digital_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in alc_build_pcms */
};

/* Used by alc_build_pcms to flag that a PCM has no playback stream */
static struct hda_pcm_stream alc_pcm_null_stream = {
	.substreams = 0,
	.channels_min = 0,
	.channels_max = 0,
};

static int alc_build_pcms(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	struct hda_pcm *info = spec->pcm_rec;
	int i;

	codec->num_pcms = 1;
	codec->pcm_info = info;

	info->name = spec->stream_name_analog;
	if (spec->stream_analog_playback) {
		snd_assert(spec->multiout.dac_nids, return -EINVAL);
		info->stream[SNDRV_PCM_STREAM_PLAYBACK] = *(spec->stream_analog_playback);
		info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid = spec->multiout.dac_nids[0];
	}
	if (spec->stream_analog_capture) {
		snd_assert(spec->adc_nids, return -EINVAL);
		info->stream[SNDRV_PCM_STREAM_CAPTURE] = *(spec->stream_analog_capture);
		info->stream[SNDRV_PCM_STREAM_CAPTURE].nid = spec->adc_nids[0];
	}

	if (spec->channel_mode) {
		info->stream[SNDRV_PCM_STREAM_PLAYBACK].channels_max = 0;
		for (i = 0; i < spec->num_channel_mode; i++) {
			if (spec->channel_mode[i].channels > info->stream[SNDRV_PCM_STREAM_PLAYBACK].channels_max) {
				info->stream[SNDRV_PCM_STREAM_PLAYBACK].channels_max = spec->channel_mode[i].channels;
			}
		}
	}

	/* SPDIF for stream index #1 */
	if (spec->multiout.dig_out_nid || spec->dig_in_nid) {
		codec->num_pcms = 2;
		info = spec->pcm_rec + 1;
		info->name = spec->stream_name_digital;
		info->pcm_type = HDA_PCM_TYPE_SPDIF;
		if (spec->multiout.dig_out_nid &&
		    spec->stream_digital_playback) {
			info->stream[SNDRV_PCM_STREAM_PLAYBACK] = *(spec->stream_digital_playback);
			info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid = spec->multiout.dig_out_nid;
		}
		if (spec->dig_in_nid &&
		    spec->stream_digital_capture) {
			info->stream[SNDRV_PCM_STREAM_CAPTURE] = *(spec->stream_digital_capture);
			info->stream[SNDRV_PCM_STREAM_CAPTURE].nid = spec->dig_in_nid;
		}
	}

	/* If the use of more than one ADC is requested for the current
	 * model, configure a second analog capture-only PCM.
	 */
	/* Additional Analaog capture for index #2 */
	if ((spec->alt_dac_nid && spec->stream_analog_alt_playback) ||
	    (spec->num_adc_nids > 1 && spec->stream_analog_alt_capture)) {
		codec->num_pcms = 3;
		info = spec->pcm_rec + 2;
		info->name = spec->stream_name_analog;
		if (spec->alt_dac_nid) {
			info->stream[SNDRV_PCM_STREAM_PLAYBACK] =
				*spec->stream_analog_alt_playback;
			info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid =
				spec->alt_dac_nid;
		} else {
			info->stream[SNDRV_PCM_STREAM_PLAYBACK] =
				alc_pcm_null_stream;
			info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid = 0;
		}
		if (spec->num_adc_nids > 1) {
			info->stream[SNDRV_PCM_STREAM_CAPTURE] =
				*spec->stream_analog_alt_capture;
			info->stream[SNDRV_PCM_STREAM_CAPTURE].nid =
				spec->adc_nids[1];
			info->stream[SNDRV_PCM_STREAM_CAPTURE].substreams =
				spec->num_adc_nids - 1;
		} else {
			info->stream[SNDRV_PCM_STREAM_CAPTURE] =
				alc_pcm_null_stream;
			info->stream[SNDRV_PCM_STREAM_CAPTURE].nid = 0;
		}
	}

	return 0;
}

static void alc_free(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	unsigned int i;

	if (!spec)
		return;

	if (spec->kctl_alloc) {
		for (i = 0; i < spec->num_kctl_used; i++)
			kfree(spec->kctl_alloc[i].name);
		kfree(spec->kctl_alloc);
	}
	kfree(spec);
	codec->spec = NULL; /* to be sure */
}

/*
 */
static struct hda_codec_ops alc_patch_ops = {
	.build_controls = alc_build_controls,
	.build_pcms = alc_build_pcms,
	.init = alc_init,
	.free = alc_free,
	.unsol_event = alc_unsol_event,
#ifdef CONFIG_SND_HDA_POWER_SAVE
	.check_power_status = alc_check_power_status,
#endif
};


/*
 * Test configuration for debugging
 *
 * Almost all inputs/outputs are enabled.  I/O pins can be configured via
 * enum controls.
 */
#ifdef CONFIG_SND_DEBUG
static hda_nid_t alc880_test_dac_nids[4] = {
	0x02, 0x03, 0x04, 0x05
};

static struct hda_input_mux alc880_test_capture_source = {
	.num_items = 7,
	.items = {
		{ "In-1", 0x0 },
		{ "In-2", 0x1 },
		{ "In-3", 0x2 },
		{ "In-4", 0x3 },
		{ "CD", 0x4 },
		{ "Front", 0x5 },
		{ "Surround", 0x6 },
	},
};

static struct hda_channel_mode alc880_test_modes[4] = {
	{ 2, NULL },
	{ 4, NULL },
	{ 6, NULL },
	{ 8, NULL },
};

static int alc_test_pin_ctl_info(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	static char *texts[] = {
		"N/A", "Line Out", "HP Out",
		"In Hi-Z", "In 50%", "In Grd", "In 80%", "In 100%"
	};
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 8;
	if (uinfo->value.enumerated.item >= 8)
		uinfo->value.enumerated.item = 7;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int alc_test_pin_ctl_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = (hda_nid_t)kcontrol->private_value;
	unsigned int pin_ctl, item = 0;

	pin_ctl = snd_hda_codec_read(codec, nid, 0,
				     AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
	if (pin_ctl & AC_PINCTL_OUT_EN) {
		if (pin_ctl & AC_PINCTL_HP_EN)
			item = 2;
		else
			item = 1;
	} else if (pin_ctl & AC_PINCTL_IN_EN) {
		switch (pin_ctl & AC_PINCTL_VREFEN) {
		case AC_PINCTL_VREF_HIZ: item = 3; break;
		case AC_PINCTL_VREF_50:  item = 4; break;
		case AC_PINCTL_VREF_GRD: item = 5; break;
		case AC_PINCTL_VREF_80:  item = 6; break;
		case AC_PINCTL_VREF_100: item = 7; break;
		}
	}
	ucontrol->value.enumerated.item[0] = item;
	return 0;
}

static int alc_test_pin_ctl_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = (hda_nid_t)kcontrol->private_value;
	static unsigned int ctls[] = {
		0, AC_PINCTL_OUT_EN, AC_PINCTL_OUT_EN | AC_PINCTL_HP_EN,
		AC_PINCTL_IN_EN | AC_PINCTL_VREF_HIZ,
		AC_PINCTL_IN_EN | AC_PINCTL_VREF_50,
		AC_PINCTL_IN_EN | AC_PINCTL_VREF_GRD,
		AC_PINCTL_IN_EN | AC_PINCTL_VREF_80,
		AC_PINCTL_IN_EN | AC_PINCTL_VREF_100,
	};
	unsigned int old_ctl, new_ctl;

	old_ctl = snd_hda_codec_read(codec, nid, 0,
				     AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
	new_ctl = ctls[ucontrol->value.enumerated.item[0]];
	if (old_ctl != new_ctl) {
		int val;
		snd_hda_codec_write_cache(codec, nid, 0,
					  AC_VERB_SET_PIN_WIDGET_CONTROL,
					  new_ctl);
		val = ucontrol->value.enumerated.item[0] >= 3 ?
			HDA_AMP_MUTE : 0;
		snd_hda_codec_amp_stereo(codec, nid, HDA_OUTPUT, 0,
					 HDA_AMP_MUTE, val);
		return 1;
	}
	return 0;
}

static int alc_test_pin_src_info(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	static char *texts[] = {
		"Front", "Surround", "CLFE", "Side"
	};
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 4;
	if (uinfo->value.enumerated.item >= 4)
		uinfo->value.enumerated.item = 3;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int alc_test_pin_src_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = (hda_nid_t)kcontrol->private_value;
	unsigned int sel;

	sel = snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_CONNECT_SEL, 0);
	ucontrol->value.enumerated.item[0] = sel & 3;
	return 0;
}

static int alc_test_pin_src_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = (hda_nid_t)kcontrol->private_value;
	unsigned int sel;

	sel = snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_CONNECT_SEL, 0) & 3;
	if (ucontrol->value.enumerated.item[0] != sel) {
		sel = ucontrol->value.enumerated.item[0] & 3;
		snd_hda_codec_write_cache(codec, nid, 0,
					  AC_VERB_SET_CONNECT_SEL, sel);
		return 1;
	}
	return 0;
}

#define PIN_CTL_TEST(xname,nid) {			\
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,	\
			.name = xname,		       \
			.info = alc_test_pin_ctl_info, \
			.get = alc_test_pin_ctl_get,   \
			.put = alc_test_pin_ctl_put,   \
			.private_value = nid	       \
			}

#define PIN_SRC_TEST(xname,nid) {			\
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,	\
			.name = xname,		       \
			.info = alc_test_pin_src_info, \
			.get = alc_test_pin_src_get,   \
			.put = alc_test_pin_src_put,   \
			.private_value = nid	       \
			}

static struct snd_kcontrol_new alc880_test_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CLFE Playback Volume", 0x0e, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Side Playback Volume", 0x0f, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_BIND_MUTE("CLFE Playback Switch", 0x0e, 2, HDA_INPUT),
	HDA_BIND_MUTE("Side Playback Switch", 0x0f, 2, HDA_INPUT),
	PIN_CTL_TEST("Front Pin Mode", 0x14),
	PIN_CTL_TEST("Surround Pin Mode", 0x15),
	PIN_CTL_TEST("CLFE Pin Mode", 0x16),
	PIN_CTL_TEST("Side Pin Mode", 0x17),
	PIN_CTL_TEST("In-1 Pin Mode", 0x18),
	PIN_CTL_TEST("In-2 Pin Mode", 0x19),
	PIN_CTL_TEST("In-3 Pin Mode", 0x1a),
	PIN_CTL_TEST("In-4 Pin Mode", 0x1b),
	PIN_SRC_TEST("In-1 Pin Source", 0x18),
	PIN_SRC_TEST("In-2 Pin Source", 0x19),
	PIN_SRC_TEST("In-3 Pin Source", 0x1a),
	PIN_SRC_TEST("In-4 Pin Source", 0x1b),
	HDA_CODEC_VOLUME("In-1 Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("In-1 Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("In-2 Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("In-2 Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("In-3 Playback Volume", 0x0b, 0x2, HDA_INPUT),
	HDA_CODEC_MUTE("In-3 Playback Switch", 0x0b, 0x2, HDA_INPUT),
	HDA_CODEC_VOLUME("In-4 Playback Volume", 0x0b, 0x3, HDA_INPUT),
	HDA_CODEC_MUTE("In-4 Playback Switch", 0x0b, 0x3, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x4, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x4, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
	},
	{ } /* end */
};

static struct hda_verb alc880_test_init_verbs[] = {
	/* Unmute inputs of 0x0c - 0x0f */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	/* Vol output for 0x0c-0x0f */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	/* Set output pins 0x14-0x17 */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	/* Unmute output pins 0x14-0x17 */
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Set input pins 0x18-0x1c */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	/* Mute input pins 0x18-0x1b */
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* ADC set up */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x07, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* Analog input/passthru */
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	{ }
};
#endif

/*
 */

static const char *alc880_models[ALC880_MODEL_LAST] = {
	[ALC880_3ST]		= "3stack",
	[ALC880_TCL_S700]	= "tcl",
	[ALC880_3ST_DIG]	= "3stack-digout",
	[ALC880_CLEVO]		= "clevo",
	[ALC880_5ST]		= "5stack",
	[ALC880_5ST_DIG]	= "5stack-digout",
	[ALC880_W810]		= "w810",
	[ALC880_Z71V]		= "z71v",
	[ALC880_6ST]		= "6stack",
	[ALC880_6ST_DIG]	= "6stack-digout",
	[ALC880_ASUS]		= "asus",
	[ALC880_ASUS_W1V]	= "asus-w1v",
	[ALC880_ASUS_DIG]	= "asus-dig",
	[ALC880_ASUS_DIG2]	= "asus-dig2",
	[ALC880_UNIWILL_DIG]	= "uniwill",
	[ALC880_UNIWILL_P53]	= "uniwill-p53",
	[ALC880_FUJITSU]	= "fujitsu",
	[ALC880_F1734]		= "F1734",
	[ALC880_LG]		= "lg",
	[ALC880_LG_LW]		= "lg-lw",
	[ALC880_MEDION_RIM]	= "medion",
#ifdef CONFIG_SND_DEBUG
	[ALC880_TEST]		= "test",
#endif
	[ALC880_AUTO]		= "auto",
};

static struct snd_pci_quirk alc880_cfg_tbl[] = {
	SND_PCI_QUIRK(0x1019, 0x0f69, "Coeus G610P", ALC880_W810),
	SND_PCI_QUIRK(0x1019, 0xa880, "ECS", ALC880_5ST_DIG),
	SND_PCI_QUIRK(0x1019, 0xa884, "Acer APFV", ALC880_6ST),
	SND_PCI_QUIRK(0x1025, 0x0070, "ULI", ALC880_3ST_DIG),
	SND_PCI_QUIRK(0x1025, 0x0077, "ULI", ALC880_6ST_DIG),
	SND_PCI_QUIRK(0x1025, 0x0078, "ULI", ALC880_6ST_DIG),
	SND_PCI_QUIRK(0x1025, 0x0087, "ULI", ALC880_6ST_DIG),
	SND_PCI_QUIRK(0x1025, 0xe309, "ULI", ALC880_3ST_DIG),
	SND_PCI_QUIRK(0x1025, 0xe310, "ULI", ALC880_3ST),
	SND_PCI_QUIRK(0x1039, 0x1234, NULL, ALC880_6ST_DIG),
	SND_PCI_QUIRK(0x103c, 0x2a09, "HP", ALC880_5ST),
	SND_PCI_QUIRK(0x1043, 0x10b3, "ASUS W1V", ALC880_ASUS_W1V),
	SND_PCI_QUIRK(0x1043, 0x10c2, "ASUS W6A", ALC880_ASUS_DIG),
	SND_PCI_QUIRK(0x1043, 0x10c3, "ASUS Wxx", ALC880_ASUS_DIG),
	SND_PCI_QUIRK(0x1043, 0x1113, "ASUS", ALC880_ASUS_DIG),
	SND_PCI_QUIRK(0x1043, 0x1123, "ASUS", ALC880_ASUS_DIG),
	SND_PCI_QUIRK(0x1043, 0x1173, "ASUS", ALC880_ASUS_DIG),
	SND_PCI_QUIRK(0x1043, 0x1964, "ASUS Z71V", ALC880_Z71V),
	/* SND_PCI_QUIRK(0x1043, 0x1964, "ASUS", ALC880_ASUS_DIG), */
	SND_PCI_QUIRK(0x1043, 0x1973, "ASUS", ALC880_ASUS_DIG),
	SND_PCI_QUIRK(0x1043, 0x19b3, "ASUS", ALC880_ASUS_DIG),
	SND_PCI_QUIRK(0x1043, 0x814e, "ASUS P5GD1 w/SPDIF", ALC880_6ST_DIG),
	SND_PCI_QUIRK(0x1043, 0x8181, "ASUS P4GPL", ALC880_ASUS_DIG),
	SND_PCI_QUIRK(0x1043, 0x8196, "ASUS P5GD1", ALC880_6ST),
	SND_PCI_QUIRK(0x1043, 0x81b4, "ASUS", ALC880_6ST),
	SND_PCI_QUIRK(0x1043, 0, "ASUS", ALC880_ASUS), /* default ASUS */
	SND_PCI_QUIRK(0x104d, 0x81a0, "Sony", ALC880_3ST),
	SND_PCI_QUIRK(0x104d, 0x81d6, "Sony", ALC880_3ST),
	SND_PCI_QUIRK(0x107b, 0x3032, "Gateway", ALC880_5ST),
	SND_PCI_QUIRK(0x107b, 0x3033, "Gateway", ALC880_5ST),
	SND_PCI_QUIRK(0x107b, 0x4039, "Gateway", ALC880_5ST),
	SND_PCI_QUIRK(0x1297, 0xc790, "Shuttle ST20G5", ALC880_6ST_DIG),
	SND_PCI_QUIRK(0x1458, 0xa102, "Gigabyte K8", ALC880_6ST_DIG),
	SND_PCI_QUIRK(0x1462, 0x1150, "MSI", ALC880_6ST_DIG),
	SND_PCI_QUIRK(0x1509, 0x925d, "FIC P4M", ALC880_6ST_DIG),
	SND_PCI_QUIRK(0x1558, 0x0520, "Clevo m520G", ALC880_CLEVO),
	SND_PCI_QUIRK(0x1558, 0x0660, "Clevo m655n", ALC880_CLEVO),
	SND_PCI_QUIRK(0x1558, 0x5401, "ASUS", ALC880_ASUS_DIG2),
	SND_PCI_QUIRK(0x1565, 0x8202, "Biostar", ALC880_5ST_DIG),
	SND_PCI_QUIRK(0x1584, 0x9050, "Uniwill", ALC880_UNIWILL_DIG),
	SND_PCI_QUIRK(0x1584, 0x9054, "Uniwlll", ALC880_F1734),
	SND_PCI_QUIRK(0x1584, 0x9070, "Uniwill", ALC880_UNIWILL),
	SND_PCI_QUIRK(0x1584, 0x9077, "Uniwill P53", ALC880_UNIWILL_P53),
	SND_PCI_QUIRK(0x161f, 0x203d, "W810", ALC880_W810),
	SND_PCI_QUIRK(0x161f, 0x205d, "Medion Rim 2150", ALC880_MEDION_RIM),
	SND_PCI_QUIRK(0x1695, 0x400d, "EPoX", ALC880_5ST_DIG),
	SND_PCI_QUIRK(0x1695, 0x4012, "EPox EP-5LDA", ALC880_5ST_DIG),
	SND_PCI_QUIRK(0x1734, 0x107c, "FSC F1734", ALC880_F1734),
	SND_PCI_QUIRK(0x1734, 0x1094, "FSC Amilo M1451G", ALC880_FUJITSU),
	SND_PCI_QUIRK(0x1734, 0x10ac, "FSC", ALC880_UNIWILL),
	SND_PCI_QUIRK(0x1734, 0x10b0, "Fujitsu", ALC880_FUJITSU),
	SND_PCI_QUIRK(0x1854, 0x0018, "LG LW20", ALC880_LG_LW),
	SND_PCI_QUIRK(0x1854, 0x003b, "LG", ALC880_LG),
	SND_PCI_QUIRK(0x1854, 0x0068, "LG w1", ALC880_LG),
	SND_PCI_QUIRK(0x1854, 0x0077, "LG LW25", ALC880_LG_LW),
	SND_PCI_QUIRK(0x19db, 0x4188, "TCL S700", ALC880_TCL_S700),
	SND_PCI_QUIRK(0x2668, 0x8086, NULL, ALC880_6ST_DIG), /* broken BIOS */
	SND_PCI_QUIRK(0x8086, 0x2668, NULL, ALC880_6ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xa100, "Intel mobo", ALC880_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xd400, "Intel mobo", ALC880_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xd401, "Intel mobo", ALC880_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xd402, "Intel mobo", ALC880_3ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe224, "Intel mobo", ALC880_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe305, "Intel mobo", ALC880_3ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe308, "Intel mobo", ALC880_3ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe400, "Intel mobo", ALC880_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe401, "Intel mobo", ALC880_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe402, "Intel mobo", ALC880_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0, "Intel mobo", ALC880_3ST), /* default Intel */
	SND_PCI_QUIRK(0xa0a0, 0x0560, "AOpen i915GMm-HFS", ALC880_5ST_DIG),
	SND_PCI_QUIRK(0xe803, 0x1019, NULL, ALC880_6ST_DIG),
	{}
};

/*
 * ALC880 codec presets
 */
static struct alc_config_preset alc880_presets[] = {
	[ALC880_3ST] = {
		.mixers = { alc880_three_stack_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_3stack_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_dac_nids),
		.dac_nids = alc880_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc880_threestack_modes),
		.channel_mode = alc880_threestack_modes,
		.need_dac_fix = 1,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_3ST_DIG] = {
		.mixers = { alc880_three_stack_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_3stack_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_dac_nids),
		.dac_nids = alc880_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_threestack_modes),
		.channel_mode = alc880_threestack_modes,
		.need_dac_fix = 1,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_TCL_S700] = {
		.mixers = { alc880_tcl_s700_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_tcl_S700_init_verbs,
				alc880_gpio2_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_dac_nids),
		.dac_nids = alc880_dac_nids,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc880_2_jack_modes),
		.channel_mode = alc880_2_jack_modes,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_5ST] = {
		.mixers = { alc880_three_stack_mixer,
			    alc880_five_stack_mixer},
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_5stack_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_dac_nids),
		.dac_nids = alc880_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc880_fivestack_modes),
		.channel_mode = alc880_fivestack_modes,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_5ST_DIG] = {
		.mixers = { alc880_three_stack_mixer,
			    alc880_five_stack_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_5stack_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_dac_nids),
		.dac_nids = alc880_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_fivestack_modes),
		.channel_mode = alc880_fivestack_modes,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_6ST] = {
		.mixers = { alc880_six_stack_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_6stack_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_6st_dac_nids),
		.dac_nids = alc880_6st_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc880_sixstack_modes),
		.channel_mode = alc880_sixstack_modes,
		.input_mux = &alc880_6stack_capture_source,
	},
	[ALC880_6ST_DIG] = {
		.mixers = { alc880_six_stack_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_6stack_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_6st_dac_nids),
		.dac_nids = alc880_6st_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_sixstack_modes),
		.channel_mode = alc880_sixstack_modes,
		.input_mux = &alc880_6stack_capture_source,
	},
	[ALC880_W810] = {
		.mixers = { alc880_w810_base_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_w810_init_verbs,
				alc880_gpio2_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_w810_dac_nids),
		.dac_nids = alc880_w810_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_w810_modes),
		.channel_mode = alc880_w810_modes,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_Z71V] = {
		.mixers = { alc880_z71v_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_z71v_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_z71v_dac_nids),
		.dac_nids = alc880_z71v_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc880_2_jack_modes),
		.channel_mode = alc880_2_jack_modes,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_F1734] = {
		.mixers = { alc880_f1734_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_f1734_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_f1734_dac_nids),
		.dac_nids = alc880_f1734_dac_nids,
		.hp_nid = 0x02,
		.num_channel_mode = ARRAY_SIZE(alc880_2_jack_modes),
		.channel_mode = alc880_2_jack_modes,
		.input_mux = &alc880_f1734_capture_source,
		.unsol_event = alc880_uniwill_p53_unsol_event,
		.init_hook = alc880_uniwill_p53_hp_automute,
	},
	[ALC880_ASUS] = {
		.mixers = { alc880_asus_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_asus_init_verbs,
				alc880_gpio1_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_asus_dac_nids),
		.dac_nids = alc880_asus_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc880_asus_modes),
		.channel_mode = alc880_asus_modes,
		.need_dac_fix = 1,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_ASUS_DIG] = {
		.mixers = { alc880_asus_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_asus_init_verbs,
				alc880_gpio1_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_asus_dac_nids),
		.dac_nids = alc880_asus_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_asus_modes),
		.channel_mode = alc880_asus_modes,
		.need_dac_fix = 1,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_ASUS_DIG2] = {
		.mixers = { alc880_asus_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_asus_init_verbs,
				alc880_gpio2_init_verbs }, /* use GPIO2 */
		.num_dacs = ARRAY_SIZE(alc880_asus_dac_nids),
		.dac_nids = alc880_asus_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_asus_modes),
		.channel_mode = alc880_asus_modes,
		.need_dac_fix = 1,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_ASUS_W1V] = {
		.mixers = { alc880_asus_mixer, alc880_asus_w1v_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_asus_init_verbs,
				alc880_gpio1_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_asus_dac_nids),
		.dac_nids = alc880_asus_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_asus_modes),
		.channel_mode = alc880_asus_modes,
		.need_dac_fix = 1,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_UNIWILL_DIG] = {
		.mixers = { alc880_asus_mixer, alc880_pcbeep_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_asus_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_asus_dac_nids),
		.dac_nids = alc880_asus_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_asus_modes),
		.channel_mode = alc880_asus_modes,
		.need_dac_fix = 1,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_UNIWILL] = {
		.mixers = { alc880_uniwill_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_uniwill_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_asus_dac_nids),
		.dac_nids = alc880_asus_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_threestack_modes),
		.channel_mode = alc880_threestack_modes,
		.need_dac_fix = 1,
		.input_mux = &alc880_capture_source,
		.unsol_event = alc880_uniwill_unsol_event,
		.init_hook = alc880_uniwill_automute,
	},
	[ALC880_UNIWILL_P53] = {
		.mixers = { alc880_uniwill_p53_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_uniwill_p53_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_asus_dac_nids),
		.dac_nids = alc880_asus_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc880_w810_modes),
		.channel_mode = alc880_threestack_modes,
		.input_mux = &alc880_capture_source,
		.unsol_event = alc880_uniwill_p53_unsol_event,
		.init_hook = alc880_uniwill_p53_hp_automute,
	},
	[ALC880_FUJITSU] = {
		.mixers = { alc880_fujitsu_mixer,
			    alc880_pcbeep_mixer, },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_uniwill_p53_init_verbs,
	       			alc880_beep_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_dac_nids),
		.dac_nids = alc880_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_2_jack_modes),
		.channel_mode = alc880_2_jack_modes,
		.input_mux = &alc880_capture_source,
		.unsol_event = alc880_uniwill_p53_unsol_event,
		.init_hook = alc880_uniwill_p53_hp_automute,
	},
	[ALC880_CLEVO] = {
		.mixers = { alc880_three_stack_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_clevo_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_dac_nids),
		.dac_nids = alc880_dac_nids,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc880_threestack_modes),
		.channel_mode = alc880_threestack_modes,
		.need_dac_fix = 1,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_LG] = {
		.mixers = { alc880_lg_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_lg_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_lg_dac_nids),
		.dac_nids = alc880_lg_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_lg_ch_modes),
		.channel_mode = alc880_lg_ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc880_lg_capture_source,
		.unsol_event = alc880_lg_unsol_event,
		.init_hook = alc880_lg_automute,
#ifdef CONFIG_SND_HDA_POWER_SAVE
		.loopbacks = alc880_lg_loopbacks,
#endif
	},
	[ALC880_LG_LW] = {
		.mixers = { alc880_lg_lw_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_lg_lw_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_dac_nids),
		.dac_nids = alc880_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_lg_lw_modes),
		.channel_mode = alc880_lg_lw_modes,
		.input_mux = &alc880_lg_lw_capture_source,
		.unsol_event = alc880_lg_lw_unsol_event,
		.init_hook = alc880_lg_lw_automute,
	},
	[ALC880_MEDION_RIM] = {
		.mixers = { alc880_medion_rim_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_medion_rim_init_verbs,
				alc_gpio2_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_dac_nids),
		.dac_nids = alc880_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_2_jack_modes),
		.channel_mode = alc880_2_jack_modes,
		.input_mux = &alc880_medion_rim_capture_source,
		.unsol_event = alc880_medion_rim_unsol_event,
		.init_hook = alc880_medion_rim_automute,
	},
#ifdef CONFIG_SND_DEBUG
	[ALC880_TEST] = {
		.mixers = { alc880_test_mixer },
		.init_verbs = { alc880_test_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_test_dac_nids),
		.dac_nids = alc880_test_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_test_modes),
		.channel_mode = alc880_test_modes,
		.input_mux = &alc880_test_capture_source,
	},
#endif
};

/*
 * Automatic parse of I/O pins from the BIOS configuration
 */

#define NUM_CONTROL_ALLOC	32
#define NUM_VERB_ALLOC		32

enum {
	ALC_CTL_WIDGET_VOL,
	ALC_CTL_WIDGET_MUTE,
	ALC_CTL_BIND_MUTE,
};
static struct snd_kcontrol_new alc880_control_templates[] = {
	HDA_CODEC_VOLUME(NULL, 0, 0, 0),
	HDA_CODEC_MUTE(NULL, 0, 0, 0),
	HDA_BIND_MUTE(NULL, 0, 0, 0),
};

/* add dynamic controls */
static int add_control(struct alc_spec *spec, int type, const char *name,
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
	*knew = alc880_control_templates[type];
	knew->name = kstrdup(name, GFP_KERNEL);
	if (!knew->name)
		return -ENOMEM;
	knew->private_value = val;
	spec->num_kctl_used++;
	return 0;
}

#define alc880_is_fixed_pin(nid)	((nid) >= 0x14 && (nid) <= 0x17)
#define alc880_fixed_pin_idx(nid)	((nid) - 0x14)
#define alc880_is_multi_pin(nid)	((nid) >= 0x18)
#define alc880_multi_pin_idx(nid)	((nid) - 0x18)
#define alc880_is_input_pin(nid)	((nid) >= 0x18)
#define alc880_input_pin_idx(nid)	((nid) - 0x18)
#define alc880_idx_to_dac(nid)		((nid) + 0x02)
#define alc880_dac_to_idx(nid)		((nid) - 0x02)
#define alc880_idx_to_mixer(nid)	((nid) + 0x0c)
#define alc880_idx_to_selector(nid)	((nid) + 0x10)
#define ALC880_PIN_CD_NID		0x1c

/* fill in the dac_nids table from the parsed pin configuration */
static int alc880_auto_fill_dac_nids(struct alc_spec *spec,
				     const struct auto_pin_cfg *cfg)
{
	hda_nid_t nid;
	int assigned[4];
	int i, j;

	memset(assigned, 0, sizeof(assigned));
	spec->multiout.dac_nids = spec->private_dac_nids;

	/* check the pins hardwired to audio widget */
	for (i = 0; i < cfg->line_outs; i++) {
		nid = cfg->line_out_pins[i];
		if (alc880_is_fixed_pin(nid)) {
			int idx = alc880_fixed_pin_idx(nid);
			spec->multiout.dac_nids[i] = alc880_idx_to_dac(idx);
			assigned[idx] = 1;
		}
	}
	/* left pins can be connect to any audio widget */
	for (i = 0; i < cfg->line_outs; i++) {
		nid = cfg->line_out_pins[i];
		if (alc880_is_fixed_pin(nid))
			continue;
		/* search for an empty channel */
		for (j = 0; j < cfg->line_outs; j++) {
			if (!assigned[j]) {
				spec->multiout.dac_nids[i] =
					alc880_idx_to_dac(j);
				assigned[j] = 1;
				break;
			}
		}
	}
	spec->multiout.num_dacs = cfg->line_outs;
	return 0;
}

/* add playback controls from the parsed DAC table */
static int alc880_auto_create_multi_out_ctls(struct alc_spec *spec,
					     const struct auto_pin_cfg *cfg)
{
	char name[32];
	static const char *chname[4] = {
		"Front", "Surround", NULL /*CLFE*/, "Side"
	};
	hda_nid_t nid;
	int i, err;

	for (i = 0; i < cfg->line_outs; i++) {
		if (!spec->multiout.dac_nids[i])
			continue;
		nid = alc880_idx_to_mixer(alc880_dac_to_idx(spec->multiout.dac_nids[i]));
		if (i == 2) {
			/* Center/LFE */
			err = add_control(spec, ALC_CTL_WIDGET_VOL,
					  "Center Playback Volume",
					  HDA_COMPOSE_AMP_VAL(nid, 1, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
			err = add_control(spec, ALC_CTL_WIDGET_VOL,
					  "LFE Playback Volume",
					  HDA_COMPOSE_AMP_VAL(nid, 2, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
			err = add_control(spec, ALC_CTL_BIND_MUTE,
					  "Center Playback Switch",
					  HDA_COMPOSE_AMP_VAL(nid, 1, 2,
							      HDA_INPUT));
			if (err < 0)
				return err;
			err = add_control(spec, ALC_CTL_BIND_MUTE,
					  "LFE Playback Switch",
					  HDA_COMPOSE_AMP_VAL(nid, 2, 2,
							      HDA_INPUT));
			if (err < 0)
				return err;
		} else {
			sprintf(name, "%s Playback Volume", chname[i]);
			err = add_control(spec, ALC_CTL_WIDGET_VOL, name,
					  HDA_COMPOSE_AMP_VAL(nid, 3, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
			sprintf(name, "%s Playback Switch", chname[i]);
			err = add_control(spec, ALC_CTL_BIND_MUTE, name,
					  HDA_COMPOSE_AMP_VAL(nid, 3, 2,
							      HDA_INPUT));
			if (err < 0)
				return err;
		}
	}
	return 0;
}

/* add playback controls for speaker and HP outputs */
static int alc880_auto_create_extra_out(struct alc_spec *spec, hda_nid_t pin,
					const char *pfx)
{
	hda_nid_t nid;
	int err;
	char name[32];

	if (!pin)
		return 0;

	if (alc880_is_fixed_pin(pin)) {
		nid = alc880_idx_to_dac(alc880_fixed_pin_idx(pin));
		/* specify the DAC as the extra output */
		if (!spec->multiout.hp_nid)
			spec->multiout.hp_nid = nid;
		else
			spec->multiout.extra_out_nid[0] = nid;
		/* control HP volume/switch on the output mixer amp */
		nid = alc880_idx_to_mixer(alc880_fixed_pin_idx(pin));
		sprintf(name, "%s Playback Volume", pfx);
		err = add_control(spec, ALC_CTL_WIDGET_VOL, name,
				  HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT));
		if (err < 0)
			return err;
		sprintf(name, "%s Playback Switch", pfx);
		err = add_control(spec, ALC_CTL_BIND_MUTE, name,
				  HDA_COMPOSE_AMP_VAL(nid, 3, 2, HDA_INPUT));
		if (err < 0)
			return err;
	} else if (alc880_is_multi_pin(pin)) {
		/* set manual connection */
		/* we have only a switch on HP-out PIN */
		sprintf(name, "%s Playback Switch", pfx);
		err = add_control(spec, ALC_CTL_WIDGET_MUTE, name,
				  HDA_COMPOSE_AMP_VAL(pin, 3, 0, HDA_OUTPUT));
		if (err < 0)
			return err;
	}
	return 0;
}

/* create input playback/capture controls for the given pin */
static int new_analog_input(struct alc_spec *spec, hda_nid_t pin,
			    const char *ctlname,
			    int idx, hda_nid_t mix_nid)
{
	char name[32];
	int err;

	sprintf(name, "%s Playback Volume", ctlname);
	err = add_control(spec, ALC_CTL_WIDGET_VOL, name,
			  HDA_COMPOSE_AMP_VAL(mix_nid, 3, idx, HDA_INPUT));
	if (err < 0)
		return err;
	sprintf(name, "%s Playback Switch", ctlname);
	err = add_control(spec, ALC_CTL_WIDGET_MUTE, name,
			  HDA_COMPOSE_AMP_VAL(mix_nid, 3, idx, HDA_INPUT));
	if (err < 0)
		return err;
	return 0;
}

/* create playback/capture controls for input pins */
static int alc880_auto_create_analog_input_ctls(struct alc_spec *spec,
						const struct auto_pin_cfg *cfg)
{
	struct hda_input_mux *imux = &spec->private_imux;
	int i, err, idx;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		if (alc880_is_input_pin(cfg->input_pins[i])) {
			idx = alc880_input_pin_idx(cfg->input_pins[i]);
			err = new_analog_input(spec, cfg->input_pins[i],
					       auto_pin_cfg_labels[i],
					       idx, 0x0b);
			if (err < 0)
				return err;
			imux->items[imux->num_items].label =
				auto_pin_cfg_labels[i];
			imux->items[imux->num_items].index =
				alc880_input_pin_idx(cfg->input_pins[i]);
			imux->num_items++;
		}
	}
	return 0;
}

static void alc_set_pin_output(struct hda_codec *codec, hda_nid_t nid,
			       unsigned int pin_type)
{
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_PIN_WIDGET_CONTROL,
			    pin_type);
	/* unmute pin */
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_AMP_GAIN_MUTE,
			    AMP_OUT_UNMUTE);
}

static void alc880_auto_set_output_and_unmute(struct hda_codec *codec,
					      hda_nid_t nid, int pin_type,
					      int dac_idx)
{
	alc_set_pin_output(codec, nid, pin_type);
	/* need the manual connection? */
	if (alc880_is_multi_pin(nid)) {
		struct alc_spec *spec = codec->spec;
		int idx = alc880_multi_pin_idx(nid);
		snd_hda_codec_write(codec, alc880_idx_to_selector(idx), 0,
				    AC_VERB_SET_CONNECT_SEL,
				    alc880_dac_to_idx(spec->multiout.dac_nids[dac_idx]));
	}
}

static int get_pin_type(int line_out_type)
{
	if (line_out_type == AUTO_PIN_HP_OUT)
		return PIN_HP;
	else
		return PIN_OUT;
}

static void alc880_auto_init_multi_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i;
	
	alc_subsystem_id(codec, 0x15, 0x1b, 0x14);
	for (i = 0; i < spec->autocfg.line_outs; i++) {
		hda_nid_t nid = spec->autocfg.line_out_pins[i];
		int pin_type = get_pin_type(spec->autocfg.line_out_type);
		alc880_auto_set_output_and_unmute(codec, nid, pin_type, i);
	}
}

static void alc880_auto_init_extra_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t pin;

	pin = spec->autocfg.speaker_pins[0];
	if (pin) /* connect to front */
		alc880_auto_set_output_and_unmute(codec, pin, PIN_OUT, 0);
	pin = spec->autocfg.hp_pins[0];
	if (pin) /* connect to front */
		alc880_auto_set_output_and_unmute(codec, pin, PIN_HP, 0);
}

static void alc880_auto_init_analog_input(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		hda_nid_t nid = spec->autocfg.input_pins[i];
		if (alc880_is_input_pin(nid)) {
			snd_hda_codec_write(codec, nid, 0,
					    AC_VERB_SET_PIN_WIDGET_CONTROL,
					    i <= AUTO_PIN_FRONT_MIC ?
					    PIN_VREF80 : PIN_IN);
			if (nid != ALC880_PIN_CD_NID)
				snd_hda_codec_write(codec, nid, 0,
						    AC_VERB_SET_AMP_GAIN_MUTE,
						    AMP_OUT_MUTE);
		}
	}
}
 */
static const struct hda_codec_ops alc_patch_ops = {
	.build_controls = alc_build_controls,
	.build_pcms = snd_hda_gen_build_pcms,
	.init = alc_init,
	.free = alc_free,
	.unsol_event = snd_hda_jack_unsol_event,
#ifdef CONFIG_PM
	.resume = alc_resume,
	.suspend = alc_suspend,
	.check_power_status = snd_hda_gen_check_power_status,
#endif
	.reboot_notify = alc_reboot_notify,
};


#define alc_codec_rename(codec, name) snd_hda_codec_set_name(codec, name)

/*
 * Rename codecs appropriately from COEF value or subvendor id
 */
struct alc_codec_rename_table {
	unsigned int vendor_id;
	unsigned short coef_mask;
	unsigned short coef_bits;
	const char *name;
};

struct alc_codec_rename_pci_table {
	unsigned int codec_vendor_id;
	unsigned short pci_subvendor;
	unsigned short pci_subdevice;
	const char *name;
};

static struct alc_codec_rename_table rename_tbl[] = {
	{ 0x10ec0221, 0xf00f, 0x1003, "ALC231" },
	{ 0x10ec0269, 0xfff0, 0x3010, "ALC277" },
	{ 0x10ec0269, 0xf0f0, 0x2010, "ALC259" },
	{ 0x10ec0269, 0xf0f0, 0x3010, "ALC258" },
	{ 0x10ec0269, 0x00f0, 0x0010, "ALC269VB" },
	{ 0x10ec0269, 0xffff, 0xa023, "ALC259" },
	{ 0x10ec0269, 0xffff, 0x6023, "ALC281X" },
	{ 0x10ec0269, 0x00f0, 0x0020, "ALC269VC" },
	{ 0x10ec0269, 0x00f0, 0x0030, "ALC269VD" },
	{ 0x10ec0662, 0xffff, 0x4020, "ALC656" },
	{ 0x10ec0887, 0x00f0, 0x0030, "ALC887-VD" },
	{ 0x10ec0888, 0x00f0, 0x0030, "ALC888-VD" },
	{ 0x10ec0888, 0xf0f0, 0x3020, "ALC886" },
	{ 0x10ec0899, 0x2000, 0x2000, "ALC899" },
	{ 0x10ec0892, 0xffff, 0x8020, "ALC661" },
	{ 0x10ec0892, 0xffff, 0x8011, "ALC661" },
	{ 0x10ec0892, 0xffff, 0x4011, "ALC656" },
	{ } /* terminator */
};

static struct alc_codec_rename_pci_table rename_pci_tbl[] = {
	{ 0x10ec0280, 0x1028, 0, "ALC3220" },
	{ 0x10ec0282, 0x1028, 0, "ALC3221" },
	{ 0x10ec0283, 0x1028, 0, "ALC3223" },
	{ 0x10ec0288, 0x1028, 0, "ALC3263" },
	{ 0x10ec0292, 0x1028, 0, "ALC3226" },
	{ 0x10ec0293, 0x1028, 0, "ALC3235" },
	{ 0x10ec0255, 0x1028, 0, "ALC3234" },
	{ 0x10ec0668, 0x1028, 0, "ALC3661" },
	{ 0x10ec0275, 0x1028, 0, "ALC3260" },
	{ 0x10ec0899, 0x1028, 0, "ALC3861" },
	{ 0x10ec0298, 0x1028, 0, "ALC3266" },
	{ 0x10ec0236, 0x1028, 0, "ALC3204" },
	{ 0x10ec0256, 0x1028, 0, "ALC3246" },
	{ 0x10ec0225, 0x1028, 0, "ALC3253" },
	{ 0x10ec0295, 0x1028, 0, "ALC3254" },
	{ 0x10ec0299, 0x1028, 0, "ALC3271" },
	{ 0x10ec0670, 0x1025, 0, "ALC669X" },
	{ 0x10ec0676, 0x1025, 0, "ALC679X" },
	{ 0x10ec0282, 0x1043, 0, "ALC3229" },
	{ 0x10ec0233, 0x1043, 0, "ALC3236" },
	{ 0x10ec0280, 0x103c, 0, "ALC3228" },
	{ 0x10ec0282, 0x103c, 0, "ALC3227" },
	{ 0x10ec0286, 0x103c, 0, "ALC3242" },
	{ 0x10ec0290, 0x103c, 0, "ALC3241" },
	{ 0x10ec0668, 0x103c, 0, "ALC3662" },
	{ 0x10ec0283, 0x17aa, 0, "ALC3239" },
	{ 0x10ec0292, 0x17aa, 0, "ALC3232" },
	{ } /* terminator */
};

static int alc_codec_rename_from_preset(struct hda_codec *codec)
{
	const struct alc_codec_rename_table *p;
	const struct alc_codec_rename_pci_table *q;

	for (p = rename_tbl; p->vendor_id; p++) {
		if (p->vendor_id != codec->core.vendor_id)
			continue;
		if ((alc_get_coef0(codec) & p->coef_mask) == p->coef_bits)
			return alc_codec_rename(codec, p->name);
	}

	if (!codec->bus->pci)
		return 0;
	for (q = rename_pci_tbl; q->codec_vendor_id; q++) {
		if (q->codec_vendor_id != codec->core.vendor_id)
			continue;
		if (q->pci_subvendor != codec->bus->pci->subsystem_vendor)
			continue;
		if (!q->pci_subdevice ||
		    q->pci_subdevice == codec->bus->pci->subsystem_device)
			return alc_codec_rename(codec, q->name);
	}

	return 0;
}


/*
 * Digital-beep handlers
 */
#ifdef CONFIG_SND_HDA_INPUT_BEEP
#define set_beep_amp(spec, nid, idx, dir) \
	((spec)->beep_amp = HDA_COMPOSE_AMP_VAL(nid, 3, idx, dir))

static const struct snd_pci_quirk beep_white_list[] = {
	SND_PCI_QUIRK(0x1043, 0x103c, "ASUS", 1),
	SND_PCI_QUIRK(0x1043, 0x115d, "ASUS", 1),
	SND_PCI_QUIRK(0x1043, 0x829f, "ASUS", 1),
	SND_PCI_QUIRK(0x1043, 0x8376, "EeePC", 1),
	SND_PCI_QUIRK(0x1043, 0x83ce, "EeePC", 1),
	SND_PCI_QUIRK(0x1043, 0x831a, "EeePC", 1),
	SND_PCI_QUIRK(0x1043, 0x834a, "EeePC", 1),
	SND_PCI_QUIRK(0x1458, 0xa002, "GA-MA790X", 1),
	SND_PCI_QUIRK(0x8086, 0xd613, "Intel", 1),
	/* blacklist -- no beep available */
	SND_PCI_QUIRK(0x17aa, 0x309e, "Lenovo ThinkCentre M73", 0),
	SND_PCI_QUIRK(0x17aa, 0x30a3, "Lenovo ThinkCentre M93", 0),
	{}
};

static inline int has_cdefine_beep(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	const struct snd_pci_quirk *q;
	q = snd_pci_quirk_lookup(codec->bus->pci, beep_white_list);
	if (q)
		return q->value;
	return spec->cdefine.enable_pcbeep;
}
#else
#define set_beep_amp(spec, nid, idx, dir) /* NOP */
#define has_cdefine_beep(codec)		0
#endif

/* parse the BIOS configuration and set up the alc_spec */
/* return 1 if successful, 0 if the proper config is not found,
 * or a negative error code
 */
static int alc880_parse_auto_config(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int err;
	static hda_nid_t alc880_ignore[] = { 0x1d, 0 };

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg,
					   alc880_ignore);
	if (err < 0)
		return err;
	if (!spec->autocfg.line_outs)
		return 0; /* can't find valid BIOS pin config */

	err = alc880_auto_fill_dac_nids(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = alc880_auto_create_multi_out_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = alc880_auto_create_extra_out(spec,
					   spec->autocfg.speaker_pins[0],
					   "Speaker");
	if (err < 0)
		return err;
	err = alc880_auto_create_extra_out(spec, spec->autocfg.hp_pins[0],
					   "Headphone");
	if (err < 0)
		return err;
	err = alc880_auto_create_analog_input_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;

	if (spec->autocfg.dig_out_pin)
		spec->multiout.dig_out_nid = ALC880_DIGOUT_NID;
	if (spec->autocfg.dig_in_pin)
		spec->dig_in_nid = ALC880_DIGIN_NID;

	if (spec->kctl_alloc)
		spec->mixers[spec->num_mixers++] = spec->kctl_alloc;

	spec->init_verbs[spec->num_init_verbs++] = alc880_volume_init_verbs;

	spec->num_mux_defs = 1;
	spec->input_mux = &spec->private_imux;
static int alc_parse_auto_config(struct hda_codec *codec,
				 const hda_nid_t *ignore_nids,
				 const hda_nid_t *ssid_nids)
{
	struct alc_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->gen.autocfg;
	int err;

	err = snd_hda_parse_pin_defcfg(codec, cfg, ignore_nids,
				       spec->parse_flags);
	if (err < 0)
		return err;

	if (ssid_nids)
		alc_ssid_check(codec, ssid_nids);

	err = snd_hda_gen_parse_auto_config(codec, cfg);
	if (err < 0)
		return err;

	return 1;
}

/* additional initialization for auto-configuration model */
static void alc880_auto_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	alc880_auto_init_multi_out(codec);
	alc880_auto_init_extra_out(codec);
	alc880_auto_init_analog_input(codec);
	if (spec->unsol_event)
		alc_sku_automute(codec);
}

/*
 * OK, here we have finally the patch for ALC880
 */

static int patch_alc880(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int board_config;
	int err;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	board_config = snd_hda_check_board_config(codec, ALC880_MODEL_LAST,
						  alc880_models,
						  alc880_cfg_tbl);
	if (board_config < 0) {
		printk(KERN_INFO "hda_codec: Unknown model for ALC880, "
		       "trying auto-probe from BIOS...\n");
		board_config = ALC880_AUTO;
	}

	if (board_config == ALC880_AUTO) {
		/* automatic parse from the BIOS config */
		err = alc880_parse_auto_config(codec);
		if (err < 0) {
			alc_free(codec);
			return err;
		} else if (!err) {
			printk(KERN_INFO
			       "hda_codec: Cannot set up configuration "
			       "from BIOS.  Using 3-stack mode...\n");
			board_config = ALC880_3ST;
		}
	}

	if (board_config != ALC880_AUTO)
		setup_preset(spec, &alc880_presets[board_config]);

	spec->stream_name_analog = "ALC880 Analog";
	spec->stream_analog_playback = &alc880_pcm_analog_playback;
	spec->stream_analog_capture = &alc880_pcm_analog_capture;
	spec->stream_analog_alt_capture = &alc880_pcm_analog_alt_capture;

	spec->stream_name_digital = "ALC880 Digital";
	spec->stream_digital_playback = &alc880_pcm_digital_playback;
	spec->stream_digital_capture = &alc880_pcm_digital_capture;

	if (!spec->adc_nids && spec->input_mux) {
		/* check whether NID 0x07 is valid */
		unsigned int wcap = get_wcaps(codec, alc880_adc_nids[0]);
		/* get type */
		wcap = (wcap & AC_WCAP_TYPE) >> AC_WCAP_TYPE_SHIFT;
		if (wcap != AC_WID_AUD_IN) {
			spec->adc_nids = alc880_adc_nids_alt;
			spec->num_adc_nids = ARRAY_SIZE(alc880_adc_nids_alt);
			spec->mixers[spec->num_mixers] =
				alc880_capture_alt_mixer;
			spec->num_mixers++;
		} else {
			spec->adc_nids = alc880_adc_nids;
			spec->num_adc_nids = ARRAY_SIZE(alc880_adc_nids);
			spec->mixers[spec->num_mixers] = alc880_capture_mixer;
			spec->num_mixers++;
		}
	}

	spec->vmaster_nid = 0x0c;

	codec->patch_ops = alc_patch_ops;
	if (board_config == ALC880_AUTO)
		spec->init_hook = alc880_auto_init;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	if (!spec->loopback.amplist)
		spec->loopback.amplist = alc880_loopbacks;
#endif

	return 0;
/* common preparation job for alc_spec */
static int alc_alloc_spec(struct hda_codec *codec, hda_nid_t mixer_nid)
{
	struct alc_spec *spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	int err;

	if (!spec)
		return -ENOMEM;
	codec->spec = spec;
	snd_hda_gen_spec_init(&spec->gen);
	spec->gen.mixer_nid = mixer_nid;
	spec->gen.own_eapd_ctl = 1;
	codec->single_adc_amp = 1;
	/* FIXME: do we need this for all Realtek codec models? */
	codec->spdif_status_reset = 1;
	codec->patch_ops = alc_patch_ops;

	err = alc_codec_rename_from_preset(codec);
	if (err < 0) {
		kfree(spec);
		return err;
	}
	return 0;
}

static int alc880_parse_auto_config(struct hda_codec *codec)
{
	static const hda_nid_t alc880_ignore[] = { 0x1d, 0 };
	static const hda_nid_t alc880_ssids[] = { 0x15, 0x1b, 0x14, 0 };
	return alc_parse_auto_config(codec, alc880_ignore, alc880_ssids);
}

/*
 * ALC880 fix-ups
 */
enum {
	ALC880_FIXUP_GPIO1,
	ALC880_FIXUP_GPIO2,
	ALC880_FIXUP_MEDION_RIM,
	ALC880_FIXUP_LG,
	ALC880_FIXUP_LG_LW25,
	ALC880_FIXUP_W810,
	ALC880_FIXUP_EAPD_COEF,
	ALC880_FIXUP_TCL_S700,
	ALC880_FIXUP_VOL_KNOB,
	ALC880_FIXUP_FUJITSU,
	ALC880_FIXUP_F1734,
	ALC880_FIXUP_UNIWILL,
	ALC880_FIXUP_UNIWILL_DIG,
	ALC880_FIXUP_Z71V,
	ALC880_FIXUP_ASUS_W5A,
	ALC880_FIXUP_3ST_BASE,
	ALC880_FIXUP_3ST,
	ALC880_FIXUP_3ST_DIG,
	ALC880_FIXUP_5ST_BASE,
	ALC880_FIXUP_5ST,
	ALC880_FIXUP_5ST_DIG,
	ALC880_FIXUP_6ST_BASE,
	ALC880_FIXUP_6ST,
	ALC880_FIXUP_6ST_DIG,
	ALC880_FIXUP_6ST_AUTOMUTE,
};

/* enable the volume-knob widget support on NID 0x21 */
static void alc880_fixup_vol_knob(struct hda_codec *codec,
				  const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PROBE)
		snd_hda_jack_detect_enable_callback(codec, 0x21,
						    alc_update_knob_master);
}

static const struct hda_fixup alc880_fixups[] = {
	[ALC880_FIXUP_GPIO1] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = alc_gpio1_init_verbs,
	},
	[ALC880_FIXUP_GPIO2] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = alc_gpio2_init_verbs,
	},
	[ALC880_FIXUP_MEDION_RIM] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF,  0x3060 },
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_GPIO2,
	},
	[ALC880_FIXUP_LG] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			/* disable bogus unused pins */
			{ 0x16, 0x411111f0 },
			{ 0x18, 0x411111f0 },
			{ 0x1a, 0x411111f0 },
			{ }
		}
	},
	[ALC880_FIXUP_LG_LW25] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1a, 0x0181344f }, /* line-in */
			{ 0x1b, 0x0321403f }, /* headphone */
			{ }
		}
	},
	[ALC880_FIXUP_W810] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			/* disable bogus unused pins */
			{ 0x17, 0x411111f0 },
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_GPIO2,
	},
	[ALC880_FIXUP_EAPD_COEF] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* change to EAPD mode */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF,  0x3060 },
			{}
		},
	},
	[ALC880_FIXUP_TCL_S700] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* change to EAPD mode */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF,  0x3070 },
			{}
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_GPIO2,
	},
	[ALC880_FIXUP_VOL_KNOB] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc880_fixup_vol_knob,
	},
	[ALC880_FIXUP_FUJITSU] = {
		/* override all pins as BIOS on old Amilo is broken */
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x0121401f }, /* HP */
			{ 0x15, 0x99030120 }, /* speaker */
			{ 0x16, 0x99030130 }, /* bass speaker */
			{ 0x17, 0x411111f0 }, /* N/A */
			{ 0x18, 0x411111f0 }, /* N/A */
			{ 0x19, 0x01a19950 }, /* mic-in */
			{ 0x1a, 0x411111f0 }, /* N/A */
			{ 0x1b, 0x411111f0 }, /* N/A */
			{ 0x1c, 0x411111f0 }, /* N/A */
			{ 0x1d, 0x411111f0 }, /* N/A */
			{ 0x1e, 0x01454140 }, /* SPDIF out */
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_VOL_KNOB,
	},
	[ALC880_FIXUP_F1734] = {
		/* almost compatible with FUJITSU, but no bass and SPDIF */
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x0121401f }, /* HP */
			{ 0x15, 0x99030120 }, /* speaker */
			{ 0x16, 0x411111f0 }, /* N/A */
			{ 0x17, 0x411111f0 }, /* N/A */
			{ 0x18, 0x411111f0 }, /* N/A */
			{ 0x19, 0x01a19950 }, /* mic-in */
			{ 0x1a, 0x411111f0 }, /* N/A */
			{ 0x1b, 0x411111f0 }, /* N/A */
			{ 0x1c, 0x411111f0 }, /* N/A */
			{ 0x1d, 0x411111f0 }, /* N/A */
			{ 0x1e, 0x411111f0 }, /* N/A */
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_VOL_KNOB,
	},
	[ALC880_FIXUP_UNIWILL] = {
		/* need to fix HP and speaker pins to be parsed correctly */
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x0121411f }, /* HP */
			{ 0x15, 0x99030120 }, /* speaker */
			{ 0x16, 0x99030130 }, /* bass speaker */
			{ }
		},
	},
	[ALC880_FIXUP_UNIWILL_DIG] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			/* disable bogus unused pins */
			{ 0x17, 0x411111f0 },
			{ 0x19, 0x411111f0 },
			{ 0x1b, 0x411111f0 },
			{ 0x1f, 0x411111f0 },
			{ }
		}
	},
	[ALC880_FIXUP_Z71V] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			/* set up the whole pins as BIOS is utterly broken */
			{ 0x14, 0x99030120 }, /* speaker */
			{ 0x15, 0x0121411f }, /* HP */
			{ 0x16, 0x411111f0 }, /* N/A */
			{ 0x17, 0x411111f0 }, /* N/A */
			{ 0x18, 0x01a19950 }, /* mic-in */
			{ 0x19, 0x411111f0 }, /* N/A */
			{ 0x1a, 0x01813031 }, /* line-in */
			{ 0x1b, 0x411111f0 }, /* N/A */
			{ 0x1c, 0x411111f0 }, /* N/A */
			{ 0x1d, 0x411111f0 }, /* N/A */
			{ 0x1e, 0x0144111e }, /* SPDIF */
			{ }
		}
	},
	[ALC880_FIXUP_ASUS_W5A] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			/* set up the whole pins as BIOS is utterly broken */
			{ 0x14, 0x0121411f }, /* HP */
			{ 0x15, 0x411111f0 }, /* N/A */
			{ 0x16, 0x411111f0 }, /* N/A */
			{ 0x17, 0x411111f0 }, /* N/A */
			{ 0x18, 0x90a60160 }, /* mic */
			{ 0x19, 0x411111f0 }, /* N/A */
			{ 0x1a, 0x411111f0 }, /* N/A */
			{ 0x1b, 0x411111f0 }, /* N/A */
			{ 0x1c, 0x411111f0 }, /* N/A */
			{ 0x1d, 0x411111f0 }, /* N/A */
			{ 0x1e, 0xb743111e }, /* SPDIF out */
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_GPIO1,
	},
	[ALC880_FIXUP_3ST_BASE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x01014010 }, /* line-out */
			{ 0x15, 0x411111f0 }, /* N/A */
			{ 0x16, 0x411111f0 }, /* N/A */
			{ 0x17, 0x411111f0 }, /* N/A */
			{ 0x18, 0x01a19c30 }, /* mic-in */
			{ 0x19, 0x0121411f }, /* HP */
			{ 0x1a, 0x01813031 }, /* line-in */
			{ 0x1b, 0x02a19c40 }, /* front-mic */
			{ 0x1c, 0x411111f0 }, /* N/A */
			{ 0x1d, 0x411111f0 }, /* N/A */
			/* 0x1e is filled in below */
			{ 0x1f, 0x411111f0 }, /* N/A */
			{ }
		}
	},
	[ALC880_FIXUP_3ST] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1e, 0x411111f0 }, /* N/A */
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_3ST_BASE,
	},
	[ALC880_FIXUP_3ST_DIG] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1e, 0x0144111e }, /* SPDIF */
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_3ST_BASE,
	},
	[ALC880_FIXUP_5ST_BASE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x01014010 }, /* front */
			{ 0x15, 0x411111f0 }, /* N/A */
			{ 0x16, 0x01011411 }, /* CLFE */
			{ 0x17, 0x01016412 }, /* surr */
			{ 0x18, 0x01a19c30 }, /* mic-in */
			{ 0x19, 0x0121411f }, /* HP */
			{ 0x1a, 0x01813031 }, /* line-in */
			{ 0x1b, 0x02a19c40 }, /* front-mic */
			{ 0x1c, 0x411111f0 }, /* N/A */
			{ 0x1d, 0x411111f0 }, /* N/A */
			/* 0x1e is filled in below */
			{ 0x1f, 0x411111f0 }, /* N/A */
			{ }
		}
	},
	[ALC880_FIXUP_5ST] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1e, 0x411111f0 }, /* N/A */
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_5ST_BASE,
	},
	[ALC880_FIXUP_5ST_DIG] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1e, 0x0144111e }, /* SPDIF */
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_5ST_BASE,
	},
	[ALC880_FIXUP_6ST_BASE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x01014010 }, /* front */
			{ 0x15, 0x01016412 }, /* surr */
			{ 0x16, 0x01011411 }, /* CLFE */
			{ 0x17, 0x01012414 }, /* side */
			{ 0x18, 0x01a19c30 }, /* mic-in */
			{ 0x19, 0x02a19c40 }, /* front-mic */
			{ 0x1a, 0x01813031 }, /* line-in */
			{ 0x1b, 0x0121411f }, /* HP */
			{ 0x1c, 0x411111f0 }, /* N/A */
			{ 0x1d, 0x411111f0 }, /* N/A */
			/* 0x1e is filled in below */
			{ 0x1f, 0x411111f0 }, /* N/A */
			{ }
		}
	},
	[ALC880_FIXUP_6ST] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1e, 0x411111f0 }, /* N/A */
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_6ST_BASE,
	},
	[ALC880_FIXUP_6ST_DIG] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1e, 0x0144111e }, /* SPDIF */
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_6ST_BASE,
	},
	[ALC880_FIXUP_6ST_AUTOMUTE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1b, 0x0121401f }, /* HP with jack detect */
			{ }
		},
		.chained_before = true,
		.chain_id = ALC880_FIXUP_6ST_BASE,
	},
};

static const struct snd_pci_quirk alc880_fixup_tbl[] = {
	SND_PCI_QUIRK(0x1019, 0x0f69, "Coeus G610P", ALC880_FIXUP_W810),
	SND_PCI_QUIRK(0x1043, 0x10c3, "ASUS W5A", ALC880_FIXUP_ASUS_W5A),
	SND_PCI_QUIRK(0x1043, 0x1964, "ASUS Z71V", ALC880_FIXUP_Z71V),
	SND_PCI_QUIRK_VENDOR(0x1043, "ASUS", ALC880_FIXUP_GPIO1),
	SND_PCI_QUIRK(0x147b, 0x1045, "ABit AA8XE", ALC880_FIXUP_6ST_AUTOMUTE),
	SND_PCI_QUIRK(0x1558, 0x5401, "Clevo GPIO2", ALC880_FIXUP_GPIO2),
	SND_PCI_QUIRK_VENDOR(0x1558, "Clevo", ALC880_FIXUP_EAPD_COEF),
	SND_PCI_QUIRK(0x1584, 0x9050, "Uniwill", ALC880_FIXUP_UNIWILL_DIG),
	SND_PCI_QUIRK(0x1584, 0x9054, "Uniwill", ALC880_FIXUP_F1734),
	SND_PCI_QUIRK(0x1584, 0x9070, "Uniwill", ALC880_FIXUP_UNIWILL),
	SND_PCI_QUIRK(0x1584, 0x9077, "Uniwill P53", ALC880_FIXUP_VOL_KNOB),
	SND_PCI_QUIRK(0x161f, 0x203d, "W810", ALC880_FIXUP_W810),
	SND_PCI_QUIRK(0x161f, 0x205d, "Medion Rim 2150", ALC880_FIXUP_MEDION_RIM),
	SND_PCI_QUIRK(0x1631, 0xe011, "PB 13201056", ALC880_FIXUP_6ST_AUTOMUTE),
	SND_PCI_QUIRK(0x1734, 0x107c, "FSC Amilo M1437", ALC880_FIXUP_FUJITSU),
	SND_PCI_QUIRK(0x1734, 0x1094, "FSC Amilo M1451G", ALC880_FIXUP_FUJITSU),
	SND_PCI_QUIRK(0x1734, 0x10ac, "FSC AMILO Xi 1526", ALC880_FIXUP_F1734),
	SND_PCI_QUIRK(0x1734, 0x10b0, "FSC Amilo Pi1556", ALC880_FIXUP_FUJITSU),
	SND_PCI_QUIRK(0x1854, 0x003b, "LG", ALC880_FIXUP_LG),
	SND_PCI_QUIRK(0x1854, 0x005f, "LG P1 Express", ALC880_FIXUP_LG),
	SND_PCI_QUIRK(0x1854, 0x0068, "LG w1", ALC880_FIXUP_LG),
	SND_PCI_QUIRK(0x1854, 0x0077, "LG LW25", ALC880_FIXUP_LG_LW25),
	SND_PCI_QUIRK(0x19db, 0x4188, "TCL S700", ALC880_FIXUP_TCL_S700),

	/* Below is the copied entries from alc880_quirks.c.
	 * It's not quite sure whether BIOS sets the correct pin-config table
	 * on these machines, thus they are kept to be compatible with
	 * the old static quirks.  Once when it's confirmed to work without
	 * these overrides, it'd be better to remove.
	 */
	SND_PCI_QUIRK(0x1019, 0xa880, "ECS", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x1019, 0xa884, "Acer APFV", ALC880_FIXUP_6ST),
	SND_PCI_QUIRK(0x1025, 0x0070, "ULI", ALC880_FIXUP_3ST_DIG),
	SND_PCI_QUIRK(0x1025, 0x0077, "ULI", ALC880_FIXUP_6ST_DIG),
	SND_PCI_QUIRK(0x1025, 0x0078, "ULI", ALC880_FIXUP_6ST_DIG),
	SND_PCI_QUIRK(0x1025, 0x0087, "ULI", ALC880_FIXUP_6ST_DIG),
	SND_PCI_QUIRK(0x1025, 0xe309, "ULI", ALC880_FIXUP_3ST_DIG),
	SND_PCI_QUIRK(0x1025, 0xe310, "ULI", ALC880_FIXUP_3ST),
	SND_PCI_QUIRK(0x1039, 0x1234, NULL, ALC880_FIXUP_6ST_DIG),
	SND_PCI_QUIRK(0x104d, 0x81a0, "Sony", ALC880_FIXUP_3ST),
	SND_PCI_QUIRK(0x104d, 0x81d6, "Sony", ALC880_FIXUP_3ST),
	SND_PCI_QUIRK(0x107b, 0x3032, "Gateway", ALC880_FIXUP_5ST),
	SND_PCI_QUIRK(0x107b, 0x3033, "Gateway", ALC880_FIXUP_5ST),
	SND_PCI_QUIRK(0x107b, 0x4039, "Gateway", ALC880_FIXUP_5ST),
	SND_PCI_QUIRK(0x1297, 0xc790, "Shuttle ST20G5", ALC880_FIXUP_6ST_DIG),
	SND_PCI_QUIRK(0x1458, 0xa102, "Gigabyte K8", ALC880_FIXUP_6ST_DIG),
	SND_PCI_QUIRK(0x1462, 0x1150, "MSI", ALC880_FIXUP_6ST_DIG),
	SND_PCI_QUIRK(0x1509, 0x925d, "FIC P4M", ALC880_FIXUP_6ST_DIG),
	SND_PCI_QUIRK(0x1565, 0x8202, "Biostar", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x1695, 0x400d, "EPoX", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x1695, 0x4012, "EPox EP-5LDA", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x2668, 0x8086, NULL, ALC880_FIXUP_6ST_DIG), /* broken BIOS */
	SND_PCI_QUIRK(0x8086, 0x2668, NULL, ALC880_FIXUP_6ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xa100, "Intel mobo", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xd400, "Intel mobo", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xd401, "Intel mobo", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xd402, "Intel mobo", ALC880_FIXUP_3ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe224, "Intel mobo", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe305, "Intel mobo", ALC880_FIXUP_3ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe308, "Intel mobo", ALC880_FIXUP_3ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe400, "Intel mobo", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe401, "Intel mobo", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe402, "Intel mobo", ALC880_FIXUP_5ST_DIG),
	/* default Intel */
	SND_PCI_QUIRK_VENDOR(0x8086, "Intel mobo", ALC880_FIXUP_3ST),
	SND_PCI_QUIRK(0xa0a0, 0x0560, "AOpen i915GMm-HFS", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0xe803, 0x1019, NULL, ALC880_FIXUP_6ST_DIG),
	{}
};

static const struct hda_model_fixup alc880_fixup_models[] = {
	{.id = ALC880_FIXUP_3ST, .name = "3stack"},
	{.id = ALC880_FIXUP_3ST_DIG, .name = "3stack-digout"},
	{.id = ALC880_FIXUP_5ST, .name = "5stack"},
	{.id = ALC880_FIXUP_5ST_DIG, .name = "5stack-digout"},
	{.id = ALC880_FIXUP_6ST, .name = "6stack"},
	{.id = ALC880_FIXUP_6ST_DIG, .name = "6stack-digout"},
	{.id = ALC880_FIXUP_6ST_AUTOMUTE, .name = "6stack-automute"},
	{}
};


/*
 * OK, here we have finally the patch for ALC880
 */
static int patch_alc880(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err;

	err = alc_alloc_spec(codec, 0x0b);
	if (err < 0)
		return err;

	spec = codec->spec;
	spec->gen.need_dac_fix = 1;
	spec->gen.beep_nid = 0x01;

	codec->patch_ops.unsol_event = alc880_unsol_event;

	snd_hda_pick_fixup(codec, alc880_fixup_models, alc880_fixup_tbl,
		       alc880_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	/* automatic parse from the BIOS config */
	err = alc880_parse_auto_config(codec);
	if (err < 0)
		goto error;

	if (!spec->gen.no_analog)
		set_beep_amp(spec, 0x0b, 0x05, HDA_INPUT);

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;

 error:
	alc_free(codec);
	return err;
}


/*
 * ALC260 support
 */

static hda_nid_t alc260_dac_nids[1] = {
	/* front */
	0x02,
};

static hda_nid_t alc260_adc_nids[1] = {
	/* ADC0 */
	0x04,
};

static hda_nid_t alc260_adc_nids_alt[1] = {
	/* ADC1 */
	0x05,
};

static hda_nid_t alc260_hp_adc_nids[2] = {
	/* ADC1, 0 */
	0x05, 0x04
};

/* NIDs used when simultaneous access to both ADCs makes sense.  Note that
 * alc260_capture_mixer assumes ADC0 (nid 0x04) is the first ADC.
 */
static hda_nid_t alc260_dual_adc_nids[2] = {
	/* ADC0, ADC1 */
	0x04, 0x05
};

#define ALC260_DIGOUT_NID	0x03
#define ALC260_DIGIN_NID	0x06

static struct hda_input_mux alc260_capture_source = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x1 },
		{ "Line", 0x2 },
		{ "CD", 0x4 },
	},
};

/* On Fujitsu S702x laptops capture only makes sense from Mic/LineIn jack,
 * headphone jack and the internal CD lines since these are the only pins at
 * which audio can appear.  For flexibility, also allow the option of
 * recording the mixer output on the second ADC (ADC0 doesn't have a
 * connection to the mixer output).
 */
static struct hda_input_mux alc260_fujitsu_capture_sources[2] = {
	{
		.num_items = 3,
		.items = {
			{ "Mic/Line", 0x0 },
			{ "CD", 0x4 },
			{ "Headphone", 0x2 },
		},
	},
	{
		.num_items = 4,
		.items = {
			{ "Mic/Line", 0x0 },
			{ "CD", 0x4 },
			{ "Headphone", 0x2 },
			{ "Mixer", 0x5 },
		},
	},

};

/* Acer TravelMate(/Extensa/Aspire) notebooks have similar configuration to
 * the Fujitsu S702x, but jacks are marked differently.
 */
static struct hda_input_mux alc260_acer_capture_sources[2] = {
	{
		.num_items = 4,
		.items = {
			{ "Mic", 0x0 },
			{ "Line", 0x2 },
			{ "CD", 0x4 },
			{ "Headphone", 0x5 },
		},
	},
	{
		.num_items = 5,
		.items = {
			{ "Mic", 0x0 },
			{ "Line", 0x2 },
			{ "CD", 0x4 },
			{ "Headphone", 0x6 },
			{ "Mixer", 0x5 },
		},
	},
};
/*
 * This is just place-holder, so there's something for alc_build_pcms to look
 * at when it calculates the maximum number of channels. ALC260 has no mixer
 * element which allows changing the channel mode, so the verb list is
 * never used.
 */
static struct hda_channel_mode alc260_modes[1] = {
	{ 2, NULL },
};


/* Mixer combinations
 *
 * basic: base_output + input + pc_beep + capture
 * HP: base_output + input + capture_alt
 * HP_3013: hp_3013 + input + capture
 * fujitsu: fujitsu + capture
 * acer: acer + capture
 */

static struct snd_kcontrol_new alc260_base_output_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x08, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x08, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x09, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x09, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Mono Playback Volume", 0x0a, 1, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Mono Playback Switch", 0x0a, 1, 2, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc260_input_mixer[] = {
	HDA_CODEC_VOLUME("CD Playback Volume", 0x07, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x07, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x07, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x07, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x07, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x07, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x07, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x07, 0x01, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc260_pc_beep_mixer[] = {
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x07, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x07, 0x05, HDA_INPUT),
	{ } /* end */
};

/* update HP, line and mono out pins according to the master switch */
static void alc260_hp_master_update(struct hda_codec *codec,
				    hda_nid_t hp, hda_nid_t line,
				    hda_nid_t mono)
{
	struct alc_spec *spec = codec->spec;
	unsigned int val = spec->master_sw ? PIN_HP : 0;
	/* change HP and line-out pins */
	snd_hda_codec_write(codec, 0x0f, 0, AC_VERB_SET_PIN_WIDGET_CONTROL,
			    val);
	snd_hda_codec_write(codec, 0x10, 0, AC_VERB_SET_PIN_WIDGET_CONTROL,
			    val);
	/* mono (speaker) depending on the HP jack sense */
	val = (val && !spec->jack_present) ? PIN_OUT : 0;
	snd_hda_codec_write(codec, 0x11, 0, AC_VERB_SET_PIN_WIDGET_CONTROL,
			    val);
}

static int alc260_hp_master_sw_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	*ucontrol->value.integer.value = spec->master_sw;
	return 0;
}

static int alc260_hp_master_sw_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	int val = !!*ucontrol->value.integer.value;
	hda_nid_t hp, line, mono;

	if (val == spec->master_sw)
		return 0;
	spec->master_sw = val;
	hp = (kcontrol->private_value >> 16) & 0xff;
	line = (kcontrol->private_value >> 8) & 0xff;
	mono = kcontrol->private_value & 0xff;
	alc260_hp_master_update(codec, hp, line, mono);
	return 1;
}

static struct snd_kcontrol_new alc260_hp_output_mixer[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = snd_ctl_boolean_mono_info,
		.get = alc260_hp_master_sw_get,
		.put = alc260_hp_master_sw_put,
		.private_value = (0x0f << 16) | (0x10 << 8) | 0x11
	},
	HDA_CODEC_VOLUME("Front Playback Volume", 0x08, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x08, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x09, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x09, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Speaker Playback Volume", 0x0a, 1, 0x0,
			      HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Speaker Playback Switch", 0x0a, 1, 2, HDA_INPUT),
	{ } /* end */
};

static struct hda_verb alc260_hp_unsol_verbs[] = {
	{0x10, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{},
};

static void alc260_hp_automute(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x10, 0,
				     AC_VERB_GET_PIN_SENSE, 0);
	spec->jack_present = (present & AC_PINSENSE_PRESENCE) != 0;
	alc260_hp_master_update(codec, 0x0f, 0x10, 0x11);
}

static void alc260_hp_unsol_event(struct hda_codec *codec, unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc260_hp_automute(codec);
}

static struct snd_kcontrol_new alc260_hp_3013_mixer[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = snd_ctl_boolean_mono_info,
		.get = alc260_hp_master_sw_get,
		.put = alc260_hp_master_sw_put,
		.private_value = (0x10 << 16) | (0x15 << 8) | 0x11
	},
	HDA_CODEC_VOLUME("Front Playback Volume", 0x09, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x10, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Aux-In Playback Volume", 0x07, 0x06, HDA_INPUT),
	HDA_CODEC_MUTE("Aux-In Playback Switch", 0x07, 0x06, HDA_INPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x08, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("Speaker Playback Volume", 0x0a, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Speaker Playback Switch", 0x11, 1, 0x0, HDA_OUTPUT),
	{ } /* end */
};

static struct hda_verb alc260_hp_3013_unsol_verbs[] = {
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{},
};

static void alc260_hp_3013_automute(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x15, 0,
				     AC_VERB_GET_PIN_SENSE, 0);
	spec->jack_present = (present & AC_PINSENSE_PRESENCE) != 0;
	alc260_hp_master_update(codec, 0x10, 0x15, 0x11);
}

static void alc260_hp_3013_unsol_event(struct hda_codec *codec,
				       unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc260_hp_3013_automute(codec);
}

/* Fujitsu S702x series laptops.  ALC260 pin usage: Mic/Line jack = 0x12, 
 * HP jack = 0x14, CD audio =  0x16, internal speaker = 0x10.
 */
static struct snd_kcontrol_new alc260_fujitsu_mixer[] = {
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x08, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x08, 2, HDA_INPUT),
	ALC_PIN_MODE("Headphone Jack Mode", 0x14, ALC_PIN_DIR_INOUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x07, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x07, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic/Line Playback Volume", 0x07, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic/Line Playback Switch", 0x07, 0x0, HDA_INPUT),
	ALC_PIN_MODE("Mic/Line Jack Mode", 0x12, ALC_PIN_DIR_IN),
	HDA_CODEC_VOLUME("Beep Playback Volume", 0x07, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("Beep Playback Switch", 0x07, 0x05, HDA_INPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x09, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x09, 2, HDA_INPUT),
	{ } /* end */
};

/* Mixer for Acer TravelMate(/Extensa/Aspire) notebooks.  Note that current
 * versions of the ALC260 don't act on requests to enable mic bias from NID
 * 0x0f (used to drive the headphone jack in these laptops).  The ALC260
 * datasheet doesn't mention this restriction.  At this stage it's not clear
 * whether this behaviour is intentional or is a hardware bug in chip
 * revisions available in early 2006.  Therefore for now allow the
 * "Headphone Jack Mode" control to span all choices, but if it turns out
 * that the lack of mic bias for this NID is intentional we could change the
 * mode from ALC_PIN_DIR_INOUT to ALC_PIN_DIR_INOUT_NOMICBIAS.
 *
 * In addition, Acer TravelMate(/Extensa/Aspire) notebooks in early 2006
 * don't appear to make the mic bias available from the "line" jack, even
 * though the NID used for this jack (0x14) can supply it.  The theory is
 * that perhaps Acer have included blocking capacitors between the ALC260
 * and the output jack.  If this turns out to be the case for all such
 * models the "Line Jack Mode" mode could be changed from ALC_PIN_DIR_INOUT
 * to ALC_PIN_DIR_INOUT_NOMICBIAS.
 *
 * The C20x Tablet series have a mono internal speaker which is controlled
 * via the chip's Mono sum widget and pin complex, so include the necessary
 * controls for such models.  On models without a "mono speaker" the control
 * won't do anything.
 */
static struct snd_kcontrol_new alc260_acer_mixer[] = {
	HDA_CODEC_VOLUME("Master Playback Volume", 0x08, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Master Playback Switch", 0x08, 2, HDA_INPUT),
	ALC_PIN_MODE("Headphone Jack Mode", 0x0f, ALC_PIN_DIR_INOUT),
	HDA_CODEC_VOLUME_MONO("Speaker Playback Volume", 0x0a, 1, 0x0,
			      HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Speaker Playback Switch", 0x0a, 1, 2,
			   HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x07, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x07, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x07, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x07, 0x0, HDA_INPUT),
	ALC_PIN_MODE("Mic Jack Mode", 0x12, ALC_PIN_DIR_IN),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x07, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x07, 0x02, HDA_INPUT),
	ALC_PIN_MODE("Line Jack Mode", 0x14, ALC_PIN_DIR_INOUT),
	HDA_CODEC_VOLUME("Beep Playback Volume", 0x07, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("Beep Playback Switch", 0x07, 0x05, HDA_INPUT),
	{ } /* end */
};

/* Packard bell V7900  ALC260 pin usage: HP = 0x0f, Mic jack = 0x12,
 * Line In jack = 0x14, CD audio =  0x16, pc beep = 0x17.
 */
static struct snd_kcontrol_new alc260_will_mixer[] = {
	HDA_CODEC_VOLUME("Master Playback Volume", 0x08, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Master Playback Switch", 0x08, 0x2, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x07, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x07, 0x0, HDA_INPUT),
	ALC_PIN_MODE("Mic Jack Mode", 0x12, ALC_PIN_DIR_IN),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x07, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x07, 0x02, HDA_INPUT),
	ALC_PIN_MODE("Line Jack Mode", 0x14, ALC_PIN_DIR_INOUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x07, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x07, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Beep Playback Volume", 0x07, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("Beep Playback Switch", 0x07, 0x05, HDA_INPUT),
	{ } /* end */
};

/* Replacer 672V ALC260 pin usage: Mic jack = 0x12,
 * Line In jack = 0x14, ATAPI Mic = 0x13, speaker = 0x0f.
 */
static struct snd_kcontrol_new alc260_replacer_672v_mixer[] = {
	HDA_CODEC_VOLUME("Master Playback Volume", 0x08, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Master Playback Switch", 0x08, 0x2, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x07, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x07, 0x0, HDA_INPUT),
	ALC_PIN_MODE("Mic Jack Mode", 0x12, ALC_PIN_DIR_IN),
	HDA_CODEC_VOLUME("ATAPI Mic Playback Volume", 0x07, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("ATATI Mic Playback Switch", 0x07, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x07, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x07, 0x02, HDA_INPUT),
	ALC_PIN_MODE("Line Jack Mode", 0x14, ALC_PIN_DIR_INOUT),
	{ } /* end */
};

/* capture mixer elements */
static struct snd_kcontrol_new alc260_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x04, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x04, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x05, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x05, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = alc_mux_enum_info,
		.get = alc_mux_enum_get,
		.put = alc_mux_enum_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc260_capture_alt_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x05, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x05, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 1,
		.info = alc_mux_enum_info,
		.get = alc_mux_enum_get,
		.put = alc_mux_enum_put,
	},
	{ } /* end */
};

/*
 * initialization verbs
 */
static struct hda_verb alc260_init_verbs[] = {
	/* Line In pin widget for input */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	/* CD pin widget for input */
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	/* Mic1 (rear panel) pin widget for input and vref at 80% */
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	/* Mic2 (front panel) pin widget for input and vref at 80% */
	{0x13, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	/* LINE-2 is used for line-out in rear */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	/* select line-out */
	{0x0e, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* LINE-OUT pin */
	{0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	/* enable HP */
	{0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	/* enable Mono */
	{0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	/* mute capture amp left and right */
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	/* set connection select to line in (default select for this ADC) */
	{0x04, AC_VERB_SET_CONNECT_SEL, 0x02},
	/* mute capture amp left and right */
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	/* set connection select to line in (default select for this ADC) */
	{0x05, AC_VERB_SET_CONNECT_SEL, 0x02},
	/* set vol=0 Line-Out mixer amp left and right */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	/* unmute pin widget amp left and right (no gain on this amp) */
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* set vol=0 HP mixer amp left and right */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	/* unmute pin widget amp left and right (no gain on this amp) */
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* set vol=0 Mono mixer amp left and right */
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	/* unmute pin widget amp left and right (no gain on this amp) */
	{0x11, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* unmute LINE-2 out pin */
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Amp Indexes: CD = 0x04, Line In 1 = 0x02, Mic 1 = 0x00 &
	 * Line In 2 = 0x03
	 */
	/* mute analog inputs */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* Amp Indexes: DAC = 0x01 & mixer = 0x00 */
	/* mute Front out path */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* mute Headphone out path */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* mute Mono out path */
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{ }
};

#if 0 /* should be identical with alc260_init_verbs? */
static struct hda_verb alc260_hp_init_verbs[] = {
	/* Headphone and output */
	{0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0},
	/* mono output */
	{0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	/* Mic1 (rear panel) pin widget for input and vref at 80% */
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},
	/* Mic2 (front panel) pin widget for input and vref at 80% */
	{0x13, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},
	/* Line In pin widget for input */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},
	/* Line-2 pin widget for output */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	/* CD pin widget for input */
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},
	/* unmute amp left and right */
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, 0x7000},
	/* set connection select to line in (default select for this ADC) */
	{0x04, AC_VERB_SET_CONNECT_SEL, 0x02},
	/* unmute Line-Out mixer amp left and right (volume = 0) */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
	/* mute pin widget amp left and right (no gain on this amp) */
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0x0000},
	/* unmute HP mixer amp left and right (volume = 0) */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
	/* mute pin widget amp left and right (no gain on this amp) */
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, 0x0000},
	/* Amp Indexes: CD = 0x04, Line In 1 = 0x02, Mic 1 = 0x00 &
	 * Line In 2 = 0x03
	 */
	/* mute analog inputs */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* Amp Indexes: DAC = 0x01 & mixer = 0x00 */
	/* Unmute Front out path */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	/* Unmute Headphone out path */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	/* Unmute Mono out path */
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	{ }
};
#endif

static struct hda_verb alc260_hp_3013_init_verbs[] = {
	/* Line out and output */
	{0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	/* mono output */
	{0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	/* Mic1 (rear panel) pin widget for input and vref at 80% */
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},
	/* Mic2 (front panel) pin widget for input and vref at 80% */
	{0x13, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},
	/* Line In pin widget for input */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},
	/* Headphone pin widget for output */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0},
	/* CD pin widget for input */
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},
	/* unmute amp left and right */
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, 0x7000},
	/* set connection select to line in (default select for this ADC) */
	{0x04, AC_VERB_SET_CONNECT_SEL, 0x02},
	/* unmute Line-Out mixer amp left and right (volume = 0) */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
	/* mute pin widget amp left and right (no gain on this amp) */
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0x0000},
	/* unmute HP mixer amp left and right (volume = 0) */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
	/* mute pin widget amp left and right (no gain on this amp) */
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, 0x0000},
	/* Amp Indexes: CD = 0x04, Line In 1 = 0x02, Mic 1 = 0x00 &
	 * Line In 2 = 0x03
	 */
	/* mute analog inputs */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* Amp Indexes: DAC = 0x01 & mixer = 0x00 */
	/* Unmute Front out path */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	/* Unmute Headphone out path */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	/* Unmute Mono out path */
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	{ }
};

/* Initialisation sequence for ALC260 as configured in Fujitsu S702x
 * laptops.  ALC260 pin usage: Mic/Line jack = 0x12, HP jack = 0x14, CD
 * audio = 0x16, internal speaker = 0x10.
 */
static struct hda_verb alc260_fujitsu_init_verbs[] = {
	/* Disable all GPIOs */
	{0x01, AC_VERB_SET_GPIO_MASK, 0},
	/* Internal speaker is connected to headphone pin */
	{0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	/* Headphone/Line-out jack connects to Line1 pin; make it an output */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	/* Mic/Line-in jack is connected to mic1 pin, so make it an input */
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	/* Ensure all other unused pins are disabled and muted. */
	{0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},
	{0x11, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x13, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},
	{0x13, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},

	/* Disable digital (SPDIF) pins */
	{0x03, AC_VERB_SET_DIGI_CONVERT_1, 0},
	{0x06, AC_VERB_SET_DIGI_CONVERT_1, 0},

	/* Ensure Line1 pin widget takes its input from the OUT1 sum bus 
	 * when acting as an output.
	 */
	{0x0d, AC_VERB_SET_CONNECT_SEL, 0},

	/* Start with output sum widgets muted and their output gains at min */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},

	/* Unmute HP pin widget amp left and right (no equiv mixer ctrl) */
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Unmute Line1 pin widget output buffer since it starts as an output.
	 * If the pin mode is changed by the user the pin mode control will
	 * take care of enabling the pin's input/output buffers as needed.
	 * Therefore there's no need to enable the input buffer at this
	 * stage.
	 */
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Unmute input buffer of pin widget used for Line-in (no equiv 
	 * mixer ctrl)
	 */
	{0x12, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	/* Mute capture amp left and right */
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	/* Set ADC connection select to match default mixer setting - line 
	 * in (on mic1 pin)
	 */
	{0x04, AC_VERB_SET_CONNECT_SEL, 0x00},

	/* Do the same for the second ADC: mute capture input amp and
	 * set ADC connection to line in (on mic1 pin)
	 */
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x05, AC_VERB_SET_CONNECT_SEL, 0x00},

	/* Mute all inputs to mixer widget (even unconnected ones) */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)}, /* mic1 pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)}, /* mic2 pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)}, /* line1 pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)}, /* line2 pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)}, /* CD pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(5)}, /* Beep-gen pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(6)}, /* Line-out pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(7)}, /* HP-pin pin */

	{ }
};

/* Initialisation sequence for ALC260 as configured in Acer TravelMate and
 * similar laptops (adapted from Fujitsu init verbs).
 */
static struct hda_verb alc260_acer_init_verbs[] = {
	/* On TravelMate laptops, GPIO 0 enables the internal speaker and
	 * the headphone jack.  Turn this on and rely on the standard mute
	 * methods whenever the user wants to turn these outputs off.
	 */
	{0x01, AC_VERB_SET_GPIO_MASK, 0x01},
	{0x01, AC_VERB_SET_GPIO_DIRECTION, 0x01},
	{0x01, AC_VERB_SET_GPIO_DATA, 0x01},
	/* Internal speaker/Headphone jack is connected to Line-out pin */
	{0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	/* Internal microphone/Mic jack is connected to Mic1 pin */
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF50},
	/* Line In jack is connected to Line1 pin */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	/* Some Acers (eg: C20x Tablets) use Mono pin for internal speaker */
	{0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	/* Ensure all other unused pins are disabled and muted. */
	{0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x13, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},
	{0x13, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	/* Disable digital (SPDIF) pins */
	{0x03, AC_VERB_SET_DIGI_CONVERT_1, 0},
	{0x06, AC_VERB_SET_DIGI_CONVERT_1, 0},

	/* Ensure Mic1 and Line1 pin widgets take input from the OUT1 sum 
	 * bus when acting as outputs.
	 */
	{0x0b, AC_VERB_SET_CONNECT_SEL, 0},
	{0x0d, AC_VERB_SET_CONNECT_SEL, 0},

	/* Start with output sum widgets muted and their output gains at min */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},

	/* Unmute Line-out pin widget amp left and right
	 * (no equiv mixer ctrl)
	 */
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Unmute mono pin widget amp output (no equiv mixer ctrl) */
	{0x11, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Unmute Mic1 and Line1 pin widget input buffers since they start as
	 * inputs. If the pin mode is changed by the user the pin mode control
	 * will take care of enabling the pin's input/output buffers as needed.
	 * Therefore there's no need to enable the input buffer at this
	 * stage.
	 */
	{0x12, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	/* Mute capture amp left and right */
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	/* Set ADC connection select to match default mixer setting - mic
	 * (on mic1 pin)
	 */
	{0x04, AC_VERB_SET_CONNECT_SEL, 0x00},

	/* Do similar with the second ADC: mute capture input amp and
	 * set ADC connection to mic to match ALSA's default state.
	 */
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x05, AC_VERB_SET_CONNECT_SEL, 0x00},

	/* Mute all inputs to mixer widget (even unconnected ones) */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)}, /* mic1 pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)}, /* mic2 pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)}, /* line1 pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)}, /* line2 pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)}, /* CD pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(5)}, /* Beep-gen pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(6)}, /* Line-out pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(7)}, /* HP-pin pin */

	{ }
};

static struct hda_verb alc260_will_verbs[] = {
	{0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x0b, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x0d, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x0f, AC_VERB_SET_EAPD_BTLENABLE, 0x02},
	{0x1a, AC_VERB_SET_COEF_INDEX, 0x07},
	{0x1a, AC_VERB_SET_PROC_COEF, 0x3040},
	{}
};

static struct hda_verb alc260_replacer_672v_verbs[] = {
	{0x0f, AC_VERB_SET_EAPD_BTLENABLE, 0x02},
	{0x1a, AC_VERB_SET_COEF_INDEX, 0x07},
	{0x1a, AC_VERB_SET_PROC_COEF, 0x3050},

	{0x01, AC_VERB_SET_GPIO_MASK, 0x01},
	{0x01, AC_VERB_SET_GPIO_DIRECTION, 0x01},
	{0x01, AC_VERB_SET_GPIO_DATA, 0x00},

	{0x0f, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{}
};

/* toggle speaker-output according to the hp-jack state */
static void alc260_replacer_672v_automute(struct hda_codec *codec)
{
        unsigned int present;

	/* speaker --> GPIO Data 0, hp or spdif --> GPIO data 1 */
        present = snd_hda_codec_read(codec, 0x0f, 0,
                                     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	if (present) {
		snd_hda_codec_write_cache(codec, 0x01, 0,
					  AC_VERB_SET_GPIO_DATA, 1);
		snd_hda_codec_write_cache(codec, 0x0f, 0,
					  AC_VERB_SET_PIN_WIDGET_CONTROL,
					  PIN_HP);
	} else {
		snd_hda_codec_write_cache(codec, 0x01, 0,
					  AC_VERB_SET_GPIO_DATA, 0);
		snd_hda_codec_write_cache(codec, 0x0f, 0,
					  AC_VERB_SET_PIN_WIDGET_CONTROL,
					  PIN_OUT);
	}
}

static void alc260_replacer_672v_unsol_event(struct hda_codec *codec,
                                       unsigned int res)
{
        if ((res >> 26) == ALC880_HP_EVENT)
                alc260_replacer_672v_automute(codec);
}

/* Test configuration for debugging, modelled after the ALC880 test
 * configuration.
 */
#ifdef CONFIG_SND_DEBUG
static hda_nid_t alc260_test_dac_nids[1] = {
	0x02,
};
static hda_nid_t alc260_test_adc_nids[2] = {
	0x04, 0x05,
};
/* For testing the ALC260, each input MUX needs its own definition since
 * the signal assignments are different.  This assumes that the first ADC 
 * is NID 0x04.
 */
static struct hda_input_mux alc260_test_capture_sources[2] = {
	{
		.num_items = 7,
		.items = {
			{ "MIC1 pin", 0x0 },
			{ "MIC2 pin", 0x1 },
			{ "LINE1 pin", 0x2 },
			{ "LINE2 pin", 0x3 },
			{ "CD pin", 0x4 },
			{ "LINE-OUT pin", 0x5 },
			{ "HP-OUT pin", 0x6 },
		},
        },
	{
		.num_items = 8,
		.items = {
			{ "MIC1 pin", 0x0 },
			{ "MIC2 pin", 0x1 },
			{ "LINE1 pin", 0x2 },
			{ "LINE2 pin", 0x3 },
			{ "CD pin", 0x4 },
			{ "Mixer", 0x5 },
			{ "LINE-OUT pin", 0x6 },
			{ "HP-OUT pin", 0x7 },
		},
        },
};
static struct snd_kcontrol_new alc260_test_mixer[] = {
	/* Output driver widgets */
	HDA_CODEC_VOLUME_MONO("Mono Playback Volume", 0x0a, 1, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Mono Playback Switch", 0x0a, 1, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("LOUT2 Playback Volume", 0x09, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("LOUT2 Playback Switch", 0x09, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("LOUT1 Playback Volume", 0x08, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("LOUT1 Playback Switch", 0x08, 2, HDA_INPUT),

	/* Modes for retasking pin widgets
	 * Note: the ALC260 doesn't seem to act on requests to enable mic
         * bias from NIDs 0x0f and 0x10.  The ALC260 datasheet doesn't
         * mention this restriction.  At this stage it's not clear whether
         * this behaviour is intentional or is a hardware bug in chip
         * revisions available at least up until early 2006.  Therefore for
         * now allow the "HP-OUT" and "LINE-OUT" Mode controls to span all
         * choices, but if it turns out that the lack of mic bias for these
         * NIDs is intentional we could change their modes from
         * ALC_PIN_DIR_INOUT to ALC_PIN_DIR_INOUT_NOMICBIAS.
	 */
	ALC_PIN_MODE("HP-OUT pin mode", 0x10, ALC_PIN_DIR_INOUT),
	ALC_PIN_MODE("LINE-OUT pin mode", 0x0f, ALC_PIN_DIR_INOUT),
	ALC_PIN_MODE("LINE2 pin mode", 0x15, ALC_PIN_DIR_INOUT),
	ALC_PIN_MODE("LINE1 pin mode", 0x14, ALC_PIN_DIR_INOUT),
	ALC_PIN_MODE("MIC2 pin mode", 0x13, ALC_PIN_DIR_INOUT),
	ALC_PIN_MODE("MIC1 pin mode", 0x12, ALC_PIN_DIR_INOUT),

	/* Loopback mixer controls */
	HDA_CODEC_VOLUME("MIC1 Playback Volume", 0x07, 0x00, HDA_INPUT),
	HDA_CODEC_MUTE("MIC1 Playback Switch", 0x07, 0x00, HDA_INPUT),
	HDA_CODEC_VOLUME("MIC2 Playback Volume", 0x07, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("MIC2 Playback Switch", 0x07, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("LINE1 Playback Volume", 0x07, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("LINE1 Playback Switch", 0x07, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("LINE2 Playback Volume", 0x07, 0x03, HDA_INPUT),
	HDA_CODEC_MUTE("LINE2 Playback Switch", 0x07, 0x03, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x07, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x07, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Beep Playback Volume", 0x07, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("Beep Playback Switch", 0x07, 0x05, HDA_INPUT),
	HDA_CODEC_VOLUME("LINE-OUT loopback Playback Volume", 0x07, 0x06, HDA_INPUT),
	HDA_CODEC_MUTE("LINE-OUT loopback Playback Switch", 0x07, 0x06, HDA_INPUT),
	HDA_CODEC_VOLUME("HP-OUT loopback Playback Volume", 0x07, 0x7, HDA_INPUT),
	HDA_CODEC_MUTE("HP-OUT loopback Playback Switch", 0x07, 0x7, HDA_INPUT),

	/* Controls for GPIO pins, assuming they are configured as outputs */
	ALC_GPIO_DATA_SWITCH("GPIO pin 0", 0x01, 0x01),
	ALC_GPIO_DATA_SWITCH("GPIO pin 1", 0x01, 0x02),
	ALC_GPIO_DATA_SWITCH("GPIO pin 2", 0x01, 0x04),
	ALC_GPIO_DATA_SWITCH("GPIO pin 3", 0x01, 0x08),

	/* Switches to allow the digital IO pins to be enabled.  The datasheet
	 * is ambigious as to which NID is which; testing on laptops which
	 * make this output available should provide clarification. 
	 */
	ALC_SPDIF_CTRL_SWITCH("SPDIF Playback Switch", 0x03, 0x01),
	ALC_SPDIF_CTRL_SWITCH("SPDIF Capture Switch", 0x06, 0x01),

	/* A switch allowing EAPD to be enabled.  Some laptops seem to use
	 * this output to turn on an external amplifier.
	 */
	ALC_EAPD_CTRL_SWITCH("LINE-OUT EAPD Enable Switch", 0x0f, 0x02),
	ALC_EAPD_CTRL_SWITCH("HP-OUT EAPD Enable Switch", 0x10, 0x02),

	{ } /* end */
};
static struct hda_verb alc260_test_init_verbs[] = {
	/* Enable all GPIOs as outputs with an initial value of 0 */
	{0x01, AC_VERB_SET_GPIO_DIRECTION, 0x0f},
	{0x01, AC_VERB_SET_GPIO_DATA, 0x00},
	{0x01, AC_VERB_SET_GPIO_MASK, 0x0f},

	/* Enable retasking pins as output, initially without power amp */
	{0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x13, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},

	/* Disable digital (SPDIF) pins initially, but users can enable
	 * them via a mixer switch.  In the case of SPDIF-out, this initverb
	 * payload also sets the generation to 0, output to be in "consumer"
	 * PCM format, copyright asserted, no pre-emphasis and no validity
	 * control.
	 */
	{0x03, AC_VERB_SET_DIGI_CONVERT_1, 0},
	{0x06, AC_VERB_SET_DIGI_CONVERT_1, 0},

	/* Ensure mic1, mic2, line1 and line2 pin widgets take input from the 
	 * OUT1 sum bus when acting as an output.
	 */
	{0x0b, AC_VERB_SET_CONNECT_SEL, 0},
	{0x0c, AC_VERB_SET_CONNECT_SEL, 0},
	{0x0d, AC_VERB_SET_CONNECT_SEL, 0},
	{0x0e, AC_VERB_SET_CONNECT_SEL, 0},

	/* Start with output sum widgets muted and their output gains at min */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},

	/* Unmute retasking pin widget output buffers since the default
	 * state appears to be output.  As the pin mode is changed by the
	 * user the pin mode control will take care of enabling the pin's
	 * input/output buffers as needed.
	 */
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x13, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x12, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Also unmute the mono-out pin widget */
	{0x11, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	/* Mute capture amp left and right */
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	/* Set ADC connection select to match default mixer setting (mic1
	 * pin)
	 */
	{0x04, AC_VERB_SET_CONNECT_SEL, 0x00},

	/* Do the same for the second ADC: mute capture input amp and
	 * set ADC connection to mic1 pin
	 */
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x05, AC_VERB_SET_CONNECT_SEL, 0x00},

	/* Mute all inputs to mixer widget (even unconnected ones) */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)}, /* mic1 pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)}, /* mic2 pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)}, /* line1 pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)}, /* line2 pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)}, /* CD pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(5)}, /* Beep-gen pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(6)}, /* Line-out pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(7)}, /* HP-pin pin */

	{ }
};
#endif

#define alc260_pcm_analog_playback	alc880_pcm_analog_alt_playback
#define alc260_pcm_analog_capture	alc880_pcm_analog_capture

#define alc260_pcm_digital_playback	alc880_pcm_digital_playback
#define alc260_pcm_digital_capture	alc880_pcm_digital_capture

/*
 * for BIOS auto-configuration
 */

static int alc260_add_playback_controls(struct alc_spec *spec, hda_nid_t nid,
					const char *pfx)
{
	hda_nid_t nid_vol;
	unsigned long vol_val, sw_val;
	char name[32];
	int err;

	if (nid >= 0x0f && nid < 0x11) {
		nid_vol = nid - 0x7;
		vol_val = HDA_COMPOSE_AMP_VAL(nid_vol, 3, 0, HDA_OUTPUT);
		sw_val = HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT);
	} else if (nid == 0x11) {
		nid_vol = nid - 0x7;
		vol_val = HDA_COMPOSE_AMP_VAL(nid_vol, 2, 0, HDA_OUTPUT);
		sw_val = HDA_COMPOSE_AMP_VAL(nid, 2, 0, HDA_OUTPUT);
	} else if (nid >= 0x12 && nid <= 0x15) {
		nid_vol = 0x08;
		vol_val = HDA_COMPOSE_AMP_VAL(nid_vol, 3, 0, HDA_OUTPUT);
		sw_val = HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT);
	} else
		return 0; /* N/A */
	
	snprintf(name, sizeof(name), "%s Playback Volume", pfx);
	err = add_control(spec, ALC_CTL_WIDGET_VOL, name, vol_val);
	if (err < 0)
		return err;
	snprintf(name, sizeof(name), "%s Playback Switch", pfx);
	err = add_control(spec, ALC_CTL_WIDGET_MUTE, name, sw_val);
	if (err < 0)
		return err;
	return 1;
}

/* add playback controls from the parsed DAC table */
static int alc260_auto_create_multi_out_ctls(struct alc_spec *spec,
					     const struct auto_pin_cfg *cfg)
{
	hda_nid_t nid;
	int err;

	spec->multiout.num_dacs = 1;
	spec->multiout.dac_nids = spec->private_dac_nids;
	spec->multiout.dac_nids[0] = 0x02;

	nid = cfg->line_out_pins[0];
	if (nid) {
		err = alc260_add_playback_controls(spec, nid, "Front");
		if (err < 0)
			return err;
	}

	nid = cfg->speaker_pins[0];
	if (nid) {
		err = alc260_add_playback_controls(spec, nid, "Speaker");
		if (err < 0)
			return err;
	}

	nid = cfg->hp_pins[0];
	if (nid) {
		err = alc260_add_playback_controls(spec, nid, "Headphone");
		if (err < 0)
			return err;
	}
	return 0;
}

/* create playback/capture controls for input pins */
static int alc260_auto_create_analog_input_ctls(struct alc_spec *spec,
						const struct auto_pin_cfg *cfg)
{
	struct hda_input_mux *imux = &spec->private_imux;
	int i, err, idx;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		if (cfg->input_pins[i] >= 0x12) {
			idx = cfg->input_pins[i] - 0x12;
			err = new_analog_input(spec, cfg->input_pins[i],
					       auto_pin_cfg_labels[i], idx,
					       0x07);
			if (err < 0)
				return err;
			imux->items[imux->num_items].label =
				auto_pin_cfg_labels[i];
			imux->items[imux->num_items].index = idx;
			imux->num_items++;
		}
		if (cfg->input_pins[i] >= 0x0f && cfg->input_pins[i] <= 0x10){
			idx = cfg->input_pins[i] - 0x09;
			err = new_analog_input(spec, cfg->input_pins[i],
					       auto_pin_cfg_labels[i], idx,
					       0x07);
			if (err < 0)
				return err;
			imux->items[imux->num_items].label =
				auto_pin_cfg_labels[i];
			imux->items[imux->num_items].index = idx;
			imux->num_items++;
		}
	}
	return 0;
}

static void alc260_auto_set_output_and_unmute(struct hda_codec *codec,
					      hda_nid_t nid, int pin_type,
					      int sel_idx)
{
	alc_set_pin_output(codec, nid, pin_type);
	/* need the manual connection? */
	if (nid >= 0x12) {
		int idx = nid - 0x12;
		snd_hda_codec_write(codec, idx + 0x0b, 0,
				    AC_VERB_SET_CONNECT_SEL, sel_idx);
	}
}

static void alc260_auto_init_multi_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t nid;

	alc_subsystem_id(codec, 0x10, 0x15, 0x0f);
	nid = spec->autocfg.line_out_pins[0];
	if (nid) {
		int pin_type = get_pin_type(spec->autocfg.line_out_type);
		alc260_auto_set_output_and_unmute(codec, nid, pin_type, 0);
	}
	
	nid = spec->autocfg.speaker_pins[0];
	if (nid)
		alc260_auto_set_output_and_unmute(codec, nid, PIN_OUT, 0);

	nid = spec->autocfg.hp_pins[0];
	if (nid)
		alc260_auto_set_output_and_unmute(codec, nid, PIN_HP, 0);
}

#define ALC260_PIN_CD_NID		0x16
static void alc260_auto_init_analog_input(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		hda_nid_t nid = spec->autocfg.input_pins[i];
		if (nid >= 0x12) {
			snd_hda_codec_write(codec, nid, 0,
					    AC_VERB_SET_PIN_WIDGET_CONTROL,
					    i <= AUTO_PIN_FRONT_MIC ?
					    PIN_VREF80 : PIN_IN);
			if (nid != ALC260_PIN_CD_NID)
				snd_hda_codec_write(codec, nid, 0,
						    AC_VERB_SET_AMP_GAIN_MUTE,
						    AMP_OUT_MUTE);
		}
	}
}

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static struct hda_verb alc260_volume_init_verbs[] = {
	/*
	 * Unmute ADC0-1 and set the default input to mic-in
	 */
	{0x04, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x05, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	
	/* Unmute input amps (CD, Line In, Mic 1 & Mic 2) of the analog-loopback
	 * mixer widget
	 * Note: PASD motherboards uses the Line In 2 as the input for
	 * front panel mic (mic 2)
	 */
	/* Amp Indices: Mic1 = 0, Mic2 = 1, Line1 = 2, Line2 = 3, CD = 4 */
	/* mute analog inputs */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},

	/*
	 * Set up output mixers (0x08 - 0x0a)
	 */
	/* set vol=0 to output mixers */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	/* set up input amps for analog loopback */
	/* Amp Indices: DAC = 0, mixer = 1 */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	
	{ }
};

static int alc260_parse_auto_config(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	unsigned int wcap;
	int err;
	static hda_nid_t alc260_ignore[] = { 0x17, 0 };

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg,
					   alc260_ignore);
	if (err < 0)
		return err;
	err = alc260_auto_create_multi_out_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;
	if (!spec->kctl_alloc)
		return 0; /* can't find valid BIOS pin config */
	err = alc260_auto_create_analog_input_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;

	spec->multiout.max_channels = 2;

	if (spec->autocfg.dig_out_pin)
		spec->multiout.dig_out_nid = ALC260_DIGOUT_NID;
	if (spec->kctl_alloc)
		spec->mixers[spec->num_mixers++] = spec->kctl_alloc;

	spec->init_verbs[spec->num_init_verbs++] = alc260_volume_init_verbs;

	spec->num_mux_defs = 1;
	spec->input_mux = &spec->private_imux;

	/* check whether NID 0x04 is valid */
	wcap = get_wcaps(codec, 0x04);
	wcap = (wcap & AC_WCAP_TYPE) >> AC_WCAP_TYPE_SHIFT; /* get type */
	if (wcap != AC_WID_AUD_IN || spec->input_mux->num_items == 1) {
		spec->adc_nids = alc260_adc_nids_alt;
		spec->num_adc_nids = ARRAY_SIZE(alc260_adc_nids_alt);
		spec->mixers[spec->num_mixers] = alc260_capture_alt_mixer;
	} else {
		spec->adc_nids = alc260_adc_nids;
		spec->num_adc_nids = ARRAY_SIZE(alc260_adc_nids);
		spec->mixers[spec->num_mixers] = alc260_capture_mixer;
	}
	spec->num_mixers++;

	return 1;
}

/* additional initialization for auto-configuration model */
static void alc260_auto_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	alc260_auto_init_multi_out(codec);
	alc260_auto_init_analog_input(codec);
	if (spec->unsol_event)
		alc_sku_automute(codec);
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
static struct hda_amp_list alc260_loopbacks[] = {
	{ 0x07, HDA_INPUT, 0 },
	{ 0x07, HDA_INPUT, 1 },
	{ 0x07, HDA_INPUT, 2 },
	{ 0x07, HDA_INPUT, 3 },
	{ 0x07, HDA_INPUT, 4 },
	{ } /* end */
};
#endif

/*
 * ALC260 configurations
 */
static const char *alc260_models[ALC260_MODEL_LAST] = {
	[ALC260_BASIC]		= "basic",
	[ALC260_HP]		= "hp",
	[ALC260_HP_3013]	= "hp-3013",
	[ALC260_FUJITSU_S702X]	= "fujitsu",
	[ALC260_ACER]		= "acer",
	[ALC260_WILL]		= "will",
	[ALC260_REPLACER_672V]	= "replacer",
#ifdef CONFIG_SND_DEBUG
	[ALC260_TEST]		= "test",
#endif
	[ALC260_AUTO]		= "auto",
};

static struct snd_pci_quirk alc260_cfg_tbl[] = {
	SND_PCI_QUIRK(0x1025, 0x007b, "Acer C20x", ALC260_ACER),
	SND_PCI_QUIRK(0x1025, 0x008f, "Acer", ALC260_ACER),
	SND_PCI_QUIRK(0x103c, 0x2808, "HP d5700", ALC260_HP_3013),
	SND_PCI_QUIRK(0x103c, 0x280a, "HP d5750", ALC260_HP_3013),
	SND_PCI_QUIRK(0x103c, 0x3010, "HP", ALC260_HP_3013),
	SND_PCI_QUIRK(0x103c, 0x3011, "HP", ALC260_HP_3013),
	SND_PCI_QUIRK(0x103c, 0x3012, "HP", ALC260_HP_3013),
	SND_PCI_QUIRK(0x103c, 0x3013, "HP", ALC260_HP_3013),
	SND_PCI_QUIRK(0x103c, 0x3014, "HP", ALC260_HP),
	SND_PCI_QUIRK(0x103c, 0x3015, "HP", ALC260_HP),
	SND_PCI_QUIRK(0x103c, 0x3016, "HP", ALC260_HP),
	SND_PCI_QUIRK(0x104d, 0x81bb, "Sony VAIO", ALC260_BASIC),
	SND_PCI_QUIRK(0x104d, 0x81cc, "Sony VAIO", ALC260_BASIC),
	SND_PCI_QUIRK(0x104d, 0x81cd, "Sony VAIO", ALC260_BASIC),
	SND_PCI_QUIRK(0x10cf, 0x1326, "Fujitsu S702X", ALC260_FUJITSU_S702X),
	SND_PCI_QUIRK(0x152d, 0x0729, "CTL U553W", ALC260_BASIC),
	SND_PCI_QUIRK(0x161f, 0x2057, "Replacer 672V", ALC260_REPLACER_672V),
	SND_PCI_QUIRK(0x1631, 0xc017, "PB V7900", ALC260_WILL),
	{}
};

static struct alc_config_preset alc260_presets[] = {
	[ALC260_BASIC] = {
		.mixers = { alc260_base_output_mixer,
			    alc260_input_mixer,
			    alc260_pc_beep_mixer,
			    alc260_capture_mixer },
		.init_verbs = { alc260_init_verbs },
		.num_dacs = ARRAY_SIZE(alc260_dac_nids),
		.dac_nids = alc260_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc260_adc_nids),
		.adc_nids = alc260_adc_nids,
		.num_channel_mode = ARRAY_SIZE(alc260_modes),
		.channel_mode = alc260_modes,
		.input_mux = &alc260_capture_source,
	},
	[ALC260_HP] = {
		.mixers = { alc260_hp_output_mixer,
			    alc260_input_mixer,
			    alc260_capture_alt_mixer },
		.init_verbs = { alc260_init_verbs,
				alc260_hp_unsol_verbs },
		.num_dacs = ARRAY_SIZE(alc260_dac_nids),
		.dac_nids = alc260_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc260_hp_adc_nids),
		.adc_nids = alc260_hp_adc_nids,
		.num_channel_mode = ARRAY_SIZE(alc260_modes),
		.channel_mode = alc260_modes,
		.input_mux = &alc260_capture_source,
		.unsol_event = alc260_hp_unsol_event,
		.init_hook = alc260_hp_automute,
	},
	[ALC260_HP_3013] = {
		.mixers = { alc260_hp_3013_mixer,
			    alc260_input_mixer,
			    alc260_capture_alt_mixer },
		.init_verbs = { alc260_hp_3013_init_verbs,
				alc260_hp_3013_unsol_verbs },
		.num_dacs = ARRAY_SIZE(alc260_dac_nids),
		.dac_nids = alc260_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc260_hp_adc_nids),
		.adc_nids = alc260_hp_adc_nids,
		.num_channel_mode = ARRAY_SIZE(alc260_modes),
		.channel_mode = alc260_modes,
		.input_mux = &alc260_capture_source,
		.unsol_event = alc260_hp_3013_unsol_event,
		.init_hook = alc260_hp_3013_automute,
	},
	[ALC260_FUJITSU_S702X] = {
		.mixers = { alc260_fujitsu_mixer,
			    alc260_capture_mixer },
		.init_verbs = { alc260_fujitsu_init_verbs },
		.num_dacs = ARRAY_SIZE(alc260_dac_nids),
		.dac_nids = alc260_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc260_dual_adc_nids),
		.adc_nids = alc260_dual_adc_nids,
		.num_channel_mode = ARRAY_SIZE(alc260_modes),
		.channel_mode = alc260_modes,
		.num_mux_defs = ARRAY_SIZE(alc260_fujitsu_capture_sources),
		.input_mux = alc260_fujitsu_capture_sources,
	},
	[ALC260_ACER] = {
		.mixers = { alc260_acer_mixer,
			    alc260_capture_mixer },
		.init_verbs = { alc260_acer_init_verbs },
		.num_dacs = ARRAY_SIZE(alc260_dac_nids),
		.dac_nids = alc260_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc260_dual_adc_nids),
		.adc_nids = alc260_dual_adc_nids,
		.num_channel_mode = ARRAY_SIZE(alc260_modes),
		.channel_mode = alc260_modes,
		.num_mux_defs = ARRAY_SIZE(alc260_acer_capture_sources),
		.input_mux = alc260_acer_capture_sources,
	},
	[ALC260_WILL] = {
		.mixers = { alc260_will_mixer,
			    alc260_capture_mixer },
		.init_verbs = { alc260_init_verbs, alc260_will_verbs },
		.num_dacs = ARRAY_SIZE(alc260_dac_nids),
		.dac_nids = alc260_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc260_adc_nids),
		.adc_nids = alc260_adc_nids,
		.dig_out_nid = ALC260_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc260_modes),
		.channel_mode = alc260_modes,
		.input_mux = &alc260_capture_source,
	},
	[ALC260_REPLACER_672V] = {
		.mixers = { alc260_replacer_672v_mixer,
			    alc260_capture_mixer },
		.init_verbs = { alc260_init_verbs, alc260_replacer_672v_verbs },
		.num_dacs = ARRAY_SIZE(alc260_dac_nids),
		.dac_nids = alc260_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc260_adc_nids),
		.adc_nids = alc260_adc_nids,
		.dig_out_nid = ALC260_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc260_modes),
		.channel_mode = alc260_modes,
		.input_mux = &alc260_capture_source,
		.unsol_event = alc260_replacer_672v_unsol_event,
		.init_hook = alc260_replacer_672v_automute,
	},
#ifdef CONFIG_SND_DEBUG
	[ALC260_TEST] = {
		.mixers = { alc260_test_mixer,
			    alc260_capture_mixer },
		.init_verbs = { alc260_test_init_verbs },
		.num_dacs = ARRAY_SIZE(alc260_test_dac_nids),
		.dac_nids = alc260_test_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc260_test_adc_nids),
		.adc_nids = alc260_test_adc_nids,
		.num_channel_mode = ARRAY_SIZE(alc260_modes),
		.channel_mode = alc260_modes,
		.num_mux_defs = ARRAY_SIZE(alc260_test_capture_sources),
		.input_mux = alc260_test_capture_sources,
	},
#endif
};

static int patch_alc260(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err, board_config;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	board_config = snd_hda_check_board_config(codec, ALC260_MODEL_LAST,
						  alc260_models,
						  alc260_cfg_tbl);
	if (board_config < 0) {
		snd_printd(KERN_INFO "hda_codec: Unknown model for ALC260, "
			   "trying auto-probe from BIOS...\n");
		board_config = ALC260_AUTO;
	}

	if (board_config == ALC260_AUTO) {
		/* automatic parse from the BIOS config */
		err = alc260_parse_auto_config(codec);
		if (err < 0) {
			alc_free(codec);
			return err;
		} else if (!err) {
			printk(KERN_INFO
			       "hda_codec: Cannot set up configuration "
			       "from BIOS.  Using base mode...\n");
			board_config = ALC260_BASIC;
		}
	}

	if (board_config != ALC260_AUTO)
		setup_preset(spec, &alc260_presets[board_config]);

	spec->stream_name_analog = "ALC260 Analog";
	spec->stream_analog_playback = &alc260_pcm_analog_playback;
	spec->stream_analog_capture = &alc260_pcm_analog_capture;

	spec->stream_name_digital = "ALC260 Digital";
	spec->stream_digital_playback = &alc260_pcm_digital_playback;
	spec->stream_digital_capture = &alc260_pcm_digital_capture;

	spec->vmaster_nid = 0x08;

	codec->patch_ops = alc_patch_ops;
	if (board_config == ALC260_AUTO)
		spec->init_hook = alc260_auto_init;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	if (!spec->loopback.amplist)
		spec->loopback.amplist = alc260_loopbacks;
#endif

	return 0;
static int alc260_parse_auto_config(struct hda_codec *codec)
{
	static const hda_nid_t alc260_ignore[] = { 0x17, 0 };
	static const hda_nid_t alc260_ssids[] = { 0x10, 0x15, 0x0f, 0 };
	return alc_parse_auto_config(codec, alc260_ignore, alc260_ssids);
}

/*
 * Pin config fixes
 */
enum {
	ALC260_FIXUP_HP_DC5750,
	ALC260_FIXUP_HP_PIN_0F,
	ALC260_FIXUP_COEF,
	ALC260_FIXUP_GPIO1,
	ALC260_FIXUP_GPIO1_TOGGLE,
	ALC260_FIXUP_REPLACER,
	ALC260_FIXUP_HP_B1900,
	ALC260_FIXUP_KN1,
	ALC260_FIXUP_FSC_S7020,
	ALC260_FIXUP_FSC_S7020_JWSE,
	ALC260_FIXUP_VAIO_PINS,
};

static void alc260_gpio1_automute(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	snd_hda_codec_write(codec, 0x01, 0, AC_VERB_SET_GPIO_DATA,
			    spec->gen.hp_jack_present);
}

static void alc260_fixup_gpio1_toggle(struct hda_codec *codec,
				      const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	if (action == HDA_FIXUP_ACT_PROBE) {
		/* although the machine has only one output pin, we need to
		 * toggle GPIO1 according to the jack state
		 */
		spec->gen.automute_hook = alc260_gpio1_automute;
		spec->gen.detect_hp = 1;
		spec->gen.automute_speaker = 1;
		spec->gen.autocfg.hp_pins[0] = 0x0f; /* copy it for automute */
		snd_hda_jack_detect_enable_callback(codec, 0x0f,
						    snd_hda_gen_hp_automute);
		snd_hda_add_verbs(codec, alc_gpio1_init_verbs);
	}
}

static void alc260_fixup_kn1(struct hda_codec *codec,
			     const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	static const struct hda_pintbl pincfgs[] = {
		{ 0x0f, 0x02214000 }, /* HP/speaker */
		{ 0x12, 0x90a60160 }, /* int mic */
		{ 0x13, 0x02a19000 }, /* ext mic */
		{ 0x18, 0x01446000 }, /* SPDIF out */
		/* disable bogus I/O pins */
		{ 0x10, 0x411111f0 },
		{ 0x11, 0x411111f0 },
		{ 0x14, 0x411111f0 },
		{ 0x15, 0x411111f0 },
		{ 0x16, 0x411111f0 },
		{ 0x17, 0x411111f0 },
		{ 0x19, 0x411111f0 },
		{ }
	};

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		snd_hda_apply_pincfgs(codec, pincfgs);
		break;
	case HDA_FIXUP_ACT_PROBE:
		spec->init_amp = ALC_INIT_NONE;
		break;
	}
}

static void alc260_fixup_fsc_s7020(struct hda_codec *codec,
				   const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	if (action == HDA_FIXUP_ACT_PROBE)
		spec->init_amp = ALC_INIT_NONE;
}

static void alc260_fixup_fsc_s7020_jwse(struct hda_codec *codec,
				   const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->gen.add_jack_modes = 1;
		spec->gen.hp_mic = 1;
	}
}

static const struct hda_fixup alc260_fixups[] = {
	[ALC260_FIXUP_HP_DC5750] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x11, 0x90130110 }, /* speaker */
			{ }
		}
	},
	[ALC260_FIXUP_HP_PIN_0F] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x0f, 0x01214000 }, /* HP */
			{ }
		}
	},
	[ALC260_FIXUP_COEF] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x1a, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x1a, AC_VERB_SET_PROC_COEF,  0x3040 },
			{ }
		},
	},
	[ALC260_FIXUP_GPIO1] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = alc_gpio1_init_verbs,
	},
	[ALC260_FIXUP_GPIO1_TOGGLE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc260_fixup_gpio1_toggle,
		.chained = true,
		.chain_id = ALC260_FIXUP_HP_PIN_0F,
	},
	[ALC260_FIXUP_REPLACER] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x1a, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x1a, AC_VERB_SET_PROC_COEF,  0x3050 },
			{ }
		},
		.chained = true,
		.chain_id = ALC260_FIXUP_GPIO1_TOGGLE,
	},
	[ALC260_FIXUP_HP_B1900] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc260_fixup_gpio1_toggle,
		.chained = true,
		.chain_id = ALC260_FIXUP_COEF,
	},
	[ALC260_FIXUP_KN1] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc260_fixup_kn1,
	},
	[ALC260_FIXUP_FSC_S7020] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc260_fixup_fsc_s7020,
	},
	[ALC260_FIXUP_FSC_S7020_JWSE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc260_fixup_fsc_s7020_jwse,
		.chained = true,
		.chain_id = ALC260_FIXUP_FSC_S7020,
	},
	[ALC260_FIXUP_VAIO_PINS] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			/* Pin configs are missing completely on some VAIOs */
			{ 0x0f, 0x01211020 },
			{ 0x10, 0x0001003f },
			{ 0x11, 0x411111f0 },
			{ 0x12, 0x01a15930 },
			{ 0x13, 0x411111f0 },
			{ 0x14, 0x411111f0 },
			{ 0x15, 0x411111f0 },
			{ 0x16, 0x411111f0 },
			{ 0x17, 0x411111f0 },
			{ 0x18, 0x411111f0 },
			{ 0x19, 0x411111f0 },
			{ }
		}
	},
};

static const struct snd_pci_quirk alc260_fixup_tbl[] = {
	SND_PCI_QUIRK(0x1025, 0x007b, "Acer C20x", ALC260_FIXUP_GPIO1),
	SND_PCI_QUIRK(0x1025, 0x007f, "Acer Aspire 9500", ALC260_FIXUP_COEF),
	SND_PCI_QUIRK(0x1025, 0x008f, "Acer", ALC260_FIXUP_GPIO1),
	SND_PCI_QUIRK(0x103c, 0x280a, "HP dc5750", ALC260_FIXUP_HP_DC5750),
	SND_PCI_QUIRK(0x103c, 0x30ba, "HP Presario B1900", ALC260_FIXUP_HP_B1900),
	SND_PCI_QUIRK(0x104d, 0x81bb, "Sony VAIO", ALC260_FIXUP_VAIO_PINS),
	SND_PCI_QUIRK(0x104d, 0x81e2, "Sony VAIO TX", ALC260_FIXUP_HP_PIN_0F),
	SND_PCI_QUIRK(0x10cf, 0x1326, "FSC LifeBook S7020", ALC260_FIXUP_FSC_S7020),
	SND_PCI_QUIRK(0x1509, 0x4540, "Favorit 100XS", ALC260_FIXUP_GPIO1),
	SND_PCI_QUIRK(0x152d, 0x0729, "Quanta KN1", ALC260_FIXUP_KN1),
	SND_PCI_QUIRK(0x161f, 0x2057, "Replacer 672V", ALC260_FIXUP_REPLACER),
	SND_PCI_QUIRK(0x1631, 0xc017, "PB V7900", ALC260_FIXUP_COEF),
	{}
};

static const struct hda_model_fixup alc260_fixup_models[] = {
	{.id = ALC260_FIXUP_GPIO1, .name = "gpio1"},
	{.id = ALC260_FIXUP_COEF, .name = "coef"},
	{.id = ALC260_FIXUP_FSC_S7020, .name = "fujitsu"},
	{.id = ALC260_FIXUP_FSC_S7020_JWSE, .name = "fujitsu-jwse"},
	{}
};

/*
 */
static int patch_alc260(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err;

	err = alc_alloc_spec(codec, 0x07);
	if (err < 0)
		return err;

	spec = codec->spec;
	/* as quite a few machines require HP amp for speaker outputs,
	 * it's easier to enable it unconditionally; even if it's unneeded,
	 * it's almost harmless.
	 */
	spec->gen.prefer_hp_amp = 1;
	spec->gen.beep_nid = 0x01;

	spec->shutup = alc_eapd_shutup;

	snd_hda_pick_fixup(codec, alc260_fixup_models, alc260_fixup_tbl,
			   alc260_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	/* automatic parse from the BIOS config */
	err = alc260_parse_auto_config(codec);
	if (err < 0)
		goto error;

	if (!spec->gen.no_analog)
		set_beep_amp(spec, 0x07, 0x05, HDA_INPUT);

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;

 error:
	alc_free(codec);
	return err;
}


/*
 * ALC882 support
 * ALC882/883/885/888/889 support
 *
 * ALC882 is almost identical with ALC880 but has cleaner and more flexible
 * configuration.  Each pin widget can choose any input DACs and a mixer.
 * Each ADC is connected from a mixer of all inputs.  This makes possible
 * 6-channel independent captures.
 *
 * In addition, an independent DAC for the multi-playback (not used in this
 * driver yet).
 */
#define ALC882_DIGOUT_NID	0x06
#define ALC882_DIGIN_NID	0x0a

static struct hda_channel_mode alc882_ch_modes[1] = {
	{ 8, NULL }
};

static hda_nid_t alc882_dac_nids[4] = {
	/* front, rear, clfe, rear_surr */
	0x02, 0x03, 0x04, 0x05
};

/* identical with ALC880 */
#define alc882_adc_nids		alc880_adc_nids
#define alc882_adc_nids_alt	alc880_adc_nids_alt

static hda_nid_t alc882_capsrc_nids[3] = { 0x24, 0x23, 0x22 };
static hda_nid_t alc882_capsrc_nids_alt[2] = { 0x23, 0x22 };

/* input MUX */
/* FIXME: should be a matrix-type input source selection */

static struct hda_input_mux alc882_capture_source = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x1 },
		{ "Line", 0x2 },
		{ "CD", 0x4 },
	},
};
#define alc882_mux_enum_info alc_mux_enum_info
#define alc882_mux_enum_get alc_mux_enum_get

static int alc882_mux_enum_put(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	const struct hda_input_mux *imux = spec->input_mux;
	unsigned int adc_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	hda_nid_t nid = spec->capsrc_nids ?
		spec->capsrc_nids[adc_idx] : spec->adc_nids[adc_idx];
	unsigned int *cur_val = &spec->cur_mux[adc_idx];
	unsigned int i, idx;

	idx = ucontrol->value.enumerated.item[0];
	if (idx >= imux->num_items)
		idx = imux->num_items - 1;
	if (*cur_val == idx)
		return 0;
	for (i = 0; i < imux->num_items; i++) {
		unsigned int v = (i == idx) ? 0 : HDA_AMP_MUTE;
		snd_hda_codec_amp_stereo(codec, nid, HDA_INPUT,
					 imux->items[i].index,
					 HDA_AMP_MUTE, v);
	}
	*cur_val = idx;
	return 1;
}

/*
 * 2ch mode
 */
static struct hda_verb alc882_3ST_ch2_init[] = {
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ } /* end */
};

/*
 * 6ch mode
 */
static struct hda_verb alc882_3ST_ch6_init[] = {
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x18, AC_VERB_SET_CONNECT_SEL, 0x02 },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x1a, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ } /* end */
};

static struct hda_channel_mode alc882_3ST_6ch_modes[2] = {
	{ 2, alc882_3ST_ch2_init },
	{ 6, alc882_3ST_ch6_init },
};

/*
 * 6ch mode
 */
static struct hda_verb alc882_sixstack_ch6_init[] = {
	{ 0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00 },
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ } /* end */
};

/*
 * 8ch mode
 */
static struct hda_verb alc882_sixstack_ch8_init[] = {
	{ 0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ } /* end */
};

static struct hda_channel_mode alc882_sixstack_modes[2] = {
	{ 6, alc882_sixstack_ch6_init },
	{ 8, alc882_sixstack_ch8_init },
};

/*
 * macbook pro ALC885 can switch LineIn to LineOut without loosing Mic
 */

/*
 * 2ch mode
 */
static struct hda_verb alc885_mbp_ch2_init[] = {
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{ 0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{ } /* end */
};

/*
 * 6ch mode
 */
static struct hda_verb alc885_mbp_ch6_init[] = {
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{ 0x1a, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ 0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{ 0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{ } /* end */
};

static struct hda_channel_mode alc885_mbp_6ch_modes[2] = {
	{ 2, alc885_mbp_ch2_init },
	{ 6, alc885_mbp_ch6_init },
};


/* Pin assignment: Front=0x14, Rear=0x15, CLFE=0x16, Side=0x17
 *                 Mic=0x18, Front Mic=0x19, Line-In=0x1a, HP=0x1b
 */
static struct snd_kcontrol_new alc882_base_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Side Playback Volume", 0x0f, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Side Playback Switch", 0x0f, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x0b, 0x05, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc885_mbp3_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x00, HDA_OUTPUT),
	HDA_BIND_MUTE   ("Front Playback Switch", 0x0c, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE  ("Speaker Playback Switch", 0x14, 0x00, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line-Out Playback Volume", 0x0d, 0x00, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE  ("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x00, HDA_INPUT),
	HDA_CODEC_MUTE  ("Mic Playback Switch", 0x0b, 0x00, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Boost", 0x1a, 0x00, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0x00, HDA_INPUT),
	{ } /* end */
};
static struct snd_kcontrol_new alc882_w2jc_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x0b, 0x05, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc882_targa_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost", 0x19, 0, HDA_INPUT),
	{ } /* end */
};

/* Pin assignment: Front=0x14, HP = 0x15, Front = 0x16, ???
 *                 Front Mic=0x18, Line In = 0x1a, Line In = 0x1b, CD = 0x1c
 */
static struct snd_kcontrol_new alc882_asus_a7j_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Mobile Front Playback Switch", 0x16, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mobile Line Playback Volume", 0x0b, 0x03, HDA_INPUT),
	HDA_CODEC_MUTE("Mobile Line Playback Switch", 0x0b, 0x03, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc882_asus_a7m_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x0b, 0x05, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc882_chmode_mixer[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
	},
	{ } /* end */
};

static struct hda_verb alc882_init_verbs[] = {
	/* Front mixer: unmute input/output amp left and right (volume = 0) */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* Rear mixer */
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* CLFE mixer */
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* Side mixer */
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},

	/* Front Pin: output 0 (0x0c) */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* Rear Pin: output 1 (0x0d) */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x01},
	/* CLFE Pin: output 2 (0x0e) */
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_CONNECT_SEL, 0x02},
	/* Side Pin: output 3 (0x0f) */
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x17, AC_VERB_SET_CONNECT_SEL, 0x03},
	/* Mic (rear) pin: input vref at 80% */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Front Mic pin: input vref at 80% */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Line In pin: input */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Line-2 In: Headphone output (output 0 - 0x0c) */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* CD pin widget for input */
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

	/* FIXME: use matrix-type input source selection */
	/* Mixer elements: 0x18, 19, 1a, 1b, 1c, 1d, 14, 15, 16, 17, 0b */
	/* Input mixer1: unmute Mic, F-Mic, Line, CD inputs */
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* Input mixer2 */
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* Input mixer3 */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* ADC1: mute amp left and right */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x07, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* ADC2: mute amp left and right */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* ADC3: mute amp left and right */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},

	{ }
};

static struct hda_verb alc882_eapd_verbs[] = {
	/* change to EAPD mode */
	{0x20, AC_VERB_SET_COEF_INDEX, 0x07},
	{0x20, AC_VERB_SET_PROC_COEF, 0x3060},
	{ }
};

/* Mac Pro test */
static struct snd_kcontrol_new alc882_macpro_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x18, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x0b, 0x02, HDA_INPUT),
	{ } /* end */
};

static struct hda_verb alc882_macpro_init_verbs[] = {
	/* Front mixer: unmute input/output amp left and right (volume = 0) */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* Front Pin: output 0 (0x0c) */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* Front Mic pin: input vref at 80% */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Speaker:  output */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x04},
	/* Headphone output (output 0 - 0x0c) */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x18, AC_VERB_SET_CONNECT_SEL, 0x00},

	/* FIXME: use matrix-type input source selection */
	/* Mixer elements: 0x18, 19, 1a, 1b, 1c, 1d, 14, 15, 16, 17, 0b */
	/* Input mixer1: unmute Mic, F-Mic, Line, CD inputs */
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* Input mixer2 */
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* Input mixer3 */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* ADC1: mute amp left and right */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x07, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* ADC2: mute amp left and right */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* ADC3: mute amp left and right */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},

	{ }
};

/* Macbook Pro rev3 */
static struct hda_verb alc885_mbp3_init_verbs[] = {
	/* Front mixer: unmute input/output amp left and right (volume = 0) */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* Rear mixer */
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* Front Pin: output 0 (0x0c) */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* HP Pin: output 0 (0x0d) */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc4},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	/* Mic (rear) pin: input vref at 80% */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Front Mic pin: input vref at 80% */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Line In pin: use output 1 when in LineOut mode */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x01},

	/* FIXME: use matrix-type input source selection */
	/* Mixer elements: 0x18, 19, 1a, 1b, 1c, 1d, 14, 15, 16, 17, 0b */
	/* Input mixer1: unmute Mic, F-Mic, Line, CD inputs */
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* Input mixer2 */
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* Input mixer3 */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* ADC1: mute amp left and right */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x07, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* ADC2: mute amp left and right */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* ADC3: mute amp left and right */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},

	{ }
};

/* iMac 24 mixer. */
static struct snd_kcontrol_new alc885_imac24_mixer[] = {
	HDA_CODEC_VOLUME("Master Playback Volume", 0x0c, 0x00, HDA_OUTPUT),
	HDA_CODEC_MUTE("Master Playback Switch", 0x0c, 0x00, HDA_INPUT),
	{ } /* end */
};

/* iMac 24 init verbs. */
static struct hda_verb alc885_imac24_init_verbs[] = {
	/* Internal speakers: output 0 (0x0c) */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x18, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* Internal speakers: output 0 (0x0c) */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* Headphone: output 0 (0x0c) */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	/* Front Mic: input vref at 80% */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{ }
};

/* Toggle speaker-output according to the hp-jack state */
static void alc885_imac24_automute(struct hda_codec *codec)
{
 	unsigned int present;

 	present = snd_hda_codec_read(codec, 0x14, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	snd_hda_codec_amp_stereo(codec, 0x18, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
	snd_hda_codec_amp_stereo(codec, 0x1a, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
}

/* Processes unsolicited events. */
static void alc885_imac24_unsol_event(struct hda_codec *codec,
				      unsigned int res)
{
	/* Headphone insertion or removal. */
	if ((res >> 26) == ALC880_HP_EVENT)
		alc885_imac24_automute(codec);
}

static void alc885_mbp3_automute(struct hda_codec *codec)
{
 	unsigned int present;

 	present = snd_hda_codec_read(codec, 0x15, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	snd_hda_codec_amp_stereo(codec, 0x14,  HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
	snd_hda_codec_amp_stereo(codec, 0x15, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, present ? 0 : HDA_AMP_MUTE);

}
static void alc885_mbp3_unsol_event(struct hda_codec *codec,
				    unsigned int res)
{
	/* Headphone insertion or removal. */
	if ((res >> 26) == ALC880_HP_EVENT)
		alc885_mbp3_automute(codec);
}


static struct hda_verb alc882_targa_verbs[] = {
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	
	{0x18, AC_VERB_SET_CONNECT_SEL, 0x02}, /* mic/clfe */
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x01}, /* line/surround */
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00}, /* HP */

	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{0x01, AC_VERB_SET_GPIO_MASK, 0x03},
	{0x01, AC_VERB_SET_GPIO_DIRECTION, 0x03},
	{0x01, AC_VERB_SET_GPIO_DATA, 0x03},
	{ } /* end */
};

/* toggle speaker-output according to the hp-jack state */
static void alc882_targa_automute(struct hda_codec *codec)
{
 	unsigned int present;
 
 	present = snd_hda_codec_read(codec, 0x14, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	snd_hda_codec_amp_stereo(codec, 0x1b, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
	snd_hda_codec_write_cache(codec, 1, 0, AC_VERB_SET_GPIO_DATA,
				  present ? 1 : 3);
}

static void alc882_targa_unsol_event(struct hda_codec *codec, unsigned int res)
{
	/* Looks like the unsol event is incompatible with the standard
	 * definition.  4bit tag is placed at 26 bit!
	 */
	if (((res >> 26) == ALC880_HP_EVENT)) {
		alc882_targa_automute(codec);
	}
}

static struct hda_verb alc882_asus_a7j_verbs[] = {
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00}, /* Front */
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00}, /* HP */
	{0x16, AC_VERB_SET_CONNECT_SEL, 0x00}, /* Front */

	{0x18, AC_VERB_SET_CONNECT_SEL, 0x02}, /* mic/clfe */
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x01}, /* line/surround */
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00}, /* HP */
	{ } /* end */
};

static struct hda_verb alc882_asus_a7m_verbs[] = {
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
        
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00}, /* Front */
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00}, /* HP */
	{0x16, AC_VERB_SET_CONNECT_SEL, 0x00}, /* Front */

	{0x18, AC_VERB_SET_CONNECT_SEL, 0x02}, /* mic/clfe */
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x01}, /* line/surround */
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00}, /* HP */
 	{ } /* end */
};


/*
 * Pin config fixes
 */
enum {
	ALC882_FIXUP_ABIT_AW9D_MAX,
	ALC882_FIXUP_LENOVO_Y530,
	ALC882_FIXUP_PB_M5210,
	ALC882_FIXUP_ACER_ASPIRE_7736,
	ALC882_FIXUP_ASUS_W90V,
	ALC889_FIXUP_CD,
	ALC889_FIXUP_FRONT_HP_NO_PRESENCE,
	ALC889_FIXUP_VAIO_TT,
	ALC888_FIXUP_EEE1601,
	ALC882_FIXUP_EAPD,
	ALC883_FIXUP_EAPD,
	ALC883_FIXUP_ACER_EAPD,
	ALC882_FIXUP_GPIO1,
	ALC882_FIXUP_GPIO2,
	ALC882_FIXUP_GPIO3,
	ALC889_FIXUP_COEF,
	ALC882_FIXUP_ASUS_W2JC,
	ALC882_FIXUP_ACER_ASPIRE_4930G,
	ALC882_FIXUP_ACER_ASPIRE_8930G,
	ALC882_FIXUP_ASPIRE_8930G_VERBS,
	ALC885_FIXUP_MACPRO_GPIO,
	ALC889_FIXUP_DAC_ROUTE,
	ALC889_FIXUP_MBP_VREF,
	ALC889_FIXUP_IMAC91_VREF,
	ALC889_FIXUP_MBA11_VREF,
	ALC889_FIXUP_MBA21_VREF,
	ALC889_FIXUP_MP11_VREF,
	ALC889_FIXUP_MP41_VREF,
	ALC882_FIXUP_INV_DMIC,
	ALC882_FIXUP_NO_PRIMARY_HP,
	ALC887_FIXUP_ASUS_BASS,
	ALC887_FIXUP_BASS_CHMAP,
};

static void alc889_fixup_coef(struct hda_codec *codec,
			      const struct hda_fixup *fix, int action)
{
	if (action != HDA_FIXUP_ACT_INIT)
		return;
	alc_update_coef_idx(codec, 7, 0, 0x2030);
}

/* toggle speaker-output according to the hp-jack state */
static void alc882_gpio_mute(struct hda_codec *codec, int pin, int muted)
{
	unsigned int gpiostate, gpiomask, gpiodir;

	gpiostate = snd_hda_codec_read(codec, codec->afg, 0,
	gpiostate = snd_hda_codec_read(codec, codec->core.afg, 0,
				       AC_VERB_GET_GPIO_DATA, 0);

	if (!muted)
		gpiostate |= (1 << pin);
	else
		gpiostate &= ~(1 << pin);

	gpiomask = snd_hda_codec_read(codec, codec->afg, 0,
				      AC_VERB_GET_GPIO_MASK, 0);
	gpiomask |= (1 << pin);

	gpiodir = snd_hda_codec_read(codec, codec->afg, 0,
	gpiomask = snd_hda_codec_read(codec, codec->core.afg, 0,
				      AC_VERB_GET_GPIO_MASK, 0);
	gpiomask |= (1 << pin);

	gpiodir = snd_hda_codec_read(codec, codec->core.afg, 0,
				     AC_VERB_GET_GPIO_DIRECTION, 0);
	gpiodir |= (1 << pin);


	snd_hda_codec_write(codec, codec->afg, 0,
			    AC_VERB_SET_GPIO_MASK, gpiomask);
	snd_hda_codec_write(codec, codec->afg, 0,
	snd_hda_codec_write(codec, codec->core.afg, 0,
			    AC_VERB_SET_GPIO_MASK, gpiomask);
	snd_hda_codec_write(codec, codec->core.afg, 0,
			    AC_VERB_SET_GPIO_DIRECTION, gpiodir);

	msleep(1);

	snd_hda_codec_write(codec, codec->afg, 0,
	snd_hda_codec_write(codec, codec->core.afg, 0,
			    AC_VERB_SET_GPIO_DATA, gpiostate);
}

/* set up GPIO at initialization */
static void alc885_macpro_init_hook(struct hda_codec *codec)
{
static void alc885_fixup_macpro_gpio(struct hda_codec *codec,
				     const struct hda_fixup *fix, int action)
{
	if (action != HDA_FIXUP_ACT_INIT)
		return;
	alc882_gpio_mute(codec, 0, 0);
	alc882_gpio_mute(codec, 1, 0);
}

/* set up GPIO and update auto-muting at initialization */
static void alc885_imac24_init_hook(struct hda_codec *codec)
{
	alc885_macpro_init_hook(codec);
	alc885_imac24_automute(codec);
}

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static struct hda_verb alc882_auto_init_verbs[] = {
	/*
	 * Unmute ADC0-2 and set the default input to mic-in
	 */
	{0x07, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	/* Mute input amps (CD, Line In, Mic 1 & Mic 2) of the analog-loopback
	 * mixer widget
	 * Note: PASD motherboards uses the Line In 2 as the input for
	 * front panel mic (mic 2)
	 */
	/* Amp Indices: Mic1 = 0, Mic2 = 1, Line1 = 2, Line2 = 3, CD = 4 */
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},

	/*
	 * Set up output mixers (0x0c - 0x0f)
	 */
	/* set vol=0 to output mixers */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	/* set up input amps for analog loopback */
	/* Amp Indices: DAC = 0, mixer = 1 */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x26, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x26, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	/* FIXME: use matrix-type input source selection */
	/* Mixer elements: 0x18, 19, 1a, 1b, 1c, 1d, 14, 15, 16, 17, 0b */
	/* Input mixer1: unmute Mic, F-Mic, Line, CD inputs */
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x03 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x02 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x04 << 8))},
	/* Input mixer2 */
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x03 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x02 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x04 << 8))},
	/* Input mixer3 */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x03 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x02 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x04 << 8))},

	{ }
};

/* capture mixer elements */
static struct snd_kcontrol_new alc882_capture_alt_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = alc882_mux_enum_info,
		.get = alc882_mux_enum_get,
		.put = alc882_mux_enum_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc882_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x07, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x07, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 2, 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 2, 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 3,
		.info = alc882_mux_enum_info,
		.get = alc882_mux_enum_get,
		.put = alc882_mux_enum_put,
	},
	{ } /* end */
};

#ifdef CONFIG_SND_HDA_POWER_SAVE
#define alc882_loopbacks	alc880_loopbacks
#endif

/* pcm configuration: identiacal with ALC880 */
#define alc882_pcm_analog_playback	alc880_pcm_analog_playback
#define alc882_pcm_analog_capture	alc880_pcm_analog_capture
#define alc882_pcm_digital_playback	alc880_pcm_digital_playback
#define alc882_pcm_digital_capture	alc880_pcm_digital_capture

/*
 * configuration and preset
 */
static const char *alc882_models[ALC882_MODEL_LAST] = {
	[ALC882_3ST_DIG]	= "3stack-dig",
	[ALC882_6ST_DIG]	= "6stack-dig",
	[ALC882_ARIMA]		= "arima",
	[ALC882_W2JC]		= "w2jc",
	[ALC882_TARGA]		= "targa",
	[ALC882_ASUS_A7J]	= "asus-a7j",
	[ALC882_ASUS_A7M]	= "asus-a7m",
	[ALC885_MACPRO]		= "macpro",
	[ALC885_MBP3]		= "mbp3",
	[ALC885_IMAC24]		= "imac24",
	[ALC882_AUTO]		= "auto",
};

static struct snd_pci_quirk alc882_cfg_tbl[] = {
	SND_PCI_QUIRK(0x1019, 0x6668, "ECS", ALC882_6ST_DIG),
	SND_PCI_QUIRK(0x1043, 0x060d, "Asus A7J", ALC882_ASUS_A7J),
	SND_PCI_QUIRK(0x1043, 0x1243, "Asus A7J", ALC882_ASUS_A7J),
	SND_PCI_QUIRK(0x1043, 0x13c2, "Asus A7M", ALC882_ASUS_A7M),
	SND_PCI_QUIRK(0x1043, 0x1971, "Asus W2JC", ALC882_W2JC),
	SND_PCI_QUIRK(0x1043, 0x817f, "Asus P5LD2", ALC882_6ST_DIG),
	SND_PCI_QUIRK(0x1043, 0x81d8, "Asus P5WD", ALC882_6ST_DIG),
	SND_PCI_QUIRK(0x105b, 0x6668, "Foxconn", ALC882_6ST_DIG),
	SND_PCI_QUIRK(0x1458, 0xa002, "Gigabyte P35 DS3R", ALC882_6ST_DIG),
	SND_PCI_QUIRK(0x1462, 0x28fb, "Targa T8", ALC882_TARGA), /* MSI-1049 T8  */
	SND_PCI_QUIRK(0x1462, 0x6668, "MSI", ALC882_6ST_DIG),
	SND_PCI_QUIRK(0x161f, 0x2054, "Arima W820", ALC882_ARIMA),
	{}
};

static struct alc_config_preset alc882_presets[] = {
	[ALC882_3ST_DIG] = {
		.mixers = { alc882_base_mixer },
		.init_verbs = { alc882_init_verbs },
		.num_dacs = ARRAY_SIZE(alc882_dac_nids),
		.dac_nids = alc882_dac_nids,
		.dig_out_nid = ALC882_DIGOUT_NID,
		.dig_in_nid = ALC882_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc882_ch_modes),
		.channel_mode = alc882_ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc882_capture_source,
	},
	[ALC882_6ST_DIG] = {
		.mixers = { alc882_base_mixer, alc882_chmode_mixer },
		.init_verbs = { alc882_init_verbs },
		.num_dacs = ARRAY_SIZE(alc882_dac_nids),
		.dac_nids = alc882_dac_nids,
		.dig_out_nid = ALC882_DIGOUT_NID,
		.dig_in_nid = ALC882_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc882_sixstack_modes),
		.channel_mode = alc882_sixstack_modes,
		.input_mux = &alc882_capture_source,
	},
	[ALC882_ARIMA] = {
		.mixers = { alc882_base_mixer, alc882_chmode_mixer },
		.init_verbs = { alc882_init_verbs, alc882_eapd_verbs },
		.num_dacs = ARRAY_SIZE(alc882_dac_nids),
		.dac_nids = alc882_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc882_sixstack_modes),
		.channel_mode = alc882_sixstack_modes,
		.input_mux = &alc882_capture_source,
	},
	[ALC882_W2JC] = {
		.mixers = { alc882_w2jc_mixer, alc882_chmode_mixer },
		.init_verbs = { alc882_init_verbs, alc882_eapd_verbs,
				alc880_gpio1_init_verbs },
		.num_dacs = ARRAY_SIZE(alc882_dac_nids),
		.dac_nids = alc882_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc880_threestack_modes),
		.channel_mode = alc880_threestack_modes,
		.need_dac_fix = 1,
		.input_mux = &alc882_capture_source,
		.dig_out_nid = ALC882_DIGOUT_NID,
	},
	[ALC885_MBP3] = {
		.mixers = { alc885_mbp3_mixer, alc882_chmode_mixer },
		.init_verbs = { alc885_mbp3_init_verbs,
				alc880_gpio1_init_verbs },
		.num_dacs = ARRAY_SIZE(alc882_dac_nids),
		.dac_nids = alc882_dac_nids,
		.channel_mode = alc885_mbp_6ch_modes,
		.num_channel_mode = ARRAY_SIZE(alc885_mbp_6ch_modes),
		.input_mux = &alc882_capture_source,
		.dig_out_nid = ALC882_DIGOUT_NID,
		.dig_in_nid = ALC882_DIGIN_NID,
		.unsol_event = alc885_mbp3_unsol_event,
		.init_hook = alc885_mbp3_automute,
	},
	[ALC885_MACPRO] = {
		.mixers = { alc882_macpro_mixer },
		.init_verbs = { alc882_macpro_init_verbs },
		.num_dacs = ARRAY_SIZE(alc882_dac_nids),
		.dac_nids = alc882_dac_nids,
		.dig_out_nid = ALC882_DIGOUT_NID,
		.dig_in_nid = ALC882_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc882_ch_modes),
		.channel_mode = alc882_ch_modes,
		.input_mux = &alc882_capture_source,
		.init_hook = alc885_macpro_init_hook,
	},
	[ALC885_IMAC24] = {
		.mixers = { alc885_imac24_mixer },
		.init_verbs = { alc885_imac24_init_verbs },
		.num_dacs = ARRAY_SIZE(alc882_dac_nids),
		.dac_nids = alc882_dac_nids,
		.dig_out_nid = ALC882_DIGOUT_NID,
		.dig_in_nid = ALC882_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc882_ch_modes),
		.channel_mode = alc882_ch_modes,
		.input_mux = &alc882_capture_source,
		.unsol_event = alc885_imac24_unsol_event,
		.init_hook = alc885_imac24_init_hook,
	},
	[ALC882_TARGA] = {
		.mixers = { alc882_targa_mixer, alc882_chmode_mixer,
			    alc882_capture_mixer },
		.init_verbs = { alc882_init_verbs, alc882_targa_verbs},
		.num_dacs = ARRAY_SIZE(alc882_dac_nids),
		.dac_nids = alc882_dac_nids,
		.dig_out_nid = ALC882_DIGOUT_NID,
		.num_adc_nids = ARRAY_SIZE(alc882_adc_nids),
		.adc_nids = alc882_adc_nids,
		.capsrc_nids = alc882_capsrc_nids,
		.num_channel_mode = ARRAY_SIZE(alc882_3ST_6ch_modes),
		.channel_mode = alc882_3ST_6ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc882_capture_source,
		.unsol_event = alc882_targa_unsol_event,
		.init_hook = alc882_targa_automute,
	},
	[ALC882_ASUS_A7J] = {
		.mixers = { alc882_asus_a7j_mixer, alc882_chmode_mixer,
			    alc882_capture_mixer },
		.init_verbs = { alc882_init_verbs, alc882_asus_a7j_verbs},
		.num_dacs = ARRAY_SIZE(alc882_dac_nids),
		.dac_nids = alc882_dac_nids,
		.dig_out_nid = ALC882_DIGOUT_NID,
		.num_adc_nids = ARRAY_SIZE(alc882_adc_nids),
		.adc_nids = alc882_adc_nids,
		.capsrc_nids = alc882_capsrc_nids,
		.num_channel_mode = ARRAY_SIZE(alc882_3ST_6ch_modes),
		.channel_mode = alc882_3ST_6ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc882_capture_source,
	},	
	[ALC882_ASUS_A7M] = {
		.mixers = { alc882_asus_a7m_mixer, alc882_chmode_mixer },
		.init_verbs = { alc882_init_verbs, alc882_eapd_verbs,
				alc880_gpio1_init_verbs,
				alc882_asus_a7m_verbs },
		.num_dacs = ARRAY_SIZE(alc882_dac_nids),
		.dac_nids = alc882_dac_nids,
		.dig_out_nid = ALC882_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_threestack_modes),
		.channel_mode = alc880_threestack_modes,
		.need_dac_fix = 1,
		.input_mux = &alc882_capture_source,
	},	
};


/*
 * Pin config fixes
 */
enum { 
	PINFIX_ABIT_AW9D_MAX
};

static struct alc_pincfg alc882_abit_aw9d_pinfix[] = {
	{ 0x15, 0x01080104 }, /* side */
	{ 0x16, 0x01011012 }, /* rear */
	{ 0x17, 0x01016011 }, /* clfe */
	{ }
};

static const struct alc_pincfg *alc882_pin_fixes[] = {
	[PINFIX_ABIT_AW9D_MAX] = alc882_abit_aw9d_pinfix,
};

static struct snd_pci_quirk alc882_pinfix_tbl[] = {
	SND_PCI_QUIRK(0x147b, 0x107a, "Abit AW9D-MAX", PINFIX_ABIT_AW9D_MAX),
/* Fix the connection of some pins for ALC889:
 * At least, Acer Aspire 5935 shows the connections to DAC3/4 don't
 * work correctly (bko#42740)
 */
static void alc889_fixup_dac_route(struct hda_codec *codec,
				   const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		/* fake the connections during parsing the tree */
		hda_nid_t conn1[2] = { 0x0c, 0x0d };
		hda_nid_t conn2[2] = { 0x0e, 0x0f };
		snd_hda_override_conn_list(codec, 0x14, 2, conn1);
		snd_hda_override_conn_list(codec, 0x15, 2, conn1);
		snd_hda_override_conn_list(codec, 0x18, 2, conn2);
		snd_hda_override_conn_list(codec, 0x1a, 2, conn2);
	} else if (action == HDA_FIXUP_ACT_PROBE) {
		/* restore the connections */
		hda_nid_t conn[5] = { 0x0c, 0x0d, 0x0e, 0x0f, 0x26 };
		snd_hda_override_conn_list(codec, 0x14, 5, conn);
		snd_hda_override_conn_list(codec, 0x15, 5, conn);
		snd_hda_override_conn_list(codec, 0x18, 5, conn);
		snd_hda_override_conn_list(codec, 0x1a, 5, conn);
	}
}

/* Set VREF on HP pin */
static void alc889_fixup_mbp_vref(struct hda_codec *codec,
				  const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	static hda_nid_t nids[3] = { 0x14, 0x15, 0x19 };
	int i;

	if (action != HDA_FIXUP_ACT_INIT)
		return;
	for (i = 0; i < ARRAY_SIZE(nids); i++) {
		unsigned int val = snd_hda_codec_get_pincfg(codec, nids[i]);
		if (get_defcfg_device(val) != AC_JACK_HP_OUT)
			continue;
		val = snd_hda_codec_get_pin_target(codec, nids[i]);
		val |= AC_PINCTL_VREF_80;
		snd_hda_set_pin_ctl(codec, nids[i], val);
		spec->gen.keep_vref_in_automute = 1;
		break;
	}
}

static void alc889_fixup_mac_pins(struct hda_codec *codec,
				  const hda_nid_t *nids, int num_nids)
{
	struct alc_spec *spec = codec->spec;
	int i;

	for (i = 0; i < num_nids; i++) {
		unsigned int val;
		val = snd_hda_codec_get_pin_target(codec, nids[i]);
		val |= AC_PINCTL_VREF_50;
		snd_hda_set_pin_ctl(codec, nids[i], val);
	}
	spec->gen.keep_vref_in_automute = 1;
}

/* Set VREF on speaker pins on imac91 */
static void alc889_fixup_imac91_vref(struct hda_codec *codec,
				     const struct hda_fixup *fix, int action)
{
	static hda_nid_t nids[2] = { 0x18, 0x1a };

	if (action == HDA_FIXUP_ACT_INIT)
		alc889_fixup_mac_pins(codec, nids, ARRAY_SIZE(nids));
}

/* Set VREF on speaker pins on mba11 */
static void alc889_fixup_mba11_vref(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	static hda_nid_t nids[1] = { 0x18 };

	if (action == HDA_FIXUP_ACT_INIT)
		alc889_fixup_mac_pins(codec, nids, ARRAY_SIZE(nids));
}

/* Set VREF on speaker pins on mba21 */
static void alc889_fixup_mba21_vref(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	static hda_nid_t nids[2] = { 0x18, 0x19 };

	if (action == HDA_FIXUP_ACT_INIT)
		alc889_fixup_mac_pins(codec, nids, ARRAY_SIZE(nids));
}

/* Don't take HP output as primary
 * Strangely, the speaker output doesn't work on Vaio Z and some Vaio
 * all-in-one desktop PCs (for example VGC-LN51JGB) through DAC 0x05
 */
static void alc882_fixup_no_primary_hp(struct hda_codec *codec,
				       const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->gen.no_primary_hp = 1;
		spec->gen.no_multi_io = 1;
	}
}

static void alc_fixup_bass_chmap(struct hda_codec *codec,
				 const struct hda_fixup *fix, int action);

static const struct hda_fixup alc882_fixups[] = {
	[ALC882_FIXUP_ABIT_AW9D_MAX] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x15, 0x01080104 }, /* side */
			{ 0x16, 0x01011012 }, /* rear */
			{ 0x17, 0x01016011 }, /* clfe */
			{ }
		}
	},
	[ALC882_FIXUP_LENOVO_Y530] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x15, 0x99130112 }, /* rear int speakers */
			{ 0x16, 0x99130111 }, /* subwoofer */
			{ }
		}
	},
	[ALC882_FIXUP_PB_M5210] = {
		.type = HDA_FIXUP_PINCTLS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, PIN_VREF50 },
			{}
		}
	},
	[ALC882_FIXUP_ACER_ASPIRE_7736] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_sku_ignore,
	},
	[ALC882_FIXUP_ASUS_W90V] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x16, 0x99130110 }, /* fix sequence for CLFE */
			{ }
		}
	},
	[ALC889_FIXUP_CD] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1c, 0x993301f0 }, /* CD */
			{ }
		}
	},
	[ALC889_FIXUP_FRONT_HP_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1b, 0x02214120 }, /* Front HP jack is flaky, disable jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC889_FIXUP_CD,
	},
	[ALC889_FIXUP_VAIO_TT] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x17, 0x90170111 }, /* hidden surround speaker */
			{ }
		}
	},
	[ALC888_FIXUP_EEE1601] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x0b },
			{ 0x20, AC_VERB_SET_PROC_COEF,  0x0838 },
			{ }
		}
	},
	[ALC882_FIXUP_EAPD] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* change to EAPD mode */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x3060 },
			{ }
		}
	},
	[ALC883_FIXUP_EAPD] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* change to EAPD mode */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x3070 },
			{ }
		}
	},
	[ALC883_FIXUP_ACER_EAPD] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* eanable EAPD on Acer laptops */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x3050 },
			{ }
		}
	},
	[ALC882_FIXUP_GPIO1] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = alc_gpio1_init_verbs,
	},
	[ALC882_FIXUP_GPIO2] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = alc_gpio2_init_verbs,
	},
	[ALC882_FIXUP_GPIO3] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = alc_gpio3_init_verbs,
	},
	[ALC882_FIXUP_ASUS_W2JC] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = alc_gpio1_init_verbs,
		.chained = true,
		.chain_id = ALC882_FIXUP_EAPD,
	},
	[ALC889_FIXUP_COEF] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc889_fixup_coef,
	},
	[ALC882_FIXUP_ACER_ASPIRE_4930G] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x16, 0x99130111 }, /* CLFE speaker */
			{ 0x17, 0x99130112 }, /* surround speaker */
			{ }
		},
		.chained = true,
		.chain_id = ALC882_FIXUP_GPIO1,
	},
	[ALC882_FIXUP_ACER_ASPIRE_8930G] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x16, 0x99130111 }, /* CLFE speaker */
			{ 0x1b, 0x99130112 }, /* surround speaker */
			{ }
		},
		.chained = true,
		.chain_id = ALC882_FIXUP_ASPIRE_8930G_VERBS,
	},
	[ALC882_FIXUP_ASPIRE_8930G_VERBS] = {
		/* additional init verbs for Acer Aspire 8930G */
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* Enable all DACs */
			/* DAC DISABLE/MUTE 1? */
			/*  setting bits 1-5 disables DAC nids 0x02-0x06
			 *  apparently. Init=0x38 */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x03 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x0000 },
			/* DAC DISABLE/MUTE 2? */
			/*  some bit here disables the other DACs.
			 *  Init=0x4900 */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x08 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x0000 },
			/* DMIC fix
			 * This laptop has a stereo digital microphone.
			 * The mics are only 1cm apart which makes the stereo
			 * useless. However, either the mic or the ALC889
			 * makes the signal become a difference/sum signal
			 * instead of standard stereo, which is annoying.
			 * So instead we flip this bit which makes the
			 * codec replicate the sum signal to both channels,
			 * turning it into a normal mono mic.
			 */
			/* DMIC_CONTROL? Init value = 0x0001 */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x0b },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x0003 },
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x3050 },
			{ }
		},
		.chained = true,
		.chain_id = ALC882_FIXUP_GPIO1,
	},
	[ALC885_FIXUP_MACPRO_GPIO] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc885_fixup_macpro_gpio,
	},
	[ALC889_FIXUP_DAC_ROUTE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc889_fixup_dac_route,
	},
	[ALC889_FIXUP_MBP_VREF] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc889_fixup_mbp_vref,
		.chained = true,
		.chain_id = ALC882_FIXUP_GPIO1,
	},
	[ALC889_FIXUP_IMAC91_VREF] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc889_fixup_imac91_vref,
		.chained = true,
		.chain_id = ALC882_FIXUP_GPIO1,
	},
	[ALC889_FIXUP_MBA11_VREF] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc889_fixup_mba11_vref,
		.chained = true,
		.chain_id = ALC889_FIXUP_MBP_VREF,
	},
	[ALC889_FIXUP_MBA21_VREF] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc889_fixup_mba21_vref,
		.chained = true,
		.chain_id = ALC889_FIXUP_MBP_VREF,
	},
	[ALC889_FIXUP_MP11_VREF] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc889_fixup_mba11_vref,
		.chained = true,
		.chain_id = ALC885_FIXUP_MACPRO_GPIO,
	},
	[ALC889_FIXUP_MP41_VREF] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc889_fixup_mbp_vref,
		.chained = true,
		.chain_id = ALC885_FIXUP_MACPRO_GPIO,
	},
	[ALC882_FIXUP_INV_DMIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_inv_dmic,
	},
	[ALC882_FIXUP_NO_PRIMARY_HP] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc882_fixup_no_primary_hp,
	},
	[ALC887_FIXUP_ASUS_BASS] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{0x16, 0x99130130}, /* bass speaker */
			{}
		},
		.chained = true,
		.chain_id = ALC887_FIXUP_BASS_CHMAP,
	},
	[ALC887_FIXUP_BASS_CHMAP] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_bass_chmap,
	},
};

static const struct snd_pci_quirk alc882_fixup_tbl[] = {
	SND_PCI_QUIRK(0x1025, 0x006c, "Acer Aspire 9810", ALC883_FIXUP_ACER_EAPD),
	SND_PCI_QUIRK(0x1025, 0x0090, "Acer Aspire", ALC883_FIXUP_ACER_EAPD),
	SND_PCI_QUIRK(0x1025, 0x0107, "Acer Aspire", ALC883_FIXUP_ACER_EAPD),
	SND_PCI_QUIRK(0x1025, 0x010a, "Acer Ferrari 5000", ALC883_FIXUP_ACER_EAPD),
	SND_PCI_QUIRK(0x1025, 0x0110, "Acer Aspire", ALC883_FIXUP_ACER_EAPD),
	SND_PCI_QUIRK(0x1025, 0x0112, "Acer Aspire 9303", ALC883_FIXUP_ACER_EAPD),
	SND_PCI_QUIRK(0x1025, 0x0121, "Acer Aspire 5920G", ALC883_FIXUP_ACER_EAPD),
	SND_PCI_QUIRK(0x1025, 0x013e, "Acer Aspire 4930G",
		      ALC882_FIXUP_ACER_ASPIRE_4930G),
	SND_PCI_QUIRK(0x1025, 0x013f, "Acer Aspire 5930G",
		      ALC882_FIXUP_ACER_ASPIRE_4930G),
	SND_PCI_QUIRK(0x1025, 0x0145, "Acer Aspire 8930G",
		      ALC882_FIXUP_ACER_ASPIRE_8930G),
	SND_PCI_QUIRK(0x1025, 0x0146, "Acer Aspire 6935G",
		      ALC882_FIXUP_ACER_ASPIRE_8930G),
	SND_PCI_QUIRK(0x1025, 0x0142, "Acer Aspire 7730G",
		      ALC882_FIXUP_ACER_ASPIRE_4930G),
	SND_PCI_QUIRK(0x1025, 0x0155, "Packard-Bell M5120", ALC882_FIXUP_PB_M5210),
	SND_PCI_QUIRK(0x1025, 0x015e, "Acer Aspire 6930G",
		      ALC882_FIXUP_ACER_ASPIRE_4930G),
	SND_PCI_QUIRK(0x1025, 0x0166, "Acer Aspire 6530G",
		      ALC882_FIXUP_ACER_ASPIRE_4930G),
	SND_PCI_QUIRK(0x1025, 0x021e, "Acer Aspire 5739G",
		      ALC882_FIXUP_ACER_ASPIRE_4930G),
	SND_PCI_QUIRK(0x1025, 0x0259, "Acer Aspire 5935", ALC889_FIXUP_DAC_ROUTE),
	SND_PCI_QUIRK(0x1025, 0x026b, "Acer Aspire 8940G", ALC882_FIXUP_ACER_ASPIRE_8930G),
	SND_PCI_QUIRK(0x1025, 0x0296, "Acer Aspire 7736z", ALC882_FIXUP_ACER_ASPIRE_7736),
	SND_PCI_QUIRK(0x1043, 0x13c2, "Asus A7M", ALC882_FIXUP_EAPD),
	SND_PCI_QUIRK(0x1043, 0x1873, "ASUS W90V", ALC882_FIXUP_ASUS_W90V),
	SND_PCI_QUIRK(0x1043, 0x1971, "Asus W2JC", ALC882_FIXUP_ASUS_W2JC),
	SND_PCI_QUIRK(0x1043, 0x835f, "Asus Eee 1601", ALC888_FIXUP_EEE1601),
	SND_PCI_QUIRK(0x1043, 0x84bc, "ASUS ET2700", ALC887_FIXUP_ASUS_BASS),
	SND_PCI_QUIRK(0x1043, 0x8691, "ASUS ROG Ranger VIII", ALC882_FIXUP_GPIO3),
	SND_PCI_QUIRK(0x104d, 0x9043, "Sony Vaio VGC-LN51JGB", ALC882_FIXUP_NO_PRIMARY_HP),
	SND_PCI_QUIRK(0x104d, 0x9044, "Sony VAIO AiO", ALC882_FIXUP_NO_PRIMARY_HP),
	SND_PCI_QUIRK(0x104d, 0x9047, "Sony Vaio TT", ALC889_FIXUP_VAIO_TT),
	SND_PCI_QUIRK(0x104d, 0x905a, "Sony Vaio Z", ALC882_FIXUP_NO_PRIMARY_HP),
	SND_PCI_QUIRK(0x104d, 0x9060, "Sony Vaio VPCL14M1R", ALC882_FIXUP_NO_PRIMARY_HP),

	/* All Apple entries are in codec SSIDs */
	SND_PCI_QUIRK(0x106b, 0x00a0, "MacBookPro 3,1", ALC889_FIXUP_MBP_VREF),
	SND_PCI_QUIRK(0x106b, 0x00a1, "Macbook", ALC889_FIXUP_MBP_VREF),
	SND_PCI_QUIRK(0x106b, 0x00a4, "MacbookPro 4,1", ALC889_FIXUP_MBP_VREF),
	SND_PCI_QUIRK(0x106b, 0x0c00, "Mac Pro", ALC889_FIXUP_MP11_VREF),
	SND_PCI_QUIRK(0x106b, 0x1000, "iMac 24", ALC885_FIXUP_MACPRO_GPIO),
	SND_PCI_QUIRK(0x106b, 0x2800, "AppleTV", ALC885_FIXUP_MACPRO_GPIO),
	SND_PCI_QUIRK(0x106b, 0x2c00, "MacbookPro rev3", ALC889_FIXUP_MBP_VREF),
	SND_PCI_QUIRK(0x106b, 0x3000, "iMac", ALC889_FIXUP_MBP_VREF),
	SND_PCI_QUIRK(0x106b, 0x3200, "iMac 7,1 Aluminum", ALC882_FIXUP_EAPD),
	SND_PCI_QUIRK(0x106b, 0x3400, "MacBookAir 1,1", ALC889_FIXUP_MBA11_VREF),
	SND_PCI_QUIRK(0x106b, 0x3500, "MacBookAir 2,1", ALC889_FIXUP_MBA21_VREF),
	SND_PCI_QUIRK(0x106b, 0x3600, "Macbook 3,1", ALC889_FIXUP_MBP_VREF),
	SND_PCI_QUIRK(0x106b, 0x3800, "MacbookPro 4,1", ALC889_FIXUP_MBP_VREF),
	SND_PCI_QUIRK(0x106b, 0x3e00, "iMac 24 Aluminum", ALC885_FIXUP_MACPRO_GPIO),
	SND_PCI_QUIRK(0x106b, 0x3f00, "Macbook 5,1", ALC889_FIXUP_IMAC91_VREF),
	SND_PCI_QUIRK(0x106b, 0x4000, "MacbookPro 5,1", ALC889_FIXUP_IMAC91_VREF),
	SND_PCI_QUIRK(0x106b, 0x4100, "Macmini 3,1", ALC889_FIXUP_IMAC91_VREF),
	SND_PCI_QUIRK(0x106b, 0x4200, "Mac Pro 4,1/5,1", ALC889_FIXUP_MP41_VREF),
	SND_PCI_QUIRK(0x106b, 0x4300, "iMac 9,1", ALC889_FIXUP_IMAC91_VREF),
	SND_PCI_QUIRK(0x106b, 0x4600, "MacbookPro 5,2", ALC889_FIXUP_IMAC91_VREF),
	SND_PCI_QUIRK(0x106b, 0x4900, "iMac 9,1 Aluminum", ALC889_FIXUP_IMAC91_VREF),
	SND_PCI_QUIRK(0x106b, 0x4a00, "Macbook 5,2", ALC889_FIXUP_MBA11_VREF),

	SND_PCI_QUIRK(0x1071, 0x8258, "Evesham Voyaeger", ALC882_FIXUP_EAPD),
	SND_PCI_QUIRK(0x1462, 0x7350, "MSI-7350", ALC889_FIXUP_CD),
	SND_PCI_QUIRK_VENDOR(0x1462, "MSI", ALC882_FIXUP_GPIO3),
	SND_PCI_QUIRK(0x1458, 0xa002, "Gigabyte EP45-DS3/Z87X-UD3H", ALC889_FIXUP_FRONT_HP_NO_PRESENCE),
	SND_PCI_QUIRK(0x147b, 0x107a, "Abit AW9D-MAX", ALC882_FIXUP_ABIT_AW9D_MAX),
	SND_PCI_QUIRK_VENDOR(0x1558, "Clevo laptop", ALC882_FIXUP_EAPD),
	SND_PCI_QUIRK(0x161f, 0x2054, "Medion laptop", ALC883_FIXUP_EAPD),
	SND_PCI_QUIRK(0x17aa, 0x3a0d, "Lenovo Y530", ALC882_FIXUP_LENOVO_Y530),
	SND_PCI_QUIRK(0x8086, 0x0022, "DX58SO", ALC889_FIXUP_COEF),
	{}
};

static const struct hda_model_fixup alc882_fixup_models[] = {
	{.id = ALC882_FIXUP_ACER_ASPIRE_4930G, .name = "acer-aspire-4930g"},
	{.id = ALC882_FIXUP_ACER_ASPIRE_8930G, .name = "acer-aspire-8930g"},
	{.id = ALC883_FIXUP_ACER_EAPD, .name = "acer-aspire"},
	{.id = ALC882_FIXUP_INV_DMIC, .name = "inv-dmic"},
	{.id = ALC882_FIXUP_NO_PRIMARY_HP, .name = "no-primary-hp"},
	{}
};

/*
 * BIOS auto configuration
 */
static void alc882_auto_set_output_and_unmute(struct hda_codec *codec,
					      hda_nid_t nid, int pin_type,
					      int dac_idx)
{
	/* set as output */
	struct alc_spec *spec = codec->spec;
	int idx;

	alc_set_pin_output(codec, nid, pin_type);
	if (spec->multiout.dac_nids[dac_idx] == 0x25)
		idx = 4;
	else
		idx = spec->multiout.dac_nids[dac_idx] - 2;
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_CONNECT_SEL, idx);

}

static void alc882_auto_init_multi_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i;

	alc_subsystem_id(codec, 0x15, 0x1b, 0x14);
	for (i = 0; i <= HDA_SIDE; i++) {
		hda_nid_t nid = spec->autocfg.line_out_pins[i];
		int pin_type = get_pin_type(spec->autocfg.line_out_type);
		if (nid)
			alc882_auto_set_output_and_unmute(codec, nid, pin_type,
							  i);
	}
}

static void alc882_auto_init_hp_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t pin;

	pin = spec->autocfg.hp_pins[0];
	if (pin) /* connect to front */
		/* use dac 0 */
		alc882_auto_set_output_and_unmute(codec, pin, PIN_HP, 0);
	pin = spec->autocfg.speaker_pins[0];
	if (pin)
		alc882_auto_set_output_and_unmute(codec, pin, PIN_OUT, 0);
}

#define alc882_is_input_pin(nid)	alc880_is_input_pin(nid)
#define ALC882_PIN_CD_NID		ALC880_PIN_CD_NID

static void alc882_auto_init_analog_input(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		hda_nid_t nid = spec->autocfg.input_pins[i];
		unsigned int vref;
		if (!nid)
			continue;
		vref = PIN_IN;
		if (1 /*i <= AUTO_PIN_FRONT_MIC*/) {
			unsigned int pincap;
			pincap = snd_hda_param_read(codec, nid, AC_PAR_PIN_CAP);
			if ((pincap >> AC_PINCAP_VREF_SHIFT) &
			    AC_PINCAP_VREF_80)
				vref = PIN_VREF80;
		}
		snd_hda_codec_write(codec, nid, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL, vref);
		if (get_wcaps(codec, nid) & AC_WCAP_OUT_AMP)
			snd_hda_codec_write(codec, nid, 0,
					    AC_VERB_SET_AMP_GAIN_MUTE,
					    AMP_OUT_MUTE);
	}
}

static void alc882_auto_init_input_src(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	const struct hda_input_mux *imux = spec->input_mux;
	int c;

	for (c = 0; c < spec->num_adc_nids; c++) {
		hda_nid_t conn_list[HDA_MAX_NUM_INPUTS];
		hda_nid_t nid = spec->capsrc_nids[c];
		int conns, mute, idx, item;

		conns = snd_hda_get_connections(codec, nid, conn_list,
						ARRAY_SIZE(conn_list));
		if (conns < 0)
			continue;
		for (idx = 0; idx < conns; idx++) {
			/* if the current connection is the selected one,
			 * unmute it as default - otherwise mute it
			 */
			mute = AMP_IN_MUTE(idx);
			for (item = 0; item < imux->num_items; item++) {
				if (imux->items[item].index == idx) {
					if (spec->cur_mux[c] == item)
						mute = AMP_IN_UNMUTE(idx);
					break;
				}
			}
			snd_hda_codec_write(codec, nid, 0,
					    AC_VERB_SET_AMP_GAIN_MUTE, mute);
		}
	}
}

/* add mic boosts if needed */
static int alc_auto_add_mic_boost(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int err;
	hda_nid_t nid;

	nid = spec->autocfg.input_pins[AUTO_PIN_MIC];
	if (nid && (get_wcaps(codec, nid) & AC_WCAP_IN_AMP)) {
		err = add_control(spec, ALC_CTL_WIDGET_VOL,
				  "Mic Boost",
				  HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_INPUT));
		if (err < 0)
			return err;
	}
	nid = spec->autocfg.input_pins[AUTO_PIN_FRONT_MIC];
	if (nid && (get_wcaps(codec, nid) & AC_WCAP_IN_AMP)) {
		err = add_control(spec, ALC_CTL_WIDGET_VOL,
				  "Front Mic Boost",
				  HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_INPUT));
		if (err < 0)
			return err;
	}
	return 0;
}

/* almost identical with ALC880 parser... */
static int alc882_parse_auto_config(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int err = alc880_parse_auto_config(codec);

	if (err < 0)
		return err;
	else if (!err)
		return 0; /* no config found */

	err = alc_auto_add_mic_boost(codec);
	if (err < 0)
		return err;

	/* hack - override the init verbs */
	spec->init_verbs[0] = alc882_auto_init_verbs;

	return 1; /* config found */
}

/* additional initialization for auto-configuration model */
static void alc882_auto_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	alc882_auto_init_multi_out(codec);
	alc882_auto_init_hp_out(codec);
	alc882_auto_init_analog_input(codec);
	alc882_auto_init_input_src(codec);
	if (spec->unsol_event)
		alc_sku_automute(codec);
}

static int patch_alc883(struct hda_codec *codec); /* called in patch_alc882() */

static int patch_alc882(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err, board_config;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	board_config = snd_hda_check_board_config(codec, ALC882_MODEL_LAST,
						  alc882_models,
						  alc882_cfg_tbl);

	if (board_config < 0 || board_config >= ALC882_MODEL_LAST) {
		/* Pick up systems that don't supply PCI SSID */
		switch (codec->subsystem_id) {
		case 0x106b0c00: /* Mac Pro */
			board_config = ALC885_MACPRO;
			break;
		case 0x106b1000: /* iMac 24 */
			board_config = ALC885_IMAC24;
			break;
		case 0x106b00a1: /* Macbook (might be wrong - PCI SSID?) */
		case 0x106b2c00: /* Macbook Pro rev3 */
		case 0x106b3600: /* Macbook 3.1 */
			board_config = ALC885_MBP3;
			break;
		default:
			/* ALC889A is handled better as ALC888-compatible */
			if (codec->revision_id == 0x100103) {
				alc_free(codec);
				return patch_alc883(codec);
			}
			printk(KERN_INFO "hda_codec: Unknown model for ALC882, "
		       			 "trying auto-probe from BIOS...\n");
			board_config = ALC882_AUTO;
		}
	}

	alc_fix_pincfg(codec, alc882_pinfix_tbl, alc882_pin_fixes);

	if (board_config == ALC882_AUTO) {
		/* automatic parse from the BIOS config */
		err = alc882_parse_auto_config(codec);
		if (err < 0) {
			alc_free(codec);
			return err;
		} else if (!err) {
			printk(KERN_INFO
			       "hda_codec: Cannot set up configuration "
			       "from BIOS.  Using base mode...\n");
			board_config = ALC882_3ST_DIG;
		}
	}

	if (board_config != ALC882_AUTO)
		setup_preset(spec, &alc882_presets[board_config]);

	if (codec->vendor_id == 0x10ec0885) {
		spec->stream_name_analog = "ALC885 Analog";
		spec->stream_name_digital = "ALC885 Digital";
	} else {
		spec->stream_name_analog = "ALC882 Analog";
		spec->stream_name_digital = "ALC882 Digital";
	}

	spec->stream_analog_playback = &alc882_pcm_analog_playback;
	spec->stream_analog_capture = &alc882_pcm_analog_capture;
	/* FIXME: setup DAC5 */
	/*spec->stream_analog_alt_playback = &alc880_pcm_analog_alt_playback;*/
	spec->stream_analog_alt_capture = &alc880_pcm_analog_alt_capture;

	spec->stream_digital_playback = &alc882_pcm_digital_playback;
	spec->stream_digital_capture = &alc882_pcm_digital_capture;

	if (!spec->adc_nids && spec->input_mux) {
		/* check whether NID 0x07 is valid */
		unsigned int wcap = get_wcaps(codec, 0x07);
		/* get type */
		wcap = (wcap & AC_WCAP_TYPE) >> AC_WCAP_TYPE_SHIFT;
		if (wcap != AC_WID_AUD_IN) {
			spec->adc_nids = alc882_adc_nids_alt;
			spec->num_adc_nids = ARRAY_SIZE(alc882_adc_nids_alt);
			spec->capsrc_nids = alc882_capsrc_nids_alt;
			spec->mixers[spec->num_mixers] =
				alc882_capture_alt_mixer;
			spec->num_mixers++;
		} else {
			spec->adc_nids = alc882_adc_nids;
			spec->num_adc_nids = ARRAY_SIZE(alc882_adc_nids);
			spec->capsrc_nids = alc882_capsrc_nids;
			spec->mixers[spec->num_mixers] = alc882_capture_mixer;
			spec->num_mixers++;
		}
	}

	spec->vmaster_nid = 0x0c;

	codec->patch_ops = alc_patch_ops;
	if (board_config == ALC882_AUTO)
		spec->init_hook = alc882_auto_init;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	if (!spec->loopback.amplist)
		spec->loopback.amplist = alc882_loopbacks;
#endif

	return 0;
}

/*
 * ALC883 support
 *
 * ALC883 is almost identical with ALC880 but has cleaner and more flexible
 * configuration.  Each pin widget can choose any input DACs and a mixer.
 * Each ADC is connected from a mixer of all inputs.  This makes possible
 * 6-channel independent captures.
 *
 * In addition, an independent DAC for the multi-playback (not used in this
 * driver yet).
 */
#define ALC883_DIGOUT_NID	0x06
#define ALC883_DIGIN_NID	0x0a

static hda_nid_t alc883_dac_nids[4] = {
	/* front, rear, clfe, rear_surr */
	0x02, 0x03, 0x04, 0x05
};

static hda_nid_t alc883_adc_nids[2] = {
	/* ADC1-2 */
	0x08, 0x09,
};

static hda_nid_t alc883_capsrc_nids[2] = { 0x23, 0x22 };

/* input MUX */
/* FIXME: should be a matrix-type input source selection */

static struct hda_input_mux alc883_capture_source = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x1 },
		{ "Line", 0x2 },
		{ "CD", 0x4 },
	},
};

static struct hda_input_mux alc883_3stack_6ch_intel = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x1 },
		{ "Front Mic", 0x0 },
		{ "Line", 0x2 },
		{ "CD", 0x4 },
	},
};

static struct hda_input_mux alc883_lenovo_101e_capture_source = {
	.num_items = 2,
	.items = {
		{ "Mic", 0x1 },
		{ "Line", 0x2 },
	},
};

static struct hda_input_mux alc883_lenovo_nb0763_capture_source = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x0 },
		{ "iMic", 0x1 },
		{ "Line", 0x2 },
		{ "CD", 0x4 },
	},
};

static struct hda_input_mux alc883_fujitsu_pi2515_capture_source = {
	.num_items = 2,
	.items = {
		{ "Mic", 0x0 },
		{ "Int Mic", 0x1 },
	},
};

#define alc883_mux_enum_info alc_mux_enum_info
#define alc883_mux_enum_get alc_mux_enum_get
/* ALC883 has the ALC882-type input selection */
#define alc883_mux_enum_put alc882_mux_enum_put

/*
 * 2ch mode
 */
static struct hda_channel_mode alc883_3ST_2ch_modes[1] = {
	{ 2, NULL }
};

/*
 * 2ch mode
 */
static struct hda_verb alc883_3ST_ch2_init[] = {
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ } /* end */
};

/*
 * 4ch mode
 */
static struct hda_verb alc883_3ST_ch4_init[] = {
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x1a, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ } /* end */
};

/*
 * 6ch mode
 */
static struct hda_verb alc883_3ST_ch6_init[] = {
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x18, AC_VERB_SET_CONNECT_SEL, 0x02 },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x1a, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ } /* end */
};

static struct hda_channel_mode alc883_3ST_6ch_modes[3] = {
	{ 2, alc883_3ST_ch2_init },
	{ 4, alc883_3ST_ch4_init },
	{ 6, alc883_3ST_ch6_init },
};

/*
 * 2ch mode
 */
static struct hda_verb alc883_3ST_ch2_intel_init[] = {
	{ 0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ } /* end */
};

/*
 * 4ch mode
 */
static struct hda_verb alc883_3ST_ch4_intel_init[] = {
	{ 0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x1a, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ } /* end */
};

/*
 * 6ch mode
 */
static struct hda_verb alc883_3ST_ch6_intel_init[] = {
	{ 0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x19, AC_VERB_SET_CONNECT_SEL, 0x02 },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x1a, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ } /* end */
};

static struct hda_channel_mode alc883_3ST_6ch_intel_modes[3] = {
	{ 2, alc883_3ST_ch2_intel_init },
	{ 4, alc883_3ST_ch4_intel_init },
	{ 6, alc883_3ST_ch6_intel_init },
};

/*
 * 6ch mode
 */
static struct hda_verb alc883_sixstack_ch6_init[] = {
	{ 0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00 },
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ } /* end */
};

/*
 * 8ch mode
 */
static struct hda_verb alc883_sixstack_ch8_init[] = {
	{ 0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ } /* end */
};

static struct hda_channel_mode alc883_sixstack_modes[2] = {
	{ 6, alc883_sixstack_ch6_init },
	{ 8, alc883_sixstack_ch8_init },
};

static struct hda_verb alc883_medion_eapd_verbs[] = {
        /* eanable EAPD on medion laptop */
	{0x20, AC_VERB_SET_COEF_INDEX, 0x07},
	{0x20, AC_VERB_SET_PROC_COEF, 0x3070},
	{ }
};

/* Pin assignment: Front=0x14, Rear=0x15, CLFE=0x16, Side=0x17
 *                 Mic=0x18, Front Mic=0x19, Line-In=0x1a, HP=0x1b
 */

static struct snd_kcontrol_new alc883_base_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Side Playback Volume", 0x0f, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Side Playback Switch", 0x0f, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = alc883_mux_enum_info,
		.get = alc883_mux_enum_get,
		.put = alc883_mux_enum_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc883_mitac_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = alc883_mux_enum_info,
		.get = alc883_mux_enum_get,
		.put = alc883_mux_enum_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc883_clevo_m720_mixer[] = {
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Int Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Int Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Int Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = alc883_mux_enum_info,
		.get = alc883_mux_enum_get,
		.put = alc883_mux_enum_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc883_2ch_fujitsu_pi2515_mixer[] = {
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Int Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Int Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Int Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = alc883_mux_enum_info,
		.get = alc883_mux_enum_get,
		.put = alc883_mux_enum_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc883_3ST_2ch_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = alc883_mux_enum_info,
		.get = alc883_mux_enum_get,
		.put = alc883_mux_enum_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc883_3ST_6ch_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = alc883_mux_enum_info,
		.get = alc883_mux_enum_get,
		.put = alc883_mux_enum_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc883_3ST_6ch_intel_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0,
			      HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = alc883_mux_enum_info,
		.get = alc883_mux_enum_get,
		.put = alc883_mux_enum_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc883_fivestack_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),

	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 1,
		.info = alc883_mux_enum_info,
		.get = alc883_mux_enum_get,
		.put = alc883_mux_enum_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc883_tagra_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = alc883_mux_enum_info,
		.get = alc883_mux_enum_get,
		.put = alc883_mux_enum_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc883_tagra_2ch_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Int Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Int Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Int Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = alc883_mux_enum_info,
		.get = alc883_mux_enum_get,
		.put = alc883_mux_enum_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc883_lenovo_101e_2ch_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 1,
		.info = alc883_mux_enum_info,
		.get = alc883_mux_enum_get,
		.put = alc883_mux_enum_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc883_lenovo_nb0763_mixer[] = {
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("iMic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("iMic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = alc883_mux_enum_info,
		.get = alc883_mux_enum_get,
		.put = alc883_mux_enum_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc883_medion_md2_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = alc883_mux_enum_info,
		.get = alc883_mux_enum_get,
		.put = alc883_mux_enum_put,
	},
	{ } /* end */
};	

static struct snd_kcontrol_new alc883_acer_aspire_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = alc883_mux_enum_info,
		.get = alc883_mux_enum_get,
		.put = alc883_mux_enum_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc883_chmode_mixer[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
	},
	{ } /* end */
};

static struct hda_verb alc883_init_verbs[] = {
	/* ADC1: mute amp left and right */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* ADC2: mute amp left and right */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* Front mixer: unmute input/output amp left and right (volume = 0) */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* Rear mixer */
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* CLFE mixer */
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* Side mixer */
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},

	/* mute analog input loopbacks */
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},

	/* Front Pin: output 0 (0x0c) */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* Rear Pin: output 1 (0x0d) */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x01},
	/* CLFE Pin: output 2 (0x0e) */
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_CONNECT_SEL, 0x02},
	/* Side Pin: output 3 (0x0f) */
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x17, AC_VERB_SET_CONNECT_SEL, 0x03},
	/* Mic (rear) pin: input vref at 80% */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Front Mic pin: input vref at 80% */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Line In pin: input */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Line-2 In: Headphone output (output 0 - 0x0c) */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* CD pin widget for input */
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

	/* FIXME: use matrix-type input source selection */
	/* Mixer elements: 0x18, 19, 1a, 1b, 1c, 1d, 14, 15, 16, 17, 0b */
	/* Input mixer2 */
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(4)},
	/* Input mixer3 */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(4)},
	{ }
};

/* toggle speaker-output according to the hp-jack state */
static void alc883_mitac_hp_automute(struct hda_codec *codec)
{
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x15, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
	snd_hda_codec_amp_stereo(codec, 0x17, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
}

/* auto-toggle front mic */
/*
static void alc883_mitac_mic_automute(struct hda_codec *codec)
{
	unsigned int present;
	unsigned char bits;

	present = snd_hda_codec_read(codec, 0x18, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x0b, HDA_INPUT, 1, HDA_AMP_MUTE, bits);
}
*/

static void alc883_mitac_automute(struct hda_codec *codec)
{
	alc883_mitac_hp_automute(codec);
	/* alc883_mitac_mic_automute(codec); */
}

static void alc883_mitac_unsol_event(struct hda_codec *codec,
					   unsigned int res)
{
	switch (res >> 26) {
	case ALC880_HP_EVENT:
		alc883_mitac_hp_automute(codec);
		break;
	case ALC880_MIC_EVENT:
		/* alc883_mitac_mic_automute(codec); */
		break;
	}
}

static struct hda_verb alc883_mitac_verbs[] = {
	/* HP */
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	/* Subwoofer */
	{0x17, AC_VERB_SET_CONNECT_SEL, 0x02},
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},

	/* enable unsolicited event */
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	/* {0x18, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_MIC_EVENT | AC_USRSP_EN}, */

	{ } /* end */
};

static struct hda_verb alc883_clevo_m720_verbs[] = {
	/* HP */
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	/* Int speaker */
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},

	/* enable unsolicited event */
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_MIC_EVENT | AC_USRSP_EN},

	{ } /* end */
};

static struct hda_verb alc883_2ch_fujitsu_pi2515_verbs[] = {
	/* HP */
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	/* Subwoofer */
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},

	/* enable unsolicited event */
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},

	{ } /* end */
};

static struct hda_verb alc883_tagra_verbs[] = {
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	
	{0x18, AC_VERB_SET_CONNECT_SEL, 0x02}, /* mic/clfe */
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x01}, /* line/surround */
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00}, /* HP */

	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{0x01, AC_VERB_SET_GPIO_MASK, 0x03},
	{0x01, AC_VERB_SET_GPIO_DIRECTION, 0x03},
	{0x01, AC_VERB_SET_GPIO_DATA, 0x03},

	{ } /* end */
};

static struct hda_verb alc883_lenovo_101e_verbs[] = {
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_FRONT_EVENT|AC_USRSP_EN},
        {0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT|AC_USRSP_EN},
	{ } /* end */
};

static struct hda_verb alc883_lenovo_nb0763_verbs[] = {
        {0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
        {0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
        {0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{ } /* end */
};

static struct hda_verb alc888_lenovo_ms7195_verbs[] = {
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_FRONT_EVENT | AC_USRSP_EN},
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT    | AC_USRSP_EN},
	{ } /* end */
};

static struct hda_verb alc883_haier_w66_verbs[] = {
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},

	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{ } /* end */
};

static struct hda_verb alc888_3st_hp_verbs[] = {
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},	/* Front: output 0 (0x0c) */
	{0x16, AC_VERB_SET_CONNECT_SEL, 0x01},	/* Rear : output 1 (0x0d) */
	{0x18, AC_VERB_SET_CONNECT_SEL, 0x02},	/* CLFE : output 2 (0x0e) */
	{ }
};

static struct hda_verb alc888_6st_dell_verbs[] = {
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{ }
};

static struct hda_verb alc888_3st_hp_2ch_init[] = {
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ }
};

static struct hda_verb alc888_3st_hp_6ch_init[] = {
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ }
};

static struct hda_channel_mode alc888_3st_hp_modes[2] = {
	{ 2, alc888_3st_hp_2ch_init },
	{ 6, alc888_3st_hp_6ch_init },
};

/* toggle front-jack and RCA according to the hp-jack state */
static void alc888_lenovo_ms7195_front_automute(struct hda_codec *codec)
{
 	unsigned int present;
 
 	present = snd_hda_codec_read(codec, 0x1b, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
	snd_hda_codec_amp_stereo(codec, 0x15, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
}

/* toggle RCA according to the front-jack state */
static void alc888_lenovo_ms7195_rca_automute(struct hda_codec *codec)
{
 	unsigned int present;
 
 	present = snd_hda_codec_read(codec, 0x14, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	snd_hda_codec_amp_stereo(codec, 0x15, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
}

static void alc883_lenovo_ms7195_unsol_event(struct hda_codec *codec,
					     unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc888_lenovo_ms7195_front_automute(codec);
	if ((res >> 26) == ALC880_FRONT_EVENT)
		alc888_lenovo_ms7195_rca_automute(codec);
}

static struct hda_verb alc883_medion_md2_verbs[] = {
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},

	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{ } /* end */
};

/* toggle speaker-output according to the hp-jack state */
static void alc883_medion_md2_automute(struct hda_codec *codec)
{
 	unsigned int present;
 
 	present = snd_hda_codec_read(codec, 0x14, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	snd_hda_codec_amp_stereo(codec, 0x15, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
}

static void alc883_medion_md2_unsol_event(struct hda_codec *codec,
					  unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc883_medion_md2_automute(codec);
}

/* toggle speaker-output according to the hp-jack state */
static void alc883_tagra_automute(struct hda_codec *codec)
{
 	unsigned int present;
	unsigned char bits;

 	present = snd_hda_codec_read(codec, 0x14, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x1b, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
	snd_hda_codec_write_cache(codec, 1, 0, AC_VERB_SET_GPIO_DATA,
				  present ? 1 : 3);
}

static void alc883_tagra_unsol_event(struct hda_codec *codec, unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc883_tagra_automute(codec);
}

/* toggle speaker-output according to the hp-jack state */
static void alc883_clevo_m720_hp_automute(struct hda_codec *codec)
{
	unsigned int present;
	unsigned char bits;

	present = snd_hda_codec_read(codec, 0x15, 0, AC_VERB_GET_PIN_SENSE, 0)
		& AC_PINSENSE_PRESENCE;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
}

static void alc883_clevo_m720_mic_automute(struct hda_codec *codec)
{
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x18, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	snd_hda_codec_amp_stereo(codec, 0x0b, HDA_INPUT, 1,
				 HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
}

static void alc883_clevo_m720_automute(struct hda_codec *codec)
{
	alc883_clevo_m720_hp_automute(codec);
	alc883_clevo_m720_mic_automute(codec);
}

static void alc883_clevo_m720_unsol_event(struct hda_codec *codec,
					   unsigned int res)
{
	switch (res >> 26) {
	case ALC880_HP_EVENT:
		alc883_clevo_m720_hp_automute(codec);
		break;
	case ALC880_MIC_EVENT:
		alc883_clevo_m720_mic_automute(codec);
		break;
	}
}

/* toggle speaker-output according to the hp-jack state */
static void alc883_2ch_fujitsu_pi2515_automute(struct hda_codec *codec)
{
 	unsigned int present;
	unsigned char bits;

 	present = snd_hda_codec_read(codec, 0x14, 0, AC_VERB_GET_PIN_SENSE, 0)
		& AC_PINSENSE_PRESENCE;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x15, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
}

static void alc883_2ch_fujitsu_pi2515_unsol_event(struct hda_codec *codec,
						  unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc883_2ch_fujitsu_pi2515_automute(codec);
}

static void alc883_haier_w66_automute(struct hda_codec *codec)
{
	unsigned int present;
	unsigned char bits;

	present = snd_hda_codec_read(codec, 0x1b, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	bits = present ? 0x80 : 0;
	snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
				 0x80, bits);
}

static void alc883_haier_w66_unsol_event(struct hda_codec *codec,
					 unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc883_haier_w66_automute(codec);
}

static void alc883_lenovo_101e_ispeaker_automute(struct hda_codec *codec)
{
 	unsigned int present;
	unsigned char bits;

 	present = snd_hda_codec_read(codec, 0x14, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x15, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
}

static void alc883_lenovo_101e_all_automute(struct hda_codec *codec)
{
 	unsigned int present;
	unsigned char bits;

 	present = snd_hda_codec_read(codec, 0x1b, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x15, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
	snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
}

static void alc883_lenovo_101e_unsol_event(struct hda_codec *codec,
					   unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc883_lenovo_101e_all_automute(codec);
	if ((res >> 26) == ALC880_FRONT_EVENT)
		alc883_lenovo_101e_ispeaker_automute(codec);
}

/* toggle speaker-output according to the hp-jack state */
static void alc883_acer_aspire_automute(struct hda_codec *codec)
{
 	unsigned int present;
 
 	present = snd_hda_codec_read(codec, 0x14, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	snd_hda_codec_amp_stereo(codec, 0x15, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
	snd_hda_codec_amp_stereo(codec, 0x16, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
}

static void alc883_acer_aspire_unsol_event(struct hda_codec *codec,
					   unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc883_acer_aspire_automute(codec);
}

static struct hda_verb alc883_acer_eapd_verbs[] = {
	/* HP Pin: output 0 (0x0c) */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* Front Pin: output 0 (0x0c) */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_CONNECT_SEL, 0x00},
        /* eanable EAPD on medion laptop */
	{0x20, AC_VERB_SET_COEF_INDEX, 0x07},
	{0x20, AC_VERB_SET_PROC_COEF, 0x3050},
	/* enable unsolicited event */
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{ }
};

static void alc888_6st_dell_front_automute(struct hda_codec *codec)
{
 	unsigned int present;
 
 	present = snd_hda_codec_read(codec, 0x1b, 0,
				AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
				HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
	snd_hda_codec_amp_stereo(codec, 0x15, HDA_OUTPUT, 0,
				HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
	snd_hda_codec_amp_stereo(codec, 0x16, HDA_OUTPUT, 0,
				HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
	snd_hda_codec_amp_stereo(codec, 0x17, HDA_OUTPUT, 0,
				HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
}

static void alc888_6st_dell_unsol_event(struct hda_codec *codec,
					     unsigned int res)
{
	switch (res >> 26) {
	case ALC880_HP_EVENT:
		printk("hp_event\n");
		alc888_6st_dell_front_automute(codec);
		break;
	}
}

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static struct hda_verb alc883_auto_init_verbs[] = {
	/*
	 * Unmute ADC0-2 and set the default input to mic-in
	 */
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	/* Mute input amps (CD, Line In, Mic 1 & Mic 2) of the analog-loopback
	 * mixer widget
	 * Note: PASD motherboards uses the Line In 2 as the input for
	 * front panel mic (mic 2)
	 */
	/* Amp Indices: Mic1 = 0, Mic2 = 1, Line1 = 2, Line2 = 3, CD = 4 */
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},

	/*
	 * Set up output mixers (0x0c - 0x0f)
	 */
	/* set vol=0 to output mixers */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	/* set up input amps for analog loopback */
	/* Amp Indices: DAC = 0, mixer = 1 */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x26, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x26, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	/* FIXME: use matrix-type input source selection */
	/* Mixer elements: 0x18, 19, 1a, 1b, 1c, 1d, 14, 15, 16, 17, 0b */
	/* Input mixer1 */
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	/* {0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(3)}, */
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(4)},
	/* Input mixer2 */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	/* {0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(3)}, */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(4)},

	{ }
};

/* capture mixer elements */
static struct snd_kcontrol_new alc883_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = alc882_mux_enum_info,
		.get = alc882_mux_enum_get,
		.put = alc882_mux_enum_put,
	},
	{ } /* end */
};

#ifdef CONFIG_SND_HDA_POWER_SAVE
#define alc883_loopbacks	alc880_loopbacks
#endif

/* pcm configuration: identiacal with ALC880 */
#define alc883_pcm_analog_playback	alc880_pcm_analog_playback
#define alc883_pcm_analog_capture	alc880_pcm_analog_capture
#define alc883_pcm_analog_alt_capture	alc880_pcm_analog_alt_capture
#define alc883_pcm_digital_playback	alc880_pcm_digital_playback
#define alc883_pcm_digital_capture	alc880_pcm_digital_capture

/*
 * configuration and preset
 */
static const char *alc883_models[ALC883_MODEL_LAST] = {
	[ALC883_3ST_2ch_DIG]	= "3stack-dig",
	[ALC883_3ST_6ch_DIG]	= "3stack-6ch-dig",
	[ALC883_3ST_6ch]	= "3stack-6ch",
	[ALC883_6ST_DIG]	= "6stack-dig",
	[ALC883_TARGA_DIG]	= "targa-dig",
	[ALC883_TARGA_2ch_DIG]	= "targa-2ch-dig",
	[ALC883_ACER]		= "acer",
	[ALC883_ACER_ASPIRE]	= "acer-aspire",
	[ALC883_MEDION]		= "medion",
	[ALC883_MEDION_MD2]	= "medion-md2",
	[ALC883_LAPTOP_EAPD]	= "laptop-eapd",
	[ALC883_LENOVO_101E_2ch] = "lenovo-101e",
	[ALC883_LENOVO_NB0763]	= "lenovo-nb0763",
	[ALC888_LENOVO_MS7195_DIG] = "lenovo-ms7195-dig",
	[ALC883_HAIER_W66] 	= "haier-w66",
	[ALC888_3ST_HP]		= "3stack-hp",
	[ALC888_6ST_DELL]	= "6stack-dell",
	[ALC883_MITAC]		= "mitac",
	[ALC883_CLEVO_M720]	= "clevo-m720",
	[ALC883_FUJITSU_PI2515] = "fujitsu-pi2515",
	[ALC883_3ST_6ch_INTEL]	= "3stack-6ch-intel",
	[ALC883_AUTO]		= "auto",
};

static struct snd_pci_quirk alc883_cfg_tbl[] = {
	SND_PCI_QUIRK(0x1019, 0x6668, "ECS", ALC883_3ST_6ch_DIG),
	SND_PCI_QUIRK(0x1025, 0x006c, "Acer Aspire 9810", ALC883_ACER_ASPIRE),
	SND_PCI_QUIRK(0x1025, 0x0110, "Acer Aspire", ALC883_ACER_ASPIRE),
	SND_PCI_QUIRK(0x1025, 0x0112, "Acer Aspire 9303", ALC883_ACER_ASPIRE),
	SND_PCI_QUIRK(0x1025, 0x0121, "Acer Aspire 5920G", ALC883_ACER_ASPIRE), 
	SND_PCI_QUIRK(0x1025, 0, "Acer laptop", ALC883_ACER), /* default Acer */
	SND_PCI_QUIRK(0x1028, 0x020d, "Dell Inspiron 530", ALC888_6ST_DELL),
	SND_PCI_QUIRK(0x103c, 0x2a3d, "HP Pavillion", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x103c, 0x2a4f, "HP Samba", ALC888_3ST_HP),
	SND_PCI_QUIRK(0x103c, 0x2a60, "HP Lucknow", ALC888_3ST_HP),
	SND_PCI_QUIRK(0x103c, 0x2a61, "HP Nettle", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x1043, 0x8249, "Asus M2A-VM HDMI", ALC883_3ST_6ch_DIG),
	SND_PCI_QUIRK(0x105b, 0x0ce8, "Foxconn P35AX-S", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x105b, 0x6668, "Foxconn", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x1071, 0x8253, "Mitac 8252d", ALC883_MITAC),
	SND_PCI_QUIRK(0x1071, 0x8258, "Evesham Voyaeger", ALC883_LAPTOP_EAPD),
	SND_PCI_QUIRK(0x108e, 0x534d, NULL, ALC883_3ST_6ch),
	SND_PCI_QUIRK(0x1458, 0xa002, "MSI", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x1462, 0x0349, "MSI", ALC883_TARGA_2ch_DIG),
	SND_PCI_QUIRK(0x1462, 0x040d, "MSI", ALC883_TARGA_2ch_DIG),
	SND_PCI_QUIRK(0x1462, 0x0579, "MSI", ALC883_TARGA_2ch_DIG),
	SND_PCI_QUIRK(0x1462, 0x2fb3, "MSI", ALC883_TARGA_2ch_DIG),
	SND_PCI_QUIRK(0x1462, 0x3729, "MSI S420", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x3783, "NEC S970", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x3b7f, "MSI", ALC883_TARGA_2ch_DIG),
	SND_PCI_QUIRK(0x1462, 0x3ef9, "MSI", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x3fc1, "MSI", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x3fc3, "MSI", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x3fcc, "MSI", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x3fdf, "MSI", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x4314, "MSI", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x4319, "MSI", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x4324, "MSI", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x6668, "MSI", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x1462, 0x7187, "MSI", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x1462, 0x7250, "MSI", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x1462, 0x7267, "MSI", ALC883_3ST_6ch_DIG),
	SND_PCI_QUIRK(0x1462, 0x7280, "MSI", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x1462, 0x7327, "MSI", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x1462, 0xa422, "MSI", ALC883_TARGA_2ch_DIG),
	SND_PCI_QUIRK(0x147b, 0x1083, "Abit IP35-PRO", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x1558, 0x0721, "Clevo laptop M720R", ALC883_CLEVO_M720),
	SND_PCI_QUIRK(0x1558, 0x0722, "Clevo laptop M720SR", ALC883_CLEVO_M720),
	SND_PCI_QUIRK(0x1558, 0, "Clevo laptop", ALC883_LAPTOP_EAPD),
	SND_PCI_QUIRK(0x15d9, 0x8780, "Supermicro PDSBA", ALC883_3ST_6ch),
	SND_PCI_QUIRK(0x161f, 0x2054, "Medion laptop", ALC883_MEDION),
	SND_PCI_QUIRK(0x1734, 0x1108, "Fujitsu AMILO Pi2515", ALC883_FUJITSU_PI2515),
	SND_PCI_QUIRK(0x17aa, 0x101e, "Lenovo 101e", ALC883_LENOVO_101E_2ch),
	SND_PCI_QUIRK(0x17aa, 0x2085, "Lenovo NB0763", ALC883_LENOVO_NB0763),
	SND_PCI_QUIRK(0x17aa, 0x3bfc, "Lenovo NB0763", ALC883_LENOVO_NB0763),
	SND_PCI_QUIRK(0x17aa, 0x3bfd, "Lenovo NB0763", ALC883_LENOVO_NB0763),
	SND_PCI_QUIRK(0x17c0, 0x4071, "MEDION MD2", ALC883_MEDION_MD2),
	SND_PCI_QUIRK(0x17f2, 0x5000, "Albatron KI690-AM2", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x1991, 0x5625, "Haier W66", ALC883_HAIER_W66),
	SND_PCI_QUIRK(0x8086, 0x0001, "DG33BUC", ALC883_3ST_6ch_INTEL),
	SND_PCI_QUIRK(0x8086, 0x0002, "DG33FBC", ALC883_3ST_6ch_INTEL),
	SND_PCI_QUIRK(0x8086, 0xd601, "D102GGC", ALC883_3ST_6ch),
	{}
};

static struct alc_config_preset alc883_presets[] = {
	[ALC883_3ST_2ch_DIG] = {
		.mixers = { alc883_3ST_2ch_mixer },
		.init_verbs = { alc883_init_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.dig_in_nid = ALC883_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_capture_source,
	},
	[ALC883_3ST_6ch_DIG] = {
		.mixers = { alc883_3ST_6ch_mixer, alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.dig_in_nid = ALC883_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_6ch_modes),
		.channel_mode = alc883_3ST_6ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc883_capture_source,
	},
	[ALC883_3ST_6ch] = {
		.mixers = { alc883_3ST_6ch_mixer, alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_6ch_modes),
		.channel_mode = alc883_3ST_6ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc883_capture_source,
	},
	[ALC883_3ST_6ch_INTEL] = {
		.mixers = { alc883_3ST_6ch_intel_mixer, alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.dig_in_nid = ALC883_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_6ch_intel_modes),
		.channel_mode = alc883_3ST_6ch_intel_modes,
		.need_dac_fix = 1,
		.input_mux = &alc883_3stack_6ch_intel,
	},
	[ALC883_6ST_DIG] = {
		.mixers = { alc883_base_mixer, alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.dig_in_nid = ALC883_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_sixstack_modes),
		.channel_mode = alc883_sixstack_modes,
		.input_mux = &alc883_capture_source,
	},
	[ALC883_TARGA_DIG] = {
		.mixers = { alc883_tagra_mixer, alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs, alc883_tagra_verbs},
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_6ch_modes),
		.channel_mode = alc883_3ST_6ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc883_tagra_unsol_event,
		.init_hook = alc883_tagra_automute,
	},
	[ALC883_TARGA_2ch_DIG] = {
		.mixers = { alc883_tagra_2ch_mixer},
		.init_verbs = { alc883_init_verbs, alc883_tagra_verbs},
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc883_tagra_unsol_event,
		.init_hook = alc883_tagra_automute,
	},
	[ALC883_ACER] = {
		.mixers = { alc883_base_mixer },
		/* On TravelMate laptops, GPIO 0 enables the internal speaker
		 * and the headphone jack.  Turn this on and rely on the
		 * standard mute methods whenever the user wants to turn
		 * these outputs off.
		 */
		.init_verbs = { alc883_init_verbs, alc880_gpio1_init_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_capture_source,
	},
	[ALC883_ACER_ASPIRE] = {
		.mixers = { alc883_acer_aspire_mixer },
		.init_verbs = { alc883_init_verbs, alc883_acer_eapd_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc883_acer_aspire_unsol_event,
		.init_hook = alc883_acer_aspire_automute,
	},
	[ALC883_MEDION] = {
		.mixers = { alc883_fivestack_mixer,
			    alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs,
				alc883_medion_eapd_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc883_sixstack_modes),
		.channel_mode = alc883_sixstack_modes,
		.input_mux = &alc883_capture_source,
	},
	[ALC883_MEDION_MD2] = {
		.mixers = { alc883_medion_md2_mixer},
		.init_verbs = { alc883_init_verbs, alc883_medion_md2_verbs},
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc883_medion_md2_unsol_event,
		.init_hook = alc883_medion_md2_automute,
	},	
	[ALC883_LAPTOP_EAPD] = {
		.mixers = { alc883_base_mixer },
		.init_verbs = { alc883_init_verbs, alc882_eapd_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_capture_source,
	},
	[ALC883_CLEVO_M720] = {
		.mixers = { alc883_clevo_m720_mixer },
		.init_verbs = { alc883_init_verbs, alc883_clevo_m720_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc883_clevo_m720_unsol_event,
		.init_hook = alc883_clevo_m720_automute,
	},
	[ALC883_LENOVO_101E_2ch] = {
		.mixers = { alc883_lenovo_101e_2ch_mixer},
		.init_verbs = { alc883_init_verbs, alc883_lenovo_101e_verbs},
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_lenovo_101e_capture_source,
		.unsol_event = alc883_lenovo_101e_unsol_event,
		.init_hook = alc883_lenovo_101e_all_automute,
	},
	[ALC883_LENOVO_NB0763] = {
		.mixers = { alc883_lenovo_nb0763_mixer },
		.init_verbs = { alc883_init_verbs, alc883_lenovo_nb0763_verbs},
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc883_lenovo_nb0763_capture_source,
		.unsol_event = alc883_medion_md2_unsol_event,
		.init_hook = alc883_medion_md2_automute,
	},
	[ALC888_LENOVO_MS7195_DIG] = {
		.mixers = { alc883_3ST_6ch_mixer, alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs, alc888_lenovo_ms7195_verbs},
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_6ch_modes),
		.channel_mode = alc883_3ST_6ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc883_lenovo_ms7195_unsol_event,
		.init_hook = alc888_lenovo_ms7195_front_automute,
	},
	[ALC883_HAIER_W66] = {
		.mixers = { alc883_tagra_2ch_mixer},
		.init_verbs = { alc883_init_verbs, alc883_haier_w66_verbs},
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc883_haier_w66_unsol_event,
		.init_hook = alc883_haier_w66_automute,
	},
	[ALC888_3ST_HP] = {
		.mixers = { alc883_3ST_6ch_mixer, alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs, alc888_3st_hp_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc888_3st_hp_modes),
		.channel_mode = alc888_3st_hp_modes,
		.need_dac_fix = 1,
		.input_mux = &alc883_capture_source,
	},
	[ALC888_6ST_DELL] = {
		.mixers = { alc883_base_mixer, alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs, alc888_6st_dell_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.dig_in_nid = ALC883_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_sixstack_modes),
		.channel_mode = alc883_sixstack_modes,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc888_6st_dell_unsol_event,
		.init_hook = alc888_6st_dell_front_automute,
	},
	[ALC883_MITAC] = {
		.mixers = { alc883_mitac_mixer },
		.init_verbs = { alc883_init_verbs, alc883_mitac_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc883_mitac_unsol_event,
		.init_hook = alc883_mitac_automute,
	},
	[ALC883_FUJITSU_PI2515] = {
		.mixers = { alc883_2ch_fujitsu_pi2515_mixer },
		.init_verbs = { alc883_init_verbs,
				alc883_2ch_fujitsu_pi2515_verbs},
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_fujitsu_pi2515_capture_source,
		.unsol_event = alc883_2ch_fujitsu_pi2515_unsol_event,
		.init_hook = alc883_2ch_fujitsu_pi2515_automute,
	},
};


/*
 * BIOS auto configuration
 */
static void alc883_auto_set_output_and_unmute(struct hda_codec *codec,
					      hda_nid_t nid, int pin_type,
					      int dac_idx)
{
	/* set as output */
	struct alc_spec *spec = codec->spec;
	int idx;

	alc_set_pin_output(codec, nid, pin_type);
	if (spec->multiout.dac_nids[dac_idx] == 0x25)
		idx = 4;
	else
		idx = spec->multiout.dac_nids[dac_idx] - 2;
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_CONNECT_SEL, idx);

}

static void alc883_auto_init_multi_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i;

	alc_subsystem_id(codec, 0x15, 0x1b, 0x14);
	for (i = 0; i <= HDA_SIDE; i++) {
		hda_nid_t nid = spec->autocfg.line_out_pins[i];
		int pin_type = get_pin_type(spec->autocfg.line_out_type);
		if (nid)
			alc883_auto_set_output_and_unmute(codec, nid, pin_type,
							  i);
	}
}

static void alc883_auto_init_hp_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t pin;

	pin = spec->autocfg.hp_pins[0];
	if (pin) /* connect to front */
		/* use dac 0 */
		alc883_auto_set_output_and_unmute(codec, pin, PIN_HP, 0);
	pin = spec->autocfg.speaker_pins[0];
	if (pin)
		alc883_auto_set_output_and_unmute(codec, pin, PIN_OUT, 0);
}

#define alc883_is_input_pin(nid)	alc880_is_input_pin(nid)
#define ALC883_PIN_CD_NID		ALC880_PIN_CD_NID

static void alc883_auto_init_analog_input(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		hda_nid_t nid = spec->autocfg.input_pins[i];
		if (alc883_is_input_pin(nid)) {
			snd_hda_codec_write(codec, nid, 0,
					    AC_VERB_SET_PIN_WIDGET_CONTROL,
					    (i <= AUTO_PIN_FRONT_MIC ?
					     PIN_VREF80 : PIN_IN));
			if (nid != ALC883_PIN_CD_NID)
				snd_hda_codec_write(codec, nid, 0,
						    AC_VERB_SET_AMP_GAIN_MUTE,
						    AMP_OUT_MUTE);
		}
	}
}

#define alc883_auto_init_input_src	alc882_auto_init_input_src

/* almost identical with ALC880 parser... */
static int alc883_parse_auto_config(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int err = alc880_parse_auto_config(codec);

	if (err < 0)
		return err;
	else if (!err)
		return 0; /* no config found */

	err = alc_auto_add_mic_boost(codec);
	if (err < 0)
		return err;

	/* hack - override the init verbs */
	spec->init_verbs[0] = alc883_auto_init_verbs;
	spec->mixers[spec->num_mixers] = alc883_capture_mixer;
	spec->num_mixers++;

	return 1; /* config found */
}

/* additional initialization for auto-configuration model */
static void alc883_auto_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	alc883_auto_init_multi_out(codec);
	alc883_auto_init_hp_out(codec);
	alc883_auto_init_analog_input(codec);
	alc883_auto_init_input_src(codec);
	if (spec->unsol_event)
		alc_sku_automute(codec);
}

static int patch_alc883(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err, board_config;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	alc_fix_pll_init(codec, 0x20, 0x0a, 10);

	board_config = snd_hda_check_board_config(codec, ALC883_MODEL_LAST,
						  alc883_models,
						  alc883_cfg_tbl);
	if (board_config < 0) {
		printk(KERN_INFO "hda_codec: Unknown model for ALC883, "
		       "trying auto-probe from BIOS...\n");
		board_config = ALC883_AUTO;
	}

	if (board_config == ALC883_AUTO) {
		/* automatic parse from the BIOS config */
		err = alc883_parse_auto_config(codec);
		if (err < 0) {
			alc_free(codec);
			return err;
		} else if (!err) {
			printk(KERN_INFO
			       "hda_codec: Cannot set up configuration "
			       "from BIOS.  Using base mode...\n");
			board_config = ALC883_3ST_2ch_DIG;
		}
	}

	if (board_config != ALC883_AUTO)
		setup_preset(spec, &alc883_presets[board_config]);

	switch (codec->vendor_id) {
	case 0x10ec0888:
		spec->stream_name_analog = "ALC888 Analog";
		spec->stream_name_digital = "ALC888 Digital";
		break;
	case 0x10ec0889:
		spec->stream_name_analog = "ALC889 Analog";
		spec->stream_name_digital = "ALC889 Digital";
		break;
	default:
		spec->stream_name_analog = "ALC883 Analog";
		spec->stream_name_digital = "ALC883 Digital";
		break;
	}

	spec->stream_analog_playback = &alc883_pcm_analog_playback;
	spec->stream_analog_capture = &alc883_pcm_analog_capture;
	spec->stream_analog_alt_capture = &alc883_pcm_analog_alt_capture;

	spec->stream_digital_playback = &alc883_pcm_digital_playback;
	spec->stream_digital_capture = &alc883_pcm_digital_capture;

	spec->num_adc_nids = ARRAY_SIZE(alc883_adc_nids);
	spec->adc_nids = alc883_adc_nids;
	spec->capsrc_nids = alc883_capsrc_nids;

	spec->vmaster_nid = 0x0c;

	codec->patch_ops = alc_patch_ops;
	if (board_config == ALC883_AUTO)
		spec->init_hook = alc883_auto_init;

#ifdef CONFIG_SND_HDA_POWER_SAVE
	if (!spec->loopback.amplist)
		spec->loopback.amplist = alc883_loopbacks;
#endif

	return 0;
}

/*
 * ALC262 support
 */

#define ALC262_DIGOUT_NID	ALC880_DIGOUT_NID
#define ALC262_DIGIN_NID	ALC880_DIGIN_NID

#define alc262_dac_nids		alc260_dac_nids
#define alc262_adc_nids		alc882_adc_nids
#define alc262_adc_nids_alt	alc882_adc_nids_alt
#define alc262_capsrc_nids	alc882_capsrc_nids
#define alc262_capsrc_nids_alt	alc882_capsrc_nids_alt

#define alc262_modes		alc260_modes
#define alc262_capture_source	alc882_capture_source

static struct snd_kcontrol_new alc262_base_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost", 0x19, 0, HDA_INPUT),
	/* HDA_CODEC_VOLUME("PC Beep Playback Volume", 0x0b, 0x05, HDA_INPUT),
	   HDA_CODEC_MUTE("PC Beep Playback Switch", 0x0b, 0x05, HDA_INPUT), */
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0D, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("Mono Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Mono Playback Switch", 0x16, 2, 0x0, HDA_OUTPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc262_hippo1_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost", 0x19, 0, HDA_INPUT),
	/* HDA_CODEC_VOLUME("PC Beep Playback Volume", 0x0b, 0x05, HDA_INPUT),
	   HDA_CODEC_MUTE("PC Beep Playback Switch", 0x0b, 0x05, HDA_INPUT), */
	/*HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0D, 0x0, HDA_OUTPUT),*/
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	{ } /* end */
};

/* update HP, line and mono-out pins according to the master switch */
static void alc262_hp_master_update(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int val = spec->master_sw;

	/* HP & line-out */
	snd_hda_codec_write_cache(codec, 0x1b, 0,
				  AC_VERB_SET_PIN_WIDGET_CONTROL,
				  val ? PIN_HP : 0);
	snd_hda_codec_write_cache(codec, 0x15, 0,
				  AC_VERB_SET_PIN_WIDGET_CONTROL,
				  val ? PIN_HP : 0);
	/* mono (speaker) depending on the HP jack sense */
	val = val && !spec->jack_present;
	snd_hda_codec_write_cache(codec, 0x16, 0,
				  AC_VERB_SET_PIN_WIDGET_CONTROL,
				  val ? PIN_OUT : 0);
}

static void alc262_hp_bpc_automute(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	unsigned int presence;
	presence = snd_hda_codec_read(codec, 0x1b, 0,
				      AC_VERB_GET_PIN_SENSE, 0);
	spec->jack_present = !!(presence & AC_PINSENSE_PRESENCE);
	alc262_hp_master_update(codec);
}

static void alc262_hp_bpc_unsol_event(struct hda_codec *codec, unsigned int res)
{
	if ((res >> 26) != ALC880_HP_EVENT)
		return;
	alc262_hp_bpc_automute(codec);
}

static void alc262_hp_wildwest_automute(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	unsigned int presence;
	presence = snd_hda_codec_read(codec, 0x15, 0,
				      AC_VERB_GET_PIN_SENSE, 0);
	spec->jack_present = !!(presence & AC_PINSENSE_PRESENCE);
	alc262_hp_master_update(codec);
}

static void alc262_hp_wildwest_unsol_event(struct hda_codec *codec,
					   unsigned int res)
{
	if ((res >> 26) != ALC880_HP_EVENT)
		return;
	alc262_hp_wildwest_automute(codec);
}

static int alc262_hp_master_sw_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	*ucontrol->value.integer.value = spec->master_sw;
	return 0;
}

static int alc262_hp_master_sw_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	int val = !!*ucontrol->value.integer.value;

	if (val == spec->master_sw)
		return 0;
	spec->master_sw = val;
	alc262_hp_master_update(codec);
	return 1;
}

static struct snd_kcontrol_new alc262_HP_BPC_mixer[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = snd_ctl_boolean_mono_info,
		.get = alc262_hp_master_sw_get,
		.put = alc262_hp_master_sw_put,
	},
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("Speaker Playback Volume", 0x0e, 2, 0x0,
			      HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Speaker Playback Switch", 0x16, 2, 0x0,
			    HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Beep Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Beep Playback Switch", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_VOLUME("AUX IN Playback Volume", 0x0b, 0x06, HDA_INPUT),
	HDA_CODEC_MUTE("AUX IN Playback Switch", 0x0b, 0x06, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc262_HP_BPC_WildWest_mixer[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = snd_ctl_boolean_mono_info,
		.get = alc262_hp_master_sw_get,
		.put = alc262_hp_master_sw_put,
	},
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("Speaker Playback Volume", 0x0e, 2, 0x0,
			      HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Speaker Playback Switch", 0x16, 2, 0x0,
			    HDA_OUTPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost", 0x1a, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Beep Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Beep Playback Switch", 0x0b, 0x05, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc262_HP_BPC_WildWest_option_mixer[] = {
	HDA_CODEC_VOLUME("Rear Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Rear Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Rear Mic Boost", 0x18, 0, HDA_INPUT),
	{ } /* end */
};

/* mute/unmute internal speaker according to the hp jack and mute state */
static void alc262_hp_t5735_automute(struct hda_codec *codec, int force)
{
	struct alc_spec *spec = codec->spec;

	if (force || !spec->sense_updated) {
		unsigned int present;
		present = snd_hda_codec_read(codec, 0x15, 0,
					     AC_VERB_GET_PIN_SENSE, 0);
		spec->jack_present = (present & AC_PINSENSE_PRESENCE) != 0;
		spec->sense_updated = 1;
	}
	snd_hda_codec_amp_stereo(codec, 0x0c, HDA_OUTPUT, 0, HDA_AMP_MUTE,
				 spec->jack_present ? HDA_AMP_MUTE : 0);
}

static void alc262_hp_t5735_unsol_event(struct hda_codec *codec,
					unsigned int res)
{
	if ((res >> 26) != ALC880_HP_EVENT)
		return;
	alc262_hp_t5735_automute(codec, 1);
}

static void alc262_hp_t5735_init_hook(struct hda_codec *codec)
{
	alc262_hp_t5735_automute(codec, 1);
}

static struct snd_kcontrol_new alc262_hp_t5735_mixer[] = {
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	{ } /* end */
};

static struct hda_verb alc262_hp_t5735_verbs[] = {
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},

	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{ }
};

static struct snd_kcontrol_new alc262_hp_rp5700_mixer[] = {
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0e, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x16, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x01, HDA_INPUT),
	{ } /* end */
};

static struct hda_verb alc262_hp_rp5700_verbs[] = {
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x00 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x00 << 8))},
	{}
};

static struct hda_input_mux alc262_hp_rp5700_capture_source = {
	.num_items = 1,
	.items = {
		{ "Line", 0x1 },
	},
};

/* bind hp and internal speaker mute (with plug check) */
static int alc262_sony_master_sw_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	long *valp = ucontrol->value.integer.value;
	int change;

	/* change hp mute */
	change = snd_hda_codec_amp_update(codec, 0x15, 0, HDA_OUTPUT, 0,
					  HDA_AMP_MUTE,
					  valp[0] ? 0 : HDA_AMP_MUTE);
	change |= snd_hda_codec_amp_update(codec, 0x15, 1, HDA_OUTPUT, 0,
					   HDA_AMP_MUTE,
					   valp[1] ? 0 : HDA_AMP_MUTE);
	if (change) {
		/* change speaker according to HP jack state */
		struct alc_spec *spec = codec->spec;
		unsigned int mute;
		if (spec->jack_present)
			mute = HDA_AMP_MUTE;
		else
			mute = snd_hda_codec_amp_read(codec, 0x15, 0,
						      HDA_OUTPUT, 0);
		snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
					 HDA_AMP_MUTE, mute);
	}
	return change;
}

static struct snd_kcontrol_new alc262_sony_mixer[] = {
	HDA_CODEC_VOLUME("Master Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = snd_hda_mixer_amp_switch_info,
		.get = snd_hda_mixer_amp_switch_get,
		.put = alc262_sony_master_sw_put,
		.private_value = HDA_COMPOSE_AMP_VAL(0x15, 3, 0, HDA_OUTPUT),
	},
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("ATAPI Mic Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("ATAPI Mic Playback Switch", 0x0b, 0x01, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc262_benq_t31_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("ATAPI Mic Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("ATAPI Mic Playback Switch", 0x0b, 0x01, HDA_INPUT),
	{ } /* end */
};

#define alc262_capture_mixer		alc882_capture_mixer
#define alc262_capture_alt_mixer	alc882_capture_alt_mixer

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static struct hda_verb alc262_init_verbs[] = {
	/*
	 * Unmute ADC0-2 and set the default input to mic-in
	 */
	{0x07, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	/* Mute input amps (CD, Line In, Mic 1 & Mic 2) of the analog-loopback
	 * mixer widget
	 * Note: PASD motherboards uses the Line In 2 as the input for
	 * front panel mic (mic 2)
	 */
	/* Amp Indices: Mic1 = 0, Mic2 = 1, Line1 = 2, Line2 = 3, CD = 4 */
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},

	/*
	 * Set up output mixers (0x0c - 0x0e)
	 */
	/* set vol=0 to output mixers */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	/* set up input amps for analog loopback */
	/* Amp Indices: DAC = 0, mixer = 1 */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},

	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, 0x0000},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0x0000},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, 0x0000},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, 0x0000},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, 0x0000},
	
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x01},
	
	/* FIXME: use matrix-type input source selection */
	/* Mixer elements: 0x18, 19, 1a, 1b, 1c, 1d, 14, 15, 16, 17, 0b */
	/* Input mixer1: unmute Mic, F-Mic, Line, CD inputs */
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x03 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x02 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x04 << 8))},
	/* Input mixer2 */
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x03 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x02 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x04 << 8))},
	/* Input mixer3 */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x03 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x02 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x04 << 8))},

	{ }
};

static struct hda_verb alc262_hippo_unsol_verbs[] = {
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{}
};

static struct hda_verb alc262_hippo1_unsol_verbs[] = {
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0},
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, 0x0000},

	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{}
};

static struct hda_verb alc262_sony_unsol_verbs[] = {
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},	// Front Mic

	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{}
};

/* mute/unmute internal speaker according to the hp jack and mute state */
static void alc262_hippo_automute(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	unsigned int mute;
	unsigned int present;

	/* need to execute and sync at first */
	snd_hda_codec_read(codec, 0x15, 0, AC_VERB_SET_PIN_SENSE, 0);
	present = snd_hda_codec_read(codec, 0x15, 0,
				     AC_VERB_GET_PIN_SENSE, 0);
	spec->jack_present = (present & 0x80000000) != 0;
	if (spec->jack_present) {
		/* mute internal speaker */
		snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
					 HDA_AMP_MUTE, HDA_AMP_MUTE);
	} else {
		/* unmute internal speaker if necessary */
		mute = snd_hda_codec_amp_read(codec, 0x15, 0, HDA_OUTPUT, 0);
		snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
					 HDA_AMP_MUTE, mute);
	}
}

/* unsolicited event for HP jack sensing */
static void alc262_hippo_unsol_event(struct hda_codec *codec,
				       unsigned int res)
{
	if ((res >> 26) != ALC880_HP_EVENT)
		return;
	alc262_hippo_automute(codec);
}

static void alc262_hippo1_automute(struct hda_codec *codec)
{
	unsigned int mute;
	unsigned int present;

	snd_hda_codec_read(codec, 0x1b, 0, AC_VERB_SET_PIN_SENSE, 0);
	present = snd_hda_codec_read(codec, 0x1b, 0,
				     AC_VERB_GET_PIN_SENSE, 0);
	present = (present & 0x80000000) != 0;
	if (present) {
		/* mute internal speaker */
		snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
					 HDA_AMP_MUTE, HDA_AMP_MUTE);
	} else {
		/* unmute internal speaker if necessary */
		mute = snd_hda_codec_amp_read(codec, 0x1b, 0, HDA_OUTPUT, 0);
		snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
					 HDA_AMP_MUTE, mute);
	}
}

/* unsolicited event for HP jack sensing */
static void alc262_hippo1_unsol_event(struct hda_codec *codec,
				       unsigned int res)
{
	if ((res >> 26) != ALC880_HP_EVENT)
		return;
	alc262_hippo1_automute(codec);
}

/*
 * fujitsu model
 *  0x14 = headphone/spdif-out, 0x15 = internal speaker,
 *  0x1b = port replicator headphone out
 */

#define ALC_HP_EVENT	0x37

static struct hda_verb alc262_fujitsu_unsol_verbs[] = {
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC_HP_EVENT},
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC_HP_EVENT},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{}
};

static struct hda_verb alc262_lenovo_3000_unsol_verbs[] = {
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC_HP_EVENT},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{}
};

static struct hda_input_mux alc262_fujitsu_capture_source = {
	.num_items = 3,
	.items = {
		{ "Mic", 0x0 },
		{ "Int Mic", 0x1 },
		{ "CD", 0x4 },
	},
};

static struct hda_input_mux alc262_HP_capture_source = {
	.num_items = 5,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x1 },
		{ "Line", 0x2 },
		{ "CD", 0x4 },
		{ "AUX IN", 0x6 },
	},
};

static struct hda_input_mux alc262_HP_D7000_capture_source = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x2 },
		{ "Line", 0x1 },
		{ "CD", 0x4 },
	},
};

/* mute/unmute internal speaker according to the hp jacks and mute state */
static void alc262_fujitsu_automute(struct hda_codec *codec, int force)
{
	struct alc_spec *spec = codec->spec;
	unsigned int mute;

	if (force || !spec->sense_updated) {
		unsigned int present;
		/* need to execute and sync at first */
		snd_hda_codec_read(codec, 0x14, 0, AC_VERB_SET_PIN_SENSE, 0);
		/* check laptop HP jack */
		present = snd_hda_codec_read(codec, 0x14, 0,
					     AC_VERB_GET_PIN_SENSE, 0);
		/* need to execute and sync at first */
		snd_hda_codec_read(codec, 0x1b, 0, AC_VERB_SET_PIN_SENSE, 0);
		/* check docking HP jack */
		present |= snd_hda_codec_read(codec, 0x1b, 0,
					      AC_VERB_GET_PIN_SENSE, 0);
		if (present & AC_PINSENSE_PRESENCE)
			spec->jack_present = 1;
		else
			spec->jack_present = 0;
		spec->sense_updated = 1;
	}
	/* unmute internal speaker only if both HPs are unplugged and
	 * master switch is on
	 */
	if (spec->jack_present)
		mute = HDA_AMP_MUTE;
	else
		mute = snd_hda_codec_amp_read(codec, 0x14, 0, HDA_OUTPUT, 0);
	snd_hda_codec_amp_stereo(codec, 0x15, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, mute);
}

/* unsolicited event for HP jack sensing */
static void alc262_fujitsu_unsol_event(struct hda_codec *codec,
				       unsigned int res)
{
	if ((res >> 26) != ALC_HP_EVENT)
		return;
	alc262_fujitsu_automute(codec, 1);
}

static void alc262_fujitsu_init_hook(struct hda_codec *codec)
{
	alc262_fujitsu_automute(codec, 1);
}

/* bind volumes of both NID 0x0c and 0x0d */
static struct hda_bind_ctls alc262_fujitsu_bind_master_vol = {
	.ops = &snd_hda_bind_vol,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x0c, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x0d, 3, 0, HDA_OUTPUT),
		0
	},
};

/* mute/unmute internal speaker according to the hp jack and mute state */
static void alc262_lenovo_3000_automute(struct hda_codec *codec, int force)
{
	struct alc_spec *spec = codec->spec;
	unsigned int mute;

	if (force || !spec->sense_updated) {
		unsigned int present_int_hp;
		/* need to execute and sync at first */
		snd_hda_codec_read(codec, 0x1b, 0, AC_VERB_SET_PIN_SENSE, 0);
		present_int_hp = snd_hda_codec_read(codec, 0x1b, 0,
					AC_VERB_GET_PIN_SENSE, 0);
		spec->jack_present = (present_int_hp & 0x80000000) != 0;
		spec->sense_updated = 1;
	}
	if (spec->jack_present) {
		/* mute internal speaker */
		snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
					 HDA_AMP_MUTE, HDA_AMP_MUTE);
		snd_hda_codec_amp_stereo(codec, 0x16, HDA_OUTPUT, 0,
					 HDA_AMP_MUTE, HDA_AMP_MUTE);
	} else {
		/* unmute internal speaker if necessary */
		mute = snd_hda_codec_amp_read(codec, 0x1b, 0, HDA_OUTPUT, 0);
		snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
					 HDA_AMP_MUTE, mute);
		snd_hda_codec_amp_stereo(codec, 0x16, HDA_OUTPUT, 0,
					 HDA_AMP_MUTE, mute);
	}
}

/* unsolicited event for HP jack sensing */
static void alc262_lenovo_3000_unsol_event(struct hda_codec *codec,
				       unsigned int res)
{
	if ((res >> 26) != ALC_HP_EVENT)
		return;
	alc262_lenovo_3000_automute(codec, 1);
}

/* bind hp and internal speaker mute (with plug check) */
static int alc262_fujitsu_master_sw_put(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	long *valp = ucontrol->value.integer.value;
	int change;

	change = snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
						 HDA_AMP_MUTE,
						 valp ? 0 : HDA_AMP_MUTE);
	change |= snd_hda_codec_amp_stereo(codec, 0x1b, HDA_OUTPUT, 0,
						 HDA_AMP_MUTE,
						 valp ? 0 : HDA_AMP_MUTE);

	if (change)
		alc262_fujitsu_automute(codec, 0);
	return change;
}

static struct snd_kcontrol_new alc262_fujitsu_mixer[] = {
	HDA_BIND_VOL("Master Playback Volume", &alc262_fujitsu_bind_master_vol),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = snd_hda_mixer_amp_switch_info,
		.get = snd_hda_mixer_amp_switch_get,
		.put = alc262_fujitsu_master_sw_put,
		.private_value = HDA_COMPOSE_AMP_VAL(0x14, 3, 0, HDA_OUTPUT),
	},
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Speaker Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Switch", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Int Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Int Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Int Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

/* bind hp and internal speaker mute (with plug check) */
static int alc262_lenovo_3000_master_sw_put(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	long *valp = ucontrol->value.integer.value;
	int change;

	change = snd_hda_codec_amp_stereo(codec, 0x1b, HDA_OUTPUT, 0,
						 HDA_AMP_MUTE,
						 valp ? 0 : HDA_AMP_MUTE);

	if (change)
		alc262_lenovo_3000_automute(codec, 0);
	return change;
}

static struct snd_kcontrol_new alc262_lenovo_3000_mixer[] = {
	HDA_BIND_VOL("Master Playback Volume", &alc262_fujitsu_bind_master_vol),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = snd_hda_mixer_amp_switch_info,
		.get = snd_hda_mixer_amp_switch_get,
		.put = alc262_lenovo_3000_master_sw_put,
		.private_value = HDA_COMPOSE_AMP_VAL(0x1b, 3, 0, HDA_OUTPUT),
	},
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Int Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Int Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Int Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

/* additional init verbs for Benq laptops */
static struct hda_verb alc262_EAPD_verbs[] = {
	{0x20, AC_VERB_SET_COEF_INDEX, 0x07},
	{0x20, AC_VERB_SET_PROC_COEF,  0x3070},
	{}
};

static struct hda_verb alc262_benq_t31_EAPD_verbs[] = {
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},

	{0x20, AC_VERB_SET_COEF_INDEX, 0x07},
	{0x20, AC_VERB_SET_PROC_COEF,  0x3050},
	{}
};

/* Samsung Q1 Ultra Vista model setup */
static struct snd_kcontrol_new alc262_ultra_mixer[] = {
	HDA_CODEC_VOLUME("Master Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Master Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Headphone Mic Boost", 0x15, 0, HDA_INPUT),
	{ } /* end */
};

static struct hda_verb alc262_ultra_verbs[] = {
	/* output mixer */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	/* speaker */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* HP */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	/* internal mic */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* ADC, choose mic */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(5)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(6)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(7)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(8)},
	{}
};

/* mute/unmute internal speaker according to the hp jack and mute state */
static void alc262_ultra_automute(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	unsigned int mute;

	mute = 0;
	/* auto-mute only when HP is used as HP */
	if (!spec->cur_mux[0]) {
		unsigned int present;
		/* need to execute and sync at first */
		snd_hda_codec_read(codec, 0x15, 0, AC_VERB_SET_PIN_SENSE, 0);
		present = snd_hda_codec_read(codec, 0x15, 0,
					     AC_VERB_GET_PIN_SENSE, 0);
		spec->jack_present = (present & AC_PINSENSE_PRESENCE) != 0;
		if (spec->jack_present)
			mute = HDA_AMP_MUTE;
	}
	/* mute/unmute internal speaker */
	snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, mute);
	/* mute/unmute HP */
	snd_hda_codec_amp_stereo(codec, 0x15, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, mute ? 0 : HDA_AMP_MUTE);
}

/* unsolicited event for HP jack sensing */
static void alc262_ultra_unsol_event(struct hda_codec *codec,
				       unsigned int res)
{
	if ((res >> 26) != ALC880_HP_EVENT)
		return;
	alc262_ultra_automute(codec);
}

static struct hda_input_mux alc262_ultra_capture_source = {
	.num_items = 2,
	.items = {
		{ "Mic", 0x1 },
		{ "Headphone", 0x7 },
	},
};

static int alc262_ultra_mux_enum_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	int ret;

	ret = alc882_mux_enum_put(kcontrol, ucontrol);
	if (!ret)
		return 0;
	/* reprogram the HP pin as mic or HP according to the input source */
	snd_hda_codec_write_cache(codec, 0x15, 0,
				  AC_VERB_SET_PIN_WIDGET_CONTROL,
				  spec->cur_mux[0] ? PIN_VREF80 : PIN_HP);
	alc262_ultra_automute(codec); /* mute/unmute HP */
	return ret;
}

static struct snd_kcontrol_new alc262_ultra_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x07, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x07, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.info = alc882_mux_enum_info,
		.get = alc882_mux_enum_get,
		.put = alc262_ultra_mux_enum_put,
	},
	{ } /* end */
};

/* add playback controls from the parsed DAC table */
static int alc262_auto_create_multi_out_ctls(struct alc_spec *spec,
					     const struct auto_pin_cfg *cfg)
{
	hda_nid_t nid;
	int err;

	spec->multiout.num_dacs = 1;	/* only use one dac */
	spec->multiout.dac_nids = spec->private_dac_nids;
	spec->multiout.dac_nids[0] = 2;

	nid = cfg->line_out_pins[0];
	if (nid) {
		err = add_control(spec, ALC_CTL_WIDGET_VOL,
				  "Front Playback Volume",
				  HDA_COMPOSE_AMP_VAL(0x0c, 3, 0, HDA_OUTPUT));
		if (err < 0)
			return err;
		err = add_control(spec, ALC_CTL_WIDGET_MUTE,
				  "Front Playback Switch",
				  HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT));
		if (err < 0)
			return err;
	}

	nid = cfg->speaker_pins[0];
	if (nid) {
		if (nid == 0x16) {
			err = add_control(spec, ALC_CTL_WIDGET_VOL,
					  "Speaker Playback Volume",
					  HDA_COMPOSE_AMP_VAL(0x0e, 2, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
			err = add_control(spec, ALC_CTL_WIDGET_MUTE,
					  "Speaker Playback Switch",
					  HDA_COMPOSE_AMP_VAL(nid, 2, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
		} else {
			err = add_control(spec, ALC_CTL_WIDGET_MUTE,
					  "Speaker Playback Switch",
					  HDA_COMPOSE_AMP_VAL(nid, 3, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
		}
	}
	nid = cfg->hp_pins[0];
	if (nid) {
		/* spec->multiout.hp_nid = 2; */
		if (nid == 0x16) {
			err = add_control(spec, ALC_CTL_WIDGET_VOL,
					  "Headphone Playback Volume",
					  HDA_COMPOSE_AMP_VAL(0x0e, 2, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
			err = add_control(spec, ALC_CTL_WIDGET_MUTE,
					  "Headphone Playback Switch",
					  HDA_COMPOSE_AMP_VAL(nid, 2, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
		} else {
			err = add_control(spec, ALC_CTL_WIDGET_MUTE,
					  "Headphone Playback Switch",
					  HDA_COMPOSE_AMP_VAL(nid, 3, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
		}
	}
	return 0;
}

/* identical with ALC880 */
#define alc262_auto_create_analog_input_ctls \
	alc880_auto_create_analog_input_ctls

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static struct hda_verb alc262_volume_init_verbs[] = {
	/*
	 * Unmute ADC0-2 and set the default input to mic-in
	 */
	{0x07, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	/* Mute input amps (CD, Line In, Mic 1 & Mic 2) of the analog-loopback
	 * mixer widget
	 * Note: PASD motherboards uses the Line In 2 as the input for
	 * front panel mic (mic 2)
	 */
	/* Amp Indices: Mic1 = 0, Mic2 = 1, Line1 = 2, Line2 = 3, CD = 4 */
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},

	/*
	 * Set up output mixers (0x0c - 0x0f)
	 */
	/* set vol=0 to output mixers */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	
	/* set up input amps for analog loopback */
	/* Amp Indices: DAC = 0, mixer = 1 */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	/* FIXME: use matrix-type input source selection */
	/* Mixer elements: 0x18, 19, 1a, 1b, 1c, 1d, 14, 15, 16, 17, 0b */
	/* Input mixer1: unmute Mic, F-Mic, Line, CD inputs */
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x03 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x02 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x04 << 8))},
	/* Input mixer2 */
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x03 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x02 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x04 << 8))},
	/* Input mixer3 */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x03 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x02 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x04 << 8))},

	{ }
};

static struct hda_verb alc262_HP_BPC_init_verbs[] = {
	/*
	 * Unmute ADC0-2 and set the default input to mic-in
	 */
	{0x07, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	/* Mute input amps (CD, Line In, Mic 1 & Mic 2) of the analog-loopback
	 * mixer widget
	 * Note: PASD motherboards uses the Line In 2 as the input for
	 * front panel mic (mic 2)
	 */
	/* Amp Indices: Mic1 = 0, Mic2 = 1, Line1 = 2, Line2 = 3, CD = 4 */
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(5)},
        {0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(6)},
	
	/*
	 * Set up output mixers (0x0c - 0x0e)
	 */
	/* set vol=0 to output mixers */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},

	/* set up input amps for analog loopback */
	/* Amp Indices: DAC = 0, mixer = 1 */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },

	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },

	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},
        {0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},

	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, 0x7023 },
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, 0x7000 },
        {0x19, AC_VERB_SET_AMP_GAIN_MUTE, 0x7000 },
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, 0x7023 },
	{0x1c, AC_VERB_SET_AMP_GAIN_MUTE, 0x7000 },
	{0x1d, AC_VERB_SET_AMP_GAIN_MUTE, 0x7000 },


	/* FIXME: use matrix-type input source selection */
	/* Mixer elements: 0x18, 19, 1a, 1b, 1c, 1d, 14, 15, 16, 17, 0b */
	/* Input mixer1: unmute Mic, F-Mic, Line, CD inputs */
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x03 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x02 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x04 << 8))},
	/* Input mixer2 */
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x03 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x02 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x04 << 8))},
	/* Input mixer3 */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x03 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x02 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x04 << 8))},

	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},

	{ }
};

static struct hda_verb alc262_HP_BPC_WildWest_init_verbs[] = {
	/*
	 * Unmute ADC0-2 and set the default input to mic-in
	 */
	{0x07, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	/* Mute input amps (CD, Line In, Mic 1 & Mic 2) of the analog-loopback
	 * mixer widget
	 * Note: PASD motherboards uses the Line In 2 as the input for front
	 * panel mic (mic 2)
	 */
	/* Amp Indices: Mic1 = 0, Mic2 = 1, Line1 = 2, Line2 = 3, CD = 4 */
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(5)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(6)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(7)},
	/*
	 * Set up output mixers (0x0c - 0x0e)
	 */
	/* set vol=0 to output mixers */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},

	/* set up input amps for analog loopback */
	/* Amp Indices: DAC = 0, mixer = 1 */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},


	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP },	/* HP */
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },	/* Mono */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },	/* rear MIC */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },	/* Line in */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },	/* Front MIC */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },	/* Line out */
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },	/* CD in */

	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },

	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x01},

	/* {0x14, AC_VERB_SET_AMP_GAIN_MUTE, 0x7023 }, */
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, 0x7000 },
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, 0x7000 },
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, 0x7023 },
	{0x1c, AC_VERB_SET_AMP_GAIN_MUTE, 0x7000 },
	{0x1d, AC_VERB_SET_AMP_GAIN_MUTE, 0x7000 },

	/* FIXME: use matrix-type input source selection */
	/* Mixer elements: 0x18, 19, 1a, 1b, 1c, 1d, 14, 15, 16, 17, 0b */
	/* Input mixer1: unmute Mic, F-Mic, Line, CD inputs */
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))}, /*rear MIC*/
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))}, /*Line in*/
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x02 << 8))}, /*F MIC*/
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x03 << 8))}, /*Front*/
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x04 << 8))}, /*CD*/
        /* {0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x06 << 8))},  */
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x07 << 8))}, /*HP*/
	/* Input mixer2 */
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x02 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x03 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x04 << 8))},
        /* {0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x06 << 8))}, */
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x07 << 8))},
	/* Input mixer3 */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x02 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x03 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x04 << 8))},
        /* {0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x06 << 8))}, */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x07 << 8))},

	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},

	{ }
};

#ifdef CONFIG_SND_HDA_POWER_SAVE
#define alc262_loopbacks	alc880_loopbacks
#endif

/* pcm configuration: identiacal with ALC880 */
#define alc262_pcm_analog_playback	alc880_pcm_analog_playback
#define alc262_pcm_analog_capture	alc880_pcm_analog_capture
#define alc262_pcm_digital_playback	alc880_pcm_digital_playback
#define alc262_pcm_digital_capture	alc880_pcm_digital_capture

/*
 * BIOS auto configuration
 */
static int alc262_parse_auto_config(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int err;
	static hda_nid_t alc262_ignore[] = { 0x1d, 0 };

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg,
					   alc262_ignore);
	if (err < 0)
		return err;
	if (!spec->autocfg.line_outs)
		return 0; /* can't find valid BIOS pin config */
	err = alc262_auto_create_multi_out_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = alc262_auto_create_analog_input_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;

	if (spec->autocfg.dig_out_pin)
		spec->multiout.dig_out_nid = ALC262_DIGOUT_NID;
	if (spec->autocfg.dig_in_pin)
		spec->dig_in_nid = ALC262_DIGIN_NID;

	if (spec->kctl_alloc)
		spec->mixers[spec->num_mixers++] = spec->kctl_alloc;

	spec->init_verbs[spec->num_init_verbs++] = alc262_volume_init_verbs;
	spec->num_mux_defs = 1;
	spec->input_mux = &spec->private_imux;

	err = alc_auto_add_mic_boost(codec);
	if (err < 0)
		return err;

	return 1;
}

#define alc262_auto_init_multi_out	alc882_auto_init_multi_out
#define alc262_auto_init_hp_out		alc882_auto_init_hp_out
#define alc262_auto_init_analog_input	alc882_auto_init_analog_input
#define alc262_auto_init_input_src	alc882_auto_init_input_src


/* init callback for auto-configuration model -- overriding the default init */
static void alc262_auto_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	alc262_auto_init_multi_out(codec);
	alc262_auto_init_hp_out(codec);
	alc262_auto_init_analog_input(codec);
	alc262_auto_init_input_src(codec);
	if (spec->unsol_event)
		alc_sku_automute(codec);
}

/*
 * configuration and preset
 */
static const char *alc262_models[ALC262_MODEL_LAST] = {
	[ALC262_BASIC]		= "basic",
	[ALC262_HIPPO]		= "hippo",
	[ALC262_HIPPO_1]	= "hippo_1",
	[ALC262_FUJITSU]	= "fujitsu",
	[ALC262_HP_BPC]		= "hp-bpc",
	[ALC262_HP_BPC_D7000_WL]= "hp-bpc-d7000",
	[ALC262_HP_TC_T5735]	= "hp-tc-t5735",
	[ALC262_HP_RP5700]	= "hp-rp5700",
	[ALC262_BENQ_ED8]	= "benq",
	[ALC262_BENQ_T31]	= "benq-t31",
	[ALC262_SONY_ASSAMD]	= "sony-assamd",
	[ALC262_ULTRA]		= "ultra",
	[ALC262_LENOVO_3000]	= "lenovo-3000",
	[ALC262_AUTO]		= "auto",
};

static struct snd_pci_quirk alc262_cfg_tbl[] = {
	SND_PCI_QUIRK(0x1002, 0x437b, "Hippo", ALC262_HIPPO),
	SND_PCI_QUIRK(0x103c, 0x12fe, "HP xw9400", ALC262_HP_BPC),
	SND_PCI_QUIRK(0x103c, 0x12ff, "HP xw4550", ALC262_HP_BPC),
	SND_PCI_QUIRK(0x103c, 0x1306, "HP xw8600", ALC262_HP_BPC),
	SND_PCI_QUIRK(0x103c, 0x1307, "HP xw6600", ALC262_HP_BPC),
	SND_PCI_QUIRK(0x103c, 0x1308, "HP xw4600", ALC262_HP_BPC),
	SND_PCI_QUIRK(0x103c, 0x1309, "HP xw4*00", ALC262_HP_BPC),
	SND_PCI_QUIRK(0x103c, 0x130a, "HP xw6*00", ALC262_HP_BPC),
	SND_PCI_QUIRK(0x103c, 0x130b, "HP xw8*00", ALC262_HP_BPC),
	SND_PCI_QUIRK(0x103c, 0x2800, "HP D7000", ALC262_HP_BPC_D7000_WL),
	SND_PCI_QUIRK(0x103c, 0x2801, "HP D7000", ALC262_HP_BPC_D7000_WF),
	SND_PCI_QUIRK(0x103c, 0x2802, "HP D7000", ALC262_HP_BPC_D7000_WL),
	SND_PCI_QUIRK(0x103c, 0x2803, "HP D7000", ALC262_HP_BPC_D7000_WF),
	SND_PCI_QUIRK(0x103c, 0x2804, "HP D7000", ALC262_HP_BPC_D7000_WL),
	SND_PCI_QUIRK(0x103c, 0x2805, "HP D7000", ALC262_HP_BPC_D7000_WF),
	SND_PCI_QUIRK(0x103c, 0x2806, "HP D7000", ALC262_HP_BPC_D7000_WL),
	SND_PCI_QUIRK(0x103c, 0x2807, "HP D7000", ALC262_HP_BPC_D7000_WF),
	SND_PCI_QUIRK(0x103c, 0x280c, "HP xw4400", ALC262_HP_BPC),
	SND_PCI_QUIRK(0x103c, 0x3014, "HP xw6400", ALC262_HP_BPC),
	SND_PCI_QUIRK(0x103c, 0x3015, "HP xw8400", ALC262_HP_BPC),
	SND_PCI_QUIRK(0x103c, 0x302f, "HP Thin Client T5735",
		      ALC262_HP_TC_T5735),
	SND_PCI_QUIRK(0x103c, 0x2817, "HP RP5700", ALC262_HP_RP5700),
	SND_PCI_QUIRK(0x104d, 0x1f00, "Sony ASSAMD", ALC262_SONY_ASSAMD),
	SND_PCI_QUIRK(0x104d, 0x8203, "Sony UX-90", ALC262_HIPPO),
	SND_PCI_QUIRK(0x104d, 0x820f, "Sony ASSAMD", ALC262_SONY_ASSAMD),
	SND_PCI_QUIRK(0x104d, 0x900e, "Sony ASSAMD", ALC262_SONY_ASSAMD),
	SND_PCI_QUIRK(0x104d, 0x9015, "Sony 0x9015", ALC262_SONY_ASSAMD),
	SND_PCI_QUIRK(0x1179, 0x0001, "Toshiba dynabook SS RX1",
		      ALC262_SONY_ASSAMD),
	SND_PCI_QUIRK(0x10cf, 0x1397, "Fujitsu", ALC262_FUJITSU),
	SND_PCI_QUIRK(0x10cf, 0x142d, "Fujitsu Lifebook E8410", ALC262_FUJITSU),
	SND_PCI_QUIRK(0x144d, 0xc032, "Samsung Q1 Ultra", ALC262_ULTRA),
	SND_PCI_QUIRK(0x144d, 0xc039, "Samsung Q1U EL", ALC262_ULTRA),
	SND_PCI_QUIRK(0x17aa, 0x384e, "Lenovo 3000 y410", ALC262_LENOVO_3000),
	SND_PCI_QUIRK(0x17ff, 0x0560, "Benq ED8", ALC262_BENQ_ED8),
	SND_PCI_QUIRK(0x17ff, 0x058d, "Benq T31-16", ALC262_BENQ_T31),
	SND_PCI_QUIRK(0x17ff, 0x058f, "Benq Hippo", ALC262_HIPPO_1),
	{}
};

static struct alc_config_preset alc262_presets[] = {
	[ALC262_BASIC] = {
		.mixers = { alc262_base_mixer },
		.init_verbs = { alc262_init_verbs },
		.num_dacs = ARRAY_SIZE(alc262_dac_nids),
		.dac_nids = alc262_dac_nids,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc262_modes),
		.channel_mode = alc262_modes,
		.input_mux = &alc262_capture_source,
	},
	[ALC262_HIPPO] = {
		.mixers = { alc262_base_mixer },
		.init_verbs = { alc262_init_verbs, alc262_hippo_unsol_verbs},
		.num_dacs = ARRAY_SIZE(alc262_dac_nids),
		.dac_nids = alc262_dac_nids,
		.hp_nid = 0x03,
		.dig_out_nid = ALC262_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc262_modes),
		.channel_mode = alc262_modes,
		.input_mux = &alc262_capture_source,
		.unsol_event = alc262_hippo_unsol_event,
		.init_hook = alc262_hippo_automute,
	},
	[ALC262_HIPPO_1] = {
		.mixers = { alc262_hippo1_mixer },
		.init_verbs = { alc262_init_verbs, alc262_hippo1_unsol_verbs},
		.num_dacs = ARRAY_SIZE(alc262_dac_nids),
		.dac_nids = alc262_dac_nids,
		.hp_nid = 0x02,
		.dig_out_nid = ALC262_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc262_modes),
		.channel_mode = alc262_modes,
		.input_mux = &alc262_capture_source,
		.unsol_event = alc262_hippo1_unsol_event,
		.init_hook = alc262_hippo1_automute,
	},
	[ALC262_FUJITSU] = {
		.mixers = { alc262_fujitsu_mixer },
		.init_verbs = { alc262_init_verbs, alc262_EAPD_verbs,
				alc262_fujitsu_unsol_verbs },
		.num_dacs = ARRAY_SIZE(alc262_dac_nids),
		.dac_nids = alc262_dac_nids,
		.hp_nid = 0x03,
		.dig_out_nid = ALC262_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc262_modes),
		.channel_mode = alc262_modes,
		.input_mux = &alc262_fujitsu_capture_source,
		.unsol_event = alc262_fujitsu_unsol_event,
		.init_hook = alc262_fujitsu_init_hook,
	},
	[ALC262_HP_BPC] = {
		.mixers = { alc262_HP_BPC_mixer },
		.init_verbs = { alc262_HP_BPC_init_verbs },
		.num_dacs = ARRAY_SIZE(alc262_dac_nids),
		.dac_nids = alc262_dac_nids,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc262_modes),
		.channel_mode = alc262_modes,
		.input_mux = &alc262_HP_capture_source,
		.unsol_event = alc262_hp_bpc_unsol_event,
		.init_hook = alc262_hp_bpc_automute,
	},
	[ALC262_HP_BPC_D7000_WF] = {
		.mixers = { alc262_HP_BPC_WildWest_mixer },
		.init_verbs = { alc262_HP_BPC_WildWest_init_verbs },
		.num_dacs = ARRAY_SIZE(alc262_dac_nids),
		.dac_nids = alc262_dac_nids,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc262_modes),
		.channel_mode = alc262_modes,
		.input_mux = &alc262_HP_D7000_capture_source,
		.unsol_event = alc262_hp_wildwest_unsol_event,
		.init_hook = alc262_hp_wildwest_automute,
	},
	[ALC262_HP_BPC_D7000_WL] = {
		.mixers = { alc262_HP_BPC_WildWest_mixer,
			    alc262_HP_BPC_WildWest_option_mixer },
		.init_verbs = { alc262_HP_BPC_WildWest_init_verbs },
		.num_dacs = ARRAY_SIZE(alc262_dac_nids),
		.dac_nids = alc262_dac_nids,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc262_modes),
		.channel_mode = alc262_modes,
		.input_mux = &alc262_HP_D7000_capture_source,
		.unsol_event = alc262_hp_wildwest_unsol_event,
		.init_hook = alc262_hp_wildwest_automute,
	},
	[ALC262_HP_TC_T5735] = {
		.mixers = { alc262_hp_t5735_mixer },
		.init_verbs = { alc262_init_verbs, alc262_hp_t5735_verbs },
		.num_dacs = ARRAY_SIZE(alc262_dac_nids),
		.dac_nids = alc262_dac_nids,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc262_modes),
		.channel_mode = alc262_modes,
		.input_mux = &alc262_capture_source,
		.unsol_event = alc262_hp_t5735_unsol_event,
		.init_hook = alc262_hp_t5735_init_hook,
	},
	[ALC262_HP_RP5700] = {
		.mixers = { alc262_hp_rp5700_mixer },
		.init_verbs = { alc262_init_verbs, alc262_hp_rp5700_verbs },
		.num_dacs = ARRAY_SIZE(alc262_dac_nids),
		.dac_nids = alc262_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc262_modes),
		.channel_mode = alc262_modes,
		.input_mux = &alc262_hp_rp5700_capture_source,
        },
	[ALC262_BENQ_ED8] = {
		.mixers = { alc262_base_mixer },
		.init_verbs = { alc262_init_verbs, alc262_EAPD_verbs },
		.num_dacs = ARRAY_SIZE(alc262_dac_nids),
		.dac_nids = alc262_dac_nids,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc262_modes),
		.channel_mode = alc262_modes,
		.input_mux = &alc262_capture_source,
	},
	[ALC262_SONY_ASSAMD] = {
		.mixers = { alc262_sony_mixer },
		.init_verbs = { alc262_init_verbs, alc262_sony_unsol_verbs},
		.num_dacs = ARRAY_SIZE(alc262_dac_nids),
		.dac_nids = alc262_dac_nids,
		.hp_nid = 0x02,
		.num_channel_mode = ARRAY_SIZE(alc262_modes),
		.channel_mode = alc262_modes,
		.input_mux = &alc262_capture_source,
		.unsol_event = alc262_hippo_unsol_event,
		.init_hook = alc262_hippo_automute,
	},
	[ALC262_BENQ_T31] = {
		.mixers = { alc262_benq_t31_mixer },
		.init_verbs = { alc262_init_verbs, alc262_benq_t31_EAPD_verbs, alc262_hippo_unsol_verbs },
		.num_dacs = ARRAY_SIZE(alc262_dac_nids),
		.dac_nids = alc262_dac_nids,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc262_modes),
		.channel_mode = alc262_modes,
		.input_mux = &alc262_capture_source,
		.unsol_event = alc262_hippo_unsol_event,
		.init_hook = alc262_hippo_automute,
	},	
	[ALC262_ULTRA] = {
		.mixers = { alc262_ultra_mixer, alc262_ultra_capture_mixer },
		.init_verbs = { alc262_ultra_verbs },
		.num_dacs = ARRAY_SIZE(alc262_dac_nids),
		.dac_nids = alc262_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc262_modes),
		.channel_mode = alc262_modes,
		.input_mux = &alc262_ultra_capture_source,
		.adc_nids = alc262_adc_nids, /* ADC0 */
		.capsrc_nids = alc262_capsrc_nids,
		.num_adc_nids = 1, /* single ADC */
		.unsol_event = alc262_ultra_unsol_event,
		.init_hook = alc262_ultra_automute,
	},
	[ALC262_LENOVO_3000] = {
		.mixers = { alc262_lenovo_3000_mixer },
		.init_verbs = { alc262_init_verbs, alc262_EAPD_verbs,
				alc262_lenovo_3000_unsol_verbs },
		.num_dacs = ARRAY_SIZE(alc262_dac_nids),
		.dac_nids = alc262_dac_nids,
		.hp_nid = 0x03,
		.dig_out_nid = ALC262_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc262_modes),
		.channel_mode = alc262_modes,
		.input_mux = &alc262_fujitsu_capture_source,
		.unsol_event = alc262_lenovo_3000_unsol_event,
	},
};

static int patch_alc262(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int board_config;
	int err;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;
/* almost identical with ALC880 parser... */
static int alc882_parse_auto_config(struct hda_codec *codec)
{
	static const hda_nid_t alc882_ignore[] = { 0x1d, 0 };
	static const hda_nid_t alc882_ssids[] = { 0x15, 0x1b, 0x14, 0 };
	return alc_parse_auto_config(codec, alc882_ignore, alc882_ssids);
}

/*
 */
static int patch_alc882(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err;

	err = alc_alloc_spec(codec, 0x0b);
	if (err < 0)
		return err;

	spec = codec->spec;

	switch (codec->core.vendor_id) {
	case 0x10ec0882:
	case 0x10ec0885:
	case 0x10ec0900:
		break;
	default:
		/* ALC883 and variants */
		alc_fix_pll_init(codec, 0x20, 0x0a, 10);
		break;
	}

	snd_hda_pick_fixup(codec, alc882_fixup_models, alc882_fixup_tbl,
		       alc882_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	alc_auto_parse_customize_define(codec);

	if (has_cdefine_beep(codec))
		spec->gen.beep_nid = 0x01;

	/* automatic parse from the BIOS config */
	err = alc882_parse_auto_config(codec);
	if (err < 0)
		goto error;

	if (!spec->gen.no_analog && spec->gen.beep_nid)
		set_beep_amp(spec, 0x0b, 0x05, HDA_INPUT);

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;

 error:
	alc_free(codec);
	return err;
}


/*
 * ALC262 support
 */
static int alc262_parse_auto_config(struct hda_codec *codec)
{
	static const hda_nid_t alc262_ignore[] = { 0x1d, 0 };
	static const hda_nid_t alc262_ssids[] = { 0x15, 0x1b, 0x14, 0 };
	return alc_parse_auto_config(codec, alc262_ignore, alc262_ssids);
}

/*
 * Pin config fixes
 */
enum {
	ALC262_FIXUP_FSC_H270,
	ALC262_FIXUP_FSC_S7110,
	ALC262_FIXUP_HP_Z200,
	ALC262_FIXUP_TYAN,
	ALC262_FIXUP_LENOVO_3000,
	ALC262_FIXUP_BENQ,
	ALC262_FIXUP_BENQ_T31,
	ALC262_FIXUP_INV_DMIC,
	ALC262_FIXUP_INTEL_BAYLEYBAY,
};

static const struct hda_fixup alc262_fixups[] = {
	[ALC262_FIXUP_FSC_H270] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x15, 0x0221142f }, /* front HP */
			{ 0x1b, 0x0121141f }, /* rear HP */
			{ }
		}
	},
	[ALC262_FIXUP_FSC_S7110] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x15, 0x90170110 }, /* speaker */
			{ }
		},
		.chained = true,
		.chain_id = ALC262_FIXUP_BENQ,
	},
	[ALC262_FIXUP_HP_Z200] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x16, 0x99130120 }, /* internal speaker */
			{ }
		}
	},
	[ALC262_FIXUP_TYAN] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x1993e1f0 }, /* int AUX */
			{ }
		}
	},
	[ALC262_FIXUP_LENOVO_3000] = {
		.type = HDA_FIXUP_PINCTLS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, PIN_VREF50 },
			{}
		},
		.chained = true,
		.chain_id = ALC262_FIXUP_BENQ,
	},
	[ALC262_FIXUP_BENQ] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x3070 },
			{}
		}
	},
	[ALC262_FIXUP_BENQ_T31] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x3050 },
			{}
		}
	},
	[ALC262_FIXUP_INV_DMIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_inv_dmic,
	},
	[ALC262_FIXUP_INTEL_BAYLEYBAY] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_no_depop_delay,
	},
};

static const struct snd_pci_quirk alc262_fixup_tbl[] = {
	SND_PCI_QUIRK(0x103c, 0x170b, "HP Z200", ALC262_FIXUP_HP_Z200),
	SND_PCI_QUIRK(0x10cf, 0x1397, "Fujitsu Lifebook S7110", ALC262_FIXUP_FSC_S7110),
	SND_PCI_QUIRK(0x10cf, 0x142d, "Fujitsu Lifebook E8410", ALC262_FIXUP_BENQ),
	SND_PCI_QUIRK(0x10f1, 0x2915, "Tyan Thunder n6650W", ALC262_FIXUP_TYAN),
	SND_PCI_QUIRK(0x1734, 0x1141, "FSC ESPRIMO U9210", ALC262_FIXUP_FSC_H270),
	SND_PCI_QUIRK(0x1734, 0x1147, "FSC Celsius H270", ALC262_FIXUP_FSC_H270),
	SND_PCI_QUIRK(0x17aa, 0x384e, "Lenovo 3000", ALC262_FIXUP_LENOVO_3000),
	SND_PCI_QUIRK(0x17ff, 0x0560, "Benq ED8", ALC262_FIXUP_BENQ),
	SND_PCI_QUIRK(0x17ff, 0x058d, "Benq T31-16", ALC262_FIXUP_BENQ_T31),
	SND_PCI_QUIRK(0x8086, 0x7270, "BayleyBay", ALC262_FIXUP_INTEL_BAYLEYBAY),
	{}
};

static const struct hda_model_fixup alc262_fixup_models[] = {
	{.id = ALC262_FIXUP_INV_DMIC, .name = "inv-dmic"},
	{}
};

/*
 */
static int patch_alc262(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err;

	err = alc_alloc_spec(codec, 0x0b);
	if (err < 0)
		return err;

	spec = codec->spec;
	spec->gen.shared_mic_vref_pin = 0x18;

	spec->shutup = alc_eapd_shutup;

#if 0
	/* pshou 07/11/05  set a zero PCM sample to DAC when FIFO is
	 * under-run
	 */
	{
	int tmp;
	snd_hda_codec_write(codec, 0x1a, 0, AC_VERB_SET_COEF_INDEX, 7);
	tmp = snd_hda_codec_read(codec, 0x20, 0, AC_VERB_GET_PROC_COEF, 0);
	snd_hda_codec_write(codec, 0x1a, 0, AC_VERB_SET_COEF_INDEX, 7);
	snd_hda_codec_write(codec, 0x1a, 0, AC_VERB_SET_PROC_COEF, tmp | 0x80);
	}
#endif

	alc_fix_pll_init(codec, 0x20, 0x0a, 10);

	board_config = snd_hda_check_board_config(codec, ALC262_MODEL_LAST,
						  alc262_models,
						  alc262_cfg_tbl);

	if (board_config < 0) {
		printk(KERN_INFO "hda_codec: Unknown model for ALC262, "
		       "trying auto-probe from BIOS...\n");
		board_config = ALC262_AUTO;
	}

	if (board_config == ALC262_AUTO) {
		/* automatic parse from the BIOS config */
		err = alc262_parse_auto_config(codec);
		if (err < 0) {
			alc_free(codec);
			return err;
		} else if (!err) {
			printk(KERN_INFO
			       "hda_codec: Cannot set up configuration "
			       "from BIOS.  Using base mode...\n");
			board_config = ALC262_BASIC;
		}
	}

	if (board_config != ALC262_AUTO)
		setup_preset(spec, &alc262_presets[board_config]);

	spec->stream_name_analog = "ALC262 Analog";
	spec->stream_analog_playback = &alc262_pcm_analog_playback;
	spec->stream_analog_capture = &alc262_pcm_analog_capture;
		
	spec->stream_name_digital = "ALC262 Digital";
	spec->stream_digital_playback = &alc262_pcm_digital_playback;
	spec->stream_digital_capture = &alc262_pcm_digital_capture;

	if (!spec->adc_nids && spec->input_mux) {
		/* check whether NID 0x07 is valid */
		unsigned int wcap = get_wcaps(codec, 0x07);

		/* get type */
		wcap = (wcap & AC_WCAP_TYPE) >> AC_WCAP_TYPE_SHIFT;
		if (wcap != AC_WID_AUD_IN) {
			spec->adc_nids = alc262_adc_nids_alt;
			spec->num_adc_nids = ARRAY_SIZE(alc262_adc_nids_alt);
			spec->capsrc_nids = alc262_capsrc_nids_alt;
			spec->mixers[spec->num_mixers] =
				alc262_capture_alt_mixer;
			spec->num_mixers++;
		} else {
			spec->adc_nids = alc262_adc_nids;
			spec->num_adc_nids = ARRAY_SIZE(alc262_adc_nids);
			spec->capsrc_nids = alc262_capsrc_nids;
			spec->mixers[spec->num_mixers] = alc262_capture_mixer;
			spec->num_mixers++;
		}
	}

	spec->vmaster_nid = 0x0c;

	codec->patch_ops = alc_patch_ops;
	if (board_config == ALC262_AUTO)
		spec->init_hook = alc262_auto_init;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	if (!spec->loopback.amplist)
		spec->loopback.amplist = alc262_loopbacks;
#endif
		
	return 0;
}

/*
 *  ALC268 channel source setting (2 channel)
 */
#define ALC268_DIGOUT_NID	ALC880_DIGOUT_NID
#define alc268_modes		alc260_modes
	
static hda_nid_t alc268_dac_nids[2] = {
	/* front, hp */
	0x02, 0x03
};

static hda_nid_t alc268_adc_nids[2] = {
	/* ADC0-1 */
	0x08, 0x07
};

static hda_nid_t alc268_adc_nids_alt[1] = {
	/* ADC0 */
	0x08
};

static hda_nid_t alc268_capsrc_nids[2] = { 0x23, 0x24 };

static struct snd_kcontrol_new alc268_base_mixer[] = {
	/* output mixer control */
	HDA_CODEC_VOLUME("Front Playback Volume", 0x2, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x3, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Line In Boost", 0x1a, 0, HDA_INPUT),
	{ }
};

/* bind Beep switches of both NID 0x0f and 0x10 */
static struct hda_bind_ctls alc268_bind_beep_sw = {
	alc_update_coefex_idx(codec, 0x1a, 7, 0, 0x80);
#endif
	alc_fix_pll_init(codec, 0x20, 0x0a, 10);

	snd_hda_pick_fixup(codec, alc262_fixup_models, alc262_fixup_tbl,
		       alc262_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	alc_auto_parse_customize_define(codec);

	if (has_cdefine_beep(codec))
		spec->gen.beep_nid = 0x01;

	/* automatic parse from the BIOS config */
	err = alc262_parse_auto_config(codec);
	if (err < 0)
		goto error;

	if (!spec->gen.no_analog && spec->gen.beep_nid)
		set_beep_amp(spec, 0x0b, 0x05, HDA_INPUT);

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;

 error:
	alc_free(codec);
	return err;
}

/*
 *  ALC268
 */
/* bind Beep switches of both NID 0x0f and 0x10 */
static const struct hda_bind_ctls alc268_bind_beep_sw = {
	.ops = &snd_hda_bind_sw,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x0f, 3, 1, HDA_INPUT),
		HDA_COMPOSE_AMP_VAL(0x10, 3, 1, HDA_INPUT),
		0
	},
};

static struct snd_kcontrol_new alc268_beep_mixer[] = {
static const struct snd_kcontrol_new alc268_beep_mixer[] = {
	HDA_CODEC_VOLUME("Beep Playback Volume", 0x1d, 0x0, HDA_INPUT),
	HDA_BIND_SW("Beep Playback Switch", &alc268_bind_beep_sw),
	{ }
};

static struct hda_verb alc268_eapd_verbs[] = {
	{0x14, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{0x15, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{ }
};

/* Toshiba specific */
#define alc268_toshiba_automute	alc262_hippo_automute

static struct hda_verb alc268_toshiba_verbs[] = {
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{ } /* end */
};

/* Acer specific */
/* bind volumes of both NID 0x02 and 0x03 */
static struct hda_bind_ctls alc268_acer_bind_master_vol = {
	.ops = &snd_hda_bind_vol,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x02, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x03, 3, 0, HDA_OUTPUT),
		0
	},
};

/* mute/unmute internal speaker according to the hp jack and mute state */
static void alc268_acer_automute(struct hda_codec *codec, int force)
{
	struct alc_spec *spec = codec->spec;
	unsigned int mute;

	if (force || !spec->sense_updated) {
		unsigned int present;
		present = snd_hda_codec_read(codec, 0x14, 0,
				    	 AC_VERB_GET_PIN_SENSE, 0);
		spec->jack_present = (present & 0x80000000) != 0;
		spec->sense_updated = 1;
	}
	if (spec->jack_present)
		mute = HDA_AMP_MUTE; /* mute internal speaker */
	else /* unmute internal speaker if necessary */
		mute = snd_hda_codec_amp_read(codec, 0x14, 0, HDA_OUTPUT, 0);
	snd_hda_codec_amp_stereo(codec, 0x15, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, mute);
}


/* bind hp and internal speaker mute (with plug check) */
static int alc268_acer_master_sw_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	long *valp = ucontrol->value.integer.value;
	int change;

	change = snd_hda_codec_amp_update(codec, 0x14, 0, HDA_OUTPUT, 0,
					  HDA_AMP_MUTE,
					  valp[0] ? 0 : HDA_AMP_MUTE);
	change |= snd_hda_codec_amp_update(codec, 0x14, 1, HDA_OUTPUT, 0,
					   HDA_AMP_MUTE,
					   valp[1] ? 0 : HDA_AMP_MUTE);
	if (change)
		alc268_acer_automute(codec, 0);
	return change;
}

static struct snd_kcontrol_new alc268_acer_mixer[] = {
	/* output mixer control */
	HDA_BIND_VOL("Master Playback Volume", &alc268_acer_bind_master_vol),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = snd_hda_mixer_amp_switch_info,
		.get = snd_hda_mixer_amp_switch_get,
		.put = alc268_acer_master_sw_put,
		.private_value = HDA_COMPOSE_AMP_VAL(0x14, 3, 0, HDA_OUTPUT),
	},
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Line In Boost", 0x1a, 0, HDA_INPUT),
	{ }
};

static struct hda_verb alc268_acer_verbs[] = {
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN}, /* internal dmic? */
	{0x13, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},

	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{ }
};

/* unsolicited event for HP jack sensing */
static void alc268_toshiba_unsol_event(struct hda_codec *codec,
				       unsigned int res)
{
	if ((res >> 26) != ALC880_HP_EVENT)
		return;
	alc268_toshiba_automute(codec);
}

static void alc268_acer_unsol_event(struct hda_codec *codec,
				       unsigned int res)
{
	if ((res >> 26) != ALC880_HP_EVENT)
		return;
	alc268_acer_automute(codec, 1);
}

static void alc268_acer_init_hook(struct hda_codec *codec)
{
	alc268_acer_automute(codec, 1);
}

static struct snd_kcontrol_new alc268_dell_mixer[] = {
	/* output mixer control */
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Boost", 0x19, 0, HDA_INPUT),
	{ }
};

static struct hda_verb alc268_dell_verbs[] = {
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{ }
};

/* mute/unmute internal speaker according to the hp jack and mute state */
static void alc268_dell_automute(struct hda_codec *codec)
{
	unsigned int present;
	unsigned int mute;

	present = snd_hda_codec_read(codec, 0x15, 0, AC_VERB_GET_PIN_SENSE, 0);
	if (present & 0x80000000)
		mute = HDA_AMP_MUTE;
	else
		mute = snd_hda_codec_amp_read(codec, 0x15, 0, HDA_OUTPUT, 0);
	snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, mute);
}

static void alc268_dell_unsol_event(struct hda_codec *codec,
				    unsigned int res)
{
	if ((res >> 26) != ALC880_HP_EVENT)
		return;
	alc268_dell_automute(codec);
}

#define alc268_dell_init_hook	alc268_dell_automute

static struct snd_kcontrol_new alc267_quanta_il1_mixer[] = {
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x2, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x3, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Capture Volume", 0x23, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Mic Capture Switch", 0x23, 2, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Ext Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Int Mic Boost", 0x19, 0, HDA_INPUT),
	{ }
};

static struct hda_verb alc267_quanta_il1_verbs[] = {
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_MIC_EVENT | AC_USRSP_EN},
	{ }
};

static void alc267_quanta_il1_hp_automute(struct hda_codec *codec)
{
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x15, 0, AC_VERB_GET_PIN_SENSE, 0)
		& AC_PINSENSE_PRESENCE;
	snd_hda_codec_write(codec, 0x14, 0, AC_VERB_SET_PIN_WIDGET_CONTROL,
			    present ? 0 : PIN_OUT);
}

static void alc267_quanta_il1_mic_automute(struct hda_codec *codec)
{
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x18, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	snd_hda_codec_write(codec, 0x23, 0,
			    AC_VERB_SET_CONNECT_SEL,
			    present ? 0x00 : 0x01);
}

static void alc267_quanta_il1_automute(struct hda_codec *codec)
{
	alc267_quanta_il1_hp_automute(codec);
	alc267_quanta_il1_mic_automute(codec);
}

static void alc267_quanta_il1_unsol_event(struct hda_codec *codec,
					   unsigned int res)
{
	switch (res >> 26) {
	case ALC880_HP_EVENT:
		alc267_quanta_il1_hp_automute(codec);
		break;
	case ALC880_MIC_EVENT:
		alc267_quanta_il1_mic_automute(codec);
		break;
	}
}

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static struct hda_verb alc268_base_init_verbs[] = {
	/* Unmute DAC0-1 and set vol = 0 */
	{0x02, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x02, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x02, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	/*
	 * Set up output mixers (0x0c - 0x0e)
	 */
	/* set vol=0 to output mixers */
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
        {0x0e, AC_VERB_SET_CONNECT_SEL, 0x00},

	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},
	{0x1d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},

	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},

	/* set PCBEEP vol = 0, mute connections */
	{0x1d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},

	/* Unmute Selector 23h,24h and set the default input to mic-in */
	
	{0x23, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x24, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	{ }
};

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static struct hda_verb alc268_volume_init_verbs[] = {
	/* set output DAC */
	{0x02, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x02, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},
	{0x1d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},

	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},

	/* set PCBEEP vol = 0, mute connections */
	{0x1d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},

	{ }
};

#define alc268_mux_enum_info alc_mux_enum_info
#define alc268_mux_enum_get alc_mux_enum_get
#define alc268_mux_enum_put alc_mux_enum_put

static struct snd_kcontrol_new alc268_capture_alt_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x23, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x23, 0x0, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 1,
		.info = alc268_mux_enum_info,
		.get = alc268_mux_enum_get,
		.put = alc268_mux_enum_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc268_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x23, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x23, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x24, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x24, 0x0, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = alc268_mux_enum_info,
		.get = alc268_mux_enum_get,
		.put = alc268_mux_enum_put,
	},
	{ } /* end */
};

static struct hda_input_mux alc268_capture_source = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x1 },
		{ "Line", 0x2 },
		{ "CD", 0x3 },
	},
};

static struct hda_input_mux alc268_acer_capture_source = {
	.num_items = 3,
	.items = {
		{ "Mic", 0x0 },
		{ "Internal Mic", 0x6 },
		{ "Line", 0x2 },
	},
};

#ifdef CONFIG_SND_DEBUG
static struct snd_kcontrol_new alc268_test_mixer[] = {
	/* Volume widgets */
	HDA_CODEC_VOLUME("LOUT1 Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("LOUT2 Playback Volume", 0x03, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Mono sum Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE("LINE-OUT sum Playback Switch", 0x0f, 2, HDA_INPUT),
	HDA_BIND_MUTE("HP-OUT sum Playback Switch", 0x10, 2, HDA_INPUT),
	HDA_BIND_MUTE("LINE-OUT Playback Switch", 0x14, 2, HDA_OUTPUT),
	HDA_BIND_MUTE("HP-OUT Playback Switch", 0x15, 2, HDA_OUTPUT),
	HDA_BIND_MUTE("Mono Playback Switch", 0x16, 2, HDA_OUTPUT),
	HDA_CODEC_VOLUME("MIC1 Capture Volume", 0x18, 0x0, HDA_INPUT),
	HDA_BIND_MUTE("MIC1 Capture Switch", 0x18, 2, HDA_OUTPUT),
	HDA_CODEC_VOLUME("MIC2 Capture Volume", 0x19, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("LINE1 Capture Volume", 0x1a, 0x0, HDA_INPUT),
	HDA_BIND_MUTE("LINE1 Capture Switch", 0x1a, 2, HDA_OUTPUT),
	/* The below appears problematic on some hardwares */
	/*HDA_CODEC_VOLUME("PCBEEP Playback Volume", 0x1d, 0x0, HDA_INPUT),*/
	HDA_CODEC_VOLUME("PCM-IN1 Capture Volume", 0x23, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("PCM-IN1 Capture Switch", 0x23, 2, HDA_OUTPUT),
	HDA_CODEC_VOLUME("PCM-IN2 Capture Volume", 0x24, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("PCM-IN2 Capture Switch", 0x24, 2, HDA_OUTPUT),

	/* Modes for retasking pin widgets */
	ALC_PIN_MODE("LINE-OUT pin mode", 0x14, ALC_PIN_DIR_INOUT),
	ALC_PIN_MODE("HP-OUT pin mode", 0x15, ALC_PIN_DIR_INOUT),
	ALC_PIN_MODE("MIC1 pin mode", 0x18, ALC_PIN_DIR_INOUT),
	ALC_PIN_MODE("LINE1 pin mode", 0x1a, ALC_PIN_DIR_INOUT),

	/* Controls for GPIO pins, assuming they are configured as outputs */
	ALC_GPIO_DATA_SWITCH("GPIO pin 0", 0x01, 0x01),
	ALC_GPIO_DATA_SWITCH("GPIO pin 1", 0x01, 0x02),
	ALC_GPIO_DATA_SWITCH("GPIO pin 2", 0x01, 0x04),
	ALC_GPIO_DATA_SWITCH("GPIO pin 3", 0x01, 0x08),

	/* Switches to allow the digital SPDIF output pin to be enabled.
	 * The ALC268 does not have an SPDIF input.
	 */
	ALC_SPDIF_CTRL_SWITCH("SPDIF Playback Switch", 0x06, 0x01),

	/* A switch allowing EAPD to be enabled.  Some laptops seem to use
	 * this output to turn on an external amplifier.
	 */
	ALC_EAPD_CTRL_SWITCH("LINE-OUT EAPD Enable Switch", 0x0f, 0x02),
	ALC_EAPD_CTRL_SWITCH("HP-OUT EAPD Enable Switch", 0x10, 0x02),

	{ } /* end */
};
#endif

/* create input playback/capture controls for the given pin */
static int alc268_new_analog_output(struct alc_spec *spec, hda_nid_t nid,
				    const char *ctlname, int idx)
{
	char name[32];
	int err;

	sprintf(name, "%s Playback Volume", ctlname);
	if (nid == 0x14) {
		err = add_control(spec, ALC_CTL_WIDGET_VOL, name,
				  HDA_COMPOSE_AMP_VAL(0x02, 3, idx,
						      HDA_OUTPUT));
		if (err < 0)
			return err;
	} else if (nid == 0x15) {
		err = add_control(spec, ALC_CTL_WIDGET_VOL, name,
				  HDA_COMPOSE_AMP_VAL(0x03, 3, idx,
						      HDA_OUTPUT));
		if (err < 0)
			return err;
	} else
		return -1;
	sprintf(name, "%s Playback Switch", ctlname);
	err = add_control(spec, ALC_CTL_WIDGET_MUTE, name,
			  HDA_COMPOSE_AMP_VAL(nid, 3, idx, HDA_OUTPUT));
	if (err < 0)
		return err;
	return 0;
}

/* add playback controls from the parsed DAC table */
static int alc268_auto_create_multi_out_ctls(struct alc_spec *spec,
					     const struct auto_pin_cfg *cfg)
{
	hda_nid_t nid;
	int err;

	spec->multiout.num_dacs = 2;	/* only use one dac */
	spec->multiout.dac_nids = spec->private_dac_nids;
	spec->multiout.dac_nids[0] = 2;
	spec->multiout.dac_nids[1] = 3;

	nid = cfg->line_out_pins[0];
	if (nid)
		alc268_new_analog_output(spec, nid, "Front", 0);	

	nid = cfg->speaker_pins[0];
	if (nid == 0x1d) {
		err = add_control(spec, ALC_CTL_WIDGET_VOL,
				  "Speaker Playback Volume",
				  HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_INPUT));
		if (err < 0)
			return err;
	}
	nid = cfg->hp_pins[0];
	if (nid)
		alc268_new_analog_output(spec, nid, "Headphone", 0);

	nid = cfg->line_out_pins[1] | cfg->line_out_pins[2];
	if (nid == 0x16) {
		err = add_control(spec, ALC_CTL_WIDGET_MUTE,
				  "Mono Playback Switch",
				  HDA_COMPOSE_AMP_VAL(nid, 2, 0, HDA_INPUT));
		if (err < 0)
			return err;
	}
	return 0;	
}

/* create playback/capture controls for input pins */
static int alc268_auto_create_analog_input_ctls(struct alc_spec *spec,
						const struct auto_pin_cfg *cfg)
{
	struct hda_input_mux *imux = &spec->private_imux;
	int i, idx1;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		switch(cfg->input_pins[i]) {
		case 0x18:
			idx1 = 0;	/* Mic 1 */
			break;
		case 0x19:
			idx1 = 1;	/* Mic 2 */
			break;
		case 0x1a:
			idx1 = 2;	/* Line In */
			break;
		case 0x1c:	
			idx1 = 3;	/* CD */
			break;
		case 0x12:
		case 0x13:
			idx1 = 6;	/* digital mics */
			break;
		default:
			continue;
		}
		imux->items[imux->num_items].label = auto_pin_cfg_labels[i];
		imux->items[imux->num_items].index = idx1;
		imux->num_items++;	
	}
	return 0;
}

static void alc268_auto_init_mono_speaker_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t speaker_nid = spec->autocfg.speaker_pins[0];
	hda_nid_t hp_nid = spec->autocfg.hp_pins[0];
	hda_nid_t line_nid = spec->autocfg.line_out_pins[0];
	unsigned int	dac_vol1, dac_vol2;

	if (speaker_nid) {
		snd_hda_codec_write(codec, speaker_nid, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT);
		snd_hda_codec_write(codec, 0x0f, 0,
				    AC_VERB_SET_AMP_GAIN_MUTE,
				    AMP_IN_UNMUTE(1));
		snd_hda_codec_write(codec, 0x10, 0,
				    AC_VERB_SET_AMP_GAIN_MUTE,
				    AMP_IN_UNMUTE(1));
	} else {
		snd_hda_codec_write(codec, 0x0f, 0,
				    AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1));
		snd_hda_codec_write(codec, 0x10, 0,
				    AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1));
	}

	dac_vol1 = dac_vol2 = 0xb000 | 0x40;	/* set max volume  */
	if (line_nid == 0x14)	
		dac_vol2 = AMP_OUT_ZERO;
	else if (line_nid == 0x15)
		dac_vol1 = AMP_OUT_ZERO;
	if (hp_nid == 0x14)	
		dac_vol2 = AMP_OUT_ZERO;
	else if (hp_nid == 0x15)
		dac_vol1 = AMP_OUT_ZERO;
	if (line_nid != 0x16 || hp_nid != 0x16 ||
	    spec->autocfg.line_out_pins[1] != 0x16 ||
	    spec->autocfg.line_out_pins[2] != 0x16)
		dac_vol1 = dac_vol2 = AMP_OUT_ZERO;

	snd_hda_codec_write(codec, 0x02, 0,
			    AC_VERB_SET_AMP_GAIN_MUTE, dac_vol1);
	snd_hda_codec_write(codec, 0x03, 0,
			    AC_VERB_SET_AMP_GAIN_MUTE, dac_vol2);
}

/* pcm configuration: identiacal with ALC880 */
#define alc268_pcm_analog_playback	alc880_pcm_analog_playback
#define alc268_pcm_analog_capture	alc880_pcm_analog_capture
#define alc268_pcm_analog_alt_capture	alc880_pcm_analog_alt_capture
#define alc268_pcm_digital_playback	alc880_pcm_digital_playback
/* set PCBEEP vol = 0, mute connections */
static const struct hda_verb alc268_beep_init_verbs[] = {
	{0x1d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{ }
};

enum {
	ALC268_FIXUP_INV_DMIC,
	ALC268_FIXUP_HP_EAPD,
	ALC268_FIXUP_SPDIF,
};

static const struct hda_fixup alc268_fixups[] = {
	[ALC268_FIXUP_INV_DMIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_inv_dmic,
	},
	[ALC268_FIXUP_HP_EAPD] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{0x15, AC_VERB_SET_EAPD_BTLENABLE, 0},
			{}
		}
	},
	[ALC268_FIXUP_SPDIF] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1e, 0x014b1180 }, /* enable SPDIF out */
			{}
		}
	},
};

static const struct hda_model_fixup alc268_fixup_models[] = {
	{.id = ALC268_FIXUP_INV_DMIC, .name = "inv-dmic"},
	{.id = ALC268_FIXUP_HP_EAPD, .name = "hp-eapd"},
	{}
};

static const struct snd_pci_quirk alc268_fixup_tbl[] = {
	SND_PCI_QUIRK(0x1025, 0x0139, "Acer TravelMate 6293", ALC268_FIXUP_SPDIF),
	SND_PCI_QUIRK(0x1025, 0x015b, "Acer AOA 150 (ZG5)", ALC268_FIXUP_INV_DMIC),
	/* below is codec SSID since multiple Toshiba laptops have the
	 * same PCI SSID 1179:ff00
	 */
	SND_PCI_QUIRK(0x1179, 0xff06, "Toshiba P200", ALC268_FIXUP_HP_EAPD),
	{}
};

/*
 * BIOS auto configuration
 */
static int alc268_parse_auto_config(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int err;
	static hda_nid_t alc268_ignore[] = { 0 };

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg,
					   alc268_ignore);
	if (err < 0)
		return err;
	if (!spec->autocfg.line_outs)
		return 0; /* can't find valid BIOS pin config */

	err = alc268_auto_create_multi_out_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = alc268_auto_create_analog_input_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;

	spec->multiout.max_channels = 2;

	/* digital only support output */
	if (spec->autocfg.dig_out_pin)
		spec->multiout.dig_out_nid = ALC268_DIGOUT_NID;

	if (spec->kctl_alloc)
		spec->mixers[spec->num_mixers++] = spec->kctl_alloc;

	if (spec->autocfg.speaker_pins[0] != 0x1d)
		spec->mixers[spec->num_mixers++] = alc268_beep_mixer;

	spec->init_verbs[spec->num_init_verbs++] = alc268_volume_init_verbs;
	spec->num_mux_defs = 1;
	spec->input_mux = &spec->private_imux;

	err = alc_auto_add_mic_boost(codec);
	if (err < 0)
		return err;

	return 1;
}

#define alc268_auto_init_multi_out	alc882_auto_init_multi_out
#define alc268_auto_init_hp_out		alc882_auto_init_hp_out
#define alc268_auto_init_analog_input	alc882_auto_init_analog_input

/* init callback for auto-configuration model -- overriding the default init */
static void alc268_auto_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	alc268_auto_init_multi_out(codec);
	alc268_auto_init_hp_out(codec);
	alc268_auto_init_mono_speaker_out(codec);
	alc268_auto_init_analog_input(codec);
	if (spec->unsol_event)
		alc_sku_automute(codec);
}

/*
 * configuration and preset
 */
static const char *alc268_models[ALC268_MODEL_LAST] = {
	[ALC267_QUANTA_IL1]	= "quanta-il1",
	[ALC268_3ST]		= "3stack",
	[ALC268_TOSHIBA]	= "toshiba",
	[ALC268_ACER]		= "acer",
	[ALC268_DELL]		= "dell",
	[ALC268_ZEPTO]		= "zepto",
#ifdef CONFIG_SND_DEBUG
	[ALC268_TEST]		= "test",
#endif
	[ALC268_AUTO]		= "auto",
};

static struct snd_pci_quirk alc268_cfg_tbl[] = {
	SND_PCI_QUIRK(0x1025, 0x011e, "Acer Aspire 5720z", ALC268_ACER),
	SND_PCI_QUIRK(0x1025, 0x0126, "Acer", ALC268_ACER),
	SND_PCI_QUIRK(0x1025, 0x012e, "Acer Aspire 5310", ALC268_ACER),
	SND_PCI_QUIRK(0x1025, 0x0130, "Acer Extensa 5210", ALC268_ACER),
	SND_PCI_QUIRK(0x1025, 0x0136, "Acer Aspire 5315", ALC268_ACER),
	SND_PCI_QUIRK(0x1028, 0x0253, "Dell OEM", ALC268_DELL),
	SND_PCI_QUIRK(0x103c, 0x30cc, "TOSHIBA", ALC268_TOSHIBA),
	SND_PCI_QUIRK(0x1043, 0x1205, "ASUS W7J", ALC268_3ST),
	SND_PCI_QUIRK(0x1179, 0xff10, "TOSHIBA A205", ALC268_TOSHIBA),
	SND_PCI_QUIRK(0x1179, 0xff50, "TOSHIBA A305", ALC268_TOSHIBA),
	SND_PCI_QUIRK(0x14c0, 0x0025, "COMPAL IFL90/JFL-92", ALC268_TOSHIBA),
	SND_PCI_QUIRK(0x152d, 0x0763, "Diverse (CPR2000)", ALC268_ACER),
	SND_PCI_QUIRK(0x152d, 0x0771, "Quanta IL1", ALC267_QUANTA_IL1),
	SND_PCI_QUIRK(0x1170, 0x0040, "ZEPTO", ALC268_ZEPTO),
	{}
};

static struct alc_config_preset alc268_presets[] = {
	[ALC267_QUANTA_IL1] = {
		.mixers = { alc267_quanta_il1_mixer },
		.init_verbs = { alc268_base_init_verbs, alc268_eapd_verbs,
				alc267_quanta_il1_verbs },
		.num_dacs = ARRAY_SIZE(alc268_dac_nids),
		.dac_nids = alc268_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc268_adc_nids_alt),
		.adc_nids = alc268_adc_nids_alt,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc268_modes),
		.channel_mode = alc268_modes,
		.input_mux = &alc268_capture_source,
		.unsol_event = alc267_quanta_il1_unsol_event,
		.init_hook = alc267_quanta_il1_automute,
	},
	[ALC268_3ST] = {
		.mixers = { alc268_base_mixer, alc268_capture_alt_mixer,
			    alc268_beep_mixer },
		.init_verbs = { alc268_base_init_verbs },
		.num_dacs = ARRAY_SIZE(alc268_dac_nids),
		.dac_nids = alc268_dac_nids,
                .num_adc_nids = ARRAY_SIZE(alc268_adc_nids_alt),
                .adc_nids = alc268_adc_nids_alt,
		.capsrc_nids = alc268_capsrc_nids,
		.hp_nid = 0x03,
		.dig_out_nid = ALC268_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc268_modes),
		.channel_mode = alc268_modes,
		.input_mux = &alc268_capture_source,
	},
	[ALC268_TOSHIBA] = {
		.mixers = { alc268_base_mixer, alc268_capture_alt_mixer,
			    alc268_beep_mixer },
		.init_verbs = { alc268_base_init_verbs, alc268_eapd_verbs,
				alc268_toshiba_verbs },
		.num_dacs = ARRAY_SIZE(alc268_dac_nids),
		.dac_nids = alc268_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc268_adc_nids_alt),
		.adc_nids = alc268_adc_nids_alt,
		.capsrc_nids = alc268_capsrc_nids,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc268_modes),
		.channel_mode = alc268_modes,
		.input_mux = &alc268_capture_source,
		.unsol_event = alc268_toshiba_unsol_event,
		.init_hook = alc268_toshiba_automute,
	},
	[ALC268_ACER] = {
		.mixers = { alc268_acer_mixer, alc268_capture_alt_mixer,
			    alc268_beep_mixer },
		.init_verbs = { alc268_base_init_verbs, alc268_eapd_verbs,
				alc268_acer_verbs },
		.num_dacs = ARRAY_SIZE(alc268_dac_nids),
		.dac_nids = alc268_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc268_adc_nids_alt),
		.adc_nids = alc268_adc_nids_alt,
		.capsrc_nids = alc268_capsrc_nids,
		.hp_nid = 0x02,
		.num_channel_mode = ARRAY_SIZE(alc268_modes),
		.channel_mode = alc268_modes,
		.input_mux = &alc268_acer_capture_source,
		.unsol_event = alc268_acer_unsol_event,
		.init_hook = alc268_acer_init_hook,
	},
	[ALC268_DELL] = {
		.mixers = { alc268_dell_mixer, alc268_beep_mixer },
		.init_verbs = { alc268_base_init_verbs, alc268_eapd_verbs,
				alc268_dell_verbs },
		.num_dacs = ARRAY_SIZE(alc268_dac_nids),
		.dac_nids = alc268_dac_nids,
		.hp_nid = 0x02,
		.num_channel_mode = ARRAY_SIZE(alc268_modes),
		.channel_mode = alc268_modes,
		.unsol_event = alc268_dell_unsol_event,
		.init_hook = alc268_dell_init_hook,
		.input_mux = &alc268_capture_source,
	},
	[ALC268_ZEPTO] = {
		.mixers = { alc268_base_mixer, alc268_capture_alt_mixer,
			    alc268_beep_mixer },
		.init_verbs = { alc268_base_init_verbs, alc268_eapd_verbs,
				alc268_toshiba_verbs },
		.num_dacs = ARRAY_SIZE(alc268_dac_nids),
		.dac_nids = alc268_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc268_adc_nids_alt),
		.adc_nids = alc268_adc_nids_alt,
		.capsrc_nids = alc268_capsrc_nids,
		.hp_nid = 0x03,
		.dig_out_nid = ALC268_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc268_modes),
		.channel_mode = alc268_modes,
		.input_mux = &alc268_capture_source,
		.unsol_event = alc268_toshiba_unsol_event,
		.init_hook = alc268_toshiba_automute
	},
#ifdef CONFIG_SND_DEBUG
	[ALC268_TEST] = {
		.mixers = { alc268_test_mixer, alc268_capture_mixer },
		.init_verbs = { alc268_base_init_verbs, alc268_eapd_verbs,
				alc268_volume_init_verbs },
		.num_dacs = ARRAY_SIZE(alc268_dac_nids),
		.dac_nids = alc268_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc268_adc_nids_alt),
		.adc_nids = alc268_adc_nids_alt,
		.capsrc_nids = alc268_capsrc_nids,
		.hp_nid = 0x03,
		.dig_out_nid = ALC268_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc268_modes),
		.channel_mode = alc268_modes,
		.input_mux = &alc268_capture_source,
	},
#endif
};

static int patch_alc268(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int board_config;
	int err;

	spec = kcalloc(1, sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	board_config = snd_hda_check_board_config(codec, ALC268_MODEL_LAST,
						  alc268_models,
						  alc268_cfg_tbl);

	if (board_config < 0 || board_config >= ALC268_MODEL_LAST) {
		printk(KERN_INFO "hda_codec: Unknown model for ALC268, "
		       "trying auto-probe from BIOS...\n");
		board_config = ALC268_AUTO;
	}

	if (board_config == ALC268_AUTO) {
		/* automatic parse from the BIOS config */
		err = alc268_parse_auto_config(codec);
		if (err < 0) {
			alc_free(codec);
			return err;
		} else if (!err) {
			printk(KERN_INFO
			       "hda_codec: Cannot set up configuration "
			       "from BIOS.  Using base mode...\n");
			board_config = ALC268_3ST;
		}
	}

	if (board_config != ALC268_AUTO)
		setup_preset(spec, &alc268_presets[board_config]);

	if (codec->vendor_id == 0x10ec0267) {
		spec->stream_name_analog = "ALC267 Analog";
		spec->stream_name_digital = "ALC267 Digital";
	} else {
		spec->stream_name_analog = "ALC268 Analog";
		spec->stream_name_digital = "ALC268 Digital";
	}

	spec->stream_analog_playback = &alc268_pcm_analog_playback;
	spec->stream_analog_capture = &alc268_pcm_analog_capture;
	spec->stream_analog_alt_capture = &alc268_pcm_analog_alt_capture;

	spec->stream_digital_playback = &alc268_pcm_digital_playback;

	if (!query_amp_caps(codec, 0x1d, HDA_INPUT))
		/* override the amp caps for beep generator */
		snd_hda_override_amp_caps(codec, 0x1d, HDA_INPUT,
	static const hda_nid_t alc268_ssids[] = { 0x15, 0x1b, 0x14, 0 };
	return alc_parse_auto_config(codec, NULL, alc268_ssids);
}

/*
 */
static int patch_alc268(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err;

	/* ALC268 has no aa-loopback mixer */
	err = alc_alloc_spec(codec, 0);
	if (err < 0)
		return err;

	spec = codec->spec;
	spec->gen.beep_nid = 0x01;

	spec->shutup = alc_eapd_shutup;

	snd_hda_pick_fixup(codec, alc268_fixup_models, alc268_fixup_tbl, alc268_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	/* automatic parse from the BIOS config */
	err = alc268_parse_auto_config(codec);
	if (err < 0)
		goto error;

	if (err > 0 && !spec->gen.no_analog &&
	    spec->gen.autocfg.speaker_pins[0] != 0x1d) {
		add_mixer(spec, alc268_beep_mixer);
		snd_hda_add_verbs(codec, alc268_beep_init_verbs);
		if (!query_amp_caps(codec, 0x1d, HDA_INPUT))
			/* override the amp caps for beep generator */
			snd_hda_override_amp_caps(codec, 0x1d, HDA_INPUT,
					  (0x0c << AC_AMPCAP_OFFSET_SHIFT) |
					  (0x0c << AC_AMPCAP_NUM_STEPS_SHIFT) |
					  (0x07 << AC_AMPCAP_STEP_SIZE_SHIFT) |
					  (0 << AC_AMPCAP_MUTE_SHIFT));

	if (!spec->adc_nids && spec->input_mux) {
		/* check whether NID 0x07 is valid */
		unsigned int wcap = get_wcaps(codec, 0x07);
		int i;

		/* get type */
		wcap = (wcap & AC_WCAP_TYPE) >> AC_WCAP_TYPE_SHIFT;
		if (wcap != AC_WID_AUD_IN || spec->input_mux->num_items == 1) {
			spec->adc_nids = alc268_adc_nids_alt;
			spec->num_adc_nids = ARRAY_SIZE(alc268_adc_nids_alt);
			spec->mixers[spec->num_mixers] =
					alc268_capture_alt_mixer;
			spec->num_mixers++;
		} else {
			spec->adc_nids = alc268_adc_nids;
			spec->num_adc_nids = ARRAY_SIZE(alc268_adc_nids);
			spec->mixers[spec->num_mixers] =
				alc268_capture_mixer;
			spec->num_mixers++;
		}
		spec->capsrc_nids = alc268_capsrc_nids;
		/* set default input source */
		for (i = 0; i < spec->num_adc_nids; i++)
			snd_hda_codec_write_cache(codec, alc268_capsrc_nids[i],
				0, AC_VERB_SET_CONNECT_SEL,
				spec->input_mux->items[0].index);
	}

	spec->vmaster_nid = 0x02;

	codec->patch_ops = alc_patch_ops;
	if (board_config == ALC268_AUTO)
		spec->init_hook = alc268_auto_init;
		
	return 0;
}

/*
 *  ALC269 channel source setting (2 channel)
 */
#define ALC269_DIGOUT_NID	ALC880_DIGOUT_NID

#define alc269_dac_nids		alc260_dac_nids

static hda_nid_t alc269_adc_nids[1] = {
	/* ADC1 */
	0x08,
};

static struct hda_input_mux alc269_eeepc_dmic_capture_source = {
	.num_items = 2,
	.items = {
		{ "i-Mic", 0x5 },
		{ "e-Mic", 0x0 },
	},
};

static struct hda_input_mux alc269_eeepc_amic_capture_source = {
	.num_items = 2,
	.items = {
		{ "i-Mic", 0x1 },
		{ "e-Mic", 0x0 },
	},
};

#define alc269_modes		alc260_modes
#define alc269_capture_source	alc880_lg_lw_capture_source

static struct snd_kcontrol_new alc269_base_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Mono Playback Switch", 0x16, 2, 0x0, HDA_OUTPUT),
	{ } /* end */
};

/* bind volumes of both NID 0x0c and 0x0d */
static struct hda_bind_ctls alc269_epc_bind_vol = {
	.ops = &snd_hda_bind_vol,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x02, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x03, 3, 0, HDA_OUTPUT),
		0
	},
};

static struct snd_kcontrol_new alc269_eeepc_mixer[] = {
	HDA_CODEC_MUTE("iSpeaker Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_BIND_VOL("LineOut Playback Volume", &alc269_epc_bind_vol),
	HDA_CODEC_MUTE("LineOut Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	{ } /* end */
};

/* capture mixer elements */
static struct snd_kcontrol_new alc269_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 1,
		.info = alc_mux_enum_info,
		.get = alc_mux_enum_get,
		.put = alc_mux_enum_put,
	},
	{ } /* end */
};

/* capture mixer elements */
static struct snd_kcontrol_new alc269_epc_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	{ } /* end */
};

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static struct hda_verb alc269_init_verbs[] = {
	/*
	 * Unmute ADC0 and set the default input to mic-in
	 */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	/* Mute input amps (PCBeep, Line In, Mic 1 & Mic 2) of the
	 * analog-loopback mixer widget
	 * Note: PASD motherboards uses the Line In 2 as the input for
	 * front panel mic (mic 2)
	 */
	/* Amp Indices: Mic1 = 0, Mic2 = 1, Line1 = 2, Line2 = 3, CD = 4 */
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},

	/*
	 * Set up output mixers (0x0c - 0x0e)
	 */
	/* set vol=0 to output mixers */
	{0x02, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},

	/* set up input amps for analog loopback */
	/* Amp Indices: DAC = 0, mixer = 1 */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},

	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},

	/* FIXME: use matrix-type input source selection */
	/* Mixer elements: 0x18, 19, 1a, 1b, 1d, 0b */
	/* Input mixer1: unmute Mic, F-Mic, Line, CD inputs */
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},

	/* set EAPD */
	{0x14, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{0x15, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{ }
};

static struct hda_verb alc269_eeepc_dmic_init_verbs[] = {
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x23, AC_VERB_SET_CONNECT_SEL, 0x05},
	{0x02, AC_VERB_SET_AMP_GAIN_MUTE, 0xb026 },
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, (0x7019 | (0x00 << 8))},
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{}
};

static struct hda_verb alc269_eeepc_amic_init_verbs[] = {
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x23, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x02, AC_VERB_SET_AMP_GAIN_MUTE, 0xb026 },
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, (0x701b | (0x00 << 8))},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{}
};

/* toggle speaker-output according to the hp-jack state */
static void alc269_speaker_automute(struct hda_codec *codec)
{
	unsigned int present;
	unsigned int bits;

	present = snd_hda_codec_read(codec, 0x15, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	bits = present ? AMP_IN_MUTE(0) : 0;
	snd_hda_codec_amp_stereo(codec, 0x0c, HDA_INPUT, 0,
				 AMP_IN_MUTE(0), bits);
	snd_hda_codec_amp_stereo(codec, 0x0c, HDA_INPUT, 1,
				 AMP_IN_MUTE(0), bits);
}

static void alc269_eeepc_dmic_automute(struct hda_codec *codec)
{
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x18, 0, AC_VERB_GET_PIN_SENSE, 0)
		& AC_PINSENSE_PRESENCE;
	snd_hda_codec_write(codec, 0x23, 0, AC_VERB_SET_CONNECT_SEL,
			    present ? 0 : 5);
}

static void alc269_eeepc_amic_automute(struct hda_codec *codec)
{
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x18, 0, AC_VERB_GET_PIN_SENSE, 0)
		& AC_PINSENSE_PRESENCE;
	snd_hda_codec_write(codec, 0x24, 0, AC_VERB_SET_AMP_GAIN_MUTE,
			    present ? AMP_IN_UNMUTE(0) : AMP_IN_MUTE(0));
	snd_hda_codec_write(codec, 0x24, 0, AC_VERB_SET_AMP_GAIN_MUTE,
			    present ? AMP_IN_MUTE(1) : AMP_IN_UNMUTE(1));
}

/* unsolicited event for HP jack sensing */
static void alc269_eeepc_dmic_unsol_event(struct hda_codec *codec,
					  unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc269_speaker_automute(codec);

	if ((res >> 26) == ALC880_MIC_EVENT)
		alc269_eeepc_dmic_automute(codec);
}

static void alc269_eeepc_dmic_inithook(struct hda_codec *codec)
{
	alc269_speaker_automute(codec);
	alc269_eeepc_dmic_automute(codec);
}

/* unsolicited event for HP jack sensing */
static void alc269_eeepc_amic_unsol_event(struct hda_codec *codec,
					  unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc269_speaker_automute(codec);

	if ((res >> 26) == ALC880_MIC_EVENT)
		alc269_eeepc_amic_automute(codec);
}

static void alc269_eeepc_amic_inithook(struct hda_codec *codec)
{
	alc269_speaker_automute(codec);
	alc269_eeepc_amic_automute(codec);
}

/* add playback controls from the parsed DAC table */
static int alc269_auto_create_multi_out_ctls(struct alc_spec *spec,
					     const struct auto_pin_cfg *cfg)
{
	hda_nid_t nid;
	int err;

	spec->multiout.num_dacs = 1;	/* only use one dac */
	spec->multiout.dac_nids = spec->private_dac_nids;
	spec->multiout.dac_nids[0] = 2;

	nid = cfg->line_out_pins[0];
	if (nid) {
		err = add_control(spec, ALC_CTL_WIDGET_VOL,
				  "Front Playback Volume",
				  HDA_COMPOSE_AMP_VAL(0x02, 3, 0, HDA_OUTPUT));
		if (err < 0)
			return err;
		err = add_control(spec, ALC_CTL_WIDGET_MUTE,
				  "Front Playback Switch",
				  HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT));
		if (err < 0)
			return err;
	}

	nid = cfg->speaker_pins[0];
	if (nid) {
		if (!cfg->line_out_pins[0]) {
			err = add_control(spec, ALC_CTL_WIDGET_VOL,
					  "Speaker Playback Volume",
					  HDA_COMPOSE_AMP_VAL(0x02, 3, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
		}
		if (nid == 0x16) {
			err = add_control(spec, ALC_CTL_WIDGET_MUTE,
					  "Speaker Playback Switch",
					  HDA_COMPOSE_AMP_VAL(nid, 2, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
		} else {
			err = add_control(spec, ALC_CTL_WIDGET_MUTE,
					  "Speaker Playback Switch",
					  HDA_COMPOSE_AMP_VAL(nid, 3, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
		}
	}
	nid = cfg->hp_pins[0];
	if (nid) {
		/* spec->multiout.hp_nid = 2; */
		if (!cfg->line_out_pins[0] && !cfg->speaker_pins[0]) {
			err = add_control(spec, ALC_CTL_WIDGET_VOL,
					  "Headphone Playback Volume",
					  HDA_COMPOSE_AMP_VAL(0x02, 3, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
		}
		if (nid == 0x16) {
			err = add_control(spec, ALC_CTL_WIDGET_MUTE,
					  "Headphone Playback Switch",
					  HDA_COMPOSE_AMP_VAL(nid, 2, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
		} else {
			err = add_control(spec, ALC_CTL_WIDGET_MUTE,
					  "Headphone Playback Switch",
					  HDA_COMPOSE_AMP_VAL(nid, 3, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
		}
	}
	return 0;
}

#define alc269_auto_create_analog_input_ctls \
	alc880_auto_create_analog_input_ctls

#ifdef CONFIG_SND_HDA_POWER_SAVE
#define alc269_loopbacks	alc880_loopbacks
#endif

/* pcm configuration: identiacal with ALC880 */
#define alc269_pcm_analog_playback	alc880_pcm_analog_playback
#define alc269_pcm_analog_capture	alc880_pcm_analog_capture
#define alc269_pcm_digital_playback	alc880_pcm_digital_playback
#define alc269_pcm_digital_capture	alc880_pcm_digital_capture

	}

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;

 error:
	alc_free(codec);
	return err;
}

/*
 * ALC269
 */

static const struct hda_pcm_stream alc269_44k_pcm_analog_playback = {
	.rates = SNDRV_PCM_RATE_44100, /* fixed rate */
};

static const struct hda_pcm_stream alc269_44k_pcm_analog_capture = {
	.rates = SNDRV_PCM_RATE_44100, /* fixed rate */
};

/* different alc269-variants */
enum {
	ALC269_TYPE_ALC269VA,
	ALC269_TYPE_ALC269VB,
	ALC269_TYPE_ALC269VC,
	ALC269_TYPE_ALC269VD,
	ALC269_TYPE_ALC280,
	ALC269_TYPE_ALC282,
	ALC269_TYPE_ALC283,
	ALC269_TYPE_ALC284,
	ALC269_TYPE_ALC285,
	ALC269_TYPE_ALC286,
	ALC269_TYPE_ALC298,
	ALC269_TYPE_ALC255,
	ALC269_TYPE_ALC256,
	ALC269_TYPE_ALC225,
	ALC269_TYPE_ALC294,
	ALC269_TYPE_ALC700,
};

/*
 * BIOS auto configuration
 */
static int alc269_parse_auto_config(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int err;
	static hda_nid_t alc269_ignore[] = { 0x1d, 0 };

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg,
					   alc269_ignore);
	if (err < 0)
		return err;

	err = alc269_auto_create_multi_out_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = alc269_auto_create_analog_input_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;

	if (spec->autocfg.dig_out_pin)
		spec->multiout.dig_out_nid = ALC269_DIGOUT_NID;

	if (spec->kctl_alloc)
		spec->mixers[spec->num_mixers++] = spec->kctl_alloc;

	spec->init_verbs[spec->num_init_verbs++] = alc269_init_verbs;
	spec->num_mux_defs = 1;
	spec->input_mux = &spec->private_imux;

	err = alc_auto_add_mic_boost(codec);
	if (err < 0)
		return err;

	spec->mixers[spec->num_mixers] = alc269_capture_mixer;
	spec->num_mixers++;

	return 1;
}

#define alc269_auto_init_multi_out	alc882_auto_init_multi_out
#define alc269_auto_init_hp_out		alc882_auto_init_hp_out
#define alc269_auto_init_analog_input	alc882_auto_init_analog_input


/* init callback for auto-configuration model -- overriding the default init */
static void alc269_auto_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	alc269_auto_init_multi_out(codec);
	alc269_auto_init_hp_out(codec);
	alc269_auto_init_analog_input(codec);
	if (spec->unsol_event)
		alc_sku_automute(codec);
}

/*
 * configuration and preset
 */
static const char *alc269_models[ALC269_MODEL_LAST] = {
	[ALC269_BASIC]		= "basic",
};

static struct snd_pci_quirk alc269_cfg_tbl[] = {
	SND_PCI_QUIRK(0x1043, 0x8330, "ASUS Eeepc P703 P900A",
		      ALC269_ASUS_EEEPC_P703),
	SND_PCI_QUIRK(0x1043, 0x831a, "ASUS Eeepc P901",
		      ALC269_ASUS_EEEPC_P901),
	{}
};

static struct alc_config_preset alc269_presets[] = {
	[ALC269_BASIC] = {
		.mixers = { alc269_base_mixer, alc269_capture_mixer },
		.init_verbs = { alc269_init_verbs },
		.num_dacs = ARRAY_SIZE(alc269_dac_nids),
		.dac_nids = alc269_dac_nids,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc269_modes),
		.channel_mode = alc269_modes,
		.input_mux = &alc269_capture_source,
	},
	[ALC269_ASUS_EEEPC_P703] = {
		.mixers = { alc269_eeepc_mixer, alc269_epc_capture_mixer },
		.init_verbs = { alc269_init_verbs,
				alc269_eeepc_amic_init_verbs },
		.num_dacs = ARRAY_SIZE(alc269_dac_nids),
		.dac_nids = alc269_dac_nids,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc269_modes),
		.channel_mode = alc269_modes,
		.input_mux = &alc269_eeepc_amic_capture_source,
		.unsol_event = alc269_eeepc_amic_unsol_event,
		.init_hook = alc269_eeepc_amic_inithook,
	},
	[ALC269_ASUS_EEEPC_P901] = {
		.mixers = { alc269_eeepc_mixer, alc269_epc_capture_mixer},
		.init_verbs = { alc269_init_verbs,
				alc269_eeepc_dmic_init_verbs },
		.num_dacs = ARRAY_SIZE(alc269_dac_nids),
		.dac_nids = alc269_dac_nids,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc269_modes),
		.channel_mode = alc269_modes,
		.input_mux = &alc269_eeepc_dmic_capture_source,
		.unsol_event = alc269_eeepc_dmic_unsol_event,
		.init_hook = alc269_eeepc_dmic_inithook,
	},
};

static int patch_alc269(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int board_config;
	int err;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	alc_fix_pll_init(codec, 0x20, 0x04, 15);

	board_config = snd_hda_check_board_config(codec, ALC269_MODEL_LAST,
						  alc269_models,
						  alc269_cfg_tbl);

	if (board_config < 0) {
		printk(KERN_INFO "hda_codec: Unknown model for ALC269, "
		       "trying auto-probe from BIOS...\n");
		board_config = ALC269_AUTO;
	}

	if (board_config == ALC269_AUTO) {
		/* automatic parse from the BIOS config */
		err = alc269_parse_auto_config(codec);
		if (err < 0) {
			alc_free(codec);
			return err;
		} else if (!err) {
			printk(KERN_INFO
			       "hda_codec: Cannot set up configuration "
			       "from BIOS.  Using base mode...\n");
			board_config = ALC269_BASIC;
		}
	}

	if (board_config != ALC269_AUTO)
		setup_preset(spec, &alc269_presets[board_config]);

	spec->stream_name_analog = "ALC269 Analog";
	spec->stream_analog_playback = &alc269_pcm_analog_playback;
	spec->stream_analog_capture = &alc269_pcm_analog_capture;

	spec->stream_name_digital = "ALC269 Digital";
	spec->stream_digital_playback = &alc269_pcm_digital_playback;
	spec->stream_digital_capture = &alc269_pcm_digital_capture;

	spec->adc_nids = alc269_adc_nids;
	spec->num_adc_nids = ARRAY_SIZE(alc269_adc_nids);

	codec->patch_ops = alc_patch_ops;
	if (board_config == ALC269_AUTO)
		spec->init_hook = alc269_auto_init;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	if (!spec->loopback.amplist)
		spec->loopback.amplist = alc269_loopbacks;
#endif

	return 0;
}

/*
 *  ALC861 channel source setting (2/6 channel selection for 3-stack)
 */

/*
 * set the path ways for 2 channel output
 * need to set the codec line out and mic 1 pin widgets to inputs
 */
static struct hda_verb alc861_threestack_ch2_init[] = {
	/* set pin widget 1Ah (line in) for input */
	{ 0x0c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	/* set pin widget 18h (mic1/2) for input, for mic also enable
	 * the vref
	 */
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },

	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0xb00c },
#if 0
	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8)) }, /*mic*/
	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x02 << 8)) }, /*line-in*/
#endif
	{ } /* end */
};
/*
 * 6ch mode
 * need to set the codec line out and mic 1 pin widgets to outputs
 */
static struct hda_verb alc861_threestack_ch6_init[] = {
	/* set pin widget 1Ah (line in) for output (Back Surround)*/
	{ 0x0c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	/* set pin widget 18h (mic1) for output (CLFE)*/
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },

	{ 0x0c, AC_VERB_SET_CONNECT_SEL, 0x00 },
	{ 0x0d, AC_VERB_SET_CONNECT_SEL, 0x00 },

	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080 },
#if 0
	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x01 << 8)) }, /*mic*/
	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x02 << 8)) }, /*line in*/
#endif
	{ } /* end */
};

static struct hda_channel_mode alc861_threestack_modes[2] = {
	{ 2, alc861_threestack_ch2_init },
	{ 6, alc861_threestack_ch6_init },
};
/* Set mic1 as input and unmute the mixer */
static struct hda_verb alc861_uniwill_m31_ch2_init[] = {
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x01 << 8)) }, /*mic*/
	{ } /* end */
};
/* Set mic1 as output and mute mixer */
static struct hda_verb alc861_uniwill_m31_ch4_init[] = {
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8)) }, /*mic*/
	{ } /* end */
};

static struct hda_channel_mode alc861_uniwill_m31_modes[2] = {
	{ 2, alc861_uniwill_m31_ch2_init },
	{ 4, alc861_uniwill_m31_ch4_init },
};

/* Set mic1 and line-in as input and unmute the mixer */
static struct hda_verb alc861_asus_ch2_init[] = {
	/* set pin widget 1Ah (line in) for input */
	{ 0x0c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	/* set pin widget 18h (mic1/2) for input, for mic also enable
	 * the vref
	 */
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },

	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0xb00c },
#if 0
	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8)) }, /*mic*/
	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x02 << 8)) }, /*line-in*/
#endif
	{ } /* end */
};
/* Set mic1 nad line-in as output and mute mixer */
static struct hda_verb alc861_asus_ch6_init[] = {
	/* set pin widget 1Ah (line in) for output (Back Surround)*/
	{ 0x0c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	/* { 0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE }, */
	/* set pin widget 18h (mic1) for output (CLFE)*/
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	/* { 0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE }, */
	{ 0x0c, AC_VERB_SET_CONNECT_SEL, 0x00 },
	{ 0x0d, AC_VERB_SET_CONNECT_SEL, 0x00 },

	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080 },
#if 0
	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x01 << 8)) }, /*mic*/
	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x02 << 8)) }, /*line in*/
#endif
	{ } /* end */
};

static struct hda_channel_mode alc861_asus_modes[2] = {
	{ 2, alc861_asus_ch2_init },
	{ 6, alc861_asus_ch6_init },
};

/* patch-ALC861 */

static struct snd_kcontrol_new alc861_base_mixer[] = {
        /* output mixer control */
	HDA_CODEC_MUTE("Front Playback Switch", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Surround Playback Switch", 0x06, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Center Playback Switch", 0x05, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("LFE Playback Switch", 0x05, 2, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Side Playback Switch", 0x04, 0x0, HDA_OUTPUT),

        /*Input mixer control */
	/* HDA_CODEC_VOLUME("Input Playback Volume", 0x15, 0x0, HDA_OUTPUT),
	   HDA_CODEC_MUTE("Input Playback Switch", 0x15, 0x0, HDA_OUTPUT), */
	HDA_CODEC_VOLUME("CD Playback Volume", 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x15, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x15, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x15, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x15, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x10, 0x01, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1a, 0x03, HDA_INPUT),

        /* Capture mixer control */
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.count = 1,
		.info = alc_mux_enum_info,
		.get = alc_mux_enum_get,
		.put = alc_mux_enum_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc861_3ST_mixer[] = {
        /* output mixer control */
	HDA_CODEC_MUTE("Front Playback Switch", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Surround Playback Switch", 0x06, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Center Playback Switch", 0x05, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("LFE Playback Switch", 0x05, 2, 0x0, HDA_OUTPUT),
	/*HDA_CODEC_MUTE("Side Playback Switch", 0x04, 0x0, HDA_OUTPUT), */

	/* Input mixer control */
	/* HDA_CODEC_VOLUME("Input Playback Volume", 0x15, 0x0, HDA_OUTPUT),
	   HDA_CODEC_MUTE("Input Playback Switch", 0x15, 0x0, HDA_OUTPUT), */
	HDA_CODEC_VOLUME("CD Playback Volume", 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x15, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x15, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x15, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x15, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x10, 0x01, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1a, 0x03, HDA_INPUT),

	/* Capture mixer control */
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.count = 1,
		.info = alc_mux_enum_info,
		.get = alc_mux_enum_get,
		.put = alc_mux_enum_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
                .private_value = ARRAY_SIZE(alc861_threestack_modes),
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc861_toshiba_mixer[] = {
        /* output mixer control */
	HDA_CODEC_MUTE("Master Playback Switch", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x15, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x15, 0x01, HDA_INPUT),
	
        /*Capture mixer control */
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.count = 1,
		.info = alc_mux_enum_info,
		.get = alc_mux_enum_get,
		.put = alc_mux_enum_put,
	},

	{ } /* end */
};

static struct snd_kcontrol_new alc861_uniwill_m31_mixer[] = {
        /* output mixer control */
	HDA_CODEC_MUTE("Front Playback Switch", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Surround Playback Switch", 0x06, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Center Playback Switch", 0x05, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("LFE Playback Switch", 0x05, 2, 0x0, HDA_OUTPUT),
	/*HDA_CODEC_MUTE("Side Playback Switch", 0x04, 0x0, HDA_OUTPUT), */

	/* Input mixer control */
	/* HDA_CODEC_VOLUME("Input Playback Volume", 0x15, 0x0, HDA_OUTPUT),
	   HDA_CODEC_MUTE("Input Playback Switch", 0x15, 0x0, HDA_OUTPUT), */
	HDA_CODEC_VOLUME("CD Playback Volume", 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x15, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x15, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x15, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x15, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x10, 0x01, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1a, 0x03, HDA_INPUT),

	/* Capture mixer control */
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.count = 1,
		.info = alc_mux_enum_info,
		.get = alc_mux_enum_get,
		.put = alc_mux_enum_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
                .private_value = ARRAY_SIZE(alc861_uniwill_m31_modes),
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc861_asus_mixer[] = {
        /* output mixer control */
	HDA_CODEC_MUTE("Front Playback Switch", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Surround Playback Switch", 0x06, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Center Playback Switch", 0x05, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("LFE Playback Switch", 0x05, 2, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Side Playback Switch", 0x04, 0x0, HDA_OUTPUT),

	/* Input mixer control */
	HDA_CODEC_VOLUME("Input Playback Volume", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Input Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x15, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x15, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x15, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x15, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x10, 0x01, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1a, 0x03, HDA_OUTPUT),

	/* Capture mixer control */
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.count = 1,
		.info = alc_mux_enum_info,
		.get = alc_mux_enum_get,
		.put = alc_mux_enum_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
                .private_value = ARRAY_SIZE(alc861_asus_modes),
	},
	{ }
};

/* additional mixer */
static struct snd_kcontrol_new alc861_asus_laptop_mixer[] = {
	HDA_CODEC_VOLUME("CD Playback Volume", 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Beep Playback Volume", 0x23, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("PC Beep Playback Switch", 0x23, 0x0, HDA_OUTPUT),
	{ }
};

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static struct hda_verb alc861_base_init_verbs[] = {
	/*
	 * Unmute ADC0 and set the default input to mic-in
	 */
	/* port-A for surround (rear panel) */
	{ 0x0e, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	{ 0x0e, AC_VERB_SET_CONNECT_SEL, 0x00 },
	/* port-B for mic-in (rear panel) with vref */
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* port-C for line-in (rear panel) */
	{ 0x0c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	/* port-D for Front */
	{ 0x0b, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	{ 0x0b, AC_VERB_SET_CONNECT_SEL, 0x00 },
	/* port-E for HP out (front panel) */
	{ 0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0 },
	/* route front PCM to HP */
	{ 0x0f, AC_VERB_SET_CONNECT_SEL, 0x00 },
	/* port-F for mic-in (front panel) with vref */
	{ 0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* port-G for CLFE (rear panel) */
	{ 0x1f, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	{ 0x1f, AC_VERB_SET_CONNECT_SEL, 0x00 },
	/* port-H for side (rear panel) */
	{ 0x20, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	{ 0x20, AC_VERB_SET_CONNECT_SEL, 0x00 },
	/* CD-in */
	{ 0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	/* route front mic to ADC1*/
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	
	/* Unmute DAC0~3 & spdif out*/
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x06, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	
	/* Unmute Mixer 14 (mic) 1c (Line in)*/
	{0x014, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
        {0x014, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x01c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
        {0x01c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	
	/* Unmute Stereo Mixer 15 */
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0xb00c}, /* Output 0~12 step */

	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	/* hp used DAC 3 (Front) */
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(3)},
        {0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},

	{ }
};

static struct hda_verb alc861_threestack_init_verbs[] = {
	/*
	 * Unmute ADC0 and set the default input to mic-in
	 */
	/* port-A for surround (rear panel) */
	{ 0x0e, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00 },
	/* port-B for mic-in (rear panel) with vref */
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* port-C for line-in (rear panel) */
	{ 0x0c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	/* port-D for Front */
	{ 0x0b, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	{ 0x0b, AC_VERB_SET_CONNECT_SEL, 0x00 },
	/* port-E for HP out (front panel) */
	{ 0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0 },
	/* route front PCM to HP */
	{ 0x0f, AC_VERB_SET_CONNECT_SEL, 0x00 },
	/* port-F for mic-in (front panel) with vref */
	{ 0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* port-G for CLFE (rear panel) */
	{ 0x1f, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00 },
	/* port-H for side (rear panel) */
	{ 0x20, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00 },
	/* CD-in */
	{ 0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	/* route front mic to ADC1*/
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	/* Unmute DAC0~3 & spdif out*/
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x06, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	
	/* Unmute Mixer 14 (mic) 1c (Line in)*/
	{0x014, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
        {0x014, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x01c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
        {0x01c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	
	/* Unmute Stereo Mixer 15 */
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0xb00c}, /* Output 0~12 step */

	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	/* hp used DAC 3 (Front) */
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(3)},
        {0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{ }
};

static struct hda_verb alc861_uniwill_m31_init_verbs[] = {
	/*
	 * Unmute ADC0 and set the default input to mic-in
	 */
	/* port-A for surround (rear panel) */
	{ 0x0e, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00 },
	/* port-B for mic-in (rear panel) with vref */
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* port-C for line-in (rear panel) */
	{ 0x0c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	/* port-D for Front */
	{ 0x0b, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	{ 0x0b, AC_VERB_SET_CONNECT_SEL, 0x00 },
	/* port-E for HP out (front panel) */
	/* this has to be set to VREF80 */
	{ 0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* route front PCM to HP */
	{ 0x0f, AC_VERB_SET_CONNECT_SEL, 0x00 },
	/* port-F for mic-in (front panel) with vref */
	{ 0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* port-G for CLFE (rear panel) */
	{ 0x1f, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00 },
	/* port-H for side (rear panel) */
	{ 0x20, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00 },
	/* CD-in */
	{ 0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	/* route front mic to ADC1*/
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	/* Unmute DAC0~3 & spdif out*/
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x06, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	
	/* Unmute Mixer 14 (mic) 1c (Line in)*/
	{0x014, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
        {0x014, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x01c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
        {0x01c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	
	/* Unmute Stereo Mixer 15 */
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0xb00c}, /* Output 0~12 step */

	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	/* hp used DAC 3 (Front) */
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(3)},
        {0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{ }
};

static struct hda_verb alc861_asus_init_verbs[] = {
	/*
	 * Unmute ADC0 and set the default input to mic-in
	 */
	/* port-A for surround (rear panel)
	 * according to codec#0 this is the HP jack
	 */
	{ 0x0e, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0 }, /* was 0x00 */
	/* route front PCM to HP */
	{ 0x0e, AC_VERB_SET_CONNECT_SEL, 0x01 },
	/* port-B for mic-in (rear panel) with vref */
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* port-C for line-in (rear panel) */
	{ 0x0c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	/* port-D for Front */
	{ 0x0b, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	{ 0x0b, AC_VERB_SET_CONNECT_SEL, 0x00 },
	/* port-E for HP out (front panel) */
	/* this has to be set to VREF80 */
	{ 0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* route front PCM to HP */
	{ 0x0f, AC_VERB_SET_CONNECT_SEL, 0x00 },
	/* port-F for mic-in (front panel) with vref */
	{ 0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* port-G for CLFE (rear panel) */
	{ 0x1f, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	/* port-H for side (rear panel) */
	{ 0x20, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	/* CD-in */
	{ 0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	/* route front mic to ADC1*/
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	/* Unmute DAC0~3 & spdif out*/
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x06, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Unmute Mixer 14 (mic) 1c (Line in)*/
	{0x014, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
        {0x014, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x01c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
        {0x01c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	
	/* Unmute Stereo Mixer 15 */
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0xb00c}, /* Output 0~12 step */

	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	/* hp used DAC 3 (Front) */
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(3)},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{ }
};

/* additional init verbs for ASUS laptops */
static struct hda_verb alc861_asus_laptop_init_verbs[] = {
	{ 0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x45 }, /* HP-out */
	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2) }, /* mute line-in */
	{ }
};

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static struct hda_verb alc861_auto_init_verbs[] = {
	/*
	 * Unmute ADC0 and set the default input to mic-in
	 */
	/* {0x08, AC_VERB_SET_CONNECT_SEL, 0x00}, */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	
	/* Unmute DAC0~3 & spdif out*/
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x06, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	
	/* Unmute Mixer 14 (mic) 1c (Line in)*/
	{0x014, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x014, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x01c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x01c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	
	/* Unmute Stereo Mixer 15 */
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0xb00c},

	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(3)},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(3)},

	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},	/* set Mic 1 */

	{ }
};

static struct hda_verb alc861_toshiba_init_verbs[] = {
	{0x0f, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},

	{ }
};

/* toggle speaker-output according to the hp-jack state */
static void alc861_toshiba_automute(struct hda_codec *codec)
{
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x0f, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	snd_hda_codec_amp_stereo(codec, 0x16, HDA_INPUT, 0,
				 HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
	snd_hda_codec_amp_stereo(codec, 0x1a, HDA_INPUT, 3,
				 HDA_AMP_MUTE, present ? 0 : HDA_AMP_MUTE);
}

static void alc861_toshiba_unsol_event(struct hda_codec *codec,
				       unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc861_toshiba_automute(codec);
}

/* pcm configuration: identiacal with ALC880 */
#define alc861_pcm_analog_playback	alc880_pcm_analog_playback
#define alc861_pcm_analog_capture	alc880_pcm_analog_capture
#define alc861_pcm_digital_playback	alc880_pcm_digital_playback
#define alc861_pcm_digital_capture	alc880_pcm_digital_capture


#define ALC861_DIGOUT_NID	0x07

static struct hda_channel_mode alc861_8ch_modes[1] = {
	{ 8, NULL }
};

static hda_nid_t alc861_dac_nids[4] = {
	/* front, surround, clfe, side */
	0x03, 0x06, 0x05, 0x04
};

static hda_nid_t alc660_dac_nids[3] = {
	/* front, clfe, surround */
	0x03, 0x05, 0x06
};

static hda_nid_t alc861_adc_nids[1] = {
	/* ADC0-2 */
	0x08,
};

static struct hda_input_mux alc861_capture_source = {
	.num_items = 5,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x3 },
		{ "Line", 0x1 },
		{ "CD", 0x4 },
		{ "Mixer", 0x5 },
	},
};

/* fill in the dac_nids table from the parsed pin configuration */
static int alc861_auto_fill_dac_nids(struct alc_spec *spec,
				     const struct auto_pin_cfg *cfg)
{
	int i;
	hda_nid_t nid;

	spec->multiout.dac_nids = spec->private_dac_nids;
	for (i = 0; i < cfg->line_outs; i++) {
		nid = cfg->line_out_pins[i];
		if (nid) {
			if (i >= ARRAY_SIZE(alc861_dac_nids))
				continue;
			spec->multiout.dac_nids[i] = alc861_dac_nids[i];
		}
	}
	spec->multiout.num_dacs = cfg->line_outs;
	return 0;
}

/* add playback controls from the parsed DAC table */
static int alc861_auto_create_multi_out_ctls(struct alc_spec *spec,
					     const struct auto_pin_cfg *cfg)
{
	char name[32];
	static const char *chname[4] = {
		"Front", "Surround", NULL /*CLFE*/, "Side"
	};
	hda_nid_t nid;
	int i, idx, err;

	for (i = 0; i < cfg->line_outs; i++) {
		nid = spec->multiout.dac_nids[i];
		if (!nid)
			continue;
		if (nid == 0x05) {
			/* Center/LFE */
			err = add_control(spec, ALC_CTL_BIND_MUTE,
					  "Center Playback Switch",
					  HDA_COMPOSE_AMP_VAL(nid, 1, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
			err = add_control(spec, ALC_CTL_BIND_MUTE,
					  "LFE Playback Switch",
					  HDA_COMPOSE_AMP_VAL(nid, 2, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
		} else {
			for (idx = 0; idx < ARRAY_SIZE(alc861_dac_nids) - 1;
			     idx++)
				if (nid == alc861_dac_nids[idx])
					break;
			sprintf(name, "%s Playback Switch", chname[idx]);
			err = add_control(spec, ALC_CTL_BIND_MUTE, name,
					  HDA_COMPOSE_AMP_VAL(nid, 3, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
		}
	}
	return 0;
}

static int alc861_auto_create_hp_ctls(struct alc_spec *spec, hda_nid_t pin)
{
	int err;
	hda_nid_t nid;

	if (!pin)
		return 0;

	if ((pin >= 0x0b && pin <= 0x10) || pin == 0x1f || pin == 0x20) {
		nid = 0x03;
		err = add_control(spec, ALC_CTL_WIDGET_MUTE,
				  "Headphone Playback Switch",
				  HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT));
		if (err < 0)
			return err;
		spec->multiout.hp_nid = nid;
	}
	return 0;
}

/* create playback/capture controls for input pins */
static int alc861_auto_create_analog_input_ctls(struct alc_spec *spec,
						const struct auto_pin_cfg *cfg)
{
	struct hda_input_mux *imux = &spec->private_imux;
	int i, err, idx, idx1;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		switch (cfg->input_pins[i]) {
		case 0x0c:
			idx1 = 1;
			idx = 2;	/* Line In */
			break;
		case 0x0f:
			idx1 = 2;
			idx = 2;	/* Line In */
			break;
		case 0x0d:
			idx1 = 0;
			idx = 1;	/* Mic In */
			break;
		case 0x10:
			idx1 = 3;
			idx = 1;	/* Mic In */
			break;
		case 0x11:
			idx1 = 4;
			idx = 0;	/* CD */
			break;
		default:
			continue;
		}

		err = new_analog_input(spec, cfg->input_pins[i],
				       auto_pin_cfg_labels[i], idx, 0x15);
		if (err < 0)
			return err;

		imux->items[imux->num_items].label = auto_pin_cfg_labels[i];
		imux->items[imux->num_items].index = idx1;
		imux->num_items++;
	}
	return 0;
}

static struct snd_kcontrol_new alc861_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),

	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 1,
		.info = alc_mux_enum_info,
		.get = alc_mux_enum_get,
		.put = alc_mux_enum_put,
	},
	{ } /* end */
};

static void alc861_auto_set_output_and_unmute(struct hda_codec *codec,
					      hda_nid_t nid,
					      int pin_type, int dac_idx)
{
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_PIN_WIDGET_CONTROL,
			    pin_type);
	snd_hda_codec_write(codec, dac_idx, 0, AC_VERB_SET_AMP_GAIN_MUTE,
			    AMP_OUT_UNMUTE);
}

static void alc861_auto_init_multi_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i;

	alc_subsystem_id(codec, 0x0e, 0x0f, 0x0b);
	for (i = 0; i < spec->autocfg.line_outs; i++) {
		hda_nid_t nid = spec->autocfg.line_out_pins[i];
		int pin_type = get_pin_type(spec->autocfg.line_out_type);
		if (nid)
			alc861_auto_set_output_and_unmute(codec, nid, pin_type,
							  spec->multiout.dac_nids[i]);
	}
}

static void alc861_auto_init_hp_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t pin;

	pin = spec->autocfg.hp_pins[0];
	if (pin) /* connect to front */
		alc861_auto_set_output_and_unmute(codec, pin, PIN_HP,
						  spec->multiout.dac_nids[0]);
	pin = spec->autocfg.speaker_pins[0];
	if (pin)
		alc861_auto_set_output_and_unmute(codec, pin, PIN_OUT, 0);
}

static void alc861_auto_init_analog_input(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		hda_nid_t nid = spec->autocfg.input_pins[i];
		if (nid >= 0x0c && nid <= 0x11) {
			snd_hda_codec_write(codec, nid, 0,
					    AC_VERB_SET_PIN_WIDGET_CONTROL,
					    i <= AUTO_PIN_FRONT_MIC ?
					    PIN_VREF80 : PIN_IN);
		}
	}
}

/* parse the BIOS configuration and set up the alc_spec */
/* return 1 if successful, 0 if the proper config is not found,
 * or a negative error code
 */
static int alc861_parse_auto_config(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int err;
	static hda_nid_t alc861_ignore[] = { 0x1d, 0 };

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg,
					   alc861_ignore);
	if (err < 0)
		return err;
	if (!spec->autocfg.line_outs)
		return 0; /* can't find valid BIOS pin config */

	err = alc861_auto_fill_dac_nids(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = alc861_auto_create_multi_out_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = alc861_auto_create_hp_ctls(spec, spec->autocfg.hp_pins[0]);
	if (err < 0)
		return err;
	err = alc861_auto_create_analog_input_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;

	if (spec->autocfg.dig_out_pin)
		spec->multiout.dig_out_nid = ALC861_DIGOUT_NID;

	if (spec->kctl_alloc)
		spec->mixers[spec->num_mixers++] = spec->kctl_alloc;

	spec->init_verbs[spec->num_init_verbs++] = alc861_auto_init_verbs;

	spec->num_mux_defs = 1;
	spec->input_mux = &spec->private_imux;

	spec->adc_nids = alc861_adc_nids;
	spec->num_adc_nids = ARRAY_SIZE(alc861_adc_nids);
	spec->mixers[spec->num_mixers] = alc861_capture_mixer;
	spec->num_mixers++;

	return 1;
}

/* additional initialization for auto-configuration model */
static void alc861_auto_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	alc861_auto_init_multi_out(codec);
	alc861_auto_init_hp_out(codec);
	alc861_auto_init_analog_input(codec);
	if (spec->unsol_event)
		alc_sku_automute(codec);
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
static struct hda_amp_list alc861_loopbacks[] = {
	{ 0x15, HDA_INPUT, 0 },
	{ 0x15, HDA_INPUT, 1 },
	{ 0x15, HDA_INPUT, 2 },
	{ 0x15, HDA_INPUT, 3 },
	{ } /* end */
};
#endif


/*
 * configuration and preset
 */
static const char *alc861_models[ALC861_MODEL_LAST] = {
	[ALC861_3ST]		= "3stack",
	[ALC660_3ST]		= "3stack-660",
	[ALC861_3ST_DIG]	= "3stack-dig",
	[ALC861_6ST_DIG]	= "6stack-dig",
	[ALC861_UNIWILL_M31]	= "uniwill-m31",
	[ALC861_TOSHIBA]	= "toshiba",
	[ALC861_ASUS]		= "asus",
	[ALC861_ASUS_LAPTOP]	= "asus-laptop",
	[ALC861_AUTO]		= "auto",
};

static struct snd_pci_quirk alc861_cfg_tbl[] = {
	SND_PCI_QUIRK(0x1043, 0x1205, "ASUS W7J", ALC861_3ST),
	SND_PCI_QUIRK(0x1043, 0x1335, "ASUS F2/3", ALC861_ASUS_LAPTOP),
	SND_PCI_QUIRK(0x1043, 0x1338, "ASUS F2/3", ALC861_ASUS_LAPTOP),
	SND_PCI_QUIRK(0x1043, 0x1393, "ASUS", ALC861_ASUS),
	SND_PCI_QUIRK(0x1043, 0x13d7, "ASUS A9rp", ALC861_ASUS_LAPTOP),
	SND_PCI_QUIRK(0x1043, 0x81cb, "ASUS P1-AH2", ALC861_3ST_DIG),
	SND_PCI_QUIRK(0x1179, 0xff00, "Toshiba", ALC861_TOSHIBA),
	/* FIXME: the entry below breaks Toshiba A100 (model=auto works!)
	 *        Any other models that need this preset?
	 */
	/* SND_PCI_QUIRK(0x1179, 0xff10, "Toshiba", ALC861_TOSHIBA), */
	SND_PCI_QUIRK(0x1462, 0x7254, "HP dx2200 (MSI MS-7254)", ALC861_3ST),
	SND_PCI_QUIRK(0x1462, 0x7297, "HP dx2250 (MSI MS-7297)", ALC861_3ST),
	SND_PCI_QUIRK(0x1584, 0x2b01, "Uniwill X40AIx", ALC861_UNIWILL_M31),
	SND_PCI_QUIRK(0x1584, 0x9072, "Uniwill m31", ALC861_UNIWILL_M31),
	SND_PCI_QUIRK(0x1584, 0x9075, "Airis Praxis N1212", ALC861_ASUS_LAPTOP),
	/* FIXME: the below seems conflict */
	/* SND_PCI_QUIRK(0x1584, 0x9075, "Uniwill", ALC861_UNIWILL_M31), */
	SND_PCI_QUIRK(0x1849, 0x0660, "Asrock 939SLI32", ALC660_3ST),
	SND_PCI_QUIRK(0x8086, 0xd600, "Intel", ALC861_3ST),
	{}
};

static struct alc_config_preset alc861_presets[] = {
	[ALC861_3ST] = {
		.mixers = { alc861_3ST_mixer },
		.init_verbs = { alc861_threestack_init_verbs },
		.num_dacs = ARRAY_SIZE(alc861_dac_nids),
		.dac_nids = alc861_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc861_threestack_modes),
		.channel_mode = alc861_threestack_modes,
		.need_dac_fix = 1,
		.num_adc_nids = ARRAY_SIZE(alc861_adc_nids),
		.adc_nids = alc861_adc_nids,
		.input_mux = &alc861_capture_source,
	},
	[ALC861_3ST_DIG] = {
		.mixers = { alc861_base_mixer },
		.init_verbs = { alc861_threestack_init_verbs },
		.num_dacs = ARRAY_SIZE(alc861_dac_nids),
		.dac_nids = alc861_dac_nids,
		.dig_out_nid = ALC861_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc861_threestack_modes),
		.channel_mode = alc861_threestack_modes,
		.need_dac_fix = 1,
		.num_adc_nids = ARRAY_SIZE(alc861_adc_nids),
		.adc_nids = alc861_adc_nids,
		.input_mux = &alc861_capture_source,
	},
	[ALC861_6ST_DIG] = {
		.mixers = { alc861_base_mixer },
		.init_verbs = { alc861_base_init_verbs },
		.num_dacs = ARRAY_SIZE(alc861_dac_nids),
		.dac_nids = alc861_dac_nids,
		.dig_out_nid = ALC861_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc861_8ch_modes),
		.channel_mode = alc861_8ch_modes,
		.num_adc_nids = ARRAY_SIZE(alc861_adc_nids),
		.adc_nids = alc861_adc_nids,
		.input_mux = &alc861_capture_source,
	},
	[ALC660_3ST] = {
		.mixers = { alc861_3ST_mixer },
		.init_verbs = { alc861_threestack_init_verbs },
		.num_dacs = ARRAY_SIZE(alc660_dac_nids),
		.dac_nids = alc660_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc861_threestack_modes),
		.channel_mode = alc861_threestack_modes,
		.need_dac_fix = 1,
		.num_adc_nids = ARRAY_SIZE(alc861_adc_nids),
		.adc_nids = alc861_adc_nids,
		.input_mux = &alc861_capture_source,
	},
	[ALC861_UNIWILL_M31] = {
		.mixers = { alc861_uniwill_m31_mixer },
		.init_verbs = { alc861_uniwill_m31_init_verbs },
		.num_dacs = ARRAY_SIZE(alc861_dac_nids),
		.dac_nids = alc861_dac_nids,
		.dig_out_nid = ALC861_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc861_uniwill_m31_modes),
		.channel_mode = alc861_uniwill_m31_modes,
		.need_dac_fix = 1,
		.num_adc_nids = ARRAY_SIZE(alc861_adc_nids),
		.adc_nids = alc861_adc_nids,
		.input_mux = &alc861_capture_source,
	},
	[ALC861_TOSHIBA] = {
		.mixers = { alc861_toshiba_mixer },
		.init_verbs = { alc861_base_init_verbs,
				alc861_toshiba_init_verbs },
		.num_dacs = ARRAY_SIZE(alc861_dac_nids),
		.dac_nids = alc861_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.num_adc_nids = ARRAY_SIZE(alc861_adc_nids),
		.adc_nids = alc861_adc_nids,
		.input_mux = &alc861_capture_source,
		.unsol_event = alc861_toshiba_unsol_event,
		.init_hook = alc861_toshiba_automute,
	},
	[ALC861_ASUS] = {
		.mixers = { alc861_asus_mixer },
		.init_verbs = { alc861_asus_init_verbs },
		.num_dacs = ARRAY_SIZE(alc861_dac_nids),
		.dac_nids = alc861_dac_nids,
		.dig_out_nid = ALC861_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc861_asus_modes),
		.channel_mode = alc861_asus_modes,
		.need_dac_fix = 1,
		.hp_nid = 0x06,
		.num_adc_nids = ARRAY_SIZE(alc861_adc_nids),
		.adc_nids = alc861_adc_nids,
		.input_mux = &alc861_capture_source,
	},
	[ALC861_ASUS_LAPTOP] = {
		.mixers = { alc861_toshiba_mixer, alc861_asus_laptop_mixer },
		.init_verbs = { alc861_asus_init_verbs,
				alc861_asus_laptop_init_verbs },
		.num_dacs = ARRAY_SIZE(alc861_dac_nids),
		.dac_nids = alc861_dac_nids,
		.dig_out_nid = ALC861_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.need_dac_fix = 1,
		.num_adc_nids = ARRAY_SIZE(alc861_adc_nids),
		.adc_nids = alc861_adc_nids,
		.input_mux = &alc861_capture_source,
	},
};


static int patch_alc861(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int board_config;
	int err;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

        board_config = snd_hda_check_board_config(codec, ALC861_MODEL_LAST,
						  alc861_models,
						  alc861_cfg_tbl);

	if (board_config < 0) {
		printk(KERN_INFO "hda_codec: Unknown model for ALC861, "
		       "trying auto-probe from BIOS...\n");
		board_config = ALC861_AUTO;
	}

	if (board_config == ALC861_AUTO) {
		/* automatic parse from the BIOS config */
		err = alc861_parse_auto_config(codec);
		if (err < 0) {
			alc_free(codec);
			return err;
		} else if (!err) {
			printk(KERN_INFO
			       "hda_codec: Cannot set up configuration "
			       "from BIOS.  Using base mode...\n");
		   board_config = ALC861_3ST_DIG;
		}
	}

	if (board_config != ALC861_AUTO)
		setup_preset(spec, &alc861_presets[board_config]);

	spec->stream_name_analog = "ALC861 Analog";
	spec->stream_analog_playback = &alc861_pcm_analog_playback;
	spec->stream_analog_capture = &alc861_pcm_analog_capture;

	spec->stream_name_digital = "ALC861 Digital";
	spec->stream_digital_playback = &alc861_pcm_digital_playback;
	spec->stream_digital_capture = &alc861_pcm_digital_capture;

	spec->vmaster_nid = 0x03;

	codec->patch_ops = alc_patch_ops;
	if (board_config == ALC861_AUTO)
		spec->init_hook = alc861_auto_init;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	if (!spec->loopback.amplist)
		spec->loopback.amplist = alc861_loopbacks;
#endif
		
	return 0;
	static const hda_nid_t alc269_ignore[] = { 0x1d, 0 };
	static const hda_nid_t alc269_ssids[] = { 0, 0x1b, 0x14, 0x21 };
	static const hda_nid_t alc269va_ssids[] = { 0x15, 0x1b, 0x14, 0 };
	struct alc_spec *spec = codec->spec;
	const hda_nid_t *ssids;

	switch (spec->codec_variant) {
	case ALC269_TYPE_ALC269VA:
	case ALC269_TYPE_ALC269VC:
	case ALC269_TYPE_ALC280:
	case ALC269_TYPE_ALC284:
	case ALC269_TYPE_ALC285:
		ssids = alc269va_ssids;
		break;
	case ALC269_TYPE_ALC269VB:
	case ALC269_TYPE_ALC269VD:
	case ALC269_TYPE_ALC282:
	case ALC269_TYPE_ALC283:
	case ALC269_TYPE_ALC286:
	case ALC269_TYPE_ALC298:
	case ALC269_TYPE_ALC255:
	case ALC269_TYPE_ALC256:
	case ALC269_TYPE_ALC225:
	case ALC269_TYPE_ALC294:
	case ALC269_TYPE_ALC700:
		ssids = alc269_ssids;
		break;
	default:
		ssids = alc269_ssids;
		break;
	}

	return alc_parse_auto_config(codec, alc269_ignore, ssids);
}

static int find_ext_mic_pin(struct hda_codec *codec);

static void alc286_shutup(struct hda_codec *codec)
{
	int i;
	int mic_pin = find_ext_mic_pin(codec);
	/* don't shut up pins when unloading the driver; otherwise it breaks
	 * the default pin setup at the next load of the driver
	 */
	if (codec->bus->shutdown)
		return;
	for (i = 0; i < codec->init_pins.used; i++) {
		struct hda_pincfg *pin = snd_array_elem(&codec->init_pins, i);
		/* use read here for syncing after issuing each verb */
		if (pin->nid != mic_pin)
			snd_hda_codec_read(codec, pin->nid, 0,
					AC_VERB_SET_PIN_WIDGET_CONTROL, 0);
	}
	codec->pins_shutup = 1;
}

static void alc269vb_toggle_power_output(struct hda_codec *codec, int power_up)
{
	alc_update_coef_idx(codec, 0x04, 1 << 11, power_up ? (1 << 11) : 0);
}

static void alc269_shutup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	if (spec->codec_variant == ALC269_TYPE_ALC269VB)
		alc269vb_toggle_power_output(codec, 0);
	if (spec->codec_variant == ALC269_TYPE_ALC269VB &&
			(alc_get_coef0(codec) & 0x00ff) == 0x018) {
		msleep(150);
	}
	snd_hda_shutup_pins(codec);
}

static struct coef_fw alc282_coefs[] = {
	WRITE_COEF(0x03, 0x0002), /* Power Down Control */
	UPDATE_COEF(0x05, 0xff3f, 0x0700), /* FIFO and filter clock */
	WRITE_COEF(0x07, 0x0200), /* DMIC control */
	UPDATE_COEF(0x06, 0x00f0, 0), /* Analog clock */
	UPDATE_COEF(0x08, 0xfffc, 0x0c2c), /* JD */
	WRITE_COEF(0x0a, 0xcccc), /* JD offset1 */
	WRITE_COEF(0x0b, 0xcccc), /* JD offset2 */
	WRITE_COEF(0x0e, 0x6e00), /* LDO1/2/3, DAC/ADC */
	UPDATE_COEF(0x0f, 0xf800, 0x1000), /* JD */
	UPDATE_COEF(0x10, 0xfc00, 0x0c00), /* Capless */
	WRITE_COEF(0x6f, 0x0), /* Class D test 4 */
	UPDATE_COEF(0x0c, 0xfe00, 0), /* IO power down directly */
	WRITE_COEF(0x34, 0xa0c0), /* ANC */
	UPDATE_COEF(0x16, 0x0008, 0), /* AGC MUX */
	UPDATE_COEF(0x1d, 0x00e0, 0), /* DAC simple content protection */
	UPDATE_COEF(0x1f, 0x00e0, 0), /* ADC simple content protection */
	WRITE_COEF(0x21, 0x8804), /* DAC ADC Zero Detection */
	WRITE_COEF(0x63, 0x2902), /* PLL */
	WRITE_COEF(0x68, 0xa080), /* capless control 2 */
	WRITE_COEF(0x69, 0x3400), /* capless control 3 */
	WRITE_COEF(0x6a, 0x2f3e), /* capless control 4 */
	WRITE_COEF(0x6b, 0x0), /* capless control 5 */
	UPDATE_COEF(0x6d, 0x0fff, 0x0900), /* class D test 2 */
	WRITE_COEF(0x6e, 0x110a), /* class D test 3 */
	UPDATE_COEF(0x70, 0x00f8, 0x00d8), /* class D test 5 */
	WRITE_COEF(0x71, 0x0014), /* class D test 6 */
	WRITE_COEF(0x72, 0xc2ba), /* classD OCP */
	UPDATE_COEF(0x77, 0x0f80, 0), /* classD pure DC test */
	WRITE_COEF(0x6c, 0xfc06), /* Class D amp control */
	{}
};

static void alc282_restore_default_value(struct hda_codec *codec)
{
	alc_process_coef_fw(codec, alc282_coefs);
}

static void alc282_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t hp_pin = spec->gen.autocfg.hp_pins[0];
	bool hp_pin_sense;
	int coef78;

	alc282_restore_default_value(codec);

	if (!hp_pin)
		return;
	hp_pin_sense = snd_hda_jack_detect(codec, hp_pin);
	coef78 = alc_read_coef_idx(codec, 0x78);

	/* Index 0x78 Direct Drive HP AMP LPM Control 1 */
	/* Headphone capless set to high power mode */
	alc_write_coef_idx(codec, 0x78, 0x9004);

	if (hp_pin_sense)
		msleep(2);

	snd_hda_codec_write(codec, hp_pin, 0,
			    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE);

	if (hp_pin_sense)
		msleep(85);

	snd_hda_codec_write(codec, hp_pin, 0,
			    AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT);

	if (hp_pin_sense)
		msleep(100);

	/* Headphone capless set to normal mode */
	alc_write_coef_idx(codec, 0x78, coef78);
}

static void alc282_shutup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t hp_pin = spec->gen.autocfg.hp_pins[0];
	bool hp_pin_sense;
	int coef78;

	if (!hp_pin) {
		alc269_shutup(codec);
		return;
	}

	hp_pin_sense = snd_hda_jack_detect(codec, hp_pin);
	coef78 = alc_read_coef_idx(codec, 0x78);
	alc_write_coef_idx(codec, 0x78, 0x9004);

	if (hp_pin_sense)
		msleep(2);

	snd_hda_codec_write(codec, hp_pin, 0,
			    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE);

	if (hp_pin_sense)
		msleep(85);

	snd_hda_codec_write(codec, hp_pin, 0,
			    AC_VERB_SET_PIN_WIDGET_CONTROL, 0x0);

	if (hp_pin_sense)
		msleep(100);

	alc_auto_setup_eapd(codec, false);
	snd_hda_shutup_pins(codec);
	alc_write_coef_idx(codec, 0x78, coef78);
}

static struct coef_fw alc283_coefs[] = {
	WRITE_COEF(0x03, 0x0002), /* Power Down Control */
	UPDATE_COEF(0x05, 0xff3f, 0x0700), /* FIFO and filter clock */
	WRITE_COEF(0x07, 0x0200), /* DMIC control */
	UPDATE_COEF(0x06, 0x00f0, 0), /* Analog clock */
	UPDATE_COEF(0x08, 0xfffc, 0x0c2c), /* JD */
	WRITE_COEF(0x0a, 0xcccc), /* JD offset1 */
	WRITE_COEF(0x0b, 0xcccc), /* JD offset2 */
	WRITE_COEF(0x0e, 0x6fc0), /* LDO1/2/3, DAC/ADC */
	UPDATE_COEF(0x0f, 0xf800, 0x1000), /* JD */
	UPDATE_COEF(0x10, 0xfc00, 0x0c00), /* Capless */
	WRITE_COEF(0x3a, 0x0), /* Class D test 4 */
	UPDATE_COEF(0x0c, 0xfe00, 0x0), /* IO power down directly */
	WRITE_COEF(0x22, 0xa0c0), /* ANC */
	UPDATE_COEFEX(0x53, 0x01, 0x000f, 0x0008), /* AGC MUX */
	UPDATE_COEF(0x1d, 0x00e0, 0), /* DAC simple content protection */
	UPDATE_COEF(0x1f, 0x00e0, 0), /* ADC simple content protection */
	WRITE_COEF(0x21, 0x8804), /* DAC ADC Zero Detection */
	WRITE_COEF(0x2e, 0x2902), /* PLL */
	WRITE_COEF(0x33, 0xa080), /* capless control 2 */
	WRITE_COEF(0x34, 0x3400), /* capless control 3 */
	WRITE_COEF(0x35, 0x2f3e), /* capless control 4 */
	WRITE_COEF(0x36, 0x0), /* capless control 5 */
	UPDATE_COEF(0x38, 0x0fff, 0x0900), /* class D test 2 */
	WRITE_COEF(0x39, 0x110a), /* class D test 3 */
	UPDATE_COEF(0x3b, 0x00f8, 0x00d8), /* class D test 5 */
	WRITE_COEF(0x3c, 0x0014), /* class D test 6 */
	WRITE_COEF(0x3d, 0xc2ba), /* classD OCP */
	UPDATE_COEF(0x42, 0x0f80, 0x0), /* classD pure DC test */
	WRITE_COEF(0x49, 0x0), /* test mode */
	UPDATE_COEF(0x40, 0xf800, 0x9800), /* Class D DC enable */
	UPDATE_COEF(0x42, 0xf000, 0x2000), /* DC offset */
	WRITE_COEF(0x37, 0xfc06), /* Class D amp control */
	UPDATE_COEF(0x1b, 0x8000, 0), /* HP JD control */
	{}
};

static void alc283_restore_default_value(struct hda_codec *codec)
{
	alc_process_coef_fw(codec, alc283_coefs);
}

static void alc283_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t hp_pin = spec->gen.autocfg.hp_pins[0];
	bool hp_pin_sense;

	if (!spec->gen.autocfg.hp_outs) {
		if (spec->gen.autocfg.line_out_type == AC_JACK_HP_OUT)
			hp_pin = spec->gen.autocfg.line_out_pins[0];
	}

	alc283_restore_default_value(codec);

	if (!hp_pin)
		return;

	msleep(30);
	hp_pin_sense = snd_hda_jack_detect(codec, hp_pin);

	/* Index 0x43 Direct Drive HP AMP LPM Control 1 */
	/* Headphone capless set to high power mode */
	alc_write_coef_idx(codec, 0x43, 0x9004);

	snd_hda_codec_write(codec, hp_pin, 0,
			    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE);

	if (hp_pin_sense)
		msleep(85);

	snd_hda_codec_write(codec, hp_pin, 0,
			    AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT);

	if (hp_pin_sense)
		msleep(85);
	/* Index 0x46 Combo jack auto switch control 2 */
	/* 3k pull low control for Headset jack. */
	alc_update_coef_idx(codec, 0x46, 3 << 12, 0);
	/* Headphone capless set to normal mode */
	alc_write_coef_idx(codec, 0x43, 0x9614);
}

static void alc283_shutup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t hp_pin = spec->gen.autocfg.hp_pins[0];
	bool hp_pin_sense;

	if (!spec->gen.autocfg.hp_outs) {
		if (spec->gen.autocfg.line_out_type == AC_JACK_HP_OUT)
			hp_pin = spec->gen.autocfg.line_out_pins[0];
	}

	if (!hp_pin) {
		alc269_shutup(codec);
		return;
	}

	hp_pin_sense = snd_hda_jack_detect(codec, hp_pin);

	alc_write_coef_idx(codec, 0x43, 0x9004);

	/*depop hp during suspend*/
	alc_write_coef_idx(codec, 0x06, 0x2100);

	snd_hda_codec_write(codec, hp_pin, 0,
			    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE);

	if (hp_pin_sense)
		msleep(100);

	snd_hda_codec_write(codec, hp_pin, 0,
			    AC_VERB_SET_PIN_WIDGET_CONTROL, 0x0);

	alc_update_coef_idx(codec, 0x46, 0, 3 << 12);

	if (hp_pin_sense)
		msleep(100);
	alc_auto_setup_eapd(codec, false);
	snd_hda_shutup_pins(codec);
	alc_write_coef_idx(codec, 0x43, 0x9614);
}

static void alc5505_coef_set(struct hda_codec *codec, unsigned int index_reg,
			     unsigned int val)
{
	snd_hda_codec_write(codec, 0x51, 0, AC_VERB_SET_COEF_INDEX, index_reg >> 1);
	snd_hda_codec_write(codec, 0x51, 0, AC_VERB_SET_PROC_COEF, val & 0xffff); /* LSB */
	snd_hda_codec_write(codec, 0x51, 0, AC_VERB_SET_PROC_COEF, val >> 16); /* MSB */
}

static int alc5505_coef_get(struct hda_codec *codec, unsigned int index_reg)
{
	unsigned int val;

	snd_hda_codec_write(codec, 0x51, 0, AC_VERB_SET_COEF_INDEX, index_reg >> 1);
	val = snd_hda_codec_read(codec, 0x51, 0, AC_VERB_GET_PROC_COEF, 0)
		& 0xffff;
	val |= snd_hda_codec_read(codec, 0x51, 0, AC_VERB_GET_PROC_COEF, 0)
		<< 16;
	return val;
}

static void alc5505_dsp_halt(struct hda_codec *codec)
{
	unsigned int val;

	alc5505_coef_set(codec, 0x3000, 0x000c); /* DSP CPU stop */
	alc5505_coef_set(codec, 0x880c, 0x0008); /* DDR enter self refresh */
	alc5505_coef_set(codec, 0x61c0, 0x11110080); /* Clock control for PLL and CPU */
	alc5505_coef_set(codec, 0x6230, 0xfc0d4011); /* Disable Input OP */
	alc5505_coef_set(codec, 0x61b4, 0x040a2b03); /* Stop PLL2 */
	alc5505_coef_set(codec, 0x61b0, 0x00005b17); /* Stop PLL1 */
	alc5505_coef_set(codec, 0x61b8, 0x04133303); /* Stop PLL3 */
	val = alc5505_coef_get(codec, 0x6220);
	alc5505_coef_set(codec, 0x6220, (val | 0x3000)); /* switch Ringbuffer clock to DBUS clock */
}

static void alc5505_dsp_back_from_halt(struct hda_codec *codec)
{
	alc5505_coef_set(codec, 0x61b8, 0x04133302);
	alc5505_coef_set(codec, 0x61b0, 0x00005b16);
	alc5505_coef_set(codec, 0x61b4, 0x040a2b02);
	alc5505_coef_set(codec, 0x6230, 0xf80d4011);
	alc5505_coef_set(codec, 0x6220, 0x2002010f);
	alc5505_coef_set(codec, 0x880c, 0x00000004);
}

static void alc5505_dsp_init(struct hda_codec *codec)
{
	unsigned int val;

	alc5505_dsp_halt(codec);
	alc5505_dsp_back_from_halt(codec);
	alc5505_coef_set(codec, 0x61b0, 0x5b14); /* PLL1 control */
	alc5505_coef_set(codec, 0x61b0, 0x5b16);
	alc5505_coef_set(codec, 0x61b4, 0x04132b00); /* PLL2 control */
	alc5505_coef_set(codec, 0x61b4, 0x04132b02);
	alc5505_coef_set(codec, 0x61b8, 0x041f3300); /* PLL3 control*/
	alc5505_coef_set(codec, 0x61b8, 0x041f3302);
	snd_hda_codec_write(codec, 0x51, 0, AC_VERB_SET_CODEC_RESET, 0); /* Function reset */
	alc5505_coef_set(codec, 0x61b8, 0x041b3302);
	alc5505_coef_set(codec, 0x61b8, 0x04173302);
	alc5505_coef_set(codec, 0x61b8, 0x04163302);
	alc5505_coef_set(codec, 0x8800, 0x348b328b); /* DRAM control */
	alc5505_coef_set(codec, 0x8808, 0x00020022); /* DRAM control */
	alc5505_coef_set(codec, 0x8818, 0x00000400); /* DRAM control */

	val = alc5505_coef_get(codec, 0x6200) >> 16; /* Read revision ID */
	if (val <= 3)
		alc5505_coef_set(codec, 0x6220, 0x2002010f); /* I/O PAD Configuration */
	else
		alc5505_coef_set(codec, 0x6220, 0x6002018f);

	alc5505_coef_set(codec, 0x61ac, 0x055525f0); /**/
	alc5505_coef_set(codec, 0x61c0, 0x12230080); /* Clock control */
	alc5505_coef_set(codec, 0x61b4, 0x040e2b02); /* PLL2 control */
	alc5505_coef_set(codec, 0x61bc, 0x010234f8); /* OSC Control */
	alc5505_coef_set(codec, 0x880c, 0x00000004); /* DRAM Function control */
	alc5505_coef_set(codec, 0x880c, 0x00000003);
	alc5505_coef_set(codec, 0x880c, 0x00000010);

#ifdef HALT_REALTEK_ALC5505
	alc5505_dsp_halt(codec);
#endif
}

#ifdef HALT_REALTEK_ALC5505
#define alc5505_dsp_suspend(codec)	/* NOP */
#define alc5505_dsp_resume(codec)	/* NOP */
#else
#define alc5505_dsp_suspend(codec)	alc5505_dsp_halt(codec)
#define alc5505_dsp_resume(codec)	alc5505_dsp_back_from_halt(codec)
#endif

#ifdef CONFIG_PM
static int alc269_suspend(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	if (spec->has_alc5505_dsp)
		alc5505_dsp_suspend(codec);
	return alc_suspend(codec);
}

static int alc269_resume(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	if (spec->codec_variant == ALC269_TYPE_ALC269VB)
		alc269vb_toggle_power_output(codec, 0);
	if (spec->codec_variant == ALC269_TYPE_ALC269VB &&
			(alc_get_coef0(codec) & 0x00ff) == 0x018) {
		msleep(150);
	}

	codec->patch_ops.init(codec);

	if (spec->codec_variant == ALC269_TYPE_ALC269VB)
		alc269vb_toggle_power_output(codec, 1);
	if (spec->codec_variant == ALC269_TYPE_ALC269VB &&
			(alc_get_coef0(codec) & 0x00ff) == 0x017) {
		msleep(200);
	}

	regcache_sync(codec->core.regmap);
	hda_call_check_power_status(codec, 0x01);

	/* on some machine, the BIOS will clear the codec gpio data when enter
	 * suspend, and won't restore the data after resume, so we restore it
	 * in the driver.
	 */
	if (spec->gpio_led)
		snd_hda_codec_write(codec, codec->core.afg, 0, AC_VERB_SET_GPIO_DATA,
			    spec->gpio_led);

	if (spec->has_alc5505_dsp)
		alc5505_dsp_resume(codec);

	return 0;
}
#endif /* CONFIG_PM */

static void alc269_fixup_pincfg_no_hp_to_lineout(struct hda_codec *codec,
						 const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		spec->parse_flags = HDA_PINCFG_NO_HP_FIXUP;
}

static void alc269_fixup_pincfg_U7x7_headset_mic(struct hda_codec *codec,
						 const struct hda_fixup *fix,
						 int action)
{
	unsigned int cfg_headphone = snd_hda_codec_get_pincfg(codec, 0x21);
	unsigned int cfg_headset_mic = snd_hda_codec_get_pincfg(codec, 0x19);

	if (cfg_headphone && cfg_headset_mic == 0x411111f0)
		snd_hda_codec_set_pincfg(codec, 0x19,
			(cfg_headphone & ~AC_DEFCFG_DEVICE) |
			(AC_JACK_MIC_IN << AC_DEFCFG_DEVICE_SHIFT));
}

static void alc269_fixup_hweq(struct hda_codec *codec,
			       const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_INIT)
		alc_update_coef_idx(codec, 0x1e, 0, 0x80);
}

static void alc269_fixup_headset_mic(struct hda_codec *codec,
				       const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		spec->parse_flags |= HDA_PINCFG_HEADSET_MIC;
}

static void alc271_fixup_dmic(struct hda_codec *codec,
			      const struct hda_fixup *fix, int action)
{
	static const struct hda_verb verbs[] = {
		{0x20, AC_VERB_SET_COEF_INDEX, 0x0d},
		{0x20, AC_VERB_SET_PROC_COEF, 0x4000},
		{}
	};
	unsigned int cfg;

	if (strcmp(codec->core.chip_name, "ALC271X") &&
	    strcmp(codec->core.chip_name, "ALC269VB"))
		return;
	cfg = snd_hda_codec_get_pincfg(codec, 0x12);
	if (get_defcfg_connect(cfg) == AC_JACK_PORT_FIXED)
		snd_hda_sequence_write(codec, verbs);
}

static void alc269_fixup_pcm_44k(struct hda_codec *codec,
				 const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	if (action != HDA_FIXUP_ACT_PROBE)
		return;

	/* Due to a hardware problem on Lenovo Ideadpad, we need to
	 * fix the sample rate of analog I/O to 44.1kHz
	 */
	spec->gen.stream_analog_playback = &alc269_44k_pcm_analog_playback;
	spec->gen.stream_analog_capture = &alc269_44k_pcm_analog_capture;
}

static void alc269_fixup_stereo_dmic(struct hda_codec *codec,
				     const struct hda_fixup *fix, int action)
{
	/* The digital-mic unit sends PDM (differential signal) instead of
	 * the standard PCM, thus you can't record a valid mono stream as is.
	 * Below is a workaround specific to ALC269 to control the dmic
	 * signal source as mono.
	 */
	if (action == HDA_FIXUP_ACT_INIT)
		alc_update_coef_idx(codec, 0x07, 0, 0x80);
}

static void alc269_quanta_automute(struct hda_codec *codec)
{
	snd_hda_gen_update_outputs(codec);

	alc_write_coef_idx(codec, 0x0c, 0x680);
	alc_write_coef_idx(codec, 0x0c, 0x480);
}

static void alc269_fixup_quanta_mute(struct hda_codec *codec,
				     const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	if (action != HDA_FIXUP_ACT_PROBE)
		return;
	spec->gen.automute_hook = alc269_quanta_automute;
}

static void alc269_x101_hp_automute_hook(struct hda_codec *codec,
					 struct hda_jack_callback *jack)
{
	struct alc_spec *spec = codec->spec;
	int vref;
	msleep(200);
	snd_hda_gen_hp_automute(codec, jack);

	vref = spec->gen.hp_jack_present ? PIN_VREF80 : 0;
	msleep(100);
	snd_hda_codec_write(codec, 0x18, 0, AC_VERB_SET_PIN_WIDGET_CONTROL,
			    vref);
	msleep(500);
	snd_hda_codec_write(codec, 0x18, 0, AC_VERB_SET_PIN_WIDGET_CONTROL,
			    vref);
}

static void alc269_fixup_x101_headset_mic(struct hda_codec *codec,
				     const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->parse_flags |= HDA_PINCFG_HEADSET_MIC;
		spec->gen.hp_automute_hook = alc269_x101_hp_automute_hook;
	}
}


/* update mute-LED according to the speaker mute state via mic VREF pin */
static void alc269_fixup_mic_mute_hook(void *private_data, int enabled)
{
	struct hda_codec *codec = private_data;
	struct alc_spec *spec = codec->spec;
	unsigned int pinval;

	if (spec->mute_led_polarity)
		enabled = !enabled;
	pinval = snd_hda_codec_get_pin_target(codec, spec->mute_led_nid);
	pinval &= ~AC_PINCTL_VREFEN;
	pinval |= enabled ? AC_PINCTL_VREF_HIZ : AC_PINCTL_VREF_80;
	if (spec->mute_led_nid) {
		/* temporarily power up/down for setting VREF */
		snd_hda_power_up_pm(codec);
		snd_hda_set_pin_ctl_cache(codec, spec->mute_led_nid, pinval);
		snd_hda_power_down_pm(codec);
	}
}

/* Make sure the led works even in runtime suspend */
static unsigned int led_power_filter(struct hda_codec *codec,
						  hda_nid_t nid,
						  unsigned int power_state)
{
	struct alc_spec *spec = codec->spec;

	if (power_state != AC_PWRST_D3 || nid == 0 ||
	    (nid != spec->mute_led_nid && nid != spec->cap_mute_led_nid))
		return power_state;

	/* Set pin ctl again, it might have just been set to 0 */
	snd_hda_set_pin_ctl(codec, nid,
			    snd_hda_codec_get_pin_target(codec, nid));

	return snd_hda_gen_path_power_filter(codec, nid, power_state);
}

static void alc269_fixup_hp_mute_led(struct hda_codec *codec,
				     const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	const struct dmi_device *dev = NULL;

	if (action != HDA_FIXUP_ACT_PRE_PROBE)
		return;

	while ((dev = dmi_find_device(DMI_DEV_TYPE_OEM_STRING, NULL, dev))) {
		int pol, pin;
		if (sscanf(dev->name, "HP_Mute_LED_%d_%x", &pol, &pin) != 2)
			continue;
		if (pin < 0x0a || pin >= 0x10)
			break;
		spec->mute_led_polarity = pol;
		spec->mute_led_nid = pin - 0x0a + 0x18;
		spec->gen.vmaster_mute.hook = alc269_fixup_mic_mute_hook;
		spec->gen.vmaster_mute_enum = 1;
		codec->power_filter = led_power_filter;
		codec_dbg(codec,
			  "Detected mute LED for %x:%d\n", spec->mute_led_nid,
			   spec->mute_led_polarity);
		break;
	}
}

static void alc269_fixup_hp_mute_led_mic1(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->mute_led_polarity = 0;
		spec->mute_led_nid = 0x18;
		spec->gen.vmaster_mute.hook = alc269_fixup_mic_mute_hook;
		spec->gen.vmaster_mute_enum = 1;
		codec->power_filter = led_power_filter;
	}
}

static void alc269_fixup_hp_mute_led_mic2(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->mute_led_polarity = 0;
		spec->mute_led_nid = 0x19;
		spec->gen.vmaster_mute.hook = alc269_fixup_mic_mute_hook;
		spec->gen.vmaster_mute_enum = 1;
		codec->power_filter = led_power_filter;
	}
}

/* update LED status via GPIO */
static void alc_update_gpio_led(struct hda_codec *codec, unsigned int mask,
				bool enabled)
{
	struct alc_spec *spec = codec->spec;
	unsigned int oldval = spec->gpio_led;

	if (spec->mute_led_polarity)
		enabled = !enabled;

	if (enabled)
		spec->gpio_led &= ~mask;
	else
		spec->gpio_led |= mask;
	if (spec->gpio_led != oldval)
		snd_hda_codec_write(codec, 0x01, 0, AC_VERB_SET_GPIO_DATA,
				    spec->gpio_led);
}

/* turn on/off mute LED via GPIO per vmaster hook */
static void alc_fixup_gpio_mute_hook(void *private_data, int enabled)
{
	struct hda_codec *codec = private_data;
	struct alc_spec *spec = codec->spec;

	alc_update_gpio_led(codec, spec->gpio_mute_led_mask, enabled);
}

/* turn on/off mic-mute LED via GPIO per capture hook */
static void alc_fixup_gpio_mic_mute_hook(struct hda_codec *codec,
					 struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct alc_spec *spec = codec->spec;

	if (ucontrol)
		alc_update_gpio_led(codec, spec->gpio_mic_led_mask,
				    ucontrol->value.integer.value[0] ||
				    ucontrol->value.integer.value[1]);
}

static void alc269_fixup_hp_gpio_led(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	static const struct hda_verb gpio_init[] = {
		{ 0x01, AC_VERB_SET_GPIO_MASK, 0x18 },
		{ 0x01, AC_VERB_SET_GPIO_DIRECTION, 0x18 },
		{}
	};

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->gen.vmaster_mute.hook = alc_fixup_gpio_mute_hook;
		spec->gen.cap_sync_hook = alc_fixup_gpio_mic_mute_hook;
		spec->gpio_led = 0;
		spec->mute_led_polarity = 0;
		spec->gpio_mute_led_mask = 0x08;
		spec->gpio_mic_led_mask = 0x10;
		snd_hda_add_verbs(codec, gpio_init);
	}
}

static void alc286_fixup_hp_gpio_led(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	static const struct hda_verb gpio_init[] = {
		{ 0x01, AC_VERB_SET_GPIO_MASK, 0x22 },
		{ 0x01, AC_VERB_SET_GPIO_DIRECTION, 0x22 },
		{}
	};

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->gen.vmaster_mute.hook = alc_fixup_gpio_mute_hook;
		spec->gen.cap_sync_hook = alc_fixup_gpio_mic_mute_hook;
		spec->gpio_led = 0;
		spec->mute_led_polarity = 0;
		spec->gpio_mute_led_mask = 0x02;
		spec->gpio_mic_led_mask = 0x20;
		snd_hda_add_verbs(codec, gpio_init);
	}
}

/* turn on/off mic-mute LED per capture hook */
static void alc269_fixup_hp_cap_mic_mute_hook(struct hda_codec *codec,
					       struct snd_kcontrol *kcontrol,
					       struct snd_ctl_elem_value *ucontrol)
{
	struct alc_spec *spec = codec->spec;
	unsigned int pinval, enable, disable;

	pinval = snd_hda_codec_get_pin_target(codec, spec->cap_mute_led_nid);
	pinval &= ~AC_PINCTL_VREFEN;
	enable  = pinval | AC_PINCTL_VREF_80;
	disable = pinval | AC_PINCTL_VREF_HIZ;

	if (!ucontrol)
		return;

	if (ucontrol->value.integer.value[0] ||
	    ucontrol->value.integer.value[1])
		pinval = disable;
	else
		pinval = enable;

	if (spec->cap_mute_led_nid)
		snd_hda_set_pin_ctl_cache(codec, spec->cap_mute_led_nid, pinval);
}

static void alc269_fixup_hp_gpio_mic1_led(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	static const struct hda_verb gpio_init[] = {
		{ 0x01, AC_VERB_SET_GPIO_MASK, 0x08 },
		{ 0x01, AC_VERB_SET_GPIO_DIRECTION, 0x08 },
		{}
	};

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->gen.vmaster_mute.hook = alc_fixup_gpio_mute_hook;
		spec->gen.cap_sync_hook = alc269_fixup_hp_cap_mic_mute_hook;
		spec->gpio_led = 0;
		spec->mute_led_polarity = 0;
		spec->gpio_mute_led_mask = 0x08;
		spec->cap_mute_led_nid = 0x18;
		snd_hda_add_verbs(codec, gpio_init);
		codec->power_filter = led_power_filter;
	}
}

static void alc280_fixup_hp_gpio4(struct hda_codec *codec,
				   const struct hda_fixup *fix, int action)
{
	/* Like hp_gpio_mic1_led, but also needs GPIO4 low to enable headphone amp */
	struct alc_spec *spec = codec->spec;
	static const struct hda_verb gpio_init[] = {
		{ 0x01, AC_VERB_SET_GPIO_MASK, 0x18 },
		{ 0x01, AC_VERB_SET_GPIO_DIRECTION, 0x18 },
		{}
	};

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->gen.vmaster_mute.hook = alc_fixup_gpio_mute_hook;
		spec->gen.cap_sync_hook = alc269_fixup_hp_cap_mic_mute_hook;
		spec->gpio_led = 0;
		spec->mute_led_polarity = 0;
		spec->gpio_mute_led_mask = 0x08;
		spec->cap_mute_led_nid = 0x18;
		snd_hda_add_verbs(codec, gpio_init);
		codec->power_filter = led_power_filter;
	}
}

#if IS_REACHABLE(CONFIG_INPUT)
static void gpio2_mic_hotkey_event(struct hda_codec *codec,
				   struct hda_jack_callback *event)
{
	struct alc_spec *spec = codec->spec;

	/* GPIO2 just toggles on a keypress/keyrelease cycle. Therefore
	   send both key on and key off event for every interrupt. */
	input_report_key(spec->kb_dev, spec->alc_mute_keycode_map[ALC_KEY_MICMUTE_INDEX], 1);
	input_sync(spec->kb_dev);
	input_report_key(spec->kb_dev, spec->alc_mute_keycode_map[ALC_KEY_MICMUTE_INDEX], 0);
	input_sync(spec->kb_dev);
}

static int alc_register_micmute_input_device(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i;

	spec->kb_dev = input_allocate_device();
	if (!spec->kb_dev) {
		codec_err(codec, "Out of memory (input_allocate_device)\n");
		return -ENOMEM;
	}

	spec->alc_mute_keycode_map[ALC_KEY_MICMUTE_INDEX] = KEY_MICMUTE;

	spec->kb_dev->name = "Microphone Mute Button";
	spec->kb_dev->evbit[0] = BIT_MASK(EV_KEY);
	spec->kb_dev->keycodesize = sizeof(spec->alc_mute_keycode_map[0]);
	spec->kb_dev->keycodemax = ARRAY_SIZE(spec->alc_mute_keycode_map);
	spec->kb_dev->keycode = spec->alc_mute_keycode_map;
	for (i = 0; i < ARRAY_SIZE(spec->alc_mute_keycode_map); i++)
		set_bit(spec->alc_mute_keycode_map[i], spec->kb_dev->keybit);

	if (input_register_device(spec->kb_dev)) {
		codec_err(codec, "input_register_device failed\n");
		input_free_device(spec->kb_dev);
		spec->kb_dev = NULL;
		return -ENOMEM;
	}

	return 0;
}

static void alc280_fixup_hp_gpio2_mic_hotkey(struct hda_codec *codec,
					     const struct hda_fixup *fix, int action)
{
	/* GPIO1 = set according to SKU external amp
	   GPIO2 = mic mute hotkey
	   GPIO3 = mute LED
	   GPIO4 = mic mute LED */
	static const struct hda_verb gpio_init[] = {
		{ 0x01, AC_VERB_SET_GPIO_MASK, 0x1e },
		{ 0x01, AC_VERB_SET_GPIO_DIRECTION, 0x1a },
		{ 0x01, AC_VERB_SET_GPIO_DATA, 0x02 },
		{}
	};

	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		if (alc_register_micmute_input_device(codec) != 0)
			return;

		snd_hda_add_verbs(codec, gpio_init);
		snd_hda_codec_write_cache(codec, codec->core.afg, 0,
					  AC_VERB_SET_GPIO_UNSOLICITED_RSP_MASK, 0x04);
		snd_hda_jack_detect_enable_callback(codec, codec->core.afg,
						    gpio2_mic_hotkey_event);

		spec->gen.vmaster_mute.hook = alc_fixup_gpio_mute_hook;
		spec->gen.cap_sync_hook = alc_fixup_gpio_mic_mute_hook;
		spec->gpio_led = 0;
		spec->mute_led_polarity = 0;
		spec->gpio_mute_led_mask = 0x08;
		spec->gpio_mic_led_mask = 0x10;
		return;
	}

	if (!spec->kb_dev)
		return;

	switch (action) {
	case HDA_FIXUP_ACT_PROBE:
		spec->init_amp = ALC_INIT_DEFAULT;
		break;
	case HDA_FIXUP_ACT_FREE:
		input_unregister_device(spec->kb_dev);
		spec->kb_dev = NULL;
	}
}

static void alc233_fixup_lenovo_line2_mic_hotkey(struct hda_codec *codec,
					     const struct hda_fixup *fix, int action)
{
	/* Line2 = mic mute hotkey
	   GPIO2 = mic mute LED */
	static const struct hda_verb gpio_init[] = {
		{ 0x01, AC_VERB_SET_GPIO_MASK, 0x04 },
		{ 0x01, AC_VERB_SET_GPIO_DIRECTION, 0x04 },
		{}
	};

	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		if (alc_register_micmute_input_device(codec) != 0)
			return;

		snd_hda_add_verbs(codec, gpio_init);
		snd_hda_jack_detect_enable_callback(codec, 0x1b,
						    gpio2_mic_hotkey_event);

		spec->gen.cap_sync_hook = alc_fixup_gpio_mic_mute_hook;
		spec->gpio_led = 0;
		spec->mute_led_polarity = 0;
		spec->gpio_mic_led_mask = 0x04;
		return;
	}

	if (!spec->kb_dev)
		return;

	switch (action) {
	case HDA_FIXUP_ACT_PROBE:
		spec->init_amp = ALC_INIT_DEFAULT;
		break;
	case HDA_FIXUP_ACT_FREE:
		input_unregister_device(spec->kb_dev);
		spec->kb_dev = NULL;
	}
}
#else /* INPUT */
#define alc280_fixup_hp_gpio2_mic_hotkey	NULL
#define alc233_fixup_lenovo_line2_mic_hotkey	NULL
#endif /* INPUT */

static void alc269_fixup_hp_line1_mic1_led(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->gen.vmaster_mute.hook = alc269_fixup_mic_mute_hook;
		spec->gen.cap_sync_hook = alc269_fixup_hp_cap_mic_mute_hook;
		spec->mute_led_polarity = 0;
		spec->mute_led_nid = 0x1a;
		spec->cap_mute_led_nid = 0x18;
		spec->gen.vmaster_mute_enum = 1;
		codec->power_filter = led_power_filter;
	}
}

static void alc_headset_mode_unplugged(struct hda_codec *codec)
{
	static struct coef_fw coef0255[] = {
		WRITE_COEF(0x45, 0xd089), /* UAJ function set to menual mode */
		UPDATE_COEFEX(0x57, 0x05, 1<<14, 0), /* Direct Drive HP Amp control(Set to verb control)*/
		WRITE_COEF(0x06, 0x6104), /* Set MIC2 Vref gate with HP */
		WRITE_COEFEX(0x57, 0x03, 0x8aa6), /* Direct Drive HP Amp control */
		{}
	};
	static struct coef_fw coef0255_1[] = {
		WRITE_COEF(0x1b, 0x0c0b), /* LDO and MISC control */
		{}
	};
	static struct coef_fw coef0256[] = {
		WRITE_COEF(0x1b, 0x0c4b), /* LDO and MISC control */
		{}
	};
	static struct coef_fw coef0233[] = {
		WRITE_COEF(0x1b, 0x0c0b),
		WRITE_COEF(0x45, 0xc429),
		UPDATE_COEF(0x35, 0x4000, 0),
		WRITE_COEF(0x06, 0x2104),
		WRITE_COEF(0x1a, 0x0001),
		WRITE_COEF(0x26, 0x0004),
		WRITE_COEF(0x32, 0x42a3),
		{}
	};
	static struct coef_fw coef0288[] = {
		UPDATE_COEF(0x4f, 0xfcc0, 0xc400),
		UPDATE_COEF(0x50, 0x2000, 0x2000),
		UPDATE_COEF(0x56, 0x0006, 0x0006),
		UPDATE_COEF(0x66, 0x0008, 0),
		UPDATE_COEF(0x67, 0x2000, 0),
		{}
	};
	static struct coef_fw coef0292[] = {
		WRITE_COEF(0x76, 0x000e),
		WRITE_COEF(0x6c, 0x2400),
		WRITE_COEF(0x18, 0x7308),
		WRITE_COEF(0x6b, 0xc429),
		{}
	};
	static struct coef_fw coef0293[] = {
		UPDATE_COEF(0x10, 7<<8, 6<<8), /* SET Line1 JD to 0 */
		UPDATE_COEFEX(0x57, 0x05, 1<<15|1<<13, 0x0), /* SET charge pump by verb */
		UPDATE_COEFEX(0x57, 0x03, 1<<10, 1<<10), /* SET EN_OSW to 1 */
		UPDATE_COEF(0x1a, 1<<3, 1<<3), /* Combo JD gating with LINE1-VREFO */
		WRITE_COEF(0x45, 0xc429), /* Set to TRS type */
		UPDATE_COEF(0x4a, 0x000f, 0x000e), /* Combo Jack auto detect */
		{}
	};
	static struct coef_fw coef0668[] = {
		WRITE_COEF(0x15, 0x0d40),
		WRITE_COEF(0xb7, 0x802b),
		{}
	};
	static struct coef_fw coef0225[] = {
		UPDATE_COEF(0x4a, 1<<8, 0),
		UPDATE_COEFEX(0x57, 0x05, 1<<14, 0),
		UPDATE_COEF(0x63, 3<<14, 3<<14),
		UPDATE_COEF(0x4a, 3<<4, 2<<4),
		UPDATE_COEF(0x4a, 3<<10, 3<<10),
		UPDATE_COEF(0x45, 0x3f<<10, 0x34<<10),
		UPDATE_COEF(0x4a, 3<<10, 0),
		{}
	};

	switch (codec->core.vendor_id) {
	case 0x10ec0255:
		alc_process_coef_fw(codec, coef0255_1);
		alc_process_coef_fw(codec, coef0255);
		break;
	case 0x10ec0236:
	case 0x10ec0256:
		alc_process_coef_fw(codec, coef0256);
		alc_process_coef_fw(codec, coef0255);
		break;
	case 0x10ec0233:
	case 0x10ec0283:
		alc_process_coef_fw(codec, coef0233);
		break;
	case 0x10ec0286:
	case 0x10ec0288:
	case 0x10ec0298:
		alc_process_coef_fw(codec, coef0288);
		break;
	case 0x10ec0292:
		alc_process_coef_fw(codec, coef0292);
		break;
	case 0x10ec0293:
		alc_process_coef_fw(codec, coef0293);
		break;
	case 0x10ec0668:
		alc_process_coef_fw(codec, coef0668);
		break;
	case 0x10ec0225:
	case 0x10ec0295:
	case 0x10ec0299:
		alc_process_coef_fw(codec, coef0225);
		break;
	}
	codec_dbg(codec, "Headset jack set to unplugged mode.\n");
}


static void alc_headset_mode_mic_in(struct hda_codec *codec, hda_nid_t hp_pin,
				    hda_nid_t mic_pin)
{
	static struct coef_fw coef0255[] = {
		WRITE_COEFEX(0x57, 0x03, 0x8aa6),
		WRITE_COEF(0x06, 0x6100), /* Set MIC2 Vref gate to normal */
		{}
	};
	static struct coef_fw coef0233[] = {
		UPDATE_COEF(0x35, 0, 1<<14),
		WRITE_COEF(0x06, 0x2100),
		WRITE_COEF(0x1a, 0x0021),
		WRITE_COEF(0x26, 0x008c),
		{}
	};
	static struct coef_fw coef0288[] = {
		UPDATE_COEF(0x50, 0x2000, 0),
		UPDATE_COEF(0x56, 0x0006, 0),
		UPDATE_COEF(0x4f, 0xfcc0, 0xc400),
		UPDATE_COEF(0x66, 0x0008, 0x0008),
		UPDATE_COEF(0x67, 0x2000, 0x2000),
		{}
	};
	static struct coef_fw coef0292[] = {
		WRITE_COEF(0x19, 0xa208),
		WRITE_COEF(0x2e, 0xacf0),
		{}
	};
	static struct coef_fw coef0293[] = {
		UPDATE_COEFEX(0x57, 0x05, 0, 1<<15|1<<13), /* SET charge pump by verb */
		UPDATE_COEFEX(0x57, 0x03, 1<<10, 0), /* SET EN_OSW to 0 */
		UPDATE_COEF(0x1a, 1<<3, 0), /* Combo JD gating without LINE1-VREFO */
		{}
	};
	static struct coef_fw coef0688[] = {
		WRITE_COEF(0xb7, 0x802b),
		WRITE_COEF(0xb5, 0x1040),
		UPDATE_COEF(0xc3, 0, 1<<12),
		{}
	};
	static struct coef_fw coef0225[] = {
		UPDATE_COEFEX(0x57, 0x05, 1<<14, 1<<14),
		UPDATE_COEF(0x4a, 3<<4, 2<<4),
		UPDATE_COEF(0x63, 3<<14, 0),
		{}
	};


	switch (codec->core.vendor_id) {
	case 0x10ec0236:
	case 0x10ec0255:
	case 0x10ec0256:
		alc_write_coef_idx(codec, 0x45, 0xc489);
		snd_hda_set_pin_ctl_cache(codec, hp_pin, 0);
		alc_process_coef_fw(codec, coef0255);
		snd_hda_set_pin_ctl_cache(codec, mic_pin, PIN_VREF50);
		break;
	case 0x10ec0233:
	case 0x10ec0283:
		alc_write_coef_idx(codec, 0x45, 0xc429);
		snd_hda_set_pin_ctl_cache(codec, hp_pin, 0);
		alc_process_coef_fw(codec, coef0233);
		snd_hda_set_pin_ctl_cache(codec, mic_pin, PIN_VREF50);
		break;
	case 0x10ec0286:
	case 0x10ec0288:
	case 0x10ec0298:
		alc_update_coef_idx(codec, 0x4f, 0x000c, 0);
		snd_hda_set_pin_ctl_cache(codec, hp_pin, 0);
		alc_process_coef_fw(codec, coef0288);
		snd_hda_set_pin_ctl_cache(codec, mic_pin, PIN_VREF50);
		break;
	case 0x10ec0292:
		snd_hda_set_pin_ctl_cache(codec, hp_pin, 0);
		alc_process_coef_fw(codec, coef0292);
		break;
	case 0x10ec0293:
		/* Set to TRS mode */
		alc_write_coef_idx(codec, 0x45, 0xc429);
		snd_hda_set_pin_ctl_cache(codec, hp_pin, 0);
		alc_process_coef_fw(codec, coef0293);
		snd_hda_set_pin_ctl_cache(codec, mic_pin, PIN_VREF50);
		break;
	case 0x10ec0662:
		snd_hda_set_pin_ctl_cache(codec, hp_pin, 0);
		snd_hda_set_pin_ctl_cache(codec, mic_pin, PIN_VREF50);
		break;
	case 0x10ec0668:
		alc_write_coef_idx(codec, 0x11, 0x0001);
		snd_hda_set_pin_ctl_cache(codec, hp_pin, 0);
		alc_process_coef_fw(codec, coef0688);
		snd_hda_set_pin_ctl_cache(codec, mic_pin, PIN_VREF50);
		break;
	case 0x10ec0225:
	case 0x10ec0295:
	case 0x10ec0299:
		alc_update_coef_idx(codec, 0x45, 0x3f<<10, 0x31<<10);
		snd_hda_set_pin_ctl_cache(codec, hp_pin, 0);
		alc_process_coef_fw(codec, coef0225);
		snd_hda_set_pin_ctl_cache(codec, mic_pin, PIN_VREF50);
		break;
	}
	codec_dbg(codec, "Headset jack set to mic-in mode.\n");
}

static void alc_headset_mode_default(struct hda_codec *codec)
{
	static struct coef_fw coef0225[] = {
		UPDATE_COEF(0x45, 0x3f<<10, 0x34<<10),
		{}
	};
	static struct coef_fw coef0255[] = {
		WRITE_COEF(0x45, 0xc089),
		WRITE_COEF(0x45, 0xc489),
		WRITE_COEFEX(0x57, 0x03, 0x8ea6),
		WRITE_COEF(0x49, 0x0049),
		{}
	};
	static struct coef_fw coef0233[] = {
		WRITE_COEF(0x06, 0x2100),
		WRITE_COEF(0x32, 0x4ea3),
		{}
	};
	static struct coef_fw coef0288[] = {
		UPDATE_COEF(0x4f, 0xfcc0, 0xc400), /* Set to TRS type */
		UPDATE_COEF(0x50, 0x2000, 0x2000),
		UPDATE_COEF(0x56, 0x0006, 0x0006),
		UPDATE_COEF(0x66, 0x0008, 0),
		UPDATE_COEF(0x67, 0x2000, 0),
		{}
	};
	static struct coef_fw coef0292[] = {
		WRITE_COEF(0x76, 0x000e),
		WRITE_COEF(0x6c, 0x2400),
		WRITE_COEF(0x6b, 0xc429),
		WRITE_COEF(0x18, 0x7308),
		{}
	};
	static struct coef_fw coef0293[] = {
		UPDATE_COEF(0x4a, 0x000f, 0x000e), /* Combo Jack auto detect */
		WRITE_COEF(0x45, 0xC429), /* Set to TRS type */
		UPDATE_COEF(0x1a, 1<<3, 0), /* Combo JD gating without LINE1-VREFO */
		{}
	};
	static struct coef_fw coef0688[] = {
		WRITE_COEF(0x11, 0x0041),
		WRITE_COEF(0x15, 0x0d40),
		WRITE_COEF(0xb7, 0x802b),
		{}
	};

	switch (codec->core.vendor_id) {
	case 0x10ec0225:
	case 0x10ec0295:
	case 0x10ec0299:
		alc_process_coef_fw(codec, coef0225);
		break;
	case 0x10ec0236:
	case 0x10ec0255:
	case 0x10ec0256:
		alc_process_coef_fw(codec, coef0255);
		break;
	case 0x10ec0233:
	case 0x10ec0283:
		alc_process_coef_fw(codec, coef0233);
		break;
	case 0x10ec0286:
	case 0x10ec0288:
	case 0x10ec0298:
		alc_process_coef_fw(codec, coef0288);
		break;
	case 0x10ec0292:
		alc_process_coef_fw(codec, coef0292);
		break;
	case 0x10ec0293:
		alc_process_coef_fw(codec, coef0293);
		break;
	case 0x10ec0668:
		alc_process_coef_fw(codec, coef0688);
		break;
	}
	codec_dbg(codec, "Headset jack set to headphone (default) mode.\n");
}

/* Iphone type */
static void alc_headset_mode_ctia(struct hda_codec *codec)
{
	static struct coef_fw coef0255[] = {
		WRITE_COEF(0x45, 0xd489), /* Set to CTIA type */
		WRITE_COEF(0x1b, 0x0c2b),
		WRITE_COEFEX(0x57, 0x03, 0x8ea6),
		{}
	};
	static struct coef_fw coef0256[] = {
		WRITE_COEF(0x45, 0xd489), /* Set to CTIA type */
		WRITE_COEF(0x1b, 0x0c6b),
		WRITE_COEFEX(0x57, 0x03, 0x8ea6),
		{}
	};
	static struct coef_fw coef0233[] = {
		WRITE_COEF(0x45, 0xd429),
		WRITE_COEF(0x1b, 0x0c2b),
		WRITE_COEF(0x32, 0x4ea3),
		{}
	};
	static struct coef_fw coef0288[] = {
		UPDATE_COEF(0x50, 0x2000, 0x2000),
		UPDATE_COEF(0x56, 0x0006, 0x0006),
		UPDATE_COEF(0x66, 0x0008, 0),
		UPDATE_COEF(0x67, 0x2000, 0),
		{}
	};
	static struct coef_fw coef0292[] = {
		WRITE_COEF(0x6b, 0xd429),
		WRITE_COEF(0x76, 0x0008),
		WRITE_COEF(0x18, 0x7388),
		{}
	};
	static struct coef_fw coef0293[] = {
		WRITE_COEF(0x45, 0xd429), /* Set to ctia type */
		UPDATE_COEF(0x10, 7<<8, 7<<8), /* SET Line1 JD to 1 */
		{}
	};
	static struct coef_fw coef0688[] = {
		WRITE_COEF(0x11, 0x0001),
		WRITE_COEF(0x15, 0x0d60),
		WRITE_COEF(0xc3, 0x0000),
		{}
	};
	static struct coef_fw coef0225[] = {
		UPDATE_COEF(0x45, 0x3f<<10, 0x35<<10),
		UPDATE_COEF(0x49, 1<<8, 1<<8),
		UPDATE_COEF(0x4a, 7<<6, 7<<6),
		UPDATE_COEF(0x4a, 3<<4, 3<<4),
		{}
	};

	switch (codec->core.vendor_id) {
	case 0x10ec0255:
		alc_process_coef_fw(codec, coef0255);
		break;
	case 0x10ec0236:
	case 0x10ec0256:
		alc_process_coef_fw(codec, coef0256);
		break;
	case 0x10ec0233:
	case 0x10ec0283:
		alc_process_coef_fw(codec, coef0233);
		break;
	case 0x10ec0298:
		alc_update_coef_idx(codec, 0x8e, 0x0070, 0x0020);/* Headset output enable */
		/* ALC298 jack type setting is the same with ALC286/ALC288 */
	case 0x10ec0286:
	case 0x10ec0288:
		alc_update_coef_idx(codec, 0x4f, 0xfcc0, 0xd400);
		msleep(300);
		alc_process_coef_fw(codec, coef0288);
		break;
	case 0x10ec0292:
		alc_process_coef_fw(codec, coef0292);
		break;
	case 0x10ec0293:
		alc_process_coef_fw(codec, coef0293);
		break;
	case 0x10ec0668:
		alc_process_coef_fw(codec, coef0688);
		break;
	case 0x10ec0225:
	case 0x10ec0295:
	case 0x10ec0299:
		alc_process_coef_fw(codec, coef0225);
		break;
	}
	codec_dbg(codec, "Headset jack set to iPhone-style headset mode.\n");
}

/* Nokia type */
static void alc_headset_mode_omtp(struct hda_codec *codec)
{
	static struct coef_fw coef0255[] = {
		WRITE_COEF(0x45, 0xe489), /* Set to OMTP Type */
		WRITE_COEF(0x1b, 0x0c2b),
		WRITE_COEFEX(0x57, 0x03, 0x8ea6),
		{}
	};
	static struct coef_fw coef0256[] = {
		WRITE_COEF(0x45, 0xe489), /* Set to OMTP Type */
		WRITE_COEF(0x1b, 0x0c6b),
		WRITE_COEFEX(0x57, 0x03, 0x8ea6),
		{}
	};
	static struct coef_fw coef0233[] = {
		WRITE_COEF(0x45, 0xe429),
		WRITE_COEF(0x1b, 0x0c2b),
		WRITE_COEF(0x32, 0x4ea3),
		{}
	};
	static struct coef_fw coef0288[] = {
		UPDATE_COEF(0x50, 0x2000, 0x2000),
		UPDATE_COEF(0x56, 0x0006, 0x0006),
		UPDATE_COEF(0x66, 0x0008, 0),
		UPDATE_COEF(0x67, 0x2000, 0),
		{}
	};
	static struct coef_fw coef0292[] = {
		WRITE_COEF(0x6b, 0xe429),
		WRITE_COEF(0x76, 0x0008),
		WRITE_COEF(0x18, 0x7388),
		{}
	};
	static struct coef_fw coef0293[] = {
		WRITE_COEF(0x45, 0xe429), /* Set to omtp type */
		UPDATE_COEF(0x10, 7<<8, 7<<8), /* SET Line1 JD to 1 */
		{}
	};
	static struct coef_fw coef0688[] = {
		WRITE_COEF(0x11, 0x0001),
		WRITE_COEF(0x15, 0x0d50),
		WRITE_COEF(0xc3, 0x0000),
		{}
	};
	static struct coef_fw coef0225[] = {
		UPDATE_COEF(0x45, 0x3f<<10, 0x39<<10),
		UPDATE_COEF(0x49, 1<<8, 1<<8),
		UPDATE_COEF(0x4a, 7<<6, 7<<6),
		UPDATE_COEF(0x4a, 3<<4, 3<<4),
		{}
	};

	switch (codec->core.vendor_id) {
	case 0x10ec0255:
		alc_process_coef_fw(codec, coef0255);
		break;
	case 0x10ec0236:
	case 0x10ec0256:
		alc_process_coef_fw(codec, coef0256);
		break;
	case 0x10ec0233:
	case 0x10ec0283:
		alc_process_coef_fw(codec, coef0233);
		break;
	case 0x10ec0298:
		alc_update_coef_idx(codec, 0x8e, 0x0070, 0x0010);/* Headset output enable */
		/* ALC298 jack type setting is the same with ALC286/ALC288 */
	case 0x10ec0286:
	case 0x10ec0288:
		alc_update_coef_idx(codec, 0x4f, 0xfcc0, 0xe400);
		msleep(300);
		alc_process_coef_fw(codec, coef0288);
		break;
	case 0x10ec0292:
		alc_process_coef_fw(codec, coef0292);
		break;
	case 0x10ec0293:
		alc_process_coef_fw(codec, coef0293);
		break;
	case 0x10ec0668:
		alc_process_coef_fw(codec, coef0688);
		break;
	case 0x10ec0225:
	case 0x10ec0295:
	case 0x10ec0299:
		alc_process_coef_fw(codec, coef0225);
		break;
	}
	codec_dbg(codec, "Headset jack set to Nokia-style headset mode.\n");
}

static void alc_determine_headset_type(struct hda_codec *codec)
{
	int val;
	bool is_ctia = false;
	struct alc_spec *spec = codec->spec;
	static struct coef_fw coef0255[] = {
		WRITE_COEF(0x45, 0xd089), /* combo jack auto switch control(Check type)*/
		WRITE_COEF(0x49, 0x0149), /* combo jack auto switch control(Vref
 conteol) */
		{}
	};
	static struct coef_fw coef0288[] = {
		UPDATE_COEF(0x4f, 0xfcc0, 0xd400), /* Check Type */
		{}
	};
	static struct coef_fw coef0293[] = {
		UPDATE_COEF(0x4a, 0x000f, 0x0008), /* Combo Jack auto detect */
		WRITE_COEF(0x45, 0xD429), /* Set to ctia type */
		{}
	};
	static struct coef_fw coef0688[] = {
		WRITE_COEF(0x11, 0x0001),
		WRITE_COEF(0xb7, 0x802b),
		WRITE_COEF(0x15, 0x0d60),
		WRITE_COEF(0xc3, 0x0c00),
		{}
	};
	static struct coef_fw coef0225[] = {
		UPDATE_COEF(0x45, 0x3f<<10, 0x34<<10),
		UPDATE_COEF(0x49, 1<<8, 1<<8),
		{}
	};

	switch (codec->core.vendor_id) {
	case 0x10ec0236:
	case 0x10ec0255:
	case 0x10ec0256:
		alc_process_coef_fw(codec, coef0255);
		msleep(300);
		val = alc_read_coef_idx(codec, 0x46);
		is_ctia = (val & 0x0070) == 0x0070;
		break;
	case 0x10ec0233:
	case 0x10ec0283:
		alc_write_coef_idx(codec, 0x45, 0xd029);
		msleep(300);
		val = alc_read_coef_idx(codec, 0x46);
		is_ctia = (val & 0x0070) == 0x0070;
		break;
	case 0x10ec0298:
		alc_update_coef_idx(codec, 0x8e, 0x0070, 0x0020); /* Headset output enable */
		/* ALC298 check jack type is the same with ALC286/ALC288 */
	case 0x10ec0286:
	case 0x10ec0288:
		alc_process_coef_fw(codec, coef0288);
		msleep(350);
		val = alc_read_coef_idx(codec, 0x50);
		is_ctia = (val & 0x0070) == 0x0070;
		break;
	case 0x10ec0292:
		alc_write_coef_idx(codec, 0x6b, 0xd429);
		msleep(300);
		val = alc_read_coef_idx(codec, 0x6c);
		is_ctia = (val & 0x001c) == 0x001c;
		break;
	case 0x10ec0293:
		alc_process_coef_fw(codec, coef0293);
		msleep(300);
		val = alc_read_coef_idx(codec, 0x46);
		is_ctia = (val & 0x0070) == 0x0070;
		break;
	case 0x10ec0668:
		alc_process_coef_fw(codec, coef0688);
		msleep(300);
		val = alc_read_coef_idx(codec, 0xbe);
		is_ctia = (val & 0x1c02) == 0x1c02;
		break;
	case 0x10ec0225:
	case 0x10ec0295:
	case 0x10ec0299:
		alc_process_coef_fw(codec, coef0225);
		msleep(800);
		val = alc_read_coef_idx(codec, 0x46);
		is_ctia = (val & 0x00f0) == 0x00f0;
		break;
	}

	codec_dbg(codec, "Headset jack detected iPhone-style headset: %s\n",
		    is_ctia ? "yes" : "no");
	spec->current_headset_type = is_ctia ? ALC_HEADSET_TYPE_CTIA : ALC_HEADSET_TYPE_OMTP;
}

static void alc_update_headset_mode(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	hda_nid_t mux_pin = spec->gen.imux_pins[spec->gen.cur_mux[0]];
	hda_nid_t hp_pin = spec->gen.autocfg.hp_pins[0];

	int new_headset_mode;

	if (!snd_hda_jack_detect(codec, hp_pin))
		new_headset_mode = ALC_HEADSET_MODE_UNPLUGGED;
	else if (mux_pin == spec->headset_mic_pin)
		new_headset_mode = ALC_HEADSET_MODE_HEADSET;
	else if (mux_pin == spec->headphone_mic_pin)
		new_headset_mode = ALC_HEADSET_MODE_MIC;
	else
		new_headset_mode = ALC_HEADSET_MODE_HEADPHONE;

	if (new_headset_mode == spec->current_headset_mode) {
		snd_hda_gen_update_outputs(codec);
		return;
	}

	switch (new_headset_mode) {
	case ALC_HEADSET_MODE_UNPLUGGED:
		alc_headset_mode_unplugged(codec);
		spec->gen.hp_jack_present = false;
		break;
	case ALC_HEADSET_MODE_HEADSET:
		if (spec->current_headset_type == ALC_HEADSET_TYPE_UNKNOWN)
			alc_determine_headset_type(codec);
		if (spec->current_headset_type == ALC_HEADSET_TYPE_CTIA)
			alc_headset_mode_ctia(codec);
		else if (spec->current_headset_type == ALC_HEADSET_TYPE_OMTP)
			alc_headset_mode_omtp(codec);
		spec->gen.hp_jack_present = true;
		break;
	case ALC_HEADSET_MODE_MIC:
		alc_headset_mode_mic_in(codec, hp_pin, spec->headphone_mic_pin);
		spec->gen.hp_jack_present = false;
		break;
	case ALC_HEADSET_MODE_HEADPHONE:
		alc_headset_mode_default(codec);
		spec->gen.hp_jack_present = true;
		break;
	}
	if (new_headset_mode != ALC_HEADSET_MODE_MIC) {
		snd_hda_set_pin_ctl_cache(codec, hp_pin,
					  AC_PINCTL_OUT_EN | AC_PINCTL_HP_EN);
		if (spec->headphone_mic_pin && spec->headphone_mic_pin != hp_pin)
			snd_hda_set_pin_ctl_cache(codec, spec->headphone_mic_pin,
						  PIN_VREFHIZ);
	}
	spec->current_headset_mode = new_headset_mode;

	snd_hda_gen_update_outputs(codec);
}

static void alc_update_headset_mode_hook(struct hda_codec *codec,
					 struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	alc_update_headset_mode(codec);
}

static void alc_update_headset_jack_cb(struct hda_codec *codec,
				       struct hda_jack_callback *jack)
{
	struct alc_spec *spec = codec->spec;
	spec->current_headset_type = ALC_HEADSET_TYPE_UNKNOWN;
	snd_hda_gen_hp_automute(codec, jack);
	alc_update_headset_mode(codec);
}

static void alc_probe_headset_mode(struct hda_codec *codec)
{
	int i;
	struct alc_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->gen.autocfg;

	/* Find mic pins */
	for (i = 0; i < cfg->num_inputs; i++) {
		if (cfg->inputs[i].is_headset_mic && !spec->headset_mic_pin)
			spec->headset_mic_pin = cfg->inputs[i].pin;
		if (cfg->inputs[i].is_headphone_mic && !spec->headphone_mic_pin)
			spec->headphone_mic_pin = cfg->inputs[i].pin;
	}

	spec->gen.cap_sync_hook = alc_update_headset_mode_hook;
	spec->gen.automute_hook = alc_update_headset_mode;
	spec->gen.hp_automute_hook = alc_update_headset_jack_cb;
}

static void alc_fixup_headset_mode(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		spec->parse_flags |= HDA_PINCFG_HEADSET_MIC | HDA_PINCFG_HEADPHONE_MIC;
		break;
	case HDA_FIXUP_ACT_PROBE:
		alc_probe_headset_mode(codec);
		break;
	case HDA_FIXUP_ACT_INIT:
		spec->current_headset_mode = 0;
		alc_update_headset_mode(codec);
		break;
	}
}

static void alc_fixup_headset_mode_no_hp_mic(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		struct alc_spec *spec = codec->spec;
		spec->parse_flags |= HDA_PINCFG_HEADSET_MIC;
	}
	else
		alc_fixup_headset_mode(codec, fix, action);
}

static void alc255_set_default_jack_type(struct hda_codec *codec)
{
	/* Set to iphone type */
	static struct coef_fw alc255fw[] = {
		WRITE_COEF(0x1b, 0x880b),
		WRITE_COEF(0x45, 0xd089),
		WRITE_COEF(0x1b, 0x080b),
		WRITE_COEF(0x46, 0x0004),
		WRITE_COEF(0x1b, 0x0c0b),
		{}
	};
	static struct coef_fw alc256fw[] = {
		WRITE_COEF(0x1b, 0x884b),
		WRITE_COEF(0x45, 0xd089),
		WRITE_COEF(0x1b, 0x084b),
		WRITE_COEF(0x46, 0x0004),
		WRITE_COEF(0x1b, 0x0c4b),
		{}
	};
	switch (codec->core.vendor_id) {
	case 0x10ec0255:
		alc_process_coef_fw(codec, alc255fw);
		break;
	case 0x10ec0236:
	case 0x10ec0256:
		alc_process_coef_fw(codec, alc256fw);
		break;
	}
	msleep(30);
}

static void alc_fixup_headset_mode_alc255(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		alc255_set_default_jack_type(codec);
	}
	alc_fixup_headset_mode(codec, fix, action);
}

static void alc_fixup_headset_mode_alc255_no_hp_mic(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		struct alc_spec *spec = codec->spec;
		spec->parse_flags |= HDA_PINCFG_HEADSET_MIC;
		alc255_set_default_jack_type(codec);
	} 
	else
		alc_fixup_headset_mode(codec, fix, action);
}

static void alc288_update_headset_jack_cb(struct hda_codec *codec,
				       struct hda_jack_callback *jack)
{
	struct alc_spec *spec = codec->spec;
	int present;

	alc_update_headset_jack_cb(codec, jack);
	/* Headset Mic enable or disable, only for Dell Dino */
	present = spec->gen.hp_jack_present ? 0x40 : 0;
	snd_hda_codec_write(codec, 0x01, 0, AC_VERB_SET_GPIO_DATA,
				present);
}

static void alc_fixup_headset_mode_dell_alc288(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	alc_fixup_headset_mode(codec, fix, action);
	if (action == HDA_FIXUP_ACT_PROBE) {
		struct alc_spec *spec = codec->spec;
		spec->gen.hp_automute_hook = alc288_update_headset_jack_cb;
	}
}

static void alc_fixup_auto_mute_via_amp(struct hda_codec *codec,
					const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		struct alc_spec *spec = codec->spec;
		spec->gen.auto_mute_via_amp = 1;
	}
}

static void alc_no_shutup(struct hda_codec *codec)
{
}

static void alc_fixup_no_shutup(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PROBE) {
		struct alc_spec *spec = codec->spec;
		spec->shutup = alc_no_shutup;
	}
}

static void alc_fixup_disable_aamix(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		struct alc_spec *spec = codec->spec;
		/* Disable AA-loopback as it causes white noise */
		spec->gen.mixer_nid = 0;
	}
}

/* fixup for Thinkpad docks: add dock pins, avoid HP parser fixup */
static void alc_fixup_tpt440_dock(struct hda_codec *codec,
				  const struct hda_fixup *fix, int action)
{
	static const struct hda_pintbl pincfgs[] = {
		{ 0x16, 0x21211010 }, /* dock headphone */
		{ 0x19, 0x21a11010 }, /* dock mic */
		{ }
	};
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->shutup = alc_no_shutup; /* reduce click noise */
		spec->reboot_notify = alc_d3_at_reboot; /* reduce noise */
		spec->parse_flags = HDA_PINCFG_NO_HP_FIXUP;
		codec->power_save_node = 0; /* avoid click noises */
		snd_hda_apply_pincfgs(codec, pincfgs);
	}
}

static void alc_shutup_dell_xps13(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int hp_pin = spec->gen.autocfg.hp_pins[0];

	/* Prevent pop noises when headphones are plugged in */
	snd_hda_codec_write(codec, hp_pin, 0,
			    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE);
	msleep(20);
}

static void alc_fixup_dell_xps13(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	struct hda_input_mux *imux = &spec->gen.input_mux;
	int i;

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		/* mic pin 0x19 must be initialized with Vref Hi-Z, otherwise
		 * it causes a click noise at start up
		 */
		snd_hda_codec_set_pin_target(codec, 0x19, PIN_VREFHIZ);
		break;
	case HDA_FIXUP_ACT_PROBE:
		spec->shutup = alc_shutup_dell_xps13;

		/* Make the internal mic the default input source. */
		for (i = 0; i < imux->num_items; i++) {
			if (spec->gen.imux_pins[i] == 0x12) {
				spec->gen.cur_mux[0] = i;
				break;
			}
		}
		break;
	}
}

static void alc_fixup_headset_mode_alc662(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->parse_flags |= HDA_PINCFG_HEADSET_MIC;
		spec->gen.hp_mic = 1; /* Mic-in is same pin as headphone */

		/* Disable boost for mic-in permanently. (This code is only called
		   from quirks that guarantee that the headphone is at NID 0x1b.) */
		snd_hda_codec_write(codec, 0x1b, 0, AC_VERB_SET_AMP_GAIN_MUTE, 0x7000);
		snd_hda_override_wcaps(codec, 0x1b, get_wcaps(codec, 0x1b) & ~AC_WCAP_IN_AMP);
	} else
		alc_fixup_headset_mode(codec, fix, action);
}

static void alc_fixup_headset_mode_alc668(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		alc_write_coef_idx(codec, 0xc4, 0x8000);
		alc_update_coef_idx(codec, 0xc2, ~0xfe, 0);
		snd_hda_set_pin_ctl_cache(codec, 0x18, 0);
	}
	alc_fixup_headset_mode(codec, fix, action);
}

/* Returns the nid of the external mic input pin, or 0 if it cannot be found. */
static int find_ext_mic_pin(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->gen.autocfg;
	hda_nid_t nid;
	unsigned int defcfg;
	int i;

	for (i = 0; i < cfg->num_inputs; i++) {
		if (cfg->inputs[i].type != AUTO_PIN_MIC)
			continue;
		nid = cfg->inputs[i].pin;
		defcfg = snd_hda_codec_get_pincfg(codec, nid);
		if (snd_hda_get_input_pin_attr(defcfg) == INPUT_PIN_ATTR_INT)
			continue;
		return nid;
	}

	return 0;
}

static void alc271_hp_gate_mic_jack(struct hda_codec *codec,
				    const struct hda_fixup *fix,
				    int action)
{
	struct alc_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PROBE) {
		int mic_pin = find_ext_mic_pin(codec);
		int hp_pin = spec->gen.autocfg.hp_pins[0];

		if (snd_BUG_ON(!mic_pin || !hp_pin))
			return;
		snd_hda_jack_set_gating_jack(codec, mic_pin, hp_pin);
	}
}

static void alc269_fixup_limit_int_mic_boost(struct hda_codec *codec,
					     const struct hda_fixup *fix,
					     int action)
{
	struct alc_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->gen.autocfg;
	int i;

	/* The mic boosts on level 2 and 3 are too noisy
	   on the internal mic input.
	   Therefore limit the boost to 0 or 1. */

	if (action != HDA_FIXUP_ACT_PROBE)
		return;

	for (i = 0; i < cfg->num_inputs; i++) {
		hda_nid_t nid = cfg->inputs[i].pin;
		unsigned int defcfg;
		if (cfg->inputs[i].type != AUTO_PIN_MIC)
			continue;
		defcfg = snd_hda_codec_get_pincfg(codec, nid);
		if (snd_hda_get_input_pin_attr(defcfg) != INPUT_PIN_ATTR_INT)
			continue;

		snd_hda_override_amp_caps(codec, nid, HDA_INPUT,
					  (0x00 << AC_AMPCAP_OFFSET_SHIFT) |
					  (0x01 << AC_AMPCAP_NUM_STEPS_SHIFT) |
					  (0x2f << AC_AMPCAP_STEP_SIZE_SHIFT) |
					  (0 << AC_AMPCAP_MUTE_SHIFT));
	}
}

static void alc283_hp_automute_hook(struct hda_codec *codec,
				    struct hda_jack_callback *jack)
{
	struct alc_spec *spec = codec->spec;
	int vref;

	msleep(200);
	snd_hda_gen_hp_automute(codec, jack);

	vref = spec->gen.hp_jack_present ? PIN_VREF80 : 0;

	msleep(600);
	snd_hda_codec_write(codec, 0x19, 0, AC_VERB_SET_PIN_WIDGET_CONTROL,
			    vref);
}

static void alc283_fixup_chromebook(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		snd_hda_override_wcaps(codec, 0x03, 0);
		/* Disable AA-loopback as it causes white noise */
		spec->gen.mixer_nid = 0;
		break;
	case HDA_FIXUP_ACT_INIT:
		/* MIC2-VREF control */
		/* Set to manual mode */
		alc_update_coef_idx(codec, 0x06, 0x000c, 0);
		/* Enable Line1 input control by verb */
		alc_update_coef_idx(codec, 0x1a, 0, 1 << 4);
		break;
	}
}

static void alc283_fixup_sense_combo_jack(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		spec->gen.hp_automute_hook = alc283_hp_automute_hook;
		break;
	case HDA_FIXUP_ACT_INIT:
		/* MIC2-VREF control */
		/* Set to manual mode */
		alc_update_coef_idx(codec, 0x06, 0x000c, 0);
		break;
	}
}

/* mute tablet speaker pin (0x14) via dock plugging in addition */
static void asus_tx300_automute(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	snd_hda_gen_update_outputs(codec);
	if (snd_hda_jack_detect(codec, 0x1b))
		spec->gen.mute_bits |= (1ULL << 0x14);
}

static void alc282_fixup_asus_tx300(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	/* TX300 needs to set up GPIO2 for the speaker amp */
	static const struct hda_verb gpio2_verbs[] = {
		{ 0x01, AC_VERB_SET_GPIO_MASK, 0x04 },
		{ 0x01, AC_VERB_SET_GPIO_DIRECTION, 0x04 },
		{ 0x01, AC_VERB_SET_GPIO_DATA, 0x04 },
		{}
	};
	static const struct hda_pintbl dock_pins[] = {
		{ 0x1b, 0x21114000 }, /* dock speaker pin */
		{}
	};
	struct snd_kcontrol *kctl;

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		snd_hda_add_verbs(codec, gpio2_verbs);
		snd_hda_apply_pincfgs(codec, dock_pins);
		spec->gen.auto_mute_via_amp = 1;
		spec->gen.automute_hook = asus_tx300_automute;
		snd_hda_jack_detect_enable_callback(codec, 0x1b,
						    snd_hda_gen_hp_automute);
		break;
	case HDA_FIXUP_ACT_BUILD:
		/* this is a bit tricky; give more sane names for the main
		 * (tablet) speaker and the dock speaker, respectively
		 */
		kctl = snd_hda_find_mixer_ctl(codec, "Speaker Playback Switch");
		if (kctl)
			strcpy(kctl->id.name, "Dock Speaker Playback Switch");
		kctl = snd_hda_find_mixer_ctl(codec, "Bass Speaker Playback Switch");
		if (kctl)
			strcpy(kctl->id.name, "Speaker Playback Switch");
		break;
	}
}

static void alc290_fixup_mono_speakers(struct hda_codec *codec,
				       const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		/* DAC node 0x03 is giving mono output. We therefore want to
		   make sure 0x14 (front speaker) and 0x15 (headphones) use the
		   stereo DAC, while leaving 0x17 (bass speaker) for node 0x03. */
		hda_nid_t conn1[2] = { 0x0c };
		snd_hda_override_conn_list(codec, 0x14, 1, conn1);
		snd_hda_override_conn_list(codec, 0x15, 1, conn1);
	}
}

static void alc298_fixup_speaker_volume(struct hda_codec *codec,
					const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		/* The speaker is routed to the Node 0x06 by a mistake, as a result
		   we can't adjust the speaker's volume since this node does not has
		   Amp-out capability. we change the speaker's route to:
		   Node 0x02 (Audio Output) -> Node 0x0c (Audio Mixer) -> Node 0x17 (
		   Pin Complex), since Node 0x02 has Amp-out caps, we can adjust
		   speaker's volume now. */

		hda_nid_t conn1[1] = { 0x0c };
		snd_hda_override_conn_list(codec, 0x17, 1, conn1);
	}
}

/* disable DAC3 (0x06) selection on NID 0x17 as it has no volume amp control */
static void alc295_fixup_disable_dac3(struct hda_codec *codec,
				      const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		hda_nid_t conn[2] = { 0x02, 0x03 };
		snd_hda_override_conn_list(codec, 0x17, 2, conn);
	}
}

/* Hook to update amp GPIO4 for automute */
static void alc280_hp_gpio4_automute_hook(struct hda_codec *codec,
					  struct hda_jack_callback *jack)
{
	struct alc_spec *spec = codec->spec;

	snd_hda_gen_hp_automute(codec, jack);
	/* mute_led_polarity is set to 0, so we pass inverted value here */
	alc_update_gpio_led(codec, 0x10, !spec->gen.hp_jack_present);
}

/* Manage GPIOs for HP EliteBook Folio 9480m.
 *
 * GPIO4 is the headphone amplifier power control
 * GPIO3 is the audio output mute indicator LED
 */

static void alc280_fixup_hp_9480m(struct hda_codec *codec,
				  const struct hda_fixup *fix,
				  int action)
{
	struct alc_spec *spec = codec->spec;
	static const struct hda_verb gpio_init[] = {
		{ 0x01, AC_VERB_SET_GPIO_MASK, 0x18 },
		{ 0x01, AC_VERB_SET_GPIO_DIRECTION, 0x18 },
		{}
	};

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		/* Set the hooks to turn the headphone amp on/off
		 * as needed
		 */
		spec->gen.vmaster_mute.hook = alc_fixup_gpio_mute_hook;
		spec->gen.hp_automute_hook = alc280_hp_gpio4_automute_hook;

		/* The GPIOs are currently off */
		spec->gpio_led = 0;

		/* GPIO3 is connected to the output mute LED,
		 * high is on, low is off
		 */
		spec->mute_led_polarity = 0;
		spec->gpio_mute_led_mask = 0x08;

		/* Initialize GPIO configuration */
		snd_hda_add_verbs(codec, gpio_init);
	}
}

static void alc_fixup_disable_mic_vref(struct hda_codec *codec,
				  const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		snd_hda_codec_set_pin_target(codec, 0x19, PIN_VREFHIZ);
}

/* for hda_fixup_thinkpad_acpi() */
#include "thinkpad_helper.c"

/* for dell wmi mic mute led */
#include "dell_wmi_helper.c"

enum {
	ALC269_FIXUP_SONY_VAIO,
	ALC275_FIXUP_SONY_VAIO_GPIO2,
	ALC269_FIXUP_DELL_M101Z,
	ALC269_FIXUP_SKU_IGNORE,
	ALC269_FIXUP_ASUS_G73JW,
	ALC269_FIXUP_LENOVO_EAPD,
	ALC275_FIXUP_SONY_HWEQ,
	ALC275_FIXUP_SONY_DISABLE_AAMIX,
	ALC271_FIXUP_DMIC,
	ALC269_FIXUP_PCM_44K,
	ALC269_FIXUP_STEREO_DMIC,
	ALC269_FIXUP_HEADSET_MIC,
	ALC269_FIXUP_QUANTA_MUTE,
	ALC269_FIXUP_LIFEBOOK,
	ALC269_FIXUP_LIFEBOOK_EXTMIC,
	ALC269_FIXUP_LIFEBOOK_HP_PIN,
	ALC269_FIXUP_LIFEBOOK_NO_HP_TO_LINEOUT,
	ALC255_FIXUP_LIFEBOOK_U7x7_HEADSET_MIC,
	ALC269_FIXUP_AMIC,
	ALC269_FIXUP_DMIC,
	ALC269VB_FIXUP_AMIC,
	ALC269VB_FIXUP_DMIC,
	ALC269_FIXUP_HP_MUTE_LED,
	ALC269_FIXUP_HP_MUTE_LED_MIC1,
	ALC269_FIXUP_HP_MUTE_LED_MIC2,
	ALC269_FIXUP_HP_GPIO_LED,
	ALC269_FIXUP_HP_GPIO_MIC1_LED,
	ALC269_FIXUP_HP_LINE1_MIC1_LED,
	ALC269_FIXUP_INV_DMIC,
	ALC269_FIXUP_LENOVO_DOCK,
	ALC269_FIXUP_LENOVO_DOCK_LIMIT_BOOST,
	ALC269_FIXUP_NO_SHUTUP,
	ALC286_FIXUP_SONY_MIC_NO_PRESENCE,
	ALC269_FIXUP_PINCFG_NO_HP_TO_LINEOUT,
	ALC269_FIXUP_DELL1_MIC_NO_PRESENCE,
	ALC269_FIXUP_DELL2_MIC_NO_PRESENCE,
	ALC269_FIXUP_DELL3_MIC_NO_PRESENCE,
	ALC269_FIXUP_DELL4_MIC_NO_PRESENCE,
	ALC269_FIXUP_HEADSET_MODE,
	ALC269_FIXUP_HEADSET_MODE_NO_HP_MIC,
	ALC269_FIXUP_ASPIRE_HEADSET_MIC,
	ALC269_FIXUP_ASUS_X101_FUNC,
	ALC269_FIXUP_ASUS_X101_VERB,
	ALC269_FIXUP_ASUS_X101,
	ALC271_FIXUP_AMIC_MIC2,
	ALC271_FIXUP_HP_GATE_MIC_JACK,
	ALC271_FIXUP_HP_GATE_MIC_JACK_E1_572,
	ALC269_FIXUP_ACER_AC700,
	ALC269_FIXUP_LIMIT_INT_MIC_BOOST,
	ALC269VB_FIXUP_ASUS_ZENBOOK,
	ALC269VB_FIXUP_ASUS_ZENBOOK_UX31A,
	ALC269_FIXUP_LIMIT_INT_MIC_BOOST_MUTE_LED,
	ALC269VB_FIXUP_ORDISSIMO_EVE2,
	ALC283_FIXUP_CHROME_BOOK,
	ALC283_FIXUP_SENSE_COMBO_JACK,
	ALC282_FIXUP_ASUS_TX300,
	ALC283_FIXUP_INT_MIC,
	ALC290_FIXUP_MONO_SPEAKERS,
	ALC290_FIXUP_MONO_SPEAKERS_HSJACK,
	ALC290_FIXUP_SUBWOOFER,
	ALC290_FIXUP_SUBWOOFER_HSJACK,
	ALC269_FIXUP_THINKPAD_ACPI,
	ALC269_FIXUP_DMIC_THINKPAD_ACPI,
	ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
	ALC255_FIXUP_DELL2_MIC_NO_PRESENCE,
	ALC255_FIXUP_HEADSET_MODE,
	ALC255_FIXUP_HEADSET_MODE_NO_HP_MIC,
	ALC293_FIXUP_DELL1_MIC_NO_PRESENCE,
	ALC292_FIXUP_TPT440_DOCK,
	ALC292_FIXUP_TPT440,
	ALC283_FIXUP_BXBT2807_MIC,
	ALC255_FIXUP_DELL_WMI_MIC_MUTE_LED,
	ALC282_FIXUP_ASPIRE_V5_PINS,
	ALC280_FIXUP_HP_GPIO4,
	ALC286_FIXUP_HP_GPIO_LED,
	ALC280_FIXUP_HP_GPIO2_MIC_HOTKEY,
	ALC280_FIXUP_HP_DOCK_PINS,
	ALC269_FIXUP_HP_DOCK_GPIO_MIC1_LED,
	ALC280_FIXUP_HP_9480M,
	ALC288_FIXUP_DELL_HEADSET_MODE,
	ALC288_FIXUP_DELL1_MIC_NO_PRESENCE,
	ALC288_FIXUP_DELL_XPS_13_GPIO6,
	ALC288_FIXUP_DELL_XPS_13,
	ALC288_FIXUP_DISABLE_AAMIX,
	ALC292_FIXUP_DELL_E7X,
	ALC292_FIXUP_DISABLE_AAMIX,
	ALC293_FIXUP_DISABLE_AAMIX_MULTIJACK,
	ALC298_FIXUP_DELL1_MIC_NO_PRESENCE,
	ALC298_FIXUP_DELL_AIO_MIC_NO_PRESENCE,
	ALC275_FIXUP_DELL_XPS,
	ALC256_FIXUP_DELL_XPS_13_HEADPHONE_NOISE,
	ALC293_FIXUP_LENOVO_SPK_NOISE,
	ALC233_FIXUP_LENOVO_LINE2_MIC_HOTKEY,
	ALC255_FIXUP_DELL_SPK_NOISE,
	ALC225_FIXUP_DISABLE_MIC_VREF,
	ALC225_FIXUP_DELL1_MIC_NO_PRESENCE,
	ALC295_FIXUP_DISABLE_DAC3,
	ALC280_FIXUP_HP_HEADSET_MIC,
	ALC221_FIXUP_HP_FRONT_MIC,
	ALC292_FIXUP_TPT460,
	ALC298_FIXUP_SPK_VOLUME,
	ALC256_FIXUP_DELL_INSPIRON_7559_SUBWOOFER,
};

static const struct hda_fixup alc269_fixups[] = {
	[ALC269_FIXUP_SONY_VAIO] = {
		.type = HDA_FIXUP_PINCTLS,
		.v.pins = (const struct hda_pintbl[]) {
			{0x19, PIN_VREFGRD},
			{}
		}
	},
	[ALC275_FIXUP_SONY_VAIO_GPIO2] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{0x01, AC_VERB_SET_GPIO_MASK, 0x04},
			{0x01, AC_VERB_SET_GPIO_DIRECTION, 0x04},
			{0x01, AC_VERB_SET_GPIO_DATA, 0x00},
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_SONY_VAIO
	},
	[ALC269_FIXUP_DELL_M101Z] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* Enables internal speaker */
			{0x20, AC_VERB_SET_COEF_INDEX, 13},
			{0x20, AC_VERB_SET_PROC_COEF, 0x4040},
			{}
		}
	},
	[ALC269_FIXUP_SKU_IGNORE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_sku_ignore,
	},
	[ALC269_FIXUP_ASUS_G73JW] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x17, 0x99130111 }, /* subwoofer */
			{ }
		}
	},
	[ALC269_FIXUP_LENOVO_EAPD] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{0x14, AC_VERB_SET_EAPD_BTLENABLE, 0},
			{}
		}
	},
	[ALC275_FIXUP_SONY_HWEQ] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_hweq,
		.chained = true,
		.chain_id = ALC275_FIXUP_SONY_VAIO_GPIO2
	},
	[ALC275_FIXUP_SONY_DISABLE_AAMIX] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_disable_aamix,
		.chained = true,
		.chain_id = ALC269_FIXUP_SONY_VAIO
	},
	[ALC271_FIXUP_DMIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc271_fixup_dmic,
	},
	[ALC269_FIXUP_PCM_44K] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_pcm_44k,
		.chained = true,
		.chain_id = ALC269_FIXUP_QUANTA_MUTE
	},
	[ALC269_FIXUP_STEREO_DMIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_stereo_dmic,
	},
	[ALC269_FIXUP_HEADSET_MIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_headset_mic,
	},
	[ALC269_FIXUP_QUANTA_MUTE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_quanta_mute,
	},
	[ALC269_FIXUP_LIFEBOOK] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1a, 0x2101103f }, /* dock line-out */
			{ 0x1b, 0x23a11040 }, /* dock mic-in */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_QUANTA_MUTE
	},
	[ALC269_FIXUP_LIFEBOOK_EXTMIC] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x01a1903c }, /* headset mic, with jack detect */
			{ }
		},
	},
	[ALC269_FIXUP_LIFEBOOK_HP_PIN] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x21, 0x0221102f }, /* HP out */
			{ }
		},
	},
	[ALC269_FIXUP_LIFEBOOK_NO_HP_TO_LINEOUT] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_pincfg_no_hp_to_lineout,
	},
	[ALC255_FIXUP_LIFEBOOK_U7x7_HEADSET_MIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_pincfg_U7x7_headset_mic,
	},
	[ALC269_FIXUP_AMIC] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x15, 0x0121401f }, /* HP out */
			{ 0x18, 0x01a19c20 }, /* mic */
			{ 0x19, 0x99a3092f }, /* int-mic */
			{ }
		},
	},
	[ALC269_FIXUP_DMIC] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x12, 0x99a3092f }, /* int-mic */
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x15, 0x0121401f }, /* HP out */
			{ 0x18, 0x01a19c20 }, /* mic */
			{ }
		},
	},
	[ALC269VB_FIXUP_AMIC] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x18, 0x01a19c20 }, /* mic */
			{ 0x19, 0x99a3092f }, /* int-mic */
			{ 0x21, 0x0121401f }, /* HP out */
			{ }
		},
	},
	[ALC269VB_FIXUP_DMIC] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x12, 0x99a3092f }, /* int-mic */
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x18, 0x01a19c20 }, /* mic */
			{ 0x21, 0x0121401f }, /* HP out */
			{ }
		},
	},
	[ALC269_FIXUP_HP_MUTE_LED] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_hp_mute_led,
	},
	[ALC269_FIXUP_HP_MUTE_LED_MIC1] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_hp_mute_led_mic1,
	},
	[ALC269_FIXUP_HP_MUTE_LED_MIC2] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_hp_mute_led_mic2,
	},
	[ALC269_FIXUP_HP_GPIO_LED] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_hp_gpio_led,
	},
	[ALC269_FIXUP_HP_GPIO_MIC1_LED] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_hp_gpio_mic1_led,
	},
	[ALC269_FIXUP_HP_LINE1_MIC1_LED] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_hp_line1_mic1_led,
	},
	[ALC269_FIXUP_INV_DMIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_inv_dmic,
	},
	[ALC269_FIXUP_NO_SHUTUP] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_no_shutup,
	},
	[ALC269_FIXUP_LENOVO_DOCK] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x23a11040 }, /* dock mic */
			{ 0x1b, 0x2121103f }, /* dock headphone */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_PINCFG_NO_HP_TO_LINEOUT
	},
	[ALC269_FIXUP_LENOVO_DOCK_LIMIT_BOOST] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_limit_int_mic_boost,
		.chained = true,
		.chain_id = ALC269_FIXUP_LENOVO_DOCK,
	},
	[ALC269_FIXUP_PINCFG_NO_HP_TO_LINEOUT] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_pincfg_no_hp_to_lineout,
		.chained = true,
		.chain_id = ALC269_FIXUP_THINKPAD_ACPI,
	},
	[ALC269_FIXUP_DELL1_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x01a1913c }, /* use as headset mic, without its own jack detect */
			{ 0x1a, 0x01a1913d }, /* use as headphone mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MODE
	},
	[ALC269_FIXUP_DELL2_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x16, 0x21014020 }, /* dock line out */
			{ 0x19, 0x21a19030 }, /* dock mic */
			{ 0x1a, 0x01a1913c }, /* use as headset mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MODE_NO_HP_MIC
	},
	[ALC269_FIXUP_DELL3_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1a, 0x01a1913c }, /* use as headset mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MODE_NO_HP_MIC
	},
	[ALC269_FIXUP_DELL4_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x01a1913c }, /* use as headset mic, without its own jack detect */
			{ 0x1b, 0x01a1913d }, /* use as headphone mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MODE
	},
	[ALC269_FIXUP_HEADSET_MODE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_headset_mode,
		.chained = true,
		.chain_id = ALC255_FIXUP_DELL_WMI_MIC_MUTE_LED
	},
	[ALC269_FIXUP_HEADSET_MODE_NO_HP_MIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_headset_mode_no_hp_mic,
	},
	[ALC269_FIXUP_ASPIRE_HEADSET_MIC] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x01a1913c }, /* headset mic w/o jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MODE,
	},
	[ALC286_FIXUP_SONY_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x18, 0x01a1913c }, /* use as headset mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MIC
	},
	[ALC269_FIXUP_ASUS_X101_FUNC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_x101_headset_mic,
	},
	[ALC269_FIXUP_ASUS_X101_VERB] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},
			{0x20, AC_VERB_SET_COEF_INDEX, 0x08},
			{0x20, AC_VERB_SET_PROC_COEF,  0x0310},
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_ASUS_X101_FUNC
	},
	[ALC269_FIXUP_ASUS_X101] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x18, 0x04a1182c }, /* Headset mic */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_ASUS_X101_VERB
	},
	[ALC271_FIXUP_AMIC_MIC2] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x19, 0x01a19c20 }, /* mic */
			{ 0x1b, 0x99a7012f }, /* int-mic */
			{ 0x21, 0x0121401f }, /* HP out */
			{ }
		},
	},
	[ALC271_FIXUP_HP_GATE_MIC_JACK] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc271_hp_gate_mic_jack,
		.chained = true,
		.chain_id = ALC271_FIXUP_AMIC_MIC2,
	},
	[ALC271_FIXUP_HP_GATE_MIC_JACK_E1_572] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_limit_int_mic_boost,
		.chained = true,
		.chain_id = ALC271_FIXUP_HP_GATE_MIC_JACK,
	},
	[ALC269_FIXUP_ACER_AC700] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x12, 0x99a3092f }, /* int-mic */
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x18, 0x03a11c20 }, /* mic */
			{ 0x1e, 0x0346101e }, /* SPDIF1 */
			{ 0x21, 0x0321101f }, /* HP out */
			{ }
		},
		.chained = true,
		.chain_id = ALC271_FIXUP_DMIC,
	},
	[ALC269_FIXUP_LIMIT_INT_MIC_BOOST] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_limit_int_mic_boost,
		.chained = true,
		.chain_id = ALC269_FIXUP_THINKPAD_ACPI,
	},
	[ALC269VB_FIXUP_ASUS_ZENBOOK] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_limit_int_mic_boost,
		.chained = true,
		.chain_id = ALC269VB_FIXUP_DMIC,
	},
	[ALC269VB_FIXUP_ASUS_ZENBOOK_UX31A] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* class-D output amp +5dB */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x12 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x2800 },
			{}
		},
		.chained = true,
		.chain_id = ALC269VB_FIXUP_ASUS_ZENBOOK,
	},
	[ALC269_FIXUP_LIMIT_INT_MIC_BOOST_MUTE_LED] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc269_fixup_limit_int_mic_boost,
		.chained = true,
		.chain_id = ALC269_FIXUP_HP_MUTE_LED_MIC1,
	},
	[ALC269VB_FIXUP_ORDISSIMO_EVE2] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x12, 0x99a3092f }, /* int-mic */
			{ 0x18, 0x03a11d20 }, /* mic */
			{ 0x19, 0x411111f0 }, /* Unused bogus pin */
			{ }
		},
	},
	[ALC283_FIXUP_CHROME_BOOK] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc283_fixup_chromebook,
	},
	[ALC283_FIXUP_SENSE_COMBO_JACK] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc283_fixup_sense_combo_jack,
		.chained = true,
		.chain_id = ALC283_FIXUP_CHROME_BOOK,
	},
	[ALC282_FIXUP_ASUS_TX300] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc282_fixup_asus_tx300,
	},
	[ALC283_FIXUP_INT_MIC] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{0x20, AC_VERB_SET_COEF_INDEX, 0x1a},
			{0x20, AC_VERB_SET_PROC_COEF, 0x0011},
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_LIMIT_INT_MIC_BOOST
	},
	[ALC290_FIXUP_SUBWOOFER_HSJACK] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x17, 0x90170112 }, /* subwoofer */
			{ }
		},
		.chained = true,
		.chain_id = ALC290_FIXUP_MONO_SPEAKERS_HSJACK,
	},
	[ALC290_FIXUP_SUBWOOFER] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x17, 0x90170112 }, /* subwoofer */
			{ }
		},
		.chained = true,
		.chain_id = ALC290_FIXUP_MONO_SPEAKERS,
	},
	[ALC290_FIXUP_MONO_SPEAKERS] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc290_fixup_mono_speakers,
	},
	[ALC290_FIXUP_MONO_SPEAKERS_HSJACK] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc290_fixup_mono_speakers,
		.chained = true,
		.chain_id = ALC269_FIXUP_DELL3_MIC_NO_PRESENCE,
	},
	[ALC269_FIXUP_THINKPAD_ACPI] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = hda_fixup_thinkpad_acpi,
	},
	[ALC269_FIXUP_DMIC_THINKPAD_ACPI] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_inv_dmic,
		.chained = true,
		.chain_id = ALC269_FIXUP_THINKPAD_ACPI,
	},
	[ALC255_FIXUP_DELL1_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x01a1913c }, /* use as headset mic, without its own jack detect */
			{ 0x1a, 0x01a1913d }, /* use as headphone mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC255_FIXUP_HEADSET_MODE
	},
	[ALC255_FIXUP_DELL2_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x01a1913c }, /* use as headset mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC255_FIXUP_HEADSET_MODE_NO_HP_MIC
	},
	[ALC255_FIXUP_HEADSET_MODE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_headset_mode_alc255,
		.chained = true,
		.chain_id = ALC255_FIXUP_DELL_WMI_MIC_MUTE_LED
	},
	[ALC255_FIXUP_HEADSET_MODE_NO_HP_MIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_headset_mode_alc255_no_hp_mic,
	},
	[ALC293_FIXUP_DELL1_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x18, 0x01a1913d }, /* use as headphone mic, without its own jack detect */
			{ 0x1a, 0x01a1913c }, /* use as headset mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MODE
	},
	[ALC292_FIXUP_TPT440_DOCK] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_tpt440_dock,
		.chained = true,
		.chain_id = ALC269_FIXUP_LIMIT_INT_MIC_BOOST
	},
	[ALC292_FIXUP_TPT440] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_disable_aamix,
		.chained = true,
		.chain_id = ALC292_FIXUP_TPT440_DOCK,
	},
	[ALC283_FIXUP_BXBT2807_MIC] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x04a110f0 },
			{ },
		},
	},
	[ALC255_FIXUP_DELL_WMI_MIC_MUTE_LED] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_dell_wmi,
	},
	[ALC282_FIXUP_ASPIRE_V5_PINS] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x12, 0x90a60130 },
			{ 0x14, 0x90170110 },
			{ 0x17, 0x40000008 },
			{ 0x18, 0x411111f0 },
			{ 0x19, 0x01a1913c },
			{ 0x1a, 0x411111f0 },
			{ 0x1b, 0x411111f0 },
			{ 0x1d, 0x40f89b2d },
			{ 0x1e, 0x411111f0 },
			{ 0x21, 0x0321101f },
			{ },
		},
	},
	[ALC280_FIXUP_HP_GPIO4] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc280_fixup_hp_gpio4,
	},
	[ALC286_FIXUP_HP_GPIO_LED] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc286_fixup_hp_gpio_led,
	},
	[ALC280_FIXUP_HP_GPIO2_MIC_HOTKEY] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc280_fixup_hp_gpio2_mic_hotkey,
	},
	[ALC280_FIXUP_HP_DOCK_PINS] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1b, 0x21011020 }, /* line-out */
			{ 0x1a, 0x01a1903c }, /* headset mic */
			{ 0x18, 0x2181103f }, /* line-in */
			{ },
		},
		.chained = true,
		.chain_id = ALC280_FIXUP_HP_GPIO4
	},
	[ALC269_FIXUP_HP_DOCK_GPIO_MIC1_LED] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1b, 0x21011020 }, /* line-out */
			{ 0x18, 0x2181103f }, /* line-in */
			{ },
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HP_GPIO_MIC1_LED
	},
	[ALC280_FIXUP_HP_9480M] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc280_fixup_hp_9480m,
	},
	[ALC288_FIXUP_DELL_HEADSET_MODE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_headset_mode_dell_alc288,
		.chained = true,
		.chain_id = ALC255_FIXUP_DELL_WMI_MIC_MUTE_LED
	},
	[ALC288_FIXUP_DELL1_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x18, 0x01a1913c }, /* use as headset mic, without its own jack detect */
			{ 0x1a, 0x01a1913d }, /* use as headphone mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC288_FIXUP_DELL_HEADSET_MODE
	},
	[ALC288_FIXUP_DELL_XPS_13_GPIO6] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{0x01, AC_VERB_SET_GPIO_MASK, 0x40},
			{0x01, AC_VERB_SET_GPIO_DIRECTION, 0x40},
			{0x01, AC_VERB_SET_GPIO_DATA, 0x00},
			{ }
		},
		.chained = true,
		.chain_id = ALC288_FIXUP_DELL1_MIC_NO_PRESENCE
	},
	[ALC288_FIXUP_DISABLE_AAMIX] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_disable_aamix,
		.chained = true,
		.chain_id = ALC288_FIXUP_DELL_XPS_13_GPIO6
	},
	[ALC288_FIXUP_DELL_XPS_13] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_dell_xps13,
		.chained = true,
		.chain_id = ALC288_FIXUP_DISABLE_AAMIX
	},
	[ALC292_FIXUP_DISABLE_AAMIX] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_disable_aamix,
		.chained = true,
		.chain_id = ALC269_FIXUP_DELL2_MIC_NO_PRESENCE
	},
	[ALC293_FIXUP_DISABLE_AAMIX_MULTIJACK] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_disable_aamix,
		.chained = true,
		.chain_id = ALC293_FIXUP_DELL1_MIC_NO_PRESENCE
	},
	[ALC292_FIXUP_DELL_E7X] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_dell_xps13,
		.chained = true,
		.chain_id = ALC292_FIXUP_DISABLE_AAMIX
	},
	[ALC298_FIXUP_DELL1_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x18, 0x01a1913c }, /* use as headset mic, without its own jack detect */
			{ 0x1a, 0x01a1913d }, /* use as headphone mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MODE
	},
	[ALC298_FIXUP_DELL_AIO_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x18, 0x01a1913c }, /* use as headset mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MODE
	},
	[ALC275_FIXUP_DELL_XPS] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* Enables internal speaker */
			{0x20, AC_VERB_SET_COEF_INDEX, 0x1f},
			{0x20, AC_VERB_SET_PROC_COEF, 0x00c0},
			{0x20, AC_VERB_SET_COEF_INDEX, 0x30},
			{0x20, AC_VERB_SET_PROC_COEF, 0x00b1},
			{}
		}
	},
	[ALC256_FIXUP_DELL_XPS_13_HEADPHONE_NOISE] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* Disable pass-through path for FRONT 14h */
			{0x20, AC_VERB_SET_COEF_INDEX, 0x36},
			{0x20, AC_VERB_SET_PROC_COEF, 0x1737},
			{}
		},
		.chained = true,
		.chain_id = ALC255_FIXUP_DELL1_MIC_NO_PRESENCE
	},
	[ALC293_FIXUP_LENOVO_SPK_NOISE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_disable_aamix,
		.chained = true,
		.chain_id = ALC269_FIXUP_THINKPAD_ACPI
	},
	[ALC233_FIXUP_LENOVO_LINE2_MIC_HOTKEY] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc233_fixup_lenovo_line2_mic_hotkey,
	},
	[ALC255_FIXUP_DELL_SPK_NOISE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_disable_aamix,
		.chained = true,
		.chain_id = ALC255_FIXUP_DELL1_MIC_NO_PRESENCE
	},
	[ALC225_FIXUP_DISABLE_MIC_VREF] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_disable_mic_vref,
		.chained = true,
		.chain_id = ALC269_FIXUP_DELL1_MIC_NO_PRESENCE
	},
	[ALC225_FIXUP_DELL1_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* Disable pass-through path for FRONT 14h */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x36 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x57d7 },
			{}
		},
		.chained = true,
		.chain_id = ALC225_FIXUP_DISABLE_MIC_VREF
	},
	[ALC280_FIXUP_HP_HEADSET_MIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_disable_aamix,
		.chained = true,
		.chain_id = ALC269_FIXUP_HEADSET_MIC,
	},
	[ALC221_FIXUP_HP_FRONT_MIC] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x02a19020 }, /* Front Mic */
			{ }
		},
	},
	[ALC292_FIXUP_TPT460] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_tpt440_dock,
		.chained = true,
		.chain_id = ALC293_FIXUP_LENOVO_SPK_NOISE,
	},
	[ALC298_FIXUP_SPK_VOLUME] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc298_fixup_speaker_volume,
		.chained = true,
		.chain_id = ALC298_FIXUP_DELL_AIO_MIC_NO_PRESENCE,
	},
	[ALC295_FIXUP_DISABLE_DAC3] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc295_fixup_disable_dac3,
	},
	[ALC256_FIXUP_DELL_INSPIRON_7559_SUBWOOFER] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1b, 0x90170151 },
			{ }
		},
		.chained = true,
		.chain_id = ALC255_FIXUP_DELL1_MIC_NO_PRESENCE
	},
};

static const struct snd_pci_quirk alc269_fixup_tbl[] = {
	SND_PCI_QUIRK(0x1025, 0x0283, "Acer TravelMate 8371", ALC269_FIXUP_INV_DMIC),
	SND_PCI_QUIRK(0x1025, 0x029b, "Acer 1810TZ", ALC269_FIXUP_INV_DMIC),
	SND_PCI_QUIRK(0x1025, 0x0349, "Acer AOD260", ALC269_FIXUP_INV_DMIC),
	SND_PCI_QUIRK(0x1025, 0x047c, "Acer AC700", ALC269_FIXUP_ACER_AC700),
	SND_PCI_QUIRK(0x1025, 0x072d, "Acer Aspire V5-571G", ALC269_FIXUP_ASPIRE_HEADSET_MIC),
	SND_PCI_QUIRK(0x1025, 0x080d, "Acer Aspire V5-122P", ALC269_FIXUP_ASPIRE_HEADSET_MIC),
	SND_PCI_QUIRK(0x1025, 0x0740, "Acer AO725", ALC271_FIXUP_HP_GATE_MIC_JACK),
	SND_PCI_QUIRK(0x1025, 0x0742, "Acer AO756", ALC271_FIXUP_HP_GATE_MIC_JACK),
	SND_PCI_QUIRK(0x1025, 0x0762, "Acer Aspire E1-472", ALC271_FIXUP_HP_GATE_MIC_JACK_E1_572),
	SND_PCI_QUIRK(0x1025, 0x0775, "Acer Aspire E1-572", ALC271_FIXUP_HP_GATE_MIC_JACK_E1_572),
	SND_PCI_QUIRK(0x1025, 0x079b, "Acer Aspire V5-573G", ALC282_FIXUP_ASPIRE_V5_PINS),
	SND_PCI_QUIRK(0x1025, 0x106d, "Acer Cloudbook 14", ALC283_FIXUP_CHROME_BOOK),
	SND_PCI_QUIRK(0x1028, 0x0470, "Dell M101z", ALC269_FIXUP_DELL_M101Z),
	SND_PCI_QUIRK(0x1028, 0x053c, "Dell Latitude E5430", ALC292_FIXUP_DELL_E7X),
	SND_PCI_QUIRK(0x1028, 0x054b, "Dell XPS one 2710", ALC275_FIXUP_DELL_XPS),
	SND_PCI_QUIRK(0x1028, 0x05bd, "Dell Latitude E6440", ALC292_FIXUP_DELL_E7X),
	SND_PCI_QUIRK(0x1028, 0x05be, "Dell Latitude E6540", ALC292_FIXUP_DELL_E7X),
	SND_PCI_QUIRK(0x1028, 0x05ca, "Dell Latitude E7240", ALC292_FIXUP_DELL_E7X),
	SND_PCI_QUIRK(0x1028, 0x05cb, "Dell Latitude E7440", ALC292_FIXUP_DELL_E7X),
	SND_PCI_QUIRK(0x1028, 0x05da, "Dell Vostro 5460", ALC290_FIXUP_SUBWOOFER),
	SND_PCI_QUIRK(0x1028, 0x05f4, "Dell", ALC269_FIXUP_DELL1_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x05f5, "Dell", ALC269_FIXUP_DELL1_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x05f6, "Dell", ALC269_FIXUP_DELL1_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x0615, "Dell Vostro 5470", ALC290_FIXUP_SUBWOOFER_HSJACK),
	SND_PCI_QUIRK(0x1028, 0x0616, "Dell Vostro 5470", ALC290_FIXUP_SUBWOOFER_HSJACK),
	SND_PCI_QUIRK(0x1028, 0x062c, "Dell Latitude E5550", ALC292_FIXUP_DELL_E7X),
	SND_PCI_QUIRK(0x1028, 0x062e, "Dell Latitude E7450", ALC292_FIXUP_DELL_E7X),
	SND_PCI_QUIRK(0x1028, 0x0638, "Dell Inspiron 5439", ALC290_FIXUP_MONO_SPEAKERS_HSJACK),
	SND_PCI_QUIRK(0x1028, 0x064a, "Dell", ALC293_FIXUP_DELL1_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x064b, "Dell", ALC293_FIXUP_DELL1_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x0665, "Dell XPS 13", ALC288_FIXUP_DELL_XPS_13),
	SND_PCI_QUIRK(0x1028, 0x0669, "Dell Optiplex 9020m", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x069a, "Dell Vostro 5480", ALC290_FIXUP_SUBWOOFER_HSJACK),
	SND_PCI_QUIRK(0x1028, 0x06c7, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x06d9, "Dell", ALC293_FIXUP_DELL1_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x06da, "Dell", ALC293_FIXUP_DELL1_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x06db, "Dell", ALC293_FIXUP_DISABLE_AAMIX_MULTIJACK),
	SND_PCI_QUIRK(0x1028, 0x06dd, "Dell", ALC293_FIXUP_DISABLE_AAMIX_MULTIJACK),
	SND_PCI_QUIRK(0x1028, 0x06de, "Dell", ALC293_FIXUP_DISABLE_AAMIX_MULTIJACK),
	SND_PCI_QUIRK(0x1028, 0x06df, "Dell", ALC293_FIXUP_DISABLE_AAMIX_MULTIJACK),
	SND_PCI_QUIRK(0x1028, 0x06e0, "Dell", ALC293_FIXUP_DISABLE_AAMIX_MULTIJACK),
	SND_PCI_QUIRK(0x1028, 0x0704, "Dell XPS 13 9350", ALC256_FIXUP_DELL_XPS_13_HEADPHONE_NOISE),
	SND_PCI_QUIRK(0x1028, 0x0706, "Dell Inspiron 7559", ALC256_FIXUP_DELL_INSPIRON_7559_SUBWOOFER),
	SND_PCI_QUIRK(0x1028, 0x0725, "Dell Inspiron 3162", ALC255_FIXUP_DELL_SPK_NOISE),
	SND_PCI_QUIRK(0x1028, 0x075b, "Dell XPS 13 9360", ALC256_FIXUP_DELL_XPS_13_HEADPHONE_NOISE),
	SND_PCI_QUIRK(0x1028, 0x075c, "Dell XPS 27 7760", ALC298_FIXUP_SPK_VOLUME),
	SND_PCI_QUIRK(0x1028, 0x075d, "Dell AIO", ALC298_FIXUP_SPK_VOLUME),
	SND_PCI_QUIRK(0x1028, 0x07b0, "Dell Precision 7520", ALC295_FIXUP_DISABLE_DAC3),
	SND_PCI_QUIRK(0x1028, 0x0798, "Dell Inspiron 17 7000 Gaming", ALC256_FIXUP_DELL_INSPIRON_7559_SUBWOOFER),
	SND_PCI_QUIRK(0x1028, 0x082a, "Dell XPS 13 9360", ALC256_FIXUP_DELL_XPS_13_HEADPHONE_NOISE),
	SND_PCI_QUIRK(0x1028, 0x164a, "Dell", ALC293_FIXUP_DELL1_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x164b, "Dell", ALC293_FIXUP_DELL1_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x103c, 0x1586, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC2),
	SND_PCI_QUIRK(0x103c, 0x18e6, "HP", ALC269_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x218b, "HP", ALC269_FIXUP_LIMIT_INT_MIC_BOOST_MUTE_LED),
	SND_PCI_QUIRK(0x103c, 0x225f, "HP", ALC280_FIXUP_HP_GPIO2_MIC_HOTKEY),
	/* ALC282 */
	SND_PCI_QUIRK(0x103c, 0x21f9, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x2210, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x2214, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x2236, "HP", ALC269_FIXUP_HP_LINE1_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2237, "HP", ALC269_FIXUP_HP_LINE1_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2238, "HP", ALC269_FIXUP_HP_LINE1_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2239, "HP", ALC269_FIXUP_HP_LINE1_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x224b, "HP", ALC269_FIXUP_HP_LINE1_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2268, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x226a, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x226b, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x226e, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x2271, "HP", ALC286_FIXUP_HP_GPIO_LED),
	SND_PCI_QUIRK(0x103c, 0x2272, "HP", ALC280_FIXUP_HP_DOCK_PINS),
	SND_PCI_QUIRK(0x103c, 0x2273, "HP", ALC280_FIXUP_HP_DOCK_PINS),
	SND_PCI_QUIRK(0x103c, 0x229e, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x22b2, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x22b7, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x22bf, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x22cf, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x22db, "HP", ALC280_FIXUP_HP_9480M),
	SND_PCI_QUIRK(0x103c, 0x22dc, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x22fb, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	/* ALC290 */
	SND_PCI_QUIRK(0x103c, 0x221b, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2221, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2225, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2253, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2254, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2255, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2256, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2257, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2259, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x225a, "HP", ALC269_FIXUP_HP_DOCK_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2260, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x2263, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x2264, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x2265, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x2272, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2273, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x2278, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED),
	SND_PCI_QUIRK(0x103c, 0x227f, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x2282, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x228b, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x228e, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x22c5, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x22c7, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x22c8, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x22c4, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x2334, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x2335, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x2336, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x2337, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1),
	SND_PCI_QUIRK(0x103c, 0x221c, "HP EliteBook 755 G2", ALC280_FIXUP_HP_HEADSET_MIC),
	SND_PCI_QUIRK(0x103c, 0x8256, "HP", ALC221_FIXUP_HP_FRONT_MIC),
	SND_PCI_QUIRK(0x1043, 0x103f, "ASUS TX300", ALC282_FIXUP_ASUS_TX300),
	SND_PCI_QUIRK(0x1043, 0x106d, "Asus K53BE", ALC269_FIXUP_LIMIT_INT_MIC_BOOST),
	SND_PCI_QUIRK(0x1043, 0x115d, "Asus 1015E", ALC269_FIXUP_LIMIT_INT_MIC_BOOST),
	SND_PCI_QUIRK(0x1043, 0x1427, "Asus Zenbook UX31E", ALC269VB_FIXUP_ASUS_ZENBOOK),
	SND_PCI_QUIRK(0x1043, 0x1517, "Asus Zenbook UX31A", ALC269VB_FIXUP_ASUS_ZENBOOK_UX31A),
	SND_PCI_QUIRK(0x1043, 0x16e3, "ASUS UX50", ALC269_FIXUP_STEREO_DMIC),
	SND_PCI_QUIRK(0x1043, 0x1a13, "Asus G73Jw", ALC269_FIXUP_ASUS_G73JW),
	SND_PCI_QUIRK(0x1043, 0x1b13, "Asus U41SV", ALC269_FIXUP_INV_DMIC),
	SND_PCI_QUIRK(0x1043, 0x1c23, "Asus X55U", ALC269_FIXUP_LIMIT_INT_MIC_BOOST),
	SND_PCI_QUIRK(0x1043, 0x831a, "ASUS P901", ALC269_FIXUP_STEREO_DMIC),
	SND_PCI_QUIRK(0x1043, 0x834a, "ASUS S101", ALC269_FIXUP_STEREO_DMIC),
	SND_PCI_QUIRK(0x1043, 0x8398, "ASUS P1005", ALC269_FIXUP_STEREO_DMIC),
	SND_PCI_QUIRK(0x1043, 0x83ce, "ASUS P1005", ALC269_FIXUP_STEREO_DMIC),
	SND_PCI_QUIRK(0x1043, 0x8516, "ASUS X101CH", ALC269_FIXUP_ASUS_X101),
	SND_PCI_QUIRK(0x104d, 0x9073, "Sony VAIO", ALC275_FIXUP_SONY_VAIO_GPIO2),
	SND_PCI_QUIRK(0x104d, 0x907b, "Sony VAIO", ALC275_FIXUP_SONY_HWEQ),
	SND_PCI_QUIRK(0x104d, 0x9084, "Sony VAIO", ALC275_FIXUP_SONY_HWEQ),
	SND_PCI_QUIRK(0x104d, 0x9099, "Sony VAIO S13", ALC275_FIXUP_SONY_DISABLE_AAMIX),
	SND_PCI_QUIRK(0x104d, 0x90b5, "Sony VAIO Pro 11", ALC286_FIXUP_SONY_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x104d, 0x90b6, "Sony VAIO Pro 13", ALC286_FIXUP_SONY_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x10cf, 0x1475, "Lifebook", ALC269_FIXUP_LIFEBOOK),
	SND_PCI_QUIRK(0x10cf, 0x159f, "Lifebook E780", ALC269_FIXUP_LIFEBOOK_NO_HP_TO_LINEOUT),
	SND_PCI_QUIRK(0x10cf, 0x15dc, "Lifebook T731", ALC269_FIXUP_LIFEBOOK_HP_PIN),
	SND_PCI_QUIRK(0x10cf, 0x1757, "Lifebook E752", ALC269_FIXUP_LIFEBOOK_HP_PIN),
	SND_PCI_QUIRK(0x10cf, 0x1629, "Lifebook U7x7", ALC255_FIXUP_LIFEBOOK_U7x7_HEADSET_MIC),
	SND_PCI_QUIRK(0x10cf, 0x1845, "Lifebook U904", ALC269_FIXUP_LIFEBOOK_EXTMIC),
	SND_PCI_QUIRK(0x144d, 0xc109, "Samsung Ativ book 9 (NP900X3G)", ALC269_FIXUP_INV_DMIC),
	SND_PCI_QUIRK(0x1458, 0xfa53, "Gigabyte BXBT-2807", ALC283_FIXUP_BXBT2807_MIC),
	SND_PCI_QUIRK(0x17aa, 0x20f2, "Thinkpad SL410/510", ALC269_FIXUP_SKU_IGNORE),
	SND_PCI_QUIRK(0x17aa, 0x215e, "Thinkpad L512", ALC269_FIXUP_SKU_IGNORE),
	SND_PCI_QUIRK(0x17aa, 0x21b8, "Thinkpad Edge 14", ALC269_FIXUP_SKU_IGNORE),
	SND_PCI_QUIRK(0x17aa, 0x21ca, "Thinkpad L412", ALC269_FIXUP_SKU_IGNORE),
	SND_PCI_QUIRK(0x17aa, 0x21e9, "Thinkpad Edge 15", ALC269_FIXUP_SKU_IGNORE),
	SND_PCI_QUIRK(0x17aa, 0x21f6, "Thinkpad T530", ALC269_FIXUP_LENOVO_DOCK_LIMIT_BOOST),
	SND_PCI_QUIRK(0x17aa, 0x21fa, "Thinkpad X230", ALC269_FIXUP_LENOVO_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x21f3, "Thinkpad T430", ALC269_FIXUP_LENOVO_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x21fb, "Thinkpad T430s", ALC269_FIXUP_LENOVO_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x2203, "Thinkpad X230 Tablet", ALC269_FIXUP_LENOVO_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x2208, "Thinkpad T431s", ALC269_FIXUP_LENOVO_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x220c, "Thinkpad T440s", ALC292_FIXUP_TPT440),
	SND_PCI_QUIRK(0x17aa, 0x220e, "Thinkpad T440p", ALC292_FIXUP_TPT440_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x2210, "Thinkpad T540p", ALC292_FIXUP_TPT440_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x2211, "Thinkpad W541", ALC292_FIXUP_TPT440_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x2212, "Thinkpad T440", ALC292_FIXUP_TPT440_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x2214, "Thinkpad X240", ALC292_FIXUP_TPT440_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x2215, "Thinkpad", ALC269_FIXUP_LIMIT_INT_MIC_BOOST),
	SND_PCI_QUIRK(0x17aa, 0x2218, "Thinkpad X1 Carbon 2nd", ALC292_FIXUP_TPT440_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x2223, "ThinkPad T550", ALC292_FIXUP_TPT440_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x2226, "ThinkPad X250", ALC292_FIXUP_TPT440_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x2231, "Thinkpad T560", ALC292_FIXUP_TPT460),
	SND_PCI_QUIRK(0x17aa, 0x2233, "Thinkpad", ALC292_FIXUP_TPT460),
	SND_PCI_QUIRK(0x17aa, 0x30bb, "ThinkCentre AIO", ALC233_FIXUP_LENOVO_LINE2_MIC_HOTKEY),
	SND_PCI_QUIRK(0x17aa, 0x30e2, "ThinkCentre AIO", ALC233_FIXUP_LENOVO_LINE2_MIC_HOTKEY),
	SND_PCI_QUIRK(0x17aa, 0x3112, "ThinkCentre AIO", ALC233_FIXUP_LENOVO_LINE2_MIC_HOTKEY),
	SND_PCI_QUIRK(0x17aa, 0x3902, "Lenovo E50-80", ALC269_FIXUP_DMIC_THINKPAD_ACPI),
	SND_PCI_QUIRK(0x17aa, 0x3977, "IdeaPad S210", ALC283_FIXUP_INT_MIC),
	SND_PCI_QUIRK(0x17aa, 0x3978, "Lenovo B50-70", ALC269_FIXUP_DMIC_THINKPAD_ACPI),
	SND_PCI_QUIRK(0x17aa, 0x5013, "Thinkpad", ALC269_FIXUP_LIMIT_INT_MIC_BOOST),
	SND_PCI_QUIRK(0x17aa, 0x501a, "Thinkpad", ALC283_FIXUP_INT_MIC),
	SND_PCI_QUIRK(0x17aa, 0x501e, "Thinkpad L440", ALC292_FIXUP_TPT440_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x5026, "Thinkpad", ALC269_FIXUP_LIMIT_INT_MIC_BOOST),
	SND_PCI_QUIRK(0x17aa, 0x5034, "Thinkpad T450", ALC292_FIXUP_TPT440_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x5036, "Thinkpad T450s", ALC292_FIXUP_TPT440_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x503c, "Thinkpad L450", ALC292_FIXUP_TPT440_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x504a, "ThinkPad X260", ALC292_FIXUP_TPT440_DOCK),
	SND_PCI_QUIRK(0x17aa, 0x504b, "Thinkpad", ALC293_FIXUP_LENOVO_SPK_NOISE),
	SND_PCI_QUIRK(0x17aa, 0x5050, "Thinkpad T560p", ALC292_FIXUP_TPT460),
	SND_PCI_QUIRK(0x17aa, 0x5051, "Thinkpad L460", ALC292_FIXUP_TPT460),
	SND_PCI_QUIRK(0x17aa, 0x5053, "Thinkpad T460", ALC292_FIXUP_TPT460),
	SND_PCI_QUIRK(0x17aa, 0x5109, "Thinkpad", ALC269_FIXUP_LIMIT_INT_MIC_BOOST),
	SND_PCI_QUIRK(0x17aa, 0x3bf8, "Quanta FL1", ALC269_FIXUP_PCM_44K),
	SND_PCI_QUIRK(0x17aa, 0x9e54, "LENOVO NB", ALC269_FIXUP_LENOVO_EAPD),
	SND_PCI_QUIRK(0x1b7d, 0xa831, "Ordissimo EVE2 ", ALC269VB_FIXUP_ORDISSIMO_EVE2), /* Also known as Malata PC-B1303 */

#if 0
	/* Below is a quirk table taken from the old code.
	 * Basically the device should work as is without the fixup table.
	 * If BIOS doesn't give a proper info, enable the corresponding
	 * fixup entry.
	 */
	SND_PCI_QUIRK(0x1043, 0x8330, "ASUS Eeepc P703 P900A",
		      ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1013, "ASUS N61Da", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1143, "ASUS B53f", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1133, "ASUS UJ20ft", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1183, "ASUS K72DR", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x11b3, "ASUS K52DR", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x11e3, "ASUS U33Jc", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1273, "ASUS UL80Jt", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1283, "ASUS U53Jc", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x12b3, "ASUS N82JV", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x12d3, "ASUS N61Jv", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x13a3, "ASUS UL30Vt", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1373, "ASUS G73JX", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1383, "ASUS UJ30Jc", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x13d3, "ASUS N61JA", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1413, "ASUS UL50", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1443, "ASUS UL30", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1453, "ASUS M60Jv", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1483, "ASUS UL80", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x14f3, "ASUS F83Vf", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x14e3, "ASUS UL20", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1513, "ASUS UX30", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1593, "ASUS N51Vn", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x15a3, "ASUS N60Jv", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x15b3, "ASUS N60Dp", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x15c3, "ASUS N70De", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x15e3, "ASUS F83T", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1643, "ASUS M60J", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1653, "ASUS U50", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1693, "ASUS F50N", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x16a3, "ASUS F5Q", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1723, "ASUS P80", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1743, "ASUS U80", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1773, "ASUS U20A", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1883, "ASUS F81Se", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x152d, 0x1778, "Quanta ON1", ALC269_FIXUP_DMIC),
	SND_PCI_QUIRK(0x17aa, 0x3be9, "Quanta Wistron", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x17aa, 0x3bf8, "Quanta FL1", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x17ff, 0x059a, "Quanta EL3", ALC269_FIXUP_DMIC),
	SND_PCI_QUIRK(0x17ff, 0x059b, "Quanta JR1", ALC269_FIXUP_DMIC),
#endif
	{}
};

static const struct snd_pci_quirk alc269_fixup_vendor_tbl[] = {
	SND_PCI_QUIRK_VENDOR(0x1025, "Acer Aspire", ALC271_FIXUP_DMIC),
	SND_PCI_QUIRK_VENDOR(0x103c, "HP", ALC269_FIXUP_HP_MUTE_LED),
	SND_PCI_QUIRK_VENDOR(0x104d, "Sony VAIO", ALC269_FIXUP_SONY_VAIO),
	SND_PCI_QUIRK_VENDOR(0x17aa, "Thinkpad", ALC269_FIXUP_THINKPAD_ACPI),
	{}
};

static const struct hda_model_fixup alc269_fixup_models[] = {
	{.id = ALC269_FIXUP_AMIC, .name = "laptop-amic"},
	{.id = ALC269_FIXUP_DMIC, .name = "laptop-dmic"},
	{.id = ALC269_FIXUP_STEREO_DMIC, .name = "alc269-dmic"},
	{.id = ALC271_FIXUP_DMIC, .name = "alc271-dmic"},
	{.id = ALC269_FIXUP_INV_DMIC, .name = "inv-dmic"},
	{.id = ALC269_FIXUP_HEADSET_MIC, .name = "headset-mic"},
	{.id = ALC269_FIXUP_HEADSET_MODE, .name = "headset-mode"},
	{.id = ALC269_FIXUP_HEADSET_MODE_NO_HP_MIC, .name = "headset-mode-no-hp-mic"},
	{.id = ALC269_FIXUP_LENOVO_DOCK, .name = "lenovo-dock"},
	{.id = ALC269_FIXUP_LENOVO_DOCK_LIMIT_BOOST, .name = "lenovo-dock-limit-boost"},
	{.id = ALC269_FIXUP_HP_GPIO_LED, .name = "hp-gpio-led"},
	{.id = ALC269_FIXUP_HP_DOCK_GPIO_MIC1_LED, .name = "hp-dock-gpio-mic1-led"},
	{.id = ALC269_FIXUP_DELL1_MIC_NO_PRESENCE, .name = "dell-headset-multi"},
	{.id = ALC269_FIXUP_DELL2_MIC_NO_PRESENCE, .name = "dell-headset-dock"},
	{.id = ALC283_FIXUP_CHROME_BOOK, .name = "alc283-dac-wcaps"},
	{.id = ALC283_FIXUP_SENSE_COMBO_JACK, .name = "alc283-sense-combo"},
	{.id = ALC292_FIXUP_TPT440_DOCK, .name = "tpt440-dock"},
	{.id = ALC292_FIXUP_TPT440, .name = "tpt440"},
	{.id = ALC292_FIXUP_TPT460, .name = "tpt460"},
	{}
};
#define ALC225_STANDARD_PINS \
	{0x21, 0x04211020}

#define ALC256_STANDARD_PINS \
	{0x12, 0x90a60140}, \
	{0x14, 0x90170110}, \
	{0x21, 0x02211020}

#define ALC282_STANDARD_PINS \
	{0x14, 0x90170110}

#define ALC290_STANDARD_PINS \
	{0x12, 0x99a30130}

#define ALC292_STANDARD_PINS \
	{0x14, 0x90170110}, \
	{0x15, 0x0221401f}

#define ALC295_STANDARD_PINS \
	{0x12, 0xb7a60130}, \
	{0x14, 0x90170110}, \
	{0x21, 0x04211020}

#define ALC298_STANDARD_PINS \
	{0x12, 0x90a60130}, \
	{0x21, 0x03211020}

static const struct snd_hda_pin_quirk alc269_pin_fixup_tbl[] = {
	SND_HDA_PIN_QUIRK(0x10ec0225, 0x1028, "Dell", ALC225_FIXUP_DELL1_MIC_NO_PRESENCE,
		ALC225_STANDARD_PINS,
		{0x12, 0xb7a60130},
		{0x14, 0x901701a0}),
	SND_HDA_PIN_QUIRK(0x10ec0225, 0x1028, "Dell", ALC225_FIXUP_DELL1_MIC_NO_PRESENCE,
		ALC225_STANDARD_PINS,
		{0x12, 0xb7a60130},
		{0x14, 0x901701b0}),
	SND_HDA_PIN_QUIRK(0x10ec0225, 0x1028, "Dell", ALC225_FIXUP_DELL1_MIC_NO_PRESENCE,
		ALC225_STANDARD_PINS,
		{0x12, 0xb7a60150},
		{0x14, 0x901701a0}),
	SND_HDA_PIN_QUIRK(0x10ec0225, 0x1028, "Dell", ALC225_FIXUP_DELL1_MIC_NO_PRESENCE,
		ALC225_STANDARD_PINS,
		{0x12, 0xb7a60150},
		{0x14, 0x901701b0}),
	SND_HDA_PIN_QUIRK(0x10ec0225, 0x1028, "Dell", ALC225_FIXUP_DELL1_MIC_NO_PRESENCE,
		ALC225_STANDARD_PINS,
		{0x12, 0xb7a60130},
		{0x1b, 0x90170110}),
	SND_HDA_PIN_QUIRK(0x10ec0236, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x12, 0x90a60140},
		{0x14, 0x90170110},
		{0x21, 0x02211020}),
	SND_HDA_PIN_QUIRK(0x10ec0236, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x12, 0x90a60140},
		{0x14, 0x90170150},
		{0x21, 0x02211020}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell", ALC255_FIXUP_DELL2_MIC_NO_PRESENCE,
		{0x14, 0x90170110},
		{0x21, 0x02211020}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x14, 0x90170130},
		{0x21, 0x02211040}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x12, 0x90a60140},
		{0x14, 0x90170110},
		{0x21, 0x02211020}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x12, 0x90a60160},
		{0x14, 0x90170120},
		{0x21, 0x02211030}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x14, 0x90170110},
		{0x1b, 0x02011020},
		{0x21, 0x0221101f}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x14, 0x90170110},
		{0x1b, 0x01011020},
		{0x21, 0x0221101f}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x14, 0x90170130},
		{0x1b, 0x01014020},
		{0x21, 0x0221103f}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x14, 0x90170130},
		{0x1b, 0x01011020},
		{0x21, 0x0221103f}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x14, 0x90170130},
		{0x1b, 0x02011020},
		{0x21, 0x0221103f}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x14, 0x90170150},
		{0x1b, 0x02011020},
		{0x21, 0x0221105f}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x14, 0x90170110},
		{0x1b, 0x01014020},
		{0x21, 0x0221101f}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x12, 0x90a60160},
		{0x14, 0x90170120},
		{0x17, 0x90170140},
		{0x21, 0x0321102f}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x12, 0x90a60160},
		{0x14, 0x90170130},
		{0x21, 0x02211040}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x12, 0x90a60160},
		{0x14, 0x90170140},
		{0x21, 0x02211050}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x12, 0x90a60170},
		{0x14, 0x90170120},
		{0x21, 0x02211030}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x12, 0x90a60170},
		{0x14, 0x90170130},
		{0x21, 0x02211040}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x12, 0x90a60170},
		{0x14, 0x90171130},
		{0x21, 0x02211040}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x12, 0x90a60170},
		{0x14, 0x90170140},
		{0x21, 0x02211050}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell Inspiron 5548", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x12, 0x90a60180},
		{0x14, 0x90170130},
		{0x21, 0x02211040}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell Inspiron 5565", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x12, 0x90a60180},
		{0x14, 0x90170120},
		{0x21, 0x02211030}),
	SND_HDA_PIN_QUIRK(0x10ec0255, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x1b, 0x01011020},
		{0x21, 0x02211010}),
	SND_HDA_PIN_QUIRK(0x10ec0256, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x12, 0x90a60130},
		{0x14, 0x90170110},
		{0x1b, 0x01011020},
		{0x21, 0x0221101f}),
	SND_HDA_PIN_QUIRK(0x10ec0256, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x12, 0x90a60160},
		{0x14, 0x90170120},
		{0x21, 0x02211030}),
	SND_HDA_PIN_QUIRK(0x10ec0256, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x12, 0x90a60170},
		{0x14, 0x90170120},
		{0x21, 0x02211030}),
	SND_HDA_PIN_QUIRK(0x10ec0256, 0x1028, "Dell Inspiron 5468", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x12, 0x90a60180},
		{0x14, 0x90170120},
		{0x21, 0x02211030}),
	SND_HDA_PIN_QUIRK(0x10ec0256, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x12, 0xb7a60130},
		{0x14, 0x90170110},
		{0x21, 0x02211020}),
	SND_HDA_PIN_QUIRK(0x10ec0256, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x12, 0x90a60130},
		{0x14, 0x90170110},
		{0x14, 0x01011020},
		{0x21, 0x0221101f}),
	SND_HDA_PIN_QUIRK(0x10ec0256, 0x1028, "Dell", ALC255_FIXUP_DELL1_MIC_NO_PRESENCE,
		ALC256_STANDARD_PINS),
	SND_HDA_PIN_QUIRK(0x10ec0280, 0x103c, "HP", ALC280_FIXUP_HP_GPIO4,
		{0x12, 0x90a60130},
		{0x14, 0x90170110},
		{0x15, 0x0421101f},
		{0x1a, 0x04a11020}),
	SND_HDA_PIN_QUIRK(0x10ec0280, 0x103c, "HP", ALC269_FIXUP_HP_GPIO_MIC1_LED,
		{0x12, 0x90a60140},
		{0x14, 0x90170110},
		{0x15, 0x0421101f},
		{0x18, 0x02811030},
		{0x1a, 0x04a1103f},
		{0x1b, 0x02011020}),
	SND_HDA_PIN_QUIRK(0x10ec0282, 0x103c, "HP 15 Touchsmart", ALC269_FIXUP_HP_MUTE_LED_MIC1,
		ALC282_STANDARD_PINS,
		{0x12, 0x99a30130},
		{0x19, 0x03a11020},
		{0x21, 0x0321101f}),
	SND_HDA_PIN_QUIRK(0x10ec0282, 0x103c, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1,
		ALC282_STANDARD_PINS,
		{0x12, 0x99a30130},
		{0x19, 0x03a11020},
		{0x21, 0x03211040}),
	SND_HDA_PIN_QUIRK(0x10ec0282, 0x103c, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1,
		ALC282_STANDARD_PINS,
		{0x12, 0x99a30130},
		{0x19, 0x03a11030},
		{0x21, 0x03211020}),
	SND_HDA_PIN_QUIRK(0x10ec0282, 0x103c, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1,
		ALC282_STANDARD_PINS,
		{0x12, 0x99a30130},
		{0x19, 0x04a11020},
		{0x21, 0x0421101f}),
	SND_HDA_PIN_QUIRK(0x10ec0282, 0x103c, "HP", ALC269_FIXUP_HP_LINE1_MIC1_LED,
		ALC282_STANDARD_PINS,
		{0x12, 0x90a60140},
		{0x19, 0x04a11030},
		{0x21, 0x04211020}),
	SND_HDA_PIN_QUIRK(0x10ec0283, 0x1028, "Dell", ALC269_FIXUP_DELL1_MIC_NO_PRESENCE,
		ALC282_STANDARD_PINS,
		{0x12, 0x90a60130},
		{0x21, 0x0321101f}),
	SND_HDA_PIN_QUIRK(0x10ec0283, 0x1028, "Dell", ALC269_FIXUP_DELL1_MIC_NO_PRESENCE,
		{0x12, 0x90a60160},
		{0x14, 0x90170120},
		{0x21, 0x02211030}),
	SND_HDA_PIN_QUIRK(0x10ec0283, 0x1028, "Dell", ALC269_FIXUP_DELL1_MIC_NO_PRESENCE,
		ALC282_STANDARD_PINS,
		{0x12, 0x90a60130},
		{0x19, 0x03a11020},
		{0x21, 0x0321101f}),
	SND_HDA_PIN_QUIRK(0x10ec0288, 0x1028, "Dell", ALC288_FIXUP_DELL_XPS_13_GPIO6,
		{0x12, 0x90a60120},
		{0x14, 0x90170110},
		{0x21, 0x0321101f}),
	SND_HDA_PIN_QUIRK(0x10ec0289, 0x1028, "Dell", ALC269_FIXUP_DELL4_MIC_NO_PRESENCE,
		{0x12, 0xb7a60130},
		{0x14, 0x90170110},
		{0x21, 0x04211020}),
	SND_HDA_PIN_QUIRK(0x10ec0290, 0x103c, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1,
		ALC290_STANDARD_PINS,
		{0x15, 0x04211040},
		{0x18, 0x90170112},
		{0x1a, 0x04a11020}),
	SND_HDA_PIN_QUIRK(0x10ec0290, 0x103c, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1,
		ALC290_STANDARD_PINS,
		{0x15, 0x04211040},
		{0x18, 0x90170110},
		{0x1a, 0x04a11020}),
	SND_HDA_PIN_QUIRK(0x10ec0290, 0x103c, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1,
		ALC290_STANDARD_PINS,
		{0x15, 0x0421101f},
		{0x1a, 0x04a11020}),
	SND_HDA_PIN_QUIRK(0x10ec0290, 0x103c, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1,
		ALC290_STANDARD_PINS,
		{0x15, 0x04211020},
		{0x1a, 0x04a11040}),
	SND_HDA_PIN_QUIRK(0x10ec0290, 0x103c, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1,
		ALC290_STANDARD_PINS,
		{0x14, 0x90170110},
		{0x15, 0x04211020},
		{0x1a, 0x04a11040}),
	SND_HDA_PIN_QUIRK(0x10ec0290, 0x103c, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1,
		ALC290_STANDARD_PINS,
		{0x14, 0x90170110},
		{0x15, 0x04211020},
		{0x1a, 0x04a11020}),
	SND_HDA_PIN_QUIRK(0x10ec0290, 0x103c, "HP", ALC269_FIXUP_HP_MUTE_LED_MIC1,
		ALC290_STANDARD_PINS,
		{0x14, 0x90170110},
		{0x15, 0x0421101f},
		{0x1a, 0x04a11020}),
	SND_HDA_PIN_QUIRK(0x10ec0292, 0x1028, "Dell", ALC269_FIXUP_DELL2_MIC_NO_PRESENCE,
		ALC292_STANDARD_PINS,
		{0x12, 0x90a60140},
		{0x16, 0x01014020},
		{0x19, 0x01a19030}),
	SND_HDA_PIN_QUIRK(0x10ec0292, 0x1028, "Dell", ALC269_FIXUP_DELL2_MIC_NO_PRESENCE,
		ALC292_STANDARD_PINS,
		{0x12, 0x90a60140},
		{0x16, 0x01014020},
		{0x18, 0x02a19031},
		{0x19, 0x01a1903e}),
	SND_HDA_PIN_QUIRK(0x10ec0292, 0x1028, "Dell", ALC269_FIXUP_DELL3_MIC_NO_PRESENCE,
		ALC292_STANDARD_PINS,
		{0x12, 0x90a60140}),
	SND_HDA_PIN_QUIRK(0x10ec0293, 0x1028, "Dell", ALC293_FIXUP_DELL1_MIC_NO_PRESENCE,
		ALC292_STANDARD_PINS,
		{0x13, 0x90a60140},
		{0x16, 0x21014020},
		{0x19, 0x21a19030}),
	SND_HDA_PIN_QUIRK(0x10ec0293, 0x1028, "Dell", ALC293_FIXUP_DELL1_MIC_NO_PRESENCE,
		ALC292_STANDARD_PINS,
		{0x13, 0x90a60140}),
	SND_HDA_PIN_QUIRK(0x10ec0295, 0x1028, "Dell", ALC269_FIXUP_DELL1_MIC_NO_PRESENCE,
		ALC295_STANDARD_PINS,
		{0x17, 0x21014020},
		{0x18, 0x21a19030}),
	SND_HDA_PIN_QUIRK(0x10ec0295, 0x1028, "Dell", ALC269_FIXUP_DELL1_MIC_NO_PRESENCE,
		ALC295_STANDARD_PINS,
		{0x17, 0x21014040},
		{0x18, 0x21a19050}),
	SND_HDA_PIN_QUIRK(0x10ec0295, 0x1028, "Dell", ALC269_FIXUP_DELL1_MIC_NO_PRESENCE,
		ALC295_STANDARD_PINS),
	SND_HDA_PIN_QUIRK(0x10ec0298, 0x1028, "Dell", ALC298_FIXUP_DELL1_MIC_NO_PRESENCE,
		ALC298_STANDARD_PINS,
		{0x17, 0x90170110}),
	SND_HDA_PIN_QUIRK(0x10ec0298, 0x1028, "Dell", ALC298_FIXUP_DELL1_MIC_NO_PRESENCE,
		ALC298_STANDARD_PINS,
		{0x17, 0x90170140}),
	SND_HDA_PIN_QUIRK(0x10ec0298, 0x1028, "Dell", ALC298_FIXUP_DELL1_MIC_NO_PRESENCE,
		ALC298_STANDARD_PINS,
		{0x17, 0x90170150}),
	SND_HDA_PIN_QUIRK(0x10ec0298, 0x1028, "Dell", ALC298_FIXUP_SPK_VOLUME,
		{0x12, 0xb7a60140},
		{0x13, 0xb7a60150},
		{0x17, 0x90170110},
		{0x1a, 0x03011020},
		{0x21, 0x03211030}),
	SND_HDA_PIN_QUIRK(0x10ec0299, 0x1028, "Dell", ALC269_FIXUP_DELL4_MIC_NO_PRESENCE,
		ALC225_STANDARD_PINS,
		{0x12, 0xb7a60130},
		{0x17, 0x90170110}),
	{}
};

static void alc269_fill_coef(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int val;

	if (spec->codec_variant != ALC269_TYPE_ALC269VB)
		return;

	if ((alc_get_coef0(codec) & 0x00ff) < 0x015) {
		alc_write_coef_idx(codec, 0xf, 0x960b);
		alc_write_coef_idx(codec, 0xe, 0x8817);
	}

	if ((alc_get_coef0(codec) & 0x00ff) == 0x016) {
		alc_write_coef_idx(codec, 0xf, 0x960b);
		alc_write_coef_idx(codec, 0xe, 0x8814);
	}

	if ((alc_get_coef0(codec) & 0x00ff) == 0x017) {
		/* Power up output pin */
		alc_update_coef_idx(codec, 0x04, 0, 1<<11);
	}

	if ((alc_get_coef0(codec) & 0x00ff) == 0x018) {
		val = alc_read_coef_idx(codec, 0xd);
		if (val != -1 && (val & 0x0c00) >> 10 != 0x1) {
			/* Capless ramp up clock control */
			alc_write_coef_idx(codec, 0xd, val | (1<<10));
		}
		val = alc_read_coef_idx(codec, 0x17);
		if (val != -1 && (val & 0x01c0) >> 6 != 0x4) {
			/* Class D power on reset */
			alc_write_coef_idx(codec, 0x17, val | (1<<7));
		}
	}

	/* HP */
	alc_update_coef_idx(codec, 0x4, 0, 1<<11);
}

/*
 */
static int patch_alc269(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err;

	err = alc_alloc_spec(codec, 0x0b);
	if (err < 0)
		return err;

	spec = codec->spec;
	spec->gen.shared_mic_vref_pin = 0x18;
	codec->power_save_node = 0;

#ifdef CONFIG_PM
	codec->patch_ops.suspend = alc269_suspend;
	codec->patch_ops.resume = alc269_resume;
#endif
	spec->shutup = alc269_shutup;

	snd_hda_pick_fixup(codec, alc269_fixup_models,
		       alc269_fixup_tbl, alc269_fixups);
	snd_hda_pick_pin_fixup(codec, alc269_pin_fixup_tbl, alc269_fixups);
	snd_hda_pick_fixup(codec, NULL,	alc269_fixup_vendor_tbl,
			   alc269_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	alc_auto_parse_customize_define(codec);

	if (has_cdefine_beep(codec))
		spec->gen.beep_nid = 0x01;

	switch (codec->core.vendor_id) {
	case 0x10ec0269:
		spec->codec_variant = ALC269_TYPE_ALC269VA;
		switch (alc_get_coef0(codec) & 0x00f0) {
		case 0x0010:
			if (codec->bus->pci &&
			    codec->bus->pci->subsystem_vendor == 0x1025 &&
			    spec->cdefine.platform_type == 1)
				err = alc_codec_rename(codec, "ALC271X");
			spec->codec_variant = ALC269_TYPE_ALC269VB;
			break;
		case 0x0020:
			if (codec->bus->pci &&
			    codec->bus->pci->subsystem_vendor == 0x17aa &&
			    codec->bus->pci->subsystem_device == 0x21f3)
				err = alc_codec_rename(codec, "ALC3202");
			spec->codec_variant = ALC269_TYPE_ALC269VC;
			break;
		case 0x0030:
			spec->codec_variant = ALC269_TYPE_ALC269VD;
			break;
		default:
			alc_fix_pll_init(codec, 0x20, 0x04, 15);
		}
		if (err < 0)
			goto error;
		spec->init_hook = alc269_fill_coef;
		alc269_fill_coef(codec);
		break;

	case 0x10ec0280:
	case 0x10ec0290:
		spec->codec_variant = ALC269_TYPE_ALC280;
		break;
	case 0x10ec0282:
		spec->codec_variant = ALC269_TYPE_ALC282;
		spec->shutup = alc282_shutup;
		spec->init_hook = alc282_init;
		break;
	case 0x10ec0233:
	case 0x10ec0283:
		spec->codec_variant = ALC269_TYPE_ALC283;
		spec->shutup = alc283_shutup;
		spec->init_hook = alc283_init;
		break;
	case 0x10ec0284:
	case 0x10ec0292:
		spec->codec_variant = ALC269_TYPE_ALC284;
		break;
	case 0x10ec0285:
	case 0x10ec0293:
		spec->codec_variant = ALC269_TYPE_ALC285;
		break;
	case 0x10ec0286:
	case 0x10ec0288:
		spec->codec_variant = ALC269_TYPE_ALC286;
		spec->shutup = alc286_shutup;
		break;
	case 0x10ec0298:
		spec->codec_variant = ALC269_TYPE_ALC298;
		break;
	case 0x10ec0235:
	case 0x10ec0255:
		spec->codec_variant = ALC269_TYPE_ALC255;
		break;
	case 0x10ec0236:
	case 0x10ec0256:
		spec->codec_variant = ALC269_TYPE_ALC256;
		spec->gen.mixer_nid = 0; /* ALC256 does not have any loopback mixer path */
		alc_update_coef_idx(codec, 0x36, 1 << 13, 1 << 5); /* Switch pcbeep path to Line in path*/
		break;
	case 0x10ec0225:
	case 0x10ec0295:
		spec->codec_variant = ALC269_TYPE_ALC225;
		break;
	case 0x10ec0299:
		spec->codec_variant = ALC269_TYPE_ALC225;
		spec->gen.mixer_nid = 0; /* no loopback on ALC299 */
		break;
	case 0x10ec0234:
	case 0x10ec0274:
	case 0x10ec0294:
		spec->codec_variant = ALC269_TYPE_ALC294;
		break;
	case 0x10ec0700:
	case 0x10ec0701:
	case 0x10ec0703:
		spec->codec_variant = ALC269_TYPE_ALC700;
		spec->gen.mixer_nid = 0; /* ALC700 does not have any loopback mixer path */
		alc_update_coef_idx(codec, 0x4a, 1 << 15, 0); /* Combo jack auto trigger control */
		break;

	}

	if (snd_hda_codec_read(codec, 0x51, 0, AC_VERB_PARAMETERS, 0) == 0x10ec5505) {
		spec->has_alc5505_dsp = 1;
		spec->init_hook = alc5505_dsp_init;
	}

	/* automatic parse from the BIOS config */
	err = alc269_parse_auto_config(codec);
	if (err < 0)
		goto error;

	if (!spec->gen.no_analog && spec->gen.beep_nid && spec->gen.mixer_nid)
		set_beep_amp(spec, spec->gen.mixer_nid, 0x04, HDA_INPUT);

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;

 error:
	alc_free(codec);
	return err;
}

/*
 * ALC861
 */

static int alc861_parse_auto_config(struct hda_codec *codec)
{
	static const hda_nid_t alc861_ignore[] = { 0x1d, 0 };
	static const hda_nid_t alc861_ssids[] = { 0x0e, 0x0f, 0x0b, 0 };
	return alc_parse_auto_config(codec, alc861_ignore, alc861_ssids);
}

/* Pin config fixes */
enum {
	ALC861_FIXUP_FSC_AMILO_PI1505,
	ALC861_FIXUP_AMP_VREF_0F,
	ALC861_FIXUP_NO_JACK_DETECT,
	ALC861_FIXUP_ASUS_A6RP,
	ALC660_FIXUP_ASUS_W7J,
};

/* On some laptops, VREF of pin 0x0f is abused for controlling the main amp */
static void alc861_fixup_asus_amp_vref_0f(struct hda_codec *codec,
			const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	unsigned int val;

	if (action != HDA_FIXUP_ACT_INIT)
		return;
	val = snd_hda_codec_get_pin_target(codec, 0x0f);
	if (!(val & (AC_PINCTL_IN_EN | AC_PINCTL_OUT_EN)))
		val |= AC_PINCTL_IN_EN;
	val |= AC_PINCTL_VREF_50;
	snd_hda_set_pin_ctl(codec, 0x0f, val);
	spec->gen.keep_vref_in_automute = 1;
}

/* suppress the jack-detection */
static void alc_fixup_no_jack_detect(struct hda_codec *codec,
				     const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		codec->no_jack_detect = 1;
}

static const struct hda_fixup alc861_fixups[] = {
	[ALC861_FIXUP_FSC_AMILO_PI1505] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x0b, 0x0221101f }, /* HP */
			{ 0x0f, 0x90170310 }, /* speaker */
			{ }
		}
	},
	[ALC861_FIXUP_AMP_VREF_0F] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc861_fixup_asus_amp_vref_0f,
	},
	[ALC861_FIXUP_NO_JACK_DETECT] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_no_jack_detect,
	},
	[ALC861_FIXUP_ASUS_A6RP] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc861_fixup_asus_amp_vref_0f,
		.chained = true,
		.chain_id = ALC861_FIXUP_NO_JACK_DETECT,
	},
	[ALC660_FIXUP_ASUS_W7J] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* ASUS W7J needs a magic pin setup on unused NID 0x10
			 * for enabling outputs
			 */
			{0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},
			{ }
		},
	}
};

static const struct snd_pci_quirk alc861_fixup_tbl[] = {
	SND_PCI_QUIRK(0x1043, 0x1253, "ASUS W7J", ALC660_FIXUP_ASUS_W7J),
	SND_PCI_QUIRK(0x1043, 0x1263, "ASUS Z35HL", ALC660_FIXUP_ASUS_W7J),
	SND_PCI_QUIRK(0x1043, 0x1393, "ASUS A6Rp", ALC861_FIXUP_ASUS_A6RP),
	SND_PCI_QUIRK_VENDOR(0x1043, "ASUS laptop", ALC861_FIXUP_AMP_VREF_0F),
	SND_PCI_QUIRK(0x1462, 0x7254, "HP DX2200", ALC861_FIXUP_NO_JACK_DETECT),
	SND_PCI_QUIRK_VENDOR(0x1584, "Haier/Uniwill", ALC861_FIXUP_AMP_VREF_0F),
	SND_PCI_QUIRK(0x1734, 0x10c7, "FSC Amilo Pi1505", ALC861_FIXUP_FSC_AMILO_PI1505),
	{}
};

/*
 */
static int patch_alc861(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err;

	err = alc_alloc_spec(codec, 0x15);
	if (err < 0)
		return err;

	spec = codec->spec;
	spec->gen.beep_nid = 0x23;

#ifdef CONFIG_PM
	spec->power_hook = alc_power_eapd;
#endif

	snd_hda_pick_fixup(codec, NULL, alc861_fixup_tbl, alc861_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	/* automatic parse from the BIOS config */
	err = alc861_parse_auto_config(codec);
	if (err < 0)
		goto error;

	if (!spec->gen.no_analog)
		set_beep_amp(spec, 0x23, 0, HDA_OUTPUT);

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;

 error:
	alc_free(codec);
	return err;
}

/*
 * ALC861-VD support
 *
 * Based on ALC882
 *
 * In addition, an independent DAC
 */
#define ALC861VD_DIGOUT_NID	0x06

static hda_nid_t alc861vd_dac_nids[4] = {
	/* front, surr, clfe, side surr */
	0x02, 0x03, 0x04, 0x05
};

/* dac_nids for ALC660vd are in a different order - according to
 * Realtek's driver.
 * This should probably tesult in a different mixer for 6stack models
 * of ALC660vd codecs, but for now there is only 3stack mixer
 * - and it is the same as in 861vd.
 * adc_nids in ALC660vd are (is) the same as in 861vd
 */
static hda_nid_t alc660vd_dac_nids[3] = {
	/* front, rear, clfe, rear_surr */
	0x02, 0x04, 0x03
};

static hda_nid_t alc861vd_adc_nids[1] = {
	/* ADC0 */
	0x09,
};

static hda_nid_t alc861vd_capsrc_nids[1] = { 0x22 };

/* input MUX */
/* FIXME: should be a matrix-type input source selection */
static struct hda_input_mux alc861vd_capture_source = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x1 },
		{ "Line", 0x2 },
		{ "CD", 0x4 },
	},
};

static struct hda_input_mux alc861vd_dallas_capture_source = {
	.num_items = 2,
	.items = {
		{ "Ext Mic", 0x0 },
		{ "Int Mic", 0x1 },
	},
};

static struct hda_input_mux alc861vd_hp_capture_source = {
	.num_items = 2,
	.items = {
		{ "Front Mic", 0x0 },
		{ "ATAPI Mic", 0x1 },
	},
};

#define alc861vd_mux_enum_info alc_mux_enum_info
#define alc861vd_mux_enum_get alc_mux_enum_get
/* ALC861VD has the ALC882-type input selection (but has only one ADC) */
#define alc861vd_mux_enum_put alc882_mux_enum_put

/*
 * 2ch mode
 */
static struct hda_channel_mode alc861vd_3stack_2ch_modes[1] = {
	{ 2, NULL }
};

/*
 * 6ch mode
 */
static struct hda_verb alc861vd_6stack_ch6_init[] = {
	{ 0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00 },
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ } /* end */
};

/*
 * 8ch mode
 */
static struct hda_verb alc861vd_6stack_ch8_init[] = {
	{ 0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ } /* end */
};

static struct hda_channel_mode alc861vd_6stack_modes[2] = {
	{ 6, alc861vd_6stack_ch6_init },
	{ 8, alc861vd_6stack_ch8_init },
};

static struct snd_kcontrol_new alc861vd_chmode_mixer[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc861vd_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x09, 0x0, HDA_INPUT),

	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 1,
		.info = alc861vd_mux_enum_info,
		.get = alc861vd_mux_enum_get,
		.put = alc861vd_mux_enum_put,
	},
	{ } /* end */
};

/* Pin assignment: Front=0x14, Rear=0x15, CLFE=0x16, Side=0x17
 *                 Mic=0x18, Front Mic=0x19, Line-In=0x1a, HP=0x1b
 */
static struct snd_kcontrol_new alc861vd_6st_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),

	HDA_CODEC_VOLUME("Surround Playback Volume", 0x03, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 2, HDA_INPUT),

	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x04, 1, 0x0,
				HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x04, 2, 0x0,
				HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),

	HDA_CODEC_VOLUME("Side Playback Volume", 0x05, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Side Playback Switch", 0x0f, 2, HDA_INPUT),

	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),

	HDA_CODEC_VOLUME("Front Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),

	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),

	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),

	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x0b, 0x05, HDA_INPUT),

	{ } /* end */
};

static struct snd_kcontrol_new alc861vd_3st_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),

	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),

	HDA_CODEC_VOLUME("Front Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),

	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),

	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),

	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x0b, 0x05, HDA_INPUT),

	{ } /* end */
};

static struct snd_kcontrol_new alc861vd_lenovo_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	/*HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),*/
	HDA_CODEC_MUTE("Front Playback Switch", 0x14, 0x0, HDA_OUTPUT),

	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),

	HDA_CODEC_VOLUME("Front Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),

	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),

	{ } /* end */
};

/* Pin assignment: Speaker=0x14, HP = 0x15,
 *                 Ext Mic=0x18, Int Mic = 0x19, CD = 0x1c, PC Beep = 0x1d
 */
static struct snd_kcontrol_new alc861vd_dallas_mixer[] = {
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x03, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Ext Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Ext Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Ext Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Int Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Int Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Int Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Beep Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Beep Switch", 0x0b, 0x05, HDA_INPUT),
	{ } /* end */
};

/* Pin assignment: Speaker=0x14, Line-out = 0x15,
 *                 Front Mic=0x18, ATAPI Mic = 0x19,
 */
static struct snd_kcontrol_new alc861vd_hp_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x03, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("ATAPI Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("ATAPI Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	
	{ } /* end */
};

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static struct hda_verb alc861vd_volume_init_verbs[] = {
	/*
	 * Unmute ADC0 and set the default input to mic-in
	 */
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	/* Unmute input amps (CD, Line In, Mic 1 & Mic 2) of
	 * the analog-loopback mixer widget
	 */
	/* Amp Indices: Mic1 = 0, Mic2 = 1, Line1 = 2, Line2 = 3, CD = 4 */
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},

	/* Capture mixer: unmute Mic, F-Mic, Line, CD inputs */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(4)},

	/*
	 * Set up output mixers (0x02 - 0x05)
	 */
	/* set vol=0 to output mixers */
	{0x02, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},

	/* set up input amps for analog loopback */
	/* Amp Indices: DAC = 0, mixer = 1 */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},

	{ }
};

/*
 * 3-stack pin configuration:
 * front = 0x14, mic/clfe = 0x18, HP = 0x19, line/surr = 0x1a, f-mic = 0x1b
 */
static struct hda_verb alc861vd_3stack_init_verbs[] = {
	/*
	 * Set pin mode and muting
	 */
	/* set front pin widgets 0x14 for output */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},

	/* Mic (rear) pin: input vref at 80% */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Front Mic pin: input vref at 80% */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Line In pin: input */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Line-2 In: Headphone output (output 0 - 0x0c) */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* CD pin widget for input */
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

	{ }
};

/*
 * 6-stack pin configuration:
 */
static struct hda_verb alc861vd_6stack_init_verbs[] = {
	/*
	 * Set pin mode and muting
	 */
	/* set front pin widgets 0x14 for output */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},

	/* Rear Pin: output 1 (0x0d) */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x01},
	/* CLFE Pin: output 2 (0x0e) */
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_CONNECT_SEL, 0x02},
	/* Side Pin: output 3 (0x0f) */
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x17, AC_VERB_SET_CONNECT_SEL, 0x03},

	/* Mic (rear) pin: input vref at 80% */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Front Mic pin: input vref at 80% */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Line In pin: input */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Line-2 In: Headphone output (output 0 - 0x0c) */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* CD pin widget for input */
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

	{ }
};

static struct hda_verb alc861vd_eapd_verbs[] = {
	{0x14, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{ }
};

static struct hda_verb alc660vd_eapd_verbs[] = {
	{0x14, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{0x15, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{ }
};

static struct hda_verb alc861vd_lenovo_unsol_verbs[] = {
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(5)},
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},	
	{}
};

/* toggle speaker-output according to the hp-jack state */
static void alc861vd_lenovo_hp_automute(struct hda_codec *codec)
{
	unsigned int present;
	unsigned char bits;

	present = snd_hda_codec_read(codec, 0x1b, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
}

static void alc861vd_lenovo_mic_automute(struct hda_codec *codec)
{
	unsigned int present;
	unsigned char bits;

	present = snd_hda_codec_read(codec, 0x18, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x0b, HDA_INPUT, 1,
				 HDA_AMP_MUTE, bits);
}

static void alc861vd_lenovo_automute(struct hda_codec *codec)
{
	alc861vd_lenovo_hp_automute(codec);
	alc861vd_lenovo_mic_automute(codec);
}

static void alc861vd_lenovo_unsol_event(struct hda_codec *codec,
					unsigned int res)
{
	switch (res >> 26) {
	case ALC880_HP_EVENT:
		alc861vd_lenovo_hp_automute(codec);
		break;
	case ALC880_MIC_EVENT:
		alc861vd_lenovo_mic_automute(codec);
		break;
	}
}

static struct hda_verb alc861vd_dallas_verbs[] = {
	{0x02, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},

	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF50},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF50},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1d, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},

	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},	
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},

	{ } /* end */
};

/* toggle speaker-output according to the hp-jack state */
static void alc861vd_dallas_automute(struct hda_codec *codec)
{
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x15, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
}

static void alc861vd_dallas_unsol_event(struct hda_codec *codec, unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc861vd_dallas_automute(codec);
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
#define alc861vd_loopbacks	alc880_loopbacks
#endif

/* pcm configuration: identiacal with ALC880 */
#define alc861vd_pcm_analog_playback	alc880_pcm_analog_playback
#define alc861vd_pcm_analog_capture	alc880_pcm_analog_capture
#define alc861vd_pcm_digital_playback	alc880_pcm_digital_playback
#define alc861vd_pcm_digital_capture	alc880_pcm_digital_capture

/*
 * configuration and preset
 */
static const char *alc861vd_models[ALC861VD_MODEL_LAST] = {
	[ALC660VD_3ST]		= "3stack-660",
	[ALC660VD_3ST_DIG]	= "3stack-660-digout",
	[ALC861VD_3ST]		= "3stack",
	[ALC861VD_3ST_DIG]	= "3stack-digout",
	[ALC861VD_6ST_DIG]	= "6stack-digout",
	[ALC861VD_LENOVO]	= "lenovo",
	[ALC861VD_DALLAS]	= "dallas",
	[ALC861VD_HP]		= "hp",
	[ALC861VD_AUTO]		= "auto",
};

static struct snd_pci_quirk alc861vd_cfg_tbl[] = {
	SND_PCI_QUIRK(0x1019, 0xa88d, "Realtek ALC660 demo", ALC660VD_3ST),
	SND_PCI_QUIRK(0x103c, 0x30bf, "HP TX1000", ALC861VD_HP),
	SND_PCI_QUIRK(0x1043, 0x12e2, "Asus z35m", ALC660VD_3ST),
	SND_PCI_QUIRK(0x1043, 0x1339, "Asus G1", ALC660VD_3ST),
	SND_PCI_QUIRK(0x1043, 0x1633, "Asus V1Sn", ALC861VD_LENOVO),
	SND_PCI_QUIRK(0x1043, 0x81e7, "ASUS", ALC660VD_3ST_DIG),
	SND_PCI_QUIRK(0x10de, 0x03f0, "Realtek ALC660 demo", ALC660VD_3ST),
	SND_PCI_QUIRK(0x1179, 0xff00, "Toshiba A135", ALC861VD_LENOVO),
	/*SND_PCI_QUIRK(0x1179, 0xff00, "DALLAS", ALC861VD_DALLAS),*/ /*lenovo*/
	SND_PCI_QUIRK(0x1179, 0xff01, "DALLAS", ALC861VD_DALLAS),
	SND_PCI_QUIRK(0x1179, 0xff03, "Toshiba P205", ALC861VD_LENOVO),
	SND_PCI_QUIRK(0x1179, 0xff31, "Toshiba L30-149", ALC861VD_DALLAS),
	SND_PCI_QUIRK(0x1565, 0x820d, "Biostar NF61S SE", ALC861VD_6ST_DIG),
	SND_PCI_QUIRK(0x17aa, 0x2066, "Lenovo", ALC861VD_LENOVO),
	SND_PCI_QUIRK(0x17aa, 0x3802, "Lenovo 3000 C200", ALC861VD_LENOVO),
	SND_PCI_QUIRK(0x17aa, 0x384e, "Lenovo 3000 N200", ALC861VD_LENOVO),
	SND_PCI_QUIRK(0x1849, 0x0862, "ASRock K8NF6G-VSTA", ALC861VD_6ST_DIG),
	{}
};

static struct alc_config_preset alc861vd_presets[] = {
	[ALC660VD_3ST] = {
		.mixers = { alc861vd_3st_mixer },
		.init_verbs = { alc861vd_volume_init_verbs,
				 alc861vd_3stack_init_verbs },
		.num_dacs = ARRAY_SIZE(alc660vd_dac_nids),
		.dac_nids = alc660vd_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc861vd_3stack_2ch_modes),
		.channel_mode = alc861vd_3stack_2ch_modes,
		.input_mux = &alc861vd_capture_source,
	},
	[ALC660VD_3ST_DIG] = {
		.mixers = { alc861vd_3st_mixer },
		.init_verbs = { alc861vd_volume_init_verbs,
				 alc861vd_3stack_init_verbs },
		.num_dacs = ARRAY_SIZE(alc660vd_dac_nids),
		.dac_nids = alc660vd_dac_nids,
		.dig_out_nid = ALC861VD_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc861vd_3stack_2ch_modes),
		.channel_mode = alc861vd_3stack_2ch_modes,
		.input_mux = &alc861vd_capture_source,
	},
	[ALC861VD_3ST] = {
		.mixers = { alc861vd_3st_mixer },
		.init_verbs = { alc861vd_volume_init_verbs,
				 alc861vd_3stack_init_verbs },
		.num_dacs = ARRAY_SIZE(alc861vd_dac_nids),
		.dac_nids = alc861vd_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc861vd_3stack_2ch_modes),
		.channel_mode = alc861vd_3stack_2ch_modes,
		.input_mux = &alc861vd_capture_source,
	},
	[ALC861VD_3ST_DIG] = {
		.mixers = { alc861vd_3st_mixer },
		.init_verbs = { alc861vd_volume_init_verbs,
		 		 alc861vd_3stack_init_verbs },
		.num_dacs = ARRAY_SIZE(alc861vd_dac_nids),
		.dac_nids = alc861vd_dac_nids,
		.dig_out_nid = ALC861VD_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc861vd_3stack_2ch_modes),
		.channel_mode = alc861vd_3stack_2ch_modes,
		.input_mux = &alc861vd_capture_source,
	},
	[ALC861VD_6ST_DIG] = {
		.mixers = { alc861vd_6st_mixer, alc861vd_chmode_mixer },
		.init_verbs = { alc861vd_volume_init_verbs,
				alc861vd_6stack_init_verbs },
		.num_dacs = ARRAY_SIZE(alc861vd_dac_nids),
		.dac_nids = alc861vd_dac_nids,
		.dig_out_nid = ALC861VD_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc861vd_6stack_modes),
		.channel_mode = alc861vd_6stack_modes,
		.input_mux = &alc861vd_capture_source,
	},
	[ALC861VD_LENOVO] = {
		.mixers = { alc861vd_lenovo_mixer },
		.init_verbs = { alc861vd_volume_init_verbs,
				alc861vd_3stack_init_verbs,
				alc861vd_eapd_verbs,
				alc861vd_lenovo_unsol_verbs },
		.num_dacs = ARRAY_SIZE(alc660vd_dac_nids),
		.dac_nids = alc660vd_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc861vd_3stack_2ch_modes),
		.channel_mode = alc861vd_3stack_2ch_modes,
		.input_mux = &alc861vd_capture_source,
		.unsol_event = alc861vd_lenovo_unsol_event,
		.init_hook = alc861vd_lenovo_automute,
	},
	[ALC861VD_DALLAS] = {
		.mixers = { alc861vd_dallas_mixer },
		.init_verbs = { alc861vd_dallas_verbs },
		.num_dacs = ARRAY_SIZE(alc861vd_dac_nids),
		.dac_nids = alc861vd_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc861vd_3stack_2ch_modes),
		.channel_mode = alc861vd_3stack_2ch_modes,
		.input_mux = &alc861vd_dallas_capture_source,
		.unsol_event = alc861vd_dallas_unsol_event,
		.init_hook = alc861vd_dallas_automute,
	},
	[ALC861VD_HP] = {
		.mixers = { alc861vd_hp_mixer },
		.init_verbs = { alc861vd_dallas_verbs, alc861vd_eapd_verbs },
		.num_dacs = ARRAY_SIZE(alc861vd_dac_nids),
		.dac_nids = alc861vd_dac_nids,
		.dig_out_nid = ALC861VD_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc861vd_3stack_2ch_modes),
		.channel_mode = alc861vd_3stack_2ch_modes,
		.input_mux = &alc861vd_hp_capture_source,
		.unsol_event = alc861vd_dallas_unsol_event,
		.init_hook = alc861vd_dallas_automute,
	},		
};

/*
 * BIOS auto configuration
 */
static void alc861vd_auto_set_output_and_unmute(struct hda_codec *codec,
				hda_nid_t nid, int pin_type, int dac_idx)
{
	alc_set_pin_output(codec, nid, pin_type);
}

static void alc861vd_auto_init_multi_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i;

	alc_subsystem_id(codec, 0x15, 0x1b, 0x14);
	for (i = 0; i <= HDA_SIDE; i++) {
		hda_nid_t nid = spec->autocfg.line_out_pins[i];
		int pin_type = get_pin_type(spec->autocfg.line_out_type);
		if (nid)
			alc861vd_auto_set_output_and_unmute(codec, nid,
							    pin_type, i);
	}
}


static void alc861vd_auto_init_hp_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t pin;

	pin = spec->autocfg.hp_pins[0];
	if (pin) /* connect to front and  use dac 0 */
		alc861vd_auto_set_output_and_unmute(codec, pin, PIN_HP, 0);
	pin = spec->autocfg.speaker_pins[0];
	if (pin)
		alc861vd_auto_set_output_and_unmute(codec, pin, PIN_OUT, 0);
}

#define alc861vd_is_input_pin(nid)	alc880_is_input_pin(nid)
#define ALC861VD_PIN_CD_NID		ALC880_PIN_CD_NID

static void alc861vd_auto_init_analog_input(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		hda_nid_t nid = spec->autocfg.input_pins[i];
		if (alc861vd_is_input_pin(nid)) {
			snd_hda_codec_write(codec, nid, 0,
					AC_VERB_SET_PIN_WIDGET_CONTROL,
					i <= AUTO_PIN_FRONT_MIC ?
							PIN_VREF80 : PIN_IN);
			if (nid != ALC861VD_PIN_CD_NID)
				snd_hda_codec_write(codec, nid, 0,
						AC_VERB_SET_AMP_GAIN_MUTE,
						AMP_OUT_MUTE);
		}
	}
}

#define alc861vd_auto_init_input_src	alc882_auto_init_input_src

#define alc861vd_idx_to_mixer_vol(nid)		((nid) + 0x02)
#define alc861vd_idx_to_mixer_switch(nid)	((nid) + 0x0c)

/* add playback controls from the parsed DAC table */
/* Based on ALC880 version. But ALC861VD has separate,
 * different NIDs for mute/unmute switch and volume control */
static int alc861vd_auto_create_multi_out_ctls(struct alc_spec *spec,
					     const struct auto_pin_cfg *cfg)
{
	char name[32];
	static const char *chname[4] = {"Front", "Surround", "CLFE", "Side"};
	hda_nid_t nid_v, nid_s;
	int i, err;

	for (i = 0; i < cfg->line_outs; i++) {
		if (!spec->multiout.dac_nids[i])
			continue;
		nid_v = alc861vd_idx_to_mixer_vol(
				alc880_dac_to_idx(
					spec->multiout.dac_nids[i]));
		nid_s = alc861vd_idx_to_mixer_switch(
				alc880_dac_to_idx(
					spec->multiout.dac_nids[i]));

		if (i == 2) {
			/* Center/LFE */
			err = add_control(spec, ALC_CTL_WIDGET_VOL,
					  "Center Playback Volume",
					  HDA_COMPOSE_AMP_VAL(nid_v, 1, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
			err = add_control(spec, ALC_CTL_WIDGET_VOL,
					  "LFE Playback Volume",
					  HDA_COMPOSE_AMP_VAL(nid_v, 2, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
			err = add_control(spec, ALC_CTL_BIND_MUTE,
					  "Center Playback Switch",
					  HDA_COMPOSE_AMP_VAL(nid_s, 1, 2,
							      HDA_INPUT));
			if (err < 0)
				return err;
			err = add_control(spec, ALC_CTL_BIND_MUTE,
					  "LFE Playback Switch",
					  HDA_COMPOSE_AMP_VAL(nid_s, 2, 2,
							      HDA_INPUT));
			if (err < 0)
				return err;
		} else {
			sprintf(name, "%s Playback Volume", chname[i]);
			err = add_control(spec, ALC_CTL_WIDGET_VOL, name,
					  HDA_COMPOSE_AMP_VAL(nid_v, 3, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
			sprintf(name, "%s Playback Switch", chname[i]);
			err = add_control(spec, ALC_CTL_BIND_MUTE, name,
					  HDA_COMPOSE_AMP_VAL(nid_s, 3, 2,
							      HDA_INPUT));
			if (err < 0)
				return err;
		}
	}
	return 0;
}

/* add playback controls for speaker and HP outputs */
/* Based on ALC880 version. But ALC861VD has separate,
 * different NIDs for mute/unmute switch and volume control */
static int alc861vd_auto_create_extra_out(struct alc_spec *spec,
					hda_nid_t pin, const char *pfx)
{
	hda_nid_t nid_v, nid_s;
	int err;
	char name[32];

	if (!pin)
		return 0;

	if (alc880_is_fixed_pin(pin)) {
		nid_v = alc880_idx_to_dac(alc880_fixed_pin_idx(pin));
		/* specify the DAC as the extra output */
		if (!spec->multiout.hp_nid)
			spec->multiout.hp_nid = nid_v;
		else
			spec->multiout.extra_out_nid[0] = nid_v;
		/* control HP volume/switch on the output mixer amp */
		nid_v = alc861vd_idx_to_mixer_vol(
				alc880_fixed_pin_idx(pin));
		nid_s = alc861vd_idx_to_mixer_switch(
				alc880_fixed_pin_idx(pin));

		sprintf(name, "%s Playback Volume", pfx);
		err = add_control(spec, ALC_CTL_WIDGET_VOL, name,
				  HDA_COMPOSE_AMP_VAL(nid_v, 3, 0, HDA_OUTPUT));
		if (err < 0)
			return err;
		sprintf(name, "%s Playback Switch", pfx);
		err = add_control(spec, ALC_CTL_BIND_MUTE, name,
				  HDA_COMPOSE_AMP_VAL(nid_s, 3, 2, HDA_INPUT));
		if (err < 0)
			return err;
	} else if (alc880_is_multi_pin(pin)) {
		/* set manual connection */
		/* we have only a switch on HP-out PIN */
		sprintf(name, "%s Playback Switch", pfx);
		err = add_control(spec, ALC_CTL_WIDGET_MUTE, name,
				  HDA_COMPOSE_AMP_VAL(pin, 3, 0, HDA_OUTPUT));
		if (err < 0)
			return err;
	}
	return 0;
}

/* parse the BIOS configuration and set up the alc_spec
 * return 1 if successful, 0 if the proper config is not found,
 * or a negative error code
 * Based on ALC880 version - had to change it to override
 * alc880_auto_create_extra_out and alc880_auto_create_multi_out_ctls */
static int alc861vd_parse_auto_config(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int err;
	static hda_nid_t alc861vd_ignore[] = { 0x1d, 0 };

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg,
					   alc861vd_ignore);
	if (err < 0)
		return err;
	if (!spec->autocfg.line_outs)
		return 0; /* can't find valid BIOS pin config */

	err = alc880_auto_fill_dac_nids(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = alc861vd_auto_create_multi_out_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = alc861vd_auto_create_extra_out(spec,
					     spec->autocfg.speaker_pins[0],
					     "Speaker");
	if (err < 0)
		return err;
	err = alc861vd_auto_create_extra_out(spec,
					     spec->autocfg.hp_pins[0],
					     "Headphone");
	if (err < 0)
		return err;
	err = alc880_auto_create_analog_input_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;

	if (spec->autocfg.dig_out_pin)
		spec->multiout.dig_out_nid = ALC861VD_DIGOUT_NID;

	if (spec->kctl_alloc)
		spec->mixers[spec->num_mixers++] = spec->kctl_alloc;

	spec->init_verbs[spec->num_init_verbs++]
		= alc861vd_volume_init_verbs;

	spec->num_mux_defs = 1;
	spec->input_mux = &spec->private_imux;

	err = alc_auto_add_mic_boost(codec);
	if (err < 0)
		return err;

	return 1;
}

/* additional initialization for auto-configuration model */
static void alc861vd_auto_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	alc861vd_auto_init_multi_out(codec);
	alc861vd_auto_init_hp_out(codec);
	alc861vd_auto_init_analog_input(codec);
	alc861vd_auto_init_input_src(codec);
	if (spec->unsol_event)
		alc_sku_automute(codec);
}

static int patch_alc861vd(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err, board_config;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	board_config = snd_hda_check_board_config(codec, ALC861VD_MODEL_LAST,
						  alc861vd_models,
						  alc861vd_cfg_tbl);

	if (board_config < 0 || board_config >= ALC861VD_MODEL_LAST) {
		printk(KERN_INFO "hda_codec: Unknown model for ALC660VD/"
			"ALC861VD, trying auto-probe from BIOS...\n");
		board_config = ALC861VD_AUTO;
	}

	if (board_config == ALC861VD_AUTO) {
		/* automatic parse from the BIOS config */
		err = alc861vd_parse_auto_config(codec);
		if (err < 0) {
			alc_free(codec);
			return err;
		} else if (!err) {
			printk(KERN_INFO
			       "hda_codec: Cannot set up configuration "
			       "from BIOS.  Using base mode...\n");
			board_config = ALC861VD_3ST;
		}
	}

	if (board_config != ALC861VD_AUTO)
		setup_preset(spec, &alc861vd_presets[board_config]);

	if (codec->vendor_id == 0x10ec0660) {
		spec->stream_name_analog = "ALC660-VD Analog";
		spec->stream_name_digital = "ALC660-VD Digital";
		/* always turn on EAPD */
		spec->init_verbs[spec->num_init_verbs++] = alc660vd_eapd_verbs;
	} else {
		spec->stream_name_analog = "ALC861VD Analog";
		spec->stream_name_digital = "ALC861VD Digital";
	}

	spec->stream_analog_playback = &alc861vd_pcm_analog_playback;
	spec->stream_analog_capture = &alc861vd_pcm_analog_capture;

	spec->stream_digital_playback = &alc861vd_pcm_digital_playback;
	spec->stream_digital_capture = &alc861vd_pcm_digital_capture;

	spec->adc_nids = alc861vd_adc_nids;
	spec->num_adc_nids = ARRAY_SIZE(alc861vd_adc_nids);
	spec->capsrc_nids = alc861vd_capsrc_nids;

	spec->mixers[spec->num_mixers] = alc861vd_capture_mixer;
	spec->num_mixers++;

	spec->vmaster_nid = 0x02;

	codec->patch_ops = alc_patch_ops;

	if (board_config == ALC861VD_AUTO)
		spec->init_hook = alc861vd_auto_init;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	if (!spec->loopback.amplist)
		spec->loopback.amplist = alc861vd_loopbacks;
#endif

	return 0;
static int alc861vd_parse_auto_config(struct hda_codec *codec)
{
	static const hda_nid_t alc861vd_ignore[] = { 0x1d, 0 };
	static const hda_nid_t alc861vd_ssids[] = { 0x15, 0x1b, 0x14, 0 };
	return alc_parse_auto_config(codec, alc861vd_ignore, alc861vd_ssids);
}

enum {
	ALC660VD_FIX_ASUS_GPIO1,
	ALC861VD_FIX_DALLAS,
};

/* exclude VREF80 */
static void alc861vd_fixup_dallas(struct hda_codec *codec,
				  const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		snd_hda_override_pin_caps(codec, 0x18, 0x00000734);
		snd_hda_override_pin_caps(codec, 0x19, 0x0000073c);
	}
}

static const struct hda_fixup alc861vd_fixups[] = {
	[ALC660VD_FIX_ASUS_GPIO1] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* reset GPIO1 */
			{0x01, AC_VERB_SET_GPIO_MASK, 0x03},
			{0x01, AC_VERB_SET_GPIO_DIRECTION, 0x01},
			{0x01, AC_VERB_SET_GPIO_DATA, 0x01},
			{ }
		}
	},
	[ALC861VD_FIX_DALLAS] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc861vd_fixup_dallas,
	},
};

static const struct snd_pci_quirk alc861vd_fixup_tbl[] = {
	SND_PCI_QUIRK(0x103c, 0x30bf, "HP TX1000", ALC861VD_FIX_DALLAS),
	SND_PCI_QUIRK(0x1043, 0x1339, "ASUS A7-K", ALC660VD_FIX_ASUS_GPIO1),
	SND_PCI_QUIRK(0x1179, 0xff31, "Toshiba L30-149", ALC861VD_FIX_DALLAS),
	{}
};

/*
 */
static int patch_alc861vd(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err;

	err = alc_alloc_spec(codec, 0x0b);
	if (err < 0)
		return err;

	spec = codec->spec;
	spec->gen.beep_nid = 0x23;

	spec->shutup = alc_eapd_shutup;

	snd_hda_pick_fixup(codec, NULL, alc861vd_fixup_tbl, alc861vd_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	/* automatic parse from the BIOS config */
	err = alc861vd_parse_auto_config(codec);
	if (err < 0)
		goto error;

	if (!spec->gen.no_analog)
		set_beep_amp(spec, 0x0b, 0x05, HDA_INPUT);

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;

 error:
	alc_free(codec);
	return err;
}

/*
 * ALC662 support
 *
 * ALC662 is almost identical with ALC880 but has cleaner and more flexible
 * configuration.  Each pin widget can choose any input DACs and a mixer.
 * Each ADC is connected from a mixer of all inputs.  This makes possible
 * 6-channel independent captures.
 *
 * In addition, an independent DAC for the multi-playback (not used in this
 * driver yet).
 */
#define ALC662_DIGOUT_NID	0x06
#define ALC662_DIGIN_NID	0x0a

static hda_nid_t alc662_dac_nids[4] = {
	/* front, rear, clfe, rear_surr */
	0x02, 0x03, 0x04
};

static hda_nid_t alc662_adc_nids[1] = {
	/* ADC1-2 */
	0x09,
};

static hda_nid_t alc662_capsrc_nids[1] = { 0x22 };

/* input MUX */
/* FIXME: should be a matrix-type input source selection */
static struct hda_input_mux alc662_capture_source = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x1 },
		{ "Line", 0x2 },
		{ "CD", 0x4 },
	},
};

static struct hda_input_mux alc662_lenovo_101e_capture_source = {
	.num_items = 2,
	.items = {
		{ "Mic", 0x1 },
		{ "Line", 0x2 },
	},
};

static struct hda_input_mux alc662_eeepc_capture_source = {
	.num_items = 2,
	.items = {
		{ "i-Mic", 0x1 },
		{ "e-Mic", 0x0 },
	},
};

static struct hda_input_mux alc663_capture_source = {
	.num_items = 3,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x1 },
		{ "Line", 0x2 },
	},
};

static struct hda_input_mux alc663_m51va_capture_source = {
	.num_items = 2,
	.items = {
		{ "Ext-Mic", 0x0 },
		{ "D-Mic", 0x9 },
	},
};

#define alc662_mux_enum_info alc_mux_enum_info
#define alc662_mux_enum_get alc_mux_enum_get
#define alc662_mux_enum_put alc882_mux_enum_put

/*
 * 2ch mode
 */
static struct hda_channel_mode alc662_3ST_2ch_modes[1] = {
	{ 2, NULL }
};

/*
 * 2ch mode
 */
static struct hda_verb alc662_3ST_ch2_init[] = {
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ } /* end */
};

/*
 * 6ch mode
 */
static struct hda_verb alc662_3ST_ch6_init[] = {
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x18, AC_VERB_SET_CONNECT_SEL, 0x02 },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x1a, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ } /* end */
};

static struct hda_channel_mode alc662_3ST_6ch_modes[2] = {
	{ 2, alc662_3ST_ch2_init },
	{ 6, alc662_3ST_ch6_init },
};

/*
 * 2ch mode
 */
static struct hda_verb alc662_sixstack_ch6_init[] = {
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00 },
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00 },
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ } /* end */
};

/*
 * 6ch mode
 */
static struct hda_verb alc662_sixstack_ch8_init[] = {
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ } /* end */
};

static struct hda_channel_mode alc662_5stack_modes[2] = {
	{ 2, alc662_sixstack_ch6_init },
	{ 6, alc662_sixstack_ch8_init },
};

/* Pin assignment: Front=0x14, Rear=0x15, CLFE=0x16, Side=0x17
 *                 Mic=0x18, Front Mic=0x19, Line-In=0x1a, HP=0x1b
 */

static struct snd_kcontrol_new alc662_base_mixer[] = {
	/* output mixer control */
	HDA_CODEC_VOLUME("Front Playback Volume", 0x2, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x0c, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x3, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Surround Playback Switch", 0x0d, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x04, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x04, 2, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Center Playback Switch", 0x0e, 1, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),

	/*Input mixer control */
	HDA_CODEC_VOLUME("CD Playback Volume", 0xb, 0x4, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0xb, 0x4, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0xb, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0xb, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0xb, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0xb, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0xb, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0xb, 0x01, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc662_3ST_2ch_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x0c, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x0b, 0x05, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc662_3ST_6ch_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x0c, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Surround Playback Switch", 0x0d, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x04, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x04, 2, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Center Playback Switch", 0x0e, 1, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x0b, 0x05, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc662_lenovo_101e_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x02, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x03, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x03, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc662_eeepc_p701_mixer[] = {
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x14, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME("Line-Out Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Line-Out Playback Switch", 0x1b, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME("e-Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("e-Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("e-Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),

	HDA_CODEC_VOLUME("i-Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("i-Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("i-Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc662_eeepc_ep20_mixer[] = {
	HDA_CODEC_VOLUME("Line-Out Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Line-Out Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x03, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x03, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x04, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x04, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x04, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x04, 2, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("MuteCtrl Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc663_m51va_mixer[] = {
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x21, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("DMic Playback Switch", 0x23, 0x9, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc663_g71v_mixer[] = {
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Front Playback Volume", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x21, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("i-Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("i-Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc663_g50v_mixer[] = {
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x21, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("i-Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("i-Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc662_chmode_mixer[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
	},
	{ } /* end */
};

static struct hda_verb alc662_init_verbs[] = {
	/* ADC: mute amp left and right */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* Front mixer: unmute input/output amp left and right (volume = 0) */

	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},

	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	/* Front Pin: output 0 (0x0c) */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	/* Rear Pin: output 1 (0x0d) */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	/* CLFE Pin: output 2 (0x0e) */
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	/* Mic (rear) pin: input vref at 80% */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Front Mic pin: input vref at 80% */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Line In pin: input */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Line-2 In: Headphone output (output 0 - 0x0c) */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* CD pin widget for input */
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

	/* FIXME: use matrix-type input source selection */
	/* Mixer elements: 0x18, 19, 1a, 1b, 1c, 1d, 14, 15, 16, 17, 0b */
	/* Input mixer */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(4)},

	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(4)},

	/* always trun on EAPD */
	{0x14, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{0x15, AC_VERB_SET_EAPD_BTLENABLE, 2},

	{ }
};

static struct hda_verb alc662_sue_init_verbs[] = {
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN|ALC880_FRONT_EVENT},
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN|ALC880_HP_EVENT},
	{}
};

static struct hda_verb alc662_eeepc_sue_init_verbs[] = {
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{}
};

/* Set Unsolicited Event*/
static struct hda_verb alc662_eeepc_ep20_sue_init_verbs[] = {
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{}
};

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static struct hda_verb alc662_auto_init_verbs[] = {
	/*
	 * Unmute ADC and set the default input to mic-in
	 */
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	/* Unmute input amps (CD, Line In, Mic 1 & Mic 2) of the analog-loopback
	 * mixer widget
	 * Note: PASD motherboards uses the Line In 2 as the input for front
	 * panel mic (mic 2)
	 */
	/* Amp Indices: Mic1 = 0, Mic2 = 1, Line1 = 2, Line2 = 3, CD = 4 */
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},

	/*
	 * Set up output mixers (0x0c - 0x0f)
	 */
	/* set vol=0 to output mixers */
	{0x02, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},

	/* set up input amps for analog loopback */
	/* Amp Indices: DAC = 0, mixer = 1 */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},


	/* FIXME: use matrix-type input source selection */
	/* Mixer elements: 0x18, 19, 1a, 1b, 1c, 1d, 14, 15, 16, 17, 0b */
	/* Input mixer */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{ }
};

/* additional verbs for ALC663 */
static struct hda_verb alc663_auto_init_verbs[] = {
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{ }
};

static struct hda_verb alc663_m51va_init_verbs[] = {
	{0x21, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x21, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x21, AC_VERB_SET_CONNECT_SEL, 0x00},	/* Headphone */

	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(9)},

	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},
	{0x21, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{}
};

static struct hda_verb alc663_g71v_init_verbs[] = {
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	/* {0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE}, */
	/* {0x15, AC_VERB_SET_CONNECT_SEL, 0x01}, */ /* Headphone */

	{0x21, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x21, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x21, AC_VERB_SET_CONNECT_SEL, 0x00},	/* Headphone */

	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN|ALC880_FRONT_EVENT},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN|ALC880_MIC_EVENT},
	{0x21, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN|ALC880_HP_EVENT},
	{}
};

static struct hda_verb alc663_g50v_init_verbs[] = {
	{0x21, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x21, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x21, AC_VERB_SET_CONNECT_SEL, 0x00},	/* Headphone */

	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},
	{0x21, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{}
};

/* capture mixer elements */
static struct snd_kcontrol_new alc662_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 1,
		.info = alc662_mux_enum_info,
		.get = alc662_mux_enum_get,
		.put = alc662_mux_enum_put,
	},
	{ } /* end */
};

static void alc662_lenovo_101e_ispeaker_automute(struct hda_codec *codec)
{
	unsigned int present;
	unsigned char bits;

	present = snd_hda_codec_read(codec, 0x14, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x15, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
}

static void alc662_lenovo_101e_all_automute(struct hda_codec *codec)
{
	unsigned int present;
	unsigned char bits;

 	present = snd_hda_codec_read(codec, 0x1b, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x15, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
	snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
}

static void alc662_lenovo_101e_unsol_event(struct hda_codec *codec,
					   unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc662_lenovo_101e_all_automute(codec);
	if ((res >> 26) == ALC880_FRONT_EVENT)
		alc662_lenovo_101e_ispeaker_automute(codec);
}

static void alc662_eeepc_mic_automute(struct hda_codec *codec)
{
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x18, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	snd_hda_codec_write(codec, 0x22, 0, AC_VERB_SET_AMP_GAIN_MUTE,
			    0x7000 | (0x00 << 8) | (present ? 0 : 0x80));
	snd_hda_codec_write(codec, 0x23, 0, AC_VERB_SET_AMP_GAIN_MUTE,
			    0x7000 | (0x00 << 8) | (present ? 0 : 0x80));
	snd_hda_codec_write(codec, 0x22, 0, AC_VERB_SET_AMP_GAIN_MUTE,
			    0x7000 | (0x01 << 8) | (present ? 0x80 : 0));
	snd_hda_codec_write(codec, 0x23, 0, AC_VERB_SET_AMP_GAIN_MUTE,
			    0x7000 | (0x01 << 8) | (present ? 0x80 : 0));
}

/* unsolicited event for HP jack sensing */
static void alc662_eeepc_unsol_event(struct hda_codec *codec,
				     unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc262_hippo1_automute( codec );

	if ((res >> 26) == ALC880_MIC_EVENT)
		alc662_eeepc_mic_automute(codec);
}

static void alc662_eeepc_inithook(struct hda_codec *codec)
{
	alc262_hippo1_automute( codec );
	alc662_eeepc_mic_automute(codec);
}

static void alc662_eeepc_ep20_automute(struct hda_codec *codec)
{
	unsigned int mute;
	unsigned int present;

	snd_hda_codec_read(codec, 0x14, 0, AC_VERB_SET_PIN_SENSE, 0);
	present = snd_hda_codec_read(codec, 0x14, 0,
				     AC_VERB_GET_PIN_SENSE, 0);
	present = (present & 0x80000000) != 0;
	if (present) {
		/* mute internal speaker */
		snd_hda_codec_amp_stereo(codec, 0x1b, HDA_OUTPUT, 0,
					 HDA_AMP_MUTE, HDA_AMP_MUTE);
	} else {
		/* unmute internal speaker if necessary */
		mute = snd_hda_codec_amp_read(codec, 0x14, 0, HDA_OUTPUT, 0);
		snd_hda_codec_amp_stereo(codec, 0x1b, HDA_OUTPUT, 0,
					 HDA_AMP_MUTE, mute);
	}
}

/* unsolicited event for HP jack sensing */
static void alc662_eeepc_ep20_unsol_event(struct hda_codec *codec,
					  unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc662_eeepc_ep20_automute(codec);
}

static void alc662_eeepc_ep20_inithook(struct hda_codec *codec)
{
	alc662_eeepc_ep20_automute(codec);
}

static void alc663_m51va_speaker_automute(struct hda_codec *codec)
{
	unsigned int present;
	unsigned char bits;

	present = snd_hda_codec_read(codec, 0x21, 0,
				     AC_VERB_GET_PIN_SENSE, 0)
		& AC_PINSENSE_PRESENCE;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
}

static void alc663_m51va_mic_automute(struct hda_codec *codec)
{
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x18, 0,
				     AC_VERB_GET_PIN_SENSE, 0)
		& AC_PINSENSE_PRESENCE;
	snd_hda_codec_write_cache(codec, 0x22, 0, AC_VERB_SET_AMP_GAIN_MUTE,
			    0x7000 | (0x00 << 8) | (present ? 0 : 0x80));
	snd_hda_codec_write_cache(codec, 0x23, 0, AC_VERB_SET_AMP_GAIN_MUTE,
			    0x7000 | (0x00 << 8) | (present ? 0 : 0x80));
	snd_hda_codec_write_cache(codec, 0x22, 0, AC_VERB_SET_AMP_GAIN_MUTE,
			    0x7000 | (0x09 << 8) | (present ? 0x80 : 0));
	snd_hda_codec_write_cache(codec, 0x23, 0, AC_VERB_SET_AMP_GAIN_MUTE,
			    0x7000 | (0x09 << 8) | (present ? 0x80 : 0));
}

static void alc663_m51va_unsol_event(struct hda_codec *codec,
					   unsigned int res)
{
	switch (res >> 26) {
	case ALC880_HP_EVENT:
		alc663_m51va_speaker_automute(codec);
		break;
	case ALC880_MIC_EVENT:
		alc663_m51va_mic_automute(codec);
		break;
	}
}

static void alc663_m51va_inithook(struct hda_codec *codec)
{
	alc663_m51va_speaker_automute(codec);
	alc663_m51va_mic_automute(codec);
}

static void alc663_g71v_hp_automute(struct hda_codec *codec)
{
	unsigned int present;
	unsigned char bits;

	present = snd_hda_codec_read(codec, 0x21, 0,
				     AC_VERB_GET_PIN_SENSE, 0)
		& AC_PINSENSE_PRESENCE;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x15, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
	snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
}

static void alc663_g71v_front_automute(struct hda_codec *codec)
{
	unsigned int present;
	unsigned char bits;

	present = snd_hda_codec_read(codec, 0x15, 0,
				     AC_VERB_GET_PIN_SENSE, 0)
		& AC_PINSENSE_PRESENCE;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
}

static void alc663_g71v_unsol_event(struct hda_codec *codec,
					   unsigned int res)
{
	switch (res >> 26) {
	case ALC880_HP_EVENT:
		alc663_g71v_hp_automute(codec);
		break;
	case ALC880_FRONT_EVENT:
		alc663_g71v_front_automute(codec);
		break;
	case ALC880_MIC_EVENT:
		alc662_eeepc_mic_automute(codec);
		break;
	}
}

static void alc663_g71v_inithook(struct hda_codec *codec)
{
	alc663_g71v_front_automute(codec);
	alc663_g71v_hp_automute(codec);
	alc662_eeepc_mic_automute(codec);
}

static void alc663_g50v_unsol_event(struct hda_codec *codec,
					   unsigned int res)
{
	switch (res >> 26) {
	case ALC880_HP_EVENT:
		alc663_m51va_speaker_automute(codec);
		break;
	case ALC880_MIC_EVENT:
		alc662_eeepc_mic_automute(codec);
		break;
	}
}

static void alc663_g50v_inithook(struct hda_codec *codec)
{
	alc663_m51va_speaker_automute(codec);
	alc662_eeepc_mic_automute(codec);
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
#define alc662_loopbacks	alc880_loopbacks
#endif


/* pcm configuration: identiacal with ALC880 */
#define alc662_pcm_analog_playback	alc880_pcm_analog_playback
#define alc662_pcm_analog_capture	alc880_pcm_analog_capture
#define alc662_pcm_digital_playback	alc880_pcm_digital_playback
#define alc662_pcm_digital_capture	alc880_pcm_digital_capture

/*
 * configuration and preset
 */
static const char *alc662_models[ALC662_MODEL_LAST] = {
	[ALC662_3ST_2ch_DIG]	= "3stack-dig",
	[ALC662_3ST_6ch_DIG]	= "3stack-6ch-dig",
	[ALC662_3ST_6ch]	= "3stack-6ch",
	[ALC662_5ST_DIG]	= "6stack-dig",
	[ALC662_LENOVO_101E]	= "lenovo-101e",
	[ALC662_ASUS_EEEPC_P701] = "eeepc-p701",
	[ALC662_ASUS_EEEPC_EP20] = "eeepc-ep20",
	[ALC663_ASUS_M51VA] = "m51va",
	[ALC663_ASUS_G71V] = "g71v",
	[ALC663_ASUS_H13] = "h13",
	[ALC663_ASUS_G50V] = "g50v",
	[ALC662_AUTO]		= "auto",
};

static struct snd_pci_quirk alc662_cfg_tbl[] = {
	SND_PCI_QUIRK(0x1043, 0x11c3, "ASUS G71V", ALC663_ASUS_G71V),
	SND_PCI_QUIRK(0x1043, 0x1878, "ASUS M51VA", ALC663_ASUS_M51VA),
	SND_PCI_QUIRK(0x1043, 0x19a3, "ASUS M51VA", ALC663_ASUS_G50V),
	SND_PCI_QUIRK(0x1043, 0x8290, "ASUS P5GC-MX", ALC662_3ST_6ch_DIG),
	SND_PCI_QUIRK(0x1043, 0x82a1, "ASUS Eeepc", ALC662_ASUS_EEEPC_P701),
	SND_PCI_QUIRK(0x1043, 0x82d1, "ASUS Eeepc EP20", ALC662_ASUS_EEEPC_EP20),
	SND_PCI_QUIRK(0x17aa, 0x101e, "Lenovo", ALC662_LENOVO_101E),
	SND_PCI_QUIRK(0x1854, 0x2000, "ASUS H13-2000", ALC663_ASUS_H13),
	SND_PCI_QUIRK(0x1854, 0x2001, "ASUS H13-2001", ALC663_ASUS_H13),
	SND_PCI_QUIRK(0x1854, 0x2002, "ASUS H13-2002", ALC663_ASUS_H13),
	{}
};

static struct alc_config_preset alc662_presets[] = {
	[ALC662_3ST_2ch_DIG] = {
		.mixers = { alc662_3ST_2ch_mixer, alc662_capture_mixer },
		.init_verbs = { alc662_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.dig_out_nid = ALC662_DIGOUT_NID,
		.dig_in_nid = ALC662_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		.input_mux = &alc662_capture_source,
	},
	[ALC662_3ST_6ch_DIG] = {
		.mixers = { alc662_3ST_6ch_mixer, alc662_chmode_mixer,
			    alc662_capture_mixer },
		.init_verbs = { alc662_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.dig_out_nid = ALC662_DIGOUT_NID,
		.dig_in_nid = ALC662_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_6ch_modes),
		.channel_mode = alc662_3ST_6ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc662_capture_source,
	},
	[ALC662_3ST_6ch] = {
		.mixers = { alc662_3ST_6ch_mixer, alc662_chmode_mixer,
			    alc662_capture_mixer },
		.init_verbs = { alc662_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_6ch_modes),
		.channel_mode = alc662_3ST_6ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc662_capture_source,
	},
	[ALC662_5ST_DIG] = {
		.mixers = { alc662_base_mixer, alc662_chmode_mixer,
			    alc662_capture_mixer },
		.init_verbs = { alc662_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.dig_out_nid = ALC662_DIGOUT_NID,
		.dig_in_nid = ALC662_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc662_5stack_modes),
		.channel_mode = alc662_5stack_modes,
		.input_mux = &alc662_capture_source,
	},
	[ALC662_LENOVO_101E] = {
		.mixers = { alc662_lenovo_101e_mixer, alc662_capture_mixer },
		.init_verbs = { alc662_init_verbs, alc662_sue_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		.input_mux = &alc662_lenovo_101e_capture_source,
		.unsol_event = alc662_lenovo_101e_unsol_event,
		.init_hook = alc662_lenovo_101e_all_automute,
	},
	[ALC662_ASUS_EEEPC_P701] = {
		.mixers = { alc662_eeepc_p701_mixer, alc662_capture_mixer },
		.init_verbs = { alc662_init_verbs,
				alc662_eeepc_sue_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		.input_mux = &alc662_eeepc_capture_source,
		.unsol_event = alc662_eeepc_unsol_event,
		.init_hook = alc662_eeepc_inithook,
	},
	[ALC662_ASUS_EEEPC_EP20] = {
		.mixers = { alc662_eeepc_ep20_mixer, alc662_capture_mixer,
			    alc662_chmode_mixer },
		.init_verbs = { alc662_init_verbs,
				alc662_eeepc_ep20_sue_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_6ch_modes),
		.channel_mode = alc662_3ST_6ch_modes,
		.input_mux = &alc662_lenovo_101e_capture_source,
		.unsol_event = alc662_eeepc_ep20_unsol_event,
		.init_hook = alc662_eeepc_ep20_inithook,
	},
	[ALC663_ASUS_M51VA] = {
		.mixers = { alc663_m51va_mixer, alc662_capture_mixer},
		.init_verbs = { alc662_init_verbs, alc663_m51va_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.dig_out_nid = ALC662_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		.input_mux = &alc663_m51va_capture_source,
		.unsol_event = alc663_m51va_unsol_event,
		.init_hook = alc663_m51va_inithook,
	},
	[ALC663_ASUS_G71V] = {
		.mixers = { alc663_g71v_mixer, alc662_capture_mixer},
		.init_verbs = { alc662_init_verbs, alc663_g71v_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.dig_out_nid = ALC662_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		.input_mux = &alc662_eeepc_capture_source,
		.unsol_event = alc663_g71v_unsol_event,
		.init_hook = alc663_g71v_inithook,
	},
	[ALC663_ASUS_H13] = {
		.mixers = { alc663_m51va_mixer, alc662_capture_mixer},
		.init_verbs = { alc662_init_verbs, alc663_m51va_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		.input_mux = &alc663_m51va_capture_source,
		.unsol_event = alc663_m51va_unsol_event,
		.init_hook = alc663_m51va_inithook,
	},
	[ALC663_ASUS_G50V] = {
		.mixers = { alc663_g50v_mixer, alc662_capture_mixer},
		.init_verbs = { alc662_init_verbs, alc663_g50v_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.dig_out_nid = ALC662_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_6ch_modes),
		.channel_mode = alc662_3ST_6ch_modes,
		.input_mux = &alc663_capture_source,
		.unsol_event = alc663_g50v_unsol_event,
		.init_hook = alc663_g50v_inithook,
	},
};


/*
 * BIOS auto configuration
 */

/* add playback controls from the parsed DAC table */
static int alc662_auto_create_multi_out_ctls(struct alc_spec *spec,
					     const struct auto_pin_cfg *cfg)
{
	char name[32];
	static const char *chname[4] = {
		"Front", "Surround", NULL /*CLFE*/, "Side"
	};
	hda_nid_t nid;
	int i, err;

	for (i = 0; i < cfg->line_outs; i++) {
		if (!spec->multiout.dac_nids[i])
			continue;
		nid = alc880_idx_to_dac(i);
		if (i == 2) {
			/* Center/LFE */
			err = add_control(spec, ALC_CTL_WIDGET_VOL,
					  "Center Playback Volume",
					  HDA_COMPOSE_AMP_VAL(nid, 1, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
			err = add_control(spec, ALC_CTL_WIDGET_VOL,
					  "LFE Playback Volume",
					  HDA_COMPOSE_AMP_VAL(nid, 2, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
			err = add_control(spec, ALC_CTL_BIND_MUTE,
					  "Center Playback Switch",
					  HDA_COMPOSE_AMP_VAL(nid, 1, 2,
							      HDA_INPUT));
			if (err < 0)
				return err;
			err = add_control(spec, ALC_CTL_BIND_MUTE,
					  "LFE Playback Switch",
					  HDA_COMPOSE_AMP_VAL(nid, 2, 2,
							      HDA_INPUT));
			if (err < 0)
				return err;
		} else {
			sprintf(name, "%s Playback Volume", chname[i]);
			err = add_control(spec, ALC_CTL_WIDGET_VOL, name,
					  HDA_COMPOSE_AMP_VAL(nid, 3, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
			sprintf(name, "%s Playback Switch", chname[i]);
			err = add_control(spec, ALC_CTL_BIND_MUTE, name,
					  HDA_COMPOSE_AMP_VAL(nid, 3, 2,
							      HDA_INPUT));
			if (err < 0)
				return err;
		}
	}
	return 0;
}

/* add playback controls for speaker and HP outputs */
static int alc662_auto_create_extra_out(struct alc_spec *spec, hda_nid_t pin,
					const char *pfx)
{
	hda_nid_t nid;
	int err;
	char name[32];

	if (!pin)
		return 0;

	if (pin == 0x17) {
		/* ALC663 has a mono output pin on 0x17 */
		sprintf(name, "%s Playback Switch", pfx);
		err = add_control(spec, ALC_CTL_WIDGET_MUTE, name,
				  HDA_COMPOSE_AMP_VAL(pin, 2, 0, HDA_OUTPUT));
		return err;
	}

	if (alc880_is_fixed_pin(pin)) {
		nid = alc880_idx_to_dac(alc880_fixed_pin_idx(pin));
                /* printk("DAC nid=%x\n",nid); */
		/* specify the DAC as the extra output */
		if (!spec->multiout.hp_nid)
			spec->multiout.hp_nid = nid;
		else
			spec->multiout.extra_out_nid[0] = nid;
		/* control HP volume/switch on the output mixer amp */
		nid = alc880_idx_to_dac(alc880_fixed_pin_idx(pin));
		sprintf(name, "%s Playback Volume", pfx);
		err = add_control(spec, ALC_CTL_WIDGET_VOL, name,
				  HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT));
		if (err < 0)
			return err;
		sprintf(name, "%s Playback Switch", pfx);
		err = add_control(spec, ALC_CTL_BIND_MUTE, name,
				  HDA_COMPOSE_AMP_VAL(nid, 3, 2, HDA_INPUT));
		if (err < 0)
			return err;
	} else if (alc880_is_multi_pin(pin)) {
		/* set manual connection */
		/* we have only a switch on HP-out PIN */
		sprintf(name, "%s Playback Switch", pfx);
		err = add_control(spec, ALC_CTL_WIDGET_MUTE, name,
				  HDA_COMPOSE_AMP_VAL(pin, 3, 0, HDA_OUTPUT));
		if (err < 0)
			return err;
	}
	return 0;
}

/* create playback/capture controls for input pins */
static int alc662_auto_create_analog_input_ctls(struct alc_spec *spec,
						const struct auto_pin_cfg *cfg)
{
	struct hda_input_mux *imux = &spec->private_imux;
	int i, err, idx;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		if (alc880_is_input_pin(cfg->input_pins[i])) {
			idx = alc880_input_pin_idx(cfg->input_pins[i]);
			err = new_analog_input(spec, cfg->input_pins[i],
					       auto_pin_cfg_labels[i],
					       idx, 0x0b);
			if (err < 0)
				return err;
			imux->items[imux->num_items].label =
				auto_pin_cfg_labels[i];
			imux->items[imux->num_items].index =
				alc880_input_pin_idx(cfg->input_pins[i]);
			imux->num_items++;
		}
	}
	return 0;
}

static void alc662_auto_set_output_and_unmute(struct hda_codec *codec,
					      hda_nid_t nid, int pin_type,
					      int dac_idx)
{
	alc_set_pin_output(codec, nid, pin_type);
	/* need the manual connection? */
	if (alc880_is_multi_pin(nid)) {
		struct alc_spec *spec = codec->spec;
		int idx = alc880_multi_pin_idx(nid);
		snd_hda_codec_write(codec, alc880_idx_to_selector(idx), 0,
				    AC_VERB_SET_CONNECT_SEL,
				    alc880_dac_to_idx(spec->multiout.dac_nids[dac_idx]));
	}
}

static void alc662_auto_init_multi_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i;

	alc_subsystem_id(codec, 0x15, 0x1b, 0x14);
	for (i = 0; i <= HDA_SIDE; i++) {
		hda_nid_t nid = spec->autocfg.line_out_pins[i];
		int pin_type = get_pin_type(spec->autocfg.line_out_type);
		if (nid)
			alc662_auto_set_output_and_unmute(codec, nid, pin_type,
							  i);
	}
}

static void alc662_auto_init_hp_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t pin;

	pin = spec->autocfg.hp_pins[0];
	if (pin) /* connect to front */
		/* use dac 0 */
		alc662_auto_set_output_and_unmute(codec, pin, PIN_HP, 0);
	pin = spec->autocfg.speaker_pins[0];
	if (pin)
		alc662_auto_set_output_and_unmute(codec, pin, PIN_OUT, 0);
}

#define alc662_is_input_pin(nid)	alc880_is_input_pin(nid)
#define ALC662_PIN_CD_NID		ALC880_PIN_CD_NID

static void alc662_auto_init_analog_input(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		hda_nid_t nid = spec->autocfg.input_pins[i];
		if (alc662_is_input_pin(nid)) {
			snd_hda_codec_write(codec, nid, 0,
					    AC_VERB_SET_PIN_WIDGET_CONTROL,
					    (i <= AUTO_PIN_FRONT_MIC ?
					     PIN_VREF80 : PIN_IN));
			if (nid != ALC662_PIN_CD_NID)
				snd_hda_codec_write(codec, nid, 0,
						    AC_VERB_SET_AMP_GAIN_MUTE,
						    AMP_OUT_MUTE);
		}
	}
}

#define alc662_auto_init_input_src	alc882_auto_init_input_src

static int alc662_parse_auto_config(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int err;
	static hda_nid_t alc662_ignore[] = { 0x1d, 0 };

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg,
					   alc662_ignore);
	if (err < 0)
		return err;
	if (!spec->autocfg.line_outs)
		return 0; /* can't find valid BIOS pin config */

	err = alc880_auto_fill_dac_nids(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = alc662_auto_create_multi_out_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = alc662_auto_create_extra_out(spec,
					   spec->autocfg.speaker_pins[0],
					   "Speaker");
	if (err < 0)
		return err;
	err = alc662_auto_create_extra_out(spec, spec->autocfg.hp_pins[0],
					   "Headphone");
	if (err < 0)
		return err;
	err = alc662_auto_create_analog_input_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;

	if (spec->autocfg.dig_out_pin)
		spec->multiout.dig_out_nid = ALC880_DIGOUT_NID;

	if (spec->kctl_alloc)
		spec->mixers[spec->num_mixers++] = spec->kctl_alloc;

	spec->num_mux_defs = 1;
	spec->input_mux = &spec->private_imux;
	
	spec->init_verbs[spec->num_init_verbs++] = alc662_auto_init_verbs;
	if (codec->vendor_id == 0x10ec0663)
		spec->init_verbs[spec->num_init_verbs++] =
			alc663_auto_init_verbs;

	err = alc_auto_add_mic_boost(codec);
	if (err < 0)
		return err;

	spec->mixers[spec->num_mixers] = alc662_capture_mixer;
	spec->num_mixers++;
	return 1;
}

/* additional initialization for auto-configuration model */
static void alc662_auto_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	alc662_auto_init_multi_out(codec);
	alc662_auto_init_hp_out(codec);
	alc662_auto_init_analog_input(codec);
	alc662_auto_init_input_src(codec);
	if (spec->unsol_event)
		alc_sku_automute(codec);
}

static int patch_alc662(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err, board_config;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	codec->spec = spec;

	alc_fix_pll_init(codec, 0x20, 0x04, 15);

	board_config = snd_hda_check_board_config(codec, ALC662_MODEL_LAST,
						  alc662_models,
			  	                  alc662_cfg_tbl);
	if (board_config < 0) {
		printk(KERN_INFO "hda_codec: Unknown model for ALC662, "
		       "trying auto-probe from BIOS...\n");
		board_config = ALC662_AUTO;
	}

	if (board_config == ALC662_AUTO) {
		/* automatic parse from the BIOS config */
		err = alc662_parse_auto_config(codec);
		if (err < 0) {
			alc_free(codec);
			return err;
		} else if (!err) {
			printk(KERN_INFO
			       "hda_codec: Cannot set up configuration "
			       "from BIOS.  Using base mode...\n");
			board_config = ALC662_3ST_2ch_DIG;
		}
	}

	if (board_config != ALC662_AUTO)
		setup_preset(spec, &alc662_presets[board_config]);

	if (codec->vendor_id == 0x10ec0663) {
		spec->stream_name_analog = "ALC663 Analog";
		spec->stream_name_digital = "ALC663 Digital";
	} else {
		spec->stream_name_analog = "ALC662 Analog";
		spec->stream_name_digital = "ALC662 Digital";
	}

	spec->stream_analog_playback = &alc662_pcm_analog_playback;
	spec->stream_analog_capture = &alc662_pcm_analog_capture;

	spec->stream_digital_playback = &alc662_pcm_digital_playback;
	spec->stream_digital_capture = &alc662_pcm_digital_capture;

	spec->adc_nids = alc662_adc_nids;
	spec->num_adc_nids = ARRAY_SIZE(alc662_adc_nids);
	spec->capsrc_nids = alc662_capsrc_nids;

	spec->vmaster_nid = 0x02;

	codec->patch_ops = alc_patch_ops;
	if (board_config == ALC662_AUTO)
		spec->init_hook = alc662_auto_init;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	if (!spec->loopback.amplist)
		spec->loopback.amplist = alc662_loopbacks;
#endif

static int alc662_parse_auto_config(struct hda_codec *codec)
{
	static const hda_nid_t alc662_ignore[] = { 0x1d, 0 };
	static const hda_nid_t alc663_ssids[] = { 0x15, 0x1b, 0x14, 0x21 };
	static const hda_nid_t alc662_ssids[] = { 0x15, 0x1b, 0x14, 0 };
	const hda_nid_t *ssids;

	if (codec->core.vendor_id == 0x10ec0272 || codec->core.vendor_id == 0x10ec0663 ||
	    codec->core.vendor_id == 0x10ec0665 || codec->core.vendor_id == 0x10ec0670 ||
	    codec->core.vendor_id == 0x10ec0671)
		ssids = alc663_ssids;
	else
		ssids = alc662_ssids;
	return alc_parse_auto_config(codec, alc662_ignore, ssids);
}

static void alc272_fixup_mario(struct hda_codec *codec,
			       const struct hda_fixup *fix, int action)
{
	if (action != HDA_FIXUP_ACT_PRE_PROBE)
		return;
	if (snd_hda_override_amp_caps(codec, 0x2, HDA_OUTPUT,
				      (0x3b << AC_AMPCAP_OFFSET_SHIFT) |
				      (0x3b << AC_AMPCAP_NUM_STEPS_SHIFT) |
				      (0x03 << AC_AMPCAP_STEP_SIZE_SHIFT) |
				      (0 << AC_AMPCAP_MUTE_SHIFT)))
		codec_warn(codec, "failed to override amp caps for NID 0x2\n");
}

static const struct snd_pcm_chmap_elem asus_pcm_2_1_chmaps[] = {
	{ .channels = 2,
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR } },
	{ .channels = 4,
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR,
		   SNDRV_CHMAP_NA, SNDRV_CHMAP_LFE } }, /* LFE only on right */
	{ }
};

/* override the 2.1 chmap */
static void alc_fixup_bass_chmap(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_BUILD) {
		struct alc_spec *spec = codec->spec;
		spec->gen.pcm_rec[0]->stream[0].chmap = asus_pcm_2_1_chmaps;
	}
}

/* avoid D3 for keeping GPIO up */
static unsigned int gpio_led_power_filter(struct hda_codec *codec,
					  hda_nid_t nid,
					  unsigned int power_state)
{
	struct alc_spec *spec = codec->spec;
	if (nid == codec->core.afg && power_state == AC_PWRST_D3 && spec->gpio_led)
		return AC_PWRST_D0;
	return power_state;
}

static void alc662_fixup_led_gpio1(struct hda_codec *codec,
				   const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	static const struct hda_verb gpio_init[] = {
		{ 0x01, AC_VERB_SET_GPIO_MASK, 0x01 },
		{ 0x01, AC_VERB_SET_GPIO_DIRECTION, 0x01 },
		{}
	};

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->gen.vmaster_mute.hook = alc_fixup_gpio_mute_hook;
		spec->gpio_led = 0;
		spec->mute_led_polarity = 1;
		spec->gpio_mute_led_mask = 0x01;
		snd_hda_add_verbs(codec, gpio_init);
		codec->power_filter = gpio_led_power_filter;
	}
}

static struct coef_fw alc668_coefs[] = {
	WRITE_COEF(0x01, 0xbebe), WRITE_COEF(0x02, 0xaaaa), WRITE_COEF(0x03,    0x0),
	WRITE_COEF(0x04, 0x0180), WRITE_COEF(0x06,    0x0), WRITE_COEF(0x07, 0x0f80),
	WRITE_COEF(0x08, 0x0031), WRITE_COEF(0x0a, 0x0060), WRITE_COEF(0x0b,    0x0),
	WRITE_COEF(0x0c, 0x7cf7), WRITE_COEF(0x0d, 0x1080), WRITE_COEF(0x0e, 0x7f7f),
	WRITE_COEF(0x0f, 0xcccc), WRITE_COEF(0x10, 0xddcc), WRITE_COEF(0x11, 0x0001),
	WRITE_COEF(0x13,    0x0), WRITE_COEF(0x14, 0x2aa0), WRITE_COEF(0x17, 0xa940),
	WRITE_COEF(0x19,    0x0), WRITE_COEF(0x1a,    0x0), WRITE_COEF(0x1b,    0x0),
	WRITE_COEF(0x1c,    0x0), WRITE_COEF(0x1d,    0x0), WRITE_COEF(0x1e, 0x7418),
	WRITE_COEF(0x1f, 0x0804), WRITE_COEF(0x20, 0x4200), WRITE_COEF(0x21, 0x0468),
	WRITE_COEF(0x22, 0x8ccc), WRITE_COEF(0x23, 0x0250), WRITE_COEF(0x24, 0x7418),
	WRITE_COEF(0x27,    0x0), WRITE_COEF(0x28, 0x8ccc), WRITE_COEF(0x2a, 0xff00),
	WRITE_COEF(0x2b, 0x8000), WRITE_COEF(0xa7, 0xff00), WRITE_COEF(0xa8, 0x8000),
	WRITE_COEF(0xaa, 0x2e17), WRITE_COEF(0xab, 0xa0c0), WRITE_COEF(0xac,    0x0),
	WRITE_COEF(0xad,    0x0), WRITE_COEF(0xae, 0x2ac6), WRITE_COEF(0xaf, 0xa480),
	WRITE_COEF(0xb0,    0x0), WRITE_COEF(0xb1,    0x0), WRITE_COEF(0xb2,    0x0),
	WRITE_COEF(0xb3,    0x0), WRITE_COEF(0xb4,    0x0), WRITE_COEF(0xb5, 0x1040),
	WRITE_COEF(0xb6, 0xd697), WRITE_COEF(0xb7, 0x902b), WRITE_COEF(0xb8, 0xd697),
	WRITE_COEF(0xb9, 0x902b), WRITE_COEF(0xba, 0xb8ba), WRITE_COEF(0xbb, 0xaaab),
	WRITE_COEF(0xbc, 0xaaaf), WRITE_COEF(0xbd, 0x6aaa), WRITE_COEF(0xbe, 0x1c02),
	WRITE_COEF(0xc0, 0x00ff), WRITE_COEF(0xc1, 0x0fa6),
	{}
};

static void alc668_restore_default_value(struct hda_codec *codec)
{
	alc_process_coef_fw(codec, alc668_coefs);
}

enum {
	ALC662_FIXUP_ASPIRE,
	ALC662_FIXUP_LED_GPIO1,
	ALC662_FIXUP_IDEAPAD,
	ALC272_FIXUP_MARIO,
	ALC662_FIXUP_CZC_P10T,
	ALC662_FIXUP_SKU_IGNORE,
	ALC662_FIXUP_HP_RP5800,
	ALC662_FIXUP_ASUS_MODE1,
	ALC662_FIXUP_ASUS_MODE2,
	ALC662_FIXUP_ASUS_MODE3,
	ALC662_FIXUP_ASUS_MODE4,
	ALC662_FIXUP_ASUS_MODE5,
	ALC662_FIXUP_ASUS_MODE6,
	ALC662_FIXUP_ASUS_MODE7,
	ALC662_FIXUP_ASUS_MODE8,
	ALC662_FIXUP_NO_JACK_DETECT,
	ALC662_FIXUP_ZOTAC_Z68,
	ALC662_FIXUP_INV_DMIC,
	ALC662_FIXUP_DELL_MIC_NO_PRESENCE,
	ALC668_FIXUP_DELL_MIC_NO_PRESENCE,
	ALC662_FIXUP_HEADSET_MODE,
	ALC668_FIXUP_HEADSET_MODE,
	ALC662_FIXUP_BASS_MODE4_CHMAP,
	ALC662_FIXUP_BASS_16,
	ALC662_FIXUP_BASS_1A,
	ALC662_FIXUP_BASS_CHMAP,
	ALC668_FIXUP_AUTO_MUTE,
	ALC668_FIXUP_DELL_DISABLE_AAMIX,
	ALC668_FIXUP_DELL_XPS13,
	ALC662_FIXUP_ASUS_Nx50,
	ALC668_FIXUP_ASUS_Nx51_HEADSET_MODE,
	ALC668_FIXUP_ASUS_Nx51,
};

static const struct hda_fixup alc662_fixups[] = {
	[ALC662_FIXUP_ASPIRE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x15, 0x99130112 }, /* subwoofer */
			{ }
		}
	},
	[ALC662_FIXUP_LED_GPIO1] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc662_fixup_led_gpio1,
	},
	[ALC662_FIXUP_IDEAPAD] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x17, 0x99130112 }, /* subwoofer */
			{ }
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_LED_GPIO1,
	},
	[ALC272_FIXUP_MARIO] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc272_fixup_mario,
	},
	[ALC662_FIXUP_CZC_P10T] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{0x14, AC_VERB_SET_EAPD_BTLENABLE, 0},
			{}
		}
	},
	[ALC662_FIXUP_SKU_IGNORE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_sku_ignore,
	},
	[ALC662_FIXUP_HP_RP5800] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x0221201f }, /* HP out */
			{ }
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_SKU_IGNORE
	},
	[ALC662_FIXUP_ASUS_MODE1] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x18, 0x01a19c20 }, /* mic */
			{ 0x19, 0x99a3092f }, /* int-mic */
			{ 0x21, 0x0121401f }, /* HP out */
			{ }
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_SKU_IGNORE
	},
	[ALC662_FIXUP_ASUS_MODE2] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x18, 0x01a19820 }, /* mic */
			{ 0x19, 0x99a3092f }, /* int-mic */
			{ 0x1b, 0x0121401f }, /* HP out */
			{ }
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_SKU_IGNORE
	},
	[ALC662_FIXUP_ASUS_MODE3] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x15, 0x0121441f }, /* HP */
			{ 0x18, 0x01a19840 }, /* mic */
			{ 0x19, 0x99a3094f }, /* int-mic */
			{ 0x21, 0x01211420 }, /* HP2 */
			{ }
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_SKU_IGNORE
	},
	[ALC662_FIXUP_ASUS_MODE4] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x16, 0x99130111 }, /* speaker */
			{ 0x18, 0x01a19840 }, /* mic */
			{ 0x19, 0x99a3094f }, /* int-mic */
			{ 0x21, 0x0121441f }, /* HP */
			{ }
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_SKU_IGNORE
	},
	[ALC662_FIXUP_ASUS_MODE5] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x15, 0x0121441f }, /* HP */
			{ 0x16, 0x99130111 }, /* speaker */
			{ 0x18, 0x01a19840 }, /* mic */
			{ 0x19, 0x99a3094f }, /* int-mic */
			{ }
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_SKU_IGNORE
	},
	[ALC662_FIXUP_ASUS_MODE6] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x15, 0x01211420 }, /* HP2 */
			{ 0x18, 0x01a19840 }, /* mic */
			{ 0x19, 0x99a3094f }, /* int-mic */
			{ 0x1b, 0x0121441f }, /* HP */
			{ }
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_SKU_IGNORE
	},
	[ALC662_FIXUP_ASUS_MODE7] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x17, 0x99130111 }, /* speaker */
			{ 0x18, 0x01a19840 }, /* mic */
			{ 0x19, 0x99a3094f }, /* int-mic */
			{ 0x1b, 0x01214020 }, /* HP */
			{ 0x21, 0x0121401f }, /* HP */
			{ }
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_SKU_IGNORE
	},
	[ALC662_FIXUP_ASUS_MODE8] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x12, 0x99a30970 }, /* int-mic */
			{ 0x15, 0x01214020 }, /* HP */
			{ 0x17, 0x99130111 }, /* speaker */
			{ 0x18, 0x01a19840 }, /* mic */
			{ 0x21, 0x0121401f }, /* HP */
			{ }
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_SKU_IGNORE
	},
	[ALC662_FIXUP_NO_JACK_DETECT] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_no_jack_detect,
	},
	[ALC662_FIXUP_ZOTAC_Z68] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1b, 0x02214020 }, /* Front HP */
			{ }
		}
	},
	[ALC662_FIXUP_INV_DMIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_inv_dmic,
	},
	[ALC668_FIXUP_DELL_XPS13] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_dell_xps13,
		.chained = true,
		.chain_id = ALC668_FIXUP_DELL_DISABLE_AAMIX
	},
	[ALC668_FIXUP_DELL_DISABLE_AAMIX] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_disable_aamix,
		.chained = true,
		.chain_id = ALC668_FIXUP_DELL_MIC_NO_PRESENCE
	},
	[ALC668_FIXUP_AUTO_MUTE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_auto_mute_via_amp,
		.chained = true,
		.chain_id = ALC668_FIXUP_DELL_MIC_NO_PRESENCE
	},
	[ALC662_FIXUP_DELL_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x03a1113c }, /* use as headset mic, without its own jack detect */
			/* headphone mic by setting pin control of 0x1b (headphone out) to in + vref_50 */
			{ }
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_HEADSET_MODE
	},
	[ALC662_FIXUP_HEADSET_MODE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_headset_mode_alc662,
	},
	[ALC668_FIXUP_DELL_MIC_NO_PRESENCE] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x03a1913d }, /* use as headphone mic, without its own jack detect */
			{ 0x1b, 0x03a1113c }, /* use as headset mic, without its own jack detect */
			{ }
		},
		.chained = true,
		.chain_id = ALC668_FIXUP_HEADSET_MODE
	},
	[ALC668_FIXUP_HEADSET_MODE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_headset_mode_alc668,
	},
	[ALC662_FIXUP_BASS_MODE4_CHMAP] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_bass_chmap,
		.chained = true,
		.chain_id = ALC662_FIXUP_ASUS_MODE4
	},
	[ALC662_FIXUP_BASS_16] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{0x16, 0x80106111}, /* bass speaker */
			{}
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_BASS_CHMAP,
	},
	[ALC662_FIXUP_BASS_1A] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{0x1a, 0x80106111}, /* bass speaker */
			{}
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_BASS_CHMAP,
	},
	[ALC662_FIXUP_BASS_CHMAP] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_bass_chmap,
	},
	[ALC662_FIXUP_ASUS_Nx50] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_auto_mute_via_amp,
		.chained = true,
		.chain_id = ALC662_FIXUP_BASS_1A
	},
	[ALC668_FIXUP_ASUS_Nx51_HEADSET_MODE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = alc_fixup_headset_mode_alc668,
		.chain_id = ALC662_FIXUP_BASS_CHMAP
	},
	[ALC668_FIXUP_ASUS_Nx51] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x19, 0x03a1913d }, /* use as headphone mic, without its own jack detect */
			{ 0x1a, 0x90170151 }, /* bass speaker */
			{ 0x1b, 0x03a1113c }, /* use as headset mic, without its own jack detect */
			{}
		},
		.chained = true,
		.chain_id = ALC668_FIXUP_ASUS_Nx51_HEADSET_MODE,
	},
};

static const struct snd_pci_quirk alc662_fixup_tbl[] = {
	SND_PCI_QUIRK(0x1019, 0x9087, "ECS", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1025, 0x022f, "Acer Aspire One", ALC662_FIXUP_INV_DMIC),
	SND_PCI_QUIRK(0x1025, 0x0241, "Packard Bell DOTS", ALC662_FIXUP_INV_DMIC),
	SND_PCI_QUIRK(0x1025, 0x0308, "Acer Aspire 8942G", ALC662_FIXUP_ASPIRE),
	SND_PCI_QUIRK(0x1025, 0x031c, "Gateway NV79", ALC662_FIXUP_SKU_IGNORE),
	SND_PCI_QUIRK(0x1025, 0x0349, "eMachines eM250", ALC662_FIXUP_INV_DMIC),
	SND_PCI_QUIRK(0x1025, 0x034a, "Gateway LT27", ALC662_FIXUP_INV_DMIC),
	SND_PCI_QUIRK(0x1025, 0x038b, "Acer Aspire 8943G", ALC662_FIXUP_ASPIRE),
	SND_PCI_QUIRK(0x1028, 0x05d8, "Dell", ALC668_FIXUP_DELL_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x05db, "Dell", ALC668_FIXUP_DELL_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x05fe, "Dell XPS 15", ALC668_FIXUP_DELL_XPS13),
	SND_PCI_QUIRK(0x1028, 0x060a, "Dell XPS 13", ALC668_FIXUP_DELL_XPS13),
	SND_PCI_QUIRK(0x1028, 0x060d, "Dell M3800", ALC668_FIXUP_DELL_XPS13),
	SND_PCI_QUIRK(0x1028, 0x0625, "Dell", ALC668_FIXUP_DELL_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x0626, "Dell", ALC668_FIXUP_DELL_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x0696, "Dell", ALC668_FIXUP_DELL_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x0698, "Dell", ALC668_FIXUP_DELL_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x1028, 0x069f, "Dell", ALC668_FIXUP_DELL_MIC_NO_PRESENCE),
	SND_PCI_QUIRK(0x103c, 0x1632, "HP RP5800", ALC662_FIXUP_HP_RP5800),
	SND_PCI_QUIRK(0x1043, 0x1080, "Asus UX501VW", ALC668_FIXUP_HEADSET_MODE),
	SND_PCI_QUIRK(0x1043, 0x11cd, "Asus N550", ALC662_FIXUP_ASUS_Nx50),
	SND_PCI_QUIRK(0x1043, 0x13df, "Asus N550JX", ALC662_FIXUP_BASS_1A),
	SND_PCI_QUIRK(0x1043, 0x129d, "Asus N750", ALC662_FIXUP_ASUS_Nx50),
	SND_PCI_QUIRK(0x1043, 0x1477, "ASUS N56VZ", ALC662_FIXUP_BASS_MODE4_CHMAP),
	SND_PCI_QUIRK(0x1043, 0x15a7, "ASUS UX51VZH", ALC662_FIXUP_BASS_16),
	SND_PCI_QUIRK(0x1043, 0x177d, "ASUS N551", ALC668_FIXUP_ASUS_Nx51),
	SND_PCI_QUIRK(0x1043, 0x17bd, "ASUS N751", ALC668_FIXUP_ASUS_Nx51),
	SND_PCI_QUIRK(0x1043, 0x1963, "ASUS X71SL", ALC662_FIXUP_ASUS_MODE8),
	SND_PCI_QUIRK(0x1043, 0x1b73, "ASUS N55SF", ALC662_FIXUP_BASS_16),
	SND_PCI_QUIRK(0x1043, 0x1bf3, "ASUS N76VZ", ALC662_FIXUP_BASS_MODE4_CHMAP),
	SND_PCI_QUIRK(0x1043, 0x8469, "ASUS mobo", ALC662_FIXUP_NO_JACK_DETECT),
	SND_PCI_QUIRK(0x105b, 0x0cd6, "Foxconn", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x144d, 0xc051, "Samsung R720", ALC662_FIXUP_IDEAPAD),
	SND_PCI_QUIRK(0x17aa, 0x38af, "Lenovo Ideapad Y550P", ALC662_FIXUP_IDEAPAD),
	SND_PCI_QUIRK(0x17aa, 0x3a0d, "Lenovo Ideapad Y550", ALC662_FIXUP_IDEAPAD),
	SND_PCI_QUIRK(0x19da, 0xa130, "Zotac Z68", ALC662_FIXUP_ZOTAC_Z68),
	SND_PCI_QUIRK(0x1b35, 0x2206, "CZC P10T", ALC662_FIXUP_CZC_P10T),

#if 0
	/* Below is a quirk table taken from the old code.
	 * Basically the device should work as is without the fixup table.
	 * If BIOS doesn't give a proper info, enable the corresponding
	 * fixup entry.
	 */
	SND_PCI_QUIRK(0x1043, 0x1000, "ASUS N50Vm", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1092, "ASUS NB", ALC662_FIXUP_ASUS_MODE3),
	SND_PCI_QUIRK(0x1043, 0x1173, "ASUS K73Jn", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x11c3, "ASUS M70V", ALC662_FIXUP_ASUS_MODE3),
	SND_PCI_QUIRK(0x1043, 0x11d3, "ASUS NB", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x11f3, "ASUS NB", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1203, "ASUS NB", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1303, "ASUS G60J", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1333, "ASUS G60Jx", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1339, "ASUS NB", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x13e3, "ASUS N71JA", ALC662_FIXUP_ASUS_MODE7),
	SND_PCI_QUIRK(0x1043, 0x1463, "ASUS N71", ALC662_FIXUP_ASUS_MODE7),
	SND_PCI_QUIRK(0x1043, 0x14d3, "ASUS G72", ALC662_FIXUP_ASUS_MODE8),
	SND_PCI_QUIRK(0x1043, 0x1563, "ASUS N90", ALC662_FIXUP_ASUS_MODE3),
	SND_PCI_QUIRK(0x1043, 0x15d3, "ASUS N50SF F50SF", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x16c3, "ASUS NB", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x16f3, "ASUS K40C K50C", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1733, "ASUS N81De", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1753, "ASUS NB", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1763, "ASUS NB", ALC662_FIXUP_ASUS_MODE6),
	SND_PCI_QUIRK(0x1043, 0x1765, "ASUS NB", ALC662_FIXUP_ASUS_MODE6),
	SND_PCI_QUIRK(0x1043, 0x1783, "ASUS NB", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1793, "ASUS F50GX", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x17b3, "ASUS F70SL", ALC662_FIXUP_ASUS_MODE3),
	SND_PCI_QUIRK(0x1043, 0x17f3, "ASUS X58LE", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1813, "ASUS NB", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1823, "ASUS NB", ALC662_FIXUP_ASUS_MODE5),
	SND_PCI_QUIRK(0x1043, 0x1833, "ASUS NB", ALC662_FIXUP_ASUS_MODE6),
	SND_PCI_QUIRK(0x1043, 0x1843, "ASUS NB", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1853, "ASUS F50Z", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1864, "ASUS NB", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1876, "ASUS NB", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1893, "ASUS M50Vm", ALC662_FIXUP_ASUS_MODE3),
	SND_PCI_QUIRK(0x1043, 0x1894, "ASUS X55", ALC662_FIXUP_ASUS_MODE3),
	SND_PCI_QUIRK(0x1043, 0x18b3, "ASUS N80Vc", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x18c3, "ASUS VX5", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x18d3, "ASUS N81Te", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x18f3, "ASUS N505Tp", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1903, "ASUS F5GL", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1913, "ASUS NB", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1933, "ASUS F80Q", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1943, "ASUS Vx3V", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1953, "ASUS NB", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1963, "ASUS X71C", ALC662_FIXUP_ASUS_MODE3),
	SND_PCI_QUIRK(0x1043, 0x1983, "ASUS N5051A", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1993, "ASUS N20", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x19b3, "ASUS F7Z", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x19c3, "ASUS F5Z/F6x", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x19e3, "ASUS NB", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x19f3, "ASUS NB", ALC662_FIXUP_ASUS_MODE4),
#endif
	{}
};

static const struct hda_model_fixup alc662_fixup_models[] = {
	{.id = ALC272_FIXUP_MARIO, .name = "mario"},
	{.id = ALC662_FIXUP_ASUS_MODE1, .name = "asus-mode1"},
	{.id = ALC662_FIXUP_ASUS_MODE2, .name = "asus-mode2"},
	{.id = ALC662_FIXUP_ASUS_MODE3, .name = "asus-mode3"},
	{.id = ALC662_FIXUP_ASUS_MODE4, .name = "asus-mode4"},
	{.id = ALC662_FIXUP_ASUS_MODE5, .name = "asus-mode5"},
	{.id = ALC662_FIXUP_ASUS_MODE6, .name = "asus-mode6"},
	{.id = ALC662_FIXUP_ASUS_MODE7, .name = "asus-mode7"},
	{.id = ALC662_FIXUP_ASUS_MODE8, .name = "asus-mode8"},
	{.id = ALC662_FIXUP_INV_DMIC, .name = "inv-dmic"},
	{.id = ALC668_FIXUP_DELL_MIC_NO_PRESENCE, .name = "dell-headset-multi"},
	{}
};

static const struct snd_hda_pin_quirk alc662_pin_fixup_tbl[] = {
	SND_HDA_PIN_QUIRK(0x10ec0662, 0x1028, "Dell", ALC662_FIXUP_DELL_MIC_NO_PRESENCE,
		{0x14, 0x01014010},
		{0x18, 0x01a19020},
		{0x1a, 0x0181302f},
		{0x1b, 0x0221401f}),
	SND_HDA_PIN_QUIRK(0x10ec0668, 0x1028, "Dell", ALC668_FIXUP_AUTO_MUTE,
		{0x12, 0x99a30130},
		{0x14, 0x90170110},
		{0x15, 0x0321101f},
		{0x16, 0x03011020}),
	SND_HDA_PIN_QUIRK(0x10ec0668, 0x1028, "Dell", ALC668_FIXUP_AUTO_MUTE,
		{0x12, 0x99a30140},
		{0x14, 0x90170110},
		{0x15, 0x0321101f},
		{0x16, 0x03011020}),
	SND_HDA_PIN_QUIRK(0x10ec0668, 0x1028, "Dell", ALC668_FIXUP_AUTO_MUTE,
		{0x12, 0x99a30150},
		{0x14, 0x90170110},
		{0x15, 0x0321101f},
		{0x16, 0x03011020}),
	SND_HDA_PIN_QUIRK(0x10ec0668, 0x1028, "Dell", ALC668_FIXUP_AUTO_MUTE,
		{0x14, 0x90170110},
		{0x15, 0x0321101f},
		{0x16, 0x03011020}),
	SND_HDA_PIN_QUIRK(0x10ec0668, 0x1028, "Dell XPS 15", ALC668_FIXUP_AUTO_MUTE,
		{0x12, 0x90a60130},
		{0x14, 0x90170110},
		{0x15, 0x0321101f}),
	{}
};

/*
 */
static int patch_alc662(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err;

	err = alc_alloc_spec(codec, 0x0b);
	if (err < 0)
		return err;

	spec = codec->spec;

	spec->shutup = alc_eapd_shutup;

	/* handle multiple HPs as is */
	spec->parse_flags = HDA_PINCFG_NO_HP_FIXUP;

	alc_fix_pll_init(codec, 0x20, 0x04, 15);

	switch (codec->core.vendor_id) {
	case 0x10ec0668:
		spec->init_hook = alc668_restore_default_value;
		break;
	}

	snd_hda_pick_fixup(codec, alc662_fixup_models,
		       alc662_fixup_tbl, alc662_fixups);
	snd_hda_pick_pin_fixup(codec, alc662_pin_fixup_tbl, alc662_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	alc_auto_parse_customize_define(codec);

	if (has_cdefine_beep(codec))
		spec->gen.beep_nid = 0x01;

	if ((alc_get_coef0(codec) & (1 << 14)) &&
	    codec->bus->pci && codec->bus->pci->subsystem_vendor == 0x1025 &&
	    spec->cdefine.platform_type == 1) {
		err = alc_codec_rename(codec, "ALC272X");
		if (err < 0)
			goto error;
	}

	/* automatic parse from the BIOS config */
	err = alc662_parse_auto_config(codec);
	if (err < 0)
		goto error;

	if (!spec->gen.no_analog && spec->gen.beep_nid) {
		switch (codec->core.vendor_id) {
		case 0x10ec0662:
			set_beep_amp(spec, 0x0b, 0x05, HDA_INPUT);
			break;
		case 0x10ec0272:
		case 0x10ec0663:
		case 0x10ec0665:
		case 0x10ec0668:
			set_beep_amp(spec, 0x0b, 0x04, HDA_INPUT);
			break;
		case 0x10ec0273:
			set_beep_amp(spec, 0x0b, 0x03, HDA_INPUT);
			break;
		}
	}

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;

 error:
	alc_free(codec);
	return err;
}

/*
 * ALC680 support
 */

static int alc680_parse_auto_config(struct hda_codec *codec)
{
	return alc_parse_auto_config(codec, NULL, NULL);
}

/*
 */
static int patch_alc680(struct hda_codec *codec)
{
	int err;

	/* ALC680 has no aa-loopback mixer */
	err = alc_alloc_spec(codec, 0);
	if (err < 0)
		return err;

	/* automatic parse from the BIOS config */
	err = alc680_parse_auto_config(codec);
	if (err < 0) {
		alc_free(codec);
		return err;
	}

	return 0;
}

/*
 * patch entries
 */
struct hda_codec_preset snd_hda_preset_realtek[] = {
	{ .id = 0x10ec0260, .name = "ALC260", .patch = patch_alc260 },
	{ .id = 0x10ec0262, .name = "ALC262", .patch = patch_alc262 },
	{ .id = 0x10ec0267, .name = "ALC267", .patch = patch_alc268 },
	{ .id = 0x10ec0268, .name = "ALC268", .patch = patch_alc268 },
	{ .id = 0x10ec0269, .name = "ALC269", .patch = patch_alc269 },
	{ .id = 0x10ec0861, .rev = 0x100340, .name = "ALC660",
	  .patch = patch_alc861 },
	{ .id = 0x10ec0660, .name = "ALC660-VD", .patch = patch_alc861vd },
	{ .id = 0x10ec0861, .name = "ALC861", .patch = patch_alc861 },
	{ .id = 0x10ec0862, .name = "ALC861-VD", .patch = patch_alc861vd },
	{ .id = 0x10ec0662, .rev = 0x100002, .name = "ALC662 rev2",
	  .patch = patch_alc883 },
	{ .id = 0x10ec0662, .rev = 0x100101, .name = "ALC662 rev1",
	  .patch = patch_alc662 },
	{ .id = 0x10ec0663, .name = "ALC663", .patch = patch_alc662 },
	{ .id = 0x10ec0880, .name = "ALC880", .patch = patch_alc880 },
	{ .id = 0x10ec0882, .name = "ALC882", .patch = patch_alc882 },
	{ .id = 0x10ec0883, .name = "ALC883", .patch = patch_alc883 },
	{ .id = 0x10ec0885, .rev = 0x100103, .name = "ALC889A",
	  .patch = patch_alc882 }, /* should be patch_alc883() in future */
	{ .id = 0x10ec0885, .name = "ALC885", .patch = patch_alc882 },
	{ .id = 0x10ec0888, .name = "ALC888", .patch = patch_alc883 },
	{ .id = 0x10ec0889, .name = "ALC889", .patch = patch_alc883 },
	{} /* terminator */
};
static const struct hda_device_id snd_hda_id_realtek[] = {
	HDA_CODEC_ENTRY(0x10ec0221, "ALC221", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0225, "ALC225", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0231, "ALC231", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0233, "ALC233", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0234, "ALC234", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0235, "ALC233", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0236, "ALC236", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0255, "ALC255", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0256, "ALC256", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0260, "ALC260", patch_alc260),
	HDA_CODEC_ENTRY(0x10ec0262, "ALC262", patch_alc262),
	HDA_CODEC_ENTRY(0x10ec0267, "ALC267", patch_alc268),
	HDA_CODEC_ENTRY(0x10ec0268, "ALC268", patch_alc268),
	HDA_CODEC_ENTRY(0x10ec0269, "ALC269", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0270, "ALC270", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0272, "ALC272", patch_alc662),
	HDA_CODEC_ENTRY(0x10ec0274, "ALC274", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0275, "ALC275", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0276, "ALC276", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0280, "ALC280", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0282, "ALC282", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0283, "ALC283", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0284, "ALC284", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0285, "ALC285", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0286, "ALC286", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0288, "ALC288", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0290, "ALC290", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0292, "ALC292", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0293, "ALC293", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0294, "ALC294", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0295, "ALC295", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0298, "ALC298", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0299, "ALC299", patch_alc269),
	HDA_CODEC_REV_ENTRY(0x10ec0861, 0x100340, "ALC660", patch_alc861),
	HDA_CODEC_ENTRY(0x10ec0660, "ALC660-VD", patch_alc861vd),
	HDA_CODEC_ENTRY(0x10ec0861, "ALC861", patch_alc861),
	HDA_CODEC_ENTRY(0x10ec0862, "ALC861-VD", patch_alc861vd),
	HDA_CODEC_REV_ENTRY(0x10ec0662, 0x100002, "ALC662 rev2", patch_alc882),
	HDA_CODEC_REV_ENTRY(0x10ec0662, 0x100101, "ALC662 rev1", patch_alc662),
	HDA_CODEC_REV_ENTRY(0x10ec0662, 0x100300, "ALC662 rev3", patch_alc662),
	HDA_CODEC_ENTRY(0x10ec0663, "ALC663", patch_alc662),
	HDA_CODEC_ENTRY(0x10ec0665, "ALC665", patch_alc662),
	HDA_CODEC_ENTRY(0x10ec0667, "ALC667", patch_alc662),
	HDA_CODEC_ENTRY(0x10ec0668, "ALC668", patch_alc662),
	HDA_CODEC_ENTRY(0x10ec0670, "ALC670", patch_alc662),
	HDA_CODEC_ENTRY(0x10ec0671, "ALC671", patch_alc662),
	HDA_CODEC_ENTRY(0x10ec0680, "ALC680", patch_alc680),
	HDA_CODEC_ENTRY(0x10ec0700, "ALC700", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0701, "ALC701", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0703, "ALC703", patch_alc269),
	HDA_CODEC_ENTRY(0x10ec0867, "ALC891", patch_alc882),
	HDA_CODEC_ENTRY(0x10ec0880, "ALC880", patch_alc880),
	HDA_CODEC_ENTRY(0x10ec0882, "ALC882", patch_alc882),
	HDA_CODEC_ENTRY(0x10ec0883, "ALC883", patch_alc882),
	HDA_CODEC_REV_ENTRY(0x10ec0885, 0x100101, "ALC889A", patch_alc882),
	HDA_CODEC_REV_ENTRY(0x10ec0885, 0x100103, "ALC889A", patch_alc882),
	HDA_CODEC_ENTRY(0x10ec0885, "ALC885", patch_alc882),
	HDA_CODEC_ENTRY(0x10ec0887, "ALC887", patch_alc882),
	HDA_CODEC_REV_ENTRY(0x10ec0888, 0x100101, "ALC1200", patch_alc882),
	HDA_CODEC_ENTRY(0x10ec0888, "ALC888", patch_alc882),
	HDA_CODEC_ENTRY(0x10ec0889, "ALC889", patch_alc882),
	HDA_CODEC_ENTRY(0x10ec0892, "ALC892", patch_alc662),
	HDA_CODEC_ENTRY(0x10ec0897, "ALC897", patch_alc662),
	HDA_CODEC_ENTRY(0x10ec0899, "ALC898", patch_alc882),
	HDA_CODEC_ENTRY(0x10ec0900, "ALC1150", patch_alc882),
	{} /* terminator */
};
MODULE_DEVICE_TABLE(hdaudio, snd_hda_id_realtek);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Realtek HD-audio codec");

static struct hda_codec_driver realtek_driver = {
	.id = snd_hda_id_realtek,
};

module_hda_codec_driver(realtek_driver);
