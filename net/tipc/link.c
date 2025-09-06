/*
 * net/tipc/link.c: TIPC link code
 *
 * Copyright (c) 1996-2007, Ericsson AB
 * Copyright (c) 2004-2007, Wind River Systems
 * Copyright (c) 1996-2007, 2012-2015, Ericsson AB
 * Copyright (c) 2004-2007, 2010-2013, Wind River Systems
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
#include "dbg.h"
#include "link.h"
#include "net.h"
#include "node.h"
#include "port.h"
#include "addr.h"
#include "node_subscr.h"
#include "name_distr.h"
#include "bearer.h"
#include "name_table.h"
#include "discover.h"
#include "config.h"
#include "bcast.h"


/*
 * Out-of-range value for link session numbers
 */

#define INVALID_SESSION 0x10000

/*
 * Limit for deferred reception queue:
 */

#define DEF_QUEUE_LIMIT 256u

/*
 * Link state events:
 */

#define  STARTING_EVT    856384768	/* link processing trigger */
#define  TRAFFIC_MSG_EVT 560815u	/* rx'd ??? */
#define  TIMEOUT_EVT     560817u	/* link timer expired */

/*
 * The following two 'message types' is really just implementation
 * data conveniently stored in the message header.
 * They must not be considered part of the protocol
 */
#define OPEN_MSG   0
#define CLOSED_MSG 1

/*
 * State value stored in 'exp_msg_count'
 */

#define START_CHANGEOVER 100000u

/**
 * struct link_name - deconstructed link name
 * @addr_local: network address of node at this end
 * @if_local: name of interface at this end
 * @addr_peer: network address of node at far end
 * @if_peer: name of interface at far end
 */

struct link_name {
	u32 addr_local;
	char if_local[TIPC_MAX_IF_NAME];
	u32 addr_peer;
	char if_peer[TIPC_MAX_IF_NAME];
};

#if 0

/* LINK EVENT CODE IS NOT SUPPORTED AT PRESENT */

/**
 * struct link_event - link up/down event notification
 */

struct link_event {
	u32 addr;
	int up;
	void (*fcn)(u32, char *, int);
	char name[TIPC_MAX_LINK_NAME];
};

#endif

static void link_handle_out_of_seq_msg(struct link *l_ptr,
				       struct sk_buff *buf);
static void link_recv_proto_msg(struct link *l_ptr, struct sk_buff *buf);
static int  link_recv_changeover_msg(struct link **l_ptr, struct sk_buff **buf);
static void link_set_supervision_props(struct link *l_ptr, u32 tolerance);
static int  link_send_sections_long(struct port *sender,
				    struct iovec const *msg_sect,
				    u32 num_sect, u32 destnode);
static void link_check_defragm_bufs(struct link *l_ptr);
static void link_state_event(struct link *l_ptr, u32 event);
static void link_reset_statistics(struct link *l_ptr);
static void link_print(struct link *l_ptr, struct print_buf *buf,
		       const char *str);

/*
 * Debugging code used by link routines only
 *
 * When debugging link problems on a system that has multiple links,
 * the standard TIPC debugging routines may not be useful since they
 * allow the output from multiple links to be intermixed.  For this reason
 * routines of the form "dbg_link_XXX()" have been created that will capture
 * debug info into a link's personal print buffer, which can then be dumped
 * into the TIPC system log (TIPC_LOG) upon request.
 *
 * To enable per-link debugging, use LINK_LOG_BUF_SIZE to specify the size
 * of the print buffer used by each link.  If LINK_LOG_BUF_SIZE is set to 0,
 * the dbg_link_XXX() routines simply send their output to the standard
 * debug print buffer (DBG_OUTPUT), if it has been defined; this can be useful
 * when there is only a single link in the system being debugged.
 *
 * Notes:
 * - When enabled, LINK_LOG_BUF_SIZE should be set to at least TIPC_PB_MIN_SIZE
 * - "l_ptr" must be valid when using dbg_link_XXX() macros
 */

#define LINK_LOG_BUF_SIZE 0

#define dbg_link(fmt, arg...) \
	do { \
		if (LINK_LOG_BUF_SIZE) \
			tipc_printf(&l_ptr->print_buf, fmt, ## arg); \
	} while (0)
#define dbg_link_msg(msg, txt) \
	do { \
		if (LINK_LOG_BUF_SIZE) \
			tipc_msg_dbg(&l_ptr->print_buf, msg, txt); \
	} while (0)
#define dbg_link_state(txt) \
	do { \
		if (LINK_LOG_BUF_SIZE) \
			link_print(l_ptr, &l_ptr->print_buf, txt); \
	} while (0)
#define dbg_link_dump() do { \
	if (LINK_LOG_BUF_SIZE) { \
		tipc_printf(LOG, "\n\nDumping link <%s>:\n", l_ptr->name); \
		tipc_printbuf_move(LOG, &l_ptr->print_buf); \
	} \
} while (0)

static void dbg_print_link(struct link *l_ptr, const char *str)
{
	if (DBG_OUTPUT != TIPC_NULL)
		link_print(l_ptr, DBG_OUTPUT, str);
}

static void dbg_print_buf_chain(struct sk_buff *root_buf)
{
	if (DBG_OUTPUT != TIPC_NULL) {
		struct sk_buff *buf = root_buf;

		while (buf) {
			msg_dbg(buf_msg(buf), "In chain: ");
			buf = buf->next;
		}
	}
}

/*
 *  Simple link routines
 */

static unsigned int align(unsigned int i)
{
	return (i + 3) & ~3u;
}

static int link_working_working(struct link *l_ptr)
{
	return (l_ptr->state == WORKING_WORKING);
}

static int link_working_unknown(struct link *l_ptr)
{
	return (l_ptr->state == WORKING_UNKNOWN);
}

static int link_reset_unknown(struct link *l_ptr)
{
	return (l_ptr->state == RESET_UNKNOWN);
}

static int link_reset_reset(struct link *l_ptr)
{
	return (l_ptr->state == RESET_RESET);
}

static int link_blocked(struct link *l_ptr)
{
	return (l_ptr->exp_msg_count || l_ptr->blocked);
}

static int link_congested(struct link *l_ptr)
{
	return (l_ptr->out_queue_size >= l_ptr->queue_limit[0]);
}

static u32 link_max_pkt(struct link *l_ptr)
{
	return l_ptr->max_pkt;
}

static void link_init_max_pkt(struct link *l_ptr)
{
	u32 max_pkt;

	max_pkt = (l_ptr->b_ptr->publ.mtu & ~3);
	if (max_pkt > MAX_MSG_SIZE)
		max_pkt = MAX_MSG_SIZE;

	l_ptr->max_pkt_target = max_pkt;
	if (l_ptr->max_pkt_target < MAX_PKT_DEFAULT)
		l_ptr->max_pkt = l_ptr->max_pkt_target;
	else
		l_ptr->max_pkt = MAX_PKT_DEFAULT;

	l_ptr->max_pkt_probes = 0;
}

static u32 link_next_sent(struct link *l_ptr)
{
	if (l_ptr->next_out)
		return msg_seqno(buf_msg(l_ptr->next_out));
	return mod(l_ptr->next_out_no);
}

static u32 link_last_sent(struct link *l_ptr)
{
	return mod(link_next_sent(l_ptr) - 1);
}
#include "subscr.h"
#include "link.h"
#include "bcast.h"
#include "socket.h"
#include "name_distr.h"
#include "discover.h"
#include "netlink.h"

#include <linux/pkt_sched.h>

/*
 * Error message prefixes
 */
static const char *link_co_err = "Link tunneling error, ";
static const char *link_rst_msg = "Resetting link ";
static const char tipc_bclink_name[] = "broadcast-link";

static const struct nla_policy tipc_nl_link_policy[TIPC_NLA_LINK_MAX + 1] = {
	[TIPC_NLA_LINK_UNSPEC]		= { .type = NLA_UNSPEC },
	[TIPC_NLA_LINK_NAME] = {
		.type = NLA_NUL_STRING,
		.len = TIPC_MAX_LINK_NAME
	},
	[TIPC_NLA_LINK_MTU]		= { .type = NLA_U32 },
	[TIPC_NLA_LINK_BROADCAST]	= { .type = NLA_FLAG },
	[TIPC_NLA_LINK_UP]		= { .type = NLA_FLAG },
	[TIPC_NLA_LINK_ACTIVE]		= { .type = NLA_FLAG },
	[TIPC_NLA_LINK_PROP]		= { .type = NLA_NESTED },
	[TIPC_NLA_LINK_STATS]		= { .type = NLA_NESTED },
	[TIPC_NLA_LINK_RX]		= { .type = NLA_U32 },
	[TIPC_NLA_LINK_TX]		= { .type = NLA_U32 }
};

/* Properties valid for media, bearar and link */
static const struct nla_policy tipc_nl_prop_policy[TIPC_NLA_PROP_MAX + 1] = {
	[TIPC_NLA_PROP_UNSPEC]		= { .type = NLA_UNSPEC },
	[TIPC_NLA_PROP_PRIO]		= { .type = NLA_U32 },
	[TIPC_NLA_PROP_TOL]		= { .type = NLA_U32 },
	[TIPC_NLA_PROP_WIN]		= { .type = NLA_U32 }
};

/* Send states for broadcast NACKs
 */
enum {
	BC_NACK_SND_CONDITIONAL,
	BC_NACK_SND_UNCONDITIONAL,
	BC_NACK_SND_SUPPRESS,
};

/*
 * Interval between NACKs when packets arrive out of order
 */
#define TIPC_NACK_INTV (TIPC_MIN_LINK_WIN * 2)
/*
 * Out-of-range value for link session numbers
 */
#define WILDCARD_SESSION 0x10000

/* Link FSM states:
 */
enum {
	LINK_ESTABLISHED     = 0xe,
	LINK_ESTABLISHING    = 0xe  << 4,
	LINK_RESET           = 0x1  << 8,
	LINK_RESETTING       = 0x2  << 12,
	LINK_PEER_RESET      = 0xd  << 16,
	LINK_FAILINGOVER     = 0xf  << 20,
	LINK_SYNCHING        = 0xc  << 24
};

/* Link FSM state checking routines
 */
static int link_is_up(struct tipc_link *l)
{
	return l->state & (LINK_ESTABLISHED | LINK_SYNCHING);
}

static int tipc_link_proto_rcv(struct tipc_link *l, struct sk_buff *skb,
			       struct sk_buff_head *xmitq);
static void tipc_link_build_proto_msg(struct tipc_link *l, int mtyp, bool probe,
				      u16 rcvgap, int tolerance, int priority,
				      struct sk_buff_head *xmitq);
static void link_reset_statistics(struct tipc_link *l_ptr);
static void link_print(struct tipc_link *l_ptr, const char *str);
static void tipc_link_build_nack_msg(struct tipc_link *l,
				     struct sk_buff_head *xmitq);
static void tipc_link_build_bc_init_msg(struct tipc_link *l,
					struct sk_buff_head *xmitq);
static bool tipc_link_release_pkts(struct tipc_link *l, u16 to);

/*
 *  Simple non-static link routines (i.e. referenced outside this file)
 */

int tipc_link_is_up(struct link *l_ptr)
{
	if (!l_ptr)
		return 0;
	return (link_working_working(l_ptr) || link_working_unknown(l_ptr));
}

int tipc_link_is_active(struct link *l_ptr)
{
	return ((l_ptr->owner->active_links[0] == l_ptr) ||
		(l_ptr->owner->active_links[1] == l_ptr));
}

/**
 * link_name_validate - validate & (optionally) deconstruct link name
 * @name - ptr to link name string
 * @name_parts - ptr to area for link name components (or NULL if not needed)
 *
 * Returns 1 if link name is valid, otherwise 0.
 */

static int link_name_validate(const char *name, struct link_name *name_parts)
{
	char name_copy[TIPC_MAX_LINK_NAME];
	char *addr_local;
	char *if_local;
	char *addr_peer;
	char *if_peer;
	char dummy;
	u32 z_local, c_local, n_local;
	u32 z_peer, c_peer, n_peer;
	u32 if_local_len;
	u32 if_peer_len;

	/* copy link name & ensure length is OK */

	name_copy[TIPC_MAX_LINK_NAME - 1] = 0;
	/* need above in case non-Posix strncpy() doesn't pad with nulls */
	strncpy(name_copy, name, TIPC_MAX_LINK_NAME);
	if (name_copy[TIPC_MAX_LINK_NAME - 1] != 0)
		return 0;

	/* ensure all component parts of link name are present */

	addr_local = name_copy;
	if ((if_local = strchr(addr_local, ':')) == NULL)
		return 0;
	*(if_local++) = 0;
	if ((addr_peer = strchr(if_local, '-')) == NULL)
		return 0;
	*(addr_peer++) = 0;
	if_local_len = addr_peer - if_local;
	if ((if_peer = strchr(addr_peer, ':')) == NULL)
		return 0;
	*(if_peer++) = 0;
	if_peer_len = strlen(if_peer) + 1;

	/* validate component parts of link name */

	if ((sscanf(addr_local, "%u.%u.%u%c",
		    &z_local, &c_local, &n_local, &dummy) != 3) ||
	    (sscanf(addr_peer, "%u.%u.%u%c",
		    &z_peer, &c_peer, &n_peer, &dummy) != 3) ||
	    (z_local > 255) || (c_local > 4095) || (n_local > 4095) ||
	    (z_peer  > 255) || (c_peer  > 4095) || (n_peer  > 4095) ||
	    (if_local_len <= 1) || (if_local_len > TIPC_MAX_IF_NAME) ||
	    (if_peer_len  <= 1) || (if_peer_len  > TIPC_MAX_IF_NAME) ||
	    (strspn(if_local, tipc_alphabet) != (if_local_len - 1)) ||
	    (strspn(if_peer, tipc_alphabet) != (if_peer_len - 1)))
		return 0;

	/* return link name components, if necessary */

	if (name_parts) {
		name_parts->addr_local = tipc_addr(z_local, c_local, n_local);
		strcpy(name_parts->if_local, if_local);
		name_parts->addr_peer = tipc_addr(z_peer, c_peer, n_peer);
		strcpy(name_parts->if_peer, if_peer);
	}
	return 1;
}

/**
 * link_timeout - handle expiration of link timer
 * @l_ptr: pointer to link
 *
 * This routine must not grab "tipc_net_lock" to avoid a potential deadlock conflict
 * with tipc_link_delete().  (There is no risk that the node will be deleted by
 * another thread because tipc_link_delete() always cancels the link timer before
 * tipc_node_delete() is called.)
 */

static void link_timeout(struct link *l_ptr)
{
	tipc_node_lock(l_ptr->owner);

	/* update counters used in statistical profiling of send traffic */

	l_ptr->stats.accu_queue_sz += l_ptr->out_queue_size;
	l_ptr->stats.queue_sz_counts++;

	if (l_ptr->out_queue_size > l_ptr->stats.max_queue_sz)
		l_ptr->stats.max_queue_sz = l_ptr->out_queue_size;

	if (l_ptr->first_out) {
		struct tipc_msg *msg = buf_msg(l_ptr->first_out);
		u32 length = msg_size(msg);

		if ((msg_user(msg) == MSG_FRAGMENTER)
		    && (msg_type(msg) == FIRST_FRAGMENT)) {
			length = msg_size(msg_get_wrapped(msg));
		}
		if (length) {
			l_ptr->stats.msg_lengths_total += length;
			l_ptr->stats.msg_length_counts++;
			if (length <= 64)
				l_ptr->stats.msg_length_profile[0]++;
			else if (length <= 256)
				l_ptr->stats.msg_length_profile[1]++;
			else if (length <= 1024)
				l_ptr->stats.msg_length_profile[2]++;
			else if (length <= 4096)
				l_ptr->stats.msg_length_profile[3]++;
			else if (length <= 16384)
				l_ptr->stats.msg_length_profile[4]++;
			else if (length <= 32768)
				l_ptr->stats.msg_length_profile[5]++;
			else
				l_ptr->stats.msg_length_profile[6]++;
		}
	}

	/* do all other link processing performed on a periodic basis */

	link_check_defragm_bufs(l_ptr);

	link_state_event(l_ptr, TIMEOUT_EVT);

	if (l_ptr->next_out)
		tipc_link_push_queue(l_ptr);

	tipc_node_unlock(l_ptr->owner);
}

static void link_set_timer(struct link *l_ptr, u32 time)
{
	k_start_timer(&l_ptr->timer, time);
bool tipc_link_is_up(struct tipc_link *l)
{
	return link_is_up(l);
}

bool tipc_link_peer_is_down(struct tipc_link *l)
{
	return l->state == LINK_PEER_RESET;
}

bool tipc_link_is_reset(struct tipc_link *l)
{
	return l->state & (LINK_RESET | LINK_FAILINGOVER | LINK_ESTABLISHING);
}

bool tipc_link_is_establishing(struct tipc_link *l)
{
	return l->state == LINK_ESTABLISHING;
}

bool tipc_link_is_synching(struct tipc_link *l)
{
	return l->state == LINK_SYNCHING;
}

bool tipc_link_is_failingover(struct tipc_link *l)
{
	return l->state == LINK_FAILINGOVER;
}

bool tipc_link_is_blocked(struct tipc_link *l)
{
	return l->state & (LINK_RESETTING | LINK_PEER_RESET | LINK_FAILINGOVER);
}

static bool link_is_bc_sndlink(struct tipc_link *l)
{
	return !l->bc_sndlink;
}

static bool link_is_bc_rcvlink(struct tipc_link *l)
{
	return ((l->bc_rcvlink == l) && !link_is_bc_sndlink(l));
}

int tipc_link_is_active(struct tipc_link *l)
{
	return l->active;
}

void tipc_link_set_active(struct tipc_link *l, bool active)
{
	l->active = active;
}

void tipc_link_add_bc_peer(struct tipc_link *snd_l,
			   struct tipc_link *uc_l,
			   struct sk_buff_head *xmitq)
{
	struct tipc_link *rcv_l = uc_l->bc_rcvlink;

	snd_l->ackers++;
	rcv_l->acked = snd_l->snd_nxt - 1;
	snd_l->state = LINK_ESTABLISHED;
	tipc_link_build_bc_init_msg(uc_l, xmitq);
}

void tipc_link_remove_bc_peer(struct tipc_link *snd_l,
			      struct tipc_link *rcv_l,
			      struct sk_buff_head *xmitq)
{
	u16 ack = snd_l->snd_nxt - 1;

	snd_l->ackers--;
	tipc_link_bc_ack_rcv(rcv_l, ack, xmitq);
	tipc_link_reset(rcv_l);
	rcv_l->state = LINK_RESET;
	if (!snd_l->ackers) {
		tipc_link_reset(snd_l);
		snd_l->state = LINK_RESET;
		__skb_queue_purge(xmitq);
	}
}

int tipc_link_bc_peers(struct tipc_link *l)
{
	return l->ackers;
}

void tipc_link_set_mtu(struct tipc_link *l, int mtu)
{
	l->mtu = mtu;
}

int tipc_link_mtu(struct tipc_link *l)
{
	return l->mtu;
}

static u32 link_own_addr(struct tipc_link *l)
{
	return msg_prevnode(l->pmsg);
}

/**
 * tipc_link_create - create a new link
 * @b_ptr: pointer to associated bearer
 * @peer: network address of node at other end of link
 * @media_addr: media address to use when sending messages over link
 *
 * Returns pointer to link.
 */

struct link *tipc_link_create(struct bearer *b_ptr, const u32 peer,
			      const struct tipc_media_addr *media_addr)
{
	struct link *l_ptr;
	struct tipc_msg *msg;
	char *if_name;

	l_ptr = kzalloc(sizeof(*l_ptr), GFP_ATOMIC);
	if (!l_ptr) {
		warn("Link creation failed, no memory\n");
		return NULL;
	}

	if (LINK_LOG_BUF_SIZE) {
		char *pb = kmalloc(LINK_LOG_BUF_SIZE, GFP_ATOMIC);

		if (!pb) {
			kfree(l_ptr);
			warn("Link creation failed, no memory for print buffer\n");
			return NULL;
		}
		tipc_printbuf_init(&l_ptr->print_buf, pb, LINK_LOG_BUF_SIZE);
	}

	l_ptr->addr = peer;
	if_name = strchr(b_ptr->publ.name, ':') + 1;
	sprintf(l_ptr->name, "%u.%u.%u:%s-%u.%u.%u:",
		tipc_zone(tipc_own_addr), tipc_cluster(tipc_own_addr),
		tipc_node(tipc_own_addr),
		if_name,
		tipc_zone(peer), tipc_cluster(peer), tipc_node(peer));
		/* note: peer i/f is appended to link name by reset/activate */
	memcpy(&l_ptr->media_addr, media_addr, sizeof(*media_addr));
	l_ptr->checkpoint = 1;
	l_ptr->b_ptr = b_ptr;
	link_set_supervision_props(l_ptr, b_ptr->media->tolerance);
	l_ptr->state = RESET_UNKNOWN;

	l_ptr->pmsg = (struct tipc_msg *)&l_ptr->proto_msg;
	msg = l_ptr->pmsg;
	msg_init(msg, LINK_PROTOCOL, RESET_MSG, INT_H_SIZE, l_ptr->addr);
	msg_set_size(msg, sizeof(l_ptr->proto_msg));
	msg_set_session(msg, (tipc_random & 0xffff));
	msg_set_bearer_id(msg, b_ptr->identity);
	strcpy((char *)msg_data(msg), if_name);

	l_ptr->priority = b_ptr->priority;
	tipc_link_set_queue_limits(l_ptr, b_ptr->media->window);

	link_init_max_pkt(l_ptr);

	l_ptr->next_out_no = 1;
	INIT_LIST_HEAD(&l_ptr->waiting_ports);

	link_reset_statistics(l_ptr);

	l_ptr->owner = tipc_node_attach_link(l_ptr);
	if (!l_ptr->owner) {
		if (LINK_LOG_BUF_SIZE)
			kfree(l_ptr->print_buf.buf);
		kfree(l_ptr);
		return NULL;
	}

	k_init_timer(&l_ptr->timer, (Handler)link_timeout, (unsigned long)l_ptr);
	list_add_tail(&l_ptr->link_list, &b_ptr->links);
	tipc_k_signal((Handler)tipc_link_start, (unsigned long)l_ptr);

	dbg("tipc_link_create(): tolerance = %u,cont intv = %u, abort_limit = %u\n",
	    l_ptr->tolerance, l_ptr->continuity_interval, l_ptr->abort_limit);

	return l_ptr;
}

/**
 * tipc_link_delete - delete a link
 * @l_ptr: pointer to link
 *
 * Note: 'tipc_net_lock' is write_locked, bearer is locked.
 * This routine must not grab the node lock until after link timer cancellation
 * to avoid a potential deadlock situation.
 */

void tipc_link_delete(struct link *l_ptr)
{
	if (!l_ptr) {
		err("Attempt to delete non-existent link\n");
		return;
	}

	dbg("tipc_link_delete()\n");

	k_cancel_timer(&l_ptr->timer);

	tipc_node_lock(l_ptr->owner);
	tipc_link_reset(l_ptr);
	tipc_node_detach_link(l_ptr->owner, l_ptr);
	tipc_link_stop(l_ptr);
	list_del_init(&l_ptr->link_list);
	if (LINK_LOG_BUF_SIZE)
		kfree(l_ptr->print_buf.buf);
	tipc_node_unlock(l_ptr->owner);
	k_term_timer(&l_ptr->timer);
	kfree(l_ptr);
}

void tipc_link_start(struct link *l_ptr)
{
	dbg("tipc_link_start %x\n", l_ptr);
	link_state_event(l_ptr, STARTING_EVT);
}

/**
 * link_schedule_port - schedule port for deferred sending
 * @l_ptr: pointer to link
 * @origport: reference to sending port
 * @sz: amount of data to be sent
 *
 * Schedules port for renewed sending of messages after link congestion
 * has abated.
 */

static int link_schedule_port(struct link *l_ptr, u32 origport, u32 sz)
{
	struct port *p_ptr;

	spin_lock_bh(&tipc_port_list_lock);
	p_ptr = tipc_port_lock(origport);
	if (p_ptr) {
		if (!p_ptr->wakeup)
			goto exit;
		if (!list_empty(&p_ptr->wait_list))
			goto exit;
		p_ptr->congested_link = l_ptr;
		p_ptr->publ.congested = 1;
		p_ptr->waiting_pkts = 1 + ((sz - 1) / link_max_pkt(l_ptr));
		list_add_tail(&p_ptr->wait_list, &l_ptr->waiting_ports);
		l_ptr->stats.link_congs++;
exit:
		tipc_port_unlock(p_ptr);
	}
	spin_unlock_bh(&tipc_port_list_lock);
	return -ELINKCONG;
}

void tipc_link_wakeup_ports(struct link *l_ptr, int all)
{
	struct port *p_ptr;
	struct port *temp_p_ptr;
	int win = l_ptr->queue_limit[0] - l_ptr->out_queue_size;

	if (all)
		win = 100000;
	if (win <= 0)
		return;
	if (!spin_trylock_bh(&tipc_port_list_lock))
		return;
	if (link_congested(l_ptr))
		goto exit;
	list_for_each_entry_safe(p_ptr, temp_p_ptr, &l_ptr->waiting_ports,
				 wait_list) {
		if (win <= 0)
			break;
		list_del_init(&p_ptr->wait_list);
		p_ptr->congested_link = NULL;
		spin_lock_bh(p_ptr->publ.lock);
		p_ptr->publ.congested = 0;
		p_ptr->wakeup(&p_ptr->publ);
		win -= p_ptr->waiting_pkts;
		spin_unlock_bh(p_ptr->publ.lock);
	}

exit:
	spin_unlock_bh(&tipc_port_list_lock);
}

/**
 * link_release_outqueue - purge link's outbound message queue
 * @l_ptr: pointer to link
 */

static void link_release_outqueue(struct link *l_ptr)
{
	struct sk_buff *buf = l_ptr->first_out;
	struct sk_buff *next;

	while (buf) {
		next = buf->next;
		buf_discard(buf);
		buf = next;
	}
	l_ptr->first_out = NULL;
	l_ptr->out_queue_size = 0;
}

/**
 * tipc_link_reset_fragments - purge link's inbound message fragments queue
 * @l_ptr: pointer to link
 */

void tipc_link_reset_fragments(struct link *l_ptr)
{
	struct sk_buff *buf = l_ptr->defragm_buf;
	struct sk_buff *next;

	while (buf) {
		next = buf->next;
		buf_discard(buf);
		buf = next;
	}
	l_ptr->defragm_buf = NULL;
}

/**
 * tipc_link_stop - purge all inbound and outbound messages associated with link
 * @l_ptr: pointer to link
 */

void tipc_link_stop(struct link *l_ptr)
{
	struct sk_buff *buf;
	struct sk_buff *next;

	buf = l_ptr->oldest_deferred_in;
	while (buf) {
		next = buf->next;
		buf_discard(buf);
		buf = next;
	}

	buf = l_ptr->first_out;
	while (buf) {
		next = buf->next;
		buf_discard(buf);
		buf = next;
	}

	tipc_link_reset_fragments(l_ptr);

	buf_discard(l_ptr->proto_msg_queue);
	l_ptr->proto_msg_queue = NULL;
}

#if 0

/* LINK EVENT CODE IS NOT SUPPORTED AT PRESENT */

static void link_recv_event(struct link_event *ev)
{
	ev->fcn(ev->addr, ev->name, ev->up);
	kfree(ev);
}

static void link_send_event(void (*fcn)(u32 a, char *n, int up),
			    struct link *l_ptr, int up)
{
	struct link_event *ev;

	ev = kmalloc(sizeof(*ev), GFP_ATOMIC);
	if (!ev) {
		warn("Link event allocation failure\n");
		return;
	}
	ev->addr = l_ptr->addr;
	ev->up = up;
	ev->fcn = fcn;
	memcpy(ev->name, l_ptr->name, TIPC_MAX_LINK_NAME);
	tipc_k_signal((Handler)link_recv_event, (unsigned long)ev);
}

#else

#define link_send_event(fcn, l_ptr, up) do { } while (0)

#endif

void tipc_link_reset(struct link *l_ptr)
{
	struct sk_buff *buf;
	u32 prev_state = l_ptr->state;
	u32 checkpoint = l_ptr->next_in_no;
	int was_active_link = tipc_link_is_active(l_ptr);

	msg_set_session(l_ptr->pmsg, ((msg_session(l_ptr->pmsg) + 1) & 0xffff));

	/* Link is down, accept any session */
	l_ptr->peer_session = INVALID_SESSION;

	/* Prepare for max packet size negotiation */
	link_init_max_pkt(l_ptr);

	l_ptr->state = RESET_UNKNOWN;
	dbg_link_state("Resetting Link\n");

	if ((prev_state == RESET_UNKNOWN) || (prev_state == RESET_RESET))
		return;

	tipc_node_link_down(l_ptr->owner, l_ptr);
	tipc_bearer_remove_dest(l_ptr->b_ptr, l_ptr->addr);
#if 0
	tipc_printf(TIPC_CONS, "\nReset link <%s>\n", l_ptr->name);
	dbg_link_dump();
#endif
	if (was_active_link && tipc_node_has_active_links(l_ptr->owner) &&
	    l_ptr->owner->permit_changeover) {
		l_ptr->reset_checkpoint = checkpoint;
		l_ptr->exp_msg_count = START_CHANGEOVER;
	}

	/* Clean up all queues: */

	link_release_outqueue(l_ptr);
	buf_discard(l_ptr->proto_msg_queue);
	l_ptr->proto_msg_queue = NULL;
	buf = l_ptr->oldest_deferred_in;
	while (buf) {
		struct sk_buff *next = buf->next;
		buf_discard(buf);
		buf = next;
	}
	if (!list_empty(&l_ptr->waiting_ports))
		tipc_link_wakeup_ports(l_ptr, 1);

	l_ptr->retransm_queue_head = 0;
	l_ptr->retransm_queue_size = 0;
	l_ptr->last_out = NULL;
	l_ptr->first_out = NULL;
	l_ptr->next_out = NULL;
	l_ptr->unacked_window = 0;
	l_ptr->checkpoint = 1;
	l_ptr->next_out_no = 1;
	l_ptr->deferred_inqueue_sz = 0;
	l_ptr->oldest_deferred_in = NULL;
	l_ptr->newest_deferred_in = NULL;
	l_ptr->fsm_msg_cnt = 0;
	l_ptr->stale_count = 0;
	link_reset_statistics(l_ptr);

	link_send_event(tipc_cfg_link_event, l_ptr, 0);
	if (!in_own_cluster(l_ptr->addr))
		link_send_event(tipc_disc_link_event, l_ptr, 0);
}


static void link_activate(struct link *l_ptr)
{
	l_ptr->next_in_no = l_ptr->stats.recv_info = 1;
	tipc_node_link_up(l_ptr->owner, l_ptr);
	tipc_bearer_add_dest(l_ptr->b_ptr, l_ptr->addr);
	link_send_event(tipc_cfg_link_event, l_ptr, 1);
	if (!in_own_cluster(l_ptr->addr))
		link_send_event(tipc_disc_link_event, l_ptr, 1);
}

/**
 * link_state_event - link finite state machine
 * @l_ptr: pointer to link
 * @event: state machine event to process
 */

static void link_state_event(struct link *l_ptr, unsigned event)
{
	struct link *other;
	u32 cont_intv = l_ptr->continuity_interval;

	if (!l_ptr->started && (event != STARTING_EVT))
		return;		/* Not yet. */

	if (link_blocked(l_ptr)) {
		if (event == TIMEOUT_EVT) {
			link_set_timer(l_ptr, cont_intv);
		}
		return;	  /* Changeover going on */
	}
	dbg_link("STATE_EV: <%s> ", l_ptr->name);

	switch (l_ptr->state) {
	case WORKING_WORKING:
		dbg_link("WW/");
		switch (event) {
		case TRAFFIC_MSG_EVT:
			dbg_link("TRF-");
			/* fall through */
		case ACTIVATE_MSG:
			dbg_link("ACT\n");
			break;
		case TIMEOUT_EVT:
			dbg_link("TIM ");
			if (l_ptr->next_in_no != l_ptr->checkpoint) {
				l_ptr->checkpoint = l_ptr->next_in_no;
				if (tipc_bclink_acks_missing(l_ptr->owner)) {
					tipc_link_send_proto_msg(l_ptr, STATE_MSG,
								 0, 0, 0, 0, 0);
					l_ptr->fsm_msg_cnt++;
				} else if (l_ptr->max_pkt < l_ptr->max_pkt_target) {
					tipc_link_send_proto_msg(l_ptr, STATE_MSG,
								 1, 0, 0, 0, 0);
					l_ptr->fsm_msg_cnt++;
				}
				link_set_timer(l_ptr, cont_intv);
				break;
			}
			dbg_link(" -> WU\n");
			l_ptr->state = WORKING_UNKNOWN;
			l_ptr->fsm_msg_cnt = 0;
			tipc_link_send_proto_msg(l_ptr, STATE_MSG, 1, 0, 0, 0, 0);
			l_ptr->fsm_msg_cnt++;
			link_set_timer(l_ptr, cont_intv / 4);
			break;
		case RESET_MSG:
			dbg_link("RES -> RR\n");
			info("Resetting link <%s>, requested by peer\n",
			     l_ptr->name);
			tipc_link_reset(l_ptr);
			l_ptr->state = RESET_RESET;
			l_ptr->fsm_msg_cnt = 0;
			tipc_link_send_proto_msg(l_ptr, ACTIVATE_MSG, 0, 0, 0, 0, 0);
			l_ptr->fsm_msg_cnt++;
			link_set_timer(l_ptr, cont_intv);
			break;
		default:
			err("Unknown link event %u in WW state\n", event);
		}
		break;
	case WORKING_UNKNOWN:
		dbg_link("WU/");
		switch (event) {
		case TRAFFIC_MSG_EVT:
			dbg_link("TRF-");
		case ACTIVATE_MSG:
			dbg_link("ACT -> WW\n");
			l_ptr->state = WORKING_WORKING;
			l_ptr->fsm_msg_cnt = 0;
			link_set_timer(l_ptr, cont_intv);
			break;
		case RESET_MSG:
			dbg_link("RES -> RR\n");
			info("Resetting link <%s>, requested by peer "
			     "while probing\n", l_ptr->name);
			tipc_link_reset(l_ptr);
			l_ptr->state = RESET_RESET;
			l_ptr->fsm_msg_cnt = 0;
			tipc_link_send_proto_msg(l_ptr, ACTIVATE_MSG, 0, 0, 0, 0, 0);
			l_ptr->fsm_msg_cnt++;
			link_set_timer(l_ptr, cont_intv);
			break;
		case TIMEOUT_EVT:
			dbg_link("TIM ");
			if (l_ptr->next_in_no != l_ptr->checkpoint) {
				dbg_link("-> WW \n");
				l_ptr->state = WORKING_WORKING;
				l_ptr->fsm_msg_cnt = 0;
				l_ptr->checkpoint = l_ptr->next_in_no;
				if (tipc_bclink_acks_missing(l_ptr->owner)) {
					tipc_link_send_proto_msg(l_ptr, STATE_MSG,
								 0, 0, 0, 0, 0);
					l_ptr->fsm_msg_cnt++;
				}
				link_set_timer(l_ptr, cont_intv);
			} else if (l_ptr->fsm_msg_cnt < l_ptr->abort_limit) {
				dbg_link("Probing %u/%u,timer = %u ms)\n",
					 l_ptr->fsm_msg_cnt, l_ptr->abort_limit,
					 cont_intv / 4);
				tipc_link_send_proto_msg(l_ptr, STATE_MSG,
							 1, 0, 0, 0, 0);
				l_ptr->fsm_msg_cnt++;
				link_set_timer(l_ptr, cont_intv / 4);
			} else {	/* Link has failed */
				dbg_link("-> RU (%u probes unanswered)\n",
					 l_ptr->fsm_msg_cnt);
				warn("Resetting link <%s>, peer not responding\n",
				     l_ptr->name);
				tipc_link_reset(l_ptr);
				l_ptr->state = RESET_UNKNOWN;
				l_ptr->fsm_msg_cnt = 0;
				tipc_link_send_proto_msg(l_ptr, RESET_MSG,
							 0, 0, 0, 0, 0);
				l_ptr->fsm_msg_cnt++;
				link_set_timer(l_ptr, cont_intv);
			}
			break;
		default:
			err("Unknown link event %u in WU state\n", event);
		}
		break;
	case RESET_UNKNOWN:
		dbg_link("RU/");
		switch (event) {
		case TRAFFIC_MSG_EVT:
			dbg_link("TRF-\n");
			break;
		case ACTIVATE_MSG:
			other = l_ptr->owner->active_links[0];
			if (other && link_working_unknown(other)) {
				dbg_link("ACT\n");
				break;
			}
			dbg_link("ACT -> WW\n");
			l_ptr->state = WORKING_WORKING;
			l_ptr->fsm_msg_cnt = 0;
			link_activate(l_ptr);
			tipc_link_send_proto_msg(l_ptr, STATE_MSG, 1, 0, 0, 0, 0);
			l_ptr->fsm_msg_cnt++;
			link_set_timer(l_ptr, cont_intv);
			break;
		case RESET_MSG:
			dbg_link("RES \n");
			dbg_link(" -> RR\n");
			l_ptr->state = RESET_RESET;
			l_ptr->fsm_msg_cnt = 0;
			tipc_link_send_proto_msg(l_ptr, ACTIVATE_MSG, 1, 0, 0, 0, 0);
			l_ptr->fsm_msg_cnt++;
			link_set_timer(l_ptr, cont_intv);
			break;
		case STARTING_EVT:
			dbg_link("START-");
			l_ptr->started = 1;
			/* fall through */
		case TIMEOUT_EVT:
			dbg_link("TIM \n");
			tipc_link_send_proto_msg(l_ptr, RESET_MSG, 0, 0, 0, 0, 0);
			l_ptr->fsm_msg_cnt++;
			link_set_timer(l_ptr, cont_intv);
			break;
		default:
			err("Unknown link event %u in RU state\n", event);
		}
		break;
	case RESET_RESET:
		dbg_link("RR/ ");
		switch (event) {
		case TRAFFIC_MSG_EVT:
			dbg_link("TRF-");
			/* fall through */
		case ACTIVATE_MSG:
			other = l_ptr->owner->active_links[0];
			if (other && link_working_unknown(other)) {
				dbg_link("ACT\n");
				break;
			}
			dbg_link("ACT -> WW\n");
			l_ptr->state = WORKING_WORKING;
			l_ptr->fsm_msg_cnt = 0;
			link_activate(l_ptr);
			tipc_link_send_proto_msg(l_ptr, STATE_MSG, 1, 0, 0, 0, 0);
			l_ptr->fsm_msg_cnt++;
			link_set_timer(l_ptr, cont_intv);
			break;
		case RESET_MSG:
			dbg_link("RES\n");
			break;
		case TIMEOUT_EVT:
			dbg_link("TIM\n");
			tipc_link_send_proto_msg(l_ptr, ACTIVATE_MSG, 0, 0, 0, 0, 0);
			l_ptr->fsm_msg_cnt++;
			link_set_timer(l_ptr, cont_intv);
			dbg_link("fsm_msg_cnt %u\n", l_ptr->fsm_msg_cnt);
			break;
		default:
			err("Unknown link event %u in RR state\n", event);
		}
		break;
	default:
		err("Unknown link state %u/%u\n", l_ptr->state, event);
	}
}

/*
 * link_bundle_buf(): Append contents of a buffer to
 * the tail of an existing one.
 */

static int link_bundle_buf(struct link *l_ptr,
			   struct sk_buff *bundler,
			   struct sk_buff *buf)
{
	struct tipc_msg *bundler_msg = buf_msg(bundler);
	struct tipc_msg *msg = buf_msg(buf);
	u32 size = msg_size(msg);
	u32 bundle_size = msg_size(bundler_msg);
	u32 to_pos = align(bundle_size);
	u32 pad = to_pos - bundle_size;

	if (msg_user(bundler_msg) != MSG_BUNDLER)
		return 0;
	if (msg_type(bundler_msg) != OPEN_MSG)
		return 0;
	if (skb_tailroom(bundler) < (pad + size))
		return 0;
	if (link_max_pkt(l_ptr) < (to_pos + size))
		return 0;

	skb_put(bundler, pad + size);
	skb_copy_to_linear_data_offset(bundler, to_pos, buf->data, size);
	msg_set_size(bundler_msg, to_pos + size);
	msg_set_msgcnt(bundler_msg, msg_msgcnt(bundler_msg) + 1);
	dbg("Packed msg # %u(%u octets) into pos %u in buf(#%u)\n",
	    msg_msgcnt(bundler_msg), size, to_pos, msg_seqno(bundler_msg));
	msg_dbg(msg, "PACKD:");
	buf_discard(buf);
	l_ptr->stats.sent_bundled++;
	return 1;
}

static void link_add_to_outqueue(struct link *l_ptr,
				 struct sk_buff *buf,
				 struct tipc_msg *msg)
{
	u32 ack = mod(l_ptr->next_in_no - 1);
	u32 seqno = mod(l_ptr->next_out_no++);

	msg_set_word(msg, 2, ((ack << 16) | seqno));
	msg_set_bcast_ack(msg, l_ptr->owner->bclink.last_in);
	buf->next = NULL;
	if (l_ptr->first_out) {
		l_ptr->last_out->next = buf;
		l_ptr->last_out = buf;
	} else
		l_ptr->first_out = l_ptr->last_out = buf;
	l_ptr->out_queue_size++;
}

/*
 * tipc_link_send_buf() is the 'full path' for messages, called from
 * inside TIPC when the 'fast path' in tipc_send_buf
 * has failed, and from link_send()
 */

int tipc_link_send_buf(struct link *l_ptr, struct sk_buff *buf)
{
	struct tipc_msg *msg = buf_msg(buf);
	u32 size = msg_size(msg);
	u32 dsz = msg_data_sz(msg);
	u32 queue_size = l_ptr->out_queue_size;
	u32 imp = msg_tot_importance(msg);
	u32 queue_limit = l_ptr->queue_limit[imp];
	u32 max_packet = link_max_pkt(l_ptr);

	msg_set_prevnode(msg, tipc_own_addr);	/* If routed message */

	/* Match msg importance against queue limits: */

	if (unlikely(queue_size >= queue_limit)) {
		if (imp <= TIPC_CRITICAL_IMPORTANCE) {
			return link_schedule_port(l_ptr, msg_origport(msg),
						  size);
		}
		msg_dbg(msg, "TIPC: Congestion, throwing away\n");
		buf_discard(buf);
		if (imp > CONN_MANAGER) {
			warn("Resetting link <%s>, send queue full", l_ptr->name);
			tipc_link_reset(l_ptr);
		}
		return dsz;
	}

	/* Fragmentation needed ? */

	if (size > max_packet)
		return tipc_link_send_long_buf(l_ptr, buf);

	/* Packet can be queued or sent: */

	if (queue_size > l_ptr->stats.max_queue_sz)
		l_ptr->stats.max_queue_sz = queue_size;

	if (likely(!tipc_bearer_congested(l_ptr->b_ptr, l_ptr) &&
		   !link_congested(l_ptr))) {
		link_add_to_outqueue(l_ptr, buf, msg);

		if (likely(tipc_bearer_send(l_ptr->b_ptr, buf, &l_ptr->media_addr))) {
			l_ptr->unacked_window = 0;
		} else {
			tipc_bearer_schedule(l_ptr->b_ptr, l_ptr);
			l_ptr->stats.bearer_congs++;
			l_ptr->next_out = buf;
		}
		return dsz;
	}
	/* Congestion: can message be bundled ?: */

	if ((msg_user(msg) != CHANGEOVER_PROTOCOL) &&
	    (msg_user(msg) != MSG_FRAGMENTER)) {

		/* Try adding message to an existing bundle */

		if (l_ptr->next_out &&
		    link_bundle_buf(l_ptr, l_ptr->last_out, buf)) {
			tipc_bearer_resolve_congestion(l_ptr->b_ptr, l_ptr);
			return dsz;
		}

		/* Try creating a new bundle */

		if (size <= max_packet * 2 / 3) {
			struct sk_buff *bundler = buf_acquire(max_packet);
			struct tipc_msg bundler_hdr;

			if (bundler) {
				msg_init(&bundler_hdr, MSG_BUNDLER, OPEN_MSG,
					 INT_H_SIZE, l_ptr->addr);
				skb_copy_to_linear_data(bundler, &bundler_hdr,
							INT_H_SIZE);
				skb_trim(bundler, INT_H_SIZE);
				link_bundle_buf(l_ptr, bundler, buf);
				buf = bundler;
				msg = buf_msg(buf);
				l_ptr->stats.sent_bundles++;
			}
		}
	}
	if (!l_ptr->next_out)
		l_ptr->next_out = buf;
	link_add_to_outqueue(l_ptr, buf, msg);
	tipc_bearer_resolve_congestion(l_ptr->b_ptr, l_ptr);
	return dsz;
}

/*
 * tipc_link_send(): same as tipc_link_send_buf(), but the link to use has
 * not been selected yet, and the the owner node is not locked
 * Called by TIPC internal users, e.g. the name distributor
 */

int tipc_link_send(struct sk_buff *buf, u32 dest, u32 selector)
{
	struct link *l_ptr;
	struct tipc_node *n_ptr;
	int res = -ELINKCONG;

	read_lock_bh(&tipc_net_lock);
	n_ptr = tipc_node_select(dest, selector);
	if (n_ptr) {
		tipc_node_lock(n_ptr);
		l_ptr = n_ptr->active_links[selector & 1];
		if (l_ptr) {
			dbg("tipc_link_send: found link %x for dest %x\n", l_ptr, dest);
			res = tipc_link_send_buf(l_ptr, buf);
		} else {
			dbg("Attempt to send msg to unreachable node:\n");
			msg_dbg(buf_msg(buf),">>>");
			buf_discard(buf);
		}
		tipc_node_unlock(n_ptr);
	} else {
		dbg("Attempt to send msg to unknown node:\n");
		msg_dbg(buf_msg(buf),">>>");
		buf_discard(buf);
	}
	read_unlock_bh(&tipc_net_lock);
	return res;
}

/*
 * link_send_buf_fast: Entry for data messages where the
 * destination link is known and the header is complete,
 * inclusive total message length. Very time critical.
 * Link is locked. Returns user data length.
 */

static int link_send_buf_fast(struct link *l_ptr, struct sk_buff *buf,
			      u32 *used_max_pkt)
{
	struct tipc_msg *msg = buf_msg(buf);
	int res = msg_data_sz(msg);

	if (likely(!link_congested(l_ptr))) {
		if (likely(msg_size(msg) <= link_max_pkt(l_ptr))) {
			if (likely(list_empty(&l_ptr->b_ptr->cong_links))) {
				link_add_to_outqueue(l_ptr, buf, msg);
				if (likely(tipc_bearer_send(l_ptr->b_ptr, buf,
							    &l_ptr->media_addr))) {
					l_ptr->unacked_window = 0;
					msg_dbg(msg,"SENT_FAST:");
					return res;
				}
				dbg("failed sent fast...\n");
				tipc_bearer_schedule(l_ptr->b_ptr, l_ptr);
				l_ptr->stats.bearer_congs++;
				l_ptr->next_out = buf;
				return res;
			}
		}
		else
			*used_max_pkt = link_max_pkt(l_ptr);
	}
	return tipc_link_send_buf(l_ptr, buf);  /* All other cases */
}

/*
 * tipc_send_buf_fast: Entry for data messages where the
 * destination node is known and the header is complete,
 * inclusive total message length.
 * Returns user data length.
 */
int tipc_send_buf_fast(struct sk_buff *buf, u32 destnode)
{
	struct link *l_ptr;
	struct tipc_node *n_ptr;
	int res;
	u32 selector = msg_origport(buf_msg(buf)) & 1;
	u32 dummy;

	if (destnode == tipc_own_addr)
		return tipc_port_recv_msg(buf);

	read_lock_bh(&tipc_net_lock);
	n_ptr = tipc_node_select(destnode, selector);
	if (likely(n_ptr)) {
		tipc_node_lock(n_ptr);
		l_ptr = n_ptr->active_links[selector];
		dbg("send_fast: buf %x selected %x, destnode = %x\n",
		    buf, l_ptr, destnode);
		if (likely(l_ptr)) {
			res = link_send_buf_fast(l_ptr, buf, &dummy);
			tipc_node_unlock(n_ptr);
			read_unlock_bh(&tipc_net_lock);
			return res;
		}
		tipc_node_unlock(n_ptr);
	}
	read_unlock_bh(&tipc_net_lock);
	res = msg_data_sz(buf_msg(buf));
	tipc_reject_msg(buf, TIPC_ERR_NO_NODE);
	return res;
}


/*
 * tipc_link_send_sections_fast: Entry for messages where the
 * destination processor is known and the header is complete,
 * except for total message length.
 * Returns user data length or errno.
 */
int tipc_link_send_sections_fast(struct port *sender,
				 struct iovec const *msg_sect,
				 const u32 num_sect,
				 u32 destaddr)
{
	struct tipc_msg *hdr = &sender->publ.phdr;
	struct link *l_ptr;
	struct sk_buff *buf;
	struct tipc_node *node;
	int res;
	u32 selector = msg_origport(hdr) & 1;

again:
	/*
	 * Try building message using port's max_pkt hint.
	 * (Must not hold any locks while building message.)
	 */

	res = msg_build(hdr, msg_sect, num_sect, sender->publ.max_pkt,
			!sender->user_port, &buf);

	read_lock_bh(&tipc_net_lock);
	node = tipc_node_select(destaddr, selector);
	if (likely(node)) {
		tipc_node_lock(node);
		l_ptr = node->active_links[selector];
		if (likely(l_ptr)) {
			if (likely(buf)) {
				res = link_send_buf_fast(l_ptr, buf,
							 &sender->publ.max_pkt);
				if (unlikely(res < 0))
					buf_discard(buf);
exit:
				tipc_node_unlock(node);
				read_unlock_bh(&tipc_net_lock);
				return res;
			}

			/* Exit if build request was invalid */

			if (unlikely(res < 0))
				goto exit;

			/* Exit if link (or bearer) is congested */

			if (link_congested(l_ptr) ||
			    !list_empty(&l_ptr->b_ptr->cong_links)) {
				res = link_schedule_port(l_ptr,
							 sender->publ.ref, res);
				goto exit;
			}

			/*
			 * Message size exceeds max_pkt hint; update hint,
			 * then re-try fast path or fragment the message
			 */

			sender->publ.max_pkt = link_max_pkt(l_ptr);
			tipc_node_unlock(node);
			read_unlock_bh(&tipc_net_lock);


			if ((msg_hdr_sz(hdr) + res) <= sender->publ.max_pkt)
				goto again;

			return link_send_sections_long(sender, msg_sect,
						       num_sect, destaddr);
		}
		tipc_node_unlock(node);
	}
	read_unlock_bh(&tipc_net_lock);

	/* Couldn't find a link to the destination node */

	if (buf)
		return tipc_reject_msg(buf, TIPC_ERR_NO_NODE);
	if (res >= 0)
		return tipc_port_reject_sections(sender, hdr, msg_sect, num_sect,
						 TIPC_ERR_NO_NODE);
	return res;
}

/*
 * link_send_sections_long(): Entry for long messages where the
 * destination node is known and the header is complete,
 * inclusive total message length.
 * Link and bearer congestion status have been checked to be ok,
 * and are ignored if they change.
 *
 * Note that fragments do not use the full link MTU so that they won't have
 * to undergo refragmentation if link changeover causes them to be sent
 * over another link with an additional tunnel header added as prefix.
 * (Refragmentation will still occur if the other link has a smaller MTU.)
 *
 * Returns user data length or errno.
 */
static int link_send_sections_long(struct port *sender,
				   struct iovec const *msg_sect,
				   u32 num_sect,
				   u32 destaddr)
{
	struct link *l_ptr;
	struct tipc_node *node;
	struct tipc_msg *hdr = &sender->publ.phdr;
	u32 dsz = msg_data_sz(hdr);
	u32 max_pkt,fragm_sz,rest;
	struct tipc_msg fragm_hdr;
	struct sk_buff *buf,*buf_chain,*prev;
	u32 fragm_crs,fragm_rest,hsz,sect_rest;
	const unchar *sect_crs;
	int curr_sect;
	u32 fragm_no;

again:
	fragm_no = 1;
	max_pkt = sender->publ.max_pkt - INT_H_SIZE;
		/* leave room for tunnel header in case of link changeover */
	fragm_sz = max_pkt - INT_H_SIZE;
		/* leave room for fragmentation header in each fragment */
	rest = dsz;
	fragm_crs = 0;
	fragm_rest = 0;
	sect_rest = 0;
	sect_crs = NULL;
	curr_sect = -1;

	/* Prepare reusable fragment header: */

	msg_dbg(hdr, ">FRAGMENTING>");
	msg_init(&fragm_hdr, MSG_FRAGMENTER, FIRST_FRAGMENT,
		 INT_H_SIZE, msg_destnode(hdr));
	msg_set_link_selector(&fragm_hdr, sender->publ.ref);
	msg_set_size(&fragm_hdr, max_pkt);
	msg_set_fragm_no(&fragm_hdr, 1);

	/* Prepare header of first fragment: */

	buf_chain = buf = buf_acquire(max_pkt);
	if (!buf)
		return -ENOMEM;
	buf->next = NULL;
	skb_copy_to_linear_data(buf, &fragm_hdr, INT_H_SIZE);
	hsz = msg_hdr_sz(hdr);
	skb_copy_to_linear_data_offset(buf, INT_H_SIZE, hdr, hsz);
	msg_dbg(buf_msg(buf), ">BUILD>");

	/* Chop up message: */

	fragm_crs = INT_H_SIZE + hsz;
	fragm_rest = fragm_sz - hsz;

	do {		/* For all sections */
		u32 sz;

		if (!sect_rest) {
			sect_rest = msg_sect[++curr_sect].iov_len;
			sect_crs = (const unchar *)msg_sect[curr_sect].iov_base;
		}

		if (sect_rest < fragm_rest)
			sz = sect_rest;
		else
			sz = fragm_rest;

		if (likely(!sender->user_port)) {
			if (copy_from_user(buf->data + fragm_crs, sect_crs, sz)) {
error:
				for (; buf_chain; buf_chain = buf) {
					buf = buf_chain->next;
					buf_discard(buf_chain);
				}
				return -EFAULT;
			}
		} else
			skb_copy_to_linear_data_offset(buf, fragm_crs,
						       sect_crs, sz);
		sect_crs += sz;
		sect_rest -= sz;
		fragm_crs += sz;
		fragm_rest -= sz;
		rest -= sz;

		if (!fragm_rest && rest) {

			/* Initiate new fragment: */
			if (rest <= fragm_sz) {
				fragm_sz = rest;
				msg_set_type(&fragm_hdr,LAST_FRAGMENT);
			} else {
				msg_set_type(&fragm_hdr, FRAGMENT);
			}
			msg_set_size(&fragm_hdr, fragm_sz + INT_H_SIZE);
			msg_set_fragm_no(&fragm_hdr, ++fragm_no);
			prev = buf;
			buf = buf_acquire(fragm_sz + INT_H_SIZE);
			if (!buf)
				goto error;

			buf->next = NULL;
			prev->next = buf;
			skb_copy_to_linear_data(buf, &fragm_hdr, INT_H_SIZE);
			fragm_crs = INT_H_SIZE;
			fragm_rest = fragm_sz;
			msg_dbg(buf_msg(buf),"  >BUILD>");
		}
	}
	while (rest > 0);

	/*
	 * Now we have a buffer chain. Select a link and check
	 * that packet size is still OK
	 */
	node = tipc_node_select(destaddr, sender->publ.ref & 1);
	if (likely(node)) {
		tipc_node_lock(node);
		l_ptr = node->active_links[sender->publ.ref & 1];
		if (!l_ptr) {
			tipc_node_unlock(node);
			goto reject;
		}
		if (link_max_pkt(l_ptr) < max_pkt) {
			sender->publ.max_pkt = link_max_pkt(l_ptr);
			tipc_node_unlock(node);
			for (; buf_chain; buf_chain = buf) {
				buf = buf_chain->next;
				buf_discard(buf_chain);
			}
			goto again;
		}
	} else {
reject:
		for (; buf_chain; buf_chain = buf) {
			buf = buf_chain->next;
			buf_discard(buf_chain);
		}
		return tipc_port_reject_sections(sender, hdr, msg_sect, num_sect,
						 TIPC_ERR_NO_NODE);
	}

	/* Append whole chain to send queue: */

	buf = buf_chain;
	l_ptr->long_msg_seq_no = mod(l_ptr->long_msg_seq_no + 1);
	if (!l_ptr->next_out)
		l_ptr->next_out = buf_chain;
	l_ptr->stats.sent_fragmented++;
	while (buf) {
		struct sk_buff *next = buf->next;
		struct tipc_msg *msg = buf_msg(buf);

		l_ptr->stats.sent_fragments++;
		msg_set_long_msgno(msg, l_ptr->long_msg_seq_no);
		link_add_to_outqueue(l_ptr, buf, msg);
		msg_dbg(msg, ">ADD>");
		buf = next;
	}

	/* Send it, if possible: */

	tipc_link_push_queue(l_ptr);
	tipc_node_unlock(node);
	return dsz;
}

/*
 * tipc_link_push_packet: Push one unsent packet to the media
 */
u32 tipc_link_push_packet(struct link *l_ptr)
{
	struct sk_buff *buf = l_ptr->first_out;
	u32 r_q_size = l_ptr->retransm_queue_size;
	u32 r_q_head = l_ptr->retransm_queue_head;

	/* Step to position where retransmission failed, if any,    */
	/* consider that buffers may have been released in meantime */

	if (r_q_size && buf) {
		u32 last = lesser(mod(r_q_head + r_q_size),
				  link_last_sent(l_ptr));
		u32 first = msg_seqno(buf_msg(buf));

		while (buf && less(first, r_q_head)) {
			first = mod(first + 1);
			buf = buf->next;
		}
		l_ptr->retransm_queue_head = r_q_head = first;
		l_ptr->retransm_queue_size = r_q_size = mod(last - first);
	}

	/* Continue retransmission now, if there is anything: */

	if (r_q_size && buf && !skb_cloned(buf)) {
		msg_set_ack(buf_msg(buf), mod(l_ptr->next_in_no - 1));
		msg_set_bcast_ack(buf_msg(buf), l_ptr->owner->bclink.last_in);
		if (tipc_bearer_send(l_ptr->b_ptr, buf, &l_ptr->media_addr)) {
			msg_dbg(buf_msg(buf), ">DEF-RETR>");
			l_ptr->retransm_queue_head = mod(++r_q_head);
			l_ptr->retransm_queue_size = --r_q_size;
			l_ptr->stats.retransmitted++;
			return 0;
		} else {
			l_ptr->stats.bearer_congs++;
			msg_dbg(buf_msg(buf), "|>DEF-RETR>");
			return PUSH_FAILED;
		}
	}

	/* Send deferred protocol message, if any: */

	buf = l_ptr->proto_msg_queue;
	if (buf) {
		msg_set_ack(buf_msg(buf), mod(l_ptr->next_in_no - 1));
		msg_set_bcast_ack(buf_msg(buf),l_ptr->owner->bclink.last_in);
		if (tipc_bearer_send(l_ptr->b_ptr, buf, &l_ptr->media_addr)) {
			msg_dbg(buf_msg(buf), ">DEF-PROT>");
			l_ptr->unacked_window = 0;
			buf_discard(buf);
			l_ptr->proto_msg_queue = NULL;
			return 0;
		} else {
			msg_dbg(buf_msg(buf), "|>DEF-PROT>");
			l_ptr->stats.bearer_congs++;
			return PUSH_FAILED;
		}
	}

	/* Send one deferred data message, if send window not full: */

	buf = l_ptr->next_out;
	if (buf) {
		struct tipc_msg *msg = buf_msg(buf);
		u32 next = msg_seqno(msg);
		u32 first = msg_seqno(buf_msg(l_ptr->first_out));

		if (mod(next - first) < l_ptr->queue_limit[0]) {
			msg_set_ack(msg, mod(l_ptr->next_in_no - 1));
			msg_set_bcast_ack(msg, l_ptr->owner->bclink.last_in);
			if (tipc_bearer_send(l_ptr->b_ptr, buf, &l_ptr->media_addr)) {
				if (msg_user(msg) == MSG_BUNDLER)
					msg_set_type(msg, CLOSED_MSG);
				msg_dbg(msg, ">PUSH-DATA>");
				l_ptr->next_out = buf->next;
				return 0;
			} else {
				msg_dbg(msg, "|PUSH-DATA|");
				l_ptr->stats.bearer_congs++;
				return PUSH_FAILED;
			}
		}
	}
	return PUSH_FINISHED;
}

/*
 * push_queue(): push out the unsent messages of a link where
 *               congestion has abated. Node is locked
 */
void tipc_link_push_queue(struct link *l_ptr)
{
	u32 res;

	if (tipc_bearer_congested(l_ptr->b_ptr, l_ptr))
		return;

	do {
		res = tipc_link_push_packet(l_ptr);
	} while (!res);

	if (res == PUSH_FAILED)
		tipc_bearer_schedule(l_ptr->b_ptr, l_ptr);
}

static void link_reset_all(unsigned long addr)
{
	struct tipc_node *n_ptr;
	char addr_string[16];
	u32 i;

	read_lock_bh(&tipc_net_lock);
	n_ptr = tipc_node_find((u32)addr);
	if (!n_ptr) {
		read_unlock_bh(&tipc_net_lock);
		return;	/* node no longer exists */
	}

	tipc_node_lock(n_ptr);

	warn("Resetting all links to %s\n",
	     addr_string_fill(addr_string, n_ptr->addr));

	for (i = 0; i < MAX_BEARERS; i++) {
		if (n_ptr->links[i]) {
			link_print(n_ptr->links[i], TIPC_OUTPUT,
				   "Resetting link\n");
			tipc_link_reset(n_ptr->links[i]);
		}
	}

	tipc_node_unlock(n_ptr);
	read_unlock_bh(&tipc_net_lock);
}

static void link_retransmit_failure(struct link *l_ptr, struct sk_buff *buf)
{
	struct tipc_msg *msg = buf_msg(buf);

	warn("Retransmission failure on link <%s>\n", l_ptr->name);
	tipc_msg_dbg(TIPC_OUTPUT, msg, ">RETR-FAIL>");

	if (l_ptr->addr) {

		/* Handle failure on standard link */

		link_print(l_ptr, TIPC_OUTPUT, "Resetting link\n");
		tipc_link_reset(l_ptr);

	} else {

		/* Handle failure on broadcast link */

		struct tipc_node *n_ptr;
		char addr_string[16];

		tipc_printf(TIPC_OUTPUT, "Msg seq number: %u,  ", msg_seqno(msg));
		tipc_printf(TIPC_OUTPUT, "Outstanding acks: %lu\n",
				     (unsigned long) TIPC_SKB_CB(buf)->handle);

		n_ptr = l_ptr->owner->next;
		tipc_node_lock(n_ptr);

		addr_string_fill(addr_string, n_ptr->addr);
		tipc_printf(TIPC_OUTPUT, "Multicast link info for %s\n", addr_string);
		tipc_printf(TIPC_OUTPUT, "Supported: %d,  ", n_ptr->bclink.supported);
		tipc_printf(TIPC_OUTPUT, "Acked: %u\n", n_ptr->bclink.acked);
		tipc_printf(TIPC_OUTPUT, "Last in: %u,  ", n_ptr->bclink.last_in);
		tipc_printf(TIPC_OUTPUT, "Gap after: %u,  ", n_ptr->bclink.gap_after);
		tipc_printf(TIPC_OUTPUT, "Gap to: %u\n", n_ptr->bclink.gap_to);
		tipc_printf(TIPC_OUTPUT, "Nack sync: %u\n\n", n_ptr->bclink.nack_sync);

		tipc_k_signal((Handler)link_reset_all, (unsigned long)n_ptr->addr);

		tipc_node_unlock(n_ptr);

		l_ptr->stale_count = 0;
	}
}

void tipc_link_retransmit(struct link *l_ptr, struct sk_buff *buf,
			  u32 retransmits)
{
	struct tipc_msg *msg;

	if (!buf)
		return;

	msg = buf_msg(buf);

	dbg("Retransmitting %u in link %x\n", retransmits, l_ptr);

	if (tipc_bearer_congested(l_ptr->b_ptr, l_ptr)) {
		if (!skb_cloned(buf)) {
			msg_dbg(msg, ">NO_RETR->BCONG>");
			dbg_print_link(l_ptr, "   ");
			l_ptr->retransm_queue_head = msg_seqno(msg);
			l_ptr->retransm_queue_size = retransmits;
			return;
		} else {
			/* Don't retransmit if driver already has the buffer */
		}
	} else {
		/* Detect repeated retransmit failures on uncongested bearer */

		if (l_ptr->last_retransmitted == msg_seqno(msg)) {
			if (++l_ptr->stale_count > 100) {
				link_retransmit_failure(l_ptr, buf);
				return;
			}
		} else {
			l_ptr->last_retransmitted = msg_seqno(msg);
			l_ptr->stale_count = 1;
		}
	}

	while (retransmits && (buf != l_ptr->next_out) && buf && !skb_cloned(buf)) {
		msg = buf_msg(buf);
		msg_set_ack(msg, mod(l_ptr->next_in_no - 1));
		msg_set_bcast_ack(msg, l_ptr->owner->bclink.last_in);
		if (tipc_bearer_send(l_ptr->b_ptr, buf, &l_ptr->media_addr)) {
			msg_dbg(buf_msg(buf), ">RETR>");
			buf = buf->next;
			retransmits--;
			l_ptr->stats.retransmitted++;
		} else {
			tipc_bearer_schedule(l_ptr->b_ptr, l_ptr);
			l_ptr->stats.bearer_congs++;
			l_ptr->retransm_queue_head = msg_seqno(buf_msg(buf));
			l_ptr->retransm_queue_size = retransmits;
			return;
		}
	}

	l_ptr->retransm_queue_head = l_ptr->retransm_queue_size = 0;
}

/**
 * link_insert_deferred_queue - insert deferred messages back into receive chain
 */

static struct sk_buff *link_insert_deferred_queue(struct link *l_ptr,
						  struct sk_buff *buf)
{
	u32 seq_no;

	if (l_ptr->oldest_deferred_in == NULL)
		return buf;

	seq_no = msg_seqno(buf_msg(l_ptr->oldest_deferred_in));
	if (seq_no == mod(l_ptr->next_in_no)) {
		l_ptr->newest_deferred_in->next = buf;
		buf = l_ptr->oldest_deferred_in;
		l_ptr->oldest_deferred_in = NULL;
		l_ptr->deferred_inqueue_sz = 0;
	}
	return buf;
}

/**
 * link_recv_buf_validate - validate basic format of received message
 *
 * This routine ensures a TIPC message has an acceptable header, and at least
 * as much data as the header indicates it should.  The routine also ensures
 * that the entire message header is stored in the main fragment of the message
 * buffer, to simplify future access to message header fields.
 *
 * Note: Having extra info present in the message header or data areas is OK.
 * TIPC will ignore the excess, under the assumption that it is optional info
 * introduced by a later release of the protocol.
 */

static int link_recv_buf_validate(struct sk_buff *buf)
{
	static u32 min_data_hdr_size[8] = {
		SHORT_H_SIZE, MCAST_H_SIZE, LONG_H_SIZE, DIR_MSG_H_SIZE,
		MAX_H_SIZE, MAX_H_SIZE, MAX_H_SIZE, MAX_H_SIZE
		};

	struct tipc_msg *msg;
	u32 tipc_hdr[2];
	u32 size;
	u32 hdr_size;
	u32 min_hdr_size;

	if (unlikely(buf->len < MIN_H_SIZE))
		return 0;

	msg = skb_header_pointer(buf, 0, sizeof(tipc_hdr), tipc_hdr);
	if (msg == NULL)
		return 0;

	if (unlikely(msg_version(msg) != TIPC_VERSION))
		return 0;

	size = msg_size(msg);
	hdr_size = msg_hdr_sz(msg);
	min_hdr_size = msg_isdata(msg) ?
		min_data_hdr_size[msg_type(msg)] : INT_H_SIZE;

	if (unlikely((hdr_size < min_hdr_size) ||
		     (size < hdr_size) ||
		     (buf->len < size) ||
		     (size - hdr_size > TIPC_MAX_USER_MSG_SIZE)))
		return 0;

	return pskb_may_pull(buf, hdr_size);
}

void tipc_recv_msg(struct sk_buff *head, struct tipc_bearer *tb_ptr)
{
	read_lock_bh(&tipc_net_lock);
	while (head) {
		struct bearer *b_ptr = (struct bearer *)tb_ptr;
		struct tipc_node *n_ptr;
		struct link *l_ptr;
		struct sk_buff *crs;
		struct sk_buff *buf = head;
		struct tipc_msg *msg;
		u32 seq_no;
		u32 ackd;
		u32 released = 0;
		int type;

		head = head->next;

		/* Ensure message is well-formed */

		if (unlikely(!link_recv_buf_validate(buf)))
			goto cont;

		/* Ensure message data is a single contiguous unit */

		if (unlikely(buf_linearize(buf))) {
			goto cont;
		}

		/* Handle arrival of a non-unicast link message */

		msg = buf_msg(buf);

		if (unlikely(msg_non_seq(msg))) {
			if (msg_user(msg) ==  LINK_CONFIG)
				tipc_disc_recv_msg(buf, b_ptr);
			else
				tipc_bclink_recv_pkt(buf);
			continue;
		}

		if (unlikely(!msg_short(msg) &&
			     (msg_destnode(msg) != tipc_own_addr)))
			goto cont;

		/* Locate unicast link endpoint that should handle message */

		n_ptr = tipc_node_find(msg_prevnode(msg));
		if (unlikely(!n_ptr))
			goto cont;
		tipc_node_lock(n_ptr);

		l_ptr = n_ptr->links[b_ptr->identity];
		if (unlikely(!l_ptr)) {
			tipc_node_unlock(n_ptr);
			goto cont;
		}

		/* Validate message sequence number info */

		seq_no = msg_seqno(msg);
		ackd = msg_ack(msg);

		/* Release acked messages */

		if (less(n_ptr->bclink.acked, msg_bcast_ack(msg))) {
			if (tipc_node_is_up(n_ptr) && n_ptr->bclink.supported)
				tipc_bclink_acknowledge(n_ptr, msg_bcast_ack(msg));
		}

		crs = l_ptr->first_out;
		while ((crs != l_ptr->next_out) &&
		       less_eq(msg_seqno(buf_msg(crs)), ackd)) {
			struct sk_buff *next = crs->next;

			buf_discard(crs);
			crs = next;
			released++;
		}
		if (released) {
			l_ptr->first_out = crs;
			l_ptr->out_queue_size -= released;
		}

		/* Try sending any messages link endpoint has pending */

		if (unlikely(l_ptr->next_out))
			tipc_link_push_queue(l_ptr);
		if (unlikely(!list_empty(&l_ptr->waiting_ports)))
			tipc_link_wakeup_ports(l_ptr, 0);
		if (unlikely(++l_ptr->unacked_window >= TIPC_MIN_LINK_WIN)) {
			l_ptr->stats.sent_acks++;
			tipc_link_send_proto_msg(l_ptr, STATE_MSG, 0, 0, 0, 0, 0);
		}

		/* Now (finally!) process the incoming message */

protocol_check:
		if (likely(link_working_working(l_ptr))) {
			if (likely(seq_no == mod(l_ptr->next_in_no))) {
				l_ptr->next_in_no++;
				if (unlikely(l_ptr->oldest_deferred_in))
					head = link_insert_deferred_queue(l_ptr,
									  head);
				if (likely(msg_is_dest(msg, tipc_own_addr))) {
deliver:
					if (likely(msg_isdata(msg))) {
						tipc_node_unlock(n_ptr);
						tipc_port_recv_msg(buf);
						continue;
					}
					switch (msg_user(msg)) {
					case MSG_BUNDLER:
						l_ptr->stats.recv_bundles++;
						l_ptr->stats.recv_bundled +=
							msg_msgcnt(msg);
						tipc_node_unlock(n_ptr);
						tipc_link_recv_bundle(buf);
						continue;
					case ROUTE_DISTRIBUTOR:
						tipc_node_unlock(n_ptr);
						tipc_cltr_recv_routing_table(buf);
						continue;
					case NAME_DISTRIBUTOR:
						tipc_node_unlock(n_ptr);
						tipc_named_recv(buf);
						continue;
					case CONN_MANAGER:
						tipc_node_unlock(n_ptr);
						tipc_port_recv_proto_msg(buf);
						continue;
					case MSG_FRAGMENTER:
						l_ptr->stats.recv_fragments++;
						if (tipc_link_recv_fragment(&l_ptr->defragm_buf,
									    &buf, &msg)) {
							l_ptr->stats.recv_fragmented++;
							goto deliver;
						}
						break;
					case CHANGEOVER_PROTOCOL:
						type = msg_type(msg);
						if (link_recv_changeover_msg(&l_ptr, &buf)) {
							msg = buf_msg(buf);
							seq_no = msg_seqno(msg);
							if (type == ORIGINAL_MSG)
								goto deliver;
							goto protocol_check;
						}
						break;
					}
				}
				tipc_node_unlock(n_ptr);
				tipc_net_route_msg(buf);
				continue;
			}
			link_handle_out_of_seq_msg(l_ptr, buf);
			head = link_insert_deferred_queue(l_ptr, head);
			tipc_node_unlock(n_ptr);
			continue;
		}

		if (msg_user(msg) == LINK_PROTOCOL) {
			link_recv_proto_msg(l_ptr, buf);
			head = link_insert_deferred_queue(l_ptr, head);
			tipc_node_unlock(n_ptr);
			continue;
		}
		msg_dbg(msg,"NSEQ<REC<");
		link_state_event(l_ptr, TRAFFIC_MSG_EVT);

		if (link_working_working(l_ptr)) {
			/* Re-insert in front of queue */
			msg_dbg(msg,"RECV-REINS:");
			buf->next = head;
			head = buf;
			tipc_node_unlock(n_ptr);
			continue;
		}
		tipc_node_unlock(n_ptr);
cont:
		buf_discard(buf);
	}
	read_unlock_bh(&tipc_net_lock);
}

/*
 * link_defer_buf(): Sort a received out-of-sequence packet
 *                   into the deferred reception queue.
 * Returns the increase of the queue length,i.e. 0 or 1
 */

u32 tipc_link_defer_pkt(struct sk_buff **head,
			struct sk_buff **tail,
			struct sk_buff *buf)
{
	struct sk_buff *prev = NULL;
	struct sk_buff *crs = *head;
	u32 seq_no = msg_seqno(buf_msg(buf));

	buf->next = NULL;

	/* Empty queue ? */
	if (*head == NULL) {
		*head = *tail = buf;
		return 1;
	}

	/* Last ? */
	if (less(msg_seqno(buf_msg(*tail)), seq_no)) {
		(*tail)->next = buf;
		*tail = buf;
		return 1;
	}

	/* Scan through queue and sort it in */
	do {
		struct tipc_msg *msg = buf_msg(crs);

		if (less(seq_no, msg_seqno(msg))) {
			buf->next = crs;
			if (prev)
				prev->next = buf;
			else
				*head = buf;
			return 1;
		}
		if (seq_no == msg_seqno(msg)) {
			break;
		}
		prev = crs;
		crs = crs->next;
	}
	while (crs);

	/* Message is a duplicate of an existing message */

	buf_discard(buf);
	return 0;
}

/**
 * link_handle_out_of_seq_msg - handle arrival of out-of-sequence packet
 */

static void link_handle_out_of_seq_msg(struct link *l_ptr,
				       struct sk_buff *buf)
{
	u32 seq_no = msg_seqno(buf_msg(buf));

	if (likely(msg_user(buf_msg(buf)) == LINK_PROTOCOL)) {
		link_recv_proto_msg(l_ptr, buf);
		return;
	}

	dbg("rx OOS msg: seq_no %u, expecting %u (%u)\n",
	    seq_no, mod(l_ptr->next_in_no), l_ptr->next_in_no);

	/* Record OOS packet arrival (force mismatch on next timeout) */

	l_ptr->checkpoint--;

	/*
	 * Discard packet if a duplicate; otherwise add it to deferred queue
	 * and notify peer of gap as per protocol specification
	 */

	if (less(seq_no, mod(l_ptr->next_in_no))) {
		l_ptr->stats.duplicates++;
		buf_discard(buf);
		return;
	}

	if (tipc_link_defer_pkt(&l_ptr->oldest_deferred_in,
				&l_ptr->newest_deferred_in, buf)) {
		l_ptr->deferred_inqueue_sz++;
		l_ptr->stats.deferred_recv++;
		if ((l_ptr->deferred_inqueue_sz % 16) == 1)
			tipc_link_send_proto_msg(l_ptr, STATE_MSG, 0, 0, 0, 0, 0);
	} else
		l_ptr->stats.duplicates++;
 * @n: pointer to associated node
 * @if_name: associated interface name
 * @bearer_id: id (index) of associated bearer
 * @tolerance: link tolerance to be used by link
 * @net_plane: network plane (A,B,c..) this link belongs to
 * @mtu: mtu to be advertised by link
 * @priority: priority to be used by link
 * @window: send window to be used by link
 * @session: session to be used by link
 * @ownnode: identity of own node
 * @peer: node id of peer node
 * @peer_caps: bitmap describing peer node capabilities
 * @bc_sndlink: the namespace global link used for broadcast sending
 * @bc_rcvlink: the peer specific link used for broadcast reception
 * @inputq: queue to put messages ready for delivery
 * @namedq: queue to put binding table update messages ready for delivery
 * @link: return value, pointer to put the created link
 *
 * Returns true if link was created, otherwise false
 */
bool tipc_link_create(struct net *net, char *if_name, int bearer_id,
		      int tolerance, char net_plane, u32 mtu, int priority,
		      int window, u32 session, u32 ownnode, u32 peer,
		      u16 peer_caps,
		      struct tipc_link *bc_sndlink,
		      struct tipc_link *bc_rcvlink,
		      struct sk_buff_head *inputq,
		      struct sk_buff_head *namedq,
		      struct tipc_link **link)
{
	struct tipc_link *l;
	struct tipc_msg *hdr;

	l = kzalloc(sizeof(*l), GFP_ATOMIC);
	if (!l)
		return false;
	*link = l;
	l->pmsg = (struct tipc_msg *)&l->proto_msg;
	hdr = l->pmsg;
	tipc_msg_init(ownnode, hdr, LINK_PROTOCOL, RESET_MSG, INT_H_SIZE, peer);
	msg_set_size(hdr, sizeof(l->proto_msg));
	msg_set_session(hdr, session);
	msg_set_bearer_id(hdr, l->bearer_id);

	/* Note: peer i/f name is completed by reset/activate message */
	sprintf(l->name, "%u.%u.%u:%s-%u.%u.%u:unknown",
		tipc_zone(ownnode), tipc_cluster(ownnode), tipc_node(ownnode),
		if_name, tipc_zone(peer), tipc_cluster(peer), tipc_node(peer));
	strcpy((char *)msg_data(hdr), if_name);

	l->addr = peer;
	l->peer_caps = peer_caps;
	l->net = net;
	l->peer_session = WILDCARD_SESSION;
	l->bearer_id = bearer_id;
	l->tolerance = tolerance;
	l->net_plane = net_plane;
	l->advertised_mtu = mtu;
	l->mtu = mtu;
	l->priority = priority;
	tipc_link_set_queue_limits(l, window);
	l->ackers = 1;
	l->bc_sndlink = bc_sndlink;
	l->bc_rcvlink = bc_rcvlink;
	l->inputq = inputq;
	l->namedq = namedq;
	l->state = LINK_RESETTING;
	__skb_queue_head_init(&l->transmq);
	__skb_queue_head_init(&l->backlogq);
	__skb_queue_head_init(&l->deferdq);
	skb_queue_head_init(&l->wakeupq);
	skb_queue_head_init(l->inputq);
	return true;
}

/**
 * tipc_link_bc_create - create new link to be used for broadcast
 * @n: pointer to associated node
 * @mtu: mtu to be used
 * @window: send window to be used
 * @inputq: queue to put messages ready for delivery
 * @namedq: queue to put binding table update messages ready for delivery
 * @link: return value, pointer to put the created link
 *
 * Returns true if link was created, otherwise false
 */
bool tipc_link_bc_create(struct net *net, u32 ownnode, u32 peer,
			 int mtu, int window, u16 peer_caps,
			 struct sk_buff_head *inputq,
			 struct sk_buff_head *namedq,
			 struct tipc_link *bc_sndlink,
			 struct tipc_link **link)
{
	struct tipc_link *l;

	if (!tipc_link_create(net, "", MAX_BEARERS, 0, 'Z', mtu, 0, window,
			      0, ownnode, peer, peer_caps, bc_sndlink,
			      NULL, inputq, namedq, link))
		return false;

	l = *link;
	strcpy(l->name, tipc_bclink_name);
	tipc_link_reset(l);
	l->state = LINK_RESET;
	l->ackers = 0;
	l->bc_rcvlink = l;

	/* Broadcast send link is always up */
	if (link_is_bc_sndlink(l))
		l->state = LINK_ESTABLISHED;

	return true;
}

/**
 * tipc_link_fsm_evt - link finite state machine
 * @l: pointer to link
 * @evt: state machine event to be processed
 */
int tipc_link_fsm_evt(struct tipc_link *l, int evt)
{
	int rc = 0;

	switch (l->state) {
	case LINK_RESETTING:
		switch (evt) {
		case LINK_PEER_RESET_EVT:
			l->state = LINK_PEER_RESET;
			break;
		case LINK_RESET_EVT:
			l->state = LINK_RESET;
			break;
		case LINK_FAILURE_EVT:
		case LINK_FAILOVER_BEGIN_EVT:
		case LINK_ESTABLISH_EVT:
		case LINK_FAILOVER_END_EVT:
		case LINK_SYNCH_BEGIN_EVT:
		case LINK_SYNCH_END_EVT:
		default:
			goto illegal_evt;
		}
		break;
	case LINK_RESET:
		switch (evt) {
		case LINK_PEER_RESET_EVT:
			l->state = LINK_ESTABLISHING;
			break;
		case LINK_FAILOVER_BEGIN_EVT:
			l->state = LINK_FAILINGOVER;
		case LINK_FAILURE_EVT:
		case LINK_RESET_EVT:
		case LINK_ESTABLISH_EVT:
		case LINK_FAILOVER_END_EVT:
			break;
		case LINK_SYNCH_BEGIN_EVT:
		case LINK_SYNCH_END_EVT:
		default:
			goto illegal_evt;
		}
		break;
	case LINK_PEER_RESET:
		switch (evt) {
		case LINK_RESET_EVT:
			l->state = LINK_ESTABLISHING;
			break;
		case LINK_PEER_RESET_EVT:
		case LINK_ESTABLISH_EVT:
		case LINK_FAILURE_EVT:
			break;
		case LINK_SYNCH_BEGIN_EVT:
		case LINK_SYNCH_END_EVT:
		case LINK_FAILOVER_BEGIN_EVT:
		case LINK_FAILOVER_END_EVT:
		default:
			goto illegal_evt;
		}
		break;
	case LINK_FAILINGOVER:
		switch (evt) {
		case LINK_FAILOVER_END_EVT:
			l->state = LINK_RESET;
			break;
		case LINK_PEER_RESET_EVT:
		case LINK_RESET_EVT:
		case LINK_ESTABLISH_EVT:
		case LINK_FAILURE_EVT:
			break;
		case LINK_FAILOVER_BEGIN_EVT:
		case LINK_SYNCH_BEGIN_EVT:
		case LINK_SYNCH_END_EVT:
		default:
			goto illegal_evt;
		}
		break;
	case LINK_ESTABLISHING:
		switch (evt) {
		case LINK_ESTABLISH_EVT:
			l->state = LINK_ESTABLISHED;
			break;
		case LINK_FAILOVER_BEGIN_EVT:
			l->state = LINK_FAILINGOVER;
			break;
		case LINK_RESET_EVT:
			l->state = LINK_RESET;
			break;
		case LINK_FAILURE_EVT:
		case LINK_PEER_RESET_EVT:
		case LINK_SYNCH_BEGIN_EVT:
		case LINK_FAILOVER_END_EVT:
			break;
		case LINK_SYNCH_END_EVT:
		default:
			goto illegal_evt;
		}
		break;
	case LINK_ESTABLISHED:
		switch (evt) {
		case LINK_PEER_RESET_EVT:
			l->state = LINK_PEER_RESET;
			rc |= TIPC_LINK_DOWN_EVT;
			break;
		case LINK_FAILURE_EVT:
			l->state = LINK_RESETTING;
			rc |= TIPC_LINK_DOWN_EVT;
			break;
		case LINK_RESET_EVT:
			l->state = LINK_RESET;
			break;
		case LINK_ESTABLISH_EVT:
		case LINK_SYNCH_END_EVT:
			break;
		case LINK_SYNCH_BEGIN_EVT:
			l->state = LINK_SYNCHING;
			break;
		case LINK_FAILOVER_BEGIN_EVT:
		case LINK_FAILOVER_END_EVT:
		default:
			goto illegal_evt;
		}
		break;
	case LINK_SYNCHING:
		switch (evt) {
		case LINK_PEER_RESET_EVT:
			l->state = LINK_PEER_RESET;
			rc |= TIPC_LINK_DOWN_EVT;
			break;
		case LINK_FAILURE_EVT:
			l->state = LINK_RESETTING;
			rc |= TIPC_LINK_DOWN_EVT;
			break;
		case LINK_RESET_EVT:
			l->state = LINK_RESET;
			break;
		case LINK_ESTABLISH_EVT:
		case LINK_SYNCH_BEGIN_EVT:
			break;
		case LINK_SYNCH_END_EVT:
			l->state = LINK_ESTABLISHED;
			break;
		case LINK_FAILOVER_BEGIN_EVT:
		case LINK_FAILOVER_END_EVT:
		default:
			goto illegal_evt;
		}
		break;
	default:
		pr_err("Unknown FSM state %x in %s\n", l->state, l->name);
	}
	return rc;
illegal_evt:
	pr_err("Illegal FSM event %x in state %x on link %s\n",
	       evt, l->state, l->name);
	return rc;
}

/* link_profile_stats - update statistical profiling of traffic
 */
static void link_profile_stats(struct tipc_link *l)
{
	struct sk_buff *skb;
	struct tipc_msg *msg;
	int length;

	/* Update counters used in statistical profiling of send traffic */
	l->stats.accu_queue_sz += skb_queue_len(&l->transmq);
	l->stats.queue_sz_counts++;

	skb = skb_peek(&l->transmq);
	if (!skb)
		return;
	msg = buf_msg(skb);
	length = msg_size(msg);

	if (msg_user(msg) == MSG_FRAGMENTER) {
		if (msg_type(msg) != FIRST_FRAGMENT)
			return;
		length = msg_size(msg_get_wrapped(msg));
	}
	l->stats.msg_lengths_total += length;
	l->stats.msg_length_counts++;
	if (length <= 64)
		l->stats.msg_length_profile[0]++;
	else if (length <= 256)
		l->stats.msg_length_profile[1]++;
	else if (length <= 1024)
		l->stats.msg_length_profile[2]++;
	else if (length <= 4096)
		l->stats.msg_length_profile[3]++;
	else if (length <= 16384)
		l->stats.msg_length_profile[4]++;
	else if (length <= 32768)
		l->stats.msg_length_profile[5]++;
	else
		l->stats.msg_length_profile[6]++;
}

/* tipc_link_timeout - perform periodic task as instructed from node timeout
 */
/* tipc_link_timeout - perform periodic task as instructed from node timeout
 */
int tipc_link_timeout(struct tipc_link *l, struct sk_buff_head *xmitq)
{
	int rc = 0;
	int mtyp = STATE_MSG;
	bool xmit = false;
	bool prb = false;
	u16 bc_snt = l->bc_sndlink->snd_nxt - 1;
	u16 bc_acked = l->bc_rcvlink->acked;
	bool bc_up = link_is_up(l->bc_rcvlink);

	link_profile_stats(l);

	switch (l->state) {
	case LINK_ESTABLISHED:
	case LINK_SYNCHING:
		if (!l->silent_intv_cnt) {
			if (bc_up && (bc_acked != bc_snt))
				xmit = true;
		} else if (l->silent_intv_cnt <= l->abort_limit) {
			xmit = true;
			prb = true;
		} else {
			rc |= tipc_link_fsm_evt(l, LINK_FAILURE_EVT);
		}
		l->silent_intv_cnt++;
		break;
	case LINK_RESET:
		xmit = true;
		mtyp = RESET_MSG;
		break;
	case LINK_ESTABLISHING:
		xmit = true;
		mtyp = ACTIVATE_MSG;
		break;
	case LINK_PEER_RESET:
	case LINK_RESETTING:
	case LINK_FAILINGOVER:
		break;
	default:
		break;
	}

	if (xmit)
		tipc_link_build_proto_msg(l, mtyp, prb, 0, 0, 0, xmitq);

	return rc;
}

/**
 * link_schedule_user - schedule a message sender for wakeup after congestion
 * @link: congested link
 * @list: message that was attempted sent
 * Create pseudo msg to send back to user when congestion abates
 * Does not consume buffer list
 */
static int link_schedule_user(struct tipc_link *link, struct sk_buff_head *list)
{
	struct tipc_msg *msg = buf_msg(skb_peek(list));
	int imp = msg_importance(msg);
	u32 oport = msg_origport(msg);
	u32 addr = link_own_addr(link);
	struct sk_buff *skb;

	/* This really cannot happen...  */
	if (unlikely(imp > TIPC_CRITICAL_IMPORTANCE)) {
		pr_warn("%s<%s>, send queue full", link_rst_msg, link->name);
		return -ENOBUFS;
	}
	/* Non-blocking sender: */
	if (TIPC_SKB_CB(skb_peek(list))->wakeup_pending)
		return -ELINKCONG;

	/* Create and schedule wakeup pseudo message */
	skb = tipc_msg_create(SOCK_WAKEUP, 0, INT_H_SIZE, 0,
			      addr, addr, oport, 0, 0);
	if (!skb)
		return -ENOBUFS;
	TIPC_SKB_CB(skb)->chain_sz = skb_queue_len(list);
	TIPC_SKB_CB(skb)->chain_imp = imp;
	skb_queue_tail(&link->wakeupq, skb);
	link->stats.link_congs++;
	return -ELINKCONG;
}

/**
 * link_prepare_wakeup - prepare users for wakeup after congestion
 * @link: congested link
 * Move a number of waiting users, as permitted by available space in
 * the send queue, from link wait queue to node wait queue for wakeup
 */
void link_prepare_wakeup(struct tipc_link *l)
{
	int pnd[TIPC_SYSTEM_IMPORTANCE + 1] = {0,};
	int imp, lim;
	struct sk_buff *skb, *tmp;

	skb_queue_walk_safe(&l->wakeupq, skb, tmp) {
		imp = TIPC_SKB_CB(skb)->chain_imp;
		lim = l->window + l->backlog[imp].limit;
		pnd[imp] += TIPC_SKB_CB(skb)->chain_sz;
		if ((pnd[imp] + l->backlog[imp].len) >= lim)
			break;
		skb_unlink(skb, &l->wakeupq);
		skb_queue_tail(l->inputq, skb);
	}
}

void tipc_link_reset(struct tipc_link *l)
{
	/* Link is down, accept any session */
	l->peer_session = WILDCARD_SESSION;

	/* If peer is up, it only accepts an incremented session number */
	msg_set_session(l->pmsg, msg_session(l->pmsg) + 1);

	/* Prepare for renewed mtu size negotiation */
	l->mtu = l->advertised_mtu;

	/* Clean up all queues and counters: */
	__skb_queue_purge(&l->transmq);
	__skb_queue_purge(&l->deferdq);
	skb_queue_splice_init(&l->wakeupq, l->inputq);
	__skb_queue_purge(&l->backlogq);
	l->backlog[TIPC_LOW_IMPORTANCE].len = 0;
	l->backlog[TIPC_MEDIUM_IMPORTANCE].len = 0;
	l->backlog[TIPC_HIGH_IMPORTANCE].len = 0;
	l->backlog[TIPC_CRITICAL_IMPORTANCE].len = 0;
	l->backlog[TIPC_SYSTEM_IMPORTANCE].len = 0;
	kfree_skb(l->reasm_buf);
	kfree_skb(l->failover_reasm_skb);
	l->reasm_buf = NULL;
	l->failover_reasm_skb = NULL;
	l->rcv_unacked = 0;
	l->snd_nxt = 1;
	l->rcv_nxt = 1;
	l->acked = 0;
	l->silent_intv_cnt = 0;
	l->stats.recv_info = 0;
	l->stale_count = 0;
	l->bc_peer_is_up = false;
	link_reset_statistics(l);
}

/**
 * tipc_link_xmit(): enqueue buffer list according to queue situation
 * @link: link to use
 * @list: chain of buffers containing message
 * @xmitq: returned list of packets to be sent by caller
 *
 * Consumes the buffer chain, except when returning -ELINKCONG,
 * since the caller then may want to make more send attempts.
 * Returns 0 if success, or errno: -ELINKCONG, -EMSGSIZE or -ENOBUFS
 * Messages at TIPC_SYSTEM_IMPORTANCE are always accepted
 */
int tipc_link_xmit(struct tipc_link *l, struct sk_buff_head *list,
		   struct sk_buff_head *xmitq)
{
	struct tipc_msg *hdr = buf_msg(skb_peek(list));
	unsigned int maxwin = l->window;
	unsigned int i, imp = msg_importance(hdr);
	unsigned int mtu = l->mtu;
	u16 ack = l->rcv_nxt - 1;
	u16 seqno = l->snd_nxt;
	u16 bc_ack = l->bc_rcvlink->rcv_nxt - 1;
	struct sk_buff_head *transmq = &l->transmq;
	struct sk_buff_head *backlogq = &l->backlogq;
	struct sk_buff *skb, *_skb, *bskb;

	/* Match msg importance against this and all higher backlog limits: */
	for (i = imp; i <= TIPC_SYSTEM_IMPORTANCE; i++) {
		if (unlikely(l->backlog[i].len >= l->backlog[i].limit))
			return link_schedule_user(l, list);
	}
	if (unlikely(msg_size(hdr) > mtu))
		return -EMSGSIZE;

	/* Prepare each packet for sending, and add to relevant queue: */
	while (skb_queue_len(list)) {
		skb = skb_peek(list);
		hdr = buf_msg(skb);
		msg_set_seqno(hdr, seqno);
		msg_set_ack(hdr, ack);
		msg_set_bcast_ack(hdr, bc_ack);

		if (likely(skb_queue_len(transmq) < maxwin)) {
			_skb = skb_clone(skb, GFP_ATOMIC);
			if (!_skb)
				return -ENOBUFS;
			__skb_dequeue(list);
			__skb_queue_tail(transmq, skb);
			__skb_queue_tail(xmitq, _skb);
			TIPC_SKB_CB(skb)->ackers = l->ackers;
			l->rcv_unacked = 0;
			seqno++;
			continue;
		}
		if (tipc_msg_bundle(skb_peek_tail(backlogq), hdr, mtu)) {
			kfree_skb(__skb_dequeue(list));
			l->stats.sent_bundled++;
			continue;
		}
		if (tipc_msg_make_bundle(&bskb, hdr, mtu, l->addr)) {
			kfree_skb(__skb_dequeue(list));
			__skb_queue_tail(backlogq, bskb);
			l->backlog[msg_importance(buf_msg(bskb))].len++;
			l->stats.sent_bundled++;
			l->stats.sent_bundles++;
			continue;
		}
		l->backlog[imp].len += skb_queue_len(list);
		skb_queue_splice_tail_init(list, backlogq);
	}
	l->snd_nxt = seqno;
	return 0;
}

void tipc_link_advance_backlog(struct tipc_link *l, struct sk_buff_head *xmitq)
{
	struct sk_buff *skb, *_skb;
	struct tipc_msg *hdr;
	u16 seqno = l->snd_nxt;
	u16 ack = l->rcv_nxt - 1;
	u16 bc_ack = l->bc_rcvlink->rcv_nxt - 1;

	while (skb_queue_len(&l->transmq) < l->window) {
		skb = skb_peek(&l->backlogq);
		if (!skb)
			break;
		_skb = skb_clone(skb, GFP_ATOMIC);
		if (!_skb)
			break;
		__skb_dequeue(&l->backlogq);
		hdr = buf_msg(skb);
		l->backlog[msg_importance(hdr)].len--;
		__skb_queue_tail(&l->transmq, skb);
		__skb_queue_tail(xmitq, _skb);
		TIPC_SKB_CB(skb)->ackers = l->ackers;
		msg_set_seqno(hdr, seqno);
		msg_set_ack(hdr, ack);
		msg_set_bcast_ack(hdr, bc_ack);
		l->rcv_unacked = 0;
		seqno++;
	}
	l->snd_nxt = seqno;
}

static void link_retransmit_failure(struct tipc_link *l, struct sk_buff *skb)
{
	struct tipc_msg *hdr = buf_msg(skb);

	pr_warn("Retransmission failure on link <%s>\n", l->name);
	link_print(l, "Resetting link ");
	pr_info("Failed msg: usr %u, typ %u, len %u, err %u\n",
		msg_user(hdr), msg_type(hdr), msg_size(hdr), msg_errcode(hdr));
	pr_info("sqno %u, prev: %x, src: %x\n",
		msg_seqno(hdr), msg_prevnode(hdr), msg_orignode(hdr));
}

int tipc_link_retrans(struct tipc_link *l, u16 from, u16 to,
		      struct sk_buff_head *xmitq)
{
	struct sk_buff *_skb, *skb = skb_peek(&l->transmq);
	struct tipc_msg *hdr;
	u16 ack = l->rcv_nxt - 1;
	u16 bc_ack = l->bc_rcvlink->rcv_nxt - 1;

	if (!skb)
		return 0;

	/* Detect repeated retransmit failures on same packet */
	if (likely(l->last_retransm != buf_seqno(skb))) {
		l->last_retransm = buf_seqno(skb);
		l->stale_count = 1;
	} else if (++l->stale_count > 100) {
		link_retransmit_failure(l, skb);
		return tipc_link_fsm_evt(l, LINK_FAILURE_EVT);
	}

	/* Move forward to where retransmission should start */
	skb_queue_walk(&l->transmq, skb) {
		if (!less(buf_seqno(skb), from))
			break;
	}

	skb_queue_walk_from(&l->transmq, skb) {
		if (more(buf_seqno(skb), to))
			break;
		hdr = buf_msg(skb);
		_skb = __pskb_copy(skb, MIN_H_SIZE, GFP_ATOMIC);
		if (!_skb)
			return 0;
		hdr = buf_msg(_skb);
		msg_set_ack(hdr, ack);
		msg_set_bcast_ack(hdr, bc_ack);
		_skb->priority = TC_PRIO_CONTROL;
		__skb_queue_tail(xmitq, _skb);
		l->stats.retransmitted++;
	}
	return 0;
}

/* tipc_data_input - deliver data and name distr msgs to upper layer
 *
 * Consumes buffer if message is of right type
 * Node lock must be held
 */
static bool tipc_data_input(struct tipc_link *l, struct sk_buff *skb,
			    struct sk_buff_head *inputq)
{
	switch (msg_user(buf_msg(skb))) {
	case TIPC_LOW_IMPORTANCE:
	case TIPC_MEDIUM_IMPORTANCE:
	case TIPC_HIGH_IMPORTANCE:
	case TIPC_CRITICAL_IMPORTANCE:
	case CONN_MANAGER:
		skb_queue_tail(inputq, skb);
		return true;
	case NAME_DISTRIBUTOR:
		l->bc_rcvlink->state = LINK_ESTABLISHED;
		skb_queue_tail(l->namedq, skb);
		return true;
	case MSG_BUNDLER:
	case TUNNEL_PROTOCOL:
	case MSG_FRAGMENTER:
	case BCAST_PROTOCOL:
		return false;
	default:
		pr_warn("Dropping received illegal msg type\n");
		kfree_skb(skb);
		return true;
	};
}

/* tipc_link_input - process packet that has passed link protocol check
 *
 * Consumes buffer
 */
static int tipc_link_input(struct tipc_link *l, struct sk_buff *skb,
			   struct sk_buff_head *inputq)
{
	struct tipc_msg *hdr = buf_msg(skb);
	struct sk_buff **reasm_skb = &l->reasm_buf;
	struct sk_buff *iskb;
	struct sk_buff_head tmpq;
	int usr = msg_user(hdr);
	int rc = 0;
	int pos = 0;
	int ipos = 0;

	if (unlikely(usr == TUNNEL_PROTOCOL)) {
		if (msg_type(hdr) == SYNCH_MSG) {
			__skb_queue_purge(&l->deferdq);
			goto drop;
		}
		if (!tipc_msg_extract(skb, &iskb, &ipos))
			return rc;
		kfree_skb(skb);
		skb = iskb;
		hdr = buf_msg(skb);
		if (less(msg_seqno(hdr), l->drop_point))
			goto drop;
		if (tipc_data_input(l, skb, inputq))
			return rc;
		usr = msg_user(hdr);
		reasm_skb = &l->failover_reasm_skb;
	}

	if (usr == MSG_BUNDLER) {
		skb_queue_head_init(&tmpq);
		l->stats.recv_bundles++;
		l->stats.recv_bundled += msg_msgcnt(hdr);
		while (tipc_msg_extract(skb, &iskb, &pos))
			tipc_data_input(l, iskb, &tmpq);
		tipc_skb_queue_splice_tail(&tmpq, inputq);
		return 0;
	} else if (usr == MSG_FRAGMENTER) {
		l->stats.recv_fragments++;
		if (tipc_buf_append(reasm_skb, &skb)) {
			l->stats.recv_fragmented++;
			tipc_data_input(l, skb, inputq);
		} else if (!*reasm_skb && !link_is_bc_rcvlink(l)) {
			pr_warn_ratelimited("Unable to build fragment list\n");
			return tipc_link_fsm_evt(l, LINK_FAILURE_EVT);
		}
		return 0;
	} else if (usr == BCAST_PROTOCOL) {
		tipc_bcast_lock(l->net);
		tipc_link_bc_init_rcv(l->bc_rcvlink, hdr);
		tipc_bcast_unlock(l->net);
	}
drop:
	kfree_skb(skb);
	return 0;
}

static bool tipc_link_release_pkts(struct tipc_link *l, u16 acked)
{
	bool released = false;
	struct sk_buff *skb, *tmp;

	skb_queue_walk_safe(&l->transmq, skb, tmp) {
		if (more(buf_seqno(skb), acked))
			break;
		__skb_unlink(skb, &l->transmq);
		kfree_skb(skb);
		released = true;
	}
	return released;
}

/* tipc_link_build_ack_msg: prepare link acknowledge message for transmission
 *
 * Note that sending of broadcast ack is coordinated among nodes, to reduce
 * risk of ack storms towards the sender
 */
int tipc_link_build_ack_msg(struct tipc_link *l, struct sk_buff_head *xmitq)
{
	if (!l)
		return 0;

	/* Broadcast ACK must be sent via a unicast link => defer to caller */
	if (link_is_bc_rcvlink(l)) {
		if (((l->rcv_nxt ^ link_own_addr(l)) & 0xf) != 0xf)
			return 0;
		l->rcv_unacked = 0;
		return TIPC_LINK_SND_BC_ACK;
	}

	/* Unicast ACK */
	l->rcv_unacked = 0;
	l->stats.sent_acks++;
	tipc_link_build_proto_msg(l, STATE_MSG, 0, 0, 0, 0, xmitq);
	return 0;
}

/* tipc_link_build_reset_msg: prepare link RESET or ACTIVATE message
 */
void tipc_link_build_reset_msg(struct tipc_link *l, struct sk_buff_head *xmitq)
{
	int mtyp = RESET_MSG;

	if (l->state == LINK_ESTABLISHING)
		mtyp = ACTIVATE_MSG;

	tipc_link_build_proto_msg(l, mtyp, 0, 0, 0, 0, xmitq);
}

/* tipc_link_build_nack_msg: prepare link nack message for transmission
 */
static void tipc_link_build_nack_msg(struct tipc_link *l,
				     struct sk_buff_head *xmitq)
{
	u32 def_cnt = ++l->stats.deferred_recv;

	if (link_is_bc_rcvlink(l))
		return;

	if ((skb_queue_len(&l->deferdq) == 1) || !(def_cnt % TIPC_NACK_INTV))
		tipc_link_build_proto_msg(l, STATE_MSG, 0, 0, 0, 0, xmitq);
}

/* tipc_link_rcv - process TIPC packets/messages arriving from off-node
 * @l: the link that should handle the message
 * @skb: TIPC packet
 * @xmitq: queue to place packets to be sent after this call
 */
int tipc_link_rcv(struct tipc_link *l, struct sk_buff *skb,
		  struct sk_buff_head *xmitq)
{
	struct sk_buff_head *defq = &l->deferdq;
	struct tipc_msg *hdr;
	u16 seqno, rcv_nxt, win_lim;
	int rc = 0;

	do {
		hdr = buf_msg(skb);
		seqno = msg_seqno(hdr);
		rcv_nxt = l->rcv_nxt;
		win_lim = rcv_nxt + TIPC_MAX_LINK_WIN;

		/* Verify and update link state */
		if (unlikely(msg_user(hdr) == LINK_PROTOCOL))
			return tipc_link_proto_rcv(l, skb, xmitq);

		if (unlikely(!link_is_up(l))) {
			if (l->state == LINK_ESTABLISHING)
				rc = TIPC_LINK_UP_EVT;
			goto drop;
		}

		/* Don't send probe at next timeout expiration */
		l->silent_intv_cnt = 0;

		/* Drop if outside receive window */
		if (unlikely(less(seqno, rcv_nxt) || more(seqno, win_lim))) {
			l->stats.duplicates++;
			goto drop;
		}

		/* Forward queues and wake up waiting users */
		if (likely(tipc_link_release_pkts(l, msg_ack(hdr)))) {
			tipc_link_advance_backlog(l, xmitq);
			if (unlikely(!skb_queue_empty(&l->wakeupq)))
				link_prepare_wakeup(l);
		}

		/* Defer delivery if sequence gap */
		if (unlikely(seqno != rcv_nxt)) {
			__tipc_skb_queue_sorted(defq, seqno, skb);
			tipc_link_build_nack_msg(l, xmitq);
			break;
		}

		/* Deliver packet */
		l->rcv_nxt++;
		l->stats.recv_info++;
		if (!tipc_data_input(l, skb, l->inputq))
			rc |= tipc_link_input(l, skb, l->inputq);
		if (unlikely(++l->rcv_unacked >= TIPC_MIN_LINK_WIN))
			rc |= tipc_link_build_ack_msg(l, xmitq);
		if (unlikely(rc & ~TIPC_LINK_SND_BC_ACK))
			break;
	} while ((skb = __skb_dequeue(defq)));

	return rc;
drop:
	kfree_skb(skb);
	return rc;
}

/*
 * Send protocol message to the other endpoint.
 */
void tipc_link_send_proto_msg(struct link *l_ptr, u32 msg_typ, int probe_msg,
			      u32 gap, u32 tolerance, u32 priority, u32 ack_mtu)
{
	struct sk_buff *buf = NULL;
	struct tipc_msg *msg = l_ptr->pmsg;
	u32 msg_size = sizeof(l_ptr->proto_msg);

	if (link_blocked(l_ptr))
		return;
	msg_set_type(msg, msg_typ);
	msg_set_net_plane(msg, l_ptr->b_ptr->net_plane);
	msg_set_bcast_ack(msg, mod(l_ptr->owner->bclink.last_in));
	msg_set_last_bcast(msg, tipc_bclink_get_last_sent());

	if (msg_typ == STATE_MSG) {
		u32 next_sent = mod(l_ptr->next_out_no);

		if (!tipc_link_is_up(l_ptr))
			return;
		if (l_ptr->next_out)
			next_sent = msg_seqno(buf_msg(l_ptr->next_out));
		msg_set_next_sent(msg, next_sent);
		if (l_ptr->oldest_deferred_in) {
			u32 rec = msg_seqno(buf_msg(l_ptr->oldest_deferred_in));
			gap = mod(rec - mod(l_ptr->next_in_no));
		}
		msg_set_seq_gap(msg, gap);
		if (gap)
			l_ptr->stats.sent_nacks++;
		msg_set_link_tolerance(msg, tolerance);
		msg_set_linkprio(msg, priority);
		msg_set_max_pkt(msg, ack_mtu);
		msg_set_ack(msg, mod(l_ptr->next_in_no - 1));
		msg_set_probe(msg, probe_msg != 0);
		if (probe_msg) {
			u32 mtu = l_ptr->max_pkt;

			if ((mtu < l_ptr->max_pkt_target) &&
			    link_working_working(l_ptr) &&
			    l_ptr->fsm_msg_cnt) {
				msg_size = (mtu + (l_ptr->max_pkt_target - mtu)/2 + 2) & ~3;
				if (l_ptr->max_pkt_probes == 10) {
					l_ptr->max_pkt_target = (msg_size - 4);
					l_ptr->max_pkt_probes = 0;
					msg_size = (mtu + (l_ptr->max_pkt_target - mtu)/2 + 2) & ~3;
				}
				l_ptr->max_pkt_probes++;
			}

			l_ptr->stats.sent_probes++;
		}
		l_ptr->stats.sent_states++;
	} else {		/* RESET_MSG or ACTIVATE_MSG */
		msg_set_ack(msg, mod(l_ptr->reset_checkpoint - 1));
		msg_set_seq_gap(msg, 0);
		msg_set_next_sent(msg, 1);
		msg_set_link_tolerance(msg, l_ptr->tolerance);
		msg_set_linkprio(msg, l_ptr->priority);
		msg_set_max_pkt(msg, l_ptr->max_pkt_target);
	}

	if (tipc_node_has_redundant_links(l_ptr->owner)) {
		msg_set_redundant_link(msg);
	} else {
		msg_clear_redundant_link(msg);
	}
	msg_set_linkprio(msg, l_ptr->priority);

	/* Ensure sequence number will not fit : */

	msg_set_seqno(msg, mod(l_ptr->next_out_no + (0xffff/2)));

	/* Congestion? */

	if (tipc_bearer_congested(l_ptr->b_ptr, l_ptr)) {
		if (!l_ptr->proto_msg_queue) {
			l_ptr->proto_msg_queue =
				buf_acquire(sizeof(l_ptr->proto_msg));
		}
		buf = l_ptr->proto_msg_queue;
		if (!buf)
			return;
		skb_copy_to_linear_data(buf, msg, sizeof(l_ptr->proto_msg));
		return;
	}
	msg_set_timestamp(msg, jiffies_to_msecs(jiffies));

	/* Message can be sent */

	msg_dbg(msg, ">>");

	buf = buf_acquire(msg_size);
	if (!buf)
		return;

	skb_copy_to_linear_data(buf, msg, sizeof(l_ptr->proto_msg));
	msg_set_size(buf_msg(buf), msg_size);

	if (tipc_bearer_send(l_ptr->b_ptr, buf, &l_ptr->media_addr)) {
		l_ptr->unacked_window = 0;
		buf_discard(buf);
		return;
	}

	/* New congestion */
	tipc_bearer_schedule(l_ptr->b_ptr, l_ptr);
	l_ptr->proto_msg_queue = buf;
	l_ptr->stats.bearer_congs++;
}

/*
 * Receive protocol message :
 * Note that network plane id propagates through the network, and may
 * change at any time. The node with lowest address rules
 */

static void link_recv_proto_msg(struct link *l_ptr, struct sk_buff *buf)
{
	u32 rec_gap = 0;
	u32 max_pkt_info;
	u32 max_pkt_ack;
	u32 msg_tol;
	struct tipc_msg *msg = buf_msg(buf);

	dbg("AT(%u):", jiffies_to_msecs(jiffies));
	msg_dbg(msg, "<<");
	if (link_blocked(l_ptr))
		goto exit;

	/* record unnumbered packet arrival (force mismatch on next timeout) */

	l_ptr->checkpoint--;

	if (l_ptr->b_ptr->net_plane != msg_net_plane(msg))
		if (tipc_own_addr > msg_prevnode(msg))
			l_ptr->b_ptr->net_plane = msg_net_plane(msg);

	l_ptr->owner->permit_changeover = msg_redundant_link(msg);

	switch (msg_type(msg)) {

	case RESET_MSG:
		if (!link_working_unknown(l_ptr) &&
		    (l_ptr->peer_session != INVALID_SESSION)) {
			if (msg_session(msg) == l_ptr->peer_session) {
				dbg("Duplicate RESET: %u<->%u\n",
				    msg_session(msg), l_ptr->peer_session);
				break; /* duplicate: ignore */
			}
		}
		/* fall thru' */
	case ACTIVATE_MSG:
		/* Update link settings according other endpoint's values */

		strcpy((strrchr(l_ptr->name, ':') + 1), (char *)msg_data(msg));

		if ((msg_tol = msg_link_tolerance(msg)) &&
		    (msg_tol > l_ptr->tolerance))
			link_set_supervision_props(l_ptr, msg_tol);

		if (msg_linkprio(msg) > l_ptr->priority)
			l_ptr->priority = msg_linkprio(msg);

		max_pkt_info = msg_max_pkt(msg);
		if (max_pkt_info) {
			if (max_pkt_info < l_ptr->max_pkt_target)
				l_ptr->max_pkt_target = max_pkt_info;
			if (l_ptr->max_pkt > l_ptr->max_pkt_target)
				l_ptr->max_pkt = l_ptr->max_pkt_target;
		} else {
			l_ptr->max_pkt = l_ptr->max_pkt_target;
		}
		l_ptr->owner->bclink.supported = (max_pkt_info != 0);

		link_state_event(l_ptr, msg_type(msg));

		l_ptr->peer_session = msg_session(msg);
		l_ptr->peer_bearer_id = msg_bearer_id(msg);

		/* Synchronize broadcast sequence numbers */
		if (!tipc_node_has_redundant_links(l_ptr->owner)) {
			l_ptr->owner->bclink.last_in = mod(msg_last_bcast(msg));
		}
		break;
	case STATE_MSG:

		if ((msg_tol = msg_link_tolerance(msg)))
			link_set_supervision_props(l_ptr, msg_tol);

		if (msg_linkprio(msg) &&
		    (msg_linkprio(msg) != l_ptr->priority)) {
			warn("Resetting link <%s>, priority change %u->%u\n",
			     l_ptr->name, l_ptr->priority, msg_linkprio(msg));
			l_ptr->priority = msg_linkprio(msg);
			tipc_link_reset(l_ptr); /* Enforce change to take effect */
			break;
		}
		link_state_event(l_ptr, TRAFFIC_MSG_EVT);
		l_ptr->stats.recv_states++;
		if (link_reset_unknown(l_ptr))
			break;

		if (less_eq(mod(l_ptr->next_in_no), msg_next_sent(msg))) {
			rec_gap = mod(msg_next_sent(msg) -
				      mod(l_ptr->next_in_no));
		}

		max_pkt_ack = msg_max_pkt(msg);
		if (max_pkt_ack > l_ptr->max_pkt) {
			dbg("Link <%s> updated MTU %u -> %u\n",
			    l_ptr->name, l_ptr->max_pkt, max_pkt_ack);
			l_ptr->max_pkt = max_pkt_ack;
			l_ptr->max_pkt_probes = 0;
		}

		max_pkt_ack = 0;
		if (msg_probe(msg)) {
			l_ptr->stats.recv_probes++;
			if (msg_size(msg) > sizeof(l_ptr->proto_msg)) {
				max_pkt_ack = msg_size(msg);
			}
		}

		/* Protocol message before retransmits, reduce loss risk */

		tipc_bclink_check_gap(l_ptr->owner, msg_last_bcast(msg));

		if (rec_gap || (msg_probe(msg))) {
			tipc_link_send_proto_msg(l_ptr, STATE_MSG,
						 0, rec_gap, 0, 0, max_pkt_ack);
		}
		if (msg_seq_gap(msg)) {
			msg_dbg(msg, "With Gap:");
			l_ptr->stats.recv_nacks++;
			tipc_link_retransmit(l_ptr, l_ptr->first_out,
					     msg_seq_gap(msg));
		}
		break;
	default:
		msg_dbg(buf_msg(buf), "<DISCARDING UNKNOWN<");
	}
exit:
	buf_discard(buf);
}


/*
 * tipc_link_tunnel(): Send one message via a link belonging to
 * another bearer. Owner node is locked.
 */
void tipc_link_tunnel(struct link *l_ptr,
		      struct tipc_msg *tunnel_hdr,
		      struct tipc_msg  *msg,
		      u32 selector)
{
	struct link *tunnel;
	struct sk_buff *buf;
	u32 length = msg_size(msg);

	tunnel = l_ptr->owner->active_links[selector & 1];
	if (!tipc_link_is_up(tunnel)) {
		warn("Link changeover error, "
		     "tunnel link no longer available\n");
		return;
	}
	msg_set_size(tunnel_hdr, length + INT_H_SIZE);
	buf = buf_acquire(length + INT_H_SIZE);
	if (!buf) {
		warn("Link changeover error, "
		     "unable to send tunnel msg\n");
		return;
	}
	skb_copy_to_linear_data(buf, tunnel_hdr, INT_H_SIZE);
	skb_copy_to_linear_data_offset(buf, INT_H_SIZE, msg, length);
	dbg("%c->%c:", l_ptr->b_ptr->net_plane, tunnel->b_ptr->net_plane);
	msg_dbg(buf_msg(buf), ">SEND>");
	tipc_link_send_buf(tunnel, buf);
}



/*
 * changeover(): Send whole message queue via the remaining link
 *               Owner node is locked.
 */

void tipc_link_changeover(struct link *l_ptr)
{
	u32 msgcount = l_ptr->out_queue_size;
	struct sk_buff *crs = l_ptr->first_out;
	struct link *tunnel = l_ptr->owner->active_links[0];
	struct tipc_msg tunnel_hdr;
	int split_bundles;

	if (!tunnel)
		return;

	if (!l_ptr->owner->permit_changeover) {
		warn("Link changeover error, "
		     "peer did not permit changeover\n");
		return;
	}

	msg_init(&tunnel_hdr, CHANGEOVER_PROTOCOL,
		 ORIGINAL_MSG, INT_H_SIZE, l_ptr->addr);
	msg_set_bearer_id(&tunnel_hdr, l_ptr->peer_bearer_id);
	msg_set_msgcnt(&tunnel_hdr, msgcount);
	dbg("Link changeover requires %u tunnel messages\n", msgcount);

	if (!l_ptr->first_out) {
		struct sk_buff *buf;

		buf = buf_acquire(INT_H_SIZE);
		if (buf) {
			skb_copy_to_linear_data(buf, &tunnel_hdr, INT_H_SIZE);
			msg_set_size(&tunnel_hdr, INT_H_SIZE);
			dbg("%c->%c:", l_ptr->b_ptr->net_plane,
			    tunnel->b_ptr->net_plane);
			msg_dbg(&tunnel_hdr, "EMPTY>SEND>");
			tipc_link_send_buf(tunnel, buf);
		} else {
			warn("Link changeover error, "
			     "unable to send changeover msg\n");
		}
		return;
	}

	split_bundles = (l_ptr->owner->active_links[0] !=
			 l_ptr->owner->active_links[1]);

	while (crs) {
		struct tipc_msg *msg = buf_msg(crs);

		if ((msg_user(msg) == MSG_BUNDLER) && split_bundles) {
			struct tipc_msg *m = msg_get_wrapped(msg);
			unchar* pos = (unchar*)m;

			msgcount = msg_msgcnt(msg);
			while (msgcount--) {
				msg_set_seqno(m,msg_seqno(msg));
				tipc_link_tunnel(l_ptr, &tunnel_hdr, m,
						 msg_link_selector(m));
				pos += align(msg_size(m));
				m = (struct tipc_msg *)pos;
			}
		} else {
			tipc_link_tunnel(l_ptr, &tunnel_hdr, msg,
					 msg_link_selector(msg));
		}
		crs = crs->next;
	}
}

void tipc_link_send_duplicate(struct link *l_ptr, struct link *tunnel)
{
	struct sk_buff *iter;
	struct tipc_msg tunnel_hdr;

	msg_init(&tunnel_hdr, CHANGEOVER_PROTOCOL,
		 DUPLICATE_MSG, INT_H_SIZE, l_ptr->addr);
	msg_set_msgcnt(&tunnel_hdr, l_ptr->out_queue_size);
	msg_set_bearer_id(&tunnel_hdr, l_ptr->peer_bearer_id);
	iter = l_ptr->first_out;
	while (iter) {
		struct sk_buff *outbuf;
		struct tipc_msg *msg = buf_msg(iter);
		u32 length = msg_size(msg);

		if (msg_user(msg) == MSG_BUNDLER)
			msg_set_type(msg, CLOSED_MSG);
		msg_set_ack(msg, mod(l_ptr->next_in_no - 1));	/* Update */
		msg_set_bcast_ack(msg, l_ptr->owner->bclink.last_in);
		msg_set_size(&tunnel_hdr, length + INT_H_SIZE);
		outbuf = buf_acquire(length + INT_H_SIZE);
		if (outbuf == NULL) {
			warn("Link changeover error, "
			     "unable to send duplicate msg\n");
			return;
		}
		skb_copy_to_linear_data(outbuf, &tunnel_hdr, INT_H_SIZE);
		skb_copy_to_linear_data_offset(outbuf, INT_H_SIZE, iter->data,
					       length);
		dbg("%c->%c:", l_ptr->b_ptr->net_plane,
		    tunnel->b_ptr->net_plane);
		msg_dbg(buf_msg(outbuf), ">SEND>");
		tipc_link_send_buf(tunnel, outbuf);
		if (!tipc_link_is_up(l_ptr))
			return;
		iter = iter->next;
	}
}



/**
 * buf_extract - extracts embedded TIPC message from another message
 * @skb: encapsulating message buffer
 * @from_pos: offset to extract from
 *
 * Returns a new message buffer containing an embedded message.  The
 * encapsulating message itself is left unchanged.
 */

static struct sk_buff *buf_extract(struct sk_buff *skb, u32 from_pos)
{
	struct tipc_msg *msg = (struct tipc_msg *)(skb->data + from_pos);
	u32 size = msg_size(msg);
	struct sk_buff *eb;

	eb = buf_acquire(size);
	if (eb)
		skb_copy_to_linear_data(eb, msg, size);
	return eb;
}

/*
 *  link_recv_changeover_msg(): Receive tunneled packet sent
 *  via other link. Node is locked. Return extracted buffer.
 */

static int link_recv_changeover_msg(struct link **l_ptr,
				    struct sk_buff **buf)
{
	struct sk_buff *tunnel_buf = *buf;
	struct link *dest_link;
	struct tipc_msg *msg;
	struct tipc_msg *tunnel_msg = buf_msg(tunnel_buf);
	u32 msg_typ = msg_type(tunnel_msg);
	u32 msg_count = msg_msgcnt(tunnel_msg);

	dest_link = (*l_ptr)->owner->links[msg_bearer_id(tunnel_msg)];
	if (!dest_link) {
		msg_dbg(tunnel_msg, "NOLINK/<REC<");
		goto exit;
	}
	if (dest_link == *l_ptr) {
		err("Unexpected changeover message on link <%s>\n",
		    (*l_ptr)->name);
		goto exit;
	}
	dbg("%c<-%c:", dest_link->b_ptr->net_plane,
	    (*l_ptr)->b_ptr->net_plane);
	*l_ptr = dest_link;
	msg = msg_get_wrapped(tunnel_msg);

	if (msg_typ == DUPLICATE_MSG) {
		if (less(msg_seqno(msg), mod(dest_link->next_in_no))) {
			msg_dbg(tunnel_msg, "DROP/<REC<");
			goto exit;
		}
		*buf = buf_extract(tunnel_buf,INT_H_SIZE);
		if (*buf == NULL) {
			warn("Link changeover error, duplicate msg dropped\n");
			goto exit;
		}
		msg_dbg(tunnel_msg, "TNL<REC<");
		buf_discard(tunnel_buf);
		return 1;
	}

	/* First original message ?: */

	if (tipc_link_is_up(dest_link)) {
		msg_dbg(tunnel_msg, "UP/FIRST/<REC<");
		info("Resetting link <%s>, changeover initiated by peer\n",
		     dest_link->name);
		tipc_link_reset(dest_link);
		dest_link->exp_msg_count = msg_count;
		dbg("Expecting %u tunnelled messages\n", msg_count);
		if (!msg_count)
			goto exit;
	} else if (dest_link->exp_msg_count == START_CHANGEOVER) {
		msg_dbg(tunnel_msg, "BLK/FIRST/<REC<");
		dest_link->exp_msg_count = msg_count;
		dbg("Expecting %u tunnelled messages\n", msg_count);
		if (!msg_count)
			goto exit;
	}

	/* Receive original message */

	if (dest_link->exp_msg_count == 0) {
		warn("Link switchover error, "
		     "got too many tunnelled messages\n");
		msg_dbg(tunnel_msg, "OVERDUE/DROP/<REC<");
		dbg_print_link(dest_link, "LINK:");
		goto exit;
	}
	dest_link->exp_msg_count--;
	if (less(msg_seqno(msg), dest_link->reset_checkpoint)) {
		msg_dbg(tunnel_msg, "DROP/DUPL/<REC<");
		goto exit;
	} else {
		*buf = buf_extract(tunnel_buf, INT_H_SIZE);
		if (*buf != NULL) {
			msg_dbg(tunnel_msg, "TNL<REC<");
			buf_discard(tunnel_buf);
			return 1;
		} else {
			warn("Link changeover error, original msg dropped\n");
		}
	}
exit:
	*buf = NULL;
	buf_discard(tunnel_buf);
	return 0;
}

/*
 *  Bundler functionality:
 */
void tipc_link_recv_bundle(struct sk_buff *buf)
{
	u32 msgcount = msg_msgcnt(buf_msg(buf));
	u32 pos = INT_H_SIZE;
	struct sk_buff *obuf;

	msg_dbg(buf_msg(buf), "<BNDL<: ");
	while (msgcount--) {
		obuf = buf_extract(buf, pos);
		if (obuf == NULL) {
			warn("Link unable to unbundle message(s)\n");
			break;
		}
		pos += align(msg_size(buf_msg(obuf)));
		msg_dbg(buf_msg(obuf), "     /");
		tipc_net_route_msg(obuf);
	}
	buf_discard(buf);
}

/*
 *  Fragmentation/defragmentation:
 */


/*
 * tipc_link_send_long_buf: Entry for buffers needing fragmentation.
 * The buffer is complete, inclusive total message length.
 * Returns user data length.
 */
int tipc_link_send_long_buf(struct link *l_ptr, struct sk_buff *buf)
{
	struct tipc_msg *inmsg = buf_msg(buf);
	struct tipc_msg fragm_hdr;
	u32 insize = msg_size(inmsg);
	u32 dsz = msg_data_sz(inmsg);
	unchar *crs = buf->data;
	u32 rest = insize;
	u32 pack_sz = link_max_pkt(l_ptr);
	u32 fragm_sz = pack_sz - INT_H_SIZE;
	u32 fragm_no = 1;
	u32 destaddr;

	if (msg_short(inmsg))
		destaddr = l_ptr->addr;
	else
		destaddr = msg_destnode(inmsg);

	if (msg_routed(inmsg))
		msg_set_prevnode(inmsg, tipc_own_addr);

	/* Prepare reusable fragment header: */

	msg_init(&fragm_hdr, MSG_FRAGMENTER, FIRST_FRAGMENT,
		 INT_H_SIZE, destaddr);
	msg_set_link_selector(&fragm_hdr, msg_link_selector(inmsg));
	msg_set_long_msgno(&fragm_hdr, mod(l_ptr->long_msg_seq_no++));
	msg_set_fragm_no(&fragm_hdr, fragm_no);
	l_ptr->stats.sent_fragmented++;

	/* Chop up message: */

	while (rest > 0) {
		struct sk_buff *fragm;

		if (rest <= fragm_sz) {
			fragm_sz = rest;
			msg_set_type(&fragm_hdr, LAST_FRAGMENT);
		}
		fragm = buf_acquire(fragm_sz + INT_H_SIZE);
		if (fragm == NULL) {
			warn("Link unable to fragment message\n");
			dsz = -ENOMEM;
			goto exit;
		}
		msg_set_size(&fragm_hdr, fragm_sz + INT_H_SIZE);
		skb_copy_to_linear_data(fragm, &fragm_hdr, INT_H_SIZE);
		skb_copy_to_linear_data_offset(fragm, INT_H_SIZE, crs,
					       fragm_sz);
		/*  Send queued messages first, if any: */

		l_ptr->stats.sent_fragments++;
		tipc_link_send_buf(l_ptr, fragm);
		if (!tipc_link_is_up(l_ptr))
			return dsz;
		msg_set_fragm_no(&fragm_hdr, ++fragm_no);
		rest -= fragm_sz;
		crs += fragm_sz;
		msg_set_type(&fragm_hdr, FRAGMENT);
	}
exit:
	buf_discard(buf);
	return dsz;
}

/*
 * A pending message being re-assembled must store certain values
 * to handle subsequent fragments correctly. The following functions
 * help storing these values in unused, available fields in the
 * pending message. This makes dynamic memory allocation unecessary.
 */

static void set_long_msg_seqno(struct sk_buff *buf, u32 seqno)
{
	msg_set_seqno(buf_msg(buf), seqno);
}

static u32 get_fragm_size(struct sk_buff *buf)
{
	return msg_ack(buf_msg(buf));
}

static void set_fragm_size(struct sk_buff *buf, u32 sz)
{
	msg_set_ack(buf_msg(buf), sz);
}

static u32 get_expected_frags(struct sk_buff *buf)
{
	return msg_bcast_ack(buf_msg(buf));
}

static void set_expected_frags(struct sk_buff *buf, u32 exp)
{
	msg_set_bcast_ack(buf_msg(buf), exp);
}

static u32 get_timer_cnt(struct sk_buff *buf)
{
	return msg_reroute_cnt(buf_msg(buf));
}

static void incr_timer_cnt(struct sk_buff *buf)
{
	msg_incr_reroute_cnt(buf_msg(buf));
}

/*
 * tipc_link_recv_fragment(): Called with node lock on. Returns
 * the reassembled buffer if message is complete.
 */
int tipc_link_recv_fragment(struct sk_buff **pending, struct sk_buff **fb,
			    struct tipc_msg **m)
{
	struct sk_buff *prev = NULL;
	struct sk_buff *fbuf = *fb;
	struct tipc_msg *fragm = buf_msg(fbuf);
	struct sk_buff *pbuf = *pending;
	u32 long_msg_seq_no = msg_long_msgno(fragm);

	*fb = NULL;
	msg_dbg(fragm,"FRG<REC<");

	/* Is there an incomplete message waiting for this fragment? */

	while (pbuf && ((msg_seqno(buf_msg(pbuf)) != long_msg_seq_no)
			|| (msg_orignode(fragm) != msg_orignode(buf_msg(pbuf))))) {
		prev = pbuf;
		pbuf = pbuf->next;
	}

	if (!pbuf && (msg_type(fragm) == FIRST_FRAGMENT)) {
		struct tipc_msg *imsg = (struct tipc_msg *)msg_data(fragm);
		u32 msg_sz = msg_size(imsg);
		u32 fragm_sz = msg_data_sz(fragm);
		u32 exp_fragm_cnt = msg_sz/fragm_sz + !!(msg_sz % fragm_sz);
		u32 max =  TIPC_MAX_USER_MSG_SIZE + LONG_H_SIZE;
		if (msg_type(imsg) == TIPC_MCAST_MSG)
			max = TIPC_MAX_USER_MSG_SIZE + MCAST_H_SIZE;
		if (msg_size(imsg) > max) {
			msg_dbg(fragm,"<REC<Oversized: ");
			buf_discard(fbuf);
			return 0;
		}
		pbuf = buf_acquire(msg_size(imsg));
		if (pbuf != NULL) {
			pbuf->next = *pending;
			*pending = pbuf;
			skb_copy_to_linear_data(pbuf, imsg,
						msg_data_sz(fragm));
			/*  Prepare buffer for subsequent fragments. */

			set_long_msg_seqno(pbuf, long_msg_seq_no);
			set_fragm_size(pbuf,fragm_sz);
			set_expected_frags(pbuf,exp_fragm_cnt - 1);
		} else {
			warn("Link unable to reassemble fragmented message\n");
		}
		buf_discard(fbuf);
		return 0;
	} else if (pbuf && (msg_type(fragm) != FIRST_FRAGMENT)) {
		u32 dsz = msg_data_sz(fragm);
		u32 fsz = get_fragm_size(pbuf);
		u32 crs = ((msg_fragm_no(fragm) - 1) * fsz);
		u32 exp_frags = get_expected_frags(pbuf) - 1;
		skb_copy_to_linear_data_offset(pbuf, crs,
					       msg_data(fragm), dsz);
		buf_discard(fbuf);

		/* Is message complete? */

		if (exp_frags == 0) {
			if (prev)
				prev->next = pbuf->next;
			else
				*pending = pbuf->next;
			msg_reset_reroute_cnt(buf_msg(pbuf));
			*fb = pbuf;
			*m = buf_msg(pbuf);
			return 1;
		}
		set_expected_frags(pbuf,exp_frags);
		return 0;
	}
	dbg(" Discarding orphan fragment %x\n",fbuf);
	msg_dbg(fragm,"ORPHAN:");
	dbg("Pending long buffers:\n");
	dbg_print_buf_chain(*pending);
	buf_discard(fbuf);
	return 0;
}

/**
 * link_check_defragm_bufs - flush stale incoming message fragments
 * @l_ptr: pointer to link
 */

static void link_check_defragm_bufs(struct link *l_ptr)
{
	struct sk_buff *prev = NULL;
	struct sk_buff *next = NULL;
	struct sk_buff *buf = l_ptr->defragm_buf;

	if (!buf)
		return;
	if (!link_working_working(l_ptr))
		return;
	while (buf) {
		u32 cnt = get_timer_cnt(buf);

		next = buf->next;
		if (cnt < 4) {
			incr_timer_cnt(buf);
			prev = buf;
		} else {
			dbg(" Discarding incomplete long buffer\n");
			msg_dbg(buf_msg(buf), "LONG:");
			dbg_print_link(l_ptr, "curr:");
			dbg("Pending long buffers:\n");
			dbg_print_buf_chain(l_ptr->defragm_buf);
			if (prev)
				prev->next = buf->next;
			else
				l_ptr->defragm_buf = buf->next;
			buf_discard(buf);
		}
		buf = next;
	}
}



static void link_set_supervision_props(struct link *l_ptr, u32 tolerance)
{
	l_ptr->tolerance = tolerance;
	l_ptr->continuity_interval =
		((tolerance / 4) > 500) ? 500 : tolerance / 4;
	l_ptr->abort_limit = tolerance / (l_ptr->continuity_interval / 4);
}


void tipc_link_set_queue_limits(struct link *l_ptr, u32 window)
{
	/* Data messages from this node, inclusive FIRST_FRAGM */
	l_ptr->queue_limit[TIPC_LOW_IMPORTANCE] = window;
	l_ptr->queue_limit[TIPC_MEDIUM_IMPORTANCE] = (window / 3) * 4;
	l_ptr->queue_limit[TIPC_HIGH_IMPORTANCE] = (window / 3) * 5;
	l_ptr->queue_limit[TIPC_CRITICAL_IMPORTANCE] = (window / 3) * 6;
	/* Transiting data messages,inclusive FIRST_FRAGM */
	l_ptr->queue_limit[TIPC_LOW_IMPORTANCE + 4] = 300;
	l_ptr->queue_limit[TIPC_MEDIUM_IMPORTANCE + 4] = 600;
	l_ptr->queue_limit[TIPC_HIGH_IMPORTANCE + 4] = 900;
	l_ptr->queue_limit[TIPC_CRITICAL_IMPORTANCE + 4] = 1200;
	l_ptr->queue_limit[CONN_MANAGER] = 1200;
	l_ptr->queue_limit[ROUTE_DISTRIBUTOR] = 1200;
	l_ptr->queue_limit[CHANGEOVER_PROTOCOL] = 2500;
	l_ptr->queue_limit[NAME_DISTRIBUTOR] = 3000;
	/* FRAGMENT and LAST_FRAGMENT packets */
	l_ptr->queue_limit[MSG_FRAGMENTER] = 4000;
}

/**
 * link_find_link - locate link by name
 * @name - ptr to link name string
 * @node - ptr to area to be filled with ptr to associated node
 *
 * Caller must hold 'tipc_net_lock' to ensure node and bearer are not deleted;
 * this also prevents link deletion.
 *
 * Returns pointer to link (or 0 if invalid link name).
 */

static struct link *link_find_link(const char *name, struct tipc_node **node)
{
	struct link_name link_name_parts;
	struct bearer *b_ptr;
	struct link *l_ptr;

	if (!link_name_validate(name, &link_name_parts))
		return NULL;

	b_ptr = tipc_bearer_find_interface(link_name_parts.if_local);
	if (!b_ptr)
		return NULL;

	*node = tipc_node_find(link_name_parts.addr_peer);
	if (!*node)
		return NULL;

	l_ptr = (*node)->links[b_ptr->identity];
	if (!l_ptr || strcmp(l_ptr->name, name))
		return NULL;

	return l_ptr;
}

struct sk_buff *tipc_link_cmd_config(const void *req_tlv_area, int req_tlv_space,
				     u16 cmd)
{
	struct tipc_link_config *args;
	u32 new_value;
	struct link *l_ptr;
	struct tipc_node *node;
	int res;

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, TIPC_TLV_LINK_CONFIG))
		return tipc_cfg_reply_error_string(TIPC_CFG_TLV_ERROR);

	args = (struct tipc_link_config *)TLV_DATA(req_tlv_area);
	new_value = ntohl(args->value);

	if (!strcmp(args->name, tipc_bclink_name)) {
		if ((cmd == TIPC_CMD_SET_LINK_WINDOW) &&
		    (tipc_bclink_set_queue_limits(new_value) == 0))
			return tipc_cfg_reply_none();
		return tipc_cfg_reply_error_string(TIPC_CFG_NOT_SUPPORTED
						   " (cannot change setting on broadcast link)");
	}

	read_lock_bh(&tipc_net_lock);
	l_ptr = link_find_link(args->name, &node);
	if (!l_ptr) {
		read_unlock_bh(&tipc_net_lock);
		return tipc_cfg_reply_error_string("link not found");
	}

	tipc_node_lock(node);
	res = -EINVAL;
	switch (cmd) {
	case TIPC_CMD_SET_LINK_TOL:
		if ((new_value >= TIPC_MIN_LINK_TOL) &&
		    (new_value <= TIPC_MAX_LINK_TOL)) {
			link_set_supervision_props(l_ptr, new_value);
			tipc_link_send_proto_msg(l_ptr, STATE_MSG,
						 0, 0, new_value, 0, 0);
			res = 0;
		}
		break;
	case TIPC_CMD_SET_LINK_PRI:
		if ((new_value >= TIPC_MIN_LINK_PRI) &&
		    (new_value <= TIPC_MAX_LINK_PRI)) {
			l_ptr->priority = new_value;
			tipc_link_send_proto_msg(l_ptr, STATE_MSG,
						 0, 0, 0, new_value, 0);
			res = 0;
		}
		break;
	case TIPC_CMD_SET_LINK_WINDOW:
		if ((new_value >= TIPC_MIN_LINK_WIN) &&
		    (new_value <= TIPC_MAX_LINK_WIN)) {
			tipc_link_set_queue_limits(l_ptr, new_value);
			res = 0;
		}
		break;
	}
	tipc_node_unlock(node);

	read_unlock_bh(&tipc_net_lock);
	if (res)
		return tipc_cfg_reply_error_string("cannot change link setting");

	return tipc_cfg_reply_none();
void tipc_link_proto_xmit(struct tipc_link *l, u32 msg_typ, int probe_msg,
			  u32 gap, u32 tolerance, u32 priority)
{
	struct sk_buff *skb = NULL;
	struct sk_buff_head xmitq;

	__skb_queue_head_init(&xmitq);
	tipc_link_build_proto_msg(l, msg_typ, probe_msg, gap,
				  tolerance, priority, &xmitq);
	skb = __skb_dequeue(&xmitq);
	if (!skb)
		return;
	tipc_bearer_xmit_skb(l->net, l->bearer_id, skb, l->media_addr);
	l->rcv_unacked = 0;
}

static void tipc_link_build_proto_msg(struct tipc_link *l, int mtyp, bool probe,
				      u16 rcvgap, int tolerance, int priority,
				      struct sk_buff_head *xmitq)
{
	struct sk_buff *skb = NULL;
	struct tipc_msg *hdr = l->pmsg;
	bool node_up = link_is_up(l->bc_rcvlink);

	/* Don't send protocol message during reset or link failover */
	if (tipc_link_is_blocked(l))
		return;

	msg_set_type(hdr, mtyp);
	msg_set_net_plane(hdr, l->net_plane);
	msg_set_next_sent(hdr, l->snd_nxt);
	msg_set_ack(hdr, l->rcv_nxt - 1);
	msg_set_bcast_ack(hdr, l->bc_rcvlink->rcv_nxt - 1);
	msg_set_last_bcast(hdr, l->bc_sndlink->snd_nxt - 1);
	msg_set_link_tolerance(hdr, tolerance);
	msg_set_linkprio(hdr, priority);
	msg_set_redundant_link(hdr, node_up);
	msg_set_seq_gap(hdr, 0);

	/* Compatibility: created msg must not be in sequence with pkt flow */
	msg_set_seqno(hdr, l->snd_nxt + U16_MAX / 2);

	if (mtyp == STATE_MSG) {
		if (!tipc_link_is_up(l))
			return;

		/* Override rcvgap if there are packets in deferred queue */
		if (!skb_queue_empty(&l->deferdq))
			rcvgap = buf_seqno(skb_peek(&l->deferdq)) - l->rcv_nxt;
		if (rcvgap) {
			msg_set_seq_gap(hdr, rcvgap);
			l->stats.sent_nacks++;
		}
		msg_set_probe(hdr, probe);
		if (probe)
			l->stats.sent_probes++;
		l->stats.sent_states++;
		l->rcv_unacked = 0;
	} else {
		/* RESET_MSG or ACTIVATE_MSG */
		msg_set_max_pkt(hdr, l->advertised_mtu);
		msg_set_ack(hdr, l->rcv_nxt - 1);
		msg_set_next_sent(hdr, 1);
	}
	skb = tipc_buf_acquire(msg_size(hdr));
	if (!skb)
		return;
	skb_copy_to_linear_data(skb, hdr, msg_size(hdr));
	skb->priority = TC_PRIO_CONTROL;
	__skb_queue_tail(xmitq, skb);
}

/* tipc_link_tnl_prepare(): prepare and return a list of tunnel packets
 * with contents of the link's transmit and backlog queues.
 */
void tipc_link_tnl_prepare(struct tipc_link *l, struct tipc_link *tnl,
			   int mtyp, struct sk_buff_head *xmitq)
{
	struct sk_buff *skb, *tnlskb;
	struct tipc_msg *hdr, tnlhdr;
	struct sk_buff_head *queue = &l->transmq;
	struct sk_buff_head tmpxq, tnlq;
	u16 pktlen, pktcnt, seqno = l->snd_nxt;

	if (!tnl)
		return;

	skb_queue_head_init(&tnlq);
	skb_queue_head_init(&tmpxq);

	/* At least one packet required for safe algorithm => add dummy */
	skb = tipc_msg_create(TIPC_LOW_IMPORTANCE, TIPC_DIRECT_MSG,
			      BASIC_H_SIZE, 0, l->addr, link_own_addr(l),
			      0, 0, TIPC_ERR_NO_PORT);
	if (!skb) {
		pr_warn("%sunable to create tunnel packet\n", link_co_err);
		return;
	}
	skb_queue_tail(&tnlq, skb);
	tipc_link_xmit(l, &tnlq, &tmpxq);
	__skb_queue_purge(&tmpxq);

	/* Initialize reusable tunnel packet header */
	tipc_msg_init(link_own_addr(l), &tnlhdr, TUNNEL_PROTOCOL,
		      mtyp, INT_H_SIZE, l->addr);
	pktcnt = skb_queue_len(&l->transmq) + skb_queue_len(&l->backlogq);
	msg_set_msgcnt(&tnlhdr, pktcnt);
	msg_set_bearer_id(&tnlhdr, l->peer_bearer_id);
tnl:
	/* Wrap each packet into a tunnel packet */
	skb_queue_walk(queue, skb) {
		hdr = buf_msg(skb);
		if (queue == &l->backlogq)
			msg_set_seqno(hdr, seqno++);
		pktlen = msg_size(hdr);
		msg_set_size(&tnlhdr, pktlen + INT_H_SIZE);
		tnlskb = tipc_buf_acquire(pktlen + INT_H_SIZE);
		if (!tnlskb) {
			pr_warn("%sunable to send packet\n", link_co_err);
			return;
		}
		skb_copy_to_linear_data(tnlskb, &tnlhdr, INT_H_SIZE);
		skb_copy_to_linear_data_offset(tnlskb, INT_H_SIZE, hdr, pktlen);
		__skb_queue_tail(&tnlq, tnlskb);
	}
	if (queue != &l->backlogq) {
		queue = &l->backlogq;
		goto tnl;
	}

	tipc_link_xmit(tnl, &tnlq, xmitq);

	if (mtyp == FAILOVER_MSG) {
		tnl->drop_point = l->rcv_nxt;
		tnl->failover_reasm_skb = l->reasm_buf;
		l->reasm_buf = NULL;
	}
}

/* tipc_link_proto_rcv(): receive link level protocol message :
 * Note that network plane id propagates through the network, and may
 * change at any time. The node with lowest numerical id determines
 * network plane
 */
static int tipc_link_proto_rcv(struct tipc_link *l, struct sk_buff *skb,
			       struct sk_buff_head *xmitq)
{
	struct tipc_msg *hdr = buf_msg(skb);
	u16 rcvgap = 0;
	u16 ack = msg_ack(hdr);
	u16 gap = msg_seq_gap(hdr);
	u16 peers_snd_nxt =  msg_next_sent(hdr);
	u16 peers_tol = msg_link_tolerance(hdr);
	u16 peers_prio = msg_linkprio(hdr);
	u16 rcv_nxt = l->rcv_nxt;
	u32 dlen = msg_data_sz(hdr);
	int mtyp = msg_type(hdr);
	char *if_name;
	int rc = 0;

	if (dlen > U16_MAX)
		goto exit;

	if (tipc_link_is_blocked(l) || !xmitq)
		goto exit;

	if (link_own_addr(l) > msg_prevnode(hdr))
		l->net_plane = msg_net_plane(hdr);

	switch (mtyp) {
	case RESET_MSG:

		/* Ignore duplicate RESET with old session number */
		if ((less_eq(msg_session(hdr), l->peer_session)) &&
		    (l->peer_session != WILDCARD_SESSION))
			break;
		/* fall thru' */

	case ACTIVATE_MSG:
		if (skb_linearize(skb))
			goto exit;

		hdr = buf_msg(skb);

		/* Complete own link name with peer's interface name */
		if_name =  strrchr(l->name, ':') + 1;
		if (sizeof(l->name) - (if_name - l->name) <= TIPC_MAX_IF_NAME)
			break;
		if (msg_data_sz(hdr) < TIPC_MAX_IF_NAME)
			break;
		strncpy(if_name, msg_data(hdr),	TIPC_MAX_IF_NAME);

		/* Update own tolerance if peer indicates a non-zero value */
		if (in_range(peers_tol, TIPC_MIN_LINK_TOL, TIPC_MAX_LINK_TOL))
			l->tolerance = peers_tol;

		/* Update own priority if peer's priority is higher */
		if (in_range(peers_prio, l->priority + 1, TIPC_MAX_LINK_PRI))
			l->priority = peers_prio;

		/* ACTIVATE_MSG serves as PEER_RESET if link is already down */
		if ((mtyp == RESET_MSG) || !link_is_up(l))
			rc = tipc_link_fsm_evt(l, LINK_PEER_RESET_EVT);

		/* ACTIVATE_MSG takes up link if it was already locally reset */
		if ((mtyp == ACTIVATE_MSG) && (l->state == LINK_ESTABLISHING))
			rc = TIPC_LINK_UP_EVT;

		l->peer_session = msg_session(hdr);
		l->peer_bearer_id = msg_bearer_id(hdr);
		if (l->mtu > msg_max_pkt(hdr))
			l->mtu = msg_max_pkt(hdr);
		break;

	case STATE_MSG:

		/* Update own tolerance if peer indicates a non-zero value */
		if (in_range(peers_tol, TIPC_MIN_LINK_TOL, TIPC_MAX_LINK_TOL))
			l->tolerance = peers_tol;

		l->silent_intv_cnt = 0;
		l->stats.recv_states++;
		if (msg_probe(hdr))
			l->stats.recv_probes++;

		if (!link_is_up(l)) {
			if (l->state == LINK_ESTABLISHING)
				rc = TIPC_LINK_UP_EVT;
			break;
		}

		/* Send NACK if peer has sent pkts we haven't received yet */
		if (more(peers_snd_nxt, rcv_nxt) && !tipc_link_is_synching(l))
			rcvgap = peers_snd_nxt - l->rcv_nxt;
		if (rcvgap || (msg_probe(hdr)))
			tipc_link_build_proto_msg(l, STATE_MSG, 0, rcvgap,
						  0, 0, xmitq);
		tipc_link_release_pkts(l, ack);

		/* If NACK, retransmit will now start at right position */
		if (gap) {
			rc = tipc_link_retrans(l, ack + 1, ack + gap, xmitq);
			l->stats.recv_nacks++;
		}

		tipc_link_advance_backlog(l, xmitq);
		if (unlikely(!skb_queue_empty(&l->wakeupq)))
			link_prepare_wakeup(l);
	}
exit:
	kfree_skb(skb);
	return rc;
}

/* tipc_link_build_bc_proto_msg() - create broadcast protocol message
 */
static bool tipc_link_build_bc_proto_msg(struct tipc_link *l, bool bcast,
					 u16 peers_snd_nxt,
					 struct sk_buff_head *xmitq)
{
	struct sk_buff *skb;
	struct tipc_msg *hdr;
	struct sk_buff *dfrd_skb = skb_peek(&l->deferdq);
	u16 ack = l->rcv_nxt - 1;
	u16 gap_to = peers_snd_nxt - 1;

	skb = tipc_msg_create(BCAST_PROTOCOL, STATE_MSG, INT_H_SIZE,
			      0, l->addr, link_own_addr(l), 0, 0, 0);
	if (!skb)
		return false;
	hdr = buf_msg(skb);
	msg_set_last_bcast(hdr, l->bc_sndlink->snd_nxt - 1);
	msg_set_bcast_ack(hdr, ack);
	msg_set_bcgap_after(hdr, ack);
	if (dfrd_skb)
		gap_to = buf_seqno(dfrd_skb) - 1;
	msg_set_bcgap_to(hdr, gap_to);
	msg_set_non_seq(hdr, bcast);
	__skb_queue_tail(xmitq, skb);
	return true;
}

/* tipc_link_build_bc_init_msg() - synchronize broadcast link endpoints.
 *
 * Give a newly added peer node the sequence number where it should
 * start receiving and acking broadcast packets.
 */
static void tipc_link_build_bc_init_msg(struct tipc_link *l,
					struct sk_buff_head *xmitq)
{
	struct sk_buff_head list;

	__skb_queue_head_init(&list);
	if (!tipc_link_build_bc_proto_msg(l->bc_rcvlink, false, 0, &list))
		return;
	tipc_link_xmit(l, &list, xmitq);
}

/* tipc_link_bc_init_rcv - receive initial broadcast synch data from peer
 */
void tipc_link_bc_init_rcv(struct tipc_link *l, struct tipc_msg *hdr)
{
	int mtyp = msg_type(hdr);
	u16 peers_snd_nxt = msg_bc_snd_nxt(hdr);

	if (link_is_up(l))
		return;

	if (msg_user(hdr) == BCAST_PROTOCOL) {
		l->rcv_nxt = peers_snd_nxt;
		l->state = LINK_ESTABLISHED;
		return;
	}

	if (l->peer_caps & TIPC_BCAST_SYNCH)
		return;

	if (msg_peer_node_is_up(hdr))
		return;

	/* Compatibility: accept older, less safe initial synch data */
	if ((mtyp == RESET_MSG) || (mtyp == ACTIVATE_MSG))
		l->rcv_nxt = peers_snd_nxt;
}

/* tipc_link_bc_sync_rcv - update rcv link according to peer's send state
 */
void tipc_link_bc_sync_rcv(struct tipc_link *l, struct tipc_msg *hdr,
			   struct sk_buff_head *xmitq)
{
	u16 peers_snd_nxt = msg_bc_snd_nxt(hdr);

	if (!link_is_up(l))
		return;

	if (!msg_peer_node_is_up(hdr))
		return;

	l->bc_peer_is_up = true;

	/* Ignore if peers_snd_nxt goes beyond receive window */
	if (more(peers_snd_nxt, l->rcv_nxt + l->window))
		return;

	if (!more(peers_snd_nxt, l->rcv_nxt)) {
		l->nack_state = BC_NACK_SND_CONDITIONAL;
		return;
	}

	/* Don't NACK if one was recently sent or peeked */
	if (l->nack_state == BC_NACK_SND_SUPPRESS) {
		l->nack_state = BC_NACK_SND_UNCONDITIONAL;
		return;
	}

	/* Conditionally delay NACK sending until next synch rcv */
	if (l->nack_state == BC_NACK_SND_CONDITIONAL) {
		l->nack_state = BC_NACK_SND_UNCONDITIONAL;
		if ((peers_snd_nxt - l->rcv_nxt) < TIPC_MIN_LINK_WIN)
			return;
	}

	/* Send NACK now but suppress next one */
	tipc_link_build_bc_proto_msg(l, true, peers_snd_nxt, xmitq);
	l->nack_state = BC_NACK_SND_SUPPRESS;
}

void tipc_link_bc_ack_rcv(struct tipc_link *l, u16 acked,
			  struct sk_buff_head *xmitq)
{
	struct sk_buff *skb, *tmp;
	struct tipc_link *snd_l = l->bc_sndlink;

	if (!link_is_up(l) || !l->bc_peer_is_up)
		return;

	if (!more(acked, l->acked))
		return;

	/* Skip over packets peer has already acked */
	skb_queue_walk(&snd_l->transmq, skb) {
		if (more(buf_seqno(skb), l->acked))
			break;
	}

	/* Update/release the packets peer is acking now */
	skb_queue_walk_from_safe(&snd_l->transmq, skb, tmp) {
		if (more(buf_seqno(skb), acked))
			break;
		if (!--TIPC_SKB_CB(skb)->ackers) {
			__skb_unlink(skb, &snd_l->transmq);
			kfree_skb(skb);
		}
	}
	l->acked = acked;
	tipc_link_advance_backlog(snd_l, xmitq);
	if (unlikely(!skb_queue_empty(&snd_l->wakeupq)))
		link_prepare_wakeup(snd_l);
}

/* tipc_link_bc_nack_rcv(): receive broadcast nack message
 */
int tipc_link_bc_nack_rcv(struct tipc_link *l, struct sk_buff *skb,
			  struct sk_buff_head *xmitq)
{
	struct tipc_msg *hdr = buf_msg(skb);
	u32 dnode = msg_destnode(hdr);
	int mtyp = msg_type(hdr);
	u16 acked = msg_bcast_ack(hdr);
	u16 from = acked + 1;
	u16 to = msg_bcgap_to(hdr);
	u16 peers_snd_nxt = to + 1;
	int rc = 0;

	kfree_skb(skb);

	if (!tipc_link_is_up(l) || !l->bc_peer_is_up)
		return 0;

	if (mtyp != STATE_MSG)
		return 0;

	if (dnode == link_own_addr(l)) {
		tipc_link_bc_ack_rcv(l, acked, xmitq);
		rc = tipc_link_retrans(l->bc_sndlink, from, to, xmitq);
		l->stats.recv_nacks++;
		return rc;
	}

	/* Msg for other node => suppress own NACK at next sync if applicable */
	if (more(peers_snd_nxt, l->rcv_nxt) && !less(l->rcv_nxt, from))
		l->nack_state = BC_NACK_SND_SUPPRESS;

	return 0;
}

void tipc_link_set_queue_limits(struct tipc_link *l, u32 win)
{
	int max_bulk = TIPC_MAX_PUBLICATIONS / (l->mtu / ITEM_SIZE);

	l->window = win;
	l->backlog[TIPC_LOW_IMPORTANCE].limit      = win / 2;
	l->backlog[TIPC_MEDIUM_IMPORTANCE].limit   = win;
	l->backlog[TIPC_HIGH_IMPORTANCE].limit     = win / 2 * 3;
	l->backlog[TIPC_CRITICAL_IMPORTANCE].limit = win * 2;
	l->backlog[TIPC_SYSTEM_IMPORTANCE].limit   = max_bulk;
}

/* tipc_link_find_owner - locate owner node of link by link's name
 * @net: the applicable net namespace
 * @name: pointer to link name string
 * @bearer_id: pointer to index in 'node->links' array where the link was found.
 *
 * Returns pointer to node owning the link, or 0 if no matching link is found.
 */
static struct tipc_node *tipc_link_find_owner(struct net *net,
					      const char *link_name,
					      unsigned int *bearer_id)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_link *l_ptr;
	struct tipc_node *n_ptr;
	struct tipc_node *found_node = NULL;
	int i;

	*bearer_id = 0;
	rcu_read_lock();
	list_for_each_entry_rcu(n_ptr, &tn->node_list, list) {
		tipc_node_lock(n_ptr);
		for (i = 0; i < MAX_BEARERS; i++) {
			l_ptr = n_ptr->links[i].link;
			if (l_ptr && !strcmp(l_ptr->name, link_name)) {
				*bearer_id = i;
				found_node = n_ptr;
				break;
			}
		}
		tipc_node_unlock(n_ptr);
		if (found_node)
			break;
	}
	rcu_read_unlock();

	return found_node;
}

/**
 * link_reset_statistics - reset link statistics
 * @l_ptr: pointer to link
 */

static void link_reset_statistics(struct link *l_ptr)
{
	memset(&l_ptr->stats, 0, sizeof(l_ptr->stats));
	l_ptr->stats.sent_info = l_ptr->next_out_no;
	l_ptr->stats.recv_info = l_ptr->next_in_no;
}

struct sk_buff *tipc_link_cmd_reset_stats(const void *req_tlv_area, int req_tlv_space)
{
	char *link_name;
	struct link *l_ptr;
	struct tipc_node *node;

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, TIPC_TLV_LINK_NAME))
		return tipc_cfg_reply_error_string(TIPC_CFG_TLV_ERROR);

	link_name = (char *)TLV_DATA(req_tlv_area);
	if (!strcmp(link_name, tipc_bclink_name)) {
		if (tipc_bclink_reset_stats())
			return tipc_cfg_reply_error_string("link not found");
		return tipc_cfg_reply_none();
	}

	read_lock_bh(&tipc_net_lock);
	l_ptr = link_find_link(link_name, &node);
	if (!l_ptr) {
		read_unlock_bh(&tipc_net_lock);
		return tipc_cfg_reply_error_string("link not found");
	}

	tipc_node_lock(node);
	link_reset_statistics(l_ptr);
	tipc_node_unlock(node);
	read_unlock_bh(&tipc_net_lock);
	return tipc_cfg_reply_none();
}

/**
 * percent - convert count to a percentage of total (rounding up or down)
 */

static u32 percent(u32 count, u32 total)
{
	return (count * 100 + (total / 2)) / total;
}

/**
 * tipc_link_stats - print link statistics
 * @name: link name
 * @buf: print buffer area
 * @buf_size: size of print buffer area
 *
 * Returns length of print buffer data string (or 0 if error)
 */

static int tipc_link_stats(const char *name, char *buf, const u32 buf_size)
{
	struct print_buf pb;
	struct link *l_ptr;
	struct tipc_node *node;
	char *status;
	u32 profile_total = 0;

	if (!strcmp(name, tipc_bclink_name))
		return tipc_bclink_stats(buf, buf_size);

	tipc_printbuf_init(&pb, buf, buf_size);

	read_lock_bh(&tipc_net_lock);
	l_ptr = link_find_link(name, &node);
	if (!l_ptr) {
		read_unlock_bh(&tipc_net_lock);
		return 0;
	}
	tipc_node_lock(node);

	if (tipc_link_is_active(l_ptr))
		status = "ACTIVE";
	else if (tipc_link_is_up(l_ptr))
		status = "STANDBY";
	else
		status = "DEFUNCT";
	tipc_printf(&pb, "Link <%s>\n"
			 "  %s  MTU:%u  Priority:%u  Tolerance:%u ms"
			 "  Window:%u packets\n",
		    l_ptr->name, status, link_max_pkt(l_ptr),
		    l_ptr->priority, l_ptr->tolerance, l_ptr->queue_limit[0]);
	tipc_printf(&pb, "  RX packets:%u fragments:%u/%u bundles:%u/%u\n",
		    l_ptr->next_in_no - l_ptr->stats.recv_info,
		    l_ptr->stats.recv_fragments,
		    l_ptr->stats.recv_fragmented,
		    l_ptr->stats.recv_bundles,
		    l_ptr->stats.recv_bundled);
	tipc_printf(&pb, "  TX packets:%u fragments:%u/%u bundles:%u/%u\n",
		    l_ptr->next_out_no - l_ptr->stats.sent_info,
		    l_ptr->stats.sent_fragments,
		    l_ptr->stats.sent_fragmented,
		    l_ptr->stats.sent_bundles,
		    l_ptr->stats.sent_bundled);
	profile_total = l_ptr->stats.msg_length_counts;
	if (!profile_total)
		profile_total = 1;
	tipc_printf(&pb, "  TX profile sample:%u packets  average:%u octets\n"
			 "  0-64:%u%% -256:%u%% -1024:%u%% -4096:%u%% "
			 "-16354:%u%% -32768:%u%% -66000:%u%%\n",
		    l_ptr->stats.msg_length_counts,
		    l_ptr->stats.msg_lengths_total / profile_total,
		    percent(l_ptr->stats.msg_length_profile[0], profile_total),
		    percent(l_ptr->stats.msg_length_profile[1], profile_total),
		    percent(l_ptr->stats.msg_length_profile[2], profile_total),
		    percent(l_ptr->stats.msg_length_profile[3], profile_total),
		    percent(l_ptr->stats.msg_length_profile[4], profile_total),
		    percent(l_ptr->stats.msg_length_profile[5], profile_total),
		    percent(l_ptr->stats.msg_length_profile[6], profile_total));
	tipc_printf(&pb, "  RX states:%u probes:%u naks:%u defs:%u dups:%u\n",
		    l_ptr->stats.recv_states,
		    l_ptr->stats.recv_probes,
		    l_ptr->stats.recv_nacks,
		    l_ptr->stats.deferred_recv,
		    l_ptr->stats.duplicates);
	tipc_printf(&pb, "  TX states:%u probes:%u naks:%u acks:%u dups:%u\n",
		    l_ptr->stats.sent_states,
		    l_ptr->stats.sent_probes,
		    l_ptr->stats.sent_nacks,
		    l_ptr->stats.sent_acks,
		    l_ptr->stats.retransmitted);
	tipc_printf(&pb, "  Congestion bearer:%u link:%u  Send queue max:%u avg:%u\n",
		    l_ptr->stats.bearer_congs,
		    l_ptr->stats.link_congs,
		    l_ptr->stats.max_queue_sz,
		    l_ptr->stats.queue_sz_counts
		    ? (l_ptr->stats.accu_queue_sz / l_ptr->stats.queue_sz_counts)
		    : 0);

	tipc_node_unlock(node);
	read_unlock_bh(&tipc_net_lock);
	return tipc_printbuf_validate(&pb);
}

#define MAX_LINK_STATS_INFO 2000

struct sk_buff *tipc_link_cmd_show_stats(const void *req_tlv_area, int req_tlv_space)
{
	struct sk_buff *buf;
	struct tlv_desc *rep_tlv;
	int str_len;

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, TIPC_TLV_LINK_NAME))
		return tipc_cfg_reply_error_string(TIPC_CFG_TLV_ERROR);

	buf = tipc_cfg_reply_alloc(TLV_SPACE(MAX_LINK_STATS_INFO));
	if (!buf)
		return NULL;

	rep_tlv = (struct tlv_desc *)buf->data;

	str_len = tipc_link_stats((char *)TLV_DATA(req_tlv_area),
				  (char *)TLV_DATA(rep_tlv), MAX_LINK_STATS_INFO);
	if (!str_len) {
		buf_discard(buf);
		return tipc_cfg_reply_error_string("link not found");
	}

	skb_put(buf, TLV_SPACE(str_len));
	TLV_SET(rep_tlv, TIPC_TLV_ULTRA_STRING, NULL, str_len);

	return buf;
}

#if 0
int link_control(const char *name, u32 op, u32 val)
{
	int res = -EINVAL;
	struct link *l_ptr;
	u32 bearer_id;
	struct tipc_node * node;
	u32 a;

	a = link_name2addr(name, &bearer_id);
	read_lock_bh(&tipc_net_lock);
	node = tipc_node_find(a);
	if (node) {
		tipc_node_lock(node);
		l_ptr = node->links[bearer_id];
		if (l_ptr) {
			if (op == TIPC_REMOVE_LINK) {
				struct bearer *b_ptr = l_ptr->b_ptr;
				spin_lock_bh(&b_ptr->publ.lock);
				tipc_link_delete(l_ptr);
				spin_unlock_bh(&b_ptr->publ.lock);
			}
			if (op == TIPC_CMD_BLOCK_LINK) {
				tipc_link_reset(l_ptr);
				l_ptr->blocked = 1;
			}
			if (op == TIPC_CMD_UNBLOCK_LINK) {
				l_ptr->blocked = 0;
			}
			res = 0;
		}
		tipc_node_unlock(node);
	}
	read_unlock_bh(&tipc_net_lock);
	return res;
}
#endif

/**
 * tipc_link_get_max_pkt - get maximum packet size to use when sending to destination
 * @dest: network address of destination node
 * @selector: used to select from set of active links
 *
 * If no active link can be found, uses default maximum packet size.
 */

u32 tipc_link_get_max_pkt(u32 dest, u32 selector)
{
	struct tipc_node *n_ptr;
	struct link *l_ptr;
	u32 res = MAX_PKT_DEFAULT;

	if (dest == tipc_own_addr)
		return MAX_MSG_SIZE;

	read_lock_bh(&tipc_net_lock);
	n_ptr = tipc_node_select(dest, selector);
	if (n_ptr) {
		tipc_node_lock(n_ptr);
		l_ptr = n_ptr->active_links[selector & 1];
		if (l_ptr)
			res = link_max_pkt(l_ptr);
		tipc_node_unlock(n_ptr);
	}
	read_unlock_bh(&tipc_net_lock);
	return res;
}

#if 0
static void link_dump_rec_queue(struct link *l_ptr)
{
	struct sk_buff *crs;

	if (!l_ptr->oldest_deferred_in) {
		info("Reception queue empty\n");
		return;
	}
	info("Contents of Reception queue:\n");
	crs = l_ptr->oldest_deferred_in;
	while (crs) {
		if (crs->data == (void *)0x0000a3a3) {
			info("buffer %x invalid\n", crs);
			return;
		}
		msg_dbg(buf_msg(crs), "In rec queue: \n");
		crs = crs->next;
	}
}
#endif

static void link_dump_send_queue(struct link *l_ptr)
{
	if (l_ptr->next_out) {
		info("\nContents of unsent queue:\n");
		dbg_print_buf_chain(l_ptr->next_out);
	}
	info("\nContents of send queue:\n");
	if (l_ptr->first_out) {
		dbg_print_buf_chain(l_ptr->first_out);
	}
	info("Empty send queue\n");
}

static void link_print(struct link *l_ptr, struct print_buf *buf,
		       const char *str)
{
	tipc_printf(buf, str);
	if (link_reset_reset(l_ptr) || link_reset_unknown(l_ptr))
		return;
	tipc_printf(buf, "Link %x<%s>:",
		    l_ptr->addr, l_ptr->b_ptr->publ.name);
	tipc_printf(buf, ": NXO(%u):", mod(l_ptr->next_out_no));
	tipc_printf(buf, "NXI(%u):", mod(l_ptr->next_in_no));
	tipc_printf(buf, "SQUE");
	if (l_ptr->first_out) {
		tipc_printf(buf, "[%u..", msg_seqno(buf_msg(l_ptr->first_out)));
		if (l_ptr->next_out)
			tipc_printf(buf, "%u..",
				    msg_seqno(buf_msg(l_ptr->next_out)));
		tipc_printf(buf, "%u]",
			    msg_seqno(buf_msg
				      (l_ptr->last_out)), l_ptr->out_queue_size);
		if ((mod(msg_seqno(buf_msg(l_ptr->last_out)) -
			 msg_seqno(buf_msg(l_ptr->first_out)))
		     != (l_ptr->out_queue_size - 1))
		    || (l_ptr->last_out->next != NULL)) {
			tipc_printf(buf, "\nSend queue inconsistency\n");
			tipc_printf(buf, "first_out= %x ", l_ptr->first_out);
			tipc_printf(buf, "next_out= %x ", l_ptr->next_out);
			tipc_printf(buf, "last_out= %x ", l_ptr->last_out);
			link_dump_send_queue(l_ptr);
		}
	} else
		tipc_printf(buf, "[]");
	tipc_printf(buf, "SQSIZ(%u)", l_ptr->out_queue_size);
	if (l_ptr->oldest_deferred_in) {
		u32 o = msg_seqno(buf_msg(l_ptr->oldest_deferred_in));
		u32 n = msg_seqno(buf_msg(l_ptr->newest_deferred_in));
		tipc_printf(buf, ":RQUE[%u..%u]", o, n);
		if (l_ptr->deferred_inqueue_sz != mod((n + 1) - o)) {
			tipc_printf(buf, ":RQSIZ(%u)",
				    l_ptr->deferred_inqueue_sz);
		}
	}
	if (link_working_unknown(l_ptr))
		tipc_printf(buf, ":WU");
	if (link_reset_reset(l_ptr))
		tipc_printf(buf, ":RR");
	if (link_reset_unknown(l_ptr))
		tipc_printf(buf, ":RU");
	if (link_working_working(l_ptr))
		tipc_printf(buf, ":WW");
	tipc_printf(buf, "\n");
}

static void link_reset_statistics(struct tipc_link *l_ptr)
{
	memset(&l_ptr->stats, 0, sizeof(l_ptr->stats));
	l_ptr->stats.sent_info = l_ptr->snd_nxt;
	l_ptr->stats.recv_info = l_ptr->rcv_nxt;
}

static void link_print(struct tipc_link *l, const char *str)
{
	struct sk_buff *hskb = skb_peek(&l->transmq);
	u16 head = hskb ? msg_seqno(buf_msg(hskb)) : l->snd_nxt - 1;
	u16 tail = l->snd_nxt - 1;

	pr_info("%s Link <%s> state %x\n", str, l->name, l->state);
	pr_info("XMTQ: %u [%u-%u], BKLGQ: %u, SNDNX: %u, RCVNX: %u\n",
		skb_queue_len(&l->transmq), head, tail,
		skb_queue_len(&l->backlogq), l->snd_nxt, l->rcv_nxt);
}

/* Parse and validate nested (link) properties valid for media, bearer and link
 */
int tipc_nl_parse_link_prop(struct nlattr *prop, struct nlattr *props[])
{
	int err;

	err = nla_parse_nested(props, TIPC_NLA_PROP_MAX, prop,
			       tipc_nl_prop_policy);
	if (err)
		return err;

	if (props[TIPC_NLA_PROP_PRIO]) {
		u32 prio;

		prio = nla_get_u32(props[TIPC_NLA_PROP_PRIO]);
		if (prio > TIPC_MAX_LINK_PRI)
			return -EINVAL;
	}

	if (props[TIPC_NLA_PROP_TOL]) {
		u32 tol;

		tol = nla_get_u32(props[TIPC_NLA_PROP_TOL]);
		if ((tol < TIPC_MIN_LINK_TOL) || (tol > TIPC_MAX_LINK_TOL))
			return -EINVAL;
	}

	if (props[TIPC_NLA_PROP_WIN]) {
		u32 win;

		win = nla_get_u32(props[TIPC_NLA_PROP_WIN]);
		if ((win < TIPC_MIN_LINK_WIN) || (win > TIPC_MAX_LINK_WIN))
			return -EINVAL;
	}

	return 0;
}

int tipc_nl_link_set(struct sk_buff *skb, struct genl_info *info)
{
	int err;
	int res = 0;
	int bearer_id;
	char *name;
	struct tipc_link *link;
	struct tipc_node *node;
	struct sk_buff_head xmitq;
	struct nlattr *attrs[TIPC_NLA_LINK_MAX + 1];
	struct net *net = sock_net(skb->sk);

	__skb_queue_head_init(&xmitq);

	if (!info->attrs[TIPC_NLA_LINK])
		return -EINVAL;

	err = nla_parse_nested(attrs, TIPC_NLA_LINK_MAX,
			       info->attrs[TIPC_NLA_LINK],
			       tipc_nl_link_policy);
	if (err)
		return err;

	if (!attrs[TIPC_NLA_LINK_NAME])
		return -EINVAL;

	name = nla_data(attrs[TIPC_NLA_LINK_NAME]);

	if (strcmp(name, tipc_bclink_name) == 0)
		return tipc_nl_bc_link_set(net, attrs);

	node = tipc_link_find_owner(net, name, &bearer_id);
	if (!node)
		return -EINVAL;

	tipc_node_lock(node);

	link = node->links[bearer_id].link;
	if (!link) {
		res = -EINVAL;
		goto out;
	}

	if (attrs[TIPC_NLA_LINK_PROP]) {
		struct nlattr *props[TIPC_NLA_PROP_MAX + 1];

		err = tipc_nl_parse_link_prop(attrs[TIPC_NLA_LINK_PROP],
					      props);
		if (err) {
			res = err;
			goto out;
		}

		if (props[TIPC_NLA_PROP_TOL]) {
			u32 tol;

			tol = nla_get_u32(props[TIPC_NLA_PROP_TOL]);
			link->tolerance = tol;
			tipc_link_build_proto_msg(link, STATE_MSG, 0, 0, tol, 0, &xmitq);
		}
		if (props[TIPC_NLA_PROP_PRIO]) {
			u32 prio;

			prio = nla_get_u32(props[TIPC_NLA_PROP_PRIO]);
			link->priority = prio;
			tipc_link_build_proto_msg(link, STATE_MSG, 0, 0, 0, prio, &xmitq);
		}
		if (props[TIPC_NLA_PROP_WIN]) {
			u32 win;

			win = nla_get_u32(props[TIPC_NLA_PROP_WIN]);
			tipc_link_set_queue_limits(link, win);
		}
	}

out:
	tipc_node_unlock(node);
	tipc_bearer_xmit(net, bearer_id, &xmitq, &node->links[bearer_id].maddr);
	return res;
}

static int __tipc_nl_add_stats(struct sk_buff *skb, struct tipc_stats *s)
{
	int i;
	struct nlattr *stats;

	struct nla_map {
		u32 key;
		u32 val;
	};

	struct nla_map map[] = {
		{TIPC_NLA_STATS_RX_INFO, s->recv_info},
		{TIPC_NLA_STATS_RX_FRAGMENTS, s->recv_fragments},
		{TIPC_NLA_STATS_RX_FRAGMENTED, s->recv_fragmented},
		{TIPC_NLA_STATS_RX_BUNDLES, s->recv_bundles},
		{TIPC_NLA_STATS_RX_BUNDLED, s->recv_bundled},
		{TIPC_NLA_STATS_TX_INFO, s->sent_info},
		{TIPC_NLA_STATS_TX_FRAGMENTS, s->sent_fragments},
		{TIPC_NLA_STATS_TX_FRAGMENTED, s->sent_fragmented},
		{TIPC_NLA_STATS_TX_BUNDLES, s->sent_bundles},
		{TIPC_NLA_STATS_TX_BUNDLED, s->sent_bundled},
		{TIPC_NLA_STATS_MSG_PROF_TOT, (s->msg_length_counts) ?
			s->msg_length_counts : 1},
		{TIPC_NLA_STATS_MSG_LEN_CNT, s->msg_length_counts},
		{TIPC_NLA_STATS_MSG_LEN_TOT, s->msg_lengths_total},
		{TIPC_NLA_STATS_MSG_LEN_P0, s->msg_length_profile[0]},
		{TIPC_NLA_STATS_MSG_LEN_P1, s->msg_length_profile[1]},
		{TIPC_NLA_STATS_MSG_LEN_P2, s->msg_length_profile[2]},
		{TIPC_NLA_STATS_MSG_LEN_P3, s->msg_length_profile[3]},
		{TIPC_NLA_STATS_MSG_LEN_P4, s->msg_length_profile[4]},
		{TIPC_NLA_STATS_MSG_LEN_P5, s->msg_length_profile[5]},
		{TIPC_NLA_STATS_MSG_LEN_P6, s->msg_length_profile[6]},
		{TIPC_NLA_STATS_RX_STATES, s->recv_states},
		{TIPC_NLA_STATS_RX_PROBES, s->recv_probes},
		{TIPC_NLA_STATS_RX_NACKS, s->recv_nacks},
		{TIPC_NLA_STATS_RX_DEFERRED, s->deferred_recv},
		{TIPC_NLA_STATS_TX_STATES, s->sent_states},
		{TIPC_NLA_STATS_TX_PROBES, s->sent_probes},
		{TIPC_NLA_STATS_TX_NACKS, s->sent_nacks},
		{TIPC_NLA_STATS_TX_ACKS, s->sent_acks},
		{TIPC_NLA_STATS_RETRANSMITTED, s->retransmitted},
		{TIPC_NLA_STATS_DUPLICATES, s->duplicates},
		{TIPC_NLA_STATS_LINK_CONGS, s->link_congs},
		{TIPC_NLA_STATS_MAX_QUEUE, s->max_queue_sz},
		{TIPC_NLA_STATS_AVG_QUEUE, s->queue_sz_counts ?
			(s->accu_queue_sz / s->queue_sz_counts) : 0}
	};

	stats = nla_nest_start(skb, TIPC_NLA_LINK_STATS);
	if (!stats)
		return -EMSGSIZE;

	for (i = 0; i <  ARRAY_SIZE(map); i++)
		if (nla_put_u32(skb, map[i].key, map[i].val))
			goto msg_full;

	nla_nest_end(skb, stats);

	return 0;
msg_full:
	nla_nest_cancel(skb, stats);

	return -EMSGSIZE;
}

/* Caller should hold appropriate locks to protect the link */
static int __tipc_nl_add_link(struct net *net, struct tipc_nl_msg *msg,
			      struct tipc_link *link, int nlflags)
{
	int err;
	void *hdr;
	struct nlattr *attrs;
	struct nlattr *prop;
	struct tipc_net *tn = net_generic(net, tipc_net_id);

	hdr = genlmsg_put(msg->skb, msg->portid, msg->seq, &tipc_genl_family,
			  nlflags, TIPC_NL_LINK_GET);
	if (!hdr)
		return -EMSGSIZE;

	attrs = nla_nest_start(msg->skb, TIPC_NLA_LINK);
	if (!attrs)
		goto msg_full;

	if (nla_put_string(msg->skb, TIPC_NLA_LINK_NAME, link->name))
		goto attr_msg_full;
	if (nla_put_u32(msg->skb, TIPC_NLA_LINK_DEST,
			tipc_cluster_mask(tn->own_addr)))
		goto attr_msg_full;
	if (nla_put_u32(msg->skb, TIPC_NLA_LINK_MTU, link->mtu))
		goto attr_msg_full;
	if (nla_put_u32(msg->skb, TIPC_NLA_LINK_RX, link->rcv_nxt))
		goto attr_msg_full;
	if (nla_put_u32(msg->skb, TIPC_NLA_LINK_TX, link->snd_nxt))
		goto attr_msg_full;

	if (tipc_link_is_up(link))
		if (nla_put_flag(msg->skb, TIPC_NLA_LINK_UP))
			goto attr_msg_full;
	if (link->active)
		if (nla_put_flag(msg->skb, TIPC_NLA_LINK_ACTIVE))
			goto attr_msg_full;

	prop = nla_nest_start(msg->skb, TIPC_NLA_LINK_PROP);
	if (!prop)
		goto attr_msg_full;
	if (nla_put_u32(msg->skb, TIPC_NLA_PROP_PRIO, link->priority))
		goto prop_msg_full;
	if (nla_put_u32(msg->skb, TIPC_NLA_PROP_TOL, link->tolerance))
		goto prop_msg_full;
	if (nla_put_u32(msg->skb, TIPC_NLA_PROP_WIN,
			link->window))
		goto prop_msg_full;
	if (nla_put_u32(msg->skb, TIPC_NLA_PROP_PRIO, link->priority))
		goto prop_msg_full;
	nla_nest_end(msg->skb, prop);

	err = __tipc_nl_add_stats(msg->skb, &link->stats);
	if (err)
		goto attr_msg_full;

	nla_nest_end(msg->skb, attrs);
	genlmsg_end(msg->skb, hdr);

	return 0;

prop_msg_full:
	nla_nest_cancel(msg->skb, prop);
attr_msg_full:
	nla_nest_cancel(msg->skb, attrs);
msg_full:
	genlmsg_cancel(msg->skb, hdr);

	return -EMSGSIZE;
}

/* Caller should hold node lock  */
static int __tipc_nl_add_node_links(struct net *net, struct tipc_nl_msg *msg,
				    struct tipc_node *node, u32 *prev_link)
{
	u32 i;
	int err;

	for (i = *prev_link; i < MAX_BEARERS; i++) {
		*prev_link = i;

		if (!node->links[i].link)
			continue;

		err = __tipc_nl_add_link(net, msg,
					 node->links[i].link, NLM_F_MULTI);
		if (err)
			return err;
	}
	*prev_link = 0;

	return 0;
}

int tipc_nl_link_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_node *node;
	struct tipc_nl_msg msg;
	u32 prev_node = cb->args[0];
	u32 prev_link = cb->args[1];
	int done = cb->args[2];
	int err;

	if (done)
		return 0;

	msg.skb = skb;
	msg.portid = NETLINK_CB(cb->skb).portid;
	msg.seq = cb->nlh->nlmsg_seq;

	rcu_read_lock();
	if (prev_node) {
		node = tipc_node_find(net, prev_node);
		if (!node) {
			/* We never set seq or call nl_dump_check_consistent()
			 * this means that setting prev_seq here will cause the
			 * consistence check to fail in the netlink callback
			 * handler. Resulting in the last NLMSG_DONE message
			 * having the NLM_F_DUMP_INTR flag set.
			 */
			cb->prev_seq = 1;
			goto out;
		}
		tipc_node_put(node);

		list_for_each_entry_continue_rcu(node, &tn->node_list,
						 list) {
			tipc_node_lock(node);
			err = __tipc_nl_add_node_links(net, &msg, node,
						       &prev_link);
			tipc_node_unlock(node);
			if (err)
				goto out;

			prev_node = node->addr;
		}
	} else {
		err = tipc_nl_add_bc_link(net, &msg);
		if (err)
			goto out;

		list_for_each_entry_rcu(node, &tn->node_list, list) {
			tipc_node_lock(node);
			err = __tipc_nl_add_node_links(net, &msg, node,
						       &prev_link);
			tipc_node_unlock(node);
			if (err)
				goto out;

			prev_node = node->addr;
		}
	}
	done = 1;
out:
	rcu_read_unlock();

	cb->args[0] = prev_node;
	cb->args[1] = prev_link;
	cb->args[2] = done;

	return skb->len;
}

int tipc_nl_link_get(struct sk_buff *skb, struct genl_info *info)
{
	struct net *net = genl_info_net(info);
	struct tipc_nl_msg msg;
	char *name;
	int err;

	msg.portid = info->snd_portid;
	msg.seq = info->snd_seq;

	if (!info->attrs[TIPC_NLA_LINK_NAME])
		return -EINVAL;
	name = nla_data(info->attrs[TIPC_NLA_LINK_NAME]);

	msg.skb = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!msg.skb)
		return -ENOMEM;

	if (strcmp(name, tipc_bclink_name) == 0) {
		err = tipc_nl_add_bc_link(net, &msg);
		if (err) {
			nlmsg_free(msg.skb);
			return err;
		}
	} else {
		int bearer_id;
		struct tipc_node *node;
		struct tipc_link *link;

		node = tipc_link_find_owner(net, name, &bearer_id);
		if (!node)
			return -EINVAL;

		tipc_node_lock(node);
		link = node->links[bearer_id].link;
		if (!link) {
			tipc_node_unlock(node);
			nlmsg_free(msg.skb);
			return -EINVAL;
		}

		err = __tipc_nl_add_link(net, &msg, link, 0);
		tipc_node_unlock(node);
		if (err) {
			nlmsg_free(msg.skb);
			return err;
		}
	}

	return genlmsg_reply(msg.skb, info);
}

int tipc_nl_link_reset_stats(struct sk_buff *skb, struct genl_info *info)
{
	int err;
	char *link_name;
	unsigned int bearer_id;
	struct tipc_link *link;
	struct tipc_node *node;
	struct nlattr *attrs[TIPC_NLA_LINK_MAX + 1];
	struct net *net = sock_net(skb->sk);

	if (!info->attrs[TIPC_NLA_LINK])
		return -EINVAL;

	err = nla_parse_nested(attrs, TIPC_NLA_LINK_MAX,
			       info->attrs[TIPC_NLA_LINK],
			       tipc_nl_link_policy);
	if (err)
		return err;

	if (!attrs[TIPC_NLA_LINK_NAME])
		return -EINVAL;

	link_name = nla_data(attrs[TIPC_NLA_LINK_NAME]);

	if (strcmp(link_name, tipc_bclink_name) == 0) {
		err = tipc_bclink_reset_stats(net);
		if (err)
			return err;
		return 0;
	}

	node = tipc_link_find_owner(net, link_name, &bearer_id);
	if (!node)
		return -EINVAL;

	tipc_node_lock(node);

	link = node->links[bearer_id].link;
	if (!link) {
		tipc_node_unlock(node);
		return -EINVAL;
	}

	link_reset_statistics(link);

	tipc_node_unlock(node);

	return 0;
}
