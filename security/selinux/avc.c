/*
 * Implementation of the kernel access vector cache (AVC).
 *
 * Authors:  Stephen Smalley, <sds@epoch.ncsc.mil>
 *	     James Morris <jmorris@redhat.com>
 *
 * Update:   KaiGai, Kohei <kaigai@ak.jp.nec.com>
 *	Replaced the avc_lock spinlock by RCU.
 *
 * Copyright (C) 2003 Red Hat, Inc., James Morris <jmorris@redhat.com>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License version 2,
 *	as published by the Free Software Foundation.
 */
#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/percpu.h>
#include <linux/list.h>
#include <net/sock.h>
#include <linux/un.h>
#include <net/af_unix.h>
#include <linux/ip.h>
#include <linux/audit.h>
#include <linux/ipv6.h>
#include <net/ipv6.h>
#include "avc.h"
#include "avc_ss.h"

static const struct av_perm_to_string av_perm_to_string[] = {
#define S_(c, v, s) { c, v, s },
#include "av_perm_to_string.h"
#undef S_
};

static const char *class_to_string[] = {
#define S_(s) s,
#include "class_to_string.h"
#undef S_
};

#define TB_(s) static const char *s[] = {
#define TE_(s) };
#define S_(s) s,
#include "common_perm_to_string.h"
#undef TB_
#undef TE_
#undef S_

static const struct av_inherit av_inherit[] = {
#define S_(c, i, b) { c, common_##i##_perm_to_string, b },
#include "av_inherit.h"
#undef S_
};

const struct selinux_class_perm selinux_class_perm = {
	av_perm_to_string,
	ARRAY_SIZE(av_perm_to_string),
	class_to_string,
	ARRAY_SIZE(class_to_string),
	av_inherit,
	ARRAY_SIZE(av_inherit)
};
#include "classmap.h"

#define AVC_CACHE_SLOTS			512
#define AVC_DEF_CACHE_THRESHOLD		512
#define AVC_CACHE_RECLAIM		16

#ifdef CONFIG_SECURITY_SELINUX_AVC_STATS
#define avc_cache_stats_incr(field)				\
do {								\
	per_cpu(avc_cache_stats, get_cpu()).field++;		\
	put_cpu();						\
} while (0)
#define avc_cache_stats_incr(field)	this_cpu_inc(avc_cache_stats.field)
#else
#define avc_cache_stats_incr(field)	do {} while (0)
#endif

struct avc_entry {
	u32			ssid;
	u32			tsid;
	u16			tclass;
	struct av_decision	avd;
	atomic_t		used;	/* used recently */
	struct avc_xperms_node	*xp_node;
};

struct avc_node {
	struct avc_entry	ae;
	struct list_head	list;
	struct rcu_head		rhead;
};

struct avc_cache {
	struct list_head	slots[AVC_CACHE_SLOTS];
	struct hlist_node	list; /* anchored in avc_cache->slots[i] */
	struct rcu_head		rhead;
};

struct avc_xperms_decision_node {
	struct extended_perms_decision xpd;
	struct list_head xpd_list; /* list of extended_perms_decision */
};

struct avc_xperms_node {
	struct extended_perms xp;
	struct list_head xpd_head; /* list head of extended_perms_decision */
};

struct avc_cache {
	struct hlist_head	slots[AVC_CACHE_SLOTS]; /* head for avc_node->list */
	spinlock_t		slots_lock[AVC_CACHE_SLOTS]; /* lock for writes */
	atomic_t		lru_hint;	/* LRU hint for reclaim scan */
	atomic_t		active_nodes;
	u32			latest_notif;	/* latest revocation notification */
};

struct avc_callback_node {
	int (*callback) (u32 event, u32 ssid, u32 tsid,
			 u16 tclass, u32 perms,
			 u32 *out_retained);
	u32 events;
	u32 ssid;
	u32 tsid;
	u16 tclass;
	u32 perms;
	int (*callback) (u32 event);
	u32 events;
	struct avc_callback_node *next;
};

/* Exported via selinufs */
unsigned int avc_cache_threshold = AVC_DEF_CACHE_THRESHOLD;

#ifdef CONFIG_SECURITY_SELINUX_AVC_STATS
DEFINE_PER_CPU(struct avc_cache_stats, avc_cache_stats) = { 0 };
#endif

static struct avc_cache avc_cache;
static struct avc_callback_node *avc_callbacks;
static struct kmem_cache *avc_node_cachep;
static struct kmem_cache *avc_xperms_data_cachep;
static struct kmem_cache *avc_xperms_decision_cachep;
static struct kmem_cache *avc_xperms_cachep;

static inline int avc_hash(u32 ssid, u32 tsid, u16 tclass)
{
	return (ssid ^ (tsid<<2) ^ (tclass<<4)) & (AVC_CACHE_SLOTS - 1);
}

/**
 * avc_dump_av - Display an access vector in human-readable form.
 * @tclass: target security class
 * @av: access vector
 */
static void avc_dump_av(struct audit_buffer *ab, u16 tclass, u32 av)
{
	const char **common_pts = NULL;
	u32 common_base = 0;
	int i, i2, perm;
	const char **perms;
	int i, perm;

	if (av == 0) {
		audit_log_format(ab, " null");
		return;
	}

	for (i = 0; i < ARRAY_SIZE(av_inherit); i++) {
		if (av_inherit[i].tclass == tclass) {
			common_pts = av_inherit[i].common_pts;
			common_base = av_inherit[i].common_base;
			break;
		}
	}
	BUG_ON(!tclass || tclass >= ARRAY_SIZE(secclass_map));
	perms = secclass_map[tclass-1].perms;

	audit_log_format(ab, " {");
	i = 0;
	perm = 1;
	while (perm < common_base) {
		if (perm & av) {
			audit_log_format(ab, " %s", common_pts[i]);
	while (i < (sizeof(av) * 8)) {
		if ((perm & av) && perms[i]) {
			audit_log_format(ab, " %s", perms[i]);
			av &= ~perm;
		}
		i++;
		perm <<= 1;
	}

	while (i < sizeof(av) * 8) {
		if (perm & av) {
			for (i2 = 0; i2 < ARRAY_SIZE(av_perm_to_string); i2++) {
				if ((av_perm_to_string[i2].tclass == tclass) &&
				    (av_perm_to_string[i2].value == perm))
					break;
			}
			if (i2 < ARRAY_SIZE(av_perm_to_string)) {
				audit_log_format(ab, " %s",
						 av_perm_to_string[i2].name);
				av &= ~perm;
			}
		}
		i++;
		perm <<= 1;
	}

	if (av)
		audit_log_format(ab, " 0x%x", av);

	audit_log_format(ab, " }");
}

/**
 * avc_dump_query - Display a SID pair and a class in human-readable form.
 * @ssid: source security identifier
 * @tsid: target security identifier
 * @tclass: target security class
 */
static void avc_dump_query(struct audit_buffer *ab, u32 ssid, u32 tsid, u16 tclass)
{
	int rc;
	char *scontext;
	u32 scontext_len;

	rc = security_sid_to_context(ssid, &scontext, &scontext_len);
	if (rc)
		audit_log_format(ab, "ssid=%d", ssid);
	else {
		audit_log_format(ab, "scontext=%s", scontext);
		kfree(scontext);
	}

	rc = security_sid_to_context(tsid, &scontext, &scontext_len);
	if (rc)
		audit_log_format(ab, " tsid=%d", tsid);
	else {
		audit_log_format(ab, " tcontext=%s", scontext);
		kfree(scontext);
	}

	BUG_ON(tclass >= ARRAY_SIZE(class_to_string) || !class_to_string[tclass]);
	audit_log_format(ab, " tclass=%s", class_to_string[tclass]);
	BUG_ON(!tclass || tclass >= ARRAY_SIZE(secclass_map));
	audit_log_format(ab, " tclass=%s", secclass_map[tclass-1].name);
}

/**
 * avc_init - Initialize the AVC.
 *
 * Initialize the access vector cache.
 */
void __init avc_init(void)
{
	int i;

	for (i = 0; i < AVC_CACHE_SLOTS; i++) {
		INIT_LIST_HEAD(&avc_cache.slots[i]);
		INIT_HLIST_HEAD(&avc_cache.slots[i]);
		spin_lock_init(&avc_cache.slots_lock[i]);
	}
	atomic_set(&avc_cache.active_nodes, 0);
	atomic_set(&avc_cache.lru_hint, 0);

	avc_node_cachep = kmem_cache_create("avc_node", sizeof(struct avc_node),
					     0, SLAB_PANIC, NULL);
					0, SLAB_PANIC, NULL);
	avc_xperms_cachep = kmem_cache_create("avc_xperms_node",
					sizeof(struct avc_xperms_node),
					0, SLAB_PANIC, NULL);
	avc_xperms_decision_cachep = kmem_cache_create(
					"avc_xperms_decision_node",
					sizeof(struct avc_xperms_decision_node),
					0, SLAB_PANIC, NULL);
	avc_xperms_data_cachep = kmem_cache_create("avc_xperms_data",
					sizeof(struct extended_perms_data),
					0, SLAB_PANIC, NULL);

	audit_log(current->audit_context, GFP_KERNEL, AUDIT_KERNEL, "AVC INITIALIZED\n");
}

int avc_get_hash_stats(char *page)
{
	int i, chain_len, max_chain_len, slots_used;
	struct avc_node *node;
	struct hlist_head *head;

	rcu_read_lock();

	slots_used = 0;
	max_chain_len = 0;
	for (i = 0; i < AVC_CACHE_SLOTS; i++) {
		if (!list_empty(&avc_cache.slots[i])) {
			slots_used++;
			chain_len = 0;
			list_for_each_entry_rcu(node, &avc_cache.slots[i], list)
		head = &avc_cache.slots[i];
		if (!hlist_empty(head)) {
			slots_used++;
			chain_len = 0;
			hlist_for_each_entry_rcu(node, head, list)
				chain_len++;
			if (chain_len > max_chain_len)
				max_chain_len = chain_len;
		}
	}

	rcu_read_unlock();

	return scnprintf(page, PAGE_SIZE, "entries: %d\nbuckets used: %d/%d\n"
			 "longest chain: %d\n",
			 atomic_read(&avc_cache.active_nodes),
			 slots_used, AVC_CACHE_SLOTS, max_chain_len);
}

static void avc_node_free(struct rcu_head *rhead)
{
	struct avc_node *node = container_of(rhead, struct avc_node, rhead);
/*
 * using a linked list for extended_perms_decision lookup because the list is
 * always small. i.e. less than 5, typically 1
 */
static struct extended_perms_decision *avc_xperms_decision_lookup(u8 driver,
					struct avc_xperms_node *xp_node)
{
	struct avc_xperms_decision_node *xpd_node;

	list_for_each_entry(xpd_node, &xp_node->xpd_head, xpd_list) {
		if (xpd_node->xpd.driver == driver)
			return &xpd_node->xpd;
	}
	return NULL;
}

static inline unsigned int
avc_xperms_has_perm(struct extended_perms_decision *xpd,
					u8 perm, u8 which)
{
	unsigned int rc = 0;

	if ((which == XPERMS_ALLOWED) &&
			(xpd->used & XPERMS_ALLOWED))
		rc = security_xperm_test(xpd->allowed->p, perm);
	else if ((which == XPERMS_AUDITALLOW) &&
			(xpd->used & XPERMS_AUDITALLOW))
		rc = security_xperm_test(xpd->auditallow->p, perm);
	else if ((which == XPERMS_DONTAUDIT) &&
			(xpd->used & XPERMS_DONTAUDIT))
		rc = security_xperm_test(xpd->dontaudit->p, perm);
	return rc;
}

static void avc_xperms_allow_perm(struct avc_xperms_node *xp_node,
				u8 driver, u8 perm)
{
	struct extended_perms_decision *xpd;
	security_xperm_set(xp_node->xp.drivers.p, driver);
	xpd = avc_xperms_decision_lookup(driver, xp_node);
	if (xpd && xpd->allowed)
		security_xperm_set(xpd->allowed->p, perm);
}

static void avc_xperms_decision_free(struct avc_xperms_decision_node *xpd_node)
{
	struct extended_perms_decision *xpd;

	xpd = &xpd_node->xpd;
	if (xpd->allowed)
		kmem_cache_free(avc_xperms_data_cachep, xpd->allowed);
	if (xpd->auditallow)
		kmem_cache_free(avc_xperms_data_cachep, xpd->auditallow);
	if (xpd->dontaudit)
		kmem_cache_free(avc_xperms_data_cachep, xpd->dontaudit);
	kmem_cache_free(avc_xperms_decision_cachep, xpd_node);
}

static void avc_xperms_free(struct avc_xperms_node *xp_node)
{
	struct avc_xperms_decision_node *xpd_node, *tmp;

	if (!xp_node)
		return;

	list_for_each_entry_safe(xpd_node, tmp, &xp_node->xpd_head, xpd_list) {
		list_del(&xpd_node->xpd_list);
		avc_xperms_decision_free(xpd_node);
	}
	kmem_cache_free(avc_xperms_cachep, xp_node);
}

static void avc_copy_xperms_decision(struct extended_perms_decision *dest,
					struct extended_perms_decision *src)
{
	dest->driver = src->driver;
	dest->used = src->used;
	if (dest->used & XPERMS_ALLOWED)
		memcpy(dest->allowed->p, src->allowed->p,
				sizeof(src->allowed->p));
	if (dest->used & XPERMS_AUDITALLOW)
		memcpy(dest->auditallow->p, src->auditallow->p,
				sizeof(src->auditallow->p));
	if (dest->used & XPERMS_DONTAUDIT)
		memcpy(dest->dontaudit->p, src->dontaudit->p,
				sizeof(src->dontaudit->p));
}

/*
 * similar to avc_copy_xperms_decision, but only copy decision
 * information relevant to this perm
 */
static inline void avc_quick_copy_xperms_decision(u8 perm,
			struct extended_perms_decision *dest,
			struct extended_perms_decision *src)
{
	/*
	 * compute index of the u32 of the 256 bits (8 u32s) that contain this
	 * command permission
	 */
	u8 i = perm >> 5;

	dest->used = src->used;
	if (dest->used & XPERMS_ALLOWED)
		dest->allowed->p[i] = src->allowed->p[i];
	if (dest->used & XPERMS_AUDITALLOW)
		dest->auditallow->p[i] = src->auditallow->p[i];
	if (dest->used & XPERMS_DONTAUDIT)
		dest->dontaudit->p[i] = src->dontaudit->p[i];
}

static struct avc_xperms_decision_node
		*avc_xperms_decision_alloc(u8 which)
{
	struct avc_xperms_decision_node *xpd_node;
	struct extended_perms_decision *xpd;

	xpd_node = kmem_cache_zalloc(avc_xperms_decision_cachep,
				     GFP_NOWAIT | __GFP_NOWARN);
	if (!xpd_node)
		return NULL;

	xpd = &xpd_node->xpd;
	if (which & XPERMS_ALLOWED) {
		xpd->allowed = kmem_cache_zalloc(avc_xperms_data_cachep,
						GFP_NOWAIT | __GFP_NOWARN);
		if (!xpd->allowed)
			goto error;
	}
	if (which & XPERMS_AUDITALLOW) {
		xpd->auditallow = kmem_cache_zalloc(avc_xperms_data_cachep,
						GFP_NOWAIT | __GFP_NOWARN);
		if (!xpd->auditallow)
			goto error;
	}
	if (which & XPERMS_DONTAUDIT) {
		xpd->dontaudit = kmem_cache_zalloc(avc_xperms_data_cachep,
						GFP_NOWAIT | __GFP_NOWARN);
		if (!xpd->dontaudit)
			goto error;
	}
	return xpd_node;
error:
	avc_xperms_decision_free(xpd_node);
	return NULL;
}

static int avc_add_xperms_decision(struct avc_node *node,
			struct extended_perms_decision *src)
{
	struct avc_xperms_decision_node *dest_xpd;

	dest_xpd = avc_xperms_decision_alloc(src->used);
	if (!dest_xpd)
		return -ENOMEM;
	avc_copy_xperms_decision(&dest_xpd->xpd, src);
	list_add(&dest_xpd->xpd_list, &node->ae.xp_node->xpd_head);
	node->ae.xp_node->xp.len++;
	return 0;
}

static struct avc_xperms_node *avc_xperms_alloc(void)
{
	struct avc_xperms_node *xp_node;

	xp_node = kmem_cache_zalloc(avc_xperms_cachep, GFP_NOWAIT | __GFP_NOWARN);
	if (!xp_node)
		return xp_node;
	INIT_LIST_HEAD(&xp_node->xpd_head);
	return xp_node;
}

static int avc_xperms_populate(struct avc_node *node,
				struct avc_xperms_node *src)
{
	struct avc_xperms_node *dest;
	struct avc_xperms_decision_node *dest_xpd;
	struct avc_xperms_decision_node *src_xpd;

	if (src->xp.len == 0)
		return 0;
	dest = avc_xperms_alloc();
	if (!dest)
		return -ENOMEM;

	memcpy(dest->xp.drivers.p, src->xp.drivers.p, sizeof(dest->xp.drivers.p));
	dest->xp.len = src->xp.len;

	/* for each source xpd allocate a destination xpd and copy */
	list_for_each_entry(src_xpd, &src->xpd_head, xpd_list) {
		dest_xpd = avc_xperms_decision_alloc(src_xpd->xpd.used);
		if (!dest_xpd)
			goto error;
		avc_copy_xperms_decision(&dest_xpd->xpd, &src_xpd->xpd);
		list_add(&dest_xpd->xpd_list, &dest->xpd_head);
	}
	node->ae.xp_node = dest;
	return 0;
error:
	avc_xperms_free(dest);
	return -ENOMEM;

}

static inline u32 avc_xperms_audit_required(u32 requested,
					struct av_decision *avd,
					struct extended_perms_decision *xpd,
					u8 perm,
					int result,
					u32 *deniedp)
{
	u32 denied, audited;

	denied = requested & ~avd->allowed;
	if (unlikely(denied)) {
		audited = denied & avd->auditdeny;
		if (audited && xpd) {
			if (avc_xperms_has_perm(xpd, perm, XPERMS_DONTAUDIT))
				audited &= ~requested;
		}
	} else if (result) {
		audited = denied = requested;
	} else {
		audited = requested & avd->auditallow;
		if (audited && xpd) {
			if (!avc_xperms_has_perm(xpd, perm, XPERMS_AUDITALLOW))
				audited &= ~requested;
		}
	}

	*deniedp = denied;
	return audited;
}

static inline int avc_xperms_audit(u32 ssid, u32 tsid, u16 tclass,
				u32 requested, struct av_decision *avd,
				struct extended_perms_decision *xpd,
				u8 perm, int result,
				struct common_audit_data *ad)
{
	u32 audited, denied;

	audited = avc_xperms_audit_required(
			requested, avd, xpd, perm, result, &denied);
	if (likely(!audited))
		return 0;
	return slow_avc_audit(ssid, tsid, tclass, requested,
			audited, denied, result, ad, 0);
}

static void avc_node_free(struct rcu_head *rhead)
{
	struct avc_node *node = container_of(rhead, struct avc_node, rhead);
	avc_xperms_free(node->ae.xp_node);
	kmem_cache_free(avc_node_cachep, node);
	avc_cache_stats_incr(frees);
}

static void avc_node_delete(struct avc_node *node)
{
	list_del_rcu(&node->list);
	hlist_del_rcu(&node->list);
	call_rcu(&node->rhead, avc_node_free);
	atomic_dec(&avc_cache.active_nodes);
}

static void avc_node_kill(struct avc_node *node)
{
	avc_xperms_free(node->ae.xp_node);
	kmem_cache_free(avc_node_cachep, node);
	avc_cache_stats_incr(frees);
	atomic_dec(&avc_cache.active_nodes);
}

static void avc_node_replace(struct avc_node *new, struct avc_node *old)
{
	list_replace_rcu(&old->list, &new->list);
	hlist_replace_rcu(&old->list, &new->list);
	call_rcu(&old->rhead, avc_node_free);
	atomic_dec(&avc_cache.active_nodes);
}

static inline int avc_reclaim_node(void)
{
	struct avc_node *node;
	int hvalue, try, ecx;
	unsigned long flags;

	for (try = 0, ecx = 0; try < AVC_CACHE_SLOTS; try++) {
		hvalue = atomic_inc_return(&avc_cache.lru_hint) & (AVC_CACHE_SLOTS - 1);

		if (!spin_trylock_irqsave(&avc_cache.slots_lock[hvalue], flags))
			continue;

		rcu_read_lock();
		list_for_each_entry(node, &avc_cache.slots[hvalue], list) {
			if (atomic_dec_and_test(&node->ae.used)) {
				/* Recently Unused */
				avc_node_delete(node);
				avc_cache_stats_incr(reclaims);
				ecx++;
				if (ecx >= AVC_CACHE_RECLAIM) {
					rcu_read_unlock();
					spin_unlock_irqrestore(&avc_cache.slots_lock[hvalue], flags);
					goto out;
				}
			}
		}
		rcu_read_unlock();
		spin_unlock_irqrestore(&avc_cache.slots_lock[hvalue], flags);
	struct hlist_head *head;
	spinlock_t *lock;

	for (try = 0, ecx = 0; try < AVC_CACHE_SLOTS; try++) {
		hvalue = atomic_inc_return(&avc_cache.lru_hint) & (AVC_CACHE_SLOTS - 1);
		head = &avc_cache.slots[hvalue];
		lock = &avc_cache.slots_lock[hvalue];

		if (!spin_trylock_irqsave(lock, flags))
			continue;

		rcu_read_lock();
		hlist_for_each_entry(node, head, list) {
			avc_node_delete(node);
			avc_cache_stats_incr(reclaims);
			ecx++;
			if (ecx >= AVC_CACHE_RECLAIM) {
				rcu_read_unlock();
				spin_unlock_irqrestore(lock, flags);
				goto out;
			}
		}
		rcu_read_unlock();
		spin_unlock_irqrestore(lock, flags);
	}
out:
	return ecx;
}

static struct avc_node *avc_alloc_node(void)
{
	struct avc_node *node;

	node = kmem_cache_zalloc(avc_node_cachep, GFP_ATOMIC);
	if (!node)
		goto out;

	INIT_RCU_HEAD(&node->rhead);
	INIT_LIST_HEAD(&node->list);
	atomic_set(&node->ae.used, 1);
	node = kmem_cache_zalloc(avc_node_cachep, GFP_ATOMIC|__GFP_NOMEMALLOC);
	node = kmem_cache_zalloc(avc_node_cachep, GFP_NOWAIT | __GFP_NOWARN);
	if (!node)
		goto out;

	INIT_HLIST_NODE(&node->list);
	avc_cache_stats_incr(allocations);

	if (atomic_inc_return(&avc_cache.active_nodes) > avc_cache_threshold)
		avc_reclaim_node();

out:
	return node;
}

static void avc_node_populate(struct avc_node *node, u32 ssid, u32 tsid, u16 tclass, struct avc_entry *ae)
static void avc_node_populate(struct avc_node *node, u32 ssid, u32 tsid, u16 tclass, struct av_decision *avd)
{
	node->ae.ssid = ssid;
	node->ae.tsid = tsid;
	node->ae.tclass = tclass;
	memcpy(&node->ae.avd, &ae->avd, sizeof(node->ae.avd));
	memcpy(&node->ae.avd, avd, sizeof(node->ae.avd));
}

static inline struct avc_node *avc_search_node(u32 ssid, u32 tsid, u16 tclass)
{
	struct avc_node *node, *ret = NULL;
	int hvalue;

	hvalue = avc_hash(ssid, tsid, tclass);
	list_for_each_entry_rcu(node, &avc_cache.slots[hvalue], list) {
	struct hlist_head *head;

	hvalue = avc_hash(ssid, tsid, tclass);
	head = &avc_cache.slots[hvalue];
	hlist_for_each_entry_rcu(node, head, list) {
		if (ssid == node->ae.ssid &&
		    tclass == node->ae.tclass &&
		    tsid == node->ae.tsid) {
			ret = node;
			break;
		}
	}

	if (ret == NULL) {
		/* cache miss */
		goto out;
	}

	/* cache hit */
	if (atomic_read(&ret->ae.used) != 1)
		atomic_set(&ret->ae.used, 1);
out:
	return ret;
}

/**
 * avc_lookup - Look up an AVC entry.
 * @ssid: source security identifier
 * @tsid: target security identifier
 * @tclass: target security class
 * @requested: requested permissions, interpreted based on @tclass
 *
 * Look up an AVC entry that is valid for the
 * @requested permissions between the SID pair
 * (@ssid, @tsid), interpreting the permissions
 * based on @tclass.  If a valid AVC entry exists,
 * then this function return the avc_node.
 * Otherwise, this function returns NULL.
 */
static struct avc_node *avc_lookup(u32 ssid, u32 tsid, u16 tclass, u32 requested)
 *
 * Look up an AVC entry that is valid for the
 * (@ssid, @tsid), interpreting the permissions
 * based on @tclass.  If a valid AVC entry exists,
 * then this function returns the avc_node.
 * Otherwise, this function returns NULL.
 */
static struct avc_node *avc_lookup(u32 ssid, u32 tsid, u16 tclass)
{
	struct avc_node *node;

	avc_cache_stats_incr(lookups);
	node = avc_search_node(ssid, tsid, tclass);

	if (node && ((node->ae.avd.decided & requested) == requested)) {
		avc_cache_stats_incr(hits);
		goto out;
	}

	node = NULL;
	avc_cache_stats_incr(misses);
out:
	return node;
	if (node)
		return node;

	avc_cache_stats_incr(misses);
	return NULL;
}

static int avc_latest_notif_update(int seqno, int is_insert)
{
	int ret = 0;
	static DEFINE_SPINLOCK(notif_lock);
	unsigned long flag;

	spin_lock_irqsave(&notif_lock, flag);
	if (is_insert) {
		if (seqno < avc_cache.latest_notif) {
			printk(KERN_WARNING "SELinux: avc:  seqno %d < latest_notif %d\n",
			       seqno, avc_cache.latest_notif);
			ret = -EAGAIN;
		}
	} else {
		if (seqno > avc_cache.latest_notif)
			avc_cache.latest_notif = seqno;
	}
	spin_unlock_irqrestore(&notif_lock, flag);

	return ret;
}

/**
 * avc_insert - Insert an AVC entry.
 * @ssid: source security identifier
 * @tsid: target security identifier
 * @tclass: target security class
 * @ae: AVC entry
 * @avd: resulting av decision
 * @xp_node: resulting extended permissions
 *
 * Insert an AVC entry for the SID pair
 * (@ssid, @tsid) and class @tclass.
 * The access vectors and the sequence number are
 * normally provided by the security server in
 * response to a security_compute_av() call.  If the
 * sequence number @ae->avd.seqno is not less than the latest
 * sequence number @avd->seqno is not less than the latest
 * revocation notification, then the function copies
 * the access vectors into a cache entry, returns
 * avc_node inserted. Otherwise, this function returns NULL.
 */
static struct avc_node *avc_insert(u32 ssid, u32 tsid, u16 tclass, struct avc_entry *ae)
static struct avc_node *avc_insert(u32 ssid, u32 tsid, u16 tclass,
				struct av_decision *avd,
				struct avc_xperms_node *xp_node)
{
	struct avc_node *pos, *node = NULL;
	int hvalue;
	unsigned long flag;

	if (avc_latest_notif_update(ae->avd.seqno, 1))
	if (avc_latest_notif_update(avd->seqno, 1))
		goto out;

	node = avc_alloc_node();
	if (node) {
		hvalue = avc_hash(ssid, tsid, tclass);
		avc_node_populate(node, ssid, tsid, tclass, ae);

		spin_lock_irqsave(&avc_cache.slots_lock[hvalue], flag);
		list_for_each_entry(pos, &avc_cache.slots[hvalue], list) {
		struct hlist_head *head;
		spinlock_t *lock;
		int rc = 0;

		hvalue = avc_hash(ssid, tsid, tclass);
		avc_node_populate(node, ssid, tsid, tclass, avd);
		rc = avc_xperms_populate(node, xp_node);
		if (rc) {
			kmem_cache_free(avc_node_cachep, node);
			return NULL;
		}
		head = &avc_cache.slots[hvalue];
		lock = &avc_cache.slots_lock[hvalue];

		spin_lock_irqsave(lock, flag);
		hlist_for_each_entry(pos, head, list) {
			if (pos->ae.ssid == ssid &&
			    pos->ae.tsid == tsid &&
			    pos->ae.tclass == tclass) {
				avc_node_replace(node, pos);
				goto found;
			}
		}
		list_add_rcu(&node->list, &avc_cache.slots[hvalue]);
found:
		spin_unlock_irqrestore(&avc_cache.slots_lock[hvalue], flag);
		hlist_add_head_rcu(&node->list, head);
found:
		spin_unlock_irqrestore(lock, flag);
	}
out:
	return node;
}

static inline void avc_print_ipv6_addr(struct audit_buffer *ab,
				       struct in6_addr *addr, __be16 port,
				       char *name1, char *name2)
{
	if (!ipv6_addr_any(addr))
		audit_log_format(ab, " %s=" NIP6_FMT, name1, NIP6(*addr));
	if (port)
		audit_log_format(ab, " %s=%d", name2, ntohs(port));
}

static inline void avc_print_ipv4_addr(struct audit_buffer *ab, __be32 addr,
				       __be16 port, char *name1, char *name2)
{
	if (addr)
		audit_log_format(ab, " %s=" NIPQUAD_FMT, name1, NIPQUAD(addr));
	if (port)
		audit_log_format(ab, " %s=%d", name2, ntohs(port));
}

/**
 * avc_audit - Audit the granting or denial of permissions.
 * @ssid: source security identifier
 * @tsid: target security identifier
 * @tclass: target security class
 * @requested: requested permissions
 * @avd: access vector decisions
 * @result: result from avc_has_perm_noaudit
 * @a:  auxiliary audit data
 *
 * Audit the granting or denial of permissions in accordance
 * with the policy.  This function is typically called by
 * avc_has_perm() after a permission check, but can also be
 * called directly by callers who use avc_has_perm_noaudit()
 * in order to separate the permission check from the auditing.
 * For example, this separation is useful when the permission check must
 * be performed under a lock, to allow the lock to be released
 * before calling the auditing code.
 */
void avc_audit(u32 ssid, u32 tsid,
	       u16 tclass, u32 requested,
	       struct av_decision *avd, int result, struct avc_audit_data *a)
{
	struct task_struct *tsk = current;
	struct inode *inode = NULL;
	u32 denied, audited;
	struct audit_buffer *ab;

	denied = requested & ~avd->allowed;
	if (denied) {
		audited = denied;
		if (!(audited & avd->auditdeny))
			return;
	} else if (result) {
		audited = denied = requested;
	} else {
		audited = requested;
		if (!(audited & avd->auditallow))
			return;
	}

	ab = audit_log_start(current->audit_context, GFP_ATOMIC, AUDIT_AVC);
	if (!ab)
		return;		/* audit_panic has been called */
	audit_log_format(ab, "avc:  %s ", denied ? "denied" : "granted");
	avc_dump_av(ab, tclass, audited);
	audit_log_format(ab, " for ");
	if (a && a->tsk)
		tsk = a->tsk;
	if (tsk && tsk->pid) {
		audit_log_format(ab, " pid=%d comm=", tsk->pid);
		audit_log_untrustedstring(ab, tsk->comm);
	}
	if (a) {
		switch (a->type) {
		case AVC_AUDIT_DATA_IPC:
			audit_log_format(ab, " key=%d", a->u.ipc_id);
			break;
		case AVC_AUDIT_DATA_CAP:
			audit_log_format(ab, " capability=%d", a->u.cap);
			break;
		case AVC_AUDIT_DATA_FS:
			if (a->u.fs.path.dentry) {
				struct dentry *dentry = a->u.fs.path.dentry;
				if (a->u.fs.path.mnt) {
					audit_log_d_path(ab, "path=",
							 &a->u.fs.path);
				} else {
					audit_log_format(ab, " name=");
					audit_log_untrustedstring(ab, dentry->d_name.name);
				}
				inode = dentry->d_inode;
			} else if (a->u.fs.inode) {
				struct dentry *dentry;
				inode = a->u.fs.inode;
				dentry = d_find_alias(inode);
				if (dentry) {
					audit_log_format(ab, " name=");
					audit_log_untrustedstring(ab, dentry->d_name.name);
					dput(dentry);
				}
			}
			if (inode)
				audit_log_format(ab, " dev=%s ino=%lu",
						 inode->i_sb->s_id,
						 inode->i_ino);
			break;
		case AVC_AUDIT_DATA_NET:
			if (a->u.net.sk) {
				struct sock *sk = a->u.net.sk;
				struct unix_sock *u;
				int len = 0;
				char *p = NULL;

				switch (sk->sk_family) {
				case AF_INET: {
					struct inet_sock *inet = inet_sk(sk);

					avc_print_ipv4_addr(ab, inet->rcv_saddr,
							    inet->sport,
							    "laddr", "lport");
					avc_print_ipv4_addr(ab, inet->daddr,
							    inet->dport,
							    "faddr", "fport");
					break;
				}
				case AF_INET6: {
					struct inet_sock *inet = inet_sk(sk);
					struct ipv6_pinfo *inet6 = inet6_sk(sk);

					avc_print_ipv6_addr(ab, &inet6->rcv_saddr,
							    inet->sport,
							    "laddr", "lport");
					avc_print_ipv6_addr(ab, &inet6->daddr,
							    inet->dport,
							    "faddr", "fport");
					break;
				}
				case AF_UNIX:
					u = unix_sk(sk);
					if (u->dentry) {
						struct path path = {
							.dentry = u->dentry,
							.mnt = u->mnt
						};
						audit_log_d_path(ab, "path=",
								 &path);
						break;
					}
					if (!u->addr)
						break;
					len = u->addr->len-sizeof(short);
					p = &u->addr->name->sun_path[0];
					audit_log_format(ab, " path=");
					if (*p)
						audit_log_untrustedstring(ab, p);
					else
						audit_log_n_hex(ab, p, len);
					break;
				}
			}

			switch (a->u.net.family) {
			case AF_INET:
				avc_print_ipv4_addr(ab, a->u.net.v4info.saddr,
						    a->u.net.sport,
						    "saddr", "src");
				avc_print_ipv4_addr(ab, a->u.net.v4info.daddr,
						    a->u.net.dport,
						    "daddr", "dest");
				break;
			case AF_INET6:
				avc_print_ipv6_addr(ab, &a->u.net.v6info.saddr,
						    a->u.net.sport,
						    "saddr", "src");
				avc_print_ipv6_addr(ab, &a->u.net.v6info.daddr,
						    a->u.net.dport,
						    "daddr", "dest");
				break;
			}
			if (a->u.net.netif > 0) {
				struct net_device *dev;

				/* NOTE: we always use init's namespace */
				dev = dev_get_by_index(&init_net,
						       a->u.net.netif);
				if (dev) {
					audit_log_format(ab, " netif=%s",
							 dev->name);
					dev_put(dev);
				}
			}
			break;
		}
	}
	audit_log_format(ab, " ");
	avc_dump_query(ab, ssid, tsid, tclass);
	audit_log_end(ab);
/**
 * avc_audit_pre_callback - SELinux specific information
 * will be called by generic audit code
 * @ab: the audit buffer
 * @a: audit_data
 */
static void avc_audit_pre_callback(struct audit_buffer *ab, void *a)
{
	struct common_audit_data *ad = a;
	audit_log_format(ab, "avc:  %s ",
			 ad->selinux_audit_data->denied ? "denied" : "granted");
	avc_dump_av(ab, ad->selinux_audit_data->tclass,
			ad->selinux_audit_data->audited);
	audit_log_format(ab, " for ");
}

/**
 * avc_audit_post_callback - SELinux specific information
 * will be called by generic audit code
 * @ab: the audit buffer
 * @a: audit_data
 */
static void avc_audit_post_callback(struct audit_buffer *ab, void *a)
{
	struct common_audit_data *ad = a;
	audit_log_format(ab, " ");
	avc_dump_query(ab, ad->selinux_audit_data->ssid,
			   ad->selinux_audit_data->tsid,
			   ad->selinux_audit_data->tclass);
	if (ad->selinux_audit_data->denied) {
		audit_log_format(ab, " permissive=%u",
				 ad->selinux_audit_data->result ? 0 : 1);
	}
}

/* This is the slow part of avc audit with big stack footprint */
noinline int slow_avc_audit(u32 ssid, u32 tsid, u16 tclass,
		u32 requested, u32 audited, u32 denied, int result,
		struct common_audit_data *a,
		unsigned flags)
{
	struct common_audit_data stack_data;
	struct selinux_audit_data sad;

	if (!a) {
		a = &stack_data;
		a->type = LSM_AUDIT_DATA_NONE;
	}

	/*
	 * When in a RCU walk do the audit on the RCU retry.  This is because
	 * the collection of the dname in an inode audit message is not RCU
	 * safe.  Note this may drop some audits when the situation changes
	 * during retry. However this is logically just as if the operation
	 * happened a little later.
	 */
	if ((a->type == LSM_AUDIT_DATA_INODE) &&
	    (flags & MAY_NOT_BLOCK))
		return -ECHILD;

	sad.tclass = tclass;
	sad.requested = requested;
	sad.ssid = ssid;
	sad.tsid = tsid;
	sad.audited = audited;
	sad.denied = denied;
	sad.result = result;

	a->selinux_audit_data = &sad;

	common_lsm_audit(a, avc_audit_pre_callback, avc_audit_post_callback);
	return 0;
}

/**
 * avc_add_callback - Register a callback for security events.
 * @callback: callback function
 * @events: security events
 * @ssid: source security identifier or %SECSID_WILD
 * @tsid: target security identifier or %SECSID_WILD
 * @tclass: target security class
 * @perms: permissions
 *
 * Register a callback function for events in the set @events
 * related to the SID pair (@ssid, @tsid) and
 * and the permissions @perms, interpreting
 * @perms based on @tclass.  Returns %0 on success or
 * -%ENOMEM if insufficient memory exists to add the callback.
 */
int avc_add_callback(int (*callback)(u32 event, u32 ssid, u32 tsid,
				     u16 tclass, u32 perms,
				     u32 *out_retained),
		     u32 events, u32 ssid, u32 tsid,
		     u16 tclass, u32 perms)
 *
 * Register a callback function for events in the set @events.
 * Returns %0 on success or -%ENOMEM if insufficient memory
 * exists to add the callback.
 */
int __init avc_add_callback(int (*callback)(u32 event), u32 events)
{
	struct avc_callback_node *c;
	int rc = 0;

	c = kmalloc(sizeof(*c), GFP_ATOMIC);
	c = kmalloc(sizeof(*c), GFP_KERNEL);
	if (!c) {
		rc = -ENOMEM;
		goto out;
	}

	c->callback = callback;
	c->events = events;
	c->ssid = ssid;
	c->tsid = tsid;
	c->perms = perms;
	c->next = avc_callbacks;
	avc_callbacks = c;
out:
	return rc;
}

static inline int avc_sidcmp(u32 x, u32 y)
{
	return (x == y || x == SECSID_WILD || y == SECSID_WILD);
}

/**
 * avc_update_node Update an AVC entry
 * @event : Updating event
 * @perms : Permission mask bits
 * @ssid,@tsid,@tclass : identifier of an AVC entry
 *
 * if a valid AVC entry doesn't exist,this function returns -ENOENT.
 * if kmalloc() called internal returns NULL, this function returns -ENOMEM.
 * otherwise, this function update the AVC entry. The original AVC-entry object
 * will release later by RCU.
 */
static int avc_update_node(u32 event, u32 perms, u32 ssid, u32 tsid, u16 tclass)
 * @seqno : sequence number when decision was made
 * @xpd: extended_perms_decision to be added to the node
 *
 * if a valid AVC entry doesn't exist,this function returns -ENOENT.
 * if kmalloc() called internal returns NULL, this function returns -ENOMEM.
 * otherwise, this function updates the AVC entry. The original AVC-entry object
 * will release later by RCU.
 */
static int avc_update_node(u32 event, u32 perms, u8 driver, u8 xperm, u32 ssid,
			u32 tsid, u16 tclass, u32 seqno,
			struct extended_perms_decision *xpd,
			u32 flags)
{
	int hvalue, rc = 0;
	unsigned long flag;
	struct avc_node *pos, *node, *orig = NULL;
	struct hlist_head *head;
	spinlock_t *lock;

	node = avc_alloc_node();
	if (!node) {
		rc = -ENOMEM;
		goto out;
	}

	/* Lock the target slot */
	hvalue = avc_hash(ssid, tsid, tclass);
	spin_lock_irqsave(&avc_cache.slots_lock[hvalue], flag);

	list_for_each_entry(pos, &avc_cache.slots[hvalue], list) {
		if (ssid == pos->ae.ssid &&
		    tsid == pos->ae.tsid &&
		    tclass == pos->ae.tclass){

	head = &avc_cache.slots[hvalue];
	lock = &avc_cache.slots_lock[hvalue];

	spin_lock_irqsave(lock, flag);

	hlist_for_each_entry(pos, head, list) {
		if (ssid == pos->ae.ssid &&
		    tsid == pos->ae.tsid &&
		    tclass == pos->ae.tclass &&
		    seqno == pos->ae.avd.seqno){
			orig = pos;
			break;
		}
	}

	if (!orig) {
		rc = -ENOENT;
		avc_node_kill(node);
		goto out_unlock;
	}

	/*
	 * Copy and replace original node.
	 */

	avc_node_populate(node, ssid, tsid, tclass, &orig->ae);
	avc_node_populate(node, ssid, tsid, tclass, &orig->ae.avd);

	if (orig->ae.xp_node) {
		rc = avc_xperms_populate(node, orig->ae.xp_node);
		if (rc) {
			avc_node_kill(node);
			goto out_unlock;
		}
	}

	switch (event) {
	case AVC_CALLBACK_GRANT:
		node->ae.avd.allowed |= perms;
		if (node->ae.xp_node && (flags & AVC_EXTENDED_PERMS))
			avc_xperms_allow_perm(node->ae.xp_node, driver, xperm);
		break;
	case AVC_CALLBACK_TRY_REVOKE:
	case AVC_CALLBACK_REVOKE:
		node->ae.avd.allowed &= ~perms;
		break;
	case AVC_CALLBACK_AUDITALLOW_ENABLE:
		node->ae.avd.auditallow |= perms;
		break;
	case AVC_CALLBACK_AUDITALLOW_DISABLE:
		node->ae.avd.auditallow &= ~perms;
		break;
	case AVC_CALLBACK_AUDITDENY_ENABLE:
		node->ae.avd.auditdeny |= perms;
		break;
	case AVC_CALLBACK_AUDITDENY_DISABLE:
		node->ae.avd.auditdeny &= ~perms;
		break;
	}
	avc_node_replace(node, orig);
out_unlock:
	spin_unlock_irqrestore(&avc_cache.slots_lock[hvalue], flag);
	case AVC_CALLBACK_ADD_XPERMS:
		avc_add_xperms_decision(node, xpd);
		break;
	}
	avc_node_replace(node, orig);
out_unlock:
	spin_unlock_irqrestore(lock, flag);
out:
	return rc;
}

/**
 * avc_flush - Flush the cache
 */
static void avc_flush(void)
{
	struct hlist_head *head;
	struct avc_node *node;
	spinlock_t *lock;
	unsigned long flag;
	int i;

	for (i = 0; i < AVC_CACHE_SLOTS; i++) {
		head = &avc_cache.slots[i];
		lock = &avc_cache.slots_lock[i];

		spin_lock_irqsave(lock, flag);
		/*
		 * With preemptable RCU, the outer spinlock does not
		 * prevent RCU grace periods from ending.
		 */
		rcu_read_lock();
		hlist_for_each_entry(node, head, list)
			avc_node_delete(node);
		rcu_read_unlock();
		spin_unlock_irqrestore(lock, flag);
	}
}

/**
 * avc_ss_reset - Flush the cache and revalidate migrated permissions.
 * @seqno: policy sequence number
 */
int avc_ss_reset(u32 seqno)
{
	struct avc_callback_node *c;
	int i, rc = 0, tmprc;
	unsigned long flag;
	struct avc_node *node;

	for (i = 0; i < AVC_CACHE_SLOTS; i++) {
		spin_lock_irqsave(&avc_cache.slots_lock[i], flag);
		/*
		 * With preemptable RCU, the outer spinlock does not
		 * prevent RCU grace periods from ending.
		 */
		rcu_read_lock();
		list_for_each_entry(node, &avc_cache.slots[i], list)
			avc_node_delete(node);
		rcu_read_unlock();
		spin_unlock_irqrestore(&avc_cache.slots_lock[i], flag);
	}

	for (c = avc_callbacks; c; c = c->next) {
		if (c->events & AVC_CALLBACK_RESET) {
			tmprc = c->callback(AVC_CALLBACK_RESET,
					    0, 0, 0, 0, NULL);
	int rc = 0, tmprc;

	avc_flush();

	for (c = avc_callbacks; c; c = c->next) {
		if (c->events & AVC_CALLBACK_RESET) {
			tmprc = c->callback(AVC_CALLBACK_RESET);
			/* save the first error encountered for the return
			   value and continue processing the callbacks */
			if (!rc)
				rc = tmprc;
		}
	}

	avc_latest_notif_update(seqno, 0);
	return rc;
}

/*
 * Slow-path helper function for avc_has_perm_noaudit,
 * when the avc_node lookup fails. We get called with
 * the RCU read lock held, and need to return with it
 * still held, but drop if for the security compute.
 *
 * Don't inline this, since it's the slow-path and just
 * results in a bigger stack frame.
 */
static noinline struct avc_node *avc_compute_av(u32 ssid, u32 tsid,
			 u16 tclass, struct av_decision *avd,
			 struct avc_xperms_node *xp_node)
{
	rcu_read_unlock();
	INIT_LIST_HEAD(&xp_node->xpd_head);
	security_compute_av(ssid, tsid, tclass, avd, &xp_node->xp);
	rcu_read_lock();
	return avc_insert(ssid, tsid, tclass, avd, xp_node);
}

static noinline int avc_denied(u32 ssid, u32 tsid,
				u16 tclass, u32 requested,
				u8 driver, u8 xperm, unsigned flags,
				struct av_decision *avd)
{
	if (flags & AVC_STRICT)
		return -EACCES;

	if (selinux_enforcing && !(avd->flags & AVD_FLAGS_PERMISSIVE))
		return -EACCES;

	avc_update_node(AVC_CALLBACK_GRANT, requested, driver, xperm, ssid,
				tsid, tclass, avd->seqno, NULL, flags);
	return 0;
}

/*
 * The avc extended permissions logic adds an additional 256 bits of
 * permissions to an avc node when extended permissions for that node are
 * specified in the avtab. If the additional 256 permissions is not adequate,
 * as-is the case with ioctls, then multiple may be chained together and the
 * driver field is used to specify which set contains the permission.
 */
int avc_has_extended_perms(u32 ssid, u32 tsid, u16 tclass, u32 requested,
			u8 driver, u8 xperm, struct common_audit_data *ad)
{
	struct avc_node *node;
	struct av_decision avd;
	u32 denied;
	struct extended_perms_decision local_xpd;
	struct extended_perms_decision *xpd = NULL;
	struct extended_perms_data allowed;
	struct extended_perms_data auditallow;
	struct extended_perms_data dontaudit;
	struct avc_xperms_node local_xp_node;
	struct avc_xperms_node *xp_node;
	int rc = 0, rc2;

	xp_node = &local_xp_node;
	BUG_ON(!requested);

	rcu_read_lock();

	node = avc_lookup(ssid, tsid, tclass);
	if (unlikely(!node)) {
		node = avc_compute_av(ssid, tsid, tclass, &avd, xp_node);
	} else {
		memcpy(&avd, &node->ae.avd, sizeof(avd));
		xp_node = node->ae.xp_node;
	}
	/* if extended permissions are not defined, only consider av_decision */
	if (!xp_node || !xp_node->xp.len)
		goto decision;

	local_xpd.allowed = &allowed;
	local_xpd.auditallow = &auditallow;
	local_xpd.dontaudit = &dontaudit;

	xpd = avc_xperms_decision_lookup(driver, xp_node);
	if (unlikely(!xpd)) {
		/*
		 * Compute the extended_perms_decision only if the driver
		 * is flagged
		 */
		if (!security_xperm_test(xp_node->xp.drivers.p, driver)) {
			avd.allowed &= ~requested;
			goto decision;
		}
		rcu_read_unlock();
		security_compute_xperms_decision(ssid, tsid, tclass, driver,
						&local_xpd);
		rcu_read_lock();
		avc_update_node(AVC_CALLBACK_ADD_XPERMS, requested, driver, xperm,
				ssid, tsid, tclass, avd.seqno, &local_xpd, 0);
	} else {
		avc_quick_copy_xperms_decision(xperm, &local_xpd, xpd);
	}
	xpd = &local_xpd;

	if (!avc_xperms_has_perm(xpd, xperm, XPERMS_ALLOWED))
		avd.allowed &= ~requested;

decision:
	denied = requested & ~(avd.allowed);
	if (unlikely(denied))
		rc = avc_denied(ssid, tsid, tclass, requested, driver, xperm,
				AVC_EXTENDED_PERMS, &avd);

	rcu_read_unlock();

	rc2 = avc_xperms_audit(ssid, tsid, tclass, requested,
			&avd, xpd, xperm, rc, ad);
	if (rc2)
		return rc2;
	return rc;
}

/**
 * avc_has_perm_noaudit - Check permissions but perform no auditing.
 * @ssid: source security identifier
 * @tsid: target security identifier
 * @tclass: target security class
 * @requested: requested permissions, interpreted based on @tclass
 * @flags:  AVC_STRICT or 0
 * @avd: access vector decisions
 *
 * Check the AVC to determine whether the @requested permissions are granted
 * for the SID pair (@ssid, @tsid), interpreting the permissions
 * based on @tclass, and call the security server on a cache miss to obtain
 * a new decision and add it to the cache.  Return a copy of the decisions
 * in @avd.  Return %0 if all @requested permissions are granted,
 * -%EACCES if any permissions are denied, or another -errno upon
 * other errors.  This function is typically called by avc_has_perm(),
 * but may also be called directly to separate permission checking from
 * auditing, e.g. in cases where a lock must be held for the check but
 * should be released for the auditing.
 */
int avc_has_perm_noaudit(u32 ssid, u32 tsid,
inline int avc_has_perm_noaudit(u32 ssid, u32 tsid,
			 u16 tclass, u32 requested,
			 unsigned flags,
			 struct av_decision *avd)
{
	struct avc_node *node;
	struct avc_entry entry, *p_ae;
	struct avc_xperms_node xp_node;
	int rc = 0;
	u32 denied;

	BUG_ON(!requested);

	rcu_read_lock();

	node = avc_lookup(ssid, tsid, tclass, requested);
	if (!node) {
		rcu_read_unlock();
		rc = security_compute_av(ssid, tsid, tclass, requested, &entry.avd);
		if (rc)
			goto out;
		rcu_read_lock();
		node = avc_insert(ssid, tsid, tclass, &entry);
	}

	p_ae = node ? &node->ae : &entry;

	if (avd)
		memcpy(avd, &p_ae->avd, sizeof(*avd));

	denied = requested & ~(p_ae->avd.allowed);

	if (denied) {
		if (flags & AVC_STRICT)
			rc = -EACCES;
		else if (!selinux_enforcing || security_permissive_sid(ssid))
			avc_update_node(AVC_CALLBACK_GRANT, requested, ssid,
					tsid, tclass);
		else
			rc = -EACCES;
	}

	rcu_read_unlock();
out:
	node = avc_lookup(ssid, tsid, tclass);
	if (unlikely(!node))
		node = avc_compute_av(ssid, tsid, tclass, avd, &xp_node);
	else
		memcpy(avd, &node->ae.avd, sizeof(*avd));

	denied = requested & ~(avd->allowed);
	if (unlikely(denied))
		rc = avc_denied(ssid, tsid, tclass, requested, 0, 0, flags, avd);

	rcu_read_unlock();
	return rc;
}

/**
 * avc_has_perm - Check permissions and perform any appropriate auditing.
 * @ssid: source security identifier
 * @tsid: target security identifier
 * @tclass: target security class
 * @requested: requested permissions, interpreted based on @tclass
 * @auditdata: auxiliary audit data
 *
 * Check the AVC to determine whether the @requested permissions are granted
 * for the SID pair (@ssid, @tsid), interpreting the permissions
 * based on @tclass, and call the security server on a cache miss to obtain
 * a new decision and add it to the cache.  Audit the granting or denial of
 * permissions in accordance with the policy.  Return %0 if all @requested
 * permissions are granted, -%EACCES if any permissions are denied, or
 * another -errno upon other errors.
 */
int avc_has_perm(u32 ssid, u32 tsid, u16 tclass,
		 u32 requested, struct avc_audit_data *auditdata)
{
	struct av_decision avd;
	int rc;

	rc = avc_has_perm_noaudit(ssid, tsid, tclass, requested, 0, &avd);
	avc_audit(ssid, tsid, tclass, requested, &avd, rc, auditdata);
		 u32 requested, struct common_audit_data *auditdata)
{
	struct av_decision avd;
	int rc, rc2;

	rc = avc_has_perm_noaudit(ssid, tsid, tclass, requested, 0, &avd);

	rc2 = avc_audit(ssid, tsid, tclass, requested, &avd, rc, auditdata, 0);
	if (rc2)
		return rc2;
	return rc;
}

int avc_has_perm_flags(u32 ssid, u32 tsid, u16 tclass,
		       u32 requested, struct common_audit_data *auditdata,
		       int flags)
{
	struct av_decision avd;
	int rc, rc2;

	rc = avc_has_perm_noaudit(ssid, tsid, tclass, requested, 0, &avd);

	rc2 = avc_audit(ssid, tsid, tclass, requested, &avd, rc,
			auditdata, flags);
	if (rc2)
		return rc2;
	return rc;
}

u32 avc_policy_seqno(void)
{
	return avc_cache.latest_notif;
}

void avc_disable(void)
{
	/*
	 * If you are looking at this because you have realized that we are
	 * not destroying the avc_node_cachep it might be easy to fix, but
	 * I don't know the memory barrier semantics well enough to know.  It's
	 * possible that some other task dereferenced security_ops when
	 * it still pointed to selinux operations.  If that is the case it's
	 * possible that it is about to use the avc and is about to need the
	 * avc_node_cachep.  I know I could wrap the security.c security_ops call
	 * in an rcu_lock, but seriously, it's not worth it.  Instead I just flush
	 * the cache and get that memory back.
	 */
	if (avc_node_cachep) {
		avc_flush();
		/* kmem_cache_destroy(avc_node_cachep); */
	}
}
