/*
 * Copyright (c) 2008 open80211s Ltd.
 * Copyright (c) 2008, 2009 open80211s Ltd.
 * Author:     Luis Carlos Cobo <luisca@cozybit.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/random.h>
#include "ieee80211_i.h"
#include "rate.h"
#include "mesh.h"

#ifdef CONFIG_MAC80211_VERBOSE_MPL_DEBUG
#define mpl_dbg(fmt, args...)	printk(KERN_DEBUG fmt, ##args)
#else
#define mpl_dbg(fmt, args...)	do { (void)(0); } while (0)
#endif

#define PLINK_GET_FRAME_SUBTYPE(p) (p)
#define PLINK_GET_LLID(p) (p + 1)
#define PLINK_GET_PLID(p) (p + 3)

#define mod_plink_timer(s, t) (mod_timer(&s->plink_timer, \
				jiffies + HZ * t / 1000))

/* Peer link cancel reasons, all subject to ANA approval */
#define MESH_LINK_CANCELLED			2
#define MESH_MAX_NEIGHBORS			3
#define MESH_CAPABILITY_POLICY_VIOLATION	4
#define MESH_CLOSE_RCVD				5
#define MESH_MAX_RETRIES			6
#define MESH_CONFIRM_TIMEOUT			7
#define MESH_SECURITY_ROLE_NEGOTIATION_DIFFERS	8
#define MESH_SECURITY_AUTHENTICATION_IMPOSSIBLE	9
#define MESH_SECURITY_FAILED_VERIFICATION	10

#define dot11MeshMaxRetries(s) (s->u.sta.mshcfg.dot11MeshMaxRetries)
#define dot11MeshRetryTimeout(s) (s->u.sta.mshcfg.dot11MeshRetryTimeout)
#define dot11MeshConfirmTimeout(s) (s->u.sta.mshcfg.dot11MeshConfirmTimeout)
#define dot11MeshHoldingTimeout(s) (s->u.sta.mshcfg.dot11MeshHoldingTimeout)
#define dot11MeshMaxPeerLinks(s) (s->u.sta.mshcfg.dot11MeshMaxPeerLinks)

enum plink_frame_type {
	PLINK_OPEN = 0,
	PLINK_CONFIRM,
	PLINK_CLOSE
};
#define PLINK_CNF_AID(mgmt) ((mgmt)->u.action.u.self_prot.variable + 2)
#define PLINK_GET_LLID(p) (p + 2)
#define PLINK_GET_PLID(p) (p + 4)

#define mod_plink_timer(s, t) (mod_timer(&s->mesh->plink_timer, \
				jiffies + msecs_to_jiffies(t)))

enum plink_event {
	PLINK_UNDEFINED,
	OPN_ACPT,
	OPN_RJCT,
	OPN_IGNR,
	CNF_ACPT,
	CNF_RJCT,
	CNF_IGNR,
	CLS_ACPT,
	CLS_IGNR
};

static inline
void mesh_plink_inc_estab_count(struct ieee80211_sub_if_data *sdata)
{
	atomic_inc(&sdata->u.sta.mshstats.estab_plinks);
	mesh_accept_plinks_update(sdata);
}

static inline
void mesh_plink_dec_estab_count(struct ieee80211_sub_if_data *sdata)
{
	atomic_dec(&sdata->u.sta.mshstats.estab_plinks);
	mesh_accept_plinks_update(sdata);
static const char * const mplstates[] = {
	[NL80211_PLINK_LISTEN] = "LISTEN",
	[NL80211_PLINK_OPN_SNT] = "OPN-SNT",
	[NL80211_PLINK_OPN_RCVD] = "OPN-RCVD",
	[NL80211_PLINK_CNF_RCVD] = "CNF_RCVD",
	[NL80211_PLINK_ESTAB] = "ESTAB",
	[NL80211_PLINK_HOLDING] = "HOLDING",
	[NL80211_PLINK_BLOCKED] = "BLOCKED"
};

static const char * const mplevents[] = {
	[PLINK_UNDEFINED] = "NONE",
	[OPN_ACPT] = "OPN_ACPT",
	[OPN_RJCT] = "OPN_RJCT",
	[OPN_IGNR] = "OPN_IGNR",
	[CNF_ACPT] = "CNF_ACPT",
	[CNF_RJCT] = "CNF_RJCT",
	[CNF_IGNR] = "CNF_IGNR",
	[CLS_ACPT] = "CLS_ACPT",
	[CLS_IGNR] = "CLS_IGNR"
};

/* We only need a valid sta if user configured a minimum rssi_threshold. */
static bool rssi_threshold_check(struct ieee80211_sub_if_data *sdata,
				 struct sta_info *sta)
{
	s32 rssi_threshold = sdata->u.mesh.mshcfg.rssi_threshold;
	return rssi_threshold == 0 ||
	       (sta &&
		(s8)-ewma_signal_read(&sta->rx_stats.avg_signal) >
						rssi_threshold);
}

/**
 * mesh_plink_fsm_restart - restart a mesh peer link finite state machine
 *
 * @sta: mes peer link to restart
 *
 * Locking: this function must be called holding sta->lock
 */
static inline void mesh_plink_fsm_restart(struct sta_info *sta)
{
	sta->plink_state = PLINK_LISTEN;
	sta->llid = sta->plid = sta->reason = 0;
	sta->plink_retries = 0;
}

/*
 * NOTE: This is just an alias for sta_info_alloc(), see notes
 *       on it in the lifecycle management section!
 */
static struct sta_info *mesh_plink_alloc(struct ieee80211_sub_if_data *sdata,
					 u8 *hw_addr, u64 rates)
{
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;

	if (local->num_sta >= MESH_MAX_PLINKS)
		return NULL;

	sta = sta_info_alloc(sdata, hw_addr, GFP_ATOMIC);
	if (!sta)
		return NULL;

	sta->flags = WLAN_STA_AUTHORIZED;
	sta->supp_rates[local->hw.conf.channel->band] = rates;

	return sta;
}

/**
 * mesh_plink_deactivate - deactivate mesh peer link
 *
 * @sta: mesh peer link to deactivate
 *
 * All mesh paths with this peer as next hop will be flushed
 *
 * Locking: the caller must hold sta->lock
 */
static void __mesh_plink_deactivate(struct sta_info *sta)
{
	struct ieee80211_sub_if_data *sdata = sta->sdata;

	if (sta->plink_state == PLINK_ESTAB)
		mesh_plink_dec_estab_count(sdata);
	sta->plink_state = PLINK_BLOCKED;
	mesh_path_flush_by_nexthop(sta);
 * @sta: mesh peer link to restart
 *
 * Locking: this function must be called holding sta->mesh->plink_lock
 */
static inline void mesh_plink_fsm_restart(struct sta_info *sta)
{
	lockdep_assert_held(&sta->mesh->plink_lock);
	sta->mesh->plink_state = NL80211_PLINK_LISTEN;
	sta->mesh->llid = sta->mesh->plid = sta->mesh->reason = 0;
	sta->mesh->plink_retries = 0;
}

/*
 * mesh_set_short_slot_time - enable / disable ERP short slot time.
 *
 * The standard indirectly mandates mesh STAs to turn off short slot time by
 * disallowing advertising this (802.11-2012 8.4.1.4), but that doesn't mean we
 * can't be sneaky about it. Enable short slot time if all mesh STAs in the
 * MBSS support ERP rates.
 *
 * Returns BSS_CHANGED_ERP_SLOT or 0 for no change.
 */
static u32 mesh_set_short_slot_time(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	enum ieee80211_band band = ieee80211_get_sdata_band(sdata);
	struct ieee80211_supported_band *sband = local->hw.wiphy->bands[band];
	struct sta_info *sta;
	u32 erp_rates = 0, changed = 0;
	int i;
	bool short_slot = false;

	if (band == IEEE80211_BAND_5GHZ) {
		/* (IEEE 802.11-2012 19.4.5) */
		short_slot = true;
		goto out;
	} else if (band != IEEE80211_BAND_2GHZ)
		goto out;

	for (i = 0; i < sband->n_bitrates; i++)
		if (sband->bitrates[i].flags & IEEE80211_RATE_ERP_G)
			erp_rates |= BIT(i);

	if (!erp_rates)
		goto out;

	rcu_read_lock();
	list_for_each_entry_rcu(sta, &local->sta_list, list) {
		if (sdata != sta->sdata ||
		    sta->mesh->plink_state != NL80211_PLINK_ESTAB)
			continue;

		short_slot = false;
		if (erp_rates & sta->sta.supp_rates[band])
			short_slot = true;
		 else
			break;
	}
	rcu_read_unlock();

out:
	if (sdata->vif.bss_conf.use_short_slot != short_slot) {
		sdata->vif.bss_conf.use_short_slot = short_slot;
		changed = BSS_CHANGED_ERP_SLOT;
		mpl_dbg(sdata, "mesh_plink %pM: ERP short slot time %d\n",
			sdata->vif.addr, short_slot);
	}
	return changed;
}

/**
 * mesh_set_ht_prot_mode - set correct HT protection mode
 *
 * Section 9.23.3.5 of IEEE 80211-2012 describes the protection rules for HT
 * mesh STA in a MBSS. Three HT protection modes are supported for now, non-HT
 * mixed mode, 20MHz-protection and no-protection mode. non-HT mixed mode is
 * selected if any non-HT peers are present in our MBSS.  20MHz-protection mode
 * is selected if all peers in our 20/40MHz MBSS support HT and atleast one
 * HT20 peer is present. Otherwise no-protection mode is selected.
 */
static u32 mesh_set_ht_prot_mode(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;
	u16 ht_opmode;
	bool non_ht_sta = false, ht20_sta = false;

	switch (sdata->vif.bss_conf.chandef.width) {
	case NL80211_CHAN_WIDTH_20_NOHT:
	case NL80211_CHAN_WIDTH_5:
	case NL80211_CHAN_WIDTH_10:
		return 0;
	default:
		break;
	}

	rcu_read_lock();
	list_for_each_entry_rcu(sta, &local->sta_list, list) {
		if (sdata != sta->sdata ||
		    sta->mesh->plink_state != NL80211_PLINK_ESTAB)
			continue;

		if (sta->sta.bandwidth > IEEE80211_STA_RX_BW_20)
			continue;

		if (!sta->sta.ht_cap.ht_supported) {
			mpl_dbg(sdata, "nonHT sta (%pM) is present\n",
				       sta->sta.addr);
			non_ht_sta = true;
			break;
		}

		mpl_dbg(sdata, "HT20 sta (%pM) is present\n", sta->sta.addr);
		ht20_sta = true;
	}
	rcu_read_unlock();

	if (non_ht_sta)
		ht_opmode = IEEE80211_HT_OP_MODE_PROTECTION_NONHT_MIXED;
	else if (ht20_sta &&
		 sdata->vif.bss_conf.chandef.width > NL80211_CHAN_WIDTH_20)
		ht_opmode = IEEE80211_HT_OP_MODE_PROTECTION_20MHZ;
	else
		ht_opmode = IEEE80211_HT_OP_MODE_PROTECTION_NONE;

	if (sdata->vif.bss_conf.ht_operation_mode == ht_opmode)
		return 0;

	sdata->vif.bss_conf.ht_operation_mode = ht_opmode;
	sdata->u.mesh.mshcfg.ht_opmode = ht_opmode;
	mpl_dbg(sdata, "selected new HT protection mode %d\n", ht_opmode);
	return BSS_CHANGED_HT;
}

static int mesh_plink_frame_tx(struct ieee80211_sub_if_data *sdata,
			       struct sta_info *sta,
			       enum ieee80211_self_protected_actioncode action,
			       u8 *da, u16 llid, u16 plid, u16 reason)
{
	struct ieee80211_local *local = sdata->local;
	struct sk_buff *skb;
	struct ieee80211_tx_info *info;
	struct ieee80211_mgmt *mgmt;
	bool include_plid = false;
	u16 peering_proto = 0;
	u8 *pos, ie_len = 4;
	int hdr_len = offsetof(struct ieee80211_mgmt, u.action.u.self_prot) +
		      sizeof(mgmt->u.action.u.self_prot);
	int err = -ENOMEM;

	skb = dev_alloc_skb(local->tx_headroom +
			    hdr_len +
			    2 + /* capability info */
			    2 + /* AID */
			    2 + 8 + /* supported rates */
			    2 + (IEEE80211_MAX_SUPP_RATES - 8) +
			    2 + sdata->u.mesh.mesh_id_len +
			    2 + sizeof(struct ieee80211_meshconf_ie) +
			    2 + sizeof(struct ieee80211_ht_cap) +
			    2 + sizeof(struct ieee80211_ht_operation) +
			    2 + sizeof(struct ieee80211_vht_cap) +
			    2 + sizeof(struct ieee80211_vht_operation) +
			    2 + 8 + /* peering IE */
			    sdata->u.mesh.ie_len);
	if (!skb)
		return err;
	info = IEEE80211_SKB_CB(skb);
	skb_reserve(skb, local->tx_headroom);
	mgmt = (struct ieee80211_mgmt *) skb_put(skb, hdr_len);
	memset(mgmt, 0, hdr_len);
	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					  IEEE80211_STYPE_ACTION);
	memcpy(mgmt->da, da, ETH_ALEN);
	memcpy(mgmt->sa, sdata->vif.addr, ETH_ALEN);
	memcpy(mgmt->bssid, sdata->vif.addr, ETH_ALEN);
	mgmt->u.action.category = WLAN_CATEGORY_SELF_PROTECTED;
	mgmt->u.action.u.self_prot.action_code = action;

	if (action != WLAN_SP_MESH_PEERING_CLOSE) {
		enum ieee80211_band band = ieee80211_get_sdata_band(sdata);

		/* capability info */
		pos = skb_put(skb, 2);
		memset(pos, 0, 2);
		if (action == WLAN_SP_MESH_PEERING_CONFIRM) {
			/* AID */
			pos = skb_put(skb, 2);
			put_unaligned_le16(sta->sta.aid, pos);
		}
		if (ieee80211_add_srates_ie(sdata, skb, true, band) ||
		    ieee80211_add_ext_srates_ie(sdata, skb, true, band) ||
		    mesh_add_rsn_ie(sdata, skb) ||
		    mesh_add_meshid_ie(sdata, skb) ||
		    mesh_add_meshconf_ie(sdata, skb))
			goto free;
	} else {	/* WLAN_SP_MESH_PEERING_CLOSE */
		info->flags |= IEEE80211_TX_CTL_NO_ACK;
		if (mesh_add_meshid_ie(sdata, skb))
			goto free;
	}

	/* Add Mesh Peering Management element */
	switch (action) {
	case WLAN_SP_MESH_PEERING_OPEN:
		break;
	case WLAN_SP_MESH_PEERING_CONFIRM:
		ie_len += 2;
		include_plid = true;
		break;
	case WLAN_SP_MESH_PEERING_CLOSE:
		if (plid) {
			ie_len += 2;
			include_plid = true;
		}
		ie_len += 2;	/* reason code */
		break;
	default:
		err = -EINVAL;
		goto free;
	}

	if (WARN_ON(skb_tailroom(skb) < 2 + ie_len))
		goto free;

	pos = skb_put(skb, 2 + ie_len);
	*pos++ = WLAN_EID_PEER_MGMT;
	*pos++ = ie_len;
	memcpy(pos, &peering_proto, 2);
	pos += 2;
	put_unaligned_le16(llid, pos);
	pos += 2;
	if (include_plid) {
		put_unaligned_le16(plid, pos);
		pos += 2;
	}
	if (action == WLAN_SP_MESH_PEERING_CLOSE) {
		put_unaligned_le16(reason, pos);
		pos += 2;
	}

	if (action != WLAN_SP_MESH_PEERING_CLOSE) {
		if (mesh_add_ht_cap_ie(sdata, skb) ||
		    mesh_add_ht_oper_ie(sdata, skb) ||
		    mesh_add_vht_cap_ie(sdata, skb) ||
		    mesh_add_vht_oper_ie(sdata, skb))
			goto free;
	}

	if (mesh_add_vendor_ies(sdata, skb))
		goto free;

	ieee80211_tx_skb(sdata, skb);
	return 0;
free:
	kfree_skb(skb);
	return err;
}

/**
 * __mesh_plink_deactivate - deactivate mesh peer link
 *
 * @sta: mesh peer link to deactivate
 *
 * All mesh paths with this peer as next hop will be flushed
 */
void mesh_plink_deactivate(struct sta_info *sta)
{
	spin_lock_bh(&sta->lock);
	__mesh_plink_deactivate(sta);
	spin_unlock_bh(&sta->lock);
}

static int mesh_plink_frame_tx(struct net_device *dev,
		enum plink_frame_type action, u8 *da, __le16 llid, __le16 plid,
		__le16 reason) {
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct sk_buff *skb = dev_alloc_skb(local->hw.extra_tx_headroom + 400);
	struct ieee80211_mgmt *mgmt;
	bool include_plid = false;
	u8 *pos;
	int ie_len;

	if (!skb)
		return -1;
	skb_reserve(skb, local->hw.extra_tx_headroom);
	/* 25 is the size of the common mgmt part (24) plus the size of the
	 * common action part (1)
	 */
	mgmt = (struct ieee80211_mgmt *)
		skb_put(skb, 25 + sizeof(mgmt->u.action.u.plink_action));
	memset(mgmt, 0, 25 + sizeof(mgmt->u.action.u.plink_action));
	mgmt->frame_control = IEEE80211_FC(IEEE80211_FTYPE_MGMT,
					   IEEE80211_STYPE_ACTION);
	memcpy(mgmt->da, da, ETH_ALEN);
	memcpy(mgmt->sa, dev->dev_addr, ETH_ALEN);
	/* BSSID is left zeroed, wildcard value */
	mgmt->u.action.category = PLINK_CATEGORY;
	mgmt->u.action.u.plink_action.action_code = action;

	if (action == PLINK_CLOSE)
		mgmt->u.action.u.plink_action.aux = reason;
	else {
		mgmt->u.action.u.plink_action.aux = cpu_to_le16(0x0);
		if (action == PLINK_CONFIRM) {
			pos = skb_put(skb, 4);
			/* two-byte status code followed by two-byte AID */
			memset(pos, 0, 4);
		}
		mesh_mgmt_ies_add(skb, dev);
	}

	/* Add Peer Link Management element */
	switch (action) {
	case PLINK_OPEN:
		ie_len = 3;
		break;
	case PLINK_CONFIRM:
		ie_len = 5;
		include_plid = true;
		break;
	case PLINK_CLOSE:
	default:
		if (!plid)
			ie_len = 5;
		else {
			ie_len = 7;
			include_plid = true;
		}
		break;
	}

	pos = skb_put(skb, 2 + ie_len);
	*pos++ = WLAN_EID_PEER_LINK;
	*pos++ = ie_len;
	*pos++ = action;
	memcpy(pos, &llid, 2);
	if (include_plid) {
		pos += 2;
		memcpy(pos, &plid, 2);
	}
	if (action == PLINK_CLOSE) {
		pos += 2;
		memcpy(pos, &reason, 2);
	}

	ieee80211_sta_tx(dev, skb, 0);
	return 0;
}

void mesh_neighbour_update(u8 *hw_addr, u64 rates, struct net_device *dev,
			   bool peer_accepting_plinks)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct sta_info *sta;

	rcu_read_lock();

	sta = sta_info_get(local, hw_addr);
	if (!sta) {
		sta = mesh_plink_alloc(sdata, hw_addr, rates);
		if (!sta) {
			rcu_read_unlock();
			return;
		}
		if (sta_info_insert(sta)) {
			rcu_read_unlock();
			return;
		}
	}

	sta->last_rx = jiffies;
	sta->supp_rates[local->hw.conf.channel->band] = rates;
	if (peer_accepting_plinks && sta->plink_state == PLINK_LISTEN &&
			sdata->u.sta.accepting_plinks &&
			sdata->u.sta.mshcfg.auto_open_plinks)
		mesh_plink_open(sta);

	rcu_read_unlock();
 * Returns beacon changed flag if the beacon content changed.
 *
 * Locking: the caller must hold sta->mesh->plink_lock
 */
static u32 __mesh_plink_deactivate(struct sta_info *sta)
{
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	u32 changed = 0;

	lockdep_assert_held(&sta->mesh->plink_lock);

	if (sta->mesh->plink_state == NL80211_PLINK_ESTAB)
		changed = mesh_plink_dec_estab_count(sdata);
	sta->mesh->plink_state = NL80211_PLINK_BLOCKED;
	mesh_path_flush_by_nexthop(sta);

	ieee80211_mps_sta_status_update(sta);
	changed |= ieee80211_mps_set_sta_local_pm(sta,
			NL80211_MESH_POWER_UNKNOWN);

	return changed;
}

/**
 * mesh_plink_deactivate - deactivate mesh peer link
 *
 * @sta: mesh peer link to deactivate
 *
 * All mesh paths with this peer as next hop will be flushed
 */
u32 mesh_plink_deactivate(struct sta_info *sta)
{
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	u32 changed;

	spin_lock_bh(&sta->mesh->plink_lock);
	changed = __mesh_plink_deactivate(sta);
	sta->mesh->reason = WLAN_REASON_MESH_PEER_CANCELED;
	mesh_plink_frame_tx(sdata, sta, WLAN_SP_MESH_PEERING_CLOSE,
			    sta->sta.addr, sta->mesh->llid, sta->mesh->plid,
			    sta->mesh->reason);
	spin_unlock_bh(&sta->mesh->plink_lock);

	return changed;
}

static void mesh_sta_info_init(struct ieee80211_sub_if_data *sdata,
			       struct sta_info *sta,
			       struct ieee802_11_elems *elems, bool insert)
{
	struct ieee80211_local *local = sdata->local;
	enum ieee80211_band band = ieee80211_get_sdata_band(sdata);
	struct ieee80211_supported_band *sband;
	u32 rates, basic_rates = 0, changed = 0;
	enum ieee80211_sta_rx_bandwidth bw = sta->sta.bandwidth;

	sband = local->hw.wiphy->bands[band];
	rates = ieee80211_sta_get_rates(sdata, elems, band, &basic_rates);

	spin_lock_bh(&sta->mesh->plink_lock);
	sta->rx_stats.last_rx = jiffies;

	/* rates and capabilities don't change during peering */
	if (sta->mesh->plink_state == NL80211_PLINK_ESTAB &&
	    sta->mesh->processed_beacon)
		goto out;
	sta->mesh->processed_beacon = true;

	if (sta->sta.supp_rates[band] != rates)
		changed |= IEEE80211_RC_SUPP_RATES_CHANGED;
	sta->sta.supp_rates[band] = rates;

	if (ieee80211_ht_cap_ie_to_sta_ht_cap(sdata, sband,
					      elems->ht_cap_elem, sta))
		changed |= IEEE80211_RC_BW_CHANGED;

	ieee80211_vht_cap_ie_to_sta_vht_cap(sdata, sband,
					    elems->vht_cap_elem, sta);

	if (bw != sta->sta.bandwidth)
		changed |= IEEE80211_RC_BW_CHANGED;

	/* HT peer is operating 20MHz-only */
	if (elems->ht_operation &&
	    !(elems->ht_operation->ht_param &
	      IEEE80211_HT_PARAM_CHAN_WIDTH_ANY)) {
		if (sta->sta.bandwidth != IEEE80211_STA_RX_BW_20)
			changed |= IEEE80211_RC_BW_CHANGED;
		sta->sta.bandwidth = IEEE80211_STA_RX_BW_20;
	}

	if (insert)
		rate_control_rate_init(sta);
	else
		rate_control_rate_update(local, sband, sta, changed);
out:
	spin_unlock_bh(&sta->mesh->plink_lock);
}

static int mesh_allocate_aid(struct ieee80211_sub_if_data *sdata)
{
	struct sta_info *sta;
	unsigned long *aid_map;
	int aid;

	aid_map = kcalloc(BITS_TO_LONGS(IEEE80211_MAX_AID + 1),
			  sizeof(*aid_map), GFP_KERNEL);
	if (!aid_map)
		return -ENOMEM;

	/* reserve aid 0 for mcast indication */
	__set_bit(0, aid_map);

	rcu_read_lock();
	list_for_each_entry_rcu(sta, &sdata->local->sta_list, list)
		__set_bit(sta->sta.aid, aid_map);
	rcu_read_unlock();

	aid = find_first_zero_bit(aid_map, IEEE80211_MAX_AID + 1);
	kfree(aid_map);

	if (aid > IEEE80211_MAX_AID)
		return -ENOBUFS;

	return aid;
}

static struct sta_info *
__mesh_sta_info_alloc(struct ieee80211_sub_if_data *sdata, u8 *hw_addr)
{
	struct sta_info *sta;
	int aid;

	if (sdata->local->num_sta >= MESH_MAX_PLINKS)
		return NULL;

	aid = mesh_allocate_aid(sdata);
	if (aid < 0)
		return NULL;

	sta = sta_info_alloc(sdata, hw_addr, GFP_KERNEL);
	if (!sta)
		return NULL;

	sta->mesh->plink_state = NL80211_PLINK_LISTEN;
	sta->sta.wme = true;
	sta->sta.aid = aid;

	sta_info_pre_move_state(sta, IEEE80211_STA_AUTH);
	sta_info_pre_move_state(sta, IEEE80211_STA_ASSOC);
	sta_info_pre_move_state(sta, IEEE80211_STA_AUTHORIZED);

	return sta;
}

static struct sta_info *
mesh_sta_info_alloc(struct ieee80211_sub_if_data *sdata, u8 *addr,
		    struct ieee802_11_elems *elems)
{
	struct sta_info *sta = NULL;

	/* Userspace handles station allocation */
	if (sdata->u.mesh.user_mpm ||
	    sdata->u.mesh.security & IEEE80211_MESH_SEC_AUTHED) {
		if (mesh_peer_accepts_plinks(elems) &&
		    mesh_plink_availables(sdata))
			cfg80211_notify_new_peer_candidate(sdata->dev, addr,
							   elems->ie_start,
							   elems->total_len,
							   GFP_KERNEL);
	} else
		sta = __mesh_sta_info_alloc(sdata, addr);

	return sta;
}

/*
 * mesh_sta_info_get - return mesh sta info entry for @addr.
 *
 * @sdata: local meshif
 * @addr: peer's address
 * @elems: IEs from beacon or mesh peering frame.
 *
 * Return existing or newly allocated sta_info under RCU read lock.
 * (re)initialize with given IEs.
 */
static struct sta_info *
mesh_sta_info_get(struct ieee80211_sub_if_data *sdata,
		  u8 *addr, struct ieee802_11_elems *elems) __acquires(RCU)
{
	struct sta_info *sta = NULL;

	rcu_read_lock();
	sta = sta_info_get(sdata, addr);
	if (sta) {
		mesh_sta_info_init(sdata, sta, elems, false);
	} else {
		rcu_read_unlock();
		/* can't run atomic */
		sta = mesh_sta_info_alloc(sdata, addr, elems);
		if (!sta) {
			rcu_read_lock();
			return NULL;
		}

		mesh_sta_info_init(sdata, sta, elems, true);

		if (sta_info_insert_rcu(sta))
			return NULL;
	}

	return sta;
}

/*
 * mesh_neighbour_update - update or initialize new mesh neighbor.
 *
 * @sdata: local meshif
 * @addr: peer's address
 * @elems: IEs from beacon or mesh peering frame
 *
 * Initiates peering if appropriate.
 */
void mesh_neighbour_update(struct ieee80211_sub_if_data *sdata,
			   u8 *hw_addr,
			   struct ieee802_11_elems *elems)
{
	struct sta_info *sta;
	u32 changed = 0;

	sta = mesh_sta_info_get(sdata, hw_addr, elems);
	if (!sta)
		goto out;

	if (mesh_peer_accepts_plinks(elems) &&
	    sta->mesh->plink_state == NL80211_PLINK_LISTEN &&
	    sdata->u.mesh.accepting_plinks &&
	    sdata->u.mesh.mshcfg.auto_open_plinks &&
	    rssi_threshold_check(sdata, sta))
		changed = mesh_plink_open(sta);

	ieee80211_mps_frame_release(sta, elems);
out:
	rcu_read_unlock();
	ieee80211_mbss_info_change_notify(sdata, changed);
}

static void mesh_plink_timer(unsigned long data)
{
	struct sta_info *sta;
	__le16 llid, plid, reason;
	struct net_device *dev = NULL;
	struct ieee80211_sub_if_data *sdata;
#ifdef CONFIG_MAC80211_VERBOSE_MPL_DEBUG
	DECLARE_MAC_BUF(mac);
#endif
	u16 reason = 0;
	struct ieee80211_sub_if_data *sdata;
	struct mesh_config *mshcfg;
	enum ieee80211_self_protected_actioncode action = 0;

	/*
	 * This STA is valid because sta_info_destroy() will
	 * del_timer_sync() this timer after having made sure
	 * it cannot be readded (by deleting the plink.)
	 */
	sta = (struct sta_info *) data;

	spin_lock_bh(&sta->lock);
	if (sta->ignore_plink_timer) {
		sta->ignore_plink_timer = false;
		spin_unlock_bh(&sta->lock);
		return;
	}
	mpl_dbg("Mesh plink timer for %s fired on state %d\n",
			print_mac(mac, sta->addr), sta->plink_state);
	reason = 0;
	llid = sta->llid;
	plid = sta->plid;
	sdata = sta->sdata;
	dev = sdata->dev;

	switch (sta->plink_state) {
	case PLINK_OPN_RCVD:
	case PLINK_OPN_SNT:
		/* retry timer */
		if (sta->plink_retries < dot11MeshMaxRetries(sdata)) {
			u32 rand;
			mpl_dbg("Mesh plink for %s (retry, timeout): %d %d\n",
					print_mac(mac, sta->addr),
					sta->plink_retries, sta->plink_timeout);
			get_random_bytes(&rand, sizeof(u32));
			sta->plink_timeout = sta->plink_timeout +
					     rand % sta->plink_timeout;
			++sta->plink_retries;
			mod_plink_timer(sta, sta->plink_timeout);
			spin_unlock_bh(&sta->lock);
			mesh_plink_frame_tx(dev, PLINK_OPEN, sta->addr, llid,
					    0, 0);
			break;
		}
		reason = cpu_to_le16(MESH_MAX_RETRIES);
		/* fall through on else */
	case PLINK_CNF_RCVD:
		/* confirm timer */
		if (!reason)
			reason = cpu_to_le16(MESH_CONFIRM_TIMEOUT);
		sta->plink_state = PLINK_HOLDING;
		mod_plink_timer(sta, dot11MeshHoldingTimeout(sdata));
		spin_unlock_bh(&sta->lock);
		mesh_plink_frame_tx(dev, PLINK_CLOSE, sta->addr, llid, plid,
				    reason);
		break;
	case PLINK_HOLDING:
		/* holding timer */
		del_timer(&sta->plink_timer);
		mesh_plink_fsm_restart(sta);
		spin_unlock_bh(&sta->lock);
		break;
	default:
		spin_unlock_bh(&sta->lock);
		break;
	}
}

static inline void mesh_plink_timer_set(struct sta_info *sta, int timeout)
{
	sta->plink_timer.expires = jiffies + (HZ * timeout / 1000);
	sta->plink_timer.data = (unsigned long) sta;
	sta->plink_timer.function = mesh_plink_timer;
	sta->plink_timeout = timeout;
	add_timer(&sta->plink_timer);
}

int mesh_plink_open(struct sta_info *sta)
{
	__le16 llid;
	struct ieee80211_sub_if_data *sdata = sta->sdata;
#ifdef CONFIG_MAC80211_VERBOSE_MPL_DEBUG
	DECLARE_MAC_BUF(mac);
#endif

	spin_lock_bh(&sta->lock);
	get_random_bytes(&llid, 2);
	sta->llid = llid;
	if (sta->plink_state != PLINK_LISTEN) {
		spin_unlock_bh(&sta->lock);
		return -EBUSY;
	}
	sta->plink_state = PLINK_OPN_SNT;
	mesh_plink_timer_set(sta, dot11MeshRetryTimeout(sdata));
	spin_unlock_bh(&sta->lock);
	mpl_dbg("Mesh plink: starting establishment with %s\n",
		print_mac(mac, sta->addr));

	return mesh_plink_frame_tx(sdata->dev, PLINK_OPEN,
				   sta->addr, llid, 0, 0);
}

void mesh_plink_block(struct sta_info *sta)
{
#ifdef CONFIG_MAC80211_VERBOSE_MPL_DEBUG
	DECLARE_MAC_BUF(mac);
#endif

	spin_lock_bh(&sta->lock);
	__mesh_plink_deactivate(sta);
	sta->plink_state = PLINK_BLOCKED;
	spin_unlock_bh(&sta->lock);
}

int mesh_plink_close(struct sta_info *sta)
{
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	__le16 llid, plid, reason;
#ifdef CONFIG_MAC80211_VERBOSE_MPL_DEBUG
	DECLARE_MAC_BUF(mac);
#endif

	mpl_dbg("Mesh plink: closing link with %s\n",
			print_mac(mac, sta->addr));
	spin_lock_bh(&sta->lock);
	sta->reason = cpu_to_le16(MESH_LINK_CANCELLED);
	reason = sta->reason;

	if (sta->plink_state == PLINK_LISTEN ||
	    sta->plink_state == PLINK_BLOCKED) {
		mesh_plink_fsm_restart(sta);
		spin_unlock_bh(&sta->lock);
		return 0;
	} else if (sta->plink_state == PLINK_ESTAB) {
		__mesh_plink_deactivate(sta);
		/* The timer should not be running */
		mod_plink_timer(sta, dot11MeshHoldingTimeout(sdata));
	} else if (!mod_plink_timer(sta, dot11MeshHoldingTimeout(sdata)))
		sta->ignore_plink_timer = true;

	sta->plink_state = PLINK_HOLDING;
	llid = sta->llid;
	plid = sta->plid;
	spin_unlock_bh(&sta->lock);
	mesh_plink_frame_tx(sta->sdata->dev, PLINK_CLOSE, sta->addr, llid,
			    plid, reason);
	return 0;
}

void mesh_rx_plink_frame(struct net_device *dev, struct ieee80211_mgmt *mgmt,
			 size_t len, struct ieee80211_rx_status *rx_status)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;
	struct ieee802_11_elems elems;
	struct sta_info *sta;
	enum plink_event event;
	enum plink_frame_type ftype;
	size_t baselen;
	u8 ie_len;
	u8 *baseaddr;
	__le16 plid, llid, reason;
#ifdef CONFIG_MAC80211_VERBOSE_MPL_DEBUG
	DECLARE_MAC_BUF(mac);
#endif

	if (is_multicast_ether_addr(mgmt->da)) {
		mpl_dbg("Mesh plink: ignore frame from multicast address");
		return;
	}

	baseaddr = mgmt->u.action.u.plink_action.variable;
	baselen = (u8 *) mgmt->u.action.u.plink_action.variable - (u8 *) mgmt;
	if (mgmt->u.action.u.plink_action.action_code == PLINK_CONFIRM) {
		baseaddr += 4;
		baselen -= 4;
	}
	ieee802_11_parse_elems(baseaddr, len - baselen, &elems);
	if (!elems.peer_link) {
		mpl_dbg("Mesh plink: missing necessary peer link ie\n");
		return;
	}

	ftype = *((u8 *)PLINK_GET_FRAME_SUBTYPE(elems.peer_link));
	ie_len = elems.peer_link_len;
	if ((ftype == PLINK_OPEN && ie_len != 3) ||
	    (ftype == PLINK_CONFIRM && ie_len != 5) ||
	    (ftype == PLINK_CLOSE && ie_len != 5 && ie_len != 7)) {
		mpl_dbg("Mesh plink: incorrect plink ie length\n");
		return;
	}

	if (ftype != PLINK_CLOSE && (!elems.mesh_id || !elems.mesh_config)) {
		mpl_dbg("Mesh plink: missing necessary ie\n");
	if (sta->sdata->local->quiescing)
		return;

	spin_lock_bh(&sta->mesh->plink_lock);

	/* If a timer fires just before a state transition on another CPU,
	 * we may have already extended the timeout and changed state by the
	 * time we've acquired the lock and arrived  here.  In that case,
	 * skip this timer and wait for the new one.
	 */
	if (time_before(jiffies, sta->mesh->plink_timer.expires)) {
		mpl_dbg(sta->sdata,
			"Ignoring timer for %pM in state %s (timer adjusted)",
			sta->sta.addr, mplstates[sta->mesh->plink_state]);
		spin_unlock_bh(&sta->mesh->plink_lock);
		return;
	}

	/* del_timer() and handler may race when entering these states */
	if (sta->mesh->plink_state == NL80211_PLINK_LISTEN ||
	    sta->mesh->plink_state == NL80211_PLINK_ESTAB) {
		mpl_dbg(sta->sdata,
			"Ignoring timer for %pM in state %s (timer deleted)",
			sta->sta.addr, mplstates[sta->mesh->plink_state]);
		spin_unlock_bh(&sta->mesh->plink_lock);
		return;
	}

	mpl_dbg(sta->sdata,
		"Mesh plink timer for %pM fired on state %s\n",
		sta->sta.addr, mplstates[sta->mesh->plink_state]);
	sdata = sta->sdata;
	mshcfg = &sdata->u.mesh.mshcfg;

	switch (sta->mesh->plink_state) {
	case NL80211_PLINK_OPN_RCVD:
	case NL80211_PLINK_OPN_SNT:
		/* retry timer */
		if (sta->mesh->plink_retries < mshcfg->dot11MeshMaxRetries) {
			u32 rand;
			mpl_dbg(sta->sdata,
				"Mesh plink for %pM (retry, timeout): %d %d\n",
				sta->sta.addr, sta->mesh->plink_retries,
				sta->mesh->plink_timeout);
			get_random_bytes(&rand, sizeof(u32));
			sta->mesh->plink_timeout = sta->mesh->plink_timeout +
					     rand % sta->mesh->plink_timeout;
			++sta->mesh->plink_retries;
			mod_plink_timer(sta, sta->mesh->plink_timeout);
			action = WLAN_SP_MESH_PEERING_OPEN;
			break;
		}
		reason = WLAN_REASON_MESH_MAX_RETRIES;
		/* fall through on else */
	case NL80211_PLINK_CNF_RCVD:
		/* confirm timer */
		if (!reason)
			reason = WLAN_REASON_MESH_CONFIRM_TIMEOUT;
		sta->mesh->plink_state = NL80211_PLINK_HOLDING;
		mod_plink_timer(sta, mshcfg->dot11MeshHoldingTimeout);
		action = WLAN_SP_MESH_PEERING_CLOSE;
		break;
	case NL80211_PLINK_HOLDING:
		/* holding timer */
		del_timer(&sta->mesh->plink_timer);
		mesh_plink_fsm_restart(sta);
		break;
	default:
		break;
	}
	spin_unlock_bh(&sta->mesh->plink_lock);
	if (action)
		mesh_plink_frame_tx(sdata, sta, action, sta->sta.addr,
				    sta->mesh->llid, sta->mesh->plid, reason);
}

static inline void mesh_plink_timer_set(struct sta_info *sta, u32 timeout)
{
	sta->mesh->plink_timer.expires = jiffies + msecs_to_jiffies(timeout);
	sta->mesh->plink_timer.data = (unsigned long) sta;
	sta->mesh->plink_timer.function = mesh_plink_timer;
	sta->mesh->plink_timeout = timeout;
	add_timer(&sta->mesh->plink_timer);
}

static bool llid_in_use(struct ieee80211_sub_if_data *sdata,
			u16 llid)
{
	struct ieee80211_local *local = sdata->local;
	bool in_use = false;
	struct sta_info *sta;

	rcu_read_lock();
	list_for_each_entry_rcu(sta, &local->sta_list, list) {
		if (sdata != sta->sdata)
			continue;

		if (!memcmp(&sta->mesh->llid, &llid, sizeof(llid))) {
			in_use = true;
			break;
		}
	}
	rcu_read_unlock();

	return in_use;
}

static u16 mesh_get_new_llid(struct ieee80211_sub_if_data *sdata)
{
	u16 llid;

	do {
		get_random_bytes(&llid, sizeof(llid));
	} while (llid_in_use(sdata, llid));

	return llid;
}

u32 mesh_plink_open(struct sta_info *sta)
{
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	u32 changed;

	if (!test_sta_flag(sta, WLAN_STA_AUTH))
		return 0;

	spin_lock_bh(&sta->mesh->plink_lock);
	sta->mesh->llid = mesh_get_new_llid(sdata);
	if (sta->mesh->plink_state != NL80211_PLINK_LISTEN &&
	    sta->mesh->plink_state != NL80211_PLINK_BLOCKED) {
		spin_unlock_bh(&sta->mesh->plink_lock);
		return 0;
	}
	sta->mesh->plink_state = NL80211_PLINK_OPN_SNT;
	mesh_plink_timer_set(sta, sdata->u.mesh.mshcfg.dot11MeshRetryTimeout);
	spin_unlock_bh(&sta->mesh->plink_lock);
	mpl_dbg(sdata,
		"Mesh plink: starting establishment with %pM\n",
		sta->sta.addr);

	/* set the non-peer mode to active during peering */
	changed = ieee80211_mps_local_status_update(sdata);

	mesh_plink_frame_tx(sdata, sta, WLAN_SP_MESH_PEERING_OPEN,
			    sta->sta.addr, sta->mesh->llid, 0, 0);
	return changed;
}

u32 mesh_plink_block(struct sta_info *sta)
{
	u32 changed;

	spin_lock_bh(&sta->mesh->plink_lock);
	changed = __mesh_plink_deactivate(sta);
	sta->mesh->plink_state = NL80211_PLINK_BLOCKED;
	spin_unlock_bh(&sta->mesh->plink_lock);

	return changed;
}

static void mesh_plink_close(struct ieee80211_sub_if_data *sdata,
			     struct sta_info *sta,
			     enum plink_event event)
{
	struct mesh_config *mshcfg = &sdata->u.mesh.mshcfg;
	u16 reason = (event == CLS_ACPT) ?
		     WLAN_REASON_MESH_CLOSE : WLAN_REASON_MESH_CONFIG;

	sta->mesh->reason = reason;
	sta->mesh->plink_state = NL80211_PLINK_HOLDING;
	mod_plink_timer(sta, mshcfg->dot11MeshHoldingTimeout);
}

static u32 mesh_plink_establish(struct ieee80211_sub_if_data *sdata,
				struct sta_info *sta)
{
	struct mesh_config *mshcfg = &sdata->u.mesh.mshcfg;
	u32 changed = 0;

	del_timer(&sta->mesh->plink_timer);
	sta->mesh->plink_state = NL80211_PLINK_ESTAB;
	changed |= mesh_plink_inc_estab_count(sdata);
	changed |= mesh_set_ht_prot_mode(sdata);
	changed |= mesh_set_short_slot_time(sdata);
	mpl_dbg(sdata, "Mesh plink with %pM ESTABLISHED\n", sta->sta.addr);
	ieee80211_mps_sta_status_update(sta);
	changed |= ieee80211_mps_set_sta_local_pm(sta, mshcfg->power_mode);
	return changed;
}

/**
 * mesh_plink_fsm - step @sta MPM based on @event
 *
 * @sdata: interface
 * @sta: mesh neighbor
 * @event: peering event
 *
 * Return: changed MBSS flags
 */
static u32 mesh_plink_fsm(struct ieee80211_sub_if_data *sdata,
			  struct sta_info *sta, enum plink_event event)
{
	struct mesh_config *mshcfg = &sdata->u.mesh.mshcfg;
	enum ieee80211_self_protected_actioncode action = 0;
	u32 changed = 0;

	mpl_dbg(sdata, "peer %pM in state %s got event %s\n", sta->sta.addr,
		mplstates[sta->mesh->plink_state], mplevents[event]);

	spin_lock_bh(&sta->mesh->plink_lock);
	switch (sta->mesh->plink_state) {
	case NL80211_PLINK_LISTEN:
		switch (event) {
		case CLS_ACPT:
			mesh_plink_fsm_restart(sta);
			break;
		case OPN_ACPT:
			sta->mesh->plink_state = NL80211_PLINK_OPN_RCVD;
			sta->mesh->llid = mesh_get_new_llid(sdata);
			mesh_plink_timer_set(sta,
					     mshcfg->dot11MeshRetryTimeout);

			/* set the non-peer mode to active during peering */
			changed |= ieee80211_mps_local_status_update(sdata);
			action = WLAN_SP_MESH_PEERING_OPEN;
			break;
		default:
			break;
		}
		break;
	case NL80211_PLINK_OPN_SNT:
		switch (event) {
		case OPN_RJCT:
		case CNF_RJCT:
		case CLS_ACPT:
			mesh_plink_close(sdata, sta, event);
			action = WLAN_SP_MESH_PEERING_CLOSE;
			break;
		case OPN_ACPT:
			/* retry timer is left untouched */
			sta->mesh->plink_state = NL80211_PLINK_OPN_RCVD;
			action = WLAN_SP_MESH_PEERING_CONFIRM;
			break;
		case CNF_ACPT:
			sta->mesh->plink_state = NL80211_PLINK_CNF_RCVD;
			mod_plink_timer(sta, mshcfg->dot11MeshConfirmTimeout);
			break;
		default:
			break;
		}
		break;
	case NL80211_PLINK_OPN_RCVD:
		switch (event) {
		case OPN_RJCT:
		case CNF_RJCT:
		case CLS_ACPT:
			mesh_plink_close(sdata, sta, event);
			action = WLAN_SP_MESH_PEERING_CLOSE;
			break;
		case OPN_ACPT:
			action = WLAN_SP_MESH_PEERING_CONFIRM;
			break;
		case CNF_ACPT:
			changed |= mesh_plink_establish(sdata, sta);
			break;
		default:
			break;
		}
		break;
	case NL80211_PLINK_CNF_RCVD:
		switch (event) {
		case OPN_RJCT:
		case CNF_RJCT:
		case CLS_ACPT:
			mesh_plink_close(sdata, sta, event);
			action = WLAN_SP_MESH_PEERING_CLOSE;
			break;
		case OPN_ACPT:
			changed |= mesh_plink_establish(sdata, sta);
			action = WLAN_SP_MESH_PEERING_CONFIRM;
			break;
		default:
			break;
		}
		break;
	case NL80211_PLINK_ESTAB:
		switch (event) {
		case CLS_ACPT:
			changed |= __mesh_plink_deactivate(sta);
			changed |= mesh_set_ht_prot_mode(sdata);
			changed |= mesh_set_short_slot_time(sdata);
			mesh_plink_close(sdata, sta, event);
			action = WLAN_SP_MESH_PEERING_CLOSE;
			break;
		case OPN_ACPT:
			action = WLAN_SP_MESH_PEERING_CONFIRM;
			break;
		default:
			break;
		}
		break;
	case NL80211_PLINK_HOLDING:
		switch (event) {
		case CLS_ACPT:
			del_timer(&sta->mesh->plink_timer);
			mesh_plink_fsm_restart(sta);
			break;
		case OPN_ACPT:
		case CNF_ACPT:
		case OPN_RJCT:
		case CNF_RJCT:
			action = WLAN_SP_MESH_PEERING_CLOSE;
			break;
		default:
			break;
		}
		break;
	default:
		/* should not get here, PLINK_BLOCKED is dealt with at the
		 * beginning of the function
		 */
		break;
	}
	spin_unlock_bh(&sta->mesh->plink_lock);
	if (action) {
		mesh_plink_frame_tx(sdata, sta, action, sta->sta.addr,
				    sta->mesh->llid, sta->mesh->plid,
				    sta->mesh->reason);

		/* also send confirm in open case */
		if (action == WLAN_SP_MESH_PEERING_OPEN) {
			mesh_plink_frame_tx(sdata, sta,
					    WLAN_SP_MESH_PEERING_CONFIRM,
					    sta->sta.addr, sta->mesh->llid,
					    sta->mesh->plid, 0);
		}
	}

	return changed;
}

/*
 * mesh_plink_get_event - get correct MPM event
 *
 * @sdata: interface
 * @sta: peer, leave NULL if processing a frame from a new suitable peer
 * @elems: peering management IEs
 * @ftype: frame type
 * @llid: peer's peer link ID
 * @plid: peer's local link ID
 *
 * Return: new peering event for @sta, but PLINK_UNDEFINED should be treated as
 * an error.
 */
static enum plink_event
mesh_plink_get_event(struct ieee80211_sub_if_data *sdata,
		     struct sta_info *sta,
		     struct ieee802_11_elems *elems,
		     enum ieee80211_self_protected_actioncode ftype,
		     u16 llid, u16 plid)
{
	enum plink_event event = PLINK_UNDEFINED;
	u8 ie_len = elems->peering_len;
	bool matches_local;

	matches_local = (ftype == WLAN_SP_MESH_PEERING_CLOSE ||
			 mesh_matches_local(sdata, elems));

	/* deny open request from non-matching peer */
	if (!matches_local && !sta) {
		event = OPN_RJCT;
		goto out;
	}

	if (!sta) {
		if (ftype != WLAN_SP_MESH_PEERING_OPEN) {
			mpl_dbg(sdata, "Mesh plink: cls or cnf from unknown peer\n");
			goto out;
		}
		/* ftype == WLAN_SP_MESH_PEERING_OPEN */
		if (!mesh_plink_free_count(sdata)) {
			mpl_dbg(sdata, "Mesh plink error: no more free plinks\n");
			goto out;
		}
	} else {
		if (!test_sta_flag(sta, WLAN_STA_AUTH)) {
			mpl_dbg(sdata, "Mesh plink: Action frame from non-authed peer\n");
			goto out;
		}
		if (sta->mesh->plink_state == NL80211_PLINK_BLOCKED)
			goto out;
	}

	/* new matching peer */
	if (!sta) {
		event = OPN_ACPT;
		goto out;
	}

	switch (ftype) {
	case WLAN_SP_MESH_PEERING_OPEN:
		if (!matches_local)
			event = OPN_RJCT;
		else if (!mesh_plink_free_count(sdata) ||
			 (sta->mesh->plid && sta->mesh->plid != plid))
			event = OPN_IGNR;
		else
			event = OPN_ACPT;
		break;
	case WLAN_SP_MESH_PEERING_CONFIRM:
		if (!matches_local)
			event = CNF_RJCT;
		else if (!mesh_plink_free_count(sdata) ||
			 sta->mesh->llid != llid ||
			 (sta->mesh->plid && sta->mesh->plid != plid))
			event = CNF_IGNR;
		else
			event = CNF_ACPT;
		break;
	case WLAN_SP_MESH_PEERING_CLOSE:
		if (sta->mesh->plink_state == NL80211_PLINK_ESTAB)
			/* Do not check for llid or plid. This does not
			 * follow the standard but since multiple plinks
			 * per sta are not supported, it is necessary in
			 * order to avoid a livelock when MP A sees an
			 * establish peer link to MP B but MP B does not
			 * see it. This can be caused by a timeout in
			 * B's peer link establishment or B beign
			 * restarted.
			 */
			event = CLS_ACPT;
		else if (sta->mesh->plid != plid)
			event = CLS_IGNR;
		else if (ie_len == 8 && sta->mesh->llid != llid)
			event = CLS_IGNR;
		else
			event = CLS_ACPT;
		break;
	default:
		mpl_dbg(sdata, "Mesh plink: unknown frame subtype\n");
		break;
	}

out:
	return event;
}

static void
mesh_process_plink_frame(struct ieee80211_sub_if_data *sdata,
			 struct ieee80211_mgmt *mgmt,
			 struct ieee802_11_elems *elems)
{

	struct sta_info *sta;
	enum plink_event event;
	enum ieee80211_self_protected_actioncode ftype;
	u32 changed = 0;
	u8 ie_len = elems->peering_len;
	u16 plid, llid = 0;

	if (!elems->peering) {
		mpl_dbg(sdata,
			"Mesh plink: missing necessary peer link ie\n");
		return;
	}

	if (elems->rsn_len &&
	    sdata->u.mesh.security == IEEE80211_MESH_SEC_NONE) {
		mpl_dbg(sdata,
			"Mesh plink: can't establish link with secure peer\n");
		return;
	}

	ftype = mgmt->u.action.u.self_prot.action_code;
	if ((ftype == WLAN_SP_MESH_PEERING_OPEN && ie_len != 4) ||
	    (ftype == WLAN_SP_MESH_PEERING_CONFIRM && ie_len != 6) ||
	    (ftype == WLAN_SP_MESH_PEERING_CLOSE && ie_len != 6
							&& ie_len != 8)) {
		mpl_dbg(sdata,
			"Mesh plink: incorrect plink ie length %d %d\n",
			ftype, ie_len);
		return;
	}

	if (ftype != WLAN_SP_MESH_PEERING_CLOSE &&
	    (!elems->mesh_id || !elems->mesh_config)) {
		mpl_dbg(sdata, "Mesh plink: missing necessary ie\n");
		return;
	}
	/* Note the lines below are correct, the llid in the frame is the plid
	 * from the point of view of this host.
	 */
	memcpy(&plid, PLINK_GET_LLID(elems.peer_link), 2);
	if (ftype == PLINK_CONFIRM || (ftype == PLINK_CLOSE && ie_len == 7))
		memcpy(&llid, PLINK_GET_PLID(elems.peer_link), 2);

	rcu_read_lock();

	sta = sta_info_get(local, mgmt->sa);
	if (!sta && ftype != PLINK_OPEN) {
		mpl_dbg("Mesh plink: cls or cnf from unknown peer\n");
		rcu_read_unlock();
		return;
	}

	if (sta && sta->plink_state == PLINK_BLOCKED) {
		rcu_read_unlock();
		return;
	}

	/* Now we will figure out the appropriate event... */
	event = PLINK_UNDEFINED;
	if (ftype != PLINK_CLOSE && (!mesh_matches_local(&elems, dev))) {
		switch (ftype) {
		case PLINK_OPEN:
			event = OPN_RJCT;
			break;
		case PLINK_CONFIRM:
			event = CNF_RJCT;
			break;
		case PLINK_CLOSE:
			/* avoid warning */
			break;
		}
		spin_lock_bh(&sta->lock);
	} else if (!sta) {
		/* ftype == PLINK_OPEN */
		u64 rates;
		if (!mesh_plink_free_count(sdata)) {
			mpl_dbg("Mesh plink error: no more free plinks\n");
			rcu_read_unlock();
			return;
		}

		rates = ieee80211_sta_get_rates(local, &elems, rx_status->band);
		sta = mesh_plink_alloc(sdata, mgmt->sa, rates);
		if (!sta) {
			mpl_dbg("Mesh plink error: plink table full\n");
			rcu_read_unlock();
			return;
		}
		if (sta_info_insert(sta)) {
			rcu_read_unlock();
			return;
		}
		event = OPN_ACPT;
		spin_lock_bh(&sta->lock);
	} else {
		spin_lock_bh(&sta->lock);
		switch (ftype) {
		case PLINK_OPEN:
			if (!mesh_plink_free_count(sdata) ||
			    (sta->plid && sta->plid != plid))
				event = OPN_IGNR;
			else
				event = OPN_ACPT;
			break;
		case PLINK_CONFIRM:
			if (!mesh_plink_free_count(sdata) ||
			    (sta->llid != llid || sta->plid != plid))
				event = CNF_IGNR;
			else
				event = CNF_ACPT;
			break;
		case PLINK_CLOSE:
			if (sta->plink_state == PLINK_ESTAB)
				/* Do not check for llid or plid. This does not
				 * follow the standard but since multiple plinks
				 * per sta are not supported, it is necessary in
				 * order to avoid a livelock when MP A sees an
				 * establish peer link to MP B but MP B does not
				 * see it. This can be caused by a timeout in
				 * B's peer link establishment or B beign
				 * restarted.
				 */
				event = CLS_ACPT;
			else if (sta->plid != plid)
				event = CLS_IGNR;
			else if (ie_len == 7 && sta->llid != llid)
				event = CLS_IGNR;
			else
				event = CLS_ACPT;
			break;
		default:
			mpl_dbg("Mesh plink: unknown frame subtype\n");
			spin_unlock_bh(&sta->lock);
			rcu_read_unlock();
			return;
		}
	}

	mpl_dbg("Mesh plink (peer, state, llid, plid, event): %s %d %d %d %d\n",
			print_mac(mac, mgmt->sa), sta->plink_state,
			le16_to_cpu(sta->llid), le16_to_cpu(sta->plid),
			event);
	reason = 0;
	switch (sta->plink_state) {
		/* spin_unlock as soon as state is updated at each case */
	case PLINK_LISTEN:
		switch (event) {
		case CLS_ACPT:
			mesh_plink_fsm_restart(sta);
			spin_unlock_bh(&sta->lock);
			break;
		case OPN_ACPT:
			sta->plink_state = PLINK_OPN_RCVD;
			sta->plid = plid;
			get_random_bytes(&llid, 2);
			sta->llid = llid;
			mesh_plink_timer_set(sta, dot11MeshRetryTimeout(sdata));
			spin_unlock_bh(&sta->lock);
			mesh_plink_frame_tx(dev, PLINK_OPEN, sta->addr, llid,
					    0, 0);
			mesh_plink_frame_tx(dev, PLINK_CONFIRM, sta->addr,
					    llid, plid, 0);
			break;
		default:
			spin_unlock_bh(&sta->lock);
			break;
		}
		break;

	case PLINK_OPN_SNT:
		switch (event) {
		case OPN_RJCT:
		case CNF_RJCT:
			reason = cpu_to_le16(MESH_CAPABILITY_POLICY_VIOLATION);
		case CLS_ACPT:
			if (!reason)
				reason = cpu_to_le16(MESH_CLOSE_RCVD);
			sta->reason = reason;
			sta->plink_state = PLINK_HOLDING;
			if (!mod_plink_timer(sta,
					     dot11MeshHoldingTimeout(sdata)))
				sta->ignore_plink_timer = true;

			llid = sta->llid;
			spin_unlock_bh(&sta->lock);
			mesh_plink_frame_tx(dev, PLINK_CLOSE, sta->addr, llid,
					    plid, reason);
			break;
		case OPN_ACPT:
			/* retry timer is left untouched */
			sta->plink_state = PLINK_OPN_RCVD;
			sta->plid = plid;
			llid = sta->llid;
			spin_unlock_bh(&sta->lock);
			mesh_plink_frame_tx(dev, PLINK_CONFIRM, sta->addr, llid,
					    plid, 0);
			break;
		case CNF_ACPT:
			sta->plink_state = PLINK_CNF_RCVD;
			if (!mod_plink_timer(sta,
					     dot11MeshConfirmTimeout(sdata)))
				sta->ignore_plink_timer = true;

			spin_unlock_bh(&sta->lock);
			break;
		default:
			spin_unlock_bh(&sta->lock);
			break;
		}
		break;

	case PLINK_OPN_RCVD:
		switch (event) {
		case OPN_RJCT:
		case CNF_RJCT:
			reason = cpu_to_le16(MESH_CAPABILITY_POLICY_VIOLATION);
		case CLS_ACPT:
			if (!reason)
				reason = cpu_to_le16(MESH_CLOSE_RCVD);
			sta->reason = reason;
			sta->plink_state = PLINK_HOLDING;
			if (!mod_plink_timer(sta,
					     dot11MeshHoldingTimeout(sdata)))
				sta->ignore_plink_timer = true;

			llid = sta->llid;
			spin_unlock_bh(&sta->lock);
			mesh_plink_frame_tx(dev, PLINK_CLOSE, sta->addr, llid,
					    plid, reason);
			break;
		case OPN_ACPT:
			llid = sta->llid;
			spin_unlock_bh(&sta->lock);
			mesh_plink_frame_tx(dev, PLINK_CONFIRM, sta->addr, llid,
					    plid, 0);
			break;
		case CNF_ACPT:
			del_timer(&sta->plink_timer);
			sta->plink_state = PLINK_ESTAB;
			mesh_plink_inc_estab_count(sdata);
			spin_unlock_bh(&sta->lock);
			mpl_dbg("Mesh plink with %s ESTABLISHED\n",
					print_mac(mac, sta->addr));
			break;
		default:
			spin_unlock_bh(&sta->lock);
			break;
		}
		break;

	case PLINK_CNF_RCVD:
		switch (event) {
		case OPN_RJCT:
		case CNF_RJCT:
			reason = cpu_to_le16(MESH_CAPABILITY_POLICY_VIOLATION);
		case CLS_ACPT:
			if (!reason)
				reason = cpu_to_le16(MESH_CLOSE_RCVD);
			sta->reason = reason;
			sta->plink_state = PLINK_HOLDING;
			if (!mod_plink_timer(sta,
					     dot11MeshHoldingTimeout(sdata)))
				sta->ignore_plink_timer = true;

			llid = sta->llid;
			spin_unlock_bh(&sta->lock);
			mesh_plink_frame_tx(dev, PLINK_CLOSE, sta->addr, llid,
					    plid, reason);
			break;
		case OPN_ACPT:
			del_timer(&sta->plink_timer);
			sta->plink_state = PLINK_ESTAB;
			mesh_plink_inc_estab_count(sdata);
			spin_unlock_bh(&sta->lock);
			mpl_dbg("Mesh plink with %s ESTABLISHED\n",
					print_mac(mac, sta->addr));
			mesh_plink_frame_tx(dev, PLINK_CONFIRM, sta->addr, llid,
					    plid, 0);
			break;
		default:
			spin_unlock_bh(&sta->lock);
			break;
		}
		break;

	case PLINK_ESTAB:
		switch (event) {
		case CLS_ACPT:
			reason = cpu_to_le16(MESH_CLOSE_RCVD);
			sta->reason = reason;
			__mesh_plink_deactivate(sta);
			sta->plink_state = PLINK_HOLDING;
			llid = sta->llid;
			mod_plink_timer(sta, dot11MeshHoldingTimeout(sdata));
			spin_unlock_bh(&sta->lock);
			mesh_plink_frame_tx(dev, PLINK_CLOSE, sta->addr, llid,
					    plid, reason);
			break;
		case OPN_ACPT:
			llid = sta->llid;
			spin_unlock_bh(&sta->lock);
			mesh_plink_frame_tx(dev, PLINK_CONFIRM, sta->addr, llid,
					    plid, 0);
			break;
		default:
			spin_unlock_bh(&sta->lock);
			break;
		}
		break;
	case PLINK_HOLDING:
		switch (event) {
		case CLS_ACPT:
			if (del_timer(&sta->plink_timer))
				sta->ignore_plink_timer = 1;
			mesh_plink_fsm_restart(sta);
			spin_unlock_bh(&sta->lock);
			break;
		case OPN_ACPT:
		case CNF_ACPT:
		case OPN_RJCT:
		case CNF_RJCT:
			llid = sta->llid;
			reason = sta->reason;
			spin_unlock_bh(&sta->lock);
			mesh_plink_frame_tx(dev, PLINK_CLOSE, sta->addr, llid,
					    plid, reason);
			break;
		default:
			spin_unlock_bh(&sta->lock);
		}
		break;
	default:
		/* should not get here, PLINK_BLOCKED is dealt with at the
		 * beggining of the function
		 */
		spin_unlock_bh(&sta->lock);
		break;
	}

	rcu_read_unlock();
	plid = get_unaligned_le16(PLINK_GET_LLID(elems->peering));
	if (ftype == WLAN_SP_MESH_PEERING_CONFIRM ||
	    (ftype == WLAN_SP_MESH_PEERING_CLOSE && ie_len == 8))
		llid = get_unaligned_le16(PLINK_GET_PLID(elems->peering));

	/* WARNING: Only for sta pointer, is dropped & re-acquired */
	rcu_read_lock();

	sta = sta_info_get(sdata, mgmt->sa);

	if (ftype == WLAN_SP_MESH_PEERING_OPEN &&
	    !rssi_threshold_check(sdata, sta)) {
		mpl_dbg(sdata, "Mesh plink: %pM does not meet rssi threshold\n",
			mgmt->sa);
		goto unlock_rcu;
	}

	/* Now we will figure out the appropriate event... */
	event = mesh_plink_get_event(sdata, sta, elems, ftype, llid, plid);

	if (event == OPN_ACPT) {
		rcu_read_unlock();
		/* allocate sta entry if necessary and update info */
		sta = mesh_sta_info_get(sdata, mgmt->sa, elems);
		if (!sta) {
			mpl_dbg(sdata, "Mesh plink: failed to init peer!\n");
			goto unlock_rcu;
		}
		sta->mesh->plid = plid;
	} else if (!sta && event == OPN_RJCT) {
		mesh_plink_frame_tx(sdata, NULL, WLAN_SP_MESH_PEERING_CLOSE,
				    mgmt->sa, 0, plid,
				    WLAN_REASON_MESH_CONFIG);
		goto unlock_rcu;
	} else if (!sta || event == PLINK_UNDEFINED) {
		/* something went wrong */
		goto unlock_rcu;
	}

	if (event == CNF_ACPT) {
		/* 802.11-2012 13.3.7.2 - update plid on CNF if not set */
		if (!sta->mesh->plid)
			sta->mesh->plid = plid;

		sta->mesh->aid = get_unaligned_le16(PLINK_CNF_AID(mgmt));
	}

	changed |= mesh_plink_fsm(sdata, sta, event);

unlock_rcu:
	rcu_read_unlock();

	if (changed)
		ieee80211_mbss_info_change_notify(sdata, changed);
}

void mesh_rx_plink_frame(struct ieee80211_sub_if_data *sdata,
			 struct ieee80211_mgmt *mgmt, size_t len,
			 struct ieee80211_rx_status *rx_status)
{
	struct ieee802_11_elems elems;
	size_t baselen;
	u8 *baseaddr;

	/* need action_code, aux */
	if (len < IEEE80211_MIN_ACTION_SIZE + 3)
		return;

	if (sdata->u.mesh.user_mpm)
		/* userspace must register for these */
		return;

	if (is_multicast_ether_addr(mgmt->da)) {
		mpl_dbg(sdata,
			"Mesh plink: ignore frame from multicast address\n");
		return;
	}

	baseaddr = mgmt->u.action.u.self_prot.variable;
	baselen = (u8 *) mgmt->u.action.u.self_prot.variable - (u8 *) mgmt;
	if (mgmt->u.action.u.self_prot.action_code ==
						WLAN_SP_MESH_PEERING_CONFIRM) {
		baseaddr += 4;
		baselen += 4;

		if (baselen > len)
			return;
	}
	ieee802_11_parse_elems(baseaddr, len - baselen, true, &elems);
	mesh_process_plink_frame(sdata, mgmt, &elems);
}
