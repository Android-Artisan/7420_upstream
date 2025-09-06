/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifndef	__XFS_LOG_H__
#define __XFS_LOG_H__

/* get lsn fields */

#define CYCLE_LSN(lsn) ((uint)((lsn)>>32))
#define BLOCK_LSN(lsn) ((uint)(lsn))

/* this is used in a spot where we might otherwise double-endian-flip */
#define CYCLE_LSN_DISK(lsn) (((__be32 *)&(lsn))[0])

#ifdef __KERNEL__
struct xfs_log_vec {
	struct xfs_log_vec	*lv_next;	/* next lv in build list */
	int			lv_niovecs;	/* number of iovecs in lv */
	struct xfs_log_iovec	*lv_iovecp;	/* iovec array */
	struct xfs_log_item	*lv_item;	/* owner */
	char			*lv_buf;	/* formatted buffer */
	int			lv_bytes;	/* accounted space in buffer */
	int			lv_buf_len;	/* aligned size of buffer */
	int			lv_size;	/* size of allocated lv */
};

#define XFS_LOG_VEC_ORDERED	(-1)

static inline void *
xlog_prepare_iovec(struct xfs_log_vec *lv, struct xfs_log_iovec **vecp,
		uint type)
{
	struct xfs_log_iovec *vec = *vecp;

	if (vec) {
		ASSERT(vec - lv->lv_iovecp < lv->lv_niovecs);
		vec++;
	} else {
		vec = &lv->lv_iovecp[0];
	}

	vec->i_type = type;
	vec->i_addr = lv->lv_buf + lv->lv_buf_len;

	ASSERT(IS_ALIGNED((unsigned long)vec->i_addr, sizeof(uint64_t)));

	*vecp = vec;
	return vec->i_addr;
}

/*
 * We need to make sure the next buffer is naturally aligned for the biggest
 * basic data type we put into it.  We already accounted for this padding when
 * sizing the buffer.
 *
 * However, this padding does not get written into the log, and hence we have to
 * track the space used by the log vectors separately to prevent log space hangs
 * due to inaccurate accounting (i.e. a leak) of the used log space through the
 * CIL context ticket.
 */
static inline void
xlog_finish_iovec(struct xfs_log_vec *lv, struct xfs_log_iovec *vec, int len)
{
	lv->lv_buf_len += round_up(len, sizeof(uint64_t));
	lv->lv_bytes += len;
	vec->i_len = len;
}

static inline void *
xlog_copy_iovec(struct xfs_log_vec *lv, struct xfs_log_iovec **vecp,
		uint type, void *data, int len)
{
	void *buf;

	buf = xlog_prepare_iovec(lv, vecp, type);
	memcpy(buf, data, len);
	xlog_finish_iovec(lv, *vecp, len);
	return buf;
}

/*
 * Structure used to pass callback function and the function's argument
 * to the log manager.
 */
typedef struct xfs_log_callback {
	struct xfs_log_callback	*cb_next;
	void			(*cb_func)(void *, int);
	void			*cb_arg;
} xfs_log_callback_t;

/*
 * By comparing each component, we don't have to worry about extra
 * endian issues in treating two 32 bit numbers as one 64 bit number
 */
static inline xfs_lsn_t	_lsn_cmp(xfs_lsn_t lsn1, xfs_lsn_t lsn2)
{
	if (CYCLE_LSN(lsn1) != CYCLE_LSN(lsn2))
		return (CYCLE_LSN(lsn1)<CYCLE_LSN(lsn2))? -999 : 999;

	if (BLOCK_LSN(lsn1) != BLOCK_LSN(lsn2))
		return (BLOCK_LSN(lsn1)<BLOCK_LSN(lsn2))? -999 : 999;

	return 0;
}

#define	XFS_LSN_CMP(x,y) _lsn_cmp(x,y)

/*
 * Macros, structures, prototypes for interface to the log manager.
 */

/*
 * Flags to xfs_log_done()
 */
#define XFS_LOG_REL_PERM_RESERV	0x1

/*
 * Flags to xfs_log_reserve()
 *
 *	XFS_LOG_SLEEP:	 If space is not available, sleep (default)
 *	XFS_LOG_NOSLEEP: If space is not available, return error
 *	XFS_LOG_PERM_RESERV: Permanent reservation.  When writes are
 *		performed against this type of reservation, the reservation
 *		is not decreased.  Long running transactions should use this.
 */
#define XFS_LOG_SLEEP		0x0
#define XFS_LOG_NOSLEEP		0x1
#define XFS_LOG_PERM_RESERV	0x2

/*
 * Flags to xfs_log_force()
 *
 *	XFS_LOG_SYNC:	Synchronous force in-core log to disk
 *	XFS_LOG_FORCE:	Start in-core log write now.
 *	XFS_LOG_URGE:	Start write within some window of time.
 *
 * Note: Either XFS_LOG_FORCE or XFS_LOG_URGE must be set.
 */
#define XFS_LOG_SYNC		0x1
#define XFS_LOG_FORCE		0x2
#define XFS_LOG_URGE		0x4

#endif	/* __KERNEL__ */


/* Log Clients */
#define XFS_TRANSACTION		0x69
#define XFS_VOLUME		0x2
#define XFS_LOG			0xaa


/* Region types for iovec's i_type */
#define XLOG_REG_TYPE_BFORMAT		1
#define XLOG_REG_TYPE_BCHUNK		2
#define XLOG_REG_TYPE_EFI_FORMAT	3
#define XLOG_REG_TYPE_EFD_FORMAT	4
#define XLOG_REG_TYPE_IFORMAT		5
#define XLOG_REG_TYPE_ICORE		6
#define XLOG_REG_TYPE_IEXT		7
#define XLOG_REG_TYPE_IBROOT		8
#define XLOG_REG_TYPE_ILOCAL		9
#define XLOG_REG_TYPE_IATTR_EXT		10
#define XLOG_REG_TYPE_IATTR_BROOT	11
#define XLOG_REG_TYPE_IATTR_LOCAL	12
#define XLOG_REG_TYPE_QFORMAT		13
#define XLOG_REG_TYPE_DQUOT		14
#define XLOG_REG_TYPE_QUOTAOFF		15
#define XLOG_REG_TYPE_LRHEADER		16
#define XLOG_REG_TYPE_UNMOUNT		17
#define XLOG_REG_TYPE_COMMIT		18
#define XLOG_REG_TYPE_TRANSHDR		19
#define XLOG_REG_TYPE_MAX		19

#define XLOG_VEC_SET_TYPE(vecp, t) ((vecp)->i_type = (t))

typedef struct xfs_log_iovec {
	xfs_caddr_t		i_addr;		/* beginning address of region */
	int		i_len;		/* length in bytes of region */
	uint		i_type;		/* type of region */
} xfs_log_iovec_t;

typedef void* xfs_log_ticket_t;

/*
 * Structure used to pass callback function and the function's argument
 * to the log manager.
 */
typedef struct xfs_log_callback {
	struct xfs_log_callback	*cb_next;
	void			(*cb_func)(void *, int);
	void			*cb_arg;
} xfs_log_callback_t;


#ifdef __KERNEL__
/* Log manager interfaces */
struct xfs_mount;
xfs_lsn_t xfs_log_done(struct xfs_mount *mp,
		       xfs_log_ticket_t ticket,
		       void		**iclog,
		       uint		flags);
int	  _xfs_log_force(struct xfs_mount *mp,
			 xfs_lsn_t	lsn,
			 uint		flags,
			 int		*log_forced);
void	  xfs_log_force(struct xfs_mount	*mp,
			xfs_lsn_t		lsn,
			uint			flags);
 * Flags to xfs_log_force()
 *
 *	XFS_LOG_SYNC:	Synchronous force in-core log to disk
 */
#define XFS_LOG_SYNC		0x1

/* Log manager interfaces */
struct xfs_mount;
struct xlog_in_core;
struct xlog_ticket;
struct xfs_log_item;
struct xfs_item_ops;
struct xfs_trans;
struct xfs_log_callback;

xfs_lsn_t xfs_log_done(struct xfs_mount *mp,
		       struct xlog_ticket *ticket,
		       struct xlog_in_core **iclog,
		       bool regrant);
int	  _xfs_log_force(struct xfs_mount *mp,
			 uint		flags,
			 int		*log_forced);
void	  xfs_log_force(struct xfs_mount	*mp,
			uint			flags);
int	  _xfs_log_force_lsn(struct xfs_mount *mp,
			     xfs_lsn_t		lsn,
			     uint		flags,
			     int		*log_forced);
void	  xfs_log_force_lsn(struct xfs_mount	*mp,
			    xfs_lsn_t		lsn,
			    uint		flags);
int	  xfs_log_mount(struct xfs_mount	*mp,
			struct xfs_buftarg	*log_target,
			xfs_daddr_t		start_block,
			int		 	num_bblocks);
int	  xfs_log_mount_finish(struct xfs_mount *mp);
void	  xfs_log_move_tail(struct xfs_mount	*mp,
			    xfs_lsn_t		tail_lsn);
int	  xfs_log_notify(struct xfs_mount	*mp,
			 void			*iclog,
			 xfs_log_callback_t	*callback_entry);
int	  xfs_log_release_iclog(struct xfs_mount *mp,
			 void			 *iclog_hndl);
int	  xfs_log_reserve(struct xfs_mount *mp,
			  int		   length,
			  int		   count,
			  xfs_log_ticket_t *ticket,
			  __uint8_t	   clientid,
			  uint		   flags,
			  uint		   t_type);
int	  xfs_log_write(struct xfs_mount *mp,
			xfs_log_iovec_t  region[],
			int		 nentries,
			xfs_log_ticket_t ticket,
			xfs_lsn_t	 *start_lsn);
int	  xfs_log_unmount(struct xfs_mount *mp);
int	  xfs_log_unmount_write(struct xfs_mount *mp);
void      xfs_log_unmount_dealloc(struct xfs_mount *mp);
int	xfs_log_mount_cancel(struct xfs_mount *);
xfs_lsn_t xlog_assign_tail_lsn(struct xfs_mount *mp);
xfs_lsn_t xlog_assign_tail_lsn_locked(struct xfs_mount *mp);
void	  xfs_log_space_wake(struct xfs_mount *mp);
int	  xfs_log_notify(struct xfs_mount	*mp,
			 struct xlog_in_core	*iclog,
			 struct xfs_log_callback *callback_entry);
int	  xfs_log_release_iclog(struct xfs_mount *mp,
			 struct xlog_in_core	 *iclog);
int	  xfs_log_reserve(struct xfs_mount *mp,
			  int		   length,
			  int		   count,
			  struct xlog_ticket **ticket,
			  __uint8_t	   clientid,
			  bool		   permanent,
			  uint		   t_type);
int	  xfs_log_regrant(struct xfs_mount *mp, struct xlog_ticket *tic);
int	  xfs_log_unmount_write(struct xfs_mount *mp);
void      xfs_log_unmount(struct xfs_mount *mp);
int	  xfs_log_force_umount(struct xfs_mount *mp, int logerror);
int	  xfs_log_need_covered(struct xfs_mount *mp);

void	  xlog_iodone(struct xfs_buf *);

#endif


extern int xlog_debug;		/* set to 1 to enable real log */

struct xlog_ticket *xfs_log_ticket_get(struct xlog_ticket *ticket);
void	  xfs_log_ticket_put(struct xlog_ticket *ticket);

void	xfs_log_commit_cil(struct xfs_mount *mp, struct xfs_trans *tp,
				xfs_lsn_t *commit_lsn, bool regrant);
bool	xfs_log_item_in_current_chkpt(struct xfs_log_item *lip);

void	xfs_log_work_queue(struct xfs_mount *mp);
void	xfs_log_worker(struct work_struct *work);
void	xfs_log_quiesce(struct xfs_mount *mp);
bool	xfs_log_check_lsn(struct xfs_mount *, xfs_lsn_t);

#endif	/* __XFS_LOG_H__ */
