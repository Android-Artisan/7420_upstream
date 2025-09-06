/*
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Lennert Buytenhek		<buytenh@gnu.org>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_IF_BRIDGE_H
#define _LINUX_IF_BRIDGE_H

#include <linux/types.h>

#define SYSFS_BRIDGE_ATTR	"bridge"
#define SYSFS_BRIDGE_FDB	"brforward"
#define SYSFS_BRIDGE_PORT_SUBDIR "brif"
#define SYSFS_BRIDGE_PORT_ATTR	"brport"
#define SYSFS_BRIDGE_PORT_LINK	"bridge"

#define BRCTL_VERSION 1

#define BRCTL_GET_VERSION 0
#define BRCTL_GET_BRIDGES 1
#define BRCTL_ADD_BRIDGE 2
#define BRCTL_DEL_BRIDGE 3
#define BRCTL_ADD_IF 4
#define BRCTL_DEL_IF 5
#define BRCTL_GET_BRIDGE_INFO 6
#define BRCTL_GET_PORT_LIST 7
#define BRCTL_SET_BRIDGE_FORWARD_DELAY 8
#define BRCTL_SET_BRIDGE_HELLO_TIME 9
#define BRCTL_SET_BRIDGE_MAX_AGE 10
#define BRCTL_SET_AGEING_TIME 11
#define BRCTL_SET_GC_INTERVAL 12
#define BRCTL_GET_PORT_INFO 13
#define BRCTL_SET_BRIDGE_STP_STATE 14
#define BRCTL_SET_BRIDGE_PRIORITY 15
#define BRCTL_SET_PORT_PRIORITY 16
#define BRCTL_SET_PATH_COST 17
#define BRCTL_GET_FDB_ENTRIES 18

#define BR_STATE_DISABLED 0
#define BR_STATE_LISTENING 1
#define BR_STATE_LEARNING 2
#define BR_STATE_FORWARDING 3
#define BR_STATE_BLOCKING 4

struct __bridge_info
{
	__u64 designated_root;
	__u64 bridge_id;
	__u32 root_path_cost;
	__u32 max_age;
	__u32 hello_time;
	__u32 forward_delay;
	__u32 bridge_max_age;
	__u32 bridge_hello_time;
	__u32 bridge_forward_delay;
	__u8 topology_change;
	__u8 topology_change_detected;
	__u8 root_port;
	__u8 stp_enabled;
	__u32 ageing_time;
	__u32 gc_interval;
	__u32 hello_timer_value;
	__u32 tcn_timer_value;
	__u32 topology_change_timer_value;
	__u32 gc_timer_value;
};

struct __port_info
{
	__u64 designated_root;
	__u64 designated_bridge;
	__u16 port_id;
	__u16 designated_port;
	__u32 path_cost;
	__u32 designated_cost;
	__u8 state;
	__u8 top_change_ack;
	__u8 config_pending;
	__u8 unused0;
	__u32 message_age_timer_value;
	__u32 forward_delay_timer_value;
	__u32 hold_timer_value;
};

struct __fdb_entry
{
	__u8 mac_addr[6];
	__u8 port_no;
	__u8 is_local;
	__u32 ageing_timer_value;
	__u8 port_hi;
	__u8 pad0;
	__u16 unused;
};

#ifdef __KERNEL__

#include <linux/netdevice.h>

extern void brioctl_set(int (*ioctl_hook)(struct net *, unsigned int, void __user *));
extern struct sk_buff *(*br_handle_frame_hook)(struct net_bridge_port *p,
					       struct sk_buff *skb);
extern int (*br_should_route_hook)(struct sk_buff *skb);

#ifndef _LINUX_IF_BRIDGE_H
#define _LINUX_IF_BRIDGE_H


#include <linux/netdevice.h>
#include <uapi/linux/if_bridge.h>
#include <linux/bitops.h>

struct br_ip {
	union {
		__be32	ip4;
#if IS_ENABLED(CONFIG_IPV6)
		struct in6_addr ip6;
#endif
	} u;
	__be16		proto;
	__u16           vid;
};

struct br_ip_list {
	struct list_head list;
	struct br_ip addr;
};

#define BR_HAIRPIN_MODE		BIT(0)
#define BR_BPDU_GUARD		BIT(1)
#define BR_ROOT_BLOCK		BIT(2)
#define BR_MULTICAST_FAST_LEAVE	BIT(3)
#define BR_ADMIN_COST		BIT(4)
#define BR_LEARNING		BIT(5)
#define BR_FLOOD		BIT(6)
#define BR_AUTO_MASK		(BR_FLOOD | BR_LEARNING)
#define BR_PROMISC		BIT(7)
#define BR_PROXYARP		BIT(8)
#define BR_LEARNING_SYNC	BIT(9)
#define BR_PROXYARP_WIFI	BIT(10)

#define BR_DEFAULT_AGEING_TIME	(300 * HZ)

extern void brioctl_set(int (*ioctl_hook)(struct net *, unsigned int, void __user *));

typedef int br_should_route_hook_t(struct sk_buff *skb);
extern br_should_route_hook_t __rcu *br_should_route_hook;

#if IS_ENABLED(CONFIG_BRIDGE) && IS_ENABLED(CONFIG_BRIDGE_IGMP_SNOOPING)
int br_multicast_list_adjacent(struct net_device *dev,
			       struct list_head *br_ip_list);
bool br_multicast_has_querier_anywhere(struct net_device *dev, int proto);
bool br_multicast_has_querier_adjacent(struct net_device *dev, int proto);
#else
static inline int br_multicast_list_adjacent(struct net_device *dev,
					     struct list_head *br_ip_list)
{
	return 0;
}
static inline bool br_multicast_has_querier_anywhere(struct net_device *dev,
						     int proto)
{
	return false;
}
static inline bool br_multicast_has_querier_adjacent(struct net_device *dev,
						     int proto)
{
	return false;
}
#endif

#endif
