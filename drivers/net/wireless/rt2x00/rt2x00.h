/*
	Copyright (C) 2004 - 2008 rt2x00 SourceForge Project
	Copyright (C) 2010 Willow Garage <http://www.willowgarage.com>
	Copyright (C) 2004 - 2010 Ivo van Doorn <IvDoorn@gmail.com>
	Copyright (C) 2004 - 2009 Gertjan van Wingerde <gwingerde@gmail.com>
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
	Module: rt2x00
	Abstract: rt2x00 global information.
 */

#ifndef RT2X00_H
#define RT2X00_H

#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/skbuff.h>
#include <linux/workqueue.h>
#include <linux/firmware.h>
#include <linux/leds.h>
#include <linux/mutex.h>
#include <linux/etherdevice.h>
#include <linux/input-polldev.h>
#include <linux/kfifo.h>
#include <linux/hrtimer.h>
#include <linux/average.h>

#include <net/mac80211.h>

#include "rt2x00debug.h"
#include "rt2x00dump.h"
#include "rt2x00leds.h"
#include "rt2x00reg.h"
#include "rt2x00queue.h"

/*
 * Module information.
 */
#define DRV_VERSION	"2.1.8"
#define DRV_PROJECT	"http://rt2x00.serialmonkey.com"

/*
 * Debug definitions.
 * Debug output has to be enabled during compile time.
 */
#define DEBUG_PRINTK_MSG(__dev, __kernlvl, __lvl, __msg, __args...)	\
	printk(__kernlvl "%s -> %s: %s - " __msg,			\
	       wiphy_name((__dev)->hw->wiphy), __FUNCTION__, __lvl, ##__args)

#define DEBUG_PRINTK_PROBE(__kernlvl, __lvl, __msg, __args...)	\
	printk(__kernlvl "%s -> %s: %s - " __msg,		\
	       KBUILD_MODNAME, __FUNCTION__, __lvl, ##__args)

#ifdef CONFIG_RT2X00_DEBUG
#define DEBUG_PRINTK(__dev, __kernlvl, __lvl, __msg, __args...)	\
	DEBUG_PRINTK_MSG(__dev, __kernlvl, __lvl, __msg, ##__args);
#else
#define DEBUG_PRINTK(__dev, __kernlvl, __lvl, __msg, __args...)	\
	do { } while (0)
#endif /* CONFIG_RT2X00_DEBUG */

/*
 * Various debug levels.
 * The debug levels PANIC and ERROR both indicate serious problems,
 * for this reason they should never be ignored.
 * The special ERROR_PROBE message is for messages that are generated
 * when the rt2x00_dev is not yet initialized.
 */
#define PANIC(__dev, __msg, __args...) \
	DEBUG_PRINTK_MSG(__dev, KERN_CRIT, "Panic", __msg, ##__args)
#define ERROR(__dev, __msg, __args...)	\
	DEBUG_PRINTK_MSG(__dev, KERN_ERR, "Error", __msg, ##__args)
#define ERROR_PROBE(__msg, __args...) \
	DEBUG_PRINTK_PROBE(KERN_ERR, "Error", __msg, ##__args)
#define WARNING(__dev, __msg, __args...) \
	DEBUG_PRINTK(__dev, KERN_WARNING, "Warning", __msg, ##__args)
#define NOTICE(__dev, __msg, __args...) \
	DEBUG_PRINTK(__dev, KERN_NOTICE, "Notice", __msg, ##__args)
#define INFO(__dev, __msg, __args...) \
	DEBUG_PRINTK(__dev, KERN_INFO, "Info", __msg, ##__args)
#define DEBUG(__dev, __msg, __args...) \
	DEBUG_PRINTK(__dev, KERN_DEBUG, "Debug", __msg, ##__args)
#define EEPROM(__dev, __msg, __args...) \
	DEBUG_PRINTK(__dev, KERN_DEBUG, "EEPROM recovery", __msg, ##__args)
#define DRV_VERSION	"2.3.0"
#define DRV_PROJECT	"http://rt2x00.serialmonkey.com"

/* Debug definitions.
 * Debug output has to be enabled during compile time.
 */
#ifdef CONFIG_RT2X00_DEBUG
#define DEBUG
#endif /* CONFIG_RT2X00_DEBUG */

/* Utility printing macros
 * rt2x00_probe_err is for messages when rt2x00_dev is uninitialized
 */
#define rt2x00_probe_err(fmt, ...)					\
	printk(KERN_ERR KBUILD_MODNAME ": %s: Error - " fmt,		\
	       __func__, ##__VA_ARGS__)
#define rt2x00_err(dev, fmt, ...)					\
	wiphy_err((dev)->hw->wiphy, "%s: Error - " fmt,			\
		  __func__, ##__VA_ARGS__)
#define rt2x00_warn(dev, fmt, ...)					\
	wiphy_warn((dev)->hw->wiphy, "%s: Warning - " fmt,		\
		   __func__, ##__VA_ARGS__)
#define rt2x00_info(dev, fmt, ...)					\
	wiphy_info((dev)->hw->wiphy, "%s: Info - " fmt,			\
		   __func__, ##__VA_ARGS__)

/* Various debug levels */
#define rt2x00_dbg(dev, fmt, ...)					\
	wiphy_dbg((dev)->hw->wiphy, "%s: Debug - " fmt,			\
		  __func__, ##__VA_ARGS__)
#define rt2x00_eeprom_dbg(dev, fmt, ...)				\
	wiphy_dbg((dev)->hw->wiphy, "%s: EEPROM recovery - " fmt,	\
		  __func__, ##__VA_ARGS__)

/*
 * Duration calculations
 * The rate variable passed is: 100kbs.
 * To convert from bytes to bits we multiply size with 8,
 * then the size is multiplied with 10 to make the
 * real rate -> rate argument correction.
 */
#define GET_DURATION(__size, __rate)	(((__size) * 8 * 10) / (__rate))
#define GET_DURATION_RES(__size, __rate)(((__size) * 8 * 10) % (__rate))

/*
 * Determine the number of L2 padding bytes required between the header and
 * the payload.
 */
#define L2PAD_SIZE(__hdrlen)	(-(__hdrlen) & 3)

/*
 * Determine the alignment requirement,
 * to make sure the 802.11 payload is padded to a 4-byte boundrary
 * we must determine the address of the payload and calculate the
 * amount of bytes needed to move the data.
 */
#define ALIGN_SIZE(__skb, __header) \
	(  ((unsigned long)((__skb)->data + (__header))) & 3 )

/*
 * Constants for extra TX headroom for alignment purposes.
 */
#define RT2X00_ALIGN_SIZE	4 /* Only whole frame needs alignment */
#define RT2X00_L2PAD_SIZE	8 /* Both header & payload need alignment */

/*
 * Standard timing and size defines.
 * These values should follow the ieee80211 specifications.
 */
#define ACK_SIZE		14
#define IEEE80211_HEADER	24
#define PLCP			48
#define BEACON			100
#define PREAMBLE		144
#define SHORT_PREAMBLE		72
#define SLOT_TIME		20
#define SHORT_SLOT_TIME		9
#define SIFS			10
#define PIFS			( SIFS + SLOT_TIME )
#define SHORT_PIFS		( SIFS + SHORT_SLOT_TIME )
#define DIFS			( PIFS + SLOT_TIME )
#define SHORT_DIFS		( SHORT_PIFS + SHORT_SLOT_TIME )
#define EIFS			( SIFS + DIFS + \
				  (8 * (IEEE80211_HEADER + ACK_SIZE)) )
#define SHORT_EIFS		( SIFS + SHORT_DIFS + \
				  (8 * (IEEE80211_HEADER + ACK_SIZE)) )
				  GET_DURATION(IEEE80211_HEADER + ACK_SIZE, 10) )
#define SHORT_EIFS		( SIFS + SHORT_DIFS + \
				  GET_DURATION(IEEE80211_HEADER + ACK_SIZE, 10) )

enum rt2x00_chip_intf {
	RT2X00_CHIP_INTF_PCI,
	RT2X00_CHIP_INTF_PCIE,
	RT2X00_CHIP_INTF_USB,
	RT2X00_CHIP_INTF_SOC,
};

/*
 * Chipset identification
 * The chipset on the device is composed of a RT and RF chip.
 * The chipset combination is important for determining device capabilities.
 */
struct rt2x00_chip {
	u16 rt;
#define RT2460		0x0101
#define RT2560		0x0201
#define RT2570		0x1201
#define RT2561s		0x0301	/* Turbo */
#define RT2561		0x0302
#define RT2661		0x0401
#define RT2571		0x1300

	u16 rf;
	u32 rev;
#define RT2460		0x2460
#define RT2560		0x2560
#define RT2570		0x2570
#define RT2661		0x2661
#define RT2573		0x2573
#define RT2860		0x2860	/* 2.4GHz */
#define RT2872		0x2872	/* WSOC */
#define RT2883		0x2883	/* WSOC */
#define RT3070		0x3070
#define RT3071		0x3071
#define RT3090		0x3090	/* 2.4GHz PCIe */
#define RT3290		0x3290
#define RT3352		0x3352  /* WSOC */
#define RT3390		0x3390
#define RT3572		0x3572
#define RT3593		0x3593
#define RT3883		0x3883	/* WSOC */
#define RT5390		0x5390  /* 2.4GHz */
#define RT5392		0x5392  /* 2.4GHz */
#define RT5592		0x5592

	u16 rf;
	u16 rev;

	enum rt2x00_chip_intf intf;
};

/*
 * RF register values that belong to a particular channel.
 */
struct rf_channel {
	int channel;
	u32 rf1;
	u32 rf2;
	u32 rf3;
	u32 rf4;
};

/*
 * Channel information structure
 */
struct channel_info {
	unsigned int flags;
#define GEOGRAPHY_ALLOWED	0x00000001

	short max_power;
	short default_power1;
	short default_power2;
	short default_power3;
};

/*
 * Antenna setup values.
 */
struct antenna_setup {
	enum antenna rx;
	enum antenna tx;
	u8 rx_chain_num;
	u8 tx_chain_num;
};

/*
 * Quality statistics about the currently active link.
 */
struct link_qual {
	/*
	 * Statistics required for Link tuning.
	 * For the average RSSI value we use the "Walking average" approach.
	 * When adding RSSI to the average value the following calculation
	 * is needed:
	 *
	 *        avg_rssi = ((avg_rssi * 7) + rssi) / 8;
	 *
	 * The advantage of this approach is that we only need 1 variable
	 * to store the average in (No need for a count and a total).
	 * But more importantly, normal average values will over time
	 * move less and less towards newly added values this results
	 * that with link tuning, the device can have a very good RSSI
	 * for a few minutes but when the device is moved away from the AP
	 * the average will not decrease fast enough to compensate.
	 * The walking average compensates this and will move towards
	 * the new values correctly allowing a effective link tuning.
	 */
	int avg_rssi;
	int false_cca;

	/*
	 * Statistics required for Signal quality calculation.
	 * For calculating the Signal quality we have to determine
	 * the total number of success and failed RX and TX frames.
	 * After that we also use the average RSSI value to help
	 * determining the signal quality.
	 * For the calculation we will use the following algorithm:
	 *
	 *         rssi_percentage = (avg_rssi * 100) / rssi_offset
	 *         rx_percentage = (rx_success * 100) / rx_total
	 *         tx_percentage = (tx_success * 100) / tx_total
	 *         avg_signal = ((WEIGHT_RSSI * avg_rssi) +
	 *                       (WEIGHT_TX * tx_percentage) +
	 *                       (WEIGHT_RX * rx_percentage)) / 100
	 *
	 * This value should then be checked to not be greated then 100.
	 */
	int rx_percentage;
	int rx_success;
	int rx_failed;
	int tx_percentage;
	int tx_success;
	int tx_failed;
#define WEIGHT_RSSI	20
#define WEIGHT_RX	40
#define WEIGHT_TX	40
};

	 * Statistics required for Link tuning by driver
	 * The rssi value is provided by rt2x00lib during the
	 * link_tuner() callback function.
	 * The false_cca field is filled during the link_stats()
	 * callback function and could be used during the
	 * link_tuner() callback function.
	 */
	int rssi;
	int false_cca;

	/*
	 * VGC levels
	 * Hardware driver will tune the VGC level during each call
	 * to the link_tuner() callback function. This vgc_level is
	 * is determined based on the link quality statistics like
	 * average RSSI and the false CCA count.
	 *
	 * In some cases the drivers need to differentiate between
	 * the currently "desired" VGC level and the level configured
	 * in the hardware. The latter is important to reduce the
	 * number of BBP register reads to reduce register access
	 * overhead. For this reason we store both values here.
	 */
	u8 vgc_level;
	u8 vgc_level_reg;

	/*
	 * Statistics required for Signal quality calculation.
	 * These fields might be changed during the link_stats()
	 * callback function.
	 */
	int rx_success;
	int rx_failed;
	int tx_success;
	int tx_failed;
};

DECLARE_EWMA(rssi, 1024, 8)

/*
 * Antenna settings about the currently active link.
 */
struct link_ant {
	/*
	 * Antenna flags
	 */
	unsigned int flags;
#define ANTENNA_RX_DIVERSITY	0x00000001
#define ANTENNA_TX_DIVERSITY	0x00000002
#define ANTENNA_MODE_SAMPLE	0x00000004

	/*
	 * Currently active TX/RX antenna setup.
	 * When software diversity is used, this will indicate
	 * which antenna is actually used at this time.
	 */
	struct antenna_setup active;

	/*
	 * RSSI information for the different antenna's.
	 * These statistics are used to determine when
	 * to switch antenna when using software diversity.
	 *
	 *        rssi[0] -> Antenna A RSSI
	 *        rssi[1] -> Antenna B RSSI
	 */
	int rssi_history[2];
	 * RSSI history information for the antenna.
	 * Used to determine when to switch antenna
	 * when using software diversity.
	 */
	int rssi_history;

	/*
	 * Current RSSI average of the currently active antenna.
	 * Similar to the avg_rssi in the link_qual structure
	 * this value is updated by using the walking average.
	 */
	int rssi_ant;
	struct ewma_rssi rssi_ant;
};

/*
 * To optimize the quality of the link we need to store
 * the quality of received frames and periodically
 * optimize the link.
 */
struct link {
	/*
	 * Link tuner counter
	 * The number of times the link has been tuned
	 * since the radio has been switched on.
	 */
	u32 count;

	/*
	 * Quality measurement values.
	 */
	struct link_qual qual;

	/*
	 * TX/RX antenna setup.
	 */
	struct link_ant ant;

	/*
	 * Active VGC level
	 */
	int vgc_level;
	 * Currently active average RSSI value
	 */
	struct ewma_rssi avg_rssi;

	/*
	 * Work structure for scheduling periodic link tuning.
	 */
	struct delayed_work work;
};

/*
 * Small helper macro to work with moving/walking averages.
 */
#define MOVING_AVERAGE(__avg, __val, __samples) \
	( (((__avg) * ((__samples) - 1)) + (__val)) / (__samples) )

/*
 * When we lack RSSI information return something less then -80 to
 * tell the driver to tune the device to maximum sensitivity.
 */
#define DEFAULT_RSSI	( -128 )

/*
 * Link quality access functions.
 */
static inline int rt2x00_get_link_rssi(struct link *link)
{
	if (link->qual.avg_rssi && link->qual.rx_success)
		return link->qual.avg_rssi;
	return DEFAULT_RSSI;
}

static inline int rt2x00_get_link_ant_rssi(struct link *link)
{
	if (link->ant.rssi_ant && link->qual.rx_success)
		return link->ant.rssi_ant;
	return DEFAULT_RSSI;
}

static inline void rt2x00_reset_link_ant_rssi(struct link *link)
{
	link->ant.rssi_ant = 0;
}

static inline int rt2x00_get_link_ant_rssi_history(struct link *link,
						   enum antenna ant)
{
	if (link->ant.rssi_history[ant - ANTENNA_A])
		return link->ant.rssi_history[ant - ANTENNA_A];
	return DEFAULT_RSSI;
}

static inline int rt2x00_update_ant_rssi(struct link *link, int rssi)
{
	int old_rssi = link->ant.rssi_history[link->ant.active.rx - ANTENNA_A];
	link->ant.rssi_history[link->ant.active.rx - ANTENNA_A] = rssi;
	return old_rssi;
}

	/*
	 * Work structure for scheduling periodic watchdog monitoring.
	 * This work must be scheduled on the kernel workqueue, while
	 * all other work structures must be queued on the mac80211
	 * workqueue. This guarantees that the watchdog can schedule
	 * other work structures and wait for their completion in order
	 * to bring the device/driver back into the desired state.
	 */
	struct delayed_work watchdog_work;

	/*
	 * Work structure for scheduling periodic AGC adjustments.
	 */
	struct delayed_work agc_work;

	/*
	 * Work structure for scheduling periodic VCO calibration.
	 */
	struct delayed_work vco_work;
};

enum rt2x00_delayed_flags {
	DELAYED_UPDATE_BEACON,
};

/*
 * Interface structure
 * Per interface configuration details, this structure
 * is allocated as the private data for ieee80211_vif.
 */
struct rt2x00_intf {
	/*
	 * All fields within the rt2x00_intf structure
	 * must be protected with a spinlock.
	 */
	spinlock_t lock;

	/*
	 * BSS configuration. Copied from the structure
	 * passed to us through the bss_info_changed()
	 * callback funtion.
	 */
	struct ieee80211_bss_conf conf;

	/*
	 * MAC of the device.
	 */
	u8 mac[ETH_ALEN];

	/*
	 * BBSID of the AP to associate with.
	 */
	u8 bssid[ETH_ALEN];
	 * beacon->skb must be protected with the mutex.
	 */
	struct mutex beacon_skb_mutex;

	/*
	 * Entry in the beacon queue which belongs to
	 * this interface. Each interface has its own
	 * dedicated beacon entry.
	 */
	struct queue_entry *beacon;
	bool enable_beacon;

	/*
	 * Actions that needed rescheduling.
	 */
	unsigned int delayed_flags;
#define DELAYED_UPDATE_BEACON		0x00000001
#define DELAYED_CONFIG_ERP		0x00000002
#define DELAYED_LED_ASSOC		0x00000004
	unsigned long delayed_flags;

	/*
	 * Software sequence counter, this is only required
	 * for hardware which doesn't support hardware
	 * sequence counting.
	 */
	spinlock_t seqlock;
	u16 seqno;
	atomic_t seqno;
};

static inline struct rt2x00_intf* vif_to_intf(struct ieee80211_vif *vif)
{
	return (struct rt2x00_intf *)vif->drv_priv;
}

/**
 * struct hw_mode_spec: Hardware specifications structure
 *
 * Details about the supported modes, rates and channels
 * of a particular chipset. This is used by rt2x00lib
 * to build the ieee80211_hw_mode array for mac80211.
 *
 * @supported_bands: Bitmask contained the supported bands (2.4GHz, 5.2GHz).
 * @supported_rates: Rate types which are supported (CCK, OFDM).
 * @num_channels: Number of supported channels. This is used as array size
 *	for @tx_power_a, @tx_power_bg and @channels.
 * @channels: Device/chipset specific channel values (See &struct rf_channel).
 * @tx_power_a: TX power values for all 5.2GHz channels (may be NULL).
 * @tx_power_bg: TX power values for all 2.4GHz channels (may be NULL).
 * @tx_power_default: Default TX power value to use when either
 *	@tx_power_a or @tx_power_bg is missing.
 * @channels_info: Additional information for channels (See &struct channel_info).
 * @ht: Driver HT Capabilities (See &ieee80211_sta_ht_cap).
 */
struct hw_mode_spec {
	unsigned int supported_bands;
#define SUPPORT_BAND_2GHZ	0x00000001
#define SUPPORT_BAND_5GHZ	0x00000002

	unsigned int supported_rates;
#define SUPPORT_RATE_CCK	0x00000001
#define SUPPORT_RATE_OFDM	0x00000002

	unsigned int num_channels;
	const struct rf_channel *channels;

	const u8 *tx_power_a;
	const u8 *tx_power_bg;
	u8 tx_power_default;
	const struct channel_info *channels_info;

	struct ieee80211_sta_ht_cap ht;
};

/*
 * Configuration structure wrapper around the
 * mac80211 configuration structure.
 * When mac80211 configures the driver, rt2x00lib
 * can precalculate values which are equal for all
 * rt2x00 drivers. Those values can be stored in here.
 */
struct rt2x00lib_conf {
	struct ieee80211_conf *conf;
	struct rf_channel rf;

	struct antenna_setup ant;

	enum ieee80211_band band;

	u32 basic_rates;
	u32 slot_time;

	short sifs;
	short pifs;
	short difs;
	short eifs;

	struct rf_channel rf;
	struct channel_info channel;
};

/*
 * Configuration structure for erp settings.
 */
struct rt2x00lib_erp {
	int short_preamble;
	int cts_protection;

	int ack_timeout;
	int ack_consume_time;
	u32 basic_rates;

	int slot_time;

	short sifs;
	short pifs;
	short difs;
	short eifs;

	u16 beacon_int;
	u16 ht_opmode;
};

/*
 * Configuration structure for hardware encryption.
 */
struct rt2x00lib_crypto {
	enum cipher cipher;

	enum set_key_cmd cmd;
	const u8 *address;

	u32 bssidx;

	u8 key[16];
	u8 tx_mic[8];
	u8 rx_mic[8];

	int wcid;
};

/*
 * Configuration structure wrapper around the
 * rt2x00 interface configuration handler.
 */
struct rt2x00intf_conf {
	/*
	 * Interface type
	 */
	enum ieee80211_if_types type;

	/*
	 * TSF sync value, this is dependant on the operation type.
	enum nl80211_iftype type;

	/*
	 * TSF sync value, this is dependent on the operation type.
	 */
	enum tsf_sync sync;

	/*
	 * The MAC and BSSID addressess are simple array of bytes,
	 * these arrays are little endian, so when sending the addressess
	 * The MAC and BSSID addresses are simple array of bytes,
	 * these arrays are little endian, so when sending the addresses
	 * to the drivers, copy the it into a endian-signed variable.
	 *
	 * Note that all devices (except rt2500usb) have 32 bits
	 * register word sizes. This means that whatever variable we
	 * pass _must_ be a multiple of 32 bits. Otherwise the device
	 * might not accept what we are sending to it.
	 * This will also make it easier for the driver to write
	 * the data to the device.
	 */
	__le32 mac[2];
	__le32 bssid[2];
};

/*
 * Private structure for storing STA details
 * wcid: Wireless Client ID
 */
struct rt2x00_sta {
	int wcid;
};

static inline struct rt2x00_sta* sta_to_rt2x00_sta(struct ieee80211_sta *sta)
{
	return (struct rt2x00_sta *)sta->drv_priv;
}

/*
 * rt2x00lib callback functions.
 */
struct rt2x00lib_ops {
	/*
	 * Interrupt handlers.
	 */
	irq_handler_t irq_handler;

	/*
	 * TX status tasklet handler.
	 */
	void (*txstatus_tasklet) (unsigned long data);
	void (*pretbtt_tasklet) (unsigned long data);
	void (*tbtt_tasklet) (unsigned long data);
	void (*rxdone_tasklet) (unsigned long data);
	void (*autowake_tasklet) (unsigned long data);

	/*
	 * Device init handlers.
	 */
	int (*probe_hw) (struct rt2x00_dev *rt2x00dev);
	char *(*get_firmware_name) (struct rt2x00_dev *rt2x00dev);
	u16 (*get_firmware_crc) (const void *data, const size_t len);
	int (*load_firmware) (struct rt2x00_dev *rt2x00dev, const void *data,
			      const size_t len);
	int (*check_firmware) (struct rt2x00_dev *rt2x00dev,
			       const u8 *data, const size_t len);
	int (*load_firmware) (struct rt2x00_dev *rt2x00dev,
			      const u8 *data, const size_t len);

	/*
	 * Device initialization/deinitialization handlers.
	 */
	int (*initialize) (struct rt2x00_dev *rt2x00dev);
	void (*uninitialize) (struct rt2x00_dev *rt2x00dev);

	/*
	 * queue initialization handlers
	 */
	void (*init_rxentry) (struct rt2x00_dev *rt2x00dev,
			      struct queue_entry *entry);
	void (*init_txentry) (struct rt2x00_dev *rt2x00dev,
			      struct queue_entry *entry);
	bool (*get_entry_state) (struct queue_entry *entry);
	void (*clear_entry) (struct queue_entry *entry);

	/*
	 * Radio control handlers.
	 */
	int (*set_device_state) (struct rt2x00_dev *rt2x00dev,
				 enum dev_state state);
	int (*rfkill_poll) (struct rt2x00_dev *rt2x00dev);
	void (*link_stats) (struct rt2x00_dev *rt2x00dev,
			    struct link_qual *qual);
	void (*reset_tuner) (struct rt2x00_dev *rt2x00dev);
	void (*link_tuner) (struct rt2x00_dev *rt2x00dev);
	void (*reset_tuner) (struct rt2x00_dev *rt2x00dev,
			     struct link_qual *qual);
	void (*link_tuner) (struct rt2x00_dev *rt2x00dev,
			    struct link_qual *qual, const u32 count);
	void (*gain_calibration) (struct rt2x00_dev *rt2x00dev);
	void (*vco_calibration) (struct rt2x00_dev *rt2x00dev);

	/*
	 * Data queue handlers.
	 */
	void (*watchdog) (struct rt2x00_dev *rt2x00dev);
	void (*start_queue) (struct data_queue *queue);
	void (*kick_queue) (struct data_queue *queue);
	void (*stop_queue) (struct data_queue *queue);
	void (*flush_queue) (struct data_queue *queue, bool drop);
	void (*tx_dma_done) (struct queue_entry *entry);

	/*
	 * TX control handlers
	 */
	void (*write_tx_desc) (struct rt2x00_dev *rt2x00dev,
			       struct sk_buff *skb,
			       struct txentry_desc *txdesc);
	int (*write_tx_data) (struct queue_entry *entry);
	void (*write_beacon) (struct queue_entry *entry);
	int (*get_tx_data_len) (struct rt2x00_dev *rt2x00dev,
				struct sk_buff *skb);
	void (*kick_tx_queue) (struct rt2x00_dev *rt2x00dev,
			       const enum data_queue_qid queue);
	void (*write_tx_desc) (struct queue_entry *entry,
			       struct txentry_desc *txdesc);
	void (*write_tx_data) (struct queue_entry *entry,
			       struct txentry_desc *txdesc);
	void (*write_beacon) (struct queue_entry *entry,
			      struct txentry_desc *txdesc);
	void (*clear_beacon) (struct queue_entry *entry);
	int (*get_tx_data_len) (struct queue_entry *entry);

	/*
	 * RX control handlers
	 */
	void (*fill_rxdone) (struct queue_entry *entry,
			     struct rxdone_entry_desc *rxdesc);

	/*
	 * Configuration handlers.
	 */
	int (*config_shared_key) (struct rt2x00_dev *rt2x00dev,
				  struct rt2x00lib_crypto *crypto,
				  struct ieee80211_key_conf *key);
	int (*config_pairwise_key) (struct rt2x00_dev *rt2x00dev,
				    struct rt2x00lib_crypto *crypto,
				    struct ieee80211_key_conf *key);
	void (*config_filter) (struct rt2x00_dev *rt2x00dev,
			       const unsigned int filter_flags);
	void (*config_intf) (struct rt2x00_dev *rt2x00dev,
			     struct rt2x00_intf *intf,
			     struct rt2x00intf_conf *conf,
			     const unsigned int flags);
#define CONFIG_UPDATE_TYPE		( 1 << 1 )
#define CONFIG_UPDATE_MAC		( 1 << 2 )
#define CONFIG_UPDATE_BSSID		( 1 << 3 )

	void (*config_erp) (struct rt2x00_dev *rt2x00dev,
			    struct rt2x00lib_erp *erp);
	void (*config) (struct rt2x00_dev *rt2x00dev,
			struct rt2x00lib_conf *libconf,
			const unsigned int flags);
#define CONFIG_UPDATE_PHYMODE		( 1 << 1 )
#define CONFIG_UPDATE_CHANNEL		( 1 << 2 )
#define CONFIG_UPDATE_TXPOWER		( 1 << 3 )
#define CONFIG_UPDATE_ANTENNA		( 1 << 4 )
#define CONFIG_UPDATE_SLOT_TIME 	( 1 << 5 )
#define CONFIG_UPDATE_BEACON_INT	( 1 << 6 )
#define CONFIG_UPDATE_ALL		0xffff
			    struct rt2x00lib_erp *erp,
			    u32 changed);
	void (*config_ant) (struct rt2x00_dev *rt2x00dev,
			    struct antenna_setup *ant);
	void (*config) (struct rt2x00_dev *rt2x00dev,
			struct rt2x00lib_conf *libconf,
			const unsigned int changed_flags);
	int (*sta_add) (struct rt2x00_dev *rt2x00dev,
			struct ieee80211_vif *vif,
			struct ieee80211_sta *sta);
	int (*sta_remove) (struct rt2x00_dev *rt2x00dev,
			   int wcid);
};

/*
 * rt2x00 driver callback operation structure.
 */
struct rt2x00_ops {
	const char *name;
	const unsigned int max_sta_intf;
	const unsigned int drv_data_size;
	const unsigned int max_ap_intf;
	const unsigned int eeprom_size;
	const unsigned int rf_size;
	const unsigned int tx_queues;
	const struct data_queue_desc *rx;
	const struct data_queue_desc *tx;
	const struct data_queue_desc *bcn;
	const struct data_queue_desc *atim;
	const struct rt2x00lib_ops *lib;
	void (*queue_init)(struct data_queue *queue);
	const struct rt2x00lib_ops *lib;
	const void *drv;
	const struct ieee80211_ops *hw;
#ifdef CONFIG_RT2X00_LIB_DEBUGFS
	const struct rt2x00debug *debugfs;
#endif /* CONFIG_RT2X00_LIB_DEBUGFS */
};

/*
 * rt2x00 device flags
 */
enum rt2x00_flags {
	/*
	 * Device state flags
	 */
	DEVICE_PRESENT,
	DEVICE_REGISTERED_HW,
	DEVICE_INITIALIZED,
	DEVICE_STARTED,
	DEVICE_STARTED_SUSPEND,
	DEVICE_ENABLED_RADIO,
	DEVICE_DISABLED_RADIO_HW,
	DEVICE_DIRTY_CONFIG,

	/*
	 * Driver features
	 */
	DRIVER_REQUIRE_FIRMWARE,
	DRIVER_REQUIRE_BEACON_GUARD,
	DRIVER_REQUIRE_ATIM_QUEUE,
	DRIVER_REQUIRE_SCHEDULED,
	DRIVER_REQUIRE_DMA,
 * rt2x00 state flags
 */
enum rt2x00_state_flags {
	/*
	 * Device flags
	 */
	DEVICE_STATE_PRESENT,
	DEVICE_STATE_REGISTERED_HW,
	DEVICE_STATE_INITIALIZED,
	DEVICE_STATE_STARTED,
	DEVICE_STATE_ENABLED_RADIO,
	DEVICE_STATE_SCANNING,

	/*
	 * Driver configuration
	 */
	CONFIG_SUPPORT_HW_BUTTON,
	CONFIG_FRAME_TYPE,
	CONFIG_RF_SEQUENCE,
	CONFIG_EXTERNAL_LNA_A,
	CONFIG_EXTERNAL_LNA_BG,
	CONFIG_DOUBLE_ANTENNA,
	CONFIG_DISABLE_LINK_TUNING,
	CONFIG_CHANNEL_HT40,
	CONFIG_POWERSAVING,
	CONFIG_HT_DISABLED,
	CONFIG_QOS_DISABLED,

	/*
	 * Mark we currently are sequentially reading TX_STA_FIFO register
	 * FIXME: this is for only rt2800usb, should go to private data
	 */
	TX_STATUS_READING,
};

/*
 * rt2x00 capability flags
 */
enum rt2x00_capability_flags {
	/*
	 * Requirements
	 */
	REQUIRE_FIRMWARE,
	REQUIRE_BEACON_GUARD,
	REQUIRE_ATIM_QUEUE,
	REQUIRE_DMA,
	REQUIRE_COPY_IV,
	REQUIRE_L2PAD,
	REQUIRE_TXSTATUS_FIFO,
	REQUIRE_TASKLET_CONTEXT,
	REQUIRE_SW_SEQNO,
	REQUIRE_HT_TX_DESC,
	REQUIRE_PS_AUTOWAKE,
	REQUIRE_DELAYED_RFKILL,

	/*
	 * Capabilities
	 */
	CAPABILITY_HW_BUTTON,
	CAPABILITY_HW_CRYPTO,
	CAPABILITY_POWER_LIMIT,
	CAPABILITY_CONTROL_FILTERS,
	CAPABILITY_CONTROL_FILTER_PSPOLL,
	CAPABILITY_PRE_TBTT_INTERRUPT,
	CAPABILITY_LINK_TUNING,
	CAPABILITY_FRAME_TYPE,
	CAPABILITY_RF_SEQUENCE,
	CAPABILITY_EXTERNAL_LNA_A,
	CAPABILITY_EXTERNAL_LNA_BG,
	CAPABILITY_DOUBLE_ANTENNA,
	CAPABILITY_BT_COEXIST,
	CAPABILITY_VCO_RECALIBRATION,
};

/*
 * Interface combinations
 */
enum {
	IF_COMB_AP = 0,
	NUM_IF_COMB,
};

/*
 * rt2x00 device structure.
 */
struct rt2x00_dev {
	/*
	 * Device structure.
	 * The structure stored in here depends on the
	 * system bus (PCI or USB).
	 * When accessing this variable, the rt2x00dev_{pci,usb}
	 * macro's should be used for correct typecasting.
	 * macros should be used for correct typecasting.
	 */
	struct device *dev;

	/*
	 * Callback functions.
	 */
	const struct rt2x00_ops *ops;

	/*
	 * Driver data.
	 */
	void *drv_data;

	/*
	 * IEEE80211 control structure.
	 */
	struct ieee80211_hw *hw;
	struct ieee80211_supported_band bands[IEEE80211_NUM_BANDS];
	enum ieee80211_band curr_band;

	/*
	 * rfkill structure for RF state switching support.
	 * This will only be compiled in when required.
	 */
#ifdef CONFIG_RT2X00_LIB_RFKILL
	unsigned long rfkill_state;
#define RFKILL_STATE_ALLOCATED		1
#define RFKILL_STATE_REGISTERED		2
	struct rfkill *rfkill;
	struct delayed_work rfkill_work;
#endif /* CONFIG_RT2X00_LIB_RFKILL */
	int curr_freq;

	/*
	 * If enabled, the debugfs interface structures
	 * required for deregistration of debugfs.
	 */
#ifdef CONFIG_RT2X00_LIB_DEBUGFS
	struct rt2x00debug_intf *debugfs_intf;
#endif /* CONFIG_RT2X00_LIB_DEBUGFS */

	/*
	 * LED structure for changing the LED status
	 * by mac8011 or the kernel.
	 */
#ifdef CONFIG_RT2X00_LIB_LEDS
	struct rt2x00_led led_radio;
	struct rt2x00_led led_assoc;
	struct rt2x00_led led_qual;
	u16 led_mcu_reg;
#endif /* CONFIG_RT2X00_LIB_LEDS */

	/*
	 * Device flags.
	 * In these flags the current status and some
	 * of the device capabilities are stored.
	 * Device state flags.
	 * In these flags the current status is stored.
	 * Access to these flags should occur atomically.
	 */
	unsigned long flags;

	/*
	 * Device capabiltiy flags.
	 * In these flags the device/driver capabilities are stored.
	 * Access to these flags should occur non-atomically.
	 */
	unsigned long cap_flags;

	/*
	 * Device information, Bus IRQ and name (PCI, SoC)
	 */
	int irq;
	const char *name;

	/*
	 * Chipset identification.
	 */
	struct rt2x00_chip chip;

	/*
	 * hw capability specifications.
	 */
	struct hw_mode_spec spec;

	/*
	 * This is the default TX/RX antenna setup as indicated
	 * by the device's EEPROM. When mac80211 sets its
	 * antenna value to 0 we should be using these values.
	 * by the device's EEPROM.
	 */
	struct antenna_setup default_ant;

	/*
	 * Register pointers
	 * csr.base: CSR base register address. (PCI)
	 * csr.cache: CSR cache for usb_control_msg. (USB)
	 */
	union csr {
		void __iomem *base;
		void *cache;
	} csr;

	/*
	 * Mutex to protect register accesses on USB devices.
	 * There are 2 reasons this is needed, one is to ensure
	 * use of the csr_cache (for USB devices) by one thread
	 * isn't corrupted by another thread trying to access it.
	 * The other is that access to BBP and RF registers
	 * require multiple BUS transactions and if another thread
	 * attempted to access one of those registers at the same
	 * time one of the writes could silently fail.
	 */
	struct mutex usb_cache_mutex;
	 * Mutex to protect register accesses.
	 * For PCI and USB devices it protects against concurrent indirect
	 * register access (BBP, RF, MCU) since accessing those
	 * registers require multiple calls to the CSR registers.
	 * For USB devices it also protects the csr_cache since that
	 * field is used for normal CSR access and it cannot support
	 * multiple callers simultaneously.
	 */
	struct mutex csr_mutex;

	/*
	 * Current packet filter configuration for the device.
	 * This contains all currently active FIF_* flags send
	 * to us by mac80211 during configure_filter().
	 */
	unsigned int packet_filter;

	/*
	 * Interface details:
	 *  - Open ap interface count.
	 *  - Open sta interface count.
	 *  - Association count.
	 *  - Beaconing enabled count.
	 */
	unsigned int intf_ap_count;
	unsigned int intf_sta_count;
	unsigned int intf_associated;
	unsigned int intf_beaconing;

	/*
	 * Interface combinations
	 */
	struct ieee80211_iface_limit if_limits_ap;
	struct ieee80211_iface_combination if_combinations[NUM_IF_COMB];

	/*
	 * Link quality
	 */
	struct link link;

	/*
	 * EEPROM data.
	 */
	__le16 *eeprom;

	/*
	 * Active RF register values.
	 * These are stored here so we don't need
	 * to read the rf registers and can directly
	 * use this value instead.
	 * This field should be accessed by using
	 * rt2x00_rf_read() and rt2x00_rf_write().
	 */
	u32 *rf;

	/*
	 * USB Max frame size (for rt2500usb & rt73usb).
	 */
	u16 usb_maxpacket;
	 * LNA gain
	 */
	short lna_gain;

	/*
	 * Current TX power value.
	 */
	u16 tx_power;

	/*
	 * Current retry values.
	 */
	u8 short_retry;
	u8 long_retry;

	/*
	 * Rssi <-> Dbm offset
	 */
	u8 rssi_offset;

	/*
	 * Frequency offset (for rt61pci & rt73usb).
	 * Frequency offset.
	 */
	u8 freq_offset;

	/*
	 * Association id.
	 */
	u16 aid;

	/*
	 * Beacon interval.
	 */
	u16 beacon_int;

	/**
	 * Timestamp of last received beacon
	 */
	unsigned long last_beacon;

	/*
	 * Low level statistics which will have
	 * to be kept up to date while device is running.
	 */
	struct ieee80211_low_level_stats low_level_stats;

	/*
	 * RX configuration information.
	 */
	struct ieee80211_rx_status rx_status;
	/**
	 * Work queue for all work which should not be placed
	 * on the mac80211 workqueue (because of dependencies
	 * between various work structures).
	 */
	struct workqueue_struct *workqueue;

	/*
	 * Scheduled work.
	 * NOTE: intf_work will use ieee80211_iterate_active_interfaces()
	 * which means it cannot be placed on the hw->workqueue
	 * due to RTNL locking requirements.
	 */
	struct work_struct intf_work;
	struct work_struct filter_work;

	/*
	 * Data queue arrays for RX, TX and Beacon.
	 * The Beacon array also contains the Atim queue
	 * if that is supported by the device.

	/**
	 * Scheduled work for TX/RX done handling (USB devices)
	 */
	struct work_struct rxdone_work;
	struct work_struct txdone_work;

	/*
	 * Powersaving work
	 */
	struct delayed_work autowakeup_work;
	struct work_struct sleep_work;

	/*
	 * Data queue arrays for RX, TX, Beacon and ATIM.
	 */
	unsigned int data_queues;
	struct data_queue *rx;
	struct data_queue *tx;
	struct data_queue *bcn;
	struct data_queue *atim;

	/*
	 * Firmware image.
	 */
	const struct firmware *fw;
};


	/*
	 * FIFO for storing tx status reports between isr and tasklet.
	 */
	DECLARE_KFIFO_PTR(txstatus_fifo, u32);

	/*
	 * Timer to ensure tx status reports are read (rt2800usb).
	 */
	struct hrtimer txstatus_timer;

	/*
	 * Tasklet for processing tx status reports (rt2800pci).
	 */
	struct tasklet_struct txstatus_tasklet;
	struct tasklet_struct pretbtt_tasklet;
	struct tasklet_struct tbtt_tasklet;
	struct tasklet_struct rxdone_tasklet;
	struct tasklet_struct autowake_tasklet;

	/*
	 * Used for VCO periodic calibration.
	 */
	int rf_channel;

	/*
	 * Protect the interrupt mask register.
	 */
	spinlock_t irqmask_lock;

	/*
	 * List of BlockAckReq TX entries that need driver BlockAck processing.
	 */
	struct list_head bar_list;
	spinlock_t bar_list_lock;

	/* Extra TX headroom required for alignment purposes. */
	unsigned int extra_tx_headroom;
};

struct rt2x00_bar_list_entry {
	struct list_head list;
	struct rcu_head head;

	struct queue_entry *entry;
	int block_acked;

	/* Relevant parts of the IEEE80211 BAR header */
	__u8 ra[6];
	__u8 ta[6];
	__le16 control;
	__le16 start_seq_num;
};

/*
 * Register defines.
 * Some registers require multiple attempts before success,
 * in those cases REGISTER_BUSY_COUNT attempts should be
 * taken with a REGISTER_BUSY_DELAY interval. Due to USB
 * bus delays, we do not have to loop so many times to wait
 * for valid register value on that bus.
 */
#define REGISTER_BUSY_COUNT	100
#define REGISTER_USB_BUSY_COUNT 20
#define REGISTER_BUSY_DELAY	100

/*
 * Generic RF access.
 * The RF is being accessed by word index.
 */
static inline void rt2x00_rf_read(struct rt2x00_dev *rt2x00dev,
				  const unsigned int word, u32 *data)
{
	*data = rt2x00dev->rf[word];
	BUG_ON(word < 1 || word > rt2x00dev->ops->rf_size / sizeof(u32));
	*data = rt2x00dev->rf[word - 1];
}

static inline void rt2x00_rf_write(struct rt2x00_dev *rt2x00dev,
				   const unsigned int word, u32 data)
{
	rt2x00dev->rf[word] = data;
}

/*
 *  Generic EEPROM access.
 * The EEPROM is being accessed by word index.
	BUG_ON(word < 1 || word > rt2x00dev->ops->rf_size / sizeof(u32));
	rt2x00dev->rf[word - 1] = data;
}

/*
 * Generic EEPROM access. The EEPROM is being accessed by word or byte index.
 */
static inline void *rt2x00_eeprom_addr(struct rt2x00_dev *rt2x00dev,
				       const unsigned int word)
{
	return (void *)&rt2x00dev->eeprom[word];
}

static inline void rt2x00_eeprom_read(struct rt2x00_dev *rt2x00dev,
				      const unsigned int word, u16 *data)
{
	*data = le16_to_cpu(rt2x00dev->eeprom[word]);
}

static inline void rt2x00_eeprom_write(struct rt2x00_dev *rt2x00dev,
				       const unsigned int word, u16 data)
{
	rt2x00dev->eeprom[word] = cpu_to_le16(data);
}

static inline u8 rt2x00_eeprom_byte(struct rt2x00_dev *rt2x00dev,
				    const unsigned int byte)
{
	return *(((u8 *)rt2x00dev->eeprom) + byte);
}

/*
 * Chipset handlers
 */
static inline void rt2x00_set_chip(struct rt2x00_dev *rt2x00dev,
				   const u16 rt, const u16 rf, const u32 rev)
{
	INFO(rt2x00dev,
	     "Chipset detected - rt: %04x, rf: %04x, rev: %08x.\n",
	     rt, rf, rev);

	rt2x00dev->chip.rt = rt;
	rt2x00dev->chip.rf = rf;
	rt2x00dev->chip.rev = rev;
}

static inline char rt2x00_rt(const struct rt2x00_chip *chipset, const u16 chip)
{
	return (chipset->rt == chip);
}

static inline char rt2x00_rf(const struct rt2x00_chip *chipset, const u16 chip)
{
	return (chipset->rf == chip);
}

static inline u16 rt2x00_rev(const struct rt2x00_chip *chipset)
{
	return chipset->rev;
}

static inline u16 rt2x00_check_rev(const struct rt2x00_chip *chipset,
				   const u32 rev)
{
	return (((chipset->rev & 0xffff0) == rev) &&
		!!(chipset->rev & 0x0000f));
}

/*
 * Duration calculations
 * The rate variable passed is: 100kbs.
 * To convert from bytes to bits we multiply size with 8,
 * then the size is multiplied with 10 to make the
 * real rate -> rate argument correction.
 */
static inline u16 get_duration(const unsigned int size, const u8 rate)
{
	return ((size * 8 * 10) / rate);
}

static inline u16 get_duration_res(const unsigned int size, const u8 rate)
{
	return ((size * 8 * 10) % rate);
				   const u16 rt, const u16 rf, const u16 rev)
{
	rt2x00dev->chip.rt = rt;
	rt2x00dev->chip.rf = rf;
	rt2x00dev->chip.rev = rev;

	rt2x00_info(rt2x00dev, "Chipset detected - rt: %04x, rf: %04x, rev: %04x\n",
		    rt2x00dev->chip.rt, rt2x00dev->chip.rf,
		    rt2x00dev->chip.rev);
}

static inline void rt2x00_set_rt(struct rt2x00_dev *rt2x00dev,
				 const u16 rt, const u16 rev)
{
	rt2x00dev->chip.rt = rt;
	rt2x00dev->chip.rev = rev;

	rt2x00_info(rt2x00dev, "RT chipset %04x, rev %04x detected\n",
		    rt2x00dev->chip.rt, rt2x00dev->chip.rev);
}

static inline void rt2x00_set_rf(struct rt2x00_dev *rt2x00dev, const u16 rf)
{
	rt2x00dev->chip.rf = rf;

	rt2x00_info(rt2x00dev, "RF chipset %04x detected\n",
		    rt2x00dev->chip.rf);
}

static inline bool rt2x00_rt(struct rt2x00_dev *rt2x00dev, const u16 rt)
{
	return (rt2x00dev->chip.rt == rt);
}

static inline bool rt2x00_rf(struct rt2x00_dev *rt2x00dev, const u16 rf)
{
	return (rt2x00dev->chip.rf == rf);
}

static inline u16 rt2x00_rev(struct rt2x00_dev *rt2x00dev)
{
	return rt2x00dev->chip.rev;
}

static inline bool rt2x00_rt_rev(struct rt2x00_dev *rt2x00dev,
				 const u16 rt, const u16 rev)
{
	return (rt2x00_rt(rt2x00dev, rt) && rt2x00_rev(rt2x00dev) == rev);
}

static inline bool rt2x00_rt_rev_lt(struct rt2x00_dev *rt2x00dev,
				    const u16 rt, const u16 rev)
{
	return (rt2x00_rt(rt2x00dev, rt) && rt2x00_rev(rt2x00dev) < rev);
}

static inline bool rt2x00_rt_rev_gte(struct rt2x00_dev *rt2x00dev,
				     const u16 rt, const u16 rev)
{
	return (rt2x00_rt(rt2x00dev, rt) && rt2x00_rev(rt2x00dev) >= rev);
}

static inline void rt2x00_set_chip_intf(struct rt2x00_dev *rt2x00dev,
					enum rt2x00_chip_intf intf)
{
	rt2x00dev->chip.intf = intf;
}

static inline bool rt2x00_intf(struct rt2x00_dev *rt2x00dev,
			       enum rt2x00_chip_intf intf)
{
	return (rt2x00dev->chip.intf == intf);
}

static inline bool rt2x00_is_pci(struct rt2x00_dev *rt2x00dev)
{
	return rt2x00_intf(rt2x00dev, RT2X00_CHIP_INTF_PCI) ||
	       rt2x00_intf(rt2x00dev, RT2X00_CHIP_INTF_PCIE);
}

static inline bool rt2x00_is_pcie(struct rt2x00_dev *rt2x00dev)
{
	return rt2x00_intf(rt2x00dev, RT2X00_CHIP_INTF_PCIE);
}

static inline bool rt2x00_is_usb(struct rt2x00_dev *rt2x00dev)
{
	return rt2x00_intf(rt2x00dev, RT2X00_CHIP_INTF_USB);
}

static inline bool rt2x00_is_soc(struct rt2x00_dev *rt2x00dev)
{
	return rt2x00_intf(rt2x00dev, RT2X00_CHIP_INTF_SOC);
}

/* Helpers for capability flags */

static inline bool
rt2x00_has_cap_flag(struct rt2x00_dev *rt2x00dev,
		    enum rt2x00_capability_flags cap_flag)
{
	return test_bit(cap_flag, &rt2x00dev->cap_flags);
}

static inline bool
rt2x00_has_cap_hw_crypto(struct rt2x00_dev *rt2x00dev)
{
	return rt2x00_has_cap_flag(rt2x00dev, CAPABILITY_HW_CRYPTO);
}

static inline bool
rt2x00_has_cap_power_limit(struct rt2x00_dev *rt2x00dev)
{
	return rt2x00_has_cap_flag(rt2x00dev, CAPABILITY_POWER_LIMIT);
}

static inline bool
rt2x00_has_cap_control_filters(struct rt2x00_dev *rt2x00dev)
{
	return rt2x00_has_cap_flag(rt2x00dev, CAPABILITY_CONTROL_FILTERS);
}

static inline bool
rt2x00_has_cap_control_filter_pspoll(struct rt2x00_dev *rt2x00dev)
{
	return rt2x00_has_cap_flag(rt2x00dev, CAPABILITY_CONTROL_FILTER_PSPOLL);
}

static inline bool
rt2x00_has_cap_pre_tbtt_interrupt(struct rt2x00_dev *rt2x00dev)
{
	return rt2x00_has_cap_flag(rt2x00dev, CAPABILITY_PRE_TBTT_INTERRUPT);
}

static inline bool
rt2x00_has_cap_link_tuning(struct rt2x00_dev *rt2x00dev)
{
	return rt2x00_has_cap_flag(rt2x00dev, CAPABILITY_LINK_TUNING);
}

static inline bool
rt2x00_has_cap_frame_type(struct rt2x00_dev *rt2x00dev)
{
	return rt2x00_has_cap_flag(rt2x00dev, CAPABILITY_FRAME_TYPE);
}

static inline bool
rt2x00_has_cap_rf_sequence(struct rt2x00_dev *rt2x00dev)
{
	return rt2x00_has_cap_flag(rt2x00dev, CAPABILITY_RF_SEQUENCE);
}

static inline bool
rt2x00_has_cap_external_lna_a(struct rt2x00_dev *rt2x00dev)
{
	return rt2x00_has_cap_flag(rt2x00dev, CAPABILITY_EXTERNAL_LNA_A);
}

static inline bool
rt2x00_has_cap_external_lna_bg(struct rt2x00_dev *rt2x00dev)
{
	return rt2x00_has_cap_flag(rt2x00dev, CAPABILITY_EXTERNAL_LNA_BG);
}

static inline bool
rt2x00_has_cap_double_antenna(struct rt2x00_dev *rt2x00dev)
{
	return rt2x00_has_cap_flag(rt2x00dev, CAPABILITY_DOUBLE_ANTENNA);
}

static inline bool
rt2x00_has_cap_bt_coexist(struct rt2x00_dev *rt2x00dev)
{
	return rt2x00_has_cap_flag(rt2x00dev, CAPABILITY_BT_COEXIST);
}

static inline bool
rt2x00_has_cap_vco_recalibration(struct rt2x00_dev *rt2x00dev)
{
	return rt2x00_has_cap_flag(rt2x00dev, CAPABILITY_VCO_RECALIBRATION);
}

/**
 * rt2x00queue_map_txskb - Map a skb into DMA for TX purposes.
 * @rt2x00dev: Pointer to &struct rt2x00_dev.
 * @skb: The skb to map.
 */
void rt2x00queue_map_txskb(struct rt2x00_dev *rt2x00dev, struct sk_buff *skb);

/**
 * rt2x00queue_get_queue - Convert queue index to queue pointer
 * @rt2x00dev: Pointer to &struct rt2x00_dev.
 * @queue: rt2x00 queue index (see &enum data_queue_qid).
 */
struct data_queue *rt2x00queue_get_queue(struct rt2x00_dev *rt2x00dev,
					 const enum data_queue_qid queue);
 * @entry: Pointer to &struct queue_entry
 *
 * Returns -ENOMEM if mapping fail, 0 otherwise.
 */
int rt2x00queue_map_txskb(struct queue_entry *entry);

/**
 * rt2x00queue_unmap_skb - Unmap a skb from DMA.
 * @entry: Pointer to &struct queue_entry
 */
void rt2x00queue_unmap_skb(struct queue_entry *entry);

/**
 * rt2x00queue_get_tx_queue - Convert tx queue index to queue pointer
 * @rt2x00dev: Pointer to &struct rt2x00_dev.
 * @queue: rt2x00 queue index (see &enum data_queue_qid).
 *
 * Returns NULL for non tx queues.
 */
static inline struct data_queue *
rt2x00queue_get_tx_queue(struct rt2x00_dev *rt2x00dev,
			 const enum data_queue_qid queue)
{
	if (queue < rt2x00dev->ops->tx_queues && rt2x00dev->tx)
		return &rt2x00dev->tx[queue];

	if (queue == QID_ATIM)
		return rt2x00dev->atim;

	return NULL;
}

/**
 * rt2x00queue_get_entry - Get queue entry where the given index points to.
 * @queue: Pointer to &struct data_queue from where we obtain the entry.
 * @index: Index identifier for obtaining the correct index.
 */
struct queue_entry *rt2x00queue_get_entry(struct data_queue *queue,
					  enum queue_index index);

/**
 * rt2x00queue_pause_queue - Pause a data queue
 * @queue: Pointer to &struct data_queue.
 *
 * This function will pause the data queue locally, preventing
 * new frames to be added to the queue (while the hardware is
 * still allowed to run).
 */
void rt2x00queue_pause_queue(struct data_queue *queue);

/**
 * rt2x00queue_unpause_queue - unpause a data queue
 * @queue: Pointer to &struct data_queue.
 *
 * This function will unpause the data queue locally, allowing
 * new frames to be added to the queue again.
 */
void rt2x00queue_unpause_queue(struct data_queue *queue);

/**
 * rt2x00queue_start_queue - Start a data queue
 * @queue: Pointer to &struct data_queue.
 *
 * This function will start handling all pending frames in the queue.
 */
void rt2x00queue_start_queue(struct data_queue *queue);

/**
 * rt2x00queue_stop_queue - Halt a data queue
 * @queue: Pointer to &struct data_queue.
 *
 * This function will stop all pending frames in the queue.
 */
void rt2x00queue_stop_queue(struct data_queue *queue);

/**
 * rt2x00queue_flush_queue - Flush a data queue
 * @queue: Pointer to &struct data_queue.
 * @drop: True to drop all pending frames.
 *
 * This function will flush the queue. After this call
 * the queue is guaranteed to be empty.
 */
void rt2x00queue_flush_queue(struct data_queue *queue, bool drop);

/**
 * rt2x00queue_start_queues - Start all data queues
 * @rt2x00dev: Pointer to &struct rt2x00_dev.
 *
 * This function will loop through all available queues to start them
 */
void rt2x00queue_start_queues(struct rt2x00_dev *rt2x00dev);

/**
 * rt2x00queue_stop_queues - Halt all data queues
 * @rt2x00dev: Pointer to &struct rt2x00_dev.
 *
 * This function will loop through all available queues to stop
 * any pending frames.
 */
void rt2x00queue_stop_queues(struct rt2x00_dev *rt2x00dev);

/**
 * rt2x00queue_flush_queues - Flush all data queues
 * @rt2x00dev: Pointer to &struct rt2x00_dev.
 * @drop: True to drop all pending frames.
 *
 * This function will loop through all available queues to flush
 * any pending frames.
 */
void rt2x00queue_flush_queues(struct rt2x00_dev *rt2x00dev, bool drop);

/*
 * Debugfs handlers.
 */
/**
 * rt2x00debug_dump_frame - Dump a frame to userspace through debugfs.
 * @rt2x00dev: Pointer to &struct rt2x00_dev.
 * @type: The type of frame that is being dumped.
 * @skb: The skb containing the frame to be dumped.
 */
#ifdef CONFIG_RT2X00_LIB_DEBUGFS
void rt2x00debug_dump_frame(struct rt2x00_dev *rt2x00dev,
			    enum rt2x00_dump_type type, struct sk_buff *skb);
#else
static inline void rt2x00debug_dump_frame(struct rt2x00_dev *rt2x00dev,
					  enum rt2x00_dump_type type,
					  struct sk_buff *skb)
{
}
#endif /* CONFIG_RT2X00_LIB_DEBUGFS */

/*
 * Utility functions.
 */
u32 rt2x00lib_get_bssidx(struct rt2x00_dev *rt2x00dev,
			 struct ieee80211_vif *vif);

/*
 * Interrupt context handlers.
 */
void rt2x00lib_beacondone(struct rt2x00_dev *rt2x00dev);
void rt2x00lib_txdone(struct queue_entry *entry,
		      struct txdone_entry_desc *txdesc);
void rt2x00lib_rxdone(struct rt2x00_dev *rt2x00dev,
		      struct queue_entry *entry);
void rt2x00lib_pretbtt(struct rt2x00_dev *rt2x00dev);
void rt2x00lib_dmastart(struct queue_entry *entry);
void rt2x00lib_dmadone(struct queue_entry *entry);
void rt2x00lib_txdone(struct queue_entry *entry,
		      struct txdone_entry_desc *txdesc);
void rt2x00lib_txdone_noinfo(struct queue_entry *entry, u32 status);
void rt2x00lib_rxdone(struct queue_entry *entry, gfp_t gfp);

/*
 * mac80211 handlers.
 */
int rt2x00mac_tx(struct ieee80211_hw *hw, struct sk_buff *skb);
int rt2x00mac_start(struct ieee80211_hw *hw);
void rt2x00mac_stop(struct ieee80211_hw *hw);
int rt2x00mac_add_interface(struct ieee80211_hw *hw,
			    struct ieee80211_if_init_conf *conf);
void rt2x00mac_remove_interface(struct ieee80211_hw *hw,
				struct ieee80211_if_init_conf *conf);
int rt2x00mac_config(struct ieee80211_hw *hw, struct ieee80211_conf *conf);
int rt2x00mac_config_interface(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif,
			       struct ieee80211_if_conf *conf);
void rt2x00mac_configure_filter(struct ieee80211_hw *hw,
				unsigned int changed_flags,
				unsigned int *total_flags,
				int mc_count, struct dev_addr_list *mc_list);
int rt2x00mac_get_stats(struct ieee80211_hw *hw,
			struct ieee80211_low_level_stats *stats);
int rt2x00mac_get_tx_stats(struct ieee80211_hw *hw,
			   struct ieee80211_tx_queue_stats *stats);
void rt2x00mac_tx(struct ieee80211_hw *hw,
		  struct ieee80211_tx_control *control,
		  struct sk_buff *skb);
int rt2x00mac_start(struct ieee80211_hw *hw);
void rt2x00mac_stop(struct ieee80211_hw *hw);
int rt2x00mac_add_interface(struct ieee80211_hw *hw,
			    struct ieee80211_vif *vif);
void rt2x00mac_remove_interface(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif);
int rt2x00mac_config(struct ieee80211_hw *hw, u32 changed);
void rt2x00mac_configure_filter(struct ieee80211_hw *hw,
				unsigned int changed_flags,
				unsigned int *total_flags,
				u64 multicast);
int rt2x00mac_set_tim(struct ieee80211_hw *hw, struct ieee80211_sta *sta,
		      bool set);
#ifdef CONFIG_RT2X00_LIB_CRYPTO
int rt2x00mac_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
		      struct ieee80211_vif *vif, struct ieee80211_sta *sta,
		      struct ieee80211_key_conf *key);
#else
#define rt2x00mac_set_key	NULL
#endif /* CONFIG_RT2X00_LIB_CRYPTO */
int rt2x00mac_sta_add(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		      struct ieee80211_sta *sta);
int rt2x00mac_sta_remove(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			 struct ieee80211_sta *sta);
void rt2x00mac_sw_scan_start(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     const u8 *mac_addr);
void rt2x00mac_sw_scan_complete(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif);
int rt2x00mac_get_stats(struct ieee80211_hw *hw,
			struct ieee80211_low_level_stats *stats);
void rt2x00mac_bss_info_changed(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct ieee80211_bss_conf *bss_conf,
				u32 changes);
int rt2x00mac_conf_tx(struct ieee80211_hw *hw, u16 queue,
		      const struct ieee80211_tx_queue_params *params);
int rt2x00mac_conf_tx(struct ieee80211_hw *hw,
		      struct ieee80211_vif *vif, u16 queue,
		      const struct ieee80211_tx_queue_params *params);
void rt2x00mac_rfkill_poll(struct ieee80211_hw *hw);
void rt2x00mac_flush(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		     u32 queues, bool drop);
int rt2x00mac_set_antenna(struct ieee80211_hw *hw, u32 tx_ant, u32 rx_ant);
int rt2x00mac_get_antenna(struct ieee80211_hw *hw, u32 *tx_ant, u32 *rx_ant);
void rt2x00mac_get_ringparam(struct ieee80211_hw *hw,
			     u32 *tx, u32 *tx_max, u32 *rx, u32 *rx_max);
bool rt2x00mac_tx_frames_pending(struct ieee80211_hw *hw);

/*
 * Driver allocation handlers.
 */
int rt2x00lib_probe_dev(struct rt2x00_dev *rt2x00dev);
void rt2x00lib_remove_dev(struct rt2x00_dev *rt2x00dev);
#ifdef CONFIG_PM
int rt2x00lib_suspend(struct rt2x00_dev *rt2x00dev, pm_message_t state);
int rt2x00lib_resume(struct rt2x00_dev *rt2x00dev);
#endif /* CONFIG_PM */

#endif /* RT2X00_H */
