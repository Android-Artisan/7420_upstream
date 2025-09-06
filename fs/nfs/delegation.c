/*
 * linux/fs/nfs/delegation.c
 *
 * Copyright (C) 2004 Trond Myklebust
 *
 * NFS file delegation management
 *
 */
#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include <linux/nfs4.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_xdr.h>

#include "nfs4_fs.h"
#include "delegation.h"
#include "internal.h"

static void nfs_do_free_delegation(struct nfs_delegation *delegation)
{
	kfree(delegation);
}

static void nfs_free_delegation_callback(struct rcu_head *head)
{
	struct nfs_delegation *delegation = container_of(head, struct nfs_delegation, rcu);

	nfs_do_free_delegation(delegation);
}

static void nfs_free_delegation(struct nfs_delegation *delegation)
{
	struct rpc_cred *cred;

	cred = rcu_dereference(delegation->cred);
	rcu_assign_pointer(delegation->cred, NULL);
	call_rcu(&delegation->rcu, nfs_free_delegation_callback);
	if (cred)
		put_rpccred(cred);
}

static int nfs_delegation_claim_locks(struct nfs_open_context *ctx, struct nfs4_state *state)
{
	struct inode *inode = state->inode;
	struct file_lock *fl;
	int status;

	for (fl = inode->i_flock; fl != NULL; fl = fl->fl_next) {
		if (!(fl->fl_flags & (FL_POSIX|FL_FLOCK)))
			continue;
		if (nfs_file_open_context(fl->fl_file) != ctx)
			continue;
		status = nfs4_lock_delegation_recall(state, fl);
		if (status >= 0)
			continue;
		switch (status) {
			default:
				printk(KERN_ERR "%s: unhandled error %d.\n",
						__func__, status);
			case -NFS4ERR_EXPIRED:
				/* kill_proc(fl->fl_pid, SIGLOST, 1); */
			case -NFS4ERR_STALE_CLIENTID:
				nfs4_schedule_state_recovery(NFS_SERVER(inode)->nfs_client);
				goto out_err;
		}
	}
	return 0;
out_err:
	return status;
}

static void nfs_delegation_claim_opens(struct inode *inode, const nfs4_stateid *stateid)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_open_context *ctx;
	struct nfs4_state *state;
#include "nfs4trace.h"

static void nfs_free_delegation(struct nfs_delegation *delegation)
{
	if (delegation->cred) {
		put_rpccred(delegation->cred);
		delegation->cred = NULL;
	}
	kfree_rcu(delegation, rcu);
}

/**
 * nfs_mark_delegation_referenced - set delegation's REFERENCED flag
 * @delegation: delegation to process
 *
 */
void nfs_mark_delegation_referenced(struct nfs_delegation *delegation)
{
	set_bit(NFS_DELEGATION_REFERENCED, &delegation->flags);
}

static bool
nfs4_is_valid_delegation(const struct nfs_delegation *delegation,
		fmode_t flags)
{
	if (delegation != NULL && (delegation->type & flags) == flags &&
	    !test_bit(NFS_DELEGATION_REVOKED, &delegation->flags) &&
	    !test_bit(NFS_DELEGATION_RETURNING, &delegation->flags))
		return true;
	return false;
}

struct nfs_delegation *nfs4_get_valid_delegation(const struct inode *inode)
{
	struct nfs_delegation *delegation;

	delegation = rcu_dereference(NFS_I(inode)->delegation);
	if (nfs4_is_valid_delegation(delegation, 0))
		return delegation;
	return NULL;
}

static int
nfs4_do_check_delegation(struct inode *inode, fmode_t flags, bool mark)
{
	struct nfs_delegation *delegation;
	int ret = 0;

	flags &= FMODE_READ|FMODE_WRITE;
	rcu_read_lock();
	delegation = rcu_dereference(NFS_I(inode)->delegation);
	if (nfs4_is_valid_delegation(delegation, flags)) {
		if (mark)
			nfs_mark_delegation_referenced(delegation);
		ret = 1;
	}
	rcu_read_unlock();
	return ret;
}
/**
 * nfs_have_delegation - check if inode has a delegation, mark it
 * NFS_DELEGATION_REFERENCED if there is one.
 * @inode: inode to check
 * @flags: delegation types to check for
 *
 * Returns one if inode has the indicated delegation, otherwise zero.
 */
int nfs4_have_delegation(struct inode *inode, fmode_t flags)
{
	return nfs4_do_check_delegation(inode, flags, true);
}

/*
 * nfs4_check_delegation - check if inode has a delegation, do not mark
 * NFS_DELEGATION_REFERENCED if it has one.
 */
int nfs4_check_delegation(struct inode *inode, fmode_t flags)
{
	return nfs4_do_check_delegation(inode, flags, false);
}

static int nfs_delegation_claim_locks(struct nfs4_state *state, const nfs4_stateid *stateid)
{
	struct inode *inode = state->inode;
	struct file_lock *fl;
	struct file_lock_context *flctx = inode->i_flctx;
	struct list_head *list;
	int status = 0;

	if (flctx == NULL)
		goto out;

	list = &flctx->flc_posix;
	spin_lock(&flctx->flc_lock);
restart:
	list_for_each_entry(fl, list, fl_list) {
		if (nfs_file_open_context(fl->fl_file)->state != state)
			continue;
		spin_unlock(&flctx->flc_lock);
		status = nfs4_lock_delegation_recall(fl, state, stateid);
		if (status < 0)
			goto out;
		spin_lock(&flctx->flc_lock);
	}
	if (list == &flctx->flc_posix) {
		list = &flctx->flc_flock;
		goto restart;
	}
	spin_unlock(&flctx->flc_lock);
out:
	return status;
}

static int nfs_delegation_claim_opens(struct inode *inode,
		const nfs4_stateid *stateid, fmode_t type)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_open_context *ctx;
	struct nfs4_state_owner *sp;
	struct nfs4_state *state;
	unsigned int seq;
	int err;

again:
	spin_lock(&inode->i_lock);
	list_for_each_entry(ctx, &nfsi->open_files, list) {
		state = ctx->state;
		if (state == NULL)
			continue;
		if (!test_bit(NFS_DELEGATED_STATE, &state->flags))
			continue;
		if (memcmp(state->stateid.data, stateid->data, sizeof(state->stateid.data)) != 0)
			continue;
		get_nfs_open_context(ctx);
		spin_unlock(&inode->i_lock);
		err = nfs4_open_delegation_recall(ctx, state, stateid);
		if (err >= 0)
			err = nfs_delegation_claim_locks(ctx, state);
		put_nfs_open_context(ctx);
		if (err != 0)
			return;
		goto again;
	}
	spin_unlock(&inode->i_lock);
}

/*
 * Set up a delegation on an inode
 */
void nfs_inode_reclaim_delegation(struct inode *inode, struct rpc_cred *cred, struct nfs_openres *res)
{
	struct nfs_delegation *delegation = NFS_I(inode)->delegation;
	struct rpc_cred *oldcred;

	if (delegation == NULL)
		return;
	memcpy(delegation->stateid.data, res->delegation.data,
			sizeof(delegation->stateid.data));
	delegation->type = res->delegation_type;
	delegation->maxsize = res->maxsize;
	oldcred = delegation->cred;
	delegation->cred = get_rpccred(cred);
	delegation->flags &= ~NFS_DELEGATION_NEED_RECLAIM;
	NFS_I(inode)->delegation_state = delegation->type;
	smp_wmb();
	put_rpccred(oldcred);
		if (!nfs4_valid_open_stateid(state))
			continue;
		if (!nfs4_stateid_match(&state->stateid, stateid))
			continue;
		get_nfs_open_context(ctx);
		spin_unlock(&inode->i_lock);
		sp = state->owner;
		/* Block nfs4_proc_unlck */
		mutex_lock(&sp->so_delegreturn_mutex);
		seq = raw_seqcount_begin(&sp->so_reclaim_seqcount);
		err = nfs4_open_delegation_recall(ctx, state, stateid, type);
		if (!err)
			err = nfs_delegation_claim_locks(state, stateid);
		if (!err && read_seqcount_retry(&sp->so_reclaim_seqcount, seq))
			err = -EAGAIN;
		mutex_unlock(&sp->so_delegreturn_mutex);
		put_nfs_open_context(ctx);
		if (err != 0)
			return err;
		goto again;
	}
	spin_unlock(&inode->i_lock);
	return 0;
}

/**
 * nfs_inode_reclaim_delegation - process a delegation reclaim request
 * @inode: inode to process
 * @cred: credential to use for request
 * @res: new delegation state from server
 *
 */
void nfs_inode_reclaim_delegation(struct inode *inode, struct rpc_cred *cred,
				  struct nfs_openres *res)
{
	struct nfs_delegation *delegation;
	struct rpc_cred *oldcred = NULL;

	rcu_read_lock();
	delegation = rcu_dereference(NFS_I(inode)->delegation);
	if (delegation != NULL) {
		spin_lock(&delegation->lock);
		if (delegation->inode != NULL) {
			nfs4_stateid_copy(&delegation->stateid, &res->delegation);
			delegation->type = res->delegation_type;
			delegation->pagemod_limit = res->pagemod_limit;
			oldcred = delegation->cred;
			delegation->cred = get_rpccred(cred);
			clear_bit(NFS_DELEGATION_NEED_RECLAIM,
				  &delegation->flags);
			spin_unlock(&delegation->lock);
			rcu_read_unlock();
			put_rpccred(oldcred);
			trace_nfs4_reclaim_delegation(inode, res->delegation_type);
		} else {
			/* We appear to have raced with a delegation return. */
			spin_unlock(&delegation->lock);
			rcu_read_unlock();
			nfs_inode_set_delegation(inode, cred, res);
		}
	} else {
		rcu_read_unlock();
	}
}

static int nfs_do_return_delegation(struct inode *inode, struct nfs_delegation *delegation, int issync)
{
	int res = 0;

	res = nfs4_proc_delegreturn(inode, delegation->cred, &delegation->stateid, issync);
	if (!test_bit(NFS_DELEGATION_REVOKED, &delegation->flags))
		res = nfs4_proc_delegreturn(inode,
				delegation->cred,
				&delegation->stateid,
				issync);
	nfs_free_delegation(delegation);
	return res;
}

static struct nfs_delegation *nfs_detach_delegation_locked(struct nfs_inode *nfsi, const nfs4_stateid *stateid)
{
	struct nfs_delegation *delegation = rcu_dereference(nfsi->delegation);

	if (delegation == NULL)
		goto nomatch;
	if (stateid != NULL && memcmp(delegation->stateid.data, stateid->data,
				sizeof(delegation->stateid.data)) != 0)
		goto nomatch;
	list_del_rcu(&delegation->super_list);
	nfsi->delegation_state = 0;
	rcu_assign_pointer(nfsi->delegation, NULL);
	return delegation;
nomatch:
	return NULL;
}

/*
 * Set up a delegation on an inode
 */
int nfs_inode_set_delegation(struct inode *inode, struct rpc_cred *cred, struct nfs_openres *res)
{
	struct nfs_client *clp = NFS_SERVER(inode)->nfs_client;
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_delegation *delegation;
	struct nfs_delegation *freeme = NULL;
	int status = 0;

	delegation = kmalloc(sizeof(*delegation), GFP_KERNEL);
	if (delegation == NULL)
		return -ENOMEM;
	memcpy(delegation->stateid.data, res->delegation.data,
			sizeof(delegation->stateid.data));
	delegation->type = res->delegation_type;
	delegation->maxsize = res->maxsize;
	delegation->change_attr = nfsi->change_attr;
	delegation->cred = get_rpccred(cred);
	delegation->inode = inode;

	spin_lock(&clp->cl_lock);
	if (rcu_dereference(nfsi->delegation) != NULL) {
		if (memcmp(&delegation->stateid, &nfsi->delegation->stateid,
					sizeof(delegation->stateid)) == 0 &&
				delegation->type == nfsi->delegation->type) {
static struct inode *nfs_delegation_grab_inode(struct nfs_delegation *delegation)
{
	struct inode *inode = NULL;

	spin_lock(&delegation->lock);
	if (delegation->inode != NULL)
		inode = igrab(delegation->inode);
	spin_unlock(&delegation->lock);
	return inode;
}

static struct nfs_delegation *
nfs_start_delegation_return_locked(struct nfs_inode *nfsi)
{
	struct nfs_delegation *ret = NULL;
	struct nfs_delegation *delegation = rcu_dereference(nfsi->delegation);

	if (delegation == NULL)
		goto out;
	spin_lock(&delegation->lock);
	if (!test_and_set_bit(NFS_DELEGATION_RETURNING, &delegation->flags))
		ret = delegation;
	spin_unlock(&delegation->lock);
out:
	return ret;
}

static struct nfs_delegation *
nfs_start_delegation_return(struct nfs_inode *nfsi)
{
	struct nfs_delegation *delegation;

	rcu_read_lock();
	delegation = nfs_start_delegation_return_locked(nfsi);
	rcu_read_unlock();
	return delegation;
}

static void
nfs_abort_delegation_return(struct nfs_delegation *delegation,
		struct nfs_client *clp)
{

	spin_lock(&delegation->lock);
	clear_bit(NFS_DELEGATION_RETURNING, &delegation->flags);
	set_bit(NFS_DELEGATION_RETURN, &delegation->flags);
	spin_unlock(&delegation->lock);
	set_bit(NFS4CLNT_DELEGRETURN, &clp->cl_state);
}

static struct nfs_delegation *
nfs_detach_delegation_locked(struct nfs_inode *nfsi,
		struct nfs_delegation *delegation,
		struct nfs_client *clp)
{
	struct nfs_delegation *deleg_cur =
		rcu_dereference_protected(nfsi->delegation,
				lockdep_is_held(&clp->cl_lock));

	if (deleg_cur == NULL || delegation != deleg_cur)
		return NULL;

	spin_lock(&delegation->lock);
	set_bit(NFS_DELEGATION_RETURNING, &delegation->flags);
	list_del_rcu(&delegation->super_list);
	delegation->inode = NULL;
	rcu_assign_pointer(nfsi->delegation, NULL);
	spin_unlock(&delegation->lock);
	return delegation;
}

static struct nfs_delegation *nfs_detach_delegation(struct nfs_inode *nfsi,
		struct nfs_delegation *delegation,
		struct nfs_server *server)
{
	struct nfs_client *clp = server->nfs_client;

	spin_lock(&clp->cl_lock);
	delegation = nfs_detach_delegation_locked(nfsi, delegation, clp);
	spin_unlock(&clp->cl_lock);
	return delegation;
}

static struct nfs_delegation *
nfs_inode_detach_delegation(struct inode *inode)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs_delegation *delegation;

	delegation = nfs_start_delegation_return(nfsi);
	if (delegation == NULL)
		return NULL;
	return nfs_detach_delegation(nfsi, delegation, server);
}

static void
nfs_update_inplace_delegation(struct nfs_delegation *delegation,
		const struct nfs_delegation *update)
{
	if (nfs4_stateid_is_newer(&update->stateid, &delegation->stateid)) {
		delegation->stateid.seqid = update->stateid.seqid;
		smp_wmb();
		delegation->type = update->type;
	}
}

/**
 * nfs_inode_set_delegation - set up a delegation on an inode
 * @inode: inode to which delegation applies
 * @cred: cred to use for subsequent delegation processing
 * @res: new delegation state from server
 *
 * Returns zero on success, or a negative errno value.
 */
int nfs_inode_set_delegation(struct inode *inode, struct rpc_cred *cred, struct nfs_openres *res)
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs_client *clp = server->nfs_client;
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_delegation *delegation, *old_delegation;
	struct nfs_delegation *freeme = NULL;
	int status = 0;

	delegation = kmalloc(sizeof(*delegation), GFP_NOFS);
	if (delegation == NULL)
		return -ENOMEM;
	nfs4_stateid_copy(&delegation->stateid, &res->delegation);
	delegation->type = res->delegation_type;
	delegation->pagemod_limit = res->pagemod_limit;
	delegation->change_attr = inode->i_version;
	delegation->cred = get_rpccred(cred);
	delegation->inode = inode;
	delegation->flags = 1<<NFS_DELEGATION_REFERENCED;
	spin_lock_init(&delegation->lock);

	spin_lock(&clp->cl_lock);
	old_delegation = rcu_dereference_protected(nfsi->delegation,
					lockdep_is_held(&clp->cl_lock));
	if (old_delegation != NULL) {
		/* Is this an update of the existing delegation? */
		if (nfs4_stateid_match_other(&old_delegation->stateid,
					&delegation->stateid)) {
			nfs_update_inplace_delegation(old_delegation,
					delegation);
			goto out;
		}
		/*
		 * Deal with broken servers that hand out two
		 * delegations for the same file.
		 * Allow for upgrades to a WRITE delegation, but
		 * nothing else.
		 */
		dfprintk(FILE, "%s: server %s handed out "
				"a duplicate delegation!\n",
				__func__, clp->cl_hostname);
		if (delegation->type <= nfsi->delegation->type) {
		if (delegation->type == old_delegation->type ||
		    !(delegation->type & FMODE_WRITE)) {
			freeme = delegation;
			delegation = NULL;
			goto out;
		}
		freeme = nfs_detach_delegation_locked(nfsi, NULL);
	}
	list_add_rcu(&delegation->super_list, &clp->cl_delegations);
	nfsi->delegation_state = delegation->type;
		if (test_and_set_bit(NFS_DELEGATION_RETURNING,
					&old_delegation->flags))
			goto out;
		freeme = nfs_detach_delegation_locked(nfsi,
				old_delegation, clp);
		if (freeme == NULL)
			goto out;
	}
	list_add_tail_rcu(&delegation->super_list, &server->delegations);
	rcu_assign_pointer(nfsi->delegation, delegation);
	delegation = NULL;

	/* Ensure we revalidate the attributes and page cache! */
	spin_lock(&inode->i_lock);
	nfsi->cache_validity |= NFS_INO_REVAL_FORCED;
	spin_unlock(&inode->i_lock);
	trace_nfs4_set_delegation(inode, res->delegation_type);

out:
	spin_unlock(&clp->cl_lock);
	if (delegation != NULL)
		nfs_free_delegation(delegation);
	if (freeme != NULL)
		nfs_do_return_delegation(inode, freeme, 0);
	return status;
}

/* Sync all data to disk upon delegation return */
static void nfs_msync_inode(struct inode *inode)
{
	filemap_fdatawrite(inode->i_mapping);
	nfs_wb_all(inode);
	filemap_fdatawait(inode->i_mapping);
}

/*
 * Basic procedure for returning a delegation to the server
 */
static int __nfs_inode_return_delegation(struct inode *inode, struct nfs_delegation *delegation)
{
	struct nfs_client *clp = NFS_SERVER(inode)->nfs_client;
	struct nfs_inode *nfsi = NFS_I(inode);

	nfs_msync_inode(inode);
	down_read(&clp->cl_sem);
	/* Guard against new delegated open calls */
	down_write(&nfsi->rwsem);
	nfs_delegation_claim_opens(inode, &delegation->stateid);
	up_write(&nfsi->rwsem);
	up_read(&clp->cl_sem);
	nfs_msync_inode(inode);

	return nfs_do_return_delegation(inode, delegation, 1);
}

/*
 * This function returns the delegation without reclaiming opens
 * or protecting against delegation reclaims.
 * It is therefore really only safe to be called from
 * nfs4_clear_inode()
 */
void nfs_inode_return_delegation_noreclaim(struct inode *inode)
{
	struct nfs_client *clp = NFS_SERVER(inode)->nfs_client;
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_delegation *delegation;

	if (rcu_dereference(nfsi->delegation) != NULL) {
		spin_lock(&clp->cl_lock);
		delegation = nfs_detach_delegation_locked(nfsi, NULL);
		spin_unlock(&clp->cl_lock);
		if (delegation != NULL)
			nfs_do_return_delegation(inode, delegation, 0);
	}
}

int nfs_inode_return_delegation(struct inode *inode)
{
	struct nfs_client *clp = NFS_SERVER(inode)->nfs_client;
/*
 * Basic procedure for returning a delegation to the server
 */
static int nfs_end_delegation_return(struct inode *inode, struct nfs_delegation *delegation, int issync)
{
	struct nfs_client *clp = NFS_SERVER(inode)->nfs_client;
	struct nfs_inode *nfsi = NFS_I(inode);
	int err = 0;

	if (delegation == NULL)
		return 0;
	do {
		if (test_bit(NFS_DELEGATION_REVOKED, &delegation->flags))
			break;
		err = nfs_delegation_claim_opens(inode, &delegation->stateid,
				delegation->type);
		if (!issync || err != -EAGAIN)
			break;
		/*
		 * Guard against state recovery
		 */
		err = nfs4_wait_clnt_recover(clp);
	} while (err == 0);

	if (err) {
		nfs_abort_delegation_return(delegation, clp);
		goto out;
	}
	if (!nfs_detach_delegation(nfsi, delegation, NFS_SERVER(inode)))
		goto out;

	err = nfs_do_return_delegation(inode, delegation, issync);
out:
	return err;
}

static bool nfs_delegation_need_return(struct nfs_delegation *delegation)
{
	bool ret = false;

	if (test_bit(NFS_DELEGATION_RETURNING, &delegation->flags))
		goto out;
	if (test_and_clear_bit(NFS_DELEGATION_RETURN, &delegation->flags))
		ret = true;
	if (test_and_clear_bit(NFS_DELEGATION_RETURN_IF_CLOSED, &delegation->flags) && !ret) {
		struct inode *inode;

		spin_lock(&delegation->lock);
		inode = delegation->inode;
		if (inode && list_empty(&NFS_I(inode)->open_files))
			ret = true;
		spin_unlock(&delegation->lock);
	}
out:
	return ret;
}

/**
 * nfs_client_return_marked_delegations - return previously marked delegations
 * @clp: nfs_client to process
 *
 * Note that this function is designed to be called by the state
 * manager thread. For this reason, it cannot flush the dirty data,
 * since that could deadlock in case of a state recovery error.
 *
 * Returns zero on success, or a negative errno value.
 */
int nfs_client_return_marked_delegations(struct nfs_client *clp)
{
	struct nfs_delegation *delegation;
	struct nfs_server *server;
	struct inode *inode;
	int err = 0;

restart:
	rcu_read_lock();
	list_for_each_entry_rcu(server, &clp->cl_superblocks, client_link) {
		list_for_each_entry_rcu(delegation, &server->delegations,
								super_list) {
			if (!nfs_delegation_need_return(delegation))
				continue;
			if (!nfs_sb_active(server->super))
				continue;
			inode = nfs_delegation_grab_inode(delegation);
			if (inode == NULL) {
				rcu_read_unlock();
				nfs_sb_deactive(server->super);
				goto restart;
			}
			delegation = nfs_start_delegation_return_locked(NFS_I(inode));
			rcu_read_unlock();

			err = nfs_end_delegation_return(inode, delegation, 0);
			iput(inode);
			nfs_sb_deactive(server->super);
			if (!err)
				goto restart;
			set_bit(NFS4CLNT_DELEGRETURN, &clp->cl_state);
			return err;
		}
	}
	rcu_read_unlock();
	return 0;
}

/**
 * nfs_inode_return_delegation_noreclaim - return delegation, don't reclaim opens
 * @inode: inode to process
 *
 * Does not protect against delegation reclaims, therefore really only safe
 * to be called from nfs4_clear_inode().
 */
void nfs_inode_return_delegation_noreclaim(struct inode *inode)
{
	struct nfs_delegation *delegation;

	delegation = nfs_inode_detach_delegation(inode);
	if (delegation != NULL)
		nfs_do_return_delegation(inode, delegation, 1);
}

/**
 * nfs_inode_return_delegation - synchronously return a delegation
 * @inode: inode to process
 *
 * This routine will always flush any dirty data to disk on the
 * assumption that if we need to return the delegation, then
 * we should stop caching.
 *
 * Returns zero on success, or a negative errno value.
 */
int nfs4_inode_return_delegation(struct inode *inode)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_delegation *delegation;
	int err = 0;

	if (rcu_dereference(nfsi->delegation) != NULL) {
		spin_lock(&clp->cl_lock);
		delegation = nfs_detach_delegation_locked(nfsi, NULL);
		spin_unlock(&clp->cl_lock);
		if (delegation != NULL)
			err = __nfs_inode_return_delegation(inode, delegation);
	}
	return err;
}

/*
 * Return all delegations associated to a super block
 */
void nfs_return_all_delegations(struct super_block *sb)
{
	struct nfs_client *clp = NFS_SB(sb)->nfs_client;
	struct nfs_delegation *delegation;
	struct inode *inode;

	if (clp == NULL)
		return;
restart:
	rcu_read_lock();
	list_for_each_entry_rcu(delegation, &clp->cl_delegations, super_list) {
		if (delegation->inode->i_sb != sb)
			continue;
		inode = igrab(delegation->inode);
		if (inode == NULL)
			continue;
		spin_lock(&clp->cl_lock);
		delegation = nfs_detach_delegation_locked(NFS_I(inode), NULL);
		spin_unlock(&clp->cl_lock);
		rcu_read_unlock();
		if (delegation != NULL)
			__nfs_inode_return_delegation(inode, delegation);
		iput(inode);
		goto restart;
	}
	rcu_read_unlock();
}

static int nfs_do_expire_all_delegations(void *ptr)
{
	struct nfs_client *clp = ptr;
	struct nfs_delegation *delegation;
	struct inode *inode;

	allow_signal(SIGKILL);
restart:
	if (test_bit(NFS4CLNT_STATE_RECOVER, &clp->cl_state) != 0)
		goto out;
	if (test_bit(NFS4CLNT_LEASE_EXPIRED, &clp->cl_state) == 0)
		goto out;
	rcu_read_lock();
	list_for_each_entry_rcu(delegation, &clp->cl_delegations, super_list) {
		inode = igrab(delegation->inode);
		if (inode == NULL)
			continue;
		spin_lock(&clp->cl_lock);
		delegation = nfs_detach_delegation_locked(NFS_I(inode), NULL);
		spin_unlock(&clp->cl_lock);
		rcu_read_unlock();
		if (delegation)
			__nfs_inode_return_delegation(inode, delegation);
		iput(inode);
		goto restart;
	}
	rcu_read_unlock();
out:
	nfs_put_client(clp);
	module_put_and_exit(0);
}

void nfs_expire_all_delegations(struct nfs_client *clp)
{
	struct task_struct *task;

	__module_get(THIS_MODULE);
	atomic_inc(&clp->cl_count);
	task = kthread_run(nfs_do_expire_all_delegations, clp,
				"%s-delegreturn",
				rpc_peeraddr2str(clp->cl_rpcclient,
							RPC_DISPLAY_ADDR));
	if (!IS_ERR(task))
		return;
	nfs_put_client(clp);
	module_put(THIS_MODULE);
}

/*
 * Return all delegations following an NFS4ERR_CB_PATH_DOWN error.
 */
void nfs_handle_cb_pathdown(struct nfs_client *clp)
{
	struct nfs_delegation *delegation;
	struct inode *inode;

	if (clp == NULL)
		return;
restart:
	rcu_read_lock();
	list_for_each_entry_rcu(delegation, &clp->cl_delegations, super_list) {
		inode = igrab(delegation->inode);
		if (inode == NULL)
			continue;
		spin_lock(&clp->cl_lock);
		delegation = nfs_detach_delegation_locked(NFS_I(inode), NULL);
		spin_unlock(&clp->cl_lock);
		rcu_read_unlock();
		if (delegation != NULL)
			__nfs_inode_return_delegation(inode, delegation);
		iput(inode);
		goto restart;
	nfs_wb_all(inode);
	delegation = nfs_start_delegation_return(nfsi);
	if (delegation != NULL)
		err = nfs_end_delegation_return(inode, delegation, 1);
	return err;
}

static void nfs_mark_return_if_closed_delegation(struct nfs_server *server,
		struct nfs_delegation *delegation)
{
	set_bit(NFS_DELEGATION_RETURN_IF_CLOSED, &delegation->flags);
	set_bit(NFS4CLNT_DELEGRETURN, &server->nfs_client->cl_state);
}

static void nfs_mark_return_delegation(struct nfs_server *server,
		struct nfs_delegation *delegation)
{
	set_bit(NFS_DELEGATION_RETURN, &delegation->flags);
	set_bit(NFS4CLNT_DELEGRETURN, &server->nfs_client->cl_state);
}

static bool nfs_server_mark_return_all_delegations(struct nfs_server *server)
{
	struct nfs_delegation *delegation;
	bool ret = false;

	list_for_each_entry_rcu(delegation, &server->delegations, super_list) {
		nfs_mark_return_delegation(server, delegation);
		ret = true;
	}
	return ret;
}

static void nfs_client_mark_return_all_delegations(struct nfs_client *clp)
{
	struct nfs_server *server;

	rcu_read_lock();
	list_for_each_entry_rcu(server, &clp->cl_superblocks, client_link)
		nfs_server_mark_return_all_delegations(server);
	rcu_read_unlock();
}

static void nfs_delegation_run_state_manager(struct nfs_client *clp)
{
	if (test_bit(NFS4CLNT_DELEGRETURN, &clp->cl_state))
		nfs4_schedule_state_manager(clp);
}

/**
 * nfs_expire_all_delegations
 * @clp: client to process
 *
 */
void nfs_expire_all_delegations(struct nfs_client *clp)
{
	nfs_client_mark_return_all_delegations(clp);
	nfs_delegation_run_state_manager(clp);
}

/**
 * nfs_super_return_all_delegations - return delegations for one superblock
 * @sb: sb to process
 *
 */
void nfs_server_return_all_delegations(struct nfs_server *server)
{
	struct nfs_client *clp = server->nfs_client;
	bool need_wait;

	if (clp == NULL)
		return;

	rcu_read_lock();
	need_wait = nfs_server_mark_return_all_delegations(server);
	rcu_read_unlock();

	if (need_wait) {
		nfs4_schedule_state_manager(clp);
		nfs4_wait_clnt_recover(clp);
	}
}

static void nfs_mark_return_unused_delegation_types(struct nfs_server *server,
						 fmode_t flags)
{
	struct nfs_delegation *delegation;

	list_for_each_entry_rcu(delegation, &server->delegations, super_list) {
		if ((delegation->type == (FMODE_READ|FMODE_WRITE)) && !(flags & FMODE_WRITE))
			continue;
		if (delegation->type & flags)
			nfs_mark_return_if_closed_delegation(server, delegation);
	}
}

static void nfs_client_mark_return_unused_delegation_types(struct nfs_client *clp,
							fmode_t flags)
{
	struct nfs_server *server;

	rcu_read_lock();
	list_for_each_entry_rcu(server, &clp->cl_superblocks, client_link)
		nfs_mark_return_unused_delegation_types(server, flags);
	rcu_read_unlock();
}

static void nfs_revoke_delegation(struct inode *inode)
{
	struct nfs_delegation *delegation;
	rcu_read_lock();
	delegation = rcu_dereference(NFS_I(inode)->delegation);
	if (delegation != NULL) {
		set_bit(NFS_DELEGATION_REVOKED, &delegation->flags);
		nfs_mark_return_delegation(NFS_SERVER(inode), delegation);
	}
	rcu_read_unlock();
}

struct recall_threadargs {
	struct inode *inode;
	struct nfs_client *clp;
	const nfs4_stateid *stateid;

	struct completion started;
	int result;
};

static int recall_thread(void *data)
{
	struct recall_threadargs *args = (struct recall_threadargs *)data;
	struct inode *inode = igrab(args->inode);
	struct nfs_client *clp = NFS_SERVER(inode)->nfs_client;
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_delegation *delegation;

	daemonize("nfsv4-delegreturn");

	nfs_msync_inode(inode);
	down_read(&clp->cl_sem);
	down_write(&nfsi->rwsem);
	spin_lock(&clp->cl_lock);
	delegation = nfs_detach_delegation_locked(nfsi, args->stateid);
	if (delegation != NULL)
		args->result = 0;
	else
		args->result = -ENOENT;
	spin_unlock(&clp->cl_lock);
	complete(&args->started);
	nfs_delegation_claim_opens(inode, args->stateid);
	up_write(&nfsi->rwsem);
	up_read(&clp->cl_sem);
	nfs_msync_inode(inode);

	if (delegation != NULL)
		nfs_do_return_delegation(inode, delegation, 1);
	iput(inode);
	module_put_and_exit(0);
}

/*
 * Asynchronous delegation recall!
 */
int nfs_async_inode_return_delegation(struct inode *inode, const nfs4_stateid *stateid)
{
	struct recall_threadargs data = {
		.inode = inode,
		.stateid = stateid,
	};
	int status;

	init_completion(&data.started);
	__module_get(THIS_MODULE);
	status = kernel_thread(recall_thread, &data, CLONE_KERNEL);
	if (status < 0)
		goto out_module_put;
	wait_for_completion(&data.started);
	return data.result;
out_module_put:
	module_put(THIS_MODULE);
	return status;
}

/*
 * Retrieve the inode associated with a delegation
 */
struct inode *nfs_delegation_find_inode(struct nfs_client *clp, const struct nfs_fh *fhandle)
{
	struct nfs_delegation *delegation;
	struct inode *res = NULL;
	rcu_read_lock();
	list_for_each_entry_rcu(delegation, &clp->cl_delegations, super_list) {
		if (nfs_compare_fh(fhandle, &NFS_I(delegation->inode)->fh) == 0) {
			res = igrab(delegation->inode);
			break;
		}
void nfs_remove_bad_delegation(struct inode *inode)
{
	struct nfs_delegation *delegation;

	nfs_revoke_delegation(inode);
	delegation = nfs_inode_detach_delegation(inode);
	if (delegation) {
		nfs_inode_find_state_and_recover(inode, &delegation->stateid);
		nfs_free_delegation(delegation);
	}
}
EXPORT_SYMBOL_GPL(nfs_remove_bad_delegation);

/**
 * nfs_expire_unused_delegation_types
 * @clp: client to process
 * @flags: delegation types to expire
 *
 */
void nfs_expire_unused_delegation_types(struct nfs_client *clp, fmode_t flags)
{
	nfs_client_mark_return_unused_delegation_types(clp, flags);
	nfs_delegation_run_state_manager(clp);
}

static void nfs_mark_return_unreferenced_delegations(struct nfs_server *server)
{
	struct nfs_delegation *delegation;

	list_for_each_entry_rcu(delegation, &server->delegations, super_list) {
		if (test_and_clear_bit(NFS_DELEGATION_REFERENCED, &delegation->flags))
			continue;
		nfs_mark_return_if_closed_delegation(server, delegation);
	}
}

/**
 * nfs_expire_unreferenced_delegations - Eliminate unused delegations
 * @clp: nfs_client to process
 *
 */
void nfs_expire_unreferenced_delegations(struct nfs_client *clp)
{
	struct nfs_server *server;

	rcu_read_lock();
	list_for_each_entry_rcu(server, &clp->cl_superblocks, client_link)
		nfs_mark_return_unreferenced_delegations(server);
	rcu_read_unlock();

	nfs_delegation_run_state_manager(clp);
}

/**
 * nfs_async_inode_return_delegation - asynchronously return a delegation
 * @inode: inode to process
 * @stateid: state ID information
 *
 * Returns zero on success, or a negative errno value.
 */
int nfs_async_inode_return_delegation(struct inode *inode,
				      const nfs4_stateid *stateid)
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs_client *clp = server->nfs_client;
	struct nfs_delegation *delegation;

	rcu_read_lock();
	delegation = rcu_dereference(NFS_I(inode)->delegation);
	if (delegation == NULL)
		goto out_enoent;
	if (stateid != NULL &&
	    !clp->cl_mvops->match_stateid(&delegation->stateid, stateid))
		goto out_enoent;
	nfs_mark_return_delegation(server, delegation);
	rcu_read_unlock();

	nfs_delegation_run_state_manager(clp);
	return 0;
out_enoent:
	rcu_read_unlock();
	return -ENOENT;
}

static struct inode *
nfs_delegation_find_inode_server(struct nfs_server *server,
				 const struct nfs_fh *fhandle)
{
	struct nfs_delegation *delegation;
	struct inode *res = NULL;

	list_for_each_entry_rcu(delegation, &server->delegations, super_list) {
		spin_lock(&delegation->lock);
		if (delegation->inode != NULL &&
		    nfs_compare_fh(fhandle, &NFS_I(delegation->inode)->fh) == 0) {
			res = igrab(delegation->inode);
		}
		spin_unlock(&delegation->lock);
		if (res != NULL)
			break;
	}
	return res;
}

/**
 * nfs_delegation_find_inode - retrieve the inode associated with a delegation
 * @clp: client state handle
 * @fhandle: filehandle from a delegation recall
 *
 * Returns pointer to inode matching "fhandle," or NULL if a matching inode
 * cannot be found.
 */
struct inode *nfs_delegation_find_inode(struct nfs_client *clp,
					const struct nfs_fh *fhandle)
{
	struct nfs_server *server;
	struct inode *res = NULL;

	rcu_read_lock();
	list_for_each_entry_rcu(server, &clp->cl_superblocks, client_link) {
		res = nfs_delegation_find_inode_server(server, fhandle);
		if (res != NULL)
			break;
	}
	rcu_read_unlock();
	return res;
}

/*
 * Mark all delegations as needing to be reclaimed
 */
void nfs_delegation_mark_reclaim(struct nfs_client *clp)
{
	struct nfs_delegation *delegation;
	rcu_read_lock();
	list_for_each_entry_rcu(delegation, &clp->cl_delegations, super_list)
		delegation->flags |= NFS_DELEGATION_NEED_RECLAIM;
	rcu_read_unlock();
}

/*
 * Reap all unclaimed delegations after reboot recovery is done
static void nfs_delegation_mark_reclaim_server(struct nfs_server *server)
{
	struct nfs_delegation *delegation;

	list_for_each_entry_rcu(delegation, &server->delegations, super_list)
		set_bit(NFS_DELEGATION_NEED_RECLAIM, &delegation->flags);
}

/**
 * nfs_delegation_mark_reclaim - mark all delegations as needing to be reclaimed
 * @clp: nfs_client to process
 *
 */
void nfs_delegation_mark_reclaim(struct nfs_client *clp)
{
	struct nfs_server *server;

	rcu_read_lock();
	list_for_each_entry_rcu(server, &clp->cl_superblocks, client_link)
		nfs_delegation_mark_reclaim_server(server);
	rcu_read_unlock();
}

/**
 * nfs_delegation_reap_unclaimed - reap unclaimed delegations after reboot recovery is done
 * @clp: nfs_client to process
 *
 */
void nfs_delegation_reap_unclaimed(struct nfs_client *clp)
{
	struct nfs_delegation *delegation;
restart:
	rcu_read_lock();
	list_for_each_entry_rcu(delegation, &clp->cl_delegations, super_list) {
		if ((delegation->flags & NFS_DELEGATION_NEED_RECLAIM) == 0)
			continue;
		spin_lock(&clp->cl_lock);
		delegation = nfs_detach_delegation_locked(NFS_I(delegation->inode), NULL);
		spin_unlock(&clp->cl_lock);
		rcu_read_unlock();
		if (delegation != NULL)
			nfs_free_delegation(delegation);
		goto restart;
	struct nfs_server *server;
	struct inode *inode;

restart:
	rcu_read_lock();
	list_for_each_entry_rcu(server, &clp->cl_superblocks, client_link) {
		list_for_each_entry_rcu(delegation, &server->delegations,
								super_list) {
			if (test_bit(NFS_DELEGATION_RETURNING,
						&delegation->flags))
				continue;
			if (test_bit(NFS_DELEGATION_NEED_RECLAIM,
						&delegation->flags) == 0)
				continue;
			if (!nfs_sb_active(server->super))
				continue;
			inode = nfs_delegation_grab_inode(delegation);
			if (inode == NULL) {
				rcu_read_unlock();
				nfs_sb_deactive(server->super);
				goto restart;
			}
			delegation = nfs_start_delegation_return_locked(NFS_I(inode));
			rcu_read_unlock();
			if (delegation != NULL) {
				delegation = nfs_detach_delegation(NFS_I(inode),
					delegation, server);
				if (delegation != NULL)
					nfs_free_delegation(delegation);
			}
			iput(inode);
			nfs_sb_deactive(server->super);
			goto restart;
		}
	}
	rcu_read_unlock();
}

int nfs4_copy_delegation_stateid(nfs4_stateid *dst, struct inode *inode)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_delegation *delegation;
	int ret = 0;

	rcu_read_lock();
	delegation = rcu_dereference(nfsi->delegation);
	if (delegation != NULL) {
		memcpy(dst->data, delegation->stateid.data, sizeof(dst->data));
		ret = 1;
/**
 * nfs_delegations_present - check for existence of delegations
 * @clp: client state handle
 *
 * Returns one if there are any nfs_delegation structures attached
 * to this nfs_client.
 */
int nfs_delegations_present(struct nfs_client *clp)
{
	struct nfs_server *server;
	int ret = 0;

	rcu_read_lock();
	list_for_each_entry_rcu(server, &clp->cl_superblocks, client_link)
		if (!list_empty(&server->delegations)) {
			ret = 1;
			break;
		}
	rcu_read_unlock();
	return ret;
}

/**
 * nfs4_copy_delegation_stateid - Copy inode's state ID information
 * @dst: stateid data structure to fill in
 * @inode: inode to check
 * @flags: delegation type requirement
 *
 * Returns "true" and fills in "dst->data" * if inode had a delegation,
 * otherwise "false" is returned.
 */
bool nfs4_copy_delegation_stateid(nfs4_stateid *dst, struct inode *inode,
		fmode_t flags)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_delegation *delegation;
	bool ret;

	flags &= FMODE_READ|FMODE_WRITE;
	rcu_read_lock();
	delegation = rcu_dereference(nfsi->delegation);
	ret = nfs4_is_valid_delegation(delegation, flags);
	if (ret) {
		nfs4_stateid_copy(dst, &delegation->stateid);
		nfs_mark_delegation_referenced(delegation);
	}
	rcu_read_unlock();
	return ret;
}

/**
 * nfs4_delegation_flush_on_close - Check if we must flush file on close
 * @inode: inode to check
 *
 * This function checks the number of outstanding writes to the file
 * against the delegation 'space_limit' field to see if
 * the spec requires us to flush the file on close.
 */
bool nfs4_delegation_flush_on_close(const struct inode *inode)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_delegation *delegation;
	bool ret = true;

	rcu_read_lock();
	delegation = rcu_dereference(nfsi->delegation);
	if (delegation == NULL || !(delegation->type & FMODE_WRITE))
		goto out;
	if (nfsi->nrequests < delegation->pagemod_limit)
		ret = false;
out:
	rcu_read_unlock();
	return ret;
}
