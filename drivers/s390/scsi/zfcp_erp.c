/*
 * zfcp device driver
 *
 * Error Recovery Procedures (ERP).
 *
 * Copyright IBM Corporation 2002, 2008
 */

#include "zfcp_ext.h"
 * Copyright IBM Corp. 2002, 2010
 * Copyright IBM Corp. 2002, 2015
 * Copyright IBM Corp. 2002, 2016
 */

#define KMSG_COMPONENT "zfcp"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/kthread.h>
#include <linux/bug.h>
#include "zfcp_ext.h"
#include "zfcp_reqlist.h"

#define ZFCP_MAX_ERPS                   3

enum zfcp_erp_act_flags {
	ZFCP_STATUS_ERP_TIMEDOUT	= 0x10000000,
	ZFCP_STATUS_ERP_CLOSE_ONLY	= 0x01000000,
	ZFCP_STATUS_ERP_DISMISSING	= 0x00100000,
	ZFCP_STATUS_ERP_DISMISSED	= 0x00200000,
	ZFCP_STATUS_ERP_LOWMEM		= 0x00400000,
	ZFCP_STATUS_ERP_NO_REF		= 0x00800000,
};

enum zfcp_erp_steps {
	ZFCP_ERP_STEP_UNINITIALIZED	= 0x0000,
	ZFCP_ERP_STEP_FSF_XCONFIG	= 0x0001,
	ZFCP_ERP_STEP_PHYS_PORT_CLOSING	= 0x0010,
	ZFCP_ERP_STEP_PORT_CLOSING	= 0x0100,
	ZFCP_ERP_STEP_NAMESERVER_OPEN	= 0x0200,
	ZFCP_ERP_STEP_NAMESERVER_LOOKUP	= 0x0400,
	ZFCP_ERP_STEP_PORT_OPENING	= 0x0800,
	ZFCP_ERP_STEP_UNIT_CLOSING	= 0x1000,
	ZFCP_ERP_STEP_UNIT_OPENING	= 0x2000,
};

enum zfcp_erp_act_type {
	ZFCP_ERP_ACTION_REOPEN_UNIT        = 1,
	ZFCP_ERP_STEP_PORT_OPENING	= 0x0800,
	ZFCP_ERP_STEP_LUN_CLOSING	= 0x1000,
	ZFCP_ERP_STEP_LUN_OPENING	= 0x2000,
};

/**
 * enum zfcp_erp_act_type - Type of ERP action object.
 * @ZFCP_ERP_ACTION_REOPEN_LUN: LUN recovery.
 * @ZFCP_ERP_ACTION_REOPEN_PORT: Port recovery.
 * @ZFCP_ERP_ACTION_REOPEN_PORT_FORCED: Forced port recovery.
 * @ZFCP_ERP_ACTION_REOPEN_ADAPTER: Adapter recovery.
 * @ZFCP_ERP_ACTION_NONE: Eyecatcher pseudo flag to bitwise or-combine with
 *			  either of the first four enum values.
 *			  Used to indicate that an ERP action could not be
 *			  set up despite a detected need for some recovery.
 * @ZFCP_ERP_ACTION_FAILED: Eyecatcher pseudo flag to bitwise or-combine with
 *			    either of the first four enum values.
 *			    Used to indicate that ERP not needed because
 *			    the object has ZFCP_STATUS_COMMON_ERP_FAILED.
 */
enum zfcp_erp_act_type {
	ZFCP_ERP_ACTION_REOPEN_LUN         = 1,
	ZFCP_ERP_ACTION_REOPEN_PORT	   = 2,
	ZFCP_ERP_ACTION_REOPEN_PORT_FORCED = 3,
	ZFCP_ERP_ACTION_REOPEN_ADAPTER     = 4,
	ZFCP_ERP_ACTION_NONE		   = 0xc0,
	ZFCP_ERP_ACTION_FAILED		   = 0xe0,
};

enum zfcp_erp_act_state {
	ZFCP_ERP_ACTION_RUNNING = 1,
	ZFCP_ERP_ACTION_READY   = 2,
};

enum zfcp_erp_act_result {
	ZFCP_ERP_SUCCEEDED = 0,
	ZFCP_ERP_FAILED    = 1,
	ZFCP_ERP_CONTINUES = 2,
	ZFCP_ERP_EXIT      = 3,
	ZFCP_ERP_DISMISSED = 4,
	ZFCP_ERP_NOMEM     = 5,
};

static void zfcp_erp_adapter_block(struct zfcp_adapter *adapter, int mask)
{
	zfcp_erp_modify_adapter_status(adapter, 15, NULL,
				       ZFCP_STATUS_COMMON_UNBLOCKED | mask,
				       ZFCP_CLEAR);
	zfcp_erp_clear_adapter_status(adapter,
				       ZFCP_STATUS_COMMON_UNBLOCKED | mask);
}

static int zfcp_erp_action_exists(struct zfcp_erp_action *act)
{
	struct zfcp_erp_action *curr_act;

	list_for_each_entry(curr_act, &act->adapter->erp_running_head, list)
		if (act == curr_act)
			return ZFCP_ERP_ACTION_RUNNING;
	return 0;
}

static void zfcp_erp_action_ready(struct zfcp_erp_action *act)
{
	struct zfcp_adapter *adapter = act->adapter;

	list_move(&act->list, &act->adapter->erp_ready_head);
	zfcp_rec_dbf_event_action(146, act);
	up(&adapter->erp_ready_sem);
	zfcp_rec_dbf_event_thread(2, adapter);
	zfcp_dbf_rec_run("erardy1", act);
	wake_up(&adapter->erp_ready_wq);
	zfcp_dbf_rec_run("erardy2", act);
}

static void zfcp_erp_action_dismiss(struct zfcp_erp_action *act)
{
	act->status |= ZFCP_STATUS_ERP_DISMISSED;
	if (zfcp_erp_action_exists(act) == ZFCP_ERP_ACTION_RUNNING)
		zfcp_erp_action_ready(act);
}

static void zfcp_erp_action_dismiss_unit(struct zfcp_unit *unit)
{
	if (atomic_read(&unit->status) & ZFCP_STATUS_COMMON_ERP_INUSE)
		zfcp_erp_action_dismiss(&unit->erp_action);
static void zfcp_erp_action_dismiss_lun(struct scsi_device *sdev)
{
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(sdev);

	if (atomic_read(&zfcp_sdev->status) & ZFCP_STATUS_COMMON_ERP_INUSE)
		zfcp_erp_action_dismiss(&zfcp_sdev->erp_action);
}

static void zfcp_erp_action_dismiss_port(struct zfcp_port *port)
{
	struct zfcp_unit *unit;

	if (atomic_read(&port->status) & ZFCP_STATUS_COMMON_ERP_INUSE)
		zfcp_erp_action_dismiss(&port->erp_action);
	else
		list_for_each_entry(unit, &port->unit_list_head, list)
		    zfcp_erp_action_dismiss_unit(unit);
	struct scsi_device *sdev;

	if (atomic_read(&port->status) & ZFCP_STATUS_COMMON_ERP_INUSE)
		zfcp_erp_action_dismiss(&port->erp_action);
	else {
		spin_lock(port->adapter->scsi_host->host_lock);
		__shost_for_each_device(sdev, port->adapter->scsi_host)
			if (sdev_to_zfcp(sdev)->port == port)
				zfcp_erp_action_dismiss_lun(sdev);
		spin_unlock(port->adapter->scsi_host->host_lock);
	}
}

static void zfcp_erp_action_dismiss_adapter(struct zfcp_adapter *adapter)
{
	struct zfcp_port *port;

	if (atomic_read(&adapter->status) & ZFCP_STATUS_COMMON_ERP_INUSE)
		zfcp_erp_action_dismiss(&adapter->erp_action);
	else
		list_for_each_entry(port, &adapter->port_list_head, list)
		    zfcp_erp_action_dismiss_port(port);
	else {
		read_lock(&adapter->port_list_lock);
		list_for_each_entry(port, &adapter->port_list, list)
		    zfcp_erp_action_dismiss_port(port);
		read_unlock(&adapter->port_list_lock);
	}
}

static int zfcp_erp_handle_failed(int want, struct zfcp_adapter *adapter,
				  struct zfcp_port *port,
				  struct scsi_device *sdev)
{
	int need = want;
	struct zfcp_scsi_dev *zsdev;

	switch (want) {
	case ZFCP_ERP_ACTION_REOPEN_LUN:
		zsdev = sdev_to_zfcp(sdev);
		if (atomic_read(&zsdev->status) & ZFCP_STATUS_COMMON_ERP_FAILED)
			need = 0;
		break;
	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
		if (atomic_read(&port->status) & ZFCP_STATUS_COMMON_ERP_FAILED)
			need = 0;
		break;
	case ZFCP_ERP_ACTION_REOPEN_PORT:
		if (atomic_read(&port->status) &
		    ZFCP_STATUS_COMMON_ERP_FAILED) {
			need = 0;
			/* ensure propagation of failed status to new devices */
			zfcp_erp_set_port_status(
				port, ZFCP_STATUS_COMMON_ERP_FAILED);
		}
		break;
	case ZFCP_ERP_ACTION_REOPEN_ADAPTER:
		if (atomic_read(&adapter->status) &
		    ZFCP_STATUS_COMMON_ERP_FAILED) {
			need = 0;
			/* ensure propagation of failed status to new devices */
			zfcp_erp_set_adapter_status(
				adapter, ZFCP_STATUS_COMMON_ERP_FAILED);
		}
		break;
	}

	return need;
}

static int zfcp_erp_required_act(int want, struct zfcp_adapter *adapter,
				 struct zfcp_port *port,
				 struct zfcp_unit *unit)
{
	int need = want;
	int u_status, p_status, a_status;

	switch (want) {
	case ZFCP_ERP_ACTION_REOPEN_UNIT:
		u_status = atomic_read(&unit->status);
		if (u_status & ZFCP_STATUS_COMMON_ERP_INUSE)
				 struct scsi_device *sdev)
{
	int need = want;
	int l_status, p_status, a_status;
	struct zfcp_scsi_dev *zfcp_sdev;

	switch (want) {
	case ZFCP_ERP_ACTION_REOPEN_LUN:
		zfcp_sdev = sdev_to_zfcp(sdev);
		l_status = atomic_read(&zfcp_sdev->status);
		if (l_status & ZFCP_STATUS_COMMON_ERP_INUSE)
			return 0;
		p_status = atomic_read(&port->status);
		if (!(p_status & ZFCP_STATUS_COMMON_RUNNING) ||
		      p_status & ZFCP_STATUS_COMMON_ERP_FAILED)
			return 0;
		if (!(p_status & ZFCP_STATUS_COMMON_UNBLOCKED))
			need = ZFCP_ERP_ACTION_REOPEN_PORT;
		/* fall through */
	case ZFCP_ERP_ACTION_REOPEN_PORT:
	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
		p_status = atomic_read(&port->status);
	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
		p_status = atomic_read(&port->status);
		if (!(p_status & ZFCP_STATUS_COMMON_OPEN))
			need = ZFCP_ERP_ACTION_REOPEN_PORT;
		/* fall through */
	case ZFCP_ERP_ACTION_REOPEN_PORT:
		p_status = atomic_read(&port->status);
		if (p_status & ZFCP_STATUS_COMMON_ERP_INUSE)
			return 0;
		a_status = atomic_read(&adapter->status);
		if (!(a_status & ZFCP_STATUS_COMMON_RUNNING) ||
		      a_status & ZFCP_STATUS_COMMON_ERP_FAILED)
			return 0;
		if (p_status & ZFCP_STATUS_COMMON_NOESC)
			return need;
		if (!(a_status & ZFCP_STATUS_COMMON_UNBLOCKED))
			need = ZFCP_ERP_ACTION_REOPEN_ADAPTER;
		/* fall through */
	case ZFCP_ERP_ACTION_REOPEN_ADAPTER:
		a_status = atomic_read(&adapter->status);
		if (a_status & ZFCP_STATUS_COMMON_ERP_INUSE)
			return 0;
		if (!(a_status & ZFCP_STATUS_COMMON_RUNNING) &&
		    !(a_status & ZFCP_STATUS_COMMON_OPEN))
			return 0; /* shutdown requested for closed adapter */
	}

	return need;
}

static struct zfcp_erp_action *zfcp_erp_setup_act(int need,
						  struct zfcp_adapter *adapter,
						  struct zfcp_port *port,
						  struct zfcp_unit *unit)
{
	struct zfcp_erp_action *erp_action;
	u32 status = 0;

	switch (need) {
	case ZFCP_ERP_ACTION_REOPEN_UNIT:
		zfcp_unit_get(unit);
		atomic_set_mask(ZFCP_STATUS_COMMON_ERP_INUSE, &unit->status);
		erp_action = &unit->erp_action;
		if (!(atomic_read(&unit->status) & ZFCP_STATUS_COMMON_RUNNING))
			status = ZFCP_STATUS_ERP_CLOSE_ONLY;
static struct zfcp_erp_action *zfcp_erp_setup_act(int need, u32 act_status,
						  struct zfcp_adapter *adapter,
						  struct zfcp_port *port,
						  struct scsi_device *sdev)
{
	struct zfcp_erp_action *erp_action;
	struct zfcp_scsi_dev *zfcp_sdev;

	if (WARN_ON_ONCE(need != ZFCP_ERP_ACTION_REOPEN_LUN &&
			 need != ZFCP_ERP_ACTION_REOPEN_PORT &&
			 need != ZFCP_ERP_ACTION_REOPEN_PORT_FORCED &&
			 need != ZFCP_ERP_ACTION_REOPEN_ADAPTER))
		return NULL;

	switch (need) {
	case ZFCP_ERP_ACTION_REOPEN_LUN:
		zfcp_sdev = sdev_to_zfcp(sdev);
		if (!(act_status & ZFCP_STATUS_ERP_NO_REF))
			if (scsi_device_get(sdev))
				return NULL;
		atomic_or(ZFCP_STATUS_COMMON_ERP_INUSE,
				&zfcp_sdev->status);
		erp_action = &zfcp_sdev->erp_action;
		WARN_ON_ONCE(erp_action->port != port);
		WARN_ON_ONCE(erp_action->sdev != sdev);
		if (!(atomic_read(&zfcp_sdev->status) &
		      ZFCP_STATUS_COMMON_RUNNING))
			act_status |= ZFCP_STATUS_ERP_CLOSE_ONLY;
		break;

	case ZFCP_ERP_ACTION_REOPEN_PORT:
	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
		zfcp_port_get(port);
		zfcp_erp_action_dismiss_port(port);
		atomic_set_mask(ZFCP_STATUS_COMMON_ERP_INUSE, &port->status);
		erp_action = &port->erp_action;
		if (!(atomic_read(&port->status) & ZFCP_STATUS_COMMON_RUNNING))
			status = ZFCP_STATUS_ERP_CLOSE_ONLY;
		break;

	case ZFCP_ERP_ACTION_REOPEN_ADAPTER:
		zfcp_adapter_get(adapter);
		zfcp_erp_action_dismiss_adapter(adapter);
		atomic_set_mask(ZFCP_STATUS_COMMON_ERP_INUSE, &adapter->status);
		erp_action = &adapter->erp_action;
		if (!(atomic_read(&adapter->status) &
		      ZFCP_STATUS_COMMON_RUNNING))
			status = ZFCP_STATUS_ERP_CLOSE_ONLY;
		if (!get_device(&port->dev))
			return NULL;
		zfcp_erp_action_dismiss_port(port);
		atomic_or(ZFCP_STATUS_COMMON_ERP_INUSE, &port->status);
		erp_action = &port->erp_action;
		WARN_ON_ONCE(erp_action->port != port);
		WARN_ON_ONCE(erp_action->sdev != NULL);
		if (!(atomic_read(&port->status) & ZFCP_STATUS_COMMON_RUNNING))
			act_status |= ZFCP_STATUS_ERP_CLOSE_ONLY;
		break;

	case ZFCP_ERP_ACTION_REOPEN_ADAPTER:
		kref_get(&adapter->ref);
		zfcp_erp_action_dismiss_adapter(adapter);
		atomic_or(ZFCP_STATUS_COMMON_ERP_INUSE, &adapter->status);
		erp_action = &adapter->erp_action;
		WARN_ON_ONCE(erp_action->port != NULL);
		WARN_ON_ONCE(erp_action->sdev != NULL);
		if (!(atomic_read(&adapter->status) &
		      ZFCP_STATUS_COMMON_RUNNING))
			act_status |= ZFCP_STATUS_ERP_CLOSE_ONLY;
		break;

	default:
		return NULL;
	}

	memset(erp_action, 0, sizeof(struct zfcp_erp_action));
	erp_action->adapter = adapter;
	erp_action->port = port;
	erp_action->unit = unit;
	erp_action->action = need;
	erp_action->status = status;
	erp_action->adapter = adapter;
	WARN_ON_ONCE(erp_action->adapter != adapter);
	memset(&erp_action->list, 0, sizeof(erp_action->list));
	memset(&erp_action->timer, 0, sizeof(erp_action->timer));
	erp_action->step = ZFCP_ERP_STEP_UNINITIALIZED;
	erp_action->fsf_req_id = 0;
	erp_action->action = need;
	erp_action->status = act_status;

	return erp_action;
}

static int zfcp_erp_action_enqueue(int want, struct zfcp_adapter *adapter,
				   struct zfcp_port *port,
				   struct zfcp_unit *unit, u8 id, void *ref)
{
	int retval = 1, need;
	struct zfcp_erp_action *act = NULL;

	if (!(atomic_read(&adapter->status) &
	      ZFCP_STATUS_ADAPTER_ERP_THREAD_UP))
		return -EIO;

	need = zfcp_erp_required_act(want, adapter, port, unit);
	if (!need)
		goto out;

	atomic_set_mask(ZFCP_STATUS_ADAPTER_ERP_PENDING, &adapter->status);
	act = zfcp_erp_setup_act(need, adapter, port, unit);
	if (!act)
		goto out;
	++adapter->erp_total_count;
	list_add_tail(&act->list, &adapter->erp_ready_head);
	up(&adapter->erp_ready_sem);
	zfcp_rec_dbf_event_thread(1, adapter);
	retval = 0;
 out:
	zfcp_rec_dbf_event_trigger(id, ref, want, need, act,
				   adapter, port, unit);
				   struct scsi_device *sdev,
				   char *id, u32 act_status)
{
	int retval = 1, need;
	struct zfcp_erp_action *act;

	need = zfcp_erp_handle_failed(want, adapter, port, sdev);
	if (!need) {
		need = ZFCP_ERP_ACTION_FAILED; /* marker for trace */
		goto out;
	}

	if (!adapter->erp_thread) {
		need = ZFCP_ERP_ACTION_NONE; /* marker for trace */
		retval = -EIO;
		goto out;
	}

	need = zfcp_erp_required_act(want, adapter, port, sdev);
	if (!need)
		goto out;

	act = zfcp_erp_setup_act(need, act_status, adapter, port, sdev);
	if (!act) {
		need |= ZFCP_ERP_ACTION_NONE; /* marker for trace */
		goto out;
	}
	atomic_or(ZFCP_STATUS_ADAPTER_ERP_PENDING, &adapter->status);
	++adapter->erp_total_count;
	list_add_tail(&act->list, &adapter->erp_ready_head);
	wake_up(&adapter->erp_ready_wq);
	retval = 0;
 out:
	zfcp_dbf_rec_trig(id, adapter, port, sdev, want, need);
	return retval;
}

void zfcp_erp_port_forced_no_port_dbf(char *id, struct zfcp_adapter *adapter,
				      u64 port_name, u32 port_id)
{
	unsigned long flags;
	static /* don't waste stack */ struct zfcp_port tmpport;

	write_lock_irqsave(&adapter->erp_lock, flags);
	/* Stand-in zfcp port with fields just good enough for
	 * zfcp_dbf_rec_trig() and zfcp_dbf_set_common().
	 * Under lock because tmpport is static.
	 */
	atomic_set(&tmpport.status, -1); /* unknown */
	tmpport.wwpn = port_name;
	tmpport.d_id = port_id;
	zfcp_dbf_rec_trig(id, adapter, &tmpport, NULL,
			  ZFCP_ERP_ACTION_REOPEN_PORT_FORCED,
			  ZFCP_ERP_ACTION_NONE);
	write_unlock_irqrestore(&adapter->erp_lock, flags);
}

static int _zfcp_erp_adapter_reopen(struct zfcp_adapter *adapter,
				    int clear_mask, u8 id, void *ref)
{
	zfcp_erp_adapter_block(adapter, clear_mask);

	/* ensure propagation of failed status to new devices */
	if (atomic_read(&adapter->status) & ZFCP_STATUS_COMMON_ERP_FAILED) {
		zfcp_erp_adapter_failed(adapter, 13, NULL);
		return -EIO;
	}
	return zfcp_erp_action_enqueue(ZFCP_ERP_ACTION_REOPEN_ADAPTER,
				       adapter, NULL, NULL, id, ref);
				    int clear_mask, char *id)
{
	zfcp_erp_adapter_block(adapter, clear_mask);
	zfcp_scsi_schedule_rports_block(adapter);

	return zfcp_erp_action_enqueue(ZFCP_ERP_ACTION_REOPEN_ADAPTER,
				       adapter, NULL, NULL, id, 0);
}

/**
 * zfcp_erp_adapter_reopen - Reopen adapter.
 * @adapter: Adapter to reopen.
 * @clear: Status flags to clear.
 * @id: Id for debug trace event.
 * @ref: Reference for debug trace event.
 */
void zfcp_erp_adapter_reopen(struct zfcp_adapter *adapter, int clear,
			     u8 id, void *ref)
{
	unsigned long flags;

	read_lock_irqsave(&zfcp_data.config_lock, flags);
	write_lock(&adapter->erp_lock);
	_zfcp_erp_adapter_reopen(adapter, clear, id, ref);
	write_unlock(&adapter->erp_lock);
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);
 */
void zfcp_erp_adapter_reopen(struct zfcp_adapter *adapter, int clear, char *id)
{
	unsigned long flags;

	zfcp_erp_adapter_block(adapter, clear);
	zfcp_scsi_schedule_rports_block(adapter);

	write_lock_irqsave(&adapter->erp_lock, flags);
	zfcp_erp_action_enqueue(ZFCP_ERP_ACTION_REOPEN_ADAPTER, adapter,
				NULL, NULL, id, 0);
	write_unlock_irqrestore(&adapter->erp_lock, flags);
}

/**
 * zfcp_erp_adapter_shutdown - Shutdown adapter.
 * @adapter: Adapter to shut down.
 * @clear: Status flags to clear.
 * @id: Id for debug trace event.
 * @ref: Reference for debug trace event.
 */
void zfcp_erp_adapter_shutdown(struct zfcp_adapter *adapter, int clear,
			       u8 id, void *ref)
{
	int flags = ZFCP_STATUS_COMMON_RUNNING | ZFCP_STATUS_COMMON_ERP_FAILED;
	zfcp_erp_adapter_reopen(adapter, clear | flags, id, ref);
 */
void zfcp_erp_adapter_shutdown(struct zfcp_adapter *adapter, int clear,
			       char *id)
{
	int flags = ZFCP_STATUS_COMMON_RUNNING | ZFCP_STATUS_COMMON_ERP_FAILED;
	zfcp_erp_adapter_reopen(adapter, clear | flags, id);
}

/**
 * zfcp_erp_port_shutdown - Shutdown port
 * @port: Port to shut down.
 * @clear: Status flags to clear.
 * @id: Id for debug trace event.
 * @ref: Reference for debug trace event.
 */
void zfcp_erp_port_shutdown(struct zfcp_port *port, int clear, u8 id, void *ref)
{
	int flags = ZFCP_STATUS_COMMON_RUNNING | ZFCP_STATUS_COMMON_ERP_FAILED;
	zfcp_erp_port_reopen(port, clear | flags, id, ref);
}

/**
 * zfcp_erp_unit_shutdown - Shutdown unit
 * @unit: Unit to shut down.
 * @clear: Status flags to clear.
 * @id: Id for debug trace event.
 * @ref: Reference for debug trace event.
 */
void zfcp_erp_unit_shutdown(struct zfcp_unit *unit, int clear, u8 id, void *ref)
{
	int flags = ZFCP_STATUS_COMMON_RUNNING | ZFCP_STATUS_COMMON_ERP_FAILED;
	zfcp_erp_unit_reopen(unit, clear | flags, id, ref);
 */
void zfcp_erp_port_shutdown(struct zfcp_port *port, int clear, char *id)
{
	int flags = ZFCP_STATUS_COMMON_RUNNING | ZFCP_STATUS_COMMON_ERP_FAILED;
	zfcp_erp_port_reopen(port, clear | flags, id);
}

static void zfcp_erp_port_block(struct zfcp_port *port, int clear)
{
	zfcp_erp_modify_port_status(port, 17, NULL,
				    ZFCP_STATUS_COMMON_UNBLOCKED | clear,
				    ZFCP_CLEAR);
}

static void _zfcp_erp_port_forced_reopen(struct zfcp_port *port,
					 int clear, u8 id, void *ref)
{
	zfcp_erp_port_block(port, clear);
	zfcp_erp_clear_port_status(port,
				    ZFCP_STATUS_COMMON_UNBLOCKED | clear);
}

static void _zfcp_erp_port_forced_reopen(struct zfcp_port *port, int clear,
					 char *id)
{
	zfcp_erp_port_block(port, clear);
	zfcp_scsi_schedule_rport_block(port);

	zfcp_erp_action_enqueue(ZFCP_ERP_ACTION_REOPEN_PORT_FORCED,
				port->adapter, port, NULL, id, ref);
				port->adapter, port, NULL, id, 0);
}

/**
 * zfcp_erp_port_forced_reopen - Forced close of port and open again
 * @port: Port to force close and to reopen.
 * @id: Id for debug trace event.
 * @ref: Reference for debug trace event.
 */
void zfcp_erp_port_forced_reopen(struct zfcp_port *port, int clear, u8 id,
				 void *ref)
 * @clear: Status flags to clear.
 * @id: Id for debug trace event.
 */
void zfcp_erp_port_forced_reopen(struct zfcp_port *port, int clear, char *id)
{
	unsigned long flags;
	struct zfcp_adapter *adapter = port->adapter;

	read_lock_irqsave(&zfcp_data.config_lock, flags);
	write_lock(&adapter->erp_lock);
	_zfcp_erp_port_forced_reopen(port, clear, id, ref);
	write_unlock(&adapter->erp_lock);
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);
}

static int _zfcp_erp_port_reopen(struct zfcp_port *port, int clear, u8 id,
				 void *ref)
{
	zfcp_erp_port_block(port, clear);

	if (atomic_read(&port->status) & ZFCP_STATUS_COMMON_ERP_FAILED) {
		/* ensure propagation of failed status to new devices */
		zfcp_erp_port_failed(port, 14, NULL);
	write_lock_irqsave(&adapter->erp_lock, flags);
	_zfcp_erp_port_forced_reopen(port, clear, id);
	write_unlock_irqrestore(&adapter->erp_lock, flags);
}

static int _zfcp_erp_port_reopen(struct zfcp_port *port, int clear, char *id)
{
	zfcp_erp_port_block(port, clear);
	zfcp_scsi_schedule_rport_block(port);

	return zfcp_erp_action_enqueue(ZFCP_ERP_ACTION_REOPEN_PORT,
				       port->adapter, port, NULL, id, ref);
				       port->adapter, port, NULL, id, 0);
}

/**
 * zfcp_erp_port_reopen - trigger remote port recovery
 * @port: port to recover
 * @clear_mask: flags in port status to be cleared
 *
 * Returns 0 if recovery has been triggered, < 0 if not.
 */
int zfcp_erp_port_reopen(struct zfcp_port *port, int clear, u8 id, void *ref)
{
	unsigned long flags;
	int retval;
	struct zfcp_adapter *adapter = port->adapter;

	read_lock_irqsave(&zfcp_data.config_lock, flags);
	write_lock(&adapter->erp_lock);
	retval = _zfcp_erp_port_reopen(port, clear, id, ref);
	write_unlock(&adapter->erp_lock);
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);
 * @id: Id for debug trace event.
 *
 * Returns 0 if recovery has been triggered, < 0 if not.
 */
int zfcp_erp_port_reopen(struct zfcp_port *port, int clear, char *id)
{
	int retval;
	unsigned long flags;
	struct zfcp_adapter *adapter = port->adapter;

	write_lock_irqsave(&adapter->erp_lock, flags);
	retval = _zfcp_erp_port_reopen(port, clear, id);
	write_unlock_irqrestore(&adapter->erp_lock, flags);

	return retval;
}

static void zfcp_erp_unit_block(struct zfcp_unit *unit, int clear_mask)
{
	zfcp_erp_modify_unit_status(unit, 19, NULL,
				    ZFCP_STATUS_COMMON_UNBLOCKED | clear_mask,
				    ZFCP_CLEAR);
}

static void _zfcp_erp_unit_reopen(struct zfcp_unit *unit, int clear, u8 id,
				  void *ref)
{
	struct zfcp_adapter *adapter = unit->port->adapter;

	zfcp_erp_unit_block(unit, clear);

	if (atomic_read(&unit->status) & ZFCP_STATUS_COMMON_ERP_FAILED)
		return;

	zfcp_erp_action_enqueue(ZFCP_ERP_ACTION_REOPEN_UNIT,
				adapter, unit->port, unit, id, ref);
}

/**
 * zfcp_erp_unit_reopen - initiate reopen of a unit
 * @unit: unit to be reopened
 * @clear_mask: specifies flags in unit status to be cleared
 * Return: 0 on success, < 0 on error
 */
void zfcp_erp_unit_reopen(struct zfcp_unit *unit, int clear, u8 id, void *ref)
{
	unsigned long flags;
	struct zfcp_port *port = unit->port;
	struct zfcp_adapter *adapter = port->adapter;

	read_lock_irqsave(&zfcp_data.config_lock, flags);
	write_lock(&adapter->erp_lock);
	_zfcp_erp_unit_reopen(unit, clear, id, ref);
	write_unlock(&adapter->erp_lock);
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);
static void zfcp_erp_lun_block(struct scsi_device *sdev, int clear_mask)
{
	zfcp_erp_clear_lun_status(sdev,
				  ZFCP_STATUS_COMMON_UNBLOCKED | clear_mask);
}

static void _zfcp_erp_lun_reopen(struct scsi_device *sdev, int clear, char *id,
				 u32 act_status)
{
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(sdev);
	struct zfcp_adapter *adapter = zfcp_sdev->port->adapter;

	zfcp_erp_lun_block(sdev, clear);

	zfcp_erp_action_enqueue(ZFCP_ERP_ACTION_REOPEN_LUN, adapter,
				zfcp_sdev->port, sdev, id, act_status);
}

/**
 * zfcp_erp_lun_reopen - initiate reopen of a LUN
 * @sdev: SCSI device / LUN to be reopened
 * @clear_mask: specifies flags in LUN status to be cleared
 * @id: Id for debug trace event.
 *
 * Return: 0 on success, < 0 on error
 */
void zfcp_erp_lun_reopen(struct scsi_device *sdev, int clear, char *id)
{
	unsigned long flags;
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(sdev);
	struct zfcp_port *port = zfcp_sdev->port;
	struct zfcp_adapter *adapter = port->adapter;

	write_lock_irqsave(&adapter->erp_lock, flags);
	_zfcp_erp_lun_reopen(sdev, clear, id, 0);
	write_unlock_irqrestore(&adapter->erp_lock, flags);
}

/**
 * zfcp_erp_lun_shutdown - Shutdown LUN
 * @sdev: SCSI device / LUN to shut down.
 * @clear: Status flags to clear.
 * @id: Id for debug trace event.
 */
void zfcp_erp_lun_shutdown(struct scsi_device *sdev, int clear, char *id)
{
	int flags = ZFCP_STATUS_COMMON_RUNNING | ZFCP_STATUS_COMMON_ERP_FAILED;
	zfcp_erp_lun_reopen(sdev, clear | flags, id);
}

/**
 * zfcp_erp_lun_shutdown_wait - Shutdown LUN and wait for erp completion
 * @sdev: SCSI device / LUN to shut down.
 * @id: Id for debug trace event.
 *
 * Do not acquire a reference for the LUN when creating the ERP
 * action. It is safe, because this function waits for the ERP to
 * complete first. This allows to shutdown the LUN, even when the SCSI
 * device is in the state SDEV_DEL when scsi_device_get will fail.
 */
void zfcp_erp_lun_shutdown_wait(struct scsi_device *sdev, char *id)
{
	unsigned long flags;
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(sdev);
	struct zfcp_port *port = zfcp_sdev->port;
	struct zfcp_adapter *adapter = port->adapter;
	int clear = ZFCP_STATUS_COMMON_RUNNING | ZFCP_STATUS_COMMON_ERP_FAILED;

	write_lock_irqsave(&adapter->erp_lock, flags);
	_zfcp_erp_lun_reopen(sdev, clear, id, ZFCP_STATUS_ERP_NO_REF);
	write_unlock_irqrestore(&adapter->erp_lock, flags);

	zfcp_erp_wait(adapter);
}

static int status_change_set(unsigned long mask, atomic_t *status)
{
	return (atomic_read(status) ^ mask) & mask;
}

static int status_change_clear(unsigned long mask, atomic_t *status)
{
	return atomic_read(status) & mask;
}

static void zfcp_erp_adapter_unblock(struct zfcp_adapter *adapter)
{
	if (status_change_set(ZFCP_STATUS_COMMON_UNBLOCKED, &adapter->status))
		zfcp_rec_dbf_event_adapter(16, NULL, adapter);
	atomic_set_mask(ZFCP_STATUS_COMMON_UNBLOCKED, &adapter->status);
static void zfcp_erp_adapter_unblock(struct zfcp_adapter *adapter)
{
	if (status_change_set(ZFCP_STATUS_COMMON_UNBLOCKED, &adapter->status))
		zfcp_dbf_rec_run("eraubl1", &adapter->erp_action);
	atomic_or(ZFCP_STATUS_COMMON_UNBLOCKED, &adapter->status);
}

static void zfcp_erp_port_unblock(struct zfcp_port *port)
{
	if (status_change_set(ZFCP_STATUS_COMMON_UNBLOCKED, &port->status))
		zfcp_rec_dbf_event_port(18, NULL, port);
	atomic_set_mask(ZFCP_STATUS_COMMON_UNBLOCKED, &port->status);
}

static void zfcp_erp_unit_unblock(struct zfcp_unit *unit)
{
	if (status_change_set(ZFCP_STATUS_COMMON_UNBLOCKED, &unit->status))
		zfcp_rec_dbf_event_unit(20, NULL, unit);
	atomic_set_mask(ZFCP_STATUS_COMMON_UNBLOCKED, &unit->status);
		zfcp_dbf_rec_run("erpubl1", &port->erp_action);
	atomic_or(ZFCP_STATUS_COMMON_UNBLOCKED, &port->status);
}

static void zfcp_erp_lun_unblock(struct scsi_device *sdev)
{
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(sdev);

	if (status_change_set(ZFCP_STATUS_COMMON_UNBLOCKED, &zfcp_sdev->status))
		zfcp_dbf_rec_run("erlubl1", &sdev_to_zfcp(sdev)->erp_action);
	atomic_or(ZFCP_STATUS_COMMON_UNBLOCKED, &zfcp_sdev->status);
}

static void zfcp_erp_action_to_running(struct zfcp_erp_action *erp_action)
{
	list_move(&erp_action->list, &erp_action->adapter->erp_running_head);
	zfcp_rec_dbf_event_action(145, erp_action);
	zfcp_dbf_rec_run("erator1", erp_action);
}

static void zfcp_erp_strategy_check_fsfreq(struct zfcp_erp_action *act)
{
	struct zfcp_adapter *adapter = act->adapter;

	if (!act->fsf_req)
		return;

	spin_lock(&adapter->req_list_lock);
	if (zfcp_reqlist_find_safe(adapter, act->fsf_req) &&
	    act->fsf_req->erp_action == act) {
		if (act->status & (ZFCP_STATUS_ERP_DISMISSED |
				   ZFCP_STATUS_ERP_TIMEDOUT)) {
			act->fsf_req->status |= ZFCP_STATUS_FSFREQ_DISMISSED;
			zfcp_rec_dbf_event_action(142, act);
		}
		if (act->status & ZFCP_STATUS_ERP_TIMEDOUT)
			zfcp_rec_dbf_event_action(143, act);
		if (act->fsf_req->status & (ZFCP_STATUS_FSFREQ_COMPLETED |
					    ZFCP_STATUS_FSFREQ_DISMISSED))
			act->fsf_req = NULL;
	} else
		act->fsf_req = NULL;
	spin_unlock(&adapter->req_list_lock);
	struct zfcp_fsf_req *req;

	if (!act->fsf_req_id)
		return;

	spin_lock(&adapter->req_list->lock);
	req = _zfcp_reqlist_find(adapter->req_list, act->fsf_req_id);
	if (req && req->erp_action == act) {
		if (act->status & (ZFCP_STATUS_ERP_DISMISSED |
				   ZFCP_STATUS_ERP_TIMEDOUT)) {
			req->status |= ZFCP_STATUS_FSFREQ_DISMISSED;
			zfcp_dbf_rec_run("erscf_1", act);
			req->erp_action = NULL;
		}
		if (act->status & ZFCP_STATUS_ERP_TIMEDOUT)
			zfcp_dbf_rec_run("erscf_2", act);
		if (req->status & ZFCP_STATUS_FSFREQ_DISMISSED)
			act->fsf_req_id = 0;
	} else
		act->fsf_req_id = 0;
	spin_unlock(&adapter->req_list->lock);
}

/**
 * zfcp_erp_notify - Trigger ERP action.
 * @erp_action: ERP action to continue.
 * @set_mask: ERP action status flags to set.
 */
void zfcp_erp_notify(struct zfcp_erp_action *erp_action, unsigned long set_mask)
{
	struct zfcp_adapter *adapter = erp_action->adapter;
	unsigned long flags;

	write_lock_irqsave(&adapter->erp_lock, flags);
	if (zfcp_erp_action_exists(erp_action) == ZFCP_ERP_ACTION_RUNNING) {
		erp_action->status |= set_mask;
		zfcp_erp_action_ready(erp_action);
	}
	write_unlock_irqrestore(&adapter->erp_lock, flags);
}

/**
 * zfcp_erp_timeout_handler - Trigger ERP action from timed out ERP request
 * @data: ERP action (from timer data)
 */
void zfcp_erp_timeout_handler(unsigned long data)
{
	struct zfcp_erp_action *act = (struct zfcp_erp_action *) data;
	zfcp_erp_notify(act, ZFCP_STATUS_ERP_TIMEDOUT);
}

static void zfcp_erp_memwait_handler(unsigned long data)
{
	zfcp_erp_notify((struct zfcp_erp_action *)data, 0);
}

static void zfcp_erp_strategy_memwait(struct zfcp_erp_action *erp_action)
{
	init_timer(&erp_action->timer);
	erp_action->timer.function = zfcp_erp_memwait_handler;
	erp_action->timer.data = (unsigned long) erp_action;
	erp_action->timer.expires = jiffies + HZ;
	add_timer(&erp_action->timer);
}

void zfcp_erp_port_forced_reopen_all(struct zfcp_adapter *adapter,
				     int clear, char *dbftag)
{
	unsigned long flags;
	struct zfcp_port *port;

	write_lock_irqsave(&adapter->erp_lock, flags);
	read_lock(&adapter->port_list_lock);
	list_for_each_entry(port, &adapter->port_list, list)
		_zfcp_erp_port_forced_reopen(port, clear, dbftag);
	read_unlock(&adapter->port_list_lock);
	write_unlock_irqrestore(&adapter->erp_lock, flags);
}

static void _zfcp_erp_port_reopen_all(struct zfcp_adapter *adapter,
				      int clear, u8 id, void *ref)
{
	struct zfcp_port *port;

	list_for_each_entry(port, &adapter->port_list_head, list)
		if (!(atomic_read(&port->status) & ZFCP_STATUS_PORT_WKA))
			_zfcp_erp_port_reopen(port, clear, id, ref);
}

static void _zfcp_erp_unit_reopen_all(struct zfcp_port *port, int clear, u8 id,
				      void *ref)
{
	struct zfcp_unit *unit;

	list_for_each_entry(unit, &port->unit_list_head, list)
		_zfcp_erp_unit_reopen(unit, clear, id, ref);
}

static void zfcp_erp_strategy_followup_actions(struct zfcp_erp_action *act)
{
	struct zfcp_adapter *adapter = act->adapter;
	struct zfcp_port *port = act->port;
	struct zfcp_unit *unit = act->unit;
	u32 status = act->status;

	/* initiate follow-up actions depending on success of finished action */
	switch (act->action) {

	case ZFCP_ERP_ACTION_REOPEN_ADAPTER:
		if (status == ZFCP_ERP_SUCCEEDED)
			_zfcp_erp_port_reopen_all(adapter, 0, 70, NULL);
		else
			_zfcp_erp_adapter_reopen(adapter, 0, 71, NULL);
		break;

	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
		if (status == ZFCP_ERP_SUCCEEDED)
			_zfcp_erp_port_reopen(port, 0, 72, NULL);
		else
			_zfcp_erp_adapter_reopen(adapter, 0, 73, NULL);
		break;

	case ZFCP_ERP_ACTION_REOPEN_PORT:
		if (status == ZFCP_ERP_SUCCEEDED)
			_zfcp_erp_unit_reopen_all(port, 0, 74, NULL);
		else
			_zfcp_erp_port_forced_reopen(port, 0, 75, NULL);
		break;

	case ZFCP_ERP_ACTION_REOPEN_UNIT:
		if (status != ZFCP_ERP_SUCCEEDED)
			_zfcp_erp_port_reopen(unit->port, 0, 76, NULL);
				      int clear, char *id)
{
	struct zfcp_port *port;

	read_lock(&adapter->port_list_lock);
	list_for_each_entry(port, &adapter->port_list, list)
		_zfcp_erp_port_reopen(port, clear, id);
	read_unlock(&adapter->port_list_lock);
}

static void _zfcp_erp_lun_reopen_all(struct zfcp_port *port, int clear,
				     char *id)
{
	struct scsi_device *sdev;

	spin_lock(port->adapter->scsi_host->host_lock);
	__shost_for_each_device(sdev, port->adapter->scsi_host)
		if (sdev_to_zfcp(sdev)->port == port)
			_zfcp_erp_lun_reopen(sdev, clear, id, 0);
	spin_unlock(port->adapter->scsi_host->host_lock);
}

static void zfcp_erp_strategy_followup_failed(struct zfcp_erp_action *act)
{
	switch (act->action) {
	case ZFCP_ERP_ACTION_REOPEN_ADAPTER:
		_zfcp_erp_adapter_reopen(act->adapter, 0, "ersff_1");
		break;
	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
		_zfcp_erp_port_forced_reopen(act->port, 0, "ersff_2");
		break;
	case ZFCP_ERP_ACTION_REOPEN_PORT:
		_zfcp_erp_port_reopen(act->port, 0, "ersff_3");
		break;
	case ZFCP_ERP_ACTION_REOPEN_LUN:
		_zfcp_erp_lun_reopen(act->sdev, 0, "ersff_4", 0);
		break;
	}
}

static void zfcp_erp_strategy_followup_success(struct zfcp_erp_action *act)
{
	switch (act->action) {
	case ZFCP_ERP_ACTION_REOPEN_ADAPTER:
		_zfcp_erp_port_reopen_all(act->adapter, 0, "ersfs_1");
		break;
	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
		_zfcp_erp_port_reopen(act->port, 0, "ersfs_2");
		break;
	case ZFCP_ERP_ACTION_REOPEN_PORT:
		_zfcp_erp_lun_reopen_all(act->port, 0, "ersfs_3");
		break;
	}
}

static void zfcp_erp_wakeup(struct zfcp_adapter *adapter)
{
	unsigned long flags;

	read_lock_irqsave(&zfcp_data.config_lock, flags);
	read_lock(&adapter->erp_lock);
	if (list_empty(&adapter->erp_ready_head) &&
	    list_empty(&adapter->erp_running_head)) {
			atomic_clear_mask(ZFCP_STATUS_ADAPTER_ERP_PENDING,
					  &adapter->status);
			wake_up(&adapter->erp_done_wqh);
	}
	read_unlock(&adapter->erp_lock);
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);
}

static int zfcp_erp_adapter_strategy_open_qdio(struct zfcp_erp_action *act)
{
	if (zfcp_qdio_open(act->adapter))
		return ZFCP_ERP_FAILED;
	init_waitqueue_head(&act->adapter->request_wq);
	atomic_set_mask(ZFCP_STATUS_ADAPTER_QDIOUP, &act->adapter->status);
	return ZFCP_ERP_SUCCEEDED;
	read_lock_irqsave(&adapter->erp_lock, flags);
	if (list_empty(&adapter->erp_ready_head) &&
	    list_empty(&adapter->erp_running_head)) {
			atomic_andnot(ZFCP_STATUS_ADAPTER_ERP_PENDING,
					  &adapter->status);
			wake_up(&adapter->erp_done_wqh);
	}
	read_unlock_irqrestore(&adapter->erp_lock, flags);
}

static void zfcp_erp_enqueue_ptp_port(struct zfcp_adapter *adapter)
{
	struct zfcp_port *port;
	port = zfcp_port_enqueue(adapter, adapter->peer_wwpn, 0,
				 adapter->peer_d_id);
	if (IS_ERR(port)) /* error or port already attached */
		return;
	_zfcp_erp_port_reopen(port, 0, 150, NULL);
	_zfcp_erp_port_reopen(port, 0, "ereptp1");
	zfcp_erp_port_reopen(port, 0, "ereptp1");
}

static int zfcp_erp_adapter_strat_fsf_xconf(struct zfcp_erp_action *erp_action)
{
	int retries;
	int sleep = 1;
	struct zfcp_adapter *adapter = erp_action->adapter;

	atomic_clear_mask(ZFCP_STATUS_ADAPTER_XCONFIG_OK, &adapter->status);

	for (retries = 7; retries; retries--) {
		atomic_clear_mask(ZFCP_STATUS_ADAPTER_HOST_CON_INIT,
	atomic_andnot(ZFCP_STATUS_ADAPTER_XCONFIG_OK, &adapter->status);

	for (retries = 7; retries; retries--) {
		atomic_andnot(ZFCP_STATUS_ADAPTER_HOST_CON_INIT,
				  &adapter->status);
		write_lock_irq(&adapter->erp_lock);
		zfcp_erp_action_to_running(erp_action);
		write_unlock_irq(&adapter->erp_lock);
		if (zfcp_fsf_exchange_config_data(erp_action)) {
			atomic_clear_mask(ZFCP_STATUS_ADAPTER_HOST_CON_INIT,
			atomic_andnot(ZFCP_STATUS_ADAPTER_HOST_CON_INIT,
					  &adapter->status);
			return ZFCP_ERP_FAILED;
		}

		zfcp_rec_dbf_event_thread_lock(6, adapter);
		down(&adapter->erp_ready_sem);
		zfcp_rec_dbf_event_thread_lock(7, adapter);
		wait_event(adapter->erp_ready_wq,
			   !list_empty(&adapter->erp_ready_head));
		if (erp_action->status & ZFCP_STATUS_ERP_TIMEDOUT)
			break;

		if (!(atomic_read(&adapter->status) &
		      ZFCP_STATUS_ADAPTER_HOST_CON_INIT))
			break;

		ssleep(sleep);
		sleep *= 2;
	}

	atomic_clear_mask(ZFCP_STATUS_ADAPTER_HOST_CON_INIT,
	atomic_andnot(ZFCP_STATUS_ADAPTER_HOST_CON_INIT,
			  &adapter->status);

	if (!(atomic_read(&adapter->status) & ZFCP_STATUS_ADAPTER_XCONFIG_OK))
		return ZFCP_ERP_FAILED;

	if (fc_host_port_type(adapter->scsi_host) == FC_PORTTYPE_PTP)
		zfcp_erp_enqueue_ptp_port(adapter);

	return ZFCP_ERP_SUCCEEDED;
}

static int zfcp_erp_adapter_strategy_open_fsf_xport(struct zfcp_erp_action *act)
{
	int ret;
	struct zfcp_adapter *adapter = act->adapter;

	atomic_clear_mask(ZFCP_STATUS_ADAPTER_XPORT_OK, &adapter->status);

	write_lock_irq(&adapter->erp_lock);
	zfcp_erp_action_to_running(act);
	write_unlock_irq(&adapter->erp_lock);

	ret = zfcp_fsf_exchange_port_data(act);
	if (ret == -EOPNOTSUPP)
		return ZFCP_ERP_SUCCEEDED;
	if (ret)
		return ZFCP_ERP_FAILED;

	zfcp_rec_dbf_event_thread_lock(8, adapter);
	down(&adapter->erp_ready_sem);
	zfcp_rec_dbf_event_thread_lock(9, adapter);
	zfcp_dbf_rec_run("erasox1", act);
	wait_event(adapter->erp_ready_wq,
		   !list_empty(&adapter->erp_ready_head));
	zfcp_dbf_rec_run("erasox2", act);
	if (act->status & ZFCP_STATUS_ERP_TIMEDOUT)
		return ZFCP_ERP_FAILED;

	return ZFCP_ERP_SUCCEEDED;
}

static int zfcp_erp_adapter_strategy_open_fsf(struct zfcp_erp_action *act)
{
	if (zfcp_erp_adapter_strat_fsf_xconf(act) == ZFCP_ERP_FAILED)
		return ZFCP_ERP_FAILED;

	if (zfcp_erp_adapter_strategy_open_fsf_xport(act) == ZFCP_ERP_FAILED)
		return ZFCP_ERP_FAILED;

	atomic_set(&act->adapter->stat_miss, 16);
	if (mempool_resize(act->adapter->pool.sr_data,
			   act->adapter->stat_read_buf_num))
		return ZFCP_ERP_FAILED;

	if (mempool_resize(act->adapter->pool.status_read_req,
			   act->adapter->stat_read_buf_num))
		return ZFCP_ERP_FAILED;

	atomic_set(&act->adapter->stat_miss, act->adapter->stat_read_buf_num);
	if (zfcp_status_read_refill(act->adapter))
		return ZFCP_ERP_FAILED;

	return ZFCP_ERP_SUCCEEDED;
}

static int zfcp_erp_adapter_strategy_generic(struct zfcp_erp_action *act,
					     int close)
{
	int retval = ZFCP_ERP_SUCCEEDED;
	struct zfcp_adapter *adapter = act->adapter;

	if (close)
		goto close_only;

	retval = zfcp_erp_adapter_strategy_open_qdio(act);
	if (retval != ZFCP_ERP_SUCCEEDED)
		goto failed_qdio;

	retval = zfcp_erp_adapter_strategy_open_fsf(act);
	if (retval != ZFCP_ERP_SUCCEEDED)
		goto failed_openfcp;

	atomic_set_mask(ZFCP_STATUS_COMMON_OPEN, &act->adapter->status);
	schedule_work(&act->adapter->scan_work);

	return ZFCP_ERP_SUCCEEDED;

 close_only:
	atomic_clear_mask(ZFCP_STATUS_COMMON_OPEN,
			  &act->adapter->status);

 failed_openfcp:
	/* close queues to ensure that buffers are not accessed by adapter */
	zfcp_qdio_close(adapter);
	zfcp_fsf_req_dismiss_all(adapter);
	adapter->fsf_req_seq_no = 0;
	/* all ports and units are closed */
	zfcp_erp_modify_adapter_status(adapter, 24, NULL,
				       ZFCP_STATUS_COMMON_OPEN, ZFCP_CLEAR);
 failed_qdio:
	atomic_clear_mask(ZFCP_STATUS_ADAPTER_XCONFIG_OK |
			  ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED |
			  ZFCP_STATUS_ADAPTER_XPORT_OK,
			  &act->adapter->status);
	return retval;
static void zfcp_erp_adapter_strategy_close(struct zfcp_erp_action *act)
{
	struct zfcp_adapter *adapter = act->adapter;

	/* close queues to ensure that buffers are not accessed by adapter */
	zfcp_qdio_close(adapter->qdio);
	zfcp_fsf_req_dismiss_all(adapter);
	adapter->fsf_req_seq_no = 0;
	zfcp_fc_wka_ports_force_offline(adapter->gs);
	/* all ports and LUNs are closed */
	zfcp_erp_clear_adapter_status(adapter, ZFCP_STATUS_COMMON_OPEN);

	atomic_andnot(ZFCP_STATUS_ADAPTER_XCONFIG_OK |
			  ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED, &adapter->status);
}

static int zfcp_erp_adapter_strategy_open(struct zfcp_erp_action *act)
{
	struct zfcp_adapter *adapter = act->adapter;

	if (zfcp_qdio_open(adapter->qdio)) {
		atomic_andnot(ZFCP_STATUS_ADAPTER_XCONFIG_OK |
				  ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED,
				  &adapter->status);
		return ZFCP_ERP_FAILED;
	}

	if (zfcp_erp_adapter_strategy_open_fsf(act)) {
		zfcp_erp_adapter_strategy_close(act);
		return ZFCP_ERP_FAILED;
	}

	atomic_or(ZFCP_STATUS_COMMON_OPEN, &adapter->status);

	return ZFCP_ERP_SUCCEEDED;
}

static int zfcp_erp_adapter_strategy(struct zfcp_erp_action *act)
{
	int retval;

	atomic_set_mask(ZFCP_STATUS_COMMON_CLOSING, &act->adapter->status);
	zfcp_erp_adapter_strategy_generic(act, 1); /* close */
	atomic_clear_mask(ZFCP_STATUS_COMMON_CLOSING, &act->adapter->status);
	if (act->status & ZFCP_STATUS_ERP_CLOSE_ONLY)
		return ZFCP_ERP_EXIT;

	atomic_set_mask(ZFCP_STATUS_COMMON_OPENING, &act->adapter->status);
	retval = zfcp_erp_adapter_strategy_generic(act, 0); /* open */
	atomic_clear_mask(ZFCP_STATUS_COMMON_OPENING, &act->adapter->status);

	if (retval == ZFCP_ERP_FAILED)
		ssleep(8);

	return retval;
	struct zfcp_adapter *adapter = act->adapter;

	if (atomic_read(&adapter->status) & ZFCP_STATUS_COMMON_OPEN) {
		zfcp_erp_adapter_strategy_close(act);
		if (act->status & ZFCP_STATUS_ERP_CLOSE_ONLY)
			return ZFCP_ERP_EXIT;
	}

	if (zfcp_erp_adapter_strategy_open(act)) {
		ssleep(8);
		return ZFCP_ERP_FAILED;
	}

	return ZFCP_ERP_SUCCEEDED;
}

static int zfcp_erp_port_forced_strategy_close(struct zfcp_erp_action *act)
{
	int retval;

	retval = zfcp_fsf_close_physical_port(act);
	if (retval == -ENOMEM)
		return ZFCP_ERP_NOMEM;
	act->step = ZFCP_ERP_STEP_PHYS_PORT_CLOSING;
	if (retval)
		return ZFCP_ERP_FAILED;

	return ZFCP_ERP_CONTINUES;
}

static void zfcp_erp_port_strategy_clearstati(struct zfcp_port *port)
{
	atomic_clear_mask(ZFCP_STATUS_COMMON_OPENING |
			  ZFCP_STATUS_COMMON_CLOSING |
			  ZFCP_STATUS_COMMON_ACCESS_DENIED |
			  ZFCP_STATUS_PORT_DID_DID |
			  ZFCP_STATUS_PORT_PHYS_CLOSING |
			  ZFCP_STATUS_PORT_INVALID_WWPN,
			  &port->status);
}

static int zfcp_erp_port_forced_strategy(struct zfcp_erp_action *erp_action)
{
	struct zfcp_port *port = erp_action->port;
	int status = atomic_read(&port->status);

	switch (erp_action->step) {
	case ZFCP_ERP_STEP_UNINITIALIZED:
		zfcp_erp_port_strategy_clearstati(port);
		if ((status & ZFCP_STATUS_PORT_PHYS_OPEN) &&
		    (status & ZFCP_STATUS_COMMON_OPEN))
			return zfcp_erp_port_forced_strategy_close(erp_action);
		else
			return ZFCP_ERP_FAILED;

	case ZFCP_ERP_STEP_PHYS_PORT_CLOSING:
		if (status & ZFCP_STATUS_PORT_PHYS_OPEN)
		if (!(status & ZFCP_STATUS_PORT_PHYS_OPEN))
			return ZFCP_ERP_SUCCEEDED;
	}
	return ZFCP_ERP_FAILED;
}

static int zfcp_erp_port_strategy_close(struct zfcp_erp_action *erp_action)
{
	int retval;

	retval = zfcp_fsf_close_port(erp_action);
	if (retval == -ENOMEM)
		return ZFCP_ERP_NOMEM;
	erp_action->step = ZFCP_ERP_STEP_PORT_CLOSING;
	if (retval)
		return ZFCP_ERP_FAILED;
	return ZFCP_ERP_CONTINUES;
}

static int zfcp_erp_port_strategy_open_port(struct zfcp_erp_action *erp_action)
{
	int retval;

	retval = zfcp_fsf_open_port(erp_action);
	if (retval == -ENOMEM)
		return ZFCP_ERP_NOMEM;
	erp_action->step = ZFCP_ERP_STEP_PORT_OPENING;
	if (retval)
		return ZFCP_ERP_FAILED;
	return ZFCP_ERP_CONTINUES;
}

static void zfcp_erp_port_strategy_open_ns_wake(struct zfcp_erp_action *ns_act)
{
	unsigned long flags;
	struct zfcp_adapter *adapter = ns_act->adapter;
	struct zfcp_erp_action *act, *tmp;
	int status;

	read_lock_irqsave(&adapter->erp_lock, flags);
	list_for_each_entry_safe(act, tmp, &adapter->erp_running_head, list) {
		if (act->step == ZFCP_ERP_STEP_NAMESERVER_OPEN) {
			status = atomic_read(&adapter->nameserver_port->status);
			if (status & ZFCP_STATUS_COMMON_ERP_FAILED)
				zfcp_erp_port_failed(act->port, 27, NULL);
			zfcp_erp_action_ready(act);
		}
	}
	read_unlock_irqrestore(&adapter->erp_lock, flags);
}

static int zfcp_erp_port_strategy_open_nameserver(struct zfcp_erp_action *act)
{
	int retval;

	switch (act->step) {
	case ZFCP_ERP_STEP_UNINITIALIZED:
	case ZFCP_ERP_STEP_PHYS_PORT_CLOSING:
	case ZFCP_ERP_STEP_PORT_CLOSING:
		return zfcp_erp_port_strategy_open_port(act);

	case ZFCP_ERP_STEP_PORT_OPENING:
		if (atomic_read(&act->port->status) & ZFCP_STATUS_COMMON_OPEN)
			retval = ZFCP_ERP_SUCCEEDED;
		else
			retval = ZFCP_ERP_FAILED;
		/* this is needed anyway  */
		zfcp_erp_port_strategy_open_ns_wake(act);
		return retval;

	default:
		return ZFCP_ERP_FAILED;
	}
}

static int zfcp_erp_port_strategy_open_lookup(struct zfcp_erp_action *act)
{
	int retval;

	retval = zfcp_fc_ns_gid_pn_request(act);
	if (retval == -ENOMEM)
		return ZFCP_ERP_NOMEM;
	act->step = ZFCP_ERP_STEP_NAMESERVER_LOOKUP;
	if (retval)
		return ZFCP_ERP_FAILED;
	return ZFCP_ERP_CONTINUES;
}

static int zfcp_erp_open_ptp_port(struct zfcp_erp_action *act)
{
	struct zfcp_adapter *adapter = act->adapter;
	struct zfcp_port *port = act->port;

	if (port->wwpn != adapter->peer_wwpn) {
		dev_err(&adapter->ccw_device->dev,
			"Failed to open port 0x%016Lx, "
			"Peer WWPN 0x%016Lx does not "
			"match.\n", port->wwpn,
			adapter->peer_wwpn);
		zfcp_erp_port_failed(port, 25, NULL);
		return ZFCP_ERP_FAILED;
	}
	port->d_id = adapter->peer_d_id;
	atomic_set_mask(ZFCP_STATUS_PORT_DID_DID, &port->status);
		zfcp_erp_set_port_status(port, ZFCP_STATUS_COMMON_ERP_FAILED);
		return ZFCP_ERP_FAILED;
	}
	port->d_id = adapter->peer_d_id;
	return zfcp_erp_port_strategy_open_port(act);
}

static int zfcp_erp_port_strategy_open_common(struct zfcp_erp_action *act)
{
	struct zfcp_adapter *adapter = act->adapter;
	struct zfcp_port *port = act->port;
	struct zfcp_port *ns_port = adapter->nameserver_port;
	int p_status = atomic_read(&port->status);

	switch (act->step) {
	case ZFCP_ERP_STEP_UNINITIALIZED:
	case ZFCP_ERP_STEP_PHYS_PORT_CLOSING:
	case ZFCP_ERP_STEP_PORT_CLOSING:
		if (fc_host_port_type(adapter->scsi_host) == FC_PORTTYPE_PTP)
			return zfcp_erp_open_ptp_port(act);
		if (!ns_port) {
			dev_err(&adapter->ccw_device->dev,
				"Nameserver port unavailable.\n");
			return ZFCP_ERP_FAILED;
		}
		if (!(atomic_read(&ns_port->status) &
		      ZFCP_STATUS_COMMON_UNBLOCKED)) {
			/* nameserver port may live again */
			atomic_set_mask(ZFCP_STATUS_COMMON_RUNNING,
					&ns_port->status);
			if (zfcp_erp_port_reopen(ns_port, 0, 77, act) >= 0) {
				act->step = ZFCP_ERP_STEP_NAMESERVER_OPEN;
				return ZFCP_ERP_CONTINUES;
			}
			return ZFCP_ERP_FAILED;
		}
		/* else nameserver port is already open, fall through */
	case ZFCP_ERP_STEP_NAMESERVER_OPEN:
		if (!(atomic_read(&ns_port->status) & ZFCP_STATUS_COMMON_OPEN))
			return ZFCP_ERP_FAILED;
		return zfcp_erp_port_strategy_open_lookup(act);

	case ZFCP_ERP_STEP_NAMESERVER_LOOKUP:
		if (!(p_status & ZFCP_STATUS_PORT_DID_DID)) {
			if (p_status & (ZFCP_STATUS_PORT_INVALID_WWPN)) {
				zfcp_erp_port_failed(port, 26, NULL);
				return ZFCP_ERP_EXIT;
			}
			return ZFCP_ERP_FAILED;
		if (!port->d_id) {
			zfcp_fc_trigger_did_lookup(port);
			return ZFCP_ERP_EXIT;
		}
		return zfcp_erp_port_strategy_open_port(act);

	case ZFCP_ERP_STEP_PORT_OPENING:
		/* D_ID might have changed during open */
		if ((p_status & ZFCP_STATUS_COMMON_OPEN) &&
		    (p_status & ZFCP_STATUS_PORT_DID_DID))
			return ZFCP_ERP_SUCCEEDED;
		if (p_status & ZFCP_STATUS_COMMON_OPEN) {
			if (!port->d_id) {
				zfcp_fc_trigger_did_lookup(port);
				return ZFCP_ERP_EXIT;
			}
			return ZFCP_ERP_SUCCEEDED;
		}
		if (port->d_id && !(p_status & ZFCP_STATUS_COMMON_NOESC)) {
			port->d_id = 0;
			return ZFCP_ERP_FAILED;
		}
		/* fall through otherwise */
	}
	return ZFCP_ERP_FAILED;
}

static int zfcp_erp_port_strategy_open(struct zfcp_erp_action *act)
{
	if (atomic_read(&act->port->status) & (ZFCP_STATUS_PORT_WKA))
		return zfcp_erp_port_strategy_open_nameserver(act);
	return zfcp_erp_port_strategy_open_common(act);
}

static int zfcp_erp_port_strategy(struct zfcp_erp_action *erp_action)
{
	struct zfcp_port *port = erp_action->port;

	switch (erp_action->step) {
	case ZFCP_ERP_STEP_UNINITIALIZED:
		zfcp_erp_port_strategy_clearstati(port);
		if (atomic_read(&port->status) & ZFCP_STATUS_COMMON_OPEN)
static int zfcp_erp_port_strategy(struct zfcp_erp_action *erp_action)
{
	struct zfcp_port *port = erp_action->port;
	int p_status = atomic_read(&port->status);

	if ((p_status & ZFCP_STATUS_COMMON_NOESC) &&
	    !(p_status & ZFCP_STATUS_COMMON_OPEN))
		goto close_init_done;

	switch (erp_action->step) {
	case ZFCP_ERP_STEP_UNINITIALIZED:
		if (p_status & ZFCP_STATUS_COMMON_OPEN)
			return zfcp_erp_port_strategy_close(erp_action);
		break;

	case ZFCP_ERP_STEP_PORT_CLOSING:
		if (atomic_read(&port->status) & ZFCP_STATUS_COMMON_OPEN)
			return ZFCP_ERP_FAILED;
		break;
	}
	if (erp_action->status & ZFCP_STATUS_ERP_CLOSE_ONLY)
		return ZFCP_ERP_EXIT;
	else
		return zfcp_erp_port_strategy_open(erp_action);

	return ZFCP_ERP_FAILED;
}

static void zfcp_erp_unit_strategy_clearstati(struct zfcp_unit *unit)
{
	atomic_clear_mask(ZFCP_STATUS_COMMON_OPENING |
			  ZFCP_STATUS_COMMON_CLOSING |
			  ZFCP_STATUS_COMMON_ACCESS_DENIED |
			  ZFCP_STATUS_UNIT_SHARED |
			  ZFCP_STATUS_UNIT_READONLY,
			  &unit->status);
}

static int zfcp_erp_unit_strategy_close(struct zfcp_erp_action *erp_action)
{
	int retval = zfcp_fsf_close_unit(erp_action);
	if (retval == -ENOMEM)
		return ZFCP_ERP_NOMEM;
	erp_action->step = ZFCP_ERP_STEP_UNIT_CLOSING;
		if (p_status & ZFCP_STATUS_COMMON_OPEN)
			return ZFCP_ERP_FAILED;
		break;
	}

close_init_done:
	if (erp_action->status & ZFCP_STATUS_ERP_CLOSE_ONLY)
		return ZFCP_ERP_EXIT;

	return zfcp_erp_port_strategy_open_common(erp_action);
}

static void zfcp_erp_lun_strategy_clearstati(struct scsi_device *sdev)
{
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(sdev);

	atomic_andnot(ZFCP_STATUS_COMMON_ACCESS_DENIED,
			  &zfcp_sdev->status);
}

static int zfcp_erp_lun_strategy_close(struct zfcp_erp_action *erp_action)
{
	int retval = zfcp_fsf_close_lun(erp_action);
	if (retval == -ENOMEM)
		return ZFCP_ERP_NOMEM;
	erp_action->step = ZFCP_ERP_STEP_LUN_CLOSING;
	if (retval)
		return ZFCP_ERP_FAILED;
	return ZFCP_ERP_CONTINUES;
}

static int zfcp_erp_unit_strategy_open(struct zfcp_erp_action *erp_action)
{
	int retval = zfcp_fsf_open_unit(erp_action);
	if (retval == -ENOMEM)
		return ZFCP_ERP_NOMEM;
	erp_action->step = ZFCP_ERP_STEP_UNIT_OPENING;
static int zfcp_erp_lun_strategy_open(struct zfcp_erp_action *erp_action)
{
	int retval = zfcp_fsf_open_lun(erp_action);
	if (retval == -ENOMEM)
		return ZFCP_ERP_NOMEM;
	erp_action->step = ZFCP_ERP_STEP_LUN_OPENING;
	if (retval)
		return  ZFCP_ERP_FAILED;
	return ZFCP_ERP_CONTINUES;
}

static int zfcp_erp_unit_strategy(struct zfcp_erp_action *erp_action)
{
	struct zfcp_unit *unit = erp_action->unit;

	switch (erp_action->step) {
	case ZFCP_ERP_STEP_UNINITIALIZED:
		zfcp_erp_unit_strategy_clearstati(unit);
		if (atomic_read(&unit->status) & ZFCP_STATUS_COMMON_OPEN)
			return zfcp_erp_unit_strategy_close(erp_action);
		/* already closed, fall through */
	case ZFCP_ERP_STEP_UNIT_CLOSING:
		if (atomic_read(&unit->status) & ZFCP_STATUS_COMMON_OPEN)
			return ZFCP_ERP_FAILED;
		if (erp_action->status & ZFCP_STATUS_ERP_CLOSE_ONLY)
			return ZFCP_ERP_EXIT;
		return zfcp_erp_unit_strategy_open(erp_action);

	case ZFCP_ERP_STEP_UNIT_OPENING:
		if (atomic_read(&unit->status) & ZFCP_STATUS_COMMON_OPEN)
static int zfcp_erp_lun_strategy(struct zfcp_erp_action *erp_action)
{
	struct scsi_device *sdev = erp_action->sdev;
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(sdev);

	switch (erp_action->step) {
	case ZFCP_ERP_STEP_UNINITIALIZED:
		zfcp_erp_lun_strategy_clearstati(sdev);
		if (atomic_read(&zfcp_sdev->status) & ZFCP_STATUS_COMMON_OPEN)
			return zfcp_erp_lun_strategy_close(erp_action);
		/* already closed, fall through */
	case ZFCP_ERP_STEP_LUN_CLOSING:
		if (atomic_read(&zfcp_sdev->status) & ZFCP_STATUS_COMMON_OPEN)
			return ZFCP_ERP_FAILED;
		if (erp_action->status & ZFCP_STATUS_ERP_CLOSE_ONLY)
			return ZFCP_ERP_EXIT;
		return zfcp_erp_lun_strategy_open(erp_action);

	case ZFCP_ERP_STEP_LUN_OPENING:
		if (atomic_read(&zfcp_sdev->status) & ZFCP_STATUS_COMMON_OPEN)
			return ZFCP_ERP_SUCCEEDED;
	}
	return ZFCP_ERP_FAILED;
}

static int zfcp_erp_strategy_check_unit(struct zfcp_unit *unit, int result)
{
	switch (result) {
	case ZFCP_ERP_SUCCEEDED :
		atomic_set(&unit->erp_counter, 0);
		zfcp_erp_unit_unblock(unit);
		break;
	case ZFCP_ERP_FAILED :
		atomic_inc(&unit->erp_counter);
		if (atomic_read(&unit->erp_counter) > ZFCP_MAX_ERPS)
			zfcp_erp_unit_failed(unit, 21, NULL);
		break;
	}

	if (atomic_read(&unit->status) & ZFCP_STATUS_COMMON_ERP_FAILED) {
		zfcp_erp_unit_block(unit, 0);
static int zfcp_erp_strategy_check_lun(struct scsi_device *sdev, int result)
{
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(sdev);

	switch (result) {
	case ZFCP_ERP_SUCCEEDED :
		atomic_set(&zfcp_sdev->erp_counter, 0);
		zfcp_erp_lun_unblock(sdev);
		break;
	case ZFCP_ERP_FAILED :
		atomic_inc(&zfcp_sdev->erp_counter);
		if (atomic_read(&zfcp_sdev->erp_counter) > ZFCP_MAX_ERPS) {
			dev_err(&zfcp_sdev->port->adapter->ccw_device->dev,
				"ERP failed for LUN 0x%016Lx on "
				"port 0x%016Lx\n",
				(unsigned long long)zfcp_scsi_dev_lun(sdev),
				(unsigned long long)zfcp_sdev->port->wwpn);
			zfcp_erp_set_lun_status(sdev,
						ZFCP_STATUS_COMMON_ERP_FAILED);
		}
		break;
	}

	if (atomic_read(&zfcp_sdev->status) & ZFCP_STATUS_COMMON_ERP_FAILED) {
		zfcp_erp_lun_block(sdev, 0);
		result = ZFCP_ERP_EXIT;
	}
	return result;
}

static int zfcp_erp_strategy_check_port(struct zfcp_port *port, int result)
{
	switch (result) {
	case ZFCP_ERP_SUCCEEDED :
		atomic_set(&port->erp_counter, 0);
		zfcp_erp_port_unblock(port);
		break;

	case ZFCP_ERP_FAILED :
		if (atomic_read(&port->status) & ZFCP_STATUS_COMMON_NOESC) {
			zfcp_erp_port_block(port, 0);
			result = ZFCP_ERP_EXIT;
		}
		atomic_inc(&port->erp_counter);
		if (atomic_read(&port->erp_counter) > ZFCP_MAX_ERPS)
			zfcp_erp_port_failed(port, 22, NULL);
		if (atomic_read(&port->erp_counter) > ZFCP_MAX_ERPS) {
			dev_err(&port->adapter->ccw_device->dev,
				"ERP failed for remote port 0x%016Lx\n",
				(unsigned long long)port->wwpn);
			zfcp_erp_set_port_status(port,
					 ZFCP_STATUS_COMMON_ERP_FAILED);
		}
		break;
	}

	if (atomic_read(&port->status) & ZFCP_STATUS_COMMON_ERP_FAILED) {
		zfcp_erp_port_block(port, 0);
		result = ZFCP_ERP_EXIT;
	}
	return result;
}

static int zfcp_erp_strategy_check_adapter(struct zfcp_adapter *adapter,
					   int result)
{
	switch (result) {
	case ZFCP_ERP_SUCCEEDED :
		atomic_set(&adapter->erp_counter, 0);
		zfcp_erp_adapter_unblock(adapter);
		break;

	case ZFCP_ERP_FAILED :
		atomic_inc(&adapter->erp_counter);
		if (atomic_read(&adapter->erp_counter) > ZFCP_MAX_ERPS)
			zfcp_erp_adapter_failed(adapter, 23, NULL);
		if (atomic_read(&adapter->erp_counter) > ZFCP_MAX_ERPS) {
			dev_err(&adapter->ccw_device->dev,
				"ERP cannot recover an error "
				"on the FCP device\n");
			zfcp_erp_set_adapter_status(adapter,
					    ZFCP_STATUS_COMMON_ERP_FAILED);
		}
		break;
	}

	if (atomic_read(&adapter->status) & ZFCP_STATUS_COMMON_ERP_FAILED) {
		zfcp_erp_adapter_block(adapter, 0);
		result = ZFCP_ERP_EXIT;
	}
	return result;
}

static int zfcp_erp_strategy_check_target(struct zfcp_erp_action *erp_action,
					  int result)
{
	struct zfcp_adapter *adapter = erp_action->adapter;
	struct zfcp_port *port = erp_action->port;
	struct zfcp_unit *unit = erp_action->unit;

	switch (erp_action->action) {

	case ZFCP_ERP_ACTION_REOPEN_UNIT:
		result = zfcp_erp_strategy_check_unit(unit, result);
	struct scsi_device *sdev = erp_action->sdev;

	switch (erp_action->action) {

	case ZFCP_ERP_ACTION_REOPEN_LUN:
		result = zfcp_erp_strategy_check_lun(sdev, result);
		break;

	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
	case ZFCP_ERP_ACTION_REOPEN_PORT:
		result = zfcp_erp_strategy_check_port(port, result);
		break;

	case ZFCP_ERP_ACTION_REOPEN_ADAPTER:
		result = zfcp_erp_strategy_check_adapter(adapter, result);
		break;
	}
	return result;
}

static int zfcp_erp_strat_change_det(atomic_t *target_status, u32 erp_status)
{
	int status = atomic_read(target_status);

	if ((status & ZFCP_STATUS_COMMON_RUNNING) &&
	    (erp_status & ZFCP_STATUS_ERP_CLOSE_ONLY))
		return 1; /* take it online */

	if (!(status & ZFCP_STATUS_COMMON_RUNNING) &&
	    !(erp_status & ZFCP_STATUS_ERP_CLOSE_ONLY))
		return 1; /* take it offline */

	return 0;
}

static int zfcp_erp_strategy_statechange(struct zfcp_erp_action *act, int ret)
{
	int action = act->action;
	struct zfcp_adapter *adapter = act->adapter;
	struct zfcp_port *port = act->port;
	struct zfcp_unit *unit = act->unit;
	struct scsi_device *sdev = act->sdev;
	struct zfcp_scsi_dev *zfcp_sdev;
	u32 erp_status = act->status;

	switch (action) {
	case ZFCP_ERP_ACTION_REOPEN_ADAPTER:
		if (zfcp_erp_strat_change_det(&adapter->status, erp_status)) {
			_zfcp_erp_adapter_reopen(adapter,
						 ZFCP_STATUS_COMMON_ERP_FAILED,
						 67, NULL);
						 "ersscg1");
			return ZFCP_ERP_EXIT;
		}
		break;

	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
	case ZFCP_ERP_ACTION_REOPEN_PORT:
		if (zfcp_erp_strat_change_det(&port->status, erp_status)) {
			_zfcp_erp_port_reopen(port,
					      ZFCP_STATUS_COMMON_ERP_FAILED,
					      68, NULL);
					      "ersscg2");
			return ZFCP_ERP_EXIT;
		}
		break;

	case ZFCP_ERP_ACTION_REOPEN_UNIT:
		if (zfcp_erp_strat_change_det(&unit->status, erp_status)) {
			_zfcp_erp_unit_reopen(unit,
					      ZFCP_STATUS_COMMON_ERP_FAILED,
					      69, NULL);
	case ZFCP_ERP_ACTION_REOPEN_LUN:
		zfcp_sdev = sdev_to_zfcp(sdev);
		if (zfcp_erp_strat_change_det(&zfcp_sdev->status, erp_status)) {
			_zfcp_erp_lun_reopen(sdev,
					     ZFCP_STATUS_COMMON_ERP_FAILED,
					     "ersscg3", 0);
			return ZFCP_ERP_EXIT;
		}
		break;
	}
	return ret;
}

static void zfcp_erp_action_dequeue(struct zfcp_erp_action *erp_action)
{
	struct zfcp_adapter *adapter = erp_action->adapter;
	struct zfcp_scsi_dev *zfcp_sdev;

	adapter->erp_total_count--;
	if (erp_action->status & ZFCP_STATUS_ERP_LOWMEM) {
		adapter->erp_low_mem_count--;
		erp_action->status &= ~ZFCP_STATUS_ERP_LOWMEM;
	}

	list_del(&erp_action->list);
	zfcp_rec_dbf_event_action(144, erp_action);

	switch (erp_action->action) {
	case ZFCP_ERP_ACTION_REOPEN_UNIT:
		atomic_clear_mask(ZFCP_STATUS_COMMON_ERP_INUSE,
				  &erp_action->unit->status);
	zfcp_dbf_rec_run("eractd1", erp_action);

	switch (erp_action->action) {
	case ZFCP_ERP_ACTION_REOPEN_LUN:
		zfcp_sdev = sdev_to_zfcp(erp_action->sdev);
		atomic_andnot(ZFCP_STATUS_COMMON_ERP_INUSE,
				  &zfcp_sdev->status);
		break;

	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
	case ZFCP_ERP_ACTION_REOPEN_PORT:
		atomic_clear_mask(ZFCP_STATUS_COMMON_ERP_INUSE,
		atomic_andnot(ZFCP_STATUS_COMMON_ERP_INUSE,
				  &erp_action->port->status);
		break;

	case ZFCP_ERP_ACTION_REOPEN_ADAPTER:
		atomic_clear_mask(ZFCP_STATUS_COMMON_ERP_INUSE,
		atomic_andnot(ZFCP_STATUS_COMMON_ERP_INUSE,
				  &erp_action->adapter->status);
		break;
	}
}

struct zfcp_erp_add_work {
	struct zfcp_unit  *unit;
	struct work_struct work;
};

static void zfcp_erp_scsi_scan(struct work_struct *work)
{
	struct zfcp_erp_add_work *p =
		container_of(work, struct zfcp_erp_add_work, work);
	struct zfcp_unit *unit = p->unit;
	struct fc_rport *rport = unit->port->rport;
	scsi_scan_target(&rport->dev, 0, rport->scsi_target_id,
			 unit->scsi_lun, 0);
	atomic_clear_mask(ZFCP_STATUS_UNIT_SCSI_WORK_PENDING, &unit->status);
	zfcp_unit_put(unit);
	kfree(p);
}

static void zfcp_erp_schedule_work(struct zfcp_unit *unit)
{
	struct zfcp_erp_add_work *p;

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p) {
		dev_err(&unit->port->adapter->ccw_device->dev,
			"Out of resources. Could not register unit "
			"0x%016Lx on port 0x%016Lx with SCSI stack.\n",
			unit->fcp_lun, unit->port->wwpn);
		return;
	}

	zfcp_unit_get(unit);
	atomic_set_mask(ZFCP_STATUS_UNIT_SCSI_WORK_PENDING, &unit->status);
	INIT_WORK(&p->work, zfcp_erp_scsi_scan);
	p->unit = unit;
	schedule_work(&p->work);
}

static void zfcp_erp_rport_register(struct zfcp_port *port)
{
	struct fc_rport_identifiers ids;
	ids.node_name = port->wwnn;
	ids.port_name = port->wwpn;
	ids.port_id = port->d_id;
	ids.roles = FC_RPORT_ROLE_FCP_TARGET;
	port->rport = fc_remote_port_add(port->adapter->scsi_host, 0, &ids);
	if (!port->rport) {
		dev_err(&port->adapter->ccw_device->dev,
			"Failed registration of rport "
			"0x%016Lx.\n", port->wwpn);
		return;
	}

	scsi_target_unblock(&port->rport->dev);
	port->rport->maxframe_size = port->maxframe_size;
	port->rport->supported_classes = port->supported_classes;
}

static void zfcp_erp_rports_del(struct zfcp_adapter *adapter)
{
	struct zfcp_port *port;
	list_for_each_entry(port, &adapter->port_list_head, list)
		if (port->rport && !(atomic_read(&port->status) &
					ZFCP_STATUS_PORT_WKA)) {
			fc_remote_port_delete(port->rport);
			port->rport = NULL;
		}
/**
 * zfcp_erp_try_rport_unblock - unblock rport if no more/new recovery
 * @port: zfcp_port whose fc_rport we should try to unblock
 */
static void zfcp_erp_try_rport_unblock(struct zfcp_port *port)
{
	unsigned long flags;
	struct zfcp_adapter *adapter = port->adapter;
	int port_status;
	struct Scsi_Host *shost = adapter->scsi_host;
	struct scsi_device *sdev;

	write_lock_irqsave(&adapter->erp_lock, flags);
	port_status = atomic_read(&port->status);
	if ((port_status & ZFCP_STATUS_COMMON_UNBLOCKED)    == 0 ||
	    (port_status & (ZFCP_STATUS_COMMON_ERP_INUSE |
			    ZFCP_STATUS_COMMON_ERP_FAILED)) != 0) {
		/* new ERP of severity >= port triggered elsewhere meanwhile or
		 * local link down (adapter erp_failed but not clear unblock)
		 */
		zfcp_dbf_rec_run_lvl(4, "ertru_p", &port->erp_action);
		write_unlock_irqrestore(&adapter->erp_lock, flags);
		return;
	}
	spin_lock(shost->host_lock);
	__shost_for_each_device(sdev, shost) {
		struct zfcp_scsi_dev *zsdev = sdev_to_zfcp(sdev);
		int lun_status;

		if (sdev->sdev_state == SDEV_DEL ||
		    sdev->sdev_state == SDEV_CANCEL)
			continue;
		if (zsdev->port != port)
			continue;
		/* LUN under port of interest */
		lun_status = atomic_read(&zsdev->status);
		if ((lun_status & ZFCP_STATUS_COMMON_ERP_FAILED) != 0)
			continue; /* unblock rport despite failed LUNs */
		/* LUN recovery not given up yet [maybe follow-up pending] */
		if ((lun_status & ZFCP_STATUS_COMMON_UNBLOCKED) == 0 ||
		    (lun_status & ZFCP_STATUS_COMMON_ERP_INUSE) != 0) {
			/* LUN blocked:
			 * not yet unblocked [LUN recovery pending]
			 * or meanwhile blocked [new LUN recovery triggered]
			 */
			zfcp_dbf_rec_run_lvl(4, "ertru_l", &zsdev->erp_action);
			spin_unlock(shost->host_lock);
			write_unlock_irqrestore(&adapter->erp_lock, flags);
			return;
		}
	}
	/* now port has no child or all children have completed recovery,
	 * and no ERP of severity >= port was meanwhile triggered elsewhere
	 */
	zfcp_scsi_schedule_rport_register(port);
	spin_unlock(shost->host_lock);
	write_unlock_irqrestore(&adapter->erp_lock, flags);
}

static void zfcp_erp_action_cleanup(struct zfcp_erp_action *act, int result)
{
	struct zfcp_adapter *adapter = act->adapter;
	struct zfcp_port *port = act->port;
	struct zfcp_unit *unit = act->unit;

	switch (act->action) {
	case ZFCP_ERP_ACTION_REOPEN_UNIT:
		if ((result == ZFCP_ERP_SUCCEEDED) &&
		    !unit->device && port->rport) {
			atomic_set_mask(ZFCP_STATUS_UNIT_REGISTERED,
					&unit->status);
			if (!(atomic_read(&unit->status) &
			      ZFCP_STATUS_UNIT_SCSI_WORK_PENDING))
				zfcp_erp_schedule_work(unit);
		}
		zfcp_unit_put(unit);
		break;

	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
	case ZFCP_ERP_ACTION_REOPEN_PORT:
		if (atomic_read(&port->status) & ZFCP_STATUS_PORT_NO_WWPN) {
			zfcp_port_put(port);
			return;
		}
		if ((result == ZFCP_ERP_SUCCEEDED) && !port->rport)
			zfcp_erp_rport_register(port);
		if ((result != ZFCP_ERP_SUCCEEDED) && port->rport) {
			fc_remote_port_delete(port->rport);
			port->rport = NULL;
		}
		zfcp_port_put(port);
		break;

	case ZFCP_ERP_ACTION_REOPEN_ADAPTER:
		if (result != ZFCP_ERP_SUCCEEDED)
			zfcp_erp_rports_del(adapter);
		zfcp_adapter_put(adapter);
	struct scsi_device *sdev = act->sdev;

	switch (act->action) {
	case ZFCP_ERP_ACTION_REOPEN_LUN:
		if (!(act->status & ZFCP_STATUS_ERP_NO_REF))
			scsi_device_put(sdev);
		zfcp_erp_try_rport_unblock(port);
		break;

	case ZFCP_ERP_ACTION_REOPEN_PORT:
		/* This switch case might also happen after a forced reopen
		 * was successfully done and thus overwritten with a new
		 * non-forced reopen at `ersfs_2'. In this case, we must not
		 * do the clean-up of the non-forced version.
		 */
		if (act->step != ZFCP_ERP_STEP_UNINITIALIZED)
			if (result == ZFCP_ERP_SUCCEEDED)
				zfcp_erp_try_rport_unblock(port);
		/* fall through */
	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
		put_device(&port->dev);
		break;

	case ZFCP_ERP_ACTION_REOPEN_ADAPTER:
		if (result == ZFCP_ERP_SUCCEEDED) {
			register_service_level(&adapter->service_level);
			zfcp_fc_conditional_port_scan(adapter);
			queue_work(adapter->work_queue, &adapter->ns_up_work);
		} else
			unregister_service_level(&adapter->service_level);

		kref_put(&adapter->ref, zfcp_adapter_release);
		break;
	}
}

static int zfcp_erp_strategy_do_action(struct zfcp_erp_action *erp_action)
{
	switch (erp_action->action) {
	case ZFCP_ERP_ACTION_REOPEN_ADAPTER:
		return zfcp_erp_adapter_strategy(erp_action);
	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
		return zfcp_erp_port_forced_strategy(erp_action);
	case ZFCP_ERP_ACTION_REOPEN_PORT:
		return zfcp_erp_port_strategy(erp_action);
	case ZFCP_ERP_ACTION_REOPEN_UNIT:
		return zfcp_erp_unit_strategy(erp_action);
	case ZFCP_ERP_ACTION_REOPEN_LUN:
		return zfcp_erp_lun_strategy(erp_action);
	}
	return ZFCP_ERP_FAILED;
}

static int zfcp_erp_strategy(struct zfcp_erp_action *erp_action)
{
	int retval;
	struct zfcp_adapter *adapter = erp_action->adapter;
	unsigned long flags;

	read_lock_irqsave(&zfcp_data.config_lock, flags);
	write_lock(&adapter->erp_lock);

	unsigned long flags;
	struct zfcp_adapter *adapter = erp_action->adapter;

	kref_get(&adapter->ref);

	write_lock_irqsave(&adapter->erp_lock, flags);
	zfcp_erp_strategy_check_fsfreq(erp_action);

	if (erp_action->status & ZFCP_STATUS_ERP_DISMISSED) {
		zfcp_erp_action_dequeue(erp_action);
		retval = ZFCP_ERP_DISMISSED;
		goto unlock;
	}

	zfcp_erp_action_to_running(erp_action);

	/* no lock to allow for blocking operations */
	write_unlock(&adapter->erp_lock);
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);
	retval = zfcp_erp_strategy_do_action(erp_action);
	read_lock_irqsave(&zfcp_data.config_lock, flags);
	write_lock(&adapter->erp_lock);
	if (erp_action->status & ZFCP_STATUS_ERP_TIMEDOUT) {
		retval = ZFCP_ERP_FAILED;
		goto check_target;
	}

	zfcp_erp_action_to_running(erp_action);

	/* no lock to allow for blocking operations */
	write_unlock_irqrestore(&adapter->erp_lock, flags);
	retval = zfcp_erp_strategy_do_action(erp_action);
	write_lock_irqsave(&adapter->erp_lock, flags);

	if (erp_action->status & ZFCP_STATUS_ERP_DISMISSED)
		retval = ZFCP_ERP_CONTINUES;

	switch (retval) {
	case ZFCP_ERP_NOMEM:
		if (!(erp_action->status & ZFCP_STATUS_ERP_LOWMEM)) {
			++adapter->erp_low_mem_count;
			erp_action->status |= ZFCP_STATUS_ERP_LOWMEM;
		}
		if (adapter->erp_total_count == adapter->erp_low_mem_count)
			_zfcp_erp_adapter_reopen(adapter, 0, 66, NULL);
			_zfcp_erp_adapter_reopen(adapter, 0, "erstgy1");
		else {
			zfcp_erp_strategy_memwait(erp_action);
			retval = ZFCP_ERP_CONTINUES;
		}
		goto unlock;

	case ZFCP_ERP_CONTINUES:
		if (erp_action->status & ZFCP_STATUS_ERP_LOWMEM) {
			--adapter->erp_low_mem_count;
			erp_action->status &= ~ZFCP_STATUS_ERP_LOWMEM;
		}
		goto unlock;
	}

check_target:
	retval = zfcp_erp_strategy_check_target(erp_action, retval);
	zfcp_erp_action_dequeue(erp_action);
	retval = zfcp_erp_strategy_statechange(erp_action, retval);
	if (retval == ZFCP_ERP_EXIT)
		goto unlock;
	zfcp_erp_strategy_followup_actions(erp_action);

 unlock:
	write_unlock(&adapter->erp_lock);
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);
	if (retval == ZFCP_ERP_SUCCEEDED)
		zfcp_erp_strategy_followup_success(erp_action);
	if (retval == ZFCP_ERP_FAILED)
		zfcp_erp_strategy_followup_failed(erp_action);

 unlock:
	write_unlock_irqrestore(&adapter->erp_lock, flags);

	if (retval != ZFCP_ERP_CONTINUES)
		zfcp_erp_action_cleanup(erp_action, retval);

	kref_put(&adapter->ref, zfcp_adapter_release);
	return retval;
}

static int zfcp_erp_thread(void *data)
{
	struct zfcp_adapter *adapter = (struct zfcp_adapter *) data;
	struct list_head *next;
	struct zfcp_erp_action *act;
	unsigned long flags;

	daemonize("zfcperp%s", adapter->ccw_device->dev.bus_id);
	/* Block all signals */
	siginitsetinv(&current->blocked, 0);
	atomic_set_mask(ZFCP_STATUS_ADAPTER_ERP_THREAD_UP, &adapter->status);
	wake_up(&adapter->erp_thread_wqh);

	while (!(atomic_read(&adapter->status) &
		 ZFCP_STATUS_ADAPTER_ERP_THREAD_KILL)) {
	for (;;) {
		wait_event_interruptible(adapter->erp_ready_wq,
			   !list_empty(&adapter->erp_ready_head) ||
			   kthread_should_stop());

		if (kthread_should_stop())
			break;

		write_lock_irqsave(&adapter->erp_lock, flags);
		next = adapter->erp_ready_head.next;
		write_unlock_irqrestore(&adapter->erp_lock, flags);

		if (next != &adapter->erp_ready_head) {
			act = list_entry(next, struct zfcp_erp_action, list);

			/* there is more to come after dismission, no notify */
			if (zfcp_erp_strategy(act) != ZFCP_ERP_DISMISSED)
				zfcp_erp_wakeup(adapter);
		}

		zfcp_rec_dbf_event_thread(4, adapter);
		down_interruptible(&adapter->erp_ready_sem);
		zfcp_rec_dbf_event_thread(5, adapter);
	}

	atomic_clear_mask(ZFCP_STATUS_ADAPTER_ERP_THREAD_UP, &adapter->status);
	wake_up(&adapter->erp_thread_wqh);

	}

	return 0;
}

/**
 * zfcp_erp_thread_setup - Start ERP thread for adapter
 * @adapter: Adapter to start the ERP thread for
 *
 * Returns 0 on success or error code from kernel_thread()
 */
int zfcp_erp_thread_setup(struct zfcp_adapter *adapter)
{
	int retval;

	atomic_clear_mask(ZFCP_STATUS_ADAPTER_ERP_THREAD_UP, &adapter->status);
	retval = kernel_thread(zfcp_erp_thread, adapter, SIGCHLD);
	if (retval < 0) {
		dev_err(&adapter->ccw_device->dev,
			"Creation of ERP thread failed.\n");
		return retval;
	}
	wait_event(adapter->erp_thread_wqh,
		   atomic_read(&adapter->status) &
			ZFCP_STATUS_ADAPTER_ERP_THREAD_UP);
	struct task_struct *thread;

	thread = kthread_run(zfcp_erp_thread, adapter, "zfcperp%s",
			     dev_name(&adapter->ccw_device->dev));
	if (IS_ERR(thread)) {
		dev_err(&adapter->ccw_device->dev,
			"Creating an ERP thread for the FCP device failed.\n");
		return PTR_ERR(thread);
	}

	adapter->erp_thread = thread;
	return 0;
}

/**
 * zfcp_erp_thread_kill - Stop ERP thread.
 * @adapter: Adapter where the ERP thread should be stopped.
 *
 * The caller of this routine ensures that the specified adapter has
 * been shut down and that this operation has been completed. Thus,
 * there are no pending erp_actions which would need to be handled
 * here.
 */
void zfcp_erp_thread_kill(struct zfcp_adapter *adapter)
{
	atomic_set_mask(ZFCP_STATUS_ADAPTER_ERP_THREAD_KILL, &adapter->status);
	up(&adapter->erp_ready_sem);
	zfcp_rec_dbf_event_thread_lock(2, adapter);

	wait_event(adapter->erp_thread_wqh,
		   !(atomic_read(&adapter->status) &
				ZFCP_STATUS_ADAPTER_ERP_THREAD_UP));

	atomic_clear_mask(ZFCP_STATUS_ADAPTER_ERP_THREAD_KILL,
			  &adapter->status);
}

/**
 * zfcp_erp_adapter_failed - Set adapter status to failed.
 * @adapter: Failed adapter.
 * @id: Event id for debug trace.
 * @ref: Reference for debug trace.
 */
void zfcp_erp_adapter_failed(struct zfcp_adapter *adapter, u8 id, void *ref)
{
	zfcp_erp_modify_adapter_status(adapter, id, ref,
				       ZFCP_STATUS_COMMON_ERP_FAILED, ZFCP_SET);
	dev_err(&adapter->ccw_device->dev, "Adapter ERP failed.\n");
}

/**
 * zfcp_erp_port_failed - Set port status to failed.
 * @port: Failed port.
 * @id: Event id for debug trace.
 * @ref: Reference for debug trace.
 */
void zfcp_erp_port_failed(struct zfcp_port *port, u8 id, void *ref)
{
	zfcp_erp_modify_port_status(port, id, ref,
				    ZFCP_STATUS_COMMON_ERP_FAILED, ZFCP_SET);

	if (atomic_read(&port->status) & ZFCP_STATUS_PORT_WKA)
		dev_err(&port->adapter->ccw_device->dev,
			"Port ERP failed for WKA port d_id=0x%06x.\n",
			port->d_id);
	else
		dev_err(&port->adapter->ccw_device->dev,
			"Port ERP failed for port wwpn=0x%016Lx.\n",
			port->wwpn);
}

/**
 * zfcp_erp_unit_failed - Set unit status to failed.
 * @unit: Failed unit.
 * @id: Event id for debug trace.
 * @ref: Reference for debug trace.
 */
void zfcp_erp_unit_failed(struct zfcp_unit *unit, u8 id, void *ref)
{
	zfcp_erp_modify_unit_status(unit, id, ref,
				    ZFCP_STATUS_COMMON_ERP_FAILED, ZFCP_SET);

	dev_err(&unit->port->adapter->ccw_device->dev,
		"Unit ERP failed for unit 0x%016Lx on port 0x%016Lx.\n",
		unit->fcp_lun, unit->port->wwpn);
	kthread_stop(adapter->erp_thread);
	adapter->erp_thread = NULL;
	WARN_ON(!list_empty(&adapter->erp_ready_head));
	WARN_ON(!list_empty(&adapter->erp_running_head));
}

/**
 * zfcp_erp_wait - wait for completion of error recovery on an adapter
 * @adapter: adapter for which to wait for completion of its error recovery
 */
void zfcp_erp_wait(struct zfcp_adapter *adapter)
{
	wait_event(adapter->erp_done_wqh,
		   !(atomic_read(&adapter->status) &
			ZFCP_STATUS_ADAPTER_ERP_PENDING));
}

/**
 * zfcp_erp_modify_adapter_status - change adapter status bits
 * @adapter: adapter to change the status
 * @id: id for the debug trace
 * @ref: reference for the debug trace
 * @mask: status bits to change
 * @set_or_clear: ZFCP_SET or ZFCP_CLEAR
 *
 * Changes in common status bits are propagated to attached ports and units.
 */
void zfcp_erp_modify_adapter_status(struct zfcp_adapter *adapter, u8 id,
				    void *ref, u32 mask, int set_or_clear)
{
	struct zfcp_port *port;
	u32 common_mask = mask & ZFCP_COMMON_FLAGS;

	if (set_or_clear == ZFCP_SET) {
		if (status_change_set(mask, &adapter->status))
			zfcp_rec_dbf_event_adapter(id, ref, adapter);
		atomic_set_mask(mask, &adapter->status);
	} else {
		if (status_change_clear(mask, &adapter->status))
			zfcp_rec_dbf_event_adapter(id, ref, adapter);
		atomic_clear_mask(mask, &adapter->status);
		if (mask & ZFCP_STATUS_COMMON_ERP_FAILED)
			atomic_set(&adapter->erp_counter, 0);
	}

	if (common_mask)
		list_for_each_entry(port, &adapter->port_list_head, list)
			zfcp_erp_modify_port_status(port, id, ref, common_mask,
						    set_or_clear);
}

/**
 * zfcp_erp_modify_port_status - change port status bits
 * @port: port to change the status bits
 * @id: id for the debug trace
 * @ref: reference for the debug trace
 * @mask: status bits to change
 * @set_or_clear: ZFCP_SET or ZFCP_CLEAR
 *
 * Changes in common status bits are propagated to attached units.
 */
void zfcp_erp_modify_port_status(struct zfcp_port *port, u8 id, void *ref,
				 u32 mask, int set_or_clear)
{
	struct zfcp_unit *unit;
	u32 common_mask = mask & ZFCP_COMMON_FLAGS;

	if (set_or_clear == ZFCP_SET) {
		if (status_change_set(mask, &port->status))
			zfcp_rec_dbf_event_port(id, ref, port);
		atomic_set_mask(mask, &port->status);
	} else {
		if (status_change_clear(mask, &port->status))
			zfcp_rec_dbf_event_port(id, ref, port);
		atomic_clear_mask(mask, &port->status);
		if (mask & ZFCP_STATUS_COMMON_ERP_FAILED)
			atomic_set(&port->erp_counter, 0);
	}

	if (common_mask)
		list_for_each_entry(unit, &port->unit_list_head, list)
			zfcp_erp_modify_unit_status(unit, id, ref, common_mask,
						    set_or_clear);
}

/**
 * zfcp_erp_modify_unit_status - change unit status bits
 * @unit: unit to change the status bits
 * @id: id for the debug trace
 * @ref: reference for the debug trace
 * @mask: status bits to change
 * @set_or_clear: ZFCP_SET or ZFCP_CLEAR
 */
void zfcp_erp_modify_unit_status(struct zfcp_unit *unit, u8 id, void *ref,
				 u32 mask, int set_or_clear)
{
	if (set_or_clear == ZFCP_SET) {
		if (status_change_set(mask, &unit->status))
			zfcp_rec_dbf_event_unit(id, ref, unit);
		atomic_set_mask(mask, &unit->status);
	} else {
		if (status_change_clear(mask, &unit->status))
			zfcp_rec_dbf_event_unit(id, ref, unit);
		atomic_clear_mask(mask, &unit->status);
		if (mask & ZFCP_STATUS_COMMON_ERP_FAILED) {
			atomic_set(&unit->erp_counter, 0);
		}
	}
}

/**
 * zfcp_erp_port_boxed - Mark port as "boxed" and start ERP
 * @port: The "boxed" port.
 * @id: The debug trace id.
 * @id: Reference for the debug trace.
 */
void zfcp_erp_port_boxed(struct zfcp_port *port, u8 id, void *ref)
{
	unsigned long flags;

	read_lock_irqsave(&zfcp_data.config_lock, flags);
	zfcp_erp_modify_port_status(port, id, ref,
				    ZFCP_STATUS_COMMON_ACCESS_BOXED, ZFCP_SET);
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);
	zfcp_erp_port_reopen(port, ZFCP_STATUS_COMMON_ERP_FAILED, id, ref);
}

/**
 * zfcp_erp_unit_boxed - Mark unit as "boxed" and start ERP
 * @port: The "boxed" unit.
 * @id: The debug trace id.
 * @id: Reference for the debug trace.
 */
void zfcp_erp_unit_boxed(struct zfcp_unit *unit, u8 id, void *ref)
{
	zfcp_erp_modify_unit_status(unit, id, ref,
				    ZFCP_STATUS_COMMON_ACCESS_BOXED, ZFCP_SET);
	zfcp_erp_unit_reopen(unit, ZFCP_STATUS_COMMON_ERP_FAILED, id, ref);
}

/**
 * zfcp_erp_port_access_denied - Adapter denied access to port.
 * @port: port where access has been denied
 * @id: id for debug trace
 * @ref: reference for debug trace
 *
 * Since the adapter has denied access, stop using the port and the
 * attached units.
 */
void zfcp_erp_port_access_denied(struct zfcp_port *port, u8 id, void *ref)
{
	unsigned long flags;

	read_lock_irqsave(&zfcp_data.config_lock, flags);
	zfcp_erp_modify_port_status(port, id, ref,
				    ZFCP_STATUS_COMMON_ERP_FAILED |
				    ZFCP_STATUS_COMMON_ACCESS_DENIED, ZFCP_SET);
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);
}

/**
 * zfcp_erp_unit_access_denied - Adapter denied access to unit.
 * @unit: unit where access has been denied
 * @id: id for debug trace
 * @ref: reference for debug trace
 *
 * Since the adapter has denied access, stop using the unit.
 */
void zfcp_erp_unit_access_denied(struct zfcp_unit *unit, u8 id, void *ref)
{
	zfcp_erp_modify_unit_status(unit, id, ref,
				    ZFCP_STATUS_COMMON_ERP_FAILED |
				    ZFCP_STATUS_COMMON_ACCESS_DENIED, ZFCP_SET);
}

static void zfcp_erp_unit_access_changed(struct zfcp_unit *unit, u8 id,
					 void *ref)
{
	int status = atomic_read(&unit->status);
	if (!(status & (ZFCP_STATUS_COMMON_ACCESS_DENIED |
			ZFCP_STATUS_COMMON_ACCESS_BOXED)))
		return;

	zfcp_erp_unit_reopen(unit, ZFCP_STATUS_COMMON_ERP_FAILED, id, ref);
}

static void zfcp_erp_port_access_changed(struct zfcp_port *port, u8 id,
					 void *ref)
{
	struct zfcp_unit *unit;
	int status = atomic_read(&port->status);

	if (!(status & (ZFCP_STATUS_COMMON_ACCESS_DENIED |
			ZFCP_STATUS_COMMON_ACCESS_BOXED))) {
		if (!(status & ZFCP_STATUS_PORT_WKA))
			list_for_each_entry(unit, &port->unit_list_head, list)
				zfcp_erp_unit_access_changed(unit, id, ref);
		return;
	}

	zfcp_erp_port_reopen(port, ZFCP_STATUS_COMMON_ERP_FAILED, id, ref);
}

/**
 * zfcp_erp_adapter_access_changed - Process change in adapter ACT
 * @adapter: Adapter where the Access Control Table (ACT) changed
 * @id: Id for debug trace
 * @ref: Reference for debug trace
 */
void zfcp_erp_adapter_access_changed(struct zfcp_adapter *adapter, u8 id,
				     void *ref)
{
	struct zfcp_port *port;
	unsigned long flags;

	if (adapter->connection_features & FSF_FEATURE_NPIV_MODE)
		return;

	read_lock_irqsave(&zfcp_data.config_lock, flags);
	if (adapter->nameserver_port)
		zfcp_erp_port_access_changed(adapter->nameserver_port, id, ref);
	list_for_each_entry(port, &adapter->port_list_head, list)
		if (port != adapter->nameserver_port)
			zfcp_erp_port_access_changed(port, id, ref);
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);
}
 * zfcp_erp_set_adapter_status - set adapter status bits
 * @adapter: adapter to change the status
 * @mask: status bits to change
 *
 * Changes in common status bits are propagated to attached ports and LUNs.
 */
void zfcp_erp_set_adapter_status(struct zfcp_adapter *adapter, u32 mask)
{
	struct zfcp_port *port;
	struct scsi_device *sdev;
	unsigned long flags;
	u32 common_mask = mask & ZFCP_COMMON_FLAGS;

	atomic_or(mask, &adapter->status);

	if (!common_mask)
		return;

	read_lock_irqsave(&adapter->port_list_lock, flags);
	list_for_each_entry(port, &adapter->port_list, list)
		atomic_or(common_mask, &port->status);
	read_unlock_irqrestore(&adapter->port_list_lock, flags);

	spin_lock_irqsave(adapter->scsi_host->host_lock, flags);
	__shost_for_each_device(sdev, adapter->scsi_host)
		atomic_or(common_mask, &sdev_to_zfcp(sdev)->status);
	spin_unlock_irqrestore(adapter->scsi_host->host_lock, flags);
}

/**
 * zfcp_erp_clear_adapter_status - clear adapter status bits
 * @adapter: adapter to change the status
 * @mask: status bits to change
 *
 * Changes in common status bits are propagated to attached ports and LUNs.
 */
void zfcp_erp_clear_adapter_status(struct zfcp_adapter *adapter, u32 mask)
{
	struct zfcp_port *port;
	struct scsi_device *sdev;
	unsigned long flags;
	u32 common_mask = mask & ZFCP_COMMON_FLAGS;
	u32 clear_counter = mask & ZFCP_STATUS_COMMON_ERP_FAILED;

	atomic_andnot(mask, &adapter->status);

	if (!common_mask)
		return;

	if (clear_counter)
		atomic_set(&adapter->erp_counter, 0);

	read_lock_irqsave(&adapter->port_list_lock, flags);
	list_for_each_entry(port, &adapter->port_list, list) {
		atomic_andnot(common_mask, &port->status);
		if (clear_counter)
			atomic_set(&port->erp_counter, 0);
	}
	read_unlock_irqrestore(&adapter->port_list_lock, flags);

	spin_lock_irqsave(adapter->scsi_host->host_lock, flags);
	__shost_for_each_device(sdev, adapter->scsi_host) {
		atomic_andnot(common_mask, &sdev_to_zfcp(sdev)->status);
		if (clear_counter)
			atomic_set(&sdev_to_zfcp(sdev)->erp_counter, 0);
	}
	spin_unlock_irqrestore(adapter->scsi_host->host_lock, flags);
}

/**
 * zfcp_erp_set_port_status - set port status bits
 * @port: port to change the status
 * @mask: status bits to change
 *
 * Changes in common status bits are propagated to attached LUNs.
 */
void zfcp_erp_set_port_status(struct zfcp_port *port, u32 mask)
{
	struct scsi_device *sdev;
	u32 common_mask = mask & ZFCP_COMMON_FLAGS;
	unsigned long flags;

	atomic_or(mask, &port->status);

	if (!common_mask)
		return;

	spin_lock_irqsave(port->adapter->scsi_host->host_lock, flags);
	__shost_for_each_device(sdev, port->adapter->scsi_host)
		if (sdev_to_zfcp(sdev)->port == port)
			atomic_or(common_mask,
					&sdev_to_zfcp(sdev)->status);
	spin_unlock_irqrestore(port->adapter->scsi_host->host_lock, flags);
}

/**
 * zfcp_erp_clear_port_status - clear port status bits
 * @port: adapter to change the status
 * @mask: status bits to change
 *
 * Changes in common status bits are propagated to attached LUNs.
 */
void zfcp_erp_clear_port_status(struct zfcp_port *port, u32 mask)
{
	struct scsi_device *sdev;
	u32 common_mask = mask & ZFCP_COMMON_FLAGS;
	u32 clear_counter = mask & ZFCP_STATUS_COMMON_ERP_FAILED;
	unsigned long flags;

	atomic_andnot(mask, &port->status);

	if (!common_mask)
		return;

	if (clear_counter)
		atomic_set(&port->erp_counter, 0);

	spin_lock_irqsave(port->adapter->scsi_host->host_lock, flags);
	__shost_for_each_device(sdev, port->adapter->scsi_host)
		if (sdev_to_zfcp(sdev)->port == port) {
			atomic_andnot(common_mask,
					  &sdev_to_zfcp(sdev)->status);
			if (clear_counter)
				atomic_set(&sdev_to_zfcp(sdev)->erp_counter, 0);
		}
	spin_unlock_irqrestore(port->adapter->scsi_host->host_lock, flags);
}

/**
 * zfcp_erp_set_lun_status - set lun status bits
 * @sdev: SCSI device / lun to set the status bits
 * @mask: status bits to change
 */
void zfcp_erp_set_lun_status(struct scsi_device *sdev, u32 mask)
{
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(sdev);

	atomic_or(mask, &zfcp_sdev->status);
}

/**
 * zfcp_erp_clear_lun_status - clear lun status bits
 * @sdev: SCSi device / lun to clear the status bits
 * @mask: status bits to change
 */
void zfcp_erp_clear_lun_status(struct scsi_device *sdev, u32 mask)
{
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(sdev);

	atomic_andnot(mask, &zfcp_sdev->status);

	if (mask & ZFCP_STATUS_COMMON_ERP_FAILED)
		atomic_set(&zfcp_sdev->erp_counter, 0);
}

