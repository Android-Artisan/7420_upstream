/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2013-2014  Intel Mobile Communications GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <net/mac80211.h>
#include <net/ieee80211_radiotap.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/wireless.h>
#include <linux/rtnetlink.h>
#include <linux/bitmap.h>
#include <net/net_namespace.h>
#include <net/cfg80211.h>

#include "ieee80211_i.h"
#include "rate.h"
#include "mesh.h"
#include "wep.h"
#include "wme.h"
#include "aes_ccm.h"
#include "led.h"
#include "cfg.h"
#include "debugfs.h"
#include "debugfs_netdev.h"

/*
 * For seeing transmitted packets on monitor interfaces
 * we have a radiotap header too.
 */
struct ieee80211_tx_status_rtap_hdr {
	struct ieee80211_radiotap_header hdr;
	__le16 tx_flags;
	u8 data_retries;
} __attribute__ ((packed));

/* common interface routines */

static int header_parse_80211(const struct sk_buff *skb, unsigned char *haddr)
{
	memcpy(haddr, skb_mac_header(skb) + 10, ETH_ALEN); /* addr2 */
	return ETH_ALEN;
}

/* must be called under mdev tx lock */
static void ieee80211_configure_filter(struct ieee80211_local *local)
{
	unsigned int changed_flags;
	unsigned int new_flags = 0;

	if (atomic_read(&local->iff_promiscs))
		new_flags |= FIF_PROMISC_IN_BSS;

	if (atomic_read(&local->iff_allmultis))
		new_flags |= FIF_ALLMULTI;

	if (local->monitors)
		new_flags |= FIF_BCN_PRBRESP_PROMISC;

#include <linux/rtnetlink.h>
#include <linux/bitmap.h>
#include <linux/inetdevice.h>
#include <net/net_namespace.h>
#include <net/cfg80211.h>
#include <net/addrconf.h>

#include "ieee80211_i.h"
#include "driver-ops.h"
#include "rate.h"
#include "mesh.h"
#include "wep.h"
#include "led.h"
#include "debugfs.h"

void ieee80211_configure_filter(struct ieee80211_local *local)
{
	u64 mc;
	unsigned int changed_flags;
	unsigned int new_flags = 0;

	if (atomic_read(&local->iff_allmultis))
		new_flags |= FIF_ALLMULTI;

	if (local->monitors || test_bit(SCAN_SW_SCANNING, &local->scanning) ||
	    test_bit(SCAN_ONCHANNEL_SCANNING, &local->scanning))
		new_flags |= FIF_BCN_PRBRESP_PROMISC;

	if (local->fif_probe_req || local->probe_req_reg)
		new_flags |= FIF_PROBE_REQ;

	if (local->fif_fcsfail)
		new_flags |= FIF_FCSFAIL;

	if (local->fif_plcpfail)
		new_flags |= FIF_PLCPFAIL;

	if (local->fif_control)
		new_flags |= FIF_CONTROL;

	if (local->fif_other_bss)
		new_flags |= FIF_OTHER_BSS;

	changed_flags = local->filter_flags ^ new_flags;

	/* be a bit nasty */
	new_flags |= (1<<31);

	local->ops->configure_filter(local_to_hw(local),
				     changed_flags, &new_flags,
				     local->mdev->mc_count,
				     local->mdev->mc_list);
	if (local->fif_pspoll)
		new_flags |= FIF_PSPOLL;

	spin_lock_bh(&local->filter_lock);
	changed_flags = local->filter_flags ^ new_flags;

	mc = drv_prepare_multicast(local, &local->mc_list);
	spin_unlock_bh(&local->filter_lock);

	/* be a bit nasty */
	new_flags |= (1<<31);

	drv_configure_filter(local, changed_flags, &new_flags, mc);

	WARN_ON(new_flags & (1<<31));

	local->filter_flags = new_flags & ~(1<<31);
}

/* master interface */

static int ieee80211_master_open(struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata;
	int res = -EOPNOTSUPP;

	/* we hold the RTNL here so can safely walk the list */
	list_for_each_entry(sdata, &local->interfaces, list) {
		if (netif_running(sdata->dev)) {
			res = 0;
			break;
		}
	}

	if (res)
		return res;

	netif_tx_start_all_queues(local->mdev);

	return 0;
}

static int ieee80211_master_stop(struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata;

	/* we hold the RTNL here so can safely walk the list */
	list_for_each_entry(sdata, &local->interfaces, list)
		if (netif_running(sdata->dev))
			dev_close(sdata->dev);

	return 0;
}

static void ieee80211_master_set_multicast_list(struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
static void ieee80211_reconfig_filter(struct work_struct *work)
{
	struct ieee80211_local *local =
		container_of(work, struct ieee80211_local, reconfig_filter);

	ieee80211_configure_filter(local);
}

/* regular interfaces */

static int ieee80211_change_mtu(struct net_device *dev, int new_mtu)
{
	int meshhdrlen;
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	meshhdrlen = (sdata->vif.type == IEEE80211_IF_TYPE_MESH_POINT) ? 5 : 0;

	/* FIX: what would be proper limits for MTU?
	 * This interface uses 802.3 frames. */
	if (new_mtu < 256 ||
	    new_mtu > IEEE80211_MAX_DATA_LEN - 24 - 6 - meshhdrlen) {
		return -EINVAL;
	}

#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
	printk(KERN_DEBUG "%s: setting MTU %d\n", dev->name, new_mtu);
#endif /* CONFIG_MAC80211_VERBOSE_DEBUG */
	dev->mtu = new_mtu;
	return 0;
}

static inline int identical_mac_addr_allowed(int type1, int type2)
{
	return (type1 == IEEE80211_IF_TYPE_MNTR ||
		type2 == IEEE80211_IF_TYPE_MNTR ||
		(type1 == IEEE80211_IF_TYPE_AP &&
		 type2 == IEEE80211_IF_TYPE_WDS) ||
		(type1 == IEEE80211_IF_TYPE_WDS &&
		 (type2 == IEEE80211_IF_TYPE_WDS ||
		  type2 == IEEE80211_IF_TYPE_AP)) ||
		(type1 == IEEE80211_IF_TYPE_AP &&
		 type2 == IEEE80211_IF_TYPE_VLAN) ||
		(type1 == IEEE80211_IF_TYPE_VLAN &&
		 (type2 == IEEE80211_IF_TYPE_AP ||
		  type2 == IEEE80211_IF_TYPE_VLAN)));
}

static int ieee80211_open(struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata, *nsdata;
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct sta_info *sta;
	struct ieee80211_if_init_conf conf;
	u32 changed = 0;
	int res;
	bool need_hw_reconfig = 0;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	/* we hold the RTNL here so can safely walk the list */
	list_for_each_entry(nsdata, &local->interfaces, list) {
		struct net_device *ndev = nsdata->dev;

		if (ndev != dev && netif_running(ndev)) {
			/*
			 * Allow only a single IBSS interface to be up at any
			 * time. This is restricted because beacon distribution
			 * cannot work properly if both are in the same IBSS.
			 *
			 * To remove this restriction we'd have to disallow them
			 * from setting the same SSID on different IBSS interfaces
			 * belonging to the same hardware. Then, however, we're
			 * faced with having to adopt two different TSF timers...
			 */
			if (sdata->vif.type == IEEE80211_IF_TYPE_IBSS &&
			    nsdata->vif.type == IEEE80211_IF_TYPE_IBSS)
				return -EBUSY;

			/*
			 * The remaining checks are only performed for interfaces
			 * with the same MAC address.
			 */
			if (compare_ether_addr(dev->dev_addr, ndev->dev_addr))
				continue;

			/*
			 * check whether it may have the same address
			 */
			if (!identical_mac_addr_allowed(sdata->vif.type,
							nsdata->vif.type))
				return -ENOTUNIQ;

			/*
			 * can only add VLANs to enabled APs
			 */
			if (sdata->vif.type == IEEE80211_IF_TYPE_VLAN &&
			    nsdata->vif.type == IEEE80211_IF_TYPE_AP)
				sdata->bss = &nsdata->u.ap;
		}
	}

	switch (sdata->vif.type) {
	case IEEE80211_IF_TYPE_WDS:
		if (!is_valid_ether_addr(sdata->u.wds.remote_addr))
			return -ENOLINK;
		break;
	case IEEE80211_IF_TYPE_VLAN:
		if (!sdata->bss)
			return -ENOLINK;
		list_add(&sdata->u.vlan.list, &sdata->bss->vlans);
		break;
	case IEEE80211_IF_TYPE_AP:
		sdata->bss = &sdata->u.ap;
		break;
	case IEEE80211_IF_TYPE_MESH_POINT:
		/* mesh ifaces must set allmulti to forward mcast traffic */
		atomic_inc(&local->iff_allmultis);
		break;
	case IEEE80211_IF_TYPE_STA:
	case IEEE80211_IF_TYPE_MNTR:
	case IEEE80211_IF_TYPE_IBSS:
		/* no special treatment */
		break;
	case IEEE80211_IF_TYPE_INVALID:
		/* cannot happen */
		WARN_ON(1);
		break;
	}

	if (local->open_count == 0) {
		res = 0;
		if (local->ops->start)
			res = local->ops->start(local_to_hw(local));
		if (res)
			goto err_del_bss;
		need_hw_reconfig = 1;
		ieee80211_led_radio(local, local->hw.conf.radio_enabled);
	}

	switch (sdata->vif.type) {
	case IEEE80211_IF_TYPE_VLAN:
		/* no need to tell driver */
		break;
	case IEEE80211_IF_TYPE_MNTR:
		if (sdata->u.mntr_flags & MONITOR_FLAG_COOK_FRAMES) {
			local->cooked_mntrs++;
			break;
		}

		/* must be before the call to ieee80211_configure_filter */
		local->monitors++;
		if (local->monitors == 1)
			local->hw.conf.flags |= IEEE80211_CONF_RADIOTAP;

		if (sdata->u.mntr_flags & MONITOR_FLAG_FCSFAIL)
			local->fif_fcsfail++;
		if (sdata->u.mntr_flags & MONITOR_FLAG_PLCPFAIL)
			local->fif_plcpfail++;
		if (sdata->u.mntr_flags & MONITOR_FLAG_CONTROL)
			local->fif_control++;
		if (sdata->u.mntr_flags & MONITOR_FLAG_OTHER_BSS)
			local->fif_other_bss++;

		netif_addr_lock_bh(local->mdev);
		ieee80211_configure_filter(local);
		netif_addr_unlock_bh(local->mdev);
		break;
	case IEEE80211_IF_TYPE_STA:
	case IEEE80211_IF_TYPE_IBSS:
		sdata->u.sta.flags &= ~IEEE80211_STA_PREV_BSSID_SET;
		/* fall through */
	default:
		conf.vif = &sdata->vif;
		conf.type = sdata->vif.type;
		conf.mac_addr = dev->dev_addr;
		res = local->ops->add_interface(local_to_hw(local), &conf);
		if (res)
			goto err_stop;

		if (ieee80211_vif_is_mesh(&sdata->vif))
			ieee80211_start_mesh(sdata->dev);
		changed |= ieee80211_reset_erp_info(dev);
		ieee80211_bss_info_change_notify(sdata, changed);
		ieee80211_enable_keys(sdata);

		if (sdata->vif.type == IEEE80211_IF_TYPE_STA &&
		    !(sdata->flags & IEEE80211_SDATA_USERSPACE_MLME))
			netif_carrier_off(dev);
		else
			netif_carrier_on(dev);
	}

	if (sdata->vif.type == IEEE80211_IF_TYPE_WDS) {
		/* Create STA entry for the WDS peer */
		sta = sta_info_alloc(sdata, sdata->u.wds.remote_addr,
				     GFP_KERNEL);
		if (!sta) {
			res = -ENOMEM;
			goto err_del_interface;
		}

		/* no locking required since STA is not live yet */
		sta->flags |= WLAN_STA_AUTHORIZED;

		res = sta_info_insert(sta);
		if (res) {
			/* STA has been freed */
			goto err_del_interface;
		}
	}

	if (local->open_count == 0) {
		res = dev_open(local->mdev);
		WARN_ON(res);
		if (res)
			goto err_del_interface;
		tasklet_enable(&local->tx_pending_tasklet);
		tasklet_enable(&local->tasklet);
	}

	/*
	 * set_multicast_list will be invoked by the networking core
	 * which will check whether any increments here were done in
	 * error and sync them down to the hardware as filter flags.
	 */
	if (sdata->flags & IEEE80211_SDATA_ALLMULTI)
		atomic_inc(&local->iff_allmultis);

	if (sdata->flags & IEEE80211_SDATA_PROMISC)
		atomic_inc(&local->iff_promiscs);

	local->open_count++;
	if (need_hw_reconfig)
		ieee80211_hw_config(local);

	/*
	 * ieee80211_sta_work is disabled while network interface
	 * is down. Therefore, some configuration changes may not
	 * yet be effective. Trigger execution of ieee80211_sta_work
	 * to fix this.
	 */
	if (sdata->vif.type == IEEE80211_IF_TYPE_STA ||
	    sdata->vif.type == IEEE80211_IF_TYPE_IBSS) {
		struct ieee80211_if_sta *ifsta = &sdata->u.sta;
		queue_work(local->hw.workqueue, &ifsta->work);
	}

	netif_tx_start_all_queues(dev);

	return 0;
 err_del_interface:
	local->ops->remove_interface(local_to_hw(local), &conf);
 err_stop:
	if (!local->open_count && local->ops->stop)
		local->ops->stop(local_to_hw(local));
 err_del_bss:
	sdata->bss = NULL;
	if (sdata->vif.type == IEEE80211_IF_TYPE_VLAN)
		list_del(&sdata->u.vlan.list);
	return res;
}

static int ieee80211_stop(struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_init_conf conf;
	struct sta_info *sta;

	/*
	 * Stop TX on this interface first.
	 */
	netif_tx_stop_all_queues(dev);

	/*
	 * Now delete all active aggregation sessions.
	 */
	rcu_read_lock();

	list_for_each_entry_rcu(sta, &local->sta_list, list) {
		if (sta->sdata == sdata)
			ieee80211_sta_tear_down_BA_sessions(dev, sta->addr);
	}

	rcu_read_unlock();

	/*
	 * Remove all stations associated with this interface.
	 *
	 * This must be done before calling ops->remove_interface()
	 * because otherwise we can later invoke ops->sta_notify()
	 * whenever the STAs are removed, and that invalidates driver
	 * assumptions about always getting a vif pointer that is valid
	 * (because if we remove a STA after ops->remove_interface()
	 * the driver will have removed the vif info already!)
	 *
	 * We could relax this and only unlink the stations from the
	 * hash table and list but keep them on a per-sdata list that
	 * will be inserted back again when the interface is brought
	 * up again, but I don't currently see a use case for that,
	 * except with WDS which gets a STA entry created when it is
	 * brought up.
	 */
	sta_info_flush(local, sdata);

	/*
	 * Don't count this interface for promisc/allmulti while it
	 * is down. dev_mc_unsync() will invoke set_multicast_list
	 * on the master interface which will sync these down to the
	 * hardware as filter flags.
	 */
	if (sdata->flags & IEEE80211_SDATA_ALLMULTI)
		atomic_dec(&local->iff_allmultis);

	if (sdata->flags & IEEE80211_SDATA_PROMISC)
		atomic_dec(&local->iff_promiscs);

	dev_mc_unsync(local->mdev, dev);

	/* APs need special treatment */
	if (sdata->vif.type == IEEE80211_IF_TYPE_AP) {
		struct ieee80211_sub_if_data *vlan, *tmp;
		struct beacon_data *old_beacon = sdata->u.ap.beacon;

		/* remove beacon */
		rcu_assign_pointer(sdata->u.ap.beacon, NULL);
		synchronize_rcu();
		kfree(old_beacon);

		/* down all dependent devices, that is VLANs */
		list_for_each_entry_safe(vlan, tmp, &sdata->u.ap.vlans,
					 u.vlan.list)
			dev_close(vlan->dev);
		WARN_ON(!list_empty(&sdata->u.ap.vlans));
	}

	local->open_count--;

	switch (sdata->vif.type) {
	case IEEE80211_IF_TYPE_VLAN:
		list_del(&sdata->u.vlan.list);
		/* no need to tell driver */
		break;
	case IEEE80211_IF_TYPE_MNTR:
		if (sdata->u.mntr_flags & MONITOR_FLAG_COOK_FRAMES) {
			local->cooked_mntrs--;
			break;
		}

		local->monitors--;
		if (local->monitors == 0)
			local->hw.conf.flags &= ~IEEE80211_CONF_RADIOTAP;

		if (sdata->u.mntr_flags & MONITOR_FLAG_FCSFAIL)
			local->fif_fcsfail--;
		if (sdata->u.mntr_flags & MONITOR_FLAG_PLCPFAIL)
			local->fif_plcpfail--;
		if (sdata->u.mntr_flags & MONITOR_FLAG_CONTROL)
			local->fif_control--;
		if (sdata->u.mntr_flags & MONITOR_FLAG_OTHER_BSS)
			local->fif_other_bss--;

		netif_addr_lock_bh(local->mdev);
		ieee80211_configure_filter(local);
		netif_addr_unlock_bh(local->mdev);
		break;
	case IEEE80211_IF_TYPE_MESH_POINT:
		/* allmulti is always set on mesh ifaces */
		atomic_dec(&local->iff_allmultis);
		/* fall through */
	case IEEE80211_IF_TYPE_STA:
	case IEEE80211_IF_TYPE_IBSS:
		sdata->u.sta.state = IEEE80211_DISABLED;
		memset(sdata->u.sta.bssid, 0, ETH_ALEN);
		del_timer_sync(&sdata->u.sta.timer);
		/*
		 * When we get here, the interface is marked down.
		 * Call synchronize_rcu() to wait for the RX path
		 * should it be using the interface and enqueuing
		 * frames at this very time on another CPU.
		 */
		synchronize_rcu();
		skb_queue_purge(&sdata->u.sta.skb_queue);

		if (local->scan_dev == sdata->dev) {
			if (!local->ops->hw_scan) {
				local->sta_sw_scanning = 0;
				cancel_delayed_work(&local->scan_work);
			} else
				local->sta_hw_scanning = 0;
		}

		sdata->u.sta.flags &= ~IEEE80211_STA_PRIVACY_INVOKED;
		kfree(sdata->u.sta.extra_ie);
		sdata->u.sta.extra_ie = NULL;
		sdata->u.sta.extra_ie_len = 0;
		/* fall through */
	default:
		conf.vif = &sdata->vif;
		conf.type = sdata->vif.type;
		conf.mac_addr = dev->dev_addr;
		/* disable all keys for as long as this netdev is down */
		ieee80211_disable_keys(sdata);
		local->ops->remove_interface(local_to_hw(local), &conf);
	}

	sdata->bss = NULL;

	if (local->open_count == 0) {
		if (netif_running(local->mdev))
			dev_close(local->mdev);

		if (local->ops->stop)
			local->ops->stop(local_to_hw(local));

		ieee80211_led_radio(local, 0);

		flush_workqueue(local->hw.workqueue);

		tasklet_disable(&local->tx_pending_tasklet);
		tasklet_disable(&local->tasklet);
	}

	return 0;
}

int ieee80211_start_tx_ba_session(struct ieee80211_hw *hw, u8 *ra, u16 tid)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct sta_info *sta;
	struct ieee80211_sub_if_data *sdata;
	u16 start_seq_num = 0;
	u8 *state;
	int ret;
	DECLARE_MAC_BUF(mac);

	if (tid >= STA_TID_NUM)
		return -EINVAL;

#ifdef CONFIG_MAC80211_HT_DEBUG
	printk(KERN_DEBUG "Open BA session requested for %s tid %u\n",
				print_mac(mac, ra), tid);
#endif /* CONFIG_MAC80211_HT_DEBUG */

	rcu_read_lock();

	sta = sta_info_get(local, ra);
	if (!sta) {
#ifdef CONFIG_MAC80211_HT_DEBUG
		printk(KERN_DEBUG "Could not find the station\n");
#endif
		ret = -ENOENT;
		goto exit;
	}

	spin_lock_bh(&sta->lock);

	/* we have tried too many times, receiver does not want A-MPDU */
	if (sta->ampdu_mlme.addba_req_num[tid] > HT_AGG_MAX_RETRIES) {
		ret = -EBUSY;
		goto err_unlock_sta;
	}

	state = &sta->ampdu_mlme.tid_state_tx[tid];
	/* check if the TID is not in aggregation flow already */
	if (*state != HT_AGG_STATE_IDLE) {
#ifdef CONFIG_MAC80211_HT_DEBUG
		printk(KERN_DEBUG "BA request denied - session is not "
				 "idle on tid %u\n", tid);
#endif /* CONFIG_MAC80211_HT_DEBUG */
		ret = -EAGAIN;
		goto err_unlock_sta;
	}

	/* prepare A-MPDU MLME for Tx aggregation */
	sta->ampdu_mlme.tid_tx[tid] =
			kmalloc(sizeof(struct tid_ampdu_tx), GFP_ATOMIC);
	if (!sta->ampdu_mlme.tid_tx[tid]) {
#ifdef CONFIG_MAC80211_HT_DEBUG
		if (net_ratelimit())
			printk(KERN_ERR "allocate tx mlme to tid %d failed\n",
					tid);
#endif
		ret = -ENOMEM;
		goto err_unlock_sta;
	}
	/* Tx timer */
	sta->ampdu_mlme.tid_tx[tid]->addba_resp_timer.function =
			sta_addba_resp_timer_expired;
	sta->ampdu_mlme.tid_tx[tid]->addba_resp_timer.data =
			(unsigned long)&sta->timer_to_tid[tid];
	init_timer(&sta->ampdu_mlme.tid_tx[tid]->addba_resp_timer);

	/* create a new queue for this aggregation */
	ret = ieee80211_ht_agg_queue_add(local, sta, tid);

	/* case no queue is available to aggregation
	 * don't switch to aggregation */
	if (ret) {
#ifdef CONFIG_MAC80211_HT_DEBUG
		printk(KERN_DEBUG "BA request denied - queue unavailable for"
					" tid %d\n", tid);
#endif /* CONFIG_MAC80211_HT_DEBUG */
		goto err_unlock_queue;
	}
	sdata = sta->sdata;

	/* Ok, the Addba frame hasn't been sent yet, but if the driver calls the
	 * call back right away, it must see that the flow has begun */
	*state |= HT_ADDBA_REQUESTED_MSK;

	if (local->ops->ampdu_action)
		ret = local->ops->ampdu_action(hw, IEEE80211_AMPDU_TX_START,
						ra, tid, &start_seq_num);

	if (ret) {
		/* No need to requeue the packets in the agg queue, since we
		 * held the tx lock: no packet could be enqueued to the newly
		 * allocated queue */
		ieee80211_ht_agg_queue_remove(local, sta, tid, 0);
#ifdef CONFIG_MAC80211_HT_DEBUG
		printk(KERN_DEBUG "BA request denied - HW unavailable for"
					" tid %d\n", tid);
#endif /* CONFIG_MAC80211_HT_DEBUG */
		*state = HT_AGG_STATE_IDLE;
		goto err_unlock_queue;
	}

	/* Will put all the packets in the new SW queue */
	ieee80211_requeue(local, ieee802_1d_to_ac[tid]);
	spin_unlock_bh(&sta->lock);

	/* send an addBA request */
	sta->ampdu_mlme.dialog_token_allocator++;
	sta->ampdu_mlme.tid_tx[tid]->dialog_token =
			sta->ampdu_mlme.dialog_token_allocator;
	sta->ampdu_mlme.tid_tx[tid]->ssn = start_seq_num;


	ieee80211_send_addba_request(sta->sdata->dev, ra, tid,
			 sta->ampdu_mlme.tid_tx[tid]->dialog_token,
			 sta->ampdu_mlme.tid_tx[tid]->ssn,
			 0x40, 5000);
	/* activate the timer for the recipient's addBA response */
	sta->ampdu_mlme.tid_tx[tid]->addba_resp_timer.expires =
				jiffies + ADDBA_RESP_INTERVAL;
	add_timer(&sta->ampdu_mlme.tid_tx[tid]->addba_resp_timer);
#ifdef CONFIG_MAC80211_HT_DEBUG
	printk(KERN_DEBUG "activated addBA response timer on tid %d\n", tid);
#endif
	goto exit;

err_unlock_queue:
	kfree(sta->ampdu_mlme.tid_tx[tid]);
	sta->ampdu_mlme.tid_tx[tid] = NULL;
	ret = -EBUSY;
err_unlock_sta:
	spin_unlock_bh(&sta->lock);
exit:
	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL(ieee80211_start_tx_ba_session);

int ieee80211_stop_tx_ba_session(struct ieee80211_hw *hw,
				 u8 *ra, u16 tid,
				 enum ieee80211_back_parties initiator)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct sta_info *sta;
	u8 *state;
	int ret = 0;
	DECLARE_MAC_BUF(mac);

	if (tid >= STA_TID_NUM)
		return -EINVAL;

	rcu_read_lock();
	sta = sta_info_get(local, ra);
	if (!sta) {
		rcu_read_unlock();
		return -ENOENT;
	}

	/* check if the TID is in aggregation */
	state = &sta->ampdu_mlme.tid_state_tx[tid];
	spin_lock_bh(&sta->lock);

	if (*state != HT_AGG_STATE_OPERATIONAL) {
		ret = -ENOENT;
		goto stop_BA_exit;
	}

#ifdef CONFIG_MAC80211_HT_DEBUG
	printk(KERN_DEBUG "Tx BA session stop requested for %s tid %u\n",
				print_mac(mac, ra), tid);
#endif /* CONFIG_MAC80211_HT_DEBUG */

	ieee80211_stop_queue(hw, sta->tid_to_tx_q[tid]);

	*state = HT_AGG_STATE_REQ_STOP_BA_MSK |
		(initiator << HT_AGG_STATE_INITIATOR_SHIFT);

	if (local->ops->ampdu_action)
		ret = local->ops->ampdu_action(hw, IEEE80211_AMPDU_TX_STOP,
						ra, tid, NULL);

	/* case HW denied going back to legacy */
	if (ret) {
		WARN_ON(ret != -EBUSY);
		*state = HT_AGG_STATE_OPERATIONAL;
		ieee80211_wake_queue(hw, sta->tid_to_tx_q[tid]);
		goto stop_BA_exit;
	}

stop_BA_exit:
	spin_unlock_bh(&sta->lock);
	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL(ieee80211_stop_tx_ba_session);

void ieee80211_start_tx_ba_cb(struct ieee80211_hw *hw, u8 *ra, u16 tid)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct sta_info *sta;
	u8 *state;
	DECLARE_MAC_BUF(mac);

	if (tid >= STA_TID_NUM) {
#ifdef CONFIG_MAC80211_HT_DEBUG
		printk(KERN_DEBUG "Bad TID value: tid = %d (>= %d)\n",
				tid, STA_TID_NUM);
#endif
		return;
	}

	rcu_read_lock();
	sta = sta_info_get(local, ra);
	if (!sta) {
		rcu_read_unlock();
#ifdef CONFIG_MAC80211_HT_DEBUG
		printk(KERN_DEBUG "Could not find station: %s\n",
				print_mac(mac, ra));
#endif
		return;
	}

	state = &sta->ampdu_mlme.tid_state_tx[tid];
	spin_lock_bh(&sta->lock);

	if (!(*state & HT_ADDBA_REQUESTED_MSK)) {
#ifdef CONFIG_MAC80211_HT_DEBUG
		printk(KERN_DEBUG "addBA was not requested yet, state is %d\n",
				*state);
#endif
		spin_unlock_bh(&sta->lock);
		rcu_read_unlock();
		return;
	}

	WARN_ON_ONCE(*state & HT_ADDBA_DRV_READY_MSK);

	*state |= HT_ADDBA_DRV_READY_MSK;

	if (*state == HT_AGG_STATE_OPERATIONAL) {
#ifdef CONFIG_MAC80211_HT_DEBUG
		printk(KERN_DEBUG "Aggregation is on for tid %d \n", tid);
#endif
		ieee80211_wake_queue(hw, sta->tid_to_tx_q[tid]);
	}
	spin_unlock_bh(&sta->lock);
	rcu_read_unlock();
}
EXPORT_SYMBOL(ieee80211_start_tx_ba_cb);

void ieee80211_stop_tx_ba_cb(struct ieee80211_hw *hw, u8 *ra, u8 tid)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct sta_info *sta;
	u8 *state;
	int agg_queue;
	DECLARE_MAC_BUF(mac);

	if (tid >= STA_TID_NUM) {
#ifdef CONFIG_MAC80211_HT_DEBUG
		printk(KERN_DEBUG "Bad TID value: tid = %d (>= %d)\n",
				tid, STA_TID_NUM);
#endif
		return;
	}

#ifdef CONFIG_MAC80211_HT_DEBUG
	printk(KERN_DEBUG "Stopping Tx BA session for %s tid %d\n",
				print_mac(mac, ra), tid);
#endif /* CONFIG_MAC80211_HT_DEBUG */

	rcu_read_lock();
	sta = sta_info_get(local, ra);
	if (!sta) {
#ifdef CONFIG_MAC80211_HT_DEBUG
		printk(KERN_DEBUG "Could not find station: %s\n",
				print_mac(mac, ra));
#endif
		rcu_read_unlock();
		return;
	}
	state = &sta->ampdu_mlme.tid_state_tx[tid];

	/* NOTE: no need to use sta->lock in this state check, as
	 * ieee80211_stop_tx_ba_session will let only one stop call to
	 * pass through per sta/tid
	 */
	if ((*state & HT_AGG_STATE_REQ_STOP_BA_MSK) == 0) {
#ifdef CONFIG_MAC80211_HT_DEBUG
		printk(KERN_DEBUG "unexpected callback to A-MPDU stop\n");
#endif
		rcu_read_unlock();
		return;
	}

	if (*state & HT_AGG_STATE_INITIATOR_MSK)
		ieee80211_send_delba(sta->sdata->dev, ra, tid,
			WLAN_BACK_INITIATOR, WLAN_REASON_QSTA_NOT_USE);

	agg_queue = sta->tid_to_tx_q[tid];

	ieee80211_ht_agg_queue_remove(local, sta, tid, 1);

	/* We just requeued the all the frames that were in the
	 * removed queue, and since we might miss a softirq we do
	 * netif_schedule_queue.  ieee80211_wake_queue is not used
	 * here as this queue is not necessarily stopped
	 */
	netif_schedule_queue(netdev_get_tx_queue(local->mdev, agg_queue));
	spin_lock_bh(&sta->lock);
	*state = HT_AGG_STATE_IDLE;
	sta->ampdu_mlme.addba_req_num[tid] = 0;
	kfree(sta->ampdu_mlme.tid_tx[tid]);
	sta->ampdu_mlme.tid_tx[tid] = NULL;
	spin_unlock_bh(&sta->lock);

	rcu_read_unlock();
}
EXPORT_SYMBOL(ieee80211_stop_tx_ba_cb);

void ieee80211_start_tx_ba_cb_irqsafe(struct ieee80211_hw *hw,
				      const u8 *ra, u16 tid)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_ra_tid *ra_tid;
	struct sk_buff *skb = dev_alloc_skb(0);

	if (unlikely(!skb)) {
#ifdef CONFIG_MAC80211_HT_DEBUG
		if (net_ratelimit())
			printk(KERN_WARNING "%s: Not enough memory, "
			       "dropping start BA session", skb->dev->name);
#endif
		return;
	}
	ra_tid = (struct ieee80211_ra_tid *) &skb->cb;
	memcpy(&ra_tid->ra, ra, ETH_ALEN);
	ra_tid->tid = tid;

	skb->pkt_type = IEEE80211_ADDBA_MSG;
	skb_queue_tail(&local->skb_queue, skb);
	tasklet_schedule(&local->tasklet);
}
EXPORT_SYMBOL(ieee80211_start_tx_ba_cb_irqsafe);

void ieee80211_stop_tx_ba_cb_irqsafe(struct ieee80211_hw *hw,
				     const u8 *ra, u16 tid)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_ra_tid *ra_tid;
	struct sk_buff *skb = dev_alloc_skb(0);

	if (unlikely(!skb)) {
#ifdef CONFIG_MAC80211_HT_DEBUG
		if (net_ratelimit())
			printk(KERN_WARNING "%s: Not enough memory, "
			       "dropping stop BA session", skb->dev->name);
#endif
		return;
	}
	ra_tid = (struct ieee80211_ra_tid *) &skb->cb;
	memcpy(&ra_tid->ra, ra, ETH_ALEN);
	ra_tid->tid = tid;

	skb->pkt_type = IEEE80211_DELBA_MSG;
	skb_queue_tail(&local->skb_queue, skb);
	tasklet_schedule(&local->tasklet);
}
EXPORT_SYMBOL(ieee80211_stop_tx_ba_cb_irqsafe);

static void ieee80211_set_multicast_list(struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	int allmulti, promisc, sdata_allmulti, sdata_promisc;

	allmulti = !!(dev->flags & IFF_ALLMULTI);
	promisc = !!(dev->flags & IFF_PROMISC);
	sdata_allmulti = !!(sdata->flags & IEEE80211_SDATA_ALLMULTI);
	sdata_promisc = !!(sdata->flags & IEEE80211_SDATA_PROMISC);

	if (allmulti != sdata_allmulti) {
		if (dev->flags & IFF_ALLMULTI)
			atomic_inc(&local->iff_allmultis);
		else
			atomic_dec(&local->iff_allmultis);
		sdata->flags ^= IEEE80211_SDATA_ALLMULTI;
	}

	if (promisc != sdata_promisc) {
		if (dev->flags & IFF_PROMISC)
			atomic_inc(&local->iff_promiscs);
		else
			atomic_dec(&local->iff_promiscs);
		sdata->flags ^= IEEE80211_SDATA_PROMISC;
	}

	dev_mc_sync(local->mdev, dev);
}

static const struct header_ops ieee80211_header_ops = {
	.create		= eth_header,
	.parse		= header_parse_80211,
	.rebuild	= eth_rebuild_header,
	.cache		= eth_header_cache,
	.cache_update	= eth_header_cache_update,
};

void ieee80211_if_setup(struct net_device *dev)
{
	ether_setup(dev);
	dev->hard_start_xmit = ieee80211_subif_start_xmit;
	dev->wireless_handlers = &ieee80211_iw_handler_def;
	dev->set_multicast_list = ieee80211_set_multicast_list;
	dev->change_mtu = ieee80211_change_mtu;
	dev->open = ieee80211_open;
	dev->stop = ieee80211_stop;
	dev->destructor = free_netdev;
}

/* everything else */

int ieee80211_if_config(struct ieee80211_sub_if_data *sdata, u32 changed)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_conf conf;

	if (WARN_ON(!netif_running(sdata->dev)))
		return 0;

	if (!local->ops->config_interface)
		return 0;

	memset(&conf, 0, sizeof(conf));
	conf.changed = changed;

	if (sdata->vif.type == IEEE80211_IF_TYPE_STA ||
	    sdata->vif.type == IEEE80211_IF_TYPE_IBSS) {
		conf.bssid = sdata->u.sta.bssid;
		conf.ssid = sdata->u.sta.ssid;
		conf.ssid_len = sdata->u.sta.ssid_len;
	} else if (sdata->vif.type == IEEE80211_IF_TYPE_AP) {
		conf.bssid = sdata->dev->dev_addr;
		conf.ssid = sdata->u.ap.ssid;
		conf.ssid_len = sdata->u.ap.ssid_len;
	} else if (ieee80211_vif_is_mesh(&sdata->vif)) {
		u8 zero[ETH_ALEN] = { 0 };
		conf.bssid = zero;
		conf.ssid = zero;
		conf.ssid_len = 0;
	} else {
		WARN_ON(1);
		return -EINVAL;
	}

	if (WARN_ON(!conf.bssid && (changed & IEEE80211_IFCC_BSSID)))
		return -EINVAL;

	if (WARN_ON(!conf.ssid && (changed & IEEE80211_IFCC_SSID)))
		return -EINVAL;

	return local->ops->config_interface(local_to_hw(local),
					    &sdata->vif, &conf);
}

int ieee80211_hw_config(struct ieee80211_local *local)
{
	struct ieee80211_channel *chan;
	int ret = 0;

	if (local->sta_sw_scanning)
		chan = local->scan_channel;
	else
		chan = local->oper_channel;

	local->hw.conf.channel = chan;

	if (!local->hw.conf.power_level)
		local->hw.conf.power_level = chan->max_power;
	else
		local->hw.conf.power_level = min(chan->max_power,
					       local->hw.conf.power_level);

	local->hw.conf.max_antenna_gain = chan->max_antenna_gain;

#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
	printk(KERN_DEBUG "%s: HW CONFIG: freq=%d\n",
	       wiphy_name(local->hw.wiphy), chan->center_freq);
#endif

	if (local->open_count)
		ret = local->ops->config(local_to_hw(local), &local->hw.conf);

	return ret;
}

/**
 * ieee80211_handle_ht should be used only after legacy configuration
 * has been determined namely band, as ht configuration depends upon
 * the hardware's HT abilities for a _specific_ band.
 */
u32 ieee80211_handle_ht(struct ieee80211_local *local, int enable_ht,
			   struct ieee80211_ht_info *req_ht_cap,
			   struct ieee80211_ht_bss_info *req_bss_cap)
{
	struct ieee80211_conf *conf = &local->hw.conf;
	struct ieee80211_supported_band *sband;
	struct ieee80211_ht_info ht_conf;
	struct ieee80211_ht_bss_info ht_bss_conf;
	u32 changed = 0;
	int i;
	u8 max_tx_streams = IEEE80211_HT_CAP_MAX_STREAMS;
	u8 tx_mcs_set_cap;

	sband = local->hw.wiphy->bands[conf->channel->band];

	memset(&ht_conf, 0, sizeof(struct ieee80211_ht_info));
	memset(&ht_bss_conf, 0, sizeof(struct ieee80211_ht_bss_info));

	/* HT is not supported */
	if (!sband->ht_info.ht_supported) {
		conf->flags &= ~IEEE80211_CONF_SUPPORT_HT_MODE;
		goto out;
	}

	/* disable HT */
	if (!enable_ht) {
		if (conf->flags & IEEE80211_CONF_SUPPORT_HT_MODE)
			changed |= BSS_CHANGED_HT;
		conf->flags &= ~IEEE80211_CONF_SUPPORT_HT_MODE;
		conf->ht_conf.ht_supported = 0;
		goto out;
	}


	if (!(conf->flags & IEEE80211_CONF_SUPPORT_HT_MODE))
		changed |= BSS_CHANGED_HT;

	conf->flags |= IEEE80211_CONF_SUPPORT_HT_MODE;
	ht_conf.ht_supported = 1;

	ht_conf.cap = req_ht_cap->cap & sband->ht_info.cap;
	ht_conf.cap &= ~(IEEE80211_HT_CAP_MIMO_PS);
	ht_conf.cap |= sband->ht_info.cap & IEEE80211_HT_CAP_MIMO_PS;
	ht_bss_conf.primary_channel = req_bss_cap->primary_channel;
	ht_bss_conf.bss_cap = req_bss_cap->bss_cap;
	ht_bss_conf.bss_op_mode = req_bss_cap->bss_op_mode;

	ht_conf.ampdu_factor = req_ht_cap->ampdu_factor;
	ht_conf.ampdu_density = req_ht_cap->ampdu_density;

	/* Bits 96-100 */
	tx_mcs_set_cap = sband->ht_info.supp_mcs_set[12];

	/* configure suppoerted Tx MCS according to requested MCS
	 * (based in most cases on Rx capabilities of peer) and self
	 * Tx MCS capabilities (as defined by low level driver HW
	 * Tx capabilities) */
	if (!(tx_mcs_set_cap & IEEE80211_HT_CAP_MCS_TX_DEFINED))
		goto check_changed;

	/* Counting from 0 therfore + 1 */
	if (tx_mcs_set_cap & IEEE80211_HT_CAP_MCS_TX_RX_DIFF)
		max_tx_streams = ((tx_mcs_set_cap &
				IEEE80211_HT_CAP_MCS_TX_STREAMS) >> 2) + 1;

	for (i = 0; i < max_tx_streams; i++)
		ht_conf.supp_mcs_set[i] =
			sband->ht_info.supp_mcs_set[i] &
					req_ht_cap->supp_mcs_set[i];

	if (tx_mcs_set_cap & IEEE80211_HT_CAP_MCS_TX_UEQM)
		for (i = IEEE80211_SUPP_MCS_SET_UEQM;
		     i < IEEE80211_SUPP_MCS_SET_LEN; i++)
			ht_conf.supp_mcs_set[i] =
				sband->ht_info.supp_mcs_set[i] &
					req_ht_cap->supp_mcs_set[i];

check_changed:
	/* if bss configuration changed store the new one */
	if (memcmp(&conf->ht_conf, &ht_conf, sizeof(ht_conf)) ||
	    memcmp(&conf->ht_bss_conf, &ht_bss_conf, sizeof(ht_bss_conf))) {
		changed |= BSS_CHANGED_HT;
		memcpy(&conf->ht_conf, &ht_conf, sizeof(ht_conf));
		memcpy(&conf->ht_bss_conf, &ht_bss_conf, sizeof(ht_bss_conf));
	}
out:
	return changed;
}

static u32 ieee80211_hw_conf_chan(struct ieee80211_local *local)
{
	struct ieee80211_sub_if_data *sdata;
	struct cfg80211_chan_def chandef = {};
	u32 changed = 0;
	int power;
	u32 offchannel_flag;

	offchannel_flag = local->hw.conf.flags & IEEE80211_CONF_OFFCHANNEL;

	if (local->scan_chandef.chan) {
		chandef = local->scan_chandef;
	} else if (local->tmp_channel) {
		chandef.chan = local->tmp_channel;
		chandef.width = NL80211_CHAN_WIDTH_20_NOHT;
		chandef.center_freq1 = chandef.chan->center_freq;
	} else
		chandef = local->_oper_chandef;

	WARN(!cfg80211_chandef_valid(&chandef),
	     "control:%d MHz width:%d center: %d/%d MHz",
	     chandef.chan->center_freq, chandef.width,
	     chandef.center_freq1, chandef.center_freq2);

	if (!cfg80211_chandef_identical(&chandef, &local->_oper_chandef))
		local->hw.conf.flags |= IEEE80211_CONF_OFFCHANNEL;
	else
		local->hw.conf.flags &= ~IEEE80211_CONF_OFFCHANNEL;

	offchannel_flag ^= local->hw.conf.flags & IEEE80211_CONF_OFFCHANNEL;

	if (offchannel_flag ||
	    !cfg80211_chandef_identical(&local->hw.conf.chandef,
					&local->_oper_chandef)) {
		local->hw.conf.chandef = chandef;
		changed |= IEEE80211_CONF_CHANGE_CHANNEL;
	}

	if (!conf_is_ht(&local->hw.conf)) {
		/*
		 * mac80211.h documents that this is only valid
		 * when the channel is set to an HT type, and
		 * that otherwise STATIC is used.
		 */
		local->hw.conf.smps_mode = IEEE80211_SMPS_STATIC;
	} else if (local->hw.conf.smps_mode != local->smps_mode) {
		local->hw.conf.smps_mode = local->smps_mode;
		changed |= IEEE80211_CONF_CHANGE_SMPS;
	}

	power = ieee80211_chandef_max_power(&chandef);
	if (local->user_power_level != IEEE80211_UNSET_POWER_LEVEL)
		power = min(local->user_power_level, power);

	rcu_read_lock();
	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		if (!rcu_access_pointer(sdata->vif.chanctx_conf))
			continue;
		if (sdata->vif.type == NL80211_IFTYPE_AP_VLAN)
			continue;
		power = min(power, sdata->vif.bss_conf.txpower);
	}
	rcu_read_unlock();

	if (local->hw.conf.power_level != power) {
		changed |= IEEE80211_CONF_CHANGE_POWER;
		local->hw.conf.power_level = power;
	}

	return changed;
}

int ieee80211_hw_config(struct ieee80211_local *local, u32 changed)
{
	int ret = 0;

	might_sleep();

	if (!local->use_chanctx)
		changed |= ieee80211_hw_conf_chan(local);
	else
		changed &= ~(IEEE80211_CONF_CHANGE_CHANNEL |
			     IEEE80211_CONF_CHANGE_POWER);

	if (changed && local->open_count) {
		ret = drv_config(local, changed);
		/*
		 * Goal:
		 * HW reconfiguration should never fail, the driver has told
		 * us what it can support so it should live up to that promise.
		 *
		 * Current status:
		 * rfkill is not integrated with mac80211 and a
		 * configuration command can thus fail if hardware rfkill
		 * is enabled
		 *
		 * FIXME: integrate rfkill with mac80211 and then add this
		 * WARN_ON() back
		 *
		 */
		/* WARN_ON(ret); */
	}

	return ret;
}

void ieee80211_bss_info_change_notify(struct ieee80211_sub_if_data *sdata,
				      u32 changed)
{
	struct ieee80211_local *local = sdata->local;

	if (!changed)
		return;

	if (local->ops->bss_info_changed)
		local->ops->bss_info_changed(local_to_hw(local),
					     &sdata->vif,
					     &sdata->bss_conf,
					     changed);
}

u32 ieee80211_reset_erp_info(struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	sdata->bss_conf.use_cts_prot = 0;
	sdata->bss_conf.use_short_preamble = 0;
	return BSS_CHANGED_ERP_CTS_PROT | BSS_CHANGED_ERP_PREAMBLE;
}

void ieee80211_tx_status_irqsafe(struct ieee80211_hw *hw,
				 struct sk_buff *skb)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	int tmp;

	skb->dev = local->mdev;
	skb->pkt_type = IEEE80211_TX_STATUS_MSG;
	skb_queue_tail(info->flags & IEEE80211_TX_CTL_REQ_TX_STATUS ?
		       &local->skb_queue : &local->skb_queue_unreliable, skb);
	tmp = skb_queue_len(&local->skb_queue) +
		skb_queue_len(&local->skb_queue_unreliable);
	while (tmp > IEEE80211_IRQSAFE_QUEUE_LIMIT &&
	       (skb = skb_dequeue(&local->skb_queue_unreliable))) {
		dev_kfree_skb_irq(skb);
		tmp--;
		I802_DEBUG_INC(local->tx_status_drop);
	}
	tasklet_schedule(&local->tasklet);
}
EXPORT_SYMBOL(ieee80211_tx_status_irqsafe);

	if (!changed || sdata->vif.type == NL80211_IFTYPE_AP_VLAN)
		return;

	drv_bss_info_changed(local, sdata, &sdata->vif.bss_conf, changed);
}

u32 ieee80211_reset_erp_info(struct ieee80211_sub_if_data *sdata)
{
	sdata->vif.bss_conf.use_cts_prot = false;
	sdata->vif.bss_conf.use_short_preamble = false;
	sdata->vif.bss_conf.use_short_slot = false;
	return BSS_CHANGED_ERP_CTS_PROT |
	       BSS_CHANGED_ERP_PREAMBLE |
	       BSS_CHANGED_ERP_SLOT;
}

static void ieee80211_tasklet_handler(unsigned long data)
{
	struct ieee80211_local *local = (struct ieee80211_local *) data;
	struct sk_buff *skb;
	struct ieee80211_rx_status rx_status;
	struct ieee80211_ra_tid *ra_tid;

	while ((skb = skb_dequeue(&local->skb_queue)) ||
	       (skb = skb_dequeue(&local->skb_queue_unreliable))) {
		switch (skb->pkt_type) {
		case IEEE80211_RX_MSG:
			/* status is in skb->cb */
			memcpy(&rx_status, skb->cb, sizeof(rx_status));
			/* Clear skb->pkt_type in order to not confuse kernel
			 * netstack. */
			skb->pkt_type = 0;
			__ieee80211_rx(local_to_hw(local), skb, &rx_status);
			break;
		case IEEE80211_TX_STATUS_MSG:
			skb->pkt_type = 0;
			ieee80211_tx_status(local_to_hw(local), skb);
			break;
		case IEEE80211_DELBA_MSG:
			ra_tid = (struct ieee80211_ra_tid *) &skb->cb;
			ieee80211_stop_tx_ba_cb(local_to_hw(local),
						ra_tid->ra, ra_tid->tid);
			dev_kfree_skb(skb);
			break;
		case IEEE80211_ADDBA_MSG:
			ra_tid = (struct ieee80211_ra_tid *) &skb->cb;
			ieee80211_start_tx_ba_cb(local_to_hw(local),
						 ra_tid->ra, ra_tid->tid);
			dev_kfree_skb(skb);
			break ;
		default:
			WARN_ON(1);
			/* Clear skb->pkt_type in order to not confuse kernel
			 * netstack. */
			skb->pkt_type = 0;
			ieee80211_rx(&local->hw, skb);
			break;
		case IEEE80211_TX_STATUS_MSG:
			skb->pkt_type = 0;
			ieee80211_tx_status(&local->hw, skb);
			break;
		default:
			WARN(1, "mac80211: Packet is of unknown type %d\n",
			     skb->pkt_type);
			dev_kfree_skb(skb);
			break;
		}
	}
}

/* Remove added headers (e.g., QoS control), encryption header/MIC, etc. to
 * make a prepared TX frame (one that has been given to hw) to look like brand
 * new IEEE 802.11 frame that is ready to go through TX processing again.
 */
static void ieee80211_remove_tx_extra(struct ieee80211_local *local,
				      struct ieee80211_key *key,
				      struct sk_buff *skb)
{
	int hdrlen, iv_len, mic_len;

	hdrlen = ieee80211_get_hdrlen_from_skb(skb);

	if (!key)
		goto no_key;

	switch (key->conf.alg) {
	case ALG_WEP:
		iv_len = WEP_IV_LEN;
		mic_len = WEP_ICV_LEN;
		break;
	case ALG_TKIP:
		iv_len = TKIP_IV_LEN;
		mic_len = TKIP_ICV_LEN;
		break;
	case ALG_CCMP:
		iv_len = CCMP_HDR_LEN;
		mic_len = CCMP_MIC_LEN;
		break;
	default:
		goto no_key;
	}

	if (skb->len >= mic_len &&
	    !(key->flags & KEY_FLAG_UPLOADED_TO_HARDWARE))
		skb_trim(skb, skb->len - mic_len);
	if (skb->len >= iv_len && skb->len > hdrlen) {
		memmove(skb->data + iv_len, skb->data, hdrlen);
		skb_pull(skb, iv_len);
	}

no_key:
	{
		struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
		u16 fc = le16_to_cpu(hdr->frame_control);
		if ((fc & 0x8C) == 0x88) /* QoS Control Field */ {
			fc &= ~IEEE80211_STYPE_QOS_DATA;
			hdr->frame_control = cpu_to_le16(fc);
			memmove(skb->data + 2, skb->data, hdrlen - 2);
			skb_pull(skb, 2);
		}
	}
}

static void ieee80211_handle_filtered_frame(struct ieee80211_local *local,
					    struct sta_info *sta,
					    struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	sta->tx_filtered_count++;

	/*
	 * Clear the TX filter mask for this STA when sending the next
	 * packet. If the STA went to power save mode, this will happen
	 * when it wakes up for the next time.
	 */
	set_sta_flags(sta, WLAN_STA_CLEAR_PS_FILT);

	/*
	 * This code races in the following way:
	 *
	 *  (1) STA sends frame indicating it will go to sleep and does so
	 *  (2) hardware/firmware adds STA to filter list, passes frame up
	 *  (3) hardware/firmware processes TX fifo and suppresses a frame
	 *  (4) we get TX status before having processed the frame and
	 *	knowing that the STA has gone to sleep.
	 *
	 * This is actually quite unlikely even when both those events are
	 * processed from interrupts coming in quickly after one another or
	 * even at the same time because we queue both TX status events and
	 * RX frames to be processed by a tasklet and process them in the
	 * same order that they were received or TX status last. Hence, there
	 * is no race as long as the frame RX is processed before the next TX
	 * status, which drivers can ensure, see below.
	 *
	 * Note that this can only happen if the hardware or firmware can
	 * actually add STAs to the filter list, if this is done by the
	 * driver in response to set_tim() (which will only reduce the race
	 * this whole filtering tries to solve, not completely solve it)
	 * this situation cannot happen.
	 *
	 * To completely solve this race drivers need to make sure that they
	 *  (a) don't mix the irq-safe/not irq-safe TX status/RX processing
	 *	functions and
	 *  (b) always process RX events before TX status events if ordering
	 *      can be unknown, for example with different interrupt status
	 *	bits.
	 */
	if (test_sta_flags(sta, WLAN_STA_PS) &&
	    skb_queue_len(&sta->tx_filtered) < STA_MAX_TX_BUFFER) {
		ieee80211_remove_tx_extra(local, sta->key, skb);
		skb_queue_tail(&sta->tx_filtered, skb);
		return;
	}

	if (!test_sta_flags(sta, WLAN_STA_PS) &&
	    !(info->flags & IEEE80211_TX_CTL_REQUEUE)) {
		/* Software retry the packet once */
		info->flags |= IEEE80211_TX_CTL_REQUEUE;
		ieee80211_remove_tx_extra(local, sta->key, skb);
		dev_queue_xmit(skb);
		return;
	}

#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
	if (net_ratelimit())
		printk(KERN_DEBUG "%s: dropped TX filtered frame, "
		       "queue_len=%d PS=%d @%lu\n",
		       wiphy_name(local->hw.wiphy),
		       skb_queue_len(&sta->tx_filtered),
		       !!test_sta_flags(sta, WLAN_STA_PS), jiffies);
#endif
	dev_kfree_skb(skb);
}

void ieee80211_tx_status(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct sk_buff *skb2;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	u16 frag, type;
	__le16 fc;
	struct ieee80211_tx_status_rtap_hdr *rthdr;
	struct ieee80211_sub_if_data *sdata;
	struct net_device *prev_dev = NULL;
	struct sta_info *sta;

	rcu_read_lock();

	if (info->status.excessive_retries) {
		sta = sta_info_get(local, hdr->addr1);
		if (sta) {
			if (test_sta_flags(sta, WLAN_STA_PS)) {
				/*
				 * The STA is in power save mode, so assume
				 * that this TX packet failed because of that.
				 */
				ieee80211_handle_filtered_frame(local, sta, skb);
				rcu_read_unlock();
				return;
			}
		}
	}

	fc = hdr->frame_control;

	if ((info->flags & IEEE80211_TX_STAT_AMPDU_NO_BACK) &&
	    (ieee80211_is_data_qos(fc))) {
		u16 tid, ssn;
		u8 *qc;
		sta = sta_info_get(local, hdr->addr1);
		if (sta) {
			qc = ieee80211_get_qos_ctl(hdr);
			tid = qc[0] & 0xf;
			ssn = ((le16_to_cpu(hdr->seq_ctrl) + 0x10)
						& IEEE80211_SCTL_SEQ);
			ieee80211_send_bar(sta->sdata->dev, hdr->addr1,
					   tid, ssn);
		}
	}

	if (info->flags & IEEE80211_TX_STAT_TX_FILTERED) {
		sta = sta_info_get(local, hdr->addr1);
		if (sta) {
			ieee80211_handle_filtered_frame(local, sta, skb);
			rcu_read_unlock();
			return;
		}
	} else
		rate_control_tx_status(local->mdev, skb);

	rcu_read_unlock();

	ieee80211_led_tx(local, 0);

	/* SNMP counters
	 * Fragments are passed to low-level drivers as separate skbs, so these
	 * are actually fragments, not frames. Update frame counters only for
	 * the first fragment of the frame. */

	frag = le16_to_cpu(hdr->seq_ctrl) & IEEE80211_SCTL_FRAG;
	type = le16_to_cpu(hdr->frame_control) & IEEE80211_FCTL_FTYPE;

	if (info->flags & IEEE80211_TX_STAT_ACK) {
		if (frag == 0) {
			local->dot11TransmittedFrameCount++;
			if (is_multicast_ether_addr(hdr->addr1))
				local->dot11MulticastTransmittedFrameCount++;
			if (info->status.retry_count > 0)
				local->dot11RetryCount++;
			if (info->status.retry_count > 1)
				local->dot11MultipleRetryCount++;
		}

		/* This counter shall be incremented for an acknowledged MPDU
		 * with an individual address in the address 1 field or an MPDU
		 * with a multicast address in the address 1 field of type Data
		 * or Management. */
		if (!is_multicast_ether_addr(hdr->addr1) ||
		    type == IEEE80211_FTYPE_DATA ||
		    type == IEEE80211_FTYPE_MGMT)
			local->dot11TransmittedFragmentCount++;
	} else {
		if (frag == 0)
			local->dot11FailedCount++;
	}

	/* this was a transmitted frame, but now we want to reuse it */
	skb_orphan(skb);

	/*
	 * This is a bit racy but we can avoid a lot of work
	 * with this test...
	 */
	if (!local->monitors && !local->cooked_mntrs) {
		dev_kfree_skb(skb);
		return;
	}

	/* send frame to monitor interfaces now */

	if (skb_headroom(skb) < sizeof(*rthdr)) {
		printk(KERN_ERR "ieee80211_tx_status: headroom too small\n");
		dev_kfree_skb(skb);
		return;
	}

	rthdr = (struct ieee80211_tx_status_rtap_hdr *)
				skb_push(skb, sizeof(*rthdr));

	memset(rthdr, 0, sizeof(*rthdr));
	rthdr->hdr.it_len = cpu_to_le16(sizeof(*rthdr));
	rthdr->hdr.it_present =
		cpu_to_le32((1 << IEEE80211_RADIOTAP_TX_FLAGS) |
			    (1 << IEEE80211_RADIOTAP_DATA_RETRIES));

	if (!(info->flags & IEEE80211_TX_STAT_ACK) &&
	    !is_multicast_ether_addr(hdr->addr1))
		rthdr->tx_flags |= cpu_to_le16(IEEE80211_RADIOTAP_F_TX_FAIL);

	if ((info->flags & IEEE80211_TX_CTL_USE_RTS_CTS) &&
	    (info->flags & IEEE80211_TX_CTL_USE_CTS_PROTECT))
		rthdr->tx_flags |= cpu_to_le16(IEEE80211_RADIOTAP_F_TX_CTS);
	else if (info->flags & IEEE80211_TX_CTL_USE_RTS_CTS)
		rthdr->tx_flags |= cpu_to_le16(IEEE80211_RADIOTAP_F_TX_RTS);

	rthdr->data_retries = info->status.retry_count;

	/* XXX: is this sufficient for BPF? */
	skb_set_mac_header(skb, 0);
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb->pkt_type = PACKET_OTHERHOST;
	skb->protocol = htons(ETH_P_802_2);
	memset(skb->cb, 0, sizeof(skb->cb));

	rcu_read_lock();
	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		if (sdata->vif.type == IEEE80211_IF_TYPE_MNTR) {
			if (!netif_running(sdata->dev))
				continue;

			if (prev_dev) {
				skb2 = skb_clone(skb, GFP_ATOMIC);
				if (skb2) {
					skb2->dev = prev_dev;
					netif_rx(skb2);
				}
			}

			prev_dev = sdata->dev;
		}
	}
	if (prev_dev) {
		skb->dev = prev_dev;
		netif_rx(skb);
		skb = NULL;
	}
	rcu_read_unlock();
	dev_kfree_skb(skb);
}
EXPORT_SYMBOL(ieee80211_tx_status);

struct ieee80211_hw *ieee80211_alloc_hw(size_t priv_data_len,
					const struct ieee80211_ops *ops)
{
	struct ieee80211_local *local;
	int priv_size;
	struct wiphy *wiphy;
static void ieee80211_restart_work(struct work_struct *work)
{
	struct ieee80211_local *local =
		container_of(work, struct ieee80211_local, restart_work);
	struct ieee80211_sub_if_data *sdata;

	/* wait for scan work complete */
	flush_workqueue(local->workqueue);

	WARN(test_bit(SCAN_HW_SCANNING, &local->scanning),
	     "%s called with hardware scan in progress\n", __func__);

	rtnl_lock();
	list_for_each_entry(sdata, &local->interfaces, list) {
		/*
		 * XXX: there may be more work for other vif types and even
		 * for station mode: a good thing would be to run most of
		 * the iface type's dependent _stop (ieee80211_mg_stop,
		 * ieee80211_ibss_stop) etc...
		 * For now, fix only the specific bug that was seen: race
		 * between csa_connection_drop_work and us.
		 */
		if (sdata->vif.type == NL80211_IFTYPE_STATION) {
			/*
			 * This worker is scheduled from the iface worker that
			 * runs on mac80211's workqueue, so we can't be
			 * scheduling this worker after the cancel right here.
			 * The exception is ieee80211_chswitch_done.
			 * Then we can have a race...
			 */
			cancel_work_sync(&sdata->u.mgd.csa_connection_drop_work);
		}
		flush_delayed_work(&sdata->dec_tailroom_needed_wk);
	}
	ieee80211_scan_cancel(local);
	ieee80211_reconfig(local);
	rtnl_unlock();
}

void ieee80211_restart_hw(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);

	trace_api_restart_hw(local);

	wiphy_info(hw->wiphy,
		   "Hardware restart was requested\n");

	/* use this reason, ieee80211_reconfig will unblock it */
	ieee80211_stop_queues_by_reason(hw, IEEE80211_MAX_QUEUE_MAP,
					IEEE80211_QUEUE_STOP_REASON_SUSPEND,
					false);

	/*
	 * Stop all Rx during the reconfig. We don't want state changes
	 * or driver callbacks while this is in progress.
	 */
	local->in_reconfig = true;
	barrier();

	queue_work(system_freezable_wq, &local->restart_work);
}
EXPORT_SYMBOL(ieee80211_restart_hw);

#ifdef CONFIG_INET
static int ieee80211_ifa_changed(struct notifier_block *nb,
				 unsigned long data, void *arg)
{
	struct in_ifaddr *ifa = arg;
	struct ieee80211_local *local =
		container_of(nb, struct ieee80211_local,
			     ifa_notifier);
	struct net_device *ndev = ifa->ifa_dev->dev;
	struct wireless_dev *wdev = ndev->ieee80211_ptr;
	struct in_device *idev;
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_bss_conf *bss_conf;
	struct ieee80211_if_managed *ifmgd;
	int c = 0;

	/* Make sure it's our interface that got changed */
	if (!wdev)
		return NOTIFY_DONE;

	if (wdev->wiphy != local->hw.wiphy)
		return NOTIFY_DONE;

	sdata = IEEE80211_DEV_TO_SUB_IF(ndev);
	bss_conf = &sdata->vif.bss_conf;

	/* ARP filtering is only supported in managed mode */
	if (sdata->vif.type != NL80211_IFTYPE_STATION)
		return NOTIFY_DONE;

	idev = __in_dev_get_rtnl(sdata->dev);
	if (!idev)
		return NOTIFY_DONE;

	ifmgd = &sdata->u.mgd;
	sdata_lock(sdata);

	/* Copy the addresses to the bss_conf list */
	ifa = idev->ifa_list;
	while (ifa) {
		if (c < IEEE80211_BSS_ARP_ADDR_LIST_LEN)
			bss_conf->arp_addr_list[c] = ifa->ifa_address;
		ifa = ifa->ifa_next;
		c++;
	}

	bss_conf->arp_addr_cnt = c;

	/* Configure driver only if associated (which also implies it is up) */
	if (ifmgd->associated)
		ieee80211_bss_info_change_notify(sdata,
						 BSS_CHANGED_ARP_FILTER);

	sdata_unlock(sdata);

	return NOTIFY_OK;
}
#endif

#if IS_ENABLED(CONFIG_IPV6)
static int ieee80211_ifa6_changed(struct notifier_block *nb,
				  unsigned long data, void *arg)
{
	struct inet6_ifaddr *ifa = (struct inet6_ifaddr *)arg;
	struct inet6_dev *idev = ifa->idev;
	struct net_device *ndev = ifa->idev->dev;
	struct ieee80211_local *local =
		container_of(nb, struct ieee80211_local, ifa6_notifier);
	struct wireless_dev *wdev = ndev->ieee80211_ptr;
	struct ieee80211_sub_if_data *sdata;

	/* Make sure it's our interface that got changed */
	if (!wdev || wdev->wiphy != local->hw.wiphy)
		return NOTIFY_DONE;

	sdata = IEEE80211_DEV_TO_SUB_IF(ndev);

	/*
	 * For now only support station mode. This is mostly because
	 * doing AP would have to handle AP_VLAN in some way ...
	 */
	if (sdata->vif.type != NL80211_IFTYPE_STATION)
		return NOTIFY_DONE;

	drv_ipv6_addr_change(local, sdata, idev);

	return NOTIFY_OK;
}
#endif

/* There isn't a lot of sense in it, but you can transmit anything you like */
static const struct ieee80211_txrx_stypes
ieee80211_default_mgmt_stypes[NUM_NL80211_IFTYPES] = {
	[NL80211_IFTYPE_ADHOC] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
			BIT(IEEE80211_STYPE_AUTH >> 4) |
			BIT(IEEE80211_STYPE_DEAUTH >> 4) |
			BIT(IEEE80211_STYPE_PROBE_REQ >> 4),
	},
	[NL80211_IFTYPE_STATION] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
			BIT(IEEE80211_STYPE_PROBE_REQ >> 4),
	},
	[NL80211_IFTYPE_AP] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
			BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
			BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
			BIT(IEEE80211_STYPE_DISASSOC >> 4) |
			BIT(IEEE80211_STYPE_AUTH >> 4) |
			BIT(IEEE80211_STYPE_DEAUTH >> 4) |
			BIT(IEEE80211_STYPE_ACTION >> 4),
	},
	[NL80211_IFTYPE_AP_VLAN] = {
		/* copy AP */
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
			BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
			BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
			BIT(IEEE80211_STYPE_DISASSOC >> 4) |
			BIT(IEEE80211_STYPE_AUTH >> 4) |
			BIT(IEEE80211_STYPE_DEAUTH >> 4) |
			BIT(IEEE80211_STYPE_ACTION >> 4),
	},
	[NL80211_IFTYPE_P2P_CLIENT] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
			BIT(IEEE80211_STYPE_PROBE_REQ >> 4),
	},
	[NL80211_IFTYPE_P2P_GO] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
			BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
			BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
			BIT(IEEE80211_STYPE_DISASSOC >> 4) |
			BIT(IEEE80211_STYPE_AUTH >> 4) |
			BIT(IEEE80211_STYPE_DEAUTH >> 4) |
			BIT(IEEE80211_STYPE_ACTION >> 4),
	},
	[NL80211_IFTYPE_MESH_POINT] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
			BIT(IEEE80211_STYPE_AUTH >> 4) |
			BIT(IEEE80211_STYPE_DEAUTH >> 4),
	},
	[NL80211_IFTYPE_P2P_DEVICE] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
			BIT(IEEE80211_STYPE_PROBE_REQ >> 4),
	},
};

static const struct ieee80211_ht_cap mac80211_ht_capa_mod_mask = {
	.ampdu_params_info = IEEE80211_HT_AMPDU_PARM_FACTOR |
			     IEEE80211_HT_AMPDU_PARM_DENSITY,

	.cap_info = cpu_to_le16(IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
				IEEE80211_HT_CAP_MAX_AMSDU |
				IEEE80211_HT_CAP_SGI_20 |
				IEEE80211_HT_CAP_SGI_40 |
				IEEE80211_HT_CAP_LDPC_CODING |
				IEEE80211_HT_CAP_40MHZ_INTOLERANT),
	.mcs = {
		.rx_mask = { 0xff, 0xff, 0xff, 0xff, 0xff,
			     0xff, 0xff, 0xff, 0xff, 0xff, },
	},
};

static const struct ieee80211_vht_cap mac80211_vht_capa_mod_mask = {
	.vht_cap_info =
		cpu_to_le32(IEEE80211_VHT_CAP_RXLDPC |
			    IEEE80211_VHT_CAP_SHORT_GI_80 |
			    IEEE80211_VHT_CAP_SHORT_GI_160 |
			    IEEE80211_VHT_CAP_RXSTBC_MASK |
			    IEEE80211_VHT_CAP_TXSTBC |
			    IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE |
			    IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE |
			    IEEE80211_VHT_CAP_TX_ANTENNA_PATTERN |
			    IEEE80211_VHT_CAP_RX_ANTENNA_PATTERN |
			    IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK),
	.supp_mcs = {
		.rx_mcs_map = cpu_to_le16(~0),
		.tx_mcs_map = cpu_to_le16(~0),
	},
};

struct ieee80211_hw *ieee80211_alloc_hw_nm(size_t priv_data_len,
					   const struct ieee80211_ops *ops,
					   const char *requested_name)
{
	struct ieee80211_local *local;
	int priv_size, i;
	struct wiphy *wiphy;
	bool use_chanctx;

	if (WARN_ON(!ops->tx || !ops->start || !ops->stop || !ops->config ||
		    !ops->add_interface || !ops->remove_interface ||
		    !ops->configure_filter))
		return NULL;

	if (WARN_ON(ops->sta_state && (ops->sta_add || ops->sta_remove)))
		return NULL;

	/* check all or no channel context operations exist */
	i = !!ops->add_chanctx + !!ops->remove_chanctx +
	    !!ops->change_chanctx + !!ops->assign_vif_chanctx +
	    !!ops->unassign_vif_chanctx;
	if (WARN_ON(i != 0 && i != 5))
		return NULL;
	use_chanctx = i == 5;

	/* Ensure 32-byte alignment of our private data and hw private data.
	 * We use the wiphy priv data for both our ieee80211_local and for
	 * the driver's private data
	 *
	 * In memory it'll be like this:
	 *
	 * +-------------------------+
	 * | struct wiphy	    |
	 * +-------------------------+
	 * | struct ieee80211_local  |
	 * +-------------------------+
	 * | driver's private data   |
	 * +-------------------------+
	 *
	 */
	priv_size = ((sizeof(struct ieee80211_local) +
		      NETDEV_ALIGN_CONST) & ~NETDEV_ALIGN_CONST) +
		    priv_data_len;

	wiphy = wiphy_new(&mac80211_config_ops, priv_size);
	priv_size = ALIGN(sizeof(*local), NETDEV_ALIGN) + priv_data_len;

	wiphy = wiphy_new_nm(&mac80211_config_ops, priv_size, requested_name);

	if (!wiphy)
		return NULL;

	wiphy->privid = mac80211_wiphy_privid;

	local = wiphy_priv(wiphy);
	local->hw.wiphy = wiphy;

	local->hw.priv = (char *)local +
			 ((sizeof(struct ieee80211_local) +
			   NETDEV_ALIGN_CONST) & ~NETDEV_ALIGN_CONST);

	BUG_ON(!ops->tx);
	BUG_ON(!ops->start);
	BUG_ON(!ops->stop);
	BUG_ON(!ops->config);
	BUG_ON(!ops->add_interface);
	BUG_ON(!ops->remove_interface);
	BUG_ON(!ops->configure_filter);
	local->ops = ops;

	local->hw.queues = 1; /* default */

	local->bridge_packets = 1;

	local->rts_threshold = IEEE80211_MAX_RTS_THRESHOLD;
	local->fragmentation_threshold = IEEE80211_MAX_FRAG_THRESHOLD;
	local->short_retry_limit = 7;
	local->long_retry_limit = 4;
	local->hw.conf.radio_enabled = 1;

	INIT_LIST_HEAD(&local->interfaces);

	spin_lock_init(&local->key_lock);

	INIT_DELAYED_WORK(&local->scan_work, ieee80211_sta_scan_work);

	sta_info_init(local);

	tasklet_init(&local->tx_pending_tasklet, ieee80211_tx_pending,
		     (unsigned long)local);
	tasklet_disable(&local->tx_pending_tasklet);
	wiphy->mgmt_stypes = ieee80211_default_mgmt_stypes;

	wiphy->privid = mac80211_wiphy_privid;

	wiphy->flags |= WIPHY_FLAG_NETNS_OK |
			WIPHY_FLAG_4ADDR_AP |
			WIPHY_FLAG_4ADDR_STATION |
			WIPHY_FLAG_REPORTS_OBSS |
			WIPHY_FLAG_OFFCHAN_TX;

	if (ops->remain_on_channel)
		wiphy->flags |= WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL;

	wiphy->features |= NL80211_FEATURE_SK_TX_STATUS |
			   NL80211_FEATURE_SAE |
			   NL80211_FEATURE_HT_IBSS |
			   NL80211_FEATURE_VIF_TXPOWER |
			   NL80211_FEATURE_MAC_ON_CREATE |
			   NL80211_FEATURE_USERSPACE_MPM;

	if (!ops->hw_scan)
		wiphy->features |= NL80211_FEATURE_LOW_PRIORITY_SCAN |
				   NL80211_FEATURE_AP_SCAN;


	if (!ops->set_key)
		wiphy->flags |= WIPHY_FLAG_IBSS_RSN;

	wiphy->bss_priv_size = sizeof(struct ieee80211_bss);

	local = wiphy_priv(wiphy);

	if (sta_info_init(local))
		goto err_free;

	local->hw.wiphy = wiphy;

	local->hw.priv = (char *)local + ALIGN(sizeof(*local), NETDEV_ALIGN);

	local->ops = ops;
	local->use_chanctx = use_chanctx;

	/* set up some defaults */
	local->hw.queues = 1;
	local->hw.max_rates = 1;
	local->hw.max_report_rates = 0;
	local->hw.max_rx_aggregation_subframes = IEEE80211_MAX_AMPDU_BUF;
	local->hw.max_tx_aggregation_subframes = IEEE80211_MAX_AMPDU_BUF;
	local->hw.offchannel_tx_hw_queue = IEEE80211_INVAL_HW_QUEUE;
	local->hw.conf.long_frame_max_tx_count = wiphy->retry_long;
	local->hw.conf.short_frame_max_tx_count = wiphy->retry_short;
	local->hw.radiotap_mcs_details = IEEE80211_RADIOTAP_MCS_HAVE_MCS |
					 IEEE80211_RADIOTAP_MCS_HAVE_GI |
					 IEEE80211_RADIOTAP_MCS_HAVE_BW;
	local->hw.radiotap_vht_details = IEEE80211_RADIOTAP_VHT_KNOWN_GI |
					 IEEE80211_RADIOTAP_VHT_KNOWN_BANDWIDTH;
	local->hw.uapsd_queues = IEEE80211_DEFAULT_UAPSD_QUEUES;
	local->hw.uapsd_max_sp_len = IEEE80211_DEFAULT_MAX_SP_LEN;
	local->user_power_level = IEEE80211_UNSET_POWER_LEVEL;
	wiphy->ht_capa_mod_mask = &mac80211_ht_capa_mod_mask;
	wiphy->vht_capa_mod_mask = &mac80211_vht_capa_mod_mask;

	local->ext_capa[7] = WLAN_EXT_CAPA8_OPMODE_NOTIF;

	wiphy->extended_capabilities = local->ext_capa;
	wiphy->extended_capabilities_mask = local->ext_capa;
	wiphy->extended_capabilities_len =
		ARRAY_SIZE(local->ext_capa);

	INIT_LIST_HEAD(&local->interfaces);

	__hw_addr_init(&local->mc_list);

	mutex_init(&local->iflist_mtx);
	mutex_init(&local->mtx);

	mutex_init(&local->key_mtx);
	spin_lock_init(&local->filter_lock);
	spin_lock_init(&local->rx_path_lock);
	spin_lock_init(&local->queue_stop_reason_lock);

	INIT_LIST_HEAD(&local->chanctx_list);
	mutex_init(&local->chanctx_mtx);

	INIT_DELAYED_WORK(&local->scan_work, ieee80211_scan_work);

	INIT_WORK(&local->restart_work, ieee80211_restart_work);

	INIT_WORK(&local->radar_detected_work,
		  ieee80211_dfs_radar_detected_work);

	INIT_WORK(&local->reconfig_filter, ieee80211_reconfig_filter);
	local->smps_mode = IEEE80211_SMPS_OFF;

	INIT_WORK(&local->dynamic_ps_enable_work,
		  ieee80211_dynamic_ps_enable_work);
	INIT_WORK(&local->dynamic_ps_disable_work,
		  ieee80211_dynamic_ps_disable_work);
	setup_timer(&local->dynamic_ps_timer,
		    ieee80211_dynamic_ps_timer, (unsigned long) local);

	INIT_WORK(&local->sched_scan_stopped_work,
		  ieee80211_sched_scan_stopped_work);

	INIT_WORK(&local->tdls_chsw_work, ieee80211_tdls_chsw_work);

	spin_lock_init(&local->ack_status_lock);
	idr_init(&local->ack_status_frames);

	for (i = 0; i < IEEE80211_MAX_QUEUES; i++) {
		skb_queue_head_init(&local->pending[i]);
		atomic_set(&local->agg_queue_stop[i], 0);
	}
	tasklet_init(&local->tx_pending_tasklet, ieee80211_tx_pending,
		     (unsigned long)local);

	tasklet_init(&local->tasklet,
		     ieee80211_tasklet_handler,
		     (unsigned long) local);
	tasklet_disable(&local->tasklet);

	skb_queue_head_init(&local->skb_queue);
	skb_queue_head_init(&local->skb_queue_unreliable);

	return local_to_hw(local);
}
EXPORT_SYMBOL(ieee80211_alloc_hw);

	skb_queue_head_init(&local->skb_queue);
	skb_queue_head_init(&local->skb_queue_unreliable);
	skb_queue_head_init(&local->skb_queue_tdls_chsw);

	ieee80211_alloc_led_names(local);

	ieee80211_roc_setup(local);

	return &local->hw;
 err_free:
	wiphy_free(wiphy);
	return NULL;
}
EXPORT_SYMBOL(ieee80211_alloc_hw_nm);

static int ieee80211_init_cipher_suites(struct ieee80211_local *local)
{
	bool have_wep = !(IS_ERR(local->wep_tx_tfm) ||
			  IS_ERR(local->wep_rx_tfm));
	bool have_mfp = ieee80211_hw_check(&local->hw, MFP_CAPABLE);
	int n_suites = 0, r = 0, w = 0;
	u32 *suites;
	static const u32 cipher_suites[] = {
		/* keep WEP first, it may be removed below */
		WLAN_CIPHER_SUITE_WEP40,
		WLAN_CIPHER_SUITE_WEP104,
		WLAN_CIPHER_SUITE_TKIP,
		WLAN_CIPHER_SUITE_CCMP,
		WLAN_CIPHER_SUITE_CCMP_256,
		WLAN_CIPHER_SUITE_GCMP,
		WLAN_CIPHER_SUITE_GCMP_256,

		/* keep last -- depends on hw flags! */
		WLAN_CIPHER_SUITE_AES_CMAC,
		WLAN_CIPHER_SUITE_BIP_CMAC_256,
		WLAN_CIPHER_SUITE_BIP_GMAC_128,
		WLAN_CIPHER_SUITE_BIP_GMAC_256,
	};

	if (ieee80211_hw_check(&local->hw, SW_CRYPTO_CONTROL) ||
	    local->hw.wiphy->cipher_suites) {
		/* If the driver advertises, or doesn't support SW crypto,
		 * we only need to remove WEP if necessary.
		 */
		if (have_wep)
			return 0;

		/* well if it has _no_ ciphers ... fine */
		if (!local->hw.wiphy->n_cipher_suites)
			return 0;

		/* Driver provides cipher suites, but we need to exclude WEP */
		suites = kmemdup(local->hw.wiphy->cipher_suites,
				 sizeof(u32) * local->hw.wiphy->n_cipher_suites,
				 GFP_KERNEL);
		if (!suites)
			return -ENOMEM;

		for (r = 0; r < local->hw.wiphy->n_cipher_suites; r++) {
			u32 suite = local->hw.wiphy->cipher_suites[r];

			if (suite == WLAN_CIPHER_SUITE_WEP40 ||
			    suite == WLAN_CIPHER_SUITE_WEP104)
				continue;
			suites[w++] = suite;
		}
	} else if (!local->hw.cipher_schemes) {
		/* If the driver doesn't have cipher schemes, there's nothing
		 * else to do other than assign the (software supported and
		 * perhaps offloaded) cipher suites.
		 */
		local->hw.wiphy->cipher_suites = cipher_suites;
		local->hw.wiphy->n_cipher_suites = ARRAY_SIZE(cipher_suites);

		if (!have_mfp)
			local->hw.wiphy->n_cipher_suites -= 4;

		if (!have_wep) {
			local->hw.wiphy->cipher_suites += 2;
			local->hw.wiphy->n_cipher_suites -= 2;
		}

		/* not dynamically allocated, so just return */
		return 0;
	} else {
		const struct ieee80211_cipher_scheme *cs;

		cs = local->hw.cipher_schemes;

		/* Driver specifies cipher schemes only (but not cipher suites
		 * including the schemes)
		 *
		 * We start counting ciphers defined by schemes, TKIP, CCMP,
		 * CCMP-256, GCMP, and GCMP-256
		 */
		n_suites = local->hw.n_cipher_schemes + 5;

		/* check if we have WEP40 and WEP104 */
		if (have_wep)
			n_suites += 2;

		/* check if we have AES_CMAC, BIP-CMAC-256, BIP-GMAC-128,
		 * BIP-GMAC-256
		 */
		if (have_mfp)
			n_suites += 4;

		suites = kmalloc(sizeof(u32) * n_suites, GFP_KERNEL);
		if (!suites)
			return -ENOMEM;

		suites[w++] = WLAN_CIPHER_SUITE_CCMP;
		suites[w++] = WLAN_CIPHER_SUITE_CCMP_256;
		suites[w++] = WLAN_CIPHER_SUITE_TKIP;
		suites[w++] = WLAN_CIPHER_SUITE_GCMP;
		suites[w++] = WLAN_CIPHER_SUITE_GCMP_256;

		if (have_wep) {
			suites[w++] = WLAN_CIPHER_SUITE_WEP40;
			suites[w++] = WLAN_CIPHER_SUITE_WEP104;
		}

		if (have_mfp) {
			suites[w++] = WLAN_CIPHER_SUITE_AES_CMAC;
			suites[w++] = WLAN_CIPHER_SUITE_BIP_CMAC_256;
			suites[w++] = WLAN_CIPHER_SUITE_BIP_GMAC_128;
			suites[w++] = WLAN_CIPHER_SUITE_BIP_GMAC_256;
		}

		for (r = 0; r < local->hw.n_cipher_schemes; r++) {
			suites[w++] = cs[r].cipher;
			if (WARN_ON(cs[r].pn_len > IEEE80211_MAX_PN_LEN)) {
				kfree(suites);
				return -EINVAL;
			}
		}
	}

	local->hw.wiphy->cipher_suites = suites;
	local->hw.wiphy->n_cipher_suites = w;
	local->wiphy_ciphers_allocated = true;

	return 0;
}

int ieee80211_register_hw(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);
	const char *name;
	int result;
	enum ieee80211_band band;
	struct net_device *mdev;
	struct wireless_dev *mwdev;
	int result, i;
	enum ieee80211_band band;
	int channels, max_bitrates;
	bool supp_ht, supp_vht;
	netdev_features_t feature_whitelist;
	struct cfg80211_chan_def dflt_chandef = {};

	if (ieee80211_hw_check(hw, QUEUE_CONTROL) &&
	    (local->hw.offchannel_tx_hw_queue == IEEE80211_INVAL_HW_QUEUE ||
	     local->hw.offchannel_tx_hw_queue >= local->hw.queues))
		return -EINVAL;

	if ((hw->wiphy->features & NL80211_FEATURE_TDLS_CHANNEL_SWITCH) &&
	    (!local->ops->tdls_channel_switch ||
	     !local->ops->tdls_cancel_channel_switch ||
	     !local->ops->tdls_recv_channel_switch))
		return -EOPNOTSUPP;

#ifdef CONFIG_PM
	if (hw->wiphy->wowlan && (!local->ops->suspend || !local->ops->resume))
		return -EINVAL;
#endif

	if (!local->use_chanctx) {
		for (i = 0; i < local->hw.wiphy->n_iface_combinations; i++) {
			const struct ieee80211_iface_combination *comb;

			comb = &local->hw.wiphy->iface_combinations[i];

			if (comb->num_different_channels > 1)
				return -EINVAL;
		}
	} else {
		/*
		 * WDS is currently prohibited when channel contexts are used
		 * because there's no clear definition of which channel WDS
		 * type interfaces use
		 */
		if (local->hw.wiphy->interface_modes & BIT(NL80211_IFTYPE_WDS))
			return -EINVAL;

		/* DFS is not supported with multi-channel combinations yet */
		for (i = 0; i < local->hw.wiphy->n_iface_combinations; i++) {
			const struct ieee80211_iface_combination *comb;

			comb = &local->hw.wiphy->iface_combinations[i];

			if (comb->radar_detect_widths &&
			    comb->num_different_channels > 1)
				return -EINVAL;
		}
	}

	/* Only HW csum features are currently compatible with mac80211 */
	feature_whitelist = NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
			    NETIF_F_HW_CSUM | NETIF_F_SG | NETIF_F_HIGHDMA |
			    NETIF_F_GSO_SOFTWARE;
	if (WARN_ON(hw->netdev_features & ~feature_whitelist))
		return -EINVAL;

	if (hw->max_report_rates == 0)
		hw->max_report_rates = hw->max_rates;

	local->rx_chains = 1;

	/*
	 * generic code guarantees at least one band,
	 * set this very early because much code assumes
	 * that hw.conf.channel is assigned
	 */
	channels = 0;
	max_bitrates = 0;
	supp_ht = false;
	supp_vht = false;
	for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
		struct ieee80211_supported_band *sband;

		sband = local->hw.wiphy->bands[band];
		if (sband) {
			/* init channel we're on */
			local->hw.conf.channel =
			local->oper_channel =
			local->scan_channel = &sband->channels[0];
			break;
		}
	}

	result = wiphy_register(local->hw.wiphy);
	if (result < 0)
		return result;
		if (!sband)
			continue;

		if (!dflt_chandef.chan) {
			/*
			 * Assign the first enabled channel to dflt_chandef
			 * from the list of channels
			 */
			for (i = 0; i < sband->n_channels; i++)
				if (!(sband->channels[i].flags &
						IEEE80211_CHAN_DISABLED))
					break;
			/* if none found then use the first anyway */
			if (i == sband->n_channels)
				i = 0;
			cfg80211_chandef_create(&dflt_chandef,
						&sband->channels[i],
						NL80211_CHAN_NO_HT);
			/* init channel we're on */
			if (!local->use_chanctx && !local->_oper_chandef.chan) {
				local->hw.conf.chandef = dflt_chandef;
				local->_oper_chandef = dflt_chandef;
			}
			local->monitor_chandef = dflt_chandef;
		}

		channels += sband->n_channels;

		if (max_bitrates < sband->n_bitrates)
			max_bitrates = sband->n_bitrates;
		supp_ht = supp_ht || sband->ht_cap.ht_supported;
		supp_vht = supp_vht || sband->vht_cap.vht_supported;

		if (!sband->ht_cap.ht_supported)
			continue;

		/* TODO: consider VHT for RX chains, hopefully it's the same */
		local->rx_chains =
			max(ieee80211_mcs_to_chains(&sband->ht_cap.mcs),
			    local->rx_chains);

		/* no need to mask, SM_PS_DISABLED has all bits set */
		sband->ht_cap.cap |= WLAN_HT_CAP_SM_PS_DISABLED <<
			             IEEE80211_HT_CAP_SM_PS_SHIFT;
	}

	/* if low-level driver supports AP, we also support VLAN */
	if (local->hw.wiphy->interface_modes & BIT(NL80211_IFTYPE_AP)) {
		hw->wiphy->interface_modes |= BIT(NL80211_IFTYPE_AP_VLAN);
		hw->wiphy->software_iftypes |= BIT(NL80211_IFTYPE_AP_VLAN);
	}

	/* mac80211 always supports monitor */
	hw->wiphy->interface_modes |= BIT(NL80211_IFTYPE_MONITOR);
	hw->wiphy->software_iftypes |= BIT(NL80211_IFTYPE_MONITOR);

	/* mac80211 doesn't support more than one IBSS interface right now */
	for (i = 0; i < hw->wiphy->n_iface_combinations; i++) {
		const struct ieee80211_iface_combination *c;
		int j;

		c = &hw->wiphy->iface_combinations[i];

		for (j = 0; j < c->n_limits; j++)
			if ((c->limits[j].types & BIT(NL80211_IFTYPE_ADHOC)) &&
			    c->limits[j].max > 1)
				return -EINVAL;
	}

	local->int_scan_req = kzalloc(sizeof(*local->int_scan_req) +
				      sizeof(void *) * channels, GFP_KERNEL);
	if (!local->int_scan_req)
		return -ENOMEM;

	for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
		if (!local->hw.wiphy->bands[band])
			continue;
		local->int_scan_req->rates[band] = (u32) -1;
	}

#ifndef CONFIG_MAC80211_MESH
	/* mesh depends on Kconfig, but drivers should set it if they want */
	local->hw.wiphy->interface_modes &= ~BIT(NL80211_IFTYPE_MESH_POINT);
#endif

	/* if the underlying driver supports mesh, mac80211 will (at least)
	 * provide routing of mesh authentication frames to userspace */
	if (local->hw.wiphy->interface_modes & BIT(NL80211_IFTYPE_MESH_POINT))
		local->hw.wiphy->flags |= WIPHY_FLAG_MESH_AUTH;

	/* mac80211 supports control port protocol changing */
	local->hw.wiphy->flags |= WIPHY_FLAG_CONTROL_PORT_PROTOCOL;

	if (ieee80211_hw_check(&local->hw, SIGNAL_DBM)) {
		local->hw.wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;
	} else if (ieee80211_hw_check(&local->hw, SIGNAL_UNSPEC)) {
		local->hw.wiphy->signal_type = CFG80211_SIGNAL_TYPE_UNSPEC;
		if (hw->max_signal <= 0) {
			result = -EINVAL;
			goto fail_wiphy_register;
		}
	}

	/*
	 * Calculate scan IE length -- we need this to alloc
	 * memory and to subtract from the driver limit. It
	 * includes the DS Params, (extended) supported rates, and HT
	 * information -- SSID is the driver's responsibility.
	 */
	local->scan_ies_len = 4 + max_bitrates /* (ext) supp rates */ +
		3 /* DS Params */;
	if (supp_ht)
		local->scan_ies_len += 2 + sizeof(struct ieee80211_ht_cap);

	if (supp_vht)
		local->scan_ies_len +=
			2 + sizeof(struct ieee80211_vht_cap);

	if (!local->ops->hw_scan) {
		/* For hw_scan, driver needs to set these up. */
		local->hw.wiphy->max_scan_ssids = 4;
		local->hw.wiphy->max_scan_ie_len = IEEE80211_MAX_DATA_LEN;
	}

	/*
	 * If the driver supports any scan IEs, then assume the
	 * limit includes the IEs mac80211 will add, otherwise
	 * leave it at zero and let the driver sort it out; we
	 * still pass our IEs to the driver but userspace will
	 * not be allowed to in that case.
	 */
	if (local->hw.wiphy->max_scan_ie_len)
		local->hw.wiphy->max_scan_ie_len -= local->scan_ies_len;

	if (WARN_ON(!ieee80211_cs_list_valid(local->hw.cipher_schemes,
					     local->hw.n_cipher_schemes))) {
		result = -EINVAL;
		goto fail_workqueue;
	}

	result = ieee80211_init_cipher_suites(local);
	if (result < 0)
		goto fail_wiphy_register;

	if (!local->ops->remain_on_channel)
		local->hw.wiphy->max_remain_on_channel_duration = 5000;

	/* mac80211 based drivers don't support internal TDLS setup */
	if (local->hw.wiphy->flags & WIPHY_FLAG_SUPPORTS_TDLS)
		local->hw.wiphy->flags |= WIPHY_FLAG_TDLS_EXTERNAL_SETUP;

	/* mac80211 supports eCSA, if the driver supports STA CSA at all */
	if (ieee80211_hw_check(&local->hw, CHANCTX_STA_CSA))
		local->ext_capa[0] |= WLAN_EXT_CAPA1_EXT_CHANNEL_SWITCHING;

	local->hw.wiphy->max_num_csa_counters = IEEE80211_MAX_CSA_COUNTERS_NUM;

	result = wiphy_register(local->hw.wiphy);
	if (result < 0)
		goto fail_wiphy_register;

	/*
	 * We use the number of queues for feature tests (QoS, HT) internally
	 * so restrict them appropriately.
	 */
	if (hw->queues > IEEE80211_MAX_QUEUES)
		hw->queues = IEEE80211_MAX_QUEUES;
	if (hw->ampdu_queues > IEEE80211_MAX_AMPDU_QUEUES)
		hw->ampdu_queues = IEEE80211_MAX_AMPDU_QUEUES;
	if (hw->queues < 4)
		hw->ampdu_queues = 0;

	mdev = alloc_netdev_mq(sizeof(struct wireless_dev),
			       "wmaster%d", ether_setup,
			       ieee80211_num_queues(hw));
	if (!mdev)
		goto fail_mdev_alloc;

	mwdev = netdev_priv(mdev);
	mdev->ieee80211_ptr = mwdev;
	mwdev->wiphy = local->hw.wiphy;

	local->mdev = mdev;

	ieee80211_rx_bss_list_init(local);

	mdev->hard_start_xmit = ieee80211_master_start_xmit;
	mdev->open = ieee80211_master_open;
	mdev->stop = ieee80211_master_stop;
	mdev->type = ARPHRD_IEEE80211;
	mdev->header_ops = &ieee80211_header_ops;
	mdev->set_multicast_list = ieee80211_master_set_multicast_list;

	name = wiphy_dev(local->hw.wiphy)->driver->name;
	local->hw.workqueue = create_freezeable_workqueue(name);
	if (!local->hw.workqueue) {

	local->workqueue =
		alloc_ordered_workqueue("%s", 0, wiphy_name(local->hw.wiphy));
	if (!local->workqueue) {
		result = -ENOMEM;
		goto fail_workqueue;
	}

	/*
	 * The hardware needs headroom for sending the frame,
	 * and we need some headroom for passing the frame to monitor
	 * interfaces, but never both at the same time.
	 */
	local->tx_headroom = max_t(unsigned int , local->hw.extra_tx_headroom,
				   sizeof(struct ieee80211_tx_status_rtap_hdr));

	debugfs_hw_add(local);

	if (local->hw.conf.beacon_int < 10)
		local->hw.conf.beacon_int = 100;

	if (local->hw.max_listen_interval == 0)
		local->hw.max_listen_interval = 1;

	local->hw.conf.listen_interval = local->hw.max_listen_interval;

	local->wstats_flags |= local->hw.flags & (IEEE80211_HW_SIGNAL_UNSPEC |
						  IEEE80211_HW_SIGNAL_DB |
						  IEEE80211_HW_SIGNAL_DBM) ?
			       IW_QUAL_QUAL_UPDATED : IW_QUAL_QUAL_INVALID;
	local->wstats_flags |= local->hw.flags & IEEE80211_HW_NOISE_DBM ?
			       IW_QUAL_NOISE_UPDATED : IW_QUAL_NOISE_INVALID;
	if (local->hw.flags & IEEE80211_HW_SIGNAL_DBM)
		local->wstats_flags |= IW_QUAL_DBM;

	result = sta_info_start(local);
	if (result < 0)
		goto fail_sta_info;

	rtnl_lock();
	result = dev_alloc_name(local->mdev, local->mdev->name);
	if (result < 0)
		goto fail_dev;

	memcpy(local->mdev->dev_addr, local->hw.wiphy->perm_addr, ETH_ALEN);
	SET_NETDEV_DEV(local->mdev, wiphy_dev(local->hw.wiphy));

	result = register_netdevice(local->mdev);
	if (result < 0)
		goto fail_dev;
				   IEEE80211_TX_STATUS_HEADROOM);

	debugfs_hw_add(local);

	/*
	 * if the driver doesn't specify a max listen interval we
	 * use 5 which should be a safe default
	 */
	if (local->hw.max_listen_interval == 0)
		local->hw.max_listen_interval = 5;

	local->hw.conf.listen_interval = local->hw.max_listen_interval;

	local->dynamic_ps_forced_timeout = -1;

	if (!local->hw.txq_ac_max_pending)
		local->hw.txq_ac_max_pending = 64;

	result = ieee80211_wep_init(local);
	if (result < 0)
		wiphy_debug(local->hw.wiphy, "Failed to initialize wep: %d\n",
			    result);

	local->hw.conf.flags = IEEE80211_CONF_IDLE;

	ieee80211_led_init(local);

	rtnl_lock();

	result = ieee80211_init_rate_ctrl_alg(local,
					      hw->rate_control_algorithm);
	if (result < 0) {
		printk(KERN_DEBUG "%s: Failed to initialize rate control "
		       "algorithm\n", wiphy_name(local->hw.wiphy));
		goto fail_rate;
	}

	result = ieee80211_wep_init(local);

	if (result < 0) {
		printk(KERN_DEBUG "%s: Failed to initialize wep: %d\n",
		       wiphy_name(local->hw.wiphy), result);
		goto fail_wep;
	}

	local->mdev->select_queue = ieee80211_select_queue;

	/* add one default STA interface */
	result = ieee80211_if_add(local, "wlan%d", NULL,
				  IEEE80211_IF_TYPE_STA, NULL);
	if (result)
		printk(KERN_WARNING "%s: Failed to add default virtual iface\n",
		       wiphy_name(local->hw.wiphy));

	rtnl_unlock();

	ieee80211_led_init(local);

	return 0;

fail_wep:
	rate_control_deinitialize(local);
fail_rate:
	unregister_netdevice(local->mdev);
	local->mdev = NULL;
fail_dev:
	rtnl_unlock();
	sta_info_stop(local);
fail_sta_info:
	debugfs_hw_del(local);
	destroy_workqueue(local->hw.workqueue);
fail_workqueue:
	if (local->mdev)
		free_netdev(local->mdev);
fail_mdev_alloc:
	wiphy_unregister(local->hw.wiphy);
		wiphy_debug(local->hw.wiphy,
			    "Failed to initialize rate control algorithm\n");
		goto fail_rate;
	}

	/* add one default STA interface if supported */
	if (local->hw.wiphy->interface_modes & BIT(NL80211_IFTYPE_STATION) &&
	    !ieee80211_hw_check(hw, NO_AUTO_VIF)) {
		result = ieee80211_if_add(local, "wlan%d", NET_NAME_ENUM, NULL,
					  NL80211_IFTYPE_STATION, NULL);
		if (result)
			wiphy_warn(local->hw.wiphy,
				   "Failed to add default virtual iface\n");
	}

	rtnl_unlock();

#ifdef CONFIG_INET
	local->ifa_notifier.notifier_call = ieee80211_ifa_changed;
	result = register_inetaddr_notifier(&local->ifa_notifier);
	if (result)
		goto fail_ifa;
#endif

#if IS_ENABLED(CONFIG_IPV6)
	local->ifa6_notifier.notifier_call = ieee80211_ifa6_changed;
	result = register_inet6addr_notifier(&local->ifa6_notifier);
	if (result)
		goto fail_ifa6;
#endif

	return 0;

#if IS_ENABLED(CONFIG_IPV6)
 fail_ifa6:
#ifdef CONFIG_INET
	unregister_inetaddr_notifier(&local->ifa_notifier);
#endif
#endif
#if defined(CONFIG_INET) || defined(CONFIG_IPV6)
 fail_ifa:
#endif
	rtnl_lock();
	rate_control_deinitialize(local);
	ieee80211_remove_interfaces(local);
 fail_rate:
	rtnl_unlock();
	ieee80211_led_exit(local);
	ieee80211_wep_free(local);
	destroy_workqueue(local->workqueue);
 fail_workqueue:
	wiphy_unregister(local->hw.wiphy);
 fail_wiphy_register:
	if (local->wiphy_ciphers_allocated)
		kfree(local->hw.wiphy->cipher_suites);
	kfree(local->int_scan_req);
	return result;
}
EXPORT_SYMBOL(ieee80211_register_hw);

void ieee80211_unregister_hw(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);

	tasklet_kill(&local->tx_pending_tasklet);
	tasklet_kill(&local->tasklet);

#ifdef CONFIG_INET
	unregister_inetaddr_notifier(&local->ifa_notifier);
#endif
#if IS_ENABLED(CONFIG_IPV6)
	unregister_inet6addr_notifier(&local->ifa6_notifier);
#endif

	rtnl_lock();

	/*
	 * At this point, interface list manipulations are fine
	 * because the driver cannot be handing us frames any
	 * more and the tasklet is killed.
	 */

	/* First, we remove all virtual interfaces. */
	ieee80211_remove_interfaces(local);

	/* then, finally, remove the master interface */
	unregister_netdevice(local->mdev);

	rtnl_unlock();

	ieee80211_rx_bss_list_deinit(local);
	ieee80211_clear_tx_pending(local);
	sta_info_stop(local);
	rate_control_deinitialize(local);
	debugfs_hw_del(local);

	if (skb_queue_len(&local->skb_queue)
			|| skb_queue_len(&local->skb_queue_unreliable))
		printk(KERN_WARNING "%s: skb_queue not empty\n",
		       wiphy_name(local->hw.wiphy));
	skb_queue_purge(&local->skb_queue);
	skb_queue_purge(&local->skb_queue_unreliable);

	destroy_workqueue(local->hw.workqueue);
	wiphy_unregister(local->hw.wiphy);
	ieee80211_wep_free(local);
	ieee80211_led_exit(local);
	free_netdev(local->mdev);
}
EXPORT_SYMBOL(ieee80211_unregister_hw);

	ieee80211_remove_interfaces(local);

	rtnl_unlock();

	cancel_work_sync(&local->restart_work);
	cancel_work_sync(&local->reconfig_filter);
	cancel_work_sync(&local->tdls_chsw_work);
	flush_work(&local->sched_scan_stopped_work);

	ieee80211_clear_tx_pending(local);
	rate_control_deinitialize(local);

	if (skb_queue_len(&local->skb_queue) ||
	    skb_queue_len(&local->skb_queue_unreliable))
		wiphy_warn(local->hw.wiphy, "skb_queue not empty\n");
	skb_queue_purge(&local->skb_queue);
	skb_queue_purge(&local->skb_queue_unreliable);
	skb_queue_purge(&local->skb_queue_tdls_chsw);

	destroy_workqueue(local->workqueue);
	wiphy_unregister(local->hw.wiphy);
	ieee80211_wep_free(local);
	ieee80211_led_exit(local);
	kfree(local->int_scan_req);
}
EXPORT_SYMBOL(ieee80211_unregister_hw);

static int ieee80211_free_ack_frame(int id, void *p, void *data)
{
	WARN_ONCE(1, "Have pending ack frames!\n");
	kfree_skb(p);
	return 0;
}

void ieee80211_free_hw(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);

	mutex_destroy(&local->iflist_mtx);
	mutex_destroy(&local->mtx);

	if (local->wiphy_ciphers_allocated)
		kfree(local->hw.wiphy->cipher_suites);

	idr_for_each(&local->ack_status_frames,
		     ieee80211_free_ack_frame, NULL);
	idr_destroy(&local->ack_status_frames);

	sta_info_stop(local);

	ieee80211_free_led_names(local);

	wiphy_free(local->hw.wiphy);
}
EXPORT_SYMBOL(ieee80211_free_hw);

static int __init ieee80211_init(void)
{
	struct sk_buff *skb;
	int ret;

	BUILD_BUG_ON(sizeof(struct ieee80211_tx_info) > sizeof(skb->cb));
	BUILD_BUG_ON(offsetof(struct ieee80211_tx_info, driver_data) +
	             IEEE80211_TX_INFO_DRIVER_DATA_SIZE > sizeof(skb->cb));

	ret = rc80211_pid_init();
	if (ret)
		return ret;

	ieee80211_debugfs_netdev_init();

	return 0;
		     IEEE80211_TX_INFO_DRIVER_DATA_SIZE > sizeof(skb->cb));

	ret = rc80211_minstrel_init();
	if (ret)
		return ret;

	ret = rc80211_minstrel_ht_init();
	if (ret)
		goto err_minstrel;

	ret = ieee80211_iface_init();
	if (ret)
		goto err_netdev;

	return 0;
 err_netdev:
	rc80211_minstrel_ht_exit();
 err_minstrel:
	rc80211_minstrel_exit();

	return ret;
}

static void __exit ieee80211_exit(void)
{
	rc80211_pid_exit();

	/*
	 * For key todo, it'll be empty by now but the work
	 * might still be scheduled.
	 */
	flush_scheduled_work();

	if (mesh_allocated)
		ieee80211s_stop();

	ieee80211_debugfs_netdev_exit();
	rc80211_minstrel_ht_exit();
	rc80211_minstrel_exit();

	ieee80211s_stop();

	ieee80211_iface_exit();

	rcu_barrier();
}


subsys_initcall(ieee80211_init);
module_exit(ieee80211_exit);

MODULE_DESCRIPTION("IEEE 802.11 subsystem");
MODULE_LICENSE("GPL");
