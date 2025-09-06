/*
 * Copyright 2003-2005	Devicescape Software, Inc.
 * Copyright (c) 2006	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007	Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2013-2014  Intel Mobile Communications GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/debugfs.h>
#include <linux/ieee80211.h>
#include "ieee80211_i.h"
#include "debugfs.h"
#include "debugfs_sta.h"
#include "sta_info.h"

/* sta attributtes */

#define STA_READ(name, buflen, field, format_string)			\
#include "driver-ops.h"

/* sta attributtes */

#define STA_READ(name, field, format_string)				\
static ssize_t sta_ ##name## _read(struct file *file,			\
				   char __user *userbuf,		\
				   size_t count, loff_t *ppos)		\
{									\
	int res;							\
	struct sta_info *sta = file->private_data;			\
	char buf[buflen];						\
	res = scnprintf(buf, buflen, format_string, sta->field);	\
	return simple_read_from_buffer(userbuf, count, ppos, buf, res);	\
}
#define STA_READ_D(name, field) STA_READ(name, 20, field, "%d\n")
#define STA_READ_U(name, field) STA_READ(name, 20, field, "%u\n")
#define STA_READ_LU(name, field) STA_READ(name, 20, field, "%lu\n")
#define STA_READ_S(name, field) STA_READ(name, 20, field, "%s\n")
	struct sta_info *sta = file->private_data;			\
	return mac80211_format_buffer(userbuf, count, ppos, 		\
				      format_string, sta->field);	\
}
#define STA_READ_D(name, field) STA_READ(name, field, "%d\n")

#define STA_OPS(name)							\
static const struct file_operations sta_ ##name## _ops = {		\
	.read = sta_##name##_read,					\
	.open = mac80211_open_file_generic,				\
}

#define STA_OPS_WR(name)						\
static const struct file_operations sta_ ##name## _ops = {		\
	.read = sta_##name##_read,					\
	.write = sta_##name##_write,					\
	.open = mac80211_open_file_generic,				\
	.open = simple_open,						\
	.llseek = generic_file_llseek,					\
}

#define STA_OPS_RW(name)						\
static const struct file_operations sta_ ##name## _ops = {		\
	.read = sta_##name##_read,					\
	.write = sta_##name##_write,					\
	.open = simple_open,						\
	.llseek = generic_file_llseek,					\
}

#define STA_FILE(name, field, format)					\
		STA_READ_##format(name, field)				\
		STA_OPS(name)

STA_FILE(aid, aid, D);
STA_FILE(dev, sdata->dev->name, S);
STA_FILE(rx_packets, rx_packets, LU);
STA_FILE(tx_packets, tx_packets, LU);
STA_FILE(rx_bytes, rx_bytes, LU);
STA_FILE(tx_bytes, tx_bytes, LU);
STA_FILE(rx_duplicates, num_duplicates, LU);
STA_FILE(rx_fragments, rx_fragments, LU);
STA_FILE(rx_dropped, rx_dropped, LU);
STA_FILE(tx_fragments, tx_fragments, LU);
STA_FILE(tx_filtered, tx_filtered_count, LU);
STA_FILE(tx_retry_failed, tx_retry_failed, LU);
STA_FILE(tx_retry_count, tx_retry_count, LU);
STA_FILE(last_signal, last_signal, D);
STA_FILE(last_qual, last_qual, D);
STA_FILE(last_noise, last_noise, D);
STA_FILE(wep_weak_iv_count, wep_weak_iv_count, LU);
STA_FILE(aid, sta.aid, D);

static ssize_t sta_flags_read(struct file *file, char __user *userbuf,
			      size_t count, loff_t *ppos)
{
	char buf[100];
	struct sta_info *sta = file->private_data;
	u32 staflags = get_sta_flags(sta);
	int res = scnprintf(buf, sizeof(buf), "%s%s%s%s%s%s%s",
		staflags & WLAN_STA_AUTH ? "AUTH\n" : "",
		staflags & WLAN_STA_ASSOC ? "ASSOC\n" : "",
		staflags & WLAN_STA_PS ? "PS\n" : "",
		staflags & WLAN_STA_AUTHORIZED ? "AUTHORIZED\n" : "",
		staflags & WLAN_STA_SHORT_PREAMBLE ? "SHORT PREAMBLE\n" : "",
		staflags & WLAN_STA_WME ? "WME\n" : "",
		staflags & WLAN_STA_WDS ? "WDS\n" : "");
	char buf[121];
	struct sta_info *sta = file->private_data;

#define TEST(flg) \
	test_sta_flag(sta, WLAN_STA_##flg) ? #flg "\n" : ""

	int res = scnprintf(buf, sizeof(buf),
			    "%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s",
			    TEST(AUTH), TEST(ASSOC), TEST(PS_STA),
			    TEST(PS_DRIVER), TEST(AUTHORIZED),
			    TEST(SHORT_PREAMBLE),
			    sta->sta.wme ? "WME\n" : "",
			    TEST(WDS), TEST(CLEAR_PS_FILT),
			    TEST(MFP), TEST(BLOCK_BA), TEST(PSPOLL),
			    TEST(UAPSD), TEST(SP), TEST(TDLS_PEER),
			    TEST(TDLS_PEER_AUTH), TEST(TDLS_INITIATOR),
			    TEST(TDLS_CHAN_SWITCH), TEST(TDLS_OFF_CHANNEL),
			    TEST(4ADDR_EVENT), TEST(INSERTED),
			    TEST(RATE_CONTROL), TEST(TOFFSET_KNOWN),
			    TEST(MPSP_OWNER), TEST(MPSP_RECIPIENT));
#undef TEST
	return simple_read_from_buffer(userbuf, count, ppos, buf, res);
}
STA_OPS(flags);

static ssize_t sta_num_ps_buf_frames_read(struct file *file,
					  char __user *userbuf,
					  size_t count, loff_t *ppos)
{
	char buf[20];
	struct sta_info *sta = file->private_data;
	int res = scnprintf(buf, sizeof(buf), "%u\n",
			    skb_queue_len(&sta->ps_tx_buf));
	return simple_read_from_buffer(userbuf, count, ppos, buf, res);
}
STA_OPS(num_ps_buf_frames);

static ssize_t sta_inactive_ms_read(struct file *file, char __user *userbuf,
				    size_t count, loff_t *ppos)
{
	char buf[20];
	struct sta_info *sta = file->private_data;
	int res = scnprintf(buf, sizeof(buf), "%d\n",
			    jiffies_to_msecs(jiffies - sta->last_rx));
	return simple_read_from_buffer(userbuf, count, ppos, buf, res);
}
STA_OPS(inactive_ms);

static ssize_t sta_last_seq_ctrl_read(struct file *file, char __user *userbuf,
				      size_t count, loff_t *ppos)
{
	char buf[15*NUM_RX_DATA_QUEUES], *p = buf;
	int i;
	struct sta_info *sta = file->private_data;
	for (i = 0; i < NUM_RX_DATA_QUEUES; i++)
	struct sta_info *sta = file->private_data;
	char buf[17*IEEE80211_NUM_ACS], *p = buf;
	int ac;

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++)
		p += scnprintf(p, sizeof(buf)+buf-p, "AC%d: %d\n", ac,
			       skb_queue_len(&sta->ps_tx_buf[ac]) +
			       skb_queue_len(&sta->tx_filtered[ac]));
	return simple_read_from_buffer(userbuf, count, ppos, buf, p - buf);
}
STA_OPS(num_ps_buf_frames);

static ssize_t sta_last_seq_ctrl_read(struct file *file, char __user *userbuf,
				      size_t count, loff_t *ppos)
{
	char buf[15*IEEE80211_NUM_TIDS], *p = buf;
	int i;
	struct sta_info *sta = file->private_data;
	for (i = 0; i < IEEE80211_NUM_TIDS; i++)
		p += scnprintf(p, sizeof(buf)+buf-p, "%x ",
			       le16_to_cpu(sta->last_seq_ctrl[i]));
	p += scnprintf(p, sizeof(buf)+buf-p, "\n");
	return simple_read_from_buffer(userbuf, count, ppos, buf, p - buf);
}
STA_OPS(last_seq_ctrl);

static ssize_t sta_agg_status_read(struct file *file, char __user *userbuf,
					size_t count, loff_t *ppos)
{
	char buf[768], *p = buf;
	int i;
	struct sta_info *sta = file->private_data;
	p += scnprintf(p, sizeof(buf)+buf-p, "Agg state for STA is:\n");
	p += scnprintf(p, sizeof(buf)+buf-p, " STA next dialog_token is %d \n "
			"TIDs info is: \n TID :",
			(sta->ampdu_mlme.dialog_token_allocator + 1));
	for (i = 0; i < STA_TID_NUM; i++)
		p += scnprintf(p, sizeof(buf)+buf-p, "%5d", i);

	p += scnprintf(p, sizeof(buf)+buf-p, "\n RX  :");
	for (i = 0; i < STA_TID_NUM; i++)
		p += scnprintf(p, sizeof(buf)+buf-p, "%5d",
			sta->ampdu_mlme.tid_state_rx[i]);

	p += scnprintf(p, sizeof(buf)+buf-p, "\n DTKN:");
	for (i = 0; i < STA_TID_NUM; i++)
		p += scnprintf(p, sizeof(buf)+buf-p, "%5d",
			sta->ampdu_mlme.tid_state_rx[i]?
			sta->ampdu_mlme.tid_rx[i]->dialog_token : 0);

	p += scnprintf(p, sizeof(buf)+buf-p, "\n TX  :");
	for (i = 0; i < STA_TID_NUM; i++)
		p += scnprintf(p, sizeof(buf)+buf-p, "%5d",
			sta->ampdu_mlme.tid_state_tx[i]);

	p += scnprintf(p, sizeof(buf)+buf-p, "\n DTKN:");
	for (i = 0; i < STA_TID_NUM; i++)
		p += scnprintf(p, sizeof(buf)+buf-p, "%5d",
			sta->ampdu_mlme.tid_state_tx[i]?
			sta->ampdu_mlme.tid_tx[i]->dialog_token : 0);

	p += scnprintf(p, sizeof(buf)+buf-p, "\n SSN :");
	for (i = 0; i < STA_TID_NUM; i++)
		p += scnprintf(p, sizeof(buf)+buf-p, "%5d",
			sta->ampdu_mlme.tid_state_tx[i]?
			sta->ampdu_mlme.tid_tx[i]->ssn : 0);

	p += scnprintf(p, sizeof(buf)+buf-p, "\n");
	char buf[71 + IEEE80211_NUM_TIDS * 40], *p = buf;
	int i;
	struct sta_info *sta = file->private_data;
	struct tid_ampdu_rx *tid_rx;
	struct tid_ampdu_tx *tid_tx;

	rcu_read_lock();

	p += scnprintf(p, sizeof(buf) + buf - p, "next dialog_token: %#02x\n",
			sta->ampdu_mlme.dialog_token_allocator + 1);
	p += scnprintf(p, sizeof(buf) + buf - p,
		       "TID\t\tRX\tDTKN\tSSN\t\tTX\tDTKN\tpending\n");

	for (i = 0; i < IEEE80211_NUM_TIDS; i++) {
		tid_rx = rcu_dereference(sta->ampdu_mlme.tid_rx[i]);
		tid_tx = rcu_dereference(sta->ampdu_mlme.tid_tx[i]);

		p += scnprintf(p, sizeof(buf) + buf - p, "%02d", i);
		p += scnprintf(p, sizeof(buf) + buf - p, "\t\t%x", !!tid_rx);
		p += scnprintf(p, sizeof(buf) + buf - p, "\t%#.2x",
				tid_rx ? tid_rx->dialog_token : 0);
		p += scnprintf(p, sizeof(buf) + buf - p, "\t%#.3x",
				tid_rx ? tid_rx->ssn : 0);

		p += scnprintf(p, sizeof(buf) + buf - p, "\t\t%x", !!tid_tx);
		p += scnprintf(p, sizeof(buf) + buf - p, "\t%#.2x",
				tid_tx ? tid_tx->dialog_token : 0);
		p += scnprintf(p, sizeof(buf) + buf - p, "\t%03d",
				tid_tx ? skb_queue_len(&tid_tx->pending) : 0);
		p += scnprintf(p, sizeof(buf) + buf - p, "\n");
	}
	rcu_read_unlock();

	return simple_read_from_buffer(userbuf, count, ppos, buf, p - buf);
}

static ssize_t sta_agg_status_write(struct file *file,
		const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct sta_info *sta = file->private_data;
	struct net_device *dev = sta->sdata->dev;
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_hw *hw = &local->hw;
	u8 *da = sta->addr;
	static int tid_static_tx[16] = {0, 0, 0, 0, 0, 0, 0, 0,
					0, 0, 0, 0, 0, 0, 0, 0};
	static int tid_static_rx[16] = {1, 1, 1, 1, 1, 1, 1, 1,
					1, 1, 1, 1, 1, 1, 1, 1};
	char *endp;
	char buf[32];
	int buf_size, rs;
	unsigned int tid_num;
	char state[4];

	memset(buf, 0x00, sizeof(buf));
	buf_size = min(count, (sizeof(buf)-1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	tid_num = simple_strtoul(buf, &endp, 0);
	if (endp == buf)
		return -EINVAL;

	if ((tid_num >= 100) && (tid_num <= 115)) {
		/* toggle Rx aggregation command */
		tid_num = tid_num - 100;
		if (tid_static_rx[tid_num] == 1) {
			strcpy(state, "off ");
			ieee80211_sta_stop_rx_ba_session(dev, da, tid_num, 0,
					WLAN_REASON_QSTA_REQUIRE_SETUP);
			sta->ampdu_mlme.tid_state_rx[tid_num] |=
					HT_AGG_STATE_DEBUGFS_CTL;
			tid_static_rx[tid_num] = 0;
		} else {
			strcpy(state, "on ");
			sta->ampdu_mlme.tid_state_rx[tid_num] &=
					~HT_AGG_STATE_DEBUGFS_CTL;
			tid_static_rx[tid_num] = 1;
		}
		printk(KERN_DEBUG "debugfs - try switching tid %u %s\n",
				tid_num, state);
	} else if ((tid_num >= 0) && (tid_num <= 15)) {
		/* toggle Tx aggregation command */
		if (tid_static_tx[tid_num] == 0) {
			strcpy(state, "on ");
			rs =  ieee80211_start_tx_ba_session(hw, da, tid_num);
			if (rs == 0)
				tid_static_tx[tid_num] = 1;
		} else {
			strcpy(state, "off");
			rs =  ieee80211_stop_tx_ba_session(hw, da, tid_num, 1);
			if (rs == 0)
				tid_static_tx[tid_num] = 0;
		}
		printk(KERN_DEBUG "debugfs - switching tid %u %s, return=%d\n",
				tid_num, state, rs);
	}

	return count;
}
STA_OPS_WR(agg_status);

#define DEBUGFS_ADD(name) \
	sta->debugfs.name = debugfs_create_file(#name, 0400, \
		sta->debugfs.dir, sta, &sta_ ##name## _ops);

#define DEBUGFS_DEL(name) \
	debugfs_remove(sta->debugfs.name);\
	sta->debugfs.name = NULL;


void ieee80211_sta_debugfs_add(struct sta_info *sta)
{
	struct dentry *stations_dir = sta->local->debugfs.stations;
	DECLARE_MAC_BUF(mbuf);
	u8 *mac;
static ssize_t sta_agg_status_write(struct file *file, const char __user *userbuf,
				    size_t count, loff_t *ppos)
{
	char _buf[12] = {}, *buf = _buf;
	struct sta_info *sta = file->private_data;
	bool start, tx;
	unsigned long tid;
	int ret;

	if (count > sizeof(_buf))
		return -EINVAL;

	if (copy_from_user(buf, userbuf, count))
		return -EFAULT;

	buf[sizeof(_buf) - 1] = '\0';

	if (strncmp(buf, "tx ", 3) == 0) {
		buf += 3;
		tx = true;
	} else if (strncmp(buf, "rx ", 3) == 0) {
		buf += 3;
		tx = false;
	} else
		return -EINVAL;

	if (strncmp(buf, "start ", 6) == 0) {
		buf += 6;
		start = true;
		if (!tx)
			return -EINVAL;
	} else if (strncmp(buf, "stop ", 5) == 0) {
		buf += 5;
		start = false;
	} else
		return -EINVAL;

	ret = kstrtoul(buf, 0, &tid);
	if (ret)
		return ret;

	if (tid >= IEEE80211_NUM_TIDS)
		return -EINVAL;

	if (tx) {
		if (start)
			ret = ieee80211_start_tx_ba_session(&sta->sta, tid, 5000);
		else
			ret = ieee80211_stop_tx_ba_session(&sta->sta, tid);
	} else {
		__ieee80211_stop_rx_ba_session(sta, tid, WLAN_BACK_RECIPIENT,
					       3, true);
		ret = 0;
	}

	return ret ?: count;
}
STA_OPS_RW(agg_status);

static ssize_t sta_ht_capa_read(struct file *file, char __user *userbuf,
				size_t count, loff_t *ppos)
{
#define PRINT_HT_CAP(_cond, _str) \
	do { \
	if (_cond) \
			p += scnprintf(p, sizeof(buf)+buf-p, "\t" _str "\n"); \
	} while (0)
	char buf[512], *p = buf;
	int i;
	struct sta_info *sta = file->private_data;
	struct ieee80211_sta_ht_cap *htc = &sta->sta.ht_cap;

	p += scnprintf(p, sizeof(buf) + buf - p, "ht %ssupported\n",
			htc->ht_supported ? "" : "not ");
	if (htc->ht_supported) {
		p += scnprintf(p, sizeof(buf)+buf-p, "cap: %#.4x\n", htc->cap);

		PRINT_HT_CAP((htc->cap & BIT(0)), "RX LDPC");
		PRINT_HT_CAP((htc->cap & BIT(1)), "HT20/HT40");
		PRINT_HT_CAP(!(htc->cap & BIT(1)), "HT20");

		PRINT_HT_CAP(((htc->cap >> 2) & 0x3) == 0, "Static SM Power Save");
		PRINT_HT_CAP(((htc->cap >> 2) & 0x3) == 1, "Dynamic SM Power Save");
		PRINT_HT_CAP(((htc->cap >> 2) & 0x3) == 3, "SM Power Save disabled");

		PRINT_HT_CAP((htc->cap & BIT(4)), "RX Greenfield");
		PRINT_HT_CAP((htc->cap & BIT(5)), "RX HT20 SGI");
		PRINT_HT_CAP((htc->cap & BIT(6)), "RX HT40 SGI");
		PRINT_HT_CAP((htc->cap & BIT(7)), "TX STBC");

		PRINT_HT_CAP(((htc->cap >> 8) & 0x3) == 0, "No RX STBC");
		PRINT_HT_CAP(((htc->cap >> 8) & 0x3) == 1, "RX STBC 1-stream");
		PRINT_HT_CAP(((htc->cap >> 8) & 0x3) == 2, "RX STBC 2-streams");
		PRINT_HT_CAP(((htc->cap >> 8) & 0x3) == 3, "RX STBC 3-streams");

		PRINT_HT_CAP((htc->cap & BIT(10)), "HT Delayed Block Ack");

		PRINT_HT_CAP(!(htc->cap & BIT(11)), "Max AMSDU length: "
			     "3839 bytes");
		PRINT_HT_CAP((htc->cap & BIT(11)), "Max AMSDU length: "
			     "7935 bytes");

		/*
		 * For beacons and probe response this would mean the BSS
		 * does or does not allow the usage of DSSS/CCK HT40.
		 * Otherwise it means the STA does or does not use
		 * DSSS/CCK HT40.
		 */
		PRINT_HT_CAP((htc->cap & BIT(12)), "DSSS/CCK HT40");
		PRINT_HT_CAP(!(htc->cap & BIT(12)), "No DSSS/CCK HT40");

		/* BIT(13) is reserved */

		PRINT_HT_CAP((htc->cap & BIT(14)), "40 MHz Intolerant");

		PRINT_HT_CAP((htc->cap & BIT(15)), "L-SIG TXOP protection");

		p += scnprintf(p, sizeof(buf)+buf-p, "ampdu factor/density: %d/%d\n",
				htc->ampdu_factor, htc->ampdu_density);
		p += scnprintf(p, sizeof(buf)+buf-p, "MCS mask:");

		for (i = 0; i < IEEE80211_HT_MCS_MASK_LEN; i++)
			p += scnprintf(p, sizeof(buf)+buf-p, " %.2x",
					htc->mcs.rx_mask[i]);
		p += scnprintf(p, sizeof(buf)+buf-p, "\n");

		/* If not set this is meaningless */
		if (le16_to_cpu(htc->mcs.rx_highest)) {
			p += scnprintf(p, sizeof(buf)+buf-p,
				       "MCS rx highest: %d Mbps\n",
				       le16_to_cpu(htc->mcs.rx_highest));
		}

		p += scnprintf(p, sizeof(buf)+buf-p, "MCS tx params: %x\n",
				htc->mcs.tx_params);
	}

	return simple_read_from_buffer(userbuf, count, ppos, buf, p - buf);
}
STA_OPS(ht_capa);

static ssize_t sta_vht_capa_read(struct file *file, char __user *userbuf,
				 size_t count, loff_t *ppos)
{
	char buf[128], *p = buf;
	struct sta_info *sta = file->private_data;
	struct ieee80211_sta_vht_cap *vhtc = &sta->sta.vht_cap;

	p += scnprintf(p, sizeof(buf) + buf - p, "VHT %ssupported\n",
			vhtc->vht_supported ? "" : "not ");
	if (vhtc->vht_supported) {
		p += scnprintf(p, sizeof(buf)+buf-p, "cap: %#.8x\n", vhtc->cap);

		p += scnprintf(p, sizeof(buf)+buf-p, "RX MCS: %.4x\n",
			       le16_to_cpu(vhtc->vht_mcs.rx_mcs_map));
		if (vhtc->vht_mcs.rx_highest)
			p += scnprintf(p, sizeof(buf)+buf-p,
				       "MCS RX highest: %d Mbps\n",
				       le16_to_cpu(vhtc->vht_mcs.rx_highest));
		p += scnprintf(p, sizeof(buf)+buf-p, "TX MCS: %.4x\n",
			       le16_to_cpu(vhtc->vht_mcs.tx_mcs_map));
		if (vhtc->vht_mcs.tx_highest)
			p += scnprintf(p, sizeof(buf)+buf-p,
				       "MCS TX highest: %d Mbps\n",
				       le16_to_cpu(vhtc->vht_mcs.tx_highest));
	}

	return simple_read_from_buffer(userbuf, count, ppos, buf, p - buf);
}
STA_OPS(vht_capa);


#define DEBUGFS_ADD(name) \
	debugfs_create_file(#name, 0400, \
		sta->debugfs.dir, sta, &sta_ ##name## _ops);

#define DEBUGFS_ADD_COUNTER(name, field)				\
	if (sizeof(sta->field) == sizeof(u32))				\
		debugfs_create_u32(#name, 0400, sta->debugfs.dir,	\
			(u32 *) &sta->field);				\
	else								\
		debugfs_create_u64(#name, 0400, sta->debugfs.dir,	\
			(u64 *) &sta->field);

void ieee80211_sta_debugfs_add(struct sta_info *sta)
{
	struct ieee80211_local *local = sta->local;
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	struct dentry *stations_dir = sta->sdata->debugfs.subdir_stations;
	u8 mac[3*ETH_ALEN];

	sta->debugfs.add_has_run = true;

	if (!stations_dir)
		return;

	mac = print_mac(mbuf, sta->addr);

	snprintf(mac, sizeof(mac), "%pM", sta->sta.addr);

	/*
	 * This might fail due to a race condition:
	 * When mac80211 unlinks a station, the debugfs entries
	 * remain, but it is already possible to link a new
	 * station with the same address which triggers adding
	 * it to debugfs; therefore, if the old station isn't
	 * destroyed quickly enough the old station's debugfs
	 * dir might still be around.
	 */
	sta->debugfs.dir = debugfs_create_dir(mac, stations_dir);
	if (!sta->debugfs.dir)
		return;

	DEBUGFS_ADD(flags);
	DEBUGFS_ADD(num_ps_buf_frames);
	DEBUGFS_ADD(inactive_ms);
	DEBUGFS_ADD(last_seq_ctrl);
	DEBUGFS_ADD(agg_status);
	DEBUGFS_ADD(last_seq_ctrl);
	DEBUGFS_ADD(agg_status);
	DEBUGFS_ADD(ht_capa);
	DEBUGFS_ADD(vht_capa);

	DEBUGFS_ADD_COUNTER(rx_duplicates, rx_stats.num_duplicates);
	DEBUGFS_ADD_COUNTER(rx_fragments, rx_stats.fragments);
	DEBUGFS_ADD_COUNTER(tx_filtered, status_stats.filtered);

	if (sizeof(sta->driver_buffered_tids) == sizeof(u32))
		debugfs_create_x32("driver_buffered_tids", 0400,
				   sta->debugfs.dir,
				   (u32 *)&sta->driver_buffered_tids);
	else
		debugfs_create_x64("driver_buffered_tids", 0400,
				   sta->debugfs.dir,
				   (u64 *)&sta->driver_buffered_tids);

	drv_sta_add_debugfs(local, sdata, &sta->sta, sta->debugfs.dir);
}

void ieee80211_sta_debugfs_remove(struct sta_info *sta)
{
	DEBUGFS_DEL(flags);
	DEBUGFS_DEL(num_ps_buf_frames);
	DEBUGFS_DEL(inactive_ms);
	DEBUGFS_DEL(last_seq_ctrl);
	DEBUGFS_DEL(agg_status);

	debugfs_remove(sta->debugfs.dir);
	struct ieee80211_local *local = sta->local;
	struct ieee80211_sub_if_data *sdata = sta->sdata;

	drv_sta_remove_debugfs(local, sdata, &sta->sta, sta->debugfs.dir);
	debugfs_remove_recursive(sta->debugfs.dir);
	sta->debugfs.dir = NULL;
}
