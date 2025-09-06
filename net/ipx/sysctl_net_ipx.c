/* -*- linux-c -*-
 * sysctl_net_ipx.c: sysctl interface to net IPX subsystem.
 *
 * Begun April 1, 1996, Mike Shaver.
 * Added /proc/sys/net/ipx directory entry (empty =) ). [MS]
 * Added /proc/sys/net/ipx/ipx_pprop_broadcasting - acme March 4, 2001
 */

#include <linux/mm.h>
#include <linux/sysctl.h>
#include <net/net_namespace.h>
#include <net/ipx.h>

#ifndef CONFIG_SYSCTL
#error This file should not be compiled without CONFIG_SYSCTL defined
#endif

/* From af_ipx.c */
extern int sysctl_ipx_pprop_broadcasting;

static struct ctl_table ipx_table[] = {
	{
		.ctl_name	= NET_IPX_PPROP_BROADCASTING,
static struct ctl_table ipx_table[] = {
	{
		.procname	= "ipx_pprop_broadcasting",
		.data		= &sysctl_ipx_pprop_broadcasting,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{ 0 },
};

static struct ctl_path ipx_path[] = {
	{ .procname = "net", .ctl_name = CTL_NET, },
	{ .procname = "ipx", .ctl_name = NET_IPX, },
	{ }
		.proc_handler	= proc_dointvec,
	},
	{ },
};

static struct ctl_table_header *ipx_table_header;

void ipx_register_sysctl(void)
{
	ipx_table_header = register_sysctl_paths(ipx_path, ipx_table);
	ipx_table_header = register_net_sysctl(&init_net, "net/ipx", ipx_table);
}

void ipx_unregister_sysctl(void)
{
	unregister_sysctl_table(ipx_table_header);
	unregister_net_sysctl_table(ipx_table_header);
}
