/*********************************************************************
 *
 * Copyright: (c) 2017 Broadcom.
 * Broadcom Proprietary and Confidential. All rights reserved.
 *
 *********************************************************************/

#include <sai.h>
#include <brcm_sai_common.h>

#ifdef L3_PERF
#include <sys/time.h>
long long avl_lookup_usecs, avl_add_usecs, avl_delete_usecs;
#endif

/*
################################################################################
#          All state thats persistent across WB including schema               #
################################################################################
*/
static _brcm_sai_global_data_t _brcm_sai_global_data;
static char *_global_data_record_name = "global_data";
static const unsigned int _global_data_record_version = 2;
syncdbJsonNode_t _global_data_record_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "sai ver",
        offsetof(_brcm_sai_global_data_t, sai_ver),
        sizeof (_brcm_sai_global_data.sai_ver),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "switch inited",
        offsetof(_brcm_sai_global_data_t, switch_inited),
        sizeof (_brcm_sai_global_data.switch_inited),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "system mac",
        offsetof(_brcm_sai_global_data_t, system_mac),
        sizeof (_brcm_sai_global_data.system_mac),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "vr count",
        offsetof(_brcm_sai_global_data_t, vr_count),
        sizeof (_brcm_sai_global_data.vr_count),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "tunnel rif count",
        offsetof(_brcm_sai_global_data_t, tunnel_rif_count),
        sizeof (_brcm_sai_global_data.tunnel_rif_count),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "wred count",
        offsetof(_brcm_sai_global_data_t, wred_count),
        sizeof (_brcm_sai_global_data.wred_count),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "sched count",
        offsetof(_brcm_sai_global_data_t, sai_scheduler_count),
        sizeof (_brcm_sai_global_data.sai_scheduler_count),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "buff prof count",
        offsetof(_brcm_sai_global_data_t, mmu_buff_profile_count),
        sizeof (_brcm_sai_global_data.mmu_buff_profile_count),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "pfc qmap count",
        offsetof(_brcm_sai_global_data_t, pfc_queue_map_count),
        sizeof (_brcm_sai_global_data.pfc_queue_map_count),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "dot1p tc map count",
        offsetof(_brcm_sai_global_data_t, dot1p_tc_map_count),
        sizeof (_brcm_sai_global_data.dot1p_tc_map_count),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "dot1p color map count",
        offsetof(_brcm_sai_global_data_t, dot1p_color_map_count),
        sizeof (_brcm_sai_global_data.dot1p_color_map_count),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "dscp tc map count",
        offsetof(_brcm_sai_global_data_t, dscp_tc_map_count),
        sizeof (_brcm_sai_global_data.dscp_tc_map_count),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "dscp color map count",
        offsetof(_brcm_sai_global_data_t, dscp_color_map_count),
        sizeof (_brcm_sai_global_data.dscp_color_map_count),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "tc dscp map count",
        offsetof(_brcm_sai_global_data_t, tc_dscp_map_count),
        sizeof (_brcm_sai_global_data.tc_dscp_map_count),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "tc dot1p map count",
        offsetof(_brcm_sai_global_data_t, tc_dot1p_map_count),
        sizeof (_brcm_sai_global_data.tc_dot1p_map_count),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "tc qmap count",
        offsetof(_brcm_sai_global_data_t, tc_queue_map_count),
        sizeof (_brcm_sai_global_data.tc_queue_map_count),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "tc pg map count",
        offsetof(_brcm_sai_global_data_t, tc_pg_map_count),
        sizeof (_brcm_sai_global_data.tc_pg_map_count),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "policer count",
        offsetof(_brcm_sai_global_data_t, policer_count),
        sizeof (_brcm_sai_global_data.policer_count),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "host intf count",
        offsetof(_brcm_sai_global_data_t, host_if_count),
        sizeof (_brcm_sai_global_data.host_if_count),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "drop intf id",
        offsetof(_brcm_sai_global_data_t, drop_if_id),
        sizeof (_brcm_sai_global_data.drop_if_id),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "trap intf id",
        offsetof(_brcm_sai_global_data_t, trap_if_id),
        sizeof (_brcm_sai_global_data.trap_if_id),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "route nh count",
        offsetof(_brcm_sai_global_data_t, route_nh_count),
        sizeof (_brcm_sai_global_data.route_nh_count),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "ecmp nh count",
        offsetof(_brcm_sai_global_data_t, ecmp_nh_count),
        sizeof (_brcm_sai_global_data.ecmp_nh_count),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "udg groups count",
        offsetof(_brcm_sai_global_data_t, udfg_count),
        sizeof (_brcm_sai_global_data.udfg_count),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "hash count",
        offsetof(_brcm_sai_global_data_t, hash_count),
        sizeof (_brcm_sai_global_data.hash_count),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "udf count",
        offsetof(_brcm_sai_global_data_t, udf_count),
        sizeof (_brcm_sai_global_data.udf_count),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "udf shared 0",
        offsetof(_brcm_sai_global_data_t, udf_shared_0),
        sizeof (_brcm_sai_global_data.udf_shared_0),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "udf shared 1",
        offsetof(_brcm_sai_global_data_t, udf_shared_1),
        sizeof (_brcm_sai_global_data.udf_shared_1),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "trap fp group",
        offsetof(_brcm_sai_global_data_t, fp_group),
        sizeof (_brcm_sai_global_data.fp_group),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "trap groups count",
        offsetof(_brcm_sai_global_data_t, trap_groups),
        sizeof (_brcm_sai_global_data.trap_groups),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "traps count",
        offsetof(_brcm_sai_global_data_t, traps),
        sizeof (_brcm_sai_global_data.traps),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "system intf id",
        offsetof(_brcm_sai_global_data_t, system_if_id),
        sizeof (_brcm_sai_global_data.system_if_id),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "system mac set",
        offsetof(_brcm_sai_global_data_t, system_mac_set),
        sizeof (_brcm_sai_global_data.system_mac_set),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "acl tables count",
        offsetof(_brcm_sai_global_data_t, acl_tables_count),
        sizeof (_brcm_sai_global_data.acl_tables_count),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "host if entry count",
        offsetof(_brcm_sai_global_data_t, host_if_entry_count),
        sizeof (_brcm_sai_global_data.host_if_entry_count),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "acl tbl grps count",
        offsetof(_brcm_sai_global_data_t, acl_tbl_grps_count),
        sizeof (_brcm_sai_global_data.acl_tbl_grps_count),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "acl tbl grp membrs count",
        offsetof(_brcm_sai_global_data_t, acl_tbl_grp_membrs_count),
        sizeof (_brcm_sai_global_data.acl_tbl_grp_membrs_count),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "pfc dld",
        offsetof(_brcm_sai_global_data_t, pfc_dld_timers),
        sizeof (_brcm_sai_global_data.pfc_dld_timers),
        {.default_number = 100},
    },
    {
        SYNCDB_JSON_ARRAY, "pfc dlr",
        offsetof(_brcm_sai_global_data_t, pfc_dlr_timers),
        sizeof (_brcm_sai_global_data.pfc_dlr_timers),
        {.default_number = 100},
    },
    {
        SYNCDB_JSON_ARRAY, "bridge ports",
        offsetof(_brcm_sai_global_data_t, bridge_ports),
        sizeof (_brcm_sai_global_data.bridge_ports),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "bridge lag ports",
        offsetof(_brcm_sai_global_data_t, bridge_lag_ports),
        sizeof (_brcm_sai_global_data.bridge_lag_ports),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "warm shut",
        offsetof(_brcm_sai_global_data_t, ws),
        sizeof (_brcm_sai_global_data.ws),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "bcast rule installed",
        offsetof(_brcm_sai_global_data_t, bcast_rule_installed),
        sizeof (_brcm_sai_global_data.bcast_rule_installed),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "bcast entry",
        offsetof(_brcm_sai_global_data_t, bcast_entry),
        sizeof (_brcm_sai_global_data.bcast_entry),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "arp trap req entry",
        offsetof(_brcm_sai_global_data_t, arp_trap_req),
        sizeof (_brcm_sai_global_data.arp_trap_req),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "arp trap resp entry",
        offsetof(_brcm_sai_global_data_t, arp_trap_resp),
        sizeof (_brcm_sai_global_data.arp_trap_resp),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "nbr with nh count",
        offsetof(_brcm_sai_global_data_t, nbr_count),
        sizeof (_brcm_sai_global_data.nbr_count),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "bcast ip count",
        offsetof(_brcm_sai_global_data_t, bcast_ips),
        sizeof (_brcm_sai_global_data.bcast_ips),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "fdb count",
        offsetof(_brcm_sai_global_data_t, fdb_count),
        sizeof (_brcm_sai_global_data.fdb_count),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "egress inuse count",
        offsetof(_brcm_sai_global_data_t, egress_inuse_count),
        sizeof (_brcm_sai_global_data.egress_inuse_count),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "udf hash use count",
        offsetof(_brcm_sai_global_data_t, udf_hash_used),
        sizeof (_brcm_sai_global_data.udf_hash_used),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "ing flex mode id",
        offsetof(_brcm_sai_global_data_t, ing_flex_mode_id),
        sizeof (_brcm_sai_global_data.ing_flex_mode_id),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "egr flex mode id",
        offsetof(_brcm_sai_global_data_t, egr_flex_mode_id),
        sizeof (_brcm_sai_global_data.egr_flex_mode_id),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "range id",
        offsetof(_brcm_sai_global_data_t, range_id),
        sizeof (_brcm_sai_global_data.range_id),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "vxlan udp port",
        offsetof(_brcm_sai_global_data_t, vxlan_udp_port),
        sizeof (_brcm_sai_global_data.vxlan_udp_port),
        {.default_number = 0},
    }
};

static _brcm_sai_vr_info_t *_brcm_sai_vrf_map = NULL;
static char *_vrf_map_table_name = "vrf_map";
static const unsigned int _vrf_map_table_version = 1;
syncdbJsonNode_t _vrf_map_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "vr id",
        offsetof(_brcm_sai_vr_info_t, vr_id),
        sizeof (bcm_vrf_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "vr mac",
        offsetof(_brcm_sai_vr_info_t, vr_mac),
        sizeof (sai_mac_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "ref count",
        offsetof(_brcm_sai_vr_info_t, ref_count),
        sizeof (int),
        {.default_number = 0},
    }
};

static _brcm_sai_rif_info_t *_brcm_sai_rif_info = NULL;
static char *_rif_info_table_name = "rif_info";
static const unsigned int _rif_info_table_version = 1;
syncdbJsonNode_t _rif_info_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "index",
        offsetof(_brcm_sai_rif_info_t, idx),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "valid",
        offsetof(_brcm_sai_rif_info_t, valid),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "vr id",
        offsetof(_brcm_sai_rif_info_t, vr_id),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "vlan obj",
        offsetof(_brcm_sai_rif_info_t, vid_obj),
        sizeof (sai_object_id_t),
        {.default_number = 1},
    },
    {
        SYNCDB_JSON_NUMBER, "station id",
        offsetof(_brcm_sai_rif_info_t, station_id),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "neighbor miss action",
        offsetof(_brcm_sai_rif_info_t, nb_mis_pa),
        sizeof (int),
        {.default_number = 0},
    }
};

static _brcm_sai_port_rif_t *_brcm_sai_port_rif_table = NULL;
static char *_port_rif_table_name = "port_rif";
static const unsigned int _port_rif_table_version = 1;
syncdbJsonNode_t _port_rif_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "idx",
        offsetof(_brcm_sai_port_rif_t, idx),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "rif object",
        offsetof(_brcm_sai_port_rif_t, rif_obj),
        sizeof (sai_object_id_t),
        {.default_number = 0},
    }
};

static _brcm_sai_lag_info_t *_brcm_sai_lag_info_table = NULL;
static char *_lag_info_table_name = "lag_info";
static const unsigned int _lag_info_table_version = 1;
syncdbJsonNode_t _lag_info_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "idx",
        offsetof(_brcm_sai_lag_info_t, idx),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "valid",
        offsetof(_brcm_sai_lag_info_t, valid),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "rif object",
        offsetof(_brcm_sai_lag_info_t, rif_obj),
        sizeof (sai_object_id_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "vid",
        offsetof(_brcm_sai_lag_info_t, vid),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "internal",
        offsetof(_brcm_sai_lag_info_t, internal),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "bcast ip",
        offsetof(_brcm_sai_lag_info_t, bcast_ip),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "vr id",
        offsetof(_brcm_sai_lag_info_t, nbr) + offsetof(_brcm_sai_nbr_t, vr_id),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "addr family",
        offsetof(_brcm_sai_lag_info_t, nbr) + offsetof(_brcm_sai_nbr_t, addr_family),
        sizeof (sai_ip_addr_family_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "ip4",
        offsetof(_brcm_sai_lag_info_t, nbr) + offsetof(_brcm_sai_nbr_t, ip4),
        sizeof (sai_ip4_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "ip6",
        offsetof(_brcm_sai_lag_info_t, nbr) + offsetof(_brcm_sai_nbr_t, ip6),
        sizeof (sai_ip6_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "rif_id",
        offsetof(_brcm_sai_lag_info_t, nbr) + offsetof(_brcm_sai_nbr_t, rif_id),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "acl bind count",
        offsetof(_brcm_sai_lag_info_t, acl_bind_count),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "acl bind list tail",
        offsetof(_brcm_sai_lag_info_t, acl_bind_list),
        sizeof (_brcm_sai_acl_bind_point_t *),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "acl bind list head",
        offsetof(_brcm_sai_lag_info_t, list_head),
        sizeof (_brcm_sai_acl_bind_point_t *),
        {.default_number = 0},
    }
};

static _brcm_sai_vlan_rif_t *_brcm_sai_vlan_rif_table = NULL;
static char *_vlan_rif_table_name = "vlan_rif";
static const unsigned int _vlan_rif_table_version = 1;
syncdbJsonNode_t _vlan_rif_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "idx",
        offsetof(_brcm_sai_vlan_rif_t, idx),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "rif object",
        offsetof(_brcm_sai_vlan_rif_t, rif_obj),
        sizeof (sai_object_id_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "bcast ip",
        offsetof(_brcm_sai_vlan_rif_t, bcast_ip),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "vr id",
        offsetof(_brcm_sai_vlan_rif_t, nbr) + offsetof(_brcm_sai_nbr_t, vr_id),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "addr family",
        offsetof(_brcm_sai_vlan_rif_t, nbr) + offsetof(_brcm_sai_nbr_t, addr_family),
        sizeof (sai_ip_addr_family_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "ip4",
        offsetof(_brcm_sai_vlan_rif_t, nbr) + offsetof(_brcm_sai_nbr_t, ip4),
        sizeof (sai_ip4_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "ip6",
        offsetof(_brcm_sai_vlan_rif_t, nbr) + offsetof(_brcm_sai_nbr_t, ip6),
        sizeof (sai_ip6_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "rif_id",
        offsetof(_brcm_sai_vlan_rif_t, nbr) + offsetof(_brcm_sai_nbr_t, rif_id),
        sizeof (int),
        {.default_number = 0},
    }
};

static _brcm_sai_trif_info_t *_brcm_sai_rif_tunnel_info = NULL;
static char *_rif_tunnel_info_table_name = "rif_tunnel_info";
static const unsigned int _rif_tunnel_info_table_version = 1;
syncdbJsonNode_t _rif_tunnel_info_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "index",
        offsetof(_brcm_sai_trif_info_t, idx),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "valid",
        offsetof(_brcm_sai_trif_info_t, valid),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "vr id",
        offsetof(_brcm_sai_trif_info_t, vr_id),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "station id",
        offsetof(_brcm_sai_trif_info_t, station_id),
        sizeof (int),
        {.default_number = 0},
    }
};

static _brcm_sai_tunnel_info_t *_brcm_sai_tunnel_info = NULL;
static char *_tunnel_info_table_name = "tunnel_info";
static const unsigned int _tunnel_info_table_version = 1;
syncdbJsonNode_t _tunnel_info_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "index",
        offsetof(_brcm_sai_tunnel_info_t, idx),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "valid",
        offsetof(_brcm_sai_tunnel_info_t, valid),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "type",
        offsetof(_brcm_sai_tunnel_info_t, type),
        sizeof (uint32_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "encap ttl mode",
        offsetof(_brcm_sai_tunnel_info_t, encap_ttl_mode),
        sizeof (uint32_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "encap ttl",
        offsetof(_brcm_sai_tunnel_info_t, encap_ttl),
        sizeof (sai_uint8_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "decap ttl mode",
        offsetof(_brcm_sai_tunnel_info_t, decap_ttl_mode),
        sizeof (uint32_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "encap dscp mode",
        offsetof(_brcm_sai_tunnel_info_t, encap_dscp_mode),
        sizeof (uint32_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "encap dscp",
        offsetof(_brcm_sai_tunnel_info_t, encap_dscp),
        sizeof (sai_uint8_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "decap dscp mode",
        offsetof(_brcm_sai_tunnel_info_t, decap_dscp_mode),
        sizeof (uint32_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "ecn",
        offsetof(_brcm_sai_tunnel_info_t, ecn),
        sizeof (uint32_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "ip addr family",
        offsetof(_brcm_sai_tunnel_info_t, ip_addr),
        sizeof (sai_ip_addr_family_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "ip addr",
        offsetof(_brcm_sai_tunnel_info_t, ip_addr) +
            offsetof(sai_ip_address_t, addr),
        sizeof (sai_ip6_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "nh ip addr family",
        offsetof(_brcm_sai_tunnel_info_t, nh_ip_addr),
        sizeof (sai_ip_addr_family_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "nh ip addr",
        offsetof(_brcm_sai_tunnel_info_t, nh_ip_addr) +
            offsetof(sai_ip_address_t, addr),
        sizeof (sai_ip6_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "underlay if",
        offsetof(_brcm_sai_tunnel_info_t, underlay_if),
        sizeof (sai_object_id_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "overlay if",
        offsetof(_brcm_sai_tunnel_info_t, overlay_if),
        sizeof (sai_object_id_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "intf",
        offsetof(_brcm_sai_tunnel_info_t, intf),
        sizeof (bcm_if_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "encap map",
        offsetof(_brcm_sai_tunnel_info_t, encap_map),
        sizeof (sai_object_id_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "decap map",
        offsetof(_brcm_sai_tunnel_info_t, decap_map),
        sizeof (sai_object_id_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "term obj",
        offsetof(_brcm_sai_tunnel_info_t, term_obj),
        sizeof (sai_object_id_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "in use",
        offsetof(_brcm_sai_tunnel_info_t, in_use),
        sizeof (uint32_t),
        {.default_number = 0},
    }
};

static _brcm_sai_tunnel_table_t *_brcm_sai_tunnel_table = NULL;
static char *_tunnel_table_table_name = "tunnel_table";
static const unsigned int _tunnel_table_table_version = 1;
syncdbJsonNode_t _tunnel_table_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "idx",
        offsetof(_brcm_sai_tunnel_table_t, idx),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "valid",
        offsetof(_brcm_sai_tunnel_table_t, valid),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "tid",
        offsetof(_brcm_sai_tunnel_table_t, tid),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "tunnel type",
        offsetof(_brcm_sai_tunnel_table_t, tunnel_type),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "vr id",
        offsetof(_brcm_sai_tunnel_table_t, vr_id),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "dip addr family",
        offsetof(_brcm_sai_tunnel_table_t, dip),
        sizeof (sai_ip_addr_family_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "dip addr",
        offsetof(_brcm_sai_tunnel_table_t, dip) +
            offsetof(sai_ip_address_t, addr),
        sizeof (sai_ip6_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "tunnel_term_entry_type",
        offsetof(_brcm_sai_tunnel_table_t, tunnel_term_entry_type),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "sip addr family",
        offsetof(_brcm_sai_tunnel_table_t, sip),
        sizeof (sai_ip_addr_family_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "sip addr",
        offsetof(_brcm_sai_tunnel_table_t, sip) +
            offsetof(sai_ip_address_t, addr),
        sizeof (sai_ip6_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "hw tunnel id",
        offsetof(_brcm_sai_tunnel_table_t, tunnel_id),
        sizeof (bcm_gport_t),
        {.default_number = 0},
    },
};

static char *_tnl_map_entry_table_name = "tnl_map_entry";
static const unsigned int _tnl_map_entry_table_version = 1;
syncdbJsonNode_t _tnl_map_entry_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "type",
        offsetof(_brcm_sai_tunnel_map_entry_t, type),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "key",
        offsetof(_brcm_sai_tunnel_map_entry_t, key),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "val",
        offsetof(_brcm_sai_tunnel_map_entry_t, val),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "tunnel_map",
        offsetof(_brcm_sai_tunnel_map_entry_t, tunnel_map),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "vpn",
        offsetof(_brcm_sai_tunnel_map_entry_t, vpn),
        sizeof (int),
        {.default_number = 0},
    }
};

static char *_tnl_net_vpn_entry_table_name = "tnl_net_vpn_entry";
static const unsigned int _tnl_net_vpn_entry_table_version = 1;
syncdbJsonNode_t _tnl_net_vpn_entry_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "type",
        offsetof(_brcm_sai_tunnel_net_vpn_entry_t, type),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "src_ip",
        offsetof(_brcm_sai_tunnel_net_vpn_entry_t, src_ip),
        sizeof (bcm_ip_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "dst_ip",
        offsetof(_brcm_sai_tunnel_net_vpn_entry_t, dst_ip),
        sizeof (bcm_ip_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "udp_dp",
        offsetof(_brcm_sai_tunnel_net_vpn_entry_t, udp_dp),
        sizeof (uint16),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "bc_group",
        offsetof(_brcm_sai_tunnel_net_vpn_entry_t, bc_group),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "vpn_id",
        offsetof(_brcm_sai_tunnel_net_vpn_entry_t, vpn_id),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "vni",
        offsetof(_brcm_sai_tunnel_net_vpn_entry_t, vni),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "init_id",
        offsetof(_brcm_sai_tunnel_net_vpn_entry_t, init_id),
        sizeof (bcm_gport_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "term_id",
        offsetof(_brcm_sai_tunnel_net_vpn_entry_t, term_id),
        sizeof (bcm_gport_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "init_id_mc",
        offsetof(_brcm_sai_tunnel_net_vpn_entry_t, init_id_mc),
        sizeof (bcm_gport_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "term_id_mc",
        offsetof(_brcm_sai_tunnel_net_vpn_entry_t, term_id_mc),
        sizeof (bcm_gport_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "ref_count",
        offsetof(_brcm_sai_tunnel_net_vpn_entry_t, ref_count),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "nr_net_ports",
        offsetof(_brcm_sai_tunnel_net_vpn_entry_t, nr_net_ports),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "net_ports",
        offsetof(_brcm_sai_tunnel_net_vpn_entry_t, net_ports),
        sizeof (bcm_port_t) * _BRCM_SAI_MAX_NR_TRUNK_MEMBERS,
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "net_egr_obj",
        offsetof(_brcm_sai_tunnel_net_vpn_entry_t, net_egr_obj),
        sizeof (bcm_if_t) * _BRCM_SAI_MAX_NR_TRUNK_MEMBERS,
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "vxlan_port",
        offsetof(_brcm_sai_tunnel_net_vpn_entry_t, vxlan_port),
        sizeof (bcm_gport_t) * _BRCM_SAI_MAX_NR_TRUNK_MEMBERS,
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "l2_station_id",
        offsetof(_brcm_sai_tunnel_net_vpn_entry_t, l2_station_id),
        sizeof (bcm_gport_t) * _BRCM_SAI_MAX_NR_TRUNK_MEMBERS,
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "net_egr_obj_mc",
        offsetof(_brcm_sai_tunnel_net_vpn_entry_t, net_egr_obj_mc),
        sizeof (bcm_if_t) * _BRCM_SAI_MAX_NR_TRUNK_MEMBERS,
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "vxlan_port_mc",
        offsetof(_brcm_sai_tunnel_net_vpn_entry_t, vxlan_port_mc),
        sizeof (bcm_gport_t) * _BRCM_SAI_MAX_NR_TRUNK_MEMBERS,
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "l2_station_id_mc",
        offsetof(_brcm_sai_tunnel_net_vpn_entry_t, l2_station_id_mc),
        sizeof (bcm_gport_t) * _BRCM_SAI_MAX_NR_TRUNK_MEMBERS,
        {.default_number = 0},
    }
};

static _brcm_sai_qos_wred_t *_brcm_sai_wred = NULL;
static char *_wred_prof_table_name = "wred_prof";
static const unsigned int _wred_prof_table_version = 1;
syncdbJsonNode_t _wred_prof_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "index",
        offsetof(_brcm_sai_qos_wred_t, idx),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "valid",
        offsetof(_brcm_sai_qos_wred_t, valid),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "discard green flags",
        offsetof(_brcm_sai_qos_wred_t, discard_g),
        sizeof (uint32),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "discard green min thresh",
        offsetof(_brcm_sai_qos_wred_t, discard_g) +
            offsetof(bcm_cosq_gport_discard_t, min_thresh),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "discard green max thresh",
        offsetof(_brcm_sai_qos_wred_t, discard_g) +
            offsetof(bcm_cosq_gport_discard_t, max_thresh),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "discard green drop prob",
        offsetof(_brcm_sai_qos_wred_t, discard_g) +
            offsetof(bcm_cosq_gport_discard_t, drop_probability),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "discard green gain",
        offsetof(_brcm_sai_qos_wred_t, discard_g) +
            offsetof(bcm_cosq_gport_discard_t, gain),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "discard green ecn thresh",
        offsetof(_brcm_sai_qos_wred_t, discard_g) +
            offsetof(bcm_cosq_gport_discard_t, ecn_thresh),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "discard green refresh time",
        offsetof(_brcm_sai_qos_wred_t, discard_g) +
            offsetof(bcm_cosq_gport_discard_t, refresh_time),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "discard green use queue group",
        offsetof(_brcm_sai_qos_wred_t, discard_g) +
            offsetof(bcm_cosq_gport_discard_t, use_queue_group),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "discard green profile id",
        offsetof(_brcm_sai_qos_wred_t, discard_g) +
            offsetof(bcm_cosq_gport_discard_t, profile_id),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "discard green et",
        offsetof(_brcm_sai_qos_wred_t, discard_g) +
            offsetof(_brcm_sai_cosq_gport_discard_t, et),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "discard green rt",
        offsetof(_brcm_sai_qos_wred_t, discard_g) +
            offsetof(_brcm_sai_cosq_gport_discard_t, rt),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "discard yellow flags",
        offsetof(_brcm_sai_qos_wred_t, discard_y),
        sizeof (uint32),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "discard yellow min thresh",
        offsetof(_brcm_sai_qos_wred_t, discard_y) +
            offsetof(bcm_cosq_gport_discard_t, min_thresh),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "discard yellow max thresh",
        offsetof(_brcm_sai_qos_wred_t, discard_y) +
            offsetof(bcm_cosq_gport_discard_t, max_thresh),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "discard yellow drop prob",
        offsetof(_brcm_sai_qos_wred_t, discard_y) +
            offsetof(bcm_cosq_gport_discard_t, drop_probability),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "discard yellow gain",
        offsetof(_brcm_sai_qos_wred_t, discard_y) +
            offsetof(bcm_cosq_gport_discard_t, gain),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "discard yellow ecn thresh",
        offsetof(_brcm_sai_qos_wred_t, discard_y) +
            offsetof(bcm_cosq_gport_discard_t, ecn_thresh),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "discard yellow refresh time",
        offsetof(_brcm_sai_qos_wred_t, discard_y) +
            offsetof(bcm_cosq_gport_discard_t, refresh_time),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "discard yellow use queue group",
        offsetof(_brcm_sai_qos_wred_t, discard_y) +
            offsetof(bcm_cosq_gport_discard_t, use_queue_group),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "discard yellow profile id",
        offsetof(_brcm_sai_qos_wred_t, discard_y) +
            offsetof(bcm_cosq_gport_discard_t, profile_id),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "discard yellow et",
        offsetof(_brcm_sai_qos_wred_t, discard_y) +
            offsetof(_brcm_sai_cosq_gport_discard_t, et),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "discard yellow rt",
        offsetof(_brcm_sai_qos_wred_t, discard_y) +
            offsetof(_brcm_sai_cosq_gport_discard_t, rt),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "discard red flags",
        offsetof(_brcm_sai_qos_wred_t, discard_r),
        sizeof (uint32),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "discard red min thresh",
        offsetof(_brcm_sai_qos_wred_t, discard_r) +
            offsetof(bcm_cosq_gport_discard_t, min_thresh),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "discard red max thresh",
        offsetof(_brcm_sai_qos_wred_t, discard_r) +
            offsetof(bcm_cosq_gport_discard_t, max_thresh),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "discard red drop prob",
        offsetof(_brcm_sai_qos_wred_t, discard_r) +
            offsetof(bcm_cosq_gport_discard_t, drop_probability),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "discard red gain",
        offsetof(_brcm_sai_qos_wred_t, discard_r) +
            offsetof(bcm_cosq_gport_discard_t, gain),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "discard red ecn thresh",
        offsetof(_brcm_sai_qos_wred_t, discard_r) +
            offsetof(bcm_cosq_gport_discard_t, ecn_thresh),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "discard red refresh time",
        offsetof(_brcm_sai_qos_wred_t, discard_r) +
            offsetof(bcm_cosq_gport_discard_t, refresh_time),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "discard red use queue group",
        offsetof(_brcm_sai_qos_wred_t, discard_r) +
            offsetof(bcm_cosq_gport_discard_t, use_queue_group),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "discard red profile id",
        offsetof(_brcm_sai_qos_wred_t, discard_r) +
            offsetof(bcm_cosq_gport_discard_t, profile_id),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "discard red et",
        offsetof(_brcm_sai_qos_wred_t, discard_r) +
            offsetof(_brcm_sai_cosq_gport_discard_t, et),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "discard red rt",
        offsetof(_brcm_sai_qos_wred_t, discard_r) +
            offsetof(_brcm_sai_cosq_gport_discard_t, rt),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "discard ect",
        offsetof(_brcm_sai_qos_wred_t, ect),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "discard gn",
        offsetof(_brcm_sai_qos_wred_t, gn),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "port data",
        offsetof(_brcm_sai_qos_wred_t, port_data),
        sizeof (_brcm_sai_queue_bmap_t)*_BRCM_SAI_MAX_PORTS,
        {.default_number = 0},
    }
};

static _brcm_sai_queue_wred_t **_brcm_sai_queue_wred = NULL;
static char *_queue_wred_table_name = "queue_wred";
static const unsigned int _queue_wred_table_version = 1;
syncdbJsonNode_t _queue_wred_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "idx1",
        offsetof(_brcm_sai_queue_wred_t, idx1),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "idx2",
        offsetof(_brcm_sai_queue_wred_t, idx2),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "wred",
        offsetof(_brcm_sai_queue_wred_t, wred),
        sizeof (int),
        {.default_number = 0},
    }
};

static bitmap_t *_brcm_sai_vlan_bmp = NULL;
static char *_vlan_bmp_record_name = "vlan_bmp";
static const unsigned int _vlan_bmp_record_version = 1;
syncdbJsonNode_t _vlan_bmp_record_schema[] =
{
    {
        SYNCDB_JSON_ARRAY, "bmp",
        0,
        BRCM_SAI_NUM_BYTES(_BRCM_SAI_VR_MAX_VID),
        {.default_number = 0},
    }
};

static char *_vlan_membrs_table_name = "vlan_membrs_table";
static const unsigned int _vlan_membrs_table_version = 1;
syncdbJsonNode_t _vlan_membrs_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "vid",
        offsetof(_brcm_sai_vlan_membr_info_t, vid),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "member count",
        offsetof(_brcm_sai_vlan_membr_info_t, membrs_count),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "members end",
        offsetof(_brcm_sai_vlan_membr_info_t, membrs_end),
        sizeof (_brcm_sai_vlan_membr_list_t*),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "members",
        offsetof(_brcm_sai_vlan_membr_info_t, membrs),
        sizeof (_brcm_sai_vlan_membr_list_t*),
        {.default_number = 0},
    }
};

static char *_vlan_membrs_list_name = "vlan_membrs_list_";
static const unsigned int _vlan_membrs_list_version = 1;
syncdbJsonNode_t _vlan_membrs_list_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "type",
        offsetof(_brcm_sai_vlan_membr_t, type),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "val",
        offsetof(_brcm_sai_vlan_membr_t, val),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "acc_intf_id",
        offsetof(_brcm_sai_vlan_membr_t, acc_intf_id),
        sizeof (bcm_if_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "acc_egr_obj",
        offsetof(_brcm_sai_vlan_membr_t, acc_egr_obj),
        sizeof (bcm_if_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "acc_vxlan_port",
        offsetof(_brcm_sai_vlan_membr_t, acc_vxlan_port),
        sizeof (bcm_gport_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "acc_encap_id",
        offsetof(_brcm_sai_vlan_membr_t, acc_encap_id),
        sizeof (bcm_if_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "next",
        offsetof(_brcm_sai_vlan_membr_list_t, next),
        sizeof (_brcm_sai_vlan_membr_list_t*),
        {.default_number = 0},
    }
};

static _brcm_sai_netif_map_t *_brcm_sai_netif_map_port = NULL;
static char *_netif_map_port_table_name = "netif_port_map";
static const unsigned int _netif_port_map_table_version = 1;
syncdbJsonNode_t _netif_port_map_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "idx",
        offsetof(_brcm_sai_netif_map_t, idx),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "netif id",
        offsetof(_brcm_sai_netif_map_t, netif_id),
        sizeof (uint8_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "filters",
        offsetof(_brcm_sai_netif_map_t, filter_id),
        sizeof (uint8_t)*_BRCM_SAI_MAX_FILTERS_PER_INTF,
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "num filters",
        offsetof(_brcm_sai_netif_map_t, num_filters),
        sizeof (int8_t),
        {.default_number = 0},
    }
};

static _brcm_sai_netif_map_t *_brcm_sai_netif_map_vlan = NULL;
static char *_netif_map_vlan_table_name = "netif_vlan_map";
static const unsigned int _netif_vlan_map_table_version = 1;
syncdbJsonNode_t _netif_vlan_map_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "idx",
        offsetof(_brcm_sai_netif_map_t, idx),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "netif id",
        offsetof(_brcm_sai_netif_map_t, netif_id),
        sizeof (uint8_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "filters",
        offsetof(_brcm_sai_netif_map_t, filter_id),
        sizeof (uint8_t)*_BRCM_SAI_MAX_FILTERS_PER_INTF,
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "num filters",
        offsetof(_brcm_sai_netif_map_t, num_filters),
        sizeof (int8_t),
        {.default_number = 0},
    }
};

static _brcm_sai_hostif_t *_brcm_sai_hostif = NULL;
static char *_hostif_table_name = "hostif";
static const unsigned int _hostif_table_version = 1;
syncdbJsonNode_t _hostif_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "idx",
        offsetof(_brcm_sai_hostif_t, idx),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "status",
        offsetof(_brcm_sai_hostif_t, info) + offsetof(_brcm_sai_netif_info_t, status),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "vlan_rif",
        offsetof(_brcm_sai_hostif_t, info) + offsetof(_brcm_sai_netif_info_t, vlan_rif),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "lag",
        offsetof(_brcm_sai_hostif_t, info) + offsetof(_brcm_sai_netif_info_t, lag),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "tag mode",
        offsetof(_brcm_sai_hostif_t, info) + offsetof(_brcm_sai_netif_info_t, tag_mode),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "if obj",
        offsetof(_brcm_sai_hostif_t, info) + offsetof(_brcm_sai_netif_info_t, if_obj),
        sizeof (sai_object_id_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "id",
        offsetof(_brcm_sai_hostif_t, netif) + offsetof(bcm_knet_netif_t, id),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "type",
        offsetof(_brcm_sai_hostif_t, netif) + offsetof(bcm_knet_netif_t, type),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "flags",
        offsetof(_brcm_sai_hostif_t, netif) + offsetof(bcm_knet_netif_t, flags),
        sizeof (uint32_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "mac",
        offsetof(_brcm_sai_hostif_t, netif) + offsetof(bcm_knet_netif_t, mac_addr),
        sizeof (bcm_mac_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "vlan",
        offsetof(_brcm_sai_hostif_t, netif) + offsetof(bcm_knet_netif_t, vlan),
        sizeof (bcm_vlan_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "port",
        offsetof(_brcm_sai_hostif_t, netif) + offsetof(bcm_knet_netif_t, port),
        sizeof (bcm_port_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "cosq",
        offsetof(_brcm_sai_hostif_t, netif) + offsetof(bcm_knet_netif_t, cosq),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "name",
        offsetof(_brcm_sai_hostif_t, netif) + offsetof(bcm_knet_netif_t, name),
        BCM_KNET_NETIF_NAME_MAX,
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "user data",
        offsetof(_brcm_sai_hostif_t, netif) + offsetof(bcm_knet_netif_t, cb_user_data),
        sizeof (int),
        {.default_number = 0},
    }
};

static _brcm_sai_hostif_filter_t **_brcm_sai_hostif_filter = NULL;
static char *_hostif_filter_table_name = "hostif_filter";
static const unsigned int _hostif_filter_table_version = 1;
syncdbJsonNode_t _hostif_filter_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "idx1",
        offsetof(_brcm_sai_hostif_filter_t, idx1),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "idx2",
        offsetof(_brcm_sai_hostif_filter_t, idx2),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "type",
        offsetof(_brcm_sai_hostif_filter_t, type),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "dest type",
        offsetof(_brcm_sai_hostif_filter_t, dest_type),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "flags",
        offsetof(_brcm_sai_hostif_filter_t, flags),
        sizeof (uint32_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "match flags",
        offsetof(_brcm_sai_hostif_filter_t, match_flags),
        sizeof (uint32_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "match ing port",
        offsetof(_brcm_sai_hostif_filter_t, m_ingport),
        sizeof (bcm_port_t),
        {.default_number = 0},
    }
};

static char *_lag_bp_vlan_list_name = "lag_bp_vlan_list_";
static const unsigned int _lag_bp_vlan_list_version = 1;
syncdbJsonNode_t _lag_bp_vlan_list_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "vid",
        offsetof(_brcm_sai_lag_bp_vlan_info_t, vid),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "utag",
        offsetof(_brcm_sai_lag_bp_vlan_info_t, utag),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "next",
        offsetof(_brcm_sai_lag_bp_vlan_info_t, next),
        sizeof (_brcm_sai_lag_bp_vlan_info_t *),
        {.default_number = 0},
    }
};

static _brcm_sai_bridge_lag_port_t *_bridge_lag_ports = NULL;
static char *_bridge_lag_ports_list_name = "bridge_lag_ports";
static const unsigned int _bridge_lag_ports_list_version = 1;
syncdbJsonNode_t _bridge_lag_ports_list_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "oid",
        offsetof(_brcm_sai_bridge_lag_port_t, oid),
        sizeof (sai_object_id_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "bridge port state",
        offsetof(_brcm_sai_bridge_lag_port_t, bridge_port_state),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "learn flags",
        offsetof(_brcm_sai_bridge_lag_port_t, learn_flags),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "vid count",
        offsetof(_brcm_sai_bridge_lag_port_t, vid_count),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "vid list",
        offsetof(_brcm_sai_bridge_lag_port_t, vid_list),
        sizeof (_brcm_sai_lag_bp_vlan_info_t *),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "next",
        offsetof(_brcm_sai_bridge_lag_port_t, next),
        sizeof (_brcm_sai_bridge_lag_port_t *),
        {.default_number = 0},
    }
};

static _brcm_sai_port_qid_t **_brcm_sai_port_qid = NULL;
static char *_port_qid_table_name = "port_qid";
static const unsigned int _port_qid_table_version = 1;
syncdbJsonNode_t _port_qid_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "idx1",
        offsetof(_brcm_sai_port_qid_t, idx1),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "idx2",
        offsetof(_brcm_sai_port_qid_t, idx2),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "qoid",
        offsetof(_brcm_sai_port_qid_t, qoid),
        sizeof (sai_object_id_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "parend sched",
        offsetof(_brcm_sai_port_qid_t, parent_sched),
        sizeof (sai_object_id_t),
        {.default_number = 0},
    }
};

static _brcm_sai_port_info_t *_brcm_sai_port_info = NULL;
static char *_port_info_table_name = "port_info";
static const unsigned int _port_info_table_version = 1;
syncdbJsonNode_t _port_info_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "idx",
        offsetof(_brcm_sai_port_info_t, idx),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "bdg port",
        offsetof(_brcm_sai_port_info_t, bdg_port),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "wred",
        offsetof(_brcm_sai_port_info_t, wred),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "pfc q map",
        offsetof(_brcm_sai_port_info_t, pfc_q_map),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "trunk",
        offsetof(_brcm_sai_port_info_t, trunk),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "tid",
        offsetof(_brcm_sai_port_info_t, tid),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "sched id",
        offsetof(_brcm_sai_port_info_t, sched_id),
        sizeof (sai_object_id_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "bdg port admin state",
        offsetof(_brcm_sai_port_info_t, bdg_port_admin_state),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "learn flags",
        offsetof(_brcm_sai_port_info_t, learn_flags),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "ing_map",
        offsetof(_brcm_sai_port_info_t, flex_stat_map)
        + offsetof (_brcm_sai_flex_stat_info_t, ing_map),
        sizeof (int),
        {.default_number = 0},
    },
      {
        SYNCDB_JSON_NUMBER, "egr_map",
        offsetof(_brcm_sai_port_info_t, flex_stat_map)
        + offsetof (_brcm_sai_flex_stat_info_t, egr_map),
        sizeof (int),
        {.default_number = 0},
    },
      {
        SYNCDB_JSON_NUMBER, "ing_ctr",
        offsetof(_brcm_sai_port_info_t, flex_stat_map)
        + offsetof (_brcm_sai_flex_stat_info_t, ing_ctr),
        sizeof (int),
        {.default_number = 0},
    },
      {
        SYNCDB_JSON_NUMBER, "egr_ctr",
        offsetof(_brcm_sai_port_info_t, flex_stat_map)
        + offsetof (_brcm_sai_flex_stat_info_t, egr_ctr),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "ingress mirror sessions",
        offsetof(_brcm_sai_port_info_t, ingress_ms),
        sizeof(bool) * _BRCM_SAI_MAX_MIRROR_SESSIONS,
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "egress mirror sessions",
        offsetof(_brcm_sai_port_info_t, egress_ms),
        sizeof(bool) * _BRCM_SAI_MAX_MIRROR_SESSIONS,
        {.default_number = 0},
    }
};

static _brcm_sai_port_buff_prof_applied_t **_brcm_sai_port_buff_prof_applied = NULL;
static char *_port_buff_prof_applied_table_name = "port_buff_prof_applied";
static const unsigned int _port_buff_prof_applied_table_version = 1;
syncdbJsonNode_t _port_buff_prof_applied_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "idx1",
        offsetof(_brcm_sai_port_buff_prof_applied_t, idx1),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "idx2",
        offsetof(_brcm_sai_port_buff_prof_applied_t, idx2),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "prof applied",
        offsetof(_brcm_sai_port_buff_prof_applied_t, prof_applied),
        sizeof (bool),
        {.default_number = 0},
    }
};

static _sai_gport_1_t *_brcm_sai_port_sched = NULL;
static char *_port_sched_table_name = "port_sched";
static const unsigned int _port_sched_table_version = 1;
syncdbJsonNode_t _gport_1_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "idx",
        offsetof(_sai_gport_1_t, idx),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "gport",
        offsetof(_sai_gport_1_t, gport),
        sizeof (bcm_gport_t),
        {.default_number = 0},
    }
};

static _sai_gport_1_t *_brcm_sai_CPUMqueue = NULL;
static char *_CPUMqueue_table_name = "CPUMqueue";
static const unsigned int _CPUMqueue_table_version = 1;

static _sai_gport_2_t **_brcm_sai_l0_sched = NULL;
static char *_l0_sched_table_name = "l0_sched";
static const unsigned int _l0_sched_table_version = 1;
syncdbJsonNode_t _gport_2_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "idx1",
        offsetof(_sai_gport_2_t, idx1),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "idx2",
        offsetof(_sai_gport_2_t, idx2),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "gport",
        offsetof(_sai_gport_2_t, gport),
        sizeof (bcm_gport_t),
        {.default_number = 0},
    }
};

static _sai_gport_2_t **_brcm_sai_l1_sched = NULL;
static char *_l1_sched_table_name = "l1_sched";
static const unsigned int _l1_sched_table_version = 1;

static _sai_gport_2_t **_brcm_sai_Uqueue = NULL;
static char *_Uqueue_table_name = "Uqueue";
static const unsigned int _Uqueue_table_version = 1;

static _sai_gport_2_t **_brcm_sai_Mqueue = NULL;
static char *_Mqueue_table_name = "Mqueue";
static const unsigned int _Mqueue_table_version = 1;

static _brcm_sai_qos_ingress_map_t *_brcm_sai_dot1p_tc_map = NULL;
static char *_dot1p_tc_map_table_name = "dot1p_tc_map";
static const unsigned int _dot1p_tc_map_table_version = 1;
syncdbJsonNode_t _qos_ingress_map_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "idx",
        offsetof(_brcm_sai_qos_ingress_map_t, idx),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "valid",
        offsetof(_brcm_sai_qos_ingress_map_t, valid),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "count",
        offsetof(_brcm_sai_qos_ingress_map_t, count),
        sizeof (uint32_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "map",
        offsetof(_brcm_sai_qos_ingress_map_t, map),
        sizeof (sai_qos_map_t) *64,
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "in use",
        offsetof(_brcm_sai_qos_ingress_map_t, inuse),
        sizeof (int),
        {.default_number = 0},
    }
};

static _brcm_sai_qos_ingress_map_t * _brcm_sai_dot1p_color_map = NULL;
static char *_dot1p_color_table_name = "dot1p_color";
static const unsigned int _dot1p_color_table_version = 1;

static _brcm_sai_qos_ingress_map_t * _brcm_sai_dscp_tc_map = NULL;
static char *_dscp_tc_table_name = "dscp_tc";
static const unsigned int _dscp_tc_table_version = 1;

static _brcm_sai_qos_ingress_map_t * _brcm_sai_dscp_color_map = NULL;
static char *_dscp_color_table_name = "dscp_color";
static const unsigned int _dscp_color_table_version = 1;

static _brcm_sai_qos_egress_map_t * _brcm_sai_tc_dscp_map = NULL;
static char *_tc_dscp_map_table_name = "tc_dscp_map";
static const unsigned int _tc_dscp_map_table_version = 1;
syncdbJsonNode_t _qos_egress_map_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "idx",
        offsetof(_brcm_sai_qos_egress_map_t, idx),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "valid",
        offsetof(_brcm_sai_qos_egress_map_t, valid),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "count",
        offsetof(_brcm_sai_qos_egress_map_t, count),
        sizeof (uint32_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "map",
        offsetof(_brcm_sai_qos_egress_map_t, map),
        sizeof (sai_qos_map_t) *16,
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "in use",
        offsetof(_brcm_sai_qos_egress_map_t, inuse),
        sizeof (int),
        {.default_number = 0},
    }
};

static _brcm_sai_qos_egress_map_t * _brcm_sai_tc_dot1p_map = NULL;
static char *_tc_dot1p_map_table_name = "tc_dot1p_map";
static const unsigned int _tc_dot1p_map_table_version = 1;

static _brcm_sai_qos_egress_map_t * _brcm_sai_tc_queue_map = NULL;
static char *_tc_queue_map_table_name = "tc_queue_map";
static const unsigned int _tc_queue_map_table_version = 1;

static _brcm_sai_qos_egress_map_t * _brcm_sai_tc_pg_map = NULL;
static char *_tc_pg_map_table_name = "tc_pg_map";
static const unsigned int _tc_pg_map_table_version = 1;

static _brcm_sai_qos_egress_map_t * _brcm_sai_pfc_queue_map = NULL;
static char *_pfc_queue_map_table_name = "pfc_queue_map";
static const unsigned int _pfc_queue_map_table_version = 1;

static _brcm_sai_buf_pool_t **_brcm_sai_buf_pools = NULL;
static char *_buf_pools_table_name = "buf_pools";
static const unsigned int _buf_pools_table_version = 3;
syncdbJsonNode_t _buf_pools_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "idx1",
        offsetof(_brcm_sai_buf_pool_t, idx1),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "idx2",
        offsetof(_brcm_sai_buf_pool_t, idx2),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "valid",
        offsetof(_brcm_sai_buf_pool_t, valid),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "type",
        offsetof(_brcm_sai_buf_pool_t, type),
        sizeof (sai_buffer_pool_type_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "size",
        offsetof(_brcm_sai_buf_pool_t, size),
        sizeof (sai_uint32_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "xoff thresh",
        offsetof(_brcm_sai_buf_pool_t, xoff_thresh),
        sizeof (sai_uint32_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "mode",
        offsetof(_brcm_sai_buf_pool_t, mode),
        sizeof (sai_buffer_pool_threshold_mode_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "rev gport map",
        offsetof(_brcm_sai_buf_pool_t, bst_rev_gport_map),
        sizeof (bcm_gport_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "rev cos map",
        offsetof(_brcm_sai_buf_pool_t, bst_rev_cos_map),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "rev gport map array",
        offsetof(_brcm_sai_buf_pool_t, bst_rev_gport_maps),
        sizeof (bcm_gport_t) * 2,
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "rev cos map array",
        offsetof(_brcm_sai_buf_pool_t, bst_rev_cos_maps),
        sizeof (int) * 2,
        {.default_number = 0},
    }
};

static _brcm_sai_buf_profile_t *_brcm_sai_buf_profiles = NULL;
static char *_buf_profiles_table_name = "buf_profiles";
static const unsigned int _buf_profiles_table_version = 1;
syncdbJsonNode_t _buf_profiles_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "idx",
        offsetof(_brcm_sai_buf_profile_t, idx),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "valid",
        offsetof(_brcm_sai_buf_profile_t, valid),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "pool id",
        offsetof(_brcm_sai_buf_profile_t, pool_id),
        sizeof (sai_object_id_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "size",
        offsetof(_brcm_sai_buf_profile_t, size),
        sizeof (sai_uint32_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "shared val",
        offsetof(_brcm_sai_buf_profile_t, shared_val),
        sizeof (sai_uint32_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "xoff thresh",
        offsetof(_brcm_sai_buf_profile_t, xoff_thresh),
        sizeof (sai_uint32_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "xon thresh",
        offsetof(_brcm_sai_buf_profile_t, xon_thresh),
        sizeof (sai_uint32_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "mode",
        offsetof(_brcm_sai_buf_profile_t, mode),
        sizeof (sai_buffer_pool_threshold_mode_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "pg data",
        offsetof(_brcm_sai_buf_profile_t, pg_data),
        sizeof (_brcm_sai_queue_bmap_t)*_BRCM_SAI_MAX_PORTS,
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "queue data",
        offsetof(_brcm_sai_buf_profile_t, queue_data),
        sizeof (_brcm_sai_queue_bmap_t)*_BRCM_SAI_MAX_PORTS,
        {.default_number = 0},
    }
};

static _brcm_sai_buf_pool_count_t *_brcm_sai_pool_count = NULL;
static char *_pool_count_table_name = "pool_count";
static const unsigned int _pool_count_table_version = 1;
syncdbJsonNode_t _pool_count_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "idx",
        offsetof(_brcm_sai_buf_pool_count_t, idx),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "count",
        offsetof(_brcm_sai_buf_pool_count_t, count),
        sizeof (int),
        {.default_number = 0},
    }
};

static _brcm_sai_qos_scheduler_t *_brcm_sai_scheduler = NULL;
static char *_scheduler_table_name = "scheduler";
static const unsigned int _scheduler_table_version = 1;
syncdbJsonNode_t _scheduler_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "idx",
        offsetof(_brcm_sai_qos_scheduler_t, idx),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "valid",
        offsetof(_brcm_sai_qos_scheduler_t, valid),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "algorithm",
        offsetof(_brcm_sai_qos_scheduler_t, algorithm),
        sizeof (sai_scheduling_type_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "weight",
        offsetof(_brcm_sai_qos_scheduler_t, weight),
        sizeof (uint8_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "shaper type",
        offsetof(_brcm_sai_qos_scheduler_t, shaper_type),
        sizeof (sai_meter_type_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "minimum bandwidth",
        offsetof(_brcm_sai_qos_scheduler_t, minimum_bandwidth),
        sizeof (uint64_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "minimum burst",
        offsetof(_brcm_sai_qos_scheduler_t, minimum_burst),
        sizeof (uint64_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "maximum bandwidth",
        offsetof(_brcm_sai_qos_scheduler_t, maximum_bandwidth),
        sizeof (uint64_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "maximum burst",
        offsetof(_brcm_sai_qos_scheduler_t, maximum_burst),
        sizeof (uint64_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "ref count",
        offsetof(_brcm_sai_qos_scheduler_t, ref_count),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "table id",
        offsetof(_brcm_sai_qos_scheduler_t, ref_count)+sizeof(int),
        sizeof (_brcm_sai_scheduler_object_t *),
        {.default_number = 0},
    }
};

static char *_scheduler_obj_table_name = "scheduler_obj_";
static const unsigned int _scheduler_obj_table_version = 1;
syncdbJsonNode_t _scheduler_obj_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "object_id",
        offsetof(_brcm_sai_scheduler_object_t, object_id),
        sizeof (sai_object_id_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "next",
        sizeof(sai_object_id_t),
        sizeof (sai_object_id_t),
        {.default_number = 0},
    }
};

static char *_scheduler_group_table_name = "scheduler_group";
static const unsigned int _scheduler_group_table_version = 1;
syncdbJsonNode_t _scheduler_group_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "port",
        offsetof(_brcm_sai_scheduler_group_t, port),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "level",
        offsetof(_brcm_sai_scheduler_group_t, level),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "index",
        offsetof(_brcm_sai_scheduler_group_t, index),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "scheduler oid",
        offsetof(_brcm_sai_scheduler_group_t, scheduler_oid),
        sizeof (sai_object_id_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "parent oid",
        offsetof(_brcm_sai_scheduler_group_t, parent_oid),
        sizeof (sai_object_id_t),
        {.default_number = 0},
    }
};

static _brcm_sai_policer_action_t *_brcm_sai_policer_action = NULL;
static char *_policer_action_table_name = "policer_action";
static const unsigned int _policer_action_table_version = 1;
syncdbJsonNode_t _policer_action_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "pol id",
        offsetof(_brcm_sai_policer_action_t, pol_id),
        sizeof (bcm_policer_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "gpa",
        offsetof(_brcm_sai_policer_action_t, gpa),
        sizeof (sai_packet_action_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "ypa",
        offsetof(_brcm_sai_policer_action_t, ypa),
        sizeof (sai_packet_action_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "rpa",
        offsetof(_brcm_sai_policer_action_t, rpa),
        sizeof (sai_packet_action_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "ref count",
        offsetof(_brcm_sai_policer_action_t, ref_count),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "oids",
        offsetof(_brcm_sai_policer_action_t, oids),
        sizeof (_brcm_sai_policer_oid_map_t *),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "next",
        offsetof(_brcm_sai_policer_action_t, next),
        sizeof (struct _sai_policer_action_s *),
        {.default_number = 0},
    }
};

static char *_policer_oid_map_table_name = "policer_oid_map_";
static const unsigned int _policer_oid_map_table_version = 1;
syncdbJsonNode_t _policer_oid_map_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "oid",
        offsetof(_brcm_sai_policer_oid_map_t, oid),
        sizeof (sai_object_id_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "next",
        sizeof(sai_object_id_t),
        sizeof (sai_object_id_t),
        {.default_number = 0},
    }
};

static _brcm_sai_nh_info_t *_brcm_sai_nh_info = NULL;
static char *_nh_info_table_name = "nh_info";
static const unsigned int _nh_info_table_version = 1;
syncdbJsonNode_t _nh_info_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "idx",
        offsetof(_brcm_sai_nh_info_t, idx),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "act type",
        offsetof(_brcm_sai_nh_info_t, act_type),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "type state",
        offsetof(_brcm_sai_nh_info_t, type_state),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "if id",
        offsetof(_brcm_sai_nh_info_t, if_id),
        sizeof (bcm_if_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "intf type",
        offsetof(_brcm_sai_nh_info_t, intf_type),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "rif obj",
        offsetof(_brcm_sai_nh_info_t, rif_obj),
        sizeof (sai_object_id_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "tid obj",
        offsetof(_brcm_sai_nh_info_t, tid_obj),
        sizeof (sai_object_id_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "vr id",
        offsetof(_brcm_sai_nh_info_t, nbr) + offsetof(_brcm_sai_nbr_t, vr_id),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "addr family",
        offsetof(_brcm_sai_nh_info_t, nbr) + offsetof(_brcm_sai_nbr_t, addr_family),
        sizeof (sai_ip_addr_family_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "ip4",
        offsetof(_brcm_sai_nh_info_t, nbr) + offsetof(_brcm_sai_nbr_t, ip4),
        sizeof (sai_ip4_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "ip6",
        offsetof(_brcm_sai_nh_info_t, nbr) + offsetof(_brcm_sai_nbr_t, ip6),
        sizeof (sai_ip6_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "rif_id",
        offsetof(_brcm_sai_nh_info_t, nbr) + offsetof(_brcm_sai_nbr_t, rif_id),
        sizeof (int),
        {.default_number = 0},
    }
};

static char *_nh_table_name = "nh_table";
static const unsigned int _nh_table_version = 1;
syncdbJsonNode_t _nh_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "nhid",
        offsetof(_brcm_sai_nh_table_t, nhid),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "fwd count",
        offsetof(_brcm_sai_nh_table_t, fwd_count),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "fwd list",
        offsetof(_brcm_sai_nh_table_t, fwd),
        sizeof (_brcm_sai_route_list_t *),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "fwd end",
        offsetof(_brcm_sai_nh_table_t, fwd_end),
        sizeof (_brcm_sai_route_list_t *),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "drop count",
        offsetof(_brcm_sai_nh_table_t, drop_count),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "drop list",
        offsetof(_brcm_sai_nh_table_t, drop),
        sizeof (_brcm_sai_route_list_t *),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "drop end",
        offsetof(_brcm_sai_nh_table_t, drop_end),
        sizeof (_brcm_sai_route_list_t *),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "ecmp count",
        offsetof(_brcm_sai_nh_table_t, ecmp_count),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "ecmp list",
        offsetof(_brcm_sai_nh_table_t, ecmp),
        sizeof (_brcm_sai_nh_ecmp_t *),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "ecmp end",
        offsetof(_brcm_sai_nh_table_t, ecmp_end),
        sizeof (_brcm_sai_nh_ecmp_t *),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "redirect entry count",
        offsetof(_brcm_sai_nh_table_t, entry_count),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "redirect entry list",
        offsetof(_brcm_sai_nh_table_t, entry_list),
        sizeof (_brcm_sai_redirect_entry_list_t *),
        {.default_number = 0},
    }
};

static char *_route_table_name = "route_table";
static const unsigned int _route_table_version = 1;
syncdbJsonNode_t _route_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "vr id",
        offsetof(_brcm_sai_route_info_t, vr_id),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "addr family",
        offsetof(_brcm_sai_route_info_t, addr_family),
        sizeof (sai_ip_addr_family_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "ip4",
        offsetof(_brcm_sai_route_info_t, ip4),
        sizeof (sai_ip4_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "ip4m",
        offsetof(_brcm_sai_route_info_t, ip4m),
        sizeof (sai_ip4_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "ip6",
        offsetof(_brcm_sai_route_info_t, ip6),
        sizeof (sai_ip6_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "ip6m",
        offsetof(_brcm_sai_route_info_t, ip6m),
        sizeof (sai_ip6_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "nh obj",
        offsetof(_brcm_sai_route_table_t, nh_obj),
        sizeof (sai_object_id_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "pkt act",
        offsetof(_brcm_sai_route_table_t, pa),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "nhid",
        offsetof(_brcm_sai_route_table_t, nhid),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "valid",
        offsetof(_brcm_sai_route_table_t, valid),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "multipath",
        offsetof(_brcm_sai_route_table_t, multipath),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "discard",
        offsetof(_brcm_sai_route_table_t, discard),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "state",
        offsetof(_brcm_sai_route_table_t, state),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "ptr",
        offsetof(_brcm_sai_route_table_t, ptr),
        sizeof (_brcm_sai_route_list_t *),
        {.default_number = 0},
    }
};

static char *_route_fwd_list_name = "route_list_fwd_";
static const unsigned int _route_list_version = 1;
syncdbJsonNode_t _route_list_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "vr id",
        offsetof(_brcm_sai_route_info_t, vr_id),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "addr family",
        offsetof(_brcm_sai_route_info_t, addr_family),
        sizeof (sai_ip_addr_family_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "ip4",
        offsetof(_brcm_sai_route_info_t, ip4),
        sizeof (sai_ip4_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "ip4m",
        offsetof(_brcm_sai_route_info_t, ip4m),
        sizeof (sai_ip4_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "ip6",
        offsetof(_brcm_sai_route_info_t, ip6),
        sizeof (sai_ip6_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "ip6m",
        offsetof(_brcm_sai_route_info_t, ip6m),
        sizeof (sai_ip6_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "valid",
        offsetof(_brcm_sai_route_list_t, valid),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "multipath",
        offsetof(_brcm_sai_route_list_t, multipath),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "discard",
        offsetof(_brcm_sai_route_list_t, discard),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "admin state",
        offsetof(_brcm_sai_route_list_t, state),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "prev",
        offsetof(_brcm_sai_route_list_t, prev),
        sizeof (struct _brcm_sai_route_list_t *),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "next",
        offsetof(_brcm_sai_route_list_t, next),
        sizeof (struct _brcm_sai_route_list_t *),
        {.default_number = 0},
    }
};

static char *_route_drop_list_name = "route_list_drop_";

static char *_ecmp_table_name = "ecmp_table";
static const unsigned int _ecmp_table_version = 1;
syncdbJsonNode_t _ecmp_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "intf",
        offsetof(_brcm_sai_ecmp_table_t, intf),
        sizeof (bcm_if_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "nh count",
        offsetof(_brcm_sai_ecmp_table_t, nh_count),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "nh list",
        offsetof(_brcm_sai_ecmp_table_t, nh_list),
        sizeof (_brcm_sai_nh_list_t *),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "list end",
        offsetof(_brcm_sai_ecmp_table_t, end),
        sizeof (_brcm_sai_nh_list_t *),
        {.default_number = 0},
    }
};

static char *_nh_ecmp_list_name = "nh_ecmp_list_";
static const unsigned int _nh_ecmp_list_version = 1;
syncdbJsonNode_t _nh_ecmp_list_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "intf",
        offsetof(_brcm_sai_nh_ecmp_t, intf),
        sizeof (bcm_if_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "next",
        offsetof(_brcm_sai_nh_ecmp_t, next),
        sizeof (_brcm_sai_nh_ecmp_t *),
        {.default_number = 0},
    }
};

static char *_ecmp_nh_list_name = "ecmp_nh_list_";
static const unsigned int _nh_list_version = 1;
syncdbJsonNode_t _nh_list_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "nhid",
        offsetof(_brcm_sai_nh_list_t, nhid),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "next",
        offsetof(_brcm_sai_nh_list_t, next),
        sizeof (_brcm_sai_nh_list_t *),
        {.default_number = 0},
    }
};

static char *_redirect_entry_list_name = "redirect_entry_list_";
static const unsigned int _redirect_entry_list_version = 1;
syncdbJsonNode_t _redirect_entry_list_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "entry",
        offsetof(_brcm_sai_redirect_entry_list_t, entry),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "next",
        offsetof(_brcm_sai_redirect_entry_list_t, next),
        sizeof (_brcm_sai_redirect_entry_list_t *),
        {.default_number = 0},
    }
};

static char *_nbr_info_list_name = "nbr_info_";
static const unsigned int _nbr_info_list_version = 1;
syncdbJsonNode_t _nbr_info_list_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "vr id",
        offsetof(_brcm_sai_nbr_t, vr_id),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "addr family",
        offsetof(_brcm_sai_nbr_t, addr_family),
        sizeof (sai_ip_addr_family_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "ip4",
        offsetof(_brcm_sai_nbr_t, ip4),
        sizeof (sai_ip4_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "ip6",
        offsetof(_brcm_sai_nbr_t, ip6),
        sizeof (sai_ip6_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "rif_id",
        offsetof(_brcm_sai_nbr_t, rif_id),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "l3a intf",
        offsetof(_brcm_sai_nbr_info_t, l3a_intf),
        sizeof (bcm_if_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "pkt action",
        offsetof(_brcm_sai_nbr_info_t, pa),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "next",
        offsetof(_brcm_sai_nbr_info_t, next),
        sizeof (_brcm_sai_nbr_info_t *),
        {.default_number = 0},
    }
};

static char *_mac_nbr_info_table_name = "mac_nbr_table";
static const unsigned int _mac_nbr_info_table_version = 1;
syncdbJsonNode_t _mac_nbr_info_table_schema[] =
{
    {
        SYNCDB_JSON_ARRAY, "mac",
        offsetof(_mac_vid_t, mac),
        sizeof (sai_mac_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "vid",
        offsetof(_mac_vid_t, vid),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "nbrs count",
        offsetof(_brcm_sai_mac_nbr_info_table_t, nbrs_count),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "nbrs list",
        offsetof(_brcm_sai_mac_nbr_info_table_t, nbrs),
        sizeof (_brcm_sai_route_list_t *),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "nbrs end",
        offsetof(_brcm_sai_mac_nbr_info_table_t, nbrs_end),
        sizeof (_brcm_sai_route_list_t *),
        {.default_number = 0},
    }
};

static _brcm_sai_nbr_id_t *_brcm_sai_nbr_id = NULL;
static char *_nbr_id_table_name = "nbr_id";
static const unsigned int _nbr_id_table_version = 1;
syncdbJsonNode_t _nbr_id_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "idx",
        offsetof(_brcm_sai_nbr_id_t, idx),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "valid",
        offsetof(_brcm_sai_nbr_id_t, valid),
        sizeof (int),
        {.default_number = 0},
    },
};

static char *_nbr_nh_list_name = "nbr_nh_list_";

static char *_nbr_eobj_list_name = "nbr_bcast_eobj_list_";
static const unsigned int _nbr_eobj_list_version = 1;
syncdbJsonNode_t _nbr_eobj_list_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "egress obj",
        offsetof(_brcm_sai_egress_objects_t, eoid),
        sizeof (bcm_if_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "type",
        offsetof(_brcm_sai_egress_objects_t, type),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "port or trunk id",
        offsetof(_brcm_sai_egress_objects_t, ptid),
        sizeof (bcm_if_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "next",
        offsetof(_brcm_sai_egress_objects_t, next),
        sizeof (_brcm_sai_egress_objects_t *),
        {.default_number = 0},
    }
};

static char *_nbr_info_table_name = "nbr_info_table";
static const unsigned int _nbr_info_table_version = 1;
syncdbJsonNode_t _nbr_info_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "vr id",
        offsetof(_brcm_sai_nbr_t, vr_id),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "addr family",
        offsetof(_brcm_sai_nbr_t, addr_family),
        sizeof (sai_ip_addr_family_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "ip4",
        offsetof(_brcm_sai_nbr_t, ip4),
        sizeof (sai_ip4_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "ip6",
        offsetof(_brcm_sai_nbr_t, ip6),
        sizeof (sai_ip6_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "rif_id",
        offsetof(_brcm_sai_nbr_t, rif_id),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "id",
        offsetof(_brcm_sai_nbr_table_info_t, id),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "pkt act",
        offsetof(_brcm_sai_nbr_table_info_t, pa),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "nh count",
        offsetof(_brcm_sai_nbr_table_info_t, nh_count),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "nh list",
        offsetof(_brcm_sai_nbr_table_info_t, nhs),
        sizeof (_brcm_sai_nh_list_t *),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "nh end",
        offsetof(_brcm_sai_nbr_table_info_t, nhs_end),
        sizeof (_brcm_sai_nh_list_t *),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "bcast ip",
        offsetof(_brcm_sai_nbr_table_info_t, bcast_ip),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "vlan",
        offsetof(_brcm_sai_nbr_table_info_t, vid),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "fp entry",
        offsetof(_brcm_sai_nbr_table_info_t, entry),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "flags",
        offsetof(_brcm_sai_nbr_table_info_t, flags),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "mac",
        offsetof(_brcm_sai_nbr_table_info_t, mac),
        sizeof (sai_mac_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "if id",
        offsetof(_brcm_sai_nbr_table_info_t, if_id),
        sizeof (bcm_if_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "noroute",
        offsetof(_brcm_sai_nbr_table_info_t, noroute),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "port_tgid",
        offsetof(_brcm_sai_nbr_table_info_t, port_tgid),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "mcast group",
        offsetof(_brcm_sai_nbr_table_info_t, mcg),
        sizeof (bcm_multicast_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "eobj count",
        offsetof(_brcm_sai_nbr_table_info_t, eobj_count),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "egress objects",
        offsetof(_brcm_sai_nbr_table_info_t, eobjs),
        sizeof (_brcm_sai_egress_objects_t *),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "egress objects end",
        offsetof(_brcm_sai_nbr_table_info_t, eobjs_end),
        sizeof (_brcm_sai_egress_objects_t *),
        {.default_number = 0},
    }
};

static _brcm_sai_udfg_info_t *_brcm_sai_udfg = NULL;
static char *_udfg_table_name = "udf_group";
static const unsigned int _udfg_table_version = 1;
syncdbJsonNode_t _udfg_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "idx",
        offsetof(_brcm_sai_udfg_info_t, idx),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "valid",
        offsetof(_brcm_sai_udfg_info_t, valid),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "length",
        offsetof(_brcm_sai_udfg_info_t, length),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "ref count",
        offsetof(_brcm_sai_udfg_info_t, ref_count),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "table id",
        offsetof(_brcm_sai_udfg_info_t, refs),
        sizeof (_brcm_sai_udf_object_t *),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "hash id",
        offsetof(_brcm_sai_udfg_info_t, hid),
        sizeof (int),
        {.default_number = 0},
    }
};

static char *_udf_obj_table_name = "udf_obj_";
static const unsigned int _udf_obj_table_version = 1;
syncdbJsonNode_t _udf_obj_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "object_id",
        offsetof(_brcm_sai_udf_object_t, object_id),
        sizeof (sai_object_id_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "hash_mask",
        offsetof(_brcm_sai_udf_object_t, hash_mask),
        sizeof (sai_uint8_t) * _BRCM_SAI_UDF_HASH_MASK_SIZE,
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "next",
        offsetof(_brcm_sai_udf_object_t, next),
        sizeof (_brcm_sai_udf_object_t *),
        {.default_number = 0},
    }
};

static _brcm_sai_hash_info_t *_brcm_sai_hash = NULL;
static char *_hash_table_name = "hash";
static const unsigned int _hash_table_version = 1;
syncdbJsonNode_t _hash_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "idx",
        offsetof(_brcm_sai_hash_info_t, idx),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "valid",
        offsetof(_brcm_sai_hash_info_t, valid),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "hash field count",
        offsetof(_brcm_sai_hash_info_t, hash_fields_count),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "hash fields",
        offsetof(_brcm_sai_hash_info_t, hash_fields),
        sizeof (int) * _BRCM_SAI_MAX_HASH,
        {.default_number = 0},
    }
};

static _brcm_sai_trap_group_t *_brcm_sai_trap_groups = NULL;
static char *_trap_groups_table_name = "trap_groups";
static const unsigned int _trap_groups_table_version = 1;
syncdbJsonNode_t _trap_groups_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "idx",
        offsetof(_brcm_sai_trap_group_t, idx),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "valid",
        offsetof(_brcm_sai_trap_group_t, valid),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "state",
        offsetof(_brcm_sai_trap_group_t, state),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "queue num",
        offsetof(_brcm_sai_trap_group_t, qnum),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "policer",
        offsetof(_brcm_sai_trap_group_t, policer_id),
        sizeof (sai_object_id_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "ref count",
        offsetof(_brcm_sai_trap_group_t, ref_count),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "traps",
        offsetof(_brcm_sai_trap_group_t, traps),
        sizeof (int),
        {.default_number = 0},
    }
};

static _brcm_sai_trap_t *_brcm_sai_traps = NULL;
static char *_traps_table_name = "traps";
static const unsigned int _traps_table_version = 1;
syncdbJsonNode_t _traps_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "idx",
        offsetof(_brcm_sai_trap_t, idx),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "valid",
        offsetof(_brcm_sai_trap_t, valid),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "installed",
        offsetof(_brcm_sai_trap_t, installed),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "trap id",
        offsetof(_brcm_sai_trap_t, trap_id),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "pkt action",
        offsetof(_brcm_sai_trap_t, pkta),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "priority",
        offsetof(_brcm_sai_trap_t, priority),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "trap group",
        offsetof(_brcm_sai_trap_t, trap_group),
        sizeof (sai_object_id_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "group",
        offsetof(_brcm_sai_trap_t, group),
        sizeof (bcm_field_group_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "entry count",
        offsetof(_brcm_sai_trap_t, count),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "entry array",
        offsetof(_brcm_sai_trap_t, entry),
        sizeof (bcm_field_entry_t) * 4,
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "exclude list",
        offsetof(_brcm_sai_trap_t, exclude_list),
        sizeof (pbmp_t),
        {.default_number = 0},
    }
};

static _brcm_sai_hostif_table_entry_t *_brcm_sai_hostif_table = NULL;
static char *_hostif_trap_table_name = "hostif_table";
static const unsigned int _hostif_trap_table_version = 1;
syncdbJsonNode_t _hostif_trap_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "idx",
        offsetof(_brcm_sai_hostif_table_entry_t, idx),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "valid",
        offsetof(_brcm_sai_hostif_table_entry_t, valid),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "entry_type",
        offsetof(_brcm_sai_hostif_table_entry_t, entry_type),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "if object",
        offsetof(_brcm_sai_hostif_table_entry_t, if_obj),
        sizeof (sai_object_id_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "trap object",
        offsetof(_brcm_sai_hostif_table_entry_t, trap_obj),
        sizeof (sai_object_id_t),
        {.default_number = 0},
    }
};

static char *_trap_refs_table_name = "trap_refs_";
static const unsigned int _trap_refs_table_version = 1;
syncdbJsonNode_t _trap_refs_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "trap",
        offsetof(_brcm_sai_trap_refs_t, trap),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "next",
        offsetof(_brcm_sai_trap_refs_t, next),
        sizeof (_brcm_sai_trap_refs_t *),
        {.default_number = 0},
    }
};

static char *_unicast_arp_table_name = "unicast_arp";
static const unsigned int _unicast_arp_table_version = 1;
syncdbJsonNode_t _unicast_arp_table_schema[] =
{
    {
        SYNCDB_JSON_ARRAY, "mac",
        offsetof(_brcm_sai_unicast_arp_t, mac),
        sizeof (sai_mac_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "type",
        offsetof(_brcm_sai_unicast_arp_t, type),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "entry id",
        offsetof(_brcm_sai_unicast_arp_t, entry_id),
        sizeof (bcm_field_entry_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "ref count",
        offsetof(_brcm_sai_unicast_arp_t, ref_count),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "system mac",
        offsetof(_brcm_sai_unicast_arp_t, sysmac),
        sizeof (bool),
        {.default_number = 0},
    }
};

static _brcm_sai_acl_table_group_t *_brcm_sai_acl_tbl_grps = NULL;
static char *_acl_tbl_grps_table_name = "acl_table_groups";
static const unsigned int _acl_tbl_grps_table_version = 1;
syncdbJsonNode_t _acl_tbl_grps_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "idx",
        offsetof(_brcm_sai_acl_table_group_t, idx),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "valid",
        offsetof(_brcm_sai_acl_table_group_t, valid),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "stage",
        offsetof(_brcm_sai_acl_table_group_t, stage),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "type",
        offsetof(_brcm_sai_acl_table_group_t, type),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "ref count",
        offsetof(_brcm_sai_acl_table_group_t, ref_count),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "acl tables",
        offsetof(_brcm_sai_acl_table_group_t, acl_tables),
        sizeof (_brcm_sai_acl_tbl_grp_membr_tbl_t *),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "bind types count",
        offsetof(_brcm_sai_acl_table_group_t, bind_types_count),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "bind types",
        offsetof(_brcm_sai_acl_table_group_t, bind_types),
        sizeof (int) * _BRCM_SAI_MAX_ACL_BIND_TYPES,
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "bind count",
        offsetof(_brcm_sai_acl_table_group_t, bind_count),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "bind points",
        offsetof(_brcm_sai_acl_table_group_t, bind_pts),
        sizeof (_brcm_sai_acl_bind_point_t *),
        {.default_number = 0},
    }
};

static _brcm_sai_acl_table_t *_brcm_sai_acl_tables = NULL;
static char *_acl_tables_table_name = "acl_tables";
static const unsigned int _acl_tables_table_version = 1;
syncdbJsonNode_t _acl_tables_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "idx",
        offsetof(_brcm_sai_acl_table_t, idx),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "valid",
        offsetof(_brcm_sai_acl_table_t, valid),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "stage",
        offsetof(_brcm_sai_acl_table_t, stage),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "ref count",
        offsetof(_brcm_sai_acl_table_t, ref_count),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "entries",
        offsetof(_brcm_sai_acl_table_t, entries),
        sizeof (_brcm_sai_acl_entry_t *),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "bind types count",
        offsetof(_brcm_sai_acl_table_t, bind_types_count),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "bind types",
        offsetof(_brcm_sai_acl_table_t, bind_types),
        sizeof (int) * _BRCM_SAI_MAX_ACL_BIND_TYPES,
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "bind count",
        offsetof(_brcm_sai_acl_table_t, bind_count),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "bind points",
        offsetof(_brcm_sai_acl_table_t, bind_pts),
        sizeof (_brcm_sai_acl_bind_point_t *),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "group count",
        offsetof(_brcm_sai_acl_table_t, group_count),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "group",
        offsetof(_brcm_sai_acl_table_t, group),
        sizeof (int) * _BRCM_SAI_MAX_ACL_TABLE_GROUPS,
        {.default_number = 0},
    }
};

static _brcm_sai_acl_tbl_grp_membr_t *_brcm_sai_acl_tbl_grp_membrs = NULL;
static char *_acl_table_group_membrs_table_name = "acl_table_group_members";
static const unsigned int _acl_tbl_group_membrs_table_version = 1;
syncdbJsonNode_t _acl_tbl_group_membrs_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "idx",
        offsetof(_brcm_sai_acl_tbl_grp_membr_t, idx),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "valid",
        offsetof(_brcm_sai_acl_tbl_grp_membr_t, valid),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "table group",
        offsetof(_brcm_sai_acl_tbl_grp_membr_t, acl_tbl_grp),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "table",
        offsetof(_brcm_sai_acl_tbl_grp_membr_t, acl_table),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "priority",
        offsetof(_brcm_sai_acl_tbl_grp_membr_t, pri),
        sizeof (int),
        {.default_number = 0},
    }
};

static char *_acl_entry_table_name = "acl_entry_";
static const unsigned int _acl_entry_table_version = 1;
syncdbJsonNode_t _acl_entry_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "id",
        offsetof(_brcm_sai_acl_entry_t, id),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "uid",
        offsetof(_brcm_sai_acl_entry_t, uid),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "state",
        offsetof(_brcm_sai_acl_entry_t, state),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "clone bind mask",
        offsetof(_brcm_sai_acl_entry_t, bind_mask),
        sizeof (uint8_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "next",
        offsetof(_brcm_sai_acl_entry_t, next),
        sizeof (_brcm_sai_acl_entry_t *),
        {.default_number = 0},
    }
};

static char *_acl_group_bind_point_table_name = "acl_group_bind_point_";
static const unsigned int _acl_bind_point_table_version = 1;
syncdbJsonNode_t _acl_bind_point_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "type",
        offsetof(_brcm_sai_acl_bind_point_t, type),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "value",
        offsetof(_brcm_sai_acl_bind_point_t, val),
        sizeof (sai_object_id_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "next",
        offsetof(_brcm_sai_acl_bind_point_t, next),
        sizeof (_brcm_sai_acl_bind_point_t *),
        {.default_number = 0},
    }
};

static char *_acl_table_bind_point_table_name = "acl_table_bind_point_";

/* uses the existing table/scheme info */
static char *_lag_acl_bind_point_table_name = "lag_acl_table_bind_point_";


static char *_acl_table_group_member_table_name = "acl_tbl_grp_membr_";
static const unsigned int _acl_table_group_member_table_version = 1;
syncdbJsonNode_t _acl_table_group_member_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "table",
        offsetof(_brcm_sai_acl_tbl_grp_membr_tbl_t, table),
        sizeof (sai_object_id_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "next",
        offsetof(_brcm_sai_acl_tbl_grp_membr_tbl_t, next),
        sizeof (_brcm_sai_acl_tbl_grp_membr_tbl_t *),
        {.default_number = 0},
    }
};

static _brcm_sai_mirror_session_t *_brcm_sai_mirror_sessions = NULL;
static char *_mirror_sessions_table_name = "mirror_sessions";
static const unsigned int _mirror_sessions_table_version = 1;
syncdbJsonNode_t _mirror_sessions_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "idx",
        offsetof(_brcm_sai_mirror_session_t, idx),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "ref count",
        offsetof(_brcm_sai_mirror_session_t, ref_count),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "dest id",
        offsetof(_brcm_sai_mirror_session_t, destid),
        sizeof (bcm_gport_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "flags",
        offsetof(_brcm_sai_mirror_session_t, flags),
        sizeof (uint32),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "mtp gport",
        offsetof(_brcm_sai_mirror_session_t, gport),
        sizeof (bcm_gport_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "src mac",
        offsetof(_brcm_sai_mirror_session_t, src_mac),
        sizeof (bcm_mac_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "dst mac",
        offsetof(_brcm_sai_mirror_session_t, dst_mac),
        sizeof (bcm_mac_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "tpid",
        offsetof(_brcm_sai_mirror_session_t, tpid),
        sizeof (uint16),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "vlan_id",
        offsetof(_brcm_sai_mirror_session_t, vlan_id),
        sizeof (bcm_vlan_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "version",
        offsetof(_brcm_sai_mirror_session_t, version),
        sizeof (uint8),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "tos",
        offsetof(_brcm_sai_mirror_session_t, tos),
        sizeof (uint8),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "ttl",
        offsetof(_brcm_sai_mirror_session_t, ttl),
        sizeof (uint8),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "src addr",
        offsetof(_brcm_sai_mirror_session_t, src_addr),
        sizeof (bcm_ip_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "dst addr",
        offsetof(_brcm_sai_mirror_session_t, dst_addr),
        sizeof (bcm_ip_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "src6 addr",
        offsetof(_brcm_sai_mirror_session_t, src6_addr),
        sizeof (bcm_ip6_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_ARRAY, "dst6 addr",
        offsetof(_brcm_sai_mirror_session_t, dst6_addr),
        sizeof (bcm_ip6_t),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "gre protocol",
        offsetof(_brcm_sai_mirror_session_t, gre_protocol),
        sizeof (uint16),
        {.default_number = 0},
    }
};

static _brcm_sai_vni_vlan_t *_brcm_sai_vni_vlan = NULL;
static char *_vni_vlan_table_name = "vni_vlan";
static const unsigned int _vni_vlan_table_version = 1;
syncdbJsonNode_t _vni_vlan_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "idx",
        offsetof(_brcm_sai_vni_vlan_t, idx),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "valid",
        offsetof(_brcm_sai_vni_vlan_t, valid),
        sizeof (bool),
        {.default_number = 0},
    }
};

static _brcm_sai_tunnel_map_t *_brcm_sai_tunnel_map = NULL;
static char *_tunnel_map_table_name = "tunnel_map";
static const unsigned int _tunnel_map_table_version = 1;
syncdbJsonNode_t _tunnel_map_table_schema[] =
{
    {
        SYNCDB_JSON_NUMBER, "idx",
        offsetof(_brcm_sai_tunnel_map_t, idx),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "valid",
        offsetof(_brcm_sai_tunnel_map_t, valid),
        sizeof (bool),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "type",
        offsetof(_brcm_sai_tunnel_map_t, type),
        sizeof (int),
        {.default_number = 0},
    },
    {
        SYNCDB_JSON_NUMBER, "tunnel_idx",
        offsetof(_brcm_sai_tunnel_map_t, tunnel_idx),
        sizeof (int),
        {.default_number = 0},
    }
};

/*
################################################################################
#                          Non persistent local state                          #
################################################################################
*/
pid_t sai_pid = -1;
static pid_t db_pid = -1;
static bool syncdb_state = FALSE;
static syncdbClientHandle_t client_id;
static int __brcm_sai_dm_total_alloc = 0;

/*
################################################################################
#                             Forward declarations                             #
################################################################################
*/
STATIC sai_status_t
_brcm_sai_dm_load_idx_data(_brcm_sai_indexed_data_type_t type, int entries[],
                           bool wb);
STATIC sai_status_t
_brcm_sai_list_entry_set(_brcm_sai_list_data_type_t type, void *entry, void *data);

/*
################################################################################
#                                Internal functions                            #
################################################################################
*/
#define _BRCM_SAI_DM_SET_IDX_DATA1(dtype, field) \
    if (IS_NULL(dtype))                          \
    {                                            \
        return SAI_STATUS_NO_MEMORY;             \
    }                                            \
    dtype[index[0]] = data->field;

#define _BRCM_SAI_DM_SET_IDX_DATA1_PTR(dtype, field) \
    if (IS_NULL(dtype))                              \
    {                                                \
        return SAI_STATUS_NO_MEMORY;                 \
    }                                                \
    dtype[index[0]] = *(data->field);

#define _BRCM_SAI_DM_SET_IDX_DATA2_PTR(dtype, field) \
    if (IS_NULL(dtype))                              \
    {                                                \
        return SAI_STATUS_NO_MEMORY;                 \
    }                                                \
    dtype[index[0]][index[1]] = *(data->field);

#define _BRCM_SAI_DM_SET_IDX_DATA2(dtype, field) \
    if (IS_NULL(dtype))                          \
    {                                            \
        return SAI_STATUS_NO_MEMORY;             \
    }                                            \
    dtype[index[0]][index[1]] = data->field;

#define _BRCM_SAI_DM_GET_IDX_DATA1(dtype, field) \
    if (IS_NULL(dtype))                          \
    {                                            \
        return SAI_STATUS_NO_MEMORY;             \
    }                                            \
    data->field = dtype[index[0]];

#define _BRCM_SAI_DM_GET_IDX_DATA1_PTR(dtype, field) \
    if (IS_NULL(data))                               \
    {                                                \
        return SAI_STATUS_NO_MEMORY;                 \
    }                                                \
    data->field = &dtype[index[0]];

#define _BRCM_SAI_DM_GET_IDX_DATA2_PTR(dtype, field) \
    if (IS_NULL(data))                               \
    {                                                \
        return SAI_STATUS_NO_MEMORY;                 \
    }                                                \
    data->field = &dtype[index[0]][index[1]];

#define _BRCM_SAI_DM_GET_IDX_DATA2(dtype, field) \
    if (IS_NULL(dtype))                          \
    {                                            \
        return SAI_STATUS_NO_MEMORY;             \
    }                                            \
    data->field = dtype[index[0]][index[1]];

#define _BRCM_SAI_DM_IDX_RSV(data, dtype, field, start, end, value) \
    {                                                               \
        int _i;                                                     \
        if (IS_NULL(data))                                          \
        {                                                           \
            return SAI_STATUS_NO_MEMORY;                            \
        }                                                           \
        for (_i=start; _i<=end; _i++)                               \
        {                                                           \
            if (0 == data[_i].field)                                \
            {                                                       \
                break;                                              \
            }                                                       \
        }                                                           \
        if (_i > end)                                               \
        {                                                           \
            return SAI_STATUS_INSUFFICIENT_RESOURCES;               \
        }                                                           \
        DATA_CLEAR(data[_i], dtype);                                \
        data[_i].field = value ? value : _i;                        \
        *index = _i;                                                \
    }

#define _BRCM_SAI_DM_IDX_RSV2(data, field, start, end, i1, value) \
    {                                                             \
        int _i;                                                   \
        if (IS_NULL(data))                                        \
        {                                                         \
            return SAI_STATUS_NO_MEMORY;                          \
        }                                                         \
        for (_i=start; _i<=end; _i++)                             \
        {                                                         \
            if (0 == data[i1][_i].field)                          \
            {                                                     \
                break;                                            \
            }                                                     \
        }                                                         \
        if (_i > end)                                             \
        {                                                         \
            return SAI_STATUS_INSUFFICIENT_RESOURCES;             \
        }                                                         \
        data[i1][_i].field = value ? value : _i;                  \
        *index = _i;                                              \
    }

#define _BRCM_SAI_DM_LOAD_IDX_DATA(name, tschema, version, size, data,    \
                                   field, key_size, key_field, entries,   \
                                   ecount)                                \
    {                                                                     \
        /* Create DB table */                                             \
        rv = syncdbUtilSchemaCreate(tschema, sizeof(tschema), schema,     \
                                    sizeof(schema), &schema_size,         \
                                    size, &schema_error);                 \
        BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,       \
                            "Creating table schema", rv);                 \
        if (wb)                                                           \
        {                                                                 \
            flags |= SYNCDB_TABLE_FLAG_FILE_LOAD;                         \
        }                                                                 \
        rv = syncdbAvlTableCreate(&client_id, name, version, entries,     \
                                  entries, size, key_size,                \
                                  flags, schema, schema_size);            \
        BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,       \
                            "Creating table", rv);                        \
        if (wb)                                                           \
        {                                                                 \
            /* Load required table entries from DB */                     \
            if (ecount != 0)                                              \
            {                                                             \
                do                                                        \
                {                                                         \
                    rv = syncdbGetNext(&client_id, name, &data,           \
                                       size, &pend);                      \
                    if (SYNCDB_OK == rv)                                  \
                    {                                                     \
                        idata.field = data;                               \
                        if (-1 == idata.field.key_field)                  \
                        {                                                 \
                            idata.field.key_field = 0;                    \
                        }                                                 \
                        rc = _brcm_sai_indexed_data_set(type,             \
                                 &idata.field.key_field, &idata);         \
                        BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH,               \
                                            SAI_LOG_LEVEL_CRITICAL,       \
                                            "Adding indexed data", rc);   \
                        count++;                                          \
                    }                                                     \
                } while(SYNCDB_OK == rv);                                 \
                BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG,                    \
                    "Expected table count: %d, retrieved count: %d\n",    \
                    ecount, count);                                       \
            }                                                             \
            rv = syncdbTableDelete(&client_id, name);                     \
            BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,   \
                                "Deleting old table", rv);                \
            flags &= ~SYNCDB_TABLE_FLAG_FILE_LOAD;                        \
            rv = syncdbAvlTableCreate(&client_id, name, version, entries, \
                                  entries, size, key_size,                \
                                  flags, schema, schema_size);            \
            BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,   \
                                "Re-creating table", rv);                 \
        }                                                                 \
    }

#define _BRCM_SAI_DM_LOAD_IDX_DATA_PTR(name, tschema, version, size, data, \
                                       data_addr, field, key_size,         \
                                       key_field, entries, ecount)         \
    {                                                                      \
        /* Create DB table */                                              \
        rv = syncdbUtilSchemaCreate(tschema, sizeof(tschema), schema,      \
                                    sizeof(schema), &schema_size,          \
                                    size, &schema_error);                  \
        BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,        \
                            "Creating table schema", rv);                  \
        if (wb)                                                            \
        {                                                                  \
            flags |= SYNCDB_TABLE_FLAG_FILE_LOAD;                          \
        }                                                                  \
        rv = syncdbAvlTableCreate(&client_id, name, version, entries,      \
                                  entries, size, key_size,                 \
                                  flags, schema, schema_size);             \
        BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,        \
                            "Creating table", rv);                         \
        if (wb)                                                            \
        {                                                                  \
            /* Load required table entries from DB */                      \
            if (ecount != 0)                                               \
            {                                                              \
                bool mod_zero;                                             \
                do                                                         \
                {                                                          \
                    mod_zero = FALSE;                                      \
                    rv = syncdbGetNext(&client_id, name, &data,            \
                                       size, &pend);                       \
                    if (SYNCDB_OK == rv)                                   \
                    {                                                      \
                        idata.field = data_addr;                           \
                        if (-1 == idata.field->key_field)                  \
                        {                                                  \
                            idata.field->key_field = 0;                    \
                            mod_zero = TRUE;                               \
                        }                                                  \
                        rc = _brcm_sai_indexed_data_set(type,              \
                                 &(idata.field->key_field), &idata);       \
                        BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH,                \
                                            SAI_LOG_LEVEL_CRITICAL,        \
                                            "Adding indexed data", rc);    \
                        count++;                                           \
                        if (mod_zero)                                      \
                        {                                                  \
                            idata.field->key_field = -1;                   \
                        }                                                  \
                    }                                                      \
                } while(SYNCDB_OK == rv);                                  \
                BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG,                     \
                    "Expected table count: %d, retrieved count: %d\n",     \
                    ecount, count);                                        \
            }                                                              \
            rv = syncdbTableDelete(&client_id, name);                      \
            BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,    \
                                "Deleting old table", rv);                 \
            flags &= ~SYNCDB_TABLE_FLAG_FILE_LOAD;                         \
            rv = syncdbAvlTableCreate(&client_id, name, version, entries,  \
                                  entries, size, key_size,                 \
                                  flags, schema, schema_size);             \
            BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,    \
                            "Re-creating table", rv);                      \
        }                                                                  \
    }

#define _BRCM_SAI_DM_IDX_INIT1(dtype, data)                                  \
    if (IS_NULL(data))                                                       \
    {                                                                        \
        sai_status_t rv;                                                     \
        data = (dtype*)ALLOC_CLEAR(1, entries * sizeof(dtype));              \
        if (IS_NULL(data))                                                   \
        {                                                                    \
            BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_ERROR,                           \
                                "%s: Error allocating memory "               \
                                "%d indexed state.\n",__FUNCTION__, type);   \
            return SAI_STATUS_NO_MEMORY;                                     \
        }                                                                    \
        /* Load from DB */                                                   \
        rv = _brcm_sai_dm_load_idx_data(type, &entries, wb);                 \
        if (SAI_STATUS_SUCCESS != rv)                                        \
        {                                                                    \
            BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_ERROR, "%s: Error loading %d "   \
                                "indexed state from DB.\n",                  \
                                __FUNCTION__, type);                         \
            return rv;                                                       \
        }                                                                    \
        __brcm_sai_dm_total_alloc += (entries * sizeof(dtype));              \
    }

#define _BRCM_SAI_DM_IDX_INIT2(dtype, data)                              \
    if (IS_NULL(data))                                                   \
    {                                                                    \
        int _e, __e[2];                                                  \
        sai_status_t rv;                                                 \
        data = (dtype**)ALLOC_CLEAR(1, entries1 * sizeof(dtype*));       \
        if (IS_NULL(data))                                               \
        {                                                                \
            BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_ERROR,                       \
                                "%s: Error allocating "                  \
                                "memory %d indexed state.\n",            \
                __FUNCTION__, type);                                     \
            return SAI_STATUS_NO_MEMORY;                                 \
        }                                                                \
        __brcm_sai_dm_total_alloc += (entries1 * sizeof(dtype*));        \
        for (_e = 0; _e < entries1; _e++)                                \
        {                                                                \
            data[_e] = (dtype*)ALLOC_CLEAR(1, entries2 * sizeof(dtype)); \
            if (IS_NULL(data[_e]))                                       \
            {                                                            \
                BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_ERROR,                   \
                     "%s: Error allocating memory %d indexed state.\n",  \
                     __FUNCTION__, type);                                \
                return SAI_STATUS_NO_MEMORY;                             \
            }                                                            \
            __brcm_sai_dm_total_alloc += (entries2 * sizeof(dtype));     \
        }                                                                \
        /* Load from DB */                                               \
        __e[0] = entries1; __e[1] = entries2;                            \
        rv = _brcm_sai_dm_load_idx_data(type, __e, wb);                  \
        if (SAI_STATUS_SUCCESS != rv)                                    \
        {                                                                \
            BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_ERROR,                       \
                                "%s: Error loading %d "                  \
                                "indexed state from DB.\n",              \
                __FUNCTION__, type);                                     \
            return rv;                                                   \
        }                                                                \
    }

#define _BRCM_SAI_DM_IDX_FREE1(name, dtype, data, field)           \
    if (!IS_NULL(data))                                            \
    {                                                              \
        if (wb && ecount != 0)                                     \
        {                                                          \
            int _e;                                                \
            sai_status_t rv;                                       \
            for (_e=start; _e<end; _e++)                           \
            {                                                      \
                if (data[_e].field)                                \
                {                                                  \
                    rv = syncdbInsert(&client_id, name, &data[_e], \
                                      sizeof(dtype));              \
                    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH,            \
                                        SAI_LOG_LEVEL_CRITICAL,    \
                                        "DB table insert", rv);    \
                }                                                  \
            }                                                      \
        }                                                          \
        __brcm_sai_dm_total_alloc -= CHECK_FREE_CLEAR_SIZE(data);  \
        data = NULL;                                               \
    }

#define _BRCM_SAI_DM_IDX_FREE2(name, dtype, data, field1, field2)          \
    if (!IS_NULL(data))                                                    \
    {                                                                      \
        int _e, _f;                                                        \
        sai_status_t rv;                                                   \
        if (wb)                                                            \
        {                                                                  \
            data[0][0].field1 = -1;                                        \
            /* Store to DB */                                              \
            for (_e = 0; _e < entries1; _e++)                              \
            {                                                              \
                for (_f = 0; _f < entries2; _f++)                          \
                {                                                          \
                    if (data[_e][_f].field1 || data[_e][_f].field2)        \
                    {                                                      \
                        rv = syncdbInsert(&client_id, name, &data[_e][_f], \
                                          sizeof(dtype));                  \
                        BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH,                \
                                            SAI_LOG_LEVEL_CRITICAL,        \
                                            "DB table insert", rv);        \
                    }                                                      \
                }                                                          \
            }                                                              \
        }                                                                  \
        for (_e = 0; _e < entries1; _e++)                                  \
        {                                                                  \
            __brcm_sai_dm_total_alloc -= CHECK_FREE_SIZE(data[_e]);        \
            data[_e] = NULL;                                               \
        }                                                                  \
        __brcm_sai_dm_total_alloc -= CHECK_FREE_CLEAR_SIZE(data);          \
        data = NULL;                                                       \
    }

/* Note: Used when an id needs to be setup in place of the next ptr for
 *       easy readibility of the JSON file.
 */
#define _BRCM_SAI_DM_LIST_FREE(name, tschema, version, dtype,          \
                               field, key_size, next, base)            \
{                                                                      \
    dtype *prev, *current = (dtype *)base;                             \
    sai_status_t rv;                                                   \
                                                                       \
    if (wb)                                                            \
    {                                                                  \
        if (id)                                                        \
        {                                                              \
            /* Create DB Table with given id and name */               \
            sprintf(table_name, "%s%d", name, id);                     \
        }                                                              \
        else                                                           \
        {                                                              \
            sprintf(table_name, "%s", name);                           \
        }                                                              \
        rv = syncdbUtilSchemaCreate(tschema, sizeof(tschema), schema,  \
                                    sizeof(schema), &schema_size,      \
                                    sizeof(dtype), &schema_error);     \
        BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,    \
                            "Creating table schema", rv);              \
        rv = syncdbAvlTableCreate(&client_id, table_name, version,     \
                                  entries, entries, sizeof(dtype),     \
                                  key_size, flags, schema,             \
                                  schema_size);                        \
        if (SYNCDB_DUPNAME == rv)                                      \
        {                                                              \
            rv = syncdbTableDelete(&client_id, table_name);            \
            BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH,                        \
                                SAI_LOG_LEVEL_CRITICAL,                \
                                "Deleting old table", rv);             \
            rv = syncdbAvlTableCreate(&client_id, table_name, version, \
                                      entries, entries, sizeof(dtype), \
                                      key_size, flags, schema,         \
                                      schema_size);                    \
        }                                                              \
        BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,    \
                            "Creating table", rv);                     \
    }                                                                  \
    while (NULL != current)                                            \
    {                                                                  \
        prev = current;                                                \
        current = current->next;                                       \
        if (wb)                                                        \
        {                                                              \
           prev->next = current ?                                      \
                        (dtype*)(uint64_t)current->field : NULL;       \
           /* Add prev entry to DB table */                            \
           rv = syncdbInsert(&client_id, table_name, prev,             \
                             sizeof(dtype));                           \
           BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL, \
                               "DB table insert", rv);                 \
        }                                                              \
        __brcm_sai_dm_total_alloc -= CHECK_FREE_SIZE(prev);            \
        prev = NULL;                                                   \
    }                                                                  \
    base = NULL;                                                       \
}

/* Note: Used when prev->next needs to be 0 */
#define _BRCM_SAI_DM_LIST_FREE_STRUCT(name, tschema, version, dtype,   \
                                      key_size, next, base, has_prev,  \
                                      prev_field)                      \
{                                                                      \
    dtype *prev, *current = (dtype *)base;                             \
    sai_status_t rv;                                                   \
                                                                       \
    if (wb)                                                            \
    {                                                                  \
        if (id)                                                        \
        {                                                              \
            /* Create DB Table with given id and name */               \
            sprintf(table_name, "%s%d", name, id);                     \
        }                                                              \
        else                                                           \
        {                                                              \
            sprintf(table_name, "%s", name);                           \
        }                                                              \
        rv = syncdbUtilSchemaCreate(tschema, sizeof(tschema), schema,  \
                                    sizeof(schema), &schema_size,      \
                                    sizeof(dtype), &schema_error);     \
        BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,    \
                            "Creating table schema", rv);              \
        rv = syncdbAvlTableCreate(&client_id, table_name, version,     \
                                  entries, entries, sizeof(dtype),     \
                                  key_size, flags, schema,             \
                                  schema_size);                        \
        if (SYNCDB_DUPNAME == rv)                                      \
        {                                                              \
            rv = syncdbTableDelete(&client_id, table_name);            \
            BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH,                        \
                                SAI_LOG_LEVEL_CRITICAL,                \
                                "Deleting old table", rv);             \
            rv = syncdbAvlTableCreate(&client_id, table_name, version, \
                                      entries, entries, sizeof(dtype), \
                                      key_size, flags, schema,         \
                                      schema_size);                    \
        }                                                              \
        BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,    \
                            "Creating table", rv);                     \
    }                                                                  \
    while (NULL != current)                                            \
    {                                                                  \
        prev = current;                                                \
        current = current->next;                                       \
        if (wb)                                                        \
        {                                                              \
           prev->next = NULL;                                          \
           if (TRUE == has_prev && prev->prev_field)                   \
           {                                                           \
               prev->prev_field = NULL;                                \
           }                                                           \
           /* Add prev entry to DB table */                            \
           rv = syncdbInsert(&client_id, table_name, prev,             \
                             sizeof(dtype));                           \
           BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL, \
                               "DB table insert", rv);                 \
        }                                                              \
        __brcm_sai_dm_total_alloc -= CHECK_FREE_SIZE(prev);            \
        prev = NULL;                                                   \
    }                                                                  \
    base = NULL;                                                       \
}

/* Note: Used when prev->next needs to be 0 */
#define _BRCM_SAI_DM_LIST_FREE_STRUCT_STR(name, tschema, version,      \
                                          dtype, key_size, next, base, \
                                          has_prev, prev_field)        \
{                                                                      \
    dtype *prev, *current = (dtype *)base;                             \
    sai_status_t rv;                                                   \
                                                                       \
    if (wb)                                                            \
    {                                                                  \
        if (str)                                                       \
        {                                                              \
            /* Create DB Table with given id and name */               \
            sprintf(table_name, "%s%s", name, str);                    \
        }                                                              \
        else                                                           \
        {                                                              \
            sprintf(table_name, "%s", name);                           \
        }                                                              \
        rv = syncdbUtilSchemaCreate(tschema, sizeof(tschema), schema,  \
                                    sizeof(schema), &schema_size,      \
                                    sizeof(dtype), &schema_error);     \
        BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,    \
                            "Creating table schema", rv);              \
        rv = syncdbAvlTableCreate(&client_id, table_name, version,     \
                                  entries, entries, sizeof(dtype),     \
                                  key_size, flags, schema,             \
                                  schema_size);                        \
        if (SYNCDB_DUPNAME == rv)                                      \
        {                                                              \
            rv = syncdbTableDelete(&client_id, table_name);            \
            BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH,                        \
                                SAI_LOG_LEVEL_CRITICAL,                \
                                "Deleting old table", rv);             \
            rv = syncdbAvlTableCreate(&client_id, table_name, version, \
                                      entries, entries, sizeof(dtype), \
                                      key_size, flags, schema,         \
                                      schema_size);                    \
        }                                                              \
        BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,    \
                            "Creating table", rv);                     \
    }                                                                  \
    while (NULL != current)                                            \
    {                                                                  \
        prev = current;                                                \
        current = current->next;                                       \
        if (wb)                                                        \
        {                                                              \
           prev->next = NULL;                                          \
           if (TRUE == has_prev && prev->prev_field)                   \
           {                                                           \
               prev->prev_field = NULL;                                \
           }                                                           \
           /* Add prev entry to DB table */                            \
           rv = syncdbInsert(&client_id, table_name, prev,             \
                             sizeof(dtype));                           \
           BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL, \
                               "DB table insert", rv);                 \
        }                                                              \
        __brcm_sai_dm_total_alloc -= CHECK_FREE_SIZE(prev);            \
        prev = NULL;                                                   \
    }                                                                  \
    base = NULL;                                                       \
}

#define _BRCM_SAI_DM_LIST_ALLOC(name, tschema, version, dtype, data,    \
                                key_size, base)                         \
{                                                                       \
    int _l, pend;                                                       \
    sai_status_t rv;                                                    \
    dtype *entry, *prev;                                                \
                                                                        \
    if (wb)                                                             \
    {                                                                   \
        if (id)                                                         \
        {                                                               \
            /* Create DB Table with given id and name */                \
            sprintf(table_name, "%s%d", name, id);                      \
        }                                                               \
        else                                                            \
        {                                                               \
            sprintf(table_name, "%s", name);                            \
        }                                                               \
        rv = syncdbUtilSchemaCreate(tschema, sizeof(tschema), schema,   \
                                    sizeof(schema), &schema_size,       \
                                    sizeof(dtype), &schema_error);      \
        BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,     \
                            "Creating table schema", rv);               \
        rv = syncdbAvlTableCreate(&client_id, table_name, version,      \
                                  entries, entries, sizeof(dtype),      \
                                  key_size, flags, schema,              \
                                  schema_size);                         \
        BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,     \
                            "Creating table", rv);                      \
    }                                                                   \
    for (_l=0; _l<entries; _l++)                                        \
    {                                                                   \
        entry = (dtype*)ALLOC_CLEAR(1, sizeof(dtype));                  \
        if (IS_NULL(entry))                                             \
        {                                                               \
            BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_ERROR,                      \
                                "%s: Error allocating "                 \
                                "%d memory list state.\n",              \
                                __FUNCTION__, type);                    \
            return SAI_STATUS_NO_MEMORY;                                \
        }                                                               \
        __brcm_sai_dm_total_alloc += sizeof(dtype);                     \
        if (!_l)                                                        \
        {                                                               \
            *base = entry;                                              \
        }                                                               \
        else                                                            \
        {                                                               \
            prev->next = entry;                                         \
        }                                                               \
        if (wb)                                                         \
        {                                                               \
            /* Read and store entry from DB table id */                 \
            rv = syncdbGetNext(&client_id, table_name, &data,           \
                               sizeof(dtype), &pend);                   \
            BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL, \
                                "Get next", rv);                        \
            rv = _brcm_sai_list_entry_set(type, entry, &data);          \
            entry->next = NULL;                                         \
            BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL, \
                                "list entry set", rv);                  \
        }                                                               \
        prev = entry;                                                   \
    }                                                                   \
    if (wb)                                                             \
    {                                                                   \
        rv = syncdbTableDelete(&client_id, table_name);                 \
        BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH,                             \
                            SAI_LOG_LEVEL_CRITICAL,                     \
                            "Deleting table", rv);                      \
    }                                                                   \
}

#define _BRCM_SAI_DM_LIST_ALLOC_END(name, tschema, version, dtype,      \
                                    data, key_size, has_prev,           \
                                    prev_field, has_parent, pdata,      \
                                    src, dest)                          \
{                                                                       \
    int _l, pend;                                                       \
    sai_status_t rv;                                                    \
    dtype *entry, *prev;                                                \
                                                                        \
    if (wb)                                                             \
    {                                                                   \
        if (id)                                                         \
        {                                                               \
            /* Create DB Table with given id and name */                \
            sprintf(table_name, "%s%d", name, id);                      \
        }                                                               \
        else                                                            \
        {                                                               \
            sprintf(table_name, "%s", name);                            \
        }                                                               \
        rv = syncdbUtilSchemaCreate(tschema, sizeof(tschema), schema,   \
                                    sizeof(schema), &schema_size,       \
                                    sizeof(dtype), &schema_error);      \
        BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,     \
                            "Creating table schema", rv);               \
        rv = syncdbAvlTableCreate(&client_id, table_name, version,      \
                                  entries, entries, sizeof(dtype),      \
                                  key_size, flags, schema,              \
                                  schema_size);                         \
        BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,     \
                            "Creating table", rv);                      \
    }                                                                   \
    for (_l=0; _l<entries; _l++)                                        \
    {                                                                   \
        entry = (dtype*)ALLOC_CLEAR(1, sizeof(dtype));                  \
        if (IS_NULL(entry))                                             \
        {                                                               \
            BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_ERROR,                      \
                                "%s: Error allocating "                 \
                                "%d memory list state.\n",              \
                                __FUNCTION__, type);                    \
            return SAI_STATUS_NO_MEMORY;                                \
        }                                                               \
        __brcm_sai_dm_total_alloc += sizeof(dtype);                     \
        if (!_l)                                                        \
        {                                                               \
            *base = entry;                                              \
        }                                                               \
        else                                                            \
        {                                                               \
            prev->next = entry;                                         \
        }                                                               \
        if (wb)                                                         \
        {                                                               \
            /* Read and store entry from DB table id */                 \
            rv = syncdbGetNext(&client_id, table_name, &data,           \
                               sizeof(dtype), &pend);                   \
            BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL, \
                                "Get next", rv);                        \
            rv = _brcm_sai_list_entry_set(type, entry, &data);          \
            entry->next = NULL;                                         \
            BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL, \
                                "list entry set", rv);                  \
            if (TRUE == has_prev)                                       \
            {                                                           \
                entry->prev_field = prev;                               \
            }                                                           \
            if (TRUE == has_parent)                                     \
            {                                                           \
                sal_memcpy(&pdata.dest, &entry->src,                    \
                           sizeof(pdata.dest));                         \
                rv = _brcm_sai_db_table_entry_restore(parent_table,     \
                                                      &parent_data,     \
                                                      entry);           \
                BRCM_SAI_RV_CHK(SAI_API_SWITCH, "route DB table entry " \
                                "restore", rv);                         \
            }                                                           \
        }                                                               \
        prev = entry;                                                   \
    }                                                                   \
    *end = entry;                                                       \
    if (wb)                                                             \
    {                                                                   \
        rv = syncdbTableDelete(&client_id, table_name);                 \
        BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH,                             \
                            SAI_LOG_LEVEL_CRITICAL,                     \
                            "Deleting table", rv);                      \
    }                                                                   \
}

#define _BRCM_SAI_DM_LIST_ALLOC_END_STR(name, tschema, version, dtype,  \
                                        data, key_size, has_prev,       \
                                        prev_field, has_parent, pdata,  \
                                        src, dest)                      \
{                                                                       \
    int _l, pend;                                                       \
    sai_status_t rv;                                                    \
    dtype *entry, *prev;                                                \
                                                                        \
    if (wb)                                                             \
    {                                                                   \
        if (str)                                                        \
        {                                                               \
            /* Create DB Table with given str id and name */            \
            sprintf(table_name, "%s%s", name, str);                     \
        }                                                               \
        else                                                            \
        {                                                               \
            sprintf(table_name, "%s", name);                            \
        }                                                               \
        rv = syncdbUtilSchemaCreate(tschema, sizeof(tschema), schema,   \
                                    sizeof(schema), &schema_size,       \
                                    sizeof(dtype), &schema_error);      \
        BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,     \
                            "Creating table schema", rv);               \
        rv = syncdbAvlTableCreate(&client_id, table_name, version,      \
                                  entries, entries, sizeof(dtype),      \
                                  key_size, flags, schema,              \
                                  schema_size);                         \
        BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,     \
                            "Creating table", rv);                      \
    }                                                                   \
    for (_l=0; _l<entries; _l++)                                        \
    {                                                                   \
        entry = (dtype*)ALLOC_CLEAR(1, sizeof(dtype));                  \
        if (IS_NULL(entry))                                             \
        {                                                               \
            BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_ERROR,                      \
                                "%s: Error allocating "                 \
                                "%d memory list state.\n",              \
                                __FUNCTION__, type);                    \
            return SAI_STATUS_NO_MEMORY;                                \
        }                                                               \
        __brcm_sai_dm_total_alloc += sizeof(dtype);                     \
        if (!_l)                                                        \
        {                                                               \
            *base = entry;                                              \
        }                                                               \
        else                                                            \
        {                                                               \
            prev->next = entry;                                         \
        }                                                               \
        if (wb)                                                         \
        {                                                               \
            /* Read and store entry from DB table id */                 \
            rv = syncdbGetNext(&client_id, table_name, &data,           \
                               sizeof(dtype), &pend);                   \
            BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL, \
                                "Get next", rv);                        \
            rv = _brcm_sai_list_entry_set(type, entry, &data);          \
            entry->next = NULL;                                         \
            BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL, \
                                "list entry set", rv);                  \
            if (TRUE == has_prev)                                       \
            {                                                           \
                entry->prev_field = prev;                               \
            }                                                           \
            if (TRUE == has_parent)                                     \
            {                                                           \
                sal_memcpy(&pdata.dest, &entry->src,                    \
                           sizeof(pdata.dest));                         \
                rv = _brcm_sai_db_table_entry_restore(parent_table,     \
                                                      &parent_data,     \
                                                      entry);           \
                BRCM_SAI_RV_CHK(SAI_API_SWITCH, "route DB table entry " \
                                "restore", rv);                         \
            }                                                           \
        }                                                               \
        prev = entry;                                                   \
    }                                                                   \
    *end = entry;                                                       \
    if (entry && entry->next) {                                         \
        BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG,                          \
                          "clear entry next %p->%p\n",                  \
                          entry, entry->next);                          \
        entry->next = NULL;                                             \
    }                                                                   \
    if (wb)                                                             \
    {                                                                   \
        rv = syncdbTableDelete(&client_id, table_name);                 \
        BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH,                             \
                            SAI_LOG_LEVEL_CRITICAL,                     \
                            "Deleting table", rv);                      \
    }                                                                   \
}

#define _BRCM_SAI_DM_LIST_DEL(dtype, field, key_name,                  \
                              has_ref_count, ref_count_field, base)    \
{                                                                      \
    dtype *prev, *entry = (dtype *)base;                               \
                                                                       \
    if (IS_NULL(entry))                                                \
    {                                                                  \
        BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n",        \
                            __FUNCTION__);                             \
        return SAI_STATUS_ITEM_NOT_FOUND;                              \
    }                                                                  \
    do                                                                 \
    {                                                                  \
        if (entry->field == key->key_name)                             \
        {                                                              \
            /* Node found - check ref count and delete */              \
            if (has_ref_count && entry->ref_count_field)               \
            {                                                          \
                BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG,                 \
                                    "%s: %s still in use.\n",          \
                                    __FUNCTION__,                      \
                            (type == _BRCM_SAI_LIST_POLICER_ACTION) ?  \
                             "Policer" : "");                          \
                return SAI_STATUS_OBJECT_IN_USE;                       \
            }                                                          \
            if (entry == base)                                         \
            {                                                          \
                /* first node */                                       \
                base = entry->next;                                    \
            }                                                          \
            else                                                       \
            {                                                          \
                prev->next = entry->next;                              \
            }                                                          \
            __brcm_sai_dm_total_alloc -= CHECK_FREE_SIZE(entry);       \
            entry = NULL;                                              \
            BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n",    \
                                __FUNCTION__);                         \
            return SAI_STATUS_SUCCESS;                                 \
        }                                                              \
        prev = entry;                                                  \
        entry = entry->next;                                           \
    } while (!IS_NULL(entry));                                         \
    return SAI_STATUS_ITEM_NOT_FOUND;                                  \
}

#define _BRCM_SAI_DM_LIST_DEL_STRUCT(dtype, field, key_name, base)      \
{                                                                       \
    dtype *prev, *entry = (dtype *)base;                                \
                                                                        \
    if (IS_NULL(entry))                                                 \
    {                                                                   \
        BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n",         \
                            __FUNCTION__);                              \
        return SAI_STATUS_ITEM_NOT_FOUND;                               \
    }                                                                   \
    do                                                                  \
    {                                                                   \
        if (MATCH == memcmp(&entry->field, &key->key_name,              \
                            sizeof(entry->field)))                      \
        {                                                               \
            if (entry == base)                                          \
            {                                                           \
                /* first node */                                        \
                base = entry->next;                                     \
            }                                                           \
            else                                                        \
            {                                                           \
                prev->next = entry->next;                               \
            }                                                           \
            __brcm_sai_dm_total_alloc -= CHECK_FREE_SIZE(entry);        \
            entry = NULL;                                               \
            BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n",     \
                                __FUNCTION__);                          \
            return SAI_STATUS_SUCCESS;                                  \
        }                                                               \
        prev = entry;                                                   \
        entry = entry->next;                                            \
    } while (!IS_NULL(entry));                                          \
    return SAI_STATUS_ITEM_NOT_FOUND;                                   \
}

#define _BRCM_SAI_DM_LIST_TRAVERSE(dtype, field, gdata)           \
{                                                                 \
    dtype *entry = base->field ? base->field : gdata;             \
                                                                  \
    if (IS_NULL(entry)) /* none */                                \
    {                                                             \
        BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n",   \
                            __FUNCTION__);                        \
        return SAI_STATUS_ITEM_NOT_FOUND;                         \
    }                                                             \
    if (IS_NULL(base->field)) /* first */                         \
    {                                                             \
        data->field = entry;                                      \
        BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n",   \
                            __FUNCTION__);                        \
        return SAI_STATUS_SUCCESS;                                \
    }                                                             \
    if (IS_NULL(entry->next)) /* last */                          \
    {                                                             \
        BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n",   \
                            __FUNCTION__);                        \
        return SAI_STATUS_ITEM_NOT_FOUND;                         \
    }                                                             \
    data->field = entry->next;                                    \
    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n",       \
                        __FUNCTION__);                            \
    return SAI_STATUS_SUCCESS;                                    \
}

#define _BRCM_SAI_DM_LIST_GET(dtype, field, key, key_name, \
                              list_data, gdata)            \
{                                                          \
    bool found = FALSE;                                    \
    dtype *entry = gdata;                                  \
                                                           \
    if (!IS_NULL(entry))                                   \
    {                                                      \
        do                                                 \
        {                                                  \
            if (entry->field == key->key_name)             \
            {                                              \
                data->list_data = entry;                   \
                found = TRUE;                              \
                break;                                     \
            }                                              \
            entry = entry->next;                           \
        } while (!IS_NULL(entry));                         \
        if (!found)                                        \
        {                                                  \
            return SAI_STATUS_ITEM_NOT_FOUND;              \
        }                                                  \
    }                                                      \
    else                                                   \
    {                                                      \
        return SAI_STATUS_ITEM_NOT_FOUND;                  \
    }                                                      \
}

#define _BRCM_SAI_DM_LIST_GET_STRUCT(dtype, field, key, key_name, \
                                     key_size, list_data, gdata)  \
{                                                                 \
    bool found = FALSE;                                           \
    dtype *entry = gdata;                                         \
                                                                  \
    if (!IS_NULL(entry))                                          \
    {                                                             \
        do                                                        \
        {                                                         \
            if (MATCH == memcmp(&entry->field, &key->key_name,    \
                                key_size))                        \
            {                                                     \
                data->list_data = entry;                          \
                found = TRUE;                                     \
                break;                                            \
            }                                                     \
            entry = entry->next;                                  \
        } while (!IS_NULL(entry));                                \
        if (!found)                                               \
        {                                                         \
            return SAI_STATUS_ITEM_NOT_FOUND;                     \
        }                                                         \
    }                                                             \
    else                                                          \
    {                                                             \
        return SAI_STATUS_ITEM_NOT_FOUND;                         \
    }                                                             \
}

#define _BRCM_SAI_DB_TABLE_NODE_LIST_FREE(id_field, ref_count_field, \
                                          list_field)                \
{                                                                    \
    do                                                               \
    {                                                                \
        rv = syncdbGetNext(&client_id, table_name, &data,            \
                           tsize, &pend);                            \
        if (SYNCDB_OK == rv)                                         \
        {                                                            \
            if (data.ref_count_field)                                \
            {                                                        \
                rv = _brcm_sai_list_free(list, data.id_field,        \
                                         data.ref_count_field,       \
                                         data.list_field);           \
                BRCM_SAI_RV_CHK(SAI_API_SCHEDULER,                   \
                                "table node list free", rv);         \
                BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG,               \
                                    "Freed table node list: "        \
                                    "%s%d with %d elements\n",       \
                                    list_name, data.id_field,        \
                                    data.ref_count_field);           \
                count++;                                             \
            }                                                        \
            else                                                     \
            {                                                        \
                zcount++;                                            \
            }                                                        \
        }                                                            \
    } while(SYNCDB_OK == rv);                                        \
}

#define _BRCM_SAI_DB_TABLE_NODE_LIST_FREE_STR(str, ref_count_field,  \
                                              list_field)            \
{                                                                    \
    do                                                               \
    {                                                                \
        rv = syncdbGetNext(&client_id, table_name, &data,            \
                           tsize, &pend);                            \
        if (SYNCDB_OK == rv)                                         \
        {                                                            \
            if (data.ref_count_field)                                \
            {                                                        \
                rv = _brcm_sai_list_free_v2(list, str,               \
                                            data.ref_count_field,    \
                                            data.list_field, &data); \
                BRCM_SAI_RV_CHK(SAI_API_SCHEDULER,                   \
                                "table node list free", rv);         \
                BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG,               \
                                    "Freed table node list: "        \
                                    "%s%s with %d elements\n",       \
                                    list_name, str,                  \
                                    data.ref_count_field);           \
                count++;                                             \
            }                                                        \
            else                                                     \
            {                                                        \
                zcount++;                                            \
            }                                                        \
        }                                                            \
    } while(SYNCDB_OK == rv);                                        \
}

char mac_string[12];
char* print_mac(sai_mac_t mac)
{
    sal_memset(mac_string, '\0', sizeof(mac_string));
    sprintf(mac_string, "%x:%x:%x:%x:%x:%x",
             mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    return mac_string;
}

sai_status_t
_brcm_sai_mac_nbr_tbl_walk()
{
    char *table_name;
    int pend, tsize;
    sai_status_t rv;
    _brcm_sai_mac_nbr_info_table_t data;
    _brcm_sai_nbr_info_t *current;

    table_name = _mac_nbr_info_table_name;
    tsize = sizeof(_brcm_sai_mac_nbr_info_table_t);
    DATA_CLEAR(data, _brcm_sai_mac_nbr_info_table_t);
    do
    {
        rv = syncdbGetNext(&client_id, table_name, &data,
                           tsize, &pend);
        if (SYNCDB_OK == rv)
        {

            printf("Table node vid %d mac %s with list elems %d \n",
                   data.mac_vid.vid,
                   print_mac(data.mac_vid.mac), data.nbrs_count);
            if (data.nbrs_count)
            {
                current = data.nbrs;
                while (NULL != current) {
                    printf("List elem nbr %d, pa %d, l3a %d, \n",
                           current->nbr.ip4,
                           current->pa, current->l3a_intf);
                    current = current->next;
                }
          }
        }
    } while(SYNCDB_OK == rv);
    return rv;
}

STATIC sai_status_t
_brcm_sai_global_data_init()
{
    sai_status_t rv;
    unsigned int i, schema_size, schema_error;
    char schema[SYNCDB_JSON_MAX_SCHEMA_SIZE];
    unsigned int flags = SYNCDB_TABLE_FLAG_STORABLE | SYNCDB_TABLE_FLAG_NVRAM;

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Enter %s\n", __FUNCTION__);

    /* Create DB table */
    rv = syncdbUtilSchemaCreate(_global_data_record_schema,
                                sizeof(_global_data_record_schema),
                                schema, sizeof(schema), &schema_size,
                                sizeof(_brcm_sai_global_data_t),
                                &schema_error);
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "Creating global data schema", rv);
    if (_brcm_sai_switch_wb_state_get())
    {
        flags |= SYNCDB_TABLE_FLAG_FILE_LOAD;
    }
    rv = syncdbRecordTableCreate(&client_id, _global_data_record_name,
                                 _global_data_record_version,
                                 sizeof(_brcm_sai_global_data_t),
                                 flags, schema, schema_size);
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "Creating global data record", rv);
    if (_brcm_sai_switch_wb_state_get())
    {
        /* Load global data from DB */
        rv = syncdbGet(&client_id, _global_data_record_name,
                       &_brcm_sai_global_data, sizeof(_brcm_sai_global_data_t), 0);
        BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                            "global data DB record get", rv);
    }
    else
    {
        DATA_CLEAR(_brcm_sai_global_data, _brcm_sai_global_data_t);
        for (i=0; i<8; i++)
        {
            _brcm_sai_global_data.pfc_dld_timers[i] =
            _brcm_sai_global_data.pfc_dlr_timers[i] = 100;
        }
        _brcm_sai_global_data.bridge_ports = _brcm_sai_switch_fp_port_count();
    }

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n", __FUNCTION__);
    return SAI_STATUS_SUCCESS;
}

STATIC sai_status_t
_brcm_sai_global_data_save(void)
{
    sai_status_t rv;

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Enter %s\n", __FUNCTION__);
    /* Store global data to DB */
    rv = syncdbSet(&client_id, _global_data_record_name,
                   &_brcm_sai_global_data, sizeof(_brcm_sai_global_data));
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "global data DB record set", rv);

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n", __FUNCTION__);
    return SAI_STATUS_SUCCESS;
}

STATIC sai_status_t
_brcm_sai_vlan_save(void)
{
    sai_status_t rv;

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Enter %s\n", __FUNCTION__);
    /* Store vlan data to DB */
    rv = syncdbSet(&client_id, _vlan_bmp_record_name, _brcm_sai_vlan_bmp,
                   BRCM_SAI_NUM_BYTES(_BRCM_SAI_VR_MAX_VID));
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "global data DB record set", rv);
    __brcm_sai_dm_total_alloc -= CHECK_FREE_CLEAR_SIZE(_brcm_sai_vlan_bmp);
    _brcm_sai_vlan_bmp = NULL;

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n", __FUNCTION__);
    return SAI_STATUS_SUCCESS;
}

void _brcm_sai_dm_free(void *ptr)
{
    __brcm_sai_dm_total_alloc -= CHECK_FREE_SIZE(ptr);
    ptr = NULL;
}

/* Global data get routine */
sai_status_t
_brcm_sai_global_data_get(_brcm_sai_global_data_type_t type,
                          _brcm_sai_data_t *data)
{
    switch (type)
    {
        case _BRCM_SAI_VER:
            data->u64 = _brcm_sai_global_data.sai_ver;
            break;
        case _BRCM_SAI_SWITCH_INITED:
            data->bool_data = _brcm_sai_global_data.switch_inited;
            break;
        case _BRCM_SAI_SYSTEM_MAC:
            sal_memcpy(data->mac, _brcm_sai_global_data.system_mac,
                       sizeof(sai_mac_t));
            break;
        case _BRCM_SAI_VR_COUNT:
            data->u32 = _brcm_sai_global_data.vr_count;
            break;
        case _BRCM_SAI_TUNNEL_RIF_COUNT:
            data->u32 = _brcm_sai_global_data.tunnel_rif_count;
            break;
        case _BRCM_SAI_WRED_COUNT:
            data->u32 = _brcm_sai_global_data.wred_count;
            break;
        case _BRCM_SAI_SAI_SCHEDULER_COUNT:
            data->u32 = _brcm_sai_global_data.sai_scheduler_count;
            break;
        case _BRCM_SAI_PFC_QUEUE_MAP_COUNT:
            data->u32 = _brcm_sai_global_data.pfc_queue_map_count;
            break;
        case _BRCM_SAI_MMU_BUFF_PROFILE_COUNT:
            data->u32 = _brcm_sai_global_data.mmu_buff_profile_count;
            break;
        case _BRCM_SAI_DOT1P_TC_MAP_COUNT:
            data->u32 = _brcm_sai_global_data.dot1p_tc_map_count;
            break;
        case _BRCM_SAI_DOT1P_COLOR_MAP_COUNT:
            data->u32 = _brcm_sai_global_data.dot1p_color_map_count;
            break;
        case _BRCM_SAI_DSCP_TC_MAP_COUNT:
            data->u32 = _brcm_sai_global_data.dscp_tc_map_count;
            break;
        case _BRCM_SAI_DSCP_COLOR_MAP_COUNT:
            data->u32 = _brcm_sai_global_data.dscp_color_map_count;
            break;
        case _BRCM_SAI_TC_DSCP_MAP_COUNT:
            data->u32 = _brcm_sai_global_data.tc_dscp_map_count;
            break;
        case _BRCM_SAI_TC_DOT1P_MAP_COUNT:
            data->u32 = _brcm_sai_global_data.tc_dot1p_map_count;
            break;
        case _BRCM_SAI_TC_QUEUE_MAP_COUNT:
            data->u32 = _brcm_sai_global_data.tc_queue_map_count;
            break;
        case _BRCM_SAI_TC_PG_MAP_COUNT:
            data->u32 = _brcm_sai_global_data.tc_pg_map_count;
            break;
        case _BRCM_SAI_POLICER_COUNT:
            data->u32 = _brcm_sai_global_data.policer_count;
            break;
        case _BRCM_SAI_HOST_INTF_COUNT:
            data->u8 = _brcm_sai_global_data.host_if_count;
            break;
        case _BRCM_SAI_DROP_INTF_ID:
            data->if_id = _brcm_sai_global_data.drop_if_id;
            break;
        case _BRCM_SAI_TRAP_INTF_ID:
            data->if_id = _brcm_sai_global_data.trap_if_id;
            break;
        case _BRCM_SAI_ROUTE_NH_COUNT:
            data->if_id = _brcm_sai_global_data.route_nh_count;
            break;
        case _BRCM_SAI_ECMP_NH_COUNT:
            data->if_id = _brcm_sai_global_data.ecmp_nh_count;
            break;
        case _BRCM_SAI_UDFG_COUNT:
            data->u32 = _brcm_sai_global_data.udfg_count;
            break;
        case _BRCM_SAI_HASH_COUNT:
            data->u32 = _brcm_sai_global_data.hash_count;
            break;
        case _BRCM_SAI_UDF_COUNT:
            data->u32 = _brcm_sai_global_data.udf_count;
            break;
        case _BRCM_SAI_UDF_SHARED_0:
            data->s32 = _brcm_sai_global_data.udf_shared_0;
            break;
        case _BRCM_SAI_UDF_SHARED_1:
            data->s32 = _brcm_sai_global_data.udf_shared_1;
            break;
        case _BRCM_SAI_TRAP_FP_GROUP:
            data->u32 = _brcm_sai_global_data.fp_group;
            break;
        case _BRCM_SAI_TRAP_GROUPS_COUNT:
            data->s32 = _brcm_sai_global_data.trap_groups;
            break;
        case _BRCM_SAI_TRAPS_COUNT:
            data->s32 = _brcm_sai_global_data.traps;
            break;
        case _BRCM_SAI_SYSTEM_INTF:
            data->if_id = _brcm_sai_global_data.system_if_id;
            break;
        case _BRCM_SAI_SYSTEM_MAC_SET:
            data->bool_data = _brcm_sai_global_data.system_mac_set;
            break;
        case _BRCM_SAI_ACL_TABLES_COUNT:
            data->u32 = _brcm_sai_global_data.acl_tables_count;
            break;
        case _BRCM_SAI_HOST_INTF_ENTRY_COUNT:
            data->s32 = _brcm_sai_global_data.host_if_entry_count;
            break;
        case _BRCM_SAI_ACL_TBL_GRPS_COUNT:
            data->u32 = _brcm_sai_global_data.acl_tbl_grps_count;
            break;
        case _BRCM_SAI_ACL_TBL_GRPS_MEMBRS_COUNT:
            data->u32 = _brcm_sai_global_data.acl_tbl_grp_membrs_count;
            break;
        case _BRCM_SAI_PFC_DLD_TIMERS:
            sal_memcpy(data->cos_timers, _brcm_sai_global_data.pfc_dld_timers,
                       8 * sizeof(sai_uint32_t));
            break;
        case _BRCM_SAI_PFC_DLR_TIMERS:
            sal_memcpy(data->cos_timers, _brcm_sai_global_data.pfc_dlr_timers,
                       8 * sizeof(sai_uint32_t));
            break;
        case _BRCM_SAI_BRIDGE_PORTS:
            data->u32 = _brcm_sai_global_data.bridge_ports;
            break;
        case _BRCM_SAI_BRIDGE_LAG_PORTS:
            data->u32 = _brcm_sai_global_data.bridge_lag_ports;
            break;
        case _BRCM_SAI_WARM_SHUT:
            data->bool_data = _brcm_sai_global_data.ws;
            break;
        case _BRCM_SAI_SW_BCAST_RULE_INSTALLED:
            data->bool_data = _brcm_sai_global_data.bcast_rule_installed;
            break;
        case _BRCM_SAI_SW_BCAST_ENTRY:
            data->s32 = _brcm_sai_global_data.bcast_entry;
            break;
        case _BRCM_SAI_ARP_TRAP_REQ:
            data->u32 = _brcm_sai_global_data.arp_trap_req;
            break;
        case _BRCM_SAI_ARP_TRAP_RESP:
            data->u32 = _brcm_sai_global_data.arp_trap_resp;
            break;
        case _BRCM_SAI_NBR_COUNT:
            data->u32 = _brcm_sai_global_data.nbr_count;
            break;
        case _BRCM_SAI_BCAST_IP_COUNT:
            data->u32 = _brcm_sai_global_data.bcast_ips;
            break;
        case _BRCM_SAI_FDB_COUNT:
            data->u32 = _brcm_sai_global_data.fdb_count;
            break;
        case _BRCM_SAI_EGRESS_INUSE_COUNT:
            data->u32 = _brcm_sai_global_data.egress_inuse_count;
            break;
        case _BRCM_SAI_UDF_HASH_USED:
            data->u32 = _brcm_sai_global_data.udf_hash_used;
            break;
        case _BRCM_SAI_ING_FLEX_CTR_MODE_ID:
            data->u32 = _brcm_sai_global_data.ing_flex_mode_id;
            break;
        case _BRCM_SAI_EGR_FLEX_CTR_MODE_ID:
            data->u32 = _brcm_sai_global_data.egr_flex_mode_id;
            break;
        case _BRCM_SAI_TRAP_RANGE_ID:
            data->u32 = _brcm_sai_global_data.range_id;
            break;
        case _BRCM_SAI_VXLAN_UDP_PORT:
            data->u16 = _brcm_sai_global_data.vxlan_udp_port;
            break;
        default:
            return SAI_STATUS_INVALID_PARAMETER;
    }
    return SAI_STATUS_SUCCESS;
}

/* Global data set routine */
sai_status_t
_brcm_sai_global_data_set(_brcm_sai_global_data_type_t type,
                          _brcm_sai_data_t *data)
{
    switch (type)
    {
        case _BRCM_SAI_VER:
            _brcm_sai_global_data.sai_ver = data->u64;
            break;
        case _BRCM_SAI_SWITCH_INITED:
            _brcm_sai_global_data.switch_inited = data->bool_data;
            break;
        case _BRCM_SAI_SYSTEM_MAC:
            sal_memcpy(_brcm_sai_global_data.system_mac, data->mac, sizeof(sai_mac_t));
            break;
        case _BRCM_SAI_VR_COUNT:
            _brcm_sai_global_data.vr_count = data->u32;
            break;
        case _BRCM_SAI_TUNNEL_RIF_COUNT:
            _brcm_sai_global_data.tunnel_rif_count = data->u32;
            break;
        case _BRCM_SAI_WRED_COUNT:
            _brcm_sai_global_data.wred_count = data->u32;
            break;
        case _BRCM_SAI_SAI_SCHEDULER_COUNT:
            _brcm_sai_global_data.sai_scheduler_count = data->u32;
            break;
        case _BRCM_SAI_PFC_QUEUE_MAP_COUNT:
            _brcm_sai_global_data.pfc_queue_map_count = data->u32;
            break;
        case _BRCM_SAI_MMU_BUFF_PROFILE_COUNT:
            _brcm_sai_global_data.mmu_buff_profile_count = data->u32;
            break;
        case _BRCM_SAI_DOT1P_TC_MAP_COUNT:
            _brcm_sai_global_data.dot1p_tc_map_count = data->u32;
            break;
        case _BRCM_SAI_DOT1P_COLOR_MAP_COUNT:
            _brcm_sai_global_data.dot1p_color_map_count = data->u32;
            break;
        case _BRCM_SAI_DSCP_TC_MAP_COUNT:
            _brcm_sai_global_data.dscp_tc_map_count = data->u32;
            break;
        case _BRCM_SAI_DSCP_COLOR_MAP_COUNT:
            _brcm_sai_global_data.dscp_color_map_count = data->u32;
            break;
        case _BRCM_SAI_TC_DSCP_MAP_COUNT:
            _brcm_sai_global_data.tc_dscp_map_count = data->u32;
            break;
        case _BRCM_SAI_TC_DOT1P_MAP_COUNT:
            _brcm_sai_global_data.tc_dot1p_map_count = data->u32;
            break;
        case _BRCM_SAI_TC_QUEUE_MAP_COUNT:
            _brcm_sai_global_data.tc_queue_map_count = data->u32;
            break;
        case _BRCM_SAI_TC_PG_MAP_COUNT:
            _brcm_sai_global_data.tc_pg_map_count = data->u32;
            break;
        case _BRCM_SAI_POLICER_COUNT:
            _brcm_sai_global_data.policer_count = data->u32;
            break;
        case _BRCM_SAI_HOST_INTF_COUNT:
            _brcm_sai_global_data.host_if_count = data->u8;
            break;
        case _BRCM_SAI_DROP_INTF_ID:
            _brcm_sai_global_data.drop_if_id = data->if_id;
            break;
        case _BRCM_SAI_TRAP_INTF_ID:
            _brcm_sai_global_data.trap_if_id = data->if_id;
            break;
        case _BRCM_SAI_UDFG_COUNT:
            _brcm_sai_global_data.udfg_count = data->u32;
            break;
        case _BRCM_SAI_HASH_COUNT:
            _brcm_sai_global_data.hash_count = data->u32;
            break;
        case _BRCM_SAI_UDF_COUNT:
            _brcm_sai_global_data.udf_count = data->u32;
            break;
        case _BRCM_SAI_UDF_SHARED_0:
            _brcm_sai_global_data.udf_shared_0 = data->s32;
            break;
        case _BRCM_SAI_UDF_SHARED_1:
            _brcm_sai_global_data.udf_shared_1 = data->s32;
            break;
        case _BRCM_SAI_TRAP_FP_GROUP:
            _brcm_sai_global_data.fp_group = data->u32;
            break;
        case _BRCM_SAI_TRAP_GROUPS_COUNT:
            _brcm_sai_global_data.trap_groups = data->s32;
            break;
        case _BRCM_SAI_TRAPS_COUNT:
            _brcm_sai_global_data.traps = data->s32;
            break;
        case _BRCM_SAI_SYSTEM_INTF:
            _brcm_sai_global_data.system_if_id = data->if_id;
            break;
        case _BRCM_SAI_SYSTEM_MAC_SET:
            _brcm_sai_global_data.system_mac_set = data->bool_data;
            break;
        case _BRCM_SAI_ACL_TABLES_COUNT:
            _brcm_sai_global_data.acl_tables_count = data->u32;
            break;
        case _BRCM_SAI_HOST_INTF_ENTRY_COUNT:
            _brcm_sai_global_data.host_if_entry_count = data->s32;
            break;
        case _BRCM_SAI_ACL_TBL_GRPS_COUNT:
            _brcm_sai_global_data.acl_tbl_grps_count = data->u32;
            break;
        case _BRCM_SAI_ACL_TBL_GRPS_MEMBRS_COUNT:
            _brcm_sai_global_data.acl_tbl_grp_membrs_count = data->u32;
            break;
        case _BRCM_SAI_PFC_DLD_TIMERS:
            sal_memcpy(_brcm_sai_global_data.pfc_dld_timers, data->cos_timers,
                       8 * sizeof(sai_uint32_t));
            break;
        case _BRCM_SAI_PFC_DLR_TIMERS:
            sal_memcpy(_brcm_sai_global_data.pfc_dlr_timers, data->cos_timers,
                       8 * sizeof(sai_uint32_t));
            break;
        case _BRCM_SAI_BRIDGE_PORTS:
            _brcm_sai_global_data.bridge_ports = data->u32;
            break;
        case _BRCM_SAI_BRIDGE_LAG_PORTS:
            _brcm_sai_global_data.bridge_lag_ports = data->u32;
            break;
        case _BRCM_SAI_WARM_SHUT:
            _brcm_sai_global_data.ws = data->bool_data;
            break;
        case _BRCM_SAI_SW_BCAST_RULE_INSTALLED:
            _brcm_sai_global_data.bcast_rule_installed = data->bool_data;
            break;
        case _BRCM_SAI_SW_BCAST_ENTRY:
            _brcm_sai_global_data.bcast_entry = data->s32;
            break;
        case _BRCM_SAI_ARP_TRAP_REQ:
            _brcm_sai_global_data.arp_trap_req = data->u32;
            break;
        case _BRCM_SAI_ARP_TRAP_RESP:
            _brcm_sai_global_data.arp_trap_resp = data->u32;
            break;
        case _BRCM_SAI_NBR_COUNT:
            _brcm_sai_global_data.nbr_count = data->u32;
            break;
        case _BRCM_SAI_BCAST_IP_COUNT:
            _brcm_sai_global_data.bcast_ips = data->u32;
            break;
        case _BRCM_SAI_FDB_COUNT:
            _brcm_sai_global_data.fdb_count = data->u32;
            break;
        case _BRCM_SAI_ING_FLEX_CTR_MODE_ID:
            _brcm_sai_global_data.ing_flex_mode_id = data->u32;
            break;
        case _BRCM_SAI_EGR_FLEX_CTR_MODE_ID:
            _brcm_sai_global_data.egr_flex_mode_id = data->u32;
            break;
        case _BRCM_SAI_TRAP_RANGE_ID:
            _brcm_sai_global_data.range_id = data->u32;
            break;
        case _BRCM_SAI_VXLAN_UDP_PORT:
            _brcm_sai_global_data.vxlan_udp_port = data->u16;
            break;
        default:
            return SAI_STATUS_INVALID_PARAMETER;
    }
    return SAI_STATUS_SUCCESS;
}

/* Global data increment/decrement routine */
sai_status_t
_brcm_sai_global_data_bump(_brcm_sai_global_data_type_t type,
                           _brcm_sai_data_bump_t inc_dec)
{
    switch (type)
    {
        case _BRCM_SAI_VR_COUNT:
            (INC == inc_dec) ? _brcm_sai_global_data.vr_count++ :
                               _brcm_sai_global_data.vr_count--;
            break;
        case _BRCM_SAI_TUNNEL_RIF_COUNT:
            (INC == inc_dec) ? _brcm_sai_global_data.tunnel_rif_count++ :
                               _brcm_sai_global_data.tunnel_rif_count--;
            break;
        case _BRCM_SAI_WRED_COUNT:
            (INC == inc_dec) ? _brcm_sai_global_data.wred_count++ :
                               _brcm_sai_global_data.wred_count--;
            break;
        case _BRCM_SAI_SAI_SCHEDULER_COUNT:
            (INC == inc_dec) ? _brcm_sai_global_data.sai_scheduler_count++ :
                               _brcm_sai_global_data.sai_scheduler_count--;
            break;
        case _BRCM_SAI_PFC_QUEUE_MAP_COUNT:
            (INC == inc_dec) ? _brcm_sai_global_data.pfc_queue_map_count++ :
                               _brcm_sai_global_data.pfc_queue_map_count--;
            break;
        case _BRCM_SAI_MMU_BUFF_PROFILE_COUNT:
            (INC == inc_dec) ? _brcm_sai_global_data.mmu_buff_profile_count++ :
                               _brcm_sai_global_data.mmu_buff_profile_count--;
            break;
        case _BRCM_SAI_DOT1P_TC_MAP_COUNT:
            (INC == inc_dec) ? _brcm_sai_global_data.dot1p_tc_map_count++ :
                               _brcm_sai_global_data.dot1p_tc_map_count--;
            break;
        case _BRCM_SAI_DOT1P_COLOR_MAP_COUNT:
            (INC == inc_dec) ? _brcm_sai_global_data.dot1p_color_map_count++ :
                               _brcm_sai_global_data.dot1p_color_map_count--;
            break;
        case _BRCM_SAI_DSCP_TC_MAP_COUNT:
            (INC == inc_dec) ? _brcm_sai_global_data.dscp_tc_map_count++ :
                               _brcm_sai_global_data.dscp_tc_map_count--;
            break;
        case _BRCM_SAI_DSCP_COLOR_MAP_COUNT:
            (INC == inc_dec) ? _brcm_sai_global_data.dscp_color_map_count++ :
                               _brcm_sai_global_data.dscp_color_map_count--;
            break;
        case _BRCM_SAI_TC_DSCP_MAP_COUNT:
            (INC == inc_dec) ? _brcm_sai_global_data.tc_dscp_map_count++ :
                               _brcm_sai_global_data.tc_dscp_map_count--;
            break;
        case _BRCM_SAI_TC_DOT1P_MAP_COUNT:
            (INC == inc_dec) ? _brcm_sai_global_data.tc_dot1p_map_count++ :
                               _brcm_sai_global_data.tc_dot1p_map_count--;
            break;
        case _BRCM_SAI_TC_QUEUE_MAP_COUNT:
            (INC == inc_dec) ? _brcm_sai_global_data.tc_queue_map_count++ :
                               _brcm_sai_global_data.tc_queue_map_count--;
            break;
        case _BRCM_SAI_TC_PG_MAP_COUNT:
            (INC == inc_dec) ? _brcm_sai_global_data.tc_pg_map_count++ :
                               _brcm_sai_global_data.tc_pg_map_count--;
            break;
        case _BRCM_SAI_POLICER_COUNT:
            (INC == inc_dec) ? _brcm_sai_global_data.policer_count++ :
                               _brcm_sai_global_data.policer_count--;
            break;
        case _BRCM_SAI_HOST_INTF_COUNT:
            (INC == inc_dec) ? _brcm_sai_global_data.host_if_count++ :
                               _brcm_sai_global_data.host_if_count--;
            break;
        case _BRCM_SAI_ROUTE_NH_COUNT:
            (INC == inc_dec) ? _brcm_sai_global_data.route_nh_count++ :
                               _brcm_sai_global_data.route_nh_count--;
            break;
        case _BRCM_SAI_ECMP_NH_COUNT:
            (INC == inc_dec) ? _brcm_sai_global_data.ecmp_nh_count++ :
                               _brcm_sai_global_data.ecmp_nh_count--;
            break;
        case _BRCM_SAI_UDFG_COUNT:
            (INC == inc_dec) ? _brcm_sai_global_data.udfg_count++ :
                               _brcm_sai_global_data.udfg_count--;
            break;
        case _BRCM_SAI_HASH_COUNT:
            (INC == inc_dec) ? _brcm_sai_global_data.hash_count++ :
                               _brcm_sai_global_data.hash_count--;
            break;
        case _BRCM_SAI_UDF_COUNT:
            (INC == inc_dec) ? _brcm_sai_global_data.udf_count++ :
                               _brcm_sai_global_data.udf_count--;
            break;
        case _BRCM_SAI_TRAP_GROUPS_COUNT:
            (INC == inc_dec) ? _brcm_sai_global_data.trap_groups++ :
                               _brcm_sai_global_data.trap_groups--;
            break;
        case _BRCM_SAI_TRAPS_COUNT:
            (INC == inc_dec) ? _brcm_sai_global_data.traps++ :
                               _brcm_sai_global_data.traps--;
            break;
        case _BRCM_SAI_ACL_TABLES_COUNT:
            (INC == inc_dec) ? _brcm_sai_global_data.acl_tables_count++ :
                               _brcm_sai_global_data.acl_tables_count--;
            break;
        case _BRCM_SAI_HOST_INTF_ENTRY_COUNT:
            (INC == inc_dec) ? _brcm_sai_global_data.host_if_entry_count++ :
                               _brcm_sai_global_data.host_if_entry_count--;
            break;
        case _BRCM_SAI_ACL_TBL_GRPS_COUNT:
            (INC == inc_dec) ? _brcm_sai_global_data.acl_tbl_grps_count++ :
                               _brcm_sai_global_data.acl_tbl_grps_count--;
            break;
        case _BRCM_SAI_ACL_TBL_GRPS_MEMBRS_COUNT:
            (INC == inc_dec) ? _brcm_sai_global_data.acl_tbl_grp_membrs_count++ :
                               _brcm_sai_global_data.acl_tbl_grp_membrs_count--;
            break;
        case _BRCM_SAI_BRIDGE_PORTS:
            (INC == inc_dec) ? _brcm_sai_global_data.bridge_ports++ :
                               _brcm_sai_global_data.bridge_ports--;
            break;
        case _BRCM_SAI_BRIDGE_LAG_PORTS:
            (INC == inc_dec) ? _brcm_sai_global_data.bridge_lag_ports++ :
                               _brcm_sai_global_data.bridge_lag_ports--;
            break;
        case _BRCM_SAI_NBR_COUNT:
            (INC == inc_dec) ? _brcm_sai_global_data.nbr_count++ :
                               _brcm_sai_global_data.nbr_count--;
            break;
        case _BRCM_SAI_BCAST_IP_COUNT:
            (INC == inc_dec) ? _brcm_sai_global_data.bcast_ips++ :
                               _brcm_sai_global_data.bcast_ips--;
            break;
        case _BRCM_SAI_FDB_COUNT:
            UINT32_OPER(_brcm_sai_global_data.fdb_count, inc_dec);
            break;
        case _BRCM_SAI_EGRESS_INUSE_COUNT:
            (INC == inc_dec) ? _brcm_sai_global_data.egress_inuse_count++ :
                               _brcm_sai_global_data.egress_inuse_count--;
            break;
        case _BRCM_SAI_UDF_HASH_USED:
            (INC == inc_dec) ? _brcm_sai_global_data.udf_hash_used++ :
                               _brcm_sai_global_data.udf_hash_used--;
            break;
        default:
            return SAI_STATUS_INVALID_PARAMETER;
    }
    return SAI_STATUS_SUCCESS;
}

/* 1D array type data init routine */
sai_status_t
_brcm_sai_indexed_data_init(_brcm_sai_indexed_data_type_t type, int entries)
{
    bool wb = _brcm_sai_switch_wb_state_get();

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Enter %s, type = %d\n",
                        __FUNCTION__, type);

    switch (type)
    {
        case _BRCM_SAI_INDEXED_VR_INFO:
            _BRCM_SAI_DM_IDX_INIT1(_brcm_sai_vr_info_t,
                                   _brcm_sai_vrf_map);
            break;
        case _BRCM_SAI_INDEXED_RIF_INFO:
            _BRCM_SAI_DM_IDX_INIT1(_brcm_sai_rif_info_t,
                                   _brcm_sai_rif_info);
            break;
        case _BRCM_SAI_INDEXED_RIF_TUNNEL_INFO:
            _BRCM_SAI_DM_IDX_INIT1(_brcm_sai_trif_info_t,
                                   _brcm_sai_rif_tunnel_info);
            break;
        case _BRCM_SAI_INDEXED_TUNNEL_INFO:
            _BRCM_SAI_DM_IDX_INIT1(_brcm_sai_tunnel_info_t,
                                   _brcm_sai_tunnel_info);
            break;
        case _BRCM_SAI_INDEXED_TUNNEL_TABLE:
            _BRCM_SAI_DM_IDX_INIT1(_brcm_sai_tunnel_table_t,
                                   _brcm_sai_tunnel_table);
            break;
        case _BRCM_SAI_INDEXED_WRED_PROF:
            _BRCM_SAI_DM_IDX_INIT1(_brcm_sai_qos_wred_t, _brcm_sai_wred);
            break;
        case _BRCM_SAI_INDEXED_PORT_INFO:
            _BRCM_SAI_DM_IDX_INIT1(_brcm_sai_port_info_t,
                                   _brcm_sai_port_info);
            break;
        case _BRCM_SAI_INDEXED_VLAN_BMP:
            _BRCM_SAI_DM_IDX_INIT1(bitmap_t, _brcm_sai_vlan_bmp);
            break;
        case _BRCM_SAI_INDEXED_NETIF_PORT_MAP:
            _BRCM_SAI_DM_IDX_INIT1(_brcm_sai_netif_map_t,
                                   _brcm_sai_netif_map_port);
            break;
        case _BRCM_SAI_INDEXED_NETIF_VLAN_MAP:
            _BRCM_SAI_DM_IDX_INIT1(_brcm_sai_netif_map_t,
                                   _brcm_sai_netif_map_vlan);
            break;
        case _BRCM_SAI_INDEXED_PORT_SCHED:
            _BRCM_SAI_DM_IDX_INIT1(_sai_gport_1_t,
                                   _brcm_sai_port_sched);
            break;
        case _BRCM_SAI_INDEXED_CPU_QUEUE:
            _BRCM_SAI_DM_IDX_INIT1(_sai_gport_1_t,
                                   _brcm_sai_CPUMqueue);
            break;
        case _BRCM_SAI_INDEXED_DOT1P_TC_MAP:
            _BRCM_SAI_DM_IDX_INIT1(_brcm_sai_qos_ingress_map_t,
                                   _brcm_sai_dot1p_tc_map);
            break;
        case _BRCM_SAI_INDEXED_DOT1P_COLOR_MAP:
            _BRCM_SAI_DM_IDX_INIT1(_brcm_sai_qos_ingress_map_t,
                                   _brcm_sai_dot1p_color_map);
            break;
        case _BRCM_SAI_INDEXED_DSCP_TC_MAP:
            _BRCM_SAI_DM_IDX_INIT1(_brcm_sai_qos_ingress_map_t,
                                   _brcm_sai_dscp_tc_map);
            break;
        case _BRCM_SAI_INDEXED_DSCP_COLOR_MAP:
            _BRCM_SAI_DM_IDX_INIT1(_brcm_sai_qos_ingress_map_t,
                                   _brcm_sai_dscp_color_map);
            break;
        case _BRCM_SAI_INDEXED_TC_DSCP_MAP:
            _BRCM_SAI_DM_IDX_INIT1(_brcm_sai_qos_egress_map_t,
                                   _brcm_sai_tc_dscp_map);
            break;
        case _BRCM_SAI_INDEXED_TC_DOT1P_MAP:
            _BRCM_SAI_DM_IDX_INIT1(_brcm_sai_qos_egress_map_t,
                                   _brcm_sai_tc_dot1p_map);
            break;
        case _BRCM_SAI_INDEXED_TC_QUEUE_MAP:
            _BRCM_SAI_DM_IDX_INIT1(_brcm_sai_qos_egress_map_t,
                                   _brcm_sai_tc_queue_map);
            break;
        case _BRCM_SAI_INDEXED_TC_PG_MAP:
            _BRCM_SAI_DM_IDX_INIT1(_brcm_sai_qos_egress_map_t,
                                   _brcm_sai_tc_pg_map);
            break;
        case _BRCM_SAI_INDEXED_PFC_QUEUE_MAP:
            _BRCM_SAI_DM_IDX_INIT1(_brcm_sai_qos_egress_map_t,
                                   _brcm_sai_pfc_queue_map);
            break;
        case _BRCM_SAI_INDEXED_BUF_PROFILES:
            _BRCM_SAI_DM_IDX_INIT1(_brcm_sai_buf_profile_t,
                                   _brcm_sai_buf_profiles);
            break;
        case _BRCM_SAI_INDEXED_POOL_COUNT:
            _BRCM_SAI_DM_IDX_INIT1(_brcm_sai_buf_pool_count_t,
                                   _brcm_sai_pool_count);
            break;
        case _BRCM_SAI_INDEXED_SCHED_PROF:
            _BRCM_SAI_DM_IDX_INIT1(_brcm_sai_qos_scheduler_t,
                                   _brcm_sai_scheduler);
            break;
        case _BRCM_SAI_INDEXED_HOSTIF_INFO:
            _BRCM_SAI_DM_IDX_INIT1(_brcm_sai_hostif_t,
                                   _brcm_sai_hostif);
            break;
        case _BRCM_SAI_INDEXED_HOSTIF_TABLE:
            _BRCM_SAI_DM_IDX_INIT1(_brcm_sai_hostif_table_entry_t,
                                   _brcm_sai_hostif_table);
            break;
        case _BRCM_SAI_INDEXED_NH_INFO:
            _BRCM_SAI_DM_IDX_INIT1(_brcm_sai_nh_info_t,
                                   _brcm_sai_nh_info);
            break;
        case _BRCM_SAI_INDEXED_UDFG_INFO:
            _BRCM_SAI_DM_IDX_INIT1(_brcm_sai_udfg_info_t,
                                   _brcm_sai_udfg);
            break;
        case _BRCM_SAI_INDEXED_HASH_INFO:
            _BRCM_SAI_DM_IDX_INIT1(_brcm_sai_hash_info_t,
                                   _brcm_sai_hash);
            break;
        case _BRCM_SAI_INDEXED_TRAP_GROUP:
            _BRCM_SAI_DM_IDX_INIT1(_brcm_sai_trap_group_t,
                                   _brcm_sai_trap_groups);
            break;
        case _BRCM_SAI_INDEXED_TRAP:
            _BRCM_SAI_DM_IDX_INIT1(_brcm_sai_trap_t,
                                   _brcm_sai_traps);
            break;
        case _BRCM_SAI_INDEXED_ACL_TABLE:
            _BRCM_SAI_DM_IDX_INIT1(_brcm_sai_acl_table_t,
                                   _brcm_sai_acl_tables);
            break;
        case _BRCM_SAI_INDEXED_PORT_RIF_TABLE:
            _BRCM_SAI_DM_IDX_INIT1(_brcm_sai_port_rif_t,
                                   _brcm_sai_port_rif_table);
            break;
        case _BRCM_SAI_INDEXED_LAG_INFO_TABLE:
            _BRCM_SAI_DM_IDX_INIT1(_brcm_sai_lag_info_t,
                                   _brcm_sai_lag_info_table);
            break;
        case _BRCM_SAI_INDEXED_VLAN_RIF_TABLE:
            _BRCM_SAI_DM_IDX_INIT1(_brcm_sai_vlan_rif_t,
                                   _brcm_sai_vlan_rif_table);
            break;
        case _BRCM_SAI_INDEXED_ACL_TBL_GRP:
            _BRCM_SAI_DM_IDX_INIT1(_brcm_sai_acl_table_group_t,
                                   _brcm_sai_acl_tbl_grps);
            break;
        case _BRCM_SAI_INDEXED_ACL_TBL_GRP_MEMBR:
            _BRCM_SAI_DM_IDX_INIT1(_brcm_sai_acl_tbl_grp_membr_t,
                                   _brcm_sai_acl_tbl_grp_membrs);
            break;
        case _BRCM_SAI_INDEXED_NBR_ID:
            _BRCM_SAI_DM_IDX_INIT1(_brcm_sai_nbr_id_t,
                                   _brcm_sai_nbr_id);
            break;
        case _BRCM_SAI_INDEXED_MIRROR_SESSION:
            _BRCM_SAI_DM_IDX_INIT1(_brcm_sai_mirror_session_t,
                                   _brcm_sai_mirror_sessions);
            break;
        case _BRCM_SAI_INDEXED_VNI_VLAN:
            _BRCM_SAI_DM_IDX_INIT1(_brcm_sai_vni_vlan_t,
                                   _brcm_sai_vni_vlan);
            break;
        case _BRCM_SAI_INDEXED_TUNNEL_MAP:
            _BRCM_SAI_DM_IDX_INIT1(_brcm_sai_tunnel_map_t,
                                   _brcm_sai_tunnel_map);
            break;
        default:
            return SAI_STATUS_INVALID_PARAMETER;
    }

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n", __FUNCTION__);
    return SAI_STATUS_SUCCESS;
}

/* 2D array type data init routine */
sai_status_t
_brcm_sai_indexed_data_init2(_brcm_sai_indexed_data_type_t type, int entries1,
                             int entries2)
{
    bool wb = _brcm_sai_switch_wb_state_get();

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Enter %s, type = %d\n",
                        __FUNCTION__, type);

    switch (type)
    {
        case _BRCM_SAI_INDEXED_PORT_QID:
            _BRCM_SAI_DM_IDX_INIT2(_brcm_sai_port_qid_t,
                                   _brcm_sai_port_qid);
            break;
        case _BRCM_SAI_INDEXED_PORT_BUF_PROF:
            _BRCM_SAI_DM_IDX_INIT2(_brcm_sai_port_buff_prof_applied_t,
                                   _brcm_sai_port_buff_prof_applied);
            break;
        case _BRCM_SAI_INDEXED_QUEUE_WRED:
            _BRCM_SAI_DM_IDX_INIT2(_brcm_sai_queue_wred_t,
                                   _brcm_sai_queue_wred);
            break;
        case _BRCM_SAI_INDEXED_L0_SCHED:
            _BRCM_SAI_DM_IDX_INIT2(_sai_gport_2_t, _brcm_sai_l0_sched);
            break;
        case _BRCM_SAI_INDEXED_L1_SCHED:
            _BRCM_SAI_DM_IDX_INIT2(_sai_gport_2_t, _brcm_sai_l1_sched);
            break;
        case _BRCM_SAI_INDEXED_UCAST_QUEUE:
            _BRCM_SAI_DM_IDX_INIT2(_sai_gport_2_t, _brcm_sai_Uqueue);
            break;
        case _BRCM_SAI_INDEXED_MCAST_QUEUE:
            _BRCM_SAI_DM_IDX_INIT2(_sai_gport_2_t, _brcm_sai_Mqueue);
            break;
        case _BRCM_SAI_INDEXED_BUF_POOLS:
            _BRCM_SAI_DM_IDX_INIT2(_brcm_sai_buf_pool_t,
                                   _brcm_sai_buf_pools);
            break;
        case _BRCM_SAI_INDEXED_HOSTIF_FILTERS:
            _BRCM_SAI_DM_IDX_INIT2(_brcm_sai_hostif_filter_t,
                                   _brcm_sai_hostif_filter);
            break;
        default:
            return SAI_STATUS_INVALID_PARAMETER;
    }

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n", __FUNCTION__);
    return SAI_STATUS_SUCCESS;
}

/* 1D array type data free routine */
sai_status_t
_brcm_sai_indexed_data_free1(_brcm_sai_indexed_data_type_t type, int start,
                             int end, int ecount)
{
    bool wb = _brcm_sai_switch_wb_state_get();

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Enter %s, type = %d\n",
                        __FUNCTION__, type);

    switch (type)
    {
        case _BRCM_SAI_INDEXED_VR_INFO:
            if (!IS_NULL(_brcm_sai_vrf_map))
            {
                _brcm_sai_vrf_map[0].vr_id = -1; /* A key value of 0 does not work for DB */
            }
            _BRCM_SAI_DM_IDX_FREE1(_vrf_map_table_name, _brcm_sai_vr_info_t,
                                   _brcm_sai_vrf_map, vr_id);
            break;
        case _BRCM_SAI_INDEXED_PORT_INFO:
            if (!IS_NULL(_brcm_sai_port_info))
            {
                _brcm_sai_port_info[0].idx = -1; /* A key value of 0 does not work for DB */
            }
            _BRCM_SAI_DM_IDX_FREE1(_port_info_table_name, _brcm_sai_port_info_t,
                                   _brcm_sai_port_info, idx);
            break;
        case _BRCM_SAI_INDEXED_PORT_SCHED:
            if (!IS_NULL(_brcm_sai_port_sched))
            {
                _brcm_sai_port_sched[0].idx = -1; /* A key value of 0 does not work for DB */
            }
            _BRCM_SAI_DM_IDX_FREE1(_port_sched_table_name, _sai_gport_1_t,
                                   _brcm_sai_port_sched, idx);
            break;
        case _BRCM_SAI_INDEXED_CPU_QUEUE:
            if (!IS_NULL(_brcm_sai_CPUMqueue))
            {
                _brcm_sai_CPUMqueue[0].idx = -1; /* A key value of 0 does not work for DB */
            }
            _BRCM_SAI_DM_IDX_FREE1(_CPUMqueue_table_name, _sai_gport_1_t,
                                   _brcm_sai_CPUMqueue, idx);
            break;
        case _BRCM_SAI_INDEXED_RIF_INFO:
            _BRCM_SAI_DM_IDX_FREE1(_rif_info_table_name, _brcm_sai_rif_info_t,
                                   _brcm_sai_rif_info, valid);
            break;
        case _BRCM_SAI_INDEXED_RIF_TUNNEL_INFO:
            _BRCM_SAI_DM_IDX_FREE1(_rif_tunnel_info_table_name, _brcm_sai_trif_info_t,
                                   _brcm_sai_rif_tunnel_info, valid);
            break;
        case _BRCM_SAI_INDEXED_WRED_PROF:
            _BRCM_SAI_DM_IDX_FREE1(_wred_prof_table_name, _brcm_sai_qos_wred_t,
                                   _brcm_sai_wred, valid);
            break;
        case _BRCM_SAI_INDEXED_NETIF_PORT_MAP:
            _BRCM_SAI_DM_IDX_FREE1(_netif_map_port_table_name, _brcm_sai_netif_map_t,
                                   _brcm_sai_netif_map_port, netif_id);
            break;
        case _BRCM_SAI_INDEXED_NETIF_VLAN_MAP:
            _BRCM_SAI_DM_IDX_FREE1(_netif_map_vlan_table_name, _brcm_sai_netif_map_t,
                                   _brcm_sai_netif_map_vlan, netif_id);
            break;
        case _BRCM_SAI_INDEXED_DOT1P_TC_MAP:
            _BRCM_SAI_DM_IDX_FREE1(_dot1p_tc_map_table_name, _brcm_sai_qos_ingress_map_t,
                                   _brcm_sai_dot1p_tc_map, valid);
            break;
        case _BRCM_SAI_INDEXED_DOT1P_COLOR_MAP:
            _BRCM_SAI_DM_IDX_FREE1(_dot1p_color_table_name, _brcm_sai_qos_ingress_map_t,
                                   _brcm_sai_dot1p_color_map, valid);
            break;
        case _BRCM_SAI_INDEXED_DSCP_TC_MAP:
            _BRCM_SAI_DM_IDX_FREE1(_dscp_tc_table_name, _brcm_sai_qos_ingress_map_t,
                                   _brcm_sai_dscp_tc_map, valid);
            break;
        case _BRCM_SAI_INDEXED_DSCP_COLOR_MAP:
            _BRCM_SAI_DM_IDX_FREE1(_dscp_color_table_name, _brcm_sai_qos_ingress_map_t,
                                   _brcm_sai_dscp_color_map, valid);
            break;
        case _BRCM_SAI_INDEXED_TC_DSCP_MAP:
            _BRCM_SAI_DM_IDX_FREE1(_tc_dscp_map_table_name, _brcm_sai_qos_egress_map_t,
                                   _brcm_sai_tc_dscp_map, valid);
            break;
        case _BRCM_SAI_INDEXED_TC_DOT1P_MAP:
            _BRCM_SAI_DM_IDX_FREE1(_tc_dot1p_map_table_name, _brcm_sai_qos_egress_map_t,
                                   _brcm_sai_tc_dot1p_map, valid);
            break;
        case _BRCM_SAI_INDEXED_TC_QUEUE_MAP:
            _BRCM_SAI_DM_IDX_FREE1(_tc_queue_map_table_name, _brcm_sai_qos_egress_map_t,
                                   _brcm_sai_tc_queue_map, valid);
            break;
        case _BRCM_SAI_INDEXED_TC_PG_MAP:
            _BRCM_SAI_DM_IDX_FREE1(_tc_pg_map_table_name, _brcm_sai_qos_egress_map_t,
                                   _brcm_sai_tc_pg_map, valid);
            break;
        case _BRCM_SAI_INDEXED_PFC_QUEUE_MAP:
            _BRCM_SAI_DM_IDX_FREE1(_pfc_queue_map_table_name, _brcm_sai_qos_egress_map_t,
                                   _brcm_sai_pfc_queue_map, valid);
            break;
        case _BRCM_SAI_INDEXED_SCHED_PROF:
            _BRCM_SAI_DM_IDX_FREE1(_scheduler_table_name, _brcm_sai_qos_scheduler_t,
                                   _brcm_sai_scheduler, valid);
            break;
        case _BRCM_SAI_INDEXED_BUF_PROFILES:
            _BRCM_SAI_DM_IDX_FREE1(_buf_profiles_table_name, _brcm_sai_buf_profile_t,
                                   _brcm_sai_buf_profiles, valid);
            break;
        case _BRCM_SAI_INDEXED_POOL_COUNT:
            if (!IS_NULL(_brcm_sai_pool_count))
            {
                _brcm_sai_pool_count[0].idx = -1; /* A key value of 0 does not work for DB */
            }
            _BRCM_SAI_DM_IDX_FREE1(_pool_count_table_name, _brcm_sai_buf_pool_count_t,
                                   _brcm_sai_pool_count, count);
            break;
        case _BRCM_SAI_INDEXED_TUNNEL_INFO:
            _BRCM_SAI_DM_IDX_FREE1(_tunnel_info_table_name, _brcm_sai_tunnel_info_t,
                                   _brcm_sai_tunnel_info, valid);
            break;
        case _BRCM_SAI_INDEXED_TUNNEL_TABLE:
            _BRCM_SAI_DM_IDX_FREE1(_tunnel_table_table_name, _brcm_sai_tunnel_table_t,
                                   _brcm_sai_tunnel_table, valid);
            break;
        case _BRCM_SAI_INDEXED_HOSTIF_INFO:
            _BRCM_SAI_DM_IDX_FREE1(_hostif_table_name, _brcm_sai_hostif_t,
                                   _brcm_sai_hostif, idx);
            break;
        case _BRCM_SAI_INDEXED_HOSTIF_TABLE:
            _BRCM_SAI_DM_IDX_FREE1(_hostif_trap_table_name, _brcm_sai_hostif_table_entry_t,
                                   _brcm_sai_hostif_table, valid);
            break;
        case _BRCM_SAI_INDEXED_NH_INFO:
            _BRCM_SAI_DM_IDX_FREE1(_nh_info_table_name, _brcm_sai_nh_info_t,
                                   _brcm_sai_nh_info, if_id);
            break;
        case _BRCM_SAI_INDEXED_UDFG_INFO:
            _BRCM_SAI_DM_IDX_FREE1(_udfg_table_name, _brcm_sai_udfg_info_t,
                                   _brcm_sai_udfg, valid);
            break;
        case _BRCM_SAI_INDEXED_HASH_INFO:
            _BRCM_SAI_DM_IDX_FREE1(_hash_table_name, _brcm_sai_hash_info_t,
                                   _brcm_sai_hash, valid);
            break;
        case _BRCM_SAI_INDEXED_TRAP_GROUP:
            if (!IS_NULL(_brcm_sai_trap_groups))
            {
                _brcm_sai_trap_groups[0].idx = -1;
            }
            _BRCM_SAI_DM_IDX_FREE1(_trap_groups_table_name, _brcm_sai_trap_group_t,
                                   _brcm_sai_trap_groups, valid);
            break;
        case _BRCM_SAI_INDEXED_TRAP:
            _BRCM_SAI_DM_IDX_FREE1(_traps_table_name, _brcm_sai_trap_t,
                                   _brcm_sai_traps, valid);
            break;
        case _BRCM_SAI_INDEXED_ACL_TABLE:
            _BRCM_SAI_DM_IDX_FREE1(_acl_tables_table_name, _brcm_sai_acl_table_t,
                                   _brcm_sai_acl_tables, valid);
            break;
        case _BRCM_SAI_INDEXED_PORT_RIF_TABLE:
            _BRCM_SAI_DM_IDX_FREE1(_port_rif_table_name, _brcm_sai_port_rif_t,
                                   _brcm_sai_port_rif_table, rif_obj);
            break;
        case _BRCM_SAI_INDEXED_LAG_INFO_TABLE:
            if (!IS_NULL(_brcm_sai_lag_info_table))
            {
                _brcm_sai_lag_info_table[0].idx = -1;
            }
            _BRCM_SAI_DM_IDX_FREE1(_lag_info_table_name, _brcm_sai_lag_info_t,
                                   _brcm_sai_lag_info_table, valid);
            break;
        case _BRCM_SAI_INDEXED_VLAN_RIF_TABLE:
            _BRCM_SAI_DM_IDX_FREE1(_vlan_rif_table_name, _brcm_sai_vlan_rif_t,
                                   _brcm_sai_vlan_rif_table, rif_obj);
            break;
        case _BRCM_SAI_INDEXED_ACL_TBL_GRP:
            _BRCM_SAI_DM_IDX_FREE1(_acl_tbl_grps_table_name, _brcm_sai_acl_table_group_t,
                                   _brcm_sai_acl_tbl_grps, valid);
            break;
        case _BRCM_SAI_INDEXED_ACL_TBL_GRP_MEMBR:
            _BRCM_SAI_DM_IDX_FREE1(_acl_table_group_membrs_table_name,
                                   _brcm_sai_acl_tbl_grp_membr_t,
                                   _brcm_sai_acl_tbl_grp_membrs, valid);
            break;
        case _BRCM_SAI_INDEXED_NBR_ID:
            _BRCM_SAI_DM_IDX_FREE1(_nbr_id_table_name, _brcm_sai_nbr_id_t,
                                   _brcm_sai_nbr_id, idx);
            break;
        case _BRCM_SAI_INDEXED_MIRROR_SESSION:
            if (!IS_NULL(_brcm_sai_mirror_sessions))
            {
                _brcm_sai_mirror_sessions[0].idx = -1; /* A key value of 0 does not work for DB */
            }
            _BRCM_SAI_DM_IDX_FREE1(_mirror_sessions_table_name, _brcm_sai_mirror_session_t,
                                   _brcm_sai_mirror_sessions, gport);
            break;
        case _BRCM_SAI_INDEXED_VNI_VLAN:
            _BRCM_SAI_DM_IDX_FREE1(_vni_vlan_table_name, _brcm_sai_vni_vlan_t,
                                   _brcm_sai_vni_vlan, valid);
            break;
        case _BRCM_SAI_INDEXED_TUNNEL_MAP:
            _BRCM_SAI_DM_IDX_FREE1(_tunnel_map_table_name, _brcm_sai_tunnel_map_t,
                                   _brcm_sai_tunnel_map, valid);
            break;
        default:
            return SAI_STATUS_INVALID_PARAMETER;
    }

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n", __FUNCTION__);
    return SAI_STATUS_SUCCESS;
}

/* 2D array type data free routine */
sai_status_t
_brcm_sai_indexed_data_free2(_brcm_sai_indexed_data_type_t type, int entries1,
                             int entries2)
{
    bool wb = _brcm_sai_switch_wb_state_get();

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Enter %s, type = %d\n",
                        __FUNCTION__, type);

    switch (type)
    {
        case _BRCM_SAI_INDEXED_PORT_QID:
            _BRCM_SAI_DM_IDX_FREE2(_port_qid_table_name, _brcm_sai_port_qid_t,
                                   _brcm_sai_port_qid, idx1, idx2);
            break;
        case _BRCM_SAI_INDEXED_PORT_BUF_PROF:
            _BRCM_SAI_DM_IDX_FREE2(_port_buff_prof_applied_table_name,
                                   _brcm_sai_port_buff_prof_applied_t,
                                   _brcm_sai_port_buff_prof_applied,
                                   idx1, idx2);
            break;
        case _BRCM_SAI_INDEXED_QUEUE_WRED:
            _BRCM_SAI_DM_IDX_FREE2(_queue_wred_table_name,
                                   _brcm_sai_queue_wred_t,
                                   _brcm_sai_queue_wred, idx1, idx2);
            break;
        case _BRCM_SAI_INDEXED_L0_SCHED:
            _BRCM_SAI_DM_IDX_FREE2(_l0_sched_table_name, _sai_gport_2_t,
                                   _brcm_sai_l0_sched, idx1, idx2);
            break;
        case _BRCM_SAI_INDEXED_L1_SCHED:
            _BRCM_SAI_DM_IDX_FREE2(_l1_sched_table_name, _sai_gport_2_t,
                                   _brcm_sai_l1_sched, idx1, idx2);
            break;
        case _BRCM_SAI_INDEXED_UCAST_QUEUE:
            _BRCM_SAI_DM_IDX_FREE2(_Uqueue_table_name, _sai_gport_2_t,
                                   _brcm_sai_Uqueue, idx1, idx2);
            break;
        case _BRCM_SAI_INDEXED_MCAST_QUEUE:
            _BRCM_SAI_DM_IDX_FREE2(_Mqueue_table_name, _sai_gport_2_t,
                                   _brcm_sai_Mqueue, idx1, idx2);
            break;
        case _BRCM_SAI_INDEXED_BUF_POOLS:
            _BRCM_SAI_DM_IDX_FREE2(_buf_pools_table_name, _brcm_sai_buf_pool_t,
                                   _brcm_sai_buf_pools, idx1, idx2);
            break;
        case _BRCM_SAI_INDEXED_HOSTIF_FILTERS:
            _BRCM_SAI_DM_IDX_FREE2(_hostif_filter_table_name,
                                   _brcm_sai_hostif_filter_t,
                                   _brcm_sai_hostif_filter, idx1, idx2);
            break;
        default:
            return SAI_STATUS_INVALID_PARAMETER;
    }

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n", __FUNCTION__);
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_indexed_data_set(_brcm_sai_indexed_data_type_t type, int index[],
                           _brcm_sai_indexed_data_t *data)
{
    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Enter %s, type = %d\n",
                        __FUNCTION__, type);
    switch (type)
    {
        case _BRCM_SAI_INDEXED_NBR_ID:
            _BRCM_SAI_DM_SET_IDX_DATA1(_brcm_sai_nbr_id, nbr_id);
            break;
        case _BRCM_SAI_INDEXED_VR_INFO:
            _BRCM_SAI_DM_SET_IDX_DATA1(_brcm_sai_vrf_map, vr_info);
            break;
        case _BRCM_SAI_INDEXED_RIF_INFO:
            _BRCM_SAI_DM_SET_IDX_DATA1(_brcm_sai_rif_info, rif_info);
            break;
        case _BRCM_SAI_INDEXED_RIF_TUNNEL_INFO:
            _BRCM_SAI_DM_SET_IDX_DATA1(_brcm_sai_rif_tunnel_info, trif_info);
            break;
        case _BRCM_SAI_INDEXED_TUNNEL_INFO:
            _BRCM_SAI_DM_SET_IDX_DATA1(_brcm_sai_tunnel_info, tunnel_info);
            break;
        case _BRCM_SAI_INDEXED_TUNNEL_TABLE:
            _BRCM_SAI_DM_SET_IDX_DATA1(_brcm_sai_tunnel_table, tunnel_table);
            break;
        case _BRCM_SAI_INDEXED_WRED_PROF:
            _BRCM_SAI_DM_SET_IDX_DATA1(_brcm_sai_wred, wred_prof);
            break;
        case _BRCM_SAI_INDEXED_PORT_QID:
            _BRCM_SAI_DM_SET_IDX_DATA2(_brcm_sai_port_qid, port_qid);
            break;
        case _BRCM_SAI_INDEXED_PORT_INFO:
            _BRCM_SAI_DM_SET_IDX_DATA1(_brcm_sai_port_info, port_info);
            break;
        case _BRCM_SAI_INDEXED_PORT_BUF_PROF:
            _BRCM_SAI_DM_SET_IDX_DATA2(_brcm_sai_port_buff_prof_applied,
                                       port_buff);
            break;
        case _BRCM_SAI_INDEXED_QUEUE_WRED:
            _BRCM_SAI_DM_SET_IDX_DATA2(_brcm_sai_queue_wred, queue_wred);
            break;
        case _BRCM_SAI_INDEXED_PORT_SCHED:
            _BRCM_SAI_DM_SET_IDX_DATA1(_brcm_sai_port_sched, gport1);
            break;
        case _BRCM_SAI_INDEXED_CPU_QUEUE:
            _BRCM_SAI_DM_SET_IDX_DATA1(_brcm_sai_CPUMqueue, gport1);
            break;
        case _BRCM_SAI_INDEXED_L0_SCHED:
            _BRCM_SAI_DM_SET_IDX_DATA2(_brcm_sai_l0_sched, gport2);
            break;
        case _BRCM_SAI_INDEXED_L1_SCHED:
            _BRCM_SAI_DM_SET_IDX_DATA2(_brcm_sai_l1_sched, gport2);
            break;
        case _BRCM_SAI_INDEXED_UCAST_QUEUE:
            _BRCM_SAI_DM_SET_IDX_DATA2(_brcm_sai_Uqueue, gport2);
            break;
        case _BRCM_SAI_INDEXED_MCAST_QUEUE:
            _BRCM_SAI_DM_SET_IDX_DATA2(_brcm_sai_Mqueue, gport2);
            break;
        case _BRCM_SAI_INDEXED_POOL_COUNT:
            _BRCM_SAI_DM_SET_IDX_DATA1(_brcm_sai_pool_count, pool_count);
            break;
        case _BRCM_SAI_INDEXED_BUF_POOLS:
            _BRCM_SAI_DM_SET_IDX_DATA2_PTR(_brcm_sai_buf_pools, buf_pool);
            break;
        case _BRCM_SAI_INDEXED_BUF_PROFILES:
            _BRCM_SAI_DM_SET_IDX_DATA1_PTR(_brcm_sai_buf_profiles, buf_prof);
            break;
        case _BRCM_SAI_INDEXED_SCHED_PROF:
            _BRCM_SAI_DM_SET_IDX_DATA1(_brcm_sai_scheduler, scheduler_prof);
            break;
        case _BRCM_SAI_INDEXED_NETIF_PORT_MAP:
            _BRCM_SAI_DM_SET_IDX_DATA1_PTR(_brcm_sai_netif_map_port, netif_map);
            break;
        case _BRCM_SAI_INDEXED_NETIF_VLAN_MAP:
            _BRCM_SAI_DM_SET_IDX_DATA1_PTR(_brcm_sai_netif_map_vlan, netif_map);
            break;
        case _BRCM_SAI_INDEXED_DOT1P_TC_MAP:
            _BRCM_SAI_DM_SET_IDX_DATA1_PTR(_brcm_sai_dot1p_tc_map, ingress_map);
            break;
        case _BRCM_SAI_INDEXED_DOT1P_COLOR_MAP:
            _BRCM_SAI_DM_SET_IDX_DATA1_PTR(_brcm_sai_dot1p_color_map, ingress_map);
            break;
        case _BRCM_SAI_INDEXED_DSCP_TC_MAP:
            _BRCM_SAI_DM_SET_IDX_DATA1_PTR(_brcm_sai_dscp_tc_map, ingress_map);
            break;
        case _BRCM_SAI_INDEXED_DSCP_COLOR_MAP:
            _BRCM_SAI_DM_SET_IDX_DATA1_PTR(_brcm_sai_dscp_color_map, ingress_map);
            break;
        case _BRCM_SAI_INDEXED_TC_DSCP_MAP:
            _BRCM_SAI_DM_SET_IDX_DATA1_PTR(_brcm_sai_tc_dscp_map, egress_map);
            break;
        case _BRCM_SAI_INDEXED_TC_DOT1P_MAP:
            _BRCM_SAI_DM_SET_IDX_DATA1_PTR(_brcm_sai_tc_dot1p_map, egress_map);
            break;
        case _BRCM_SAI_INDEXED_TC_QUEUE_MAP:
            _BRCM_SAI_DM_SET_IDX_DATA1_PTR(_brcm_sai_tc_queue_map, egress_map);
            break;
        case _BRCM_SAI_INDEXED_TC_PG_MAP:
            _BRCM_SAI_DM_SET_IDX_DATA1_PTR(_brcm_sai_tc_pg_map, egress_map);
            break;
        case _BRCM_SAI_INDEXED_PFC_QUEUE_MAP:
            _BRCM_SAI_DM_SET_IDX_DATA1_PTR(_brcm_sai_pfc_queue_map, egress_map);
            break;
        case _BRCM_SAI_INDEXED_HOSTIF_INFO:
            _BRCM_SAI_DM_SET_IDX_DATA1(_brcm_sai_hostif, hostif_info);
            break;
        case _BRCM_SAI_INDEXED_HOSTIF_FILTERS:
            _BRCM_SAI_DM_SET_IDX_DATA2(_brcm_sai_hostif_filter, hostif_filter);
            break;
        case _BRCM_SAI_INDEXED_NH_INFO:
            _BRCM_SAI_DM_SET_IDX_DATA1(_brcm_sai_nh_info, nh_info);
            break;
        case _BRCM_SAI_INDEXED_UDFG_INFO:
            _BRCM_SAI_DM_SET_IDX_DATA1(_brcm_sai_udfg, udfg);
            break;
        case _BRCM_SAI_INDEXED_HASH_INFO:
            _BRCM_SAI_DM_SET_IDX_DATA1(_brcm_sai_hash, hash);
            break;
        case _BRCM_SAI_INDEXED_TRAP_GROUP:
            _BRCM_SAI_DM_SET_IDX_DATA1(_brcm_sai_trap_groups, trap_group);
            break;
        case _BRCM_SAI_INDEXED_TRAP:
            _BRCM_SAI_DM_SET_IDX_DATA1(_brcm_sai_traps, trap);
            break;
        case _BRCM_SAI_INDEXED_ACL_TABLE:
            _BRCM_SAI_DM_SET_IDX_DATA1(_brcm_sai_acl_tables, acl_table);
            break;
        case _BRCM_SAI_INDEXED_HOSTIF_TABLE:
            _BRCM_SAI_DM_SET_IDX_DATA1(_brcm_sai_hostif_table, hostif_table);
            break;
        case _BRCM_SAI_INDEXED_PORT_RIF_TABLE:
            _BRCM_SAI_DM_SET_IDX_DATA1(_brcm_sai_port_rif_table, port_rif);
            break;
        case _BRCM_SAI_INDEXED_LAG_INFO_TABLE:
            _BRCM_SAI_DM_SET_IDX_DATA1(_brcm_sai_lag_info_table, lag_info);
            break;
        case _BRCM_SAI_INDEXED_VLAN_RIF_TABLE:
            _BRCM_SAI_DM_SET_IDX_DATA1(_brcm_sai_vlan_rif_table, vlan_rif);
            break;
        case _BRCM_SAI_INDEXED_ACL_TBL_GRP:
            _BRCM_SAI_DM_SET_IDX_DATA1(_brcm_sai_acl_tbl_grps, acl_tbl_grp);
            break;
        case _BRCM_SAI_INDEXED_ACL_TBL_GRP_MEMBR:
            _BRCM_SAI_DM_SET_IDX_DATA1(_brcm_sai_acl_tbl_grp_membrs,
                                       acl_tbl_grp_membr);
            break;
        case _BRCM_SAI_INDEXED_MIRROR_SESSION:
            _BRCM_SAI_DM_SET_IDX_DATA1(_brcm_sai_mirror_sessions, ms);
            break;
        case _BRCM_SAI_INDEXED_VNI_VLAN:
            _BRCM_SAI_DM_SET_IDX_DATA1(_brcm_sai_vni_vlan, vni_vlan);
            break;
        case _BRCM_SAI_INDEXED_TUNNEL_MAP:
            _BRCM_SAI_DM_SET_IDX_DATA1(_brcm_sai_tunnel_map, tunnel_map);
            break;
        default:
            return SAI_STATUS_INVALID_PARAMETER;
    }
    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n", __FUNCTION__);
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_indexed_data_get(_brcm_sai_indexed_data_type_t type, int index[],
                           _brcm_sai_indexed_data_t *data)
{
    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Enter %s, type = %d\n",
                        __FUNCTION__, type);
    switch (type)
    {
        case _BRCM_SAI_INDEXED_NBR_ID:
            _BRCM_SAI_DM_GET_IDX_DATA1(_brcm_sai_nbr_id, nbr_id);
            break;
        case _BRCM_SAI_INDEXED_VR_INFO:
            _BRCM_SAI_DM_GET_IDX_DATA1(_brcm_sai_vrf_map, vr_info);
            break;
        case _BRCM_SAI_INDEXED_RIF_INFO:
            _BRCM_SAI_DM_GET_IDX_DATA1(_brcm_sai_rif_info, rif_info);
            break;
        case _BRCM_SAI_INDEXED_RIF_TUNNEL_INFO:
            _BRCM_SAI_DM_GET_IDX_DATA1(_brcm_sai_rif_tunnel_info, trif_info);
            break;
        case _BRCM_SAI_INDEXED_TUNNEL_INFO:
            _BRCM_SAI_DM_GET_IDX_DATA1(_brcm_sai_tunnel_info, tunnel_info);
            break;
        case _BRCM_SAI_INDEXED_TUNNEL_TABLE:
            _BRCM_SAI_DM_GET_IDX_DATA1(_brcm_sai_tunnel_table, tunnel_table);
            break;
        case _BRCM_SAI_INDEXED_WRED_PROF:
            _BRCM_SAI_DM_GET_IDX_DATA1(_brcm_sai_wred, wred_prof);
            break;
        case _BRCM_SAI_INDEXED_PORT_QID:
            _BRCM_SAI_DM_GET_IDX_DATA2(_brcm_sai_port_qid, port_qid);
            break;
        case _BRCM_SAI_INDEXED_PORT_INFO:
            _BRCM_SAI_DM_GET_IDX_DATA1(_brcm_sai_port_info, port_info);
            break;
        case _BRCM_SAI_INDEXED_PORT_BUF_PROF:
            _BRCM_SAI_DM_GET_IDX_DATA2(_brcm_sai_port_buff_prof_applied,
                                       port_buff);
            break;
        case _BRCM_SAI_INDEXED_QUEUE_WRED:
            _BRCM_SAI_DM_GET_IDX_DATA2(_brcm_sai_queue_wred, queue_wred);
            break;
        case _BRCM_SAI_INDEXED_VLAN_BMP:
            data->vlan_bmp = _brcm_sai_vlan_bmp;
            break;
        case _BRCM_SAI_INDEXED_PORT_SCHED:
            _BRCM_SAI_DM_GET_IDX_DATA1(_brcm_sai_port_sched, gport1);
            break;
        case _BRCM_SAI_INDEXED_CPU_QUEUE:
            _BRCM_SAI_DM_GET_IDX_DATA1(_brcm_sai_CPUMqueue, gport1);
            break;
        case _BRCM_SAI_INDEXED_L0_SCHED:
            _BRCM_SAI_DM_GET_IDX_DATA2(_brcm_sai_l0_sched, gport2);
            break;
        case _BRCM_SAI_INDEXED_L1_SCHED:
            _BRCM_SAI_DM_GET_IDX_DATA2(_brcm_sai_l1_sched, gport2);
            break;
        case _BRCM_SAI_INDEXED_UCAST_QUEUE:
            _BRCM_SAI_DM_GET_IDX_DATA2(_brcm_sai_Uqueue, gport2);
            break;
        case _BRCM_SAI_INDEXED_MCAST_QUEUE:
            _BRCM_SAI_DM_GET_IDX_DATA2(_brcm_sai_Mqueue, gport2);
            break;
        case _BRCM_SAI_INDEXED_DOT1P_TC_MAP:
            _BRCM_SAI_DM_GET_IDX_DATA1_PTR(_brcm_sai_dot1p_tc_map, ingress_map);
            break;
        case _BRCM_SAI_INDEXED_DOT1P_COLOR_MAP:
            _BRCM_SAI_DM_GET_IDX_DATA1_PTR(_brcm_sai_dot1p_color_map, ingress_map);
            break;
        case _BRCM_SAI_INDEXED_DSCP_TC_MAP:
            _BRCM_SAI_DM_GET_IDX_DATA1_PTR(_brcm_sai_dscp_tc_map, ingress_map);
            break;
        case _BRCM_SAI_INDEXED_DSCP_COLOR_MAP:
            _BRCM_SAI_DM_GET_IDX_DATA1_PTR(_brcm_sai_dscp_color_map, ingress_map);
            break;
        case _BRCM_SAI_INDEXED_TC_DSCP_MAP:
            _BRCM_SAI_DM_GET_IDX_DATA1_PTR(_brcm_sai_tc_dscp_map, egress_map);
            break;
        case _BRCM_SAI_INDEXED_TC_DOT1P_MAP:
            _BRCM_SAI_DM_GET_IDX_DATA1_PTR(_brcm_sai_tc_dot1p_map, egress_map);
            break;
        case _BRCM_SAI_INDEXED_TC_QUEUE_MAP:
            _BRCM_SAI_DM_GET_IDX_DATA1_PTR(_brcm_sai_tc_queue_map, egress_map);
            break;
        case _BRCM_SAI_INDEXED_TC_PG_MAP:
            _BRCM_SAI_DM_GET_IDX_DATA1_PTR(_brcm_sai_tc_pg_map, egress_map);
            break;
        case _BRCM_SAI_INDEXED_PFC_QUEUE_MAP:
            _BRCM_SAI_DM_GET_IDX_DATA1_PTR(_brcm_sai_pfc_queue_map, egress_map);
            break;
        case _BRCM_SAI_INDEXED_NETIF_PORT_MAP:
            _BRCM_SAI_DM_GET_IDX_DATA1_PTR(_brcm_sai_netif_map_port, netif_map);
            break;
        case _BRCM_SAI_INDEXED_NETIF_VLAN_MAP:
            _BRCM_SAI_DM_GET_IDX_DATA1_PTR(_brcm_sai_netif_map_vlan, netif_map);
            break;
        case _BRCM_SAI_INDEXED_POOL_COUNT:
            _BRCM_SAI_DM_GET_IDX_DATA1(_brcm_sai_pool_count, pool_count);
            break;
        case _BRCM_SAI_INDEXED_BUF_POOLS:
            _BRCM_SAI_DM_GET_IDX_DATA2_PTR(_brcm_sai_buf_pools, buf_pool);
            break;
        case _BRCM_SAI_INDEXED_BUF_PROFILES:
            _BRCM_SAI_DM_GET_IDX_DATA1_PTR(_brcm_sai_buf_profiles, buf_prof);
            break;
        case _BRCM_SAI_INDEXED_SCHED_PROF:
            _BRCM_SAI_DM_GET_IDX_DATA1(_brcm_sai_scheduler, scheduler_prof);
            break;
        case _BRCM_SAI_INDEXED_HOSTIF_INFO:
            _BRCM_SAI_DM_GET_IDX_DATA1(_brcm_sai_hostif, hostif_info);
            break;
        case _BRCM_SAI_INDEXED_HOSTIF_FILTERS:
            _BRCM_SAI_DM_GET_IDX_DATA2(_brcm_sai_hostif_filter, hostif_filter);
            break;
        case _BRCM_SAI_INDEXED_NH_INFO:
            _BRCM_SAI_DM_GET_IDX_DATA1(_brcm_sai_nh_info, nh_info);
            break;
        case _BRCM_SAI_INDEXED_UDFG_INFO:
            _BRCM_SAI_DM_GET_IDX_DATA1(_brcm_sai_udfg, udfg);
            break;
        case _BRCM_SAI_INDEXED_HASH_INFO:
            _BRCM_SAI_DM_GET_IDX_DATA1(_brcm_sai_hash, hash);
            break;
        case _BRCM_SAI_INDEXED_TRAP_GROUP:
            _BRCM_SAI_DM_GET_IDX_DATA1(_brcm_sai_trap_groups, trap_group);
            break;
        case _BRCM_SAI_INDEXED_TRAP:
            _BRCM_SAI_DM_GET_IDX_DATA1(_brcm_sai_traps, trap);
            break;
        case _BRCM_SAI_INDEXED_ACL_TABLE:
            _BRCM_SAI_DM_GET_IDX_DATA1(_brcm_sai_acl_tables, acl_table);
            break;
        case _BRCM_SAI_INDEXED_HOSTIF_TABLE:
            _BRCM_SAI_DM_GET_IDX_DATA1(_brcm_sai_hostif_table, hostif_table);
            break;
        case _BRCM_SAI_INDEXED_PORT_RIF_TABLE:
            _BRCM_SAI_DM_GET_IDX_DATA1(_brcm_sai_port_rif_table, port_rif);
            break;
        case _BRCM_SAI_INDEXED_LAG_INFO_TABLE:
            _BRCM_SAI_DM_GET_IDX_DATA1(_brcm_sai_lag_info_table, lag_info);
            break;
        case _BRCM_SAI_INDEXED_VLAN_RIF_TABLE:
            _BRCM_SAI_DM_GET_IDX_DATA1(_brcm_sai_vlan_rif_table, vlan_rif);
            break;
        case _BRCM_SAI_INDEXED_ACL_TBL_GRP:
            _BRCM_SAI_DM_GET_IDX_DATA1(_brcm_sai_acl_tbl_grps, acl_tbl_grp);
            break;
        case _BRCM_SAI_INDEXED_ACL_TBL_GRP_MEMBR:
            _BRCM_SAI_DM_GET_IDX_DATA1(_brcm_sai_acl_tbl_grp_membrs,
                                       acl_tbl_grp_membr);
            break;
        case _BRCM_SAI_INDEXED_MIRROR_SESSION:
            _BRCM_SAI_DM_GET_IDX_DATA1(_brcm_sai_mirror_sessions, ms);
            break;
        case _BRCM_SAI_INDEXED_VNI_VLAN:
            _BRCM_SAI_DM_GET_IDX_DATA1(_brcm_sai_vni_vlan, vni_vlan);
            break;
        case _BRCM_SAI_INDEXED_TUNNEL_MAP:
            _BRCM_SAI_DM_GET_IDX_DATA1(_brcm_sai_tunnel_map, tunnel_map);
            break;
        default:
            return SAI_STATUS_INVALID_PARAMETER;
    }
    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n", __FUNCTION__);
    return SAI_STATUS_SUCCESS;
}

/* 1D array type data entry reserve routine */
sai_status_t
_brcm_sai_indexed_data_reserve_index(_brcm_sai_indexed_data_type_t type,
                                     int start, int end, int *index)
{
    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Enter %s, type = %d\n",
                        __FUNCTION__, type);
    switch (type)
    {
        case _BRCM_SAI_INDEXED_VR_INFO:
            _BRCM_SAI_DM_IDX_RSV(_brcm_sai_vrf_map, _brcm_sai_vr_info_t, vr_id,
                                 start, end, FALSE);
            break;
        case _BRCM_SAI_INDEXED_RIF_TUNNEL_INFO:
            _BRCM_SAI_DM_IDX_RSV(_brcm_sai_rif_tunnel_info, _brcm_sai_trif_info_t,
                                 valid, start, end, TRUE);
            break;
        case _BRCM_SAI_INDEXED_TUNNEL_INFO:
            _BRCM_SAI_DM_IDX_RSV(_brcm_sai_tunnel_info, _brcm_sai_tunnel_info_t,
                                 valid, start, end, TRUE);
            break;
        case _BRCM_SAI_INDEXED_TUNNEL_TABLE:
            _BRCM_SAI_DM_IDX_RSV(_brcm_sai_tunnel_table, _brcm_sai_tunnel_table_t,
                                 valid, start, end, TRUE);
            break;
        case _BRCM_SAI_INDEXED_WRED_PROF:
            _BRCM_SAI_DM_IDX_RSV(_brcm_sai_wred, _brcm_sai_qos_wred_t, valid,
                                 start, end, TRUE);
            break;
        case _BRCM_SAI_INDEXED_DOT1P_TC_MAP:
            _BRCM_SAI_DM_IDX_RSV(_brcm_sai_dot1p_tc_map, _brcm_sai_qos_ingress_map_t,
                                 valid, start, end, TRUE);
            break;
        case _BRCM_SAI_INDEXED_DOT1P_COLOR_MAP:
            _BRCM_SAI_DM_IDX_RSV(_brcm_sai_dot1p_color_map, _brcm_sai_qos_ingress_map_t,
                                 valid, start, end, TRUE);
            break;
        case _BRCM_SAI_INDEXED_DSCP_TC_MAP:
            _BRCM_SAI_DM_IDX_RSV(_brcm_sai_dscp_tc_map, _brcm_sai_qos_ingress_map_t,
                                 valid, start, end, TRUE);
            break;
        case _BRCM_SAI_INDEXED_DSCP_COLOR_MAP:
            _BRCM_SAI_DM_IDX_RSV(_brcm_sai_dscp_color_map, _brcm_sai_qos_ingress_map_t,
                                 valid, start, end, TRUE);
            break;
        case _BRCM_SAI_INDEXED_TC_DSCP_MAP:
            _BRCM_SAI_DM_IDX_RSV(_brcm_sai_tc_dscp_map, _brcm_sai_qos_egress_map_t,
                                 valid, start, end, TRUE);
            break;
        case _BRCM_SAI_INDEXED_TC_DOT1P_MAP:
            _BRCM_SAI_DM_IDX_RSV(_brcm_sai_tc_dot1p_map, _brcm_sai_qos_egress_map_t,
                                  valid, start, end, TRUE);
            break;
        case _BRCM_SAI_INDEXED_TC_QUEUE_MAP:
            _BRCM_SAI_DM_IDX_RSV(_brcm_sai_tc_queue_map, _brcm_sai_qos_egress_map_t,
                                 valid, start, end, TRUE);
            break;
        case _BRCM_SAI_INDEXED_TC_PG_MAP:
            _BRCM_SAI_DM_IDX_RSV(_brcm_sai_tc_pg_map, _brcm_sai_qos_egress_map_t,
                                 valid, start, end, TRUE);
            break;
        case _BRCM_SAI_INDEXED_PFC_QUEUE_MAP:
            _BRCM_SAI_DM_IDX_RSV(_brcm_sai_pfc_queue_map, _brcm_sai_qos_egress_map_t,
                                 valid, start, end, TRUE);
            break;
        case _BRCM_SAI_INDEXED_BUF_PROFILES:
            _BRCM_SAI_DM_IDX_RSV(_brcm_sai_buf_profiles, _brcm_sai_buf_profile_t,
                                 valid, start, end, TRUE);
            break;
        case _BRCM_SAI_INDEXED_SCHED_PROF:
            _BRCM_SAI_DM_IDX_RSV(_brcm_sai_scheduler, _brcm_sai_qos_scheduler_t,
                                 valid, start, end, TRUE);
            break;
        case _BRCM_SAI_INDEXED_NH_INFO:
            _BRCM_SAI_DM_IDX_RSV(_brcm_sai_nh_info, _brcm_sai_nh_info_t, if_id,
                                 start, end, TRUE);
            break;
        case _BRCM_SAI_INDEXED_UDFG_INFO:
            _BRCM_SAI_DM_IDX_RSV(_brcm_sai_udfg, _brcm_sai_udfg_info_t, valid,
                                 start, end, TRUE);
            break;
        case _BRCM_SAI_INDEXED_HASH_INFO:
            _BRCM_SAI_DM_IDX_RSV(_brcm_sai_hash, _brcm_sai_hash_info_t, valid,
                                 start, end, TRUE);
            break;
        case _BRCM_SAI_INDEXED_TRAP_GROUP:
            _BRCM_SAI_DM_IDX_RSV(_brcm_sai_trap_groups, _brcm_sai_trap_group_t,
                                 valid, start, end, TRUE);
            break;
        case _BRCM_SAI_INDEXED_TRAP:
            _BRCM_SAI_DM_IDX_RSV(_brcm_sai_traps, _brcm_sai_trap_t, valid,
                                 start, end, TRUE);
            break;
        case _BRCM_SAI_INDEXED_HOSTIF_TABLE:
            _BRCM_SAI_DM_IDX_RSV(_brcm_sai_hostif_table,
                                 _brcm_sai_hostif_table_entry_t, valid, start,
                                 end, TRUE);
            break;
        case _BRCM_SAI_INDEXED_ACL_TBL_GRP:
            _BRCM_SAI_DM_IDX_RSV(_brcm_sai_acl_tbl_grps, _brcm_sai_acl_table_group_t,
                                 valid, start, end, TRUE);
            break;
        case _BRCM_SAI_INDEXED_ACL_TBL_GRP_MEMBR:
            _BRCM_SAI_DM_IDX_RSV(_brcm_sai_acl_tbl_grp_membrs,
                                 _brcm_sai_acl_tbl_grp_membr_t, valid, start,
                                 end, TRUE);
            break;
        case _BRCM_SAI_INDEXED_NBR_ID:
            _BRCM_SAI_DM_IDX_RSV(_brcm_sai_nbr_id, _brcm_sai_nbr_id_t, idx,
                                 start, end, FALSE);
            break;
        case _BRCM_SAI_INDEXED_MIRROR_SESSION:
            _BRCM_SAI_DM_IDX_RSV(_brcm_sai_mirror_sessions, _brcm_sai_mirror_session_t, idx,
                                 start, end, FALSE);
            break;
        case _BRCM_SAI_INDEXED_VNI_VLAN:
            _BRCM_SAI_DM_IDX_RSV(_brcm_sai_vni_vlan, _brcm_sai_vni_vlan_t, valid, start, end, TRUE);
            break;
        case _BRCM_SAI_INDEXED_TUNNEL_MAP:
            _BRCM_SAI_DM_IDX_RSV(_brcm_sai_tunnel_map, _brcm_sai_tunnel_map_t, valid, start, end, TRUE);
            break;
        default:
            return SAI_STATUS_INVALID_PARAMETER;
    }
    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n", __FUNCTION__);
    return SAI_STATUS_SUCCESS;
}

/* 2D array type data entry reserve routine */
sai_status_t
_brcm_sai_indexed_data_reserve_index2(_brcm_sai_indexed_data_type_t type,
                                      int start, int end, int i1, int *index)
{
    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Enter %s, type = %d\n",
                        __FUNCTION__, type);
    switch (type)
    {
        case _BRCM_SAI_INDEXED_BUF_POOLS:
            _BRCM_SAI_DM_IDX_RSV2(_brcm_sai_buf_pools, valid, start, end, i1,
                                  TRUE);
            break;
        default:
            return SAI_STATUS_INVALID_PARAMETER;
    }
    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n", __FUNCTION__);
    return SAI_STATUS_SUCCESS;
}

/* 1D array type data entry free routine */
void
_brcm_sai_indexed_data_free_index(_brcm_sai_indexed_data_type_t type, int index)
{
    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Enter %s, type = %d\n",
                        __FUNCTION__, type);
    switch (type)
    {
        case _BRCM_SAI_INDEXED_VR_INFO:
            _brcm_sai_vrf_map[index].vr_id = 0;
            break;
        case _BRCM_SAI_INDEXED_RIF_INFO:
            _brcm_sai_rif_info[index].valid = FALSE;
            break;
        case _BRCM_SAI_INDEXED_RIF_TUNNEL_INFO:
            _brcm_sai_rif_tunnel_info[index].valid = FALSE;
            break;
        case _BRCM_SAI_INDEXED_TUNNEL_INFO:
            _brcm_sai_tunnel_info[index].valid = FALSE;
            break;
        case _BRCM_SAI_INDEXED_TUNNEL_TABLE:
            _brcm_sai_tunnel_table[index].valid = FALSE;
            break;
        case _BRCM_SAI_INDEXED_WRED_PROF:
            _brcm_sai_wred[index].valid = FALSE;
            break;
        case _BRCM_SAI_INDEXED_DOT1P_TC_MAP:
            _brcm_sai_dot1p_tc_map[index].valid = FALSE;
            break;
        case _BRCM_SAI_INDEXED_DOT1P_COLOR_MAP:
            _brcm_sai_dot1p_color_map[index].valid = FALSE;
            break;
        case _BRCM_SAI_INDEXED_DSCP_TC_MAP:
            _brcm_sai_dscp_tc_map[index].valid = FALSE;
            break;
        case _BRCM_SAI_INDEXED_DSCP_COLOR_MAP:
            _brcm_sai_dscp_color_map[index].valid = FALSE;
            break;
        case _BRCM_SAI_INDEXED_TC_DSCP_MAP:
            _brcm_sai_tc_dscp_map[index].valid = FALSE;
            break;
        case _BRCM_SAI_INDEXED_TC_DOT1P_MAP:
            _brcm_sai_tc_dot1p_map[index].valid = FALSE;
            break;
        case _BRCM_SAI_INDEXED_TC_QUEUE_MAP:
            _brcm_sai_tc_queue_map[index].valid = FALSE;
            break;
        case _BRCM_SAI_INDEXED_TC_PG_MAP:
            _brcm_sai_tc_pg_map[index].valid = FALSE;
            break;
        case _BRCM_SAI_INDEXED_PFC_QUEUE_MAP:
            _brcm_sai_pfc_queue_map[index].valid = FALSE;
            break;
        case _BRCM_SAI_INDEXED_BUF_PROFILES:
            _brcm_sai_buf_profiles[index].valid = FALSE;
            break;
        case _BRCM_SAI_INDEXED_SCHED_PROF:
            _brcm_sai_scheduler[index].valid = FALSE;
            break;
        case _BRCM_SAI_INDEXED_NH_INFO:
            _brcm_sai_nh_info[index].if_id = 0;
            break;
        case _BRCM_SAI_INDEXED_UDFG_INFO:
            _brcm_sai_udfg[index].valid = FALSE;
            break;
        case _BRCM_SAI_INDEXED_HASH_INFO:
            _brcm_sai_hash[index].valid = FALSE;
            _brcm_sai_hash[index].hash_fields_count = 0;
            break;
        case _BRCM_SAI_INDEXED_TRAP_GROUP:
            _brcm_sai_trap_groups[index].valid = FALSE;
            _brcm_sai_trap_groups[index].state = FALSE;
            _brcm_sai_trap_groups[index].qnum = 0;
            _brcm_sai_trap_groups[index].policer_id = 0;
            break;
        case _BRCM_SAI_INDEXED_TRAP:
            _brcm_sai_traps[index].valid = FALSE;
            _brcm_sai_traps[index].installed = FALSE;
            break;
        case _BRCM_SAI_INDEXED_ACL_TABLE:
            _brcm_sai_acl_tables[index].valid = FALSE;
            break;
        case _BRCM_SAI_INDEXED_HOSTIF_TABLE:
            _brcm_sai_hostif_table[index].valid = FALSE;
            break;
        case _BRCM_SAI_INDEXED_PORT_RIF_TABLE:
            _brcm_sai_port_rif_table[index].rif_obj = 0;
            break;
        case _BRCM_SAI_INDEXED_LAG_INFO_TABLE:
            _brcm_sai_lag_info_table[index].rif_obj = 0;
            _brcm_sai_lag_info_table[index].vid = 0;
            _brcm_sai_lag_info_table[index].internal = 0;
            _brcm_sai_lag_info_table[index].bcast_ip = 0;
            break;
        case _BRCM_SAI_INDEXED_VLAN_RIF_TABLE:
            _brcm_sai_vlan_rif_table[index].rif_obj = 0;
            break;
        case _BRCM_SAI_INDEXED_ACL_TBL_GRP:
            _brcm_sai_acl_tbl_grps[index].valid = FALSE;
            break;
        case _BRCM_SAI_INDEXED_ACL_TBL_GRP_MEMBR:
            _brcm_sai_acl_tbl_grp_membrs[index].valid = FALSE;
            break;
        case _BRCM_SAI_INDEXED_NBR_ID:
            _brcm_sai_nbr_id[index].idx = 0;
            break;
        case _BRCM_SAI_INDEXED_MIRROR_SESSION:
            _brcm_sai_mirror_sessions[index].gport = 0;
            break;
        case _BRCM_SAI_INDEXED_VNI_VLAN:
            _brcm_sai_vni_vlan[index].valid = FALSE;
            break;
        case _BRCM_SAI_INDEXED_TUNNEL_MAP:
            _brcm_sai_tunnel_map[index].valid = FALSE;
            break;

        default:
            break;
     }
     BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n", __FUNCTION__);
}

/* 2D array type data entry free routine */
void
_brcm_sai_indexed_data_free_index2(_brcm_sai_indexed_data_type_t type, int i1,
                                   int index)
{
    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Enter %s, type = %d\n",
                        __FUNCTION__, type);
    switch (type)
    {
        case _BRCM_SAI_INDEXED_BUF_POOLS:
            _brcm_sai_buf_pools[i1][index].valid = FALSE;
            break;
        default:
            break;
     }
     BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n", __FUNCTION__);
}

/* Data clear routine */
void
_brcm_sai_indexed_data_clear(_brcm_sai_indexed_data_type_t type, int size)
{
    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Enter %s, type = %d\n",
                        __FUNCTION__, type);
    switch (type)
    {
        case _BRCM_SAI_INDEXED_VLAN_BMP:
            sal_memset(_brcm_sai_vlan_bmp, 0, size);
            break;
        default:
            break;
    }
    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n", __FUNCTION__);
}

/* Routine to load data from DB */
STATIC sai_status_t
_brcm_sai_dm_load_idx_data(_brcm_sai_indexed_data_type_t type, int entries[],
                           bool wb)
{
    int pend, count = 0;
    sai_status_t rv, rc;
    _brcm_sai_indexed_data_t idata;
    unsigned int schema_size, schema_error;
    char schema[SYNCDB_JSON_MAX_SCHEMA_SIZE];
    unsigned int flags = SYNCDB_TABLE_FLAG_STORABLE | SYNCDB_TABLE_FLAG_NVRAM;

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Enter %s, type = %d\n",
                        __FUNCTION__, type);

    switch (type)
    {
        case _BRCM_SAI_INDEXED_VR_INFO:
        {
            _brcm_sai_vr_info_t vr_map;

            DATA_CLEAR(vr_map, _brcm_sai_vr_info_t);
            _BRCM_SAI_DM_LOAD_IDX_DATA
                (_vrf_map_table_name, _vrf_map_table_schema,
                 _vrf_map_table_version, sizeof(_brcm_sai_vr_info_t),
                 vr_map, vr_info, sizeof(int), vr_id, entries[0],
                 _brcm_sai_global_data.vr_count);
            break;
        }
        case _BRCM_SAI_INDEXED_PORT_INFO:
        {
            _brcm_sai_port_info_t wred;

            DATA_CLEAR(wred, _brcm_sai_port_info_t);
            _BRCM_SAI_DM_LOAD_IDX_DATA
                (_port_info_table_name, _port_info_table_schema,
                 _port_info_table_version, sizeof(_brcm_sai_port_info_t),
                 wred, port_info, sizeof(int), idx, entries[0], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_PORT_QID:
        {
            _brcm_sai_port_qid_t pqid;

            DATA_CLEAR(pqid, _brcm_sai_port_qid_t);
            _BRCM_SAI_DM_LOAD_IDX_DATA
                (_port_qid_table_name, _port_qid_table_schema,
                 _port_qid_table_version, sizeof(_brcm_sai_port_qid_t),
                 pqid, port_qid, offsetof(_brcm_sai_port_qid_t, qoid), idx1,
                 entries[0]*entries[1], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_PORT_BUF_PROF:
        {
            _brcm_sai_port_buff_prof_applied_t prof_appl;

            DATA_CLEAR(prof_appl, _brcm_sai_port_buff_prof_applied_t);
            _BRCM_SAI_DM_LOAD_IDX_DATA
                (_port_buff_prof_applied_table_name,
                 _port_buff_prof_applied_table_schema,
                 _port_buff_prof_applied_table_version,
                 sizeof(_brcm_sai_port_buff_prof_applied_t),
                 prof_appl, port_buff,
                 offsetof(_brcm_sai_port_buff_prof_applied_t, prof_applied), idx1,
                 entries[0]*entries[1], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_VLAN_BMP:
        {
            /* Create DB table */
            rv = syncdbUtilSchemaCreate(_vlan_bmp_record_schema,
                                        sizeof(_vlan_bmp_record_schema),
                                        schema, sizeof(schema), &schema_size,
                                        BRCM_SAI_NUM_BYTES(_BRCM_SAI_VR_MAX_VID),
                                        &schema_error);
            BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                                "Creating vlan data schema", rv);
            if (wb)
            {
                flags |= SYNCDB_TABLE_FLAG_FILE_LOAD;
            }
            rv = syncdbRecordTableCreate(&client_id, _vlan_bmp_record_name,
                                         _vlan_bmp_record_version,
                                         BRCM_SAI_NUM_BYTES(_BRCM_SAI_VR_MAX_VID),
                                         flags, schema, schema_size);
            BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                                "Creating vlan data record", rv);
            if (wb)
            {
                /* Load global data from DB */
                rv = syncdbGet(&client_id, _vlan_bmp_record_name,
                               _brcm_sai_vlan_bmp,
                               BRCM_SAI_NUM_BYTES(_BRCM_SAI_VR_MAX_VID), 0);
                BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                                    "vlan data DB record get", rv);
            }
            break;
        }
        case _BRCM_SAI_INDEXED_PORT_SCHED:
        {
            _sai_gport_1_t gport;

            DATA_CLEAR(gport, _sai_gport_1_t);
            _BRCM_SAI_DM_LOAD_IDX_DATA
                (_port_sched_table_name, _gport_1_table_schema,
                 _port_sched_table_version, sizeof(_sai_gport_1_t),
                 gport, gport1, sizeof(int), idx, entries[0], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_CPU_QUEUE:
        {
            _sai_gport_1_t gport;

            DATA_CLEAR(gport, gport);
            _BRCM_SAI_DM_LOAD_IDX_DATA
                (_CPUMqueue_table_name, _gport_1_table_schema,
                 _CPUMqueue_table_version, sizeof(_sai_gport_1_t),
                 gport, gport1, sizeof(int), idx, entries[0], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_L0_SCHED:
        {
            _sai_gport_2_t gport;

            DATA_CLEAR(gport, gport);
            _BRCM_SAI_DM_LOAD_IDX_DATA
                (_l0_sched_table_name, _gport_2_table_schema,
                 _l0_sched_table_version, sizeof(_sai_gport_2_t),
                 gport, gport2, offsetof(_sai_gport_2_t, gport), idx1,
                 entries[0]*entries[1], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_L1_SCHED:
        {
            _sai_gport_2_t gport;

            DATA_CLEAR(gport, gport);
            _BRCM_SAI_DM_LOAD_IDX_DATA
                (_l1_sched_table_name, _gport_2_table_schema,
                 _l1_sched_table_version, sizeof(_sai_gport_2_t),
                 gport, gport2, offsetof(_sai_gport_2_t, gport), idx1,
                 entries[0]*entries[1], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_UCAST_QUEUE:
        {
            _sai_gport_2_t gport;

            DATA_CLEAR(gport, gport);
            _BRCM_SAI_DM_LOAD_IDX_DATA
                (_Uqueue_table_name, _gport_2_table_schema,
                 _Uqueue_table_version, sizeof(_sai_gport_2_t),
                 gport, gport2, offsetof(_sai_gport_2_t, gport), idx1,
                 entries[0]*entries[1], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_MCAST_QUEUE:
        {
            _sai_gport_2_t gport;

            DATA_CLEAR(gport, gport);
            _BRCM_SAI_DM_LOAD_IDX_DATA
                (_Mqueue_table_name, _gport_2_table_schema,
                 _Mqueue_table_version, sizeof(_sai_gport_2_t),
                 gport, gport2, offsetof(_sai_gport_2_t, gport), idx1,
                 entries[0]*entries[1], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_QUEUE_WRED:
        {
            _brcm_sai_queue_wred_t wred;

            DATA_CLEAR(wred, wred);
            _BRCM_SAI_DM_LOAD_IDX_DATA
                (_queue_wred_table_name, _queue_wred_table_schema,
                 _queue_wred_table_version, sizeof(_brcm_sai_queue_wred_t),
                 wred, queue_wred, offsetof(_brcm_sai_queue_wred_t, wred), idx1,
                 entries[0]*entries[1], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_RIF_INFO:
        {
            _brcm_sai_rif_info_t rif;

            DATA_CLEAR(rif, rif);
            _BRCM_SAI_DM_LOAD_IDX_DATA
                (_rif_info_table_name, _rif_info_table_schema,
                 _rif_info_table_version, sizeof(_brcm_sai_rif_info_t),
                 rif, rif_info, sizeof(int), idx, entries[0], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_RIF_TUNNEL_INFO:
        {
            _brcm_sai_trif_info_t trif;

            DATA_CLEAR(trif, trif);
            _BRCM_SAI_DM_LOAD_IDX_DATA
                (_rif_tunnel_info_table_name, _rif_tunnel_info_table_schema,
                 _rif_tunnel_info_table_version, sizeof(_brcm_sai_trif_info_t),
                 trif, trif_info, sizeof(int), idx, entries[0], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_WRED_PROF:
        {
            _brcm_sai_qos_wred_t wred;

            DATA_CLEAR(wred, wred);
            _BRCM_SAI_DM_LOAD_IDX_DATA
                (_wred_prof_table_name, _wred_prof_table_schema,
                 _wred_prof_table_version, sizeof(_brcm_sai_qos_wred_t),
                 wred, wred_prof, sizeof(int), idx, entries[0],
                 _brcm_sai_global_data.wred_count);
            break;
        }
        case _BRCM_SAI_INDEXED_NETIF_PORT_MAP:
        {
            _brcm_sai_netif_map_t map;

            DATA_CLEAR(map, map);
            _BRCM_SAI_DM_LOAD_IDX_DATA_PTR
                (_netif_map_port_table_name, _netif_port_map_table_schema,
                 _netif_port_map_table_version, sizeof(_brcm_sai_netif_map_t),
                 map, &map, netif_map, sizeof(int), idx, entries[0], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_NETIF_VLAN_MAP:
        {
            _brcm_sai_netif_map_t map;

            DATA_CLEAR(map, map);
            _BRCM_SAI_DM_LOAD_IDX_DATA_PTR
                (_netif_map_vlan_table_name, _netif_vlan_map_table_schema,
                 _netif_vlan_map_table_version, sizeof(_brcm_sai_netif_map_t),
                 map, &map, netif_map, sizeof(int), idx, entries[0], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_DOT1P_TC_MAP:
        {
            _brcm_sai_qos_ingress_map_t map;

            DATA_CLEAR(map, map);
            _BRCM_SAI_DM_LOAD_IDX_DATA_PTR
                (_dot1p_tc_map_table_name, _qos_ingress_map_table_schema,
                 _dot1p_tc_map_table_version, sizeof(_brcm_sai_qos_ingress_map_t),
                 map, &map, ingress_map, sizeof(int), idx, entries[0], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_DOT1P_COLOR_MAP:
        {
            _brcm_sai_qos_ingress_map_t map;

            DATA_CLEAR(map, map);
            _BRCM_SAI_DM_LOAD_IDX_DATA_PTR
                (_dot1p_color_table_name, _qos_ingress_map_table_schema,
                 _dot1p_color_table_version, sizeof(_brcm_sai_qos_ingress_map_t),
                 map, &map, ingress_map, sizeof(int), idx, entries[0], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_DSCP_TC_MAP:
        {
            _brcm_sai_qos_ingress_map_t map;

            DATA_CLEAR(map, map);
            _BRCM_SAI_DM_LOAD_IDX_DATA_PTR
                (_dscp_tc_table_name, _qos_ingress_map_table_schema,
                 _dscp_tc_table_version, sizeof(_brcm_sai_qos_ingress_map_t),
                 map, &map, ingress_map, sizeof(int), idx, entries[0], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_DSCP_COLOR_MAP:
        {
            _brcm_sai_qos_ingress_map_t map;

            DATA_CLEAR(map, map);
            _BRCM_SAI_DM_LOAD_IDX_DATA_PTR
                (_dscp_color_table_name, _qos_ingress_map_table_schema,
                 _dscp_color_table_version, sizeof(_brcm_sai_qos_ingress_map_t),
                 map, &map, ingress_map, sizeof(int), idx, entries[0], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_TC_DSCP_MAP:
        {
            _brcm_sai_qos_egress_map_t map;

            DATA_CLEAR(map, map);
            _BRCM_SAI_DM_LOAD_IDX_DATA_PTR
                (_tc_dscp_map_table_name, _qos_egress_map_table_schema,
                 _tc_dscp_map_table_version, sizeof(_brcm_sai_qos_egress_map_t),
                 map, &map, egress_map, sizeof(int), idx, entries[0], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_TC_DOT1P_MAP:
        {
            _brcm_sai_qos_egress_map_t map;

            DATA_CLEAR(map, map);
            _BRCM_SAI_DM_LOAD_IDX_DATA_PTR
                (_tc_dot1p_map_table_name, _qos_egress_map_table_schema,
                 _tc_dot1p_map_table_version, sizeof(_brcm_sai_qos_egress_map_t),
                 map, &map, egress_map, sizeof(int), idx, entries[0], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_TC_QUEUE_MAP:
        {
            _brcm_sai_qos_egress_map_t map;

            DATA_CLEAR(map, map);
            _BRCM_SAI_DM_LOAD_IDX_DATA_PTR
                (_tc_queue_map_table_name, _qos_egress_map_table_schema,
                 _tc_queue_map_table_version, sizeof(_brcm_sai_qos_egress_map_t),
                 map, &map, egress_map, sizeof(int), idx, entries[0], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_TC_PG_MAP:
        {
            _brcm_sai_qos_egress_map_t map;

            DATA_CLEAR(map, map);
            _BRCM_SAI_DM_LOAD_IDX_DATA_PTR
                (_tc_pg_map_table_name, _qos_egress_map_table_schema,
                 _tc_pg_map_table_version, sizeof(_brcm_sai_qos_egress_map_t),
                 map, &map, egress_map, sizeof(int), idx, entries[0], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_PFC_QUEUE_MAP:
        {
            _brcm_sai_qos_egress_map_t map;

            DATA_CLEAR(map, map);
            _BRCM_SAI_DM_LOAD_IDX_DATA_PTR
                (_pfc_queue_map_table_name, _qos_egress_map_table_schema,
                 _pfc_queue_map_table_version, sizeof(_brcm_sai_qos_egress_map_t),
                 map, &map, egress_map, sizeof(int), idx, entries[0], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_SCHED_PROF:
        {
            _brcm_sai_qos_scheduler_t sched;

            DATA_CLEAR(sched, sched);
            _BRCM_SAI_DM_LOAD_IDX_DATA
                (_scheduler_table_name, _scheduler_table_schema,
                 _scheduler_table_version, sizeof(_brcm_sai_qos_scheduler_t),
                 sched, scheduler_prof, sizeof(int), idx, entries[0], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_BUF_POOLS:
        {
            _brcm_sai_buf_pool_t pool;

            DATA_CLEAR(pool, pool);
            _BRCM_SAI_DM_LOAD_IDX_DATA_PTR
                (_buf_pools_table_name, _buf_pools_table_schema,
                 _buf_pools_table_version, sizeof(_brcm_sai_buf_pool_t),
                 pool, &pool, buf_pool, offsetof(_brcm_sai_buf_pool_t, valid),
                 idx1, entries[0]*entries[1], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_BUF_PROFILES:
        {
            _brcm_sai_buf_profile_t profile;

            DATA_CLEAR(profile, profile);
            _BRCM_SAI_DM_LOAD_IDX_DATA_PTR
                (_buf_profiles_table_name, _buf_profiles_table_schema,
                 _buf_profiles_table_version, sizeof(_brcm_sai_buf_profile_t),
                 profile, &profile, buf_prof, sizeof(int), idx, entries[0], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_POOL_COUNT:
        {
            _brcm_sai_buf_pool_count_t pcount;

            DATA_CLEAR(pcount, pcount);
            _BRCM_SAI_DM_LOAD_IDX_DATA
                (_pool_count_table_name, _pool_count_table_schema,
                 _pool_count_table_version, sizeof(_brcm_sai_buf_pool_count_t),
                 pcount, pool_count, sizeof(int), idx, entries[0], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_TUNNEL_INFO:
        {
            _brcm_sai_tunnel_info_t tinfo;

            DATA_CLEAR(tinfo, tinfo);
            _BRCM_SAI_DM_LOAD_IDX_DATA
                (_tunnel_info_table_name, _tunnel_info_table_schema,
                 _tunnel_info_table_version, sizeof(_brcm_sai_tunnel_info_t),
                 tinfo, tunnel_info, sizeof(int), idx, entries[0], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_TUNNEL_TABLE:
        {
            _brcm_sai_tunnel_table_t ttable;

            DATA_CLEAR(ttable, ttable);
            _BRCM_SAI_DM_LOAD_IDX_DATA
                (_tunnel_table_table_name, _tunnel_table_table_schema,
                 _tunnel_table_table_version, sizeof(_brcm_sai_tunnel_table_t),
                 ttable, tunnel_table, sizeof(int), idx, entries[0], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_HOSTIF_INFO:
        {
            _brcm_sai_hostif_t hostif;

            DATA_CLEAR(hostif, hostif);
            _BRCM_SAI_DM_LOAD_IDX_DATA
                (_hostif_table_name, _hostif_table_schema,
                 _hostif_table_version, sizeof(_brcm_sai_hostif_t),
                 hostif, hostif_info, sizeof(int), idx, entries[0], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_HOSTIF_TABLE:
        {
            _brcm_sai_hostif_table_entry_t t;

            DATA_CLEAR(t, t);
            _BRCM_SAI_DM_LOAD_IDX_DATA
                (_hostif_trap_table_name, _hostif_trap_table_schema,
                 _hostif_trap_table_version, sizeof(_brcm_sai_hostif_table_entry_t),
                 t, hostif_table, sizeof(int), idx, entries[0], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_HOSTIF_FILTERS:
        {
            _brcm_sai_hostif_filter_t filter;

            DATA_CLEAR(filter, filter);
            _BRCM_SAI_DM_LOAD_IDX_DATA
                (_hostif_filter_table_name, _hostif_filter_table_schema,
                 _hostif_filter_table_version, sizeof(_brcm_sai_hostif_filter_t),
                 filter, hostif_filter, offsetof(_brcm_sai_hostif_filter_t, type),
                 idx1, entries[0]*entries[1], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_NH_INFO:
        {
            _brcm_sai_nh_info_t nh;

            DATA_CLEAR(nh, nh);
            _BRCM_SAI_DM_LOAD_IDX_DATA
                (_nh_info_table_name, _nh_info_table_schema,
                 _nh_info_table_version, sizeof(_brcm_sai_nh_info_t),
                 nh, nh_info, offsetof(_brcm_sai_nh_info_t, act_type),
                 idx, entries[0], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_UDFG_INFO:
        {
            _brcm_sai_udfg_info_t udf_group;

            DATA_CLEAR(udf_group, udf_group);
            _BRCM_SAI_DM_LOAD_IDX_DATA
                (_udfg_table_name, _udfg_table_schema,
                 _udfg_table_version, sizeof(_brcm_sai_udfg_info_t),
                 udf_group, udfg, sizeof(int), idx, entries[0], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_HASH_INFO:
        {
            _brcm_sai_hash_info_t hash_info;

            DATA_CLEAR(hash_info, hash_info);
            _BRCM_SAI_DM_LOAD_IDX_DATA
                (_hash_table_name, _hash_table_schema,
                 _hash_table_version, sizeof(_brcm_sai_hash_info_t),
                 hash_info, hash, sizeof(int), idx, entries[0], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_TRAP_GROUP:
        {
            _brcm_sai_trap_group_t tg;

            DATA_CLEAR(tg, tg);
            _BRCM_SAI_DM_LOAD_IDX_DATA
                (_trap_groups_table_name, _trap_groups_table_schema,
                 _trap_groups_table_version, sizeof(_brcm_sai_trap_group_t),
                 tg, trap_group, sizeof(int), idx, entries[0], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_TRAP:
        {
            _brcm_sai_trap_t t;

            DATA_CLEAR(t, t);
            _BRCM_SAI_DM_LOAD_IDX_DATA
                (_traps_table_name, _traps_table_schema,
                 _traps_table_version, sizeof(_brcm_sai_trap_t),
                 t, trap, sizeof(int), idx, entries[0], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_ACL_TABLE:
        {
            _brcm_sai_acl_table_t _acl_table;

            DATA_CLEAR(_acl_table, _acl_table);
            _BRCM_SAI_DM_LOAD_IDX_DATA
                (_acl_tables_table_name, _acl_tables_table_schema,
                 _acl_tables_table_version, sizeof(_brcm_sai_acl_table_t),
                 _acl_table, acl_table, sizeof(int), idx, entries[0], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_PORT_RIF_TABLE:
        {
            _brcm_sai_port_rif_t _port_rif_table;

            DATA_CLEAR(_port_rif_table, _port_rif_table);
            _BRCM_SAI_DM_LOAD_IDX_DATA
                (_port_rif_table_name, _port_rif_table_schema,
                 _port_rif_table_version, sizeof(_brcm_sai_port_rif_t),
                 _port_rif_table, port_rif, sizeof(int), idx, entries[0], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_LAG_INFO_TABLE:
        {
            _brcm_sai_lag_info_t _lag_info_table;

            DATA_CLEAR(_lag_info_table, _lag_info_table);
            _BRCM_SAI_DM_LOAD_IDX_DATA
                (_lag_info_table_name, _lag_info_table_schema,
                 _lag_info_table_version, sizeof(_brcm_sai_lag_info_t),
                 _lag_info_table, lag_info, sizeof(int), idx, entries[0], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_VLAN_RIF_TABLE:
        {
            _brcm_sai_vlan_rif_t _vlan_rif_table;

            DATA_CLEAR(_vlan_rif_table, _vlan_rif_table);
            _BRCM_SAI_DM_LOAD_IDX_DATA
                (_vlan_rif_table_name, _vlan_rif_table_schema,
                 _vlan_rif_table_version, sizeof(_brcm_sai_vlan_rif_t),
                 _vlan_rif_table, vlan_rif, sizeof(int), idx, entries[0], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_ACL_TBL_GRP:
        {
            _brcm_sai_acl_table_group_t _acl_tbl_grp_table;

            DATA_CLEAR(_acl_tbl_grp_table, _acl_tbl_grp_table);
            _BRCM_SAI_DM_LOAD_IDX_DATA
                (_acl_tbl_grps_table_name, _acl_tbl_grps_table_schema,
                 _acl_tbl_grps_table_version, sizeof(_brcm_sai_acl_table_group_t),
                 _acl_tbl_grp_table, acl_tbl_grp, sizeof(int), idx, entries[0], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_ACL_TBL_GRP_MEMBR:
        {
            _brcm_sai_acl_tbl_grp_membr_t _acl_tbl_grp_membr_table;

            DATA_CLEAR(_acl_tbl_grp_membr_table, _acl_tbl_grp_membr_table);
            _BRCM_SAI_DM_LOAD_IDX_DATA
                (_acl_table_group_membrs_table_name,
                 _acl_tbl_group_membrs_table_schema,
                 _acl_tbl_group_membrs_table_version,
                 sizeof(_brcm_sai_acl_tbl_grp_membr_t),
                 _acl_tbl_grp_membr_table, acl_tbl_grp_membr, sizeof(int), idx,
                 entries[0], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_NBR_ID:
        {
            _brcm_sai_nbr_id_t _nbr_id_table;

            DATA_CLEAR(_nbr_id_table, _brcm_sai_nbr_id_t);
            _BRCM_SAI_DM_LOAD_IDX_DATA
                (_nbr_id_table_name, _nbr_id_table_schema,
                 _nbr_id_table_version, sizeof(_brcm_sai_nbr_id_t),
                 _nbr_id_table, nbr_id, sizeof(int), idx, entries[0], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_MIRROR_SESSION:
        {
            _brcm_sai_mirror_session_t _ms;

            DATA_CLEAR(_ms, _brcm_sai_mirror_session_t);
            _BRCM_SAI_DM_LOAD_IDX_DATA
                (_mirror_sessions_table_name, _mirror_sessions_table_schema,
                 _mirror_sessions_table_version, sizeof(_brcm_sai_mirror_session_t),
                 _ms, ms, sizeof(int), idx, entries[0], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_VNI_VLAN:
        {
            _brcm_sai_vni_vlan_t _vni_vlan_table;

            DATA_CLEAR(_vni_vlan_table, _brcm_sai_vni_vlan_t);
            _BRCM_SAI_DM_LOAD_IDX_DATA
                (_vni_vlan_table_name, _vni_vlan_table_schema,
                 _vni_vlan_table_version, sizeof(_brcm_sai_vni_vlan_t),
                 _vni_vlan_table, vni_vlan, sizeof(int), idx, entries[0], -1);
            break;
        }
        case _BRCM_SAI_INDEXED_TUNNEL_MAP:
        {
            _brcm_sai_tunnel_map_t _tunnel_map_table;

            DATA_CLEAR(_tunnel_map_table, _brcm_sai_tunnel_map_t);
            _BRCM_SAI_DM_LOAD_IDX_DATA
                (_tunnel_map_table_name, _tunnel_map_table_schema,
                 _tunnel_map_table_version, sizeof(_brcm_sai_tunnel_map_t),
                 _tunnel_map_table, tunnel_map, sizeof(int), idx, entries[0], -1);
            break;
        }
        default:
            return SAI_STATUS_INVALID_PARAMETER;
    }

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n", __FUNCTION__);
    return SAI_STATUS_SUCCESS;
}

/* List type data entry set routine */
STATIC sai_status_t
_brcm_sai_list_entry_set(_brcm_sai_list_data_type_t type, void *entry, void *data)
{
    switch(type)
    {
        case _BRCM_SAI_LIST_SCHED_OBJ_MAP:
            ((_brcm_sai_scheduler_object_t*)entry)->object_id =
                ((_brcm_sai_scheduler_object_t*)data)->object_id;
            break;
        case _BRCM_SAI_LIST_POLICER_OID_MAP:
            ((_brcm_sai_policer_oid_map_t*)entry)->oid =
                ((_brcm_sai_policer_oid_map_t*)data)->oid;
            break;
        case _BRCM_SAI_LIST_POLICER_ACTION:
            *((_brcm_sai_policer_action_t*)entry) =
                *((_brcm_sai_policer_action_t*)data);
            break;
        case _BRCM_SAI_LIST_UDFG_UDF_MAP:
            *((_brcm_sai_udf_object_t*)entry) =
                *((_brcm_sai_udf_object_t*)data);
            break;
        case _BRCM_SAI_LIST_TGRP_TRAP_REF:
            ((_brcm_sai_trap_refs_t*)entry)->trap =
                ((_brcm_sai_trap_refs_t*)data)->trap;
            break;
        case _BRCM_SAI_LIST_ACL_ENTRIES:
            *((_brcm_sai_acl_entry_t*)entry) =
                *((_brcm_sai_acl_entry_t*)data);
            break;
        case _BRCM_SAI_LIST_ACL_TBL_BIND_POINTS:
        case _BRCM_SAI_LIST_LAG_ACL_BIND_POINTS:
            *((_brcm_sai_acl_bind_point_t*)entry) =
              *((_brcm_sai_acl_bind_point_t*)data);
            break;
        case _BRCM_SAI_LIST_ACL_GRP_BIND_POINTS:
            *((_brcm_sai_acl_bind_point_t*)entry) =
                *((_brcm_sai_acl_bind_point_t*)data);
            break;
        case _BRCM_SAI_LIST_BRIDGE_LAG_PORTS:
            *((_brcm_sai_bridge_lag_port_t*)entry) =
                *((_brcm_sai_bridge_lag_port_t*)data);
            break;
        case _BRCM_SAI_LIST_LAG_BP_VLAN_LIST:
            *((_brcm_sai_lag_bp_vlan_info_t*)entry) =
                *((_brcm_sai_lag_bp_vlan_info_t*)data);
            break;
        case _BRCM_SAI_LIST_ROUTE_LIST_FWD:
        case _BRCM_SAI_LIST_ROUTE_LIST_DROP:
            *((_brcm_sai_route_list_t*)entry) =
                *((_brcm_sai_route_list_t*)data);
            break;
        case _BRCM_SAI_LIST_ECMP_NH:
            ((_brcm_sai_nh_list_t*)entry)->nhid =
                ((_brcm_sai_nh_list_t*)data)->nhid;
            break;
        case _BRCM_SAI_LIST_NH_ECMP_INFO:
            *((_brcm_sai_nh_ecmp_t*)entry) =
                *((_brcm_sai_nh_ecmp_t*)data);
            break;
        case _BRCM_SAI_LIST_MAC_NBRS:
            *((_brcm_sai_nbr_info_t*)entry) =
                *((_brcm_sai_nbr_info_t*)data);
            break;
        case _BRCM_SAI_LIST_NBR_NHS:
            *((_brcm_sai_nh_list_t*)entry) =
              *((_brcm_sai_nh_list_t*)data);
            break;
        case _BRCM_SAI_LIST_VLAN_MEMBRS:
            *((_brcm_sai_vlan_membr_list_t*)entry) =
              *((_brcm_sai_vlan_membr_list_t*)data);
            break;
        case _BRCM_SAI_LIST_NBR_BCAST_EOBJS:
            *((_brcm_sai_egress_objects_t*)entry) =
              *((_brcm_sai_egress_objects_t*)data);
            break;
        case _BRCM_SAI_LIST_ACL_TBL_GRP_MEMBR:
            *((_brcm_sai_acl_tbl_grp_membr_tbl_t*)entry) =
                *((_brcm_sai_acl_tbl_grp_membr_tbl_t*)data);
            break;
        default:
            return SAI_STATUS_INVALID_PARAMETER;
    }
    return SAI_STATUS_SUCCESS;
}

/* List type data entry add routine */
sai_status_t
_brcm_sai_list_add(_brcm_sai_list_data_type_t type, _brcm_sai_list_data_t *base,
                   _brcm_sai_list_key_t *key, _brcm_sai_list_data_t *data)
{
    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Enter %s, type = %d\n",
                        __FUNCTION__, type);

    switch(type)
    {
        case _BRCM_SAI_LIST_POLICER_ACTION:
        {
            _brcm_sai_policer_action_t *prev, *entry;

            if (IS_NULL(_brcm_sai_policer_action))
            {
                _brcm_sai_policer_action = ALLOC_CLEAR(1, sizeof(_brcm_sai_policer_action_t));
                __brcm_sai_dm_total_alloc += sizeof(_brcm_sai_policer_action_t);
                if (IS_NULL(_brcm_sai_policer_action))
                {
                    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_ERROR, "%s: Error allocating "
                                        "memory for policer actions.\n",
                                        __FUNCTION__);
                    return SAI_STATUS_NO_MEMORY;
                }
                entry = _brcm_sai_policer_action;
            }
            else
            {
                entry = _brcm_sai_policer_action;
                while (!IS_NULL(entry))
                {
                    prev = entry;
                    entry = entry->next;
                }
                entry = ALLOC_CLEAR(1, sizeof(_brcm_sai_policer_action_t));
                __brcm_sai_dm_total_alloc += sizeof(_brcm_sai_policer_action_t);
                if (IS_NULL(entry))
                {
                    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_ERROR, "%s: Error allocating "
                                        "memory for policer actions.\n",
                                        __FUNCTION__);
                    return SAI_STATUS_NO_MEMORY;
                }
                prev->next = entry;
            }
            sal_memcpy(entry, data->policer_action, sizeof(_brcm_sai_policer_action_t));
            entry->next = NULL;
            break;
        }
        case _BRCM_SAI_LIST_POLICER_OID_MAP:
        {
            _brcm_sai_policer_oid_map_t *prev, *entry;

            if (IS_NULL(*(base->oid_map)))
            {
                *(base->oid_map) = ALLOC_CLEAR(1, sizeof(_brcm_sai_policer_oid_map_t));
                __brcm_sai_dm_total_alloc += sizeof(_brcm_sai_policer_oid_map_t);
                if (IS_NULL(*(base->oid_map)))
                {
                    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_ERROR, "%s: Error allocating "
                                        "memory for policer "
                                        "actions oids.\n", __FUNCTION__);
                    return SAI_STATUS_NO_MEMORY;
                }
                (*(base->oid_map))->oid = key->obj_id;
            }
            else
            {
                entry = *(base->oid_map);
                while (!IS_NULL(entry))
                {
                    prev = entry;
                    entry = entry->next;
                }
                entry = ALLOC_CLEAR(1, sizeof(_brcm_sai_policer_oid_map_t));
                __brcm_sai_dm_total_alloc += sizeof(_brcm_sai_policer_oid_map_t);
                if (IS_NULL(entry))
                {
                    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_ERROR, "%s: Error allocating "
                                        "memory for policer "
                                        "actions oids.\n", __FUNCTION__);
                    return SAI_STATUS_NO_MEMORY;
                }
                entry->oid = key->obj_id;
                prev->next = entry;
            }
            break;
        }
        case _BRCM_SAI_LIST_BRIDGE_LAG_PORTS:
        {
            _brcm_sai_bridge_lag_port_t *prev, *entry;

            if (IS_NULL(_bridge_lag_ports))
            {
                _bridge_lag_ports = ALLOC_CLEAR(1, sizeof(_brcm_sai_bridge_lag_port_t));
                __brcm_sai_dm_total_alloc += sizeof(_brcm_sai_bridge_lag_port_t);
                if (IS_NULL(_bridge_lag_ports))
                {
                    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_ERROR, "%s: Error allocating "
                                        "memory for bridge lag ports.\n",
                                        __FUNCTION__);
                    return SAI_STATUS_NO_MEMORY;
                }
                entry = _bridge_lag_ports;
            }
            else
            {
                entry = _bridge_lag_ports;
                while (!IS_NULL(entry))
                {
                    prev = entry;
                    entry = entry->next;
                }
                entry = ALLOC_CLEAR(1, sizeof(_brcm_sai_bridge_lag_port_t));
                __brcm_sai_dm_total_alloc += sizeof(_brcm_sai_bridge_lag_port_t);
                if (IS_NULL(entry))
                {
                    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_ERROR, "%s: Error allocating "
                                        "memory for bridge lag ports.\n",
                                        __FUNCTION__);
                    return SAI_STATUS_NO_MEMORY;
                }
                prev->next = entry;
            }
            sal_memcpy(entry, data->bdg_lag_ports, sizeof(_brcm_sai_bridge_lag_port_t));
            entry->next = NULL;
            break;
        }
        case _BRCM_SAI_LIST_LAG_BP_VLAN_LIST:
        {
            _brcm_sai_lag_bp_vlan_info_t *prev, *entry;

            if (IS_NULL(base->vid_list))
            {
                base->vid_list = ALLOC_CLEAR(1, sizeof(_brcm_sai_lag_bp_vlan_info_t));
                __brcm_sai_dm_total_alloc += sizeof(_brcm_sai_lag_bp_vlan_info_t);
                if (IS_NULL(base->vid_list))
                {
                    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_ERROR, "%s: Error allocating "
                                        "memory for lag bridge port vlan list\n",
                                        __FUNCTION__);
                    return SAI_STATUS_NO_MEMORY;
                }
                entry = base->vid_list;
            }
            else
            {
                entry = base->vid_list;
                while (!IS_NULL(entry))
                {
                    prev = entry;
                    entry = entry->next;
                }
                entry = ALLOC_CLEAR(1, sizeof(_brcm_sai_lag_bp_vlan_info_t));
                __brcm_sai_dm_total_alloc += sizeof(_brcm_sai_lag_bp_vlan_info_t);
                if (IS_NULL(entry))
                {
                    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_ERROR, "%s: Error allocating "
                                        "memory for lag bridge port vlan list\n",
                                        __FUNCTION__);
                    return SAI_STATUS_NO_MEMORY;
                }
                prev->next = entry;
            }
            sal_memcpy(entry, data->vid_list, sizeof(_brcm_sai_lag_bp_vlan_info_t));
            entry->next = NULL;
            break;
        }
        default:
            return SAI_STATUS_INVALID_PARAMETER;
    }

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n", __FUNCTION__);
    return SAI_STATUS_SUCCESS;
}

/* List type data entry get routine */
sai_status_t
_brcm_sai_list_get(_brcm_sai_list_data_type_t type, _brcm_sai_list_data_t *base,
                   _brcm_sai_list_key_t *key, _brcm_sai_list_data_t *data)
{
    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Enter %s, type = %d\n",
                        __FUNCTION__, type);

    switch(type)
    {
        case _BRCM_SAI_LIST_POLICER_ACTION:
            _BRCM_SAI_DM_LIST_GET(_brcm_sai_policer_action_t, pol_id, key,
                                  pol_id, policer_action,
                                  _brcm_sai_policer_action);
            break;
        case _BRCM_SAI_LIST_UDFG_UDF_MAP:
            _BRCM_SAI_DM_LIST_GET(_brcm_sai_udf_object_t, object_id, key,
                                  obj_id, udf_obj,
                                  (_brcm_sai_udf_object_t *) base);
            break;
        case _BRCM_SAI_LIST_BRIDGE_LAG_PORTS:
            _BRCM_SAI_DM_LIST_GET(_brcm_sai_bridge_lag_port_t, oid, key,
                                  obj_id, bdg_lag_ports,
                                  _bridge_lag_ports);
            break;
        default:
            return SAI_STATUS_INVALID_PARAMETER;
    }

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n", __FUNCTION__);
    return SAI_STATUS_SUCCESS;
}

/* List type data entry delete routine */
sai_status_t
_brcm_sai_list_del(_brcm_sai_list_data_type_t type, _brcm_sai_list_data_t *base,
                   _brcm_sai_list_key_t *key)
{
    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Enter %s, type = %d\n",
                        __FUNCTION__, type);

    switch(type)
    {
        case _BRCM_SAI_LIST_POLICER_ACTION:
            _BRCM_SAI_DM_LIST_DEL(_brcm_sai_policer_action_t,
                                  pol_id, pol_id, TRUE, ref_count,
                                  _brcm_sai_policer_action);
            break;
        case _BRCM_SAI_LIST_BRIDGE_LAG_PORTS:
            _BRCM_SAI_DM_LIST_DEL(_brcm_sai_bridge_lag_port_t, oid, obj_id,
                                  FALSE, oid, /* dummy field */
                                  _bridge_lag_ports);
            break;
        case _BRCM_SAI_LIST_LAG_BP_VLAN_LIST:
            _BRCM_SAI_DM_LIST_DEL(_brcm_sai_lag_bp_vlan_info_t, vid, vid,
                                  FALSE, vid, /* dummy field */
                                  base->vid_list);
            break;
        case _BRCM_SAI_LIST_ROUTE_LIST_FWD:
        case _BRCM_SAI_LIST_ROUTE_LIST_DROP:
            _BRCM_SAI_DM_LIST_DEL_STRUCT(_brcm_sai_route_list_t,
                                         route, route, base->route_list);
            break;
        case _BRCM_SAI_LIST_MAC_NBRS:
            _BRCM_SAI_DM_LIST_DEL_STRUCT(_brcm_sai_nbr_info_t,
                                         nbr, nbr, base->nbrs_list);
            break;
        default:
            return SAI_STATUS_INVALID_PARAMETER;
    }

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n", __FUNCTION__);
    return SAI_STATUS_SUCCESS;
}

/* List type data traverse routine */
sai_status_t
_brcm_sai_list_traverse(_brcm_sai_list_data_type_t type,
                        _brcm_sai_list_data_t *base,
                        _brcm_sai_list_data_t *data)
{
    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Enter %s, type = %d\n",
                        __FUNCTION__, type);

    switch(type)
    {
        case _BRCM_SAI_LIST_POLICER_ACTION:
            _BRCM_SAI_DM_LIST_TRAVERSE(_brcm_sai_policer_action_t,
                                       policer_action, _brcm_sai_policer_action);
            break;
        case _BRCM_SAI_LIST_BRIDGE_LAG_PORTS:
            _BRCM_SAI_DM_LIST_TRAVERSE(_brcm_sai_bridge_lag_port_t,
                                       bdg_lag_ports, _bridge_lag_ports);
            break;
        case _BRCM_SAI_LIST_LAG_BP_VLAN_LIST:
            _BRCM_SAI_DM_LIST_TRAVERSE(_brcm_sai_lag_bp_vlan_info_t,
                                       vid_list, NULL);
            break;
        case _BRCM_SAI_LIST_NH_ECMP_INFO:
            _BRCM_SAI_DM_LIST_TRAVERSE(_brcm_sai_nh_ecmp_t,
                                       nh_ecmp, NULL);
            break;
        case _BRCM_SAI_LIST_ECMP_NH:
            _BRCM_SAI_DM_LIST_TRAVERSE(_brcm_sai_nh_list_t,
                                       nh_list, NULL);
            break;
        case _BRCM_SAI_LIST_ROUTE_LIST_FWD:
        case _BRCM_SAI_LIST_ROUTE_LIST_DROP:
            _BRCM_SAI_DM_LIST_TRAVERSE(_brcm_sai_route_list_t,
                                       route_list, NULL);
            break;
        case _BRCM_SAI_LIST_MAC_NBRS:
            _BRCM_SAI_DM_LIST_TRAVERSE(_brcm_sai_nbr_info_t,
                                       nbrs_list, NULL);
            break;
        default:
            return SAI_STATUS_INVALID_PARAMETER;
    }

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n", __FUNCTION__);
    return SAI_STATUS_SUCCESS;
}

/* List type data alloc routine */
sai_status_t
_brcm_sai_list_init(_brcm_sai_list_data_type_t type, int id,
                    int entries, void **base)
{
    char table_name[40];
    unsigned int schema_size, schema_error;
    char schema[SYNCDB_JSON_MAX_SCHEMA_SIZE];
    bool wb = _brcm_sai_switch_wb_state_get();
    unsigned int flags = SYNCDB_TABLE_FLAG_STORABLE | SYNCDB_TABLE_FLAG_NVRAM |
                         SYNCDB_TABLE_FLAG_FILE_LOAD;

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Enter %s, type = %d\n",
                        __FUNCTION__, type);

    switch(type)
    {
        case _BRCM_SAI_LIST_POLICER_ACTION:
        {
            _brcm_sai_policer_action_t pol;

            DATA_CLEAR(pol, pol);
            _BRCM_SAI_DM_LIST_ALLOC(_policer_action_table_name,
                                    _policer_action_table_schema,
                                    _policer_action_table_version,
                                    _brcm_sai_policer_action_t, pol,
                                    sizeof(bcm_policer_t),
                                    &_brcm_sai_policer_action);
            break;
        }
        case _BRCM_SAI_LIST_POLICER_OID_MAP:
        {
            _brcm_sai_policer_oid_map_t map;

            DATA_CLEAR(map, map);
            _BRCM_SAI_DM_LIST_ALLOC(_policer_oid_map_table_name,
                                    _policer_oid_map_table_schema,
                                    _policer_oid_map_table_version,
                                    _brcm_sai_policer_oid_map_t, map,
                                    sizeof(sai_object_id_t), base);
            break;
        }
        case _BRCM_SAI_LIST_SCHED_OBJ_MAP:
        {
            _brcm_sai_scheduler_object_t obj;

            DATA_CLEAR(obj, obj);
            _BRCM_SAI_DM_LIST_ALLOC(_scheduler_obj_table_name,
                                    _scheduler_obj_table_schema,
                                    _scheduler_obj_table_version,
                                    _brcm_sai_scheduler_object_t, obj,
                                    sizeof(sai_object_id_t), base);
            break;
        }
        case _BRCM_SAI_LIST_UDFG_UDF_MAP:
        {
            _brcm_sai_udf_object_t obj;

            DATA_CLEAR(obj, obj);
            _BRCM_SAI_DM_LIST_ALLOC(_udf_obj_table_name,
                                    _udf_obj_table_schema,
                                    _udf_obj_table_version,
                                    _brcm_sai_udf_object_t, obj,
                                    sizeof(sai_object_id_t), base);
            break;
        }
        case _BRCM_SAI_LIST_TGRP_TRAP_REF:
        {
            _brcm_sai_trap_refs_t traps;

            DATA_CLEAR(traps, traps);
            _BRCM_SAI_DM_LIST_ALLOC(_trap_refs_table_name,
                                    _trap_refs_table_schema,
                                    _trap_refs_table_version,
                                    _brcm_sai_trap_refs_t, traps,
                                    sizeof(int), base);
            break;
        }
        case _BRCM_SAI_LIST_ACL_ENTRIES:
        {
            _brcm_sai_acl_entry_t acl_entries;

            DATA_CLEAR(acl_entries, acl_entries);
            _BRCM_SAI_DM_LIST_ALLOC(_acl_entry_table_name,
                                    _acl_entry_table_schema,
                                    _acl_entry_table_version,
                                    _brcm_sai_acl_entry_t, acl_entries,
                                    sizeof(int), base);
            break;
        }
        case _BRCM_SAI_LIST_LAG_ACL_BIND_POINTS:
        {
            _brcm_sai_acl_bind_point_t bind_points;

            DATA_CLEAR(bind_points, bind_points);
            _BRCM_SAI_DM_LIST_ALLOC(_lag_acl_bind_point_table_name,
                                    _acl_bind_point_table_schema,
                                    _acl_bind_point_table_version,
                                    _brcm_sai_acl_bind_point_t, bind_points,
                                    offsetof(_brcm_sai_acl_bind_point_t, next),
                                    base);
            break;
        }
        case _BRCM_SAI_LIST_ACL_TBL_BIND_POINTS:
        {
            _brcm_sai_acl_bind_point_t bind_points;

            DATA_CLEAR(bind_points, bind_points);
            _BRCM_SAI_DM_LIST_ALLOC(_acl_table_bind_point_table_name,
                                    _acl_bind_point_table_schema,
                                    _acl_bind_point_table_version,
                                    _brcm_sai_acl_bind_point_t, bind_points,
                                    offsetof(_brcm_sai_acl_bind_point_t, next),
                                    base);
            break;
        }
        case _BRCM_SAI_LIST_ACL_GRP_BIND_POINTS:
        {
            _brcm_sai_acl_bind_point_t bind_points;

            DATA_CLEAR(bind_points, bind_points);
            _BRCM_SAI_DM_LIST_ALLOC(_acl_group_bind_point_table_name,
                                    _acl_bind_point_table_schema,
                                    _acl_bind_point_table_version,
                                    _brcm_sai_acl_bind_point_t, bind_points,
                                    offsetof(_brcm_sai_acl_bind_point_t, next),
                                    base);
            break;
        }
        case _BRCM_SAI_LIST_REDIRECT:
        {
            _brcm_sai_redirect_entry_list_t redirect_list;

            DATA_CLEAR(redirect_list, _brcm_sai_redirect_entry_list_t);
            _BRCM_SAI_DM_LIST_ALLOC(_redirect_entry_list_name,
                                    _redirect_entry_list_schema,
                                    _redirect_entry_list_version,
                                    _brcm_sai_redirect_entry_list_t,
                                    redirect_list,
                                    offsetof(_brcm_sai_redirect_entry_list_t, next),
                                    base);

            break;
        }
        case _BRCM_SAI_LIST_BRIDGE_LAG_PORTS:
        {
            _brcm_sai_bridge_lag_port_t bdg_lag_port;

            DATA_CLEAR(bdg_lag_port, _brcm_sai_bridge_lag_port_t);
            _BRCM_SAI_DM_LIST_ALLOC(_bridge_lag_ports_list_name,
                                    _bridge_lag_ports_list_schema,
                                    _bridge_lag_ports_list_version,
                                    _brcm_sai_bridge_lag_port_t,
                                    bdg_lag_port,
                                    offsetof(_brcm_sai_bridge_lag_port_t, bridge_port_state),
                                    &_bridge_lag_ports);
            break;
        }
        case _BRCM_SAI_LIST_LAG_BP_VLAN_LIST:
        {
            _brcm_sai_lag_bp_vlan_info_t lag_bp_vlan_info;

            DATA_CLEAR(lag_bp_vlan_info, _brcm_sai_lag_bp_vlan_info_t);
            _BRCM_SAI_DM_LIST_ALLOC(_lag_bp_vlan_list_name,
                                    _lag_bp_vlan_list_schema,
                                    _lag_bp_vlan_list_version,
                                    _brcm_sai_lag_bp_vlan_info_t,
                                    lag_bp_vlan_info,
                                    offsetof(_brcm_sai_lag_bp_vlan_info_t, utag),
                                    base);
            break;
        }
        case _BRCM_SAI_LIST_ACL_TBL_GRP_MEMBR:
        {
            _brcm_sai_acl_tbl_grp_membr_tbl_t group_members;

            DATA_CLEAR(group_members, _brcm_sai_acl_tbl_grp_membr_tbl_t);
            _BRCM_SAI_DM_LIST_ALLOC(_acl_table_group_member_table_name,
                                    _acl_table_group_member_table_schema,
                                    _acl_table_group_member_table_version,
                                    _brcm_sai_acl_tbl_grp_membr_tbl_t, group_members,
                                    offsetof(_brcm_sai_acl_tbl_grp_membr_tbl_t, next),
                                    base);
            break;
        }
        default:
            return SAI_STATUS_INVALID_PARAMETER;
    }
    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n", __FUNCTION__);
    return SAI_STATUS_SUCCESS;
}

STATIC sai_status_t
_brcm_sai_list_init_end(_brcm_sai_list_data_type_t type, int id,
                        int entries, void **base, void **end)
{
    char table_name[40];
    unsigned int schema_size, schema_error;
    char schema[SYNCDB_JSON_MAX_SCHEMA_SIZE];
    bool wb = _brcm_sai_switch_wb_state_get();
    unsigned int flags = SYNCDB_TABLE_FLAG_STORABLE | SYNCDB_TABLE_FLAG_NVRAM |
                         SYNCDB_TABLE_FLAG_FILE_LOAD;

    _brcm_sai_table_type_t parent_table = _BRCM_SAI_TABLE_RSVD;
    _brcm_sai_table_data_t parent_data;

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Enter %s, type = %d\n",
                        __FUNCTION__, type);

    switch(type)
    {
        case _BRCM_SAI_LIST_NH_ECMP_INFO:
        {
            _brcm_sai_nh_ecmp_t nh_ecmp_list;

            parent_data.nh_table = NULL;
            DATA_CLEAR(nh_ecmp_list, _brcm_sai_nh_ecmp_t);
            _BRCM_SAI_DM_LIST_ALLOC_END(_nh_ecmp_list_name,
                                        _nh_ecmp_list_schema,
                                        _nh_ecmp_list_version,
                                        _brcm_sai_nh_ecmp_t,
                                        nh_ecmp_list,
                                        offsetof(_brcm_sai_nh_ecmp_t, next),
                                        FALSE,
                                        next,           /* dummy field */
                                        FALSE,
                                        nh_ecmp_list,   /* dummy field */
                                        next,           /* dummy field */
                                        next);          /* dummy field */
            break;
        }
        case _BRCM_SAI_LIST_ECMP_NH:
        {
            _brcm_sai_nh_list_t ecmp_nh_list;

            parent_data.ecmp_table = NULL;
            DATA_CLEAR(ecmp_nh_list, _brcm_sai_nh_list_t);
            _BRCM_SAI_DM_LIST_ALLOC_END(_ecmp_nh_list_name,
                                        _nh_list_schema,
                                        _nh_list_version,
                                        _brcm_sai_nh_list_t,
                                        ecmp_nh_list,
                                        offsetof(_brcm_sai_nh_list_t, next),
                                        FALSE,
                                        next,           /* dummy field */
                                        FALSE,
                                        ecmp_nh_list,   /* dummy field */
                                        next,           /* dummy field */
                                        next);          /* dummy field */
            break;
        }
        case _BRCM_SAI_LIST_ROUTE_LIST_FWD:
        case _BRCM_SAI_LIST_ROUTE_LIST_DROP:
        {
            _brcm_sai_route_list_t route_list;
            _brcm_sai_route_table_t route_table;
            char *_table_name;

            _table_name = _BRCM_SAI_LIST_ROUTE_LIST_FWD ? _route_fwd_list_name : _route_drop_list_name;
            parent_table = _BRCM_SAI_TABLE_ROUTE;
            parent_data.route_table = &route_table;
            DATA_CLEAR(route_list, _brcm_sai_route_list_t);
            DATA_CLEAR(route_table, _brcm_sai_route_table_t);
            _BRCM_SAI_DM_LIST_ALLOC_END(_table_name,
                                        _route_list_schema,
                                        _route_list_version,
                                        _brcm_sai_route_list_t,
                                        route_list,
                                        offsetof(_brcm_sai_route_list_t, multipath),
                                        TRUE,
                                        prev,
                                        TRUE,
                                        route_table,
                                        route,
                                        route);
            break;
        }
        case _BRCM_SAI_LIST_MAC_NBRS:
        {
            _brcm_sai_nbr_info_t nbrs_list;

            parent_data.mac_nbr = NULL;
            DATA_CLEAR(nbrs_list, _brcm_sai_nbr_info_t);
            _BRCM_SAI_DM_LIST_ALLOC_END(_nbr_info_list_name,
                                        _nbr_info_list_schema,
                                        _nbr_info_list_version,
                                        _brcm_sai_nbr_info_t,
                                        nbrs_list,
                                        offsetof(_brcm_sai_nbr_info_t, l3a_intf),
                                        FALSE,
                                        next,           /* dummy field */
                                        FALSE,
                                        nbrs_list,      /* dummy field */
                                        next,           /* dummy field */
                                        next);          /* dummy field */
            break;
        }
        case _BRCM_SAI_LIST_NBR_NHS:
        {
            _brcm_sai_nh_list_t nhs_list;

            parent_data.nbr_table = NULL;
            DATA_CLEAR(nhs_list, _brcm_sai_nh_list_t);
            _BRCM_SAI_DM_LIST_ALLOC_END(_nbr_nh_list_name,
                                        _nh_list_schema,
                                        _nh_list_version,
                                        _brcm_sai_nh_list_t,
                                        nhs_list,
                                        offsetof(_brcm_sai_nh_list_t, next),
                                        FALSE,
                                        next,           /* dummy field */
                                        FALSE,
                                        nhs_list,       /* dummy field */
                                        next,           /* dummy field */
                                        next);          /* dummy field */
            break;
        }
        case _BRCM_SAI_LIST_NBR_BCAST_EOBJS:
        {
            _brcm_sai_egress_objects_t eobjs_list;

            parent_data.nbr_table = NULL;
            DATA_CLEAR(eobjs_list, _brcm_sai_egress_objects_t);
            _BRCM_SAI_DM_LIST_ALLOC_END(_nbr_eobj_list_name,
                                        _nbr_eobj_list_schema,
                                        _nbr_eobj_list_version,
                                        _brcm_sai_egress_objects_t,
                                        eobjs_list,
                                        offsetof(_brcm_sai_egress_objects_t, next),
                                        FALSE,
                                        next,           /* dummy field */
                                        FALSE,
                                        eobjs_list,     /* dummy field */
                                        next,           /* dummy field */
                                        next);          /* dummy field */
            break;
        }
        case _BRCM_SAI_LIST_VLAN_MEMBRS:
        {
            _brcm_sai_vlan_membr_list_t vlan_membrs_list;

            parent_data.nbr_table = NULL;
            DATA_CLEAR(vlan_membrs_list, _brcm_sai_vlan_membr_list_t);
            _BRCM_SAI_DM_LIST_ALLOC_END(_vlan_membrs_list_name,
                                        _vlan_membrs_list_schema,
                                        _vlan_membrs_list_version,
                                        _brcm_sai_vlan_membr_list_t,
                                        vlan_membrs_list,
                                        offsetof(_brcm_sai_vlan_membr_list_t, next),
                                        FALSE,
                                        next,             /* dummy field */
                                        FALSE,
                                        vlan_membrs_list, /* dummy field */
                                        next,             /* dummy field */
                                        next);            /* dummy field */
            break;
        }
        default:
            return SAI_STATUS_INVALID_PARAMETER;
    }
    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n", __FUNCTION__);
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_list_init_end_v2(_brcm_sai_list_data_type_t type, char *str,
                           int entries, void **base, void **end)
{
    char table_name[40];
    unsigned int schema_size, schema_error;
    char schema[SYNCDB_JSON_MAX_SCHEMA_SIZE];
    bool wb = _brcm_sai_switch_wb_state_get();
    unsigned int flags = SYNCDB_TABLE_FLAG_STORABLE | SYNCDB_TABLE_FLAG_NVRAM |
                         SYNCDB_TABLE_FLAG_FILE_LOAD;

    _brcm_sai_table_type_t parent_table = _BRCM_SAI_TABLE_RSVD;
    _brcm_sai_table_data_t parent_data;

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Enter %s, type = %d\n",
                        __FUNCTION__, type);

    switch(type)
    {
        case _BRCM_SAI_LIST_MAC_NBRS:
        {
            _brcm_sai_nbr_info_t nbrs_list;

            parent_data.mac_nbr = NULL;
            DATA_CLEAR(nbrs_list, _brcm_sai_nbr_info_t);
            _BRCM_SAI_DM_LIST_ALLOC_END_STR(_nbr_info_list_name,
                                            _nbr_info_list_schema,
                                            _nbr_info_list_version,
                                            _brcm_sai_nbr_info_t,
                                            nbrs_list,
                                            offsetof(_brcm_sai_nbr_info_t, pa),
                                            FALSE,
                                            next,           /* dummy field */
                                            FALSE,
                                            nbrs_list,      /* dummy field */
                                            next,           /* dummy field */
                                            next);          /* dummy field */
            break;
        }
        default:
            return SAI_STATUS_INVALID_PARAMETER;
    }
    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n", __FUNCTION__);
    return SAI_STATUS_SUCCESS;
}

/* List type data free routine */
sai_status_t
_brcm_sai_list_free(_brcm_sai_list_data_type_t type, int id,
                    int entries, void *base)
{
    char table_name[40];
    unsigned int schema_size, schema_error;
    char schema[SYNCDB_JSON_MAX_SCHEMA_SIZE];
    bool wb = _brcm_sai_switch_wb_state_get();
    unsigned int flags = SYNCDB_TABLE_FLAG_STORABLE | SYNCDB_TABLE_FLAG_NVRAM;

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Enter %s, type = %d\n",
                        __FUNCTION__, type);

    switch(type)
    {
        case _BRCM_SAI_LIST_POLICER_ACTION:
            _BRCM_SAI_DM_LIST_FREE(_policer_action_table_name,
                                   _policer_action_table_schema,
                                   _policer_action_table_version,
                                   _brcm_sai_policer_action_t, pol_id,
                                   sizeof(bcm_policer_t),
                                   next, _brcm_sai_policer_action);
            break;
        case _BRCM_SAI_LIST_POLICER_OID_MAP:
            _BRCM_SAI_DM_LIST_FREE(_policer_oid_map_table_name,
                                   _policer_oid_map_table_schema,
                                   _policer_oid_map_table_version,
                                   _brcm_sai_policer_oid_map_t, oid,
                                   sizeof(sai_object_id_t),
                                   next, base);
            break;
        case _BRCM_SAI_LIST_SCHED_OBJ_MAP:
            _BRCM_SAI_DM_LIST_FREE(_scheduler_obj_table_name,
                                   _scheduler_obj_table_schema,
                                   _scheduler_obj_table_version,
                                   _brcm_sai_scheduler_object_t, object_id,
                                   sizeof(sai_object_id_t),
                                   next, base);
            break;
        case _BRCM_SAI_LIST_UDFG_UDF_MAP:
            _BRCM_SAI_DM_LIST_FREE(_udf_obj_table_name,
                                   _udf_obj_table_schema,
                                   _udf_obj_table_version,
                                   _brcm_sai_udf_object_t, object_id,
                                   sizeof(sai_object_id_t),
                                   next, base);
            break;
        case _BRCM_SAI_LIST_TGRP_TRAP_REF:
            _BRCM_SAI_DM_LIST_FREE(_trap_refs_table_name,
                                   _trap_refs_table_schema,
                                   _trap_refs_table_version,
                                   _brcm_sai_trap_refs_t, trap,
                                   sizeof(int), next, base);
            break;
        case _BRCM_SAI_LIST_ACL_ENTRIES:
            _BRCM_SAI_DM_LIST_FREE(_acl_entry_table_name,
                                   _acl_entry_table_schema,
                                   _acl_entry_table_version,
                                   _brcm_sai_acl_entry_t, id,
                                   sizeof(int), next, base);
            break;
        case _BRCM_SAI_LIST_LAG_ACL_BIND_POINTS:
            _BRCM_SAI_DM_LIST_FREE_STRUCT(_lag_acl_bind_point_table_name,
                                          _acl_bind_point_table_schema,
                                          _acl_bind_point_table_version,
                                          _brcm_sai_acl_bind_point_t,
                                          offsetof(_brcm_sai_acl_bind_point_t, next),
                                          next, base, FALSE,
                                          next); /* dummy field */
            break;
        case _BRCM_SAI_LIST_ACL_TBL_BIND_POINTS:
            _BRCM_SAI_DM_LIST_FREE_STRUCT(_acl_table_bind_point_table_name,
                                         _acl_bind_point_table_schema,
                                         _acl_bind_point_table_version,
                                         _brcm_sai_acl_bind_point_t,
                                         offsetof(_brcm_sai_acl_bind_point_t, next),
                                         next, base, FALSE,
                                         next); /* dummy field */
            break;
        case _BRCM_SAI_LIST_ACL_GRP_BIND_POINTS:
            _BRCM_SAI_DM_LIST_FREE_STRUCT(_acl_group_bind_point_table_name,
                                         _acl_bind_point_table_schema,
                                         _acl_bind_point_table_version,
                                         _brcm_sai_acl_bind_point_t,
                                         offsetof(_brcm_sai_acl_bind_point_t, next),
                                          next, base, FALSE,
                                          next); /* dummy field */
            break;
        case _BRCM_SAI_LIST_REDIRECT:
            _BRCM_SAI_DM_LIST_FREE(_redirect_entry_list_name,
                                   _redirect_entry_list_schema,
                                   _redirect_entry_list_version,
                                   _brcm_sai_redirect_entry_list_t, entry,
                                   offsetof(_brcm_sai_redirect_entry_list_t, next),
                                   next, base);
            break;
        case _BRCM_SAI_LIST_BRIDGE_LAG_PORTS:
            _BRCM_SAI_DM_LIST_FREE(_bridge_lag_ports_list_name,
                                   _bridge_lag_ports_list_schema,
                                   _bridge_lag_ports_list_version,
                                   _brcm_sai_bridge_lag_port_t, oid,
                                    offsetof(_brcm_sai_bridge_lag_port_t, next),
                                    next, _bridge_lag_ports);
            break;
        case _BRCM_SAI_LIST_LAG_BP_VLAN_LIST:
            _BRCM_SAI_DM_LIST_FREE(_lag_bp_vlan_list_name,
                                   _lag_bp_vlan_list_schema,
                                   _lag_bp_vlan_list_version,
                                   _brcm_sai_lag_bp_vlan_info_t, vid,
                                   offsetof(_brcm_sai_lag_bp_vlan_info_t, utag),
                                   next, base);
            break;
        case _BRCM_SAI_LIST_NH_ECMP_INFO:
            _BRCM_SAI_DM_LIST_FREE(_nh_ecmp_list_name,
                                   _nh_ecmp_list_schema,
                                   _nh_ecmp_list_version,
                                   _brcm_sai_nh_ecmp_t, intf,
                                   offsetof(_brcm_sai_nh_ecmp_t, next),
                                   next, base);
            break;
        case _BRCM_SAI_LIST_ECMP_NH:
            _BRCM_SAI_DM_LIST_FREE(_ecmp_nh_list_name,
                                   _nh_list_schema,
                                   _nh_list_version,
                                   _brcm_sai_nh_list_t, nhid,
                                   offsetof(_brcm_sai_nh_list_t, next),
                                   next, base);
            break;
        case _BRCM_SAI_LIST_ROUTE_LIST_FWD:
            _BRCM_SAI_DM_LIST_FREE_STRUCT(_route_fwd_list_name,
                                          _route_list_schema,
                                          _route_list_version,
                                          _brcm_sai_route_list_t,
                                          offsetof(_brcm_sai_route_list_t, multipath),
                                          next, base, TRUE, prev);
            break;
        case _BRCM_SAI_LIST_ROUTE_LIST_DROP:
            _BRCM_SAI_DM_LIST_FREE_STRUCT(_route_drop_list_name,
                                          _route_list_schema,
                                          _route_list_version,
                                          _brcm_sai_route_list_t,
                                          offsetof(_brcm_sai_route_list_t, multipath),
                                          next, base, TRUE, prev);
            break;
        case _BRCM_SAI_LIST_NBR_NHS:
            _BRCM_SAI_DM_LIST_FREE_STRUCT(_nbr_nh_list_name,
                                          _nh_list_schema,
                                          _nh_list_version,
                                          _brcm_sai_nh_list_t,
                                          offsetof(_brcm_sai_nh_list_t, next),
                                          next, base, FALSE,
                                          next); /* dummy field */
            break;
        case _BRCM_SAI_LIST_NBR_BCAST_EOBJS:
            _BRCM_SAI_DM_LIST_FREE_STRUCT(_nbr_eobj_list_name,
                                          _nbr_eobj_list_schema,
                                          _nbr_eobj_list_version,
                                          _brcm_sai_egress_objects_t,
                                          offsetof(_brcm_sai_egress_objects_t, next),
                                          next, base, FALSE,
                                          next); /* dummy field */
            break;
        case _BRCM_SAI_LIST_VLAN_MEMBRS:
            _BRCM_SAI_DM_LIST_FREE_STRUCT(_vlan_membrs_list_name,
                                          _vlan_membrs_list_schema,
                                          _vlan_membrs_list_version,
                                          _brcm_sai_vlan_membr_list_t,
                                          offsetof(_brcm_sai_vlan_membr_list_t, next),
                                          next, base, FALSE,
                                          next); /* dummy field */
            break;
        case _BRCM_SAI_LIST_ACL_TBL_GRP_MEMBR:
            _BRCM_SAI_DM_LIST_FREE_STRUCT(_acl_table_group_member_table_name,
                                         _acl_table_group_member_table_schema,
                                         _acl_table_group_member_table_version,
                                         _brcm_sai_acl_tbl_grp_membr_tbl_t,
                                         offsetof(_brcm_sai_acl_tbl_grp_membr_tbl_t, next),
                                          next, base, FALSE,
                                          next); /* dummy field */
            break;
        default:
            return SAI_STATUS_INVALID_PARAMETER;
    }
    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n", __FUNCTION__);
    return SAI_STATUS_SUCCESS;
}

/* List type data free routine */
sai_status_t
_brcm_sai_list_free_v2(_brcm_sai_list_data_type_t type, char *str,
                       int entries, void *base, void *data)
{
    char table_name[40];
    unsigned int schema_size, schema_error;
    char schema[SYNCDB_JSON_MAX_SCHEMA_SIZE];
    bool wb = _brcm_sai_switch_wb_state_get();
    unsigned int flags = SYNCDB_TABLE_FLAG_STORABLE | SYNCDB_TABLE_FLAG_NVRAM;

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Enter %s, type = %d\n",
                        __FUNCTION__, type);

    switch(type)
    {
        case _BRCM_SAI_LIST_MAC_NBRS:
        {
            _brcm_sai_mac_nbr_info_table_t *mnt = (_brcm_sai_mac_nbr_info_table_t*)data;
            uint8_t *mac = mnt->mac_vid.mac;
            sprintf(str, "%02x%02x%02x%02x%02x%02x_%d",
                    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                    mnt->mac_vid.vid);
            _BRCM_SAI_DM_LIST_FREE_STRUCT_STR(_nbr_info_list_name,
                                              _nbr_info_list_schema,
                                              _nbr_info_list_version,
                                              _brcm_sai_nbr_info_t,
                                              offsetof(_brcm_sai_nbr_info_t, pa),
                                              next, base, FALSE,
                                              next); /* dummy field */
            break;
        }
        default:
            return SAI_STATUS_INVALID_PARAMETER;
    }
    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n", __FUNCTION__);
    return SAI_STATUS_SUCCESS;
}


sai_status_t
_brcm_sai_db_table_create(_brcm_sai_table_type_t table, int entries)
{
    bool wb = _brcm_sai_switch_wb_state_get();
    sai_status_t rv;
    unsigned int schema_size, schema_error,
                 table_schema_size, table_version,
                 key_size, element_size;
    char schema[SYNCDB_JSON_MAX_SCHEMA_SIZE];
    unsigned int flags = SYNCDB_TABLE_FLAG_STORABLE | SYNCDB_TABLE_FLAG_NVRAM;
    syncdbJsonNode_t *table_schema;
    char *table_name;

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Enter %s, table = %d, entries %d\n",
                        __FUNCTION__, table, entries);

    switch (table)
    {
        case _BRCM_SAI_TABLE_MAC_NBR:
            table_schema = _mac_nbr_info_table_schema;
            table_schema_size = sizeof(_mac_nbr_info_table_schema);
            table_name = _mac_nbr_info_table_name;
            table_version = _mac_nbr_info_table_version;
            element_size = sizeof(_brcm_sai_mac_nbr_info_table_t);
            key_size = offsetof(_brcm_sai_mac_nbr_info_table_t, nbrs_count);
            break;
        case _BRCM_SAI_TABLE_NBR_INFO:
            table_schema = _nbr_info_table_schema;
            table_schema_size = sizeof(_nbr_info_table_schema);
            table_name = _nbr_info_table_name;
            table_version = _nbr_info_table_version;
            element_size = sizeof(_brcm_sai_nbr_table_info_t);
            key_size = offsetof(_brcm_sai_nbr_table_info_t, id);
            break;
        case _BRCM_SAI_TABLE_NH:
            table_schema = _nh_table_schema;
            table_schema_size = sizeof(_nh_table_schema);
            table_name = _nh_table_name;
            table_version = _nh_table_version;
            element_size = sizeof(_brcm_sai_nh_table_t);
            key_size = offsetof(_brcm_sai_nh_table_t, fwd_count);
            break;
        case _BRCM_SAI_TABLE_ROUTE:
            table_schema = _route_table_schema;
            table_schema_size = sizeof(_route_table_schema);
            table_name = _route_table_name;
            table_version = _route_table_version;
            element_size = sizeof(_brcm_sai_route_table_t);
            key_size = offsetof(_brcm_sai_route_table_t, nh_obj);
            break;
        case _BRCM_SAI_TABLE_ECMP:
            table_schema = _ecmp_table_schema;
            table_schema_size = sizeof(_ecmp_table_schema);
            table_name = _ecmp_table_name;
            table_version = _ecmp_table_version;
            element_size = sizeof(_brcm_sai_ecmp_table_t);
            key_size = offsetof(_brcm_sai_ecmp_table_t, nh_count);
            break;
        case _BRCM_SAI_TABLE_SCHED_GRP:
            table_schema = _scheduler_group_table_schema;
            table_schema_size = sizeof(_scheduler_group_table_schema);
            table_name = _scheduler_group_table_name;
            table_version = _scheduler_group_table_version;
            element_size = sizeof(_brcm_sai_scheduler_group_t);
            key_size = offsetof(_brcm_sai_scheduler_group_t, scheduler_oid);
            break;
        case _BRCM_SAI_TABLE_UCAST_ARP:
            table_schema = _unicast_arp_table_schema;
            table_schema_size = sizeof(_unicast_arp_table_schema);
            table_name = _unicast_arp_table_name;
            table_version = _unicast_arp_table_version;
            element_size = sizeof(_brcm_sai_unicast_arp_t);
            key_size = offsetof(_brcm_sai_unicast_arp_t, entry_id);
            break;
        case _BRCM_SAI_TABLE_VLAN_MEMBRS:
            table_schema = _vlan_membrs_table_schema;
            table_schema_size = sizeof(_vlan_membrs_table_schema);
            table_name = _vlan_membrs_table_name;
            table_version = _vlan_membrs_table_version;
            element_size = sizeof(_brcm_sai_vlan_membr_info_t);
            key_size = offsetof(_brcm_sai_vlan_membr_info_t, membrs_count);
            break;
        case _BRCM_SAI_TABLE_TNL_MAP_ENT:
            table_schema = _tnl_map_entry_table_schema;
            table_schema_size = sizeof(_tnl_map_entry_table_schema);
            table_name = _tnl_map_entry_table_name;
            table_version = _tnl_map_entry_table_version;
            element_size = sizeof(_brcm_sai_tunnel_map_entry_t);
            key_size = offsetof(_brcm_sai_tunnel_map_entry_t, val);
            break;
        case _BRCM_SAI_TABLE_TNL_NET_VPN_ENT:
            table_schema = _tnl_net_vpn_entry_table_schema;
            table_schema_size = sizeof(_tnl_net_vpn_entry_table_schema);
            table_name = _tnl_net_vpn_entry_table_name;
            table_version = _tnl_net_vpn_entry_table_version;
            element_size = sizeof(_brcm_sai_tunnel_net_vpn_entry_t);
            key_size = offsetof(_brcm_sai_tunnel_net_vpn_entry_t, bc_group);
            break;
        default:
            return SAI_STATUS_INVALID_PARAMETER;
    }

    /* Create DB table */
    rv = syncdbUtilSchemaCreate(table_schema,
                                table_schema_size,
                                schema, sizeof(schema), &schema_size,
                                element_size,
                                &schema_error);
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "Creating table schema", rv);
    if (wb)
    {
        flags |= SYNCDB_TABLE_FLAG_FILE_LOAD;
    }
    rv = syncdbAvlTableCreate(&client_id, table_name,
                              table_version, entries,
                              entries, element_size,
                              key_size,
                              flags, schema, schema_size);
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "Creating table AVL", rv);

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n", __FUNCTION__);

    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_db_table_entry_add(_brcm_sai_table_type_t table,
                             _brcm_sai_table_data_t *data)
{
    char *table_name;
    void *table_data;
    unsigned int element_size;
    int rv;
#ifdef L3_PERF
    struct timeval cur_time1, cur_time2;
    gettimeofday(&cur_time1, NULL);
#endif

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Enter %s, table = %d\n",
                        __FUNCTION__, table);

    switch (table)
    {
        case _BRCM_SAI_TABLE_MAC_NBR:
            table_data = data->mac_nbr;
            table_name = _mac_nbr_info_table_name;
            element_size = sizeof(_brcm_sai_mac_nbr_info_table_t);
            break;
        case _BRCM_SAI_TABLE_NBR_INFO:
            table_data = data->nbr_table;
            table_name = _nbr_info_table_name;
            element_size = sizeof(_brcm_sai_nbr_table_info_t);
            break;
        case _BRCM_SAI_TABLE_NH:
            table_data = data->nh_table;
            table_name = _nh_table_name;
            element_size = sizeof(_brcm_sai_nh_table_t);
            break;
        case _BRCM_SAI_TABLE_ROUTE:
            table_data = data->route_table;
            table_name = _route_table_name;
            element_size = sizeof(_brcm_sai_route_table_t);
            /* To avoid an all zero key, bump up the family to non-zero */
            if (data->route_table->route.addr_family == 0)
            {
                data->route_table->route.addr_family = 2;
            }
            break;
        case _BRCM_SAI_TABLE_ECMP:
            table_data = data->ecmp_table;
            table_name = _ecmp_table_name;
            element_size = sizeof(_brcm_sai_ecmp_table_t);
            break;
        case _BRCM_SAI_TABLE_SCHED_GRP:
            table_data = data->sched_group;
            table_name = _scheduler_group_table_name;
            element_size = sizeof(_brcm_sai_scheduler_group_t);
            break;
        case _BRCM_SAI_TABLE_UCAST_ARP:
            table_data = data->ucast_arp;
            table_name = _unicast_arp_table_name;
            element_size = sizeof(_brcm_sai_unicast_arp_t);
            break;
        case _BRCM_SAI_TABLE_VLAN_MEMBRS:
            table_data = data->vlan_membrs;
            table_name = _vlan_membrs_table_name;
            element_size = sizeof(_brcm_sai_vlan_membr_info_t);
            break;
        case _BRCM_SAI_TABLE_TNL_MAP_ENT:
            table_data = data->tnl_map_entry;
            table_name = _tnl_map_entry_table_name;
            element_size = sizeof(_brcm_sai_tunnel_map_entry_t);
            break;
        case _BRCM_SAI_TABLE_TNL_NET_VPN_ENT:
            table_data = data->tnl_net_vpn_entry;
            table_name = _tnl_net_vpn_entry_table_name;
            element_size = sizeof(_brcm_sai_tunnel_net_vpn_entry_t);
            break;
        default:
            return SAI_STATUS_INVALID_PARAMETER;
    }

    rv = syncdbInsert(&client_id, table_name, table_data,
                      element_size);
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "DB table insert", rv);

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n", __FUNCTION__);
#ifdef L3_PERF
    gettimeofday(&cur_time2, NULL);
    avl_add_usecs += (cur_time2.tv_usec >= cur_time1.tv_usec) ?
        (cur_time2.tv_usec - cur_time1.tv_usec) :
        (1000000 - cur_time1.tv_usec) + cur_time2.tv_usec;
#endif
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_db_table_entry_delete(_brcm_sai_table_type_t table,
                                _brcm_sai_table_data_t *data)
{
    char *table_name;
    void *table_data;
    unsigned int element_size;
    int rv;
#ifdef L3_PERF
    struct timeval cur_time1, cur_time2;
    gettimeofday(&cur_time1, NULL);
#endif

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Enter %s, table = %d\n",
                        __FUNCTION__, table);

    switch (table)
    {
        case _BRCM_SAI_TABLE_MAC_NBR:
            table_data = data->mac_nbr;
            table_name = _mac_nbr_info_table_name;
            element_size = sizeof(_brcm_sai_mac_nbr_info_table_t);
            break;
        case _BRCM_SAI_TABLE_NBR_INFO:
            table_data = data->nbr_table;
            table_name = _nbr_info_table_name;
            element_size = sizeof(_brcm_sai_nbr_table_info_t);
            break;
        case _BRCM_SAI_TABLE_NH:
            table_data = data->nh_table;
            table_name = _nh_table_name;
            element_size = sizeof(_brcm_sai_nh_table_t);
            break;
        case _BRCM_SAI_TABLE_ROUTE:
            table_data = data->route_table;
            table_name = _route_table_name;
            element_size = sizeof(_brcm_sai_route_table_t);
            /* addr family value has been bumped up */
            if (data->route_table->route.addr_family == 0)
            {
                data->route_table->route.addr_family = 2;
            }
            break;
        case _BRCM_SAI_TABLE_ECMP:
            table_data = data->ecmp_table;
            table_name = _ecmp_table_name;
            element_size = sizeof(_brcm_sai_ecmp_table_t);
            break;
        case _BRCM_SAI_TABLE_SCHED_GRP:
            table_data = data->sched_group;
            table_name = _scheduler_group_table_name;
            element_size = sizeof(_brcm_sai_scheduler_group_t);
            break;
        case _BRCM_SAI_TABLE_UCAST_ARP:
            table_data = data->ucast_arp;
            table_name = _unicast_arp_table_name;
            element_size = sizeof(_brcm_sai_unicast_arp_t);
            break;
        case _BRCM_SAI_TABLE_VLAN_MEMBRS:
            table_data = data->vlan_membrs;
            table_name = _vlan_membrs_table_name;
            element_size = sizeof(_brcm_sai_vlan_membr_info_t);
            break;
        case _BRCM_SAI_TABLE_TNL_MAP_ENT:
            table_data = data->tnl_map_entry;
            table_name = _tnl_map_entry_table_name;
            element_size = sizeof(_brcm_sai_tunnel_map_entry_t);
            break;
        case _BRCM_SAI_TABLE_TNL_NET_VPN_ENT:
            table_data = data->tnl_net_vpn_entry;
            table_name = _tnl_net_vpn_entry_table_name;
            element_size = sizeof(_brcm_sai_tunnel_net_vpn_entry_t);
            break;
        default:
            return SAI_STATUS_INVALID_PARAMETER;
    }

    rv = syncdbDelete(&client_id, table_name, table_data,
                      element_size);
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "DB table delete", rv);

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n", __FUNCTION__);
#ifdef L3_PERF
    gettimeofday(&cur_time2, NULL);
    avl_delete_usecs += (cur_time2.tv_usec >= cur_time1.tv_usec) ?
        (cur_time2.tv_usec - cur_time1.tv_usec) :
        (1000000 - cur_time1.tv_usec) + cur_time2.tv_usec;
#endif
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_db_table_entry_lookup(_brcm_sai_table_type_t table,
                                _brcm_sai_table_data_t *data)
{
    char *table_name;
    void *table_data;
    unsigned int element_size;
    int rv;
#ifdef L3_PERF
    struct timeval cur_time1, cur_time2;
    gettimeofday(&cur_time1, NULL);
#endif

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Enter %s, table = %d\n",
                        __FUNCTION__, table);

    switch (table)
    {
        case _BRCM_SAI_TABLE_MAC_NBR:
            table_data = data->mac_nbr;
            table_name = _mac_nbr_info_table_name;
            element_size = sizeof(_brcm_sai_mac_nbr_info_table_t);
            break;
        case _BRCM_SAI_TABLE_NBR_INFO:
            table_data = data->nbr_table;
            table_name = _nbr_info_table_name;
            element_size = sizeof(_brcm_sai_nbr_table_info_t);
            break;
        case _BRCM_SAI_TABLE_NH:
            table_data = data->nh_table;
            table_name = _nh_table_name;
            element_size = sizeof(_brcm_sai_nh_table_t);
            break;
        case _BRCM_SAI_TABLE_ROUTE:
            table_data = data->route_table;
            table_name = _route_table_name;
            element_size = sizeof(_brcm_sai_route_table_t);
            /* To avoid an all zero key, bump up the family to non-zero */
            if (data->route_table->route.addr_family == 0)
            {
                data->route_table->route.addr_family = 2;
            }
            break;
        case _BRCM_SAI_TABLE_ECMP:
            table_data = data->ecmp_table;
            table_name = _ecmp_table_name;
            element_size = sizeof(_brcm_sai_ecmp_table_t);
            break;
        case _BRCM_SAI_TABLE_SCHED_GRP:
            table_data = data->sched_group;
            table_name = _scheduler_group_table_name;
            element_size = sizeof(_brcm_sai_scheduler_group_t);
            break;
        case _BRCM_SAI_TABLE_UCAST_ARP:
            table_data = data->ucast_arp;
            table_name = _unicast_arp_table_name;
            element_size = sizeof(_brcm_sai_unicast_arp_t);
            break;
        case _BRCM_SAI_TABLE_VLAN_MEMBRS:
            table_data = data->vlan_membrs;
            table_name = _vlan_membrs_table_name;
            element_size = sizeof(_brcm_sai_vlan_membr_info_t);
            break;
        case _BRCM_SAI_TABLE_TNL_MAP_ENT:
            table_data = data->tnl_map_entry;
            table_name = _tnl_map_entry_table_name;
            element_size = sizeof(_brcm_sai_tunnel_map_entry_t);
            break;
        case _BRCM_SAI_TABLE_TNL_NET_VPN_ENT:
            table_data = data->tnl_net_vpn_entry;
            table_name = _tnl_net_vpn_entry_table_name;
            element_size = sizeof(_brcm_sai_tunnel_net_vpn_entry_t);
            break;
        default:
            return SAI_STATUS_INVALID_PARAMETER;
    }

    /* Get data from DB */
    rv = syncdbGet(&client_id, table_name,
                   table_data, element_size, 0);
    if (SYNCDB_NOT_FOUND == rv)
    {
        return SAI_STATUS_ITEM_NOT_FOUND;
    }

    /* Restore the value of addr_family back to sai enum */
    if  ((table == _BRCM_SAI_TABLE_ROUTE) &&
         (data->route_table->route.addr_family == 2))
    {
        data->route_table->route.addr_family = 0;
    }

    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "table data DB record get", rv);

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n", __FUNCTION__);
#ifdef L3_PERF
    gettimeofday(&cur_time2, NULL);
    avl_delete_usecs += (cur_time2.tv_usec >= cur_time1.tv_usec) ?
        (cur_time2.tv_usec - cur_time1.tv_usec) :
        (1000000 - cur_time1.tv_usec) + cur_time2.tv_usec;
#endif
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_db_table_entry_getnext(_brcm_sai_table_type_t table,
                                 _brcm_sai_table_data_t *data)
{
    char *table_name;
    void *table_data;
    unsigned int element_size;
    int rv;

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Enter %s, table = %d\n",
                        __FUNCTION__, table);

    switch (table)
    {
        case _BRCM_SAI_TABLE_MAC_NBR:
            table_data = data->mac_nbr;
            table_name = _mac_nbr_info_table_name;
            element_size = sizeof(_brcm_sai_mac_nbr_info_table_t);
            break;
        case _BRCM_SAI_TABLE_NBR_INFO:
            table_data = data->nbr_table;
            table_name = _nbr_info_table_name;
            element_size = sizeof(_brcm_sai_nbr_table_info_t);
            break;
        case _BRCM_SAI_TABLE_NH:
            table_data = data->nh_table;
            table_name = _nh_table_name;
            element_size = sizeof(_brcm_sai_nh_table_t);
            break;
        case _BRCM_SAI_TABLE_ROUTE:
            table_data = data->route_table;
            table_name = _route_table_name;
            element_size = sizeof(_brcm_sai_route_table_t);
            break;
        case _BRCM_SAI_TABLE_ECMP:
            table_data = data->ecmp_table;
            table_name = _ecmp_table_name;
            element_size = sizeof(_brcm_sai_ecmp_table_t);
            break;
        case _BRCM_SAI_TABLE_SCHED_GRP:
            table_data = data->sched_group;
            table_name = _scheduler_group_table_name;
            element_size = sizeof(_brcm_sai_scheduler_group_t);
            break;
        case _BRCM_SAI_TABLE_UCAST_ARP:
            table_data = data->ucast_arp;
            table_name = _unicast_arp_table_name;
            element_size = sizeof(_brcm_sai_unicast_arp_t);
            break;
        case _BRCM_SAI_TABLE_VLAN_MEMBRS:
            table_data = data->vlan_membrs;
            table_name = _vlan_membrs_table_name;
            element_size = sizeof(_brcm_sai_vlan_membr_info_t);
            break;
        case _BRCM_SAI_TABLE_TNL_MAP_ENT:
            table_data = data->tnl_map_entry;
            table_name = _tnl_map_entry_table_name;
            element_size = sizeof(_brcm_sai_tunnel_map_entry_t);
            break;
        case _BRCM_SAI_TABLE_TNL_NET_VPN_ENT:
            table_data = data->tnl_net_vpn_entry;
            table_name = _tnl_net_vpn_entry_table_name;
            element_size = sizeof(_brcm_sai_tunnel_net_vpn_entry_t);
            break;
        default:
            return SAI_STATUS_INVALID_PARAMETER;
    }

    /* Get data from DB */
    rv = syncdbGetNext(&client_id, table_name,
                       table_data, element_size, 0);
    if (SYNCDB_NOT_FOUND == rv)
    {
        return SAI_STATUS_ITEM_NOT_FOUND;
    }

    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "table data DB record get", rv);

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n", __FUNCTION__);
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_db_table_entry_restore(_brcm_sai_table_type_t table,
                                 _brcm_sai_table_data_t *data,
                                 void *ptr)
{
    char *table_name;
    void *table_data;
    unsigned int element_size;
    int rv;

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Enter %s, table = %d\n",
                        __FUNCTION__, table);

    switch (table)
    {
        case _BRCM_SAI_TABLE_ROUTE:
            table_data = data->route_table;
            table_name = _route_table_name;
            element_size = sizeof(_brcm_sai_route_table_t);
            /* To avoid an all zero key, bump up the family to non-zero */
            if (data->route_table->route.addr_family == 0)
            {
                data->route_table->route.addr_family = 2;
            }
            break;
        default:
            return SAI_STATUS_INVALID_PARAMETER;
    }

    /* Get data from DB */
    rv = syncdbGet(&client_id, table_name,
                   table_data, element_size, 0);
    if (SYNCDB_NOT_FOUND == rv)
    {
        return SAI_STATUS_ITEM_NOT_FOUND;
    }

    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "table data DB record get", rv);

    switch (table)
    {
        case _BRCM_SAI_TABLE_ROUTE:
            BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG,
                              "Restoring route_table.ptr %p\n", ptr);
            data->route_table->ptr = ptr;
            break;
        default:
            return SAI_STATUS_INVALID_PARAMETER;
    }

    /* Update DB */
    rv = syncdbInsert(&client_id, table_name, table_data,
                      element_size);
    if (SYNCDB_NOT_FOUND == rv)
    {
        return SAI_STATUS_ITEM_NOT_FOUND;
    }

    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "table data DB record insert", rv);

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n", __FUNCTION__);
    return SAI_STATUS_SUCCESS;
}

/* Initialize a list attached to a tables node */
sai_status_t
_brcm_sai_db_table_node_list_init(_brcm_sai_table_type_t table,
                                  _brcm_sai_list_data_type_t list)
{
    sai_status_t rv;
    int pend, tsize;
    char *table_name, *list_name;
    _brcm_sai_nh_table_t nh_table;
    _brcm_sai_ecmp_table_t ecmp_table;
    _brcm_sai_nbr_table_info_t nbr_table;
    _brcm_sai_vlan_membr_info_t vlan_membr_table;
    void *table_data, *list_start, *list_end;
    int id, count = 0, zcount = 0, ref_count;

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Enter %s, table = %d, list = %d\n",
                        __FUNCTION__, table, list);

    switch (table)
    {
        case _BRCM_SAI_TABLE_ECMP:
            table_name = _ecmp_table_name;
            table_data = &ecmp_table;
            PTR_CLEAR(table_data, _brcm_sai_ecmp_table_t);
            tsize = sizeof(_brcm_sai_ecmp_table_t);
            break;
        case _BRCM_SAI_TABLE_NH:
            table_name = _nh_table_name;
            table_data = &nh_table;
            PTR_CLEAR(table_data, _brcm_sai_nh_table_t);
            tsize = sizeof(_brcm_sai_nh_table_t);
            break;
        case _BRCM_SAI_TABLE_NBR_INFO:
            table_name = _nbr_info_table_name;
            table_data = &nbr_table;
            PTR_CLEAR(table_data, _brcm_sai_nbr_table_info_t);
            tsize = sizeof(_brcm_sai_nbr_table_info_t);
            break;
        case _BRCM_SAI_TABLE_VLAN_MEMBRS:
            table_name = _vlan_membrs_table_name;
            table_data = &vlan_membr_table;
            PTR_CLEAR(table_data, _brcm_sai_vlan_membr_info_t);
            tsize = sizeof(_brcm_sai_vlan_membr_info_t);
            break;
        default:
            return SAI_STATUS_INVALID_PARAMETER;
    }

    switch (list)
    {
        case _BRCM_SAI_LIST_ECMP_NH:
            list_name = _ecmp_nh_list_name;
            break;
        case _BRCM_SAI_LIST_ROUTE_LIST_FWD:
            list_name = _route_fwd_list_name;
            break;
        case _BRCM_SAI_LIST_ROUTE_LIST_DROP:
            list_name = _route_drop_list_name;
            break;
        case _BRCM_SAI_LIST_NH_ECMP_INFO:
            list_name = _nh_ecmp_list_name;
            break;
        case _BRCM_SAI_LIST_NBR_NHS:
            list_name = _nbr_nh_list_name;
            break;
        case _BRCM_SAI_LIST_NBR_BCAST_EOBJS:
            list_name = _nbr_eobj_list_name;
            break;
        case _BRCM_SAI_LIST_VLAN_MEMBRS:
            list_name = _vlan_membrs_list_name;
            break;
        default:
            return SAI_STATUS_INVALID_PARAMETER;
    }

    do
    {
        rv = syncdbGetNext(&client_id, table_name, table_data,
                           tsize, &pend);
        if (SYNCDB_OK == rv)
        {
            switch (table)
            {
                case _BRCM_SAI_TABLE_ECMP:
                    id = ecmp_table.intf;
                    break;
                case _BRCM_SAI_TABLE_NH:
                    id = nh_table.nhid;
                    break;
                case _BRCM_SAI_TABLE_NBR_INFO:
                    id = nbr_table.id;
                    break;
                case _BRCM_SAI_TABLE_VLAN_MEMBRS:
                    id = vlan_membr_table.vid;
                    break;
                default:
                    return SAI_STATUS_INVALID_PARAMETER;
            }
            switch (list)
            {
                case _BRCM_SAI_LIST_ECMP_NH:
                    ref_count = ecmp_table.nh_count;
                    list_start = ecmp_table.nh_list;
                    list_end = ecmp_table.end;
                    break;
                case _BRCM_SAI_LIST_ROUTE_LIST_FWD:
                    ref_count = nh_table.fwd_count;
                    list_start = nh_table.fwd;
                    list_end = nh_table.fwd_end;
                    break;
                case _BRCM_SAI_LIST_ROUTE_LIST_DROP:
                    ref_count = nh_table.drop_count;
                    list_start = nh_table.drop;
                    list_end = nh_table.drop_end;
                    break;
                case _BRCM_SAI_LIST_NH_ECMP_INFO:
                    ref_count = nh_table.ecmp_count;
                    list_start = nh_table.ecmp;
                    list_end = nh_table.ecmp_end;
                    break;
                case _BRCM_SAI_LIST_NBR_NHS:
                    ref_count = nbr_table.nh_count;
                    list_start = nbr_table.nhs;
                    list_end = nbr_table.nhs_end;
                    break;
                case _BRCM_SAI_LIST_NBR_BCAST_EOBJS:
                    ref_count = nbr_table.eobj_count;
                    list_start = nbr_table.eobjs;
                    list_end = nbr_table.eobjs_end;
                    break;
                case _BRCM_SAI_LIST_VLAN_MEMBRS:
                    ref_count = vlan_membr_table.membrs_count;
                    list_start = vlan_membr_table.membrs;
                    list_end = vlan_membr_table.membrs_end;
                    break;
                default:
                    return SAI_STATUS_INVALID_PARAMETER;
            }
            if (ref_count)
            {
                rv = _brcm_sai_list_init_end(list, id, ref_count,
                                             (void**)&list_start, (void**)&list_end);
                BRCM_SAI_RV_CHK(SAI_API_SWITCH, "table node list init", rv);
                BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG,
                            "Created table node list: %s%d with %d elements\n",
                            list_name, id, ref_count);
                switch (list)
                {
                    case _BRCM_SAI_LIST_ECMP_NH:
                        ecmp_table.nh_list = list_start;
                        ecmp_table.end = list_end;
                        break;
                    case _BRCM_SAI_LIST_ROUTE_LIST_FWD:
                        nh_table.fwd = list_start;
                        nh_table.fwd_end = list_end;
                        break;
                    case _BRCM_SAI_LIST_ROUTE_LIST_DROP:
                        nh_table.drop = list_start;
                        nh_table.drop_end = list_end;
                        break;
                    case _BRCM_SAI_LIST_NH_ECMP_INFO:
                        nh_table.ecmp = list_start;
                        nh_table.ecmp_end = list_end;
                        break;
                    case _BRCM_SAI_LIST_NBR_NHS:
                        nbr_table.nhs = list_start;
                        nbr_table.nhs_end = list_end;
                        break;
                    case _BRCM_SAI_LIST_NBR_BCAST_EOBJS:
                        nbr_table.eobjs = list_start;
                        nbr_table.eobjs_end = list_end;
                        break;
                    case _BRCM_SAI_LIST_VLAN_MEMBRS:
                        vlan_membr_table.membrs = list_start;
                        vlan_membr_table.membrs_end = list_end;
                        break;
                    default:
                        return SAI_STATUS_INVALID_PARAMETER;
                }
                rv = syncdbInsert(&client_id, table_name, table_data, tsize);
                BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                                    "DB table insert", rv);
                count++;
            }
            else
            {
                zcount++;
            }
        }
    } while(SYNCDB_OK == rv);
    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG,
                        "Empty nodes: %d. Retrieved table node lists count: %d\n",
                        zcount, count);

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n", __FUNCTION__);
    return SAI_STATUS_SUCCESS;
}

/* Initialize a list attached to a tables node */
sai_status_t
_brcm_sai_db_table_node_list_init_v2(_brcm_sai_table_type_t table,
                                     _brcm_sai_list_data_type_t list)
{
    sai_status_t rv;
    int pend, tsize;
    char *table_name, *list_name, list_name_str[16];
    _brcm_sai_mac_nbr_info_table_t mac_nbr_table;
    void *table_data, *list_start, *list_end;
    int count = 0, zcount = 0, ref_count;

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Enter %s, table = %d, list = %d\n",
                        __FUNCTION__, table, list);

    switch (table)
    {
        case _BRCM_SAI_TABLE_MAC_NBR:
            table_name = _mac_nbr_info_table_name ;
            table_data = &mac_nbr_table;
            PTR_CLEAR(table_data, _brcm_sai_mac_nbr_info_table_t);
            tsize = sizeof(_brcm_sai_mac_nbr_info_table_t);
            break;
        default:
            return SAI_STATUS_INVALID_PARAMETER;
    }

    switch (list)
    {
        case _BRCM_SAI_LIST_MAC_NBRS:
            list_name = _nbr_info_list_name;
            break;
        default:
            return SAI_STATUS_INVALID_PARAMETER;
    }

    do
    {
        rv = syncdbGetNext(&client_id, table_name, table_data,
                           tsize, &pend);
        if (SYNCDB_OK == rv)
        {
            switch (table)
            {
                    break;
                case _BRCM_SAI_TABLE_MAC_NBR:
                {
                    uint8_t *mac = mac_nbr_table.mac_vid.mac;
                    sprintf(list_name_str, "%02x%02x%02x%02x%02x%02x_%d",
                            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                            mac_nbr_table.mac_vid.vid);
                    break;
                }
                default:
                    return SAI_STATUS_INVALID_PARAMETER;
            }
            switch (list)
            {
                case _BRCM_SAI_LIST_MAC_NBRS:
                    ref_count = mac_nbr_table.nbrs_count;
                    list_start = mac_nbr_table.nbrs;
                    list_end = mac_nbr_table.nbrs_end;
                    break;
                default:
                    return SAI_STATUS_INVALID_PARAMETER;
            }
            if (ref_count)
            {
                rv = _brcm_sai_list_init_end_v2(list, list_name_str, ref_count,
                                                (void**)&list_start, (void**)&list_end);
                BRCM_SAI_RV_CHK(SAI_API_SWITCH, "table node list init", rv);
                BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG,
                            "Created table node list: %s%s with %d elements\n",
                            list_name, list_name_str, ref_count);
                switch (list)
                {
                    case _BRCM_SAI_LIST_MAC_NBRS:
                        mac_nbr_table.nbrs = list_start;
                        mac_nbr_table.nbrs_end = list_end;
                        break;
                    default:
                        return SAI_STATUS_INVALID_PARAMETER;
                }
                rv = syncdbInsert(&client_id, table_name, table_data, tsize);
                BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                                    "DB table insert", rv);
                count++;
            }
            else
            {
                zcount++;
            }
        }
    } while(SYNCDB_OK == rv);
    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG,
                        "Empty nodes: %d. Retrieved table node lists count: %d\n",
                        zcount, count);

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n", __FUNCTION__);
    return SAI_STATUS_SUCCESS;
}

/* De-initialize a list attached to a tables node */
sai_status_t
_brcm_sai_db_table_node_list_free(_brcm_sai_table_type_t table,
                                  _brcm_sai_list_data_type_t list)
{
    sai_status_t rv;
    int pend, tsize;
    int count = 0, zcount = 0;
    char *table_name, *list_name, list_name_str[16];

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Enter %s, table = %d, list = %d\n",
                        __FUNCTION__, table, list);

    switch (list)
    {
        case _BRCM_SAI_LIST_ECMP_NH:
            list_name = _ecmp_nh_list_name;
            break;
        case _BRCM_SAI_LIST_ROUTE_LIST_FWD:
            list_name = _route_fwd_list_name;
            break;
        case _BRCM_SAI_LIST_ROUTE_LIST_DROP:
            list_name = _route_drop_list_name;
            break;
        case _BRCM_SAI_LIST_NH_ECMP_INFO:
            list_name = _nh_ecmp_list_name;
            break;
        case _BRCM_SAI_LIST_MAC_NBRS:
            list_name = _nbr_info_list_name;
            break;
        case _BRCM_SAI_LIST_NBR_NHS:
            list_name = _nbr_nh_list_name;
            break;
        case _BRCM_SAI_LIST_NBR_BCAST_EOBJS:
            list_name = _nbr_eobj_list_name;
            break;
        case _BRCM_SAI_LIST_VLAN_MEMBRS:
            list_name = _vlan_membrs_list_name;
            break;
        default:
            return SAI_STATUS_INVALID_PARAMETER;
    }
    switch (table)
    {
        case _BRCM_SAI_TABLE_ECMP:
        {
            _brcm_sai_ecmp_table_t data;

            table_name = _ecmp_table_name;
            tsize = sizeof(_brcm_sai_ecmp_table_t);
            DATA_CLEAR(data, _brcm_sai_ecmp_table_t);
            _BRCM_SAI_DB_TABLE_NODE_LIST_FREE(intf, nh_count, nh_list);
            break;
        }
        case _BRCM_SAI_TABLE_NH:
        {
            _brcm_sai_nh_table_t data;

            table_name = _nh_table_name;
            tsize = sizeof(_brcm_sai_nh_table_t);
            DATA_CLEAR(data, _brcm_sai_nh_table_t);
            if (_BRCM_SAI_LIST_ROUTE_LIST_DROP == list)
            {
                _BRCM_SAI_DB_TABLE_NODE_LIST_FREE(nhid, drop_count, drop);
            }
            else if (_BRCM_SAI_LIST_ROUTE_LIST_FWD == list)
            {
                _BRCM_SAI_DB_TABLE_NODE_LIST_FREE(nhid, fwd_count, fwd);
            }
            else
            {
                _BRCM_SAI_DB_TABLE_NODE_LIST_FREE(nhid, ecmp_count, ecmp);
            }
            break;
        }
        case _BRCM_SAI_TABLE_MAC_NBR:
        {
            _brcm_sai_mac_nbr_info_table_t data;

            table_name = _mac_nbr_info_table_name;
            tsize = sizeof(_brcm_sai_mac_nbr_info_table_t);
            DATA_CLEAR(data, _brcm_sai_mac_nbr_info_table_t);
            _BRCM_SAI_DB_TABLE_NODE_LIST_FREE_STR(&list_name_str[0], nbrs_count, nbrs);
            break;
        }
        case _BRCM_SAI_TABLE_NBR_INFO:
        {
            _brcm_sai_nbr_table_info_t data;

            table_name = _nbr_info_table_name;
            tsize = sizeof(_brcm_sai_nbr_table_info_t);
            DATA_CLEAR(data, _brcm_sai_nbr_table_info_t);
            if (_BRCM_SAI_LIST_NBR_NHS == list)
            {
                _BRCM_SAI_DB_TABLE_NODE_LIST_FREE(id, nh_count, nhs);
            }
            else
            {
                _BRCM_SAI_DB_TABLE_NODE_LIST_FREE(id, eobj_count, eobjs);
            }
            break;
        }
        case _BRCM_SAI_TABLE_VLAN_MEMBRS:
        {
            _brcm_sai_vlan_membr_info_t data;

            table_name = _vlan_membrs_table_name;
            tsize = sizeof(_brcm_sai_vlan_membr_info_t);
            DATA_CLEAR(data, _brcm_sai_vlan_membr_info_t);
            _BRCM_SAI_DB_TABLE_NODE_LIST_FREE(vid, membrs_count, membrs);
            break;
        }
        default:
            return SAI_STATUS_INVALID_PARAMETER;
    }

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG,
                        "Empty nodes: %d. Freed table node lists count: %d\n",
                        zcount, count);

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n", __FUNCTION__);
    return SAI_STATUS_SUCCESS;
}

STATIC void
_brcm_sai_sig_handler(int sig)
{
    pid_t _pid;
    int status;

    while (1)
    {
        _pid = waitpid(-1, &status, WNOHANG);
        if (-1 == _pid)
        {
            if (EINTR == errno)
            {
                continue;
            }
            break;
        }
        else if (0 == _pid)
        {
            break;
        }
        if (syncdb_state)
        {
            syncdb_state = FALSE;
            exit(0);
        }
    }
}

/* DM DB process */
STATIC void
*_brcm_sai_db_process(void *arg)
{
    if (0 == db_pid)
    {
        syncdb_main((char*)arg);
    }
    return NULL;
}

/* DM initializer routine */
sai_status_t
_brcm_sai_dm_init(bool wb, char *path)
{
    sai_status_t rv;
    struct sigaction siga;

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Enter %s\n", __FUNCTION__);

    sigemptyset(&siga.sa_mask);
    siga.sa_flags = 0;
    siga.sa_handler = _brcm_sai_sig_handler;
    sigaction(SIGCHLD, &siga, NULL);

    sai_pid = getpid();
    db_pid = fork();
    if (-1 == db_pid)
    {
        perror(0);
        exit(1);
    }
    _brcm_sai_db_process(path);
    if (0 == db_pid)
    {
        exit(0);
    }
    else
    {
        syncdb_state = TRUE;
        BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI (%d) created dbprocess with id: %d\n",
                            sai_pid, db_pid);
        sleep(2);
    }

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "Calling syncdbClientRegister()...\n");
    rv = syncdbClientRegister ("My AVL Client", &client_id, path);
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "Registering with DB", rv);
    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI DB client_id = %d, cmd_socket = %d, "
                        "notify_socket = %d\n",
                        client_id.client_id, client_id.cmd_socket,
                        client_id.notify_socket);

    rv = _brcm_sai_global_data_init();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "initializing global state", rv);
    rv = _brcm_sai_alloc_hostif();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "initializing hostif state", rv);
    rv = _brcm_sai_alloc_switch_gport();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "initializing switch gport state", rv);
    rv = _brcm_sai_alloc_vlan();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "initializing vlan module/state", rv);
    rv = _brcm_sai_alloc_bridge_info();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "initializing bridge info state", rv);
    rv = _brcm_sai_alloc_port_info();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "initializing port info state", rv);
    rv = _brcm_sai_alloc_lag();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "initializing lag info state", rv);
    rv = _brcm_sai_alloc_mirror();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "initializing mirror info state", rv);
    rv = _brcm_sai_alloc_vrf();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "initializing vrf state", rv);
    rv = _brcm_sai_alloc_rif();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "initializing rif state", rv);
    rv = _brcm_sai_alloc_wred();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "initializing wred state", rv);
    rv = _brcm_sai_alloc_queue_wred();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "initializing queue wred state", rv);
    rv = _brcm_sai_alloc_qos_map();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "initializing qos map state", rv);
    rv = _brcm_sai_alloc_buff_pools();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "initializing buff pools state", rv);
    rv = _brcm_sai_alloc_tunnel();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "initializing tunnel state", rv);
    rv = _brcm_sai_alloc_sched_group_info();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "initializing scheduler group state", rv);
    rv = _brcm_sai_alloc_sched();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "initializing sched profile state", rv);
    /* Need Route Table initialized before NH as NH table
     * Route List depends on it for WB.
     */
    rv = _brcm_sai_alloc_route_info();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "initializing route info state", rv);
    rv = _brcm_sai_alloc_nh_info();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "initializing nh info state", rv);
    rv = _brcm_sai_alloc_nbr();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "initializing nbr info state", rv);
    rv = _brcm_sai_alloc_nhg_info();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "initializing nhg info state", rv);
    rv = _brcm_sai_alloc_udf();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "initializing udf hash state", rv);
    rv = _brcm_sai_alloc_hash();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "initializing hash state", rv);
    rv = _brcm_sai_alloc_acl();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "initializing acl state", rv);
    if (wb)
    {
        rv = _brcm_sai_alloc_sched_prof_refs();
        BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                            "initializing sched profile refs state", rv);
        rv = _brcm_sai_alloc_policer();
        BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                            "initializing policer state", rv);
    }
    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n", __FUNCTION__);
    return SAI_STATUS_SUCCESS;
}

/* DM termination routine */
sai_status_t
_brcm_sai_dm_fini(bool wb)
{
    sai_status_t rv;

    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Enter %s\n",
                        __FUNCTION__);

    if (wb)
    {
        /* Store global data to DB */
        rv = _brcm_sai_global_data_save();
        BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                            "saving global data to DB", rv);
        rv = _brcm_sai_vlan_save();
        BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                            "saving vlan data to DB", rv);
    }
    rv = _brcm_sai_free_vrf();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "freeing vrf info", rv);
    rv = _brcm_sai_free_rif();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "freeing rif info", rv);
    rv = _brcm_sai_free_wred();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "freeing wred profile", rv);
    rv = _brcm_sai_free_hostif();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "freeing hostif maps", rv);
    rv = _brcm_sai_free_bridge_info();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "freeing bridge info", rv);
    rv = _brcm_sai_free_port_info();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "freeing port info", rv);
    rv = _brcm_sai_free_lag();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "freeing lag info", rv);
    rv = _brcm_sai_free_mirror();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "freeing mirror info", rv);
    rv = _brcm_sai_free_vlan();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "freeing vlan info", rv);
    rv = _brcm_sai_free_queue_wred();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "freeing queue wred info", rv);
    rv = _brcm_sai_free_switch_gport();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "freeing switch gport info", rv);
    rv = _brcm_sai_free_buff_pools();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "freeing buff pools info", rv);
    rv = _brcm_sai_free_sched_prof_refs();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "freeing sched prof refs", rv);
    rv = _brcm_sai_free_sched();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "freeing scheduler info", rv);
    rv = _brcm_sai_free_policer();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "freeing policer state", rv);
    rv = _brcm_sai_free_qos_map();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "freeing qos map state", rv);
    rv = _brcm_sai_free_tunnel();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "freeing tunnel state", rv);
    rv = _brcm_sai_free_route_info();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "freeing route info state", rv);
    rv = _brcm_sai_free_nh_info();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "freeing nh info state", rv);
    rv = _brcm_sai_free_nbr();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "freeing nbr info state", rv);
    rv = _brcm_sai_free_nhg_info();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "freeing nhg info state", rv);
    rv = _brcm_sai_free_udf();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "freeing udf hash state", rv);
    rv = _brcm_sai_free_hash();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "freeing hash state", rv);
    rv = _brcm_sai_free_acl();
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "freeing acl state", rv);
    if (wb)
    {
        /* Trigger - Sync DB to NV mem and shzip it */
        rv = syncdbTableStore(&client_id, 0, TRUE);
        BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                            "Sync DB to NV mem", rv);
    }
    if (db_pid > 0 && db_pid != -1)
    {
        syncdb_state = FALSE;
        kill(db_pid, SIGKILL);
        BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "Issued termination "
                            "request to syncdb.\n");
    }
    BRCM_SAI_LOG_DMGR(SAI_LOG_LEVEL_DEBUG, "SAI Exit %s\n", __FUNCTION__);
    return SAI_STATUS_SUCCESS;
}
