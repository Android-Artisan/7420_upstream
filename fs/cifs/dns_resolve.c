/*
 *  fs/cifs/dns_resolve.c
 *
 *   Copyright (c) 2007 Igor Mammedov
 *   Author(s): Igor Mammedov (niallain@gmail.com)
 *              Steve French (sfrench@us.ibm.com)
 *              Wang Lei (wang840925@gmail.com)
 *		David Howells (dhowells@redhat.com)
 *
 *   Contains the CIFS DFS upcall routines used for hostname to
 *   IP address translation.
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <keys/user-type.h>
#include <linux/slab.h>
#include <linux/dns_resolver.h>
#include "dns_resolve.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"

static int dns_resolver_instantiate(struct key *key, const void *data,
		size_t datalen)
{
	int rc = 0;
	char *ip;

	ip = kmalloc(datalen+1, GFP_KERNEL);
	if (!ip)
		return -ENOMEM;

	memcpy(ip, data, datalen);
	ip[datalen] = '\0';

	rcu_assign_pointer(key->payload.data, ip);

	return rc;
}

static void
dns_resolver_destroy(struct key *key)
{
	kfree(key->payload.data);
}

struct key_type key_type_dns_resolver = {
	.name        = "dns_resolver",
	.def_datalen = sizeof(struct in_addr),
	.describe    = user_describe,
	.instantiate = dns_resolver_instantiate,
	.destroy     = dns_resolver_destroy,
	.match       = user_match,
};

/* Checks if supplied name is IP address
 * returns:
 * 		1 - name is IP
 * 		0 - name is not IP
 */
static int is_ip(const char *name)
{
	int rc;
	struct sockaddr_in sin_server;
	struct sockaddr_in6 sin_server6;

	rc = cifs_inet_pton(AF_INET, name,
			&sin_server.sin_addr.s_addr);

	if (rc <= 0) {
		/* not ipv4 address, try ipv6 */
		rc = cifs_inet_pton(AF_INET6, name,
				&sin_server6.sin6_addr.in6_u);
		if (rc > 0)
			return 1;
	} else {
		return 1;
	}
	/* we failed translating address */
	return 0;
}

/* Resolves server name to ip address.
 * input:
 * 	unc - server UNC
 * output:
 * 	*ip_addr - pointer to server ip, caller responcible for freeing it.
 * return 0 on success
/**
 * dns_resolve_server_name_to_ip - Resolve UNC server name to ip address.
 * @unc: UNC path specifying the server (with '/' as delimiter)
 * @ip_addr: Where to return the IP address.
 *
 * The IP address will be returned in string form, and the caller is
 * responsible for freeing it.
 *
 * Returns length of result on success, -ve on error.
 */
int
dns_resolve_server_name_to_ip(const char *unc, char **ip_addr)
{
	int rc = -EAGAIN;
	struct key *rkey = ERR_PTR(-EAGAIN);
	char *name;
	char *data = NULL;
	int len;
	struct sockaddr_storage ss;
	const char *hostname, *sep;
	char *name;
	int len, rc;

	if (!ip_addr || !unc)
		return -EINVAL;

	/* search for server name delimiter */
	len = strlen(unc);
	if (len < 3) {
		cFYI(1, ("%s: unc is too short: %s", __func__, unc));
		return -EINVAL;
	}
	len -= 2;
	name = memchr(unc+2, '\\', len);
	if (!name) {
		cFYI(1, ("%s: probably server name is whole unc: %s",
					__func__, unc));
	} else {
		len = (name - unc) - 2/* leading // */;
	}

	name = kmalloc(len+1, GFP_KERNEL);
	if (!name) {
		rc = -ENOMEM;
		return rc;
	}
	memcpy(name, unc+2, len);
	name[len] = 0;

	if (is_ip(name)) {
		cFYI(1, ("%s: it is IP, skipping dns upcall: %s",
					__func__, name));
		data = name;
		goto skip_upcall;
	}

	rkey = request_key(&key_type_dns_resolver, name, "");
	if (!IS_ERR(rkey)) {
		data = rkey->payload.data;
	} else {
		cERROR(1, ("%s: unable to resolve: %s", __func__, name));
		goto out;
	}

skip_upcall:
	if (data) {
		len = strlen(data);
		*ip_addr = kmalloc(len+1, GFP_KERNEL);
		if (*ip_addr) {
			memcpy(*ip_addr, data, len);
			(*ip_addr)[len] = '\0';
			if (!IS_ERR(rkey))
				cFYI(1, ("%s: resolved: %s to %s", __func__,
							name,
							*ip_addr
					));
			rc = 0;
		} else {
			rc = -ENOMEM;
		}
		if (!IS_ERR(rkey))
			key_put(rkey);
	}

out:
	kfree(name);
	return rc;
}


	len = strlen(unc);
	if (len < 3) {
		cifs_dbg(FYI, "%s: unc is too short: %s\n", __func__, unc);
		return -EINVAL;
	}

	/* Discount leading slashes for cifs */
	len -= 2;
	hostname = unc + 2;

	/* Search for server name delimiter */
	sep = memchr(hostname, '/', len);
	if (sep)
		len = sep - hostname;
	else
		cifs_dbg(FYI, "%s: probably server name is whole unc: %s\n",
			 __func__, unc);

	/* Try to interpret hostname as an IPv4 or IPv6 address */
	rc = cifs_convert_address((struct sockaddr *)&ss, hostname, len);
	if (rc > 0)
		goto name_is_IP_address;

	/* Perform the upcall */
	rc = dns_query(NULL, hostname, len, NULL, ip_addr, NULL);
	if (rc < 0)
		cifs_dbg(FYI, "%s: unable to resolve: %*.*s\n",
			 __func__, len, len, hostname);
	else
		cifs_dbg(FYI, "%s: resolved: %*.*s to %s\n",
			 __func__, len, len, hostname, *ip_addr);
	return rc;

name_is_IP_address:
	name = kmalloc(len + 1, GFP_KERNEL);
	if (!name)
		return -ENOMEM;
	memcpy(name, hostname, len);
	name[len] = 0;
	cifs_dbg(FYI, "%s: unc is IP, skipping dns upcall: %s\n",
		 __func__, name);
	*ip_addr = name;
	return 0;
}
