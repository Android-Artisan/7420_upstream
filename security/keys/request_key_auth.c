/* request_key_auth.c: request key authorisation controlling key def
/* Request key authorisation token key definition.
 *
 * Copyright (C) 2005 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * See Documentation/keys-request-key.txt
 * See Documentation/security/keys-request-key.txt
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/err.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include "internal.h"

static int request_key_auth_instantiate(struct key *, const void *, size_t);
#include <keys/user-type.h>

static int request_key_auth_preparse(struct key_preparsed_payload *);
static void request_key_auth_free_preparse(struct key_preparsed_payload *);
static int request_key_auth_instantiate(struct key *,
					struct key_preparsed_payload *);
static void request_key_auth_describe(const struct key *, struct seq_file *);
static void request_key_auth_revoke(struct key *);
static void request_key_auth_destroy(struct key *);
static long request_key_auth_read(const struct key *, char __user *, size_t);

/*
 * the request-key authorisation key type definition
 * The request-key authorisation key type definition.
 */
struct key_type key_type_request_key_auth = {
	.name		= ".request_key_auth",
	.def_datalen	= sizeof(struct request_key_auth),
	.preparse	= request_key_auth_preparse,
	.free_preparse	= request_key_auth_free_preparse,
	.instantiate	= request_key_auth_instantiate,
	.describe	= request_key_auth_describe,
	.revoke		= request_key_auth_revoke,
	.destroy	= request_key_auth_destroy,
	.read		= request_key_auth_read,
};

/*****************************************************************************/
/*
 * instantiate a request-key authorisation key
 */
static int request_key_auth_instantiate(struct key *key,
					const void *data,
					size_t datalen)
{
	key->payload.data = (struct request_key_auth *) data;
	return 0;

} /* end request_key_auth_instantiate() */

/*****************************************************************************/
/*
 * reading a request-key authorisation key retrieves the callout information
static int request_key_auth_preparse(struct key_preparsed_payload *prep)
{
	return 0;
}

static void request_key_auth_free_preparse(struct key_preparsed_payload *prep)
{
}

/*
 * Instantiate a request-key authorisation key.
 */
static int request_key_auth_instantiate(struct key *key,
					struct key_preparsed_payload *prep)
{
	key->payload.data[0] = (struct request_key_auth *)prep->data;
	return 0;
}

/*
 * Describe an authorisation token.
 */
static void request_key_auth_describe(const struct key *key,
				      struct seq_file *m)
{
	struct request_key_auth *rka = key->payload.data;

	seq_puts(m, "key:");
	seq_puts(m, key->description);
	seq_printf(m, " pid:%d ci:%zu", rka->pid, rka->callout_len);

} /* end request_key_auth_describe() */

/*****************************************************************************/
/*
 * read the callout_info data
	struct request_key_auth *rka = key->payload.data[0];

	if (!rka)
		return;

	seq_puts(m, "key:");
	seq_puts(m, key->description);
	if (key_is_positive(key))
		seq_printf(m, " pid:%d ci:%zu", rka->pid, rka->callout_len);
}

/*
 * Read the callout_info data (retrieves the callout information).
 * - the key's semaphore is read-locked
 */
static long request_key_auth_read(const struct key *key,
				  char __user *buffer, size_t buflen)
{
	struct request_key_auth *rka = key->payload.data;
	struct request_key_auth *rka = key->payload.data[0];
	size_t datalen;
	long ret;

	if (!rka)
		return -EKEYREVOKED;

	datalen = rka->callout_len;
	ret = datalen;

	/* we can return the data as is */
	if (buffer && buflen > 0) {
		if (buflen > datalen)
			buflen = datalen;

		if (copy_to_user(buffer, rka->callout_info, buflen) != 0)
			ret = -EFAULT;
	}

	return ret;

} /* end request_key_auth_read() */

/*****************************************************************************/
/*
 * handle revocation of an authorisation token key
 * - called with the key sem write-locked
 */
static void request_key_auth_revoke(struct key *key)
{
	struct request_key_auth *rka = key->payload.data;

	kenter("{%d}", key->serial);

	if (rka->context) {
		put_task_struct(rka->context);
		rka->context = NULL;
	}

} /* end request_key_auth_revoke() */

/*****************************************************************************/
/*
 * destroy an instantiation authorisation token key
 */
static void request_key_auth_destroy(struct key *key)
{
	struct request_key_auth *rka = key->payload.data;

	kenter("{%d}", key->serial);

	if (rka->context) {
		put_task_struct(rka->context);
		rka->context = NULL;
	}

	key_put(rka->target_key);
	kfree(rka->callout_info);
	kfree(rka);

} /* end request_key_auth_destroy() */

/*****************************************************************************/
/*
 * create an authorisation token for /sbin/request-key or whoever to gain
 * access to the caller's security data
 */
struct key *request_key_auth_new(struct key *target, const void *callout_info,
				 size_t callout_len)
{
	struct request_key_auth *rka, *irka;
}

/*
 * Handle revocation of an authorisation token key.
 *
 * Called with the key sem write-locked.
 */
static void request_key_auth_revoke(struct key *key)
{
	struct request_key_auth *rka = key->payload.data[0];

	kenter("{%d}", key->serial);

	if (rka->cred) {
		put_cred(rka->cred);
		rka->cred = NULL;
	}
}

/*
 * Destroy an instantiation authorisation token key.
 */
static void request_key_auth_destroy(struct key *key)
{
	struct request_key_auth *rka = key->payload.data[0];

	kenter("{%d}", key->serial);

	if (rka->cred) {
		put_cred(rka->cred);
		rka->cred = NULL;
	}

	key_put(rka->target_key);
	key_put(rka->dest_keyring);
	kfree(rka->callout_info);
	kfree(rka);
}

/*
 * Create an authorisation token for /sbin/request-key or whoever to gain
 * access to the caller's security data.
 */
struct key *request_key_auth_new(struct key *target, const void *callout_info,
				 size_t callout_len, struct key *dest_keyring)
{
	struct request_key_auth *rka, *irka;
	const struct cred *cred = current->cred;
	struct key *authkey = NULL;
	char desc[20];
	int ret;

	kenter("%d,", target->serial);

	/* allocate a auth record */
	rka = kmalloc(sizeof(*rka), GFP_KERNEL);
	if (!rka) {
		kleave(" = -ENOMEM");
		return ERR_PTR(-ENOMEM);
	}
	rka->callout_info = kmalloc(callout_len, GFP_KERNEL);
	if (!rka->callout_info) {
		kleave(" = -ENOMEM");
		kfree(rka);
		return ERR_PTR(-ENOMEM);
	}

	/* see if the calling process is already servicing the key request of
	 * another process */
	if (current->request_key_auth) {
		/* it is - use that instantiation context here too */
		down_read(&current->request_key_auth->sem);

		/* if the auth key has been revoked, then the key we're
		 * servicing is already instantiated */
		if (test_bit(KEY_FLAG_REVOKED,
			     &current->request_key_auth->flags))
			goto auth_key_revoked;

		irka = current->request_key_auth->payload.data;
		rka->context = irka->context;
		rka->pid = irka->pid;
		get_task_struct(rka->context);

		up_read(&current->request_key_auth->sem);
	}
	else {
		/* it isn't - use this process as the context */
		rka->context = current;
		rka->pid = current->pid;
		get_task_struct(rka->context);
	}

	rka->target_key = key_get(target);
	if (cred->request_key_auth) {
		/* it is - use that instantiation context here too */
		down_read(&cred->request_key_auth->sem);

		/* if the auth key has been revoked, then the key we're
		 * servicing is already instantiated */
		if (test_bit(KEY_FLAG_REVOKED, &cred->request_key_auth->flags))
			goto auth_key_revoked;

		irka = cred->request_key_auth->payload.data[0];
		rka->cred = get_cred(irka->cred);
		rka->pid = irka->pid;

		up_read(&cred->request_key_auth->sem);
	}
	else {
		/* it isn't - use this process as the context */
		rka->cred = get_cred(cred);
		rka->pid = current->pid;
	}

	rka->target_key = key_get(target);
	rka->dest_keyring = key_get(dest_keyring);
	memcpy(rka->callout_info, callout_info, callout_len);
	rka->callout_len = callout_len;

	/* allocate the auth key */
	sprintf(desc, "%x", target->serial);

	authkey = key_alloc(&key_type_request_key_auth, desc,
			    current->fsuid, current->fsgid, current,
			    cred->fsuid, cred->fsgid, cred,
			    KEY_POS_VIEW | KEY_POS_READ | KEY_POS_SEARCH |
			    KEY_USR_VIEW, KEY_ALLOC_NOT_IN_QUOTA);
	if (IS_ERR(authkey)) {
		ret = PTR_ERR(authkey);
		goto error_alloc;
	}

	/* construct and attach to the keyring */
	/* construct the auth key */
	ret = key_instantiate_and_link(authkey, rka, 0, NULL, NULL);
	if (ret < 0)
		goto error_inst;

	kleave(" = {%d}", authkey->serial);
	return authkey;

auth_key_revoked:
	up_read(&current->request_key_auth->sem);
	kleave(" = {%d,%d}", authkey->serial, atomic_read(&authkey->usage));
	return authkey;

auth_key_revoked:
	up_read(&cred->request_key_auth->sem);
	kfree(rka->callout_info);
	kfree(rka);
	kleave("= -EKEYREVOKED");
	return ERR_PTR(-EKEYREVOKED);

error_inst:
	key_revoke(authkey);
	key_put(authkey);
error_alloc:
	key_put(rka->target_key);
	key_put(rka->dest_keyring);
	kfree(rka->callout_info);
	kfree(rka);
	kleave("= %d", ret);
	return ERR_PTR(ret);

} /* end request_key_auth_new() */

/*****************************************************************************/
/*
 * see if an authorisation key is associated with a particular key
 */
static int key_get_instantiation_authkey_match(const struct key *key,
					       const void *_id)
{
	struct request_key_auth *rka = key->payload.data;
	key_serial_t id = (key_serial_t)(unsigned long) _id;

	return rka->target_key->serial == id;

} /* end key_get_instantiation_authkey_match() */

/*****************************************************************************/
/*
 * get the authorisation key for instantiation of a specific key if attached to
 * the current process's keyrings
 * - this key is inserted into a keyring and that is set as /sbin/request-key's
 *   session keyring
 * - a target_id of zero specifies any valid token
 */
struct key *key_get_instantiation_authkey(key_serial_t target_id)
{
	struct key *authkey;
	key_ref_t authkey_ref;

	authkey_ref = search_process_keyrings(
		&key_type_request_key_auth,
		(void *) (unsigned long) target_id,
		key_get_instantiation_authkey_match,
		current);

	if (IS_ERR(authkey_ref)) {
		authkey = ERR_CAST(authkey_ref);
}

/*
 * Search the current process's keyrings for the authorisation key for
 * instantiation of a key.
 */
struct key *key_get_instantiation_authkey(key_serial_t target_id)
{
	char description[16];
	struct keyring_search_context ctx = {
		.index_key.type		= &key_type_request_key_auth,
		.index_key.description	= description,
		.cred			= current_cred(),
		.match_data.cmp		= key_default_cmp,
		.match_data.raw_data	= description,
		.match_data.lookup_type	= KEYRING_SEARCH_LOOKUP_DIRECT,
		.flags			= KEYRING_SEARCH_DO_STATE_CHECK,
	};
	struct key *authkey;
	key_ref_t authkey_ref;

	ctx.index_key.desc_len = sprintf(description, "%x", target_id);

	authkey_ref = search_process_keyrings(&ctx);

	if (IS_ERR(authkey_ref)) {
		authkey = ERR_CAST(authkey_ref);
		if (authkey == ERR_PTR(-EAGAIN))
			authkey = ERR_PTR(-ENOKEY);
		goto error;
	}

	authkey = key_ref_to_ptr(authkey_ref);
	if (test_bit(KEY_FLAG_REVOKED, &authkey->flags)) {
		key_put(authkey);
		authkey = ERR_PTR(-EKEYREVOKED);
	}

error:
	return authkey;

} /* end key_get_instantiation_authkey() */
}
