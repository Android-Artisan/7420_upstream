/*
 * xfrm4_policy.c
 *
 * Changes:
 *	Kazunori MIYAZAWA @USAGI
 * 	YOSHIFUJI Hideaki @USAGI
 *		Split up af-specific portion
 *
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/inetdevice.h>
#include <net/dst.h>
#include <net/xfrm.h>
#include <net/ip.h>

static struct dst_ops xfrm4_dst_ops;
static struct xfrm_policy_afinfo xfrm4_policy_afinfo;

static struct dst_entry *xfrm4_dst_lookup(int tos, xfrm_address_t *saddr,
					  xfrm_address_t *daddr)
{
	struct flowi fl = {
		.nl_u = {
			.ip4_u = {
				.tos = tos,
				.daddr = daddr->a4,
			},
		},
	};
	struct dst_entry *dst;
	struct rtable *rt;
	int err;

	if (saddr)
		fl.fl4_src = saddr->a4;

	err = __ip_route_output_key(&init_net, &rt, &fl);
	dst = &rt->u.dst;
	if (err)
		dst = ERR_PTR(err);
	return dst;
}

static int xfrm4_get_saddr(xfrm_address_t *saddr, xfrm_address_t *daddr)
{
	struct dst_entry *dst;
	struct rtable *rt;

	dst = xfrm4_dst_lookup(0, NULL, daddr);
	if (IS_ERR(dst))
		return -EHOSTUNREACH;

	rt = (struct rtable *)dst;
	saddr->a4 = rt->rt_src;
#include <linux/if_tunnel.h>
#include <net/dst.h>
#include <net/xfrm.h>
#include <net/ip.h>
#include <net/l3mdev.h>
#include <net/inet_ecn.h>

static struct xfrm_policy_afinfo xfrm4_policy_afinfo;

static struct dst_entry *__xfrm4_dst_lookup(struct net *net, struct flowi4 *fl4,
					    int tos, int oif,
					    const xfrm_address_t *saddr,
					    const xfrm_address_t *daddr)
{
	struct rtable *rt;

	memset(fl4, 0, sizeof(*fl4));
	fl4->daddr = daddr->a4;
	fl4->flowi4_tos = tos;
	fl4->flowi4_oif = oif;
	if (saddr)
		fl4->saddr = saddr->a4;

	fl4->flowi4_flags = FLOWI_FLAG_SKIP_NH_OIF;

	rt = __ip_route_output_key(net, fl4);
	if (!IS_ERR(rt))
		return &rt->dst;

	return ERR_CAST(rt);
}

static struct dst_entry *xfrm4_dst_lookup(struct net *net, int tos, int oif,
					  const xfrm_address_t *saddr,
					  const xfrm_address_t *daddr)
{
	struct flowi4 fl4;

	return __xfrm4_dst_lookup(net, &fl4, tos, oif, saddr, daddr);
}

static int xfrm4_get_saddr(struct net *net, int oif,
			   xfrm_address_t *saddr, xfrm_address_t *daddr)
{
	struct dst_entry *dst;
	struct flowi4 fl4;

	dst = __xfrm4_dst_lookup(net, &fl4, 0, oif, NULL, daddr);
	if (IS_ERR(dst))
		return -EHOSTUNREACH;

	saddr->a4 = fl4.saddr;
	dst_release(dst);
	return 0;
}

static struct dst_entry *
__xfrm4_find_bundle(struct flowi *fl, struct xfrm_policy *policy)
{
	struct dst_entry *dst;

	read_lock_bh(&policy->lock);
	for (dst = policy->bundles; dst; dst = dst->next) {
		struct xfrm_dst *xdst = (struct xfrm_dst*)dst;
		if (xdst->u.rt.fl.oif == fl->oif &&	/*XXX*/
		    xdst->u.rt.fl.fl4_dst == fl->fl4_dst &&
		    xdst->u.rt.fl.fl4_src == fl->fl4_src &&
		    xdst->u.rt.fl.fl4_tos == fl->fl4_tos &&
		    xfrm_bundle_ok(policy, xdst, fl, AF_INET, 0)) {
			dst_clone(dst);
			break;
		}
	}
	read_unlock_bh(&policy->lock);
	return dst;
}

static int xfrm4_get_tos(struct flowi *fl)
{
	return fl->fl4_tos;
static int xfrm4_get_tos(const struct flowi *fl)
{
	return IPTOS_RT_MASK & fl->u.ip4.flowi4_tos; /* Strip ECN bits */
}

static int xfrm4_init_path(struct xfrm_dst *path, struct dst_entry *dst,
			   int nfheader_len)
{
	return 0;
}

static int xfrm4_fill_dst(struct xfrm_dst *xdst, struct net_device *dev)
{
	struct rtable *rt = (struct rtable *)xdst->route;

	xdst->u.rt.fl = rt->fl;
static int xfrm4_fill_dst(struct xfrm_dst *xdst, struct net_device *dev,
			  const struct flowi *fl)
{
	struct rtable *rt = (struct rtable *)xdst->route;
	const struct flowi4 *fl4 = &fl->u.ip4;

	xdst->u.rt.rt_iif = fl4->flowi4_iif;

	xdst->u.dst.dev = dev;
	dev_hold(dev);

	xdst->u.rt.idev = in_dev_get(dev);
	if (!xdst->u.rt.idev)
		return -ENODEV;

	xdst->u.rt.peer = rt->peer;
	if (rt->peer)
		atomic_inc(&rt->peer->refcnt);

	/* Sheit... I remember I did this right. Apparently,
	 * it was magically lost, so this code needs audit */
	xdst->u.rt.rt_flags = rt->rt_flags & (RTCF_BROADCAST | RTCF_MULTICAST |
					      RTCF_LOCAL);
	xdst->u.rt.rt_type = rt->rt_type;
	xdst->u.rt.rt_src = rt->rt_src;
	xdst->u.rt.rt_dst = rt->rt_dst;
	xdst->u.rt.rt_gateway = rt->rt_gateway;
	xdst->u.rt.rt_spec_dst = rt->rt_spec_dst;
	/* Sheit... I remember I did this right. Apparently,
	 * it was magically lost, so this code needs audit */
	xdst->u.rt.rt_is_input = rt->rt_is_input;
	xdst->u.rt.rt_flags = rt->rt_flags & (RTCF_BROADCAST | RTCF_MULTICAST |
					      RTCF_LOCAL);
	xdst->u.rt.rt_type = rt->rt_type;
	xdst->u.rt.rt_gateway = rt->rt_gateway;
	xdst->u.rt.rt_uses_gateway = rt->rt_uses_gateway;
	xdst->u.rt.rt_pmtu = rt->rt_pmtu;
	xdst->u.rt.rt_mtu_locked = rt->rt_mtu_locked;
	xdst->u.rt.rt_table_id = rt->rt_table_id;
	INIT_LIST_HEAD(&xdst->u.rt.rt_uncached);

	return 0;
}

static void
_decode_session4(struct sk_buff *skb, struct flowi *fl, int reverse)
{
	struct iphdr *iph = ip_hdr(skb);
	u8 *xprth = skb_network_header(skb) + iph->ihl * 4;

	memset(fl, 0, sizeof(struct flowi));
	if (!(iph->frag_off & htons(IP_MF | IP_OFFSET))) {
	const struct iphdr *iph = ip_hdr(skb);
	int ihl = iph->ihl;
	u8 *xprth = skb_network_header(skb) + ihl * 4;
	struct flowi4 *fl4 = &fl->u.ip4;
	int oif = 0;

	if (skb_dst(skb))
		oif = l3mdev_fib_oif(skb_dst(skb)->dev);

	memset(fl4, 0, sizeof(struct flowi4));
	fl4->flowi4_mark = skb->mark;
	fl4->flowi4_oif = reverse ? skb->skb_iif : oif;

	fl4->flowi4_proto = iph->protocol;
	fl4->daddr = reverse ? iph->saddr : iph->daddr;
	fl4->saddr = reverse ? iph->daddr : iph->saddr;
	fl4->flowi4_tos = iph->tos & ~INET_ECN_MASK;

	if (!ip_is_fragment(iph)) {
		switch (iph->protocol) {
		case IPPROTO_UDP:
		case IPPROTO_UDPLITE:
		case IPPROTO_TCP:
		case IPPROTO_SCTP:
		case IPPROTO_DCCP:
			if (pskb_may_pull(skb, xprth + 4 - skb->data)) {
				__be16 *ports = (__be16 *)xprth;

				fl->fl_ip_sport = ports[!!reverse];
				fl->fl_ip_dport = ports[!reverse];
			if (xprth + 4 < skb->data ||
			    pskb_may_pull(skb, xprth + 4 - skb->data)) {
				__be16 *ports;

				xprth = skb_network_header(skb) + ihl * 4;
				ports = (__be16 *)xprth;

				fl4->fl4_sport = ports[!!reverse];
				fl4->fl4_dport = ports[!reverse];
			}
			break;

		case IPPROTO_ICMP:
			if (pskb_may_pull(skb, xprth + 2 - skb->data)) {
				u8 *icmp = xprth;

				fl->fl_icmp_type = icmp[0];
				fl->fl_icmp_code = icmp[1];
			if (xprth + 2 < skb->data ||
			    pskb_may_pull(skb, xprth + 2 - skb->data)) {
				u8 *icmp;

				xprth = skb_network_header(skb) + ihl * 4;
				icmp = xprth;

				fl4->fl4_icmp_type = icmp[0];
				fl4->fl4_icmp_code = icmp[1];
			}
			break;

		case IPPROTO_ESP:
			if (pskb_may_pull(skb, xprth + 4 - skb->data)) {
				__be32 *ehdr = (__be32 *)xprth;

				fl->fl_ipsec_spi = ehdr[0];
			if (xprth + 4 < skb->data ||
			    pskb_may_pull(skb, xprth + 4 - skb->data)) {
				__be32 *ehdr;

				xprth = skb_network_header(skb) + ihl * 4;
				ehdr = (__be32 *)xprth;

				fl4->fl4_ipsec_spi = ehdr[0];
			}
			break;

		case IPPROTO_AH:
			if (pskb_may_pull(skb, xprth + 8 - skb->data)) {
				__be32 *ah_hdr = (__be32*)xprth;

				fl->fl_ipsec_spi = ah_hdr[1];
			if (xprth + 8 < skb->data ||
			    pskb_may_pull(skb, xprth + 8 - skb->data)) {
				__be32 *ah_hdr;

				xprth = skb_network_header(skb) + ihl * 4;
				ah_hdr = (__be32 *)xprth;

				fl4->fl4_ipsec_spi = ah_hdr[1];
			}
			break;

		case IPPROTO_COMP:
			if (pskb_may_pull(skb, xprth + 4 - skb->data)) {
				__be16 *ipcomp_hdr = (__be16 *)xprth;

				fl->fl_ipsec_spi = htonl(ntohs(ipcomp_hdr[1]));
			}
			break;
		default:
			fl->fl_ipsec_spi = 0;
			break;
		}
	}
	fl->proto = iph->protocol;
	fl->fl4_dst = reverse ? iph->saddr : iph->daddr;
	fl->fl4_src = reverse ? iph->daddr : iph->saddr;
	fl->fl4_tos = iph->tos;
			if (xprth + 4 < skb->data ||
			    pskb_may_pull(skb, xprth + 4 - skb->data)) {
				__be16 *ipcomp_hdr;

				xprth = skb_network_header(skb) + ihl * 4;
				ipcomp_hdr = (__be16 *)xprth;

				fl4->fl4_ipsec_spi = htonl(ntohs(ipcomp_hdr[1]));
			}
			break;

		case IPPROTO_GRE:
			if (xprth + 12 < skb->data ||
			    pskb_may_pull(skb, xprth + 12 - skb->data)) {
				__be16 *greflags;
				__be32 *gre_hdr;

				xprth = skb_network_header(skb) + ihl * 4;
				greflags = (__be16 *)xprth;
				gre_hdr = (__be32 *)xprth;

				if (greflags[0] & GRE_KEY) {
					if (greflags[0] & GRE_CSUM)
						gre_hdr++;
					fl4->fl4_gre_key = gre_hdr[1];
				}
			}
			break;

		default:
			fl4->fl4_ipsec_spi = 0;
			break;
		}
	}
}

static inline int xfrm4_garbage_collect(struct dst_ops *ops)
{
	xfrm4_policy_afinfo.garbage_collect();
	return (atomic_read(&xfrm4_dst_ops.entries) > xfrm4_dst_ops.gc_thresh*2);
}

static void xfrm4_update_pmtu(struct dst_entry *dst, u32 mtu)
	struct net *net = container_of(ops, struct net, xfrm.xfrm4_dst_ops);

	xfrm4_policy_afinfo.garbage_collect(net);
	return (dst_entries_get_slow(ops) > ops->gc_thresh * 2);
}

static void xfrm4_update_pmtu(struct dst_entry *dst, struct sock *sk,
			      struct sk_buff *skb, u32 mtu)
{
	struct xfrm_dst *xdst = (struct xfrm_dst *)dst;
	struct dst_entry *path = xdst->route;

	path->ops->update_pmtu(path, mtu);
	path->ops->update_pmtu(path, sk, skb, mtu);
}

static void xfrm4_redirect(struct dst_entry *dst, struct sock *sk,
			   struct sk_buff *skb)
{
	struct xfrm_dst *xdst = (struct xfrm_dst *)dst;
	struct dst_entry *path = xdst->route;

	path->ops->redirect(path, sk, skb);
}

static void xfrm4_dst_destroy(struct dst_entry *dst)
{
	struct xfrm_dst *xdst = (struct xfrm_dst *)dst;

	if (likely(xdst->u.rt.idev))
		in_dev_put(xdst->u.rt.idev);
	if (likely(xdst->u.rt.peer))
		inet_putpeer(xdst->u.rt.peer);
	dst_destroy_metrics_generic(dst);

	xfrm_dst_destroy(xdst);
}

static void xfrm4_dst_ifdown(struct dst_entry *dst, struct net_device *dev,
			     int unregister)
{
	struct xfrm_dst *xdst;

	if (!unregister)
		return;

	xdst = (struct xfrm_dst *)dst;
	if (xdst->u.rt.idev->dev == dev) {
		struct in_device *loopback_idev =
			in_dev_get(dev_net(dev)->loopback_dev);
		BUG_ON(!loopback_idev);

		do {
			in_dev_put(xdst->u.rt.idev);
			xdst->u.rt.idev = loopback_idev;
			in_dev_hold(loopback_idev);
			xdst = (struct xfrm_dst *)xdst->u.dst.child;
		} while (xdst->u.dst.xfrm);

		__in_dev_put(loopback_idev);
	}

	xfrm_dst_ifdown(dst, dev);
}

static struct dst_ops xfrm4_dst_ops = {
	.family =		AF_INET,
	.protocol =		__constant_htons(ETH_P_IP),
	.gc =			xfrm4_garbage_collect,
	.update_pmtu =		xfrm4_update_pmtu,
	.destroy =		xfrm4_dst_destroy,
	.ifdown =		xfrm4_dst_ifdown,
	.local_out =		__ip_local_out,
	.gc_thresh =		1024,
	.entry_size =		sizeof(struct xfrm_dst),
	.entries =		ATOMIC_INIT(0),
	if (!unregister)
		return;

	xfrm_dst_ifdown(dst, dev);
}

static struct dst_ops xfrm4_dst_ops_template = {
	.family =		AF_INET,
	.gc =			xfrm4_garbage_collect,
	.update_pmtu =		xfrm4_update_pmtu,
	.redirect =		xfrm4_redirect,
	.cow_metrics =		dst_cow_metrics_generic,
	.destroy =		xfrm4_dst_destroy,
	.ifdown =		xfrm4_dst_ifdown,
	.local_out =		__ip_local_out,
	.gc_thresh =		INT_MAX,
};

static struct xfrm_policy_afinfo xfrm4_policy_afinfo = {
	.family = 		AF_INET,
	.dst_ops =		&xfrm4_dst_ops,
	.dst_lookup =		xfrm4_dst_lookup,
	.get_saddr =		xfrm4_get_saddr,
	.find_bundle = 		__xfrm4_find_bundle,
	.dst_ops =		&xfrm4_dst_ops_template,
	.dst_lookup =		xfrm4_dst_lookup,
	.get_saddr =		xfrm4_get_saddr,
	.decode_session =	_decode_session4,
	.get_tos =		xfrm4_get_tos,
	.init_path =		xfrm4_init_path,
	.fill_dst =		xfrm4_fill_dst,
	.blackhole_route =	ipv4_blackhole_route,
};

#ifdef CONFIG_SYSCTL
static struct ctl_table xfrm4_policy_table[] = {
	{
		.procname       = "xfrm4_gc_thresh",
		.data           = &init_net.xfrm.xfrm4_dst_ops.gc_thresh,
		.maxlen         = sizeof(int),
		.mode           = 0644,
		.proc_handler   = proc_dointvec,
	},
	{ }
};

static int __net_init xfrm4_net_sysctl_init(struct net *net)
{
	struct ctl_table *table;
	struct ctl_table_header *hdr;

	table = xfrm4_policy_table;
	if (!net_eq(net, &init_net)) {
		table = kmemdup(table, sizeof(xfrm4_policy_table), GFP_KERNEL);
		if (!table)
			goto err_alloc;

		table[0].data = &net->xfrm.xfrm4_dst_ops.gc_thresh;
	}

	hdr = register_net_sysctl(net, "net/ipv4", table);
	if (!hdr)
		goto err_reg;

	net->ipv4.xfrm4_hdr = hdr;
	return 0;

err_reg:
	if (!net_eq(net, &init_net))
		kfree(table);
err_alloc:
	return -ENOMEM;
}

static void __net_exit xfrm4_net_sysctl_exit(struct net *net)
{
	struct ctl_table *table;

	if (!net->ipv4.xfrm4_hdr)
		return;

	table = net->ipv4.xfrm4_hdr->ctl_table_arg;
	unregister_net_sysctl_table(net->ipv4.xfrm4_hdr);
	if (!net_eq(net, &init_net))
		kfree(table);
}
#else /* CONFIG_SYSCTL */
static int inline xfrm4_net_sysctl_init(struct net *net)
{
	return 0;
}

static void inline xfrm4_net_sysctl_exit(struct net *net)
{
}
#endif

static int __net_init xfrm4_net_init(struct net *net)
{
	int ret;

	memcpy(&net->xfrm.xfrm4_dst_ops, &xfrm4_dst_ops_template,
	       sizeof(xfrm4_dst_ops_template));
	ret = dst_entries_init(&net->xfrm.xfrm4_dst_ops);
	if (ret)
		return ret;

	ret = xfrm4_net_sysctl_init(net);
	if (ret)
		dst_entries_destroy(&net->xfrm.xfrm4_dst_ops);

	return ret;
}

static void __net_exit xfrm4_net_exit(struct net *net)
{
	xfrm4_net_sysctl_exit(net);
	dst_entries_destroy(&net->xfrm.xfrm4_dst_ops);
}

static struct pernet_operations __net_initdata xfrm4_net_ops = {
	.init	= xfrm4_net_init,
	.exit	= xfrm4_net_exit,
};

static void __init xfrm4_policy_init(void)
{
	xfrm_policy_register_afinfo(&xfrm4_policy_afinfo);
}

static void __exit xfrm4_policy_fini(void)
{
	xfrm_policy_unregister_afinfo(&xfrm4_policy_afinfo);
}

void __init xfrm4_init(void)
{
	xfrm4_state_init();
	xfrm4_policy_init();
	xfrm4_protocol_init();
	register_pernet_subsys(&xfrm4_net_ops);
}

