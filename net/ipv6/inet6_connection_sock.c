/*
 * INET        An implementation of the TCP/IP protocol suite for the LINUX
 *             operating system.  INET is implemented using the  BSD Socket
 *             interface as the means of communication with the user level.
 *
 *             Support for INET6 connection oriented protocols.
 *
 * Authors:    See the TCPv6 sources
 *
 *             This program is free software; you can redistribute it and/or
 *             modify it under the terms of the GNU General Public License
 *             as published by the Free Software Foundation; either version
 *             2 of the License, or(at your option) any later version.
 */

#include <linux/module.h>
#include <linux/in6.h>
#include <linux/ipv6.h>
#include <linux/jhash.h>
#include <linux/slab.h>

#include <net/addrconf.h>
#include <net/inet_connection_sock.h>
#include <net/inet_ecn.h>
#include <net/inet_hashtables.h>
#include <net/ip6_route.h>
#include <net/sock.h>
#include <net/inet6_connection_sock.h>

int inet6_csk_bind_conflict(const struct sock *sk,
			    const struct inet_bind_bucket *tb)
{
	const struct sock *sk2;
	const struct hlist_node *node;
			    const struct inet_bind_bucket *tb, bool relax)
{
	const struct sock *sk2;
	int reuse = sk->sk_reuse;
	int reuseport = sk->sk_reuseport;
	kuid_t uid = sock_i_uid((struct sock *)sk);

	/* We must walk the whole port owner list in this case. -DaveM */
	/*
	 * See comment in inet_csk_bind_conflict about sock lookup
	 * vs net namespaces issues.
	 */
	sk_for_each_bound(sk2, node, &tb->owners) {
		if (sk != sk2 &&
		    (!sk->sk_bound_dev_if ||
		     !sk2->sk_bound_dev_if ||
		     sk->sk_bound_dev_if == sk2->sk_bound_dev_if) &&
		    (!sk->sk_reuse || !sk2->sk_reuse ||
		     sk2->sk_state == TCP_LISTEN) &&
		     ipv6_rcv_saddr_equal(sk, sk2))
			break;
	}

	return node != NULL;
}

EXPORT_SYMBOL_GPL(inet6_csk_bind_conflict);

/*
 * request_sock (formerly open request) hash tables.
 */
static u32 inet6_synq_hash(const struct in6_addr *raddr, const __be16 rport,
			   const u32 rnd, const u16 synq_hsize)
{
	u32 a = (__force u32)raddr->s6_addr32[0];
	u32 b = (__force u32)raddr->s6_addr32[1];
	u32 c = (__force u32)raddr->s6_addr32[2];

	a += JHASH_GOLDEN_RATIO;
	b += JHASH_GOLDEN_RATIO;
	c += rnd;
	__jhash_mix(a, b, c);

	a += (__force u32)raddr->s6_addr32[3];
	b += (__force u32)rport;
	__jhash_mix(a, b, c);

	return c & (synq_hsize - 1);
}

struct request_sock *inet6_csk_search_req(const struct sock *sk,
					  struct request_sock ***prevp,
					  const __be16 rport,
					  const struct in6_addr *raddr,
					  const struct in6_addr *laddr,
					  const int iif)
{
	const struct inet_connection_sock *icsk = inet_csk(sk);
	struct listen_sock *lopt = icsk->icsk_accept_queue.listen_opt;
	struct request_sock *req, **prev;

	for (prev = &lopt->syn_table[inet6_synq_hash(raddr, rport,
						     lopt->hash_rnd,
						     lopt->nr_table_entries)];
	     (req = *prev) != NULL;
	     prev = &req->dl_next) {
		const struct inet6_request_sock *treq = inet6_rsk(req);

		if (inet_rsk(req)->rmt_port == rport &&
		    req->rsk_ops->family == AF_INET6 &&
		    ipv6_addr_equal(&treq->rmt_addr, raddr) &&
		    ipv6_addr_equal(&treq->loc_addr, laddr) &&
		    (!treq->iif || treq->iif == iif)) {
			WARN_ON(req->sk != NULL);
			*prevp = prev;
			return req;
		}
	}

	return NULL;
}

EXPORT_SYMBOL_GPL(inet6_csk_search_req);

void inet6_csk_reqsk_queue_hash_add(struct sock *sk,
				    struct request_sock *req,
				    const unsigned long timeout)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
	struct listen_sock *lopt = icsk->icsk_accept_queue.listen_opt;
	const u32 h = inet6_synq_hash(&inet6_rsk(req)->rmt_addr,
				      inet_rsk(req)->rmt_port,
				      lopt->hash_rnd, lopt->nr_table_entries);

	reqsk_queue_hash_req(&icsk->icsk_accept_queue, h, req, timeout);
	inet_csk_reqsk_queue_added(sk, timeout);
}

EXPORT_SYMBOL_GPL(inet6_csk_reqsk_queue_hash_add);

void inet6_csk_addr2sockaddr(struct sock *sk, struct sockaddr * uaddr)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *) uaddr;

	sin6->sin6_family = AF_INET6;
	ipv6_addr_copy(&sin6->sin6_addr, &np->daddr);
	sin6->sin6_port	= inet_sk(sk)->dport;
	/* We do not store received flowlabel for TCP */
	sin6->sin6_flowinfo = 0;
	sin6->sin6_scope_id = 0;
	if (sk->sk_bound_dev_if &&
	    ipv6_addr_type(&sin6->sin6_addr) & IPV6_ADDR_LINKLOCAL)
		sin6->sin6_scope_id = sk->sk_bound_dev_if;
}

EXPORT_SYMBOL_GPL(inet6_csk_addr2sockaddr);

static inline
void __inet6_csk_dst_store(struct sock *sk, struct dst_entry *dst,
			   struct in6_addr *daddr, struct in6_addr *saddr)
{
	__ip6_dst_store(sk, dst, daddr, saddr);

#ifdef CONFIG_XFRM
	{
		struct rt6_info *rt = (struct rt6_info  *)dst;
		rt->rt6i_flow_cache_genid = atomic_read(&flow_cache_genid);
	}
#endif
}

static inline
struct dst_entry *__inet6_csk_dst_check(struct sock *sk, u32 cookie)
{
	struct dst_entry *dst;

	dst = __sk_dst_check(sk, cookie);

#ifdef CONFIG_XFRM
	if (dst) {
		struct rt6_info *rt = (struct rt6_info *)dst;
		if (rt->rt6i_flow_cache_genid != atomic_read(&flow_cache_genid)) {
			sk->sk_dst_cache = NULL;
			dst_release(dst);
			dst = NULL;
		}
	}
#endif

	return dst;
}

int inet6_csk_xmit(struct sk_buff *skb, int ipfragok)
{
	struct sock *sk = skb->sk;
	struct inet_sock *inet = inet_sk(sk);
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct flowi fl;
	struct dst_entry *dst;
	struct in6_addr *final_p = NULL, final;

	memset(&fl, 0, sizeof(fl));
	fl.proto = sk->sk_protocol;
	ipv6_addr_copy(&fl.fl6_dst, &np->daddr);
	ipv6_addr_copy(&fl.fl6_src, &np->saddr);
	fl.fl6_flowlabel = np->flow_label;
	IP6_ECN_flow_xmit(sk, fl.fl6_flowlabel);
	fl.oif = sk->sk_bound_dev_if;
	fl.fl_ip_sport = inet->sport;
	fl.fl_ip_dport = inet->dport;
	security_sk_classify_flow(sk, &fl);

	if (np->opt && np->opt->srcrt) {
		struct rt0_hdr *rt0 = (struct rt0_hdr *)np->opt->srcrt;
		ipv6_addr_copy(&final, &fl.fl6_dst);
		ipv6_addr_copy(&fl.fl6_dst, rt0->addr);
		final_p = &final;
	}

	dst = __inet6_csk_dst_check(sk, np->dst_cookie);

	if (dst == NULL) {
		int err = ip6_dst_lookup(sk, &dst, &fl);

		if (err) {
			sk->sk_err_soft = -err;
			kfree_skb(skb);
			return err;
		}

		if (final_p)
			ipv6_addr_copy(&fl.fl6_dst, final_p);

		if ((err = xfrm_lookup(&dst, &fl, sk, 0)) < 0) {
			sk->sk_route_caps = 0;
			kfree_skb(skb);
			return err;
		}

		__inet6_csk_dst_store(sk, dst, NULL, NULL);
	}

	skb->dst = dst_clone(dst);

	/* Restore final destination back after routing done */
	ipv6_addr_copy(&fl.fl6_dst, &np->daddr);

	return ip6_xmit(sk, skb, &fl, np->opt, 0);
}

EXPORT_SYMBOL_GPL(inet6_csk_xmit);
	sk_for_each_bound(sk2, &tb->owners) {
		if (sk != sk2 &&
		    (!sk->sk_bound_dev_if ||
		     !sk2->sk_bound_dev_if ||
		     sk->sk_bound_dev_if == sk2->sk_bound_dev_if)) {
			if ((!reuse || !sk2->sk_reuse ||
			     sk2->sk_state == TCP_LISTEN) &&
			    (!reuseport || !sk2->sk_reuseport ||
			     (sk2->sk_state != TCP_TIME_WAIT &&
			      !uid_eq(uid,
				      sock_i_uid((struct sock *)sk2))))) {
				if (ipv6_rcv_saddr_equal(sk, sk2))
					break;
			}
			if (!relax && reuse && sk2->sk_reuse &&
			    sk2->sk_state != TCP_LISTEN &&
			    ipv6_rcv_saddr_equal(sk, sk2))
				break;
		}
	}

	return sk2 != NULL;
}
EXPORT_SYMBOL_GPL(inet6_csk_bind_conflict);

struct dst_entry *inet6_csk_route_req(const struct sock *sk,
				      struct flowi6 *fl6,
				      const struct request_sock *req,
				      u8 proto)
{
	struct inet_request_sock *ireq = inet_rsk(req);
	const struct ipv6_pinfo *np = inet6_sk(sk);
	struct in6_addr *final_p, final;
	struct dst_entry *dst;

	memset(fl6, 0, sizeof(*fl6));
	fl6->flowi6_proto = proto;
	fl6->daddr = ireq->ir_v6_rmt_addr;
	rcu_read_lock();
	final_p = fl6_update_dst(fl6, rcu_dereference(np->opt), &final);
	rcu_read_unlock();
	fl6->saddr = ireq->ir_v6_loc_addr;
	fl6->flowi6_oif = ireq->ir_iif;
	fl6->flowi6_mark = ireq->ir_mark;
	fl6->fl6_dport = ireq->ir_rmt_port;
	fl6->fl6_sport = htons(ireq->ir_num);
	security_req_classify_flow(req, flowi6_to_flowi(fl6));

	dst = ip6_dst_lookup_flow(sock_net(sk), sk, fl6, final_p);
	if (IS_ERR(dst))
		return NULL;

	return dst;
}
EXPORT_SYMBOL(inet6_csk_route_req);

void inet6_csk_addr2sockaddr(struct sock *sk, struct sockaddr *uaddr)
{
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *) uaddr;

	sin6->sin6_family = AF_INET6;
	sin6->sin6_addr = sk->sk_v6_daddr;
	sin6->sin6_port	= inet_sk(sk)->inet_dport;
	/* We do not store received flowlabel for TCP */
	sin6->sin6_flowinfo = 0;
	sin6->sin6_scope_id = ipv6_iface_scope_id(&sin6->sin6_addr,
						  sk->sk_bound_dev_if);
}
EXPORT_SYMBOL_GPL(inet6_csk_addr2sockaddr);

static inline
struct dst_entry *__inet6_csk_dst_check(struct sock *sk, u32 cookie)
{
	return __sk_dst_check(sk, cookie);
}

static struct dst_entry *inet6_csk_route_socket(struct sock *sk,
						struct flowi6 *fl6)
{
	struct inet_sock *inet = inet_sk(sk);
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct in6_addr *final_p, final;
	struct dst_entry *dst;

	memset(fl6, 0, sizeof(*fl6));
	fl6->flowi6_proto = sk->sk_protocol;
	fl6->daddr = sk->sk_v6_daddr;
	fl6->saddr = np->saddr;
	fl6->flowlabel = np->flow_label;
	IP6_ECN_flow_xmit(sk, fl6->flowlabel);
	fl6->flowi6_oif = sk->sk_bound_dev_if;
	fl6->flowi6_mark = sk->sk_mark;
	fl6->fl6_sport = inet->inet_sport;
	fl6->fl6_dport = inet->inet_dport;
	security_sk_classify_flow(sk, flowi6_to_flowi(fl6));

	rcu_read_lock();
	final_p = fl6_update_dst(fl6, rcu_dereference(np->opt), &final);
	rcu_read_unlock();

	dst = __inet6_csk_dst_check(sk, np->dst_cookie);
	if (!dst) {
		dst = ip6_dst_lookup_flow(sock_net(sk), sk, fl6, final_p);

		if (!IS_ERR(dst))
			ip6_dst_store(sk, dst, NULL, NULL);
	}
	return dst;
}

int inet6_csk_xmit(struct sock *sk, struct sk_buff *skb, struct flowi *fl_unused)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct flowi6 fl6;
	struct dst_entry *dst;
	int res;

	dst = inet6_csk_route_socket(sk, &fl6);
	if (IS_ERR(dst)) {
		sk->sk_err_soft = -PTR_ERR(dst);
		sk->sk_route_caps = 0;
		kfree_skb(skb);
		return PTR_ERR(dst);
	}

	rcu_read_lock();
	skb_dst_set_noref(skb, dst);

	/* Restore final destination back after routing done */
	fl6.daddr = sk->sk_v6_daddr;

	res = ip6_xmit(sk, skb, &fl6, rcu_dereference(np->opt),
		       np->tclass);
	rcu_read_unlock();
	return res;
}
EXPORT_SYMBOL_GPL(inet6_csk_xmit);

struct dst_entry *inet6_csk_update_pmtu(struct sock *sk, u32 mtu)
{
	struct flowi6 fl6;
	struct dst_entry *dst = inet6_csk_route_socket(sk, &fl6);

	if (IS_ERR(dst))
		return NULL;
	dst->ops->update_pmtu(dst, sk, NULL, mtu);

	dst = inet6_csk_route_socket(sk, &fl6);
	return IS_ERR(dst) ? NULL : dst;
}
EXPORT_SYMBOL_GPL(inet6_csk_update_pmtu);
