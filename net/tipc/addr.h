/*
 * net/tipc/addr.h: Include file for TIPC address utility routines
 *
 * Copyright (c) 2000-2006, Ericsson AB
 * Copyright (c) 2004-2005, Wind River Systems
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

#ifndef _TIPC_ADDR_H
#define _TIPC_ADDR_H

static inline u32 own_node(void)
{
	return tipc_node(tipc_own_addr);
}

static inline u32 own_cluster(void)
{
	return tipc_cluster(tipc_own_addr);
}

static inline u32 own_zone(void)
{
	return tipc_zone(tipc_own_addr);
}

static inline int in_own_cluster(u32 addr)
{
	return !((addr ^ tipc_own_addr) >> 12);
}

static inline int is_slave(u32 addr)
{
	return addr & 0x800;
}

static inline int may_route(u32 addr)
{
	return(addr ^ tipc_own_addr) >> 11;
}

static inline int in_scope(u32 domain, u32 addr)
{
	if (!domain || (domain == addr))
		return 1;
	if (domain == (addr & 0xfffff000u)) /* domain <Z.C.0> */
		return 1;
	if (domain == (addr & 0xff000000u)) /* domain <Z.0.0> */
		return 1;
	return 0;
}

/**
 * addr_scope - convert message lookup domain to equivalent 2-bit scope value
 */

static inline int addr_scope(u32 domain)
{
	if (likely(!domain))
		return TIPC_ZONE_SCOPE;
	if (tipc_node(domain))
		return TIPC_NODE_SCOPE;
	if (tipc_cluster(domain))
		return TIPC_CLUSTER_SCOPE;
	return TIPC_ZONE_SCOPE;
}

/**
 * addr_domain - convert 2-bit scope value to equivalent message lookup domain
 *
 * Needed when address of a named message must be looked up a second time
 * after a network hop.
 */

static inline int addr_domain(int sc)
{
	if (likely(sc == TIPC_NODE_SCOPE))
		return tipc_own_addr;
	if (sc == TIPC_CLUSTER_SCOPE)
		return tipc_addr(tipc_zone(tipc_own_addr),
				 tipc_cluster(tipc_own_addr), 0);
	return tipc_addr(tipc_zone(tipc_own_addr), 0, 0);
}

static inline char *addr_string_fill(char *string, u32 addr)
{
	snprintf(string, 16, "<%u.%u.%u>",
		 tipc_zone(addr), tipc_cluster(addr), tipc_node(addr));
	return string;
}

int tipc_addr_domain_valid(u32);
int tipc_addr_node_valid(u32 addr);

#include <linux/types.h>
#include <linux/tipc.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>
#include "core.h"

#define TIPC_ZONE_MASK		0xff000000u
#define TIPC_CLUSTER_MASK	0xfffff000u

static inline u32 tipc_own_addr(struct net *net)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);

	return tn->own_addr;
}

static inline u32 tipc_zone_mask(u32 addr)
{
	return addr & TIPC_ZONE_MASK;
}

static inline u32 tipc_cluster_mask(u32 addr)
{
	return addr & TIPC_CLUSTER_MASK;
}

u32 tipc_own_addr(struct net *net);
int in_own_cluster(struct net *net, u32 addr);
int in_own_cluster_exact(struct net *net, u32 addr);
int in_own_node(struct net *net, u32 addr);
u32 addr_domain(struct net *net, u32 sc);
int tipc_addr_domain_valid(u32);
int tipc_addr_node_valid(u32 addr);
int tipc_in_scope(u32 domain, u32 addr);
int tipc_addr_scope(u32 domain);
char *tipc_addr_string_fill(char *string, u32 addr);
#endif
