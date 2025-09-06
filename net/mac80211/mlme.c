/*
 * BSS client mode implementation
 * Copyright 2003, Jouni Malinen <jkmaline@cc.hut.fi>
 * Copyright 2003-2008, Jouni Malinen <j@w1.fi>
 * Copyright 2004, Instant802 Networks, Inc.
 * Copyright 2005, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007, Michael Wu <flamingice@sourmilk.net>
 * Copyright 2013-2014  Intel Mobile Communications GmbH
 * Copyright (C) 2015 Intel Deutschland GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* TODO:
 * order BSS list by RSSI(?) ("quality of AP")
 * scan result table filtering (by capability (privacy, IBSS/BSS, WPA/RSN IE,
 *    SSID)
 */
#include <linux/delay.h>
#include <linux/if_ether.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/wireless.h>
#include <linux/random.h>
#include <linux/etherdevice.h>
#include <linux/rtnetlink.h>
#include <net/iw_handler.h>
#include <asm/types.h>

#include <net/mac80211.h>
#include "ieee80211_i.h"
#include "rate.h"
#include "led.h"
#include "mesh.h"

#define IEEE80211_AUTH_TIMEOUT (HZ / 5)
#define IEEE80211_AUTH_MAX_TRIES 3
#define IEEE80211_ASSOC_TIMEOUT (HZ / 5)
#define IEEE80211_ASSOC_MAX_TRIES 3
#define IEEE80211_MONITORING_INTERVAL (2 * HZ)
#define IEEE80211_MESH_HOUSEKEEPING_INTERVAL (60 * HZ)
#define IEEE80211_PROBE_INTERVAL (60 * HZ)
#define IEEE80211_RETRY_AUTH_INTERVAL (1 * HZ)
#define IEEE80211_SCAN_INTERVAL (2 * HZ)
#define IEEE80211_SCAN_INTERVAL_SLOW (15 * HZ)
#define IEEE80211_IBSS_JOIN_TIMEOUT (7 * HZ)

#define IEEE80211_PROBE_DELAY (HZ / 33)
#define IEEE80211_CHANNEL_TIME (HZ / 33)
#define IEEE80211_PASSIVE_CHANNEL_TIME (HZ / 5)
#define IEEE80211_SCAN_RESULT_EXPIRE (10 * HZ)
#define IEEE80211_IBSS_MERGE_INTERVAL (30 * HZ)
#define IEEE80211_IBSS_INACTIVITY_LIMIT (60 * HZ)
#define IEEE80211_MESH_PEER_INACTIVITY_LIMIT (1800 * HZ)

#define IEEE80211_IBSS_MAX_STA_ENTRIES 128


#define ERP_INFO_USE_PROTECTION BIT(1)

/* mgmt header + 1 byte action code */
#define IEEE80211_MIN_ACTION_SIZE (24 + 1)

#define IEEE80211_ADDBA_PARAM_POLICY_MASK 0x0002
#define IEEE80211_ADDBA_PARAM_TID_MASK 0x003C
#define IEEE80211_ADDBA_PARAM_BUF_SIZE_MASK 0xFFA0
#define IEEE80211_DELBA_PARAM_TID_MASK 0xF000
#define IEEE80211_DELBA_PARAM_INITIATOR_MASK 0x0800

/* next values represent the buffer size for A-MPDU frame.
 * According to IEEE802.11n spec size varies from 8K to 64K (in powers of 2) */
#define IEEE80211_MIN_AMPDU_BUF 0x8
#define IEEE80211_MAX_AMPDU_BUF 0x40

static void ieee80211_send_probe_req(struct net_device *dev, u8 *dst,
				     u8 *ssid, size_t ssid_len);
static struct ieee80211_sta_bss *
ieee80211_rx_bss_get(struct net_device *dev, u8 *bssid, int freq,
		     u8 *ssid, u8 ssid_len);
static void ieee80211_rx_bss_put(struct ieee80211_local *local,
				 struct ieee80211_sta_bss *bss);
static int ieee80211_sta_find_ibss(struct net_device *dev,
				   struct ieee80211_if_sta *ifsta);
static int ieee80211_sta_wep_configured(struct net_device *dev);
static int ieee80211_sta_start_scan(struct net_device *dev,
				    u8 *ssid, size_t ssid_len);
static int ieee80211_sta_config_auth(struct net_device *dev,
				     struct ieee80211_if_sta *ifsta);
static void sta_rx_agg_session_timer_expired(unsigned long data);


void ieee802_11_parse_elems(u8 *start, size_t len,
			    struct ieee802_11_elems *elems)
{
	size_t left = len;
	u8 *pos = start;

	memset(elems, 0, sizeof(*elems));

	while (left >= 2) {
		u8 id, elen;

		id = *pos++;
		elen = *pos++;
		left -= 2;

		if (elen > left)
			return;

		switch (id) {
		case WLAN_EID_SSID:
			elems->ssid = pos;
			elems->ssid_len = elen;
			break;
		case WLAN_EID_SUPP_RATES:
			elems->supp_rates = pos;
			elems->supp_rates_len = elen;
			break;
		case WLAN_EID_FH_PARAMS:
			elems->fh_params = pos;
			elems->fh_params_len = elen;
			break;
		case WLAN_EID_DS_PARAMS:
			elems->ds_params = pos;
			elems->ds_params_len = elen;
			break;
		case WLAN_EID_CF_PARAMS:
			elems->cf_params = pos;
			elems->cf_params_len = elen;
			break;
		case WLAN_EID_TIM:
			elems->tim = pos;
			elems->tim_len = elen;
			break;
		case WLAN_EID_IBSS_PARAMS:
			elems->ibss_params = pos;
			elems->ibss_params_len = elen;
			break;
		case WLAN_EID_CHALLENGE:
			elems->challenge = pos;
			elems->challenge_len = elen;
			break;
		case WLAN_EID_WPA:
			if (elen >= 4 && pos[0] == 0x00 && pos[1] == 0x50 &&
			    pos[2] == 0xf2) {
				/* Microsoft OUI (00:50:F2) */
				if (pos[3] == 1) {
					/* OUI Type 1 - WPA IE */
					elems->wpa = pos;
					elems->wpa_len = elen;
				} else if (elen >= 5 && pos[3] == 2) {
					if (pos[4] == 0) {
						elems->wmm_info = pos;
						elems->wmm_info_len = elen;
					} else if (pos[4] == 1) {
						elems->wmm_param = pos;
						elems->wmm_param_len = elen;
					}
				}
			}
			break;
		case WLAN_EID_RSN:
			elems->rsn = pos;
			elems->rsn_len = elen;
			break;
		case WLAN_EID_ERP_INFO:
			elems->erp_info = pos;
			elems->erp_info_len = elen;
			break;
		case WLAN_EID_EXT_SUPP_RATES:
			elems->ext_supp_rates = pos;
			elems->ext_supp_rates_len = elen;
			break;
		case WLAN_EID_HT_CAPABILITY:
			elems->ht_cap_elem = pos;
			elems->ht_cap_elem_len = elen;
			break;
		case WLAN_EID_HT_EXTRA_INFO:
			elems->ht_info_elem = pos;
			elems->ht_info_elem_len = elen;
			break;
		case WLAN_EID_MESH_ID:
			elems->mesh_id = pos;
			elems->mesh_id_len = elen;
			break;
		case WLAN_EID_MESH_CONFIG:
			elems->mesh_config = pos;
			elems->mesh_config_len = elen;
			break;
		case WLAN_EID_PEER_LINK:
			elems->peer_link = pos;
			elems->peer_link_len = elen;
			break;
		case WLAN_EID_PREQ:
			elems->preq = pos;
			elems->preq_len = elen;
			break;
		case WLAN_EID_PREP:
			elems->prep = pos;
			elems->prep_len = elen;
			break;
		case WLAN_EID_PERR:
			elems->perr = pos;
			elems->perr_len = elen;
			break;
		case WLAN_EID_CHANNEL_SWITCH:
			elems->ch_switch_elem = pos;
			elems->ch_switch_elem_len = elen;
			break;
		case WLAN_EID_QUIET:
			if (!elems->quiet_elem) {
				elems->quiet_elem = pos;
				elems->quiet_elem_len = elen;
			}
			elems->num_of_quiet_elem++;
			break;
		case WLAN_EID_COUNTRY:
			elems->country_elem = pos;
			elems->country_elem_len = elen;
			break;
		case WLAN_EID_PWR_CONSTRAINT:
			elems->pwr_constr_elem = pos;
			elems->pwr_constr_elem_len = elen;
			break;
		default:
			break;
		}

		left -= elen;
		pos += elen;
	}
}

#include <linux/delay.h>
#include <linux/if_ether.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/etherdevice.h>
#include <linux/moduleparam.h>
#include <linux/rtnetlink.h>
#include <linux/crc32.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <net/mac80211.h>
#include <asm/unaligned.h>

#include "ieee80211_i.h"
#include "driver-ops.h"
#include "rate.h"
#include "led.h"

#define IEEE80211_AUTH_TIMEOUT		(HZ / 5)
#define IEEE80211_AUTH_TIMEOUT_LONG	(HZ / 2)
#define IEEE80211_AUTH_TIMEOUT_SHORT	(HZ / 10)
#define IEEE80211_AUTH_MAX_TRIES	3
#define IEEE80211_AUTH_WAIT_ASSOC	(HZ * 5)
#define IEEE80211_ASSOC_TIMEOUT		(HZ / 5)
#define IEEE80211_ASSOC_TIMEOUT_LONG	(HZ / 2)
#define IEEE80211_ASSOC_TIMEOUT_SHORT	(HZ / 10)
#define IEEE80211_ASSOC_MAX_TRIES	3

static int max_nullfunc_tries = 2;
module_param(max_nullfunc_tries, int, 0644);
MODULE_PARM_DESC(max_nullfunc_tries,
		 "Maximum nullfunc tx tries before disconnecting (reason 4).");

static int max_probe_tries = 5;
module_param(max_probe_tries, int, 0644);
MODULE_PARM_DESC(max_probe_tries,
		 "Maximum probe tries before disconnecting (reason 4).");

/*
 * Beacon loss timeout is calculated as N frames times the
 * advertised beacon interval.  This may need to be somewhat
 * higher than what hardware might detect to account for
 * delays in the host processing frames. But since we also
 * probe on beacon miss before declaring the connection lost
 * default to what we want.
 */
static int beacon_loss_count = 7;
module_param(beacon_loss_count, int, 0644);
MODULE_PARM_DESC(beacon_loss_count,
		 "Number of beacon intervals before we decide beacon was lost.");

/*
 * Time the connection can be idle before we probe
 * it to see if we can still talk to the AP.
 */
#define IEEE80211_CONNECTION_IDLE_TIME	(30 * HZ)
/*
 * Time we wait for a probe response after sending
 * a probe request because of beacon loss or for
 * checking the connection still works.
 */
static int probe_wait_ms = 500;
module_param(probe_wait_ms, int, 0644);
MODULE_PARM_DESC(probe_wait_ms,
		 "Maximum time(ms) to wait for probe response"
		 " before disconnecting (reason 4).");

/*
 * How many Beacon frames need to have been used in average signal strength
 * before starting to indicate signal change events.
 */
#define IEEE80211_SIGNAL_AVE_MIN_COUNT	4

/*
 * We can have multiple work items (and connection probing)
 * scheduling this timer, but we need to take care to only
 * reschedule it when it should fire _earlier_ than it was
 * asked for before, or if it's not pending right now. This
 * function ensures that. Note that it then is required to
 * run this function for all timeouts after the first one
 * has happened -- the work that runs from this timer will
 * do that.
 */
static void run_again(struct ieee80211_sub_if_data *sdata,
		      unsigned long timeout)
{
	sdata_assert_lock(sdata);

	if (!timer_pending(&sdata->u.mgd.timer) ||
	    time_before(timeout, sdata->u.mgd.timer.expires))
		mod_timer(&sdata->u.mgd.timer, timeout);
}

void ieee80211_sta_reset_beacon_monitor(struct ieee80211_sub_if_data *sdata)
{
	if (sdata->vif.driver_flags & IEEE80211_VIF_BEACON_FILTER)
		return;

	if (ieee80211_hw_check(&sdata->local->hw, CONNECTION_MONITOR))
		return;

	mod_timer(&sdata->u.mgd.bcn_mon_timer,
		  round_jiffies_up(jiffies + sdata->u.mgd.beacon_timeout));
}

void ieee80211_sta_reset_conn_monitor(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	if (unlikely(!sdata->u.mgd.associated))
		return;

	ifmgd->probe_send_count = 0;

	if (ieee80211_hw_check(&sdata->local->hw, CONNECTION_MONITOR))
		return;

	mod_timer(&sdata->u.mgd.conn_mon_timer,
		  round_jiffies_up(jiffies + IEEE80211_CONNECTION_IDLE_TIME));
}

static int ecw2cw(int ecw)
{
	return (1 << ecw) - 1;
}


static void ieee80211_sta_def_wmm_params(struct net_device *dev,
					 struct ieee80211_sta_bss *bss,
					 int ibss)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;
	int i, have_higher_than_11mbit = 0;


	/* cf. IEEE 802.11 9.2.12 */
	for (i = 0; i < bss->supp_rates_len; i++)
		if ((bss->supp_rates[i] & 0x7f) * 5 > 110)
			have_higher_than_11mbit = 1;

	if (local->hw.conf.channel->band == IEEE80211_BAND_2GHZ &&
	    have_higher_than_11mbit)
		sdata->flags |= IEEE80211_SDATA_OPERATING_GMODE;
	else
		sdata->flags &= ~IEEE80211_SDATA_OPERATING_GMODE;


	if (local->ops->conf_tx) {
		struct ieee80211_tx_queue_params qparam;

		memset(&qparam, 0, sizeof(qparam));

		qparam.aifs = 2;

		if (local->hw.conf.channel->band == IEEE80211_BAND_2GHZ &&
		    !(sdata->flags & IEEE80211_SDATA_OPERATING_GMODE))
			qparam.cw_min = 31;
		else
			qparam.cw_min = 15;

		qparam.cw_max = 1023;
		qparam.txop = 0;

		for (i = 0; i < local_to_hw(local)->queues; i++)
			local->ops->conf_tx(local_to_hw(local), i, &qparam);
	}
}

static void ieee80211_sta_wmm_params(struct net_device *dev,
				     struct ieee80211_if_sta *ifsta,
				     u8 *wmm_param, size_t wmm_param_len)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_tx_queue_params params;
	size_t left;
	int count;
	u8 *pos;

	if (!(ifsta->flags & IEEE80211_STA_WMM_ENABLED))
		return;

	if (!wmm_param)
		return;

	if (wmm_param_len < 8 || wmm_param[5] /* version */ != 1)
		return;
	count = wmm_param[6] & 0x0f;
	if (count == ifsta->wmm_last_param_set)
		return;
	ifsta->wmm_last_param_set = count;

	pos = wmm_param + 8;
	left = wmm_param_len - 8;

	memset(&params, 0, sizeof(params));

	if (!local->ops->conf_tx)
		return;

	local->wmm_acm = 0;
	for (; left >= 4; left -= 4, pos += 4) {
		int aci = (pos[0] >> 5) & 0x03;
		int acm = (pos[0] >> 4) & 0x01;
		int queue;

		switch (aci) {
		case 1:
			queue = 3;
			if (acm)
				local->wmm_acm |= BIT(0) | BIT(3);
			break;
		case 2:
			queue = 1;
			if (acm)
				local->wmm_acm |= BIT(4) | BIT(5);
			break;
		case 3:
			queue = 0;
			if (acm)
				local->wmm_acm |= BIT(6) | BIT(7);
			break;
		case 0:
		default:
			queue = 2;
			if (acm)
				local->wmm_acm |= BIT(1) | BIT(2);
			break;
		}

		params.aifs = pos[0] & 0x0f;
		params.cw_max = ecw2cw((pos[1] & 0xf0) >> 4);
		params.cw_min = ecw2cw(pos[1] & 0x0f);
		params.txop = get_unaligned_le16(pos + 2);
#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
		printk(KERN_DEBUG "%s: WMM queue=%d aci=%d acm=%d aifs=%d "
		       "cWmin=%d cWmax=%d txop=%d\n",
		       dev->name, queue, aci, acm, params.aifs, params.cw_min,
		       params.cw_max, params.txop);
#endif
		/* TODO: handle ACM (block TX, fallback to next lowest allowed
		 * AC for now) */
		if (local->ops->conf_tx(local_to_hw(local), queue, &params)) {
			printk(KERN_DEBUG "%s: failed to set TX queue "
			       "parameters for queue %d\n", dev->name, queue);
		}
	}
}

static u32 ieee80211_handle_protect_preamb(struct ieee80211_sub_if_data *sdata,
					   bool use_protection,
					   bool use_short_preamble)
{
	struct ieee80211_bss_conf *bss_conf = &sdata->bss_conf;
#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
	struct ieee80211_if_sta *ifsta = &sdata->u.sta;
	DECLARE_MAC_BUF(mac);
#endif
	u32 changed = 0;

	if (use_protection != bss_conf->use_cts_prot) {
#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
		if (net_ratelimit()) {
			printk(KERN_DEBUG "%s: CTS protection %s (BSSID="
			       "%s)\n",
			       sdata->dev->name,
			       use_protection ? "enabled" : "disabled",
			       print_mac(mac, ifsta->bssid));
		}
#endif
		bss_conf->use_cts_prot = use_protection;
		changed |= BSS_CHANGED_ERP_CTS_PROT;
	}

	if (use_short_preamble != bss_conf->use_short_preamble) {
#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
		if (net_ratelimit()) {
			printk(KERN_DEBUG "%s: switched to %s barker preamble"
			       " (BSSID=%s)\n",
			       sdata->dev->name,
			       use_short_preamble ? "short" : "long",
			       print_mac(mac, ifsta->bssid));
		}
#endif
		bss_conf->use_short_preamble = use_short_preamble;
		changed |= BSS_CHANGED_ERP_PREAMBLE;
	}

	return changed;
}

static u32 ieee80211_handle_erp_ie(struct ieee80211_sub_if_data *sdata,
				   u8 erp_value)
{
	bool use_protection = (erp_value & WLAN_ERP_USE_PROTECTION) != 0;
	bool use_short_preamble = (erp_value & WLAN_ERP_BARKER_PREAMBLE) == 0;

	return ieee80211_handle_protect_preamb(sdata,
			use_protection, use_short_preamble);
}

static u32 ieee80211_handle_bss_capability(struct ieee80211_sub_if_data *sdata,
					   struct ieee80211_sta_bss *bss)
{
	u32 changed = 0;

	if (bss->has_erp_value)
		changed |= ieee80211_handle_erp_ie(sdata, bss->erp_value);
	else {
		u16 capab = bss->capability;
		changed |= ieee80211_handle_protect_preamb(sdata, false,
				(capab & WLAN_CAPABILITY_SHORT_PREAMBLE) != 0);
	}

	return changed;
}

int ieee80211_ht_cap_ie_to_ht_info(struct ieee80211_ht_cap *ht_cap_ie,
				   struct ieee80211_ht_info *ht_info)
{

	if (ht_info == NULL)
		return -EINVAL;

	memset(ht_info, 0, sizeof(*ht_info));

	if (ht_cap_ie) {
		u8 ampdu_info = ht_cap_ie->ampdu_params_info;

		ht_info->ht_supported = 1;
		ht_info->cap = le16_to_cpu(ht_cap_ie->cap_info);
		ht_info->ampdu_factor =
			ampdu_info & IEEE80211_HT_CAP_AMPDU_FACTOR;
		ht_info->ampdu_density =
			(ampdu_info & IEEE80211_HT_CAP_AMPDU_DENSITY) >> 2;
		memcpy(ht_info->supp_mcs_set, ht_cap_ie->supp_mcs_set, 16);
	} else
		ht_info->ht_supported = 0;

	return 0;
}

int ieee80211_ht_addt_info_ie_to_ht_bss_info(
			struct ieee80211_ht_addt_info *ht_add_info_ie,
			struct ieee80211_ht_bss_info *bss_info)
{
	if (bss_info == NULL)
		return -EINVAL;

	memset(bss_info, 0, sizeof(*bss_info));

	if (ht_add_info_ie) {
		u16 op_mode;
		op_mode = le16_to_cpu(ht_add_info_ie->operation_mode);

		bss_info->primary_channel = ht_add_info_ie->control_chan;
		bss_info->bss_cap = ht_add_info_ie->ht_param;
		bss_info->bss_op_mode = (u8)(op_mode & 0xff);
	}

	return 0;
}

static void ieee80211_sta_send_associnfo(struct net_device *dev,
					 struct ieee80211_if_sta *ifsta)
{
	char *buf;
	size_t len;
	int i;
	union iwreq_data wrqu;

	if (!ifsta->assocreq_ies && !ifsta->assocresp_ies)
		return;

	buf = kmalloc(50 + 2 * (ifsta->assocreq_ies_len +
				ifsta->assocresp_ies_len), GFP_KERNEL);
	if (!buf)
		return;

	len = sprintf(buf, "ASSOCINFO(");
	if (ifsta->assocreq_ies) {
		len += sprintf(buf + len, "ReqIEs=");
		for (i = 0; i < ifsta->assocreq_ies_len; i++) {
			len += sprintf(buf + len, "%02x",
				       ifsta->assocreq_ies[i]);
		}
	}
	if (ifsta->assocresp_ies) {
		if (ifsta->assocreq_ies)
			len += sprintf(buf + len, " ");
		len += sprintf(buf + len, "RespIEs=");
		for (i = 0; i < ifsta->assocresp_ies_len; i++) {
			len += sprintf(buf + len, "%02x",
				       ifsta->assocresp_ies[i]);
		}
	}
	len += sprintf(buf + len, ")");

	if (len > IW_CUSTOM_MAX) {
		len = sprintf(buf, "ASSOCRESPIE=");
		for (i = 0; i < ifsta->assocresp_ies_len; i++) {
			len += sprintf(buf + len, "%02x",
				       ifsta->assocresp_ies[i]);
		}
	}

	memset(&wrqu, 0, sizeof(wrqu));
	wrqu.data.length = len;
	wireless_send_event(dev, IWEVCUSTOM, &wrqu, buf);

	kfree(buf);
}


static void ieee80211_set_associated(struct net_device *dev,
				     struct ieee80211_if_sta *ifsta,
				     bool assoc)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_conf *conf = &local_to_hw(local)->conf;
	union iwreq_data wrqu;
	u32 changed = BSS_CHANGED_ASSOC;

	if (assoc) {
		struct ieee80211_sta_bss *bss;

		ifsta->flags |= IEEE80211_STA_ASSOCIATED;

		if (sdata->vif.type != IEEE80211_IF_TYPE_STA)
			return;

		bss = ieee80211_rx_bss_get(dev, ifsta->bssid,
					   conf->channel->center_freq,
					   ifsta->ssid, ifsta->ssid_len);
		if (bss) {
			/* set timing information */
			sdata->bss_conf.beacon_int = bss->beacon_int;
			sdata->bss_conf.timestamp = bss->timestamp;
			sdata->bss_conf.dtim_period = bss->dtim_period;

			changed |= ieee80211_handle_bss_capability(sdata, bss);

			ieee80211_rx_bss_put(local, bss);
		}

		if (conf->flags & IEEE80211_CONF_SUPPORT_HT_MODE) {
			changed |= BSS_CHANGED_HT;
			sdata->bss_conf.assoc_ht = 1;
			sdata->bss_conf.ht_conf = &conf->ht_conf;
			sdata->bss_conf.ht_bss_conf = &conf->ht_bss_conf;
		}

		ifsta->flags |= IEEE80211_STA_PREV_BSSID_SET;
		memcpy(ifsta->prev_bssid, sdata->u.sta.bssid, ETH_ALEN);
		memcpy(wrqu.ap_addr.sa_data, sdata->u.sta.bssid, ETH_ALEN);
		ieee80211_sta_send_associnfo(dev, ifsta);
	} else {
		netif_carrier_off(dev);
		ieee80211_sta_tear_down_BA_sessions(dev, ifsta->bssid);
		ifsta->flags &= ~IEEE80211_STA_ASSOCIATED;
		changed |= ieee80211_reset_erp_info(dev);

		sdata->bss_conf.assoc_ht = 0;
		sdata->bss_conf.ht_conf = NULL;
		sdata->bss_conf.ht_bss_conf = NULL;

		memset(wrqu.ap_addr.sa_data, 0, ETH_ALEN);
	}
	ifsta->last_probe = jiffies;
	ieee80211_led_assoc(local, assoc);

	sdata->bss_conf.assoc = assoc;
	ieee80211_bss_info_change_notify(sdata, changed);

	if (assoc)
		netif_carrier_on(dev);

	wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	wireless_send_event(dev, SIOCGIWAP, &wrqu, NULL);
}

static void ieee80211_set_disassoc(struct net_device *dev,
				   struct ieee80211_if_sta *ifsta, int deauth)
{
	if (deauth)
		ifsta->auth_tries = 0;
	ifsta->assoc_tries = 0;
	ieee80211_set_associated(dev, ifsta, 0);
}

void ieee80211_sta_tx(struct net_device *dev, struct sk_buff *skb,
		      int encrypt)
{
	struct ieee80211_sub_if_data *sdata;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	skb->dev = sdata->local->mdev;
	skb_set_mac_header(skb, 0);
	skb_set_network_header(skb, 0);
	skb_set_transport_header(skb, 0);

	skb->iif = sdata->dev->ifindex;
	skb->do_not_encrypt = !encrypt;

	dev_queue_xmit(skb);
}


static void ieee80211_send_auth(struct net_device *dev,
				struct ieee80211_if_sta *ifsta,
				int transaction, u8 *extra, size_t extra_len,
				int encrypt)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct sk_buff *skb;
	struct ieee80211_mgmt *mgmt;

	skb = dev_alloc_skb(local->hw.extra_tx_headroom +
			    sizeof(*mgmt) + 6 + extra_len);
	if (!skb) {
		printk(KERN_DEBUG "%s: failed to allocate buffer for auth "
		       "frame\n", dev->name);
		return;
	}
	skb_reserve(skb, local->hw.extra_tx_headroom);

	mgmt = (struct ieee80211_mgmt *) skb_put(skb, 24 + 6);
	memset(mgmt, 0, 24 + 6);
	mgmt->frame_control = IEEE80211_FC(IEEE80211_FTYPE_MGMT,
					   IEEE80211_STYPE_AUTH);
	if (encrypt)
		mgmt->frame_control |= cpu_to_le16(IEEE80211_FCTL_PROTECTED);
	memcpy(mgmt->da, ifsta->bssid, ETH_ALEN);
	memcpy(mgmt->sa, dev->dev_addr, ETH_ALEN);
	memcpy(mgmt->bssid, ifsta->bssid, ETH_ALEN);
	mgmt->u.auth.auth_alg = cpu_to_le16(ifsta->auth_alg);
	mgmt->u.auth.auth_transaction = cpu_to_le16(transaction);
	ifsta->auth_transaction = transaction + 1;
	mgmt->u.auth.status_code = cpu_to_le16(0);
	if (extra)
		memcpy(skb_put(skb, extra_len), extra, extra_len);

	ieee80211_sta_tx(dev, skb, encrypt);
}


static void ieee80211_authenticate(struct net_device *dev,
				   struct ieee80211_if_sta *ifsta)
{
	DECLARE_MAC_BUF(mac);

	ifsta->auth_tries++;
	if (ifsta->auth_tries > IEEE80211_AUTH_MAX_TRIES) {
		printk(KERN_DEBUG "%s: authentication with AP %s"
		       " timed out\n",
		       dev->name, print_mac(mac, ifsta->bssid));
		ifsta->state = IEEE80211_DISABLED;
		return;
	}

	ifsta->state = IEEE80211_AUTHENTICATE;
	printk(KERN_DEBUG "%s: authenticate with AP %s\n",
	       dev->name, print_mac(mac, ifsta->bssid));

	ieee80211_send_auth(dev, ifsta, 1, NULL, 0, 0);

	mod_timer(&ifsta->timer, jiffies + IEEE80211_AUTH_TIMEOUT);
}

static int ieee80211_compatible_rates(struct ieee80211_sta_bss *bss,
				      struct ieee80211_supported_band *sband,
				      u64 *rates)
{
	int i, j, count;
	*rates = 0;
	count = 0;
	for (i = 0; i < bss->supp_rates_len; i++) {
		int rate = (bss->supp_rates[i] & 0x7F) * 5;

		for (j = 0; j < sband->n_bitrates; j++)
			if (sband->bitrates[j].bitrate == rate) {
				*rates |= BIT(j);
				count++;
				break;
			}
	}

	return count;
}

static void ieee80211_send_assoc(struct net_device *dev,
				 struct ieee80211_if_sta *ifsta)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct sk_buff *skb;
	struct ieee80211_mgmt *mgmt;
	u8 *pos, *ies;
	int i, len, count, rates_len, supp_rates_len;
	u16 capab;
	struct ieee80211_sta_bss *bss;
	int wmm = 0;
	struct ieee80211_supported_band *sband;
	u64 rates = 0;

	skb = dev_alloc_skb(local->hw.extra_tx_headroom +
			    sizeof(*mgmt) + 200 + ifsta->extra_ie_len +
			    ifsta->ssid_len);
	if (!skb) {
		printk(KERN_DEBUG "%s: failed to allocate buffer for assoc "
		       "frame\n", dev->name);
		return;
	}
	skb_reserve(skb, local->hw.extra_tx_headroom);

	sband = local->hw.wiphy->bands[local->hw.conf.channel->band];

	capab = ifsta->capab;

	if (local->hw.conf.channel->band == IEEE80211_BAND_2GHZ) {
		if (!(local->hw.flags & IEEE80211_HW_2GHZ_SHORT_SLOT_INCAPABLE))
			capab |= WLAN_CAPABILITY_SHORT_SLOT_TIME;
		if (!(local->hw.flags & IEEE80211_HW_2GHZ_SHORT_PREAMBLE_INCAPABLE))
			capab |= WLAN_CAPABILITY_SHORT_PREAMBLE;
	}

	bss = ieee80211_rx_bss_get(dev, ifsta->bssid,
				   local->hw.conf.channel->center_freq,
				   ifsta->ssid, ifsta->ssid_len);
	if (bss) {
		if (bss->capability & WLAN_CAPABILITY_PRIVACY)
			capab |= WLAN_CAPABILITY_PRIVACY;
		if (bss->wmm_ie)
			wmm = 1;

		/* get all rates supported by the device and the AP as
		 * some APs don't like getting a superset of their rates
		 * in the association request (e.g. D-Link DAP 1353 in
		 * b-only mode) */
		rates_len = ieee80211_compatible_rates(bss, sband, &rates);

		if ((bss->capability & WLAN_CAPABILITY_SPECTRUM_MGMT) &&
		    (local->hw.flags & IEEE80211_HW_SPECTRUM_MGMT))
			capab |= WLAN_CAPABILITY_SPECTRUM_MGMT;

		ieee80211_rx_bss_put(local, bss);
	} else {
		rates = ~0;
		rates_len = sband->n_bitrates;
	}

	mgmt = (struct ieee80211_mgmt *) skb_put(skb, 24);
	memset(mgmt, 0, 24);
	memcpy(mgmt->da, ifsta->bssid, ETH_ALEN);
	memcpy(mgmt->sa, dev->dev_addr, ETH_ALEN);
	memcpy(mgmt->bssid, ifsta->bssid, ETH_ALEN);

	if (ifsta->flags & IEEE80211_STA_PREV_BSSID_SET) {
		skb_put(skb, 10);
		mgmt->frame_control = IEEE80211_FC(IEEE80211_FTYPE_MGMT,
						   IEEE80211_STYPE_REASSOC_REQ);
		mgmt->u.reassoc_req.capab_info = cpu_to_le16(capab);
		mgmt->u.reassoc_req.listen_interval =
				cpu_to_le16(local->hw.conf.listen_interval);
		memcpy(mgmt->u.reassoc_req.current_ap, ifsta->prev_bssid,
		       ETH_ALEN);
	} else {
		skb_put(skb, 4);
		mgmt->frame_control = IEEE80211_FC(IEEE80211_FTYPE_MGMT,
						   IEEE80211_STYPE_ASSOC_REQ);
		mgmt->u.assoc_req.capab_info = cpu_to_le16(capab);
		mgmt->u.reassoc_req.listen_interval =
static u32
ieee80211_determine_chantype(struct ieee80211_sub_if_data *sdata,
			     struct ieee80211_supported_band *sband,
			     struct ieee80211_channel *channel,
			     const struct ieee80211_ht_cap *ht_cap,
			     const struct ieee80211_ht_operation *ht_oper,
			     const struct ieee80211_vht_operation *vht_oper,
			     struct cfg80211_chan_def *chandef, bool tracking)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct cfg80211_chan_def vht_chandef;
	struct ieee80211_sta_ht_cap sta_ht_cap;
	u32 ht_cfreq, ret;

	memcpy(&sta_ht_cap, &sband->ht_cap, sizeof(sta_ht_cap));
	ieee80211_apply_htcap_overrides(sdata, &sta_ht_cap);

	chandef->chan = channel;
	chandef->width = NL80211_CHAN_WIDTH_20_NOHT;
	chandef->center_freq1 = channel->center_freq;
	chandef->center_freq2 = 0;

	if (!ht_cap || !ht_oper || !sta_ht_cap.ht_supported) {
		ret = IEEE80211_STA_DISABLE_HT | IEEE80211_STA_DISABLE_VHT;
		goto out;
	}

	chandef->width = NL80211_CHAN_WIDTH_20;

	if (!(ht_cap->cap_info &
	      cpu_to_le16(IEEE80211_HT_CAP_SUP_WIDTH_20_40))) {
		ret = IEEE80211_STA_DISABLE_40MHZ;
		vht_chandef = *chandef;
		goto out;
	}

	ht_cfreq = ieee80211_channel_to_frequency(ht_oper->primary_chan,
						  channel->band);
	/* check that channel matches the right operating channel */
	if (!tracking && channel->center_freq != ht_cfreq) {
		/*
		 * It's possible that some APs are confused here;
		 * Netgear WNDR3700 sometimes reports 4 higher than
		 * the actual channel in association responses, but
		 * since we look at probe response/beacon data here
		 * it should be OK.
		 */
		sdata_info(sdata,
			   "Wrong control channel: center-freq: %d ht-cfreq: %d ht->primary_chan: %d band: %d - Disabling HT\n",
			   channel->center_freq, ht_cfreq,
			   ht_oper->primary_chan, channel->band);
		ret = IEEE80211_STA_DISABLE_HT | IEEE80211_STA_DISABLE_VHT;
		goto out;
	}

	/* check 40 MHz support, if we have it */
	if (sta_ht_cap.cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40) {
		switch (ht_oper->ht_param & IEEE80211_HT_PARAM_CHA_SEC_OFFSET) {
		case IEEE80211_HT_PARAM_CHA_SEC_ABOVE:
			chandef->width = NL80211_CHAN_WIDTH_40;
			chandef->center_freq1 += 10;
			break;
		case IEEE80211_HT_PARAM_CHA_SEC_BELOW:
			chandef->width = NL80211_CHAN_WIDTH_40;
			chandef->center_freq1 -= 10;
			break;
		}
	} else {
		/* 40 MHz (and 80 MHz) must be supported for VHT */
		ret = IEEE80211_STA_DISABLE_VHT;
		/* also mark 40 MHz disabled */
		ret |= IEEE80211_STA_DISABLE_40MHZ;
		goto out;
	}

	if (!vht_oper || !sband->vht_cap.vht_supported) {
		ret = IEEE80211_STA_DISABLE_VHT;
		goto out;
	}

	vht_chandef.chan = channel;
	vht_chandef.center_freq1 =
		ieee80211_channel_to_frequency(vht_oper->center_freq_seg1_idx,
					       channel->band);
	vht_chandef.center_freq2 = 0;

	switch (vht_oper->chan_width) {
	case IEEE80211_VHT_CHANWIDTH_USE_HT:
		vht_chandef.width = chandef->width;
		vht_chandef.center_freq1 = chandef->center_freq1;
		break;
	case IEEE80211_VHT_CHANWIDTH_80MHZ:
		vht_chandef.width = NL80211_CHAN_WIDTH_80;
		break;
	case IEEE80211_VHT_CHANWIDTH_160MHZ:
		vht_chandef.width = NL80211_CHAN_WIDTH_160;
		break;
	case IEEE80211_VHT_CHANWIDTH_80P80MHZ:
		vht_chandef.width = NL80211_CHAN_WIDTH_80P80;
		vht_chandef.center_freq2 =
			ieee80211_channel_to_frequency(
				vht_oper->center_freq_seg2_idx,
				channel->band);
		break;
	default:
		if (!(ifmgd->flags & IEEE80211_STA_DISABLE_VHT))
			sdata_info(sdata,
				   "AP VHT operation IE has invalid channel width (%d), disable VHT\n",
				   vht_oper->chan_width);
		ret = IEEE80211_STA_DISABLE_VHT;
		goto out;
	}

	if (!cfg80211_chandef_valid(&vht_chandef)) {
		if (!(ifmgd->flags & IEEE80211_STA_DISABLE_VHT))
			sdata_info(sdata,
				   "AP VHT information is invalid, disable VHT\n");
		ret = IEEE80211_STA_DISABLE_VHT;
		goto out;
	}

	if (cfg80211_chandef_identical(chandef, &vht_chandef)) {
		ret = 0;
		goto out;
	}

	if (!cfg80211_chandef_compatible(chandef, &vht_chandef)) {
		if (!(ifmgd->flags & IEEE80211_STA_DISABLE_VHT))
			sdata_info(sdata,
				   "AP VHT information doesn't match HT, disable VHT\n");
		ret = IEEE80211_STA_DISABLE_VHT;
		goto out;
	}

	*chandef = vht_chandef;

	ret = 0;

out:
	/*
	 * When tracking the current AP, don't do any further checks if the
	 * new chandef is identical to the one we're currently using for the
	 * connection. This keeps us from playing ping-pong with regulatory,
	 * without it the following can happen (for example):
	 *  - connect to an AP with 80 MHz, world regdom allows 80 MHz
	 *  - AP advertises regdom US
	 *  - CRDA loads regdom US with 80 MHz prohibited (old database)
	 *  - the code below detects an unsupported channel, downgrades, and
	 *    we disconnect from the AP in the caller
	 *  - disconnect causes CRDA to reload world regdomain and the game
	 *    starts anew.
	 * (see https://bugzilla.kernel.org/show_bug.cgi?id=70881)
	 *
	 * It seems possible that there are still scenarios with CSA or real
	 * bandwidth changes where a this could happen, but those cases are
	 * less common and wouldn't completely prevent using the AP.
	 */
	if (tracking &&
	    cfg80211_chandef_identical(chandef, &sdata->vif.bss_conf.chandef))
		return ret;

	/* don't print the message below for VHT mismatch if VHT is disabled */
	if (ret & IEEE80211_STA_DISABLE_VHT)
		vht_chandef = *chandef;

	/*
	 * Ignore the DISABLED flag when we're already connected and only
	 * tracking the APs beacon for bandwidth changes - otherwise we
	 * might get disconnected here if we connect to an AP, update our
	 * regulatory information based on the AP's country IE and the
	 * information we have is wrong/outdated and disables the channel
	 * that we're actually using for the connection to the AP.
	 */
	while (!cfg80211_chandef_usable(sdata->local->hw.wiphy, chandef,
					tracking ? 0 :
						   IEEE80211_CHAN_DISABLED)) {
		if (WARN_ON(chandef->width == NL80211_CHAN_WIDTH_20_NOHT)) {
			ret = IEEE80211_STA_DISABLE_HT |
			      IEEE80211_STA_DISABLE_VHT;
			break;
		}

		ret |= ieee80211_chandef_downgrade(chandef);
	}

	if (chandef->width != vht_chandef.width && !tracking)
		sdata_info(sdata,
			   "capabilities/regulatory prevented using AP HT/VHT configuration, downgraded\n");

	WARN_ON_ONCE(!cfg80211_chandef_valid(chandef));
	return ret;
}

static int ieee80211_config_bw(struct ieee80211_sub_if_data *sdata,
			       struct sta_info *sta,
			       const struct ieee80211_ht_cap *ht_cap,
			       const struct ieee80211_ht_operation *ht_oper,
			       const struct ieee80211_vht_operation *vht_oper,
			       const u8 *bssid, u32 *changed)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *chan;
	struct cfg80211_chan_def chandef;
	u16 ht_opmode;
	u32 flags;
	enum ieee80211_sta_rx_bandwidth new_sta_bw;
	int ret;

	/* if HT was/is disabled, don't track any bandwidth changes */
	if (ifmgd->flags & IEEE80211_STA_DISABLE_HT || !ht_oper)
		return 0;

	/* don't check VHT if we associated as non-VHT station */
	if (ifmgd->flags & IEEE80211_STA_DISABLE_VHT)
		vht_oper = NULL;

	if (WARN_ON_ONCE(!sta))
		return -EINVAL;

	/*
	 * if bss configuration changed store the new one -
	 * this may be applicable even if channel is identical
	 */
	ht_opmode = le16_to_cpu(ht_oper->operation_mode);
	if (sdata->vif.bss_conf.ht_operation_mode != ht_opmode) {
		*changed |= BSS_CHANGED_HT;
		sdata->vif.bss_conf.ht_operation_mode = ht_opmode;
	}

	chan = sdata->vif.bss_conf.chandef.chan;
	sband = local->hw.wiphy->bands[chan->band];

	/* calculate new channel (type) based on HT/VHT operation IEs */
	flags = ieee80211_determine_chantype(sdata, sband, chan,
					     ht_cap, ht_oper, vht_oper,
					     &chandef, true);

	/*
	 * Downgrade the new channel if we associated with restricted
	 * capabilities. For example, if we associated as a 20 MHz STA
	 * to a 40 MHz AP (due to regulatory, capabilities or config
	 * reasons) then switching to a 40 MHz channel now won't do us
	 * any good -- we couldn't use it with the AP.
	 */
	if (ifmgd->flags & IEEE80211_STA_DISABLE_80P80MHZ &&
	    chandef.width == NL80211_CHAN_WIDTH_80P80)
		flags |= ieee80211_chandef_downgrade(&chandef);
	if (ifmgd->flags & IEEE80211_STA_DISABLE_160MHZ &&
	    chandef.width == NL80211_CHAN_WIDTH_160)
		flags |= ieee80211_chandef_downgrade(&chandef);
	if (ifmgd->flags & IEEE80211_STA_DISABLE_40MHZ &&
	    chandef.width > NL80211_CHAN_WIDTH_20)
		flags |= ieee80211_chandef_downgrade(&chandef);

	if (cfg80211_chandef_identical(&chandef, &sdata->vif.bss_conf.chandef))
		return 0;

	sdata_info(sdata,
		   "AP %pM changed bandwidth, new config is %d MHz, width %d (%d/%d MHz)\n",
		   ifmgd->bssid, chandef.chan->center_freq, chandef.width,
		   chandef.center_freq1, chandef.center_freq2);

	if (flags != (ifmgd->flags & (IEEE80211_STA_DISABLE_HT |
				      IEEE80211_STA_DISABLE_VHT |
				      IEEE80211_STA_DISABLE_40MHZ |
				      IEEE80211_STA_DISABLE_80P80MHZ |
				      IEEE80211_STA_DISABLE_160MHZ)) ||
	    !cfg80211_chandef_valid(&chandef)) {
		sdata_info(sdata,
			   "AP %pM changed bandwidth in a way we can't support - disconnect\n",
			   ifmgd->bssid);
		return -EINVAL;
	}

	switch (chandef.width) {
	case NL80211_CHAN_WIDTH_20_NOHT:
	case NL80211_CHAN_WIDTH_20:
		new_sta_bw = IEEE80211_STA_RX_BW_20;
		break;
	case NL80211_CHAN_WIDTH_40:
		new_sta_bw = IEEE80211_STA_RX_BW_40;
		break;
	case NL80211_CHAN_WIDTH_80:
		new_sta_bw = IEEE80211_STA_RX_BW_80;
		break;
	case NL80211_CHAN_WIDTH_80P80:
	case NL80211_CHAN_WIDTH_160:
		new_sta_bw = IEEE80211_STA_RX_BW_160;
		break;
	default:
		return -EINVAL;
	}

	if (new_sta_bw > sta->cur_max_bandwidth)
		new_sta_bw = sta->cur_max_bandwidth;

	if (new_sta_bw < sta->sta.bandwidth) {
		sta->sta.bandwidth = new_sta_bw;
		rate_control_rate_update(local, sband, sta,
					 IEEE80211_RC_BW_CHANGED);
	}

	ret = ieee80211_vif_change_bandwidth(sdata, &chandef, changed);
	if (ret) {
		sdata_info(sdata,
			   "AP %pM changed bandwidth to incompatible one - disconnect\n",
			   ifmgd->bssid);
		return ret;
	}

	if (new_sta_bw > sta->sta.bandwidth) {
		sta->sta.bandwidth = new_sta_bw;
		rate_control_rate_update(local, sband, sta,
					 IEEE80211_RC_BW_CHANGED);
	}

	return 0;
}

/* frame sending functions */

static void ieee80211_add_ht_ie(struct ieee80211_sub_if_data *sdata,
				struct sk_buff *skb, u8 ap_ht_param,
				struct ieee80211_supported_band *sband,
				struct ieee80211_channel *channel,
				enum ieee80211_smps_mode smps)
{
	u8 *pos;
	u32 flags = channel->flags;
	u16 cap;
	struct ieee80211_sta_ht_cap ht_cap;

	BUILD_BUG_ON(sizeof(ht_cap) != sizeof(sband->ht_cap));

	memcpy(&ht_cap, &sband->ht_cap, sizeof(ht_cap));
	ieee80211_apply_htcap_overrides(sdata, &ht_cap);

	/* determine capability flags */
	cap = ht_cap.cap;

	switch (ap_ht_param & IEEE80211_HT_PARAM_CHA_SEC_OFFSET) {
	case IEEE80211_HT_PARAM_CHA_SEC_ABOVE:
		if (flags & IEEE80211_CHAN_NO_HT40PLUS) {
			cap &= ~IEEE80211_HT_CAP_SUP_WIDTH_20_40;
			cap &= ~IEEE80211_HT_CAP_SGI_40;
		}
		break;
	case IEEE80211_HT_PARAM_CHA_SEC_BELOW:
		if (flags & IEEE80211_CHAN_NO_HT40MINUS) {
			cap &= ~IEEE80211_HT_CAP_SUP_WIDTH_20_40;
			cap &= ~IEEE80211_HT_CAP_SGI_40;
		}
		break;
	}

	/*
	 * If 40 MHz was disabled associate as though we weren't
	 * capable of 40 MHz -- some broken APs will never fall
	 * back to trying to transmit in 20 MHz.
	 */
	if (sdata->u.mgd.flags & IEEE80211_STA_DISABLE_40MHZ) {
		cap &= ~IEEE80211_HT_CAP_SUP_WIDTH_20_40;
		cap &= ~IEEE80211_HT_CAP_SGI_40;
	}

	/* set SM PS mode properly */
	cap &= ~IEEE80211_HT_CAP_SM_PS;
	switch (smps) {
	case IEEE80211_SMPS_AUTOMATIC:
	case IEEE80211_SMPS_NUM_MODES:
		WARN_ON(1);
	case IEEE80211_SMPS_OFF:
		cap |= WLAN_HT_CAP_SM_PS_DISABLED <<
			IEEE80211_HT_CAP_SM_PS_SHIFT;
		break;
	case IEEE80211_SMPS_STATIC:
		cap |= WLAN_HT_CAP_SM_PS_STATIC <<
			IEEE80211_HT_CAP_SM_PS_SHIFT;
		break;
	case IEEE80211_SMPS_DYNAMIC:
		cap |= WLAN_HT_CAP_SM_PS_DYNAMIC <<
			IEEE80211_HT_CAP_SM_PS_SHIFT;
		break;
	}

	/* reserve and fill IE */
	pos = skb_put(skb, sizeof(struct ieee80211_ht_cap) + 2);
	ieee80211_ie_build_ht_cap(pos, &ht_cap, cap);
}

/* This function determines vht capability flags for the association
 * and builds the IE.
 * Note - the function may set the owner of the MU-MIMO capability
 */
static void ieee80211_add_vht_ie(struct ieee80211_sub_if_data *sdata,
				 struct sk_buff *skb,
				 struct ieee80211_supported_band *sband,
				 struct ieee80211_vht_cap *ap_vht_cap)
{
	struct ieee80211_local *local = sdata->local;
	u8 *pos;
	u32 cap;
	struct ieee80211_sta_vht_cap vht_cap;
	u32 mask, ap_bf_sts, our_bf_sts;

	BUILD_BUG_ON(sizeof(vht_cap) != sizeof(sband->vht_cap));

	memcpy(&vht_cap, &sband->vht_cap, sizeof(vht_cap));
	ieee80211_apply_vhtcap_overrides(sdata, &vht_cap);

	/* determine capability flags */
	cap = vht_cap.cap;

	if (sdata->u.mgd.flags & IEEE80211_STA_DISABLE_80P80MHZ) {
		u32 bw = cap & IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_MASK;

		cap &= ~IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_MASK;
		if (bw == IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ ||
		    bw == IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80MHZ)
			cap |= IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ;
	}

	if (sdata->u.mgd.flags & IEEE80211_STA_DISABLE_160MHZ) {
		cap &= ~IEEE80211_VHT_CAP_SHORT_GI_160;
		cap &= ~IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_MASK;
	}

	/*
	 * Some APs apparently get confused if our capabilities are better
	 * than theirs, so restrict what we advertise in the assoc request.
	 */
	if (!(ap_vht_cap->vht_cap_info &
			cpu_to_le32(IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE)))
		cap &= ~(IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE |
			 IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE);
	else if (!(ap_vht_cap->vht_cap_info &
			cpu_to_le32(IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE)))
		cap &= ~IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE;

	/*
	 * If some other vif is using the MU-MIMO capablity we cannot associate
	 * using MU-MIMO - this will lead to contradictions in the group-id
	 * mechanism.
	 * Ownership is defined since association request, in order to avoid
	 * simultaneous associations with MU-MIMO.
	 */
	if (cap & IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE) {
		bool disable_mu_mimo = false;
		struct ieee80211_sub_if_data *other;

		list_for_each_entry_rcu(other, &local->interfaces, list) {
			if (other->flags & IEEE80211_SDATA_MU_MIMO_OWNER) {
				disable_mu_mimo = true;
				break;
			}
		}
		if (disable_mu_mimo)
			cap &= ~IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE;
		else
			sdata->flags |= IEEE80211_SDATA_MU_MIMO_OWNER;
	}

	mask = IEEE80211_VHT_CAP_BEAMFORMEE_STS_MASK;

	ap_bf_sts = le32_to_cpu(ap_vht_cap->vht_cap_info) & mask;
	our_bf_sts = cap & mask;

	if (ap_bf_sts < our_bf_sts) {
		cap &= ~mask;
		cap |= ap_bf_sts;
	}

	/* reserve and fill IE */
	pos = skb_put(skb, sizeof(struct ieee80211_vht_cap) + 2);
	ieee80211_ie_build_vht_cap(pos, &vht_cap, cap);
}

static void ieee80211_send_assoc(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_mgd_assoc_data *assoc_data = ifmgd->assoc_data;
	struct sk_buff *skb;
	struct ieee80211_mgmt *mgmt;
	u8 *pos, qos_info;
	size_t offset = 0, noffset;
	int i, count, rates_len, supp_rates_len, shift;
	u16 capab;
	struct ieee80211_supported_band *sband;
	struct ieee80211_chanctx_conf *chanctx_conf;
	struct ieee80211_channel *chan;
	u32 rate_flags, rates = 0;

	sdata_assert_lock(sdata);

	rcu_read_lock();
	chanctx_conf = rcu_dereference(sdata->vif.chanctx_conf);
	if (WARN_ON(!chanctx_conf)) {
		rcu_read_unlock();
		return;
	}
	chan = chanctx_conf->def.chan;
	rate_flags = ieee80211_chandef_rate_flags(&chanctx_conf->def);
	rcu_read_unlock();
	sband = local->hw.wiphy->bands[chan->band];
	shift = ieee80211_vif_get_shift(&sdata->vif);

	if (assoc_data->supp_rates_len) {
		/*
		 * Get all rates supported by the device and the AP as
		 * some APs don't like getting a superset of their rates
		 * in the association request (e.g. D-Link DAP 1353 in
		 * b-only mode)...
		 */
		rates_len = ieee80211_parse_bitrates(&chanctx_conf->def, sband,
						     assoc_data->supp_rates,
						     assoc_data->supp_rates_len,
						     &rates);
	} else {
		/*
		 * In case AP not provide any supported rates information
		 * before association, we send information element(s) with
		 * all rates that we support.
		 */
		rates_len = 0;
		for (i = 0; i < sband->n_bitrates; i++) {
			if ((rate_flags & sband->bitrates[i].flags)
			    != rate_flags)
				continue;
			rates |= BIT(i);
			rates_len++;
		}
	}

	skb = alloc_skb(local->hw.extra_tx_headroom +
			sizeof(*mgmt) + /* bit too much but doesn't matter */
			2 + assoc_data->ssid_len + /* SSID */
			4 + rates_len + /* (extended) rates */
			4 + /* power capability */
			2 + 2 * sband->n_channels + /* supported channels */
			2 + sizeof(struct ieee80211_ht_cap) + /* HT */
			2 + sizeof(struct ieee80211_vht_cap) + /* VHT */
			assoc_data->ie_len + /* extra IEs */
			9, /* WMM */
			GFP_KERNEL);
	if (!skb)
		return;

	skb_reserve(skb, local->hw.extra_tx_headroom);

	capab = WLAN_CAPABILITY_ESS;

	if (sband->band == IEEE80211_BAND_2GHZ) {
		capab |= WLAN_CAPABILITY_SHORT_SLOT_TIME;
		capab |= WLAN_CAPABILITY_SHORT_PREAMBLE;
	}

	if (assoc_data->capability & WLAN_CAPABILITY_PRIVACY)
		capab |= WLAN_CAPABILITY_PRIVACY;

	if ((assoc_data->capability & WLAN_CAPABILITY_SPECTRUM_MGMT) &&
	    ieee80211_hw_check(&local->hw, SPECTRUM_MGMT))
		capab |= WLAN_CAPABILITY_SPECTRUM_MGMT;

	if (ifmgd->flags & IEEE80211_STA_ENABLE_RRM)
		capab |= WLAN_CAPABILITY_RADIO_MEASURE;

	mgmt = (struct ieee80211_mgmt *) skb_put(skb, 24);
	memset(mgmt, 0, 24);
	memcpy(mgmt->da, assoc_data->bss->bssid, ETH_ALEN);
	memcpy(mgmt->sa, sdata->vif.addr, ETH_ALEN);
	memcpy(mgmt->bssid, assoc_data->bss->bssid, ETH_ALEN);

	if (!is_zero_ether_addr(assoc_data->prev_bssid)) {
		skb_put(skb, 10);
		mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
						  IEEE80211_STYPE_REASSOC_REQ);
		mgmt->u.reassoc_req.capab_info = cpu_to_le16(capab);
		mgmt->u.reassoc_req.listen_interval =
				cpu_to_le16(local->hw.conf.listen_interval);
		memcpy(mgmt->u.reassoc_req.current_ap, assoc_data->prev_bssid,
		       ETH_ALEN);
	} else {
		skb_put(skb, 4);
		mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
						  IEEE80211_STYPE_ASSOC_REQ);
		mgmt->u.assoc_req.capab_info = cpu_to_le16(capab);
		mgmt->u.assoc_req.listen_interval =
				cpu_to_le16(local->hw.conf.listen_interval);
	}

	/* SSID */
	ies = pos = skb_put(skb, 2 + ifsta->ssid_len);
	*pos++ = WLAN_EID_SSID;
	*pos++ = ifsta->ssid_len;
	memcpy(pos, ifsta->ssid, ifsta->ssid_len);
	pos = skb_put(skb, 2 + assoc_data->ssid_len);
	*pos++ = WLAN_EID_SSID;
	*pos++ = assoc_data->ssid_len;
	memcpy(pos, assoc_data->ssid, assoc_data->ssid_len);

	/* add all rates which were marked to be used above */
	supp_rates_len = rates_len;
	if (supp_rates_len > 8)
		supp_rates_len = 8;

	len = sband->n_bitrates;
	pos = skb_put(skb, supp_rates_len + 2);
	*pos++ = WLAN_EID_SUPP_RATES;
	*pos++ = supp_rates_len;

	count = 0;
	for (i = 0; i < sband->n_bitrates; i++) {
		if (BIT(i) & rates) {
			int rate = sband->bitrates[i].bitrate;
			*pos++ = (u8) (rate / 5);
			int rate = DIV_ROUND_UP(sband->bitrates[i].bitrate,
						5 * (1 << shift));
			*pos++ = (u8) rate;
			if (++count == 8)
				break;
		}
	}

	if (rates_len > count) {
		pos = skb_put(skb, rates_len - count + 2);
		*pos++ = WLAN_EID_EXT_SUPP_RATES;
		*pos++ = rates_len - count;

		for (i++; i < sband->n_bitrates; i++) {
			if (BIT(i) & rates) {
				int rate = sband->bitrates[i].bitrate;
				*pos++ = (u8) (rate / 5);
				int rate;
				rate = DIV_ROUND_UP(sband->bitrates[i].bitrate,
						    5 * (1 << shift));
				*pos++ = (u8) rate;
			}
		}
	}

	if (capab & WLAN_CAPABILITY_SPECTRUM_MGMT) {
		/* 1. power capabilities */
	if (capab & WLAN_CAPABILITY_SPECTRUM_MGMT ||
	    capab & WLAN_CAPABILITY_RADIO_MEASURE) {
		pos = skb_put(skb, 4);
		*pos++ = WLAN_EID_PWR_CAPABILITY;
		*pos++ = 2;
		*pos++ = 0; /* min tx power */
		*pos++ = local->hw.conf.channel->max_power; /* max tx power */

		/* 2. supported channels */
		 /* max tx power */
		*pos++ = ieee80211_chandef_max_power(&chanctx_conf->def);
	}

	if (capab & WLAN_CAPABILITY_SPECTRUM_MGMT) {
		/* TODO: get this in reg domain format */
		pos = skb_put(skb, 2 * sband->n_channels + 2);
		*pos++ = WLAN_EID_SUPPORTED_CHANNELS;
		*pos++ = 2 * sband->n_channels;
		for (i = 0; i < sband->n_channels; i++) {
			*pos++ = ieee80211_frequency_to_channel(
					sband->channels[i].center_freq);
			*pos++ = 1; /* one channel in the subband*/
		}
	}

	if (ifsta->extra_ie) {
		pos = skb_put(skb, ifsta->extra_ie_len);
		memcpy(pos, ifsta->extra_ie, ifsta->extra_ie_len);
	}

	if (wmm && (ifsta->flags & IEEE80211_STA_WMM_ENABLED)) {
		pos = skb_put(skb, 9);
		*pos++ = WLAN_EID_VENDOR_SPECIFIC;
		*pos++ = 7; /* len */
		*pos++ = 0x00; /* Microsoft OUI 00:50:F2 */
		*pos++ = 0x50;
		*pos++ = 0xf2;
		*pos++ = 2; /* WME */
		*pos++ = 0; /* WME info */
		*pos++ = 1; /* WME ver */
		*pos++ = 0;
	}

	/* wmm support is a must to HT */
	if (wmm && (ifsta->flags & IEEE80211_STA_WMM_ENABLED) &&
	    sband->ht_info.ht_supported && bss->ht_add_ie) {
		struct ieee80211_ht_addt_info *ht_add_info =
			(struct ieee80211_ht_addt_info *)bss->ht_add_ie;
		u16 cap = sband->ht_info.cap;
		__le16 tmp;
		u32 flags = local->hw.conf.channel->flags;

		switch (ht_add_info->ht_param & IEEE80211_HT_IE_CHA_SEC_OFFSET) {
		case IEEE80211_HT_IE_CHA_SEC_ABOVE:
			if (flags & IEEE80211_CHAN_NO_FAT_ABOVE) {
				cap &= ~IEEE80211_HT_CAP_SUP_WIDTH;
				cap &= ~IEEE80211_HT_CAP_SGI_40;
			}
			break;
		case IEEE80211_HT_IE_CHA_SEC_BELOW:
			if (flags & IEEE80211_CHAN_NO_FAT_BELOW) {
				cap &= ~IEEE80211_HT_CAP_SUP_WIDTH;
				cap &= ~IEEE80211_HT_CAP_SGI_40;
			}
			break;
		}

		tmp = cpu_to_le16(cap);
		pos = skb_put(skb, sizeof(struct ieee80211_ht_cap)+2);
		*pos++ = WLAN_EID_HT_CAPABILITY;
		*pos++ = sizeof(struct ieee80211_ht_cap);
		memset(pos, 0, sizeof(struct ieee80211_ht_cap));
		memcpy(pos, &tmp, sizeof(u16));
		pos += sizeof(u16);
		/* TODO: needs a define here for << 2 */
		*pos++ = sband->ht_info.ampdu_factor |
			 (sband->ht_info.ampdu_density << 2);
		memcpy(pos, sband->ht_info.supp_mcs_set, 16);
	}

	kfree(ifsta->assocreq_ies);
	ifsta->assocreq_ies_len = (skb->data + skb->len) - ies;
	ifsta->assocreq_ies = kmalloc(ifsta->assocreq_ies_len, GFP_KERNEL);
	if (ifsta->assocreq_ies)
		memcpy(ifsta->assocreq_ies, ies, ifsta->assocreq_ies_len);

	ieee80211_sta_tx(dev, skb, 0);
}


static void ieee80211_send_deauth(struct net_device *dev,
				  struct ieee80211_if_sta *ifsta, u16 reason)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct sk_buff *skb;
	struct ieee80211_mgmt *mgmt;

	skb = dev_alloc_skb(local->hw.extra_tx_headroom + sizeof(*mgmt));
	if (!skb) {
		printk(KERN_DEBUG "%s: failed to allocate buffer for deauth "
		       "frame\n", dev->name);
		return;
	}
	skb_reserve(skb, local->hw.extra_tx_headroom);

	mgmt = (struct ieee80211_mgmt *) skb_put(skb, 24);
	memset(mgmt, 0, 24);
	memcpy(mgmt->da, ifsta->bssid, ETH_ALEN);
	memcpy(mgmt->sa, dev->dev_addr, ETH_ALEN);
	memcpy(mgmt->bssid, ifsta->bssid, ETH_ALEN);
	mgmt->frame_control = IEEE80211_FC(IEEE80211_FTYPE_MGMT,
					   IEEE80211_STYPE_DEAUTH);
	skb_put(skb, 2);
	mgmt->u.deauth.reason_code = cpu_to_le16(reason);

	ieee80211_sta_tx(dev, skb, 0);
}


static void ieee80211_send_disassoc(struct net_device *dev,
				    struct ieee80211_if_sta *ifsta, u16 reason)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct sk_buff *skb;
	struct ieee80211_mgmt *mgmt;

	skb = dev_alloc_skb(local->hw.extra_tx_headroom + sizeof(*mgmt));
	if (!skb) {
		printk(KERN_DEBUG "%s: failed to allocate buffer for disassoc "
		       "frame\n", dev->name);
		return;
	}
	skb_reserve(skb, local->hw.extra_tx_headroom);

	mgmt = (struct ieee80211_mgmt *) skb_put(skb, 24);
	memset(mgmt, 0, 24);
	memcpy(mgmt->da, ifsta->bssid, ETH_ALEN);
	memcpy(mgmt->sa, dev->dev_addr, ETH_ALEN);
	memcpy(mgmt->bssid, ifsta->bssid, ETH_ALEN);
	mgmt->frame_control = IEEE80211_FC(IEEE80211_FTYPE_MGMT,
					   IEEE80211_STYPE_DISASSOC);
	skb_put(skb, 2);
	mgmt->u.disassoc.reason_code = cpu_to_le16(reason);

	ieee80211_sta_tx(dev, skb, 0);
}


static int ieee80211_privacy_mismatch(struct net_device *dev,
				      struct ieee80211_if_sta *ifsta)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sta_bss *bss;
	int bss_privacy;
	int wep_privacy;
	int privacy_invoked;

	if (!ifsta || (ifsta->flags & IEEE80211_STA_MIXED_CELL))
		return 0;

	bss = ieee80211_rx_bss_get(dev, ifsta->bssid,
				   local->hw.conf.channel->center_freq,
				   ifsta->ssid, ifsta->ssid_len);
	if (!bss)
		return 0;

	bss_privacy = !!(bss->capability & WLAN_CAPABILITY_PRIVACY);
	wep_privacy = !!ieee80211_sta_wep_configured(dev);
	privacy_invoked = !!(ifsta->flags & IEEE80211_STA_PRIVACY_INVOKED);

	ieee80211_rx_bss_put(local, bss);

	if ((bss_privacy == wep_privacy) || (bss_privacy == privacy_invoked))
		return 0;

	return 1;
}


static void ieee80211_associate(struct net_device *dev,
				struct ieee80211_if_sta *ifsta)
{
	DECLARE_MAC_BUF(mac);

	ifsta->assoc_tries++;
	if (ifsta->assoc_tries > IEEE80211_ASSOC_MAX_TRIES) {
		printk(KERN_DEBUG "%s: association with AP %s"
		       " timed out\n",
		       dev->name, print_mac(mac, ifsta->bssid));
		ifsta->state = IEEE80211_DISABLED;
		return;
	}

	ifsta->state = IEEE80211_ASSOCIATE;
	printk(KERN_DEBUG "%s: associate with AP %s\n",
	       dev->name, print_mac(mac, ifsta->bssid));
	if (ieee80211_privacy_mismatch(dev, ifsta)) {
		printk(KERN_DEBUG "%s: mismatch in privacy configuration and "
		       "mixed-cell disabled - abort association\n", dev->name);
		ifsta->state = IEEE80211_DISABLED;
		return;
	}

	ieee80211_send_assoc(dev, ifsta);

	mod_timer(&ifsta->timer, jiffies + IEEE80211_ASSOC_TIMEOUT);
}


static void ieee80211_associated(struct net_device *dev,
				 struct ieee80211_if_sta *ifsta)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct sta_info *sta;
	int disassoc;
	DECLARE_MAC_BUF(mac);

	/* TODO: start monitoring current AP signal quality and number of
	 * missed beacons. Scan other channels every now and then and search
	 * for better APs. */
	/* TODO: remove expired BSSes */

	ifsta->state = IEEE80211_ASSOCIATED;

	rcu_read_lock();

	sta = sta_info_get(local, ifsta->bssid);
	if (!sta) {
		printk(KERN_DEBUG "%s: No STA entry for own AP %s\n",
		       dev->name, print_mac(mac, ifsta->bssid));
		disassoc = 1;
	} else {
		disassoc = 0;
		if (time_after(jiffies,
			       sta->last_rx + IEEE80211_MONITORING_INTERVAL)) {
			if (ifsta->flags & IEEE80211_STA_PROBEREQ_POLL) {
				printk(KERN_DEBUG "%s: No ProbeResp from "
				       "current AP %s - assume out of "
				       "range\n",
				       dev->name, print_mac(mac, ifsta->bssid));
				disassoc = 1;
				sta_info_unlink(&sta);
			} else
				ieee80211_send_probe_req(dev, ifsta->bssid,
							 local->scan_ssid,
							 local->scan_ssid_len);
			ifsta->flags ^= IEEE80211_STA_PROBEREQ_POLL;
		} else {
			ifsta->flags &= ~IEEE80211_STA_PROBEREQ_POLL;
			if (time_after(jiffies, ifsta->last_probe +
				       IEEE80211_PROBE_INTERVAL)) {
				ifsta->last_probe = jiffies;
				ieee80211_send_probe_req(dev, ifsta->bssid,
							 ifsta->ssid,
							 ifsta->ssid_len);
			}
		}
	}

	rcu_read_unlock();

	if (disassoc && sta)
		sta_info_destroy(sta);

	if (disassoc) {
		ifsta->state = IEEE80211_DISABLED;
		ieee80211_set_associated(dev, ifsta, 0);
	} else {
		mod_timer(&ifsta->timer, jiffies +
				      IEEE80211_MONITORING_INTERVAL);
	}
}


static void ieee80211_send_probe_req(struct net_device *dev, u8 *dst,
				     u8 *ssid, size_t ssid_len)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_supported_band *sband;
	struct sk_buff *skb;
	struct ieee80211_mgmt *mgmt;
	u8 *pos, *supp_rates, *esupp_rates = NULL;
	int i;

	skb = dev_alloc_skb(local->hw.extra_tx_headroom + sizeof(*mgmt) + 200);
	if (!skb) {
		printk(KERN_DEBUG "%s: failed to allocate buffer for probe "
		       "request\n", dev->name);
		return;
	}
	skb_reserve(skb, local->hw.extra_tx_headroom);

	mgmt = (struct ieee80211_mgmt *) skb_put(skb, 24);
	memset(mgmt, 0, 24);
	mgmt->frame_control = IEEE80211_FC(IEEE80211_FTYPE_MGMT,
					   IEEE80211_STYPE_PROBE_REQ);
	memcpy(mgmt->sa, dev->dev_addr, ETH_ALEN);
	if (dst) {
		memcpy(mgmt->da, dst, ETH_ALEN);
		memcpy(mgmt->bssid, dst, ETH_ALEN);
	} else {
		memset(mgmt->da, 0xff, ETH_ALEN);
		memset(mgmt->bssid, 0xff, ETH_ALEN);
	}
	pos = skb_put(skb, 2 + ssid_len);
	*pos++ = WLAN_EID_SSID;
	*pos++ = ssid_len;
	memcpy(pos, ssid, ssid_len);

	supp_rates = skb_put(skb, 2);
	supp_rates[0] = WLAN_EID_SUPP_RATES;
	supp_rates[1] = 0;
	sband = local->hw.wiphy->bands[local->hw.conf.channel->band];

	for (i = 0; i < sband->n_bitrates; i++) {
		struct ieee80211_rate *rate = &sband->bitrates[i];
		if (esupp_rates) {
			pos = skb_put(skb, 1);
			esupp_rates[1]++;
		} else if (supp_rates[1] == 8) {
			esupp_rates = skb_put(skb, 3);
			esupp_rates[0] = WLAN_EID_EXT_SUPP_RATES;
			esupp_rates[1] = 1;
			pos = &esupp_rates[2];
		} else {
			pos = skb_put(skb, 1);
			supp_rates[1]++;
		}
		*pos = rate->bitrate / 5;
	}

	ieee80211_sta_tx(dev, skb, 0);
}


static int ieee80211_sta_wep_configured(struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	if (!sdata || !sdata->default_key ||
	    sdata->default_key->conf.alg != ALG_WEP)
		return 0;
	return 1;
}


static void ieee80211_auth_completed(struct net_device *dev,
				     struct ieee80211_if_sta *ifsta)
{
	printk(KERN_DEBUG "%s: authenticated\n", dev->name);
	ifsta->flags |= IEEE80211_STA_AUTHENTICATED;
	ieee80211_associate(dev, ifsta);
}


static void ieee80211_auth_challenge(struct net_device *dev,
				     struct ieee80211_if_sta *ifsta,
				     struct ieee80211_mgmt *mgmt,
				     size_t len)
{
	u8 *pos;
	struct ieee802_11_elems elems;

	pos = mgmt->u.auth.variable;
	ieee802_11_parse_elems(pos, len - (pos - (u8 *) mgmt), &elems);
	if (!elems.challenge)
		return;
	ieee80211_send_auth(dev, ifsta, 3, elems.challenge - 2,
			    elems.challenge_len + 2, 1);
}

static void ieee80211_send_addba_resp(struct net_device *dev, u8 *da, u16 tid,
					u8 dialog_token, u16 status, u16 policy,
					u16 buf_size, u16 timeout)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_if_sta *ifsta = &sdata->u.sta;
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct sk_buff *skb;
	struct ieee80211_mgmt *mgmt;
	u16 capab;

	skb = dev_alloc_skb(sizeof(*mgmt) + local->hw.extra_tx_headroom);

	if (!skb) {
		printk(KERN_DEBUG "%s: failed to allocate buffer "
		       "for addba resp frame\n", dev->name);
		return;
	}

	skb_reserve(skb, local->hw.extra_tx_headroom);
	mgmt = (struct ieee80211_mgmt *) skb_put(skb, 24);
	memset(mgmt, 0, 24);
	memcpy(mgmt->da, da, ETH_ALEN);
	memcpy(mgmt->sa, dev->dev_addr, ETH_ALEN);
	if (sdata->vif.type == IEEE80211_IF_TYPE_AP)
		memcpy(mgmt->bssid, dev->dev_addr, ETH_ALEN);
	else
		memcpy(mgmt->bssid, ifsta->bssid, ETH_ALEN);
	mgmt->frame_control = IEEE80211_FC(IEEE80211_FTYPE_MGMT,
					   IEEE80211_STYPE_ACTION);

	skb_put(skb, 1 + sizeof(mgmt->u.action.u.addba_resp));
	mgmt->u.action.category = WLAN_CATEGORY_BACK;
	mgmt->u.action.u.addba_resp.action_code = WLAN_ACTION_ADDBA_RESP;
	mgmt->u.action.u.addba_resp.dialog_token = dialog_token;

	capab = (u16)(policy << 1);	/* bit 1 aggregation policy */
	capab |= (u16)(tid << 2); 	/* bit 5:2 TID number */
	capab |= (u16)(buf_size << 6);	/* bit 15:6 max size of aggregation */

	mgmt->u.action.u.addba_resp.capab = cpu_to_le16(capab);
	mgmt->u.action.u.addba_resp.timeout = cpu_to_le16(timeout);
	mgmt->u.action.u.addba_resp.status = cpu_to_le16(status);

	ieee80211_sta_tx(dev, skb, 0);

	return;
}

void ieee80211_send_addba_request(struct net_device *dev, const u8 *da,
				u16 tid, u8 dialog_token, u16 start_seq_num,
				u16 agg_size, u16 timeout)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_if_sta *ifsta = &sdata->u.sta;
	struct sk_buff *skb;
	struct ieee80211_mgmt *mgmt;
	u16 capab;

	skb = dev_alloc_skb(sizeof(*mgmt) + local->hw.extra_tx_headroom);

	if (!skb) {
		printk(KERN_ERR "%s: failed to allocate buffer "
				"for addba request frame\n", dev->name);
		return;
	}
	skb_reserve(skb, local->hw.extra_tx_headroom);
	mgmt = (struct ieee80211_mgmt *) skb_put(skb, 24);
	memset(mgmt, 0, 24);
	memcpy(mgmt->da, da, ETH_ALEN);
	memcpy(mgmt->sa, dev->dev_addr, ETH_ALEN);
	if (sdata->vif.type == IEEE80211_IF_TYPE_AP)
		memcpy(mgmt->bssid, dev->dev_addr, ETH_ALEN);
	else
		memcpy(mgmt->bssid, ifsta->bssid, ETH_ALEN);

	mgmt->frame_control = IEEE80211_FC(IEEE80211_FTYPE_MGMT,
					IEEE80211_STYPE_ACTION);

	skb_put(skb, 1 + sizeof(mgmt->u.action.u.addba_req));

	mgmt->u.action.category = WLAN_CATEGORY_BACK;
	mgmt->u.action.u.addba_req.action_code = WLAN_ACTION_ADDBA_REQ;

	mgmt->u.action.u.addba_req.dialog_token = dialog_token;
	capab = (u16)(1 << 1);		/* bit 1 aggregation policy */
	capab |= (u16)(tid << 2); 	/* bit 5:2 TID number */
	capab |= (u16)(agg_size << 6);	/* bit 15:6 max size of aggergation */

	mgmt->u.action.u.addba_req.capab = cpu_to_le16(capab);

	mgmt->u.action.u.addba_req.timeout = cpu_to_le16(timeout);
	mgmt->u.action.u.addba_req.start_seq_num =
					cpu_to_le16(start_seq_num << 4);

	ieee80211_sta_tx(dev, skb, 0);
}

static void ieee80211_sta_process_addba_request(struct net_device *dev,
						struct ieee80211_mgmt *mgmt,
						size_t len)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_hw *hw = &local->hw;
	struct ieee80211_conf *conf = &hw->conf;
	struct sta_info *sta;
	struct tid_ampdu_rx *tid_agg_rx;
	u16 capab, tid, timeout, ba_policy, buf_size, start_seq_num, status;
	u8 dialog_token;
	int ret = -EOPNOTSUPP;
	DECLARE_MAC_BUF(mac);

	rcu_read_lock();

	sta = sta_info_get(local, mgmt->sa);
	if (!sta) {
		rcu_read_unlock();
		return;
	}

	/* extract session parameters from addba request frame */
	dialog_token = mgmt->u.action.u.addba_req.dialog_token;
	timeout = le16_to_cpu(mgmt->u.action.u.addba_req.timeout);
	start_seq_num =
		le16_to_cpu(mgmt->u.action.u.addba_req.start_seq_num) >> 4;

	capab = le16_to_cpu(mgmt->u.action.u.addba_req.capab);
	ba_policy = (capab & IEEE80211_ADDBA_PARAM_POLICY_MASK) >> 1;
	tid = (capab & IEEE80211_ADDBA_PARAM_TID_MASK) >> 2;
	buf_size = (capab & IEEE80211_ADDBA_PARAM_BUF_SIZE_MASK) >> 6;

	status = WLAN_STATUS_REQUEST_DECLINED;

	/* sanity check for incoming parameters:
	 * check if configuration can support the BA policy
	 * and if buffer size does not exceeds max value */
	if (((ba_policy != 1)
		&& (!(conf->ht_conf.cap & IEEE80211_HT_CAP_DELAY_BA)))
		|| (buf_size > IEEE80211_MAX_AMPDU_BUF)) {
		status = WLAN_STATUS_INVALID_QOS_PARAM;
#ifdef CONFIG_MAC80211_HT_DEBUG
		if (net_ratelimit())
			printk(KERN_DEBUG "AddBA Req with bad params from "
				"%s on tid %u. policy %d, buffer size %d\n",
				print_mac(mac, mgmt->sa), tid, ba_policy,
				buf_size);
#endif /* CONFIG_MAC80211_HT_DEBUG */
		goto end_no_lock;
	}
	/* determine default buffer size */
	if (buf_size == 0) {
		struct ieee80211_supported_band *sband;

		sband = local->hw.wiphy->bands[conf->channel->band];
		buf_size = IEEE80211_MIN_AMPDU_BUF;
		buf_size = buf_size << sband->ht_info.ampdu_factor;
	}


	/* examine state machine */
	spin_lock_bh(&sta->lock);

	if (sta->ampdu_mlme.tid_state_rx[tid] != HT_AGG_STATE_IDLE) {
#ifdef CONFIG_MAC80211_HT_DEBUG
		if (net_ratelimit())
			printk(KERN_DEBUG "unexpected AddBA Req from "
				"%s on tid %u\n",
				print_mac(mac, mgmt->sa), tid);
#endif /* CONFIG_MAC80211_HT_DEBUG */
		goto end;
	}

	/* prepare A-MPDU MLME for Rx aggregation */
	sta->ampdu_mlme.tid_rx[tid] =
			kmalloc(sizeof(struct tid_ampdu_rx), GFP_ATOMIC);
	if (!sta->ampdu_mlme.tid_rx[tid]) {
#ifdef CONFIG_MAC80211_HT_DEBUG
		if (net_ratelimit())
			printk(KERN_ERR "allocate rx mlme to tid %d failed\n",
					tid);
#endif
		goto end;
	}
	/* rx timer */
	sta->ampdu_mlme.tid_rx[tid]->session_timer.function =
				sta_rx_agg_session_timer_expired;
	sta->ampdu_mlme.tid_rx[tid]->session_timer.data =
				(unsigned long)&sta->timer_to_tid[tid];
	init_timer(&sta->ampdu_mlme.tid_rx[tid]->session_timer);

	tid_agg_rx = sta->ampdu_mlme.tid_rx[tid];

	/* prepare reordering buffer */
	tid_agg_rx->reorder_buf =
		kmalloc(buf_size * sizeof(struct sk_buff *), GFP_ATOMIC);
	if (!tid_agg_rx->reorder_buf) {
#ifdef CONFIG_MAC80211_HT_DEBUG
		if (net_ratelimit())
			printk(KERN_ERR "can not allocate reordering buffer "
			       "to tid %d\n", tid);
#endif
		kfree(sta->ampdu_mlme.tid_rx[tid]);
		goto end;
	}
	memset(tid_agg_rx->reorder_buf, 0,
		buf_size * sizeof(struct sk_buff *));

	if (local->ops->ampdu_action)
		ret = local->ops->ampdu_action(hw, IEEE80211_AMPDU_RX_START,
					       sta->addr, tid, &start_seq_num);
#ifdef CONFIG_MAC80211_HT_DEBUG
	printk(KERN_DEBUG "Rx A-MPDU request on tid %d result %d\n", tid, ret);
#endif /* CONFIG_MAC80211_HT_DEBUG */

	if (ret) {
		kfree(tid_agg_rx->reorder_buf);
		kfree(tid_agg_rx);
		sta->ampdu_mlme.tid_rx[tid] = NULL;
		goto end;
	}

	/* change state and send addba resp */
	sta->ampdu_mlme.tid_state_rx[tid] = HT_AGG_STATE_OPERATIONAL;
	tid_agg_rx->dialog_token = dialog_token;
	tid_agg_rx->ssn = start_seq_num;
	tid_agg_rx->head_seq_num = start_seq_num;
	tid_agg_rx->buf_size = buf_size;
	tid_agg_rx->timeout = timeout;
	tid_agg_rx->stored_mpdu_num = 0;
	status = WLAN_STATUS_SUCCESS;
end:
	spin_unlock_bh(&sta->lock);

end_no_lock:
	ieee80211_send_addba_resp(sta->sdata->dev, sta->addr, tid,
				  dialog_token, status, 1, buf_size, timeout);
	rcu_read_unlock();
}

static void ieee80211_sta_process_addba_resp(struct net_device *dev,
					     struct ieee80211_mgmt *mgmt,
					     size_t len)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_hw *hw = &local->hw;
	struct sta_info *sta;
	u16 capab;
	u16 tid;
	u8 *state;

	rcu_read_lock();

	sta = sta_info_get(local, mgmt->sa);
	if (!sta) {
		rcu_read_unlock();
		return;
	}

	capab = le16_to_cpu(mgmt->u.action.u.addba_resp.capab);
	tid = (capab & IEEE80211_ADDBA_PARAM_TID_MASK) >> 2;

	state = &sta->ampdu_mlme.tid_state_tx[tid];

	spin_lock_bh(&sta->lock);

	if (!(*state & HT_ADDBA_REQUESTED_MSK)) {
		spin_unlock_bh(&sta->lock);
		goto addba_resp_exit;
	}

	if (mgmt->u.action.u.addba_resp.dialog_token !=
		sta->ampdu_mlme.tid_tx[tid]->dialog_token) {
		spin_unlock_bh(&sta->lock);
#ifdef CONFIG_MAC80211_HT_DEBUG
		printk(KERN_DEBUG "wrong addBA response token, tid %d\n", tid);
#endif /* CONFIG_MAC80211_HT_DEBUG */
		goto addba_resp_exit;
	}

	del_timer_sync(&sta->ampdu_mlme.tid_tx[tid]->addba_resp_timer);
#ifdef CONFIG_MAC80211_HT_DEBUG
	printk(KERN_DEBUG "switched off addBA timer for tid %d \n", tid);
#endif /* CONFIG_MAC80211_HT_DEBUG */
	if (le16_to_cpu(mgmt->u.action.u.addba_resp.status)
			== WLAN_STATUS_SUCCESS) {
		*state |= HT_ADDBA_RECEIVED_MSK;
		sta->ampdu_mlme.addba_req_num[tid] = 0;

		if (*state == HT_AGG_STATE_OPERATIONAL)
			ieee80211_wake_queue(hw, sta->tid_to_tx_q[tid]);

		spin_unlock_bh(&sta->lock);
	} else {
		sta->ampdu_mlme.addba_req_num[tid]++;
		/* this will allow the state check in stop_BA_session */
		*state = HT_AGG_STATE_OPERATIONAL;
		spin_unlock_bh(&sta->lock);
		ieee80211_stop_tx_ba_session(hw, sta->addr, tid,
					     WLAN_BACK_INITIATOR);
	}

addba_resp_exit:
	rcu_read_unlock();
}

void ieee80211_send_delba(struct net_device *dev, const u8 *da, u16 tid,
			  u16 initiator, u16 reason_code)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_if_sta *ifsta = &sdata->u.sta;
	struct sk_buff *skb;
	struct ieee80211_mgmt *mgmt;
	u16 params;

	skb = dev_alloc_skb(sizeof(*mgmt) + local->hw.extra_tx_headroom);

	if (!skb) {
		printk(KERN_ERR "%s: failed to allocate buffer "
					"for delba frame\n", dev->name);
		return;
	}

	skb_reserve(skb, local->hw.extra_tx_headroom);
	mgmt = (struct ieee80211_mgmt *) skb_put(skb, 24);
	memset(mgmt, 0, 24);
	memcpy(mgmt->da, da, ETH_ALEN);
	memcpy(mgmt->sa, dev->dev_addr, ETH_ALEN);
	if (sdata->vif.type == IEEE80211_IF_TYPE_AP)
		memcpy(mgmt->bssid, dev->dev_addr, ETH_ALEN);
	else
		memcpy(mgmt->bssid, ifsta->bssid, ETH_ALEN);
	mgmt->frame_control = IEEE80211_FC(IEEE80211_FTYPE_MGMT,
					IEEE80211_STYPE_ACTION);

	skb_put(skb, 1 + sizeof(mgmt->u.action.u.delba));

	mgmt->u.action.category = WLAN_CATEGORY_BACK;
	mgmt->u.action.u.delba.action_code = WLAN_ACTION_DELBA;
	params = (u16)(initiator << 11); 	/* bit 11 initiator */
	params |= (u16)(tid << 12); 		/* bit 15:12 TID number */

	mgmt->u.action.u.delba.params = cpu_to_le16(params);
	mgmt->u.action.u.delba.reason_code = cpu_to_le16(reason_code);

	ieee80211_sta_tx(dev, skb, 0);
}

void ieee80211_send_bar(struct net_device *dev, u8 *ra, u16 tid, u16 ssn)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct sk_buff *skb;
	struct ieee80211_bar *bar;
	u16 bar_control = 0;

	skb = dev_alloc_skb(sizeof(*bar) + local->hw.extra_tx_headroom);
	if (!skb) {
		printk(KERN_ERR "%s: failed to allocate buffer for "
			"bar frame\n", dev->name);
		return;
	}
	skb_reserve(skb, local->hw.extra_tx_headroom);
	bar = (struct ieee80211_bar *)skb_put(skb, sizeof(*bar));
	memset(bar, 0, sizeof(*bar));
	bar->frame_control = IEEE80211_FC(IEEE80211_FTYPE_CTL,
					IEEE80211_STYPE_BACK_REQ);
	memcpy(bar->ra, ra, ETH_ALEN);
	memcpy(bar->ta, dev->dev_addr, ETH_ALEN);
	bar_control |= (u16)IEEE80211_BAR_CTRL_ACK_POLICY_NORMAL;
	bar_control |= (u16)IEEE80211_BAR_CTRL_CBMTID_COMPRESSED_BA;
	bar_control |= (u16)(tid << 12);
	bar->control = cpu_to_le16(bar_control);
	bar->start_seq_num = cpu_to_le16(ssn);

	ieee80211_sta_tx(dev, skb, 0);
}

void ieee80211_sta_stop_rx_ba_session(struct net_device *dev, u8 *ra, u16 tid,
					u16 initiator, u16 reason)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_hw *hw = &local->hw;
	struct sta_info *sta;
	int ret, i;
	DECLARE_MAC_BUF(mac);

	rcu_read_lock();

	sta = sta_info_get(local, ra);
	if (!sta) {
		rcu_read_unlock();
		return;
	}

	/* check if TID is in operational state */
	spin_lock_bh(&sta->lock);
	if (sta->ampdu_mlme.tid_state_rx[tid]
				!= HT_AGG_STATE_OPERATIONAL) {
		spin_unlock_bh(&sta->lock);
		rcu_read_unlock();
		return;
	}
	sta->ampdu_mlme.tid_state_rx[tid] =
		HT_AGG_STATE_REQ_STOP_BA_MSK |
		(initiator << HT_AGG_STATE_INITIATOR_SHIFT);
	spin_unlock_bh(&sta->lock);

	/* stop HW Rx aggregation. ampdu_action existence
	 * already verified in session init so we add the BUG_ON */
	BUG_ON(!local->ops->ampdu_action);

#ifdef CONFIG_MAC80211_HT_DEBUG
	printk(KERN_DEBUG "Rx BA session stop requested for %s tid %u\n",
				print_mac(mac, ra), tid);
#endif /* CONFIG_MAC80211_HT_DEBUG */

	ret = local->ops->ampdu_action(hw, IEEE80211_AMPDU_RX_STOP,
					ra, tid, NULL);
	if (ret)
		printk(KERN_DEBUG "HW problem - can not stop rx "
				"aggregation for tid %d\n", tid);

	/* shutdown timer has not expired */
	if (initiator != WLAN_BACK_TIMER)
		del_timer_sync(&sta->ampdu_mlme.tid_rx[tid]->session_timer);

	/* check if this is a self generated aggregation halt */
	if (initiator == WLAN_BACK_RECIPIENT || initiator == WLAN_BACK_TIMER)
		ieee80211_send_delba(dev, ra, tid, 0, reason);

	/* free the reordering buffer */
	for (i = 0; i < sta->ampdu_mlme.tid_rx[tid]->buf_size; i++) {
		if (sta->ampdu_mlme.tid_rx[tid]->reorder_buf[i]) {
			/* release the reordered frames */
			dev_kfree_skb(sta->ampdu_mlme.tid_rx[tid]->reorder_buf[i]);
			sta->ampdu_mlme.tid_rx[tid]->stored_mpdu_num--;
			sta->ampdu_mlme.tid_rx[tid]->reorder_buf[i] = NULL;
		}
	}
	/* free resources */
	kfree(sta->ampdu_mlme.tid_rx[tid]->reorder_buf);
	kfree(sta->ampdu_mlme.tid_rx[tid]);
	sta->ampdu_mlme.tid_rx[tid] = NULL;
	sta->ampdu_mlme.tid_state_rx[tid] = HT_AGG_STATE_IDLE;

	rcu_read_unlock();
}


static void ieee80211_sta_process_delba(struct net_device *dev,
			struct ieee80211_mgmt *mgmt, size_t len)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct sta_info *sta;
	u16 tid, params;
	u16 initiator;
	DECLARE_MAC_BUF(mac);

	rcu_read_lock();

	sta = sta_info_get(local, mgmt->sa);
	if (!sta) {
		rcu_read_unlock();
		return;
	}

	params = le16_to_cpu(mgmt->u.action.u.delba.params);
	tid = (params & IEEE80211_DELBA_PARAM_TID_MASK) >> 12;
	initiator = (params & IEEE80211_DELBA_PARAM_INITIATOR_MASK) >> 11;

#ifdef CONFIG_MAC80211_HT_DEBUG
	if (net_ratelimit())
		printk(KERN_DEBUG "delba from %s (%s) tid %d reason code %d\n",
			print_mac(mac, mgmt->sa),
			initiator ? "initiator" : "recipient", tid,
			mgmt->u.action.u.delba.reason_code);
#endif /* CONFIG_MAC80211_HT_DEBUG */

	if (initiator == WLAN_BACK_INITIATOR)
		ieee80211_sta_stop_rx_ba_session(dev, sta->addr, tid,
						 WLAN_BACK_INITIATOR, 0);
	else { /* WLAN_BACK_RECIPIENT */
		spin_lock_bh(&sta->lock);
		sta->ampdu_mlme.tid_state_tx[tid] =
				HT_AGG_STATE_OPERATIONAL;
		spin_unlock_bh(&sta->lock);
		ieee80211_stop_tx_ba_session(&local->hw, sta->addr, tid,
					     WLAN_BACK_RECIPIENT);
	}
	rcu_read_unlock();
}

/*
 * After sending add Block Ack request we activated a timer until
 * add Block Ack response will arrive from the recipient.
 * If this timer expires sta_addba_resp_timer_expired will be executed.
 */
void sta_addba_resp_timer_expired(unsigned long data)
{
	/* not an elegant detour, but there is no choice as the timer passes
	 * only one argument, and both sta_info and TID are needed, so init
	 * flow in sta_info_create gives the TID as data, while the timer_to_id
	 * array gives the sta through container_of */
	u16 tid = *(u8 *)data;
	struct sta_info *temp_sta = container_of((void *)data,
		struct sta_info, timer_to_tid[tid]);

	struct ieee80211_local *local = temp_sta->local;
	struct ieee80211_hw *hw = &local->hw;
	struct sta_info *sta;
	u8 *state;

	rcu_read_lock();

	sta = sta_info_get(local, temp_sta->addr);
	if (!sta) {
		rcu_read_unlock();
		return;
	}

	state = &sta->ampdu_mlme.tid_state_tx[tid];
	/* check if the TID waits for addBA response */
	spin_lock_bh(&sta->lock);
	if (!(*state & HT_ADDBA_REQUESTED_MSK)) {
		spin_unlock_bh(&sta->lock);
		*state = HT_AGG_STATE_IDLE;
#ifdef CONFIG_MAC80211_HT_DEBUG
		printk(KERN_DEBUG "timer expired on tid %d but we are not "
				"expecting addBA response there", tid);
#endif
		goto timer_expired_exit;
	}

#ifdef CONFIG_MAC80211_HT_DEBUG
	printk(KERN_DEBUG "addBA response timer expired on tid %d\n", tid);
#endif

	/* go through the state check in stop_BA_session */
	*state = HT_AGG_STATE_OPERATIONAL;
	spin_unlock_bh(&sta->lock);
	ieee80211_stop_tx_ba_session(hw, temp_sta->addr, tid,
				     WLAN_BACK_INITIATOR);

timer_expired_exit:
	rcu_read_unlock();
}

/*
 * After accepting the AddBA Request we activated a timer,
 * resetting it after each frame that arrives from the originator.
 * if this timer expires ieee80211_sta_stop_rx_ba_session will be executed.
 */
static void sta_rx_agg_session_timer_expired(unsigned long data)
{
	/* not an elegant detour, but there is no choice as the timer passes
	 * only one argument, and various sta_info are needed here, so init
	 * flow in sta_info_create gives the TID as data, while the timer_to_id
	 * array gives the sta through container_of */
	u8 *ptid = (u8 *)data;
	u8 *timer_to_id = ptid - *ptid;
	struct sta_info *sta = container_of(timer_to_id, struct sta_info,
					 timer_to_tid[0]);

#ifdef CONFIG_MAC80211_HT_DEBUG
	printk(KERN_DEBUG "rx session timer expired on tid %d\n", (u16)*ptid);
#endif
	ieee80211_sta_stop_rx_ba_session(sta->sdata->dev, sta->addr,
					 (u16)*ptid, WLAN_BACK_TIMER,
					 WLAN_REASON_QSTA_TIMEOUT);
}

void ieee80211_sta_tear_down_BA_sessions(struct net_device *dev, u8 *addr)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	int i;

	for (i = 0; i <  STA_TID_NUM; i++) {
		ieee80211_stop_tx_ba_session(&local->hw, addr, i,
					     WLAN_BACK_INITIATOR);
		ieee80211_sta_stop_rx_ba_session(dev, addr, i,
						 WLAN_BACK_RECIPIENT,
						 WLAN_REASON_QSTA_LEAVE_QBSS);
	}
}

static void ieee80211_send_refuse_measurement_request(struct net_device *dev,
					struct ieee80211_msrment_ie *request_ie,
					const u8 *da, const u8 *bssid,
					u8 dialog_token)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct sk_buff *skb;
	struct ieee80211_mgmt *msr_report;

	skb = dev_alloc_skb(sizeof(*msr_report) + local->hw.extra_tx_headroom +
				sizeof(struct ieee80211_msrment_ie));

	if (!skb) {
		printk(KERN_ERR "%s: failed to allocate buffer for "
				"measurement report frame\n", dev->name);
		return;
	}

	skb_reserve(skb, local->hw.extra_tx_headroom);
	msr_report = (struct ieee80211_mgmt *)skb_put(skb, 24);
	memset(msr_report, 0, 24);
	memcpy(msr_report->da, da, ETH_ALEN);
	memcpy(msr_report->sa, dev->dev_addr, ETH_ALEN);
	memcpy(msr_report->bssid, bssid, ETH_ALEN);
	msr_report->frame_control = IEEE80211_FC(IEEE80211_FTYPE_MGMT,
						IEEE80211_STYPE_ACTION);

	skb_put(skb, 1 + sizeof(msr_report->u.action.u.measurement));
	msr_report->u.action.category = WLAN_CATEGORY_SPECTRUM_MGMT;
	msr_report->u.action.u.measurement.action_code =
				WLAN_ACTION_SPCT_MSR_RPRT;
	msr_report->u.action.u.measurement.dialog_token = dialog_token;

	msr_report->u.action.u.measurement.element_id = WLAN_EID_MEASURE_REPORT;
	msr_report->u.action.u.measurement.length =
			sizeof(struct ieee80211_msrment_ie);

	memset(&msr_report->u.action.u.measurement.msr_elem, 0,
		sizeof(struct ieee80211_msrment_ie));
	msr_report->u.action.u.measurement.msr_elem.token = request_ie->token;
	msr_report->u.action.u.measurement.msr_elem.mode |=
			IEEE80211_SPCT_MSR_RPRT_MODE_REFUSED;
	msr_report->u.action.u.measurement.msr_elem.type = request_ie->type;

	ieee80211_sta_tx(dev, skb, 0);
}

static void ieee80211_sta_process_measurement_req(struct net_device *dev,
						struct ieee80211_mgmt *mgmt,
						size_t len)
{
	/*
	 * Ignoring measurement request is spec violation.
	 * Mandatory measurements must be reported optional
	 * measurements might be refused or reported incapable
	 * For now just refuse
	 * TODO: Answer basic measurement as unmeasured
	 */
	ieee80211_send_refuse_measurement_request(dev,
			&mgmt->u.action.u.measurement.msr_elem,
			mgmt->sa, mgmt->bssid,
			mgmt->u.action.u.measurement.dialog_token);
}


static void ieee80211_rx_mgmt_auth(struct net_device *dev,
				   struct ieee80211_if_sta *ifsta,
				   struct ieee80211_mgmt *mgmt,
				   size_t len)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	u16 auth_alg, auth_transaction, status_code;
	DECLARE_MAC_BUF(mac);

	if (ifsta->state != IEEE80211_AUTHENTICATE &&
	    sdata->vif.type != IEEE80211_IF_TYPE_IBSS)
		return;

	if (len < 24 + 6)
		return;

	if (sdata->vif.type != IEEE80211_IF_TYPE_IBSS &&
	    memcmp(ifsta->bssid, mgmt->sa, ETH_ALEN) != 0)
		return;

	if (sdata->vif.type != IEEE80211_IF_TYPE_IBSS &&
	    memcmp(ifsta->bssid, mgmt->bssid, ETH_ALEN) != 0)
	/* if present, add any custom IEs that go before HT */
	if (assoc_data->ie_len) {
		static const u8 before_ht[] = {
			WLAN_EID_SSID,
			WLAN_EID_SUPP_RATES,
			WLAN_EID_EXT_SUPP_RATES,
			WLAN_EID_PWR_CAPABILITY,
			WLAN_EID_SUPPORTED_CHANNELS,
			WLAN_EID_RSN,
			WLAN_EID_QOS_CAPA,
			WLAN_EID_RRM_ENABLED_CAPABILITIES,
			WLAN_EID_MOBILITY_DOMAIN,
			WLAN_EID_FAST_BSS_TRANSITION,	/* reassoc only */
			WLAN_EID_RIC_DATA,		/* reassoc only */
			WLAN_EID_SUPPORTED_REGULATORY_CLASSES,
		};
		static const u8 after_ric[] = {
			WLAN_EID_SUPPORTED_REGULATORY_CLASSES,
			WLAN_EID_HT_CAPABILITY,
			WLAN_EID_BSS_COEX_2040,
			WLAN_EID_EXT_CAPABILITY,
			WLAN_EID_QOS_TRAFFIC_CAPA,
			WLAN_EID_TIM_BCAST_REQ,
			WLAN_EID_INTERWORKING,
			/* 60GHz doesn't happen right now */
			WLAN_EID_VHT_CAPABILITY,
			WLAN_EID_OPMODE_NOTIF,
		};

		noffset = ieee80211_ie_split_ric(assoc_data->ie,
						 assoc_data->ie_len,
						 before_ht,
						 ARRAY_SIZE(before_ht),
						 after_ric,
						 ARRAY_SIZE(after_ric),
						 offset);
		pos = skb_put(skb, noffset - offset);
		memcpy(pos, assoc_data->ie + offset, noffset - offset);
		offset = noffset;
	}

	if (WARN_ON_ONCE((ifmgd->flags & IEEE80211_STA_DISABLE_HT) &&
			 !(ifmgd->flags & IEEE80211_STA_DISABLE_VHT)))
		ifmgd->flags |= IEEE80211_STA_DISABLE_VHT;

	if (!(ifmgd->flags & IEEE80211_STA_DISABLE_HT))
		ieee80211_add_ht_ie(sdata, skb, assoc_data->ap_ht_param,
				    sband, chan, sdata->smps_mode);

	/* if present, add any custom IEs that go before VHT */
	if (assoc_data->ie_len) {
		static const u8 before_vht[] = {
			WLAN_EID_SSID,
			WLAN_EID_SUPP_RATES,
			WLAN_EID_EXT_SUPP_RATES,
			WLAN_EID_PWR_CAPABILITY,
			WLAN_EID_SUPPORTED_CHANNELS,
			WLAN_EID_RSN,
			WLAN_EID_QOS_CAPA,
			WLAN_EID_RRM_ENABLED_CAPABILITIES,
			WLAN_EID_MOBILITY_DOMAIN,
			WLAN_EID_SUPPORTED_REGULATORY_CLASSES,
			WLAN_EID_HT_CAPABILITY,
			WLAN_EID_BSS_COEX_2040,
			WLAN_EID_EXT_CAPABILITY,
			WLAN_EID_QOS_TRAFFIC_CAPA,
			WLAN_EID_TIM_BCAST_REQ,
			WLAN_EID_INTERWORKING,
		};

		/* RIC already taken above, so no need to handle here anymore */
		noffset = ieee80211_ie_split(assoc_data->ie, assoc_data->ie_len,
					     before_vht, ARRAY_SIZE(before_vht),
					     offset);
		pos = skb_put(skb, noffset - offset);
		memcpy(pos, assoc_data->ie + offset, noffset - offset);
		offset = noffset;
	}

	if (!(ifmgd->flags & IEEE80211_STA_DISABLE_VHT))
		ieee80211_add_vht_ie(sdata, skb, sband,
				     &assoc_data->ap_vht_cap);

	/* if present, add any custom non-vendor IEs that go after HT */
	if (assoc_data->ie_len) {
		noffset = ieee80211_ie_split_vendor(assoc_data->ie,
						    assoc_data->ie_len,
						    offset);
		pos = skb_put(skb, noffset - offset);
		memcpy(pos, assoc_data->ie + offset, noffset - offset);
		offset = noffset;
	}

	if (assoc_data->wmm) {
		if (assoc_data->uapsd) {
			qos_info = ifmgd->uapsd_queues;
			qos_info |= (ifmgd->uapsd_max_sp_len <<
				     IEEE80211_WMM_IE_STA_QOSINFO_SP_SHIFT);
		} else {
			qos_info = 0;
		}

		pos = ieee80211_add_wmm_info_ie(skb_put(skb, 9), qos_info);
	}

	/* add any remaining custom (i.e. vendor specific here) IEs */
	if (assoc_data->ie_len) {
		noffset = assoc_data->ie_len;
		pos = skb_put(skb, noffset - offset);
		memcpy(pos, assoc_data->ie + offset, noffset - offset);
	}

	drv_mgd_prepare_tx(local, sdata);

	IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT;
	if (ieee80211_hw_check(&local->hw, REPORTS_TX_ACK_STATUS))
		IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_CTL_REQ_TX_STATUS |
						IEEE80211_TX_INTFL_MLME_CONN_TX;
	ieee80211_tx_skb(sdata, skb);
}

void ieee80211_send_pspoll(struct ieee80211_local *local,
			   struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_pspoll *pspoll;
	struct sk_buff *skb;

	skb = ieee80211_pspoll_get(&local->hw, &sdata->vif);
	if (!skb)
		return;

	pspoll = (struct ieee80211_pspoll *) skb->data;
	pspoll->frame_control |= cpu_to_le16(IEEE80211_FCTL_PM);

	IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT;
	ieee80211_tx_skb(sdata, skb);
}

void ieee80211_send_nullfunc(struct ieee80211_local *local,
			     struct ieee80211_sub_if_data *sdata,
			     bool powersave)
{
	struct sk_buff *skb;
	struct ieee80211_hdr_3addr *nullfunc;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	skb = ieee80211_nullfunc_get(&local->hw, &sdata->vif);
	if (!skb)
		return;

	nullfunc = (struct ieee80211_hdr_3addr *) skb->data;
	if (powersave)
		nullfunc->frame_control |= cpu_to_le16(IEEE80211_FCTL_PM);

	IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT |
					IEEE80211_TX_INTFL_OFFCHAN_TX_OK;

	if (ieee80211_hw_check(&local->hw, REPORTS_TX_ACK_STATUS))
		IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_CTL_REQ_TX_STATUS;

	if (ifmgd->flags & IEEE80211_STA_CONNECTION_POLL)
		IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_CTL_USE_MINRATE;

	ieee80211_tx_skb(sdata, skb);
}

static void ieee80211_send_4addr_nullfunc(struct ieee80211_local *local,
					  struct ieee80211_sub_if_data *sdata)
{
	struct sk_buff *skb;
	struct ieee80211_hdr *nullfunc;
	__le16 fc;

	if (WARN_ON(sdata->vif.type != NL80211_IFTYPE_STATION))
		return;

	skb = dev_alloc_skb(local->hw.extra_tx_headroom + 30);
	if (!skb)
		return;

	skb_reserve(skb, local->hw.extra_tx_headroom);

	nullfunc = (struct ieee80211_hdr *) skb_put(skb, 30);
	memset(nullfunc, 0, 30);
	fc = cpu_to_le16(IEEE80211_FTYPE_DATA | IEEE80211_STYPE_NULLFUNC |
			 IEEE80211_FCTL_FROMDS | IEEE80211_FCTL_TODS);
	nullfunc->frame_control = fc;
	memcpy(nullfunc->addr1, sdata->u.mgd.bssid, ETH_ALEN);
	memcpy(nullfunc->addr2, sdata->vif.addr, ETH_ALEN);
	memcpy(nullfunc->addr3, sdata->u.mgd.bssid, ETH_ALEN);
	memcpy(nullfunc->addr4, sdata->vif.addr, ETH_ALEN);

	IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT;
	ieee80211_tx_skb(sdata, skb);
}

/* spectrum management related things */
static void ieee80211_chswitch_work(struct work_struct *work)
{
	struct ieee80211_sub_if_data *sdata =
		container_of(work, struct ieee80211_sub_if_data, u.mgd.chswitch_work);
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	int ret;

	if (!ieee80211_sdata_running(sdata))
		return;

	sdata_lock(sdata);
	mutex_lock(&local->mtx);
	mutex_lock(&local->chanctx_mtx);

	if (!ifmgd->associated)
		goto out;

	if (!sdata->vif.csa_active)
		goto out;

	/*
	 * using reservation isn't immediate as it may be deferred until later
	 * with multi-vif. once reservation is complete it will re-schedule the
	 * work with no reserved_chanctx so verify chandef to check if it
	 * completed successfully
	 */

	if (sdata->reserved_chanctx) {
		struct ieee80211_supported_band *sband = NULL;
		struct sta_info *mgd_sta = NULL;
		enum ieee80211_sta_rx_bandwidth bw = IEEE80211_STA_RX_BW_20;

		/*
		 * with multi-vif csa driver may call ieee80211_csa_finish()
		 * many times while waiting for other interfaces to use their
		 * reservations
		 */
		if (sdata->reserved_ready)
			goto out;

		if (sdata->vif.bss_conf.chandef.width !=
		    sdata->csa_chandef.width) {
			/*
			 * For managed interface, we need to also update the AP
			 * station bandwidth and align the rate scale algorithm
			 * on the bandwidth change. Here we only consider the
			 * bandwidth of the new channel definition (as channel
			 * switch flow does not have the full HT/VHT/HE
			 * information), assuming that if additional changes are
			 * required they would be done as part of the processing
			 * of the next beacon from the AP.
			 */
			switch (sdata->csa_chandef.width) {
			case NL80211_CHAN_WIDTH_20_NOHT:
			case NL80211_CHAN_WIDTH_20:
			default:
				bw = IEEE80211_STA_RX_BW_20;
				break;
			case NL80211_CHAN_WIDTH_40:
				bw = IEEE80211_STA_RX_BW_40;
				break;
			case NL80211_CHAN_WIDTH_80:
				bw = IEEE80211_STA_RX_BW_80;
				break;
			case NL80211_CHAN_WIDTH_80P80:
			case NL80211_CHAN_WIDTH_160:
				bw = IEEE80211_STA_RX_BW_160;
				break;
			}

			mgd_sta = sta_info_get(sdata, ifmgd->bssid);
			sband =
				local->hw.wiphy->bands[sdata->csa_chandef.chan->band];
		}

		if (sdata->vif.bss_conf.chandef.width >
		    sdata->csa_chandef.width) {
			mgd_sta->sta.bandwidth = bw;
			rate_control_rate_update(local, sband, mgd_sta,
						 IEEE80211_RC_BW_CHANGED);
		}

		ret = ieee80211_vif_use_reserved_context(sdata);
		if (ret) {
			sdata_info(sdata,
				   "failed to use reserved channel context, disconnecting (err=%d)\n",
				   ret);
			ieee80211_queue_work(&sdata->local->hw,
					     &ifmgd->csa_connection_drop_work);
			goto out;
		}

		if (sdata->vif.bss_conf.chandef.width <
		    sdata->csa_chandef.width) {
			mgd_sta->sta.bandwidth = bw;
			rate_control_rate_update(local, sband, mgd_sta,
						 IEEE80211_RC_BW_CHANGED);
		}

		goto out;
	}

	if (!cfg80211_chandef_identical(&sdata->vif.bss_conf.chandef,
					&sdata->csa_chandef)) {
		sdata_info(sdata,
			   "failed to finalize channel switch, disconnecting\n");
		ieee80211_queue_work(&sdata->local->hw,
				     &ifmgd->csa_connection_drop_work);
		goto out;
	}

	ifmgd->csa_waiting_bcn = true;

	ieee80211_sta_reset_beacon_monitor(sdata);
	ieee80211_sta_reset_conn_monitor(sdata);

out:
	mutex_unlock(&local->chanctx_mtx);
	mutex_unlock(&local->mtx);
	sdata_unlock(sdata);
}

static void ieee80211_chswitch_post_beacon(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	int ret;

	sdata_assert_lock(sdata);

	WARN_ON(!sdata->vif.csa_active);

	if (sdata->csa_block_tx) {
		ieee80211_wake_vif_queues(local, sdata,
					  IEEE80211_QUEUE_STOP_REASON_CSA);
		sdata->csa_block_tx = false;
	}

	sdata->vif.csa_active = false;
	ifmgd->csa_waiting_bcn = false;
	/*
	 * If the CSA IE is still present on the beacon after the switch,
	 * we need to consider it as a new CSA (possibly to self).
	 */
	ifmgd->beacon_crc_valid = false;

	ret = drv_post_channel_switch(sdata);
	if (ret) {
		sdata_info(sdata,
			   "driver post channel switch failed, disconnecting\n");
		ieee80211_queue_work(&local->hw,
				     &ifmgd->csa_connection_drop_work);
		return;
	}

	cfg80211_ch_switch_notify(sdata->dev, &sdata->reserved_chandef);
}

void ieee80211_chswitch_done(struct ieee80211_vif *vif, bool success)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	trace_api_chswitch_done(sdata, success);
	if (!success) {
		sdata_info(sdata,
			   "driver channel switch failed, disconnecting\n");
		ieee80211_queue_work(&sdata->local->hw,
				     &ifmgd->csa_connection_drop_work);
	} else {
		ieee80211_queue_work(&sdata->local->hw, &ifmgd->chswitch_work);
	}
}
EXPORT_SYMBOL(ieee80211_chswitch_done);

static void ieee80211_chswitch_timer(unsigned long data)
{
	struct ieee80211_sub_if_data *sdata =
		(struct ieee80211_sub_if_data *) data;

	ieee80211_queue_work(&sdata->local->hw, &sdata->u.mgd.chswitch_work);
}

static void
ieee80211_sta_process_chanswitch(struct ieee80211_sub_if_data *sdata,
				 u64 timestamp, u32 device_timestamp,
				 struct ieee802_11_elems *elems,
				 bool beacon)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct cfg80211_bss *cbss = ifmgd->associated;
	struct ieee80211_chanctx_conf *conf;
	struct ieee80211_chanctx *chanctx;
	enum ieee80211_band current_band;
	struct ieee80211_csa_ie csa_ie;
	struct ieee80211_channel_switch ch_switch;
	int res;

	sdata_assert_lock(sdata);

	if (!cbss)
		return;

	if (local->scanning)
		return;

	/* disregard subsequent announcements if we are already processing */
	if (sdata->vif.csa_active)
		return;

	current_band = cbss->channel->band;
	memset(&csa_ie, 0, sizeof(csa_ie));
	res = ieee80211_parse_ch_switch_ie(sdata, elems, current_band,
					   ifmgd->flags,
					   ifmgd->associated->bssid, &csa_ie);
	if (res	< 0)
		ieee80211_queue_work(&local->hw,
				     &ifmgd->csa_connection_drop_work);
	if (res)
		return;

	if (!cfg80211_chandef_usable(local->hw.wiphy, &csa_ie.chandef,
				     IEEE80211_CHAN_DISABLED)) {
		sdata_info(sdata,
			   "AP %pM switches to unsupported channel (%d MHz, width:%d, CF1/2: %d/%d MHz), disconnecting\n",
			   ifmgd->associated->bssid,
			   csa_ie.chandef.chan->center_freq,
			   csa_ie.chandef.width, csa_ie.chandef.center_freq1,
			   csa_ie.chandef.center_freq2);
		ieee80211_queue_work(&local->hw,
				     &ifmgd->csa_connection_drop_work);
		return;
	}

	if (cfg80211_chandef_identical(&csa_ie.chandef,
				       &sdata->vif.bss_conf.chandef)) {
		if (ifmgd->csa_ignored_same_chan)
			return;
		sdata_info(sdata,
			   "AP %pM tries to chanswitch to same channel, ignore\n",
			   ifmgd->associated->bssid);
		ifmgd->csa_ignored_same_chan = true;
		return;
	}

	/*
	 * Drop all TDLS peers - either we disconnect or move to a different
	 * channel from this point on. There's no telling what our peer will do.
	 * The TDLS WIDER_BW scenario is also problematic, as peers might now
	 * have an incompatible wider chandef.
	 */
	ieee80211_teardown_tdls_peers(sdata);

	mutex_lock(&local->mtx);
	mutex_lock(&local->chanctx_mtx);
	conf = rcu_dereference_protected(sdata->vif.chanctx_conf,
					 lockdep_is_held(&local->chanctx_mtx));
	if (!conf) {
		sdata_info(sdata,
			   "no channel context assigned to vif?, disconnecting\n");
		goto drop_connection;
	}

	chanctx = container_of(conf, struct ieee80211_chanctx, conf);

	if (local->use_chanctx &&
	    !ieee80211_hw_check(&local->hw, CHANCTX_STA_CSA)) {
		sdata_info(sdata,
			   "driver doesn't support chan-switch with channel contexts\n");
		goto drop_connection;
	}

	ch_switch.timestamp = timestamp;
	ch_switch.device_timestamp = device_timestamp;
	ch_switch.block_tx = csa_ie.mode;
	ch_switch.chandef = csa_ie.chandef;
	ch_switch.count = csa_ie.count;

	if (drv_pre_channel_switch(sdata, &ch_switch)) {
		sdata_info(sdata,
			   "preparing for channel switch failed, disconnecting\n");
		goto drop_connection;
	}

	res = ieee80211_vif_reserve_chanctx(sdata, &csa_ie.chandef,
					    chanctx->mode, false);
	if (res) {
		sdata_info(sdata,
			   "failed to reserve channel context for channel switch, disconnecting (err=%d)\n",
			   res);
		goto drop_connection;
	}
	mutex_unlock(&local->chanctx_mtx);

	sdata->vif.csa_active = true;
	sdata->csa_chandef = csa_ie.chandef;
	sdata->csa_block_tx = csa_ie.mode;
	ifmgd->csa_ignored_same_chan = false;

	if (sdata->csa_block_tx)
		ieee80211_stop_vif_queues(local, sdata,
					  IEEE80211_QUEUE_STOP_REASON_CSA);
	mutex_unlock(&local->mtx);

	cfg80211_ch_switch_started_notify(sdata->dev, &csa_ie.chandef,
					  csa_ie.count);

	if (local->ops->channel_switch) {
		/* use driver's channel switch callback */
		drv_channel_switch(local, sdata, &ch_switch);
		return;
	}

	/* channel switch handled in software */
	if (csa_ie.count <= 1)
		ieee80211_queue_work(&local->hw, &ifmgd->chswitch_work);
	else
		mod_timer(&ifmgd->chswitch_timer,
			  TU_TO_EXP_TIME((csa_ie.count - 1) *
					 cbss->beacon_interval));
	return;
 drop_connection:
	ieee80211_queue_work(&local->hw, &ifmgd->csa_connection_drop_work);
	mutex_unlock(&local->chanctx_mtx);
	mutex_unlock(&local->mtx);
}

static bool
ieee80211_find_80211h_pwr_constr(struct ieee80211_sub_if_data *sdata,
				 struct ieee80211_channel *channel,
				 const u8 *country_ie, u8 country_ie_len,
				 const u8 *pwr_constr_elem,
				 int *chan_pwr, int *pwr_reduction)
{
	struct ieee80211_country_ie_triplet *triplet;
	int chan = ieee80211_frequency_to_channel(channel->center_freq);
	int i, chan_increment;
	bool have_chan_pwr = false;

	/* Invalid IE */
	if (country_ie_len % 2 || country_ie_len < IEEE80211_COUNTRY_IE_MIN_LEN)
		return false;

	triplet = (void *)(country_ie + 3);
	country_ie_len -= 3;

	switch (channel->band) {
	default:
		WARN_ON_ONCE(1);
		/* fall through */
	case IEEE80211_BAND_2GHZ:
	case IEEE80211_BAND_60GHZ:
		chan_increment = 1;
		break;
	case IEEE80211_BAND_5GHZ:
		chan_increment = 4;
		break;
	}

	/* find channel */
	while (country_ie_len >= 3) {
		u8 first_channel = triplet->chans.first_channel;

		if (first_channel >= IEEE80211_COUNTRY_EXTENSION_ID)
			goto next;

		for (i = 0; i < triplet->chans.num_channels; i++) {
			if (first_channel + i * chan_increment == chan) {
				have_chan_pwr = true;
				*chan_pwr = triplet->chans.max_power;
				break;
			}
		}
		if (have_chan_pwr)
			break;

 next:
		triplet++;
		country_ie_len -= 3;
	}

	if (have_chan_pwr && pwr_constr_elem)
		*pwr_reduction = *pwr_constr_elem;
	else
		*pwr_reduction = 0;

	return have_chan_pwr;
}

static void ieee80211_find_cisco_dtpc(struct ieee80211_sub_if_data *sdata,
				      struct ieee80211_channel *channel,
				      const u8 *cisco_dtpc_ie,
				      int *pwr_level)
{
	/* From practical testing, the first data byte of the DTPC element
	 * seems to contain the requested dBm level, and the CLI on Cisco
	 * APs clearly state the range is -127 to 127 dBm, which indicates
	 * a signed byte, although it seemingly never actually goes negative.
	 * The other byte seems to always be zero.
	 */
	*pwr_level = (__s8)cisco_dtpc_ie[4];
}

static u32 ieee80211_handle_pwr_constr(struct ieee80211_sub_if_data *sdata,
				       struct ieee80211_channel *channel,
				       struct ieee80211_mgmt *mgmt,
				       const u8 *country_ie, u8 country_ie_len,
				       const u8 *pwr_constr_ie,
				       const u8 *cisco_dtpc_ie)
{
	bool has_80211h_pwr = false, has_cisco_pwr = false;
	int chan_pwr = 0, pwr_reduction_80211h = 0;
	int pwr_level_cisco, pwr_level_80211h;
	int new_ap_level;
	__le16 capab = mgmt->u.probe_resp.capab_info;

	if (country_ie &&
	    (capab & cpu_to_le16(WLAN_CAPABILITY_SPECTRUM_MGMT) ||
	     capab & cpu_to_le16(WLAN_CAPABILITY_RADIO_MEASURE))) {
		has_80211h_pwr = ieee80211_find_80211h_pwr_constr(
			sdata, channel, country_ie, country_ie_len,
			pwr_constr_ie, &chan_pwr, &pwr_reduction_80211h);
		pwr_level_80211h =
			max_t(int, 0, chan_pwr - pwr_reduction_80211h);
	}

	if (cisco_dtpc_ie) {
		ieee80211_find_cisco_dtpc(
			sdata, channel, cisco_dtpc_ie, &pwr_level_cisco);
		has_cisco_pwr = true;
	}

	if (!has_80211h_pwr && !has_cisco_pwr)
		return 0;

	/* If we have both 802.11h and Cisco DTPC, apply both limits
	 * by picking the smallest of the two power levels advertised.
	 */
	if (has_80211h_pwr &&
	    (!has_cisco_pwr || pwr_level_80211h <= pwr_level_cisco)) {
		new_ap_level = pwr_level_80211h;

		if (sdata->ap_power_level == new_ap_level)
			return 0;

		sdata_dbg(sdata,
			  "Limiting TX power to %d (%d - %d) dBm as advertised by %pM\n",
			  pwr_level_80211h, chan_pwr, pwr_reduction_80211h,
			  sdata->u.mgd.bssid);
	} else {  /* has_cisco_pwr is always true here. */
		new_ap_level = pwr_level_cisco;

		if (sdata->ap_power_level == new_ap_level)
			return 0;

		sdata_dbg(sdata,
			  "Limiting TX power to %d dBm as advertised by %pM\n",
			  pwr_level_cisco, sdata->u.mgd.bssid);
	}

	sdata->ap_power_level = new_ap_level;
	if (__ieee80211_recalc_txpower(sdata))
		return BSS_CHANGED_TXPOWER;
	return 0;
}

/* powersave */
static void ieee80211_enable_ps(struct ieee80211_local *local,
				struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_conf *conf = &local->hw.conf;

	/*
	 * If we are scanning right now then the parameters will
	 * take effect when scan finishes.
	 */
	if (local->scanning)
		return;

	if (conf->dynamic_ps_timeout > 0 &&
	    !ieee80211_hw_check(&local->hw, SUPPORTS_DYNAMIC_PS)) {
		mod_timer(&local->dynamic_ps_timer, jiffies +
			  msecs_to_jiffies(conf->dynamic_ps_timeout));
	} else {
		if (ieee80211_hw_check(&local->hw, PS_NULLFUNC_STACK))
			ieee80211_send_nullfunc(local, sdata, true);

		if (ieee80211_hw_check(&local->hw, PS_NULLFUNC_STACK) &&
		    ieee80211_hw_check(&local->hw, REPORTS_TX_ACK_STATUS))
			return;

		conf->flags |= IEEE80211_CONF_PS;
		ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_PS);
	}
}

static void ieee80211_change_ps(struct ieee80211_local *local)
{
	struct ieee80211_conf *conf = &local->hw.conf;

	if (local->ps_sdata) {
		ieee80211_enable_ps(local, local->ps_sdata);
	} else if (conf->flags & IEEE80211_CONF_PS) {
		conf->flags &= ~IEEE80211_CONF_PS;
		ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_PS);
		del_timer_sync(&local->dynamic_ps_timer);
		cancel_work_sync(&local->dynamic_ps_enable_work);
	}
}

static bool ieee80211_powersave_allowed(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *mgd = &sdata->u.mgd;
	struct sta_info *sta = NULL;
	bool authorized = false;

	if (!mgd->powersave)
		return false;

	if (mgd->broken_ap)
		return false;

	if (!mgd->associated)
		return false;

	if (mgd->flags & IEEE80211_STA_CONNECTION_POLL)
		return false;

	if (!mgd->have_beacon)
		return false;

	rcu_read_lock();
	sta = sta_info_get(sdata, mgd->bssid);
	if (sta)
		authorized = test_sta_flag(sta, WLAN_STA_AUTHORIZED);
	rcu_read_unlock();

	return authorized;
}

/* need to hold RTNL or interface lock */
void ieee80211_recalc_ps(struct ieee80211_local *local)
{
	struct ieee80211_sub_if_data *sdata, *found = NULL;
	int count = 0;
	int timeout;

	if (!ieee80211_hw_check(&local->hw, SUPPORTS_PS)) {
		local->ps_sdata = NULL;
		return;
	}

	list_for_each_entry(sdata, &local->interfaces, list) {
		if (!ieee80211_sdata_running(sdata))
			continue;
		if (sdata->vif.type == NL80211_IFTYPE_AP) {
			/* If an AP vif is found, then disable PS
			 * by setting the count to zero thereby setting
			 * ps_sdata to NULL.
			 */
			count = 0;
			break;
		}
		if (sdata->vif.type != NL80211_IFTYPE_STATION)
			continue;
		found = sdata;
		count++;
	}

	if (count == 1 && ieee80211_powersave_allowed(found)) {
		u8 dtimper = found->u.mgd.dtim_period;
		s32 beaconint_us;

		beaconint_us = ieee80211_tu_to_usec(
					found->vif.bss_conf.beacon_int);

		timeout = local->dynamic_ps_forced_timeout;
		if (timeout < 0)
			timeout = 100;
		local->hw.conf.dynamic_ps_timeout = timeout;

		/* If the TIM IE is invalid, pretend the value is 1 */
		if (!dtimper)
			dtimper = 1;

		local->hw.conf.ps_dtim_period = dtimper;
		local->ps_sdata = found;
	} else {
		local->ps_sdata = NULL;
	}

	ieee80211_change_ps(local);
}

void ieee80211_recalc_ps_vif(struct ieee80211_sub_if_data *sdata)
{
	bool ps_allowed = ieee80211_powersave_allowed(sdata);

	if (sdata->vif.bss_conf.ps != ps_allowed) {
		sdata->vif.bss_conf.ps = ps_allowed;
		ieee80211_bss_info_change_notify(sdata, BSS_CHANGED_PS);
	}
}

void ieee80211_dynamic_ps_disable_work(struct work_struct *work)
{
	struct ieee80211_local *local =
		container_of(work, struct ieee80211_local,
			     dynamic_ps_disable_work);

	if (local->hw.conf.flags & IEEE80211_CONF_PS) {
		local->hw.conf.flags &= ~IEEE80211_CONF_PS;
		ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_PS);
	}

	ieee80211_wake_queues_by_reason(&local->hw,
					IEEE80211_MAX_QUEUE_MAP,
					IEEE80211_QUEUE_STOP_REASON_PS,
					false);
}

void ieee80211_dynamic_ps_enable_work(struct work_struct *work)
{
	struct ieee80211_local *local =
		container_of(work, struct ieee80211_local,
			     dynamic_ps_enable_work);
	struct ieee80211_sub_if_data *sdata = local->ps_sdata;
	struct ieee80211_if_managed *ifmgd;
	unsigned long flags;
	int q;

	/* can only happen when PS was just disabled anyway */
	if (!sdata)
		return;

	ifmgd = &sdata->u.mgd;

	if (local->hw.conf.flags & IEEE80211_CONF_PS)
		return;

	if (local->hw.conf.dynamic_ps_timeout > 0) {
		/* don't enter PS if TX frames are pending */
		if (drv_tx_frames_pending(local)) {
			mod_timer(&local->dynamic_ps_timer, jiffies +
				  msecs_to_jiffies(
				  local->hw.conf.dynamic_ps_timeout));
			return;
		}

		/*
		 * transmission can be stopped by others which leads to
		 * dynamic_ps_timer expiry. Postpone the ps timer if it
		 * is not the actual idle state.
		 */
		spin_lock_irqsave(&local->queue_stop_reason_lock, flags);
		for (q = 0; q < local->hw.queues; q++) {
			if (local->queue_stop_reasons[q]) {
				spin_unlock_irqrestore(&local->queue_stop_reason_lock,
						       flags);
				mod_timer(&local->dynamic_ps_timer, jiffies +
					  msecs_to_jiffies(
					  local->hw.conf.dynamic_ps_timeout));
				return;
			}
		}
		spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);
	}

	if (ieee80211_hw_check(&local->hw, PS_NULLFUNC_STACK) &&
	    !(ifmgd->flags & IEEE80211_STA_NULLFUNC_ACKED)) {
		if (drv_tx_frames_pending(local)) {
			mod_timer(&local->dynamic_ps_timer, jiffies +
				  msecs_to_jiffies(
				  local->hw.conf.dynamic_ps_timeout));
		} else {
			ieee80211_send_nullfunc(local, sdata, true);
			/* Flush to get the tx status of nullfunc frame */
			ieee80211_flush_queues(local, sdata, false);
		}
	}

	if (!(ieee80211_hw_check(&local->hw, REPORTS_TX_ACK_STATUS) &&
	      ieee80211_hw_check(&local->hw, PS_NULLFUNC_STACK)) ||
	    (ifmgd->flags & IEEE80211_STA_NULLFUNC_ACKED)) {
		ifmgd->flags &= ~IEEE80211_STA_NULLFUNC_ACKED;
		local->hw.conf.flags |= IEEE80211_CONF_PS;
		ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_PS);
	}
}

void ieee80211_dynamic_ps_timer(unsigned long data)
{
	struct ieee80211_local *local = (void *) data;

	ieee80211_queue_work(&local->hw, &local->dynamic_ps_enable_work);
}

void ieee80211_dfs_cac_timer_work(struct work_struct *work)
{
	struct delayed_work *delayed_work =
		container_of(work, struct delayed_work, work);
	struct ieee80211_sub_if_data *sdata =
		container_of(delayed_work, struct ieee80211_sub_if_data,
			     dfs_cac_timer_work);
	struct cfg80211_chan_def chandef = sdata->vif.bss_conf.chandef;

	mutex_lock(&sdata->local->mtx);
	if (sdata->wdev.cac_started) {
		ieee80211_vif_release_channel(sdata);
		cfg80211_cac_event(sdata->dev, &chandef,
				   NL80211_RADAR_CAC_FINISHED,
				   GFP_KERNEL);
	}
	mutex_unlock(&sdata->local->mtx);
}

static bool
__ieee80211_sta_handle_tspec_ac_params(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	bool ret = false;
	int ac;

	if (local->hw.queues < IEEE80211_NUM_ACS)
		return false;

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
		struct ieee80211_sta_tx_tspec *tx_tspec = &ifmgd->tx_tspec[ac];
		int non_acm_ac;
		unsigned long now = jiffies;

		if (tx_tspec->action == TX_TSPEC_ACTION_NONE &&
		    tx_tspec->admitted_time &&
		    time_after(now, tx_tspec->time_slice_start + HZ)) {
			tx_tspec->consumed_tx_time = 0;
			tx_tspec->time_slice_start = now;

			if (tx_tspec->downgraded)
				tx_tspec->action =
					TX_TSPEC_ACTION_STOP_DOWNGRADE;
		}

		switch (tx_tspec->action) {
		case TX_TSPEC_ACTION_STOP_DOWNGRADE:
			/* take the original parameters */
			if (drv_conf_tx(local, sdata, ac, &sdata->tx_conf[ac]))
				sdata_err(sdata,
					  "failed to set TX queue parameters for queue %d\n",
					  ac);
			tx_tspec->action = TX_TSPEC_ACTION_NONE;
			tx_tspec->downgraded = false;
			ret = true;
			break;
		case TX_TSPEC_ACTION_DOWNGRADE:
			if (time_after(now, tx_tspec->time_slice_start + HZ)) {
				tx_tspec->action = TX_TSPEC_ACTION_NONE;
				ret = true;
				break;
			}
			/* downgrade next lower non-ACM AC */
			for (non_acm_ac = ac + 1;
			     non_acm_ac < IEEE80211_NUM_ACS;
			     non_acm_ac++)
				if (!(sdata->wmm_acm & BIT(7 - 2 * non_acm_ac)))
					break;
			/* The loop will result in using BK even if it requires
			 * admission control, such configuration makes no sense
			 * and we have to transmit somehow - the AC selection
			 * does the same thing.
			 */
			if (drv_conf_tx(local, sdata, ac,
					&sdata->tx_conf[non_acm_ac]))
				sdata_err(sdata,
					  "failed to set TX queue parameters for queue %d\n",
					  ac);
			tx_tspec->action = TX_TSPEC_ACTION_NONE;
			ret = true;
			schedule_delayed_work(&ifmgd->tx_tspec_wk,
				tx_tspec->time_slice_start + HZ - now + 1);
			break;
		case TX_TSPEC_ACTION_NONE:
			/* nothing now */
			break;
		}
	}

	return ret;
}

void ieee80211_sta_handle_tspec_ac_params(struct ieee80211_sub_if_data *sdata)
{
	if (__ieee80211_sta_handle_tspec_ac_params(sdata))
		ieee80211_bss_info_change_notify(sdata, BSS_CHANGED_QOS);
}

static void ieee80211_sta_handle_tspec_ac_params_wk(struct work_struct *work)
{
	struct ieee80211_sub_if_data *sdata;

	sdata = container_of(work, struct ieee80211_sub_if_data,
			     u.mgd.tx_tspec_wk.work);
	ieee80211_sta_handle_tspec_ac_params(sdata);
}

/* MLME */
static bool ieee80211_sta_wmm_params(struct ieee80211_local *local,
				     struct ieee80211_sub_if_data *sdata,
				     const u8 *wmm_param, size_t wmm_param_len)
{
	struct ieee80211_tx_queue_params params[IEEE80211_NUM_ACS];
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	size_t left;
	int count, ac;
	const u8 *pos;
	u8 uapsd_queues = 0;

	if (!local->ops->conf_tx)
		return false;

	if (local->hw.queues < IEEE80211_NUM_ACS)
		return false;

	if (!wmm_param)
		return false;

	if (wmm_param_len < 8 || wmm_param[5] /* version */ != 1)
		return false;

	if (ifmgd->flags & IEEE80211_STA_UAPSD_ENABLED)
		uapsd_queues = ifmgd->uapsd_queues;

	count = wmm_param[6] & 0x0f;
	if (count == ifmgd->wmm_last_param_set)
		return false;
	ifmgd->wmm_last_param_set = count;

	pos = wmm_param + 8;
	left = wmm_param_len - 8;

	memset(&params, 0, sizeof(params));

	sdata->wmm_acm = 0;
	for (; left >= 4; left -= 4, pos += 4) {
		int aci = (pos[0] >> 5) & 0x03;
		int acm = (pos[0] >> 4) & 0x01;
		bool uapsd = false;

		switch (aci) {
		case 1: /* AC_BK */
			ac = IEEE80211_AC_BK;
			if (acm)
				sdata->wmm_acm |= BIT(1) | BIT(2); /* BK/- */
			if (uapsd_queues & IEEE80211_WMM_IE_STA_QOSINFO_AC_BK)
				uapsd = true;
			break;
		case 2: /* AC_VI */
			ac = IEEE80211_AC_VI;
			if (acm)
				sdata->wmm_acm |= BIT(4) | BIT(5); /* CL/VI */
			if (uapsd_queues & IEEE80211_WMM_IE_STA_QOSINFO_AC_VI)
				uapsd = true;
			break;
		case 3: /* AC_VO */
			ac = IEEE80211_AC_VO;
			if (acm)
				sdata->wmm_acm |= BIT(6) | BIT(7); /* VO/NC */
			if (uapsd_queues & IEEE80211_WMM_IE_STA_QOSINFO_AC_VO)
				uapsd = true;
			break;
		case 0: /* AC_BE */
		default:
			ac = IEEE80211_AC_BE;
			if (acm)
				sdata->wmm_acm |= BIT(0) | BIT(3); /* BE/EE */
			if (uapsd_queues & IEEE80211_WMM_IE_STA_QOSINFO_AC_BE)
				uapsd = true;
			break;
		}

		params[ac].aifs = pos[0] & 0x0f;

		if (params[ac].aifs < 2) {
			sdata_info(sdata,
				   "AP has invalid WMM params (AIFSN=%d for ACI %d), will use 2\n",
				   params[ac].aifs, aci);
			params[ac].aifs = 2;
		}
		params[ac].cw_max = ecw2cw((pos[1] & 0xf0) >> 4);
		params[ac].cw_min = ecw2cw(pos[1] & 0x0f);
		params[ac].txop = get_unaligned_le16(pos + 2);
		params[ac].acm = acm;
		params[ac].uapsd = uapsd;

		if (params[ac].cw_min == 0 ||
		    params[ac].cw_min > params[ac].cw_max) {
			sdata_info(sdata,
				   "AP has invalid WMM params (CWmin/max=%d/%d for ACI %d), using defaults\n",
				   params[ac].cw_min, params[ac].cw_max, aci);
			return false;
		}
	}

	/* WMM specification requires all 4 ACIs. */
	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
		if (params[ac].cw_min == 0) {
			sdata_info(sdata,
				   "AP has invalid WMM params (missing AC %d), using defaults\n",
				   ac);
			return false;
		}
	}

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
		mlme_dbg(sdata,
			 "WMM AC=%d acm=%d aifs=%d cWmin=%d cWmax=%d txop=%d uapsd=%d, downgraded=%d\n",
			 ac, params[ac].acm,
			 params[ac].aifs, params[ac].cw_min, params[ac].cw_max,
			 params[ac].txop, params[ac].uapsd,
			 ifmgd->tx_tspec[ac].downgraded);
		sdata->tx_conf[ac] = params[ac];
		if (!ifmgd->tx_tspec[ac].downgraded &&
		    drv_conf_tx(local, sdata, ac, &params[ac]))
			sdata_err(sdata,
				  "failed to set TX queue parameters for AC %d\n",
				  ac);
	}

	/* enable WMM or activate new settings */
	sdata->vif.bss_conf.qos = true;
	return true;
}

static void __ieee80211_stop_poll(struct ieee80211_sub_if_data *sdata)
{
	lockdep_assert_held(&sdata->local->mtx);

	sdata->u.mgd.flags &= ~IEEE80211_STA_CONNECTION_POLL;
	ieee80211_run_deferred_scan(sdata->local);
}

static void ieee80211_stop_poll(struct ieee80211_sub_if_data *sdata)
{
	mutex_lock(&sdata->local->mtx);
	__ieee80211_stop_poll(sdata);
	mutex_unlock(&sdata->local->mtx);
}

static u32 ieee80211_handle_bss_capability(struct ieee80211_sub_if_data *sdata,
					   u16 capab, bool erp_valid, u8 erp)
{
	struct ieee80211_bss_conf *bss_conf = &sdata->vif.bss_conf;
	u32 changed = 0;
	bool use_protection;
	bool use_short_preamble;
	bool use_short_slot;

	if (erp_valid) {
		use_protection = (erp & WLAN_ERP_USE_PROTECTION) != 0;
		use_short_preamble = (erp & WLAN_ERP_BARKER_PREAMBLE) == 0;
	} else {
		use_protection = false;
		use_short_preamble = !!(capab & WLAN_CAPABILITY_SHORT_PREAMBLE);
	}

	use_short_slot = !!(capab & WLAN_CAPABILITY_SHORT_SLOT_TIME);
	if (ieee80211_get_sdata_band(sdata) == IEEE80211_BAND_5GHZ)
		use_short_slot = true;

	if (use_protection != bss_conf->use_cts_prot) {
		bss_conf->use_cts_prot = use_protection;
		changed |= BSS_CHANGED_ERP_CTS_PROT;
	}

	if (use_short_preamble != bss_conf->use_short_preamble) {
		bss_conf->use_short_preamble = use_short_preamble;
		changed |= BSS_CHANGED_ERP_PREAMBLE;
	}

	if (use_short_slot != bss_conf->use_short_slot) {
		bss_conf->use_short_slot = use_short_slot;
		changed |= BSS_CHANGED_ERP_SLOT;
	}

	return changed;
}

static void ieee80211_set_associated(struct ieee80211_sub_if_data *sdata,
				     struct cfg80211_bss *cbss,
				     u32 bss_info_changed)
{
	struct ieee80211_bss *bss = (void *)cbss->priv;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_bss_conf *bss_conf = &sdata->vif.bss_conf;

	bss_info_changed |= BSS_CHANGED_ASSOC;
	bss_info_changed |= ieee80211_handle_bss_capability(sdata,
		bss_conf->assoc_capability, bss->has_erp_value, bss->erp_value);

	sdata->u.mgd.beacon_timeout = usecs_to_jiffies(ieee80211_tu_to_usec(
		beacon_loss_count * bss_conf->beacon_int));

	sdata->u.mgd.associated = cbss;
	memcpy(sdata->u.mgd.bssid, cbss->bssid, ETH_ALEN);

	sdata->u.mgd.flags |= IEEE80211_STA_RESET_SIGNAL_AVE;

	if (sdata->vif.p2p) {
		const struct cfg80211_bss_ies *ies;

		rcu_read_lock();
		ies = rcu_dereference(cbss->ies);
		if (ies) {
			int ret;

			ret = cfg80211_get_p2p_attr(
					ies->data, ies->len,
					IEEE80211_P2P_ATTR_ABSENCE_NOTICE,
					(u8 *) &bss_conf->p2p_noa_attr,
					sizeof(bss_conf->p2p_noa_attr));
			if (ret >= 2) {
				sdata->u.mgd.p2p_noa_index =
					bss_conf->p2p_noa_attr.index;
				bss_info_changed |= BSS_CHANGED_P2P_PS;
			}
		}
		rcu_read_unlock();
	}

	/* just to be sure */
	ieee80211_stop_poll(sdata);

	ieee80211_led_assoc(local, 1);

	if (sdata->u.mgd.have_beacon) {
		/*
		 * If the AP is buggy we may get here with no DTIM period
		 * known, so assume it's 1 which is the only safe assumption
		 * in that case, although if the TIM IE is broken powersave
		 * probably just won't work at all.
		 */
		bss_conf->dtim_period = sdata->u.mgd.dtim_period ?: 1;
		bss_conf->beacon_rate = bss->beacon_rate;
		bss_info_changed |= BSS_CHANGED_BEACON_INFO;
	} else {
		bss_conf->beacon_rate = NULL;
		bss_conf->dtim_period = 0;
	}

	bss_conf->assoc = 1;

	/* Tell the driver to monitor connection quality (if supported) */
	if (sdata->vif.driver_flags & IEEE80211_VIF_SUPPORTS_CQM_RSSI &&
	    bss_conf->cqm_rssi_thold)
		bss_info_changed |= BSS_CHANGED_CQM;

	/* Enable ARP filtering */
	if (bss_conf->arp_addr_cnt)
		bss_info_changed |= BSS_CHANGED_ARP_FILTER;

	ieee80211_bss_info_change_notify(sdata, bss_info_changed);

	mutex_lock(&local->iflist_mtx);
	ieee80211_recalc_ps(local);
	mutex_unlock(&local->iflist_mtx);

	ieee80211_recalc_smps(sdata);
	ieee80211_recalc_ps_vif(sdata);

	netif_carrier_on(sdata->dev);
}

static void ieee80211_set_disassoc(struct ieee80211_sub_if_data *sdata,
				   u16 stype, u16 reason, bool tx,
				   u8 *frame_buf)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_local *local = sdata->local;
	u32 changed = 0;

	sdata_assert_lock(sdata);

	if (WARN_ON_ONCE(tx && !frame_buf))
		return;

	if (WARN_ON(!ifmgd->associated))
		return;

	ieee80211_stop_poll(sdata);

	ifmgd->associated = NULL;
	netif_carrier_off(sdata->dev);

	/*
	 * if we want to get out of ps before disassoc (why?) we have
	 * to do it before sending disassoc, as otherwise the null-packet
	 * won't be valid.
	 */
	if (local->hw.conf.flags & IEEE80211_CONF_PS) {
		local->hw.conf.flags &= ~IEEE80211_CONF_PS;
		ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_PS);
	}
	local->ps_sdata = NULL;

	/* disable per-vif ps */
	ieee80211_recalc_ps_vif(sdata);

	/* make sure ongoing transmission finishes */
	synchronize_net();

	/*
	 * drop any frame before deauth/disassoc, this can be data or
	 * management frame. Since we are disconnecting, we should not
	 * insist sending these frames which can take time and delay
	 * the disconnection and possible the roaming.
	 */
	if (tx)
		ieee80211_flush_queues(local, sdata, true);

	/* deauthenticate/disassociate now */
	if (tx || frame_buf)
		ieee80211_send_deauth_disassoc(sdata, ifmgd->bssid, stype,
					       reason, tx, frame_buf);

	/* flush out frame - make sure the deauth was actually sent */
	if (tx)
		ieee80211_flush_queues(local, sdata, false);

	/* clear bssid only after building the needed mgmt frames */
	eth_zero_addr(ifmgd->bssid);

	/* remove AP and TDLS peers */
	sta_info_flush(sdata);

	/* finally reset all BSS / config parameters */
	changed |= ieee80211_reset_erp_info(sdata);

	ieee80211_led_assoc(local, 0);
	changed |= BSS_CHANGED_ASSOC;
	sdata->vif.bss_conf.assoc = false;

	ifmgd->p2p_noa_index = -1;
	memset(&sdata->vif.bss_conf.p2p_noa_attr, 0,
	       sizeof(sdata->vif.bss_conf.p2p_noa_attr));

	/* on the next assoc, re-program HT/VHT parameters */
	memset(&ifmgd->ht_capa, 0, sizeof(ifmgd->ht_capa));
	memset(&ifmgd->ht_capa_mask, 0, sizeof(ifmgd->ht_capa_mask));
	memset(&ifmgd->vht_capa, 0, sizeof(ifmgd->vht_capa));
	memset(&ifmgd->vht_capa_mask, 0, sizeof(ifmgd->vht_capa_mask));
	sdata->flags &= ~IEEE80211_SDATA_MU_MIMO_OWNER;

	sdata->ap_power_level = IEEE80211_UNSET_POWER_LEVEL;

	del_timer_sync(&local->dynamic_ps_timer);
	cancel_work_sync(&local->dynamic_ps_enable_work);

	/* Disable ARP filtering */
	if (sdata->vif.bss_conf.arp_addr_cnt)
		changed |= BSS_CHANGED_ARP_FILTER;

	sdata->vif.bss_conf.qos = false;
	changed |= BSS_CHANGED_QOS;

	/* The BSSID (not really interesting) and HT changed */
	changed |= BSS_CHANGED_BSSID | BSS_CHANGED_HT;
	ieee80211_bss_info_change_notify(sdata, changed);

	/* disassociated - set to defaults now */
	ieee80211_set_wmm_default(sdata, false, false);

	del_timer_sync(&sdata->u.mgd.conn_mon_timer);
	del_timer_sync(&sdata->u.mgd.bcn_mon_timer);
	del_timer_sync(&sdata->u.mgd.timer);
	del_timer_sync(&sdata->u.mgd.chswitch_timer);

	sdata->vif.bss_conf.dtim_period = 0;
	sdata->vif.bss_conf.beacon_rate = NULL;

	ifmgd->have_beacon = false;

	ifmgd->flags = 0;
	mutex_lock(&local->mtx);
	ieee80211_vif_release_channel(sdata);

	sdata->vif.csa_active = false;
	ifmgd->csa_waiting_bcn = false;
	ifmgd->csa_ignored_same_chan = false;
	if (sdata->csa_block_tx) {
		ieee80211_wake_vif_queues(local, sdata,
					  IEEE80211_QUEUE_STOP_REASON_CSA);
		sdata->csa_block_tx = false;
	}
	mutex_unlock(&local->mtx);

	/* existing TX TSPEC sessions no longer exist */
	memset(ifmgd->tx_tspec, 0, sizeof(ifmgd->tx_tspec));
	cancel_delayed_work_sync(&ifmgd->tx_tspec_wk);

	sdata->encrypt_headroom = IEEE80211_ENCRYPT_HEADROOM;
}

void ieee80211_sta_rx_notify(struct ieee80211_sub_if_data *sdata,
			     struct ieee80211_hdr *hdr)
{
	/*
	 * We can postpone the mgd.timer whenever receiving unicast frames
	 * from AP because we know that the connection is working both ways
	 * at that time. But multicast frames (and hence also beacons) must
	 * be ignored here, because we need to trigger the timer during
	 * data idle periods for sending the periodic probe request to the
	 * AP we're connected to.
	 */
	if (is_multicast_ether_addr(hdr->addr1))
		return;

	ieee80211_sta_reset_conn_monitor(sdata);
}

static void ieee80211_reset_ap_probe(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_local *local = sdata->local;

	mutex_lock(&local->mtx);
	if (!(ifmgd->flags & IEEE80211_STA_CONNECTION_POLL))
		goto out;

	__ieee80211_stop_poll(sdata);

	mutex_lock(&local->iflist_mtx);
	ieee80211_recalc_ps(local);
	mutex_unlock(&local->iflist_mtx);

	if (ieee80211_hw_check(&sdata->local->hw, CONNECTION_MONITOR))
		goto out;

	/*
	 * We've received a probe response, but are not sure whether
	 * we have or will be receiving any beacons or data, so let's
	 * schedule the timers again, just in case.
	 */
	ieee80211_sta_reset_beacon_monitor(sdata);

	mod_timer(&ifmgd->conn_mon_timer,
		  round_jiffies_up(jiffies +
				   IEEE80211_CONNECTION_IDLE_TIME));
out:
	mutex_unlock(&local->mtx);
}

static void ieee80211_sta_tx_wmm_ac_notify(struct ieee80211_sub_if_data *sdata,
					   struct ieee80211_hdr *hdr,
					   u16 tx_time)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	u16 tid = *ieee80211_get_qos_ctl(hdr) & IEEE80211_QOS_CTL_TID_MASK;
	int ac = ieee80211_ac_from_tid(tid);
	struct ieee80211_sta_tx_tspec *tx_tspec = &ifmgd->tx_tspec[ac];
	unsigned long now = jiffies;

	if (likely(!tx_tspec->admitted_time))
		return;

	if (time_after(now, tx_tspec->time_slice_start + HZ)) {
		tx_tspec->consumed_tx_time = 0;
		tx_tspec->time_slice_start = now;

		if (tx_tspec->downgraded) {
			tx_tspec->action = TX_TSPEC_ACTION_STOP_DOWNGRADE;
			schedule_delayed_work(&ifmgd->tx_tspec_wk, 0);
		}
	}

	if (tx_tspec->downgraded)
		return;

	tx_tspec->consumed_tx_time += tx_time;

	if (tx_tspec->consumed_tx_time >= tx_tspec->admitted_time) {
		tx_tspec->downgraded = true;
		tx_tspec->action = TX_TSPEC_ACTION_DOWNGRADE;
		schedule_delayed_work(&ifmgd->tx_tspec_wk, 0);
	}
}

void ieee80211_sta_tx_notify(struct ieee80211_sub_if_data *sdata,
			     struct ieee80211_hdr *hdr, bool ack, u16 tx_time)
{
	ieee80211_sta_tx_wmm_ac_notify(sdata, hdr, tx_time);

	if (!ieee80211_is_data(hdr->frame_control))
	    return;

	if (ieee80211_is_any_nullfunc(hdr->frame_control) &&
	    sdata->u.mgd.probe_send_count > 0) {
		if (ack)
			ieee80211_sta_reset_conn_monitor(sdata);
		else
			sdata->u.mgd.nullfunc_failed = true;
		ieee80211_queue_work(&sdata->local->hw, &sdata->work);
		return;
	}

	if (ack)
		ieee80211_sta_reset_conn_monitor(sdata);
}

static void ieee80211_mgd_probe_ap_send(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	const u8 *ssid;
	u8 *dst = ifmgd->associated->bssid;
	u8 unicast_limit = max(1, max_probe_tries - 3);

	/*
	 * Try sending broadcast probe requests for the last three
	 * probe requests after the first ones failed since some
	 * buggy APs only support broadcast probe requests.
	 */
	if (ifmgd->probe_send_count >= unicast_limit)
		dst = NULL;

	/*
	 * When the hardware reports an accurate Tx ACK status, it's
	 * better to send a nullfunc frame instead of a probe request,
	 * as it will kick us off the AP quickly if we aren't associated
	 * anymore. The timeout will be reset if the frame is ACKed by
	 * the AP.
	 */
	ifmgd->probe_send_count++;

	if (ieee80211_hw_check(&sdata->local->hw, REPORTS_TX_ACK_STATUS)) {
		ifmgd->nullfunc_failed = false;
		ieee80211_send_nullfunc(sdata->local, sdata, false);
	} else {
		int ssid_len;

		rcu_read_lock();
		ssid = ieee80211_bss_get_ie(ifmgd->associated, WLAN_EID_SSID);
		if (WARN_ON_ONCE(ssid == NULL))
			ssid_len = 0;
		else
			ssid_len = ssid[1];

		ieee80211_send_probe_req(sdata, sdata->vif.addr, dst,
					 ssid + 2, ssid_len, NULL,
					 0, (u32) -1, true, 0,
					 ifmgd->associated->channel, false);
		rcu_read_unlock();
	}

	ifmgd->probe_timeout = jiffies + msecs_to_jiffies(probe_wait_ms);
	run_again(sdata, ifmgd->probe_timeout);
}

static void ieee80211_mgd_probe_ap(struct ieee80211_sub_if_data *sdata,
				   bool beacon)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	bool already = false;

	if (!ieee80211_sdata_running(sdata))
		return;

	sdata_lock(sdata);

	if (!ifmgd->associated)
		goto out;

	mutex_lock(&sdata->local->mtx);

	if (sdata->local->tmp_channel || sdata->local->scanning) {
		mutex_unlock(&sdata->local->mtx);
		goto out;
	}

	if (beacon) {
		mlme_dbg_ratelimited(sdata,
				     "detected beacon loss from AP (missed %d beacons) - probing\n",
				     beacon_loss_count);

		ieee80211_cqm_beacon_loss_notify(&sdata->vif, GFP_KERNEL);
	}

	/*
	 * The driver/our work has already reported this event or the
	 * connection monitoring has kicked in and we have already sent
	 * a probe request. Or maybe the AP died and the driver keeps
	 * reporting until we disassociate...
	 *
	 * In either case we have to ignore the current call to this
	 * function (except for setting the correct probe reason bit)
	 * because otherwise we would reset the timer every time and
	 * never check whether we received a probe response!
	 */
	if (ifmgd->flags & IEEE80211_STA_CONNECTION_POLL)
		already = true;

	ifmgd->flags |= IEEE80211_STA_CONNECTION_POLL;

	mutex_unlock(&sdata->local->mtx);

	if (already)
		goto out;

	mutex_lock(&sdata->local->iflist_mtx);
	ieee80211_recalc_ps(sdata->local);
	mutex_unlock(&sdata->local->iflist_mtx);

	ifmgd->probe_send_count = 0;
	ieee80211_mgd_probe_ap_send(sdata);
 out:
	sdata_unlock(sdata);
}

struct sk_buff *ieee80211_ap_probereq_get(struct ieee80211_hw *hw,
					  struct ieee80211_vif *vif)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct cfg80211_bss *cbss;
	struct sk_buff *skb;
	const u8 *ssid;
	int ssid_len;

	if (WARN_ON(sdata->vif.type != NL80211_IFTYPE_STATION))
		return NULL;

	sdata_assert_lock(sdata);

	if (ifmgd->associated)
		cbss = ifmgd->associated;
	else if (ifmgd->auth_data)
		cbss = ifmgd->auth_data->bss;
	else if (ifmgd->assoc_data)
		cbss = ifmgd->assoc_data->bss;
	else
		return NULL;

	rcu_read_lock();
	ssid = ieee80211_bss_get_ie(cbss, WLAN_EID_SSID);
	if (WARN_ONCE(!ssid || ssid[1] > IEEE80211_MAX_SSID_LEN,
		      "invalid SSID element (len=%d)", ssid ? ssid[1] : -1))
		ssid_len = 0;
	else
		ssid_len = ssid[1];

	skb = ieee80211_build_probe_req(sdata, sdata->vif.addr, cbss->bssid,
					(u32) -1, cbss->channel,
					ssid + 2, ssid_len,
					NULL, 0, true);
	rcu_read_unlock();

	return skb;
}
EXPORT_SYMBOL(ieee80211_ap_probereq_get);

static void ieee80211_report_disconnect(struct ieee80211_sub_if_data *sdata,
					const u8 *buf, size_t len, bool tx,
					u16 reason)
{
	struct ieee80211_event event = {
		.type = MLME_EVENT,
		.u.mlme.data = tx ? DEAUTH_TX_EVENT : DEAUTH_RX_EVENT,
		.u.mlme.reason = reason,
	};

	if (tx)
		cfg80211_tx_mlme_mgmt(sdata->dev, buf, len);
	else
		cfg80211_rx_mlme_mgmt(sdata->dev, buf, len);

	drv_event_callback(sdata->local, sdata, &event);
}

static void __ieee80211_disconnect(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	u8 frame_buf[IEEE80211_DEAUTH_FRAME_LEN];

	sdata_lock(sdata);
	if (!ifmgd->associated) {
		sdata_unlock(sdata);
		return;
	}

	ieee80211_set_disassoc(sdata, IEEE80211_STYPE_DEAUTH,
			       WLAN_REASON_DISASSOC_DUE_TO_INACTIVITY,
			       true, frame_buf);
	mutex_lock(&local->mtx);
	sdata->vif.csa_active = false;
	ifmgd->csa_waiting_bcn = false;
	if (sdata->csa_block_tx) {
		ieee80211_wake_vif_queues(local, sdata,
					  IEEE80211_QUEUE_STOP_REASON_CSA);
		sdata->csa_block_tx = false;
	}
	mutex_unlock(&local->mtx);

	ieee80211_report_disconnect(sdata, frame_buf, sizeof(frame_buf), true,
				    WLAN_REASON_DISASSOC_DUE_TO_INACTIVITY);

	sdata_unlock(sdata);
}

static void ieee80211_beacon_connection_loss_work(struct work_struct *work)
{
	struct ieee80211_sub_if_data *sdata =
		container_of(work, struct ieee80211_sub_if_data,
			     u.mgd.beacon_connection_loss_work);
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	if (ifmgd->associated)
		ifmgd->beacon_loss_count++;

	if (ifmgd->connection_loss) {
		sdata_info(sdata, "Connection to AP %pM lost\n",
			   ifmgd->bssid);
		__ieee80211_disconnect(sdata);
	} else {
		ieee80211_mgd_probe_ap(sdata, true);
	}
}

static void ieee80211_csa_connection_drop_work(struct work_struct *work)
{
	struct ieee80211_sub_if_data *sdata =
		container_of(work, struct ieee80211_sub_if_data,
			     u.mgd.csa_connection_drop_work);

	__ieee80211_disconnect(sdata);
}

void ieee80211_beacon_loss(struct ieee80211_vif *vif)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct ieee80211_hw *hw = &sdata->local->hw;

	trace_api_beacon_loss(sdata);

	sdata->u.mgd.connection_loss = false;
	ieee80211_queue_work(hw, &sdata->u.mgd.beacon_connection_loss_work);
}
EXPORT_SYMBOL(ieee80211_beacon_loss);

void ieee80211_connection_loss(struct ieee80211_vif *vif)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct ieee80211_hw *hw = &sdata->local->hw;

	trace_api_connection_loss(sdata);

	sdata->u.mgd.connection_loss = true;
	ieee80211_queue_work(hw, &sdata->u.mgd.beacon_connection_loss_work);
}
EXPORT_SYMBOL(ieee80211_connection_loss);


static void ieee80211_destroy_auth_data(struct ieee80211_sub_if_data *sdata,
					bool assoc)
{
	struct ieee80211_mgd_auth_data *auth_data = sdata->u.mgd.auth_data;

	sdata_assert_lock(sdata);

	if (!assoc) {
		/*
		 * we are not authenticated yet, the only timer that could be
		 * running is the timeout for the authentication response which
		 * which is not relevant anymore.
		 */
		del_timer_sync(&sdata->u.mgd.timer);
		sta_info_destroy_addr(sdata, auth_data->bss->bssid);

		eth_zero_addr(sdata->u.mgd.bssid);
		ieee80211_bss_info_change_notify(sdata, BSS_CHANGED_BSSID);
		sdata->u.mgd.flags = 0;
		mutex_lock(&sdata->local->mtx);
		ieee80211_vif_release_channel(sdata);
		mutex_unlock(&sdata->local->mtx);
	}

	cfg80211_put_bss(sdata->local->hw.wiphy, auth_data->bss);
	kfree(auth_data);
	sdata->u.mgd.auth_data = NULL;
}

static void ieee80211_destroy_assoc_data(struct ieee80211_sub_if_data *sdata,
					 bool assoc, bool abandon)
{
	struct ieee80211_mgd_assoc_data *assoc_data = sdata->u.mgd.assoc_data;

	sdata_assert_lock(sdata);

	if (!assoc) {
		/*
		 * we are not associated yet, the only timer that could be
		 * running is the timeout for the association response which
		 * which is not relevant anymore.
		 */
		del_timer_sync(&sdata->u.mgd.timer);
		sta_info_destroy_addr(sdata, assoc_data->bss->bssid);

		eth_zero_addr(sdata->u.mgd.bssid);
		ieee80211_bss_info_change_notify(sdata, BSS_CHANGED_BSSID);
		sdata->u.mgd.flags = 0;
		sdata->flags &= ~IEEE80211_SDATA_MU_MIMO_OWNER;
		mutex_lock(&sdata->local->mtx);
		ieee80211_vif_release_channel(sdata);
		mutex_unlock(&sdata->local->mtx);

		if (abandon)
			cfg80211_abandon_assoc(sdata->dev, assoc_data->bss);
	}

	kfree(assoc_data);
	sdata->u.mgd.assoc_data = NULL;
}

static void ieee80211_auth_challenge(struct ieee80211_sub_if_data *sdata,
				     struct ieee80211_mgmt *mgmt, size_t len)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_mgd_auth_data *auth_data = sdata->u.mgd.auth_data;
	u8 *pos;
	struct ieee802_11_elems elems;
	u32 tx_flags = 0;

	pos = mgmt->u.auth.variable;
	ieee802_11_parse_elems(pos, len - (pos - (u8 *) mgmt), false, &elems);
	if (!elems.challenge)
		return;
	auth_data->expected_transaction = 4;
	drv_mgd_prepare_tx(sdata->local, sdata);
	if (ieee80211_hw_check(&local->hw, REPORTS_TX_ACK_STATUS))
		tx_flags = IEEE80211_TX_CTL_REQ_TX_STATUS |
			   IEEE80211_TX_INTFL_MLME_CONN_TX;
	ieee80211_send_auth(sdata, 3, auth_data->algorithm, 0,
			    elems.challenge - 2, elems.challenge_len + 2,
			    auth_data->bss->bssid, auth_data->bss->bssid,
			    auth_data->key, auth_data->key_len,
			    auth_data->key_idx, tx_flags);
}

static void ieee80211_rx_mgmt_auth(struct ieee80211_sub_if_data *sdata,
				   struct ieee80211_mgmt *mgmt, size_t len)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	u8 bssid[ETH_ALEN];
	u16 auth_alg, auth_transaction, status_code;
	struct sta_info *sta;
	struct ieee80211_event event = {
		.type = MLME_EVENT,
		.u.mlme.data = AUTH_EVENT,
	};

	sdata_assert_lock(sdata);

	if (len < 24 + 6)
		return;

	if (!ifmgd->auth_data || ifmgd->auth_data->done)
		return;

	memcpy(bssid, ifmgd->auth_data->bss->bssid, ETH_ALEN);

	if (!ether_addr_equal(bssid, mgmt->bssid))
		return;

	auth_alg = le16_to_cpu(mgmt->u.auth.auth_alg);
	auth_transaction = le16_to_cpu(mgmt->u.auth.auth_transaction);
	status_code = le16_to_cpu(mgmt->u.auth.status_code);

	if (sdata->vif.type == IEEE80211_IF_TYPE_IBSS) {
		/*
		 * IEEE 802.11 standard does not require authentication in IBSS
		 * networks and most implementations do not seem to use it.
		 * However, try to reply to authentication attempts if someone
		 * has actually implemented this.
		 */
		if (auth_alg != WLAN_AUTH_OPEN || auth_transaction != 1)
			return;
		ieee80211_send_auth(dev, ifsta, 2, NULL, 0, 0);
	}

	if (auth_alg != ifsta->auth_alg ||
	    auth_transaction != ifsta->auth_transaction)
		return;

	if (status_code != WLAN_STATUS_SUCCESS) {
		if (status_code == WLAN_STATUS_NOT_SUPPORTED_AUTH_ALG) {
			u8 algs[3];
			const int num_algs = ARRAY_SIZE(algs);
			int i, pos;
			algs[0] = algs[1] = algs[2] = 0xff;
			if (ifsta->auth_algs & IEEE80211_AUTH_ALG_OPEN)
				algs[0] = WLAN_AUTH_OPEN;
			if (ifsta->auth_algs & IEEE80211_AUTH_ALG_SHARED_KEY)
				algs[1] = WLAN_AUTH_SHARED_KEY;
			if (ifsta->auth_algs & IEEE80211_AUTH_ALG_LEAP)
				algs[2] = WLAN_AUTH_LEAP;
			if (ifsta->auth_alg == WLAN_AUTH_OPEN)
				pos = 0;
			else if (ifsta->auth_alg == WLAN_AUTH_SHARED_KEY)
				pos = 1;
			else
				pos = 2;
			for (i = 0; i < num_algs; i++) {
				pos++;
				if (pos >= num_algs)
					pos = 0;
				if (algs[pos] == ifsta->auth_alg ||
				    algs[pos] == 0xff)
					continue;
				if (algs[pos] == WLAN_AUTH_SHARED_KEY &&
				    !ieee80211_sta_wep_configured(dev))
					continue;
				ifsta->auth_alg = algs[pos];
				break;
			}
		}
		return;
	}

	switch (ifsta->auth_alg) {
	case WLAN_AUTH_OPEN:
	case WLAN_AUTH_LEAP:
		ieee80211_auth_completed(dev, ifsta);
		break;
	case WLAN_AUTH_SHARED_KEY:
		if (ifsta->auth_transaction == 4)
			ieee80211_auth_completed(dev, ifsta);
		else
			ieee80211_auth_challenge(dev, ifsta, mgmt, len);
		break;
	}
}


static void ieee80211_rx_mgmt_deauth(struct net_device *dev,
				     struct ieee80211_if_sta *ifsta,
				     struct ieee80211_mgmt *mgmt,
				     size_t len)
{
	u16 reason_code;
	DECLARE_MAC_BUF(mac);
	if (auth_alg != ifmgd->auth_data->algorithm ||
	    auth_transaction != ifmgd->auth_data->expected_transaction) {
		sdata_info(sdata, "%pM unexpected authentication state: alg %d (expected %d) transact %d (expected %d)\n",
			   mgmt->sa, auth_alg, ifmgd->auth_data->algorithm,
			   auth_transaction,
			   ifmgd->auth_data->expected_transaction);
		return;
	}

	if (status_code != WLAN_STATUS_SUCCESS) {
		sdata_info(sdata, "%pM denied authentication (status %d)\n",
			   mgmt->sa, status_code);
		ieee80211_destroy_auth_data(sdata, false);
		cfg80211_rx_mlme_mgmt(sdata->dev, (u8 *)mgmt, len);
		event.u.mlme.status = MLME_DENIED;
		event.u.mlme.reason = status_code;
		drv_event_callback(sdata->local, sdata, &event);
		return;
	}

	switch (ifmgd->auth_data->algorithm) {
	case WLAN_AUTH_OPEN:
	case WLAN_AUTH_LEAP:
	case WLAN_AUTH_FT:
	case WLAN_AUTH_SAE:
		break;
	case WLAN_AUTH_SHARED_KEY:
		if (ifmgd->auth_data->expected_transaction != 4) {
			ieee80211_auth_challenge(sdata, mgmt, len);
			/* need another frame */
			return;
		}
		break;
	default:
		WARN_ONCE(1, "invalid auth alg %d",
			  ifmgd->auth_data->algorithm);
		return;
	}

	event.u.mlme.status = MLME_SUCCESS;
	drv_event_callback(sdata->local, sdata, &event);
	sdata_info(sdata, "authenticated\n");
	ifmgd->auth_data->done = true;
	ifmgd->auth_data->timeout = jiffies + IEEE80211_AUTH_WAIT_ASSOC;
	ifmgd->auth_data->timeout_started = true;
	run_again(sdata, ifmgd->auth_data->timeout);

	if (ifmgd->auth_data->algorithm == WLAN_AUTH_SAE &&
	    ifmgd->auth_data->expected_transaction != 2) {
		/*
		 * Report auth frame to user space for processing since another
		 * round of Authentication frames is still needed.
		 */
		cfg80211_rx_mlme_mgmt(sdata->dev, (u8 *)mgmt, len);
		return;
	}

	/* move station state to auth */
	mutex_lock(&sdata->local->sta_mtx);
	sta = sta_info_get(sdata, bssid);
	if (!sta) {
		WARN_ONCE(1, "%s: STA %pM not found", sdata->name, bssid);
		goto out_err;
	}
	if (sta_info_move_state(sta, IEEE80211_STA_AUTH)) {
		sdata_info(sdata, "failed moving %pM to auth\n", bssid);
		goto out_err;
	}
	mutex_unlock(&sdata->local->sta_mtx);

	cfg80211_rx_mlme_mgmt(sdata->dev, (u8 *)mgmt, len);
	return;
 out_err:
	mutex_unlock(&sdata->local->sta_mtx);
	/* ignore frame -- wait for timeout */
}

#define case_WLAN(type) \
	case WLAN_REASON_##type: return #type

const char *ieee80211_get_reason_code_string(u16 reason_code)
{
	switch (reason_code) {
	case_WLAN(UNSPECIFIED);
	case_WLAN(PREV_AUTH_NOT_VALID);
	case_WLAN(DEAUTH_LEAVING);
	case_WLAN(DISASSOC_DUE_TO_INACTIVITY);
	case_WLAN(DISASSOC_AP_BUSY);
	case_WLAN(CLASS2_FRAME_FROM_NONAUTH_STA);
	case_WLAN(CLASS3_FRAME_FROM_NONASSOC_STA);
	case_WLAN(DISASSOC_STA_HAS_LEFT);
	case_WLAN(STA_REQ_ASSOC_WITHOUT_AUTH);
	case_WLAN(DISASSOC_BAD_POWER);
	case_WLAN(DISASSOC_BAD_SUPP_CHAN);
	case_WLAN(INVALID_IE);
	case_WLAN(MIC_FAILURE);
	case_WLAN(4WAY_HANDSHAKE_TIMEOUT);
	case_WLAN(GROUP_KEY_HANDSHAKE_TIMEOUT);
	case_WLAN(IE_DIFFERENT);
	case_WLAN(INVALID_GROUP_CIPHER);
	case_WLAN(INVALID_PAIRWISE_CIPHER);
	case_WLAN(INVALID_AKMP);
	case_WLAN(UNSUPP_RSN_VERSION);
	case_WLAN(INVALID_RSN_IE_CAP);
	case_WLAN(IEEE8021X_FAILED);
	case_WLAN(CIPHER_SUITE_REJECTED);
	case_WLAN(DISASSOC_UNSPECIFIED_QOS);
	case_WLAN(DISASSOC_QAP_NO_BANDWIDTH);
	case_WLAN(DISASSOC_LOW_ACK);
	case_WLAN(DISASSOC_QAP_EXCEED_TXOP);
	case_WLAN(QSTA_LEAVE_QBSS);
	case_WLAN(QSTA_NOT_USE);
	case_WLAN(QSTA_REQUIRE_SETUP);
	case_WLAN(QSTA_TIMEOUT);
	case_WLAN(QSTA_CIPHER_NOT_SUPP);
	case_WLAN(MESH_PEER_CANCELED);
	case_WLAN(MESH_MAX_PEERS);
	case_WLAN(MESH_CONFIG);
	case_WLAN(MESH_CLOSE);
	case_WLAN(MESH_MAX_RETRIES);
	case_WLAN(MESH_CONFIRM_TIMEOUT);
	case_WLAN(MESH_INVALID_GTK);
	case_WLAN(MESH_INCONSISTENT_PARAM);
	case_WLAN(MESH_INVALID_SECURITY);
	case_WLAN(MESH_PATH_ERROR);
	case_WLAN(MESH_PATH_NOFORWARD);
	case_WLAN(MESH_PATH_DEST_UNREACHABLE);
	case_WLAN(MAC_EXISTS_IN_MBSS);
	case_WLAN(MESH_CHAN_REGULATORY);
	case_WLAN(MESH_CHAN);
	default: return "<unknown>";
	}
}

static void ieee80211_rx_mgmt_deauth(struct ieee80211_sub_if_data *sdata,
				     struct ieee80211_mgmt *mgmt, size_t len)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	u16 reason_code = le16_to_cpu(mgmt->u.deauth.reason_code);

	sdata_assert_lock(sdata);

	if (len < 24 + 2)
		return;

	if (memcmp(ifsta->bssid, mgmt->sa, ETH_ALEN))
		return;

	reason_code = le16_to_cpu(mgmt->u.deauth.reason_code);

	if (ifsta->flags & IEEE80211_STA_AUTHENTICATED)
		printk(KERN_DEBUG "%s: deauthenticated\n", dev->name);

	if (ifsta->state == IEEE80211_AUTHENTICATE ||
	    ifsta->state == IEEE80211_ASSOCIATE ||
	    ifsta->state == IEEE80211_ASSOCIATED) {
		ifsta->state = IEEE80211_AUTHENTICATE;
		mod_timer(&ifsta->timer, jiffies +
				      IEEE80211_RETRY_AUTH_INTERVAL);
	}

	ieee80211_set_disassoc(dev, ifsta, 1);
	ifsta->flags &= ~IEEE80211_STA_AUTHENTICATED;
}


static void ieee80211_rx_mgmt_disassoc(struct net_device *dev,
				       struct ieee80211_if_sta *ifsta,
				       struct ieee80211_mgmt *mgmt,
				       size_t len)
{
	u16 reason_code;
	DECLARE_MAC_BUF(mac);
	if (!ether_addr_equal(mgmt->bssid, mgmt->sa)) {
		ieee80211_tdls_handle_disconnect(sdata, mgmt->sa, reason_code);
		return;
	}

	if (ifmgd->associated &&
	    ether_addr_equal(mgmt->bssid, ifmgd->associated->bssid)) {
		const u8 *bssid = ifmgd->associated->bssid;

		sdata_info(sdata, "deauthenticated from %pM (Reason: %u=%s)\n",
			   bssid, reason_code,
			   ieee80211_get_reason_code_string(reason_code));

		ieee80211_set_disassoc(sdata, 0, 0, false, NULL);

		ieee80211_report_disconnect(sdata, (u8 *)mgmt, len, false,
					    reason_code);
		return;
	}

	if (ifmgd->assoc_data &&
	    ether_addr_equal(mgmt->bssid, ifmgd->assoc_data->bss->bssid)) {
		const u8 *bssid = ifmgd->assoc_data->bss->bssid;

		sdata_info(sdata,
			   "deauthenticated from %pM while associating (Reason: %u=%s)\n",
			   bssid, reason_code,
			   ieee80211_get_reason_code_string(reason_code));

		ieee80211_destroy_assoc_data(sdata, false, true);

		cfg80211_rx_mlme_mgmt(sdata->dev, (u8 *)mgmt, len);
		return;
	}
}


static void ieee80211_rx_mgmt_disassoc(struct ieee80211_sub_if_data *sdata,
				       struct ieee80211_mgmt *mgmt, size_t len)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	u16 reason_code;

	sdata_assert_lock(sdata);

	if (len < 24 + 2)
		return;

	if (memcmp(ifsta->bssid, mgmt->sa, ETH_ALEN))
	if (!ifmgd->associated ||
	    !ether_addr_equal(mgmt->bssid, ifmgd->associated->bssid))
		return;

	reason_code = le16_to_cpu(mgmt->u.disassoc.reason_code);

	if (ifsta->flags & IEEE80211_STA_ASSOCIATED)
		printk(KERN_DEBUG "%s: disassociated\n", dev->name);

	if (ifsta->state == IEEE80211_ASSOCIATED) {
		ifsta->state = IEEE80211_ASSOCIATE;
		mod_timer(&ifsta->timer, jiffies +
				      IEEE80211_RETRY_AUTH_INTERVAL);
	}

	ieee80211_set_disassoc(dev, ifsta, 0);
}


static void ieee80211_rx_mgmt_assoc_resp(struct ieee80211_sub_if_data *sdata,
					 struct ieee80211_if_sta *ifsta,
					 struct ieee80211_mgmt *mgmt,
					 size_t len,
					 int reassoc)
{
	struct ieee80211_local *local = sdata->local;
	struct net_device *dev = sdata->dev;
	struct ieee80211_supported_band *sband;
	struct sta_info *sta;
	u64 rates, basic_rates;
	u16 capab_info, status_code, aid;
	struct ieee802_11_elems elems;
	struct ieee80211_bss_conf *bss_conf = &sdata->bss_conf;
	u8 *pos;
	int i, j;
	DECLARE_MAC_BUF(mac);
	bool have_higher_than_11mbit = false;

	/* AssocResp and ReassocResp have identical structure, so process both
	 * of them in this function. */

	if (ifsta->state != IEEE80211_ASSOCIATE)
		return;

	if (len < 24 + 6)
		return;

	if (memcmp(ifsta->bssid, mgmt->sa, ETH_ALEN) != 0)
		return;

	capab_info = le16_to_cpu(mgmt->u.assoc_resp.capab_info);
	status_code = le16_to_cpu(mgmt->u.assoc_resp.status_code);
	aid = le16_to_cpu(mgmt->u.assoc_resp.aid);

	printk(KERN_DEBUG "%s: RX %sssocResp from %s (capab=0x%x "
	       "status=%d aid=%d)\n",
	       dev->name, reassoc ? "Rea" : "A", print_mac(mac, mgmt->sa),
	       capab_info, status_code, (u16)(aid & ~(BIT(15) | BIT(14))));

	if (status_code != WLAN_STATUS_SUCCESS) {
		printk(KERN_DEBUG "%s: AP denied association (code=%d)\n",
		       dev->name, status_code);
		/* if this was a reassociation, ensure we try a "full"
		 * association next time. This works around some broken APs
		 * which do not correctly reject reassociation requests. */
		ifsta->flags &= ~IEEE80211_STA_PREV_BSSID_SET;
		return;
	}

	if ((aid & (BIT(15) | BIT(14))) != (BIT(15) | BIT(14)))
		printk(KERN_DEBUG "%s: invalid aid value %d; bits 15:14 not "
		       "set\n", dev->name, aid);
	aid &= ~(BIT(15) | BIT(14));

	pos = mgmt->u.assoc_resp.variable;
	ieee802_11_parse_elems(pos, len - (pos - (u8 *) mgmt), &elems);

	if (!elems.supp_rates) {
		printk(KERN_DEBUG "%s: no SuppRates element in AssocResp\n",
		       dev->name);
		return;
	}

	printk(KERN_DEBUG "%s: associated\n", dev->name);
	ifsta->aid = aid;
	ifsta->ap_capab = capab_info;

	kfree(ifsta->assocresp_ies);
	ifsta->assocresp_ies_len = len - (pos - (u8 *) mgmt);
	ifsta->assocresp_ies = kmalloc(ifsta->assocresp_ies_len, GFP_KERNEL);
	if (ifsta->assocresp_ies)
		memcpy(ifsta->assocresp_ies, pos, ifsta->assocresp_ies_len);

	rcu_read_lock();

	/* Add STA entry for the AP */
	sta = sta_info_get(local, ifsta->bssid);
	if (!sta) {
		struct ieee80211_sta_bss *bss;
		int err;

		sta = sta_info_alloc(sdata, ifsta->bssid, GFP_ATOMIC);
		if (!sta) {
			printk(KERN_DEBUG "%s: failed to alloc STA entry for"
			       " the AP\n", dev->name);
			rcu_read_unlock();
			return;
		}
		bss = ieee80211_rx_bss_get(dev, ifsta->bssid,
					   local->hw.conf.channel->center_freq,
					   ifsta->ssid, ifsta->ssid_len);
		if (bss) {
			sta->last_signal = bss->signal;
			sta->last_qual = bss->qual;
			sta->last_noise = bss->noise;
			ieee80211_rx_bss_put(local, bss);
		}

		err = sta_info_insert(sta);
		if (err) {
			printk(KERN_DEBUG "%s: failed to insert STA entry for"
			       " the AP (error %d)\n", dev->name, err);
			rcu_read_unlock();
			return;
		}
		/* update new sta with its last rx activity */
		sta->last_rx = jiffies;
	}

	/*
	 * FIXME: Do we really need to update the sta_info's information here?
	 *	  We already know about the AP (we found it in our list) so it
	 *	  should already be filled with the right info, no?
	 *	  As is stands, all this is racy because typically we assume
	 *	  the information that is filled in here (except flags) doesn't
	 *	  change while a STA structure is alive. As such, it should move
	 *	  to between the sta_info_alloc() and sta_info_insert() above.
	 */

	set_sta_flags(sta, WLAN_STA_AUTH | WLAN_STA_ASSOC | WLAN_STA_ASSOC_AP |
			   WLAN_STA_AUTHORIZED);

	rates = 0;
	basic_rates = 0;
	sband = local->hw.wiphy->bands[local->hw.conf.channel->band];

	for (i = 0; i < elems.supp_rates_len; i++) {
		int rate = (elems.supp_rates[i] & 0x7f) * 5;

		if (rate > 110)
			have_higher_than_11mbit = true;

		for (j = 0; j < sband->n_bitrates; j++) {
			if (sband->bitrates[j].bitrate == rate)
				rates |= BIT(j);
			if (elems.supp_rates[i] & 0x80)
				basic_rates |= BIT(j);
		}
	}

	for (i = 0; i < elems.ext_supp_rates_len; i++) {
		int rate = (elems.ext_supp_rates[i] & 0x7f) * 5;

		if (rate > 110)
			have_higher_than_11mbit = true;

		for (j = 0; j < sband->n_bitrates; j++) {
			if (sband->bitrates[j].bitrate == rate)
				rates |= BIT(j);
			if (elems.ext_supp_rates[i] & 0x80)
				basic_rates |= BIT(j);
		}
	}

	sta->supp_rates[local->hw.conf.channel->band] = rates;
	sdata->basic_rates = basic_rates;

	/* cf. IEEE 802.11 9.2.12 */
	if (local->hw.conf.channel->band == IEEE80211_BAND_2GHZ &&
	    have_higher_than_11mbit)
		sdata->flags |= IEEE80211_SDATA_OPERATING_GMODE;
	else
		sdata->flags &= ~IEEE80211_SDATA_OPERATING_GMODE;

	if (elems.ht_cap_elem && elems.ht_info_elem && elems.wmm_param &&
	    (ifsta->flags & IEEE80211_STA_WMM_ENABLED)) {
		struct ieee80211_ht_bss_info bss_info;
		ieee80211_ht_cap_ie_to_ht_info(
				(struct ieee80211_ht_cap *)
				elems.ht_cap_elem, &sta->ht_info);
		ieee80211_ht_addt_info_ie_to_ht_bss_info(
				(struct ieee80211_ht_addt_info *)
				elems.ht_info_elem, &bss_info);
		ieee80211_handle_ht(local, 1, &sta->ht_info, &bss_info);
	}

	rate_control_rate_init(sta, local);

	if (elems.wmm_param) {
		set_sta_flags(sta, WLAN_STA_WME);
		rcu_read_unlock();
		ieee80211_sta_wmm_params(dev, ifsta, elems.wmm_param,
					 elems.wmm_param_len);
	} else
		rcu_read_unlock();
	sdata_info(sdata, "disassociated from %pM (Reason: %u)\n",
		   mgmt->sa, reason_code);
	if (!ether_addr_equal(mgmt->bssid, mgmt->sa)) {
		ieee80211_tdls_handle_disconnect(sdata, mgmt->sa, reason_code);
		return;
	}

	sdata_info(sdata, "disassociated from %pM (Reason: %u=%s)\n",
		   mgmt->sa, reason_code,
		   ieee80211_get_reason_code_string(reason_code));

	ieee80211_set_disassoc(sdata, 0, 0, false, NULL);

	ieee80211_report_disconnect(sdata, (u8 *)mgmt, len, false, reason_code);
}

static void ieee80211_get_rates(struct ieee80211_supported_band *sband,
				u8 *supp_rates, unsigned int supp_rates_len,
				u32 *rates, u32 *basic_rates,
				bool *have_higher_than_11mbit,
				int *min_rate, int *min_rate_index,
				int shift, u32 rate_flags)
{
	int i, j;

	for (i = 0; i < supp_rates_len; i++) {
		int rate = supp_rates[i] & 0x7f;
		bool is_basic = !!(supp_rates[i] & 0x80);

		if ((rate * 5 * (1 << shift)) > 110)
			*have_higher_than_11mbit = true;

		/*
		 * BSS_MEMBERSHIP_SELECTOR_HT_PHY is defined in 802.11n-2009
		 * 7.3.2.2 as a magic value instead of a rate. Hence, skip it.
		 *
		 * Note: Even through the membership selector and the basic
		 *	 rate flag share the same bit, they are not exactly
		 *	 the same.
		 */
		if (!!(supp_rates[i] & 0x80) &&
		    (supp_rates[i] & 0x7f) == BSS_MEMBERSHIP_SELECTOR_HT_PHY)
			continue;

		for (j = 0; j < sband->n_bitrates; j++) {
			struct ieee80211_rate *br;
			int brate;

			br = &sband->bitrates[j];
			if ((rate_flags & br->flags) != rate_flags)
				continue;

			brate = DIV_ROUND_UP(br->bitrate, (1 << shift) * 5);
			if (brate == rate) {
				*rates |= BIT(j);
				if (is_basic)
					*basic_rates |= BIT(j);
				if ((rate * 5) < *min_rate) {
					*min_rate = rate * 5;
					*min_rate_index = j;
				}
				break;
			}
		}
	}
}

static bool ieee80211_assoc_success(struct ieee80211_sub_if_data *sdata,
				    struct cfg80211_bss *cbss,
				    struct ieee80211_mgmt *mgmt, size_t len)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_supported_band *sband;
	struct sta_info *sta;
	u8 *pos;
	u16 capab_info, aid;
	struct ieee802_11_elems elems;
	struct ieee80211_bss_conf *bss_conf = &sdata->vif.bss_conf;
	const struct cfg80211_bss_ies *bss_ies = NULL;
	struct ieee80211_mgd_assoc_data *assoc_data = ifmgd->assoc_data;
	u32 changed = 0;
	int err;
	bool ret;

	/* AssocResp and ReassocResp have identical structure */

	aid = le16_to_cpu(mgmt->u.assoc_resp.aid);
	capab_info = le16_to_cpu(mgmt->u.assoc_resp.capab_info);

	if ((aid & (BIT(15) | BIT(14))) != (BIT(15) | BIT(14)))
		sdata_info(sdata, "invalid AID value 0x%x; bits 15:14 not set\n",
			   aid);
	aid &= ~(BIT(15) | BIT(14));

	ifmgd->broken_ap = false;

	if (aid == 0 || aid > IEEE80211_MAX_AID) {
		sdata_info(sdata, "invalid AID value %d (out of range), turn off PS\n",
			   aid);
		aid = 0;
		ifmgd->broken_ap = true;
	}

	pos = mgmt->u.assoc_resp.variable;
	ieee802_11_parse_elems(pos, len - (pos - (u8 *) mgmt), false, &elems);

	if (!elems.supp_rates) {
		sdata_info(sdata, "no SuppRates element in AssocResp\n");
		return false;
	}

	ifmgd->aid = aid;
	ifmgd->tdls_chan_switch_prohibited =
		elems.ext_capab && elems.ext_capab_len >= 5 &&
		(elems.ext_capab[4] & WLAN_EXT_CAPA5_TDLS_CH_SW_PROHIBITED);

	/*
	 * Some APs are erroneously not including some information in their
	 * (re)association response frames. Try to recover by using the data
	 * from the beacon or probe response. This seems to afflict mobile
	 * 2G/3G/4G wifi routers, reported models include the "Onda PN51T",
	 * "Vodafone PocketWiFi 2", "ZTE MF60" and a similar T-Mobile device.
	 */
	if ((assoc_data->wmm && !elems.wmm_param) ||
	    (!(ifmgd->flags & IEEE80211_STA_DISABLE_HT) &&
	     (!elems.ht_cap_elem || !elems.ht_operation)) ||
	    (!(ifmgd->flags & IEEE80211_STA_DISABLE_VHT) &&
	     (!elems.vht_cap_elem || !elems.vht_operation))) {
		const struct cfg80211_bss_ies *ies;
		struct ieee802_11_elems bss_elems;

		rcu_read_lock();
		ies = rcu_dereference(cbss->ies);
		if (ies)
			bss_ies = kmemdup(ies, sizeof(*ies) + ies->len,
					  GFP_ATOMIC);
		rcu_read_unlock();
		if (!bss_ies)
			return false;

		ieee802_11_parse_elems(bss_ies->data, bss_ies->len,
				       false, &bss_elems);
		if (assoc_data->wmm &&
		    !elems.wmm_param && bss_elems.wmm_param) {
			elems.wmm_param = bss_elems.wmm_param;
			sdata_info(sdata,
				   "AP bug: WMM param missing from AssocResp\n");
		}

		/*
		 * Also check if we requested HT/VHT, otherwise the AP doesn't
		 * have to include the IEs in the (re)association response.
		 */
		if (!elems.ht_cap_elem && bss_elems.ht_cap_elem &&
		    !(ifmgd->flags & IEEE80211_STA_DISABLE_HT)) {
			elems.ht_cap_elem = bss_elems.ht_cap_elem;
			sdata_info(sdata,
				   "AP bug: HT capability missing from AssocResp\n");
		}
		if (!elems.ht_operation && bss_elems.ht_operation &&
		    !(ifmgd->flags & IEEE80211_STA_DISABLE_HT)) {
			elems.ht_operation = bss_elems.ht_operation;
			sdata_info(sdata,
				   "AP bug: HT operation missing from AssocResp\n");
		}
		if (!elems.vht_cap_elem && bss_elems.vht_cap_elem &&
		    !(ifmgd->flags & IEEE80211_STA_DISABLE_VHT)) {
			elems.vht_cap_elem = bss_elems.vht_cap_elem;
			sdata_info(sdata,
				   "AP bug: VHT capa missing from AssocResp\n");
		}
		if (!elems.vht_operation && bss_elems.vht_operation &&
		    !(ifmgd->flags & IEEE80211_STA_DISABLE_VHT)) {
			elems.vht_operation = bss_elems.vht_operation;
			sdata_info(sdata,
				   "AP bug: VHT operation missing from AssocResp\n");
		}
	}

	/*
	 * We previously checked these in the beacon/probe response, so
	 * they should be present here. This is just a safety net.
	 */
	if (!(ifmgd->flags & IEEE80211_STA_DISABLE_HT) &&
	    (!elems.wmm_param || !elems.ht_cap_elem || !elems.ht_operation)) {
		sdata_info(sdata,
			   "HT AP is missing WMM params or HT capability/operation\n");
		ret = false;
		goto out;
	}

	if (!(ifmgd->flags & IEEE80211_STA_DISABLE_VHT) &&
	    (!elems.vht_cap_elem || !elems.vht_operation)) {
		sdata_info(sdata,
			   "VHT AP is missing VHT capability/operation\n");
		ret = false;
		goto out;
	}

	mutex_lock(&sdata->local->sta_mtx);
	/*
	 * station info was already allocated and inserted before
	 * the association and should be available to us
	 */
	sta = sta_info_get(sdata, cbss->bssid);
	if (WARN_ON(!sta)) {
		mutex_unlock(&sdata->local->sta_mtx);
		ret = false;
		goto out;
	}

	sband = local->hw.wiphy->bands[ieee80211_get_sdata_band(sdata)];

	/* Set up internal HT/VHT capabilities */
	if (elems.ht_cap_elem && !(ifmgd->flags & IEEE80211_STA_DISABLE_HT))
		ieee80211_ht_cap_ie_to_sta_ht_cap(sdata, sband,
						  elems.ht_cap_elem, sta);

	if (elems.vht_cap_elem && !(ifmgd->flags & IEEE80211_STA_DISABLE_VHT))
		ieee80211_vht_cap_ie_to_sta_vht_cap(sdata, sband,
						    elems.vht_cap_elem, sta);

	/*
	 * Some APs, e.g. Netgear WNDR3700, report invalid HT operation data
	 * in their association response, so ignore that data for our own
	 * configuration. If it changed since the last beacon, we'll get the
	 * next beacon and update then.
	 */

	/*
	 * If an operating mode notification IE is present, override the
	 * NSS calculation (that would be done in rate_control_rate_init())
	 * and use the # of streams from that element.
	 */
	if (elems.opmode_notif &&
	    !(*elems.opmode_notif & IEEE80211_OPMODE_NOTIF_RX_NSS_TYPE_BF)) {
		u8 nss;

		nss = *elems.opmode_notif & IEEE80211_OPMODE_NOTIF_RX_NSS_MASK;
		nss >>= IEEE80211_OPMODE_NOTIF_RX_NSS_SHIFT;
		nss += 1;
		sta->sta.rx_nss = nss;
	}

	rate_control_rate_init(sta);

	if (ifmgd->flags & IEEE80211_STA_MFP_ENABLED) {
		set_sta_flag(sta, WLAN_STA_MFP);
		sta->sta.mfp = true;
	} else {
		sta->sta.mfp = false;
	}

	sta->sta.wme = elems.wmm_param && local->hw.queues >= IEEE80211_NUM_ACS;

	err = sta_info_move_state(sta, IEEE80211_STA_ASSOC);
	if (!err && !(ifmgd->flags & IEEE80211_STA_CONTROL_PORT))
		err = sta_info_move_state(sta, IEEE80211_STA_AUTHORIZED);
	if (err) {
		sdata_info(sdata,
			   "failed to move station %pM to desired state\n",
			   sta->sta.addr);
		WARN_ON(__sta_info_destroy(sta));
		mutex_unlock(&sdata->local->sta_mtx);
		ret = false;
		goto out;
	}

	mutex_unlock(&sdata->local->sta_mtx);

	/*
	 * Always handle WMM once after association regardless
	 * of the first value the AP uses. Setting -1 here has
	 * that effect because the AP values is an unsigned
	 * 4-bit value.
	 */
	ifmgd->wmm_last_param_set = -1;

	if (ifmgd->flags & IEEE80211_STA_DISABLE_WMM) {
		ieee80211_set_wmm_default(sdata, false, false);
	} else if (!ieee80211_sta_wmm_params(local, sdata, elems.wmm_param,
					     elems.wmm_param_len)) {
		/* still enable QoS since we might have HT/VHT */
		ieee80211_set_wmm_default(sdata, false, true);
		/* set the disable-WMM flag in this case to disable
		 * tracking WMM parameter changes in the beacon if
		 * the parameters weren't actually valid. Doing so
		 * avoids changing parameters very strangely when
		 * the AP is going back and forth between valid and
		 * invalid parameters.
		 */
		ifmgd->flags |= IEEE80211_STA_DISABLE_WMM;
	}
	changed |= BSS_CHANGED_QOS;

	/* set AID and assoc capability,
	 * ieee80211_set_associated() will tell the driver */
	bss_conf->aid = aid;
	bss_conf->assoc_capability = capab_info;
	ieee80211_set_associated(dev, ifsta, 1);

	ieee80211_associated(dev, ifsta);
}


/* Caller must hold local->sta_bss_lock */
static void __ieee80211_rx_bss_hash_add(struct net_device *dev,
					struct ieee80211_sta_bss *bss)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	u8 hash_idx;

	if (bss_mesh_cfg(bss))
		hash_idx = mesh_id_hash(bss_mesh_id(bss),
					bss_mesh_id_len(bss));
	else
		hash_idx = STA_HASH(bss->bssid);

	bss->hnext = local->sta_bss_hash[hash_idx];
	local->sta_bss_hash[hash_idx] = bss;
}


/* Caller must hold local->sta_bss_lock */
static void __ieee80211_rx_bss_hash_del(struct ieee80211_local *local,
					struct ieee80211_sta_bss *bss)
{
	struct ieee80211_sta_bss *b, *prev = NULL;
	b = local->sta_bss_hash[STA_HASH(bss->bssid)];
	while (b) {
		if (b == bss) {
			if (!prev)
				local->sta_bss_hash[STA_HASH(bss->bssid)] =
					bss->hnext;
			else
				prev->hnext = bss->hnext;
			break;
		}
		prev = b;
		b = b->hnext;
	}
}


static struct ieee80211_sta_bss *
ieee80211_rx_bss_add(struct net_device *dev, u8 *bssid, int freq,
		     u8 *ssid, u8 ssid_len)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sta_bss *bss;

	bss = kzalloc(sizeof(*bss), GFP_ATOMIC);
	if (!bss)
		return NULL;
	atomic_inc(&bss->users);
	atomic_inc(&bss->users);
	memcpy(bss->bssid, bssid, ETH_ALEN);
	bss->freq = freq;
	if (ssid && ssid_len <= IEEE80211_MAX_SSID_LEN) {
		memcpy(bss->ssid, ssid, ssid_len);
		bss->ssid_len = ssid_len;
	}

	spin_lock_bh(&local->sta_bss_lock);
	/* TODO: order by RSSI? */
	list_add_tail(&bss->list, &local->sta_bss_list);
	__ieee80211_rx_bss_hash_add(dev, bss);
	spin_unlock_bh(&local->sta_bss_lock);
	return bss;
}

static struct ieee80211_sta_bss *
ieee80211_rx_bss_get(struct net_device *dev, u8 *bssid, int freq,
		     u8 *ssid, u8 ssid_len)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sta_bss *bss;

	spin_lock_bh(&local->sta_bss_lock);
	bss = local->sta_bss_hash[STA_HASH(bssid)];
	while (bss) {
		if (!bss_mesh_cfg(bss) &&
		    !memcmp(bss->bssid, bssid, ETH_ALEN) &&
		    bss->freq == freq &&
		    bss->ssid_len == ssid_len &&
		    (ssid_len == 0 || !memcmp(bss->ssid, ssid, ssid_len))) {
			atomic_inc(&bss->users);
			break;
		}
		bss = bss->hnext;
	}
	spin_unlock_bh(&local->sta_bss_lock);
	return bss;
}

#ifdef CONFIG_MAC80211_MESH
static struct ieee80211_sta_bss *
ieee80211_rx_mesh_bss_get(struct net_device *dev, u8 *mesh_id, int mesh_id_len,
			  u8 *mesh_cfg, int freq)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sta_bss *bss;

	spin_lock_bh(&local->sta_bss_lock);
	bss = local->sta_bss_hash[mesh_id_hash(mesh_id, mesh_id_len)];
	while (bss) {
		if (bss_mesh_cfg(bss) &&
		    !memcmp(bss_mesh_cfg(bss), mesh_cfg, MESH_CFG_CMP_LEN) &&
		    bss->freq == freq &&
		    mesh_id_len == bss->mesh_id_len &&
		    (mesh_id_len == 0 || !memcmp(bss->mesh_id, mesh_id,
						 mesh_id_len))) {
			atomic_inc(&bss->users);
			break;
		}
		bss = bss->hnext;
	}
	spin_unlock_bh(&local->sta_bss_lock);
	return bss;
}

static struct ieee80211_sta_bss *
ieee80211_rx_mesh_bss_add(struct net_device *dev, u8 *mesh_id, int mesh_id_len,
			  u8 *mesh_cfg, int mesh_config_len, int freq)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sta_bss *bss;

	if (mesh_config_len != MESH_CFG_LEN)
		return NULL;

	bss = kzalloc(sizeof(*bss), GFP_ATOMIC);
	if (!bss)
		return NULL;

	bss->mesh_cfg = kmalloc(MESH_CFG_CMP_LEN, GFP_ATOMIC);
	if (!bss->mesh_cfg) {
		kfree(bss);
		return NULL;
	}

	if (mesh_id_len && mesh_id_len <= IEEE80211_MAX_MESH_ID_LEN) {
		bss->mesh_id = kmalloc(mesh_id_len, GFP_ATOMIC);
		if (!bss->mesh_id) {
			kfree(bss->mesh_cfg);
			kfree(bss);
			return NULL;
		}
		memcpy(bss->mesh_id, mesh_id, mesh_id_len);
	}

	atomic_inc(&bss->users);
	atomic_inc(&bss->users);
	memcpy(bss->mesh_cfg, mesh_cfg, MESH_CFG_CMP_LEN);
	bss->mesh_id_len = mesh_id_len;
	bss->freq = freq;
	spin_lock_bh(&local->sta_bss_lock);
	/* TODO: order by RSSI? */
	list_add_tail(&bss->list, &local->sta_bss_list);
	__ieee80211_rx_bss_hash_add(dev, bss);
	spin_unlock_bh(&local->sta_bss_lock);
	return bss;
}
#endif

static void ieee80211_rx_bss_free(struct ieee80211_sta_bss *bss)
{
	kfree(bss->wpa_ie);
	kfree(bss->rsn_ie);
	kfree(bss->wmm_ie);
	kfree(bss->ht_ie);
	kfree(bss->ht_add_ie);
	kfree(bss_mesh_id(bss));
	kfree(bss_mesh_cfg(bss));
	kfree(bss);
}


static void ieee80211_rx_bss_put(struct ieee80211_local *local,
				 struct ieee80211_sta_bss *bss)
{
	local_bh_disable();
	if (!atomic_dec_and_lock(&bss->users, &local->sta_bss_lock)) {
		local_bh_enable();
		return;
	}

	__ieee80211_rx_bss_hash_del(local, bss);
	list_del(&bss->list);
	spin_unlock_bh(&local->sta_bss_lock);
	ieee80211_rx_bss_free(bss);
}


void ieee80211_rx_bss_list_init(struct ieee80211_local *local)
{
	spin_lock_init(&local->sta_bss_lock);
	INIT_LIST_HEAD(&local->sta_bss_list);
}


void ieee80211_rx_bss_list_deinit(struct ieee80211_local *local)
{
	struct ieee80211_sta_bss *bss, *tmp;

	list_for_each_entry_safe(bss, tmp, &local->sta_bss_list, list)
		ieee80211_rx_bss_put(local, bss);
}


static int ieee80211_sta_join_ibss(struct net_device *dev,
				   struct ieee80211_if_sta *ifsta,
				   struct ieee80211_sta_bss *bss)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	int res, rates, i, j;
	struct sk_buff *skb;
	struct ieee80211_mgmt *mgmt;
	u8 *pos;
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_supported_band *sband;
	union iwreq_data wrqu;

	sband = local->hw.wiphy->bands[local->hw.conf.channel->band];

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	/* Remove possible STA entries from other IBSS networks. */
	sta_info_flush_delayed(sdata);

	if (local->ops->reset_tsf) {
		/* Reset own TSF to allow time synchronization work. */
		local->ops->reset_tsf(local_to_hw(local));
	}
	memcpy(ifsta->bssid, bss->bssid, ETH_ALEN);
	res = ieee80211_if_config(sdata, IEEE80211_IFCC_BSSID);
	if (res)
		return res;

	local->hw.conf.beacon_int = bss->beacon_int >= 10 ? bss->beacon_int : 10;

	sdata->drop_unencrypted = bss->capability &
		WLAN_CAPABILITY_PRIVACY ? 1 : 0;

	res = ieee80211_set_freq(dev, bss->freq);

	if (res)
		return res;

	/* Build IBSS probe response */
	skb = dev_alloc_skb(local->hw.extra_tx_headroom + 400);
	if (skb) {
		skb_reserve(skb, local->hw.extra_tx_headroom);

		mgmt = (struct ieee80211_mgmt *)
			skb_put(skb, 24 + sizeof(mgmt->u.beacon));
		memset(mgmt, 0, 24 + sizeof(mgmt->u.beacon));
		mgmt->frame_control = IEEE80211_FC(IEEE80211_FTYPE_MGMT,
						   IEEE80211_STYPE_PROBE_RESP);
		memset(mgmt->da, 0xff, ETH_ALEN);
		memcpy(mgmt->sa, dev->dev_addr, ETH_ALEN);
		memcpy(mgmt->bssid, ifsta->bssid, ETH_ALEN);
		mgmt->u.beacon.beacon_int =
			cpu_to_le16(local->hw.conf.beacon_int);
		mgmt->u.beacon.timestamp = cpu_to_le64(bss->timestamp);
		mgmt->u.beacon.capab_info = cpu_to_le16(bss->capability);

		pos = skb_put(skb, 2 + ifsta->ssid_len);
		*pos++ = WLAN_EID_SSID;
		*pos++ = ifsta->ssid_len;
		memcpy(pos, ifsta->ssid, ifsta->ssid_len);

		rates = bss->supp_rates_len;
		if (rates > 8)
			rates = 8;
		pos = skb_put(skb, 2 + rates);
		*pos++ = WLAN_EID_SUPP_RATES;
		*pos++ = rates;
		memcpy(pos, bss->supp_rates, rates);

		if (bss->band == IEEE80211_BAND_2GHZ) {
			pos = skb_put(skb, 2 + 1);
			*pos++ = WLAN_EID_DS_PARAMS;
			*pos++ = 1;
			*pos++ = ieee80211_frequency_to_channel(bss->freq);
		}

		pos = skb_put(skb, 2 + 2);
		*pos++ = WLAN_EID_IBSS_PARAMS;
		*pos++ = 2;
		/* FIX: set ATIM window based on scan results */
		*pos++ = 0;
		*pos++ = 0;

		if (bss->supp_rates_len > 8) {
			rates = bss->supp_rates_len - 8;
			pos = skb_put(skb, 2 + rates);
			*pos++ = WLAN_EID_EXT_SUPP_RATES;
			*pos++ = rates;
			memcpy(pos, &bss->supp_rates[8], rates);
		}

		ifsta->probe_resp = skb;

		ieee80211_if_config(sdata, IEEE80211_IFCC_BEACON);
	}

	rates = 0;
	sband = local->hw.wiphy->bands[local->hw.conf.channel->band];
	for (i = 0; i < bss->supp_rates_len; i++) {
		int bitrate = (bss->supp_rates[i] & 0x7f) * 5;
		for (j = 0; j < sband->n_bitrates; j++)
			if (sband->bitrates[j].bitrate == bitrate)
				rates |= BIT(j);
	}
	ifsta->supp_rates_bits[local->hw.conf.channel->band] = rates;

	ieee80211_sta_def_wmm_params(dev, bss, 1);

	ifsta->state = IEEE80211_IBSS_JOINED;
	mod_timer(&ifsta->timer, jiffies + IEEE80211_IBSS_MERGE_INTERVAL);

	memset(&wrqu, 0, sizeof(wrqu));
	memcpy(wrqu.ap_addr.sa_data, bss->bssid, ETH_ALEN);
	wireless_send_event(dev, SIOCGIWAP, &wrqu, NULL);

	return res;
}

u64 ieee80211_sta_get_rates(struct ieee80211_local *local,
			    struct ieee802_11_elems *elems,
			    enum ieee80211_band band)
{
	struct ieee80211_supported_band *sband;
	struct ieee80211_rate *bitrates;
	size_t num_rates;
	u64 supp_rates;
	int i, j;
	sband = local->hw.wiphy->bands[band];

	if (!sband) {
		WARN_ON(1);
		sband = local->hw.wiphy->bands[local->hw.conf.channel->band];
	}

	bitrates = sband->bitrates;
	num_rates = sband->n_bitrates;
	supp_rates = 0;
	for (i = 0; i < elems->supp_rates_len +
		     elems->ext_supp_rates_len; i++) {
		u8 rate = 0;
		int own_rate;
		if (i < elems->supp_rates_len)
			rate = elems->supp_rates[i];
		else if (elems->ext_supp_rates)
			rate = elems->ext_supp_rates
				[i - elems->supp_rates_len];
		own_rate = 5 * (rate & 0x7f);
		for (j = 0; j < num_rates; j++)
			if (bitrates[j].bitrate == own_rate)
				supp_rates |= BIT(j);
	}
	return supp_rates;
}


static void ieee80211_rx_bss_info(struct net_device *dev,
				  struct ieee80211_mgmt *mgmt,
				  size_t len,
				  struct ieee80211_rx_status *rx_status,
				  struct ieee802_11_elems *elems,
				  int beacon)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	int freq, clen;
	struct ieee80211_sta_bss *bss;
	struct sta_info *sta;
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	u64 beacon_timestamp, rx_timestamp;
	struct ieee80211_channel *channel;
	DECLARE_MAC_BUF(mac);
	DECLARE_MAC_BUF(mac2);

	if (!beacon && memcmp(mgmt->da, dev->dev_addr, ETH_ALEN))
		return; /* ignore ProbeResp to foreign address */

	beacon_timestamp = le64_to_cpu(mgmt->u.beacon.timestamp);

	if (ieee80211_vif_is_mesh(&sdata->vif) && elems->mesh_id &&
	    elems->mesh_config && mesh_matches_local(elems, dev)) {
		u64 rates = ieee80211_sta_get_rates(local, elems,
						rx_status->band);

		mesh_neighbour_update(mgmt->sa, rates, dev,
				      mesh_peer_accepts_plinks(elems, dev));
	}

	rcu_read_lock();

	if (sdata->vif.type == IEEE80211_IF_TYPE_IBSS && elems->supp_rates &&
	    memcmp(mgmt->bssid, sdata->u.sta.bssid, ETH_ALEN) == 0 &&
	    (sta = sta_info_get(local, mgmt->sa))) {
		u64 prev_rates;
		u64 supp_rates = ieee80211_sta_get_rates(local, elems,
							rx_status->band);

		prev_rates = sta->supp_rates[rx_status->band];
		sta->supp_rates[rx_status->band] &= supp_rates;
		if (sta->supp_rates[rx_status->band] == 0) {
			/* No matching rates - this should not really happen.
			 * Make sure that at least one rate is marked
			 * supported to avoid issues with TX rate ctrl. */
			sta->supp_rates[rx_status->band] =
				sdata->u.sta.supp_rates_bits[rx_status->band];
		}
	}

	rcu_read_unlock();

	if (elems->ds_params && elems->ds_params_len == 1)
		freq = ieee80211_channel_to_frequency(elems->ds_params[0]);
	else
		freq = rx_status->freq;

	channel = ieee80211_get_channel(local->hw.wiphy, freq);

	if (!channel || channel->flags & IEEE80211_CHAN_DISABLED)
		return;

#ifdef CONFIG_MAC80211_MESH
	if (elems->mesh_config)
		bss = ieee80211_rx_mesh_bss_get(dev, elems->mesh_id,
				elems->mesh_id_len, elems->mesh_config, freq);
	else
#endif
		bss = ieee80211_rx_bss_get(dev, mgmt->bssid, freq,
					   elems->ssid, elems->ssid_len);
	if (!bss) {
#ifdef CONFIG_MAC80211_MESH
		if (elems->mesh_config)
			bss = ieee80211_rx_mesh_bss_add(dev, elems->mesh_id,
				elems->mesh_id_len, elems->mesh_config,
				elems->mesh_config_len, freq);
		else
#endif
			bss = ieee80211_rx_bss_add(dev, mgmt->bssid, freq,
						  elems->ssid, elems->ssid_len);
		if (!bss)
			return;
	} else {
#if 0
		/* TODO: order by RSSI? */
		spin_lock_bh(&local->sta_bss_lock);
		list_move_tail(&bss->list, &local->sta_bss_list);
		spin_unlock_bh(&local->sta_bss_lock);
#endif
	}

	/* save the ERP value so that it is available at association time */
	if (elems->erp_info && elems->erp_info_len >= 1) {
		bss->erp_value = elems->erp_info[0];
		bss->has_erp_value = 1;
	}

	if (elems->ht_cap_elem &&
	     (!bss->ht_ie || bss->ht_ie_len != elems->ht_cap_elem_len ||
	     memcmp(bss->ht_ie, elems->ht_cap_elem, elems->ht_cap_elem_len))) {
		kfree(bss->ht_ie);
		bss->ht_ie = kmalloc(elems->ht_cap_elem_len + 2, GFP_ATOMIC);
		if (bss->ht_ie) {
			memcpy(bss->ht_ie, elems->ht_cap_elem - 2,
				elems->ht_cap_elem_len + 2);
			bss->ht_ie_len = elems->ht_cap_elem_len + 2;
		} else
			bss->ht_ie_len = 0;
	} else if (!elems->ht_cap_elem && bss->ht_ie) {
		kfree(bss->ht_ie);
		bss->ht_ie = NULL;
		bss->ht_ie_len = 0;
	}

	if (elems->ht_info_elem &&
	     (!bss->ht_add_ie ||
	     bss->ht_add_ie_len != elems->ht_info_elem_len ||
	     memcmp(bss->ht_add_ie, elems->ht_info_elem,
			elems->ht_info_elem_len))) {
		kfree(bss->ht_add_ie);
		bss->ht_add_ie =
			kmalloc(elems->ht_info_elem_len + 2, GFP_ATOMIC);
		if (bss->ht_add_ie) {
			memcpy(bss->ht_add_ie, elems->ht_info_elem - 2,
				elems->ht_info_elem_len + 2);
			bss->ht_add_ie_len = elems->ht_info_elem_len + 2;
		} else
			bss->ht_add_ie_len = 0;
	} else if (!elems->ht_info_elem && bss->ht_add_ie) {
		kfree(bss->ht_add_ie);
		bss->ht_add_ie = NULL;
		bss->ht_add_ie_len = 0;
	}

	bss->beacon_int = le16_to_cpu(mgmt->u.beacon.beacon_int);
	bss->capability = le16_to_cpu(mgmt->u.beacon.capab_info);

	if (elems->tim) {
		struct ieee80211_tim_ie *tim_ie =
			(struct ieee80211_tim_ie *)elems->tim;
		bss->dtim_period = tim_ie->dtim_period;
	}

	/* set default value for buggy APs */
	if (!elems->tim || bss->dtim_period == 0)
		bss->dtim_period = 1;

	bss->supp_rates_len = 0;
	if (elems->supp_rates) {
		clen = IEEE80211_MAX_SUPP_RATES - bss->supp_rates_len;
		if (clen > elems->supp_rates_len)
			clen = elems->supp_rates_len;
		memcpy(&bss->supp_rates[bss->supp_rates_len], elems->supp_rates,
		       clen);
		bss->supp_rates_len += clen;
	}
	if (elems->ext_supp_rates) {
		clen = IEEE80211_MAX_SUPP_RATES - bss->supp_rates_len;
		if (clen > elems->ext_supp_rates_len)
			clen = elems->ext_supp_rates_len;
		memcpy(&bss->supp_rates[bss->supp_rates_len],
		       elems->ext_supp_rates, clen);
		bss->supp_rates_len += clen;
	}

	bss->band = rx_status->band;

	bss->timestamp = beacon_timestamp;
	bss->last_update = jiffies;
	bss->signal = rx_status->signal;
	bss->noise = rx_status->noise;
	bss->qual = rx_status->qual;
	if (!beacon && !bss->probe_resp)
		bss->probe_resp = true;

	/*
	 * In STA mode, the remaining parameters should not be overridden
	 * by beacons because they're not necessarily accurate there.
	 */
	if (sdata->vif.type != IEEE80211_IF_TYPE_IBSS &&
	    bss->probe_resp && beacon) {
		ieee80211_rx_bss_put(local, bss);
		return;
	}

	if (elems->wpa &&
	    (!bss->wpa_ie || bss->wpa_ie_len != elems->wpa_len ||
	     memcmp(bss->wpa_ie, elems->wpa, elems->wpa_len))) {
		kfree(bss->wpa_ie);
		bss->wpa_ie = kmalloc(elems->wpa_len + 2, GFP_ATOMIC);
		if (bss->wpa_ie) {
			memcpy(bss->wpa_ie, elems->wpa - 2, elems->wpa_len + 2);
			bss->wpa_ie_len = elems->wpa_len + 2;
		} else
			bss->wpa_ie_len = 0;
	} else if (!elems->wpa && bss->wpa_ie) {
		kfree(bss->wpa_ie);
		bss->wpa_ie = NULL;
		bss->wpa_ie_len = 0;
	}

	if (elems->rsn &&
	    (!bss->rsn_ie || bss->rsn_ie_len != elems->rsn_len ||
	     memcmp(bss->rsn_ie, elems->rsn, elems->rsn_len))) {
		kfree(bss->rsn_ie);
		bss->rsn_ie = kmalloc(elems->rsn_len + 2, GFP_ATOMIC);
		if (bss->rsn_ie) {
			memcpy(bss->rsn_ie, elems->rsn - 2, elems->rsn_len + 2);
			bss->rsn_ie_len = elems->rsn_len + 2;
		} else
			bss->rsn_ie_len = 0;
	} else if (!elems->rsn && bss->rsn_ie) {
		kfree(bss->rsn_ie);
		bss->rsn_ie = NULL;
		bss->rsn_ie_len = 0;
	}

	/*
	 * Cf.
	 * http://www.wipo.int/pctdb/en/wo.jsp?wo=2007047181&IA=WO2007047181&DISPLAY=DESC
	 *
	 * quoting:
	 *
	 * In particular, "Wi-Fi CERTIFIED for WMM - Support for Multimedia
	 * Applications with Quality of Service in Wi-Fi Networks," Wi- Fi
	 * Alliance (September 1, 2004) is incorporated by reference herein.
	 * The inclusion of the WMM Parameters in probe responses and
	 * association responses is mandatory for WMM enabled networks. The
	 * inclusion of the WMM Parameters in beacons, however, is optional.
	 */

	if (elems->wmm_param &&
	    (!bss->wmm_ie || bss->wmm_ie_len != elems->wmm_param_len ||
	     memcmp(bss->wmm_ie, elems->wmm_param, elems->wmm_param_len))) {
		kfree(bss->wmm_ie);
		bss->wmm_ie = kmalloc(elems->wmm_param_len + 2, GFP_ATOMIC);
		if (bss->wmm_ie) {
			memcpy(bss->wmm_ie, elems->wmm_param - 2,
			       elems->wmm_param_len + 2);
			bss->wmm_ie_len = elems->wmm_param_len + 2;
		} else
			bss->wmm_ie_len = 0;
	} else if (elems->wmm_info &&
		    (!bss->wmm_ie || bss->wmm_ie_len != elems->wmm_info_len ||
		     memcmp(bss->wmm_ie, elems->wmm_info,
						elems->wmm_info_len))) {
		 /* As for certain AP's Fifth bit is not set in WMM IE in
		  * beacon frames.So while parsing the beacon frame the
		  * wmm_info structure is used instead of wmm_param.
		  * wmm_info structure was never used to set bss->wmm_ie.
		  * This code fixes this problem by copying the WME
		  * information from wmm_info to bss->wmm_ie and enabling
		  * n-band association.
		  */
		kfree(bss->wmm_ie);
		bss->wmm_ie = kmalloc(elems->wmm_info_len + 2, GFP_ATOMIC);
		if (bss->wmm_ie) {
			memcpy(bss->wmm_ie, elems->wmm_info - 2,
			       elems->wmm_info_len + 2);
			bss->wmm_ie_len = elems->wmm_info_len + 2;
		} else
			bss->wmm_ie_len = 0;
	} else if (!elems->wmm_param && !elems->wmm_info && bss->wmm_ie) {
		kfree(bss->wmm_ie);
		bss->wmm_ie = NULL;
		bss->wmm_ie_len = 0;
	}

	/* check if we need to merge IBSS */
	if (sdata->vif.type == IEEE80211_IF_TYPE_IBSS && beacon &&
	    !local->sta_sw_scanning && !local->sta_hw_scanning &&
	    bss->capability & WLAN_CAPABILITY_IBSS &&
	    bss->freq == local->oper_channel->center_freq &&
	    elems->ssid_len == sdata->u.sta.ssid_len &&
	    memcmp(elems->ssid, sdata->u.sta.ssid,
				sdata->u.sta.ssid_len) == 0) {
		if (rx_status->flag & RX_FLAG_TSFT) {
			/* in order for correct IBSS merging we need mactime
			 *
			 * since mactime is defined as the time the first data
			 * symbol of the frame hits the PHY, and the timestamp
			 * of the beacon is defined as "the time that the data
			 * symbol containing the first bit of the timestamp is
			 * transmitted to the PHY plus the transmitting STA’s
			 * delays through its local PHY from the MAC-PHY
			 * interface to its interface with the WM"
			 * (802.11 11.1.2) - equals the time this bit arrives at
			 * the receiver - we have to take into account the
			 * offset between the two.
			 * e.g: at 1 MBit that means mactime is 192 usec earlier
			 * (=24 bytes * 8 usecs/byte) than the beacon timestamp.
			 */
			int rate = local->hw.wiphy->bands[rx_status->band]->
					bitrates[rx_status->rate_idx].bitrate;
			rx_timestamp = rx_status->mactime + (24 * 8 * 10 / rate);
		} else if (local && local->ops && local->ops->get_tsf)
			/* second best option: get current TSF */
			rx_timestamp = local->ops->get_tsf(local_to_hw(local));
		else
			/* can't merge without knowing the TSF */
			rx_timestamp = -1LLU;
#ifdef CONFIG_MAC80211_IBSS_DEBUG
		printk(KERN_DEBUG "RX beacon SA=%s BSSID="
		       "%s TSF=0x%llx BCN=0x%llx diff=%lld @%lu\n",
		       print_mac(mac, mgmt->sa),
		       print_mac(mac2, mgmt->bssid),
		       (unsigned long long)rx_timestamp,
		       (unsigned long long)beacon_timestamp,
		       (unsigned long long)(rx_timestamp - beacon_timestamp),
		       jiffies);
#endif /* CONFIG_MAC80211_IBSS_DEBUG */
		if (beacon_timestamp > rx_timestamp) {
#ifdef CONFIG_MAC80211_IBSS_DEBUG
			printk(KERN_DEBUG "%s: beacon TSF higher than "
			       "local TSF - IBSS merge with BSSID %s\n",
			       dev->name, print_mac(mac, mgmt->bssid));
#endif
			ieee80211_sta_join_ibss(dev, &sdata->u.sta, bss);
			ieee80211_ibss_add_sta(dev, NULL,
					       mgmt->bssid, mgmt->sa,
					       BIT(rx_status->rate_idx));
		}
	}

	ieee80211_rx_bss_put(local, bss);
}


static void ieee80211_rx_mgmt_probe_resp(struct net_device *dev,
					 struct ieee80211_mgmt *mgmt,
					 size_t len,
					 struct ieee80211_rx_status *rx_status)
{
	size_t baselen;
	struct ieee802_11_elems elems;
	ieee80211_set_associated(sdata, cbss, changed);

	/*
	 * If we're using 4-addr mode, let the AP know that we're
	 * doing so, so that it can create the STA VLAN on its side
	 */
	if (ifmgd->use_4addr)
		ieee80211_send_4addr_nullfunc(local, sdata);

	/*
	 * Start timer to probe the connection to the AP now.
	 * Also start the timer that will detect beacon loss.
	 */
	ieee80211_sta_rx_notify(sdata, (struct ieee80211_hdr *)mgmt);
	ieee80211_sta_reset_beacon_monitor(sdata);

	ret = true;
 out:
	kfree(bss_ies);
	return ret;
}

static void ieee80211_rx_mgmt_assoc_resp(struct ieee80211_sub_if_data *sdata,
					 struct ieee80211_mgmt *mgmt,
					 size_t len)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_mgd_assoc_data *assoc_data = ifmgd->assoc_data;
	u16 capab_info, status_code, aid;
	struct ieee802_11_elems elems;
	int ac, uapsd_queues = -1;
	u8 *pos;
	bool reassoc;
	struct cfg80211_bss *bss;
	struct ieee80211_event event = {
		.type = MLME_EVENT,
		.u.mlme.data = ASSOC_EVENT,
	};

	sdata_assert_lock(sdata);

	if (!assoc_data)
		return;
	if (!ether_addr_equal(assoc_data->bss->bssid, mgmt->bssid))
		return;

	/*
	 * AssocResp and ReassocResp have identical structure, so process both
	 * of them in this function.
	 */

	if (len < 24 + 6)
		return;

	reassoc = ieee80211_is_reassoc_req(mgmt->frame_control);
	capab_info = le16_to_cpu(mgmt->u.assoc_resp.capab_info);
	status_code = le16_to_cpu(mgmt->u.assoc_resp.status_code);
	aid = le16_to_cpu(mgmt->u.assoc_resp.aid);

	sdata_info(sdata,
		   "RX %sssocResp from %pM (capab=0x%x status=%d aid=%d)\n",
		   reassoc ? "Rea" : "A", mgmt->sa,
		   capab_info, status_code, (u16)(aid & ~(BIT(15) | BIT(14))));

	pos = mgmt->u.assoc_resp.variable;
	ieee802_11_parse_elems(pos, len - (pos - (u8 *) mgmt), false, &elems);

	if (status_code == WLAN_STATUS_ASSOC_REJECTED_TEMPORARILY &&
	    elems.timeout_int &&
	    elems.timeout_int->type == WLAN_TIMEOUT_ASSOC_COMEBACK) {
		u32 tu, ms;
		tu = le32_to_cpu(elems.timeout_int->value);
		ms = tu * 1024 / 1000;
		sdata_info(sdata,
			   "%pM rejected association temporarily; comeback duration %u TU (%u ms)\n",
			   mgmt->sa, tu, ms);
		assoc_data->timeout = jiffies + msecs_to_jiffies(ms);
		assoc_data->timeout_started = true;
		if (ms > IEEE80211_ASSOC_TIMEOUT)
			run_again(sdata, assoc_data->timeout);
		return;
	}

	bss = assoc_data->bss;

	if (status_code != WLAN_STATUS_SUCCESS) {
		sdata_info(sdata, "%pM denied association (code=%d)\n",
			   mgmt->sa, status_code);
		ieee80211_destroy_assoc_data(sdata, false, false);
		event.u.mlme.status = MLME_DENIED;
		event.u.mlme.reason = status_code;
		drv_event_callback(sdata->local, sdata, &event);
	} else {
		if (!ieee80211_assoc_success(sdata, bss, mgmt, len)) {
			/* oops -- internal error -- send timeout for now */
			ieee80211_destroy_assoc_data(sdata, false, false);
			cfg80211_assoc_timeout(sdata->dev, bss);
			return;
		}
		event.u.mlme.status = MLME_SUCCESS;
		drv_event_callback(sdata->local, sdata, &event);
		sdata_info(sdata, "associated\n");

		/*
		 * destroy assoc_data afterwards, as otherwise an idle
		 * recalc after assoc_data is NULL but before associated
		 * is set can cause the interface to go idle
		 */
		ieee80211_destroy_assoc_data(sdata, true, false);

		/* get uapsd queues configuration */
		uapsd_queues = 0;
		for (ac = 0; ac < IEEE80211_NUM_ACS; ac++)
			if (sdata->tx_conf[ac].uapsd)
				uapsd_queues |= BIT(ac);
	}

	cfg80211_rx_assoc_resp(sdata->dev, bss, (u8 *)mgmt, len, uapsd_queues);
}

static void ieee80211_rx_bss_info(struct ieee80211_sub_if_data *sdata,
				  struct ieee80211_mgmt *mgmt, size_t len,
				  struct ieee80211_rx_status *rx_status,
				  struct ieee802_11_elems *elems)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_bss *bss;
	struct ieee80211_channel *channel;

	sdata_assert_lock(sdata);

	channel = ieee80211_get_channel(local->hw.wiphy, rx_status->freq);
	if (!channel)
		return;

	bss = ieee80211_bss_info_update(local, rx_status, mgmt, len, elems,
					channel);
	if (bss) {
		sdata->vif.bss_conf.beacon_rate = bss->beacon_rate;
		ieee80211_rx_bss_put(local, bss);
	}
}


static void ieee80211_rx_mgmt_probe_resp(struct ieee80211_sub_if_data *sdata,
					 struct sk_buff *skb)
{
	struct ieee80211_mgmt *mgmt = (void *)skb->data;
	struct ieee80211_if_managed *ifmgd;
	struct ieee80211_rx_status *rx_status = (void *) skb->cb;
	size_t baselen, len = skb->len;
	struct ieee802_11_elems elems;

	ifmgd = &sdata->u.mgd;

	sdata_assert_lock(sdata);

	if (!ether_addr_equal(mgmt->da, sdata->vif.addr))
		return; /* ignore ProbeResp to foreign address */

	baselen = (u8 *) mgmt->u.probe_resp.variable - (u8 *) mgmt;
	if (baselen > len)
		return;

	ieee802_11_parse_elems(mgmt->u.probe_resp.variable, len - baselen,
				&elems);

	ieee80211_rx_bss_info(dev, mgmt, len, rx_status, &elems, 0);
}


static void ieee80211_rx_mgmt_beacon(struct net_device *dev,
				     struct ieee80211_mgmt *mgmt,
				     size_t len,
				     struct ieee80211_rx_status *rx_status)
{
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_if_sta *ifsta;
	size_t baselen;
	struct ieee802_11_elems elems;
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_conf *conf = &local->hw.conf;
	u32 changed = 0;
			       false, &elems);

	ieee80211_rx_bss_info(sdata, mgmt, len, rx_status, &elems);

	if (ifmgd->associated &&
	    ether_addr_equal(mgmt->bssid, ifmgd->associated->bssid))
		ieee80211_reset_ap_probe(sdata);
}

/*
 * This is the canonical list of information elements we care about,
 * the filter code also gives us all changes to the Microsoft OUI
 * (00:50:F2) vendor IE which is used for WMM which we need to track,
 * as well as the DTPC IE (part of the Cisco OUI) used for signaling
 * changes to requested client power.
 *
 * We implement beacon filtering in software since that means we can
 * avoid processing the frame here and in cfg80211, and userspace
 * will not be able to tell whether the hardware supports it or not.
 *
 * XXX: This list needs to be dynamic -- userspace needs to be able to
 *	add items it requires. It also needs to be able to tell us to
 *	look out for other vendor IEs.
 */
static const u64 care_about_ies =
	(1ULL << WLAN_EID_COUNTRY) |
	(1ULL << WLAN_EID_ERP_INFO) |
	(1ULL << WLAN_EID_CHANNEL_SWITCH) |
	(1ULL << WLAN_EID_PWR_CONSTRAINT) |
	(1ULL << WLAN_EID_HT_CAPABILITY) |
	(1ULL << WLAN_EID_HT_OPERATION) |
	(1ULL << WLAN_EID_EXT_CHANSWITCH_ANN);

static void ieee80211_rx_mgmt_beacon(struct ieee80211_sub_if_data *sdata,
				     struct ieee80211_mgmt *mgmt, size_t len,
				     struct ieee80211_rx_status *rx_status)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_bss_conf *bss_conf = &sdata->vif.bss_conf;
	size_t baselen;
	struct ieee802_11_elems elems;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_chanctx_conf *chanctx_conf;
	struct ieee80211_channel *chan;
	struct sta_info *sta;
	u32 changed = 0;
	bool erp_valid;
	u8 erp_value = 0;
	u32 ncrc;
	u8 *bssid;
	u8 deauth_buf[IEEE80211_DEAUTH_FRAME_LEN];

	sdata_assert_lock(sdata);

	/* Process beacon from the current BSS */
	baselen = (u8 *) mgmt->u.beacon.variable - (u8 *) mgmt;
	if (baselen > len)
		return;

	ieee802_11_parse_elems(mgmt->u.beacon.variable, len - baselen, &elems);

	ieee80211_rx_bss_info(dev, mgmt, len, rx_status, &elems, 1);

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	if (sdata->vif.type != IEEE80211_IF_TYPE_STA)
		return;
	ifsta = &sdata->u.sta;

	if (!(ifsta->flags & IEEE80211_STA_ASSOCIATED) ||
	    memcmp(ifsta->bssid, mgmt->bssid, ETH_ALEN) != 0)
		return;

	ieee80211_sta_wmm_params(dev, ifsta, elems.wmm_param,
				 elems.wmm_param_len);

	/* Do not send changes to driver if we are scanning. This removes
	 * requirement that driver's bss_info_changed function needs to be
	 * atomic. */
	if (local->sta_sw_scanning || local->sta_hw_scanning)
		return;

	if (elems.erp_info && elems.erp_info_len >= 1)
		changed |= ieee80211_handle_erp_ie(sdata, elems.erp_info[0]);
	else {
		u16 capab = le16_to_cpu(mgmt->u.beacon.capab_info);
		changed |= ieee80211_handle_protect_preamb(sdata, false,
				(capab & WLAN_CAPABILITY_SHORT_PREAMBLE) != 0);
	}

	if (elems.ht_cap_elem && elems.ht_info_elem &&
	    elems.wmm_param && conf->flags & IEEE80211_CONF_SUPPORT_HT_MODE) {
		struct ieee80211_ht_bss_info bss_info;

		ieee80211_ht_addt_info_ie_to_ht_bss_info(
				(struct ieee80211_ht_addt_info *)
				elems.ht_info_elem, &bss_info);
		changed |= ieee80211_handle_ht(local, 1, &conf->ht_conf,
					       &bss_info);
	}
	rcu_read_lock();
	chanctx_conf = rcu_dereference(sdata->vif.chanctx_conf);
	if (!chanctx_conf) {
		rcu_read_unlock();
		return;
	}

	if (rx_status->freq != chanctx_conf->def.chan->center_freq) {
		rcu_read_unlock();
		return;
	}
	chan = chanctx_conf->def.chan;
	rcu_read_unlock();

	if (ifmgd->assoc_data && ifmgd->assoc_data->need_beacon &&
	    ether_addr_equal(mgmt->bssid, ifmgd->assoc_data->bss->bssid)) {
		ieee802_11_parse_elems(mgmt->u.beacon.variable,
				       len - baselen, false, &elems);

		ieee80211_rx_bss_info(sdata, mgmt, len, rx_status, &elems);
		if (elems.tim && !elems.parse_error) {
			const struct ieee80211_tim_ie *tim_ie = elems.tim;
			ifmgd->dtim_period = tim_ie->dtim_period;
		}
		ifmgd->have_beacon = true;
		ifmgd->assoc_data->need_beacon = false;
		if (ieee80211_hw_check(&local->hw, TIMING_BEACON_ONLY)) {
			sdata->vif.bss_conf.sync_tsf =
				le64_to_cpu(mgmt->u.beacon.timestamp);
			sdata->vif.bss_conf.sync_device_ts =
				rx_status->device_timestamp;
			if (elems.tim)
				sdata->vif.bss_conf.sync_dtim_count =
					elems.tim->dtim_count;
			else
				sdata->vif.bss_conf.sync_dtim_count = 0;
		}
		/* continue assoc process */
		ifmgd->assoc_data->timeout = jiffies;
		ifmgd->assoc_data->timeout_started = true;
		run_again(sdata, ifmgd->assoc_data->timeout);
		return;
	}

	if (!ifmgd->associated ||
	    !ether_addr_equal(mgmt->bssid, ifmgd->associated->bssid))
		return;
	bssid = ifmgd->associated->bssid;

	/* Track average RSSI from the Beacon frames of the current AP */
	if (ifmgd->flags & IEEE80211_STA_RESET_SIGNAL_AVE) {
		ifmgd->flags &= ~IEEE80211_STA_RESET_SIGNAL_AVE;
		ewma_beacon_signal_init(&ifmgd->ave_beacon_signal);
		ifmgd->last_cqm_event_signal = 0;
		ifmgd->count_beacon_signal = 1;
		ifmgd->last_ave_beacon_signal = 0;
	} else {
		ifmgd->count_beacon_signal++;
	}

	ewma_beacon_signal_add(&ifmgd->ave_beacon_signal, -rx_status->signal);

	if (ifmgd->rssi_min_thold != ifmgd->rssi_max_thold &&
	    ifmgd->count_beacon_signal >= IEEE80211_SIGNAL_AVE_MIN_COUNT) {
		int sig = -ewma_beacon_signal_read(&ifmgd->ave_beacon_signal);
		int last_sig = ifmgd->last_ave_beacon_signal;
		struct ieee80211_event event = {
			.type = RSSI_EVENT,
		};

		/*
		 * if signal crosses either of the boundaries, invoke callback
		 * with appropriate parameters
		 */
		if (sig > ifmgd->rssi_max_thold &&
		    (last_sig <= ifmgd->rssi_min_thold || last_sig == 0)) {
			ifmgd->last_ave_beacon_signal = sig;
			event.u.rssi.data = RSSI_EVENT_HIGH;
			drv_event_callback(local, sdata, &event);
		} else if (sig < ifmgd->rssi_min_thold &&
			   (last_sig >= ifmgd->rssi_max_thold ||
			   last_sig == 0)) {
			ifmgd->last_ave_beacon_signal = sig;
			event.u.rssi.data = RSSI_EVENT_LOW;
			drv_event_callback(local, sdata, &event);
		}
	}

	if (bss_conf->cqm_rssi_thold &&
	    ifmgd->count_beacon_signal >= IEEE80211_SIGNAL_AVE_MIN_COUNT &&
	    !(sdata->vif.driver_flags & IEEE80211_VIF_SUPPORTS_CQM_RSSI)) {
		int sig = -ewma_beacon_signal_read(&ifmgd->ave_beacon_signal);
		int last_event = ifmgd->last_cqm_event_signal;
		int thold = bss_conf->cqm_rssi_thold;
		int hyst = bss_conf->cqm_rssi_hyst;

		if (sig < thold &&
		    (last_event == 0 || sig < last_event - hyst)) {
			ifmgd->last_cqm_event_signal = sig;
			ieee80211_cqm_rssi_notify(
				&sdata->vif,
				NL80211_CQM_RSSI_THRESHOLD_EVENT_LOW,
				GFP_KERNEL);
		} else if (sig > thold &&
			   (last_event == 0 || sig > last_event + hyst)) {
			ifmgd->last_cqm_event_signal = sig;
			ieee80211_cqm_rssi_notify(
				&sdata->vif,
				NL80211_CQM_RSSI_THRESHOLD_EVENT_HIGH,
				GFP_KERNEL);
		}
	}

	if (ifmgd->flags & IEEE80211_STA_CONNECTION_POLL) {
		mlme_dbg_ratelimited(sdata,
				     "cancelling AP probe due to a received beacon\n");
		ieee80211_reset_ap_probe(sdata);
	}

	/*
	 * Push the beacon loss detection into the future since
	 * we are processing a beacon from the AP just now.
	 */
	ieee80211_sta_reset_beacon_monitor(sdata);

	ncrc = crc32_be(0, (void *)&mgmt->u.beacon.beacon_int, 4);
	ncrc = ieee802_11_parse_elems_crc(mgmt->u.beacon.variable,
					  len - baselen, false, &elems,
					  care_about_ies, ncrc);

	if (ieee80211_hw_check(&local->hw, PS_NULLFUNC_STACK) &&
	    ieee80211_check_tim(elems.tim, elems.tim_len, ifmgd->aid)) {
		if (local->hw.conf.dynamic_ps_timeout > 0) {
			if (local->hw.conf.flags & IEEE80211_CONF_PS) {
				local->hw.conf.flags &= ~IEEE80211_CONF_PS;
				ieee80211_hw_config(local,
						    IEEE80211_CONF_CHANGE_PS);
			}
			ieee80211_send_nullfunc(local, sdata, false);
		} else if (!local->pspolling && sdata->u.mgd.powersave) {
			local->pspolling = true;

			/*
			 * Here is assumed that the driver will be
			 * able to send ps-poll frame and receive a
			 * response even though power save mode is
			 * enabled, but some drivers might require
			 * to disable power save here. This needs
			 * to be investigated.
			 */
			ieee80211_send_pspoll(local, sdata);
		}
	}

	if (sdata->vif.p2p) {
		struct ieee80211_p2p_noa_attr noa = {};
		int ret;

		ret = cfg80211_get_p2p_attr(mgmt->u.beacon.variable,
					    len - baselen,
					    IEEE80211_P2P_ATTR_ABSENCE_NOTICE,
					    (u8 *) &noa, sizeof(noa));
		if (ret >= 2) {
			if (sdata->u.mgd.p2p_noa_index != noa.index) {
				/* valid noa_attr and index changed */
				sdata->u.mgd.p2p_noa_index = noa.index;
				memcpy(&bss_conf->p2p_noa_attr, &noa, sizeof(noa));
				changed |= BSS_CHANGED_P2P_PS;
				/*
				 * make sure we update all information, the CRC
				 * mechanism doesn't look at P2P attributes.
				 */
				ifmgd->beacon_crc_valid = false;
			}
		} else if (sdata->u.mgd.p2p_noa_index != -1) {
			/* noa_attr not found and we had valid noa_attr before */
			sdata->u.mgd.p2p_noa_index = -1;
			memset(&bss_conf->p2p_noa_attr, 0, sizeof(bss_conf->p2p_noa_attr));
			changed |= BSS_CHANGED_P2P_PS;
			ifmgd->beacon_crc_valid = false;
		}
	}

	if (ifmgd->csa_waiting_bcn)
		ieee80211_chswitch_post_beacon(sdata);

	/*
	 * Update beacon timing and dtim count on every beacon appearance. This
	 * will allow the driver to use the most updated values. Do it before
	 * comparing this one with last received beacon.
	 * IMPORTANT: These parameters would possibly be out of sync by the time
	 * the driver will use them. The synchronized view is currently
	 * guaranteed only in certain callbacks.
	 */
	if (ieee80211_hw_check(&local->hw, TIMING_BEACON_ONLY)) {
		sdata->vif.bss_conf.sync_tsf =
			le64_to_cpu(mgmt->u.beacon.timestamp);
		sdata->vif.bss_conf.sync_device_ts =
			rx_status->device_timestamp;
		if (elems.tim)
			sdata->vif.bss_conf.sync_dtim_count =
				elems.tim->dtim_count;
		else
			sdata->vif.bss_conf.sync_dtim_count = 0;
	}

	if (ncrc == ifmgd->beacon_crc && ifmgd->beacon_crc_valid)
		return;
	ifmgd->beacon_crc = ncrc;
	ifmgd->beacon_crc_valid = true;

	ieee80211_rx_bss_info(sdata, mgmt, len, rx_status, &elems);

	ieee80211_sta_process_chanswitch(sdata, rx_status->mactime,
					 rx_status->device_timestamp,
					 &elems, true);

	if (!(ifmgd->flags & IEEE80211_STA_DISABLE_WMM) &&
	    ieee80211_sta_wmm_params(local, sdata, elems.wmm_param,
				     elems.wmm_param_len))
		changed |= BSS_CHANGED_QOS;

	/*
	 * If we haven't had a beacon before, tell the driver about the
	 * DTIM period (and beacon timing if desired) now.
	 */
	if (!ifmgd->have_beacon) {
		/* a few bogus AP send dtim_period = 0 or no TIM IE */
		if (elems.tim)
			bss_conf->dtim_period = elems.tim->dtim_period ?: 1;
		else
			bss_conf->dtim_period = 1;

		changed |= BSS_CHANGED_BEACON_INFO;
		ifmgd->have_beacon = true;

		mutex_lock(&local->iflist_mtx);
		ieee80211_recalc_ps(local);
		mutex_unlock(&local->iflist_mtx);

		ieee80211_recalc_ps_vif(sdata);
	}

	if (elems.erp_info) {
		erp_valid = true;
		erp_value = elems.erp_info[0];
	} else {
		erp_valid = false;
	}
	changed |= ieee80211_handle_bss_capability(sdata,
			le16_to_cpu(mgmt->u.beacon.capab_info),
			erp_valid, erp_value);

	mutex_lock(&local->sta_mtx);
	sta = sta_info_get(sdata, bssid);

	if (ieee80211_config_bw(sdata, sta,
				elems.ht_cap_elem, elems.ht_operation,
				elems.vht_operation, bssid, &changed)) {
		mutex_unlock(&local->sta_mtx);
		ieee80211_set_disassoc(sdata, IEEE80211_STYPE_DEAUTH,
				       WLAN_REASON_DEAUTH_LEAVING,
				       true, deauth_buf);
		ieee80211_report_disconnect(sdata, deauth_buf,
					    sizeof(deauth_buf), true,
					    WLAN_REASON_DEAUTH_LEAVING);
		return;
	}

	if (sta && elems.opmode_notif)
		ieee80211_vht_handle_opmode(sdata, sta, *elems.opmode_notif,
					    rx_status->band);
	mutex_unlock(&local->sta_mtx);

	changed |= ieee80211_handle_pwr_constr(sdata, chan, mgmt,
					       elems.country_elem,
					       elems.country_elem_len,
					       elems.pwr_constr_elem,
					       elems.cisco_dtpc_elem);

	ieee80211_bss_info_change_notify(sdata, changed);
}


static void ieee80211_rx_mgmt_probe_req(struct net_device *dev,
					struct ieee80211_if_sta *ifsta,
					struct ieee80211_mgmt *mgmt,
					size_t len,
					struct ieee80211_rx_status *rx_status)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	int tx_last_beacon;
	struct sk_buff *skb;
	struct ieee80211_mgmt *resp;
	u8 *pos, *end;
	DECLARE_MAC_BUF(mac);
#ifdef CONFIG_MAC80211_IBSS_DEBUG
	DECLARE_MAC_BUF(mac2);
	DECLARE_MAC_BUF(mac3);
#endif

	if (sdata->vif.type != IEEE80211_IF_TYPE_IBSS ||
	    ifsta->state != IEEE80211_IBSS_JOINED ||
	    len < 24 + 2 || !ifsta->probe_resp)
		return;

	if (local->ops->tx_last_beacon)
		tx_last_beacon = local->ops->tx_last_beacon(local_to_hw(local));
	else
		tx_last_beacon = 1;

#ifdef CONFIG_MAC80211_IBSS_DEBUG
	printk(KERN_DEBUG "%s: RX ProbeReq SA=%s DA=%s BSSID="
	       "%s (tx_last_beacon=%d)\n",
	       dev->name, print_mac(mac, mgmt->sa), print_mac(mac2, mgmt->da),
	       print_mac(mac3, mgmt->bssid), tx_last_beacon);
#endif /* CONFIG_MAC80211_IBSS_DEBUG */

	if (!tx_last_beacon)
		return;

	if (memcmp(mgmt->bssid, ifsta->bssid, ETH_ALEN) != 0 &&
	    memcmp(mgmt->bssid, "\xff\xff\xff\xff\xff\xff", ETH_ALEN) != 0)
		return;

	end = ((u8 *) mgmt) + len;
	pos = mgmt->u.probe_req.variable;
	if (pos[0] != WLAN_EID_SSID ||
	    pos + 2 + pos[1] > end) {
#ifdef CONFIG_MAC80211_IBSS_DEBUG
		printk(KERN_DEBUG "%s: Invalid SSID IE in ProbeReq "
		       "from %s\n",
		       dev->name, print_mac(mac, mgmt->sa));
#endif
		return;
	}
	if (pos[1] != 0 &&
	    (pos[1] != ifsta->ssid_len ||
	     memcmp(pos + 2, ifsta->ssid, ifsta->ssid_len) != 0)) {
		/* Ignore ProbeReq for foreign SSID */
		return;
	}

	/* Reply with ProbeResp */
	skb = skb_copy(ifsta->probe_resp, GFP_KERNEL);
	if (!skb)
		return;

	resp = (struct ieee80211_mgmt *) skb->data;
	memcpy(resp->da, mgmt->sa, ETH_ALEN);
#ifdef CONFIG_MAC80211_IBSS_DEBUG
	printk(KERN_DEBUG "%s: Sending ProbeResp to %s\n",
	       dev->name, print_mac(mac, resp->da));
#endif /* CONFIG_MAC80211_IBSS_DEBUG */
	ieee80211_sta_tx(dev, skb, 0);
}

static void ieee80211_rx_mgmt_action(struct net_device *dev,
				     struct ieee80211_if_sta *ifsta,
				     struct ieee80211_mgmt *mgmt,
				     size_t len,
				     struct ieee80211_rx_status *rx_status)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);

	if (len < IEEE80211_MIN_ACTION_SIZE)
		return;

	switch (mgmt->u.action.category) {
	case WLAN_CATEGORY_SPECTRUM_MGMT:
		if (local->hw.conf.channel->band != IEEE80211_BAND_5GHZ)
			break;
		switch (mgmt->u.action.u.chan_switch.action_code) {
		case WLAN_ACTION_SPCT_MSR_REQ:
			if (len < (IEEE80211_MIN_ACTION_SIZE +
				   sizeof(mgmt->u.action.u.measurement)))
				break;
			ieee80211_sta_process_measurement_req(dev, mgmt, len);
			break;
		}
		break;
	case WLAN_CATEGORY_BACK:
		switch (mgmt->u.action.u.addba_req.action_code) {
		case WLAN_ACTION_ADDBA_REQ:
			if (len < (IEEE80211_MIN_ACTION_SIZE +
				   sizeof(mgmt->u.action.u.addba_req)))
				break;
			ieee80211_sta_process_addba_request(dev, mgmt, len);
			break;
		case WLAN_ACTION_ADDBA_RESP:
			if (len < (IEEE80211_MIN_ACTION_SIZE +
				   sizeof(mgmt->u.action.u.addba_resp)))
				break;
			ieee80211_sta_process_addba_resp(dev, mgmt, len);
			break;
		case WLAN_ACTION_DELBA:
			if (len < (IEEE80211_MIN_ACTION_SIZE +
				   sizeof(mgmt->u.action.u.delba)))
				break;
			ieee80211_sta_process_delba(dev, mgmt, len);
			break;
		}
		break;
	case PLINK_CATEGORY:
		if (ieee80211_vif_is_mesh(&sdata->vif))
			mesh_rx_plink_frame(dev, mgmt, len, rx_status);
		break;
	case MESH_PATH_SEL_CATEGORY:
		if (ieee80211_vif_is_mesh(&sdata->vif))
			mesh_rx_path_sel_frame(dev, mgmt, len);
		break;
	}
}

void ieee80211_sta_rx_mgmt(struct net_device *dev, struct sk_buff *skb,
			   struct ieee80211_rx_status *rx_status)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_if_sta *ifsta;
	struct ieee80211_mgmt *mgmt;
	u16 fc;

	if (skb->len < 24)
		goto fail;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	ifsta = &sdata->u.sta;

	mgmt = (struct ieee80211_mgmt *) skb->data;
	fc = le16_to_cpu(mgmt->frame_control);

	switch (fc & IEEE80211_FCTL_STYPE) {
	case IEEE80211_STYPE_PROBE_REQ:
	case IEEE80211_STYPE_PROBE_RESP:
	case IEEE80211_STYPE_BEACON:
	case IEEE80211_STYPE_ACTION:
		memcpy(skb->cb, rx_status, sizeof(*rx_status));
	case IEEE80211_STYPE_AUTH:
	case IEEE80211_STYPE_ASSOC_RESP:
	case IEEE80211_STYPE_REASSOC_RESP:
	case IEEE80211_STYPE_DEAUTH:
	case IEEE80211_STYPE_DISASSOC:
		skb_queue_tail(&ifsta->skb_queue, skb);
		queue_work(local->hw.workqueue, &ifsta->work);
		return;
	}

 fail:
	kfree_skb(skb);
}


static void ieee80211_sta_rx_queued_mgmt(struct net_device *dev,
					 struct sk_buff *skb)
{
	struct ieee80211_rx_status *rx_status;
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_if_sta *ifsta;
	struct ieee80211_mgmt *mgmt;
	u16 fc;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	ifsta = &sdata->u.sta;
void ieee80211_sta_rx_queued_mgmt(struct ieee80211_sub_if_data *sdata,
				  struct sk_buff *skb)
{
	struct ieee80211_rx_status *rx_status;
	struct ieee80211_mgmt *mgmt;
	u16 fc;
	struct ieee802_11_elems elems;
	int ies_len;

	rx_status = (struct ieee80211_rx_status *) skb->cb;
	mgmt = (struct ieee80211_mgmt *) skb->data;
	fc = le16_to_cpu(mgmt->frame_control);

	switch (fc & IEEE80211_FCTL_STYPE) {
	case IEEE80211_STYPE_PROBE_REQ:
		ieee80211_rx_mgmt_probe_req(dev, ifsta, mgmt, skb->len,
					    rx_status);
		break;
	case IEEE80211_STYPE_PROBE_RESP:
		ieee80211_rx_mgmt_probe_resp(dev, mgmt, skb->len, rx_status);
		break;
	case IEEE80211_STYPE_BEACON:
		ieee80211_rx_mgmt_beacon(dev, mgmt, skb->len, rx_status);
		break;
	case IEEE80211_STYPE_AUTH:
		ieee80211_rx_mgmt_auth(dev, ifsta, mgmt, skb->len);
		break;
	case IEEE80211_STYPE_ASSOC_RESP:
		ieee80211_rx_mgmt_assoc_resp(sdata, ifsta, mgmt, skb->len, 0);
		break;
	case IEEE80211_STYPE_REASSOC_RESP:
		ieee80211_rx_mgmt_assoc_resp(sdata, ifsta, mgmt, skb->len, 1);
		break;
	case IEEE80211_STYPE_DEAUTH:
		ieee80211_rx_mgmt_deauth(dev, ifsta, mgmt, skb->len);
		break;
	case IEEE80211_STYPE_DISASSOC:
		ieee80211_rx_mgmt_disassoc(dev, ifsta, mgmt, skb->len);
		break;
	case IEEE80211_STYPE_ACTION:
		ieee80211_rx_mgmt_action(dev, ifsta, mgmt, skb->len, rx_status);
		break;
	}

	kfree_skb(skb);
}


ieee80211_rx_result
ieee80211_sta_rx_scan(struct net_device *dev, struct sk_buff *skb,
		      struct ieee80211_rx_status *rx_status)
{
	struct ieee80211_mgmt *mgmt;
	__le16 fc;

	if (skb->len < 2)
		return RX_DROP_UNUSABLE;

	mgmt = (struct ieee80211_mgmt *) skb->data;
	fc = mgmt->frame_control;

	if (ieee80211_is_ctl(fc))
		return RX_CONTINUE;

	if (skb->len < 24)
		return RX_DROP_MONITOR;

	if (ieee80211_is_probe_resp(fc)) {
		ieee80211_rx_mgmt_probe_resp(dev, mgmt, skb->len, rx_status);
		dev_kfree_skb(skb);
		return RX_QUEUED;
	}

	if (ieee80211_is_beacon(fc)) {
		ieee80211_rx_mgmt_beacon(dev, mgmt, skb->len, rx_status);
		dev_kfree_skb(skb);
		return RX_QUEUED;
	}

	return RX_CONTINUE;
}


static int ieee80211_sta_active_ibss(struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	int active = 0;
	struct sta_info *sta;
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	rcu_read_lock();

	list_for_each_entry_rcu(sta, &local->sta_list, list) {
		if (sta->sdata == sdata &&
		    time_after(sta->last_rx + IEEE80211_IBSS_MERGE_INTERVAL,
			       jiffies)) {
			active++;
			break;
		}
	}

	rcu_read_unlock();

	return active;
}


static void ieee80211_sta_expire(struct net_device *dev, unsigned long exp_time)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct sta_info *sta, *tmp;
	LIST_HEAD(tmp_list);
	DECLARE_MAC_BUF(mac);
	unsigned long flags;

	spin_lock_irqsave(&local->sta_lock, flags);
	list_for_each_entry_safe(sta, tmp, &local->sta_list, list)
		if (time_after(jiffies, sta->last_rx + exp_time)) {
#ifdef CONFIG_MAC80211_IBSS_DEBUG
			printk(KERN_DEBUG "%s: expiring inactive STA %s\n",
			       dev->name, print_mac(mac, sta->addr));
#endif
			__sta_info_unlink(&sta);
			if (sta)
				list_add(&sta->list, &tmp_list);
		}
	spin_unlock_irqrestore(&local->sta_lock, flags);

	list_for_each_entry_safe(sta, tmp, &tmp_list, list)
		sta_info_destroy(sta);
}


static void ieee80211_sta_merge_ibss(struct net_device *dev,
				     struct ieee80211_if_sta *ifsta)
{
	mod_timer(&ifsta->timer, jiffies + IEEE80211_IBSS_MERGE_INTERVAL);

	ieee80211_sta_expire(dev, IEEE80211_IBSS_INACTIVITY_LIMIT);
	if (ieee80211_sta_active_ibss(dev))
		return;

	printk(KERN_DEBUG "%s: No active IBSS STAs - trying to scan for other "
	       "IBSS networks with same SSID (merge)\n", dev->name);
	ieee80211_sta_req_scan(dev, ifsta->ssid, ifsta->ssid_len);
}


#ifdef CONFIG_MAC80211_MESH
static void ieee80211_mesh_housekeeping(struct net_device *dev,
			   struct ieee80211_if_sta *ifsta)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	bool free_plinks;

	ieee80211_sta_expire(dev, IEEE80211_MESH_PEER_INACTIVITY_LIMIT);
	mesh_path_expire(dev);

	free_plinks = mesh_plink_availables(sdata);
	if (free_plinks != sdata->u.sta.accepting_plinks)
		ieee80211_if_config(sdata, IEEE80211_IFCC_BEACON);

	mod_timer(&ifsta->timer, jiffies +
			IEEE80211_MESH_HOUSEKEEPING_INTERVAL);
}


void ieee80211_start_mesh(struct net_device *dev)
{
	struct ieee80211_if_sta *ifsta;
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	ifsta = &sdata->u.sta;
	ifsta->state = IEEE80211_MESH_UP;
	ieee80211_sta_timer((unsigned long)sdata);
	ieee80211_if_config(sdata, IEEE80211_IFCC_BEACON);
}
#endif


void ieee80211_sta_timer(unsigned long data)
{
	struct ieee80211_sub_if_data *sdata =
		(struct ieee80211_sub_if_data *) data;
	struct ieee80211_if_sta *ifsta = &sdata->u.sta;
	struct ieee80211_local *local = wdev_priv(&sdata->wdev);

	set_bit(IEEE80211_STA_REQ_RUN, &ifsta->request);
	queue_work(local->hw.workqueue, &ifsta->work);
}

void ieee80211_sta_work(struct work_struct *work)
{
	struct ieee80211_sub_if_data *sdata =
		container_of(work, struct ieee80211_sub_if_data, u.sta.work);
	struct net_device *dev = sdata->dev;
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_if_sta *ifsta;
	struct sk_buff *skb;

	if (!netif_running(dev))
		return;

	if (local->sta_sw_scanning || local->sta_hw_scanning)
		return;

	if (WARN_ON(sdata->vif.type != IEEE80211_IF_TYPE_STA &&
		    sdata->vif.type != IEEE80211_IF_TYPE_IBSS &&
		    sdata->vif.type != IEEE80211_IF_TYPE_MESH_POINT))
		return;
	ifsta = &sdata->u.sta;

	while ((skb = skb_dequeue(&ifsta->skb_queue)))
		ieee80211_sta_rx_queued_mgmt(dev, skb);

#ifdef CONFIG_MAC80211_MESH
	if (ifsta->preq_queue_len &&
	    time_after(jiffies,
		       ifsta->last_preq + msecs_to_jiffies(ifsta->mshcfg.dot11MeshHWMPpreqMinInterval)))
		mesh_path_start_discovery(dev);
#endif

	if (ifsta->state != IEEE80211_AUTHENTICATE &&
	    ifsta->state != IEEE80211_ASSOCIATE &&
	    test_and_clear_bit(IEEE80211_STA_REQ_SCAN, &ifsta->request)) {
		if (ifsta->scan_ssid_len)
			ieee80211_sta_start_scan(dev, ifsta->scan_ssid, ifsta->scan_ssid_len);
		else
			ieee80211_sta_start_scan(dev, NULL, 0);
		return;
	}

	if (test_and_clear_bit(IEEE80211_STA_REQ_AUTH, &ifsta->request)) {
		if (ieee80211_sta_config_auth(dev, ifsta))
			return;
		clear_bit(IEEE80211_STA_REQ_RUN, &ifsta->request);
	} else if (!test_and_clear_bit(IEEE80211_STA_REQ_RUN, &ifsta->request))
		return;

	switch (ifsta->state) {
	case IEEE80211_DISABLED:
		break;
	case IEEE80211_AUTHENTICATE:
		ieee80211_authenticate(dev, ifsta);
		break;
	case IEEE80211_ASSOCIATE:
		ieee80211_associate(dev, ifsta);
		break;
	case IEEE80211_ASSOCIATED:
		ieee80211_associated(dev, ifsta);
		break;
	case IEEE80211_IBSS_SEARCH:
		ieee80211_sta_find_ibss(dev, ifsta);
		break;
	case IEEE80211_IBSS_JOINED:
		ieee80211_sta_merge_ibss(dev, ifsta);
		break;
#ifdef CONFIG_MAC80211_MESH
	case IEEE80211_MESH_UP:
		ieee80211_mesh_housekeeping(dev, ifsta);
		break;
#endif
	default:
		WARN_ON(1);
		break;
	}

	if (ieee80211_privacy_mismatch(dev, ifsta)) {
		printk(KERN_DEBUG "%s: privacy configuration mismatch and "
		       "mixed-cell disabled - disassociate\n", dev->name);

		ieee80211_send_disassoc(dev, ifsta, WLAN_REASON_UNSPECIFIED);
		ieee80211_set_disassoc(dev, ifsta, 0);
	}
}


static void ieee80211_sta_reset_auth(struct net_device *dev,
				     struct ieee80211_if_sta *ifsta)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);

	if (local->ops->reset_tsf) {
		/* Reset own TSF to allow time synchronization work. */
		local->ops->reset_tsf(local_to_hw(local));
	}

	ifsta->wmm_last_param_set = -1; /* allow any WMM update */


	if (ifsta->auth_algs & IEEE80211_AUTH_ALG_OPEN)
		ifsta->auth_alg = WLAN_AUTH_OPEN;
	else if (ifsta->auth_algs & IEEE80211_AUTH_ALG_SHARED_KEY)
		ifsta->auth_alg = WLAN_AUTH_SHARED_KEY;
	else if (ifsta->auth_algs & IEEE80211_AUTH_ALG_LEAP)
		ifsta->auth_alg = WLAN_AUTH_LEAP;
	else
		ifsta->auth_alg = WLAN_AUTH_OPEN;
	ifsta->auth_transaction = -1;
	ifsta->flags &= ~IEEE80211_STA_ASSOCIATED;
	ifsta->auth_tries = ifsta->assoc_tries = 0;
	netif_carrier_off(dev);
}


void ieee80211_sta_req_auth(struct net_device *dev,
			    struct ieee80211_if_sta *ifsta)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (sdata->vif.type != IEEE80211_IF_TYPE_STA)
		return;

	if ((ifsta->flags & (IEEE80211_STA_BSSID_SET |
				IEEE80211_STA_AUTO_BSSID_SEL)) &&
	    (ifsta->flags & (IEEE80211_STA_SSID_SET |
				IEEE80211_STA_AUTO_SSID_SEL))) {
		set_bit(IEEE80211_STA_REQ_AUTH, &ifsta->request);
		queue_work(local->hw.workqueue, &ifsta->work);
	}
}

static int ieee80211_sta_match_ssid(struct ieee80211_if_sta *ifsta,
				    const char *ssid, int ssid_len)
{
	int tmp, hidden_ssid;

	if (ssid_len == ifsta->ssid_len &&
	    !memcmp(ifsta->ssid, ssid, ssid_len))
		return 1;

	if (ifsta->flags & IEEE80211_STA_AUTO_BSSID_SEL)
		return 0;

	hidden_ssid = 1;
	tmp = ssid_len;
	while (tmp--) {
		if (ssid[tmp] != '\0') {
			hidden_ssid = 0;
			break;
		}
	}

	if (hidden_ssid && ifsta->ssid_len == ssid_len)
		return 1;

	if (ssid_len == 1 && ssid[0] == ' ')
		return 1;

	return 0;
}

static int ieee80211_sta_config_auth(struct net_device *dev,
				     struct ieee80211_if_sta *ifsta)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_sta_bss *bss, *selected = NULL;
	int top_rssi = 0, freq;

	spin_lock_bh(&local->sta_bss_lock);
	freq = local->oper_channel->center_freq;
	list_for_each_entry(bss, &local->sta_bss_list, list) {
		if (!(bss->capability & WLAN_CAPABILITY_ESS))
			continue;

		if ((ifsta->flags & (IEEE80211_STA_AUTO_SSID_SEL |
			IEEE80211_STA_AUTO_BSSID_SEL |
			IEEE80211_STA_AUTO_CHANNEL_SEL)) &&
		    (!!(bss->capability & WLAN_CAPABILITY_PRIVACY) ^
		     !!sdata->default_key))
			continue;

		if (!(ifsta->flags & IEEE80211_STA_AUTO_CHANNEL_SEL) &&
		    bss->freq != freq)
			continue;

		if (!(ifsta->flags & IEEE80211_STA_AUTO_BSSID_SEL) &&
		    memcmp(bss->bssid, ifsta->bssid, ETH_ALEN))
			continue;

		if (!(ifsta->flags & IEEE80211_STA_AUTO_SSID_SEL) &&
		    !ieee80211_sta_match_ssid(ifsta, bss->ssid, bss->ssid_len))
			continue;

		if (!selected || top_rssi < bss->signal) {
			selected = bss;
			top_rssi = bss->signal;
		}
	}
	if (selected)
		atomic_inc(&selected->users);
	spin_unlock_bh(&local->sta_bss_lock);

	if (selected) {
		ieee80211_set_freq(dev, selected->freq);
		if (!(ifsta->flags & IEEE80211_STA_SSID_SET))
			ieee80211_sta_set_ssid(dev, selected->ssid,
					       selected->ssid_len);
		ieee80211_sta_set_bssid(dev, selected->bssid);
		ieee80211_sta_def_wmm_params(dev, selected, 0);
		ieee80211_rx_bss_put(local, selected);
		ifsta->state = IEEE80211_AUTHENTICATE;
		ieee80211_sta_reset_auth(dev, ifsta);
		return 0;
	} else {
		if (ifsta->state != IEEE80211_AUTHENTICATE) {
			if (ifsta->flags & IEEE80211_STA_AUTO_SSID_SEL)
				ieee80211_sta_start_scan(dev, NULL, 0);
			else
				ieee80211_sta_start_scan(dev, ifsta->ssid,
							 ifsta->ssid_len);
			ifsta->state = IEEE80211_AUTHENTICATE;
			set_bit(IEEE80211_STA_REQ_AUTH, &ifsta->request);
		} else
			ifsta->state = IEEE80211_DISABLED;
	}
	return -1;
}


static int ieee80211_sta_create_ibss(struct net_device *dev,
				     struct ieee80211_if_sta *ifsta)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sta_bss *bss;
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_supported_band *sband;
	u8 bssid[ETH_ALEN], *pos;
	int i;
	int ret;
	DECLARE_MAC_BUF(mac);

#if 0
	/* Easier testing, use fixed BSSID. */
	memset(bssid, 0xfe, ETH_ALEN);
#else
	/* Generate random, not broadcast, locally administered BSSID. Mix in
	 * own MAC address to make sure that devices that do not have proper
	 * random number generator get different BSSID. */
	get_random_bytes(bssid, ETH_ALEN);
	for (i = 0; i < ETH_ALEN; i++)
		bssid[i] ^= dev->dev_addr[i];
	bssid[0] &= ~0x01;
	bssid[0] |= 0x02;
#endif

	printk(KERN_DEBUG "%s: Creating new IBSS network, BSSID %s\n",
	       dev->name, print_mac(mac, bssid));

	bss = ieee80211_rx_bss_add(dev, bssid,
				   local->hw.conf.channel->center_freq,
				   sdata->u.sta.ssid, sdata->u.sta.ssid_len);
	if (!bss)
		return -ENOMEM;

	bss->band = local->hw.conf.channel->band;
	sband = local->hw.wiphy->bands[bss->band];

	if (local->hw.conf.beacon_int == 0)
		local->hw.conf.beacon_int = 100;
	bss->beacon_int = local->hw.conf.beacon_int;
	bss->last_update = jiffies;
	bss->capability = WLAN_CAPABILITY_IBSS;

	if (sdata->default_key)
		bss->capability |= WLAN_CAPABILITY_PRIVACY;
	else
		sdata->drop_unencrypted = 0;

	bss->supp_rates_len = sband->n_bitrates;
	pos = bss->supp_rates;
	for (i = 0; i < sband->n_bitrates; i++) {
		int rate = sband->bitrates[i].bitrate;
		*pos++ = (u8) (rate / 5);
	}

	ret = ieee80211_sta_join_ibss(dev, ifsta, bss);
	ieee80211_rx_bss_put(local, bss);
	return ret;
}


static int ieee80211_sta_find_ibss(struct net_device *dev,
				   struct ieee80211_if_sta *ifsta)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sta_bss *bss;
	int found = 0;
	u8 bssid[ETH_ALEN];
	int active_ibss;
	DECLARE_MAC_BUF(mac);
	DECLARE_MAC_BUF(mac2);

	if (ifsta->ssid_len == 0)
		return -EINVAL;

	active_ibss = ieee80211_sta_active_ibss(dev);
#ifdef CONFIG_MAC80211_IBSS_DEBUG
	printk(KERN_DEBUG "%s: sta_find_ibss (active_ibss=%d)\n",
	       dev->name, active_ibss);
#endif /* CONFIG_MAC80211_IBSS_DEBUG */
	spin_lock_bh(&local->sta_bss_lock);
	list_for_each_entry(bss, &local->sta_bss_list, list) {
		if (ifsta->ssid_len != bss->ssid_len ||
		    memcmp(ifsta->ssid, bss->ssid, bss->ssid_len) != 0
		    || !(bss->capability & WLAN_CAPABILITY_IBSS))
			continue;
#ifdef CONFIG_MAC80211_IBSS_DEBUG
		printk(KERN_DEBUG "   bssid=%s found\n",
		       print_mac(mac, bss->bssid));
#endif /* CONFIG_MAC80211_IBSS_DEBUG */
		memcpy(bssid, bss->bssid, ETH_ALEN);
		found = 1;
		if (active_ibss || memcmp(bssid, ifsta->bssid, ETH_ALEN) != 0)
			break;
	}
	spin_unlock_bh(&local->sta_bss_lock);

#ifdef CONFIG_MAC80211_IBSS_DEBUG
	if (found)
		printk(KERN_DEBUG "   sta_find_ibss: selected %s current "
		       "%s\n", print_mac(mac, bssid),
		       print_mac(mac2, ifsta->bssid));
#endif /* CONFIG_MAC80211_IBSS_DEBUG */

	if (found && memcmp(ifsta->bssid, bssid, ETH_ALEN) != 0) {
		int ret;
		int search_freq;

		if (ifsta->flags & IEEE80211_STA_AUTO_CHANNEL_SEL)
			search_freq = bss->freq;
		else
			search_freq = local->hw.conf.channel->center_freq;

		bss = ieee80211_rx_bss_get(dev, bssid, search_freq,
					   ifsta->ssid, ifsta->ssid_len);
		if (!bss)
			goto dont_join;

		printk(KERN_DEBUG "%s: Selected IBSS BSSID %s"
		       " based on configured SSID\n",
		       dev->name, print_mac(mac, bssid));
		ret = ieee80211_sta_join_ibss(dev, ifsta, bss);
		ieee80211_rx_bss_put(local, bss);
		return ret;
	}

dont_join:
#ifdef CONFIG_MAC80211_IBSS_DEBUG
	printk(KERN_DEBUG "   did not try to join ibss\n");
#endif /* CONFIG_MAC80211_IBSS_DEBUG */

	/* Selected IBSS not found in current scan results - try to scan */
	if (ifsta->state == IEEE80211_IBSS_JOINED &&
	    !ieee80211_sta_active_ibss(dev)) {
		mod_timer(&ifsta->timer, jiffies +
				      IEEE80211_IBSS_MERGE_INTERVAL);
	} else if (time_after(jiffies, local->last_scan_completed +
			      IEEE80211_SCAN_INTERVAL)) {
		printk(KERN_DEBUG "%s: Trigger new scan to find an IBSS to "
		       "join\n", dev->name);
		return ieee80211_sta_req_scan(dev, ifsta->ssid,
					      ifsta->ssid_len);
	} else if (ifsta->state != IEEE80211_IBSS_JOINED) {
		int interval = IEEE80211_SCAN_INTERVAL;

		if (time_after(jiffies, ifsta->ibss_join_req +
			       IEEE80211_IBSS_JOIN_TIMEOUT)) {
			if ((ifsta->flags & IEEE80211_STA_CREATE_IBSS) &&
			    (!(local->oper_channel->flags &
					IEEE80211_CHAN_NO_IBSS)))
				return ieee80211_sta_create_ibss(dev, ifsta);
			if (ifsta->flags & IEEE80211_STA_CREATE_IBSS) {
				printk(KERN_DEBUG "%s: IBSS not allowed on"
				       " %d MHz\n", dev->name,
				       local->hw.conf.channel->center_freq);
			}

			/* No IBSS found - decrease scan interval and continue
			 * scanning. */
			interval = IEEE80211_SCAN_INTERVAL_SLOW;
		}

		ifsta->state = IEEE80211_IBSS_SEARCH;
		mod_timer(&ifsta->timer, jiffies + interval);
		return 0;
	}

	return 0;
}


int ieee80211_sta_set_ssid(struct net_device *dev, char *ssid, size_t len)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_if_sta *ifsta;
	int res;

	if (len > IEEE80211_MAX_SSID_LEN)
		return -EINVAL;

	ifsta = &sdata->u.sta;

	if (ifsta->ssid_len != len || memcmp(ifsta->ssid, ssid, len) != 0) {
		memset(ifsta->ssid, 0, sizeof(ifsta->ssid));
		memcpy(ifsta->ssid, ssid, len);
		ifsta->ssid_len = len;
		ifsta->flags &= ~IEEE80211_STA_PREV_BSSID_SET;

		res = 0;
		/*
		 * Hack! MLME code needs to be cleaned up to have different
		 * entry points for configuration and internal selection change
		 */
		if (netif_running(sdata->dev))
			res = ieee80211_if_config(sdata, IEEE80211_IFCC_SSID);
		if (res) {
			printk(KERN_DEBUG "%s: Failed to config new SSID to "
			       "the low-level driver\n", dev->name);
			return res;
		}
	}

	if (len)
		ifsta->flags |= IEEE80211_STA_SSID_SET;
	else
		ifsta->flags &= ~IEEE80211_STA_SSID_SET;

	if (sdata->vif.type == IEEE80211_IF_TYPE_IBSS &&
	    !(ifsta->flags & IEEE80211_STA_BSSID_SET)) {
		ifsta->ibss_join_req = jiffies;
		ifsta->state = IEEE80211_IBSS_SEARCH;
		return ieee80211_sta_find_ibss(dev, ifsta);
	}

	return 0;
}


int ieee80211_sta_get_ssid(struct net_device *dev, char *ssid, size_t *len)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_if_sta *ifsta = &sdata->u.sta;
	memcpy(ssid, ifsta->ssid, ifsta->ssid_len);
	*len = ifsta->ssid_len;
	return 0;
}


int ieee80211_sta_set_bssid(struct net_device *dev, u8 *bssid)
{
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_if_sta *ifsta;
	int res;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	ifsta = &sdata->u.sta;

	if (memcmp(ifsta->bssid, bssid, ETH_ALEN) != 0) {
		memcpy(ifsta->bssid, bssid, ETH_ALEN);
		res = 0;
		/*
		 * Hack! See also ieee80211_sta_set_ssid.
		 */
		if (netif_running(sdata->dev))
			res = ieee80211_if_config(sdata, IEEE80211_IFCC_BSSID);
		if (res) {
			printk(KERN_DEBUG "%s: Failed to config new BSSID to "
			       "the low-level driver\n", dev->name);
			return res;
		}
	}

	if (is_valid_ether_addr(bssid))
		ifsta->flags |= IEEE80211_STA_BSSID_SET;
	else
		ifsta->flags &= ~IEEE80211_STA_BSSID_SET;

	return 0;
}


static void ieee80211_send_nullfunc(struct ieee80211_local *local,
				    struct ieee80211_sub_if_data *sdata,
				    int powersave)
{
	struct sk_buff *skb;
	struct ieee80211_hdr *nullfunc;
	__le16 fc;

	skb = dev_alloc_skb(local->hw.extra_tx_headroom + 24);
	if (!skb) {
		printk(KERN_DEBUG "%s: failed to allocate buffer for nullfunc "
		       "frame\n", sdata->dev->name);
		return;
	}
	skb_reserve(skb, local->hw.extra_tx_headroom);

	nullfunc = (struct ieee80211_hdr *) skb_put(skb, 24);
	memset(nullfunc, 0, 24);
	fc = cpu_to_le16(IEEE80211_FTYPE_DATA | IEEE80211_STYPE_NULLFUNC |
			 IEEE80211_FCTL_TODS);
	if (powersave)
		fc |= cpu_to_le16(IEEE80211_FCTL_PM);
	nullfunc->frame_control = fc;
	memcpy(nullfunc->addr1, sdata->u.sta.bssid, ETH_ALEN);
	memcpy(nullfunc->addr2, sdata->dev->dev_addr, ETH_ALEN);
	memcpy(nullfunc->addr3, sdata->u.sta.bssid, ETH_ALEN);

	ieee80211_sta_tx(sdata->dev, skb, 0);
}


static void ieee80211_restart_sta_timer(struct ieee80211_sub_if_data *sdata)
{
	if (sdata->vif.type == IEEE80211_IF_TYPE_STA ||
	    ieee80211_vif_is_mesh(&sdata->vif))
		ieee80211_sta_timer((unsigned long)sdata);
}

void ieee80211_scan_completed(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct net_device *dev = local->scan_dev;
	struct ieee80211_sub_if_data *sdata;
	union iwreq_data wrqu;

	local->last_scan_completed = jiffies;
	memset(&wrqu, 0, sizeof(wrqu));
	wireless_send_event(dev, SIOCGIWSCAN, &wrqu, NULL);

	if (local->sta_hw_scanning) {
		local->sta_hw_scanning = 0;
		if (ieee80211_hw_config(local))
			printk(KERN_DEBUG "%s: failed to restore operational "
			       "channel after scan\n", dev->name);
		/* Restart STA timer for HW scan case */
		rcu_read_lock();
		list_for_each_entry_rcu(sdata, &local->interfaces, list)
			ieee80211_restart_sta_timer(sdata);
		rcu_read_unlock();

		goto done;
	}

	local->sta_sw_scanning = 0;
	if (ieee80211_hw_config(local))
		printk(KERN_DEBUG "%s: failed to restore operational "
		       "channel after scan\n", dev->name);


	netif_tx_lock_bh(local->mdev);
	netif_addr_lock(local->mdev);
	local->filter_flags &= ~FIF_BCN_PRBRESP_PROMISC;
	local->ops->configure_filter(local_to_hw(local),
				     FIF_BCN_PRBRESP_PROMISC,
				     &local->filter_flags,
				     local->mdev->mc_count,
				     local->mdev->mc_list);

	netif_addr_unlock(local->mdev);
	netif_tx_unlock_bh(local->mdev);

	rcu_read_lock();
	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		/* Tell AP we're back */
		if (sdata->vif.type == IEEE80211_IF_TYPE_STA &&
		    sdata->u.sta.flags & IEEE80211_STA_ASSOCIATED)
			ieee80211_send_nullfunc(local, sdata, 0);

		ieee80211_restart_sta_timer(sdata);

		netif_wake_queue(sdata->dev);
	}
	rcu_read_unlock();

done:
	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	if (sdata->vif.type == IEEE80211_IF_TYPE_IBSS) {
		struct ieee80211_if_sta *ifsta = &sdata->u.sta;
		if (!(ifsta->flags & IEEE80211_STA_BSSID_SET) ||
		    (!(ifsta->state == IEEE80211_IBSS_JOINED) &&
		    !ieee80211_sta_active_ibss(dev)))
			ieee80211_sta_find_ibss(dev, ifsta);
	}
}
EXPORT_SYMBOL(ieee80211_scan_completed);

void ieee80211_sta_scan_work(struct work_struct *work)
{
	struct ieee80211_local *local =
		container_of(work, struct ieee80211_local, scan_work.work);
	struct net_device *dev = local->scan_dev;
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *chan;
	int skip;
	unsigned long next_delay = 0;

	if (!local->sta_sw_scanning)
		return;

	switch (local->scan_state) {
	case SCAN_SET_CHANNEL:
		/*
		 * Get current scan band. scan_band may be IEEE80211_NUM_BANDS
		 * after we successfully scanned the last channel of the last
		 * band (and the last band is supported by the hw)
		 */
		if (local->scan_band < IEEE80211_NUM_BANDS)
			sband = local->hw.wiphy->bands[local->scan_band];
		else
			sband = NULL;

		/*
		 * If we are at an unsupported band and have more bands
		 * left to scan, advance to the next supported one.
		 */
		while (!sband && local->scan_band < IEEE80211_NUM_BANDS - 1) {
			local->scan_band++;
			sband = local->hw.wiphy->bands[local->scan_band];
			local->scan_channel_idx = 0;
		}

		/* if no more bands/channels left, complete scan */
		if (!sband || local->scan_channel_idx >= sband->n_channels) {
			ieee80211_scan_completed(local_to_hw(local));
			return;
		}
		skip = 0;
		chan = &sband->channels[local->scan_channel_idx];

		if (chan->flags & IEEE80211_CHAN_DISABLED ||
		    (sdata->vif.type == IEEE80211_IF_TYPE_IBSS &&
		     chan->flags & IEEE80211_CHAN_NO_IBSS))
			skip = 1;

		if (!skip) {
			local->scan_channel = chan;
			if (ieee80211_hw_config(local)) {
				printk(KERN_DEBUG "%s: failed to set freq to "
				       "%d MHz for scan\n", dev->name,
				       chan->center_freq);
				skip = 1;
			}
		}

		/* advance state machine to next channel/band */
		local->scan_channel_idx++;
		if (local->scan_channel_idx >= sband->n_channels) {
			/*
			 * scan_band may end up == IEEE80211_NUM_BANDS, but
			 * we'll catch that case above and complete the scan
			 * if that is the case.
			 */
			local->scan_band++;
			local->scan_channel_idx = 0;
		}

		if (skip)
			break;

		next_delay = IEEE80211_PROBE_DELAY +
			     usecs_to_jiffies(local->hw.channel_change_time);
		local->scan_state = SCAN_SEND_PROBE;
		break;
	case SCAN_SEND_PROBE:
		next_delay = IEEE80211_PASSIVE_CHANNEL_TIME;
		local->scan_state = SCAN_SET_CHANNEL;

		if (local->scan_channel->flags & IEEE80211_CHAN_PASSIVE_SCAN)
			break;
		ieee80211_send_probe_req(dev, NULL, local->scan_ssid,
					 local->scan_ssid_len);
		next_delay = IEEE80211_CHANNEL_TIME;
		break;
	}

	if (local->sta_sw_scanning)
		queue_delayed_work(local->hw.workqueue, &local->scan_work,
				   next_delay);
}


static int ieee80211_sta_start_scan(struct net_device *dev,
				    u8 *ssid, size_t ssid_len)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata;

	if (ssid_len > IEEE80211_MAX_SSID_LEN)
		return -EINVAL;

	/* MLME-SCAN.request (page 118)  page 144 (11.1.3.1)
	 * BSSType: INFRASTRUCTURE, INDEPENDENT, ANY_BSS
	 * BSSID: MACAddress
	 * SSID
	 * ScanType: ACTIVE, PASSIVE
	 * ProbeDelay: delay (in microseconds) to be used prior to transmitting
	 *    a Probe frame during active scanning
	 * ChannelList
	 * MinChannelTime (>= ProbeDelay), in TU
	 * MaxChannelTime: (>= MinChannelTime), in TU
	 */

	 /* MLME-SCAN.confirm
	  * BSSDescriptionSet
	  * ResultCode: SUCCESS, INVALID_PARAMETERS
	 */

	if (local->sta_sw_scanning || local->sta_hw_scanning) {
		if (local->scan_dev == dev)
			return 0;
		return -EBUSY;
	}

	if (local->ops->hw_scan) {
		int rc = local->ops->hw_scan(local_to_hw(local),
					     ssid, ssid_len);
		if (!rc) {
			local->sta_hw_scanning = 1;
			local->scan_dev = dev;
		}
		return rc;
	}

	local->sta_sw_scanning = 1;

	rcu_read_lock();
	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		netif_stop_queue(sdata->dev);
		if (sdata->vif.type == IEEE80211_IF_TYPE_STA &&
		    (sdata->u.sta.flags & IEEE80211_STA_ASSOCIATED))
			ieee80211_send_nullfunc(local, sdata, 1);
	}
	rcu_read_unlock();

	if (ssid) {
		local->scan_ssid_len = ssid_len;
		memcpy(local->scan_ssid, ssid, ssid_len);
	} else
		local->scan_ssid_len = 0;
	local->scan_state = SCAN_SET_CHANNEL;
	local->scan_channel_idx = 0;
	local->scan_band = IEEE80211_BAND_2GHZ;
	local->scan_dev = dev;

	netif_addr_lock_bh(local->mdev);
	local->filter_flags |= FIF_BCN_PRBRESP_PROMISC;
	local->ops->configure_filter(local_to_hw(local),
				     FIF_BCN_PRBRESP_PROMISC,
				     &local->filter_flags,
				     local->mdev->mc_count,
				     local->mdev->mc_list);
	netif_addr_unlock_bh(local->mdev);

	/* TODO: start scan as soon as all nullfunc frames are ACKed */
	queue_delayed_work(local->hw.workqueue, &local->scan_work,
			   IEEE80211_CHANNEL_TIME);

	return 0;
}


int ieee80211_sta_req_scan(struct net_device *dev, u8 *ssid, size_t ssid_len)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_if_sta *ifsta = &sdata->u.sta;
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);

	if (sdata->vif.type != IEEE80211_IF_TYPE_STA)
		return ieee80211_sta_start_scan(dev, ssid, ssid_len);

	if (local->sta_sw_scanning || local->sta_hw_scanning) {
		if (local->scan_dev == dev)
			return 0;
		return -EBUSY;
	}

	ifsta->scan_ssid_len = ssid_len;
	if (ssid_len)
		memcpy(ifsta->scan_ssid, ssid, ssid_len);
	set_bit(IEEE80211_STA_REQ_SCAN, &ifsta->request);
	queue_work(local->hw.workqueue, &ifsta->work);
	return 0;
}

static char *
ieee80211_sta_scan_result(struct net_device *dev,
			  struct iw_request_info *info,
			  struct ieee80211_sta_bss *bss,
			  char *current_ev, char *end_buf)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct iw_event iwe;

	if (time_after(jiffies,
		       bss->last_update + IEEE80211_SCAN_RESULT_EXPIRE))
		return current_ev;

	memset(&iwe, 0, sizeof(iwe));
	iwe.cmd = SIOCGIWAP;
	iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
	memcpy(iwe.u.ap_addr.sa_data, bss->bssid, ETH_ALEN);
	current_ev = iwe_stream_add_event(info, current_ev, end_buf, &iwe,
					  IW_EV_ADDR_LEN);

	memset(&iwe, 0, sizeof(iwe));
	iwe.cmd = SIOCGIWESSID;
	if (bss_mesh_cfg(bss)) {
		iwe.u.data.length = bss_mesh_id_len(bss);
		iwe.u.data.flags = 1;
		current_ev = iwe_stream_add_point(info, current_ev, end_buf,
						  &iwe, bss_mesh_id(bss));
	} else {
		iwe.u.data.length = bss->ssid_len;
		iwe.u.data.flags = 1;
		current_ev = iwe_stream_add_point(info, current_ev, end_buf,
						  &iwe, bss->ssid);
	}

	if (bss->capability & (WLAN_CAPABILITY_ESS | WLAN_CAPABILITY_IBSS)
	    || bss_mesh_cfg(bss)) {
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = SIOCGIWMODE;
		if (bss_mesh_cfg(bss))
			iwe.u.mode = IW_MODE_MESH;
		else if (bss->capability & WLAN_CAPABILITY_ESS)
			iwe.u.mode = IW_MODE_MASTER;
		else
			iwe.u.mode = IW_MODE_ADHOC;
		current_ev = iwe_stream_add_event(info, current_ev, end_buf,
						  &iwe, IW_EV_UINT_LEN);
	}

	memset(&iwe, 0, sizeof(iwe));
	iwe.cmd = SIOCGIWFREQ;
	iwe.u.freq.m = ieee80211_frequency_to_channel(bss->freq);
	iwe.u.freq.e = 0;
	current_ev = iwe_stream_add_event(info, current_ev, end_buf, &iwe,
					  IW_EV_FREQ_LEN);

	memset(&iwe, 0, sizeof(iwe));
	iwe.cmd = SIOCGIWFREQ;
	iwe.u.freq.m = bss->freq;
	iwe.u.freq.e = 6;
	current_ev = iwe_stream_add_event(info, current_ev, end_buf, &iwe,
					  IW_EV_FREQ_LEN);
	memset(&iwe, 0, sizeof(iwe));
	iwe.cmd = IWEVQUAL;
	iwe.u.qual.qual = bss->qual;
	iwe.u.qual.level = bss->signal;
	iwe.u.qual.noise = bss->noise;
	iwe.u.qual.updated = local->wstats_flags;
	current_ev = iwe_stream_add_event(info, current_ev, end_buf, &iwe,
					  IW_EV_QUAL_LEN);

	memset(&iwe, 0, sizeof(iwe));
	iwe.cmd = SIOCGIWENCODE;
	if (bss->capability & WLAN_CAPABILITY_PRIVACY)
		iwe.u.data.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
	else
		iwe.u.data.flags = IW_ENCODE_DISABLED;
	iwe.u.data.length = 0;
	current_ev = iwe_stream_add_point(info, current_ev, end_buf,
					  &iwe, "");

	if (bss && bss->wpa_ie) {
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = IWEVGENIE;
		iwe.u.data.length = bss->wpa_ie_len;
		current_ev = iwe_stream_add_point(info, current_ev, end_buf,
						  &iwe, bss->wpa_ie);
	}

	if (bss && bss->rsn_ie) {
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = IWEVGENIE;
		iwe.u.data.length = bss->rsn_ie_len;
		current_ev = iwe_stream_add_point(info, current_ev, end_buf,
						  &iwe, bss->rsn_ie);
	}

	if (bss && bss->ht_ie) {
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = IWEVGENIE;
		iwe.u.data.length = bss->ht_ie_len;
		current_ev = iwe_stream_add_point(info, current_ev, end_buf,
						  &iwe, bss->ht_ie);
	}

	if (bss && bss->supp_rates_len > 0) {
		/* display all supported rates in readable format */
		char *p = current_ev + iwe_stream_lcp_len(info);
		int i;

		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = SIOCGIWRATE;
		/* Those two flags are ignored... */
		iwe.u.bitrate.fixed = iwe.u.bitrate.disabled = 0;

		for (i = 0; i < bss->supp_rates_len; i++) {
			iwe.u.bitrate.value = ((bss->supp_rates[i] &
							0x7f) * 500000);
			p = iwe_stream_add_value(info, current_ev, p,
					end_buf, &iwe, IW_EV_PARAM_LEN);
		}
		current_ev = p;
	}

	if (bss) {
		char *buf;
		buf = kmalloc(30, GFP_ATOMIC);
		if (buf) {
			memset(&iwe, 0, sizeof(iwe));
			iwe.cmd = IWEVCUSTOM;
			sprintf(buf, "tsf=%016llx", (unsigned long long)(bss->timestamp));
			iwe.u.data.length = strlen(buf);
			current_ev = iwe_stream_add_point(info, current_ev,
							  end_buf,
							  &iwe, buf);
			memset(&iwe, 0, sizeof(iwe));
			iwe.cmd = IWEVCUSTOM;
			sprintf(buf, " Last beacon: %dms ago",
				jiffies_to_msecs(jiffies - bss->last_update));
			iwe.u.data.length = strlen(buf);
			current_ev = iwe_stream_add_point(info, current_ev,
							  end_buf, &iwe, buf);
			kfree(buf);
		}
	}

	if (bss_mesh_cfg(bss)) {
		char *buf;
		u8 *cfg = bss_mesh_cfg(bss);
		buf = kmalloc(50, GFP_ATOMIC);
		if (buf) {
			memset(&iwe, 0, sizeof(iwe));
			iwe.cmd = IWEVCUSTOM;
			sprintf(buf, "Mesh network (version %d)", cfg[0]);
			iwe.u.data.length = strlen(buf);
			current_ev = iwe_stream_add_point(info, current_ev,
							  end_buf,
							  &iwe, buf);
			sprintf(buf, "Path Selection Protocol ID: "
				"0x%02X%02X%02X%02X", cfg[1], cfg[2], cfg[3],
							cfg[4]);
			iwe.u.data.length = strlen(buf);
			current_ev = iwe_stream_add_point(info, current_ev,
							  end_buf,
							  &iwe, buf);
			sprintf(buf, "Path Selection Metric ID: "
				"0x%02X%02X%02X%02X", cfg[5], cfg[6], cfg[7],
							cfg[8]);
			iwe.u.data.length = strlen(buf);
			current_ev = iwe_stream_add_point(info, current_ev,
							  end_buf,
							  &iwe, buf);
			sprintf(buf, "Congestion Control Mode ID: "
				"0x%02X%02X%02X%02X", cfg[9], cfg[10],
							cfg[11], cfg[12]);
			iwe.u.data.length = strlen(buf);
			current_ev = iwe_stream_add_point(info, current_ev,
							  end_buf,
							  &iwe, buf);
			sprintf(buf, "Channel Precedence: "
				"0x%02X%02X%02X%02X", cfg[13], cfg[14],
							cfg[15], cfg[16]);
			iwe.u.data.length = strlen(buf);
			current_ev = iwe_stream_add_point(info, current_ev,
							  end_buf,
							  &iwe, buf);
			kfree(buf);
		}
	}

	return current_ev;
}


int ieee80211_sta_scan_results(struct net_device *dev,
			       struct iw_request_info *info,
			       char *buf, size_t len)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	char *current_ev = buf;
	char *end_buf = buf + len;
	struct ieee80211_sta_bss *bss;

	spin_lock_bh(&local->sta_bss_lock);
	list_for_each_entry(bss, &local->sta_bss_list, list) {
		if (buf + len - current_ev <= IW_EV_ADDR_LEN) {
			spin_unlock_bh(&local->sta_bss_lock);
			return -E2BIG;
		}
		current_ev = ieee80211_sta_scan_result(dev, info, bss,
						       current_ev, end_buf);
	}
	spin_unlock_bh(&local->sta_bss_lock);
	return current_ev - buf;
}


int ieee80211_sta_set_extra_ie(struct net_device *dev, char *ie, size_t len)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_if_sta *ifsta = &sdata->u.sta;

	kfree(ifsta->extra_ie);
	if (len == 0) {
		ifsta->extra_ie = NULL;
		ifsta->extra_ie_len = 0;
		return 0;
	}
	ifsta->extra_ie = kmalloc(len, GFP_KERNEL);
	if (!ifsta->extra_ie) {
		ifsta->extra_ie_len = 0;
		return -ENOMEM;
	}
	memcpy(ifsta->extra_ie, ie, len);
	ifsta->extra_ie_len = len;
	return 0;
}


struct sta_info *ieee80211_ibss_add_sta(struct net_device *dev,
					struct sk_buff *skb, u8 *bssid,
					u8 *addr, u64 supp_rates)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct sta_info *sta;
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	DECLARE_MAC_BUF(mac);
	int band = local->hw.conf.channel->band;

	/* TODO: Could consider removing the least recently used entry and
	 * allow new one to be added. */
	if (local->num_sta >= IEEE80211_IBSS_MAX_STA_ENTRIES) {
		if (net_ratelimit()) {
			printk(KERN_DEBUG "%s: No room for a new IBSS STA "
			       "entry %s\n", dev->name, print_mac(mac, addr));
		}
		return NULL;
	}

	if (compare_ether_addr(bssid, sdata->u.sta.bssid))
		return NULL;

#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
	printk(KERN_DEBUG "%s: Adding new IBSS station %s (dev=%s)\n",
	       wiphy_name(local->hw.wiphy), print_mac(mac, addr), dev->name);
#endif

	sta = sta_info_alloc(sdata, addr, GFP_ATOMIC);
	if (!sta)
		return NULL;

	set_sta_flags(sta, WLAN_STA_AUTHORIZED);

	if (supp_rates)
		sta->supp_rates[band] = supp_rates;
	else
		sta->supp_rates[band] = sdata->u.sta.supp_rates_bits[band];

	rate_control_rate_init(sta, local);

	if (sta_info_insert(sta))
		return NULL;

	return sta;
}


int ieee80211_sta_deauthenticate(struct net_device *dev, u16 reason)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_if_sta *ifsta = &sdata->u.sta;

	printk(KERN_DEBUG "%s: deauthenticating by local choice (reason=%d)\n",
	       dev->name, reason);

	if (sdata->vif.type != IEEE80211_IF_TYPE_STA &&
	    sdata->vif.type != IEEE80211_IF_TYPE_IBSS)
		return -EINVAL;

	ieee80211_send_deauth(dev, ifsta, reason);
	ieee80211_set_disassoc(dev, ifsta, 1);
	return 0;
}


int ieee80211_sta_disassociate(struct net_device *dev, u16 reason)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_if_sta *ifsta = &sdata->u.sta;

	printk(KERN_DEBUG "%s: disassociating by local choice (reason=%d)\n",
	       dev->name, reason);

	if (sdata->vif.type != IEEE80211_IF_TYPE_STA)
		return -EINVAL;

	if (!(ifsta->flags & IEEE80211_STA_ASSOCIATED))
		return -1;

	ieee80211_send_disassoc(dev, ifsta, reason);
	ieee80211_set_disassoc(dev, ifsta, 0);
	return 0;
}

void ieee80211_notify_mac(struct ieee80211_hw *hw,
			  enum ieee80211_notification_types  notif_type)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_sub_if_data *sdata;

	switch (notif_type) {
	case IEEE80211_NOTIFY_RE_ASSOC:
		rcu_read_lock();
		list_for_each_entry_rcu(sdata, &local->interfaces, list) {
			if (sdata->vif.type != IEEE80211_IF_TYPE_STA)
				continue;

			ieee80211_sta_req_auth(sdata->dev, &sdata->u.sta);
		}
		rcu_read_unlock();
		break;
	}
}
EXPORT_SYMBOL(ieee80211_notify_mac);
	sdata_lock(sdata);

	switch (fc & IEEE80211_FCTL_STYPE) {
	case IEEE80211_STYPE_BEACON:
		ieee80211_rx_mgmt_beacon(sdata, mgmt, skb->len, rx_status);
		break;
	case IEEE80211_STYPE_PROBE_RESP:
		ieee80211_rx_mgmt_probe_resp(sdata, skb);
		break;
	case IEEE80211_STYPE_AUTH:
		ieee80211_rx_mgmt_auth(sdata, mgmt, skb->len);
		break;
	case IEEE80211_STYPE_DEAUTH:
		ieee80211_rx_mgmt_deauth(sdata, mgmt, skb->len);
		break;
	case IEEE80211_STYPE_DISASSOC:
		ieee80211_rx_mgmt_disassoc(sdata, mgmt, skb->len);
		break;
	case IEEE80211_STYPE_ASSOC_RESP:
	case IEEE80211_STYPE_REASSOC_RESP:
		ieee80211_rx_mgmt_assoc_resp(sdata, mgmt, skb->len);
		break;
	case IEEE80211_STYPE_ACTION:
		if (mgmt->u.action.category == WLAN_CATEGORY_SPECTRUM_MGMT) {
			ies_len = skb->len -
				  offsetof(struct ieee80211_mgmt,
					   u.action.u.chan_switch.variable);

			if (ies_len < 0)
				break;

			ieee802_11_parse_elems(
				mgmt->u.action.u.chan_switch.variable,
				ies_len, true, &elems);

			if (elems.parse_error)
				break;

			ieee80211_sta_process_chanswitch(sdata,
						 rx_status->mactime,
						 rx_status->device_timestamp,
						 &elems, false);
		} else if (mgmt->u.action.category == WLAN_CATEGORY_PUBLIC) {
			ies_len = skb->len -
				  offsetof(struct ieee80211_mgmt,
					   u.action.u.ext_chan_switch.variable);

			if (ies_len < 0)
				break;

			ieee802_11_parse_elems(
				mgmt->u.action.u.ext_chan_switch.variable,
				ies_len, true, &elems);

			if (elems.parse_error)
				break;

			/* for the handling code pretend this was also an IE */
			elems.ext_chansw_ie =
				&mgmt->u.action.u.ext_chan_switch.data;

			ieee80211_sta_process_chanswitch(sdata,
						 rx_status->mactime,
						 rx_status->device_timestamp,
						 &elems, false);
		}
		break;
	}
	sdata_unlock(sdata);
}

static void ieee80211_sta_timer(unsigned long data)
{
	struct ieee80211_sub_if_data *sdata =
		(struct ieee80211_sub_if_data *) data;

	ieee80211_queue_work(&sdata->local->hw, &sdata->work);
}

static void ieee80211_sta_connection_lost(struct ieee80211_sub_if_data *sdata,
					  u8 *bssid, u8 reason, bool tx)
{
	u8 frame_buf[IEEE80211_DEAUTH_FRAME_LEN];

	ieee80211_set_disassoc(sdata, IEEE80211_STYPE_DEAUTH, reason,
			       tx, frame_buf);

	ieee80211_report_disconnect(sdata, frame_buf, sizeof(frame_buf), true,
				    reason);
}

static int ieee80211_auth(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_mgd_auth_data *auth_data = ifmgd->auth_data;
	u32 tx_flags = 0;
	u16 trans = 1;
	u16 status = 0;

	sdata_assert_lock(sdata);

	if (WARN_ON_ONCE(!auth_data))
		return -EINVAL;

	auth_data->tries++;

	if (auth_data->tries > IEEE80211_AUTH_MAX_TRIES) {
		sdata_info(sdata, "authentication with %pM timed out\n",
			   auth_data->bss->bssid);

		/*
		 * Most likely AP is not in the range so remove the
		 * bss struct for that AP.
		 */
		cfg80211_unlink_bss(local->hw.wiphy, auth_data->bss);

		return -ETIMEDOUT;
	}

	drv_mgd_prepare_tx(local, sdata);

	sdata_info(sdata, "send auth to %pM (try %d/%d)\n",
		   auth_data->bss->bssid, auth_data->tries,
		   IEEE80211_AUTH_MAX_TRIES);

	auth_data->expected_transaction = 2;

	if (auth_data->algorithm == WLAN_AUTH_SAE) {
		trans = auth_data->sae_trans;
		status = auth_data->sae_status;
		auth_data->expected_transaction = trans;
	}

	if (ieee80211_hw_check(&local->hw, REPORTS_TX_ACK_STATUS))
		tx_flags = IEEE80211_TX_CTL_REQ_TX_STATUS |
			   IEEE80211_TX_INTFL_MLME_CONN_TX;

	ieee80211_send_auth(sdata, trans, auth_data->algorithm, status,
			    auth_data->data, auth_data->data_len,
			    auth_data->bss->bssid,
			    auth_data->bss->bssid, NULL, 0, 0,
			    tx_flags);

	if (tx_flags == 0) {
		auth_data->timeout = jiffies + IEEE80211_AUTH_TIMEOUT;
		auth_data->timeout_started = true;
		run_again(sdata, auth_data->timeout);
	} else {
		auth_data->timeout =
			round_jiffies_up(jiffies + IEEE80211_AUTH_TIMEOUT_LONG);
		auth_data->timeout_started = true;
		run_again(sdata, auth_data->timeout);
	}

	return 0;
}

static int ieee80211_do_assoc(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_mgd_assoc_data *assoc_data = sdata->u.mgd.assoc_data;
	struct ieee80211_local *local = sdata->local;

	sdata_assert_lock(sdata);

	assoc_data->tries++;
	if (assoc_data->tries > IEEE80211_ASSOC_MAX_TRIES) {
		sdata_info(sdata, "association with %pM timed out\n",
			   assoc_data->bss->bssid);

		/*
		 * Most likely AP is not in the range so remove the
		 * bss struct for that AP.
		 */
		cfg80211_unlink_bss(local->hw.wiphy, assoc_data->bss);

		return -ETIMEDOUT;
	}

	sdata_info(sdata, "associate with %pM (try %d/%d)\n",
		   assoc_data->bss->bssid, assoc_data->tries,
		   IEEE80211_ASSOC_MAX_TRIES);
	ieee80211_send_assoc(sdata);

	if (!ieee80211_hw_check(&local->hw, REPORTS_TX_ACK_STATUS)) {
		assoc_data->timeout = jiffies + IEEE80211_ASSOC_TIMEOUT;
		assoc_data->timeout_started = true;
		run_again(sdata, assoc_data->timeout);
	} else {
		assoc_data->timeout =
			round_jiffies_up(jiffies +
					 IEEE80211_ASSOC_TIMEOUT_LONG);
		assoc_data->timeout_started = true;
		run_again(sdata, assoc_data->timeout);
	}

	return 0;
}

void ieee80211_mgd_conn_tx_status(struct ieee80211_sub_if_data *sdata,
				  __le16 fc, bool acked)
{
	struct ieee80211_local *local = sdata->local;

	sdata->u.mgd.status_fc = fc;
	sdata->u.mgd.status_acked = acked;
	sdata->u.mgd.status_received = true;

	ieee80211_queue_work(&local->hw, &sdata->work);
}

void ieee80211_sta_work(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	sdata_lock(sdata);

	if (ifmgd->status_received) {
		__le16 fc = ifmgd->status_fc;
		bool status_acked = ifmgd->status_acked;

		ifmgd->status_received = false;
		if (ifmgd->auth_data && ieee80211_is_auth(fc)) {
			if (status_acked) {
				ifmgd->auth_data->timeout =
					jiffies + IEEE80211_AUTH_TIMEOUT_SHORT;
				run_again(sdata, ifmgd->auth_data->timeout);
			} else {
				ifmgd->auth_data->timeout = jiffies - 1;
			}
			ifmgd->auth_data->timeout_started = true;
		} else if (ifmgd->assoc_data &&
			   (ieee80211_is_assoc_req(fc) ||
			    ieee80211_is_reassoc_req(fc))) {
			if (status_acked) {
				ifmgd->assoc_data->timeout =
					jiffies + IEEE80211_ASSOC_TIMEOUT_SHORT;
				run_again(sdata, ifmgd->assoc_data->timeout);
			} else {
				ifmgd->assoc_data->timeout = jiffies - 1;
			}
			ifmgd->assoc_data->timeout_started = true;
		}
	}

	if (ifmgd->auth_data && ifmgd->auth_data->timeout_started &&
	    time_after(jiffies, ifmgd->auth_data->timeout)) {
		if (ifmgd->auth_data->done) {
			/*
			 * ok ... we waited for assoc but userspace didn't,
			 * so let's just kill the auth data
			 */
			ieee80211_destroy_auth_data(sdata, false);
		} else if (ieee80211_auth(sdata)) {
			u8 bssid[ETH_ALEN];
			struct ieee80211_event event = {
				.type = MLME_EVENT,
				.u.mlme.data = AUTH_EVENT,
				.u.mlme.status = MLME_TIMEOUT,
			};

			memcpy(bssid, ifmgd->auth_data->bss->bssid, ETH_ALEN);

			ieee80211_destroy_auth_data(sdata, false);

			cfg80211_auth_timeout(sdata->dev, bssid);
			drv_event_callback(sdata->local, sdata, &event);
		}
	} else if (ifmgd->auth_data && ifmgd->auth_data->timeout_started)
		run_again(sdata, ifmgd->auth_data->timeout);

	if (ifmgd->assoc_data && ifmgd->assoc_data->timeout_started &&
	    time_after(jiffies, ifmgd->assoc_data->timeout)) {
		if ((ifmgd->assoc_data->need_beacon && !ifmgd->have_beacon) ||
		    ieee80211_do_assoc(sdata)) {
			struct cfg80211_bss *bss = ifmgd->assoc_data->bss;
			struct ieee80211_event event = {
				.type = MLME_EVENT,
				.u.mlme.data = ASSOC_EVENT,
				.u.mlme.status = MLME_TIMEOUT,
			};

			ieee80211_destroy_assoc_data(sdata, false, false);
			cfg80211_assoc_timeout(sdata->dev, bss);
			drv_event_callback(sdata->local, sdata, &event);
		}
	} else if (ifmgd->assoc_data && ifmgd->assoc_data->timeout_started)
		run_again(sdata, ifmgd->assoc_data->timeout);

	if (ifmgd->flags & IEEE80211_STA_CONNECTION_POLL &&
	    ifmgd->associated) {
		u8 bssid[ETH_ALEN];
		int max_tries;

		memcpy(bssid, ifmgd->associated->bssid, ETH_ALEN);

		if (ieee80211_hw_check(&local->hw, REPORTS_TX_ACK_STATUS))
			max_tries = max_nullfunc_tries;
		else
			max_tries = max_probe_tries;

		/* ACK received for nullfunc probing frame */
		if (!ifmgd->probe_send_count)
			ieee80211_reset_ap_probe(sdata);
		else if (ifmgd->nullfunc_failed) {
			if (ifmgd->probe_send_count < max_tries) {
				mlme_dbg(sdata,
					 "No ack for nullfunc frame to AP %pM, try %d/%i\n",
					 bssid, ifmgd->probe_send_count,
					 max_tries);
				ieee80211_mgd_probe_ap_send(sdata);
			} else {
				mlme_dbg(sdata,
					 "No ack for nullfunc frame to AP %pM, disconnecting.\n",
					 bssid);
				ieee80211_sta_connection_lost(sdata, bssid,
					WLAN_REASON_DISASSOC_DUE_TO_INACTIVITY,
					false);
			}
		} else if (time_is_after_jiffies(ifmgd->probe_timeout))
			run_again(sdata, ifmgd->probe_timeout);
		else if (ieee80211_hw_check(&local->hw, REPORTS_TX_ACK_STATUS)) {
			mlme_dbg(sdata,
				 "Failed to send nullfunc to AP %pM after %dms, disconnecting\n",
				 bssid, probe_wait_ms);
			ieee80211_sta_connection_lost(sdata, bssid,
				WLAN_REASON_DISASSOC_DUE_TO_INACTIVITY, false);
		} else if (ifmgd->probe_send_count < max_tries) {
			mlme_dbg(sdata,
				 "No probe response from AP %pM after %dms, try %d/%i\n",
				 bssid, probe_wait_ms,
				 ifmgd->probe_send_count, max_tries);
			ieee80211_mgd_probe_ap_send(sdata);
		} else {
			/*
			 * We actually lost the connection ... or did we?
			 * Let's make sure!
			 */
			wiphy_debug(local->hw.wiphy,
				    "%s: No probe response from AP %pM"
				    " after %dms, disconnecting.\n",
				    sdata->name,
				    bssid, probe_wait_ms);

			ieee80211_sta_connection_lost(sdata, bssid,
				WLAN_REASON_DISASSOC_DUE_TO_INACTIVITY, false);
		}
	}

	sdata_unlock(sdata);
}

static void ieee80211_sta_bcn_mon_timer(unsigned long data)
{
	struct ieee80211_sub_if_data *sdata =
		(struct ieee80211_sub_if_data *) data;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	if (sdata->vif.csa_active && !ifmgd->csa_waiting_bcn)
		return;

	sdata->u.mgd.connection_loss = false;
	ieee80211_queue_work(&sdata->local->hw,
			     &sdata->u.mgd.beacon_connection_loss_work);
}

static void ieee80211_sta_conn_mon_timer(unsigned long data)
{
	struct ieee80211_sub_if_data *sdata =
		(struct ieee80211_sub_if_data *) data;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_local *local = sdata->local;

	if (sdata->vif.csa_active && !ifmgd->csa_waiting_bcn)
		return;

	ieee80211_queue_work(&local->hw, &ifmgd->monitor_work);
}

static void ieee80211_sta_monitor_work(struct work_struct *work)
{
	struct ieee80211_sub_if_data *sdata =
		container_of(work, struct ieee80211_sub_if_data,
			     u.mgd.monitor_work);

	ieee80211_mgd_probe_ap(sdata, false);
}

static void ieee80211_restart_sta_timer(struct ieee80211_sub_if_data *sdata)
{
	if (sdata->vif.type == NL80211_IFTYPE_STATION) {
		__ieee80211_stop_poll(sdata);

		/* let's probe the connection once */
		if (!ieee80211_hw_check(&sdata->local->hw, CONNECTION_MONITOR))
			ieee80211_queue_work(&sdata->local->hw,
					     &sdata->u.mgd.monitor_work);
	}
}

#ifdef CONFIG_PM
void ieee80211_mgd_quiesce(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	u8 frame_buf[IEEE80211_DEAUTH_FRAME_LEN];

	sdata_lock(sdata);

	if (ifmgd->auth_data || ifmgd->assoc_data) {
		const u8 *bssid = ifmgd->auth_data ?
				ifmgd->auth_data->bss->bssid :
				ifmgd->assoc_data->bss->bssid;

		/*
		 * If we are trying to authenticate / associate while suspending,
		 * cfg80211 won't know and won't actually abort those attempts,
		 * thus we need to do that ourselves.
		 */
		ieee80211_send_deauth_disassoc(sdata, bssid,
					       IEEE80211_STYPE_DEAUTH,
					       WLAN_REASON_DEAUTH_LEAVING,
					       false, frame_buf);
		if (ifmgd->assoc_data)
			ieee80211_destroy_assoc_data(sdata, false, true);
		if (ifmgd->auth_data)
			ieee80211_destroy_auth_data(sdata, false);
		cfg80211_tx_mlme_mgmt(sdata->dev, frame_buf,
				      IEEE80211_DEAUTH_FRAME_LEN);
	}

	/* This is a bit of a hack - we should find a better and more generic
	 * solution to this. Normally when suspending, cfg80211 will in fact
	 * deauthenticate. However, it doesn't (and cannot) stop an ongoing
	 * auth (not so important) or assoc (this is the problem) process.
	 *
	 * As a consequence, it can happen that we are in the process of both
	 * associating and suspending, and receive an association response
	 * after cfg80211 has checked if it needs to disconnect, but before
	 * we actually set the flag to drop incoming frames. This will then
	 * cause the workqueue flush to process the association response in
	 * the suspend, resulting in a successful association just before it
	 * tries to remove the interface from the driver, which now though
	 * has a channel context assigned ... this results in issues.
	 *
	 * To work around this (for now) simply deauth here again if we're
	 * now connected.
	 */
	if (ifmgd->associated && !sdata->local->wowlan) {
		u8 bssid[ETH_ALEN];
		struct cfg80211_deauth_request req = {
			.reason_code = WLAN_REASON_DEAUTH_LEAVING,
			.bssid = bssid,
		};

		memcpy(bssid, ifmgd->associated->bssid, ETH_ALEN);
		ieee80211_mgd_deauth(sdata, &req);
	}

	sdata_unlock(sdata);
}

void ieee80211_sta_restart(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	sdata_lock(sdata);
	if (!ifmgd->associated) {
		sdata_unlock(sdata);
		return;
	}

	if (sdata->flags & IEEE80211_SDATA_DISCONNECT_RESUME) {
		sdata->flags &= ~IEEE80211_SDATA_DISCONNECT_RESUME;
		mlme_dbg(sdata, "driver requested disconnect after resume\n");
		ieee80211_sta_connection_lost(sdata,
					      ifmgd->associated->bssid,
					      WLAN_REASON_UNSPECIFIED,
					      true);
		sdata_unlock(sdata);
		return;
	}
	sdata_unlock(sdata);
}
#endif

/* interface setup */
void ieee80211_sta_setup_sdata(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd;

	ifmgd = &sdata->u.mgd;
	INIT_WORK(&ifmgd->monitor_work, ieee80211_sta_monitor_work);
	INIT_WORK(&ifmgd->chswitch_work, ieee80211_chswitch_work);
	INIT_WORK(&ifmgd->beacon_connection_loss_work,
		  ieee80211_beacon_connection_loss_work);
	INIT_WORK(&ifmgd->csa_connection_drop_work,
		  ieee80211_csa_connection_drop_work);
	INIT_WORK(&ifmgd->request_smps_work, ieee80211_request_smps_mgd_work);
	INIT_DELAYED_WORK(&ifmgd->tdls_peer_del_work,
			  ieee80211_tdls_peer_del_work);
	setup_timer(&ifmgd->timer, ieee80211_sta_timer,
		    (unsigned long) sdata);
	setup_timer(&ifmgd->bcn_mon_timer, ieee80211_sta_bcn_mon_timer,
		    (unsigned long) sdata);
	setup_timer(&ifmgd->conn_mon_timer, ieee80211_sta_conn_mon_timer,
		    (unsigned long) sdata);
	setup_timer(&ifmgd->chswitch_timer, ieee80211_chswitch_timer,
		    (unsigned long) sdata);
	INIT_DELAYED_WORK(&ifmgd->tx_tspec_wk,
			  ieee80211_sta_handle_tspec_ac_params_wk);

	ifmgd->flags = 0;
	ifmgd->powersave = sdata->wdev.ps;
	ifmgd->uapsd_queues = sdata->local->hw.uapsd_queues;
	ifmgd->uapsd_max_sp_len = sdata->local->hw.uapsd_max_sp_len;
	ifmgd->p2p_noa_index = -1;

	if (sdata->local->hw.wiphy->features & NL80211_FEATURE_DYNAMIC_SMPS)
		ifmgd->req_smps = IEEE80211_SMPS_AUTOMATIC;
	else
		ifmgd->req_smps = IEEE80211_SMPS_OFF;

	/* Setup TDLS data */
	spin_lock_init(&ifmgd->teardown_lock);
	ifmgd->teardown_skb = NULL;
	ifmgd->orig_teardown_skb = NULL;
}

/* scan finished notification */
void ieee80211_mlme_notify_scan_completed(struct ieee80211_local *local)
{
	struct ieee80211_sub_if_data *sdata;

	/* Restart STA timers */
	rcu_read_lock();
	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		if (ieee80211_sdata_running(sdata))
			ieee80211_restart_sta_timer(sdata);
	}
	rcu_read_unlock();
}

static u8 ieee80211_ht_vht_rx_chains(struct ieee80211_sub_if_data *sdata,
				     struct cfg80211_bss *cbss)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	const u8 *ht_cap_ie, *vht_cap_ie;
	const struct ieee80211_ht_cap *ht_cap;
	const struct ieee80211_vht_cap *vht_cap;
	u8 chains = 1;

	if (ifmgd->flags & IEEE80211_STA_DISABLE_HT)
		return chains;

	ht_cap_ie = ieee80211_bss_get_ie(cbss, WLAN_EID_HT_CAPABILITY);
	if (ht_cap_ie && ht_cap_ie[1] >= sizeof(*ht_cap)) {
		ht_cap = (void *)(ht_cap_ie + 2);
		chains = ieee80211_mcs_to_chains(&ht_cap->mcs);
		/*
		 * TODO: use "Tx Maximum Number Spatial Streams Supported" and
		 *	 "Tx Unequal Modulation Supported" fields.
		 */
	}

	if (ifmgd->flags & IEEE80211_STA_DISABLE_VHT)
		return chains;

	vht_cap_ie = ieee80211_bss_get_ie(cbss, WLAN_EID_VHT_CAPABILITY);
	if (vht_cap_ie && vht_cap_ie[1] >= sizeof(*vht_cap)) {
		u8 nss;
		u16 tx_mcs_map;

		vht_cap = (void *)(vht_cap_ie + 2);
		tx_mcs_map = le16_to_cpu(vht_cap->supp_mcs.tx_mcs_map);
		for (nss = 8; nss > 0; nss--) {
			if (((tx_mcs_map >> (2 * (nss - 1))) & 3) !=
					IEEE80211_VHT_MCS_NOT_SUPPORTED)
				break;
		}
		/* TODO: use "Tx Highest Supported Long GI Data Rate" field? */
		chains = max(chains, nss);
	}

	return chains;
}

static int ieee80211_prep_channel(struct ieee80211_sub_if_data *sdata,
				  struct cfg80211_bss *cbss)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	const struct ieee80211_ht_cap *ht_cap = NULL;
	const struct ieee80211_ht_operation *ht_oper = NULL;
	const struct ieee80211_vht_operation *vht_oper = NULL;
	struct ieee80211_supported_band *sband;
	struct cfg80211_chan_def chandef;
	int ret;
	u32 i;
	bool have_80mhz;

	sband = local->hw.wiphy->bands[cbss->channel->band];

	ifmgd->flags &= ~(IEEE80211_STA_DISABLE_40MHZ |
			  IEEE80211_STA_DISABLE_80P80MHZ |
			  IEEE80211_STA_DISABLE_160MHZ);

	rcu_read_lock();

	if (!(ifmgd->flags & IEEE80211_STA_DISABLE_HT) &&
	    sband->ht_cap.ht_supported) {
		const u8 *ht_oper_ie, *ht_cap_ie;

		ht_oper_ie = ieee80211_bss_get_ie(cbss, WLAN_EID_HT_OPERATION);
		if (ht_oper_ie && ht_oper_ie[1] >= sizeof(*ht_oper))
			ht_oper = (void *)(ht_oper_ie + 2);

		ht_cap_ie = ieee80211_bss_get_ie(cbss, WLAN_EID_HT_CAPABILITY);
		if (ht_cap_ie && ht_cap_ie[1] >= sizeof(*ht_cap))
			ht_cap = (void *)(ht_cap_ie + 2);

		if (!ht_cap) {
			ifmgd->flags |= IEEE80211_STA_DISABLE_HT;
			ht_oper = NULL;
		}
	}

	if (!(ifmgd->flags & IEEE80211_STA_DISABLE_VHT) &&
	    sband->vht_cap.vht_supported) {
		const u8 *vht_oper_ie, *vht_cap;

		vht_oper_ie = ieee80211_bss_get_ie(cbss,
						   WLAN_EID_VHT_OPERATION);
		if (vht_oper_ie && vht_oper_ie[1] >= sizeof(*vht_oper))
			vht_oper = (void *)(vht_oper_ie + 2);
		if (vht_oper && !ht_oper) {
			vht_oper = NULL;
			sdata_info(sdata,
				   "AP advertised VHT without HT, disabling both\n");
			ifmgd->flags |= IEEE80211_STA_DISABLE_HT;
			ifmgd->flags |= IEEE80211_STA_DISABLE_VHT;
		}

		vht_cap = ieee80211_bss_get_ie(cbss, WLAN_EID_VHT_CAPABILITY);
		if (!vht_cap || vht_cap[1] < sizeof(struct ieee80211_vht_cap)) {
			ifmgd->flags |= IEEE80211_STA_DISABLE_VHT;
			vht_oper = NULL;
		}
	}

	/* Allow VHT if at least one channel on the sband supports 80 MHz */
	have_80mhz = false;
	for (i = 0; i < sband->n_channels; i++) {
		if (sband->channels[i].flags & (IEEE80211_CHAN_DISABLED |
						IEEE80211_CHAN_NO_80MHZ))
			continue;

		have_80mhz = true;
		break;
	}

	if (!have_80mhz)
		ifmgd->flags |= IEEE80211_STA_DISABLE_VHT;

	ifmgd->flags |= ieee80211_determine_chantype(sdata, sband,
						     cbss->channel,
						     ht_cap, ht_oper, vht_oper,
						     &chandef, false);

	sdata->needed_rx_chains = min(ieee80211_ht_vht_rx_chains(sdata, cbss),
				      local->rx_chains);

	rcu_read_unlock();

	/* will change later if needed */
	sdata->smps_mode = IEEE80211_SMPS_OFF;

	mutex_lock(&local->mtx);
	/*
	 * If this fails (possibly due to channel context sharing
	 * on incompatible channels, e.g. 80+80 and 160 sharing the
	 * same control channel) try to use a smaller bandwidth.
	 */
	ret = ieee80211_vif_use_channel(sdata, &chandef,
					IEEE80211_CHANCTX_SHARED);

	/* don't downgrade for 5 and 10 MHz channels, though. */
	if (chandef.width == NL80211_CHAN_WIDTH_5 ||
	    chandef.width == NL80211_CHAN_WIDTH_10)
		goto out;

	while (ret && chandef.width != NL80211_CHAN_WIDTH_20_NOHT) {
		ifmgd->flags |= ieee80211_chandef_downgrade(&chandef);
		ret = ieee80211_vif_use_channel(sdata, &chandef,
						IEEE80211_CHANCTX_SHARED);
	}
 out:
	mutex_unlock(&local->mtx);
	return ret;
}

static int ieee80211_prep_connection(struct ieee80211_sub_if_data *sdata,
				     struct cfg80211_bss *cbss, bool assoc,
				     bool override)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_bss *bss = (void *)cbss->priv;
	struct sta_info *new_sta = NULL;
	struct ieee80211_supported_band *sband;
	bool have_sta = false;
	int err;

	sband = local->hw.wiphy->bands[cbss->channel->band];

	if (WARN_ON(!ifmgd->auth_data && !ifmgd->assoc_data))
		return -EINVAL;

	/* If a reconfig is happening, bail out */
	if (local->in_reconfig)
		return -EBUSY;

	if (assoc) {
		rcu_read_lock();
		have_sta = sta_info_get(sdata, cbss->bssid);
		rcu_read_unlock();
	}

	if (!have_sta) {
		new_sta = sta_info_alloc(sdata, cbss->bssid, GFP_KERNEL);
		if (!new_sta)
			return -ENOMEM;
	}

	if (new_sta || override) {
		err = ieee80211_prep_channel(sdata, cbss);
		if (err) {
			if (new_sta)
				sta_info_free(local, new_sta);
			return -EINVAL;
		}
	}

	if (new_sta) {
		u32 rates = 0, basic_rates = 0;
		bool have_higher_than_11mbit = false;
		int min_rate = INT_MAX, min_rate_index = -1;
		struct ieee80211_chanctx_conf *chanctx_conf;
		const struct cfg80211_bss_ies *ies;
		int shift = ieee80211_vif_get_shift(&sdata->vif);
		u32 rate_flags;

		rcu_read_lock();
		chanctx_conf = rcu_dereference(sdata->vif.chanctx_conf);
		if (WARN_ON(!chanctx_conf)) {
			rcu_read_unlock();
			sta_info_free(local, new_sta);
			return -EINVAL;
		}
		rate_flags = ieee80211_chandef_rate_flags(&chanctx_conf->def);
		rcu_read_unlock();

		ieee80211_get_rates(sband, bss->supp_rates,
				    bss->supp_rates_len,
				    &rates, &basic_rates,
				    &have_higher_than_11mbit,
				    &min_rate, &min_rate_index,
				    shift, rate_flags);

		/*
		 * This used to be a workaround for basic rates missing
		 * in the association response frame. Now that we no
		 * longer use the basic rates from there, it probably
		 * doesn't happen any more, but keep the workaround so
		 * in case some *other* APs are buggy in different ways
		 * we can connect -- with a warning.
		 */
		if (!basic_rates && min_rate_index >= 0) {
			sdata_info(sdata,
				   "No basic rates, using min rate instead\n");
			basic_rates = BIT(min_rate_index);
		}

		new_sta->sta.supp_rates[cbss->channel->band] = rates;
		sdata->vif.bss_conf.basic_rates = basic_rates;

		/* cf. IEEE 802.11 9.2.12 */
		if (cbss->channel->band == IEEE80211_BAND_2GHZ &&
		    have_higher_than_11mbit)
			sdata->flags |= IEEE80211_SDATA_OPERATING_GMODE;
		else
			sdata->flags &= ~IEEE80211_SDATA_OPERATING_GMODE;

		memcpy(ifmgd->bssid, cbss->bssid, ETH_ALEN);

		/* set timing information */
		sdata->vif.bss_conf.beacon_int = cbss->beacon_interval;
		rcu_read_lock();
		ies = rcu_dereference(cbss->beacon_ies);
		if (ies) {
			const u8 *tim_ie;

			sdata->vif.bss_conf.sync_tsf = ies->tsf;
			sdata->vif.bss_conf.sync_device_ts =
				bss->device_ts_beacon;
			tim_ie = cfg80211_find_ie(WLAN_EID_TIM,
						  ies->data, ies->len);
			if (tim_ie && tim_ie[1] >= 2)
				sdata->vif.bss_conf.sync_dtim_count = tim_ie[2];
			else
				sdata->vif.bss_conf.sync_dtim_count = 0;
		} else if (!ieee80211_hw_check(&sdata->local->hw,
					       TIMING_BEACON_ONLY)) {
			ies = rcu_dereference(cbss->proberesp_ies);
			/* must be non-NULL since beacon IEs were NULL */
			sdata->vif.bss_conf.sync_tsf = ies->tsf;
			sdata->vif.bss_conf.sync_device_ts =
				bss->device_ts_presp;
			sdata->vif.bss_conf.sync_dtim_count = 0;
		} else {
			sdata->vif.bss_conf.sync_tsf = 0;
			sdata->vif.bss_conf.sync_device_ts = 0;
			sdata->vif.bss_conf.sync_dtim_count = 0;
		}
		rcu_read_unlock();

		/* tell driver about BSSID, basic rates and timing */
		ieee80211_bss_info_change_notify(sdata,
			BSS_CHANGED_BSSID | BSS_CHANGED_BASIC_RATES |
			BSS_CHANGED_BEACON_INT);

		if (assoc)
			sta_info_pre_move_state(new_sta, IEEE80211_STA_AUTH);

		err = sta_info_insert(new_sta);
		new_sta = NULL;
		if (err) {
			sdata_info(sdata,
				   "failed to insert STA entry for the AP (error %d)\n",
				   err);
			return err;
		}
	} else
		WARN_ON_ONCE(!ether_addr_equal(ifmgd->bssid, cbss->bssid));

	/* Cancel scan to ensure that nothing interferes with connection */
	if (local->scanning)
		ieee80211_scan_cancel(local);

	return 0;
}

/* config hooks */
int ieee80211_mgd_auth(struct ieee80211_sub_if_data *sdata,
		       struct cfg80211_auth_request *req)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_mgd_auth_data *auth_data;
	u16 auth_alg;
	int err;

	/* prepare auth data structure */

	switch (req->auth_type) {
	case NL80211_AUTHTYPE_OPEN_SYSTEM:
		auth_alg = WLAN_AUTH_OPEN;
		break;
	case NL80211_AUTHTYPE_SHARED_KEY:
		if (IS_ERR(local->wep_tx_tfm))
			return -EOPNOTSUPP;
		auth_alg = WLAN_AUTH_SHARED_KEY;
		break;
	case NL80211_AUTHTYPE_FT:
		auth_alg = WLAN_AUTH_FT;
		break;
	case NL80211_AUTHTYPE_NETWORK_EAP:
		auth_alg = WLAN_AUTH_LEAP;
		break;
	case NL80211_AUTHTYPE_SAE:
		auth_alg = WLAN_AUTH_SAE;
		break;
	default:
		return -EOPNOTSUPP;
	}

	auth_data = kzalloc(sizeof(*auth_data) + req->sae_data_len +
			    req->ie_len, GFP_KERNEL);
	if (!auth_data)
		return -ENOMEM;

	auth_data->bss = req->bss;

	if (req->sae_data_len >= 4) {
		__le16 *pos = (__le16 *) req->sae_data;
		auth_data->sae_trans = le16_to_cpu(pos[0]);
		auth_data->sae_status = le16_to_cpu(pos[1]);
		memcpy(auth_data->data, req->sae_data + 4,
		       req->sae_data_len - 4);
		auth_data->data_len += req->sae_data_len - 4;
	}

	if (req->ie && req->ie_len) {
		memcpy(&auth_data->data[auth_data->data_len],
		       req->ie, req->ie_len);
		auth_data->data_len += req->ie_len;
	}

	if (req->key && req->key_len) {
		auth_data->key_len = req->key_len;
		auth_data->key_idx = req->key_idx;
		memcpy(auth_data->key, req->key, req->key_len);
	}

	auth_data->algorithm = auth_alg;

	/* try to authenticate/probe */

	if ((ifmgd->auth_data && !ifmgd->auth_data->done) ||
	    ifmgd->assoc_data) {
		err = -EBUSY;
		goto err_free;
	}

	if (ifmgd->auth_data)
		ieee80211_destroy_auth_data(sdata, false);

	/* prep auth_data so we don't go into idle on disassoc */
	ifmgd->auth_data = auth_data;

	if (ifmgd->associated) {
		u8 frame_buf[IEEE80211_DEAUTH_FRAME_LEN];

		ieee80211_set_disassoc(sdata, IEEE80211_STYPE_DEAUTH,
				       WLAN_REASON_UNSPECIFIED,
				       false, frame_buf);

		ieee80211_report_disconnect(sdata, frame_buf,
					    sizeof(frame_buf), true,
					    WLAN_REASON_UNSPECIFIED);
	}

	sdata_info(sdata, "authenticate with %pM\n", req->bss->bssid);

	err = ieee80211_prep_connection(sdata, req->bss, false, false);
	if (err)
		goto err_clear;

	err = ieee80211_auth(sdata);
	if (err) {
		sta_info_destroy_addr(sdata, req->bss->bssid);
		goto err_clear;
	}

	/* hold our own reference */
	cfg80211_ref_bss(local->hw.wiphy, auth_data->bss);
	return 0;

 err_clear:
	eth_zero_addr(ifmgd->bssid);
	ieee80211_bss_info_change_notify(sdata, BSS_CHANGED_BSSID);
	ifmgd->auth_data = NULL;
	mutex_lock(&sdata->local->mtx);
	ieee80211_vif_release_channel(sdata);
	mutex_unlock(&sdata->local->mtx);
 err_free:
	kfree(auth_data);
	return err;
}

int ieee80211_mgd_assoc(struct ieee80211_sub_if_data *sdata,
			struct cfg80211_assoc_request *req)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_bss *bss = (void *)req->bss->priv;
	struct ieee80211_mgd_assoc_data *assoc_data;
	const struct cfg80211_bss_ies *beacon_ies;
	struct ieee80211_supported_band *sband;
	const u8 *ssidie, *ht_ie, *vht_ie;
	int i, err;
	bool override = false;

	assoc_data = kzalloc(sizeof(*assoc_data) + req->ie_len, GFP_KERNEL);
	if (!assoc_data)
		return -ENOMEM;

	rcu_read_lock();
	ssidie = ieee80211_bss_get_ie(req->bss, WLAN_EID_SSID);
	if (!ssidie || ssidie[1] > sizeof(assoc_data->ssid)) {
		rcu_read_unlock();
		kfree(assoc_data);
		return -EINVAL;
	}
	memcpy(assoc_data->ssid, ssidie + 2, ssidie[1]);
	assoc_data->ssid_len = ssidie[1];
	rcu_read_unlock();

	if (ifmgd->associated) {
		u8 frame_buf[IEEE80211_DEAUTH_FRAME_LEN];

		ieee80211_set_disassoc(sdata, IEEE80211_STYPE_DEAUTH,
				       WLAN_REASON_UNSPECIFIED,
				       false, frame_buf);

		ieee80211_report_disconnect(sdata, frame_buf,
					    sizeof(frame_buf), true,
					    WLAN_REASON_UNSPECIFIED);
	}

	if (ifmgd->auth_data && !ifmgd->auth_data->done) {
		err = -EBUSY;
		goto err_free;
	}

	if (ifmgd->assoc_data) {
		err = -EBUSY;
		goto err_free;
	}

	if (ifmgd->auth_data) {
		bool match;

		/* keep sta info, bssid if matching */
		match = ether_addr_equal(ifmgd->bssid, req->bss->bssid);
		ieee80211_destroy_auth_data(sdata, match);
	}

	/* prepare assoc data */

	ifmgd->beacon_crc_valid = false;

	assoc_data->wmm = bss->wmm_used &&
			  (local->hw.queues >= IEEE80211_NUM_ACS);

	/*
	 * IEEE802.11n does not allow TKIP/WEP as pairwise ciphers in HT mode.
	 * We still associate in non-HT mode (11a/b/g) if any one of these
	 * ciphers is configured as pairwise.
	 * We can set this to true for non-11n hardware, that'll be checked
	 * separately along with the peer capabilities.
	 */
	for (i = 0; i < req->crypto.n_ciphers_pairwise; i++) {
		if (req->crypto.ciphers_pairwise[i] == WLAN_CIPHER_SUITE_WEP40 ||
		    req->crypto.ciphers_pairwise[i] == WLAN_CIPHER_SUITE_TKIP ||
		    req->crypto.ciphers_pairwise[i] == WLAN_CIPHER_SUITE_WEP104) {
			ifmgd->flags |= IEEE80211_STA_DISABLE_HT;
			ifmgd->flags |= IEEE80211_STA_DISABLE_VHT;
			netdev_info(sdata->dev,
				    "disabling HT/VHT due to WEP/TKIP use\n");
		}
	}

	/* Also disable HT if we don't support it or the AP doesn't use WMM */
	sband = local->hw.wiphy->bands[req->bss->channel->band];
	if (!sband->ht_cap.ht_supported ||
	    local->hw.queues < IEEE80211_NUM_ACS || !bss->wmm_used ||
	    ifmgd->flags & IEEE80211_STA_DISABLE_WMM) {
		ifmgd->flags |= IEEE80211_STA_DISABLE_HT;
		if (!bss->wmm_used &&
		    !(ifmgd->flags & IEEE80211_STA_DISABLE_WMM))
			netdev_info(sdata->dev,
				    "disabling HT as WMM/QoS is not supported by the AP\n");
	}

	/* disable VHT if we don't support it or the AP doesn't use WMM */
	if (!sband->vht_cap.vht_supported ||
	    local->hw.queues < IEEE80211_NUM_ACS || !bss->wmm_used ||
	    ifmgd->flags & IEEE80211_STA_DISABLE_WMM) {
		ifmgd->flags |= IEEE80211_STA_DISABLE_VHT;
		if (!bss->wmm_used &&
		    !(ifmgd->flags & IEEE80211_STA_DISABLE_WMM))
			netdev_info(sdata->dev,
				    "disabling VHT as WMM/QoS is not supported by the AP\n");
	}

	memcpy(&ifmgd->ht_capa, &req->ht_capa, sizeof(ifmgd->ht_capa));
	memcpy(&ifmgd->ht_capa_mask, &req->ht_capa_mask,
	       sizeof(ifmgd->ht_capa_mask));

	memcpy(&ifmgd->vht_capa, &req->vht_capa, sizeof(ifmgd->vht_capa));
	memcpy(&ifmgd->vht_capa_mask, &req->vht_capa_mask,
	       sizeof(ifmgd->vht_capa_mask));

	if (req->ie && req->ie_len) {
		memcpy(assoc_data->ie, req->ie, req->ie_len);
		assoc_data->ie_len = req->ie_len;
	}

	assoc_data->bss = req->bss;

	if (ifmgd->req_smps == IEEE80211_SMPS_AUTOMATIC) {
		if (ifmgd->powersave)
			sdata->smps_mode = IEEE80211_SMPS_DYNAMIC;
		else
			sdata->smps_mode = IEEE80211_SMPS_OFF;
	} else
		sdata->smps_mode = ifmgd->req_smps;

	assoc_data->capability = req->bss->capability;
	assoc_data->supp_rates = bss->supp_rates;
	assoc_data->supp_rates_len = bss->supp_rates_len;

	rcu_read_lock();
	ht_ie = ieee80211_bss_get_ie(req->bss, WLAN_EID_HT_OPERATION);
	if (ht_ie && ht_ie[1] >= sizeof(struct ieee80211_ht_operation))
		assoc_data->ap_ht_param =
			((struct ieee80211_ht_operation *)(ht_ie + 2))->ht_param;
	else
		ifmgd->flags |= IEEE80211_STA_DISABLE_HT;
	vht_ie = ieee80211_bss_get_ie(req->bss, WLAN_EID_VHT_CAPABILITY);
	if (vht_ie && vht_ie[1] >= sizeof(struct ieee80211_vht_cap))
		memcpy(&assoc_data->ap_vht_cap, vht_ie + 2,
		       sizeof(struct ieee80211_vht_cap));
	else
		ifmgd->flags |= IEEE80211_STA_DISABLE_VHT;
	rcu_read_unlock();

	if (WARN((sdata->vif.driver_flags & IEEE80211_VIF_SUPPORTS_UAPSD) &&
		 ieee80211_hw_check(&local->hw, PS_NULLFUNC_STACK),
	     "U-APSD not supported with HW_PS_NULLFUNC_STACK\n"))
		sdata->vif.driver_flags &= ~IEEE80211_VIF_SUPPORTS_UAPSD;

	if (bss->wmm_used && bss->uapsd_supported &&
	    (sdata->vif.driver_flags & IEEE80211_VIF_SUPPORTS_UAPSD)) {
		assoc_data->uapsd = true;
		ifmgd->flags |= IEEE80211_STA_UAPSD_ENABLED;
	} else {
		assoc_data->uapsd = false;
		ifmgd->flags &= ~IEEE80211_STA_UAPSD_ENABLED;
	}

	if (req->prev_bssid)
		memcpy(assoc_data->prev_bssid, req->prev_bssid, ETH_ALEN);

	if (req->use_mfp) {
		ifmgd->mfp = IEEE80211_MFP_REQUIRED;
		ifmgd->flags |= IEEE80211_STA_MFP_ENABLED;
	} else {
		ifmgd->mfp = IEEE80211_MFP_DISABLED;
		ifmgd->flags &= ~IEEE80211_STA_MFP_ENABLED;
	}

	if (req->flags & ASSOC_REQ_USE_RRM)
		ifmgd->flags |= IEEE80211_STA_ENABLE_RRM;
	else
		ifmgd->flags &= ~IEEE80211_STA_ENABLE_RRM;

	if (req->crypto.control_port)
		ifmgd->flags |= IEEE80211_STA_CONTROL_PORT;
	else
		ifmgd->flags &= ~IEEE80211_STA_CONTROL_PORT;

	sdata->control_port_protocol = req->crypto.control_port_ethertype;
	sdata->control_port_no_encrypt = req->crypto.control_port_no_encrypt;
	sdata->encrypt_headroom = ieee80211_cs_headroom(local, &req->crypto,
							sdata->vif.type);

	/* kick off associate process */

	ifmgd->assoc_data = assoc_data;
	ifmgd->dtim_period = 0;
	ifmgd->have_beacon = false;

	/* override HT/VHT configuration only if the AP and we support it */
	if (!(ifmgd->flags & IEEE80211_STA_DISABLE_HT)) {
		struct ieee80211_sta_ht_cap sta_ht_cap;

		if (req->flags & ASSOC_REQ_DISABLE_HT)
			override = true;

		memcpy(&sta_ht_cap, &sband->ht_cap, sizeof(sta_ht_cap));
		ieee80211_apply_htcap_overrides(sdata, &sta_ht_cap);

		/* check for 40 MHz disable override */
		if (!(ifmgd->flags & IEEE80211_STA_DISABLE_40MHZ) &&
		    sband->ht_cap.cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40 &&
		    !(sta_ht_cap.cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40))
			override = true;

		if (!(ifmgd->flags & IEEE80211_STA_DISABLE_VHT) &&
		    req->flags & ASSOC_REQ_DISABLE_VHT)
			override = true;
	}

	if (req->flags & ASSOC_REQ_DISABLE_HT) {
		ifmgd->flags |= IEEE80211_STA_DISABLE_HT;
		ifmgd->flags |= IEEE80211_STA_DISABLE_VHT;
	}

	if (req->flags & ASSOC_REQ_DISABLE_VHT)
		ifmgd->flags |= IEEE80211_STA_DISABLE_VHT;

	err = ieee80211_prep_connection(sdata, req->bss, true, override);
	if (err)
		goto err_clear;

	rcu_read_lock();
	beacon_ies = rcu_dereference(req->bss->beacon_ies);

	if (ieee80211_hw_check(&sdata->local->hw, NEED_DTIM_BEFORE_ASSOC) &&
	    !beacon_ies) {
		/*
		 * Wait up to one beacon interval ...
		 * should this be more if we miss one?
		 */
		sdata_info(sdata, "waiting for beacon from %pM\n",
			   ifmgd->bssid);
		assoc_data->timeout = TU_TO_EXP_TIME(req->bss->beacon_interval);
		assoc_data->timeout_started = true;
		assoc_data->need_beacon = true;
	} else if (beacon_ies) {
		const u8 *tim_ie = cfg80211_find_ie(WLAN_EID_TIM,
						    beacon_ies->data,
						    beacon_ies->len);
		u8 dtim_count = 0;

		if (tim_ie && tim_ie[1] >= sizeof(struct ieee80211_tim_ie)) {
			const struct ieee80211_tim_ie *tim;
			tim = (void *)(tim_ie + 2);
			ifmgd->dtim_period = tim->dtim_period;
			dtim_count = tim->dtim_count;
		}
		ifmgd->have_beacon = true;
		assoc_data->timeout = jiffies;
		assoc_data->timeout_started = true;

		if (ieee80211_hw_check(&local->hw, TIMING_BEACON_ONLY)) {
			sdata->vif.bss_conf.sync_tsf = beacon_ies->tsf;
			sdata->vif.bss_conf.sync_device_ts =
				bss->device_ts_beacon;
			sdata->vif.bss_conf.sync_dtim_count = dtim_count;
		}
	} else {
		assoc_data->timeout = jiffies;
		assoc_data->timeout_started = true;
	}
	rcu_read_unlock();

	run_again(sdata, assoc_data->timeout);

	if (bss->corrupt_data) {
		char *corrupt_type = "data";
		if (bss->corrupt_data & IEEE80211_BSS_CORRUPT_BEACON) {
			if (bss->corrupt_data &
					IEEE80211_BSS_CORRUPT_PROBE_RESP)
				corrupt_type = "beacon and probe response";
			else
				corrupt_type = "beacon";
		} else if (bss->corrupt_data & IEEE80211_BSS_CORRUPT_PROBE_RESP)
			corrupt_type = "probe response";
		sdata_info(sdata, "associating with AP with corrupt %s\n",
			   corrupt_type);
	}

	return 0;
 err_clear:
	eth_zero_addr(ifmgd->bssid);
	ieee80211_bss_info_change_notify(sdata, BSS_CHANGED_BSSID);
	ifmgd->assoc_data = NULL;
 err_free:
	kfree(assoc_data);
	return err;
}

int ieee80211_mgd_deauth(struct ieee80211_sub_if_data *sdata,
			 struct cfg80211_deauth_request *req)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	u8 frame_buf[IEEE80211_DEAUTH_FRAME_LEN];
	bool tx = !req->local_state_change;

	if (ifmgd->auth_data &&
	    ether_addr_equal(ifmgd->auth_data->bss->bssid, req->bssid)) {
		sdata_info(sdata,
			   "aborting authentication with %pM by local choice (Reason: %u=%s)\n",
			   req->bssid, req->reason_code,
			   ieee80211_get_reason_code_string(req->reason_code));

		drv_mgd_prepare_tx(sdata->local, sdata);
		ieee80211_send_deauth_disassoc(sdata, req->bssid,
					       IEEE80211_STYPE_DEAUTH,
					       req->reason_code, tx,
					       frame_buf);
		ieee80211_destroy_auth_data(sdata, false);
		ieee80211_report_disconnect(sdata, frame_buf,
					    sizeof(frame_buf), true,
					    req->reason_code);

		return 0;
	}

	if (ifmgd->assoc_data &&
	    ether_addr_equal(ifmgd->assoc_data->bss->bssid, req->bssid)) {
		sdata_info(sdata,
			   "aborting association with %pM by local choice (Reason: %u=%s)\n",
			   req->bssid, req->reason_code,
			   ieee80211_get_reason_code_string(req->reason_code));

		drv_mgd_prepare_tx(sdata->local, sdata);
		ieee80211_send_deauth_disassoc(sdata, req->bssid,
					       IEEE80211_STYPE_DEAUTH,
					       req->reason_code, tx,
					       frame_buf);
		ieee80211_destroy_assoc_data(sdata, false, true);
		ieee80211_report_disconnect(sdata, frame_buf,
					    sizeof(frame_buf), true,
					    req->reason_code);
		return 0;
	}

	if (ifmgd->associated &&
	    ether_addr_equal(ifmgd->associated->bssid, req->bssid)) {
		sdata_info(sdata,
			   "deauthenticating from %pM by local choice (Reason: %u=%s)\n",
			   req->bssid, req->reason_code,
			   ieee80211_get_reason_code_string(req->reason_code));

		ieee80211_set_disassoc(sdata, IEEE80211_STYPE_DEAUTH,
				       req->reason_code, tx, frame_buf);
		ieee80211_report_disconnect(sdata, frame_buf,
					    sizeof(frame_buf), true,
					    req->reason_code);
		return 0;
	}

	return -ENOTCONN;
}

int ieee80211_mgd_disassoc(struct ieee80211_sub_if_data *sdata,
			   struct cfg80211_disassoc_request *req)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	u8 bssid[ETH_ALEN];
	u8 frame_buf[IEEE80211_DEAUTH_FRAME_LEN];

	/*
	 * cfg80211 should catch this ... but it's racy since
	 * we can receive a disassoc frame, process it, hand it
	 * to cfg80211 while that's in a locked section already
	 * trying to tell us that the user wants to disconnect.
	 */
	if (ifmgd->associated != req->bss)
		return -ENOLINK;

	sdata_info(sdata,
		   "disassociating from %pM by local choice (Reason: %u=%s)\n",
		   req->bss->bssid, req->reason_code, ieee80211_get_reason_code_string(req->reason_code));

	memcpy(bssid, req->bss->bssid, ETH_ALEN);
	ieee80211_set_disassoc(sdata, IEEE80211_STYPE_DISASSOC,
			       req->reason_code, !req->local_state_change,
			       frame_buf);

	ieee80211_report_disconnect(sdata, frame_buf, sizeof(frame_buf), true,
				    req->reason_code);

	return 0;
}

void ieee80211_mgd_stop(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	/*
	 * Make sure some work items will not run after this,
	 * they will not do anything but might not have been
	 * cancelled when disconnecting.
	 */
	cancel_work_sync(&ifmgd->monitor_work);
	cancel_work_sync(&ifmgd->beacon_connection_loss_work);
	cancel_work_sync(&ifmgd->request_smps_work);
	cancel_work_sync(&ifmgd->csa_connection_drop_work);
	cancel_work_sync(&ifmgd->chswitch_work);
	cancel_delayed_work_sync(&ifmgd->tdls_peer_del_work);

	sdata_lock(sdata);
	if (ifmgd->assoc_data) {
		struct cfg80211_bss *bss = ifmgd->assoc_data->bss;
		ieee80211_destroy_assoc_data(sdata, false, false);
		cfg80211_assoc_timeout(sdata->dev, bss);
	}
	if (ifmgd->auth_data)
		ieee80211_destroy_auth_data(sdata, false);
	spin_lock_bh(&ifmgd->teardown_lock);
	if (ifmgd->teardown_skb) {
		kfree_skb(ifmgd->teardown_skb);
		ifmgd->teardown_skb = NULL;
		ifmgd->orig_teardown_skb = NULL;
	}
	spin_unlock_bh(&ifmgd->teardown_lock);
	del_timer_sync(&ifmgd->timer);
	sdata_unlock(sdata);
}

void ieee80211_cqm_rssi_notify(struct ieee80211_vif *vif,
			       enum nl80211_cqm_rssi_threshold_event rssi_event,
			       gfp_t gfp)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);

	trace_api_cqm_rssi_notify(sdata, rssi_event);

	cfg80211_cqm_rssi_notify(sdata->dev, rssi_event, gfp);
}
EXPORT_SYMBOL(ieee80211_cqm_rssi_notify);

void ieee80211_cqm_beacon_loss_notify(struct ieee80211_vif *vif, gfp_t gfp)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);

	trace_api_cqm_beacon_loss_notify(sdata->local, sdata);

	cfg80211_cqm_beacon_loss_notify(sdata->dev, gfp);
}
EXPORT_SYMBOL(ieee80211_cqm_beacon_loss_notify);
