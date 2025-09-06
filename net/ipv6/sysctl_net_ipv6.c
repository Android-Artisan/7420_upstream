/*
 * sysctl_net_ipv6.c: sysctl interface to net IPV6 subsystem.
 *
 * Changes:
 * YOSHIFUJI Hideaki @USAGI:	added icmp sysctl table.
 */

#include <linux/mm.h>
#include <linux/sysctl.h>
#include <linux/in6.h>
#include <linux/ipv6.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <net/ndisc.h>
#include <net/ipv6.h>
#include <net/addrconf.h>
#include <net/inet_frag.h>

static ctl_table ipv6_table_template[] = {
	{
		.ctl_name	= NET_IPV6_ROUTE,
		.procname	= "route",
		.maxlen		= 0,
		.mode		= 0555,
		.child		= ipv6_route_table_template
	},
	{
		.ctl_name	= NET_IPV6_ICMP,
		.procname	= "icmp",
		.maxlen		= 0,
		.mode		= 0555,
		.child		= ipv6_icmp_table_template
	},
	{
		.ctl_name	= NET_IPV6_BINDV6ONLY,
static int one = 1;
static int auto_flowlabels_min;
static int auto_flowlabels_max = IP6_AUTO_FLOW_LABEL_MAX;


static struct ctl_table ipv6_table_template[] = {
	{
		.procname	= "bindv6only",
		.data		= &init_net.ipv6.sysctl.bindv6only,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{ .ctl_name = 0 }
};

static ctl_table ipv6_table[] = {
	{
		.ctl_name	= NET_IPV6_MLD_MAX_MSF,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "anycast_src_echo_reply",
		.data		= &init_net.ipv6.sysctl.anycast_src_echo_reply,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "flowlabel_consistency",
		.data		= &init_net.ipv6.sysctl.flowlabel_consistency,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "auto_flowlabels",
		.data		= &init_net.ipv6.sysctl.auto_flowlabels,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &auto_flowlabels_min,
		.extra2		= &auto_flowlabels_max
	},
	{
		.procname	= "fwmark_reflect",
		.data		= &init_net.ipv6.sysctl.fwmark_reflect,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "idgen_retries",
		.data		= &init_net.ipv6.sysctl.idgen_retries,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "idgen_delay",
		.data		= &init_net.ipv6.sysctl.idgen_delay,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{
		.procname	= "flowlabel_state_ranges",
		.data		= &init_net.ipv6.sysctl.flowlabel_state_ranges,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "ip_nonlocal_bind",
		.data		= &init_net.ipv6.sysctl.ip_nonlocal_bind,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{ }
};

static struct ctl_table ipv6_rotable[] = {
	{
		.procname	= "mld_max_msf",
		.data		= &sysctl_mld_max_msf,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{ .ctl_name = 0 }
};

struct ctl_path net_ipv6_ctl_path[] = {
	{ .procname = "net", .ctl_name = CTL_NET, },
	{ .procname = "ipv6", .ctl_name = NET_IPV6, },
	{ },
};
EXPORT_SYMBOL_GPL(net_ipv6_ctl_path);

static int ipv6_sysctl_net_init(struct net *net)
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "mld_qrv",
		.data		= &sysctl_mld_qrv,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &one
	},
	{ }
};

static int __net_init ipv6_sysctl_net_init(struct net *net)
{
	struct ctl_table *ipv6_table;
	struct ctl_table *ipv6_route_table;
	struct ctl_table *ipv6_icmp_table;
	int err;

	err = -ENOMEM;
	ipv6_table = kmemdup(ipv6_table_template, sizeof(ipv6_table_template),
			     GFP_KERNEL);
	if (!ipv6_table)
		goto out;
	ipv6_table[0].data = &net->ipv6.sysctl.bindv6only;
	ipv6_table[1].data = &net->ipv6.sysctl.anycast_src_echo_reply;
	ipv6_table[2].data = &net->ipv6.sysctl.flowlabel_consistency;
	ipv6_table[3].data = &net->ipv6.sysctl.auto_flowlabels;
	ipv6_table[4].data = &net->ipv6.sysctl.fwmark_reflect;
	ipv6_table[5].data = &net->ipv6.sysctl.idgen_retries;
	ipv6_table[6].data = &net->ipv6.sysctl.idgen_delay;
	ipv6_table[7].data = &net->ipv6.sysctl.flowlabel_state_ranges;
	ipv6_table[8].data = &net->ipv6.sysctl.ip_nonlocal_bind;

	ipv6_route_table = ipv6_route_sysctl_init(net);
	if (!ipv6_route_table)
		goto out_ipv6_table;
	ipv6_table[0].child = ipv6_route_table;

	ipv6_icmp_table = ipv6_icmp_sysctl_init(net);
	if (!ipv6_icmp_table)
		goto out_ipv6_route_table;
	ipv6_table[1].child = ipv6_icmp_table;

	ipv6_table[2].data = &net->ipv6.sysctl.bindv6only;

	net->ipv6.sysctl.table = register_net_sysctl_table(net, net_ipv6_ctl_path,
							   ipv6_table);
	if (!net->ipv6.sysctl.table)
		goto out_ipv6_icmp_table;

	err = 0;
out:
	return err;


	net->ipv6.sysctl.hdr = register_net_sysctl(net, "net/ipv6", ipv6_table);
	if (!net->ipv6.sysctl.hdr)
		goto out_ipv6_icmp_table;

	net->ipv6.sysctl.route_hdr =
		register_net_sysctl(net, "net/ipv6/route", ipv6_route_table);
	if (!net->ipv6.sysctl.route_hdr)
		goto out_unregister_ipv6_table;

	net->ipv6.sysctl.icmp_hdr =
		register_net_sysctl(net, "net/ipv6/icmp", ipv6_icmp_table);
	if (!net->ipv6.sysctl.icmp_hdr)
		goto out_unregister_route_table;

	err = 0;
out:
	return err;
out_unregister_route_table:
	unregister_net_sysctl_table(net->ipv6.sysctl.route_hdr);
out_unregister_ipv6_table:
	unregister_net_sysctl_table(net->ipv6.sysctl.hdr);
out_ipv6_icmp_table:
	kfree(ipv6_icmp_table);
out_ipv6_route_table:
	kfree(ipv6_route_table);
out_ipv6_table:
	kfree(ipv6_table);
	goto out;
}

static void ipv6_sysctl_net_exit(struct net *net)
static void __net_exit ipv6_sysctl_net_exit(struct net *net)
{
	struct ctl_table *ipv6_table;
	struct ctl_table *ipv6_route_table;
	struct ctl_table *ipv6_icmp_table;

	ipv6_table = net->ipv6.sysctl.table->ctl_table_arg;
	ipv6_route_table = ipv6_table[0].child;
	ipv6_icmp_table = ipv6_table[1].child;

	unregister_net_sysctl_table(net->ipv6.sysctl.table);
	ipv6_table = net->ipv6.sysctl.hdr->ctl_table_arg;
	ipv6_route_table = net->ipv6.sysctl.route_hdr->ctl_table_arg;
	ipv6_icmp_table = net->ipv6.sysctl.icmp_hdr->ctl_table_arg;

	unregister_net_sysctl_table(net->ipv6.sysctl.icmp_hdr);
	unregister_net_sysctl_table(net->ipv6.sysctl.route_hdr);
	unregister_net_sysctl_table(net->ipv6.sysctl.hdr);

	kfree(ipv6_table);
	kfree(ipv6_route_table);
	kfree(ipv6_icmp_table);
}

static struct pernet_operations ipv6_sysctl_net_ops = {
	.init = ipv6_sysctl_net_init,
	.exit = ipv6_sysctl_net_exit,
};

static struct ctl_table_header *ip6_header;

int ipv6_sysctl_register(void)
{
	int err = -ENOMEM;;

	ip6_header = register_net_sysctl_rotable(net_ipv6_ctl_path, ipv6_table);
	if (ip6_header == NULL)
	int err = -ENOMEM;

	ip6_header = register_net_sysctl(&init_net, "net/ipv6", ipv6_rotable);
	if (!ip6_header)
		goto out;

	err = register_pernet_subsys(&ipv6_sysctl_net_ops);
	if (err)
		goto err_pernet;
out:
	return err;

err_pernet:
	unregister_net_sysctl_table(ip6_header);
	goto out;
}

void ipv6_sysctl_unregister(void)
{
	unregister_net_sysctl_table(ip6_header);
	unregister_pernet_subsys(&ipv6_sysctl_net_ops);
}

static struct ctl_table_header *ip6_base;

int ipv6_static_sysctl_register(void)
{
	static struct ctl_table empty[1];
	ip6_base = register_sysctl_paths(net_ipv6_ctl_path, empty);
	if (ip6_base == NULL)
		return -ENOMEM;
	return 0;
}

void ipv6_static_sysctl_unregister(void)
{
	unregister_net_sysctl_table(ip6_base);
}
