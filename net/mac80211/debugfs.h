#ifndef __MAC80211_DEBUGFS_H
#define __MAC80211_DEBUGFS_H

#ifdef CONFIG_MAC80211_DEBUGFS
extern void debugfs_hw_add(struct ieee80211_local *local);
extern void debugfs_hw_del(struct ieee80211_local *local);
extern int mac80211_open_file_generic(struct inode *inode, struct file *file);
#else
static inline void debugfs_hw_add(struct ieee80211_local *local)
{
	return;
}
static inline void debugfs_hw_del(struct ieee80211_local *local) {}
#include "ieee80211_i.h"

#ifdef CONFIG_MAC80211_DEBUGFS
void debugfs_hw_add(struct ieee80211_local *local);
int __printf(4, 5) mac80211_format_buffer(char __user *userbuf, size_t count,
					  loff_t *ppos, char *fmt, ...);
#else
static inline void debugfs_hw_add(struct ieee80211_local *local)
{
}
#endif

#endif /* __MAC80211_DEBUGFS_H */
