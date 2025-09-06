/*
*  linux/fs/nfsd/nfs4state.c
*
*  Copyright (c) 2001 The Regents of the University of Michigan.
*  All rights reserved.
*
*  Kendrick Smith <kmsmith@umich.edu>
*  Andy Adamson <kandros@umich.edu>
*
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
*
*  1. Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*  2. Redistributions in binary form must reproduce the above copyright
*     notice, this list of conditions and the following disclaimer in the
*     documentation and/or other materials provided with the distribution.
*  3. Neither the name of the University nor the names of its
*     contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
*  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
*  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
*  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
*  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
*  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
*  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
*  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
*  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
*  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
*  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
*/

#include <linux/param.h>
#include <linux/major.h>
#include <linux/slab.h>

#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/cache.h>
#include <linux/file.h>
#include <linux/mount.h>
#include <linux/workqueue.h>
#include <linux/smp_lock.h>
#include <linux/kthread.h>
#include <linux/nfs4.h>
#include <linux/nfsd/state.h>
#include <linux/nfsd/xdr4.h>
#include <linux/namei.h>
#include <linux/swap.h>
#include <linux/mutex.h>
#include <linux/lockd/bind.h>
#include <linux/module.h>

#define NFSDDBG_FACILITY                NFSDDBG_PROC

/* Globals */
static time_t lease_time = 90;     /* default lease time */
static time_t user_lease_time = 90;
static time_t boot_time;
static int in_grace = 1;
static u32 current_ownerid = 1;
static u32 current_fileid = 1;
static u32 current_delegid = 1;
static u32 nfs4_init;
static stateid_t zerostateid;             /* bits all 0 */
static stateid_t onestateid;              /* bits all 1 */

#define ZERO_STATEID(stateid) (!memcmp((stateid), &zerostateid, sizeof(stateid_t)))
#define ONE_STATEID(stateid)  (!memcmp((stateid), &onestateid, sizeof(stateid_t)))

/* forward declarations */
static struct nfs4_stateid * find_stateid(stateid_t *stid, int flags);
static struct nfs4_delegation * find_delegation_stateid(struct inode *ino, stateid_t *stid);
static void release_stateid_lockowners(struct nfs4_stateid *open_stp);
static char user_recovery_dirname[PATH_MAX] = "/var/lib/nfs/v4recovery";
static void nfs4_set_recdir(char *recdir);

/* Locking:
 *
 * client_mutex:
 * 	protects clientid_hashtbl[], clientstr_hashtbl[],
 * 	unconfstr_hashtbl[], uncofid_hashtbl[].
 */
static DEFINE_MUTEX(client_mutex);

static struct kmem_cache *stateowner_slab = NULL;
static struct kmem_cache *file_slab = NULL;
static struct kmem_cache *stateid_slab = NULL;
static struct kmem_cache *deleg_slab = NULL;

void
nfs4_lock_state(void)
{
	mutex_lock(&client_mutex);
}

void
nfs4_unlock_state(void)
{
	mutex_unlock(&client_mutex);
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/namei.h>
#include <linux/swap.h>
#include <linux/pagemap.h>
#include <linux/ratelimit.h>
#include <linux/sunrpc/svcauth_gss.h>
#include <linux/sunrpc/addr.h>
#include <linux/jhash.h>
#include "xdr4.h"
#include "xdr4cb.h"
#include "vfs.h"
#include "current_stateid.h"

#include "netns.h"
#include "pnfs.h"

#define NFSDDBG_FACILITY                NFSDDBG_PROC

#define all_ones {{~0,~0},~0}
static const stateid_t one_stateid = {
	.si_generation = ~0,
	.si_opaque = all_ones,
};
static const stateid_t zero_stateid = {
	/* all fields zero */
};
static const stateid_t currentstateid = {
	.si_generation = 1,
};
static const stateid_t close_stateid = {
	.si_generation = 0xffffffffU,
};

static u64 current_sessionid = 1;

#define ZERO_STATEID(stateid) (!memcmp((stateid), &zero_stateid, sizeof(stateid_t)))
#define ONE_STATEID(stateid)  (!memcmp((stateid), &one_stateid, sizeof(stateid_t)))
#define CURRENT_STATEID(stateid) (!memcmp((stateid), &currentstateid, sizeof(stateid_t)))
#define CLOSE_STATEID(stateid)  (!memcmp((stateid), &close_stateid, sizeof(stateid_t)))

/* forward declarations */
static bool check_for_locks(struct nfs4_file *fp, struct nfs4_lockowner *lowner);
static void nfs4_free_ol_stateid(struct nfs4_stid *stid);

/* Locking: */

/*
 * Currently used for the del_recall_lru and file hash table.  In an
 * effort to decrease the scope of the client_mutex, this spinlock may
 * eventually cover more:
 */
static DEFINE_SPINLOCK(state_lock);

/*
 * A waitqueue for all in-progress 4.0 CLOSE operations that are waiting for
 * the refcount on the open stateid to drop.
 */
static DECLARE_WAIT_QUEUE_HEAD(close_wq);

static struct kmem_cache *openowner_slab;
static struct kmem_cache *lockowner_slab;
static struct kmem_cache *file_slab;
static struct kmem_cache *stateid_slab;
static struct kmem_cache *deleg_slab;
static struct kmem_cache *odstate_slab;

static void free_session(struct nfsd4_session *);

static struct nfsd4_callback_ops nfsd4_cb_recall_ops;

static bool is_session_dead(struct nfsd4_session *ses)
{
	return ses->se_flags & NFS4_SESSION_DEAD;
}

static __be32 mark_session_dead_locked(struct nfsd4_session *ses, int ref_held_by_me)
{
	if (atomic_read(&ses->se_ref) > ref_held_by_me)
		return nfserr_jukebox;
	ses->se_flags |= NFS4_SESSION_DEAD;
	return nfs_ok;
}

static bool is_client_expired(struct nfs4_client *clp)
{
	return clp->cl_time == 0;
}

static __be32 get_client_locked(struct nfs4_client *clp)
{
	struct nfsd_net *nn = net_generic(clp->net, nfsd_net_id);

	lockdep_assert_held(&nn->client_lock);

	if (is_client_expired(clp))
		return nfserr_expired;
	atomic_inc(&clp->cl_refcount);
	return nfs_ok;
}

/* must be called under the client_lock */
static inline void
renew_client_locked(struct nfs4_client *clp)
{
	struct nfsd_net *nn = net_generic(clp->net, nfsd_net_id);

	if (is_client_expired(clp)) {
		WARN_ON(1);
		printk("%s: client (clientid %08x/%08x) already expired\n",
			__func__,
			clp->cl_clientid.cl_boot,
			clp->cl_clientid.cl_id);
		return;
	}

	dprintk("renewing client (clientid %08x/%08x)\n",
			clp->cl_clientid.cl_boot,
			clp->cl_clientid.cl_id);
	list_move_tail(&clp->cl_lru, &nn->client_lru);
	clp->cl_time = get_seconds();
}

static void put_client_renew_locked(struct nfs4_client *clp)
{
	struct nfsd_net *nn = net_generic(clp->net, nfsd_net_id);

	lockdep_assert_held(&nn->client_lock);

	if (!atomic_dec_and_test(&clp->cl_refcount))
		return;
	if (!is_client_expired(clp))
		renew_client_locked(clp);
}

static void put_client_renew(struct nfs4_client *clp)
{
	struct nfsd_net *nn = net_generic(clp->net, nfsd_net_id);

	if (!atomic_dec_and_lock(&clp->cl_refcount, &nn->client_lock))
		return;
	if (!is_client_expired(clp))
		renew_client_locked(clp);
	spin_unlock(&nn->client_lock);
}

static __be32 nfsd4_get_session_locked(struct nfsd4_session *ses)
{
	__be32 status;

	if (is_session_dead(ses))
		return nfserr_badsession;
	status = get_client_locked(ses->se_client);
	if (status)
		return status;
	atomic_inc(&ses->se_ref);
	return nfs_ok;
}

static void nfsd4_put_session_locked(struct nfsd4_session *ses)
{
	struct nfs4_client *clp = ses->se_client;
	struct nfsd_net *nn = net_generic(clp->net, nfsd_net_id);

	lockdep_assert_held(&nn->client_lock);

	if (atomic_dec_and_test(&ses->se_ref) && is_session_dead(ses))
		free_session(ses);
	put_client_renew_locked(clp);
}

static void nfsd4_put_session(struct nfsd4_session *ses)
{
	struct nfs4_client *clp = ses->se_client;
	struct nfsd_net *nn = net_generic(clp->net, nfsd_net_id);

	spin_lock(&nn->client_lock);
	nfsd4_put_session_locked(ses);
	spin_unlock(&nn->client_lock);
}

static inline struct nfs4_stateowner *
nfs4_get_stateowner(struct nfs4_stateowner *sop)
{
	atomic_inc(&sop->so_count);
	return sop;
}

static int
same_owner_str(struct nfs4_stateowner *sop, struct xdr_netobj *owner)
{
	return (sop->so_owner.len == owner->len) &&
		0 == memcmp(sop->so_owner.data, owner->data, owner->len);
}

static struct nfs4_openowner *
find_openstateowner_str_locked(unsigned int hashval, struct nfsd4_open *open,
			struct nfs4_client *clp)
{
	struct nfs4_stateowner *so;

	lockdep_assert_held(&clp->cl_lock);

	list_for_each_entry(so, &clp->cl_ownerstr_hashtbl[hashval],
			    so_strhash) {
		if (!so->so_is_open_owner)
			continue;
		if (same_owner_str(so, &open->op_owner))
			return openowner(nfs4_get_stateowner(so));
	}
	return NULL;
}

static struct nfs4_openowner *
find_openstateowner_str(unsigned int hashval, struct nfsd4_open *open,
			struct nfs4_client *clp)
{
	struct nfs4_openowner *oo;

	spin_lock(&clp->cl_lock);
	oo = find_openstateowner_str_locked(hashval, open, clp);
	spin_unlock(&clp->cl_lock);
	return oo;
}

static inline u32
opaque_hashval(const void *ptr, int nbytes)
{
	unsigned char *cptr = (unsigned char *) ptr;

	u32 x = 0;
	while (nbytes--) {
		x *= 37;
		x += *cptr++;
	}
	return x;
}

/* forward declarations */
static void release_stateowner(struct nfs4_stateowner *sop);
static void release_stateid(struct nfs4_stateid *stp, int flags);

/*
 * Delegation state
 */

/* recall_lock protects the del_recall_lru */
static DEFINE_SPINLOCK(recall_lock);
static struct list_head del_recall_lru;

static void
free_nfs4_file(struct kref *kref)
{
	struct nfs4_file *fp = container_of(kref, struct nfs4_file, fi_ref);
	list_del(&fp->fi_hash);
	iput(fp->fi_inode);
	kmem_cache_free(file_slab, fp);
}

static inline void
put_nfs4_file(struct nfs4_file *fi)
{
	kref_put(&fi->fi_ref, free_nfs4_file);
}

static inline void
get_nfs4_file(struct nfs4_file *fi)
{
	kref_get(&fi->fi_ref);
}

static int num_delegations;
unsigned int max_delegations;
static void nfsd4_free_file_rcu(struct rcu_head *rcu)
{
	struct nfs4_file *fp = container_of(rcu, struct nfs4_file, fi_rcu);

	kmem_cache_free(file_slab, fp);
}

void
put_nfs4_file(struct nfs4_file *fi)
{
	might_lock(&state_lock);

	if (atomic_dec_and_lock(&fi->fi_ref, &state_lock)) {
		hlist_del_rcu(&fi->fi_hash);
		spin_unlock(&state_lock);
		WARN_ON_ONCE(!list_empty(&fi->fi_clnt_odstate));
		WARN_ON_ONCE(!list_empty(&fi->fi_delegations));
		call_rcu(&fi->fi_rcu, nfsd4_free_file_rcu);
	}
}

static struct file *
__nfs4_get_fd(struct nfs4_file *f, int oflag)
{
	if (f->fi_fds[oflag])
		return get_file(f->fi_fds[oflag]);
	return NULL;
}

static struct file *
find_writeable_file_locked(struct nfs4_file *f)
{
	struct file *ret;

	lockdep_assert_held(&f->fi_lock);

	ret = __nfs4_get_fd(f, O_WRONLY);
	if (!ret)
		ret = __nfs4_get_fd(f, O_RDWR);
	return ret;
}

static struct file *
find_writeable_file(struct nfs4_file *f)
{
	struct file *ret;

	spin_lock(&f->fi_lock);
	ret = find_writeable_file_locked(f);
	spin_unlock(&f->fi_lock);

	return ret;
}

static struct file *find_readable_file_locked(struct nfs4_file *f)
{
	struct file *ret;

	lockdep_assert_held(&f->fi_lock);

	ret = __nfs4_get_fd(f, O_RDONLY);
	if (!ret)
		ret = __nfs4_get_fd(f, O_RDWR);
	return ret;
}

static struct file *
find_readable_file(struct nfs4_file *f)
{
	struct file *ret;

	spin_lock(&f->fi_lock);
	ret = find_readable_file_locked(f);
	spin_unlock(&f->fi_lock);

	return ret;
}

struct file *
find_any_file(struct nfs4_file *f)
{
	struct file *ret;

	spin_lock(&f->fi_lock);
	ret = __nfs4_get_fd(f, O_RDWR);
	if (!ret) {
		ret = __nfs4_get_fd(f, O_WRONLY);
		if (!ret)
			ret = __nfs4_get_fd(f, O_RDONLY);
	}
	spin_unlock(&f->fi_lock);
	return ret;
}

static atomic_long_t num_delegations;
unsigned long max_delegations;

/*
 * Open owner state (share locks)
 */

/* hash tables for nfs4_stateowner */
/* hash tables for lock and open owners */
#define OWNER_HASH_BITS              8
#define OWNER_HASH_SIZE             (1 << OWNER_HASH_BITS)
#define OWNER_HASH_MASK             (OWNER_HASH_SIZE - 1)

#define ownerid_hashval(id) \
        ((id) & OWNER_HASH_MASK)
#define ownerstr_hashval(clientid, ownername) \
        (((clientid) + opaque_hashval((ownername.data), (ownername.len))) & OWNER_HASH_MASK)

static struct list_head	ownerid_hashtbl[OWNER_HASH_SIZE];
static struct list_head	ownerstr_hashtbl[OWNER_HASH_SIZE];
static unsigned int ownerstr_hashval(struct xdr_netobj *ownername)
{
	unsigned int ret;

	ret = opaque_hashval(ownername->data, ownername->len);
	return ret & OWNER_HASH_MASK;
}

/* hash table for nfs4_file */
#define FILE_HASH_BITS                   8
#define FILE_HASH_SIZE                  (1 << FILE_HASH_BITS)
#define FILE_HASH_MASK                  (FILE_HASH_SIZE - 1)
/* hash table for (open)nfs4_stateid */
#define STATEID_HASH_BITS              10
#define STATEID_HASH_SIZE              (1 << STATEID_HASH_BITS)
#define STATEID_HASH_MASK              (STATEID_HASH_SIZE - 1)

#define file_hashval(x) \
        hash_ptr(x, FILE_HASH_BITS)
#define stateid_hashval(owner_id, file_id)  \
        (((owner_id) + (file_id)) & STATEID_HASH_MASK)

static struct list_head file_hashtbl[FILE_HASH_SIZE];
static struct list_head stateid_hashtbl[STATEID_HASH_SIZE];

static struct nfs4_delegation *
alloc_init_deleg(struct nfs4_client *clp, struct nfs4_stateid *stp, struct svc_fh *current_fh, u32 type)
{
	struct nfs4_delegation *dp;
	struct nfs4_file *fp = stp->st_file;
	struct nfs4_callback *cb = &stp->st_stateowner->so_client->cl_callback;

	dprintk("NFSD alloc_init_deleg\n");
	if (fp->fi_had_conflict)
		return NULL;
	if (num_delegations > max_delegations)
		return NULL;
	dp = kmem_cache_alloc(deleg_slab, GFP_KERNEL);
	if (dp == NULL)
		return dp;
	num_delegations++;
	INIT_LIST_HEAD(&dp->dl_perfile);
	INIT_LIST_HEAD(&dp->dl_perclnt);
	INIT_LIST_HEAD(&dp->dl_recall_lru);
	dp->dl_client = clp;
	get_nfs4_file(fp);
	dp->dl_file = fp;
	dp->dl_flock = NULL;
	get_file(stp->st_vfs_file);
	dp->dl_vfs_file = stp->st_vfs_file;
	dp->dl_type = type;
	dp->dl_recall.cbr_dp = NULL;
	dp->dl_recall.cbr_ident = cb->cb_ident;
	dp->dl_recall.cbr_trunc = 0;
	dp->dl_stateid.si_boot = boot_time;
	dp->dl_stateid.si_stateownerid = current_delegid++;
	dp->dl_stateid.si_fileid = 0;
	dp->dl_stateid.si_generation = 0;
	dp->dl_fhlen = current_fh->fh_handle.fh_size;
	memcpy(dp->dl_fhval, &current_fh->fh_handle.fh_base,
		        current_fh->fh_handle.fh_size);
	dp->dl_time = 0;
	atomic_set(&dp->dl_count, 1);
	list_add(&dp->dl_perfile, &fp->fi_delegations);
	list_add(&dp->dl_perclnt, &clp->cl_delegations);
	return dp;
}

void
nfs4_put_delegation(struct nfs4_delegation *dp)
{
	if (atomic_dec_and_test(&dp->dl_count)) {
		dprintk("NFSD: freeing dp %p\n",dp);
		put_nfs4_file(dp->dl_file);
		kmem_cache_free(deleg_slab, dp);
		num_delegations--;
	}
}

/* Remove the associated file_lock first, then remove the delegation.
 * lease_modify() is called to remove the FS_LEASE file_lock from
 * the i_flock list, eventually calling nfsd's lock_manager
 * fl_release_callback.
 */
static void
nfs4_close_delegation(struct nfs4_delegation *dp)
{
	struct file *filp = dp->dl_vfs_file;

	dprintk("NFSD: close_delegation dp %p\n",dp);
	dp->dl_vfs_file = NULL;
	/* The following nfsd_close may not actually close the file,
	 * but we want to remove the lease in any case. */
	if (dp->dl_flock)
		vfs_setlease(filp, F_UNLCK, &dp->dl_flock);
	nfsd_close(filp);
}

/* Called under the state lock. */
static void
unhash_delegation(struct nfs4_delegation *dp)
{
	list_del_init(&dp->dl_perfile);
	list_del_init(&dp->dl_perclnt);
	spin_lock(&recall_lock);
	list_del_init(&dp->dl_recall_lru);
	spin_unlock(&recall_lock);
	nfs4_close_delegation(dp);
	nfs4_put_delegation(dp);

static unsigned int nfsd_fh_hashval(struct knfsd_fh *fh)
{
	return jhash2(fh->fh_base.fh_pad, XDR_QUADLEN(fh->fh_size), 0);
}

static unsigned int file_hashval(struct knfsd_fh *fh)
{
	return nfsd_fh_hashval(fh) & (FILE_HASH_SIZE - 1);
}

static struct hlist_head file_hashtbl[FILE_HASH_SIZE];

static void
__nfs4_file_get_access(struct nfs4_file *fp, u32 access)
{
	lockdep_assert_held(&fp->fi_lock);

	if (access & NFS4_SHARE_ACCESS_WRITE)
		atomic_inc(&fp->fi_access[O_WRONLY]);
	if (access & NFS4_SHARE_ACCESS_READ)
		atomic_inc(&fp->fi_access[O_RDONLY]);
}

static __be32
nfs4_file_get_access(struct nfs4_file *fp, u32 access)
{
	lockdep_assert_held(&fp->fi_lock);

	/* Does this access mode make sense? */
	if (access & ~NFS4_SHARE_ACCESS_BOTH)
		return nfserr_inval;

	/* Does it conflict with a deny mode already set? */
	if ((access & fp->fi_share_deny) != 0)
		return nfserr_share_denied;

	__nfs4_file_get_access(fp, access);
	return nfs_ok;
}

static __be32 nfs4_file_check_deny(struct nfs4_file *fp, u32 deny)
{
	/* Common case is that there is no deny mode. */
	if (deny) {
		/* Does this deny mode make sense? */
		if (deny & ~NFS4_SHARE_DENY_BOTH)
			return nfserr_inval;

		if ((deny & NFS4_SHARE_DENY_READ) &&
		    atomic_read(&fp->fi_access[O_RDONLY]))
			return nfserr_share_denied;

		if ((deny & NFS4_SHARE_DENY_WRITE) &&
		    atomic_read(&fp->fi_access[O_WRONLY]))
			return nfserr_share_denied;
	}
	return nfs_ok;
}

static void __nfs4_file_put_access(struct nfs4_file *fp, int oflag)
{
	might_lock(&fp->fi_lock);

	if (atomic_dec_and_lock(&fp->fi_access[oflag], &fp->fi_lock)) {
		struct file *f1 = NULL;
		struct file *f2 = NULL;

		swap(f1, fp->fi_fds[oflag]);
		if (atomic_read(&fp->fi_access[1 - oflag]) == 0)
			swap(f2, fp->fi_fds[O_RDWR]);
		spin_unlock(&fp->fi_lock);
		if (f1)
			fput(f1);
		if (f2)
			fput(f2);
	}
}

static void nfs4_file_put_access(struct nfs4_file *fp, u32 access)
{
	WARN_ON_ONCE(access & ~NFS4_SHARE_ACCESS_BOTH);

	if (access & NFS4_SHARE_ACCESS_WRITE)
		__nfs4_file_put_access(fp, O_WRONLY);
	if (access & NFS4_SHARE_ACCESS_READ)
		__nfs4_file_put_access(fp, O_RDONLY);
}

/*
 * Allocate a new open/delegation state counter. This is needed for
 * pNFS for proper return on close semantics.
 *
 * Note that we only allocate it for pNFS-enabled exports, otherwise
 * all pointers to struct nfs4_clnt_odstate are always NULL.
 */
static struct nfs4_clnt_odstate *
alloc_clnt_odstate(struct nfs4_client *clp)
{
	struct nfs4_clnt_odstate *co;

	co = kmem_cache_zalloc(odstate_slab, GFP_KERNEL);
	if (co) {
		co->co_client = clp;
		atomic_set(&co->co_odcount, 1);
	}
	return co;
}

static void
hash_clnt_odstate_locked(struct nfs4_clnt_odstate *co)
{
	struct nfs4_file *fp = co->co_file;

	lockdep_assert_held(&fp->fi_lock);
	list_add(&co->co_perfile, &fp->fi_clnt_odstate);
}

static inline void
get_clnt_odstate(struct nfs4_clnt_odstate *co)
{
	if (co)
		atomic_inc(&co->co_odcount);
}

static void
put_clnt_odstate(struct nfs4_clnt_odstate *co)
{
	struct nfs4_file *fp;

	if (!co)
		return;

	fp = co->co_file;
	if (atomic_dec_and_lock(&co->co_odcount, &fp->fi_lock)) {
		list_del(&co->co_perfile);
		spin_unlock(&fp->fi_lock);

		nfsd4_return_all_file_layouts(co->co_client, fp);
		kmem_cache_free(odstate_slab, co);
	}
}

static struct nfs4_clnt_odstate *
find_or_hash_clnt_odstate(struct nfs4_file *fp, struct nfs4_clnt_odstate *new)
{
	struct nfs4_clnt_odstate *co;
	struct nfs4_client *cl;

	if (!new)
		return NULL;

	cl = new->co_client;

	spin_lock(&fp->fi_lock);
	list_for_each_entry(co, &fp->fi_clnt_odstate, co_perfile) {
		if (co->co_client == cl) {
			get_clnt_odstate(co);
			goto out;
		}
	}
	co = new;
	co->co_file = fp;
	hash_clnt_odstate_locked(new);
out:
	spin_unlock(&fp->fi_lock);
	return co;
}

struct nfs4_stid *nfs4_alloc_stid(struct nfs4_client *cl, struct kmem_cache *slab,
				  void (*sc_free)(struct nfs4_stid *))
{
	struct nfs4_stid *stid;
	int new_id;

	stid = kmem_cache_zalloc(slab, GFP_KERNEL);
	if (!stid)
		return NULL;

	idr_preload(GFP_KERNEL);
	spin_lock(&cl->cl_lock);
	new_id = idr_alloc_cyclic(&cl->cl_stateids, stid, 0, 0, GFP_NOWAIT);
	spin_unlock(&cl->cl_lock);
	idr_preload_end();
	if (new_id < 0)
		goto out_free;

	stid->sc_free = sc_free;
	stid->sc_client = cl;
	stid->sc_stateid.si_opaque.so_id = new_id;
	stid->sc_stateid.si_opaque.so_clid = cl->cl_clientid;
	/* Will be incremented before return to client: */
	atomic_set(&stid->sc_count, 1);
	spin_lock_init(&stid->sc_lock);

	/*
	 * It shouldn't be a problem to reuse an opaque stateid value.
	 * I don't think it is for 4.1.  But with 4.0 I worry that, for
	 * example, a stray write retransmission could be accepted by
	 * the server when it should have been rejected.  Therefore,
	 * adopt a trick from the sctp code to attempt to maximize the
	 * amount of time until an id is reused, by ensuring they always
	 * "increase" (mod INT_MAX):
	 */
	return stid;
out_free:
	kmem_cache_free(slab, stid);
	return NULL;
}

static struct nfs4_ol_stateid * nfs4_alloc_open_stateid(struct nfs4_client *clp)
{
	struct nfs4_stid *stid;

	stid = nfs4_alloc_stid(clp, stateid_slab, nfs4_free_ol_stateid);
	if (!stid)
		return NULL;

	return openlockstateid(stid);
}

static void nfs4_free_deleg(struct nfs4_stid *stid)
{
	kmem_cache_free(deleg_slab, stid);
	atomic_long_dec(&num_delegations);
}

/*
 * When we recall a delegation, we should be careful not to hand it
 * out again straight away.
 * To ensure this we keep a pair of bloom filters ('new' and 'old')
 * in which the filehandles of recalled delegations are "stored".
 * If a filehandle appear in either filter, a delegation is blocked.
 * When a delegation is recalled, the filehandle is stored in the "new"
 * filter.
 * Every 30 seconds we swap the filters and clear the "new" one,
 * unless both are empty of course.  This results in delegations for a
 * given filehandle being blocked for between 30 and 60 seconds.
 *
 * Each filter is 256 bits.  We hash the filehandle to 32bit and use the
 * low 3 bytes as hash-table indices.
 *
 * 'blocked_delegations_lock', which is always taken in block_delegations(),
 * is used to manage concurrent access.  Testing does not need the lock
 * except when swapping the two filters.
 */
static DEFINE_SPINLOCK(blocked_delegations_lock);
static struct bloom_pair {
	int	entries, old_entries;
	time64_t swap_time;
	int	new; /* index into 'set' */
	DECLARE_BITMAP(set[2], 256);
} blocked_delegations;

static int delegation_blocked(struct knfsd_fh *fh)
{
	u32 hash;
	struct bloom_pair *bd = &blocked_delegations;

	if (bd->entries == 0)
		return 0;
	if (ktime_get_seconds() - bd->swap_time > 30) {
		spin_lock(&blocked_delegations_lock);
		if (ktime_get_seconds() - bd->swap_time > 30) {
			bd->entries -= bd->old_entries;
			bd->old_entries = bd->entries;
			bd->new = 1-bd->new;
			memset(bd->set[bd->new], 0,
			       sizeof(bd->set[0]));
			bd->swap_time = ktime_get_seconds();
		}
		spin_unlock(&blocked_delegations_lock);
	}
	hash = jhash(&fh->fh_base, fh->fh_size, 0);
	if (test_bit(hash&255, bd->set[0]) &&
	    test_bit((hash>>8)&255, bd->set[0]) &&
	    test_bit((hash>>16)&255, bd->set[0]))
		return 1;

	if (test_bit(hash&255, bd->set[1]) &&
	    test_bit((hash>>8)&255, bd->set[1]) &&
	    test_bit((hash>>16)&255, bd->set[1]))
		return 1;

	return 0;
}

static void block_delegations(struct knfsd_fh *fh)
{
	u32 hash;
	struct bloom_pair *bd = &blocked_delegations;

	hash = jhash(&fh->fh_base, fh->fh_size, 0);

	spin_lock(&blocked_delegations_lock);
	__set_bit(hash&255, bd->set[bd->new]);
	__set_bit((hash>>8)&255, bd->set[bd->new]);
	__set_bit((hash>>16)&255, bd->set[bd->new]);
	if (bd->entries == 0)
		bd->swap_time = ktime_get_seconds();
	bd->entries += 1;
	spin_unlock(&blocked_delegations_lock);
}

static struct nfs4_delegation *
alloc_init_deleg(struct nfs4_client *clp, struct svc_fh *current_fh,
		 struct nfs4_clnt_odstate *odstate)
{
	struct nfs4_delegation *dp;
	long n;

	dprintk("NFSD alloc_init_deleg\n");
	n = atomic_long_inc_return(&num_delegations);
	if (n < 0 || n > max_delegations)
		goto out_dec;
	if (delegation_blocked(&current_fh->fh_handle))
		goto out_dec;
	dp = delegstateid(nfs4_alloc_stid(clp, deleg_slab, nfs4_free_deleg));
	if (dp == NULL)
		goto out_dec;

	/*
	 * delegation seqid's are never incremented.  The 4.1 special
	 * meaning of seqid 0 isn't meaningful, really, but let's avoid
	 * 0 anyway just for consistency and use 1:
	 */
	dp->dl_stid.sc_stateid.si_generation = 1;
	INIT_LIST_HEAD(&dp->dl_perfile);
	INIT_LIST_HEAD(&dp->dl_perclnt);
	INIT_LIST_HEAD(&dp->dl_recall_lru);
	dp->dl_clnt_odstate = odstate;
	get_clnt_odstate(odstate);
	dp->dl_type = NFS4_OPEN_DELEGATE_READ;
	dp->dl_retries = 1;
	nfsd4_init_cb(&dp->dl_recall, dp->dl_stid.sc_client,
		      &nfsd4_cb_recall_ops, NFSPROC4_CLNT_CB_RECALL);
	return dp;
out_dec:
	atomic_long_dec(&num_delegations);
	return NULL;
}

void
nfs4_put_stid(struct nfs4_stid *s)
{
	struct nfs4_file *fp = s->sc_file;
	struct nfs4_client *clp = s->sc_client;

	might_lock(&clp->cl_lock);

	if (!atomic_dec_and_lock(&s->sc_count, &clp->cl_lock)) {
		wake_up_all(&close_wq);
		return;
	}
	idr_remove(&clp->cl_stateids, s->sc_stateid.si_opaque.so_id);
	spin_unlock(&clp->cl_lock);
	s->sc_free(s);
	if (fp)
		put_nfs4_file(fp);
}

void
nfs4_inc_and_copy_stateid(stateid_t *dst, struct nfs4_stid *stid)
{
	stateid_t *src = &stid->sc_stateid;

	spin_lock(&stid->sc_lock);
	if (unlikely(++src->si_generation == 0))
		src->si_generation = 1;
	memcpy(dst, src, sizeof(*dst));
	spin_unlock(&stid->sc_lock);
}

static void nfs4_put_deleg_lease(struct nfs4_file *fp)
{
	struct file *filp = NULL;

	spin_lock(&fp->fi_lock);
	if (fp->fi_deleg_file && --fp->fi_delegees == 0)
		swap(filp, fp->fi_deleg_file);
	spin_unlock(&fp->fi_lock);

	if (filp) {
		vfs_setlease(filp, F_UNLCK, NULL, (void **)&fp);
		fput(filp);
	}
}

void nfs4_unhash_stid(struct nfs4_stid *s)
{
	s->sc_type = 0;
}

/**
 * nfs4_get_existing_delegation - Discover if this delegation already exists
 * @clp:     a pointer to the nfs4_client we're granting a delegation to
 * @fp:      a pointer to the nfs4_file we're granting a delegation on
 *
 * Return:
 *      On success: NULL if an existing delegation was not found.
 *
 *      On error: -EAGAIN if one was previously granted to this nfs4_client
 *                 for this nfs4_file.
 *
 */

static int
nfs4_get_existing_delegation(struct nfs4_client *clp, struct nfs4_file *fp)
{
	struct nfs4_delegation *searchdp = NULL;
	struct nfs4_client *searchclp = NULL;

	lockdep_assert_held(&state_lock);
	lockdep_assert_held(&fp->fi_lock);

	list_for_each_entry(searchdp, &fp->fi_delegations, dl_perfile) {
		searchclp = searchdp->dl_stid.sc_client;
		if (clp == searchclp) {
			return -EAGAIN;
		}
	}
	return 0;
}

/**
 * hash_delegation_locked - Add a delegation to the appropriate lists
 * @dp:     a pointer to the nfs4_delegation we are adding.
 * @fp:     a pointer to the nfs4_file we're granting a delegation on
 *
 * Return:
 *      On success: NULL if the delegation was successfully hashed.
 *
 *      On error: -EAGAIN if one was previously granted to this
 *                 nfs4_client for this nfs4_file. Delegation is not hashed.
 *
 */

static int
hash_delegation_locked(struct nfs4_delegation *dp, struct nfs4_file *fp)
{
	int status;
	struct nfs4_client *clp = dp->dl_stid.sc_client;

	lockdep_assert_held(&state_lock);
	lockdep_assert_held(&fp->fi_lock);

	status = nfs4_get_existing_delegation(clp, fp);
	if (status)
		return status;
	++fp->fi_delegees;
	atomic_inc(&dp->dl_stid.sc_count);
	dp->dl_stid.sc_type = NFS4_DELEG_STID;
	list_add(&dp->dl_perfile, &fp->fi_delegations);
	list_add(&dp->dl_perclnt, &clp->cl_delegations);
	return 0;
}

static bool delegation_hashed(struct nfs4_delegation *dp)
{
	return !(list_empty(&dp->dl_perfile));
}

static bool
unhash_delegation_locked(struct nfs4_delegation *dp)
{
	struct nfs4_file *fp = dp->dl_stid.sc_file;

	lockdep_assert_held(&state_lock);

	if (!delegation_hashed(dp))
		return false;

	dp->dl_stid.sc_type = NFS4_CLOSED_DELEG_STID;
	/* Ensure that deleg break won't try to requeue it */
	++dp->dl_time;
	spin_lock(&fp->fi_lock);
	list_del_init(&dp->dl_perclnt);
	list_del_init(&dp->dl_recall_lru);
	list_del_init(&dp->dl_perfile);
	spin_unlock(&fp->fi_lock);
	return true;
}

static void destroy_delegation(struct nfs4_delegation *dp)
{
	bool unhashed;

	spin_lock(&state_lock);
	unhashed = unhash_delegation_locked(dp);
	spin_unlock(&state_lock);
	if (unhashed) {
		put_clnt_odstate(dp->dl_clnt_odstate);
		nfs4_put_deleg_lease(dp->dl_stid.sc_file);
		nfs4_put_stid(&dp->dl_stid);
	}
}

static void revoke_delegation(struct nfs4_delegation *dp)
{
	struct nfs4_client *clp = dp->dl_stid.sc_client;

	WARN_ON(!list_empty(&dp->dl_recall_lru));

	put_clnt_odstate(dp->dl_clnt_odstate);
	nfs4_put_deleg_lease(dp->dl_stid.sc_file);

	if (clp->cl_minorversion == 0)
		nfs4_put_stid(&dp->dl_stid);
	else {
		dp->dl_stid.sc_type = NFS4_REVOKED_DELEG_STID;
		spin_lock(&clp->cl_lock);
		list_add(&dp->dl_recall_lru, &clp->cl_revoked);
		spin_unlock(&clp->cl_lock);
	}
}

/* 
 * SETCLIENTID state 
 */

/* Hash tables for nfs4_clientid state */
#define CLIENT_HASH_BITS                 4
#define CLIENT_HASH_SIZE                (1 << CLIENT_HASH_BITS)
#define CLIENT_HASH_MASK                (CLIENT_HASH_SIZE - 1)

#define clientid_hashval(id) \
	((id) & CLIENT_HASH_MASK)
#define clientstr_hashval(name) \
	(opaque_hashval((name), 8) & CLIENT_HASH_MASK)
/*
 * reclaim_str_hashtbl[] holds known client info from previous reset/reboot
 * used in reboot/reset lease grace period processing
 *
 * conf_id_hashtbl[], and conf_str_hashtbl[] hold confirmed
 * setclientid_confirmed info. 
 *
 * unconf_str_hastbl[] and unconf_id_hashtbl[] hold unconfirmed 
 * setclientid info.
 *
 * client_lru holds client queue ordered by nfs4_client.cl_time
 * for lease renewal.
 *
 * close_lru holds (open) stateowner queue ordered by nfs4_stateowner.so_time
 * for last close replay.
 */
static struct list_head	reclaim_str_hashtbl[CLIENT_HASH_SIZE];
static int reclaim_str_hashtbl_size = 0;
static struct list_head	conf_id_hashtbl[CLIENT_HASH_SIZE];
static struct list_head	conf_str_hashtbl[CLIENT_HASH_SIZE];
static struct list_head	unconf_str_hashtbl[CLIENT_HASH_SIZE];
static struct list_head	unconf_id_hashtbl[CLIENT_HASH_SIZE];
static struct list_head client_lru;
static struct list_head close_lru;

static inline void
renew_client(struct nfs4_client *clp)
{
	/*
	* Move client to the end to the LRU list.
	*/
	dprintk("renewing client (clientid %08x/%08x)\n", 
			clp->cl_clientid.cl_boot, 
			clp->cl_clientid.cl_id);
	list_move_tail(&clp->cl_lru, &client_lru);
	clp->cl_time = get_seconds();
}

/* SETCLIENTID and SETCLIENTID_CONFIRM Helper functions */
static int
STALE_CLIENTID(clientid_t *clid)
{
	if (clid->cl_boot == boot_time)
		return 0;
	dprintk("NFSD stale clientid (%08x/%08x)\n", 
			clid->cl_boot, clid->cl_id);
	return 1;
}

/* 
 * XXX Should we use a slab cache ?
 * This type of memory management is somewhat inefficient, but we use it
 * anyway since SETCLIENTID is not a common operation.
 */
static struct nfs4_client *alloc_client(struct xdr_netobj name)
{
	struct nfs4_client *clp;

	clp = kzalloc(sizeof(struct nfs4_client), GFP_KERNEL);
	if (clp == NULL)
		return NULL;
	clp->cl_name.data = kmalloc(name.len, GFP_KERNEL);
	if (clp->cl_name.data == NULL) {
		kfree(clp);
		return NULL;
	}
	memcpy(clp->cl_name.data, name.data, name.len);
	clp->cl_name.len = name.len;
	return clp;
}

static void
shutdown_callback_client(struct nfs4_client *clp)
{
	struct rpc_clnt *clnt = clp->cl_callback.cb_client;

	if (clnt) {
		/*
		 * Callback threads take a reference on the client, so there
		 * should be no outstanding callbacks at this point.
		 */
		clp->cl_callback.cb_client = NULL;
		rpc_shutdown_client(clnt);
	}
}

static inline void
free_client(struct nfs4_client *clp)
{
	shutdown_callback_client(clp);
	if (clp->cl_cred.cr_group_info)
		put_group_info(clp->cl_cred.cr_group_info);
	kfree(clp->cl_name.data);
	kfree(clp);
}

void
put_nfs4_client(struct nfs4_client *clp)
{
	if (atomic_dec_and_test(&clp->cl_count))
		free_client(clp);
}

static void
expire_client(struct nfs4_client *clp)
{
	struct nfs4_stateowner *sop;
	struct nfs4_delegation *dp;
	struct list_head reaplist;

	dprintk("NFSD: expire_client cl_count %d\n",
	                    atomic_read(&clp->cl_count));

	INIT_LIST_HEAD(&reaplist);
	spin_lock(&recall_lock);
	while (!list_empty(&clp->cl_delegations)) {
		dp = list_entry(clp->cl_delegations.next, struct nfs4_delegation, dl_perclnt);
		dprintk("NFSD: expire client. dp %p, fp %p\n", dp,
				dp->dl_flock);
		list_del_init(&dp->dl_perclnt);
		list_move(&dp->dl_recall_lru, &reaplist);
	}
	spin_unlock(&recall_lock);
	while (!list_empty(&reaplist)) {
		dp = list_entry(reaplist.next, struct nfs4_delegation, dl_recall_lru);
		list_del_init(&dp->dl_recall_lru);
		unhash_delegation(dp);
	}
	list_del(&clp->cl_idhash);
	list_del(&clp->cl_strhash);
	list_del(&clp->cl_lru);
	while (!list_empty(&clp->cl_openowners)) {
		sop = list_entry(clp->cl_openowners.next, struct nfs4_stateowner, so_perclient);
		release_stateowner(sop);
	}
	put_nfs4_client(clp);
}

static struct nfs4_client *create_client(struct xdr_netobj name, char *recdir)
{
	struct nfs4_client *clp;

	clp = alloc_client(name);
	if (clp == NULL)
		return NULL;
	memcpy(clp->cl_recdir, recdir, HEXDIR_LEN);
	atomic_set(&clp->cl_count, 1);
	atomic_set(&clp->cl_callback.cb_set, 0);
	INIT_LIST_HEAD(&clp->cl_idhash);
	INIT_LIST_HEAD(&clp->cl_strhash);
	INIT_LIST_HEAD(&clp->cl_openowners);
	INIT_LIST_HEAD(&clp->cl_delegations);
	INIT_LIST_HEAD(&clp->cl_lru);
	return clp;
}

static void copy_verf(struct nfs4_client *target, nfs4_verifier *source)
{
	memcpy(target->cl_verifier.data, source->data,
			sizeof(target->cl_verifier.data));
}

static void copy_clid(struct nfs4_client *target, struct nfs4_client *source)
{
	target->cl_clientid.cl_boot = source->cl_clientid.cl_boot; 
	target->cl_clientid.cl_id = source->cl_clientid.cl_id; 
}

static void copy_cred(struct svc_cred *target, struct svc_cred *source)
{
	target->cr_uid = source->cr_uid;
	target->cr_gid = source->cr_gid;
	target->cr_group_info = source->cr_group_info;
	get_group_info(target->cr_group_info);
}

static int same_name(const char *n1, const char *n2)
{
	return 0 == memcmp(n1, n2, HEXDIR_LEN);
}

static int
same_verf(nfs4_verifier *v1, nfs4_verifier *v2)
{
	return 0 == memcmp(v1->data, v2->data, sizeof(v1->data));
}

static int
same_clid(clientid_t *cl1, clientid_t *cl2)
{
	return (cl1->cl_boot == cl2->cl_boot) && (cl1->cl_id == cl2->cl_id);
}

/* XXX what about NGROUP */
static int
same_creds(struct svc_cred *cr1, struct svc_cred *cr2)
{
	return cr1->cr_uid == cr2->cr_uid;
}

static void gen_clid(struct nfs4_client *clp)
{
	static u32 current_clientid = 1;

	clp->cl_clientid.cl_boot = boot_time;
	clp->cl_clientid.cl_id = current_clientid++; 
}

static void gen_confirm(struct nfs4_client *clp)
{
	static u32 i;
	u32 *p;

	p = (u32 *)clp->cl_confirm.data;
	*p++ = get_seconds();
	*p++ = i++;
}

static int check_name(struct xdr_netobj name)
{
	if (name.len == 0) 
		return 0;
	if (name.len > NFS4_OPAQUE_LIMIT) {
		dprintk("NFSD: check_name: name too long(%d)!\n", name.len);
		return 0;
	}
	return 1;
}

static void
add_to_unconfirmed(struct nfs4_client *clp, unsigned int strhashval)
{
	unsigned int idhashval;

	list_add(&clp->cl_strhash, &unconf_str_hashtbl[strhashval]);
	idhashval = clientid_hashval(clp->cl_clientid.cl_id);
	list_add(&clp->cl_idhash, &unconf_id_hashtbl[idhashval]);
	list_add_tail(&clp->cl_lru, &client_lru);
	clp->cl_time = get_seconds();
}

static void
move_to_confirmed(struct nfs4_client *clp)
{
	unsigned int idhashval = clientid_hashval(clp->cl_clientid.cl_id);
	unsigned int strhashval;

	dprintk("NFSD: move_to_confirm nfs4_client %p\n", clp);
	list_del_init(&clp->cl_strhash);
	list_move(&clp->cl_idhash, &conf_id_hashtbl[idhashval]);
	strhashval = clientstr_hashval(clp->cl_recdir);
	list_add(&clp->cl_strhash, &conf_str_hashtbl[strhashval]);
	renew_client(clp);
}

static struct nfs4_client *
find_confirmed_client(clientid_t *clid)
{
	struct nfs4_client *clp;
	unsigned int idhashval = clientid_hashval(clid->cl_id);

	list_for_each_entry(clp, &conf_id_hashtbl[idhashval], cl_idhash) {
		if (same_clid(&clp->cl_clientid, clid))
			return clp;
	}
	return NULL;
}

static struct nfs4_client *
find_unconfirmed_client(clientid_t *clid)
{
	struct nfs4_client *clp;
	unsigned int idhashval = clientid_hashval(clid->cl_id);

	list_for_each_entry(clp, &unconf_id_hashtbl[idhashval], cl_idhash) {
		if (same_clid(&clp->cl_clientid, clid))
			return clp;
	}
	return NULL;
}

static struct nfs4_client *
find_confirmed_client_by_str(const char *dname, unsigned int hashval)
{
	struct nfs4_client *clp;

	list_for_each_entry(clp, &conf_str_hashtbl[hashval], cl_strhash) {
		if (same_name(clp->cl_recdir, dname))
			return clp;
	}
	return NULL;
}

static struct nfs4_client *
find_unconfirmed_client_by_str(const char *dname, unsigned int hashval)
{
	struct nfs4_client *clp;

	list_for_each_entry(clp, &unconf_str_hashtbl[hashval], cl_strhash) {
		if (same_name(clp->cl_recdir, dname))
			return clp;
	}
	return NULL;
}

/* a helper function for parse_callback */
static int
parse_octet(unsigned int *lenp, char **addrp)
{
	unsigned int len = *lenp;
	char *p = *addrp;
	int n = -1;
	char c;

	for (;;) {
		if (!len)
			break;
		len--;
		c = *p++;
		if (c == '.')
			break;
		if ((c < '0') || (c > '9')) {
			n = -1;
			break;
		}
		if (n < 0)
			n = 0;
		n = (n * 10) + (c - '0');
		if (n > 255) {
			n = -1;
			break;
		}
	}
	*lenp = len;
	*addrp = p;
	return n;
}

/* parse and set the setclientid ipv4 callback address */
static int
parse_ipv4(unsigned int addr_len, char *addr_val, unsigned int *cbaddrp, unsigned short *cbportp)
{
	int temp = 0;
	u32 cbaddr = 0;
	u16 cbport = 0;
	u32 addrlen = addr_len;
	char *addr = addr_val;
	int i, shift;

	/* ipaddress */
	shift = 24;
	for(i = 4; i > 0  ; i--) {
		if ((temp = parse_octet(&addrlen, &addr)) < 0) {
			return 0;
		}
		cbaddr |= (temp << shift);
		if (shift > 0)
		shift -= 8;
	}
	*cbaddrp = cbaddr;

	/* port */
	shift = 8;
	for(i = 2; i > 0  ; i--) {
		if ((temp = parse_octet(&addrlen, &addr)) < 0) {
			return 0;
		}
		cbport |= (temp << shift);
		if (shift > 0)
			shift -= 8;
	}
	*cbportp = cbport;
	return 1;
}

static void
gen_callback(struct nfs4_client *clp, struct nfsd4_setclientid *se)
{
	struct nfs4_callback *cb = &clp->cl_callback;

	/* Currently, we only support tcp for the callback channel */
	if ((se->se_callback_netid_len != 3) || memcmp((char *)se->se_callback_netid_val, "tcp", 3))
		goto out_err;

	if ( !(parse_ipv4(se->se_callback_addr_len, se->se_callback_addr_val,
	                 &cb->cb_addr, &cb->cb_port)))
		goto out_err;
	cb->cb_prog = se->se_callback_prog;
	cb->cb_ident = se->se_callback_ident;
	return;
out_err:
	dprintk(KERN_INFO "NFSD: this client (clientid %08x/%08x) "
		"will not receive delegations\n",
		clp->cl_clientid.cl_boot, clp->cl_clientid.cl_id);

	return;
}

__be32
nfsd4_setclientid(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
		  struct nfsd4_setclientid *setclid)
{
	struct sockaddr_in	*sin = svc_addr_in(rqstp);
	struct xdr_netobj 	clname = { 
		.len = setclid->se_namelen,
		.data = setclid->se_name,
	};
	nfs4_verifier		clverifier = setclid->se_verf;
	unsigned int 		strhashval;
	struct nfs4_client	*conf, *unconf, *new;
	__be32 			status;
	char                    dname[HEXDIR_LEN];
	
	if (!check_name(clname))
		return nfserr_inval;

	status = nfs4_make_rec_clidname(dname, &clname);
	if (status)
		return status;

	/* 
	 * XXX The Duplicate Request Cache (DRC) has been checked (??)
	 * We get here on a DRC miss.
	 */

	strhashval = clientstr_hashval(dname);

	nfs4_lock_state();
	conf = find_confirmed_client_by_str(dname, strhashval);
	if (conf) {
		/* RFC 3530 14.2.33 CASE 0: */
		status = nfserr_clid_inuse;
		if (!same_creds(&conf->cl_cred, &rqstp->rq_cred)
				|| conf->cl_addr != sin->sin_addr.s_addr) {
			dprintk("NFSD: setclientid: string in use by client"
				"at %u.%u.%u.%u\n", NIPQUAD(conf->cl_addr));
			goto out;
		}
	}
	/*
	 * section 14.2.33 of RFC 3530 (under the heading "IMPLEMENTATION")
	 * has a description of SETCLIENTID request processing consisting
	 * of 5 bullet points, labeled as CASE0 - CASE4 below.
	 */
	unconf = find_unconfirmed_client_by_str(dname, strhashval);
	status = nfserr_resource;
	if (!conf) {
		/*
		 * RFC 3530 14.2.33 CASE 4:
		 * placed first, because it is the normal case
		 */
		if (unconf)
			expire_client(unconf);
		new = create_client(clname, dname);
		if (new == NULL)
			goto out;
		gen_clid(new);
	} else if (same_verf(&conf->cl_verifier, &clverifier)) {
		/*
		 * RFC 3530 14.2.33 CASE 1:
		 * probable callback update
		 */
		if (unconf) {
			/* Note this is removing unconfirmed {*x***},
			 * which is stronger than RFC recommended {vxc**}.
			 * This has the advantage that there is at most
			 * one {*x***} in either list at any time.
			 */
			expire_client(unconf);
		}
		new = create_client(clname, dname);
		if (new == NULL)
			goto out;
		copy_clid(new, conf);
	} else if (!unconf) {
		/*
		 * RFC 3530 14.2.33 CASE 2:
		 * probable client reboot; state will be removed if
		 * confirmed.
		 */
		new = create_client(clname, dname);
		if (new == NULL)
			goto out;
		gen_clid(new);
	} else {
		/*
		 * RFC 3530 14.2.33 CASE 3:
		 * probable client reboot; state will be removed if
		 * confirmed.
		 */
		expire_client(unconf);
		new = create_client(clname, dname);
		if (new == NULL)
			goto out;
		gen_clid(new);
	}
	copy_verf(new, &clverifier);
	new->cl_addr = sin->sin_addr.s_addr;
	copy_cred(&new->cl_cred, &rqstp->rq_cred);
	gen_confirm(new);
	gen_callback(new, setclid);
	add_to_unconfirmed(new, strhashval);
	setclid->se_clientid.cl_boot = new->cl_clientid.cl_boot;
	setclid->se_clientid.cl_id = new->cl_clientid.cl_id;
	memcpy(setclid->se_confirm.data, new->cl_confirm.data, sizeof(setclid->se_confirm.data));
	status = nfs_ok;
out:
	nfs4_unlock_state();
	return status;
}


/*
 * Section 14.2.34 of RFC 3530 (under the heading "IMPLEMENTATION") has
 * a description of SETCLIENTID_CONFIRM request processing consisting of 4
 * bullets, labeled as CASE1 - CASE4 below.
 */
__be32
nfsd4_setclientid_confirm(struct svc_rqst *rqstp,
			 struct nfsd4_compound_state *cstate,
			 struct nfsd4_setclientid_confirm *setclientid_confirm)
{
	struct sockaddr_in *sin = svc_addr_in(rqstp);
	struct nfs4_client *conf, *unconf;
	nfs4_verifier confirm = setclientid_confirm->sc_confirm; 
	clientid_t * clid = &setclientid_confirm->sc_clientid;
	__be32 status;

	if (STALE_CLIENTID(clid))
		return nfserr_stale_clientid;
	/* 
	 * XXX The Duplicate Request Cache (DRC) has been checked (??)
	 * We get here on a DRC miss.
	 */

	nfs4_lock_state();

	conf = find_confirmed_client(clid);
	unconf = find_unconfirmed_client(clid);

	status = nfserr_clid_inuse;
	if (conf && conf->cl_addr != sin->sin_addr.s_addr)
		goto out;
	if (unconf && unconf->cl_addr != sin->sin_addr.s_addr)
		goto out;

	/*
	 * section 14.2.34 of RFC 3530 has a description of
	 * SETCLIENTID_CONFIRM request processing consisting
	 * of 4 bullet points, labeled as CASE1 - CASE4 below.
	 */
	if (conf && unconf && same_verf(&confirm, &unconf->cl_confirm)) {
		/*
		 * RFC 3530 14.2.34 CASE 1:
		 * callback update
		 */
		if (!same_creds(&conf->cl_cred, &unconf->cl_cred))
			status = nfserr_clid_inuse;
		else {
			/* XXX: We just turn off callbacks until we can handle
			  * change request correctly. */
			atomic_set(&conf->cl_callback.cb_set, 0);
			gen_confirm(conf);
			nfsd4_remove_clid_dir(unconf);
			expire_client(unconf);
			status = nfs_ok;

		}
	} else if (conf && !unconf) {
		/*
		 * RFC 3530 14.2.34 CASE 2:
		 * probable retransmitted request; play it safe and
		 * do nothing.
		 */
		if (!same_creds(&conf->cl_cred, &rqstp->rq_cred))
			status = nfserr_clid_inuse;
		else
			status = nfs_ok;
	} else if (!conf && unconf
			&& same_verf(&unconf->cl_confirm, &confirm)) {
		/*
		 * RFC 3530 14.2.34 CASE 3:
		 * Normal case; new or rebooted client:
		 */
		if (!same_creds(&unconf->cl_cred, &rqstp->rq_cred)) {
			status = nfserr_clid_inuse;
		} else {
			unsigned int hash =
				clientstr_hashval(unconf->cl_recdir);
			conf = find_confirmed_client_by_str(unconf->cl_recdir,
									hash);
			if (conf) {
				nfsd4_remove_clid_dir(conf);
				expire_client(conf);
			}
			move_to_confirmed(unconf);
			conf = unconf;
			nfsd4_probe_callback(conf);
			status = nfs_ok;
		}
	} else if ((!conf || (conf && !same_verf(&conf->cl_confirm, &confirm)))
	    && (!unconf || (unconf && !same_verf(&unconf->cl_confirm,
				    				&confirm)))) {
		/*
		 * RFC 3530 14.2.34 CASE 4:
		 * Client probably hasn't noticed that we rebooted yet.
		 */
		status = nfserr_stale_clientid;
	} else {
		/* check that we have hit one of the cases...*/
		status = nfserr_clid_inuse;
	}
out:
	nfs4_unlock_state();
	return status;
}

/* OPEN Share state helper functions */
static inline struct nfs4_file *
alloc_init_file(struct inode *ino)
{
	struct nfs4_file *fp;
	unsigned int hashval = file_hashval(ino);

	fp = kmem_cache_alloc(file_slab, GFP_KERNEL);
	if (fp) {
		kref_init(&fp->fi_ref);
		INIT_LIST_HEAD(&fp->fi_hash);
		INIT_LIST_HEAD(&fp->fi_stateids);
		INIT_LIST_HEAD(&fp->fi_delegations);
		list_add(&fp->fi_hash, &file_hashtbl[hashval]);
		fp->fi_inode = igrab(ino);
		fp->fi_id = current_fileid++;
		fp->fi_had_conflict = false;
		return fp;
	}
	return NULL;
}

static void
nfsd4_free_slab(struct kmem_cache **slab)
{
	if (*slab == NULL)
		return;
	kmem_cache_destroy(*slab);
	*slab = NULL;
}

void
nfsd4_free_slabs(void)
{
	nfsd4_free_slab(&stateowner_slab);
	nfsd4_free_slab(&file_slab);
	nfsd4_free_slab(&stateid_slab);
	nfsd4_free_slab(&deleg_slab);
}

static int
nfsd4_init_slabs(void)
{
	stateowner_slab = kmem_cache_create("nfsd4_stateowners",
			sizeof(struct nfs4_stateowner), 0, 0, NULL);
	if (stateowner_slab == NULL)
		goto out_nomem;
	file_slab = kmem_cache_create("nfsd4_files",
			sizeof(struct nfs4_file), 0, 0, NULL);
	if (file_slab == NULL)
		goto out_nomem;
	stateid_slab = kmem_cache_create("nfsd4_stateids",
			sizeof(struct nfs4_stateid), 0, 0, NULL);
	if (stateid_slab == NULL)
		goto out_nomem;
	deleg_slab = kmem_cache_create("nfsd4_delegations",
			sizeof(struct nfs4_delegation), 0, 0, NULL);
	if (deleg_slab == NULL)
		goto out_nomem;
	return 0;
out_nomem:
	nfsd4_free_slabs();
	dprintk("nfsd4: out of memory while initializing nfsv4\n");
	return -ENOMEM;
}

void
nfs4_free_stateowner(struct kref *kref)
{
	struct nfs4_stateowner *sop =
		container_of(kref, struct nfs4_stateowner, so_ref);
	kfree(sop->so_owner.data);
	kmem_cache_free(stateowner_slab, sop);
}

static inline struct nfs4_stateowner *
alloc_stateowner(struct xdr_netobj *owner)
{
	struct nfs4_stateowner *sop;

	if ((sop = kmem_cache_alloc(stateowner_slab, GFP_KERNEL))) {
		if ((sop->so_owner.data = kmalloc(owner->len, GFP_KERNEL))) {
			memcpy(sop->so_owner.data, owner->data, owner->len);
			sop->so_owner.len = owner->len;
			kref_init(&sop->so_ref);
			return sop;
		} 
		kmem_cache_free(stateowner_slab, sop);
	}
	return NULL;
}

static struct nfs4_stateowner *
alloc_init_open_stateowner(unsigned int strhashval, struct nfs4_client *clp, struct nfsd4_open *open) {
	struct nfs4_stateowner *sop;
	struct nfs4_replay *rp;
	unsigned int idhashval;

	if (!(sop = alloc_stateowner(&open->op_owner)))
		return NULL;
	idhashval = ownerid_hashval(current_ownerid);
	INIT_LIST_HEAD(&sop->so_idhash);
	INIT_LIST_HEAD(&sop->so_strhash);
	INIT_LIST_HEAD(&sop->so_perclient);
	INIT_LIST_HEAD(&sop->so_stateids);
	INIT_LIST_HEAD(&sop->so_perstateid);  /* not used */
	INIT_LIST_HEAD(&sop->so_close_lru);
	sop->so_time = 0;
	list_add(&sop->so_idhash, &ownerid_hashtbl[idhashval]);
	list_add(&sop->so_strhash, &ownerstr_hashtbl[strhashval]);
	list_add(&sop->so_perclient, &clp->cl_openowners);
	sop->so_is_open_owner = 1;
	sop->so_id = current_ownerid++;
	sop->so_client = clp;
	sop->so_seqid = open->op_seqid;
	sop->so_confirmed = 0;
	rp = &sop->so_replay;
	rp->rp_status = nfserr_serverfault;
	rp->rp_buflen = 0;
	rp->rp_buf = rp->rp_ibuf;
	return sop;
}

static void
release_stateid_lockowners(struct nfs4_stateid *open_stp)
{
	struct nfs4_stateowner *lock_sop;

	while (!list_empty(&open_stp->st_lockowners)) {
		lock_sop = list_entry(open_stp->st_lockowners.next,
				struct nfs4_stateowner, so_perstateid);
		/* list_del(&open_stp->st_lockowners);  */
		BUG_ON(lock_sop->so_is_open_owner);
		release_stateowner(lock_sop);
	}
}

static void
unhash_stateowner(struct nfs4_stateowner *sop)
{
	struct nfs4_stateid *stp;

	list_del(&sop->so_idhash);
	list_del(&sop->so_strhash);
	if (sop->so_is_open_owner)
		list_del(&sop->so_perclient);
	list_del(&sop->so_perstateid);
	while (!list_empty(&sop->so_stateids)) {
		stp = list_entry(sop->so_stateids.next,
			struct nfs4_stateid, st_perstateowner);
		if (sop->so_is_open_owner)
			release_stateid(stp, OPEN_STATE);
		else
			release_stateid(stp, LOCK_STATE);
	}
}

static void
release_stateowner(struct nfs4_stateowner *sop)
{
	unhash_stateowner(sop);
	list_del(&sop->so_close_lru);
	nfs4_put_stateowner(sop);
}

static inline void
init_stateid(struct nfs4_stateid *stp, struct nfs4_file *fp, struct nfsd4_open *open) {
	struct nfs4_stateowner *sop = open->op_stateowner;
	unsigned int hashval = stateid_hashval(sop->so_id, fp->fi_id);

	INIT_LIST_HEAD(&stp->st_hash);
	INIT_LIST_HEAD(&stp->st_perstateowner);
	INIT_LIST_HEAD(&stp->st_lockowners);
	INIT_LIST_HEAD(&stp->st_perfile);
	list_add(&stp->st_hash, &stateid_hashtbl[hashval]);
	list_add(&stp->st_perstateowner, &sop->so_stateids);
	list_add(&stp->st_perfile, &fp->fi_stateids);
	stp->st_stateowner = sop;
	get_nfs4_file(fp);
	stp->st_file = fp;
	stp->st_stateid.si_boot = boot_time;
	stp->st_stateid.si_stateownerid = sop->so_id;
	stp->st_stateid.si_fileid = fp->fi_id;
	stp->st_stateid.si_generation = 0;
	stp->st_access_bmap = 0;
	stp->st_deny_bmap = 0;
	__set_bit(open->op_share_access, &stp->st_access_bmap);
	__set_bit(open->op_share_deny, &stp->st_deny_bmap);
	stp->st_openstp = NULL;
}

static void
release_stateid(struct nfs4_stateid *stp, int flags)
{
	struct file *filp = stp->st_vfs_file;

	list_del(&stp->st_hash);
	list_del(&stp->st_perfile);
	list_del(&stp->st_perstateowner);
	if (flags & OPEN_STATE) {
		release_stateid_lockowners(stp);
		stp->st_vfs_file = NULL;
		nfsd_close(filp);
	} else if (flags & LOCK_STATE)
		locks_remove_posix(filp, (fl_owner_t) stp->st_stateowner);
	put_nfs4_file(stp->st_file);
	kmem_cache_free(stateid_slab, stp);
}

static void
move_to_close_lru(struct nfs4_stateowner *sop)
{
	dprintk("NFSD: move_to_close_lru nfs4_stateowner %p\n", sop);

	list_move_tail(&sop->so_close_lru, &close_lru);
	sop->so_time = get_seconds();
}

static int
same_owner_str(struct nfs4_stateowner *sop, struct xdr_netobj *owner,
							clientid_t *clid)
{
	return (sop->so_owner.len == owner->len) &&
		0 == memcmp(sop->so_owner.data, owner->data, owner->len) &&
		(sop->so_client->cl_clientid.cl_id == clid->cl_id);
}

static struct nfs4_stateowner *
find_openstateowner_str(unsigned int hashval, struct nfsd4_open *open)
{
	struct nfs4_stateowner *so = NULL;

	list_for_each_entry(so, &ownerstr_hashtbl[hashval], so_strhash) {
		if (same_owner_str(so, &open->op_owner, &open->op_clientid))
			return so;
	}
	return NULL;
}

/* search file_hashtbl[] for file */
static struct nfs4_file *
find_file(struct inode *ino)
{
	unsigned int hashval = file_hashval(ino);
	struct nfs4_file *fp;

	list_for_each_entry(fp, &file_hashtbl[hashval], fi_hash) {
		if (fp->fi_inode == ino) {
			get_nfs4_file(fp);
			return fp;
		}
	}
	return NULL;
}

static inline int access_valid(u32 x)
{
	if (x < NFS4_SHARE_ACCESS_READ)
		return 0;
	if (x > NFS4_SHARE_ACCESS_BOTH)
		return 0;
	return 1;
}

static inline int deny_valid(u32 x)
{
	/* Note: unlike access bits, deny bits may be zero. */
	return x <= NFS4_SHARE_DENY_BOTH;
static unsigned int clientid_hashval(u32 id)
{
	return id & CLIENT_HASH_MASK;
}

static unsigned int clientstr_hashval(const char *name)
{
	return opaque_hashval(name, 8) & CLIENT_HASH_MASK;
}

/*
 * We store the NONE, READ, WRITE, and BOTH bits separately in the
 * st_{access,deny}_bmap field of the stateid, in order to track not
 * only what share bits are currently in force, but also what
 * combinations of share bits previous opens have used.  This allows us
 * to enforce the recommendation of rfc 3530 14.2.19 that the server
 * return an error if the client attempt to downgrade to a combination
 * of share bits not explicable by closing some of its previous opens.
 *
 * XXX: This enforcement is actually incomplete, since we don't keep
 * track of access/deny bit combinations; so, e.g., we allow:
 *
 *	OPEN allow read, deny write
 *	OPEN allow both, deny none
 *	DOWNGRADE allow read, deny none
 *
 * which we should reject.
 */
static void
set_access(unsigned int *access, unsigned long bmap) {
	int i;

	*access = 0;
	for (i = 1; i < 4; i++) {
		if (test_bit(i, &bmap))
			*access |= i;
	}
}

static void
set_deny(unsigned int *deny, unsigned long bmap) {
	int i;

	*deny = 0;
	for (i = 0; i < 4; i++) {
		if (test_bit(i, &bmap))
			*deny |= i ;
	}
}

static int
test_share(struct nfs4_stateid *stp, struct nfsd4_open *open) {
	unsigned int access, deny;

	set_access(&access, stp->st_access_bmap);
	set_deny(&deny, stp->st_deny_bmap);
	if ((access & open->op_share_deny) || (deny & open->op_share_access))
		return 0;
	return 1;
static unsigned int
bmap_to_share_mode(unsigned long bmap) {
	int i;
	unsigned int access = 0;

	for (i = 1; i < 4; i++) {
		if (test_bit(i, &bmap))
			access |= i;
	}
	return access;
}

/* set share access for a given stateid */
static inline void
set_access(u32 access, struct nfs4_ol_stateid *stp)
{
	unsigned char mask = 1 << access;

	WARN_ON_ONCE(access > NFS4_SHARE_ACCESS_BOTH);
	stp->st_access_bmap |= mask;
}

/* clear share access for a given stateid */
static inline void
clear_access(u32 access, struct nfs4_ol_stateid *stp)
{
	unsigned char mask = 1 << access;

	WARN_ON_ONCE(access > NFS4_SHARE_ACCESS_BOTH);
	stp->st_access_bmap &= ~mask;
}

/* test whether a given stateid has access */
static inline bool
test_access(u32 access, struct nfs4_ol_stateid *stp)
{
	unsigned char mask = 1 << access;

	return (bool)(stp->st_access_bmap & mask);
}

/* set share deny for a given stateid */
static inline void
set_deny(u32 deny, struct nfs4_ol_stateid *stp)
{
	unsigned char mask = 1 << deny;

	WARN_ON_ONCE(deny > NFS4_SHARE_DENY_BOTH);
	stp->st_deny_bmap |= mask;
}

/* clear share deny for a given stateid */
static inline void
clear_deny(u32 deny, struct nfs4_ol_stateid *stp)
{
	unsigned char mask = 1 << deny;

	WARN_ON_ONCE(deny > NFS4_SHARE_DENY_BOTH);
	stp->st_deny_bmap &= ~mask;
}

/* test whether a given stateid is denying specific access */
static inline bool
test_deny(u32 deny, struct nfs4_ol_stateid *stp)
{
	unsigned char mask = 1 << deny;

	return (bool)(stp->st_deny_bmap & mask);
}

static int nfs4_access_to_omode(u32 access)
{
	switch (access & NFS4_SHARE_ACCESS_BOTH) {
	case NFS4_SHARE_ACCESS_READ:
		return O_RDONLY;
	case NFS4_SHARE_ACCESS_WRITE:
		return O_WRONLY;
	case NFS4_SHARE_ACCESS_BOTH:
		return O_RDWR;
	}
	WARN_ON_ONCE(1);
	return O_RDONLY;
}

/*
 * A stateid that had a deny mode associated with it is being released
 * or downgraded. Recalculate the deny mode on the file.
 */
static void
recalculate_deny_mode(struct nfs4_file *fp)
{
	struct nfs4_ol_stateid *stp;

	spin_lock(&fp->fi_lock);
	fp->fi_share_deny = 0;
	list_for_each_entry(stp, &fp->fi_stateids, st_perfile)
		fp->fi_share_deny |= bmap_to_share_mode(stp->st_deny_bmap);
	spin_unlock(&fp->fi_lock);
}

static void
reset_union_bmap_deny(u32 deny, struct nfs4_ol_stateid *stp)
{
	int i;
	bool change = false;

	for (i = 1; i < 4; i++) {
		if ((i & deny) != i) {
			change = true;
			clear_deny(i, stp);
		}
	}

	/* Recalculate per-file deny mode if there was a change */
	if (change)
		recalculate_deny_mode(stp->st_stid.sc_file);
}

/* release all access and file references for a given stateid */
static void
release_all_access(struct nfs4_ol_stateid *stp)
{
	int i;
	struct nfs4_file *fp = stp->st_stid.sc_file;

	if (fp && stp->st_deny_bmap != 0)
		recalculate_deny_mode(fp);

	for (i = 1; i < 4; i++) {
		if (test_access(i, stp))
			nfs4_file_put_access(stp->st_stid.sc_file, i);
		clear_access(i, stp);
	}
}

static inline void nfs4_free_stateowner(struct nfs4_stateowner *sop)
{
	kfree(sop->so_owner.data);
	sop->so_ops->so_free(sop);
}

static void nfs4_put_stateowner(struct nfs4_stateowner *sop)
{
	struct nfs4_client *clp = sop->so_client;

	might_lock(&clp->cl_lock);

	if (!atomic_dec_and_lock(&sop->so_count, &clp->cl_lock))
		return;
	sop->so_ops->so_unhash(sop);
	spin_unlock(&clp->cl_lock);
	nfs4_free_stateowner(sop);
}

static bool unhash_ol_stateid(struct nfs4_ol_stateid *stp)
{
	struct nfs4_file *fp = stp->st_stid.sc_file;

	lockdep_assert_held(&stp->st_stateowner->so_client->cl_lock);

	if (list_empty(&stp->st_perfile))
		return false;

	spin_lock(&fp->fi_lock);
	list_del_init(&stp->st_perfile);
	spin_unlock(&fp->fi_lock);
	list_del(&stp->st_perstateowner);
	return true;
}

static void nfs4_free_ol_stateid(struct nfs4_stid *stid)
{
	struct nfs4_ol_stateid *stp = openlockstateid(stid);

	put_clnt_odstate(stp->st_clnt_odstate);
	release_all_access(stp);
	if (stp->st_stateowner)
		nfs4_put_stateowner(stp->st_stateowner);
	kmem_cache_free(stateid_slab, stid);
}

static void nfs4_free_lock_stateid(struct nfs4_stid *stid)
{
	struct nfs4_ol_stateid *stp = openlockstateid(stid);
	struct nfs4_lockowner *lo = lockowner(stp->st_stateowner);
	struct file *file;

	file = find_any_file(stp->st_stid.sc_file);
	if (file)
		filp_close(file, (fl_owner_t)lo);
	nfs4_free_ol_stateid(stid);
}

/*
 * Put the persistent reference to an already unhashed generic stateid, while
 * holding the cl_lock. If it's the last reference, then put it onto the
 * reaplist for later destruction.
 */
static void put_ol_stateid_locked(struct nfs4_ol_stateid *stp,
				       struct list_head *reaplist)
{
	struct nfs4_stid *s = &stp->st_stid;
	struct nfs4_client *clp = s->sc_client;

	lockdep_assert_held(&clp->cl_lock);

	WARN_ON_ONCE(!list_empty(&stp->st_locks));

	if (!atomic_dec_and_test(&s->sc_count)) {
		wake_up_all(&close_wq);
		return;
	}

	idr_remove(&clp->cl_stateids, s->sc_stateid.si_opaque.so_id);
	list_add(&stp->st_locks, reaplist);
}

static bool unhash_lock_stateid(struct nfs4_ol_stateid *stp)
{
	lockdep_assert_held(&stp->st_stid.sc_client->cl_lock);

	list_del_init(&stp->st_locks);
	nfs4_unhash_stid(&stp->st_stid);
	return unhash_ol_stateid(stp);
}

static void release_lock_stateid(struct nfs4_ol_stateid *stp)
{
	struct nfs4_client *clp = stp->st_stid.sc_client;
	bool unhashed;

	spin_lock(&clp->cl_lock);
	unhashed = unhash_lock_stateid(stp);
	spin_unlock(&clp->cl_lock);
	if (unhashed)
		nfs4_put_stid(&stp->st_stid);
}

static void unhash_lockowner_locked(struct nfs4_lockowner *lo)
{
	struct nfs4_client *clp = lo->lo_owner.so_client;

	lockdep_assert_held(&clp->cl_lock);

	list_del_init(&lo->lo_owner.so_strhash);
}

/*
 * Free a list of generic stateids that were collected earlier after being
 * fully unhashed.
 */
static void
free_ol_stateid_reaplist(struct list_head *reaplist)
{
	struct nfs4_ol_stateid *stp;
	struct nfs4_file *fp;

	might_sleep();

	while (!list_empty(reaplist)) {
		stp = list_first_entry(reaplist, struct nfs4_ol_stateid,
				       st_locks);
		list_del(&stp->st_locks);
		fp = stp->st_stid.sc_file;
		stp->st_stid.sc_free(&stp->st_stid);
		if (fp)
			put_nfs4_file(fp);
	}
}

static void release_open_stateid_locks(struct nfs4_ol_stateid *open_stp,
				       struct list_head *reaplist)
{
	struct nfs4_ol_stateid *stp;

	lockdep_assert_held(&open_stp->st_stid.sc_client->cl_lock);

	while (!list_empty(&open_stp->st_locks)) {
		stp = list_entry(open_stp->st_locks.next,
				struct nfs4_ol_stateid, st_locks);
		WARN_ON(!unhash_lock_stateid(stp));
		put_ol_stateid_locked(stp, reaplist);
	}
}

static bool unhash_open_stateid(struct nfs4_ol_stateid *stp,
				struct list_head *reaplist)
{
	bool unhashed;

	lockdep_assert_held(&stp->st_stid.sc_client->cl_lock);

	unhashed = unhash_ol_stateid(stp);
	release_open_stateid_locks(stp, reaplist);
	return unhashed;
}

static void release_open_stateid(struct nfs4_ol_stateid *stp)
{
	LIST_HEAD(reaplist);

	spin_lock(&stp->st_stid.sc_client->cl_lock);
	if (unhash_open_stateid(stp, &reaplist))
		put_ol_stateid_locked(stp, &reaplist);
	spin_unlock(&stp->st_stid.sc_client->cl_lock);
	free_ol_stateid_reaplist(&reaplist);
}

static bool nfs4_openowner_unhashed(struct nfs4_openowner *oo)
{
	lockdep_assert_held(&oo->oo_owner.so_client->cl_lock);

	return list_empty(&oo->oo_owner.so_strhash) &&
		list_empty(&oo->oo_perclient);
}

static void unhash_openowner_locked(struct nfs4_openowner *oo)
{
	struct nfs4_client *clp = oo->oo_owner.so_client;

	lockdep_assert_held(&clp->cl_lock);

	list_del_init(&oo->oo_owner.so_strhash);
	list_del_init(&oo->oo_perclient);
}

static void release_last_closed_stateid(struct nfs4_openowner *oo)
{
	struct nfsd_net *nn = net_generic(oo->oo_owner.so_client->net,
					  nfsd_net_id);
	struct nfs4_ol_stateid *s;

	spin_lock(&nn->client_lock);
	s = oo->oo_last_closed_stid;
	if (s) {
		list_del_init(&oo->oo_close_lru);
		oo->oo_last_closed_stid = NULL;
	}
	spin_unlock(&nn->client_lock);
	if (s)
		nfs4_put_stid(&s->st_stid);
}

static void release_openowner(struct nfs4_openowner *oo)
{
	struct nfs4_ol_stateid *stp;
	struct nfs4_client *clp = oo->oo_owner.so_client;
	struct list_head reaplist;

	INIT_LIST_HEAD(&reaplist);

	spin_lock(&clp->cl_lock);
	unhash_openowner_locked(oo);
	while (!list_empty(&oo->oo_owner.so_stateids)) {
		stp = list_first_entry(&oo->oo_owner.so_stateids,
				struct nfs4_ol_stateid, st_perstateowner);
		if (unhash_open_stateid(stp, &reaplist))
			put_ol_stateid_locked(stp, &reaplist);
	}
	spin_unlock(&clp->cl_lock);
	free_ol_stateid_reaplist(&reaplist);
	release_last_closed_stateid(oo);
	nfs4_put_stateowner(&oo->oo_owner);
}

static inline int
hash_sessionid(struct nfs4_sessionid *sessionid)
{
	struct nfsd4_sessionid *sid = (struct nfsd4_sessionid *)sessionid;

	return sid->sequence % SESSION_HASH_SIZE;
}

#ifdef CONFIG_SUNRPC_DEBUG
static inline void
dump_sessionid(const char *fn, struct nfs4_sessionid *sessionid)
{
	u32 *ptr = (u32 *)(&sessionid->data[0]);
	dprintk("%s: %u:%u:%u:%u\n", fn, ptr[0], ptr[1], ptr[2], ptr[3]);
}
#else
static inline void
dump_sessionid(const char *fn, struct nfs4_sessionid *sessionid)
{
}
#endif

/*
 * Bump the seqid on cstate->replay_owner, and clear replay_owner if it
 * won't be used for replay.
 */
void nfsd4_bump_seqid(struct nfsd4_compound_state *cstate, __be32 nfserr)
{
	struct nfs4_stateowner *so = cstate->replay_owner;

	if (nfserr == nfserr_replay_me)
		return;

	if (!seqid_mutating_err(ntohl(nfserr))) {
		nfsd4_cstate_clear_replay(cstate);
		return;
	}
	if (!so)
		return;
	if (so->so_is_open_owner)
		release_last_closed_stateid(openowner(so));
	so->so_seqid++;
	return;
}

static void
gen_sessionid(struct nfsd4_session *ses)
{
	struct nfs4_client *clp = ses->se_client;
	struct nfsd4_sessionid *sid;

	sid = (struct nfsd4_sessionid *)ses->se_sessionid.data;
	sid->clientid = clp->cl_clientid;
	sid->sequence = current_sessionid++;
	sid->reserved = 0;
}

/*
 * The protocol defines ca_maxresponssize_cached to include the size of
 * the rpc header, but all we need to cache is the data starting after
 * the end of the initial SEQUENCE operation--the rest we regenerate
 * each time.  Therefore we can advertise a ca_maxresponssize_cached
 * value that is the number of bytes in our cache plus a few additional
 * bytes.  In order to stay on the safe side, and not promise more than
 * we can cache, those additional bytes must be the minimum possible: 24
 * bytes of rpc header (xid through accept state, with AUTH_NULL
 * verifier), 12 for the compound header (with zero-length tag), and 44
 * for the SEQUENCE op response:
 */
#define NFSD_MIN_HDR_SEQ_SZ  (24 + 12 + 44)

static void
free_session_slots(struct nfsd4_session *ses)
{
	int i;

	for (i = 0; i < ses->se_fchannel.maxreqs; i++)
		kfree(ses->se_slots[i]);
}

/*
 * We don't actually need to cache the rpc and session headers, so we
 * can allocate a little less for each slot:
 */
static inline u32 slot_bytes(struct nfsd4_channel_attrs *ca)
{
	u32 size;

	if (ca->maxresp_cached < NFSD_MIN_HDR_SEQ_SZ)
		size = 0;
	else
		size = ca->maxresp_cached - NFSD_MIN_HDR_SEQ_SZ;
	return size + sizeof(struct nfsd4_slot);
}

/*
 * XXX: If we run out of reserved DRC memory we could (up to a point)
 * re-negotiate active sessions and reduce their slot usage to make
 * room for new connections. For now we just fail the create session.
 */
static u32 nfsd4_get_drc_mem(struct nfsd4_channel_attrs *ca)
{
	u32 slotsize = slot_bytes(ca);
	u32 num = ca->maxreqs;
	unsigned long avail, total_avail;

	spin_lock(&nfsd_drc_lock);
	total_avail = nfsd_drc_max_mem - nfsd_drc_mem_used;
	avail = min((unsigned long)NFSD_MAX_MEM_PER_SESSION, total_avail);
	/*
	 * Never use more than a third of the remaining memory,
	 * unless it's the only way to give this client a slot:
	 */
	avail = clamp_t(unsigned long, avail, slotsize, total_avail/3);
	num = min_t(int, num, avail / slotsize);
	nfsd_drc_mem_used += num * slotsize;
	spin_unlock(&nfsd_drc_lock);

	return num;
}

static void nfsd4_put_drc_mem(struct nfsd4_channel_attrs *ca)
{
	int slotsize = slot_bytes(ca);

	spin_lock(&nfsd_drc_lock);
	nfsd_drc_mem_used -= slotsize * ca->maxreqs;
	spin_unlock(&nfsd_drc_lock);
}

static struct nfsd4_session *alloc_session(struct nfsd4_channel_attrs *fattrs,
					   struct nfsd4_channel_attrs *battrs)
{
	int numslots = fattrs->maxreqs;
	int slotsize = slot_bytes(fattrs);
	struct nfsd4_session *new;
	int mem, i;

	BUILD_BUG_ON(NFSD_MAX_SLOTS_PER_SESSION * sizeof(struct nfsd4_slot *)
			+ sizeof(struct nfsd4_session) > PAGE_SIZE);
	mem = numslots * sizeof(struct nfsd4_slot *);

	new = kzalloc(sizeof(*new) + mem, GFP_KERNEL);
	if (!new)
		return NULL;
	/* allocate each struct nfsd4_slot and data cache in one piece */
	for (i = 0; i < numslots; i++) {
		new->se_slots[i] = kzalloc(slotsize, GFP_KERNEL);
		if (!new->se_slots[i])
			goto out_free;
	}

	memcpy(&new->se_fchannel, fattrs, sizeof(struct nfsd4_channel_attrs));
	memcpy(&new->se_bchannel, battrs, sizeof(struct nfsd4_channel_attrs));

	return new;
out_free:
	while (i--)
		kfree(new->se_slots[i]);
	kfree(new);
	return NULL;
}

static void free_conn(struct nfsd4_conn *c)
{
	svc_xprt_put(c->cn_xprt);
	kfree(c);
}

static void nfsd4_conn_lost(struct svc_xpt_user *u)
{
	struct nfsd4_conn *c = container_of(u, struct nfsd4_conn, cn_xpt_user);
	struct nfs4_client *clp = c->cn_session->se_client;

	spin_lock(&clp->cl_lock);
	if (!list_empty(&c->cn_persession)) {
		list_del(&c->cn_persession);
		free_conn(c);
	}
	nfsd4_probe_callback(clp);
	spin_unlock(&clp->cl_lock);
}

static struct nfsd4_conn *alloc_conn(struct svc_rqst *rqstp, u32 flags)
{
	struct nfsd4_conn *conn;

	conn = kmalloc(sizeof(struct nfsd4_conn), GFP_KERNEL);
	if (!conn)
		return NULL;
	svc_xprt_get(rqstp->rq_xprt);
	conn->cn_xprt = rqstp->rq_xprt;
	conn->cn_flags = flags;
	INIT_LIST_HEAD(&conn->cn_xpt_user.list);
	return conn;
}

static void __nfsd4_hash_conn(struct nfsd4_conn *conn, struct nfsd4_session *ses)
{
	conn->cn_session = ses;
	list_add(&conn->cn_persession, &ses->se_conns);
}

static void nfsd4_hash_conn(struct nfsd4_conn *conn, struct nfsd4_session *ses)
{
	struct nfs4_client *clp = ses->se_client;

	spin_lock(&clp->cl_lock);
	__nfsd4_hash_conn(conn, ses);
	spin_unlock(&clp->cl_lock);
}

static int nfsd4_register_conn(struct nfsd4_conn *conn)
{
	conn->cn_xpt_user.callback = nfsd4_conn_lost;
	return register_xpt_user(conn->cn_xprt, &conn->cn_xpt_user);
}

static void nfsd4_init_conn(struct svc_rqst *rqstp, struct nfsd4_conn *conn, struct nfsd4_session *ses)
{
	int ret;

	nfsd4_hash_conn(conn, ses);
	ret = nfsd4_register_conn(conn);
	if (ret)
		/* oops; xprt is already down: */
		nfsd4_conn_lost(&conn->cn_xpt_user);
	/* We may have gained or lost a callback channel: */
	nfsd4_probe_callback_sync(ses->se_client);
}

static struct nfsd4_conn *alloc_conn_from_crses(struct svc_rqst *rqstp, struct nfsd4_create_session *cses)
{
	u32 dir = NFS4_CDFC4_FORE;

	if (cses->flags & SESSION4_BACK_CHAN)
		dir |= NFS4_CDFC4_BACK;
	return alloc_conn(rqstp, dir);
}

/* must be called under client_lock */
static void nfsd4_del_conns(struct nfsd4_session *s)
{
	struct nfs4_client *clp = s->se_client;
	struct nfsd4_conn *c;

	spin_lock(&clp->cl_lock);
	while (!list_empty(&s->se_conns)) {
		c = list_first_entry(&s->se_conns, struct nfsd4_conn, cn_persession);
		list_del_init(&c->cn_persession);
		spin_unlock(&clp->cl_lock);

		unregister_xpt_user(c->cn_xprt, &c->cn_xpt_user);
		free_conn(c);

		spin_lock(&clp->cl_lock);
	}
	spin_unlock(&clp->cl_lock);
}

static void __free_session(struct nfsd4_session *ses)
{
	free_session_slots(ses);
	kfree(ses);
}

static void free_session(struct nfsd4_session *ses)
{
	nfsd4_del_conns(ses);
	nfsd4_put_drc_mem(&ses->se_fchannel);
	__free_session(ses);
}

static void init_session(struct svc_rqst *rqstp, struct nfsd4_session *new, struct nfs4_client *clp, struct nfsd4_create_session *cses)
{
	int idx;
	struct nfsd_net *nn = net_generic(SVC_NET(rqstp), nfsd_net_id);

	new->se_client = clp;
	gen_sessionid(new);

	INIT_LIST_HEAD(&new->se_conns);

	new->se_cb_seq_nr = 1;
	new->se_flags = cses->flags;
	new->se_cb_prog = cses->callback_prog;
	new->se_cb_sec = cses->cb_sec;
	atomic_set(&new->se_ref, 0);
	idx = hash_sessionid(&new->se_sessionid);
	list_add(&new->se_hash, &nn->sessionid_hashtbl[idx]);
	spin_lock(&clp->cl_lock);
	list_add(&new->se_perclnt, &clp->cl_sessions);
	spin_unlock(&clp->cl_lock);

	{
		struct sockaddr *sa = svc_addr(rqstp);
		/*
		 * This is a little silly; with sessions there's no real
		 * use for the callback address.  Use the peer address
		 * as a reasonable default for now, but consider fixing
		 * the rpc client not to require an address in the
		 * future:
		 */
		rpc_copy_addr((struct sockaddr *)&clp->cl_cb_conn.cb_addr, sa);
		clp->cl_cb_conn.cb_addrlen = svc_addr_len(sa);
	}
}

/* caller must hold client_lock */
static struct nfsd4_session *
__find_in_sessionid_hashtbl(struct nfs4_sessionid *sessionid, struct net *net)
{
	struct nfsd4_session *elem;
	int idx;
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);

	lockdep_assert_held(&nn->client_lock);

	dump_sessionid(__func__, sessionid);
	idx = hash_sessionid(sessionid);
	/* Search in the appropriate list */
	list_for_each_entry(elem, &nn->sessionid_hashtbl[idx], se_hash) {
		if (!memcmp(elem->se_sessionid.data, sessionid->data,
			    NFS4_MAX_SESSIONID_LEN)) {
			return elem;
		}
	}

	dprintk("%s: session not found\n", __func__);
	return NULL;
}

static struct nfsd4_session *
find_in_sessionid_hashtbl(struct nfs4_sessionid *sessionid, struct net *net,
		__be32 *ret)
{
	struct nfsd4_session *session;
	__be32 status = nfserr_badsession;

	session = __find_in_sessionid_hashtbl(sessionid, net);
	if (!session)
		goto out;
	status = nfsd4_get_session_locked(session);
	if (status)
		session = NULL;
out:
	*ret = status;
	return session;
}

/* caller must hold client_lock */
static void
unhash_session(struct nfsd4_session *ses)
{
	struct nfs4_client *clp = ses->se_client;
	struct nfsd_net *nn = net_generic(clp->net, nfsd_net_id);

	lockdep_assert_held(&nn->client_lock);

	list_del(&ses->se_hash);
	spin_lock(&ses->se_client->cl_lock);
	list_del(&ses->se_perclnt);
	spin_unlock(&ses->se_client->cl_lock);
}

/* SETCLIENTID and SETCLIENTID_CONFIRM Helper functions */
static int
STALE_CLIENTID(clientid_t *clid, struct nfsd_net *nn)
{
	/*
	 * We're assuming the clid was not given out from a boot
	 * precisely 2^32 (about 136 years) before this one.  That seems
	 * a safe assumption:
	 */
	if (clid->cl_boot == (u32)nn->boot_time)
		return 0;
	dprintk("NFSD stale clientid (%08x/%08x) boot_time %08lx\n",
		clid->cl_boot, clid->cl_id, nn->boot_time);
	return 1;
}

/* 
 * XXX Should we use a slab cache ?
 * This type of memory management is somewhat inefficient, but we use it
 * anyway since SETCLIENTID is not a common operation.
 */
static struct nfs4_client *alloc_client(struct xdr_netobj name)
{
	struct nfs4_client *clp;
	int i;

	clp = kzalloc(sizeof(struct nfs4_client), GFP_KERNEL);
	if (clp == NULL)
		return NULL;
	clp->cl_name.data = kmemdup(name.data, name.len, GFP_KERNEL);
	if (clp->cl_name.data == NULL)
		goto err_no_name;
	clp->cl_ownerstr_hashtbl = kmalloc(sizeof(struct list_head) *
			OWNER_HASH_SIZE, GFP_KERNEL);
	if (!clp->cl_ownerstr_hashtbl)
		goto err_no_hashtbl;
	for (i = 0; i < OWNER_HASH_SIZE; i++)
		INIT_LIST_HEAD(&clp->cl_ownerstr_hashtbl[i]);
	clp->cl_name.len = name.len;
	INIT_LIST_HEAD(&clp->cl_sessions);
	idr_init(&clp->cl_stateids);
	atomic_set(&clp->cl_refcount, 0);
	clp->cl_cb_state = NFSD4_CB_UNKNOWN;
	INIT_LIST_HEAD(&clp->cl_idhash);
	INIT_LIST_HEAD(&clp->cl_openowners);
	INIT_LIST_HEAD(&clp->cl_delegations);
	INIT_LIST_HEAD(&clp->cl_lru);
	INIT_LIST_HEAD(&clp->cl_revoked);
#ifdef CONFIG_NFSD_PNFS
	INIT_LIST_HEAD(&clp->cl_lo_states);
#endif
	spin_lock_init(&clp->cl_lock);
	rpc_init_wait_queue(&clp->cl_cb_waitq, "Backchannel slot table");
	return clp;
err_no_hashtbl:
	kfree(clp->cl_name.data);
err_no_name:
	kfree(clp);
	return NULL;
}

static void
free_client(struct nfs4_client *clp)
{
	while (!list_empty(&clp->cl_sessions)) {
		struct nfsd4_session *ses;
		ses = list_entry(clp->cl_sessions.next, struct nfsd4_session,
				se_perclnt);
		list_del(&ses->se_perclnt);
		WARN_ON_ONCE(atomic_read(&ses->se_ref));
		free_session(ses);
	}
	rpc_destroy_wait_queue(&clp->cl_cb_waitq);
	free_svc_cred(&clp->cl_cred);
	kfree(clp->cl_ownerstr_hashtbl);
	kfree(clp->cl_name.data);
	idr_destroy(&clp->cl_stateids);
	kfree(clp);
}

/* must be called under the client_lock */
static void
unhash_client_locked(struct nfs4_client *clp)
{
	struct nfsd_net *nn = net_generic(clp->net, nfsd_net_id);
	struct nfsd4_session *ses;

	lockdep_assert_held(&nn->client_lock);

	/* Mark the client as expired! */
	clp->cl_time = 0;
	/* Make it invisible */
	if (!list_empty(&clp->cl_idhash)) {
		list_del_init(&clp->cl_idhash);
		if (test_bit(NFSD4_CLIENT_CONFIRMED, &clp->cl_flags))
			rb_erase(&clp->cl_namenode, &nn->conf_name_tree);
		else
			rb_erase(&clp->cl_namenode, &nn->unconf_name_tree);
	}
	list_del_init(&clp->cl_lru);
	spin_lock(&clp->cl_lock);
	list_for_each_entry(ses, &clp->cl_sessions, se_perclnt)
		list_del_init(&ses->se_hash);
	spin_unlock(&clp->cl_lock);
}

static void
unhash_client(struct nfs4_client *clp)
{
	struct nfsd_net *nn = net_generic(clp->net, nfsd_net_id);

	spin_lock(&nn->client_lock);
	unhash_client_locked(clp);
	spin_unlock(&nn->client_lock);
}

static __be32 mark_client_expired_locked(struct nfs4_client *clp)
{
	if (atomic_read(&clp->cl_refcount))
		return nfserr_jukebox;
	unhash_client_locked(clp);
	return nfs_ok;
}

static void
__destroy_client(struct nfs4_client *clp)
{
	struct nfs4_openowner *oo;
	struct nfs4_delegation *dp;
	struct list_head reaplist;

	INIT_LIST_HEAD(&reaplist);
	spin_lock(&state_lock);
	while (!list_empty(&clp->cl_delegations)) {
		dp = list_entry(clp->cl_delegations.next, struct nfs4_delegation, dl_perclnt);
		WARN_ON(!unhash_delegation_locked(dp));
		list_add(&dp->dl_recall_lru, &reaplist);
	}
	spin_unlock(&state_lock);
	while (!list_empty(&reaplist)) {
		dp = list_entry(reaplist.next, struct nfs4_delegation, dl_recall_lru);
		list_del_init(&dp->dl_recall_lru);
		put_clnt_odstate(dp->dl_clnt_odstate);
		nfs4_put_deleg_lease(dp->dl_stid.sc_file);
		nfs4_put_stid(&dp->dl_stid);
	}
	while (!list_empty(&clp->cl_revoked)) {
		dp = list_entry(clp->cl_revoked.next, struct nfs4_delegation, dl_recall_lru);
		list_del_init(&dp->dl_recall_lru);
		nfs4_put_stid(&dp->dl_stid);
	}
	while (!list_empty(&clp->cl_openowners)) {
		oo = list_entry(clp->cl_openowners.next, struct nfs4_openowner, oo_perclient);
		nfs4_get_stateowner(&oo->oo_owner);
		release_openowner(oo);
	}
	nfsd4_return_all_client_layouts(clp);
	nfsd4_shutdown_callback(clp);
	if (clp->cl_cb_conn.cb_xprt)
		svc_xprt_put(clp->cl_cb_conn.cb_xprt);
	free_client(clp);
}

static void
destroy_client(struct nfs4_client *clp)
{
	unhash_client(clp);
	__destroy_client(clp);
}

static void expire_client(struct nfs4_client *clp)
{
	unhash_client(clp);
	nfsd4_client_record_remove(clp);
	__destroy_client(clp);
}

static void copy_verf(struct nfs4_client *target, nfs4_verifier *source)
{
	memcpy(target->cl_verifier.data, source->data,
			sizeof(target->cl_verifier.data));
}

static void copy_clid(struct nfs4_client *target, struct nfs4_client *source)
{
	target->cl_clientid.cl_boot = source->cl_clientid.cl_boot; 
	target->cl_clientid.cl_id = source->cl_clientid.cl_id; 
}

static int copy_cred(struct svc_cred *target, struct svc_cred *source)
{
	if (source->cr_principal) {
		target->cr_principal =
				kstrdup(source->cr_principal, GFP_KERNEL);
		if (target->cr_principal == NULL)
			return -ENOMEM;
	} else
		target->cr_principal = NULL;
	target->cr_flavor = source->cr_flavor;
	target->cr_uid = source->cr_uid;
	target->cr_gid = source->cr_gid;
	target->cr_group_info = source->cr_group_info;
	get_group_info(target->cr_group_info);
	target->cr_gss_mech = source->cr_gss_mech;
	if (source->cr_gss_mech)
		gss_mech_get(source->cr_gss_mech);
	return 0;
}

static int
compare_blob(const struct xdr_netobj *o1, const struct xdr_netobj *o2)
{
	if (o1->len < o2->len)
		return -1;
	if (o1->len > o2->len)
		return 1;
	return memcmp(o1->data, o2->data, o1->len);
}

static int same_name(const char *n1, const char *n2)
{
	return 0 == memcmp(n1, n2, HEXDIR_LEN);
}

static int
same_verf(nfs4_verifier *v1, nfs4_verifier *v2)
{
	return 0 == memcmp(v1->data, v2->data, sizeof(v1->data));
}

static int
same_clid(clientid_t *cl1, clientid_t *cl2)
{
	return (cl1->cl_boot == cl2->cl_boot) && (cl1->cl_id == cl2->cl_id);
}

static bool groups_equal(struct group_info *g1, struct group_info *g2)
{
	int i;

	if (g1->ngroups != g2->ngroups)
		return false;
	for (i=0; i<g1->ngroups; i++)
		if (!gid_eq(GROUP_AT(g1, i), GROUP_AT(g2, i)))
			return false;
	return true;
}

/*
 * RFC 3530 language requires clid_inuse be returned when the
 * "principal" associated with a requests differs from that previously
 * used.  We use uid, gid's, and gss principal string as our best
 * approximation.  We also don't want to allow non-gss use of a client
 * established using gss: in theory cr_principal should catch that
 * change, but in practice cr_principal can be null even in the gss case
 * since gssd doesn't always pass down a principal string.
 */
static bool is_gss_cred(struct svc_cred *cr)
{
	/* Is cr_flavor one of the gss "pseudoflavors"?: */
	return (cr->cr_flavor > RPC_AUTH_MAXFLAVOR);
}


static bool
same_creds(struct svc_cred *cr1, struct svc_cred *cr2)
{
	if ((is_gss_cred(cr1) != is_gss_cred(cr2))
		|| (!uid_eq(cr1->cr_uid, cr2->cr_uid))
		|| (!gid_eq(cr1->cr_gid, cr2->cr_gid))
		|| !groups_equal(cr1->cr_group_info, cr2->cr_group_info))
		return false;
	if (cr1->cr_principal == cr2->cr_principal)
		return true;
	if (!cr1->cr_principal || !cr2->cr_principal)
		return false;
	return 0 == strcmp(cr1->cr_principal, cr2->cr_principal);
}

static bool svc_rqst_integrity_protected(struct svc_rqst *rqstp)
{
	struct svc_cred *cr = &rqstp->rq_cred;
	u32 service;

	if (!cr->cr_gss_mech)
		return false;
	service = gss_pseudoflavor_to_service(cr->cr_gss_mech, cr->cr_flavor);
	return service == RPC_GSS_SVC_INTEGRITY ||
	       service == RPC_GSS_SVC_PRIVACY;
}

static bool mach_creds_match(struct nfs4_client *cl, struct svc_rqst *rqstp)
{
	struct svc_cred *cr = &rqstp->rq_cred;

	if (!cl->cl_mach_cred)
		return true;
	if (cl->cl_cred.cr_gss_mech != cr->cr_gss_mech)
		return false;
	if (!svc_rqst_integrity_protected(rqstp))
		return false;
	if (!cr->cr_principal)
		return false;
	return 0 == strcmp(cl->cl_cred.cr_principal, cr->cr_principal);
}

static void gen_confirm(struct nfs4_client *clp, struct nfsd_net *nn)
{
	__be32 verf[2];

	/*
	 * This is opaque to client, so no need to byte-swap. Use
	 * __force to keep sparse happy
	 */
	verf[0] = (__force __be32)get_seconds();
	verf[1] = (__force __be32)nn->clverifier_counter++;
	memcpy(clp->cl_confirm.data, verf, sizeof(clp->cl_confirm.data));
}

static void gen_clid(struct nfs4_client *clp, struct nfsd_net *nn)
{
	clp->cl_clientid.cl_boot = nn->boot_time;
	clp->cl_clientid.cl_id = nn->clientid_counter++;
	gen_confirm(clp, nn);
}

static struct nfs4_stid *
find_stateid_locked(struct nfs4_client *cl, stateid_t *t)
{
	struct nfs4_stid *ret;

	ret = idr_find(&cl->cl_stateids, t->si_opaque.so_id);
	if (!ret || !ret->sc_type)
		return NULL;
	return ret;
}

static struct nfs4_stid *
find_stateid_by_type(struct nfs4_client *cl, stateid_t *t, char typemask)
{
	struct nfs4_stid *s;

	spin_lock(&cl->cl_lock);
	s = find_stateid_locked(cl, t);
	if (s != NULL) {
		if (typemask & s->sc_type)
			atomic_inc(&s->sc_count);
		else
			s = NULL;
	}
	spin_unlock(&cl->cl_lock);
	return s;
}

static struct nfs4_client *create_client(struct xdr_netobj name,
		struct svc_rqst *rqstp, nfs4_verifier *verf)
{
	struct nfs4_client *clp;
	struct sockaddr *sa = svc_addr(rqstp);
	int ret;
	struct net *net = SVC_NET(rqstp);

	clp = alloc_client(name);
	if (clp == NULL)
		return NULL;

	ret = copy_cred(&clp->cl_cred, &rqstp->rq_cred);
	if (ret) {
		free_client(clp);
		return NULL;
	}
	nfsd4_init_cb(&clp->cl_cb_null, clp, NULL, NFSPROC4_CLNT_CB_NULL);
	clp->cl_time = get_seconds();
	clear_bit(0, &clp->cl_cb_slot_busy);
	copy_verf(clp, verf);
	rpc_copy_addr((struct sockaddr *) &clp->cl_addr, sa);
	clp->cl_cb_session = NULL;
	clp->net = net;
	return clp;
}

static void
add_clp_to_name_tree(struct nfs4_client *new_clp, struct rb_root *root)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;
	struct nfs4_client *clp;

	while (*new) {
		clp = rb_entry(*new, struct nfs4_client, cl_namenode);
		parent = *new;

		if (compare_blob(&clp->cl_name, &new_clp->cl_name) > 0)
			new = &((*new)->rb_left);
		else
			new = &((*new)->rb_right);
	}

	rb_link_node(&new_clp->cl_namenode, parent, new);
	rb_insert_color(&new_clp->cl_namenode, root);
}

static struct nfs4_client *
find_clp_in_name_tree(struct xdr_netobj *name, struct rb_root *root)
{
	int cmp;
	struct rb_node *node = root->rb_node;
	struct nfs4_client *clp;

	while (node) {
		clp = rb_entry(node, struct nfs4_client, cl_namenode);
		cmp = compare_blob(&clp->cl_name, name);
		if (cmp > 0)
			node = node->rb_left;
		else if (cmp < 0)
			node = node->rb_right;
		else
			return clp;
	}
	return NULL;
}

static void
add_to_unconfirmed(struct nfs4_client *clp)
{
	unsigned int idhashval;
	struct nfsd_net *nn = net_generic(clp->net, nfsd_net_id);

	lockdep_assert_held(&nn->client_lock);

	clear_bit(NFSD4_CLIENT_CONFIRMED, &clp->cl_flags);
	add_clp_to_name_tree(clp, &nn->unconf_name_tree);
	idhashval = clientid_hashval(clp->cl_clientid.cl_id);
	list_add(&clp->cl_idhash, &nn->unconf_id_hashtbl[idhashval]);
	renew_client_locked(clp);
}

static void
move_to_confirmed(struct nfs4_client *clp)
{
	unsigned int idhashval = clientid_hashval(clp->cl_clientid.cl_id);
	struct nfsd_net *nn = net_generic(clp->net, nfsd_net_id);

	lockdep_assert_held(&nn->client_lock);

	dprintk("NFSD: move_to_confirm nfs4_client %p\n", clp);
	list_move(&clp->cl_idhash, &nn->conf_id_hashtbl[idhashval]);
	rb_erase(&clp->cl_namenode, &nn->unconf_name_tree);
	add_clp_to_name_tree(clp, &nn->conf_name_tree);
	set_bit(NFSD4_CLIENT_CONFIRMED, &clp->cl_flags);
	renew_client_locked(clp);
}

static struct nfs4_client *
find_client_in_id_table(struct list_head *tbl, clientid_t *clid, bool sessions)
{
	struct nfs4_client *clp;
	unsigned int idhashval = clientid_hashval(clid->cl_id);

	list_for_each_entry(clp, &tbl[idhashval], cl_idhash) {
		if (same_clid(&clp->cl_clientid, clid)) {
			if ((bool)clp->cl_minorversion != sessions)
				return NULL;
			renew_client_locked(clp);
			return clp;
		}
	}
	return NULL;
}

static struct nfs4_client *
find_confirmed_client(clientid_t *clid, bool sessions, struct nfsd_net *nn)
{
	struct list_head *tbl = nn->conf_id_hashtbl;

	lockdep_assert_held(&nn->client_lock);
	return find_client_in_id_table(tbl, clid, sessions);
}

static struct nfs4_client *
find_unconfirmed_client(clientid_t *clid, bool sessions, struct nfsd_net *nn)
{
	struct list_head *tbl = nn->unconf_id_hashtbl;

	lockdep_assert_held(&nn->client_lock);
	return find_client_in_id_table(tbl, clid, sessions);
}

static bool clp_used_exchangeid(struct nfs4_client *clp)
{
	return clp->cl_exchange_flags != 0;
} 

static struct nfs4_client *
find_confirmed_client_by_name(struct xdr_netobj *name, struct nfsd_net *nn)
{
	lockdep_assert_held(&nn->client_lock);
	return find_clp_in_name_tree(name, &nn->conf_name_tree);
}

static struct nfs4_client *
find_unconfirmed_client_by_name(struct xdr_netobj *name, struct nfsd_net *nn)
{
	lockdep_assert_held(&nn->client_lock);
	return find_clp_in_name_tree(name, &nn->unconf_name_tree);
}

static void
gen_callback(struct nfs4_client *clp, struct nfsd4_setclientid *se, struct svc_rqst *rqstp)
{
	struct nfs4_cb_conn *conn = &clp->cl_cb_conn;
	struct sockaddr	*sa = svc_addr(rqstp);
	u32 scopeid = rpc_get_scope_id(sa);
	unsigned short expected_family;

	/* Currently, we only support tcp and tcp6 for the callback channel */
	if (se->se_callback_netid_len == 3 &&
	    !memcmp(se->se_callback_netid_val, "tcp", 3))
		expected_family = AF_INET;
	else if (se->se_callback_netid_len == 4 &&
		 !memcmp(se->se_callback_netid_val, "tcp6", 4))
		expected_family = AF_INET6;
	else
		goto out_err;

	conn->cb_addrlen = rpc_uaddr2sockaddr(clp->net, se->se_callback_addr_val,
					    se->se_callback_addr_len,
					    (struct sockaddr *)&conn->cb_addr,
					    sizeof(conn->cb_addr));

	if (!conn->cb_addrlen || conn->cb_addr.ss_family != expected_family)
		goto out_err;

	if (conn->cb_addr.ss_family == AF_INET6)
		((struct sockaddr_in6 *)&conn->cb_addr)->sin6_scope_id = scopeid;

	conn->cb_prog = se->se_callback_prog;
	conn->cb_ident = se->se_callback_ident;
	memcpy(&conn->cb_saddr, &rqstp->rq_daddr, rqstp->rq_daddrlen);
	return;
out_err:
	conn->cb_addr.ss_family = AF_UNSPEC;
	conn->cb_addrlen = 0;
	dprintk(KERN_INFO "NFSD: this client (clientid %08x/%08x) "
		"will not receive delegations\n",
		clp->cl_clientid.cl_boot, clp->cl_clientid.cl_id);

	return;
}

/*
 * Cache a reply. nfsd4_check_resp_size() has bounded the cache size.
 */
static void
nfsd4_store_cache_entry(struct nfsd4_compoundres *resp)
{
	struct xdr_buf *buf = resp->xdr.buf;
	struct nfsd4_slot *slot = resp->cstate.slot;
	unsigned int base;

	dprintk("--> %s slot %p\n", __func__, slot);

	slot->sl_opcnt = resp->opcnt;
	slot->sl_status = resp->cstate.status;

	slot->sl_flags |= NFSD4_SLOT_INITIALIZED;
	if (nfsd4_not_cached(resp)) {
		slot->sl_datalen = 0;
		return;
	}
	base = resp->cstate.data_offset;
	slot->sl_datalen = buf->len - base;
	if (read_bytes_from_xdr_buf(buf, base, slot->sl_data, slot->sl_datalen))
		WARN("%s: sessions DRC could not cache compound\n", __func__);
	return;
}

/*
 * Encode the replay sequence operation from the slot values.
 * If cachethis is FALSE encode the uncached rep error on the next
 * operation which sets resp->p and increments resp->opcnt for
 * nfs4svc_encode_compoundres.
 *
 */
static __be32
nfsd4_enc_sequence_replay(struct nfsd4_compoundargs *args,
			  struct nfsd4_compoundres *resp)
{
	struct nfsd4_op *op;
	struct nfsd4_slot *slot = resp->cstate.slot;

	/* Encode the replayed sequence operation */
	op = &args->ops[resp->opcnt - 1];
	nfsd4_encode_operation(resp, op);

	/* Return nfserr_retry_uncached_rep in next operation. */
	if (args->opcnt > 1 && !(slot->sl_flags & NFSD4_SLOT_CACHETHIS)) {
		op = &args->ops[resp->opcnt++];
		op->status = nfserr_retry_uncached_rep;
		nfsd4_encode_operation(resp, op);
	}
	return op->status;
}

/*
 * The sequence operation is not cached because we can use the slot and
 * session values.
 */
static __be32
nfsd4_replay_cache_entry(struct nfsd4_compoundres *resp,
			 struct nfsd4_sequence *seq)
{
	struct nfsd4_slot *slot = resp->cstate.slot;
	struct xdr_stream *xdr = &resp->xdr;
	__be32 *p;
	__be32 status;

	dprintk("--> %s slot %p\n", __func__, slot);

	status = nfsd4_enc_sequence_replay(resp->rqstp->rq_argp, resp);
	if (status)
		return status;

	p = xdr_reserve_space(xdr, slot->sl_datalen);
	if (!p) {
		WARN_ON_ONCE(1);
		return nfserr_serverfault;
	}
	xdr_encode_opaque_fixed(p, slot->sl_data, slot->sl_datalen);
	xdr_commit_encode(xdr);

	resp->opcnt = slot->sl_opcnt;
	return slot->sl_status;
}

/*
 * Set the exchange_id flags returned by the server.
 */
static void
nfsd4_set_ex_flags(struct nfs4_client *new, struct nfsd4_exchange_id *clid)
{
#ifdef CONFIG_NFSD_PNFS
	new->cl_exchange_flags |= EXCHGID4_FLAG_USE_PNFS_MDS;
#else
	new->cl_exchange_flags |= EXCHGID4_FLAG_USE_NON_PNFS;
#endif

	/* Referrals are supported, Migration is not. */
	new->cl_exchange_flags |= EXCHGID4_FLAG_SUPP_MOVED_REFER;

	/* set the wire flags to return to client. */
	clid->flags = new->cl_exchange_flags;
}

static bool client_has_openowners(struct nfs4_client *clp)
{
	struct nfs4_openowner *oo;

	list_for_each_entry(oo, &clp->cl_openowners, oo_perclient) {
		if (!list_empty(&oo->oo_owner.so_stateids))
			return true;
	}
	return false;
}

static bool client_has_state(struct nfs4_client *clp)
{
	return client_has_openowners(clp)
#ifdef CONFIG_NFSD_PNFS
		|| !list_empty(&clp->cl_lo_states)
#endif
		|| !list_empty(&clp->cl_delegations)
		|| !list_empty(&clp->cl_sessions);
}

__be32
nfsd4_exchange_id(struct svc_rqst *rqstp,
		  struct nfsd4_compound_state *cstate,
		  struct nfsd4_exchange_id *exid)
{
	struct nfs4_client *conf, *new;
	struct nfs4_client *unconf = NULL;
	__be32 status;
	char			addr_str[INET6_ADDRSTRLEN];
	nfs4_verifier		verf = exid->verifier;
	struct sockaddr		*sa = svc_addr(rqstp);
	bool	update = exid->flags & EXCHGID4_FLAG_UPD_CONFIRMED_REC_A;
	struct nfsd_net		*nn = net_generic(SVC_NET(rqstp), nfsd_net_id);

	rpc_ntop(sa, addr_str, sizeof(addr_str));
	dprintk("%s rqstp=%p exid=%p clname.len=%u clname.data=%p "
		"ip_addr=%s flags %x, spa_how %d\n",
		__func__, rqstp, exid, exid->clname.len, exid->clname.data,
		addr_str, exid->flags, exid->spa_how);

	if (exid->flags & ~EXCHGID4_FLAG_MASK_A)
		return nfserr_inval;

	switch (exid->spa_how) {
	case SP4_MACH_CRED:
		if (!svc_rqst_integrity_protected(rqstp))
			return nfserr_inval;
	case SP4_NONE:
		break;
	default:				/* checked by xdr code */
		WARN_ON_ONCE(1);
	case SP4_SSV:
		return nfserr_encr_alg_unsupp;
	}

	new = create_client(exid->clname, rqstp, &verf);
	if (new == NULL)
		return nfserr_jukebox;

	/* Cases below refer to rfc 5661 section 18.35.4: */
	spin_lock(&nn->client_lock);
	conf = find_confirmed_client_by_name(&exid->clname, nn);
	if (conf) {
		bool creds_match = same_creds(&conf->cl_cred, &rqstp->rq_cred);
		bool verfs_match = same_verf(&verf, &conf->cl_verifier);

		if (update) {
			if (!clp_used_exchangeid(conf)) { /* buggy client */
				status = nfserr_inval;
				goto out;
			}
			if (!mach_creds_match(conf, rqstp)) {
				status = nfserr_wrong_cred;
				goto out;
			}
			if (!creds_match) { /* case 9 */
				status = nfserr_perm;
				goto out;
			}
			if (!verfs_match) { /* case 8 */
				status = nfserr_not_same;
				goto out;
			}
			/* case 6 */
			exid->flags |= EXCHGID4_FLAG_CONFIRMED_R;
			goto out_copy;
		}
		if (!creds_match) { /* case 3 */
			if (client_has_state(conf)) {
				status = nfserr_clid_inuse;
				goto out;
			}
			goto out_new;
		}
		if (verfs_match) { /* case 2 */
			conf->cl_exchange_flags |= EXCHGID4_FLAG_CONFIRMED_R;
			goto out_copy;
		}
		/* case 5, client reboot */
		conf = NULL;
		goto out_new;
	}

	if (update) { /* case 7 */
		status = nfserr_noent;
		goto out;
	}

	unconf  = find_unconfirmed_client_by_name(&exid->clname, nn);
	if (unconf) /* case 4, possible retry or client restart */
		unhash_client_locked(unconf);

	/* case 1 (normal case) */
out_new:
	if (conf) {
		status = mark_client_expired_locked(conf);
		if (status)
			goto out;
	}
	new->cl_minorversion = cstate->minorversion;
	new->cl_mach_cred = (exid->spa_how == SP4_MACH_CRED);

	gen_clid(new, nn);
	add_to_unconfirmed(new);
	swap(new, conf);
out_copy:
	exid->clientid.cl_boot = conf->cl_clientid.cl_boot;
	exid->clientid.cl_id = conf->cl_clientid.cl_id;

	exid->seqid = conf->cl_cs_slot.sl_seqid + 1;
	nfsd4_set_ex_flags(conf, exid);

	dprintk("nfsd4_exchange_id seqid %d flags %x\n",
		conf->cl_cs_slot.sl_seqid, conf->cl_exchange_flags);
	status = nfs_ok;

out:
	spin_unlock(&nn->client_lock);
	if (new)
		expire_client(new);
	if (unconf)
		expire_client(unconf);
	return status;
}

static __be32
check_slot_seqid(u32 seqid, u32 slot_seqid, int slot_inuse)
{
	dprintk("%s enter. seqid %d slot_seqid %d\n", __func__, seqid,
		slot_seqid);

	/* The slot is in use, and no response has been sent. */
	if (slot_inuse) {
		if (seqid == slot_seqid)
			return nfserr_jukebox;
		else
			return nfserr_seq_misordered;
	}
	/* Note unsigned 32-bit arithmetic handles wraparound: */
	if (likely(seqid == slot_seqid + 1))
		return nfs_ok;
	if (seqid == slot_seqid)
		return nfserr_replay_cache;
	return nfserr_seq_misordered;
}

/*
 * Cache the create session result into the create session single DRC
 * slot cache by saving the xdr structure. sl_seqid has been set.
 * Do this for solo or embedded create session operations.
 */
static void
nfsd4_cache_create_session(struct nfsd4_create_session *cr_ses,
			   struct nfsd4_clid_slot *slot, __be32 nfserr)
{
	slot->sl_status = nfserr;
	memcpy(&slot->sl_cr_ses, cr_ses, sizeof(*cr_ses));
}

static __be32
nfsd4_replay_create_session(struct nfsd4_create_session *cr_ses,
			    struct nfsd4_clid_slot *slot)
{
	memcpy(cr_ses, &slot->sl_cr_ses, sizeof(*cr_ses));
	return slot->sl_status;
}

#define NFSD_MIN_REQ_HDR_SEQ_SZ	((\
			2 * 2 + /* credential,verifier: AUTH_NULL, length 0 */ \
			1 +	/* MIN tag is length with zero, only length */ \
			3 +	/* version, opcount, opcode */ \
			XDR_QUADLEN(NFS4_MAX_SESSIONID_LEN) + \
				/* seqid, slotID, slotID, cache */ \
			4 ) * sizeof(__be32))

#define NFSD_MIN_RESP_HDR_SEQ_SZ ((\
			2 +	/* verifier: AUTH_NULL, length 0 */\
			1 +	/* status */ \
			1 +	/* MIN tag is length with zero, only length */ \
			3 +	/* opcount, opcode, opstatus*/ \
			XDR_QUADLEN(NFS4_MAX_SESSIONID_LEN) + \
				/* seqid, slotID, slotID, slotID, status */ \
			5 ) * sizeof(__be32))

static __be32 check_forechannel_attrs(struct nfsd4_channel_attrs *ca, struct nfsd_net *nn)
{
	u32 maxrpc = nn->nfsd_serv->sv_max_mesg;

	if (ca->maxreq_sz < NFSD_MIN_REQ_HDR_SEQ_SZ)
		return nfserr_toosmall;
	if (ca->maxresp_sz < NFSD_MIN_RESP_HDR_SEQ_SZ)
		return nfserr_toosmall;
	ca->headerpadsz = 0;
	ca->maxreq_sz = min_t(u32, ca->maxreq_sz, maxrpc);
	ca->maxresp_sz = min_t(u32, ca->maxresp_sz, maxrpc);
	ca->maxops = min_t(u32, ca->maxops, NFSD_MAX_OPS_PER_COMPOUND);
	ca->maxresp_cached = min_t(u32, ca->maxresp_cached,
			NFSD_SLOT_CACHE_SIZE + NFSD_MIN_HDR_SEQ_SZ);
	ca->maxreqs = min_t(u32, ca->maxreqs, NFSD_MAX_SLOTS_PER_SESSION);
	/*
	 * Note decreasing slot size below client's request may make it
	 * difficult for client to function correctly, whereas
	 * decreasing the number of slots will (just?) affect
	 * performance.  When short on memory we therefore prefer to
	 * decrease number of slots instead of their size.  Clients that
	 * request larger slots than they need will get poor results:
	 */
	ca->maxreqs = nfsd4_get_drc_mem(ca);
	if (!ca->maxreqs)
		return nfserr_jukebox;

	return nfs_ok;
}

#define NFSD_CB_MAX_REQ_SZ	((NFS4_enc_cb_recall_sz + \
				 RPC_MAX_HEADER_WITH_AUTH) * sizeof(__be32))
#define NFSD_CB_MAX_RESP_SZ	((NFS4_dec_cb_recall_sz + \
				 RPC_MAX_REPHEADER_WITH_AUTH) * sizeof(__be32))

static __be32 check_backchannel_attrs(struct nfsd4_channel_attrs *ca)
{
	ca->headerpadsz = 0;

	/*
	 * These RPC_MAX_HEADER macros are overkill, especially since we
	 * don't even do gss on the backchannel yet.  But this is still
	 * less than 1k.  Tighten up this estimate in the unlikely event
	 * it turns out to be a problem for some client:
	 */
	if (ca->maxreq_sz < NFSD_CB_MAX_REQ_SZ)
		return nfserr_toosmall;
	if (ca->maxresp_sz < NFSD_CB_MAX_RESP_SZ)
		return nfserr_toosmall;
	ca->maxresp_cached = 0;
	if (ca->maxops < 2)
		return nfserr_toosmall;

	return nfs_ok;
}

static __be32 nfsd4_check_cb_sec(struct nfsd4_cb_sec *cbs)
{
	switch (cbs->flavor) {
	case RPC_AUTH_NULL:
	case RPC_AUTH_UNIX:
		return nfs_ok;
	default:
		/*
		 * GSS case: the spec doesn't allow us to return this
		 * error.  But it also doesn't allow us not to support
		 * GSS.
		 * I'd rather this fail hard than return some error the
		 * client might think it can already handle:
		 */
		return nfserr_encr_alg_unsupp;
	}
}

__be32
nfsd4_create_session(struct svc_rqst *rqstp,
		     struct nfsd4_compound_state *cstate,
		     struct nfsd4_create_session *cr_ses)
{
	struct sockaddr *sa = svc_addr(rqstp);
	struct nfs4_client *conf, *unconf;
	struct nfs4_client *old = NULL;
	struct nfsd4_session *new;
	struct nfsd4_conn *conn;
	struct nfsd4_clid_slot *cs_slot = NULL;
	__be32 status = 0;
	struct nfsd_net *nn = net_generic(SVC_NET(rqstp), nfsd_net_id);

	if (cr_ses->flags & ~SESSION4_FLAG_MASK_A)
		return nfserr_inval;
	status = nfsd4_check_cb_sec(&cr_ses->cb_sec);
	if (status)
		return status;
	status = check_forechannel_attrs(&cr_ses->fore_channel, nn);
	if (status)
		return status;
	status = check_backchannel_attrs(&cr_ses->back_channel);
	if (status)
		goto out_release_drc_mem;
	status = nfserr_jukebox;
	new = alloc_session(&cr_ses->fore_channel, &cr_ses->back_channel);
	if (!new)
		goto out_release_drc_mem;
	conn = alloc_conn_from_crses(rqstp, cr_ses);
	if (!conn)
		goto out_free_session;

	spin_lock(&nn->client_lock);
	unconf = find_unconfirmed_client(&cr_ses->clientid, true, nn);
	conf = find_confirmed_client(&cr_ses->clientid, true, nn);
	WARN_ON_ONCE(conf && unconf);

	if (conf) {
		status = nfserr_wrong_cred;
		if (!mach_creds_match(conf, rqstp))
			goto out_free_conn;
		cs_slot = &conf->cl_cs_slot;
		status = check_slot_seqid(cr_ses->seqid, cs_slot->sl_seqid, 0);
		if (status) {
			if (status == nfserr_replay_cache)
				status = nfsd4_replay_create_session(cr_ses, cs_slot);
			goto out_free_conn;
		}
	} else if (unconf) {
		if (!same_creds(&unconf->cl_cred, &rqstp->rq_cred) ||
		    !rpc_cmp_addr(sa, (struct sockaddr *) &unconf->cl_addr)) {
			status = nfserr_clid_inuse;
			goto out_free_conn;
		}
		status = nfserr_wrong_cred;
		if (!mach_creds_match(unconf, rqstp))
			goto out_free_conn;
		cs_slot = &unconf->cl_cs_slot;
		status = check_slot_seqid(cr_ses->seqid, cs_slot->sl_seqid, 0);
		if (status) {
			/* an unconfirmed replay returns misordered */
			status = nfserr_seq_misordered;
			goto out_free_conn;
		}
		old = find_confirmed_client_by_name(&unconf->cl_name, nn);
		if (old) {
			status = mark_client_expired_locked(old);
			if (status) {
				old = NULL;
				goto out_free_conn;
			}
		}
		move_to_confirmed(unconf);
		conf = unconf;
	} else {
		status = nfserr_stale_clientid;
		goto out_free_conn;
	}
	status = nfs_ok;
	/*
	 * We do not support RDMA or persistent sessions
	 */
	cr_ses->flags &= ~SESSION4_PERSIST;
	cr_ses->flags &= ~SESSION4_RDMA;

	init_session(rqstp, new, conf, cr_ses);
	nfsd4_get_session_locked(new);

	memcpy(cr_ses->sessionid.data, new->se_sessionid.data,
	       NFS4_MAX_SESSIONID_LEN);
	cs_slot->sl_seqid++;
	cr_ses->seqid = cs_slot->sl_seqid;

	/* cache solo and embedded create sessions under the client_lock */
	nfsd4_cache_create_session(cr_ses, cs_slot, status);
	spin_unlock(&nn->client_lock);
	/* init connection and backchannel */
	nfsd4_init_conn(rqstp, conn, new);
	nfsd4_put_session(new);
	if (old)
		expire_client(old);
	return status;
out_free_conn:
	spin_unlock(&nn->client_lock);
	free_conn(conn);
	if (old)
		expire_client(old);
out_free_session:
	__free_session(new);
out_release_drc_mem:
	nfsd4_put_drc_mem(&cr_ses->fore_channel);
	return status;
}

static __be32 nfsd4_map_bcts_dir(u32 *dir)
{
	switch (*dir) {
	case NFS4_CDFC4_FORE:
	case NFS4_CDFC4_BACK:
		return nfs_ok;
	case NFS4_CDFC4_FORE_OR_BOTH:
	case NFS4_CDFC4_BACK_OR_BOTH:
		*dir = NFS4_CDFC4_BOTH;
		return nfs_ok;
	};
	return nfserr_inval;
}

__be32 nfsd4_backchannel_ctl(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate, struct nfsd4_backchannel_ctl *bc)
{
	struct nfsd4_session *session = cstate->session;
	struct nfsd_net *nn = net_generic(SVC_NET(rqstp), nfsd_net_id);
	__be32 status;

	status = nfsd4_check_cb_sec(&bc->bc_cb_sec);
	if (status)
		return status;
	spin_lock(&nn->client_lock);
	session->se_cb_prog = bc->bc_cb_program;
	session->se_cb_sec = bc->bc_cb_sec;
	spin_unlock(&nn->client_lock);

	nfsd4_probe_callback(session->se_client);

	return nfs_ok;
}

__be32 nfsd4_bind_conn_to_session(struct svc_rqst *rqstp,
		     struct nfsd4_compound_state *cstate,
		     struct nfsd4_bind_conn_to_session *bcts)
{
	__be32 status;
	struct nfsd4_conn *conn;
	struct nfsd4_session *session;
	struct net *net = SVC_NET(rqstp);
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);

	if (!nfsd4_last_compound_op(rqstp))
		return nfserr_not_only_op;
	spin_lock(&nn->client_lock);
	session = find_in_sessionid_hashtbl(&bcts->sessionid, net, &status);
	spin_unlock(&nn->client_lock);
	if (!session)
		goto out_no_session;
	status = nfserr_wrong_cred;
	if (!mach_creds_match(session->se_client, rqstp))
		goto out;
	status = nfsd4_map_bcts_dir(&bcts->dir);
	if (status)
		goto out;
	conn = alloc_conn(rqstp, bcts->dir);
	status = nfserr_jukebox;
	if (!conn)
		goto out;
	nfsd4_init_conn(rqstp, conn, session);
	status = nfs_ok;
out:
	nfsd4_put_session(session);
out_no_session:
	return status;
}

static bool nfsd4_compound_in_session(struct nfsd4_session *session, struct nfs4_sessionid *sid)
{
	if (!session)
		return 0;
	return !memcmp(sid, &session->se_sessionid, sizeof(*sid));
}

__be32
nfsd4_destroy_session(struct svc_rqst *r,
		      struct nfsd4_compound_state *cstate,
		      struct nfsd4_destroy_session *sessionid)
{
	struct nfsd4_session *ses;
	__be32 status;
	int ref_held_by_me = 0;
	struct net *net = SVC_NET(r);
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);

	status = nfserr_not_only_op;
	if (nfsd4_compound_in_session(cstate->session, &sessionid->sessionid)) {
		if (!nfsd4_last_compound_op(r))
			goto out;
		ref_held_by_me++;
	}
	dump_sessionid(__func__, &sessionid->sessionid);
	spin_lock(&nn->client_lock);
	ses = find_in_sessionid_hashtbl(&sessionid->sessionid, net, &status);
	if (!ses)
		goto out_client_lock;
	status = nfserr_wrong_cred;
	if (!mach_creds_match(ses->se_client, r))
		goto out_put_session;
	status = mark_session_dead_locked(ses, 1 + ref_held_by_me);
	if (status)
		goto out_put_session;
	unhash_session(ses);
	spin_unlock(&nn->client_lock);

	nfsd4_probe_callback_sync(ses->se_client);

	spin_lock(&nn->client_lock);
	status = nfs_ok;
out_put_session:
	nfsd4_put_session_locked(ses);
out_client_lock:
	spin_unlock(&nn->client_lock);
out:
	return status;
}

static struct nfsd4_conn *__nfsd4_find_conn(struct svc_xprt *xpt, struct nfsd4_session *s)
{
	struct nfsd4_conn *c;

	list_for_each_entry(c, &s->se_conns, cn_persession) {
		if (c->cn_xprt == xpt) {
			return c;
		}
	}
	return NULL;
}

static __be32 nfsd4_sequence_check_conn(struct nfsd4_conn *new, struct nfsd4_session *ses)
{
	struct nfs4_client *clp = ses->se_client;
	struct nfsd4_conn *c;
	__be32 status = nfs_ok;
	int ret;

	spin_lock(&clp->cl_lock);
	c = __nfsd4_find_conn(new->cn_xprt, ses);
	if (c)
		goto out_free;
	status = nfserr_conn_not_bound_to_session;
	if (clp->cl_mach_cred)
		goto out_free;
	__nfsd4_hash_conn(new, ses);
	spin_unlock(&clp->cl_lock);
	ret = nfsd4_register_conn(new);
	if (ret)
		/* oops; xprt is already down: */
		nfsd4_conn_lost(&new->cn_xpt_user);
	return nfs_ok;
out_free:
	spin_unlock(&clp->cl_lock);
	free_conn(new);
	return status;
}

static bool nfsd4_session_too_many_ops(struct svc_rqst *rqstp, struct nfsd4_session *session)
{
	struct nfsd4_compoundargs *args = rqstp->rq_argp;

	return args->opcnt > session->se_fchannel.maxops;
}

static bool nfsd4_request_too_big(struct svc_rqst *rqstp,
				  struct nfsd4_session *session)
{
	struct xdr_buf *xb = &rqstp->rq_arg;

	return xb->len > session->se_fchannel.maxreq_sz;
}

__be32
nfsd4_sequence(struct svc_rqst *rqstp,
	       struct nfsd4_compound_state *cstate,
	       struct nfsd4_sequence *seq)
{
	struct nfsd4_compoundres *resp = rqstp->rq_resp;
	struct xdr_stream *xdr = &resp->xdr;
	struct nfsd4_session *session;
	struct nfs4_client *clp;
	struct nfsd4_slot *slot;
	struct nfsd4_conn *conn;
	__be32 status;
	int buflen;
	struct net *net = SVC_NET(rqstp);
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);

	if (resp->opcnt != 1)
		return nfserr_sequence_pos;

	/*
	 * Will be either used or freed by nfsd4_sequence_check_conn
	 * below.
	 */
	conn = alloc_conn(rqstp, NFS4_CDFC4_FORE);
	if (!conn)
		return nfserr_jukebox;

	spin_lock(&nn->client_lock);
	session = find_in_sessionid_hashtbl(&seq->sessionid, net, &status);
	if (!session)
		goto out_no_session;
	clp = session->se_client;

	status = nfserr_too_many_ops;
	if (nfsd4_session_too_many_ops(rqstp, session))
		goto out_put_session;

	status = nfserr_req_too_big;
	if (nfsd4_request_too_big(rqstp, session))
		goto out_put_session;

	status = nfserr_badslot;
	if (seq->slotid >= session->se_fchannel.maxreqs)
		goto out_put_session;

	slot = session->se_slots[seq->slotid];
	dprintk("%s: slotid %d\n", __func__, seq->slotid);

	/* We do not negotiate the number of slots yet, so set the
	 * maxslots to the session maxreqs which is used to encode
	 * sr_highest_slotid and the sr_target_slot id to maxslots */
	seq->maxslots = session->se_fchannel.maxreqs;

	status = check_slot_seqid(seq->seqid, slot->sl_seqid,
					slot->sl_flags & NFSD4_SLOT_INUSE);
	if (status == nfserr_replay_cache) {
		status = nfserr_seq_misordered;
		if (!(slot->sl_flags & NFSD4_SLOT_INITIALIZED))
			goto out_put_session;
		cstate->slot = slot;
		cstate->session = session;
		cstate->clp = clp;
		/* Return the cached reply status and set cstate->status
		 * for nfsd4_proc_compound processing */
		status = nfsd4_replay_cache_entry(resp, seq);
		cstate->status = nfserr_replay_cache;
		goto out;
	}
	if (status)
		goto out_put_session;

	status = nfsd4_sequence_check_conn(conn, session);
	conn = NULL;
	if (status)
		goto out_put_session;

	buflen = (seq->cachethis) ?
			session->se_fchannel.maxresp_cached :
			session->se_fchannel.maxresp_sz;
	status = (seq->cachethis) ? nfserr_rep_too_big_to_cache :
				    nfserr_rep_too_big;
	if (xdr_restrict_buflen(xdr, buflen - rqstp->rq_auth_slack))
		goto out_put_session;
	svc_reserve(rqstp, buflen);

	status = nfs_ok;
	/* Success! bump slot seqid */
	slot->sl_seqid = seq->seqid;
	slot->sl_flags |= NFSD4_SLOT_INUSE;
	if (seq->cachethis)
		slot->sl_flags |= NFSD4_SLOT_CACHETHIS;
	else
		slot->sl_flags &= ~NFSD4_SLOT_CACHETHIS;

	cstate->slot = slot;
	cstate->session = session;
	cstate->clp = clp;

out:
	switch (clp->cl_cb_state) {
	case NFSD4_CB_DOWN:
		seq->status_flags = SEQ4_STATUS_CB_PATH_DOWN;
		break;
	case NFSD4_CB_FAULT:
		seq->status_flags = SEQ4_STATUS_BACKCHANNEL_FAULT;
		break;
	default:
		seq->status_flags = 0;
	}
	if (!list_empty(&clp->cl_revoked))
		seq->status_flags |= SEQ4_STATUS_RECALLABLE_STATE_REVOKED;
out_no_session:
	if (conn)
		free_conn(conn);
	spin_unlock(&nn->client_lock);
	return status;
out_put_session:
	nfsd4_put_session_locked(session);
	goto out_no_session;
}

void
nfsd4_sequence_done(struct nfsd4_compoundres *resp)
{
	struct nfsd4_compound_state *cs = &resp->cstate;

	if (nfsd4_has_session(cs)) {
		if (cs->status != nfserr_replay_cache) {
			nfsd4_store_cache_entry(resp);
			cs->slot->sl_flags &= ~NFSD4_SLOT_INUSE;
		}
		/* Drop session reference that was taken in nfsd4_sequence() */
		nfsd4_put_session(cs->session);
	} else if (cs->clp)
		put_client_renew(cs->clp);
}

__be32
nfsd4_destroy_clientid(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate, struct nfsd4_destroy_clientid *dc)
{
	struct nfs4_client *conf, *unconf;
	struct nfs4_client *clp = NULL;
	__be32 status = 0;
	struct nfsd_net *nn = net_generic(SVC_NET(rqstp), nfsd_net_id);

	spin_lock(&nn->client_lock);
	unconf = find_unconfirmed_client(&dc->clientid, true, nn);
	conf = find_confirmed_client(&dc->clientid, true, nn);
	WARN_ON_ONCE(conf && unconf);

	if (conf) {
		if (client_has_state(conf)) {
			status = nfserr_clientid_busy;
			goto out;
		}
		status = mark_client_expired_locked(conf);
		if (status)
			goto out;
		clp = conf;
	} else if (unconf)
		clp = unconf;
	else {
		status = nfserr_stale_clientid;
		goto out;
	}
	if (!mach_creds_match(clp, rqstp)) {
		clp = NULL;
		status = nfserr_wrong_cred;
		goto out;
	}
	unhash_client_locked(clp);
out:
	spin_unlock(&nn->client_lock);
	if (clp)
		expire_client(clp);
	return status;
}

__be32
nfsd4_reclaim_complete(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate, struct nfsd4_reclaim_complete *rc)
{
	__be32 status = 0;

	if (rc->rca_one_fs) {
		if (!cstate->current_fh.fh_dentry)
			return nfserr_nofilehandle;
		/*
		 * We don't take advantage of the rca_one_fs case.
		 * That's OK, it's optional, we can safely ignore it.
		 */
		 return nfs_ok;
	}

	status = nfserr_complete_already;
	if (test_and_set_bit(NFSD4_CLIENT_RECLAIM_COMPLETE,
			     &cstate->session->se_client->cl_flags))
		goto out;

	status = nfserr_stale_clientid;
	if (is_client_expired(cstate->session->se_client))
		/*
		 * The following error isn't really legal.
		 * But we only get here if the client just explicitly
		 * destroyed the client.  Surely it no longer cares what
		 * error it gets back on an operation for the dead
		 * client.
		 */
		goto out;

	status = nfs_ok;
	nfsd4_client_record_create(cstate->session->se_client);
out:
	return status;
}

__be32
nfsd4_setclientid(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
		  struct nfsd4_setclientid *setclid)
{
	struct xdr_netobj 	clname = setclid->se_name;
	nfs4_verifier		clverifier = setclid->se_verf;
	struct nfs4_client	*conf, *new;
	struct nfs4_client	*unconf = NULL;
	__be32 			status;
	struct nfsd_net		*nn = net_generic(SVC_NET(rqstp), nfsd_net_id);

	new = create_client(clname, rqstp, &clverifier);
	if (new == NULL)
		return nfserr_jukebox;
	/* Cases below refer to rfc 3530 section 14.2.33: */
	spin_lock(&nn->client_lock);
	conf = find_confirmed_client_by_name(&clname, nn);
	if (conf && client_has_state(conf)) {
		/* case 0: */
		status = nfserr_clid_inuse;
		if (clp_used_exchangeid(conf))
			goto out;
		if (!same_creds(&conf->cl_cred, &rqstp->rq_cred)) {
			char addr_str[INET6_ADDRSTRLEN];
			rpc_ntop((struct sockaddr *) &conf->cl_addr, addr_str,
				 sizeof(addr_str));
			dprintk("NFSD: setclientid: string in use by client "
				"at %s\n", addr_str);
			goto out;
		}
	}
	unconf = find_unconfirmed_client_by_name(&clname, nn);
	if (unconf)
		unhash_client_locked(unconf);
	if (conf && same_verf(&conf->cl_verifier, &clverifier)) {
		/* case 1: probable callback update */
		copy_clid(new, conf);
		gen_confirm(new, nn);
	} else /* case 4 (new client) or cases 2, 3 (client reboot): */
		gen_clid(new, nn);
	new->cl_minorversion = 0;
	gen_callback(new, setclid, rqstp);
	add_to_unconfirmed(new);
	setclid->se_clientid.cl_boot = new->cl_clientid.cl_boot;
	setclid->se_clientid.cl_id = new->cl_clientid.cl_id;
	memcpy(setclid->se_confirm.data, new->cl_confirm.data, sizeof(setclid->se_confirm.data));
	new = NULL;
	status = nfs_ok;
out:
	spin_unlock(&nn->client_lock);
	if (new)
		free_client(new);
	if (unconf)
		expire_client(unconf);
	return status;
}


__be32
nfsd4_setclientid_confirm(struct svc_rqst *rqstp,
			 struct nfsd4_compound_state *cstate,
			 struct nfsd4_setclientid_confirm *setclientid_confirm)
{
	struct nfs4_client *conf, *unconf;
	struct nfs4_client *old = NULL;
	nfs4_verifier confirm = setclientid_confirm->sc_confirm; 
	clientid_t * clid = &setclientid_confirm->sc_clientid;
	__be32 status;
	struct nfsd_net	*nn = net_generic(SVC_NET(rqstp), nfsd_net_id);

	if (STALE_CLIENTID(clid, nn))
		return nfserr_stale_clientid;

	spin_lock(&nn->client_lock);
	conf = find_confirmed_client(clid, false, nn);
	unconf = find_unconfirmed_client(clid, false, nn);
	/*
	 * We try hard to give out unique clientid's, so if we get an
	 * attempt to confirm the same clientid with a different cred,
	 * the client may be buggy; this should never happen.
	 *
	 * Nevertheless, RFC 7530 recommends INUSE for this case:
	 */
	status = nfserr_clid_inuse;
	if (unconf && !same_creds(&unconf->cl_cred, &rqstp->rq_cred))
		goto out;
	if (conf && !same_creds(&conf->cl_cred, &rqstp->rq_cred))
		goto out;
	/* cases below refer to rfc 3530 section 14.2.34: */
	if (!unconf || !same_verf(&confirm, &unconf->cl_confirm)) {
		if (conf && !unconf) /* case 2: probable retransmit */
			status = nfs_ok;
		else /* case 4: client hasn't noticed we rebooted yet? */
			status = nfserr_stale_clientid;
		goto out;
	}
	status = nfs_ok;
	if (conf) { /* case 1: callback update */
		old = unconf;
		unhash_client_locked(old);
		nfsd4_change_callback(conf, &unconf->cl_cb_conn);
	} else { /* case 3: normal case; new or rebooted client */
		old = find_confirmed_client_by_name(&unconf->cl_name, nn);
		if (old) {
			status = nfserr_clid_inuse;
			if (client_has_state(old)
					&& !same_creds(&unconf->cl_cred,
							&old->cl_cred)) {
				old = NULL;
				goto out;
			}
			status = mark_client_expired_locked(old);
			if (status) {
				old = NULL;
				goto out;
			}
		}
		move_to_confirmed(unconf);
		conf = unconf;
	}
	get_client_locked(conf);
	spin_unlock(&nn->client_lock);
	nfsd4_probe_callback(conf);
	spin_lock(&nn->client_lock);
	put_client_renew_locked(conf);
out:
	spin_unlock(&nn->client_lock);
	if (old)
		expire_client(old);
	return status;
}

static struct nfs4_file *nfsd4_alloc_file(void)
{
	return kmem_cache_alloc(file_slab, GFP_KERNEL);
}

/* OPEN Share state helper functions */
static void nfsd4_init_file(struct knfsd_fh *fh, unsigned int hashval,
				struct nfs4_file *fp)
{
	lockdep_assert_held(&state_lock);

	atomic_set(&fp->fi_ref, 1);
	spin_lock_init(&fp->fi_lock);
	INIT_LIST_HEAD(&fp->fi_stateids);
	INIT_LIST_HEAD(&fp->fi_delegations);
	INIT_LIST_HEAD(&fp->fi_clnt_odstate);
	fh_copy_shallow(&fp->fi_fhandle, fh);
	fp->fi_deleg_file = NULL;
	fp->fi_had_conflict = false;
	fp->fi_share_deny = 0;
	memset(fp->fi_fds, 0, sizeof(fp->fi_fds));
	memset(fp->fi_access, 0, sizeof(fp->fi_access));
#ifdef CONFIG_NFSD_PNFS
	INIT_LIST_HEAD(&fp->fi_lo_states);
	atomic_set(&fp->fi_lo_recalls, 0);
#endif
	hlist_add_head_rcu(&fp->fi_hash, &file_hashtbl[hashval]);
}

void
nfsd4_free_slabs(void)
{
	kmem_cache_destroy(odstate_slab);
	kmem_cache_destroy(openowner_slab);
	kmem_cache_destroy(lockowner_slab);
	kmem_cache_destroy(file_slab);
	kmem_cache_destroy(stateid_slab);
	kmem_cache_destroy(deleg_slab);
}

int
nfsd4_init_slabs(void)
{
	openowner_slab = kmem_cache_create("nfsd4_openowners",
			sizeof(struct nfs4_openowner), 0, 0, NULL);
	if (openowner_slab == NULL)
		goto out;
	lockowner_slab = kmem_cache_create("nfsd4_lockowners",
			sizeof(struct nfs4_lockowner), 0, 0, NULL);
	if (lockowner_slab == NULL)
		goto out_free_openowner_slab;
	file_slab = kmem_cache_create("nfsd4_files",
			sizeof(struct nfs4_file), 0, 0, NULL);
	if (file_slab == NULL)
		goto out_free_lockowner_slab;
	stateid_slab = kmem_cache_create("nfsd4_stateids",
			sizeof(struct nfs4_ol_stateid), 0, 0, NULL);
	if (stateid_slab == NULL)
		goto out_free_file_slab;
	deleg_slab = kmem_cache_create("nfsd4_delegations",
			sizeof(struct nfs4_delegation), 0, 0, NULL);
	if (deleg_slab == NULL)
		goto out_free_stateid_slab;
	odstate_slab = kmem_cache_create("nfsd4_odstate",
			sizeof(struct nfs4_clnt_odstate), 0, 0, NULL);
	if (odstate_slab == NULL)
		goto out_free_deleg_slab;
	return 0;

out_free_deleg_slab:
	kmem_cache_destroy(deleg_slab);
out_free_stateid_slab:
	kmem_cache_destroy(stateid_slab);
out_free_file_slab:
	kmem_cache_destroy(file_slab);
out_free_lockowner_slab:
	kmem_cache_destroy(lockowner_slab);
out_free_openowner_slab:
	kmem_cache_destroy(openowner_slab);
out:
	dprintk("nfsd4: out of memory while initializing nfsv4\n");
	return -ENOMEM;
}

static void init_nfs4_replay(struct nfs4_replay *rp)
{
	rp->rp_status = nfserr_serverfault;
	rp->rp_buflen = 0;
	rp->rp_buf = rp->rp_ibuf;
	mutex_init(&rp->rp_mutex);
}

static void nfsd4_cstate_assign_replay(struct nfsd4_compound_state *cstate,
		struct nfs4_stateowner *so)
{
	if (!nfsd4_has_session(cstate)) {
		mutex_lock(&so->so_replay.rp_mutex);
		cstate->replay_owner = nfs4_get_stateowner(so);
	}
}

void nfsd4_cstate_clear_replay(struct nfsd4_compound_state *cstate)
{
	struct nfs4_stateowner *so = cstate->replay_owner;

	if (so != NULL) {
		cstate->replay_owner = NULL;
		mutex_unlock(&so->so_replay.rp_mutex);
		nfs4_put_stateowner(so);
	}
}

static inline void *alloc_stateowner(struct kmem_cache *slab, struct xdr_netobj *owner, struct nfs4_client *clp)
{
	struct nfs4_stateowner *sop;

	sop = kmem_cache_alloc(slab, GFP_KERNEL);
	if (!sop)
		return NULL;

	sop->so_owner.data = kmemdup(owner->data, owner->len, GFP_KERNEL);
	if (!sop->so_owner.data) {
		kmem_cache_free(slab, sop);
		return NULL;
	}
	sop->so_owner.len = owner->len;

	INIT_LIST_HEAD(&sop->so_stateids);
	sop->so_client = clp;
	init_nfs4_replay(&sop->so_replay);
	atomic_set(&sop->so_count, 1);
	return sop;
}

static void hash_openowner(struct nfs4_openowner *oo, struct nfs4_client *clp, unsigned int strhashval)
{
	lockdep_assert_held(&clp->cl_lock);

	list_add(&oo->oo_owner.so_strhash,
		 &clp->cl_ownerstr_hashtbl[strhashval]);
	list_add(&oo->oo_perclient, &clp->cl_openowners);
}

static void nfs4_unhash_openowner(struct nfs4_stateowner *so)
{
	unhash_openowner_locked(openowner(so));
}

static void nfs4_free_openowner(struct nfs4_stateowner *so)
{
	struct nfs4_openowner *oo = openowner(so);

	kmem_cache_free(openowner_slab, oo);
}

static const struct nfs4_stateowner_operations openowner_ops = {
	.so_unhash =	nfs4_unhash_openowner,
	.so_free =	nfs4_free_openowner,
};

static struct nfs4_ol_stateid *
nfsd4_find_existing_open(struct nfs4_file *fp, struct nfsd4_open *open)
{
	struct nfs4_ol_stateid *local, *ret = NULL;
	struct nfs4_openowner *oo = open->op_openowner;

	lockdep_assert_held(&fp->fi_lock);

	list_for_each_entry(local, &fp->fi_stateids, st_perfile) {
		/* ignore lock owners */
		if (local->st_stateowner->so_is_open_owner == 0)
			continue;
		if (local->st_stateowner != &oo->oo_owner)
			continue;
		if (local->st_stid.sc_type == NFS4_OPEN_STID) {
			ret = local;
			atomic_inc(&ret->st_stid.sc_count);
			break;
		}
	}
	return ret;
}

static __be32
nfsd4_verify_open_stid(struct nfs4_stid *s)
{
	__be32 ret = nfs_ok;

	switch (s->sc_type) {
	default:
		break;
	case NFS4_CLOSED_STID:
	case NFS4_CLOSED_DELEG_STID:
		ret = nfserr_bad_stateid;
		break;
	case NFS4_REVOKED_DELEG_STID:
		ret = nfserr_deleg_revoked;
	}
	return ret;
}

/* Lock the stateid st_mutex, and deal with races with CLOSE */
static __be32
nfsd4_lock_ol_stateid(struct nfs4_ol_stateid *stp)
{
	__be32 ret;

	mutex_lock(&stp->st_mutex);
	ret = nfsd4_verify_open_stid(&stp->st_stid);
	if (ret != nfs_ok)
		mutex_unlock(&stp->st_mutex);
	return ret;
}

static struct nfs4_ol_stateid *
nfsd4_find_and_lock_existing_open(struct nfs4_file *fp, struct nfsd4_open *open)
{
	struct nfs4_ol_stateid *stp;
	for (;;) {
		spin_lock(&fp->fi_lock);
		stp = nfsd4_find_existing_open(fp, open);
		spin_unlock(&fp->fi_lock);
		if (!stp || nfsd4_lock_ol_stateid(stp) == nfs_ok)
			break;
		nfs4_put_stid(&stp->st_stid);
	}
	return stp;
}

static struct nfs4_openowner *
alloc_init_open_stateowner(unsigned int strhashval, struct nfsd4_open *open,
			   struct nfsd4_compound_state *cstate)
{
	struct nfs4_client *clp = cstate->clp;
	struct nfs4_openowner *oo, *ret;

	oo = alloc_stateowner(openowner_slab, &open->op_owner, clp);
	if (!oo)
		return NULL;
	oo->oo_owner.so_ops = &openowner_ops;
	oo->oo_owner.so_is_open_owner = 1;
	oo->oo_owner.so_seqid = open->op_seqid;
	oo->oo_flags = 0;
	if (nfsd4_has_session(cstate))
		oo->oo_flags |= NFS4_OO_CONFIRMED;
	oo->oo_time = 0;
	oo->oo_last_closed_stid = NULL;
	INIT_LIST_HEAD(&oo->oo_close_lru);
	spin_lock(&clp->cl_lock);
	ret = find_openstateowner_str_locked(strhashval, open, clp);
	if (ret == NULL) {
		hash_openowner(oo, clp, strhashval);
		ret = oo;
	} else
		nfs4_free_stateowner(&oo->oo_owner);

	spin_unlock(&clp->cl_lock);
	return ret;
}

static struct nfs4_ol_stateid *
init_open_stateid(struct nfs4_file *fp, struct nfsd4_open *open)
{

	struct nfs4_openowner *oo = open->op_openowner;
	struct nfs4_ol_stateid *retstp = NULL;
	struct nfs4_ol_stateid *stp;

	stp = open->op_stp;
	/* We are moving these outside of the spinlocks to avoid the warnings */
	mutex_init(&stp->st_mutex);
	mutex_lock(&stp->st_mutex);

retry:
	spin_lock(&oo->oo_owner.so_client->cl_lock);
	spin_lock(&fp->fi_lock);

	if (nfs4_openowner_unhashed(oo)) {
		mutex_unlock(&stp->st_mutex);
		stp = NULL;
		goto out_unlock;
	}

	retstp = nfsd4_find_existing_open(fp, open);
	if (retstp)
		goto out_unlock;

	open->op_stp = NULL;
	atomic_inc(&stp->st_stid.sc_count);
	stp->st_stid.sc_type = NFS4_OPEN_STID;
	INIT_LIST_HEAD(&stp->st_locks);
	stp->st_stateowner = nfs4_get_stateowner(&oo->oo_owner);
	get_nfs4_file(fp);
	stp->st_stid.sc_file = fp;
	stp->st_access_bmap = 0;
	stp->st_deny_bmap = 0;
	stp->st_openstp = NULL;
	list_add(&stp->st_perstateowner, &oo->oo_owner.so_stateids);
	list_add(&stp->st_perfile, &fp->fi_stateids);

out_unlock:
	spin_unlock(&fp->fi_lock);
	spin_unlock(&oo->oo_owner.so_client->cl_lock);
	if (retstp) {
		/* Handle races with CLOSE */
		if (nfsd4_lock_ol_stateid(retstp) != nfs_ok) {
			nfs4_put_stid(&retstp->st_stid);
			goto retry;
		}
		/* To keep mutex tracking happy */
		mutex_unlock(&stp->st_mutex);
		stp = retstp;
	}
	return stp;
}

/*
 * In the 4.0 case we need to keep the owners around a little while to handle
 * CLOSE replay. We still do need to release any file access that is held by
 * them before returning however.
 */
static void
move_to_close_lru(struct nfs4_ol_stateid *s, struct net *net)
{
	struct nfs4_ol_stateid *last;
	struct nfs4_openowner *oo = openowner(s->st_stateowner);
	struct nfsd_net *nn = net_generic(s->st_stid.sc_client->net,
						nfsd_net_id);

	dprintk("NFSD: move_to_close_lru nfs4_openowner %p\n", oo);

	/*
	 * We know that we hold one reference via nfsd4_close, and another
	 * "persistent" reference for the client. If the refcount is higher
	 * than 2, then there are still calls in progress that are using this
	 * stateid. We can't put the sc_file reference until they are finished.
	 * Wait for the refcount to drop to 2. Since it has been unhashed,
	 * there should be no danger of the refcount going back up again at
	 * this point.
	 */
	wait_event(close_wq, atomic_read(&s->st_stid.sc_count) == 2);

	release_all_access(s);
	if (s->st_stid.sc_file) {
		put_nfs4_file(s->st_stid.sc_file);
		s->st_stid.sc_file = NULL;
	}

	spin_lock(&nn->client_lock);
	last = oo->oo_last_closed_stid;
	oo->oo_last_closed_stid = s;
	list_move_tail(&oo->oo_close_lru, &nn->close_lru);
	oo->oo_time = get_seconds();
	spin_unlock(&nn->client_lock);
	if (last)
		nfs4_put_stid(&last->st_stid);
}

/* search file_hashtbl[] for file */
static struct nfs4_file *
find_file_locked(struct knfsd_fh *fh, unsigned int hashval)
{
	struct nfs4_file *fp;

	hlist_for_each_entry_rcu(fp, &file_hashtbl[hashval], fi_hash) {
		if (fh_match(&fp->fi_fhandle, fh)) {
			if (atomic_inc_not_zero(&fp->fi_ref))
				return fp;
		}
	}
	return NULL;
}

struct nfs4_file *
find_file(struct knfsd_fh *fh)
{
	struct nfs4_file *fp;
	unsigned int hashval = file_hashval(fh);

	rcu_read_lock();
	fp = find_file_locked(fh, hashval);
	rcu_read_unlock();
	return fp;
}

static struct nfs4_file *
find_or_add_file(struct nfs4_file *new, struct knfsd_fh *fh)
{
	struct nfs4_file *fp;
	unsigned int hashval = file_hashval(fh);

	rcu_read_lock();
	fp = find_file_locked(fh, hashval);
	rcu_read_unlock();
	if (fp)
		return fp;

	spin_lock(&state_lock);
	fp = find_file_locked(fh, hashval);
	if (likely(fp == NULL)) {
		nfsd4_init_file(fh, hashval, new);
		fp = new;
	}
	spin_unlock(&state_lock);

	return fp;
}

/*
 * Called to check deny when READ with all zero stateid or
 * WRITE with all zero or all one stateid
 */
static __be32
nfs4_share_conflict(struct svc_fh *current_fh, unsigned int deny_type)
{
	struct inode *ino = current_fh->fh_dentry->d_inode;
	struct nfs4_file *fp;
	struct nfs4_stateid *stp;
	__be32 ret;

	dprintk("NFSD: nfs4_share_conflict\n");

	fp = find_file(ino);
	if (!fp)
		return nfs_ok;
	ret = nfserr_locked;
	/* Search for conflicting share reservations */
	list_for_each_entry(stp, &fp->fi_stateids, st_perfile) {
		if (test_bit(deny_type, &stp->st_deny_bmap) ||
		    test_bit(NFS4_SHARE_DENY_BOTH, &stp->st_deny_bmap))
			goto out;
	}
	ret = nfs_ok;
out:
	struct nfs4_file *fp;
	__be32 ret = nfs_ok;

	fp = find_file(&current_fh->fh_handle);
	if (!fp)
		return ret;
	/* Check for conflicting share reservations */
	spin_lock(&fp->fi_lock);
	if (fp->fi_share_deny & deny_type)
		ret = nfserr_locked;
	spin_unlock(&fp->fi_lock);
	put_nfs4_file(fp);
	return ret;
}

static inline void
nfs4_file_downgrade(struct file *filp, unsigned int share_access)
{
	if (share_access & NFS4_SHARE_ACCESS_WRITE) {
		drop_file_write_access(filp);
		filp->f_mode = (filp->f_mode | FMODE_READ) & ~FMODE_WRITE;
	}
}

/*
 * Recall a delegation
 */
static int
do_recall(void *__dp)
{
	struct nfs4_delegation *dp = __dp;

	dp->dl_file->fi_had_conflict = true;
	nfsd4_cb_recall(dp);
	return 0;
}

/*
 * Spawn a thread to perform a recall on the delegation represented
 * by the lease (file_lock)
 *
 * Called from break_lease() with lock_kernel() held.
 * Note: we assume break_lease will only call this *once* for any given
 * lease.
 */
static
void nfsd_break_deleg_cb(struct file_lock *fl)
{
	struct nfs4_delegation *dp=  (struct nfs4_delegation *)fl->fl_owner;
	struct task_struct *t;

	dprintk("NFSD nfsd_break_deleg_cb: dp %p fl %p\n",dp,fl);
	if (!dp)
		return;

	/* We're assuming the state code never drops its reference
	 * without first removing the lease.  Since we're in this lease
	 * callback (and since the lease code is serialized by the kernel
	 * lock) we know the server hasn't removed the lease yet, we know
	 * it's safe to take a reference: */
	atomic_inc(&dp->dl_count);
	atomic_inc(&dp->dl_client->cl_count);

	spin_lock(&recall_lock);
	list_add_tail(&dp->dl_recall_lru, &del_recall_lru);
	spin_unlock(&recall_lock);

	/* only place dl_time is set. protected by lock_kernel*/
	dp->dl_time = get_seconds();

	/*
	 * We don't want the locks code to timeout the lease for us;
	 * we'll remove it ourself if the delegation isn't returned
	 * in time.
	 */
	fl->fl_break_time = 0;

	t = kthread_run(do_recall, dp, "%s", "nfs4_cb_recall");
	if (IS_ERR(t)) {
		struct nfs4_client *clp = dp->dl_client;

		printk(KERN_INFO "NFSD: Callback thread failed for "
			"for client (clientid %08x/%08x)\n",
			clp->cl_clientid.cl_boot, clp->cl_clientid.cl_id);
		put_nfs4_client(dp->dl_client);
		nfs4_put_delegation(dp);
	}
}

/*
 * The file_lock is being reapd.
 *
 * Called by locks_free_lock() with lock_kernel() held.
 */
static
void nfsd_release_deleg_cb(struct file_lock *fl)
{
	struct nfs4_delegation *dp = (struct nfs4_delegation *)fl->fl_owner;

	dprintk("NFSD nfsd_release_deleg_cb: fl %p dp %p dl_count %d\n", fl,dp, atomic_read(&dp->dl_count));

	if (!(fl->fl_flags & FL_LEASE) || !dp)
		return;
	dp->dl_flock = NULL;
}

/*
 * Set the delegation file_lock back pointer.
 *
 * Called from setlease() with lock_kernel() held.
 */
static
void nfsd_copy_lock_deleg_cb(struct file_lock *new, struct file_lock *fl)
{
	struct nfs4_delegation *dp = (struct nfs4_delegation *)new->fl_owner;

	dprintk("NFSD: nfsd_copy_lock_deleg_cb: new fl %p dp %p\n", new, dp);
	if (!dp)
		return;
	dp->dl_flock = new;
}

/*
 * Called from setlease() with lock_kernel() held
 */
static
int nfsd_same_client_deleg_cb(struct file_lock *onlist, struct file_lock *try)
{
	struct nfs4_delegation *onlistd =
		(struct nfs4_delegation *)onlist->fl_owner;
	struct nfs4_delegation *tryd =
		(struct nfs4_delegation *)try->fl_owner;

	if (onlist->fl_lmops != try->fl_lmops)
		return 0;

	return onlistd->dl_client == tryd->dl_client;
}


static
int nfsd_change_deleg_cb(struct file_lock **onlist, int arg)
{
	if (arg & F_UNLCK)
		return lease_modify(onlist, arg);
static void nfsd4_cb_recall_prepare(struct nfsd4_callback *cb)
{
	struct nfs4_delegation *dp = cb_to_delegation(cb);
	struct nfsd_net *nn = net_generic(dp->dl_stid.sc_client->net,
					  nfsd_net_id);

	block_delegations(&dp->dl_stid.sc_file->fi_fhandle);

	/*
	 * We can't do this in nfsd_break_deleg_cb because it is
	 * already holding inode->i_lock.
	 *
	 * If the dl_time != 0, then we know that it has already been
	 * queued for a lease break. Don't queue it again.
	 */
	spin_lock(&state_lock);
	if (delegation_hashed(dp) && dp->dl_time == 0) {
		dp->dl_time = get_seconds();
		list_add_tail(&dp->dl_recall_lru, &nn->del_recall_lru);
	}
	spin_unlock(&state_lock);
}

static int nfsd4_cb_recall_done(struct nfsd4_callback *cb,
		struct rpc_task *task)
{
	struct nfs4_delegation *dp = cb_to_delegation(cb);

	if (dp->dl_stid.sc_type == NFS4_CLOSED_DELEG_STID)
	        return 1;

	switch (task->tk_status) {
	case 0:
		return 1;
	case -EBADHANDLE:
	case -NFS4ERR_BAD_STATEID:
		/*
		 * Race: client probably got cb_recall before open reply
		 * granting delegation.
		 */
		if (dp->dl_retries--) {
			rpc_delay(task, 2 * HZ);
			return 0;
		}
		/*FALLTHRU*/
	default:
		return -1;
	}
}

static void nfsd4_cb_recall_release(struct nfsd4_callback *cb)
{
	struct nfs4_delegation *dp = cb_to_delegation(cb);

	nfs4_put_stid(&dp->dl_stid);
}

static struct nfsd4_callback_ops nfsd4_cb_recall_ops = {
	.prepare	= nfsd4_cb_recall_prepare,
	.done		= nfsd4_cb_recall_done,
	.release	= nfsd4_cb_recall_release,
};

static void nfsd_break_one_deleg(struct nfs4_delegation *dp)
{
	/*
	 * We're assuming the state code never drops its reference
	 * without first removing the lease.  Since we're in this lease
	 * callback (and since the lease code is serialized by the kernel
	 * lock) we know the server hasn't removed the lease yet, we know
	 * it's safe to take a reference.
	 */
	atomic_inc(&dp->dl_stid.sc_count);
	nfsd4_run_cb(&dp->dl_recall);
}

/* Called from break_lease() with i_lock held. */
static bool
nfsd_break_deleg_cb(struct file_lock *fl)
{
	bool ret = false;
	struct nfs4_file *fp = (struct nfs4_file *)fl->fl_owner;
	struct nfs4_delegation *dp;

	if (!fp) {
		WARN(1, "(%p)->fl_owner NULL\n", fl);
		return ret;
	}
	if (fp->fi_had_conflict) {
		WARN(1, "duplicate break on %p\n", fp);
		return ret;
	}
	/*
	 * We don't want the locks code to timeout the lease for us;
	 * we'll remove it ourself if a delegation isn't returned
	 * in time:
	 */
	fl->fl_break_time = 0;

	spin_lock(&fp->fi_lock);
	fp->fi_had_conflict = true;
	/*
	 * If there are no delegations on the list, then return true
	 * so that the lease code will go ahead and delete it.
	 */
	if (list_empty(&fp->fi_delegations))
		ret = true;
	else
		list_for_each_entry(dp, &fp->fi_delegations, dl_perfile)
			nfsd_break_one_deleg(dp);
	spin_unlock(&fp->fi_lock);
	return ret;
}

static int
nfsd_change_deleg_cb(struct file_lock *onlist, int arg,
		     struct list_head *dispose)
{
	if (arg & F_UNLCK)
		return lease_modify(onlist, arg, dispose);
	else
		return -EAGAIN;
}

static struct lock_manager_operations nfsd_lease_mng_ops = {
	.fl_break = nfsd_break_deleg_cb,
	.fl_release_private = nfsd_release_deleg_cb,
	.fl_copy_lock = nfsd_copy_lock_deleg_cb,
	.fl_mylease = nfsd_same_client_deleg_cb,
	.fl_change = nfsd_change_deleg_cb,
};


__be32
nfsd4_process_open1(struct nfsd4_open *open)
static const struct lock_manager_operations nfsd_lease_mng_ops = {
	.lm_break = nfsd_break_deleg_cb,
	.lm_change = nfsd_change_deleg_cb,
};

static __be32 nfsd4_check_seqid(struct nfsd4_compound_state *cstate, struct nfs4_stateowner *so, u32 seqid)
{
	if (nfsd4_has_session(cstate))
		return nfs_ok;
	if (seqid == so->so_seqid - 1)
		return nfserr_replay_me;
	if (seqid == so->so_seqid)
		return nfs_ok;
	return nfserr_bad_seqid;
}

static __be32 lookup_clientid(clientid_t *clid,
		struct nfsd4_compound_state *cstate,
		struct nfsd_net *nn)
{
	struct nfs4_client *found;

	if (cstate->clp) {
		found = cstate->clp;
		if (!same_clid(&found->cl_clientid, clid))
			return nfserr_stale_clientid;
		return nfs_ok;
	}

	if (STALE_CLIENTID(clid, nn))
		return nfserr_stale_clientid;

	/*
	 * For v4.1+ we get the client in the SEQUENCE op. If we don't have one
	 * cached already then we know this is for is for v4.0 and "sessions"
	 * will be false.
	 */
	WARN_ON_ONCE(cstate->session);
	spin_lock(&nn->client_lock);
	found = find_confirmed_client(clid, false, nn);
	if (!found) {
		spin_unlock(&nn->client_lock);
		return nfserr_expired;
	}
	atomic_inc(&found->cl_refcount);
	spin_unlock(&nn->client_lock);

	/* Cache the nfs4_client in cstate! */
	cstate->clp = found;
	return nfs_ok;
}

__be32
nfsd4_process_open1(struct nfsd4_compound_state *cstate,
		    struct nfsd4_open *open, struct nfsd_net *nn)
{
	clientid_t *clientid = &open->op_clientid;
	struct nfs4_client *clp = NULL;
	unsigned int strhashval;
	struct nfs4_stateowner *sop = NULL;

	if (!check_name(open->op_owner))
		return nfserr_inval;

	if (STALE_CLIENTID(&open->op_clientid))
		return nfserr_stale_clientid;

	strhashval = ownerstr_hashval(clientid->cl_id, open->op_owner);
	sop = find_openstateowner_str(strhashval, open);
	open->op_stateowner = sop;
	if (!sop) {
		/* Make sure the client's lease hasn't expired. */
		clp = find_confirmed_client(clientid);
		if (clp == NULL)
			return nfserr_expired;
		goto renew;
	}
	if (!sop->so_confirmed) {
		/* Replace unconfirmed owners without checking for replay. */
		clp = sop->so_client;
		release_stateowner(sop);
		open->op_stateowner = NULL;
		goto renew;
	}
	if (open->op_seqid == sop->so_seqid - 1) {
		if (sop->so_replay.rp_buflen)
			return nfserr_replay_me;
		/* The original OPEN failed so spectacularly
		 * that we don't even have replay data saved!
		 * Therefore, we have no choice but to continue
		 * processing this OPEN; presumably, we'll
		 * fail again for the same reason.
		 */
		dprintk("nfsd4_process_open1: replay with no replay cache\n");
		goto renew;
	}
	if (open->op_seqid != sop->so_seqid)
		return nfserr_bad_seqid;
renew:
	if (open->op_stateowner == NULL) {
		sop = alloc_init_open_stateowner(strhashval, clp, open);
		if (sop == NULL)
			return nfserr_resource;
		open->op_stateowner = sop;
	}
	list_del_init(&sop->so_close_lru);
	renew_client(sop->so_client);
	struct nfs4_openowner *oo = NULL;
	__be32 status;

	if (STALE_CLIENTID(&open->op_clientid, nn))
		return nfserr_stale_clientid;
	/*
	 * In case we need it later, after we've already created the
	 * file and don't want to risk a further failure:
	 */
	open->op_file = nfsd4_alloc_file();
	if (open->op_file == NULL)
		return nfserr_jukebox;

	status = lookup_clientid(clientid, cstate, nn);
	if (status)
		return status;
	clp = cstate->clp;

	strhashval = ownerstr_hashval(&open->op_owner);
	oo = find_openstateowner_str(strhashval, open, clp);
	open->op_openowner = oo;
	if (!oo) {
		goto new_owner;
	}
	if (!(oo->oo_flags & NFS4_OO_CONFIRMED)) {
		/* Replace unconfirmed owners without checking for replay. */
		release_openowner(oo);
		open->op_openowner = NULL;
		goto new_owner;
	}
	status = nfsd4_check_seqid(cstate, &oo->oo_owner, open->op_seqid);
	if (status)
		return status;
	goto alloc_stateid;
new_owner:
	oo = alloc_init_open_stateowner(strhashval, open, cstate);
	if (oo == NULL)
		return nfserr_jukebox;
	open->op_openowner = oo;
alloc_stateid:
	open->op_stp = nfs4_alloc_open_stateid(clp);
	if (!open->op_stp)
		return nfserr_jukebox;

	if (nfsd4_has_session(cstate) &&
	    (cstate->current_fh.fh_export->ex_flags & NFSEXP_PNFS)) {
		open->op_odstate = alloc_clnt_odstate(clp);
		if (!open->op_odstate)
			return nfserr_jukebox;
	}

	return nfs_ok;
}

static inline __be32
nfs4_check_delegmode(struct nfs4_delegation *dp, int flags)
{
	if ((flags & WR_STATE) && (dp->dl_type == NFS4_OPEN_DELEGATE_READ))
		return nfserr_openmode;
	else
		return nfs_ok;
}

static struct nfs4_delegation *
find_delegation_file(struct nfs4_file *fp, stateid_t *stid)
{
	struct nfs4_delegation *dp;

	list_for_each_entry(dp, &fp->fi_delegations, dl_perfile) {
		if (dp->dl_stateid.si_stateownerid == stid->si_stateownerid)
			return dp;
	}
	return NULL;
}

static __be32
nfs4_check_deleg(struct nfs4_file *fp, struct nfsd4_open *open,
static int share_access_to_flags(u32 share_access)
{
	return share_access == NFS4_SHARE_ACCESS_READ ? RD_STATE : WR_STATE;
}

static struct nfs4_delegation *find_deleg_stateid(struct nfs4_client *cl, stateid_t *s)
{
	struct nfs4_stid *ret;

	ret = find_stateid_by_type(cl, s,
				NFS4_DELEG_STID|NFS4_REVOKED_DELEG_STID);
	if (!ret)
		return NULL;
	return delegstateid(ret);
}

static bool nfsd4_is_deleg_cur(struct nfsd4_open *open)
{
	return open->op_claim_type == NFS4_OPEN_CLAIM_DELEGATE_CUR ||
	       open->op_claim_type == NFS4_OPEN_CLAIM_DELEG_CUR_FH;
}

static __be32
nfs4_check_deleg(struct nfs4_client *cl, struct nfsd4_open *open,
		struct nfs4_delegation **dp)
{
	int flags;
	__be32 status = nfserr_bad_stateid;

	*dp = find_delegation_file(fp, &open->op_delegate_stateid);
	if (*dp == NULL)
		goto out;
	flags = open->op_share_access == NFS4_SHARE_ACCESS_READ ?
						RD_STATE : WR_STATE;
	status = nfs4_check_delegmode(*dp, flags);
	if (status)
		*dp = NULL;
out:
	if (open->op_claim_type != NFS4_OPEN_CLAIM_DELEGATE_CUR)
		return nfs_ok;
	if (status)
		return status;
	open->op_stateowner->so_confirmed = 1;
	return nfs_ok;
}

static __be32
nfs4_check_open(struct nfs4_file *fp, struct nfsd4_open *open, struct nfs4_stateid **stpp)
{
	struct nfs4_stateid *local;
	__be32 status = nfserr_share_denied;
	struct nfs4_stateowner *sop = open->op_stateowner;

	list_for_each_entry(local, &fp->fi_stateids, st_perfile) {
		/* ignore lock owners */
		if (local->st_stateowner->so_is_open_owner == 0)
			continue;
		/* remember if we have seen this open owner */
		if (local->st_stateowner == sop)
			*stpp = local;
		/* check for conflicting share reservations */
		if (!test_share(local, open))
			goto out;
	}
	status = 0;
out:
	return status;
}

static inline struct nfs4_stateid *
nfs4_alloc_stateid(void)
{
	return kmem_cache_alloc(stateid_slab, GFP_KERNEL);
}

static __be32
nfs4_new_open(struct svc_rqst *rqstp, struct nfs4_stateid **stpp,
		struct nfs4_delegation *dp,
		struct svc_fh *cur_fh, int flags)
{
	struct nfs4_stateid *stp;

	stp = nfs4_alloc_stateid();
	if (stp == NULL)
		return nfserr_resource;

	if (dp) {
		get_file(dp->dl_vfs_file);
		stp->st_vfs_file = dp->dl_vfs_file;
	} else {
		__be32 status;
		status = nfsd_open(rqstp, cur_fh, S_IFREG, flags,
				&stp->st_vfs_file);
		if (status) {
			if (status == nfserr_dropit)
				status = nfserr_jukebox;
			kmem_cache_free(stateid_slab, stp);
			return status;
		}
	}
	*stpp = stp;
	return 0;
	struct nfs4_delegation *deleg;

	deleg = find_deleg_stateid(cl, &open->op_delegate_stateid);
	if (deleg == NULL)
		goto out;
	if (deleg->dl_stid.sc_type == NFS4_REVOKED_DELEG_STID) {
		nfs4_put_stid(&deleg->dl_stid);
		if (cl->cl_minorversion)
			status = nfserr_deleg_revoked;
		goto out;
	}
	flags = share_access_to_flags(open->op_share_access);
	status = nfs4_check_delegmode(deleg, flags);
	if (status) {
		nfs4_put_stid(&deleg->dl_stid);
		goto out;
	}
	*dp = deleg;
out:
	if (!nfsd4_is_deleg_cur(open))
		return nfs_ok;
	if (status)
		return status;
	open->op_openowner->oo_flags |= NFS4_OO_CONFIRMED;
	return nfs_ok;
}

static inline int nfs4_access_to_access(u32 nfs4_access)
{
	int flags = 0;

	if (nfs4_access & NFS4_SHARE_ACCESS_READ)
		flags |= NFSD_MAY_READ;
	if (nfs4_access & NFS4_SHARE_ACCESS_WRITE)
		flags |= NFSD_MAY_WRITE;
	return flags;
}

static inline __be32
nfsd4_truncate(struct svc_rqst *rqstp, struct svc_fh *fh,
		struct nfsd4_open *open)
{
	struct iattr iattr = {
		.ia_valid = ATTR_SIZE,
		.ia_size = 0,
	};
	if (!open->op_truncate)
		return 0;
	if (!(open->op_share_access & NFS4_SHARE_ACCESS_WRITE))
		return nfserr_inval;
	return nfsd_setattr(rqstp, fh, &iattr, 0, (time_t)0);
}

static __be32
nfs4_upgrade_open(struct svc_rqst *rqstp, struct svc_fh *cur_fh, struct nfs4_stateid *stp, struct nfsd4_open *open)
{
	struct file *filp = stp->st_vfs_file;
	struct inode *inode = filp->f_path.dentry->d_inode;
	unsigned int share_access, new_writer;
	__be32 status;

	set_access(&share_access, stp->st_access_bmap);
	new_writer = (~share_access) & open->op_share_access
			& NFS4_SHARE_ACCESS_WRITE;

	if (new_writer) {
		int err = get_write_access(inode);
		if (err)
			return nfserrno(err);
		err = mnt_want_write(cur_fh->fh_export->ex_path.mnt);
		if (err)
			return nfserrno(err);
		file_take_write(filp);
	}
	status = nfsd4_truncate(rqstp, cur_fh, open);
	if (status) {
		if (new_writer)
			put_write_access(inode);
		return status;
	}
	/* remember the open */
	filp->f_mode |= open->op_share_access;
	__set_bit(open->op_share_access, &stp->st_access_bmap);
	__set_bit(open->op_share_deny, &stp->st_deny_bmap);

	return nfs_ok;
}


static void
nfs4_set_claim_prev(struct nfsd4_open *open)
{
	open->op_stateowner->so_confirmed = 1;
	open->op_stateowner->so_client->cl_firststate = 1;
static __be32 nfs4_get_vfs_file(struct svc_rqst *rqstp, struct nfs4_file *fp,
		struct svc_fh *cur_fh, struct nfs4_ol_stateid *stp,
		struct nfsd4_open *open)
{
	struct file *filp = NULL;
	__be32 status;
	int oflag = nfs4_access_to_omode(open->op_share_access);
	int access = nfs4_access_to_access(open->op_share_access);
	unsigned char old_access_bmap, old_deny_bmap;

	spin_lock(&fp->fi_lock);

	/*
	 * Are we trying to set a deny mode that would conflict with
	 * current access?
	 */
	status = nfs4_file_check_deny(fp, open->op_share_deny);
	if (status != nfs_ok) {
		spin_unlock(&fp->fi_lock);
		goto out;
	}

	/* set access to the file */
	status = nfs4_file_get_access(fp, open->op_share_access);
	if (status != nfs_ok) {
		spin_unlock(&fp->fi_lock);
		goto out;
	}

	/* Set access bits in stateid */
	old_access_bmap = stp->st_access_bmap;
	set_access(open->op_share_access, stp);

	/* Set new deny mask */
	old_deny_bmap = stp->st_deny_bmap;
	set_deny(open->op_share_deny, stp);
	fp->fi_share_deny |= (open->op_share_deny & NFS4_SHARE_DENY_BOTH);

	if (!fp->fi_fds[oflag]) {
		spin_unlock(&fp->fi_lock);
		status = nfsd_open(rqstp, cur_fh, S_IFREG, access, &filp);
		if (status)
			goto out_put_access;
		spin_lock(&fp->fi_lock);
		if (!fp->fi_fds[oflag]) {
			fp->fi_fds[oflag] = filp;
			filp = NULL;
		}
	}
	spin_unlock(&fp->fi_lock);
	if (filp)
		fput(filp);

	status = nfsd4_truncate(rqstp, cur_fh, open);
	if (status)
		goto out_put_access;
out:
	return status;
out_put_access:
	stp->st_access_bmap = old_access_bmap;
	nfs4_file_put_access(fp, open->op_share_access);
	reset_union_bmap_deny(bmap_to_share_mode(old_deny_bmap), stp);
	goto out;
}

static __be32
nfs4_upgrade_open(struct svc_rqst *rqstp, struct nfs4_file *fp, struct svc_fh *cur_fh, struct nfs4_ol_stateid *stp, struct nfsd4_open *open)
{
	__be32 status;
	unsigned char old_deny_bmap = stp->st_deny_bmap;

	if (!test_access(open->op_share_access, stp))
		return nfs4_get_vfs_file(rqstp, fp, cur_fh, stp, open);

	/* test and set deny mode */
	spin_lock(&fp->fi_lock);
	status = nfs4_file_check_deny(fp, open->op_share_deny);
	if (status == nfs_ok) {
		set_deny(open->op_share_deny, stp);
		fp->fi_share_deny |=
				(open->op_share_deny & NFS4_SHARE_DENY_BOTH);
	}
	spin_unlock(&fp->fi_lock);

	if (status != nfs_ok)
		return status;

	status = nfsd4_truncate(rqstp, cur_fh, open);
	if (status != nfs_ok)
		reset_union_bmap_deny(old_deny_bmap, stp);
	return status;
}

/* Should we give out recallable state?: */
static bool nfsd4_cb_channel_good(struct nfs4_client *clp)
{
	if (clp->cl_cb_state == NFSD4_CB_UP)
		return true;
	/*
	 * In the sessions case, since we don't have to establish a
	 * separate connection for callbacks, we assume it's OK
	 * until we hear otherwise:
	 */
	return clp->cl_minorversion && clp->cl_cb_state == NFSD4_CB_UNKNOWN;
}

static struct file_lock *nfs4_alloc_init_lease(struct nfs4_file *fp, int flag)
{
	struct file_lock *fl;

	fl = locks_alloc_lock();
	if (!fl)
		return NULL;
	fl->fl_lmops = &nfsd_lease_mng_ops;
	fl->fl_flags = FL_DELEG;
	fl->fl_type = flag == NFS4_OPEN_DELEGATE_READ? F_RDLCK: F_WRLCK;
	fl->fl_end = OFFSET_MAX;
	fl->fl_owner = (fl_owner_t)fp;
	fl->fl_pid = current->tgid;
	return fl;
}

/**
 * nfs4_setlease - Obtain a delegation by requesting lease from vfs layer
 * @dp:   a pointer to the nfs4_delegation we're adding.
 *
 * Return:
 *      On success: Return code will be 0 on success.
 *
 *      On error: -EAGAIN if there was an existing delegation.
 *                 nonzero if there is an error in other cases.
 *
 */

static int nfs4_setlease(struct nfs4_delegation *dp)
{
	struct nfs4_file *fp = dp->dl_stid.sc_file;
	struct file_lock *fl;
	struct file *filp;
	int status = 0;

	fl = nfs4_alloc_init_lease(fp, NFS4_OPEN_DELEGATE_READ);
	if (!fl)
		return -ENOMEM;
	filp = find_readable_file(fp);
	if (!filp) {
		/* We should always have a readable file here */
		WARN_ON_ONCE(1);
		locks_free_lock(fl);
		return -EBADF;
	}
	fl->fl_file = filp;
	status = vfs_setlease(filp, fl->fl_type, &fl, NULL);
	if (fl)
		locks_free_lock(fl);
	if (status)
		goto out_fput;
	spin_lock(&state_lock);
	spin_lock(&fp->fi_lock);
	/* Did the lease get broken before we took the lock? */
	status = -EAGAIN;
	if (fp->fi_had_conflict)
		goto out_unlock;
	/* Race breaker */
	if (fp->fi_deleg_file) {
		status = hash_delegation_locked(dp, fp);
		goto out_unlock;
	}
	fp->fi_deleg_file = filp;
	fp->fi_delegees = 0;
	status = hash_delegation_locked(dp, fp);
	spin_unlock(&fp->fi_lock);
	spin_unlock(&state_lock);
	if (status) {
		/* Should never happen, this is a new fi_deleg_file  */
		WARN_ON_ONCE(1);
		goto out_fput;
	}
	return 0;
out_unlock:
	spin_unlock(&fp->fi_lock);
	spin_unlock(&state_lock);
out_fput:
	fput(filp);
	return status;
}

static struct nfs4_delegation *
nfs4_set_delegation(struct nfs4_client *clp, struct svc_fh *fh,
		    struct nfs4_file *fp, struct nfs4_clnt_odstate *odstate)
{
	int status;
	struct nfs4_delegation *dp;

	if (fp->fi_had_conflict)
		return ERR_PTR(-EAGAIN);

	spin_lock(&state_lock);
	spin_lock(&fp->fi_lock);
	status = nfs4_get_existing_delegation(clp, fp);
	spin_unlock(&fp->fi_lock);
	spin_unlock(&state_lock);

	if (status)
		return ERR_PTR(status);

	dp = alloc_init_deleg(clp, fh, odstate);
	if (!dp)
		return ERR_PTR(-ENOMEM);

	get_nfs4_file(fp);
	spin_lock(&state_lock);
	spin_lock(&fp->fi_lock);
	dp->dl_stid.sc_file = fp;
	if (!fp->fi_deleg_file) {
		spin_unlock(&fp->fi_lock);
		spin_unlock(&state_lock);
		status = nfs4_setlease(dp);
		goto out;
	}
	if (fp->fi_had_conflict) {
		status = -EAGAIN;
		goto out_unlock;
	}
	status = hash_delegation_locked(dp, fp);
out_unlock:
	spin_unlock(&fp->fi_lock);
	spin_unlock(&state_lock);
out:
	if (status) {
		put_clnt_odstate(dp->dl_clnt_odstate);
		nfs4_put_stid(&dp->dl_stid);
		return ERR_PTR(status);
	}
	return dp;
}

static void nfsd4_open_deleg_none_ext(struct nfsd4_open *open, int status)
{
	open->op_delegate_type = NFS4_OPEN_DELEGATE_NONE_EXT;
	if (status == -EAGAIN)
		open->op_why_no_deleg = WND4_CONTENTION;
	else {
		open->op_why_no_deleg = WND4_RESOURCE;
		switch (open->op_deleg_want) {
		case NFS4_SHARE_WANT_READ_DELEG:
		case NFS4_SHARE_WANT_WRITE_DELEG:
		case NFS4_SHARE_WANT_ANY_DELEG:
			break;
		case NFS4_SHARE_WANT_CANCEL:
			open->op_why_no_deleg = WND4_CANCELLED;
			break;
		case NFS4_SHARE_WANT_NO_DELEG:
			WARN_ON_ONCE(1);
		}
	}
}

/*
 * Attempt to hand out a delegation.
 */
static void
nfs4_open_delegation(struct svc_fh *fh, struct nfsd4_open *open, struct nfs4_stateid *stp)
{
	struct nfs4_delegation *dp;
	struct nfs4_stateowner *sop = stp->st_stateowner;
	struct nfs4_callback *cb = &sop->so_client->cl_callback;
	struct file_lock fl, *flp = &fl;
	int status, flag = 0;

	flag = NFS4_OPEN_DELEGATE_NONE;
	open->op_recall = 0;
	switch (open->op_claim_type) {
		case NFS4_OPEN_CLAIM_PREVIOUS:
			if (!atomic_read(&cb->cb_set))
				open->op_recall = 1;
			flag = open->op_delegate_type;
			if (flag == NFS4_OPEN_DELEGATE_NONE)
				goto out;
			break;
		case NFS4_OPEN_CLAIM_NULL:
			/* Let's not give out any delegations till everyone's
			 * had the chance to reclaim theirs.... */
			if (nfs4_in_grace())
				goto out;
			if (!atomic_read(&cb->cb_set) || !sop->so_confirmed)
				goto out;
			if (open->op_share_access & NFS4_SHARE_ACCESS_WRITE)
				flag = NFS4_OPEN_DELEGATE_WRITE;
			else
				flag = NFS4_OPEN_DELEGATE_READ;
			break;
		default:
			goto out;
	}

	dp = alloc_init_deleg(sop->so_client, stp, fh, flag);
	if (dp == NULL) {
		flag = NFS4_OPEN_DELEGATE_NONE;
		goto out;
	}
	locks_init_lock(&fl);
	fl.fl_lmops = &nfsd_lease_mng_ops;
	fl.fl_flags = FL_LEASE;
	fl.fl_type = flag == NFS4_OPEN_DELEGATE_READ? F_RDLCK: F_WRLCK;
	fl.fl_end = OFFSET_MAX;
	fl.fl_owner =  (fl_owner_t)dp;
	fl.fl_file = stp->st_vfs_file;
	fl.fl_pid = current->tgid;

	/* vfs_setlease checks to see if delegation should be handed out.
	 * the lock_manager callbacks fl_mylease and fl_change are used
	 */
	if ((status = vfs_setlease(stp->st_vfs_file, fl.fl_type, &flp))) {
		dprintk("NFSD: setlease failed [%d], no delegation\n", status);
		unhash_delegation(dp);
		flag = NFS4_OPEN_DELEGATE_NONE;
		goto out;
	}

	memcpy(&open->op_delegate_stateid, &dp->dl_stateid, sizeof(dp->dl_stateid));

	dprintk("NFSD: delegation stateid=(%08x/%08x/%08x/%08x)\n\n",
	             dp->dl_stateid.si_boot,
	             dp->dl_stateid.si_stateownerid,
	             dp->dl_stateid.si_fileid,
	             dp->dl_stateid.si_generation);
out:
	if (open->op_claim_type == NFS4_OPEN_CLAIM_PREVIOUS
			&& flag == NFS4_OPEN_DELEGATE_NONE
			&& open->op_delegate_type != NFS4_OPEN_DELEGATE_NONE)
		dprintk("NFSD: WARNING: refusing delegation reclaim\n");
	open->op_delegate_type = flag;
}

/*
 * called with nfs4_lock_state() held.
 */
__be32
nfsd4_process_open2(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_open *open)
{
	struct nfs4_file *fp = NULL;
	struct inode *ino = current_fh->fh_dentry->d_inode;
	struct nfs4_stateid *stp = NULL;
	struct nfs4_delegation *dp = NULL;
	__be32 status;

	status = nfserr_inval;
	if (!access_valid(open->op_share_access)
			|| !deny_valid(open->op_share_deny))
		goto out;
 *
 * Note we don't support write delegations, and won't until the vfs has
 * proper support for them.
 */
static void
nfs4_open_delegation(struct svc_fh *fh, struct nfsd4_open *open,
			struct nfs4_ol_stateid *stp)
{
	struct nfs4_delegation *dp;
	struct nfs4_openowner *oo = openowner(stp->st_stateowner);
	struct nfs4_client *clp = stp->st_stid.sc_client;
	int cb_up;
	int status = 0;

	cb_up = nfsd4_cb_channel_good(oo->oo_owner.so_client);
	open->op_recall = 0;
	switch (open->op_claim_type) {
		case NFS4_OPEN_CLAIM_PREVIOUS:
			if (!cb_up)
				open->op_recall = 1;
			if (open->op_delegate_type != NFS4_OPEN_DELEGATE_READ)
				goto out_no_deleg;
			break;
		case NFS4_OPEN_CLAIM_NULL:
		case NFS4_OPEN_CLAIM_FH:
			/*
			 * Let's not give out any delegations till everyone's
			 * had the chance to reclaim theirs, *and* until
			 * NLM locks have all been reclaimed:
			 */
			if (locks_in_grace(clp->net))
				goto out_no_deleg;
			if (!cb_up || !(oo->oo_flags & NFS4_OO_CONFIRMED))
				goto out_no_deleg;
			/*
			 * Also, if the file was opened for write or
			 * create, there's a good chance the client's
			 * about to write to it, resulting in an
			 * immediate recall (since we don't support
			 * write delegations):
			 */
			if (open->op_share_access & NFS4_SHARE_ACCESS_WRITE)
				goto out_no_deleg;
			if (open->op_create == NFS4_OPEN_CREATE)
				goto out_no_deleg;
			break;
		default:
			goto out_no_deleg;
	}
	dp = nfs4_set_delegation(clp, fh, stp->st_stid.sc_file, stp->st_clnt_odstate);
	if (IS_ERR(dp))
		goto out_no_deleg;

	memcpy(&open->op_delegate_stateid, &dp->dl_stid.sc_stateid, sizeof(dp->dl_stid.sc_stateid));

	dprintk("NFSD: delegation stateid=" STATEID_FMT "\n",
		STATEID_VAL(&dp->dl_stid.sc_stateid));
	open->op_delegate_type = NFS4_OPEN_DELEGATE_READ;
	nfs4_put_stid(&dp->dl_stid);
	return;
out_no_deleg:
	open->op_delegate_type = NFS4_OPEN_DELEGATE_NONE;
	if (open->op_claim_type == NFS4_OPEN_CLAIM_PREVIOUS &&
	    open->op_delegate_type != NFS4_OPEN_DELEGATE_NONE) {
		dprintk("NFSD: WARNING: refusing delegation reclaim\n");
		open->op_recall = 1;
	}

	/* 4.1 client asking for a delegation? */
	if (open->op_deleg_want)
		nfsd4_open_deleg_none_ext(open, status);
	return;
}

static void nfsd4_deleg_xgrade_none_ext(struct nfsd4_open *open,
					struct nfs4_delegation *dp)
{
	if (open->op_deleg_want == NFS4_SHARE_WANT_READ_DELEG &&
	    dp->dl_type == NFS4_OPEN_DELEGATE_WRITE) {
		open->op_delegate_type = NFS4_OPEN_DELEGATE_NONE_EXT;
		open->op_why_no_deleg = WND4_NOT_SUPP_DOWNGRADE;
	} else if (open->op_deleg_want == NFS4_SHARE_WANT_WRITE_DELEG &&
		   dp->dl_type == NFS4_OPEN_DELEGATE_WRITE) {
		open->op_delegate_type = NFS4_OPEN_DELEGATE_NONE_EXT;
		open->op_why_no_deleg = WND4_NOT_SUPP_UPGRADE;
	}
	/* Otherwise the client must be confused wanting a delegation
	 * it already has, therefore we don't return
	 * NFS4_OPEN_DELEGATE_NONE_EXT and reason.
	 */
}

__be32
nfsd4_process_open2(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_open *open)
{
	struct nfsd4_compoundres *resp = rqstp->rq_resp;
	struct nfs4_client *cl = open->op_openowner->oo_owner.so_client;
	struct nfs4_file *fp = NULL;
	struct nfs4_ol_stateid *stp = NULL;
	struct nfs4_delegation *dp = NULL;
	__be32 status;
	bool new_stp = false;

	/*
	 * Lookup file; if found, lookup stateid and check open request,
	 * and check for delegations in the process of being recalled.
	 * If not found, create the nfs4_file struct
	 */
	fp = find_file(ino);
	if (fp) {
		if ((status = nfs4_check_open(fp, open, &stp)))
			goto out;
		status = nfs4_check_deleg(fp, open, &dp);
		if (status)
			goto out;
	} else {
		status = nfserr_bad_stateid;
		if (open->op_claim_type == NFS4_OPEN_CLAIM_DELEGATE_CUR)
			goto out;
		status = nfserr_resource;
		fp = alloc_init_file(ino);
		if (fp == NULL)
	fp = find_or_add_file(open->op_file, &current_fh->fh_handle);
	if (fp != open->op_file) {
		status = nfs4_check_deleg(cl, open, &dp);
		if (status)
			goto out;
		stp = nfsd4_find_and_lock_existing_open(fp, open);
	} else {
		open->op_file = NULL;
		status = nfserr_bad_stateid;
		if (nfsd4_is_deleg_cur(open))
			goto out;
	}

	if (!stp) {
		stp = init_open_stateid(fp, open);
		if (!stp) {
			status = nfserr_jukebox;
			goto out;
		}

		if (!open->op_stp)
			new_stp = true;
	}

	/*
	 * OPEN the file, or upgrade an existing OPEN.
	 * If truncate fails, the OPEN fails.
	 *
	 * stp is already locked.
	 */
	if (!new_stp) {
		/* Stateid was found, this is an OPEN upgrade */
		status = nfs4_upgrade_open(rqstp, current_fh, stp, open);
		if (status)
			goto out;
		update_stateid(&stp->st_stateid);
	} else {
		/* Stateid was not found, this is a new OPEN */
		int flags = 0;
		if (open->op_share_access & NFS4_SHARE_ACCESS_READ)
			flags |= NFSD_MAY_READ;
		if (open->op_share_access & NFS4_SHARE_ACCESS_WRITE)
			flags |= NFSD_MAY_WRITE;
		status = nfs4_new_open(rqstp, &stp, dp, current_fh, flags);
		if (status)
			goto out;
		init_stateid(stp, fp, open);
		status = nfsd4_truncate(rqstp, current_fh, open);
		if (status) {
			release_stateid(stp, OPEN_STATE);
			goto out;
		}
	}
	memcpy(&open->op_stateid, &stp->st_stateid, sizeof(stateid_t));
		down_read(&stp->st_rwsem);
		mutex_lock(&stp->st_mutex);
		status = nfs4_upgrade_open(rqstp, fp, current_fh, stp, open);
		if (status) {
			mutex_unlock(&stp->st_mutex);
			goto out;
		}
	} else {
		status = nfs4_get_vfs_file(rqstp, fp, current_fh, stp, open);
		if (status) {
			stp->st_stid.sc_type = NFS4_CLOSED_STID;
			release_open_stateid(stp);
			mutex_unlock(&stp->st_mutex);
			goto out;
		}

		stp->st_clnt_odstate = find_or_hash_clnt_odstate(fp,
							open->op_odstate);
		if (stp->st_clnt_odstate == open->op_odstate)
			open->op_odstate = NULL;
	}

	nfs4_inc_and_copy_stateid(&open->op_stateid, &stp->st_stid);
	mutex_unlock(&stp->st_mutex);

	if (nfsd4_has_session(&resp->cstate)) {
		if (open->op_deleg_want & NFS4_SHARE_WANT_NO_DELEG) {
			open->op_delegate_type = NFS4_OPEN_DELEGATE_NONE_EXT;
			open->op_why_no_deleg = WND4_NOT_WANTED;
			goto nodeleg;
		}
	}

	/*
	* Attempt to hand out a delegation. No error return, because the
	* OPEN succeeds even if we fail.
	*/
	nfs4_open_delegation(current_fh, open, stp);

	status = nfs_ok;

	dprintk("nfs4_process_open2: stateid=(%08x/%08x/%08x/%08x)\n",
	            stp->st_stateid.si_boot, stp->st_stateid.si_stateownerid,
	            stp->st_stateid.si_fileid, stp->st_stateid.si_generation);
out:
	if (fp)
		put_nfs4_file(fp);
	if (status == 0 && open->op_claim_type == NFS4_OPEN_CLAIM_PREVIOUS)
		nfs4_set_claim_prev(open);
nodeleg:
	status = nfs_ok;

	dprintk("%s: stateid=" STATEID_FMT "\n", __func__,
		STATEID_VAL(&stp->st_stid.sc_stateid));
out:
	/* 4.1 client trying to upgrade/downgrade delegation? */
	if (open->op_delegate_type == NFS4_OPEN_DELEGATE_NONE && dp &&
	    open->op_deleg_want)
		nfsd4_deleg_xgrade_none_ext(open, dp);

	if (fp)
		put_nfs4_file(fp);
	if (status == 0 && open->op_claim_type == NFS4_OPEN_CLAIM_PREVIOUS)
		open->op_openowner->oo_flags |= NFS4_OO_CONFIRMED;
	/*
	* To finish the open response, we just need to set the rflags.
	*/
	open->op_rflags = NFS4_OPEN_RESULT_LOCKTYPE_POSIX;
	if (!open->op_stateowner->so_confirmed)
		open->op_rflags |= NFS4_OPEN_RESULT_CONFIRM;
	if (!(open->op_openowner->oo_flags & NFS4_OO_CONFIRMED) &&
	    !nfsd4_has_session(&resp->cstate))
		open->op_rflags |= NFS4_OPEN_RESULT_CONFIRM;
	if (dp)
		nfs4_put_stid(&dp->dl_stid);
	if (stp)
		nfs4_put_stid(&stp->st_stid);

	return status;
}

void nfsd4_cleanup_open_state(struct nfsd4_compound_state *cstate,
			      struct nfsd4_open *open)
{
	if (open->op_openowner) {
		struct nfs4_stateowner *so = &open->op_openowner->oo_owner;

		nfsd4_cstate_assign_replay(cstate, so);
		nfs4_put_stateowner(so);
	}
	if (open->op_file)
		kmem_cache_free(file_slab, open->op_file);
	if (open->op_stp)
		nfs4_put_stid(&open->op_stp->st_stid);
	if (open->op_odstate)
		kmem_cache_free(odstate_slab, open->op_odstate);
}

__be32
nfsd4_renew(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	    clientid_t *clid)
{
	struct nfs4_client *clp;
	__be32 status;

	nfs4_lock_state();
	dprintk("process_renew(%08x/%08x): starting\n", 
			clid->cl_boot, clid->cl_id);
	status = nfserr_stale_clientid;
	if (STALE_CLIENTID(clid))
		goto out;
	clp = find_confirmed_client(clid);
	status = nfserr_expired;
	if (clp == NULL) {
		/* We assume the client took too long to RENEW. */
		dprintk("nfsd4_renew: clientid not found!\n");
		goto out;
	}
	renew_client(clp);
	status = nfserr_cb_path_down;
	if (!list_empty(&clp->cl_delegations)
			&& !atomic_read(&clp->cl_callback.cb_set))
		goto out;
	status = nfs_ok;
out:
	nfs4_unlock_state();
	return status;
}

static void
end_grace(void)
{
	dprintk("NFSD: end of grace period\n");
	nfsd4_recdir_purge_old();
	in_grace = 0;
}

static time_t
nfs4_laundromat(void)
{
	struct nfs4_client *clp;
	struct nfs4_stateowner *sop;
	struct nfs4_delegation *dp;
	struct list_head *pos, *next, reaplist;
	time_t cutoff = get_seconds() - NFSD_LEASE_TIME;
	time_t t, clientid_val = NFSD_LEASE_TIME;
	time_t u, test_val = NFSD_LEASE_TIME;

	nfs4_lock_state();

	dprintk("NFSD: laundromat service - starting\n");
	if (in_grace)
		end_grace();
	list_for_each_safe(pos, next, &client_lru) {
		clp = list_entry(pos, struct nfs4_client, cl_lru);
		if (time_after((unsigned long)clp->cl_time, (unsigned long)cutoff)) {
			t = clp->cl_time - cutoff;
			if (clientid_val > t)
				clientid_val = t;
			break;
		}
		dprintk("NFSD: purging unused client (clientid %08x)\n",
			clp->cl_clientid.cl_id);
		nfsd4_remove_clid_dir(clp);
		expire_client(clp);
	}
	INIT_LIST_HEAD(&reaplist);
	spin_lock(&recall_lock);
	list_for_each_safe(pos, next, &del_recall_lru) {
		dp = list_entry (pos, struct nfs4_delegation, dl_recall_lru);
		if (time_after((unsigned long)dp->dl_time, (unsigned long)cutoff)) {
			u = dp->dl_time - cutoff;
			if (test_val > u)
				test_val = u;
			break;
		}
		dprintk("NFSD: purging unused delegation dp %p, fp %p\n",
			            dp, dp->dl_flock);
		list_move(&dp->dl_recall_lru, &reaplist);
	}
	spin_unlock(&recall_lock);
	list_for_each_safe(pos, next, &reaplist) {
		dp = list_entry (pos, struct nfs4_delegation, dl_recall_lru);
		list_del_init(&dp->dl_recall_lru);
		unhash_delegation(dp);
	}
	test_val = NFSD_LEASE_TIME;
	list_for_each_safe(pos, next, &close_lru) {
		sop = list_entry(pos, struct nfs4_stateowner, so_close_lru);
		if (time_after((unsigned long)sop->so_time, (unsigned long)cutoff)) {
			u = sop->so_time - cutoff;
			if (test_val > u)
				test_val = u;
			break;
		}
		dprintk("NFSD: purging unused open stateowner (so_id %d)\n",
			sop->so_id);
		release_stateowner(sop);
	}
	if (clientid_val < NFSD_LAUNDROMAT_MINTIMEOUT)
		clientid_val = NFSD_LAUNDROMAT_MINTIMEOUT;
	nfs4_unlock_state();
	return clientid_val;
	struct nfsd_net *nn = net_generic(SVC_NET(rqstp), nfsd_net_id);

	dprintk("process_renew(%08x/%08x): starting\n", 
			clid->cl_boot, clid->cl_id);
	status = lookup_clientid(clid, cstate, nn);
	if (status)
		goto out;
	clp = cstate->clp;
	status = nfserr_cb_path_down;
	if (!list_empty(&clp->cl_delegations)
			&& clp->cl_cb_state != NFSD4_CB_UP)
		goto out;
	status = nfs_ok;
out:
	return status;
}

void
nfsd4_end_grace(struct nfsd_net *nn)
{
	/* do nothing if grace period already ended */
	if (nn->grace_ended)
		return;

	dprintk("NFSD: end of grace period\n");
	nn->grace_ended = true;
	/*
	 * If the server goes down again right now, an NFSv4
	 * client will still be allowed to reclaim after it comes back up,
	 * even if it hasn't yet had a chance to reclaim state this time.
	 *
	 */
	nfsd4_record_grace_done(nn);
	/*
	 * At this point, NFSv4 clients can still reclaim.  But if the
	 * server crashes, any that have not yet reclaimed will be out
	 * of luck on the next boot.
	 *
	 * (NFSv4.1+ clients are considered to have reclaimed once they
	 * call RECLAIM_COMPLETE.  NFSv4.0 clients are considered to
	 * have reclaimed after their first OPEN.)
	 */
	locks_end_grace(&nn->nfsd4_manager);
	/*
	 * At this point, and once lockd and/or any other containers
	 * exit their grace period, further reclaims will fail and
	 * regular locking can resume.
	 */
}

static time_t
nfs4_laundromat(struct nfsd_net *nn)
{
	struct nfs4_client *clp;
	struct nfs4_openowner *oo;
	struct nfs4_delegation *dp;
	struct nfs4_ol_stateid *stp;
	struct list_head *pos, *next, reaplist;
	time_t cutoff = get_seconds() - nn->nfsd4_lease;
	time_t t, new_timeo = nn->nfsd4_lease;

	dprintk("NFSD: laundromat service - starting\n");
	nfsd4_end_grace(nn);
	INIT_LIST_HEAD(&reaplist);
	spin_lock(&nn->client_lock);
	list_for_each_safe(pos, next, &nn->client_lru) {
		clp = list_entry(pos, struct nfs4_client, cl_lru);
		if (time_after((unsigned long)clp->cl_time, (unsigned long)cutoff)) {
			t = clp->cl_time - cutoff;
			new_timeo = min(new_timeo, t);
			break;
		}
		if (mark_client_expired_locked(clp)) {
			dprintk("NFSD: client in use (clientid %08x)\n",
				clp->cl_clientid.cl_id);
			continue;
		}
		list_add(&clp->cl_lru, &reaplist);
	}
	spin_unlock(&nn->client_lock);
	list_for_each_safe(pos, next, &reaplist) {
		clp = list_entry(pos, struct nfs4_client, cl_lru);
		dprintk("NFSD: purging unused client (clientid %08x)\n",
			clp->cl_clientid.cl_id);
		list_del_init(&clp->cl_lru);
		expire_client(clp);
	}
	spin_lock(&state_lock);
	list_for_each_safe(pos, next, &nn->del_recall_lru) {
		dp = list_entry (pos, struct nfs4_delegation, dl_recall_lru);
		if (time_after((unsigned long)dp->dl_time, (unsigned long)cutoff)) {
			t = dp->dl_time - cutoff;
			new_timeo = min(new_timeo, t);
			break;
		}
		WARN_ON(!unhash_delegation_locked(dp));
		list_add(&dp->dl_recall_lru, &reaplist);
	}
	spin_unlock(&state_lock);
	while (!list_empty(&reaplist)) {
		dp = list_first_entry(&reaplist, struct nfs4_delegation,
					dl_recall_lru);
		list_del_init(&dp->dl_recall_lru);
		revoke_delegation(dp);
	}

	spin_lock(&nn->client_lock);
	while (!list_empty(&nn->close_lru)) {
		oo = list_first_entry(&nn->close_lru, struct nfs4_openowner,
					oo_close_lru);
		if (time_after((unsigned long)oo->oo_time,
			       (unsigned long)cutoff)) {
			t = oo->oo_time - cutoff;
			new_timeo = min(new_timeo, t);
			break;
		}
		list_del_init(&oo->oo_close_lru);
		stp = oo->oo_last_closed_stid;
		oo->oo_last_closed_stid = NULL;
		spin_unlock(&nn->client_lock);
		nfs4_put_stid(&stp->st_stid);
		spin_lock(&nn->client_lock);
	}
	spin_unlock(&nn->client_lock);

	new_timeo = max_t(time_t, new_timeo, NFSD_LAUNDROMAT_MINTIMEOUT);
	return new_timeo;
}

static struct workqueue_struct *laundry_wq;
static void laundromat_main(struct work_struct *);
static DECLARE_DELAYED_WORK(laundromat_work, laundromat_main);

static void
laundromat_main(struct work_struct *not_used)
{
	time_t t;

	t = nfs4_laundromat();
	dprintk("NFSD: laundromat_main - sleeping for %ld seconds\n", t);
	queue_delayed_work(laundry_wq, &laundromat_work, t*HZ);
}

static struct nfs4_stateowner *
search_close_lru(u32 st_id, int flags)
{
	struct nfs4_stateowner *local = NULL;

	if (flags & CLOSE_STATE) {
		list_for_each_entry(local, &close_lru, so_close_lru) {
			if (local->so_id == st_id)
				return local;
		}
	}
	return NULL;
}

static inline int
nfs4_check_fh(struct svc_fh *fhp, struct nfs4_stateid *stp)
{
	return fhp->fh_dentry->d_inode != stp->st_vfs_file->f_path.dentry->d_inode;
}

static int
STALE_STATEID(stateid_t *stateid)
{
	if (stateid->si_boot == boot_time)
		return 0;
	dprintk("NFSD: stale stateid (%08x/%08x/%08x/%08x)!\n",
		stateid->si_boot, stateid->si_stateownerid, stateid->si_fileid,
		stateid->si_generation);
	return 1;
}

static inline int
access_permit_read(unsigned long access_bmap)
{
	return test_bit(NFS4_SHARE_ACCESS_READ, &access_bmap) ||
		test_bit(NFS4_SHARE_ACCESS_BOTH, &access_bmap) ||
		test_bit(NFS4_SHARE_ACCESS_WRITE, &access_bmap);
}

static inline int
access_permit_write(unsigned long access_bmap)
{
	return test_bit(NFS4_SHARE_ACCESS_WRITE, &access_bmap) ||
		test_bit(NFS4_SHARE_ACCESS_BOTH, &access_bmap);
}

static
__be32 nfs4_check_openmode(struct nfs4_stateid *stp, int flags)
{
        __be32 status = nfserr_openmode;

	if ((flags & WR_STATE) && (!access_permit_write(stp->st_access_bmap)))
                goto out;
	if ((flags & RD_STATE) && (!access_permit_read(stp->st_access_bmap)))

static void
laundromat_main(struct work_struct *laundry)
{
	time_t t;
	struct delayed_work *dwork = container_of(laundry, struct delayed_work,
						  work);
	struct nfsd_net *nn = container_of(dwork, struct nfsd_net,
					   laundromat_work);

	t = nfs4_laundromat(nn);
	dprintk("NFSD: laundromat_main - sleeping for %ld seconds\n", t);
	queue_delayed_work(laundry_wq, &nn->laundromat_work, t*HZ);
}

static inline __be32 nfs4_check_fh(struct svc_fh *fhp, struct nfs4_stid *stp)
{
	if (!fh_match(&fhp->fh_handle, &stp->sc_file->fi_fhandle))
		return nfserr_bad_stateid;
	return nfs_ok;
}

static inline int
access_permit_read(struct nfs4_ol_stateid *stp)
{
	return test_access(NFS4_SHARE_ACCESS_READ, stp) ||
		test_access(NFS4_SHARE_ACCESS_BOTH, stp) ||
		test_access(NFS4_SHARE_ACCESS_WRITE, stp);
}

static inline int
access_permit_write(struct nfs4_ol_stateid *stp)
{
	return test_access(NFS4_SHARE_ACCESS_WRITE, stp) ||
		test_access(NFS4_SHARE_ACCESS_BOTH, stp);
}

static
__be32 nfs4_check_openmode(struct nfs4_ol_stateid *stp, int flags)
{
        __be32 status = nfserr_openmode;

	/* For lock stateid's, we test the parent open, not the lock: */
	if (stp->st_openstp)
		stp = stp->st_openstp;
	if ((flags & WR_STATE) && !access_permit_write(stp))
                goto out;
	if ((flags & RD_STATE) && !access_permit_read(stp))
                goto out;
	status = nfs_ok;
out:
	return status;
}

static inline __be32
check_special_stateids(svc_fh *current_fh, stateid_t *stateid, int flags)
{
	/* Trying to call delegreturn with a special stateid? Yuch: */
	if (!(flags & (RD_STATE | WR_STATE)))
		return nfserr_bad_stateid;
	else if (ONE_STATEID(stateid) && (flags & RD_STATE))
		return nfs_ok;
	else if (nfs4_in_grace()) {
		/* Answer in remaining cases depends on existance of
check_special_stateids(struct net *net, svc_fh *current_fh, stateid_t *stateid, int flags)
{
	if (ONE_STATEID(stateid) && (flags & RD_STATE))
		return nfs_ok;
	else if (opens_in_grace(net)) {
		/* Answer in remaining cases depends on existence of
		 * conflicting state; so we must wait out the grace period. */
		return nfserr_grace;
	} else if (flags & WR_STATE)
		return nfs4_share_conflict(current_fh,
				NFS4_SHARE_DENY_WRITE);
	else /* (flags & RD_STATE) && ZERO_STATEID(stateid) */
		return nfs4_share_conflict(current_fh,
				NFS4_SHARE_DENY_READ);
}

/*
 * Allow READ/WRITE during grace period on recovered state only for files
 * that are not able to provide mandatory locking.
 */
static inline int
io_during_grace_disallowed(struct inode *inode, int flags)
{
	return nfs4_in_grace() && (flags & (RD_STATE | WR_STATE))
		&& mandatory_lock(inode);
}

static int check_stateid_generation(stateid_t *in, stateid_t *ref)
{
	/* If the client sends us a stateid from the future, it's buggy: */
	if (in->si_generation > ref->si_generation)
		return nfserr_bad_stateid;
	/*
	 * The following, however, can happen.  For example, if the
	 * client sends an open and some IO at the same time, the open
	 * may bump si_generation while the IO is still in flight.
	 * Thanks to hard links and renames, the client never knows what
	 * file an open will affect.  So it could avoid that situation
	 * only by serializing all opens and IO from the same open
	 * owner.  To recover from the old_stateid error, the client
	 * will just have to retry the IO:
	 */
	if (in->si_generation < ref->si_generation)
		return nfserr_old_stateid;
	return nfs_ok;
}

/*
* Checks for stateid operations
*/
__be32
nfs4_preprocess_stateid_op(struct svc_fh *current_fh, stateid_t *stateid, int flags, struct file **filpp)
{
	struct nfs4_stateid *stp = NULL;
	struct nfs4_delegation *dp = NULL;
	stateid_t *stidp;
	struct inode *ino = current_fh->fh_dentry->d_inode;
	__be32 status;

	dprintk("NFSD: preprocess_stateid_op: stateid = (%08x/%08x/%08x/%08x)\n",
		stateid->si_boot, stateid->si_stateownerid, 
		stateid->si_fileid, stateid->si_generation); 
	if (filpp)
		*filpp = NULL;

	if (io_during_grace_disallowed(ino, flags))
		return nfserr_grace;

	if (ZERO_STATEID(stateid) || ONE_STATEID(stateid))
		return check_special_stateids(current_fh, stateid, flags);

	/* STALE STATEID */
	status = nfserr_stale_stateid;
	if (STALE_STATEID(stateid)) 
		goto out;

	/* BAD STATEID */
	status = nfserr_bad_stateid;
	if (!stateid->si_fileid) { /* delegation stateid */
		if(!(dp = find_delegation_stateid(ino, stateid))) {
			dprintk("NFSD: delegation stateid not found\n");
			goto out;
		}
		stidp = &dp->dl_stateid;
	} else { /* open or lock stateid */
		if (!(stp = find_stateid(stateid, flags))) {
			dprintk("NFSD: open or lock stateid not found\n");
			goto out;
		}
		if ((flags & CHECK_FH) && nfs4_check_fh(current_fh, stp))
			goto out;
		if (!stp->st_stateowner->so_confirmed)
			goto out;
		stidp = &stp->st_stateid;
	}
	status = check_stateid_generation(stateid, stidp);
	if (status)
		goto out;
	if (stp) {
		if ((status = nfs4_check_openmode(stp,flags)))
			goto out;
		renew_client(stp->st_stateowner->so_client);
		if (filpp)
			*filpp = stp->st_vfs_file;
	} else {
		if ((status = nfs4_check_delegmode(dp, flags)))
			goto out;
		renew_client(dp->dl_client);
		if (flags & DELEG_RET)
			unhash_delegation(dp);
		if (filpp)
			*filpp = dp->dl_vfs_file;
	}
	status = nfs_ok;
out:
	return status;
}

grace_disallows_io(struct net *net, struct inode *inode)
{
	return opens_in_grace(net) && mandatory_lock(inode);
}

/* Returns true iff a is later than b: */
static bool stateid_generation_after(stateid_t *a, stateid_t *b)
{
	return (s32)(a->si_generation - b->si_generation) > 0;
}

static __be32 check_stateid_generation(stateid_t *in, stateid_t *ref, bool has_session)
{
	/*
	 * When sessions are used the stateid generation number is ignored
	 * when it is zero.
	 */
	if (has_session && in->si_generation == 0)
		return nfs_ok;

	if (in->si_generation == ref->si_generation)
		return nfs_ok;

	/* If the client sends us a stateid from the future, it's buggy: */
	if (stateid_generation_after(in, ref))
		return nfserr_bad_stateid;
	/*
	 * However, we could see a stateid from the past, even from a
	 * non-buggy client.  For example, if the client sends a lock
	 * while some IO is outstanding, the lock may bump si_generation
	 * while the IO is still in flight.  The client could avoid that
	 * situation by waiting for responses on all the IO requests,
	 * but better performance may result in retrying IO that
	 * receives an old_stateid error if requests are rarely
	 * reordered in flight:
	 */
	return nfserr_old_stateid;
}

static __be32 nfsd4_check_openowner_confirmed(struct nfs4_ol_stateid *ols)
{
	if (ols->st_stateowner->so_is_open_owner &&
	    !(openowner(ols->st_stateowner)->oo_flags & NFS4_OO_CONFIRMED))
		return nfserr_bad_stateid;
	return nfs_ok;
}

static __be32 nfsd4_validate_stateid(struct nfs4_client *cl, stateid_t *stateid)
{
	struct nfs4_stid *s;
	__be32 status = nfserr_bad_stateid;

	if (ZERO_STATEID(stateid) || ONE_STATEID(stateid) ||
		CLOSE_STATEID(stateid))
		return status;
	/* Client debugging aid. */
	if (!same_clid(&stateid->si_opaque.so_clid, &cl->cl_clientid)) {
		char addr_str[INET6_ADDRSTRLEN];
		rpc_ntop((struct sockaddr *)&cl->cl_addr, addr_str,
				 sizeof(addr_str));
		pr_warn_ratelimited("NFSD: client %s testing state ID "
					"with incorrect client ID\n", addr_str);
		return status;
	}
	spin_lock(&cl->cl_lock);
	s = find_stateid_locked(cl, stateid);
	if (!s)
		goto out_unlock;
	status = check_stateid_generation(stateid, &s->sc_stateid, 1);
	if (status)
		goto out_unlock;
	switch (s->sc_type) {
	case NFS4_DELEG_STID:
		status = nfs_ok;
		break;
	case NFS4_REVOKED_DELEG_STID:
		status = nfserr_deleg_revoked;
		break;
	case NFS4_OPEN_STID:
	case NFS4_LOCK_STID:
		status = nfsd4_check_openowner_confirmed(openlockstateid(s));
		break;
	default:
		printk("unknown stateid type %x\n", s->sc_type);
		/* Fallthrough */
	case NFS4_CLOSED_STID:
	case NFS4_CLOSED_DELEG_STID:
		status = nfserr_bad_stateid;
	}
out_unlock:
	spin_unlock(&cl->cl_lock);
	return status;
}

__be32
nfsd4_lookup_stateid(struct nfsd4_compound_state *cstate,
		     stateid_t *stateid, unsigned char typemask,
		     struct nfs4_stid **s, struct nfsd_net *nn)
{
	__be32 status;
	bool return_revoked = false;

	/*
	 *  only return revoked delegations if explicitly asked.
	 *  otherwise we report revoked or bad_stateid status.
	 */
	if (typemask & NFS4_REVOKED_DELEG_STID)
		return_revoked = true;
	else if (typemask & NFS4_DELEG_STID)
		typemask |= NFS4_REVOKED_DELEG_STID;

	if (ZERO_STATEID(stateid) || ONE_STATEID(stateid) ||
		CLOSE_STATEID(stateid))
		return nfserr_bad_stateid;
	status = lookup_clientid(&stateid->si_opaque.so_clid, cstate, nn);
	if (status == nfserr_stale_clientid) {
		if (cstate->session)
			return nfserr_bad_stateid;
		return nfserr_stale_stateid;
	}
	if (status)
		return status;
	*s = find_stateid_by_type(cstate->clp, stateid, typemask);
	if (!*s)
		return nfserr_bad_stateid;
	if (((*s)->sc_type == NFS4_REVOKED_DELEG_STID) && !return_revoked) {
		nfs4_put_stid(*s);
		if (cstate->minorversion)
			return nfserr_deleg_revoked;
		return nfserr_bad_stateid;
	}
	return nfs_ok;
}

static struct file *
nfs4_find_file(struct nfs4_stid *s, int flags)
{
	if (!s)
		return NULL;

	switch (s->sc_type) {
	case NFS4_DELEG_STID:
		if (WARN_ON_ONCE(!s->sc_file->fi_deleg_file))
			return NULL;
		return get_file(s->sc_file->fi_deleg_file);
	case NFS4_OPEN_STID:
	case NFS4_LOCK_STID:
		if (flags & RD_STATE)
			return find_readable_file(s->sc_file);
		else
			return find_writeable_file(s->sc_file);
		break;
	}

	return NULL;
}

static __be32
nfs4_check_olstateid(struct svc_fh *fhp, struct nfs4_ol_stateid *ols, int flags)
{
	__be32 status;

	status = nfsd4_check_openowner_confirmed(ols);
	if (status)
		return status;
	return nfs4_check_openmode(ols, flags);
}

static __be32
nfs4_check_file(struct svc_rqst *rqstp, struct svc_fh *fhp, struct nfs4_stid *s,
		struct file **filpp, bool *tmp_file, int flags)
{
	int acc = (flags & RD_STATE) ? NFSD_MAY_READ : NFSD_MAY_WRITE;
	struct file *file;
	__be32 status;

	file = nfs4_find_file(s, flags);
	if (file) {
		status = nfsd_permission(rqstp, fhp->fh_export, fhp->fh_dentry,
				acc | NFSD_MAY_OWNER_OVERRIDE);
		if (status) {
			fput(file);
			return status;
		}

		*filpp = file;
	} else {
		status = nfsd_open(rqstp, fhp, S_IFREG, acc, filpp);
		if (status)
			return status;

		if (tmp_file)
			*tmp_file = true;
	}

	return 0;
}

/*
 * Checks for stateid operations
 */
__be32
nfs4_preprocess_stateid_op(struct svc_rqst *rqstp,
		struct nfsd4_compound_state *cstate, stateid_t *stateid,
		int flags, struct file **filpp, bool *tmp_file)
{
	struct svc_fh *fhp = &cstate->current_fh;
	struct inode *ino = d_inode(fhp->fh_dentry);
	struct net *net = SVC_NET(rqstp);
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);
	struct nfs4_stid *s = NULL;
	__be32 status;

	if (filpp)
		*filpp = NULL;
	if (tmp_file)
		*tmp_file = false;

	if (grace_disallows_io(net, ino))
		return nfserr_grace;

	if (ZERO_STATEID(stateid) || ONE_STATEID(stateid)) {
		status = check_special_stateids(net, fhp, stateid, flags);
		goto done;
	}

	status = nfsd4_lookup_stateid(cstate, stateid,
				NFS4_DELEG_STID|NFS4_OPEN_STID|NFS4_LOCK_STID,
				&s, nn);
	if (status)
		return status;
	status = check_stateid_generation(stateid, &s->sc_stateid,
			nfsd4_has_session(cstate));
	if (status)
		goto out;

	switch (s->sc_type) {
	case NFS4_DELEG_STID:
		status = nfs4_check_delegmode(delegstateid(s), flags);
		break;
	case NFS4_OPEN_STID:
	case NFS4_LOCK_STID:
		status = nfs4_check_olstateid(fhp, openlockstateid(s), flags);
		break;
	default:
		status = nfserr_bad_stateid;
		break;
	}
	if (status)
		goto out;
	status = nfs4_check_fh(fhp, s);

done:
	if (!status && filpp)
		status = nfs4_check_file(rqstp, fhp, s, filpp, tmp_file, flags);
out:
	if (s)
		nfs4_put_stid(s);
	return status;
}

/*
 * Test if the stateid is valid
 */
__be32
nfsd4_test_stateid(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
		   struct nfsd4_test_stateid *test_stateid)
{
	struct nfsd4_test_stateid_id *stateid;
	struct nfs4_client *cl = cstate->session->se_client;

	list_for_each_entry(stateid, &test_stateid->ts_stateid_list, ts_id_list)
		stateid->ts_id_status =
			nfsd4_validate_stateid(cl, &stateid->ts_id_stateid);

	return nfs_ok;
}

static __be32
nfsd4_free_lock_stateid(stateid_t *stateid, struct nfs4_stid *s)
{
	struct nfs4_ol_stateid *stp = openlockstateid(s);
	__be32 ret;

	mutex_lock(&stp->st_mutex);

	ret = check_stateid_generation(stateid, &s->sc_stateid, 1);
	if (ret)
		goto out;

	ret = nfserr_locks_held;
	if (check_for_locks(stp->st_stid.sc_file,
			    lockowner(stp->st_stateowner)))
		goto out;

	release_lock_stateid(stp);
	ret = nfs_ok;

out:
	mutex_unlock(&stp->st_mutex);
	nfs4_put_stid(s);
	return ret;
}

__be32
nfsd4_free_stateid(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
		   struct nfsd4_free_stateid *free_stateid)
{
	stateid_t *stateid = &free_stateid->fr_stateid;
	struct nfs4_stid *s;
	struct nfs4_delegation *dp;
	struct nfs4_client *cl = cstate->session->se_client;
	__be32 ret = nfserr_bad_stateid;

	spin_lock(&cl->cl_lock);
	s = find_stateid_locked(cl, stateid);
	if (!s)
		goto out_unlock;
	switch (s->sc_type) {
	case NFS4_DELEG_STID:
		ret = nfserr_locks_held;
		break;
	case NFS4_OPEN_STID:
		ret = check_stateid_generation(stateid, &s->sc_stateid, 1);
		if (ret)
			break;
		ret = nfserr_locks_held;
		break;
	case NFS4_LOCK_STID:
		atomic_inc(&s->sc_count);
		spin_unlock(&cl->cl_lock);
		ret = nfsd4_free_lock_stateid(stateid, s);
		goto out;
	case NFS4_REVOKED_DELEG_STID:
		dp = delegstateid(s);
		list_del_init(&dp->dl_recall_lru);
		spin_unlock(&cl->cl_lock);
		nfs4_put_stid(s);
		ret = nfs_ok;
		goto out;
	/* Default falls through and returns nfserr_bad_stateid */
	}
out_unlock:
	spin_unlock(&cl->cl_lock);
out:
	return ret;
}

static inline int
setlkflg (int type)
{
	return (type == NFS4_READW_LT || type == NFS4_READ_LT) ?
		RD_STATE : WR_STATE;
}

static __be32 nfs4_seqid_op_checks(struct nfsd4_compound_state *cstate, stateid_t *stateid, u32 seqid, struct nfs4_ol_stateid *stp)
{
	struct svc_fh *current_fh = &cstate->current_fh;
	struct nfs4_stateowner *sop = stp->st_stateowner;
	__be32 status;

	status = nfsd4_check_seqid(cstate, sop, seqid);
	if (status)
		return status;
	status = nfsd4_lock_ol_stateid(stp);
	if (status != nfs_ok)
		return status;
	status = check_stateid_generation(stateid, &stp->st_stid.sc_stateid, nfsd4_has_session(cstate));
	if (status == nfs_ok)
		status = nfs4_check_fh(current_fh, &stp->st_stid);
	if (status != nfs_ok)
		mutex_unlock(&stp->st_mutex);
	return status;
}

/* 
 * Checks for sequence id mutating operations. 
 */
static __be32
nfs4_preprocess_seqid_op(struct svc_fh *current_fh, u32 seqid, stateid_t *stateid, int flags, struct nfs4_stateowner **sopp, struct nfs4_stateid **stpp, struct nfsd4_lock *lock)
{
	struct nfs4_stateid *stp;
	struct nfs4_stateowner *sop;
	__be32 status;

	dprintk("NFSD: preprocess_seqid_op: seqid=%d " 
			"stateid = (%08x/%08x/%08x/%08x)\n", seqid,
		stateid->si_boot, stateid->si_stateownerid, stateid->si_fileid,
		stateid->si_generation);

	*stpp = NULL;
	*sopp = NULL;

	if (ZERO_STATEID(stateid) || ONE_STATEID(stateid)) {
		dprintk("NFSD: preprocess_seqid_op: magic stateid!\n");
		return nfserr_bad_stateid;
	}

	if (STALE_STATEID(stateid))
		return nfserr_stale_stateid;
	/*
	* We return BAD_STATEID if filehandle doesn't match stateid, 
	* the confirmed flag is incorrecly set, or the generation 
	* number is incorrect.  
	*/
	stp = find_stateid(stateid, flags);
	if (stp == NULL) {
		/*
		 * Also, we should make sure this isn't just the result of
		 * a replayed close:
		 */
		sop = search_close_lru(stateid->si_stateownerid, flags);
		if (sop == NULL)
			return nfserr_bad_stateid;
		*sopp = sop;
		goto check_replay;
	}

	*stpp = stp;
	*sopp = sop = stp->st_stateowner;

	if (lock) {
		clientid_t *lockclid = &lock->v.new.clientid;
		struct nfs4_client *clp = sop->so_client;
		int lkflg = 0;
		__be32 status;

		lkflg = setlkflg(lock->lk_type);

		if (lock->lk_is_new) {
			if (!sop->so_is_open_owner)
				return nfserr_bad_stateid;
			if (!same_clid(&clp->cl_clientid, lockclid))
			       return nfserr_bad_stateid;
			/* stp is the open stateid */
			status = nfs4_check_openmode(stp, lkflg);
			if (status)
				return status;
		} else {
			/* stp is the lock stateid */
			status = nfs4_check_openmode(stp->st_openstp, lkflg);
			if (status)
				return status;
               }
	}

	if (nfs4_check_fh(current_fh, stp)) {
		dprintk("NFSD: preprocess_seqid_op: fh-stateid mismatch!\n");
		return nfserr_bad_stateid;
	}

	/*
	*  We now validate the seqid and stateid generation numbers.
	*  For the moment, we ignore the possibility of 
	*  generation number wraparound.
	*/
	if (seqid != sop->so_seqid)
		goto check_replay;

	if (sop->so_confirmed && flags & CONFIRM) {
		dprintk("NFSD: preprocess_seqid_op: expected"
				" unconfirmed stateowner!\n");
		return nfserr_bad_stateid;
	}
	if (!sop->so_confirmed && !(flags & CONFIRM)) {
		dprintk("NFSD: preprocess_seqid_op: stateowner not"
				" confirmed yet!\n");
		return nfserr_bad_stateid;
	}
	status = check_stateid_generation(stateid, &stp->st_stateid);
	if (status)
		return status;
	renew_client(sop->so_client);
	return nfs_ok;

check_replay:
	if (seqid == sop->so_seqid - 1) {
		dprintk("NFSD: preprocess_seqid_op: retransmission?\n");
		/* indicate replay to calling function */
		return nfserr_replay_me;
	}
	dprintk("NFSD: preprocess_seqid_op: bad seqid (expected %d, got %d)\n",
			sop->so_seqid, seqid);
	*sopp = NULL;
	return nfserr_bad_seqid;
nfs4_preprocess_seqid_op(struct nfsd4_compound_state *cstate, u32 seqid,
			 stateid_t *stateid, char typemask,
			 struct nfs4_ol_stateid **stpp,
			 struct nfsd_net *nn)
{
	__be32 status;
	struct nfs4_stid *s;
	struct nfs4_ol_stateid *stp = NULL;

	dprintk("NFSD: %s: seqid=%d stateid = " STATEID_FMT "\n", __func__,
		seqid, STATEID_VAL(stateid));

	*stpp = NULL;
	status = nfsd4_lookup_stateid(cstate, stateid, typemask, &s, nn);
	if (status)
		return status;
	stp = openlockstateid(s);
	nfsd4_cstate_assign_replay(cstate, stp->st_stateowner);

	status = nfs4_seqid_op_checks(cstate, stateid, seqid, stp);
	if (!status)
		*stpp = stp;
	else
		nfs4_put_stid(&stp->st_stid);
	return status;
}

static __be32 nfs4_preprocess_confirmed_seqid_op(struct nfsd4_compound_state *cstate, u32 seqid,
						 stateid_t *stateid, struct nfs4_ol_stateid **stpp, struct nfsd_net *nn)
{
	__be32 status;
	struct nfs4_openowner *oo;
	struct nfs4_ol_stateid *stp;

	status = nfs4_preprocess_seqid_op(cstate, seqid, stateid,
						NFS4_OPEN_STID, &stp, nn);
	if (status)
		return status;
	oo = openowner(stp->st_stateowner);
	if (!(oo->oo_flags & NFS4_OO_CONFIRMED)) {
		mutex_unlock(&stp->st_mutex);
		nfs4_put_stid(&stp->st_stid);
		return nfserr_bad_stateid;
	}
	*stpp = stp;
	return nfs_ok;
}

__be32
nfsd4_open_confirm(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
		   struct nfsd4_open_confirm *oc)
{
	__be32 status;
	struct nfs4_stateowner *sop;
	struct nfs4_stateid *stp;

	dprintk("NFSD: nfsd4_open_confirm on file %.*s\n",
			(int)cstate->current_fh.fh_dentry->d_name.len,
			cstate->current_fh.fh_dentry->d_name.name);
	struct nfs4_openowner *oo;
	struct nfs4_ol_stateid *stp;
	struct nfsd_net *nn = net_generic(SVC_NET(rqstp), nfsd_net_id);

	dprintk("NFSD: nfsd4_open_confirm on file %pd\n",
			cstate->current_fh.fh_dentry);

	status = fh_verify(rqstp, &cstate->current_fh, S_IFREG, 0);
	if (status)
		return status;

	nfs4_lock_state();

	if ((status = nfs4_preprocess_seqid_op(&cstate->current_fh,
					oc->oc_seqid, &oc->oc_req_stateid,
					CONFIRM | OPEN_STATE,
					&oc->oc_stateowner, &stp, NULL)))
		goto out; 

	sop = oc->oc_stateowner;
	sop->so_confirmed = 1;
	update_stateid(&stp->st_stateid);
	memcpy(&oc->oc_resp_stateid, &stp->st_stateid, sizeof(stateid_t));
	dprintk("NFSD: nfsd4_open_confirm: success, seqid=%d " 
		"stateid=(%08x/%08x/%08x/%08x)\n", oc->oc_seqid,
		         stp->st_stateid.si_boot,
		         stp->st_stateid.si_stateownerid,
		         stp->st_stateid.si_fileid,
		         stp->st_stateid.si_generation);

	nfsd4_create_clid_dir(sop->so_client);
out:
	if (oc->oc_stateowner) {
		nfs4_get_stateowner(oc->oc_stateowner);
		cstate->replay_owner = oc->oc_stateowner;
	}
	nfs4_unlock_state();
	return status;
}


/*
 * unset all bits in union bitmap (bmap) that
 * do not exist in share (from successful OPEN_DOWNGRADE)
 */
static void
reset_union_bmap_access(unsigned long access, unsigned long *bmap)
{
	int i;
	for (i = 1; i < 4; i++) {
		if ((i & access) != i)
			__clear_bit(i, bmap);
	}
}

static void
reset_union_bmap_deny(unsigned long deny, unsigned long *bmap)
{
	int i;
	for (i = 0; i < 4; i++) {
		if ((i & deny) != i)
			__clear_bit(i, bmap);
	status = nfs4_preprocess_seqid_op(cstate,
					oc->oc_seqid, &oc->oc_req_stateid,
					NFS4_OPEN_STID, &stp, nn);
	if (status)
		goto out;
	oo = openowner(stp->st_stateowner);
	status = nfserr_bad_stateid;
	if (oo->oo_flags & NFS4_OO_CONFIRMED) {
		mutex_unlock(&stp->st_mutex);
		goto put_stateid;
	}
	oo->oo_flags |= NFS4_OO_CONFIRMED;
	nfs4_inc_and_copy_stateid(&oc->oc_resp_stateid, &stp->st_stid);
	mutex_unlock(&stp->st_mutex);
	dprintk("NFSD: %s: success, seqid=%d stateid=" STATEID_FMT "\n",
		__func__, oc->oc_seqid, STATEID_VAL(&stp->st_stid.sc_stateid));

	nfsd4_client_record_create(oo->oo_owner.so_client);
	status = nfs_ok;
put_stateid:
	nfs4_put_stid(&stp->st_stid);
out:
	nfsd4_bump_seqid(cstate, status);
	return status;
}

static inline void nfs4_stateid_downgrade_bit(struct nfs4_ol_stateid *stp, u32 access)
{
	if (!test_access(access, stp))
		return;
	nfs4_file_put_access(stp->st_stid.sc_file, access);
	clear_access(access, stp);
}

static inline void nfs4_stateid_downgrade(struct nfs4_ol_stateid *stp, u32 to_access)
{
	switch (to_access) {
	case NFS4_SHARE_ACCESS_READ:
		nfs4_stateid_downgrade_bit(stp, NFS4_SHARE_ACCESS_WRITE);
		nfs4_stateid_downgrade_bit(stp, NFS4_SHARE_ACCESS_BOTH);
		break;
	case NFS4_SHARE_ACCESS_WRITE:
		nfs4_stateid_downgrade_bit(stp, NFS4_SHARE_ACCESS_READ);
		nfs4_stateid_downgrade_bit(stp, NFS4_SHARE_ACCESS_BOTH);
		break;
	case NFS4_SHARE_ACCESS_BOTH:
		break;
	default:
		WARN_ON_ONCE(1);
	}
}

__be32
nfsd4_open_downgrade(struct svc_rqst *rqstp,
		     struct nfsd4_compound_state *cstate,
		     struct nfsd4_open_downgrade *od)
{
	__be32 status;
	struct nfs4_stateid *stp;
	unsigned int share_access;

	dprintk("NFSD: nfsd4_open_downgrade on file %.*s\n", 
			(int)cstate->current_fh.fh_dentry->d_name.len,
			cstate->current_fh.fh_dentry->d_name.name);

	if (!access_valid(od->od_share_access)
			|| !deny_valid(od->od_share_deny))
		return nfserr_inval;

	nfs4_lock_state();
	if ((status = nfs4_preprocess_seqid_op(&cstate->current_fh,
					od->od_seqid,
					&od->od_stateid, 
					OPEN_STATE,
					&od->od_stateowner, &stp, NULL)))
		goto out; 

	status = nfserr_inval;
	if (!test_bit(od->od_share_access, &stp->st_access_bmap)) {
		dprintk("NFSD:access not a subset current bitmap: 0x%lx, input access=%08x\n",
			stp->st_access_bmap, od->od_share_access);
		goto out;
	}
	if (!test_bit(od->od_share_deny, &stp->st_deny_bmap)) {
		dprintk("NFSD:deny not a subset current bitmap: 0x%lx, input deny=%08x\n",
			stp->st_deny_bmap, od->od_share_deny);
		goto out;
	}
	set_access(&share_access, stp->st_access_bmap);
	nfs4_file_downgrade(stp->st_vfs_file,
	                    share_access & ~od->od_share_access);

	reset_union_bmap_access(od->od_share_access, &stp->st_access_bmap);
	reset_union_bmap_deny(od->od_share_deny, &stp->st_deny_bmap);

	update_stateid(&stp->st_stateid);
	memcpy(&od->od_stateid, &stp->st_stateid, sizeof(stateid_t));
	status = nfs_ok;
out:
	if (od->od_stateowner) {
		nfs4_get_stateowner(od->od_stateowner);
		cstate->replay_owner = od->od_stateowner;
	}
	nfs4_unlock_state();
	return status;
}

	struct nfs4_ol_stateid *stp;
	struct nfsd_net *nn = net_generic(SVC_NET(rqstp), nfsd_net_id);

	dprintk("NFSD: nfsd4_open_downgrade on file %pd\n", 
			cstate->current_fh.fh_dentry);

	/* We don't yet support WANT bits: */
	if (od->od_deleg_want)
		dprintk("NFSD: %s: od_deleg_want=0x%x ignored\n", __func__,
			od->od_deleg_want);

	status = nfs4_preprocess_confirmed_seqid_op(cstate, od->od_seqid,
					&od->od_stateid, &stp, nn);
	if (status)
		goto out; 
	status = nfserr_inval;
	if (!test_access(od->od_share_access, stp)) {
		dprintk("NFSD: access not a subset of current bitmap: 0x%hhx, input access=%08x\n",
			stp->st_access_bmap, od->od_share_access);
		goto put_stateid;
	}
	if (!test_deny(od->od_share_deny, stp)) {
		dprintk("NFSD: deny not a subset of current bitmap: 0x%hhx, input deny=%08x\n",
			stp->st_deny_bmap, od->od_share_deny);
		goto put_stateid;
	}
	nfs4_stateid_downgrade(stp, od->od_share_access);
	reset_union_bmap_deny(od->od_share_deny, stp);
	nfs4_inc_and_copy_stateid(&od->od_stateid, &stp->st_stid);
	status = nfs_ok;
put_stateid:
	mutex_unlock(&stp->st_mutex);
	nfs4_put_stid(&stp->st_stid);
out:
	nfsd4_bump_seqid(cstate, status);
	return status;
}

static bool nfsd4_close_open_stateid(struct nfs4_ol_stateid *s)
{
	struct nfs4_client *clp = s->st_stid.sc_client;
	bool unhashed;
	LIST_HEAD(reaplist);

	spin_lock(&clp->cl_lock);
	unhashed = unhash_open_stateid(s, &reaplist);

	if (clp->cl_minorversion) {
		if (unhashed)
			put_ol_stateid_locked(s, &reaplist);
		spin_unlock(&clp->cl_lock);
		free_ol_stateid_reaplist(&reaplist);
		return false;
	} else {
		spin_unlock(&clp->cl_lock);
		free_ol_stateid_reaplist(&reaplist);
		return unhashed;
	}
}

/*
 * nfs4_unlock_state() called after encode
 */
__be32
nfsd4_close(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	    struct nfsd4_close *close)
{
	__be32 status;
	struct nfs4_stateid *stp;

	dprintk("NFSD: nfsd4_close on file %.*s\n", 
			(int)cstate->current_fh.fh_dentry->d_name.len,
			cstate->current_fh.fh_dentry->d_name.name);

	nfs4_lock_state();
	/* check close_lru for replay */
	if ((status = nfs4_preprocess_seqid_op(&cstate->current_fh,
					close->cl_seqid,
					&close->cl_stateid, 
					OPEN_STATE | CLOSE_STATE,
					&close->cl_stateowner, &stp, NULL)))
		goto out; 
	status = nfs_ok;
	update_stateid(&stp->st_stateid);
	memcpy(&close->cl_stateid, &stp->st_stateid, sizeof(stateid_t));

	/* release_stateid() calls nfsd_close() if needed */
	release_stateid(stp, OPEN_STATE);

	/* place unused nfs4_stateowners on so_close_lru list to be
	 * released by the laundromat service after the lease period
	 * to enable us to handle CLOSE replay
	 */
	if (list_empty(&close->cl_stateowner->so_stateids))
		move_to_close_lru(close->cl_stateowner);
out:
	if (close->cl_stateowner) {
		nfs4_get_stateowner(close->cl_stateowner);
		cstate->replay_owner = close->cl_stateowner;
	}
	nfs4_unlock_state();
	struct nfs4_ol_stateid *stp;
	struct net *net = SVC_NET(rqstp);
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);
	bool need_move_to_close_list;

	dprintk("NFSD: nfsd4_close on file %pd\n", 
			cstate->current_fh.fh_dentry);

	status = nfs4_preprocess_seqid_op(cstate, close->cl_seqid,
					&close->cl_stateid,
					NFS4_OPEN_STID|NFS4_CLOSED_STID,
					&stp, nn);
	nfsd4_bump_seqid(cstate, status);
	if (status)
		goto out; 

	stp->st_stid.sc_type = NFS4_CLOSED_STID;
	nfs4_inc_and_copy_stateid(&close->cl_stateid, &stp->st_stid);

	need_move_to_close_list = nfsd4_close_open_stateid(stp);
	mutex_unlock(&stp->st_mutex);
	if (need_move_to_close_list)
		move_to_close_lru(stp, net);

	/* See RFC5661 sectionm 18.2.4 */
	if (stp->st_stid.sc_client->cl_minorversion)
		memcpy(&close->cl_stateid, &close_stateid,
				sizeof(close->cl_stateid));

	/* put reference from nfs4_preprocess_seqid_op */
	nfs4_put_stid(&stp->st_stid);
out:
	return status;
}

__be32
nfsd4_delegreturn(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
		  struct nfsd4_delegreturn *dr)
{
	__be32 status;

	if ((status = fh_verify(rqstp, &cstate->current_fh, S_IFREG, 0)))
		goto out;

	nfs4_lock_state();
	status = nfs4_preprocess_stateid_op(&cstate->current_fh,
					    &dr->dr_stateid, DELEG_RET, NULL);
	nfs4_unlock_state();
	struct nfs4_delegation *dp;
	stateid_t *stateid = &dr->dr_stateid;
	struct nfs4_stid *s;
	__be32 status;
	struct nfsd_net *nn = net_generic(SVC_NET(rqstp), nfsd_net_id);

	if ((status = fh_verify(rqstp, &cstate->current_fh, S_IFREG, 0)))
		return status;

	status = nfsd4_lookup_stateid(cstate, stateid, NFS4_DELEG_STID, &s, nn);
	if (status)
		goto out;
	dp = delegstateid(s);
	status = check_stateid_generation(stateid, &dp->dl_stid.sc_stateid, nfsd4_has_session(cstate));
	if (status)
		goto put_stateid;

	destroy_delegation(dp);
put_stateid:
	nfs4_put_stid(&dp->dl_stid);
out:
	return status;
}


/* 
 * Lock owner state (byte-range locks)
 */
#define LOFF_OVERFLOW(start, len)      ((u64)(len) > ~(u64)(start))
#define LOCK_HASH_BITS              8
#define LOCK_HASH_SIZE             (1 << LOCK_HASH_BITS)
#define LOCK_HASH_MASK             (LOCK_HASH_SIZE - 1)

#define lockownerid_hashval(id) \
        ((id) & LOCK_HASH_MASK)

static inline unsigned int
lock_ownerstr_hashval(struct inode *inode, u32 cl_id,
		struct xdr_netobj *ownername)
{
	return (file_hashval(inode) + cl_id
			+ opaque_hashval(ownername->data, ownername->len))
		& LOCK_HASH_MASK;
}

static struct list_head lock_ownerid_hashtbl[LOCK_HASH_SIZE];
static struct list_head	lock_ownerstr_hashtbl[LOCK_HASH_SIZE];
static struct list_head lockstateid_hashtbl[STATEID_HASH_SIZE];

static struct nfs4_stateid *
find_stateid(stateid_t *stid, int flags)
{
	struct nfs4_stateid *local = NULL;
	u32 st_id = stid->si_stateownerid;
	u32 f_id = stid->si_fileid;
	unsigned int hashval;

	dprintk("NFSD: find_stateid flags 0x%x\n",flags);
	if ((flags & LOCK_STATE) || (flags & RD_STATE) || (flags & WR_STATE)) {
		hashval = stateid_hashval(st_id, f_id);
		list_for_each_entry(local, &lockstateid_hashtbl[hashval], st_hash) {
			if ((local->st_stateid.si_stateownerid == st_id) &&
			    (local->st_stateid.si_fileid == f_id))
				return local;
		}
	} 
	if ((flags & OPEN_STATE) || (flags & RD_STATE) || (flags & WR_STATE)) {
		hashval = stateid_hashval(st_id, f_id);
		list_for_each_entry(local, &stateid_hashtbl[hashval], st_hash) {
			if ((local->st_stateid.si_stateownerid == st_id) &&
			    (local->st_stateid.si_fileid == f_id))
				return local;
		}
	}
	return NULL;
}

static struct nfs4_delegation *
find_delegation_stateid(struct inode *ino, stateid_t *stid)
{
	struct nfs4_file *fp;
	struct nfs4_delegation *dl;

	dprintk("NFSD:find_delegation_stateid stateid=(%08x/%08x/%08x/%08x)\n",
                    stid->si_boot, stid->si_stateownerid,
                    stid->si_fileid, stid->si_generation);

	fp = find_file(ino);
	if (!fp)
		return NULL;
	dl = find_delegation_file(fp, stid);
	put_nfs4_file(fp);
	return dl;
static inline u64
end_offset(u64 start, u64 len)
{
	u64 end;

	end = start + len;
	return end >= start ? end: NFS4_MAX_UINT64;
}

/* last octet in a range */
static inline u64
last_byte_offset(u64 start, u64 len)
{
	u64 end;

	WARN_ON_ONCE(!len);
	end = start + len;
	return end > start ? end - 1: NFS4_MAX_UINT64;
}

/*
 * TODO: Linux file offsets are _signed_ 64-bit quantities, which means that
 * we can't properly handle lock requests that go beyond the (2^63 - 1)-th
 * byte, because of sign extension problems.  Since NFSv4 calls for 64-bit
 * locking, this prevents us from being completely protocol-compliant.  The
 * real solution to this problem is to start using unsigned file offsets in
 * the VFS, but this is a very deep change!
 */
static inline void
nfs4_transform_lock_offset(struct file_lock *lock)
{
	if (lock->fl_start < 0)
		lock->fl_start = OFFSET_MAX;
	if (lock->fl_end < 0)
		lock->fl_end = OFFSET_MAX;
}

/* Hack!: For now, we're defining this just so we can use a pointer to it
 * as a unique cookie to identify our (NFSv4's) posix locks. */
static struct lock_manager_operations nfsd_posix_mng_ops  = {
static fl_owner_t
nfsd4_fl_get_owner(fl_owner_t owner)
{
	struct nfs4_lockowner *lo = (struct nfs4_lockowner *)owner;

	nfs4_get_stateowner(&lo->lo_owner);
	return owner;
}

static void
nfsd4_fl_put_owner(fl_owner_t owner)
{
	struct nfs4_lockowner *lo = (struct nfs4_lockowner *)owner;

	if (lo)
		nfs4_put_stateowner(&lo->lo_owner);
}

static const struct lock_manager_operations nfsd_posix_mng_ops  = {
	.lm_get_owner = nfsd4_fl_get_owner,
	.lm_put_owner = nfsd4_fl_put_owner,
};

static inline void
nfs4_set_lock_denied(struct file_lock *fl, struct nfsd4_lock_denied *deny)
{
	struct nfs4_stateowner *sop;
	unsigned int hval;

	if (fl->fl_lmops == &nfsd_posix_mng_ops) {
		sop = (struct nfs4_stateowner *) fl->fl_owner;
		hval = lockownerid_hashval(sop->so_id);
		kref_get(&sop->so_ref);
		deny->ld_sop = sop;
		deny->ld_clientid = sop->so_client->cl_clientid;
	} else {
		deny->ld_sop = NULL;
	struct nfs4_lockowner *lo;

	if (fl->fl_lmops == &nfsd_posix_mng_ops) {
		lo = (struct nfs4_lockowner *) fl->fl_owner;
		deny->ld_owner.data = kmemdup(lo->lo_owner.so_owner.data,
					lo->lo_owner.so_owner.len, GFP_KERNEL);
		if (!deny->ld_owner.data)
			/* We just don't care that much */
			goto nevermind;
		deny->ld_owner.len = lo->lo_owner.so_owner.len;
		deny->ld_clientid = lo->lo_owner.so_client->cl_clientid;
	} else {
nevermind:
		deny->ld_owner.len = 0;
		deny->ld_owner.data = NULL;
		deny->ld_clientid.cl_boot = 0;
		deny->ld_clientid.cl_id = 0;
	}
	deny->ld_start = fl->fl_start;
	deny->ld_length = ~(u64)0;
	if (fl->fl_end != ~(u64)0)
	deny->ld_length = NFS4_MAX_UINT64;
	if (fl->fl_end != NFS4_MAX_UINT64)
		deny->ld_length = fl->fl_end - fl->fl_start + 1;        
	deny->ld_type = NFS4_READ_LT;
	if (fl->fl_type != F_RDLCK)
		deny->ld_type = NFS4_WRITE_LT;
}

static struct nfs4_stateowner *
find_lockstateowner_str(struct inode *inode, clientid_t *clid,
		struct xdr_netobj *owner)
{
	unsigned int hashval = lock_ownerstr_hashval(inode, clid->cl_id, owner);
	struct nfs4_stateowner *op;

	list_for_each_entry(op, &lock_ownerstr_hashtbl[hashval], so_strhash) {
		if (same_owner_str(op, owner, clid))
			return op;
static struct nfs4_lockowner *
find_lockowner_str_locked(struct nfs4_client *clp, struct xdr_netobj *owner)
{
	unsigned int strhashval = ownerstr_hashval(owner);
	struct nfs4_stateowner *so;

	lockdep_assert_held(&clp->cl_lock);

	list_for_each_entry(so, &clp->cl_ownerstr_hashtbl[strhashval],
			    so_strhash) {
		if (so->so_is_open_owner)
			continue;
		if (same_owner_str(so, owner))
			return lockowner(nfs4_get_stateowner(so));
	}
	return NULL;
}

/*
 * Alloc a lock owner structure.
 * Called in nfsd4_lock - therefore, OPEN and OPEN_CONFIRM (if needed) has 
 * occured. 
 *
 * strhashval = lock_ownerstr_hashval 
 */

static struct nfs4_stateowner *
alloc_init_lock_stateowner(unsigned int strhashval, struct nfs4_client *clp, struct nfs4_stateid *open_stp, struct nfsd4_lock *lock) {
	struct nfs4_stateowner *sop;
	struct nfs4_replay *rp;
	unsigned int idhashval;

	if (!(sop = alloc_stateowner(&lock->lk_new_owner)))
		return NULL;
	idhashval = lockownerid_hashval(current_ownerid);
	INIT_LIST_HEAD(&sop->so_idhash);
	INIT_LIST_HEAD(&sop->so_strhash);
	INIT_LIST_HEAD(&sop->so_perclient);
	INIT_LIST_HEAD(&sop->so_stateids);
	INIT_LIST_HEAD(&sop->so_perstateid);
	INIT_LIST_HEAD(&sop->so_close_lru); /* not used */
	sop->so_time = 0;
	list_add(&sop->so_idhash, &lock_ownerid_hashtbl[idhashval]);
	list_add(&sop->so_strhash, &lock_ownerstr_hashtbl[strhashval]);
	list_add(&sop->so_perstateid, &open_stp->st_lockowners);
	sop->so_is_open_owner = 0;
	sop->so_id = current_ownerid++;
	sop->so_client = clp;
	/* It is the openowner seqid that will be incremented in encode in the
	 * case of new lockowners; so increment the lock seqid manually: */
	sop->so_seqid = lock->lk_new_lock_seqid + 1;
	sop->so_confirmed = 1;
	rp = &sop->so_replay;
	rp->rp_status = nfserr_serverfault;
	rp->rp_buflen = 0;
	rp->rp_buf = rp->rp_ibuf;
	return sop;
}

static struct nfs4_stateid *
alloc_init_lock_stateid(struct nfs4_stateowner *sop, struct nfs4_file *fp, struct nfs4_stateid *open_stp)
{
	struct nfs4_stateid *stp;
	unsigned int hashval = stateid_hashval(sop->so_id, fp->fi_id);

	stp = nfs4_alloc_stateid();
	if (stp == NULL)
		goto out;
	INIT_LIST_HEAD(&stp->st_hash);
	INIT_LIST_HEAD(&stp->st_perfile);
	INIT_LIST_HEAD(&stp->st_perstateowner);
	INIT_LIST_HEAD(&stp->st_lockowners); /* not used */
	list_add(&stp->st_hash, &lockstateid_hashtbl[hashval]);
	list_add(&stp->st_perfile, &fp->fi_stateids);
	list_add(&stp->st_perstateowner, &sop->so_stateids);
	stp->st_stateowner = sop;
	get_nfs4_file(fp);
	stp->st_file = fp;
	stp->st_stateid.si_boot = boot_time;
	stp->st_stateid.si_stateownerid = sop->so_id;
	stp->st_stateid.si_fileid = fp->fi_id;
	stp->st_stateid.si_generation = 0;
	stp->st_vfs_file = open_stp->st_vfs_file; /* FIXME refcount?? */
	stp->st_access_bmap = open_stp->st_access_bmap;
	stp->st_deny_bmap = open_stp->st_deny_bmap;
	stp->st_openstp = open_stp;

out:
	return stp;
static struct nfs4_lockowner *
find_lockowner_str(struct nfs4_client *clp, struct xdr_netobj *owner)
{
	struct nfs4_lockowner *lo;

	spin_lock(&clp->cl_lock);
	lo = find_lockowner_str_locked(clp, owner);
	spin_unlock(&clp->cl_lock);
	return lo;
}

static void nfs4_unhash_lockowner(struct nfs4_stateowner *sop)
{
	unhash_lockowner_locked(lockowner(sop));
}

static void nfs4_free_lockowner(struct nfs4_stateowner *sop)
{
	struct nfs4_lockowner *lo = lockowner(sop);

	kmem_cache_free(lockowner_slab, lo);
}

static const struct nfs4_stateowner_operations lockowner_ops = {
	.so_unhash =	nfs4_unhash_lockowner,
	.so_free =	nfs4_free_lockowner,
};

/*
 * Alloc a lock owner structure.
 * Called in nfsd4_lock - therefore, OPEN and OPEN_CONFIRM (if needed) has 
 * occurred. 
 *
 * strhashval = ownerstr_hashval
 */
static struct nfs4_lockowner *
alloc_init_lock_stateowner(unsigned int strhashval, struct nfs4_client *clp,
			   struct nfs4_ol_stateid *open_stp,
			   struct nfsd4_lock *lock)
{
	struct nfs4_lockowner *lo, *ret;

	lo = alloc_stateowner(lockowner_slab, &lock->lk_new_owner, clp);
	if (!lo)
		return NULL;
	INIT_LIST_HEAD(&lo->lo_owner.so_stateids);
	lo->lo_owner.so_is_open_owner = 0;
	lo->lo_owner.so_seqid = lock->lk_new_lock_seqid;
	lo->lo_owner.so_ops = &lockowner_ops;
	spin_lock(&clp->cl_lock);
	ret = find_lockowner_str_locked(clp, &lock->lk_new_owner);
	if (ret == NULL) {
		list_add(&lo->lo_owner.so_strhash,
			 &clp->cl_ownerstr_hashtbl[strhashval]);
		ret = lo;
	} else
		nfs4_free_stateowner(&lo->lo_owner);

	spin_unlock(&clp->cl_lock);
	return ret;
}

static void
init_lock_stateid(struct nfs4_ol_stateid *stp, struct nfs4_lockowner *lo,
		  struct nfs4_file *fp, struct inode *inode,
		  struct nfs4_ol_stateid *open_stp)
{
	struct nfs4_client *clp = lo->lo_owner.so_client;

	lockdep_assert_held(&clp->cl_lock);

	atomic_inc(&stp->st_stid.sc_count);
	stp->st_stid.sc_type = NFS4_LOCK_STID;
	stp->st_stateowner = nfs4_get_stateowner(&lo->lo_owner);
	get_nfs4_file(fp);
	stp->st_stid.sc_file = fp;
	stp->st_access_bmap = 0;
	stp->st_deny_bmap = open_stp->st_deny_bmap;
	stp->st_openstp = open_stp;
	mutex_init(&stp->st_mutex);
	list_add(&stp->st_locks, &open_stp->st_locks);
	list_add(&stp->st_perstateowner, &lo->lo_owner.so_stateids);
	spin_lock(&fp->fi_lock);
	list_add(&stp->st_perfile, &fp->fi_stateids);
	spin_unlock(&fp->fi_lock);
}

static struct nfs4_ol_stateid *
find_lock_stateid(struct nfs4_lockowner *lo, struct nfs4_file *fp)
{
	struct nfs4_ol_stateid *lst;
	struct nfs4_client *clp = lo->lo_owner.so_client;

	lockdep_assert_held(&clp->cl_lock);

	list_for_each_entry(lst, &lo->lo_owner.so_stateids, st_perstateowner) {
		if (lst->st_stid.sc_file == fp) {
			atomic_inc(&lst->st_stid.sc_count);
			return lst;
		}
	}
	return NULL;
}

static struct nfs4_ol_stateid *
find_or_create_lock_stateid(struct nfs4_lockowner *lo, struct nfs4_file *fi,
			    struct inode *inode, struct nfs4_ol_stateid *ost,
			    bool *new)
{
	struct nfs4_stid *ns = NULL;
	struct nfs4_ol_stateid *lst;
	struct nfs4_openowner *oo = openowner(ost->st_stateowner);
	struct nfs4_client *clp = oo->oo_owner.so_client;

	spin_lock(&clp->cl_lock);
	lst = find_lock_stateid(lo, fi);
	if (lst == NULL) {
		spin_unlock(&clp->cl_lock);
		ns = nfs4_alloc_stid(clp, stateid_slab, nfs4_free_lock_stateid);
		if (ns == NULL)
			return NULL;

		spin_lock(&clp->cl_lock);
		lst = find_lock_stateid(lo, fi);
		if (likely(!lst)) {
			lst = openlockstateid(ns);
			init_lock_stateid(lst, lo, fi, inode, ost);
			ns = NULL;
			*new = true;
		}
	}
	spin_unlock(&clp->cl_lock);
	if (ns)
		nfs4_put_stid(ns);
	return lst;
}

static int
check_lock_length(u64 offset, u64 length)
{
	return ((length == 0)  || ((length != ~(u64)0) &&
	     LOFF_OVERFLOW(offset, length)));
	return ((length == 0) || ((length != NFS4_MAX_UINT64) &&
		(length > ~offset)));
}

static void get_lock_access(struct nfs4_ol_stateid *lock_stp, u32 access)
{
	struct nfs4_file *fp = lock_stp->st_stid.sc_file;

	lockdep_assert_held(&fp->fi_lock);

	if (test_access(access, lock_stp))
		return;
	__nfs4_file_get_access(fp, access);
	set_access(access, lock_stp);
}

static __be32
lookup_or_create_lock_state(struct nfsd4_compound_state *cstate,
			    struct nfs4_ol_stateid *ost,
			    struct nfsd4_lock *lock,
			    struct nfs4_ol_stateid **plst, bool *new)
{
	__be32 status;
	struct nfs4_file *fi = ost->st_stid.sc_file;
	struct nfs4_openowner *oo = openowner(ost->st_stateowner);
	struct nfs4_client *cl = oo->oo_owner.so_client;
	struct inode *inode = d_inode(cstate->current_fh.fh_dentry);
	struct nfs4_lockowner *lo;
	struct nfs4_ol_stateid *lst;
	unsigned int strhashval;
	bool hashed;

	lo = find_lockowner_str(cl, &lock->lk_new_owner);
	if (!lo) {
		strhashval = ownerstr_hashval(&lock->lk_new_owner);
		lo = alloc_init_lock_stateowner(strhashval, cl, ost, lock);
		if (lo == NULL)
			return nfserr_jukebox;
	} else {
		/* with an existing lockowner, seqids must be the same */
		status = nfserr_bad_seqid;
		if (!cstate->minorversion &&
		    lock->lk_new_lock_seqid != lo->lo_owner.so_seqid)
			goto out;
	}

retry:
	lst = find_or_create_lock_stateid(lo, fi, inode, ost, new);
	if (lst == NULL) {
		status = nfserr_jukebox;
		goto out;
	}

	mutex_lock(&lst->st_mutex);

	/* See if it's still hashed to avoid race with FREE_STATEID */
	spin_lock(&cl->cl_lock);
	hashed = !list_empty(&lst->st_perfile);
	spin_unlock(&cl->cl_lock);

	if (!hashed) {
		mutex_unlock(&lst->st_mutex);
		nfs4_put_stid(&lst->st_stid);
		goto retry;
	}
	status = nfs_ok;
	*plst = lst;
out:
	nfs4_put_stateowner(&lo->lo_owner);
	return status;
}

/*
 *  LOCK operation 
 */
__be32
nfsd4_lock(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	   struct nfsd4_lock *lock)
{
	struct nfs4_stateowner *open_sop = NULL;
	struct nfs4_stateowner *lock_sop = NULL;
	struct nfs4_stateid *lock_stp;
	struct file *filp;
	struct file_lock file_lock;
	struct file_lock conflock;
	__be32 status = 0;
	unsigned int strhashval;
	unsigned int cmd;
	int err;
	struct nfs4_openowner *open_sop = NULL;
	struct nfs4_lockowner *lock_sop = NULL;
	struct nfs4_ol_stateid *lock_stp = NULL;
	struct nfs4_ol_stateid *open_stp = NULL;
	struct nfs4_file *fp;
	struct file *filp = NULL;
	struct file_lock *file_lock = NULL;
	struct file_lock *conflock = NULL;
	__be32 status = 0;
	int lkflg;
	int err;
	bool new = false;
	struct net *net = SVC_NET(rqstp);
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);

	dprintk("NFSD: nfsd4_lock: start=%Ld length=%Ld\n",
		(long long) lock->lk_offset,
		(long long) lock->lk_length);

	if (check_lock_length(lock->lk_offset, lock->lk_length))
		 return nfserr_inval;

	if ((status = fh_verify(rqstp, &cstate->current_fh,
				S_IFREG, NFSD_MAY_LOCK))) {
		dprintk("NFSD: nfsd4_lock: permission denied!\n");
		return status;
	}

	nfs4_lock_state();

	if (lock->lk_is_new) {
		/*
		 * Client indicates that this is a new lockowner.
		 * Use open owner and open stateid to create lock owner and
		 * lock stateid.
		 */
		struct nfs4_stateid *open_stp = NULL;
		struct nfs4_file *fp;
		
		status = nfserr_stale_clientid;
		if (STALE_CLIENTID(&lock->lk_new_clientid))
			goto out;

		/* validate and update open stateid and open seqid */
		status = nfs4_preprocess_seqid_op(&cstate->current_fh,
				        lock->lk_new_open_seqid,
		                        &lock->lk_new_open_stateid,
					OPEN_STATE,
		                        &lock->lk_replay_owner, &open_stp,
					lock);
		if (status)
			goto out;
		open_sop = lock->lk_replay_owner;
		/* create lockowner and lock stateid */
		fp = open_stp->st_file;
		strhashval = lock_ownerstr_hashval(fp->fi_inode, 
				open_sop->so_client->cl_clientid.cl_id, 
				&lock->v.new.owner);
		/* XXX: Do we need to check for duplicate stateowners on
		 * the same file, or should they just be allowed (and
		 * create new stateids)? */
		status = nfserr_resource;
		lock_sop = alloc_init_lock_stateowner(strhashval,
				open_sop->so_client, open_stp, lock);
		if (lock_sop == NULL)
			goto out;
		lock_stp = alloc_init_lock_stateid(lock_sop, fp, open_stp);
		if (lock_stp == NULL)
			goto out;
	} else {
		/* lock (lock owner + lock stateid) already exists */
		status = nfs4_preprocess_seqid_op(&cstate->current_fh,
				       lock->lk_old_lock_seqid, 
				       &lock->lk_old_lock_stateid, 
				       LOCK_STATE,
				       &lock->lk_replay_owner, &lock_stp, lock);
		if (status)
			goto out;
		lock_sop = lock->lk_replay_owner;
	}
	/* lock->lk_replay_owner and lock_stp have been created or found */
	filp = lock_stp->st_vfs_file;

	status = nfserr_grace;
	if (nfs4_in_grace() && !lock->lk_reclaim)
		goto out;
	status = nfserr_no_grace;
	if (!nfs4_in_grace() && lock->lk_reclaim)
		goto out;

	locks_init_lock(&file_lock);
	switch (lock->lk_type) {
		case NFS4_READ_LT:
		case NFS4_READW_LT:
			file_lock.fl_type = F_RDLCK;
			cmd = F_SETLK;
		break;
		case NFS4_WRITE_LT:
		case NFS4_WRITEW_LT:
			file_lock.fl_type = F_WRLCK;
			cmd = F_SETLK;
		break;
	if (lock->lk_is_new) {
		if (nfsd4_has_session(cstate))
			/* See rfc 5661 18.10.3: given clientid is ignored: */
			memcpy(&lock->lk_new_clientid,
				&cstate->session->se_client->cl_clientid,
				sizeof(clientid_t));

		status = nfserr_stale_clientid;
		if (STALE_CLIENTID(&lock->lk_new_clientid, nn))
			goto out;

		/* validate and update open stateid and open seqid */
		status = nfs4_preprocess_confirmed_seqid_op(cstate,
				        lock->lk_new_open_seqid,
		                        &lock->lk_new_open_stateid,
					&open_stp, nn);
		if (status)
			goto out;
		mutex_unlock(&open_stp->st_mutex);
		open_sop = openowner(open_stp->st_stateowner);
		status = nfserr_bad_stateid;
		if (!same_clid(&open_sop->oo_owner.so_client->cl_clientid,
						&lock->lk_new_clientid))
			goto out;
		status = lookup_or_create_lock_state(cstate, open_stp, lock,
							&lock_stp, &new);
	} else {
		status = nfs4_preprocess_seqid_op(cstate,
				       lock->lk_old_lock_seqid,
				       &lock->lk_old_lock_stateid,
				       NFS4_LOCK_STID, &lock_stp, nn);
	}
	if (status)
		goto out;
	lock_sop = lockowner(lock_stp->st_stateowner);

	lkflg = setlkflg(lock->lk_type);
	status = nfs4_check_openmode(lock_stp, lkflg);
	if (status)
		goto out;

	status = nfserr_grace;
	if (locks_in_grace(net) && !lock->lk_reclaim)
		goto out;
	status = nfserr_no_grace;
	if (!locks_in_grace(net) && lock->lk_reclaim)
		goto out;

	file_lock = locks_alloc_lock();
	if (!file_lock) {
		dprintk("NFSD: %s: unable to allocate lock!\n", __func__);
		status = nfserr_jukebox;
		goto out;
	}

	fp = lock_stp->st_stid.sc_file;
	switch (lock->lk_type) {
		case NFS4_READ_LT:
		case NFS4_READW_LT:
			spin_lock(&fp->fi_lock);
			filp = find_readable_file_locked(fp);
			if (filp)
				get_lock_access(lock_stp, NFS4_SHARE_ACCESS_READ);
			spin_unlock(&fp->fi_lock);
			file_lock->fl_type = F_RDLCK;
			break;
		case NFS4_WRITE_LT:
		case NFS4_WRITEW_LT:
			spin_lock(&fp->fi_lock);
			filp = find_writeable_file_locked(fp);
			if (filp)
				get_lock_access(lock_stp, NFS4_SHARE_ACCESS_WRITE);
			spin_unlock(&fp->fi_lock);
			file_lock->fl_type = F_WRLCK;
			break;
		default:
			status = nfserr_inval;
		goto out;
	}
	file_lock.fl_owner = (fl_owner_t)lock_sop;
	file_lock.fl_pid = current->tgid;
	file_lock.fl_file = filp;
	file_lock.fl_flags = FL_POSIX;
	file_lock.fl_lmops = &nfsd_posix_mng_ops;

	file_lock.fl_start = lock->lk_offset;
	if ((lock->lk_length == ~(u64)0) || 
			LOFF_OVERFLOW(lock->lk_offset, lock->lk_length))
		file_lock.fl_end = ~(u64)0;
	else
		file_lock.fl_end = lock->lk_offset + lock->lk_length - 1;
	nfs4_transform_lock_offset(&file_lock);

	/*
	* Try to lock the file in the VFS.
	* Note: locks.c uses the BKL to protect the inode's lock list.
	*/

	err = vfs_lock_file(filp, cmd, &file_lock, &conflock);
	switch (-err) {
	case 0: /* success! */
		update_stateid(&lock_stp->st_stateid);
		memcpy(&lock->lk_resp_stateid, &lock_stp->st_stateid, 
				sizeof(stateid_t));
	if (!filp) {
		status = nfserr_openmode;
		goto out;
	}

	file_lock->fl_owner = (fl_owner_t)lockowner(nfs4_get_stateowner(&lock_sop->lo_owner));
	file_lock->fl_pid = current->tgid;
	file_lock->fl_file = filp;
	file_lock->fl_flags = FL_POSIX;
	file_lock->fl_lmops = &nfsd_posix_mng_ops;
	file_lock->fl_start = lock->lk_offset;
	file_lock->fl_end = last_byte_offset(lock->lk_offset, lock->lk_length);
	nfs4_transform_lock_offset(file_lock);

	conflock = locks_alloc_lock();
	if (!conflock) {
		dprintk("NFSD: %s: unable to allocate lock!\n", __func__);
		status = nfserr_jukebox;
		goto out;
	}

	err = vfs_lock_file(filp, F_SETLK, file_lock, conflock);
	switch (-err) {
	case 0: /* success! */
		nfs4_inc_and_copy_stateid(&lock->lk_resp_stateid, &lock_stp->st_stid);
		status = 0;
		break;
	case (EAGAIN):		/* conflock holds conflicting lock */
		status = nfserr_denied;
		dprintk("NFSD: nfsd4_lock: conflicting lock found!\n");
		nfs4_set_lock_denied(&conflock, &lock->lk_denied);
		nfs4_set_lock_denied(conflock, &lock->lk_denied);
		break;
	case (EDEADLK):
		status = nfserr_deadlock;
		break;
	default:        
		dprintk("NFSD: nfsd4_lock: vfs_lock_file() failed! status %d\n",err);
		status = nfserr_resource;
		break;
	}
out:
	if (status && lock->lk_is_new && lock_sop)
		release_stateowner(lock_sop);
	if (lock->lk_replay_owner) {
		nfs4_get_stateowner(lock->lk_replay_owner);
		cstate->replay_owner = lock->lk_replay_owner;
	}
	nfs4_unlock_state();
	default:
		dprintk("NFSD: nfsd4_lock: vfs_lock_file() failed! status %d\n",err);
		status = nfserrno(err);
		break;
	}
out:
	if (filp)
		fput(filp);
	if (lock_stp) {
		/* Bump seqid manually if the 4.0 replay owner is openowner */
		if (cstate->replay_owner &&
		    cstate->replay_owner != &lock_sop->lo_owner &&
		    seqid_mutating_err(ntohl(status)))
			lock_sop->lo_owner.so_seqid++;

		mutex_unlock(&lock_stp->st_mutex);

		/*
		 * If this is a new, never-before-used stateid, and we are
		 * returning an error, then just go ahead and release it.
		 */
		if (status && new)
			release_lock_stateid(lock_stp);

		nfs4_put_stid(&lock_stp->st_stid);
	}
	if (open_stp)
		nfs4_put_stid(&open_stp->st_stid);
	nfsd4_bump_seqid(cstate, status);
	if (file_lock)
		locks_free_lock(file_lock);
	if (conflock)
		locks_free_lock(conflock);
	return status;
}

/*
 * The NFSv4 spec allows a client to do a LOCKT without holding an OPEN,
 * so we do a temporary open here just to get an open file to pass to
 * vfs_test_lock.  (Arguably perhaps test_lock should be done with an
 * inode operation.)
 */
static __be32 nfsd_test_lock(struct svc_rqst *rqstp, struct svc_fh *fhp, struct file_lock *lock)
{
	struct file *file;
	__be32 err = nfsd_open(rqstp, fhp, S_IFREG, NFSD_MAY_READ, &file);
	if (!err) {
		err = nfserrno(vfs_test_lock(file, lock));
		fput(file);
	}
	return err;
}

/*
 * LOCKT operation
 */
__be32
nfsd4_lockt(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	    struct nfsd4_lockt *lockt)
{
	struct inode *inode;
	struct file file;
	struct file_lock file_lock;
	int error;
	__be32 status;

	if (nfs4_in_grace())
	struct file_lock *file_lock = NULL;
	struct nfs4_lockowner *lo = NULL;
	__be32 status;
	struct nfsd_net *nn = net_generic(SVC_NET(rqstp), nfsd_net_id);

	if (locks_in_grace(SVC_NET(rqstp)))
		return nfserr_grace;

	if (check_lock_length(lockt->lt_offset, lockt->lt_length))
		 return nfserr_inval;

	lockt->lt_stateowner = NULL;
	nfs4_lock_state();

	status = nfserr_stale_clientid;
	if (STALE_CLIENTID(&lockt->lt_clientid))
		goto out;

	if ((status = fh_verify(rqstp, &cstate->current_fh, S_IFREG, 0))) {
		dprintk("NFSD: nfsd4_lockt: fh_verify() failed!\n");
		if (status == nfserr_symlink)
			status = nfserr_inval;
		goto out;
	}

	inode = cstate->current_fh.fh_dentry->d_inode;
	locks_init_lock(&file_lock);
	switch (lockt->lt_type) {
		case NFS4_READ_LT:
		case NFS4_READW_LT:
			file_lock.fl_type = F_RDLCK;
		break;
		case NFS4_WRITE_LT:
		case NFS4_WRITEW_LT:
			file_lock.fl_type = F_WRLCK;
	if (!nfsd4_has_session(cstate)) {
		status = lookup_clientid(&lockt->lt_clientid, cstate, nn);
		if (status)
			goto out;
	}

	if ((status = fh_verify(rqstp, &cstate->current_fh, S_IFREG, 0)))
		goto out;

	file_lock = locks_alloc_lock();
	if (!file_lock) {
		dprintk("NFSD: %s: unable to allocate lock!\n", __func__);
		status = nfserr_jukebox;
		goto out;
	}

	switch (lockt->lt_type) {
		case NFS4_READ_LT:
		case NFS4_READW_LT:
			file_lock->fl_type = F_RDLCK;
		break;
		case NFS4_WRITE_LT:
		case NFS4_WRITEW_LT:
			file_lock->fl_type = F_WRLCK;
		break;
		default:
			dprintk("NFSD: nfs4_lockt: bad lock type!\n");
			status = nfserr_inval;
		goto out;
	}

	lockt->lt_stateowner = find_lockstateowner_str(inode,
			&lockt->lt_clientid, &lockt->lt_owner);
	if (lockt->lt_stateowner)
		file_lock.fl_owner = (fl_owner_t)lockt->lt_stateowner;
	file_lock.fl_pid = current->tgid;
	file_lock.fl_flags = FL_POSIX;
	file_lock.fl_lmops = &nfsd_posix_mng_ops;

	file_lock.fl_start = lockt->lt_offset;
	if ((lockt->lt_length == ~(u64)0) || LOFF_OVERFLOW(lockt->lt_offset, lockt->lt_length))
		file_lock.fl_end = ~(u64)0;
	else
		file_lock.fl_end = lockt->lt_offset + lockt->lt_length - 1;

	nfs4_transform_lock_offset(&file_lock);

	/* vfs_test_lock uses the struct file _only_ to resolve the inode.
	 * since LOCKT doesn't require an OPEN, and therefore a struct
	 * file may not exist, pass vfs_test_lock a struct file with
	 * only the dentry:inode set.
	 */
	memset(&file, 0, sizeof (struct file));
	file.f_path.dentry = cstate->current_fh.fh_dentry;

	status = nfs_ok;
	error = vfs_test_lock(&file, &file_lock);
	if (error) {
		status = nfserrno(error);
		goto out;
	}
	if (file_lock.fl_type != F_UNLCK) {
		status = nfserr_denied;
		nfs4_set_lock_denied(&file_lock, &lockt->lt_denied);
	}
out:
	nfs4_unlock_state();
	lo = find_lockowner_str(cstate->clp, &lockt->lt_owner);
	if (lo)
		file_lock->fl_owner = (fl_owner_t)lo;
	file_lock->fl_pid = current->tgid;
	file_lock->fl_flags = FL_POSIX;

	file_lock->fl_start = lockt->lt_offset;
	file_lock->fl_end = last_byte_offset(lockt->lt_offset, lockt->lt_length);

	nfs4_transform_lock_offset(file_lock);

	status = nfsd_test_lock(rqstp, &cstate->current_fh, file_lock);
	if (status)
		goto out;

	if (file_lock->fl_type != F_UNLCK) {
		status = nfserr_denied;
		nfs4_set_lock_denied(file_lock, &lockt->lt_denied);
	}
out:
	if (lo)
		nfs4_put_stateowner(&lo->lo_owner);
	if (file_lock)
		locks_free_lock(file_lock);
	return status;
}

__be32
nfsd4_locku(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	    struct nfsd4_locku *locku)
{
	struct nfs4_stateid *stp;
	struct file *filp = NULL;
	struct file_lock file_lock;
	__be32 status;
	int err;
						        
	struct nfs4_ol_stateid *stp;
	struct file *filp = NULL;
	struct file_lock *file_lock = NULL;
	__be32 status;
	int err;
	struct nfsd_net *nn = net_generic(SVC_NET(rqstp), nfsd_net_id);

	dprintk("NFSD: nfsd4_locku: start=%Ld length=%Ld\n",
		(long long) locku->lu_offset,
		(long long) locku->lu_length);

	if (check_lock_length(locku->lu_offset, locku->lu_length))
		 return nfserr_inval;

	nfs4_lock_state();
									        
	if ((status = nfs4_preprocess_seqid_op(&cstate->current_fh,
					locku->lu_seqid, 
					&locku->lu_stateid, 
					LOCK_STATE,
					&locku->lu_stateowner, &stp, NULL)))
		goto out;

	filp = stp->st_vfs_file;
	BUG_ON(!filp);
	locks_init_lock(&file_lock);
	file_lock.fl_type = F_UNLCK;
	file_lock.fl_owner = (fl_owner_t) locku->lu_stateowner;
	file_lock.fl_pid = current->tgid;
	file_lock.fl_file = filp;
	file_lock.fl_flags = FL_POSIX; 
	file_lock.fl_lmops = &nfsd_posix_mng_ops;
	file_lock.fl_start = locku->lu_offset;

	if ((locku->lu_length == ~(u64)0) || LOFF_OVERFLOW(locku->lu_offset, locku->lu_length))
		file_lock.fl_end = ~(u64)0;
	else
		file_lock.fl_end = locku->lu_offset + locku->lu_length - 1;
	nfs4_transform_lock_offset(&file_lock);

	/*
	*  Try to unlock the file in the VFS.
	*/
	err = vfs_lock_file(filp, F_SETLK, &file_lock, NULL);
	status = nfs4_preprocess_seqid_op(cstate, locku->lu_seqid,
					&locku->lu_stateid, NFS4_LOCK_STID,
					&stp, nn);
	if (status)
		goto out;
	filp = find_any_file(stp->st_stid.sc_file);
	if (!filp) {
		status = nfserr_lock_range;
		goto put_stateid;
	}
	file_lock = locks_alloc_lock();
	if (!file_lock) {
		dprintk("NFSD: %s: unable to allocate lock!\n", __func__);
		status = nfserr_jukebox;
		goto fput;
	}

	file_lock->fl_type = F_UNLCK;
	file_lock->fl_owner = (fl_owner_t)lockowner(nfs4_get_stateowner(stp->st_stateowner));
	file_lock->fl_pid = current->tgid;
	file_lock->fl_file = filp;
	file_lock->fl_flags = FL_POSIX;
	file_lock->fl_lmops = &nfsd_posix_mng_ops;
	file_lock->fl_start = locku->lu_offset;

	file_lock->fl_end = last_byte_offset(locku->lu_offset,
						locku->lu_length);
	nfs4_transform_lock_offset(file_lock);

	err = vfs_lock_file(filp, F_SETLK, file_lock, NULL);
	if (err) {
		dprintk("NFSD: nfs4_locku: vfs_lock_file failed!\n");
		goto out_nfserr;
	}
	/*
	* OK, unlock succeeded; the only thing left to do is update the stateid.
	*/
	update_stateid(&stp->st_stateid);
	memcpy(&locku->lu_stateid, &stp->st_stateid, sizeof(stateid_t));

out:
	if (locku->lu_stateowner) {
		nfs4_get_stateowner(locku->lu_stateowner);
		cstate->replay_owner = locku->lu_stateowner;
	}
	nfs4_unlock_state();
	nfs4_inc_and_copy_stateid(&locku->lu_stateid, &stp->st_stid);
fput:
	fput(filp);
put_stateid:
	mutex_unlock(&stp->st_mutex);
	nfs4_put_stid(&stp->st_stid);
out:
	nfsd4_bump_seqid(cstate, status);
	if (file_lock)
		locks_free_lock(file_lock);
	return status;

out_nfserr:
	status = nfserrno(err);
	goto out;
	goto fput;
}

/*
 * returns
 * 	1: locks held by lockowner
 * 	0: no locks held by lockowner
 */
static int
check_for_locks(struct file *filp, struct nfs4_stateowner *lowner)
{
	struct file_lock **flpp;
	struct inode *inode = filp->f_path.dentry->d_inode;
	int status = 0;

	lock_kernel();
	for (flpp = &inode->i_flock; *flpp != NULL; flpp = &(*flpp)->fl_next) {
		if ((*flpp)->fl_owner == (fl_owner_t)lowner) {
			status = 1;
			goto out;
		}
	}
out:
	unlock_kernel();
 * 	true:  locks held by lockowner
 * 	false: no locks held by lockowner
 */
static bool
check_for_locks(struct nfs4_file *fp, struct nfs4_lockowner *lowner)
{
	struct file_lock *fl;
	int status = false;
	struct file *filp = find_any_file(fp);
	struct inode *inode;
	struct file_lock_context *flctx;

	if (!filp) {
		/* Any valid lock stateid should have some sort of access */
		WARN_ON_ONCE(1);
		return status;
	}

	inode = file_inode(filp);
	flctx = inode->i_flctx;

	if (flctx && !list_empty_careful(&flctx->flc_posix)) {
		spin_lock(&flctx->flc_lock);
		list_for_each_entry(fl, &flctx->flc_posix, fl_list) {
			if (fl->fl_owner == (fl_owner_t)lowner) {
				status = true;
				break;
			}
		}
		spin_unlock(&flctx->flc_lock);
	}
	fput(filp);
	return status;
}

__be32
nfsd4_release_lockowner(struct svc_rqst *rqstp,
			struct nfsd4_compound_state *cstate,
			struct nfsd4_release_lockowner *rlockowner)
{
	clientid_t *clid = &rlockowner->rl_clientid;
	struct nfs4_stateowner *sop;
	struct nfs4_stateid *stp;
	struct xdr_netobj *owner = &rlockowner->rl_owner;
	struct list_head matches;
	int i;
	__be32 status;
	struct nfs4_lockowner *lo = NULL;
	struct nfs4_ol_stateid *stp;
	struct xdr_netobj *owner = &rlockowner->rl_owner;
	unsigned int hashval = ownerstr_hashval(owner);
	__be32 status;
	struct nfsd_net *nn = net_generic(SVC_NET(rqstp), nfsd_net_id);
	struct nfs4_client *clp;
	LIST_HEAD (reaplist);

	dprintk("nfsd4_release_lockowner clientid: (%08x/%08x):\n",
		clid->cl_boot, clid->cl_id);

	/* XXX check for lease expiration */

	status = nfserr_stale_clientid;
	if (STALE_CLIENTID(clid))
		return status;

	nfs4_lock_state();

	status = nfserr_locks_held;
	/* XXX: we're doing a linear search through all the lockowners.
	 * Yipes!  For now we'll just hope clients aren't really using
	 * release_lockowner much, but eventually we have to fix these
	 * data structures. */
	INIT_LIST_HEAD(&matches);
	for (i = 0; i < LOCK_HASH_SIZE; i++) {
		list_for_each_entry(sop, &lock_ownerid_hashtbl[i], so_idhash) {
			if (!same_owner_str(sop, owner, clid))
				continue;
			list_for_each_entry(stp, &sop->so_stateids,
					st_perstateowner) {
				if (check_for_locks(stp->st_vfs_file, sop))
					goto out;
				/* Note: so_perclient unused for lockowners,
				 * so it's OK to fool with here. */
				list_add(&sop->so_perclient, &matches);
			}
		}
	}
	/* Clients probably won't expect us to return with some (but not all)
	 * of the lockowner state released; so don't release any until all
	 * have been checked. */
	status = nfs_ok;
	while (!list_empty(&matches)) {
		sop = list_entry(matches.next, struct nfs4_stateowner,
								so_perclient);
		/* unhash_stateowner deletes so_perclient only
		 * for openowners. */
		list_del(&sop->so_perclient);
		release_stateowner(sop);
	}
out:
	nfs4_unlock_state();
	status = lookup_clientid(clid, cstate, nn);
	if (status)
		return status;

	clp = cstate->clp;
	/* Find the matching lock stateowner */
	spin_lock(&clp->cl_lock);
	list_for_each_entry(sop, &clp->cl_ownerstr_hashtbl[hashval],
			    so_strhash) {

		if (sop->so_is_open_owner || !same_owner_str(sop, owner))
			continue;

		/* see if there are still any locks associated with it */
		lo = lockowner(sop);
		list_for_each_entry(stp, &sop->so_stateids, st_perstateowner) {
			if (check_for_locks(stp->st_stid.sc_file, lo)) {
				status = nfserr_locks_held;
				spin_unlock(&clp->cl_lock);
				return status;
			}
		}

		nfs4_get_stateowner(sop);
		break;
	}
	if (!lo) {
		spin_unlock(&clp->cl_lock);
		return status;
	}

	unhash_lockowner_locked(lo);
	while (!list_empty(&lo->lo_owner.so_stateids)) {
		stp = list_first_entry(&lo->lo_owner.so_stateids,
				       struct nfs4_ol_stateid,
				       st_perstateowner);
		WARN_ON(!unhash_lock_stateid(stp));
		put_ol_stateid_locked(stp, &reaplist);
	}
	spin_unlock(&clp->cl_lock);
	free_ol_stateid_reaplist(&reaplist);
	nfs4_put_stateowner(&lo->lo_owner);

	return status;
}

static inline struct nfs4_client_reclaim *
alloc_reclaim(void)
{
	return kmalloc(sizeof(struct nfs4_client_reclaim), GFP_KERNEL);
}

int
nfs4_has_reclaimed_state(const char *name)
{
	unsigned int strhashval = clientstr_hashval(name);
	struct nfs4_client *clp;

	clp = find_confirmed_client_by_str(name, strhashval);
	return clp ? 1 : 0;
bool
nfs4_has_reclaimed_state(const char *name, struct nfsd_net *nn)
{
	struct nfs4_client_reclaim *crp;

	crp = nfsd4_find_reclaim_client(name, nn);
	return (crp && crp->cr_clp);
}

/*
 * failure => all reset bets are off, nfserr_no_grace...
 */
int
nfs4_client_to_reclaim(const char *name)
{
	unsigned int strhashval;
	struct nfs4_client_reclaim *crp = NULL;

	dprintk("NFSD nfs4_client_to_reclaim NAME: %.*s\n", HEXDIR_LEN, name);
	crp = alloc_reclaim();
	if (!crp)
		return 0;
	strhashval = clientstr_hashval(name);
	INIT_LIST_HEAD(&crp->cr_strhash);
	list_add(&crp->cr_strhash, &reclaim_str_hashtbl[strhashval]);
	memcpy(crp->cr_recdir, name, HEXDIR_LEN);
	reclaim_str_hashtbl_size++;
	return 1;
}

static void
nfs4_release_reclaim(void)
struct nfs4_client_reclaim *
nfs4_client_to_reclaim(const char *name, struct nfsd_net *nn)
{
	unsigned int strhashval;
	struct nfs4_client_reclaim *crp;

	dprintk("NFSD nfs4_client_to_reclaim NAME: %.*s\n", HEXDIR_LEN, name);
	crp = alloc_reclaim();
	if (crp) {
		strhashval = clientstr_hashval(name);
		INIT_LIST_HEAD(&crp->cr_strhash);
		list_add(&crp->cr_strhash, &nn->reclaim_str_hashtbl[strhashval]);
		memcpy(crp->cr_recdir, name, HEXDIR_LEN);
		crp->cr_clp = NULL;
		nn->reclaim_str_hashtbl_size++;
	}
	return crp;
}

void
nfs4_remove_reclaim_record(struct nfs4_client_reclaim *crp, struct nfsd_net *nn)
{
	list_del(&crp->cr_strhash);
	kfree(crp);
	nn->reclaim_str_hashtbl_size--;
}

void
nfs4_release_reclaim(struct nfsd_net *nn)
{
	struct nfs4_client_reclaim *crp = NULL;
	int i;

	for (i = 0; i < CLIENT_HASH_SIZE; i++) {
		while (!list_empty(&reclaim_str_hashtbl[i])) {
			crp = list_entry(reclaim_str_hashtbl[i].next,
			                struct nfs4_client_reclaim, cr_strhash);
			list_del(&crp->cr_strhash);
			kfree(crp);
			reclaim_str_hashtbl_size--;
		}
	}
	BUG_ON(reclaim_str_hashtbl_size);
		while (!list_empty(&nn->reclaim_str_hashtbl[i])) {
			crp = list_entry(nn->reclaim_str_hashtbl[i].next,
			                struct nfs4_client_reclaim, cr_strhash);
			nfs4_remove_reclaim_record(crp, nn);
		}
	}
	WARN_ON_ONCE(nn->reclaim_str_hashtbl_size);
}

/*
 * called from OPEN, CLAIM_PREVIOUS with a new clientid. */
static struct nfs4_client_reclaim *
nfs4_find_reclaim_client(clientid_t *clid)
{
	unsigned int strhashval;
	struct nfs4_client *clp;
	struct nfs4_client_reclaim *crp = NULL;


	/* find clientid in conf_id_hashtbl */
	clp = find_confirmed_client(clid);
	if (clp == NULL)
		return NULL;

	dprintk("NFSD: nfs4_find_reclaim_client for %.*s with recdir %s\n",
		            clp->cl_name.len, clp->cl_name.data,
			    clp->cl_recdir);

	/* find clp->cl_name in reclaim_str_hashtbl */
	strhashval = clientstr_hashval(clp->cl_recdir);
	list_for_each_entry(crp, &reclaim_str_hashtbl[strhashval], cr_strhash) {
		if (same_name(crp->cr_recdir, clp->cl_recdir)) {
struct nfs4_client_reclaim *
nfsd4_find_reclaim_client(const char *recdir, struct nfsd_net *nn)
{
	unsigned int strhashval;
	struct nfs4_client_reclaim *crp = NULL;

	dprintk("NFSD: nfs4_find_reclaim_client for recdir %s\n", recdir);

	strhashval = clientstr_hashval(recdir);
	list_for_each_entry(crp, &nn->reclaim_str_hashtbl[strhashval], cr_strhash) {
		if (same_name(crp->cr_recdir, recdir)) {
			return crp;
		}
	}
	return NULL;
}

/*
* Called from OPEN. Look for clientid in reclaim list.
*/
__be32
nfs4_check_open_reclaim(clientid_t *clid)
{
	return nfs4_find_reclaim_client(clid) ? nfs_ok : nfserr_reclaim_bad;
}

/* initialization to perform at module load time: */

int
nfs4_state_init(void)
{
	int i, status;

	status = nfsd4_init_slabs();
	if (status)
		return status;
	for (i = 0; i < CLIENT_HASH_SIZE; i++) {
		INIT_LIST_HEAD(&conf_id_hashtbl[i]);
		INIT_LIST_HEAD(&conf_str_hashtbl[i]);
		INIT_LIST_HEAD(&unconf_str_hashtbl[i]);
		INIT_LIST_HEAD(&unconf_id_hashtbl[i]);
	}
	for (i = 0; i < FILE_HASH_SIZE; i++) {
		INIT_LIST_HEAD(&file_hashtbl[i]);
	}
	for (i = 0; i < OWNER_HASH_SIZE; i++) {
		INIT_LIST_HEAD(&ownerstr_hashtbl[i]);
		INIT_LIST_HEAD(&ownerid_hashtbl[i]);
	}
	for (i = 0; i < STATEID_HASH_SIZE; i++) {
		INIT_LIST_HEAD(&stateid_hashtbl[i]);
		INIT_LIST_HEAD(&lockstateid_hashtbl[i]);
	}
	for (i = 0; i < LOCK_HASH_SIZE; i++) {
		INIT_LIST_HEAD(&lock_ownerid_hashtbl[i]);
		INIT_LIST_HEAD(&lock_ownerstr_hashtbl[i]);
	}
	memset(&onestateid, ~0, sizeof(stateid_t));
	INIT_LIST_HEAD(&close_lru);
	INIT_LIST_HEAD(&client_lru);
	INIT_LIST_HEAD(&del_recall_lru);
	for (i = 0; i < CLIENT_HASH_SIZE; i++)
		INIT_LIST_HEAD(&reclaim_str_hashtbl[i]);
	reclaim_str_hashtbl_size = 0;
	return 0;
}

static void
nfsd4_load_reboot_recovery_data(void)
{
	int status;

	nfs4_lock_state();
	nfsd4_init_recdir(user_recovery_dirname);
	status = nfsd4_recdir_load();
	nfs4_unlock_state();
	if (status)
		printk("NFSD: Failure reading reboot recovery data\n");
}

unsigned long
get_nfs4_grace_period(void)
{
	return max(user_lease_time, lease_time) * HZ;
}

nfs4_check_open_reclaim(clientid_t *clid,
		struct nfsd4_compound_state *cstate,
		struct nfsd_net *nn)
{
	__be32 status;

	/* find clientid in conf_id_hashtbl */
	status = lookup_clientid(clid, cstate, nn);
	if (status)
		return nfserr_reclaim_bad;

	if (test_bit(NFSD4_CLIENT_RECLAIM_COMPLETE, &cstate->clp->cl_flags))
		return nfserr_no_grace;

	if (nfsd4_client_record_check(cstate->clp))
		return nfserr_reclaim_bad;

	return nfs_ok;
}

#ifdef CONFIG_NFSD_FAULT_INJECTION
static inline void
put_client(struct nfs4_client *clp)
{
	atomic_dec(&clp->cl_refcount);
}

static struct nfs4_client *
nfsd_find_client(struct sockaddr_storage *addr, size_t addr_size)
{
	struct nfs4_client *clp;
	struct nfsd_net *nn = net_generic(current->nsproxy->net_ns,
					  nfsd_net_id);

	if (!nfsd_netns_ready(nn))
		return NULL;

	list_for_each_entry(clp, &nn->client_lru, cl_lru) {
		if (memcmp(&clp->cl_addr, addr, addr_size) == 0)
			return clp;
	}
	return NULL;
}

u64
nfsd_inject_print_clients(void)
{
	struct nfs4_client *clp;
	u64 count = 0;
	struct nfsd_net *nn = net_generic(current->nsproxy->net_ns,
					  nfsd_net_id);
	char buf[INET6_ADDRSTRLEN];

	if (!nfsd_netns_ready(nn))
		return 0;

	spin_lock(&nn->client_lock);
	list_for_each_entry(clp, &nn->client_lru, cl_lru) {
		rpc_ntop((struct sockaddr *)&clp->cl_addr, buf, sizeof(buf));
		pr_info("NFS Client: %s\n", buf);
		++count;
	}
	spin_unlock(&nn->client_lock);

	return count;
}

u64
nfsd_inject_forget_client(struct sockaddr_storage *addr, size_t addr_size)
{
	u64 count = 0;
	struct nfs4_client *clp;
	struct nfsd_net *nn = net_generic(current->nsproxy->net_ns,
					  nfsd_net_id);

	if (!nfsd_netns_ready(nn))
		return count;

	spin_lock(&nn->client_lock);
	clp = nfsd_find_client(addr, addr_size);
	if (clp) {
		if (mark_client_expired_locked(clp) == nfs_ok)
			++count;
		else
			clp = NULL;
	}
	spin_unlock(&nn->client_lock);

	if (clp)
		expire_client(clp);

	return count;
}

u64
nfsd_inject_forget_clients(u64 max)
{
	u64 count = 0;
	struct nfs4_client *clp, *next;
	struct nfsd_net *nn = net_generic(current->nsproxy->net_ns,
						nfsd_net_id);
	LIST_HEAD(reaplist);

	if (!nfsd_netns_ready(nn))
		return count;

	spin_lock(&nn->client_lock);
	list_for_each_entry_safe(clp, next, &nn->client_lru, cl_lru) {
		if (mark_client_expired_locked(clp) == nfs_ok) {
			list_add(&clp->cl_lru, &reaplist);
			if (max != 0 && ++count >= max)
				break;
		}
	}
	spin_unlock(&nn->client_lock);

	list_for_each_entry_safe(clp, next, &reaplist, cl_lru)
		expire_client(clp);

	return count;
}

static void nfsd_print_count(struct nfs4_client *clp, unsigned int count,
			     const char *type)
{
	char buf[INET6_ADDRSTRLEN];
	rpc_ntop((struct sockaddr *)&clp->cl_addr, buf, sizeof(buf));
	printk(KERN_INFO "NFS Client: %s has %u %s\n", buf, count, type);
}

static void
nfsd_inject_add_lock_to_list(struct nfs4_ol_stateid *lst,
			     struct list_head *collect)
{
	struct nfs4_client *clp = lst->st_stid.sc_client;
	struct nfsd_net *nn = net_generic(current->nsproxy->net_ns,
					  nfsd_net_id);

	if (!collect)
		return;

	lockdep_assert_held(&nn->client_lock);
	atomic_inc(&clp->cl_refcount);
	list_add(&lst->st_locks, collect);
}

static u64 nfsd_foreach_client_lock(struct nfs4_client *clp, u64 max,
				    struct list_head *collect,
				    bool (*func)(struct nfs4_ol_stateid *))
{
	struct nfs4_openowner *oop;
	struct nfs4_ol_stateid *stp, *st_next;
	struct nfs4_ol_stateid *lst, *lst_next;
	u64 count = 0;

	spin_lock(&clp->cl_lock);
	list_for_each_entry(oop, &clp->cl_openowners, oo_perclient) {
		list_for_each_entry_safe(stp, st_next,
				&oop->oo_owner.so_stateids, st_perstateowner) {
			list_for_each_entry_safe(lst, lst_next,
					&stp->st_locks, st_locks) {
				if (func) {
					if (func(lst))
						nfsd_inject_add_lock_to_list(lst,
									collect);
				}
				++count;
				/*
				 * Despite the fact that these functions deal
				 * with 64-bit integers for "count", we must
				 * ensure that it doesn't blow up the
				 * clp->cl_refcount. Throw a warning if we
				 * start to approach INT_MAX here.
				 */
				WARN_ON_ONCE(count == (INT_MAX / 2));
				if (count == max)
					goto out;
			}
		}
	}
out:
	spin_unlock(&clp->cl_lock);

	return count;
}

static u64
nfsd_collect_client_locks(struct nfs4_client *clp, struct list_head *collect,
			  u64 max)
{
	return nfsd_foreach_client_lock(clp, max, collect, unhash_lock_stateid);
}

static u64
nfsd_print_client_locks(struct nfs4_client *clp)
{
	u64 count = nfsd_foreach_client_lock(clp, 0, NULL, NULL);
	nfsd_print_count(clp, count, "locked files");
	return count;
}

u64
nfsd_inject_print_locks(void)
{
	struct nfs4_client *clp;
	u64 count = 0;
	struct nfsd_net *nn = net_generic(current->nsproxy->net_ns,
						nfsd_net_id);

	if (!nfsd_netns_ready(nn))
		return 0;

	spin_lock(&nn->client_lock);
	list_for_each_entry(clp, &nn->client_lru, cl_lru)
		count += nfsd_print_client_locks(clp);
	spin_unlock(&nn->client_lock);

	return count;
}

static void
nfsd_reap_locks(struct list_head *reaplist)
{
	struct nfs4_client *clp;
	struct nfs4_ol_stateid *stp, *next;

	list_for_each_entry_safe(stp, next, reaplist, st_locks) {
		list_del_init(&stp->st_locks);
		clp = stp->st_stid.sc_client;
		nfs4_put_stid(&stp->st_stid);
		put_client(clp);
	}
}

u64
nfsd_inject_forget_client_locks(struct sockaddr_storage *addr, size_t addr_size)
{
	unsigned int count = 0;
	struct nfs4_client *clp;
	struct nfsd_net *nn = net_generic(current->nsproxy->net_ns,
						nfsd_net_id);
	LIST_HEAD(reaplist);

	if (!nfsd_netns_ready(nn))
		return count;

	spin_lock(&nn->client_lock);
	clp = nfsd_find_client(addr, addr_size);
	if (clp)
		count = nfsd_collect_client_locks(clp, &reaplist, 0);
	spin_unlock(&nn->client_lock);
	nfsd_reap_locks(&reaplist);
	return count;
}

u64
nfsd_inject_forget_locks(u64 max)
{
	u64 count = 0;
	struct nfs4_client *clp;
	struct nfsd_net *nn = net_generic(current->nsproxy->net_ns,
						nfsd_net_id);
	LIST_HEAD(reaplist);

	if (!nfsd_netns_ready(nn))
		return count;

	spin_lock(&nn->client_lock);
	list_for_each_entry(clp, &nn->client_lru, cl_lru) {
		count += nfsd_collect_client_locks(clp, &reaplist, max - count);
		if (max != 0 && count >= max)
			break;
	}
	spin_unlock(&nn->client_lock);
	nfsd_reap_locks(&reaplist);
	return count;
}

static u64
nfsd_foreach_client_openowner(struct nfs4_client *clp, u64 max,
			      struct list_head *collect,
			      void (*func)(struct nfs4_openowner *))
{
	struct nfs4_openowner *oop, *next;
	struct nfsd_net *nn = net_generic(current->nsproxy->net_ns,
						nfsd_net_id);
	u64 count = 0;

	lockdep_assert_held(&nn->client_lock);

	spin_lock(&clp->cl_lock);
	list_for_each_entry_safe(oop, next, &clp->cl_openowners, oo_perclient) {
		if (func) {
			func(oop);
			if (collect) {
				atomic_inc(&clp->cl_refcount);
				list_add(&oop->oo_perclient, collect);
			}
		}
		++count;
		/*
		 * Despite the fact that these functions deal with
		 * 64-bit integers for "count", we must ensure that
		 * it doesn't blow up the clp->cl_refcount. Throw a
		 * warning if we start to approach INT_MAX here.
		 */
		WARN_ON_ONCE(count == (INT_MAX / 2));
		if (count == max)
			break;
	}
	spin_unlock(&clp->cl_lock);

	return count;
}

static u64
nfsd_print_client_openowners(struct nfs4_client *clp)
{
	u64 count = nfsd_foreach_client_openowner(clp, 0, NULL, NULL);

	nfsd_print_count(clp, count, "openowners");
	return count;
}

static u64
nfsd_collect_client_openowners(struct nfs4_client *clp,
			       struct list_head *collect, u64 max)
{
	return nfsd_foreach_client_openowner(clp, max, collect,
						unhash_openowner_locked);
}

u64
nfsd_inject_print_openowners(void)
{
	struct nfs4_client *clp;
	u64 count = 0;
	struct nfsd_net *nn = net_generic(current->nsproxy->net_ns,
						nfsd_net_id);

	if (!nfsd_netns_ready(nn))
		return 0;

	spin_lock(&nn->client_lock);
	list_for_each_entry(clp, &nn->client_lru, cl_lru)
		count += nfsd_print_client_openowners(clp);
	spin_unlock(&nn->client_lock);

	return count;
}

static void
nfsd_reap_openowners(struct list_head *reaplist)
{
	struct nfs4_client *clp;
	struct nfs4_openowner *oop, *next;

	list_for_each_entry_safe(oop, next, reaplist, oo_perclient) {
		list_del_init(&oop->oo_perclient);
		clp = oop->oo_owner.so_client;
		release_openowner(oop);
		put_client(clp);
	}
}

u64
nfsd_inject_forget_client_openowners(struct sockaddr_storage *addr,
				     size_t addr_size)
{
	unsigned int count = 0;
	struct nfs4_client *clp;
	struct nfsd_net *nn = net_generic(current->nsproxy->net_ns,
						nfsd_net_id);
	LIST_HEAD(reaplist);

	if (!nfsd_netns_ready(nn))
		return count;

	spin_lock(&nn->client_lock);
	clp = nfsd_find_client(addr, addr_size);
	if (clp)
		count = nfsd_collect_client_openowners(clp, &reaplist, 0);
	spin_unlock(&nn->client_lock);
	nfsd_reap_openowners(&reaplist);
	return count;
}

u64
nfsd_inject_forget_openowners(u64 max)
{
	u64 count = 0;
	struct nfs4_client *clp;
	struct nfsd_net *nn = net_generic(current->nsproxy->net_ns,
						nfsd_net_id);
	LIST_HEAD(reaplist);

	if (!nfsd_netns_ready(nn))
		return count;

	spin_lock(&nn->client_lock);
	list_for_each_entry(clp, &nn->client_lru, cl_lru) {
		count += nfsd_collect_client_openowners(clp, &reaplist,
							max - count);
		if (max != 0 && count >= max)
			break;
	}
	spin_unlock(&nn->client_lock);
	nfsd_reap_openowners(&reaplist);
	return count;
}

static u64 nfsd_find_all_delegations(struct nfs4_client *clp, u64 max,
				     struct list_head *victims)
{
	struct nfs4_delegation *dp, *next;
	struct nfsd_net *nn = net_generic(current->nsproxy->net_ns,
						nfsd_net_id);
	u64 count = 0;

	lockdep_assert_held(&nn->client_lock);

	spin_lock(&state_lock);
	list_for_each_entry_safe(dp, next, &clp->cl_delegations, dl_perclnt) {
		if (victims) {
			/*
			 * It's not safe to mess with delegations that have a
			 * non-zero dl_time. They might have already been broken
			 * and could be processed by the laundromat outside of
			 * the state_lock. Just leave them be.
			 */
			if (dp->dl_time != 0)
				continue;

			atomic_inc(&clp->cl_refcount);
			WARN_ON(!unhash_delegation_locked(dp));
			list_add(&dp->dl_recall_lru, victims);
		}
		++count;
		/*
		 * Despite the fact that these functions deal with
		 * 64-bit integers for "count", we must ensure that
		 * it doesn't blow up the clp->cl_refcount. Throw a
		 * warning if we start to approach INT_MAX here.
		 */
		WARN_ON_ONCE(count == (INT_MAX / 2));
		if (count == max)
			break;
	}
	spin_unlock(&state_lock);
	return count;
}

static u64
nfsd_print_client_delegations(struct nfs4_client *clp)
{
	u64 count = nfsd_find_all_delegations(clp, 0, NULL);

	nfsd_print_count(clp, count, "delegations");
	return count;
}

u64
nfsd_inject_print_delegations(void)
{
	struct nfs4_client *clp;
	u64 count = 0;
	struct nfsd_net *nn = net_generic(current->nsproxy->net_ns,
						nfsd_net_id);

	if (!nfsd_netns_ready(nn))
		return 0;

	spin_lock(&nn->client_lock);
	list_for_each_entry(clp, &nn->client_lru, cl_lru)
		count += nfsd_print_client_delegations(clp);
	spin_unlock(&nn->client_lock);

	return count;
}

static void
nfsd_forget_delegations(struct list_head *reaplist)
{
	struct nfs4_client *clp;
	struct nfs4_delegation *dp, *next;

	list_for_each_entry_safe(dp, next, reaplist, dl_recall_lru) {
		list_del_init(&dp->dl_recall_lru);
		clp = dp->dl_stid.sc_client;
		revoke_delegation(dp);
		put_client(clp);
	}
}

u64
nfsd_inject_forget_client_delegations(struct sockaddr_storage *addr,
				      size_t addr_size)
{
	u64 count = 0;
	struct nfs4_client *clp;
	struct nfsd_net *nn = net_generic(current->nsproxy->net_ns,
						nfsd_net_id);
	LIST_HEAD(reaplist);

	if (!nfsd_netns_ready(nn))
		return count;

	spin_lock(&nn->client_lock);
	clp = nfsd_find_client(addr, addr_size);
	if (clp)
		count = nfsd_find_all_delegations(clp, 0, &reaplist);
	spin_unlock(&nn->client_lock);

	nfsd_forget_delegations(&reaplist);
	return count;
}

u64
nfsd_inject_forget_delegations(u64 max)
{
	u64 count = 0;
	struct nfs4_client *clp;
	struct nfsd_net *nn = net_generic(current->nsproxy->net_ns,
						nfsd_net_id);
	LIST_HEAD(reaplist);

	if (!nfsd_netns_ready(nn))
		return count;

	spin_lock(&nn->client_lock);
	list_for_each_entry(clp, &nn->client_lru, cl_lru) {
		count += nfsd_find_all_delegations(clp, max - count, &reaplist);
		if (max != 0 && count >= max)
			break;
	}
	spin_unlock(&nn->client_lock);
	nfsd_forget_delegations(&reaplist);
	return count;
}

static void
nfsd_recall_delegations(struct list_head *reaplist)
{
	struct nfs4_client *clp;
	struct nfs4_delegation *dp, *next;

	list_for_each_entry_safe(dp, next, reaplist, dl_recall_lru) {
		list_del_init(&dp->dl_recall_lru);
		clp = dp->dl_stid.sc_client;
		/*
		 * We skipped all entries that had a zero dl_time before,
		 * so we can now reset the dl_time back to 0. If a delegation
		 * break comes in now, then it won't make any difference since
		 * we're recalling it either way.
		 */
		spin_lock(&state_lock);
		dp->dl_time = 0;
		spin_unlock(&state_lock);
		nfsd_break_one_deleg(dp);
		put_client(clp);
	}
}

u64
nfsd_inject_recall_client_delegations(struct sockaddr_storage *addr,
				      size_t addr_size)
{
	u64 count = 0;
	struct nfs4_client *clp;
	struct nfsd_net *nn = net_generic(current->nsproxy->net_ns,
						nfsd_net_id);
	LIST_HEAD(reaplist);

	if (!nfsd_netns_ready(nn))
		return count;

	spin_lock(&nn->client_lock);
	clp = nfsd_find_client(addr, addr_size);
	if (clp)
		count = nfsd_find_all_delegations(clp, 0, &reaplist);
	spin_unlock(&nn->client_lock);

	nfsd_recall_delegations(&reaplist);
	return count;
}

u64
nfsd_inject_recall_delegations(u64 max)
{
	u64 count = 0;
	struct nfs4_client *clp, *next;
	struct nfsd_net *nn = net_generic(current->nsproxy->net_ns,
						nfsd_net_id);
	LIST_HEAD(reaplist);

	if (!nfsd_netns_ready(nn))
		return count;

	spin_lock(&nn->client_lock);
	list_for_each_entry_safe(clp, next, &nn->client_lru, cl_lru) {
		count += nfsd_find_all_delegations(clp, max - count, &reaplist);
		if (max != 0 && ++count >= max)
			break;
	}
	spin_unlock(&nn->client_lock);
	nfsd_recall_delegations(&reaplist);
	return count;
}
#endif /* CONFIG_NFSD_FAULT_INJECTION */

/*
 * Since the lifetime of a delegation isn't limited to that of an open, a
 * client may quite reasonably hang on to a delegation as long as it has
 * the inode cached.  This becomes an obvious problem the first time a
 * client's inode cache approaches the size of the server's total memory.
 *
 * For now we avoid this problem by imposing a hard limit on the number
 * of delegations, which varies according to the server's memory size.
 */
static void
set_max_delegations(void)
{
	/*
	 * Allow at most 4 delegations per megabyte of RAM.  Quick
	 * estimates suggest that in the worst case (where every delegation
	 * is for a different inode), a delegation could take about 1.5K,
	 * giving a worst case usage of about 6% of memory.
	 */
	max_delegations = nr_free_buffer_pages() >> (20 - 2 - PAGE_SHIFT);
}

/* initialization to perform when the nfsd service is started: */

static void
__nfs4_state_start(void)
{
	unsigned long grace_time;

	boot_time = get_seconds();
	grace_time = get_nfs_grace_period();
	lease_time = user_lease_time;
	in_grace = 1;
	printk(KERN_INFO "NFSD: starting %ld-second grace period\n",
	       grace_time/HZ);
	laundry_wq = create_singlethread_workqueue("nfsd4");
	queue_delayed_work(laundry_wq, &laundromat_work, grace_time);
	set_max_delegations();
}

void
nfs4_state_start(void)
{
	if (nfs4_init)
		return;
	nfsd4_load_reboot_recovery_data();
	__nfs4_state_start();
	nfs4_init = 1;
	return;
}

int
nfs4_in_grace(void)
{
	return in_grace;
}

time_t
nfs4_lease_time(void)
{
	return lease_time;
}

static void
__nfs4_state_shutdown(void)
{
	int i;
	struct nfs4_client *clp = NULL;
	struct nfs4_delegation *dp = NULL;
	struct list_head *pos, *next, reaplist;

	for (i = 0; i < CLIENT_HASH_SIZE; i++) {
		while (!list_empty(&conf_id_hashtbl[i])) {
			clp = list_entry(conf_id_hashtbl[i].next, struct nfs4_client, cl_idhash);
			expire_client(clp);
		}
		while (!list_empty(&unconf_str_hashtbl[i])) {
			clp = list_entry(unconf_str_hashtbl[i].next, struct nfs4_client, cl_strhash);
			expire_client(clp);
		}
	}
	INIT_LIST_HEAD(&reaplist);
	spin_lock(&recall_lock);
	list_for_each_safe(pos, next, &del_recall_lru) {
		dp = list_entry (pos, struct nfs4_delegation, dl_recall_lru);
		list_move(&dp->dl_recall_lru, &reaplist);
	}
	spin_unlock(&recall_lock);
	list_for_each_safe(pos, next, &reaplist) {
		dp = list_entry (pos, struct nfs4_delegation, dl_recall_lru);
		list_del_init(&dp->dl_recall_lru);
		unhash_delegation(dp);
	}

	nfsd4_shutdown_recdir();
	nfs4_init = 0;
static int nfs4_state_create_net(struct net *net)
{
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);
	int i;

	nn->conf_id_hashtbl = kmalloc(sizeof(struct list_head) *
			CLIENT_HASH_SIZE, GFP_KERNEL);
	if (!nn->conf_id_hashtbl)
		goto err;
	nn->unconf_id_hashtbl = kmalloc(sizeof(struct list_head) *
			CLIENT_HASH_SIZE, GFP_KERNEL);
	if (!nn->unconf_id_hashtbl)
		goto err_unconf_id;
	nn->sessionid_hashtbl = kmalloc(sizeof(struct list_head) *
			SESSION_HASH_SIZE, GFP_KERNEL);
	if (!nn->sessionid_hashtbl)
		goto err_sessionid;

	for (i = 0; i < CLIENT_HASH_SIZE; i++) {
		INIT_LIST_HEAD(&nn->conf_id_hashtbl[i]);
		INIT_LIST_HEAD(&nn->unconf_id_hashtbl[i]);
	}
	for (i = 0; i < SESSION_HASH_SIZE; i++)
		INIT_LIST_HEAD(&nn->sessionid_hashtbl[i]);
	nn->conf_name_tree = RB_ROOT;
	nn->unconf_name_tree = RB_ROOT;
	nn->boot_time = get_seconds();
	nn->grace_ended = false;
	nn->nfsd4_manager.block_opens = true;
	INIT_LIST_HEAD(&nn->nfsd4_manager.list);
	INIT_LIST_HEAD(&nn->client_lru);
	INIT_LIST_HEAD(&nn->close_lru);
	INIT_LIST_HEAD(&nn->del_recall_lru);
	spin_lock_init(&nn->client_lock);

	INIT_DELAYED_WORK(&nn->laundromat_work, laundromat_main);
	get_net(net);

	return 0;

err_sessionid:
	kfree(nn->unconf_id_hashtbl);
err_unconf_id:
	kfree(nn->conf_id_hashtbl);
err:
	return -ENOMEM;
}

static void
nfs4_state_destroy_net(struct net *net)
{
	int i;
	struct nfs4_client *clp = NULL;
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);

	for (i = 0; i < CLIENT_HASH_SIZE; i++) {
		while (!list_empty(&nn->conf_id_hashtbl[i])) {
			clp = list_entry(nn->conf_id_hashtbl[i].next, struct nfs4_client, cl_idhash);
			destroy_client(clp);
		}
	}

	for (i = 0; i < CLIENT_HASH_SIZE; i++) {
		while (!list_empty(&nn->unconf_id_hashtbl[i])) {
			clp = list_entry(nn->unconf_id_hashtbl[i].next, struct nfs4_client, cl_idhash);
			destroy_client(clp);
		}
	}

	kfree(nn->sessionid_hashtbl);
	kfree(nn->unconf_id_hashtbl);
	kfree(nn->conf_id_hashtbl);
	put_net(net);
}

int
nfs4_state_start_net(struct net *net)
{
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);
	int ret;

	ret = nfs4_state_create_net(net);
	if (ret)
		return ret;
	locks_start_grace(net, &nn->nfsd4_manager);
	nfsd4_client_tracking_init(net);
	printk(KERN_INFO "NFSD: starting %ld-second grace period (net %p)\n",
	       nn->nfsd4_grace, net);
	queue_delayed_work(laundry_wq, &nn->laundromat_work, nn->nfsd4_grace * HZ);
	return 0;
}

/* initialization to perform when the nfsd service is started: */

int
nfs4_state_start(void)
{
	int ret;

	ret = set_callback_cred();
	if (ret)
		return ret;

	laundry_wq = alloc_workqueue("%s", WQ_UNBOUND, 0, "nfsd4");
	if (laundry_wq == NULL) {
		ret = -ENOMEM;
		goto out_cleanup_cred;
	}
	ret = nfsd4_create_callback_queue();
	if (ret)
		goto out_free_laundry;

	set_max_delegations();
	return 0;

out_free_laundry:
	destroy_workqueue(laundry_wq);
out_cleanup_cred:
	cleanup_callback_cred();
	return ret;
}

void
nfs4_state_shutdown_net(struct net *net)
{
	struct nfs4_delegation *dp = NULL;
	struct list_head *pos, *next, reaplist;
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);

	cancel_delayed_work_sync(&nn->laundromat_work);
	locks_end_grace(&nn->nfsd4_manager);

	INIT_LIST_HEAD(&reaplist);
	spin_lock(&state_lock);
	list_for_each_safe(pos, next, &nn->del_recall_lru) {
		dp = list_entry (pos, struct nfs4_delegation, dl_recall_lru);
		WARN_ON(!unhash_delegation_locked(dp));
		list_add(&dp->dl_recall_lru, &reaplist);
	}
	spin_unlock(&state_lock);
	list_for_each_safe(pos, next, &reaplist) {
		dp = list_entry (pos, struct nfs4_delegation, dl_recall_lru);
		list_del_init(&dp->dl_recall_lru);
		put_clnt_odstate(dp->dl_clnt_odstate);
		nfs4_put_deleg_lease(dp->dl_stid.sc_file);
		nfs4_put_stid(&dp->dl_stid);
	}

	nfsd4_client_tracking_exit(net);
	nfs4_state_destroy_net(net);
}

void
nfs4_state_shutdown(void)
{
	cancel_rearming_delayed_workqueue(laundry_wq, &laundromat_work);
	destroy_workqueue(laundry_wq);
	nfs4_lock_state();
	nfs4_release_reclaim();
	__nfs4_state_shutdown();
	nfs4_unlock_state();
}

/*
 * user_recovery_dirname is protected by the nfsd_mutex since it's only
 * accessed when nfsd is starting.
 */
static void
nfs4_set_recdir(char *recdir)
{
	strcpy(user_recovery_dirname, recdir);
}

/*
 * Change the NFSv4 recovery directory to recdir.
 */
int
nfs4_reset_recoverydir(char *recdir)
{
	int status;
	struct nameidata nd;

	status = path_lookup(recdir, LOOKUP_FOLLOW, &nd);
	if (status)
		return status;
	status = -ENOTDIR;
	if (S_ISDIR(nd.path.dentry->d_inode->i_mode)) {
		nfs4_set_recdir(recdir);
		status = 0;
	}
	path_put(&nd.path);
	return status;
}

char *
nfs4_recoverydir(void)
{
	return user_recovery_dirname;
}

/*
 * Called when leasetime is changed.
 *
 * The only way the protocol gives us to handle on-the-fly lease changes is to
 * simulate a reboot.  Instead of doing that, we just wait till the next time
 * we start to register any changes in lease time.  If the administrator
 * really wants to change the lease time *now*, they can go ahead and bring
 * nfsd down and then back up again after changing the lease time.
 *
 * user_lease_time is protected by nfsd_mutex since it's only really accessed
 * when nfsd is starting
 */
void
nfs4_reset_lease(time_t leasetime)
{
	user_lease_time = leasetime;
	destroy_workqueue(laundry_wq);
	nfsd4_destroy_callback_queue();
	cleanup_callback_cred();
}

static void
get_stateid(struct nfsd4_compound_state *cstate, stateid_t *stateid)
{
	if (HAS_STATE_ID(cstate, CURRENT_STATE_ID_FLAG) && CURRENT_STATEID(stateid))
		memcpy(stateid, &cstate->current_stateid, sizeof(stateid_t));
}

static void
put_stateid(struct nfsd4_compound_state *cstate, stateid_t *stateid)
{
	if (cstate->minorversion) {
		memcpy(&cstate->current_stateid, stateid, sizeof(stateid_t));
		SET_STATE_ID(cstate, CURRENT_STATE_ID_FLAG);
	}
}

void
clear_current_stateid(struct nfsd4_compound_state *cstate)
{
	CLEAR_STATE_ID(cstate, CURRENT_STATE_ID_FLAG);
}

/*
 * functions to set current state id
 */
void
nfsd4_set_opendowngradestateid(struct nfsd4_compound_state *cstate, struct nfsd4_open_downgrade *odp)
{
	put_stateid(cstate, &odp->od_stateid);
}

void
nfsd4_set_openstateid(struct nfsd4_compound_state *cstate, struct nfsd4_open *open)
{
	put_stateid(cstate, &open->op_stateid);
}

void
nfsd4_set_closestateid(struct nfsd4_compound_state *cstate, struct nfsd4_close *close)
{
	put_stateid(cstate, &close->cl_stateid);
}

void
nfsd4_set_lockstateid(struct nfsd4_compound_state *cstate, struct nfsd4_lock *lock)
{
	put_stateid(cstate, &lock->lk_resp_stateid);
}

/*
 * functions to consume current state id
 */

void
nfsd4_get_opendowngradestateid(struct nfsd4_compound_state *cstate, struct nfsd4_open_downgrade *odp)
{
	get_stateid(cstate, &odp->od_stateid);
}

void
nfsd4_get_delegreturnstateid(struct nfsd4_compound_state *cstate, struct nfsd4_delegreturn *drp)
{
	get_stateid(cstate, &drp->dr_stateid);
}

void
nfsd4_get_freestateid(struct nfsd4_compound_state *cstate, struct nfsd4_free_stateid *fsp)
{
	get_stateid(cstate, &fsp->fr_stateid);
}

void
nfsd4_get_setattrstateid(struct nfsd4_compound_state *cstate, struct nfsd4_setattr *setattr)
{
	get_stateid(cstate, &setattr->sa_stateid);
}

void
nfsd4_get_closestateid(struct nfsd4_compound_state *cstate, struct nfsd4_close *close)
{
	get_stateid(cstate, &close->cl_stateid);
}

void
nfsd4_get_lockustateid(struct nfsd4_compound_state *cstate, struct nfsd4_locku *locku)
{
	get_stateid(cstate, &locku->lu_stateid);
}

void
nfsd4_get_readstateid(struct nfsd4_compound_state *cstate, struct nfsd4_read *read)
{
	get_stateid(cstate, &read->rd_stateid);
}

void
nfsd4_get_writestateid(struct nfsd4_compound_state *cstate, struct nfsd4_write *write)
{
	get_stateid(cstate, &write->wr_stateid);
}
