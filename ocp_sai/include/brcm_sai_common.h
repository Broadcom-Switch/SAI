/*********************************************************************
 *
 * Copyright: (c) 2017 Broadcom.
 * Broadcom Proprietary and Confidential. All rights reserved.
 *
 *********************************************************************/

#if !defined (_BRM_SAI_COMMON)
#define _BRM_SAI_COMMON

/*
################################################################################
#                                 Public includes                              #
################################################################################
*/
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <semaphore.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <inttypes.h>

/*
################################################################################
#                               All internal includes                          #
################################################################################
*/
#include "sal/types.h"
#include "soc/devids.h"
#include "soc/cm.h"
#include "soc/drv.h"
#include "soc/trident2.h"
#include "soc/tomahawk.h"
#include "sal/appl/sal.h"
#include "sal/appl/config.h"

#include "bcm_int/esw/trident2.h"
#include "bcm_int/esw/tomahawk.h"

#include "bcm/error.h"
#include "bcm/init.h"
#include "bcm/switch.h"
#include "bcm/stack.h"
#include "bcm/port.h"
#include "bcm/stg.h"
#include "bcm/l2.h"
#include "bcm/l3.h"
#include "bcm/link.h"
#include "bcm/field.h"
#include "bcm/knet.h"
#include "bcm/cosq.h"
#include "bcm/trunk.h"
#include "bcm/tunnel.h"

#include "appl/diag/shell.h"

/* Useful pbmp debug API */
#include "appl/diag/system.h"


#include "brcm_sai_version.h"
#include "brcm_syncdb.h"
#include "driver_util.h"
#include "brcm_sai_debug.h"

#ifdef PRINT_TO_SYSLOG
#include <syslog.h>
#endif

/*
################################################################################
#                                   Common defines                             #
################################################################################
*/
#ifndef STATIC
#define STATIC
#endif /* !STATIC */

#ifndef TRUE
#define TRUE      1
#endif
#ifndef FALSE
#define FALSE     0
#endif
#ifndef INGRESS
#define INGRESS   0
#endif
#ifndef EGRESS
#define EGRESS    1
#endif
#ifndef MATCH
#define MATCH     0
#endif

#define COLD_BOOT 0
#define WARM_BOOT 1
#define FAST_BOOT 2

#define _BRCM_SAI_MASK_8           0xFF
#define _BRCM_SAI_MASK_16          0xFFFF
#define _BRCM_SAI_MASK_32          0xFFFFFFFF
#define _BRCM_SAI_MASK_64          0xFFFFFFFFFFFFFFFF
#define _BRCM_SAI_MASK_64_UPPER_32 0xFFFFFFFF00000000
#define _BRCM_SAI_MASK_64_LOWER_32 0x00000000FFFFFFFF
#define _BCAST_MAC_ADDR            { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }
#define _BCM_L3_HOST_AS_ROUTE      0

#define SAI_BUFFER_POOL_ATTR_BRCM_CUSTOM_POOL_ID (BRCM_SAI_BUFFER_POOL_CUSTOM_START)


/*
################################################################################
#                            Platform based defines                            #
################################################################################
*/
#define BRCM_MMU_B_PER_C 208

#define _CPU_L0_NODES               1
#define _L0_NODES                   2

#define NUM_CPU_L1_NODES            2

/* This is the max so far - seen in TH3 */
#define NUM_QUEUES                  12
#define NUM_L1_NODES                10
#define NUM_L2_NODES                2

#define NUM_TH_QUEUES               10
#define NUM_TH_CPU_L0_NODES         10
#define NUM_TH_CPU_MC_QUEUES        48
#define NUM_TH_L0_NODES             10

#define NUM_TH3_CPU_L1_NODES         2
#define NUM_TH3_CPU_L0_NODES         12
#define NUM_TH3_CPU_MC_QUEUES        48
#define NUM_TH3_L0_NODES             12
#define NUM_TH3_L1_NODES             2


#define NUM_TD2_CPU_QUEUES          8
#define NUM_TD2_CPU_L0_NODES        1
#define NUM_TD2_CPU_MC_QUEUES       16 /* h/w supports 48 but limiting based on expected usage */
#define NUM_TD2_L0_NODES            2
#define NUM_TD2_L1_PER_L0           8
#define NUM_TD2_L2_NODES            2

#define NUM_HX4_QUEUES              8
#define NUM_HX4_L1_NODES            8
#define NUM_HX4_CPU_QUEUES          8
#define NUM_HX4_CPU_L0_NODES        1
#define NUM_HX4_CPU_MC_QUEUES       16 /* h/w supports 48 but limiting based on expected usage */
#define NUM_HX4_L0_NODES            1
#define NUM_HX4_L1_PER_L0           8

#define TOTAL_SCHED_NODES_PER_PORT  13 /* This is the current max but will change based on device type */

#define _BRCM_SAI_MAX_HIERARCHY_TD2 3
#define _BRCM_SAI_MAX_HIERARCHY_TH  2

#ifndef MAX
#define MAX(a, b)                   (a > b ? a : b)
#endif
#define NUM_L0_NODES                MAX(NUM_TH3_CPU_L0_NODES, NUM_TD2_CPU_L0_NODES)
#define NUM_CPU_MC_QUEUES           MAX(NUM_TH_CPU_MC_QUEUES, NUM_TD2_CPU_MC_QUEUES)

#define _BRCM_SAI_MAX_ECMP_MEMBERS  16384
#define _BRCM_HX4_MAX_ECMP_MEMBERS  4096
#define _BRCM_HX4_MAX_FDB_ENTRIES   32768
#define _BRCM_TD3_MAX_ECMP_MEMBERS  32768

/*
################################################################################
#                            Common internal defines                           #
################################################################################
*/
#define BRCM_SAI_API_ID_MIN                SAI_API_UNSPECIFIED+1
#define BRCM_SAI_API_ID_MAX                SAI_API_BRIDGE /* FIXME: Check and Update */

#define _BRCM_SAI_CMD_BUFF_SIZE            512
#define _BRCM_SAI_FILE_PATH_BUFF_SIZE      256

#define _BRCM_SAI_VR_MAX_VID               4094
#define _BRCM_SAI_VR_DEFAULT_TTL           0   /* This is the MCAST ttl threshold */
#define _BRCM_SAI_MAX_PORTS                220 /* Max logical port number used by underlying SDK/driver (TH) */
#define _BRCM_SAI_MAX_HOSTIF               128
#define _BRCM_SAI_MAX_FILTERS_PER_INTF     32
#define _BRCM_SAI_MAX_TUNNELS              (1024+1)
#define _BRCM_SAI_MAX_TUNNEL_RIFS          (_BRCM_SAI_MAX_TUNNELS*2)
#define _BRCM_SAI_MAX_UDF_GROUPS           64
#define _BRCM_SAI_MAX_HASH                 32
#define _BRCM_SAI_MAX_TRAP_GROUPS          64
#define _BRCM_SAI_MAX_TRAPS                128
#define _BRCM_SAI_MAX_HOSTIF_TABLE_ENTRIES 128

#define _BRCM_SAI_PORT_SCHEDULER_TYPE      0
#define _BRCM_SAI_L0_SCHEDULER_TYPE        1
#define _BRCM_SAI_L1_SCHEDULER_TYPE        2
#define _BRCM_SAI_QUEUE_TYPE_UCAST         3
#define _BRCM_SAI_QUEUE_TYPE_MULTICAST     4
#define _BRCM_SAI_MAX_WRED                 128 /* Max profiles */
#define _BRCM_SAI_SCHEDULER_ATTR_INVALID   -1
#define _BRCM_SAI_MAX_SCHEDULER_PROFILES   128 /* Max profiles */
#define _BRCM_SAI_SCHEDULER_WEIGHT_MIN     1
#define _BRCM_SAI_SCHEDULER_WEIGHT_MAX     100
#define _BRCM_SAI_PORT_MAX_QUEUES          (NUM_QUEUES) /* Only UC queues */
#define _BRCM_SAI_MAX_NODES                8
#define _BRCM_SAI_MAX_BUF_POOLS            4 /* Ingress, Egress each */
/* Ingress (PG) + Egress (Queue) */
#define _BRCM_SAI_MAX_TC                   16
#define _BRCM_SAI_MAX_BUF_PROFILES         (8*_BRCM_SAI_MAX_PORTS*2+1)
#define _BRCM_SAI_MAX_SC_POLICERS          (_BRCM_SAI_MAX_PORTS*3) /* For DLF, MCAST, BCAST */

#define _BRCM_SAI_WRED_ATTR_GREEN          0x1
#define _BRCM_SAI_WRED_ATTR_YELLOW         0x2
#define _BRCM_SAI_WRED_ATTR_RED            0x4

#define _BRCM_SAI_STORMCONTROL_MAX_CBS     16777249   /* Only valid in Byte Mode */
#define _BRCM_SAI_STORMCONTROL_MAX_PPS     3355441600 /* In Packet Mode */

#define _BRCM_SAI_IP2ME_CLASS              1

#define _BRCM_SAI_UDF_HASH_MASK_SIZE       2

#define _BRCM_SAI_RIF_TYPE_LAG             16
#define _BRCM_SAI_NH_UNKOWN_NBR_TYPE       16

#define _BRCM_SAI_MAX_ACL_TABLES           20 /* Note, this is h/w dependent */
#define _BRCM_SAI_MAX_ACL_TABLE_GROUPS     (1024+1)
#define _BRCM_SAI_MAX_ACL_BIND_TYPES       SAI_ACL_BIND_POINT_TYPE_SWITCH+1

#define _BRCM_SAI_ECN_NOT_ECT              0
#define _BRCM_SAI_ECN_ECT1                 1
#define _BRCM_SAI_ECN_ECT0                 2
#define _BRCM_SAI_ECN_CE                   3

#define _BRCM_SAI_NUM_LOSSLESS_QUEUES      1 /* Note: this should be a soc or KVP input */

#define _BRCM_SAI_MAX_BCAST_IP             _BRCM_SAI_VR_MAX_VID /* Equal to max possible vlans */

#define _BRCM_SAI_MAX_ACL_INGRESS_GROUPS   12
#define _BRCM_SAI_MAX_ACL_EGRESS_GROUPS    4

#define _BRCM_SAI_RIF_DEFAULT_MTU          9100
#define _BRCM_SAI_RIF_MAX_MTU              16383

#define _BRCM_SAI_MAX_QOS_MAPS             64

#define _BRCM_SAI_DEFAULT_INTER_FRAME_GAP  12

#define _BRCM_SAI_MAX_MIRROR_SESSIONS      8

#define _BRCM_SAI_MAX_TNL_MAP              16348
#define _BRCM_SAI_MAX_TNL_NET_VPN          256
#define _BRCM_SAI_VNI_VLAN_BASE            0x7000
#define _BRCM_SAI_MAX_VNI_VLAN             8192
#define _BRCM_SAI_MAX_TUNNEL_MAP           1024
#define _BRCM_RSVD_NET_VNI_BASE            0x100000
#define _BRCM_SAI_VXLAN_DEFAULT_UDP_PORT   4789

#define _BRCM_SAI_MAX_NR_TRUNK_MEMBERS     128
#define _BRCM_SAI_MAX_NR_VLAN_MEMBERS      128
#define _BRCM_SAI_MAX_LAYERS               2

/*
################################################################################
#                        Port validation macros                                #
################################################################################
*/
#define _BRCM_SAI_IS_CPU_PORT(_port_) (0 == _port_)

/*
################################################################################
#                                   Common types                               #
################################################################################
*/

typedef struct _brcm_sai_queue_tc_mapping_s {
    int size;
    bcm_cos_t tc[_BRCM_SAI_MAX_TC];
} _brcm_sai_queue_tc_mapping_t;

typedef struct _brcm_sai_qmin_cache_s {
    bool valid;
    sai_object_id_t buff_prof;
    int val;
} _brcm_sai_qmin_cache_t;

typedef uint8_t bitmap_t;

/* Define queue bitmap type. Width of this may need expansion
   depending on NUM_QUEUES */
typedef uint16_t _brcm_sai_queue_bmap_t;

typedef struct _brcm_sai_vr_info_s {
    bcm_vrf_t vr_id;
    sai_mac_t vr_mac;
    int ref_count;
}_brcm_sai_vr_info_t;

typedef struct _sai_rif_info_s {
    int idx;
    bool valid;
    int vr_id;
    sai_object_id_t vid_obj;
    int station_id;
    int nb_mis_pa;
} _brcm_sai_rif_info_t;

typedef struct _sai_trif_info_s {
    int idx;
    bool valid;
    int vr_id;
    int station_id;
} _brcm_sai_trif_info_t;

typedef struct _brcm_sai_nbr_s {
    int vr_id;
    sai_ip_addr_family_t addr_family;
    sai_ip4_t ip4;
    sai_ip6_t ip6;
    int rif_id;
} _brcm_sai_nbr_t;

typedef struct _brcm_sai_nbr_info_s {
    _brcm_sai_nbr_t nbr; /* key */
    bcm_if_t l3a_intf; /* value */ /* Is always the phy intf value even for TH LAG */
    int pa;
    struct _brcm_sai_nbr_info_s *next;
} _brcm_sai_nbr_info_t;

typedef struct _mac_vid_s {
    sai_mac_t mac;
    int vid;
} _mac_vid_t;

typedef struct _brcm_sai_mac_nbr_info_table_s {
    _mac_vid_t mac_vid; /* key */
    int nbrs_count; /* value */
    _brcm_sai_nbr_info_t *nbrs;
    _brcm_sai_nbr_info_t *nbrs_end;
} _brcm_sai_mac_nbr_info_table_t;

typedef struct _brcm_sai_nbr_id_s {
    int idx; /* key */
    bool valid; /* value */
} _brcm_sai_nbr_id_t;

typedef struct _brcm_sai_flex_stat_info_s {
  unsigned int ing_map;
  unsigned int egr_map;
  unsigned int ing_ctr;
  unsigned int egr_ctr;
} _brcm_sai_flex_stat_info_t;

typedef struct _brcm_sai_nh_list_s {
    int nhid; /* key */
    struct _brcm_sai_nh_list_s *next; /* value */
} _brcm_sai_nh_list_t;

typedef struct _brcm_sai_egress_objects_s {
    bcm_if_t eoid;
    int type;
    int ptid;
    struct _brcm_sai_egress_objects_s *next;
} _brcm_sai_egress_objects_t;

typedef struct _brcm_sai_nbr_table_info_s {
    _brcm_sai_nbr_t nbr; /* key */
    int id; /* value */
    int pa;
    int nh_count;
    _brcm_sai_nh_list_t *nhs;
    _brcm_sai_nh_list_t *nhs_end;
    bool bcast_ip;
    int vid; /* for info only */
    int entry;
    unsigned int flags;
    sai_mac_t mac;
    bcm_if_t if_id; /* egress object */
    bool noroute;
    unsigned int port_tgid;
    bcm_multicast_t mcg;
    int eobj_count;
    _brcm_sai_egress_objects_t *eobjs;
    _brcm_sai_egress_objects_t *eobjs_end;
} _brcm_sai_nbr_table_info_t;

typedef struct _brcm_sai_nh_info_s {
    int idx; /* key */ /* generated nhid */
    int act_type; /* value */
    int type_state; /* dynamic type */
    bcm_if_t if_id; /* egress obj */
    /* physical interface type : SAI_ROUTER_INTERFACE_TYPE_PORT, _BRCM_SAI_RIF_TYPE_LAG */
    int intf_type;
    sai_object_id_t rif_obj;
    sai_object_id_t tid_obj;
    _brcm_sai_nbr_t nbr; /* used to lookup associated nbr at attach/detach time */
} _brcm_sai_nh_info_t;

typedef struct _brcm_sai_redirect_entry_list_s {
    int entry;
    struct _brcm_sai_redirect_entry_list_s *next;
} _brcm_sai_redirect_entry_list_t;

typedef struct _brcm_sai_ecmp_table_s {
    bcm_if_t intf; /* key */ /* ecmp intf */
    int nh_count; /* value */
    _brcm_sai_nh_list_t *nh_list;
    _brcm_sai_nh_list_t *end;
} _brcm_sai_ecmp_table_t;

#define _ROUTE_STATE_FORWARD 0
#define _ROUTE_STATE_DROP    1
#define _ROUTE_STATE_TRAP    2

typedef struct _brcm_sai_route_info_s {
    int vr_id;
    sai_ip_addr_family_t addr_family;
    sai_ip4_t ip4;
    sai_ip4_t ip4m;
    sai_ip6_t ip6;
    sai_ip6_t ip6m;
} _brcm_sai_route_info_t;

typedef struct _brcm_sai_route_list_s {
    _brcm_sai_route_info_t route; /* key */
    bool valid;     /* Note: this field is part of key to avoid null node
                             for default route in default VRF */
    bool multipath; /* value */
    bool discard;
    int state;
    struct _brcm_sai_route_list_s *prev;
    struct _brcm_sai_route_list_s *next;
} _brcm_sai_route_list_t;

typedef struct _brcm_sai_route_table_s {
    _brcm_sai_route_info_t route; /* key */
    sai_object_id_t nh_obj; /* value */
    int pa;
    int nhid;
    _brcm_sai_route_list_t *ptr;
    bool valid; /* value */
    bool multipath;
    bool discard;
    int state;
} _brcm_sai_route_table_t;

typedef struct _brcm_sai_nh_ecmp_s {
    bcm_if_t intf; /* key */ /* ecmp intf */
    struct _brcm_sai_nh_ecmp_s *next; /* value */
} _brcm_sai_nh_ecmp_t;

typedef struct _brcm_sai_nh_table_s {
    int nhid; /* key */
    int fwd_count; /* value */
    _brcm_sai_route_list_t *fwd;
    _brcm_sai_route_list_t *fwd_end;
    int drop_count;
    _brcm_sai_route_list_t *drop;
    _brcm_sai_route_list_t *drop_end;
    int ecmp_count;
    _brcm_sai_nh_ecmp_t *ecmp;
    _brcm_sai_nh_ecmp_t *ecmp_end;
    int entry_count;
    _brcm_sai_redirect_entry_list_t *entry_list;
} _brcm_sai_nh_table_t;

typedef struct _sai_gport_1_s {
    int idx;
    bcm_gport_t gport;
} _sai_gport_1_t;

typedef struct _sai_gport_2_s {
    int idx1;
    int idx2;
    bcm_gport_t gport;
} _sai_gport_2_t;

typedef struct _brcm_sai_cosq_gport_discard_s {
    bcm_cosq_gport_discard_t discard;
    int et;
    int rt;
} _brcm_sai_cosq_gport_discard_t;

typedef struct _sai_qos_wred_s {
    int idx;
    uint8_t valid; /* Set the specific colors for wred type */
    _brcm_sai_cosq_gport_discard_t discard_g;
    _brcm_sai_cosq_gport_discard_t discard_y;
    _brcm_sai_cosq_gport_discard_t discard_r;
    bool ect;
    int gn;
    _brcm_sai_queue_bmap_t port_data[_BRCM_SAI_MAX_PORTS];
} _brcm_sai_qos_wred_t;

typedef struct _brcm_sai_lag_bp_vlan_info_s {
    int vid; /* key */
    int utag; /* value */
    struct _brcm_sai_lag_bp_vlan_info_s *next;
} _brcm_sai_lag_bp_vlan_info_t;

typedef struct _brcm_sai_bridge_lag_port_s {
    sai_object_id_t oid;
    bool bridge_port_state; /* value */
    int learn_flags;
    int vid_count;
    _brcm_sai_lag_bp_vlan_info_t *vid_list;
    struct _brcm_sai_bridge_lag_port_s *next;
} _brcm_sai_bridge_lag_port_t;

typedef struct _brcm_sai_port_info_s {
    int idx;
#define _BP_DEFAULT 0
#define _BP_DELETED 1
#define _BP_CREATED 2
    int bdg_port; /* value */
    int wred;
    int pfc_q_map;
    bool trunk; /* Is part of a trunk */
    int tid; /* FIXME: needs to be a list */
    sai_object_id_t sched_id;
    bool bdg_port_admin_state;
    int learn_flags;
    _brcm_sai_flex_stat_info_t flex_stat_map;
    bool ingress_ms[_BRCM_SAI_MAX_MIRROR_SESSIONS]; /* ingress mirror sessions */
    bool egress_ms[_BRCM_SAI_MAX_MIRROR_SESSIONS];  /* egress mirror sessions  */
} _brcm_sai_port_info_t;

typedef struct _brcm_sai_port_qid_s {
    int idx1;
    int idx2;
    sai_object_id_t qoid;
    sai_object_id_t parent_sched;
} _brcm_sai_port_qid_t;

typedef struct _brcm_sai_port_buff_prof_applied_s {
    int idx1;
    int idx2;
    bool prof_applied;
} _brcm_sai_port_buff_prof_applied_t;

typedef struct _brcm_sai_queue_wred_s {
    int idx1;
    int idx2;
    int wred;
} _brcm_sai_queue_wred_t;

typedef struct _sai_policer_oid_map_s {
    sai_object_id_t oid;
    struct _sai_policer_oid_map_s *next;
} _brcm_sai_policer_oid_map_t;

typedef struct _sai_policer_action_s {
    bcm_policer_t pol_id;
    sai_packet_action_t gpa;
    sai_packet_action_t ypa;
    sai_packet_action_t rpa;
    int ref_count;
    _brcm_sai_policer_oid_map_t *oids;
    struct _sai_policer_action_s *next;
} _brcm_sai_policer_action_t;

typedef struct _brcm_sai_netif_info_s {
    bool status;
    bool vlan_rif; /* Is this a vlan rif or not */
    bool lag; /* Is this a LAG or not */
    sai_hostif_vlan_tag_t tag_mode;
    sai_object_id_t if_obj;
} _brcm_sai_netif_info_t;

typedef struct _brcm_sai_netif_map_s {
    int idx;
    /* Max 256 ids */
    uint8_t netif_id;
    uint8_t filter_id[_BRCM_SAI_MAX_FILTERS_PER_INTF];
    int8_t num_filters;
} _brcm_sai_netif_map_t;

typedef struct _brcm_sai_hostif_s {
    int idx;
    _brcm_sai_netif_info_t info;
    /* FIXME: the following seems redundant, could be removed in the future */
    bcm_knet_netif_t netif; /* NOTE: Need to update DM schema if SDK struct changes */
} _brcm_sai_hostif_t;

typedef struct _brcm_sai_hostif_filter_s {
    int idx1;
    int idx2;
    int type;
    int dest_type;
    uint32_t flags;
    uint32_t match_flags;
    bcm_port_t m_ingport;
} _brcm_sai_hostif_filter_t;

typedef struct _brcm_sai_hostif_table_entry_s {
    int idx;
    bool valid;
    int entry_type;
    sai_object_id_t if_obj;
    sai_object_id_t trap_obj;
} _brcm_sai_hostif_table_entry_t;

typedef struct _brcm_sai_trap_refs_s {
    int trap;
    struct _brcm_sai_trap_refs_s *next;
} _brcm_sai_trap_refs_t;

typedef struct _brcm_sai_trap_group_s {
    int idx;
    bool valid;
    bool state;
    uint32_t qnum;
    sai_object_id_t policer_id;
    int ref_count;
    _brcm_sai_trap_refs_t *traps;
} _brcm_sai_trap_group_t;

typedef struct _brcm_sai_trap_s {
    int idx;
    bool valid;
    bool installed;
    int trap_id;
    int pkta;
    uint32_t priority;
    sai_object_id_t trap_group;
    bcm_field_group_t group;
    int count;
    bcm_field_entry_t entry[4];
    pbmp_t exclude_list;
} _brcm_sai_trap_t;

typedef struct _brcm_sai_unicast_arp_cb_info_s {
    int rif_type;
    int arp_type;
    bool state;
    bcm_mac_t *mac;
} _brcm_sai_unicast_arp_cb_info_t;

typedef struct _brcm_sai_unicast_arp_s {
    sai_mac_t mac;
    int type; /* req/resp */ /* key */
    bcm_field_entry_t entry_id; /* value */
    int ref_count;
    bool sysmac;
} _brcm_sai_unicast_arp_t;

typedef struct _brcm_sai_policer_pa_s {
    int act;
    sai_packet_action_t gpa;
    sai_packet_action_t ypa;
    sai_packet_action_t rpa;
} _brcm_sai_policer_pa_t;

typedef struct _brcm_sai_qos_ingress_map_s {
    int idx;
    bool valid;            /* Set to TRUE when used */
    uint32_t count;        /* Number of valid entries in this map */
    sai_qos_map_t map[64];
    int inuse;             /* To track the reference count */
} _brcm_sai_qos_ingress_map_t;

typedef struct _brcm_sai_qos_egress_map_s {
    int idx;
    bool valid;            /* Set to TRUE when used */
    uint32_t count;        /* Number of valid entries in this map */
    sai_qos_map_t map[16];
    int inuse;             /* To track the reference count */
} _brcm_sai_qos_egress_map_t;

typedef struct _brcm_sai_scheduler_object_s {
    sai_object_id_t object_id;
    struct _brcm_sai_scheduler_object_s *next;
} _brcm_sai_scheduler_object_t;

typedef struct _brcm_sai_qos_scheduler_s {
    int idx;
    bool valid;
    sai_scheduling_type_t algorithm;
    uint8_t weight;
    sai_meter_type_t shaper_type;
    uint64_t minimum_bandwidth;
    uint64_t minimum_burst;
    uint64_t maximum_bandwidth;
    uint64_t maximum_burst;
    int ref_count;
    _brcm_sai_scheduler_object_t *object_list;
} _brcm_sai_qos_scheduler_t;

typedef struct _brcm_sai_scheduler_group_s {
    int port;
    int level;
    int index;
    sai_object_id_t scheduler_oid; /* profile */
    sai_object_id_t parent_oid; /* scheduler grp or port */
} _brcm_sai_scheduler_group_t;

typedef struct _brcm_sai_buf_pool_s {
    int idx1;
    int idx2;
    bool valid;
    sai_buffer_pool_type_t type;
    sai_uint32_t size;
    sai_uint32_t xoff_thresh;
    sai_buffer_pool_threshold_mode_t mode;
    bcm_gport_t bst_rev_gport_map; /* Obsolete - reverse front panel gport cache for bst */
    int bst_rev_cos_map;           /* Obsolete - reverse front panel port queue cache for bst */
    bcm_gport_t bst_rev_gport_maps[_BRCM_SAI_MAX_LAYERS]; /* reverse front panel gport cache for bst */
    int bst_rev_cos_maps[_BRCM_SAI_MAX_LAYERS]; /* reverse front panel port queue cache for bst */
} _brcm_sai_buf_pool_t;

typedef struct _brcm_sai_buf_profile_s {
    int idx;
    bool valid;
    sai_object_id_t pool_id;
    sai_uint32_t size;
    sai_uint32_t shared_val; /* dynamic or static depending upon TH_MODE */
    sai_uint32_t xoff_thresh;
    sai_uint32_t xon_thresh;
    sai_uint32_t xon_offset_thresh;
    sai_buffer_pool_threshold_mode_t mode;
    /* bitmaps indicating which resources are using these profiles */
    _brcm_sai_queue_bmap_t pg_data[_BRCM_SAI_MAX_PORTS];
    _brcm_sai_queue_bmap_t queue_data[_BRCM_SAI_MAX_PORTS];
    /* assuming profiles are not applied to ports */
} _brcm_sai_buf_profile_t;

typedef struct _brcm_sai_buf_pool_count_s {
    int idx;
    int count;
} _brcm_sai_buf_pool_count_t;

typedef struct _brcm_sai_tunnel_info_s {
    int idx;
    bool valid;
    uint32_t type;
    uint32_t encap_ttl_mode;
    sai_uint8_t encap_ttl;
    uint32_t decap_ttl_mode;
    uint32_t encap_dscp_mode;
    sai_uint8_t encap_dscp;
    uint32_t decap_dscp_mode;
    uint32_t ecn;
    sai_ip_address_t ip_addr;
    sai_ip_address_t nh_ip_addr;
    sai_object_id_t underlay_if; /* init/encap */
    sai_object_id_t overlay_if; /* term/decap */
    bcm_if_t intf;
    sai_object_id_t encap_map;
    sai_object_id_t decap_map;
    sai_object_id_t term_obj;
    uint32_t in_use; /* ref count */
} _brcm_sai_tunnel_info_t;

typedef struct _brcm_sai_tunnel_table_s {
    int idx;
    bool valid;
    int tid;
    int tunnel_type;
    int vr_id;
    sai_ip_address_t dip;
    int tunnel_term_entry_type;
    sai_ip_address_t sip;
    bcm_gport_t tunnel_id;
} _brcm_sai_tunnel_table_t;

typedef struct _brcm_sai_tunnel_map_entry_s {
    int type;  /* key */    /* syncdb key */
    int key;   /* vni */    /* syncdb key */
    int val;   /* vlan */
    int tunnel_map;
    bcm_vpn_t vpn;
} _brcm_sai_tunnel_map_entry_t;

typedef struct _brcm_sai_tunnel_net_vpn_entry_s {
    int type;           /* syncdb key */
    bcm_ip_t src_ip;    /* syncdb key */
    bcm_ip_t dst_ip;    /* syncdb key */
    uint16 udp_dp;      /* syncdb key */
    bcm_multicast_t bc_group;
    bcm_vpn_t vpn_id;
    uint32 vni;
    bcm_gport_t init_id;
    bcm_gport_t term_id;
    bcm_gport_t init_id_mc;
    bcm_gport_t term_id_mc;
    int ref_count;
    int nr_net_ports;
    bcm_port_t  net_ports[_BRCM_SAI_MAX_NR_TRUNK_MEMBERS];
    bcm_if_t    net_egr_obj[_BRCM_SAI_MAX_NR_TRUNK_MEMBERS];
    bcm_gport_t vxlan_port[_BRCM_SAI_MAX_NR_TRUNK_MEMBERS];
    int         l2_station_id[_BRCM_SAI_MAX_NR_TRUNK_MEMBERS];
    bcm_if_t    net_egr_obj_mc[_BRCM_SAI_MAX_NR_TRUNK_MEMBERS];
    bcm_gport_t vxlan_port_mc[_BRCM_SAI_MAX_NR_TRUNK_MEMBERS];
    int         l2_station_id_mc[_BRCM_SAI_MAX_NR_TRUNK_MEMBERS];
} _brcm_sai_tunnel_net_vpn_entry_t;

typedef struct _brcm_sai_vni_vlan_s {
    int idx;
    bool valid;
} _brcm_sai_vni_vlan_t;

typedef struct _brcm_sai_tunnel_map_s {
    int idx;
    bool valid;
    int type;
    int tunnel_idx;
} _brcm_sai_tunnel_map_t;

typedef struct _brcm_sai_udf_object_s {
    sai_object_id_t object_id;
    uint8_t hash_mask[_BRCM_SAI_UDF_HASH_MASK_SIZE];
    struct _brcm_sai_udf_object_s *next;
} _brcm_sai_udf_object_t;

typedef struct _brcm_sai_udfg_info_s {
    int idx;
    bool valid;
    int length;
    int ref_count;
    _brcm_sai_udf_object_t *refs;
    int hid; /* hash id */
} _brcm_sai_udfg_info_t;

typedef struct _brcm_sai_hash_info_s {
    int idx;
    bool valid;
    int hash_fields_count;
    int hash_fields[_BRCM_SAI_MAX_HASH];
} _brcm_sai_hash_info_t;

typedef struct _brcm_sai_acl_entry_s {
    int id;
    int uid; /* user entry id - indicates this is local entry */
    bool state;
    uint8_t bind_mask;
    struct _brcm_sai_acl_entry_s *next;
} _brcm_sai_acl_entry_t;

typedef struct _brcm_sai_acl_bind_point_s {
    int type;
     /* not all lists use object id, some store 32 bit id */
    sai_object_id_t val;
    struct _brcm_sai_acl_bind_point_s *next;
} _brcm_sai_acl_bind_point_t;

typedef struct _brcm_sai_acl_table_s {
    int idx;
    bool valid;
    int stage;
    int ref_count;
    _brcm_sai_acl_entry_t *entries;
    int bind_types_count;
    int bind_types[_BRCM_SAI_MAX_ACL_BIND_TYPES];
    int bind_count;
    _brcm_sai_acl_bind_point_t *bind_pts;
    int group_count;
    int group[_BRCM_SAI_MAX_ACL_TABLE_GROUPS];
} _brcm_sai_acl_table_t;

typedef struct _brcm_sai_acl_tbl_grp_membr_tbl_s {
    sai_object_id_t table;
    struct _brcm_sai_acl_tbl_grp_membr_tbl_s *next;
} _brcm_sai_acl_tbl_grp_membr_tbl_t;

typedef struct _brcm_sai_acl_table_group_s {
    int idx;
    bool valid;
    int stage;
    int type;
    int ref_count;
    _brcm_sai_acl_tbl_grp_membr_tbl_t *acl_tables;
    int bind_types_count;
    int bind_types[_BRCM_SAI_MAX_ACL_BIND_TYPES];
    int bind_count;
    _brcm_sai_acl_bind_point_t *bind_pts;
} _brcm_sai_acl_table_group_t;

typedef struct _brcm_sai_acl_tbl_grp_membr_s {
    int idx;
    bool valid;
    int acl_tbl_grp;
    int acl_table;
    int pri;
}_brcm_sai_acl_tbl_grp_membr_t;

typedef struct _brcm_sai_port_rif_s {
    int idx;
    sai_object_id_t rif_obj;
} _brcm_sai_port_rif_t;

typedef struct _brcm_sai_lag_info_s
{
  int idx;
  bool valid;
  sai_object_id_t rif_obj; /* Also indicates if this LAG is used as a RIF */
  int vid;  /* Assigned by RIF create or user PVID */
  bool internal; /* To distinguish between user or internal vlan */
  bool bcast_ip; /* indicates if nbr field is valid or not */
  _brcm_sai_nbr_t nbr; /* used to lookup associated nbr at member add/remove time */
  unsigned int acl_bind_count;
  _brcm_sai_acl_bind_point_t* acl_bind_list;
  _brcm_sai_acl_bind_point_t* list_head;
} _brcm_sai_lag_info_t;

typedef struct _brcm_sai_vlan_rif_s {
    int idx;
    sai_object_id_t rif_obj;
    bool bcast_ip;
    _brcm_sai_nbr_t nbr; /* applicable only if bcast_ip is set */
} _brcm_sai_vlan_rif_t;

typedef struct _brcm_sai_vlan_membr_s {
    int type; /* port-lag */
    int val;
    bcm_if_t acc_intf_id;        /* vxlan */
    bcm_if_t acc_egr_obj;        /* vxlan */
    bcm_gport_t acc_vxlan_port;  /* vxlan */
    bcm_if_t acc_encap_id;       /* vxlan */
} _brcm_sai_vlan_membr_t;

typedef struct _brcm_sai_vlan_membr_list_s {
    _brcm_sai_vlan_membr_t membr; /* key */
    struct _brcm_sai_vlan_membr_list_s *next; /* value */
} _brcm_sai_vlan_membr_list_t;

typedef struct _brcm_sai_vlan_membr_info_s {
    int vid; /* key */
    int membrs_count; /* value */
    _brcm_sai_vlan_membr_list_t *membrs_end;
    _brcm_sai_vlan_membr_list_t *membrs;
} _brcm_sai_vlan_membr_info_t;

typedef struct _brcm_sai_mirror_session_s {
    int idx; /* key */
    int ref_count; /* value */
    bcm_gport_t destid;
    uint32 flags;
    bcm_gport_t gport;
    bcm_mac_t src_mac;
    bcm_mac_t dst_mac;
    uint16 tpid;
    bcm_vlan_t vlan_id;
    uint8 version;
    uint8 tos;
    uint8 ttl;
    bcm_ip_t src_addr;
    bcm_ip_t dst_addr;
    bcm_ip6_t src6_addr;
    bcm_ip6_t dst6_addr;
    uint16 gre_protocol;
} _brcm_sai_mirror_session_t;

/*
################################################################################
#                                  Common macros                               #
################################################################################
*/

#define DEV_IS_TH3() (_brcm_sai_dev_is_th3())
#define DEV_IS_TH2() (_brcm_sai_dev_is_th2())
#define DEV_IS_TH()  (_brcm_sai_dev_is_th())
#define DEV_IS_TD2() (_brcm_sai_dev_is_td2())
#define DEV_IS_THX() (_brcm_sai_dev_is_thx())
#define DEV_IS_HX4() (_brcm_sai_dev_is_hx4())
#define DEV_IS_TD3() (_brcm_sai_dev_is_td3())
#define DEV_IS_TDX() (_brcm_sai_dev_is_tdx())

#ifndef SAI_LOG_OFF

#define BRCM_SAI_CHK_LOG(api, log_level) if (log_level >= _adapter_log_level[api])


/* Only use for v6 addreses */
#define _BRCM_SAI_IS_ADDR_LINKLOCAL(a)                                  \
  ((((__const uint32_t *) (a))[0] & htonl (0xffc00000))                 \
   == htonl (0xfe800000))


#ifdef PRINT_TO_SYSLOG
extern
uint8_t _brcm_sai_to_syslog(uint8_t sai_log);
/* Internal Log define, dont use directly */
#define _BRCM_SAI_INT_LOG(api, sai_log_level, __fmt, __args...)  \
  do {                                                           \
      BRCM_SAI_CHK_LOG(api, sai_log_level)                       \
        syslog(_brcm_sai_to_syslog((sai_log_level)),             \
               "%s:%d " __fmt, __FUNCTION__, __LINE__,##__args); \
  }                                                              \
  while(0)

#else

/* Internal Log define, dont use directly */
#define _BRCM_SAI_INT_LOG(api, log_level, __fmt, __args...)      \
  do {                                                           \
      BRCM_SAI_CHK_LOG(api, log_level)                           \
        printf("%s:%d " __fmt, __FUNCTION__, __LINE__,##__args); \
  }                                                              \
  while(0)

#endif

#define BRCM_SAI_LOG(__fmt, __args...)  \
  _BRCM_SAI_INT_LOG(SAI_API_UNSPECIFIED, SAI_LOG_LEVEL_DEBUG, __fmt,##__args)

#define BRCM_SAI_INIT_LOG(__fmt, __args...)  \
  _BRCM_SAI_INT_LOG(SAI_API_UNSPECIFIED, SAI_LOG_LEVEL_ERROR, __fmt,##__args)


#define COMP_DBG_LEVEL(COMP, DBG_LVL, __fmt,__args...)  \
  BRCM_SAI_LOG_##COMP(SAI_LOG_LEVEL_##DBG_LVL ,__fmt,##__args)


#else /* SAI_LOG_OFF */
#define _BRCM_SAI_INT_LOG(...)
#define BRCM_SAI_LOG(...)
#endif

#define _BRCM_SAI_ATTR_LIST_DEFAULT_ERR_CHECK(last_attr) \
  if (attr_list[i].id < last_attr)                       \
  {                                                      \
      rv = SAI_STATUS_ATTR_NOT_IMPLEMENTED_0 + i;        \
  }                                                      \
  else                                                   \
  {                                                      \
      rv = SAI_STATUS_INVALID_ATTRIBUTE_0 + i;           \
  }

#define _BRCM_SAI_ATTR_DEFAULT_ERR_CHECK(last_attr)     \
  if (attr->id < last_attr)                             \
  {                                                     \
      rv = SAI_STATUS_ATTR_NOT_IMPLEMENTED_0;           \
  }                                                     \
  else                                                  \
  {                                                     \
      rv = SAI_STATUS_INVALID_ATTRIBUTE_0;              \
  }


/*                      Object management macros
 * +--------------|---------|---------|-----------------------+
 * |63..........48|47.....40|39.....32|31....................0|
 * +--------------|---------|---------|-----------------------+
 * |     map1     | subtype |   type  |           id          |
 * |              | or map2 |         |                       |
 * +--------------|---------|---------|-----------------------+
 */
#define BRCM_SAI_CREATE_OBJ(type, value) ((((sai_object_id_t)type) << 32) | value)
#define BRCM_SAI_CREATE_OBJ_SUB(type, subtype, value) ((((sai_object_id_t)subtype) << 40) | \
                                                       (((sai_object_id_t)type) << 32) | value)
#define BRCM_SAI_CREATE_OBJ_SUB_MAP(type, subtype, map, value) ((((sai_object_id_t)map) << 48) | \
                                                                (((sai_object_id_t)subtype) << 40) | \
                                                                (((sai_object_id_t)type) << 32) | value)
#define BRCM_SAI_GET_OBJ_TYPE(var) ((uint8_t)(var >> 32))
#define BRCM_SAI_GET_OBJ_SUB_TYPE(var) ((uint8_t)(var >> 40))
#define BRCM_SAI_GET_OBJ_MAP(var) ((uint16_t)(var >> 48))
#define BRCM_SAI_GET_OBJ_VAL(type, var) ((type)var)
#define BRCM_SAI_CHK_OBJ_MISMATCH(var, type) (type != BRCM_SAI_GET_OBJ_TYPE(var))

/* Attribute value retreival macros */
#define BRCM_SAI_ATTR_PTR_OBJ() attr->value.oid
#define BRCM_SAI_ATTR_PTR_OBJ_LIST(o) attr->value.objlist.list[o]
#define BRCM_SAI_ATTR_PTR_OBJ_COUNT() attr->value.objlist.count
#define BRCM_SAI_ATTR_PTR_OBJ_TYPE() BRCM_SAI_GET_OBJ_TYPE(BRCM_SAI_ATTR_PTR_OBJ())
#define BRCM_SAI_ATTR_PTR_OBJ_SUBTYPE() BRCM_SAI_GET_OBJ_SUB_TYPE(BRCM_SAI_ATTR_PTR_OBJ())
#define BRCM_SAI_ATTR_PTR_OBJ_VAL(t) BRCM_SAI_GET_OBJ_VAL(t, BRCM_SAI_ATTR_PTR_OBJ())

#define BRCM_SAI_ATTR_LIST_OBJ(a) attr_list[a].value.oid
#define BRCM_SAI_ATTR_LIST_OBJ_TYPE(a) BRCM_SAI_GET_OBJ_TYPE(BRCM_SAI_ATTR_LIST_OBJ(a))
#define BRCM_SAI_ATTR_LIST_OBJ_SUBTYPE(a) BRCM_SAI_GET_OBJ_SUB_TYPE(BRCM_SAI_ATTR_LIST_OBJ(a))
#define BRCM_SAI_ATTR_LIST_OBJ_MAP(a) BRCM_SAI_GET_OBJ_MAP(BRCM_SAI_ATTR_LIST_OBJ(a))
#define BRCM_SAI_ATTR_LIST_OBJ_VAL(t, a) BRCM_SAI_GET_OBJ_VAL(t, BRCM_SAI_ATTR_LIST_OBJ(a))
#define BRCM_SAI_ATTR_LIST_OBJ_COUNT(a) attr_list[a].value.objlist.count
#define BRCM_SAI_ATTR_LIST_OBJ_LIST(a, o) attr_list[a].value.objlist.list[o]
#define BRCM_SAI_ATTR_LIST_OBJ_LIST_VAL(t, a, o) BRCM_SAI_GET_OBJ_VAL(t, BRCM_SAI_ATTR_LIST_OBJ_LIST(a, o))
#define BRCM_SAI_ATTR_ACL_FLD_8(a) (attr_list[a].value.aclfield.data.u8)
#define BRCM_SAI_ATTR_ACL_FLD_MASK_8(a) (attr_list[a].value.aclfield.mask.u8)
#define BRCM_SAI_ATTR_ACL_FLD_16(a) (attr_list[a].value.aclfield.data.u16)
#define BRCM_SAI_ATTR_ACL_FLD_MASK_16(a) (attr_list[a].value.aclfield.mask.u16)
#define BRCM_SAI_ATTR_ACL_FLD_32(a) (attr_list[a].value.aclfield.data.u32)
#define BRCM_SAI_ATTR_ACL_FLD_MASK_32(a) (attr_list[a].value.aclfield.mask.u32)
#define BRCM_SAI_ATTR_ACL_FLD_MAC(a) (attr_list[a].value.aclfield.data.mac)
#define BRCM_SAI_ATTR_ACL_FLD_MASK_MAC(a) (attr_list[a].value.aclfield.mask.mac)
#define BRCM_SAI_ATTR_ACL_FLD_IP4(a) (attr_list[a].value.aclfield.data.ip4)
#define BRCM_SAI_ATTR_ACL_FLD_MASK_IP4(a) (attr_list[a].value.aclfield.mask.ip4)
#define BRCM_SAI_ATTR_ACL_FLD_IP6(a) (attr_list[a].value.aclfield.data.ip6)
#define BRCM_SAI_ATTR_ACL_FLD_MASK_IP6(a) (attr_list[a].value.aclfield.mask.ip6)
#define BRCM_SAI_ATTR_ACL_FLD_OBJ(a) (attr_list[a].value.aclfield.data.oid)
#define BRCM_SAI_ATTR_ACL_FLD_OBJ_COUNT(a) (attr_list[a].value.aclfield.data.objlist.count)
#define BRCM_SAI_ATTR_ACL_FLD_OBJ_LIST(a, o) (attr_list[a].value.aclfield.data.objlist.list[o])
#define BRCM_SAI_ATTR_ACL_FLD_OBJ_LIST_VAL(t, a, o) BRCM_SAI_GET_OBJ_VAL(t, BRCM_SAI_ATTR_ACL_FLD_OBJ_LIST(a, o))
#define BRCM_SAI_ATTR_ACL_ACTION_8(a) ((uint32_t)attr_list[a].value.aclaction.parameter.u8)
#define BRCM_SAI_ATTR_ACL_ACTION_16(a) ((uint32_t)attr_list[a].value.aclaction.parameter.u16)
#define BRCM_SAI_ATTR_ACL_ACTION_32(a) ((uint32_t)attr_list[a].value.aclaction.parameter.u32)
#define BRCM_SAI_ATTR_ACL_ACTION_VAL_8(a) (attr_list[a].value.aclaction.parameter.u8)
#define BRCM_SAI_ATTR_ACL_ACTION_VAL_16(a) (attr_list[a].value.aclaction.parameter.u16)
#define BRCM_SAI_ATTR_ACL_ACTION_VAL_32(a) (attr_list[a].value.aclaction.parameter.u32)
#define BRCM_SAI_ATTR_ACL_ACTION_MAC(a) (attr_list[a].value.aclaction.parameter.mac)
#define BRCM_SAI_ATTR_ACL_ACTION_OBJ(a) (attr_list[a].value.aclaction.parameter.oid)
#define BRCM_SAI_ATTR_ACL_ACTION_OBJ_COUNT(a) (attr_list[a].value.aclaction.parameter.objlist.count)
#define BRCM_SAI_ATTR_ACL_ACTION_OBJ_LIST(a, o) (attr_list[a].value.aclaction.parameter.objlist.list[o])
#define BRCM_SAI_ATTR_ACL_ACTION_OBJ_LIST_VAL(t, a, o) BRCM_SAI_GET_OBJ_VAL(t, BRCM_SAI_ATTR_ACL_ACTION_OBJ_LIST(a, o))

/* Per module logging macros */
#define BRCM_SAI_LOG_DMGR(log_level,__fmt,__args...)                    \
  _BRCM_SAI_INT_LOG(SAI_API_SWITCH, log_level, __fmt,##__args)

#define BRCM_SAI_LOG_SWITCH(log_level,__fmt,__args...)  \
  _BRCM_SAI_INT_LOG(SAI_API_SWITCH, log_level,__fmt,##__args)

#define BRCM_SAI_LOG_PORT(log_level,__fmt,__args...)  \
  _BRCM_SAI_INT_LOG(SAI_API_PORT, log_level,__fmt,##__args)

#define BRCM_SAI_LOG_FDB(log_level,__fmt,__args...)  \
  _BRCM_SAI_INT_LOG(SAI_API_FDB, log_level,__fmt,##__args)

#define BRCM_SAI_LOG_VLAN(log_level,__fmt,__args...)  \
  _BRCM_SAI_INT_LOG(SAI_API_VLAN, log_level,__fmt,##__args)

#define BRCM_SAI_LOG_VR(log_level,__fmt,__args...)  \
  _BRCM_SAI_INT_LOG(SAI_API_VIRTUAL_ROUTER, log_level,__fmt,##__args)

#define BRCM_SAI_LOG_ROUTE(log_level,__fmt,__args...)  \
  _BRCM_SAI_INT_LOG(SAI_API_ROUTE, log_level,__fmt,##__args)

#define BRCM_SAI_LOG_NH(log_level,__fmt,__args...)  \
  _BRCM_SAI_INT_LOG(SAI_API_NEXT_HOP, log_level,__fmt,##__args)

#define BRCM_SAI_LOG_NHG(log_level,__fmt,__args...)  \
  _BRCM_SAI_INT_LOG(SAI_API_NEXT_HOP_GROUP, log_level,__fmt,##__args)

#define BRCM_SAI_LOG_RINTF(log_level,__fmt,__args...)  \
  _BRCM_SAI_INT_LOG(SAI_API_ROUTER_INTERFACE, log_level,__fmt,##__args)

#define BRCM_SAI_LOG_NBOR(log_level,__fmt,__args...)  \
  _BRCM_SAI_INT_LOG(SAI_API_NEIGHBOR, log_level,__fmt,##__args)

#define BRCM_SAI_LOG_ACL(log_level,__fmt,__args...)  \
  _BRCM_SAI_INT_LOG(SAI_API_ACL, log_level,__fmt,##__args)

#define BRCM_SAI_LOG_HINTF(log_level,__fmt,__args...)  \
  _BRCM_SAI_INT_LOG(SAI_API_HOSTIF, log_level,__fmt,##__args)

#define BRCM_SAI_LOG_LAG(log_level,__fmt,__args...)  \
  _BRCM_SAI_INT_LOG(SAI_API_LAG, log_level,__fmt,##__args)

#define BRCM_SAI_LOG_POLICER(log_level,__fmt,__args...)  \
  _BRCM_SAI_INT_LOG(SAI_API_POLICER, log_level,__fmt,##__args)

#define BRCM_SAI_LOG_QMAP(log_level,__fmt,__args...)  \
  _BRCM_SAI_INT_LOG(SAI_API_QOS_MAP, log_level,__fmt,##__args)

#define BRCM_SAI_LOG_QUEUE(log_level,__fmt,__args...)  \
  _BRCM_SAI_INT_LOG(SAI_API_QUEUE, log_level,__fmt,##__args)

#define BRCM_SAI_LOG_WRED(log_level,__fmt,__args...)  \
  _BRCM_SAI_INT_LOG(SAI_API_WRED, log_level,__fmt,##__args)

#define BRCM_SAI_LOG_SCHEDULER(log_level,__fmt,__args...)  \
  _BRCM_SAI_INT_LOG(SAI_API_SCHEDULER, log_level,__fmt,##__args)

#define BRCM_SAI_LOG_SCHED_GROUP(log_level,__fmt,__args...)  \
  _BRCM_SAI_INT_LOG(SAI_API_SCHEDULER_GROUP, log_level,__fmt,##__args)

#define BRCM_SAI_LOG_MMU_BUFFER(log_level,__fmt,__args...)  \
  _BRCM_SAI_INT_LOG(SAI_API_BUFFER, log_level,__fmt,##__args)

#define BRCM_SAI_LOG_HASH(log_level,__fmt,__args...)  \
  _BRCM_SAI_INT_LOG(SAI_API_HASH, log_level,__fmt,##__args)

#define BRCM_SAI_LOG_UDF(log_level,__fmt,__args...)  \
  _BRCM_SAI_INT_LOG(SAI_API_UDF, log_level,__fmt,##__args)

#define BRCM_SAI_LOG_TUNNEL(log_level,__fmt,__args...)  \
  _BRCM_SAI_INT_LOG(SAI_API_TUNNEL, log_level,__fmt,##__args)

#define BRCM_SAI_LOG_MIRROR(log_level,__fmt,__args...)  \
  _BRCM_SAI_INT_LOG(SAI_API_MIRROR, log_level,__fmt,##__args)

#define BRCM_SAI_LOG_BRIDGE(log_level,__fmt,__args...)  \
  _BRCM_SAI_INT_LOG(SAI_API_BRIDGE, log_level,__fmt,##__args)

/* Helper macros */

#define BRCM_SAI_ATTR_IP_ADDR_FAMILY(__sai_attr)        \
  (__sai_attr).addr_family


#define BRCM_SAI_IS_ATTR_FAMILY_IPV4(__sai_attr)                        \
  (BRCM_SAI_ATTR_IP_ADDR_FAMILY((__sai_attr)) == SAI_IP_ADDR_FAMILY_IPV4)


#define BRCM_SAI_IS_ATTR_FAMILY_IPV6(__sai_attr)                        \
  (BRCM_SAI_ATTR_IP_ADDR_FAMILY((__sai_attr)) == SAI_IP_ADDR_FAMILY_IPV6)


#define SAI_MARKER 0xAABBCCDD
#define RANDOM() rand()
void *ALLOC(int size);
void *ALLOC_CLEAR(int n, int size);
void FREE(void *ptr);
int FREE_SIZE(void *ptr);
#define FREE_CLEAR(__ptr) \
  do {                    \
          FREE((__ptr));  \
          __ptr = NULL;   \
  } while (0)
#define CHECK_FREE(__ptr)  \
  do {                     \
      if (NULL != (__ptr)) \
      {                    \
          FREE((__ptr));   \
      }                    \
  } while (0)
int CHECK_FREE_SIZE(void *ptr);
#define CHECK_FREE_CLEAR(__ptr) \
  do {                          \
      if (NULL != (__ptr))      \
      {                         \
          FREE((__ptr));        \
          __ptr = NULL;         \
      }                         \
  } while (0)
int CHECK_FREE_CLEAR_SIZE(void *ptr);

#define DATA_CLEAR(data, data_type) sal_memset(&data, 0, sizeof(data_type))
#define PTR_CLEAR(ptr, data_type) sal_memset(ptr, 0, sizeof(data_type))

/* Error check macros */
#define IS_NULL(ptr) (NULL == ptr)

#define SAI_STATUS_ERROR(rv) (SAI_STATUS_SUCCESS != rv)

#define BRCM_SAI_SWITCH_INIT_CHECK                 \
  do {                                             \
      if (false == _brcm_sai_switch_is_inited())   \
      {                                            \
          BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR, \
          "Switch not initialized yet.\n");        \
          return SAI_STATUS_UNINITIALIZED;         \
      }                                            \
  }while(0)

#define BRCM_SAI_OBJ_CREATE_PARAM_CHK(ptr)                         \
  do {                                                             \
    if ((NULL == ptr) || (NULL == attr_list) || (0 == attr_count)) \
    {                                                              \
        return SAI_STATUS_INVALID_PARAMETER;                       \
    }                                                              \
  }while(0)

#define BRCM_SAI_SET_OBJ_ATTRIB_PARAM_CHK(obj, type)               \
  do {                                                             \
    if (BRCM_SAI_CHK_OBJ_MISMATCH(obj, type))                      \
    {                                                              \
        BRCM_SAI_LOG("Invalid object type 0x%016lx passed\n", obj); \
        return SAI_STATUS_INVALID_OBJECT_TYPE;                     \
    }                                                              \
    if (NULL == attr)                                              \
    {                                                              \
        return SAI_STATUS_INVALID_PARAMETER;                       \
    }                                                              \
  }while(0)


#define BRCM_SAI_GET_ATTRIB_PARAM_CHK             \
  do {                                            \
    if ((NULL == attr_list) || (0 == attr_count)) \
    {                                             \
        return SAI_STATUS_INVALID_PARAMETER;      \
    }                                             \
  }while(0)

#define BRCM_SAI_GET_OBJ_ATTRIB_PARAM_CHK(obj, type)               \
  do {                                                             \
    if (BRCM_SAI_CHK_OBJ_MISMATCH(obj, type))                      \
    {                                                              \
        BRCM_SAI_LOG("Invalid object type 0x%016lx passed\n", obj); \
        return SAI_STATUS_INVALID_OBJECT_TYPE;                     \
    }                                                              \
    if ((NULL == attr_list) || (0 == attr_count))                  \
    {                                                              \
        return SAI_STATUS_INVALID_PARAMETER;                       \
    }                                                              \
  }while(0)

#define BRCM_SAI_SET_STATS_PARAM_CHK                            \
  do {                                                          \
    if ((NULL == counter_ids) || (0 == number_of_counters))     \
    {                                                           \
        return SAI_STATUS_INVALID_PARAMETER;                    \
    }                                                           \
  }while(0)

#define BRCM_SAI_SET_OBJ_STATS_PARAM_CHK(obj, type)               \
  do {                                                             \
    if (BRCM_SAI_CHK_OBJ_MISMATCH(obj, type))                      \
    {                                                              \
        BRCM_SAI_LOG("Invalid object type 0x%016lx passed\n", obj); \
        return SAI_STATUS_INVALID_OBJECT_TYPE;                     \
    }                                                              \
    if ((NULL == counter_ids) || (0 == number_of_counters))        \
    {                                                              \
        return SAI_STATUS_INVALID_PARAMETER;                       \
    }                                                              \
  }while(0)


#define BRCM_SAI_FUNCTION_ENTER(api)  \
  _BRCM_SAI_INT_LOG(api, SAI_LOG_LEVEL_INFO, "SAI Enter %s\n", __FUNCTION__)
#define BRCM_SAI_FUNCTION_EXIT(api)  \
  _BRCM_SAI_INT_LOG(api, SAI_LOG_LEVEL_INFO, "SAI Exit %s\n", __FUNCTION__)

/* Macro for SAI type return code checks */
#define BRCM_SAI_RV_CHK(api, prepend, rv)                      \
  if (SAI_STATUS_SUCCESS != rv)                                \
  {                                                            \
      _BRCM_SAI_INT_LOG(api,                                   \
                        SAI_LOG_LEVEL_ERROR,                   \
                        "%s failed with error %d.\n", prepend, \
                        rv);                                   \
      return rv;                                               \
  }

#define BRCM_SAI_RV_CHK_FREE(api, prepend, rv, ptr)            \
  if (SAI_STATUS_SUCCESS != rv)                                \
  {                                                            \
      _BRCM_SAI_INT_LOG(api,                                   \
                        SAI_LOG_LEVEL_ERROR,                   \
                        "%s failed with error %d.\n", prepend, \
                        rv);                                   \
      CHECK_FREE(ptr);                                         \
      return rv;                                               \
  }

#define BRCM_SAI_RV_CHK_SEM_POST(api, prepend, rv, sem)        \
  if (SAI_STATUS_SUCCESS != rv)                                \
  {                                                            \
      _BRCM_SAI_INT_LOG(api,                                   \
                        SAI_LOG_LEVEL_ERROR,                   \
                        "%s failed with error %d.\n", prepend, \
                        rv);                                   \
      sem_post(&sem);                                          \
      return rv;                                               \
  }

#define BRCM_SAI_RV_LVL_CHK(api, level, prepend, rv)           \
  if (SAI_STATUS_SUCCESS != rv)                                \
  {                                                            \
      _BRCM_SAI_INT_LOG(api,                                   \
                        level,                                 \
                        "%s failed with error %d.\n", prepend, \
                        rv);                                   \
      return rv;                                               \
  }

#define BRCM_SAI_RV_ATTR_CHK(api, prepend, num, rv, id)              \
  if (SAI_STATUS_SUCCESS != rv)                                      \
  {                                                                  \
      _BRCM_SAI_INT_LOG(api,                                         \
                        SAI_LOG_LEVEL_ERROR,                         \
                        "%s %d attrib %d failed with error 0x%x.\n", \
                        prepend, num, id,                            \
                        rv);                                         \
      return rv;                                                     \
  }

#ifndef SAI_CLOSED_SOURCE

#if !defined(sal_memcpy)
#define sal_memcpy              memcpy
#endif

#if !defined(sal_memset)
#define sal_memset              memset
#endif


/* Macros for Opennsl type return code checks and conversion */
#define BRCM_SAI_API_CHK(api, prepend, rv)                            \
  if (BCM_FAILURE(rv))                                                \
  {                                                                   \
      _BRCM_SAI_INT_LOG(api,                                          \
                        SAI_LOG_LEVEL_ERROR,                          \
                        "%s failed with error %s (0x%x).\n", prepend, \
                        bcm_errmsg(rv),                               \
                        rv);                                          \
      return BRCM_RV_BCM_TO_SAI(rv);                                  \
  }

#define BRCM_SAI_API_LVL_CHK(api, level, prepend, rv)                 \
  if (BCM_FAILURE(rv))                                                \
  {                                                                   \
      _BRCM_SAI_INT_LOG(api,                                          \
                        level,                                        \
                        "%s failed with error %s (0x%x).\n", prepend, \
                        bcm_errmsg(rv),                               \
                        rv);                                          \
      return BRCM_RV_BCM_TO_SAI(rv);                                  \
  }

#define BRCM_SAI_ATTR_API_CHK(api, prepend, rv, id)                    \
  if (BCM_FAILURE(rv))                                                 \
  {                                                                    \
      _BRCM_SAI_INT_LOG(api,                                           \
                        SAI_LOG_LEVEL_ERROR,                           \
                        "%s attrib %d failed with error %s (0x%x).\n", \
                        prepend,                                       \
                        id,                                            \
                        bcm_errmsg(rv),                                \
                        rv);                                           \
      return BRCM_RV_BCM_TO_SAI(rv);                                   \
  }

#define BRCM_SAI_NUM_ATTR_API_CHK(api, prepend, num, rv, id)              \
  if (BCM_FAILURE(rv))                                                    \
  {                                                                       \
      _BRCM_SAI_INT_LOG(api,                                              \
                        SAI_LOG_LEVEL_ERROR,                              \
                        "%s %d attrib %d failed with error %s (0x%x).\n", \
                        prepend, num, id,                                 \
                        bcm_errmsg(rv),                                   \
                        rv);                                              \
      return BRCM_RV_BCM_TO_SAI(rv);                                      \
  }

#define BRCM_SAI_API_CHK_FREE(api, prepend, rv, ptr)                  \
  if (BCM_FAILURE(rv))                                                \
  {                                                                   \
      _BRCM_SAI_INT_LOG(api,                                          \
                        SAI_LOG_LEVEL_ERROR,                          \
                        "%s failed with error %s (0x%x).\n", prepend, \
                        bcm_errmsg(rv),                               \
                        rv);                                          \
      CHECK_FREE(ptr);                                                \
      return BRCM_RV_BCM_TO_SAI(rv);                                  \
  }

#define BRCM_SAI_API_CHK_FREE2(api, prepend, rv, ptr1, ptr2)          \
  if (BCM_FAILURE(rv))                                                \
  {                                                                   \
      _BRCM_SAI_INT_LOG(api,                                          \
                        SAI_LOG_LEVEL_ERROR,                          \
                        "%s failed with error %s (0x%x).\n", prepend, \
                        bcm_errmsg(rv),                               \
                        rv);                                          \
      CHECK_FREE(ptr1);                                               \
      CHECK_FREE(ptr2);                                               \
      return BRCM_RV_BCM_TO_SAI(rv);                                  \
  }
#endif /* SAI_CLOSED_SOURCE */

/* Bit manipulation macros */
#define BYTE_SIZE (sizeof(bitmap_t) * 8)
/* Here num_bits is the count of items to create a bitmap of */
#define BRCM_SAI_NUM_BYTES(__num_bits)    ((((__num_bits) - 1) / BYTE_SIZE) + 1)

/* __map is bitmap and __bitp is the bit position */
#define BRCM_SAI_SET_BIT(__map, __bitp)                                             \
    do {                                                                            \
        ((__map)[((__bitp) - 1) / BYTE_SIZE] |= 1 << (((__bitp) - 1) % BYTE_SIZE)); \
    } while(0)

#define BRCM_SAI_CLEAR_BIT(__map, __bitp)                                              \
    do {                                                                               \
        ((__map)[((__bitp) - 1) / BYTE_SIZE] &= ~(1 << (((__bitp) - 1) % BYTE_SIZE))); \
    } while (0)

#define BRCM_SAI_IS_SET(__map, __bitp) \
  ((__map)[(((__bitp) - 1) / BYTE_SIZE)] & ( 1 << (((__bitp) - 1) % BYTE_SIZE)))

#define BRCM_SAI_MAX_UNSET(__map,__pos,__max)                      \
    do {                                                           \
        sai_uint32_t i,j=0;                                        \
        sai_uint32_t __size = BRCM_SAI_NUM_BYTES(__max);           \
        uint8_t mask;                                              \
        for (i=(__size)-1; i>=0; i--)                              \
        {                                                          \
            mask = 0xFF;                                           \
            if ( ((__size) -1) == i)                               \
            {                                                      \
                if ( (__max) < (__size)*BYTE_SIZE)                 \
                {                                                  \
                  mask = mask >> (((__size)*BYTE_SIZE) - (__max)); \
                }                                                  \
            }                                                      \
            if ((__map)[i] != (uint8_t)mask)                       \
            {                                                      \
                break;                                             \
            }                                                      \
        }                                                          \
        if (i >= 0)                                                \
        {                                                          \
            for (j=BYTE_SIZE-1; j>=0; j--)                         \
            {                                                      \
                (__pos) = (j+1) + (i*BYTE_SIZE);                   \
                if ((__pos) > __max)                               \
                {                                                  \
                    continue;                                      \
                }                                                  \
                if (((__map)[i] & (1 << j)) == 0)                  \
                {                                                  \
                    break;                                         \
                }                                                  \
            }                                                      \
        }                                                          \
    } while(0)

/* Mac helper macros */
#define BRCM_SAI_MAC_IS_ZERO(_mac_) ((_mac_[0] | _mac_[1] | _mac_[2] | \
                                      _mac_[3] | _mac_[4] | _mac_[5]) == 0)
#define BRCM_SAI_MAC_IS_MCAST(_mac_) (_mac_[0] & 0x1)
#define BRCM_SAI_MAC_IS_BCAST(_mac_) ((_mac_[0] & _mac_[1] & _mac_[2] & \
                                       _mac_[3] & _mac_[4] & _mac_[5]) == 0xFF)

#define MAC_FMT_STR "%02X-%02X-%02X-%02X-%02X-%02X"
#define MAC_PRINT_STR(__mac)                                    \
  __mac[0], __mac[1], __mac[2], __mac[3], __mac[4], __mac[5]


/* Increment with overflow check */
#define UINT32_INCR(__val)                                       \
  do  { if ((__val) < _BRCM_SAI_MASK_32) (__val)++; } while(0)

/* Decrement with underflow check */
#define UINT32_DECR(__val)                      \
  do  { if ((__val) > 0) (__val)--; } while(0)


/* Convenience macro */
#define UINT32_OPER(__val, __oper)                                      \
  do { if (__oper == INC) { UINT32_INCR(__val); } else {  UINT32_DECR(__val); } \
  } while(0)


/*
################################################################################
#                    MMU related params
################################################################################
*/

/* default PG used to send traffic to CPU */
#define BRCM_SAI_CPU_INGRESS_POOL_DEFAULT 1
#define BRCM_SAI_CPU_EGRESS_POOL_DEFAULT 2
#define BRCM_SAI_CPU_PG_DEFAULT  7
#define BRCM_SAI_CPU_EGRESS_QUEUE_DEFAULT 7

/* Needs to be the same as
   SAI_BUFFER_POOL_ATTR_CUSTOM_RANGE_START */
#define BRCM_SAI_BUFFER_POOL_CUSTOM_START 0x10000000

/*
################################################################################
#                    Global states - non-persistent across WB                  #
################################################################################
*/
extern sai_service_method_table_t host_services;

extern const sai_switch_api_t switch_apis;
extern const sai_port_api_t port_apis;
extern const sai_fdb_api_t fdb_apis;
extern const sai_vlan_api_t vlan_apis;
extern const sai_virtual_router_api_t router_apis;
extern const sai_route_api_t route_apis;
extern const sai_next_hop_api_t next_hop_apis;
extern const sai_next_hop_group_api_t next_hop_grp_apis;
extern const sai_router_interface_api_t router_intf_apis;
extern const sai_neighbor_api_t neighbor_apis;
extern const sai_acl_api_t acl_apis;
extern const sai_hostif_api_t hostif_apis;
extern const sai_lag_api_t lag_apis;
extern const sai_policer_api_t policer_apis;
extern const sai_qos_map_api_t qos_map_apis;
extern const sai_queue_api_t qos_apis;
extern const sai_wred_api_t wred_apis;
extern const sai_scheduler_api_t qos_scheduler_apis;
extern const sai_scheduler_group_api_t scheduler_group_apis;
extern const sai_buffer_api_t buffer_apis;
extern const sai_hash_api_t hash_api;
extern const sai_udf_api_t udf_apis;
extern const sai_tunnel_api_t tunnel_apis;
extern const sai_mirror_api_t mirror_apis;
extern const sai_bridge_api_t bridge_apis;

extern sai_log_level_t _adapter_log_level[];
extern char *_brcm_sai_api_type_strings[];
extern bcm_pbmp_t _rx_los_pbm;

extern sai_fdb_event_notification_fn _fdb_event;
extern sai_port_state_change_notification_fn _port_state_change_event;
extern sai_queue_pfc_deadlock_notification_fn _pfc_deadlock_event;


/* These can be set by SAI user */
extern int _brcm_sai_cpu_pg_id;
extern int _brcm_sai_cpu_ingress_pool_id;
extern int _brcm_sai_cpu_pool_config;

/* Classic host table */
extern bool _brcm_sai_classic_host_tbl;
/* Tomahawk WAR */
extern bool _brcm_sai_ext_view_no_trnk;
/* TH3 has no extended host table */
extern bool _brcm_sai_no_ext_host_tbl;

/* Feature check to easily identify if L3 hosts need a special
   treatment */
#define BRCM_SAI_IS_FEAT_EXT_VIEW_NO_TRNK() (_brcm_sai_ext_view_no_trnk)
#define BRCM_SAI_IS_FEAT_NO_EXT_HOST_VIEW() (_brcm_sai_no_ext_host_tbl)
#define BRCM_SAI_IS_FEAT_CLASSIC_HOST_TBL() (_brcm_sai_classic_host_tbl)
/*
################################################################################
#                                Public routines                               #
################################################################################
*/
/* Enum convertors */
extern sai_status_t BRCM_RV_BCM_TO_SAI(int);
extern sai_status_t BRCM_STAT_SAI_TO_BCM(sai_port_stat_t sai_stat,
                                             bcm_stat_val_t *stat);

/* All other routines */
extern bool _brcm_sai_dev_chk(int id);
extern bool _brcm_sai_dev_is_hx4();
extern bool _brcm_sai_dev_is_td2();
extern bool _brcm_sai_dev_is_th();
extern bool _brcm_sai_dev_is_th2();
extern bool _brcm_sai_dev_is_th3();
extern bool _brcm_sai_dev_is_thx();
extern bool _brcm_sai_dev_is_tdx();
extern bool _brcm_sai_dev_is_td3();



extern bool _brcm_sai_api_is_inited(void);
extern bool _brcm_sai_switch_is_inited(void);
extern sai_object_id_t _brcm_sai_switch_id_get(int unit);
extern sai_status_t _brcm_sai_system_mac_get(sai_mac_t *mac);
extern sai_status_t _brcm_sai_trap_if_get(bcm_if_t *if_id);
extern sai_status_t _brcm_sai_trap_if_set(bcm_if_t if_id);
extern sai_status_t _brcm_sai_drop_if_get(bcm_if_t *if_id);
extern sai_status_t _brcm_sai_drop_if_set(bcm_if_t if_id);
extern bool _brcm_sai_switch_wb_state_get(void);
extern void _brcm_sai_link_event_cb(int unit, bcm_port_t port,
                                    bcm_port_info_t *info);
extern void _brcm_sai_fdb_event_cb(int unit, bcm_l2_addr_t *l2addr,
                                   int operation, void *userdata);
extern int _brcm_cosq_pfc_deadlock_recovery_event_cb(int unit,
               bcm_port_t port, bcm_cos_queue_t cosq,
               bcm_cosq_pfc_deadlock_recovery_event_t recovery_state, void *userdata);
extern void _brcm_sai_switch_pbmp_all_get(bcm_pbmp_t *pbmp);
extern void _brcm_sai_switch_pbmp_fp_all_get(bcm_pbmp_t *pbmp);
extern int _brcm_sai_switch_fp_port_count();
extern void _brcm_sai_switch_pbmp_get(bcm_pbmp_t *pbmp);
extern sai_status_t _brcm_sai_alloc_lag();
extern sai_status_t _brcm_sai_free_lag();
extern sai_status_t _brcm_sai_alloc_mirror();
extern sai_status_t _brcm_sai_free_mirror();
extern sai_status_t _brcm_sai_alloc_vrf(void);
extern sai_status_t _brcm_sai_free_vrf(void);
extern bool _brcm_sai_vrf_valid(_In_ sai_uint32_t vr_id);
extern sai_status_t _brcm_sai_vrf_info_get(sai_uint32_t vr_id, sai_mac_t *mac);
extern bool _brcm_sai_vr_id_valid(sai_uint32_t index);
extern sai_status_t _brcm_sai_vrf_ref_count_update(sai_uint32_t vr_id,
                                                   bool inc_dec);
extern sai_status_t _brcm_sai_vlan_exists(_In_ sai_vlan_id_t vlan_id, bool *e);
extern sai_status_t _brcm_sai_get_max_unused_vlan_id(bcm_vlan_t *vid);
extern sai_status_t _brcm_sai_vlan_obj_members_get(int vid, int *count,
                                                   _brcm_sai_vlan_membr_list_t **list);
extern sai_status_t _brcm_sai_alloc_vlan();
extern sai_status_t _brcm_sai_free_vlan();
extern sai_status_t _brcm_sai_vlan_init(void);
extern sai_status_t _brcm_sai_alloc_rif(void);
extern sai_status_t _brcm_sai_free_rif(void);
extern sai_status_t _brcm_sai_rif_info_get(sai_uint32_t rif_id, int *vrf, int *station_id,
                                           sai_object_id_t *vid_obj, int *nb_mis_pa);
extern void _brcm_sai_vlan_rif_info_set(sai_uint32_t rif_id, bool val, _brcm_sai_nbr_t *nbr);
typedef void (*_brcm_sai_rif_cb)(int unit, _brcm_sai_unicast_arp_cb_info_t *info);
extern sai_status_t _brcm_sai_rif_traverse(int unit, _brcm_sai_unicast_arp_cb_info_t *info,
                                           _brcm_sai_rif_cb cb_fn);
extern sai_status_t _brcm_sai_alloc_nh_info();
extern sai_status_t _brcm_sai_free_nh_info();
extern sai_status_t _brcm_sai_alloc_route_info();
extern sai_status_t _brcm_sai_free_route_info();
extern sai_status_t _brcm_sai_alloc_nhg_info();
extern sai_status_t _brcm_sai_free_nhg_info();
extern int _brcm_sai_storm_info_get(sai_object_id_t policer_id, bool *pkt_mode,
                                    int *cir, int *cbs);
extern sai_status_t
_brcm_sai_policer_actions_get(sai_object_id_t policer_id,
                              sai_packet_action_t *gpa,
                              sai_packet_action_t *ypa,
                              sai_packet_action_t *rpa);
extern sai_status_t _brcm_sai_alloc_qos_map(void);
extern sai_status_t _brcm_sai_free_qos_map();
extern sai_status_t _brcm_sai_ingress_qosmap_get(uint8_t map_type, uint32_t map_id,
                                                 _brcm_sai_qos_ingress_map_t *map);
extern sai_status_t _brcm_sai_egress_qosmap_get(uint8_t map_type, uint32_t map_id,
                                                _brcm_sai_qos_egress_map_t *map);
extern int _brcm_sai_get_max_queues();
extern int _brcm_sai_get_port_max_queues(bool cpu);

extern int _brcm_sai_get_num_queues(); /* UC queues */
extern int _brcm_sai_get_num_mc_queues();
extern int _brcm_sai_get_num_l0_nodes(bcm_port_t port);
extern int _brcm_sai_get_num_l1_nodes(bcm_port_t port);
extern int _brcm_sai_get_num_scheduler_groups(bcm_port_t port);

extern sai_status_t _brcm_sai_alloc_queue_wred(void);
extern sai_status_t _brcm_sai_alloc_wred(void);
extern sai_status_t _brcm_sai_free_wred(void);
extern sai_status_t _brcm_sai_free_queue_wred(void);
extern sai_status_t _brcm_sai_wred_discard_get(int id, uint8_t *type,
                                               _brcm_sai_qos_wred_t *wred_p);
extern sai_status_t _brcm_sai_alloc_switch_gport(void);
extern sai_status_t _brcm_sai_free_switch_gport(void);
extern bcm_gport_t _brcm_sai_switch_port_queue_get(int port, int qid,
                                                       int type);
extern int _brcm_sai_policer_action_ref_attach(sai_object_id_t policer_id,
                                               sai_object_id_t oid);
extern void _brcm_sai_policer_action_ref_detach(sai_object_id_t oid);
extern sai_status_t _brcm_sai_alloc_policer(void);
extern sai_status_t _brcm_sai_free_policer(void);
extern int _brcm_sai_get_scheduler_max(int type);
extern sai_status_t _brcm_sai_scheduler_get(int id,
                        _brcm_sai_qos_scheduler_t *sched);
extern sai_status_t _brcm_sai_alloc_sched(void);
extern sai_status_t _brcm_sai_free_sched(void);
extern sai_status_t _brcm_sai_alloc_sched_prof_refs(void);
extern sai_status_t _brcm_sai_free_sched_prof_refs(void);
extern sai_status_t _brcm_sai_scheduler_attach_object(int id,
                        _brcm_sai_qos_scheduler_t *scheduler,
                        sai_object_id_t object_id);
extern sai_status_t _brcm_sai_scheduler_detach_object(int id,
                        _brcm_sai_qos_scheduler_t *scheduler,
                        sai_object_id_t object_id,
                        bool apply_default);
extern int _brcm_sai_scheduler_mode_get(_brcm_sai_qos_scheduler_t *scheduler);
extern sai_status_t _brcm_sai_alloc_sched_group_info();
extern sai_status_t _brcm_sai_hostif_clean(void);
extern sai_status_t _brcm_sai_alloc_hostif(void);
extern sai_status_t _brcm_sai_free_hostif(void);
extern bcm_field_group_t _brcm_sai_global_trap_group_get(int unit);
extern sai_status_t _brcm_sai_arp_trap_add(int unit, bool state, int trap_type,
                                           int mac_type, bcm_mac_t *mac);
extern sai_status_t _brcm_sai_ucast_arp_trap_entry_del(int unit, int trap_type,
                                                      int mac_type, bcm_mac_t *_mac);
extern sai_status_t _brcm_sai_32bit_size_check(sai_meter_type_t meter_type,
                                               sai_uint64_t test_var);
extern sai_status_t _brcm_sai_port_ingress_buffer_config_set(uint8_t port,
                                                             uint8_t pg,
                                                             uint8_t pool_id,
                                                             int max, int min,
                                                             int resume);
extern sai_status_t _brcm_sai_port_egress_buffer_config_set(uint8_t port,
                                                            uint8_t pool_id,
                                                            int mode, int limit,
                                                            int resume);
extern sai_status_t _brcm_sai_buffer_profile_get(sai_object_id_t buffer_profile_id,
                        _brcm_sai_buf_profile_t **buf_profile);
extern sai_status_t _brcm_sai_buffer_pool_get(sai_object_id_t buffer_pool_id,
                                              _brcm_sai_buf_pool_t **buf_pool);
extern sai_status_t _brcm_sai_alloc_buff_pools(void);
extern sai_status_t _brcm_sai_free_buff_pools(void);
extern sai_status_t _brcm_sai_alloc_bridge_info();
extern sai_status_t _brcm_sai_free_bridge_info();
extern sai_status_t _brcm_sai_bridge_port_valid(sai_object_id_t bridge_port_id,
                                                bool *found);
extern sai_status_t _brcm_sai_bridge_lag_port_vlan_add(sai_object_id_t bridge_port_id,
                                                       int vid, int utag);
extern sai_status_t _brcm_sai_bridge_lag_port_vlan_remove(sai_object_id_t bridge_port_id,
                                                          int vid);
extern sai_status_t _brcm_sai_alloc_port_info(void);
extern sai_status_t _brcm_sai_free_port_info(void);
extern sai_status_t _brcm_sai_egress_shared_limit_set(int pool_idx,
                                                      int pool_size,
                                                      int bp_size);
extern sai_status_t _brcm_sai_alloc_nbr();
extern sai_status_t _brcm_sai_free_nbr();
extern void _brcm_sai_nbr_mac_db_update(bool add, bcm_l2_addr_t *l2addr);
extern sai_status_t _brcm_sai_nbr_nh_ref_attach(bcm_l3_host_t *l3_host, int nhid, int pa);
extern sai_status_t _brcm_sai_nbr_nh_ref_detach(bcm_l3_host_t *l3_host, int nhid,
                                                bool lookup_host);
extern sai_status_t _brcm_sai_nbr_bcast_member_add(_brcm_sai_nbr_t *nbr, bcm_if_t intf,
                                                   uint8_t type, int port_tid);
extern sai_status_t _brcm_sai_nbr_bcast_member_remove(_brcm_sai_nbr_t *nbr,
                                                      uint8_t type, int port_tid);
extern sai_status_t _brcm_sai_nbr_bcast_update(_brcm_sai_nbr_t *nbr, bool add,
                                               int tid, int port);
extern sai_status_t _brcm_sai_mac_nbr_lookup(bcm_mac_t mac,
                                             bcm_l3_intf_t *l3_intf,
                                             bcm_l3_host_t *l3_host,
                                             _brcm_sai_nbr_info_t *nbr_info);
extern sai_status_t _brcm_sai_nh_mac_update(bcm_if_t if_id, bool old_lag, bool lag,
                                            int old_port_lag, int port_lag,
                                            int vlan, bcm_if_t intf,
                                            bcm_mac_t old_mac,
                                            bcm_mac_t new_mac);
extern sai_status_t _brcm_sai_nh_intf_update(int nhid, bool add, bool check_glbl,
                                             bcm_l3_host_t *l3_host);
extern int _brcm_sai_max_nh_count_get();
extern sai_status_t _brcm_sai_nhg_nh_update(bool add, int nhid,
                                            bcm_if_t if_id);
extern sai_status_t _brcm_sai_nh_table_route_list_route_add(int nhidx,
                                                            _brcm_sai_route_list_t *route_node);
extern sai_status_t _brcm_sai_nh_table_route_list_route_del(int nhidx,
                                                            _brcm_sai_route_list_t *route_node);
extern sai_status_t _brcm_sai_nh_table_route_list_dump(int nhidx);
extern void _brcm_sai_nh_table_route_list_lock(int nhid);
extern void _brcm_sai_nh_table_route_list_unlock(int nhid);
extern sai_status_t _brcm_sai_nh_table_ecmp_list_ecmp_add(int nhidx, bcm_if_t ecmp_if_id);
extern sai_status_t _brcm_sai_nh_table_ecmp_list_ecmp_del(int nhidx, bcm_if_t ecmp_if_id);
extern sai_status_t _brcm_sai_nh_table_ecmp_get(int nhid, int *count, _brcm_sai_nh_ecmp_t **ecmp);
extern void _brcm_sai_nh_table_ecmp_lock(int nhid);
extern void _brcm_sai_nh_table_ecmp_unlock(int nhid);
extern sai_status_t _brcm_sai_alloc_tunnel(void);
extern sai_status_t _brcm_sai_free_tunnel(void);
extern sai_status_t _brcm_sai_tunnel_info_get(int tunnel_id,
                                              _brcm_sai_tunnel_info_t *info);
extern void _brcm_sai_tunnel_info_set(int tunnel_id,
                                      _brcm_sai_tunnel_info_t *info);
extern sai_status_t _brcm_sai_alloc_udf();
extern sai_status_t _brcm_sai_free_udf();
extern sai_status_t _brcm_sai_udfg_get_next_udf(int gid,
                                                sai_object_id_t **object_id);
extern sai_status_t _brcm_sai_alloc_hash();
extern sai_status_t _brcm_sai_free_hash();
extern sai_status_t _brcm_sai_alloc_acl();
extern sai_status_t _brcm_sai_free_acl();
extern sai_status_t _brcm_sai_acl_obj_bind(sai_object_id_t acl_obj,
                                           bool direction,
                                           sai_object_id_t bind_obj);
extern sai_status_t _brcm_sai_get_vlanobj_from_vlan(bcm_vlan_t vid,
                                                    sai_object_id_t *vlan_obj);
extern sai_status_t _brcm_sai_acl_table_bind_point_detach(int tid, int type, int val);
extern sai_status_t _brcm_sai_acl_table_group_bind_point_detach(int gid, int type, int val);

extern int _brcm_sai_get_max_ecmp_members();

extern sai_status_t _brcm_sai_mmu_cpu_init();
extern sai_status_t _brcm_sai_create_trap_group_entry();

extern int DRV_TH_MMU_PIPED_MEM_INDEX(int port, int mem, int idx);
extern int DRV_MEM_UNIQUE_ACC(int mem, int pipe);
extern sai_status_t
_brcm_sai_wred_discard_update(int id, _brcm_sai_qos_wred_t *wred_p);
extern sai_status_t
_brcm_sai_qos_queue_wred_set(int port, int qid,
                             _brcm_sai_qos_wred_t* wred_p);

extern sai_status_t
_brcm_sai_lag_member_pbmp_get(bcm_trunk_t tid, bcm_pbmp_t* member_pbmp);

extern sai_status_t
_brcm_sai_acl_obj_lag_unbind(sai_object_id_t acl_obj,
                             sai_object_id_t bind_obj,
                             bcm_pbmp_t* unbind_pbmp);

extern sai_status_t
_brcm_sai_lag_unbind_acl_obj(int tid,
                             sai_object_id_t acl_obj);

extern sai_status_t
_brcm_sai_mirror_ref_update(int unit, int id, bool add);

/*
################################################################################
#                 Central Data mgmt (DM) and Warm boot (WB) stuff              #
################################################################################
*/
#define _BRCM_SAI_SDK_CACHE "/brcm_bcm_scache"

typedef enum _brcm_sai_data_bump_e {
    INC,
    DEC
} _brcm_sai_data_bump_t;

typedef union _brcm_sai_data_s {
    bool bool_data;
    sai_uint8_t u8;
    sai_int8_t s8;
    sai_uint16_t u16;
    sai_int16_t s16;
    sai_uint32_t u32;
    sai_int32_t s32;
    bcm_if_t if_id;
    sai_uint64_t u64;
    sai_int64_t s64;
    sai_mac_t mac;
    sai_ip4_t ip4;
    sai_ip6_t ip6;
    sai_ip_address_t ipaddr;
    sai_object_id_t oid;
    sai_uint32_t cos_timers[8];
} _brcm_sai_data_t;

extern sai_status_t
_brcm_sai_get_local_mac(sai_mac_t local_mac_address);

/*
 * Note: Needs to be kept in sync with _brcm_sai_global_data_t !!
 */
typedef enum _brcm_sai_global_data_type_e {
    _BRCM_SAI_VER                       = 0,
    _BRCM_SAI_SWITCH_INITED             = 1,
    _BRCM_SAI_SYSTEM_MAC                = 2,
    _BRCM_SAI_VR_COUNT                  = 3,
    _BRCM_SAI_TUNNEL_RIF_COUNT          = 4,
    _BRCM_SAI_WRED_COUNT                = 5,
    _BRCM_SAI_SAI_SCHEDULER_COUNT       = 6,
    _BRCM_SAI_MMU_BUFF_PROFILE_COUNT    = 7,
    _BRCM_SAI_PFC_QUEUE_MAP_COUNT       = 8,
    _BRCM_SAI_DOT1P_TC_MAP_COUNT        = 9,
    _BRCM_SAI_DOT1P_COLOR_MAP_COUNT     = 10,
    _BRCM_SAI_DSCP_TC_MAP_COUNT         = 11,
    _BRCM_SAI_DSCP_COLOR_MAP_COUNT      = 12,
    _BRCM_SAI_TC_DSCP_MAP_COUNT         = 13,
    _BRCM_SAI_TC_DOT1P_MAP_COUNT        = 14,
    _BRCM_SAI_TC_QUEUE_MAP_COUNT        = 15,
    _BRCM_SAI_TC_PG_MAP_COUNT           = 16,
    _BRCM_SAI_POLICER_COUNT             = 17,
    _BRCM_SAI_HOST_INTF_COUNT           = 18,
    _BRCM_SAI_DROP_INTF_ID              = 19,
    _BRCM_SAI_TRAP_INTF_ID              = 20,
    _BRCM_SAI_ROUTE_NH_COUNT            = 21,
    _BRCM_SAI_ECMP_NH_COUNT             = 22,
    _BRCM_SAI_UDFG_COUNT                = 23,
    _BRCM_SAI_HASH_COUNT                = 24,
    _BRCM_SAI_UDF_COUNT                 = 25,
    _BRCM_SAI_UDF_SHARED_0              = 26,
    _BRCM_SAI_UDF_SHARED_1              = 27,
    _BRCM_SAI_TRAP_FP_GROUP             = 28,
    _BRCM_SAI_TRAP_GROUPS_COUNT         = 29,
    _BRCM_SAI_TRAPS_COUNT               = 30,
    _BRCM_SAI_SYSTEM_INTF               = 31,
    _BRCM_SAI_SYSTEM_MAC_SET            = 32,
    _BRCM_SAI_ACL_TABLES_COUNT          = 33,
    _BRCM_SAI_HOST_INTF_ENTRY_COUNT     = 34,
    _BRCM_SAI_ACL_TBL_GRPS_COUNT        = 36,
    _BRCM_SAI_ACL_TBL_GRPS_MEMBRS_COUNT = 37,
    _BRCM_SAI_PFC_DLD_TIMERS            = 38,
    _BRCM_SAI_PFC_DLR_TIMERS            = 39,
    _BRCM_SAI_BRIDGE_PORTS              = 40,
    _BRCM_SAI_BRIDGE_LAG_PORTS          = 41,
    _BRCM_SAI_WARM_SHUT                 = 42,
    _BRCM_SAI_SW_BCAST_RULE_INSTALLED   = 43,
    _BRCM_SAI_SW_BCAST_ENTRY            = 44,
    _BRCM_SAI_ARP_TRAP_REQ              = 45,
    _BRCM_SAI_ARP_TRAP_RESP             = 46,
    _BRCM_SAI_NBR_COUNT                 = 47,
    _BRCM_SAI_BCAST_IP_COUNT            = 48,
    _BRCM_SAI_FDB_COUNT                 = 49,
    _BRCM_SAI_EGRESS_INUSE_COUNT        = 50,
    _BRCM_SAI_UDF_HASH_USED             = 51,
    _BRCM_SAI_ING_FLEX_CTR_MODE_ID      = 52,
    _BRCM_SAI_EGR_FLEX_CTR_MODE_ID      = 53,
    _BRCM_SAI_TRAP_RANGE_ID             = 54,
    _BRCM_SAI_VXLAN_UDP_PORT            = 55
} _brcm_sai_global_data_type_t;

/*
 * Note: Needs to be kept in sync with _brcm_sai_global_data_type_t !!
 */
typedef struct _brcm_sai_global_data_s {
    sai_uint64_t sai_ver;
    bool switch_inited;
    sai_mac_t system_mac;
    sai_uint32_t vr_count;
    sai_uint32_t tunnel_rif_count;
    /* Used to track the number of wred profiles created */
    sai_uint32_t wred_count;
    /* Used to track the number of scheduler profiles created */
    sai_uint32_t sai_scheduler_count;
    sai_uint32_t mmu_buff_profile_count;
    sai_uint32_t pfc_queue_map_count;
    sai_uint32_t dot1p_tc_map_count;
    sai_uint32_t dot1p_color_map_count;
    sai_uint32_t dscp_tc_map_count;
    sai_uint32_t dscp_color_map_count;
    sai_uint32_t tc_dscp_map_count;
    sai_uint32_t tc_dot1p_map_count;
    sai_uint32_t tc_queue_map_count;
    sai_uint32_t tc_pg_map_count;
    sai_uint32_t policer_count; /* Count of policers with actions */
    sai_uint8_t host_if_count;
    bcm_if_t drop_if_id;
    bcm_if_t trap_if_id;
    sai_uint32_t route_nh_count;
    sai_uint32_t ecmp_nh_count;
    sai_uint32_t udfg_count;
    sai_uint32_t hash_count;
    sai_uint32_t udf_count;
    sai_int32_t udf_shared_0;
    sai_int32_t udf_shared_1;
    sai_uint32_t fp_group;
    sai_uint32_t trap_groups;
    sai_uint32_t traps;
    bcm_if_t system_if_id;
    bool system_mac_set;
    /* Used to track the number of ACL tables created */
    sai_uint32_t acl_tables_count;
    sai_uint32_t host_if_entry_count;
    /* Used to track the number of ACL table groups created */
    sai_uint32_t acl_tbl_grps_count;
    /* Used to track the number of ACL table group members created */
    sai_uint32_t acl_tbl_grp_membrs_count;
    sai_uint32_t pfc_dld_timers[8];
    sai_uint32_t pfc_dlr_timers[8];
    sai_uint32_t bridge_ports;
    sai_uint32_t bridge_lag_ports;
    bool ws;
    bool bcast_rule_installed;
    sai_int32_t bcast_entry;
    sai_uint32_t arp_trap_req;
    sai_uint32_t arp_trap_resp;
    sai_uint32_t nbr_count;
    sai_uint32_t bcast_ips;
    sai_uint32_t fdb_count;
    /* Used to track the number of SDK egress objects inuse for CRM */
    sai_uint32_t egress_inuse_count;
    sai_uint32_t udf_hash_used;
    sai_uint32_t ing_flex_mode_id;
    sai_uint32_t egr_flex_mode_id;
    sai_uint32_t range_id;
    sai_uint16_t vxlan_udp_port;
} _brcm_sai_global_data_t;

typedef union _brcm_sai_indexed_data_s {
    bitmap_t *vlan_bmp;
    _sai_gport_1_t gport1;
    _sai_gport_2_t gport2;
    _brcm_sai_port_info_t port_info;
    _brcm_sai_port_qid_t port_qid;
    _brcm_sai_port_buff_prof_applied_t port_buff;
    _brcm_sai_queue_wred_t queue_wred;
    _brcm_sai_netif_map_t *netif_map; /* Using original reference to reduce data copy time */
    _brcm_sai_buf_pool_t *buf_pool; /* Using original reference to reduce data copy time */
    _brcm_sai_buf_profile_t *buf_prof; /* Using original reference to reduce data copy time */
    _brcm_sai_buf_pool_count_t pool_count;
    _brcm_sai_qos_ingress_map_t *ingress_map; /* Using original reference to reduce data copy time */
    _brcm_sai_qos_egress_map_t *egress_map; /* Using original reference to reduce data copy time */
    _brcm_sai_qos_wred_t wred_prof;
    _brcm_sai_qos_scheduler_t scheduler_prof;
    _brcm_sai_vr_info_t vr_info;
    _brcm_sai_rif_info_t rif_info;
    _brcm_sai_tunnel_info_t tunnel_info;
    _brcm_sai_tunnel_table_t tunnel_table;
    _brcm_sai_tunnel_map_entry_t tunnel_map_entry;
    _brcm_sai_tunnel_net_vpn_entry_t tunnel_net_vpn_entry;
    _brcm_sai_trif_info_t trif_info;
    _brcm_sai_hostif_t hostif_info;
    _brcm_sai_hostif_table_entry_t hostif_table;
    _brcm_sai_hostif_filter_t hostif_filter;
    _brcm_sai_nh_info_t nh_info;
    _brcm_sai_udfg_info_t udfg;
    _brcm_sai_hash_info_t hash;
    _brcm_sai_trap_group_t trap_group;
    _brcm_sai_trap_t trap;
    _brcm_sai_acl_table_t acl_table;
    _brcm_sai_port_rif_t port_rif;
    _brcm_sai_lag_info_t lag_info;
    _brcm_sai_vlan_rif_t vlan_rif;
    _brcm_sai_acl_table_group_t acl_tbl_grp;
    _brcm_sai_acl_tbl_grp_membr_t acl_tbl_grp_membr;
    _brcm_sai_nbr_id_t nbr_id;
    _brcm_sai_mirror_session_t ms;
    _brcm_sai_vni_vlan_t vni_vlan;
    _brcm_sai_tunnel_map_t tunnel_map;
} _brcm_sai_indexed_data_t;

typedef enum _brcm_sai_indexed_data_type_e {
    _BRCM_SAI_INDEXED_RSVD               = 0,
    _BRCM_SAI_INDEXED_PORT_SCHED         = 1,
    _BRCM_SAI_INDEXED_L0_SCHED           = 2,
    _BRCM_SAI_INDEXED_L1_SCHED           = 3,
    _BRCM_SAI_INDEXED_UCAST_QUEUE        = 4,
    _BRCM_SAI_INDEXED_MCAST_QUEUE        = 5,
    _BRCM_SAI_INDEXED_CPU_QUEUE          = 6,
    _BRCM_SAI_INDEXED_BUF_POOLS          = 7,
    _BRCM_SAI_INDEXED_BUF_PROFILES       = 8,
    _BRCM_SAI_INDEXED_POOL_COUNT         = 9,
    _BRCM_SAI_INDEXED_PORT_QID           = 10,
    _BRCM_SAI_INDEXED_PORT_INFO          = 11,
    _BRCM_SAI_INDEXED_PORT_BUF_PROF      = 12,
    _BRCM_SAI_INDEXED_DOT1P_TC_MAP       = 13,
    _BRCM_SAI_INDEXED_DOT1P_COLOR_MAP    = 14,
    _BRCM_SAI_INDEXED_DSCP_TC_MAP        = 15,
    _BRCM_SAI_INDEXED_DSCP_COLOR_MAP     = 16,
    _BRCM_SAI_INDEXED_TC_DSCP_MAP        = 17,
    _BRCM_SAI_INDEXED_TC_DOT1P_MAP       = 18,
    _BRCM_SAI_INDEXED_TC_QUEUE_MAP       = 19,
    _BRCM_SAI_INDEXED_TC_PG_MAP          = 20,
    _BRCM_SAI_INDEXED_PFC_QUEUE_MAP      = 21,
    _BRCM_SAI_INDEXED_QUEUE_WRED         = 22,
    _BRCM_SAI_INDEXED_WRED_PROF          = 23,
    _BRCM_SAI_INDEXED_SCHED_PROF         = 24,
    _BRCM_SAI_INDEXED_NETIF_PORT_MAP     = 25,
    _BRCM_SAI_INDEXED_NETIF_VLAN_MAP     = 26,
    _BRCM_SAI_INDEXED_VR_INFO            = 27,
    _BRCM_SAI_INDEXED_RIF_INFO           = 28,
    _BRCM_SAI_INDEXED_TUNNEL_INFO        = 29,
    _BRCM_SAI_INDEXED_TUNNEL_TABLE       = 30,
    _BRCM_SAI_INDEXED_RIF_TUNNEL_INFO    = 31,
    _BRCM_SAI_INDEXED_VLAN_BMP           = 32,
    _BRCM_SAI_INDEXED_HOSTIF_INFO        = 33,
    _BRCM_SAI_INDEXED_HOSTIF_FILTERS     = 34,
    _BRCM_SAI_INDEXED_HOSTIF_TABLE       = 35,
    _BRCM_SAI_INDEXED_NH_INFO            = 36,
    _BRCM_SAI_INDEXED_UDFG_INFO          = 37,
    _BRCM_SAI_INDEXED_HASH_INFO          = 38,
    _BRCM_SAI_INDEXED_TRAP_GROUP         = 39,
    _BRCM_SAI_INDEXED_TRAP               = 40,
    _BRCM_SAI_INDEXED_ACL_TABLE          = 41,
    _BRCM_SAI_INDEXED_PORT_RIF_TABLE     = 42,
    _BRCM_SAI_INDEXED_LAG_INFO_TABLE     = 43,
    _BRCM_SAI_INDEXED_VLAN_RIF_TABLE     = 44,
    _BRCM_SAI_INDEXED_ACL_TBL_GRP        = 45,
    _BRCM_SAI_INDEXED_ACL_TBL_GRP_MEMBR  = 46,
    _BRCM_SAI_INDEXED_NBR_ID             = 47,
    _BRCM_SAI_INDEXED_MIRROR_SESSION     = 48,
    _BRCM_SAI_INDEXED_VNI_VLAN           = 49,
    _BRCM_SAI_INDEXED_TUNNEL_MAP         = 50
} _brcm_sai_indexed_data_type_t;

typedef union _brcm_sai_list_key_s {
    bcm_policer_t pol_id;
    sai_object_id_t obj_id;
    _brcm_sai_route_info_t route;
    bcm_if_t ecmp_intf;
    _brcm_sai_nbr_t nbr;
    int vid;
    bcm_l3_egress_t eobj;
} _brcm_sai_list_key_t;

typedef union _brcm_sai_list_data_s {
    _brcm_sai_policer_action_t *policer_action;
    _brcm_sai_policer_oid_map_t **oid_map;
    _brcm_sai_scheduler_object_t *schd_obj;
    _brcm_sai_udf_object_t *udf_obj;
    _brcm_sai_trap_refs_t *trap_refs;
    _brcm_sai_acl_entry_t *acl_entries;
    _brcm_sai_acl_bind_point_t *bind_points;
    _brcm_sai_redirect_entry_list_t *redirect_list;
    _brcm_sai_bridge_lag_port_t *bdg_lag_ports;
    _brcm_sai_lag_bp_vlan_info_t *vid_list;
    _brcm_sai_nh_ecmp_t *nh_ecmp;
    _brcm_sai_nh_list_t *nh_list;
    _brcm_sai_route_list_t *route_list;
    _brcm_sai_nbr_info_t *nbrs_list;
    _brcm_sai_egress_objects_t *eobj_list;
    _brcm_sai_vlan_membr_list_t *vlan_membrs_list;
    _brcm_sai_acl_tbl_grp_membr_tbl_t *acl_tbl_grp_membr;
} _brcm_sai_list_data_t;

typedef enum _brcm_sai_list_data_type_e {
    _BRCM_SAI_LIST_RSVD                = 0,
    _BRCM_SAI_LIST_POLICER_ACTION      = 1,
    _BRCM_SAI_LIST_POLICER_OID_MAP     = 2,
    _BRCM_SAI_LIST_SCHED_OBJ_MAP       = 3,
    _BRCM_SAI_LIST_UDFG_UDF_MAP        = 4,
    _BRCM_SAI_LIST_TGRP_TRAP_REF       = 5,
    _BRCM_SAI_LIST_ACL_ENTRIES         = 6,
    _BRCM_SAI_LIST_ACL_TBL_BIND_POINTS = 7,
    _BRCM_SAI_LIST_ACL_GRP_BIND_POINTS = 8,
    _BRCM_SAI_LIST_REDIRECT            = 9,
    _BRCM_SAI_LIST_BRIDGE_LAG_PORTS    = 10,
    _BRCM_SAI_LIST_LAG_BP_VLAN_LIST    = 11,
    _BRCM_SAI_LIST_NH_ECMP_INFO        = 12,
    _BRCM_SAI_LIST_ECMP_NH             = 13,
    _BRCM_SAI_LIST_ROUTE_LIST_FWD      = 14,
    _BRCM_SAI_LIST_ROUTE_LIST_DROP     = 15,
    _BRCM_SAI_LIST_MAC_NBRS            = 16,
    _BRCM_SAI_LIST_NBR_NHS             = 17,
    _BRCM_SAI_LIST_NBR_BCAST_EOBJS     = 18,
    _BRCM_SAI_LIST_VLAN_MEMBRS         = 19,
    _BRCM_SAI_LIST_LAG_ACL_BIND_POINTS = 20,
    _BRCM_SAI_LIST_ACL_TBL_GRP_MEMBR   = 21
} _brcm_sai_list_data_type_t;

typedef union _brcm_sai_table_data_s {
    _brcm_sai_mac_nbr_info_table_t *mac_nbr;
    _brcm_sai_nbr_table_info_t *nbr_table;
    _brcm_sai_nh_table_t *nh_table;
    _brcm_sai_route_table_t *route_table;
    _brcm_sai_ecmp_table_t *ecmp_table;
    _brcm_sai_scheduler_group_t *sched_group;
    _brcm_sai_unicast_arp_t *ucast_arp;
    _brcm_sai_vlan_membr_info_t *vlan_membrs;
    _brcm_sai_tunnel_map_entry_t *tnl_map_entry;
    _brcm_sai_tunnel_net_vpn_entry_t *tnl_net_vpn_entry;
} _brcm_sai_table_data_t;

typedef enum _brcm_sai_table_type_e {
    _BRCM_SAI_TABLE_RSVD            = 0,
    _BRCM_SAI_TABLE_MAC_NBR         = 1,
    _BRCM_SAI_TABLE_NBR_INFO        = 2,
    _BRCM_SAI_TABLE_NH              = 3,
    _BRCM_SAI_TABLE_ROUTE           = 4,
    _BRCM_SAI_TABLE_ECMP            = 5,
    _BRCM_SAI_TABLE_SCHED_GRP       = 6,
    _BRCM_SAI_TABLE_UCAST_ARP       = 7,
    _BRCM_SAI_TABLE_VLAN_MEMBRS     = 8,
    _BRCM_SAI_TABLE_TNL_MAP_ENT     = 9,
    _BRCM_SAI_TABLE_TNL_NET_VPN_ENT = 10
} _brcm_sai_table_type_t;

extern sai_status_t _brcm_sai_dm_init(bool wb, char *path);
extern sai_status_t _brcm_sai_dm_fini(bool wb);
extern void _brcm_sai_dm_free(void *ptr);
extern sai_status_t _brcm_sai_global_data_get(_brcm_sai_global_data_type_t type,
                                              _brcm_sai_data_t *data);
extern sai_status_t _brcm_sai_global_data_set(_brcm_sai_global_data_type_t type,
                                              _brcm_sai_data_t *data);
extern sai_status_t _brcm_sai_global_data_bump(_brcm_sai_global_data_type_t type,
                                               _brcm_sai_data_bump_t inc_dec);

extern sai_status_t _brcm_sai_indexed_data_init(_brcm_sai_indexed_data_type_t type,
                                                int entries);
extern sai_status_t _brcm_sai_indexed_data_init2(_brcm_sai_indexed_data_type_t type,
                                                 int entries1, int entries2);
extern sai_status_t _brcm_sai_indexed_data_free1(_brcm_sai_indexed_data_type_t type, int start,
                                                 int end, int count);
extern sai_status_t _brcm_sai_indexed_data_free2(_brcm_sai_indexed_data_type_t type,
                                                 int entries1, int entries2);
extern sai_status_t _brcm_sai_indexed_data_set(_brcm_sai_indexed_data_type_t type,
                                               int index[],
                                               _brcm_sai_indexed_data_t *data);
extern sai_status_t _brcm_sai_indexed_data_get(_brcm_sai_indexed_data_type_t type,
                                               int index[],
                                               _brcm_sai_indexed_data_t *data);
extern sai_status_t _brcm_sai_indexed_data_reserve_index(_brcm_sai_indexed_data_type_t type,
                                                         int start, int end, int *index);
extern sai_status_t _brcm_sai_indexed_data_reserve_index2(_brcm_sai_indexed_data_type_t type,
                                                          int start, int end, int i1, int *index);
extern void _brcm_sai_indexed_data_free_index(_brcm_sai_indexed_data_type_t type,
                                              int index);
extern void _brcm_sai_indexed_data_free_index2(_brcm_sai_indexed_data_type_t type, int i1,
                                               int index);
extern void _brcm_sai_indexed_data_clear(_brcm_sai_indexed_data_type_t type, int size);

extern sai_status_t _brcm_sai_list_init(_brcm_sai_list_data_type_t type, int id,
                                        int entries, void **base);
extern sai_status_t _brcm_sai_list_free(_brcm_sai_list_data_type_t type, int id,
                                        int entries, void *base);
extern sai_status_t _brcm_sai_list_free_v2(_brcm_sai_list_data_type_t type, char *str,
                                           int entries, void *base, void *data);
extern sai_status_t _brcm_sai_list_add(_brcm_sai_list_data_type_t type,
                                       _brcm_sai_list_data_t *base,
                                       _brcm_sai_list_key_t *key,
                                       _brcm_sai_list_data_t *data);
extern sai_status_t _brcm_sai_list_get(_brcm_sai_list_data_type_t type,
                                       _brcm_sai_list_data_t *base,
                                       _brcm_sai_list_key_t *key,
                                       _brcm_sai_list_data_t *data);
/* Delete single node in list */
extern sai_status_t _brcm_sai_list_del(_brcm_sai_list_data_type_t type,
                                       _brcm_sai_list_data_t *base,
                                       _brcm_sai_list_key_t *key);
extern sai_status_t _brcm_sai_list_traverse(_brcm_sai_list_data_type_t type,
                                            _brcm_sai_list_data_t *base,
                                            _brcm_sai_list_data_t *data);
extern sai_status_t _brcm_sai_db_table_create(_brcm_sai_table_type_t table,
                                              int entries);
extern sai_status_t _brcm_sai_db_table_entry_add(_brcm_sai_table_type_t table,
                                                 _brcm_sai_table_data_t *data);
extern sai_status_t _brcm_sai_db_table_entry_lookup(_brcm_sai_table_type_t table,
                                                    _brcm_sai_table_data_t *data);
extern sai_status_t _brcm_sai_db_table_entry_delete(_brcm_sai_table_type_t table,
                                                    _brcm_sai_table_data_t *data);
extern sai_status_t _brcm_sai_db_table_entry_getnext(_brcm_sai_table_type_t table,
                                                     _brcm_sai_table_data_t *data);
extern sai_status_t _brcm_sai_db_table_entry_restore(_brcm_sai_table_type_t table,
                                                     _brcm_sai_table_data_t *data,
                                                     void *ptr);
extern sai_status_t _brcm_sai_db_table_node_list_init(_brcm_sai_table_type_t table,
                                                      _brcm_sai_list_data_type_t list);
extern sai_status_t _brcm_sai_db_table_node_list_init_v2(_brcm_sai_table_type_t table,
                                                         _brcm_sai_list_data_type_t list);
extern sai_status_t _brcm_sai_db_table_node_list_free(_brcm_sai_table_type_t table,
                                                      _brcm_sai_list_data_type_t list);

/* Get sdk boot time in sec */
#define SAI_SWITCH_ATTR_GET_SDK_BOOT_TIME    SAI_SWITCH_ATTR_CUSTOM_RANGE_START
/*
 * Register callback fn for getting sdk shutdown time in sec.
 * This will be invoked just before switch_remove exits.
 */
#define SAI_SWITCH_ATTR_SDK_SHUT_TIME_GET_FN SAI_SWITCH_ATTR_CUSTOM_RANGE_START+1
typedef void (*brcm_sai_sdk_shutdown_time_cb_fn)(int unit, unsigned int time_sec);

/*
################################################################################
#                                SDK routines                                  #
################################################################################
*/
extern void ledproc_linkscan_cb(int unit, soc_port_t port, bcm_port_info_t *info);

/*
 * This should be last after all the public declarations
 */
#include "brcm_sai_private_apis.h"

#endif /* _BRM_SAI_COMMON */
