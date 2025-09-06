/*
 * originally based on the dummy device.
 *
 * Copyright 1999, Thomas Davis, tadavis@lbl.gov.
 * Licensed under the GPL. Based on dummy.c, and eql.c devices.
 *
 * bonding.c: an Ethernet Bonding driver
 *
 * This is useful to talk to a Cisco EtherChannel compatible equipment:
 *	Cisco 5500
 *	Sun Trunking (Solaris)
 *	Alteon AceDirector Trunks
 *	Linux Bonding
 *	and probably many L2 switches ...
 *
 * How it works:
 *    ifconfig bond0 ipaddress netmask up
 *      will setup a network device, with an ip address.  No mac address
 *	will be assigned at this time.  The hw mac address will come from
 *	the first slave bonded to the channel.  All slaves will then use
 *	this hw mac address.
 *
 *    ifconfig bond0 down
 *         will release all slaves, marking them as down.
 *
 *    ifenslave bond0 eth0
 *	will attach eth0 to bond0 as a slave.  eth0 hw mac address will either
 *	a: be used as initial mac address
 *	b: if a hw mac address already is there, eth0's hw mac address
 *	   will then be set from bond0.
 *
 */

//#define BONDING_DEBUG 1

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <net/ip.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/socket.h>
#include <linux/ctype.h>
#include <linux/inet.h>
#include <linux/bitops.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/uaccess.h>
#include <linux/io.h>
#include <asm/dma.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/igmp.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <linux/rtnetlink.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/smp.h>
#include <linux/if_ether.h>
#include <net/arp.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/if_vlan.h>
#include <linux/if_bonding.h>
#include <linux/jiffies.h>
#include <net/route.h>
#include <net/net_namespace.h>
#include "bonding.h"
#include "bond_3ad.h"
#include "bond_alb.h"
#include <linux/preempt.h>
#include <net/route.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>
#include <net/pkt_sched.h>
#include <linux/rculist.h>
#include <net/flow_dissector.h>
#include <net/switchdev.h>
#include <net/bonding.h>
#include <net/bond_3ad.h>
#include <net/bond_alb.h>

#include "bonding_priv.h"

/*---------------------------- Module parameters ----------------------------*/

/* monitor all links that often (in milliseconds). <=0 disables monitoring */
#define BOND_LINK_MON_INTERV	0
#define BOND_LINK_ARP_INTERV	0

static int max_bonds	= BOND_DEFAULT_MAX_BONDS;
static int num_grat_arp = 1;
static int miimon	= BOND_LINK_MON_INTERV;
static int updelay	= 0;
static int downdelay	= 0;
static int use_carrier	= 1;
static char *mode	= NULL;
static char *primary	= NULL;
static char *lacp_rate	= NULL;
static char *xmit_hash_policy = NULL;
static int arp_interval = BOND_LINK_ARP_INTERV;
static char *arp_ip_target[BOND_MAX_ARP_TARGETS] = { NULL, };
static char *arp_validate = NULL;
static char *fail_over_mac = NULL;
struct bond_params bonding_defaults;

module_param(max_bonds, int, 0);
MODULE_PARM_DESC(max_bonds, "Max number of bonded devices");
module_param(num_grat_arp, int, 0644);
MODULE_PARM_DESC(num_grat_arp, "Number of gratuitous ARP packets to send on failover event");

static int max_bonds	= BOND_DEFAULT_MAX_BONDS;
static int tx_queues	= BOND_DEFAULT_TX_QUEUES;
static int num_peer_notif = 1;
static int miimon;
static int updelay;
static int downdelay;
static int use_carrier	= 1;
static char *mode;
static char *primary;
static char *primary_reselect;
static char *lacp_rate;
static int min_links;
static char *ad_select;
static char *xmit_hash_policy;
static int arp_interval;
static char *arp_ip_target[BOND_MAX_ARP_TARGETS];
static char *arp_validate;
static char *arp_all_targets;
static char *fail_over_mac;
static int all_slaves_active;
static struct bond_params bonding_defaults;
static int resend_igmp = BOND_DEFAULT_RESEND_IGMP;
static int packets_per_slave = 1;
static int lp_interval = BOND_ALB_DEFAULT_LP_INTERVAL;

module_param(max_bonds, int, 0);
MODULE_PARM_DESC(max_bonds, "Max number of bonded devices");
module_param(tx_queues, int, 0);
MODULE_PARM_DESC(tx_queues, "Max number of transmit queues (default = 16)");
module_param_named(num_grat_arp, num_peer_notif, int, 0644);
MODULE_PARM_DESC(num_grat_arp, "Number of peer notifications to send on "
			       "failover event (alias of num_unsol_na)");
module_param_named(num_unsol_na, num_peer_notif, int, 0644);
MODULE_PARM_DESC(num_unsol_na, "Number of peer notifications to send on "
			       "failover event (alias of num_grat_arp)");
module_param(miimon, int, 0);
MODULE_PARM_DESC(miimon, "Link check interval in milliseconds");
module_param(updelay, int, 0);
MODULE_PARM_DESC(updelay, "Delay before considering link up, in milliseconds");
module_param(downdelay, int, 0);
MODULE_PARM_DESC(downdelay, "Delay before considering link down, "
			    "in milliseconds");
module_param(use_carrier, int, 0);
MODULE_PARM_DESC(use_carrier, "Use netif_carrier_ok (vs MII ioctls) in miimon; "
			      "0 for off, 1 for on (default)");
module_param(mode, charp, 0);
MODULE_PARM_DESC(mode, "Mode of operation : 0 for balance-rr, "
MODULE_PARM_DESC(mode, "Mode of operation; 0 for balance-rr, "
		       "1 for active-backup, 2 for balance-xor, "
		       "3 for broadcast, 4 for 802.3ad, 5 for balance-tlb, "
		       "6 for balance-alb");
module_param(primary, charp, 0);
MODULE_PARM_DESC(primary, "Primary network device to use");
module_param(lacp_rate, charp, 0);
MODULE_PARM_DESC(lacp_rate, "LACPDU tx rate to request from 802.3ad partner "
			    "(slow/fast)");
module_param(xmit_hash_policy, charp, 0);
MODULE_PARM_DESC(xmit_hash_policy, "XOR hashing method: 0 for layer 2 (default)"
				   ", 1 for layer 3+4");
module_param(primary_reselect, charp, 0);
MODULE_PARM_DESC(primary_reselect, "Reselect primary slave "
				   "once it comes up; "
				   "0 for always (default), "
				   "1 for only if speed of primary is "
				   "better, "
				   "2 for only on active slave "
				   "failure");
module_param(lacp_rate, charp, 0);
MODULE_PARM_DESC(lacp_rate, "LACPDU tx rate to request from 802.3ad partner; "
			    "0 for slow, 1 for fast");
module_param(ad_select, charp, 0);
MODULE_PARM_DESC(ad_select, "803.ad aggregation selection logic; "
			    "0 for stable (default), 1 for bandwidth, "
			    "2 for count");
module_param(min_links, int, 0);
MODULE_PARM_DESC(min_links, "Minimum number of available links before turning on carrier");

module_param(xmit_hash_policy, charp, 0);
MODULE_PARM_DESC(xmit_hash_policy, "balance-xor and 802.3ad hashing method; "
				   "0 for layer 2 (default), 1 for layer 3+4, "
				   "2 for layer 2+3, 3 for encap layer 2+3, "
				   "4 for encap layer 3+4");
module_param(arp_interval, int, 0);
MODULE_PARM_DESC(arp_interval, "arp interval in milliseconds");
module_param_array(arp_ip_target, charp, NULL, 0);
MODULE_PARM_DESC(arp_ip_target, "arp targets in n.n.n.n form");
module_param(arp_validate, charp, 0);
MODULE_PARM_DESC(arp_validate, "validate src/dst of ARP probes: none (default), active, backup or all");
module_param(fail_over_mac, charp, 0);
MODULE_PARM_DESC(fail_over_mac, "For active-backup, do not set all slaves to the same MAC.  none (default), active or follow");

/*----------------------------- Global variables ----------------------------*/

static const char * const version =
	DRV_DESCRIPTION ": v" DRV_VERSION " (" DRV_RELDATE ")\n";

LIST_HEAD(bond_dev_list);

#ifdef CONFIG_PROC_FS
static struct proc_dir_entry *bond_proc_dir = NULL;
#endif

extern struct rw_semaphore bonding_rwsem;
static __be32 arp_target[BOND_MAX_ARP_TARGETS] = { 0, } ;
static int arp_ip_count	= 0;
static int bond_mode	= BOND_MODE_ROUNDROBIN;
static int xmit_hashtype= BOND_XMIT_POLICY_LAYER2;
static int lacp_fast	= 0;


struct bond_parm_tbl bond_lacp_tbl[] = {
{	"slow",		AD_LACP_SLOW},
{	"fast",		AD_LACP_FAST},
{	NULL,		-1},
};

struct bond_parm_tbl bond_mode_tbl[] = {
{	"balance-rr",		BOND_MODE_ROUNDROBIN},
{	"active-backup",	BOND_MODE_ACTIVEBACKUP},
{	"balance-xor",		BOND_MODE_XOR},
{	"broadcast",		BOND_MODE_BROADCAST},
{	"802.3ad",		BOND_MODE_8023AD},
{	"balance-tlb",		BOND_MODE_TLB},
{	"balance-alb",		BOND_MODE_ALB},
{	NULL,			-1},
};

struct bond_parm_tbl xmit_hashtype_tbl[] = {
{	"layer2",		BOND_XMIT_POLICY_LAYER2},
{	"layer3+4",		BOND_XMIT_POLICY_LAYER34},
{	"layer2+3",		BOND_XMIT_POLICY_LAYER23},
{	NULL,			-1},
};

struct bond_parm_tbl arp_validate_tbl[] = {
{	"none",			BOND_ARP_VALIDATE_NONE},
{	"active",		BOND_ARP_VALIDATE_ACTIVE},
{	"backup",		BOND_ARP_VALIDATE_BACKUP},
{	"all",			BOND_ARP_VALIDATE_ALL},
{	NULL,			-1},
};

struct bond_parm_tbl fail_over_mac_tbl[] = {
{	"none",			BOND_FOM_NONE},
{	"active",		BOND_FOM_ACTIVE},
{	"follow",		BOND_FOM_FOLLOW},
{	NULL,			-1},
};

/*-------------------------- Forward declarations ---------------------------*/

static void bond_send_gratuitous_arp(struct bonding *bond);
static void bond_deinit(struct net_device *bond_dev);

/*---------------------------- General routines -----------------------------*/

static const char *bond_mode_name(int mode)
{
	switch (mode) {
	case BOND_MODE_ROUNDROBIN :
		return "load balancing (round-robin)";
	case BOND_MODE_ACTIVEBACKUP :
		return "fault-tolerance (active-backup)";
	case BOND_MODE_XOR :
		return "load balancing (xor)";
	case BOND_MODE_BROADCAST :
		return "fault-tolerance (broadcast)";
	case BOND_MODE_8023AD:
		return "IEEE 802.3ad Dynamic link aggregation";
	case BOND_MODE_TLB:
		return "transmit load balancing";
	case BOND_MODE_ALB:
		return "adaptive load balancing";
	default:
		return "unknown";
	}
MODULE_PARM_DESC(arp_validate, "validate src/dst of ARP probes; "
			       "0 for none (default), 1 for active, "
			       "2 for backup, 3 for all");
module_param(arp_all_targets, charp, 0);
MODULE_PARM_DESC(arp_all_targets, "fail on any/all arp targets timeout; 0 for any (default), 1 for all");
module_param(fail_over_mac, charp, 0);
MODULE_PARM_DESC(fail_over_mac, "For active-backup, do not set all slaves to "
				"the same MAC; 0 for none (default), "
				"1 for active, 2 for follow");
module_param(all_slaves_active, int, 0);
MODULE_PARM_DESC(all_slaves_active, "Keep all frames received on an interface "
				     "by setting active flag for all slaves; "
				     "0 for never (default), 1 for always.");
module_param(resend_igmp, int, 0);
MODULE_PARM_DESC(resend_igmp, "Number of IGMP membership reports to send on "
			      "link failure");
module_param(packets_per_slave, int, 0);
MODULE_PARM_DESC(packets_per_slave, "Packets to send per slave in balance-rr "
				    "mode; 0 for a random slave, 1 packet per "
				    "slave (default), >1 packets per slave.");
module_param(lp_interval, uint, 0);
MODULE_PARM_DESC(lp_interval, "The number of seconds between instances where "
			      "the bonding driver sends learning packets to "
			      "each slaves peer switch. The default is 1.");

/*----------------------------- Global variables ----------------------------*/

#ifdef CONFIG_NET_POLL_CONTROLLER
atomic_t netpoll_block_tx = ATOMIC_INIT(0);
#endif

int bond_net_id __read_mostly;

static __be32 arp_target[BOND_MAX_ARP_TARGETS];
static int arp_ip_count;
static int bond_mode	= BOND_MODE_ROUNDROBIN;
static int xmit_hashtype = BOND_XMIT_POLICY_LAYER2;
static int lacp_fast;

/*-------------------------- Forward declarations ---------------------------*/

static int bond_init(struct net_device *bond_dev);
static void bond_uninit(struct net_device *bond_dev);
static struct rtnl_link_stats64 *bond_get_stats(struct net_device *bond_dev,
						struct rtnl_link_stats64 *stats);
static void bond_slave_arr_handler(struct work_struct *work);
static bool bond_time_in_interval(struct bonding *bond, unsigned long last_act,
				  int mod);
static void bond_netdev_notify_work(struct work_struct *work);

/*---------------------------- General routines -----------------------------*/

const char *bond_mode_name(int mode)
{
	static const char *names[] = {
		[BOND_MODE_ROUNDROBIN] = "load balancing (round-robin)",
		[BOND_MODE_ACTIVEBACKUP] = "fault-tolerance (active-backup)",
		[BOND_MODE_XOR] = "load balancing (xor)",
		[BOND_MODE_BROADCAST] = "fault-tolerance (broadcast)",
		[BOND_MODE_8023AD] = "IEEE 802.3ad Dynamic link aggregation",
		[BOND_MODE_TLB] = "transmit load balancing",
		[BOND_MODE_ALB] = "adaptive load balancing",
	};

	if (mode < BOND_MODE_ROUNDROBIN || mode > BOND_MODE_ALB)
		return "unknown";

	return names[mode];
}

/*---------------------------------- VLAN -----------------------------------*/

/**
 * bond_add_vlan - add a new vlan id on bond
 * @bond: bond that got the notification
 * @vlan_id: the vlan id to add
 *
 * Returns -ENOMEM if allocation failed.
 */
static int bond_add_vlan(struct bonding *bond, unsigned short vlan_id)
{
	struct vlan_entry *vlan;

	dprintk("bond: %s, vlan id %d\n",
		(bond ? bond->dev->name: "None"), vlan_id);

	vlan = kmalloc(sizeof(struct vlan_entry), GFP_KERNEL);
	if (!vlan) {
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&vlan->vlan_list);
	vlan->vlan_id = vlan_id;
	vlan->vlan_ip = 0;

	write_lock_bh(&bond->lock);

	list_add_tail(&vlan->vlan_list, &bond->vlan_list);

	write_unlock_bh(&bond->lock);

	dprintk("added VLAN ID %d on bond %s\n", vlan_id, bond->dev->name);

	return 0;
}

/**
 * bond_del_vlan - delete a vlan id from bond
 * @bond: bond that got the notification
 * @vlan_id: the vlan id to delete
 *
 * returns -ENODEV if @vlan_id was not found in @bond.
 */
static int bond_del_vlan(struct bonding *bond, unsigned short vlan_id)
{
	struct vlan_entry *vlan;
	int res = -ENODEV;

	dprintk("bond: %s, vlan id %d\n", bond->dev->name, vlan_id);

	write_lock_bh(&bond->lock);

	list_for_each_entry(vlan, &bond->vlan_list, vlan_list) {
		if (vlan->vlan_id == vlan_id) {
			list_del(&vlan->vlan_list);

			if ((bond->params.mode == BOND_MODE_TLB) ||
			    (bond->params.mode == BOND_MODE_ALB)) {
				bond_alb_clear_vlan(bond, vlan_id);
			}

			dprintk("removed VLAN ID %d from bond %s\n", vlan_id,
				bond->dev->name);

			kfree(vlan);

			if (list_empty(&bond->vlan_list) &&
			    (bond->slave_cnt == 0)) {
				/* Last VLAN removed and no slaves, so
				 * restore block on adding VLANs. This will
				 * be removed once new slaves that are not
				 * VLAN challenged will be added.
				 */
				bond->dev->features |= NETIF_F_VLAN_CHALLENGED;
			}

			res = 0;
			goto out;
		}
	}

	dprintk("couldn't find VLAN ID %d in bond %s\n", vlan_id,
		bond->dev->name);

out:
	write_unlock_bh(&bond->lock);
	return res;
}

/**
 * bond_has_challenged_slaves
 * @bond: the bond we're working on
 *
 * Searches the slave list. Returns 1 if a vlan challenged slave
 * was found, 0 otherwise.
 *
 * Assumes bond->lock is held.
 */
static int bond_has_challenged_slaves(struct bonding *bond)
{
	struct slave *slave;
	int i;

	bond_for_each_slave(bond, slave, i) {
		if (slave->dev->features & NETIF_F_VLAN_CHALLENGED) {
			dprintk("found VLAN challenged slave - %s\n",
				slave->dev->name);
			return 1;
		}
	}

	dprintk("no VLAN challenged slaves found\n");
	return 0;
}

/**
 * bond_next_vlan - safely skip to the next item in the vlans list.
 * @bond: the bond we're working on
 * @curr: item we're advancing from
 *
 * Returns %NULL if list is empty, bond->next_vlan if @curr is %NULL,
 * or @curr->next otherwise (even if it is @curr itself again).
 * 
 * Caller must hold bond->lock
 */
struct vlan_entry *bond_next_vlan(struct bonding *bond, struct vlan_entry *curr)
{
	struct vlan_entry *next, *last;

	if (list_empty(&bond->vlan_list)) {
		return NULL;
	}

	if (!curr) {
		next = list_entry(bond->vlan_list.next,
				  struct vlan_entry, vlan_list);
	} else {
		last = list_entry(bond->vlan_list.prev,
				  struct vlan_entry, vlan_list);
		if (last == curr) {
			next = list_entry(bond->vlan_list.next,
					  struct vlan_entry, vlan_list);
		} else {
			next = list_entry(curr->vlan_list.next,
					  struct vlan_entry, vlan_list);
		}
	}

	return next;
}

/**
 * bond_dev_queue_xmit - Prepare skb for xmit.
 * 
 * @bond: bond device that got this skb for tx.
 * @skb: hw accel VLAN tagged skb to transmit
 * @slave_dev: slave that is supposed to xmit this skbuff
 * 
 * When the bond gets an skb to transmit that is
 * already hardware accelerated VLAN tagged, and it
 * needs to relay this skb to a slave that is not
 * hw accel capable, the skb needs to be "unaccelerated",
 * i.e. strip the hwaccel tag and re-insert it as part
 * of the payload.
 */
int bond_dev_queue_xmit(struct bonding *bond, struct sk_buff *skb, struct net_device *slave_dev)
{
	unsigned short uninitialized_var(vlan_id);

	if (!list_empty(&bond->vlan_list) &&
	    !(slave_dev->features & NETIF_F_HW_VLAN_TX) &&
	    vlan_get_tag(skb, &vlan_id) == 0) {
		skb->dev = slave_dev;
		skb = vlan_put_tag(skb, vlan_id);
		if (!skb) {
			/* vlan_put_tag() frees the skb in case of error,
			 * so return success here so the calling functions
			 * won't attempt to free is again.
			 */
			return 0;
		}
	} else {
		skb->dev = slave_dev;
	}

	skb->priority = 1;
	dev_queue_xmit(skb);

	return 0;
}

/*
 * In the following 3 functions, bond_vlan_rx_register(), bond_vlan_rx_add_vid
 * and bond_vlan_rx_kill_vid, We don't protect the slave list iteration with a
 * lock because:
 * bond_dev_queue_xmit - Prepare skb for xmit.
 *
 * @bond: bond device that got this skb for tx.
 * @skb: hw accel VLAN tagged skb to transmit
 * @slave_dev: slave that is supposed to xmit this skbuff
 */
void bond_dev_queue_xmit(struct bonding *bond, struct sk_buff *skb,
			struct net_device *slave_dev)
{
	skb->dev = slave_dev;

	BUILD_BUG_ON(sizeof(skb->queue_mapping) !=
		     sizeof(qdisc_skb_cb(skb)->slave_dev_queue_mapping));
	skb->queue_mapping = qdisc_skb_cb(skb)->slave_dev_queue_mapping;

	if (unlikely(netpoll_tx_running(bond->dev)))
		bond_netpoll_send_skb(bond_get_slave_by_dev(bond, slave_dev), skb);
	else
		dev_queue_xmit(skb);
}

/* In the following 2 functions, bond_vlan_rx_add_vid and bond_vlan_rx_kill_vid,
 * We don't protect the slave list iteration with a lock because:
 * a. This operation is performed in IOCTL context,
 * b. The operation is protected by the RTNL semaphore in the 8021q code,
 * c. Holding a lock with BH disabled while directly calling a base driver
 *    entry point is generally a BAD idea.
 * 
 *
 * The design of synchronization/protection for this operation in the 8021q
 * module is good for one or more VLAN devices over a single physical device
 * and cannot be extended for a teaming solution like bonding, so there is a
 * potential race condition here where a net device from the vlan group might
 * be referenced (either by a base driver or the 8021q code) while it is being
 * removed from the system. However, it turns out we're not making matters
 * worse, and if it works for regular VLAN usage it will work here too.
*/

/**
 * bond_vlan_rx_register - Propagates registration to slaves
 * @bond_dev: bonding net device that got called
 * @grp: vlan group being registered
 */
static void bond_vlan_rx_register(struct net_device *bond_dev, struct vlan_group *grp)
{
	struct bonding *bond = bond_dev->priv;
	struct slave *slave;
	int i;

	bond->vlgrp = grp;

	bond_for_each_slave(bond, slave, i) {
		struct net_device *slave_dev = slave->dev;

		if ((slave_dev->features & NETIF_F_HW_VLAN_RX) &&
		    slave_dev->vlan_rx_register) {
			slave_dev->vlan_rx_register(slave_dev, grp);
		}
	}
}

/**
 * bond_vlan_rx_add_vid - Propagates adding an id to slaves
 * @bond_dev: bonding net device that got called
 * @vid: vlan id being added
 */
static void bond_vlan_rx_add_vid(struct net_device *bond_dev, uint16_t vid)
{
	struct bonding *bond = bond_dev->priv;
	struct slave *slave;
	int i, res;

	bond_for_each_slave(bond, slave, i) {
		struct net_device *slave_dev = slave->dev;

		if ((slave_dev->features & NETIF_F_HW_VLAN_FILTER) &&
		    slave_dev->vlan_rx_add_vid) {
			slave_dev->vlan_rx_add_vid(slave_dev, vid);
		}
	}

	res = bond_add_vlan(bond, vid);
	if (res) {
		printk(KERN_ERR DRV_NAME
		       ": %s: Error: Failed to add vlan id %d\n",
		       bond_dev->name, vid);
	}
static int bond_vlan_rx_add_vid(struct net_device *bond_dev,
				__be16 proto, u16 vid)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct slave *slave, *rollback_slave;
	struct list_head *iter;
	int res;

	bond_for_each_slave(bond, slave, iter) {
		res = vlan_vid_add(slave->dev, proto, vid);
		if (res)
			goto unwind;
	}

	return 0;

unwind:
	/* unwind to the slave that failed */
	bond_for_each_slave(bond, rollback_slave, iter) {
		if (rollback_slave == slave)
			break;

		vlan_vid_del(rollback_slave->dev, proto, vid);
	}

	return res;
}

/**
 * bond_vlan_rx_kill_vid - Propagates deleting an id to slaves
 * @bond_dev: bonding net device that got called
 * @vid: vlan id being removed
 */
static void bond_vlan_rx_kill_vid(struct net_device *bond_dev, uint16_t vid)
{
	struct bonding *bond = bond_dev->priv;
	struct slave *slave;
	struct net_device *vlan_dev;
	int i, res;

	bond_for_each_slave(bond, slave, i) {
		struct net_device *slave_dev = slave->dev;

		if ((slave_dev->features & NETIF_F_HW_VLAN_FILTER) &&
		    slave_dev->vlan_rx_kill_vid) {
			/* Save and then restore vlan_dev in the grp array,
			 * since the slave's driver might clear it.
			 */
			vlan_dev = vlan_group_get_device(bond->vlgrp, vid);
			slave_dev->vlan_rx_kill_vid(slave_dev, vid);
			vlan_group_set_device(bond->vlgrp, vid, vlan_dev);
		}
	}

	res = bond_del_vlan(bond, vid);
	if (res) {
		printk(KERN_ERR DRV_NAME
		       ": %s: Error: Failed to remove vlan id %d\n",
		       bond_dev->name, vid);
	}
}

static void bond_add_vlans_on_slave(struct bonding *bond, struct net_device *slave_dev)
{
	struct vlan_entry *vlan;

	write_lock_bh(&bond->lock);

	if (list_empty(&bond->vlan_list)) {
		goto out;
	}

	if ((slave_dev->features & NETIF_F_HW_VLAN_RX) &&
	    slave_dev->vlan_rx_register) {
		slave_dev->vlan_rx_register(slave_dev, bond->vlgrp);
	}

	if (!(slave_dev->features & NETIF_F_HW_VLAN_FILTER) ||
	    !(slave_dev->vlan_rx_add_vid)) {
		goto out;
	}

	list_for_each_entry(vlan, &bond->vlan_list, vlan_list) {
		slave_dev->vlan_rx_add_vid(slave_dev, vlan->vlan_id);
	}

out:
	write_unlock_bh(&bond->lock);
}

static void bond_del_vlans_from_slave(struct bonding *bond, struct net_device *slave_dev)
{
	struct vlan_entry *vlan;
	struct net_device *vlan_dev;

	write_lock_bh(&bond->lock);

	if (list_empty(&bond->vlan_list)) {
		goto out;
	}

	if (!(slave_dev->features & NETIF_F_HW_VLAN_FILTER) ||
	    !(slave_dev->vlan_rx_kill_vid)) {
		goto unreg;
	}

	list_for_each_entry(vlan, &bond->vlan_list, vlan_list) {
		/* Save and then restore vlan_dev in the grp array,
		 * since the slave's driver might clear it.
		 */
		vlan_dev = vlan_group_get_device(bond->vlgrp, vlan->vlan_id);
		slave_dev->vlan_rx_kill_vid(slave_dev, vlan->vlan_id);
		vlan_group_set_device(bond->vlgrp, vlan->vlan_id, vlan_dev);
	}

unreg:
	if ((slave_dev->features & NETIF_F_HW_VLAN_RX) &&
	    slave_dev->vlan_rx_register) {
		slave_dev->vlan_rx_register(slave_dev, NULL);
	}

out:
	write_unlock_bh(&bond->lock);
static int bond_vlan_rx_kill_vid(struct net_device *bond_dev,
				 __be16 proto, u16 vid)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct list_head *iter;
	struct slave *slave;

	bond_for_each_slave(bond, slave, iter)
		vlan_vid_del(slave->dev, proto, vid);

	if (bond_is_lb(bond))
		bond_alb_clear_vlan(bond, vid);

	return 0;
}

/*------------------------------- Link status -------------------------------*/

/*
 * Set the carrier state for the master according to the state of its
/* Set the carrier state for the master according to the state of its
 * slaves.  If any slaves are up, the master is up.  In 802.3ad mode,
 * do special 802.3ad magic.
 *
 * Returns zero if carrier state does not change, nonzero if it does.
 */
static int bond_set_carrier(struct bonding *bond)
{
	struct slave *slave;
	int i;

	if (bond->slave_cnt == 0)
		goto down;

	if (bond->params.mode == BOND_MODE_8023AD)
		return bond_3ad_set_carrier(bond);

	bond_for_each_slave(bond, slave, i) {
int bond_set_carrier(struct bonding *bond)
{
	struct list_head *iter;
	struct slave *slave;

	if (!bond_has_slaves(bond))
		goto down;

	if (BOND_MODE(bond) == BOND_MODE_8023AD)
		return bond_3ad_set_carrier(bond);

	bond_for_each_slave(bond, slave, iter) {
		if (slave->link == BOND_LINK_UP) {
			if (!netif_carrier_ok(bond->dev)) {
				netif_carrier_on(bond->dev);
				return 1;
			}
			return 0;
		}
	}

down:
	if (netif_carrier_ok(bond->dev)) {
		netif_carrier_off(bond->dev);
		return 1;
	}
	return 0;
}

/*
 * Get link speed and duplex from the slave's base driver
 * using ethtool. If for some reason the call fails or the
 * values are invalid, fake speed and duplex to 100/Full
 * and return error.
 */
static int bond_update_speed_duplex(struct slave *slave)
{
	struct net_device *slave_dev = slave->dev;
	struct ethtool_cmd etool;
	int res;

	/* Fake speed and duplex */
	slave->speed = SPEED_100;
	slave->duplex = DUPLEX_FULL;

	if (!slave_dev->ethtool_ops || !slave_dev->ethtool_ops->get_settings)
		return -1;

	res = slave_dev->ethtool_ops->get_settings(slave_dev, &etool);
	if (res < 0)
		return -1;

	switch (etool.speed) {
	case SPEED_10:
	case SPEED_100:
	case SPEED_1000:
	case SPEED_10000:
		break;
	default:
		return -1;
	}

	switch (etool.duplex) {
/* Get link speed and duplex from the slave's base driver
 * using ethtool. If for some reason the call fails or the
 * values are invalid, set speed and duplex to -1,
 * and return.
 */
static void bond_update_speed_duplex(struct slave *slave)
{
	struct net_device *slave_dev = slave->dev;
	struct ethtool_cmd ecmd;
	u32 slave_speed;
	int res;

	slave->speed = SPEED_UNKNOWN;
	slave->duplex = DUPLEX_UNKNOWN;

	res = __ethtool_get_settings(slave_dev, &ecmd);
	if (res < 0)
		return;

	slave_speed = ethtool_cmd_speed(&ecmd);
	if (slave_speed == 0 || slave_speed == ((__u32) -1))
		return;

	switch (ecmd.duplex) {
	case DUPLEX_FULL:
	case DUPLEX_HALF:
		break;
	default:
		return -1;
	}

	slave->speed = etool.speed;
	slave->duplex = etool.duplex;

	return 0;
}

/*
 * if <dev> supports MII link status reporting, check its link status.
 *
 * We either do MII/ETHTOOL ioctls, or check netif_carrier_ok(),
 * depening upon the setting of the use_carrier parameter.
		return;
	}

	slave->speed = slave_speed;
	slave->duplex = ecmd.duplex;

	return;
}

const char *bond_slave_link_status(s8 link)
{
	switch (link) {
	case BOND_LINK_UP:
		return "up";
	case BOND_LINK_FAIL:
		return "going down";
	case BOND_LINK_DOWN:
		return "down";
	case BOND_LINK_BACK:
		return "going back";
	default:
		return "unknown";
	}
}

/* if <dev> supports MII link status reporting, check its link status.
 *
 * We either do MII/ETHTOOL ioctls, or check netif_carrier_ok(),
 * depending upon the setting of the use_carrier parameter.
 *
 * Return either BMSR_LSTATUS, meaning that the link is up (or we
 * can't tell and just pretend it is), or 0, meaning that the link is
 * down.
 *
 * If reporting is non-zero, instead of faking link up, return -1 if
 * both ETHTOOL and MII ioctls fail (meaning the device does not
 * support them).  If use_carrier is set, return whatever it says.
 * It'd be nice if there was a good way to tell if a driver supports
 * netif_carrier, but there really isn't.
 */
static int bond_check_dev_link(struct bonding *bond, struct net_device *slave_dev, int reporting)
{
	static int (* ioctl)(struct net_device *, struct ifreq *, int);
	struct ifreq ifr;
	struct mii_ioctl_data *mii;

	if (bond->params.use_carrier) {
		return netif_carrier_ok(slave_dev) ? BMSR_LSTATUS : 0;
	}

	ioctl = slave_dev->do_ioctl;
	if (ioctl) {
		/* TODO: set pointer to correct ioctl on a per team member */
		/*       bases to make this more efficient. that is, once  */
		/*       we determine the correct ioctl, we will always    */
		/*       call it and not the others for that team          */
		/*       member.                                           */

		/*
		 * We cannot assume that SIOCGMIIPHY will also read a
static int bond_check_dev_link(struct bonding *bond,
			       struct net_device *slave_dev, int reporting)
{
	const struct net_device_ops *slave_ops = slave_dev->netdev_ops;
	int (*ioctl)(struct net_device *, struct ifreq *, int);
	struct ifreq ifr;
	struct mii_ioctl_data *mii;

	if (!reporting && !netif_running(slave_dev))
		return 0;

	if (bond->params.use_carrier)
		return netif_carrier_ok(slave_dev) ? BMSR_LSTATUS : 0;

	/* Try to get link status using Ethtool first. */
	if (slave_dev->ethtool_ops->get_link)
		return slave_dev->ethtool_ops->get_link(slave_dev) ?
			BMSR_LSTATUS : 0;

	/* Ethtool can't be used, fallback to MII ioctls. */
	ioctl = slave_ops->ndo_do_ioctl;
	if (ioctl) {
		/* TODO: set pointer to correct ioctl on a per team member
		 *       bases to make this more efficient. that is, once
		 *       we determine the correct ioctl, we will always
		 *       call it and not the others for that team
		 *       member.
		 */

		/* We cannot assume that SIOCGMIIPHY will also read a
		 * register; not all network drivers (e.g., e100)
		 * support that.
		 */

		/* Yes, the mii is overlaid on the ifreq.ifr_ifru */
		strncpy(ifr.ifr_name, slave_dev->name, IFNAMSIZ);
		mii = if_mii(&ifr);
		if (IOCTL(slave_dev, &ifr, SIOCGMIIPHY) == 0) {
			mii->reg_num = MII_BMSR;
			if (IOCTL(slave_dev, &ifr, SIOCGMIIREG) == 0) {
				return (mii->val_out & BMSR_LSTATUS);
			}
		}
	}

	/*
	 * Some drivers cache ETHTOOL_GLINK for a period of time so we only
	 * attempt to get link status from it if the above MII ioctls fail.
	 */
	if (slave_dev->ethtool_ops) {
		if (slave_dev->ethtool_ops->get_link) {
			u32 link;

			link = slave_dev->ethtool_ops->get_link(slave_dev);

			return link ? BMSR_LSTATUS : 0;
		}
	}

	/*
	 * If reporting, report that either there's no dev->do_ioctl,
			if (IOCTL(slave_dev, &ifr, SIOCGMIIREG) == 0)
				return mii->val_out & BMSR_LSTATUS;
		}
	}

	/* If reporting, report that either there's no dev->do_ioctl,
	 * or both SIOCGMIIREG and get_link failed (meaning that we
	 * cannot report link status).  If not reporting, pretend
	 * we're ok.
	 */
	return (reporting ? -1 : BMSR_LSTATUS);
	return reporting ? -1 : BMSR_LSTATUS;
}

/*----------------------------- Multicast list ------------------------------*/

/*
 * Returns 0 if dmi1 and dmi2 are the same, non-0 otherwise
 */
static inline int bond_is_dmi_same(struct dev_mc_list *dmi1, struct dev_mc_list *dmi2)
{
	return memcmp(dmi1->dmi_addr, dmi2->dmi_addr, dmi1->dmi_addrlen) == 0 &&
			dmi1->dmi_addrlen == dmi2->dmi_addrlen;
}

/*
 * returns dmi entry if found, NULL otherwise
 */
static struct dev_mc_list *bond_mc_list_find_dmi(struct dev_mc_list *dmi, struct dev_mc_list *mc_list)
{
	struct dev_mc_list *idmi;

	for (idmi = mc_list; idmi; idmi = idmi->next) {
		if (bond_is_dmi_same(dmi, idmi)) {
			return idmi;
		}
	}

	return NULL;
}

/*
 * Push the promiscuity flag down to appropriate slaves
 */
static int bond_set_promiscuity(struct bonding *bond, int inc)
{
	int err = 0;
	if (USES_PRIMARY(bond->params.mode)) {
		/* write lock already acquired */
		if (bond->curr_active_slave) {
			err = dev_set_promiscuity(bond->curr_active_slave->dev,
						  inc);
		}
	} else {
		struct slave *slave;
		int i;
		bond_for_each_slave(bond, slave, i) {
/* Push the promiscuity flag down to appropriate slaves */
static int bond_set_promiscuity(struct bonding *bond, int inc)
{
	struct list_head *iter;
	int err = 0;

	if (bond_uses_primary(bond)) {
		struct slave *curr_active = rtnl_dereference(bond->curr_active_slave);

		if (curr_active)
			err = dev_set_promiscuity(curr_active->dev, inc);
	} else {
		struct slave *slave;

		bond_for_each_slave(bond, slave, iter) {
			err = dev_set_promiscuity(slave->dev, inc);
			if (err)
				return err;
		}
	}
	return err;
}

/*
 * Push the allmulti flag down to all slaves
 */
static int bond_set_allmulti(struct bonding *bond, int inc)
{
	int err = 0;
	if (USES_PRIMARY(bond->params.mode)) {
		/* write lock already acquired */
		if (bond->curr_active_slave) {
			err = dev_set_allmulti(bond->curr_active_slave->dev,
					       inc);
		}
	} else {
		struct slave *slave;
		int i;
		bond_for_each_slave(bond, slave, i) {
/* Push the allmulti flag down to all slaves */
static int bond_set_allmulti(struct bonding *bond, int inc)
{
	struct list_head *iter;
	int err = 0;

	if (bond_uses_primary(bond)) {
		struct slave *curr_active = rtnl_dereference(bond->curr_active_slave);

		if (curr_active)
			err = dev_set_allmulti(curr_active->dev, inc);
	} else {
		struct slave *slave;

		bond_for_each_slave(bond, slave, iter) {
			err = dev_set_allmulti(slave->dev, inc);
			if (err)
				return err;
		}
	}
	return err;
}

/*
 * Add a Multicast address to slaves
 * according to mode
 */
static void bond_mc_add(struct bonding *bond, void *addr, int alen)
{
	if (USES_PRIMARY(bond->params.mode)) {
		/* write lock already acquired */
		if (bond->curr_active_slave) {
			dev_mc_add(bond->curr_active_slave->dev, addr, alen, 0);
		}
	} else {
		struct slave *slave;
		int i;
		bond_for_each_slave(bond, slave, i) {
			dev_mc_add(slave->dev, addr, alen, 0);
		}
	}
}

/*
 * Remove a multicast address from slave
 * according to mode
 */
static void bond_mc_delete(struct bonding *bond, void *addr, int alen)
{
	if (USES_PRIMARY(bond->params.mode)) {
		/* write lock already acquired */
		if (bond->curr_active_slave) {
			dev_mc_delete(bond->curr_active_slave->dev, addr, alen, 0);
		}
	} else {
		struct slave *slave;
		int i;
		bond_for_each_slave(bond, slave, i) {
			dev_mc_delete(slave->dev, addr, alen, 0);
		}
	}
}


/*
 * Retrieve the list of registered multicast addresses for the bonding
 * device and retransmit an IGMP JOIN request to the current active
 * slave.
 */
static void bond_resend_igmp_join_requests(struct bonding *bond)
{
	struct in_device *in_dev;
	struct ip_mc_list *im;

	rcu_read_lock();
	in_dev = __in_dev_get_rcu(bond->dev);
	if (in_dev) {
		for (im = in_dev->mc_list; im; im = im->next) {
			ip_mc_rejoin_group(im);
		}
	}

	rcu_read_unlock();
}

/*
 * Totally destroys the mc_list in bond
 */
static void bond_mc_list_destroy(struct bonding *bond)
{
	struct dev_mc_list *dmi;

	dmi = bond->mc_list;
	while (dmi) {
		bond->mc_list = dmi->next;
		kfree(dmi);
		dmi = bond->mc_list;
	}
        bond->mc_list = NULL;
}

/*
 * Copy all the Multicast addresses from src to the bonding device dst
 */
static int bond_mc_list_copy(struct dev_mc_list *mc_list, struct bonding *bond,
			     gfp_t gfp_flag)
{
	struct dev_mc_list *dmi, *new_dmi;

	for (dmi = mc_list; dmi; dmi = dmi->next) {
		new_dmi = kmalloc(sizeof(struct dev_mc_list), gfp_flag);

		if (!new_dmi) {
			/* FIXME: Potential memory leak !!! */
			return -ENOMEM;
		}

		new_dmi->next = bond->mc_list;
		bond->mc_list = new_dmi;
		new_dmi->dmi_addrlen = dmi->dmi_addrlen;
		memcpy(new_dmi->dmi_addr, dmi->dmi_addr, dmi->dmi_addrlen);
		new_dmi->dmi_users = dmi->dmi_users;
		new_dmi->dmi_gusers = dmi->dmi_gusers;
	}

	return 0;
}

/*
 * flush all members of flush->mc_list from device dev->mc_list
 */
static void bond_mc_list_flush(struct net_device *bond_dev, struct net_device *slave_dev)
{
	struct bonding *bond = bond_dev->priv;
	struct dev_mc_list *dmi;

	for (dmi = bond_dev->mc_list; dmi; dmi = dmi->next) {
		dev_mc_delete(slave_dev, dmi->dmi_addr, dmi->dmi_addrlen, 0);
	}

	if (bond->params.mode == BOND_MODE_8023AD) {
		/* del lacpdu mc addr from mc list */
		u8 lacpdu_multicast[ETH_ALEN] = MULTICAST_LACPDU_ADDR;

		dev_mc_delete(slave_dev, lacpdu_multicast, ETH_ALEN, 0);
/* Retrieve the list of registered multicast addresses for the bonding
 * device and retransmit an IGMP JOIN request to the current active
 * slave.
 */
static void bond_resend_igmp_join_requests_delayed(struct work_struct *work)
{
	struct bonding *bond = container_of(work, struct bonding,
					    mcast_work.work);

	if (!rtnl_trylock()) {
		queue_delayed_work(bond->wq, &bond->mcast_work, 1);
		return;
	}
	call_netdevice_notifiers(NETDEV_RESEND_IGMP, bond->dev);

	if (bond->igmp_retrans > 1) {
		bond->igmp_retrans--;
		queue_delayed_work(bond->wq, &bond->mcast_work, HZ/5);
	}
	rtnl_unlock();
}

/* Flush bond's hardware addresses from slave */
static void bond_hw_addr_flush(struct net_device *bond_dev,
			       struct net_device *slave_dev)
{
	struct bonding *bond = netdev_priv(bond_dev);

	dev_uc_unsync(slave_dev, bond_dev);
	dev_mc_unsync(slave_dev, bond_dev);

	if (BOND_MODE(bond) == BOND_MODE_8023AD) {
		/* del lacpdu mc addr from mc list */
		u8 lacpdu_multicast[ETH_ALEN] = MULTICAST_LACPDU_ADDR;

		dev_mc_del(slave_dev, lacpdu_multicast);
	}
}

/*--------------------------- Active slave change ---------------------------*/

/*
 * Update the mc list and multicast-related flags for the new and
 * old active slaves (if any) according to the multicast mode, and
 * promiscuous flags unconditionally.
 */
static void bond_mc_swap(struct bonding *bond, struct slave *new_active, struct slave *old_active)
{
	struct dev_mc_list *dmi;

	if (!USES_PRIMARY(bond->params.mode)) {
		/* nothing to do -  mc list is already up-to-date on
		 * all slaves
		 */
		return;
	}

	if (old_active) {
		if (bond->dev->flags & IFF_PROMISC) {
			dev_set_promiscuity(old_active->dev, -1);
		}

		if (bond->dev->flags & IFF_ALLMULTI) {
			dev_set_allmulti(old_active->dev, -1);
		}

		for (dmi = bond->dev->mc_list; dmi; dmi = dmi->next) {
			dev_mc_delete(old_active->dev, dmi->dmi_addr, dmi->dmi_addrlen, 0);
		}
/* Update the hardware address list and promisc/allmulti for the new and
 * old active slaves (if any).  Modes that are not using primary keep all
 * slaves up date at all times; only the modes that use primary need to call
 * this function to swap these settings during a failover.
 */
static void bond_hw_addr_swap(struct bonding *bond, struct slave *new_active,
			      struct slave *old_active)
{
	if (old_active) {
		if (bond->dev->flags & IFF_PROMISC)
			dev_set_promiscuity(old_active->dev, -1);

		if (bond->dev->flags & IFF_ALLMULTI)
			dev_set_allmulti(old_active->dev, -1);

		bond_hw_addr_flush(bond->dev, old_active->dev);
	}

	if (new_active) {
		/* FIXME: Signal errors upstream. */
		if (bond->dev->flags & IFF_PROMISC) {
			dev_set_promiscuity(new_active->dev, 1);
		}

		if (bond->dev->flags & IFF_ALLMULTI) {
			dev_set_allmulti(new_active->dev, 1);
		}

		for (dmi = bond->dev->mc_list; dmi; dmi = dmi->next) {
			dev_mc_add(new_active->dev, dmi->dmi_addr, dmi->dmi_addrlen, 0);
		}
		bond_resend_igmp_join_requests(bond);
	}
}

/*
 * bond_do_fail_over_mac
 *
 * Perform special MAC address swapping for fail_over_mac settings
 *
 * Called with RTNL, bond->lock for read, curr_slave_lock for write_bh.
		if (bond->dev->flags & IFF_PROMISC)
			dev_set_promiscuity(new_active->dev, 1);

		if (bond->dev->flags & IFF_ALLMULTI)
			dev_set_allmulti(new_active->dev, 1);

		netif_addr_lock_bh(bond->dev);
		dev_uc_sync(new_active->dev, bond->dev);
		dev_mc_sync(new_active->dev, bond->dev);
		netif_addr_unlock_bh(bond->dev);
	}
}

/**
 * bond_set_dev_addr - clone slave's address to bond
 * @bond_dev: bond net device
 * @slave_dev: slave net device
 *
 * Should be called with RTNL held.
 */
static void bond_set_dev_addr(struct net_device *bond_dev,
			      struct net_device *slave_dev)
{
	netdev_dbg(bond_dev, "bond_dev=%p slave_dev=%p slave_dev->addr_len=%d\n",
		   bond_dev, slave_dev, slave_dev->addr_len);
	memcpy(bond_dev->dev_addr, slave_dev->dev_addr, slave_dev->addr_len);
	bond_dev->addr_assign_type = NET_ADDR_STOLEN;
	call_netdevice_notifiers(NETDEV_CHANGEADDR, bond_dev);
}

static struct slave *bond_get_old_active(struct bonding *bond,
					 struct slave *new_active)
{
	struct slave *slave;
	struct list_head *iter;

	bond_for_each_slave(bond, slave, iter) {
		if (slave == new_active)
			continue;

		if (ether_addr_equal(bond->dev->dev_addr, slave->dev->dev_addr))
			return slave;
	}

	return NULL;
}

/* bond_do_fail_over_mac
 *
 * Perform special MAC address swapping for fail_over_mac settings
 *
 * Called with RTNL
 */
static void bond_do_fail_over_mac(struct bonding *bond,
				  struct slave *new_active,
				  struct slave *old_active)
{
	u8 tmp_mac[ETH_ALEN];
	struct sockaddr saddr;
	int rv;

	switch (bond->params.fail_over_mac) {
	case BOND_FOM_ACTIVE:
		if (new_active)
			memcpy(bond->dev->dev_addr,  new_active->dev->dev_addr,
			       new_active->dev->addr_len);
		break;
	case BOND_FOM_FOLLOW:
		/*
		 * if new_active && old_active, swap them
			bond_set_dev_addr(bond->dev, new_active->dev);
		break;
	case BOND_FOM_FOLLOW:
		/* if new_active && old_active, swap them
		 * if just old_active, do nothing (going to no active slave)
		 * if just new_active, set new_active to bond's MAC
		 */
		if (!new_active)
			return;

		write_unlock_bh(&bond->curr_slave_lock);
		read_unlock(&bond->lock);

		if (old_active) {
			memcpy(tmp_mac, new_active->dev->dev_addr, ETH_ALEN);
			memcpy(saddr.sa_data, old_active->dev->dev_addr,
			       ETH_ALEN);
			saddr.sa_family = new_active->dev->type;
		} else {
			memcpy(saddr.sa_data, bond->dev->dev_addr, ETH_ALEN);
		if (!old_active)
			old_active = bond_get_old_active(bond, new_active);

		if (old_active) {
			ether_addr_copy(tmp_mac, new_active->dev->dev_addr);
			ether_addr_copy(saddr.sa_data,
					old_active->dev->dev_addr);
			saddr.sa_family = new_active->dev->type;
		} else {
			ether_addr_copy(saddr.sa_data, bond->dev->dev_addr);
			saddr.sa_family = bond->dev->type;
		}

		rv = dev_set_mac_address(new_active->dev, &saddr);
		if (rv) {
			printk(KERN_ERR DRV_NAME
			       ": %s: Error %d setting MAC of slave %s\n",
			       bond->dev->name, -rv, new_active->dev->name);
			netdev_err(bond->dev, "Error %d setting MAC of slave %s\n",
				   -rv, new_active->dev->name);
			goto out;
		}

		if (!old_active)
			goto out;

		memcpy(saddr.sa_data, tmp_mac, ETH_ALEN);
		ether_addr_copy(saddr.sa_data, tmp_mac);
		saddr.sa_family = old_active->dev->type;

		rv = dev_set_mac_address(old_active->dev, &saddr);
		if (rv)
			printk(KERN_ERR DRV_NAME
			       ": %s: Error %d setting MAC of slave %s\n",
			       bond->dev->name, -rv, new_active->dev->name);
out:
		read_lock(&bond->lock);
		write_lock_bh(&bond->curr_slave_lock);
		break;
	default:
		printk(KERN_ERR DRV_NAME
		       ": %s: bond_do_fail_over_mac impossible: bad policy %d\n",
		       bond->dev->name, bond->params.fail_over_mac);
			netdev_err(bond->dev, "Error %d setting MAC of slave %s\n",
				   -rv, new_active->dev->name);
out:
		break;
	default:
		netdev_err(bond->dev, "bond_do_fail_over_mac impossible: bad policy %d\n",
			   bond->params.fail_over_mac);
		break;
	}

}


/**
 * find_best_interface - select the best available slave to be the active one
 * @bond: our bonding struct
 *
 * Warning: Caller must hold curr_slave_lock for writing.
 */
static struct slave *bond_find_best_slave(struct bonding *bond)
{
	struct slave *new_active, *old_active;
	struct slave *bestslave = NULL;
	int mintime = bond->params.updelay;
	int i;

	new_active = old_active = bond->curr_active_slave;

	if (!new_active) { /* there were no active slaves left */
		if (bond->slave_cnt > 0) {  /* found one slave */
			new_active = bond->first_slave;
		} else {
			return NULL; /* still no slave, return NULL */
		}
	}

	/* first try the primary link; if arping, a link must tx/rx traffic
	 * before it can be considered the curr_active_slave - also, we would skip
	 * slaves between the curr_active_slave and primary_slave that may be up
	 * and able to arp
	 */
	if ((bond->primary_slave) &&
	    (!bond->params.arp_interval) &&
	    (IS_UP(bond->primary_slave->dev))) {
		new_active = bond->primary_slave;
	}

	/* remember where to stop iterating over the slaves */
	old_active = new_active;

	bond_for_each_slave_from(bond, new_active, i, old_active) {
		if (IS_UP(new_active->dev)) {
			if (new_active->link == BOND_LINK_UP) {
				return new_active;
			} else if (new_active->link == BOND_LINK_BACK) {
				/* link up, but waiting for stabilization */
				if (new_active->delay < mintime) {
					mintime = new_active->delay;
					bestslave = new_active;
				}
			}
static struct slave *bond_choose_primary_or_current(struct bonding *bond)
{
	struct slave *prim = rtnl_dereference(bond->primary_slave);
	struct slave *curr = rtnl_dereference(bond->curr_active_slave);

	if (!prim || prim->link != BOND_LINK_UP) {
		if (!curr || curr->link != BOND_LINK_UP)
			return NULL;
		return curr;
	}

	if (bond->force_primary) {
		bond->force_primary = false;
		return prim;
	}

	if (!curr || curr->link != BOND_LINK_UP)
		return prim;

	/* At this point, prim and curr are both up */
	switch (bond->params.primary_reselect) {
	case BOND_PRI_RESELECT_ALWAYS:
		return prim;
	case BOND_PRI_RESELECT_BETTER:
		if (prim->speed < curr->speed)
			return curr;
		if (prim->speed == curr->speed && prim->duplex <= curr->duplex)
			return curr;
		return prim;
	case BOND_PRI_RESELECT_FAILURE:
		return curr;
	default:
		netdev_err(bond->dev, "impossible primary_reselect %d\n",
			   bond->params.primary_reselect);
		return curr;
	}
}

/**
 * bond_find_best_slave - select the best available slave to be the active one
 * @bond: our bonding struct
 */
static struct slave *bond_find_best_slave(struct bonding *bond)
{
	struct slave *slave, *bestslave = NULL;
	struct list_head *iter;
	int mintime = bond->params.updelay;

	slave = bond_choose_primary_or_current(bond);
	if (slave)
		return slave;

	bond_for_each_slave(bond, slave, iter) {
		if (slave->link == BOND_LINK_UP)
			return slave;
		if (slave->link == BOND_LINK_BACK && bond_slave_is_up(slave) &&
		    slave->delay < mintime) {
			mintime = slave->delay;
			bestslave = slave;
		}
	}

	return bestslave;
}

/* must be called in RCU critical section or with RTNL held */
static bool bond_should_notify_peers(struct bonding *bond)
{
	struct slave *slave = rcu_dereference_rtnl(bond->curr_active_slave);

	if (!slave || !bond->send_peer_notif ||
	    !netif_carrier_ok(bond->dev) ||
	    test_bit(__LINK_STATE_LINKWATCH_PENDING, &slave->dev->state))
		return false;

	netdev_dbg(bond->dev, "bond_should_notify_peers: slave %s\n",
		   slave ? slave->dev->name : "NULL");

	return true;
}

/**
 * change_active_interface - change the active slave into the specified one
 * @bond: our bonding struct
 * @new: the new slave to make the active one
 *
 * Set the new slave to the bond's settings and unset them on the old
 * curr_active_slave.
 * Setting include flags, mc-list, promiscuity, allmulti, etc.
 *
 * If @new's link state is %BOND_LINK_BACK we'll set it to %BOND_LINK_UP,
 * because it is apparently the best available slave we have, even though its
 * updelay hasn't timed out yet.
 *
 * If new_active is not NULL, caller must hold bond->lock for read and
 * curr_slave_lock for write_bh.
 */
void bond_change_active_slave(struct bonding *bond, struct slave *new_active)
{
	struct slave *old_active = bond->curr_active_slave;

	if (old_active == new_active) {
		return;
	}

	if (new_active) {
		new_active->jiffies = jiffies;

		if (new_active->link == BOND_LINK_BACK) {
			if (USES_PRIMARY(bond->params.mode)) {
				printk(KERN_INFO DRV_NAME
				       ": %s: making interface %s the new "
				       "active one %d ms earlier.\n",
				       bond->dev->name, new_active->dev->name,
				       (bond->params.updelay - new_active->delay) * bond->params.miimon);
			}

			new_active->delay = 0;
			new_active->link = BOND_LINK_UP;

			if (bond->params.mode == BOND_MODE_8023AD) {
				bond_3ad_handle_link_change(new_active, BOND_LINK_UP);
			}

			if ((bond->params.mode == BOND_MODE_TLB) ||
			    (bond->params.mode == BOND_MODE_ALB)) {
				bond_alb_handle_link_change(bond, new_active, BOND_LINK_UP);
			}
		} else {
			if (USES_PRIMARY(bond->params.mode)) {
				printk(KERN_INFO DRV_NAME
				       ": %s: making interface %s the new "
				       "active one.\n",
				       bond->dev->name, new_active->dev->name);
 * Caller must hold RTNL.
 */
void bond_change_active_slave(struct bonding *bond, struct slave *new_active)
{
	struct slave *old_active;

	ASSERT_RTNL();

	old_active = rtnl_dereference(bond->curr_active_slave);

	if (old_active == new_active)
		return;

	if (new_active) {
		new_active->last_link_up = jiffies;

		if (new_active->link == BOND_LINK_BACK) {
			if (bond_uses_primary(bond)) {
				netdev_info(bond->dev, "making interface %s the new active one %d ms earlier\n",
					    new_active->dev->name,
					    (bond->params.updelay - new_active->delay) * bond->params.miimon);
			}

			new_active->delay = 0;
			bond_set_slave_link_state(new_active, BOND_LINK_UP);

			if (BOND_MODE(bond) == BOND_MODE_8023AD)
				bond_3ad_handle_link_change(new_active, BOND_LINK_UP);

			if (bond_is_lb(bond))
				bond_alb_handle_link_change(bond, new_active, BOND_LINK_UP);
		} else {
			if (bond_uses_primary(bond)) {
				netdev_info(bond->dev, "making interface %s the new active one\n",
					    new_active->dev->name);
			}
		}
	}

	if (USES_PRIMARY(bond->params.mode)) {
		bond_mc_swap(bond, new_active, old_active);
	}

	if ((bond->params.mode == BOND_MODE_TLB) ||
	    (bond->params.mode == BOND_MODE_ALB)) {
		bond_alb_handle_active_change(bond, new_active);
		if (old_active)
			bond_set_slave_inactive_flags(old_active);
		if (new_active)
			bond_set_slave_active_flags(new_active);
	} else {
		bond->curr_active_slave = new_active;
	}

	if (bond->params.mode == BOND_MODE_ACTIVEBACKUP) {
		if (old_active) {
			bond_set_slave_inactive_flags(old_active);
		}

		if (new_active) {
			bond_set_slave_active_flags(new_active);
	if (bond_uses_primary(bond))
		bond_hw_addr_swap(bond, new_active, old_active);

	if (bond_is_lb(bond)) {
		bond_alb_handle_active_change(bond, new_active);
		if (old_active)
			bond_set_slave_inactive_flags(old_active,
						      BOND_SLAVE_NOTIFY_NOW);
		if (new_active)
			bond_set_slave_active_flags(new_active,
						    BOND_SLAVE_NOTIFY_NOW);
	} else {
		rcu_assign_pointer(bond->curr_active_slave, new_active);
	}

	if (BOND_MODE(bond) == BOND_MODE_ACTIVEBACKUP) {
		if (old_active)
			bond_set_slave_inactive_flags(old_active,
						      BOND_SLAVE_NOTIFY_NOW);

		if (new_active) {
			bool should_notify_peers = false;

			bond_set_slave_active_flags(new_active,
						    BOND_SLAVE_NOTIFY_NOW);

			if (bond->params.fail_over_mac)
				bond_do_fail_over_mac(bond, new_active,
						      old_active);

			bond->send_grat_arp = bond->params.num_grat_arp;
			bond_send_gratuitous_arp(bond);

			write_unlock_bh(&bond->curr_slave_lock);
			read_unlock(&bond->lock);

			netdev_bonding_change(bond->dev);

			read_lock(&bond->lock);
			write_lock_bh(&bond->curr_slave_lock);
		}
	}
			if (netif_running(bond->dev)) {
				bond->send_peer_notif =
					bond->params.num_peer_notif;
				should_notify_peers =
					bond_should_notify_peers(bond);
			}

			call_netdevice_notifiers(NETDEV_BONDING_FAILOVER, bond->dev);
			if (should_notify_peers)
				call_netdevice_notifiers(NETDEV_NOTIFY_PEERS,
							 bond->dev);
		}
	}

	/* resend IGMP joins since active slave has changed or
	 * all were sent on curr_active_slave.
	 * resend only if bond is brought up with the affected
	 * bonding modes and the retransmission is enabled
	 */
	if (netif_running(bond->dev) && (bond->params.resend_igmp > 0) &&
	    ((bond_uses_primary(bond) && new_active) ||
	     BOND_MODE(bond) == BOND_MODE_ROUNDROBIN)) {
		bond->igmp_retrans = bond->params.resend_igmp;
		queue_delayed_work(bond->wq, &bond->mcast_work, 1);
	}
}

/**
 * bond_select_active_slave - select a new active slave, if needed
 * @bond: our bonding struct
 *
 * This functions shoud be called when one of the following occurs:
 * This functions should be called when one of the following occurs:
 * - The old curr_active_slave has been released or lost its link.
 * - The primary_slave has got its link back.
 * - A slave has got its link back and there's no old curr_active_slave.
 *
 * Caller must hold bond->lock for read and curr_slave_lock for write_bh.
 * Caller must hold RTNL.
 */
void bond_select_active_slave(struct bonding *bond)
{
	struct slave *best_slave;
	int rv;

	best_slave = bond_find_best_slave(bond);
	if (best_slave != bond->curr_active_slave) {
	ASSERT_RTNL();

	best_slave = bond_find_best_slave(bond);
	if (best_slave != rtnl_dereference(bond->curr_active_slave)) {
		bond_change_active_slave(bond, best_slave);
		rv = bond_set_carrier(bond);
		if (!rv)
			return;

		if (netif_carrier_ok(bond->dev)) {
			printk(KERN_INFO DRV_NAME
			       ": %s: first active interface up!\n",
			       bond->dev->name);
		} else {
			printk(KERN_INFO DRV_NAME ": %s: "
			       "now running without any active interface !\n",
			       bond->dev->name);
			netdev_info(bond->dev, "first active interface up!\n");
		} else {
			netdev_info(bond->dev, "now running without any active interface!\n");
		}
	}
}

/*--------------------------- slave list handling ---------------------------*/

/*
 * This function attaches the slave to the end of list.
 *
 * bond->lock held for writing by caller.
 */
static void bond_attach_slave(struct bonding *bond, struct slave *new_slave)
{
	if (bond->first_slave == NULL) { /* attaching the first slave */
		new_slave->next = new_slave;
		new_slave->prev = new_slave;
		bond->first_slave = new_slave;
	} else {
		new_slave->next = bond->first_slave;
		new_slave->prev = bond->first_slave->prev;
		new_slave->next->prev = new_slave;
		new_slave->prev->next = new_slave;
	}

	bond->slave_cnt++;
}

/*
 * This function detaches the slave from the list.
 * WARNING: no check is made to verify if the slave effectively
 * belongs to <bond>.
 * Nothing is freed on return, structures are just unchained.
 * If any slave pointer in bond was pointing to <slave>,
 * it should be changed by the calling function.
 *
 * bond->lock held for writing by caller.
 */
static void bond_detach_slave(struct bonding *bond, struct slave *slave)
{
	if (slave->next) {
		slave->next->prev = slave->prev;
	}

	if (slave->prev) {
		slave->prev->next = slave->next;
	}

	if (bond->first_slave == slave) { /* slave is the first slave */
		if (bond->slave_cnt > 1) { /* there are more slave */
			bond->first_slave = slave->next;
		} else {
			bond->first_slave = NULL; /* slave was the last one */
		}
	}

	slave->next = NULL;
	slave->prev = NULL;
	bond->slave_cnt--;
}

/*---------------------------------- IOCTL ----------------------------------*/

static int bond_sethwaddr(struct net_device *bond_dev,
			  struct net_device *slave_dev)
{
	dprintk("bond_dev=%p\n", bond_dev);
	dprintk("slave_dev=%p\n", slave_dev);
	dprintk("slave_dev->addr_len=%d\n", slave_dev->addr_len);
	memcpy(bond_dev->dev_addr, slave_dev->dev_addr, slave_dev->addr_len);
	return 0;
}

#define BOND_VLAN_FEATURES \
	(NETIF_F_VLAN_CHALLENGED | NETIF_F_HW_VLAN_RX | NETIF_F_HW_VLAN_TX | \
	 NETIF_F_HW_VLAN_FILTER)

/* 
 * Compute the common dev->feature set available to all slaves.  Some
 * feature bits are managed elsewhere, so preserve those feature bits
 * on the master device.
 */
static int bond_compute_features(struct bonding *bond)
{
	struct slave *slave;
	struct net_device *bond_dev = bond->dev;
	unsigned long features = bond_dev->features;
	unsigned short max_hard_header_len = max((u16)ETH_HLEN,
						bond_dev->hard_header_len);
	int i;

	features &= ~(NETIF_F_ALL_CSUM | BOND_VLAN_FEATURES);
	features |= NETIF_F_SG | NETIF_F_FRAGLIST | NETIF_F_HIGHDMA |
		    NETIF_F_GSO_MASK | NETIF_F_NO_CSUM;

	bond_for_each_slave(bond, slave, i) {
		features = netdev_compute_features(features,
						   slave->dev->features);
		if (slave->dev->hard_header_len > max_hard_header_len)
			max_hard_header_len = slave->dev->hard_header_len;
	}

	features |= (bond_dev->features & BOND_VLAN_FEATURES);
	bond_dev->features = features;
	bond_dev->hard_header_len = max_hard_header_len;

	return 0;
}


static void bond_setup_by_slave(struct net_device *bond_dev,
				struct net_device *slave_dev)
{
	struct bonding *bond = bond_dev->priv;

	bond_dev->neigh_setup           = slave_dev->neigh_setup;
	bond_dev->header_ops		= slave_dev->header_ops;
#ifdef CONFIG_NET_POLL_CONTROLLER
static inline int slave_enable_netpoll(struct slave *slave)
{
	struct netpoll *np;
	int err = 0;

	np = kzalloc(sizeof(*np), GFP_KERNEL);
	err = -ENOMEM;
	if (!np)
		goto out;

	err = __netpoll_setup(np, slave->dev);
	if (err) {
		kfree(np);
		goto out;
	}
	slave->np = np;
out:
	return err;
}
static inline void slave_disable_netpoll(struct slave *slave)
{
	struct netpoll *np = slave->np;

	if (!np)
		return;

	slave->np = NULL;
	__netpoll_free_async(np);
}

static void bond_poll_controller(struct net_device *bond_dev)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct slave *slave = NULL;
	struct list_head *iter;
	struct ad_info ad_info;
	struct netpoll_info *ni;
	const struct net_device_ops *ops;

	if (BOND_MODE(bond) == BOND_MODE_8023AD)
		if (bond_3ad_get_active_agg_info(bond, &ad_info))
			return;

	bond_for_each_slave_rcu(bond, slave, iter) {
		ops = slave->dev->netdev_ops;
		if (!bond_slave_is_up(slave) || !ops->ndo_poll_controller)
			continue;

		if (BOND_MODE(bond) == BOND_MODE_8023AD) {
			struct aggregator *agg =
			    SLAVE_AD_INFO(slave)->port.aggregator;

			if (agg &&
			    agg->aggregator_identifier != ad_info.aggregator_id)
				continue;
		}

		ni = rcu_dereference_bh(slave->dev->npinfo);
		if (down_trylock(&ni->dev_lock))
			continue;
		ops->ndo_poll_controller(slave->dev);
		up(&ni->dev_lock);
	}
}

static void bond_netpoll_cleanup(struct net_device *bond_dev)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct list_head *iter;
	struct slave *slave;

	bond_for_each_slave(bond, slave, iter)
		if (bond_slave_is_up(slave))
			slave_disable_netpoll(slave);
}

static int bond_netpoll_setup(struct net_device *dev, struct netpoll_info *ni)
{
	struct bonding *bond = netdev_priv(dev);
	struct list_head *iter;
	struct slave *slave;
	int err = 0;

	bond_for_each_slave(bond, slave, iter) {
		err = slave_enable_netpoll(slave);
		if (err) {
			bond_netpoll_cleanup(dev);
			break;
		}
	}
	return err;
}
#else
static inline int slave_enable_netpoll(struct slave *slave)
{
	return 0;
}
static inline void slave_disable_netpoll(struct slave *slave)
{
}
static void bond_netpoll_cleanup(struct net_device *bond_dev)
{
}
#endif

/*---------------------------------- IOCTL ----------------------------------*/

static netdev_features_t bond_fix_features(struct net_device *dev,
					   netdev_features_t features)
{
	struct bonding *bond = netdev_priv(dev);
	struct list_head *iter;
	netdev_features_t mask;
	struct slave *slave;

	mask = features;

	features &= ~NETIF_F_ONE_FOR_ALL;
	features |= NETIF_F_ALL_FOR_ALL;

	bond_for_each_slave(bond, slave, iter) {
		features = netdev_increment_features(features,
						     slave->dev->features,
						     mask);
	}
	features = netdev_add_tso_features(features, mask);

	return features;
}

#define BOND_VLAN_FEATURES	(NETIF_F_ALL_CSUM | NETIF_F_SG | \
				 NETIF_F_FRAGLIST | NETIF_F_ALL_TSO | \
				 NETIF_F_HIGHDMA | NETIF_F_LRO)

#define BOND_ENC_FEATURES	(NETIF_F_ALL_CSUM | NETIF_F_SG | NETIF_F_RXCSUM |\
				 NETIF_F_ALL_TSO)

static void bond_compute_features(struct bonding *bond)
{
	unsigned int dst_release_flag = IFF_XMIT_DST_RELEASE |
					IFF_XMIT_DST_RELEASE_PERM;
	netdev_features_t vlan_features = BOND_VLAN_FEATURES;
	netdev_features_t enc_features  = BOND_ENC_FEATURES;
	struct net_device *bond_dev = bond->dev;
	struct list_head *iter;
	struct slave *slave;
	unsigned short max_hard_header_len = ETH_HLEN;
	unsigned int gso_max_size = GSO_MAX_SIZE;
	u16 gso_max_segs = GSO_MAX_SEGS;

	if (!bond_has_slaves(bond))
		goto done;
	vlan_features &= NETIF_F_ALL_FOR_ALL;

	bond_for_each_slave(bond, slave, iter) {
		vlan_features = netdev_increment_features(vlan_features,
			slave->dev->vlan_features, BOND_VLAN_FEATURES);

		enc_features = netdev_increment_features(enc_features,
							 slave->dev->hw_enc_features,
							 BOND_ENC_FEATURES);
		dst_release_flag &= slave->dev->priv_flags;
		if (slave->dev->hard_header_len > max_hard_header_len)
			max_hard_header_len = slave->dev->hard_header_len;

		gso_max_size = min(gso_max_size, slave->dev->gso_max_size);
		gso_max_segs = min(gso_max_segs, slave->dev->gso_max_segs);
	}
	bond_dev->hard_header_len = max_hard_header_len;

done:
	bond_dev->vlan_features = vlan_features;
	bond_dev->hw_enc_features = enc_features | NETIF_F_GSO_ENCAP_ALL |
				    NETIF_F_HW_VLAN_CTAG_TX |
				    NETIF_F_HW_VLAN_STAG_TX;
	bond_dev->gso_max_segs = gso_max_segs;
	netif_set_gso_max_size(bond_dev, gso_max_size);

	bond_dev->priv_flags &= ~IFF_XMIT_DST_RELEASE;
	if ((bond_dev->priv_flags & IFF_XMIT_DST_RELEASE_PERM) &&
	    dst_release_flag == (IFF_XMIT_DST_RELEASE | IFF_XMIT_DST_RELEASE_PERM))
		bond_dev->priv_flags |= IFF_XMIT_DST_RELEASE;

	netdev_change_features(bond_dev);
}

static void bond_setup_by_slave(struct net_device *bond_dev,
				struct net_device *slave_dev)
{
	bond_dev->header_ops	    = slave_dev->header_ops;

	bond_dev->type		    = slave_dev->type;
	bond_dev->hard_header_len   = slave_dev->hard_header_len;
	bond_dev->needed_headroom   = slave_dev->needed_headroom;
	bond_dev->addr_len	    = slave_dev->addr_len;

	memcpy(bond_dev->broadcast, slave_dev->broadcast,
		slave_dev->addr_len);
	bond->setup_by_slave = 1;

	if (slave_dev->flags & IFF_POINTOPOINT) {
		bond_dev->flags &= ~(IFF_BROADCAST | IFF_MULTICAST);
		bond_dev->flags |= (IFF_POINTOPOINT | IFF_NOARP);
	}
}

/* On bonding slaves other than the currently active slave, suppress
 * duplicates except for alb non-mcast/bcast.
 */
static bool bond_should_deliver_exact_match(struct sk_buff *skb,
					    struct slave *slave,
					    struct bonding *bond)
{
	if (bond_is_slave_inactive(slave)) {
		if (BOND_MODE(bond) == BOND_MODE_ALB &&
		    skb->pkt_type != PACKET_BROADCAST &&
		    skb->pkt_type != PACKET_MULTICAST)
			return false;
		return true;
	}
	return false;
}

static rx_handler_result_t bond_handle_frame(struct sk_buff **pskb)
{
	struct sk_buff *skb = *pskb;
	struct slave *slave;
	struct bonding *bond;
	int (*recv_probe)(const struct sk_buff *, struct bonding *,
			  struct slave *);
	int ret = RX_HANDLER_ANOTHER;

	skb = skb_share_check(skb, GFP_ATOMIC);
	if (unlikely(!skb))
		return RX_HANDLER_CONSUMED;

	*pskb = skb;

	slave = bond_slave_get_rcu(skb->dev);
	bond = slave->bond;

	recv_probe = ACCESS_ONCE(bond->recv_probe);
	if (recv_probe) {
		ret = recv_probe(skb, bond, slave);
		if (ret == RX_HANDLER_CONSUMED) {
			consume_skb(skb);
			return ret;
		}
	}

	if (bond_should_deliver_exact_match(skb, slave, bond)) {
		return RX_HANDLER_EXACT;
	}

	skb->dev = bond->dev;

	if (BOND_MODE(bond) == BOND_MODE_ALB &&
	    bond->dev->priv_flags & IFF_BRIDGE_PORT &&
	    skb->pkt_type == PACKET_HOST) {

		if (unlikely(skb_cow_head(skb,
					  skb->data - skb_mac_header(skb)))) {
			kfree_skb(skb);
			return RX_HANDLER_CONSUMED;
		}
		ether_addr_copy(eth_hdr(skb)->h_dest, bond->dev->dev_addr);
	}

	return ret;
}

static int bond_master_upper_dev_link(struct net_device *bond_dev,
				      struct net_device *slave_dev,
				      struct slave *slave)
{
	int err;

	err = netdev_master_upper_dev_link_private(slave_dev, bond_dev, slave);
	if (err)
		return err;
	rtmsg_ifinfo(RTM_NEWLINK, slave_dev, IFF_SLAVE, GFP_KERNEL);
	return 0;
}

static void bond_upper_dev_unlink(struct net_device *bond_dev,
				  struct net_device *slave_dev)
{
	netdev_upper_dev_unlink(slave_dev, bond_dev);
	slave_dev->flags &= ~IFF_SLAVE;
	rtmsg_ifinfo(RTM_NEWLINK, slave_dev, IFF_SLAVE, GFP_KERNEL);
}

static void slave_kobj_release(struct kobject *kobj)
{
	struct slave *slave = to_slave(kobj);
	struct bonding *bond = bond_get_bond_by_slave(slave);

	cancel_delayed_work_sync(&slave->notify_work);
	if (BOND_MODE(bond) == BOND_MODE_8023AD)
		kfree(SLAVE_AD_INFO(slave));

	kfree(slave);
}

static struct kobj_type slave_ktype = {
	.release = slave_kobj_release,
#ifdef CONFIG_SYSFS
	.sysfs_ops = &slave_sysfs_ops,
#endif
};

static int bond_kobj_init(struct slave *slave)
{
	int err;

	err = kobject_init_and_add(&slave->kobj, &slave_ktype,
				   &(slave->dev->dev.kobj), "bonding_slave");
	if (err)
		kobject_put(&slave->kobj);

	return err;
}

static struct slave *bond_alloc_slave(struct bonding *bond,
				      struct net_device *slave_dev)
{
	struct slave *slave = NULL;

	slave = kzalloc(sizeof(struct slave), GFP_KERNEL);
	if (!slave)
		return NULL;

	slave->bond = bond;
	slave->dev = slave_dev;
	INIT_DELAYED_WORK(&slave->notify_work, bond_netdev_notify_work);

	if (bond_kobj_init(slave))
		return NULL;

	if (BOND_MODE(bond) == BOND_MODE_8023AD) {
		SLAVE_AD_INFO(slave) = kzalloc(sizeof(struct ad_slave_info),
					       GFP_KERNEL);
		if (!SLAVE_AD_INFO(slave)) {
			kobject_put(&slave->kobj);
			return NULL;
		}
	}

	return slave;
}

static void bond_fill_ifbond(struct bonding *bond, struct ifbond *info)
{
	info->bond_mode = BOND_MODE(bond);
	info->miimon = bond->params.miimon;
	info->num_slaves = bond->slave_cnt;
}

static void bond_fill_ifslave(struct slave *slave, struct ifslave *info)
{
	strcpy(info->slave_name, slave->dev->name);
	info->link = slave->link;
	info->state = bond_slave_state(slave);
	info->link_failure_count = slave->link_failure_count;
}

static void bond_netdev_notify_work(struct work_struct *_work)
{
	struct slave *slave = container_of(_work, struct slave,
					   notify_work.work);

	if (rtnl_trylock()) {
		struct netdev_bonding_info binfo;

		bond_fill_ifslave(slave, &binfo.slave);
		bond_fill_ifbond(slave->bond, &binfo.master);
		netdev_bonding_info_change(slave->dev, &binfo);
		rtnl_unlock();
	} else {
		queue_delayed_work(slave->bond->wq, &slave->notify_work, 1);
	}
}

void bond_queue_slave_event(struct slave *slave)
{
	queue_delayed_work(slave->bond->wq, &slave->notify_work, 0);
}

/* enslave device <slave> to bond device <master> */
int bond_enslave(struct net_device *bond_dev, struct net_device *slave_dev)
{
	struct bonding *bond = bond_dev->priv;
	struct slave *new_slave = NULL;
	struct dev_mc_list *dmi;
	struct sockaddr addr;
	int link_reporting;
	int old_features = bond_dev->features;
	int res = 0;

	if (!bond->params.use_carrier && slave_dev->ethtool_ops == NULL &&
		slave_dev->do_ioctl == NULL) {
		printk(KERN_WARNING DRV_NAME
		       ": %s: Warning: no link monitoring support for %s\n",
		       bond_dev->name, slave_dev->name);
	}

	/* bond must be initialized by bond_open() before enslaving */
	if (!(bond_dev->flags & IFF_UP)) {
		printk(KERN_WARNING DRV_NAME
			" %s: master_dev is not up in bond_enslave\n",
			bond_dev->name);
	struct bonding *bond = netdev_priv(bond_dev);
	const struct net_device_ops *slave_ops = slave_dev->netdev_ops;
	struct slave *new_slave = NULL, *prev_slave;
	struct sockaddr addr;
	int link_reporting;
	int res = 0, i;

	if (!bond->params.use_carrier &&
	    slave_dev->ethtool_ops->get_link == NULL &&
	    slave_ops->ndo_do_ioctl == NULL) {
		netdev_warn(bond_dev, "no link monitoring support for %s\n",
			    slave_dev->name);
	}

	/* already enslaved */
	if (slave_dev->flags & IFF_SLAVE) {
		dprintk("Error, Device was already enslaved\n");
		return -EBUSY;
	}

	/* vlan challenged mutual exclusion */
	/* no need to lock since we're protected by rtnl_lock */
	if (slave_dev->features & NETIF_F_VLAN_CHALLENGED) {
		dprintk("%s: NETIF_F_VLAN_CHALLENGED\n", slave_dev->name);
		if (!list_empty(&bond->vlan_list)) {
			printk(KERN_ERR DRV_NAME
			       ": %s: Error: cannot enslave VLAN "
			       "challenged slave %s on VLAN enabled "
			       "bond %s\n", bond_dev->name, slave_dev->name,
			       bond_dev->name);
			return -EPERM;
		} else {
			printk(KERN_WARNING DRV_NAME
			       ": %s: Warning: enslaved VLAN challenged "
			       "slave %s. Adding VLANs will be blocked as "
			       "long as %s is part of bond %s\n",
			       bond_dev->name, slave_dev->name, slave_dev->name,
			       bond_dev->name);
			bond_dev->features |= NETIF_F_VLAN_CHALLENGED;
		}
	} else {
		dprintk("%s: ! NETIF_F_VLAN_CHALLENGED\n", slave_dev->name);
		if (bond->slave_cnt == 0) {
			/* First slave, and it is not VLAN challenged,
			 * so remove the block of adding VLANs over the bond.
			 */
			bond_dev->features &= ~NETIF_F_VLAN_CHALLENGED;
		}
	}

	/*
	 * Old ifenslave binaries are no longer supported.  These can
	 * be identified with moderate accurary by the state of the slave:
		netdev_dbg(bond_dev, "Error: Device was already enslaved\n");
	/* already in-use? */
	if (netdev_is_rx_handler_busy(slave_dev)) {
		netdev_err(bond_dev,
			   "Error: Device is in use and cannot be enslaved\n");
		return -EBUSY;
	}

	if (bond_dev == slave_dev) {
		netdev_err(bond_dev, "cannot enslave bond to itself.\n");
		return -EPERM;
	}

	/* vlan challenged mutual exclusion */
	/* no need to lock since we're protected by rtnl_lock */
	if (slave_dev->features & NETIF_F_VLAN_CHALLENGED) {
		netdev_dbg(bond_dev, "%s is NETIF_F_VLAN_CHALLENGED\n",
			   slave_dev->name);
		if (vlan_uses_dev(bond_dev)) {
			netdev_err(bond_dev, "Error: cannot enslave VLAN challenged slave %s on VLAN enabled bond %s\n",
				   slave_dev->name, bond_dev->name);
			return -EPERM;
		} else {
			netdev_warn(bond_dev, "enslaved VLAN challenged slave %s. Adding VLANs will be blocked as long as %s is part of bond %s\n",
				    slave_dev->name, slave_dev->name,
				    bond_dev->name);
		}
	} else {
		netdev_dbg(bond_dev, "%s is !NETIF_F_VLAN_CHALLENGED\n",
			   slave_dev->name);
	}

	/* Old ifenslave binaries are no longer supported.  These can
	 * be identified with moderate accuracy by the state of the slave:
	 * the current ifenslave will set the interface down prior to
	 * enslaving it; the old ifenslave will not.
	 */
	if ((slave_dev->flags & IFF_UP)) {
		printk(KERN_ERR DRV_NAME ": %s is up. "
		       "This may be due to an out of date ifenslave.\n",
		       slave_dev->name);
		netdev_err(bond_dev, "%s is up - this may be due to an out of date ifenslave\n",
			   slave_dev->name);
		res = -EPERM;
		goto err_undo_flags;
	}

	/* set bonding device ether type by slave - bonding netdevices are
	 * created with ether_setup, so when the slave type is not ARPHRD_ETHER
	 * there is a need to override some of the type dependent attribs/funcs.
	 *
	 * bond ether type mutual exclusion - don't allow slaves of dissimilar
	 * ether type (eg ARPHRD_ETHER and ARPHRD_INFINIBAND) share the same bond
	 */
	if (bond->slave_cnt == 0) {
		if (slave_dev->type != ARPHRD_ETHER)
			bond_setup_by_slave(bond_dev, slave_dev);
	} else if (bond_dev->type != slave_dev->type) {
		printk(KERN_ERR DRV_NAME ": %s ether type (%d) is different "
			"from other slaves (%d), can not enslave it.\n",
			slave_dev->name,
			slave_dev->type, bond_dev->type);
			res = -EINVAL;
			goto err_undo_flags;
	}

	if (slave_dev->set_mac_address == NULL) {
		if (bond->slave_cnt == 0) {
			printk(KERN_WARNING DRV_NAME
			       ": %s: Warning: The first slave device "
			       "specified does not support setting the MAC "
			       "address. Setting fail_over_mac to active.",
			       bond_dev->name);
			bond->params.fail_over_mac = BOND_FOM_ACTIVE;
		} else if (bond->params.fail_over_mac != BOND_FOM_ACTIVE) {
			printk(KERN_ERR DRV_NAME
				": %s: Error: The slave device specified "
				"does not support setting the MAC address, "
				"but fail_over_mac is not set to active.\n"
				, bond_dev->name);
			res = -EOPNOTSUPP;
			goto err_undo_flags;
		}
	}

	new_slave = kzalloc(sizeof(struct slave), GFP_KERNEL);
	if (!bond_has_slaves(bond)) {
		if (bond_dev->type != slave_dev->type) {
			netdev_dbg(bond_dev, "change device type from %d to %d\n",
				   bond_dev->type, slave_dev->type);

			res = call_netdevice_notifiers(NETDEV_PRE_TYPE_CHANGE,
						       bond_dev);
			res = notifier_to_errno(res);
			if (res) {
				netdev_err(bond_dev, "refused to change device type\n");
				res = -EBUSY;
				goto err_undo_flags;
			}

			/* Flush unicast and multicast addresses */
			dev_uc_flush(bond_dev);
			dev_mc_flush(bond_dev);

			if (slave_dev->type != ARPHRD_ETHER)
				bond_setup_by_slave(bond_dev, slave_dev);
			else {
				ether_setup(bond_dev);
				bond_dev->priv_flags &= ~IFF_TX_SKB_SHARING;
			}

			call_netdevice_notifiers(NETDEV_POST_TYPE_CHANGE,
						 bond_dev);
		}
	} else if (bond_dev->type != slave_dev->type) {
		netdev_err(bond_dev, "%s ether type (%d) is different from other slaves (%d), can not enslave it\n",
			   slave_dev->name, slave_dev->type, bond_dev->type);
		res = -EINVAL;
		goto err_undo_flags;
	}

	if (slave_ops->ndo_set_mac_address == NULL) {
		netdev_warn(bond_dev, "The slave device specified does not support setting the MAC address\n");
		if (BOND_MODE(bond) == BOND_MODE_ACTIVEBACKUP &&
		    bond->params.fail_over_mac != BOND_FOM_ACTIVE) {
			if (!bond_has_slaves(bond)) {
				bond->params.fail_over_mac = BOND_FOM_ACTIVE;
				netdev_warn(bond_dev, "Setting fail_over_mac to active for active-backup mode\n");
			} else {
				netdev_err(bond_dev, "The slave device specified does not support setting the MAC address, but fail_over_mac is not set to active\n");
				res = -EOPNOTSUPP;
				goto err_undo_flags;
			}
		}
	}

	call_netdevice_notifiers(NETDEV_JOIN, slave_dev);

	/* If this is the first slave, then we need to set the master's hardware
	 * address to be the same as the slave's.
	 */
	if (!bond_has_slaves(bond) &&
	    bond->dev->addr_assign_type == NET_ADDR_RANDOM)
		bond_set_dev_addr(bond->dev, slave_dev);

	new_slave = bond_alloc_slave(bond, slave_dev);
	if (!new_slave) {
		res = -ENOMEM;
		goto err_undo_flags;
	}

	/* save slave's original flags before calling
	 * netdev_set_master and dev_open
	 */
	new_slave->original_flags = slave_dev->flags;

	/*
	 * Save slave's original ("permanent") mac address for modes
	 * that need it, and for restoring it upon release, and then
	 * set it to the master's address
	 */
	memcpy(new_slave->perm_hwaddr, slave_dev->dev_addr, ETH_ALEN);

	if (!bond->params.fail_over_mac) {
		/*
		 * Set slave to master's mac address.  The application already
	new_slave->bond = bond;
	new_slave->dev = slave_dev;
	/* Set the new_slave's queue_id to be zero.  Queue ID mapping
	 * is set via sysfs or module option if desired.
	 */
	new_slave->queue_id = 0;

	/* Save slave's original mtu and then set it to match the bond */
	new_slave->original_mtu = slave_dev->mtu;
	res = dev_set_mtu(slave_dev, bond->dev->mtu);
	if (res) {
		netdev_dbg(bond_dev, "Error %d calling dev_set_mtu\n", res);
		goto err_free;
	}

	/* Save slave's original ("permanent") mac address for modes
	 * that need it, and for restoring it upon release, and then
	 * set it to the master's address
	 */
	ether_addr_copy(new_slave->perm_hwaddr, slave_dev->dev_addr);

	if (!bond->params.fail_over_mac ||
	    BOND_MODE(bond) != BOND_MODE_ACTIVEBACKUP) {
		/* Set slave to master's mac address.  The application already
		 * set the master's mac address to that of the first slave
		 */
		memcpy(addr.sa_data, bond_dev->dev_addr, bond_dev->addr_len);
		addr.sa_family = slave_dev->type;
		res = dev_set_mac_address(slave_dev, &addr);
		if (res) {
			dprintk("Error %d calling set_mac_address\n", res);
			goto err_free;
		}
	}

	res = netdev_set_master(slave_dev, bond_dev);
	if (res) {
		dprintk("Error %d calling netdev_set_master\n", res);
		goto err_restore_mac;
	}
	/* open the slave since the application closed it */
	res = dev_open(slave_dev);
	if (res) {
		dprintk("Openning slave %s failed\n", slave_dev->name);
		goto err_unset_master;
	}

	new_slave->dev = slave_dev;
	slave_dev->priv_flags |= IFF_BONDING;

	if ((bond->params.mode == BOND_MODE_TLB) ||
	    (bond->params.mode == BOND_MODE_ALB)) {
			netdev_dbg(bond_dev, "Error %d calling set_mac_address\n", res);
			goto err_restore_mtu;
		}
	}

	/* set slave flag before open to prevent IPv6 addrconf */
	slave_dev->flags |= IFF_SLAVE;

	/* open the slave since the application closed it */
	res = dev_open(slave_dev);
	if (res) {
		netdev_dbg(bond_dev, "Opening slave %s failed\n", slave_dev->name);
		goto err_restore_mac;
	}

	slave_dev->priv_flags |= IFF_BONDING;
	/* initialize slave stats */
	dev_get_stats(new_slave->dev, &new_slave->slave_stats);

	if (bond_is_lb(bond)) {
		/* bond_alb_init_slave() must be called before all other stages since
		 * it might fail and we do not want to have to undo everything
		 */
		res = bond_alb_init_slave(bond, new_slave);
		if (res) {
			goto err_close;
		}
	}

	/* If the mode USES_PRIMARY, then the new slave gets the
	 * master's promisc (and mc) settings only if it becomes the
	 * curr_active_slave, and that is taken care of later when calling
	 * bond_change_active()
	 */
	if (!USES_PRIMARY(bond->params.mode)) {
		if (res)
			goto err_close;
	}

	/* If the mode uses primary, then the following is handled by
	 * bond_change_active_slave().
	 */
	if (!bond_uses_primary(bond)) {
		/* set promiscuity level to new slave */
		if (bond_dev->flags & IFF_PROMISC) {
			res = dev_set_promiscuity(slave_dev, 1);
			if (res)
				goto err_close;
		}

		/* set allmulti level to new slave */
		if (bond_dev->flags & IFF_ALLMULTI) {
			res = dev_set_allmulti(slave_dev, 1);
			if (res)
				goto err_close;
		}

		netif_addr_lock_bh(bond_dev);
		/* upload master's mc_list to new slave */
		for (dmi = bond_dev->mc_list; dmi; dmi = dmi->next) {
			dev_mc_add (slave_dev, dmi->dmi_addr, dmi->dmi_addrlen, 0);
		}
		netif_addr_unlock_bh(bond_dev);
	}

	if (bond->params.mode == BOND_MODE_8023AD) {
		/* add lacpdu mc addr to mc list */
		u8 lacpdu_multicast[ETH_ALEN] = MULTICAST_LACPDU_ADDR;

		dev_mc_add(slave_dev, lacpdu_multicast, ETH_ALEN, 0);
	}

	bond_add_vlans_on_slave(bond, slave_dev);

	write_lock_bh(&bond->lock);

	bond_attach_slave(bond, new_slave);

		dev_mc_sync_multiple(slave_dev, bond_dev);
		dev_uc_sync_multiple(slave_dev, bond_dev);

		netif_addr_unlock_bh(bond_dev);
	}

	if (BOND_MODE(bond) == BOND_MODE_8023AD) {
		/* add lacpdu mc addr to mc list */
		u8 lacpdu_multicast[ETH_ALEN] = MULTICAST_LACPDU_ADDR;

		dev_mc_add(slave_dev, lacpdu_multicast);
	}

	res = vlan_vids_add_by_dev(slave_dev, bond_dev);
	if (res) {
		netdev_err(bond_dev, "Couldn't add bond vlan ids to %s\n",
			   slave_dev->name);
		goto err_close;
	}

	prev_slave = bond_last_slave(bond);

	new_slave->delay = 0;
	new_slave->link_failure_count = 0;

	bond_compute_features(bond);

	write_unlock_bh(&bond->lock);

	read_lock(&bond->lock);

	new_slave->last_arp_rx = jiffies;
	bond_update_speed_duplex(new_slave);

	new_slave->last_rx = jiffies -
		(msecs_to_jiffies(bond->params.arp_interval) + 1);
	for (i = 0; i < BOND_MAX_ARP_TARGETS; i++)
		new_slave->target_last_arp_rx[i] = new_slave->last_rx;

	if (bond->params.miimon && !bond->params.use_carrier) {
		link_reporting = bond_check_dev_link(bond, slave_dev, 1);

		if ((link_reporting == -1) && !bond->params.arp_interval) {
			/*
			 * miimon is set but a bonded network driver
			/* miimon is set but a bonded network driver
			 * does not support ETHTOOL/MII and
			 * arp_interval is not set.  Note: if
			 * use_carrier is enabled, we will never go
			 * here (because netif_carrier is always
			 * supported); thus, we don't need to change
			 * the messages for netif_carrier.
			 */
			printk(KERN_WARNING DRV_NAME
			       ": %s: Warning: MII and ETHTOOL support not "
			       "available for interface %s, and "
			       "arp_interval/arp_ip_target module parameters "
			       "not specified, thus bonding will not detect "
			       "link failures! see bonding.txt for details.\n",
			       bond_dev->name, slave_dev->name);
		} else if (link_reporting == -1) {
			/* unable get link status using mii/ethtool */
			printk(KERN_WARNING DRV_NAME
			       ": %s: Warning: can't get link status from "
			       "interface %s; the network driver associated "
			       "with this interface does not support MII or "
			       "ETHTOOL link status reporting, thus miimon "
			       "has no effect on this interface.\n",
			       bond_dev->name, slave_dev->name);
			netdev_warn(bond_dev, "MII and ETHTOOL support not available for interface %s, and arp_interval/arp_ip_target module parameters not specified, thus bonding will not detect link failures! see bonding.txt for details\n",
				    slave_dev->name);
		} else if (link_reporting == -1) {
			/* unable get link status using mii/ethtool */
			netdev_warn(bond_dev, "can't get link status from interface %s; the network driver associated with this interface does not support MII or ETHTOOL link status reporting, thus miimon has no effect on this interface\n",
				    slave_dev->name);
		}
	}

	/* check for initial state */
	if (!bond->params.miimon ||
	    (bond_check_dev_link(bond, slave_dev, 0) == BMSR_LSTATUS)) {
		if (bond->params.updelay) {
			dprintk("Initial state of slave_dev is "
				"BOND_LINK_BACK\n");
			new_slave->link  = BOND_LINK_BACK;
			new_slave->delay = bond->params.updelay;
		} else {
			dprintk("Initial state of slave_dev is "
				"BOND_LINK_UP\n");
			new_slave->link  = BOND_LINK_UP;
		}
		new_slave->jiffies = jiffies;
	} else {
		dprintk("Initial state of slave_dev is "
			"BOND_LINK_DOWN\n");
		new_slave->link  = BOND_LINK_DOWN;
	}

	if (bond_update_speed_duplex(new_slave) &&
	    (new_slave->link != BOND_LINK_DOWN)) {
		printk(KERN_WARNING DRV_NAME
		       ": %s: Warning: failed to get speed and duplex from %s, "
		       "assumed to be 100Mb/sec and Full.\n",
		       bond_dev->name, new_slave->dev->name);

		if (bond->params.mode == BOND_MODE_8023AD) {
			printk(KERN_WARNING DRV_NAME
			       ": %s: Warning: Operation of 802.3ad mode requires ETHTOOL "
			       "support in base driver for proper aggregator "
			       "selection.\n", bond_dev->name);
		}
	}

	if (USES_PRIMARY(bond->params.mode) && bond->params.primary[0]) {
		/* if there is a primary slave, remember it */
		if (strcmp(bond->params.primary, new_slave->dev->name) == 0) {
			bond->primary_slave = new_slave;
		}
	}

	write_lock_bh(&bond->curr_slave_lock);

	switch (bond->params.mode) {
	case BOND_MODE_ACTIVEBACKUP:
		bond_set_slave_inactive_flags(new_slave);
		bond_select_active_slave(bond);
	if (bond->params.miimon) {
		if (bond_check_dev_link(bond, slave_dev, 0) == BMSR_LSTATUS) {
			if (bond->params.updelay) {
				bond_set_slave_link_state(new_slave,
							  BOND_LINK_BACK);
				new_slave->delay = bond->params.updelay;
			} else {
				bond_set_slave_link_state(new_slave,
							  BOND_LINK_UP);
			}
		} else {
			bond_set_slave_link_state(new_slave, BOND_LINK_DOWN);
		}
	} else if (bond->params.arp_interval) {
		bond_set_slave_link_state(new_slave,
					  (netif_carrier_ok(slave_dev) ?
					  BOND_LINK_UP : BOND_LINK_DOWN));
	} else {
		bond_set_slave_link_state(new_slave, BOND_LINK_UP);
	}

	if (new_slave->link != BOND_LINK_DOWN)
		new_slave->last_link_up = jiffies;
	netdev_dbg(bond_dev, "Initial state of slave_dev is BOND_LINK_%s\n",
		   new_slave->link == BOND_LINK_DOWN ? "DOWN" :
		   (new_slave->link == BOND_LINK_UP ? "UP" : "BACK"));

	if (bond_uses_primary(bond) && bond->params.primary[0]) {
		/* if there is a primary slave, remember it */
		if (strcmp(bond->params.primary, new_slave->dev->name) == 0) {
			rcu_assign_pointer(bond->primary_slave, new_slave);
			bond->force_primary = true;
		}
	}

	switch (BOND_MODE(bond)) {
	case BOND_MODE_ACTIVEBACKUP:
		bond_set_slave_inactive_flags(new_slave,
					      BOND_SLAVE_NOTIFY_NOW);
		break;
	case BOND_MODE_8023AD:
		/* in 802.3ad mode, the internal mechanism
		 * will activate the slaves in the selected
		 * aggregator
		 */
		bond_set_slave_inactive_flags(new_slave);
		/* if this is the first slave */
		if (bond->slave_cnt == 1) {
			SLAVE_AD_INFO(new_slave).id = 1;
			/* Initialize AD with the number of times that the AD timer is called in 1 second
			 * can be called only after the mac address of the bond is set
			 */
			bond_3ad_initialize(bond, 1000/AD_TIMER_INTERVAL,
					    bond->params.lacp_fast);
		} else {
			SLAVE_AD_INFO(new_slave).id =
				SLAVE_AD_INFO(new_slave->prev).id + 1;
		bond_set_slave_inactive_flags(new_slave, BOND_SLAVE_NOTIFY_NOW);
		/* if this is the first slave */
		if (!prev_slave) {
			SLAVE_AD_INFO(new_slave)->id = 1;
			/* Initialize AD with the number of times that the AD timer is called in 1 second
			 * can be called only after the mac address of the bond is set
			 */
			bond_3ad_initialize(bond, 1000/AD_TIMER_INTERVAL);
		} else {
			SLAVE_AD_INFO(new_slave)->id =
				SLAVE_AD_INFO(prev_slave)->id + 1;
		}

		bond_3ad_bind_slave(new_slave);
		break;
	case BOND_MODE_TLB:
	case BOND_MODE_ALB:
		new_slave->state = BOND_STATE_ACTIVE;
		bond_set_slave_inactive_flags(new_slave);
		break;
	default:
		dprintk("This slave is always active in trunk mode\n");

		/* always active in trunk mode */
		new_slave->state = BOND_STATE_ACTIVE;
		bond_set_active_slave(new_slave);
		bond_set_slave_inactive_flags(new_slave, BOND_SLAVE_NOTIFY_NOW);
		break;
	default:
		netdev_dbg(bond_dev, "This slave is always active in trunk mode\n");

		/* always active in trunk mode */
		bond_set_active_slave(new_slave);

		/* In trunking mode there is little meaning to curr_active_slave
		 * anyway (it holds no special properties of the bond device),
		 * so we can change it without calling change_active_interface()
		 */
		if (!bond->curr_active_slave) {
			bond->curr_active_slave = new_slave;
		}
		break;
	} /* switch(bond_mode) */

	write_unlock_bh(&bond->curr_slave_lock);

	bond_set_carrier(bond);

	read_unlock(&bond->lock);

	res = bond_create_slave_symlinks(bond_dev, slave_dev);
	if (res)
		goto err_close;

	printk(KERN_INFO DRV_NAME
	       ": %s: enslaving %s as a%s interface with a%s link.\n",
	       bond_dev->name, slave_dev->name,
	       new_slave->state == BOND_STATE_ACTIVE ? "n active" : " backup",
	       new_slave->link != BOND_LINK_DOWN ? "n up" : " down");

	/* enslave is successful */
	return 0;

/* Undo stages on error */
err_close:
	dev_close(slave_dev);

err_unset_master:
	netdev_set_master(slave_dev, NULL);

err_restore_mac:
	if (!bond->params.fail_over_mac) {
		if (!rcu_access_pointer(bond->curr_active_slave) &&
		    new_slave->link == BOND_LINK_UP)
			rcu_assign_pointer(bond->curr_active_slave, new_slave);

		break;
	} /* switch(bond_mode) */

#ifdef CONFIG_NET_POLL_CONTROLLER
	if (bond->dev->npinfo) {
		if (slave_enable_netpoll(new_slave)) {
			netdev_info(bond_dev, "master_dev is using netpoll, but new slave device does not support netpoll\n");
			res = -EBUSY;
			goto err_detach;
		}
	}
#endif

	if (!(bond_dev->features & NETIF_F_LRO))
		dev_disable_lro(slave_dev);

	res = netdev_rx_handler_register(slave_dev, bond_handle_frame,
					 new_slave);
	if (res) {
		netdev_dbg(bond_dev, "Error %d calling netdev_rx_handler_register\n", res);
		goto err_detach;
	}

	res = bond_master_upper_dev_link(bond_dev, slave_dev, new_slave);
	if (res) {
		netdev_dbg(bond_dev, "Error %d calling bond_master_upper_dev_link\n", res);
		goto err_unregister;
	}

	res = bond_sysfs_slave_add(new_slave);
	if (res) {
		netdev_dbg(bond_dev, "Error %d calling bond_sysfs_slave_add\n", res);
		goto err_upper_unlink;
	}

	/* If the mode uses primary, then the following is handled by
	 * bond_change_active_slave().
	 */
	if (!bond_uses_primary(bond)) {
		/* set promiscuity level to new slave */
		if (bond_dev->flags & IFF_PROMISC) {
			res = dev_set_promiscuity(slave_dev, 1);
			if (res)
				goto err_sysfs_del;
		}

		/* set allmulti level to new slave */
		if (bond_dev->flags & IFF_ALLMULTI) {
			res = dev_set_allmulti(slave_dev, 1);
			if (res) {
				if (bond_dev->flags & IFF_PROMISC)
					dev_set_promiscuity(slave_dev, -1);
				goto err_sysfs_del;
			}
		}

		netif_addr_lock_bh(bond_dev);
		dev_mc_sync_multiple(slave_dev, bond_dev);
		dev_uc_sync_multiple(slave_dev, bond_dev);
		netif_addr_unlock_bh(bond_dev);

		if (BOND_MODE(bond) == BOND_MODE_8023AD) {
			/* add lacpdu mc addr to mc list */
			u8 lacpdu_multicast[ETH_ALEN] = MULTICAST_LACPDU_ADDR;

			dev_mc_add(slave_dev, lacpdu_multicast);
		}
	}

	bond->slave_cnt++;
	bond_compute_features(bond);
	bond_set_carrier(bond);

	if (bond_uses_primary(bond)) {
		block_netpoll_tx();
		bond_select_active_slave(bond);
		unblock_netpoll_tx();
	}

	if (bond_mode_uses_xmit_hash(bond))
		bond_update_slave_arr(bond, NULL);

	netdev_info(bond_dev, "Enslaving %s as %s interface with %s link\n",
		    slave_dev->name,
		    bond_is_active_slave(new_slave) ? "an active" : "a backup",
		    new_slave->link != BOND_LINK_DOWN ? "an up" : "a down");

	/* enslave is successful */
	bond_queue_slave_event(new_slave);
	return 0;

/* Undo stages on error */
err_sysfs_del:
	bond_sysfs_slave_del(new_slave);

err_upper_unlink:
	bond_upper_dev_unlink(bond_dev, slave_dev);

err_unregister:
	netdev_rx_handler_unregister(slave_dev);

err_detach:
	vlan_vids_del_by_dev(slave_dev, bond_dev);
	if (rcu_access_pointer(bond->primary_slave) == new_slave)
		RCU_INIT_POINTER(bond->primary_slave, NULL);
	if (rcu_access_pointer(bond->curr_active_slave) == new_slave) {
		block_netpoll_tx();
		bond_change_active_slave(bond, NULL);
		bond_select_active_slave(bond);
		unblock_netpoll_tx();
	}
	/* either primary_slave or curr_active_slave might've changed */
	synchronize_rcu();
	slave_disable_netpoll(new_slave);

err_close:
	if (!netif_is_bond_master(slave_dev))
		slave_dev->priv_flags &= ~IFF_BONDING;
	dev_close(slave_dev);

err_restore_mac:
	slave_dev->flags &= ~IFF_SLAVE;
	if (!bond->params.fail_over_mac ||
	    BOND_MODE(bond) != BOND_MODE_ACTIVEBACKUP) {
		/* XXX TODO - fom follow mode needs to change master's
		 * MAC if this slave's MAC is in use by the bond, or at
		 * least print a warning.
		 */
		memcpy(addr.sa_data, new_slave->perm_hwaddr, ETH_ALEN);
		ether_addr_copy(addr.sa_data, new_slave->perm_hwaddr);
		addr.sa_family = slave_dev->type;
		dev_set_mac_address(slave_dev, &addr);
	}

err_free:
	kfree(new_slave);

err_undo_flags:
	bond_dev->features = old_features;
 
	return res;
}

/*
 * Try to release the slave device <slave> from the bond device <master>
 * It is legal to access curr_active_slave without a lock because all the function
 * is write-locked.
err_restore_mtu:
	dev_set_mtu(slave_dev, new_slave->original_mtu);

err_free:
	kobject_put(&new_slave->kobj);

err_undo_flags:
	/* Enslave of first slave has failed and we need to fix master's mac */
	if (!bond_has_slaves(bond)) {
		if (ether_addr_equal_64bits(bond_dev->dev_addr,
					    slave_dev->dev_addr))
			eth_hw_addr_random(bond_dev);
		if (bond_dev->type != ARPHRD_ETHER) {
			dev_close(bond_dev);
			ether_setup(bond_dev);
			bond_dev->flags |= IFF_MASTER;
			bond_dev->priv_flags &= ~IFF_TX_SKB_SHARING;
		}
	}

	return res;
}

/* Try to release the slave device <slave> from the bond device <master>
 * It is legal to access curr_active_slave without a lock because all the function
 * is RTNL-locked. If "all" is true it means that the function is being called
 * while destroying a bond interface and all slaves are being released.
 *
 * The rules for slave state should be:
 *   for Active/Backup:
 *     Active stays on all backups go down
 *   for Bonded connections:
 *     The first up interface should be left on and all others downed.
 */
int bond_release(struct net_device *bond_dev, struct net_device *slave_dev)
{
	struct bonding *bond = bond_dev->priv;
	struct slave *slave, *oldcurrent;
	struct sockaddr addr;
	int mac_addr_differ;
	DECLARE_MAC_BUF(mac);

	/* slave is not a slave or master is not master of this slave */
	if (!(slave_dev->flags & IFF_SLAVE) ||
	    (slave_dev->master != bond_dev)) {
		printk(KERN_ERR DRV_NAME
		       ": %s: Error: cannot release %s.\n",
		       bond_dev->name, slave_dev->name);
		return -EINVAL;
	}

	write_lock_bh(&bond->lock);
static int __bond_release_one(struct net_device *bond_dev,
			      struct net_device *slave_dev,
			      bool all)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct slave *slave, *oldcurrent;
	struct sockaddr addr;
	int old_flags = bond_dev->flags;
	netdev_features_t old_features = bond_dev->features;

	/* slave is not a slave or master is not master of this slave */
	if (!(slave_dev->flags & IFF_SLAVE) ||
	    !netdev_has_upper_dev(slave_dev, bond_dev)) {
		netdev_dbg(bond_dev, "cannot release %s\n",
			   slave_dev->name);
		return -EINVAL;
	}

	block_netpoll_tx();

	slave = bond_get_slave_by_dev(bond, slave_dev);
	if (!slave) {
		/* not a slave of this bond */
		printk(KERN_INFO DRV_NAME
		       ": %s: %s not enslaved\n",
		       bond_dev->name, slave_dev->name);
		write_unlock_bh(&bond->lock);
		return -EINVAL;
	}

	if (!bond->params.fail_over_mac) {
		mac_addr_differ = memcmp(bond_dev->dev_addr, slave->perm_hwaddr,
					 ETH_ALEN);
		if (!mac_addr_differ && (bond->slave_cnt > 1))
			printk(KERN_WARNING DRV_NAME
			       ": %s: Warning: the permanent HWaddr of %s - "
			       "%s - is still in use by %s. "
			       "Set the HWaddr of %s to a different address "
			       "to avoid conflicts.\n",
			       bond_dev->name, slave_dev->name,
			       print_mac(mac, slave->perm_hwaddr),
			       bond_dev->name, slave_dev->name);
	}

	/* Inform AD package of unbinding of slave. */
	if (bond->params.mode == BOND_MODE_8023AD) {
		/* must be called before the slave is
		 * detached from the list
		 */
		bond_3ad_unbind_slave(slave);
	}

	printk(KERN_INFO DRV_NAME
	       ": %s: releasing %s interface %s\n",
	       bond_dev->name,
	       (slave->state == BOND_STATE_ACTIVE)
	       ? "active" : "backup",
	       slave_dev->name);

	oldcurrent = bond->curr_active_slave;

	bond->current_arp_slave = NULL;

	/* release the slave from its bond */
	bond_detach_slave(bond, slave);

	bond_compute_features(bond);

	if (bond->primary_slave == slave) {
		bond->primary_slave = NULL;
	}

	if (oldcurrent == slave) {
		bond_change_active_slave(bond, NULL);
	}

	if ((bond->params.mode == BOND_MODE_TLB) ||
	    (bond->params.mode == BOND_MODE_ALB)) {
		netdev_info(bond_dev, "%s not enslaved\n",
			    slave_dev->name);
		unblock_netpoll_tx();
		return -EINVAL;
	}

	bond_sysfs_slave_del(slave);

	/* recompute stats just before removing the slave */
	bond_get_stats(bond->dev, &bond->bond_stats);

	bond_upper_dev_unlink(bond_dev, slave_dev);
	/* unregister rx_handler early so bond_handle_frame wouldn't be called
	 * for this slave anymore.
	 */
	netdev_rx_handler_unregister(slave_dev);

	if (BOND_MODE(bond) == BOND_MODE_8023AD)
		bond_3ad_unbind_slave(slave);

	if (bond_mode_uses_xmit_hash(bond))
		bond_update_slave_arr(bond, slave);

	netdev_info(bond_dev, "Releasing %s interface %s\n",
		    bond_is_active_slave(slave) ? "active" : "backup",
		    slave_dev->name);

	oldcurrent = rcu_access_pointer(bond->curr_active_slave);

	RCU_INIT_POINTER(bond->current_arp_slave, NULL);

	if (!all && (!bond->params.fail_over_mac ||
		     BOND_MODE(bond) != BOND_MODE_ACTIVEBACKUP)) {
		if (ether_addr_equal_64bits(bond_dev->dev_addr, slave->perm_hwaddr) &&
		    bond_has_slaves(bond))
			netdev_warn(bond_dev, "the permanent HWaddr of %s - %pM - is still in use by %s - set the HWaddr of %s to a different address to avoid conflicts\n",
				    slave_dev->name, slave->perm_hwaddr,
				    bond_dev->name, slave_dev->name);
	}

	if (rtnl_dereference(bond->primary_slave) == slave)
		RCU_INIT_POINTER(bond->primary_slave, NULL);

	if (oldcurrent == slave)
		bond_change_active_slave(bond, NULL);

	if (bond_is_lb(bond)) {
		/* Must be called only after the slave has been
		 * detached from the list and the curr_active_slave
		 * has been cleared (if our_slave == old_current),
		 * but before a new active slave is selected.
		 */
		write_unlock_bh(&bond->lock);
		bond_alb_deinit_slave(bond, slave);
		write_lock_bh(&bond->lock);
	}

	if (oldcurrent == slave) {
		/*
		 * Note that we hold RTNL over this sequence, so there
		 * is no concern that another slave add/remove event
		 * will interfere.
		 */
		write_unlock_bh(&bond->lock);
		read_lock(&bond->lock);
		write_lock_bh(&bond->curr_slave_lock);

		bond_select_active_slave(bond);

		write_unlock_bh(&bond->curr_slave_lock);
		read_unlock(&bond->lock);
		write_lock_bh(&bond->lock);
	}

	if (bond->slave_cnt == 0) {
		bond_set_carrier(bond);

		/* if the last slave was removed, zero the mac address
		 * of the master so it will be set by the application
		 * to the mac address of the first slave
		 */
		memset(bond_dev->dev_addr, 0, bond_dev->addr_len);

		if (list_empty(&bond->vlan_list)) {
			bond_dev->features |= NETIF_F_VLAN_CHALLENGED;
		} else {
			printk(KERN_WARNING DRV_NAME
			       ": %s: Warning: clearing HW address of %s while it "
			       "still has VLANs.\n",
			       bond_dev->name, bond_dev->name);
			printk(KERN_WARNING DRV_NAME
			       ": %s: When re-adding slaves, make sure the bond's "
			       "HW address matches its VLANs'.\n",
			       bond_dev->name);
		}
	} else if ((bond_dev->features & NETIF_F_VLAN_CHALLENGED) &&
		   !bond_has_challenged_slaves(bond)) {
		printk(KERN_INFO DRV_NAME
		       ": %s: last VLAN challenged slave %s "
		       "left bond %s. VLAN blocking is removed\n",
		       bond_dev->name, slave_dev->name, bond_dev->name);
		bond_dev->features &= ~NETIF_F_VLAN_CHALLENGED;
	}

	write_unlock_bh(&bond->lock);

	/* must do this from outside any spinlocks */
	bond_destroy_slave_symlinks(bond_dev, slave_dev);

	bond_del_vlans_from_slave(bond, slave_dev);

	/* If the mode USES_PRIMARY, then we should only remove its
	 * promisc and mc settings if it was the curr_active_slave, but that was
	 * already taken care of above when we detached the slave
	 */
	if (!USES_PRIMARY(bond->params.mode)) {
		/* unset promiscuity level from slave */
		if (bond_dev->flags & IFF_PROMISC) {
			dev_set_promiscuity(slave_dev, -1);
		}

		/* unset allmulti level from slave */
		if (bond_dev->flags & IFF_ALLMULTI) {
			dev_set_allmulti(slave_dev, -1);
		}

		/* flush master's mc_list from slave */
		netif_addr_lock_bh(bond_dev);
		bond_mc_list_flush(bond_dev, slave_dev);
		netif_addr_unlock_bh(bond_dev);
	}

	netdev_set_master(slave_dev, NULL);
		bond_alb_deinit_slave(bond, slave);
	}

	if (all) {
		RCU_INIT_POINTER(bond->curr_active_slave, NULL);
	} else if (oldcurrent == slave) {
		/* Note that we hold RTNL over this sequence, so there
		 * is no concern that another slave add/remove event
		 * will interfere.
		 */
		bond_select_active_slave(bond);
	}

	if (!bond_has_slaves(bond)) {
		bond_set_carrier(bond);
		eth_hw_addr_random(bond_dev);
	}

	unblock_netpoll_tx();
	synchronize_rcu();
	bond->slave_cnt--;

	if (!bond_has_slaves(bond)) {
		call_netdevice_notifiers(NETDEV_CHANGEADDR, bond->dev);
		call_netdevice_notifiers(NETDEV_RELEASE, bond->dev);
	}

	bond_compute_features(bond);
	if (!(bond_dev->features & NETIF_F_VLAN_CHALLENGED) &&
	    (old_features & NETIF_F_VLAN_CHALLENGED))
		netdev_info(bond_dev, "last VLAN challenged slave %s left bond %s - VLAN blocking is removed\n",
			    slave_dev->name, bond_dev->name);

	vlan_vids_del_by_dev(slave_dev, bond_dev);

	/* If the mode uses primary, then this case was handled above by
	 * bond_change_active_slave(..., NULL)
	 */
	if (!bond_uses_primary(bond)) {
		/* unset promiscuity level from slave
		 * NOTE: The NETDEV_CHANGEADDR call above may change the value
		 * of the IFF_PROMISC flag in the bond_dev, but we need the
		 * value of that flag before that change, as that was the value
		 * when this slave was attached, so we cache at the start of the
		 * function and use it here. Same goes for ALLMULTI below
		 */
		if (old_flags & IFF_PROMISC)
			dev_set_promiscuity(slave_dev, -1);

		/* unset allmulti level from slave */
		if (old_flags & IFF_ALLMULTI)
			dev_set_allmulti(slave_dev, -1);

		bond_hw_addr_flush(bond_dev, slave_dev);
	}

	slave_disable_netpoll(slave);

	/* close slave before restoring its mac address */
	dev_close(slave_dev);

	if (bond->params.fail_over_mac != BOND_FOM_ACTIVE) {
		/* restore original ("permanent") mac address */
		memcpy(addr.sa_data, slave->perm_hwaddr, ETH_ALEN);
	if (bond->params.fail_over_mac != BOND_FOM_ACTIVE ||
	    BOND_MODE(bond) != BOND_MODE_ACTIVEBACKUP) {
		/* restore original ("permanent") mac address */
		ether_addr_copy(addr.sa_data, slave->perm_hwaddr);
		addr.sa_family = slave_dev->type;
		dev_set_mac_address(slave_dev, &addr);
	}

	slave_dev->priv_flags &= ~(IFF_MASTER_8023AD | IFF_MASTER_ALB |
				   IFF_SLAVE_INACTIVE | IFF_BONDING |
				   IFF_SLAVE_NEEDARP);

	kfree(slave);

	return 0;  /* deletion OK */
}

/*
* Destroy a bonding device.
* Must be under rtnl_lock when this function is called.
*/
void bond_destroy(struct bonding *bond)
{
	bond_deinit(bond->dev);
	bond_destroy_sysfs_entry(bond);
	unregister_netdevice(bond->dev);
}

/*
* First release a slave and than destroy the bond if no more slaves iare left.
* Must be under rtnl_lock when this function is called.
*/
int  bond_release_and_destroy(struct net_device *bond_dev, struct net_device *slave_dev)
{
	struct bonding *bond = bond_dev->priv;
	int ret;

	ret = bond_release(bond_dev, slave_dev);
	if ((ret == 0) && (bond->slave_cnt == 0)) {
		printk(KERN_INFO DRV_NAME ": %s: destroying bond %s.\n",
		       bond_dev->name, bond_dev->name);
		bond_destroy(bond);
	}
	return ret;
}

/*
 * This function releases all slaves.
 */
static int bond_release_all(struct net_device *bond_dev)
{
	struct bonding *bond = bond_dev->priv;
	struct slave *slave;
	struct net_device *slave_dev;
	struct sockaddr addr;

	write_lock_bh(&bond->lock);

	netif_carrier_off(bond_dev);

	if (bond->slave_cnt == 0) {
		goto out;
	}

	bond->current_arp_slave = NULL;
	bond->primary_slave = NULL;
	bond_change_active_slave(bond, NULL);

	while ((slave = bond->first_slave) != NULL) {
		/* Inform AD package of unbinding of slave
		 * before slave is detached from the list.
		 */
		if (bond->params.mode == BOND_MODE_8023AD) {
			bond_3ad_unbind_slave(slave);
		}

		slave_dev = slave->dev;
		bond_detach_slave(bond, slave);

		/* now that the slave is detached, unlock and perform
		 * all the undo steps that should not be called from
		 * within a lock.
		 */
		write_unlock_bh(&bond->lock);

		if ((bond->params.mode == BOND_MODE_TLB) ||
		    (bond->params.mode == BOND_MODE_ALB)) {
			/* must be called only after the slave
			 * has been detached from the list
			 */
			bond_alb_deinit_slave(bond, slave);
		}

		bond_compute_features(bond);

		bond_destroy_slave_symlinks(bond_dev, slave_dev);
		bond_del_vlans_from_slave(bond, slave_dev);

		/* If the mode USES_PRIMARY, then we should only remove its
		 * promisc and mc settings if it was the curr_active_slave, but that was
		 * already taken care of above when we detached the slave
		 */
		if (!USES_PRIMARY(bond->params.mode)) {
			/* unset promiscuity level from slave */
			if (bond_dev->flags & IFF_PROMISC) {
				dev_set_promiscuity(slave_dev, -1);
			}

			/* unset allmulti level from slave */
			if (bond_dev->flags & IFF_ALLMULTI) {
				dev_set_allmulti(slave_dev, -1);
			}

			/* flush master's mc_list from slave */
			netif_addr_lock_bh(bond_dev);
			bond_mc_list_flush(bond_dev, slave_dev);
			netif_addr_unlock_bh(bond_dev);
		}

		netdev_set_master(slave_dev, NULL);

		/* close slave before restoring its mac address */
		dev_close(slave_dev);

		if (!bond->params.fail_over_mac) {
			/* restore original ("permanent") mac address*/
			memcpy(addr.sa_data, slave->perm_hwaddr, ETH_ALEN);
			addr.sa_family = slave_dev->type;
			dev_set_mac_address(slave_dev, &addr);
		}

		slave_dev->priv_flags &= ~(IFF_MASTER_8023AD | IFF_MASTER_ALB |
					   IFF_SLAVE_INACTIVE);

		kfree(slave);

		/* re-acquire the lock before getting the next slave */
		write_lock_bh(&bond->lock);
	}

	/* zero the mac address of the master so it will be
	 * set by the application to the mac address of the
	 * first slave
	 */
	memset(bond_dev->dev_addr, 0, bond_dev->addr_len);

	if (list_empty(&bond->vlan_list)) {
		bond_dev->features |= NETIF_F_VLAN_CHALLENGED;
	} else {
		printk(KERN_WARNING DRV_NAME
		       ": %s: Warning: clearing HW address of %s while it "
		       "still has VLANs.\n",
		       bond_dev->name, bond_dev->name);
		printk(KERN_WARNING DRV_NAME
		       ": %s: When re-adding slaves, make sure the bond's "
		       "HW address matches its VLANs'.\n",
		       bond_dev->name);
	}

	printk(KERN_INFO DRV_NAME
	       ": %s: released all slaves\n",
	       bond_dev->name);

out:
	write_unlock_bh(&bond->lock);
	dev_set_mtu(slave_dev, slave->original_mtu);

	if (!netif_is_bond_master(slave_dev))
		slave_dev->priv_flags &= ~IFF_BONDING;

	kobject_put(&slave->kobj);

	return 0;
}

/*
 * This function changes the active slave to slave <slave_dev>.
 * It returns -EINVAL in the following cases.
 *  - <slave_dev> is not found in the list.
 *  - There is not active slave now.
 *  - <slave_dev> is already active.
 *  - The link state of <slave_dev> is not BOND_LINK_UP.
 *  - <slave_dev> is not running.
 * In these cases, this fuction does nothing.
 * In the other cases, currnt_slave pointer is changed and 0 is returned.
 */
static int bond_ioctl_change_active(struct net_device *bond_dev, struct net_device *slave_dev)
{
	struct bonding *bond = bond_dev->priv;
	struct slave *old_active = NULL;
	struct slave *new_active = NULL;
	int res = 0;

	if (!USES_PRIMARY(bond->params.mode)) {
		return -EINVAL;
	}

	/* Verify that master_dev is indeed the master of slave_dev */
	if (!(slave_dev->flags & IFF_SLAVE) ||
	    (slave_dev->master != bond_dev)) {
		return -EINVAL;
	}

	read_lock(&bond->lock);

	read_lock(&bond->curr_slave_lock);
	old_active = bond->curr_active_slave;
	read_unlock(&bond->curr_slave_lock);

	new_active = bond_get_slave_by_dev(bond, slave_dev);

	/*
	 * Changing to the current active: do nothing; return success.
	 */
	if (new_active && (new_active == old_active)) {
		read_unlock(&bond->lock);
		return 0;
	}

	if ((new_active) &&
	    (old_active) &&
	    (new_active->link == BOND_LINK_UP) &&
	    IS_UP(new_active->dev)) {
		write_lock_bh(&bond->curr_slave_lock);
		bond_change_active_slave(bond, new_active);
		write_unlock_bh(&bond->curr_slave_lock);
	} else {
		res = -EINVAL;
	}

	read_unlock(&bond->lock);

	return res;
/* A wrapper used because of ndo_del_link */
int bond_release(struct net_device *bond_dev, struct net_device *slave_dev)
{
	return __bond_release_one(bond_dev, slave_dev, false);
}

/* First release a slave and then destroy the bond if no more slaves are left.
 * Must be under rtnl_lock when this function is called.
 */
static int  bond_release_and_destroy(struct net_device *bond_dev,
				     struct net_device *slave_dev)
{
	struct bonding *bond = netdev_priv(bond_dev);
	int ret;

	ret = bond_release(bond_dev, slave_dev);
	if (ret == 0 && !bond_has_slaves(bond) &&
	    bond_dev->reg_state != NETREG_UNREGISTERING) {
		bond_dev->priv_flags |= IFF_DISABLE_NETPOLL;
		netdev_info(bond_dev, "Destroying bond %s\n",
			    bond_dev->name);
		bond_remove_proc_entry(bond);
		unregister_netdevice(bond_dev);
	}
	return ret;
}

static int bond_info_query(struct net_device *bond_dev, struct ifbond *info)
{
	struct bonding *bond = bond_dev->priv;

	info->bond_mode = bond->params.mode;
	info->miimon = bond->params.miimon;

	read_lock(&bond->lock);
	info->num_slaves = bond->slave_cnt;
	read_unlock(&bond->lock);

	struct bonding *bond = netdev_priv(bond_dev);
	bond_fill_ifbond(bond, info);
	return 0;
}

static int bond_slave_info_query(struct net_device *bond_dev, struct ifslave *info)
{
	struct bonding *bond = bond_dev->priv;
	struct slave *slave;
	int i, found = 0;

	if (info->slave_id < 0) {
		return -ENODEV;
	}

	read_lock(&bond->lock);

	bond_for_each_slave(bond, slave, i) {
		if (i == (int)info->slave_id) {
			found = 1;
	struct bonding *bond = netdev_priv(bond_dev);
	struct list_head *iter;
	int i = 0, res = -ENODEV;
	struct slave *slave;

	bond_for_each_slave(bond, slave, iter) {
		if (i++ == (int)info->slave_id) {
			res = 0;
			bond_fill_ifslave(slave, info);
			break;
		}
	}

	read_unlock(&bond->lock);

	if (found) {
		strcpy(info->slave_name, slave->dev->name);
		info->link = slave->link;
		info->state = slave->state;
		info->link_failure_count = slave->link_failure_count;
	} else {
		return -ENODEV;
	}

	return 0;
	return res;
}

/*-------------------------------- Monitoring -------------------------------*/


static int bond_miimon_inspect(struct bonding *bond)
{
	struct slave *slave;
	int i, link_state, commit = 0;

	bond_for_each_slave(bond, slave, i) {
/* called with rcu_read_lock() */
static int bond_miimon_inspect(struct bonding *bond)
{
	bool ignore_updelay = false;
	int link_state, commit = 0;
	struct list_head *iter;
	struct slave *slave;

	ignore_updelay = !rcu_dereference(bond->curr_active_slave);

	bond_for_each_slave_rcu(bond, slave, iter) {
		slave->new_link = BOND_LINK_NOCHANGE;

		link_state = bond_check_dev_link(bond, slave->dev, 0);

		switch (slave->link) {
		case BOND_LINK_UP:
			if (link_state)
				continue;

			slave->link = BOND_LINK_FAIL;
			slave->delay = bond->params.downdelay;
			if (slave->delay) {
				printk(KERN_INFO DRV_NAME
				       ": %s: link status down for %s"
				       "interface %s, disabling it in %d ms.\n",
				       bond->dev->name,
				       (bond->params.mode ==
					BOND_MODE_ACTIVEBACKUP) ?
				       ((slave->state == BOND_STATE_ACTIVE) ?
					"active " : "backup ") : "",
				       slave->dev->name,
				       bond->params.downdelay * bond->params.miimon);
			bond_set_slave_link_state(slave, BOND_LINK_FAIL);
			slave->delay = bond->params.downdelay;
			if (slave->delay) {
				netdev_info(bond->dev, "link status down for %sinterface %s, disabling it in %d ms\n",
					    (BOND_MODE(bond) ==
					     BOND_MODE_ACTIVEBACKUP) ?
					     (bond_is_active_slave(slave) ?
					      "active " : "backup ") : "",
					    slave->dev->name,
					    bond->params.downdelay * bond->params.miimon);
			}
			/*FALLTHRU*/
		case BOND_LINK_FAIL:
			if (link_state) {
				/*
				 * recovered before downdelay expired
				 */
				slave->link = BOND_LINK_UP;
				slave->jiffies = jiffies;
				printk(KERN_INFO DRV_NAME
				       ": %s: link status up again after %d "
				       "ms for interface %s.\n",
				       bond->dev->name,
				       (bond->params.downdelay - slave->delay) *
				       bond->params.miimon,
				       slave->dev->name);
				/* recovered before downdelay expired */
				bond_set_slave_link_state(slave, BOND_LINK_UP);
				slave->last_link_up = jiffies;
				netdev_info(bond->dev, "link status up again after %d ms for interface %s\n",
					    (bond->params.downdelay - slave->delay) *
					    bond->params.miimon,
					    slave->dev->name);
				continue;
			}

			if (slave->delay <= 0) {
				slave->new_link = BOND_LINK_DOWN;
				commit++;
				continue;
			}

			slave->delay--;
			break;

		case BOND_LINK_DOWN:
			if (!link_state)
				continue;

			slave->link = BOND_LINK_BACK;
			slave->delay = bond->params.updelay;

			if (slave->delay) {
				printk(KERN_INFO DRV_NAME
				       ": %s: link status up for "
				       "interface %s, enabling it in %d ms.\n",
				       bond->dev->name, slave->dev->name,
				       bond->params.updelay *
				       bond->params.miimon);
			bond_set_slave_link_state(slave, BOND_LINK_BACK);
			slave->delay = bond->params.updelay;

			if (slave->delay) {
				netdev_info(bond->dev, "link status up for interface %s, enabling it in %d ms\n",
					    slave->dev->name,
					    ignore_updelay ? 0 :
					    bond->params.updelay *
					    bond->params.miimon);
			}
			/*FALLTHRU*/
		case BOND_LINK_BACK:
			if (!link_state) {
				slave->link = BOND_LINK_DOWN;
				printk(KERN_INFO DRV_NAME
				       ": %s: link status down again after %d "
				       "ms for interface %s.\n",
				       bond->dev->name,
				       (bond->params.updelay - slave->delay) *
				       bond->params.miimon,
				       slave->dev->name);
				bond_set_slave_link_state(slave,
							  BOND_LINK_DOWN);
				netdev_info(bond->dev, "link status down again after %d ms for interface %s\n",
					    (bond->params.updelay - slave->delay) *
					    bond->params.miimon,
					    slave->dev->name);

				continue;
			}

			if (slave->delay <= 0) {
				slave->new_link = BOND_LINK_UP;
				commit++;
			if (ignore_updelay)
				slave->delay = 0;

			if (slave->delay <= 0) {
				slave->new_link = BOND_LINK_UP;
				commit++;
				ignore_updelay = false;
				continue;
			}

			slave->delay--;
			break;
		}
	}

	return commit;
}

static void bond_miimon_commit(struct bonding *bond)
{
	struct slave *slave;
	int i;

	bond_for_each_slave(bond, slave, i) {
	struct list_head *iter;
	struct slave *slave, *primary;

	bond_for_each_slave(bond, slave, iter) {
		switch (slave->new_link) {
		case BOND_LINK_NOCHANGE:
			/* For 802.3ad mode, check current slave speed and
			 * duplex again in case its port was disabled after
			 * invalid speed/duplex reporting but recovered before
			 * link monitoring could make a decision on the actual
			 * link status
			 */
			if (BOND_MODE(bond) == BOND_MODE_8023AD &&
			    slave->link == BOND_LINK_UP)
				bond_3ad_adapter_speed_duplex_changed(slave);
			continue;

		case BOND_LINK_UP:
			slave->link = BOND_LINK_UP;
			slave->jiffies = jiffies;

			if (bond->params.mode == BOND_MODE_8023AD) {
				/* prevent it from being the active one */
				slave->state = BOND_STATE_BACKUP;
			} else if (bond->params.mode != BOND_MODE_ACTIVEBACKUP) {
				/* make it immediately active */
				slave->state = BOND_STATE_ACTIVE;
			} else if (slave != bond->primary_slave) {
				/* prevent it from being the active one */
				slave->state = BOND_STATE_BACKUP;
			}

			printk(KERN_INFO DRV_NAME
			       ": %s: link status definitely "
			       "up for interface %s.\n",
			       bond->dev->name, slave->dev->name);

			/* notify ad that the link status has changed */
			if (bond->params.mode == BOND_MODE_8023AD)
				bond_3ad_handle_link_change(slave, BOND_LINK_UP);

			if ((bond->params.mode == BOND_MODE_TLB) ||
			    (bond->params.mode == BOND_MODE_ALB))
				bond_alb_handle_link_change(bond, slave,
							    BOND_LINK_UP);

			if (!bond->curr_active_slave ||
			    (slave == bond->primary_slave))
			bond_set_slave_link_state(slave, BOND_LINK_UP);
			slave->last_link_up = jiffies;

			primary = rtnl_dereference(bond->primary_slave);
			if (BOND_MODE(bond) == BOND_MODE_8023AD) {
				/* prevent it from being the active one */
				bond_set_backup_slave(slave);
			} else if (BOND_MODE(bond) != BOND_MODE_ACTIVEBACKUP) {
				/* make it immediately active */
				bond_set_active_slave(slave);
			} else if (slave != primary) {
				/* prevent it from being the active one */
				bond_set_backup_slave(slave);
			}

			netdev_info(bond->dev, "link status definitely up for interface %s, %u Mbps %s duplex\n",
				    slave->dev->name,
				    slave->speed == SPEED_UNKNOWN ? 0 : slave->speed,
				    slave->duplex ? "full" : "half");

			/* notify ad that the link status has changed */
			if (BOND_MODE(bond) == BOND_MODE_8023AD)
				bond_3ad_handle_link_change(slave, BOND_LINK_UP);

			if (bond_is_lb(bond))
				bond_alb_handle_link_change(bond, slave,
							    BOND_LINK_UP);

			if (BOND_MODE(bond) == BOND_MODE_XOR)
				bond_update_slave_arr(bond, NULL);

			if (!bond->curr_active_slave || slave == primary)
				goto do_failover;

			continue;

		case BOND_LINK_DOWN:
			slave->link = BOND_LINK_DOWN;

			if (bond->params.mode == BOND_MODE_ACTIVEBACKUP ||
			    bond->params.mode == BOND_MODE_8023AD)
				bond_set_slave_inactive_flags(slave);

			printk(KERN_INFO DRV_NAME
			       ": %s: link status definitely down for "
			       "interface %s, disabling it\n",
			       bond->dev->name, slave->dev->name);

			if (bond->params.mode == BOND_MODE_8023AD)
				bond_3ad_handle_link_change(slave,
							    BOND_LINK_DOWN);

			if (bond->params.mode == BOND_MODE_TLB ||
			    bond->params.mode == BOND_MODE_ALB)
				bond_alb_handle_link_change(bond, slave,
							    BOND_LINK_DOWN);

			if (slave == bond->curr_active_slave)
			if (slave->link_failure_count < UINT_MAX)
				slave->link_failure_count++;

			bond_set_slave_link_state(slave, BOND_LINK_DOWN);

			if (BOND_MODE(bond) == BOND_MODE_ACTIVEBACKUP ||
			    BOND_MODE(bond) == BOND_MODE_8023AD)
				bond_set_slave_inactive_flags(slave,
							      BOND_SLAVE_NOTIFY_NOW);

			netdev_info(bond->dev, "link status definitely down for interface %s, disabling it\n",
				    slave->dev->name);

			if (BOND_MODE(bond) == BOND_MODE_8023AD)
				bond_3ad_handle_link_change(slave,
							    BOND_LINK_DOWN);

			if (bond_is_lb(bond))
				bond_alb_handle_link_change(bond, slave,
							    BOND_LINK_DOWN);

			if (BOND_MODE(bond) == BOND_MODE_XOR)
				bond_update_slave_arr(bond, NULL);

			if (slave == rcu_access_pointer(bond->curr_active_slave))
				goto do_failover;

			continue;

		default:
			printk(KERN_ERR DRV_NAME
			       ": %s: invalid new link %d on slave %s\n",
			       bond->dev->name, slave->new_link,
			       slave->dev->name);
			netdev_err(bond->dev, "invalid new link %d on slave %s\n",
				   slave->new_link, slave->dev->name);
			slave->new_link = BOND_LINK_NOCHANGE;

			continue;
		}

do_failover:
		ASSERT_RTNL();
		write_lock_bh(&bond->curr_slave_lock);
		bond_select_active_slave(bond);
		write_unlock_bh(&bond->curr_slave_lock);
		block_netpoll_tx();
		bond_select_active_slave(bond);
		unblock_netpoll_tx();
	}

	bond_set_carrier(bond);
}

/*
 * bond_mii_monitor
/* bond_mii_monitor
 *
 * Really a wrapper that splits the mii monitor into two phases: an
 * inspection, then (if inspection indicates something needs to be done)
 * an acquisition of appropriate locks followed by a commit phase to
 * implement whatever link state changes are indicated.
 */
void bond_mii_monitor(struct work_struct *work)
{
	struct bonding *bond = container_of(work, struct bonding,
					    mii_work.work);

	read_lock(&bond->lock);
	if (bond->kill_timers)
		goto out;

	if (bond->slave_cnt == 0)
		goto re_arm;

	if (bond->send_grat_arp) {
		read_lock(&bond->curr_slave_lock);
		bond_send_gratuitous_arp(bond);
		read_unlock(&bond->curr_slave_lock);
	}

	if (bond_miimon_inspect(bond)) {
		read_unlock(&bond->lock);
		rtnl_lock();
		read_lock(&bond->lock);

		bond_miimon_commit(bond);

		read_unlock(&bond->lock);
		rtnl_unlock();	/* might sleep, hold no other locks */
		read_lock(&bond->lock);
	}

re_arm:
	if (bond->params.miimon)
		queue_delayed_work(bond->wq, &bond->mii_work,
				   msecs_to_jiffies(bond->params.miimon));
out:
	read_unlock(&bond->lock);
}

static __be32 bond_glean_dev_ip(struct net_device *dev)
{
	struct in_device *idev;
	struct in_ifaddr *ifa;
	__be32 addr = 0;

	if (!dev)
		return 0;

	rcu_read_lock();
	idev = __in_dev_get_rcu(dev);
	if (!idev)
		goto out;

	ifa = idev->ifa_list;
	if (!ifa)
		goto out;

	addr = ifa->ifa_local;
out:
	rcu_read_unlock();
	return addr;
}

static int bond_has_this_ip(struct bonding *bond, __be32 ip)
{
	struct vlan_entry *vlan;

	if (ip == bond->master_ip)
		return 1;

	list_for_each_entry(vlan, &bond->vlan_list, vlan_list) {
		if (ip == vlan->vlan_ip)
			return 1;
	}

	return 0;
}

/*
 * We go to the (large) trouble of VLAN tagging ARP frames because
 * switches in VLAN mode (especially if ports are configured as
 * "native" to a VLAN) might not pass non-tagged frames.
 */
static void bond_arp_send(struct net_device *slave_dev, int arp_op, __be32 dest_ip, __be32 src_ip, unsigned short vlan_id)
{
	struct sk_buff *skb;

	dprintk("arp %d on slave %s: dst %x src %x vid %d\n", arp_op,
	       slave_dev->name, dest_ip, src_ip, vlan_id);
	       
static void bond_mii_monitor(struct work_struct *work)
{
	struct bonding *bond = container_of(work, struct bonding,
					    mii_work.work);
	bool should_notify_peers = false;
	unsigned long delay;

	delay = msecs_to_jiffies(bond->params.miimon);

	if (!bond_has_slaves(bond))
		goto re_arm;

	rcu_read_lock();

	should_notify_peers = bond_should_notify_peers(bond);

	if (bond_miimon_inspect(bond)) {
		rcu_read_unlock();

		/* Race avoidance with bond_close cancel of workqueue */
		if (!rtnl_trylock()) {
			delay = 1;
			should_notify_peers = false;
			goto re_arm;
		}

		bond_miimon_commit(bond);

		rtnl_unlock();	/* might sleep, hold no other locks */
	} else
		rcu_read_unlock();

re_arm:
	if (bond->params.miimon)
		queue_delayed_work(bond->wq, &bond->mii_work, delay);

	if (should_notify_peers) {
		if (!rtnl_trylock())
			return;
		call_netdevice_notifiers(NETDEV_NOTIFY_PEERS, bond->dev);
		rtnl_unlock();
	}
}

static bool bond_has_this_ip(struct bonding *bond, __be32 ip)
{
	struct net_device *upper;
	struct list_head *iter;
	bool ret = false;

	if (ip == bond_confirm_addr(bond->dev, 0, ip))
		return true;

	rcu_read_lock();
	netdev_for_each_all_upper_dev_rcu(bond->dev, upper, iter) {
		if (ip == bond_confirm_addr(upper, 0, ip)) {
			ret = true;
			break;
		}
	}
	rcu_read_unlock();

	return ret;
}

/* We go to the (large) trouble of VLAN tagging ARP frames because
 * switches in VLAN mode (especially if ports are configured as
 * "native" to a VLAN) might not pass non-tagged frames.
 */
static void bond_arp_send(struct net_device *slave_dev, int arp_op,
			  __be32 dest_ip, __be32 src_ip,
			  struct bond_vlan_tag *tags)
{
	struct sk_buff *skb;
	struct bond_vlan_tag *outer_tag = tags;

	netdev_dbg(slave_dev, "arp %d on slave %s: dst %pI4 src %pI4\n",
		   arp_op, slave_dev->name, &dest_ip, &src_ip);

	skb = arp_create(arp_op, ETH_P_ARP, dest_ip, slave_dev, src_ip,
			 NULL, slave_dev->dev_addr, NULL);

	if (!skb) {
		printk(KERN_ERR DRV_NAME ": ARP packet allocation failed\n");
		return;
	}
	if (vlan_id) {
		skb = vlan_put_tag(skb, vlan_id);
		if (!skb) {
			printk(KERN_ERR DRV_NAME ": failed to insert VLAN tag\n");
			return;
		}
	}
	arp_xmit(skb);
}


static void bond_arp_send_all(struct bonding *bond, struct slave *slave)
{
	int i, vlan_id, rv;
	__be32 *targets = bond->params.arp_targets;
	struct vlan_entry *vlan;
	struct net_device *vlan_dev;
	struct flowi fl;
	struct rtable *rt;

	for (i = 0; (i < BOND_MAX_ARP_TARGETS); i++) {
		if (!targets[i])
			continue;
		dprintk("basa: target %x\n", targets[i]);
		if (list_empty(&bond->vlan_list)) {
			dprintk("basa: empty vlan: arp_send\n");
			bond_arp_send(slave->dev, ARPOP_REQUEST, targets[i],
				      bond->master_ip, 0);
			continue;
		}

		/*
		 * If VLANs are configured, we do a route lookup to
		 * determine which VLAN interface would be used, so we
		 * can tag the ARP with the proper VLAN tag.
		 */
		memset(&fl, 0, sizeof(fl));
		fl.fl4_dst = targets[i];
		fl.fl4_tos = RTO_ONLINK;

		rv = ip_route_output_key(&init_net, &rt, &fl);
		if (rv) {
			if (net_ratelimit()) {
				printk(KERN_WARNING DRV_NAME
			     ": %s: no route to arp_ip_target %u.%u.%u.%u\n",
				       bond->dev->name, NIPQUAD(fl.fl4_dst));
			}
			continue;
		}

		/*
		 * This target is not on a VLAN
		 */
		if (rt->u.dst.dev == bond->dev) {
			ip_rt_put(rt);
			dprintk("basa: rtdev == bond->dev: arp_send\n");
			bond_arp_send(slave->dev, ARPOP_REQUEST, targets[i],
				      bond->master_ip, 0);
			continue;
		}

		vlan_id = 0;
		list_for_each_entry(vlan, &bond->vlan_list, vlan_list) {
			vlan_dev = vlan_group_get_device(bond->vlgrp, vlan->vlan_id);
			if (vlan_dev == rt->u.dst.dev) {
				vlan_id = vlan->vlan_id;
				dprintk("basa: vlan match on %s %d\n",
				       vlan_dev->name, vlan_id);
				break;
			}
		}

		if (vlan_id) {
			ip_rt_put(rt);
			bond_arp_send(slave->dev, ARPOP_REQUEST, targets[i],
				      vlan->vlan_ip, vlan_id);
			continue;
		}

		if (net_ratelimit()) {
			printk(KERN_WARNING DRV_NAME
	       ": %s: no path to arp_ip_target %u.%u.%u.%u via rt.dev %s\n",
			       bond->dev->name, NIPQUAD(fl.fl4_dst),
			       rt->u.dst.dev ? rt->u.dst.dev->name : "NULL");
		}
		ip_rt_put(rt);
	}
}

/*
 * Kick out a gratuitous ARP for an IP on the bonding master plus one
 * for each VLAN above us.
 *
 * Caller must hold curr_slave_lock for read or better
 */
static void bond_send_gratuitous_arp(struct bonding *bond)
{
	struct slave *slave = bond->curr_active_slave;
	struct vlan_entry *vlan;
	struct net_device *vlan_dev;

	dprintk("bond_send_grat_arp: bond %s slave %s\n", bond->dev->name,
				slave ? slave->dev->name : "NULL");

	if (!slave || !bond->send_grat_arp ||
	    test_bit(__LINK_STATE_LINKWATCH_PENDING, &slave->dev->state))
		return;

	bond->send_grat_arp--;

	if (bond->master_ip) {
		bond_arp_send(slave->dev, ARPOP_REPLY, bond->master_ip,
				bond->master_ip, 0);
	}

	list_for_each_entry(vlan, &bond->vlan_list, vlan_list) {
		vlan_dev = vlan_group_get_device(bond->vlgrp, vlan->vlan_id);
		if (vlan->vlan_ip) {
			bond_arp_send(slave->dev, ARPOP_REPLY, vlan->vlan_ip,
				      vlan->vlan_ip, vlan->vlan_id);
		}
		net_err_ratelimited("ARP packet allocation failed\n");
		return;
	}

	if (!tags || tags->vlan_proto == VLAN_N_VID)
		goto xmit;

	tags++;

	/* Go through all the tags backwards and add them to the packet */
	while (tags->vlan_proto != VLAN_N_VID) {
		if (!tags->vlan_id) {
			tags++;
			continue;
		}

		netdev_dbg(slave_dev, "inner tag: proto %X vid %X\n",
			   ntohs(outer_tag->vlan_proto), tags->vlan_id);
		skb = vlan_insert_tag_set_proto(skb, tags->vlan_proto,
						tags->vlan_id);
		if (!skb) {
			net_err_ratelimited("failed to insert inner VLAN tag\n");
			return;
		}

		tags++;
	}
	/* Set the outer tag */
	if (outer_tag->vlan_id) {
		netdev_dbg(slave_dev, "outer tag: proto %X vid %X\n",
			   ntohs(outer_tag->vlan_proto), outer_tag->vlan_id);
		__vlan_hwaccel_put_tag(skb, outer_tag->vlan_proto,
				       outer_tag->vlan_id);
	}

xmit:
	arp_xmit(skb);
}

/* Validate the device path between the @start_dev and the @end_dev.
 * The path is valid if the @end_dev is reachable through device
 * stacking.
 * When the path is validated, collect any vlan information in the
 * path.
 */
struct bond_vlan_tag *bond_verify_device_path(struct net_device *start_dev,
					      struct net_device *end_dev,
					      int level)
{
	struct bond_vlan_tag *tags;
	struct net_device *upper;
	struct list_head  *iter;

	if (start_dev == end_dev) {
		tags = kzalloc(sizeof(*tags) * (level + 1), GFP_ATOMIC);
		if (!tags) {
			net_err_ratelimited("%s: %s: Failed to allocate tags\n",
					    __func__, start_dev->name);
			return ERR_PTR(-ENOMEM);
		}
		tags[level].vlan_proto = VLAN_N_VID;
		return tags;
	}

	netdev_for_each_upper_dev_rcu(start_dev, upper, iter) {
		tags = bond_verify_device_path(upper, end_dev, level + 1);
		if (IS_ERR_OR_NULL(tags)) {
			if (IS_ERR(tags))
				return tags;
			continue;
		}
		if (is_vlan_dev(upper)) {
			tags[level].vlan_proto = vlan_dev_vlan_proto(upper);
			tags[level].vlan_id = vlan_dev_vlan_id(upper);
		}

		return tags;
	}

	return NULL;
}

static void bond_arp_send_all(struct bonding *bond, struct slave *slave)
{
	struct rtable *rt;
	struct bond_vlan_tag *tags;
	__be32 *targets = bond->params.arp_targets, addr;
	int i;

	for (i = 0; i < BOND_MAX_ARP_TARGETS && targets[i]; i++) {
		netdev_dbg(bond->dev, "basa: target %pI4\n", &targets[i]);
		tags = NULL;

		/* Find out through which dev should the packet go */
		rt = ip_route_output(dev_net(bond->dev), targets[i], 0,
				     RTO_ONLINK, 0);
		if (IS_ERR(rt)) {
			/* there's no route to target - try to send arp
			 * probe to generate any traffic (arp_validate=0)
			 */
			if (bond->params.arp_validate)
				net_warn_ratelimited("%s: no route to arp_ip_target %pI4 and arp_validate is set\n",
						     bond->dev->name,
						     &targets[i]);
			bond_arp_send(slave->dev, ARPOP_REQUEST, targets[i],
				      0, tags);
			continue;
		}

		/* bond device itself */
		if (rt->dst.dev == bond->dev)
			goto found;

		rcu_read_lock();
		tags = bond_verify_device_path(bond->dev, rt->dst.dev, 0);
		rcu_read_unlock();

		if (!IS_ERR_OR_NULL(tags))
			goto found;

		/* Not our device - skip */
		netdev_dbg(bond->dev, "no path to arp_ip_target %pI4 via rt.dev %s\n",
			   &targets[i], rt->dst.dev ? rt->dst.dev->name : "NULL");

		ip_rt_put(rt);
		continue;

found:
		addr = bond_confirm_addr(rt->dst.dev, targets[i], 0);
		ip_rt_put(rt);
		bond_arp_send(slave->dev, ARPOP_REQUEST, targets[i],
			      addr, tags);
		kfree(tags);
	}
}

static void bond_validate_arp(struct bonding *bond, struct slave *slave, __be32 sip, __be32 tip)
{
	int i;
	__be32 *targets = bond->params.arp_targets;

	targets = bond->params.arp_targets;
	for (i = 0; (i < BOND_MAX_ARP_TARGETS) && targets[i]; i++) {
		dprintk("bva: sip %u.%u.%u.%u tip %u.%u.%u.%u t[%d] "
			"%u.%u.%u.%u bhti(tip) %d\n",
		       NIPQUAD(sip), NIPQUAD(tip), i, NIPQUAD(targets[i]),
		       bond_has_this_ip(bond, tip));
		if (sip == targets[i]) {
			if (bond_has_this_ip(bond, tip))
				slave->last_arp_rx = jiffies;
			return;
		}
	}
}

static int bond_arp_rcv(struct sk_buff *skb, struct net_device *dev, struct packet_type *pt, struct net_device *orig_dev)
{
	struct arphdr *arp;
	struct slave *slave;
	struct bonding *bond;
	unsigned char *arp_ptr;
	__be32 sip, tip;

	if (dev_net(dev) != &init_net)
		goto out;

	if (!(dev->priv_flags & IFF_BONDING) || !(dev->flags & IFF_MASTER))
		goto out;

	bond = dev->priv;
	read_lock(&bond->lock);

	dprintk("bond_arp_rcv: bond %s skb->dev %s orig_dev %s\n",
		bond->dev->name, skb->dev ? skb->dev->name : "NULL",
		orig_dev ? orig_dev->name : "NULL");

	slave = bond_get_slave_by_dev(bond, orig_dev);
	if (!slave || !slave_do_arp_validate(bond, slave))
		goto out_unlock;

	if (!pskb_may_pull(skb, arp_hdr_len(dev)))
		goto out_unlock;

	arp = arp_hdr(skb);
	if (arp->ar_hln != dev->addr_len ||

	if (!sip || !bond_has_this_ip(bond, tip)) {
		netdev_dbg(bond->dev, "bva: sip %pI4 tip %pI4 not found\n",
			   &sip, &tip);
		return;
	}

	i = bond_get_targets_ip(bond->params.arp_targets, sip);
	if (i == -1) {
		netdev_dbg(bond->dev, "bva: sip %pI4 not found in targets\n",
			   &sip);
		return;
	}
	slave->last_rx = jiffies;
	slave->target_last_arp_rx[i] = jiffies;
}

int bond_arp_rcv(const struct sk_buff *skb, struct bonding *bond,
		 struct slave *slave)
{
	struct arphdr *arp = (struct arphdr *)skb->data;
	struct slave *curr_active_slave, *curr_arp_slave;
	unsigned char *arp_ptr;
	__be32 sip, tip;
	int alen, is_arp = skb->protocol == __cpu_to_be16(ETH_P_ARP);

	if (!slave_do_arp_validate(bond, slave)) {
		if ((slave_do_arp_validate_only(bond) && is_arp) ||
		    !slave_do_arp_validate_only(bond))
			slave->last_rx = jiffies;
		return RX_HANDLER_ANOTHER;
	} else if (!is_arp) {
		return RX_HANDLER_ANOTHER;
	}

	alen = arp_hdr_len(bond->dev);

	netdev_dbg(bond->dev, "bond_arp_rcv: skb->dev %s\n",
		   skb->dev->name);

	if (alen > skb_headlen(skb)) {
		arp = kmalloc(alen, GFP_ATOMIC);
		if (!arp)
			goto out_unlock;
		if (skb_copy_bits(skb, 0, arp, alen) < 0)
			goto out_unlock;
	}

	if (arp->ar_hln != bond->dev->addr_len ||
	    skb->pkt_type == PACKET_OTHERHOST ||
	    skb->pkt_type == PACKET_LOOPBACK ||
	    arp->ar_hrd != htons(ARPHRD_ETHER) ||
	    arp->ar_pro != htons(ETH_P_IP) ||
	    arp->ar_pln != 4)
		goto out_unlock;

	arp_ptr = (unsigned char *)(arp + 1);
	arp_ptr += dev->addr_len;
	memcpy(&sip, arp_ptr, 4);
	arp_ptr += 4 + dev->addr_len;
	memcpy(&tip, arp_ptr, 4);

	dprintk("bond_arp_rcv: %s %s/%d av %d sv %d sip %u.%u.%u.%u"
		" tip %u.%u.%u.%u\n", bond->dev->name, slave->dev->name,
		slave->state, bond->params.arp_validate,
		slave_do_arp_validate(bond, slave), NIPQUAD(sip), NIPQUAD(tip));

	/*
	 * Backup slaves won't see the ARP reply, but do come through
	arp_ptr += bond->dev->addr_len;
	memcpy(&sip, arp_ptr, 4);
	arp_ptr += 4 + bond->dev->addr_len;
	memcpy(&tip, arp_ptr, 4);

	netdev_dbg(bond->dev, "bond_arp_rcv: %s/%d av %d sv %d sip %pI4 tip %pI4\n",
		   slave->dev->name, bond_slave_state(slave),
		     bond->params.arp_validate, slave_do_arp_validate(bond, slave),
		     &sip, &tip);

	curr_active_slave = rcu_dereference(bond->curr_active_slave);
	curr_arp_slave = rcu_dereference(bond->current_arp_slave);

	/* Backup slaves won't see the ARP reply, but do come through
	 * here for each ARP probe (so we swap the sip/tip to validate
	 * the probe).  In a "redundant switch, common router" type of
	 * configuration, the ARP probe will (hopefully) travel from
	 * the active, through one switch, the router, then the other
	 * switch before reaching the backup.
	 */
	if (slave->state == BOND_STATE_ACTIVE)
		bond_validate_arp(bond, slave, sip, tip);
	else
		bond_validate_arp(bond, slave, tip, sip);

out_unlock:
	read_unlock(&bond->lock);
out:
	dev_kfree_skb(skb);
	return NET_RX_SUCCESS;
}

/*
 * this function is called regularly to monitor each slave's link
	/* We 'trust' the received ARP enough to validate it if:
	 *
	 * (a) the slave receiving the ARP is active (which includes the
	 * current ARP slave, if any), or
	 *
	 * (b) the receiving slave isn't active, but there is a currently
	 * active slave and it received valid arp reply(s) after it became
	 * the currently active slave, or
	 *
	 * (c) there is an ARP slave that sent an ARP during the prior ARP
	 * interval, and we receive an ARP reply on any slave.  We accept
	 * these because switch FDB update delays may deliver the ARP
	 * reply to a slave other than the sender of the ARP request.
	 *
	 * Note: for (b), backup slaves are receiving the broadcast ARP
	 * request, not a reply.  This request passes from the sending
	 * slave through the L2 switch(es) to the receiving slave.  Since
	 * this is checking the request, sip/tip are swapped for
	 * validation.
	 *
	 * This is done to avoid endless looping when we can't reach the
	 * arp_ip_target and fool ourselves with our own arp requests.
	 */
	if (bond_is_active_slave(slave))
		bond_validate_arp(bond, slave, sip, tip);
	else if (curr_active_slave &&
		 time_after(slave_last_rx(bond, curr_active_slave),
			    curr_active_slave->last_link_up))
		bond_validate_arp(bond, slave, tip, sip);
	else if (curr_arp_slave && (arp->ar_op == htons(ARPOP_REPLY)) &&
		 bond_time_in_interval(bond,
				       dev_trans_start(curr_arp_slave->dev), 1))
		bond_validate_arp(bond, slave, sip, tip);

out_unlock:
	if (arp != (struct arphdr *)skb->data)
		kfree(arp);
	return RX_HANDLER_ANOTHER;
}

/* function to verify if we're in the arp_interval timeslice, returns true if
 * (last_act - arp_interval) <= jiffies <= (last_act + mod * arp_interval +
 * arp_interval/2) . the arp_interval/2 is needed for really fast networks.
 */
static bool bond_time_in_interval(struct bonding *bond, unsigned long last_act,
				  int mod)
{
	int delta_in_ticks = msecs_to_jiffies(bond->params.arp_interval);

	return time_in_range(jiffies,
			     last_act - delta_in_ticks,
			     last_act + mod * delta_in_ticks + delta_in_ticks/2);
}

/* This function is called regularly to monitor each slave's link
 * ensuring that traffic is being sent and received when arp monitoring
 * is used in load-balancing mode. if the adapter has been dormant, then an
 * arp is transmitted to generate traffic. see activebackup_arp_monitor for
 * arp monitoring in active backup mode.
 */
void bond_loadbalance_arp_mon(struct work_struct *work)
static void bond_loadbalance_arp_mon(struct work_struct *work)
{
	struct bonding *bond = container_of(work, struct bonding,
					    arp_work.work);
	struct slave *slave, *oldcurrent;
	int do_failover = 0;
	int delta_in_ticks;
	int i;

	read_lock(&bond->lock);

	delta_in_ticks = msecs_to_jiffies(bond->params.arp_interval);

	if (bond->kill_timers) {
		goto out;
	}

	if (bond->slave_cnt == 0) {
		goto re_arm;
	}

	read_lock(&bond->curr_slave_lock);
	oldcurrent = bond->curr_active_slave;
	read_unlock(&bond->curr_slave_lock);

	/* see if any of the previous devices are up now (i.e. they have
	 * xmt and rcv traffic). the curr_active_slave does not come into
	 * the picture unless it is null. also, slave->jiffies is not needed
	 * here because we send an arp on each slave and give a slave as
	 * long as it needs to get the tx/rx within the delta.
	 * TODO: what about up/down delay in arp mode? it wasn't here before
	 *       so it can wait
	 */
	bond_for_each_slave(bond, slave, i) {
		if (slave->link != BOND_LINK_UP) {
			if (time_before_eq(jiffies, slave->dev->trans_start + delta_in_ticks) &&
			    time_before_eq(jiffies, slave->dev->last_rx + delta_in_ticks)) {

				slave->link  = BOND_LINK_UP;
				slave->state = BOND_STATE_ACTIVE;
	struct list_head *iter;
	int do_failover = 0, slave_state_changed = 0;

	if (!bond_has_slaves(bond))
		goto re_arm;

	rcu_read_lock();

	oldcurrent = rcu_dereference(bond->curr_active_slave);
	/* see if any of the previous devices are up now (i.e. they have
	 * xmt and rcv traffic). the curr_active_slave does not come into
	 * the picture unless it is null. also, slave->last_link_up is not
	 * needed here because we send an arp on each slave and give a slave
	 * as long as it needs to get the tx/rx within the delta.
	 * TODO: what about up/down delay in arp mode? it wasn't here before
	 *       so it can wait
	 */
	bond_for_each_slave_rcu(bond, slave, iter) {
		unsigned long trans_start = dev_trans_start(slave->dev);

		slave->new_link = BOND_LINK_NOCHANGE;

		if (slave->link != BOND_LINK_UP) {
			if (bond_time_in_interval(bond, trans_start, 1) &&
			    bond_time_in_interval(bond, slave->last_rx, 1)) {

				slave->new_link = BOND_LINK_UP;
				slave_state_changed = 1;

				/* primary_slave has no meaning in round-robin
				 * mode. the window of a slave being up and
				 * curr_active_slave being null after enslaving
				 * is closed.
				 */
				if (!oldcurrent) {
					printk(KERN_INFO DRV_NAME
					       ": %s: link status definitely "
					       "up for interface %s, ",
					       bond->dev->name,
					       slave->dev->name);
					do_failover = 1;
				} else {
					printk(KERN_INFO DRV_NAME
					       ": %s: interface %s is now up\n",
					       bond->dev->name,
					       slave->dev->name);
					netdev_info(bond->dev, "link status definitely up for interface %s\n",
						    slave->dev->name);
					do_failover = 1;
				} else {
					netdev_info(bond->dev, "interface %s is now up\n",
						    slave->dev->name);
				}
			}
		} else {
			/* slave->link == BOND_LINK_UP */

			/* not all switches will respond to an arp request
			 * when the source ip is 0, so don't take the link down
			 * if we don't know our ip yet
			 */
			if (time_after_eq(jiffies, slave->dev->trans_start + 2*delta_in_ticks) ||
			    (time_after_eq(jiffies, slave->dev->last_rx + 2*delta_in_ticks))) {

				slave->link  = BOND_LINK_DOWN;
				slave->state = BOND_STATE_BACKUP;

				if (slave->link_failure_count < UINT_MAX) {
					slave->link_failure_count++;
				}

				printk(KERN_INFO DRV_NAME
				       ": %s: interface %s is now down.\n",
				       bond->dev->name,
				       slave->dev->name);

				if (slave == oldcurrent) {
					do_failover = 1;
				}
			if (!bond_time_in_interval(bond, trans_start, 2) ||
			    !bond_time_in_interval(bond, slave->last_rx, 2)) {

				slave->new_link = BOND_LINK_DOWN;
				slave_state_changed = 1;

				if (slave->link_failure_count < UINT_MAX)
					slave->link_failure_count++;

				netdev_info(bond->dev, "interface %s is now down\n",
					    slave->dev->name);

				if (slave == oldcurrent)
					do_failover = 1;
			}
		}

		/* note: if switch is in round-robin mode, all links
		 * must tx arp to ensure all links rx an arp - otherwise
		 * links may oscillate or not come up at all; if switch is
		 * in something like xor mode, there is nothing we can
		 * do - all replies will be rx'ed on same link causing slaves
		 * to be unstable during low/no traffic periods
		 */
		if (IS_UP(slave->dev)) {
			bond_arp_send_all(bond, slave);
		}
	}

	if (do_failover) {
		write_lock_bh(&bond->curr_slave_lock);

		bond_select_active_slave(bond);

		write_unlock_bh(&bond->curr_slave_lock);
		if (bond_slave_is_up(slave))
			bond_arp_send_all(bond, slave);
	}

	rcu_read_unlock();

	if (do_failover || slave_state_changed) {
		if (!rtnl_trylock())
			goto re_arm;

		bond_for_each_slave(bond, slave, iter) {
			if (slave->new_link != BOND_LINK_NOCHANGE)
				slave->link = slave->new_link;
		}

		if (slave_state_changed) {
			bond_slave_state_change(bond);
			if (BOND_MODE(bond) == BOND_MODE_XOR)
				bond_update_slave_arr(bond, NULL);
		}
		if (do_failover) {
			block_netpoll_tx();
			bond_select_active_slave(bond);
			unblock_netpoll_tx();
		}
		rtnl_unlock();
	}

re_arm:
	if (bond->params.arp_interval)
		queue_delayed_work(bond->wq, &bond->arp_work, delta_in_ticks);
out:
	read_unlock(&bond->lock);
}

/*
 * Called to inspect slaves for active-backup mode ARP monitor link state
		queue_delayed_work(bond->wq, &bond->arp_work,
				   msecs_to_jiffies(bond->params.arp_interval));
}

/* Called to inspect slaves for active-backup mode ARP monitor link state
 * changes.  Sets new_link in slaves to specify what action should take
 * place for the slave.  Returns 0 if no changes are found, >0 if changes
 * to link states must be committed.
 *
 * Called with bond->lock held for read.
 */
static int bond_ab_arp_inspect(struct bonding *bond, int delta_in_ticks)
{
	struct slave *slave;
	int i, commit = 0;

	bond_for_each_slave(bond, slave, i) {
		slave->new_link = BOND_LINK_NOCHANGE;

		if (slave->link != BOND_LINK_UP) {
			if (time_before_eq(jiffies, slave_last_rx(bond, slave) +
					   delta_in_ticks)) {
				slave->new_link = BOND_LINK_UP;
				commit++;
			}

			continue;
		}

		/*
		 * Give slaves 2*delta after being enslaved or made
		 * active.  This avoids bouncing, as the last receive
		 * times need a full ARP monitor cycle to be updated.
		 */
		if (!time_after_eq(jiffies, slave->jiffies +
				   2 * delta_in_ticks))
			continue;

		/*
		 * Backup slave is down if:
 * Called with rcu_read_lock held.
 */
static int bond_ab_arp_inspect(struct bonding *bond)
{
	unsigned long trans_start, last_rx;
	struct list_head *iter;
	struct slave *slave;
	int commit = 0;

	bond_for_each_slave_rcu(bond, slave, iter) {
		slave->new_link = BOND_LINK_NOCHANGE;
		last_rx = slave_last_rx(bond, slave);

		if (slave->link != BOND_LINK_UP) {
			if (bond_time_in_interval(bond, last_rx, 1)) {
				slave->new_link = BOND_LINK_UP;
				commit++;
			}
			continue;
		}

		/* Give slaves 2*delta after being enslaved or made
		 * active.  This avoids bouncing, as the last receive
		 * times need a full ARP monitor cycle to be updated.
		 */
		if (bond_time_in_interval(bond, slave->last_link_up, 2))
			continue;

		/* Backup slave is down if:
		 * - No current_arp_slave AND
		 * - more than 3*delta since last receive AND
		 * - the bond has an IP address
		 *
		 * Note: a non-null current_arp_slave indicates
		 * the curr_active_slave went down and we are
		 * searching for a new one; under this condition
		 * we only take the curr_active_slave down - this
		 * gives each slave a chance to tx/rx traffic
		 * before being taken out
		 */
		if (slave->state == BOND_STATE_BACKUP &&
		    !bond->current_arp_slave &&
		    time_after(jiffies, slave_last_rx(bond, slave) +
			       3 * delta_in_ticks)) {
		if (!bond_is_active_slave(slave) &&
		    !rcu_access_pointer(bond->current_arp_slave) &&
		    !bond_time_in_interval(bond, last_rx, 3)) {
			slave->new_link = BOND_LINK_DOWN;
			commit++;
		}

		/*
		 * Active slave is down if:
		/* Active slave is down if:
		 * - more than 2*delta since transmitting OR
		 * - (more than 2*delta since receive AND
		 *    the bond has an IP address)
		 */
		if ((slave->state == BOND_STATE_ACTIVE) &&
		    (time_after_eq(jiffies, slave->dev->trans_start +
				    2 * delta_in_ticks) ||
		      (time_after_eq(jiffies, slave_last_rx(bond, slave)
				     + 2 * delta_in_ticks)))) {
		trans_start = dev_trans_start(slave->dev);
		if (bond_is_active_slave(slave) &&
		    (!bond_time_in_interval(bond, trans_start, 2) ||
		     !bond_time_in_interval(bond, last_rx, 2))) {
			slave->new_link = BOND_LINK_DOWN;
			commit++;
		}
	}

	read_lock(&bond->curr_slave_lock);

	/*
	 * Trigger a commit if the primary option setting has changed.
	 */
	if (bond->primary_slave &&
	    (bond->primary_slave != bond->curr_active_slave) &&
	    (bond->primary_slave->link == BOND_LINK_UP))
		commit++;

	read_unlock(&bond->curr_slave_lock);

	return commit;
}

/*
 * Called to commit link state changes noted by inspection step of
 * active-backup mode ARP monitor.
 *
 * Called with RTNL and bond->lock for read.
 */
static void bond_ab_arp_commit(struct bonding *bond, int delta_in_ticks)
{
	struct slave *slave;
	int i;

	bond_for_each_slave(bond, slave, i) {
	return commit;
}

/* Called to commit link state changes noted by inspection step of
 * active-backup mode ARP monitor.
 *
 * Called with RTNL hold.
 */
static void bond_ab_arp_commit(struct bonding *bond)
{
	unsigned long trans_start;
	struct list_head *iter;
	struct slave *slave;

	bond_for_each_slave(bond, slave, iter) {
		switch (slave->new_link) {
		case BOND_LINK_NOCHANGE:
			continue;

		case BOND_LINK_UP:
			write_lock_bh(&bond->curr_slave_lock);

			if (!bond->curr_active_slave &&
			    time_before_eq(jiffies, slave->dev->trans_start +
					   delta_in_ticks)) {
				slave->link = BOND_LINK_UP;
				bond_change_active_slave(bond, slave);
				bond->current_arp_slave = NULL;

				printk(KERN_INFO DRV_NAME
				       ": %s: %s is up and now the "
				       "active interface\n",
				       bond->dev->name, slave->dev->name);

			} else if (bond->curr_active_slave != slave) {
				/* this slave has just come up but we
				 * already have a current slave; this can
				 * also happen if bond_enslave adds a new
				 * slave that is up while we are searching
				 * for a new slave
				 */
				slave->link = BOND_LINK_UP;
				bond_set_slave_inactive_flags(slave);
				bond->current_arp_slave = NULL;

				printk(KERN_INFO DRV_NAME
				       ": %s: backup interface %s is now up\n",
				       bond->dev->name, slave->dev->name);
			}

			write_unlock_bh(&bond->curr_slave_lock);

			break;
			trans_start = dev_trans_start(slave->dev);
			if (rtnl_dereference(bond->curr_active_slave) != slave ||
			    (!rtnl_dereference(bond->curr_active_slave) &&
			     bond_time_in_interval(bond, trans_start, 1))) {
				struct slave *current_arp_slave;

				current_arp_slave = rtnl_dereference(bond->current_arp_slave);
				bond_set_slave_link_state(slave, BOND_LINK_UP);
				if (current_arp_slave) {
					bond_set_slave_inactive_flags(
						current_arp_slave,
						BOND_SLAVE_NOTIFY_NOW);
					RCU_INIT_POINTER(bond->current_arp_slave, NULL);
				}

				netdev_info(bond->dev, "link status definitely up for interface %s\n",
					    slave->dev->name);

				if (!rtnl_dereference(bond->curr_active_slave) ||
				    slave == rtnl_dereference(bond->primary_slave))
					goto do_failover;

			}

			continue;

		case BOND_LINK_DOWN:
			if (slave->link_failure_count < UINT_MAX)
				slave->link_failure_count++;

			slave->link = BOND_LINK_DOWN;

			if (slave == bond->curr_active_slave) {
				printk(KERN_INFO DRV_NAME
				       ": %s: link status down for active "
				       "interface %s, disabling it\n",
				       bond->dev->name, slave->dev->name);

				bond_set_slave_inactive_flags(slave);

				write_lock_bh(&bond->curr_slave_lock);

				bond_select_active_slave(bond);
				if (bond->curr_active_slave)
					bond->curr_active_slave->jiffies =
						jiffies;

				write_unlock_bh(&bond->curr_slave_lock);

				bond->current_arp_slave = NULL;

			} else if (slave->state == BOND_STATE_BACKUP) {
				printk(KERN_INFO DRV_NAME
				       ": %s: backup interface %s is now down\n",
				       bond->dev->name, slave->dev->name);

				bond_set_slave_inactive_flags(slave);
			}
			break;

		default:
			printk(KERN_ERR DRV_NAME
			       ": %s: impossible: new_link %d on slave %s\n",
			       bond->dev->name, slave->new_link,
			       slave->dev->name);
		}
	}

	/*
	 * No race with changes to primary via sysfs, as we hold rtnl.
	 */
	if (bond->primary_slave &&
	    (bond->primary_slave != bond->curr_active_slave) &&
	    (bond->primary_slave->link == BOND_LINK_UP)) {
		write_lock_bh(&bond->curr_slave_lock);
		bond_change_active_slave(bond, bond->primary_slave);
		write_unlock_bh(&bond->curr_slave_lock);
			bond_set_slave_link_state(slave, BOND_LINK_DOWN);
			bond_set_slave_inactive_flags(slave,
						      BOND_SLAVE_NOTIFY_NOW);

			netdev_info(bond->dev, "link status definitely down for interface %s, disabling it\n",
				    slave->dev->name);

			if (slave == rtnl_dereference(bond->curr_active_slave)) {
				RCU_INIT_POINTER(bond->current_arp_slave, NULL);
				goto do_failover;
			}

			continue;

		default:
			netdev_err(bond->dev, "impossible: new_link %d on slave %s\n",
				   slave->new_link, slave->dev->name);
			continue;
		}

do_failover:
		block_netpoll_tx();
		bond_select_active_slave(bond);
		unblock_netpoll_tx();
	}

	bond_set_carrier(bond);
}

/*
 * Send ARP probes for active-backup mode ARP monitor.
 *
 * Called with bond->lock held for read.
 */
static void bond_ab_arp_probe(struct bonding *bond)
{
	struct slave *slave;
	int i;

	read_lock(&bond->curr_slave_lock);

	if (bond->current_arp_slave && bond->curr_active_slave)
		printk("PROBE: c_arp %s && cas %s BAD\n",
		       bond->current_arp_slave->dev->name,
		       bond->curr_active_slave->dev->name);

	if (bond->curr_active_slave) {
		bond_arp_send_all(bond, bond->curr_active_slave);
		read_unlock(&bond->curr_slave_lock);
		return;
	}

	read_unlock(&bond->curr_slave_lock);

/* Send ARP probes for active-backup mode ARP monitor.
 *
 * Called with rcu_read_lock held.
 */
static bool bond_ab_arp_probe(struct bonding *bond)
{
	struct slave *slave, *before = NULL, *new_slave = NULL,
		     *curr_arp_slave = rcu_dereference(bond->current_arp_slave),
		     *curr_active_slave = rcu_dereference(bond->curr_active_slave);
	struct list_head *iter;
	bool found = false;
	bool should_notify_rtnl = BOND_SLAVE_NOTIFY_LATER;

	if (curr_arp_slave && curr_active_slave)
		netdev_info(bond->dev, "PROBE: c_arp %s && cas %s BAD\n",
			    curr_arp_slave->dev->name,
			    curr_active_slave->dev->name);

	if (curr_active_slave) {
		bond_arp_send_all(bond, curr_active_slave);
		return should_notify_rtnl;
	}

	/* if we don't have a curr_active_slave, search for the next available
	 * backup slave from the current_arp_slave and make it the candidate
	 * for becoming the curr_active_slave
	 */

	if (!bond->current_arp_slave) {
		bond->current_arp_slave = bond->first_slave;
		if (!bond->current_arp_slave)
			return;
	}

	bond_set_slave_inactive_flags(bond->current_arp_slave);

	/* search for next candidate */
	bond_for_each_slave_from(bond, slave, i, bond->current_arp_slave->next) {
		if (IS_UP(slave->dev)) {
			slave->link = BOND_LINK_BACK;
			bond_set_slave_active_flags(slave);
			bond_arp_send_all(bond, slave);
			slave->jiffies = jiffies;
			bond->current_arp_slave = slave;
			break;
		}

	if (!curr_arp_slave) {
		curr_arp_slave = bond_first_slave_rcu(bond);
		if (!curr_arp_slave)
			return should_notify_rtnl;
	}

	bond_set_slave_inactive_flags(curr_arp_slave, BOND_SLAVE_NOTIFY_LATER);

	bond_for_each_slave_rcu(bond, slave, iter) {
		if (!found && !before && bond_slave_is_up(slave))
			before = slave;

		if (found && !new_slave && bond_slave_is_up(slave))
			new_slave = slave;
		/* if the link state is up at this point, we
		 * mark it down - this can happen if we have
		 * simultaneous link failures and
		 * reselect_active_interface doesn't make this
		 * one the current slave so it is still marked
		 * up when it is actually down
		 */
		if (slave->link == BOND_LINK_UP) {
			slave->link = BOND_LINK_DOWN;
			if (slave->link_failure_count < UINT_MAX)
				slave->link_failure_count++;

			bond_set_slave_inactive_flags(slave);

			printk(KERN_INFO DRV_NAME
			       ": %s: backup interface %s is now down.\n",
			       bond->dev->name, slave->dev->name);
		}
	}
}

void bond_activebackup_arp_mon(struct work_struct *work)
{
	struct bonding *bond = container_of(work, struct bonding,
					    arp_work.work);
	int delta_in_ticks;

	read_lock(&bond->lock);

	if (bond->kill_timers)
		goto out;

	delta_in_ticks = msecs_to_jiffies(bond->params.arp_interval);

	if (bond->slave_cnt == 0)
		goto re_arm;

	if (bond->send_grat_arp) {
		read_lock(&bond->curr_slave_lock);
		bond_send_gratuitous_arp(bond);
		read_unlock(&bond->curr_slave_lock);
	}

	if (bond_ab_arp_inspect(bond, delta_in_ticks)) {
		read_unlock(&bond->lock);
		rtnl_lock();
		read_lock(&bond->lock);

		bond_ab_arp_commit(bond, delta_in_ticks);

		read_unlock(&bond->lock);
		rtnl_unlock();
		read_lock(&bond->lock);
	}

	bond_ab_arp_probe(bond);

re_arm:
	if (bond->params.arp_interval) {
		queue_delayed_work(bond->wq, &bond->arp_work, delta_in_ticks);
	}
out:
	read_unlock(&bond->lock);
}

/*------------------------------ proc/seq_file-------------------------------*/

#ifdef CONFIG_PROC_FS

static void *bond_info_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct bonding *bond = seq->private;
	loff_t off = 0;
	struct slave *slave;
	int i;

	/* make sure the bond won't be taken away */
	read_lock(&dev_base_lock);
	read_lock(&bond->lock);

	if (*pos == 0) {
		return SEQ_START_TOKEN;
	}

	bond_for_each_slave(bond, slave, i) {
		if (++off == *pos) {
			return slave;
		}
	}

	return NULL;
}

static void *bond_info_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct bonding *bond = seq->private;
	struct slave *slave = v;

	++*pos;
	if (v == SEQ_START_TOKEN) {
		return bond->first_slave;
	}

	slave = slave->next;

	return (slave == bond->first_slave) ? NULL : slave;
}

static void bond_info_seq_stop(struct seq_file *seq, void *v)
{
	struct bonding *bond = seq->private;

	read_unlock(&bond->lock);
	read_unlock(&dev_base_lock);
}

static void bond_info_show_master(struct seq_file *seq)
{
	struct bonding *bond = seq->private;
	struct slave *curr;
	int i;
	u32 target;

	read_lock(&bond->curr_slave_lock);
	curr = bond->curr_active_slave;
	read_unlock(&bond->curr_slave_lock);

	seq_printf(seq, "Bonding Mode: %s",
		   bond_mode_name(bond->params.mode));

	if (bond->params.mode == BOND_MODE_ACTIVEBACKUP &&
	    bond->params.fail_over_mac)
		seq_printf(seq, " (fail_over_mac %s)",
		   fail_over_mac_tbl[bond->params.fail_over_mac].modename);

	seq_printf(seq, "\n");

	if (bond->params.mode == BOND_MODE_XOR ||
		bond->params.mode == BOND_MODE_8023AD) {
		seq_printf(seq, "Transmit Hash Policy: %s (%d)\n",
			xmit_hashtype_tbl[bond->params.xmit_policy].modename,
			bond->params.xmit_policy);
	}

	if (USES_PRIMARY(bond->params.mode)) {
		seq_printf(seq, "Primary Slave: %s\n",
			   (bond->primary_slave) ?
			   bond->primary_slave->dev->name : "None");

		seq_printf(seq, "Currently Active Slave: %s\n",
			   (curr) ? curr->dev->name : "None");
	}

	seq_printf(seq, "MII Status: %s\n", netif_carrier_ok(bond->dev) ?
		   "up" : "down");
	seq_printf(seq, "MII Polling Interval (ms): %d\n", bond->params.miimon);
	seq_printf(seq, "Up Delay (ms): %d\n",
		   bond->params.updelay * bond->params.miimon);
	seq_printf(seq, "Down Delay (ms): %d\n",
		   bond->params.downdelay * bond->params.miimon);


	/* ARP information */
	if(bond->params.arp_interval > 0) {
		int printed=0;
		seq_printf(seq, "ARP Polling Interval (ms): %d\n",
				bond->params.arp_interval);

		seq_printf(seq, "ARP IP target/s (n.n.n.n form):");

		for(i = 0; (i < BOND_MAX_ARP_TARGETS) ;i++) {
			if (!bond->params.arp_targets[i])
				continue;
			if (printed)
				seq_printf(seq, ",");
			target = ntohl(bond->params.arp_targets[i]);
			seq_printf(seq, " %d.%d.%d.%d", HIPQUAD(target));
			printed = 1;
		}
		seq_printf(seq, "\n");
	}

	if (bond->params.mode == BOND_MODE_8023AD) {
		struct ad_info ad_info;
		DECLARE_MAC_BUF(mac);

		seq_puts(seq, "\n802.3ad info\n");
		seq_printf(seq, "LACP rate: %s\n",
			   (bond->params.lacp_fast) ? "fast" : "slow");

		if (bond_3ad_get_active_agg_info(bond, &ad_info)) {
			seq_printf(seq, "bond %s has no active aggregator\n",
				   bond->dev->name);
		} else {
			seq_printf(seq, "Active Aggregator Info:\n");

			seq_printf(seq, "\tAggregator ID: %d\n",
				   ad_info.aggregator_id);
			seq_printf(seq, "\tNumber of ports: %d\n",
				   ad_info.ports);
			seq_printf(seq, "\tActor Key: %d\n",
				   ad_info.actor_key);
			seq_printf(seq, "\tPartner Key: %d\n",
				   ad_info.partner_key);
			seq_printf(seq, "\tPartner Mac Address: %s\n",
				   print_mac(mac, ad_info.partner_system));
		}
	}
}

static void bond_info_show_slave(struct seq_file *seq, const struct slave *slave)
{
	struct bonding *bond = seq->private;
	DECLARE_MAC_BUF(mac);

	seq_printf(seq, "\nSlave Interface: %s\n", slave->dev->name);
	seq_printf(seq, "MII Status: %s\n",
		   (slave->link == BOND_LINK_UP) ?  "up" : "down");
	seq_printf(seq, "Link Failure Count: %u\n",
		   slave->link_failure_count);

	seq_printf(seq,
		   "Permanent HW addr: %s\n",
		   print_mac(mac, slave->perm_hwaddr));

	if (bond->params.mode == BOND_MODE_8023AD) {
		const struct aggregator *agg
			= SLAVE_AD_INFO(slave).port.aggregator;

		if (agg) {
			seq_printf(seq, "Aggregator ID: %d\n",
				   agg->aggregator_identifier);
		} else {
			seq_puts(seq, "Aggregator ID: N/A\n");
		}
	}
}

static int bond_info_seq_show(struct seq_file *seq, void *v)
{
	if (v == SEQ_START_TOKEN) {
		seq_printf(seq, "%s\n", version);
		bond_info_show_master(seq);
	} else {
		bond_info_show_slave(seq, v);
	}

	return 0;
}

static struct seq_operations bond_info_seq_ops = {
	.start = bond_info_seq_start,
	.next  = bond_info_seq_next,
	.stop  = bond_info_seq_stop,
	.show  = bond_info_seq_show,
};

static int bond_info_open(struct inode *inode, struct file *file)
{
	struct seq_file *seq;
	struct proc_dir_entry *proc;
	int res;

	res = seq_open(file, &bond_info_seq_ops);
	if (!res) {
		/* recover the pointer buried in proc_dir_entry data */
		seq = file->private_data;
		proc = PDE(inode);
		seq->private = proc->data;
	}

	return res;
}

static const struct file_operations bond_info_fops = {
	.owner   = THIS_MODULE,
	.open    = bond_info_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
};

static int bond_create_proc_entry(struct bonding *bond)
{
	struct net_device *bond_dev = bond->dev;

	if (bond_proc_dir) {
		bond->proc_entry = proc_create_data(bond_dev->name,
						    S_IRUGO, bond_proc_dir,
						    &bond_info_fops, bond);
		if (bond->proc_entry == NULL) {
			printk(KERN_WARNING DRV_NAME
			       ": Warning: Cannot create /proc/net/%s/%s\n",
			       DRV_NAME, bond_dev->name);
		} else {
			memcpy(bond->proc_file_name, bond_dev->name, IFNAMSIZ);
		}
	}

	return 0;
}

static void bond_remove_proc_entry(struct bonding *bond)
{
	if (bond_proc_dir && bond->proc_entry) {
		remove_proc_entry(bond->proc_file_name, bond_proc_dir);
		memset(bond->proc_file_name, 0, IFNAMSIZ);
		bond->proc_entry = NULL;
	}
}

/* Create the bonding directory under /proc/net, if doesn't exist yet.
 * Caller must hold rtnl_lock.
 */
static void bond_create_proc_dir(void)
{
	int len = strlen(DRV_NAME);

	for (bond_proc_dir = init_net.proc_net->subdir; bond_proc_dir;
	     bond_proc_dir = bond_proc_dir->next) {
		if ((bond_proc_dir->namelen == len) &&
		    !memcmp(bond_proc_dir->name, DRV_NAME, len)) {
			break;
		}
	}

	if (!bond_proc_dir) {
		bond_proc_dir = proc_mkdir(DRV_NAME, init_net.proc_net);
		if (bond_proc_dir) {
			bond_proc_dir->owner = THIS_MODULE;
		} else {
			printk(KERN_WARNING DRV_NAME
				": Warning: cannot create /proc/net/%s\n",
				DRV_NAME);
		}
	}
}

/* Destroy the bonding directory under /proc/net, if empty.
 * Caller must hold rtnl_lock.
 */
static void bond_destroy_proc_dir(void)
{
	struct proc_dir_entry *de;

	if (!bond_proc_dir) {
		return;
	}

	/* verify that the /proc dir is empty */
	for (de = bond_proc_dir->subdir; de; de = de->next) {
		/* ignore . and .. */
		if (*(de->name) != '.') {
			break;
		}
	}

	if (de) {
		if (bond_proc_dir->owner == THIS_MODULE) {
			bond_proc_dir->owner = NULL;
		}
	} else {
		remove_proc_entry(DRV_NAME, init_net.proc_net);
		bond_proc_dir = NULL;
	}
}
#endif /* CONFIG_PROC_FS */

/*-------------------------- netdev event handling --------------------------*/

/*
 * Change device name
 */
static int bond_event_changename(struct bonding *bond)
{
#ifdef CONFIG_PROC_FS
	bond_remove_proc_entry(bond);
	bond_create_proc_entry(bond);
#endif
	down_write(&(bonding_rwsem));
        bond_destroy_sysfs_entry(bond);
        bond_create_sysfs_entry(bond);
	up_write(&(bonding_rwsem));
	return NOTIFY_DONE;
}

static int bond_master_netdev_event(unsigned long event, struct net_device *bond_dev)
{
	struct bonding *event_bond = bond_dev->priv;
		if (!bond_slave_is_up(slave) && slave->link == BOND_LINK_UP) {
			bond_set_slave_link_state(slave, BOND_LINK_DOWN);
			if (slave->link_failure_count < UINT_MAX)
				slave->link_failure_count++;

			bond_set_slave_inactive_flags(slave,
						      BOND_SLAVE_NOTIFY_LATER);

			netdev_info(bond->dev, "backup interface %s is now down\n",
				    slave->dev->name);
		}
		if (slave == curr_arp_slave)
			found = true;
	}

	if (!new_slave && before)
		new_slave = before;

	if (!new_slave)
		goto check_state;

	bond_set_slave_link_state(new_slave, BOND_LINK_BACK);
	bond_set_slave_active_flags(new_slave, BOND_SLAVE_NOTIFY_LATER);
	bond_arp_send_all(bond, new_slave);
	new_slave->last_link_up = jiffies;
	rcu_assign_pointer(bond->current_arp_slave, new_slave);

check_state:
	bond_for_each_slave_rcu(bond, slave, iter) {
		if (slave->should_notify) {
			should_notify_rtnl = BOND_SLAVE_NOTIFY_NOW;
			break;
		}
	}
	return should_notify_rtnl;
}

static void bond_activebackup_arp_mon(struct work_struct *work)
{
	struct bonding *bond = container_of(work, struct bonding,
					    arp_work.work);
	bool should_notify_peers = false;
	bool should_notify_rtnl = false;
	int delta_in_ticks;

	delta_in_ticks = msecs_to_jiffies(bond->params.arp_interval);

	if (!bond_has_slaves(bond))
		goto re_arm;

	rcu_read_lock();

	should_notify_peers = bond_should_notify_peers(bond);

	if (bond_ab_arp_inspect(bond)) {
		rcu_read_unlock();

		/* Race avoidance with bond_close flush of workqueue */
		if (!rtnl_trylock()) {
			delta_in_ticks = 1;
			should_notify_peers = false;
			goto re_arm;
		}

		bond_ab_arp_commit(bond);

		rtnl_unlock();
		rcu_read_lock();
	}

	should_notify_rtnl = bond_ab_arp_probe(bond);
	rcu_read_unlock();

re_arm:
	if (bond->params.arp_interval)
		queue_delayed_work(bond->wq, &bond->arp_work, delta_in_ticks);

	if (should_notify_peers || should_notify_rtnl) {
		if (!rtnl_trylock())
			return;

		if (should_notify_peers)
			call_netdevice_notifiers(NETDEV_NOTIFY_PEERS,
						 bond->dev);
		if (should_notify_rtnl)
			bond_slave_state_notify(bond);

		rtnl_unlock();
	}
}

/*-------------------------- netdev event handling --------------------------*/

/* Change device name */
static int bond_event_changename(struct bonding *bond)
{
	bond_remove_proc_entry(bond);
	bond_create_proc_entry(bond);

	bond_debug_reregister(bond);

	return NOTIFY_DONE;
}

static int bond_master_netdev_event(unsigned long event,
				    struct net_device *bond_dev)
{
	struct bonding *event_bond = netdev_priv(bond_dev);

	switch (event) {
	case NETDEV_CHANGENAME:
		return bond_event_changename(event_bond);
	case NETDEV_UNREGISTER:
		bond_release_all(event_bond->dev);
		bond_remove_proc_entry(event_bond);
		break;
	case NETDEV_REGISTER:
		bond_create_proc_entry(event_bond);
		break;
	case NETDEV_NOTIFY_PEERS:
		if (event_bond->send_peer_notif)
			event_bond->send_peer_notif--;
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}

static int bond_slave_netdev_event(unsigned long event, struct net_device *slave_dev)
{
	struct net_device *bond_dev = slave_dev->master;
	struct bonding *bond = bond_dev->priv;

	switch (event) {
	case NETDEV_UNREGISTER:
		if (bond_dev) {
			if (bond->setup_by_slave)
				bond_release_and_destroy(bond_dev, slave_dev);
			else
				bond_release(bond_dev, slave_dev);
		}
		break;
	case NETDEV_CHANGE:
		/*
		 * TODO: is this what we get if somebody
		 * sets up a hierarchical bond, then rmmod's
		 * one of the slave bonding devices?
		 */
		break;
	case NETDEV_DOWN:
		/*
		 * ... Or is it this?
		 */
		break;
	case NETDEV_CHANGEMTU:
		/*
		 * TODO: Should slaves be allowed to
static int bond_slave_netdev_event(unsigned long event,
				   struct net_device *slave_dev)
{
	struct slave *slave = bond_slave_get_rtnl(slave_dev), *primary;
	struct bonding *bond;
	struct net_device *bond_dev;

	/* A netdev event can be generated while enslaving a device
	 * before netdev_rx_handler_register is called in which case
	 * slave will be NULL
	 */
	if (!slave)
		return NOTIFY_DONE;
	bond_dev = slave->bond->dev;
	bond = slave->bond;
	primary = rtnl_dereference(bond->primary_slave);

	switch (event) {
	case NETDEV_UNREGISTER:
		if (bond_dev->type != ARPHRD_ETHER)
			bond_release_and_destroy(bond_dev, slave_dev);
		else
			bond_release(bond_dev, slave_dev);
		break;
	case NETDEV_UP:
	case NETDEV_CHANGE:
		bond_update_speed_duplex(slave);
		if (BOND_MODE(bond) == BOND_MODE_8023AD)
			bond_3ad_adapter_speed_duplex_changed(slave);
		/* Fallthrough */
	case NETDEV_DOWN:
		/* Refresh slave-array if applicable!
		 * If the setup does not use miimon or arpmon (mode-specific!),
		 * then these events will not cause the slave-array to be
		 * refreshed. This will cause xmit to use a slave that is not
		 * usable. Avoid such situation by refeshing the array at these
		 * events. If these (miimon/arpmon) parameters are configured
		 * then array gets refreshed twice and that should be fine!
		 */
		if (bond_mode_uses_xmit_hash(bond))
			bond_update_slave_arr(bond, NULL);
		break;
	case NETDEV_CHANGEMTU:
		/* TODO: Should slaves be allowed to
		 * independently alter their MTU?  For
		 * an active-backup bond, slaves need
		 * not be the same type of device, so
		 * MTUs may vary.  For other modes,
		 * slaves arguably should have the
		 * same MTUs. To do this, we'd need to
		 * take over the slave's change_mtu
		 * function for the duration of their
		 * servitude.
		 */
		break;
	case NETDEV_CHANGENAME:
		/*
		 * TODO: handle changing the primary's name
		 */
		/* we don't care if we don't have primary set */
		if (!bond_uses_primary(bond) ||
		    !bond->params.primary[0])
			break;

		if (slave == primary) {
			/* slave's name changed - he's no longer primary */
			RCU_INIT_POINTER(bond->primary_slave, NULL);
		} else if (!strcmp(slave_dev->name, bond->params.primary)) {
			/* we have a new primary slave */
			rcu_assign_pointer(bond->primary_slave, slave);
		} else { /* we didn't change primary - exit */
			break;
		}

		netdev_info(bond->dev, "Primary slave changed to %s, reselecting active slave\n",
			    primary ? slave_dev->name : "none");

		block_netpoll_tx();
		bond_select_active_slave(bond);
		unblock_netpoll_tx();
		break;
	case NETDEV_FEAT_CHANGE:
		bond_compute_features(bond);
		break;
	case NETDEV_RESEND_IGMP:
		/* Propagate to master device */
		call_netdevice_notifiers(event, slave->bond->dev);
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}

/*
 * bond_netdev_event: handle netdev notifier chain events.
/* bond_netdev_event: handle netdev notifier chain events.
 *
 * This function receives events for the netdev chain.  The caller (an
 * ioctl handler calling blocking_notifier_call_chain) holds the necessary
 * locks for us to safely manipulate the slave devices (RTNL lock,
 * dev_probe_lock).
 */
static int bond_netdev_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct net_device *event_dev = (struct net_device *)ptr;

	if (dev_net(event_dev) != &init_net)
		return NOTIFY_DONE;

	dprintk("event_dev: %s, event: %lx\n",
		(event_dev ? event_dev->name : "None"),
		event);
static int bond_netdev_event(struct notifier_block *this,
			     unsigned long event, void *ptr)
{
	struct net_device *event_dev = netdev_notifier_info_to_dev(ptr);

	netdev_dbg(event_dev, "event: %lx\n", event);

	if (!(event_dev->priv_flags & IFF_BONDING))
		return NOTIFY_DONE;

	if (event_dev->flags & IFF_MASTER) {
		dprintk("IFF_MASTER\n");
		int ret;

		netdev_dbg(event_dev, "IFF_MASTER\n");
		ret = bond_master_netdev_event(event, event_dev);
		if (ret != NOTIFY_DONE)
			return ret;
	}

	if (event_dev->flags & IFF_SLAVE) {
		dprintk("IFF_SLAVE\n");
		netdev_dbg(event_dev, "IFF_SLAVE\n");
		return bond_slave_netdev_event(event, event_dev);
	}

	return NOTIFY_DONE;
}

/*
 * bond_inetaddr_event: handle inetaddr notifier chain events.
 *
 * We keep track of device IPs primarily to use as source addresses in
 * ARP monitor probes (rather than spewing out broadcasts all the time).
 *
 * We track one IP for the main device (if it has one), plus one per VLAN.
 */
static int bond_inetaddr_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct in_ifaddr *ifa = ptr;
	struct net_device *vlan_dev, *event_dev = ifa->ifa_dev->dev;
	struct bonding *bond;
	struct vlan_entry *vlan;

	if (dev_net(ifa->ifa_dev->dev) != &init_net)
		return NOTIFY_DONE;

	list_for_each_entry(bond, &bond_dev_list, bond_list) {
		if (bond->dev == event_dev) {
			switch (event) {
			case NETDEV_UP:
				bond->master_ip = ifa->ifa_local;
				return NOTIFY_OK;
			case NETDEV_DOWN:
				bond->master_ip = bond_glean_dev_ip(bond->dev);
				return NOTIFY_OK;
			default:
				return NOTIFY_DONE;
			}
		}

		list_for_each_entry(vlan, &bond->vlan_list, vlan_list) {
			vlan_dev = vlan_group_get_device(bond->vlgrp, vlan->vlan_id);
			if (vlan_dev == event_dev) {
				switch (event) {
				case NETDEV_UP:
					vlan->vlan_ip = ifa->ifa_local;
					return NOTIFY_OK;
				case NETDEV_DOWN:
					vlan->vlan_ip =
						bond_glean_dev_ip(vlan_dev);
					return NOTIFY_OK;
				default:
					return NOTIFY_DONE;
				}
			}
		}
	}
	return NOTIFY_DONE;
}

static struct notifier_block bond_netdev_notifier = {
	.notifier_call = bond_netdev_event,
};

static struct notifier_block bond_inetaddr_notifier = {
	.notifier_call = bond_inetaddr_event,
};

/*-------------------------- Packet type handling ---------------------------*/

/* register to receive lacpdus on a bond */
static void bond_register_lacpdu(struct bonding *bond)
{
	struct packet_type *pk_type = &(BOND_AD_INFO(bond).ad_pkt_type);

	/* initialize packet type */
	pk_type->type = PKT_TYPE_LACPDU;
	pk_type->dev = bond->dev;
	pk_type->func = bond_3ad_lacpdu_recv;

	dev_add_pack(pk_type);
}

/* unregister to receive lacpdus on a bond */
static void bond_unregister_lacpdu(struct bonding *bond)
{
	dev_remove_pack(&(BOND_AD_INFO(bond).ad_pkt_type));
}

void bond_register_arp(struct bonding *bond)
{
	struct packet_type *pt = &bond->arp_mon_pt;

	if (pt->type)
		return;

	pt->type = htons(ETH_P_ARP);
	pt->dev = bond->dev;
	pt->func = bond_arp_rcv;
	dev_add_pack(pt);
}

void bond_unregister_arp(struct bonding *bond)
{
	struct packet_type *pt = &bond->arp_mon_pt;

	dev_remove_pack(pt);
	pt->type = 0;
}

/*---------------------------- Hashing Policies -----------------------------*/

/*
 * Hash for the output device based upon layer 2 and layer 3 data. If
 * the packet is not IP mimic bond_xmit_hash_policy_l2()
 */
static int bond_xmit_hash_policy_l23(struct sk_buff *skb,
				     struct net_device *bond_dev, int count)
{
	struct ethhdr *data = (struct ethhdr *)skb->data;
	struct iphdr *iph = ip_hdr(skb);

	if (skb->protocol == __constant_htons(ETH_P_IP)) {
		return ((ntohl(iph->saddr ^ iph->daddr) & 0xffff) ^
			(data->h_dest[5] ^ bond_dev->dev_addr[5])) % count;
	}

	return (data->h_dest[5] ^ bond_dev->dev_addr[5]) % count;
}

/*
 * Hash for the output device based upon layer 3 and layer 4 data. If
 * the packet is a frag or not TCP or UDP, just use layer 3 data.  If it is
 * altogether not IP, mimic bond_xmit_hash_policy_l2()
 */
static int bond_xmit_hash_policy_l34(struct sk_buff *skb,
				    struct net_device *bond_dev, int count)
{
	struct ethhdr *data = (struct ethhdr *)skb->data;
	struct iphdr *iph = ip_hdr(skb);
	__be16 *layer4hdr = (__be16 *)((u32 *)iph + iph->ihl);
	int layer4_xor = 0;

	if (skb->protocol == __constant_htons(ETH_P_IP)) {
		if (!(iph->frag_off & __constant_htons(IP_MF|IP_OFFSET)) &&
		    (iph->protocol == IPPROTO_TCP ||
		     iph->protocol == IPPROTO_UDP)) {
			layer4_xor = ntohs((*layer4hdr ^ *(layer4hdr + 1)));
		}
		return (layer4_xor ^
			((ntohl(iph->saddr ^ iph->daddr)) & 0xffff)) % count;

	}

	return (data->h_dest[5] ^ bond_dev->dev_addr[5]) % count;
}

/*
 * Hash for the output device based upon layer 2 data
 */
static int bond_xmit_hash_policy_l2(struct sk_buff *skb,
				   struct net_device *bond_dev, int count)
{
	struct ethhdr *data = (struct ethhdr *)skb->data;

	return (data->h_dest[5] ^ bond_dev->dev_addr[5]) % count;
/*---------------------------- Hashing Policies -----------------------------*/

/* L2 hash helper */
static inline u32 bond_eth_hash(struct sk_buff *skb)
{
	struct ethhdr *ep, hdr_tmp;

	ep = skb_header_pointer(skb, 0, sizeof(hdr_tmp), &hdr_tmp);
	if (ep)
		return ep->h_dest[5] ^ ep->h_source[5] ^ ep->h_proto;
	return 0;
}

/* Extract the appropriate headers based on bond's xmit policy */
static bool bond_flow_dissect(struct bonding *bond, struct sk_buff *skb,
			      struct flow_keys *fk)
{
	const struct ipv6hdr *iph6;
	const struct iphdr *iph;
	int noff, proto = -1;

	if (bond->params.xmit_policy > BOND_XMIT_POLICY_LAYER23)
		return skb_flow_dissect_flow_keys(skb, fk, 0);

	fk->ports.ports = 0;
	noff = skb_network_offset(skb);
	if (skb->protocol == htons(ETH_P_IP)) {
		if (unlikely(!pskb_may_pull(skb, noff + sizeof(*iph))))
			return false;
		iph = ip_hdr(skb);
		iph_to_flow_copy_v4addrs(fk, iph);
		noff += iph->ihl << 2;
		if (!ip_is_fragment(iph))
			proto = iph->protocol;
	} else if (skb->protocol == htons(ETH_P_IPV6)) {
		if (unlikely(!pskb_may_pull(skb, noff + sizeof(*iph6))))
			return false;
		iph6 = ipv6_hdr(skb);
		iph_to_flow_copy_v6addrs(fk, iph6);
		noff += sizeof(*iph6);
		proto = iph6->nexthdr;
	} else {
		return false;
	}
	if (bond->params.xmit_policy == BOND_XMIT_POLICY_LAYER34 && proto >= 0)
		fk->ports.ports = skb_flow_get_ports(skb, noff, proto);

	return true;
}

/**
 * bond_xmit_hash - generate a hash value based on the xmit policy
 * @bond: bonding device
 * @skb: buffer to use for headers
 *
 * This function will extract the necessary headers from the skb buffer and use
 * them to generate a hash based on the xmit_policy set in the bonding device
 */
u32 bond_xmit_hash(struct bonding *bond, struct sk_buff *skb)
{
	struct flow_keys flow;
	u32 hash;

	if (bond->params.xmit_policy == BOND_XMIT_POLICY_ENCAP34 &&
	    skb->l4_hash)
		return skb->hash;

	if (bond->params.xmit_policy == BOND_XMIT_POLICY_LAYER2 ||
	    !bond_flow_dissect(bond, skb, &flow))
		return bond_eth_hash(skb);

	if (bond->params.xmit_policy == BOND_XMIT_POLICY_LAYER23 ||
	    bond->params.xmit_policy == BOND_XMIT_POLICY_ENCAP23)
		hash = bond_eth_hash(skb);
	else
		hash = (__force u32)flow.ports.ports;
	hash ^= (__force u32)flow_get_u32_dst(&flow) ^
		(__force u32)flow_get_u32_src(&flow);
	hash ^= (hash >> 16);
	hash ^= (hash >> 8);

	return hash >> 1;
}

/*-------------------------- Device entry points ----------------------------*/

static int bond_open(struct net_device *bond_dev)
{
	struct bonding *bond = bond_dev->priv;

	bond->kill_timers = 0;

	if ((bond->params.mode == BOND_MODE_TLB) ||
	    (bond->params.mode == BOND_MODE_ALB)) {
		/* bond_alb_initialize must be called before the timer
		 * is started.
		 */
		if (bond_alb_initialize(bond, (bond->params.mode == BOND_MODE_ALB))) {
			/* something went wrong - fail the open operation */
			return -1;
		}

		INIT_DELAYED_WORK(&bond->alb_work, bond_alb_monitor);
		queue_delayed_work(bond->wq, &bond->alb_work, 0);
	}

	if (bond->params.miimon) {  /* link check interval, in milliseconds. */
		INIT_DELAYED_WORK(&bond->mii_work, bond_mii_monitor);
		queue_delayed_work(bond->wq, &bond->mii_work, 0);
	}

	if (bond->params.arp_interval) {  /* arp interval, in milliseconds. */
		if (bond->params.mode == BOND_MODE_ACTIVEBACKUP)
			INIT_DELAYED_WORK(&bond->arp_work,
					  bond_activebackup_arp_mon);
		else
			INIT_DELAYED_WORK(&bond->arp_work,
					  bond_loadbalance_arp_mon);

		queue_delayed_work(bond->wq, &bond->arp_work, 0);
		if (bond->params.arp_validate)
			bond_register_arp(bond);
	}

	if (bond->params.mode == BOND_MODE_8023AD) {
		INIT_DELAYED_WORK(&bond->ad_work, bond_3ad_state_machine_handler);
		queue_delayed_work(bond->wq, &bond->ad_work, 0);
		/* register to receive LACPDUs */
		bond_register_lacpdu(bond);
	}

static void bond_work_init_all(struct bonding *bond)
{
	INIT_DELAYED_WORK(&bond->mcast_work,
			  bond_resend_igmp_join_requests_delayed);
	INIT_DELAYED_WORK(&bond->alb_work, bond_alb_monitor);
	INIT_DELAYED_WORK(&bond->mii_work, bond_mii_monitor);
	if (BOND_MODE(bond) == BOND_MODE_ACTIVEBACKUP)
		INIT_DELAYED_WORK(&bond->arp_work, bond_activebackup_arp_mon);
	else
		INIT_DELAYED_WORK(&bond->arp_work, bond_loadbalance_arp_mon);
	INIT_DELAYED_WORK(&bond->ad_work, bond_3ad_state_machine_handler);
	INIT_DELAYED_WORK(&bond->slave_arr_work, bond_slave_arr_handler);
}

static void bond_work_cancel_all(struct bonding *bond)
{
	cancel_delayed_work_sync(&bond->mii_work);
	cancel_delayed_work_sync(&bond->arp_work);
	cancel_delayed_work_sync(&bond->alb_work);
	cancel_delayed_work_sync(&bond->ad_work);
	cancel_delayed_work_sync(&bond->mcast_work);
	cancel_delayed_work_sync(&bond->slave_arr_work);
}

static int bond_open(struct net_device *bond_dev)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct list_head *iter;
	struct slave *slave;

	/* reset slave->backup and slave->inactive */
	if (bond_has_slaves(bond)) {
		bond_for_each_slave(bond, slave, iter) {
			if (bond_uses_primary(bond) &&
			    slave != rcu_access_pointer(bond->curr_active_slave)) {
				bond_set_slave_inactive_flags(slave,
							      BOND_SLAVE_NOTIFY_NOW);
			} else if (BOND_MODE(bond) != BOND_MODE_8023AD) {
				bond_set_slave_active_flags(slave,
							    BOND_SLAVE_NOTIFY_NOW);
			}
		}
	}

	bond_work_init_all(bond);

	if (bond_is_lb(bond)) {
		/* bond_alb_initialize must be called before the timer
		 * is started.
		 */
		if (bond_alb_initialize(bond, (BOND_MODE(bond) == BOND_MODE_ALB)))
			return -ENOMEM;
		if (bond->params.tlb_dynamic_lb)
			queue_delayed_work(bond->wq, &bond->alb_work, 0);
	}

	if (bond->params.miimon)  /* link check interval, in milliseconds. */
		queue_delayed_work(bond->wq, &bond->mii_work, 0);

	if (bond->params.arp_interval) {  /* arp interval, in milliseconds. */
		queue_delayed_work(bond->wq, &bond->arp_work, 0);
		bond->recv_probe = bond_arp_rcv;
	}

	if (BOND_MODE(bond) == BOND_MODE_8023AD) {
		queue_delayed_work(bond->wq, &bond->ad_work, 0);
		/* register to receive LACPDUs */
		bond->recv_probe = bond_3ad_lacpdu_recv;
		bond_3ad_initiate_agg_selection(bond, 1);
	}

	if (bond_mode_uses_xmit_hash(bond))
		bond_update_slave_arr(bond, NULL);

	return 0;
}

static int bond_close(struct net_device *bond_dev)
{
	struct bonding *bond = bond_dev->priv;

	if (bond->params.mode == BOND_MODE_8023AD) {
		/* Unregister the receive of LACPDUs */
		bond_unregister_lacpdu(bond);
	}

	if (bond->params.arp_validate)
		bond_unregister_arp(bond);

	write_lock_bh(&bond->lock);

	bond->send_grat_arp = 0;

	/* signal timers not to re-arm */
	bond->kill_timers = 1;

	write_unlock_bh(&bond->lock);

	if (bond->params.miimon) {  /* link check interval, in milliseconds. */
		cancel_delayed_work(&bond->mii_work);
	}

	if (bond->params.arp_interval) {  /* arp interval, in milliseconds. */
		cancel_delayed_work(&bond->arp_work);
	}

	switch (bond->params.mode) {
	case BOND_MODE_8023AD:
		cancel_delayed_work(&bond->ad_work);
		break;
	case BOND_MODE_TLB:
	case BOND_MODE_ALB:
		cancel_delayed_work(&bond->alb_work);
		break;
	default:
		break;
	}


	if ((bond->params.mode == BOND_MODE_TLB) ||
	    (bond->params.mode == BOND_MODE_ALB)) {
		/* Must be called only after all
		 * slaves have been released
		 */
		bond_alb_deinitialize(bond);
	}
	struct bonding *bond = netdev_priv(bond_dev);

	bond_work_cancel_all(bond);
	bond->send_peer_notif = 0;
	if (bond_is_lb(bond))
		bond_alb_deinitialize(bond);
	bond->recv_probe = NULL;

	return 0;
}

static struct net_device_stats *bond_get_stats(struct net_device *bond_dev)
{
	struct bonding *bond = bond_dev->priv;
	struct net_device_stats *stats = &(bond->stats), *sstats;
	struct net_device_stats local_stats;
	struct slave *slave;
	int i;

	memset(&local_stats, 0, sizeof(struct net_device_stats));

	read_lock_bh(&bond->lock);

	bond_for_each_slave(bond, slave, i) {
		sstats = slave->dev->get_stats(slave->dev);
		local_stats.rx_packets += sstats->rx_packets;
		local_stats.rx_bytes += sstats->rx_bytes;
		local_stats.rx_errors += sstats->rx_errors;
		local_stats.rx_dropped += sstats->rx_dropped;

		local_stats.tx_packets += sstats->tx_packets;
		local_stats.tx_bytes += sstats->tx_bytes;
		local_stats.tx_errors += sstats->tx_errors;
		local_stats.tx_dropped += sstats->tx_dropped;

		local_stats.multicast += sstats->multicast;
		local_stats.collisions += sstats->collisions;

		local_stats.rx_length_errors += sstats->rx_length_errors;
		local_stats.rx_over_errors += sstats->rx_over_errors;
		local_stats.rx_crc_errors += sstats->rx_crc_errors;
		local_stats.rx_frame_errors += sstats->rx_frame_errors;
		local_stats.rx_fifo_errors += sstats->rx_fifo_errors;
		local_stats.rx_missed_errors += sstats->rx_missed_errors;

		local_stats.tx_aborted_errors += sstats->tx_aborted_errors;
		local_stats.tx_carrier_errors += sstats->tx_carrier_errors;
		local_stats.tx_fifo_errors += sstats->tx_fifo_errors;
		local_stats.tx_heartbeat_errors += sstats->tx_heartbeat_errors;
		local_stats.tx_window_errors += sstats->tx_window_errors;
	}

	memcpy(stats, &local_stats, sizeof(struct net_device_stats));

	read_unlock_bh(&bond->lock);
/* fold stats, assuming all rtnl_link_stats64 fields are u64, but
 * that some drivers can provide 32bit values only.
 */
static void bond_fold_stats(struct rtnl_link_stats64 *_res,
			    const struct rtnl_link_stats64 *_new,
			    const struct rtnl_link_stats64 *_old)
{
	const u64 *new = (const u64 *)_new;
	const u64 *old = (const u64 *)_old;
	u64 *res = (u64 *)_res;
	int i;

	for (i = 0; i < sizeof(*_res) / sizeof(u64); i++) {
		u64 nv = new[i];
		u64 ov = old[i];
		s64 delta = nv - ov;

		/* detects if this particular field is 32bit only */
		if (((nv | ov) >> 32) == 0)
			delta = (s64)(s32)((u32)nv - (u32)ov);

		/* filter anomalies, some drivers reset their stats
		 * at down/up events.
		 */
		if (delta > 0)
			res[i] += delta;
	}
}

static struct rtnl_link_stats64 *bond_get_stats(struct net_device *bond_dev,
						struct rtnl_link_stats64 *stats)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct rtnl_link_stats64 temp;
	struct list_head *iter;
	struct slave *slave;

	spin_lock(&bond->stats_lock);
	memcpy(stats, &bond->bond_stats, sizeof(*stats));

	rcu_read_lock();
	bond_for_each_slave_rcu(bond, slave, iter) {
		const struct rtnl_link_stats64 *new =
			dev_get_stats(slave->dev, &temp);

		bond_fold_stats(stats, new, &slave->slave_stats);

		/* save off the slave stats for the next run */
		memcpy(&slave->slave_stats, new, sizeof(*new));
	}
	rcu_read_unlock();

	memcpy(&bond->bond_stats, stats, sizeof(*stats));
	spin_unlock(&bond->stats_lock);

	return stats;
}

static int bond_do_ioctl(struct net_device *bond_dev, struct ifreq *ifr, int cmd)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct net_device *slave_dev = NULL;
	struct ifbond k_binfo;
	struct ifbond __user *u_binfo = NULL;
	struct ifslave k_sinfo;
	struct ifslave __user *u_sinfo = NULL;
	struct mii_ioctl_data *mii = NULL;
	int res = 0;

	dprintk("bond_ioctl: master=%s, cmd=%d\n",
		bond_dev->name, cmd);
	struct bond_opt_value newval;
	struct net *net;
	int res = 0;

	netdev_dbg(bond_dev, "bond_ioctl: cmd=%d\n", cmd);

	switch (cmd) {
	case SIOCGMIIPHY:
		mii = if_mii(ifr);
		if (!mii) {
			return -EINVAL;
		}
		mii->phy_id = 0;
		/* Fall Through */
	case SIOCGMIIREG:
		/*
		 * We do this again just in case we were called by SIOCGMIIREG
		 * instead of SIOCGMIIPHY.
		 */
		mii = if_mii(ifr);
		if (!mii) {
			return -EINVAL;
		}

		if (mii->reg_num == 1) {
			struct bonding *bond = bond_dev->priv;
			mii->val_out = 0;
			read_lock(&bond->lock);
			read_lock(&bond->curr_slave_lock);
			if (netif_carrier_ok(bond->dev)) {
				mii->val_out = BMSR_LSTATUS;
			}
			read_unlock(&bond->curr_slave_lock);
			read_unlock(&bond->lock);
		if (!mii)
			return -EINVAL;

		mii->phy_id = 0;
		/* Fall Through */
	case SIOCGMIIREG:
		/* We do this again just in case we were called by SIOCGMIIREG
		 * instead of SIOCGMIIPHY.
		 */
		mii = if_mii(ifr);
		if (!mii)
			return -EINVAL;

		if (mii->reg_num == 1) {
			mii->val_out = 0;
			if (netif_carrier_ok(bond->dev))
				mii->val_out = BMSR_LSTATUS;
		}

		return 0;
	case BOND_INFO_QUERY_OLD:
	case SIOCBONDINFOQUERY:
		u_binfo = (struct ifbond __user *)ifr->ifr_data;

		if (copy_from_user(&k_binfo, u_binfo, sizeof(ifbond))) {
			return -EFAULT;
		}

		res = bond_info_query(bond_dev, &k_binfo);
		if (res == 0) {
			if (copy_to_user(u_binfo, &k_binfo, sizeof(ifbond))) {
				return -EFAULT;
			}
		}
		if (copy_from_user(&k_binfo, u_binfo, sizeof(ifbond)))
			return -EFAULT;

		res = bond_info_query(bond_dev, &k_binfo);
		if (res == 0 &&
		    copy_to_user(u_binfo, &k_binfo, sizeof(ifbond)))
			return -EFAULT;

		return res;
	case BOND_SLAVE_INFO_QUERY_OLD:
	case SIOCBONDSLAVEINFOQUERY:
		u_sinfo = (struct ifslave __user *)ifr->ifr_data;

		if (copy_from_user(&k_sinfo, u_sinfo, sizeof(ifslave))) {
			return -EFAULT;
		}

		res = bond_slave_info_query(bond_dev, &k_sinfo);
		if (res == 0) {
			if (copy_to_user(u_sinfo, &k_sinfo, sizeof(ifslave))) {
				return -EFAULT;
			}
		}

		return res;
	default:
		/* Go on */
		break;
	}

	if (!capable(CAP_NET_ADMIN)) {
		return -EPERM;
	}

	down_write(&(bonding_rwsem));
	slave_dev = dev_get_by_name(&init_net, ifr->ifr_slave);

	dprintk("slave_dev=%p: \n", slave_dev);

	if (!slave_dev) {
		res = -ENODEV;
	} else {
		dprintk("slave_dev->name=%s: \n", slave_dev->name);
		switch (cmd) {
		case BOND_ENSLAVE_OLD:
		case SIOCBONDENSLAVE:
			res = bond_enslave(bond_dev, slave_dev);
			break;
		case BOND_RELEASE_OLD:
		case SIOCBONDRELEASE:
			res = bond_release(bond_dev, slave_dev);
			break;
		case BOND_SETHWADDR_OLD:
		case SIOCBONDSETHWADDR:
			res = bond_sethwaddr(bond_dev, slave_dev);
			break;
		case BOND_CHANGE_ACTIVE_OLD:
		case SIOCBONDCHANGEACTIVE:
			res = bond_ioctl_change_active(bond_dev, slave_dev);
			break;
		default:
			res = -EOPNOTSUPP;
		}

		dev_put(slave_dev);
	}

	up_write(&(bonding_rwsem));
	return res;
}

static void bond_set_multicast_list(struct net_device *bond_dev)
{
	struct bonding *bond = bond_dev->priv;
	struct dev_mc_list *dmi;

	/*
	 * Do promisc before checking multicast_mode
	 */
	if ((bond_dev->flags & IFF_PROMISC) && !(bond->flags & IFF_PROMISC)) {
		/*
		 * FIXME: Need to handle the error when one of the multi-slaves
		 * encounters error.
		 */
		bond_set_promiscuity(bond, 1);
	}

	if (!(bond_dev->flags & IFF_PROMISC) && (bond->flags & IFF_PROMISC)) {
		bond_set_promiscuity(bond, -1);
	}

	/* set allmulti flag to slaves */
	if ((bond_dev->flags & IFF_ALLMULTI) && !(bond->flags & IFF_ALLMULTI)) {
		/*
		 * FIXME: Need to handle the error when one of the multi-slaves
		 * encounters error.
		 */
		bond_set_allmulti(bond, 1);
	}

	if (!(bond_dev->flags & IFF_ALLMULTI) && (bond->flags & IFF_ALLMULTI)) {
		bond_set_allmulti(bond, -1);
	}

	read_lock(&bond->lock);

	bond->flags = bond_dev->flags;

	/* looking for addresses to add to slaves' mc list */
	for (dmi = bond_dev->mc_list; dmi; dmi = dmi->next) {
		if (!bond_mc_list_find_dmi(dmi, bond->mc_list)) {
			bond_mc_add(bond, dmi->dmi_addr, dmi->dmi_addrlen);
		}
	}

	/* looking for addresses to delete from slaves' list */
	for (dmi = bond->mc_list; dmi; dmi = dmi->next) {
		if (!bond_mc_list_find_dmi(dmi, bond_dev->mc_list)) {
			bond_mc_delete(bond, dmi->dmi_addr, dmi->dmi_addrlen);
		}
	}

	/* save master's multicast list */
	bond_mc_list_destroy(bond);
	bond_mc_list_copy(bond_dev->mc_list, bond, GFP_ATOMIC);

	read_unlock(&bond->lock);
}

/*
 * Change the MTU of all of a master's slaves to match the master
 */
static int bond_change_mtu(struct net_device *bond_dev, int new_mtu)
{
	struct bonding *bond = bond_dev->priv;
	struct slave *slave, *stop_at;
	int res = 0;
	int i;

	dprintk("bond=%p, name=%s, new_mtu=%d\n", bond,
		(bond_dev ? bond_dev->name : "None"), new_mtu);

	/* Can't hold bond->lock with bh disabled here since
	 * some base drivers panic. On the other hand we can't
	 * hold bond->lock without bh disabled because we'll
	 * deadlock. The only solution is to rely on the fact
	 * that we're under rtnl_lock here, and the slaves
	 * list won't change. This doesn't solve the problem
	 * of setting the slave's MTU while it is
	 * transmitting, but the assumption is that the base
	 * driver can handle that.
	 *
	 * TODO: figure out a way to safely iterate the slaves
	 * list, but without holding a lock around the actual
	 * call to the base driver.
	 */

	bond_for_each_slave(bond, slave, i) {
		dprintk("s %p s->p %p c_m %p\n", slave,
			slave->prev, slave->dev->change_mtu);
		if (copy_from_user(&k_sinfo, u_sinfo, sizeof(ifslave)))
			return -EFAULT;

		res = bond_slave_info_query(bond_dev, &k_sinfo);
		if (res == 0 &&
		    copy_to_user(u_sinfo, &k_sinfo, sizeof(ifslave)))
			return -EFAULT;

		return res;
	default:
		break;
	}

	net = dev_net(bond_dev);

	if (!ns_capable(net->user_ns, CAP_NET_ADMIN))
		return -EPERM;

	slave_dev = __dev_get_by_name(net, ifr->ifr_slave);

	netdev_dbg(bond_dev, "slave_dev=%p:\n", slave_dev);

	if (!slave_dev)
		return -ENODEV;

	netdev_dbg(bond_dev, "slave_dev->name=%s:\n", slave_dev->name);
	switch (cmd) {
	case BOND_ENSLAVE_OLD:
	case SIOCBONDENSLAVE:
		res = bond_enslave(bond_dev, slave_dev);
		break;
	case BOND_RELEASE_OLD:
	case SIOCBONDRELEASE:
		res = bond_release(bond_dev, slave_dev);
		break;
	case BOND_SETHWADDR_OLD:
	case SIOCBONDSETHWADDR:
		bond_set_dev_addr(bond_dev, slave_dev);
		res = 0;
		break;
	case BOND_CHANGE_ACTIVE_OLD:
	case SIOCBONDCHANGEACTIVE:
		bond_opt_initstr(&newval, slave_dev->name);
		res = __bond_opt_set(bond, BOND_OPT_ACTIVE_SLAVE, &newval);
		break;
	default:
		res = -EOPNOTSUPP;
	}

	return res;
}

static void bond_change_rx_flags(struct net_device *bond_dev, int change)
{
	struct bonding *bond = netdev_priv(bond_dev);

	if (change & IFF_PROMISC)
		bond_set_promiscuity(bond,
				     bond_dev->flags & IFF_PROMISC ? 1 : -1);

	if (change & IFF_ALLMULTI)
		bond_set_allmulti(bond,
				  bond_dev->flags & IFF_ALLMULTI ? 1 : -1);
}

static void bond_set_rx_mode(struct net_device *bond_dev)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct list_head *iter;
	struct slave *slave;

	rcu_read_lock();
	if (bond_uses_primary(bond)) {
		slave = rcu_dereference(bond->curr_active_slave);
		if (slave) {
			dev_uc_sync(slave->dev, bond_dev);
			dev_mc_sync(slave->dev, bond_dev);
		}
	} else {
		bond_for_each_slave_rcu(bond, slave, iter) {
			dev_uc_sync_multiple(slave->dev, bond_dev);
			dev_mc_sync_multiple(slave->dev, bond_dev);
		}
	}
	rcu_read_unlock();
}

static int bond_neigh_init(struct neighbour *n)
{
	struct bonding *bond = netdev_priv(n->dev);
	const struct net_device_ops *slave_ops;
	struct neigh_parms parms;
	struct slave *slave;
	int ret;

	slave = bond_first_slave(bond);
	if (!slave)
		return 0;
	slave_ops = slave->dev->netdev_ops;
	if (!slave_ops->ndo_neigh_setup)
		return 0;

	parms.neigh_setup = NULL;
	parms.neigh_cleanup = NULL;
	ret = slave_ops->ndo_neigh_setup(slave->dev, &parms);
	if (ret)
		return ret;

	/* Assign slave's neigh_cleanup to neighbour in case cleanup is called
	 * after the last slave has been detached.  Assumes that all slaves
	 * utilize the same neigh_cleanup (true at this writing as only user
	 * is ipoib).
	 */
	n->parms->neigh_cleanup = parms.neigh_cleanup;

	if (!parms.neigh_setup)
		return 0;

	return parms.neigh_setup(n);
}

/* The bonding ndo_neigh_setup is called at init time beofre any
 * slave exists. So we must declare proxy setup function which will
 * be used at run time to resolve the actual slave neigh param setup.
 *
 * It's also called by master devices (such as vlans) to setup their
 * underlying devices. In that case - do nothing, we're already set up from
 * our init.
 */
static int bond_neigh_setup(struct net_device *dev,
			    struct neigh_parms *parms)
{
	/* modify only our neigh_parms */
	if (parms->dev == dev)
		parms->neigh_setup = bond_neigh_init;

	return 0;
}

/* Change the MTU of all of a master's slaves to match the master */
static int bond_change_mtu(struct net_device *bond_dev, int new_mtu)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct slave *slave, *rollback_slave;
	struct list_head *iter;
	int res = 0;

	netdev_dbg(bond_dev, "bond=%p, new_mtu=%d\n", bond, new_mtu);

	bond_for_each_slave(bond, slave, iter) {
		netdev_dbg(bond_dev, "s %p c_m %p\n",
			   slave, slave->dev->netdev_ops->ndo_change_mtu);

		res = dev_set_mtu(slave->dev, new_mtu);

		if (res) {
			/* If we failed to set the slave's mtu to the new value
			 * we must abort the operation even in ACTIVE_BACKUP
			 * mode, because if we allow the backup slaves to have
			 * different mtu values than the active slave we'll
			 * need to change their mtu when doing a failover. That
			 * means changing their mtu from timer context, which
			 * is probably not a good idea.
			 */
			dprintk("err %d %s\n", res, slave->dev->name);
			netdev_dbg(bond_dev, "err %d %s\n", res,
				   slave->dev->name);
			goto unwind;
		}
	}

	bond_dev->mtu = new_mtu;

	return 0;

unwind:
	/* unwind from head to the slave that failed */
	stop_at = slave;
	bond_for_each_slave_from_to(bond, slave, i, bond->first_slave, stop_at) {
		int tmp_res;

		tmp_res = dev_set_mtu(slave->dev, bond_dev->mtu);
		if (tmp_res) {
			dprintk("unwind err %d dev %s\n", tmp_res,
				slave->dev->name);
	bond_for_each_slave(bond, rollback_slave, iter) {
		int tmp_res;

		if (rollback_slave == slave)
			break;

		tmp_res = dev_set_mtu(rollback_slave->dev, bond_dev->mtu);
		if (tmp_res) {
			netdev_dbg(bond_dev, "unwind err %d dev %s\n",
				   tmp_res, rollback_slave->dev->name);
		}
	}

	return res;
}

/*
 * Change HW address
/* Change HW address
 *
 * Note that many devices must be down to change the HW address, and
 * downing the master releases all slaves.  We can make bonds full of
 * bonding devices to test this, however.
 */
static int bond_set_mac_address(struct net_device *bond_dev, void *addr)
{
	struct bonding *bond = bond_dev->priv;
	struct sockaddr *sa = addr, tmp_sa;
	struct slave *slave, *stop_at;
	int res = 0;
	int i;

	dprintk("bond=%p, name=%s\n", bond, (bond_dev ? bond_dev->name : "None"));

	/*
	 * If fail_over_mac is set to active, do nothing and return
	 * success.  Returning an error causes ifenslave to fail.
	 */
	if (bond->params.fail_over_mac == BOND_FOM_ACTIVE)
		return 0;

	if (!is_valid_ether_addr(sa->sa_data)) {
		return -EADDRNOTAVAIL;
	}

	/* Can't hold bond->lock with bh disabled here since
	 * some base drivers panic. On the other hand we can't
	 * hold bond->lock without bh disabled because we'll
	 * deadlock. The only solution is to rely on the fact
	 * that we're under rtnl_lock here, and the slaves
	 * list won't change. This doesn't solve the problem
	 * of setting the slave's hw address while it is
	 * transmitting, but the assumption is that the base
	 * driver can handle that.
	 *
	 * TODO: figure out a way to safely iterate the slaves
	 * list, but without holding a lock around the actual
	 * call to the base driver.
	 */

	bond_for_each_slave(bond, slave, i) {
		dprintk("slave %p %s\n", slave, slave->dev->name);

		if (slave->dev->set_mac_address == NULL) {
			res = -EOPNOTSUPP;
			dprintk("EOPNOTSUPP %s\n", slave->dev->name);
			goto unwind;
		}

	struct bonding *bond = netdev_priv(bond_dev);
	struct slave *slave, *rollback_slave;
	struct sockaddr *sa = addr, tmp_sa;
	struct list_head *iter;
	int res = 0;

	if (BOND_MODE(bond) == BOND_MODE_ALB)
		return bond_alb_set_mac_address(bond_dev, addr);


	netdev_dbg(bond_dev, "bond=%p\n", bond);

	/* If fail_over_mac is enabled, do nothing and return success.
	 * Returning an error causes ifenslave to fail.
	 */
	if (bond->params.fail_over_mac &&
	    BOND_MODE(bond) == BOND_MODE_ACTIVEBACKUP)
		return 0;

	if (!is_valid_ether_addr(sa->sa_data))
		return -EADDRNOTAVAIL;

	bond_for_each_slave(bond, slave, iter) {
		netdev_dbg(bond_dev, "slave %p %s\n", slave, slave->dev->name);
		res = dev_set_mac_address(slave->dev, addr);
		if (res) {
			/* TODO: consider downing the slave
			 * and retry ?
			 * User should expect communications
			 * breakage anyway until ARP finish
			 * updating, so...
			 */
			dprintk("err %d %s\n", res, slave->dev->name);
			netdev_dbg(bond_dev, "err %d %s\n", res, slave->dev->name);
			goto unwind;
		}
	}

	/* success */
	memcpy(bond_dev->dev_addr, sa->sa_data, bond_dev->addr_len);
	return 0;

unwind:
	memcpy(tmp_sa.sa_data, bond_dev->dev_addr, bond_dev->addr_len);
	tmp_sa.sa_family = bond_dev->type;

	/* unwind from head to the slave that failed */
	stop_at = slave;
	bond_for_each_slave_from_to(bond, slave, i, bond->first_slave, stop_at) {
		int tmp_res;

		tmp_res = dev_set_mac_address(slave->dev, &tmp_sa);
		if (tmp_res) {
			dprintk("unwind err %d dev %s\n", tmp_res,
				slave->dev->name);
	bond_for_each_slave(bond, rollback_slave, iter) {
		int tmp_res;

		if (rollback_slave == slave)
			break;

		tmp_res = dev_set_mac_address(rollback_slave->dev, &tmp_sa);
		if (tmp_res) {
			netdev_dbg(bond_dev, "unwind err %d dev %s\n",
				   tmp_res, rollback_slave->dev->name);
		}
	}

	return res;
}

static int bond_xmit_roundrobin(struct sk_buff *skb, struct net_device *bond_dev)
{
	struct bonding *bond = bond_dev->priv;
	struct slave *slave, *start_at;
	int i, slave_no, res = 1;

	read_lock(&bond->lock);

	if (!BOND_IS_OK(bond)) {
		goto out;
	}

	/*
	 * Concurrent TX may collide on rr_tx_counter; we accept that
	 * as being rare enough not to justify using an atomic op here
	 */
	slave_no = bond->rr_tx_counter++ % bond->slave_cnt;

	bond_for_each_slave(bond, slave, i) {
		slave_no--;
		if (slave_no < 0) {
			break;
		}
	}

	start_at = slave;
	bond_for_each_slave_from(bond, slave, i, start_at) {
		if (IS_UP(slave->dev) &&
		    (slave->link == BOND_LINK_UP) &&
		    (slave->state == BOND_STATE_ACTIVE)) {
			res = bond_dev_queue_xmit(bond, skb, slave->dev);
			break;
		}
	}

out:
	if (res) {
		/* no suitable interface, frame not sent */
		dev_kfree_skb(skb);
	}
	read_unlock(&bond->lock);
	return 0;
}


/*
 * in active-backup mode, we know that bond->curr_active_slave is always valid if
/**
 * bond_xmit_slave_id - transmit skb through slave with slave_id
 * @bond: bonding device that is transmitting
 * @skb: buffer to transmit
 * @slave_id: slave id up to slave_cnt-1 through which to transmit
 *
 * This function tries to transmit through slave with slave_id but in case
 * it fails, it tries to find the first available slave for transmission.
 * The skb is consumed in all cases, thus the function is void.
 */
static void bond_xmit_slave_id(struct bonding *bond, struct sk_buff *skb, int slave_id)
{
	struct list_head *iter;
	struct slave *slave;
	int i = slave_id;

	/* Here we start from the slave with slave_id */
	bond_for_each_slave_rcu(bond, slave, iter) {
		if (--i < 0) {
			if (bond_slave_can_tx(slave)) {
				bond_dev_queue_xmit(bond, skb, slave->dev);
				return;
			}
		}
	}

	/* Here we start from the first slave up to slave_id */
	i = slave_id;
	bond_for_each_slave_rcu(bond, slave, iter) {
		if (--i < 0)
			break;
		if (bond_slave_can_tx(slave)) {
			bond_dev_queue_xmit(bond, skb, slave->dev);
			return;
		}
	}
	/* no slave that can tx has been found */
	bond_tx_drop(bond->dev, skb);
}

/**
 * bond_rr_gen_slave_id - generate slave id based on packets_per_slave
 * @bond: bonding device to use
 *
 * Based on the value of the bonding device's packets_per_slave parameter
 * this function generates a slave id, which is usually used as the next
 * slave to transmit through.
 */
static u32 bond_rr_gen_slave_id(struct bonding *bond)
{
	u32 slave_id;
	struct reciprocal_value reciprocal_packets_per_slave;
	int packets_per_slave = bond->params.packets_per_slave;

	switch (packets_per_slave) {
	case 0:
		slave_id = prandom_u32();
		break;
	case 1:
		slave_id = bond->rr_tx_counter;
		break;
	default:
		reciprocal_packets_per_slave =
			bond->params.reciprocal_packets_per_slave;
		slave_id = reciprocal_divide(bond->rr_tx_counter,
					     reciprocal_packets_per_slave);
		break;
	}
	bond->rr_tx_counter++;

	return slave_id;
}

static int bond_xmit_roundrobin(struct sk_buff *skb, struct net_device *bond_dev)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct slave *slave;
	int slave_cnt;
	u32 slave_id;

	/* Start with the curr_active_slave that joined the bond as the
	 * default for sending IGMP traffic.  For failover purposes one
	 * needs to maintain some consistency for the interface that will
	 * send the join/membership reports.  The curr_active_slave found
	 * will send all of this type of traffic.
	 */
	if (skb->protocol == htons(ETH_P_IP)) {
		int noff = skb_network_offset(skb);
		struct iphdr *iph;

		if (unlikely(!pskb_may_pull(skb, noff + sizeof(*iph))))
			goto non_igmp;

		iph = ip_hdr(skb);
		if (iph->protocol == IPPROTO_IGMP) {
			slave = rcu_dereference(bond->curr_active_slave);
			if (slave)
				bond_dev_queue_xmit(bond, skb, slave->dev);
			else
				bond_xmit_slave_id(bond, skb, 0);
			return NETDEV_TX_OK;
		}
	}

non_igmp:
	slave_cnt = ACCESS_ONCE(bond->slave_cnt);
	if (likely(slave_cnt)) {
		slave_id = bond_rr_gen_slave_id(bond);
		bond_xmit_slave_id(bond, skb, slave_id % slave_cnt);
	} else {
		bond_tx_drop(bond_dev, skb);
	}
	return NETDEV_TX_OK;
}

/* In active-backup mode, we know that bond->curr_active_slave is always valid if
 * the bond has a usable interface.
 */
static int bond_xmit_activebackup(struct sk_buff *skb, struct net_device *bond_dev)
{
	struct bonding *bond = bond_dev->priv;
	int res = 1;

	read_lock(&bond->lock);
	read_lock(&bond->curr_slave_lock);

	if (!BOND_IS_OK(bond)) {
		goto out;
	}

	if (!bond->curr_active_slave)
		goto out;

	res = bond_dev_queue_xmit(bond, skb, bond->curr_active_slave->dev);

out:
	if (res) {
		/* no suitable interface, frame not sent */
		dev_kfree_skb(skb);
	}
	read_unlock(&bond->curr_slave_lock);
	read_unlock(&bond->lock);
	return 0;
}

/*
 * In bond_xmit_xor() , we determine the output device by using a pre-
 * determined xmit_hash_policy(), If the selected device is not enabled,
 * find the next active slave.
 */
static int bond_xmit_xor(struct sk_buff *skb, struct net_device *bond_dev)
{
	struct bonding *bond = bond_dev->priv;
	struct slave *slave, *start_at;
	int slave_no;
	int i;
	int res = 1;

	read_lock(&bond->lock);

	if (!BOND_IS_OK(bond)) {
		goto out;
	}

	slave_no = bond->xmit_hash_policy(skb, bond_dev, bond->slave_cnt);

	bond_for_each_slave(bond, slave, i) {
		slave_no--;
		if (slave_no < 0) {
			break;
		}
	}

	start_at = slave;

	bond_for_each_slave_from(bond, slave, i, start_at) {
		if (IS_UP(slave->dev) &&
		    (slave->link == BOND_LINK_UP) &&
		    (slave->state == BOND_STATE_ACTIVE)) {
			res = bond_dev_queue_xmit(bond, skb, slave->dev);
			break;
		}
	}

out:
	if (res) {
		/* no suitable interface, frame not sent */
		dev_kfree_skb(skb);
	}
	read_unlock(&bond->lock);
	return 0;
}

/*
 * in broadcast mode, we send everything to all usable interfaces.
 */
static int bond_xmit_broadcast(struct sk_buff *skb, struct net_device *bond_dev)
{
	struct bonding *bond = bond_dev->priv;
	struct slave *slave, *start_at;
	struct net_device *tx_dev = NULL;
	int i;
	int res = 1;

	read_lock(&bond->lock);

	if (!BOND_IS_OK(bond)) {
		goto out;
	}

	read_lock(&bond->curr_slave_lock);
	start_at = bond->curr_active_slave;
	read_unlock(&bond->curr_slave_lock);

	if (!start_at) {
		goto out;
	}

	bond_for_each_slave_from(bond, slave, i, start_at) {
		if (IS_UP(slave->dev) &&
		    (slave->link == BOND_LINK_UP) &&
		    (slave->state == BOND_STATE_ACTIVE)) {
			if (tx_dev) {
				struct sk_buff *skb2 = skb_clone(skb, GFP_ATOMIC);
				if (!skb2) {
					printk(KERN_ERR DRV_NAME
					       ": %s: Error: bond_xmit_broadcast(): "
					       "skb_clone() failed\n",
					       bond_dev->name);
					continue;
				}

				res = bond_dev_queue_xmit(bond, skb2, tx_dev);
				if (res) {
					dev_kfree_skb(skb2);
					continue;
				}
			}
			tx_dev = slave->dev;
		}
	}

	if (tx_dev) {
		res = bond_dev_queue_xmit(bond, skb, tx_dev);
	}

out:
	if (res) {
		/* no suitable interface, frame not sent */
		dev_kfree_skb(skb);
	}
	/* frame sent to all suitable interfaces */
	read_unlock(&bond->lock);
	return 0;
	struct bonding *bond = netdev_priv(bond_dev);
	struct slave *slave;

	slave = rcu_dereference(bond->curr_active_slave);
	if (slave)
		bond_dev_queue_xmit(bond, skb, slave->dev);
	else
		bond_tx_drop(bond_dev, skb);

	return NETDEV_TX_OK;
}

/* Use this to update slave_array when (a) it's not appropriate to update
 * slave_array right away (note that update_slave_array() may sleep)
 * and / or (b) RTNL is not held.
 */
void bond_slave_arr_work_rearm(struct bonding *bond, unsigned long delay)
{
	queue_delayed_work(bond->wq, &bond->slave_arr_work, delay);
}

/* Slave array work handler. Holds only RTNL */
static void bond_slave_arr_handler(struct work_struct *work)
{
	struct bonding *bond = container_of(work, struct bonding,
					    slave_arr_work.work);
	int ret;

	if (!rtnl_trylock())
		goto err;

	ret = bond_update_slave_arr(bond, NULL);
	rtnl_unlock();
	if (ret) {
		pr_warn_ratelimited("Failed to update slave array from WT\n");
		goto err;
	}
	return;

err:
	bond_slave_arr_work_rearm(bond, 1);
}

/* Build the usable slaves array in control path for modes that use xmit-hash
 * to determine the slave interface -
 * (a) BOND_MODE_8023AD
 * (b) BOND_MODE_XOR
 * (c) BOND_MODE_TLB && tlb_dynamic_lb == 0
 *
 * The caller is expected to hold RTNL only and NO other lock!
 */
int bond_update_slave_arr(struct bonding *bond, struct slave *skipslave)
{
	struct slave *slave;
	struct list_head *iter;
	struct bond_up_slave *new_arr, *old_arr;
	int agg_id = 0;
	int ret = 0;

#ifdef CONFIG_LOCKDEP
	WARN_ON(lockdep_is_held(&bond->mode_lock));
#endif

	new_arr = kzalloc(offsetof(struct bond_up_slave, arr[bond->slave_cnt]),
			  GFP_KERNEL);
	if (!new_arr) {
		ret = -ENOMEM;
		pr_err("Failed to build slave-array.\n");
		goto out;
	}
	if (BOND_MODE(bond) == BOND_MODE_8023AD) {
		struct ad_info ad_info;

		if (bond_3ad_get_active_agg_info(bond, &ad_info)) {
			pr_debug("bond_3ad_get_active_agg_info failed\n");
			kfree_rcu(new_arr, rcu);
			/* No active aggragator means it's not safe to use
			 * the previous array.
			 */
			old_arr = rtnl_dereference(bond->slave_arr);
			if (old_arr) {
				RCU_INIT_POINTER(bond->slave_arr, NULL);
				kfree_rcu(old_arr, rcu);
			}
			goto out;
		}
		agg_id = ad_info.aggregator_id;
	}
	bond_for_each_slave(bond, slave, iter) {
		if (BOND_MODE(bond) == BOND_MODE_8023AD) {
			struct aggregator *agg;

			agg = SLAVE_AD_INFO(slave)->port.aggregator;
			if (!agg || agg->aggregator_identifier != agg_id)
				continue;
		}
		if (!bond_slave_can_tx(slave))
			continue;
		if (skipslave == slave)
			continue;
		new_arr->arr[new_arr->count++] = slave;
	}

	old_arr = rtnl_dereference(bond->slave_arr);
	rcu_assign_pointer(bond->slave_arr, new_arr);
	if (old_arr)
		kfree_rcu(old_arr, rcu);
out:
	if (ret != 0 && skipslave) {
		int idx;

		/* Rare situation where caller has asked to skip a specific
		 * slave but allocation failed (most likely!). BTW this is
		 * only possible when the call is initiated from
		 * __bond_release_one(). In this situation; overwrite the
		 * skipslave entry in the array with the last entry from the
		 * array to avoid a situation where the xmit path may choose
		 * this to-be-skipped slave to send a packet out.
		 */
		old_arr = rtnl_dereference(bond->slave_arr);
		for (idx = 0; old_arr != NULL && idx < old_arr->count; idx++) {
			if (skipslave == old_arr->arr[idx]) {
				old_arr->arr[idx] =
				    old_arr->arr[old_arr->count-1];
				old_arr->count--;
				break;
			}
		}
	}
	return ret;
}

/* Use this Xmit function for 3AD as well as XOR modes. The current
 * usable slave array is formed in the control path. The xmit function
 * just calculates hash and sends the packet out.
 */
static int bond_3ad_xor_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct bonding *bond = netdev_priv(dev);
	struct slave *slave;
	struct bond_up_slave *slaves;
	unsigned int count;

	slaves = rcu_dereference(bond->slave_arr);
	count = slaves ? ACCESS_ONCE(slaves->count) : 0;
	if (likely(count)) {
		slave = slaves->arr[bond_xmit_hash(bond, skb) % count];
		bond_dev_queue_xmit(bond, skb, slave->dev);
	} else {
		bond_tx_drop(dev, skb);
	}

	return NETDEV_TX_OK;
}

/* in broadcast mode, we send everything to all usable interfaces. */
static int bond_xmit_broadcast(struct sk_buff *skb, struct net_device *bond_dev)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct slave *slave = NULL;
	struct list_head *iter;

	bond_for_each_slave_rcu(bond, slave, iter) {
		if (bond_is_last_slave(bond, slave))
			break;
		if (bond_slave_is_up(slave) && slave->link == BOND_LINK_UP) {
			struct sk_buff *skb2 = skb_clone(skb, GFP_ATOMIC);

			if (!skb2) {
				net_err_ratelimited("%s: Error: %s: skb_clone() failed\n",
						    bond_dev->name, __func__);
				continue;
			}
			bond_dev_queue_xmit(bond, skb2, slave->dev);
		}
	}
	if (slave && bond_slave_is_up(slave) && slave->link == BOND_LINK_UP)
		bond_dev_queue_xmit(bond, skb, slave->dev);
	else
		bond_tx_drop(bond_dev, skb);

	return NETDEV_TX_OK;
}

/*------------------------- Device initialization ---------------------------*/

static void bond_set_xmit_hash_policy(struct bonding *bond)
{
	switch (bond->params.xmit_policy) {
	case BOND_XMIT_POLICY_LAYER23:
		bond->xmit_hash_policy = bond_xmit_hash_policy_l23;
		break;
	case BOND_XMIT_POLICY_LAYER34:
		bond->xmit_hash_policy = bond_xmit_hash_policy_l34;
		break;
	case BOND_XMIT_POLICY_LAYER2:
	default:
		bond->xmit_hash_policy = bond_xmit_hash_policy_l2;
		break;
	}
}

/*
 * set bond mode specific net device operations
 */
void bond_set_mode_ops(struct bonding *bond, int mode)
{
	struct net_device *bond_dev = bond->dev;

	switch (mode) {
	case BOND_MODE_ROUNDROBIN:
		bond_dev->hard_start_xmit = bond_xmit_roundrobin;
		break;
	case BOND_MODE_ACTIVEBACKUP:
		bond_dev->hard_start_xmit = bond_xmit_activebackup;
		break;
	case BOND_MODE_XOR:
		bond_dev->hard_start_xmit = bond_xmit_xor;
		bond_set_xmit_hash_policy(bond);
		break;
	case BOND_MODE_BROADCAST:
		bond_dev->hard_start_xmit = bond_xmit_broadcast;
		break;
	case BOND_MODE_8023AD:
		bond_set_master_3ad_flags(bond);
		bond_dev->hard_start_xmit = bond_3ad_xmit_xor;
		bond_set_xmit_hash_policy(bond);
		break;
	case BOND_MODE_ALB:
		bond_set_master_alb_flags(bond);
		/* FALLTHRU */
	case BOND_MODE_TLB:
		bond_dev->hard_start_xmit = bond_alb_xmit;
		bond_dev->set_mac_address = bond_alb_set_mac_address;
		break;
	default:
		/* Should never happen, mode already checked */
		printk(KERN_ERR DRV_NAME
		       ": %s: Error: Unknown bonding mode %d\n",
		       bond_dev->name,
		       mode);
		break;
	}
}

static void bond_ethtool_get_drvinfo(struct net_device *bond_dev,
				    struct ethtool_drvinfo *drvinfo)
{
	strncpy(drvinfo->driver, DRV_NAME, 32);
	strncpy(drvinfo->version, DRV_VERSION, 32);
	snprintf(drvinfo->fw_version, 32, "%d", BOND_ABI_VERSION);
/* Lookup the slave that corresponds to a qid */
static inline int bond_slave_override(struct bonding *bond,
				      struct sk_buff *skb)
{
	struct slave *slave = NULL;
	struct list_head *iter;

	if (!skb->queue_mapping)
		return 1;

	/* Find out if any slaves have the same mapping as this skb. */
	bond_for_each_slave_rcu(bond, slave, iter) {
		if (slave->queue_id == skb->queue_mapping) {
			if (bond_slave_is_up(slave) &&
			    slave->link == BOND_LINK_UP) {
				bond_dev_queue_xmit(bond, skb, slave->dev);
				return 0;
			}
			/* If the slave isn't UP, use default transmit policy. */
			break;
		}
	}

	return 1;
}


static u16 bond_select_queue(struct net_device *dev, struct sk_buff *skb,
			     void *accel_priv, select_queue_fallback_t fallback)
{
	/* This helper function exists to help dev_pick_tx get the correct
	 * destination queue.  Using a helper function skips a call to
	 * skb_tx_hash and will put the skbs in the queue we expect on their
	 * way down to the bonding driver.
	 */
	u16 txq = skb_rx_queue_recorded(skb) ? skb_get_rx_queue(skb) : 0;

	/* Save the original txq to restore before passing to the driver */
	qdisc_skb_cb(skb)->slave_dev_queue_mapping = skb->queue_mapping;

	if (unlikely(txq >= dev->real_num_tx_queues)) {
		do {
			txq -= dev->real_num_tx_queues;
		} while (txq >= dev->real_num_tx_queues);
	}
	return txq;
}

static netdev_tx_t __bond_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct bonding *bond = netdev_priv(dev);

	if (bond_should_override_tx_queue(bond) &&
	    !bond_slave_override(bond, skb))
		return NETDEV_TX_OK;

	switch (BOND_MODE(bond)) {
	case BOND_MODE_ROUNDROBIN:
		return bond_xmit_roundrobin(skb, dev);
	case BOND_MODE_ACTIVEBACKUP:
		return bond_xmit_activebackup(skb, dev);
	case BOND_MODE_8023AD:
	case BOND_MODE_XOR:
		return bond_3ad_xor_xmit(skb, dev);
	case BOND_MODE_BROADCAST:
		return bond_xmit_broadcast(skb, dev);
	case BOND_MODE_ALB:
		return bond_alb_xmit(skb, dev);
	case BOND_MODE_TLB:
		return bond_tlb_xmit(skb, dev);
	default:
		/* Should never happen, mode already checked */
		netdev_err(dev, "Unknown bonding mode %d\n", BOND_MODE(bond));
		WARN_ON_ONCE(1);
		bond_tx_drop(dev, skb);
		return NETDEV_TX_OK;
	}
}

static netdev_tx_t bond_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct bonding *bond = netdev_priv(dev);
	netdev_tx_t ret = NETDEV_TX_OK;

	/* If we risk deadlock from transmitting this in the
	 * netpoll path, tell netpoll to queue the frame for later tx
	 */
	if (unlikely(is_netpoll_tx_blocked(dev)))
		return NETDEV_TX_BUSY;

	rcu_read_lock();
	if (bond_has_slaves(bond))
		ret = __bond_start_xmit(skb, dev);
	else
		bond_tx_drop(dev, skb);
	rcu_read_unlock();

	return ret;
}

static u32 bond_mode_bcast_speed(struct slave *slave, u32 speed)
{
	if (speed == 0 || speed == SPEED_UNKNOWN)
		speed = slave->speed;
	else
		speed = min(speed, slave->speed);

	return speed;
}

static int bond_ethtool_get_settings(struct net_device *bond_dev,
				     struct ethtool_cmd *ecmd)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct list_head *iter;
	struct slave *slave;
	u32 speed = 0;

	ecmd->duplex = DUPLEX_UNKNOWN;
	ecmd->port = PORT_OTHER;

	/* Since bond_slave_can_tx returns false for all inactive or down slaves, we
	 * do not need to check mode.  Though link speed might not represent
	 * the true receive or transmit bandwidth (not all modes are symmetric)
	 * this is an accurate maximum.
	 */
	bond_for_each_slave(bond, slave, iter) {
		if (bond_slave_can_tx(slave)) {
			if (slave->speed != SPEED_UNKNOWN) {
				if (BOND_MODE(bond) == BOND_MODE_BROADCAST)
					speed = bond_mode_bcast_speed(slave,
								      speed);
				else
					speed += slave->speed;
			}
			if (ecmd->duplex == DUPLEX_UNKNOWN &&
			    slave->duplex != DUPLEX_UNKNOWN)
				ecmd->duplex = slave->duplex;
		}
	}
	ethtool_cmd_speed_set(ecmd, speed ? : SPEED_UNKNOWN);

	return 0;
}

static void bond_ethtool_get_drvinfo(struct net_device *bond_dev,
				     struct ethtool_drvinfo *drvinfo)
{
	strlcpy(drvinfo->driver, DRV_NAME, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, DRV_VERSION, sizeof(drvinfo->version));
	snprintf(drvinfo->fw_version, sizeof(drvinfo->fw_version), "%d",
		 BOND_ABI_VERSION);
}

static const struct ethtool_ops bond_ethtool_ops = {
	.get_drvinfo		= bond_ethtool_get_drvinfo,
};

/*
 * Does not allocate but creates a /proc entry.
 * Allowed to fail.
 */
static int bond_init(struct net_device *bond_dev, struct bond_params *params)
{
	struct bonding *bond = bond_dev->priv;

	dprintk("Begin bond_init for %s\n", bond_dev->name);

	/* initialize rwlocks */
	rwlock_init(&bond->lock);
	rwlock_init(&bond->curr_slave_lock);

	bond->params = *params; /* copy params struct */

	bond->wq = create_singlethread_workqueue(bond_dev->name);
	if (!bond->wq)
		return -ENOMEM;

	/* Initialize pointers */
	bond->first_slave = NULL;
	bond->curr_active_slave = NULL;
	bond->current_arp_slave = NULL;
	bond->primary_slave = NULL;
	bond->dev = bond_dev;
	bond->send_grat_arp = 0;
	bond->setup_by_slave = 0;
	INIT_LIST_HEAD(&bond->vlan_list);

	/* Initialize the device entry points */
	bond_dev->open = bond_open;
	bond_dev->stop = bond_close;
	bond_dev->get_stats = bond_get_stats;
	bond_dev->do_ioctl = bond_do_ioctl;
	bond_dev->ethtool_ops = &bond_ethtool_ops;
	bond_dev->set_multicast_list = bond_set_multicast_list;
	bond_dev->change_mtu = bond_change_mtu;
	bond_dev->set_mac_address = bond_set_mac_address;
	bond_dev->validate_addr = NULL;

	bond_set_mode_ops(bond, bond->params.mode);

	bond_dev->destructor = free_netdev;

	/* Initialize the device options */
	bond_dev->tx_queue_len = 0;
	bond_dev->flags |= IFF_MASTER|IFF_MULTICAST;
	bond_dev->priv_flags |= IFF_BONDING;

	/* At first, we block adding VLANs. That's the only way to
	 * prevent problems that occur when adding VLANs over an
	 * empty bond. The block will be removed once non-challenged
	 * slaves are enslaved.
	 */
	bond_dev->features |= NETIF_F_VLAN_CHALLENGED;

	/* don't acquire bond device's netif_tx_lock when
	 * transmitting */
	.get_settings		= bond_ethtool_get_settings,
	.get_link		= ethtool_op_get_link,
};

static const struct net_device_ops bond_netdev_ops = {
	.ndo_init		= bond_init,
	.ndo_uninit		= bond_uninit,
	.ndo_open		= bond_open,
	.ndo_stop		= bond_close,
	.ndo_start_xmit		= bond_start_xmit,
	.ndo_select_queue	= bond_select_queue,
	.ndo_get_stats64	= bond_get_stats,
	.ndo_do_ioctl		= bond_do_ioctl,
	.ndo_change_rx_flags	= bond_change_rx_flags,
	.ndo_set_rx_mode	= bond_set_rx_mode,
	.ndo_change_mtu		= bond_change_mtu,
	.ndo_set_mac_address	= bond_set_mac_address,
	.ndo_neigh_setup	= bond_neigh_setup,
	.ndo_vlan_rx_add_vid	= bond_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= bond_vlan_rx_kill_vid,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_netpoll_setup	= bond_netpoll_setup,
	.ndo_netpoll_cleanup	= bond_netpoll_cleanup,
	.ndo_poll_controller	= bond_poll_controller,
#endif
	.ndo_add_slave		= bond_enslave,
	.ndo_del_slave		= bond_release,
	.ndo_fix_features	= bond_fix_features,
	.ndo_bridge_setlink	= switchdev_port_bridge_setlink,
	.ndo_bridge_getlink	= switchdev_port_bridge_getlink,
	.ndo_bridge_dellink	= switchdev_port_bridge_dellink,
	.ndo_fdb_add		= switchdev_port_fdb_add,
	.ndo_fdb_del		= switchdev_port_fdb_del,
	.ndo_fdb_dump		= switchdev_port_fdb_dump,
	.ndo_features_check	= passthru_features_check,
};

static const struct device_type bond_type = {
	.name = "bond",
};

static void bond_destructor(struct net_device *bond_dev)
{
	struct bonding *bond = netdev_priv(bond_dev);
	if (bond->wq)
		destroy_workqueue(bond->wq);
	free_netdev(bond_dev);
}

void bond_setup(struct net_device *bond_dev)
{
	struct bonding *bond = netdev_priv(bond_dev);

	spin_lock_init(&bond->mode_lock);
	spin_lock_init(&bond->stats_lock);
	bond->params = bonding_defaults;

	/* Initialize pointers */
	bond->dev = bond_dev;

	/* Initialize the device entry points */
	ether_setup(bond_dev);
	bond_dev->netdev_ops = &bond_netdev_ops;
	bond_dev->ethtool_ops = &bond_ethtool_ops;

	bond_dev->destructor = bond_destructor;

	SET_NETDEV_DEVTYPE(bond_dev, &bond_type);

	/* Initialize the device options */
	bond_dev->flags |= IFF_MASTER|IFF_MULTICAST;
	bond_dev->priv_flags |= IFF_BONDING | IFF_UNICAST_FLT | IFF_NO_QUEUE;
	bond_dev->priv_flags &= ~(IFF_XMIT_DST_RELEASE | IFF_TX_SKB_SHARING);

	/* don't acquire bond device's netif_tx_lock when transmitting */
	bond_dev->features |= NETIF_F_LLTX;

	/* By default, we declare the bond to be fully
	 * VLAN hardware accelerated capable. Special
	 * care is taken in the various xmit functions
	 * when there are slaves that are not hw accel
	 * capable
	 */
	bond_dev->vlan_rx_register = bond_vlan_rx_register;
	bond_dev->vlan_rx_add_vid  = bond_vlan_rx_add_vid;
	bond_dev->vlan_rx_kill_vid = bond_vlan_rx_kill_vid;
	bond_dev->features |= (NETIF_F_HW_VLAN_TX |
			       NETIF_F_HW_VLAN_RX |
			       NETIF_F_HW_VLAN_FILTER);

#ifdef CONFIG_PROC_FS
	bond_create_proc_entry(bond);
#endif
	list_add_tail(&bond->bond_list, &bond_dev_list);

	return 0;
}

/* De-initialize device specific data.
 * Caller must hold rtnl_lock.
 */
static void bond_deinit(struct net_device *bond_dev)
{
	struct bonding *bond = bond_dev->priv;

	list_del(&bond->bond_list);

#ifdef CONFIG_PROC_FS
	bond_remove_proc_entry(bond);
#endif
}

static void bond_work_cancel_all(struct bonding *bond)
{
	write_lock_bh(&bond->lock);
	bond->kill_timers = 1;
	write_unlock_bh(&bond->lock);

	if (bond->params.miimon && delayed_work_pending(&bond->mii_work))
		cancel_delayed_work(&bond->mii_work);

	if (bond->params.arp_interval && delayed_work_pending(&bond->arp_work))
		cancel_delayed_work(&bond->arp_work);

	if (bond->params.mode == BOND_MODE_ALB &&
	    delayed_work_pending(&bond->alb_work))
		cancel_delayed_work(&bond->alb_work);

	if (bond->params.mode == BOND_MODE_8023AD &&
	    delayed_work_pending(&bond->ad_work))
		cancel_delayed_work(&bond->ad_work);
}

/* Unregister and free all bond devices.
 * Caller must hold rtnl_lock.
 */
static void bond_free_all(void)
{
	struct bonding *bond, *nxt;

	list_for_each_entry_safe(bond, nxt, &bond_dev_list, bond_list) {
		struct net_device *bond_dev = bond->dev;

		bond_work_cancel_all(bond);
		netif_addr_lock_bh(bond_dev);
		bond_mc_list_destroy(bond);
		netif_addr_unlock_bh(bond_dev);
		/* Release the bonded slaves */
		bond_release_all(bond_dev);
		bond_destroy(bond);
	}

#ifdef CONFIG_PROC_FS
	bond_destroy_proc_dir();
#endif

	/* Don't allow bond devices to change network namespaces. */
	bond_dev->features |= NETIF_F_NETNS_LOCAL;

	bond_dev->hw_features = BOND_VLAN_FEATURES |
				NETIF_F_HW_VLAN_CTAG_RX |
				NETIF_F_HW_VLAN_CTAG_FILTER |
				NETIF_F_HW_VLAN_STAG_RX |
				NETIF_F_HW_VLAN_STAG_FILTER;

	bond_dev->hw_features &= ~(NETIF_F_ALL_CSUM & ~NETIF_F_HW_CSUM);
	bond_dev->hw_features |= NETIF_F_GSO_ENCAP_ALL;
	bond_dev->features |= bond_dev->hw_features;
	bond_dev->features |= NETIF_F_HW_VLAN_CTAG_TX;
}

/* Destroy a bonding device.
 * Must be under rtnl_lock when this function is called.
 */
static void bond_uninit(struct net_device *bond_dev)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct list_head *iter;
	struct slave *slave;
	struct bond_up_slave *arr;

	bond_netpoll_cleanup(bond_dev);

	/* Release the bonded slaves */
	bond_for_each_slave(bond, slave, iter)
		__bond_release_one(bond_dev, slave->dev, true);
	netdev_info(bond_dev, "Released all slaves\n");

	arr = rtnl_dereference(bond->slave_arr);
	if (arr) {
		RCU_INIT_POINTER(bond->slave_arr, NULL);
		kfree_rcu(arr, rcu);
	}

	list_del(&bond->bond_list);

	bond_debug_unregister(bond);
}

/*------------------------- Module initialization ---------------------------*/

/*
 * Convert string input module parms.  Accept either the
 * number of the mode or its string name.  A bit complicated because
 * some mode names are substrings of other names, and calls from sysfs
 * may have whitespace in the name (trailing newlines, for example).
 */
int bond_parse_parm(const char *buf, struct bond_parm_tbl *tbl)
{
	int mode = -1, i, rv;
	char *p, modestr[BOND_MAX_MODENAME_LEN + 1] = { 0, };

	for (p = (char *)buf; *p; p++)
		if (!(isdigit(*p) || isspace(*p)))
			break;

	if (*p)
		rv = sscanf(buf, "%20s", modestr);
	else
		rv = sscanf(buf, "%d", &mode);

	if (!rv)
		return -1;

	for (i = 0; tbl[i].modename; i++) {
		if (mode == tbl[i].mode)
			return tbl[i].mode;
		if (strcmp(modestr, tbl[i].modename) == 0)
			return tbl[i].mode;
	}

	return -1;
}

static int bond_check_params(struct bond_params *params)
{
	int arp_validate_value, fail_over_mac_value;

	/*
	 * Convert string parameters.
	 */
	if (mode) {
		bond_mode = bond_parse_parm(mode, bond_mode_tbl);
		if (bond_mode == -1) {
			printk(KERN_ERR DRV_NAME
			       ": Error: Invalid bonding mode \"%s\"\n",
			       mode == NULL ? "NULL" : mode);
			return -EINVAL;
		}
static int bond_check_params(struct bond_params *params)
{
	int arp_validate_value, fail_over_mac_value, primary_reselect_value, i;
	struct bond_opt_value newval;
	const struct bond_opt_value *valptr;
	int arp_all_targets_value;
	u16 ad_actor_sys_prio = 0;
	u16 ad_user_port_key = 0;

	/* Convert string parameters. */
	if (mode) {
		bond_opt_initstr(&newval, mode);
		valptr = bond_opt_parse(bond_opt_get(BOND_OPT_MODE), &newval);
		if (!valptr) {
			pr_err("Error: Invalid bonding mode \"%s\"\n", mode);
			return -EINVAL;
		}
		bond_mode = valptr->value;
	}

	if (xmit_hash_policy) {
		if ((bond_mode != BOND_MODE_XOR) &&
		    (bond_mode != BOND_MODE_8023AD)) {
			printk(KERN_INFO DRV_NAME
			       ": xor_mode param is irrelevant in mode %s\n",
			       bond_mode_name(bond_mode));
		} else {
			xmit_hashtype = bond_parse_parm(xmit_hash_policy,
							xmit_hashtype_tbl);
			if (xmit_hashtype == -1) {
				printk(KERN_ERR DRV_NAME
			       	": Error: Invalid xmit_hash_policy \"%s\"\n",
			       	xmit_hash_policy == NULL ? "NULL" :
				       xmit_hash_policy);
				return -EINVAL;
			}
		    (bond_mode != BOND_MODE_8023AD) &&
		    (bond_mode != BOND_MODE_TLB)) {
			pr_info("xmit_hash_policy param is irrelevant in mode %s\n",
				bond_mode_name(bond_mode));
		} else {
			bond_opt_initstr(&newval, xmit_hash_policy);
			valptr = bond_opt_parse(bond_opt_get(BOND_OPT_XMIT_HASH),
						&newval);
			if (!valptr) {
				pr_err("Error: Invalid xmit_hash_policy \"%s\"\n",
				       xmit_hash_policy);
				return -EINVAL;
			}
			xmit_hashtype = valptr->value;
		}
	}

	if (lacp_rate) {
		if (bond_mode != BOND_MODE_8023AD) {
			printk(KERN_INFO DRV_NAME
			       ": lacp_rate param is irrelevant in mode %s\n",
			       bond_mode_name(bond_mode));
		} else {
			lacp_fast = bond_parse_parm(lacp_rate, bond_lacp_tbl);
			if (lacp_fast == -1) {
				printk(KERN_ERR DRV_NAME
				       ": Error: Invalid lacp rate \"%s\"\n",
				       lacp_rate == NULL ? "NULL" : lacp_rate);
				return -EINVAL;
			}
		}
	}

	if (max_bonds < 0 || max_bonds > INT_MAX) {
		printk(KERN_WARNING DRV_NAME
		       ": Warning: max_bonds (%d) not in range %d-%d, so it "
		       "was reset to BOND_DEFAULT_MAX_BONDS (%d)\n",
		       max_bonds, 0, INT_MAX, BOND_DEFAULT_MAX_BONDS);
			pr_info("lacp_rate param is irrelevant in mode %s\n",
				bond_mode_name(bond_mode));
		} else {
			bond_opt_initstr(&newval, lacp_rate);
			valptr = bond_opt_parse(bond_opt_get(BOND_OPT_LACP_RATE),
						&newval);
			if (!valptr) {
				pr_err("Error: Invalid lacp rate \"%s\"\n",
				       lacp_rate);
				return -EINVAL;
			}
			lacp_fast = valptr->value;
		}
	}

	if (ad_select) {
		bond_opt_initstr(&newval, ad_select);
		valptr = bond_opt_parse(bond_opt_get(BOND_OPT_AD_SELECT),
					&newval);
		if (!valptr) {
			pr_err("Error: Invalid ad_select \"%s\"\n", ad_select);
			return -EINVAL;
		}
		params->ad_select = valptr->value;
		if (bond_mode != BOND_MODE_8023AD)
			pr_warn("ad_select param only affects 802.3ad mode\n");
	} else {
		params->ad_select = BOND_AD_STABLE;
	}

	if (max_bonds < 0) {
		pr_warn("Warning: max_bonds (%d) not in range %d-%d, so it was reset to BOND_DEFAULT_MAX_BONDS (%d)\n",
			max_bonds, 0, INT_MAX, BOND_DEFAULT_MAX_BONDS);
		max_bonds = BOND_DEFAULT_MAX_BONDS;
	}

	if (miimon < 0) {
		printk(KERN_WARNING DRV_NAME
		       ": Warning: miimon module parameter (%d), "
		       "not in range 0-%d, so it was reset to %d\n",
		       miimon, INT_MAX, BOND_LINK_MON_INTERV);
		miimon = BOND_LINK_MON_INTERV;
	}

	if (updelay < 0) {
		printk(KERN_WARNING DRV_NAME
		       ": Warning: updelay module parameter (%d), "
		       "not in range 0-%d, so it was reset to 0\n",
		       updelay, INT_MAX);
		pr_warn("Warning: miimon module parameter (%d), not in range 0-%d, so it was reset to 0\n",
			miimon, INT_MAX);
		miimon = 0;
	}

	if (updelay < 0) {
		pr_warn("Warning: updelay module parameter (%d), not in range 0-%d, so it was reset to 0\n",
			updelay, INT_MAX);
		updelay = 0;
	}

	if (downdelay < 0) {
		printk(KERN_WARNING DRV_NAME
		       ": Warning: downdelay module parameter (%d), "
		       "not in range 0-%d, so it was reset to 0\n",
		       downdelay, INT_MAX);
		pr_warn("Warning: downdelay module parameter (%d), not in range 0-%d, so it was reset to 0\n",
			downdelay, INT_MAX);
		downdelay = 0;
	}

	if ((use_carrier != 0) && (use_carrier != 1)) {
		printk(KERN_WARNING DRV_NAME
		       ": Warning: use_carrier module parameter (%d), "
		       "not of valid value (0/1), so it was set to 1\n",
		       use_carrier);
		use_carrier = 1;
	}

	if (num_grat_arp < 0 || num_grat_arp > 255) {
		printk(KERN_WARNING DRV_NAME
		       ": Warning: num_grat_arp (%d) not in range 0-255 so it "
		       "was reset to 1 \n", num_grat_arp);
		num_grat_arp = 1;
	}

	/* reset values for 802.3ad */
	if (bond_mode == BOND_MODE_8023AD) {
		if (!miimon) {
			printk(KERN_WARNING DRV_NAME
			       ": Warning: miimon must be specified, "
			       "otherwise bonding will not detect link "
			       "failure, speed and duplex which are "
			       "essential for 802.3ad operation\n");
			printk(KERN_WARNING "Forcing miimon to 100msec\n");
			miimon = 100;
		}
	}

	/* reset values for TLB/ALB */
	if ((bond_mode == BOND_MODE_TLB) ||
	    (bond_mode == BOND_MODE_ALB)) {
		if (!miimon) {
			printk(KERN_WARNING DRV_NAME
			       ": Warning: miimon must be specified, "
			       "otherwise bonding will not detect link "
			       "failure and link speed which are essential "
			       "for TLB/ALB load balancing\n");
			printk(KERN_WARNING "Forcing miimon to 100msec\n");
			miimon = 100;
		}
	}

	if (bond_mode == BOND_MODE_ALB) {
		printk(KERN_NOTICE DRV_NAME
		       ": In ALB mode you might experience client "
		       "disconnections upon reconnection of a link if the "
		       "bonding module updelay parameter (%d msec) is "
		       "incompatible with the forwarding delay time of the "
		       "switch\n",
		       updelay);
		pr_warn("Warning: use_carrier module parameter (%d), not of valid value (0/1), so it was set to 1\n",
			use_carrier);
		use_carrier = 1;
	}

	if (num_peer_notif < 0 || num_peer_notif > 255) {
		pr_warn("Warning: num_grat_arp/num_unsol_na (%d) not in range 0-255 so it was reset to 1\n",
			num_peer_notif);
		num_peer_notif = 1;
	}

	/* reset values for 802.3ad/TLB/ALB */
	if (!bond_mode_uses_arp(bond_mode)) {
		if (!miimon) {
			pr_warn("Warning: miimon must be specified, otherwise bonding will not detect link failure, speed and duplex which are essential for 802.3ad operation\n");
			pr_warn("Forcing miimon to 100msec\n");
			miimon = BOND_DEFAULT_MIIMON;
		}
	}

	if (tx_queues < 1 || tx_queues > 255) {
		pr_warn("Warning: tx_queues (%d) should be between 1 and 255, resetting to %d\n",
			tx_queues, BOND_DEFAULT_TX_QUEUES);
		tx_queues = BOND_DEFAULT_TX_QUEUES;
	}

	if ((all_slaves_active != 0) && (all_slaves_active != 1)) {
		pr_warn("Warning: all_slaves_active module parameter (%d), not of valid value (0/1), so it was set to 0\n",
			all_slaves_active);
		all_slaves_active = 0;
	}

	if (resend_igmp < 0 || resend_igmp > 255) {
		pr_warn("Warning: resend_igmp (%d) should be between 0 and 255, resetting to %d\n",
			resend_igmp, BOND_DEFAULT_RESEND_IGMP);
		resend_igmp = BOND_DEFAULT_RESEND_IGMP;
	}

	bond_opt_initval(&newval, packets_per_slave);
	if (!bond_opt_parse(bond_opt_get(BOND_OPT_PACKETS_PER_SLAVE), &newval)) {
		pr_warn("Warning: packets_per_slave (%d) should be between 0 and %u resetting to 1\n",
			packets_per_slave, USHRT_MAX);
		packets_per_slave = 1;
	}

	if (bond_mode == BOND_MODE_ALB) {
		pr_notice("In ALB mode you might experience client disconnections upon reconnection of a link if the bonding module updelay parameter (%d msec) is incompatible with the forwarding delay time of the switch\n",
			  updelay);
	}

	if (!miimon) {
		if (updelay || downdelay) {
			/* just warn the user the up/down delay will have
			 * no effect since miimon is zero...
			 */
			printk(KERN_WARNING DRV_NAME
			       ": Warning: miimon module parameter not set "
			       "and updelay (%d) or downdelay (%d) module "
			       "parameter is set; updelay and downdelay have "
			       "no effect unless miimon is set\n",
			       updelay, downdelay);
			pr_warn("Warning: miimon module parameter not set and updelay (%d) or downdelay (%d) module parameter is set; updelay and downdelay have no effect unless miimon is set\n",
				updelay, downdelay);
		}
	} else {
		/* don't allow arp monitoring */
		if (arp_interval) {
			printk(KERN_WARNING DRV_NAME
			       ": Warning: miimon (%d) and arp_interval (%d) "
			       "can't be used simultaneously, disabling ARP "
			       "monitoring\n",
			       miimon, arp_interval);
			pr_warn("Warning: miimon (%d) and arp_interval (%d) can't be used simultaneously, disabling ARP monitoring\n",
				miimon, arp_interval);
			arp_interval = 0;
		}

		if ((updelay % miimon) != 0) {
			printk(KERN_WARNING DRV_NAME
			       ": Warning: updelay (%d) is not a multiple "
			       "of miimon (%d), updelay rounded to %d ms\n",
			       updelay, miimon, (updelay / miimon) * miimon);
			pr_warn("Warning: updelay (%d) is not a multiple of miimon (%d), updelay rounded to %d ms\n",
				updelay, miimon, (updelay / miimon) * miimon);
		}

		updelay /= miimon;

		if ((downdelay % miimon) != 0) {
			printk(KERN_WARNING DRV_NAME
			       ": Warning: downdelay (%d) is not a multiple "
			       "of miimon (%d), downdelay rounded to %d ms\n",
			       downdelay, miimon,
			       (downdelay / miimon) * miimon);
			pr_warn("Warning: downdelay (%d) is not a multiple of miimon (%d), downdelay rounded to %d ms\n",
				downdelay, miimon,
				(downdelay / miimon) * miimon);
		}

		downdelay /= miimon;
	}

	if (arp_interval < 0) {
		printk(KERN_WARNING DRV_NAME
		       ": Warning: arp_interval module parameter (%d) "
		       ", not in range 0-%d, so it was reset to %d\n",
		       arp_interval, INT_MAX, BOND_LINK_ARP_INTERV);
		arp_interval = BOND_LINK_ARP_INTERV;
	}

	for (arp_ip_count = 0;
	     (arp_ip_count < BOND_MAX_ARP_TARGETS) && arp_ip_target[arp_ip_count];
	     arp_ip_count++) {
		/* not complete check, but should be good enough to
		   catch mistakes */
		if (!isdigit(arp_ip_target[arp_ip_count][0])) {
			printk(KERN_WARNING DRV_NAME
			       ": Warning: bad arp_ip_target module parameter "
			       "(%s), ARP monitoring will not be performed\n",
			       arp_ip_target[arp_ip_count]);
			arp_interval = 0;
		} else {
			__be32 ip = in_aton(arp_ip_target[arp_ip_count]);
			arp_target[arp_ip_count] = ip;
		pr_warn("Warning: arp_interval module parameter (%d), not in range 0-%d, so it was reset to 0\n",
			arp_interval, INT_MAX);
		arp_interval = 0;
	}

	for (arp_ip_count = 0, i = 0;
	     (arp_ip_count < BOND_MAX_ARP_TARGETS) && arp_ip_target[i]; i++) {
		__be32 ip;

		/* not a complete check, but good enough to catch mistakes */
		if (!in4_pton(arp_ip_target[i], -1, (u8 *)&ip, -1, NULL) ||
		    !bond_is_ip_target_ok(ip)) {
			pr_warn("Warning: bad arp_ip_target module parameter (%s), ARP monitoring will not be performed\n",
				arp_ip_target[i]);
			arp_interval = 0;
		} else {
			if (bond_get_targets_ip(arp_target, ip) == -1)
				arp_target[arp_ip_count++] = ip;
			else
				pr_warn("Warning: duplicate address %pI4 in arp_ip_target, skipping\n",
					&ip);
		}
	}

	if (arp_interval && !arp_ip_count) {
		/* don't allow arping if no arp_ip_target given... */
		printk(KERN_WARNING DRV_NAME
		       ": Warning: arp_interval module parameter (%d) "
		       "specified without providing an arp_ip_target "
		       "parameter, arp_interval was reset to 0\n",
		       arp_interval);
		pr_warn("Warning: arp_interval module parameter (%d) specified without providing an arp_ip_target parameter, arp_interval was reset to 0\n",
			arp_interval);
		arp_interval = 0;
	}

	if (arp_validate) {
		if (bond_mode != BOND_MODE_ACTIVEBACKUP) {
			printk(KERN_ERR DRV_NAME
	       ": arp_validate only supported in active-backup mode\n");
			return -EINVAL;
		}
		if (!arp_interval) {
			printk(KERN_ERR DRV_NAME
			       ": arp_validate requires arp_interval\n");
			return -EINVAL;
		}

		arp_validate_value = bond_parse_parm(arp_validate,
						     arp_validate_tbl);
		if (arp_validate_value == -1) {
			printk(KERN_ERR DRV_NAME
			       ": Error: invalid arp_validate \"%s\"\n",
			       arp_validate == NULL ? "NULL" : arp_validate);
			return -EINVAL;
		}
	} else
		arp_validate_value = 0;

	if (miimon) {
		printk(KERN_INFO DRV_NAME
		       ": MII link monitoring set to %d ms\n",
		       miimon);
	} else if (arp_interval) {
		int i;

		printk(KERN_INFO DRV_NAME
		       ": ARP monitoring set to %d ms, validate %s, with %d target(s):",
		       arp_interval,
		       arp_validate_tbl[arp_validate_value].modename,
		       arp_ip_count);

		for (i = 0; i < arp_ip_count; i++)
			printk (" %s", arp_ip_target[i]);

		printk("\n");
		if (!arp_interval) {
			pr_err("arp_validate requires arp_interval\n");
			return -EINVAL;
		}

		bond_opt_initstr(&newval, arp_validate);
		valptr = bond_opt_parse(bond_opt_get(BOND_OPT_ARP_VALIDATE),
					&newval);
		if (!valptr) {
			pr_err("Error: invalid arp_validate \"%s\"\n",
			       arp_validate);
			return -EINVAL;
		}
		arp_validate_value = valptr->value;
	} else {
		arp_validate_value = 0;
	}

	arp_all_targets_value = 0;
	if (arp_all_targets) {
		bond_opt_initstr(&newval, arp_all_targets);
		valptr = bond_opt_parse(bond_opt_get(BOND_OPT_ARP_ALL_TARGETS),
					&newval);
		if (!valptr) {
			pr_err("Error: invalid arp_all_targets_value \"%s\"\n",
			       arp_all_targets);
			arp_all_targets_value = 0;
		} else {
			arp_all_targets_value = valptr->value;
		}
	}

	if (miimon) {
		pr_info("MII link monitoring set to %d ms\n", miimon);
	} else if (arp_interval) {
		valptr = bond_opt_get_val(BOND_OPT_ARP_VALIDATE,
					  arp_validate_value);
		pr_info("ARP monitoring set to %d ms, validate %s, with %d target(s):",
			arp_interval, valptr->string, arp_ip_count);

		for (i = 0; i < arp_ip_count; i++)
			pr_cont(" %s", arp_ip_target[i]);

		pr_cont("\n");

	} else if (max_bonds) {
		/* miimon and arp_interval not set, we need one so things
		 * work as expected, see bonding.txt for details
		 */
		printk(KERN_WARNING DRV_NAME
		       ": Warning: either miimon or arp_interval and "
		       "arp_ip_target module parameters must be specified, "
		       "otherwise bonding will not detect link failures! see "
		       "bonding.txt for details.\n");
	}

	if (primary && !USES_PRIMARY(bond_mode)) {
		/* currently, using a primary only makes sense
		 * in active backup, TLB or ALB modes
		 */
		printk(KERN_WARNING DRV_NAME
		       ": Warning: %s primary device specified but has no "
		       "effect in %s mode\n",
		       primary, bond_mode_name(bond_mode));
		primary = NULL;
	}

	if (fail_over_mac) {
		fail_over_mac_value = bond_parse_parm(fail_over_mac,
						      fail_over_mac_tbl);
		if (fail_over_mac_value == -1) {
			printk(KERN_ERR DRV_NAME
			       ": Error: invalid fail_over_mac \"%s\"\n",
			       arp_validate == NULL ? "NULL" : arp_validate);
			return -EINVAL;
		}

		if (bond_mode != BOND_MODE_ACTIVEBACKUP)
			printk(KERN_WARNING DRV_NAME
			       ": Warning: fail_over_mac only affects "
			       "active-backup mode.\n");
		pr_debug("Warning: either miimon or arp_interval and arp_ip_target module parameters must be specified, otherwise bonding will not detect link failures! see bonding.txt for details\n");
	}

	if (primary && !bond_mode_uses_primary(bond_mode)) {
		/* currently, using a primary only makes sense
		 * in active backup, TLB or ALB modes
		 */
		pr_warn("Warning: %s primary device specified but has no effect in %s mode\n",
			primary, bond_mode_name(bond_mode));
		primary = NULL;
	}

	if (primary && primary_reselect) {
		bond_opt_initstr(&newval, primary_reselect);
		valptr = bond_opt_parse(bond_opt_get(BOND_OPT_PRIMARY_RESELECT),
					&newval);
		if (!valptr) {
			pr_err("Error: Invalid primary_reselect \"%s\"\n",
			       primary_reselect);
			return -EINVAL;
		}
		primary_reselect_value = valptr->value;
	} else {
		primary_reselect_value = BOND_PRI_RESELECT_ALWAYS;
	}

	if (fail_over_mac) {
		bond_opt_initstr(&newval, fail_over_mac);
		valptr = bond_opt_parse(bond_opt_get(BOND_OPT_FAIL_OVER_MAC),
					&newval);
		if (!valptr) {
			pr_err("Error: invalid fail_over_mac \"%s\"\n",
			       fail_over_mac);
			return -EINVAL;
		}
		fail_over_mac_value = valptr->value;
		if (bond_mode != BOND_MODE_ACTIVEBACKUP)
			pr_warn("Warning: fail_over_mac only affects active-backup mode\n");
	} else {
		fail_over_mac_value = BOND_FOM_NONE;
	}

	bond_opt_initstr(&newval, "default");
	valptr = bond_opt_parse(
			bond_opt_get(BOND_OPT_AD_ACTOR_SYS_PRIO),
				     &newval);
	if (!valptr) {
		pr_err("Error: No ad_actor_sys_prio default value");
		return -EINVAL;
	}
	ad_actor_sys_prio = valptr->value;

	valptr = bond_opt_parse(bond_opt_get(BOND_OPT_AD_USER_PORT_KEY),
				&newval);
	if (!valptr) {
		pr_err("Error: No ad_user_port_key default value");
		return -EINVAL;
	}
	ad_user_port_key = valptr->value;

	if (lp_interval == 0) {
		pr_warn("Warning: ip_interval must be between 1 and %d, so it was reset to %d\n",
			INT_MAX, BOND_ALB_DEFAULT_LP_INTERVAL);
		lp_interval = BOND_ALB_DEFAULT_LP_INTERVAL;
	}

	/* fill params struct with the proper values */
	params->mode = bond_mode;
	params->xmit_policy = xmit_hashtype;
	params->miimon = miimon;
	params->num_grat_arp = num_grat_arp;
	params->arp_interval = arp_interval;
	params->arp_validate = arp_validate_value;
	params->num_peer_notif = num_peer_notif;
	params->arp_interval = arp_interval;
	params->arp_validate = arp_validate_value;
	params->arp_all_targets = arp_all_targets_value;
	params->updelay = updelay;
	params->downdelay = downdelay;
	params->use_carrier = use_carrier;
	params->lacp_fast = lacp_fast;
	params->primary[0] = 0;
	params->fail_over_mac = fail_over_mac_value;
	params->primary_reselect = primary_reselect_value;
	params->fail_over_mac = fail_over_mac_value;
	params->tx_queues = tx_queues;
	params->all_slaves_active = all_slaves_active;
	params->resend_igmp = resend_igmp;
	params->min_links = min_links;
	params->lp_interval = lp_interval;
	params->packets_per_slave = packets_per_slave;
	params->tlb_dynamic_lb = 1; /* Default value */
	params->ad_actor_sys_prio = ad_actor_sys_prio;
	eth_zero_addr(params->ad_actor_system);
	params->ad_user_port_key = ad_user_port_key;
	if (packets_per_slave > 0) {
		params->reciprocal_packets_per_slave =
			reciprocal_value(packets_per_slave);
	} else {
		/* reciprocal_packets_per_slave is unused if
		 * packets_per_slave is 0 or 1, just initialize it
		 */
		params->reciprocal_packets_per_slave =
			(struct reciprocal_value) { 0 };
	}

	if (primary) {
		strncpy(params->primary, primary, IFNAMSIZ);
		params->primary[IFNAMSIZ - 1] = 0;
	}

	memcpy(params->arp_targets, arp_target, sizeof(arp_target));

	return 0;
}

static struct lock_class_key bonding_netdev_xmit_lock_key;
static struct lock_class_key bonding_netdev_addr_lock_key;
static struct lock_class_key bonding_tx_busylock_key;

static void bond_set_lockdep_class_one(struct net_device *dev,
				       struct netdev_queue *txq,
				       void *_unused)
{
	lockdep_set_class(&txq->_xmit_lock,
			  &bonding_netdev_xmit_lock_key);
}

static void bond_set_lockdep_class(struct net_device *dev)
{
	lockdep_set_class(&dev->addr_list_lock,
			  &bonding_netdev_addr_lock_key);
	netdev_for_each_tx_queue(dev, bond_set_lockdep_class_one, NULL);
	dev->qdisc_tx_busylock = &bonding_tx_busylock_key;
}

/* Called from registration process */
static int bond_init(struct net_device *bond_dev)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct bond_net *bn = net_generic(dev_net(bond_dev), bond_net_id);

	netdev_dbg(bond_dev, "Begin bond_init\n");

	bond->wq = create_singlethread_workqueue(bond_dev->name);
	if (!bond->wq)
		return -ENOMEM;

	bond_set_lockdep_class(bond_dev);

	list_add_tail(&bond->bond_list, &bn->dev_list);

	bond_prepare_sysfs_group(bond);

	bond_debug_register(bond);

	/* Ensure valid dev_addr */
	if (is_zero_ether_addr(bond_dev->dev_addr) &&
	    bond_dev->addr_assign_type == NET_ADDR_PERM)
		eth_hw_addr_random(bond_dev);

	return 0;
}

unsigned int bond_get_num_tx_queues(void)
{
	return tx_queues;
}

/* Create a new bond based on the specified name and bonding parameters.
 * If name is NULL, obtain a suitable "bond%d" name for us.
 * Caller must NOT hold rtnl_lock; we need to release it here before we
 * set up our sysfs entries.
 */
int bond_create(char *name, struct bond_params *params)
{
	struct net_device *bond_dev;
	struct bonding *bond;
	int res;

	rtnl_lock();
	down_write(&bonding_rwsem);

	/* Check to see if the bond already exists. */
	if (name) {
		list_for_each_entry(bond, &bond_dev_list, bond_list)
			if (strnicmp(bond->dev->name, name, IFNAMSIZ) == 0) {
				printk(KERN_ERR DRV_NAME
			       ": cannot add bond %s; it already exists\n",
				       name);
				res = -EPERM;
				goto out_rtnl;
			}
	}

	bond_dev = alloc_netdev(sizeof(struct bonding), name ? name : "",
				ether_setup);
	if (!bond_dev) {
		printk(KERN_ERR DRV_NAME
		       ": %s: eek! can't alloc netdev!\n",
		       name);
		res = -ENOMEM;
		goto out_rtnl;
	}

	if (!name) {
		res = dev_alloc_name(bond_dev, "bond%d");
		if (res < 0)
			goto out_netdev;
	}

	/* bond_init() must be called after dev_alloc_name() (for the
	 * /proc files), but before register_netdevice(), because we
	 * need to set function pointers.
	 */

	res = bond_init(bond_dev, params);
	if (res < 0) {
		goto out_netdev;
	}

	res = register_netdevice(bond_dev);
	if (res < 0) {
		goto out_bond;
	}

	bond_set_lockdep_class(bond_dev);

	netif_carrier_off(bond_dev);

	up_write(&bonding_rwsem);
	rtnl_unlock(); /* allows sysfs registration of net device */
	res = bond_create_sysfs_entry(bond_dev->priv);
	if (res < 0) {
		rtnl_lock();
		down_write(&bonding_rwsem);
		bond_deinit(bond_dev);
		unregister_netdevice(bond_dev);
		goto out_rtnl;
	}

	return 0;

out_bond:
	bond_deinit(bond_dev);
out_netdev:
	free_netdev(bond_dev);
out_rtnl:
	up_write(&bonding_rwsem);
	rtnl_unlock();
	return res;
}

int bond_create(struct net *net, const char *name)
{
	struct net_device *bond_dev;
	struct bonding *bond;
	struct alb_bond_info *bond_info;
	int res;

	rtnl_lock();

	bond_dev = alloc_netdev_mq(sizeof(struct bonding),
				   name ? name : "bond%d", NET_NAME_UNKNOWN,
				   bond_setup, tx_queues);
	if (!bond_dev) {
		pr_err("%s: eek! can't alloc netdev!\n", name);
		rtnl_unlock();
		return -ENOMEM;
	}

	/*
	 * Initialize rx_hashtbl_used_head to RLB_NULL_INDEX.
	 * It is set to 0 by default which is wrong.
	 */
	bond = netdev_priv(bond_dev);
	bond_info = &(BOND_ALB_INFO(bond));
	bond_info->rx_hashtbl_used_head = RLB_NULL_INDEX;

	dev_net_set(bond_dev, net);
	bond_dev->rtnl_link_ops = &bond_link_ops;

	res = register_netdevice(bond_dev);

	netif_carrier_off(bond_dev);

	rtnl_unlock();
	if (res < 0)
		bond_destructor(bond_dev);
	return res;
}

static int __net_init bond_net_init(struct net *net)
{
	struct bond_net *bn = net_generic(net, bond_net_id);

	bn->net = net;
	INIT_LIST_HEAD(&bn->dev_list);

	bond_create_proc_dir(bn);
	bond_create_sysfs(bn);

	return 0;
}

static void __net_exit bond_net_exit(struct net *net)
{
	struct bond_net *bn = net_generic(net, bond_net_id);
	struct bonding *bond, *tmp_bond;
	LIST_HEAD(list);

	bond_destroy_sysfs(bn);

	/* Kill off any bonds created after unregistering bond rtnl ops */
	rtnl_lock();
	list_for_each_entry_safe(bond, tmp_bond, &bn->dev_list, bond_list)
		unregister_netdevice_queue(bond->dev, &list);
	unregister_netdevice_many(&list);
	rtnl_unlock();

	bond_destroy_proc_dir(bn);
}

static struct pernet_operations bond_net_ops = {
	.init = bond_net_init,
	.exit = bond_net_exit,
	.id   = &bond_net_id,
	.size = sizeof(struct bond_net),
};

static int __init bonding_init(void)
{
	int i;
	int res;
	struct bonding *bond;

	printk(KERN_INFO "%s", version);

	res = bond_check_params(&bonding_defaults);
	if (res) {
		goto out;
	}

#ifdef CONFIG_PROC_FS
	bond_create_proc_dir();
#endif

	init_rwsem(&bonding_rwsem);

	for (i = 0; i < max_bonds; i++) {
		res = bond_create(NULL, &bonding_defaults);

	pr_info("%s", bond_version);

	res = bond_check_params(&bonding_defaults);
	if (res)
		goto out;

	res = register_pernet_subsys(&bond_net_ops);
	if (res)
		goto out;

	res = bond_netlink_init();
	if (res)
		goto err_link;

	bond_create_debugfs();

	for (i = 0; i < max_bonds; i++) {
		res = bond_create(&init_net, NULL);
		if (res)
			goto err;
	}

	res = bond_create_sysfs();
	if (res)
		goto err;

	register_netdevice_notifier(&bond_netdev_notifier);
	register_inetaddr_notifier(&bond_inetaddr_notifier);

	goto out;
err:
	list_for_each_entry(bond, &bond_dev_list, bond_list) {
		bond_work_cancel_all(bond);
		destroy_workqueue(bond->wq);
	}

	bond_destroy_sysfs();

	rtnl_lock();
	bond_free_all();
	rtnl_unlock();
out:
	return res;
	register_netdevice_notifier(&bond_netdev_notifier);
out:
	return res;
err:
	bond_destroy_debugfs();
	bond_netlink_fini();
err_link:
	unregister_pernet_subsys(&bond_net_ops);
	goto out;

}

static void __exit bonding_exit(void)
{
	unregister_netdevice_notifier(&bond_netdev_notifier);
	unregister_inetaddr_notifier(&bond_inetaddr_notifier);

	bond_destroy_sysfs();

	rtnl_lock();
	bond_free_all();
	rtnl_unlock();

	bond_destroy_debugfs();

	bond_netlink_fini();
	unregister_pernet_subsys(&bond_net_ops);

#ifdef CONFIG_NET_POLL_CONTROLLER
	/* Make sure we don't have an imbalance on our netpoll blocking */
	WARN_ON(atomic_read(&netpoll_block_tx));
#endif
}

module_init(bonding_init);
module_exit(bonding_exit);
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
MODULE_DESCRIPTION(DRV_DESCRIPTION ", v" DRV_VERSION);
MODULE_AUTHOR("Thomas Davis, tadavis@lbl.gov and many others");
MODULE_SUPPORTED_DEVICE("most ethernet devices");

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 *  tab-width: 8
 * End:
 */

