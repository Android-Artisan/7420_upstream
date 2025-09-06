#ifndef _LINUX_BLKDEV_H
#define _LINUX_BLKDEV_H

#ifdef CONFIG_BLOCK

#include <linux/sched.h>
#include <linux/major.h>
#include <linux/genhd.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/pagemap.h>
#include <linux/backing-dev.h>
#include <linux/wait.h>
#include <linux/mempool.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/stringify.h>
#include <linux/bsg.h>

#include <asm/scatterlist.h>

#include <linux/sched.h>

#ifdef CONFIG_BLOCK

#include <linux/major.h>
#include <linux/genhd.h>
#include <linux/list.h>
#include <linux/llist.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/pagemap.h>
#include <linux/backing-dev-defs.h>
#include <linux/wait.h>
#include <linux/mempool.h>
#include <linux/bio.h>
#include <linux/stringify.h>
#include <linux/gfp.h>
#include <linux/bsg.h>
#include <linux/smp.h>
#include <linux/rcupdate.h>
#include <linux/percpu-refcount.h>
#include <linux/scatterlist.h>

struct module;
struct scsi_ioctl_command;

struct request_queue;
struct elevator_queue;
typedef struct elevator_queue elevator_t;
struct request_pm_state;
struct blk_trace;
struct request;
struct sg_io_hdr;
struct blk_trace;
struct request;
struct sg_io_hdr;
struct bsg_job;
struct blkcg_gq;
struct blk_flush_queue;
struct pr_ops;

#define BLKDEV_MIN_RQ	4
#define BLKDEV_MAX_RQ	128	/* Default maximum */

struct request;
typedef void (rq_end_io_fn)(struct request *, int);

struct request_list {
	int count[2];
	int starved[2];
	int elvpriv;
	mempool_t *rq_pool;
	wait_queue_head_t wait[2];
/*
 * Maximum number of blkcg policies allowed to be registered concurrently.
 * Defined here to simplify include dependency.
 */
#define BLKCG_MAX_POLS		2

struct request;
typedef void (rq_end_io_fn)(struct request *, int);

static inline int blk_validate_block_size(unsigned long bsize)
{
	if (bsize < 512 || bsize > PAGE_SIZE || !is_power_of_2(bsize))
		return -EINVAL;

	return 0;
}

#define BLK_RL_SYNCFULL		(1U << 0)
#define BLK_RL_ASYNCFULL	(1U << 1)

struct request_list {
	struct request_queue	*q;	/* the queue this rl belongs to */
#ifdef CONFIG_BLK_CGROUP
	struct blkcg_gq		*blkg;	/* blkg this request pool belongs to */
#endif
	/*
	 * count[], starved[], and wait[] are indexed by
	 * BLK_RW_SYNC/BLK_RW_ASYNC
	 */
	int			count[2];
	int			starved[2];
	mempool_t		*rq_pool;
	wait_queue_head_t	wait[2];
	unsigned int		flags;
};

/*
 * request command types
 */
enum rq_cmd_type_bits {
	REQ_TYPE_FS		= 1,	/* fs request */
	REQ_TYPE_BLOCK_PC,		/* scsi command */
	REQ_TYPE_SENSE,			/* sense request */
	REQ_TYPE_PM_SUSPEND,		/* suspend request */
	REQ_TYPE_PM_RESUME,		/* resume request */
	REQ_TYPE_PM_SHUTDOWN,		/* shutdown request */
	REQ_TYPE_FLUSH,			/* flush request */
	REQ_TYPE_SPECIAL,		/* driver defined type */
	REQ_TYPE_LINUX_BLOCK,		/* generic block layer message */
	/*
	 * for ATA/ATAPI devices. this really doesn't belong here, ide should
	 * use REQ_TYPE_SPECIAL and use rq->cmd[0] with the range of driver
	 * private REQ_LB opcodes to differentiate what type of request this is
	 */
	REQ_TYPE_ATA_TASKFILE,
	REQ_TYPE_ATA_PC,
};

/*
 * For request of type REQ_TYPE_LINUX_BLOCK, rq->cmd[0] is the opcode being
 * sent down (similar to how REQ_TYPE_BLOCK_PC means that ->cmd[] holds a
 * SCSI cdb.
 *
 * 0x00 -> 0x3f are driver private, to be used for whatever purpose they need,
 * typically to differentiate REQ_TYPE_SPECIAL requests.
 *
 */
enum {
	/*
	 * just examples for now
	 */
	REQ_LB_OP_EJECT	= 0x40,		/* eject request */
	REQ_LB_OP_FLUSH = 0x41,		/* flush device */
};

/*
 * request type modified bits. first three bits match BIO_RW* bits, important
 */
enum rq_flag_bits {
	__REQ_RW,		/* not set, read. set, write */
	__REQ_FAILFAST,		/* no low level driver retries */
	__REQ_SORTED,		/* elevator knows about this request */
	__REQ_SOFTBARRIER,	/* may not be passed by ioscheduler */
	__REQ_HARDBARRIER,	/* may not be passed by drive either */
	__REQ_FUA,		/* forced unit access */
	__REQ_NOMERGE,		/* don't touch this for merging */
	__REQ_STARTED,		/* drive already may have started this one */
	__REQ_DONTPREP,		/* don't call prep for this one */
	__REQ_QUEUED,		/* uses queueing */
	__REQ_ELVPRIV,		/* elevator private data attached */
	__REQ_FAILED,		/* set if the request failed */
	__REQ_QUIET,		/* don't worry about errors */
	__REQ_PREEMPT,		/* set for "ide_preempt" requests */
	__REQ_ORDERED_COLOR,	/* is before or after barrier */
	__REQ_RW_SYNC,		/* request is sync (O_DIRECT) */
	__REQ_ALLOCED,		/* request came from our alloc pool */
	__REQ_RW_META,		/* metadata io request */
	__REQ_COPY_USER,	/* contains copies of user pages */
	__REQ_INTEGRITY,	/* integrity metadata has been remapped */
	__REQ_NR_BITS,		/* stops here */
};

#define REQ_RW		(1 << __REQ_RW)
#define REQ_FAILFAST	(1 << __REQ_FAILFAST)
#define REQ_SORTED	(1 << __REQ_SORTED)
#define REQ_SOFTBARRIER	(1 << __REQ_SOFTBARRIER)
#define REQ_HARDBARRIER	(1 << __REQ_HARDBARRIER)
#define REQ_FUA		(1 << __REQ_FUA)
#define REQ_NOMERGE	(1 << __REQ_NOMERGE)
#define REQ_STARTED	(1 << __REQ_STARTED)
#define REQ_DONTPREP	(1 << __REQ_DONTPREP)
#define REQ_QUEUED	(1 << __REQ_QUEUED)
#define REQ_ELVPRIV	(1 << __REQ_ELVPRIV)
#define REQ_FAILED	(1 << __REQ_FAILED)
#define REQ_QUIET	(1 << __REQ_QUIET)
#define REQ_PREEMPT	(1 << __REQ_PREEMPT)
#define REQ_ORDERED_COLOR	(1 << __REQ_ORDERED_COLOR)
#define REQ_RW_SYNC	(1 << __REQ_RW_SYNC)
#define REQ_ALLOCED	(1 << __REQ_ALLOCED)
#define REQ_RW_META	(1 << __REQ_RW_META)
#define REQ_COPY_USER	(1 << __REQ_COPY_USER)
#define REQ_INTEGRITY	(1 << __REQ_INTEGRITY)

#define BLK_MAX_CDB	16

/*
 * try to put the fields that are referenced together in the same cacheline.
 * if you modify this structure, be sure to check block/blk-core.c:rq_init()
 * as well!
 */
struct request {
	struct list_head queuelist;
	struct list_head donelist;

	struct request_queue *q;

	unsigned int cmd_flags;
	enum rq_cmd_type_bits cmd_type;

	/* Maintain bio traversal state for part by part I/O submission.
	 * hard_* are block layer internals, no driver should touch them!
	 */

	sector_t sector;		/* next sector to submit */
	sector_t hard_sector;		/* next sector to complete */
	unsigned long nr_sectors;	/* no. of sectors left to submit */
	unsigned long hard_nr_sectors;	/* no. of sectors left to complete */
	/* no. of sectors left to submit in the current segment */
	unsigned int current_nr_sectors;

	/* no. of sectors left to complete in the current segment */
	unsigned int hard_cur_sectors;
	REQ_TYPE_DRV_PRIV,		/* driver defined types from here */
};

#define BLK_MAX_CDB	16

/*
 * Try to put the fields that are referenced together in the same cacheline.
 *
 * If you modify this structure, make sure to update blk_rq_init() and
 * especially blk_mq_rq_ctx_init() to take care of the added fields.
 */
struct request {
	struct list_head queuelist;
	union {
		struct call_single_data csd;
		struct work_struct work;
		unsigned long fifo_time;
	};

	struct request_queue *q;
	struct blk_mq_ctx *mq_ctx;

	u64 cmd_flags;
	unsigned cmd_type;
	unsigned long atomic_flags;

	int cpu;

	/* the following two fields are internal, NEVER access directly */
	unsigned int __data_len;	/* total data len */
	sector_t __sector;		/* sector cursor */

	struct bio *bio;
	struct bio *biotail;

	struct hlist_node hash;	/* merge hash */
	/*
	 * The hash is used inside the scheduler, and killed once the
	 * request reaches the dispatch list. The ipi_list is only used
	 * to queue the request for softirq completion, which is long
	 * after the request has been unhashed (and even removed from
	 * the dispatch list).
	 */
	union {
		struct hlist_node hash;	/* merge hash */
		struct list_head ipi_list;
	};

	/*
	 * The rb_node is only used inside the io scheduler, requests
	 * are pruned when moved to the dispatch queue. So let the
	 * completion_data share space with the rb_node.
	 */
	union {
		struct rb_node rb_node;	/* sort/lookup */
		void *completion_data;
	};

	/*
	 * two pointers are available for the IO schedulers, if they need
	 * more they have to dynamically allocate it.
	 */
	void *elevator_private;
	void *elevator_private2;

	struct gendisk *rq_disk;
	unsigned long start_time;

	 * Three pointers are available for the IO schedulers, if they need
	 * more they have to dynamically allocate it.  Flush requests are
	 * never put on the IO scheduler. So let the flush fields share
	 * space with the elevator data.
	 */
	union {
		struct {
			struct io_cq		*icq;
			void			*priv[2];
		} elv;

		struct {
			unsigned int		seq;
			struct list_head	list;
			rq_end_io_fn		*saved_end_io;
		} flush;
	};

	struct gendisk *rq_disk;
	struct hd_struct *part;
	unsigned long start_time;
#ifdef CONFIG_BLK_CGROUP
	struct request_list *rl;		/* rl this rq is alloced from */
	unsigned long long start_time_ns;
	unsigned long long io_start_time_ns;    /* when passed to hardware */
#endif
	/* Number of scatter-gather DMA addr+len pairs after
	 * physical address coalescing is performed.
	 */
	unsigned short nr_phys_segments;

	/* Number of scatter-gather addr+len pairs after
	 * physical and DMA remapping hardware coalescing is performed.
	 * This is the number of scatter-gather entries the driver
	 * will actually have to deal with after DMA mapping is done.
	 */
	unsigned short nr_hw_segments;

	unsigned short ioprio;

	void *special;
	char *buffer;
#if defined(CONFIG_BLK_DEV_INTEGRITY)
	unsigned short nr_integrity_segments;
#endif

	unsigned short ioprio;

	void *special;		/* opaque pointer available for LLD use */

	int tag;
	int errors;

	int ref_count;

	/*
	 * when request is used as a packet command carrier
	 */
	unsigned short cmd_len;
	unsigned char __cmd[BLK_MAX_CDB];
	unsigned char *cmd;

	unsigned int data_len;
	unsigned int extra_len;	/* length of alignment and padding */
	unsigned int sense_len;
	void *data;
	void *sense;

	/*
	 * when request is used as a packet command carrier
	 */
	unsigned char __cmd[BLK_MAX_CDB];
	unsigned char *cmd;
	unsigned short cmd_len;

	unsigned int extra_len;	/* length of alignment and padding */
	unsigned int sense_len;
	unsigned int resid_len;	/* residual count */
	void *sense;

	unsigned long deadline;
	struct list_head timeout_list;
	unsigned int timeout;
	int retries;

	/*
	 * completion callback.
	 */
	rq_end_io_fn *end_io;
	void *end_io_data;

	/* for bidi */
	struct request *next_rq;
};

/*
 * State information carried for REQ_TYPE_PM_SUSPEND and REQ_TYPE_PM_RESUME
 * requests. Some step values could eventually be made generic.
 */
struct request_pm_state
{
	/* PM state machine step value, currently driver specific */
	int	pm_step;
	/* requested PM state value (S1, S2, S3, S4, ...) */
	u32	pm_state;
	void*	data;		/* for driver use */
};

#include <linux/elevator.h>

typedef void (request_fn_proc) (struct request_queue *q);
typedef int (make_request_fn) (struct request_queue *q, struct bio *bio);
typedef int (prep_rq_fn) (struct request_queue *, struct request *);
typedef void (unplug_fn) (struct request_queue *);

struct bio_vec;
struct bvec_merge_data {
	struct block_device *bi_bdev;
	sector_t bi_sector;
	unsigned bi_size;
	unsigned long bi_rw;
};
typedef int (merge_bvec_fn) (struct request_queue *, struct bvec_merge_data *,
			     struct bio_vec *);
typedef void (prepare_flush_fn) (struct request_queue *, struct request *);
typedef void (softirq_done_fn)(struct request *);
typedef int (dma_drain_needed_fn)(struct request *);
static inline bool blk_rq_is_passthrough(struct request *rq)
{
	return rq->cmd_type != REQ_TYPE_FS;
}

static inline unsigned short req_get_ioprio(struct request *req)
{
	return req->ioprio;
}

#include <linux/elevator.h>

struct blk_queue_ctx;

typedef void (request_fn_proc) (struct request_queue *q);
typedef blk_qc_t (make_request_fn) (struct request_queue *q, struct bio *bio);
typedef int (prep_rq_fn) (struct request_queue *, struct request *);
typedef void (unprep_rq_fn) (struct request_queue *, struct request *);

struct bio_vec;
typedef void (softirq_done_fn)(struct request *);
typedef int (dma_drain_needed_fn)(struct request *);
typedef int (lld_busy_fn) (struct request_queue *q);
typedef int (bsg_job_fn) (struct bsg_job *);

enum blk_eh_timer_return {
	BLK_EH_NOT_HANDLED,
	BLK_EH_HANDLED,
	BLK_EH_RESET_TIMER,
};

typedef enum blk_eh_timer_return (rq_timed_out_fn)(struct request *);

enum blk_queue_state {
	Queue_down,
	Queue_up,
};

struct blk_queue_tag {
	struct request **tag_index;	/* map of busy tags */
	unsigned long *tag_map;		/* bit map of free/busy tags */
	int busy;			/* current depth */
	int max_depth;			/* what we will send to device */
	int real_max_depth;		/* what the array can hold */
	atomic_t refcnt;		/* map can be shared */
};
	int alloc_policy;		/* tag allocation policy */
	int next_tag;			/* next tag */
};
#define BLK_TAG_ALLOC_FIFO 0 /* allocate starting from 0 */
#define BLK_TAG_ALLOC_RR 1 /* allocate starting from last allocated tag */

#define BLK_SCSI_MAX_CMDS	(256)
#define BLK_SCSI_CMD_PER_LONG	(BLK_SCSI_MAX_CMDS / (sizeof(long) * 8))

struct blk_cmd_filter {
	unsigned long read_ok[BLK_SCSI_CMD_PER_LONG];
	unsigned long write_ok[BLK_SCSI_CMD_PER_LONG];
	struct kobject kobj;
};

struct request_queue
{
struct queue_limits {
	unsigned long		bounce_pfn;
	unsigned long		seg_boundary_mask;
	unsigned long		virt_boundary_mask;

	unsigned int		max_hw_sectors;
	unsigned int		max_dev_sectors;
	unsigned int		chunk_sectors;
	unsigned int		max_sectors;
	unsigned int		max_segment_size;
	unsigned int		physical_block_size;
	unsigned int		logical_block_size;
	unsigned int		alignment_offset;
	unsigned int		io_min;
	unsigned int		io_opt;
	unsigned int		max_discard_sectors;
	unsigned int		max_hw_discard_sectors;
	unsigned int		max_write_same_sectors;
	unsigned int		discard_granularity;
	unsigned int		discard_alignment;

	unsigned short		max_segments;
	unsigned short		max_integrity_segments;

	unsigned char		misaligned;
	unsigned char		discard_misaligned;
	unsigned char		cluster;
	unsigned char		discard_zeroes_data;
	unsigned char		raid_partial_stripes_expensive;
};

struct request_queue {
	/*
	 * Together with queue_head for cacheline sharing
	 */
	struct list_head	queue_head;
	struct request		*last_merge;
	elevator_t		*elevator;

	/*
	 * the queue request freelist, one for reads and one for writes
	 */
	struct request_list	rq;
	struct elevator_queue	*elevator;
	int			nr_rqs[2];	/* # allocated [a]sync rqs */
	int			nr_rqs_elvpriv;	/* # allocated rqs w/ elvpriv */

	/*
	 * If blkcg is not used, @q->root_rl serves all requests.  If blkcg
	 * is used, root blkg allocates from @q->root_rl and all other
	 * blkgs from their own blkg->rl.  Which one to use should be
	 * determined using bio_request_list().
	 */
	struct request_list	root_rl;

	request_fn_proc		*request_fn;
	make_request_fn		*make_request_fn;
	prep_rq_fn		*prep_rq_fn;
	unplug_fn		*unplug_fn;
	merge_bvec_fn		*merge_bvec_fn;
	prepare_flush_fn	*prepare_flush_fn;
	softirq_done_fn		*softirq_done_fn;
	dma_drain_needed_fn	*dma_drain_needed;
	unprep_rq_fn		*unprep_rq_fn;
	softirq_done_fn		*softirq_done_fn;
	rq_timed_out_fn		*rq_timed_out_fn;
	dma_drain_needed_fn	*dma_drain_needed;
	lld_busy_fn		*lld_busy_fn;

	struct blk_mq_ops	*mq_ops;

	unsigned int		*mq_map;

	/* sw queues */
	struct blk_mq_ctx __percpu	*queue_ctx;
	unsigned int		nr_queues;

	/* hw dispatch queues */
	struct blk_mq_hw_ctx	**queue_hw_ctx;
	unsigned int		nr_hw_queues;

	/*
	 * Dispatch queue sorting
	 */
	sector_t		end_sector;
	struct request		*boundary_rq;

	/*
	 * Auto-unplugging state
	 */
	struct timer_list	unplug_timer;
	int			unplug_thresh;	/* After this many requests */
	unsigned long		unplug_delay;	/* After this many jiffies */
	struct work_struct	unplug_work;
	 * Delayed queue handling
	 */
	struct delayed_work	delay_work;

	struct backing_dev_info	backing_dev_info;

	/*
	 * The queue owner gets to use this for whatever they like.
	 * ll_rw_blk doesn't touch it.
	 */
	void			*queuedata;

	/*
	 * queue needs bounce pages for pages above this limit
	 */
	unsigned long		bounce_pfn;
	gfp_t			bounce_gfp;

	/*
	 * various queue flags, see QUEUE_* below
	 */
	unsigned long		queue_flags;

	/*
	 * ida allocated id for this queue.  Used to index queues from
	 * ioctx.
	 */
	int			id;

	/*
	 * queue needs bounce pages for pages above this limit
	 */
	gfp_t			bounce_gfp;

	/*
	 * protects queue structures from reentrancy. ->__queue_lock should
	 * _never_ be used directly, it is queue private. always use
	 * ->queue_lock.
	 */
	spinlock_t		__queue_lock;
	spinlock_t		*queue_lock;

	/*
	 * queue kobject
	 */
	struct kobject kobj;

	/*
	 * mq queue kobject
	 */
	struct kobject mq_kobj;

#ifdef  CONFIG_BLK_DEV_INTEGRITY
	struct blk_integrity integrity;
#endif	/* CONFIG_BLK_DEV_INTEGRITY */

#ifdef CONFIG_PM
	struct device		*dev;
	int			rpm_status;
	unsigned int		nr_pending;
#endif

	/*
	 * queue settings
	 */
	unsigned long		nr_requests;	/* Max # of requests */
	unsigned int		nr_congestion_on;
	unsigned int		nr_congestion_off;
	unsigned int		nr_batching;

	unsigned int		max_sectors;
	unsigned int		max_hw_sectors;
	unsigned short		max_phys_segments;
	unsigned short		max_hw_segments;
	unsigned short		hardsect_size;
	unsigned int		max_segment_size;

	unsigned long		seg_boundary_mask;
	void			*dma_drain_buffer;
	unsigned int		dma_drain_size;
	unsigned int		dma_drain_size;
	void			*dma_drain_buffer;
	unsigned int		dma_pad_mask;
	unsigned int		dma_alignment;

	struct blk_queue_tag	*queue_tags;
	struct list_head	tag_busy_list;

	unsigned int		nr_sorted;
	unsigned int		in_flight;
	unsigned int		in_flight[2];
	/*
	 * Number of active block driver functions for which blk_drain_queue()
	 * must wait. Must be incremented around functions that unlock the
	 * queue_lock internally, e.g. scsi_request_fn().
	 */
	unsigned int		request_fn_active;

	unsigned int		rq_timeout;
	struct timer_list	timeout;
	struct list_head	timeout_list;

	struct list_head	icq_list;
#ifdef CONFIG_BLK_CGROUP
	DECLARE_BITMAP		(blkcg_pols, BLKCG_MAX_POLS);
	struct blkcg_gq		*root_blkg;
	struct list_head	blkg_list;
#endif

	struct queue_limits	limits;

	/*
	 * sg stuff
	 */
	unsigned int		sg_timeout;
	unsigned int		sg_reserved_size;
	int			node;
#ifdef CONFIG_BLK_DEV_IO_TRACE
	struct blk_trace __rcu	*blk_trace;
	struct mutex		blk_trace_mutex;
#endif
	/*
	 * reserved for flush operations
	 */
	unsigned int		ordered, next_ordered, ordseq;
	int			orderr, ordcolor;
	struct request		pre_flush_rq, bar_rq, post_flush_rq;
	struct request		*orig_bar_rq;

	struct mutex		sysfs_lock;

#if defined(CONFIG_BLK_DEV_BSG)
	struct bsg_class_device bsg_dev;
#endif
	struct blk_cmd_filter cmd_filter;
};

#define QUEUE_FLAG_CLUSTER	0	/* cluster several segments into 1 */
#define QUEUE_FLAG_QUEUED	1	/* uses generic tag queueing */
#define QUEUE_FLAG_STOPPED	2	/* queue is stopped */
#define	QUEUE_FLAG_READFULL	3	/* read queue has been filled */
#define QUEUE_FLAG_WRITEFULL	4	/* write queue has been filled */
#define QUEUE_FLAG_DEAD		5	/* queue being torn down */
#define QUEUE_FLAG_REENTER	6	/* Re-entrancy avoidance */
#define QUEUE_FLAG_PLUGGED	7	/* queue is plugged */
#define QUEUE_FLAG_ELVSWITCH	8	/* don't use elevator, just do FIFO */
#define QUEUE_FLAG_BIDI		9	/* queue supports bidi requests */
#define QUEUE_FLAG_NOMERGES    10	/* disable merge attempts */

static inline int queue_is_locked(struct request_queue *q)
{
#ifdef CONFIG_SMP
	spinlock_t *lock = q->queue_lock;
	return lock && spin_is_locked(lock);
#else
	return 1;
#endif
	 * for flush operations
	 */
	unsigned int		flush_flags;
	unsigned int		flush_not_queueable:1;
	struct blk_flush_queue	*fq;

	struct list_head	requeue_list;
	spinlock_t		requeue_lock;
	struct work_struct	requeue_work;

	struct mutex		sysfs_lock;

	int			bypass_depth;
	atomic_t		mq_freeze_depth;

#if defined(CONFIG_BLK_DEV_BSG)
	bsg_job_fn		*bsg_job_fn;
	int			bsg_job_size;
	struct bsg_class_device bsg_dev;
#endif

#ifdef CONFIG_BLK_DEV_THROTTLING
	/* Throttle data */
	struct throtl_data *td;
#endif
	struct rcu_head		rcu_head;
	struct swait_queue_head	mq_freeze_wq;
	struct percpu_ref	q_usage_counter;
	struct list_head	all_q_node;

	struct blk_mq_tag_set	*tag_set;
	struct list_head	tag_set_list;
	struct bio_set		*bio_split;

	bool			mq_sysfs_init_done;
};

#define QUEUE_FLAG_QUEUED	1	/* uses generic tag queueing */
#define QUEUE_FLAG_STOPPED	2	/* queue is stopped */
#define	QUEUE_FLAG_SYNCFULL	3	/* read queue has been filled */
#define QUEUE_FLAG_ASYNCFULL	4	/* write queue has been filled */
#define QUEUE_FLAG_DYING	5	/* queue being torn down */
#define QUEUE_FLAG_BYPASS	6	/* act as dumb FIFO queue */
#define QUEUE_FLAG_BIDI		7	/* queue supports bidi requests */
#define QUEUE_FLAG_NOMERGES     8	/* disable merge attempts */
#define QUEUE_FLAG_SAME_COMP	9	/* complete on same CPU-group */
#define QUEUE_FLAG_FAIL_IO     10	/* fake timeout */
#define QUEUE_FLAG_STACKABLE   11	/* supports request stacking */
#define QUEUE_FLAG_NONROT      12	/* non-rotational device (SSD) */
#define QUEUE_FLAG_VIRT        QUEUE_FLAG_NONROT /* paravirt device */
#define QUEUE_FLAG_IO_STAT     13	/* do IO stats */
#define QUEUE_FLAG_DISCARD     14	/* supports DISCARD */
#define QUEUE_FLAG_NOXMERGES   15	/* No extended merges */
#define QUEUE_FLAG_ADD_RANDOM  16	/* Contributes to random pool */
#define QUEUE_FLAG_SECDISCARD  17	/* supports SECDISCARD */
#define QUEUE_FLAG_SAME_FORCE  18	/* force complete on same CPU */
#define QUEUE_FLAG_DEAD        19	/* queue tear-down finished */
#define QUEUE_FLAG_INIT_DONE   20	/* queue is initialized */
#define QUEUE_FLAG_NO_SG_MERGE 21	/* don't attempt to merge SG segments*/
#define QUEUE_FLAG_POLL	       22	/* IO polling enabled if set */

#define QUEUE_FLAG_DEFAULT	((1 << QUEUE_FLAG_IO_STAT) |		\
				 (1 << QUEUE_FLAG_STACKABLE)	|	\
				 (1 << QUEUE_FLAG_SAME_COMP)	|	\
				 (1 << QUEUE_FLAG_ADD_RANDOM))

#define QUEUE_FLAG_MQ_DEFAULT	((1 << QUEUE_FLAG_IO_STAT) |		\
				 (1 << QUEUE_FLAG_STACKABLE)	|	\
				 (1 << QUEUE_FLAG_SAME_COMP))

static inline void queue_lockdep_assert_held(struct request_queue *q)
{
	if (q->queue_lock)
		lockdep_assert_held(q->queue_lock);
}

static inline void queue_flag_set_unlocked(unsigned int flag,
					   struct request_queue *q)
{
	__set_bit(flag, &q->queue_flags);
}

static inline int queue_flag_test_and_clear(unsigned int flag,
					    struct request_queue *q)
{
	WARN_ON_ONCE(!queue_is_locked(q));
	queue_lockdep_assert_held(q);

	if (test_bit(flag, &q->queue_flags)) {
		__clear_bit(flag, &q->queue_flags);
		return 1;
	}

	return 0;
}

static inline int queue_flag_test_and_set(unsigned int flag,
					  struct request_queue *q)
{
	WARN_ON_ONCE(!queue_is_locked(q));
	queue_lockdep_assert_held(q);

	if (!test_bit(flag, &q->queue_flags)) {
		__set_bit(flag, &q->queue_flags);
		return 0;
	}

	return 1;
}

static inline void queue_flag_set(unsigned int flag, struct request_queue *q)
{
	WARN_ON_ONCE(!queue_is_locked(q));
	queue_lockdep_assert_held(q);
	__set_bit(flag, &q->queue_flags);
}

static inline void queue_flag_clear_unlocked(unsigned int flag,
					     struct request_queue *q)
{
	__clear_bit(flag, &q->queue_flags);
}

static inline void queue_flag_clear(unsigned int flag, struct request_queue *q)
{
	WARN_ON_ONCE(!queue_is_locked(q));
	__clear_bit(flag, &q->queue_flags);
}

enum {
	/*
	 * Hardbarrier is supported with one of the following methods.
	 *
	 * NONE		: hardbarrier unsupported
	 * DRAIN	: ordering by draining is enough
	 * DRAIN_FLUSH	: ordering by draining w/ pre and post flushes
	 * DRAIN_FUA	: ordering by draining w/ pre flush and FUA write
	 * TAG		: ordering by tag is enough
	 * TAG_FLUSH	: ordering by tag w/ pre and post flushes
	 * TAG_FUA	: ordering by tag w/ pre flush and FUA write
	 */
	QUEUE_ORDERED_NONE	= 0x00,
	QUEUE_ORDERED_DRAIN	= 0x01,
	QUEUE_ORDERED_TAG	= 0x02,

	QUEUE_ORDERED_PREFLUSH	= 0x10,
	QUEUE_ORDERED_POSTFLUSH	= 0x20,
	QUEUE_ORDERED_FUA	= 0x40,

	QUEUE_ORDERED_DRAIN_FLUSH = QUEUE_ORDERED_DRAIN |
			QUEUE_ORDERED_PREFLUSH | QUEUE_ORDERED_POSTFLUSH,
	QUEUE_ORDERED_DRAIN_FUA	= QUEUE_ORDERED_DRAIN |
			QUEUE_ORDERED_PREFLUSH | QUEUE_ORDERED_FUA,
	QUEUE_ORDERED_TAG_FLUSH	= QUEUE_ORDERED_TAG |
			QUEUE_ORDERED_PREFLUSH | QUEUE_ORDERED_POSTFLUSH,
	QUEUE_ORDERED_TAG_FUA	= QUEUE_ORDERED_TAG |
			QUEUE_ORDERED_PREFLUSH | QUEUE_ORDERED_FUA,

	/*
	 * Ordered operation sequence
	 */
	QUEUE_ORDSEQ_STARTED	= 0x01,	/* flushing in progress */
	QUEUE_ORDSEQ_DRAIN	= 0x02,	/* waiting for the queue to be drained */
	QUEUE_ORDSEQ_PREFLUSH	= 0x04,	/* pre-flushing in progress */
	QUEUE_ORDSEQ_BAR	= 0x08,	/* original barrier req in progress */
	QUEUE_ORDSEQ_POSTFLUSH	= 0x10,	/* post-flushing in progress */
	QUEUE_ORDSEQ_DONE	= 0x20,
};

#define blk_queue_plugged(q)	test_bit(QUEUE_FLAG_PLUGGED, &(q)->queue_flags)
#define blk_queue_tagged(q)	test_bit(QUEUE_FLAG_QUEUED, &(q)->queue_flags)
#define blk_queue_stopped(q)	test_bit(QUEUE_FLAG_STOPPED, &(q)->queue_flags)
#define blk_queue_nomerges(q)	test_bit(QUEUE_FLAG_NOMERGES, &(q)->queue_flags)
#define blk_queue_flushing(q)	((q)->ordseq)

#define blk_fs_request(rq)	((rq)->cmd_type == REQ_TYPE_FS)
#define blk_pc_request(rq)	((rq)->cmd_type == REQ_TYPE_BLOCK_PC)
#define blk_special_request(rq)	((rq)->cmd_type == REQ_TYPE_SPECIAL)
#define blk_sense_request(rq)	((rq)->cmd_type == REQ_TYPE_SENSE)

#define blk_noretry_request(rq)	((rq)->cmd_flags & REQ_FAILFAST)
#define blk_rq_started(rq)	((rq)->cmd_flags & REQ_STARTED)

#define blk_account_rq(rq)	(blk_rq_started(rq) && blk_fs_request(rq))

#define blk_pm_suspend_request(rq)	((rq)->cmd_type == REQ_TYPE_PM_SUSPEND)
#define blk_pm_resume_request(rq)	((rq)->cmd_type == REQ_TYPE_PM_RESUME)
#define blk_pm_request(rq)	\
	(blk_pm_suspend_request(rq) || blk_pm_resume_request(rq))

#define blk_sorted_rq(rq)	((rq)->cmd_flags & REQ_SORTED)
#define blk_barrier_rq(rq)	((rq)->cmd_flags & REQ_HARDBARRIER)
#define blk_fua_rq(rq)		((rq)->cmd_flags & REQ_FUA)
#define blk_bidi_rq(rq)		((rq)->next_rq != NULL)
#define blk_empty_barrier(rq)	(blk_barrier_rq(rq) && blk_fs_request(rq) && !(rq)->hard_nr_sectors)
static inline int queue_in_flight(struct request_queue *q)
{
	return q->in_flight[0] + q->in_flight[1];
}

static inline void queue_flag_clear(unsigned int flag, struct request_queue *q)
{
	queue_lockdep_assert_held(q);
	__clear_bit(flag, &q->queue_flags);
}

#define blk_queue_tagged(q)	test_bit(QUEUE_FLAG_QUEUED, &(q)->queue_flags)
#define blk_queue_stopped(q)	test_bit(QUEUE_FLAG_STOPPED, &(q)->queue_flags)
#define blk_queue_dying(q)	test_bit(QUEUE_FLAG_DYING, &(q)->queue_flags)
#define blk_queue_dead(q)	test_bit(QUEUE_FLAG_DEAD, &(q)->queue_flags)
#define blk_queue_bypass(q)	test_bit(QUEUE_FLAG_BYPASS, &(q)->queue_flags)
#define blk_queue_init_done(q)	test_bit(QUEUE_FLAG_INIT_DONE, &(q)->queue_flags)
#define blk_queue_nomerges(q)	test_bit(QUEUE_FLAG_NOMERGES, &(q)->queue_flags)
#define blk_queue_noxmerges(q)	\
	test_bit(QUEUE_FLAG_NOXMERGES, &(q)->queue_flags)
#define blk_queue_nonrot(q)	test_bit(QUEUE_FLAG_NONROT, &(q)->queue_flags)
#define blk_queue_io_stat(q)	test_bit(QUEUE_FLAG_IO_STAT, &(q)->queue_flags)
#define blk_queue_add_random(q)	test_bit(QUEUE_FLAG_ADD_RANDOM, &(q)->queue_flags)
#define blk_queue_stackable(q)	\
	test_bit(QUEUE_FLAG_STACKABLE, &(q)->queue_flags)
#define blk_queue_discard(q)	test_bit(QUEUE_FLAG_DISCARD, &(q)->queue_flags)
#define blk_queue_secdiscard(q)	(blk_queue_discard(q) && \
	test_bit(QUEUE_FLAG_SECDISCARD, &(q)->queue_flags))

#define blk_noretry_request(rq) \
	((rq)->cmd_flags & (REQ_FAILFAST_DEV|REQ_FAILFAST_TRANSPORT| \
			     REQ_FAILFAST_DRIVER))

static inline bool blk_account_rq(struct request *rq)
{
	return (rq->cmd_flags & REQ_STARTED) && !blk_rq_is_passthrough(rq);
}

#define blk_rq_cpu_valid(rq)	((rq)->cpu != -1)
#define blk_bidi_rq(rq)		((rq)->next_rq != NULL)
/* rq->queuelist of dequeued request must be list_empty() */
#define blk_queued_rq(rq)	(!list_empty(&(rq)->queuelist))

#define list_entry_rq(ptr)	list_entry((ptr), struct request, queuelist)

#define rq_data_dir(rq)		((rq)->cmd_flags & 1)

/*
 * We regard a request as sync, if it's a READ or a SYNC write.
 */
#define rq_is_sync(rq)		(rq_data_dir((rq)) == READ || (rq)->cmd_flags & REQ_RW_SYNC)
#define rq_is_meta(rq)		((rq)->cmd_flags & REQ_RW_META)

static inline int blk_queue_full(struct request_queue *q, int rw)
{
	if (rw == READ)
		return test_bit(QUEUE_FLAG_READFULL, &q->queue_flags);
	return test_bit(QUEUE_FLAG_WRITEFULL, &q->queue_flags);
}

static inline void blk_set_queue_full(struct request_queue *q, int rw)
{
	if (rw == READ)
		queue_flag_set(QUEUE_FLAG_READFULL, q);
	else
		queue_flag_set(QUEUE_FLAG_WRITEFULL, q);
}

static inline void blk_clear_queue_full(struct request_queue *q, int rw)
{
	if (rw == READ)
		queue_flag_clear(QUEUE_FLAG_READFULL, q);
	else
		queue_flag_clear(QUEUE_FLAG_WRITEFULL, q);
}


/*
 * mergeable request must not have _NOMERGE or _BARRIER bit set, nor may
 * it already be started by driver.
 */
#define RQ_NOMERGE_FLAGS	\
	(REQ_NOMERGE | REQ_STARTED | REQ_HARDBARRIER | REQ_SOFTBARRIER)
#define rq_mergeable(rq)	\
	(!((rq)->cmd_flags & RQ_NOMERGE_FLAGS) && blk_fs_request((rq)))
#define rq_data_dir(rq)		((int)((rq)->cmd_flags & 1))

/*
 * Driver can handle struct request, if it either has an old style
 * request_fn defined, or is blk-mq based.
 */
static inline bool queue_is_rq_based(struct request_queue *q)
{
	return q->request_fn || q->mq_ops;
}

static inline unsigned int blk_queue_cluster(struct request_queue *q)
{
	return q->limits.cluster;
}

/*
 * We regard a request as sync, if either a read or a sync write
 */
static inline bool rw_is_sync(unsigned int rw_flags)
{
	return !(rw_flags & REQ_WRITE) || (rw_flags & REQ_SYNC);
}

static inline bool rq_is_sync(struct request *rq)
{
	return rw_is_sync(rq->cmd_flags);
}

static inline bool blk_rl_full(struct request_list *rl, bool sync)
{
	unsigned int flag = sync ? BLK_RL_SYNCFULL : BLK_RL_ASYNCFULL;

	return rl->flags & flag;
}

static inline void blk_set_rl_full(struct request_list *rl, bool sync)
{
	unsigned int flag = sync ? BLK_RL_SYNCFULL : BLK_RL_ASYNCFULL;

	rl->flags |= flag;
}

static inline void blk_clear_rl_full(struct request_list *rl, bool sync)
{
	unsigned int flag = sync ? BLK_RL_SYNCFULL : BLK_RL_ASYNCFULL;

	rl->flags &= ~flag;
}

static inline bool rq_mergeable(struct request *rq)
{
	if (blk_rq_is_passthrough(rq))
		return false;

	if (rq->cmd_flags & REQ_NOMERGE_FLAGS)
		return false;

	return true;
}

static inline bool blk_check_merge_flags(unsigned int flags1,
					 unsigned int flags2)
{
	if ((flags1 & REQ_DISCARD) != (flags2 & REQ_DISCARD))
		return false;

	if ((flags1 & REQ_SECURE) != (flags2 & REQ_SECURE))
		return false;

	if ((flags1 & REQ_WRITE_SAME) != (flags2 & REQ_WRITE_SAME))
		return false;

	return true;
}

static inline bool blk_write_same_mergeable(struct bio *a, struct bio *b)
{
	if (bio_data(a) == bio_data(b))
		return true;

	return false;
}

/*
 * q->prep_rq_fn return values
 */
#define BLKPREP_OK		0	/* serve it */
#define BLKPREP_KILL		1	/* fatal error, kill */
#define BLKPREP_DEFER		2	/* leave on queue */

extern unsigned long blk_max_low_pfn, blk_max_pfn;

/*
 * standard bounce addresses:
 *
 * BLK_BOUNCE_HIGH	: bounce all highmem pages
 * BLK_BOUNCE_ANY	: don't bounce anything
 * BLK_BOUNCE_ISA	: bounce pages above ISA DMA boundary
 */

#if BITS_PER_LONG == 32
#define BLK_BOUNCE_HIGH		((u64)blk_max_low_pfn << PAGE_SHIFT)
#else
#define BLK_BOUNCE_HIGH		-1ULL
#endif
#define BLK_BOUNCE_ANY		(-1ULL)
#define BLK_BOUNCE_ISA		(ISA_DMA_THRESHOLD)
#define BLK_BOUNCE_ISA		(DMA_BIT_MASK(24))

/*
 * default timeout for SG_IO if none specified
 */
#define BLK_DEFAULT_SG_TIMEOUT	(60 * HZ)
#define BLK_MIN_SG_TIMEOUT	(7 * HZ)

#ifdef CONFIG_BOUNCE
extern int init_emergency_isa_pool(void);
extern void blk_queue_bounce(struct request_queue *q, struct bio **bio);
#else
static inline int init_emergency_isa_pool(void)
{
	return 0;
}
static inline void blk_queue_bounce(struct request_queue *q, struct bio **bio)
{
}
#endif /* CONFIG_MMU */

struct req_iterator {
	int i;
struct rq_map_data {
	struct page **pages;
	int page_order;
	int nr_entries;
	unsigned long offset;
	int null_mapped;
	int from_user;
};

struct req_iterator {
	struct bvec_iter iter;
	struct bio *bio;
};

/* This should not be used directly - use rq_for_each_segment */
#define for_each_bio(_bio)		\
	for (; _bio; _bio = _bio->bi_next)
#define __rq_for_each_bio(_bio, rq)	\
	if ((rq->bio))			\
		for (_bio = (rq)->bio; _bio; _bio = _bio->bi_next)

#define rq_for_each_segment(bvl, _rq, _iter)			\
	__rq_for_each_bio(_iter.bio, _rq)			\
		bio_for_each_segment(bvl, _iter.bio, _iter.i)

#define rq_iter_last(rq, _iter)					\
		(_iter.bio->bi_next == NULL && _iter.i == _iter.bio->bi_vcnt-1)

extern int blk_register_queue(struct gendisk *disk);
extern void blk_unregister_queue(struct gendisk *disk);
extern void register_disk(struct gendisk *dev);
extern void generic_make_request(struct bio *bio);
		bio_for_each_segment(bvl, _iter.bio, _iter.iter)

#define rq_iter_last(bvec, _iter)				\
		(_iter.bio->bi_next == NULL &&			\
		 bio_iter_last(bvec, _iter.iter))

#ifndef ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE
# error	"You should define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE for your platform"
#endif
#if ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE
extern void rq_flush_dcache_pages(struct request *rq);
#else
static inline void rq_flush_dcache_pages(struct request *rq)
{
}
#endif

extern int blk_register_queue(struct gendisk *disk);
extern void blk_unregister_queue(struct gendisk *disk);
extern blk_qc_t generic_make_request(struct bio *bio);
extern void blk_rq_init(struct request_queue *q, struct request *rq);
extern void blk_put_request(struct request *);
extern void __blk_put_request(struct request_queue *, struct request *);
extern struct request *blk_get_request(struct request_queue *, int, gfp_t);
extern void blk_insert_request(struct request_queue *, struct request *, int, void *);
extern void blk_requeue_request(struct request_queue *, struct request *);
extern void blk_plug_device(struct request_queue *);
extern void blk_plug_device_unlocked(struct request_queue *);
extern int blk_remove_plug(struct request_queue *);
extern void blk_recount_segments(struct request_queue *, struct bio *);
extern int scsi_cmd_ioctl(struct file *, struct request_queue *,
			  struct gendisk *, unsigned int, void __user *);
extern int sg_scsi_ioctl(struct file *, struct request_queue *,
		struct gendisk *, struct scsi_ioctl_command __user *);

/*
 * Temporary export, until SCSI gets fixed up.
 */
extern int blk_rq_append_bio(struct request_queue *q, struct request *rq,
			     struct bio *bio);

/*
 * A queue has just exitted congestion.  Note this in the global counter of
 * congested queues, and wake up anyone who was waiting for requests to be
 * put back.
 */
static inline void blk_clear_queue_congested(struct request_queue *q, int rw)
{
	clear_bdi_congested(&q->backing_dev_info, rw);
}

/*
 * A queue has just entered congestion.  Flag that in the queue's VM-visible
 * state flags and increment the global gounter of congested queues.
 */
static inline void blk_set_queue_congested(struct request_queue *q, int rw)
{
	set_bdi_congested(&q->backing_dev_info, rw);
}

extern void blk_start_queue(struct request_queue *q);
extern void blk_stop_queue(struct request_queue *q);
extern void blk_sync_queue(struct request_queue *q);
extern void __blk_stop_queue(struct request_queue *q);
extern void __blk_run_queue(struct request_queue *);
extern void blk_run_queue(struct request_queue *);
extern void blk_start_queueing(struct request_queue *);
extern int blk_rq_map_user(struct request_queue *, struct request *, void __user *, unsigned long);
extern int blk_rq_unmap_user(struct bio *);
extern int blk_rq_map_kern(struct request_queue *, struct request *, void *, unsigned int, gfp_t);
extern int blk_rq_map_user_iov(struct request_queue *, struct request *,
			       struct sg_iovec *, int, unsigned int);
extern struct request *blk_make_request(struct request_queue *, struct bio *,
					gfp_t);
extern void blk_rq_set_block_pc(struct request *);
extern void blk_requeue_request(struct request_queue *, struct request *);
extern void blk_add_request_payload(struct request *rq, struct page *page,
		unsigned int len);
extern int blk_lld_busy(struct request_queue *q);
extern int blk_rq_prep_clone(struct request *rq, struct request *rq_src,
			     struct bio_set *bs, gfp_t gfp_mask,
			     int (*bio_ctr)(struct bio *, struct bio *, void *),
			     void *data);
extern void blk_rq_unprep_clone(struct request *rq);
extern int blk_insert_cloned_request(struct request_queue *q,
				     struct request *rq);
extern void blk_delay_queue(struct request_queue *, unsigned long);
extern void blk_queue_split(struct request_queue *, struct bio **,
			    struct bio_set *);
extern void blk_recount_segments(struct request_queue *, struct bio *);
extern int scsi_verify_blk_ioctl(struct block_device *, unsigned int);
extern int scsi_cmd_blk_ioctl(struct block_device *, fmode_t,
			      unsigned int, void __user *);
extern int scsi_cmd_ioctl(struct request_queue *, struct gendisk *, fmode_t,
			  unsigned int, void __user *);
extern int sg_scsi_ioctl(struct request_queue *, struct gendisk *, fmode_t,
			 struct scsi_ioctl_command __user *);

extern int blk_queue_enter(struct request_queue *q, gfp_t gfp);
extern void blk_queue_exit(struct request_queue *q);
extern void blk_start_queue(struct request_queue *q);
extern void blk_start_queue_async(struct request_queue *q);
extern void blk_stop_queue(struct request_queue *q);
extern void blk_sync_queue(struct request_queue *q);
extern void __blk_stop_queue(struct request_queue *q);
extern void __blk_run_queue(struct request_queue *q);
extern void __blk_run_queue_uncond(struct request_queue *q);
extern void blk_run_queue(struct request_queue *);
extern void blk_run_queue_async(struct request_queue *q);
extern int blk_rq_map_user(struct request_queue *, struct request *,
			   struct rq_map_data *, void __user *, unsigned long,
			   gfp_t);
extern int blk_rq_unmap_user(struct bio *);
extern int blk_rq_map_kern(struct request_queue *, struct request *, void *, unsigned int, gfp_t);
extern int blk_rq_map_user_iov(struct request_queue *, struct request *,
			       struct rq_map_data *, const struct iov_iter *,
			       gfp_t);
extern int blk_execute_rq(struct request_queue *, struct gendisk *,
			  struct request *, int);
extern void blk_execute_rq_nowait(struct request_queue *, struct gendisk *,
				  struct request *, int, rq_end_io_fn *);
extern void blk_unplug(struct request_queue *q);

static inline struct request_queue *bdev_get_queue(struct block_device *bdev)
{
	return bdev->bd_disk->queue;
}

static inline void blk_run_backing_dev(struct backing_dev_info *bdi,
				       struct page *page)
{
	if (bdi && bdi->unplug_io_fn)
		bdi->unplug_io_fn(bdi, page);
}

static inline void blk_run_address_space(struct address_space *mapping)
{
	if (mapping)
		blk_run_backing_dev(mapping->backing_dev_info, NULL);
}

/*
 * blk_end_request() and friends.
 * __blk_end_request() and end_request() must be called with
 * the request queue spinlock acquired.

bool blk_poll(struct request_queue *q, blk_qc_t cookie);

static inline struct request_queue *bdev_get_queue(struct block_device *bdev)
{
	return bdev->bd_disk->queue;	/* this is never NULL */
}

/*
 * The basic unit of block I/O is a sector. It is used in a number of contexts
 * in Linux (blk, bio, genhd). The size of one sector is 512 = 2**9
 * bytes. Variables of type sector_t represent an offset or size that is a
 * multiple of 512 bytes. Hence these two constants.
 */
#ifndef SECTOR_SHIFT
#define SECTOR_SHIFT 9
#endif
#ifndef SECTOR_SIZE
#define SECTOR_SIZE (1 << SECTOR_SHIFT)
#endif

/*
 * blk_rq_pos()			: the current sector
 * blk_rq_bytes()		: bytes left in the entire request
 * blk_rq_cur_bytes()		: bytes left in the current segment
 * blk_rq_err_bytes()		: bytes left till the next error boundary
 * blk_rq_sectors()		: sectors left in the entire request
 * blk_rq_cur_sectors()		: sectors left in the current segment
 */
static inline sector_t blk_rq_pos(const struct request *rq)
{
	return rq->__sector;
}

static inline unsigned int blk_rq_bytes(const struct request *rq)
{
	return rq->__data_len;
}

static inline int blk_rq_cur_bytes(const struct request *rq)
{
	return rq->bio ? bio_cur_bytes(rq->bio) : 0;
}

extern unsigned int blk_rq_err_bytes(const struct request *rq);

static inline unsigned int blk_rq_sectors(const struct request *rq)
{
	return blk_rq_bytes(rq) >> SECTOR_SHIFT;
}

static inline unsigned int blk_rq_cur_sectors(const struct request *rq)
{
	return blk_rq_cur_bytes(rq) >> SECTOR_SHIFT;
}

static inline unsigned int blk_queue_get_max_sectors(struct request_queue *q,
						     unsigned int cmd_flags)
{
	if (unlikely(cmd_flags & REQ_DISCARD))
		return min(q->limits.max_discard_sectors,
			   UINT_MAX >> SECTOR_SHIFT);

	if (unlikely(cmd_flags & REQ_WRITE_SAME))
		return q->limits.max_write_same_sectors;

	return q->limits.max_sectors;
}

/*
 * Return maximum size of a request at given offset. Only valid for
 * file system requests.
 */
static inline unsigned int blk_max_size_offset(struct request_queue *q,
					       sector_t offset)
{
	if (!q->limits.chunk_sectors)
		return q->limits.max_sectors;

	return min(q->limits.max_sectors, (unsigned int)(q->limits.chunk_sectors -
			(offset & (q->limits.chunk_sectors - 1))));
}

static inline unsigned int blk_rq_get_max_sectors(struct request *rq)
{
	struct request_queue *q = rq->q;

	if (blk_rq_is_passthrough(rq))
		return q->limits.max_hw_sectors;

	if (!q->limits.chunk_sectors || (rq->cmd_flags & REQ_DISCARD))
		return blk_queue_get_max_sectors(q, rq->cmd_flags);

	return min(blk_max_size_offset(q, blk_rq_pos(rq)),
			blk_queue_get_max_sectors(q, rq->cmd_flags));
}

static inline unsigned int blk_rq_count_bios(struct request *rq)
{
	unsigned int nr_bios = 0;
	struct bio *bio;

	__rq_for_each_bio(bio, rq)
		nr_bios++;

	return nr_bios;
}

/*
 * Request issue related functions.
 */
extern struct request *blk_peek_request(struct request_queue *q);
extern void blk_start_request(struct request *rq);
extern struct request *blk_fetch_request(struct request_queue *q);

/*
 * Request completion related functions.
 *
 * blk_update_request() completes given number of bytes and updates
 * the request without completing it.
 *
 * blk_end_request() and friends.  __blk_end_request() must be called
 * with the request queue spinlock acquired.
 *
 * Several drivers define their own end_request and call
 * blk_end_request() for parts of the original function.
 * This prevents code duplication in drivers.
 */
extern int blk_end_request(struct request *rq, int error,
				unsigned int nr_bytes);
extern int __blk_end_request(struct request *rq, int error,
				unsigned int nr_bytes);
extern int blk_end_bidi_request(struct request *rq, int error,
				unsigned int nr_bytes, unsigned int bidi_bytes);
extern void end_request(struct request *, int);
extern void end_queued_request(struct request *, int);
extern void end_dequeued_request(struct request *, int);
extern int blk_end_request_callback(struct request *rq, int error,
				unsigned int nr_bytes,
				int (drv_callback)(struct request *));
extern void blk_complete_request(struct request *);

/*
 * blk_end_request() takes bytes instead of sectors as a complete size.
 * blk_rq_bytes() returns bytes left to complete in the entire request.
 * blk_rq_cur_bytes() returns bytes left to complete in the current segment.
 */
extern unsigned int blk_rq_bytes(struct request *rq);
extern unsigned int blk_rq_cur_bytes(struct request *rq);

static inline void blkdev_dequeue_request(struct request *req)
{
	elv_dequeue_request(req->q, req);
}
extern bool blk_update_request(struct request *rq, int error,
			       unsigned int nr_bytes);
extern void blk_finish_request(struct request *rq, int error);
extern bool blk_end_request(struct request *rq, int error,
			    unsigned int nr_bytes);
extern void blk_end_request_all(struct request *rq, int error);
extern bool blk_end_request_cur(struct request *rq, int error);
extern bool blk_end_request_err(struct request *rq, int error);
extern bool __blk_end_request(struct request *rq, int error,
			      unsigned int nr_bytes);
extern void __blk_end_request_all(struct request *rq, int error);
extern bool __blk_end_request_cur(struct request *rq, int error);
extern bool __blk_end_request_err(struct request *rq, int error);

extern void blk_complete_request(struct request *);
extern void __blk_complete_request(struct request *);
extern void blk_abort_request(struct request *);
extern void blk_unprep_request(struct request *);

/*
 * Access functions for manipulating queue properties
 */
extern struct request_queue *blk_init_queue_node(request_fn_proc *rfn,
					spinlock_t *lock, int node_id);
extern struct request_queue *blk_init_queue(request_fn_proc *, spinlock_t *);
extern void blk_cleanup_queue(struct request_queue *);
extern void blk_queue_make_request(struct request_queue *, make_request_fn *);
extern void blk_queue_bounce_limit(struct request_queue *, u64);
extern void blk_queue_max_sectors(struct request_queue *, unsigned int);
extern void blk_queue_max_phys_segments(struct request_queue *, unsigned short);
extern void blk_queue_max_hw_segments(struct request_queue *, unsigned short);
extern void blk_queue_max_segment_size(struct request_queue *, unsigned int);
extern void blk_queue_hardsect_size(struct request_queue *, unsigned short);
extern struct request_queue *blk_init_allocated_queue(struct request_queue *,
						      request_fn_proc *, spinlock_t *);
extern void blk_cleanup_queue(struct request_queue *);
extern void blk_queue_make_request(struct request_queue *, make_request_fn *);
extern void blk_queue_bounce_limit(struct request_queue *, u64);
extern void blk_queue_max_hw_sectors(struct request_queue *, unsigned int);
extern void blk_queue_chunk_sectors(struct request_queue *, unsigned int);
extern void blk_queue_max_segments(struct request_queue *, unsigned short);
extern void blk_queue_max_segment_size(struct request_queue *, unsigned int);
extern void blk_queue_max_discard_sectors(struct request_queue *q,
		unsigned int max_discard_sectors);
extern void blk_queue_max_write_same_sectors(struct request_queue *q,
		unsigned int max_write_same_sectors);
extern void blk_queue_logical_block_size(struct request_queue *, unsigned int);
extern void blk_queue_physical_block_size(struct request_queue *, unsigned int);
extern void blk_queue_alignment_offset(struct request_queue *q,
				       unsigned int alignment);
extern void blk_limits_io_min(struct queue_limits *limits, unsigned int min);
extern void blk_queue_io_min(struct request_queue *q, unsigned int min);
extern void blk_limits_io_opt(struct queue_limits *limits, unsigned int opt);
extern void blk_queue_io_opt(struct request_queue *q, unsigned int opt);
extern void blk_set_default_limits(struct queue_limits *lim);
extern void blk_set_stacking_limits(struct queue_limits *lim);
extern int blk_stack_limits(struct queue_limits *t, struct queue_limits *b,
			    sector_t offset);
extern int bdev_stack_limits(struct queue_limits *t, struct block_device *bdev,
			    sector_t offset);
extern void disk_stack_limits(struct gendisk *disk, struct block_device *bdev,
			      sector_t offset);
extern void blk_queue_stack_limits(struct request_queue *t, struct request_queue *b);
extern void blk_queue_dma_pad(struct request_queue *, unsigned int);
extern void blk_queue_update_dma_pad(struct request_queue *, unsigned int);
extern int blk_queue_dma_drain(struct request_queue *q,
			       dma_drain_needed_fn *dma_drain_needed,
			       void *buf, unsigned int size);
extern void blk_queue_segment_boundary(struct request_queue *, unsigned long);
extern void blk_queue_prep_rq(struct request_queue *, prep_rq_fn *pfn);
extern void blk_queue_merge_bvec(struct request_queue *, merge_bvec_fn *);
extern void blk_queue_dma_alignment(struct request_queue *, int);
extern void blk_queue_update_dma_alignment(struct request_queue *, int);
extern void blk_queue_softirq_done(struct request_queue *, softirq_done_fn *);
extern struct backing_dev_info *blk_get_backing_dev_info(struct block_device *bdev);
extern int blk_queue_ordered(struct request_queue *, unsigned, prepare_flush_fn *);
extern int blk_do_ordered(struct request_queue *, struct request **);
extern unsigned blk_ordered_cur_seq(struct request_queue *);
extern unsigned blk_ordered_req_seq(struct request *);
extern void blk_ordered_complete_seq(struct request_queue *, unsigned, int);

extern int blk_rq_map_sg(struct request_queue *, struct request *, struct scatterlist *);
extern void blk_dump_rq_flags(struct request *, char *);
extern void generic_unplug_device(struct request_queue *);
extern void __generic_unplug_device(struct request_queue *);
extern long nr_blockdev_pages(void);

int blk_get_queue(struct request_queue *);
struct request_queue *blk_alloc_queue(gfp_t);
struct request_queue *blk_alloc_queue_node(gfp_t, int);
extern void blk_put_queue(struct request_queue *);
extern void blk_queue_lld_busy(struct request_queue *q, lld_busy_fn *fn);
extern void blk_queue_segment_boundary(struct request_queue *, unsigned long);
extern void blk_queue_virt_boundary(struct request_queue *, unsigned long);
extern void blk_queue_prep_rq(struct request_queue *, prep_rq_fn *pfn);
extern void blk_queue_unprep_rq(struct request_queue *, unprep_rq_fn *ufn);
extern void blk_queue_dma_alignment(struct request_queue *, int);
extern void blk_queue_update_dma_alignment(struct request_queue *, int);
extern void blk_queue_softirq_done(struct request_queue *, softirq_done_fn *);
extern void blk_queue_rq_timed_out(struct request_queue *, rq_timed_out_fn *);
extern void blk_queue_rq_timeout(struct request_queue *, unsigned int);
extern void blk_queue_flush(struct request_queue *q, unsigned int flush);
extern void blk_queue_flush_queueable(struct request_queue *q, bool queueable);
extern struct backing_dev_info *blk_get_backing_dev_info(struct block_device *bdev);

extern int blk_rq_map_sg(struct request_queue *, struct request *, struct scatterlist *);
extern void blk_dump_rq_flags(struct request *, char *);
extern long nr_blockdev_pages(void);

bool __must_check blk_get_queue(struct request_queue *);
struct request_queue *blk_alloc_queue(gfp_t);
struct request_queue *blk_alloc_queue_node(gfp_t, int);
extern void blk_put_queue(struct request_queue *);
extern void blk_set_queue_dying(struct request_queue *);

/*
 * block layer runtime pm functions
 */
#ifdef CONFIG_PM
extern void blk_pm_runtime_init(struct request_queue *q, struct device *dev);
extern int blk_pre_runtime_suspend(struct request_queue *q);
extern void blk_post_runtime_suspend(struct request_queue *q, int err);
extern void blk_pre_runtime_resume(struct request_queue *q);
extern void blk_post_runtime_resume(struct request_queue *q, int err);
#else
static inline void blk_pm_runtime_init(struct request_queue *q,
	struct device *dev) {}
static inline int blk_pre_runtime_suspend(struct request_queue *q)
{
	return -ENOSYS;
}
static inline void blk_post_runtime_suspend(struct request_queue *q, int err) {}
static inline void blk_pre_runtime_resume(struct request_queue *q) {}
static inline void blk_post_runtime_resume(struct request_queue *q, int err) {}
#endif

/*
 * blk_plug permits building a queue of related requests by holding the I/O
 * fragments for a short period. This allows merging of sequential requests
 * into single larger request. As the requests are moved from a per-task list to
 * the device's request_queue in a batch, this results in improved scalability
 * as the lock contention for request_queue lock is reduced.
 *
 * It is ok not to disable preemption when adding the request to the plug list
 * or when attempting a merge, because blk_schedule_flush_list() will only flush
 * the plug list when the task sleeps by itself. For details, please see
 * schedule() where blk_schedule_flush_plug() is called.
 */
struct blk_plug {
	struct list_head list; /* requests */
	struct list_head mq_list; /* blk-mq requests */
	struct list_head cb_list; /* md requires an unplug callback */
};
#define BLK_MAX_REQUEST_COUNT 16

struct blk_plug_cb;
typedef void (*blk_plug_cb_fn)(struct blk_plug_cb *, bool);
struct blk_plug_cb {
	struct list_head list;
	blk_plug_cb_fn callback;
	void *data;
};
extern struct blk_plug_cb *blk_check_plugged(blk_plug_cb_fn unplug,
					     void *data, int size);
extern void blk_start_plug(struct blk_plug *);
extern void blk_finish_plug(struct blk_plug *);
extern void blk_flush_plug_list(struct blk_plug *, bool);

static inline void blk_flush_plug(struct task_struct *tsk)
{
	struct blk_plug *plug = tsk->plug;

	if (plug)
		blk_flush_plug_list(plug, false);
}

static inline void blk_schedule_flush_plug(struct task_struct *tsk)
{
	struct blk_plug *plug = tsk->plug;

	if (plug)
		blk_flush_plug_list(plug, true);
}

static inline bool blk_needs_flush_plug(struct task_struct *tsk)
{
	struct blk_plug *plug = tsk->plug;

	return plug &&
		(!list_empty(&plug->list) ||
		 !list_empty(&plug->mq_list) ||
		 !list_empty(&plug->cb_list));
}

/*
 * tag stuff
 */
#define blk_rq_tagged(rq)		((rq)->cmd_flags & REQ_QUEUED)
extern int blk_queue_start_tag(struct request_queue *, struct request *);
extern struct request *blk_queue_find_tag(struct request_queue *, int);
extern void blk_queue_end_tag(struct request_queue *, struct request *);
extern int blk_queue_init_tags(struct request_queue *, int, struct blk_queue_tag *);
extern void blk_queue_free_tags(struct request_queue *);
extern int blk_queue_resize_tags(struct request_queue *, int);
extern void blk_queue_invalidate_tags(struct request_queue *);
extern struct blk_queue_tag *blk_init_tags(int);
extern int blk_queue_start_tag(struct request_queue *, struct request *);
extern struct request *blk_queue_find_tag(struct request_queue *, int);
extern void blk_queue_end_tag(struct request_queue *, struct request *);
extern int blk_queue_init_tags(struct request_queue *, int, struct blk_queue_tag *, int);
extern void blk_queue_free_tags(struct request_queue *);
extern int blk_queue_resize_tags(struct request_queue *, int);
extern void blk_queue_invalidate_tags(struct request_queue *);
extern struct blk_queue_tag *blk_init_tags(int, int);
extern void blk_free_tags(struct blk_queue_tag *);

static inline struct request *blk_map_queue_find_tag(struct blk_queue_tag *bqt,
						int tag)
{
	if (unlikely(bqt == NULL || tag >= bqt->real_max_depth))
		return NULL;
	return bqt->tag_index[tag];
}

extern int blkdev_issue_flush(struct block_device *, sector_t *);

/*
* command filter functions
*/
extern int blk_verify_command(struct blk_cmd_filter *filter,
			      unsigned char *cmd, int has_write_perm);
extern void blk_set_cmd_filter_defaults(struct blk_cmd_filter *filter);

#define MAX_PHYS_SEGMENTS 128
#define MAX_HW_SEGMENTS 128
#define SAFE_MAX_SECTORS 255
#define BLK_DEF_MAX_SECTORS 1024

#define MAX_SEGMENT_SIZE	65536

#define blkdev_entry_to_request(entry) list_entry((entry), struct request, queuelist)

static inline int queue_hardsect_size(struct request_queue *q)
{
	int retval = 512;

	if (q && q->hardsect_size)
		retval = q->hardsect_size;
#define BLKDEV_DISCARD_SECURE  0x01    /* secure discard */

extern int blkdev_issue_flush(struct block_device *, gfp_t, sector_t *);
extern int blkdev_issue_discard(struct block_device *bdev, sector_t sector,
		sector_t nr_sects, gfp_t gfp_mask, unsigned long flags);
extern int blkdev_issue_write_same(struct block_device *bdev, sector_t sector,
		sector_t nr_sects, gfp_t gfp_mask, struct page *page);
extern int blkdev_issue_zeroout(struct block_device *bdev, sector_t sector,
		sector_t nr_sects, gfp_t gfp_mask, bool discard);
static inline int sb_issue_discard(struct super_block *sb, sector_t block,
		sector_t nr_blocks, gfp_t gfp_mask, unsigned long flags)
{
	return blkdev_issue_discard(sb->s_bdev,
				    block << (sb->s_blocksize_bits -
					      SECTOR_SHIFT),
				    nr_blocks << (sb->s_blocksize_bits -
						  SECTOR_SHIFT),
				    gfp_mask, flags);
}
static inline int sb_issue_zeroout(struct super_block *sb, sector_t block,
		sector_t nr_blocks, gfp_t gfp_mask)
{
	return blkdev_issue_zeroout(sb->s_bdev,
				    block << (sb->s_blocksize_bits -
					      SECTOR_SHIFT),
				    nr_blocks << (sb->s_blocksize_bits -
						  SECTOR_SHIFT),
				    gfp_mask, true);
}

extern int blk_verify_command(unsigned char *cmd, fmode_t has_write_perm);

enum blk_default_limits {
	BLK_MAX_SEGMENTS	= 128,
	BLK_SAFE_MAX_SECTORS	= 255,
	BLK_DEF_MAX_SECTORS	= 2560,
	BLK_MAX_SEGMENT_SIZE	= 65536,
	BLK_SEG_BOUNDARY_MASK	= 0xFFFFFFFFUL,
};

#define blkdev_entry_to_request(entry) list_entry((entry), struct request, queuelist)

static inline unsigned long queue_bounce_pfn(struct request_queue *q)
{
	return q->limits.bounce_pfn;
}

static inline unsigned long queue_segment_boundary(struct request_queue *q)
{
	return q->limits.seg_boundary_mask;
}

static inline unsigned long queue_virt_boundary(struct request_queue *q)
{
	return q->limits.virt_boundary_mask;
}

static inline unsigned int queue_max_sectors(struct request_queue *q)
{
	return q->limits.max_sectors;
}

static inline unsigned int queue_max_hw_sectors(struct request_queue *q)
{
	return q->limits.max_hw_sectors;
}

static inline unsigned short queue_max_segments(struct request_queue *q)
{
	return q->limits.max_segments;
}

static inline unsigned int queue_max_segment_size(struct request_queue *q)
{
	return q->limits.max_segment_size;
}

static inline unsigned queue_logical_block_size(struct request_queue *q)
{
	int retval = 512;

	if (q && q->limits.logical_block_size)
		retval = q->limits.logical_block_size;

	return retval;
}

static inline int bdev_hardsect_size(struct block_device *bdev)
{
	return queue_hardsect_size(bdev_get_queue(bdev));
static inline unsigned short bdev_logical_block_size(struct block_device *bdev)
static inline unsigned int bdev_logical_block_size(struct block_device *bdev)
{
	return queue_logical_block_size(bdev_get_queue(bdev));
}

static inline unsigned int queue_physical_block_size(struct request_queue *q)
{
	return q->limits.physical_block_size;
}

static inline unsigned int bdev_physical_block_size(struct block_device *bdev)
{
	return queue_physical_block_size(bdev_get_queue(bdev));
}

static inline unsigned int queue_io_min(struct request_queue *q)
{
	return q->limits.io_min;
}

static inline unsigned int bdev_io_min(struct block_device *bdev)
{
	return queue_io_min(bdev_get_queue(bdev));
}

static inline unsigned int queue_io_opt(struct request_queue *q)
{
	return q->limits.io_opt;
}

static inline int bdev_io_opt(struct block_device *bdev)
{
	return queue_io_opt(bdev_get_queue(bdev));
}

static inline int queue_alignment_offset(struct request_queue *q)
{
	if (q->limits.misaligned)
		return -1;

	return q->limits.alignment_offset;
}

static inline int queue_limit_alignment_offset(struct queue_limits *lim, sector_t sector)
{
	unsigned int granularity = max(lim->physical_block_size, lim->io_min);
	unsigned int alignment = sector_div(sector, granularity >> SECTOR_SHIFT)
		<< SECTOR_SHIFT;

	return (granularity + lim->alignment_offset - alignment) % granularity;
}

static inline int bdev_alignment_offset(struct block_device *bdev)
{
	struct request_queue *q = bdev_get_queue(bdev);

	if (q->limits.misaligned)
		return -1;

	if (bdev != bdev->bd_contains)
		return bdev->bd_part->alignment_offset;

	return q->limits.alignment_offset;
}

static inline int queue_discard_alignment(struct request_queue *q)
{
	if (q->limits.discard_misaligned)
		return -1;

	return q->limits.discard_alignment;
}

static inline int queue_limit_discard_alignment(struct queue_limits *lim, sector_t sector)
{
	unsigned int alignment, granularity, offset;

	if (!lim->max_discard_sectors)
		return 0;

	/* Why are these in bytes, not sectors? */
	alignment = lim->discard_alignment >> SECTOR_SHIFT;
	granularity = lim->discard_granularity >> SECTOR_SHIFT;
	if (!granularity)
		return 0;

	/* Offset of the partition start in 'granularity' sectors */
	offset = sector_div(sector, granularity);

	/* And why do we do this modulus *again* in blkdev_issue_discard()? */
	offset = (granularity + alignment - offset) % granularity;

	/* Turn it back into bytes, gaah */
	return offset << SECTOR_SHIFT;
}

static inline int bdev_discard_alignment(struct block_device *bdev)
{
	struct request_queue *q = bdev_get_queue(bdev);

	if (bdev != bdev->bd_contains)
		return bdev->bd_part->discard_alignment;

	return q->limits.discard_alignment;
}

static inline unsigned int queue_discard_zeroes_data(struct request_queue *q)
{
	if (q->limits.max_discard_sectors && q->limits.discard_zeroes_data == 1)
		return 1;

	return 0;
}

static inline unsigned int bdev_discard_zeroes_data(struct block_device *bdev)
{
	return queue_discard_zeroes_data(bdev_get_queue(bdev));
}

static inline unsigned int bdev_write_same(struct block_device *bdev)
{
	struct request_queue *q = bdev_get_queue(bdev);

	if (q)
		return q->limits.max_write_same_sectors;

	return 0;
}

static inline int queue_dma_alignment(struct request_queue *q)
{
	return q ? q->dma_alignment : 511;
}

static inline int blk_rq_aligned(struct request_queue *q, unsigned long addr,
				 unsigned int len)
{
	unsigned int alignment = queue_dma_alignment(q) | q->dma_pad_mask;
	return !(addr & alignment) && !(len & alignment);
}

/* assumes size > 256 */
static inline unsigned int blksize_bits(unsigned int size)
{
	unsigned int bits = 8;
	do {
		bits++;
		size >>= 1;
	} while (size > 256);
	return bits;
}

static inline unsigned int block_size(struct block_device *bdev)
{
	return bdev->bd_block_size;
}

static inline bool queue_flush_queueable(struct request_queue *q)
{
	return !q->flush_not_queueable;
}

typedef struct {struct page *v;} Sector;

unsigned char *read_dev_sector(struct block_device *, sector_t, Sector *);

static inline void put_dev_sector(Sector p)
{
	page_cache_release(p.v);
}

struct work_struct;
int kblockd_schedule_work(struct work_struct *work);
void kblockd_flush_work(struct work_struct *work);
static inline bool __bvec_gap_to_prev(struct request_queue *q,
				struct bio_vec *bprv, unsigned int offset)
{
	return offset ||
		((bprv->bv_offset + bprv->bv_len) & queue_virt_boundary(q));
}

/*
 * Check if adding a bio_vec after bprv with offset would create a gap in
 * the SG list. Most drivers don't care about this, but some do.
 */
static inline bool bvec_gap_to_prev(struct request_queue *q,
				struct bio_vec *bprv, unsigned int offset)
{
	if (!queue_virt_boundary(q))
		return false;
	return __bvec_gap_to_prev(q, bprv, offset);
}

static inline bool bio_will_gap(struct request_queue *q, struct bio *prev,
			 struct bio *next)
{
	if (bio_has_data(prev) && queue_virt_boundary(q)) {
		struct bio_vec pb, nb;

		bio_get_last_bvec(prev, &pb);
		bio_get_first_bvec(next, &nb);

		return __bvec_gap_to_prev(q, &pb, nb.bv_offset);
	}

	return false;
}

static inline bool req_gap_back_merge(struct request *req, struct bio *bio)
{
	return bio_will_gap(req->q, req->biotail, bio);
}

static inline bool req_gap_front_merge(struct request *req, struct bio *bio)
{
	return bio_will_gap(req->q, bio, req->bio);
}

struct work_struct;
int kblockd_schedule_work(struct work_struct *work);
int kblockd_schedule_delayed_work(struct delayed_work *dwork, unsigned long delay);
int kblockd_schedule_delayed_work_on(int cpu, struct delayed_work *dwork, unsigned long delay);

#ifdef CONFIG_BLK_CGROUP
/*
 * This should not be using sched_clock(). A real patch is in progress
 * to fix this up, until that is in place we need to disable preemption
 * around sched_clock() in this function and set_io_start_time_ns().
 */
static inline void set_start_time_ns(struct request *req)
{
	preempt_disable();
	req->start_time_ns = sched_clock();
	preempt_enable();
}

static inline void set_io_start_time_ns(struct request *req)
{
	preempt_disable();
	req->io_start_time_ns = sched_clock();
	preempt_enable();
}

static inline uint64_t rq_start_time_ns(struct request *req)
{
        return req->start_time_ns;
}

static inline uint64_t rq_io_start_time_ns(struct request *req)
{
        return req->io_start_time_ns;
}
#else
static inline void set_start_time_ns(struct request *req) {}
static inline void set_io_start_time_ns(struct request *req) {}
static inline uint64_t rq_start_time_ns(struct request *req)
{
	return 0;
}
static inline uint64_t rq_io_start_time_ns(struct request *req)
{
	return 0;
}
#endif

#define MODULE_ALIAS_BLOCKDEV(major,minor) \
	MODULE_ALIAS("block-major-" __stringify(major) "-" __stringify(minor))
#define MODULE_ALIAS_BLOCKDEV_MAJOR(major) \
	MODULE_ALIAS("block-major-" __stringify(major) "-*")

#if defined(CONFIG_BLK_DEV_INTEGRITY)

#define INTEGRITY_FLAG_READ	2	/* verify data integrity on read */
#define INTEGRITY_FLAG_WRITE	4	/* generate data integrity on write */

struct blk_integrity_exchg {
	void			*prot_buf;
	void			*data_buf;
	sector_t		sector;
	unsigned int		data_size;
	unsigned short		sector_size;
	const char		*disk_name;
};

typedef void (integrity_gen_fn) (struct blk_integrity_exchg *);
typedef int (integrity_vrfy_fn) (struct blk_integrity_exchg *);
typedef void (integrity_set_tag_fn) (void *, void *, unsigned int);
typedef void (integrity_get_tag_fn) (void *, void *, unsigned int);

struct blk_integrity {
	integrity_gen_fn	*generate_fn;
	integrity_vrfy_fn	*verify_fn;
	integrity_set_tag_fn	*set_tag_fn;
	integrity_get_tag_fn	*get_tag_fn;

	unsigned short		flags;
	unsigned short		tuple_size;
	unsigned short		sector_size;
	unsigned short		tag_size;

	const char		*name;

	struct kobject		kobj;
};

extern int blk_integrity_register(struct gendisk *, struct blk_integrity *);
extern void blk_integrity_unregister(struct gendisk *);
extern int blk_integrity_compare(struct block_device *, struct block_device *);
extern int blk_rq_map_integrity_sg(struct request *, struct scatterlist *);
extern int blk_rq_count_integrity_sg(struct request *);

static inline unsigned short blk_integrity_tuple_size(struct blk_integrity *bi)
{
	if (bi)
		return bi->tuple_size;

	return 0;
}

static inline struct blk_integrity *bdev_get_integrity(struct block_device *bdev)
{
	return bdev->bd_disk->integrity;
}

static inline unsigned int bdev_get_tag_size(struct block_device *bdev)
{
	struct blk_integrity *bi = bdev_get_integrity(bdev);

	if (bi)
		return bi->tag_size;

	return 0;
}

static inline int bdev_integrity_enabled(struct block_device *bdev, int rw)
{
	struct blk_integrity *bi = bdev_get_integrity(bdev);

	if (bi == NULL)
		return 0;

	if (rw == READ && bi->verify_fn != NULL &&
	    (bi->flags & INTEGRITY_FLAG_READ))
		return 1;

	if (rw == WRITE && bi->generate_fn != NULL &&
	    (bi->flags & INTEGRITY_FLAG_WRITE))
		return 1;

	return 0;
}

static inline int blk_integrity_rq(struct request *rq)
{
	if (rq->bio == NULL)
		return 0;

	return bio_integrity(rq->bio);
enum blk_integrity_flags {
	BLK_INTEGRITY_VERIFY		= 1 << 0,
	BLK_INTEGRITY_GENERATE		= 1 << 1,
	BLK_INTEGRITY_DEVICE_CAPABLE	= 1 << 2,
	BLK_INTEGRITY_IP_CHECKSUM	= 1 << 3,
};

struct blk_integrity_iter {
	void			*prot_buf;
	void			*data_buf;
	sector_t		seed;
	unsigned int		data_size;
	unsigned short		interval;
	const char		*disk_name;
};

typedef int (integrity_processing_fn) (struct blk_integrity_iter *);

struct blk_integrity_profile {
	integrity_processing_fn		*generate_fn;
	integrity_processing_fn		*verify_fn;
	const char			*name;
};

extern void blk_integrity_register(struct gendisk *, struct blk_integrity *);
extern void blk_integrity_unregister(struct gendisk *);
extern int blk_integrity_compare(struct gendisk *, struct gendisk *);
extern int blk_rq_map_integrity_sg(struct request_queue *, struct bio *,
				   struct scatterlist *);
extern int blk_rq_count_integrity_sg(struct request_queue *, struct bio *);
extern bool blk_integrity_merge_rq(struct request_queue *, struct request *,
				   struct request *);
extern bool blk_integrity_merge_bio(struct request_queue *, struct request *,
				    struct bio *);

static inline struct blk_integrity *blk_get_integrity(struct gendisk *disk)
{
	struct blk_integrity *bi = &disk->queue->integrity;

	if (!bi->profile)
		return NULL;

	return bi;
}

static inline
struct blk_integrity *bdev_get_integrity(struct block_device *bdev)
{
	return blk_get_integrity(bdev->bd_disk);
}

static inline bool blk_integrity_rq(struct request *rq)
{
	return rq->cmd_flags & REQ_INTEGRITY;
}

static inline void blk_queue_max_integrity_segments(struct request_queue *q,
						    unsigned int segs)
{
	q->limits.max_integrity_segments = segs;
}

static inline unsigned short
queue_max_integrity_segments(struct request_queue *q)
{
	return q->limits.max_integrity_segments;
}

static inline bool integrity_req_gap_back_merge(struct request *req,
						struct bio *next)
{
	struct bio_integrity_payload *bip = bio_integrity(req->bio);
	struct bio_integrity_payload *bip_next = bio_integrity(next);

	return bvec_gap_to_prev(req->q, &bip->bip_vec[bip->bip_vcnt - 1],
				bip_next->bip_vec[0].bv_offset);
}

static inline bool integrity_req_gap_front_merge(struct request *req,
						 struct bio *bio)
{
	struct bio_integrity_payload *bip = bio_integrity(bio);
	struct bio_integrity_payload *bip_next = bio_integrity(req->bio);

	return bvec_gap_to_prev(req->q, &bip->bip_vec[bip->bip_vcnt - 1],
				bip_next->bip_vec[0].bv_offset);
}

#else /* CONFIG_BLK_DEV_INTEGRITY */

#define blk_integrity_rq(rq)			(0)
#define blk_rq_count_integrity_sg(a)		(0)
#define blk_rq_map_integrity_sg(a, b)		(0)
#define bdev_get_integrity(a)			(0)
#define bdev_get_tag_size(a)			(0)
#define blk_integrity_compare(a, b)		(0)
#define blk_integrity_register(a, b)		(0)
#define blk_integrity_unregister(a)		do { } while (0);

#endif /* CONFIG_BLK_DEV_INTEGRITY */

#else /* CONFIG_BLOCK */
struct bio;
struct block_device;
struct gendisk;
struct blk_integrity;

static inline int blk_integrity_rq(struct request *rq)
{
	return 0;
}
static inline int blk_rq_count_integrity_sg(struct request_queue *q,
					    struct bio *b)
{
	return 0;
}
static inline int blk_rq_map_integrity_sg(struct request_queue *q,
					  struct bio *b,
					  struct scatterlist *s)
{
	return 0;
}
static inline struct blk_integrity *bdev_get_integrity(struct block_device *b)
{
	return NULL;
}
static inline struct blk_integrity *blk_get_integrity(struct gendisk *disk)
{
	return NULL;
}
static inline int blk_integrity_compare(struct gendisk *a, struct gendisk *b)
{
	return 0;
}
static inline void blk_integrity_register(struct gendisk *d,
					 struct blk_integrity *b)
{
}
static inline void blk_integrity_unregister(struct gendisk *d)
{
}
static inline void blk_queue_max_integrity_segments(struct request_queue *q,
						    unsigned int segs)
{
}
static inline unsigned short queue_max_integrity_segments(struct request_queue *q)
{
	return 0;
}
static inline bool blk_integrity_merge_rq(struct request_queue *rq,
					  struct request *r1,
					  struct request *r2)
{
	return true;
}
static inline bool blk_integrity_merge_bio(struct request_queue *rq,
					   struct request *r,
					   struct bio *b)
{
	return true;
}

static inline bool integrity_req_gap_back_merge(struct request *req,
						struct bio *next)
{
	return false;
}
static inline bool integrity_req_gap_front_merge(struct request *req,
						 struct bio *bio)
{
	return false;
}

#endif /* CONFIG_BLK_DEV_INTEGRITY */

struct block_device_operations {
	int (*open) (struct block_device *, fmode_t);
	void (*release) (struct gendisk *, fmode_t);
	int (*rw_page)(struct block_device *, sector_t, struct page *, int rw);
	int (*ioctl) (struct block_device *, fmode_t, unsigned, unsigned long);
	int (*compat_ioctl) (struct block_device *, fmode_t, unsigned, unsigned long);
	long (*direct_access)(struct block_device *, sector_t, void __pmem **,
			unsigned long *pfn);
	unsigned int (*check_events) (struct gendisk *disk,
				      unsigned int clearing);
	void (*unlock_native_capacity) (struct gendisk *);
	int (*revalidate_disk) (struct gendisk *);
	int (*getgeo)(struct block_device *, struct hd_geometry *);
	int (*set_read_only)(struct block_device *bdev, bool ro);
	/* this callback is with swap_lock and sometimes page table lock held */
	void (*swap_slot_free_notify) (struct block_device *, unsigned long);
	struct module *owner;
	const struct pr_ops *pr_ops;
};

extern int __blkdev_driver_ioctl(struct block_device *, fmode_t, unsigned int,
				 unsigned long);
extern int bdev_read_page(struct block_device *, sector_t, struct page *);
extern int bdev_write_page(struct block_device *, sector_t, struct page *,
						struct writeback_control *);
extern long bdev_direct_access(struct block_device *, sector_t,
		void __pmem **addr, unsigned long *pfn, long size);
#else /* CONFIG_BLOCK */

struct block_device;

/*
 * stubs for when the block layer is configured out
 */
#define buffer_heads_over_limit 0

static inline long nr_blockdev_pages(void)
{
	return 0;
}

struct blk_plug {
};

static inline void blk_start_plug(struct blk_plug *plug)
{
}

static inline void blk_finish_plug(struct blk_plug *plug)
{
}

static inline void blk_flush_plug(struct task_struct *task)
{
}

static inline void blk_schedule_flush_plug(struct task_struct *task)
{
}


static inline bool blk_needs_flush_plug(struct task_struct *tsk)
{
	return false;
}

static inline int blkdev_issue_flush(struct block_device *bdev, gfp_t gfp_mask,
				     sector_t *error_sector)
{
	return 0;
}

#endif /* CONFIG_BLOCK */

#endif
