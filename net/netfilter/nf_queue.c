#include <linux/kernel.h>
/*
 * Rusty Russell (C)2000 -- This code is GPL.
 * Patrick McHardy (c) 2006-2012
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/skbuff.h>
#include <linux/netfilter.h>
#include <linux/netfilter_bridge.h>
#include <linux/seq_file.h>
#include <linux/rcupdate.h>
#include <net/protocol.h>
#include <net/netfilter/nf_queue.h>
#include <net/dst.h>

#include "nf_internals.h"

/*
 * A queue handler may be registered for each protocol.  Each is protected by
 * long term mutex.  The handler must provide an an outfn() to accept packets
 * for queueing and must reinject all packets it receives, no matter what.
 */
static const struct nf_queue_handler *queue_handler[NPROTO];

static DEFINE_MUTEX(queue_handler_mutex);

/* return EBUSY when somebody else is registered, return EEXIST if the
 * same handler is registered, return 0 in case of success. */
int nf_register_queue_handler(int pf, const struct nf_queue_handler *qh)
{
	int ret;

	if (pf >= NPROTO)
		return -EINVAL;

	mutex_lock(&queue_handler_mutex);
	if (queue_handler[pf] == qh)
		ret = -EEXIST;
	else if (queue_handler[pf])
		ret = -EBUSY;
	else {
		rcu_assign_pointer(queue_handler[pf], qh);
		ret = 0;
	}
	mutex_unlock(&queue_handler_mutex);

	return ret;
 * Hook for nfnetlink_queue to register its queue handler.
 * We do this so that most of the NFQUEUE code can be modular.
 *
 * Once the queue is registered it must reinject all packets it
 * receives, no matter what.
 */

/* return EBUSY when somebody else is registered, return EEXIST if the
 * same handler is registered, return 0 in case of success. */
void nf_register_queue_handler(struct net *net, const struct nf_queue_handler *qh)
{
	/* should never happen, we only have one queueing backend in kernel */
	WARN_ON(rcu_access_pointer(net->nf.queue_handler));
	rcu_assign_pointer(net->nf.queue_handler, qh);
}
EXPORT_SYMBOL(nf_register_queue_handler);

/* The caller must flush their queue before this */
int nf_unregister_queue_handler(int pf, const struct nf_queue_handler *qh)
{
	if (pf >= NPROTO)
		return -EINVAL;

	mutex_lock(&queue_handler_mutex);
	if (queue_handler[pf] && queue_handler[pf] != qh) {
		mutex_unlock(&queue_handler_mutex);
		return -EINVAL;
	}

	rcu_assign_pointer(queue_handler[pf], NULL);
	mutex_unlock(&queue_handler_mutex);

	synchronize_rcu();

	return 0;
}
EXPORT_SYMBOL(nf_unregister_queue_handler);

void nf_unregister_queue_handlers(const struct nf_queue_handler *qh)
{
	int pf;

	mutex_lock(&queue_handler_mutex);
	for (pf = 0; pf < NPROTO; pf++)  {
		if (queue_handler[pf] == qh)
			rcu_assign_pointer(queue_handler[pf], NULL);
	}
	mutex_unlock(&queue_handler_mutex);

	synchronize_rcu();
}
EXPORT_SYMBOL_GPL(nf_unregister_queue_handlers);

static void nf_queue_entry_release_refs(struct nf_queue_entry *entry)
{
	/* Release those devices we held, or Alexey will kill me. */
	if (entry->indev)
		dev_put(entry->indev);
	if (entry->outdev)
		dev_put(entry->outdev);
#ifdef CONFIG_BRIDGE_NETFILTER
	if (entry->skb->nf_bridge) {
		struct nf_bridge_info *nf_bridge = entry->skb->nf_bridge;

		if (nf_bridge->physindev)
			dev_put(nf_bridge->physindev);
		if (nf_bridge->physoutdev)
			dev_put(nf_bridge->physoutdev);
	}
#endif
	/* Drop reference to owner of hook which queued us. */
	module_put(entry->elem->owner);
void nf_unregister_queue_handler(void)
void nf_unregister_queue_handler(struct net *net)
{
	RCU_INIT_POINTER(net->nf.queue_handler, NULL);
}
EXPORT_SYMBOL(nf_unregister_queue_handler);

static void nf_queue_sock_put(struct sock *sk)
{
#ifdef CONFIG_INET
	sock_gen_put(sk);
#else
	sock_put(sk);
#endif
}

void nf_queue_entry_release_refs(struct nf_queue_entry *entry)
{
	struct nf_hook_state *state = &entry->state;

	/* Release those devices we held, or Alexey will kill me. */
	if (state->in)
		dev_put(state->in);
	if (state->out)
		dev_put(state->out);
	if (state->sk)
		nf_queue_sock_put(state->sk);
#if IS_ENABLED(CONFIG_BRIDGE_NETFILTER)
	if (entry->skb->nf_bridge) {
		struct net_device *physdev;

		physdev = nf_bridge_get_physindev(entry->skb);
		if (physdev)
			dev_put(physdev);
		physdev = nf_bridge_get_physoutdev(entry->skb);
		if (physdev)
			dev_put(physdev);
	}
#endif
}
EXPORT_SYMBOL_GPL(nf_queue_entry_release_refs);

/* Bump dev refs so they don't vanish while packet is out */
bool nf_queue_entry_get_refs(struct nf_queue_entry *entry)
{
	struct nf_hook_state *state = &entry->state;

	if (state->sk && !atomic_inc_not_zero(&state->sk->sk_refcnt))
		return false;

	if (state->in)
		dev_hold(state->in);
	if (state->out)
		dev_hold(state->out);
	if (state->sk)
		sock_hold(state->sk);
#if IS_ENABLED(CONFIG_BRIDGE_NETFILTER)
	if (entry->skb->nf_bridge) {
		struct net_device *physdev;

		physdev = nf_bridge_get_physindev(entry->skb);
		if (physdev)
			dev_hold(physdev);
		physdev = nf_bridge_get_physoutdev(entry->skb);
		if (physdev)
			dev_hold(physdev);
	}
#endif
	return true;
}
EXPORT_SYMBOL_GPL(nf_queue_entry_get_refs);

void nf_queue_nf_hook_drop(struct net *net, struct nf_hook_ops *ops)
{
	const struct nf_queue_handler *qh;

	rcu_read_lock();
	qh = rcu_dereference(net->nf.queue_handler);
	if (qh)
		qh->nf_hook_drop(net, ops);
	rcu_read_unlock();
}

/*
 * Any packet that leaves via this function must come back
 * through nf_reinject().
 */
static int __nf_queue(struct sk_buff *skb,
		      struct list_head *elem,
		      int pf, unsigned int hook,
		      struct net_device *indev,
		      struct net_device *outdev,
		      int (*okfn)(struct sk_buff *),
		      unsigned int queuenum)
{
	int status;
	struct nf_queue_entry *entry = NULL;
#ifdef CONFIG_BRIDGE_NETFILTER
	struct net_device *physindev;
	struct net_device *physoutdev;
#endif
	const struct nf_afinfo *afinfo;
	const struct nf_queue_handler *qh;

	/* QUEUE == DROP if noone is waiting, to be safe. */
	rcu_read_lock();

	qh = rcu_dereference(queue_handler[pf]);
	if (!qh)
		goto err_unlock;

	afinfo = nf_get_afinfo(pf);
	if (!afinfo)
		goto err_unlock;

	entry = kmalloc(sizeof(*entry) + afinfo->route_key_size, GFP_ATOMIC);
	if (!entry)
		goto err_unlock;

	*entry = (struct nf_queue_entry) {
		.skb	= skb,
		.elem	= list_entry(elem, struct nf_hook_ops, list),
		.pf	= pf,
		.hook	= hook,
		.indev	= indev,
		.outdev	= outdev,
		.okfn	= okfn,
	};

	/* If it's going away, ignore hook. */
	if (!try_module_get(entry->elem->owner)) {
		rcu_read_unlock();
		kfree(entry);
		return 0;
	}

	/* Bump dev refs so they don't vanish while packet is out */
	if (indev)
		dev_hold(indev);
	if (outdev)
		dev_hold(outdev);
#ifdef CONFIG_BRIDGE_NETFILTER
	if (skb->nf_bridge) {
		physindev = skb->nf_bridge->physindev;
		if (physindev)
			dev_hold(physindev);
		physoutdev = skb->nf_bridge->physoutdev;
		if (physoutdev)
			dev_hold(physoutdev);
	}
#endif
	afinfo->saveroute(skb, entry);
	status = qh->outfn(entry, queuenum);

	rcu_read_unlock();

int nf_queue(struct sk_buff *skb,
	     struct nf_hook_ops *elem,
	     struct nf_hook_state *state,
	     unsigned int queuenum)
{
	int status = -ENOENT;
	struct nf_queue_entry *entry = NULL;
	const struct nf_afinfo *afinfo;
	const struct nf_queue_handler *qh;
	struct net *net = state->net;

	/* QUEUE == DROP if no one is waiting, to be safe. */
	qh = rcu_dereference(net->nf.queue_handler);
	if (!qh) {
		status = -ESRCH;
		goto err;
	}

	afinfo = nf_get_afinfo(state->pf);
	if (!afinfo)
		goto err;

	entry = kmalloc(sizeof(*entry) + afinfo->route_key_size, GFP_ATOMIC);
	if (!entry) {
		status = -ENOMEM;
		goto err;
	}

	*entry = (struct nf_queue_entry) {
		.skb	= skb,
		.elem	= elem,
		.state	= *state,
		.size	= sizeof(*entry) + afinfo->route_key_size,
	};

	if (!nf_queue_entry_get_refs(entry)) {
		kfree(entry);
		return -ENOTCONN;
	}

	skb_dst_force(skb);
	afinfo->saveroute(skb, entry);
	status = qh->outfn(entry, queuenum);

	if (status < 0) {
		nf_queue_entry_release_refs(entry);
		goto err;
	}

	return 1;

err_unlock:
	rcu_read_unlock();
err:
	kfree_skb(skb);
	kfree(entry);
	return 1;
}

int nf_queue(struct sk_buff *skb,
	     struct list_head *elem,
	     int pf, unsigned int hook,
	     struct net_device *indev,
	     struct net_device *outdev,
	     int (*okfn)(struct sk_buff *),
	     unsigned int queuenum)
{
	struct sk_buff *segs;

	if (!skb_is_gso(skb))
		return __nf_queue(skb, elem, pf, hook, indev, outdev, okfn,
				  queuenum);

	switch (pf) {
	case AF_INET:
		skb->protocol = htons(ETH_P_IP);
		break;
	case AF_INET6:
		skb->protocol = htons(ETH_P_IPV6);
		break;
	}

	segs = skb_gso_segment(skb, 0);
	kfree_skb(skb);
	if (IS_ERR(segs))
		return 1;

	do {
		struct sk_buff *nskb = segs->next;

		segs->next = NULL;
		if (!__nf_queue(segs, elem, pf, hook, indev, outdev, okfn,
				queuenum))
			kfree_skb(segs);
		segs = nskb;
	} while (segs);
	return 1;
	return 0;

err:
	kfree(entry);
	return status;
}

void nf_reinject(struct nf_queue_entry *entry, unsigned int verdict)
{
	struct sk_buff *skb = entry->skb;
	struct list_head *elem = &entry->elem->list;
	const struct nf_afinfo *afinfo;

	rcu_read_lock();
	struct nf_hook_ops *elem = entry->elem;
	const struct nf_afinfo *afinfo;
	int err;

	nf_queue_entry_release_refs(entry);

	/* Continue traversal iff userspace said ok... */
	if (verdict == NF_REPEAT) {
		elem = elem->prev;
		verdict = NF_ACCEPT;
	}

	if (verdict == NF_ACCEPT) {
		afinfo = nf_get_afinfo(entry->pf);
		if (!afinfo || afinfo->reroute(skb, entry) < 0)
			verdict = NF_DROP;
	}

	if (verdict == NF_ACCEPT) {
	next_hook:
		verdict = nf_iterate(&nf_hooks[entry->pf][entry->hook],
				     skb, entry->hook,
				     entry->indev, entry->outdev, &elem,
				     entry->okfn, INT_MIN);
	if (verdict == NF_REPEAT)
		verdict = elem->hook(elem->priv, skb, &entry->state);

	if (verdict == NF_ACCEPT) {
		afinfo = nf_get_afinfo(entry->state.pf);
		if (!afinfo || afinfo->reroute(entry->state.net, skb, entry) < 0)
			verdict = NF_DROP;
	}

	entry->state.thresh = INT_MIN;

	if (verdict == NF_ACCEPT) {
	next_hook:
		verdict = nf_iterate(entry->state.hook_list,
				     skb, &entry->state, &elem);
	}

	switch (verdict & NF_VERDICT_MASK) {
	case NF_ACCEPT:
	case NF_STOP:
		local_bh_disable();
		entry->okfn(skb);
		local_bh_enable();
	case NF_STOLEN:
		break;
	case NF_QUEUE:
		if (!__nf_queue(skb, elem, entry->pf, entry->hook,
				entry->indev, entry->outdev, entry->okfn,
				verdict >> NF_VERDICT_BITS))
			goto next_hook;
		entry->state.okfn(entry->state.net, entry->state.sk, skb);
		local_bh_enable();
		break;
	case NF_QUEUE:
		err = nf_queue(skb, elem, &entry->state,
			       verdict >> NF_VERDICT_QBITS);
		if (err < 0) {
			if (err == -ESRCH &&
			   (verdict & NF_VERDICT_FLAG_QUEUE_BYPASS))
				goto next_hook;
			kfree_skb(skb);
		}
		break;
	case NF_STOLEN:
		break;
	default:
		kfree_skb(skb);
	}
	rcu_read_unlock();
	kfree(entry);
	return;
}
EXPORT_SYMBOL(nf_reinject);

#ifdef CONFIG_PROC_FS
static void *seq_start(struct seq_file *seq, loff_t *pos)
{
	if (*pos >= NPROTO)
		return NULL;

	return pos;
}

static void *seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	(*pos)++;

	if (*pos >= NPROTO)
		return NULL;

	return pos;
}

static void seq_stop(struct seq_file *s, void *v)
{

}

static int seq_show(struct seq_file *s, void *v)
{
	int ret;
	loff_t *pos = v;
	const struct nf_queue_handler *qh;

	rcu_read_lock();
	qh = rcu_dereference(queue_handler[*pos]);
	if (!qh)
		ret = seq_printf(s, "%2lld NONE\n", *pos);
	else
		ret = seq_printf(s, "%2lld %s\n", *pos, qh->name);
	rcu_read_unlock();

	return ret;
}

static const struct seq_operations nfqueue_seq_ops = {
	.start	= seq_start,
	.next	= seq_next,
	.stop	= seq_stop,
	.show	= seq_show,
};

static int nfqueue_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &nfqueue_seq_ops);
}

static const struct file_operations nfqueue_file_ops = {
	.owner	 = THIS_MODULE,
	.open	 = nfqueue_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release,
};
#endif /* PROC_FS */


int __init netfilter_queue_init(void)
{
#ifdef CONFIG_PROC_FS
	if (!proc_create("nf_queue", S_IRUGO,
			 proc_net_netfilter, &nfqueue_file_ops))
		return -1;
#endif
	return 0;
}


	kfree(entry);
}
EXPORT_SYMBOL(nf_reinject);
