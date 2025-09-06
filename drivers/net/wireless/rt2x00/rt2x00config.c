/*
	Copyright (C) 2004 - 2008 rt2x00 SourceForge Project
	Copyright (C) 2004 - 2009 Ivo van Doorn <IvDoorn@gmail.com>
	<http://rt2x00.serialmonkey.com>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the
	Free Software Foundation, Inc.,
	59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
	along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/*
	Module: rt2x00lib
	Abstract: rt2x00 generic configuration routines.
 */

#include <linux/kernel.h>
#include <linux/module.h>

#include "rt2x00.h"
#include "rt2x00lib.h"

void rt2x00lib_config_intf(struct rt2x00_dev *rt2x00dev,
			   struct rt2x00_intf *intf,
			   enum ieee80211_if_types type,
			   u8 *mac, u8 *bssid)
			   enum nl80211_iftype type,
			   const u8 *mac, const u8 *bssid)
{
	struct rt2x00intf_conf conf;
	unsigned int flags = 0;

	conf.type = type;

	switch (type) {
	case IEEE80211_IF_TYPE_IBSS:
	case IEEE80211_IF_TYPE_AP:
		conf.sync = TSF_SYNC_BEACON;
		break;
	case IEEE80211_IF_TYPE_STA:
	case NL80211_IFTYPE_ADHOC:
		conf.sync = TSF_SYNC_ADHOC;
		break;
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_WDS:
		conf.sync = TSF_SYNC_AP_NONE;
		break;
	case NL80211_IFTYPE_STATION:
		conf.sync = TSF_SYNC_INFRA;
		break;
	default:
		conf.sync = TSF_SYNC_NONE;
		break;
	}

	/*
	 * Note that when NULL is passed as address we will send
	 * 00:00:00:00:00 to the device to clear the address.
	 * This will prevent the device being confused when it wants
	 * to ACK frames or consideres itself associated.
	 */
	memset(&conf.mac, 0, sizeof(conf.mac));
	if (mac)
		memcpy(&conf.mac, mac, ETH_ALEN);

	memset(&conf.bssid, 0, sizeof(conf.bssid));
	if (bssid)
		memcpy(&conf.bssid, bssid, ETH_ALEN);
	 * to ACK frames or considers itself associated.
	 */
	memset(conf.mac, 0, sizeof(conf.mac));
	if (mac)
		memcpy(conf.mac, mac, ETH_ALEN);

	memset(conf.bssid, 0, sizeof(conf.bssid));
	if (bssid)
		memcpy(conf.bssid, bssid, ETH_ALEN);

	flags |= CONFIG_UPDATE_TYPE;
	if (mac || (!rt2x00dev->intf_ap_count && !rt2x00dev->intf_sta_count))
		flags |= CONFIG_UPDATE_MAC;
	if (bssid || (!rt2x00dev->intf_ap_count && !rt2x00dev->intf_sta_count))
		flags |= CONFIG_UPDATE_BSSID;

	rt2x00dev->ops->lib->config_intf(rt2x00dev, intf, &conf, flags);
}

void rt2x00lib_config_erp(struct rt2x00_dev *rt2x00dev,
			  struct rt2x00_intf *intf,
			  struct ieee80211_bss_conf *bss_conf)
			  struct ieee80211_bss_conf *bss_conf,
			  u32 changed)
{
	struct rt2x00lib_erp erp;

	memset(&erp, 0, sizeof(erp));

	erp.short_preamble = bss_conf->use_short_preamble;
	erp.cts_protection = bss_conf->use_cts_prot;

	erp.ack_timeout = PLCP + get_duration(ACK_SIZE, 10);
	erp.ack_consume_time = SIFS + PLCP + get_duration(ACK_SIZE, 10);

	if (rt2x00dev->hw->conf.flags & IEEE80211_CONF_SHORT_SLOT_TIME)
		erp.ack_timeout += SHORT_DIFS;
	else
		erp.ack_timeout += DIFS;

	if (bss_conf->use_short_preamble) {
		erp.ack_timeout += SHORT_PREAMBLE;
		erp.ack_consume_time += SHORT_PREAMBLE;
	} else {
		erp.ack_timeout += PREAMBLE;
		erp.ack_consume_time += PREAMBLE;
	}

	rt2x00dev->ops->lib->config_erp(rt2x00dev, &erp);
}

void rt2x00lib_config_antenna(struct rt2x00_dev *rt2x00dev,
			      enum antenna rx, enum antenna tx)
{
	struct rt2x00lib_conf libconf;

	libconf.ant.rx = rx;
	libconf.ant.tx = tx;

	if (rx == rt2x00dev->link.ant.active.rx &&
	    tx == rt2x00dev->link.ant.active.tx)
		return;
	erp.slot_time = bss_conf->use_short_slot ? SHORT_SLOT_TIME : SLOT_TIME;
	erp.sifs = SIFS;
	erp.pifs = bss_conf->use_short_slot ? SHORT_PIFS : PIFS;
	erp.difs = bss_conf->use_short_slot ? SHORT_DIFS : DIFS;
	erp.eifs = bss_conf->use_short_slot ? SHORT_EIFS : EIFS;

	erp.basic_rates = bss_conf->basic_rates;
	erp.beacon_int = bss_conf->beacon_int;

	/* Update the AID, this is needed for dynamic PS support */
	rt2x00dev->aid = bss_conf->assoc ? bss_conf->aid : 0;
	rt2x00dev->last_beacon = bss_conf->sync_tsf;

	/* Update global beacon interval time, this is needed for PS support */
	rt2x00dev->beacon_int = bss_conf->beacon_int;

	if (changed & BSS_CHANGED_HT)
		erp.ht_opmode = bss_conf->ht_operation_mode;

	rt2x00dev->ops->lib->config_erp(rt2x00dev, &erp, changed);
}

void rt2x00lib_config_antenna(struct rt2x00_dev *rt2x00dev,
			      struct antenna_setup config)
{
	struct link_ant *ant = &rt2x00dev->link.ant;
	struct antenna_setup *def = &rt2x00dev->default_ant;
	struct antenna_setup *active = &rt2x00dev->link.ant.active;

	/*
	 * When the caller tries to send the SW diversity,
	 * we must update the ANTENNA_RX_DIVERSITY flag to
	 * enable the antenna diversity in the link tuner.
	 *
	 * Secondly, we must guarentee we never send the
	 * software antenna diversity command to the driver.
	 */
	if (!(ant->flags & ANTENNA_RX_DIVERSITY)) {
		if (config.rx == ANTENNA_SW_DIVERSITY) {
			ant->flags |= ANTENNA_RX_DIVERSITY;

			if (def->rx == ANTENNA_SW_DIVERSITY)
				config.rx = ANTENNA_B;
			else
				config.rx = def->rx;
		}
	} else if (config.rx == ANTENNA_SW_DIVERSITY)
		config.rx = active->rx;

	if (!(ant->flags & ANTENNA_TX_DIVERSITY)) {
		if (config.tx == ANTENNA_SW_DIVERSITY) {
			ant->flags |= ANTENNA_TX_DIVERSITY;

			if (def->tx == ANTENNA_SW_DIVERSITY)
				config.tx = ANTENNA_B;
			else
				config.tx = def->tx;
		}
	} else if (config.tx == ANTENNA_SW_DIVERSITY)
		config.tx = active->tx;

	/*
	 * Antenna setup changes require the RX to be disabled,
	 * else the changes will be ignored by the device.
	 */
	if (test_bit(DEVICE_ENABLED_RADIO, &rt2x00dev->flags))
		rt2x00lib_toggle_rx(rt2x00dev, STATE_RADIO_RX_OFF_LINK);
	if (test_bit(DEVICE_STATE_ENABLED_RADIO, &rt2x00dev->flags))
		rt2x00queue_stop_queue(rt2x00dev->rx);

	/*
	 * Write new antenna setup to device and reset the link tuner.
	 * The latter is required since we need to recalibrate the
	 * noise-sensitivity ratio for the new setup.
	 */
	rt2x00dev->ops->lib->config(rt2x00dev, &libconf, CONFIG_UPDATE_ANTENNA);
	rt2x00lib_reset_link_tuner(rt2x00dev);
	rt2x00_reset_link_ant_rssi(&rt2x00dev->link);

	rt2x00dev->link.ant.active.rx = libconf.ant.rx;
	rt2x00dev->link.ant.active.tx = libconf.ant.tx;

	if (test_bit(DEVICE_ENABLED_RADIO, &rt2x00dev->flags))
		rt2x00lib_toggle_rx(rt2x00dev, STATE_RADIO_RX_ON_LINK);
}

static u32 rt2x00lib_get_basic_rates(struct ieee80211_supported_band *band)
{
	const struct rt2x00_rate *rate;
	unsigned int i;
	u32 mask = 0;

	for (i = 0; i < band->n_bitrates; i++) {
		rate = rt2x00_get_rate(band->bitrates[i].hw_value);
		if (rate->flags & DEV_RATE_BASIC)
			mask |= rate->ratemask;
	}

	return mask;
}

void rt2x00lib_config(struct rt2x00_dev *rt2x00dev,
		      struct ieee80211_conf *conf, const int force_config)
{
	struct rt2x00lib_conf libconf;
	struct ieee80211_supported_band *band;
	struct antenna_setup *default_ant = &rt2x00dev->default_ant;
	struct antenna_setup *active_ant = &rt2x00dev->link.ant.active;
	int flags = 0;
	int short_slot_time;

	/*
	 * In some situations we want to force all configurations
	 * to be reloaded (When resuming for instance).
	 */
	if (force_config) {
		flags = CONFIG_UPDATE_ALL;
		goto config;
	}

	/*
	 * Check which configuration options have been
	 * updated and should be send to the device.
	 */
	if (rt2x00dev->rx_status.band != conf->channel->band)
		flags |= CONFIG_UPDATE_PHYMODE;
	if (rt2x00dev->rx_status.freq != conf->channel->center_freq)
		flags |= CONFIG_UPDATE_CHANNEL;
	if (rt2x00dev->tx_power != conf->power_level)
		flags |= CONFIG_UPDATE_TXPOWER;

	/*
	 * Determining changes in the antenna setups request several checks:
	 * antenna_sel_{r,t}x = 0
	 *    -> Does active_{r,t}x match default_{r,t}x
	 *    -> Is default_{r,t}x SW_DIVERSITY
	 * antenna_sel_{r,t}x = 1/2
	 *    -> Does active_{r,t}x match antenna_sel_{r,t}x
	 * The reason for not updating the antenna while SW diversity
	 * should be used is simple: Software diversity means that
	 * we should switch between the antenna's based on the
	 * quality. This means that the current antenna is good enough
	 * to work with untill the link tuner decides that an antenna
	 * switch should be performed.
	 */
	if (!conf->antenna_sel_rx &&
	    default_ant->rx != ANTENNA_SW_DIVERSITY &&
	    default_ant->rx != active_ant->rx)
		flags |= CONFIG_UPDATE_ANTENNA;
	else if (conf->antenna_sel_rx &&
		 conf->antenna_sel_rx != active_ant->rx)
		flags |= CONFIG_UPDATE_ANTENNA;
	else if (active_ant->rx == ANTENNA_SW_DIVERSITY)
		flags |= CONFIG_UPDATE_ANTENNA;

	if (!conf->antenna_sel_tx &&
	    default_ant->tx != ANTENNA_SW_DIVERSITY &&
	    default_ant->tx != active_ant->tx)
		flags |= CONFIG_UPDATE_ANTENNA;
	else if (conf->antenna_sel_tx &&
		 conf->antenna_sel_tx != active_ant->tx)
		flags |= CONFIG_UPDATE_ANTENNA;
	else if (active_ant->tx == ANTENNA_SW_DIVERSITY)
		flags |= CONFIG_UPDATE_ANTENNA;

	/*
	 * The following configuration options are never
	 * stored anywhere and will always be updated.
	 */
	flags |= CONFIG_UPDATE_SLOT_TIME;
	flags |= CONFIG_UPDATE_BEACON_INT;

	/*
	 * We have determined what options should be updated,
	 * now precalculate device configuration values depending
	 * on what configuration options need to be updated.
	 */
config:
	memset(&libconf, 0, sizeof(libconf));

	if (flags & CONFIG_UPDATE_PHYMODE) {
		band = &rt2x00dev->bands[conf->channel->band];

		libconf.band = conf->channel->band;
		libconf.basic_rates = rt2x00lib_get_basic_rates(band);
	}

	if (flags & CONFIG_UPDATE_CHANNEL) {
		memcpy(&libconf.rf,
		       &rt2x00dev->spec.channels[conf->channel->hw_value],
		       sizeof(libconf.rf));
	}

	if (flags & CONFIG_UPDATE_ANTENNA) {
		if (conf->antenna_sel_rx)
			libconf.ant.rx = conf->antenna_sel_rx;
		else if (default_ant->rx != ANTENNA_SW_DIVERSITY)
			libconf.ant.rx = default_ant->rx;
		else if (active_ant->rx == ANTENNA_SW_DIVERSITY)
			libconf.ant.rx = ANTENNA_B;
		else
			libconf.ant.rx = active_ant->rx;

		if (conf->antenna_sel_tx)
			libconf.ant.tx = conf->antenna_sel_tx;
		else if (default_ant->tx != ANTENNA_SW_DIVERSITY)
			libconf.ant.tx = default_ant->tx;
		else if (active_ant->tx == ANTENNA_SW_DIVERSITY)
			libconf.ant.tx = ANTENNA_B;
		else
			libconf.ant.tx = active_ant->tx;
	}

	if (flags & CONFIG_UPDATE_SLOT_TIME) {
		short_slot_time = conf->flags & IEEE80211_CONF_SHORT_SLOT_TIME;

		libconf.slot_time =
		    short_slot_time ? SHORT_SLOT_TIME : SLOT_TIME;
		libconf.sifs = SIFS;
		libconf.pifs = short_slot_time ? SHORT_PIFS : PIFS;
		libconf.difs = short_slot_time ? SHORT_DIFS : DIFS;
		libconf.eifs = short_slot_time ? SHORT_EIFS : EIFS;
	}

	libconf.conf = conf;

	/*
	 * Start configuration.
	 */
	rt2x00dev->ops->lib->config(rt2x00dev, &libconf, flags);
	rt2x00dev->ops->lib->config_ant(rt2x00dev, &config);

	rt2x00link_reset_tuner(rt2x00dev, true);

	memcpy(active, &config, sizeof(config));

	if (test_bit(DEVICE_STATE_ENABLED_RADIO, &rt2x00dev->flags))
		rt2x00queue_start_queue(rt2x00dev->rx);
}

static u16 rt2x00ht_center_channel(struct rt2x00_dev *rt2x00dev,
				   struct ieee80211_conf *conf)
{
	struct hw_mode_spec *spec = &rt2x00dev->spec;
	int center_channel;
	u16 i;

	/*
	 * Initialize center channel to current channel.
	 */
	center_channel = spec->channels[conf->chandef.chan->hw_value].channel;

	/*
	 * Adjust center channel to HT40+ and HT40- operation.
	 */
	if (conf_is_ht40_plus(conf))
		center_channel += 2;
	else if (conf_is_ht40_minus(conf))
		center_channel -= (center_channel == 14) ? 1 : 2;

	for (i = 0; i < spec->num_channels; i++)
		if (spec->channels[i].channel == center_channel)
			return i;

	WARN_ON(1);
	return conf->chandef.chan->hw_value;
}

void rt2x00lib_config(struct rt2x00_dev *rt2x00dev,
		      struct ieee80211_conf *conf,
		      unsigned int ieee80211_flags)
{
	struct rt2x00lib_conf libconf;
	u16 hw_value;
	u16 autowake_timeout;
	u16 beacon_int;
	u16 beacon_diff;

	memset(&libconf, 0, sizeof(libconf));

	libconf.conf = conf;

	if (ieee80211_flags & IEEE80211_CONF_CHANGE_CHANNEL) {
		if (!conf_is_ht(conf))
			set_bit(CONFIG_HT_DISABLED, &rt2x00dev->flags);
		else
			clear_bit(CONFIG_HT_DISABLED, &rt2x00dev->flags);

		if (conf_is_ht40(conf)) {
			set_bit(CONFIG_CHANNEL_HT40, &rt2x00dev->flags);
			hw_value = rt2x00ht_center_channel(rt2x00dev, conf);
		} else {
			clear_bit(CONFIG_CHANNEL_HT40, &rt2x00dev->flags);
			hw_value = conf->chandef.chan->hw_value;
		}

		memcpy(&libconf.rf,
		       &rt2x00dev->spec.channels[hw_value],
		       sizeof(libconf.rf));

		memcpy(&libconf.channel,
		       &rt2x00dev->spec.channels_info[hw_value],
		       sizeof(libconf.channel));

		/* Used for VCO periodic calibration */
		rt2x00dev->rf_channel = libconf.rf.channel;
	}

	if (rt2x00_has_cap_flag(rt2x00dev, REQUIRE_PS_AUTOWAKE) &&
	    (ieee80211_flags & IEEE80211_CONF_CHANGE_PS))
		cancel_delayed_work_sync(&rt2x00dev->autowakeup_work);

	/*
	 * Start configuration.
	 */
	rt2x00dev->ops->lib->config(rt2x00dev, &libconf, ieee80211_flags);

	/*
	 * Some configuration changes affect the link quality
	 * which means we need to reset the link tuner.
	 */
	if (flags & (CONFIG_UPDATE_CHANNEL | CONFIG_UPDATE_ANTENNA))
		rt2x00lib_reset_link_tuner(rt2x00dev);

	if (flags & CONFIG_UPDATE_PHYMODE) {
		rt2x00dev->curr_band = conf->channel->band;
		rt2x00dev->rx_status.band = conf->channel->band;
	}

	rt2x00dev->rx_status.freq = conf->channel->center_freq;
	rt2x00dev->tx_power = conf->power_level;

	if (flags & CONFIG_UPDATE_ANTENNA) {
		rt2x00dev->link.ant.active.rx = libconf.ant.rx;
		rt2x00dev->link.ant.active.tx = libconf.ant.tx;
	}
	if (ieee80211_flags & IEEE80211_CONF_CHANGE_CHANNEL)
		rt2x00link_reset_tuner(rt2x00dev, false);

	if (test_bit(DEVICE_STATE_PRESENT, &rt2x00dev->flags) &&
	    rt2x00_has_cap_flag(rt2x00dev, REQUIRE_PS_AUTOWAKE) &&
	    (ieee80211_flags & IEEE80211_CONF_CHANGE_PS) &&
	    (conf->flags & IEEE80211_CONF_PS)) {
		beacon_diff = (long)jiffies - (long)rt2x00dev->last_beacon;
		beacon_int = msecs_to_jiffies(rt2x00dev->beacon_int);

		if (beacon_diff > beacon_int)
			beacon_diff = 0;

		autowake_timeout = (conf->ps_dtim_period * beacon_int) - beacon_diff;
		queue_delayed_work(rt2x00dev->workqueue,
				   &rt2x00dev->autowakeup_work,
				   autowake_timeout - 15);
	}

	if (conf->flags & IEEE80211_CONF_PS)
		set_bit(CONFIG_POWERSAVING, &rt2x00dev->flags);
	else
		clear_bit(CONFIG_POWERSAVING, &rt2x00dev->flags);

	rt2x00dev->curr_band = conf->chandef.chan->band;
	rt2x00dev->curr_freq = conf->chandef.chan->center_freq;
	rt2x00dev->tx_power = conf->power_level;
	rt2x00dev->short_retry = conf->short_frame_max_tx_count;
	rt2x00dev->long_retry = conf->long_frame_max_tx_count;
}
