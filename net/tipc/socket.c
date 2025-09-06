/*
 * net/tipc/socket.c: TIPC socket API
 *
 * Copyright (c) 2001-2007, Ericsson AB
 * Copyright (c) 2004-2008, Wind River Systems
 * Copyright (c) 2001-2007, 2012-2015, Ericsson AB
 * Copyright (c) 2004-2008, 2010-2013, Wind River Systems
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

#include <linux/module.h>
#include <linux/types.h>
#include <linux/net.h>
#include <linux/socket.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/fcntl.h>
#include <asm/string.h>
#include <asm/atomic.h>
#include <net/sock.h>

#include <linux/tipc.h>
#include <linux/tipc_config.h>
#include <net/tipc/tipc_msg.h>
#include <net/tipc/tipc_port.h>

#include "core.h"

#define SS_LISTENING	-1	/* socket is listening */
#define SS_READY	-2	/* socket is connectionless */

#define OVERLOAD_LIMIT_BASE	5000
#define CONN_TIMEOUT_DEFAULT	8000	/* default connect timeout = 8s */

struct tipc_sock {
	struct sock sk;
	struct tipc_port *p;
	struct tipc_portid peer_name;
};

#define tipc_sk(sk) ((struct tipc_sock *)(sk))
#define tipc_sk_port(sk) ((struct tipc_port *)(tipc_sk(sk)->p))

static int backlog_rcv(struct sock *sk, struct sk_buff *skb);
static u32 dispatch(struct tipc_port *tport, struct sk_buff *buf);
static void wakeupdispatch(struct tipc_port *tport);
#include <linux/rhashtable.h>
#include "core.h"
#include "name_table.h"
#include "node.h"
#include "link.h"
#include "name_distr.h"
#include "socket.h"
#include "bcast.h"

#define SS_LISTENING		-1	/* socket is listening */
#define SS_READY		-2	/* socket is connectionless */

#define CONN_TIMEOUT_DEFAULT	8000	/* default connect timeout = 8s */
#define CONN_PROBING_INTERVAL	msecs_to_jiffies(3600000)  /* [ms] => 1 h */
#define TIPC_FWD_MSG		1
#define TIPC_CONN_OK		0
#define TIPC_CONN_PROBING	1
#define TIPC_MAX_PORT		0xffffffff
#define TIPC_MIN_PORT		1

/**
 * struct tipc_sock - TIPC socket structure
 * @sk: socket - interacts with 'port' and with user via the socket API
 * @connected: non-zero if port is currently connected to a peer port
 * @conn_type: TIPC type used when connection was established
 * @conn_instance: TIPC instance used when connection was established
 * @published: non-zero if port has one or more associated names
 * @max_pkt: maximum packet size "hint" used when building messages sent by port
 * @portid: unique port identity in TIPC socket hash table
 * @phdr: preformatted message header used when sending messages
 * @port_list: adjacent ports in TIPC's global list of ports
 * @publications: list of publications for port
 * @pub_count: total # of publications port has made during its lifetime
 * @probing_state:
 * @probing_intv:
 * @conn_timeout: the time we can wait for an unresponded setup request
 * @dupl_rcvcnt: number of bytes counted twice, in both backlog and rcv queue
 * @link_cong: non-zero if owner must sleep because of link congestion
 * @sent_unacked: # messages sent by socket, and not yet acked by peer
 * @rcv_unacked: # messages read by user, but not yet acked back to peer
 * @remote: 'connected' peer for dgram/rdm
 * @node: hash table node
 * @rcu: rcu struct for tipc_sock
 */
struct tipc_sock {
	struct sock sk;
	int connected;
	u32 conn_type;
	u32 conn_instance;
	int published;
	u32 max_pkt;
	u32 portid;
	struct tipc_msg phdr;
	struct list_head sock_list;
	struct list_head publications;
	u32 pub_count;
	u32 probing_state;
	unsigned long probing_intv;
	uint conn_timeout;
	atomic_t dupl_rcvcnt;
	bool link_cong;
	uint sent_unacked;
	uint rcv_unacked;
	struct sockaddr_tipc remote;
	struct rhash_head node;
	struct rcu_head rcu;
};

static int tipc_backlog_rcv(struct sock *sk, struct sk_buff *skb);
static void tipc_data_ready(struct sock *sk);
static void tipc_write_space(struct sock *sk);
static void tipc_sock_destruct(struct sock *sk);
static int tipc_release(struct socket *sock);
static int tipc_accept(struct socket *sock, struct socket *new_sock, int flags);
static int tipc_wait_for_sndmsg(struct socket *sock, long *timeo_p);
static void tipc_sk_timeout(unsigned long data);
static int tipc_sk_publish(struct tipc_sock *tsk, uint scope,
			   struct tipc_name_seq const *seq);
static int tipc_sk_withdraw(struct tipc_sock *tsk, uint scope,
			    struct tipc_name_seq const *seq);
static struct tipc_sock *tipc_sk_lookup(struct net *net, u32 portid);
static int tipc_sk_insert(struct tipc_sock *tsk);
static void tipc_sk_remove(struct tipc_sock *tsk);
static int __tipc_send_stream(struct socket *sock, struct msghdr *m,
			      size_t dsz);
static int __tipc_sendmsg(struct socket *sock, struct msghdr *m, size_t dsz);

static const struct proto_ops packet_ops;
static const struct proto_ops stream_ops;
static const struct proto_ops msg_ops;

static struct proto tipc_proto;

static int sockets_enabled = 0;

static atomic_t tipc_queue_size = ATOMIC_INIT(0);
static struct proto tipc_proto;

static const struct nla_policy tipc_nl_sock_policy[TIPC_NLA_SOCK_MAX + 1] = {
	[TIPC_NLA_SOCK_UNSPEC]		= { .type = NLA_UNSPEC },
	[TIPC_NLA_SOCK_ADDR]		= { .type = NLA_U32 },
	[TIPC_NLA_SOCK_REF]		= { .type = NLA_U32 },
	[TIPC_NLA_SOCK_CON]		= { .type = NLA_NESTED },
	[TIPC_NLA_SOCK_HAS_PUBL]	= { .type = NLA_FLAG }
};

static const struct rhashtable_params tsk_rht_params;

/*
 * Revised TIPC socket locking policy:
 *
 * Most socket operations take the standard socket lock when they start
 * and hold it until they finish (or until they need to sleep).  Acquiring
 * this lock grants the owner exclusive access to the fields of the socket
 * data structures, with the exception of the backlog queue.  A few socket
 * operations can be done without taking the socket lock because they only
 * read socket information that never changes during the life of the socket.
 *
 * Socket operations may acquire the lock for the associated TIPC port if they
 * need to perform an operation on the port.  If any routine needs to acquire
 * both the socket lock and the port lock it must take the socket lock first
 * to avoid the risk of deadlock.
 *
 * The dispatcher handling incoming messages cannot grab the socket lock in
 * the standard fashion, since invoked it runs at the BH level and cannot block.
 * Instead, it checks to see if the socket lock is currently owned by someone,
 * and either handles the message itself or adds it to the socket's backlog
 * queue; in the latter case the queued message is processed once the process
 * owning the socket lock releases it.
 *
 * NOTE: Releasing the socket lock while an operation is sleeping overcomes
 * the problem of a blocked socket operation preventing any other operations
 * from occurring.  However, applications must be careful if they have
 * multiple threads trying to send (or receive) on the same socket, as these
 * operations might interfere with each other.  For example, doing a connect
 * and a receive at the same time might allow the receive to consume the
 * ACK message meant for the connect.  While additional work could be done
 * to try and overcome this, it doesn't seem to be worthwhile at the present.
 *
 * NOTE: Releasing the socket lock while an operation is sleeping also ensures
 * that another operation that must be performed in a non-blocking manner is
 * not delayed for very long because the lock has already been taken.
 *
 * NOTE: This code assumes that certain fields of a port/socket pair are
 * constant over its lifetime; such fields can be examined without taking
 * the socket lock and/or port lock, and do not need to be re-read even
 * after resuming processing after waiting.  These fields include:
 *   - socket type
 *   - pointer to socket sk structure (aka tipc_sock structure)
 *   - pointer to port structure
 *   - port reference
 */

/**
 * advance_rx_queue - discard first buffer in socket receive queue
 *
 * Caller must hold socket lock
 */

static void advance_rx_queue(struct sock *sk)
{
	buf_discard(__skb_dequeue(&sk->sk_receive_queue));
	atomic_dec(&tipc_queue_size);
}

/**
 * discard_rx_queue - discard all buffers in socket receive queue
 *
 * Caller must hold socket lock
 */

static void discard_rx_queue(struct sock *sk)
{
	struct sk_buff *buf;

	while ((buf = __skb_dequeue(&sk->sk_receive_queue))) {
		atomic_dec(&tipc_queue_size);
		buf_discard(buf);
	}
}

/**
 * reject_rx_queue - reject all buffers in socket receive queue
 *
 * Caller must hold socket lock
 */

static void reject_rx_queue(struct sock *sk)
{
	struct sk_buff *buf;

	while ((buf = __skb_dequeue(&sk->sk_receive_queue))) {
		tipc_reject_msg(buf, TIPC_ERR_NO_PORT);
		atomic_dec(&tipc_queue_size);
	}
}

/**
 * tipc_create - create a TIPC socket
 * @net: network namespace (must be default network)
 * @sock: pre-allocated socket structure
 * @protocol: protocol indicator (must be 0)
static u32 tsk_own_node(struct tipc_sock *tsk)
{
	return msg_prevnode(&tsk->phdr);
}

static u32 tsk_peer_node(struct tipc_sock *tsk)
{
	return msg_destnode(&tsk->phdr);
}

static u32 tsk_peer_port(struct tipc_sock *tsk)
{
	return msg_destport(&tsk->phdr);
}

static  bool tsk_unreliable(struct tipc_sock *tsk)
{
	return msg_src_droppable(&tsk->phdr) != 0;
}

static void tsk_set_unreliable(struct tipc_sock *tsk, bool unreliable)
{
	msg_set_src_droppable(&tsk->phdr, unreliable ? 1 : 0);
}

static bool tsk_unreturnable(struct tipc_sock *tsk)
{
	return msg_dest_droppable(&tsk->phdr) != 0;
}

static void tsk_set_unreturnable(struct tipc_sock *tsk, bool unreturnable)
{
	msg_set_dest_droppable(&tsk->phdr, unreturnable ? 1 : 0);
}

static int tsk_importance(struct tipc_sock *tsk)
{
	return msg_importance(&tsk->phdr);
}

static int tsk_set_importance(struct tipc_sock *tsk, int imp)
{
	if (imp > TIPC_CRITICAL_IMPORTANCE)
		return -EINVAL;
	msg_set_importance(&tsk->phdr, (u32)imp);
	return 0;
}

static struct tipc_sock *tipc_sk(const struct sock *sk)
{
	return container_of(sk, struct tipc_sock, sk);
}

static int tsk_conn_cong(struct tipc_sock *tsk)
{
	return tsk->sent_unacked >= TIPC_FLOWCTRL_WIN;
}

/**
 * tsk_advance_rx_queue - discard first buffer in socket receive queue
 *
 * Caller must hold socket lock
 */
static void tsk_advance_rx_queue(struct sock *sk)
{
	kfree_skb(__skb_dequeue(&sk->sk_receive_queue));
}

/* tipc_sk_respond() : send response message back to sender
 */
static void tipc_sk_respond(struct sock *sk, struct sk_buff *skb, int err)
{
	u32 selector;
	u32 dnode;
	u32 onode = tipc_own_addr(sock_net(sk));

	if (!tipc_msg_reverse(onode, &skb, err))
		return;

	dnode = msg_destnode(buf_msg(skb));
	selector = msg_origport(buf_msg(skb));
	tipc_node_xmit_skb(sock_net(sk), skb, dnode, selector);
}

/**
 * tsk_rej_rx_queue - reject all buffers in socket receive queue
 *
 * Caller must hold socket lock
 */
static void tsk_rej_rx_queue(struct sock *sk)
{
	struct sk_buff *skb;

	while ((skb = __skb_dequeue(&sk->sk_receive_queue)))
		tipc_sk_respond(sk, skb, TIPC_ERR_NO_PORT);
}

/* tsk_peer_msg - verify if message was sent by connected port's peer
 *
 * Handles cases where the node's network address has changed from
 * the default of <0.0.0> to its configured setting.
 */
static bool tsk_peer_msg(struct tipc_sock *tsk, struct tipc_msg *msg)
{
	struct tipc_net *tn = net_generic(sock_net(&tsk->sk), tipc_net_id);
	u32 peer_port = tsk_peer_port(tsk);
	u32 orig_node;
	u32 peer_node;

	if (unlikely(!tsk->connected))
		return false;

	if (unlikely(msg_origport(msg) != peer_port))
		return false;

	orig_node = msg_orignode(msg);
	peer_node = tsk_peer_node(tsk);

	if (likely(orig_node == peer_node))
		return true;

	if (!orig_node && (peer_node == tn->own_addr))
		return true;

	if (!peer_node && (orig_node == tn->own_addr))
		return true;

	return false;
}

/**
 * tipc_sk_create - create a TIPC socket
 * @net: network namespace (must be default network)
 * @sock: pre-allocated socket structure
 * @protocol: protocol indicator (must be 0)
 * @kern: caused by kernel or by userspace?
 *
 * This routine creates additional data structures used by the TIPC socket,
 * initializes them, and links them together.
 *
 * Returns 0 on success, errno otherwise
 */

static int tipc_create(struct net *net, struct socket *sock, int protocol)
{
	const struct proto_ops *ops;
	socket_state state;
	struct sock *sk;
	struct tipc_port *tp_ptr;

	/* Validate arguments */

	if (net != &init_net)
		return -EAFNOSUPPORT;

static int tipc_sk_create(struct net *net, struct socket *sock,
			  int protocol, int kern)
{
	struct tipc_net *tn;
	const struct proto_ops *ops;
	socket_state state;
	struct sock *sk;
	struct tipc_sock *tsk;
	struct tipc_msg *msg;

	/* Validate arguments */
	if (unlikely(protocol != 0))
		return -EPROTONOSUPPORT;

	switch (sock->type) {
	case SOCK_STREAM:
		ops = &stream_ops;
		state = SS_UNCONNECTED;
		break;
	case SOCK_SEQPACKET:
		ops = &packet_ops;
		state = SS_UNCONNECTED;
		break;
	case SOCK_DGRAM:
	case SOCK_RDM:
		ops = &msg_ops;
		state = SS_READY;
		break;
	default:
		return -EPROTOTYPE;
	}

	/* Allocate socket's protocol area */

	sk = sk_alloc(net, AF_TIPC, GFP_KERNEL, &tipc_proto);
	if (sk == NULL)
		return -ENOMEM;

	/* Allocate TIPC port for socket to use */

	tp_ptr = tipc_createport_raw(sk, &dispatch, &wakeupdispatch,
				     TIPC_LOW_IMPORTANCE);
	if (unlikely(!tp_ptr)) {
		sk_free(sk);
		return -ENOMEM;
	}

	/* Finish initializing socket data structures */

	sock->ops = ops;
	sock->state = state;

	sock_init_data(sock, sk);
	sk->sk_rcvtimeo = msecs_to_jiffies(CONN_TIMEOUT_DEFAULT);
	sk->sk_backlog_rcv = backlog_rcv;
	tipc_sk(sk)->p = tp_ptr;

	spin_unlock_bh(tp_ptr->lock);

	if (sock->state == SS_READY) {
		tipc_set_portunreturnable(tp_ptr->ref, 1);
		if (sock->type == SOCK_DGRAM)
			tipc_set_portunreliable(tp_ptr->ref, 1);
	}

	atomic_inc(&tipc_user_count);
	return 0;
}

/**
 * release - destroy a TIPC socket
	sk = sk_alloc(net, AF_TIPC, GFP_KERNEL, &tipc_proto, kern);
	if (sk == NULL)
		return -ENOMEM;

	tsk = tipc_sk(sk);
	tsk->max_pkt = MAX_PKT_DEFAULT;
	INIT_LIST_HEAD(&tsk->publications);
	msg = &tsk->phdr;
	tn = net_generic(sock_net(sk), tipc_net_id);
	tipc_msg_init(tn->own_addr, msg, TIPC_LOW_IMPORTANCE, TIPC_NAMED_MSG,
		      NAMED_H_SIZE, 0);

	/* Finish initializing socket data structures */
	sock->ops = ops;
	sock->state = state;
	sock_init_data(sock, sk);
	if (tipc_sk_insert(tsk)) {
		pr_warn("Socket create failed; port numbrer exhausted\n");
		return -EINVAL;
	}
	msg_set_origport(msg, tsk->portid);
	setup_timer(&sk->sk_timer, tipc_sk_timeout, (unsigned long)tsk);
	sk->sk_backlog_rcv = tipc_backlog_rcv;
	sk->sk_rcvbuf = sysctl_tipc_rmem[1];
	sk->sk_data_ready = tipc_data_ready;
	sk->sk_write_space = tipc_write_space;
	sk->sk_destruct = tipc_sock_destruct;
	tsk->conn_timeout = CONN_TIMEOUT_DEFAULT;
	tsk->sent_unacked = 0;
	atomic_set(&tsk->dupl_rcvcnt, 0);

	if (sock->state == SS_READY) {
		tsk_set_unreturnable(tsk, true);
		if (sock->type == SOCK_DGRAM)
			tsk_set_unreliable(tsk, true);
	}
	return 0;
}

static void tipc_sk_callback(struct rcu_head *head)
{
	struct tipc_sock *tsk = container_of(head, struct tipc_sock, rcu);

	sock_put(&tsk->sk);
}

/**
 * tipc_release - destroy a TIPC socket
 * @sock: socket to destroy
 *
 * This routine cleans up any messages that are still queued on the socket.
 * For DGRAM and RDM socket types, all queued messages are rejected.
 * For SEQPACKET and STREAM socket types, the first message is rejected
 * and any others are discarded.  (If the first message on a STREAM socket
 * is partially-read, it is discarded and the next one is rejected instead.)
 *
 * NOTE: Rejected messages are not necessarily returned to the sender!  They
 * are returned or discarded according to the "destination droppable" setting
 * specified for the message by the sender.
 *
 * Returns 0 on success, errno otherwise
 */

static int release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct tipc_port *tport;
	struct sk_buff *buf;
	int res;
static int tipc_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct net *net;
	struct tipc_sock *tsk;
	struct sk_buff *skb;
	u32 dnode;

	/*
	 * Exit if socket isn't fully initialized (occurs when a failed accept()
	 * releases a pre-allocated child socket that was never used)
	 */

	if (sk == NULL)
		return 0;

	tport = tipc_sk_port(sk);
	if (sk == NULL)
		return 0;

	net = sock_net(sk);
	tsk = tipc_sk(sk);
	lock_sock(sk);

	/*
	 * Reject all unreceived messages, except on an active connection
	 * (which disconnects locally & sends a 'FIN+' to peer)
	 */

	while (sock->state != SS_DISCONNECTING) {
		buf = __skb_dequeue(&sk->sk_receive_queue);
		if (buf == NULL)
			break;
		atomic_dec(&tipc_queue_size);
		if (TIPC_SKB_CB(buf)->handle != msg_data(buf_msg(buf)))
			buf_discard(buf);
	dnode = tsk_peer_node(tsk);
	while (sock->state != SS_DISCONNECTING) {
		skb = __skb_dequeue(&sk->sk_receive_queue);
		if (skb == NULL)
			break;
		if (TIPC_SKB_CB(skb)->handle != NULL)
			kfree_skb(skb);
		else {
			if ((sock->state == SS_CONNECTING) ||
			    (sock->state == SS_CONNECTED)) {
				sock->state = SS_DISCONNECTING;
				tipc_disconnect(tport->ref);
			}
			tipc_reject_msg(buf, TIPC_ERR_NO_PORT);
		}
	}

	/*
	 * Delete TIPC port; this ensures no more messages are queued
	 * (also disconnects an active connection & sends a 'FIN-' to peer)
	 */

	res = tipc_deleteport(tport->ref);

	/* Discard any remaining (connection-based) messages in receive queue */

	discard_rx_queue(sk);

	/* Reject any messages that accumulated in backlog queue */

	sock->state = SS_DISCONNECTING;
	release_sock(sk);

	sock_put(sk);
	sock->sk = NULL;

	atomic_dec(&tipc_user_count);
	return res;
}

/**
 * bind - associate or disassocate TIPC name(s) with a socket
				tsk->connected = 0;
				tipc_node_remove_conn(net, dnode, tsk->portid);
			}
			tipc_sk_respond(sk, skb, TIPC_ERR_NO_PORT);
		}
	}

	tipc_sk_withdraw(tsk, 0, NULL);
	sk_stop_timer(sk, &sk->sk_timer);
	tipc_sk_remove(tsk);
	if (tsk->connected) {
		skb = tipc_msg_create(TIPC_CRITICAL_IMPORTANCE,
				      TIPC_CONN_MSG, SHORT_H_SIZE, 0, dnode,
				      tsk_own_node(tsk), tsk_peer_port(tsk),
				      tsk->portid, TIPC_ERR_NO_PORT);
		if (skb)
			tipc_node_xmit_skb(net, skb, dnode, tsk->portid);
		tipc_node_remove_conn(net, dnode, tsk->portid);
	}

	/* Reject any messages that accumulated in backlog queue */
	sock->state = SS_DISCONNECTING;
	release_sock(sk);

	call_rcu(&tsk->rcu, tipc_sk_callback);
	sock->sk = NULL;

	return 0;
}

/**
 * tipc_bind - associate or disassocate TIPC name(s) with a socket
 * @sock: socket structure
 * @uaddr: socket address describing name(s) and desired operation
 * @uaddr_len: size of socket address data structure
 *
 * Name and name sequence binding is indicated using a positive scope value;
 * a negative scope value unbinds the specified name.  Specifying no name
 * (i.e. a socket address length of 0) unbinds all names from the socket.
 *
 * Returns 0 on success, errno otherwise
 *
 * NOTE: This routine doesn't need to take the socket lock since it doesn't
 *       access any non-constant socket information.
 */

static int bind(struct socket *sock, struct sockaddr *uaddr, int uaddr_len)
{
	struct sockaddr_tipc *addr = (struct sockaddr_tipc *)uaddr;
	u32 portref = tipc_sk_port(sock->sk)->ref;

	if (unlikely(!uaddr_len))
		return tipc_withdraw(portref, 0, NULL);

	if (uaddr_len < sizeof(struct sockaddr_tipc))
		return -EINVAL;
	if (addr->family != AF_TIPC)
		return -EAFNOSUPPORT;

	if (addr->addrtype == TIPC_ADDR_NAME)
		addr->addr.nameseq.upper = addr->addr.nameseq.lower;
	else if (addr->addrtype != TIPC_ADDR_NAMESEQ)
		return -EAFNOSUPPORT;

	return (addr->scope > 0) ?
		tipc_publish(portref, addr->scope, &addr->addr.nameseq) :
		tipc_withdraw(portref, -addr->scope, &addr->addr.nameseq);
}

/**
 * get_name - get port ID of socket or peer socket
static int tipc_bind(struct socket *sock, struct sockaddr *uaddr,
		     int uaddr_len)
{
	struct sock *sk = sock->sk;
	struct sockaddr_tipc *addr = (struct sockaddr_tipc *)uaddr;
	struct tipc_sock *tsk = tipc_sk(sk);
	int res = -EINVAL;

	lock_sock(sk);
	if (unlikely(!uaddr_len)) {
		res = tipc_sk_withdraw(tsk, 0, NULL);
		goto exit;
	}

	if (uaddr_len < sizeof(struct sockaddr_tipc)) {
		res = -EINVAL;
		goto exit;
	}
	if (addr->family != AF_TIPC) {
		res = -EAFNOSUPPORT;
		goto exit;
	}

	if (addr->addrtype == TIPC_ADDR_NAME)
		addr->addr.nameseq.upper = addr->addr.nameseq.lower;
	else if (addr->addrtype != TIPC_ADDR_NAMESEQ) {
		res = -EAFNOSUPPORT;
		goto exit;
	}

	if ((addr->addr.nameseq.type < TIPC_RESERVED_TYPES) &&
	    (addr->addr.nameseq.type != TIPC_TOP_SRV) &&
	    (addr->addr.nameseq.type != TIPC_CFG_SRV)) {
		res = -EACCES;
		goto exit;
	}

	res = (addr->scope > 0) ?
		tipc_sk_publish(tsk, addr->scope, &addr->addr.nameseq) :
		tipc_sk_withdraw(tsk, -addr->scope, &addr->addr.nameseq);
exit:
	release_sock(sk);
	return res;
}

/**
 * tipc_getname - get port ID of socket or peer socket
 * @sock: socket structure
 * @uaddr: area for returned socket address
 * @uaddr_len: area for returned length of socket address
 * @peer: 0 = own ID, 1 = current peer ID, 2 = current/former peer ID
 *
 * Returns 0 on success, errno otherwise
 *
 * NOTE: This routine doesn't need to take the socket lock since it only
 *       accesses socket information that is unchanging (or which changes in
 * 	 a completely predictable manner).
 */

static int get_name(struct socket *sock, struct sockaddr *uaddr,
		    int *uaddr_len, int peer)
{
	struct sockaddr_tipc *addr = (struct sockaddr_tipc *)uaddr;
	struct tipc_sock *tsock = tipc_sk(sock->sk);

 *       a completely predictable manner).
 */
static int tipc_getname(struct socket *sock, struct sockaddr *uaddr,
			int *uaddr_len, int peer)
{
	struct sockaddr_tipc *addr = (struct sockaddr_tipc *)uaddr;
	struct tipc_sock *tsk = tipc_sk(sock->sk);
	struct tipc_net *tn = net_generic(sock_net(sock->sk), tipc_net_id);

	memset(addr, 0, sizeof(*addr));
	if (peer) {
		if ((sock->state != SS_CONNECTED) &&
			((peer != 2) || (sock->state != SS_DISCONNECTING)))
			return -ENOTCONN;
		addr->addr.id.ref = tsock->peer_name.ref;
		addr->addr.id.node = tsock->peer_name.node;
	} else {
		tipc_ownidentity(tsock->p->ref, &addr->addr.id);
		addr->addr.id.ref = tsk_peer_port(tsk);
		addr->addr.id.node = tsk_peer_node(tsk);
	} else {
		addr->addr.id.ref = tsk->portid;
		addr->addr.id.node = tn->own_addr;
	}

	*uaddr_len = sizeof(*addr);
	addr->addrtype = TIPC_ADDR_ID;
	addr->family = AF_TIPC;
	addr->scope = 0;
	addr->addr.name.domain = 0;

	return 0;
}

/**
 * poll - read and possibly block on pollmask
 * tipc_poll - read and possibly block on pollmask
 * @file: file structure associated with the socket
 * @sock: socket for which to calculate the poll bits
 * @wait: ???
 *
 * Returns pollmask value
 *
 * COMMENTARY:
 * It appears that the usual socket locking mechanisms are not useful here
 * since the pollmask info is potentially out-of-date the moment this routine
 * exits.  TCP and other protocols seem to rely on higher level poll routines
 * to handle any preventable race conditions, so TIPC will do the same ...
 *
 * TIPC sets the returned events as follows:
 * a) POLLRDNORM and POLLIN are set if the socket's receive queue is non-empty
 *    or if a connection-oriented socket is does not have an active connection
 *    (i.e. a read operation will not block).
 * b) POLLOUT is set except when a socket's connection has been terminated
 *    (i.e. a write operation will not block).
 * c) POLLHUP is set when a socket's connection has been terminated.
 *
 * IMPORTANT: The fact that a read or write operation will not block does NOT
 * imply that the operation will succeed!
 */

static unsigned int poll(struct file *file, struct socket *sock,
			 poll_table *wait)
{
	struct sock *sk = sock->sk;
	u32 mask;

	poll_wait(file, sk->sk_sleep, wait);

	if (!skb_queue_empty(&sk->sk_receive_queue) ||
	    (sock->state == SS_UNCONNECTED) ||
	    (sock->state == SS_DISCONNECTING))
		mask = (POLLRDNORM | POLLIN);
	else
		mask = 0;

	if (sock->state == SS_DISCONNECTING)
		mask |= POLLHUP;
	else
		mask |= POLLOUT;
 *
 * socket state		flags set
 * ------------		---------
 * unconnected		no read flags
 *			POLLOUT if port is not congested
 *
 * connecting		POLLIN/POLLRDNORM if ACK/NACK in rx queue
 *			no write flags
 *
 * connected		POLLIN/POLLRDNORM if data in rx queue
 *			POLLOUT if port is not congested
 *
 * disconnecting	POLLIN/POLLRDNORM/POLLHUP
 *			no write flags
 *
 * listening		POLLIN if SYN in rx queue
 *			no write flags
 *
 * ready		POLLIN/POLLRDNORM if data in rx queue
 * [connectionless]	POLLOUT (since port cannot be congested)
 *
 * IMPORTANT: The fact that a read or write operation is indicated does NOT
 * imply that the operation will succeed, merely that it should be performed
 * and will not block.
 */
static unsigned int tipc_poll(struct file *file, struct socket *sock,
			      poll_table *wait)
{
	struct sock *sk = sock->sk;
	struct tipc_sock *tsk = tipc_sk(sk);
	u32 mask = 0;

	sock_poll_wait(file, sk_sleep(sk), wait);

	switch ((int)sock->state) {
	case SS_UNCONNECTED:
		if (!tsk->link_cong)
			mask |= POLLOUT;
		break;
	case SS_READY:
	case SS_CONNECTED:
		if (!tsk->link_cong && !tsk_conn_cong(tsk))
			mask |= POLLOUT;
		/* fall thru' */
	case SS_CONNECTING:
	case SS_LISTENING:
		if (!skb_queue_empty(&sk->sk_receive_queue))
			mask |= (POLLIN | POLLRDNORM);
		break;
	case SS_DISCONNECTING:
		mask = (POLLIN | POLLRDNORM | POLLHUP);
		break;
	}

	return mask;
}

/**
 * dest_name_check - verify user is permitted to send to specified port name
 * @dest: destination address
 * @m: descriptor for message to be sent
 *
 * Prevents restricted configuration commands from being issued by
 * unauthorized users.
 *
 * Returns 0 if permission is granted, otherwise errno
 */

static int dest_name_check(struct sockaddr_tipc *dest, struct msghdr *m)
{
	struct tipc_cfg_msg_hdr hdr;

	if (likely(dest->addr.name.name.type >= TIPC_RESERVED_TYPES))
		return 0;
	if (likely(dest->addr.name.name.type == TIPC_TOP_SRV))
		return 0;
	if (likely(dest->addr.name.name.type != TIPC_CFG_SRV))
		return -EACCES;

	if (copy_from_user(&hdr, m->msg_iov[0].iov_base, sizeof(hdr)))
		return -EFAULT;
	if ((ntohs(hdr.tcm_type) & 0xC000) && (!capable(CAP_NET_ADMIN)))
		return -EACCES;

 * tipc_sendmcast - send multicast message
 * @sock: socket structure
 * @seq: destination address
 * @msg: message to send
 * @dsz: total length of message data
 * @timeo: timeout to wait for wakeup
 *
 * Called from function tipc_sendmsg(), which has done all sanity checks
 * Returns the number of bytes sent on success, or errno
 */
static int tipc_sendmcast(struct  socket *sock, struct tipc_name_seq *seq,
			  struct msghdr *msg, size_t dsz, long timeo)
{
	struct sock *sk = sock->sk;
	struct tipc_sock *tsk = tipc_sk(sk);
	struct net *net = sock_net(sk);
	struct tipc_msg *mhdr = &tsk->phdr;
	struct sk_buff_head pktchain;
	struct iov_iter save = msg->msg_iter;
	uint mtu;
	int rc;

	msg_set_type(mhdr, TIPC_MCAST_MSG);
	msg_set_lookup_scope(mhdr, TIPC_CLUSTER_SCOPE);
	msg_set_destport(mhdr, 0);
	msg_set_destnode(mhdr, 0);
	msg_set_nametype(mhdr, seq->type);
	msg_set_namelower(mhdr, seq->lower);
	msg_set_nameupper(mhdr, seq->upper);
	msg_set_hdr_sz(mhdr, MCAST_H_SIZE);

	skb_queue_head_init(&pktchain);

new_mtu:
	mtu = tipc_bcast_get_mtu(net);
	rc = tipc_msg_build(mhdr, msg, 0, dsz, mtu, &pktchain);
	if (unlikely(rc < 0))
		return rc;

	do {
		rc = tipc_bcast_xmit(net, &pktchain);
		if (likely(!rc))
			return dsz;

		if (rc == -ELINKCONG) {
			tsk->link_cong = 1;
			rc = tipc_wait_for_sndmsg(sock, &timeo);
			if (!rc)
				continue;
		}
		__skb_queue_purge(&pktchain);
		if (rc == -EMSGSIZE) {
			msg->msg_iter = save;
			goto new_mtu;
		}
		break;
	} while (1);
	return rc;
}

/**
 * tipc_sk_mcast_rcv - Deliver multicast messages to all destination sockets
 * @arrvq: queue with arriving messages, to be cloned after destination lookup
 * @inputq: queue with cloned messages, delivered to socket after dest lookup
 *
 * Multi-threaded: parallel calls with reference to same queues may occur
 */
void tipc_sk_mcast_rcv(struct net *net, struct sk_buff_head *arrvq,
		       struct sk_buff_head *inputq)
{
	struct tipc_msg *msg;
	struct tipc_plist dports;
	u32 portid;
	u32 scope = TIPC_CLUSTER_SCOPE;
	struct sk_buff_head tmpq;
	uint hsz;
	struct sk_buff *skb, *_skb;

	__skb_queue_head_init(&tmpq);
	tipc_plist_init(&dports);

	skb = tipc_skb_peek(arrvq, &inputq->lock);
	for (; skb; skb = tipc_skb_peek(arrvq, &inputq->lock)) {
		msg = buf_msg(skb);
		hsz = skb_headroom(skb) + msg_hdr_sz(msg);

		if (in_own_node(net, msg_orignode(msg)))
			scope = TIPC_NODE_SCOPE;

		/* Create destination port list and message clones: */
		tipc_nametbl_mc_translate(net,
					  msg_nametype(msg), msg_namelower(msg),
					  msg_nameupper(msg), scope, &dports);
		portid = tipc_plist_pop(&dports);
		for (; portid; portid = tipc_plist_pop(&dports)) {
			_skb = __pskb_copy(skb, hsz, GFP_ATOMIC);
			if (_skb) {
				msg_set_destport(buf_msg(_skb), portid);
				__skb_queue_tail(&tmpq, _skb);
				continue;
			}
			pr_warn("Failed to clone mcast rcv buffer\n");
		}
		/* Append to inputq if not already done by other thread */
		spin_lock_bh(&inputq->lock);
		if (skb_peek(arrvq) == skb) {
			skb_queue_splice_tail_init(&tmpq, inputq);
			/* Decrease the skb's refcnt as increasing in the
			 * function tipc_skb_peek
			 */
			kfree_skb(__skb_dequeue(arrvq));
		}
		spin_unlock_bh(&inputq->lock);
		__skb_queue_purge(&tmpq);
		kfree_skb(skb);
	}
	tipc_sk_rcv(net, inputq);
}

/**
 * tipc_sk_proto_rcv - receive a connection mng protocol message
 * @tsk: receiving socket
 * @skb: pointer to message buffer.
 */
static void tipc_sk_proto_rcv(struct tipc_sock *tsk, struct sk_buff *skb,
			      struct sk_buff_head *xmitq)
{
	struct sock *sk = &tsk->sk;
	u32 onode = tsk_own_node(tsk);
	struct tipc_msg *hdr = buf_msg(skb);
	int mtyp = msg_type(hdr);
	int conn_cong;

	/* Ignore if connection cannot be validated: */
	if (!tsk_peer_msg(tsk, hdr))
		goto exit;

	tsk->probing_state = TIPC_CONN_OK;

	if (mtyp == CONN_PROBE) {
		msg_set_type(hdr, CONN_PROBE_REPLY);
		if (tipc_msg_reverse(onode, &skb, TIPC_OK))
			__skb_queue_tail(xmitq, skb);
		return;
	} else if (mtyp == CONN_ACK) {
		conn_cong = tsk_conn_cong(tsk);
		tsk->sent_unacked -= msg_msgcnt(hdr);
		if (conn_cong)
			sk->sk_write_space(sk);
	} else if (mtyp != CONN_PROBE_REPLY) {
		pr_warn("Received unknown CONN_PROTO msg\n");
	}
exit:
	kfree_skb(skb);
}

static int tipc_wait_for_sndmsg(struct socket *sock, long *timeo_p)
{
	struct sock *sk = sock->sk;
	struct tipc_sock *tsk = tipc_sk(sk);
	DEFINE_WAIT(wait);
	int done;

	do {
		int err = sock_error(sk);
		if (err)
			return err;
		if (sock->state == SS_DISCONNECTING)
			return -EPIPE;
		if (!*timeo_p)
			return -EAGAIN;
		if (signal_pending(current))
			return sock_intr_errno(*timeo_p);

		prepare_to_wait(sk_sleep(sk), &wait, TASK_INTERRUPTIBLE);
		done = sk_wait_event(sk, timeo_p, !tsk->link_cong);
		finish_wait(sk_sleep(sk), &wait);
	} while (!done);
	return 0;
}

/**
 * send_msg - send message in connectionless manner
 * @iocb: if NULL, indicates that socket lock is already held
 * @sock: socket structure
 * @m: message to send
 * @total_len: length of message
 * tipc_sendmsg - send message in connectionless manner
 * @sock: socket structure
 * @m: message to send
 * @dsz: amount of user data to be sent
 *
 * Message must have an destination specified explicitly.
 * Used for SOCK_RDM and SOCK_DGRAM messages,
 * and for 'SYN' messages on SOCK_SEQPACKET and SOCK_STREAM connections.
 * (Note: 'SYN+' is prohibited on SOCK_STREAM.)
 *
 * Returns the number of bytes sent on success, or errno otherwise
 */

static int send_msg(struct kiocb *iocb, struct socket *sock,
		    struct msghdr *m, size_t total_len)
{
	struct sock *sk = sock->sk;
	struct tipc_port *tport = tipc_sk_port(sk);
	struct sockaddr_tipc *dest = (struct sockaddr_tipc *)m->msg_name;
	int needs_conn;
	int res = -EINVAL;

	if (unlikely(!dest))
		return -EDESTADDRREQ;
	if (unlikely((m->msg_namelen < sizeof(*dest)) ||
		     (dest->family != AF_TIPC)))
		return -EINVAL;

	if (iocb)
		lock_sock(sk);

	needs_conn = (sock->state != SS_READY);
	if (unlikely(needs_conn)) {
		if (sock->state == SS_LISTENING) {
			res = -EPIPE;
			goto exit;
		}
		if (sock->state != SS_UNCONNECTED) {
			res = -EISCONN;
			goto exit;
		}
		if ((tport->published) ||
		    ((sock->type == SOCK_STREAM) && (total_len != 0))) {
			res = -EOPNOTSUPP;
			goto exit;
		}
		if (dest->addrtype == TIPC_ADDR_NAME) {
			tport->conn_type = dest->addr.name.name.type;
			tport->conn_instance = dest->addr.name.name.instance;
		}

		/* Abort any pending connection attempts (very unlikely) */

		reject_rx_queue(sk);
	}

	do {
		if (dest->addrtype == TIPC_ADDR_NAME) {
			if ((res = dest_name_check(dest, m)))
				break;
			res = tipc_send2name(tport->ref,
					     &dest->addr.name.name,
					     dest->addr.name.domain,
					     m->msg_iovlen,
					     m->msg_iov);
		}
		else if (dest->addrtype == TIPC_ADDR_ID) {
			res = tipc_send2port(tport->ref,
					     &dest->addr.id,
					     m->msg_iovlen,
					     m->msg_iov);
		}
		else if (dest->addrtype == TIPC_ADDR_MCAST) {
			if (needs_conn) {
				res = -EOPNOTSUPP;
				break;
			}
			if ((res = dest_name_check(dest, m)))
				break;
			res = tipc_multicast(tport->ref,
					     &dest->addr.nameseq,
					     0,
					     m->msg_iovlen,
					     m->msg_iov);
		}
		if (likely(res != -ELINKCONG)) {
			if (needs_conn && (res >= 0)) {
				sock->state = SS_CONNECTING;
			}
			break;
		}
		if (m->msg_flags & MSG_DONTWAIT) {
			res = -EWOULDBLOCK;
			break;
		}
		release_sock(sk);
		res = wait_event_interruptible(*sk->sk_sleep,
					       !tport->congested);
		lock_sock(sk);
		if (res)
			break;
	} while (1);

exit:
	if (iocb)
		release_sock(sk);
	return res;
}

/**
 * send_packet - send a connection-oriented message
 * @iocb: if NULL, indicates that socket lock is already held
 * @sock: socket structure
 * @m: message to send
 * @total_len: length of message
 *
 * Used for SOCK_SEQPACKET messages and SOCK_STREAM data.
 *
 * Returns the number of bytes sent on success, or errno otherwise
 */

static int send_packet(struct kiocb *iocb, struct socket *sock,
		       struct msghdr *m, size_t total_len)
{
	struct sock *sk = sock->sk;
	struct tipc_port *tport = tipc_sk_port(sk);
	struct sockaddr_tipc *dest = (struct sockaddr_tipc *)m->msg_name;
	int res;

	/* Handle implied connection establishment */

	if (unlikely(dest))
		return send_msg(iocb, sock, m, total_len);

	if (iocb)
		lock_sock(sk);

	do {
		if (unlikely(sock->state != SS_CONNECTED)) {
			if (sock->state == SS_DISCONNECTING)
				res = -EPIPE;
			else
				res = -ENOTCONN;
			break;
		}

		res = tipc_send(tport->ref, m->msg_iovlen, m->msg_iov);
		if (likely(res != -ELINKCONG)) {
			break;
		}
		if (m->msg_flags & MSG_DONTWAIT) {
			res = -EWOULDBLOCK;
			break;
		}
		release_sock(sk);
		res = wait_event_interruptible(*sk->sk_sleep,
			(!tport->congested || !tport->connected));
		lock_sock(sk);
		if (res)
			break;
	} while (1);

	if (iocb)
		release_sock(sk);
	return res;
}

/**
 * send_stream - send stream-oriented data
 * @iocb: (unused)
 * @sock: socket structure
 * @m: data to send
 * @total_len: total length of data to be sent
static int tipc_sendmsg(struct socket *sock,
			struct msghdr *m, size_t dsz)
{
	struct sock *sk = sock->sk;
	int ret;

	lock_sock(sk);
	ret = __tipc_sendmsg(sock, m, dsz);
	release_sock(sk);

	return ret;
}

static int __tipc_sendmsg(struct socket *sock, struct msghdr *m, size_t dsz)
{
	DECLARE_SOCKADDR(struct sockaddr_tipc *, dest, m->msg_name);
	struct sock *sk = sock->sk;
	struct tipc_sock *tsk = tipc_sk(sk);
	struct net *net = sock_net(sk);
	struct tipc_msg *mhdr = &tsk->phdr;
	u32 dnode, dport;
	struct sk_buff_head pktchain;
	struct sk_buff *skb;
	struct tipc_name_seq *seq;
	struct iov_iter save;
	u32 mtu;
	long timeo;
	int rc;

	if (dsz > TIPC_MAX_USER_MSG_SIZE)
		return -EMSGSIZE;
	if (unlikely(!dest)) {
		if (tsk->connected && sock->state == SS_READY)
			dest = &tsk->remote;
		else
			return -EDESTADDRREQ;
	} else if (unlikely(m->msg_namelen < sizeof(*dest)) ||
		   dest->family != AF_TIPC) {
		return -EINVAL;
	}
	if (unlikely(sock->state != SS_READY)) {
		if (sock->state == SS_LISTENING)
			return -EPIPE;
		if (sock->state != SS_UNCONNECTED)
			return -EISCONN;
		if (tsk->published)
			return -EOPNOTSUPP;
		if (dest->addrtype == TIPC_ADDR_NAME) {
			tsk->conn_type = dest->addr.name.name.type;
			tsk->conn_instance = dest->addr.name.name.instance;
		}
	}
	seq = &dest->addr.nameseq;
	timeo = sock_sndtimeo(sk, m->msg_flags & MSG_DONTWAIT);

	if (dest->addrtype == TIPC_ADDR_MCAST) {
		return tipc_sendmcast(sock, seq, m, dsz, timeo);
	} else if (dest->addrtype == TIPC_ADDR_NAME) {
		u32 type = dest->addr.name.name.type;
		u32 inst = dest->addr.name.name.instance;
		u32 domain = dest->addr.name.domain;

		dnode = domain;
		msg_set_type(mhdr, TIPC_NAMED_MSG);
		msg_set_hdr_sz(mhdr, NAMED_H_SIZE);
		msg_set_nametype(mhdr, type);
		msg_set_nameinst(mhdr, inst);
		msg_set_lookup_scope(mhdr, tipc_addr_scope(domain));
		dport = tipc_nametbl_translate(net, type, inst, &dnode);
		msg_set_destnode(mhdr, dnode);
		msg_set_destport(mhdr, dport);
		if (unlikely(!dport && !dnode))
			return -EHOSTUNREACH;
	} else if (dest->addrtype == TIPC_ADDR_ID) {
		dnode = dest->addr.id.node;
		msg_set_type(mhdr, TIPC_DIRECT_MSG);
		msg_set_lookup_scope(mhdr, 0);
		msg_set_destnode(mhdr, dnode);
		msg_set_destport(mhdr, dest->addr.id.ref);
		msg_set_hdr_sz(mhdr, BASIC_H_SIZE);
	}

	skb_queue_head_init(&pktchain);
	save = m->msg_iter;
new_mtu:
	mtu = tipc_node_get_mtu(net, dnode, tsk->portid);
	rc = tipc_msg_build(mhdr, m, 0, dsz, mtu, &pktchain);
	if (rc < 0)
		return rc;

	do {
		skb = skb_peek(&pktchain);
		TIPC_SKB_CB(skb)->wakeup_pending = tsk->link_cong;
		rc = tipc_node_xmit(net, &pktchain, dnode, tsk->portid);
		if (likely(!rc)) {
			if (sock->state != SS_READY)
				sock->state = SS_CONNECTING;
			return dsz;
		}
		if (rc == -ELINKCONG) {
			tsk->link_cong = 1;
			rc = tipc_wait_for_sndmsg(sock, &timeo);
			if (!rc)
				continue;
		}
		__skb_queue_purge(&pktchain);
		if (rc == -EMSGSIZE) {
			m->msg_iter = save;
			goto new_mtu;
		}
		break;
	} while (1);

	return rc;
}

static int tipc_wait_for_sndpkt(struct socket *sock, long *timeo_p)
{
	struct sock *sk = sock->sk;
	struct tipc_sock *tsk = tipc_sk(sk);
	DEFINE_WAIT(wait);
	int done;

	do {
		int err = sock_error(sk);
		if (err)
			return err;
		if (sock->state == SS_DISCONNECTING)
			return -EPIPE;
		else if (sock->state != SS_CONNECTED)
			return -ENOTCONN;
		if (!*timeo_p)
			return -EAGAIN;
		if (signal_pending(current))
			return sock_intr_errno(*timeo_p);

		prepare_to_wait(sk_sleep(sk), &wait, TASK_INTERRUPTIBLE);
		done = sk_wait_event(sk, timeo_p,
				     (!tsk->link_cong &&
				      !tsk_conn_cong(tsk)) ||
				     !tsk->connected);
		finish_wait(sk_sleep(sk), &wait);
	} while (!done);
	return 0;
}

/**
 * tipc_send_stream - send stream-oriented data
 * @sock: socket structure
 * @m: data to send
 * @dsz: total length of data to be transmitted
 *
 * Used for SOCK_STREAM data.
 *
 * Returns the number of bytes sent on success (or partial success),
 * or errno if no data sent
 */

static int send_stream(struct kiocb *iocb, struct socket *sock,
		       struct msghdr *m, size_t total_len)
{
	struct sock *sk = sock->sk;
	struct tipc_port *tport = tipc_sk_port(sk);
	struct msghdr my_msg;
	struct iovec my_iov;
	struct iovec *curr_iov;
	int curr_iovlen;
	char __user *curr_start;
	u32 hdr_size;
	int curr_left;
	int bytes_to_send;
	int bytes_sent;
	int res;

	lock_sock(sk);

	/* Handle special cases where there is no connection */

	if (unlikely(sock->state != SS_CONNECTED)) {
		if (sock->state == SS_UNCONNECTED) {
			res = send_packet(NULL, sock, m, total_len);
			goto exit;
		} else if (sock->state == SS_DISCONNECTING) {
			res = -EPIPE;
			goto exit;
		} else {
			res = -ENOTCONN;
			goto exit;
		}
	}

	if (unlikely(m->msg_name)) {
		res = -EISCONN;
		goto exit;
	}

	/*
	 * Send each iovec entry using one or more messages
	 *
	 * Note: This algorithm is good for the most likely case
	 * (i.e. one large iovec entry), but could be improved to pass sets
	 * of small iovec entries into send_packet().
	 */

	curr_iov = m->msg_iov;
	curr_iovlen = m->msg_iovlen;
	my_msg.msg_iov = &my_iov;
	my_msg.msg_iovlen = 1;
	my_msg.msg_flags = m->msg_flags;
	my_msg.msg_name = NULL;
	bytes_sent = 0;

	hdr_size = msg_hdr_sz(&tport->phdr);

	while (curr_iovlen--) {
		curr_start = curr_iov->iov_base;
		curr_left = curr_iov->iov_len;

		while (curr_left) {
			bytes_to_send = tport->max_pkt - hdr_size;
			if (bytes_to_send > TIPC_MAX_USER_MSG_SIZE)
				bytes_to_send = TIPC_MAX_USER_MSG_SIZE;
			if (curr_left < bytes_to_send)
				bytes_to_send = curr_left;
			my_iov.iov_base = curr_start;
			my_iov.iov_len = bytes_to_send;
			if ((res = send_packet(NULL, sock, &my_msg, 0)) < 0) {
				if (bytes_sent)
					res = bytes_sent;
				goto exit;
			}
			curr_left -= bytes_to_send;
			curr_start += bytes_to_send;
			bytes_sent += bytes_to_send;
		}

		curr_iov++;
	}
	res = bytes_sent;
exit:
	release_sock(sk);
	return res;
}

/**
 * auto_connect - complete connection setup to a remote port
 * @sock: socket structure
 * @msg: peer's response message
 *
 * Returns 0 on success, errno otherwise
 */

static int auto_connect(struct socket *sock, struct tipc_msg *msg)
{
	struct tipc_sock *tsock = tipc_sk(sock->sk);

	if (msg_errcode(msg)) {
		sock->state = SS_DISCONNECTING;
		return -ECONNREFUSED;
	}

	tsock->peer_name.ref = msg_origport(msg);
	tsock->peer_name.node = msg_orignode(msg);
	tipc_connect2port(tsock->p->ref, &tsock->peer_name);
	tipc_set_portimportance(tsock->p->ref, msg_importance(msg));
	sock->state = SS_CONNECTED;
	return 0;
static int tipc_send_stream(struct socket *sock, struct msghdr *m, size_t dsz)
{
	struct sock *sk = sock->sk;
	int ret;

	lock_sock(sk);
	ret = __tipc_send_stream(sock, m, dsz);
	release_sock(sk);

	return ret;
}

static int __tipc_send_stream(struct socket *sock, struct msghdr *m, size_t dsz)
{
	struct sock *sk = sock->sk;
	struct net *net = sock_net(sk);
	struct tipc_sock *tsk = tipc_sk(sk);
	struct tipc_msg *mhdr = &tsk->phdr;
	struct sk_buff_head pktchain;
	DECLARE_SOCKADDR(struct sockaddr_tipc *, dest, m->msg_name);
	u32 portid = tsk->portid;
	int rc = -EINVAL;
	long timeo;
	u32 dnode;
	uint mtu, send, sent = 0;
	struct iov_iter save;

	/* Handle implied connection establishment */
	if (unlikely(dest)) {
		rc = __tipc_sendmsg(sock, m, dsz);
		if (dsz && (dsz == rc))
			tsk->sent_unacked = 1;
		return rc;
	}
	if (dsz > (uint)INT_MAX)
		return -EMSGSIZE;

	if (unlikely(sock->state != SS_CONNECTED)) {
		if (sock->state == SS_DISCONNECTING)
			return -EPIPE;
		else
			return -ENOTCONN;
	}

	timeo = sock_sndtimeo(sk, m->msg_flags & MSG_DONTWAIT);
	dnode = tsk_peer_node(tsk);
	skb_queue_head_init(&pktchain);

next:
	save = m->msg_iter;
	mtu = tsk->max_pkt;
	send = min_t(uint, dsz - sent, TIPC_MAX_USER_MSG_SIZE);
	rc = tipc_msg_build(mhdr, m, sent, send, mtu, &pktchain);
	if (unlikely(rc < 0))
		return rc;

	do {
		if (likely(!tsk_conn_cong(tsk))) {
			rc = tipc_node_xmit(net, &pktchain, dnode, portid);
			if (likely(!rc)) {
				tsk->sent_unacked++;
				sent += send;
				if (sent == dsz)
					return dsz;
				goto next;
			}
			if (rc == -EMSGSIZE) {
				__skb_queue_purge(&pktchain);
				tsk->max_pkt = tipc_node_get_mtu(net, dnode,
								 portid);
				m->msg_iter = save;
				goto next;
			}
			if (rc != -ELINKCONG)
				break;

			tsk->link_cong = 1;
		}
		rc = tipc_wait_for_sndpkt(sock, &timeo);
	} while (!rc);

	__skb_queue_purge(&pktchain);
	return sent ? sent : rc;
}

/**
 * tipc_send_packet - send a connection-oriented message
 * @sock: socket structure
 * @m: message to send
 * @dsz: length of data to be transmitted
 *
 * Used for SOCK_SEQPACKET messages.
 *
 * Returns the number of bytes sent on success, or errno otherwise
 */
static int tipc_send_packet(struct socket *sock, struct msghdr *m, size_t dsz)
{
	if (dsz > TIPC_MAX_USER_MSG_SIZE)
		return -EMSGSIZE;

	return tipc_send_stream(sock, m, dsz);
}

/* tipc_sk_finish_conn - complete the setup of a connection
 */
static void tipc_sk_finish_conn(struct tipc_sock *tsk, u32 peer_port,
				u32 peer_node)
{
	struct sock *sk = &tsk->sk;
	struct net *net = sock_net(sk);
	struct tipc_msg *msg = &tsk->phdr;

	msg_set_destnode(msg, peer_node);
	msg_set_destport(msg, peer_port);
	msg_set_type(msg, TIPC_CONN_MSG);
	msg_set_lookup_scope(msg, 0);
	msg_set_hdr_sz(msg, SHORT_H_SIZE);

	tsk->probing_intv = CONN_PROBING_INTERVAL;
	tsk->probing_state = TIPC_CONN_OK;
	tsk->connected = 1;
	sk_reset_timer(sk, &sk->sk_timer, jiffies + tsk->probing_intv);
	tipc_node_add_conn(net, peer_node, tsk->portid, peer_port);
	tsk->max_pkt = tipc_node_get_mtu(net, peer_node, tsk->portid);
}

/**
 * set_orig_addr - capture sender's address for received message
 * @m: descriptor for message info
 * @msg: received message header
 *
 * Note: Address is not captured if not requested by receiver.
 */

static void set_orig_addr(struct msghdr *m, struct tipc_msg *msg)
{
	struct sockaddr_tipc *addr = (struct sockaddr_tipc *)m->msg_name;
static void set_orig_addr(struct msghdr *m, struct tipc_msg *msg)
{
	DECLARE_SOCKADDR(struct sockaddr_tipc *, addr, m->msg_name);

	if (addr) {
		addr->family = AF_TIPC;
		addr->addrtype = TIPC_ADDR_ID;
		addr->addr.id.ref = msg_origport(msg);
		addr->addr.id.node = msg_orignode(msg);
		addr->addr.name.domain = 0;   	/* could leave uninitialized */
		addr->scope = 0;   		/* could leave uninitialized */
		memset(&addr->addr, 0, sizeof(addr->addr));
		addr->addr.id.ref = msg_origport(msg);
		addr->addr.id.node = msg_orignode(msg);
		addr->addr.name.domain = 0;	/* could leave uninitialized */
		addr->scope = 0;		/* could leave uninitialized */
		m->msg_namelen = sizeof(struct sockaddr_tipc);
	}
}

/**
 * anc_data_recv - optionally capture ancillary data for received message
 * @m: descriptor for message info
 * @msg: received message header
 * @tport: TIPC port associated with message
 * tipc_sk_anc_data_recv - optionally capture ancillary data for received message
 * @m: descriptor for message info
 * @msg: received message header
 * @tsk: TIPC port associated with message
 *
 * Note: Ancillary data is not captured if not requested by receiver.
 *
 * Returns 0 if successful, otherwise errno
 */

static int anc_data_recv(struct msghdr *m, struct tipc_msg *msg,
				struct tipc_port *tport)
static int tipc_sk_anc_data_recv(struct msghdr *m, struct tipc_msg *msg,
				 struct tipc_sock *tsk)
{
	u32 anc_data[3];
	u32 err;
	u32 dest_type;
	int has_name;
	int res;

	if (likely(m->msg_controllen == 0))
		return 0;

	/* Optionally capture errored message object(s) */

	err = msg ? msg_errcode(msg) : 0;
	if (unlikely(err)) {
		anc_data[0] = err;
		anc_data[1] = msg_data_sz(msg);
		if ((res = put_cmsg(m, SOL_TIPC, TIPC_ERRINFO, 8, anc_data)))
			return res;
		if (anc_data[1] &&
		    (res = put_cmsg(m, SOL_TIPC, TIPC_RETDATA, anc_data[1],
				    msg_data(msg))))
			return res;
	}

	/* Optionally capture message destination object */

		res = put_cmsg(m, SOL_TIPC, TIPC_ERRINFO, 8, anc_data);
		if (res)
			return res;
		if (anc_data[1]) {
			res = put_cmsg(m, SOL_TIPC, TIPC_RETDATA, anc_data[1],
				       msg_data(msg));
			if (res)
				return res;
		}
	}

	/* Optionally capture message destination object */
	dest_type = msg ? msg_type(msg) : TIPC_DIRECT_MSG;
	switch (dest_type) {
	case TIPC_NAMED_MSG:
		has_name = 1;
		anc_data[0] = msg_nametype(msg);
		anc_data[1] = msg_namelower(msg);
		anc_data[2] = msg_namelower(msg);
		break;
	case TIPC_MCAST_MSG:
		has_name = 1;
		anc_data[0] = msg_nametype(msg);
		anc_data[1] = msg_namelower(msg);
		anc_data[2] = msg_nameupper(msg);
		break;
	case TIPC_CONN_MSG:
		has_name = (tport->conn_type != 0);
		anc_data[0] = tport->conn_type;
		anc_data[1] = tport->conn_instance;
		anc_data[2] = tport->conn_instance;
		has_name = (tsk->conn_type != 0);
		anc_data[0] = tsk->conn_type;
		anc_data[1] = tsk->conn_instance;
		anc_data[2] = tsk->conn_instance;
		break;
	default:
		has_name = 0;
	}
	if (has_name &&
	    (res = put_cmsg(m, SOL_TIPC, TIPC_DESTNAME, 12, anc_data)))
		return res;
	if (has_name) {
		res = put_cmsg(m, SOL_TIPC, TIPC_DESTNAME, 12, anc_data);
		if (res)
			return res;
	}

	return 0;
}

/**
 * recv_msg - receive packet-oriented message
 * @iocb: (unused)
static void tipc_sk_send_ack(struct tipc_sock *tsk, uint ack)
{
	struct net *net = sock_net(&tsk->sk);
	struct sk_buff *skb = NULL;
	struct tipc_msg *msg;
	u32 peer_port = tsk_peer_port(tsk);
	u32 dnode = tsk_peer_node(tsk);

	if (!tsk->connected)
		return;
	skb = tipc_msg_create(CONN_MANAGER, CONN_ACK, INT_H_SIZE, 0,
			      dnode, tsk_own_node(tsk), peer_port,
			      tsk->portid, TIPC_OK);
	if (!skb)
		return;
	msg = buf_msg(skb);
	msg_set_msgcnt(msg, ack);
	tipc_node_xmit_skb(net, skb, dnode, msg_link_selector(msg));
}

static int tipc_wait_for_rcvmsg(struct socket *sock, long *timeop)
{
	struct sock *sk = sock->sk;
	DEFINE_WAIT(wait);
	long timeo = *timeop;
	int err;

	for (;;) {
		prepare_to_wait(sk_sleep(sk), &wait, TASK_INTERRUPTIBLE);
		if (timeo && skb_queue_empty(&sk->sk_receive_queue)) {
			if (sock->state == SS_DISCONNECTING) {
				err = -ENOTCONN;
				break;
			}
			release_sock(sk);
			timeo = schedule_timeout(timeo);
			lock_sock(sk);
		}
		err = 0;
		if (!skb_queue_empty(&sk->sk_receive_queue))
			break;
		err = -EAGAIN;
		if (!timeo)
			break;
		err = sock_intr_errno(timeo);
		if (signal_pending(current))
			break;
	}
	finish_wait(sk_sleep(sk), &wait);
	*timeop = timeo;
	return err;
}

/**
 * tipc_recvmsg - receive packet-oriented message
 * @m: descriptor for message info
 * @buf_len: total size of user buffer area
 * @flags: receive flags
 *
 * Used for SOCK_DGRAM, SOCK_RDM, and SOCK_SEQPACKET messages.
 * If the complete message doesn't fit in user area, truncate it.
 *
 * Returns size of returned message data, errno otherwise
 */

static int recv_msg(struct kiocb *iocb, struct socket *sock,
		    struct msghdr *m, size_t buf_len, int flags)
{
	struct sock *sk = sock->sk;
	struct tipc_port *tport = tipc_sk_port(sk);
	struct sk_buff *buf;
	struct tipc_msg *msg;
static int tipc_recvmsg(struct socket *sock, struct msghdr *m, size_t buf_len,
			int flags)
{
	struct sock *sk = sock->sk;
	struct tipc_sock *tsk = tipc_sk(sk);
	struct sk_buff *buf;
	struct tipc_msg *msg;
	long timeo;
	unsigned int sz;
	u32 err;
	int res;

	/* Catch invalid receive requests */

	if (m->msg_iovlen != 1)
		return -EOPNOTSUPP;   /* Don't do multiple iovec entries yet */

	if (unlikely(!buf_len))
		return -EINVAL;

	lock_sock(sk);

	if (unlikely(sock->state == SS_UNCONNECTED)) {
		res = -ENOTCONN;
		goto exit;
	}

restart:

	/* Look for a message in receive queue; wait if necessary */

	while (skb_queue_empty(&sk->sk_receive_queue)) {
		if (sock->state == SS_DISCONNECTING) {
			res = -ENOTCONN;
			goto exit;
		}
		if (flags & MSG_DONTWAIT) {
			res = -EWOULDBLOCK;
			goto exit;
		}
		release_sock(sk);
		res = wait_event_interruptible(*sk->sk_sleep,
			(!skb_queue_empty(&sk->sk_receive_queue) ||
			 (sock->state == SS_DISCONNECTING)));
		lock_sock(sk);
		if (res)
			goto exit;
	}

	/* Look at first message in receive queue */

	timeo = sock_rcvtimeo(sk, flags & MSG_DONTWAIT);
restart:

	/* Look for a message in receive queue; wait if necessary */
	res = tipc_wait_for_rcvmsg(sock, &timeo);
	if (res)
		goto exit;

	/* Look at first message in receive queue */
	buf = skb_peek(&sk->sk_receive_queue);
	msg = buf_msg(buf);
	sz = msg_data_sz(msg);
	err = msg_errcode(msg);

	/* Complete connection setup for an implied connect */

	if (unlikely(sock->state == SS_CONNECTING)) {
		res = auto_connect(sock, msg);
		if (res)
			goto exit;
	}

	/* Discard an empty non-errored message & try again */

	if ((!sz) && (!err)) {
		advance_rx_queue(sk);
	/* Discard an empty non-errored message & try again */
	if ((!sz) && (!err)) {
		tsk_advance_rx_queue(sk);
		goto restart;
	}

	/* Capture sender's address (optional) */

	set_orig_addr(m, msg);

	/* Capture ancillary data (optional) */

	res = anc_data_recv(m, msg, tport);
	set_orig_addr(m, msg);

	/* Capture ancillary data (optional) */
	res = tipc_sk_anc_data_recv(m, msg, tsk);
	if (res)
		goto exit;

	/* Capture message data (if valid) & compute return value (always) */

	if (!err) {
		if (unlikely(buf_len < sz)) {
			sz = buf_len;
			m->msg_flags |= MSG_TRUNC;
		}
		if (unlikely(copy_to_user(m->msg_iov->iov_base, msg_data(msg),
					  sz))) {
			res = -EFAULT;
			goto exit;
		}
		res = skb_copy_datagram_msg(buf, msg_hdr_sz(msg), m, sz);
		if (res)
			goto exit;
		res = sz;
	} else {
		if ((sock->state == SS_READY) ||
		    ((err == TIPC_CONN_SHUTDOWN) || m->msg_control))
			res = 0;
		else
			res = -ECONNRESET;
	}

	/* Consume received message (optional) */

	if (likely(!(flags & MSG_PEEK))) {
		if ((sock->state != SS_READY) &&
		    (++tport->conn_unacked >= TIPC_FLOW_CONTROL_WIN))
			tipc_acknowledge(tport->ref, tport->conn_unacked);
		advance_rx_queue(sk);
	if (likely(!(flags & MSG_PEEK))) {
		if ((sock->state != SS_READY) &&
		    (++tsk->rcv_unacked >= TIPC_CONNACK_INTV)) {
			tipc_sk_send_ack(tsk, tsk->rcv_unacked);
			tsk->rcv_unacked = 0;
		}
		tsk_advance_rx_queue(sk);
	}
exit:
	release_sock(sk);
	return res;
}

/**
 * recv_stream - receive stream-oriented data
 * @iocb: (unused)
 * tipc_recv_stream - receive stream-oriented data
 * @m: descriptor for message info
 * @buf_len: total size of user buffer area
 * @flags: receive flags
 *
 * Used for SOCK_STREAM messages only.  If not enough data is available
 * will optionally wait for more; never truncates data.
 *
 * Returns size of returned message data, errno otherwise
 */

static int recv_stream(struct kiocb *iocb, struct socket *sock,
		       struct msghdr *m, size_t buf_len, int flags)
{
	struct sock *sk = sock->sk;
	struct tipc_port *tport = tipc_sk_port(sk);
	struct sk_buff *buf;
	struct tipc_msg *msg;
	unsigned int sz;
	int sz_to_copy;
	int sz_copied = 0;
	int needed;
	char __user *crs = m->msg_iov->iov_base;
	unsigned char *buf_crs;
static int tipc_recv_stream(struct socket *sock, struct msghdr *m,
			    size_t buf_len, int flags)
{
	struct sock *sk = sock->sk;
	struct tipc_sock *tsk = tipc_sk(sk);
	struct sk_buff *buf;
	struct tipc_msg *msg;
	long timeo;
	unsigned int sz;
	int sz_to_copy, target, needed;
	int sz_copied = 0;
	u32 err;
	int res = 0;

	/* Catch invalid receive attempts */

	if (m->msg_iovlen != 1)
		return -EOPNOTSUPP;   /* Don't do multiple iovec entries yet */

	if (unlikely(!buf_len))
		return -EINVAL;

	lock_sock(sk);

	if (unlikely((sock->state == SS_UNCONNECTED) ||
		     (sock->state == SS_CONNECTING))) {
	if (unlikely(sock->state == SS_UNCONNECTED)) {
		res = -ENOTCONN;
		goto exit;
	}

restart:

	/* Look for a message in receive queue; wait if necessary */

	while (skb_queue_empty(&sk->sk_receive_queue)) {
		if (sock->state == SS_DISCONNECTING) {
			res = -ENOTCONN;
			goto exit;
		}
		if (flags & MSG_DONTWAIT) {
			res = -EWOULDBLOCK;
			goto exit;
		}
		release_sock(sk);
		res = wait_event_interruptible(*sk->sk_sleep,
			(!skb_queue_empty(&sk->sk_receive_queue) ||
			 (sock->state == SS_DISCONNECTING)));
		lock_sock(sk);
		if (res)
			goto exit;
	}

	/* Look at first message in receive queue */

	target = sock_rcvlowat(sk, flags & MSG_WAITALL, buf_len);
	timeo = sock_rcvtimeo(sk, flags & MSG_DONTWAIT);

restart:
	/* Look for a message in receive queue; wait if necessary */
	res = tipc_wait_for_rcvmsg(sock, &timeo);
	if (res)
		goto exit;

	/* Look at first message in receive queue */
	buf = skb_peek(&sk->sk_receive_queue);
	msg = buf_msg(buf);
	sz = msg_data_sz(msg);
	err = msg_errcode(msg);

	/* Discard an empty non-errored message & try again */

	if ((!sz) && (!err)) {
		advance_rx_queue(sk);
	if ((!sz) && (!err)) {
		tsk_advance_rx_queue(sk);
		goto restart;
	}

	/* Optionally capture sender's address & ancillary data of first msg */

	if (sz_copied == 0) {
		set_orig_addr(m, msg);
		res = anc_data_recv(m, msg, tport);
	if (sz_copied == 0) {
		set_orig_addr(m, msg);
		res = tipc_sk_anc_data_recv(m, msg, tsk);
		if (res)
			goto exit;
	}

	/* Capture message data (if valid) & compute return value (always) */

	if (!err) {
		buf_crs = (unsigned char *)(TIPC_SKB_CB(buf)->handle);
		sz = (unsigned char *)msg + msg_size(msg) - buf_crs;

		needed = (buf_len - sz_copied);
		sz_to_copy = (sz <= needed) ? sz : needed;
		if (unlikely(copy_to_user(crs, buf_crs, sz_to_copy))) {
			res = -EFAULT;
			goto exit;
		}
	if (!err) {
		u32 offset = (u32)(unsigned long)(TIPC_SKB_CB(buf)->handle);

		sz -= offset;
		needed = (buf_len - sz_copied);
		sz_to_copy = (sz <= needed) ? sz : needed;

		res = skb_copy_datagram_msg(buf, msg_hdr_sz(msg) + offset,
					    m, sz_to_copy);
		if (res)
			goto exit;

		sz_copied += sz_to_copy;

		if (sz_to_copy < sz) {
			if (!(flags & MSG_PEEK))
				TIPC_SKB_CB(buf)->handle = buf_crs + sz_to_copy;
			goto exit;
		}

		crs += sz_to_copy;
				TIPC_SKB_CB(buf)->handle =
				(void *)(unsigned long)(offset + sz_to_copy);
			goto exit;
		}
	} else {
		if (sz_copied != 0)
			goto exit; /* can't add error msg to valid data */

		if ((err == TIPC_CONN_SHUTDOWN) || m->msg_control)
			res = 0;
		else
			res = -ECONNRESET;
	}

	/* Consume received message (optional) */

	if (likely(!(flags & MSG_PEEK))) {
		if (unlikely(++tport->conn_unacked >= TIPC_FLOW_CONTROL_WIN))
			tipc_acknowledge(tport->ref, tport->conn_unacked);
		advance_rx_queue(sk);
	}

	/* Loop around if more data is required */

	if ((sz_copied < buf_len)    /* didn't get all requested data */
	    && (!skb_queue_empty(&sk->sk_receive_queue) ||
		(flags & MSG_WAITALL))
				     /* ... and more is ready or required */
	    && (!(flags & MSG_PEEK)) /* ... and aren't just peeking at data */
	    && (!err)                /* ... and haven't reached a FIN */
	    )
	if (likely(!(flags & MSG_PEEK))) {
		if (unlikely(++tsk->rcv_unacked >= TIPC_CONNACK_INTV)) {
			tipc_sk_send_ack(tsk, tsk->rcv_unacked);
			tsk->rcv_unacked = 0;
		}
		tsk_advance_rx_queue(sk);
	}

	/* Loop around if more data is required */
	if ((sz_copied < buf_len) &&	/* didn't get all requested data */
	    (!skb_queue_empty(&sk->sk_receive_queue) ||
	    (sz_copied < target)) &&	/* and more is ready or required */
	    (!(flags & MSG_PEEK)) &&	/* and aren't just peeking at data */
	    (!err))			/* and haven't reached a FIN */
		goto restart;

exit:
	release_sock(sk);
	return sz_copied ? sz_copied : res;
}

/**
 * rx_queue_full - determine if receive queue can accept another message
 * @msg: message to be added to queue
 * @queue_size: current size of queue
 * @base: nominal maximum size of queue
 *
 * Returns 1 if queue is unable to accept message, 0 otherwise
 */

static int rx_queue_full(struct tipc_msg *msg, u32 queue_size, u32 base)
{
	u32 threshold;
	u32 imp = msg_importance(msg);

	if (imp == TIPC_LOW_IMPORTANCE)
		threshold = base;
	else if (imp == TIPC_MEDIUM_IMPORTANCE)
		threshold = base * 2;
	else if (imp == TIPC_HIGH_IMPORTANCE)
		threshold = base * 100;
	else
		return 0;

	if (msg_connected(msg))
		threshold *= 4;

	return (queue_size >= threshold);
 * tipc_write_space - wake up thread if port congestion is released
 * @sk: socket
 */
static void tipc_write_space(struct sock *sk)
{
	struct socket_wq *wq;

	rcu_read_lock();
	wq = rcu_dereference(sk->sk_wq);
	if (wq_has_sleeper(wq))
		wake_up_interruptible_sync_poll(&wq->wait, POLLOUT |
						POLLWRNORM | POLLWRBAND);
	rcu_read_unlock();
}

/**
 * tipc_data_ready - wake up threads to indicate messages have been received
 * @sk: socket
 * @len: the length of messages
 */
static void tipc_data_ready(struct sock *sk)
{
	struct socket_wq *wq;

	rcu_read_lock();
	wq = rcu_dereference(sk->sk_wq);
	if (wq_has_sleeper(wq))
		wake_up_interruptible_sync_poll(&wq->wait, POLLIN |
						POLLRDNORM | POLLRDBAND);
	rcu_read_unlock();
}

static void tipc_sock_destruct(struct sock *sk)
{
	__skb_queue_purge(&sk->sk_receive_queue);
}

/**
 * filter_connect - Handle all incoming messages for a connection-based socket
 * @tsk: TIPC socket
 * @skb: pointer to message buffer. Set to NULL if buffer is consumed
 *
 * Returns true if everything ok, false otherwise
 */
static bool filter_connect(struct tipc_sock *tsk, struct sk_buff *skb)
{
	struct sock *sk = &tsk->sk;
	struct net *net = sock_net(sk);
	struct socket *sock = sk->sk_socket;
	struct tipc_msg *hdr = buf_msg(skb);

	if (unlikely(msg_mcast(hdr)))
		return false;

	switch ((int)sock->state) {
	case SS_CONNECTED:

		/* Accept only connection-based messages sent by peer */
		if (unlikely(!tsk_peer_msg(tsk, hdr)))
			return false;

		if (unlikely(msg_errcode(hdr))) {
			sock->state = SS_DISCONNECTING;
			tsk->connected = 0;
			/* Let timer expire on it's own */
			tipc_node_remove_conn(net, tsk_peer_node(tsk),
					      tsk->portid);
		}
		return true;

	case SS_CONNECTING:

		/* Accept only ACK or NACK message */
		if (unlikely(!msg_connected(hdr)))
			return false;

		if (unlikely(msg_errcode(hdr))) {
			sock->state = SS_DISCONNECTING;
			sk->sk_err = ECONNREFUSED;
			return true;
		}

		if (unlikely(!msg_isdata(hdr))) {
			sock->state = SS_DISCONNECTING;
			sk->sk_err = EINVAL;
			return true;
		}

		tipc_sk_finish_conn(tsk, msg_origport(hdr), msg_orignode(hdr));
		msg_set_importance(&tsk->phdr, msg_importance(hdr));
		sock->state = SS_CONNECTED;

		/* If 'ACK+' message, add to socket receive queue */
		if (msg_data_sz(hdr))
			return true;

		/* If empty 'ACK-' message, wake up sleeping connect() */
		if (waitqueue_active(sk_sleep(sk)))
			wake_up_interruptible(sk_sleep(sk));

		/* 'ACK-' message is neither accepted nor rejected: */
		msg_set_dest_droppable(hdr, 1);
		return false;

	case SS_LISTENING:
	case SS_UNCONNECTED:

		/* Accept only SYN message */
		if (!msg_connected(hdr) && !(msg_errcode(hdr)))
			return true;
		break;
	case SS_DISCONNECTING:
		break;
	default:
		pr_err("Unknown socket state %u\n", sock->state);
	}
	return false;
}

/**
 * rcvbuf_limit - get proper overload limit of socket receive queue
 * @sk: socket
 * @buf: message
 *
 * For all connection oriented messages, irrespective of importance,
 * the default overload value (i.e. 67MB) is set as limit.
 *
 * For all connectionless messages, by default new queue limits are
 * as belows:
 *
 * TIPC_LOW_IMPORTANCE       (4 MB)
 * TIPC_MEDIUM_IMPORTANCE    (8 MB)
 * TIPC_HIGH_IMPORTANCE      (16 MB)
 * TIPC_CRITICAL_IMPORTANCE  (32 MB)
 *
 * Returns overload limit according to corresponding message importance
 */
static unsigned int rcvbuf_limit(struct sock *sk, struct sk_buff *buf)
{
	struct tipc_msg *msg = buf_msg(buf);

	if (msg_connected(msg))
		return sysctl_tipc_rmem[2];

	return sk->sk_rcvbuf >> TIPC_CRITICAL_IMPORTANCE <<
		msg_importance(msg);
}

/**
 * filter_rcv - validate incoming message
 * @sk: socket
 * @buf: message
 * @skb: pointer to message.
 *
 * Enqueues message on receive queue if acceptable; optionally handles
 * disconnect indication for a connected socket.
 *
 * Called with socket lock already taken; port lock may also be taken.
 *
 * Returns TIPC error status code (TIPC_OK if message is not to be rejected)
 */

static u32 filter_rcv(struct sock *sk, struct sk_buff *buf)
{
	struct socket *sock = sk->sk_socket;
	struct tipc_msg *msg = buf_msg(buf);
	u32 recv_q_len;

	/* Reject message if it is wrong sort of message for socket */

	/*
	 * WOULD IT BE BETTER TO JUST DISCARD THESE MESSAGES INSTEAD?
	 * "NO PORT" ISN'T REALLY THE RIGHT ERROR CODE, AND THERE MAY
	 * BE SECURITY IMPLICATIONS INHERENT IN REJECTING INVALID TRAFFIC
	 */

	if (sock->state == SS_READY) {
		if (msg_connected(msg)) {
			msg_dbg(msg, "dispatch filter 1\n");
			return TIPC_ERR_NO_PORT;
		}
	} else {
		if (msg_mcast(msg)) {
			msg_dbg(msg, "dispatch filter 2\n");
			return TIPC_ERR_NO_PORT;
		}
		if (sock->state == SS_CONNECTED) {
			if (!msg_connected(msg)) {
				msg_dbg(msg, "dispatch filter 3\n");
				return TIPC_ERR_NO_PORT;
			}
		}
		else if (sock->state == SS_CONNECTING) {
			if (!msg_connected(msg) && (msg_errcode(msg) == 0)) {
				msg_dbg(msg, "dispatch filter 4\n");
				return TIPC_ERR_NO_PORT;
			}
		}
		else if (sock->state == SS_LISTENING) {
			if (msg_connected(msg) || msg_errcode(msg)) {
				msg_dbg(msg, "dispatch filter 5\n");
				return TIPC_ERR_NO_PORT;
			}
		}
		else if (sock->state == SS_DISCONNECTING) {
			msg_dbg(msg, "dispatch filter 6\n");
			return TIPC_ERR_NO_PORT;
		}
		else /* (sock->state == SS_UNCONNECTED) */ {
			if (msg_connected(msg) || msg_errcode(msg)) {
				msg_dbg(msg, "dispatch filter 7\n");
				return TIPC_ERR_NO_PORT;
			}
		}
	}

	/* Reject message if there isn't room to queue it */

	recv_q_len = (u32)atomic_read(&tipc_queue_size);
	if (unlikely(recv_q_len >= OVERLOAD_LIMIT_BASE)) {
		if (rx_queue_full(msg, recv_q_len, OVERLOAD_LIMIT_BASE))
			return TIPC_ERR_OVERLOAD;
	}
	recv_q_len = skb_queue_len(&sk->sk_receive_queue);
	if (unlikely(recv_q_len >= (OVERLOAD_LIMIT_BASE / 2))) {
		if (rx_queue_full(msg, recv_q_len, OVERLOAD_LIMIT_BASE / 2))
			return TIPC_ERR_OVERLOAD;
	}

	/* Enqueue message (finally!) */

	msg_dbg(msg, "<DISP<: ");
	TIPC_SKB_CB(buf)->handle = msg_data(msg);
	atomic_inc(&tipc_queue_size);
	__skb_queue_tail(&sk->sk_receive_queue, buf);

	/* Initiate connection termination for an incoming 'FIN' */

	if (unlikely(msg_errcode(msg) && (sock->state == SS_CONNECTED))) {
		sock->state = SS_DISCONNECTING;
		tipc_disconnect_port(tipc_sk_port(sk));
	}

	if (waitqueue_active(sk->sk_sleep))
		wake_up_interruptible(sk->sk_sleep);
	return TIPC_OK;
}

/**
 * backlog_rcv - handle incoming message from backlog queue
 * @sk: socket
 * @buf: message
 *
 * Caller must hold socket lock, but not port lock.
 *
 * Returns 0
 */

static int backlog_rcv(struct sock *sk, struct sk_buff *buf)
{
	u32 res;

	res = filter_rcv(sk, buf);
	if (res)
		tipc_reject_msg(buf, res);
 * Called with socket lock already taken
 *
 * Returns true if message was added to socket receive queue, otherwise false
 */
static bool filter_rcv(struct sock *sk, struct sk_buff *skb,
		       struct sk_buff_head *xmitq)
{
	struct socket *sock = sk->sk_socket;
	struct tipc_sock *tsk = tipc_sk(sk);
	struct tipc_msg *hdr = buf_msg(skb);
	unsigned int limit = rcvbuf_limit(sk, skb);
	int err = TIPC_OK;
	int usr = msg_user(hdr);

	if (unlikely(msg_user(hdr) == CONN_MANAGER)) {
		tipc_sk_proto_rcv(tsk, skb, xmitq);
		return false;
	}

	if (unlikely(usr == SOCK_WAKEUP)) {
		kfree_skb(skb);
		tsk->link_cong = 0;
		sk->sk_write_space(sk);
		return false;
	}

	/* Drop if illegal message type */
	if (unlikely(msg_type(hdr) > TIPC_DIRECT_MSG)) {
		kfree_skb(skb);
		return false;
	}

	/* Reject if wrong message type for current socket state */
	if (unlikely(sock->state == SS_READY)) {
		if (msg_connected(hdr)) {
			err = TIPC_ERR_NO_PORT;
			goto reject;
		}
	} else if (unlikely(!filter_connect(tsk, skb))) {
		err = TIPC_ERR_NO_PORT;
		goto reject;
	}

	/* Reject message if there isn't room to queue it */
	if (unlikely(sk_rmem_alloc_get(sk) + skb->truesize >= limit)) {
		err = TIPC_ERR_OVERLOAD;
		goto reject;
	}

	/* Enqueue message */
	TIPC_SKB_CB(skb)->handle = NULL;
	__skb_queue_tail(&sk->sk_receive_queue, skb);
	skb_set_owner_r(skb, sk);

	sk->sk_data_ready(sk);
	return true;

reject:
	if (tipc_msg_reverse(tsk_own_node(tsk), &skb, err))
		__skb_queue_tail(xmitq, skb);
	return false;
}

/**
 * tipc_backlog_rcv - handle incoming message from backlog queue
 * @sk: socket
 * @skb: message
 *
 * Caller must hold socket lock
 *
 * Returns 0
 */
static int tipc_backlog_rcv(struct sock *sk, struct sk_buff *skb)
{
	unsigned int truesize = skb->truesize;
	struct sk_buff_head xmitq;
	u32 dnode, selector;

	__skb_queue_head_init(&xmitq);

	if (likely(filter_rcv(sk, skb, &xmitq))) {
		atomic_add(truesize, &tipc_sk(sk)->dupl_rcvcnt);
		return 0;
	}

	if (skb_queue_empty(&xmitq))
		return 0;

	/* Send response/rejected message */
	skb = __skb_dequeue(&xmitq);
	dnode = msg_destnode(buf_msg(skb));
	selector = msg_origport(buf_msg(skb));
	tipc_node_xmit_skb(sock_net(sk), skb, dnode, selector);
	return 0;
}

/**
 * dispatch - handle incoming message
 * @tport: TIPC port that received message
 * @buf: message
 *
 * Called with port lock already taken.
 *
 * Returns TIPC error status code (TIPC_OK if message is not to be rejected)
 */

static u32 dispatch(struct tipc_port *tport, struct sk_buff *buf)
{
	struct sock *sk = (struct sock *)tport->usr_handle;
	u32 res;

	/*
	 * Process message if socket is unlocked; otherwise add to backlog queue
	 *
	 * This code is based on sk_receive_skb(), but must be distinct from it
	 * since a TIPC-specific filter/reject mechanism is utilized
	 */

	bh_lock_sock(sk);
	if (!sock_owned_by_user(sk)) {
		res = filter_rcv(sk, buf);
	} else {
		sk_add_backlog(sk, buf);
		res = TIPC_OK;
	}
	bh_unlock_sock(sk);

	return res;
}

/**
 * wakeupdispatch - wake up port after congestion
 * @tport: port to wakeup
 *
 * Called with port lock already taken.
 */

static void wakeupdispatch(struct tipc_port *tport)
{
	struct sock *sk = (struct sock *)tport->usr_handle;

	if (waitqueue_active(sk->sk_sleep))
		wake_up_interruptible(sk->sk_sleep);
}

/**
 * connect - establish a connection to another TIPC port
 * tipc_sk_enqueue - extract all buffers with destination 'dport' from
 *                   inputq and try adding them to socket or backlog queue
 * @inputq: list of incoming buffers with potentially different destinations
 * @sk: socket where the buffers should be enqueued
 * @dport: port number for the socket
 *
 * Caller must hold socket lock
 */
static void tipc_sk_enqueue(struct sk_buff_head *inputq, struct sock *sk,
			    u32 dport, struct sk_buff_head *xmitq)
{
	unsigned long time_limit = jiffies + usecs_to_jiffies(20000);
	struct sk_buff *skb;
	unsigned int lim;
	atomic_t *dcnt;
	u32 onode;

	while (skb_queue_len(inputq)) {
		if (unlikely(time_after_eq(jiffies, time_limit)))
			return;

		skb = tipc_skb_dequeue(inputq, dport);
		if (unlikely(!skb))
			return;

		/* Add message directly to receive queue if possible */
		if (!sock_owned_by_user(sk)) {
			filter_rcv(sk, skb, xmitq);
			continue;
		}

		/* Try backlog, compensating for double-counted bytes */
		dcnt = &tipc_sk(sk)->dupl_rcvcnt;
		if (!sk->sk_backlog.len)
			atomic_set(dcnt, 0);
		lim = rcvbuf_limit(sk, skb) + atomic_read(dcnt);
		if (likely(!sk_add_backlog(sk, skb, lim)))
			continue;

		/* Overload => reject message back to sender */
		onode = tipc_own_addr(sock_net(sk));
		if (tipc_msg_reverse(onode, &skb, TIPC_ERR_OVERLOAD))
			__skb_queue_tail(xmitq, skb);
		break;
	}
}

/**
 * tipc_sk_rcv - handle a chain of incoming buffers
 * @inputq: buffer list containing the buffers
 * Consumes all buffers in list until inputq is empty
 * Note: may be called in multiple threads referring to the same queue
 */
void tipc_sk_rcv(struct net *net, struct sk_buff_head *inputq)
{
	struct sk_buff_head xmitq;
	u32 dnode, dport = 0;
	int err;
	struct tipc_sock *tsk;
	struct sock *sk;
	struct sk_buff *skb;

	__skb_queue_head_init(&xmitq);
	while (skb_queue_len(inputq)) {
		dport = tipc_skb_peek_port(inputq, dport);
		tsk = tipc_sk_lookup(net, dport);

		if (likely(tsk)) {
			sk = &tsk->sk;
			if (likely(spin_trylock_bh(&sk->sk_lock.slock))) {
				tipc_sk_enqueue(inputq, sk, dport, &xmitq);
				spin_unlock_bh(&sk->sk_lock.slock);
			}
			/* Send pending response/rejected messages, if any */
			while ((skb = __skb_dequeue(&xmitq))) {
				dnode = msg_destnode(buf_msg(skb));
				tipc_node_xmit_skb(net, skb, dnode, dport);
			}
			sock_put(sk);
			continue;
		}

		/* No destination socket => dequeue skb if still there */
		skb = tipc_skb_dequeue(inputq, dport);
		if (!skb)
			return;

		/* Try secondary lookup if unresolved named message */
		err = TIPC_ERR_NO_PORT;
		if (tipc_msg_lookup_dest(net, skb, &err))
			goto xmit;

		/* Prepare for message rejection */
		if (!tipc_msg_reverse(tipc_own_addr(net), &skb, err))
			continue;
xmit:
		dnode = msg_destnode(buf_msg(skb));
		tipc_node_xmit_skb(net, skb, dnode, dport);
	}
}

static int tipc_wait_for_connect(struct socket *sock, long *timeo_p)
{
	struct sock *sk = sock->sk;
	DEFINE_WAIT(wait);
	int done;

	do {
		int err = sock_error(sk);
		if (err)
			return err;
		if (!*timeo_p)
			return -ETIMEDOUT;
		if (signal_pending(current))
			return sock_intr_errno(*timeo_p);

		prepare_to_wait(sk_sleep(sk), &wait, TASK_INTERRUPTIBLE);
		done = sk_wait_event(sk, timeo_p, sock->state != SS_CONNECTING);
		finish_wait(sk_sleep(sk), &wait);
	} while (!done);
	return 0;
}

/**
 * tipc_connect - establish a connection to another TIPC port
 * @sock: socket structure
 * @dest: socket address for destination port
 * @destlen: size of socket address data structure
 * @flags: file-related flags associated with socket
 *
 * Returns 0 on success, errno otherwise
 */

static int connect(struct socket *sock, struct sockaddr *dest, int destlen,
		   int flags)
{
	struct sock *sk = sock->sk;
	struct sockaddr_tipc *dst = (struct sockaddr_tipc *)dest;
	struct msghdr m = {NULL,};
	struct sk_buff *buf;
	struct tipc_msg *msg;
	int res;

	lock_sock(sk);

	/* For now, TIPC does not allow use of connect() with DGRAM/RDM types */

	if (sock->state == SS_READY) {
		res = -EOPNOTSUPP;
		goto exit;
	}

	/* For now, TIPC does not support the non-blocking form of connect() */

	if (flags & O_NONBLOCK) {
		res = -EWOULDBLOCK;
		goto exit;
	}

	/* Issue Posix-compliant error code if socket is in the wrong state */

	if (sock->state == SS_LISTENING) {
		res = -EOPNOTSUPP;
		goto exit;
	}
	if (sock->state == SS_CONNECTING) {
		res = -EALREADY;
		goto exit;
	}
	if (sock->state != SS_UNCONNECTED) {
		res = -EISCONN;
static int tipc_connect(struct socket *sock, struct sockaddr *dest,
			int destlen, int flags)
{
	struct sock *sk = sock->sk;
	struct tipc_sock *tsk = tipc_sk(sk);
	struct sockaddr_tipc *dst = (struct sockaddr_tipc *)dest;
	struct msghdr m = {NULL,};
	long timeout = (flags & O_NONBLOCK) ? 0 : tsk->conn_timeout;
	socket_state previous;
	int res = 0;

	lock_sock(sk);

	/* DGRAM/RDM connect(), just save the destaddr */
	if (sock->state == SS_READY) {
		if (dst->family == AF_UNSPEC) {
			memset(&tsk->remote, 0, sizeof(struct sockaddr_tipc));
			tsk->connected = 0;
		} else if (destlen != sizeof(struct sockaddr_tipc)) {
			res = -EINVAL;
		} else {
			memcpy(&tsk->remote, dest, destlen);
			tsk->connected = 1;
		}
		goto exit;
	}

	/*
	 * Reject connection attempt using multicast address
	 *
	 * Note: send_msg() validates the rest of the address fields,
	 *       so there's no need to do it here
	 */

	if (dst->addrtype == TIPC_ADDR_MCAST) {
		res = -EINVAL;
		goto exit;
	}

	/* Reject any messages already in receive queue (very unlikely) */

	reject_rx_queue(sk);

	/* Send a 'SYN-' to destination */

	m.msg_name = dest;
	m.msg_namelen = destlen;
	res = send_msg(NULL, sock, &m, 0);
	if (res < 0) {
		goto exit;
	}

	/* Wait until an 'ACK' or 'RST' arrives, or a timeout occurs */

	release_sock(sk);
	res = wait_event_interruptible_timeout(*sk->sk_sleep,
			(!skb_queue_empty(&sk->sk_receive_queue) ||
			(sock->state != SS_CONNECTING)),
			sk->sk_rcvtimeo);
	lock_sock(sk);

	if (res > 0) {
		buf = skb_peek(&sk->sk_receive_queue);
		if (buf != NULL) {
			msg = buf_msg(buf);
			res = auto_connect(sock, msg);
			if (!res) {
				if (!msg_data_sz(msg))
					advance_rx_queue(sk);
			}
		} else {
			if (sock->state == SS_CONNECTED) {
				res = -EISCONN;
			} else {
				res = -ECONNREFUSED;
			}
		}
	} else {
		if (res == 0)
			res = -ETIMEDOUT;
		else
			; /* leave "res" unchanged */
		sock->state = SS_DISCONNECTING;
	}

	previous = sock->state;
	switch (sock->state) {
	case SS_UNCONNECTED:
		/* Send a 'SYN-' to destination */
		m.msg_name = dest;
		m.msg_namelen = destlen;

		/* If connect is in non-blocking case, set MSG_DONTWAIT to
		 * indicate send_msg() is never blocked.
		 */
		if (!timeout)
			m.msg_flags = MSG_DONTWAIT;

		res = __tipc_sendmsg(sock, &m, 0);
		if ((res < 0) && (res != -EWOULDBLOCK))
			goto exit;

		/* Just entered SS_CONNECTING state; the only
		 * difference is that return value in non-blocking
		 * case is EINPROGRESS, rather than EALREADY.
		 */
		res = -EINPROGRESS;
	case SS_CONNECTING:
		if (previous == SS_CONNECTING)
			res = -EALREADY;
		if (!timeout)
			goto exit;
		timeout = msecs_to_jiffies(timeout);
		/* Wait until an 'ACK' or 'RST' arrives, or a timeout occurs */
		res = tipc_wait_for_connect(sock, &timeout);
		break;
	case SS_CONNECTED:
		res = -EISCONN;
		break;
	default:
		res = -EINVAL;
		break;
	}
exit:
	release_sock(sk);
	return res;
}

/**
 * listen - allow socket to listen for incoming connections
 * tipc_listen - allow socket to listen for incoming connections
 * @sock: socket structure
 * @len: (unused)
 *
 * Returns 0 on success, errno otherwise
 */

static int listen(struct socket *sock, int len)
static int tipc_listen(struct socket *sock, int len)
{
	struct sock *sk = sock->sk;
	int res;

	lock_sock(sk);

	if (sock->state == SS_READY)
		res = -EOPNOTSUPP;
	else if (sock->state != SS_UNCONNECTED)
	if (sock->state != SS_UNCONNECTED)
		res = -EINVAL;
	else {
		sock->state = SS_LISTENING;
		res = 0;
	}

	release_sock(sk);
	return res;
}

/**
 * accept - wait for connection request
static int tipc_wait_for_accept(struct socket *sock, long timeo)
{
	struct sock *sk = sock->sk;
	DEFINE_WAIT_FUNC(wait, woken_wake_function);
	int err;

	/* True wake-one mechanism for incoming connections: only
	 * one process gets woken up, not the 'whole herd'.
	 * Since we do not 'race & poll' for established sockets
	 * anymore, the common case will execute the loop only once.
	*/
	for (;;) {
		if (timeo && skb_queue_empty(&sk->sk_receive_queue)) {
			add_wait_queue(sk_sleep(sk), &wait);
			release_sock(sk);
			timeo = wait_woken(&wait, TASK_INTERRUPTIBLE, timeo);
			lock_sock(sk);
			remove_wait_queue(sk_sleep(sk), &wait);
		}
		err = 0;
		if (!skb_queue_empty(&sk->sk_receive_queue))
			break;
		err = -EINVAL;
		if (sock->state != SS_LISTENING)
			break;
		err = -EAGAIN;
		if (!timeo)
			break;
		err = sock_intr_errno(timeo);
		if (signal_pending(current))
			break;
	}
	return err;
}

/**
 * tipc_accept - wait for connection request
 * @sock: listening socket
 * @newsock: new socket that is to be connected
 * @flags: file-related flags associated with socket
 *
 * Returns 0 on success, errno otherwise
 */

static int accept(struct socket *sock, struct socket *new_sock, int flags)
{
	struct sock *sk = sock->sk;
	struct sk_buff *buf;
static int tipc_accept(struct socket *sock, struct socket *new_sock, int flags)
{
	struct sock *new_sk, *sk = sock->sk;
	struct sk_buff *buf;
	struct tipc_sock *new_tsock;
	struct tipc_msg *msg;
	long timeo;
	int res;

	lock_sock(sk);

	if (sock->state == SS_READY) {
		res = -EOPNOTSUPP;
		goto exit;
	}
	if (sock->state != SS_LISTENING) {
		res = -EINVAL;
		goto exit;
	}

	while (skb_queue_empty(&sk->sk_receive_queue)) {
		if (flags & O_NONBLOCK) {
			res = -EWOULDBLOCK;
			goto exit;
		}
		release_sock(sk);
		res = wait_event_interruptible(*sk->sk_sleep,
				(!skb_queue_empty(&sk->sk_receive_queue)));
		lock_sock(sk);
		if (res)
			goto exit;
	}

	buf = skb_peek(&sk->sk_receive_queue);

	res = tipc_create(sock_net(sock->sk), new_sock, 0);
	if (!res) {
		struct sock *new_sk = new_sock->sk;
		struct tipc_sock *new_tsock = tipc_sk(new_sk);
		struct tipc_port *new_tport = new_tsock->p;
		u32 new_ref = new_tport->ref;
		struct tipc_msg *msg = buf_msg(buf);

		lock_sock(new_sk);

		/*
		 * Reject any stray messages received by new socket
		 * before the socket lock was taken (very, very unlikely)
		 */

		reject_rx_queue(new_sk);

		/* Connect new socket to it's peer */

		new_tsock->peer_name.ref = msg_origport(msg);
		new_tsock->peer_name.node = msg_orignode(msg);
		tipc_connect2port(new_ref, &new_tsock->peer_name);
		new_sock->state = SS_CONNECTED;

		tipc_set_portimportance(new_ref, msg_importance(msg));
		if (msg_named(msg)) {
			new_tport->conn_type = msg_nametype(msg);
			new_tport->conn_instance = msg_nameinst(msg);
		}

		/*
		 * Respond to 'SYN-' by discarding it & returning 'ACK'-.
		 * Respond to 'SYN+' by queuing it on new socket.
		 */

		msg_dbg(msg,"<ACC<: ");
		if (!msg_data_sz(msg)) {
			struct msghdr m = {NULL,};

			advance_rx_queue(sk);
			send_packet(NULL, new_sock, &m, 0);
		} else {
			__skb_dequeue(&sk->sk_receive_queue);
			__skb_queue_head(&new_sk->sk_receive_queue, buf);
		}
		release_sock(new_sk);
	}
	timeo = sock_rcvtimeo(sk, flags & O_NONBLOCK);
	res = tipc_wait_for_accept(sock, timeo);
	if (res)
		goto exit;

	buf = skb_peek(&sk->sk_receive_queue);

	res = tipc_sk_create(sock_net(sock->sk), new_sock, 0, 1);
	if (res)
		goto exit;
	security_sk_clone(sock->sk, new_sock->sk);

	new_sk = new_sock->sk;
	new_tsock = tipc_sk(new_sk);
	msg = buf_msg(buf);

	/* we lock on new_sk; but lockdep sees the lock on sk */
	lock_sock_nested(new_sk, SINGLE_DEPTH_NESTING);

	/*
	 * Reject any stray messages received by new socket
	 * before the socket lock was taken (very, very unlikely)
	 */
	tsk_rej_rx_queue(new_sk);

	/* Connect new socket to it's peer */
	tipc_sk_finish_conn(new_tsock, msg_origport(msg), msg_orignode(msg));
	new_sock->state = SS_CONNECTED;

	tsk_set_importance(new_tsock, msg_importance(msg));
	if (msg_named(msg)) {
		new_tsock->conn_type = msg_nametype(msg);
		new_tsock->conn_instance = msg_nameinst(msg);
	}

	/*
	 * Respond to 'SYN-' by discarding it & returning 'ACK'-.
	 * Respond to 'SYN+' by queuing it on new socket.
	 */
	if (!msg_data_sz(msg)) {
		struct msghdr m = {NULL,};

		tsk_advance_rx_queue(sk);
		__tipc_send_stream(new_sock, &m, 0);
	} else {
		__skb_dequeue(&sk->sk_receive_queue);
		__skb_queue_head(&new_sk->sk_receive_queue, buf);
		skb_set_owner_r(buf, new_sk);
	}
	release_sock(new_sk);
exit:
	release_sock(sk);
	return res;
}

/**
 * shutdown - shutdown socket connection
 * tipc_shutdown - shutdown socket connection
 * @sock: socket structure
 * @how: direction to close (must be SHUT_RDWR)
 *
 * Terminates connection (if necessary), then purges socket's receive queue.
 *
 * Returns 0 on success, errno otherwise
 */

static int shutdown(struct socket *sock, int how)
{
	struct sock *sk = sock->sk;
	struct tipc_port *tport = tipc_sk_port(sk);
	struct sk_buff *buf;
static int tipc_shutdown(struct socket *sock, int how)
{
	struct sock *sk = sock->sk;
	struct net *net = sock_net(sk);
	struct tipc_sock *tsk = tipc_sk(sk);
	struct sk_buff *skb;
	u32 dnode = tsk_peer_node(tsk);
	u32 dport = tsk_peer_port(tsk);
	u32 onode = tipc_own_addr(net);
	u32 oport = tsk->portid;
	int res;

	if (how != SHUT_RDWR)
		return -EINVAL;

	lock_sock(sk);

	switch (sock->state) {
	case SS_CONNECTING:
	case SS_CONNECTED:

		/* Disconnect and send a 'FIN+' or 'FIN-' message to peer */
restart:
		buf = __skb_dequeue(&sk->sk_receive_queue);
		if (buf) {
			atomic_dec(&tipc_queue_size);
			if (TIPC_SKB_CB(buf)->handle != msg_data(buf_msg(buf))) {
				buf_discard(buf);
				goto restart;
			}
			tipc_disconnect(tport->ref);
			tipc_reject_msg(buf, TIPC_CONN_SHUTDOWN);
		} else {
			tipc_shutdown(tport->ref);
		}

		sock->state = SS_DISCONNECTING;

restart:
		dnode = tsk_peer_node(tsk);

		/* Disconnect and send a 'FIN+' or 'FIN-' message to peer */
		skb = __skb_dequeue(&sk->sk_receive_queue);
		if (skb) {
			if (TIPC_SKB_CB(skb)->handle != NULL) {
				kfree_skb(skb);
				goto restart;
			}
			tipc_sk_respond(sk, skb, TIPC_CONN_SHUTDOWN);
		} else {
			skb = tipc_msg_create(TIPC_CRITICAL_IMPORTANCE,
					      TIPC_CONN_MSG, SHORT_H_SIZE,
					      0, dnode, onode, dport, oport,
					      TIPC_CONN_SHUTDOWN);
			if (skb)
				tipc_node_xmit_skb(net, skb, dnode, tsk->portid);
		}
		tsk->connected = 0;
		sock->state = SS_DISCONNECTING;
		tipc_node_remove_conn(net, dnode, tsk->portid);
		/* fall through */

	case SS_DISCONNECTING:

		/* Discard any unreceived messages; wake up sleeping tasks */

		discard_rx_queue(sk);
		if (waitqueue_active(sk->sk_sleep))
			wake_up_interruptible(sk->sk_sleep);
		/* Discard any unreceived messages */
		__skb_queue_purge(&sk->sk_receive_queue);

		/* Wake up anyone sleeping in poll */
		sk->sk_state_change(sk);
		res = 0;
		break;

	default:
		res = -ENOTCONN;
	}

	release_sock(sk);
	return res;
}

/**
 * setsockopt - set socket option
static void tipc_sk_timeout(unsigned long data)
{
	struct tipc_sock *tsk = (struct tipc_sock *)data;
	struct sock *sk = &tsk->sk;
	struct sk_buff *skb = NULL;
	u32 peer_port, peer_node;
	u32 own_node = tsk_own_node(tsk);

	bh_lock_sock(sk);
	if (!tsk->connected) {
		bh_unlock_sock(sk);
		goto exit;
	}
	peer_port = tsk_peer_port(tsk);
	peer_node = tsk_peer_node(tsk);

	if (tsk->probing_state == TIPC_CONN_PROBING) {
		if (!sock_owned_by_user(sk)) {
			sk->sk_socket->state = SS_DISCONNECTING;
			tsk->connected = 0;
			tipc_node_remove_conn(sock_net(sk), tsk_peer_node(tsk),
					      tsk_peer_port(tsk));
			sk->sk_state_change(sk);
		} else {
			/* Try again later */
			sk_reset_timer(sk, &sk->sk_timer, (HZ / 20));
		}

	} else {
		skb = tipc_msg_create(CONN_MANAGER, CONN_PROBE,
				      INT_H_SIZE, 0, peer_node, own_node,
				      peer_port, tsk->portid, TIPC_OK);
		tsk->probing_state = TIPC_CONN_PROBING;
		sk_reset_timer(sk, &sk->sk_timer, jiffies + tsk->probing_intv);
	}
	bh_unlock_sock(sk);
	if (skb)
		tipc_node_xmit_skb(sock_net(sk), skb, peer_node, tsk->portid);
exit:
	sock_put(sk);
}

static int tipc_sk_publish(struct tipc_sock *tsk, uint scope,
			   struct tipc_name_seq const *seq)
{
	struct net *net = sock_net(&tsk->sk);
	struct publication *publ;
	u32 key;

	if (tsk->connected)
		return -EINVAL;
	key = tsk->portid + tsk->pub_count + 1;
	if (key == tsk->portid)
		return -EADDRINUSE;

	publ = tipc_nametbl_publish(net, seq->type, seq->lower, seq->upper,
				    scope, tsk->portid, key);
	if (unlikely(!publ))
		return -EINVAL;

	list_add(&publ->pport_list, &tsk->publications);
	tsk->pub_count++;
	tsk->published = 1;
	return 0;
}

static int tipc_sk_withdraw(struct tipc_sock *tsk, uint scope,
			    struct tipc_name_seq const *seq)
{
	struct net *net = sock_net(&tsk->sk);
	struct publication *publ;
	struct publication *safe;
	int rc = -EINVAL;

	list_for_each_entry_safe(publ, safe, &tsk->publications, pport_list) {
		if (seq) {
			if (publ->scope != scope)
				continue;
			if (publ->type != seq->type)
				continue;
			if (publ->lower != seq->lower)
				continue;
			if (publ->upper != seq->upper)
				break;
			tipc_nametbl_withdraw(net, publ->type, publ->lower,
					      publ->ref, publ->key);
			rc = 0;
			break;
		}
		tipc_nametbl_withdraw(net, publ->type, publ->lower,
				      publ->ref, publ->key);
		rc = 0;
	}
	if (list_empty(&tsk->publications))
		tsk->published = 0;
	return rc;
}

/* tipc_sk_reinit: set non-zero address in all existing sockets
 *                 when we go from standalone to network mode.
 */
void tipc_sk_reinit(struct net *net)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	const struct bucket_table *tbl;
	struct rhash_head *pos;
	struct tipc_sock *tsk;
	struct tipc_msg *msg;
	int i;

	rcu_read_lock();
	tbl = rht_dereference_rcu((&tn->sk_rht)->tbl, &tn->sk_rht);
	for (i = 0; i < tbl->size; i++) {
		rht_for_each_entry_rcu(tsk, pos, tbl, i, node) {
			spin_lock_bh(&tsk->sk.sk_lock.slock);
			msg = &tsk->phdr;
			msg_set_prevnode(msg, tn->own_addr);
			msg_set_orignode(msg, tn->own_addr);
			spin_unlock_bh(&tsk->sk.sk_lock.slock);
		}
	}
	rcu_read_unlock();
}

static struct tipc_sock *tipc_sk_lookup(struct net *net, u32 portid)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_sock *tsk;

	rcu_read_lock();
	tsk = rhashtable_lookup_fast(&tn->sk_rht, &portid, tsk_rht_params);
	if (tsk)
		sock_hold(&tsk->sk);
	rcu_read_unlock();

	return tsk;
}

static int tipc_sk_insert(struct tipc_sock *tsk)
{
	struct sock *sk = &tsk->sk;
	struct net *net = sock_net(sk);
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	u32 remaining = (TIPC_MAX_PORT - TIPC_MIN_PORT) + 1;
	u32 portid = prandom_u32() % remaining + TIPC_MIN_PORT;

	while (remaining--) {
		portid++;
		if ((portid < TIPC_MIN_PORT) || (portid > TIPC_MAX_PORT))
			portid = TIPC_MIN_PORT;
		tsk->portid = portid;
		sock_hold(&tsk->sk);
		if (!rhashtable_lookup_insert_fast(&tn->sk_rht, &tsk->node,
						   tsk_rht_params))
			return 0;
		sock_put(&tsk->sk);
	}

	return -1;
}

static void tipc_sk_remove(struct tipc_sock *tsk)
{
	struct sock *sk = &tsk->sk;
	struct tipc_net *tn = net_generic(sock_net(sk), tipc_net_id);

	if (!rhashtable_remove_fast(&tn->sk_rht, &tsk->node, tsk_rht_params)) {
		WARN_ON(atomic_read(&sk->sk_refcnt) == 1);
		__sock_put(sk);
	}
}

static const struct rhashtable_params tsk_rht_params = {
	.nelem_hint = 192,
	.head_offset = offsetof(struct tipc_sock, node),
	.key_offset = offsetof(struct tipc_sock, portid),
	.key_len = sizeof(u32), /* portid */
	.max_size = 1048576,
	.min_size = 256,
	.automatic_shrinking = true,
};

int tipc_sk_rht_init(struct net *net)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);

	return rhashtable_init(&tn->sk_rht, &tsk_rht_params);
}

void tipc_sk_rht_destroy(struct net *net)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);

	/* Wait for socket readers to complete */
	synchronize_net();

	rhashtable_destroy(&tn->sk_rht);
}

/**
 * tipc_setsockopt - set socket option
 * @sock: socket structure
 * @lvl: option level
 * @opt: option identifier
 * @ov: pointer to new option value
 * @ol: length of option value
 *
 * For stream sockets only, accepts and ignores all IPPROTO_TCP options
 * (to ease compatibility).
 *
 * Returns 0 on success, errno otherwise
 */

static int setsockopt(struct socket *sock,
		      int lvl, int opt, char __user *ov, int ol)
{
	struct sock *sk = sock->sk;
	struct tipc_port *tport = tipc_sk_port(sk);
static int tipc_setsockopt(struct socket *sock, int lvl, int opt,
			   char __user *ov, unsigned int ol)
{
	struct sock *sk = sock->sk;
	struct tipc_sock *tsk = tipc_sk(sk);
	u32 value;
	int res;

	if ((lvl == IPPROTO_TCP) && (sock->type == SOCK_STREAM))
		return 0;
	if (lvl != SOL_TIPC)
		return -ENOPROTOOPT;
	if (ol < sizeof(value))
		return -EINVAL;
	if ((res = get_user(value, (u32 __user *)ov)))
	res = get_user(value, (u32 __user *)ov);
	if (res)
		return res;

	lock_sock(sk);

	switch (opt) {
	case TIPC_IMPORTANCE:
		res = tipc_set_portimportance(tport->ref, value);
		break;
	case TIPC_SRC_DROPPABLE:
		if (sock->type != SOCK_STREAM)
			res = tipc_set_portunreliable(tport->ref, value);
		res = tsk_set_importance(tsk, value);
		break;
	case TIPC_SRC_DROPPABLE:
		if (sock->type != SOCK_STREAM)
			tsk_set_unreliable(tsk, value);
		else
			res = -ENOPROTOOPT;
		break;
	case TIPC_DEST_DROPPABLE:
		res = tipc_set_portunreturnable(tport->ref, value);
		break;
	case TIPC_CONN_TIMEOUT:
		sk->sk_rcvtimeo = msecs_to_jiffies(value);
		tsk_set_unreturnable(tsk, value);
		break;
	case TIPC_CONN_TIMEOUT:
		tipc_sk(sk)->conn_timeout = value;
		/* no need to set "res", since already 0 at this point */
		break;
	default:
		res = -EINVAL;
	}

	release_sock(sk);

	return res;
}

/**
 * getsockopt - get socket option
 * tipc_getsockopt - get socket option
 * @sock: socket structure
 * @lvl: option level
 * @opt: option identifier
 * @ov: receptacle for option value
 * @ol: receptacle for length of option value
 *
 * For stream sockets only, returns 0 length result for all IPPROTO_TCP options
 * (to ease compatibility).
 *
 * Returns 0 on success, errno otherwise
 */

static int getsockopt(struct socket *sock,
		      int lvl, int opt, char __user *ov, int __user *ol)
{
	struct sock *sk = sock->sk;
	struct tipc_port *tport = tipc_sk_port(sk);
static int tipc_getsockopt(struct socket *sock, int lvl, int opt,
			   char __user *ov, int __user *ol)
{
	struct sock *sk = sock->sk;
	struct tipc_sock *tsk = tipc_sk(sk);
	int len;
	u32 value;
	int res;

	if ((lvl == IPPROTO_TCP) && (sock->type == SOCK_STREAM))
		return put_user(0, ol);
	if (lvl != SOL_TIPC)
		return -ENOPROTOOPT;
	if ((res = get_user(len, ol)))
	res = get_user(len, ol);
	if (res)
		return res;

	lock_sock(sk);

	switch (opt) {
	case TIPC_IMPORTANCE:
		res = tipc_portimportance(tport->ref, &value);
		break;
	case TIPC_SRC_DROPPABLE:
		res = tipc_portunreliable(tport->ref, &value);
		break;
	case TIPC_DEST_DROPPABLE:
		res = tipc_portunreturnable(tport->ref, &value);
		break;
	case TIPC_CONN_TIMEOUT:
		value = jiffies_to_msecs(sk->sk_rcvtimeo);
		/* no need to set "res", since already 0 at this point */
		break;
		value = tsk_importance(tsk);
		break;
	case TIPC_SRC_DROPPABLE:
		value = tsk_unreliable(tsk);
		break;
	case TIPC_DEST_DROPPABLE:
		value = tsk_unreturnable(tsk);
		break;
	case TIPC_CONN_TIMEOUT:
		value = tsk->conn_timeout;
		/* no need to set "res", since already 0 at this point */
		break;
	case TIPC_NODE_RECVQ_DEPTH:
		value = 0; /* was tipc_queue_size, now obsolete */
		break;
	case TIPC_SOCK_RECVQ_DEPTH:
		value = skb_queue_len(&sk->sk_receive_queue);
		break;
	default:
		res = -EINVAL;
	}

	release_sock(sk);

	if (res) {
		/* "get" failed */
	}
	else if (len < sizeof(value)) {
		res = -EINVAL;
	}
	else if (copy_to_user(ov, &value, sizeof(value))) {
		res = -EFAULT;
	}
	else {
		res = put_user(sizeof(value), ol);
	}

	return res;
}

/**
 * Protocol switches for the various types of TIPC sockets
 */

static const struct proto_ops msg_ops = {
	.owner 		= THIS_MODULE,
	.family		= AF_TIPC,
	.release	= release,
	.bind		= bind,
	.connect	= connect,
	.socketpair	= sock_no_socketpair,
	.accept		= accept,
	.getname	= get_name,
	.poll		= poll,
	.ioctl		= sock_no_ioctl,
	.listen		= listen,
	.shutdown	= shutdown,
	.setsockopt	= setsockopt,
	.getsockopt	= getsockopt,
	.sendmsg	= send_msg,
	.recvmsg	= recv_msg,
	if (res)
		return res;	/* "get" failed */

	if (len < sizeof(value))
		return -EINVAL;

	if (copy_to_user(ov, &value, sizeof(value)))
		return -EFAULT;

	return put_user(sizeof(value), ol);
}

static int tipc_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct sock *sk = sock->sk;
	struct tipc_sioc_ln_req lnr;
	void __user *argp = (void __user *)arg;

	switch (cmd) {
	case SIOCGETLINKNAME:
		if (copy_from_user(&lnr, argp, sizeof(lnr)))
			return -EFAULT;
		if (!tipc_node_get_linkname(sock_net(sk),
					    lnr.bearer_id & 0xffff, lnr.peer,
					    lnr.linkname, TIPC_MAX_LINK_NAME)) {
			if (copy_to_user(argp, &lnr, sizeof(lnr)))
				return -EFAULT;
			return 0;
		}
		return -EADDRNOTAVAIL;
	default:
		return -ENOIOCTLCMD;
	}
}

/* Protocol switches for the various types of TIPC sockets */

static const struct proto_ops msg_ops = {
	.owner		= THIS_MODULE,
	.family		= AF_TIPC,
	.release	= tipc_release,
	.bind		= tipc_bind,
	.connect	= tipc_connect,
	.socketpair	= sock_no_socketpair,
	.accept		= sock_no_accept,
	.getname	= tipc_getname,
	.poll		= tipc_poll,
	.ioctl		= tipc_ioctl,
	.listen		= sock_no_listen,
	.shutdown	= tipc_shutdown,
	.setsockopt	= tipc_setsockopt,
	.getsockopt	= tipc_getsockopt,
	.sendmsg	= tipc_sendmsg,
	.recvmsg	= tipc_recvmsg,
	.mmap		= sock_no_mmap,
	.sendpage	= sock_no_sendpage
};

static const struct proto_ops packet_ops = {
	.owner 		= THIS_MODULE,
	.family		= AF_TIPC,
	.release	= release,
	.bind		= bind,
	.connect	= connect,
	.socketpair	= sock_no_socketpair,
	.accept		= accept,
	.getname	= get_name,
	.poll		= poll,
	.ioctl		= sock_no_ioctl,
	.listen		= listen,
	.shutdown	= shutdown,
	.setsockopt	= setsockopt,
	.getsockopt	= getsockopt,
	.sendmsg	= send_packet,
	.recvmsg	= recv_msg,
	.owner		= THIS_MODULE,
	.family		= AF_TIPC,
	.release	= tipc_release,
	.bind		= tipc_bind,
	.connect	= tipc_connect,
	.socketpair	= sock_no_socketpair,
	.accept		= tipc_accept,
	.getname	= tipc_getname,
	.poll		= tipc_poll,
	.ioctl		= tipc_ioctl,
	.listen		= tipc_listen,
	.shutdown	= tipc_shutdown,
	.setsockopt	= tipc_setsockopt,
	.getsockopt	= tipc_getsockopt,
	.sendmsg	= tipc_send_packet,
	.recvmsg	= tipc_recvmsg,
	.mmap		= sock_no_mmap,
	.sendpage	= sock_no_sendpage
};

static const struct proto_ops stream_ops = {
	.owner 		= THIS_MODULE,
	.family		= AF_TIPC,
	.release	= release,
	.bind		= bind,
	.connect	= connect,
	.socketpair	= sock_no_socketpair,
	.accept		= accept,
	.getname	= get_name,
	.poll		= poll,
	.ioctl		= sock_no_ioctl,
	.listen		= listen,
	.shutdown	= shutdown,
	.setsockopt	= setsockopt,
	.getsockopt	= getsockopt,
	.sendmsg	= send_stream,
	.recvmsg	= recv_stream,
	.owner		= THIS_MODULE,
	.family		= AF_TIPC,
	.release	= tipc_release,
	.bind		= tipc_bind,
	.connect	= tipc_connect,
	.socketpair	= sock_no_socketpair,
	.accept		= tipc_accept,
	.getname	= tipc_getname,
	.poll		= tipc_poll,
	.ioctl		= tipc_ioctl,
	.listen		= tipc_listen,
	.shutdown	= tipc_shutdown,
	.setsockopt	= tipc_setsockopt,
	.getsockopt	= tipc_getsockopt,
	.sendmsg	= tipc_send_stream,
	.recvmsg	= tipc_recv_stream,
	.mmap		= sock_no_mmap,
	.sendpage	= sock_no_sendpage
};

static const struct net_proto_family tipc_family_ops = {
	.owner 		= THIS_MODULE,
	.family		= AF_TIPC,
	.create		= tipc_create
	.owner		= THIS_MODULE,
	.family		= AF_TIPC,
	.create		= tipc_sk_create
};

static struct proto tipc_proto = {
	.name		= "TIPC",
	.owner		= THIS_MODULE,
	.obj_size	= sizeof(struct tipc_sock)
	.obj_size	= sizeof(struct tipc_sock),
	.sysctl_rmem	= sysctl_tipc_rmem
};

/**
 * tipc_socket_init - initialize TIPC socket interface
 *
 * Returns 0 on success, errno otherwise
 */
int tipc_socket_init(void)
{
	int res;

	res = proto_register(&tipc_proto, 1);
	if (res) {
		err("Failed to register TIPC protocol type\n");
		pr_err("Failed to register TIPC protocol type\n");
		goto out;
	}

	res = sock_register(&tipc_family_ops);
	if (res) {
		err("Failed to register TIPC socket type\n");
		proto_unregister(&tipc_proto);
		goto out;
	}

	sockets_enabled = 1;
		pr_err("Failed to register TIPC socket type\n");
		proto_unregister(&tipc_proto);
		goto out;
	}
 out:
	return res;
}

/**
 * tipc_socket_stop - stop TIPC socket interface
 */

void tipc_socket_stop(void)
{
	if (!sockets_enabled)
		return;

	sockets_enabled = 0;
void tipc_socket_stop(void)
{
	sock_unregister(tipc_family_ops.family);
	proto_unregister(&tipc_proto);
}

/* Caller should hold socket lock for the passed tipc socket. */
static int __tipc_nl_add_sk_con(struct sk_buff *skb, struct tipc_sock *tsk)
{
	u32 peer_node;
	u32 peer_port;
	struct nlattr *nest;

	peer_node = tsk_peer_node(tsk);
	peer_port = tsk_peer_port(tsk);

	nest = nla_nest_start(skb, TIPC_NLA_SOCK_CON);

	if (nla_put_u32(skb, TIPC_NLA_CON_NODE, peer_node))
		goto msg_full;
	if (nla_put_u32(skb, TIPC_NLA_CON_SOCK, peer_port))
		goto msg_full;

	if (tsk->conn_type != 0) {
		if (nla_put_flag(skb, TIPC_NLA_CON_FLAG))
			goto msg_full;
		if (nla_put_u32(skb, TIPC_NLA_CON_TYPE, tsk->conn_type))
			goto msg_full;
		if (nla_put_u32(skb, TIPC_NLA_CON_INST, tsk->conn_instance))
			goto msg_full;
	}
	nla_nest_end(skb, nest);

	return 0;

msg_full:
	nla_nest_cancel(skb, nest);

	return -EMSGSIZE;
}

/* Caller should hold socket lock for the passed tipc socket. */
static int __tipc_nl_add_sk(struct sk_buff *skb, struct netlink_callback *cb,
			    struct tipc_sock *tsk)
{
	int err;
	void *hdr;
	struct nlattr *attrs;
	struct net *net = sock_net(skb->sk);
	struct tipc_net *tn = net_generic(net, tipc_net_id);

	hdr = genlmsg_put(skb, NETLINK_CB(cb->skb).portid, cb->nlh->nlmsg_seq,
			  &tipc_genl_family, NLM_F_MULTI, TIPC_NL_SOCK_GET);
	if (!hdr)
		goto msg_cancel;

	attrs = nla_nest_start(skb, TIPC_NLA_SOCK);
	if (!attrs)
		goto genlmsg_cancel;
	if (nla_put_u32(skb, TIPC_NLA_SOCK_REF, tsk->portid))
		goto attr_msg_cancel;
	if (nla_put_u32(skb, TIPC_NLA_SOCK_ADDR, tn->own_addr))
		goto attr_msg_cancel;

	if (tsk->connected) {
		err = __tipc_nl_add_sk_con(skb, tsk);
		if (err)
			goto attr_msg_cancel;
	} else if (!list_empty(&tsk->publications)) {
		if (nla_put_flag(skb, TIPC_NLA_SOCK_HAS_PUBL))
			goto attr_msg_cancel;
	}
	nla_nest_end(skb, attrs);
	genlmsg_end(skb, hdr);

	return 0;

attr_msg_cancel:
	nla_nest_cancel(skb, attrs);
genlmsg_cancel:
	genlmsg_cancel(skb, hdr);
msg_cancel:
	return -EMSGSIZE;
}

int tipc_nl_sk_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	int err;
	struct tipc_sock *tsk;
	const struct bucket_table *tbl;
	struct rhash_head *pos;
	struct net *net = sock_net(skb->sk);
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	u32 tbl_id = cb->args[0];
	u32 prev_portid = cb->args[1];

	rcu_read_lock();
	tbl = rht_dereference_rcu((&tn->sk_rht)->tbl, &tn->sk_rht);
	for (; tbl_id < tbl->size; tbl_id++) {
		rht_for_each_entry_rcu(tsk, pos, tbl, tbl_id, node) {
			spin_lock_bh(&tsk->sk.sk_lock.slock);
			if (prev_portid && prev_portid != tsk->portid) {
				spin_unlock_bh(&tsk->sk.sk_lock.slock);
				continue;
			}

			err = __tipc_nl_add_sk(skb, cb, tsk);
			if (err) {
				prev_portid = tsk->portid;
				spin_unlock_bh(&tsk->sk.sk_lock.slock);
				goto out;
			}
			prev_portid = 0;
			spin_unlock_bh(&tsk->sk.sk_lock.slock);
		}
	}
out:
	rcu_read_unlock();
	cb->args[0] = tbl_id;
	cb->args[1] = prev_portid;

	return skb->len;
}

/* Caller should hold socket lock for the passed tipc socket. */
static int __tipc_nl_add_sk_publ(struct sk_buff *skb,
				 struct netlink_callback *cb,
				 struct publication *publ)
{
	void *hdr;
	struct nlattr *attrs;

	hdr = genlmsg_put(skb, NETLINK_CB(cb->skb).portid, cb->nlh->nlmsg_seq,
			  &tipc_genl_family, NLM_F_MULTI, TIPC_NL_PUBL_GET);
	if (!hdr)
		goto msg_cancel;

	attrs = nla_nest_start(skb, TIPC_NLA_PUBL);
	if (!attrs)
		goto genlmsg_cancel;

	if (nla_put_u32(skb, TIPC_NLA_PUBL_KEY, publ->key))
		goto attr_msg_cancel;
	if (nla_put_u32(skb, TIPC_NLA_PUBL_TYPE, publ->type))
		goto attr_msg_cancel;
	if (nla_put_u32(skb, TIPC_NLA_PUBL_LOWER, publ->lower))
		goto attr_msg_cancel;
	if (nla_put_u32(skb, TIPC_NLA_PUBL_UPPER, publ->upper))
		goto attr_msg_cancel;

	nla_nest_end(skb, attrs);
	genlmsg_end(skb, hdr);

	return 0;

attr_msg_cancel:
	nla_nest_cancel(skb, attrs);
genlmsg_cancel:
	genlmsg_cancel(skb, hdr);
msg_cancel:
	return -EMSGSIZE;
}

/* Caller should hold socket lock for the passed tipc socket. */
static int __tipc_nl_list_sk_publ(struct sk_buff *skb,
				  struct netlink_callback *cb,
				  struct tipc_sock *tsk, u32 *last_publ)
{
	int err;
	struct publication *p;

	if (*last_publ) {
		list_for_each_entry(p, &tsk->publications, pport_list) {
			if (p->key == *last_publ)
				break;
		}
		if (p->key != *last_publ) {
			/* We never set seq or call nl_dump_check_consistent()
			 * this means that setting prev_seq here will cause the
			 * consistence check to fail in the netlink callback
			 * handler. Resulting in the last NLMSG_DONE message
			 * having the NLM_F_DUMP_INTR flag set.
			 */
			cb->prev_seq = 1;
			*last_publ = 0;
			return -EPIPE;
		}
	} else {
		p = list_first_entry(&tsk->publications, struct publication,
				     pport_list);
	}

	list_for_each_entry_from(p, &tsk->publications, pport_list) {
		err = __tipc_nl_add_sk_publ(skb, cb, p);
		if (err) {
			*last_publ = p->key;
			return err;
		}
	}
	*last_publ = 0;

	return 0;
}

int tipc_nl_publ_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	int err;
	u32 tsk_portid = cb->args[0];
	u32 last_publ = cb->args[1];
	u32 done = cb->args[2];
	struct net *net = sock_net(skb->sk);
	struct tipc_sock *tsk;

	if (!tsk_portid) {
		struct nlattr **attrs;
		struct nlattr *sock[TIPC_NLA_SOCK_MAX + 1];

		err = tipc_nlmsg_parse(cb->nlh, &attrs);
		if (err)
			return err;

		if (!attrs[TIPC_NLA_SOCK])
			return -EINVAL;

		err = nla_parse_nested(sock, TIPC_NLA_SOCK_MAX,
				       attrs[TIPC_NLA_SOCK],
				       tipc_nl_sock_policy);
		if (err)
			return err;

		if (!sock[TIPC_NLA_SOCK_REF])
			return -EINVAL;

		tsk_portid = nla_get_u32(sock[TIPC_NLA_SOCK_REF]);
	}

	if (done)
		return 0;

	tsk = tipc_sk_lookup(net, tsk_portid);
	if (!tsk)
		return -EINVAL;

	lock_sock(&tsk->sk);
	err = __tipc_nl_list_sk_publ(skb, cb, tsk, &last_publ);
	if (!err)
		done = 1;
	release_sock(&tsk->sk);
	sock_put(&tsk->sk);

	cb->args[0] = tsk_portid;
	cb->args[1] = last_publ;
	cb->args[2] = done;

	return skb->len;
}
