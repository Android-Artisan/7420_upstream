#ifndef __BEN_VLAN_802_1Q_INC__
#define __BEN_VLAN_802_1Q_INC__

#include <linux/if_vlan.h>


/**
 *	struct vlan_priority_tci_mapping - vlan egress priority mappings
 *	@priority: skb priority
 *	@vlan_qos: vlan priority: (skb->priority << 13) & 0xE000
 *	@next: pointer to next struct
 */
struct vlan_priority_tci_mapping {
	u32					priority;
	u16					vlan_qos;
	struct vlan_priority_tci_mapping	*next;
};

/**
 *	struct vlan_dev_info - VLAN private device data
 *	@nr_ingress_mappings: number of ingress priority mappings
 *	@ingress_priority_map: ingress priority mappings
 *	@nr_egress_mappings: number of egress priority mappings
 *	@egress_priority_map: hash of egress priority mappings
 *	@vlan_id: VLAN identifier
 *	@flags: device flags
 *	@real_dev: underlying netdevice
 *	@real_dev_addr: address of underlying netdevice
 *	@dent: proc dir entry
 *	@cnt_inc_headroom_on_tx: statistic - number of skb expansions on TX
 *	@cnt_encap_on_xmit: statistic - number of skb encapsulations on TX
 */
struct vlan_dev_info {
	unsigned int				nr_ingress_mappings;
	u32					ingress_priority_map[8];
	unsigned int				nr_egress_mappings;
	struct vlan_priority_tci_mapping	*egress_priority_map[16];

	u16					vlan_id;
	u16					flags;

	struct net_device			*real_dev;
	unsigned char				real_dev_addr[ETH_ALEN];

	struct proc_dir_entry			*dent;
	unsigned long				cnt_inc_headroom_on_tx;
	unsigned long				cnt_encap_on_xmit;
};

static inline struct vlan_dev_info *vlan_dev_info(const struct net_device *dev)
{
	return netdev_priv(dev);
}

#define VLAN_GRP_HASH_SHIFT	5
#define VLAN_GRP_HASH_SIZE	(1 << VLAN_GRP_HASH_SHIFT)
#define VLAN_GRP_HASH_MASK	(VLAN_GRP_HASH_SIZE - 1)

/*  Find a VLAN device by the MAC address of its Ethernet device, and
 *  it's VLAN ID.  The default configuration is to have VLAN's scope
 *  to be box-wide, so the MAC will be ignored.  The mac will only be
 *  looked at if we are configured to have a separate set of VLANs per
 *  each MAC addressable interface.  Note that this latter option does
 *  NOT follow the spec for VLANs, but may be useful for doing very
 *  large quantities of VLAN MUX/DEMUX onto FrameRelay or ATM PVCs.
 *
 *  Must be invoked with rcu_read_lock (ie preempt disabled)
 *  or with RTNL.
 */
struct net_device *__find_vlan_dev(struct net_device *real_dev, u16 vlan_id);

/* found in vlan_dev.c */
int vlan_skb_recv(struct sk_buff *skb, struct net_device *dev,
		  struct packet_type *ptype, struct net_device *orig_dev);
#include <linux/u64_stats_sync.h>
#include <linux/list.h>

/* if this changes, algorithm will have to be reworked because this
 * depends on completely exhausting the VLAN identifier space.  Thus
 * it gives constant time look-up, but in many cases it wastes memory.
 */
#define VLAN_GROUP_ARRAY_SPLIT_PARTS  8
#define VLAN_GROUP_ARRAY_PART_LEN     (VLAN_N_VID/VLAN_GROUP_ARRAY_SPLIT_PARTS)

enum vlan_protos {
	VLAN_PROTO_8021Q	= 0,
	VLAN_PROTO_8021AD,
	VLAN_PROTO_NUM,
};

struct vlan_group {
	unsigned int		nr_vlan_devs;
	struct hlist_node	hlist;	/* linked list */
	struct net_device **vlan_devices_arrays[VLAN_PROTO_NUM]
					       [VLAN_GROUP_ARRAY_SPLIT_PARTS];
};

struct vlan_info {
	struct net_device	*real_dev; /* The ethernet(like) device
					    * the vlan is attached to.
					    */
	struct vlan_group	grp;
	struct list_head	vid_list;
	unsigned int		nr_vids;
	struct rcu_head		rcu;
};

static inline unsigned int vlan_proto_idx(__be16 proto)
{
	switch (proto) {
	case htons(ETH_P_8021Q):
		return VLAN_PROTO_8021Q;
	case htons(ETH_P_8021AD):
		return VLAN_PROTO_8021AD;
	default:
		BUG();
		return 0;
	}
}

static inline struct net_device *__vlan_group_get_device(struct vlan_group *vg,
							 unsigned int pidx,
							 u16 vlan_id)
{
	struct net_device **array;

	array = vg->vlan_devices_arrays[pidx]
				       [vlan_id / VLAN_GROUP_ARRAY_PART_LEN];
	return array ? array[vlan_id % VLAN_GROUP_ARRAY_PART_LEN] : NULL;
}

static inline struct net_device *vlan_group_get_device(struct vlan_group *vg,
						       __be16 vlan_proto,
						       u16 vlan_id)
{
	return __vlan_group_get_device(vg, vlan_proto_idx(vlan_proto), vlan_id);
}

static inline void vlan_group_set_device(struct vlan_group *vg,
					 __be16 vlan_proto, u16 vlan_id,
					 struct net_device *dev)
{
	struct net_device **array;
	if (!vg)
		return;
	array = vg->vlan_devices_arrays[vlan_proto_idx(vlan_proto)]
				       [vlan_id / VLAN_GROUP_ARRAY_PART_LEN];
	array[vlan_id % VLAN_GROUP_ARRAY_PART_LEN] = dev;
}

/* Must be invoked with rcu_read_lock or with RTNL. */
static inline struct net_device *vlan_find_dev(struct net_device *real_dev,
					       __be16 vlan_proto, u16 vlan_id)
{
	struct vlan_info *vlan_info = rcu_dereference_rtnl(real_dev->vlan_info);

	if (vlan_info)
		return vlan_group_get_device(&vlan_info->grp,
					     vlan_proto, vlan_id);

	return NULL;
}

#define vlan_group_for_each_dev(grp, i, dev) \
	for ((i) = 0; i < VLAN_PROTO_NUM * VLAN_N_VID; i++) \
		if (((dev) = __vlan_group_get_device((grp), (i) / VLAN_N_VID, \
							    (i) % VLAN_N_VID)))

/* found in vlan_dev.c */
void vlan_dev_set_ingress_priority(const struct net_device *dev,
				   u32 skb_prio, u16 vlan_prio);
int vlan_dev_set_egress_priority(const struct net_device *dev,
				 u32 skb_prio, u16 vlan_prio);
int vlan_dev_change_flags(const struct net_device *dev, u32 flag, u32 mask);
void vlan_dev_get_realdev_name(const struct net_device *dev, char *result);

int vlan_check_real_dev(struct net_device *real_dev, u16 vlan_id);
void vlan_setup(struct net_device *dev);
int register_vlan_dev(struct net_device *dev);
void unregister_vlan_dev(struct net_device *dev);
int vlan_check_real_dev(struct net_device *real_dev,
			__be16 protocol, u16 vlan_id);
void vlan_setup(struct net_device *dev);
int register_vlan_dev(struct net_device *dev);
void unregister_vlan_dev(struct net_device *dev, struct list_head *head);
void vlan_dev_uninit(struct net_device *dev);
bool vlan_dev_inherit_address(struct net_device *dev,
			      struct net_device *real_dev);

static inline u32 vlan_get_ingress_priority(struct net_device *dev,
					    u16 vlan_tci)
{
	struct vlan_dev_info *vip = vlan_dev_info(dev);

	return vip->ingress_priority_map[(vlan_tci >> 13) & 0x7];
}

#ifdef CONFIG_VLAN_8021Q_GVRP
extern int vlan_gvrp_request_join(const struct net_device *dev);
extern void vlan_gvrp_request_leave(const struct net_device *dev);
extern int vlan_gvrp_init_applicant(struct net_device *dev);
extern void vlan_gvrp_uninit_applicant(struct net_device *dev);
extern int vlan_gvrp_init(void);
extern void vlan_gvrp_uninit(void);
	struct vlan_dev_priv *vip = vlan_dev_priv(dev);

	return vip->ingress_priority_map[(vlan_tci >> VLAN_PRIO_SHIFT) & 0x7];
}

#ifdef CONFIG_VLAN_8021Q_GVRP
int vlan_gvrp_request_join(const struct net_device *dev);
void vlan_gvrp_request_leave(const struct net_device *dev);
int vlan_gvrp_init_applicant(struct net_device *dev);
void vlan_gvrp_uninit_applicant(struct net_device *dev);
int vlan_gvrp_init(void);
void vlan_gvrp_uninit(void);
#else
static inline int vlan_gvrp_request_join(const struct net_device *dev) { return 0; }
static inline void vlan_gvrp_request_leave(const struct net_device *dev) {}
static inline int vlan_gvrp_init_applicant(struct net_device *dev) { return 0; }
static inline void vlan_gvrp_uninit_applicant(struct net_device *dev) {}
static inline int vlan_gvrp_init(void) { return 0; }
static inline void vlan_gvrp_uninit(void) {}
#endif

#ifdef CONFIG_VLAN_8021Q_MVRP
int vlan_mvrp_request_join(const struct net_device *dev);
void vlan_mvrp_request_leave(const struct net_device *dev);
int vlan_mvrp_init_applicant(struct net_device *dev);
void vlan_mvrp_uninit_applicant(struct net_device *dev);
int vlan_mvrp_init(void);
void vlan_mvrp_uninit(void);
#else
static inline int vlan_mvrp_request_join(const struct net_device *dev) { return 0; }
static inline void vlan_mvrp_request_leave(const struct net_device *dev) {}
static inline int vlan_mvrp_init_applicant(struct net_device *dev) { return 0; }
static inline void vlan_mvrp_uninit_applicant(struct net_device *dev) {}
static inline int vlan_mvrp_init(void) { return 0; }
static inline void vlan_mvrp_uninit(void) {}
#endif

extern const char vlan_fullname[];
extern const char vlan_version[];
int vlan_netlink_init(void);
void vlan_netlink_fini(void);

extern struct rtnl_link_ops vlan_link_ops;

static inline int is_vlan_dev(struct net_device *dev)
{
	return dev->priv_flags & IFF_802_1Q_VLAN;
}

extern int vlan_net_id;

struct proc_dir_entry;

struct vlan_net {
	/* /proc/net/vlan */
	struct proc_dir_entry *proc_vlan_dir;
	/* /proc/net/vlan/config */
	struct proc_dir_entry *proc_vlan_conf;
	/* Determines interface naming scheme. */
	unsigned short name_type;
};

#endif /* !(__BEN_VLAN_802_1Q_INC__) */
