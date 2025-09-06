/*
 * zfcp device driver
 *
 * Implementation of FSF commands.
 *
 * Copyright IBM Corporation 2002, 2008
 */

#include "zfcp_ext.h"
 * Copyright IBM Corp. 2002, 2013
 * Copyright IBM Corp. 2002, 2015
 */

#define KMSG_COMPONENT "zfcp"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/blktrace_api.h>
#include <linux/slab.h>
#include <scsi/fc/fc_els.h>
#include "zfcp_ext.h"
#include "zfcp_fc.h"
#include "zfcp_dbf.h"
#include "zfcp_qdio.h"
#include "zfcp_reqlist.h"

struct kmem_cache *zfcp_fsf_qtcb_cache;

static bool ber_stop = true;
module_param(ber_stop, bool, 0600);
MODULE_PARM_DESC(ber_stop,
		 "Shuts down FCP devices for FCP channels that report a bit-error count in excess of its threshold (default on)");

static void zfcp_fsf_request_timeout_handler(unsigned long data)
{
	struct zfcp_adapter *adapter = (struct zfcp_adapter *) data;
	zfcp_erp_adapter_reopen(adapter, ZFCP_STATUS_COMMON_ERP_FAILED, 62,
				NULL);
	zfcp_qdio_siosl(adapter);
	zfcp_erp_adapter_reopen(adapter, ZFCP_STATUS_COMMON_ERP_FAILED,
				"fsrth_1");
}

static void zfcp_fsf_start_timer(struct zfcp_fsf_req *fsf_req,
				 unsigned long timeout)
{
	fsf_req->timer.function = zfcp_fsf_request_timeout_handler;
	fsf_req->timer.data = (unsigned long) fsf_req->adapter;
	fsf_req->timer.expires = jiffies + timeout;
	add_timer(&fsf_req->timer);
}

static void zfcp_fsf_start_erp_timer(struct zfcp_fsf_req *fsf_req)
{
	BUG_ON(!fsf_req->erp_action);
	fsf_req->timer.function = zfcp_erp_timeout_handler;
	fsf_req->timer.data = (unsigned long) fsf_req->erp_action;
	fsf_req->timer.expires = jiffies + 30 * HZ;
	add_timer(&fsf_req->timer);
}

/* association between FSF command and FSF QTCB type */
static u32 fsf_qtcb_type[] = {
	[FSF_QTCB_FCP_CMND] =             FSF_IO_COMMAND,
	[FSF_QTCB_ABORT_FCP_CMND] =       FSF_SUPPORT_COMMAND,
	[FSF_QTCB_OPEN_PORT_WITH_DID] =   FSF_SUPPORT_COMMAND,
	[FSF_QTCB_OPEN_LUN] =             FSF_SUPPORT_COMMAND,
	[FSF_QTCB_CLOSE_LUN] =            FSF_SUPPORT_COMMAND,
	[FSF_QTCB_CLOSE_PORT] =           FSF_SUPPORT_COMMAND,
	[FSF_QTCB_CLOSE_PHYSICAL_PORT] =  FSF_SUPPORT_COMMAND,
	[FSF_QTCB_SEND_ELS] =             FSF_SUPPORT_COMMAND,
	[FSF_QTCB_SEND_GENERIC] =         FSF_SUPPORT_COMMAND,
	[FSF_QTCB_EXCHANGE_CONFIG_DATA] = FSF_CONFIG_COMMAND,
	[FSF_QTCB_EXCHANGE_PORT_DATA] =   FSF_PORT_COMMAND,
	[FSF_QTCB_DOWNLOAD_CONTROL_FILE] = FSF_SUPPORT_COMMAND,
	[FSF_QTCB_UPLOAD_CONTROL_FILE] =  FSF_SUPPORT_COMMAND
};

static const char *zfcp_act_subtable_type[] = {
	"unknown", "OS", "WWPN", "DID", "LUN"
};

static void zfcp_act_eval_err(struct zfcp_adapter *adapter, u32 table)
{
	u16 subtable = table >> 16;
	u16 rule = table & 0xffff;

	if (subtable && subtable < ARRAY_SIZE(zfcp_act_subtable_type))
		dev_warn(&adapter->ccw_device->dev,
			 "Access denied in subtable %s, rule %d.\n",
			 zfcp_act_subtable_type[subtable], rule);
}

static void zfcp_fsf_access_denied_port(struct zfcp_fsf_req *req,
					struct zfcp_port *port)
{
	struct fsf_qtcb_header *header = &req->qtcb->header;
	dev_warn(&req->adapter->ccw_device->dev,
		 "Access denied, cannot send command to port 0x%016Lx.\n",
		 port->wwpn);
	zfcp_act_eval_err(req->adapter, header->fsf_status_qual.halfword[0]);
	zfcp_act_eval_err(req->adapter, header->fsf_status_qual.halfword[1]);
	zfcp_erp_port_access_denied(port, 55, req);
	req->status |= ZFCP_STATUS_FSFREQ_ERROR;
}

static void zfcp_fsf_access_denied_unit(struct zfcp_fsf_req *req,
					struct zfcp_unit *unit)
{
	struct fsf_qtcb_header *header = &req->qtcb->header;
	dev_warn(&req->adapter->ccw_device->dev,
		 "Access denied for unit 0x%016Lx on port 0x%016Lx.\n",
		 unit->fcp_lun, unit->port->wwpn);
	zfcp_act_eval_err(req->adapter, header->fsf_status_qual.halfword[0]);
	zfcp_act_eval_err(req->adapter, header->fsf_status_qual.halfword[1]);
	zfcp_erp_unit_access_denied(unit, 59, req);
	req->status |= ZFCP_STATUS_FSFREQ_ERROR;
}

static void zfcp_fsf_class_not_supp(struct zfcp_fsf_req *req)
{
	dev_err(&req->adapter->ccw_device->dev,
		"Required FC class not supported by adapter, "
		"shutting down adapter.\n");
	zfcp_erp_adapter_shutdown(req->adapter, 0, 123, req);
static void zfcp_fsf_class_not_supp(struct zfcp_fsf_req *req)
{
	dev_err(&req->adapter->ccw_device->dev, "FCP device not "
		"operational because of an unsupported FC class\n");
	zfcp_erp_adapter_shutdown(req->adapter, 0, "fscns_1");
	req->status |= ZFCP_STATUS_FSFREQ_ERROR;
}

/**
 * zfcp_fsf_req_free - free memory used by fsf request
 * @fsf_req: pointer to struct zfcp_fsf_req
 */
void zfcp_fsf_req_free(struct zfcp_fsf_req *req)
{
	if (likely(req->pool)) {
		if (likely(req->qtcb))
			mempool_free(req->qtcb, req->adapter->pool.qtcb_pool);
		mempool_free(req, req->pool);
		return;
	}

	if (req->qtcb) {
		kmem_cache_free(zfcp_data.fsf_req_qtcb_cache, req);
		return;
	}
}

/**
 * zfcp_fsf_req_dismiss_all - dismiss all fsf requests
 * @adapter: pointer to struct zfcp_adapter
 *
 * Never ever call this without shutting down the adapter first.
 * Otherwise the adapter would continue using and corrupting s390 storage.
 * Included BUG_ON() call to ensure this is done.
 * ERP is supposed to be the only user of this function.
 */
void zfcp_fsf_req_dismiss_all(struct zfcp_adapter *adapter)
{
	struct zfcp_fsf_req *req, *tmp;
	unsigned long flags;
	LIST_HEAD(remove_queue);
	unsigned int i;

	BUG_ON(atomic_read(&adapter->status) & ZFCP_STATUS_ADAPTER_QDIOUP);
	spin_lock_irqsave(&adapter->req_list_lock, flags);
	for (i = 0; i < REQUEST_LIST_SIZE; i++)
		list_splice_init(&adapter->req_list[i], &remove_queue);
	spin_unlock_irqrestore(&adapter->req_list_lock, flags);

	list_for_each_entry_safe(req, tmp, &remove_queue, list) {
		list_del(&req->list);
		req->status |= ZFCP_STATUS_FSFREQ_DISMISSED;
		zfcp_fsf_req_complete(req);
	}
	if (likely(req->qtcb))
		kmem_cache_free(zfcp_fsf_qtcb_cache, req->qtcb);
	kfree(req);
}

static void zfcp_fsf_status_read_port_closed(struct zfcp_fsf_req *req)
{
	struct fsf_status_read_buffer *sr_buf = req->data;
	struct zfcp_adapter *adapter = req->adapter;
	struct zfcp_port *port;
	int d_id = sr_buf->d_id & ZFCP_DID_MASK;
	unsigned long flags;

	read_lock_irqsave(&zfcp_data.config_lock, flags);
	list_for_each_entry(port, &adapter->port_list_head, list)
		if (port->d_id == d_id) {
			read_unlock_irqrestore(&zfcp_data.config_lock, flags);
			switch (sr_buf->status_subtype) {
			case FSF_STATUS_READ_SUB_CLOSE_PHYS_PORT:
				zfcp_erp_port_reopen(port, 0, 101, req);
				break;
			case FSF_STATUS_READ_SUB_ERROR_PORT:
				zfcp_erp_port_shutdown(port, 0, 122, req);
				break;
			}
			return;
		}
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);
}

static void zfcp_fsf_bit_error_threshold(struct zfcp_fsf_req *req)
{
	struct zfcp_adapter *adapter = req->adapter;
	struct fsf_status_read_buffer *sr_buf = req->data;
	struct fsf_bit_error_payload *err = &sr_buf->payload.bit_error;

	dev_warn(&adapter->ccw_device->dev,
		 "Warning: bit error threshold data "
		 "received for the adapter: "
		 "link failures = %i, loss of sync errors = %i, "
		 "loss of signal errors = %i, "
		 "primitive sequence errors = %i, "
		 "invalid transmission word errors = %i, "
		 "CRC errors = %i).\n",
		 err->link_failure_error_count,
		 err->loss_of_sync_error_count,
		 err->loss_of_signal_error_count,
		 err->primitive_sequence_error_count,
		 err->invalid_transmission_word_error_count,
		 err->crc_error_count);
	dev_warn(&adapter->ccw_device->dev,
		 "Additional bit error threshold data of the adapter: "
		 "primitive sequence event time-outs = %i, "
		 "elastic buffer overrun errors = %i, "
		 "advertised receive buffer-to-buffer credit = %i, "
		 "current receice buffer-to-buffer credit = %i, "
		 "advertised transmit buffer-to-buffer credit = %i, "
		 "current transmit buffer-to-buffer credit = %i).\n",
		 err->primitive_sequence_event_timeout_count,
		 err->elastic_buffer_overrun_error_count,
		 err->advertised_receive_b2b_credit,
		 err->current_receive_b2b_credit,
		 err->advertised_transmit_b2b_credit,
		 err->current_transmit_b2b_credit);
}

static void zfcp_fsf_link_down_info_eval(struct zfcp_fsf_req *req, u8 id,
	unsigned long flags;
	struct fsf_status_read_buffer *sr_buf = req->data;
	struct zfcp_adapter *adapter = req->adapter;
	struct zfcp_port *port;
	int d_id = ntoh24(sr_buf->d_id);

	read_lock_irqsave(&adapter->port_list_lock, flags);
	list_for_each_entry(port, &adapter->port_list, list)
		if (port->d_id == d_id) {
			zfcp_erp_port_reopen(port, 0, "fssrpc1");
			break;
		}
	read_unlock_irqrestore(&adapter->port_list_lock, flags);
}

static void zfcp_fsf_link_down_info_eval(struct zfcp_fsf_req *req,
					 struct fsf_link_down_info *link_down)
{
	struct zfcp_adapter *adapter = req->adapter;

	if (atomic_read(&adapter->status) & ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED)
		return;

	atomic_set_mask(ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED, &adapter->status);
	atomic_or(ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED, &adapter->status);

	zfcp_scsi_schedule_rports_block(adapter);

	if (!link_down)
		goto out;

	switch (link_down->error_code) {
	case FSF_PSQ_LINK_NO_LIGHT:
		dev_warn(&req->adapter->ccw_device->dev,
			 "The local link is down: no light detected.\n");
		break;
	case FSF_PSQ_LINK_WRAP_PLUG:
		dev_warn(&req->adapter->ccw_device->dev,
			 "The local link is down: wrap plug detected.\n");
		break;
	case FSF_PSQ_LINK_NO_FCP:
		dev_warn(&req->adapter->ccw_device->dev,
			 "The local link is down: "
			 "adjacent node on link does not support FCP.\n");
		break;
	case FSF_PSQ_LINK_FIRMWARE_UPDATE:
		dev_warn(&req->adapter->ccw_device->dev,
			 "The local link is down: "
			 "firmware update in progress.\n");
		break;
	case FSF_PSQ_LINK_INVALID_WWPN:
		dev_warn(&req->adapter->ccw_device->dev,
			 "The local link is down: "
			 "duplicate or invalid WWPN detected.\n");
		break;
	case FSF_PSQ_LINK_NO_NPIV_SUPPORT:
		dev_warn(&req->adapter->ccw_device->dev,
			 "The local link is down: "
			 "no support for NPIV by Fabric.\n");
		break;
	case FSF_PSQ_LINK_NO_FCP_RESOURCES:
		dev_warn(&req->adapter->ccw_device->dev,
			 "The local link is down: "
			 "out of resource in FCP daughtercard.\n");
		break;
	case FSF_PSQ_LINK_NO_FABRIC_RESOURCES:
		dev_warn(&req->adapter->ccw_device->dev,
			 "The local link is down: "
			 "out of resource in Fabric.\n");
		break;
	case FSF_PSQ_LINK_FABRIC_LOGIN_UNABLE:
		dev_warn(&req->adapter->ccw_device->dev,
			 "The local link is down: "
			 "unable to login to Fabric.\n");
		break;
	case FSF_PSQ_LINK_WWPN_ASSIGNMENT_CORRUPTED:
		dev_warn(&req->adapter->ccw_device->dev,
			 "WWPN assignment file corrupted on adapter.\n");
		break;
	case FSF_PSQ_LINK_MODE_TABLE_CURRUPTED:
		dev_warn(&req->adapter->ccw_device->dev,
			 "Mode table corrupted on adapter.\n");
		break;
	case FSF_PSQ_LINK_NO_WWPN_ASSIGNMENT:
		dev_warn(&req->adapter->ccw_device->dev,
			 "No WWPN for assignment table on adapter.\n");
		break;
	default:
		dev_warn(&req->adapter->ccw_device->dev,
			 "The local link to adapter is down.\n");
	}
out:
	zfcp_erp_adapter_failed(adapter, id, req);
			 "There is no light signal from the local "
			 "fibre channel cable\n");
		break;
	case FSF_PSQ_LINK_WRAP_PLUG:
		dev_warn(&req->adapter->ccw_device->dev,
			 "There is a wrap plug instead of a fibre "
			 "channel cable\n");
		break;
	case FSF_PSQ_LINK_NO_FCP:
		dev_warn(&req->adapter->ccw_device->dev,
			 "The adjacent fibre channel node does not "
			 "support FCP\n");
		break;
	case FSF_PSQ_LINK_FIRMWARE_UPDATE:
		dev_warn(&req->adapter->ccw_device->dev,
			 "The FCP device is suspended because of a "
			 "firmware update\n");
		break;
	case FSF_PSQ_LINK_INVALID_WWPN:
		dev_warn(&req->adapter->ccw_device->dev,
			 "The FCP device detected a WWPN that is "
			 "duplicate or not valid\n");
		break;
	case FSF_PSQ_LINK_NO_NPIV_SUPPORT:
		dev_warn(&req->adapter->ccw_device->dev,
			 "The fibre channel fabric does not support NPIV\n");
		break;
	case FSF_PSQ_LINK_NO_FCP_RESOURCES:
		dev_warn(&req->adapter->ccw_device->dev,
			 "The FCP adapter cannot support more NPIV ports\n");
		break;
	case FSF_PSQ_LINK_NO_FABRIC_RESOURCES:
		dev_warn(&req->adapter->ccw_device->dev,
			 "The adjacent switch cannot support "
			 "more NPIV ports\n");
		break;
	case FSF_PSQ_LINK_FABRIC_LOGIN_UNABLE:
		dev_warn(&req->adapter->ccw_device->dev,
			 "The FCP adapter could not log in to the "
			 "fibre channel fabric\n");
		break;
	case FSF_PSQ_LINK_WWPN_ASSIGNMENT_CORRUPTED:
		dev_warn(&req->adapter->ccw_device->dev,
			 "The WWPN assignment file on the FCP adapter "
			 "has been damaged\n");
		break;
	case FSF_PSQ_LINK_MODE_TABLE_CURRUPTED:
		dev_warn(&req->adapter->ccw_device->dev,
			 "The mode table on the FCP adapter "
			 "has been damaged\n");
		break;
	case FSF_PSQ_LINK_NO_WWPN_ASSIGNMENT:
		dev_warn(&req->adapter->ccw_device->dev,
			 "All NPIV ports on the FCP adapter have "
			 "been assigned\n");
		break;
	default:
		dev_warn(&req->adapter->ccw_device->dev,
			 "The link between the FCP adapter and "
			 "the FC fabric is down\n");
	}
out:
	zfcp_erp_set_adapter_status(adapter, ZFCP_STATUS_COMMON_ERP_FAILED);
}

static void zfcp_fsf_status_read_link_down(struct zfcp_fsf_req *req)
{
	struct zfcp_adapter *adapter = req->adapter;
	struct fsf_status_read_buffer *sr_buf = req->data;
	struct fsf_link_down_info *ldi =
		(struct fsf_link_down_info *) &sr_buf->payload;

	switch (sr_buf->status_subtype) {
	case FSF_STATUS_READ_SUB_NO_PHYSICAL_LINK:
		dev_warn(&adapter->ccw_device->dev,
			 "Physical link is down.\n");
		zfcp_fsf_link_down_info_eval(req, 38, ldi);
		break;
	case FSF_STATUS_READ_SUB_FDISC_FAILED:
		dev_warn(&adapter->ccw_device->dev,
			 "Local link is down "
			 "due to failed FDISC login.\n");
		zfcp_fsf_link_down_info_eval(req, 39, ldi);
		break;
	case FSF_STATUS_READ_SUB_FIRMWARE_UPDATE:
		dev_warn(&adapter->ccw_device->dev,
			 "Local link is down "
			 "due to firmware update on adapter.\n");
		zfcp_fsf_link_down_info_eval(req, 40, NULL);
	};
		zfcp_fsf_link_down_info_eval(req, ldi);
		break;
	case FSF_STATUS_READ_SUB_FDISC_FAILED:
		zfcp_fsf_link_down_info_eval(req, ldi);
		break;
	case FSF_STATUS_READ_SUB_FIRMWARE_UPDATE:
		zfcp_fsf_link_down_info_eval(req, NULL);
	}
}

static void zfcp_fsf_status_read_handler(struct zfcp_fsf_req *req)
{
	struct zfcp_adapter *adapter = req->adapter;
	struct fsf_status_read_buffer *sr_buf = req->data;

	if (req->status & ZFCP_STATUS_FSFREQ_DISMISSED) {
		zfcp_hba_dbf_event_fsf_unsol("dism", adapter, sr_buf);
		mempool_free(sr_buf, adapter->pool.data_status_read);
		zfcp_dbf_hba_fsf_uss("fssrh_1", req);
		mempool_free(virt_to_page(sr_buf), adapter->pool.sr_data);
		zfcp_fsf_req_free(req);
		return;
	}

	zfcp_hba_dbf_event_fsf_unsol("read", adapter, sr_buf);
	zfcp_dbf_hba_fsf_uss("fssrh_4", req);

	switch (sr_buf->status_type) {
	case FSF_STATUS_READ_PORT_CLOSED:
		zfcp_fsf_status_read_port_closed(req);
		break;
	case FSF_STATUS_READ_INCOMING_ELS:
		zfcp_fc_incoming_els(req);
		break;
	case FSF_STATUS_READ_SENSE_DATA_AVAIL:
		break;
	case FSF_STATUS_READ_BIT_ERROR_THRESHOLD:
		zfcp_fsf_bit_error_threshold(req);
		break;
	case FSF_STATUS_READ_LINK_DOWN:
		zfcp_fsf_status_read_link_down(req);
		break;
	case FSF_STATUS_READ_LINK_UP:
		dev_info(&adapter->ccw_device->dev,
			 "Local link was replugged.\n");
		/* All ports should be marked as ready to run again */
		zfcp_erp_modify_adapter_status(adapter, 30, NULL,
					       ZFCP_STATUS_COMMON_RUNNING,
					       ZFCP_SET);
		zfcp_erp_adapter_reopen(adapter,
					ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED |
					ZFCP_STATUS_COMMON_ERP_FAILED,
					102, req);
		break;
	case FSF_STATUS_READ_NOTIFICATION_LOST:
		if (sr_buf->status_subtype & FSF_STATUS_READ_SUB_ACT_UPDATED)
			zfcp_erp_adapter_access_changed(adapter, 135, req);
		if (sr_buf->status_subtype & FSF_STATUS_READ_SUB_INCOMING_ELS)
			schedule_work(&adapter->scan_work);
		break;
	case FSF_STATUS_READ_CFDC_UPDATED:
		zfcp_erp_adapter_access_changed(adapter, 136, req);
		dev_warn(&adapter->ccw_device->dev,
			 "The error threshold for checksum statistics "
			 "has been exceeded\n");
		zfcp_dbf_hba_bit_err("fssrh_3", req);
		if (ber_stop) {
			dev_warn(&adapter->ccw_device->dev,
				 "All paths over this FCP device are disused because of excessive bit errors\n");
			zfcp_erp_adapter_shutdown(adapter, 0, "fssrh_b");
		} else {
			dev_warn(&adapter->ccw_device->dev,
				 "The error threshold for checksum statistics has been exceeded\n");
		}
		break;
	case FSF_STATUS_READ_LINK_DOWN:
		zfcp_fsf_status_read_link_down(req);
		zfcp_fc_enqueue_event(adapter, FCH_EVT_LINKDOWN, 0);
		break;
	case FSF_STATUS_READ_LINK_UP:
		dev_info(&adapter->ccw_device->dev,
			 "The local link has been restored\n");
		/* All ports should be marked as ready to run again */
		zfcp_erp_set_adapter_status(adapter,
					    ZFCP_STATUS_COMMON_RUNNING);
		zfcp_erp_adapter_reopen(adapter,
					ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED |
					ZFCP_STATUS_COMMON_ERP_FAILED,
					"fssrh_2");
		zfcp_fc_enqueue_event(adapter, FCH_EVT_LINKUP, 0);

		break;
	case FSF_STATUS_READ_NOTIFICATION_LOST:
		if (sr_buf->status_subtype & FSF_STATUS_READ_SUB_INCOMING_ELS)
			zfcp_fc_conditional_port_scan(adapter);
		break;
	case FSF_STATUS_READ_FEATURE_UPDATE_ALERT:
		adapter->adapter_features = sr_buf->payload.word[0];
		break;
	}

	mempool_free(sr_buf, adapter->pool.data_status_read);
	zfcp_fsf_req_free(req);

	atomic_inc(&adapter->stat_miss);
	schedule_work(&adapter->stat_work);
	mempool_free(virt_to_page(sr_buf), adapter->pool.sr_data);
	zfcp_fsf_req_free(req);

	atomic_inc(&adapter->stat_miss);
	queue_work(adapter->work_queue, &adapter->stat_work);
}

static void zfcp_fsf_fsfstatus_qual_eval(struct zfcp_fsf_req *req)
{
	switch (req->qtcb->header.fsf_status_qual.word[0]) {
	case FSF_SQ_FCP_RSP_AVAILABLE:
	case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
	case FSF_SQ_NO_RETRY_POSSIBLE:
	case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
		return;
	case FSF_SQ_COMMAND_ABORTED:
		req->status |= ZFCP_STATUS_FSFREQ_ABORTED;
		break;
	case FSF_SQ_NO_RECOM:
		dev_err(&req->adapter->ccw_device->dev,
			"No recommendation could be given for a "
			"problem on the adapter.\n");
		zfcp_erp_adapter_shutdown(req->adapter, 0, 121, req);
		break;
	case FSF_SQ_NO_RECOM:
		dev_err(&req->adapter->ccw_device->dev,
			"The FCP adapter reported a problem "
			"that cannot be recovered\n");
		zfcp_qdio_siosl(req->adapter);
		zfcp_erp_adapter_shutdown(req->adapter, 0, "fsfsqe1");
		break;
	}
	/* all non-return stats set FSFREQ_ERROR*/
	req->status |= ZFCP_STATUS_FSFREQ_ERROR;
}

static void zfcp_fsf_fsfstatus_eval(struct zfcp_fsf_req *req)
{
	if (unlikely(req->status & ZFCP_STATUS_FSFREQ_ERROR))
		return;

	switch (req->qtcb->header.fsf_status) {
	case FSF_UNKNOWN_COMMAND:
		dev_err(&req->adapter->ccw_device->dev,
			"Command issued by the device driver (0x%x) is "
			"not known by the adapter.\n",
			req->qtcb->header.fsf_command);
		zfcp_erp_adapter_shutdown(req->adapter, 0, 120, req);
			"The FCP adapter does not recognize the command 0x%x\n",
			req->qtcb->header.fsf_command);
		zfcp_erp_adapter_shutdown(req->adapter, 0, "fsfse_1");
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_ADAPTER_STATUS_AVAILABLE:
		zfcp_fsf_fsfstatus_qual_eval(req);
		break;
	}
}

static void zfcp_fsf_protstatus_eval(struct zfcp_fsf_req *req)
{
	struct zfcp_adapter *adapter = req->adapter;
	struct fsf_qtcb *qtcb = req->qtcb;
	union fsf_prot_status_qual *psq = &qtcb->prefix.prot_status_qual;

	zfcp_hba_dbf_event_fsf_response(req);

	if (req->status & ZFCP_STATUS_FSFREQ_DISMISSED) {
		req->status |= ZFCP_STATUS_FSFREQ_ERROR |
			ZFCP_STATUS_FSFREQ_RETRY; /* only for SCSI cmnds. */
	zfcp_dbf_hba_fsf_response(req);

	if (req->status & ZFCP_STATUS_FSFREQ_DISMISSED) {
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		return;
	}

	switch (qtcb->prefix.prot_status) {
	case FSF_PROT_GOOD:
	case FSF_PROT_FSF_STATUS_PRESENTED:
		return;
	case FSF_PROT_QTCB_VERSION_ERROR:
		dev_err(&adapter->ccw_device->dev,
			"The QTCB version requested by zfcp (0x%x) is not "
			"supported by the FCP adapter (lowest supported "
			"0x%x, highest supported 0x%x).\n",
			FSF_QTCB_CURRENT_VERSION, psq->word[0],
			psq->word[1]);
		zfcp_erp_adapter_shutdown(adapter, 0, 117, req);
		break;
	case FSF_PROT_ERROR_STATE:
	case FSF_PROT_SEQ_NUMB_ERROR:
		zfcp_erp_adapter_reopen(adapter, 0, 98, req);
		req->status |= ZFCP_STATUS_FSFREQ_RETRY;
		break;
	case FSF_PROT_UNSUPP_QTCB_TYPE:
		dev_err(&adapter->ccw_device->dev,
			"Packet header type used by the device driver is "
			"incompatible with that used on the adapter.\n");
		zfcp_erp_adapter_shutdown(adapter, 0, 118, req);
		break;
	case FSF_PROT_HOST_CONNECTION_INITIALIZING:
		atomic_set_mask(ZFCP_STATUS_ADAPTER_HOST_CON_INIT,
			"QTCB version 0x%x not supported by FCP adapter "
			"(0x%x to 0x%x)\n", FSF_QTCB_CURRENT_VERSION,
			psq->word[0], psq->word[1]);
		zfcp_erp_adapter_shutdown(adapter, 0, "fspse_1");
		break;
	case FSF_PROT_ERROR_STATE:
	case FSF_PROT_SEQ_NUMB_ERROR:
		zfcp_erp_adapter_reopen(adapter, 0, "fspse_2");
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_PROT_UNSUPP_QTCB_TYPE:
		dev_err(&adapter->ccw_device->dev,
			"The QTCB type is not supported by the FCP adapter\n");
		zfcp_erp_adapter_shutdown(adapter, 0, "fspse_3");
		break;
	case FSF_PROT_HOST_CONNECTION_INITIALIZING:
		atomic_or(ZFCP_STATUS_ADAPTER_HOST_CON_INIT,
				&adapter->status);
		break;
	case FSF_PROT_DUPLICATE_REQUEST_ID:
		dev_err(&adapter->ccw_device->dev,
			"The request identifier 0x%Lx is ambiguous.\n",
			(unsigned long long)qtcb->bottom.support.req_handle);
		zfcp_erp_adapter_shutdown(adapter, 0, 78, req);
		break;
	case FSF_PROT_LINK_DOWN:
		zfcp_fsf_link_down_info_eval(req, 37, &psq->link_down_info);
		/* FIXME: reopening adapter now? better wait for link up */
		zfcp_erp_adapter_reopen(adapter, 0, 79, req);
		break;
	case FSF_PROT_REEST_QUEUE:
		/* All ports should be marked as ready to run again */
		zfcp_erp_modify_adapter_status(adapter, 28, NULL,
					       ZFCP_STATUS_COMMON_RUNNING,
					       ZFCP_SET);
		zfcp_erp_adapter_reopen(adapter,
					ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED |
					ZFCP_STATUS_COMMON_ERP_FAILED, 99, req);
		break;
	default:
		dev_err(&adapter->ccw_device->dev,
			"Transfer protocol status information"
			"provided by the adapter (0x%x) "
			"is not compatible with the device driver.\n",
			qtcb->prefix.prot_status);
		zfcp_erp_adapter_shutdown(adapter, 0, 119, req);
			"0x%Lx is an ambiguous request identifier\n",
			(unsigned long long)qtcb->bottom.support.req_handle);
		zfcp_erp_adapter_shutdown(adapter, 0, "fspse_4");
		break;
	case FSF_PROT_LINK_DOWN:
		zfcp_fsf_link_down_info_eval(req, &psq->link_down_info);
		/* go through reopen to flush pending requests */
		zfcp_erp_adapter_reopen(adapter, 0, "fspse_6");
		break;
	case FSF_PROT_REEST_QUEUE:
		/* All ports should be marked as ready to run again */
		zfcp_erp_set_adapter_status(adapter,
					    ZFCP_STATUS_COMMON_RUNNING);
		zfcp_erp_adapter_reopen(adapter,
					ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED |
					ZFCP_STATUS_COMMON_ERP_FAILED,
					"fspse_8");
		break;
	default:
		dev_err(&adapter->ccw_device->dev,
			"0x%x is not a valid transfer protocol status\n",
			qtcb->prefix.prot_status);
		zfcp_qdio_siosl(adapter);
		zfcp_erp_adapter_shutdown(adapter, 0, "fspse_9");
	}
	req->status |= ZFCP_STATUS_FSFREQ_ERROR;
}

/**
 * zfcp_fsf_req_complete - process completion of a FSF request
 * @fsf_req: The FSF request that has been completed.
 *
 * When a request has been completed either from the FCP adapter,
 * or it has been dismissed due to a queue shutdown, this function
 * is called to process the completion status and trigger further
 * events related to the FSF request.
 */
void zfcp_fsf_req_complete(struct zfcp_fsf_req *req)
static void zfcp_fsf_req_complete(struct zfcp_fsf_req *req)
{
	if (unlikely(req->fsf_command == FSF_QTCB_UNSOLICITED_STATUS)) {
		zfcp_fsf_status_read_handler(req);
		return;
	}

	del_timer(&req->timer);
	zfcp_fsf_protstatus_eval(req);
	zfcp_fsf_fsfstatus_eval(req);
	req->handler(req);

	if (req->erp_action)
		zfcp_erp_notify(req->erp_action, 0);
	req->status |= ZFCP_STATUS_FSFREQ_COMPLETED;

	if (likely(req->status & ZFCP_STATUS_FSFREQ_CLEANUP))
		zfcp_fsf_req_free(req);
	else
	/* notify initiator waiting for the requests completion */
	/*
	 * FIXME: Race! We must not access fsf_req here as it might have been
	 * cleaned up already due to the set ZFCP_STATUS_FSFREQ_COMPLETED
	 * flag. It's an improbable case. But, we have the same paranoia for
	 * the cleanup flag already.
	 * Might better be handled using complete()?
	 * (setting the flag and doing wakeup ought to be atomic
	 *  with regard to checking the flag as long as waitqueue is
	 *  part of the to be released structure)
	 */
		wake_up(&req->completion_wq);
		complete(&req->completion);
}

/**
 * zfcp_fsf_req_dismiss_all - dismiss all fsf requests
 * @adapter: pointer to struct zfcp_adapter
 *
 * Never ever call this without shutting down the adapter first.
 * Otherwise the adapter would continue using and corrupting s390 storage.
 * Included BUG_ON() call to ensure this is done.
 * ERP is supposed to be the only user of this function.
 */
void zfcp_fsf_req_dismiss_all(struct zfcp_adapter *adapter)
{
	struct zfcp_fsf_req *req, *tmp;
	LIST_HEAD(remove_queue);

	BUG_ON(atomic_read(&adapter->status) & ZFCP_STATUS_ADAPTER_QDIOUP);
	zfcp_reqlist_move(adapter->req_list, &remove_queue);

	list_for_each_entry_safe(req, tmp, &remove_queue, list) {
		list_del(&req->list);
		req->status |= ZFCP_STATUS_FSFREQ_DISMISSED;
		zfcp_fsf_req_complete(req);
	}
}

#define ZFCP_FSF_PORTSPEED_1GBIT	(1 <<  0)
#define ZFCP_FSF_PORTSPEED_2GBIT	(1 <<  1)
#define ZFCP_FSF_PORTSPEED_4GBIT	(1 <<  2)
#define ZFCP_FSF_PORTSPEED_10GBIT	(1 <<  3)
#define ZFCP_FSF_PORTSPEED_8GBIT	(1 <<  4)
#define ZFCP_FSF_PORTSPEED_16GBIT	(1 <<  5)
#define ZFCP_FSF_PORTSPEED_NOT_NEGOTIATED (1 << 15)

static u32 zfcp_fsf_convert_portspeed(u32 fsf_speed)
{
	u32 fdmi_speed = 0;
	if (fsf_speed & ZFCP_FSF_PORTSPEED_1GBIT)
		fdmi_speed |= FC_PORTSPEED_1GBIT;
	if (fsf_speed & ZFCP_FSF_PORTSPEED_2GBIT)
		fdmi_speed |= FC_PORTSPEED_2GBIT;
	if (fsf_speed & ZFCP_FSF_PORTSPEED_4GBIT)
		fdmi_speed |= FC_PORTSPEED_4GBIT;
	if (fsf_speed & ZFCP_FSF_PORTSPEED_10GBIT)
		fdmi_speed |= FC_PORTSPEED_10GBIT;
	if (fsf_speed & ZFCP_FSF_PORTSPEED_8GBIT)
		fdmi_speed |= FC_PORTSPEED_8GBIT;
	if (fsf_speed & ZFCP_FSF_PORTSPEED_16GBIT)
		fdmi_speed |= FC_PORTSPEED_16GBIT;
	if (fsf_speed & ZFCP_FSF_PORTSPEED_NOT_NEGOTIATED)
		fdmi_speed |= FC_PORTSPEED_NOT_NEGOTIATED;
	return fdmi_speed;
}

static int zfcp_fsf_exchange_config_evaluate(struct zfcp_fsf_req *req)
{
	struct fsf_qtcb_bottom_config *bottom;
	struct zfcp_adapter *adapter = req->adapter;
	struct Scsi_Host *shost = adapter->scsi_host;

	bottom = &req->qtcb->bottom.config;
	struct fsf_qtcb_bottom_config *bottom = &req->qtcb->bottom.config;
	struct zfcp_adapter *adapter = req->adapter;
	struct Scsi_Host *shost = adapter->scsi_host;
	struct fc_els_flogi *nsp, *plogi;

	/* adjust pointers for missing command code */
	nsp = (struct fc_els_flogi *) ((u8 *)&bottom->nport_serv_param
					- sizeof(u32));
	plogi = (struct fc_els_flogi *) ((u8 *)&bottom->plogi_payload
					- sizeof(u32));

	if (req->data)
		memcpy(req->data, bottom, sizeof(*bottom));

	fc_host_node_name(shost) = bottom->nport_serv_param.wwnn;
	fc_host_port_name(shost) = bottom->nport_serv_param.wwpn;
	fc_host_port_id(shost) = bottom->s_id & ZFCP_DID_MASK;
	fc_host_speed(shost) = bottom->fc_link_speed;
	fc_host_supported_classes(shost) = FC_COS_CLASS2 | FC_COS_CLASS3;

	adapter->hydra_version = bottom->adapter_type;
	adapter->timer_ticks = bottom->timer_interval;
	fc_host_port_name(shost) = nsp->fl_wwpn;
	fc_host_node_name(shost) = nsp->fl_wwnn;
	fc_host_supported_classes(shost) = FC_COS_CLASS2 | FC_COS_CLASS3;

	adapter->timer_ticks = bottom->timer_interval & ZFCP_FSF_TIMER_INT_MASK;
	adapter->stat_read_buf_num = max(bottom->status_read_buf_num,
					 (u16)FSF_STATUS_READS_RECOM);

	if (fc_host_permanent_port_name(shost) == -1)
		fc_host_permanent_port_name(shost) = fc_host_port_name(shost);

	switch (bottom->fc_topology) {
	case FSF_TOPO_P2P:
		adapter->peer_d_id = bottom->peer_d_id & ZFCP_DID_MASK;
		adapter->peer_wwpn = bottom->plogi_payload.wwpn;
		adapter->peer_wwnn = bottom->plogi_payload.wwnn;
		fc_host_port_type(shost) = FC_PORTTYPE_PTP;
		if (req->erp_action)
			dev_info(&adapter->ccw_device->dev,
				 "Point-to-Point fibrechannel "
				 "configuration detected.\n");
		break;
	case FSF_TOPO_FABRIC:
		fc_host_port_type(shost) = FC_PORTTYPE_NPORT;
		if (req->erp_action)
			dev_info(&adapter->ccw_device->dev,
				 "Switched fabric fibrechannel "
				 "network detected.\n");
		break;
	case FSF_TOPO_AL:
		fc_host_port_type(shost) = FC_PORTTYPE_NLPORT;
		dev_err(&adapter->ccw_device->dev,
			"Unsupported arbitrated loop fibrechannel "
			"topology detected, shutting down "
			"adapter.\n");
		zfcp_erp_adapter_shutdown(adapter, 0, 127, req);
		return -EIO;
	default:
		fc_host_port_type(shost) = FC_PORTTYPE_UNKNOWN;
		dev_err(&adapter->ccw_device->dev,
			"The fibrechannel topology reported by the"
			" adapter is not known by the zfcp driver,"
			" shutting down adapter.\n");
		zfcp_erp_adapter_shutdown(adapter, 0, 128, req);
	zfcp_scsi_set_prot(adapter);

	/* no error return above here, otherwise must fix call chains */
	/* do not evaluate invalid fields */
	if (req->qtcb->header.fsf_status == FSF_EXCHANGE_CONFIG_DATA_INCOMPLETE)
		return 0;

	fc_host_port_id(shost) = ntoh24(bottom->s_id);
	fc_host_speed(shost) =
		zfcp_fsf_convert_portspeed(bottom->fc_link_speed);

	adapter->hydra_version = bottom->adapter_type;

	switch (bottom->fc_topology) {
	case FSF_TOPO_P2P:
		adapter->peer_d_id = ntoh24(bottom->peer_d_id);
		adapter->peer_wwpn = plogi->fl_wwpn;
		adapter->peer_wwnn = plogi->fl_wwnn;
		fc_host_port_type(shost) = FC_PORTTYPE_PTP;
		break;
	case FSF_TOPO_FABRIC:
		if (bottom->connection_features & FSF_FEATURE_NPIV_MODE)
			fc_host_port_type(shost) = FC_PORTTYPE_NPIV;
		else
			fc_host_port_type(shost) = FC_PORTTYPE_NPORT;
		break;
	case FSF_TOPO_AL:
		fc_host_port_type(shost) = FC_PORTTYPE_NLPORT;
		/* fall through */
	default:
		dev_err(&adapter->ccw_device->dev,
			"Unknown or unsupported arbitrated loop "
			"fibre channel topology detected\n");
		zfcp_erp_adapter_shutdown(adapter, 0, "fsece_1");
		return -EIO;
	}

	return 0;
}

static void zfcp_fsf_exchange_config_data_handler(struct zfcp_fsf_req *req)
{
	struct zfcp_adapter *adapter = req->adapter;
	struct fsf_qtcb *qtcb = req->qtcb;
	struct fsf_qtcb_bottom_config *bottom = &qtcb->bottom.config;
	struct Scsi_Host *shost = adapter->scsi_host;

	if (req->status & ZFCP_STATUS_FSFREQ_ERROR)
		return;

	adapter->fsf_lic_version = bottom->lic_version;
	adapter->adapter_features = bottom->adapter_features;
	adapter->connection_features = bottom->connection_features;
	adapter->peer_wwpn = 0;
	adapter->peer_wwnn = 0;
	adapter->peer_d_id = 0;

	switch (qtcb->header.fsf_status) {
	case FSF_GOOD:
		if (zfcp_fsf_exchange_config_evaluate(req))
			return;

		if (bottom->max_qtcb_size < sizeof(struct fsf_qtcb)) {
			dev_err(&adapter->ccw_device->dev,
				"Maximum QTCB size (%d bytes) allowed by "
				"the adapter is lower than the minimum "
				"required by the driver (%ld bytes).\n",
				bottom->max_qtcb_size,
				sizeof(struct fsf_qtcb));
			zfcp_erp_adapter_shutdown(adapter, 0, 129, req);
			return;
		}
		atomic_set_mask(ZFCP_STATUS_ADAPTER_XCONFIG_OK,
				"FCP adapter maximum QTCB size (%d bytes) "
				"is too small\n",
				bottom->max_qtcb_size);
			zfcp_erp_adapter_shutdown(adapter, 0, "fsecdh1");
			return;
		}
		atomic_or(ZFCP_STATUS_ADAPTER_XCONFIG_OK,
				&adapter->status);
		break;
	case FSF_EXCHANGE_CONFIG_DATA_INCOMPLETE:
		fc_host_node_name(shost) = 0;
		fc_host_port_name(shost) = 0;
		fc_host_port_id(shost) = 0;
		fc_host_speed(shost) = FC_PORTSPEED_UNKNOWN;
		fc_host_port_type(shost) = FC_PORTTYPE_UNKNOWN;
		adapter->hydra_version = 0;

		atomic_set_mask(ZFCP_STATUS_ADAPTER_XCONFIG_OK,
				&adapter->status);

		zfcp_fsf_link_down_info_eval(req, 42,
			&qtcb->header.fsf_status_qual.link_down_info);
		break;
	default:
		zfcp_erp_adapter_shutdown(adapter, 0, 130, req);
		/* avoids adapter shutdown to be able to recognize
		 * events such as LINK UP */
		atomic_or(ZFCP_STATUS_ADAPTER_XCONFIG_OK,
				&adapter->status);
		zfcp_fsf_link_down_info_eval(req,
			&qtcb->header.fsf_status_qual.link_down_info);
		if (zfcp_fsf_exchange_config_evaluate(req))
			return;
		break;
	default:
		zfcp_erp_adapter_shutdown(adapter, 0, "fsecdh3");
		return;
	}

	if (adapter->adapter_features & FSF_FEATURE_HBAAPI_MANAGEMENT) {
		adapter->hardware_version = bottom->hardware_version;
		memcpy(fc_host_serial_number(shost), bottom->serial_number,
		       min(FC_SERIAL_NUMBER_SIZE, 17));
		EBCASC(fc_host_serial_number(shost),
		       min(FC_SERIAL_NUMBER_SIZE, 17));
	}

	if (FSF_QTCB_CURRENT_VERSION < bottom->low_qtcb_version) {
		dev_err(&adapter->ccw_device->dev,
			"The adapter only supports newer control block "
			"versions, try updated device driver.\n");
		zfcp_erp_adapter_shutdown(adapter, 0, 125, req);
			"The FCP adapter only supports newer "
			"control block versions\n");
		zfcp_erp_adapter_shutdown(adapter, 0, "fsecdh4");
		return;
	}
	if (FSF_QTCB_CURRENT_VERSION > bottom->high_qtcb_version) {
		dev_err(&adapter->ccw_device->dev,
			"The adapter only supports older control block "
			"versions, consider a microcode upgrade.\n");
		zfcp_erp_adapter_shutdown(adapter, 0, 126, req);
			"The FCP adapter only supports older "
			"control block versions\n");
		zfcp_erp_adapter_shutdown(adapter, 0, "fsecdh5");
	}
}

static void zfcp_fsf_exchange_port_evaluate(struct zfcp_fsf_req *req)
{
	struct zfcp_adapter *adapter = req->adapter;
	struct fsf_qtcb_bottom_port *bottom = &req->qtcb->bottom.port;
	struct Scsi_Host *shost = adapter->scsi_host;

	if (req->data)
		memcpy(req->data, bottom, sizeof(*bottom));

	if (adapter->connection_features & FSF_FEATURE_NPIV_MODE)
		fc_host_permanent_port_name(shost) = bottom->wwpn;
	else
		fc_host_permanent_port_name(shost) = fc_host_port_name(shost);
	fc_host_maxframe_size(shost) = bottom->maximum_frame_size;
	fc_host_supported_speeds(shost) = bottom->supported_speed;
	if (adapter->connection_features & FSF_FEATURE_NPIV_MODE) {
		fc_host_permanent_port_name(shost) = bottom->wwpn;
	} else
		fc_host_permanent_port_name(shost) = fc_host_port_name(shost);
	fc_host_maxframe_size(shost) = bottom->maximum_frame_size;
	fc_host_supported_speeds(shost) =
		zfcp_fsf_convert_portspeed(bottom->supported_speed);
	memcpy(fc_host_supported_fc4s(shost), bottom->supported_fc4_types,
	       FC_FC4_LIST_SIZE);
	memcpy(fc_host_active_fc4s(shost), bottom->active_fc4_types,
	       FC_FC4_LIST_SIZE);
}

static void zfcp_fsf_exchange_port_data_handler(struct zfcp_fsf_req *req)
{
	struct zfcp_adapter *adapter = req->adapter;
	struct fsf_qtcb *qtcb = req->qtcb;

	if (req->status & ZFCP_STATUS_FSFREQ_ERROR)
		return;

	switch (qtcb->header.fsf_status) {
	case FSF_GOOD:
		zfcp_fsf_exchange_port_evaluate(req);
		atomic_set_mask(ZFCP_STATUS_ADAPTER_XPORT_OK, &adapter->status);
		break;
	case FSF_EXCHANGE_CONFIG_DATA_INCOMPLETE:
		zfcp_fsf_exchange_port_evaluate(req);
		atomic_set_mask(ZFCP_STATUS_ADAPTER_XPORT_OK, &adapter->status);
		zfcp_fsf_link_down_info_eval(req, 43,
		break;
	case FSF_EXCHANGE_CONFIG_DATA_INCOMPLETE:
		zfcp_fsf_exchange_port_evaluate(req);
		zfcp_fsf_link_down_info_eval(req,
			&qtcb->header.fsf_status_qual.link_down_info);
		break;
	}
}

static int zfcp_fsf_sbal_check(struct zfcp_qdio_queue *queue)
{
	spin_lock_bh(&queue->lock);
	if (atomic_read(&queue->count))
		return 1;
	spin_unlock_bh(&queue->lock);
	return 0;
}

static int zfcp_fsf_req_sbal_get(struct zfcp_adapter *adapter)
{
	long ret;
	struct zfcp_qdio_queue *req_q = &adapter->req_q;

	spin_unlock_bh(&req_q->lock);
	ret = wait_event_interruptible_timeout(adapter->request_wq,
					zfcp_fsf_sbal_check(req_q), 5 * HZ);
	if (ret > 0)
		return 0;

	spin_lock_bh(&req_q->lock);
	return -EIO;
}

static struct zfcp_fsf_req *zfcp_fsf_alloc_noqtcb(mempool_t *pool)
{
	struct zfcp_fsf_req *req;
	req = mempool_alloc(pool, GFP_ATOMIC);
	if (!req)
		return NULL;
	memset(req, 0, sizeof(*req));
	return req;
}

static struct zfcp_fsf_req *zfcp_fsf_alloc_qtcb(mempool_t *pool)
{
	struct zfcp_fsf_req_qtcb *qtcb;
static struct zfcp_fsf_req *zfcp_fsf_alloc(mempool_t *pool)
{
	struct zfcp_fsf_req *req;

	if (likely(pool))
		req = mempool_alloc(pool, GFP_ATOMIC);
	else
		req = kmalloc(sizeof(*req), GFP_ATOMIC);

	if (unlikely(!req))
		return NULL;

	memset(req, 0, sizeof(*req));
	req->pool = pool;
	return req;
}

static struct fsf_qtcb *zfcp_qtcb_alloc(mempool_t *pool)
{
	struct fsf_qtcb *qtcb;

	if (likely(pool))
		qtcb = mempool_alloc(pool, GFP_ATOMIC);
	else
		qtcb = kmem_cache_alloc(zfcp_data.fsf_req_qtcb_cache,
					GFP_ATOMIC);
		qtcb = kmem_cache_alloc(zfcp_fsf_qtcb_cache, GFP_ATOMIC);

	if (unlikely(!qtcb))
		return NULL;

	memset(qtcb, 0, sizeof(*qtcb));
	qtcb->fsf_req.qtcb = &qtcb->qtcb;
	qtcb->fsf_req.pool = pool;

	return &qtcb->fsf_req;
}

static struct zfcp_fsf_req *zfcp_fsf_req_create(struct zfcp_adapter *adapter,
						u32 fsf_cmd, int req_flags,
						mempool_t *pool)
{
	volatile struct qdio_buffer_element *sbale;

	struct zfcp_fsf_req *req;
	struct zfcp_qdio_queue *req_q = &adapter->req_q;

	if (req_flags & ZFCP_REQ_NO_QTCB)
		req = zfcp_fsf_alloc_noqtcb(pool);
	else
		req = zfcp_fsf_alloc_qtcb(pool);

	if (unlikely(!req))
		return ERR_PTR(-EIO);
	return qtcb;
}

static struct zfcp_fsf_req *zfcp_fsf_req_create(struct zfcp_qdio *qdio,
						u32 fsf_cmd, u8 sbtype,
						mempool_t *pool)
{
	struct zfcp_adapter *adapter = qdio->adapter;
	struct zfcp_fsf_req *req = zfcp_fsf_alloc(pool);

	if (unlikely(!req))
		return ERR_PTR(-ENOMEM);

	if (adapter->req_no == 0)
		adapter->req_no++;

	INIT_LIST_HEAD(&req->list);
	init_timer(&req->timer);
	init_waitqueue_head(&req->completion_wq);

	req->adapter = adapter;
	req->fsf_command = fsf_cmd;
	req->req_id = adapter->req_no++;
	req->sbal_number = 1;
	req->sbal_first = req_q->first;
	req->sbal_last = req_q->first;
	req->sbale_curr = 1;

	sbale = zfcp_qdio_sbale_req(req);
	sbale[0].addr = (void *) req->req_id;
	sbale[0].flags |= SBAL_FLAGS0_COMMAND;

	if (likely(req->qtcb)) {
		req->qtcb->prefix.req_seq_no = req->adapter->fsf_req_seq_no;
	init_completion(&req->completion);

	req->adapter = adapter;
	req->fsf_command = fsf_cmd;
	req->req_id = adapter->req_no;

	if (likely(fsf_cmd != FSF_QTCB_UNSOLICITED_STATUS)) {
		if (likely(pool))
			req->qtcb = zfcp_qtcb_alloc(adapter->pool.qtcb_pool);
		else
			req->qtcb = zfcp_qtcb_alloc(NULL);

		if (unlikely(!req->qtcb)) {
			zfcp_fsf_req_free(req);
			return ERR_PTR(-ENOMEM);
		}

		req->seq_no = adapter->fsf_req_seq_no;
		req->qtcb->prefix.req_seq_no = adapter->fsf_req_seq_no;
		req->qtcb->prefix.req_id = req->req_id;
		req->qtcb->prefix.ulp_info = 26;
		req->qtcb->prefix.qtcb_type = fsf_qtcb_type[req->fsf_command];
		req->qtcb->prefix.qtcb_version = FSF_QTCB_CURRENT_VERSION;
		req->qtcb->header.req_handle = req->req_id;
		req->qtcb->header.fsf_command = req->fsf_command;
		req->seq_no = adapter->fsf_req_seq_no;
		req->qtcb->prefix.req_seq_no = adapter->fsf_req_seq_no;
		sbale[1].addr = (void *) req->qtcb;
		sbale[1].length = sizeof(struct fsf_qtcb);
	}

	if (!(atomic_read(&adapter->status) & ZFCP_STATUS_ADAPTER_QDIOUP)) {
		zfcp_fsf_req_free(req);
		return ERR_PTR(-EIO);
	}

	if (likely(req_flags & ZFCP_REQ_AUTO_CLEANUP))
		req->status |= ZFCP_STATUS_FSFREQ_CLEANUP;
	}

	zfcp_qdio_req_init(adapter->qdio, &req->qdio_req, req->req_id, sbtype,
			   req->qtcb, sizeof(struct fsf_qtcb));

	return req;
}

static int zfcp_fsf_req_send(struct zfcp_fsf_req *req)
{
	struct zfcp_adapter *adapter = req->adapter;
	struct zfcp_qdio_queue *req_q = &adapter->req_q;
	int idx;

	/* put allocated FSF request into hash table */
	spin_lock(&adapter->req_list_lock);
	idx = zfcp_reqlist_hash(req->req_id);
	list_add_tail(&req->list, &adapter->req_list[idx]);
	spin_unlock(&adapter->req_list_lock);

	req->issued = get_clock();
	if (zfcp_qdio_send(req)) {
		/* Queues are down..... */
		del_timer(&req->timer);
		spin_lock(&adapter->req_list_lock);
		zfcp_reqlist_remove(adapter, req);
		spin_unlock(&adapter->req_list_lock);
		/* undo changes in request queue made for this request */
		atomic_add(req->sbal_number, &req_q->count);
		req_q->first -= req->sbal_number;
		req_q->first += QDIO_MAX_BUFFERS_PER_Q;
		req_q->first %= QDIO_MAX_BUFFERS_PER_Q; /* wrap */
		zfcp_erp_adapter_reopen(adapter, 0, 116, req);
	struct zfcp_qdio *qdio = adapter->qdio;
	int with_qtcb = (req->qtcb != NULL);
	int req_id = req->req_id;

	zfcp_reqlist_add(adapter->req_list, req);

	req->qdio_req.qdio_outb_usage = atomic_read(&qdio->req_q_free);
	req->issued = get_tod_clock();
	if (zfcp_qdio_send(qdio, &req->qdio_req)) {
		del_timer(&req->timer);
		/* lookup request again, list might have changed */
		zfcp_reqlist_find_rm(adapter->req_list, req_id);
		zfcp_erp_adapter_reopen(adapter, 0, "fsrs__1");
		return -EIO;
	}

	/* Don't increase for unsolicited status */
	if (req->qtcb)
		adapter->fsf_req_seq_no++;
	if (with_qtcb)
		adapter->fsf_req_seq_no++;
	adapter->req_no++;

	return 0;
}

/**
 * zfcp_fsf_status_read - send status read request
 * @adapter: pointer to struct zfcp_adapter
 * @req_flags: request flags
 * Returns: 0 on success, ERROR otherwise
 */
int zfcp_fsf_status_read(struct zfcp_adapter *adapter)
{
	struct zfcp_fsf_req *req;
	struct fsf_status_read_buffer *sr_buf;
	volatile struct qdio_buffer_element *sbale;
	int retval = -EIO;

	spin_lock_bh(&adapter->req_q.lock);
	if (zfcp_fsf_req_sbal_get(adapter))
		goto out;

	req = zfcp_fsf_req_create(adapter, FSF_QTCB_UNSOLICITED_STATUS,
				  ZFCP_REQ_NO_QTCB,
				  adapter->pool.fsf_req_status_read);
int zfcp_fsf_status_read(struct zfcp_qdio *qdio)
{
	struct zfcp_adapter *adapter = qdio->adapter;
	struct zfcp_fsf_req *req;
	struct fsf_status_read_buffer *sr_buf;
	struct page *page;
	int retval = -EIO;

	spin_lock_irq(&qdio->req_q_lock);
	if (zfcp_qdio_sbal_get(qdio))
		goto out;

	req = zfcp_fsf_req_create(qdio, FSF_QTCB_UNSOLICITED_STATUS,
				  SBAL_SFLAGS0_TYPE_STATUS,
				  adapter->pool.status_read_req);
	if (IS_ERR(req)) {
		retval = PTR_ERR(req);
		goto out;
	}

	sbale = zfcp_qdio_sbale_req(req);
	sbale[0].flags |= SBAL_FLAGS0_TYPE_STATUS;
	sbale[2].flags |= SBAL_FLAGS_LAST_ENTRY;
	req->sbale_curr = 2;

	sr_buf = mempool_alloc(adapter->pool.data_status_read, GFP_ATOMIC);
	if (!sr_buf) {
		retval = -ENOMEM;
		goto failed_buf;
	}
	memset(sr_buf, 0, sizeof(*sr_buf));
	req->data = sr_buf;
	sbale = zfcp_qdio_sbale_curr(req);
	sbale->addr = (void *) sr_buf;
	sbale->length = sizeof(*sr_buf);
	page = mempool_alloc(adapter->pool.sr_data, GFP_ATOMIC);
	if (!page) {
		retval = -ENOMEM;
		goto failed_buf;
	}
	sr_buf = page_address(page);
	memset(sr_buf, 0, sizeof(*sr_buf));
	req->data = sr_buf;

	zfcp_qdio_fill_next(qdio, &req->qdio_req, sr_buf, sizeof(*sr_buf));
	zfcp_qdio_set_sbale_last(qdio, &req->qdio_req);

	retval = zfcp_fsf_req_send(req);
	if (retval)
		goto failed_req_send;

	goto out;

failed_req_send:
	mempool_free(sr_buf, adapter->pool.data_status_read);
failed_buf:
	zfcp_fsf_req_free(req);
	zfcp_hba_dbf_event_fsf_unsol("fail", adapter, NULL);
out:
	spin_unlock_bh(&adapter->req_q.lock);
	req->data = NULL;
	mempool_free(virt_to_page(sr_buf), adapter->pool.sr_data);
failed_buf:
	zfcp_dbf_hba_fsf_uss("fssr__1", req);
	zfcp_fsf_req_free(req);
out:
	spin_unlock_irq(&qdio->req_q_lock);
	return retval;
}

static void zfcp_fsf_abort_fcp_command_handler(struct zfcp_fsf_req *req)
{
	struct zfcp_unit *unit = req->data;
	struct scsi_device *sdev = req->data;
	struct zfcp_scsi_dev *zfcp_sdev;
	union fsf_status_qual *fsq = &req->qtcb->header.fsf_status_qual;

	if (req->status & ZFCP_STATUS_FSFREQ_ERROR)
		return;

	switch (req->qtcb->header.fsf_status) {
	case FSF_PORT_HANDLE_NOT_VALID:
		if (fsq->word[0] == fsq->word[1]) {
			zfcp_erp_adapter_reopen(unit->port->adapter, 0, 104,
						req);
	zfcp_sdev = sdev_to_zfcp(sdev);

	switch (req->qtcb->header.fsf_status) {
	case FSF_PORT_HANDLE_NOT_VALID:
		if (fsq->word[0] == fsq->word[1]) {
			zfcp_erp_adapter_reopen(zfcp_sdev->port->adapter, 0,
						"fsafch1");
			req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		}
		break;
	case FSF_LUN_HANDLE_NOT_VALID:
		if (fsq->word[0] == fsq->word[1]) {
			zfcp_erp_port_reopen(unit->port, 0, 105, req);
			zfcp_erp_port_reopen(zfcp_sdev->port, 0, "fsafch2");
			req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		}
		break;
	case FSF_FCP_COMMAND_DOES_NOT_EXIST:
		req->status |= ZFCP_STATUS_FSFREQ_ABORTNOTNEEDED;
		break;
	case FSF_PORT_BOXED:
		zfcp_erp_port_boxed(unit->port, 47, req);
		req->status |= ZFCP_STATUS_FSFREQ_ERROR |
			       ZFCP_STATUS_FSFREQ_RETRY;
		break;
	case FSF_LUN_BOXED:
		zfcp_erp_unit_boxed(unit, 48, req);
		req->status |= ZFCP_STATUS_FSFREQ_ERROR |
			       ZFCP_STATUS_FSFREQ_RETRY;
		zfcp_erp_set_port_status(zfcp_sdev->port,
					 ZFCP_STATUS_COMMON_ACCESS_BOXED);
		zfcp_erp_port_reopen(zfcp_sdev->port,
				     ZFCP_STATUS_COMMON_ERP_FAILED, "fsafch3");
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_LUN_BOXED:
		zfcp_erp_set_lun_status(sdev, ZFCP_STATUS_COMMON_ACCESS_BOXED);
		zfcp_erp_lun_reopen(sdev, ZFCP_STATUS_COMMON_ERP_FAILED,
				    "fsafch4");
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
                break;
	case FSF_ADAPTER_STATUS_AVAILABLE:
		switch (fsq->word[0]) {
		case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
			zfcp_test_link(unit->port);
			zfcp_fc_test_link(zfcp_sdev->port);
			/* fall through */
		case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
			req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		}
		break;
	case FSF_GOOD:
		req->status |= ZFCP_STATUS_FSFREQ_ABORTSUCCEEDED;
		break;
	}
}

/**
 * zfcp_fsf_abort_fcp_command - abort running SCSI command
 * @old_req_id: unsigned long
 * @adapter: pointer to struct zfcp_adapter
 * @unit: pointer to struct zfcp_unit
 * @req_flags: integer specifying the request flags
 * Returns: pointer to struct zfcp_fsf_req
 *
 * FIXME(design): should be watched by a timeout !!!
 */

struct zfcp_fsf_req *zfcp_fsf_abort_fcp_command(unsigned long old_req_id,
						struct zfcp_adapter *adapter,
						struct zfcp_unit *unit,
						int req_flags)
{
	volatile struct qdio_buffer_element *sbale;
	struct zfcp_fsf_req *req = NULL;

	spin_lock(&adapter->req_q.lock);
	if (!atomic_read(&adapter->req_q.count))
		goto out;
	req = zfcp_fsf_req_create(adapter, FSF_QTCB_ABORT_FCP_CMND,
				  req_flags, adapter->pool.fsf_req_abort);
	if (IS_ERR(req))
		goto out;

	if (unlikely(!(atomic_read(&unit->status) &
		       ZFCP_STATUS_COMMON_UNBLOCKED)))
		goto out_error_free;

	sbale = zfcp_qdio_sbale_req(req);
	sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
	sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	req->data = unit;
	req->handler = zfcp_fsf_abort_fcp_command_handler;
	req->qtcb->header.lun_handle = unit->handle;
	req->qtcb->header.port_handle = unit->port->handle;
 * zfcp_fsf_abort_fcp_cmnd - abort running SCSI command
 * @scmnd: The SCSI command to abort
 * Returns: pointer to struct zfcp_fsf_req
 */

struct zfcp_fsf_req *zfcp_fsf_abort_fcp_cmnd(struct scsi_cmnd *scmnd)
{
	struct zfcp_fsf_req *req = NULL;
	struct scsi_device *sdev = scmnd->device;
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(sdev);
	struct zfcp_qdio *qdio = zfcp_sdev->port->adapter->qdio;
	unsigned long old_req_id = (unsigned long) scmnd->host_scribble;

	spin_lock_irq(&qdio->req_q_lock);
	if (zfcp_qdio_sbal_get(qdio))
		goto out;
	req = zfcp_fsf_req_create(qdio, FSF_QTCB_ABORT_FCP_CMND,
				  SBAL_SFLAGS0_TYPE_READ,
				  qdio->adapter->pool.scsi_abort);
	if (IS_ERR(req)) {
		req = NULL;
		goto out;
	}

	if (unlikely(!(atomic_read(&zfcp_sdev->status) &
		       ZFCP_STATUS_COMMON_UNBLOCKED)))
		goto out_error_free;

	zfcp_qdio_set_sbale_last(qdio, &req->qdio_req);

	req->data = sdev;
	req->handler = zfcp_fsf_abort_fcp_command_handler;
	req->qtcb->header.lun_handle = zfcp_sdev->lun_handle;
	req->qtcb->header.port_handle = zfcp_sdev->port->handle;
	req->qtcb->bottom.support.req_handle = (u64) old_req_id;

	zfcp_fsf_start_timer(req, ZFCP_SCSI_ER_TIMEOUT);
	if (!zfcp_fsf_req_send(req))
		goto out;

out_error_free:
	zfcp_fsf_req_free(req);
	req = NULL;
out:
	spin_unlock(&adapter->req_q.lock);
	spin_unlock_irq(&qdio->req_q_lock);
	return req;
}

static void zfcp_fsf_send_ct_handler(struct zfcp_fsf_req *req)
{
	struct zfcp_adapter *adapter = req->adapter;
	struct zfcp_send_ct *send_ct = req->data;
	struct zfcp_port *port = send_ct->port;
	struct fsf_qtcb_header *header = &req->qtcb->header;

	send_ct->status = -EINVAL;
	struct zfcp_fsf_ct_els *ct = req->data;
	struct fsf_qtcb_header *header = &req->qtcb->header;

	ct->status = -EINVAL;

	if (req->status & ZFCP_STATUS_FSFREQ_ERROR)
		goto skip_fsfstatus;

	switch (header->fsf_status) {
        case FSF_GOOD:
		zfcp_san_dbf_event_ct_response(req);
		send_ct->status = 0;
		zfcp_dbf_san_res("fsscth2", req);
		ct->status = 0;
		zfcp_dbf_san_res("fsscth2", req);
		break;
        case FSF_SERVICE_CLASS_NOT_SUPPORTED:
		zfcp_fsf_class_not_supp(req);
		break;
        case FSF_ADAPTER_STATUS_AVAILABLE:
                switch (header->fsf_status_qual.word[0]){
                case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
			zfcp_test_link(port);
                case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
			req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
                }
                break;
	case FSF_ACCESS_DENIED:
		zfcp_fsf_access_denied_port(req, port);
		break;
        case FSF_PORT_BOXED:
		zfcp_erp_port_boxed(port, 49, req);
		req->status |= ZFCP_STATUS_FSFREQ_ERROR |
			       ZFCP_STATUS_FSFREQ_RETRY;
		break;
	case FSF_PORT_HANDLE_NOT_VALID:
		zfcp_erp_adapter_reopen(adapter, 0, 106, req);
        case FSF_PORT_BOXED:
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_PORT_HANDLE_NOT_VALID:
		zfcp_erp_adapter_reopen(adapter, 0, "fsscth1");
		/* fall through */
	case FSF_GENERIC_COMMAND_REJECTED:
	case FSF_PAYLOAD_SIZE_MISMATCH:
	case FSF_REQUEST_SIZE_TOO_LARGE:
	case FSF_RESPONSE_SIZE_TOO_LARGE:
	case FSF_SBAL_MISMATCH:
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	}

skip_fsfstatus:
	if (send_ct->handler)
		send_ct->handler(send_ct->handler_data);
}

static int zfcp_fsf_setup_sbals(struct zfcp_fsf_req *req,
				struct scatterlist *sg_req,
				struct scatterlist *sg_resp, int max_sbals)
{
	int bytes;

	bytes = zfcp_qdio_sbals_from_sg(req, SBAL_FLAGS0_TYPE_WRITE_READ,
					sg_req, max_sbals);
	if (bytes <= 0)
		return -ENOMEM;
	req->qtcb->bottom.support.req_buf_length = bytes;
	req->sbale_curr = ZFCP_LAST_SBALE_PER_SBAL;

	bytes = zfcp_qdio_sbals_from_sg(req, SBAL_FLAGS0_TYPE_WRITE_READ,
					sg_resp, max_sbals);
	if (bytes <= 0)
		return -ENOMEM;
	req->qtcb->bottom.support.resp_buf_length = bytes;
	if (ct->handler)
		ct->handler(ct->handler_data);
}

static void zfcp_fsf_setup_ct_els_unchained(struct zfcp_qdio *qdio,
					    struct zfcp_qdio_req *q_req,
					    struct scatterlist *sg_req,
					    struct scatterlist *sg_resp)
{
	zfcp_qdio_fill_next(qdio, q_req, sg_virt(sg_req), sg_req->length);
	zfcp_qdio_fill_next(qdio, q_req, sg_virt(sg_resp), sg_resp->length);
	zfcp_qdio_set_sbale_last(qdio, q_req);
}

static int zfcp_fsf_setup_ct_els_sbals(struct zfcp_fsf_req *req,
				       struct scatterlist *sg_req,
				       struct scatterlist *sg_resp)
{
	struct zfcp_adapter *adapter = req->adapter;
	struct zfcp_qdio *qdio = adapter->qdio;
	struct fsf_qtcb *qtcb = req->qtcb;
	u32 feat = adapter->adapter_features;

	if (zfcp_adapter_multi_buffer_active(adapter)) {
		if (zfcp_qdio_sbals_from_sg(qdio, &req->qdio_req, sg_req))
			return -EIO;
		qtcb->bottom.support.req_buf_length =
			zfcp_qdio_real_bytes(sg_req);
		if (zfcp_qdio_sbals_from_sg(qdio, &req->qdio_req, sg_resp))
			return -EIO;
		qtcb->bottom.support.resp_buf_length =
			zfcp_qdio_real_bytes(sg_resp);

		zfcp_qdio_set_data_div(qdio, &req->qdio_req,
					zfcp_qdio_sbale_count(sg_req));
		zfcp_qdio_set_sbale_last(qdio, &req->qdio_req);
		zfcp_qdio_set_scount(qdio, &req->qdio_req);
		return 0;
	}

	/* use single, unchained SBAL if it can hold the request */
	if (zfcp_qdio_sg_one_sbale(sg_req) && zfcp_qdio_sg_one_sbale(sg_resp)) {
		zfcp_fsf_setup_ct_els_unchained(qdio, &req->qdio_req,
						sg_req, sg_resp);
		return 0;
	}

	if (!(feat & FSF_FEATURE_ELS_CT_CHAINED_SBALS))
		return -EOPNOTSUPP;

	if (zfcp_qdio_sbals_from_sg(qdio, &req->qdio_req, sg_req))
		return -EIO;

	qtcb->bottom.support.req_buf_length = zfcp_qdio_real_bytes(sg_req);

	zfcp_qdio_set_sbale_last(qdio, &req->qdio_req);
	zfcp_qdio_skip_to_last_sbale(qdio, &req->qdio_req);

	if (zfcp_qdio_sbals_from_sg(qdio, &req->qdio_req, sg_resp))
		return -EIO;

	qtcb->bottom.support.resp_buf_length = zfcp_qdio_real_bytes(sg_resp);

	zfcp_qdio_set_sbale_last(qdio, &req->qdio_req);

	return 0;
}

static int zfcp_fsf_setup_ct_els(struct zfcp_fsf_req *req,
				 struct scatterlist *sg_req,
				 struct scatterlist *sg_resp,
				 unsigned int timeout)
{
	int ret;

	ret = zfcp_fsf_setup_ct_els_sbals(req, sg_req, sg_resp);
	if (ret)
		return ret;

	/* common settings for ct/gs and els requests */
	if (timeout > 255)
		timeout = 255; /* max value accepted by hardware */
	req->qtcb->bottom.support.service_class = FSF_CLASS_3;
	req->qtcb->bottom.support.timeout = timeout;
	zfcp_fsf_start_timer(req, (timeout + 10) * HZ);

	return 0;
}

/**
 * zfcp_fsf_send_ct - initiate a Generic Service request (FC-GS)
 * @ct: pointer to struct zfcp_send_ct with data for request
 * @pool: if non-null this mempool is used to allocate struct zfcp_fsf_req
 * @erp_action: if non-null the Generic Service request sent within ERP
 */
int zfcp_fsf_send_ct(struct zfcp_send_ct *ct, mempool_t *pool,
		     struct zfcp_erp_action *erp_action)
{
	struct zfcp_port *port = ct->port;
	struct zfcp_adapter *adapter = port->adapter;
	struct zfcp_fsf_req *req;
	int ret = -EIO;

	spin_lock_bh(&adapter->req_q.lock);
	if (zfcp_fsf_req_sbal_get(adapter))
		goto out;

	req = zfcp_fsf_req_create(adapter, FSF_QTCB_SEND_GENERIC,
				  ZFCP_REQ_AUTO_CLEANUP, pool);
 */
int zfcp_fsf_send_ct(struct zfcp_fc_wka_port *wka_port,
		     struct zfcp_fsf_ct_els *ct, mempool_t *pool,
		     unsigned int timeout)
{
	struct zfcp_qdio *qdio = wka_port->adapter->qdio;
	struct zfcp_fsf_req *req;
	int ret = -EIO;

	spin_lock_irq(&qdio->req_q_lock);
	if (zfcp_qdio_sbal_get(qdio))
		goto out;

	req = zfcp_fsf_req_create(qdio, FSF_QTCB_SEND_GENERIC,
				  SBAL_SFLAGS0_TYPE_WRITE_READ, pool);

	if (IS_ERR(req)) {
		ret = PTR_ERR(req);
		goto out;
	}

	ret = zfcp_fsf_setup_sbals(req, ct->req, ct->resp,
				   FSF_MAX_SBALS_PER_REQ);
	req->status |= ZFCP_STATUS_FSFREQ_CLEANUP;
	ret = zfcp_fsf_setup_ct_els(req, ct->req, ct->resp, timeout);
	if (ret)
		goto failed_send;

	req->handler = zfcp_fsf_send_ct_handler;
	req->qtcb->header.port_handle = port->handle;
	req->qtcb->bottom.support.service_class = FSF_CLASS_3;
	req->qtcb->bottom.support.timeout = ct->timeout;
	req->data = ct;

	zfcp_san_dbf_event_ct_request(req);

	if (erp_action) {
		erp_action->fsf_req = req;
		req->erp_action = erp_action;
		zfcp_fsf_start_erp_timer(req);
	} else
		zfcp_fsf_start_timer(req, ZFCP_FSF_REQUEST_TIMEOUT);
	req->qtcb->header.port_handle = wka_port->handle;
	ct->d_id = wka_port->d_id;
	req->data = ct;

	zfcp_dbf_san_req("fssct_1", req, wka_port->d_id);

	ret = zfcp_fsf_req_send(req);
	if (ret)
		goto failed_send;

	goto out;

failed_send:
	zfcp_fsf_req_free(req);
	if (erp_action)
		erp_action->fsf_req = NULL;
out:
	spin_unlock_bh(&adapter->req_q.lock);
out:
	spin_unlock_irq(&qdio->req_q_lock);
	return ret;
}

static void zfcp_fsf_send_els_handler(struct zfcp_fsf_req *req)
{
	struct zfcp_send_els *send_els = req->data;
	struct zfcp_port *port = send_els->port;
	struct zfcp_fsf_ct_els *send_els = req->data;
	struct fsf_qtcb_header *header = &req->qtcb->header;

	send_els->status = -EINVAL;

	if (req->status & ZFCP_STATUS_FSFREQ_ERROR)
		goto skip_fsfstatus;

	switch (header->fsf_status) {
	case FSF_GOOD:
		zfcp_san_dbf_event_els_response(req);
		zfcp_dbf_san_res("fsselh1", req);
		send_els->status = 0;
		zfcp_dbf_san_res("fsselh1", req);
		break;
	case FSF_SERVICE_CLASS_NOT_SUPPORTED:
		zfcp_fsf_class_not_supp(req);
		break;
	case FSF_ADAPTER_STATUS_AVAILABLE:
		switch (header->fsf_status_qual.word[0]){
		case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
			if (port && (send_els->ls_code != ZFCP_LS_ADISC))
				zfcp_test_link(port);
			/*fall through */
		case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
		case FSF_SQ_RETRY_IF_POSSIBLE:
			req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		}
		break;
	case FSF_ELS_COMMAND_REJECTED:
	case FSF_PAYLOAD_SIZE_MISMATCH:
	case FSF_REQUEST_SIZE_TOO_LARGE:
	case FSF_RESPONSE_SIZE_TOO_LARGE:
		break;
	case FSF_ACCESS_DENIED:
		zfcp_fsf_access_denied_port(req, port);
		break;
	case FSF_SBAL_MISMATCH:
		/* should never occure, avoided in zfcp_fsf_send_els */
	case FSF_SBAL_MISMATCH:
		/* should never occur, avoided in zfcp_fsf_send_els */
		/* fall through */
	default:
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	}
skip_fsfstatus:
	if (send_els->handler)
		send_els->handler(send_els->handler_data);
}

/**
 * zfcp_fsf_send_els - initiate an ELS command (FC-FS)
 * @els: pointer to struct zfcp_send_els with data for the command
 */
int zfcp_fsf_send_els(struct zfcp_send_els *els)
{
	struct zfcp_fsf_req *req;
	struct zfcp_adapter *adapter = els->adapter;
	struct fsf_qtcb_bottom_support *bottom;
	int ret = -EIO;

	if (unlikely(!(atomic_read(&els->port->status) &
		       ZFCP_STATUS_COMMON_UNBLOCKED)))
		return -EBUSY;

	spin_lock(&adapter->req_q.lock);
	if (!atomic_read(&adapter->req_q.count))
		goto out;
	req = zfcp_fsf_req_create(adapter, FSF_QTCB_SEND_ELS,
				  ZFCP_REQ_AUTO_CLEANUP, NULL);
int zfcp_fsf_send_els(struct zfcp_adapter *adapter, u32 d_id,
		      struct zfcp_fsf_ct_els *els, unsigned int timeout)
{
	struct zfcp_fsf_req *req;
	struct zfcp_qdio *qdio = adapter->qdio;
	int ret = -EIO;

	spin_lock_irq(&qdio->req_q_lock);
	if (zfcp_qdio_sbal_get(qdio))
		goto out;

	req = zfcp_fsf_req_create(qdio, FSF_QTCB_SEND_ELS,
				  SBAL_SFLAGS0_TYPE_WRITE_READ, NULL);

	if (IS_ERR(req)) {
		ret = PTR_ERR(req);
		goto out;
	}

	ret = zfcp_fsf_setup_sbals(req, els->req, els->resp,
				   FSF_MAX_SBALS_PER_ELS_REQ);
	if (ret)
		goto failed_send;

	bottom = &req->qtcb->bottom.support;
	req->handler = zfcp_fsf_send_els_handler;
	bottom->d_id = els->d_id;
	bottom->service_class = FSF_CLASS_3;
	bottom->timeout = 2 * R_A_TOV;
	req->data = els;

	zfcp_san_dbf_event_els_request(req);

	zfcp_fsf_start_timer(req, ZFCP_FSF_REQUEST_TIMEOUT);
	req->status |= ZFCP_STATUS_FSFREQ_CLEANUP;

	if (!zfcp_adapter_multi_buffer_active(adapter))
		zfcp_qdio_sbal_limit(qdio, &req->qdio_req, 2);

	ret = zfcp_fsf_setup_ct_els(req, els->req, els->resp, timeout);

	if (ret)
		goto failed_send;

	hton24(req->qtcb->bottom.support.d_id, d_id);
	req->handler = zfcp_fsf_send_els_handler;
	els->d_id = d_id;
	req->data = els;

	zfcp_dbf_san_req("fssels1", req, d_id);

	ret = zfcp_fsf_req_send(req);
	if (ret)
		goto failed_send;

	goto out;

failed_send:
	zfcp_fsf_req_free(req);
out:
	spin_unlock(&adapter->req_q.lock);
	spin_unlock_irq(&qdio->req_q_lock);
	return ret;
}

int zfcp_fsf_exchange_config_data(struct zfcp_erp_action *erp_action)
{
	volatile struct qdio_buffer_element *sbale;
	struct zfcp_fsf_req *req;
	struct zfcp_adapter *adapter = erp_action->adapter;
	int retval = -EIO;

	spin_lock_bh(&adapter->req_q.lock);
	if (!atomic_read(&adapter->req_q.count))
		goto out;
	req = zfcp_fsf_req_create(adapter,
				  FSF_QTCB_EXCHANGE_CONFIG_DATA,
				  ZFCP_REQ_AUTO_CLEANUP,
				  adapter->pool.fsf_req_erp);
	struct zfcp_fsf_req *req;
	struct zfcp_qdio *qdio = erp_action->adapter->qdio;
	int retval = -EIO;

	spin_lock_irq(&qdio->req_q_lock);
	if (zfcp_qdio_sbal_get(qdio))
		goto out;

	req = zfcp_fsf_req_create(qdio, FSF_QTCB_EXCHANGE_CONFIG_DATA,
				  SBAL_SFLAGS0_TYPE_READ,
				  qdio->adapter->pool.erp_req);

	if (IS_ERR(req)) {
		retval = PTR_ERR(req);
		goto out;
	}

	sbale = zfcp_qdio_sbale_req(req);
	sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
	sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	req->qtcb->bottom.config.feature_selection =
			FSF_FEATURE_CFDC |
			FSF_FEATURE_LUN_SHARING |
	req->status |= ZFCP_STATUS_FSFREQ_CLEANUP;
	zfcp_qdio_set_sbale_last(qdio, &req->qdio_req);

	req->qtcb->bottom.config.feature_selection =
			FSF_FEATURE_NOTIFICATION_LOST |
			FSF_FEATURE_UPDATE_ALERT;
	req->erp_action = erp_action;
	req->handler = zfcp_fsf_exchange_config_data_handler;
	erp_action->fsf_req = req;
	erp_action->fsf_req_id = req->req_id;

	zfcp_fsf_start_erp_timer(req);
	retval = zfcp_fsf_req_send(req);
	if (retval) {
		zfcp_fsf_req_free(req);
		erp_action->fsf_req = NULL;
	}
out:
	spin_unlock_bh(&adapter->req_q.lock);
	return retval;
}

int zfcp_fsf_exchange_config_data_sync(struct zfcp_adapter *adapter,
				       struct fsf_qtcb_bottom_config *data)
{
	volatile struct qdio_buffer_element *sbale;
	struct zfcp_fsf_req *req = NULL;
	int retval = -EIO;

	spin_lock_bh(&adapter->req_q.lock);
	if (zfcp_fsf_req_sbal_get(adapter))
		goto out;

	req = zfcp_fsf_req_create(adapter, FSF_QTCB_EXCHANGE_CONFIG_DATA,
				  0, NULL);
	if (IS_ERR(req)) {
		retval = PTR_ERR(req);
		goto out;
	}

	sbale = zfcp_qdio_sbale_req(req);
	sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
	sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;
	req->handler = zfcp_fsf_exchange_config_data_handler;

	req->qtcb->bottom.config.feature_selection =
			FSF_FEATURE_CFDC |
			FSF_FEATURE_LUN_SHARING |
		erp_action->fsf_req_id = 0;
	}
out:
	spin_unlock_irq(&qdio->req_q_lock);
	return retval;
}

int zfcp_fsf_exchange_config_data_sync(struct zfcp_qdio *qdio,
				       struct fsf_qtcb_bottom_config *data)
{
	struct zfcp_fsf_req *req = NULL;
	int retval = -EIO;

	spin_lock_irq(&qdio->req_q_lock);
	if (zfcp_qdio_sbal_get(qdio))
		goto out_unlock;

	req = zfcp_fsf_req_create(qdio, FSF_QTCB_EXCHANGE_CONFIG_DATA,
				  SBAL_SFLAGS0_TYPE_READ, NULL);

	if (IS_ERR(req)) {
		retval = PTR_ERR(req);
		goto out_unlock;
	}

	zfcp_qdio_set_sbale_last(qdio, &req->qdio_req);
	req->handler = zfcp_fsf_exchange_config_data_handler;

	req->qtcb->bottom.config.feature_selection =
			FSF_FEATURE_NOTIFICATION_LOST |
			FSF_FEATURE_UPDATE_ALERT;

	if (data)
		req->data = data;

	zfcp_fsf_start_timer(req, ZFCP_FSF_REQUEST_TIMEOUT);
	retval = zfcp_fsf_req_send(req);
out:
	spin_unlock_bh(&adapter->req_q.lock);
	if (!retval)
		wait_event(req->completion_wq,
			   req->status & ZFCP_STATUS_FSFREQ_COMPLETED);

	zfcp_fsf_req_free(req);

	spin_unlock_irq(&qdio->req_q_lock);
	if (!retval)
		wait_for_completion(&req->completion);

	zfcp_fsf_req_free(req);
	return retval;

out_unlock:
	spin_unlock_irq(&qdio->req_q_lock);
	return retval;
}

/**
 * zfcp_fsf_exchange_port_data - request information about local port
 * @erp_action: ERP action for the adapter for which port data is requested
 * Returns: 0 on success, error otherwise
 */
int zfcp_fsf_exchange_port_data(struct zfcp_erp_action *erp_action)
{
	volatile struct qdio_buffer_element *sbale;
	struct zfcp_fsf_req *req;
	struct zfcp_adapter *adapter = erp_action->adapter;
	int retval = -EIO;

	if (!(adapter->adapter_features & FSF_FEATURE_HBAAPI_MANAGEMENT))
		return -EOPNOTSUPP;

	spin_lock_bh(&adapter->req_q.lock);
	if (!atomic_read(&adapter->req_q.count))
		goto out;
	req = zfcp_fsf_req_create(adapter, FSF_QTCB_EXCHANGE_PORT_DATA,
				  ZFCP_REQ_AUTO_CLEANUP,
				  adapter->pool.fsf_req_erp);
	struct zfcp_qdio *qdio = erp_action->adapter->qdio;
	struct zfcp_fsf_req *req;
	int retval = -EIO;

	if (!(qdio->adapter->adapter_features & FSF_FEATURE_HBAAPI_MANAGEMENT))
		return -EOPNOTSUPP;

	spin_lock_irq(&qdio->req_q_lock);
	if (zfcp_qdio_sbal_get(qdio))
		goto out;

	req = zfcp_fsf_req_create(qdio, FSF_QTCB_EXCHANGE_PORT_DATA,
				  SBAL_SFLAGS0_TYPE_READ,
				  qdio->adapter->pool.erp_req);

	if (IS_ERR(req)) {
		retval = PTR_ERR(req);
		goto out;
	}

	sbale = zfcp_qdio_sbale_req(req);
	sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
	sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	req->handler = zfcp_fsf_exchange_port_data_handler;
	req->erp_action = erp_action;
	erp_action->fsf_req = req;
	req->status |= ZFCP_STATUS_FSFREQ_CLEANUP;
	zfcp_qdio_set_sbale_last(qdio, &req->qdio_req);

	req->handler = zfcp_fsf_exchange_port_data_handler;
	req->erp_action = erp_action;
	erp_action->fsf_req_id = req->req_id;

	zfcp_fsf_start_erp_timer(req);
	retval = zfcp_fsf_req_send(req);
	if (retval) {
		zfcp_fsf_req_free(req);
		erp_action->fsf_req = NULL;
	}
out:
	spin_unlock_bh(&adapter->req_q.lock);
		erp_action->fsf_req_id = 0;
	}
out:
	spin_unlock_irq(&qdio->req_q_lock);
	return retval;
}

/**
 * zfcp_fsf_exchange_port_data_sync - request information about local port
 * @adapter: pointer to struct zfcp_adapter
 * @data: pointer to struct fsf_qtcb_bottom_port
 * Returns: 0 on success, error otherwise
 */
int zfcp_fsf_exchange_port_data_sync(struct zfcp_adapter *adapter,
				     struct fsf_qtcb_bottom_port *data)
{
	volatile struct qdio_buffer_element *sbale;
	struct zfcp_fsf_req *req = NULL;
	int retval = -EIO;

	if (!(adapter->adapter_features & FSF_FEATURE_HBAAPI_MANAGEMENT))
		return -EOPNOTSUPP;

	spin_lock_bh(&adapter->req_q.lock);
	if (!atomic_read(&adapter->req_q.count))
		goto out;

	req = zfcp_fsf_req_create(adapter, FSF_QTCB_EXCHANGE_PORT_DATA, 0,
				  NULL);
	if (IS_ERR(req)) {
		retval = PTR_ERR(req);
		goto out;
 * @qdio: pointer to struct zfcp_qdio
 * @data: pointer to struct fsf_qtcb_bottom_port
 * Returns: 0 on success, error otherwise
 */
int zfcp_fsf_exchange_port_data_sync(struct zfcp_qdio *qdio,
				     struct fsf_qtcb_bottom_port *data)
{
	struct zfcp_fsf_req *req = NULL;
	int retval = -EIO;

	if (!(qdio->adapter->adapter_features & FSF_FEATURE_HBAAPI_MANAGEMENT))
		return -EOPNOTSUPP;

	spin_lock_irq(&qdio->req_q_lock);
	if (zfcp_qdio_sbal_get(qdio))
		goto out_unlock;

	req = zfcp_fsf_req_create(qdio, FSF_QTCB_EXCHANGE_PORT_DATA,
				  SBAL_SFLAGS0_TYPE_READ, NULL);

	if (IS_ERR(req)) {
		retval = PTR_ERR(req);
		goto out_unlock;
	}

	if (data)
		req->data = data;

	sbale = zfcp_qdio_sbale_req(req);
	sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
	sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;
	zfcp_qdio_set_sbale_last(qdio, &req->qdio_req);

	req->handler = zfcp_fsf_exchange_port_data_handler;
	zfcp_fsf_start_timer(req, ZFCP_FSF_REQUEST_TIMEOUT);
	retval = zfcp_fsf_req_send(req);
out:
	spin_unlock_bh(&adapter->req_q.lock);
	if (!retval)
		wait_event(req->completion_wq,
			   req->status & ZFCP_STATUS_FSFREQ_COMPLETED);
	zfcp_fsf_req_free(req);

	return retval;
	spin_unlock_irq(&qdio->req_q_lock);

	if (!retval)
		wait_for_completion(&req->completion);

	zfcp_fsf_req_free(req);

	return retval;

out_unlock:
	spin_unlock_irq(&qdio->req_q_lock);
	return retval;
}

static void zfcp_fsf_open_port_handler(struct zfcp_fsf_req *req)
{
	struct zfcp_port *port = req->data;
	struct fsf_qtcb_header *header = &req->qtcb->header;
	struct fsf_plogi *plogi;

	if (req->status & ZFCP_STATUS_FSFREQ_ERROR)
		goto skip_fsfstatus;
	struct fc_els_flogi *plogi;

	if (req->status & ZFCP_STATUS_FSFREQ_ERROR)
		goto out;

	switch (header->fsf_status) {
	case FSF_PORT_ALREADY_OPEN:
		break;
	case FSF_ACCESS_DENIED:
		zfcp_fsf_access_denied_port(req, port);
		break;
	case FSF_MAXIMUM_NUMBER_OF_PORTS_EXCEEDED:
		dev_warn(&req->adapter->ccw_device->dev,
			 "The adapter is out of resources. The remote port "
			 "0x%016Lx could not be opened, disabling it.\n",
			 port->wwpn);
		zfcp_erp_port_failed(port, 31, req);
	case FSF_MAXIMUM_NUMBER_OF_PORTS_EXCEEDED:
		dev_warn(&req->adapter->ccw_device->dev,
			 "Not enough FCP adapter resources to open "
			 "remote port 0x%016Lx\n",
			 (unsigned long long)port->wwpn);
		zfcp_erp_set_port_status(port,
					 ZFCP_STATUS_COMMON_ERP_FAILED);
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_ADAPTER_STATUS_AVAILABLE:
		switch (header->fsf_status_qual.word[0]) {
		case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
		case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
			req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		case FSF_SQ_NO_RETRY_POSSIBLE:
			dev_warn(&req->adapter->ccw_device->dev,
				 "The remote port 0x%016Lx could not be "
				 "opened. Disabling it.\n", port->wwpn);
			zfcp_erp_port_failed(port, 32, req);
		case FSF_SQ_NO_RETRY_POSSIBLE:
			req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		}
		break;
	case FSF_GOOD:
		port->handle = header->port_handle;
		atomic_set_mask(ZFCP_STATUS_COMMON_OPEN |
				ZFCP_STATUS_PORT_PHYS_OPEN, &port->status);
		atomic_clear_mask(ZFCP_STATUS_COMMON_ACCESS_DENIED |
		                  ZFCP_STATUS_COMMON_ACCESS_BOXED,
		atomic_or(ZFCP_STATUS_COMMON_OPEN |
				ZFCP_STATUS_PORT_PHYS_OPEN, &port->status);
		atomic_andnot(ZFCP_STATUS_COMMON_ACCESS_BOXED,
		                  &port->status);
		/* check whether D_ID has changed during open */
		/*
		 * FIXME: This check is not airtight, as the FCP channel does
		 * not monitor closures of target port connections caused on
		 * the remote side. Thus, they might miss out on invalidating
		 * locally cached WWPNs (and other N_Port parameters) of gone
		 * target ports. So, our heroic attempt to make things safe
		 * could be undermined by 'open port' response data tagged with
		 * obsolete WWPNs. Another reason to monitor potential
		 * connection closures ourself at least (by interpreting
		 * incoming ELS' and unsolicited status). It just crosses my
		 * mind that one should be able to cross-check by means of
		 * another GID_PN straight after a port has been opened.
		 * Alternately, an ADISC/PDISC ELS should suffice, as well.
		 */
		if (atomic_read(&port->status) & ZFCP_STATUS_PORT_NO_WWPN)
			break;

		plogi = (struct fsf_plogi *) req->qtcb->bottom.support.els;
		if (req->qtcb->bottom.support.els1_length >= sizeof(*plogi)) {
			if (plogi->serv_param.wwpn != port->wwpn)
				atomic_clear_mask(ZFCP_STATUS_PORT_DID_DID,
						  &port->status);
			else {
				port->wwnn = plogi->serv_param.wwnn;
				zfcp_fc_plogi_evaluate(port, plogi);
			}
		}
		plogi = (struct fc_els_flogi *) req->qtcb->bottom.support.els;
		if (req->qtcb->bottom.support.els1_length >=
		    FSF_PLOGI_MIN_LEN)
				zfcp_fc_plogi_evaluate(port, plogi);
		break;
	case FSF_UNKNOWN_OP_SUBTYPE:
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	}

skip_fsfstatus:
	atomic_clear_mask(ZFCP_STATUS_COMMON_OPENING, &port->status);
out:
	put_device(&port->dev);
}

/**
 * zfcp_fsf_open_port - create and send open port request
 * @erp_action: pointer to struct zfcp_erp_action
 * Returns: 0 on success, error otherwise
 */
int zfcp_fsf_open_port(struct zfcp_erp_action *erp_action)
{
	volatile struct qdio_buffer_element *sbale;
	struct zfcp_adapter *adapter = erp_action->adapter;
	struct zfcp_fsf_req *req;
	int retval = -EIO;

	spin_lock_bh(&adapter->req_q.lock);
	if (zfcp_fsf_req_sbal_get(adapter))
		goto out;

	req = zfcp_fsf_req_create(adapter,
				  FSF_QTCB_OPEN_PORT_WITH_DID,
				  ZFCP_REQ_AUTO_CLEANUP,
				  adapter->pool.fsf_req_erp);
	struct zfcp_qdio *qdio = erp_action->adapter->qdio;
	struct zfcp_port *port = erp_action->port;
	struct zfcp_fsf_req *req;
	int retval = -EIO;

	spin_lock_irq(&qdio->req_q_lock);
	if (zfcp_qdio_sbal_get(qdio))
		goto out;

	req = zfcp_fsf_req_create(qdio, FSF_QTCB_OPEN_PORT_WITH_DID,
				  SBAL_SFLAGS0_TYPE_READ,
				  qdio->adapter->pool.erp_req);

	if (IS_ERR(req)) {
		retval = PTR_ERR(req);
		goto out;
	}

	sbale = zfcp_qdio_sbale_req(req);
        sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
        sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	req->handler = zfcp_fsf_open_port_handler;
	req->qtcb->bottom.support.d_id = erp_action->port->d_id;
	req->data = erp_action->port;
	req->erp_action = erp_action;
	erp_action->fsf_req = req;
	atomic_set_mask(ZFCP_STATUS_COMMON_OPENING, &erp_action->port->status);
	req->status |= ZFCP_STATUS_FSFREQ_CLEANUP;
	zfcp_qdio_set_sbale_last(qdio, &req->qdio_req);

	req->handler = zfcp_fsf_open_port_handler;
	hton24(req->qtcb->bottom.support.d_id, port->d_id);
	req->data = port;
	req->erp_action = erp_action;
	erp_action->fsf_req_id = req->req_id;
	get_device(&port->dev);

	zfcp_fsf_start_erp_timer(req);
	retval = zfcp_fsf_req_send(req);
	if (retval) {
		zfcp_fsf_req_free(req);
		erp_action->fsf_req = NULL;
	}
out:
	spin_unlock_bh(&adapter->req_q.lock);
		erp_action->fsf_req_id = 0;
		put_device(&port->dev);
	}
out:
	spin_unlock_irq(&qdio->req_q_lock);
	return retval;
}

static void zfcp_fsf_close_port_handler(struct zfcp_fsf_req *req)
{
	struct zfcp_port *port = req->data;

	if (req->status & ZFCP_STATUS_FSFREQ_ERROR)
		goto skip_fsfstatus;

	switch (req->qtcb->header.fsf_status) {
	case FSF_PORT_HANDLE_NOT_VALID:
		zfcp_erp_adapter_reopen(port->adapter, 0, 107, req);
		return;

	switch (req->qtcb->header.fsf_status) {
	case FSF_PORT_HANDLE_NOT_VALID:
		zfcp_erp_adapter_reopen(port->adapter, 0, "fscph_1");
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_ADAPTER_STATUS_AVAILABLE:
		break;
	case FSF_GOOD:
		zfcp_erp_modify_port_status(port, 33, req,
					    ZFCP_STATUS_COMMON_OPEN,
					    ZFCP_CLEAR);
		break;
	}

skip_fsfstatus:
	atomic_clear_mask(ZFCP_STATUS_COMMON_CLOSING, &port->status);
		zfcp_erp_clear_port_status(port, ZFCP_STATUS_COMMON_OPEN);
		break;
	}
}

/**
 * zfcp_fsf_close_port - create and send close port request
 * @erp_action: pointer to struct zfcp_erp_action
 * Returns: 0 on success, error otherwise
 */
int zfcp_fsf_close_port(struct zfcp_erp_action *erp_action)
{
	volatile struct qdio_buffer_element *sbale;
	struct zfcp_adapter *adapter = erp_action->adapter;
	struct zfcp_fsf_req *req;
	int retval = -EIO;

	spin_lock_bh(&adapter->req_q.lock);
	if (zfcp_fsf_req_sbal_get(adapter))
		goto out;

	req = zfcp_fsf_req_create(adapter, FSF_QTCB_CLOSE_PORT,
				  ZFCP_REQ_AUTO_CLEANUP,
				  adapter->pool.fsf_req_erp);
	struct zfcp_qdio *qdio = erp_action->adapter->qdio;
	struct zfcp_fsf_req *req;
	int retval = -EIO;

	spin_lock_irq(&qdio->req_q_lock);
	if (zfcp_qdio_sbal_get(qdio))
		goto out;

	req = zfcp_fsf_req_create(qdio, FSF_QTCB_CLOSE_PORT,
				  SBAL_SFLAGS0_TYPE_READ,
				  qdio->adapter->pool.erp_req);

	if (IS_ERR(req)) {
		retval = PTR_ERR(req);
		goto out;
	}

	sbale = zfcp_qdio_sbale_req(req);
	sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
	sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;
	req->status |= ZFCP_STATUS_FSFREQ_CLEANUP;
	zfcp_qdio_set_sbale_last(qdio, &req->qdio_req);

	req->handler = zfcp_fsf_close_port_handler;
	req->data = erp_action->port;
	req->erp_action = erp_action;
	req->qtcb->header.port_handle = erp_action->port->handle;
	erp_action->fsf_req = req;
	atomic_set_mask(ZFCP_STATUS_COMMON_CLOSING, &erp_action->port->status);
	erp_action->fsf_req_id = req->req_id;

	zfcp_fsf_start_erp_timer(req);
	retval = zfcp_fsf_req_send(req);
	if (retval) {
		zfcp_fsf_req_free(req);
		erp_action->fsf_req = NULL;
	}
out:
	spin_unlock_bh(&adapter->req_q.lock);
		erp_action->fsf_req_id = 0;
	}
out:
	spin_unlock_irq(&qdio->req_q_lock);
	return retval;
}

static void zfcp_fsf_open_wka_port_handler(struct zfcp_fsf_req *req)
{
	struct zfcp_fc_wka_port *wka_port = req->data;
	struct fsf_qtcb_header *header = &req->qtcb->header;

	if (req->status & ZFCP_STATUS_FSFREQ_ERROR) {
		wka_port->status = ZFCP_FC_WKA_PORT_OFFLINE;
		goto out;
	}

	switch (header->fsf_status) {
	case FSF_MAXIMUM_NUMBER_OF_PORTS_EXCEEDED:
		dev_warn(&req->adapter->ccw_device->dev,
			 "Opening WKA port 0x%x failed\n", wka_port->d_id);
		/* fall through */
	case FSF_ADAPTER_STATUS_AVAILABLE:
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		wka_port->status = ZFCP_FC_WKA_PORT_OFFLINE;
		break;
	case FSF_GOOD:
		wka_port->handle = header->port_handle;
		/* fall through */
	case FSF_PORT_ALREADY_OPEN:
		wka_port->status = ZFCP_FC_WKA_PORT_ONLINE;
	}
out:
	wake_up(&wka_port->opened);
}

/**
 * zfcp_fsf_open_wka_port - create and send open wka-port request
 * @wka_port: pointer to struct zfcp_fc_wka_port
 * Returns: 0 on success, error otherwise
 */
int zfcp_fsf_open_wka_port(struct zfcp_fc_wka_port *wka_port)
{
	struct zfcp_qdio *qdio = wka_port->adapter->qdio;
	struct zfcp_fsf_req *req;
	unsigned long req_id = 0;
	int retval = -EIO;

	spin_lock_irq(&qdio->req_q_lock);
	if (zfcp_qdio_sbal_get(qdio))
		goto out;

	req = zfcp_fsf_req_create(qdio, FSF_QTCB_OPEN_PORT_WITH_DID,
				  SBAL_SFLAGS0_TYPE_READ,
				  qdio->adapter->pool.erp_req);

	if (IS_ERR(req)) {
		retval = PTR_ERR(req);
		goto out;
	}

	req->status |= ZFCP_STATUS_FSFREQ_CLEANUP;
	zfcp_qdio_set_sbale_last(qdio, &req->qdio_req);

	req->handler = zfcp_fsf_open_wka_port_handler;
	hton24(req->qtcb->bottom.support.d_id, wka_port->d_id);
	req->data = wka_port;

	req_id = req->req_id;

	zfcp_fsf_start_timer(req, ZFCP_FSF_REQUEST_TIMEOUT);
	retval = zfcp_fsf_req_send(req);
	if (retval)
		zfcp_fsf_req_free(req);
out:
	spin_unlock_irq(&qdio->req_q_lock);
	if (!retval)
		zfcp_dbf_rec_run_wka("fsowp_1", wka_port, req_id);
	return retval;
}

static void zfcp_fsf_close_wka_port_handler(struct zfcp_fsf_req *req)
{
	struct zfcp_fc_wka_port *wka_port = req->data;

	if (req->qtcb->header.fsf_status == FSF_PORT_HANDLE_NOT_VALID) {
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		zfcp_erp_adapter_reopen(wka_port->adapter, 0, "fscwph1");
	}

	wka_port->status = ZFCP_FC_WKA_PORT_OFFLINE;
	wake_up(&wka_port->closed);
}

/**
 * zfcp_fsf_close_wka_port - create and send close wka port request
 * @wka_port: WKA port to open
 * Returns: 0 on success, error otherwise
 */
int zfcp_fsf_close_wka_port(struct zfcp_fc_wka_port *wka_port)
{
	struct zfcp_qdio *qdio = wka_port->adapter->qdio;
	struct zfcp_fsf_req *req;
	unsigned long req_id = 0;
	int retval = -EIO;

	spin_lock_irq(&qdio->req_q_lock);
	if (zfcp_qdio_sbal_get(qdio))
		goto out;

	req = zfcp_fsf_req_create(qdio, FSF_QTCB_CLOSE_PORT,
				  SBAL_SFLAGS0_TYPE_READ,
				  qdio->adapter->pool.erp_req);

	if (IS_ERR(req)) {
		retval = PTR_ERR(req);
		goto out;
	}

	req->status |= ZFCP_STATUS_FSFREQ_CLEANUP;
	zfcp_qdio_set_sbale_last(qdio, &req->qdio_req);

	req->handler = zfcp_fsf_close_wka_port_handler;
	req->data = wka_port;
	req->qtcb->header.port_handle = wka_port->handle;

	req_id = req->req_id;

	zfcp_fsf_start_timer(req, ZFCP_FSF_REQUEST_TIMEOUT);
	retval = zfcp_fsf_req_send(req);
	if (retval)
		zfcp_fsf_req_free(req);
out:
	spin_unlock_irq(&qdio->req_q_lock);
	if (!retval)
		zfcp_dbf_rec_run_wka("fscwp_1", wka_port, req_id);
	return retval;
}

static void zfcp_fsf_close_physical_port_handler(struct zfcp_fsf_req *req)
{
	struct zfcp_port *port = req->data;
	struct fsf_qtcb_header *header = &req->qtcb->header;
	struct zfcp_unit *unit;

	if (req->status & ZFCP_STATUS_FSFREQ_ERROR)
		goto skip_fsfstatus;

	switch (header->fsf_status) {
	case FSF_PORT_HANDLE_NOT_VALID:
		zfcp_erp_adapter_reopen(port->adapter, 0, 108, req);
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_ACCESS_DENIED:
		zfcp_fsf_access_denied_port(req, port);
		break;
	case FSF_PORT_BOXED:
		zfcp_erp_port_boxed(port, 50, req);
		req->status |= ZFCP_STATUS_FSFREQ_ERROR |
			       ZFCP_STATUS_FSFREQ_RETRY;
		/* can't use generic zfcp_erp_modify_port_status because
		 * ZFCP_STATUS_COMMON_OPEN must not be reset for the port */
		atomic_clear_mask(ZFCP_STATUS_PORT_PHYS_OPEN, &port->status);
		list_for_each_entry(unit, &port->unit_list_head, list)
			atomic_clear_mask(ZFCP_STATUS_COMMON_OPEN,
					  &unit->status);
	struct scsi_device *sdev;

	if (req->status & ZFCP_STATUS_FSFREQ_ERROR)
		return;

	switch (header->fsf_status) {
	case FSF_PORT_HANDLE_NOT_VALID:
		zfcp_erp_adapter_reopen(port->adapter, 0, "fscpph1");
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_PORT_BOXED:
		/* can't use generic zfcp_erp_modify_port_status because
		 * ZFCP_STATUS_COMMON_OPEN must not be reset for the port */
		atomic_andnot(ZFCP_STATUS_PORT_PHYS_OPEN, &port->status);
		shost_for_each_device(sdev, port->adapter->scsi_host)
			if (sdev_to_zfcp(sdev)->port == port)
				atomic_andnot(ZFCP_STATUS_COMMON_OPEN,
						  &sdev_to_zfcp(sdev)->status);
		zfcp_erp_set_port_status(port, ZFCP_STATUS_COMMON_ACCESS_BOXED);
		zfcp_erp_port_reopen(port, ZFCP_STATUS_COMMON_ERP_FAILED,
				     "fscpph2");
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_ADAPTER_STATUS_AVAILABLE:
		switch (header->fsf_status_qual.word[0]) {
		case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
			/* fall through */
		case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
			req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		}
		break;
	case FSF_GOOD:
		/* can't use generic zfcp_erp_modify_port_status because
		 * ZFCP_STATUS_COMMON_OPEN must not be reset for the port
		 */
		atomic_clear_mask(ZFCP_STATUS_PORT_PHYS_OPEN, &port->status);
		list_for_each_entry(unit, &port->unit_list_head, list)
			atomic_clear_mask(ZFCP_STATUS_COMMON_OPEN,
					  &unit->status);
		break;
	}
skip_fsfstatus:
	atomic_clear_mask(ZFCP_STATUS_PORT_PHYS_CLOSING, &port->status);
		atomic_andnot(ZFCP_STATUS_PORT_PHYS_OPEN, &port->status);
		shost_for_each_device(sdev, port->adapter->scsi_host)
			if (sdev_to_zfcp(sdev)->port == port)
				atomic_andnot(ZFCP_STATUS_COMMON_OPEN,
						  &sdev_to_zfcp(sdev)->status);
		break;
	}
}

/**
 * zfcp_fsf_close_physical_port - close physical port
 * @erp_action: pointer to struct zfcp_erp_action
 * Returns: 0 on success
 */
int zfcp_fsf_close_physical_port(struct zfcp_erp_action *erp_action)
{
	volatile struct qdio_buffer_element *sbale;
	struct zfcp_adapter *adapter = erp_action->adapter;
	struct zfcp_fsf_req *req;
	int retval = -EIO;

	spin_lock_bh(&adapter->req_q.lock);
	if (zfcp_fsf_req_sbal_get(adapter))
		goto out;

	req = zfcp_fsf_req_create(adapter, FSF_QTCB_CLOSE_PHYSICAL_PORT,
				  ZFCP_REQ_AUTO_CLEANUP,
				  adapter->pool.fsf_req_erp);
	struct zfcp_qdio *qdio = erp_action->adapter->qdio;
	struct zfcp_fsf_req *req;
	int retval = -EIO;

	spin_lock_irq(&qdio->req_q_lock);
	if (zfcp_qdio_sbal_get(qdio))
		goto out;

	req = zfcp_fsf_req_create(qdio, FSF_QTCB_CLOSE_PHYSICAL_PORT,
				  SBAL_SFLAGS0_TYPE_READ,
				  qdio->adapter->pool.erp_req);

	if (IS_ERR(req)) {
		retval = PTR_ERR(req);
		goto out;
	}

	sbale = zfcp_qdio_sbale_req(req);
	sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
	sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;
	req->status |= ZFCP_STATUS_FSFREQ_CLEANUP;
	zfcp_qdio_set_sbale_last(qdio, &req->qdio_req);

	req->data = erp_action->port;
	req->qtcb->header.port_handle = erp_action->port->handle;
	req->erp_action = erp_action;
	req->handler = zfcp_fsf_close_physical_port_handler;
	erp_action->fsf_req = req;
	atomic_set_mask(ZFCP_STATUS_PORT_PHYS_CLOSING,
			&erp_action->port->status);
	erp_action->fsf_req_id = req->req_id;

	zfcp_fsf_start_erp_timer(req);
	retval = zfcp_fsf_req_send(req);
	if (retval) {
		zfcp_fsf_req_free(req);
		erp_action->fsf_req = NULL;
	}
out:
	spin_unlock_bh(&adapter->req_q.lock);
	return retval;
}

static void zfcp_fsf_open_unit_handler(struct zfcp_fsf_req *req)
{
	struct zfcp_adapter *adapter = req->adapter;
	struct zfcp_unit *unit = req->data;
	struct fsf_qtcb_header *header = &req->qtcb->header;
	struct fsf_qtcb_bottom_support *bottom = &req->qtcb->bottom.support;
	struct fsf_queue_designator *queue_designator =
				&header->fsf_status_qual.fsf_queue_designator;
	int exclusive, readwrite;

	if (req->status & ZFCP_STATUS_FSFREQ_ERROR)
		goto skip_fsfstatus;

	atomic_clear_mask(ZFCP_STATUS_COMMON_ACCESS_DENIED |
			  ZFCP_STATUS_COMMON_ACCESS_BOXED |
			  ZFCP_STATUS_UNIT_SHARED |
			  ZFCP_STATUS_UNIT_READONLY,
			  &unit->status);
		erp_action->fsf_req_id = 0;
	}
out:
	spin_unlock_irq(&qdio->req_q_lock);
	return retval;
}

static void zfcp_fsf_open_lun_handler(struct zfcp_fsf_req *req)
{
	struct zfcp_adapter *adapter = req->adapter;
	struct scsi_device *sdev = req->data;
	struct zfcp_scsi_dev *zfcp_sdev;
	struct fsf_qtcb_header *header = &req->qtcb->header;
	union fsf_status_qual *qual = &header->fsf_status_qual;

	if (req->status & ZFCP_STATUS_FSFREQ_ERROR)
		return;

	zfcp_sdev = sdev_to_zfcp(sdev);

	atomic_andnot(ZFCP_STATUS_COMMON_ACCESS_DENIED |
			  ZFCP_STATUS_COMMON_ACCESS_BOXED,
			  &zfcp_sdev->status);

	switch (header->fsf_status) {

	case FSF_PORT_HANDLE_NOT_VALID:
		zfcp_erp_adapter_reopen(unit->port->adapter, 0, 109, req);
		/* fall through */
	case FSF_LUN_ALREADY_OPEN:
		break;
	case FSF_ACCESS_DENIED:
		zfcp_fsf_access_denied_unit(req, unit);
		atomic_clear_mask(ZFCP_STATUS_UNIT_SHARED, &unit->status);
		atomic_clear_mask(ZFCP_STATUS_UNIT_READONLY, &unit->status);
		break;
	case FSF_PORT_BOXED:
		zfcp_erp_port_boxed(unit->port, 51, req);
		req->status |= ZFCP_STATUS_FSFREQ_ERROR |
			       ZFCP_STATUS_FSFREQ_RETRY;
		break;
	case FSF_LUN_SHARING_VIOLATION:
		if (header->fsf_status_qual.word[0])
			dev_warn(&adapter->ccw_device->dev,
				 "FCP-LUN 0x%Lx at the remote port "
				 "with WWPN 0x%Lx "
				 "connected to the adapter "
				 "is already in use in LPAR%d, CSS%d.\n",
				 unit->fcp_lun,
				 unit->port->wwpn,
				 queue_designator->hla,
				 queue_designator->cssid);
		else
			zfcp_act_eval_err(adapter,
					  header->fsf_status_qual.word[2]);
		zfcp_erp_unit_access_denied(unit, 60, req);
		atomic_clear_mask(ZFCP_STATUS_UNIT_SHARED, &unit->status);
		atomic_clear_mask(ZFCP_STATUS_UNIT_READONLY, &unit->status);
		zfcp_erp_adapter_reopen(adapter, 0, "fsouh_1");
		/* fall through */
	case FSF_LUN_ALREADY_OPEN:
		break;
	case FSF_PORT_BOXED:
		zfcp_erp_set_port_status(zfcp_sdev->port,
					 ZFCP_STATUS_COMMON_ACCESS_BOXED);
		zfcp_erp_port_reopen(zfcp_sdev->port,
				     ZFCP_STATUS_COMMON_ERP_FAILED, "fsouh_2");
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_LUN_SHARING_VIOLATION:
		if (qual->word[0])
			dev_warn(&zfcp_sdev->port->adapter->ccw_device->dev,
				 "LUN 0x%Lx on port 0x%Lx is already in "
				 "use by CSS%d, MIF Image ID %x\n",
				 zfcp_scsi_dev_lun(sdev),
				 (unsigned long long)zfcp_sdev->port->wwpn,
				 qual->fsf_queue_designator.cssid,
				 qual->fsf_queue_designator.hla);
		zfcp_erp_set_lun_status(sdev,
					ZFCP_STATUS_COMMON_ERP_FAILED |
					ZFCP_STATUS_COMMON_ACCESS_DENIED);
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_MAXIMUM_NUMBER_OF_LUNS_EXCEEDED:
		dev_warn(&adapter->ccw_device->dev,
			 "The adapter ran out of resources. There is no "
			 "handle available for unit 0x%016Lx on port 0x%016Lx.",
			 unit->fcp_lun, unit->port->wwpn);
		zfcp_erp_unit_failed(unit, 34, req);
			 "No handle is available for LUN "
			 "0x%016Lx on port 0x%016Lx\n",
			 (unsigned long long)zfcp_scsi_dev_lun(sdev),
			 (unsigned long long)zfcp_sdev->port->wwpn);
		zfcp_erp_set_lun_status(sdev, ZFCP_STATUS_COMMON_ERP_FAILED);
		/* fall through */
	case FSF_INVALID_COMMAND_OPTION:
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_ADAPTER_STATUS_AVAILABLE:
		switch (header->fsf_status_qual.word[0]) {
		case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
			zfcp_test_link(unit->port);
			zfcp_fc_test_link(zfcp_sdev->port);
			/* fall through */
		case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
			req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		}
		break;

	case FSF_GOOD:
		unit->handle = header->lun_handle;
		atomic_set_mask(ZFCP_STATUS_COMMON_OPEN, &unit->status);

		if (!(adapter->connection_features & FSF_FEATURE_NPIV_MODE) &&
		    (adapter->adapter_features & FSF_FEATURE_LUN_SHARING) &&
		    (adapter->ccw_device->id.dev_model != ZFCP_DEVICE_MODEL_PRIV)) {
			exclusive = (bottom->lun_access_info &
					FSF_UNIT_ACCESS_EXCLUSIVE);
			readwrite = (bottom->lun_access_info &
					FSF_UNIT_ACCESS_OUTBOUND_TRANSFER);

			if (!exclusive)
		                atomic_set_mask(ZFCP_STATUS_UNIT_SHARED,
						&unit->status);

			if (!readwrite) {
                		atomic_set_mask(ZFCP_STATUS_UNIT_READONLY,
						&unit->status);
				dev_info(&adapter->ccw_device->dev,
					 "Read-only access for unit 0x%016Lx "
					 "on port 0x%016Lx.\n",
					 unit->fcp_lun, unit->port->wwpn);
        		}

        		if (exclusive && !readwrite) {
				dev_err(&adapter->ccw_device->dev,
					"Exclusive access of read-only unit "
					"0x%016Lx on port 0x%016Lx not "
					"supported, disabling unit.\n",
					unit->fcp_lun, unit->port->wwpn);
				zfcp_erp_unit_failed(unit, 35, req);
				req->status |= ZFCP_STATUS_FSFREQ_ERROR;
				zfcp_erp_unit_shutdown(unit, 0, 80, req);
        		} else if (!exclusive && readwrite) {
				dev_err(&adapter->ccw_device->dev,
					"Shared access of read-write unit "
					"0x%016Lx on port 0x%016Lx not "
					"supported, disabling unit.\n",
					unit->fcp_lun, unit->port->wwpn);
				zfcp_erp_unit_failed(unit, 36, req);
				req->status |= ZFCP_STATUS_FSFREQ_ERROR;
				zfcp_erp_unit_shutdown(unit, 0, 81, req);
        		}
		}
		break;
	}

skip_fsfstatus:
	atomic_clear_mask(ZFCP_STATUS_COMMON_OPENING, &unit->status);
}

/**
 * zfcp_fsf_open_unit - open unit
 * @erp_action: pointer to struct zfcp_erp_action
 * Returns: 0 on success, error otherwise
 */
int zfcp_fsf_open_unit(struct zfcp_erp_action *erp_action)
{
	volatile struct qdio_buffer_element *sbale;
	struct zfcp_adapter *adapter = erp_action->adapter;
	struct zfcp_fsf_req *req;
	int retval = -EIO;

	spin_lock_bh(&adapter->req_q.lock);
	if (zfcp_fsf_req_sbal_get(adapter))
		goto out;

	req = zfcp_fsf_req_create(adapter, FSF_QTCB_OPEN_LUN,
				  ZFCP_REQ_AUTO_CLEANUP,
				  adapter->pool.fsf_req_erp);
		zfcp_sdev->lun_handle = header->lun_handle;
		atomic_or(ZFCP_STATUS_COMMON_OPEN, &zfcp_sdev->status);
		break;
	}
}

/**
 * zfcp_fsf_open_lun - open LUN
 * @erp_action: pointer to struct zfcp_erp_action
 * Returns: 0 on success, error otherwise
 */
int zfcp_fsf_open_lun(struct zfcp_erp_action *erp_action)
{
	struct zfcp_adapter *adapter = erp_action->adapter;
	struct zfcp_qdio *qdio = adapter->qdio;
	struct zfcp_fsf_req *req;
	int retval = -EIO;

	spin_lock_irq(&qdio->req_q_lock);
	if (zfcp_qdio_sbal_get(qdio))
		goto out;

	req = zfcp_fsf_req_create(qdio, FSF_QTCB_OPEN_LUN,
				  SBAL_SFLAGS0_TYPE_READ,
				  adapter->pool.erp_req);

	if (IS_ERR(req)) {
		retval = PTR_ERR(req);
		goto out;
	}

	sbale = zfcp_qdio_sbale_req(req);
        sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
        sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	req->qtcb->header.port_handle = erp_action->port->handle;
	req->qtcb->bottom.support.fcp_lun = erp_action->unit->fcp_lun;
	req->handler = zfcp_fsf_open_unit_handler;
	req->data = erp_action->unit;
	req->erp_action = erp_action;
	erp_action->fsf_req = req;
	req->status |= ZFCP_STATUS_FSFREQ_CLEANUP;
	zfcp_qdio_set_sbale_last(qdio, &req->qdio_req);

	req->qtcb->header.port_handle = erp_action->port->handle;
	req->qtcb->bottom.support.fcp_lun = zfcp_scsi_dev_lun(erp_action->sdev);
	req->handler = zfcp_fsf_open_lun_handler;
	req->data = erp_action->sdev;
	req->erp_action = erp_action;
	erp_action->fsf_req_id = req->req_id;

	if (!(adapter->connection_features & FSF_FEATURE_NPIV_MODE))
		req->qtcb->bottom.support.option = FSF_OPEN_LUN_SUPPRESS_BOXING;

	atomic_set_mask(ZFCP_STATUS_COMMON_OPENING, &erp_action->unit->status);

	zfcp_fsf_start_erp_timer(req);
	retval = zfcp_fsf_req_send(req);
	if (retval) {
		zfcp_fsf_req_free(req);
		erp_action->fsf_req = NULL;
	}
out:
	spin_unlock_bh(&adapter->req_q.lock);
	return retval;
}

static void zfcp_fsf_close_unit_handler(struct zfcp_fsf_req *req)
{
	struct zfcp_unit *unit = req->data;

	if (req->status & ZFCP_STATUS_FSFREQ_ERROR)
		goto skip_fsfstatus;

	switch (req->qtcb->header.fsf_status) {
	case FSF_PORT_HANDLE_NOT_VALID:
		zfcp_erp_adapter_reopen(unit->port->adapter, 0, 110, req);
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_LUN_HANDLE_NOT_VALID:
		zfcp_erp_port_reopen(unit->port, 0, 111, req);
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_PORT_BOXED:
		zfcp_erp_port_boxed(unit->port, 52, req);
		req->status |= ZFCP_STATUS_FSFREQ_ERROR |
			       ZFCP_STATUS_FSFREQ_RETRY;
		erp_action->fsf_req_id = 0;
	}
out:
	spin_unlock_irq(&qdio->req_q_lock);
	return retval;
}

static void zfcp_fsf_close_lun_handler(struct zfcp_fsf_req *req)
{
	struct scsi_device *sdev = req->data;
	struct zfcp_scsi_dev *zfcp_sdev;

	if (req->status & ZFCP_STATUS_FSFREQ_ERROR)
		return;

	zfcp_sdev = sdev_to_zfcp(sdev);

	switch (req->qtcb->header.fsf_status) {
	case FSF_PORT_HANDLE_NOT_VALID:
		zfcp_erp_adapter_reopen(zfcp_sdev->port->adapter, 0, "fscuh_1");
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_LUN_HANDLE_NOT_VALID:
		zfcp_erp_port_reopen(zfcp_sdev->port, 0, "fscuh_2");
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_PORT_BOXED:
		zfcp_erp_set_port_status(zfcp_sdev->port,
					 ZFCP_STATUS_COMMON_ACCESS_BOXED);
		zfcp_erp_port_reopen(zfcp_sdev->port,
				     ZFCP_STATUS_COMMON_ERP_FAILED, "fscuh_3");
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_ADAPTER_STATUS_AVAILABLE:
		switch (req->qtcb->header.fsf_status_qual.word[0]) {
		case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
			zfcp_test_link(unit->port);
			zfcp_fc_test_link(zfcp_sdev->port);
			/* fall through */
		case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
			req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		}
		break;
	case FSF_GOOD:
		atomic_clear_mask(ZFCP_STATUS_COMMON_OPEN, &unit->status);
		break;
	}
skip_fsfstatus:
	atomic_clear_mask(ZFCP_STATUS_COMMON_CLOSING, &unit->status);
}

/**
 * zfcp_fsf_close_unit - close zfcp unit
 * @erp_action: pointer to struct zfcp_unit
 * Returns: 0 on success, error otherwise
 */
int zfcp_fsf_close_unit(struct zfcp_erp_action *erp_action)
{
	volatile struct qdio_buffer_element *sbale;
	struct zfcp_adapter *adapter = erp_action->adapter;
	struct zfcp_fsf_req *req;
	int retval = -EIO;

	spin_lock_bh(&adapter->req_q.lock);
	if (zfcp_fsf_req_sbal_get(adapter))
		goto out;
	req = zfcp_fsf_req_create(adapter, FSF_QTCB_CLOSE_LUN,
				  ZFCP_REQ_AUTO_CLEANUP,
				  adapter->pool.fsf_req_erp);
		atomic_andnot(ZFCP_STATUS_COMMON_OPEN, &zfcp_sdev->status);
		break;
	}
}

/**
 * zfcp_fsf_close_LUN - close LUN
 * @erp_action: pointer to erp_action triggering the "close LUN"
 * Returns: 0 on success, error otherwise
 */
int zfcp_fsf_close_lun(struct zfcp_erp_action *erp_action)
{
	struct zfcp_qdio *qdio = erp_action->adapter->qdio;
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(erp_action->sdev);
	struct zfcp_fsf_req *req;
	int retval = -EIO;

	spin_lock_irq(&qdio->req_q_lock);
	if (zfcp_qdio_sbal_get(qdio))
		goto out;

	req = zfcp_fsf_req_create(qdio, FSF_QTCB_CLOSE_LUN,
				  SBAL_SFLAGS0_TYPE_READ,
				  qdio->adapter->pool.erp_req);

	if (IS_ERR(req)) {
		retval = PTR_ERR(req);
		goto out;
	}

	sbale = zfcp_qdio_sbale_req(req);
	sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
	sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	req->qtcb->header.port_handle = erp_action->port->handle;
	req->qtcb->header.lun_handle = erp_action->unit->handle;
	req->handler = zfcp_fsf_close_unit_handler;
	req->data = erp_action->unit;
	req->erp_action = erp_action;
	erp_action->fsf_req = req;
	atomic_set_mask(ZFCP_STATUS_COMMON_CLOSING, &erp_action->unit->status);
	req->status |= ZFCP_STATUS_FSFREQ_CLEANUP;
	zfcp_qdio_set_sbale_last(qdio, &req->qdio_req);

	req->qtcb->header.port_handle = erp_action->port->handle;
	req->qtcb->header.lun_handle = zfcp_sdev->lun_handle;
	req->handler = zfcp_fsf_close_lun_handler;
	req->data = erp_action->sdev;
	req->erp_action = erp_action;
	erp_action->fsf_req_id = req->req_id;

	zfcp_fsf_start_erp_timer(req);
	retval = zfcp_fsf_req_send(req);
	if (retval) {
		zfcp_fsf_req_free(req);
		erp_action->fsf_req = NULL;
	}
out:
	spin_unlock_bh(&adapter->req_q.lock);
		erp_action->fsf_req_id = 0;
	}
out:
	spin_unlock_irq(&qdio->req_q_lock);
	return retval;
}

static void zfcp_fsf_update_lat(struct fsf_latency_record *lat_rec, u32 lat)
{
	lat_rec->sum += lat;
	lat_rec->min = min(lat_rec->min, lat);
	lat_rec->max = max(lat_rec->max, lat);
}

static void zfcp_fsf_req_latency(struct zfcp_fsf_req *req)
{
	struct fsf_qual_latency_info *lat_inf;
	struct latency_cont *lat;
	struct zfcp_unit *unit = req->unit;
	unsigned long flags;

	lat_inf = &req->qtcb->prefix.prot_status_qual.latency_info;

	switch (req->qtcb->bottom.io.data_direction) {
	case FSF_DATADIR_READ:
		lat = &unit->latencies.read;
		break;
	case FSF_DATADIR_WRITE:
		lat = &unit->latencies.write;
		break;
	case FSF_DATADIR_CMND:
		lat = &unit->latencies.cmd;
		break;
	default:
		return;
	}

	spin_lock_irqsave(&unit->latencies.lock, flags);
	zfcp_fsf_update_lat(&lat->channel, lat_inf->channel_lat);
	zfcp_fsf_update_lat(&lat->fabric, lat_inf->fabric_lat);
	lat->counter++;
	spin_unlock_irqrestore(&unit->latencies.lock, flags);
}

static void zfcp_fsf_send_fcp_command_task_handler(struct zfcp_fsf_req *req)
{
	struct scsi_cmnd *scpnt = req->data;
	struct fcp_rsp_iu *fcp_rsp_iu = (struct fcp_rsp_iu *)
	    &(req->qtcb->bottom.io.fcp_rsp);
	u32 sns_len;
	char *fcp_rsp_info = (unsigned char *) &fcp_rsp_iu[1];
	unsigned long flags;

	if (unlikely(!scpnt))
		return;

	read_lock_irqsave(&req->adapter->abort_lock, flags);

	if (unlikely(req->status & ZFCP_STATUS_FSFREQ_ABORTED)) {
		set_host_byte(scpnt, DID_SOFT_ERROR);
		set_driver_byte(scpnt, SUGGEST_RETRY);
		goto skip_fsfstatus;
	}

	if (unlikely(req->status & ZFCP_STATUS_FSFREQ_ERROR)) {
		set_host_byte(scpnt, DID_ERROR);
		goto skip_fsfstatus;
	}

	set_msg_byte(scpnt, COMMAND_COMPLETE);

	scpnt->result |= fcp_rsp_iu->scsi_status;

	if (req->adapter->adapter_features & FSF_FEATURE_MEASUREMENT_DATA)
		zfcp_fsf_req_latency(req);

	if (unlikely(fcp_rsp_iu->validity.bits.fcp_rsp_len_valid)) {
		if (fcp_rsp_info[3] == RSP_CODE_GOOD)
			set_host_byte(scpnt, DID_OK);
		else {
			set_host_byte(scpnt, DID_ERROR);
			goto skip_fsfstatus;
		}
	}

	if (unlikely(fcp_rsp_iu->validity.bits.fcp_sns_len_valid)) {
		sns_len = FSF_FCP_RSP_SIZE - sizeof(struct fcp_rsp_iu) +
			  fcp_rsp_iu->fcp_rsp_len;
		sns_len = min(sns_len, (u32) SCSI_SENSE_BUFFERSIZE);
		sns_len = min(sns_len, fcp_rsp_iu->fcp_sns_len);

		memcpy(scpnt->sense_buffer,
		       zfcp_get_fcp_sns_info_ptr(fcp_rsp_iu), sns_len);
	}

	if (unlikely(fcp_rsp_iu->validity.bits.fcp_resid_under)) {
		scsi_set_resid(scpnt, fcp_rsp_iu->fcp_resid);
		if (scsi_bufflen(scpnt) - scsi_get_resid(scpnt) <
		    scpnt->underflow)
			set_host_byte(scpnt, DID_ERROR);
	}
skip_fsfstatus:
	if (scpnt->result != 0)
		zfcp_scsi_dbf_event_result("erro", 3, req->adapter, scpnt, req);
	else if (scpnt->retries > 0)
		zfcp_scsi_dbf_event_result("retr", 4, req->adapter, scpnt, req);
	else
		zfcp_scsi_dbf_event_result("norm", 6, req->adapter, scpnt, req);
static void zfcp_fsf_req_trace(struct zfcp_fsf_req *req, struct scsi_cmnd *scsi)
{
	struct fsf_qual_latency_info *lat_in;
	struct latency_cont *lat = NULL;
	struct zfcp_scsi_dev *zfcp_sdev;
	struct zfcp_blk_drv_data blktrc;
	int ticks = req->adapter->timer_ticks;

	lat_in = &req->qtcb->prefix.prot_status_qual.latency_info;

	blktrc.flags = 0;
	blktrc.magic = ZFCP_BLK_DRV_DATA_MAGIC;
	if (req->status & ZFCP_STATUS_FSFREQ_ERROR)
		blktrc.flags |= ZFCP_BLK_REQ_ERROR;
	blktrc.inb_usage = 0;
	blktrc.outb_usage = req->qdio_req.qdio_outb_usage;

	if (req->adapter->adapter_features & FSF_FEATURE_MEASUREMENT_DATA &&
	    !(req->status & ZFCP_STATUS_FSFREQ_ERROR)) {
		zfcp_sdev = sdev_to_zfcp(scsi->device);
		blktrc.flags |= ZFCP_BLK_LAT_VALID;
		blktrc.channel_lat = lat_in->channel_lat * ticks;
		blktrc.fabric_lat = lat_in->fabric_lat * ticks;

		switch (req->qtcb->bottom.io.data_direction) {
		case FSF_DATADIR_DIF_READ_STRIP:
		case FSF_DATADIR_DIF_READ_CONVERT:
		case FSF_DATADIR_READ:
			lat = &zfcp_sdev->latencies.read;
			break;
		case FSF_DATADIR_DIF_WRITE_INSERT:
		case FSF_DATADIR_DIF_WRITE_CONVERT:
		case FSF_DATADIR_WRITE:
			lat = &zfcp_sdev->latencies.write;
			break;
		case FSF_DATADIR_CMND:
			lat = &zfcp_sdev->latencies.cmd;
			break;
		}

		if (lat) {
			spin_lock(&zfcp_sdev->latencies.lock);
			zfcp_fsf_update_lat(&lat->channel, lat_in->channel_lat);
			zfcp_fsf_update_lat(&lat->fabric, lat_in->fabric_lat);
			lat->counter++;
			spin_unlock(&zfcp_sdev->latencies.lock);
		}
	}

	blk_add_driver_data(scsi->request->q, scsi->request, &blktrc,
			    sizeof(blktrc));
}

static void zfcp_fsf_fcp_handler_common(struct zfcp_fsf_req *req)
{
	struct scsi_cmnd *scmnd = req->data;
	struct scsi_device *sdev = scmnd->device;
	struct zfcp_scsi_dev *zfcp_sdev;
	struct fsf_qtcb_header *header = &req->qtcb->header;

	if (unlikely(req->status & ZFCP_STATUS_FSFREQ_ERROR))
		return;

	zfcp_sdev = sdev_to_zfcp(sdev);

	switch (header->fsf_status) {
	case FSF_HANDLE_MISMATCH:
	case FSF_PORT_HANDLE_NOT_VALID:
		zfcp_erp_adapter_reopen(zfcp_sdev->port->adapter, 0, "fssfch1");
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_FCPLUN_NOT_VALID:
	case FSF_LUN_HANDLE_NOT_VALID:
		zfcp_erp_port_reopen(zfcp_sdev->port, 0, "fssfch2");
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_SERVICE_CLASS_NOT_SUPPORTED:
		zfcp_fsf_class_not_supp(req);
		break;
	case FSF_DIRECTION_INDICATOR_NOT_VALID:
		dev_err(&req->adapter->ccw_device->dev,
			"Incorrect direction %d, LUN 0x%016Lx on port "
			"0x%016Lx closed\n",
			req->qtcb->bottom.io.data_direction,
			(unsigned long long)zfcp_scsi_dev_lun(sdev),
			(unsigned long long)zfcp_sdev->port->wwpn);
		zfcp_erp_adapter_shutdown(zfcp_sdev->port->adapter, 0,
					  "fssfch3");
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_CMND_LENGTH_NOT_VALID:
		dev_err(&req->adapter->ccw_device->dev,
			"Incorrect CDB length %d, LUN 0x%016Lx on "
			"port 0x%016Lx closed\n",
			req->qtcb->bottom.io.fcp_cmnd_length,
			(unsigned long long)zfcp_scsi_dev_lun(sdev),
			(unsigned long long)zfcp_sdev->port->wwpn);
		zfcp_erp_adapter_shutdown(zfcp_sdev->port->adapter, 0,
					  "fssfch4");
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_PORT_BOXED:
		zfcp_erp_set_port_status(zfcp_sdev->port,
					 ZFCP_STATUS_COMMON_ACCESS_BOXED);
		zfcp_erp_port_reopen(zfcp_sdev->port,
				     ZFCP_STATUS_COMMON_ERP_FAILED, "fssfch5");
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_LUN_BOXED:
		zfcp_erp_set_lun_status(sdev, ZFCP_STATUS_COMMON_ACCESS_BOXED);
		zfcp_erp_lun_reopen(sdev, ZFCP_STATUS_COMMON_ERP_FAILED,
				    "fssfch6");
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_ADAPTER_STATUS_AVAILABLE:
		if (header->fsf_status_qual.word[0] ==
		    FSF_SQ_INVOKE_LINK_TEST_PROCEDURE)
			zfcp_fc_test_link(zfcp_sdev->port);
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	}
}

static void zfcp_fsf_fcp_cmnd_handler(struct zfcp_fsf_req *req)
{
	struct scsi_cmnd *scpnt;
	struct fcp_resp_with_ext *fcp_rsp;
	unsigned long flags;

	read_lock_irqsave(&req->adapter->abort_lock, flags);

	scpnt = req->data;
	if (unlikely(!scpnt)) {
		read_unlock_irqrestore(&req->adapter->abort_lock, flags);
		return;
	}

	zfcp_fsf_fcp_handler_common(req);

	if (unlikely(req->status & ZFCP_STATUS_FSFREQ_ERROR)) {
		set_host_byte(scpnt, DID_TRANSPORT_DISRUPTED);
		goto skip_fsfstatus;
	}

	switch (req->qtcb->header.fsf_status) {
	case FSF_INCONSISTENT_PROT_DATA:
	case FSF_INVALID_PROT_PARM:
		set_host_byte(scpnt, DID_ERROR);
		goto skip_fsfstatus;
	case FSF_BLOCK_GUARD_CHECK_FAILURE:
		zfcp_scsi_dif_sense_error(scpnt, 0x1);
		goto skip_fsfstatus;
	case FSF_APP_TAG_CHECK_FAILURE:
		zfcp_scsi_dif_sense_error(scpnt, 0x2);
		goto skip_fsfstatus;
	case FSF_REF_TAG_CHECK_FAILURE:
		zfcp_scsi_dif_sense_error(scpnt, 0x3);
		goto skip_fsfstatus;
	}
	fcp_rsp = (struct fcp_resp_with_ext *) &req->qtcb->bottom.io.fcp_rsp;
	zfcp_fc_eval_fcp_rsp(fcp_rsp, scpnt);

skip_fsfstatus:
	zfcp_fsf_req_trace(req, scpnt);
	zfcp_dbf_scsi_result(scpnt, req);

	scpnt->host_scribble = NULL;
	(scpnt->scsi_done) (scpnt);
	/*
	 * We must hold this lock until scsi_done has been called.
	 * Otherwise we may call scsi_done after abort regarding this
	 * command has completed.
	 * Note: scsi_done must not block!
	 */
	read_unlock_irqrestore(&req->adapter->abort_lock, flags);
}

static void zfcp_fsf_send_fcp_ctm_handler(struct zfcp_fsf_req *req)
{
	struct fcp_rsp_iu *fcp_rsp_iu = (struct fcp_rsp_iu *)
	    &(req->qtcb->bottom.io.fcp_rsp);
	char *fcp_rsp_info = (unsigned char *) &fcp_rsp_iu[1];

	if ((fcp_rsp_info[3] != RSP_CODE_GOOD) ||
	     (req->status & ZFCP_STATUS_FSFREQ_ERROR))
		req->status |= ZFCP_STATUS_FSFREQ_TMFUNCFAILED;
}


static void zfcp_fsf_send_fcp_command_handler(struct zfcp_fsf_req *req)
{
	struct zfcp_unit *unit;
	struct fsf_qtcb_header *header = &req->qtcb->header;

	if (unlikely(req->status & ZFCP_STATUS_FSFREQ_TASK_MANAGEMENT))
		unit = req->data;
	else
		unit = req->unit;

	if (unlikely(req->status & ZFCP_STATUS_FSFREQ_ERROR))
		goto skip_fsfstatus;

	switch (header->fsf_status) {
	case FSF_HANDLE_MISMATCH:
	case FSF_PORT_HANDLE_NOT_VALID:
		zfcp_erp_adapter_reopen(unit->port->adapter, 0, 112, req);
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_FCPLUN_NOT_VALID:
	case FSF_LUN_HANDLE_NOT_VALID:
		zfcp_erp_port_reopen(unit->port, 0, 113, req);
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_SERVICE_CLASS_NOT_SUPPORTED:
		zfcp_fsf_class_not_supp(req);
		break;
	case FSF_ACCESS_DENIED:
		zfcp_fsf_access_denied_unit(req, unit);
		break;
	case FSF_DIRECTION_INDICATOR_NOT_VALID:
		dev_err(&req->adapter->ccw_device->dev,
			"Invalid data direction (%d) given for unit "
			"0x%016Lx on port 0x%016Lx, shutting down "
			"adapter.\n",
			req->qtcb->bottom.io.data_direction,
			unit->fcp_lun, unit->port->wwpn);
		zfcp_erp_adapter_shutdown(unit->port->adapter, 0, 133, req);
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_CMND_LENGTH_NOT_VALID:
		dev_err(&req->adapter->ccw_device->dev,
			"An invalid control-data-block length field (%d) "
			"was found in a command for unit 0x%016Lx on port "
			"0x%016Lx. Shutting down adapter.\n",
			req->qtcb->bottom.io.fcp_cmnd_length,
			unit->fcp_lun, unit->port->wwpn);
		zfcp_erp_adapter_shutdown(unit->port->adapter, 0, 134, req);
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_PORT_BOXED:
		zfcp_erp_port_boxed(unit->port, 53, req);
		req->status |= ZFCP_STATUS_FSFREQ_ERROR |
			       ZFCP_STATUS_FSFREQ_RETRY;
		break;
	case FSF_LUN_BOXED:
		zfcp_erp_unit_boxed(unit, 54, req);
		req->status |= ZFCP_STATUS_FSFREQ_ERROR |
			       ZFCP_STATUS_FSFREQ_RETRY;
		break;
	case FSF_ADAPTER_STATUS_AVAILABLE:
		if (header->fsf_status_qual.word[0] ==
		    FSF_SQ_INVOKE_LINK_TEST_PROCEDURE)
			zfcp_test_link(unit->port);
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	}
skip_fsfstatus:
	if (req->status & ZFCP_STATUS_FSFREQ_TASK_MANAGEMENT)
		zfcp_fsf_send_fcp_ctm_handler(req);
	else {
		zfcp_fsf_send_fcp_command_task_handler(req);
		req->unit = NULL;
		zfcp_unit_put(unit);
	}
}

/**
 * zfcp_fsf_send_fcp_command_task - initiate an FCP command (for a SCSI command)
 * @adapter: adapter where scsi command is issued
 * @unit: unit where command is sent to
 * @scsi_cmnd: scsi command to be sent
 * @timer: timer to be started when request is initiated
 * @req_flags: flags for fsf_request
 */
int zfcp_fsf_send_fcp_command_task(struct zfcp_adapter *adapter,
				   struct zfcp_unit *unit,
				   struct scsi_cmnd *scsi_cmnd,
				   int use_timer, int req_flags)
{
	struct zfcp_fsf_req *req;
	struct fcp_cmnd_iu *fcp_cmnd_iu;
	unsigned int sbtype;
	int real_bytes, retval = -EIO;

	if (unlikely(!(atomic_read(&unit->status) &
		       ZFCP_STATUS_COMMON_UNBLOCKED)))
		return -EBUSY;

	spin_lock(&adapter->req_q.lock);
	if (!atomic_read(&adapter->req_q.count))
		goto out;
	req = zfcp_fsf_req_create(adapter, FSF_QTCB_FCP_CMND, req_flags,
				  adapter->pool.fsf_req_scsi);
static int zfcp_fsf_set_data_dir(struct scsi_cmnd *scsi_cmnd, u32 *data_dir)
{
	switch (scsi_get_prot_op(scsi_cmnd)) {
	case SCSI_PROT_NORMAL:
		switch (scsi_cmnd->sc_data_direction) {
		case DMA_NONE:
			*data_dir = FSF_DATADIR_CMND;
			break;
		case DMA_FROM_DEVICE:
			*data_dir = FSF_DATADIR_READ;
			break;
		case DMA_TO_DEVICE:
			*data_dir = FSF_DATADIR_WRITE;
			break;
		case DMA_BIDIRECTIONAL:
			return -EINVAL;
		}
		break;

	case SCSI_PROT_READ_STRIP:
		*data_dir = FSF_DATADIR_DIF_READ_STRIP;
		break;
	case SCSI_PROT_WRITE_INSERT:
		*data_dir = FSF_DATADIR_DIF_WRITE_INSERT;
		break;
	case SCSI_PROT_READ_PASS:
		*data_dir = FSF_DATADIR_DIF_READ_CONVERT;
		break;
	case SCSI_PROT_WRITE_PASS:
		*data_dir = FSF_DATADIR_DIF_WRITE_CONVERT;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/**
 * zfcp_fsf_fcp_cmnd - initiate an FCP command (for a SCSI command)
 * @scsi_cmnd: scsi command to be sent
 */
int zfcp_fsf_fcp_cmnd(struct scsi_cmnd *scsi_cmnd)
{
	struct zfcp_fsf_req *req;
	struct fcp_cmnd *fcp_cmnd;
	u8 sbtype = SBAL_SFLAGS0_TYPE_READ;
	int retval = -EIO;
	struct scsi_device *sdev = scsi_cmnd->device;
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(sdev);
	struct zfcp_adapter *adapter = zfcp_sdev->port->adapter;
	struct zfcp_qdio *qdio = adapter->qdio;
	struct fsf_qtcb_bottom_io *io;
	unsigned long flags;

	if (unlikely(!(atomic_read(&zfcp_sdev->status) &
		       ZFCP_STATUS_COMMON_UNBLOCKED)))
		return -EBUSY;

	spin_lock_irqsave(&qdio->req_q_lock, flags);
	if (atomic_read(&qdio->req_q_free) <= 0) {
		atomic_inc(&qdio->req_q_full);
		goto out;
	}

	if (scsi_cmnd->sc_data_direction == DMA_TO_DEVICE)
		sbtype = SBAL_SFLAGS0_TYPE_WRITE;

	req = zfcp_fsf_req_create(qdio, FSF_QTCB_FCP_CMND,
				  sbtype, adapter->pool.scsi_req);

	if (IS_ERR(req)) {
		retval = PTR_ERR(req);
		goto out;
	}

	zfcp_unit_get(unit);
	req->unit = unit;
	req->data = scsi_cmnd;
	req->handler = zfcp_fsf_send_fcp_command_handler;
	req->qtcb->header.lun_handle = unit->handle;
	req->qtcb->header.port_handle = unit->port->handle;
	req->qtcb->bottom.io.service_class = FSF_CLASS_3;

	scsi_cmnd->host_scribble = (unsigned char *) req->req_id;

	fcp_cmnd_iu = (struct fcp_cmnd_iu *) &(req->qtcb->bottom.io.fcp_cmnd);
	fcp_cmnd_iu->fcp_lun = unit->fcp_lun;
	/*
	 * set depending on data direction:
	 *      data direction bits in SBALE (SB Type)
	 *      data direction bits in QTCB
	 *      data direction bits in FCP_CMND IU
	 */
	switch (scsi_cmnd->sc_data_direction) {
	case DMA_NONE:
		req->qtcb->bottom.io.data_direction = FSF_DATADIR_CMND;
		sbtype = SBAL_FLAGS0_TYPE_READ;
		break;
	case DMA_FROM_DEVICE:
		req->qtcb->bottom.io.data_direction = FSF_DATADIR_READ;
		sbtype = SBAL_FLAGS0_TYPE_READ;
		fcp_cmnd_iu->rddata = 1;
		break;
	case DMA_TO_DEVICE:
		req->qtcb->bottom.io.data_direction = FSF_DATADIR_WRITE;
		sbtype = SBAL_FLAGS0_TYPE_WRITE;
		fcp_cmnd_iu->wddata = 1;
		break;
	case DMA_BIDIRECTIONAL:
	default:
		retval = -EIO;
		goto failed_scsi_cmnd;
	}

	if (likely((scsi_cmnd->device->simple_tags) ||
		   ((atomic_read(&unit->status) & ZFCP_STATUS_UNIT_READONLY) &&
		    (atomic_read(&unit->status) & ZFCP_STATUS_UNIT_SHARED))))
		fcp_cmnd_iu->task_attribute = SIMPLE_Q;
	else
		fcp_cmnd_iu->task_attribute = UNTAGGED;

	if (unlikely(scsi_cmnd->cmd_len > FCP_CDB_LENGTH))
		fcp_cmnd_iu->add_fcp_cdb_length =
			(scsi_cmnd->cmd_len - FCP_CDB_LENGTH) >> 2;

	memcpy(fcp_cmnd_iu->fcp_cdb, scsi_cmnd->cmnd, scsi_cmnd->cmd_len);

	req->qtcb->bottom.io.fcp_cmnd_length = sizeof(struct fcp_cmnd_iu) +
		fcp_cmnd_iu->add_fcp_cdb_length + sizeof(fcp_dl_t);

	real_bytes = zfcp_qdio_sbals_from_sg(req, sbtype,
					     scsi_sglist(scsi_cmnd),
					     FSF_MAX_SBALS_PER_REQ);
	if (unlikely(real_bytes < 0)) {
		if (req->sbal_number < FSF_MAX_SBALS_PER_REQ)
			retval = -EIO;
		else {
			dev_err(&adapter->ccw_device->dev,
				"SCSI request too large. "
				"Shutting down unit 0x%016Lx on port "
				"0x%016Lx.\n", unit->fcp_lun,
				unit->port->wwpn);
			zfcp_erp_unit_shutdown(unit, 0, 131, req);
			retval = -EINVAL;
		}
		goto failed_scsi_cmnd;
	}

	zfcp_set_fcp_dl(fcp_cmnd_iu, real_bytes);

	if (use_timer)
		zfcp_fsf_start_timer(req, ZFCP_FSF_REQUEST_TIMEOUT);
	scsi_cmnd->host_scribble = (unsigned char *) req->req_id;

	io = &req->qtcb->bottom.io;
	req->status |= ZFCP_STATUS_FSFREQ_CLEANUP;
	req->data = scsi_cmnd;
	req->handler = zfcp_fsf_fcp_cmnd_handler;
	req->qtcb->header.lun_handle = zfcp_sdev->lun_handle;
	req->qtcb->header.port_handle = zfcp_sdev->port->handle;
	io->service_class = FSF_CLASS_3;
	io->fcp_cmnd_length = FCP_CMND_LEN;

	if (scsi_get_prot_op(scsi_cmnd) != SCSI_PROT_NORMAL) {
		io->data_block_length = scsi_cmnd->device->sector_size;
		io->ref_tag_value = scsi_get_lba(scsi_cmnd) & 0xFFFFFFFF;
	}

	if (zfcp_fsf_set_data_dir(scsi_cmnd, &io->data_direction))
		goto failed_scsi_cmnd;

	fcp_cmnd = (struct fcp_cmnd *) &req->qtcb->bottom.io.fcp_cmnd;
	zfcp_fc_scsi_to_fcp(fcp_cmnd, scsi_cmnd, 0);

	if ((scsi_get_prot_op(scsi_cmnd) != SCSI_PROT_NORMAL) &&
	    scsi_prot_sg_count(scsi_cmnd)) {
		zfcp_qdio_set_data_div(qdio, &req->qdio_req,
				       scsi_prot_sg_count(scsi_cmnd));
		retval = zfcp_qdio_sbals_from_sg(qdio, &req->qdio_req,
						 scsi_prot_sglist(scsi_cmnd));
		if (retval)
			goto failed_scsi_cmnd;
		io->prot_data_length = zfcp_qdio_real_bytes(
						scsi_prot_sglist(scsi_cmnd));
	}

	retval = zfcp_qdio_sbals_from_sg(qdio, &req->qdio_req,
					 scsi_sglist(scsi_cmnd));
	if (unlikely(retval))
		goto failed_scsi_cmnd;

	zfcp_qdio_set_sbale_last(adapter->qdio, &req->qdio_req);
	if (zfcp_adapter_multi_buffer_active(adapter))
		zfcp_qdio_set_scount(qdio, &req->qdio_req);

	retval = zfcp_fsf_req_send(req);
	if (unlikely(retval))
		goto failed_scsi_cmnd;

	goto out;

failed_scsi_cmnd:
	zfcp_unit_put(unit);
	zfcp_fsf_req_free(req);
	scsi_cmnd->host_scribble = NULL;
out:
	spin_unlock(&adapter->req_q.lock);
	return retval;
}

/**
 * zfcp_fsf_send_fcp_ctm - send SCSI task management command
 * @adapter: pointer to struct zfcp-adapter
 * @unit: pointer to struct zfcp_unit
 * @tm_flags: unsigned byte for task management flags
 * @req_flags: int request flags
 * Returns: on success pointer to struct fsf_req, NULL otherwise
 */
struct zfcp_fsf_req *zfcp_fsf_send_fcp_ctm(struct zfcp_adapter *adapter,
					   struct zfcp_unit *unit,
					   u8 tm_flags, int req_flags)
{
	volatile struct qdio_buffer_element *sbale;
	struct zfcp_fsf_req *req = NULL;
	struct fcp_cmnd_iu *fcp_cmnd_iu;

	if (unlikely(!(atomic_read(&unit->status) &
		       ZFCP_STATUS_COMMON_UNBLOCKED)))
		return NULL;

	spin_lock(&adapter->req_q.lock);
	if (!atomic_read(&adapter->req_q.count))
		goto out;
	req = zfcp_fsf_req_create(adapter, FSF_QTCB_FCP_CMND, req_flags,
				  adapter->pool.fsf_req_scsi);
	if (IS_ERR(req))
		goto out;

	req->status |= ZFCP_STATUS_FSFREQ_TASK_MANAGEMENT;
	req->data = unit;
	req->handler = zfcp_fsf_send_fcp_command_handler;
	req->qtcb->header.lun_handle = unit->handle;
	req->qtcb->header.port_handle = unit->port->handle;
	req->qtcb->bottom.io.data_direction = FSF_DATADIR_CMND;
	req->qtcb->bottom.io.service_class = FSF_CLASS_3;
	req->qtcb->bottom.io.fcp_cmnd_length = 	sizeof(struct fcp_cmnd_iu) +
						sizeof(fcp_dl_t);

	sbale = zfcp_qdio_sbale_req(req);
	sbale[0].flags |= SBAL_FLAGS0_TYPE_WRITE;
	sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	fcp_cmnd_iu = (struct fcp_cmnd_iu *) &req->qtcb->bottom.io.fcp_cmnd;
	fcp_cmnd_iu->fcp_lun = unit->fcp_lun;
	fcp_cmnd_iu->task_management_flags = tm_flags;
	zfcp_fsf_req_free(req);
	scsi_cmnd->host_scribble = NULL;
out:
	spin_unlock_irqrestore(&qdio->req_q_lock, flags);
	return retval;
}

static void zfcp_fsf_fcp_task_mgmt_handler(struct zfcp_fsf_req *req)
{
	struct fcp_resp_with_ext *fcp_rsp;
	struct fcp_resp_rsp_info *rsp_info;

	zfcp_fsf_fcp_handler_common(req);

	fcp_rsp = (struct fcp_resp_with_ext *) &req->qtcb->bottom.io.fcp_rsp;
	rsp_info = (struct fcp_resp_rsp_info *) &fcp_rsp[1];

	if ((rsp_info->rsp_code != FCP_TMF_CMPL) ||
	     (req->status & ZFCP_STATUS_FSFREQ_ERROR))
		req->status |= ZFCP_STATUS_FSFREQ_TMFUNCFAILED;
}

/**
 * zfcp_fsf_fcp_task_mgmt - send SCSI task management command
 * @scmnd: SCSI command to send the task management command for
 * @tm_flags: unsigned byte for task management flags
 * Returns: on success pointer to struct fsf_req, NULL otherwise
 */
struct zfcp_fsf_req *zfcp_fsf_fcp_task_mgmt(struct scsi_cmnd *scmnd,
					    u8 tm_flags)
{
	struct zfcp_fsf_req *req = NULL;
	struct fcp_cmnd *fcp_cmnd;
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(scmnd->device);
	struct zfcp_qdio *qdio = zfcp_sdev->port->adapter->qdio;

	if (unlikely(!(atomic_read(&zfcp_sdev->status) &
		       ZFCP_STATUS_COMMON_UNBLOCKED)))
		return NULL;

	spin_lock_irq(&qdio->req_q_lock);
	if (zfcp_qdio_sbal_get(qdio))
		goto out;

	req = zfcp_fsf_req_create(qdio, FSF_QTCB_FCP_CMND,
				  SBAL_SFLAGS0_TYPE_WRITE,
				  qdio->adapter->pool.scsi_req);

	if (IS_ERR(req)) {
		req = NULL;
		goto out;
	}

	req->data = scmnd;
	req->handler = zfcp_fsf_fcp_task_mgmt_handler;
	req->qtcb->header.lun_handle = zfcp_sdev->lun_handle;
	req->qtcb->header.port_handle = zfcp_sdev->port->handle;
	req->qtcb->bottom.io.data_direction = FSF_DATADIR_CMND;
	req->qtcb->bottom.io.service_class = FSF_CLASS_3;
	req->qtcb->bottom.io.fcp_cmnd_length = FCP_CMND_LEN;

	zfcp_qdio_set_sbale_last(qdio, &req->qdio_req);

	fcp_cmnd = (struct fcp_cmnd *) &req->qtcb->bottom.io.fcp_cmnd;
	zfcp_fc_scsi_to_fcp(fcp_cmnd, scmnd, tm_flags);

	zfcp_fsf_start_timer(req, ZFCP_SCSI_ER_TIMEOUT);
	if (!zfcp_fsf_req_send(req))
		goto out;

	zfcp_fsf_req_free(req);
	req = NULL;
out:
	spin_unlock(&adapter->req_q.lock);
	return req;
}

static void zfcp_fsf_control_file_handler(struct zfcp_fsf_req *req)
{
	if (req->qtcb->header.fsf_status != FSF_GOOD)
		req->status |= ZFCP_STATUS_FSFREQ_ERROR;
}

/**
 * zfcp_fsf_control_file - control file upload/download
 * @adapter: pointer to struct zfcp_adapter
 * @fsf_cfdc: pointer to struct zfcp_fsf_cfdc
 * Returns: on success pointer to struct zfcp_fsf_req, NULL otherwise
 */
struct zfcp_fsf_req *zfcp_fsf_control_file(struct zfcp_adapter *adapter,
					   struct zfcp_fsf_cfdc *fsf_cfdc)
{
	volatile struct qdio_buffer_element *sbale;
	struct zfcp_fsf_req *req = NULL;
	struct fsf_qtcb_bottom_support *bottom;
	int direction, retval = -EIO, bytes;

	if (!(adapter->adapter_features & FSF_FEATURE_CFDC))
		return ERR_PTR(-EOPNOTSUPP);

	switch (fsf_cfdc->command) {
	case FSF_QTCB_DOWNLOAD_CONTROL_FILE:
		direction = SBAL_FLAGS0_TYPE_WRITE;
		break;
	case FSF_QTCB_UPLOAD_CONTROL_FILE:
		direction = SBAL_FLAGS0_TYPE_READ;
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	spin_lock_bh(&adapter->req_q.lock);
	if (zfcp_fsf_req_sbal_get(adapter))
		goto out;

	req = zfcp_fsf_req_create(adapter, fsf_cfdc->command, 0, NULL);
	if (IS_ERR(req)) {
		retval = -EPERM;
		goto out;
	}

	req->handler = zfcp_fsf_control_file_handler;

	sbale = zfcp_qdio_sbale_req(req);
	sbale[0].flags |= direction;

	bottom = &req->qtcb->bottom.support;
	bottom->operation_subtype = FSF_CFDC_OPERATION_SUBTYPE;
	bottom->option = fsf_cfdc->option;

	bytes = zfcp_qdio_sbals_from_sg(req, direction, fsf_cfdc->sg,
					FSF_MAX_SBALS_PER_REQ);
	if (bytes != ZFCP_CFDC_MAX_SIZE) {
		retval = -ENOMEM;
		zfcp_fsf_req_free(req);
		goto out;
	}

	zfcp_fsf_start_timer(req, ZFCP_FSF_REQUEST_TIMEOUT);
	retval = zfcp_fsf_req_send(req);
out:
	spin_unlock_bh(&adapter->req_q.lock);

	if (!retval) {
		wait_event(req->completion_wq,
			   req->status & ZFCP_STATUS_FSFREQ_COMPLETED);
		return req;
	}
	return ERR_PTR(retval);
	spin_unlock_irq(&qdio->req_q_lock);
	return req;
}

/**
 * zfcp_fsf_reqid_check - validate req_id contained in SBAL returned by QDIO
 * @adapter: pointer to struct zfcp_adapter
 * @sbal_idx: response queue index of SBAL to be processed
 */
void zfcp_fsf_reqid_check(struct zfcp_qdio *qdio, int sbal_idx)
{
	struct zfcp_adapter *adapter = qdio->adapter;
	struct qdio_buffer *sbal = qdio->res_q[sbal_idx];
	struct qdio_buffer_element *sbale;
	struct zfcp_fsf_req *fsf_req;
	unsigned long req_id;
	int idx;

	for (idx = 0; idx < QDIO_MAX_ELEMENTS_PER_BUFFER; idx++) {

		sbale = &sbal->element[idx];
		req_id = (unsigned long) sbale->addr;
		fsf_req = zfcp_reqlist_find_rm(adapter->req_list, req_id);

		if (!fsf_req) {
			/*
			 * Unknown request means that we have potentially memory
			 * corruption and must stop the machine immediately.
			 */
			zfcp_qdio_siosl(adapter);
			panic("error: unknown req_id (%lx) on adapter %s.\n",
			      req_id, dev_name(&adapter->ccw_device->dev));
		}

		fsf_req->qdio_req.sbal_response = sbal_idx;
		zfcp_fsf_req_complete(fsf_req);

		if (likely(sbale->eflags & SBAL_EFLAGS_LAST_ENTRY))
			break;
	}
}
