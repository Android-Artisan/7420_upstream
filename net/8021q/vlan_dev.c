/* -*- linux-c -*-
 * INET		802.1Q VLAN
 *		Ethernet-type device handling.
 *
 * Authors:	Ben Greear <greearb@candelatech.com>
 *              Please send support related email to: netdev@vger.kernel.org
 *              VLAN Home Page: http://www.candelatech.com/~greear/vlan.html
 *
 * Fixes:       Mar 22 2001: Martin Bokaemper <mbokaemper@unispherenetworks.com>
 *                - reset skb->pkt_type on incoming packets when MAC was changed
 *                - see that changed MAC is saddr for outgoing packets
 *              Oct 20, 2001:  Ard van Breeman:
 *                - Fix MC-list, finally.
 *                - Flush MC-list on VLAN destroy.
 *
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/net_tstamp.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/phy.h>
#include <net/arp.h>

#include "vlan.h"
#include "vlanproc.h"
#include <linux/if_vlan.h>

/*
 *	Rebuild the Ethernet MAC header. This is called after an ARP
 *	(or in future other address resolution) has completed on this
 *	sk_buff. We now let ARP fill in the other fields.
 *
 *	This routine CANNOT use cached dst->neigh!
 *	Really, it is used only when dst->neigh is wrong.
 *
 * TODO:  This needs a checkup, I'm ignorant here. --BLG
 */
static int vlan_dev_rebuild_header(struct sk_buff *skb)
{
	struct net_device *dev = skb->dev;
	struct vlan_ethhdr *veth = (struct vlan_ethhdr *)(skb->data);

	switch (veth->h_vlan_encapsulated_proto) {
#ifdef CONFIG_INET
	case __constant_htons(ETH_P_IP):

		/* TODO:  Confirm this will work with VLAN headers... */
		return arp_find(veth->h_dest, skb);
#endif
	default:
		pr_debug("%s: unable to resolve type %X addresses.\n",
			 dev->name, ntohs(veth->h_vlan_encapsulated_proto));

		memcpy(veth->h_source, dev->dev_addr, ETH_ALEN);
		break;
	}

	return 0;
}

static inline struct sk_buff *vlan_check_reorder_header(struct sk_buff *skb)
{
	if (vlan_dev_info(skb->dev)->flags & VLAN_FLAG_REORDER_HDR) {
		if (skb_cow(skb, skb_headroom(skb)) < 0)
			skb = NULL;
		if (skb) {
			/* Lifted from Gleb's VLAN code... */
			memmove(skb->data - ETH_HLEN,
				skb->data - VLAN_ETH_HLEN, 12);
			skb->mac_header += VLAN_HLEN;
		}
	}

	return skb;
}

static inline void vlan_set_encap_proto(struct sk_buff *skb,
		struct vlan_hdr *vhdr)
{
	__be16 proto;
	unsigned char *rawp;

	/*
	 * Was a VLAN packet, grab the encapsulated protocol, which the layer
	 * three protocols care about.
	 */

	proto = vhdr->h_vlan_encapsulated_proto;
	if (ntohs(proto) >= 1536) {
		skb->protocol = proto;
		return;
	}

	rawp = skb->data;
	if (*(unsigned short *)rawp == 0xFFFF)
		/*
		 * This is a magic hack to spot IPX packets. Older Novell
		 * breaks the protocol design and runs IPX over 802.3 without
		 * an 802.2 LLC layer. We look for FFFF which isn't a used
		 * 802.2 SSAP/DSAP. This won't work for fault tolerant netware
		 * but does for the rest.
		 */
		skb->protocol = htons(ETH_P_802_3);
	else
		/*
		 * Real 802.2 LLC
		 */
		skb->protocol = htons(ETH_P_802_2);
}

/*
 *	Determine the packet's protocol ID. The rule here is that we
 *	assume 802.3 if the type field is short enough to be a length.
 *	This is normal practice and works for any 'now in use' protocol.
 *
 *  Also, at this point we assume that we ARE dealing exclusively with
 *  VLAN packets, or packets that should be made into VLAN packets based
 *  on a default VLAN ID.
 *
 *  NOTE:  Should be similar to ethernet/eth.c.
 *
 *  SANITY NOTE:  This method is called when a packet is moving up the stack
 *                towards userland.  To get here, it would have already passed
 *                through the ethernet/eth.c eth_type_trans() method.
 *  SANITY NOTE 2: We are referencing to the VLAN_HDR frields, which MAY be
 *                 stored UNALIGNED in the memory.  RISC systems don't like
 *                 such cases very much...
 *  SANITY NOTE 2a: According to Dave Miller & Alexey, it will always be
 *  		    aligned, so there doesn't need to be any of the unaligned
 *  		    stuff.  It has been commented out now...  --Ben
 *
 */
int vlan_skb_recv(struct sk_buff *skb, struct net_device *dev,
		  struct packet_type *ptype, struct net_device *orig_dev)
{
	struct vlan_hdr *vhdr;
	struct net_device_stats *stats;
	u16 vlan_id;
	u16 vlan_tci;

	skb = skb_share_check(skb, GFP_ATOMIC);
	if (skb == NULL)
		goto err_free;

	if (unlikely(!pskb_may_pull(skb, VLAN_HLEN)))
		goto err_free;

	vhdr = (struct vlan_hdr *)skb->data;
	vlan_tci = ntohs(vhdr->h_vlan_TCI);
	vlan_id = vlan_tci & VLAN_VID_MASK;

	rcu_read_lock();
	skb->dev = __find_vlan_dev(dev, vlan_id);
	if (!skb->dev) {
		pr_debug("%s: ERROR: No net_device for VID: %u on dev: %s\n",
			 __func__, vlan_id, dev->name);
		goto err_unlock;
	}

	skb->dev->last_rx = jiffies;

	stats = &skb->dev->stats;
	stats->rx_packets++;
	stats->rx_bytes += skb->len;

	skb_pull_rcsum(skb, VLAN_HLEN);

	skb->priority = vlan_get_ingress_priority(skb->dev, vlan_tci);

	pr_debug("%s: priority: %u for TCI: %hu\n",
		 __func__, skb->priority, vlan_tci);

	switch (skb->pkt_type) {
	case PACKET_BROADCAST: /* Yeah, stats collect these together.. */
		/* stats->broadcast ++; // no such counter :-( */
		break;

	case PACKET_MULTICAST:
		stats->multicast++;
		break;

	case PACKET_OTHERHOST:
		/* Our lower layer thinks this is not local, let's make sure.
		 * This allows the VLAN to have a different MAC than the
		 * underlying device, and still route correctly.
		 */
		if (!compare_ether_addr(eth_hdr(skb)->h_dest,
					skb->dev->dev_addr))
			skb->pkt_type = PACKET_HOST;
		break;
	default:
		break;
	}

	vlan_set_encap_proto(skb, vhdr);

	skb = vlan_check_reorder_header(skb);
	if (!skb) {
		stats->rx_errors++;
		goto err_unlock;
	}

	netif_rx(skb);
	rcu_read_unlock();
	return NET_RX_SUCCESS;

err_unlock:
	rcu_read_unlock();
err_free:
	kfree_skb(skb);
	return NET_RX_DROP;
}

static inline u16
vlan_dev_get_egress_qos_mask(struct net_device *dev, struct sk_buff *skb)
{
	struct vlan_priority_tci_mapping *mp;

	mp = vlan_dev_info(dev)->egress_priority_map[(skb->priority & 0xF)];
	while (mp) {
		if (mp->priority == skb->priority) {
			return mp->vlan_qos; /* This should already be shifted
					      * to mask correctly with the
					      * VLAN's TCI */
		}
		mp = mp->next;
	}
	return 0;
}
#include <linux/netpoll.h>

/*
 *	Create the VLAN header for an arbitrary protocol layer
 *
 *	saddr=NULL	means use device source address
 *	daddr=NULL	means leave destination address (eg unresolved arp)
 *
 *  This is called when the SKB is moving down the stack towards the
 *  physical devices.
 */
static int vlan_dev_hard_header(struct sk_buff *skb, struct net_device *dev,
				unsigned short type,
				const void *daddr, const void *saddr,
				unsigned int len)
{
	struct vlan_dev_priv *vlan = vlan_dev_priv(dev);
	struct vlan_hdr *vhdr;
	unsigned int vhdrlen = 0;
	u16 vlan_tci = 0;
	int rc;

	if (WARN_ON(skb_headroom(skb) < dev->hard_header_len))
		return -ENOSPC;

	if (!(vlan_dev_info(dev)->flags & VLAN_FLAG_REORDER_HDR)) {
		vhdr = (struct vlan_hdr *) skb_push(skb, VLAN_HLEN);

		vlan_tci = vlan_dev_info(dev)->vlan_id;
		vlan_tci |= vlan_dev_get_egress_qos_mask(dev, skb);
		vhdr->h_vlan_TCI = htons(vlan_tci);

		/*
		 *  Set the protocol type. For a packet of type ETH_P_802_3 we
		 *  put the length in here instead. It is up to the 802.2
		 *  layer to carry protocol information.
		 */
		if (type != ETH_P_802_3)
	if (!(vlan->flags & VLAN_FLAG_REORDER_HDR)) {
		vhdr = (struct vlan_hdr *) skb_push(skb, VLAN_HLEN);

		vlan_tci = vlan->vlan_id;
		vlan_tci |= vlan_dev_get_egress_qos_mask(dev, skb->priority);
		vhdr->h_vlan_TCI = htons(vlan_tci);

		/*
		 *  Set the protocol type. For a packet of type ETH_P_802_3/2 we
		 *  put the length in here instead.
		 */
		if (type != ETH_P_802_3 && type != ETH_P_802_2)
			vhdr->h_vlan_encapsulated_proto = htons(type);
		else
			vhdr->h_vlan_encapsulated_proto = htons(len);

		skb->protocol = htons(ETH_P_8021Q);
		type = ETH_P_8021Q;
		skb->protocol = vlan->vlan_proto;
		type = ntohs(vlan->vlan_proto);
		vhdrlen = VLAN_HLEN;
	}

	/* Before delegating work to the lower layer, enter our MAC-address */
	if (saddr == NULL)
		saddr = dev->dev_addr;

	/* Now make the underlying real hard header */
	dev = vlan_dev_info(dev)->real_dev;
	dev = vlan->real_dev;
	rc = dev_hard_header(skb, dev, type, daddr, saddr, len + vhdrlen);
	if (rc > 0)
		rc += vhdrlen;
	return rc;
}

static int vlan_dev_hard_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct net_device_stats *stats = &dev->stats;
	struct vlan_ethhdr *veth = (struct vlan_ethhdr *)(skb->data);
static inline netdev_tx_t vlan_netpoll_send_skb(struct vlan_dev_priv *vlan, struct sk_buff *skb)
{
#ifdef CONFIG_NET_POLL_CONTROLLER
	if (vlan->netpoll)
		netpoll_send_skb(vlan->netpoll, skb);
#else
	BUG();
#endif
	return NETDEV_TX_OK;
}

static netdev_tx_t vlan_dev_hard_start_xmit(struct sk_buff *skb,
					    struct net_device *dev)
{
	struct vlan_dev_priv *vlan = vlan_dev_priv(dev);
	struct vlan_ethhdr *veth = (struct vlan_ethhdr *)(skb->data);
	unsigned int len;
	int ret;

	/* Handle non-VLAN frames if they are sent to us, for example by DHCP.
	 *
	 * NOTE: THIS ASSUMES DIX ETHERNET, SPECIFICALLY NOT SUPPORTING
	 * OTHER THINGS LIKE FDDI/TokenRing/802.3 SNAPs...
	 */
	if (veth->h_vlan_proto != htons(ETH_P_8021Q) ||
	    vlan_dev_info(dev)->flags & VLAN_FLAG_REORDER_HDR) {
		unsigned int orig_headroom = skb_headroom(skb);
		u16 vlan_tci;

		vlan_dev_info(dev)->cnt_encap_on_xmit++;

		vlan_tci = vlan_dev_info(dev)->vlan_id;
		vlan_tci |= vlan_dev_get_egress_qos_mask(dev, skb);
		skb = __vlan_put_tag(skb, vlan_tci);
		if (!skb) {
			stats->tx_dropped++;
			return NETDEV_TX_OK;
		}

		if (orig_headroom < VLAN_HLEN)
			vlan_dev_info(dev)->cnt_inc_headroom_on_tx++;
	}

	stats->tx_packets++;
	stats->tx_bytes += skb->len;

	skb->dev = vlan_dev_info(dev)->real_dev;
	dev_queue_xmit(skb);
	return NETDEV_TX_OK;
}

static int vlan_dev_hwaccel_hard_start_xmit(struct sk_buff *skb,
					    struct net_device *dev)
{
	struct net_device_stats *stats = &dev->stats;
	u16 vlan_tci;

	vlan_tci = vlan_dev_info(dev)->vlan_id;
	vlan_tci |= vlan_dev_get_egress_qos_mask(dev, skb);
	skb = __vlan_hwaccel_put_tag(skb, vlan_tci);

	stats->tx_packets++;
	stats->tx_bytes += skb->len;

	skb->dev = vlan_dev_info(dev)->real_dev;
	dev_queue_xmit(skb);
	return NETDEV_TX_OK;
	if (veth->h_vlan_proto != vlan->vlan_proto ||
	    vlan->flags & VLAN_FLAG_REORDER_HDR) {
	if (vlan->flags & VLAN_FLAG_REORDER_HDR ||
	    veth->h_vlan_proto != vlan->vlan_proto) {
		u16 vlan_tci;
		vlan_tci = vlan->vlan_id;
		vlan_tci |= vlan_dev_get_egress_qos_mask(dev, skb->priority);
		__vlan_hwaccel_put_tag(skb, vlan->vlan_proto, vlan_tci);
	}

	skb->dev = vlan->real_dev;
	len = skb->len;
	if (unlikely(netpoll_tx_running(dev)))
		return vlan_netpoll_send_skb(vlan, skb);

	ret = dev_queue_xmit(skb);

	if (likely(ret == NET_XMIT_SUCCESS || ret == NET_XMIT_CN)) {
		struct vlan_pcpu_stats *stats;

		stats = this_cpu_ptr(vlan->vlan_pcpu_stats);
		u64_stats_update_begin(&stats->syncp);
		stats->tx_packets++;
		stats->tx_bytes += len;
		u64_stats_update_end(&stats->syncp);
	} else {
		this_cpu_inc(vlan->vlan_pcpu_stats->tx_dropped);
	}

	return ret;
}

static int vlan_dev_change_mtu(struct net_device *dev, int new_mtu)
{
	/* TODO: gotta make sure the underlying layer can handle it,
	 * maybe an IFF_VLAN_CAPABLE flag for devices?
	 */
	if (vlan_dev_info(dev)->real_dev->mtu < new_mtu)
	if (vlan_dev_priv(dev)->real_dev->mtu < new_mtu)
		return -ERANGE;

	dev->mtu = new_mtu;

	return 0;
}

void vlan_dev_set_ingress_priority(const struct net_device *dev,
				   u32 skb_prio, u16 vlan_prio)
{
	struct vlan_dev_info *vlan = vlan_dev_info(dev);
	struct vlan_dev_priv *vlan = vlan_dev_priv(dev);

	if (vlan->ingress_priority_map[vlan_prio & 0x7] && !skb_prio)
		vlan->nr_ingress_mappings--;
	else if (!vlan->ingress_priority_map[vlan_prio & 0x7] && skb_prio)
		vlan->nr_ingress_mappings++;

	vlan->ingress_priority_map[vlan_prio & 0x7] = skb_prio;
}

int vlan_dev_set_egress_priority(const struct net_device *dev,
				 u32 skb_prio, u16 vlan_prio)
{
	struct vlan_dev_info *vlan = vlan_dev_info(dev);
	struct vlan_priority_tci_mapping *mp = NULL;
	struct vlan_priority_tci_mapping *np;
	u32 vlan_qos = (vlan_prio << 13) & 0xE000;
	struct vlan_dev_priv *vlan = vlan_dev_priv(dev);
	struct vlan_priority_tci_mapping *mp = NULL;
	struct vlan_priority_tci_mapping *np;
	u32 vlan_qos = (vlan_prio << VLAN_PRIO_SHIFT) & VLAN_PRIO_MASK;

	/* See if a priority mapping exists.. */
	mp = vlan->egress_priority_map[skb_prio & 0xF];
	while (mp) {
		if (mp->priority == skb_prio) {
			if (mp->vlan_qos && !vlan_qos)
				vlan->nr_egress_mappings--;
			else if (!mp->vlan_qos && vlan_qos)
				vlan->nr_egress_mappings++;
			mp->vlan_qos = vlan_qos;
			return 0;
		}
		mp = mp->next;
	}

	/* Create a new mapping then. */
	mp = vlan->egress_priority_map[skb_prio & 0xF];
	np = kmalloc(sizeof(struct vlan_priority_tci_mapping), GFP_KERNEL);
	if (!np)
		return -ENOBUFS;

	np->next = mp;
	np->priority = skb_prio;
	np->vlan_qos = vlan_qos;
	/* Before inserting this element in hash table, make sure all its fields
	 * are committed to memory.
	 * coupled with smp_rmb() in vlan_dev_get_egress_qos_mask()
	 */
	smp_wmb();
	vlan->egress_priority_map[skb_prio & 0xF] = np;
	if (vlan_qos)
		vlan->nr_egress_mappings++;
	return 0;
}

/* Flags are defined in the vlan_flags enum in include/linux/if_vlan.h file. */
int vlan_dev_change_flags(const struct net_device *dev, u32 flags, u32 mask)
{
	struct vlan_dev_info *vlan = vlan_dev_info(dev);
	u32 old_flags = vlan->flags;

	if (mask & ~(VLAN_FLAG_REORDER_HDR | VLAN_FLAG_GVRP))
	struct vlan_dev_priv *vlan = vlan_dev_priv(dev);
	u32 old_flags = vlan->flags;

	if (mask & ~(VLAN_FLAG_REORDER_HDR | VLAN_FLAG_GVRP |
		     VLAN_FLAG_LOOSE_BINDING | VLAN_FLAG_MVRP))
		return -EINVAL;

	vlan->flags = (old_flags & ~mask) | (flags & mask);

	if (netif_running(dev) && (vlan->flags ^ old_flags) & VLAN_FLAG_GVRP) {
		if (vlan->flags & VLAN_FLAG_GVRP)
			vlan_gvrp_request_join(dev);
		else
			vlan_gvrp_request_leave(dev);
	}

	if (netif_running(dev) && (vlan->flags ^ old_flags) & VLAN_FLAG_MVRP) {
		if (vlan->flags & VLAN_FLAG_MVRP)
			vlan_mvrp_request_join(dev);
		else
			vlan_mvrp_request_leave(dev);
	}
	return 0;
}

void vlan_dev_get_realdev_name(const struct net_device *dev, char *result)
{
	strncpy(result, vlan_dev_info(dev)->real_dev->name, 23);
	strncpy(result, vlan_dev_priv(dev)->real_dev->name, 23);
}

bool vlan_dev_inherit_address(struct net_device *dev,
			      struct net_device *real_dev)
{
	if (dev->addr_assign_type != NET_ADDR_STOLEN)
		return false;

	ether_addr_copy(dev->dev_addr, real_dev->dev_addr);
	call_netdevice_notifiers(NETDEV_CHANGEADDR, dev);
	return true;
}

static int vlan_dev_open(struct net_device *dev)
{
	struct vlan_dev_info *vlan = vlan_dev_info(dev);
	struct net_device *real_dev = vlan->real_dev;
	int err;

	if (!(real_dev->flags & IFF_UP))
		return -ENETDOWN;

	if (compare_ether_addr(dev->dev_addr, real_dev->dev_addr)) {
		err = dev_unicast_add(real_dev, dev->dev_addr, ETH_ALEN);
	struct vlan_dev_priv *vlan = vlan_dev_priv(dev);
	struct net_device *real_dev = vlan->real_dev;
	int err;

	if (!(real_dev->flags & IFF_UP) &&
	    !(vlan->flags & VLAN_FLAG_LOOSE_BINDING))
		return -ENETDOWN;

	if (!ether_addr_equal(dev->dev_addr, real_dev->dev_addr) &&
	    !vlan_dev_inherit_address(dev, real_dev)) {
		err = dev_uc_add(real_dev, dev->dev_addr);
		if (err < 0)
			goto out;
	}

	if (dev->flags & IFF_ALLMULTI) {
		err = dev_set_allmulti(real_dev, 1);
		if (err < 0)
			goto del_unicast;
	}
	if (dev->flags & IFF_PROMISC) {
		err = dev_set_promiscuity(real_dev, 1);
		if (err < 0)
			goto clear_allmulti;
	}

	memcpy(vlan->real_dev_addr, real_dev->dev_addr, ETH_ALEN);
	ether_addr_copy(vlan->real_dev_addr, real_dev->dev_addr);

	if (vlan->flags & VLAN_FLAG_GVRP)
		vlan_gvrp_request_join(dev);

	if (vlan->flags & VLAN_FLAG_MVRP)
		vlan_mvrp_request_join(dev);

	if (netif_carrier_ok(real_dev))
		netif_carrier_on(dev);
	return 0;

clear_allmulti:
	if (dev->flags & IFF_ALLMULTI)
		dev_set_allmulti(real_dev, -1);
del_unicast:
	if (compare_ether_addr(dev->dev_addr, real_dev->dev_addr))
		dev_unicast_delete(real_dev, dev->dev_addr, ETH_ALEN);
out:
	if (!ether_addr_equal(dev->dev_addr, real_dev->dev_addr))
		dev_uc_del(real_dev, dev->dev_addr);
out:
	netif_carrier_off(dev);
	return err;
}

static int vlan_dev_stop(struct net_device *dev)
{
	struct vlan_dev_info *vlan = vlan_dev_info(dev);
	struct net_device *real_dev = vlan->real_dev;

	if (vlan->flags & VLAN_FLAG_GVRP)
		vlan_gvrp_request_leave(dev);

	dev_mc_unsync(real_dev, dev);
	dev_unicast_unsync(real_dev, dev);
	struct vlan_dev_priv *vlan = vlan_dev_priv(dev);
	struct net_device *real_dev = vlan->real_dev;

	dev_mc_unsync(real_dev, dev);
	dev_uc_unsync(real_dev, dev);

	if (compare_ether_addr(dev->dev_addr, real_dev->dev_addr))
		dev_unicast_delete(real_dev, dev->dev_addr, dev->addr_len);

	if (!ether_addr_equal(dev->dev_addr, real_dev->dev_addr))
		dev_uc_del(real_dev, dev->dev_addr);

	netif_carrier_off(dev);
	return 0;
}

static int vlan_dev_set_mac_address(struct net_device *dev, void *p)
{
	struct net_device *real_dev = vlan_dev_info(dev)->real_dev;
	struct net_device *real_dev = vlan_dev_priv(dev)->real_dev;
	struct sockaddr *addr = p;
	int err;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	if (!(dev->flags & IFF_UP))
		goto out;

	if (compare_ether_addr(addr->sa_data, real_dev->dev_addr)) {
		err = dev_unicast_add(real_dev, addr->sa_data, ETH_ALEN);
	if (!ether_addr_equal(addr->sa_data, real_dev->dev_addr)) {
		err = dev_uc_add(real_dev, addr->sa_data);
		if (err < 0)
			return err;
	}

	if (compare_ether_addr(dev->dev_addr, real_dev->dev_addr))
		dev_unicast_delete(real_dev, dev->dev_addr, ETH_ALEN);

out:
	memcpy(dev->dev_addr, addr->sa_data, ETH_ALEN);
	if (!ether_addr_equal(dev->dev_addr, real_dev->dev_addr))
		dev_uc_del(real_dev, dev->dev_addr);

out:
	ether_addr_copy(dev->dev_addr, addr->sa_data);
	return 0;
}

static int vlan_dev_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct net_device *real_dev = vlan_dev_info(dev)->real_dev;
	struct net_device *real_dev = vlan_dev_priv(dev)->real_dev;
	const struct net_device_ops *ops = real_dev->netdev_ops;
	struct ifreq ifrr;
	int err = -EOPNOTSUPP;

	strncpy(ifrr.ifr_name, real_dev->name, IFNAMSIZ);
	ifrr.ifr_ifru = ifr->ifr_ifru;

	switch (cmd) {
	case SIOCSHWTSTAMP:
		if (!net_eq(dev_net(dev), dev_net(real_dev)))
			break;
	case SIOCGMIIPHY:
	case SIOCGMIIREG:
	case SIOCSMIIREG:
		if (real_dev->do_ioctl && netif_device_present(real_dev))
			err = real_dev->do_ioctl(real_dev, &ifrr, cmd);
	case SIOCSHWTSTAMP:
	case SIOCGHWTSTAMP:
		if (netif_device_present(real_dev) && ops->ndo_do_ioctl)
			err = ops->ndo_do_ioctl(real_dev, &ifrr, cmd);
		break;
	}

	if (!err)
		ifr->ifr_ifru = ifrr.ifr_ifru;

	return err;
}

static void vlan_dev_change_rx_flags(struct net_device *dev, int change)
{
	struct net_device *real_dev = vlan_dev_info(dev)->real_dev;

	if (change & IFF_ALLMULTI)
		dev_set_allmulti(real_dev, dev->flags & IFF_ALLMULTI ? 1 : -1);
	if (change & IFF_PROMISC)
		dev_set_promiscuity(real_dev, dev->flags & IFF_PROMISC ? 1 : -1);
static int vlan_dev_neigh_setup(struct net_device *dev, struct neigh_parms *pa)
{
	struct net_device *real_dev = vlan_dev_priv(dev)->real_dev;
	const struct net_device_ops *ops = real_dev->netdev_ops;
	int err = 0;

	if (netif_device_present(real_dev) && ops->ndo_neigh_setup)
		err = ops->ndo_neigh_setup(real_dev, pa);

	return err;
}

#if IS_ENABLED(CONFIG_FCOE)
static int vlan_dev_fcoe_ddp_setup(struct net_device *dev, u16 xid,
				   struct scatterlist *sgl, unsigned int sgc)
{
	struct net_device *real_dev = vlan_dev_priv(dev)->real_dev;
	const struct net_device_ops *ops = real_dev->netdev_ops;
	int rc = 0;

	if (ops->ndo_fcoe_ddp_setup)
		rc = ops->ndo_fcoe_ddp_setup(real_dev, xid, sgl, sgc);

	return rc;
}

static int vlan_dev_fcoe_ddp_done(struct net_device *dev, u16 xid)
{
	struct net_device *real_dev = vlan_dev_priv(dev)->real_dev;
	const struct net_device_ops *ops = real_dev->netdev_ops;
	int len = 0;

	if (ops->ndo_fcoe_ddp_done)
		len = ops->ndo_fcoe_ddp_done(real_dev, xid);

	return len;
}

static int vlan_dev_fcoe_enable(struct net_device *dev)
{
	struct net_device *real_dev = vlan_dev_priv(dev)->real_dev;
	const struct net_device_ops *ops = real_dev->netdev_ops;
	int rc = -EINVAL;

	if (ops->ndo_fcoe_enable)
		rc = ops->ndo_fcoe_enable(real_dev);
	return rc;
}

static int vlan_dev_fcoe_disable(struct net_device *dev)
{
	struct net_device *real_dev = vlan_dev_priv(dev)->real_dev;
	const struct net_device_ops *ops = real_dev->netdev_ops;
	int rc = -EINVAL;

	if (ops->ndo_fcoe_disable)
		rc = ops->ndo_fcoe_disable(real_dev);
	return rc;
}

static int vlan_dev_fcoe_get_wwn(struct net_device *dev, u64 *wwn, int type)
{
	struct net_device *real_dev = vlan_dev_priv(dev)->real_dev;
	const struct net_device_ops *ops = real_dev->netdev_ops;
	int rc = -EINVAL;

	if (ops->ndo_fcoe_get_wwn)
		rc = ops->ndo_fcoe_get_wwn(real_dev, wwn, type);
	return rc;
}

static int vlan_dev_fcoe_ddp_target(struct net_device *dev, u16 xid,
				    struct scatterlist *sgl, unsigned int sgc)
{
	struct net_device *real_dev = vlan_dev_priv(dev)->real_dev;
	const struct net_device_ops *ops = real_dev->netdev_ops;
	int rc = 0;

	if (ops->ndo_fcoe_ddp_target)
		rc = ops->ndo_fcoe_ddp_target(real_dev, xid, sgl, sgc);

	return rc;
}
#endif

static void vlan_dev_change_rx_flags(struct net_device *dev, int change)
{
	struct net_device *real_dev = vlan_dev_priv(dev)->real_dev;

	if (change & IFF_ALLMULTI)
		dev_set_allmulti(real_dev, dev->flags & IFF_ALLMULTI ? 1 : -1);
	if (change & IFF_PROMISC)
		dev_set_promiscuity(real_dev, dev->flags & IFF_PROMISC ? 1 : -1);
}

static void vlan_dev_set_rx_mode(struct net_device *vlan_dev)
{
	dev_mc_sync(vlan_dev_info(vlan_dev)->real_dev, vlan_dev);
	dev_unicast_sync(vlan_dev_info(vlan_dev)->real_dev, vlan_dev);
	dev_mc_sync(vlan_dev_priv(vlan_dev)->real_dev, vlan_dev);
	dev_uc_sync(vlan_dev_priv(vlan_dev)->real_dev, vlan_dev);
}

/*
 * vlan network devices have devices nesting below it, and are a special
 * "super class" of normal network devices; split their locks off into a
 * separate class since they always nest.
 */
static struct lock_class_key vlan_netdev_xmit_lock_key;
static struct lock_class_key vlan_netdev_addr_lock_key;

static void vlan_dev_set_lockdep_one(struct net_device *dev,
				     struct netdev_queue *txq,
				     void *_subclass)
{
	lockdep_set_class_and_subclass(&txq->_xmit_lock,
				       &vlan_netdev_xmit_lock_key,
				       *(int *)_subclass);
}

static void vlan_dev_set_lockdep_class(struct net_device *dev, int subclass)
{
	lockdep_set_class_and_subclass(&dev->addr_list_lock,
				       &vlan_netdev_addr_lock_key,
				       subclass);
	netdev_for_each_tx_queue(dev, vlan_dev_set_lockdep_one, &subclass);
}

static const struct header_ops vlan_header_ops = {
	.create	 = vlan_dev_hard_header,
	.rebuild = vlan_dev_rebuild_header,
	.parse	 = eth_header_parse,
};

static int vlan_dev_init(struct net_device *dev)
{
	struct net_device *real_dev = vlan_dev_info(dev)->real_dev;
	int subclass = 0;

	/* IFF_BROADCAST|IFF_MULTICAST; ??? */
	dev->flags  = real_dev->flags & ~(IFF_UP | IFF_PROMISC | IFF_ALLMULTI);
	dev->iflink = real_dev->ifindex;
static int vlan_dev_get_lock_subclass(struct net_device *dev)
{
	return vlan_dev_priv(dev)->nest_level;
}

static const struct header_ops vlan_header_ops = {
	.create	 = vlan_dev_hard_header,
	.parse	 = eth_header_parse,
};

static int vlan_passthru_hard_header(struct sk_buff *skb, struct net_device *dev,
				     unsigned short type,
				     const void *daddr, const void *saddr,
				     unsigned int len)
{
	struct vlan_dev_priv *vlan = vlan_dev_priv(dev);
	struct net_device *real_dev = vlan->real_dev;

	if (saddr == NULL)
		saddr = dev->dev_addr;

	return dev_hard_header(skb, real_dev, type, daddr, saddr, len);
}

static const struct header_ops vlan_passthru_header_ops = {
	.create	 = vlan_passthru_hard_header,
	.parse	 = eth_header_parse,
};

static struct device_type vlan_type = {
	.name	= "vlan",
};

static const struct net_device_ops vlan_netdev_ops;

static int vlan_dev_init(struct net_device *dev)
{
	struct net_device *real_dev = vlan_dev_priv(dev)->real_dev;

	netif_carrier_off(dev);

	/* IFF_BROADCAST|IFF_MULTICAST; ??? */
	dev->flags  = real_dev->flags & ~(IFF_UP | IFF_PROMISC | IFF_ALLMULTI |
					  IFF_MASTER | IFF_SLAVE);
	dev->state  = (real_dev->state & ((1<<__LINK_STATE_NOCARRIER) |
					  (1<<__LINK_STATE_DORMANT))) |
		      (1<<__LINK_STATE_PRESENT);

	dev->features |= real_dev->features & real_dev->vlan_features;
	dev->hw_features = NETIF_F_ALL_CSUM | NETIF_F_SG |
			   NETIF_F_FRAGLIST | NETIF_F_GSO_SOFTWARE |
			   NETIF_F_HIGHDMA | NETIF_F_SCTP_CSUM |
			   NETIF_F_ALL_FCOE;

	dev->features |= dev->hw_features | NETIF_F_LLTX;
	dev->gso_max_size = real_dev->gso_max_size;
	if (dev->features & NETIF_F_VLAN_FEATURES)
		netdev_warn(real_dev, "VLAN features are set incorrectly.  Q-in-Q configurations may not work correctly.\n");

	dev->vlan_features = real_dev->vlan_features & ~NETIF_F_ALL_FCOE;

	/* ipv6 shared card related stuff */
	dev->dev_id = real_dev->dev_id;

	if (is_zero_ether_addr(dev->dev_addr))
		memcpy(dev->dev_addr, real_dev->dev_addr, dev->addr_len);
	if (is_zero_ether_addr(dev->broadcast))
		memcpy(dev->broadcast, real_dev->broadcast, dev->addr_len);

	if (real_dev->features & NETIF_F_HW_VLAN_TX) {
		dev->header_ops      = real_dev->header_ops;
		dev->hard_header_len = real_dev->hard_header_len;
		dev->hard_start_xmit = vlan_dev_hwaccel_hard_start_xmit;
	} else {
		dev->header_ops      = &vlan_header_ops;
		dev->hard_header_len = real_dev->hard_header_len + VLAN_HLEN;
		dev->hard_start_xmit = vlan_dev_hard_start_xmit;
	}

	if (is_vlan_dev(real_dev))
		subclass = 1;

	vlan_dev_set_lockdep_class(dev, subclass);
		eth_hw_addr_inherit(dev, real_dev);
	if (is_zero_ether_addr(dev->dev_addr)) {
		ether_addr_copy(dev->dev_addr, real_dev->dev_addr);
		dev->addr_assign_type = NET_ADDR_STOLEN;
	}
	if (is_zero_ether_addr(dev->broadcast))
		memcpy(dev->broadcast, real_dev->broadcast, dev->addr_len);

#if IS_ENABLED(CONFIG_FCOE)
	dev->fcoe_ddp_xid = real_dev->fcoe_ddp_xid;
#endif

	dev->needed_headroom = real_dev->needed_headroom;
	if (vlan_hw_offload_capable(real_dev->features,
				    vlan_dev_priv(dev)->vlan_proto)) {
		dev->header_ops      = &vlan_passthru_header_ops;
		dev->hard_header_len = real_dev->hard_header_len;
	} else {
		dev->header_ops      = &vlan_header_ops;
		dev->hard_header_len = real_dev->hard_header_len + VLAN_HLEN;
	}

	dev->netdev_ops = &vlan_netdev_ops;

	SET_NETDEV_DEVTYPE(dev, &vlan_type);

	vlan_dev_set_lockdep_class(dev, vlan_dev_get_lock_subclass(dev));

	vlan_dev_priv(dev)->vlan_pcpu_stats = netdev_alloc_pcpu_stats(struct vlan_pcpu_stats);
	if (!vlan_dev_priv(dev)->vlan_pcpu_stats)
		return -ENOMEM;

	return 0;
}

/* Note: this function might be called multiple times for the same device. */
void vlan_dev_uninit(struct net_device *dev)
{
	struct vlan_priority_tci_mapping *pm;
	struct vlan_dev_info *vlan = vlan_dev_info(dev);
	struct vlan_dev_priv *vlan = vlan_dev_priv(dev);
	int i;

	for (i = 0; i < ARRAY_SIZE(vlan->egress_priority_map); i++) {
		while ((pm = vlan->egress_priority_map[i]) != NULL) {
			vlan->egress_priority_map[i] = pm->next;
			kfree(pm);
		}
	}
}

static u32 vlan_ethtool_get_rx_csum(struct net_device *dev)
{
	const struct vlan_dev_info *vlan = vlan_dev_info(dev);
	struct net_device *real_dev = vlan->real_dev;

	if (real_dev->ethtool_ops == NULL ||
	    real_dev->ethtool_ops->get_rx_csum == NULL)
		return 0;
	return real_dev->ethtool_ops->get_rx_csum(real_dev);
}

static u32 vlan_ethtool_get_flags(struct net_device *dev)
{
	const struct vlan_dev_info *vlan = vlan_dev_info(dev);
	struct net_device *real_dev = vlan->real_dev;

	if (!(real_dev->features & NETIF_F_HW_VLAN_RX) ||
	    real_dev->ethtool_ops == NULL ||
	    real_dev->ethtool_ops->get_flags == NULL)
		return 0;
	return real_dev->ethtool_ops->get_flags(real_dev);
}

static const struct ethtool_ops vlan_ethtool_ops = {
	.get_link		= ethtool_op_get_link,
	.get_rx_csum		= vlan_ethtool_get_rx_csum,
	.get_flags		= vlan_ethtool_get_flags,
};

static netdev_features_t vlan_dev_fix_features(struct net_device *dev,
	netdev_features_t features)
{
	struct net_device *real_dev = vlan_dev_priv(dev)->real_dev;
	netdev_features_t old_features = features;

	features = netdev_intersect_features(features, real_dev->vlan_features);
	features |= NETIF_F_RXCSUM;
	features = netdev_intersect_features(features, real_dev->features);

	features |= old_features & (NETIF_F_SOFT_FEATURES | NETIF_F_GSO_SOFTWARE);
	features |= NETIF_F_LLTX;

	return features;
}

static int vlan_ethtool_get_settings(struct net_device *dev,
				     struct ethtool_cmd *cmd)
{
	const struct vlan_dev_priv *vlan = vlan_dev_priv(dev);

	return __ethtool_get_settings(vlan->real_dev, cmd);
}

static void vlan_ethtool_get_drvinfo(struct net_device *dev,
				     struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, vlan_fullname, sizeof(info->driver));
	strlcpy(info->version, vlan_version, sizeof(info->version));
	strlcpy(info->fw_version, "N/A", sizeof(info->fw_version));
}

static int vlan_ethtool_get_ts_info(struct net_device *dev,
				    struct ethtool_ts_info *info)
{
	const struct vlan_dev_priv *vlan = vlan_dev_priv(dev);
	const struct ethtool_ops *ops = vlan->real_dev->ethtool_ops;
	struct phy_device *phydev = vlan->real_dev->phydev;

	if (phydev && phydev->drv && phydev->drv->ts_info) {
		 return phydev->drv->ts_info(phydev, info);
	} else if (ops->get_ts_info) {
		return ops->get_ts_info(vlan->real_dev, info);
	} else {
		info->so_timestamping = SOF_TIMESTAMPING_RX_SOFTWARE |
			SOF_TIMESTAMPING_SOFTWARE;
		info->phc_index = -1;
	}

	return 0;
}

static struct rtnl_link_stats64 *vlan_dev_get_stats64(struct net_device *dev, struct rtnl_link_stats64 *stats)
{
	struct vlan_pcpu_stats *p;
	u32 rx_errors = 0, tx_dropped = 0;
	int i;

	for_each_possible_cpu(i) {
		u64 rxpackets, rxbytes, rxmulticast, txpackets, txbytes;
		unsigned int start;

		p = per_cpu_ptr(vlan_dev_priv(dev)->vlan_pcpu_stats, i);
		do {
			start = u64_stats_fetch_begin_irq(&p->syncp);
			rxpackets	= p->rx_packets;
			rxbytes		= p->rx_bytes;
			rxmulticast	= p->rx_multicast;
			txpackets	= p->tx_packets;
			txbytes		= p->tx_bytes;
		} while (u64_stats_fetch_retry_irq(&p->syncp, start));

		stats->rx_packets	+= rxpackets;
		stats->rx_bytes		+= rxbytes;
		stats->multicast	+= rxmulticast;
		stats->tx_packets	+= txpackets;
		stats->tx_bytes		+= txbytes;
		/* rx_errors & tx_dropped are u32 */
		rx_errors	+= p->rx_errors;
		tx_dropped	+= p->tx_dropped;
	}
	stats->rx_errors  = rx_errors;
	stats->tx_dropped = tx_dropped;

	return stats;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void vlan_dev_poll_controller(struct net_device *dev)
{
	return;
}

static int vlan_dev_netpoll_setup(struct net_device *dev, struct netpoll_info *npinfo)
{
	struct vlan_dev_priv *vlan = vlan_dev_priv(dev);
	struct net_device *real_dev = vlan->real_dev;
	struct netpoll *netpoll;
	int err = 0;

	netpoll = kzalloc(sizeof(*netpoll), GFP_KERNEL);
	err = -ENOMEM;
	if (!netpoll)
		goto out;

	err = __netpoll_setup(netpoll, real_dev);
	if (err) {
		kfree(netpoll);
		goto out;
	}

	vlan->netpoll = netpoll;

out:
	return err;
}

static void vlan_dev_netpoll_cleanup(struct net_device *dev)
{
	struct vlan_dev_priv *vlan= vlan_dev_priv(dev);
	struct netpoll *netpoll = vlan->netpoll;

	if (!netpoll)
		return;

	vlan->netpoll = NULL;

	__netpoll_free_async(netpoll);
}
#endif /* CONFIG_NET_POLL_CONTROLLER */

static int vlan_dev_get_iflink(const struct net_device *dev)
{
	struct net_device *real_dev = vlan_dev_priv(dev)->real_dev;

	return real_dev->ifindex;
}

static const struct ethtool_ops vlan_ethtool_ops = {
	.get_settings	        = vlan_ethtool_get_settings,
	.get_drvinfo	        = vlan_ethtool_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_ts_info		= vlan_ethtool_get_ts_info,
};

static const struct net_device_ops vlan_netdev_ops = {
	.ndo_change_mtu		= vlan_dev_change_mtu,
	.ndo_init		= vlan_dev_init,
	.ndo_uninit		= vlan_dev_uninit,
	.ndo_open		= vlan_dev_open,
	.ndo_stop		= vlan_dev_stop,
	.ndo_start_xmit =  vlan_dev_hard_start_xmit,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= vlan_dev_set_mac_address,
	.ndo_set_rx_mode	= vlan_dev_set_rx_mode,
	.ndo_change_rx_flags	= vlan_dev_change_rx_flags,
	.ndo_do_ioctl		= vlan_dev_ioctl,
	.ndo_neigh_setup	= vlan_dev_neigh_setup,
	.ndo_get_stats64	= vlan_dev_get_stats64,
#if IS_ENABLED(CONFIG_FCOE)
	.ndo_fcoe_ddp_setup	= vlan_dev_fcoe_ddp_setup,
	.ndo_fcoe_ddp_done	= vlan_dev_fcoe_ddp_done,
	.ndo_fcoe_enable	= vlan_dev_fcoe_enable,
	.ndo_fcoe_disable	= vlan_dev_fcoe_disable,
	.ndo_fcoe_get_wwn	= vlan_dev_fcoe_get_wwn,
	.ndo_fcoe_ddp_target	= vlan_dev_fcoe_ddp_target,
#endif
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= vlan_dev_poll_controller,
	.ndo_netpoll_setup	= vlan_dev_netpoll_setup,
	.ndo_netpoll_cleanup	= vlan_dev_netpoll_cleanup,
#endif
	.ndo_fix_features	= vlan_dev_fix_features,
	.ndo_get_lock_subclass  = vlan_dev_get_lock_subclass,
	.ndo_get_iflink		= vlan_dev_get_iflink,
};

static void vlan_dev_free(struct net_device *dev)
{
	struct vlan_dev_priv *vlan = vlan_dev_priv(dev);

	free_percpu(vlan->vlan_pcpu_stats);
	vlan->vlan_pcpu_stats = NULL;
	free_netdev(dev);
}

void vlan_setup(struct net_device *dev)
{
	ether_setup(dev);

	dev->priv_flags		|= IFF_802_1Q_VLAN;
	dev->tx_queue_len	= 0;

	dev->change_mtu		= vlan_dev_change_mtu;
	dev->init		= vlan_dev_init;
	dev->uninit		= vlan_dev_uninit;
	dev->open		= vlan_dev_open;
	dev->stop		= vlan_dev_stop;
	dev->set_mac_address	= vlan_dev_set_mac_address;
	dev->set_rx_mode	= vlan_dev_set_rx_mode;
	dev->set_multicast_list	= vlan_dev_set_rx_mode;
	dev->change_rx_flags	= vlan_dev_change_rx_flags;
	dev->do_ioctl		= vlan_dev_ioctl;
	dev->destructor		= free_netdev;
	dev->ethtool_ops	= &vlan_ethtool_ops;

	memset(dev->broadcast, 0, ETH_ALEN);
	dev->priv_flags		|= IFF_802_1Q_VLAN | IFF_NO_QUEUE;
	dev->priv_flags		&= ~IFF_TX_SKB_SHARING;
	netif_keep_dst(dev);

	dev->netdev_ops		= &vlan_netdev_ops;
	dev->destructor		= vlan_dev_free;
	dev->ethtool_ops	= &vlan_ethtool_ops;

	eth_zero_addr(dev->broadcast);
}
