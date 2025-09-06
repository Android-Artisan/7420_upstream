/*
 *  ebt_mark
 *
 *	Authors:
 *	Bart De Schuymer <bdschuym@pandora.be>
 *
 *  July, 2002
 *
 */

/* The mark target can be used in any chain,
 * I believe adding a mangle table just for marking is total overkill.
 * Marking a frame doesn't really change anything in the frame anyway.
 */

#include <linux/netfilter_bridge/ebtables.h>
#include <linux/netfilter_bridge/ebt_mark_t.h>
#include <linux/module.h>

static int ebt_target_mark(struct sk_buff *skb, unsigned int hooknr,
   const struct net_device *in, const struct net_device *out,
   const void *data, unsigned int datalen)
{
	const struct ebt_mark_t_info *info = data;
#include <linux/module.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_bridge/ebtables.h>
#include <linux/netfilter_bridge/ebt_mark_t.h>

static unsigned int
ebt_mark_tg(struct sk_buff *skb, const struct xt_action_param *par)
{
	const struct ebt_mark_t_info *info = par->targinfo;
	int action = info->target & -16;

	if (action == MARK_SET_VALUE)
		skb->mark = info->mark;
	else if (action == MARK_OR_VALUE)
		skb->mark |= info->mark;
	else if (action == MARK_AND_VALUE)
		skb->mark &= info->mark;
	else
		skb->mark ^= info->mark;

	return info->target | ~EBT_VERDICT_BITS;
}

static int ebt_target_mark_check(const char *tablename, unsigned int hookmask,
   const struct ebt_entry *e, void *data, unsigned int datalen)
{
	const struct ebt_mark_t_info *info = data;
	int tmp;

	if (datalen != EBT_ALIGN(sizeof(struct ebt_mark_t_info)))
		return -EINVAL;
	tmp = info->target | ~EBT_VERDICT_BITS;
	if (BASE_CHAIN && tmp == EBT_RETURN)
		return -EINVAL;
	CLEAR_BASE_CHAIN_BIT;
static int ebt_mark_tg_check(const struct xt_tgchk_param *par)
{
	const struct ebt_mark_t_info *info = par->targinfo;
	int tmp;

	tmp = info->target | ~EBT_VERDICT_BITS;
	if (BASE_CHAIN && tmp == EBT_RETURN)
		return -EINVAL;
	if (tmp < -NUM_STANDARD_TARGETS || tmp >= 0)
		return -EINVAL;
	tmp = info->target & ~EBT_VERDICT_BITS;
	if (tmp != MARK_SET_VALUE && tmp != MARK_OR_VALUE &&
	    tmp != MARK_AND_VALUE && tmp != MARK_XOR_VALUE)
		return -EINVAL;
	return 0;
}

static struct ebt_target mark_target __read_mostly = {
	.name		= EBT_MARK_TARGET,
	.target		= ebt_target_mark,
	.check		= ebt_target_mark_check,
#ifdef CONFIG_COMPAT
struct compat_ebt_mark_t_info {
	compat_ulong_t mark;
	compat_uint_t target;
};

static void mark_tg_compat_from_user(void *dst, const void *src)
{
	const struct compat_ebt_mark_t_info *user = src;
	struct ebt_mark_t_info *kern = dst;

	kern->mark = user->mark;
	kern->target = user->target;
}

static int mark_tg_compat_to_user(void __user *dst, const void *src)
{
	struct compat_ebt_mark_t_info __user *user = dst;
	const struct ebt_mark_t_info *kern = src;

	if (put_user(kern->mark, &user->mark) ||
	    put_user(kern->target, &user->target))
		return -EFAULT;
	return 0;
}
#endif

static struct xt_target ebt_mark_tg_reg __read_mostly = {
	.name		= "mark",
	.revision	= 0,
	.family		= NFPROTO_BRIDGE,
	.target		= ebt_mark_tg,
	.checkentry	= ebt_mark_tg_check,
	.targetsize	= sizeof(struct ebt_mark_t_info),
#ifdef CONFIG_COMPAT
	.compatsize	= sizeof(struct compat_ebt_mark_t_info),
	.compat_from_user = mark_tg_compat_from_user,
	.compat_to_user	= mark_tg_compat_to_user,
#endif
	.me		= THIS_MODULE,
};

static int __init ebt_mark_init(void)
{
	return ebt_register_target(&mark_target);
	return xt_register_target(&ebt_mark_tg_reg);
}

static void __exit ebt_mark_fini(void)
{
	ebt_unregister_target(&mark_target);
	xt_unregister_target(&ebt_mark_tg_reg);
}

module_init(ebt_mark_init);
module_exit(ebt_mark_fini);
MODULE_DESCRIPTION("Ebtables: Packet mark modification");
MODULE_LICENSE("GPL");
