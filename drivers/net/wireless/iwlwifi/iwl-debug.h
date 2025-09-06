/******************************************************************************
 *
 * Copyright(c) 2003 - 2008 Intel Corporation. All rights reserved.
 * Copyright(c) 2003 - 2014 Intel Corporation. All rights reserved.
 *
 * Portions of this file are derived from the ipw3945 project.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * James P. Ketrenos <ipw2100-admin@linux.intel.com>
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#ifndef __iwl_debug_h__
#define __iwl_debug_h__

#ifdef CONFIG_IWLWIFI_DEBUG
#define IWL_DEBUG(level, fmt, args...) \
do { if (priv->debug_level & (level)) \
  dev_printk(KERN_ERR, &(priv->hw->wiphy->dev), "%c %s " fmt, \
	 in_interrupt() ? 'I' : 'U', __func__ , ## args); } while (0)

#define IWL_DEBUG_LIMIT(level, fmt, args...) \
do { if ((priv->debug_level & (level)) && net_ratelimit()) \
  dev_printk(KERN_ERR, &(priv->hw->wiphy->dev), "%c %s " fmt, \
	 in_interrupt() ? 'I' : 'U', __func__ , ## args); } while (0)

#ifdef CONFIG_IWLWIFI_DEBUGFS
struct iwl_debugfs {
	const char *name;
	struct dentry *dir_drv;
	struct dentry *dir_data;
	struct dentry *dir_rf;
	struct dir_data_files {
		struct dentry *file_sram;
		struct dentry *file_eeprom;
		struct dentry *file_stations;
		struct dentry *file_rx_statistics;
		struct dentry *file_tx_statistics;
		struct dentry *file_log_event;
	} dbgfs_data_files;
	struct dir_rf_files {
		struct dentry *file_disable_sensitivity;
		struct dentry *file_disable_chain_noise;
		struct dentry *file_disable_tx_power;
	} dbgfs_rf_files;
	u32 sram_offset;
	u32 sram_len;
};

int iwl_dbgfs_register(struct iwl_priv *priv, const char *name);
void iwl_dbgfs_unregister(struct iwl_priv *priv);
#endif

#else
#define IWL_DEBUG(level, fmt, args...)
#define IWL_DEBUG_LIMIT(level, fmt, args...)
#endif				/* CONFIG_IWLWIFI_DEBUG */



#ifndef CONFIG_IWLWIFI_DEBUGFS
static inline int iwl_dbgfs_register(struct iwl_priv *priv, const char *name)
{
	return 0;
}
static inline void iwl_dbgfs_unregister(struct iwl_priv *priv)
{
}
#endif				/* CONFIG_IWLWIFI_DEBUGFS */

/*
 * To use the debug system;
 *
 * If you are defining a new debug classification, simply add it to the #define
 * list here in the form of:
 *
 * #define IWL_DL_xxxx VALUE
 *
 * shifting value to the left one bit from the previous entry.  xxxx should be
 * the name of the classification (for example, WEP)
#include "iwl-modparams.h"


static inline bool iwl_have_debug_level(u32 level)
{
#ifdef CONFIG_IWLWIFI_DEBUG
	return iwlwifi_mod_params.debug_level & level;
#else
	return false;
#endif
}

void __iwl_err(struct device *dev, bool rfkill_prefix, bool only_trace,
		const char *fmt, ...) __printf(4, 5);
void __iwl_warn(struct device *dev, const char *fmt, ...) __printf(2, 3);
void __iwl_info(struct device *dev, const char *fmt, ...) __printf(2, 3);
void __iwl_crit(struct device *dev, const char *fmt, ...) __printf(2, 3);

/* not all compilers can evaluate strlen() at compile time, so use sizeof() */
#define CHECK_FOR_NEWLINE(f) BUILD_BUG_ON(f[sizeof(f) - 2] != '\n')

/* No matter what is m (priv, bus, trans), this will work */
#define IWL_ERR_DEV(d, f, a...)						\
	do {								\
		CHECK_FOR_NEWLINE(f);					\
		__iwl_err((d), false, false, f, ## a);			\
	} while (0)
#define IWL_ERR(m, f, a...)						\
	IWL_ERR_DEV((m)->dev, f, ## a)
#define IWL_WARN(m, f, a...)						\
	do {								\
		CHECK_FOR_NEWLINE(f);					\
		__iwl_warn((m)->dev, f, ## a);				\
	} while (0)
#define IWL_INFO(m, f, a...)						\
	do {								\
		CHECK_FOR_NEWLINE(f);					\
		__iwl_info((m)->dev, f, ## a);				\
	} while (0)
#define IWL_CRIT(m, f, a...)						\
	do {								\
		CHECK_FOR_NEWLINE(f);					\
		__iwl_crit((m)->dev, f, ## a);				\
	} while (0)

#if defined(CONFIG_IWLWIFI_DEBUG) || defined(CONFIG_IWLWIFI_DEVICE_TRACING)
void __iwl_dbg(struct device *dev,
	       u32 level, bool limit, const char *function,
	       const char *fmt, ...) __printf(5, 6);
#else
__printf(5, 6) static inline void
__iwl_dbg(struct device *dev,
	  u32 level, bool limit, const char *function,
	  const char *fmt, ...)
{}
#endif

#define iwl_print_hex_error(m, p, len)					\
do {									\
	print_hex_dump(KERN_ERR, "iwl data: ",				\
		       DUMP_PREFIX_OFFSET, 16, 1, p, len, 1);		\
} while (0)

#define __IWL_DEBUG_DEV(dev, level, limit, fmt, args...)		\
	do {								\
		CHECK_FOR_NEWLINE(fmt);					\
		__iwl_dbg(dev, level, limit, __func__, fmt, ##args);	\
	} while (0)
#define IWL_DEBUG(m, level, fmt, args...)				\
	__IWL_DEBUG_DEV((m)->dev, level, false, fmt, ##args)
#define IWL_DEBUG_DEV(dev, level, fmt, args...)				\
	__IWL_DEBUG_DEV(dev, level, false, fmt, ##args)
#define IWL_DEBUG_LIMIT(m, level, fmt, args...)				\
	__IWL_DEBUG_DEV((m)->dev, level, true, fmt, ##args)

#ifdef CONFIG_IWLWIFI_DEBUG
#define iwl_print_hex_dump(m, level, p, len)				\
do {                                            			\
	if (iwl_have_debug_level(level))				\
		print_hex_dump(KERN_DEBUG, "iwl data: ",		\
			       DUMP_PREFIX_OFFSET, 16, 1, p, len, 1);	\
} while (0)
#else
#define iwl_print_hex_dump(m, level, p, len)
#endif				/* CONFIG_IWLWIFI_DEBUG */

/*
 * To use the debug system:
 *
 * If you are defining a new debug classification, simply add it to the #define
 * list here in the form of
 *
 * #define IWL_DL_xxxx VALUE
 *
 * where xxxx should be the name of the classification (for example, WEP).
 *
 * You then need to either add a IWL_xxxx_DEBUG() macro definition for your
 * classification, or use IWL_DEBUG(IWL_DL_xxxx, ...) whenever you want
 * to send output to that classification.
 *
 * To add your debug level to the list of levels seen when you perform
 *
 * % cat /proc/net/iwl/debug_level
 *
 * you simply need to add your entry to the iwl_debug_levels array.
 *
 * If you do not see debug_level in /proc/net/iwl then you do not have
 * CONFIG_IWLWIFI_DEBUG defined in your kernel configuration
 *
 */

#define IWL_DL_INFO          (1 << 0)
#define IWL_DL_MAC80211      (1 << 1)
#define IWL_DL_HOST_COMMAND  (1 << 2)
#define IWL_DL_STATE         (1 << 3)
#define IWL_DL_MACDUMP		(1 << 4)
#define IWL_DL_RADIO         (1 << 7)
#define IWL_DL_POWER         (1 << 8)
#define IWL_DL_TEMP          (1 << 9)

#define IWL_DL_NOTIF         (1 << 10)
#define IWL_DL_SCAN          (1 << 11)
#define IWL_DL_ASSOC         (1 << 12)
#define IWL_DL_DROP          (1 << 13)

#define IWL_DL_TXPOWER       (1 << 14)

#define IWL_DL_AP            (1 << 15)

#define IWL_DL_FW            (1 << 16)
#define IWL_DL_RF_KILL       (1 << 17)
#define IWL_DL_FW_ERRORS     (1 << 18)

#define IWL_DL_LED           (1 << 19)

#define IWL_DL_RATE          (1 << 20)

#define IWL_DL_CALIB         (1 << 21)
#define IWL_DL_WEP           (1 << 22)
#define IWL_DL_TX            (1 << 23)
#define IWL_DL_RX            (1 << 24)
#define IWL_DL_ISR           (1 << 25)
#define IWL_DL_HT            (1 << 26)
#define IWL_DL_IO            (1 << 27)
#define IWL_DL_11H           (1 << 28)

#define IWL_DL_STATS         (1 << 29)
#define IWL_DL_TX_REPLY      (1 << 30)
#define IWL_DL_QOS           (1 << 31)

#define IWL_ERROR(f, a...) printk(KERN_ERR DRV_NAME ": " f, ## a)
#define IWL_WARNING(f, a...) printk(KERN_WARNING DRV_NAME ": " f, ## a)
#define IWL_DEBUG_INFO(f, a...)    IWL_DEBUG(IWL_DL_INFO, f, ## a)

#define IWL_DEBUG_MAC80211(f, a...)     IWL_DEBUG(IWL_DL_MAC80211, f, ## a)
#define IWL_DEBUG_MACDUMP(f, a...)     IWL_DEBUG(IWL_DL_MACDUMP, f, ## a)
#define IWL_DEBUG_TEMP(f, a...)   IWL_DEBUG(IWL_DL_TEMP, f, ## a)
#define IWL_DEBUG_SCAN(f, a...)   IWL_DEBUG(IWL_DL_SCAN, f, ## a)
#define IWL_DEBUG_RX(f, a...)     IWL_DEBUG(IWL_DL_RX, f, ## a)
#define IWL_DEBUG_TX(f, a...)     IWL_DEBUG(IWL_DL_TX, f, ## a)
#define IWL_DEBUG_ISR(f, a...)    IWL_DEBUG(IWL_DL_ISR, f, ## a)
#define IWL_DEBUG_LED(f, a...) IWL_DEBUG(IWL_DL_LED, f, ## a)
#define IWL_DEBUG_WEP(f, a...)    IWL_DEBUG(IWL_DL_WEP, f, ## a)
#define IWL_DEBUG_HC(f, a...) IWL_DEBUG(IWL_DL_HOST_COMMAND, f, ## a)
#define IWL_DEBUG_CALIB(f, a...) IWL_DEBUG(IWL_DL_CALIB, f, ## a)
#define IWL_DEBUG_FW(f, a...) IWL_DEBUG(IWL_DL_FW, f, ## a)
#define IWL_DEBUG_RF_KILL(f, a...) IWL_DEBUG(IWL_DL_RF_KILL, f, ## a)
#define IWL_DEBUG_DROP(f, a...) IWL_DEBUG(IWL_DL_DROP, f, ## a)
#define IWL_DEBUG_DROP_LIMIT(f, a...) IWL_DEBUG_LIMIT(IWL_DL_DROP, f, ## a)
#define IWL_DEBUG_AP(f, a...) IWL_DEBUG(IWL_DL_AP, f, ## a)
#define IWL_DEBUG_TXPOWER(f, a...) IWL_DEBUG(IWL_DL_TXPOWER, f, ## a)
#define IWL_DEBUG_IO(f, a...) IWL_DEBUG(IWL_DL_IO, f, ## a)
#define IWL_DEBUG_RATE(f, a...) IWL_DEBUG(IWL_DL_RATE, f, ## a)
#define IWL_DEBUG_RATE_LIMIT(f, a...) IWL_DEBUG_LIMIT(IWL_DL_RATE, f, ## a)
#define IWL_DEBUG_NOTIF(f, a...) IWL_DEBUG(IWL_DL_NOTIF, f, ## a)
#define IWL_DEBUG_ASSOC(f, a...) IWL_DEBUG(IWL_DL_ASSOC | IWL_DL_INFO, f, ## a)
#define IWL_DEBUG_ASSOC_LIMIT(f, a...) \
	IWL_DEBUG_LIMIT(IWL_DL_ASSOC | IWL_DL_INFO, f, ## a)
#define IWL_DEBUG_HT(f, a...) IWL_DEBUG(IWL_DL_HT, f, ## a)
#define IWL_DEBUG_STATS(f, a...) IWL_DEBUG(IWL_DL_STATS, f, ## a)
#define IWL_DEBUG_STATS_LIMIT(f, a...) IWL_DEBUG_LIMIT(IWL_DL_STATS, f, ## a)
#define IWL_DEBUG_TX_REPLY(f, a...) IWL_DEBUG(IWL_DL_TX_REPLY, f, ## a)
#define IWL_DEBUG_QOS(f, a...)   IWL_DEBUG(IWL_DL_QOS, f, ## a)
#define IWL_DEBUG_RADIO(f, a...)  IWL_DEBUG(IWL_DL_RADIO, f, ## a)
#define IWL_DEBUG_POWER(f, a...)  IWL_DEBUG(IWL_DL_POWER, f, ## a)
#define IWL_DEBUG_11H(f, a...)  IWL_DEBUG(IWL_DL_11H, f, ## a)
 * The active debug levels can be accessed via files
 *
 *	/sys/module/iwlwifi/parameters/debug
 * when CONFIG_IWLWIFI_DEBUG=y.
 *
 *	/sys/kernel/debug/phy0/iwlwifi/debug/debug_level
 * when CONFIG_IWLWIFI_DEBUGFS=y.
 *
 */

/* 0x0000000F - 0x00000001 */
#define IWL_DL_INFO		0x00000001
#define IWL_DL_MAC80211		0x00000002
#define IWL_DL_HCMD		0x00000004
#define IWL_DL_TDLS		0x00000008
/* 0x000000F0 - 0x00000010 */
#define IWL_DL_QUOTA		0x00000010
#define IWL_DL_TE		0x00000020
#define IWL_DL_EEPROM		0x00000040
#define IWL_DL_RADIO		0x00000080
/* 0x00000F00 - 0x00000100 */
#define IWL_DL_POWER		0x00000100
#define IWL_DL_TEMP		0x00000200
#define IWL_DL_RPM		0x00000400
#define IWL_DL_SCAN		0x00000800
/* 0x0000F000 - 0x00001000 */
#define IWL_DL_ASSOC		0x00001000
#define IWL_DL_DROP		0x00002000
#define IWL_DL_LAR		0x00004000
#define IWL_DL_COEX		0x00008000
/* 0x000F0000 - 0x00010000 */
#define IWL_DL_FW		0x00010000
#define IWL_DL_RF_KILL		0x00020000
#define IWL_DL_FW_ERRORS	0x00040000
#define IWL_DL_LED		0x00080000
/* 0x00F00000 - 0x00100000 */
#define IWL_DL_RATE		0x00100000
#define IWL_DL_CALIB		0x00200000
#define IWL_DL_WEP		0x00400000
#define IWL_DL_TX		0x00800000
/* 0x0F000000 - 0x01000000 */
#define IWL_DL_RX		0x01000000
#define IWL_DL_ISR		0x02000000
#define IWL_DL_HT		0x04000000
#define IWL_DL_EXTERNAL		0x08000000
/* 0xF0000000 - 0x10000000 */
#define IWL_DL_11H		0x10000000
#define IWL_DL_STATS		0x20000000
#define IWL_DL_TX_REPLY		0x40000000
#define IWL_DL_TX_QUEUES	0x80000000

#define IWL_DEBUG_INFO(p, f, a...)	IWL_DEBUG(p, IWL_DL_INFO, f, ## a)
#define IWL_DEBUG_TDLS(p, f, a...)	IWL_DEBUG(p, IWL_DL_TDLS, f, ## a)
#define IWL_DEBUG_MAC80211(p, f, a...)	IWL_DEBUG(p, IWL_DL_MAC80211, f, ## a)
#define IWL_DEBUG_EXTERNAL(p, f, a...)	IWL_DEBUG(p, IWL_DL_EXTERNAL, f, ## a)
#define IWL_DEBUG_TEMP(p, f, a...)	IWL_DEBUG(p, IWL_DL_TEMP, f, ## a)
#define IWL_DEBUG_SCAN(p, f, a...)	IWL_DEBUG(p, IWL_DL_SCAN, f, ## a)
#define IWL_DEBUG_RX(p, f, a...)	IWL_DEBUG(p, IWL_DL_RX, f, ## a)
#define IWL_DEBUG_TX(p, f, a...)	IWL_DEBUG(p, IWL_DL_TX, f, ## a)
#define IWL_DEBUG_ISR(p, f, a...)	IWL_DEBUG(p, IWL_DL_ISR, f, ## a)
#define IWL_DEBUG_LED(p, f, a...)	IWL_DEBUG(p, IWL_DL_LED, f, ## a)
#define IWL_DEBUG_WEP(p, f, a...)	IWL_DEBUG(p, IWL_DL_WEP, f, ## a)
#define IWL_DEBUG_HC(p, f, a...)	IWL_DEBUG(p, IWL_DL_HCMD, f, ## a)
#define IWL_DEBUG_QUOTA(p, f, a...)	IWL_DEBUG(p, IWL_DL_QUOTA, f, ## a)
#define IWL_DEBUG_TE(p, f, a...)	IWL_DEBUG(p, IWL_DL_TE, f, ## a)
#define IWL_DEBUG_EEPROM(d, f, a...)	IWL_DEBUG_DEV(d, IWL_DL_EEPROM, f, ## a)
#define IWL_DEBUG_CALIB(p, f, a...)	IWL_DEBUG(p, IWL_DL_CALIB, f, ## a)
#define IWL_DEBUG_FW(p, f, a...)	IWL_DEBUG(p, IWL_DL_FW, f, ## a)
#define IWL_DEBUG_RF_KILL(p, f, a...)	IWL_DEBUG(p, IWL_DL_RF_KILL, f, ## a)
#define IWL_DEBUG_FW_ERRORS(p, f, a...)	IWL_DEBUG(p, IWL_DL_FW_ERRORS, f, ## a)
#define IWL_DEBUG_DROP(p, f, a...)	IWL_DEBUG(p, IWL_DL_DROP, f, ## a)
#define IWL_DEBUG_DROP_LIMIT(p, f, a...)	\
		IWL_DEBUG_LIMIT(p, IWL_DL_DROP, f, ## a)
#define IWL_DEBUG_COEX(p, f, a...)	IWL_DEBUG(p, IWL_DL_COEX, f, ## a)
#define IWL_DEBUG_RATE(p, f, a...)	IWL_DEBUG(p, IWL_DL_RATE, f, ## a)
#define IWL_DEBUG_RATE_LIMIT(p, f, a...)	\
		IWL_DEBUG_LIMIT(p, IWL_DL_RATE, f, ## a)
#define IWL_DEBUG_ASSOC(p, f, a...)	\
		IWL_DEBUG(p, IWL_DL_ASSOC | IWL_DL_INFO, f, ## a)
#define IWL_DEBUG_ASSOC_LIMIT(p, f, a...)	\
		IWL_DEBUG_LIMIT(p, IWL_DL_ASSOC | IWL_DL_INFO, f, ## a)
#define IWL_DEBUG_HT(p, f, a...)	IWL_DEBUG(p, IWL_DL_HT, f, ## a)
#define IWL_DEBUG_STATS(p, f, a...)	IWL_DEBUG(p, IWL_DL_STATS, f, ## a)
#define IWL_DEBUG_STATS_LIMIT(p, f, a...)	\
		IWL_DEBUG_LIMIT(p, IWL_DL_STATS, f, ## a)
#define IWL_DEBUG_TX_REPLY(p, f, a...)	IWL_DEBUG(p, IWL_DL_TX_REPLY, f, ## a)
#define IWL_DEBUG_TX_QUEUES(p, f, a...)	IWL_DEBUG(p, IWL_DL_TX_QUEUES, f, ## a)
#define IWL_DEBUG_RADIO(p, f, a...)	IWL_DEBUG(p, IWL_DL_RADIO, f, ## a)
#define IWL_DEBUG_POWER(p, f, a...)	IWL_DEBUG(p, IWL_DL_POWER, f, ## a)
#define IWL_DEBUG_11H(p, f, a...)	IWL_DEBUG(p, IWL_DL_11H, f, ## a)
#define IWL_DEBUG_RPM(p, f, a...)	IWL_DEBUG(p, IWL_DL_RPM, f, ## a)
#define IWL_DEBUG_LAR(p, f, a...)	IWL_DEBUG(p, IWL_DL_LAR, f, ## a)

#endif
