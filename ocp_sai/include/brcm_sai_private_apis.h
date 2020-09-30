/*********************************************************************
 *
 * Copyright: (c) 2017 Broadcom.
 * Broadcom Proprietary and Confidential. All rights reserved.
 *
 *********************************************************************/

#if !defined (BRCM_SAI_PRIVATE_APIS_H)
#define BRCM_SAI_PRIVATE_APIS_H

extern char *readline(const char *prompt);
extern int driverPropertyCheck(int prop, int *val);
extern int driverMmuInfoGet(int type, uint32_t *val);
extern int driverMMUInit(int unit);
extern int driverTcPgMapSet(uint8_t port, uint8_t tc, uint8_t pg);
extern int driverSPLimitSet(int idx, int val);
extern int driverSPLimitGet(int idx, int *val);
extern int driverSPHeadroomSet(int idx, int val);
extern int driverIngressGlobalHdrmSet(int port, int val);
extern int driverPGGlobalHdrmEnable(int port, int pg, uint8_t enable);
extern int driverDBLimitSet(int idx, int val);
extern int driverPGAttributeSet(int port, int pg, int type, int val);
extern uint32_t driverPortPoolMaxLimitBytesMaxGet();
extern int driverPortSPSet(int port, int pool, int type, int val);
extern int driverEgressPortPoolLimitSet(int port, int pool, int val);
extern int driverEgressPortPoolResumeSet(int port, int pool, int val);
extern int driverEgressQueueSharedAlphaSet(int port, int qid, int val);
extern int driverEgressQueueMinLimitSet(int port, int qid, int val);
extern sai_status_t driverEgressQueueMinLimitGet(int unit, int port, int qid, int *val);
extern int driverEgressQueueResetOffsetSet(int port, int qid, int val);
extern int driverDisableStormControl(int port);
extern int driverPortPktTxEnableSet(int unit, bcm_port_t port, int enable);
extern int driverMMUInstMapIndexGet(int unit, bcm_gport_t gport, bcm_bst_stat_id_t bid, int *index);

extern sai_status_t
_brcm_sai_create_acl_table(_Out_ sai_object_id_t* acl_table_id,
                           _In_ uint32_t attr_count,
                           _In_ const sai_attribute_t *attr_list,
                           int *_stage, int *bcount, int32_t **blist);

extern sai_status_t
_brcm_sai_get_acl_table_attribute(_In_ sai_object_id_t acl_table_id,
                                  _In_ uint32_t attr_count,
                                  _Out_ sai_attribute_t *attr_list);

extern sai_status_t
_brcm_sai_create_acl_entry(_Out_ sai_object_id_t *acl_entry_id,
                           _In_ uint32_t attr_count,
                           _In_ const sai_attribute_t *attr_list,
                           bcm_field_entry_t entry, bool ingress,
                           sai_packet_action_t gpa, sai_packet_action_t ypa,
                           sai_packet_action_t rpa, bool *state);

extern sai_status_t
_brcm_sai_get_acl_entry_attribute(_In_ sai_object_id_t acl_entry_id,
                                 _In_ uint32_t attr_count,
                                 _Out_ sai_attribute_t *attr_list);

extern sai_status_t
_brcm_sai_create_acl_range(_Out_ sai_object_id_t* acl_range_id,
                          _In_ uint32_t attr_count,
                          _In_ const sai_attribute_t *attr_list);

extern sai_status_t
_brcm_sai_get_acl_counter_attribute(_In_ sai_object_id_t acl_counter_id,
                                    _In_ uint32_t attr_count,
                                    _Out_ sai_attribute_t *attr_list);

extern bool
_brcm_sai_field_table_qualify_lag_test(bool direction,
                                       bcm_field_qset_t qset);

extern sai_status_t
_brcm_sai_field_qualify_lag(bcm_field_entry_t entry, bool ingress,
                            int lag);

extern sai_status_t
_brcm_udf_hash_config_add(int id, int len, uint8_t *mask);

extern sai_status_t
_brcm_sai_set_trap_attribute(sai_hostif_trap_type_t hostif_trapid, int priority,
                             int pkt_action, int queue, int policer,
                             bool state, bcm_field_group_t *group,
                             bcm_field_entry_t *entry);

extern sai_status_t
_brcm_entry_enable(bcm_field_entry_t entry, bool state);

extern sai_status_t
_brcm_entry_field_qualify(bcm_field_entry_t entry,
                          sai_hostif_trap_type_t hostif_trapid);

extern sai_status_t
_brcm_sai_pol_sh_en();

extern sai_status_t
_brcm_sai_dscp_ecn_mode_set();

extern sai_status_t
_brcm_sai_create_mirror_session(_Out_ sai_object_id_t *session_id,
                                _In_  uint32_t attr_count,
                                _In_  const sai_attribute_t *attr_list);

extern sai_status_t
_brcm_sai_cosq_config(int port, int id, int type, int data);

extern sai_status_t
_brcm_sai_l3_config_get(int id, int *max);

extern sai_status_t
_brcm_sai_tunnel_encap(bcm_l3_intf_t *l3_intf,
                       bcm_tunnel_initiator_t *tunnel_init);

extern sai_status_t
_brcm_l3_egress_ecmp_add(bcm_l3_egress_ecmp_t *ecmp_object,
                         bcm_if_t if_id);

extern sai_status_t
_brcm_l3_egress_ecmp_get(bcm_l3_egress_ecmp_t *ecmp_object, int max,
                         bcm_if_t *intfs, int *count);

extern sai_status_t
_brcm_sai_port_set_rxlos(int port, int val);

extern sai_status_t
_brcm_sai_port_sched_prof_set(_brcm_sai_qos_scheduler_t *scheduler,
                              bcm_gport_t scheduler_gport, bool all_cosq,
                              int cos);

extern sai_status_t
_brcm_sai_set_port_attribute(_In_ sai_object_id_t port_id,
                             _In_ const sai_attribute_t *attr,
                             bool pkt_mode, int cir, int cbs);

extern sai_status_t
_brcm_sai_get_port_attribute(_In_ sai_object_id_t port_id,
                             _Inout_ sai_attribute_t *attr);

extern sai_status_t
__brcm_sai_port_dot1p_tc_map_set(int port, int lval, int rval);

extern sai_status_t
__brcm_sai_port_dot1p_color_map_set(int port, int lval, int rval);

extern sai_status_t
__brcm_sai_port_dot1p_tc_color_map_set(int port, int lval, int rval1,
                                       int rval2);

extern sai_status_t
__brcm_sai_port_tc_color_dot1p_map_set(int port, int lval1, int lval2,
                                       int rval);

extern sai_status_t
__brcm_sai_port_tc_color_dscp_map_set(int port, int lval1, int lval2,
                                      int rval);
extern sai_status_t
_brcm_sai_port_update_queue_tc_mapping(int port, int qid, bcm_gport_t gport);

extern sai_status_t
_brcm_sai_stat_get(int port, int type, uint64 *count);

extern sai_int32_t
BRCM_IPTYPE_SAI_TO_BCM(sai_int32_t iptype);
extern sai_int32_t
BRCM_IPTYPE_BCM_TO_SAI(sai_int32_t iptype);
extern sai_int32_t
BRCM_IPFRAG_SAI_TO_BCM(sai_int32_t ipfrag);
extern sai_int32_t
BRCM_IPFRAG_BCM_TO_SAI(sai_int32_t ipfrag);

extern sai_status_t
__brcm_sai_port_pfc_queue_map_set(int port,
                                  bcm_gport_t gports[8]);

extern sai_status_t
_brcm_sai_cosq_bandwidth_set(bcm_gport_t parent_gport,
                             bcm_cos_queue_t cosq,
                             _brcm_sai_qos_scheduler_t *scheduler);

extern sai_status_t
_brcm_sai_cosq_stat_get(int port, int qid, bcm_gport_t gport,
                        _In_ const sai_queue_stat_t counter_id,
                        _Out_ uint64_t* counter);
sai_status_t
_brcm_sai_cosq_stat_set(int port, int qid, bcm_gport_t gport,
                        _In_ const sai_queue_stat_t counter_id,
                        _In_ uint64_t counter);

sai_status_t
_brcm_sai_ingress_pg_stat_get(int port, int pg, bcm_gport_t gport,
                              _In_ const sai_ingress_priority_group_stat_t counter_id,
                              _Out_ uint64_t* counter);

sai_status_t
_brcm_sai_ingress_pg_stat_set(int port, int pg, bcm_gport_t gport,
                              _In_ const sai_ingress_priority_group_stat_t counter_id,
                              _In_ uint64_t counter);

extern sai_status_t
_brcm_sai_cosq_state_get(int unit, int port, int qid, int attr,
                         int *val);

extern sai_status_t
_brcm_sai_l3_route_config(bool add_del, int cid,
                          bcm_l3_route_t *l3_rt);

extern sai_status_t
_brcm_sai_l3_host_config(bool v6_128, int cid, bcm_l3_host_t *l3_host,
                         bcm_l3_egress_t *l3_egr);

extern sai_status_t
_brcm_sai_switch_flex_counter_init();

extern sai_status_t
_brcm_sai_switch_flex_counter_port_enable(int port);

extern sai_status_t
_brcm_sai_switch_config_get(int id, int *val);

extern sai_status_t
_brcm_sai_l2_station_add(int tid, bcm_l2_station_t *l2_stn,
                         int *station_id);

extern int
_brcm_cosq_gport_add(int unit, bcm_gport_t port, int numq,
                     uint32 flags, bool align,
                     bcm_gport_t *gport);

extern bcm_gport_t
_brcm_sai_gport_get(bcm_port_t pp);

extern sai_status_t
_brcm_sh_process_cmd(char *cmd);

extern sai_status_t
_brcm_sai_miimc45_set(int val);

extern sai_status_t
_brcm_sai_set_switch_attribute(_In_ const sai_attribute_t *attr);

extern sai_status_t
_brcm_sai_get_switch_attribute(_In_ uint32_t attr_count,
                               _Inout_ sai_attribute_t *attr_list);

extern int
_brcm_sai_modport_port_get(bcm_gport_t port);

extern sai_status_t
_brcm_sai_stats_init();

extern sai_status_t
_brcm_sai_tunnel_term_add(int type, _brcm_sai_tunnel_info_t *tinfo,
                          bcm_tunnel_terminator_t *tunnel_term);

extern sai_status_t
_brcm_sai_cosq_gport_discard_set(bcm_gport_t gport, int qid, bool ect, int gn,
                                 _brcm_sai_cosq_gport_discard_t *sai_discard);

extern sai_status_t
_brcm_sai_create_udf(int pktid, int layer, int start, int width, int shared,
                     sai_uint8_t hash_mask[_BRCM_SAI_UDF_HASH_MASK_SIZE],
                     _Out_ int* udf_id);

extern sai_status_t
_brcm_sai_create_udf_match(_Out_ sai_object_id_t* udf_match_id,
                           _In_ uint32_t attr_count,
                           _In_ const sai_attribute_t *attr_list);

extern void
_brcm_sai_driver_shell();

extern sai_status_t
_BRCM_STAT_SAI_TO_BCM(sai_port_stat_t sai_stat,
                      bcm_stat_val_t *stat);

extern sai_status_t
_sai_log_set(_In_ sai_api_t sai_api_id,
             _In_ sai_log_level_t log_level);

extern void _verify(int marker, void *ptr);

extern uint8_t
_brcm_sai_to_syslog(uint8_t _sai_level);

extern int
_brcm_get_hardware_lane_count(int unit, int port,
                              int* lanes);
#endif  /* BRCM_SAI_PRIVATE_APIS_H */
