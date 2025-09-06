/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005, Devicescape Software, Inc.
 * Copyright (c) 2006 Jiri Benc <jbenc@suse.cz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef IEEE80211_RATE_H
#define IEEE80211_RATE_H

#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <linux/kref.h>
#include <net/mac80211.h>
#include "ieee80211_i.h"
#include "sta_info.h"

/**
 * struct rate_selection - rate selection for rate control algos
 * @rate: selected transmission rate index
 * @nonerp: Non-ERP rate to use instead if ERP cannot be used
 * @probe: rate for probing (or -1)
 *
 */
struct rate_selection {
	s8 rate_idx, nonerp_idx, probe_idx;
};

struct rate_control_ops {
	struct module *module;
	const char *name;
	void (*tx_status)(void *priv, struct net_device *dev,
			  struct sk_buff *skb);
	void (*get_rate)(void *priv, struct net_device *dev,
			 struct ieee80211_supported_band *band,
			 struct sk_buff *skb,
			 struct rate_selection *sel);
	void (*rate_init)(void *priv, void *priv_sta,
			  struct ieee80211_local *local, struct sta_info *sta);
	void (*clear)(void *priv);

	void *(*alloc)(struct ieee80211_local *local);
	void (*free)(void *priv);
	void *(*alloc_sta)(void *priv, gfp_t gfp);
	void (*free_sta)(void *priv, void *priv_sta);

	int (*add_attrs)(void *priv, struct kobject *kobj);
	void (*remove_attrs)(void *priv, struct kobject *kobj);
	void (*add_sta_debugfs)(void *priv, void *priv_sta,
				struct dentry *dir);
	void (*remove_sta_debugfs)(void *priv, void *priv_sta);
};

struct rate_control_ref {
	struct rate_control_ops *ops;
	void *priv;
	struct kref kref;
};

int ieee80211_rate_control_register(struct rate_control_ops *ops);
void ieee80211_rate_control_unregister(struct rate_control_ops *ops);

/* Get a reference to the rate control algorithm. If `name' is NULL, get the
 * first available algorithm. */
struct rate_control_ref *rate_control_alloc(const char *name,
					    struct ieee80211_local *local);
void rate_control_get_rate(struct net_device *dev,
			   struct ieee80211_supported_band *sband,
			   struct sk_buff *skb,
			   struct rate_selection *sel);
struct rate_control_ref *rate_control_get(struct rate_control_ref *ref);
void rate_control_put(struct rate_control_ref *ref);

static inline void rate_control_tx_status(struct net_device *dev,
					  struct sk_buff *skb)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct rate_control_ref *ref = local->rate_ctrl;

	ref->ops->tx_status(ref->priv, dev, skb);
}


static inline void rate_control_rate_init(struct sta_info *sta,
					  struct ieee80211_local *local)
{
	struct rate_control_ref *ref = sta->rate_ctrl;
	ref->ops->rate_init(ref->priv, sta->rate_ctrl_priv, local, sta);
}


static inline void rate_control_clear(struct ieee80211_local *local)
{
	struct rate_control_ref *ref = local->rate_ctrl;
	ref->ops->clear(ref->priv);
}

static inline void *rate_control_alloc_sta(struct rate_control_ref *ref,
					   gfp_t gfp)
{
	return ref->ops->alloc_sta(ref->priv, gfp);
}

static inline void rate_control_free_sta(struct rate_control_ref *ref,
					 void *priv)
{
	ref->ops->free_sta(ref->priv, priv);
#include <net/mac80211.h>
#include "ieee80211_i.h"
#include "sta_info.h"
#include "driver-ops.h"

struct rate_control_ref {
	struct ieee80211_local *local;
	const struct rate_control_ops *ops;
	void *priv;
};

void rate_control_get_rate(struct ieee80211_sub_if_data *sdata,
			   struct sta_info *sta,
			   struct ieee80211_tx_rate_control *txrc);

static inline void rate_control_tx_status(struct ieee80211_local *local,
					  struct ieee80211_supported_band *sband,
					  struct sta_info *sta,
					  struct sk_buff *skb)
{
	struct rate_control_ref *ref = local->rate_ctrl;
	struct ieee80211_sta *ista = &sta->sta;
	void *priv_sta = sta->rate_ctrl_priv;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	if (!ref || !test_sta_flag(sta, WLAN_STA_RATE_CONTROL))
		return;

	spin_lock_bh(&sta->rate_ctrl_lock);
	if (ref->ops->tx_status)
		ref->ops->tx_status(ref->priv, sband, ista, priv_sta, skb);
	else
		ref->ops->tx_status_noskb(ref->priv, sband, ista, priv_sta, info);
	spin_unlock_bh(&sta->rate_ctrl_lock);
}

static inline void
rate_control_tx_status_noskb(struct ieee80211_local *local,
			     struct ieee80211_supported_band *sband,
			     struct sta_info *sta,
			     struct ieee80211_tx_info *info)
{
	struct rate_control_ref *ref = local->rate_ctrl;
	struct ieee80211_sta *ista = &sta->sta;
	void *priv_sta = sta->rate_ctrl_priv;

	if (!ref || !test_sta_flag(sta, WLAN_STA_RATE_CONTROL))
		return;

	if (WARN_ON_ONCE(!ref->ops->tx_status_noskb))
		return;

	spin_lock_bh(&sta->rate_ctrl_lock);
	ref->ops->tx_status_noskb(ref->priv, sband, ista, priv_sta, info);
	spin_unlock_bh(&sta->rate_ctrl_lock);
}

void rate_control_rate_init(struct sta_info *sta);
void rate_control_rate_update(struct ieee80211_local *local,
				    struct ieee80211_supported_band *sband,
				    struct sta_info *sta, u32 changed);

static inline void *rate_control_alloc_sta(struct rate_control_ref *ref,
					   struct sta_info *sta, gfp_t gfp)
{
	spin_lock_init(&sta->rate_ctrl_lock);
	return ref->ops->alloc_sta(ref->priv, &sta->sta, gfp);
}

static inline void rate_control_free_sta(struct sta_info *sta)
{
	struct rate_control_ref *ref = sta->rate_ctrl;
	struct ieee80211_sta *ista = &sta->sta;
	void *priv_sta = sta->rate_ctrl_priv;

	ref->ops->free_sta(ref->priv, ista, priv_sta);
}

static inline void rate_control_add_sta_debugfs(struct sta_info *sta)
{
#ifdef CONFIG_MAC80211_DEBUGFS
	struct rate_control_ref *ref = sta->rate_ctrl;
	if (sta->debugfs.dir && ref->ops->add_sta_debugfs)
	if (ref && sta->debugfs.dir && ref->ops->add_sta_debugfs)
		ref->ops->add_sta_debugfs(ref->priv, sta->rate_ctrl_priv,
					  sta->debugfs.dir);
#endif
}

static inline void rate_control_remove_sta_debugfs(struct sta_info *sta)
{
#ifdef CONFIG_MAC80211_DEBUGFS
	struct rate_control_ref *ref = sta->rate_ctrl;
	if (ref->ops->remove_sta_debugfs)
	if (ref && ref->ops->remove_sta_debugfs)
		ref->ops->remove_sta_debugfs(ref->priv, sta->rate_ctrl_priv);
#endif
}

static inline int rate_supported(struct sta_info *sta,
				 enum ieee80211_band band,
				 int index)
{
	return (sta == NULL || sta->supp_rates[band] & BIT(index));
}

static inline s8
rate_lowest_index(struct ieee80211_local *local,
		  struct ieee80211_supported_band *sband,
		  struct sta_info *sta)
{
	int i;

	for (i = 0; i < sband->n_bitrates; i++)
		if (rate_supported(sta, sband->band, i))
			return i;

	/* warn when we cannot find a rate. */
	WARN_ON(1);

	return 0;
}


/* functions for rate control related to a device */
/* Get a reference to the rate control algorithm. If `name' is NULL, get the
 * first available algorithm. */
int ieee80211_init_rate_ctrl_alg(struct ieee80211_local *local,
				 const char *name);
void rate_control_deinitialize(struct ieee80211_local *local);


/* Rate control algorithms */
#ifdef CONFIG_MAC80211_RC_PID
extern int rc80211_pid_init(void);
extern void rc80211_pid_exit(void);
#else
static inline int rc80211_pid_init(void)
{
	return 0;
}
static inline void rc80211_pid_exit(void)
#ifdef CONFIG_MAC80211_RC_MINSTREL
int rc80211_minstrel_init(void);
void rc80211_minstrel_exit(void);
#else
static inline int rc80211_minstrel_init(void)
{
	return 0;
}
static inline void rc80211_minstrel_exit(void)
{
}
#endif

#ifdef CONFIG_MAC80211_RC_MINSTREL_HT
int rc80211_minstrel_ht_init(void);
void rc80211_minstrel_ht_exit(void);
#else
static inline int rc80211_minstrel_ht_init(void)
{
	return 0;
}
static inline void rc80211_minstrel_ht_exit(void)
{
}
#endif


#endif /* IEEE80211_RATE_H */
