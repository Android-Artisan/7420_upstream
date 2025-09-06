/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
**  Copyright (C) 2004-2010 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include "dlm_internal.h"
#include "lock.h"
#include "user.h"
#include "ast.h"

#define WAKE_ASTS  0

static struct list_head		ast_queue;
static spinlock_t		ast_queue_lock;
static struct task_struct *	astd_task;
static unsigned long		astd_wakeflags;
static struct mutex		astd_running;


void dlm_del_ast(struct dlm_lkb *lkb)
{
	spin_lock(&ast_queue_lock);
	if (lkb->lkb_ast_type & (AST_COMP | AST_BAST))
		list_del(&lkb->lkb_astqueue);
	spin_unlock(&ast_queue_lock);
}

void dlm_add_ast(struct dlm_lkb *lkb, int type)
{
	if (lkb->lkb_flags & DLM_IFL_USER) {
		dlm_user_add_ast(lkb, type);
		return;
	}

	spin_lock(&ast_queue_lock);
	if (!(lkb->lkb_ast_type & (AST_COMP | AST_BAST))) {
		kref_get(&lkb->lkb_ref);
		list_add_tail(&lkb->lkb_astqueue, &ast_queue);
	}
	lkb->lkb_ast_type |= type;
	spin_unlock(&ast_queue_lock);

	set_bit(WAKE_ASTS, &astd_wakeflags);
	wake_up_process(astd_task);
}

static void process_asts(void)
{
	struct dlm_ls *ls = NULL;
	struct dlm_rsb *r = NULL;
	struct dlm_lkb *lkb;
	void (*cast) (void *astparam);
	void (*bast) (void *astparam, int mode);
	int type = 0, found, bmode;

	for (;;) {
		found = 0;
		spin_lock(&ast_queue_lock);
		list_for_each_entry(lkb, &ast_queue, lkb_astqueue) {
			r = lkb->lkb_resource;
			ls = r->res_ls;

			if (dlm_locking_stopped(ls))
				continue;

			list_del(&lkb->lkb_astqueue);
			type = lkb->lkb_ast_type;
			lkb->lkb_ast_type = 0;
			found = 1;
			break;
		}
		spin_unlock(&ast_queue_lock);

		if (!found)
			break;

		cast = lkb->lkb_astfn;
		bast = lkb->lkb_bastfn;
		bmode = lkb->lkb_bastmode;

		if ((type & AST_COMP) && cast)
			cast(lkb->lkb_astparam);

		/* FIXME: Is it safe to look at lkb_grmode here
		   without doing a lock_rsb() ?
		   Look at other checks in v1 to avoid basts. */

		if ((type & AST_BAST) && bast)
			if (!dlm_modes_compat(lkb->lkb_grmode, bmode))
				bast(lkb->lkb_astparam, bmode);

		/* this removes the reference added by dlm_add_ast
		   and may result in the lkb being freed */
		dlm_put_lkb(lkb);

		schedule();
	}
}

static inline int no_asts(void)
{
	int ret;

	spin_lock(&ast_queue_lock);
	ret = list_empty(&ast_queue);
	spin_unlock(&ast_queue_lock);
	return ret;
}

static int dlm_astd(void *data)
{
	while (!kthread_should_stop()) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (!test_bit(WAKE_ASTS, &astd_wakeflags))
			schedule();
		set_current_state(TASK_RUNNING);

		mutex_lock(&astd_running);
		if (test_and_clear_bit(WAKE_ASTS, &astd_wakeflags))
			process_asts();
		mutex_unlock(&astd_running);
static uint64_t dlm_cb_seq;
static DEFINE_SPINLOCK(dlm_cb_seq_spin);

static void dlm_dump_lkb_callbacks(struct dlm_lkb *lkb)
{
	int i;

	log_print("last_bast %x %llu flags %x mode %d sb %d %x",
		  lkb->lkb_id,
		  (unsigned long long)lkb->lkb_last_bast.seq,
		  lkb->lkb_last_bast.flags,
		  lkb->lkb_last_bast.mode,
		  lkb->lkb_last_bast.sb_status,
		  lkb->lkb_last_bast.sb_flags);

	log_print("last_cast %x %llu flags %x mode %d sb %d %x",
		  lkb->lkb_id,
		  (unsigned long long)lkb->lkb_last_cast.seq,
		  lkb->lkb_last_cast.flags,
		  lkb->lkb_last_cast.mode,
		  lkb->lkb_last_cast.sb_status,
		  lkb->lkb_last_cast.sb_flags);

	for (i = 0; i < DLM_CALLBACKS_SIZE; i++) {
		log_print("cb %x %llu flags %x mode %d sb %d %x",
			  lkb->lkb_id,
			  (unsigned long long)lkb->lkb_callbacks[i].seq,
			  lkb->lkb_callbacks[i].flags,
			  lkb->lkb_callbacks[i].mode,
			  lkb->lkb_callbacks[i].sb_status,
			  lkb->lkb_callbacks[i].sb_flags);
	}
}

int dlm_add_lkb_callback(struct dlm_lkb *lkb, uint32_t flags, int mode,
			 int status, uint32_t sbflags, uint64_t seq)
{
	struct dlm_ls *ls = lkb->lkb_resource->res_ls;
	uint64_t prev_seq;
	int prev_mode;
	int i, rv;

	for (i = 0; i < DLM_CALLBACKS_SIZE; i++) {
		if (lkb->lkb_callbacks[i].seq)
			continue;

		/*
		 * Suppress some redundant basts here, do more on removal.
		 * Don't even add a bast if the callback just before it
		 * is a bast for the same mode or a more restrictive mode.
		 * (the addional > PR check is needed for PR/CW inversion)
		 */

		if ((i > 0) && (flags & DLM_CB_BAST) &&
		    (lkb->lkb_callbacks[i-1].flags & DLM_CB_BAST)) {

			prev_seq = lkb->lkb_callbacks[i-1].seq;
			prev_mode = lkb->lkb_callbacks[i-1].mode;

			if ((prev_mode == mode) ||
			    (prev_mode > mode && prev_mode > DLM_LOCK_PR)) {

				log_debug(ls, "skip %x add bast %llu mode %d "
					  "for bast %llu mode %d",
					  lkb->lkb_id,
					  (unsigned long long)seq,
					  mode,
					  (unsigned long long)prev_seq,
					  prev_mode);
				rv = 0;
				goto out;
			}
		}

		lkb->lkb_callbacks[i].seq = seq;
		lkb->lkb_callbacks[i].flags = flags;
		lkb->lkb_callbacks[i].mode = mode;
		lkb->lkb_callbacks[i].sb_status = status;
		lkb->lkb_callbacks[i].sb_flags = (sbflags & 0x000000FF);
		rv = 0;
		break;
	}

	if (i == DLM_CALLBACKS_SIZE) {
		log_error(ls, "no callbacks %x %llu flags %x mode %d sb %d %x",
			  lkb->lkb_id, (unsigned long long)seq,
			  flags, mode, status, sbflags);
		dlm_dump_lkb_callbacks(lkb);
		rv = -1;
		goto out;
	}
 out:
	return rv;
}

int dlm_rem_lkb_callback(struct dlm_ls *ls, struct dlm_lkb *lkb,
			 struct dlm_callback *cb, int *resid)
{
	int i, rv;

	*resid = 0;

	if (!lkb->lkb_callbacks[0].seq) {
		rv = -ENOENT;
		goto out;
	}

	/* oldest undelivered cb is callbacks[0] */

	memcpy(cb, &lkb->lkb_callbacks[0], sizeof(struct dlm_callback));
	memset(&lkb->lkb_callbacks[0], 0, sizeof(struct dlm_callback));

	/* shift others down */

	for (i = 1; i < DLM_CALLBACKS_SIZE; i++) {
		if (!lkb->lkb_callbacks[i].seq)
			break;
		memcpy(&lkb->lkb_callbacks[i-1], &lkb->lkb_callbacks[i],
		       sizeof(struct dlm_callback));
		memset(&lkb->lkb_callbacks[i], 0, sizeof(struct dlm_callback));
		(*resid)++;
	}

	/* if cb is a bast, it should be skipped if the blocking mode is
	   compatible with the last granted mode */

	if ((cb->flags & DLM_CB_BAST) && lkb->lkb_last_cast.seq) {
		if (dlm_modes_compat(cb->mode, lkb->lkb_last_cast.mode)) {
			cb->flags |= DLM_CB_SKIP;

			log_debug(ls, "skip %x bast %llu mode %d "
				  "for cast %llu mode %d",
				  lkb->lkb_id,
				  (unsigned long long)cb->seq,
				  cb->mode,
				  (unsigned long long)lkb->lkb_last_cast.seq,
				  lkb->lkb_last_cast.mode);
			rv = 0;
			goto out;
		}
	}

	if (cb->flags & DLM_CB_CAST) {
		memcpy(&lkb->lkb_last_cast, cb, sizeof(struct dlm_callback));
		lkb->lkb_last_cast_time = ktime_get();
	}

	if (cb->flags & DLM_CB_BAST) {
		memcpy(&lkb->lkb_last_bast, cb, sizeof(struct dlm_callback));
		lkb->lkb_last_bast_time = ktime_get();
	}
	rv = 0;
 out:
	return rv;
}

void dlm_add_cb(struct dlm_lkb *lkb, uint32_t flags, int mode, int status,
		uint32_t sbflags)
{
	struct dlm_ls *ls = lkb->lkb_resource->res_ls;
	uint64_t new_seq, prev_seq;
	int rv;

	spin_lock(&dlm_cb_seq_spin);
	new_seq = ++dlm_cb_seq;
	spin_unlock(&dlm_cb_seq_spin);

	if (lkb->lkb_flags & DLM_IFL_USER) {
		dlm_user_add_ast(lkb, flags, mode, status, sbflags, new_seq);
		return;
	}

	mutex_lock(&lkb->lkb_cb_mutex);
	prev_seq = lkb->lkb_callbacks[0].seq;

	rv = dlm_add_lkb_callback(lkb, flags, mode, status, sbflags, new_seq);
	if (rv < 0)
		goto out;

	if (!prev_seq) {
		kref_get(&lkb->lkb_ref);

		mutex_lock(&ls->ls_cb_mutex);
		if (test_bit(LSFL_CB_DELAY, &ls->ls_flags)) {
			list_add(&lkb->lkb_cb_list, &ls->ls_cb_delay);
		} else {
			queue_work(ls->ls_callback_wq, &lkb->lkb_cb_work);
		}
		mutex_unlock(&ls->ls_cb_mutex);
	}
 out:
	mutex_unlock(&lkb->lkb_cb_mutex);
}

void dlm_callback_work(struct work_struct *work)
{
	struct dlm_lkb *lkb = container_of(work, struct dlm_lkb, lkb_cb_work);
	struct dlm_ls *ls = lkb->lkb_resource->res_ls;
	void (*castfn) (void *astparam);
	void (*bastfn) (void *astparam, int mode);
	struct dlm_callback callbacks[DLM_CALLBACKS_SIZE];
	int i, rv, resid;

	memset(&callbacks, 0, sizeof(callbacks));

	mutex_lock(&lkb->lkb_cb_mutex);
	if (!lkb->lkb_callbacks[0].seq) {
		/* no callback work exists, shouldn't happen */
		log_error(ls, "dlm_callback_work %x no work", lkb->lkb_id);
		dlm_print_lkb(lkb);
		dlm_dump_lkb_callbacks(lkb);
	}

	for (i = 0; i < DLM_CALLBACKS_SIZE; i++) {
		rv = dlm_rem_lkb_callback(ls, lkb, &callbacks[i], &resid);
		if (rv < 0)
			break;
	}

	if (resid) {
		/* cbs remain, loop should have removed all, shouldn't happen */
		log_error(ls, "dlm_callback_work %x resid %d", lkb->lkb_id,
			  resid);
		dlm_print_lkb(lkb);
		dlm_dump_lkb_callbacks(lkb);
	}
	mutex_unlock(&lkb->lkb_cb_mutex);

	castfn = lkb->lkb_astfn;
	bastfn = lkb->lkb_bastfn;

	for (i = 0; i < DLM_CALLBACKS_SIZE; i++) {
		if (!callbacks[i].seq)
			break;
		if (callbacks[i].flags & DLM_CB_SKIP) {
			continue;
		} else if (callbacks[i].flags & DLM_CB_BAST) {
			bastfn(lkb->lkb_astparam, callbacks[i].mode);
		} else if (callbacks[i].flags & DLM_CB_CAST) {
			lkb->lkb_lksb->sb_status = callbacks[i].sb_status;
			lkb->lkb_lksb->sb_flags = callbacks[i].sb_flags;
			castfn(lkb->lkb_astparam);
		}
	}

	/* undo kref_get from dlm_add_callback, may cause lkb to be freed */
	dlm_put_lkb(lkb);
}

int dlm_callback_start(struct dlm_ls *ls)
{
	ls->ls_callback_wq = alloc_workqueue("dlm_callback",
					     WQ_UNBOUND | WQ_MEM_RECLAIM, 0);
	if (!ls->ls_callback_wq) {
		log_print("can't start dlm_callback workqueue");
		return -ENOMEM;
	}
	return 0;
}

void dlm_astd_wake(void)
{
	if (!no_asts()) {
		set_bit(WAKE_ASTS, &astd_wakeflags);
		wake_up_process(astd_task);
	}
}

int dlm_astd_start(void)
{
	struct task_struct *p;
	int error = 0;

	INIT_LIST_HEAD(&ast_queue);
	spin_lock_init(&ast_queue_lock);
	mutex_init(&astd_running);

	p = kthread_run(dlm_astd, NULL, "dlm_astd");
	if (IS_ERR(p))
		error = PTR_ERR(p);
	else
		astd_task = p;
	return error;
}

void dlm_astd_stop(void)
{
	kthread_stop(astd_task);
}

void dlm_astd_suspend(void)
{
	mutex_lock(&astd_running);
}

void dlm_astd_resume(void)
{
	mutex_unlock(&astd_running);
void dlm_callback_stop(struct dlm_ls *ls)
{
	if (ls->ls_callback_wq)
		destroy_workqueue(ls->ls_callback_wq);
}

void dlm_callback_suspend(struct dlm_ls *ls)
{
	mutex_lock(&ls->ls_cb_mutex);
	set_bit(LSFL_CB_DELAY, &ls->ls_flags);
	mutex_unlock(&ls->ls_cb_mutex);

	if (ls->ls_callback_wq)
		flush_workqueue(ls->ls_callback_wq);
}

#define MAX_CB_QUEUE 25

void dlm_callback_resume(struct dlm_ls *ls)
{
	struct dlm_lkb *lkb, *safe;
	int count = 0;

	clear_bit(LSFL_CB_DELAY, &ls->ls_flags);

	if (!ls->ls_callback_wq)
		return;

more:
	mutex_lock(&ls->ls_cb_mutex);
	list_for_each_entry_safe(lkb, safe, &ls->ls_cb_delay, lkb_cb_list) {
		list_del_init(&lkb->lkb_cb_list);
		queue_work(ls->ls_callback_wq, &lkb->lkb_cb_work);
		count++;
		if (count == MAX_CB_QUEUE)
			break;
	}
	mutex_unlock(&ls->ls_cb_mutex);

	if (count)
		log_rinfo(ls, "dlm_callback_resume %d", count);
	if (count == MAX_CB_QUEUE) {
		count = 0;
		cond_resched();
		goto more;
	}
}

