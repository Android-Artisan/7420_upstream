/*
 * zfcp device driver
 *
 * External function declarations.
 *
 * Copyright IBM Corporation 2002, 2008
 * Copyright IBM Corp. 2002, 2010
 * Copyright IBM Corp. 2002, 2015
 * Copyright IBM Corp. 2002, 2016
 * Copyright IBM Corp. 2002, 2018
 */

#ifndef ZFCP_EXT_H
#define ZFCP_EXT_H

#include "zfcp_def.h"

/* zfcp_aux.c */
extern struct zfcp_unit *zfcp_get_unit_by_lun(struct zfcp_port *,
					      fcp_lun_t);
extern struct zfcp_port *zfcp_get_port_by_wwpn(struct zfcp_adapter *,
					       wwn_t);
extern int zfcp_adapter_enqueue(struct ccw_device *);
extern void zfcp_adapter_dequeue(struct zfcp_adapter *);
extern struct zfcp_port *zfcp_port_enqueue(struct zfcp_adapter *, wwn_t, u32,
					   u32);
extern void zfcp_port_dequeue(struct zfcp_port *);
extern struct zfcp_unit *zfcp_unit_enqueue(struct zfcp_port *, fcp_lun_t);
extern void zfcp_unit_dequeue(struct zfcp_unit *);
extern int zfcp_reqlist_isempty(struct zfcp_adapter *);
extern void zfcp_sg_free_table(struct scatterlist *, int);
extern int zfcp_sg_setup_table(struct scatterlist *, int);

/* zfcp_ccw.c */
extern int zfcp_ccw_register(void);

/* zfcp_cfdc.c */
extern struct miscdevice zfcp_cfdc_misc;

/* zfcp_dbf.c */
extern int zfcp_adapter_debug_register(struct zfcp_adapter *);
extern void zfcp_adapter_debug_unregister(struct zfcp_adapter *);
extern void zfcp_rec_dbf_event_thread(u8, struct zfcp_adapter *);
extern void zfcp_rec_dbf_event_thread_lock(u8, struct zfcp_adapter *);
extern void zfcp_rec_dbf_event_adapter(u8, void *, struct zfcp_adapter *);
extern void zfcp_rec_dbf_event_port(u8, void *, struct zfcp_port *);
extern void zfcp_rec_dbf_event_unit(u8, void *, struct zfcp_unit *);
extern void zfcp_rec_dbf_event_trigger(u8, void *, u8, u8, void *,
				       struct zfcp_adapter *,
				       struct zfcp_port *, struct zfcp_unit *);
extern void zfcp_rec_dbf_event_action(u8, struct zfcp_erp_action *);
extern void zfcp_hba_dbf_event_fsf_response(struct zfcp_fsf_req *);
extern void zfcp_hba_dbf_event_fsf_unsol(const char *, struct zfcp_adapter *,
					 struct fsf_status_read_buffer *);
extern void zfcp_hba_dbf_event_qdio(struct zfcp_adapter *, unsigned int, int,
				    int);
extern void zfcp_san_dbf_event_ct_request(struct zfcp_fsf_req *);
extern void zfcp_san_dbf_event_ct_response(struct zfcp_fsf_req *);
extern void zfcp_san_dbf_event_els_request(struct zfcp_fsf_req *);
extern void zfcp_san_dbf_event_els_response(struct zfcp_fsf_req *);
extern void zfcp_san_dbf_event_incoming_els(struct zfcp_fsf_req *);
extern void zfcp_scsi_dbf_event_result(const char *, int, struct zfcp_adapter *,
				       struct scsi_cmnd *,
				       struct zfcp_fsf_req *);
extern void zfcp_scsi_dbf_event_abort(const char *, struct zfcp_adapter *,
				      struct scsi_cmnd *, struct zfcp_fsf_req *,
				      unsigned long);
extern void zfcp_scsi_dbf_event_devreset(const char *, u8, struct zfcp_unit *,
					 struct scsi_cmnd *);

/* zfcp_erp.c */
extern void zfcp_erp_modify_adapter_status(struct zfcp_adapter *, u8, void *,
					   u32, int);
extern void zfcp_erp_adapter_reopen(struct zfcp_adapter *, int, u8, void *);
extern void zfcp_erp_adapter_shutdown(struct zfcp_adapter *, int, u8, void *);
extern void zfcp_erp_adapter_failed(struct zfcp_adapter *, u8, void *);
extern void zfcp_erp_modify_port_status(struct zfcp_port *, u8, void *, u32,
					int);
extern int  zfcp_erp_port_reopen(struct zfcp_port *, int, u8, void *);
extern void zfcp_erp_port_shutdown(struct zfcp_port *, int, u8, void *);
extern void zfcp_erp_port_forced_reopen(struct zfcp_port *, int, u8, void *);
extern void zfcp_erp_port_failed(struct zfcp_port *, u8, void *);
extern void zfcp_erp_modify_unit_status(struct zfcp_unit *, u8, void *, u32,
					int);
extern void zfcp_erp_unit_reopen(struct zfcp_unit *, int, u8, void *);
extern void zfcp_erp_unit_shutdown(struct zfcp_unit *, int, u8, void *);
extern void zfcp_erp_unit_failed(struct zfcp_unit *, u8, void *);
#include <linux/types.h>
#include <scsi/fc/fc_els.h>
#include "zfcp_def.h"
#include "zfcp_fc.h"

/* zfcp_aux.c */
extern struct zfcp_port *zfcp_get_port_by_wwpn(struct zfcp_adapter *, u64);
extern struct zfcp_adapter *zfcp_adapter_enqueue(struct ccw_device *);
extern struct zfcp_port *zfcp_port_enqueue(struct zfcp_adapter *, u64, u32,
					   u32);
extern void zfcp_sg_free_table(struct scatterlist *, int);
extern int zfcp_sg_setup_table(struct scatterlist *, int);
extern void zfcp_adapter_release(struct kref *);
extern void zfcp_adapter_unregister(struct zfcp_adapter *);

/* zfcp_ccw.c */
extern struct ccw_driver zfcp_ccw_driver;
extern struct zfcp_adapter *zfcp_ccw_adapter_by_cdev(struct ccw_device *);
extern void zfcp_ccw_adapter_put(struct zfcp_adapter *);

/* zfcp_dbf.c */
extern int zfcp_dbf_adapter_register(struct zfcp_adapter *);
extern void zfcp_dbf_adapter_unregister(struct zfcp_adapter *);
extern void zfcp_dbf_rec_trig(char *, struct zfcp_adapter *,
			      struct zfcp_port *, struct scsi_device *, u8, u8);
extern void zfcp_dbf_rec_trig_lock(char *tag, struct zfcp_adapter *adapter,
				   struct zfcp_port *port,
				   struct scsi_device *sdev, u8 want, u8 need);
extern void zfcp_dbf_rec_run(char *, struct zfcp_erp_action *);
extern void zfcp_dbf_rec_run_lvl(int level, char *tag,
				 struct zfcp_erp_action *erp);
extern void zfcp_dbf_rec_run_wka(char *, struct zfcp_fc_wka_port *, u64);
extern void zfcp_dbf_hba_fsf_uss(char *, struct zfcp_fsf_req *);
extern void zfcp_dbf_hba_fsf_res(char *, int, struct zfcp_fsf_req *);
extern void zfcp_dbf_hba_bit_err(char *, struct zfcp_fsf_req *);
extern void zfcp_dbf_hba_berr(struct zfcp_dbf *, struct zfcp_fsf_req *);
extern void zfcp_dbf_hba_def_err(struct zfcp_adapter *, u64, u16, void **);
extern void zfcp_dbf_hba_basic(char *, struct zfcp_adapter *);
extern void zfcp_dbf_san_req(char *, struct zfcp_fsf_req *, u32);
extern void zfcp_dbf_san_res(char *, struct zfcp_fsf_req *);
extern void zfcp_dbf_san_in_els(char *, struct zfcp_fsf_req *);
extern void zfcp_dbf_scsi(char *, int, struct scsi_cmnd *,
			  struct zfcp_fsf_req *);
extern void zfcp_dbf_scsi_eh(char *tag, struct zfcp_adapter *adapter,
			     unsigned int scsi_id, int ret);

/* zfcp_erp.c */
extern void zfcp_erp_set_adapter_status(struct zfcp_adapter *, u32);
extern void zfcp_erp_clear_adapter_status(struct zfcp_adapter *, u32);
extern void zfcp_erp_port_forced_no_port_dbf(char *id,
					     struct zfcp_adapter *adapter,
					     u64 port_name, u32 port_id);
extern void zfcp_erp_adapter_reopen(struct zfcp_adapter *, int, char *);
extern void zfcp_erp_adapter_shutdown(struct zfcp_adapter *, int, char *);
extern void zfcp_erp_set_port_status(struct zfcp_port *, u32);
extern void zfcp_erp_clear_port_status(struct zfcp_port *, u32);
extern int  zfcp_erp_port_reopen(struct zfcp_port *, int, char *);
extern void zfcp_erp_port_shutdown(struct zfcp_port *, int, char *);
extern void zfcp_erp_port_forced_reopen(struct zfcp_port *, int, char *);
extern void zfcp_erp_port_forced_reopen_all(struct zfcp_adapter *adapter,
					    int clear, char *dbftag);
extern void zfcp_erp_set_lun_status(struct scsi_device *, u32);
extern void zfcp_erp_clear_lun_status(struct scsi_device *, u32);
extern void zfcp_erp_lun_reopen(struct scsi_device *, int, char *);
extern void zfcp_erp_lun_shutdown(struct scsi_device *, int, char *);
extern void zfcp_erp_lun_shutdown_wait(struct scsi_device *, char *);
extern int  zfcp_erp_thread_setup(struct zfcp_adapter *);
extern void zfcp_erp_thread_kill(struct zfcp_adapter *);
extern void zfcp_erp_wait(struct zfcp_adapter *);
extern void zfcp_erp_notify(struct zfcp_erp_action *, unsigned long);
extern void zfcp_erp_port_boxed(struct zfcp_port *, u8, void *);
extern void zfcp_erp_unit_boxed(struct zfcp_unit *, u8, void *);
extern void zfcp_erp_port_access_denied(struct zfcp_port *, u8, void *);
extern void zfcp_erp_unit_access_denied(struct zfcp_unit *, u8, void *);
extern void zfcp_erp_adapter_access_changed(struct zfcp_adapter *, u8, void *);
extern void zfcp_erp_timeout_handler(unsigned long);

/* zfcp_fc.c */
extern int zfcp_scan_ports(struct zfcp_adapter *);
extern void _zfcp_scan_ports_later(struct work_struct *);
extern void zfcp_fc_incoming_els(struct zfcp_fsf_req *);
extern int zfcp_fc_ns_gid_pn_request(struct zfcp_erp_action *);
extern void zfcp_fc_plogi_evaluate(struct zfcp_port *, struct fsf_plogi *);
extern void zfcp_test_link(struct zfcp_port *);

/* zfcp_fsf.c */
extern int zfcp_fsf_open_port(struct zfcp_erp_action *);
extern int zfcp_fsf_close_port(struct zfcp_erp_action *);
extern int zfcp_fsf_close_physical_port(struct zfcp_erp_action *);
extern int zfcp_fsf_open_unit(struct zfcp_erp_action *);
extern int zfcp_fsf_close_unit(struct zfcp_erp_action *);
extern int zfcp_fsf_exchange_config_data(struct zfcp_erp_action *);
extern int zfcp_fsf_exchange_config_data_sync(struct zfcp_adapter *,
					      struct fsf_qtcb_bottom_config *);
extern int zfcp_fsf_exchange_port_data(struct zfcp_erp_action *);
extern int zfcp_fsf_exchange_port_data_sync(struct zfcp_adapter *,
					    struct fsf_qtcb_bottom_port *);
extern struct zfcp_fsf_req *zfcp_fsf_control_file(struct zfcp_adapter *,
						  struct zfcp_fsf_cfdc *);
extern void zfcp_fsf_req_dismiss_all(struct zfcp_adapter *);
extern int zfcp_fsf_status_read(struct zfcp_adapter *);
extern int zfcp_status_read_refill(struct zfcp_adapter *adapter);
extern int zfcp_fsf_send_ct(struct zfcp_send_ct *, mempool_t *,
			    struct zfcp_erp_action *);
extern int zfcp_fsf_send_els(struct zfcp_send_els *);
extern int zfcp_fsf_send_fcp_command_task(struct zfcp_adapter *,
					  struct zfcp_unit *,
					  struct scsi_cmnd *, int, int);
extern void zfcp_fsf_req_complete(struct zfcp_fsf_req *);
extern void zfcp_fsf_req_free(struct zfcp_fsf_req *);
extern struct zfcp_fsf_req *zfcp_fsf_send_fcp_ctm(struct zfcp_adapter *,
						  struct zfcp_unit *, u8, int);
extern struct zfcp_fsf_req *zfcp_fsf_abort_fcp_command(unsigned long,
						       struct zfcp_adapter *,
						       struct zfcp_unit *, int);

/* zfcp_qdio.c */
extern int zfcp_qdio_allocate(struct zfcp_adapter *);
extern void zfcp_qdio_free(struct zfcp_adapter *);
extern int zfcp_qdio_send(struct zfcp_fsf_req *);
extern volatile struct qdio_buffer_element *zfcp_qdio_sbale_req(
						struct zfcp_fsf_req *);
extern volatile struct qdio_buffer_element *zfcp_qdio_sbale_curr(
						struct zfcp_fsf_req *);
extern int zfcp_qdio_sbals_from_sg(struct zfcp_fsf_req *, unsigned long,
				   struct scatterlist *, int);
extern int zfcp_qdio_open(struct zfcp_adapter *);
extern void zfcp_qdio_close(struct zfcp_adapter *);

/* zfcp_scsi.c */
extern struct zfcp_data zfcp_data;
extern int zfcp_adapter_scsi_register(struct zfcp_adapter *);
extern void zfcp_adapter_scsi_unregister(struct zfcp_adapter *);
extern void zfcp_set_fcp_dl(struct fcp_cmnd_iu *, fcp_dl_t);
extern char *zfcp_get_fcp_sns_info_ptr(struct fcp_rsp_iu *);
extern struct fc_function_template zfcp_transport_functions;

/* zfcp_sysfs.c */
extern struct attribute_group zfcp_sysfs_unit_attrs;
extern struct attribute_group zfcp_sysfs_adapter_attrs;
extern struct attribute_group zfcp_sysfs_ns_port_attrs;
extern struct attribute_group zfcp_sysfs_port_attrs;
extern struct device_attribute *zfcp_sysfs_sdev_attrs[];
extern struct device_attribute *zfcp_sysfs_shost_attrs[];

extern void zfcp_erp_timeout_handler(unsigned long);

/* zfcp_fc.c */
extern struct kmem_cache *zfcp_fc_req_cache;
extern void zfcp_fc_enqueue_event(struct zfcp_adapter *,
				enum fc_host_event_code event_code, u32);
extern void zfcp_fc_post_event(struct work_struct *);
extern void zfcp_fc_scan_ports(struct work_struct *);
extern void zfcp_fc_incoming_els(struct zfcp_fsf_req *);
extern void zfcp_fc_port_did_lookup(struct work_struct *);
extern void zfcp_fc_trigger_did_lookup(struct zfcp_port *);
extern void zfcp_fc_plogi_evaluate(struct zfcp_port *, struct fc_els_flogi *);
extern void zfcp_fc_test_link(struct zfcp_port *);
extern void zfcp_fc_link_test_work(struct work_struct *);
extern void zfcp_fc_wka_ports_force_offline(struct zfcp_fc_wka_ports *);
extern int zfcp_fc_gs_setup(struct zfcp_adapter *);
extern void zfcp_fc_gs_destroy(struct zfcp_adapter *);
extern int zfcp_fc_exec_bsg_job(struct fc_bsg_job *);
extern int zfcp_fc_timeout_bsg_job(struct fc_bsg_job *);
extern void zfcp_fc_sym_name_update(struct work_struct *);
extern unsigned int zfcp_fc_port_scan_backoff(void);
extern void zfcp_fc_conditional_port_scan(struct zfcp_adapter *);
extern void zfcp_fc_inverse_conditional_port_scan(struct zfcp_adapter *);

/* zfcp_fsf.c */
extern struct kmem_cache *zfcp_fsf_qtcb_cache;
extern int zfcp_fsf_open_port(struct zfcp_erp_action *);
extern int zfcp_fsf_open_wka_port(struct zfcp_fc_wka_port *);
extern int zfcp_fsf_close_wka_port(struct zfcp_fc_wka_port *);
extern int zfcp_fsf_close_port(struct zfcp_erp_action *);
extern int zfcp_fsf_close_physical_port(struct zfcp_erp_action *);
extern int zfcp_fsf_open_lun(struct zfcp_erp_action *);
extern int zfcp_fsf_close_lun(struct zfcp_erp_action *);
extern int zfcp_fsf_exchange_config_data(struct zfcp_erp_action *);
extern int zfcp_fsf_exchange_config_data_sync(struct zfcp_qdio *,
					      struct fsf_qtcb_bottom_config *);
extern int zfcp_fsf_exchange_port_data(struct zfcp_erp_action *);
extern int zfcp_fsf_exchange_port_data_sync(struct zfcp_qdio *,
					    struct fsf_qtcb_bottom_port *);
extern void zfcp_fsf_req_dismiss_all(struct zfcp_adapter *);
extern int zfcp_fsf_status_read(struct zfcp_qdio *);
extern int zfcp_status_read_refill(struct zfcp_adapter *adapter);
extern int zfcp_fsf_send_ct(struct zfcp_fc_wka_port *, struct zfcp_fsf_ct_els *,
			    mempool_t *, unsigned int);
extern int zfcp_fsf_send_els(struct zfcp_adapter *, u32,
			     struct zfcp_fsf_ct_els *, unsigned int);
extern int zfcp_fsf_fcp_cmnd(struct scsi_cmnd *);
extern void zfcp_fsf_req_free(struct zfcp_fsf_req *);
extern struct zfcp_fsf_req *zfcp_fsf_fcp_task_mgmt(struct scsi_cmnd *, u8);
extern struct zfcp_fsf_req *zfcp_fsf_abort_fcp_cmnd(struct scsi_cmnd *);
extern void zfcp_fsf_reqid_check(struct zfcp_qdio *, int);

/* zfcp_qdio.c */
extern int zfcp_qdio_setup(struct zfcp_adapter *);
extern void zfcp_qdio_destroy(struct zfcp_qdio *);
extern int zfcp_qdio_sbal_get(struct zfcp_qdio *);
extern int zfcp_qdio_send(struct zfcp_qdio *, struct zfcp_qdio_req *);
extern int zfcp_qdio_sbals_from_sg(struct zfcp_qdio *, struct zfcp_qdio_req *,
				   struct scatterlist *);
extern int zfcp_qdio_open(struct zfcp_qdio *);
extern void zfcp_qdio_close(struct zfcp_qdio *);
extern void zfcp_qdio_siosl(struct zfcp_adapter *);

/* zfcp_scsi.c */
extern struct scsi_transport_template *zfcp_scsi_transport_template;
extern int zfcp_scsi_adapter_register(struct zfcp_adapter *);
extern void zfcp_scsi_adapter_unregister(struct zfcp_adapter *);
extern struct fc_function_template zfcp_transport_functions;
extern void zfcp_scsi_rport_work(struct work_struct *);
extern void zfcp_scsi_schedule_rport_register(struct zfcp_port *);
extern void zfcp_scsi_schedule_rport_block(struct zfcp_port *);
extern void zfcp_scsi_schedule_rports_block(struct zfcp_adapter *);
extern void zfcp_scsi_set_prot(struct zfcp_adapter *);
extern void zfcp_scsi_dif_sense_error(struct scsi_cmnd *, int);

/* zfcp_sysfs.c */
extern const struct attribute_group *zfcp_unit_attr_groups[];
extern struct attribute_group zfcp_sysfs_adapter_attrs;
extern const struct attribute_group *zfcp_port_attr_groups[];
extern struct mutex zfcp_sysfs_port_units_mutex;
extern struct device_attribute *zfcp_sysfs_sdev_attrs[];
extern struct device_attribute *zfcp_sysfs_shost_attrs[];
bool zfcp_sysfs_port_is_removing(const struct zfcp_port *const port);

/* zfcp_unit.c */
extern int zfcp_unit_add(struct zfcp_port *, u64);
extern int zfcp_unit_remove(struct zfcp_port *, u64);
extern struct zfcp_unit *zfcp_unit_find(struct zfcp_port *, u64);
extern struct scsi_device *zfcp_unit_sdev(struct zfcp_unit *unit);
extern void zfcp_unit_scsi_scan(struct zfcp_unit *);
extern void zfcp_unit_queue_scsi_scan(struct zfcp_port *);
extern unsigned int zfcp_unit_sdev_status(struct zfcp_unit *);

#endif	/* ZFCP_EXT_H */
