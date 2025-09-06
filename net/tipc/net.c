/*
 * net/tipc/net.c: TIPC network routing code
 *
 * Copyright (c) 1995-2006, Ericsson AB
 * Copyright (c) 2005, Wind River Systems
 * Copyright (c) 1995-2006, 2014, Ericsson AB
 * Copyright (c) 2005, 2010-2011, Wind River Systems
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "core.h"
#include "bearer.h"
#include "net.h"
#include "zone.h"
#include "addr.h"
#include "name_table.h"
#include "name_distr.h"
#include "subscr.h"
#include "link.h"
#include "msg.h"
#include "port.h"
#include "bcast.h"
#include "discover.h"
#include "config.h"
#include "net.h"
#include "name_distr.h"
#include "subscr.h"
#include "socket.h"
#include "node.h"
#include "bcast.h"

static const struct nla_policy tipc_nl_net_policy[TIPC_NLA_NET_MAX + 1] = {
	[TIPC_NLA_NET_UNSPEC]	= { .type = NLA_UNSPEC },
	[TIPC_NLA_NET_ID]	= { .type = NLA_U32 },
	[TIPC_NLA_NET_ADDR]	= { .type = NLA_U32 },
};

/*
 * The TIPC locking policy is designed to ensure a very fine locking
 * granularity, permitting complete parallel access to individual
 * port and node/link instances. The code consists of three major
 * locking domains, each protected with their own disjunct set of locks.
 *
 * 1: The routing hierarchy.
 *    Comprises the structures 'zone', 'cluster', 'node', 'link'
 *    and 'bearer'. The whole hierarchy is protected by a big
 *    read/write lock, tipc_net_lock, to enssure that nothing is added
 *    or removed while code is accessing any of these structures.
 *    This layer must not be called from the two others while they
 *    hold any of their own locks.
 *    Neither must it itself do any upcalls to the other two before
 *    it has released tipc_net_lock and other protective locks.
 *
 *   Within the tipc_net_lock domain there are two sub-domains;'node' and
 *   'bearer', where local write operations are permitted,
 *   provided that those are protected by individual spin_locks
 *   per instance. Code holding tipc_net_lock(read) and a node spin_lock
 *   is permitted to poke around in both the node itself and its
 *   subordinate links. I.e, it can update link counters and queues,
 *   change link state, send protocol messages, and alter the
 *   "active_links" array in the node; but it can _not_ remove a link
 *   or a node from the overall structure.
 *   Correspondingly, individual bearers may change status within a
 *   tipc_net_lock(read), protected by an individual spin_lock ber bearer
 *   instance, but it needs tipc_net_lock(write) to remove/add any bearers.
 *
 *
 *  2: The transport level of the protocol.
 *     This consists of the structures port, (and its user level
 *     representations, such as user_port and tipc_sock), reference and
 *     tipc_user (port.c, reg.c, socket.c).
 *
 *     This layer has four different locks:
 * port and node/link instances. The code consists of four major
 * locking domains, each protected with their own disjunct set of locks.
 *
 * 1: The bearer level.
 *    RTNL lock is used to serialize the process of configuring bearer
 *    on update side, and RCU lock is applied on read side to make
 *    bearer instance valid on both paths of message transmission and
 *    reception.
 *
 * 2: The node and link level.
 *    All node instances are saved into two tipc_node_list and node_htable
 *    lists. The two lists are protected by node_list_lock on write side,
 *    and they are guarded with RCU lock on read side. Especially node
 *    instance is destroyed only when TIPC module is removed, and we can
 *    confirm that there has no any user who is accessing the node at the
 *    moment. Therefore, Except for iterating the two lists within RCU
 *    protection, it's no needed to hold RCU that we access node instance
 *    in other places.
 *
 *    In addition, all members in node structure including link instances
 *    are protected by node spin lock.
 *
 * 3: The transport level of the protocol.
 *    This consists of the structures port, (and its user level
 *    representations, such as user_port and tipc_sock), reference and
 *    tipc_user (port.c, reg.c, socket.c).
 *
 *    This layer has four different locks:
 *     - The tipc_port spin_lock. This is protecting each port instance
 *       from parallel data access and removal. Since we can not place
 *       this lock in the port itself, it has been placed in the
 *       corresponding reference table entry, which has the same life
 *       cycle as the module. This entry is difficult to access from
 *       outside the TIPC core, however, so a pointer to the lock has
 *       been added in the port instance, -to be used for unlocking
 *       only.
 *     - A read/write lock to protect the reference table itself (teg.c).
 *       (Nobody is using read-only access to this, so it can just as
 *       well be changed to a spin_lock)
 *     - A spin lock to protect the registry of kernel/driver users (reg.c)
 *     - A global spin_lock (tipc_port_lock), which only task is to ensure
 *       consistency where more than one port is involved in an operation,
 *       i.e., whe a port is part of a linked list of ports.
 *       There are two such lists; 'port_list', which is used for management,
 *       and 'wait_list', which is used to queue ports during congestion.
 *
 *  3: The name table (name_table.c, name_distr.c, subscription.c)
 *  4: The name table (name_table.c, name_distr.c, subscription.c)
 *     - There is one big read/write-lock (tipc_nametbl_lock) protecting the
 *       overall name table structure. Nothing must be added/removed to
 *       this structure without holding write access to it.
 *     - There is one local spin_lock per sub_sequence, which can be seen
 *       as a sub-domain to the tipc_nametbl_lock domain. It is used only
 *       for translation operations, and is needed because a translation
 *       steps the root of the 'publication' linked list between each lookup.
 *       This is always used within the scope of a tipc_nametbl_lock(read).
 *     - A local spin_lock protecting the queue of subscriber events.
*/

DEFINE_RWLOCK(tipc_net_lock);
struct network tipc_net = { NULL };

struct tipc_node *tipc_net_select_remote_node(u32 addr, u32 ref)
{
	return tipc_zone_select_remote_node(tipc_net.zones[tipc_zone(addr)], addr, ref);
}

u32 tipc_net_select_router(u32 addr, u32 ref)
{
	return tipc_zone_select_router(tipc_net.zones[tipc_zone(addr)], addr, ref);
}

#if 0
u32 tipc_net_next_node(u32 a)
{
	if (tipc_net.zones[tipc_zone(a)])
		return tipc_zone_next_node(a);
	return 0;
}
#endif

void tipc_net_remove_as_router(u32 router)
{
	u32 z_num;

	for (z_num = 1; z_num <= tipc_max_zones; z_num++) {
		if (!tipc_net.zones[z_num])
			continue;
		tipc_zone_remove_as_router(tipc_net.zones[z_num], router);
	}
}

void tipc_net_send_external_routes(u32 dest)
{
	u32 z_num;

	for (z_num = 1; z_num <= tipc_max_zones; z_num++) {
		if (tipc_net.zones[z_num])
			tipc_zone_send_external_routes(tipc_net.zones[z_num], dest);
	}
}

static int net_init(void)
{
	memset(&tipc_net, 0, sizeof(tipc_net));
	tipc_net.zones = kcalloc(tipc_max_zones + 1, sizeof(struct _zone *), GFP_ATOMIC);
	if (!tipc_net.zones) {
		return -ENOMEM;
	}
	return 0;
}

static void net_stop(void)
{
	u32 z_num;

	if (!tipc_net.zones)
		return;

	for (z_num = 1; z_num <= tipc_max_zones; z_num++) {
		tipc_zone_delete(tipc_net.zones[z_num]);
	}
	kfree(tipc_net.zones);
	tipc_net.zones = NULL;
}

static void net_route_named_msg(struct sk_buff *buf)
{
	struct tipc_msg *msg = buf_msg(buf);
	u32 dnode;
	u32 dport;

	if (!msg_named(msg)) {
		msg_dbg(msg, "tipc_net->drop_nam:");
		buf_discard(buf);
		return;
	}

	dnode = addr_domain(msg_lookup_scope(msg));
	dport = tipc_nametbl_translate(msg_nametype(msg), msg_nameinst(msg), &dnode);
	dbg("tipc_net->lookup<%u,%u>-><%u,%x>\n",
	    msg_nametype(msg), msg_nameinst(msg), dport, dnode);
	if (dport) {
		msg_set_destnode(msg, dnode);
		msg_set_destport(msg, dport);
		tipc_net_route_msg(buf);
		return;
	}
	msg_dbg(msg, "tipc_net->rej:NO NAME: ");
	tipc_reject_msg(buf, TIPC_ERR_NO_NAME);
}

void tipc_net_route_msg(struct sk_buff *buf)
{
	struct tipc_msg *msg;
	u32 dnode;

	if (!buf)
		return;
	msg = buf_msg(buf);

	msg_incr_reroute_cnt(msg);
	if (msg_reroute_cnt(msg) > 6) {
		if (msg_errcode(msg)) {
			msg_dbg(msg, "NET>DISC>:");
			buf_discard(buf);
		} else {
			msg_dbg(msg, "NET>REJ>:");
			tipc_reject_msg(buf, msg_destport(msg) ?
					TIPC_ERR_NO_PORT : TIPC_ERR_NO_NAME);
		}
		return;
	}

	msg_dbg(msg, "tipc_net->rout: ");

	/* Handle message for this node */
	dnode = msg_short(msg) ? tipc_own_addr : msg_destnode(msg);
	if (in_scope(dnode, tipc_own_addr)) {
		if (msg_isdata(msg)) {
			if (msg_mcast(msg))
				tipc_port_recv_mcast(buf, NULL);
			else if (msg_destport(msg))
				tipc_port_recv_msg(buf);
			else
				net_route_named_msg(buf);
			return;
		}
		switch (msg_user(msg)) {
		case ROUTE_DISTRIBUTOR:
			tipc_cltr_recv_routing_table(buf);
			break;
		case NAME_DISTRIBUTOR:
			tipc_named_recv(buf);
			break;
		case CONN_MANAGER:
			tipc_port_recv_proto_msg(buf);
			break;
		default:
			msg_dbg(msg,"DROP/NET/<REC<");
			buf_discard(buf);
		}
		return;
	}

	/* Handle message for another node */
	msg_dbg(msg, "NET>SEND>: ");
	tipc_link_send(buf, dnode, msg_link_selector(msg));
}

int tipc_net_start(u32 addr)
{
	char addr_string[16];
	int res;

	if (tipc_mode != TIPC_NODE_MODE)
		return -ENOPROTOOPT;

	tipc_subscr_stop();
	tipc_cfg_stop();

	tipc_own_addr = addr;
	tipc_mode = TIPC_NET_MODE;
	tipc_named_reinit();
	tipc_port_reinit();

	if ((res = tipc_bearer_init()) ||
	    (res = net_init()) ||
	    (res = tipc_cltr_init()) ||
	    (res = tipc_bclink_init())) {
		return res;
	}

	tipc_k_signal((Handler)tipc_subscr_start, 0);
	tipc_k_signal((Handler)tipc_cfg_init, 0);

	info("Started in network mode\n");
	info("Own node address %s, network identity %u\n",
	     addr_string_fill(addr_string, tipc_own_addr), tipc_net_id);
	return 0;
}

void tipc_net_stop(void)
{
	if (tipc_mode != TIPC_NET_MODE)
		return;
	write_lock_bh(&tipc_net_lock);
	tipc_bearer_stop();
	tipc_mode = TIPC_NODE_MODE;
	tipc_bclink_stop();
	net_stop();
	write_unlock_bh(&tipc_net_lock);
	info("Left network mode \n");
}

int tipc_net_start(struct net *net, u32 addr)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	char addr_string[16];

	tn->own_addr = addr;
	tipc_named_reinit(net);
	tipc_sk_reinit(net);
	tipc_bcast_reinit(net);

	tipc_nametbl_publish(net, TIPC_CFG_SRV, tn->own_addr, tn->own_addr,
			     TIPC_ZONE_SCOPE, 0, tn->own_addr);

	pr_info("Started in network mode\n");
	pr_info("Own node address %s, network identity %u\n",
		tipc_addr_string_fill(addr_string, tn->own_addr),
		tn->net_id);
	return 0;
}

void tipc_net_stop(struct net *net)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);

	if (!tn->own_addr)
		return;

	tipc_nametbl_withdraw(net, TIPC_CFG_SRV, tn->own_addr, 0,
			      tn->own_addr);
	rtnl_lock();
	tipc_bearer_stop(net);
	tipc_node_stop(net);
	rtnl_unlock();

	pr_info("Left network mode\n");
}

static int __tipc_nl_add_net(struct net *net, struct tipc_nl_msg *msg)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	void *hdr;
	struct nlattr *attrs;

	hdr = genlmsg_put(msg->skb, msg->portid, msg->seq, &tipc_genl_family,
			  NLM_F_MULTI, TIPC_NL_NET_GET);
	if (!hdr)
		return -EMSGSIZE;

	attrs = nla_nest_start(msg->skb, TIPC_NLA_NET);
	if (!attrs)
		goto msg_full;

	if (nla_put_u32(msg->skb, TIPC_NLA_NET_ID, tn->net_id))
		goto attr_msg_full;

	nla_nest_end(msg->skb, attrs);
	genlmsg_end(msg->skb, hdr);

	return 0;

attr_msg_full:
	nla_nest_cancel(msg->skb, attrs);
msg_full:
	genlmsg_cancel(msg->skb, hdr);

	return -EMSGSIZE;
}

int tipc_nl_net_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	int err;
	int done = cb->args[0];
	struct tipc_nl_msg msg;

	if (done)
		return 0;

	msg.skb = skb;
	msg.portid = NETLINK_CB(cb->skb).portid;
	msg.seq = cb->nlh->nlmsg_seq;

	err = __tipc_nl_add_net(net, &msg);
	if (err)
		goto out;

	done = 1;
out:
	cb->args[0] = done;

	return skb->len;
}

int tipc_nl_net_set(struct sk_buff *skb, struct genl_info *info)
{
	struct net *net = sock_net(skb->sk);
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct nlattr *attrs[TIPC_NLA_NET_MAX + 1];
	int err;

	if (!info->attrs[TIPC_NLA_NET])
		return -EINVAL;

	err = nla_parse_nested(attrs, TIPC_NLA_NET_MAX,
			       info->attrs[TIPC_NLA_NET],
			       tipc_nl_net_policy);
	if (err)
		return err;

	if (attrs[TIPC_NLA_NET_ID]) {
		u32 val;

		/* Can't change net id once TIPC has joined a network */
		if (tn->own_addr)
			return -EPERM;

		val = nla_get_u32(attrs[TIPC_NLA_NET_ID]);
		if (val < 1 || val > 9999)
			return -EINVAL;

		tn->net_id = val;
	}

	if (attrs[TIPC_NLA_NET_ADDR]) {
		u32 addr;

		/* Can't change net addr once TIPC has joined a network */
		if (tn->own_addr)
			return -EPERM;

		addr = nla_get_u32(attrs[TIPC_NLA_NET_ADDR]);
		if (!tipc_addr_node_valid(addr))
			return -EINVAL;

		rtnl_lock();
		tipc_net_start(net, addr);
		rtnl_unlock();
	}

	return 0;
}
