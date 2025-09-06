/*
 * zfcp device driver
 *
 * Setup and helper functions to access QDIO.
 *
 * Copyright IBM Corporation 2002, 2008
 */

#include "zfcp_ext.h"

/* FIXME(tune): free space should be one max. SBAL chain plus what? */
#define ZFCP_QDIO_PCI_INTERVAL	(QDIO_MAX_BUFFERS_PER_Q \
				- (FSF_MAX_SBALS_PER_REQ + 4))
#define QBUFF_PER_PAGE		(PAGE_SIZE / sizeof(struct qdio_buffer))

static int zfcp_qdio_buffers_enqueue(struct qdio_buffer **sbal)
{
	int pos;

	for (pos = 0; pos < QDIO_MAX_BUFFERS_PER_Q; pos += QBUFF_PER_PAGE) {
		sbal[pos] = (struct qdio_buffer *) get_zeroed_page(GFP_KERNEL);
		if (!sbal[pos])
			return -ENOMEM;
	}
	for (pos = 0; pos < QDIO_MAX_BUFFERS_PER_Q; pos++)
		if (pos % QBUFF_PER_PAGE)
			sbal[pos] = sbal[pos - 1] + 1;
	return 0;
}

static volatile struct qdio_buffer_element *
zfcp_qdio_sbale(struct zfcp_qdio_queue *q, int sbal_idx, int sbale_idx)
{
	return &q->sbal[sbal_idx]->element[sbale_idx];
}

/**
 * zfcp_qdio_free - free memory used by request- and resposne queue
 * @adapter: pointer to the zfcp_adapter structure
 */
void zfcp_qdio_free(struct zfcp_adapter *adapter)
{
	struct qdio_buffer **sbal_req, **sbal_resp;
	int p;

	if (adapter->ccw_device)
		qdio_free(adapter->ccw_device);

	sbal_req = adapter->req_q.sbal;
	sbal_resp = adapter->resp_q.sbal;

	for (p = 0; p < QDIO_MAX_BUFFERS_PER_Q; p += QBUFF_PER_PAGE) {
		free_page((unsigned long) sbal_req[p]);
		free_page((unsigned long) sbal_resp[p]);
	}
}

static void zfcp_qdio_handler_error(struct zfcp_adapter *adapter, u8 id)
{
	dev_warn(&adapter->ccw_device->dev, "QDIO problem occurred.\n");

	zfcp_erp_adapter_reopen(adapter,
				ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED |
				ZFCP_STATUS_COMMON_ERP_FAILED, id, NULL);
 * Copyright IBM Corp. 2002, 2010
 */

#define KMSG_COMPONENT "zfcp"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/slab.h>
#include <linux/module.h>
#include "zfcp_ext.h"
#include "zfcp_qdio.h"

static bool enable_multibuffer = 1;
module_param_named(datarouter, enable_multibuffer, bool, 0400);
MODULE_PARM_DESC(datarouter, "Enable hardware data router support (default on)");

static void zfcp_qdio_handler_error(struct zfcp_qdio *qdio, char *id,
				    unsigned int qdio_err)
{
	struct zfcp_adapter *adapter = qdio->adapter;

	dev_warn(&adapter->ccw_device->dev, "A QDIO problem occurred\n");

	if (qdio_err & QDIO_ERROR_SLSB_STATE) {
		zfcp_qdio_siosl(adapter);
		zfcp_erp_adapter_shutdown(adapter, 0, id);
		return;
	}
	zfcp_erp_adapter_reopen(adapter,
				ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED |
				ZFCP_STATUS_COMMON_ERP_FAILED, id);
}

static void zfcp_qdio_zero_sbals(struct qdio_buffer *sbal[], int first, int cnt)
{
	int i, sbal_idx;

	for (i = first; i < first + cnt; i++) {
		sbal_idx = i % QDIO_MAX_BUFFERS_PER_Q;
		memset(sbal[sbal_idx], 0, sizeof(struct qdio_buffer));
	}
}

static void zfcp_qdio_int_req(struct ccw_device *cdev, unsigned int qdio_err,
			      int queue_no, int first, int count,
			      unsigned long parm)
{
	struct zfcp_adapter *adapter = (struct zfcp_adapter *) parm;
	struct zfcp_qdio_queue *queue = &adapter->req_q;

	if (unlikely(qdio_err)) {
		zfcp_hba_dbf_event_qdio(adapter, qdio_err, first, count);
		zfcp_qdio_handler_error(adapter, 140);
/* this needs to be called prior to updating the queue fill level */
static inline void zfcp_qdio_account(struct zfcp_qdio *qdio)
{
	unsigned long long now, span;
	int used;

	now = get_tod_clock_monotonic();
	span = (now - qdio->req_q_time) >> 12;
	used = QDIO_MAX_BUFFERS_PER_Q - atomic_read(&qdio->req_q_free);
	qdio->req_q_util += used * span;
	qdio->req_q_time = now;
}

static void zfcp_qdio_int_req(struct ccw_device *cdev, unsigned int qdio_err,
			      int queue_no, int idx, int count,
			      unsigned long parm)
{
	struct zfcp_qdio *qdio = (struct zfcp_qdio *) parm;

	if (unlikely(qdio_err)) {
		zfcp_qdio_handler_error(qdio, "qdireq1", qdio_err);
		return;
	}

	/* cleanup all SBALs being program-owned now */
	zfcp_qdio_zero_sbals(queue->sbal, first, count);

	atomic_add(count, &queue->count);
	wake_up(&adapter->request_wq);
}

static void zfcp_qdio_reqid_check(struct zfcp_adapter *adapter,
				  unsigned long req_id, int sbal_idx)
{
	struct zfcp_fsf_req *fsf_req;
	unsigned long flags;

	spin_lock_irqsave(&adapter->req_list_lock, flags);
	fsf_req = zfcp_reqlist_find(adapter, req_id);

	if (!fsf_req)
		/*
		 * Unknown request means that we have potentially memory
		 * corruption and must stop the machine immediatly.
		 */
		panic("error: unknown request id (%lx) on adapter %s.\n",
		      req_id, zfcp_get_busid_by_adapter(adapter));

	zfcp_reqlist_remove(adapter, fsf_req);
	spin_unlock_irqrestore(&adapter->req_list_lock, flags);

	fsf_req->sbal_response = sbal_idx;
	zfcp_fsf_req_complete(fsf_req);
}

static void zfcp_qdio_resp_put_back(struct zfcp_adapter *adapter, int processed)
{
	struct zfcp_qdio_queue *queue = &adapter->resp_q;
	struct ccw_device *cdev = adapter->ccw_device;
	u8 count, start = queue->first;
	unsigned int retval;

	count = atomic_read(&queue->count) + processed;

	retval = do_QDIO(cdev, QDIO_FLAG_SYNC_INPUT, 0, start, count);

	if (unlikely(retval)) {
		atomic_set(&queue->count, count);
		/* FIXME: Recover this with an adapter reopen? */
	} else {
		queue->first += count;
		queue->first %= QDIO_MAX_BUFFERS_PER_Q;
		atomic_set(&queue->count, 0);
	}
}

static void zfcp_qdio_int_resp(struct ccw_device *cdev, unsigned int qdio_err,
			       int queue_no, int first, int count,
			       unsigned long parm)
{
	struct zfcp_adapter *adapter = (struct zfcp_adapter *) parm;
	struct zfcp_qdio_queue *queue = &adapter->resp_q;
	volatile struct qdio_buffer_element *sbale;
	int sbal_idx, sbale_idx, sbal_no;

	if (unlikely(qdio_err)) {
		zfcp_hba_dbf_event_qdio(adapter, qdio_err, first, count);
		zfcp_qdio_handler_error(adapter, 147);
	zfcp_qdio_zero_sbals(qdio->req_q, idx, count);

	spin_lock_irq(&qdio->stat_lock);
	zfcp_qdio_account(qdio);
	spin_unlock_irq(&qdio->stat_lock);
	atomic_add(count, &qdio->req_q_free);
	wake_up(&qdio->req_q_wq);
}

static void zfcp_qdio_int_resp(struct ccw_device *cdev, unsigned int qdio_err,
			       int queue_no, int idx, int count,
			       unsigned long parm)
{
	struct zfcp_qdio *qdio = (struct zfcp_qdio *) parm;
	struct zfcp_adapter *adapter = qdio->adapter;
	int sbal_no, sbal_idx;

	if (unlikely(qdio_err)) {
		if (zfcp_adapter_multi_buffer_active(adapter)) {
			void *pl[ZFCP_QDIO_MAX_SBALS_PER_REQ + 1];
			struct qdio_buffer_element *sbale;
			u64 req_id;
			u8 scount;

			memset(pl, 0,
			       ZFCP_QDIO_MAX_SBALS_PER_REQ * sizeof(void *));
			sbale = qdio->res_q[idx]->element;
			req_id = (u64) sbale->addr;
			scount = min(sbale->scount + 1,
				     ZFCP_QDIO_MAX_SBALS_PER_REQ + 1);
				     /* incl. signaling SBAL */

			for (sbal_no = 0; sbal_no < scount; sbal_no++) {
				sbal_idx = (idx + sbal_no) %
					QDIO_MAX_BUFFERS_PER_Q;
				pl[sbal_no] = qdio->res_q[sbal_idx];
			}
			zfcp_dbf_hba_def_err(adapter, req_id, scount, pl);
		}
		zfcp_qdio_handler_error(qdio, "qdires1", qdio_err);
		return;
	}

	/*
	 * go through all SBALs from input queue currently
	 * returned by QDIO layer
	 */
	for (sbal_no = 0; sbal_no < count; sbal_no++) {
		sbal_idx = (first + sbal_no) % QDIO_MAX_BUFFERS_PER_Q;

		/* go through all SBALEs of SBAL */
		for (sbale_idx = 0; sbale_idx < QDIO_MAX_ELEMENTS_PER_BUFFER;
		     sbale_idx++) {
			sbale = zfcp_qdio_sbale(queue, sbal_idx, sbale_idx);
			zfcp_qdio_reqid_check(adapter,
					      (unsigned long) sbale->addr,
					      sbal_idx);
			if (likely(sbale->flags & SBAL_FLAGS_LAST_ENTRY))
				break;
		};

		if (unlikely(!(sbale->flags & SBAL_FLAGS_LAST_ENTRY)))
			dev_warn(&adapter->ccw_device->dev,
				 "Protocol violation by adapter. "
				 "Continuing operations.\n");
	}

	/*
	 * put range of SBALs back to response queue
	 * (including SBALs which have already been free before)
	 */
	zfcp_qdio_resp_put_back(adapter, count);
}

/**
 * zfcp_qdio_sbale_req - return ptr to SBALE of req_q for a struct zfcp_fsf_req
 * @fsf_req: pointer to struct fsf_req
 * Returns: pointer to qdio_buffer_element (SBALE) structure
 */
volatile struct qdio_buffer_element *
zfcp_qdio_sbale_req(struct zfcp_fsf_req *req)
{
	return zfcp_qdio_sbale(&req->adapter->req_q, req->sbal_last, 0);
}

/**
 * zfcp_qdio_sbale_curr - return curr SBALE on req_q for a struct zfcp_fsf_req
 * @fsf_req: pointer to struct fsf_req
 * Returns: pointer to qdio_buffer_element (SBALE) structure
 */
volatile struct qdio_buffer_element *
zfcp_qdio_sbale_curr(struct zfcp_fsf_req *req)
{
	return zfcp_qdio_sbale(&req->adapter->req_q, req->sbal_last,
			       req->sbale_curr);
}

static void zfcp_qdio_sbal_limit(struct zfcp_fsf_req *fsf_req, int max_sbals)
{
	int count = atomic_read(&fsf_req->adapter->req_q.count);
	count = min(count, max_sbals);
	fsf_req->sbal_limit = (fsf_req->sbal_first + count - 1)
					% QDIO_MAX_BUFFERS_PER_Q;
}

static volatile struct qdio_buffer_element *
zfcp_qdio_sbal_chain(struct zfcp_fsf_req *fsf_req, unsigned long sbtype)
{
	volatile struct qdio_buffer_element *sbale;

	/* set last entry flag in current SBALE of current SBAL */
	sbale = zfcp_qdio_sbale_curr(fsf_req);
	sbale->flags |= SBAL_FLAGS_LAST_ENTRY;

	/* don't exceed last allowed SBAL */
	if (fsf_req->sbal_last == fsf_req->sbal_limit)
		return NULL;

	/* set chaining flag in first SBALE of current SBAL */
	sbale = zfcp_qdio_sbale_req(fsf_req);
	sbale->flags |= SBAL_FLAGS0_MORE_SBALS;

	/* calculate index of next SBAL */
	fsf_req->sbal_last++;
	fsf_req->sbal_last %= QDIO_MAX_BUFFERS_PER_Q;

	/* keep this requests number of SBALs up-to-date */
	fsf_req->sbal_number++;

	/* start at first SBALE of new SBAL */
	fsf_req->sbale_curr = 0;

	/* set storage-block type for new SBAL */
	sbale = zfcp_qdio_sbale_curr(fsf_req);
	sbale->flags |= sbtype;
		sbal_idx = (idx + sbal_no) % QDIO_MAX_BUFFERS_PER_Q;
		/* go through all SBALEs of SBAL */
		zfcp_fsf_reqid_check(qdio, sbal_idx);
	}

	/*
	 * put SBALs back to response queue
	 */
	if (do_QDIO(cdev, QDIO_FLAG_SYNC_INPUT, 0, idx, count))
		zfcp_erp_adapter_reopen(qdio->adapter, 0, "qdires2");
}

static struct qdio_buffer_element *
zfcp_qdio_sbal_chain(struct zfcp_qdio *qdio, struct zfcp_qdio_req *q_req)
{
	struct qdio_buffer_element *sbale;

	/* set last entry flag in current SBALE of current SBAL */
	sbale = zfcp_qdio_sbale_curr(qdio, q_req);
	sbale->eflags |= SBAL_EFLAGS_LAST_ENTRY;

	/* don't exceed last allowed SBAL */
	if (q_req->sbal_last == q_req->sbal_limit)
		return NULL;

	/* set chaining flag in first SBALE of current SBAL */
	sbale = zfcp_qdio_sbale_req(qdio, q_req);
	sbale->sflags |= SBAL_SFLAGS0_MORE_SBALS;

	/* calculate index of next SBAL */
	q_req->sbal_last++;
	q_req->sbal_last %= QDIO_MAX_BUFFERS_PER_Q;

	/* keep this requests number of SBALs up-to-date */
	q_req->sbal_number++;
	BUG_ON(q_req->sbal_number > ZFCP_QDIO_MAX_SBALS_PER_REQ);

	/* start at first SBALE of new SBAL */
	q_req->sbale_curr = 0;

	/* set storage-block type for new SBAL */
	sbale = zfcp_qdio_sbale_curr(qdio, q_req);
	sbale->sflags |= q_req->sbtype;

	return sbale;
}

static volatile struct qdio_buffer_element *
zfcp_qdio_sbale_next(struct zfcp_fsf_req *fsf_req, unsigned long sbtype)
{
	if (fsf_req->sbale_curr == ZFCP_LAST_SBALE_PER_SBAL)
		return zfcp_qdio_sbal_chain(fsf_req, sbtype);
	fsf_req->sbale_curr++;
	return zfcp_qdio_sbale_curr(fsf_req);
}

static void zfcp_qdio_undo_sbals(struct zfcp_fsf_req *fsf_req)
{
	struct qdio_buffer **sbal = fsf_req->adapter->req_q.sbal;
	int first = fsf_req->sbal_first;
	int last = fsf_req->sbal_last;
	int count = (last - first + QDIO_MAX_BUFFERS_PER_Q) %
		QDIO_MAX_BUFFERS_PER_Q + 1;
	zfcp_qdio_zero_sbals(sbal, first, count);
}

static int zfcp_qdio_fill_sbals(struct zfcp_fsf_req *fsf_req,
				unsigned int sbtype, void *start_addr,
				unsigned int total_length)
{
	volatile struct qdio_buffer_element *sbale;
	unsigned long remaining, length;
	void *addr;

	/* split segment up */
	for (addr = start_addr, remaining = total_length; remaining > 0;
	     addr += length, remaining -= length) {
		sbale = zfcp_qdio_sbale_next(fsf_req, sbtype);
		if (!sbale) {
			zfcp_qdio_undo_sbals(fsf_req);
			return -EINVAL;
		}

		/* new piece must not exceed next page boundary */
		length = min(remaining,
			     (PAGE_SIZE - ((unsigned long)addr &
					   (PAGE_SIZE - 1))));
		sbale->addr = addr;
		sbale->length = length;
	}
	return 0;
static struct qdio_buffer_element *
zfcp_qdio_sbale_next(struct zfcp_qdio *qdio, struct zfcp_qdio_req *q_req)
{
	if (q_req->sbale_curr == qdio->max_sbale_per_sbal - 1)
		return zfcp_qdio_sbal_chain(qdio, q_req);
	q_req->sbale_curr++;
	return zfcp_qdio_sbale_curr(qdio, q_req);
}

/**
 * zfcp_qdio_sbals_from_sg - fill SBALs from scatter-gather list
 * @fsf_req: request to be processed
 * @sbtype: SBALE flags
 * @sg: scatter-gather list
 * @max_sbals: upper bound for number of SBALs to be used
 * Returns: number of bytes, or error (negativ)
 */
int zfcp_qdio_sbals_from_sg(struct zfcp_fsf_req *fsf_req, unsigned long sbtype,
			    struct scatterlist *sg, int max_sbals)
{
	volatile struct qdio_buffer_element *sbale;
	int retval, bytes = 0;

	/* figure out last allowed SBAL */
	zfcp_qdio_sbal_limit(fsf_req, max_sbals);

	/* set storage-block type for this request */
	sbale = zfcp_qdio_sbale_req(fsf_req);
	sbale->flags |= sbtype;

	for (; sg; sg = sg_next(sg)) {
		retval = zfcp_qdio_fill_sbals(fsf_req, sbtype, sg_virt(sg),
					      sg->length);
		if (retval < 0)
			return retval;
		bytes += sg->length;
	}

	/* assume that no other SBALEs are to follow in the same SBAL */
	sbale = zfcp_qdio_sbale_curr(fsf_req);
	sbale->flags |= SBAL_FLAGS_LAST_ENTRY;

	return bytes;
 * @qdio: pointer to struct zfcp_qdio
 * @q_req: pointer to struct zfcp_qdio_req
 * @sg: scatter-gather list
 * @max_sbals: upper bound for number of SBALs to be used
 * Returns: zero or -EINVAL on error
 */
int zfcp_qdio_sbals_from_sg(struct zfcp_qdio *qdio, struct zfcp_qdio_req *q_req,
			    struct scatterlist *sg)
{
	struct qdio_buffer_element *sbale;

	/* set storage-block type for this request */
	sbale = zfcp_qdio_sbale_req(qdio, q_req);
	sbale->sflags |= q_req->sbtype;

	for (; sg; sg = sg_next(sg)) {
		sbale = zfcp_qdio_sbale_next(qdio, q_req);
		if (!sbale) {
			atomic_inc(&qdio->req_q_full);
			zfcp_qdio_zero_sbals(qdio->req_q, q_req->sbal_first,
					     q_req->sbal_number);
			return -EINVAL;
		}
		sbale->addr = sg_virt(sg);
		sbale->length = sg->length;
	}
	return 0;
}

static int zfcp_qdio_sbal_check(struct zfcp_qdio *qdio)
{
	if (atomic_read(&qdio->req_q_free) ||
	    !(atomic_read(&qdio->adapter->status) & ZFCP_STATUS_ADAPTER_QDIOUP))
		return 1;
	return 0;
}

/**
 * zfcp_qdio_sbal_get - get free sbal in request queue, wait if necessary
 * @qdio: pointer to struct zfcp_qdio
 *
 * The req_q_lock must be held by the caller of this function, and
 * this function may only be called from process context; it will
 * sleep when waiting for a free sbal.
 *
 * Returns: 0 on success, -EIO if there is no free sbal after waiting.
 */
int zfcp_qdio_sbal_get(struct zfcp_qdio *qdio)
{
	long ret;

	ret = wait_event_interruptible_lock_irq_timeout(qdio->req_q_wq,
		       zfcp_qdio_sbal_check(qdio), qdio->req_q_lock, 5 * HZ);

	if (!(atomic_read(&qdio->adapter->status) & ZFCP_STATUS_ADAPTER_QDIOUP))
		return -EIO;

	if (ret > 0)
		return 0;

	if (!ret) {
		atomic_inc(&qdio->req_q_full);
		/* assume hanging outbound queue, try queue recovery */
		zfcp_erp_adapter_reopen(qdio->adapter, 0, "qdsbg_1");
	}

	return -EIO;
}

/**
 * zfcp_qdio_send - set PCI flag in first SBALE and send req to QDIO
 * @fsf_req: pointer to struct zfcp_fsf_req
 * Returns: 0 on success, error otherwise
 */
int zfcp_qdio_send(struct zfcp_fsf_req *fsf_req)
{
	struct zfcp_adapter *adapter = fsf_req->adapter;
	struct zfcp_qdio_queue *req_q = &adapter->req_q;
	int first = fsf_req->sbal_first;
	int count = fsf_req->sbal_number;
	int retval, pci, pci_batch;
	volatile struct qdio_buffer_element *sbale;

	/* acknowledgements for transferred buffers */
	pci_batch = req_q->pci_batch + count;
	if (unlikely(pci_batch >= ZFCP_QDIO_PCI_INTERVAL)) {
		pci_batch %= ZFCP_QDIO_PCI_INTERVAL;
		pci = first + count - (pci_batch + 1);
		pci %= QDIO_MAX_BUFFERS_PER_Q;
		sbale = zfcp_qdio_sbale(req_q, pci, 0);
		sbale->flags |= SBAL_FLAGS0_PCI;
	}

	retval = do_QDIO(adapter->ccw_device, QDIO_FLAG_SYNC_OUTPUT, 0, first,
			 count);
	if (unlikely(retval)) {
		zfcp_qdio_zero_sbals(req_q->sbal, first, count);
 * @qdio: pointer to struct zfcp_qdio
 * @q_req: pointer to struct zfcp_qdio_req
 * Returns: 0 on success, error otherwise
 */
int zfcp_qdio_send(struct zfcp_qdio *qdio, struct zfcp_qdio_req *q_req)
{
	int retval;
	u8 sbal_number = q_req->sbal_number;

	spin_lock(&qdio->stat_lock);
	zfcp_qdio_account(qdio);
	spin_unlock(&qdio->stat_lock);

	retval = do_QDIO(qdio->adapter->ccw_device, QDIO_FLAG_SYNC_OUTPUT, 0,
			 q_req->sbal_first, sbal_number);

	if (unlikely(retval)) {
		zfcp_qdio_zero_sbals(qdio->req_q, q_req->sbal_first,
				     sbal_number);
		return retval;
	}

	/* account for transferred buffers */
	atomic_sub(count, &req_q->count);
	req_q->first += count;
	req_q->first %= QDIO_MAX_BUFFERS_PER_Q;
	req_q->pci_batch = pci_batch;
	return 0;
}

	atomic_sub(sbal_number, &qdio->req_q_free);
	qdio->req_q_idx += sbal_number;
	qdio->req_q_idx %= QDIO_MAX_BUFFERS_PER_Q;

	return 0;
}


static void zfcp_qdio_setup_init_data(struct qdio_initialize *id,
				      struct zfcp_qdio *qdio)
{
	memset(id, 0, sizeof(*id));
	id->cdev = qdio->adapter->ccw_device;
	id->q_format = QDIO_ZFCP_QFMT;
	memcpy(id->adapter_name, dev_name(&id->cdev->dev), 8);
	ASCEBC(id->adapter_name, 8);
	id->qib_rflags = QIB_RFLAGS_ENABLE_DATA_DIV;
	if (enable_multibuffer)
		id->qdr_ac |= QDR_AC_MULTI_BUFFER_ENABLE;
	id->no_input_qs = 1;
	id->no_output_qs = 1;
	id->input_handler = zfcp_qdio_int_resp;
	id->output_handler = zfcp_qdio_int_req;
	id->int_parm = (unsigned long) qdio;
	id->input_sbal_addr_array = (void **) (qdio->res_q);
	id->output_sbal_addr_array = (void **) (qdio->req_q);
	id->scan_threshold =
		QDIO_MAX_BUFFERS_PER_Q - ZFCP_QDIO_MAX_SBALS_PER_REQ * 2;
}

/**
 * zfcp_qdio_allocate - allocate queue memory and initialize QDIO data
 * @adapter: pointer to struct zfcp_adapter
 * Returns: -ENOMEM on memory allocation error or return value from
 *          qdio_allocate
 */
int zfcp_qdio_allocate(struct zfcp_adapter *adapter)
{
	struct qdio_initialize *init_data;

	if (zfcp_qdio_buffers_enqueue(adapter->req_q.sbal) ||
		   zfcp_qdio_buffers_enqueue(adapter->resp_q.sbal))
		return -ENOMEM;

	init_data = &adapter->qdio_init_data;

	init_data->cdev = adapter->ccw_device;
	init_data->q_format = QDIO_ZFCP_QFMT;
	memcpy(init_data->adapter_name, zfcp_get_busid_by_adapter(adapter), 8);
	ASCEBC(init_data->adapter_name, 8);
	init_data->qib_param_field_format = 0;
	init_data->qib_param_field = NULL;
	init_data->input_slib_elements = NULL;
	init_data->output_slib_elements = NULL;
	init_data->no_input_qs = 1;
	init_data->no_output_qs = 1;
	init_data->input_handler = zfcp_qdio_int_resp;
	init_data->output_handler = zfcp_qdio_int_req;
	init_data->int_parm = (unsigned long) adapter;
	init_data->flags = QDIO_INBOUND_0COPY_SBALS |
			QDIO_OUTBOUND_0COPY_SBALS | QDIO_USE_OUTBOUND_PCIS;
	init_data->input_sbal_addr_array =
			(void **) (adapter->resp_q.sbal);
	init_data->output_sbal_addr_array =
			(void **) (adapter->req_q.sbal);

	return qdio_allocate(init_data);
static int zfcp_qdio_allocate(struct zfcp_qdio *qdio)
{
	struct qdio_initialize init_data;
	int ret;

	ret = qdio_alloc_buffers(qdio->req_q, QDIO_MAX_BUFFERS_PER_Q);
	if (ret)
		return -ENOMEM;

	ret = qdio_alloc_buffers(qdio->res_q, QDIO_MAX_BUFFERS_PER_Q);
	if (ret)
		goto free_req_q;

	zfcp_qdio_setup_init_data(&init_data, qdio);
	init_waitqueue_head(&qdio->req_q_wq);

	ret = qdio_allocate(&init_data);
	if (ret)
		goto free_res_q;

	return 0;

free_res_q:
	qdio_free_buffers(qdio->res_q, QDIO_MAX_BUFFERS_PER_Q);
free_req_q:
	qdio_free_buffers(qdio->req_q, QDIO_MAX_BUFFERS_PER_Q);
	return ret;
}

/**
 * zfcp_close_qdio - close qdio queues for an adapter
 */
void zfcp_qdio_close(struct zfcp_adapter *adapter)
{
	struct zfcp_qdio_queue *req_q;
	int first, count;

	if (!atomic_test_mask(ZFCP_STATUS_ADAPTER_QDIOUP, &adapter->status))
		return;

	/* clear QDIOUP flag, thus do_QDIO is not called during qdio_shutdown */
	req_q = &adapter->req_q;
	spin_lock_bh(&req_q->lock);
	atomic_clear_mask(ZFCP_STATUS_ADAPTER_QDIOUP, &adapter->status);
	spin_unlock_bh(&req_q->lock);
 * @qdio: pointer to structure zfcp_qdio
 */
void zfcp_qdio_close(struct zfcp_qdio *qdio)
{
	struct zfcp_adapter *adapter = qdio->adapter;
	int idx, count;

	if (!(atomic_read(&adapter->status) & ZFCP_STATUS_ADAPTER_QDIOUP))
		return;

	/* clear QDIOUP flag, thus do_QDIO is not called during qdio_shutdown */
	spin_lock_irq(&qdio->req_q_lock);
	atomic_andnot(ZFCP_STATUS_ADAPTER_QDIOUP, &adapter->status);
	spin_unlock_irq(&qdio->req_q_lock);

	wake_up(&qdio->req_q_wq);

	qdio_shutdown(adapter->ccw_device, QDIO_FLAG_CLEANUP_USING_CLEAR);

	/* cleanup used outbound sbals */
	count = atomic_read(&req_q->count);
	if (count < QDIO_MAX_BUFFERS_PER_Q) {
		first = (req_q->first + count) % QDIO_MAX_BUFFERS_PER_Q;
		count = QDIO_MAX_BUFFERS_PER_Q - count;
		zfcp_qdio_zero_sbals(req_q->sbal, first, count);
	}
	req_q->first = 0;
	atomic_set(&req_q->count, 0);
	req_q->pci_batch = 0;
	adapter->resp_q.first = 0;
	atomic_set(&adapter->resp_q.count, 0);
	count = atomic_read(&qdio->req_q_free);
	if (count < QDIO_MAX_BUFFERS_PER_Q) {
		idx = (qdio->req_q_idx + count) % QDIO_MAX_BUFFERS_PER_Q;
		count = QDIO_MAX_BUFFERS_PER_Q - count;
		zfcp_qdio_zero_sbals(qdio->req_q, idx, count);
	}
	qdio->req_q_idx = 0;
	atomic_set(&qdio->req_q_free, 0);
}

/**
 * zfcp_qdio_open - prepare and initialize response queue
 * @adapter: pointer to struct zfcp_adapter
 * Returns: 0 on success, otherwise -EIO
 */
int zfcp_qdio_open(struct zfcp_adapter *adapter)
{
	volatile struct qdio_buffer_element *sbale;
	int cc;

	if (atomic_test_mask(ZFCP_STATUS_ADAPTER_QDIOUP, &adapter->status))
		return -EIO;

	if (qdio_establish(&adapter->qdio_init_data)) {
		dev_err(&adapter->ccw_device->dev,
			 "Establish of QDIO queues failed.\n");
		return -EIO;
	}

	if (qdio_activate(adapter->ccw_device)) {
		dev_err(&adapter->ccw_device->dev,
			 "Activate of QDIO queues failed.\n");
		goto failed_qdio;
	}

	for (cc = 0; cc < QDIO_MAX_BUFFERS_PER_Q; cc++) {
		sbale = &(adapter->resp_q.sbal[cc]->element[0]);
		sbale->length = 0;
		sbale->flags = SBAL_FLAGS_LAST_ENTRY;
		sbale->addr = NULL;
	}

	if (do_QDIO(adapter->ccw_device, QDIO_FLAG_SYNC_INPUT, 0, 0,
		     QDIO_MAX_BUFFERS_PER_Q)) {
		dev_err(&adapter->ccw_device->dev,
			 "Init of QDIO response queue failed.\n");
		goto failed_qdio;
	}

	/* set index of first avalable SBALS / number of available SBALS */
	adapter->req_q.first = 0;
	atomic_set(&adapter->req_q.count, QDIO_MAX_BUFFERS_PER_Q);
	adapter->req_q.pci_batch = 0;
 * @qdio: pointer to struct zfcp_qdio
 * Returns: 0 on success, otherwise -EIO
 */
int zfcp_qdio_open(struct zfcp_qdio *qdio)
{
	struct qdio_buffer_element *sbale;
	struct qdio_initialize init_data;
	struct zfcp_adapter *adapter = qdio->adapter;
	struct ccw_device *cdev = adapter->ccw_device;
	struct qdio_ssqd_desc ssqd;
	int cc;

	if (atomic_read(&adapter->status) & ZFCP_STATUS_ADAPTER_QDIOUP)
		return -EIO;

	atomic_andnot(ZFCP_STATUS_ADAPTER_SIOSL_ISSUED,
			  &qdio->adapter->status);

	zfcp_qdio_setup_init_data(&init_data, qdio);

	if (qdio_establish(&init_data))
		goto failed_establish;

	if (qdio_get_ssqd_desc(init_data.cdev, &ssqd))
		goto failed_qdio;

	if (ssqd.qdioac2 & CHSC_AC2_DATA_DIV_ENABLED)
		atomic_or(ZFCP_STATUS_ADAPTER_DATA_DIV_ENABLED,
				&qdio->adapter->status);

	if (ssqd.qdioac2 & CHSC_AC2_MULTI_BUFFER_ENABLED) {
		atomic_or(ZFCP_STATUS_ADAPTER_MB_ACT, &adapter->status);
		qdio->max_sbale_per_sbal = QDIO_MAX_ELEMENTS_PER_BUFFER;
	} else {
		atomic_andnot(ZFCP_STATUS_ADAPTER_MB_ACT, &adapter->status);
		qdio->max_sbale_per_sbal = QDIO_MAX_ELEMENTS_PER_BUFFER - 1;
	}

	qdio->max_sbale_per_req =
		ZFCP_QDIO_MAX_SBALS_PER_REQ * qdio->max_sbale_per_sbal
		- 2;
	if (qdio_activate(cdev))
		goto failed_qdio;

	for (cc = 0; cc < QDIO_MAX_BUFFERS_PER_Q; cc++) {
		sbale = &(qdio->res_q[cc]->element[0]);
		sbale->length = 0;
		sbale->eflags = SBAL_EFLAGS_LAST_ENTRY;
		sbale->sflags = 0;
		sbale->addr = NULL;
	}

	if (do_QDIO(cdev, QDIO_FLAG_SYNC_INPUT, 0, 0, QDIO_MAX_BUFFERS_PER_Q))
		goto failed_qdio;

	/* set index of first available SBALS / number of available SBALS */
	qdio->req_q_idx = 0;
	atomic_set(&qdio->req_q_free, QDIO_MAX_BUFFERS_PER_Q);
	atomic_or(ZFCP_STATUS_ADAPTER_QDIOUP, &qdio->adapter->status);

	if (adapter->scsi_host) {
		adapter->scsi_host->sg_tablesize = qdio->max_sbale_per_req;
		adapter->scsi_host->max_sectors = qdio->max_sbale_per_req * 8;
	}

	return 0;

failed_qdio:
	qdio_shutdown(adapter->ccw_device, QDIO_FLAG_CLEANUP_USING_CLEAR);
	return -EIO;
}
	qdio_shutdown(cdev, QDIO_FLAG_CLEANUP_USING_CLEAR);
failed_establish:
	dev_err(&cdev->dev,
		"Setting up the QDIO connection to the FCP adapter failed\n");
	return -EIO;
}

void zfcp_qdio_destroy(struct zfcp_qdio *qdio)
{
	if (!qdio)
		return;

	if (qdio->adapter->ccw_device)
		qdio_free(qdio->adapter->ccw_device);

	qdio_free_buffers(qdio->req_q, QDIO_MAX_BUFFERS_PER_Q);
	qdio_free_buffers(qdio->res_q, QDIO_MAX_BUFFERS_PER_Q);
	kfree(qdio);
}

int zfcp_qdio_setup(struct zfcp_adapter *adapter)
{
	struct zfcp_qdio *qdio;

	qdio = kzalloc(sizeof(struct zfcp_qdio), GFP_KERNEL);
	if (!qdio)
		return -ENOMEM;

	qdio->adapter = adapter;

	if (zfcp_qdio_allocate(qdio)) {
		kfree(qdio);
		return -ENOMEM;
	}

	spin_lock_init(&qdio->req_q_lock);
	spin_lock_init(&qdio->stat_lock);

	adapter->qdio = qdio;
	return 0;
}

/**
 * zfcp_qdio_siosl - Trigger logging in FCP channel
 * @adapter: The zfcp_adapter where to trigger logging
 *
 * Call the cio siosl function to trigger hardware logging.  This
 * wrapper function sets a flag to ensure hardware logging is only
 * triggered once before going through qdio shutdown.
 *
 * The triggers are always run from qdio tasklet context, so no
 * additional synchronization is necessary.
 */
void zfcp_qdio_siosl(struct zfcp_adapter *adapter)
{
	int rc;

	if (atomic_read(&adapter->status) & ZFCP_STATUS_ADAPTER_SIOSL_ISSUED)
		return;

	rc = ccw_device_siosl(adapter->ccw_device);
	if (!rc)
		atomic_or(ZFCP_STATUS_ADAPTER_SIOSL_ISSUED,
				&adapter->status);
}
