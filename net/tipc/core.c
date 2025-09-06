/*
 * net/tipc/core.c: TIPC module code
 *
 * Copyright (c) 2003-2006, Ericsson AB
 * Copyright (c) 2005-2006, Wind River Systems
 * Copyright (c) 2003-2006, 2013, Ericsson AB
 * Copyright (c) 2005-2006, 2010-2013, Wind River Systems
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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/random.h>

#include "core.h"
#include "dbg.h"
#include "ref.h"
#include "net.h"
#include "user_reg.h"
#include "name_table.h"
#include "subscr.h"
#include "config.h"


#define TIPC_MOD_VER "1.6.4"

#ifndef CONFIG_TIPC_ZONES
#define CONFIG_TIPC_ZONES 3
#endif

#ifndef CONFIG_TIPC_CLUSTERS
#define CONFIG_TIPC_CLUSTERS 1
#endif

#ifndef CONFIG_TIPC_NODES
#define CONFIG_TIPC_NODES 255
#endif

#ifndef CONFIG_TIPC_SLAVE_NODES
#define CONFIG_TIPC_SLAVE_NODES 0
#endif

#ifndef CONFIG_TIPC_PORTS
#define CONFIG_TIPC_PORTS 8191
#endif

#ifndef CONFIG_TIPC_LOG
#define CONFIG_TIPC_LOG 0
#endif

/* global variables used by multiple sub-systems within TIPC */

int tipc_mode = TIPC_NOT_RUNNING;
int tipc_random;
atomic_t tipc_user_count = ATOMIC_INIT(0);

const char tipc_alphabet[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.";

/* configurable TIPC parameters */

u32 tipc_own_addr;
int tipc_max_zones;
int tipc_max_clusters;
int tipc_max_nodes;
int tipc_max_slaves;
int tipc_max_ports;
int tipc_max_subscriptions;
int tipc_max_publications;
int tipc_net_id;
int tipc_remote_management;


int tipc_get_mode(void)
{
	return tipc_mode;
}

/**
 * tipc_core_stop_net - shut down TIPC networking sub-systems
 */

void tipc_core_stop_net(void)
{
	tipc_eth_media_stop();
	tipc_net_stop();
}

/**
 * start_net - start TIPC networking sub-systems
 */

int tipc_core_start_net(unsigned long addr)
{
	int res;

	if ((res = tipc_net_start(addr)) ||
	    (res = tipc_eth_media_start())) {
		tipc_core_stop_net();
	}
	return res;
}

/**
 * tipc_core_stop - switch TIPC from SINGLE NODE to NOT RUNNING mode
 */

void tipc_core_stop(void)
{
	if (tipc_mode != TIPC_NODE_MODE)
		return;

	tipc_mode = TIPC_NOT_RUNNING;

	tipc_netlink_stop();
	tipc_handler_stop();
	tipc_cfg_stop();
	tipc_subscr_stop();
	tipc_reg_stop();
	tipc_nametbl_stop();
	tipc_ref_table_stop();
	tipc_socket_stop();
}

/**
 * tipc_core_start - switch TIPC from NOT RUNNING to SINGLE NODE mode
 */

int tipc_core_start(void)
{
	int res;

	if (tipc_mode != TIPC_NOT_RUNNING)
		return -ENOPROTOOPT;

	get_random_bytes(&tipc_random, sizeof(tipc_random));
	tipc_mode = TIPC_NODE_MODE;

	if ((res = tipc_handler_start()) ||
	    (res = tipc_ref_table_init(tipc_max_ports, tipc_random)) ||
	    (res = tipc_reg_start()) ||
	    (res = tipc_nametbl_init()) ||
	    (res = tipc_k_signal((Handler)tipc_subscr_start, 0)) ||
	    (res = tipc_k_signal((Handler)tipc_cfg_init, 0)) ||
	    (res = tipc_netlink_start()) ||
	    (res = tipc_socket_init())) {
		tipc_core_stop();
	}
	return res;
}


static int __init tipc_init(void)
{
	int res;

	tipc_log_resize(CONFIG_TIPC_LOG);
	info("Activated (version " TIPC_MOD_VER
	     " compiled " __DATE__ " " __TIME__ ")\n");

	tipc_own_addr = 0;
	tipc_remote_management = 1;
	tipc_max_publications = 10000;
	tipc_max_subscriptions = 2000;
	tipc_max_ports = delimit(CONFIG_TIPC_PORTS, 127, 65536);
	tipc_max_zones = delimit(CONFIG_TIPC_ZONES, 1, 255);
	tipc_max_clusters = delimit(CONFIG_TIPC_CLUSTERS, 1, 1);
	tipc_max_nodes = delimit(CONFIG_TIPC_NODES, 8, 2047);
	tipc_max_slaves = delimit(CONFIG_TIPC_SLAVE_NODES, 0, 2047);
	tipc_net_id = 4711;

	if ((res = tipc_core_start()))
		err("Unable to start in single node mode\n");
	else
		info("Started in single node mode\n");
	return res;
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "core.h"
#include "name_table.h"
#include "subscr.h"
#include "bearer.h"
#include "net.h"
#include "socket.h"
#include "bcast.h"

#include <linux/module.h>

/* configurable TIPC parameters */
int tipc_net_id __read_mostly;
int sysctl_tipc_rmem[3] __read_mostly;	/* min/default/max */

static int __net_init tipc_init_net(struct net *net)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	int err;

	tn->net_id = 4711;
	tn->own_addr = 0;
	get_random_bytes(&tn->random, sizeof(int));
	INIT_LIST_HEAD(&tn->node_list);
	spin_lock_init(&tn->node_list_lock);

	err = tipc_sk_rht_init(net);
	if (err)
		goto out_sk_rht;

	err = tipc_nametbl_init(net);
	if (err)
		goto out_nametbl;

	INIT_LIST_HEAD(&tn->dist_queue);

	err = tipc_bcast_init(net);
	if (err)
		goto out_bclink;

	return 0;

out_bclink:
	tipc_nametbl_stop(net);
out_nametbl:
	tipc_sk_rht_destroy(net);
out_sk_rht:
	return err;
}

static void __net_exit tipc_exit_net(struct net *net)
{
	tipc_net_stop(net);

	/* Make sure the tipc_net_finalize_work stopped
	 * before releasing the resources.
	 */
	flush_scheduled_work();
	tipc_bcast_stop(net);
	tipc_nametbl_stop(net);
	tipc_sk_rht_destroy(net);
}

static struct pernet_operations tipc_net_ops = {
	.init = tipc_init_net,
	.exit = tipc_exit_net,
	.id   = &tipc_net_id,
	.size = sizeof(struct tipc_net),
};

static struct pernet_operations tipc_topsrv_net_ops = {
	.init = tipc_topsrv_init_net,
	.exit = tipc_topsrv_exit_net,
};

static int __init tipc_init(void)
{
	int err;

	pr_info("Activated (version " TIPC_MOD_VER ")\n");

	sysctl_tipc_rmem[0] = TIPC_CONN_OVERLOAD_LIMIT >> 4 <<
			      TIPC_LOW_IMPORTANCE;
	sysctl_tipc_rmem[1] = TIPC_CONN_OVERLOAD_LIMIT >> 4 <<
			      TIPC_CRITICAL_IMPORTANCE;
	sysctl_tipc_rmem[2] = TIPC_CONN_OVERLOAD_LIMIT;

	err = tipc_register_sysctl();
	if (err)
		goto out_sysctl;

	err = register_pernet_device(&tipc_net_ops);
	if (err)
		goto out_pernet;

	err = tipc_socket_init();
	if (err)
		goto out_socket;

	err = register_pernet_device(&tipc_topsrv_net_ops);
	if (err)
		goto out_pernet_topsrv;

	err = tipc_bearer_setup();
	if (err)
		goto out_bearer;

	err = tipc_netlink_start();
	if (err)
		goto out_netlink;

	err = tipc_netlink_compat_start();
	if (err)
		goto out_netlink_compat;

	pr_info("Started in single node mode\n");
	return 0;

out_netlink_compat:
	tipc_netlink_stop();
out_netlink:
	tipc_bearer_cleanup();
out_bearer:
	unregister_pernet_device(&tipc_topsrv_net_ops);
out_pernet_topsrv:
	tipc_socket_stop();
out_socket:
	unregister_pernet_device(&tipc_net_ops);
out_pernet:
	tipc_unregister_sysctl();
out_sysctl:
	pr_err("Unable to start in single node mode\n");
	return err;
}

static void __exit tipc_exit(void)
{
	tipc_core_stop_net();
	tipc_core_stop();
	info("Deactivated\n");
	tipc_log_resize(0);
	tipc_bearer_cleanup();
	unregister_pernet_subsys(&tipc_net_ops);
	tipc_netlink_stop();
	tipc_netlink_compat_stop();
	tipc_netlink_stop();
	tipc_bearer_cleanup();
	unregister_pernet_device(&tipc_topsrv_net_ops);
	tipc_socket_stop();
	unregister_pernet_device(&tipc_net_ops);
	tipc_unregister_sysctl();

	pr_info("Deactivated\n");
}

module_init(tipc_init);
module_exit(tipc_exit);

MODULE_DESCRIPTION("TIPC: Transparent Inter Process Communication");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(TIPC_MOD_VER);

/* Native TIPC API for kernel-space applications (see tipc.h) */

EXPORT_SYMBOL(tipc_attach);
EXPORT_SYMBOL(tipc_detach);
EXPORT_SYMBOL(tipc_get_addr);
EXPORT_SYMBOL(tipc_get_mode);
EXPORT_SYMBOL(tipc_createport);
EXPORT_SYMBOL(tipc_deleteport);
EXPORT_SYMBOL(tipc_ownidentity);
EXPORT_SYMBOL(tipc_portimportance);
EXPORT_SYMBOL(tipc_set_portimportance);
EXPORT_SYMBOL(tipc_portunreliable);
EXPORT_SYMBOL(tipc_set_portunreliable);
EXPORT_SYMBOL(tipc_portunreturnable);
EXPORT_SYMBOL(tipc_set_portunreturnable);
EXPORT_SYMBOL(tipc_publish);
EXPORT_SYMBOL(tipc_withdraw);
EXPORT_SYMBOL(tipc_connect2port);
EXPORT_SYMBOL(tipc_disconnect);
EXPORT_SYMBOL(tipc_shutdown);
EXPORT_SYMBOL(tipc_isconnected);
EXPORT_SYMBOL(tipc_peer);
EXPORT_SYMBOL(tipc_ref_valid);
EXPORT_SYMBOL(tipc_send);
EXPORT_SYMBOL(tipc_send_buf);
EXPORT_SYMBOL(tipc_send2name);
EXPORT_SYMBOL(tipc_forward2name);
EXPORT_SYMBOL(tipc_send_buf2name);
EXPORT_SYMBOL(tipc_forward_buf2name);
EXPORT_SYMBOL(tipc_send2port);
EXPORT_SYMBOL(tipc_forward2port);
EXPORT_SYMBOL(tipc_send_buf2port);
EXPORT_SYMBOL(tipc_forward_buf2port);
EXPORT_SYMBOL(tipc_multicast);
/* EXPORT_SYMBOL(tipc_multicast_buf); not available yet */
EXPORT_SYMBOL(tipc_ispublished);
EXPORT_SYMBOL(tipc_available_nodes);

/* TIPC API for external bearers (see tipc_bearer.h) */

EXPORT_SYMBOL(tipc_block_bearer);
EXPORT_SYMBOL(tipc_continue);
EXPORT_SYMBOL(tipc_disable_bearer);
EXPORT_SYMBOL(tipc_enable_bearer);
EXPORT_SYMBOL(tipc_recv_msg);
EXPORT_SYMBOL(tipc_register_media);

/* TIPC API for external APIs (see tipc_port.h) */

EXPORT_SYMBOL(tipc_createport_raw);
EXPORT_SYMBOL(tipc_reject_msg);
EXPORT_SYMBOL(tipc_send_buf_fast);
EXPORT_SYMBOL(tipc_acknowledge);
EXPORT_SYMBOL(tipc_get_port);
EXPORT_SYMBOL(tipc_get_handle);

