/*
 * Copyright 2004, Instant802 Networks, Inc.
 * Copyright 2013-2014  Intel Mobile Communications GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/module.h>
#include <linux/if_arp.h>
#include <linux/types.h>
#include <net/ip.h>
#include <net/pkt_sched.h>

#include <net/mac80211.h>
#include "ieee80211_i.h"
#include "wme.h"

/* Default mapping in classifier to work with default
 * queue setup.
 */
const int ieee802_1d_to_ac[8] = { 2, 3, 3, 2, 1, 1, 0, 0 };

static const char llc_ip_hdr[8] = {0xAA, 0xAA, 0x3, 0, 0, 0, 0x08, 0};

/* Given a data frame determine the 802.1p/1d tag to use.  */
static unsigned int classify_1d(struct sk_buff *skb)
{
	unsigned int dscp;

	/* skb->priority values from 256->263 are magic values to
	 * directly indicate a specific 802.1d priority.  This is used
	 * to allow 802.1d priority to be passed directly in from VLAN
	 * tags, etc.
	 */
	if (skb->priority >= 256 && skb->priority <= 263)
		return skb->priority - 256;

	switch (skb->protocol) {
	case __constant_htons(ETH_P_IP):
		dscp = ip_hdr(skb)->tos & 0xfc;
		break;

	default:
		return 0;
	}

	if (dscp & 0x1c)
		return 0;
	return dscp >> 5;
}

const int ieee802_1d_to_ac[8] = {
	IEEE80211_AC_BE,
	IEEE80211_AC_BK,
	IEEE80211_AC_BK,
	IEEE80211_AC_BE,
	IEEE80211_AC_VI,
	IEEE80211_AC_VI,
	IEEE80211_AC_VO,
	IEEE80211_AC_VO
};

static int wme_downgrade_ac(struct sk_buff *skb)
{
	switch (skb->priority) {
	case 6:
	case 7:
		skb->priority = 5; /* VO -> VI */
		return 0;
	case 4:
	case 5:
		skb->priority = 3; /* VI -> BE */
		return 0;
	case 0:
	case 3:
		skb->priority = 2; /* BE -> BK */
		return 0;
	default:
		return -1;
	}
}


/* Indicate which queue to use.  */
static u16 classify80211(struct sk_buff *skb, struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;

	if (!ieee80211_is_data(hdr->frame_control)) {
		/* management frames go on AC_VO queue, but are sent
		* without QoS control fields */
		return 0;
	}

	if (0 /* injected */) {
		/* use AC from radiotap */
	}

	if (!ieee80211_is_data_qos(hdr->frame_control)) {
		skb->priority = 0; /* required for correct WPA/11i MIC */
		return ieee802_1d_to_ac[skb->priority];
	}

	/* use the data classifier to determine what 802.1d tag the
	 * data frame has */
	skb->priority = classify_1d(skb);

	/* in case we are a client verify acm is not set for this ac */
	while (unlikely(local->wmm_acm & BIT(skb->priority))) {
		if (wme_downgrade_ac(skb)) {
			/* The old code would drop the packet in this
			 * case.
			 */
			return 0;
		}
	}

/**
 * ieee80211_fix_reserved_tid - return the TID to use if this one is reserved
 * @tid: the assumed-reserved TID
 *
 * Returns: the alternative TID to use, or 0 on error
 */
static inline u8 ieee80211_fix_reserved_tid(u8 tid)
{
	switch (tid) {
	case 0:
		return 3;
	case 1:
		return 2;
	case 2:
		return 1;
	case 3:
		return 0;
	case 4:
		return 5;
	case 5:
		return 4;
	case 6:
		return 7;
	case 7:
		return 6;
	}

	return 0;
}

static u16 ieee80211_downgrade_queue(struct ieee80211_sub_if_data *sdata,
				     struct sta_info *sta, struct sk_buff *skb)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	/* in case we are a client verify acm is not set for this ac */
	while (sdata->wmm_acm & BIT(skb->priority)) {
		int ac = ieee802_1d_to_ac[skb->priority];

		if (ifmgd->tx_tspec[ac].admitted_time &&
		    skb->priority == ifmgd->tx_tspec[ac].up)
			return ac;

		if (wme_downgrade_ac(skb)) {
			/*
			 * This should not really happen. The AP has marked all
			 * lower ACs to require admission control which is not
			 * a reasonable configuration. Allow the frame to be
			 * transmitted using AC_BK as a workaround.
			 */
			break;
		}
	}

	/* Check to see if this is a reserved TID */
	if (sta && sta->reserved_tid == skb->priority)
		skb->priority = ieee80211_fix_reserved_tid(skb->priority);

	/* look up which queue to use for frames with this 1d tag */
	return ieee802_1d_to_ac[skb->priority];
}

u16 ieee80211_select_queue(struct net_device *dev, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct sta_info *sta;
	u16 queue;
	u8 tid;

	queue = classify80211(skb, dev);
	if (unlikely(queue >= local->hw.queues))
		queue = local->hw.queues - 1;

	if (info->flags & IEEE80211_TX_CTL_REQUEUE) {
		rcu_read_lock();
		sta = sta_info_get(local, hdr->addr1);
		tid = skb->priority & IEEE80211_QOS_CTL_TAG1D_MASK;
		if (sta) {
			struct ieee80211_hw *hw = &local->hw;
			int ampdu_queue = sta->tid_to_tx_q[tid];

			if ((ampdu_queue < ieee80211_num_queues(hw)) &&
			    test_bit(ampdu_queue, local->queue_pool)) {
				queue = ampdu_queue;
				info->flags |= IEEE80211_TX_CTL_AMPDU;
			} else {
				info->flags &= ~IEEE80211_TX_CTL_AMPDU;
			}
		}
		rcu_read_unlock();

		return queue;
	}

	/* Now we know the 1d priority, fill in the QoS header if
	 * there is one.
	 */
	if (ieee80211_is_data_qos(hdr->frame_control)) {
		u8 *p = ieee80211_get_qos_ctl(hdr);
		u8 ack_policy = 0;
		tid = skb->priority & IEEE80211_QOS_CTL_TAG1D_MASK;
		if (local->wifi_wme_noack_test)
			ack_policy |= QOS_CONTROL_ACK_POLICY_NOACK <<
					QOS_CONTROL_ACK_POLICY_SHIFT;
		/* qos header is 2 bytes, second reserved */
		*p++ = ack_policy | tid;
		*p = 0;

		rcu_read_lock();

		sta = sta_info_get(local, hdr->addr1);
		if (sta) {
			int ampdu_queue = sta->tid_to_tx_q[tid];
			struct ieee80211_hw *hw = &local->hw;

			if ((ampdu_queue < ieee80211_num_queues(hw)) &&
			    test_bit(ampdu_queue, local->queue_pool)) {
				queue = ampdu_queue;
				info->flags |= IEEE80211_TX_CTL_AMPDU;
			} else {
				info->flags &= ~IEEE80211_TX_CTL_AMPDU;
			}
		}

		rcu_read_unlock();
	}

	return queue;
}

int ieee80211_ht_agg_queue_add(struct ieee80211_local *local,
			       struct sta_info *sta, u16 tid)
{
	int i;

	/* XXX: currently broken due to cb/requeue use */
	return -EPERM;

	/* prepare the filter and save it for the SW queue
	 * matching the received HW queue */

	if (!local->hw.ampdu_queues)
		return -EPERM;

	/* try to get a Qdisc from the pool */
	for (i = local->hw.queues; i < ieee80211_num_queues(&local->hw); i++)
		if (!test_and_set_bit(i, local->queue_pool)) {
			ieee80211_stop_queue(local_to_hw(local), i);
			sta->tid_to_tx_q[tid] = i;

			/* IF there are already pending packets
			 * on this tid first we need to drain them
			 * on the previous queue
			 * since HT is strict in order */
#ifdef CONFIG_MAC80211_HT_DEBUG
			if (net_ratelimit()) {
				DECLARE_MAC_BUF(mac);
				printk(KERN_DEBUG "allocated aggregation queue"
					" %d tid %d addr %s pool=0x%lX\n",
					i, tid, print_mac(mac, sta->addr),
					local->queue_pool[0]);
			}
#endif /* CONFIG_MAC80211_HT_DEBUG */
			return 0;
		}

	return -EAGAIN;
}

/**
 * the caller needs to hold netdev_get_tx_queue(local->mdev, X)->lock
 */
void ieee80211_ht_agg_queue_remove(struct ieee80211_local *local,
				   struct sta_info *sta, u16 tid,
				   u8 requeue)
{
	int agg_queue = sta->tid_to_tx_q[tid];
	struct ieee80211_hw *hw = &local->hw;

	/* return the qdisc to the pool */
	clear_bit(agg_queue, local->queue_pool);
	sta->tid_to_tx_q[tid] = ieee80211_num_queues(hw);

	if (requeue) {
		ieee80211_requeue(local, agg_queue);
	} else {
		struct netdev_queue *txq;
		spinlock_t *root_lock;
		struct Qdisc *q;

		txq = netdev_get_tx_queue(local->mdev, agg_queue);
		q = rcu_dereference(txq->qdisc);
		root_lock = qdisc_lock(q);

		spin_lock_bh(root_lock);
		qdisc_reset(q);
		spin_unlock_bh(root_lock);
	}
}

void ieee80211_requeue(struct ieee80211_local *local, int queue)
{
	struct netdev_queue *txq = netdev_get_tx_queue(local->mdev, queue);
	struct sk_buff_head list;
	spinlock_t *root_lock;
	struct Qdisc *qdisc;
	u32 len;

	rcu_read_lock_bh();

	qdisc = rcu_dereference(txq->qdisc);
	if (!qdisc || !qdisc->dequeue)
		goto out_unlock;

	skb_queue_head_init(&list);

	root_lock = qdisc_root_lock(qdisc);
	spin_lock(root_lock);
	for (len = qdisc->q.qlen; len > 0; len--) {
		struct sk_buff *skb = qdisc->dequeue(qdisc);

		if (skb)
			__skb_queue_tail(&list, skb);
	}
	spin_unlock(root_lock);

	for (len = list.qlen; len > 0; len--) {
		struct sk_buff *skb = __skb_dequeue(&list);
		u16 new_queue;

		BUG_ON(!skb);
		new_queue = ieee80211_select_queue(local->mdev, skb);
		skb_set_queue_mapping(skb, new_queue);

		txq = netdev_get_tx_queue(local->mdev, new_queue);


		qdisc = rcu_dereference(txq->qdisc);
		root_lock = qdisc_root_lock(qdisc);

		spin_lock(root_lock);
		qdisc_enqueue_root(skb, qdisc);
		spin_unlock(root_lock);
	}

out_unlock:
	rcu_read_unlock_bh();
}
/* Indicate which queue to use for this fully formed 802.11 frame */
u16 ieee80211_select_queue_80211(struct ieee80211_sub_if_data *sdata,
				 struct sk_buff *skb,
				 struct ieee80211_hdr *hdr)
{
	struct ieee80211_local *local = sdata->local;
	u8 *p;

	if (local->hw.queues < IEEE80211_NUM_ACS)
		return 0;

	if (!ieee80211_is_data(hdr->frame_control)) {
		skb->priority = 7;
		return ieee802_1d_to_ac[skb->priority];
	}
	if (!ieee80211_is_data_qos(hdr->frame_control)) {
		skb->priority = 0;
		return ieee802_1d_to_ac[skb->priority];
	}

	p = ieee80211_get_qos_ctl(hdr);
	skb->priority = *p & IEEE80211_QOS_CTL_TAG1D_MASK;

	return ieee80211_downgrade_queue(sdata, NULL, skb);
}

/* Indicate which queue to use. */
u16 ieee80211_select_queue(struct ieee80211_sub_if_data *sdata,
			   struct sk_buff *skb)
{
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta = NULL;
	const u8 *ra = NULL;
	bool qos = false;
	struct mac80211_qos_map *qos_map;
	u16 ret;

	if (local->hw.queues < IEEE80211_NUM_ACS || skb->len < 6) {
		skb->priority = 0; /* required for correct WPA/11i MIC */
		return 0;
	}

	rcu_read_lock();
	switch (sdata->vif.type) {
	case NL80211_IFTYPE_AP_VLAN:
		sta = rcu_dereference(sdata->u.vlan.sta);
		if (sta) {
			qos = sta->sta.wme;
			break;
		}
	case NL80211_IFTYPE_AP:
		ra = skb->data;
		break;
	case NL80211_IFTYPE_WDS:
		ra = sdata->u.wds.remote_addr;
		break;
#ifdef CONFIG_MAC80211_MESH
	case NL80211_IFTYPE_MESH_POINT:
		qos = true;
		break;
#endif
	case NL80211_IFTYPE_STATION:
		/* might be a TDLS station */
		sta = sta_info_get(sdata, skb->data);
		if (sta)
			qos = sta->sta.wme;

		ra = sdata->u.mgd.bssid;
		break;
	case NL80211_IFTYPE_ADHOC:
		ra = skb->data;
		break;
	case NL80211_IFTYPE_OCB:
		/* all stations are required to support WME */
		qos = true;
		break;
	default:
		break;
	}

	if (!sta && ra && !is_multicast_ether_addr(ra)) {
		sta = sta_info_get(sdata, ra);
		if (sta)
			qos = sta->sta.wme;
	}

	if (!qos) {
		skb->priority = 0; /* required for correct WPA/11i MIC */
		ret = IEEE80211_AC_BE;
		goto out;
	}

	if (skb->protocol == sdata->control_port_protocol) {
		skb->priority = 7;
		goto downgrade;
	}

	/* use the data classifier to determine what 802.1d tag the
	 * data frame has */
	qos_map = rcu_dereference(sdata->qos_map);
	skb->priority = cfg80211_classify8021d(skb, qos_map ?
					       &qos_map->qos_map : NULL);

 downgrade:
	ret = ieee80211_downgrade_queue(sdata, sta, skb);
 out:
	rcu_read_unlock();
	return ret;
}

/**
 * ieee80211_set_qos_hdr - Fill in the QoS header if there is one.
 *
 * @sdata: local subif
 * @skb: packet to be updated
 */
void ieee80211_set_qos_hdr(struct ieee80211_sub_if_data *sdata,
			   struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (void *)skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	u8 *p;
	u8 ack_policy, tid;

	if (!ieee80211_is_data_qos(hdr->frame_control))
		return;

	p = ieee80211_get_qos_ctl(hdr);
	tid = skb->priority & IEEE80211_QOS_CTL_TAG1D_MASK;

	/* preserve EOSP bit */
	ack_policy = *p & IEEE80211_QOS_CTL_EOSP;

	if (is_multicast_ether_addr(hdr->addr1) ||
	    sdata->noack_map & BIT(tid)) {
		ack_policy |= IEEE80211_QOS_CTL_ACK_POLICY_NOACK;
		info->flags |= IEEE80211_TX_CTL_NO_ACK;
	}

	/* qos header is 2 bytes */
	*p++ = ack_policy | tid;
	if (ieee80211_vif_is_mesh(&sdata->vif)) {
		/* preserve RSPI and Mesh PS Level bit */
		*p &= ((IEEE80211_QOS_CTL_RSPI |
			IEEE80211_QOS_CTL_MESH_PS_LEVEL) >> 8);

		/* Nulls don't have a mesh header (frame body) */
		if (!ieee80211_is_qos_nullfunc(hdr->frame_control))
			*p |= (IEEE80211_QOS_CTL_MESH_CONTROL_PRESENT >> 8);
	} else {
		*p = 0;
	}
}
