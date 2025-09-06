/*
 * linux/drivers/s390/cio/qdio_main.c
 *
 * Linux for s390 qdio support, buffer handling, qdio API and module support.
 *
 * Copyright 2000,2008 IBM Corp.
 * Linux for s390 qdio support, buffer handling, qdio API and module support.
 *
 * Copyright IBM Corp. 2000, 2008
 * Author(s): Utz Bacher <utz.bacher@de.ibm.com>
 *	      Jan Glauber <jang@linux.vnet.ibm.com>
 * 2.6 cio integration by Cornelia Huck <cornelia.huck@de.ibm.com>
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <asm/atomic.h>
#include <asm/debug.h>
#include <asm/qdio.h>
#include <linux/gfp.h>
#include <linux/io.h>
#include <linux/atomic.h>
#include <asm/debug.h>
#include <asm/qdio.h>
#include <asm/ipl.h>

#include "cio.h"
#include "css.h"
#include "device.h"
#include "qdio.h"
#include "qdio_debug.h"
#include "qdio_perf.h"

MODULE_AUTHOR("Utz Bacher <utz.bacher@de.ibm.com>,"\
	"Jan Glauber <jang@linux.vnet.ibm.com>");
MODULE_DESCRIPTION("QDIO base support");
MODULE_LICENSE("GPL");

static inline int do_siga_sync(struct subchannel_id schid,
			       unsigned int out_mask, unsigned int in_mask)
{
	register unsigned long __fc asm ("0") = 2;
	register struct subchannel_id __schid asm ("1") = schid;
static inline int do_siga_sync(unsigned long schid,
			       unsigned int out_mask, unsigned int in_mask,
			       unsigned int fc)
{
	register unsigned long __fc asm ("0") = fc;
	register unsigned long __schid asm ("1") = schid;
	register unsigned long out asm ("2") = out_mask;
	register unsigned long in asm ("3") = in_mask;
	int cc;

	asm volatile(
		"	siga	0\n"
		"	ipm	%0\n"
		"	srl	%0,28\n"
		: "=d" (cc)
		: "d" (__fc), "d" (__schid), "d" (out), "d" (in) : "cc");
	return cc;
}

static inline int do_siga_input(struct subchannel_id schid, unsigned int mask)
{
	register unsigned long __fc asm ("0") = 1;
	register struct subchannel_id __schid asm ("1") = schid;
static inline int do_siga_input(unsigned long schid, unsigned int mask,
				unsigned int fc)
{
	register unsigned long __fc asm ("0") = fc;
	register unsigned long __schid asm ("1") = schid;
	register unsigned long __mask asm ("2") = mask;
	int cc;

	asm volatile(
		"	siga	0\n"
		"	ipm	%0\n"
		"	srl	%0,28\n"
		: "=d" (cc)
		: "d" (__fc), "d" (__schid), "d" (__mask) : "cc", "memory");
		: "d" (__fc), "d" (__schid), "d" (__mask) : "cc");
	return cc;
}

/**
 * do_siga_output - perform SIGA-w/wt function
 * @schid: subchannel id or in case of QEBSM the subchannel token
 * @mask: which output queues to process
 * @bb: busy bit indicator, set only if SIGA-w/wt could not access a buffer
 * @fc: function code to perform
 *
 * Returns cc or QDIO_ERROR_SIGA_ACCESS_EXCEPTION.
 * Note: For IQDC unicast queues only the highest priority queue is processed.
 */
static inline int do_siga_output(unsigned long schid, unsigned long mask,
				 u32 *bb, unsigned int fc)
 * Returns condition code.
 * Note: For IQDC unicast queues only the highest priority queue is processed.
 */
static inline int do_siga_output(unsigned long schid, unsigned long mask,
				 unsigned int *bb, unsigned int fc,
				 unsigned long aob)
{
	register unsigned long __fc asm("0") = fc;
	register unsigned long __schid asm("1") = schid;
	register unsigned long __mask asm("2") = mask;
	int cc = QDIO_ERROR_SIGA_ACCESS_EXCEPTION;

	asm volatile(
		"	siga	0\n"
		"0:	ipm	%0\n"
		"	srl	%0,28\n"
		"1:\n"
		EX_TABLE(0b, 1b)
		: "+d" (cc), "+d" (__fc), "+d" (__schid), "+d" (__mask)
		: : "cc", "memory");
	*bb = ((unsigned int) __fc) >> 31;
	register unsigned long __aob asm("3") = aob;
	int cc;

	asm volatile(
		"	siga	0\n"
		"	ipm	%0\n"
		"	srl	%0,28\n"
		: "=d" (cc), "+d" (__fc), "+d" (__aob)
		: "d" (__schid), "d" (__mask)
		: "cc");
	*bb = __fc >> 31;
	return cc;
}

static inline int qdio_check_ccq(struct qdio_q *q, unsigned int ccq)
{
	char dbf_text[15];

	/* all done or next buffer state different */
	if (ccq == 0 || ccq == 32)
		return 0;
	/* not all buffers processed */
	if (ccq == 96 || ccq == 97)
		return 1;
	/* notify devices immediately */
	sprintf(dbf_text, "%d", ccq);
	QDIO_DBF_TEXT2(1, trace, dbf_text);
	/* all done or next buffer state different */
	if (ccq == 0 || ccq == 32)
		return 0;
	/* no buffer processed */
	if (ccq == 97)
		return 1;
	/* not all buffers processed */
	if (ccq == 96)
		return 2;
	/* notify devices immediately */
	DBF_ERROR("%4x ccq:%3d", SCH_NO(q), ccq);
	return -EIO;
}

/**
 * qdio_do_eqbs - extract buffer states for QEBSM
 * @q: queue to manipulate
 * @state: state of the extracted buffers
 * @start: buffer number to start at
 * @count: count of buffers to examine
 *
 * Returns the number of successfull extracted equal buffer states.
 * Stops processing if a state is different from the last buffers state.
 */
static int qdio_do_eqbs(struct qdio_q *q, unsigned char *state,
			int start, int count)
{
	unsigned int ccq = 0;
	int tmp_count = count, tmp_start = start;
	int nr = q->nr;
	int rc;
	char dbf_text[15];

	BUG_ON(!q->irq_ptr->sch_token);
 * @auto_ack: automatically acknowledge buffers
 *
 * Returns the number of successfully extracted equal buffer states.
 * Stops processing if a state is different from the last buffers state.
 */
static int qdio_do_eqbs(struct qdio_q *q, unsigned char *state,
			int start, int count, int auto_ack)
{
	int rc, tmp_count = count, tmp_start = start, nr = q->nr;
	unsigned int ccq = 0;

	qperf_inc(q, eqbs);

	if (!q->is_input_q)
		nr += q->irq_ptr->nr_input_qs;
again:
	ccq = do_eqbs(q->irq_ptr->sch_token, state, nr, &tmp_start, &tmp_count);
	rc = qdio_check_ccq(q, ccq);

	/* At least one buffer was processed, return and extract the remaining
	 * buffers later.
	 */
	if ((ccq == 96) && (count != tmp_count))
		return (count - tmp_count);
	if (rc == 1) {
		QDIO_DBF_TEXT5(1, trace, "eqAGAIN");
		goto again;
	}

	if (rc < 0) {
		QDIO_DBF_TEXT2(1, trace, "eqberr");
		sprintf(dbf_text, "%2x,%2x,%d,%d", count, tmp_count, ccq, nr);
		QDIO_DBF_TEXT2(1, trace, dbf_text);
		q->handler(q->irq_ptr->cdev,
			   QDIO_ERROR_ACTIVATE_CHECK_CONDITION,
			   0, -1, -1, q->irq_ptr->int_parm);
		return 0;
	}
	return count - tmp_count;
	ccq = do_eqbs(q->irq_ptr->sch_token, state, nr, &tmp_start, &tmp_count,
		      auto_ack);
	rc = qdio_check_ccq(q, ccq);
	if (!rc)
		return count - tmp_count;

	if (rc == 1) {
		DBF_DEV_EVENT(DBF_WARN, q->irq_ptr, "EQBS again:%2d", ccq);
		goto again;
	}

	if (rc == 2) {
		qperf_inc(q, eqbs_partial);
		DBF_DEV_EVENT(DBF_WARN, q->irq_ptr, "EQBS part:%02x",
			tmp_count);
		return count - tmp_count;
	}

	DBF_ERROR("%4x EQBS ERROR", SCH_NO(q));
	DBF_ERROR("%3d%3d%2d", count, tmp_count, nr);
	q->handler(q->irq_ptr->cdev, QDIO_ERROR_GET_BUF_STATE,
		   q->nr, q->first_to_kick, count, q->irq_ptr->int_parm);
	return 0;
}

/**
 * qdio_do_sqbs - set buffer states for QEBSM
 * @q: queue to manipulate
 * @state: new state of the buffers
 * @start: first buffer number to change
 * @count: how many buffers to change
 *
 * Returns the number of successfully changed buffers.
 * Does retrying until the specified count of buffer states is set or an
 * error occurs.
 */
static int qdio_do_sqbs(struct qdio_q *q, unsigned char state, int start,
			int count)
{
	unsigned int ccq = 0;
	int tmp_count = count, tmp_start = start;
	int nr = q->nr;
	int rc;
	char dbf_text[15];

	BUG_ON(!q->irq_ptr->sch_token);

	if (!count)
		return 0;
	qperf_inc(q, sqbs);

	if (!q->is_input_q)
		nr += q->irq_ptr->nr_input_qs;
again:
	ccq = do_sqbs(q->irq_ptr->sch_token, state, nr, &tmp_start, &tmp_count);
	rc = qdio_check_ccq(q, ccq);
	if (rc == 1) {
		QDIO_DBF_TEXT5(1, trace, "sqAGAIN");
		goto again;
	}
	if (rc < 0) {
		QDIO_DBF_TEXT3(1, trace, "sqberr");
		sprintf(dbf_text, "%2x,%2x", count, tmp_count);
		QDIO_DBF_TEXT3(1, trace, dbf_text);
		sprintf(dbf_text, "%d,%d", ccq, nr);
		QDIO_DBF_TEXT3(1, trace, dbf_text);

		q->handler(q->irq_ptr->cdev,
			   QDIO_ERROR_ACTIVATE_CHECK_CONDITION,
			   0, -1, -1, q->irq_ptr->int_parm);
		return 0;
	}
	WARN_ON(tmp_count);
	return count - tmp_count;
	if (!rc) {
		WARN_ON_ONCE(tmp_count);
		return count - tmp_count;
	}

	if (rc == 1 || rc == 2) {
		DBF_DEV_EVENT(DBF_INFO, q->irq_ptr, "SQBS again:%2d", ccq);
		qperf_inc(q, sqbs_partial);
		goto again;
	}

	DBF_ERROR("%4x SQBS ERROR", SCH_NO(q));
	DBF_ERROR("%3d%3d%2d", count, tmp_count, nr);
	q->handler(q->irq_ptr->cdev, QDIO_ERROR_SET_BUF_STATE,
		   q->nr, q->first_to_kick, count, q->irq_ptr->int_parm);
	return 0;
}

/*
 * Returns number of examined buffers and their common state in *state.
 * Requested number of buffers-to-examine must be > 0.
 */
static inline int get_buf_states(struct qdio_q *q, unsigned int bufnr,
				 unsigned char *state, unsigned int count)
				 unsigned char *state, unsigned int count,
				 int auto_ack, int merge_pending)
{
	unsigned char __state = 0;
	int i;

	BUG_ON(bufnr > QDIO_MAX_BUFFERS_MASK);
	BUG_ON(count > QDIO_MAX_BUFFERS_PER_Q);

	if (is_qebsm(q))
		return qdio_do_eqbs(q, state, bufnr, count);

	for (i = 0; i < count; i++) {
		if (!__state)
			__state = q->slsb.val[bufnr];
		else if (q->slsb.val[bufnr] != __state)
	if (is_qebsm(q))
		return qdio_do_eqbs(q, state, bufnr, count, auto_ack);

	/* get initial state: */
	__state = q->slsb.val[bufnr];
	if (merge_pending && __state == SLSB_P_OUTPUT_PENDING)
		__state = SLSB_P_OUTPUT_EMPTY;

	for (i = 1; i < count; i++) {
		bufnr = next_buf(bufnr);

		/* merge PENDING into EMPTY: */
		if (merge_pending &&
		    q->slsb.val[bufnr] == SLSB_P_OUTPUT_PENDING &&
		    __state == SLSB_P_OUTPUT_EMPTY)
			continue;

		/* stop if next state differs from initial state: */
		if (q->slsb.val[bufnr] != __state)
			break;
	}
	*state = __state;
	return i;
}

inline int get_buf_state(struct qdio_q *q, unsigned int bufnr,
		  unsigned char *state)
{
	return get_buf_states(q, bufnr, state, 1);
static inline int get_buf_state(struct qdio_q *q, unsigned int bufnr,
				unsigned char *state, int auto_ack)
{
	return get_buf_states(q, bufnr, state, 1, auto_ack, 0);
}

/* wrap-around safe setting of slsb states, returns number of changed buffers */
static inline int set_buf_states(struct qdio_q *q, int bufnr,
				 unsigned char state, int count)
{
	int i;

	BUG_ON(bufnr > QDIO_MAX_BUFFERS_MASK);
	BUG_ON(count > QDIO_MAX_BUFFERS_PER_Q);

	if (is_qebsm(q))
		return qdio_do_sqbs(q, state, bufnr, count);

	for (i = 0; i < count; i++) {
		xchg(&q->slsb.val[bufnr], state);
		bufnr = next_buf(bufnr);
	}
	return count;
}

static inline int set_buf_state(struct qdio_q *q, int bufnr,
				unsigned char state)
{
	return set_buf_states(q, bufnr, state, 1);
}

/* set slsb states to initial state */
void qdio_init_buf_states(struct qdio_irq *irq_ptr)
static void qdio_init_buf_states(struct qdio_irq *irq_ptr)
{
	struct qdio_q *q;
	int i;

	for_each_input_queue(irq_ptr, q, i)
		set_buf_states(q, 0, SLSB_P_INPUT_NOT_INIT,
			       QDIO_MAX_BUFFERS_PER_Q);
	for_each_output_queue(irq_ptr, q, i)
		set_buf_states(q, 0, SLSB_P_OUTPUT_NOT_INIT,
			       QDIO_MAX_BUFFERS_PER_Q);
}

static int qdio_siga_sync(struct qdio_q *q, unsigned int output,
			  unsigned int input)
{
	int cc;

	if (!need_siga_sync(q))
		return 0;

	qdio_perf_stat_inc(&perf_stats.siga_sync);

	cc = do_siga_sync(q->irq_ptr->schid, output, input);
	if (cc) {
		QDIO_DBF_TEXT4(0, trace, "sigasync");
		QDIO_DBF_HEX4(0, trace, &q, sizeof(void *));
		QDIO_DBF_HEX3(0, trace, &cc, sizeof(int *));
	}
	return cc;
}

inline int qdio_siga_sync_q(struct qdio_q *q)
static inline int qdio_siga_sync(struct qdio_q *q, unsigned int output,
			  unsigned int input)
{
	unsigned long schid = *((u32 *) &q->irq_ptr->schid);
	unsigned int fc = QDIO_SIGA_SYNC;
	int cc;

	DBF_DEV_EVENT(DBF_INFO, q->irq_ptr, "siga-s:%1d", q->nr);
	qperf_inc(q, siga_sync);

	if (is_qebsm(q)) {
		schid = q->irq_ptr->sch_token;
		fc |= QDIO_SIGA_QEBSM_FLAG;
	}

	cc = do_siga_sync(schid, output, input, fc);
	if (unlikely(cc))
		DBF_ERROR("%4x SIGA-S:%2d", SCH_NO(q), cc);
	return (cc) ? -EIO : 0;
}

static inline int qdio_siga_sync_q(struct qdio_q *q)
{
	if (q->is_input_q)
		return qdio_siga_sync(q, 0, q->mask);
	else
		return qdio_siga_sync(q, q->mask, 0);
}

static inline int qdio_siga_sync_out(struct qdio_q *q)
{
	return qdio_siga_sync(q, ~0U, 0);
}

static inline int qdio_siga_sync_all(struct qdio_q *q)
{
	return qdio_siga_sync(q, ~0U, ~0U);
}

static inline int qdio_do_siga_output(struct qdio_q *q, unsigned int *busy_bit)
{
	unsigned int fc = 0;
	unsigned long schid;

	if (!is_qebsm(q))
		schid = *((u32 *)&q->irq_ptr->schid);
	else {
		schid = q->irq_ptr->sch_token;
		fc |= 0x80;
	}
	return do_siga_output(schid, q->mask, busy_bit, fc);
}

static int qdio_siga_output(struct qdio_q *q)
{
	int cc;
	u32 busy_bit;
	u64 start_time = 0;
	char dbf_text[15];

	QDIO_DBF_TEXT5(0, trace, "sigaout");
	QDIO_DBF_HEX5(0, trace, &q, sizeof(void *));

	qdio_perf_stat_inc(&perf_stats.siga_out);
again:
	cc = qdio_do_siga_output(q, &busy_bit);
	if (queue_type(q) == QDIO_IQDIO_QFMT && cc == 2 && busy_bit) {
		sprintf(dbf_text, "bb%4x%2x", q->irq_ptr->schid.sch_no, q->nr);
		QDIO_DBF_TEXT3(0, trace, dbf_text);

		if (!start_time)
			start_time = get_usecs();
		else if ((get_usecs() - start_time) < QDIO_BUSY_BIT_PATIENCE)
			goto again;
	}

	if (cc == 2 && busy_bit)
		cc |= QDIO_ERROR_SIGA_BUSY;
	if (cc)
		QDIO_DBF_HEX3(0, trace, &cc, sizeof(int *));
static int qdio_siga_output(struct qdio_q *q, unsigned int *busy_bit,
	unsigned long aob)
{
	unsigned long schid = *((u32 *) &q->irq_ptr->schid);
	unsigned int fc = QDIO_SIGA_WRITE;
	u64 start_time = 0;
	int retries = 0, cc;
	unsigned long laob = 0;

	WARN_ON_ONCE(aob && ((queue_type(q) != QDIO_IQDIO_QFMT) ||
			     !q->u.out.use_cq));
	if (q->u.out.use_cq && aob != 0) {
		fc = QDIO_SIGA_WRITEQ;
		laob = aob;
	}

	if (is_qebsm(q)) {
		schid = q->irq_ptr->sch_token;
		fc |= QDIO_SIGA_QEBSM_FLAG;
	}
again:
	cc = do_siga_output(schid, q->mask, busy_bit, fc, laob);

	/* hipersocket busy condition */
	if (unlikely(*busy_bit)) {
		retries++;

		if (!start_time) {
			start_time = get_tod_clock_fast();
			goto again;
		}
		if (get_tod_clock_fast() - start_time < QDIO_BUSY_BIT_PATIENCE)
			goto again;
	}
	if (retries) {
		DBF_DEV_EVENT(DBF_WARN, q->irq_ptr,
			      "%4x cc2 BB1:%1d", SCH_NO(q), q->nr);
		DBF_DEV_EVENT(DBF_WARN, q->irq_ptr, "count:%u", retries);
	}
	return cc;
}

static inline int qdio_siga_input(struct qdio_q *q)
{
	int cc;

	QDIO_DBF_TEXT4(0, trace, "sigain");
	QDIO_DBF_HEX4(0, trace, &q, sizeof(void *));

	qdio_perf_stat_inc(&perf_stats.siga_in);

	cc = do_siga_input(q->irq_ptr->schid, q->mask);
	if (cc)
		QDIO_DBF_HEX3(0, trace, &cc, sizeof(int *));
	return cc;
}

/* called from thinint inbound handler */
void qdio_sync_after_thinint(struct qdio_q *q)
{
	if (pci_out_supported(q)) {
		if (need_siga_sync_thinint(q))
			qdio_siga_sync_all(q);
		else if (need_siga_sync_out_thinint(q))
			qdio_siga_sync_out(q);
	} else
		qdio_siga_sync_q(q);
}

inline void qdio_stop_polling(struct qdio_q *q)
{
	spin_lock_bh(&q->u.in.lock);
	if (!q->u.in.polling) {
		spin_unlock_bh(&q->u.in.lock);
		return;
	}
	q->u.in.polling = 0;
	qdio_perf_stat_inc(&perf_stats.debug_stop_polling);

	/* show the card that we are not polling anymore */
	set_buf_state(q, q->last_move_ftc, SLSB_P_INPUT_NOT_INIT);
	spin_unlock_bh(&q->u.in.lock);
}

static void announce_buffer_error(struct qdio_q *q)
{
	char dbf_text[15];

	if (q->is_input_q)
		QDIO_DBF_TEXT3(1, trace, "inperr");
	else
		QDIO_DBF_TEXT3(0, trace, "outperr");

	sprintf(dbf_text, "%x-%x-%x", q->first_to_check,
		q->sbal[q->first_to_check]->element[14].flags,
		q->sbal[q->first_to_check]->element[15].flags);
	QDIO_DBF_TEXT3(1, trace, dbf_text);
	QDIO_DBF_HEX2(1, trace, q->sbal[q->first_to_check], 256);

	q->qdio_error = QDIO_ERROR_SLSB_STATE;
	unsigned long schid = *((u32 *) &q->irq_ptr->schid);
	unsigned int fc = QDIO_SIGA_READ;
	int cc;

	DBF_DEV_EVENT(DBF_INFO, q->irq_ptr, "siga-r:%1d", q->nr);
	qperf_inc(q, siga_read);

	if (is_qebsm(q)) {
		schid = q->irq_ptr->sch_token;
		fc |= QDIO_SIGA_QEBSM_FLAG;
	}

	cc = do_siga_input(schid, q->mask, fc);
	if (unlikely(cc))
		DBF_ERROR("%4x SIGA-R:%2d", SCH_NO(q), cc);
	return (cc) ? -EIO : 0;
}

#define qdio_siga_sync_out(q) qdio_siga_sync(q, ~0U, 0)
#define qdio_siga_sync_all(q) qdio_siga_sync(q, ~0U, ~0U)

static inline void qdio_sync_queues(struct qdio_q *q)
{
	/* PCI capable outbound queues will also be scanned so sync them too */
	if (pci_out_supported(q))
		qdio_siga_sync_all(q);
	else
		qdio_siga_sync_q(q);
}

int debug_get_buf_state(struct qdio_q *q, unsigned int bufnr,
			unsigned char *state)
{
	if (need_siga_sync(q))
		qdio_siga_sync_q(q);
	return get_buf_states(q, bufnr, state, 1, 0, 0);
}

static inline void qdio_stop_polling(struct qdio_q *q)
{
	if (!q->u.in.polling)
		return;

	q->u.in.polling = 0;
	qperf_inc(q, stop_polling);

	/* show the card that we are not polling anymore */
	if (is_qebsm(q)) {
		set_buf_states(q, q->u.in.ack_start, SLSB_P_INPUT_NOT_INIT,
			       q->u.in.ack_count);
		q->u.in.ack_count = 0;
	} else
		set_buf_state(q, q->u.in.ack_start, SLSB_P_INPUT_NOT_INIT);
}

static inline void account_sbals(struct qdio_q *q, unsigned int count)
{
	int pos;

	q->q_stats.nr_sbal_total += count;
	if (count == QDIO_MAX_BUFFERS_MASK) {
		q->q_stats.nr_sbals[7]++;
		return;
	}
	pos = ilog2(count);
	q->q_stats.nr_sbals[pos]++;
}

static void process_buffer_error(struct qdio_q *q, int count)
{
	unsigned char state = (q->is_input_q) ? SLSB_P_INPUT_NOT_INIT :
					SLSB_P_OUTPUT_NOT_INIT;

	q->qdio_error = QDIO_ERROR_SLSB_STATE;

	/* special handling for no target buffer empty */
	if ((!q->is_input_q &&
	    (q->sbal[q->first_to_check]->element[15].sflags) == 0x10)) {
		qperf_inc(q, target_full);
		DBF_DEV_EVENT(DBF_INFO, q->irq_ptr, "OUTFULL FTC:%02x",
			      q->first_to_check);
		goto set;
	}

	DBF_ERROR("%4x BUF ERROR", SCH_NO(q));
	DBF_ERROR((q->is_input_q) ? "IN:%2d" : "OUT:%2d", q->nr);
	DBF_ERROR("FTC:%3d C:%3d", q->first_to_check, count);
	DBF_ERROR("F14:%2x F15:%2x",
		  q->sbal[q->first_to_check]->element[14].sflags,
		  q->sbal[q->first_to_check]->element[15].sflags);

set:
	/*
	 * Interrupts may be avoided as long as the error is present
	 * so change the buffer state immediately to avoid starvation.
	 */
	set_buf_states(q, q->first_to_check, state, count);
}

static inline void inbound_primed(struct qdio_q *q, int count)
{
	int new;

	DBF_DEV_EVENT(DBF_INFO, q->irq_ptr, "in prim: %02x", count);

	/* for QEBSM the ACK was already set by EQBS */
	if (is_qebsm(q)) {
		if (!q->u.in.polling) {
			q->u.in.polling = 1;
			q->u.in.ack_count = count;
			q->u.in.ack_start = q->first_to_check;
			return;
		}

		/* delete the previous ACK's */
		set_buf_states(q, q->u.in.ack_start, SLSB_P_INPUT_NOT_INIT,
			       q->u.in.ack_count);
		q->u.in.ack_count = count;
		q->u.in.ack_start = q->first_to_check;
		return;
	}

	/*
	 * ACK the newest buffer. The ACK will be removed in qdio_stop_polling
	 * or by the next inbound run.
	 */
	new = add_buf(q->first_to_check, count - 1);
	if (q->u.in.polling) {
		/* reset the previous ACK but first set the new one */
		set_buf_state(q, new, SLSB_P_INPUT_ACK);
		set_buf_state(q, q->u.in.ack_start, SLSB_P_INPUT_NOT_INIT);
	} else {
		q->u.in.polling = 1;
		set_buf_state(q, new, SLSB_P_INPUT_ACK);
	}

	q->u.in.ack_start = new;
	count--;
	if (!count)
		return;
	/* need to change ALL buffers to get more interrupts */
	set_buf_states(q, q->first_to_check, SLSB_P_INPUT_NOT_INIT, count);
}

static int get_inbound_buffer_frontier(struct qdio_q *q)
{
	int count, stop;
	unsigned char state;

	/*
	 * If we still poll don't update last_move_ftc, keep the
	 * previously ACK buffer there.
	 */
	if (!q->u.in.polling)
		q->last_move_ftc = q->first_to_check;
	unsigned char state = 0;

	q->timestamp = get_tod_clock_fast();

	/*
	 * Don't check 128 buffers, as otherwise qdio_inbound_q_moved
	 * would return 0.
	 */
	count = min(atomic_read(&q->nr_buf_used), QDIO_MAX_BUFFERS_MASK);
	stop = add_buf(q->first_to_check, count);

	/*
	 * No siga sync here, as a PCI or we after a thin interrupt
	 * will sync the queues.
	 */

	/* need to set count to 1 for non-qebsm */
	if (!is_qebsm(q))
		count = 1;

check_next:
	if (q->first_to_check == stop)
		goto out;

	count = get_buf_states(q, q->first_to_check, &state, count);
	if (q->first_to_check == stop)
		goto out;

	/*
	 * No siga sync here, as a PCI or we after a thin interrupt
	 * already sync'ed the queues.
	 */
	count = get_buf_states(q, q->first_to_check, &state, count, 1, 0);
	if (!count)
		goto out;

	switch (state) {
	case SLSB_P_INPUT_PRIMED:
		QDIO_DBF_TEXT5(0, trace, "inptprim");

		/*
		 * Only ACK the first buffer. The ACK will be removed in
		 * qdio_stop_polling.
		 */
		if (q->u.in.polling)
			state = SLSB_P_INPUT_NOT_INIT;
		else {
			q->u.in.polling = 1;
			state = SLSB_P_INPUT_ACK;
		}
		set_buf_state(q, q->first_to_check, state);

		/*
		 * Need to change all PRIMED buffers to NOT_INIT, otherwise
		 * we're loosing initiative in the thinint code.
		 */
		if (count > 1)
			set_buf_states(q, next_buf(q->first_to_check),
				       SLSB_P_INPUT_NOT_INIT, count - 1);

		/*
		 * No siga-sync needed for non-qebsm here, as the inbound queue
		 * will be synced on the next siga-r, resp.
		 * tiqdio_is_inbound_q_done will do the siga-sync.
		 */
		q->first_to_check = add_buf(q->first_to_check, count);
		atomic_sub(count, &q->nr_buf_used);
		goto check_next;
	case SLSB_P_INPUT_ERROR:
		announce_buffer_error(q);
		/* process the buffer, the upper layer will take care of it */
		q->first_to_check = add_buf(q->first_to_check, count);
		atomic_sub(count, &q->nr_buf_used);
		inbound_primed(q, count);
		q->first_to_check = add_buf(q->first_to_check, count);
		if (atomic_sub_return(count, &q->nr_buf_used) == 0)
			qperf_inc(q, inbound_queue_full);
		if (q->irq_ptr->perf_stat_enabled)
			account_sbals(q, count);
		break;
	case SLSB_P_INPUT_ERROR:
		process_buffer_error(q, count);
		q->first_to_check = add_buf(q->first_to_check, count);
		atomic_sub(count, &q->nr_buf_used);
		if (q->irq_ptr->perf_stat_enabled)
			account_sbals_error(q, count);
		break;
	case SLSB_CU_INPUT_EMPTY:
	case SLSB_P_INPUT_NOT_INIT:
	case SLSB_P_INPUT_ACK:
		QDIO_DBF_TEXT5(0, trace, "inpnipro");
		break;
	default:
		BUG();
	}
out:
	QDIO_DBF_HEX4(0, trace, &q->first_to_check, sizeof(int));
	return q->first_to_check;
}

int qdio_inbound_q_moved(struct qdio_q *q)
		if (q->irq_ptr->perf_stat_enabled)
			q->q_stats.nr_sbal_nop++;
		DBF_DEV_EVENT(DBF_INFO, q->irq_ptr, "in nop");
		break;
	default:
		WARN_ON_ONCE(1);
	}
out:
	return q->first_to_check;
}

static int qdio_inbound_q_moved(struct qdio_q *q)
{
	int bufnr;

	bufnr = get_inbound_buffer_frontier(q);

	if ((bufnr != q->last_move_ftc) || q->qdio_error) {
		if (!need_siga_sync(q) && !pci_out_supported(q))
			q->u.in.timestamp = get_usecs();

		QDIO_DBF_TEXT4(0, trace, "inhasmvd");
		QDIO_DBF_HEX4(0, trace, &q, sizeof(void *));
	if (bufnr != q->last_move) {
		q->last_move = bufnr;
		if (!is_thinint_irq(q->irq_ptr) && MACHINE_IS_LPAR)
			q->u.in.timestamp = get_tod_clock();
		return 1;
	} else
		return 0;
}

static int qdio_inbound_q_done(struct qdio_q *q)
{
	unsigned char state;
#ifdef CONFIG_QDIO_DEBUG
	char dbf_text[15];
#endif
static inline int qdio_inbound_q_done(struct qdio_q *q)
{
	unsigned char state = 0;

	if (!atomic_read(&q->nr_buf_used))
		return 1;

	/*
	 * We need that one for synchronization with the adapter, as it
	 * does a kind of PCI avoidance.
	 */
	qdio_siga_sync_q(q);

	get_buf_state(q, q->first_to_check, &state);
	if (state == SLSB_P_INPUT_PRIMED)
		/* we got something to do */
		return 0;

	/* on VM, we don't poll, so the q is always done here */
	if (need_siga_sync(q) || pci_out_supported(q))
	if (need_siga_sync(q))
		qdio_siga_sync_q(q);
	get_buf_state(q, q->first_to_check, &state, 0);

	if (state == SLSB_P_INPUT_PRIMED || state == SLSB_P_INPUT_ERROR)
		/* more work coming */
		return 0;

	if (is_thinint_irq(q->irq_ptr))
		return 1;

	/* don't poll under z/VM */
	if (MACHINE_IS_VM)
		return 1;

	/*
	 * At this point we know, that inbound first_to_check
	 * has (probably) not moved (see qdio_inbound_processing).
	 */
	if (get_usecs() > q->u.in.timestamp + QDIO_INPUT_THRESHOLD) {
#ifdef CONFIG_QDIO_DEBUG
		QDIO_DBF_TEXT4(0, trace, "inqisdon");
		QDIO_DBF_HEX4(0, trace, &q, sizeof(void *));
		sprintf(dbf_text, "pf%02x", q->first_to_check);
		QDIO_DBF_TEXT4(0, trace, dbf_text);
#endif /* CONFIG_QDIO_DEBUG */
		return 1;
	} else {
#ifdef CONFIG_QDIO_DEBUG
		QDIO_DBF_TEXT4(0, trace, "inqisntd");
		QDIO_DBF_HEX4(0, trace, &q, sizeof(void *));
		sprintf(dbf_text, "pf%02x", q->first_to_check);
		QDIO_DBF_TEXT4(0, trace, dbf_text);
#endif /* CONFIG_QDIO_DEBUG */
		return 0;
	}
}

void qdio_kick_inbound_handler(struct qdio_q *q)
{
	int count, start, end;
#ifdef CONFIG_QDIO_DEBUG
	char dbf_text[15];
#endif

	qdio_perf_stat_inc(&perf_stats.inbound_handler);

	start = q->first_to_kick;
	end = q->first_to_check;
	if (end >= start)
		count = end - start;
	else
		count = end + QDIO_MAX_BUFFERS_PER_Q - start;

#ifdef CONFIG_QDIO_DEBUG
	sprintf(dbf_text, "s=%2xc=%2x", start, count);
	QDIO_DBF_TEXT4(0, trace, dbf_text);
#endif /* CONFIG_QDIO_DEBUG */
	if (get_tod_clock_fast() > q->u.in.timestamp + QDIO_INPUT_THRESHOLD) {
		DBF_DEV_EVENT(DBF_INFO, q->irq_ptr, "in done:%02x",
			      q->first_to_check);
		return 1;
	} else
		return 0;
}

static inline int contains_aobs(struct qdio_q *q)
{
	return !q->is_input_q && q->u.out.use_cq;
}

static inline void qdio_handle_aobs(struct qdio_q *q, int start, int count)
{
	unsigned char state = 0;
	int j, b = start;

	if (!contains_aobs(q))
		return;

	for (j = 0; j < count; ++j) {
		get_buf_state(q, b, &state, 0);
		if (state == SLSB_P_OUTPUT_PENDING) {
			struct qaob *aob = q->u.out.aobs[b];
			if (aob == NULL)
				continue;

			q->u.out.sbal_state[b].flags |=
				QDIO_OUTBUF_STATE_FLAG_PENDING;
			q->u.out.aobs[b] = NULL;
		} else if (state == SLSB_P_OUTPUT_EMPTY) {
			q->u.out.sbal_state[b].aob = NULL;
		}
		b = next_buf(b);
	}
}

static inline unsigned long qdio_aob_for_buffer(struct qdio_output_q *q,
					int bufnr)
{
	unsigned long phys_aob = 0;

	if (!q->use_cq)
		return 0;

	if (!q->aobs[bufnr]) {
		struct qaob *aob = qdio_allocate_aob();
		q->aobs[bufnr] = aob;
	}
	if (q->aobs[bufnr]) {
		q->sbal_state[bufnr].aob = q->aobs[bufnr];
		q->aobs[bufnr]->user1 = (u64) q->sbal_state[bufnr].user;
		phys_aob = virt_to_phys(q->aobs[bufnr]);
		WARN_ON_ONCE(phys_aob & 0xFF);
	}

	q->sbal_state[bufnr].flags = 0;
	return phys_aob;
}

static void qdio_kick_handler(struct qdio_q *q)
{
	int start = q->first_to_kick;
	int end = q->first_to_check;
	int count;

	if (unlikely(q->irq_ptr->state != QDIO_IRQ_STATE_ACTIVE))
		return;

	q->handler(q->irq_ptr->cdev, q->qdio_error, q->nr,
		   start, count, q->irq_ptr->int_parm);

	/* for the next time */
	q->first_to_kick = q->first_to_check;
	count = sub_buf(end, start);

	if (q->is_input_q) {
		qperf_inc(q, inbound_handler);
		DBF_DEV_EVENT(DBF_INFO, q->irq_ptr, "kih s:%02x c:%02x", start, count);
	} else {
		qperf_inc(q, outbound_handler);
		DBF_DEV_EVENT(DBF_INFO, q->irq_ptr, "koh: s:%02x c:%02x",
			      start, count);
	}

	qdio_handle_aobs(q, start, count);

	q->handler(q->irq_ptr->cdev, q->qdio_error, q->nr, start, count,
		   q->irq_ptr->int_parm);

	/* for the next time */
	q->first_to_kick = end;
	q->qdio_error = 0;
}

static void __qdio_inbound_processing(struct qdio_q *q)
{
	qdio_perf_stat_inc(&perf_stats.tasklet_inbound);
again:
	if (!qdio_inbound_q_moved(q))
		return;

	qdio_kick_inbound_handler(q);

	if (!qdio_inbound_q_done(q))
		/* means poll time is not yet over */
		goto again;
	qperf_inc(q, tasklet_inbound);

	if (!qdio_inbound_q_moved(q))
		return;

	qdio_kick_handler(q);

	if (!qdio_inbound_q_done(q)) {
		/* means poll time is not yet over */
		qperf_inc(q, tasklet_inbound_resched);
		if (likely(q->irq_ptr->state != QDIO_IRQ_STATE_STOPPED)) {
			tasklet_schedule(&q->tasklet);
			return;
		}
	}

	qdio_stop_polling(q);
	/*
	 * We need to check again to not lose initiative after
	 * resetting the ACK state.
	 */
	if (!qdio_inbound_q_done(q))
		goto again;
}

/* inbound tasklet */
	if (!qdio_inbound_q_done(q)) {
		qperf_inc(q, tasklet_inbound_resched2);
		if (likely(q->irq_ptr->state != QDIO_IRQ_STATE_STOPPED))
			tasklet_schedule(&q->tasklet);
	}
}

void qdio_inbound_processing(unsigned long data)
{
	struct qdio_q *q = (struct qdio_q *)data;
	__qdio_inbound_processing(q);
}

static int get_outbound_buffer_frontier(struct qdio_q *q)
{
	int count, stop;
	unsigned char state;

	if (((queue_type(q) != QDIO_IQDIO_QFMT) && !pci_out_supported(q)) ||
	    (queue_type(q) == QDIO_IQDIO_QFMT && multicast_outbound(q)))
		qdio_siga_sync_q(q);
	unsigned char state = 0;

	q->timestamp = get_tod_clock_fast();

	if (need_siga_sync(q))
		if (((queue_type(q) != QDIO_IQDIO_QFMT) &&
		    !pci_out_supported(q)) ||
		    (queue_type(q) == QDIO_IQDIO_QFMT &&
		    multicast_outbound(q)))
			qdio_siga_sync_q(q);

	/*
	 * Don't check 128 buffers, as otherwise qdio_inbound_q_moved
	 * would return 0.
	 */
	count = min(atomic_read(&q->nr_buf_used), QDIO_MAX_BUFFERS_MASK);
	stop = add_buf(q->first_to_check, count);

	/* need to set count to 1 for non-qebsm */
	if (!is_qebsm(q))
		count = 1;

check_next:
	if (q->first_to_check == stop)
		return q->first_to_check;

	count = get_buf_states(q, q->first_to_check, &state, count);
	if (!count)
		return q->first_to_check;
	if (q->first_to_check == stop)
		goto out;

	count = get_buf_states(q, q->first_to_check, &state, count, 0, 1);
	if (!count)
		goto out;

	switch (state) {
	case SLSB_P_OUTPUT_EMPTY:
	case SLSB_P_OUTPUT_PENDING:
		/* the adapter got it */
		QDIO_DBF_TEXT5(0, trace, "outpempt");

		atomic_sub(count, &q->nr_buf_used);
		q->first_to_check = add_buf(q->first_to_check, count);
		/*
		 * We fetch all buffer states at once. get_buf_states may
		 * return count < stop. For QEBSM we do not loop.
		 */
		if (is_qebsm(q))
			break;
		goto check_next;
	case SLSB_P_OUTPUT_ERROR:
		announce_buffer_error(q);
		/* process the buffer, the upper layer will take care of it */
		q->first_to_check = add_buf(q->first_to_check, count);
		atomic_sub(count, &q->nr_buf_used);
		break;
	case SLSB_CU_OUTPUT_PRIMED:
		/* the adapter has not fetched the output yet */
		QDIO_DBF_TEXT5(0, trace, "outpprim");
		DBF_DEV_EVENT(DBF_INFO, q->irq_ptr,
			"out empty:%1d %02x", q->nr, count);

		atomic_sub(count, &q->nr_buf_used);
		q->first_to_check = add_buf(q->first_to_check, count);
		if (q->irq_ptr->perf_stat_enabled)
			account_sbals(q, count);

		break;
	case SLSB_P_OUTPUT_ERROR:
		process_buffer_error(q, count);
		q->first_to_check = add_buf(q->first_to_check, count);
		atomic_sub(count, &q->nr_buf_used);
		if (q->irq_ptr->perf_stat_enabled)
			account_sbals_error(q, count);
		break;
	case SLSB_CU_OUTPUT_PRIMED:
		/* the adapter has not fetched the output yet */
		if (q->irq_ptr->perf_stat_enabled)
			q->q_stats.nr_sbal_nop++;
		DBF_DEV_EVENT(DBF_INFO, q->irq_ptr, "out primed:%1d",
			      q->nr);
		break;
	case SLSB_P_OUTPUT_NOT_INIT:
	case SLSB_P_OUTPUT_HALTED:
		break;
	default:
		BUG();
	}
		WARN_ON_ONCE(1);
	}

out:
	return q->first_to_check;
}

/* all buffers processed? */
static inline int qdio_outbound_q_done(struct qdio_q *q)
{
	return atomic_read(&q->nr_buf_used) == 0;
}

static inline int qdio_outbound_q_moved(struct qdio_q *q)
{
	int bufnr;

	bufnr = get_outbound_buffer_frontier(q);

	if ((bufnr != q->last_move_ftc) || q->qdio_error) {
		q->last_move_ftc = bufnr;
		QDIO_DBF_TEXT4(0, trace, "oqhasmvd");
		QDIO_DBF_HEX4(0, trace, &q, sizeof(void *));
	if (bufnr != q->last_move) {
		q->last_move = bufnr;
		DBF_DEV_EVENT(DBF_INFO, q->irq_ptr, "out moved:%1d", q->nr);
		return 1;
	} else
		return 0;
}

/*
 * VM could present us cc=2 and busy bit set on SIGA-write
 * during reconfiguration of their Guest LAN (only in iqdio mode,
 * otherwise qdio is asynchronous and cc=2 and busy bit there will take
 * the queues down immediately).
 *
 * Therefore qdio_siga_output will try for a short time constantly,
 * if such a condition occurs. If it doesn't change, it will
 * increase the busy_siga_counter and save the timestamp, and
 * schedule the queue for later processing. qdio_outbound_processing
 * will check out the counter. If non-zero, it will call qdio_kick_outbound_q
 * as often as the value of the counter. This will attempt further SIGA
 * instructions. For each successful SIGA, the counter is
 * decreased, for failing SIGAs the counter remains the same, after
 * all. After some time of no movement, qdio_kick_outbound_q will
 * finally fail and reflect corresponding error codes to call
 * the upper layer module and have it take the queues down.
 *
 * Note that this is a change from the original HiperSockets design
 * (saying cc=2 and busy bit means take the queues down), but in
 * these days Guest LAN didn't exist... excessive cc=2 with busy bit
 * conditions will still take the queues down, but the threshold is
 * higher due to the Guest LAN environment.
 *
 * Called from outbound tasklet and do_QDIO handler.
 */
static void qdio_kick_outbound_q(struct qdio_q *q)
{
	int rc;
#ifdef CONFIG_QDIO_DEBUG
	char dbf_text[15];

	QDIO_DBF_TEXT5(0, trace, "kickoutq");
	QDIO_DBF_HEX5(0, trace, &q, sizeof(void *));
#endif /* CONFIG_QDIO_DEBUG */

	if (!need_siga_out(q))
		return;

	rc = qdio_siga_output(q);
	switch (rc) {
	case 0:
		/* TODO: improve error handling for CC=0 case */
#ifdef CONFIG_QDIO_DEBUG
		if (q->u.out.timestamp) {
			QDIO_DBF_TEXT3(0, trace, "cc2reslv");
			sprintf(dbf_text, "%4x%2x%2x", q->irq_ptr->schid.sch_no,
				q->nr,
				atomic_read(&q->u.out.busy_siga_counter));
			QDIO_DBF_TEXT3(0, trace, dbf_text);
		}
#endif /* CONFIG_QDIO_DEBUG */
		/* went smooth this time, reset timestamp */
		q->u.out.timestamp = 0;
		break;
	/* cc=2 and busy bit */
	case (2 | QDIO_ERROR_SIGA_BUSY):
		atomic_inc(&q->u.out.busy_siga_counter);

		/* if the last siga was successful, save timestamp here */
		if (!q->u.out.timestamp)
			q->u.out.timestamp = get_usecs();

		/* if we're in time, don't touch qdio_error */
		if (get_usecs() - q->u.out.timestamp < QDIO_BUSY_BIT_GIVE_UP) {
			tasklet_schedule(&q->tasklet);
			break;
		}
		QDIO_DBF_TEXT2(0, trace, "cc2REPRT");
#ifdef CONFIG_QDIO_DEBUG
		sprintf(dbf_text, "%4x%2x%2x", q->irq_ptr->schid.sch_no, q->nr,
			atomic_read(&q->u.out.busy_siga_counter));
		QDIO_DBF_TEXT3(0, trace, dbf_text);
#endif /* CONFIG_QDIO_DEBUG */
	default:
		/* for plain cc=1, 2 or 3 */
		q->qdio_error = rc;
	}
}

static void qdio_kick_outbound_handler(struct qdio_q *q)
{
	int start, end, count;
#ifdef CONFIG_QDIO_DEBUG
	char dbf_text[15];
#endif

	start = q->first_to_kick;
	end = q->last_move_ftc;
	if (end >= start)
		count = end - start;
	else
		count = end + QDIO_MAX_BUFFERS_PER_Q - start;

#ifdef CONFIG_QDIO_DEBUG
	QDIO_DBF_TEXT4(0, trace, "kickouth");
	QDIO_DBF_HEX4(0, trace, &q, sizeof(void *));

	sprintf(dbf_text, "s=%2xc=%2x", start, count);
	QDIO_DBF_TEXT4(0, trace, dbf_text);
#endif /* CONFIG_QDIO_DEBUG */

	if (unlikely(q->irq_ptr->state != QDIO_IRQ_STATE_ACTIVE))
		return;

	q->handler(q->irq_ptr->cdev, q->qdio_error, q->nr, start, count,
		   q->irq_ptr->int_parm);

	/* for the next time: */
	q->first_to_kick = q->last_move_ftc;
	q->qdio_error = 0;
static int qdio_kick_outbound_q(struct qdio_q *q, unsigned long aob)
{
	int retries = 0, cc;
	unsigned int busy_bit;

	if (!need_siga_out(q))
		return 0;

	DBF_DEV_EVENT(DBF_INFO, q->irq_ptr, "siga-w:%1d", q->nr);
retry:
	qperf_inc(q, siga_write);

	cc = qdio_siga_output(q, &busy_bit, aob);
	switch (cc) {
	case 0:
		break;
	case 2:
		if (busy_bit) {
			while (++retries < QDIO_BUSY_BIT_RETRIES) {
				mdelay(QDIO_BUSY_BIT_RETRY_DELAY);
				goto retry;
			}
			DBF_ERROR("%4x cc2 BBC:%1d", SCH_NO(q), q->nr);
			cc = -EBUSY;
		} else {
			DBF_DEV_EVENT(DBF_INFO, q->irq_ptr, "siga-w cc2:%1d", q->nr);
			cc = -ENOBUFS;
		}
		break;
	case 1:
	case 3:
		DBF_ERROR("%4x SIGA-W:%1d", SCH_NO(q), cc);
		cc = -EIO;
		break;
	}
	if (retries) {
		DBF_ERROR("%4x cc2 BB2:%1d", SCH_NO(q), q->nr);
		DBF_ERROR("count:%u", retries);
	}
	return cc;
}

static void __qdio_outbound_processing(struct qdio_q *q)
{
	int siga_attempts;

	qdio_perf_stat_inc(&perf_stats.tasklet_outbound);

	/* see comment in qdio_kick_outbound_q */
	siga_attempts = atomic_read(&q->u.out.busy_siga_counter);
	while (siga_attempts--) {
		atomic_dec(&q->u.out.busy_siga_counter);
		qdio_kick_outbound_q(q);
	}

	BUG_ON(atomic_read(&q->nr_buf_used) < 0);

	if (qdio_outbound_q_moved(q))
		qdio_kick_outbound_handler(q);

	if (queue_type(q) == QDIO_ZFCP_QFMT) {
		if (!pci_out_supported(q) && !qdio_outbound_q_done(q))
			tasklet_schedule(&q->tasklet);
		return;
	}

	/* bail out for HiperSockets unicast queues */
	if (queue_type(q) == QDIO_IQDIO_QFMT && !multicast_outbound(q))
		return;
	qperf_inc(q, tasklet_outbound);
	WARN_ON_ONCE(atomic_read(&q->nr_buf_used) < 0);

	if (qdio_outbound_q_moved(q))
		qdio_kick_handler(q);

	if (queue_type(q) == QDIO_ZFCP_QFMT)
		if (!pci_out_supported(q) && !qdio_outbound_q_done(q))
			goto sched;

	if (q->u.out.pci_out_enabled)
		return;

	/*
	 * Now we know that queue type is either qeth without pci enabled
	 * or HiperSockets multicast. Make sure buffer switch from PRIMED to
	 * EMPTY is noticed and outbound_handler is called after some time.
	 */
	if (qdio_outbound_q_done(q))
		del_timer(&q->u.out.timer);
	else {
		if (!timer_pending(&q->u.out.timer)) {
			mod_timer(&q->u.out.timer, jiffies + 10 * HZ);
			qdio_perf_stat_inc(&perf_stats.debug_tl_out_timer);
		}
	}
	 * or HiperSockets. Make sure buffer switch from PRIMED to EMPTY
	 * is noticed and outbound_handler is called after some time.
	 */
	if (qdio_outbound_q_done(q))
		del_timer(&q->u.out.timer);
	else
		if (!timer_pending(&q->u.out.timer))
			mod_timer(&q->u.out.timer, jiffies + 10 * HZ);
	return;

sched:
	if (unlikely(q->irq_ptr->state == QDIO_IRQ_STATE_STOPPED))
		return;
	tasklet_schedule(&q->tasklet);
}

/* outbound tasklet */
void qdio_outbound_processing(unsigned long data)
{
	struct qdio_q *q = (struct qdio_q *)data;
	__qdio_outbound_processing(q);
}

void qdio_outbound_timer(unsigned long data)
{
	struct qdio_q *q = (struct qdio_q *)data;
	tasklet_schedule(&q->tasklet);
}

/* called from thinint inbound tasklet */
void qdio_check_outbound_after_thinint(struct qdio_q *q)

	if (unlikely(q->irq_ptr->state == QDIO_IRQ_STATE_STOPPED))
		return;
	tasklet_schedule(&q->tasklet);
}

static inline void qdio_check_outbound_after_thinint(struct qdio_q *q)
{
	struct qdio_q *out;
	int i;

	if (!pci_out_supported(q))
		return;

	for_each_output_queue(q->irq_ptr, out, i)
		if (!qdio_outbound_q_done(out))
			tasklet_schedule(&out->tasklet);
}

static inline void qdio_set_state(struct qdio_irq *irq_ptr,
				  enum qdio_irq_states state)
{
#ifdef CONFIG_QDIO_DEBUG
	char dbf_text[15];

	QDIO_DBF_TEXT5(0, trace, "newstate");
	sprintf(dbf_text, "%4x%4x", irq_ptr->schid.sch_no, state);
	QDIO_DBF_TEXT5(0, trace, dbf_text);
#endif /* CONFIG_QDIO_DEBUG */
static void __tiqdio_inbound_processing(struct qdio_q *q)
{
	qperf_inc(q, tasklet_inbound);
	if (need_siga_sync(q) && need_siga_sync_after_ai(q))
		qdio_sync_queues(q);

	/*
	 * The interrupt could be caused by a PCI request. Check the
	 * PCI capable outbound queues.
	 */
	qdio_check_outbound_after_thinint(q);

	if (!qdio_inbound_q_moved(q))
		return;

	qdio_kick_handler(q);

	if (!qdio_inbound_q_done(q)) {
		qperf_inc(q, tasklet_inbound_resched);
		if (likely(q->irq_ptr->state != QDIO_IRQ_STATE_STOPPED)) {
			tasklet_schedule(&q->tasklet);
			return;
		}
	}

	qdio_stop_polling(q);
	/*
	 * We need to check again to not lose initiative after
	 * resetting the ACK state.
	 */
	if (!qdio_inbound_q_done(q)) {
		qperf_inc(q, tasklet_inbound_resched2);
		if (likely(q->irq_ptr->state != QDIO_IRQ_STATE_STOPPED))
			tasklet_schedule(&q->tasklet);
	}
}

void tiqdio_inbound_processing(unsigned long data)
{
	struct qdio_q *q = (struct qdio_q *)data;
	__tiqdio_inbound_processing(q);
}

static inline void qdio_set_state(struct qdio_irq *irq_ptr,
				  enum qdio_irq_states state)
{
	DBF_DEV_EVENT(DBF_INFO, irq_ptr, "newstate: %1d", state);

	irq_ptr->state = state;
	mb();
}

static void qdio_irq_check_sense(struct subchannel_id schid, struct irb *irb)
{
	char dbf_text[15];

	if (irb->esw.esw0.erw.cons) {
		sprintf(dbf_text, "sens%4x", schid.sch_no);
		QDIO_DBF_TEXT2(1, trace, dbf_text);
		QDIO_DBF_HEX0(0, trace, irb, 64);
		QDIO_DBF_HEX0(0, trace, irb->ecw, 64);
static void qdio_irq_check_sense(struct qdio_irq *irq_ptr, struct irb *irb)
{
	if (irb->esw.esw0.erw.cons) {
		DBF_ERROR("%4x sense:", irq_ptr->schid.sch_no);
		DBF_ERROR_HEX(irb, 64);
		DBF_ERROR_HEX(irb->ecw, 64);
	}
}

/* PCI interrupt handler */
static void qdio_int_handler_pci(struct qdio_irq *irq_ptr)
{
	int i;
	struct qdio_q *q;

	qdio_perf_stat_inc(&perf_stats.pci_int);

	for_each_input_queue(irq_ptr, q, i)
		tasklet_schedule(&q->tasklet);
	if (unlikely(irq_ptr->state == QDIO_IRQ_STATE_STOPPED))
		return;

	for_each_input_queue(irq_ptr, q, i) {
		if (q->u.in.queue_start_poll) {
			/* skip if polling is enabled or already in work */
			if (test_and_set_bit(QDIO_QUEUE_IRQS_DISABLED,
				     &q->u.in.queue_irq_state)) {
				qperf_inc(q, int_discarded);
				continue;
			}
			q->u.in.queue_start_poll(q->irq_ptr->cdev, q->nr,
						 q->irq_ptr->int_parm);
		} else {
			tasklet_schedule(&q->tasklet);
		}
	}

	if (!(irq_ptr->qib.ac & QIB_AC_OUTBOUND_PCI_SUPPORTED))
		return;

	for_each_output_queue(irq_ptr, q, i) {
		if (qdio_outbound_q_done(q))
			continue;

		if (!siga_syncs_out_pci(q))
			qdio_siga_sync_q(q);

		if (need_siga_sync(q) && need_siga_sync_out_after_pci(q))
			qdio_siga_sync_q(q);
		tasklet_schedule(&q->tasklet);
	}
}

static void qdio_handle_activate_check(struct ccw_device *cdev,
				unsigned long intparm, int cstat, int dstat)
{
	struct qdio_irq *irq_ptr = cdev->private->qdio_data;
	struct qdio_q *q;
	char dbf_text[15];

	QDIO_DBF_TEXT2(1, trace, "ick2");
	sprintf(dbf_text, "%s", cdev->dev.bus_id);
	QDIO_DBF_TEXT2(1, trace, dbf_text);
	QDIO_DBF_HEX2(0, trace, &intparm, sizeof(int));
	QDIO_DBF_HEX2(0, trace, &dstat, sizeof(int));
	QDIO_DBF_HEX2(0, trace, &cstat, sizeof(int));
	int count;

	DBF_ERROR("%4x ACT CHECK", irq_ptr->schid.sch_no);
	DBF_ERROR("intp :%lx", intparm);
	DBF_ERROR("ds: %2x cs:%2x", dstat, cstat);

	if (irq_ptr->nr_input_qs) {
		q = irq_ptr->input_qs[0];
	} else if (irq_ptr->nr_output_qs) {
		q = irq_ptr->output_qs[0];
	} else {
		dump_stack();
		goto no_handler;
	}
	q->handler(q->irq_ptr->cdev, QDIO_ERROR_ACTIVATE_CHECK_CONDITION,
		   0, -1, -1, irq_ptr->int_parm);
no_handler:
	qdio_set_state(irq_ptr, QDIO_IRQ_STATE_STOPPED);
}

static void qdio_call_shutdown(struct work_struct *work)
{
	struct ccw_device_private *priv;
	struct ccw_device *cdev;

	priv = container_of(work, struct ccw_device_private, kick_work);
	cdev = priv->cdev;
	qdio_shutdown(cdev, QDIO_FLAG_CLEANUP_USING_CLEAR);
	put_device(&cdev->dev);
}

static void qdio_int_error(struct ccw_device *cdev)
{
	struct qdio_irq *irq_ptr = cdev->private->qdio_data;

	switch (irq_ptr->state) {
	case QDIO_IRQ_STATE_INACTIVE:
	case QDIO_IRQ_STATE_CLEANUP:
		qdio_set_state(irq_ptr, QDIO_IRQ_STATE_ERR);
		break;
	case QDIO_IRQ_STATE_ESTABLISHED:
	case QDIO_IRQ_STATE_ACTIVE:
		qdio_set_state(irq_ptr, QDIO_IRQ_STATE_STOPPED);
		if (get_device(&cdev->dev)) {
			/* Can't call shutdown from interrupt context. */
			PREPARE_WORK(&cdev->private->kick_work,
				     qdio_call_shutdown);
			queue_work(ccw_device_work, &cdev->private->kick_work);
		}
		break;
	default:
		WARN_ON(1);
	}
	wake_up(&cdev->private->wait_q);
}

static int qdio_establish_check_errors(struct ccw_device *cdev, int cstat,
					   int dstat)
{
	struct qdio_irq *irq_ptr = cdev->private->qdio_data;

	if (cstat || (dstat & ~(DEV_STAT_CHN_END | DEV_STAT_DEV_END))) {
		QDIO_DBF_TEXT2(1, setup, "eq:ckcon");
		goto error;
	}

	if (!(dstat & DEV_STAT_DEV_END)) {
		QDIO_DBF_TEXT2(1, setup, "eq:no de");
		goto error;
	}

	if (dstat & ~(DEV_STAT_CHN_END | DEV_STAT_DEV_END)) {
		QDIO_DBF_TEXT2(1, setup, "eq:badio");
		goto error;
	}
	return 0;
error:
	QDIO_DBF_HEX2(0, trace, &cstat, sizeof(int));
	QDIO_DBF_HEX2(0, trace, &dstat, sizeof(int));
	qdio_set_state(irq_ptr, QDIO_IRQ_STATE_ERR);
	return 1;

	count = sub_buf(q->first_to_check, q->first_to_kick);
	q->handler(q->irq_ptr->cdev, QDIO_ERROR_ACTIVATE,
		   q->nr, q->first_to_kick, count, irq_ptr->int_parm);
no_handler:
	qdio_set_state(irq_ptr, QDIO_IRQ_STATE_STOPPED);
	/*
	 * In case of z/VM LGR (Live Guest Migration) QDIO recovery will happen.
	 * Therefore we call the LGR detection function here.
	 */
	lgr_info_log();
}

static void qdio_establish_handle_irq(struct ccw_device *cdev, int cstat,
				      int dstat)
{
	struct qdio_irq *irq_ptr = cdev->private->qdio_data;
	char dbf_text[15];

	sprintf(dbf_text, "qehi%4x", cdev->private->schid.sch_no);
	QDIO_DBF_TEXT0(0, setup, dbf_text);
	QDIO_DBF_TEXT0(0, trace, dbf_text);

	if (!qdio_establish_check_errors(cdev, cstat, dstat))
		qdio_set_state(irq_ptr, QDIO_IRQ_STATE_ESTABLISHED);

	DBF_DEV_EVENT(DBF_INFO, irq_ptr, "qest irq");

	if (cstat)
		goto error;
	if (dstat & ~(DEV_STAT_DEV_END | DEV_STAT_CHN_END))
		goto error;
	if (!(dstat & DEV_STAT_DEV_END))
		goto error;
	qdio_set_state(irq_ptr, QDIO_IRQ_STATE_ESTABLISHED);
	return;

error:
	DBF_ERROR("%4x EQ:error", irq_ptr->schid.sch_no);
	DBF_ERROR("ds: %2x cs:%2x", dstat, cstat);
	qdio_set_state(irq_ptr, QDIO_IRQ_STATE_ERR);
}

/* qdio interrupt handler */
void qdio_int_handler(struct ccw_device *cdev, unsigned long intparm,
		      struct irb *irb)
{
	struct qdio_irq *irq_ptr = cdev->private->qdio_data;
	int cstat, dstat;
	char dbf_text[15];

	qdio_perf_stat_inc(&perf_stats.qdio_int);

	if (!intparm || !irq_ptr) {
		sprintf(dbf_text, "qihd%4x", cdev->private->schid.sch_no);
		QDIO_DBF_TEXT2(1, setup, dbf_text);
		return;
	}

	if (IS_ERR(irb)) {
		switch (PTR_ERR(irb)) {
		case -EIO:
			sprintf(dbf_text, "ierr%4x", irq_ptr->schid.sch_no);
			QDIO_DBF_TEXT2(1, setup, dbf_text);
			qdio_int_error(cdev);
			return;
		case -ETIMEDOUT:
			sprintf(dbf_text, "qtoh%4x", irq_ptr->schid.sch_no);
			QDIO_DBF_TEXT2(1, setup, dbf_text);
			qdio_int_error(cdev);
			return;
		default:
			WARN_ON(1);
			return;
		}
	}
	qdio_irq_check_sense(irq_ptr->schid, irb);


	if (!intparm || !irq_ptr) {
		DBF_ERROR("qint:%4x", cdev->private->schid.sch_no);
		return;
	}

	if (irq_ptr->perf_stat_enabled)
		irq_ptr->perf_stat.qdio_int++;

	if (IS_ERR(irb)) {
		DBF_ERROR("%4x IO error", irq_ptr->schid.sch_no);
		qdio_set_state(irq_ptr, QDIO_IRQ_STATE_ERR);
		wake_up(&cdev->private->wait_q);
		return;
	}
	qdio_irq_check_sense(irq_ptr, irb);
	cstat = irb->scsw.cmd.cstat;
	dstat = irb->scsw.cmd.dstat;

	switch (irq_ptr->state) {
	case QDIO_IRQ_STATE_INACTIVE:
		qdio_establish_handle_irq(cdev, cstat, dstat);
		break;

	case QDIO_IRQ_STATE_CLEANUP:
		qdio_set_state(irq_ptr, QDIO_IRQ_STATE_INACTIVE);
		break;

	case QDIO_IRQ_STATE_CLEANUP:
		qdio_set_state(irq_ptr, QDIO_IRQ_STATE_INACTIVE);
		break;
	case QDIO_IRQ_STATE_ESTABLISHED:
	case QDIO_IRQ_STATE_ACTIVE:
		if (cstat & SCHN_STAT_PCI) {
			qdio_int_handler_pci(irq_ptr);
			/* no state change so no need to wake up wait_q */
			return;
		}
		if ((cstat & ~SCHN_STAT_PCI) || dstat) {
			qdio_handle_activate_check(cdev, intparm, cstat,
						   dstat);
			break;
		}
	default:
		WARN_ON(1);
			return;
		}
		if (cstat || dstat)
			qdio_handle_activate_check(cdev, intparm, cstat,
						   dstat);
		break;
	case QDIO_IRQ_STATE_STOPPED:
		break;
	default:
		WARN_ON_ONCE(1);
	}
	wake_up(&cdev->private->wait_q);
}

/**
 * qdio_get_ssqd_desc - get qdio subchannel description
 * @cdev: ccw device to get description for
 *
 * Returns a pointer to the saved qdio subchannel description,
 * or NULL for not setup qdio devices.
 */
struct qdio_ssqd_desc *qdio_get_ssqd_desc(struct ccw_device *cdev)
{
	struct qdio_irq *irq_ptr;
	char dbf_text[15];

	sprintf(dbf_text, "qssq%4x", cdev->private->schid.sch_no);
	QDIO_DBF_TEXT0(0, setup, dbf_text);

	irq_ptr = cdev->private->qdio_data;
	if (!irq_ptr)
		return NULL;

	return &irq_ptr->ssqd_desc;
}
EXPORT_SYMBOL_GPL(qdio_get_ssqd_desc);

/**
 * qdio_cleanup - shutdown queues and free data structures
 * @cdev: associated ccw device
 * @how: use halt or clear to shutdown
 *
 * This function calls qdio_shutdown() for @cdev with method @how
 * and on success qdio_free() for @cdev.
 */
int qdio_cleanup(struct ccw_device *cdev, int how)
{
	struct qdio_irq *irq_ptr;
	char dbf_text[15];
	int rc;

	sprintf(dbf_text, "qcln%4x", cdev->private->schid.sch_no);
	QDIO_DBF_TEXT0(0, setup, dbf_text);

	irq_ptr = cdev->private->qdio_data;
	if (!irq_ptr)
		return -ENODEV;

	rc = qdio_shutdown(cdev, how);
	if (rc == 0)
		rc = qdio_free(cdev);
	return rc;
}
EXPORT_SYMBOL_GPL(qdio_cleanup);

 * @data: where to store the ssqd
 *
 * Returns 0 or an error code. The results of the chsc are stored in the
 * specified structure.
 */
int qdio_get_ssqd_desc(struct ccw_device *cdev,
		       struct qdio_ssqd_desc *data)
{

	if (!cdev || !cdev->private)
		return -EINVAL;

	DBF_EVENT("get ssqd:%4x", cdev->private->schid.sch_no);
	return qdio_setup_get_ssqd(NULL, &cdev->private->schid, data);
}
EXPORT_SYMBOL_GPL(qdio_get_ssqd_desc);

static void qdio_shutdown_queues(struct ccw_device *cdev)
{
	struct qdio_irq *irq_ptr = cdev->private->qdio_data;
	struct qdio_q *q;
	int i;

	for_each_input_queue(irq_ptr, q, i)
		tasklet_disable(&q->tasklet);

	for_each_output_queue(irq_ptr, q, i) {
		tasklet_disable(&q->tasklet);
		del_timer(&q->u.out.timer);
		tasklet_kill(&q->tasklet);

	for_each_output_queue(irq_ptr, q, i) {
		del_timer(&q->u.out.timer);
		tasklet_kill(&q->tasklet);
	}
}

/**
 * qdio_shutdown - shut down a qdio subchannel
 * @cdev: associated ccw device
 * @how: use halt or clear to shutdown
 */
int qdio_shutdown(struct ccw_device *cdev, int how)
{
	struct qdio_irq *irq_ptr;
	int rc;
	unsigned long flags;
	char dbf_text[15];

	sprintf(dbf_text, "qshu%4x", cdev->private->schid.sch_no);
	QDIO_DBF_TEXT0(0, setup, dbf_text);

	irq_ptr = cdev->private->qdio_data;
	if (!irq_ptr)
		return -ENODEV;

	struct qdio_irq *irq_ptr = cdev->private->qdio_data;
	int rc;
	unsigned long flags;

	if (!irq_ptr)
		return -ENODEV;

	WARN_ON_ONCE(irqs_disabled());
	DBF_EVENT("qshutdown:%4x", cdev->private->schid.sch_no);

	mutex_lock(&irq_ptr->setup_mutex);
	/*
	 * Subchannel was already shot down. We cannot prevent being called
	 * twice since cio may trigger a shutdown asynchronously.
	 */
	if (irq_ptr->state == QDIO_IRQ_STATE_INACTIVE) {
		mutex_unlock(&irq_ptr->setup_mutex);
		return 0;
	}

	tiqdio_remove_input_queues(irq_ptr);
	qdio_shutdown_queues(cdev);
	qdio_shutdown_debug_entries(irq_ptr, cdev);
	/*
	 * Indicate that the device is going down. Scheduling the queue
	 * tasklets is forbidden from here on.
	 */
	qdio_set_state(irq_ptr, QDIO_IRQ_STATE_STOPPED);

	tiqdio_remove_input_queues(irq_ptr);
	qdio_shutdown_queues(cdev);
	qdio_shutdown_debug_entries(irq_ptr);

	/* cleanup subchannel */
	spin_lock_irqsave(get_ccwdev_lock(cdev), flags);

	if (how & QDIO_FLAG_CLEANUP_USING_CLEAR)
		rc = ccw_device_clear(cdev, QDIO_DOING_CLEANUP);
	else
		/* default behaviour is halt */
		rc = ccw_device_halt(cdev, QDIO_DOING_CLEANUP);
	if (rc) {
		sprintf(dbf_text, "sher%4x", irq_ptr->schid.sch_no);
		QDIO_DBF_TEXT0(0, setup, dbf_text);
		sprintf(dbf_text, "rc=%d", rc);
		QDIO_DBF_TEXT0(0, setup, dbf_text);
		DBF_ERROR("%4x SHUTD ERR", irq_ptr->schid.sch_no);
		DBF_ERROR("rc:%4d", rc);
		goto no_cleanup;
	}

	qdio_set_state(irq_ptr, QDIO_IRQ_STATE_CLEANUP);
	spin_unlock_irqrestore(get_ccwdev_lock(cdev), flags);
	wait_event_interruptible_timeout(cdev->private->wait_q,
		irq_ptr->state == QDIO_IRQ_STATE_INACTIVE ||
		irq_ptr->state == QDIO_IRQ_STATE_ERR,
		10 * HZ);
	spin_lock_irqsave(get_ccwdev_lock(cdev), flags);

no_cleanup:
	qdio_shutdown_thinint(irq_ptr);

	/* restore interrupt handler */
	if ((void *)cdev->handler == (void *)qdio_int_handler)
		cdev->handler = irq_ptr->orig_handler;
	spin_unlock_irqrestore(get_ccwdev_lock(cdev), flags);

	qdio_set_state(irq_ptr, QDIO_IRQ_STATE_INACTIVE);
	mutex_unlock(&irq_ptr->setup_mutex);
	if (rc)
		return rc;
	return 0;
}
EXPORT_SYMBOL_GPL(qdio_shutdown);

/**
 * qdio_free - free data structures for a qdio subchannel
 * @cdev: associated ccw device
 */
int qdio_free(struct ccw_device *cdev)
{
	struct qdio_irq *irq_ptr;
	char dbf_text[15];

	sprintf(dbf_text, "qfre%4x", cdev->private->schid.sch_no);
	QDIO_DBF_TEXT0(0, setup, dbf_text);

	irq_ptr = cdev->private->qdio_data;
	if (!irq_ptr)
		return -ENODEV;

	mutex_lock(&irq_ptr->setup_mutex);
	struct qdio_irq *irq_ptr = cdev->private->qdio_data;

	if (!irq_ptr)
		return -ENODEV;

	DBF_EVENT("qfree:%4x", cdev->private->schid.sch_no);
	DBF_DEV_EVENT(DBF_ERR, irq_ptr, "dbf abandoned");
	mutex_lock(&irq_ptr->setup_mutex);

	irq_ptr->debug_area = NULL;
	cdev->private->qdio_data = NULL;
	mutex_unlock(&irq_ptr->setup_mutex);

	qdio_release_memory(irq_ptr);
	return 0;
}
EXPORT_SYMBOL_GPL(qdio_free);

/**
 * qdio_initialize - allocate and establish queues for a qdio subchannel
 * @init_data: initialization data
 *
 * This function first allocates queues via qdio_allocate() and on success
 * establishes them via qdio_establish().
 */
int qdio_initialize(struct qdio_initialize *init_data)
{
	int rc;
	char dbf_text[15];

	sprintf(dbf_text, "qini%4x", init_data->cdev->private->schid.sch_no);
	QDIO_DBF_TEXT0(0, setup, dbf_text);

	rc = qdio_allocate(init_data);
	if (rc)
		return rc;

	rc = qdio_establish(init_data);
	if (rc)
		qdio_free(init_data->cdev);
	return rc;
}
EXPORT_SYMBOL_GPL(qdio_initialize);

/**
 * qdio_allocate - allocate qdio queues and associated data
 * @init_data: initialization data
 */
int qdio_allocate(struct qdio_initialize *init_data)
{
	struct qdio_irq *irq_ptr;
	char dbf_text[15];

	sprintf(dbf_text, "qalc%4x", init_data->cdev->private->schid.sch_no);
	QDIO_DBF_TEXT0(0, setup, dbf_text);

	DBF_EVENT("qallocate:%4x", init_data->cdev->private->schid.sch_no);

	if ((init_data->no_input_qs && !init_data->input_handler) ||
	    (init_data->no_output_qs && !init_data->output_handler))
		return -EINVAL;

	if ((init_data->no_input_qs > QDIO_MAX_QUEUES_PER_IRQ) ||
	    (init_data->no_output_qs > QDIO_MAX_QUEUES_PER_IRQ))
		return -EINVAL;

	if ((!init_data->input_sbal_addr_array) ||
	    (!init_data->output_sbal_addr_array))
		return -EINVAL;

	qdio_allocate_do_dbf(init_data);

	/* irq_ptr must be in GFP_DMA since it contains ccw1.cda */
	irq_ptr = (void *) get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!irq_ptr)
		goto out_err;
	QDIO_DBF_TEXT0(0, setup, "irq_ptr:");
	QDIO_DBF_HEX0(0, setup, &irq_ptr, sizeof(void *));

	mutex_init(&irq_ptr->setup_mutex);

	mutex_init(&irq_ptr->setup_mutex);
	if (qdio_allocate_dbf(init_data, irq_ptr))
		goto out_rel;

	/*
	 * Allocate a page for the chsc calls in qdio_establish.
	 * Must be pre-allocated since a zfcp recovery will call
	 * qdio_establish. In case of low memory and swap on a zfcp disk
	 * we may not be able to allocate memory otherwise.
	 */
	irq_ptr->chsc_page = get_zeroed_page(GFP_KERNEL);
	if (!irq_ptr->chsc_page)
		goto out_rel;

	/* qdr is used in ccw1.cda which is u32 */
	irq_ptr->qdr = (struct qdr *) get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!irq_ptr->qdr)
		goto out_rel;
	WARN_ON((unsigned long)irq_ptr->qdr & 0xfff);

	QDIO_DBF_TEXT0(0, setup, "qdr:");
	QDIO_DBF_HEX0(0, setup, &irq_ptr->qdr, sizeof(void *));

	if (qdio_allocate_qs(irq_ptr, init_data->no_input_qs,
			     init_data->no_output_qs))
		goto out_rel;

	init_data->cdev->private->qdio_data = irq_ptr;
	qdio_set_state(irq_ptr, QDIO_IRQ_STATE_INACTIVE);
	return 0;
out_rel:
	qdio_release_memory(irq_ptr);
out_err:
	return -ENOMEM;
}
EXPORT_SYMBOL_GPL(qdio_allocate);

static void qdio_detect_hsicq(struct qdio_irq *irq_ptr)
{
	struct qdio_q *q = irq_ptr->input_qs[0];
	int i, use_cq = 0;

	if (irq_ptr->nr_input_qs > 1 && queue_type(q) == QDIO_IQDIO_QFMT)
		use_cq = 1;

	for_each_output_queue(irq_ptr, q, i) {
		if (use_cq) {
			if (qdio_enable_async_operation(&q->u.out) < 0) {
				use_cq = 0;
				continue;
			}
		} else
			qdio_disable_async_operation(&q->u.out);
	}
	DBF_EVENT("use_cq:%d", use_cq);
}

/**
 * qdio_establish - establish queues on a qdio subchannel
 * @init_data: initialization data
 */
int qdio_establish(struct qdio_initialize *init_data)
{
	char dbf_text[20];
	struct qdio_irq *irq_ptr;
	struct ccw_device *cdev = init_data->cdev;
	unsigned long saveflags;
	int rc;

	sprintf(dbf_text, "qest%4x", cdev->private->schid.sch_no);
	QDIO_DBF_TEXT0(0, setup, dbf_text);
	DBF_EVENT("qestablish:%4x", cdev->private->schid.sch_no);

	irq_ptr = cdev->private->qdio_data;
	if (!irq_ptr)
		return -ENODEV;

	if (cdev->private->state != DEV_STATE_ONLINE)
		return -EINVAL;

	mutex_lock(&irq_ptr->setup_mutex);
	qdio_setup_irq(init_data);

	rc = qdio_establish_thinint(irq_ptr);
	if (rc) {
		mutex_unlock(&irq_ptr->setup_mutex);
		qdio_shutdown(cdev, QDIO_FLAG_CLEANUP_USING_CLEAR);
		return rc;
	}

	/* establish q */
	irq_ptr->ccw.cmd_code = irq_ptr->equeue.cmd;
	irq_ptr->ccw.flags = CCW_FLAG_SLI;
	irq_ptr->ccw.count = irq_ptr->equeue.count;
	irq_ptr->ccw.cda = (u32)((addr_t)irq_ptr->qdr);

	spin_lock_irqsave(get_ccwdev_lock(cdev), saveflags);
	ccw_device_set_options_mask(cdev, 0);

	rc = ccw_device_start(cdev, &irq_ptr->ccw, QDIO_DOING_ESTABLISH, 0, 0);
	if (rc) {
		sprintf(dbf_text, "eq:io%4x", irq_ptr->schid.sch_no);
		QDIO_DBF_TEXT2(1, setup, dbf_text);
		sprintf(dbf_text, "eq:rc%4x", rc);
		QDIO_DBF_TEXT2(1, setup, dbf_text);
		DBF_ERROR("%4x est IO ERR", irq_ptr->schid.sch_no);
		DBF_ERROR("rc:%4x", rc);
	}
	spin_unlock_irqrestore(get_ccwdev_lock(cdev), saveflags);

	if (rc) {
		mutex_unlock(&irq_ptr->setup_mutex);
		qdio_shutdown(cdev, QDIO_FLAG_CLEANUP_USING_CLEAR);
		return rc;
	}

	wait_event_interruptible_timeout(cdev->private->wait_q,
		irq_ptr->state == QDIO_IRQ_STATE_ESTABLISHED ||
		irq_ptr->state == QDIO_IRQ_STATE_ERR, HZ);

	if (irq_ptr->state != QDIO_IRQ_STATE_ESTABLISHED) {
		mutex_unlock(&irq_ptr->setup_mutex);
		qdio_shutdown(cdev, QDIO_FLAG_CLEANUP_USING_CLEAR);
		return -EIO;
	}

	qdio_setup_ssqd_info(irq_ptr);
	sprintf(dbf_text, "qib ac%2x", irq_ptr->qib.ac);
	QDIO_DBF_TEXT2(0, setup, dbf_text);

	qdio_detect_hsicq(irq_ptr);

	/* qebsm is now setup if available, initialize buffer states */
	qdio_init_buf_states(irq_ptr);

	mutex_unlock(&irq_ptr->setup_mutex);
	qdio_print_subchannel_info(irq_ptr, cdev);
	qdio_setup_debug_entries(irq_ptr, cdev);
	return 0;
}
EXPORT_SYMBOL_GPL(qdio_establish);

/**
 * qdio_activate - activate queues on a qdio subchannel
 * @cdev: associated cdev
 */
int qdio_activate(struct ccw_device *cdev)
{
	struct qdio_irq *irq_ptr;
	int rc;
	unsigned long saveflags;
	char dbf_text[20];

	sprintf(dbf_text, "qact%4x", cdev->private->schid.sch_no);
	QDIO_DBF_TEXT0(0, setup, dbf_text);

	DBF_EVENT("qactivate:%4x", cdev->private->schid.sch_no);

	irq_ptr = cdev->private->qdio_data;
	if (!irq_ptr)
		return -ENODEV;

	if (cdev->private->state != DEV_STATE_ONLINE)
		return -EINVAL;

	mutex_lock(&irq_ptr->setup_mutex);
	if (irq_ptr->state == QDIO_IRQ_STATE_INACTIVE) {
		rc = -EBUSY;
		goto out;
	}

	irq_ptr->ccw.cmd_code = irq_ptr->aqueue.cmd;
	irq_ptr->ccw.flags = CCW_FLAG_SLI;
	irq_ptr->ccw.count = irq_ptr->aqueue.count;
	irq_ptr->ccw.cda = 0;

	spin_lock_irqsave(get_ccwdev_lock(cdev), saveflags);
	ccw_device_set_options(cdev, CCWDEV_REPORT_ALL);

	rc = ccw_device_start(cdev, &irq_ptr->ccw, QDIO_DOING_ACTIVATE,
			      0, DOIO_DENY_PREFETCH);
	if (rc) {
		sprintf(dbf_text, "aq:io%4x", irq_ptr->schid.sch_no);
		QDIO_DBF_TEXT2(1, setup, dbf_text);
		sprintf(dbf_text, "aq:rc%4x", rc);
		QDIO_DBF_TEXT2(1, setup, dbf_text);
		DBF_ERROR("%4x act IO ERR", irq_ptr->schid.sch_no);
		DBF_ERROR("rc:%4x", rc);
	}
	spin_unlock_irqrestore(get_ccwdev_lock(cdev), saveflags);

	if (rc)
		goto out;

	if (is_thinint_irq(irq_ptr))
		tiqdio_add_input_queues(irq_ptr);

	/* wait for subchannel to become active */
	msleep(5);

	switch (irq_ptr->state) {
	case QDIO_IRQ_STATE_STOPPED:
	case QDIO_IRQ_STATE_ERR:
		mutex_unlock(&irq_ptr->setup_mutex);
		qdio_shutdown(cdev, QDIO_FLAG_CLEANUP_USING_CLEAR);
		return -EIO;
		rc = -EIO;
		break;
	default:
		qdio_set_state(irq_ptr, QDIO_IRQ_STATE_ACTIVE);
		rc = 0;
	}
out:
	mutex_unlock(&irq_ptr->setup_mutex);
	return rc;
}
EXPORT_SYMBOL_GPL(qdio_activate);

static inline int buf_in_between(int bufnr, int start, int count)
{
	int end = add_buf(start, count);

	if (end > start) {
		if (bufnr >= start && bufnr < end)
			return 1;
		else
			return 0;
	}

	/* wrap-around case */
	if ((bufnr >= start && bufnr <= QDIO_MAX_BUFFERS_PER_Q) ||
	    (bufnr < end))
		return 1;
	else
		return 0;
}

/**
 * handle_inbound - reset processed input buffers
 * @q: queue containing the buffers
 * @callflags: flags
 * @bufnr: first buffer to process
 * @count: how many buffers are emptied
 */
static void handle_inbound(struct qdio_q *q, unsigned int callflags,
			   int bufnr, int count)
{
	unsigned long flags;
	int used, rc;

	/*
	 * do_QDIO could run in parallel with the queue tasklet so the
	 * upper-layer programm could empty the ACK'ed buffer here.
	 * If that happens we must clear the polling flag, otherwise
	 * qdio_stop_polling() could set the buffer to NOT_INIT after
	 * it was set to EMPTY which would kill us.
	 */
	spin_lock_irqsave(&q->u.in.lock, flags);
	if (q->u.in.polling)
		if (buf_in_between(q->last_move_ftc, bufnr, count))
			q->u.in.polling = 0;

	count = set_buf_states(q, bufnr, SLSB_CU_INPUT_EMPTY, count);
	spin_unlock_irqrestore(&q->u.in.lock, flags);

	used = atomic_add_return(count, &q->nr_buf_used) - count;
	BUG_ON(used + count > QDIO_MAX_BUFFERS_PER_Q);

	/* no need to signal as long as the adapter had free buffers */
	if (used)
		return;

	if (need_siga_in(q)) {
		rc = qdio_siga_input(q);
		if (rc)
			q->qdio_error = rc;
	}
static int handle_inbound(struct qdio_q *q, unsigned int callflags,
			  int bufnr, int count)
{
	int diff;

	qperf_inc(q, inbound_call);

	if (!q->u.in.polling)
		goto set;

	/* protect against stop polling setting an ACK for an emptied slsb */
	if (count == QDIO_MAX_BUFFERS_PER_Q) {
		/* overwriting everything, just delete polling status */
		q->u.in.polling = 0;
		q->u.in.ack_count = 0;
		goto set;
	} else if (buf_in_between(q->u.in.ack_start, bufnr, count)) {
		if (is_qebsm(q)) {
			/* partial overwrite, just update ack_start */
			diff = add_buf(bufnr, count);
			diff = sub_buf(diff, q->u.in.ack_start);
			q->u.in.ack_count -= diff;
			if (q->u.in.ack_count <= 0) {
				q->u.in.polling = 0;
				q->u.in.ack_count = 0;
				goto set;
			}
			q->u.in.ack_start = add_buf(q->u.in.ack_start, diff);
		}
		else
			/* the only ACK will be deleted, so stop polling */
			q->u.in.polling = 0;
	}

set:
	count = set_buf_states(q, bufnr, SLSB_CU_INPUT_EMPTY, count);
	atomic_add(count, &q->nr_buf_used);

	if (need_siga_in(q))
		return qdio_siga_input(q);

	return 0;
}

/**
 * handle_outbound - process filled outbound buffers
 * @q: queue containing the buffers
 * @callflags: flags
 * @bufnr: first buffer to process
 * @count: how many buffers are filled
 */
static void handle_outbound(struct qdio_q *q, unsigned int callflags,
			    int bufnr, int count)
{
	unsigned char state;
	int used;

	qdio_perf_stat_inc(&perf_stats.outbound_handler);

	count = set_buf_states(q, bufnr, SLSB_CU_OUTPUT_PRIMED, count);
	used = atomic_add_return(count, &q->nr_buf_used);
	BUG_ON(used > QDIO_MAX_BUFFERS_PER_Q);

	if (callflags & QDIO_FLAG_PCI_OUT)
		q->u.out.pci_out_enabled = 1;
	else
		q->u.out.pci_out_enabled = 0;

	if (queue_type(q) == QDIO_IQDIO_QFMT) {
		if (multicast_outbound(q))
			qdio_kick_outbound_q(q);
		else
			/*
			 * One siga-w per buffer required for unicast
			 * HiperSockets.
			 */
			while (count--)
				qdio_kick_outbound_q(q);
		goto out;
	}

	if (need_siga_sync(q)) {
		qdio_siga_sync_q(q);
		goto out;
	}

	/* try to fast requeue buffers */
	get_buf_state(q, prev_buf(bufnr), &state);
	if (state != SLSB_CU_OUTPUT_PRIMED)
		qdio_kick_outbound_q(q);
	else {
		QDIO_DBF_TEXT5(0, trace, "fast-req");
		qdio_perf_stat_inc(&perf_stats.fast_requeue);
	}
out:
	/* Fixme: could wait forever if called from process context */
	tasklet_schedule(&q->tasklet);
static int handle_outbound(struct qdio_q *q, unsigned int callflags,
			   int bufnr, int count)
{
	unsigned char state = 0;
	int used, rc = 0;

	qperf_inc(q, outbound_call);

	count = set_buf_states(q, bufnr, SLSB_CU_OUTPUT_PRIMED, count);
	used = atomic_add_return(count, &q->nr_buf_used);

	if (used == QDIO_MAX_BUFFERS_PER_Q)
		qperf_inc(q, outbound_queue_full);

	if (callflags & QDIO_FLAG_PCI_OUT) {
		q->u.out.pci_out_enabled = 1;
		qperf_inc(q, pci_request_int);
	} else
		q->u.out.pci_out_enabled = 0;

	if (queue_type(q) == QDIO_IQDIO_QFMT) {
		unsigned long phys_aob = 0;

		/* One SIGA-W per buffer required for unicast HSI */
		WARN_ON_ONCE(count > 1 && !multicast_outbound(q));

		phys_aob = qdio_aob_for_buffer(&q->u.out, bufnr);

		rc = qdio_kick_outbound_q(q, phys_aob);
	} else if (need_siga_sync(q)) {
		rc = qdio_siga_sync_q(q);
	} else if (count < QDIO_MAX_BUFFERS_PER_Q &&
		   get_buf_state(q, prev_buf(bufnr), &state, 0) > 0 &&
		   state == SLSB_CU_OUTPUT_PRIMED) {
		/* The previous buffer is not processed yet, tack on. */
		qperf_inc(q, fast_requeue);
	} else {
		rc = qdio_kick_outbound_q(q, 0);
	}

	/* in case of SIGA errors we must process the error immediately */
	if (used >= q->u.out.scan_threshold || rc)
		tasklet_schedule(&q->tasklet);
	else
		/* free the SBALs in case of no further traffic */
		if (!timer_pending(&q->u.out.timer))
			mod_timer(&q->u.out.timer, jiffies + HZ);
	return rc;
}

/**
 * do_QDIO - process input or output buffers
 * @cdev: associated ccw_device for the qdio subchannel
 * @callflags: input or output and special flags from the program
 * @q_nr: queue number
 * @bufnr: buffer number
 * @count: how many buffers to process
 */
int do_QDIO(struct ccw_device *cdev, unsigned int callflags,
	    int q_nr, int bufnr, int count)
{
	struct qdio_irq *irq_ptr;
#ifdef CONFIG_QDIO_DEBUG
	char dbf_text[20];

	sprintf(dbf_text, "doQD%4x", cdev->private->schid.sch_no);
	QDIO_DBF_TEXT3(0, trace, dbf_text);
#endif /* CONFIG_QDIO_DEBUG */

	if ((bufnr > QDIO_MAX_BUFFERS_PER_Q) ||
	    (count > QDIO_MAX_BUFFERS_PER_Q) ||
	    (q_nr > QDIO_MAX_QUEUES_PER_IRQ))
		return -EINVAL;

	if (!count)
		return 0;

	    int q_nr, unsigned int bufnr, unsigned int count)
{
	struct qdio_irq *irq_ptr;

	if (bufnr >= QDIO_MAX_BUFFERS_PER_Q || count > QDIO_MAX_BUFFERS_PER_Q)
		return -EINVAL;

	irq_ptr = cdev->private->qdio_data;
	if (!irq_ptr)
		return -ENODEV;

#ifdef CONFIG_QDIO_DEBUG
	if (callflags & QDIO_FLAG_SYNC_INPUT)
		QDIO_DBF_HEX3(0, trace, &irq_ptr->input_qs[q_nr],
			      sizeof(void *));
	else
		QDIO_DBF_HEX3(0, trace, &irq_ptr->output_qs[q_nr],
			      sizeof(void *));

	sprintf(dbf_text, "flag%04x", callflags);
	QDIO_DBF_TEXT3(0, trace, dbf_text);
	sprintf(dbf_text, "qi%02xct%02x", bufnr, count);
	QDIO_DBF_TEXT3(0, trace, dbf_text);
#endif /* CONFIG_QDIO_DEBUG */

	if (irq_ptr->state != QDIO_IRQ_STATE_ACTIVE)
		return -EBUSY;

	if (callflags & QDIO_FLAG_SYNC_INPUT)
		handle_inbound(irq_ptr->input_qs[q_nr],
			       callflags, bufnr, count);
	else if (callflags & QDIO_FLAG_SYNC_OUTPUT)
		handle_outbound(irq_ptr->output_qs[q_nr],
				callflags, bufnr, count);
	else {
		QDIO_DBF_TEXT3(1, trace, "doQD:inv");
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(do_QDIO);

	DBF_DEV_EVENT(DBF_INFO, irq_ptr,
		      "do%02x b:%02x c:%02x", callflags, bufnr, count);

	if (irq_ptr->state != QDIO_IRQ_STATE_ACTIVE)
		return -EIO;
	if (!count)
		return 0;
	if (callflags & QDIO_FLAG_SYNC_INPUT)
		return handle_inbound(irq_ptr->input_qs[q_nr],
				      callflags, bufnr, count);
	else if (callflags & QDIO_FLAG_SYNC_OUTPUT)
		return handle_outbound(irq_ptr->output_qs[q_nr],
				       callflags, bufnr, count);
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(do_QDIO);

/**
 * qdio_start_irq - process input buffers
 * @cdev: associated ccw_device for the qdio subchannel
 * @nr: input queue number
 *
 * Return codes
 *   0 - success
 *   1 - irqs not started since new data is available
 */
int qdio_start_irq(struct ccw_device *cdev, int nr)
{
	struct qdio_q *q;
	struct qdio_irq *irq_ptr = cdev->private->qdio_data;

	if (!irq_ptr)
		return -ENODEV;
	q = irq_ptr->input_qs[nr];

	clear_nonshared_ind(irq_ptr);
	qdio_stop_polling(q);
	clear_bit(QDIO_QUEUE_IRQS_DISABLED, &q->u.in.queue_irq_state);

	/*
	 * We need to check again to not lose initiative after
	 * resetting the ACK state.
	 */
	if (test_nonshared_ind(irq_ptr))
		goto rescan;
	if (!qdio_inbound_q_done(q))
		goto rescan;
	return 0;

rescan:
	if (test_and_set_bit(QDIO_QUEUE_IRQS_DISABLED,
			     &q->u.in.queue_irq_state))
		return 0;
	else
		return 1;

}
EXPORT_SYMBOL(qdio_start_irq);

/**
 * qdio_get_next_buffers - process input buffers
 * @cdev: associated ccw_device for the qdio subchannel
 * @nr: input queue number
 * @bufnr: first filled buffer number
 * @error: buffers are in error state
 *
 * Return codes
 *   < 0 - error
 *   = 0 - no new buffers found
 *   > 0 - number of processed buffers
 */
int qdio_get_next_buffers(struct ccw_device *cdev, int nr, int *bufnr,
			  int *error)
{
	struct qdio_q *q;
	int start, end;
	struct qdio_irq *irq_ptr = cdev->private->qdio_data;

	if (!irq_ptr)
		return -ENODEV;
	q = irq_ptr->input_qs[nr];

	/*
	 * Cannot rely on automatic sync after interrupt since queues may
	 * also be examined without interrupt.
	 */
	if (need_siga_sync(q))
		qdio_sync_queues(q);

	/* check the PCI capable outbound queues. */
	qdio_check_outbound_after_thinint(q);

	if (!qdio_inbound_q_moved(q))
		return 0;

	/* Note: upper-layer MUST stop processing immediately here ... */
	if (unlikely(q->irq_ptr->state != QDIO_IRQ_STATE_ACTIVE))
		return -EIO;

	start = q->first_to_kick;
	end = q->first_to_check;
	*bufnr = start;
	*error = q->qdio_error;

	/* for the next time */
	q->first_to_kick = end;
	q->qdio_error = 0;
	return sub_buf(end, start);
}
EXPORT_SYMBOL(qdio_get_next_buffers);

/**
 * qdio_stop_irq - disable interrupt processing for the device
 * @cdev: associated ccw_device for the qdio subchannel
 * @nr: input queue number
 *
 * Return codes
 *   0 - interrupts were already disabled
 *   1 - interrupts successfully disabled
 */
int qdio_stop_irq(struct ccw_device *cdev, int nr)
{
	struct qdio_q *q;
	struct qdio_irq *irq_ptr = cdev->private->qdio_data;

	if (!irq_ptr)
		return -ENODEV;
	q = irq_ptr->input_qs[nr];

	if (test_and_set_bit(QDIO_QUEUE_IRQS_DISABLED,
			     &q->u.in.queue_irq_state))
		return 0;
	else
		return 1;
}
EXPORT_SYMBOL(qdio_stop_irq);

/**
 * qdio_pnso_brinfo() - perform network subchannel op #0 - bridge info.
 * @schid:		Subchannel ID.
 * @cnc:		Boolean Change-Notification Control
 * @response:		Response code will be stored at this address
 * @cb: 		Callback function will be executed for each element
 *			of the address list
 * @priv:		Pointer passed from the caller to qdio_pnso_brinfo()
 * @type:		Type of the address entry passed to the callback
 * @entry:		Entry containg the address of the specified type
 * @priv:		Pointer to pass to the callback function.
 *
 * Performs "Store-network-bridging-information list" operation and calls
 * the callback function for every entry in the list. If "change-
 * notification-control" is set, further changes in the address list
 * will be reported via the IPA command.
 */
int qdio_pnso_brinfo(struct subchannel_id schid,
		int cnc, u16 *response,
		void (*cb)(void *priv, enum qdio_brinfo_entry_type type,
				void *entry),
		void *priv)
{
	struct chsc_pnso_area *rr;
	int rc;
	u32 prev_instance = 0;
	int isfirstblock = 1;
	int i, size, elems;

	rr = (struct chsc_pnso_area *)get_zeroed_page(GFP_KERNEL);
	if (rr == NULL)
		return -ENOMEM;
	do {
		/* on the first iteration, naihdr.resume_token will be zero */
		rc = chsc_pnso_brinfo(schid, rr, rr->naihdr.resume_token, cnc);
		if (rc != 0 && rc != -EBUSY)
			goto out;
		if (rr->response.code != 1) {
			rc = -EIO;
			continue;
		} else
			rc = 0;

		if (cb == NULL)
			continue;

		size = rr->naihdr.naids;
		elems = (rr->response.length -
				sizeof(struct chsc_header) -
				sizeof(struct chsc_brinfo_naihdr)) /
				size;

		if (!isfirstblock && (rr->naihdr.instance != prev_instance)) {
			/* Inform the caller that they need to scrap */
			/* the data that was already reported via cb */
				rc = -EAGAIN;
				break;
		}
		isfirstblock = 0;
		prev_instance = rr->naihdr.instance;
		for (i = 0; i < elems; i++)
			switch (size) {
			case sizeof(struct qdio_brinfo_entry_l3_ipv6):
				(*cb)(priv, l3_ipv6_addr,
						&rr->entries.l3_ipv6[i]);
				break;
			case sizeof(struct qdio_brinfo_entry_l3_ipv4):
				(*cb)(priv, l3_ipv4_addr,
						&rr->entries.l3_ipv4[i]);
				break;
			case sizeof(struct qdio_brinfo_entry_l2):
				(*cb)(priv, l2_addr_lnid,
						&rr->entries.l2[i]);
				break;
			default:
				WARN_ON_ONCE(1);
				rc = -EIO;
				goto out;
			}
	} while (rr->response.code == 0x0107 ||  /* channel busy */
		  (rr->response.code == 1 && /* list stored */
		   /* resume token is non-zero => list incomplete */
		   (rr->naihdr.resume_token.t1 || rr->naihdr.resume_token.t2)));
	(*response) = rr->response.code;

out:
	free_page((unsigned long)rr);
	return rc;
}
EXPORT_SYMBOL_GPL(qdio_pnso_brinfo);

static int __init init_QDIO(void)
{
	int rc;

	rc = qdio_setup_init();
	if (rc)
		return rc;
	rc = tiqdio_allocate_memory();
	if (rc)
		goto out_cache;
	rc = qdio_debug_init();
	if (rc)
		goto out_ti;
	rc = qdio_setup_perf_stats();
	if (rc)
		goto out_debug;
	rc = tiqdio_register_thinints();
	if (rc)
		goto out_perf;
	return 0;

out_perf:
	qdio_remove_perf_stats();
out_debug:
	qdio_debug_exit();
	rc = qdio_debug_init();
	if (rc)
		return rc;
	rc = qdio_setup_init();
	if (rc)
		goto out_debug;
	rc = tiqdio_allocate_memory();
	if (rc)
		goto out_cache;
	rc = tiqdio_register_thinints();
	if (rc)
		goto out_ti;
	return 0;

out_ti:
	tiqdio_free_memory();
out_cache:
	qdio_setup_exit();
out_debug:
	qdio_debug_exit();
	return rc;
}

static void __exit exit_QDIO(void)
{
	tiqdio_unregister_thinints();
	tiqdio_free_memory();
	qdio_remove_perf_stats();
	qdio_debug_exit();
	qdio_setup_exit();
	qdio_setup_exit();
	qdio_debug_exit();
}

module_init(init_QDIO);
module_exit(exit_QDIO);
