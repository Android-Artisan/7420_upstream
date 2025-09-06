/*
 * inet fragments management
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * 		Authors:	Pavel Emelyanov <xemul@openvz.org>
 *				Started as consolidation of ipv4/ip_fragment.c,
 *				ipv6/reassembly. and ipv6 nf conntrack reassembly
 */

#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/random.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>

#include <net/inet_frag.h>

static void inet_frag_secret_rebuild(unsigned long dummy)
{
	struct inet_frags *f = (struct inet_frags *)dummy;
	unsigned long now = jiffies;
	int i;

	write_lock(&f->lock);
	get_random_bytes(&f->rnd, sizeof(u32));
	for (i = 0; i < INETFRAGS_HASHSZ; i++) {
		struct inet_frag_queue *q;
		struct hlist_node *p, *n;

		hlist_for_each_entry_safe(q, p, n, &f->hash[i], list) {
			unsigned int hval = f->hashfn(q);

			if (hval != i) {
				hlist_del(&q->list);

				/* Relink to new hash chain. */
				hlist_add_head(&q->list, &f->hash[hval]);
			}
		}
	}
	write_unlock(&f->lock);

	mod_timer(&f->secret_timer, now + f->secret_interval);
}

void inet_frags_init(struct inet_frags *f)
{
	int i;

	for (i = 0; i < INETFRAGS_HASHSZ; i++)
		INIT_HLIST_HEAD(&f->hash[i]);

	rwlock_init(&f->lock);

	f->rnd = (u32) ((num_physpages ^ (num_physpages>>7)) ^
				   (jiffies ^ (jiffies >> 6)));

	setup_timer(&f->secret_timer, inet_frag_secret_rebuild,
			(unsigned long)f);
	f->secret_timer.expires = jiffies + f->secret_interval;
	add_timer(&f->secret_timer);
}
EXPORT_SYMBOL(inet_frags_init);

void inet_frags_init_net(struct netns_frags *nf)
{
	nf->nqueues = 0;
	atomic_set(&nf->mem, 0);
	INIT_LIST_HEAD(&nf->lru_list);
}
EXPORT_SYMBOL(inet_frags_init_net);

void inet_frags_fini(struct inet_frags *f)
{
	del_timer(&f->secret_timer);
#include <linux/slab.h>

#include <net/sock.h>
#include <net/inet_frag.h>
#include <net/inet_ecn.h>

/* Given the OR values of all fragments, apply RFC 3168 5.3 requirements
 * Value : 0xff if frame should be dropped.
 *         0 or INET_ECN_CE value, to be ORed in to final iph->tos field
 */
const u8 ip_frag_ecn_table[16] = {
	/* at least one fragment had CE, and others ECT_0 or ECT_1 */
	[IPFRAG_ECN_CE | IPFRAG_ECN_ECT_0]			= INET_ECN_CE,
	[IPFRAG_ECN_CE | IPFRAG_ECN_ECT_1]			= INET_ECN_CE,
	[IPFRAG_ECN_CE | IPFRAG_ECN_ECT_0 | IPFRAG_ECN_ECT_1]	= INET_ECN_CE,

	/* invalid combinations : drop frame */
	[IPFRAG_ECN_NOT_ECT | IPFRAG_ECN_CE] = 0xff,
	[IPFRAG_ECN_NOT_ECT | IPFRAG_ECN_ECT_0] = 0xff,
	[IPFRAG_ECN_NOT_ECT | IPFRAG_ECN_ECT_1] = 0xff,
	[IPFRAG_ECN_NOT_ECT | IPFRAG_ECN_ECT_0 | IPFRAG_ECN_ECT_1] = 0xff,
	[IPFRAG_ECN_NOT_ECT | IPFRAG_ECN_CE | IPFRAG_ECN_ECT_0] = 0xff,
	[IPFRAG_ECN_NOT_ECT | IPFRAG_ECN_CE | IPFRAG_ECN_ECT_1] = 0xff,
	[IPFRAG_ECN_NOT_ECT | IPFRAG_ECN_CE | IPFRAG_ECN_ECT_0 | IPFRAG_ECN_ECT_1] = 0xff,
};
EXPORT_SYMBOL(ip_frag_ecn_table);

int inet_frags_init(struct inet_frags *f)
{
	f->frags_cachep = kmem_cache_create(f->frags_cache_name, f->qsize, 0, 0,
					    NULL);
	if (!f->frags_cachep)
		return -ENOMEM;

	return 0;
}
EXPORT_SYMBOL(inet_frags_init);

void inet_frags_fini(struct inet_frags *f)
{
	/* We must wait that all inet_frag_destroy_rcu() have completed. */
	rcu_barrier();

	kmem_cache_destroy(f->frags_cachep);
	f->frags_cachep = NULL;
}
EXPORT_SYMBOL(inet_frags_fini);

static void inet_frags_free_cb(void *ptr, void *arg)
{
	nf->low_thresh = 0;

	local_bh_disable();
	inet_frag_evictor(nf, f);
	local_bh_enable();
}
EXPORT_SYMBOL(inet_frags_exit_net);

static inline void fq_unlink(struct inet_frag_queue *fq, struct inet_frags *f)
{
	write_lock(&f->lock);
	hlist_del(&fq->list);
	list_del(&fq->lru_list);
	fq->net->nqueues--;
	write_unlock(&f->lock);
	unsigned int seq;
	int i;
	struct inet_frag_queue *fq = ptr;

	/* If we can not cancel the timer, it means this frag_queue
	 * is already disappearing, we have nothing to do.
	 * Otherwise, we own a refcount until the end of this function.
	 */
	if (!del_timer(&fq->timer))
		return;

	spin_lock_bh(&fq->lock);
	if (!(fq->flags & INET_FRAG_COMPLETE)) {
		fq->flags |= INET_FRAG_COMPLETE;
		atomic_dec(&fq->refcnt);
	}
	spin_unlock_bh(&fq->lock);

	inet_frag_put(fq);
}

void inet_frags_exit_net(struct netns_frags *nf)
{
	nf->high_thresh = 0; /* prevent creation of new frags */

	rhashtable_free_and_destroy(&nf->rhashtable, inet_frags_free_cb, NULL);
}
EXPORT_SYMBOL(inet_frags_exit_net);

void inet_frag_kill(struct inet_frag_queue *fq)
{
	if (del_timer(&fq->timer))
		atomic_dec(&fq->refcnt);

	if (!(fq->last_in & INET_FRAG_COMPLETE)) {
		fq_unlink(fq, f);
		atomic_dec(&fq->refcnt);
		fq->last_in |= INET_FRAG_COMPLETE;
	}
}

EXPORT_SYMBOL(inet_frag_kill);

static inline void frag_kfree_skb(struct netns_frags *nf, struct inet_frags *f,
		struct sk_buff *skb, int *work)
{
	if (work)
		*work -= skb->truesize;

	atomic_sub(skb->truesize, &nf->mem);
	if (!(fq->flags & INET_FRAG_COMPLETE)) {
		struct netns_frags *nf = fq->net;

		fq->flags |= INET_FRAG_COMPLETE;
		rhashtable_remove_fast(&nf->rhashtable, &fq->node, nf->f->rhash_params);
		atomic_dec(&fq->refcnt);
	}
}
EXPORT_SYMBOL(inet_frag_kill);

static inline void frag_kfree_skb(struct netns_frags *nf, struct inet_frags *f,
				  struct sk_buff *skb)
{
	if (f->skb_free)
		f->skb_free(skb);
	kfree_skb(skb);
}

void inet_frag_destroy(struct inet_frag_queue *q, struct inet_frags *f,
					int *work)
{
	struct sk_buff *fp;
	struct netns_frags *nf;

	WARN_ON(!(q->last_in & INET_FRAG_COMPLETE));
void inet_frag_destroy(struct inet_frag_queue *q, struct inet_frags *f)
static void inet_frag_destroy_rcu(struct rcu_head *head)
{
	struct inet_frag_queue *q = container_of(head, struct inet_frag_queue,
						 rcu);
	struct inet_frags *f = q->net->f;

	if (f->destructor)
		f->destructor(q);
	kmem_cache_free(f->frags_cachep, q);
}

void inet_frag_destroy(struct inet_frag_queue *q)
{
	struct sk_buff *fp;
	struct netns_frags *nf;
	unsigned int sum, sum_truesize = 0;
	struct inet_frags *f;

	WARN_ON(!(q->flags & INET_FRAG_COMPLETE));
	WARN_ON(del_timer(&q->timer) != 0);

	/* Release all fragment data. */
	fp = q->fragments;
	nf = q->net;
	f = nf->f;
	if (fp) {
		do {
			struct sk_buff *xp = fp->next;

		frag_kfree_skb(nf, f, fp, work);
		fp = xp;
	}

	if (work)
		*work -= f->qsize;
	atomic_sub(f->qsize, &nf->mem);

	if (f->destructor)
		f->destructor(q);
	kfree(q);

}
EXPORT_SYMBOL(inet_frag_destroy);

int inet_frag_evictor(struct netns_frags *nf, struct inet_frags *f)
{
	struct inet_frag_queue *q;
	int work, evicted = 0;

	work = atomic_read(&nf->mem) - nf->low_thresh;
	while (work > 0) {
		read_lock(&f->lock);
		if (list_empty(&nf->lru_list)) {
			read_unlock(&f->lock);
			break;
		}

		q = list_first_entry(&nf->lru_list,
				struct inet_frag_queue, lru_list);
		atomic_inc(&q->refcnt);
		read_unlock(&f->lock);

		spin_lock(&q->lock);
		if (!(q->last_in & INET_FRAG_COMPLETE))
			inet_frag_kill(q, f);
		spin_unlock(&q->lock);

		if (atomic_dec_and_test(&q->refcnt))
			inet_frag_destroy(q, f, &work);
		evicted++;
	}

	return evicted;
}
EXPORT_SYMBOL(inet_frag_evictor);

static struct inet_frag_queue *inet_frag_intern(struct netns_frags *nf,
		struct inet_frag_queue *qp_in, struct inet_frags *f,
		void *arg)
{
	struct inet_frag_queue *qp;
#ifdef CONFIG_SMP
	struct hlist_node *n;
#endif
	unsigned int hash;

	write_lock(&f->lock);
	/*
	 * While we stayed w/o the lock other CPU could update
	 * the rnd seed, so we need to re-calculate the hash
	 * chain. Fortunatelly the qp_in can be used to get one.
	 */
	hash = f->hashfn(qp_in);
#ifdef CONFIG_SMP
	/* With SMP race we have to recheck hash table, because
	 * such entry could be created on other cpu, while we
	 * promoted read lock to write lock.
	 */
	hlist_for_each_entry(qp, n, &f->hash[hash], list) {
		if (qp->net == nf && f->match(qp, arg)) {
			atomic_inc(&qp->refcnt);
			write_unlock(&f->lock);
			qp_in->last_in |= INET_FRAG_COMPLETE;
		sum_truesize += fp->truesize;
		frag_kfree_skb(nf, f, fp);
		fp = xp;
			sum_truesize += fp->truesize;
			frag_kfree_skb(nf, f, fp);
			fp = xp;
		} while (fp);
	} else {
		sum_truesize = inet_frag_rbtree_purge(&q->rb_fragments);
	}
	sum = sum_truesize + f->qsize;

	call_rcu(&q->rcu, inet_frag_destroy_rcu);

	sub_frag_mem_limit(nf, sum);
}
EXPORT_SYMBOL(inet_frag_destroy);

static struct inet_frag_queue *inet_frag_intern(struct netns_frags *nf,
						struct inet_frag_queue *qp_in,
						struct inet_frags *f,
						void *arg)
{
	struct inet_frag_bucket *hb = get_frag_bucket_locked(qp_in, f);
	struct inet_frag_queue *qp;

#ifdef CONFIG_SMP
	/* With SMP race we have to recheck hash table, because
	 * such entry could have been created on other cpu before
	 * we acquired hash bucket lock.
	 */
	hlist_for_each_entry(qp, &hb->chain, list) {
		if (qp->net == nf && f->match(qp, arg)) {
			atomic_inc(&qp->refcnt);
			spin_unlock(&hb->chain_lock);
			qp_in->flags |= INET_FRAG_COMPLETE;
			inet_frag_put(qp_in, f);
			return qp;
		}
	}
#endif
	qp = qp_in;
	if (!mod_timer(&qp->timer, jiffies + nf->timeout))
		atomic_inc(&qp->refcnt);

	atomic_inc(&qp->refcnt);
	hlist_add_head(&qp->list, &f->hash[hash]);
	list_add_tail(&qp->lru_list, &nf->lru_list);
	nf->nqueues++;
	write_unlock(&f->lock);
	hlist_add_head(&qp->list, &hb->chain);

	spin_unlock(&hb->chain_lock);

	return qp;
}

static struct inet_frag_queue *inet_frag_alloc(struct netns_frags *nf,
		struct inet_frags *f, void *arg)
{
	struct inet_frag_queue *q;

	q = kzalloc(f->qsize, GFP_ATOMIC);
	if (q == NULL)
		return NULL;

	f->constructor(q, arg);
	atomic_add(f->qsize, &nf->mem);
	setup_timer(&q->timer, f->frag_expire, (unsigned long)q);
	spin_lock_init(&q->lock);
	atomic_set(&q->refcnt, 1);
	q->net = nf;
					       struct inet_frags *f,
					       void *arg)
{
	struct inet_frag_queue *q;

	if (!nf->high_thresh || frag_mem_limit(nf) > nf->high_thresh)
		return NULL;

	q = kmem_cache_zalloc(f->frags_cachep, GFP_ATOMIC);
	if (!q)
		return NULL;

	q->net = nf;
	f->constructor(q, arg);
	add_frag_mem_limit(nf, f->qsize);

	setup_timer(&q->timer, f->frag_expire, (unsigned long)q);
	spin_lock_init(&q->lock);
	atomic_set(&q->refcnt, 3);

	return q;
}

static struct inet_frag_queue *inet_frag_create(struct netns_frags *nf,
		struct inet_frags *f, void *arg)
						struct inet_frags *f,
						void *arg)
						void *arg,
						struct inet_frag_queue **prev)
{
	struct inet_frags *f = nf->f;
	struct inet_frag_queue *q;

	q = inet_frag_alloc(nf, f, arg);
	if (q == NULL)
	if (!q)
	if (!q) {
		*prev = ERR_PTR(-ENOMEM);
		return NULL;
	}
	mod_timer(&q->timer, jiffies + nf->timeout);

	*prev = rhashtable_lookup_get_insert_key(&nf->rhashtable, &q->key,
						 &q->node, f->rhash_params);
	if (*prev) {
		q->flags |= INET_FRAG_COMPLETE;
		inet_frag_kill(q);
		inet_frag_destroy(q);
		return NULL;
	}
	return q;
}
EXPORT_SYMBOL(inet_frag_create);

struct inet_frag_queue *inet_frag_find(struct netns_frags *nf,
		struct inet_frags *f, void *key, unsigned int hash)
{
	struct inet_frag_queue *q;
	struct hlist_node *n;

	hlist_for_each_entry(q, n, &f->hash[hash], list) {
		if (q->net == nf && f->match(q, key)) {
			atomic_inc(&q->refcnt);
			read_unlock(&f->lock);
			return q;
		}
	}
	read_unlock(&f->lock);

	return inet_frag_create(nf, f, key);
}
EXPORT_SYMBOL(inet_frag_find);
				       struct inet_frags *f, void *key,
				       unsigned int hash)
/* TODO : call from rcu_read_lock() and no longer use refcount_inc_not_zero() */
struct inet_frag_queue *inet_frag_find(struct netns_frags *nf, void *key)
{
	struct inet_frag_queue *fq = NULL, *prev;

	rcu_read_lock();
	prev = rhashtable_lookup(&nf->rhashtable, key, nf->f->rhash_params);
	if (!prev)
		fq = inet_frag_create(nf, key, &prev);
	if (prev && !IS_ERR(prev)) {
		fq = prev;
		if (!atomic_inc_not_zero(&fq->refcnt))
			fq = NULL;
	}
	rcu_read_unlock();
	return fq;
}
EXPORT_SYMBOL(inet_frag_find);
