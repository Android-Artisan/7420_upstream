/*
 * net/tipc/node.h: Include file for TIPC node management routines
 *
 * Copyright (c) 2000-2006, Ericsson AB
 * Copyright (c) 2005, Wind River Systems
 * Copyright (c) 2000-2006, 2014-2015, Ericsson AB
 * Copyright (c) 2005, 2010-2014, Wind River Systems
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

#ifndef _TIPC_NODE_H
#define _TIPC_NODE_H

#include "node_subscr.h"
#include "addr.h"
#include "cluster.h"
#include "bearer.h"
#include "addr.h"
#include "net.h"
#include "bearer.h"
#include "msg.h"

/* Out-of-range value for node signature */
#define INVALID_NODE_SIG	0x10000

#define INVALID_BEARER_ID -1

/* Flags used to take different actions according to flag type
 * TIPC_NOTIFY_NODE_DOWN: notify node is down
 * TIPC_NOTIFY_NODE_UP: notify node is up
 * TIPC_DISTRIBUTE_NAME: publish or withdraw link state name type
 */
enum {
	TIPC_NOTIFY_NODE_DOWN		= (1 << 3),
	TIPC_NOTIFY_NODE_UP		= (1 << 4),
	TIPC_NOTIFY_LINK_UP		= (1 << 6),
	TIPC_NOTIFY_LINK_DOWN		= (1 << 7)
};

/* Optional capabilities supported by this code version
 */
enum {
	TIPC_BCAST_SYNCH = (1 << 1)
};

#define TIPC_NODE_CAPABILITIES TIPC_BCAST_SYNCH

struct tipc_link_entry {
	struct tipc_link *link;
	u32 mtu;
	struct sk_buff_head inputq;
	struct tipc_media_addr maddr;
};

struct tipc_bclink_entry {
	struct tipc_link *link;
	struct sk_buff_head inputq1;
	struct sk_buff_head arrvq;
	struct sk_buff_head inputq2;
	struct sk_buff_head namedq;
};

/**
 * struct tipc_node - TIPC node structure
 * @addr: network address of node
 * @lock: spinlock governing access to structure
 * @owner: pointer to cluster that node belongs to
 * @next: pointer to next node in sorted list of cluster's nodes
 * @nsub: list of "node down" subscriptions monitoring node
 * @active_links: pointers to active links to node
 * @links: pointers to all links to node
 * @working_links: number of working links to node (both active and standby)
 * @link_cnt: number of links to node
 * @permit_changeover: non-zero if node has redundant links to this system
 * @routers: bitmap (used for multicluster communication)
 * @last_router: (used for multicluster communication)
 * @bclink: broadcast-related info
 *    @supported: non-zero if node supports TIPC b'cast capability
 *    @acked: sequence # of last outbound b'cast message acknowledged by node
 *    @last_in: sequence # of last in-sequence b'cast message received from node
 *    @gap_after: sequence # of last message not requiring a NAK request
 *    @gap_to: sequence # of last message requiring a NAK request
 *    @nack_sync: counter that determines when NAK requests should be sent
 *    @deferred_head: oldest OOS b'cast message received from node
 *    @deferred_tail: newest OOS b'cast message received from node
 *    @defragm: list of partially reassembled b'cast message fragments from node
 */

struct tipc_node {
	u32 addr;
	spinlock_t lock;
	struct cluster *owner;
	struct tipc_node *next;
	struct list_head nsub;
	struct link *active_links[2];
	struct link *links[MAX_BEARERS];
	int link_cnt;
	int working_links;
	int permit_changeover;
	u32 routers[512/32];
	int last_router;
	struct {
		int supported;
		u32 acked;
		u32 last_in;
		u32 gap_after;
		u32 gap_to;
		u32 nack_sync;
		struct sk_buff *deferred_head;
		struct sk_buff *deferred_tail;
		struct sk_buff *defragm;
	} bclink;
};

extern struct tipc_node *tipc_nodes;
extern u32 tipc_own_tag;

struct tipc_node *tipc_node_create(u32 addr);
void tipc_node_delete(struct tipc_node *n_ptr);
struct tipc_node *tipc_node_attach_link(struct link *l_ptr);
void tipc_node_detach_link(struct tipc_node *n_ptr, struct link *l_ptr);
void tipc_node_link_down(struct tipc_node *n_ptr, struct link *l_ptr);
void tipc_node_link_up(struct tipc_node *n_ptr, struct link *l_ptr);
int tipc_node_has_active_links(struct tipc_node *n_ptr);
int tipc_node_has_redundant_links(struct tipc_node *n_ptr);
u32 tipc_node_select_router(struct tipc_node *n_ptr, u32 ref);
struct tipc_node *tipc_node_select_next_hop(u32 addr, u32 selector);
int tipc_node_is_up(struct tipc_node *n_ptr);
void tipc_node_add_router(struct tipc_node *n_ptr, u32 router);
void tipc_node_remove_router(struct tipc_node *n_ptr, u32 router);
struct sk_buff *tipc_node_get_links(const void *req_tlv_area, int req_tlv_space);
struct sk_buff *tipc_node_get_nodes(const void *req_tlv_area, int req_tlv_space);

static inline struct tipc_node *tipc_node_find(u32 addr)
{
	if (likely(in_own_cluster(addr)))
		return tipc_local_nodes[tipc_node(addr)];
	else if (tipc_addr_domain_valid(addr)) {
		struct cluster *c_ptr = tipc_cltr_find(addr);

		if (c_ptr)
			return c_ptr->nodes[tipc_node(addr)];
	}
	return NULL;
}

static inline struct tipc_node *tipc_node_select(u32 addr, u32 selector)
{
	if (likely(in_own_cluster(addr)))
		return tipc_local_nodes[tipc_node(addr)];
	return tipc_node_select_next_hop(addr, selector);
}

static inline void tipc_node_lock(struct tipc_node *n_ptr)
{
	spin_lock_bh(&n_ptr->lock);
}

static inline void tipc_node_unlock(struct tipc_node *n_ptr)
{
	spin_unlock_bh(&n_ptr->lock);
 * @ref: reference counter to node object
 * @lock: spinlock governing access to structure
 * @net: the applicable net namespace
 * @hash: links to adjacent nodes in unsorted hash chain
 * @inputq: pointer to input queue containing messages for msg event
 * @namedq: pointer to name table input queue with name table messages
 * @active_links: bearer ids of active links, used as index into links[] array
 * @links: array containing references to all links to node
 * @action_flags: bit mask of different types of node actions
 * @state: connectivity state vs peer node
 * @sync_point: sequence number where synch/failover is finished
 * @list: links to adjacent nodes in sorted list of cluster's nodes
 * @working_links: number of working links to node (both active and standby)
 * @link_cnt: number of links to node
 * @capabilities: bitmap, indicating peer node's functional capabilities
 * @signature: node instance identifier
 * @link_id: local and remote bearer ids of changing link, if any
 * @publ_list: list of publications
 * @rcu: rcu struct for tipc_node
 */
struct tipc_node {
	u32 addr;
	struct kref kref;
	spinlock_t lock;
	struct net *net;
	struct hlist_node hash;
	int active_links[2];
	struct tipc_link_entry links[MAX_BEARERS];
	struct tipc_bclink_entry bc_entry;
	int action_flags;
	struct list_head list;
	int state;
	u16 sync_point;
	int link_cnt;
	u16 working_links;
	u16 capabilities;
	u32 signature;
	u32 link_id;
	struct list_head publ_list;
	struct list_head conn_sks;
	unsigned long keepalive_intv;
	struct timer_list timer;
	struct rcu_head rcu;
};

struct tipc_node *tipc_node_find(struct net *net, u32 addr);
void tipc_node_put(struct tipc_node *node);
void tipc_node_stop(struct net *net);
void tipc_node_check_dest(struct net *net, u32 onode,
			  struct tipc_bearer *bearer,
			  u16 capabilities, u32 signature,
			  struct tipc_media_addr *maddr,
			  bool *respond, bool *dupl_addr);
void tipc_node_delete_links(struct net *net, int bearer_id);
void tipc_node_attach_link(struct tipc_node *n_ptr, struct tipc_link *l_ptr);
void tipc_node_detach_link(struct tipc_node *n_ptr, struct tipc_link *l_ptr);
bool tipc_node_is_up(struct tipc_node *n);
int tipc_node_get_linkname(struct net *net, u32 bearer_id, u32 node,
			   char *linkname, size_t len);
void tipc_node_unlock(struct tipc_node *node);
int tipc_node_xmit(struct net *net, struct sk_buff_head *list, u32 dnode,
		   int selector);
int tipc_node_xmit_skb(struct net *net, struct sk_buff *skb, u32 dest,
		       u32 selector);
int tipc_node_add_conn(struct net *net, u32 dnode, u32 port, u32 peer_port);
void tipc_node_remove_conn(struct net *net, u32 dnode, u32 port);
int tipc_nl_node_dump(struct sk_buff *skb, struct netlink_callback *cb);

static inline void tipc_node_lock(struct tipc_node *node)
{
	spin_lock_bh(&node->lock);
}

static inline struct tipc_link *node_active_link(struct tipc_node *n, int sel)
{
	int bearer_id = n->active_links[sel & 1];

	if (unlikely(bearer_id == INVALID_BEARER_ID))
		return NULL;

	return n->links[bearer_id].link;
}

static inline unsigned int tipc_node_get_mtu(struct net *net, u32 addr, u32 sel)
{
	struct tipc_node *n;
	int bearer_id;
	unsigned int mtu = MAX_MSG_SIZE;

	n = tipc_node_find(net, addr);
	if (unlikely(!n))
		return mtu;

	bearer_id = n->active_links[sel & 1];
	if (likely(bearer_id != INVALID_BEARER_ID))
		mtu = n->links[bearer_id].mtu;
	tipc_node_put(n);
	return mtu;
}

#endif
