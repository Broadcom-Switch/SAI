/*********************************************************************
 *
 * Copyright: (c) 2017 Broadcom.
 * Broadcom Proprietary and Confidential. All rights reserved.
 *
 *********************************************************************/

#include <sai.h>
#include <brcm_sai_common.h>
#include <sys/time.h>

#define _sai_ver 0x5A1100 /* SAI 1.0.0 */

/*
################################################################################
#                     Local state - non-persistent across WB                   #
################################################################################
*/
static int dev = 0, rev = 0;
static int chip_td2 = 0;
static bool chip_th = 0;
static bool chip_th2 = 0;
static bool chip_th3 = 0;
static bool chip_thx = 0;
static bool chip_hx4 = 0;
static bool chip_td3 = 0;
static uint32_t _brcm_sai_switch_cookie = _sai_ver;
static bool _brcm_sai_switch_initied = FALSE;
static bool _brcm_sai_wb_state = FALSE;
static sai_object_id_t switchid = SAI_NULL_OBJECT_ID;
static char _init_config_file[_BRCM_SAI_FILE_PATH_BUFF_SIZE];
static char _wb_nv_file[_BRCM_SAI_FILE_PATH_BUFF_SIZE];
static char _wb_nv_path[_BRCM_SAI_FILE_PATH_BUFF_SIZE];
static char _wb_syncdb_file[_BRCM_SAI_FILE_PATH_BUFF_SIZE];
static char _wb_bcm_scache_file[_BRCM_SAI_FILE_PATH_BUFF_SIZE];
static char _system_command[_BRCM_SAI_CMD_BUFF_SIZE];
static bcm_gport_t c_port_sched[_BRCM_SAI_MAX_PORTS];
static bcm_gport_t c_l0_sched[_BRCM_SAI_MAX_PORTS][NUM_L0_NODES];
static bcm_gport_t c_l1_sched[_BRCM_SAI_MAX_PORTS][NUM_L1_NODES];
static bcm_gport_t c_Uqueue[_BRCM_SAI_MAX_PORTS][NUM_QUEUES];
static bcm_gport_t c_Mqueue[_BRCM_SAI_MAX_PORTS][NUM_QUEUES];
static bcm_gport_t c_CPUMqueue[NUM_CPU_MC_QUEUES];
static int front_panel_port_count;
static bcm_pbmp_t pbmp_all;
static bcm_pbmp_t pbmp_all_front_panel;
static bcm_pbmp_t pbmp_fp_plus_cpu;
static sai_switch_state_change_notification_fn _switch_state_change_event = NULL;
static sai_switch_shutdown_request_notification_fn _switch_shutdown_event = NULL;
static brcm_sai_sdk_shutdown_time_cb_fn _sdk_shutdown_time_notify = NULL;
static bool bcast_rule_installed = FALSE;
static bcm_field_entry_t bcast_entry = 0;
static int nhg_member_count;
static int l2_entry_count;
static int ug_ing_count;
static int ug_eg_count;
static int sdk_boot_sec = 0;
static int sdk_shut_sec = 0;
static int pre_shutdown;

/*
################################################################################
#                     Flex counter declarations                                #
################################################################################
*/
uint32 ingress_pg_flex_counter_mode_id = 0;
uint32 ingress_port_flex_counter_id_map[_BRCM_SAI_MAX_PORTS];
uint32 ingress_port_flex_counter_num_map[_BRCM_SAI_MAX_PORTS];

uint32 egress_pg_flex_counter_mode_id = 0;
uint32 egress_port_flex_counter_id_map[_BRCM_SAI_MAX_PORTS];
uint32 egress_port_flex_counter_num_map[_BRCM_SAI_MAX_PORTS];

/* Defines chip-specific L3 behavior. */
bool _brcm_sai_classic_host_tbl = FALSE;
/* TD2 and TH2 dont have this set */
bool _brcm_sai_ext_view_no_trnk = FALSE;
/* TH3 specific */
bool _brcm_sai_no_ext_host_tbl = FALSE;

extern sai_status_t _brcm_sai_port_update_queue_tc_mapping(int port, int qid, bcm_gport_t gport);
/*
################################################################################
#                             Forward declarations                             #
################################################################################
*/
STATIC sai_status_t
_brcm_sai_load_balance_init(bool set, int mode);
STATIC sai_status_t
_brcm_sai_mmu_gport_init();
STATIC void
_brcm_sai_switch_wb_state_set(bool wb);
STATIC sai_status_t
_brcm_sai_switch_fdb_miss_set(int type, int pa);
STATIC sai_status_t
_brcm_sai_switch_mac_addr_set(const sai_mac_t mac, int *intf, int *difid,
                              int *tifid);
STATIC sai_status_t
_brcm_sai_switch_mac_addr_update(const sai_mac_t mac);
STATIC sai_status_t
_brcm_sai_ecmp_members_get(int unit, int index, int intf_count,
                           bcm_if_t *info, void *user_data);
STATIC sai_status_t
_brcm_sai_l2_count_init(void);
STATIC sai_status_t
_brcm_sai_l2_count_get(int unit, bcm_l2_addr_t *l2addr, void *user_data);
STATIC sai_status_t
_brcm_sai_gather_fp_info(int unit, bcm_field_group_t group, void *user_data);

STATIC sai_status_t _brcm_sai_switch_flex_counter_restore();
STATIC bool _brcm_sai_dev_set();

/*
################################################################################
#                               Event handlers                                 #
################################################################################
*/
void
_brcm_sai_switch_event_cb(int unit, bcm_switch_event_t event,
                          uint32 arg1, uint32 arg2, uint32 arg3, void *userdata)
{
    BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_CRITICAL,
                        "%d Received switch event %d on unit %d: %x %x %x\n",
                        (int)(uint64_t)userdata, event, unit, arg1, arg2, arg3);
    switch (event)
    {
        case BCM_SWITCH_EVENT_PARITY_ERROR:
            /* FIXME: check if non fatal event then break here */
        case BCM_SWITCH_EVENT_UNCONTROLLED_SHUTDOWN:
            if (event == BCM_SWITCH_EVENT_UNCONTROLLED_SHUTDOWN &&
                _switch_shutdown_event)
            {
                _switch_shutdown_event(unit);
                break;
            }
        case BCM_SWITCH_EVENT_IO_ERROR:
        case BCM_SWITCH_EVENT_THREAD_ERROR:
        case BCM_SWITCH_EVENT_ACCESS_ERROR:
        case BCM_SWITCH_EVENT_ASSERT_ERROR:
        case BCM_SWITCH_EVENT_STABLE_FULL:
        case BCM_SWITCH_EVENT_STABLE_ERROR:
        case BCM_SWITCH_EVENT_ALARM:
            if (_switch_state_change_event)
            {
                _switch_state_change_event(unit, SAI_SWITCH_OPER_STATUS_FAILED);
            }
            break;
        default:
            break;
    }
}

void
_brcm_sai_switch_assert(const char *expr, const char *file, int line)
{
    BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_CRITICAL,
                        "ERROR: Assertion failed: (%s) at %s:%d\n",
                        expr, file, line);
    if (_switch_state_change_event)
    {
        _switch_state_change_event(0, SAI_SWITCH_OPER_STATUS_FAILED);
    }
}

/*
################################################################################
#                              Switch functions                                #
################################################################################
*/

/*
* Routine Description:
*   SDK initialization. After the call the capability attributes should be
*   ready for retrieval via sai_get_switch_attribute().
*
* Arguments:
*   [in] profile_id - Handle for the switch profile.
*   [in] switch_hardware_id - Switch hardware ID to open
*   [in/opt] firmware_path_name - Vendor specific path name of the firmware
*                                     to load
*   [in] switch_notifications - switch notification table
* Return Values:
*   SAI_STATUS_SUCCESS on success
*   Failure status code on error
*/
static sai_status_t
brcm_sai_create_switch(_Out_ sai_object_id_t* switch_id,
                       _In_ uint32_t attr_count,
                       _In_ const sai_attribute_t *attr_list)
{
    char *path;
    bcm_info_t info;
    sai_init_t init;
    _brcm_sai_data_t data;
    _brcm_sai_data_t gdata;
    sai_mac_t src_mac_addr;
    _brcm_sai_version_t ver;
    bcm_port_config_t config;
    const char *start = NULL;
    const char *k = "", *v = "";
    int ecmp_max = -1, i, p, rv;
    int wb = COLD_BOOT, val = 0, profile_id = 0;
    bool src_mac = FALSE, switch_init = FALSE;
    int vxlan_udp_port = _BRCM_SAI_VXLAN_DEFAULT_UDP_PORT;
    int ucast_miss_pa, mcast_miss_pa, bcast_miss_pa;
    int propval;
    int num_queues;
    struct timeval cur_time1, cur_time2;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_SWITCH);
    BRCM_SAI_OBJ_CREATE_PARAM_CHK(switch_id);

#ifdef SAI_DEBUG
    brcm_sai_mem_debug_init();
#endif

    ucast_miss_pa = mcast_miss_pa = bcast_miss_pa = SAI_PACKET_ACTION_FORWARD;
    pre_shutdown = 0;
    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_SWITCH_ATTR_INIT_SWITCH:
                switch_init = attr_list[i].value.booldata;
                break;
            case SAI_SWITCH_ATTR_SWITCH_PROFILE_ID:
                profile_id = attr_list[i].value.u32;
                break;
            case SAI_SWITCH_ATTR_SWITCH_HARDWARE_INFO:
            case SAI_SWITCH_ATTR_FIRMWARE_PATH_NAME:
                break;
            case SAI_SWITCH_ATTR_SWITCH_STATE_CHANGE_NOTIFY:
            	_switch_state_change_event = attr_list[i].value.ptr;
                break;
            case SAI_SWITCH_ATTR_SHUTDOWN_REQUEST_NOTIFY:
                _switch_shutdown_event = attr_list[i].value.ptr;
                break;
            case SAI_SWITCH_ATTR_FDB_EVENT_NOTIFY:
                _fdb_event = attr_list[i].value.ptr;
                break;
            case SAI_SWITCH_ATTR_PORT_STATE_CHANGE_NOTIFY:
                _port_state_change_event = attr_list[i].value.ptr;
                break;
            case SAI_SWITCH_ATTR_QUEUE_PFC_DEADLOCK_NOTIFY:
                _pfc_deadlock_event = attr_list[i].value.ptr;
                break;
            case SAI_SWITCH_ATTR_SRC_MAC_ADDRESS:
                src_mac = TRUE;
                sal_memcpy(&src_mac_addr, &(attr_list[i].value.mac), sizeof(sai_mac_t));
                break;
            case SAI_SWITCH_ATTR_FDB_UNICAST_MISS_PACKET_ACTION:
            case SAI_SWITCH_ATTR_FDB_BROADCAST_MISS_PACKET_ACTION:
            case SAI_SWITCH_ATTR_FDB_MULTICAST_MISS_PACKET_ACTION:
            {
                if ((SAI_PACKET_ACTION_FORWARD == attr_list[i].value.s32) ||
                    (SAI_PACKET_ACTION_DROP == attr_list[i].value.s32) ||
                    (SAI_PACKET_ACTION_TRAP == attr_list[i].value.s32))
                {
                    if (SAI_SWITCH_ATTR_FDB_UNICAST_MISS_PACKET_ACTION == attr_list[i].id)
                    {
                        ucast_miss_pa = attr_list[i].value.s32;
                    }
                    else if (SAI_SWITCH_ATTR_FDB_BROADCAST_MISS_PACKET_ACTION == attr_list[i].id)
                    {
                        bcast_miss_pa = attr_list[i].value.s32;
                    }
                    else
                    {
                        mcast_miss_pa = attr_list[i].value.s32;
                    }
                }
                else
                {
                    return SAI_STATUS_INVALID_PARAMETER;
                }
                break;
            }
            case SAI_SWITCH_ATTR_RESTART_WARM:
                gdata.bool_data = attr_list[i].value.booldata;
                rv = _brcm_sai_global_data_set(_BRCM_SAI_WARM_SHUT, &gdata);
                BRCM_SAI_RV_CHK(SAI_API_SWITCH, "setting ws global data", rv);
                break;
            case SAI_SWITCH_ATTR_SDK_SHUT_TIME_GET_FN: /* sdk shutdown time cb fn */
                _sdk_shutdown_time_notify = attr_list[i].value.ptr;
                break;
            case SAI_SWITCH_ATTR_VXLAN_DEFAULT_PORT:
                vxlan_udp_port = (int)attr_list[i].value.u16;
                break;
            default:
                break;
        }
    }
    if (FALSE == switch_init)
    {
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                            "Switch init not requested.\n"
                            "Connect to already initialized SDK not currently supported.\n");
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }
    BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_INFO,
                        "Host profile %d init request.\n", profile_id);

    memset(&init, 0, sizeof(init));
    if (host_services.profile_get_next_value)
    {
        /* reset KVPs */
        host_services.profile_get_next_value(profile_id, &start, NULL);
        /* get KVPs based upon profile_id */
        do
        {
            val = host_services.profile_get_next_value(profile_id, &k, &v);
            BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_DEBUG,
                                "Retreiving KVP [%s:%s]\n", k, v);
            if (strcmp(k, SAI_KEY_BOOT_TYPE) == 0)
            {
                wb = atoi(v);
            }
            else if (strcmp(k, SAI_KEY_INIT_CONFIG_FILE) == 0)
            {
                strncpy(_init_config_file, v, sizeof(_init_config_file)-1);
                init.cfg_fname = _init_config_file;
            }
            else if (strcmp(k, SAI_KEY_WARM_BOOT_READ_FILE) == 0)
            {
                strncpy(_wb_nv_file, v, sizeof(_wb_nv_file)-1);
            }
            else if (strcmp(k, SAI_KEY_WARM_BOOT_WRITE_FILE) == 0)
            {
                strncpy(_wb_nv_file, v, sizeof(_wb_nv_file)-1);
            }
            else if (strcmp(k, SAI_KEY_NUM_ECMP_MEMBERS) == 0)
            {
                ecmp_max = atoi(v);
            }
        } while (val != -1);
    }
    _brcm_sai_switch_wb_state_set(WARM_BOOT == wb);
    /* Get tmp path from file location */
    strncpy(_wb_nv_path, _wb_nv_file, sizeof(_wb_nv_path)-1);
    path = strrchr(_wb_nv_path, '/');
    if (NULL == path)
    {
        sprintf(_wb_nv_path, "%s", ".");
    }
    else
    {
        *path = '\0';
    }
    if (_brcm_sai_switch_initied)
    {
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                            "Switch already initialized - "
                            "first call remove_switch\n");
        return SAI_STATUS_FAILURE;
    }
    /* init SDK */
    if (WARM_BOOT == wb)
    {
        init.flags = 0x200000;
    }
    else if (FAST_BOOT == wb)
    {
        init.sai_flags = SAI_F_FAST_BOOT;
    }
    sprintf(_wb_bcm_scache_file, "%s%s", _wb_nv_path, _BRCM_SAI_SDK_CACHE);
    sprintf(_wb_syncdb_file, "%s%s", _wb_nv_path, SYNCDB_NVRAM_ARCHIVE);
    /* extract cache files */
    if (0 == access(_wb_nv_path, F_OK))
    {
        if (0 == access(_wb_nv_file, F_OK))
        {
            sprintf(_system_command, "%s%s%s", "tar -zxf ", _wb_nv_file,
                    " -C /");
            system (_system_command);
        }
    }
    else
    {
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                            "Can't access NV path: %s\n", _wb_nv_path);
        return SAI_STATUS_FAILURE;
    }
    /* add the cache file info */
    init.wb_fname = _wb_bcm_scache_file;

    gettimeofday(&cur_time1, NULL);
    rv = sai_driver_init(&init);
    gettimeofday(&cur_time2, NULL);
    sdk_boot_sec = cur_time2.tv_sec - cur_time1.tv_sec;
    BRCM_SAI_API_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                         "initializing SDK", rv);
    rv = bcm_info_get(0, &info);
    dev = info.device;
    rev = info.revision;
    BRCM_SAI_API_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                         "hw info get", rv);

    if (TRUE != _brcm_sai_dev_set())
    {
        return SAI_STATUS_FAILURE;
    }

    if ((WARM_BOOT != wb) &&
        (DEV_IS_THX()))
    {
        /* Init with defaults */
      _brcm_sai_cpu_pg_id = BRCM_SAI_CPU_PG_DEFAULT;
      _brcm_sai_cpu_ingress_pool_id = BRCM_SAI_CPU_INGRESS_POOL_DEFAULT;
        /* Setup MMU */
        rv = driverMMUInit(0);
        BRCM_SAI_API_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                           "Device MMU Init Failed", rv);
    }

    /* Register for switch events */
    rv = bcm_switch_event_register(0, _brcm_sai_switch_event_cb,
                                   (void*)(uint64_t)_brcm_sai_switch_cookie);
    BRCM_SAI_API_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                         "registering for switch events", rv);

    if (WARM_BOOT != wb)
    {
        /* Mirroring config */
        rv =  bcm_switch_control_set(0, bcmSwitchFlexibleMirrorDestinations, 1);
        BRCM_SAI_API_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                             "setting flex mirror dest", rv);
        rv =  bcm_switch_control_set(0, bcmSwitchDirectedMirroring, 1);
        BRCM_SAI_API_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                             "setting directed mirroring", rv);
        rv =  bcm_mirror_init(0);
        BRCM_SAI_API_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                             "mirror re-init", rv);
        rv = _brcm_sai_pol_sh_en();
        BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                            "policer sharing enable", rv);
        rv = _brcm_sai_dscp_ecn_mode_set();
        BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                            "set dscp ecn mode", rv);
    }
    /* Set L3 Egress Mode */
    rv =  bcm_switch_control_set(0, bcmSwitchL3EgressMode, 1);
    BRCM_SAI_API_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                         "setting egress mode", rv);
    if (DEV_IS_TH3())
    {
        /* Set Inter-frame gap to 12 bytes */
        rv =  bcm_switch_control_set(0, bcmSwitchShaperAdjust,
                                     _BRCM_SAI_DEFAULT_INTER_FRAME_GAP);
        BRCM_SAI_API_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                             "setting inter-frame gap", rv);
    }
    if (-1 != ecmp_max)
    {
        rv = bcm_l3_route_max_ecmp_get(0, &val);
        BRCM_SAI_API_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                             "max ecmp get", rv);
        if (val != ecmp_max)
        {
            rv = bcm_l3_route_max_ecmp_set(0, ecmp_max);
            BRCM_SAI_API_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                                 "max ecmp set", rv);
        }
    }
    else if (WARM_BOOT != wb)
    {
        /* If user has not specified then set to spec default */
        rv = bcm_l3_route_max_ecmp_set(0, 64);
        BRCM_SAI_API_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                             "max ecmp set", rv);
    }

    /* L3 feature set */
    if (DEV_IS_TD2() || DEV_IS_HX4() || DEV_IS_TH2())
    {
        _brcm_sai_classic_host_tbl = TRUE;
    }
    if (DEV_IS_TH() || DEV_IS_TD3() || DEV_IS_TH3())
    {
        /* this should be dynamically retrieved from soc_feature */
        _brcm_sai_ext_view_no_trnk = TRUE;
    }
    if (DEV_IS_TH3())
    {
        _brcm_sai_no_ext_host_tbl = TRUE;
    }

    /* Initialize data mgr module */
    rv = _brcm_sai_dm_init(WARM_BOOT == wb, _wb_nv_path);
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "initializing modules data", rv);
    rv = _brcm_sai_global_data_get(_BRCM_SAI_SWITCH_INITED, &data);
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "getting switch init global data", rv);
    /*
     * Check and abort if wb is set but the retrieved global data says that
     * the switch was not initialized previously
     */
    ver = brcm_sai_version_get();
    if (WARM_BOOT == wb)
    {
        long _ver, __ver;
        struct in_addr _inv;
        if (FALSE == data.bool_data)
        {
            BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                                "Switch was not shutdown gracefully or "
                                "DB retreival error.\n");
            return SAI_STATUS_FAILURE;
        }
        rv = _brcm_sai_global_data_get(_BRCM_SAI_VER, &gdata);
        BRCM_SAI_RV_CHK(SAI_API_SWITCH, "getting sai ver global data", rv);
        __ver = gdata.u64;
        _ver = ntohl(inet_addr(ver.brcm_sai_ver));
        _inv.s_addr = ntohl(__ver);
        if (__ver != _ver)
        {
            BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_NOTICE,
                                "WB upgrade from: %s to %s\n",
                                inet_ntoa(_inv), ver.brcm_sai_ver);
        }
        else
        {
            BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_NOTICE,
                                "WB ver: %s\n", inet_ntoa(_inv));
        }
    }

    rv = bcm_port_config_get(0, &config);
    BRCM_SAI_API_CHK(SAI_API_SWITCH, "Port config get", rv);
    BCM_PBMP_ASSIGN(pbmp_all, config.all);
    BCM_PBMP_ASSIGN(pbmp_all_front_panel, config.port);
    BCM_PBMP_COUNT(pbmp_all_front_panel, front_panel_port_count);
    BCM_PBMP_ASSIGN(pbmp_fp_plus_cpu, config.port);
    BCM_PBMP_OR(pbmp_fp_plus_cpu, config.cpu);

    rv = _brcm_sai_global_data_get(_BRCM_SAI_SW_BCAST_RULE_INSTALLED, &data);
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "getting global bcast rule installed", rv);
    bcast_rule_installed = data.bool_data;
    rv = _brcm_sai_global_data_get(_BRCM_SAI_SW_BCAST_ENTRY, &data);
    BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                        "getting global bcast entry", rv);
    bcast_entry = data.s32;

    if (WARM_BOOT != wb)
    {
        int stg = 1;
        int stp_state = BCM_STG_STP_FORWARD;
        _brcm_sai_indexed_data_t pdata;

        rv = _brcm_sai_vlan_init();
        BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                            "vlan module init", rv);
        rv = _brcm_sai_hostif_clean();
        BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                            "cleaning up hostif", rv);
        /* Init default lls gport tree */
        rv = _brcm_sai_mmu_gport_init();
        BRCM_SAI_API_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                             "initializing lls gport tree", rv)
        /* Assign port info indexes */
        BCM_PBMP_ITER(pbmp_fp_plus_cpu, p)
        {
            rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_PORT_INFO,
                                            &p, &pdata);
            BRCM_SAI_RV_CHK(SAI_API_SWITCH, "port info data get", rv);
            pdata.port_info.idx = p;
            rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_PORT_INFO,
                                            &p, &pdata);
            BRCM_SAI_RV_CHK(SAI_API_SWITCH, "port info data set", rv);
        }
        /* Setup port sw-rx-los */
        if (DEV_IS_TD2())
        {
            /* BCM_PORT_PHY_CONTROL_SOFTWARE_RX_LOS feature unavailable
             * (0xfffffff0) in TH.
             */
            BCM_PBMP_ITER(config.port, p)
            {
                rv = _brcm_sai_port_set_rxlos(p, 1);
                BRCM_SAI_API_CHK(SAI_API_SWITCH, "port rxlos set", rv);
                BCM_PBMP_PORT_ADD(_rx_los_pbm, p);
            }
        }
        /* Setup TH PFC DLD timer granularity */
        if (DEV_IS_THX())
        {
            if(!DEV_IS_TH3())
            {
                rv = bcm_switch_control_set(0, bcmSwitchPFCDeadlockDetectionTimeInterval,
                                            bcmSwitchPFCDeadlockDetectionInterval10MiliSecond);
                BRCM_SAI_API_CHK(SAI_API_SWITCH, "PFC DLD timer interval set", rv);
            }
            rv = bcm_switch_control_set(0, bcmSwitchPFCDeadlockRecoveryAction,
                                        bcmSwitchPFCDeadlockActionDrop);
            BRCM_SAI_API_CHK(SAI_API_SWITCH, "PFC DL Recovery Action set", rv);
        }

        BCM_PBMP_ITER(config.port, p)
        {
            /* Set the STP state to forward in default STG for all ports */
            rv = bcm_stg_stp_set(0, stg, p, stp_state);
            BRCM_SAI_API_CHK(SAI_API_SWITCH, "stg stp set", rv);
            /* Set Ingress Filtering on all ports */
            rv = bcm_esw_port_ifilter_set(0, p, 1);
            BRCM_SAI_API_CHK(SAI_API_SWITCH, "ingress filter set", rv);
        }

        rv = _brcm_sai_load_balance_init(TRUE, 0);
        BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                            "configuring load balancing", rv);
        rv = _brcm_sai_stats_init();
        BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                            "configuring stats", rv);
        driverPropertyCheck(2, &propval);
        if (propval == 0)
        {
            rv = bcm_l2_cache_delete_all(0);
            BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                                "removing all L2 multicast addresses", rv);
        }

        if (SAI_PACKET_ACTION_FORWARD != ucast_miss_pa)
        {
            rv = _brcm_sai_switch_fdb_miss_set(SAI_SWITCH_ATTR_FDB_UNICAST_MISS_PACKET_ACTION,
                                               ucast_miss_pa);
            BRCM_SAI_RV_CHK(SAI_API_SWITCH, "setting switch fdb miss", rv);
        }
        if (SAI_PACKET_ACTION_FORWARD != bcast_miss_pa)
        {
            rv = _brcm_sai_switch_fdb_miss_set(SAI_SWITCH_ATTR_FDB_BROADCAST_MISS_PACKET_ACTION,
                                               bcast_miss_pa);
            BRCM_SAI_RV_CHK(SAI_API_SWITCH, "setting switch fdb miss", rv);
        }
        if (SAI_PACKET_ACTION_FORWARD != mcast_miss_pa)
        {
            rv = _brcm_sai_switch_fdb_miss_set(SAI_SWITCH_ATTR_FDB_MULTICAST_MISS_PACKET_ACTION,
                                               mcast_miss_pa);
            BRCM_SAI_RV_CHK(SAI_API_SWITCH, "setting switch fdb miss", rv);
        }
        if (!DEV_IS_HX4())
        {
            /* Enable BST for Watermark stats. */
            rv = bcm_switch_control_set(0, bcmSwitchBstEnable, TRUE);
            BRCM_SAI_API_CHK(SAI_API_SWITCH, "BST Enable set", rv);
            rv = bcm_switch_control_set(0, bcmSwitchBstTrackingMode, 1);
            BRCM_SAI_API_CHK(SAI_API_SWITCH, "BST Tracking Mode set", rv);
        }
    }

    if (src_mac)
    {
        int intf, difid, tifid;
        _brcm_sai_data_t data;

        rv = _brcm_sai_global_data_get(_BRCM_SAI_SYSTEM_MAC_SET, &data);
        BRCM_SAI_RV_CHK(SAI_API_SWITCH,
                        "getting system mac state global data", rv);
        if (data.bool_data)
        {
            rv = _brcm_sai_switch_mac_addr_update(src_mac_addr);
            BRCM_SAI_RV_CHK(SAI_API_SWITCH, "updating switch mac addr", rv);
        }
        else
        {
            rv = _brcm_sai_switch_mac_addr_set(src_mac_addr, &intf,
                                               &difid, &tifid);
            BRCM_SAI_RV_CHK(SAI_API_SWITCH, "setting switch mac addr", rv);

            data.if_id = difid;
            rv = _brcm_sai_global_data_set(_BRCM_SAI_DROP_INTF_ID, &data);
            BRCM_SAI_RV_CHK(SAI_API_SWITCH,
                            "setting system drop intf global data", rv);

            data.if_id = tifid;
            rv = _brcm_sai_global_data_set(_BRCM_SAI_TRAP_INTF_ID, &data);
            BRCM_SAI_RV_CHK(SAI_API_SWITCH,
                            "setting system trap intf global data", rv);

            data.bool_data = TRUE;
            rv = _brcm_sai_global_data_set(_BRCM_SAI_SYSTEM_MAC_SET, &data);
            BRCM_SAI_RV_CHK(SAI_API_SWITCH,
                            "setting system mac state global data", rv);

            if (_brcm_sai_cpu_pool_config)
            {
                /* For devices with CPU traffic in a different pool, we need a
                   catch all policy to send traffic to CPU_PG.  */
                rv = _brcm_sai_create_trap_group_entry();
                BRCM_SAI_RV_CHK(SAI_API_SWITCH,
                                "Switch CPU traffic trap entry failed", rv);
            }
        }
        memcpy (&data.mac, &src_mac_addr, sizeof(sai_mac_t));
        rv = _brcm_sai_global_data_set(_BRCM_SAI_SYSTEM_MAC, &data);
        BRCM_SAI_RV_CHK(SAI_API_SWITCH, "setting system mac global data",
                        rv);
    }
    if (!(DEV_IS_HX4() || DEV_IS_TH3())) /* FIXME */
    {
        rv = bcm_switch_control_set(0, bcmSwitchVxlanUdpDestPortSet, vxlan_udp_port);
        BRCM_SAI_API_CHK(SAI_API_SWITCH, "vxlan udp dest port set", rv);
        data.u16 = (sai_uint16_t)vxlan_udp_port;
        rv = _brcm_sai_global_data_set(_BRCM_SAI_VXLAN_UDP_PORT, &data);
        BRCM_SAI_RV_CHK(SAI_API_SWITCH, "setting vxlan udp port global data", rv);

        rv = bcm_switch_control_set(0, bcmSwitchVxlanEntropyEnable, 0);
        BRCM_SAI_RV_CHK(SAI_API_SWITCH, "setting vxlan entropy", rv);

        rv = bcm_switch_control_set(0, bcmSwitchVxlanTunnelMissToCpu, 1);
        BRCM_SAI_RV_CHK(SAI_API_SWITCH, "setting vxlan tunnel miss to cpu", rv);

        rv = bcm_switch_control_set(0, bcmSwitchVxlanVnIdMissToCpu, 1);
        BRCM_SAI_RV_CHK(SAI_API_SWITCH, "setting vxlan VNID miss to cpu", rv);
    }
     /* rx start */
    if (!bcm_rx_active(0))
    {
        rv = bcm_rx_start(0, NULL);
        BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                            "rx start", rv);
    }

    /* Register for link events */
    rv = bcm_linkscan_register(0, _brcm_sai_link_event_cb);
    BRCM_SAI_API_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                         "registering for link events", rv);
    /* Register for L2 events */
    rv = bcm_l2_addr_register(0, _brcm_sai_fdb_event_cb,
                              (void*)(uint64_t)_brcm_sai_switch_cookie);
    BRCM_SAI_API_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                         "registering for fdb events", rv);

    if (WARM_BOOT == wb)
    {
        rv = bcm_linkscan_register(0, ledproc_linkscan_cb);
        BRCM_SAI_API_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                             "registering for LED link events", rv);
        rv = _brcm_sai_l2_count_init();
        BRCM_SAI_RV_CHK(SAI_API_SWITCH, "initializing l2 inuse count", rv);
    }
    if (DEV_IS_THX())
    {
        /* Register for PFC DLDR */
        rv = bcm_cosq_pfc_deadlock_recovery_event_register(0,
                 _brcm_cosq_pfc_deadlock_recovery_event_cb,
                 (void*)(uint64_t)_brcm_sai_switch_cookie);
        BRCM_SAI_API_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_CRITICAL,
                             "registering for PFC DLDR events", rv);
    }
    if (WARM_BOOT == wb)
    {
        _brcm_sai_switch_wb_state_set(FALSE);
    }
    _brcm_sai_switch_initied = TRUE;
    sal_assert_set(_brcm_sai_switch_assert);
    if (WARM_BOOT != wb)
    {
        bcm_gport_t qgport[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
        data.bool_data = _brcm_sai_switch_initied;
        rv = _brcm_sai_global_data_set(_BRCM_SAI_SWITCH_INITED, &data);
        BRCM_SAI_RV_CHK(SAI_API_SWITCH, "setting switch init global data", rv);

        /* Init flex counter */
        rv = _brcm_sai_switch_flex_counter_init();
        BRCM_SAI_RV_CHK(SAI_API_SWITCH, "init switch flex counter", rv);

        /* Generate and attach stat_counters for each port
           Also enable PFC Rx for all ports.
         */
        BCM_PBMP_ITER(config.port, p)
        {
            rv = _brcm_sai_switch_flex_counter_port_enable(p);
            BRCM_SAI_RV_CHK(SAI_API_SWITCH, "enable flex counter on each port", rv);
            /* Disable global flow control */
            if (DEV_IS_HX4())
            {
                bcm_port_abil_t abil_mask;
                /* on HX4, since autoneg is enabled, remove pause frame abilities in the adv */
                rv =  bcm_port_advert_get(0, p, &abil_mask);
                BRCM_SAI_RV_CHK(SAI_API_SWITCH, "Get autoneg abilities", rv);
                if (BCM_E_NONE == rv)
                {
                    abil_mask &= ~BCM_PORT_ABIL_PAUSE_TX;
                    abil_mask &= ~BCM_PORT_ABIL_PAUSE_RX;
                    rv = bcm_port_advert_set(0, p, abil_mask);
                    BRCM_SAI_RV_CHK(SAI_API_SWITCH, "Disable pause TX/RX in autoneg", rv);
                }
            }

            rv = bcm_port_pause_sym_set(0, p, 0);
            BRCM_SAI_API_CHK(SAI_API_PORT, "port pause sym set", rv);

            if (DEV_IS_TH2() || DEV_IS_TH3())
            {
                rv = bcm_port_control_set(0, p, bcmPortControlPFCReceive, 1);
                BRCM_SAI_API_CHK(SAI_API_PORT, "port control set", rv);
                rv = bcm_port_control_set(0, p, bcmPortControlPFCTransmit, 0);
            }
            else
            {
                rv = bcm_port_control_set(0, p, bcmPortControlPFCReceive, 0xff);
                BRCM_SAI_API_CHK(SAI_API_PORT, "port control set", rv);
                rv = bcm_port_control_set(0, p, bcmPortControlPFCTransmit, 0x0);
            }
            BRCM_SAI_API_CHK(SAI_API_PORT, "port control set", rv);
            if (!DEV_IS_TH3())
            {
                rv = __brcm_sai_port_pfc_queue_map_set(p, qgport);
                BRCM_SAI_RV_CHK(SAI_API_PORT, "port pfc queue map set", rv);
            }
        }
    }
    else
    {
        _brcm_sai_switch_flex_counter_restore();
    }

    *switch_id = switchid = BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_SWITCH, rev, dev, 0);

    if ((WARM_BOOT != wb) &&
        (_brcm_sai_cpu_pool_config))
    {
        rv = _brcm_sai_mmu_cpu_init();
        BRCM_SAI_RV_CHK(SAI_API_SWITCH, "CPU MMU settings init failed", rv);
    }

    num_queues = _brcm_sai_get_num_queues();
    BCM_PBMP_ITER(config.port, p)
    {

        int qid;
        bcm_gport_t gport;
        for (qid = 0; qid < num_queues; qid++)
        {
            gport = _brcm_sai_switch_port_queue_get(p, qid, _BRCM_SAI_QUEUE_TYPE_UCAST);
            rv = _brcm_sai_port_update_queue_tc_mapping(p, qid, gport);
            BRCM_SAI_API_CHK(SAI_API_PORT, "update port queue tc mapping", rv);
        }
    }

    gdata.u64 = ntohl(inet_addr(ver.brcm_sai_ver));
    rv = _brcm_sai_global_data_set(_BRCM_SAI_VER, &gdata);
    BRCM_SAI_RV_CHK(SAI_API_SWITCH, "setting sai ver global data", rv);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_SWITCH);

    return SAI_STATUS_SUCCESS;
}

/*
* Routine Description:
*   Release all resources associated with currently opened switch
*
* Arguments:
*   [in] warm_restart_hint - hint that indicates controlled warm restart.
*                            Since warm restart can be caused by crash
*                            (therefore there are no guarantees for this call),
*                            this hint is really a performance optimization.
*
* Return Values:
*   None
*/
STATIC sai_status_t
brcm_sai_shutdown_switch(_In_ bool warm_restart_hint)
{
    int p;
    sai_status_t rv;
    _brcm_sai_data_t data;
    bcm_pbmp_t pbmp;
    struct timeval cur_time1, cur_time2;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_SWITCH);
    _brcm_sai_switch_wb_state_set(warm_restart_hint);
    if (!pre_shutdown)
    {
        if (SAI_STATUS_SUCCESS != _brcm_sai_dm_fini(warm_restart_hint))
        {
            BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_CRITICAL,
                                "Error syncing out global data.\n");
        }
    }
    if (warm_restart_hint)
    {
        _brcm_sai_switch_pbmp_fp_all_get(&pbmp);
        if (DEV_IS_TD2())
        {
            BCM_PBMP_ITER(pbmp, p)
            {
                rv = _brcm_sai_port_set_rxlos(p, 0);
                if (BCM_FAILURE(rv))
                {
                    BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_CRITICAL,
                                        "Port rxlos set failed !!\n");
                }
            }
        }
        gettimeofday(&cur_time1, NULL);
        rv = bcm_switch_control_set(0, bcmSwitchControlSync, 1);
        gettimeofday(&cur_time2, NULL);
        sdk_shut_sec += cur_time2.tv_sec - cur_time1.tv_sec;
        if (BCM_FAILURE(rv))
        {
            BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_CRITICAL,
                                "SDK WB sync failed !!\n");
        }
        /* Create final WB NV storage file */
        if (0 == access(_wb_nv_path, F_OK))
        {
            sprintf(_system_command, "%s%s%s%s%s%s", "tar -zcf ", _wb_nv_file,
                    " ", _wb_bcm_scache_file, " ", _wb_syncdb_file);
            system (_system_command);
            sprintf(_system_command, "%s%s", "rm -rf ", _wb_bcm_scache_file);
            system (_system_command);
            sprintf(_system_command, "%s%s", "rm -rf ", _wb_syncdb_file);
            system (_system_command);
            sprintf(_system_command, "%s%s%s", "rm -rf ", _wb_nv_path,
                    "/sockets");
            system (_system_command);
        }
        else
        {
            BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                                "Can't access WB NV storage path: %s\n",
                                _wb_nv_file);
        }
    }

#if defined (OPENNSL_PHY_ROUTINES)
    if (DEV_IS_THX())
    {
        platform_phy_cleanup();
    }
#endif

    gettimeofday(&cur_time1, NULL);
    rv = _bcm_shutdown(0);
    gettimeofday(&cur_time2, NULL);
    sdk_shut_sec += cur_time2.tv_sec - cur_time1.tv_sec;
    BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_DEBUG,
                        "bcm shutdown returned %d(0x%x)\n", rv, rv);
    /* Delay soc shutdown to allow time for l2x thread to quit */
    sleep(1);
    gettimeofday(&cur_time1, NULL);
    rv = soc_shutdown(0);
    gettimeofday(&cur_time2, NULL);
    sdk_shut_sec += cur_time2.tv_sec - cur_time1.tv_sec;
    BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_DEBUG,
                        "Chip shutdown complete\n");

    /* _brcm_sai_switch_initied = warm_restart_hint; */
    data.bool_data = _brcm_sai_switch_initied;
    rv = _brcm_sai_global_data_set(_BRCM_SAI_SWITCH_INITED, &data);
    if (SAI_STATUS_ERROR(rv))
    {
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                            "Setting switch init global data\n");
    }

    sai_warm_shut();

    BRCM_SAI_FUNCTION_EXIT(SAI_API_SWITCH);

    return SAI_STATUS_SUCCESS;
}

/*
* Routine Description:
*   Disconnect this SAI library from the SDK.
*
* Arguments:
*   @param[in] switch_id The Switch id
* Return Values:
*   SAI_STATUS_SUCCESS on success
*   Failure status code on error
*/
STATIC sai_status_t
brcm_sai_remove_switch(_In_ sai_object_id_t switch_id)
{
    sai_status_t rv;
    _brcm_sai_data_t data;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_SWITCH);
    if (!_brcm_sai_switch_initied)
    {
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                            "Switch already shutdown - "
                            "first call create_switch\n");
        return SAI_STATUS_FAILURE;
    }
    rv = _brcm_sai_global_data_get(_BRCM_SAI_WARM_SHUT, &data);
    BRCM_SAI_RV_CHK(SAI_API_SWITCH, "getting ws global data", rv);
    rv = brcm_sai_shutdown_switch(data.bool_data);
    BRCM_SAI_RV_CHK(SAI_API_SWITCH, "shutdown switch", rv);
    if (_sdk_shutdown_time_notify)
    {
        _sdk_shutdown_time_notify(0, sdk_shut_sec);
    }
    _brcm_sai_switch_initied = FALSE;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_SWITCH);
    return SAI_STATUS_SUCCESS;
}

/*
* Routine Description:
*    Set switch attribute value
*
* Arguments:
*    [in] attr - switch attribute
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_set_switch_attribute(_In_ sai_object_id_t switch_id,
                              _In_ const sai_attribute_t *attr)
{
#define _SET_SWITCH "Set switch"
    int val;
    _brcm_sai_data_t gdata;
    sai_status_t rv = SAI_STATUS_SUCCESS;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_SWITCH);
    BRCM_SAI_SWITCH_INIT_CHECK;

    if (IS_NULL(attr))
    {
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR, "Null attr param\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    switch(attr->id)
    {
        case SAI_SWITCH_ATTR_MAX_VIRTUAL_ROUTERS:
        case SAI_SWITCH_ATTR_FDB_TABLE_SIZE:
        case SAI_SWITCH_ATTR_ON_LINK_ROUTE_SUPPORTED:
        case SAI_SWITCH_ATTR_OPER_STATUS:
        case SAI_SWITCH_ATTR_MAX_TEMP:
        case SAI_SWITCH_ATTR_SWITCHING_MODE:
            rv = SAI_STATUS_NOT_SUPPORTED;
            break;
        case SAI_SWITCH_ATTR_BCAST_CPU_FLOOD_ENABLE:
        case SAI_SWITCH_ATTR_MCAST_CPU_FLOOD_ENABLE:
            rv = SAI_STATUS_NOT_SUPPORTED;
            break;
        case SAI_SWITCH_ATTR_SRC_MAC_ADDRESS:
        {
            int intf, difid, tifid;
            _brcm_sai_data_t data;

            rv = _brcm_sai_global_data_get(_BRCM_SAI_SYSTEM_MAC_SET, &data);
            BRCM_SAI_RV_CHK(SAI_API_SWITCH,
                            "getting system mac state global data", rv);
            if (data.bool_data)
            {
                rv = _brcm_sai_switch_mac_addr_update(attr->value.mac);
                BRCM_SAI_RV_CHK(SAI_API_SWITCH, "updating switch mac addr", rv);
            }
            else
            {
                rv = _brcm_sai_switch_mac_addr_set(attr->value.mac, &intf,
                                                   &difid, &tifid);
                BRCM_SAI_RV_CHK(SAI_API_SWITCH, "setting switch mac addr", rv);

                data.if_id = difid;
                rv = _brcm_sai_global_data_set(_BRCM_SAI_DROP_INTF_ID, &data);
                BRCM_SAI_RV_CHK(SAI_API_SWITCH,
                                "setting system drop intf global data", rv);

                data.if_id = tifid;
                rv = _brcm_sai_global_data_set(_BRCM_SAI_TRAP_INTF_ID, &data);
                BRCM_SAI_RV_CHK(SAI_API_SWITCH,
                                "setting system trap intf global data", rv);

                data.bool_data = TRUE;
                rv = _brcm_sai_global_data_set(_BRCM_SAI_SYSTEM_MAC_SET, &data);
                BRCM_SAI_RV_CHK(SAI_API_SWITCH,
                                "setting system mac state global data", rv);
                if (_brcm_sai_cpu_pool_config)
                {
                  /* For devices with CPU traffic in a different pool, we need a
                     catch all policy to send traffic to CPU_PG.  */
                  rv = _brcm_sai_create_trap_group_entry();
                  BRCM_SAI_RV_CHK(SAI_API_SWITCH,
                                  "Switch CPU traffic trap entry failed", rv);
                }
            }
            memcpy (&data.mac, &(attr->value.mac), sizeof(sai_mac_t));
            rv = _brcm_sai_global_data_set(_BRCM_SAI_SYSTEM_MAC, &data);
            BRCM_SAI_RV_CHK(SAI_API_SWITCH, "setting system mac global data",
                            rv);

            break;
        }
        case SAI_SWITCH_ATTR_MAX_LEARNED_ADDRESSES:
            rv = SAI_STATUS_NOT_SUPPORTED;
            break;
        case SAI_SWITCH_ATTR_FDB_AGING_TIME:
            rv = bcm_l2_age_timer_set(0, attr->value.u32);
            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
            break;
        case SAI_SWITCH_ATTR_FDB_UNICAST_MISS_PACKET_ACTION:
        case SAI_SWITCH_ATTR_FDB_BROADCAST_MISS_PACKET_ACTION:
        case SAI_SWITCH_ATTR_FDB_MULTICAST_MISS_PACKET_ACTION:
        {
            if ((SAI_PACKET_ACTION_FORWARD == attr->value.s32) ||
                (SAI_PACKET_ACTION_DROP == attr->value.s32) ||
                (SAI_PACKET_ACTION_TRAP == attr->value.s32))
            {
                rv = _brcm_sai_switch_fdb_miss_set(attr->id, attr->value.s32);
                BRCM_SAI_RV_CHK(SAI_API_SWITCH, "setting switch fdb miss", rv);
            }
            else
            {
                return SAI_STATUS_INVALID_PARAMETER;
            }
            break;
        }
        case SAI_SWITCH_ATTR_INGRESS_ACL:
        case SAI_SWITCH_ATTR_EGRESS_ACL:
            rv = _brcm_sai_acl_obj_bind(BRCM_SAI_ATTR_PTR_OBJ(), 0,
                     BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_SWITCH, 0));
            BRCM_SAI_RV_CHK(SAI_API_SWITCH, "ACL obj switch bind", rv);
            break;
        case SAI_SWITCH_ATTR_LAG_DEFAULT_HASH_SEED:
            rv = bcm_switch_control_set(0, bcmSwitchHashSeed0,attr->value.u32);
            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
            rv = bcm_switch_control_set(0, bcmSwitchTrunkHashSet0UnicastOffset,attr->value.u32 % 16);
            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
            rv = bcm_switch_control_set(0, bcmSwitchTrunkHashSet0NonUnicastOffset,attr->value.u32 % 16);
            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
            break;

        case SAI_SWITCH_ATTR_ECMP_DEFAULT_HASH_SEED:
            rv = bcm_switch_control_set(0, bcmSwitchHashSeed0,
                                            attr->value.u32);
            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
            rv = bcm_switch_control_set(0, bcmSwitchECMPHashSet0Offset,attr->value.u32 % 16 + 68);
            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);

            break;
        case SAI_SWITCH_ATTR_LAG_DEFAULT_HASH_ALGORITHM:
        case SAI_SWITCH_ATTR_ECMP_DEFAULT_HASH_ALGORITHM:
            rv = _brcm_sai_set_switch_attribute(attr);
            break;
        case SAI_SWITCH_ATTR_ECMP_MEMBERS:
            val = attr->value.u32;
            rv = bcm_l3_route_max_ecmp_set(0, val);
            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
            break;
        case SAI_SWITCH_ATTR_ECMP_HASH_IPV4:
        case SAI_SWITCH_ATTR_ECMP_HASH_IPV6:
            rv = _brcm_sai_set_switch_attribute(attr);
            break;
        case SAI_SWITCH_ATTR_ECMP_HASH_IPV4_IN_IPV4:
        case SAI_SWITCH_ATTR_DEFAULT_TRAP_GROUP:
        case SAI_SWITCH_ATTR_QOS_DEFAULT_TC:
        case SAI_SWITCH_ATTR_QOS_DOT1P_TO_TC_MAP:
        case SAI_SWITCH_ATTR_QOS_DOT1P_TO_COLOR_MAP:
        case SAI_SWITCH_ATTR_QOS_DSCP_TO_TC_MAP:
        case SAI_SWITCH_ATTR_QOS_DSCP_TO_COLOR_MAP:
        case SAI_SWITCH_ATTR_QOS_TC_TO_QUEUE_MAP:
            rv = SAI_STATUS_NOT_SUPPORTED;
            break;
        case SAI_SWITCH_ATTR_COUNTER_REFRESH_INTERVAL:
            rv = SAI_STATUS_ATTR_NOT_IMPLEMENTED_0;
            break;
        case SAI_SWITCH_ATTR_SWITCH_SHELL_ENABLE:
            if (TRUE == attr->value.booldata)
            {
                /* Debugging support - blocking call */
                fprintf(stdout, "Hit enter to get drivshell prompt..\n");
                sai_driver_shell();
            }
            break;
        case SAI_SWITCH_ATTR_CPU_PORT:
            rv = SAI_STATUS_NOT_SUPPORTED;
            break;
        case SAI_SWITCH_ATTR_MIRROR_TC:
            rv = _brcm_sai_set_switch_attribute(attr);
            break;
        case SAI_SWITCH_ATTR_RESTART_WARM:
            gdata.bool_data = attr->value.booldata;
            rv = _brcm_sai_global_data_set(_BRCM_SAI_WARM_SHUT, &gdata);
            BRCM_SAI_RV_CHK(SAI_API_SWITCH, "setting ws global data", rv);
            break;
        case SAI_SWITCH_ATTR_SWITCH_STATE_CHANGE_NOTIFY:
        	_switch_state_change_event = attr->value.ptr;
            break;
        case SAI_SWITCH_ATTR_SHUTDOWN_REQUEST_NOTIFY:
            _switch_shutdown_event = attr->value.ptr;
            break;
        case SAI_SWITCH_ATTR_FDB_EVENT_NOTIFY:
            _fdb_event = attr->value.ptr;
            break;
        case SAI_SWITCH_ATTR_PORT_STATE_CHANGE_NOTIFY:
            _port_state_change_event = attr->value.ptr;
            break;
        case SAI_SWITCH_ATTR_PFC_DLR_PACKET_ACTION:
        {
            int pa = bcmSwitchPFCDeadlockActionDrop;

            if (SAI_PACKET_ACTION_FORWARD == attr->value.u32)
            {
                pa = bcmSwitchPFCDeadlockActionTransmit;
            }
            else if (SAI_PACKET_ACTION_DROP != attr->value.u32)
            {
                rv = SAI_STATUS_INVALID_ATTR_VALUE_0;
            }
            rv = bcm_switch_control_set(0, bcmSwitchPFCDeadlockRecoveryAction, pa);
            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
            break;
        }
        case SAI_SWITCH_ATTR_PFC_TC_DLD_INTERVAL:
        {
            int c, count;
            _brcm_sai_data_t d_data, r_data;
            bcm_cosq_pfc_deadlock_config_t deadlock_config;

            rv = bcm_switch_object_count_get(0, bcmSwitchObjectPFCDeadlockCosMax,
                                             &count);
            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
            if (0 == attr->value.maplist.count || count < attr->value.maplist.count)
            {
                rv = SAI_STATUS_INVALID_ATTR_VALUE_0;
            }
            else
            {
                rv = _brcm_sai_global_data_get(_BRCM_SAI_PFC_DLD_TIMERS, &d_data);
                BRCM_SAI_RV_CHK(SAI_API_SWITCH, "getting system mac global data", rv);
                rv = _brcm_sai_global_data_get(_BRCM_SAI_PFC_DLR_TIMERS, &r_data);
                BRCM_SAI_RV_CHK(SAI_API_SWITCH, "getting system mac global data", rv);
                for (c = 0; c < attr->value.maplist.count; c++)
                {
                    bcm_cosq_pfc_deadlock_config_t_init(&deadlock_config);
                    deadlock_config.detection_timer = attr->value.maplist.list[c].value;
                    deadlock_config.recovery_timer = r_data.cos_timers[attr->value.maplist.list[c].key];
                    rv = bcm_cosq_pfc_deadlock_config_set(0, attr->value.maplist.list[c].key,
                                                          &deadlock_config);
                    BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                    d_data.cos_timers[attr->value.maplist.list[c].key] = attr->value.maplist.list[c].value;
                }
                rv = _brcm_sai_global_data_set(_BRCM_SAI_PFC_DLD_TIMERS, &d_data);
                BRCM_SAI_RV_CHK(SAI_API_SWITCH, "getting system mac global data", rv);
                rv = _brcm_sai_global_data_set(_BRCM_SAI_PFC_DLR_TIMERS, &r_data);
                BRCM_SAI_RV_CHK(SAI_API_SWITCH, "getting system mac global data", rv);
            }
            break;
        }
        case SAI_SWITCH_ATTR_PFC_TC_DLR_INTERVAL:
        {
            int c, count;
            _brcm_sai_data_t d_data, r_data;
            bcm_cosq_pfc_deadlock_config_t deadlock_config;

            rv = bcm_switch_object_count_get(0, bcmSwitchObjectPFCDeadlockCosMax,
                                             &count);
            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
            if (0 == attr->value.maplist.count || count < attr->value.maplist.count)
            {
                rv = SAI_STATUS_INVALID_ATTR_VALUE_0;
            }
            else
            {
                rv = _brcm_sai_global_data_get(_BRCM_SAI_PFC_DLD_TIMERS, &d_data);
                BRCM_SAI_RV_CHK(SAI_API_SWITCH, "getting system mac global data", rv);
                rv = _brcm_sai_global_data_get(_BRCM_SAI_PFC_DLR_TIMERS, &r_data);
                BRCM_SAI_RV_CHK(SAI_API_SWITCH, "getting system mac global data", rv);
                for (c = 0; c < attr->value.maplist.count; c++)
                {
                    if (1000 < attr->value.maplist.list[c].value)
                    {
                        rv = SAI_STATUS_INVALID_ATTR_VALUE_0;
                        break;
                    }
                    bcm_cosq_pfc_deadlock_config_t_init(&deadlock_config);
                    deadlock_config.recovery_timer = attr->value.maplist.list[c].value;
                    deadlock_config.detection_timer = d_data.cos_timers[attr->value.maplist.list[c].key];
                    rv = bcm_cosq_pfc_deadlock_config_set(0, attr->value.maplist.list[c].key,
                                                          &deadlock_config);
                    BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                    r_data.cos_timers[attr->value.maplist.list[c].key] = attr->value.maplist.list[c].value;
                }
                rv = _brcm_sai_global_data_set(_BRCM_SAI_PFC_DLD_TIMERS, &d_data);
                BRCM_SAI_RV_CHK(SAI_API_SWITCH, "getting system mac global data", rv);
                rv = _brcm_sai_global_data_set(_BRCM_SAI_PFC_DLR_TIMERS, &r_data);
                BRCM_SAI_RV_CHK(SAI_API_SWITCH, "getting system mac global data", rv);
            }
            break;
        }
        case SAI_SWITCH_ATTR_SDK_SHUT_TIME_GET_FN: /* sdk shutdown time cb fn */
            _sdk_shutdown_time_notify = attr->value.ptr;
            break;
        case SAI_SWITCH_ATTR_PRE_SHUTDOWN:
            rv = _brcm_sai_global_data_get(_BRCM_SAI_WARM_SHUT, &gdata);
            BRCM_SAI_RV_CHK(SAI_API_SWITCH, "getting ws global data", rv);
            if (gdata.bool_data)
            {
                _brcm_sai_switch_wb_state_set(gdata.bool_data);
                if (SAI_STATUS_SUCCESS != _brcm_sai_dm_fini(gdata.bool_data))
                {
                    BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_CRITICAL,
                                        "Error syncing out global data.\n");
                }
                pre_shutdown = 1;
            }
            break;
        case SAI_SWITCH_ATTR_VXLAN_DEFAULT_PORT:
            rv = bcm_switch_control_set(0, bcmSwitchVxlanUdpDestPortSet, (int)attr->value.u16);
            BRCM_SAI_API_CHK(SAI_API_SWITCH, "vxlan udp dest port set", rv);
            gdata.u16 = attr->value.u16;
            rv = _brcm_sai_global_data_set(_BRCM_SAI_VXLAN_UDP_PORT, &gdata);
            BRCM_SAI_RV_CHK(SAI_API_SWITCH, "setting vxlan udp port global data", rv);
            break;
        default:
            BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                                "Unknown switch attribute %d passed\n",
                                attr->id);
            rv = SAI_STATUS_INVALID_PARAMETER;;
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_SWITCH);
    return rv;
#undef _SET_SWITCH
}

/*
* Routine Description:
*    Get switch attribute value
*
* Arguments:
*    [in] attr_count - number of switch attributes
*    [inout] attr_list - array of switch attributes
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_get_switch_attribute(_In_ sai_object_id_t switch_id,
                              _In_ uint32_t attr_count,
                              _Inout_ sai_attribute_t *attr_list)
{
#define _GET_SWITCH "Get switch"
    sai_status_t rv = SAI_STATUS_SUCCESS;
    int i, j, val, max, used, acl_resource_count;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_SWITCH);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_GET_ATTRIB_PARAM_CHK;

    for (i = 0; i<attr_count; i++)
    {
        switch(attr_list[i].id)
        {
            case SAI_SWITCH_ATTR_PORT_NUMBER:
                /* Note: currently using only front panel ports, no hg ports */
                attr_list[i].value.u32 = _brcm_sai_switch_fp_port_count();
                break;
            case SAI_SWITCH_ATTR_PORT_LIST:
                rv = _brcm_sai_get_switch_attribute(attr_count, attr_list);
                break;
            case SAI_SWITCH_ATTR_CPU_PORT:
                BRCM_SAI_ATTR_LIST_OBJ(i) =
                    BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_PORT, 0);
                break;
            case SAI_SWITCH_ATTR_MAX_VIRTUAL_ROUTERS:
                rv = _brcm_sai_get_switch_attribute(attr_count, attr_list);
                break;
            case SAI_SWITCH_ATTR_FDB_TABLE_SIZE:
            case SAI_SWITCH_ATTR_ON_LINK_ROUTE_SUPPORTED:
            case SAI_SWITCH_ATTR_OPER_STATUS:
            case SAI_SWITCH_ATTR_MAX_TEMP:
                rv = SAI_STATUS_NOT_SUPPORTED;
                break;
            case SAI_SWITCH_ATTR_ACL_TABLE_MINIMUM_PRIORITY:
            case SAI_SWITCH_ATTR_ACL_TABLE_MAXIMUM_PRIORITY:
                rv = SAI_STATUS_NOT_SUPPORTED;
                break;
            case SAI_SWITCH_ATTR_ACL_ENTRY_MINIMUM_PRIORITY:
                rv = _brcm_sai_get_switch_attribute(attr_count, attr_list);
                break;
            case SAI_SWITCH_ATTR_ACL_ENTRY_MAXIMUM_PRIORITY:
                rv = _brcm_sai_get_switch_attribute(attr_count, attr_list);
                break;
            case SAI_SWITCH_ATTR_QOS_MAX_NUMBER_OF_TRAFFIC_CLASSES:
                attr_list[i].value.u8 = _brcm_sai_get_num_queues();
                break;
            case SAI_SWITCH_ATTR_DEFAULT_STP_INST_ID:
                rv = SAI_STATUS_ATTR_NOT_IMPLEMENTED_0;
                break;
            case SAI_SWITCH_ATTR_DEFAULT_VIRTUAL_ROUTER_ID:
                BRCM_SAI_ATTR_LIST_OBJ(i) =
                    BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_VIRTUAL_ROUTER, 0);
                break;
            case SAI_SWITCH_ATTR_QOS_MAX_NUMBER_OF_SCHEDULER_GROUP_HIERARCHY_LEVELS:
                attr_list[i].value.u32 = DEV_IS_TD2() ? _BRCM_SAI_MAX_HIERARCHY_TD2 :
                                         _BRCM_SAI_MAX_HIERARCHY_TH;
                /* TD2:
                 * 0: Root,
                 * 1: L0,
                 * 2: L1.
                 * Note: L2 represents queues not scheduler.
                 *
                 * TH:
                 * 0: Root,
                 * 1: L0,
                 * Note: L1 represents queues not scheduler.
                 */
                break;
            case SAI_SWITCH_ATTR_QOS_MAX_NUMBER_OF_SCHEDULER_GROUPS_PER_HIERARCHY_LEVEL:
                attr_list[i].value.u32 = _BRCM_SAI_MAX_NODES;
                break;
            case SAI_SWITCH_ATTR_QOS_MAX_NUMBER_OF_CHILDS_PER_SCHEDULER_GROUP:
                attr_list[i].value.u32 = 16;
                break;
            case SAI_SWITCH_ATTR_LAG_MEMBERS:
            {
                bcm_trunk_chip_info_t trunk_info;

                rv = bcm_trunk_chip_info_get(0, &trunk_info);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _GET_SWITCH, rv,
                                      attr_list[i].id);
                attr_list[i].value.u32 = trunk_info.trunk_ports_max;
                break;
            }
            case SAI_SWITCH_ATTR_NUMBER_OF_LAGS:
            {
                bcm_trunk_chip_info_t trunk_info;

                rv = bcm_trunk_chip_info_get(0, &trunk_info);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _GET_SWITCH, rv,
                                      attr_list[i].id);
                attr_list[i].value.u32 = trunk_info.trunk_group_count;
                break;
            }
            case SAI_SWITCH_ATTR_TOTAL_BUFFER_SIZE:
                rv = driverMmuInfoGet(0, &attr_list[i].value.u32);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _GET_SWITCH, rv,
                                      attr_list[i].id);
                break;
            case SAI_SWITCH_ATTR_INGRESS_BUFFER_POOL_NUM:
                rv = driverMmuInfoGet(1, &attr_list[i].value.u32);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _GET_SWITCH, rv,
                                      attr_list[i].id);
                break;
            case SAI_SWITCH_ATTR_EGRESS_BUFFER_POOL_NUM:
                rv = driverMmuInfoGet(2, &attr_list[i].value.u32);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _GET_SWITCH, rv,
                                      attr_list[i].id);
                break;
            case SAI_SWITCH_ATTR_RESTART_TYPE:
                attr_list[i].value.s32 = SAI_SWITCH_RESTART_TYPE_PLANNED;
                break;
            case SAI_SWITCH_ATTR_MIN_PLANNED_RESTART_INTERVAL:
                attr_list[i].value.u32 = 10*1000; /* 10 sec */
                break;
            case SAI_SWITCH_ATTR_NV_STORAGE_SIZE:
                attr_list[i].value.u32 = 10*1000; /* 10MB. */
                break;
            case SAI_SWITCH_ATTR_SWITCHING_MODE:
            case SAI_SWITCH_ATTR_BCAST_CPU_FLOOD_ENABLE:
            case SAI_SWITCH_ATTR_MCAST_CPU_FLOOD_ENABLE:
            case SAI_SWITCH_ATTR_ECMP_HASH:
            case SAI_SWITCH_ATTR_LAG_HASH:
                rv = SAI_STATUS_NOT_SUPPORTED;
                break;
            case SAI_SWITCH_ATTR_SRC_MAC_ADDRESS:
                if ((rv = _brcm_sai_system_mac_get(&attr_list[i].value.mac))
                    != SAI_STATUS_SUCCESS)
                {
                    BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                                        "Error retreiving system mac.\n");
                }
                break;
            case SAI_SWITCH_ATTR_MAX_LEARNED_ADDRESSES:
                rv = SAI_STATUS_NOT_SUPPORTED;
                break;
            case SAI_SWITCH_ATTR_FDB_AGING_TIME:
                rv = bcm_l2_age_timer_get(0, &val);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _GET_SWITCH, rv,
                                      attr_list[i].id);
                attr_list[i].value.u32 = (uint32_t)val;
                break;
            case SAI_SWITCH_ATTR_FDB_UNICAST_MISS_PACKET_ACTION:
            case SAI_SWITCH_ATTR_FDB_BROADCAST_MISS_PACKET_ACTION:
            case SAI_SWITCH_ATTR_FDB_MULTICAST_MISS_PACKET_ACTION:
                rv = SAI_STATUS_NOT_SUPPORTED;
                break;
            case SAI_SWITCH_ATTR_ECMP_DEFAULT_HASH_SEED:
                rv = bcm_switch_control_get(0, bcmSwitchHashSeed0, &val);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _GET_SWITCH, rv,
                                      attr_list[i].id);
                attr_list[i].value.u32 = (uint32_t)val;
                break;
            case SAI_SWITCH_ATTR_LAG_DEFAULT_HASH_ALGORITHM:
                rv = bcm_switch_control_get(0, bcmSwitchHashField0Config,
                                                &val);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _GET_SWITCH, rv,
                                      attr_list[i].id);
                attr_list[i].value.u32 = (BCM_HASH_FIELD_CONFIG_CRC32LO == val) ?
                                         SAI_HASH_ALGORITHM_CRC :
                                         SAI_HASH_ALGORITHM_XOR;
                break;
            case SAI_SWITCH_ATTR_ECMP_DEFAULT_HASH_ALGORITHM:
                rv = bcm_switch_control_get(0, bcmSwitchHashField0Config1,
                                                &val);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _GET_SWITCH, rv,
                                      attr_list[i].id);
                attr_list[i].value.u32 = (BCM_HASH_FIELD_CONFIG_CRC32HI == val) ?
                                         SAI_HASH_ALGORITHM_CRC :
                                         SAI_HASH_ALGORITHM_XOR;
                break;
            case SAI_SWITCH_ATTR_ECMP_MEMBERS:
                rv = bcm_l3_route_max_ecmp_get(0, &val);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _GET_SWITCH, rv,
                                      attr_list[i].id);
                attr_list[i].value.u32 = val;
                break;
            case SAI_SWITCH_ATTR_NUMBER_OF_ECMP_GROUPS:
                 rv = bcm_l3_route_max_ecmp_get(0, &val);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _GET_SWITCH, rv,
                                      attr_list[i].id);
                attr_list[i].value.u32 = _brcm_sai_get_max_ecmp_members()/val;
                break;
            case SAI_SWITCH_ATTR_DEFAULT_TRAP_GROUP:
                BRCM_SAI_ATTR_LIST_OBJ(i) =
                    BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_HOSTIF_TRAP_GROUP, 0);
                break;
            case SAI_SWITCH_ATTR_QOS_DEFAULT_TC:
            case SAI_SWITCH_ATTR_QOS_DOT1P_TO_TC_MAP:
            case SAI_SWITCH_ATTR_QOS_DOT1P_TO_COLOR_MAP:
            case SAI_SWITCH_ATTR_QOS_DSCP_TO_TC_MAP:
            case SAI_SWITCH_ATTR_QOS_DSCP_TO_COLOR_MAP:
            case SAI_SWITCH_ATTR_QOS_TC_TO_QUEUE_MAP:
                rv = SAI_STATUS_NOT_SUPPORTED;
                break;
            case SAI_SWITCH_ATTR_COUNTER_REFRESH_INTERVAL:
                rv = SAI_STATUS_ATTR_NOT_IMPLEMENTED_0;
                break;
            case SAI_SWITCH_ATTR_SWITCH_SHELL_ENABLE:
                rv = SAI_STATUS_INVALID_ATTRIBUTE_0 + i;
                break;
            case SAI_SWITCH_ATTR_MIRROR_TC:
                rv = _brcm_sai_get_switch_attribute(attr_count, attr_list);
                break;
            case SAI_SWITCH_ATTR_DEFAULT_1Q_BRIDGE_ID:
                BRCM_SAI_ATTR_LIST_OBJ(i) =
                    BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_BRIDGE, 0);
                break;
            case SAI_SWITCH_ATTR_DEFAULT_VLAN_ID:
                BRCM_SAI_ATTR_LIST_OBJ(i) =
                    BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_VLAN, 1);
                break;
            case SAI_SWITCH_ATTR_QOS_NUM_LOSSLESS_QUEUES:
                rv = bcm_switch_object_count_get(0, bcmSwitchObjectPFCDeadlockCosMax,
                                                 &val);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _GET_SWITCH, rv,
                                      attr_list[i].id);
                attr_list[i].value.u32 = val;
                break;
            case SAI_SWITCH_ATTR_PFC_TC_DLD_INTERVAL_RANGE:
                attr_list[i].value.u32range.min = 10;
                attr_list[i].value.u32range.max = 150;
                break;
            case SAI_SWITCH_ATTR_PFC_TC_DLR_INTERVAL_RANGE:
                attr_list[i].value.u32range.min = 10;
                attr_list[i].value.u32range.max = 1000;
                break;
            case SAI_SWITCH_ATTR_PFC_DLR_PACKET_ACTION:
            {
                int pa = bcmSwitchPFCDeadlockActionDrop;

                rv = bcm_switch_control_get(0, bcmSwitchPFCDeadlockRecoveryAction, &pa);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _GET_SWITCH, rv,
                                      attr_list[i].id);
                if (bcmSwitchPFCDeadlockActionTransmit == pa)
                {
                    attr_list[i].value.u32 = SAI_PACKET_ACTION_FORWARD;
                }
                else
                {
                    attr_list[i].value.u32 = SAI_PACKET_ACTION_DROP;
                }
                break;
            }
            case SAI_SWITCH_ATTR_PFC_TC_DLD_INTERVAL:
            {
                sai_status_t rv1;
                int c, count, limit;
                bcm_cosq_pfc_deadlock_config_t deadlock_config;

                if (0 == attr_list[i].value.maplist.count)
                {
                    rv = SAI_STATUS_INVALID_PARAMETER;
                    break;
                }
                rv = bcm_switch_object_count_get(0, bcmSwitchObjectPFCDeadlockCosMax,
                                                 &count);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _GET_SWITCH, rv, attr_list[i].id);
                limit = count-1;
                if (attr_list[i].value.maplist.count < count)
                {
                    limit = attr_list[i].value.maplist.count-1;
                    rv = SAI_STATUS_BUFFER_OVERFLOW;
                }
                attr_list[i].value.maplist.count = count;
                for (c = 0; c<=limit; c++)
                {
                    rv1 = bcm_cosq_pfc_deadlock_config_get(0, attr_list[i].value.maplist.list[c].key,
                                                           &deadlock_config);
                    BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _GET_SWITCH, rv1, attr_list[i].id);
                    attr_list[i].value.maplist.list[c].value = deadlock_config.detection_timer;
                }
                break;
            }
            case SAI_SWITCH_ATTR_PFC_TC_DLR_INTERVAL:
            {
                sai_status_t rv1;
                int c, count, limit;
                bcm_cosq_pfc_deadlock_config_t deadlock_config;

                if (0 == attr_list[i].value.maplist.count)
                {
                    rv = SAI_STATUS_INVALID_PARAMETER;
                    break;
                }
                rv = bcm_switch_object_count_get(0, bcmSwitchObjectPFCDeadlockCosMax,
                                                 &count);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _GET_SWITCH, rv, attr_list[i].id);
                limit = count;
                if (attr_list[i].value.maplist.count < count)
                {
                    limit = attr_list[i].value.maplist.count-1;
                    rv = SAI_STATUS_BUFFER_OVERFLOW;
                }
                for (c = 0; c <= limit; c++)
                {
                    rv1 = bcm_cosq_pfc_deadlock_config_get(0, attr_list[i].value.maplist.list[c].key,
                                                           &deadlock_config);
                    BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _GET_SWITCH, rv1, attr_list[i].id);
                    attr_list[i].value.maplist.list[c].value = deadlock_config.recovery_timer;
                }
                break;
            }
            case SAI_SWITCH_ATTR_AVAILABLE_IPV4_ROUTE_ENTRY:
            {
                rv = bcm_switch_object_count_get(0, bcmSwitchObjectL3RouteV4RoutesMax, &max);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _GET_SWITCH, rv, attr_list[i].id);
                rv = bcm_switch_object_count_get(0, bcmSwitchObjectL3RouteV4RoutesUsed, &used);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _GET_SWITCH, rv, attr_list[i].id);
                attr_list[i].value.u32 = max - used;
                break;
            }
            case SAI_SWITCH_ATTR_AVAILABLE_IPV6_ROUTE_ENTRY:
            {
                rv = bcm_switch_object_count_get(0, bcmSwitchObjectL3RouteV6Routes64bMax, &max);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _GET_SWITCH, rv, attr_list[i].id);
                if (0 == max)
                {
                    rv = bcm_switch_object_count_get(0, bcmSwitchObjectL3RouteV6Routes128bMax, &max);
                    BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _GET_SWITCH, rv, attr_list[i].id);
                    rv = bcm_switch_object_count_get(0, bcmSwitchObjectL3RouteV6Routes128bUsed, &used);
                    BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _GET_SWITCH, rv, attr_list[i].id);
                }
                else
                {
                    rv = bcm_switch_object_count_get(0, bcmSwitchObjectL3RouteV6Routes64bUsed, &used);
                    BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _GET_SWITCH, rv, attr_list[i].id);
                }
                attr_list[i].value.u32 = max - used;
                break;
            }
            case SAI_SWITCH_ATTR_AVAILABLE_IPV4_NEXTHOP_ENTRY:
            case SAI_SWITCH_ATTR_AVAILABLE_IPV6_NEXTHOP_ENTRY:
            {
                _brcm_sai_data_t data;

                rv = bcm_switch_object_count_get(0, bcmSwitchObjectL3EgressMax, &max);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _GET_SWITCH, rv, attr_list[i].id);
                rv = _brcm_sai_global_data_get(_BRCM_SAI_EGRESS_INUSE_COUNT, &data);
                BRCM_SAI_RV_CHK(SAI_API_SWITCH, "global egress inuse count", rv);
                attr_list[i].value.u32 = max - data.u32;
                break;
            }
            case SAI_SWITCH_ATTR_AVAILABLE_IPV4_NEIGHBOR_ENTRY:
            {
                rv = bcm_switch_object_count_get(0, bcmSwitchObjectL3HostV4Max, &max);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _GET_SWITCH, rv, attr_list[i].id);
                max /= 2;   /* FIXME:  For some reason, we are only able to populate 1/2
                             * the L3 Host entries returned by the SDK.
                             */
                rv = bcm_switch_object_count_get(0, bcmSwitchObjectL3HostCurrent, &used);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _GET_SWITCH, rv, attr_list[i].id);
                attr_list[i].value.u32 = max - used;
                break;
            }
            case SAI_SWITCH_ATTR_AVAILABLE_IPV6_NEIGHBOR_ENTRY:
            {
                rv = bcm_switch_object_count_get(0, bcmSwitchObjectL3HostV6Max, &max);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _GET_SWITCH, rv, attr_list[i].id);
                max /= 2;   /* FIXME:  For some reason, we are only able to populate 1/2
                             * the L3 Host entries returned by the SDK.
                             */
                rv = bcm_switch_object_count_get(0, bcmSwitchObjectL3HostCurrent, &used);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _GET_SWITCH, rv, attr_list[i].id);
                attr_list[i].value.u32 = (max - used) / 2;
                break;
            }
            case SAI_SWITCH_ATTR_AVAILABLE_NEXT_HOP_GROUP_ENTRY:
            {
                rv = bcm_l3_route_max_ecmp_get(0, &max);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _GET_SWITCH, rv, attr_list[i].id);
                max = _brcm_sai_get_max_ecmp_members()/max;
                rv = bcm_switch_object_count_get(0, bcmSwitchObjectEcmpCurrent, &used);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _GET_SWITCH, rv, attr_list[i].id);
                attr_list[i].value.u32 = max - used;
                break;
            }
            case SAI_SWITCH_ATTR_AVAILABLE_NEXT_HOP_GROUP_MEMBER_ENTRY:
            {
                max = _brcm_sai_get_max_ecmp_members();
                nhg_member_count = 0;
                rv = bcm_l3_egress_multipath_traverse(0,_brcm_sai_ecmp_members_get, NULL);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _GET_SWITCH, rv, attr_list[i].id);
                attr_list[i].value.u32 = max - nhg_member_count;
                break;
            }
            case SAI_SWITCH_ATTR_AVAILABLE_FDB_ENTRY:
            {
                _brcm_sai_data_t data;

                if (DEV_IS_HX4())
                {
                    max = _BRCM_HX4_MAX_FDB_ENTRIES;
                }
                else
                {
                    rv = bcm_switch_object_count_get(0, bcmSwitchObjectL2EntryMax, &max);
                    BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _GET_SWITCH, rv, attr_list[i].id);
                }
                rv = _brcm_sai_global_data_get(_BRCM_SAI_FDB_COUNT, &data);
                BRCM_SAI_RV_CHK(SAI_API_SWITCH, "fdb global count get", rv);
                attr_list[i].value.u32 = max - data.u32;
                break;
            }
            case SAI_SWITCH_ATTR_AVAILABLE_ACL_TABLE:
            {
                acl_resource_count = 2 * (SAI_ACL_BIND_POINT_TYPE_SWITCH + 1);
                if (attr_list[i].value.aclresource.count < acl_resource_count)
                {
                    rv =  SAI_STATUS_BUFFER_OVERFLOW;
                    acl_resource_count = attr_list[i].value.aclresource.count ;
                }
                attr_list[i].value.aclresource.count =
                  2 * (SAI_ACL_BIND_POINT_TYPE_SWITCH + 1);
                if (SAI_STATUS_SUCCESS == rv)
                {
                    ug_ing_count = ug_eg_count = 0;
                    rv = bcm_field_group_traverse(0, _brcm_sai_gather_fp_info, NULL);
                    BRCM_SAI_API_CHK(SAI_API_SWITCH, "field group traverse", rv);
                    for (j = 0; j < acl_resource_count; j++)
                    {
                        if (SAI_ACL_BIND_POINT_TYPE_SWITCH < j)
                        {
                            attr_list[i].value.aclresource.list[j].stage = SAI_ACL_STAGE_EGRESS;
                        }
                        else
                        {
                            attr_list[i].value.aclresource.list[j].stage = SAI_ACL_STAGE_INGRESS;
                        }
                        /* Dividing by 2 since InPorts will force a Group to be Double. */
                        attr_list[i].value.aclresource.list[j].bind_point =
                            SAI_ACL_BIND_POINT_TYPE_PORT + (j % (acl_resource_count / 2));
                        if (SAI_ACL_STAGE_INGRESS == attr_list[i].value.aclresource.list[j].stage)
                        {
                            if (SAI_ACL_BIND_POINT_TYPE_PORT ==
                                attr_list[i].value.aclresource.list[j].bind_point)
                            {
                                attr_list[i].value.aclresource.list[j].avail_num =
                                    (_BRCM_SAI_MAX_ACL_INGRESS_GROUPS - ug_ing_count) / 2;
                            }
                            else
                            {
                                attr_list[i].value.aclresource.list[j].avail_num =
                                    _BRCM_SAI_MAX_ACL_INGRESS_GROUPS - ug_ing_count;
                            }
                        }
                        else
                        {
                            attr_list[i].value.aclresource.list[j].avail_num =
                                _BRCM_SAI_MAX_ACL_EGRESS_GROUPS - ug_eg_count;
                        }
                    }
                }
                break;
            }
            case SAI_SWITCH_ATTR_AVAILABLE_ACL_TABLE_GROUP:
            {
                _brcm_sai_data_t data;

                rv = _brcm_sai_global_data_get(_BRCM_SAI_ACL_TBL_GRPS_COUNT, &data);
                BRCM_SAI_RV_CHK(SAI_API_SWITCH, "acl table group global count get", rv);
                acl_resource_count = 2 * (SAI_ACL_BIND_POINT_TYPE_SWITCH + 1);
                if (attr_list[i].value.aclresource.count < acl_resource_count)
                {
                    rv =  SAI_STATUS_BUFFER_OVERFLOW;
                    acl_resource_count = attr_list[i].value.aclresource.count ;
                }
                attr_list[i].value.aclresource.count =
                  2 * (SAI_ACL_BIND_POINT_TYPE_SWITCH + 1);
                if (SAI_STATUS_SUCCESS == rv)
                {
                    for (j = 0; j < acl_resource_count; j++)
                    {
                        if (SAI_ACL_BIND_POINT_TYPE_SWITCH < j)
                        {
                            attr_list[i].value.aclresource.list[j].stage = SAI_ACL_STAGE_EGRESS;
                        }
                        else
                        {
                            attr_list[i].value.aclresource.list[j].stage = SAI_ACL_STAGE_INGRESS;
                        }
                        attr_list[i].value.aclresource.list[j].bind_point =
                            SAI_ACL_BIND_POINT_TYPE_PORT + (j % (acl_resource_count / 2));
                        attr_list[i].value.aclresource.list[j].avail_num =
                            _BRCM_SAI_MAX_ACL_TABLE_GROUPS - 1 - data.u32;
                    }
                }
                break;
            }
            case SAI_SWITCH_ATTR_GET_SDK_BOOT_TIME: /* sdk boot time in sec */
                attr_list[i].value.u32 = sdk_boot_sec;
                break;
            case SAI_SWITCH_ATTR_VXLAN_DEFAULT_PORT:
            {
                _brcm_sai_data_t data;

                rv = _brcm_sai_global_data_get(_BRCM_SAI_VXLAN_UDP_PORT, &data);
                BRCM_SAI_RV_CHK(SAI_API_SWITCH, "getting vxlan udp port global data", rv);
                attr_list[i].value.u16 = data.u16;
                break;
            }
            default:
                BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                                    "Unknown switch attribute %d passed.\n",
                                    attr_list[i].id);
                rv = SAI_STATUS_INVALID_PARAMETER;
                break;
        }
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_INFO,
                                "Error processing switch attributes\n");
            return rv;
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_SWITCH);

    return rv;
#undef _GET_SWITCH
}

/*
################################################################################
#                                Internal functions                            #
################################################################################
*/
/* Routine to get internal init state */
bool
_brcm_sai_switch_is_inited(void)
{
    return _brcm_sai_switch_initied;
}

sai_object_id_t
_brcm_sai_switch_id_get(int unit)
{
    return switchid;
}

sai_status_t
_brcm_sai_system_mac_get(sai_mac_t *mac)
{
    sai_status_t rv;
    _brcm_sai_data_t data;

    rv = _brcm_sai_global_data_get(_BRCM_SAI_SYSTEM_MAC_SET, &data);
    BRCM_SAI_RV_CHK(SAI_API_SWITCH, "getting system mac set global data", rv);
    if (FALSE == data.bool_data)
    {
        return SAI_STATUS_ITEM_NOT_FOUND;
    }
    rv = _brcm_sai_global_data_get(_BRCM_SAI_SYSTEM_MAC, &data);
    BRCM_SAI_RV_CHK(SAI_API_SWITCH, "getting system mac global data", rv);
    sal_memcpy(mac, data.mac, sizeof(sai_mac_t));
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_drop_if_get(bcm_if_t *if_id)
{
    sai_status_t rv;
    _brcm_sai_data_t data;

    rv = _brcm_sai_global_data_get(_BRCM_SAI_DROP_INTF_ID, &data);
    BRCM_SAI_RV_CHK(SAI_API_SWITCH, "getting system drop intf global data", rv);
    if (0 == data.if_id)
    {
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_CRITICAL,
                            "Global drop intf not created !!\n");
        return SAI_STATUS_FAILURE;
    }
    *if_id = data.if_id;
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_trap_if_get(bcm_if_t *if_id)
{
    sai_status_t rv;
    _brcm_sai_data_t data;

    rv = _brcm_sai_global_data_get(_BRCM_SAI_TRAP_INTF_ID, &data);
    BRCM_SAI_RV_CHK(SAI_API_SWITCH, "getting system trap intf global data", rv);
    if (0 == data.if_id)
    {
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_CRITICAL,
                            "Global trap intf not created !!\n");
        return SAI_STATUS_FAILURE;
    }
    *if_id = data.if_id;
    return SAI_STATUS_SUCCESS;
}

bcm_gport_t
_brcm_sai_switch_port_queue_get(int port, int qid, int type)
{
    int idx[2] = { port, qid };
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;

    /* Type: Port, L0, L1, UC, MC */
    if (_BRCM_SAI_IS_CPU_PORT(port) && (type == _BRCM_SAI_QUEUE_TYPE_MULTICAST ||
        type == _BRCM_SAI_QUEUE_TYPE_UCAST)) /* CPU port only has MC */
    {
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_CPU_QUEUE,
                                        &qid, &data);
        if (SAI_STATUS_SUCCESS != rv)
        {
            return -1;
        }
        return data.gport1.gport;
    }
    else if (_BRCM_SAI_PORT_SCHEDULER_TYPE == type)
    {
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_PORT_SCHED,
                                        &port, &data);
        if (SAI_STATUS_SUCCESS != rv)
        {
            return -1;
        }
        return data.gport1.gport;
    }
    else if (_BRCM_SAI_L0_SCHEDULER_TYPE == type)
    {
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_L0_SCHED,
                                        idx, &data);
        if (SAI_STATUS_SUCCESS != rv)
        {
            return -1;
        }
        return data.gport2.gport;
    }
    else if (_BRCM_SAI_L1_SCHEDULER_TYPE == type)
    {
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_L1_SCHED,
                                        idx, &data);
        if (SAI_STATUS_SUCCESS != rv)
        {
            return -1;
        }
        return data.gport2.gport;
    }
    else if (_BRCM_SAI_QUEUE_TYPE_MULTICAST == type)
    {
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_MCAST_QUEUE,
                                        idx, &data);
        if (SAI_STATUS_SUCCESS != rv)
        {
            return -1;
        }
        return data.gport2.gport;
    }
    else
    {
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_UCAST_QUEUE,
                                        idx, &data);
        if (SAI_STATUS_SUCCESS != rv)
        {
            return -1;
        }
        return data.gport2.gport;
    }
}

sai_status_t
_brcm_sai_alloc_switch_gport()
{
    sai_status_t rv;

    if ((rv = _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_PORT_SCHED,
                                          _BRCM_SAI_MAX_PORTS))
            != SAI_STATUS_SUCCESS)
    {
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_CRITICAL,
                            "Error initializing port sched data !!\n");
        return rv;
    }
    if ((rv = _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_CPU_QUEUE,
                                          _BRCM_SAI_MAX_PORTS))
            != SAI_STATUS_SUCCESS)
    {
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_CRITICAL,
                            "Error initializing cpu queue data !!\n");
        return rv;
    }
    if ((rv = _brcm_sai_indexed_data_init2(_BRCM_SAI_INDEXED_L0_SCHED,
                  _BRCM_SAI_MAX_PORTS,
                  _brcm_sai_get_scheduler_max(_L0_NODES)))
            != SAI_STATUS_SUCCESS)
    {
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_CRITICAL,
                            "Error initializing L0 sched data !!\n");
        return rv;
    }
    if ((rv = _brcm_sai_indexed_data_init2(_BRCM_SAI_INDEXED_L1_SCHED,
                                           _BRCM_SAI_MAX_PORTS, NUM_L1_NODES))
            != SAI_STATUS_SUCCESS)
    {
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_CRITICAL,
                            "Error initializing L1 sched data !!\n");
        return rv;
    }
    if ((rv = _brcm_sai_indexed_data_init2(_BRCM_SAI_INDEXED_UCAST_QUEUE,
                                           _BRCM_SAI_MAX_PORTS, NUM_QUEUES))
            != SAI_STATUS_SUCCESS)
    {
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_CRITICAL,
                            "Error initializing ucast queue data !!\n");
        return rv;
    }
    if ((rv = _brcm_sai_indexed_data_init2(_BRCM_SAI_INDEXED_MCAST_QUEUE,
                                           _BRCM_SAI_MAX_PORTS, NUM_QUEUES))
            != SAI_STATUS_SUCCESS)
    {
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_CRITICAL,
                            "Error initializing mcast queue data !!\n");
        return rv;
    }
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_free_switch_gport()
{
    sai_status_t rv;

    if ((rv =  _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_PORT_SCHED,
                                            0, _BRCM_SAI_MAX_PORTS, -1))
            != SAI_STATUS_SUCCESS)
    {
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_CRITICAL,
                            "Error freeing port sched data !!\n");
        return rv;
    }
    if ((rv =  _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_CPU_QUEUE,
                                            0, _BRCM_SAI_MAX_PORTS, -1))
            != SAI_STATUS_SUCCESS)
    {
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_CRITICAL,
                            "Error freeing cpu queue data !!\n");
        return rv;
    }
    if ((rv =  _brcm_sai_indexed_data_free2(_BRCM_SAI_INDEXED_L0_SCHED,
                   _BRCM_SAI_MAX_PORTS,
                   _brcm_sai_get_scheduler_max(_L0_NODES)))
            != SAI_STATUS_SUCCESS)
    {
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_CRITICAL,
                            "Error freeing L0 sched data !!\n");
        return rv;
    }
    if ((rv =  _brcm_sai_indexed_data_free2(_BRCM_SAI_INDEXED_L1_SCHED,
                                            _BRCM_SAI_MAX_PORTS, NUM_L1_NODES))
            != SAI_STATUS_SUCCESS)
    {
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_CRITICAL,
                            "Error freeing L1 sched data !!\n");
        return rv;
    }
    if ((rv =  _brcm_sai_indexed_data_free2(_BRCM_SAI_INDEXED_UCAST_QUEUE,
                                            _BRCM_SAI_MAX_PORTS, NUM_QUEUES))
            != SAI_STATUS_SUCCESS)
    {
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_CRITICAL,
                            "Error freeing ucast queue data !!\n");
        return rv;
    }
    if ((rv =  _brcm_sai_indexed_data_free2(_BRCM_SAI_INDEXED_MCAST_QUEUE,
                                            _BRCM_SAI_MAX_PORTS, NUM_QUEUES))
            != SAI_STATUS_SUCCESS)
    {
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_CRITICAL,
                            "Error freeing mcast queue data !!\n");
        return rv;
    }
    return SAI_STATUS_SUCCESS;
}

int
_brcm_sai_switch_cpu_lls_init(int unit)
{
    int cos, rv, port = 0;
    int idx[2] = { port };
    bcm_gport_t gport;
    _brcm_sai_indexed_data_t data;

    /* Delete the default hierarchy of given port */
    rv = bcm_port_gport_get(unit, port, &gport);

    if (BCM_FAILURE(rv))
    {
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                           "bcm_port_gport_get returned error: %d\r\n", rv);

        return rv;
    }

    rv = bcm_cosq_gport_delete(unit, gport);
    if (BCM_FAILURE(rv))
    {
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                            "bcm_cosq_gport_delete returned error: %d\r\n", rv);
        return rv;
    }

    /* Create port scheduler. Specify port scheduler with 1 input */
    rv = bcm_cosq_gport_add(unit, gport,
                            _brcm_sai_get_scheduler_max(_CPU_L0_NODES), 0,
                            &c_port_sched[port]);
    if (BCM_FAILURE(rv))
    {
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                            "bcm_cosq_gport_add returned error @ Port: %d\r\n",
                            rv);

        return rv;
    }
    data.gport1.idx = idx[0];
    data.gport1.gport = c_port_sched[port];
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_PORT_SCHED,
                                    &port, &data);
    BRCM_SAI_RV_CHK(SAI_API_QUEUE, "port sched gport data set", rv);

    /* Create L0 scheduler gport object */
    for (cos = 0; cos < _brcm_sai_get_scheduler_max(_CPU_L0_NODES); cos++)
    {
        rv = bcm_cosq_gport_add(unit, gport, NUM_CPU_L1_NODES,
                                    BCM_COSQ_GPORT_SCHEDULER,
                                    &c_l0_sched[port][cos]);
        if (BCM_FAILURE(rv))
        {
            BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                "bcm_cosq_gport_add returned error: %d @ L0(cos %d)\r\n",
                rv, cos);

            return rv;
        }
        idx[1] = cos;
        data.gport2.idx1 = idx[0];
        data.gport2.idx2 = idx[1];
        data.gport2.gport = c_l0_sched[port][cos];
        rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_L0_SCHED,
                                        idx, &data);
        BRCM_SAI_RV_CHK(SAI_API_QUEUE, "L0 sched gport data set", rv);
    }

    /* Create L1 gport objects */
    for (cos = 0;  cos < NUM_CPU_L1_NODES; cos++)
    {
        rv = bcm_cosq_gport_add(unit, gport,
                                NUM_TD2_CPU_QUEUES, BCM_COSQ_GPORT_SCHEDULER,
                                &c_l1_sched[port][cos]);
        if (BCM_FAILURE(rv))
        {
            BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                "bcm_cosq_gport_add returned error: %d @ L1(cos %d)\r\n",
                rv, cos);

            return rv;
        }
        idx[1] = cos;
        data.gport2.idx1 = idx[0];
        data.gport2.idx2 = idx[1];
        data.gport2.gport = c_l1_sched[port][cos];
        rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_L1_SCHED,
                                        idx, &data);
        BRCM_SAI_RV_CHK(SAI_API_QUEUE, "L1 sched gport data set", rv);
    }

    /* Create all MC queue gports and attach to correspoding L1 gports at COS1 */
    for (cos = 0; cos < _brcm_sai_get_port_max_queues(TRUE); cos++)
    {
        rv = bcm_cosq_gport_add(unit, port, 1,
                                    BCM_COSQ_GPORT_MCAST_QUEUE_GROUP,
                                    &c_CPUMqueue[cos]);
        if (BCM_FAILURE(rv))
        {
            BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                "bcm_cosq_gport_add returned error: %d @ L2(cos %d)\r\n",
                rv, cos);
            return rv;
        }
        data.gport1.idx = cos;
        data.gport1.gport = c_CPUMqueue[cos];
        rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_CPU_QUEUE,
                                        &cos, &data);
        BRCM_SAI_RV_CHK(SAI_API_QUEUE, "cpu queue gport data set", rv);
    }

    /* Attach L0 gport to port scheduler at input 0 */
    for (cos = 0; cos < 1; cos++)
    {
        rv = bcm_cosq_gport_attach(unit, c_l0_sched[port][cos],
                                   c_port_sched[port], -1);
        if (BCM_FAILURE(rv))
        {
            BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                "bcm_cosq_gport_attach returned error: %d @ L0(cos %d)\r\n",
                rv, cos);
            return rv;
        }
    }

    /* Attach L1[0] to L1[7]  gport to L0 [0] */
    for (cos = 0; cos < NUM_CPU_L1_NODES; cos++)
    {
        rv = bcm_cosq_gport_attach(unit, c_l1_sched[port][cos],
                                   c_l0_sched[port][0], -1);
        if (BCM_FAILURE(rv))
        {
            BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                "bcm_cosq_gport_attach returned error: %d @ L1(cos %d)\r\n",
                rv, cos);
            return rv;
        }
    }

    /* Multicast queues 0-15 are attached to L1 nodes 0-1 respectively. */
    for (cos = 0; cos < _brcm_sai_get_port_max_queues(TRUE); cos++)
    {
        rv = bcm_cosq_gport_attach(unit, c_CPUMqueue[cos],
                                   c_l1_sched[port][cos/NUM_TD2_CPU_QUEUES], -1);
        if (BCM_FAILURE(rv))
        {
            BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                "bcm_cosq_gport_attach returned error: %d @ L2(port %d/cos %d)\r\n",
                rv, port, cos);
            return rv;
        }
    }

    return rv;
}

int
_brcm_sai_switch_port_lls_init(int unit, int port)
{
    int cos, rv = 0;
    int idx[2] = { port };
    bcm_gport_t gport;
    _brcm_sai_indexed_data_t data;

    int num_l1_nodes = DEV_IS_HX4()? NUM_HX4_L1_NODES : NUM_L1_NODES;
    int num_queues = _brcm_sai_get_num_queues();

    /* Delete the default hierarchy of given port */
    rv = bcm_port_gport_get(unit, port, &gport);
    if (BCM_FAILURE(rv))
    {
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                            "bcm_port_gport_get returned error: %d\r\n", rv);
        return rv;
    }

    rv = bcm_cosq_gport_delete(unit, gport);
    if (BCM_FAILURE(rv))
    {
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                            "bcm_cosq_gport_delete returned error: %d\r\n", rv);
        return rv;
    }

    /* Create port scheduler. Specify port scheduler with 2 inputs */
    rv = bcm_cosq_gport_add(unit, gport, _brcm_sai_get_scheduler_max(_L0_NODES),
                                0, &c_port_sched[port]);
    if (BCM_FAILURE(rv))
    {
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                            "bcm_cosq_gport_add returned error: %d\r\n", rv);
        return rv;
    }
    data.gport1.idx = idx[0];
    data.gport1.gport = c_port_sched[port];
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_PORT_SCHED,
                                    &port, &data);
    BRCM_SAI_RV_CHK(SAI_API_QUEUE, "port sched gport data set", rv);

    /* Create L0 scheduler gport object with 8 input */
    for (cos = 0; cos < _brcm_sai_get_scheduler_max(_L0_NODES); cos++)
    {
        rv = bcm_cosq_gport_add (unit, gport, NUM_TD2_L1_PER_L0,
                                     BCM_COSQ_GPORT_SCHEDULER,
                                     &c_l0_sched[port][cos]);
        if (BCM_FAILURE(rv))
        {
            BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                                "bcm_cosq_gport_add returned error: %d\r\n", rv);
            return rv;
        }
        idx[1] = cos;
        data.gport2.idx1 = idx[0];
        data.gport2.idx2 = idx[1];
        data.gport2.gport = c_l0_sched[port][cos];
        rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_L0_SCHED,
                                        idx, &data);
        BRCM_SAI_RV_CHK(SAI_API_QUEUE, "L0 sched gport data set", rv);
    }

    /* Create first L1 gport object with 2 inputs - one UC, one MC */
    for (cos = 0; cos < num_l1_nodes; cos++)
    {
        rv = bcm_cosq_gport_add(unit, gport, NUM_L2_NODES,
                                BCM_COSQ_GPORT_SCHEDULER,
                                &c_l1_sched[port][cos]);
        if (BCM_FAILURE(rv))
        {
            BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                                "bcm_cosq_gport_add returned error: %d\r\n", rv);
            return rv;
        }
        idx[1] = cos;
        data.gport2.idx1 = idx[0];
        data.gport2.idx2 = idx[1];
        data.gport2.gport = c_l1_sched[port][cos];
        rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_L1_SCHED,
                                        idx, &data);
        BRCM_SAI_RV_CHK(SAI_API_QUEUE, "L1 sched gport data set", rv);
    }

    /* Create UC queue gports and attach to L1 gport at COS 0 */
    for (cos = 0; cos < num_queues; cos++)
    {
        rv = bcm_cosq_gport_add(unit, gport, 1,
                                BCM_COSQ_GPORT_UCAST_QUEUE_GROUP,
                                &c_Uqueue[port][cos]);
        if (BCM_FAILURE(rv))
        {
            BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                                "bcm_cosq_gport_add returned error: %d\r\n", rv);
            return rv;
        }
        idx[1] = cos;
        data.gport2.idx1 = idx[0];
        data.gport2.idx2 = idx[1];
        data.gport2.gport = c_Uqueue[port][cos];
        rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_UCAST_QUEUE,
                                        idx, &data);
        BRCM_SAI_RV_CHK(SAI_API_QUEUE, "ucast queue gport data set", rv);
    }

    /* Create MC queue gports and attach to correspoding L1 gports at COS1 */
    for (cos = 0; cos < num_queues; cos++)
    {
        rv = bcm_cosq_gport_add(unit, port, 1,
                                BCM_COSQ_GPORT_MCAST_QUEUE_GROUP,
                                &c_Mqueue[port][cos]);
        if (BCM_FAILURE(rv))
        {
            BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                                "bcm_cosq_gport_add returned error: cos %d %d\r\n", cos, rv);
            return rv;
        }
        idx[1] = cos;
        data.gport2.idx1 = idx[0];
        data.gport2.idx2 = idx[1];
        data.gport2.gport = c_Mqueue[port][cos];
        rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_MCAST_QUEUE,
                                        idx, &data);
        BRCM_SAI_RV_CHK(SAI_API_QUEUE, "mcast queue gport data set", rv);
    }

    /* Attach L0 gport to port scheduler at input 0 */
    for (cos = 0; cos < _brcm_sai_get_scheduler_max(_L0_NODES); cos++)
    {
        rv = bcm_cosq_gport_attach(unit, c_l0_sched[port][cos],
                                   c_port_sched[port], -1);
        if (BCM_FAILURE(rv))
        {
            BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                                "bcm_cosq_gport_attach returned error: %d\r\n", rv);
            return rv;
        }
    }

    /* Attach L1[0] to L1[7]  gport to L0 [0] */
    for (cos = 0; cos < num_l1_nodes; cos++)
    {
        rv = bcm_cosq_gport_attach(unit, c_l1_sched[port][cos],
                                   c_l0_sched[port][cos/NUM_TD2_L1_PER_L0], -1);
        if (BCM_FAILURE(rv))
        {
            BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                                "bcm_cosq_gport_attach returned error: %d\r\n", rv);
            return rv;
        }
    }

    /* Unicast queues 0-7 are attached to L1 nodes 0-7 respectively. */
    for (cos = 0; cos < num_queues; cos++)
    {
        rv = bcm_cosq_gport_attach(unit, c_Uqueue[port][cos],
                                   c_l1_sched[port][cos], -1);
        if (BCM_FAILURE(rv))
        {
            BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                                "bcm_cosq_gport_attach returned error: %d\r\n", rv);
            return rv;
        }
    }

    /* Multicast queues 0-7 are attached to L1 nodes 0-7 respectively. */
    for (cos = 0; cos < num_queues; cos++)
    {
        rv = bcm_cosq_gport_attach(unit, c_Mqueue[port][cos],
                                       c_l1_sched[port][cos], -1);
        if (BCM_FAILURE(rv))
        {
            BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                                "bcm_cosq_gport_attach returned error: %d\r\n", rv);
            return rv;
        }
    }

    return rv;
}

/* Local variables used in cosq traversal */
static int cb_cnt=0;
static int c_l0_cnt = 0;
static int c_l0_mc_cnt = 0;
static int p_uc_gport_cnt = 0;
static int p_mc_gport_cnt = 0;
static int p_shed_gport_cnt = 0;
static int prev_local_port = -1;
static int shed_cos_index = 0;
static int uc_cos_index = 0;
static int mc_cos_index = 0;

static int gport_traverse_cb(int unit, bcm_gport_t port, int numq, uint32 flags,
                             bcm_gport_t gport, void *user_data)
{
    int idx[2] = { 0 };
    bcm_cos_queue_t cosq;
    int local_port = -1, rv = 0;
    bcm_gport_t parent_gport;
    _brcm_sai_indexed_data_t data;
    int num_queues = _brcm_sai_get_num_queues();

    memset(&data, 0, sizeof(_brcm_sai_indexed_data_t));

    local_port = _brcm_sai_modport_port_get(port);
    if (local_port < 0)
    {
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                            "%s: Invalid port 0x%x(localport %d) : %d\r\n",
                            __FUNCTION__, port, local_port, rv);
        return -1;
    }

    ++cb_cnt;
    BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_DEBUG,
                        "%s(%d): %s%d (p:0x%x) %s f:0x%x  numq:%d gp:0x%x \n",
                        __FUNCTION__, cb_cnt,
                        (_BRCM_SAI_IS_CPU_PORT(local_port) ? "CPU" : "PORT"),
                        local_port, port,
                        (flags & BCM_COSQ_GPORT_SCHEDULER ? "GPORT_SCHED" :
                        (flags & BCM_COSQ_GPORT_MCAST_QUEUE_GROUP ?
                        "MCAST" : "UCAST")),
                        flags, numq, gport);

    rv = bcm_cosq_gport_attach_get(0, gport, &parent_gport, &cosq);
    if (0 == rv)
    {
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_DEBUG,
                            "gport 0x%x (%d) : parent gport 0x%x\n",
                            gport, cosq, parent_gport);
    }

    if (local_port == 0)
    {
        if (flags & BCM_COSQ_GPORT_SCHEDULER)
        {
            memcpy(&c_l0_sched[local_port][c_l0_cnt], &gport, sizeof(bcm_gport_t));

            idx[0] = local_port;
            idx[1] = c_l0_cnt;
            data.gport1.idx = local_port;
            data.gport1.gport = c_port_sched[local_port];
            data.gport2.idx1 = local_port;
            data.gport2.idx2 = c_l0_cnt;
            data.gport2.gport = c_l0_sched[local_port][c_l0_cnt];
            rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_L0_SCHED,
                                            idx, &data);
            BRCM_SAI_RV_CHK(SAI_API_QUEUE, "L0 sched gport data set", rv);

            c_l0_cnt++;
        }
        else if (flags & BCM_COSQ_GPORT_MCAST_QUEUE_GROUP)
        {
            memcpy(&c_CPUMqueue[c_l0_mc_cnt], &gport, sizeof(bcm_gport_t));

            data.gport1.idx = c_l0_mc_cnt;
            data.gport1.gport = c_CPUMqueue[c_l0_mc_cnt];
            rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_CPU_QUEUE,
                                            &c_l0_mc_cnt, &data);
            BRCM_SAI_RV_CHK(SAI_API_QUEUE, "cpu queue gport data set", rv);

            c_l0_mc_cnt++;
        }
        else
        {
            BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                "%s: Invalid flag for port 0x%x(localport %d) : %d\r\n",
                __FUNCTION__, port, local_port, rv);
        }
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_DEBUG,
            "%s() INFO(%d): c_l0_cnt:%d c_l0_mc_cnt:%d\n",
            __FUNCTION__, cb_cnt, c_l0_cnt, c_l0_mc_cnt);
    }
    else
    {
        if (local_port != prev_local_port)
        {
            prev_local_port = local_port;
            shed_cos_index = uc_cos_index = mc_cos_index = 0;
        }
        if (flags & BCM_COSQ_GPORT_SCHEDULER)
        {
            memcpy(&c_l0_sched[local_port][shed_cos_index], &gport, sizeof(bcm_gport_t));

            idx[0] = local_port;
            idx[1] = shed_cos_index;
            data.gport1.idx = local_port;
            data.gport1.gport = c_port_sched[local_port];
            data.gport2.idx1 = local_port;
            data.gport2.idx2 = shed_cos_index;
            data.gport2.gport = c_l0_sched[local_port][shed_cos_index];
            rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_L0_SCHED,
                                            idx, &data);
            BRCM_SAI_RV_CHK(SAI_API_QUEUE, "L0 sched gport data set", rv);
            p_shed_gport_cnt++;
            shed_cos_index = (shed_cos_index + 1) % num_queues;
        }
        else if (flags & BCM_COSQ_GPORT_UCAST_QUEUE_GROUP)
        {
            c_Uqueue[local_port][uc_cos_index] = gport;

            idx[0] = local_port;
            idx[1] = uc_cos_index;
            data.gport1.idx = local_port;
            data.gport1.gport = c_port_sched[local_port];
            data.gport2.idx1 = local_port;
            data.gport2.idx2 = uc_cos_index;
            data.gport2.gport = c_Uqueue[local_port][uc_cos_index];
            rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_UCAST_QUEUE,
                                            idx, &data);
            BRCM_SAI_RV_CHK(SAI_API_QUEUE, "ucast queue gport data set", rv);
            p_uc_gport_cnt++;
            uc_cos_index = (uc_cos_index + 1) % num_queues;
        }
        else if (flags & BCM_COSQ_GPORT_MCAST_QUEUE_GROUP)
        {
            c_Mqueue[local_port][mc_cos_index] = gport;

            idx[0] = local_port;
            idx[1] = mc_cos_index;
            data.gport1.idx = local_port;
            data.gport1.gport = c_port_sched[local_port];
            data.gport2.idx1 = local_port;
            data.gport2.idx2 = mc_cos_index;
            data.gport2.gport = c_Mqueue[local_port][mc_cos_index];
            rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_MCAST_QUEUE,
                                            idx, &data);
            BRCM_SAI_RV_CHK(SAI_API_QUEUE, "mcast queue gport data set", rv);
            p_mc_gport_cnt++;
            mc_cos_index = (mc_cos_index + 1) % num_queues;
        }
        else
        {
            BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                "%s: Invalid flag for port 0x%x(localport %d) : %d\r\n",
                __FUNCTION__, port, local_port, rv);
        }
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_DEBUG,
                            "%s() INFO(%d): shed_cos_index:%d uc_cos_index:"
                            "%d mc_cos_index:%d\n",
                             __FUNCTION__, cb_cnt, shed_cos_index, uc_cos_index,
                            mc_cos_index);
    }
    return 0;
}

/* Debug API to verify the cosq traverse info */
#define QGET(port, numq, qtype, str) for (qid=0;qid<numq;qid++)       \
        {                                                             \
           gport = _brcm_sai_switch_port_queue_get(port, qid, qtype); \
           printf("VERIFY: Port %d %s Cos-%d gPort 0x%x\n",           \
                  port, str, qid, gport);                             \
        }

STATIC sai_status_t
_brcm_sai_mmu_gport_init()
{
    int i = _SHR_PBMP_PORT_MAX, rv;
    bcm_pbmp_t pbmp;

    bcm_gport_t gport;
    _brcm_sai_indexed_data_t data;

    /* THx and TD3 do not support flex/creating LLS.
    ** It has fixed scheduling hierarchy so by default,
    ** gport tree is created during HW init.
    *  GPorts created already can be obtained by
    ** calling bcm_cosq_gport_traverse() API.
    ** So populate the SAI gport info by
    ** traversing the SDK gport default tree.
    */

    if (DEV_IS_THX() || DEV_IS_TD3())
    {
        _brcm_sai_switch_pbmp_get(&pbmp);
        BCM_PBMP_ITER(pbmp, i)
        {
            rv = bcm_port_gport_get(0, i, &gport);
            BRCM_SAI_API_CHK(SAI_API_SWITCH,
                             "ERROR(%d) while getting the port's gport", rv);
            if (BCM_FAILURE(rv))
            {
                BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                    "bcm_port_gport_get(port=%d) returned error: %d\r\n",
                    i, rv);
                return rv;
            }

            c_port_sched[i] = gport;
            data.gport1.idx = i;
            data.gport1.gport = c_port_sched[i];
            rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_PORT_SCHED,
                                           &i, &data);
            BRCM_SAI_RV_CHK(SAI_API_QUEUE, "port sched gport data set", rv);
        }

        rv = bcm_cosq_gport_traverse(0, gport_traverse_cb, NULL);
        if (rv  < BCM_E_NONE)
        {
            BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                                "ERROR(%d) while traversing the gport tree.\n",
                                rv);
            return rv;
        }

        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_DEBUG, "COSQ TRAVERSE:: "
                            "CPU(shed-%d, MC-%d) PORT(shed-%d, "
                            "UC-%d, MC-%d)\n", c_l0_cnt, c_l0_mc_cnt,
                             p_shed_gport_cnt, p_uc_gport_cnt, p_mc_gport_cnt);

        return 0;
    }

    /* CPU port */
    rv = _brcm_sai_switch_cpu_lls_init(0);
    if (rv < 0)
    {
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                            "ERROR(%d) while configuring CPU port(%d)", rv, 0);
        return rv;
    }
    /* Note: currently using only front panel ports, no hg ports */
    _brcm_sai_switch_pbmp_fp_all_get(&pbmp);
    BCM_PBMP_ITER(pbmp, i)
    {
        rv = _brcm_sai_switch_port_lls_init(0, i);
        if (rv < 0)
        {
            BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                                "ERROR(%d) while configuring port %d", rv, i);
            return rv;
        }
    }
    return 0;
}

STATIC void
_brcm_sai_switch_wb_state_set(bool wb)
{
    _brcm_sai_wb_state = wb;
}

bool
_brcm_sai_switch_wb_state_get(void)
{
    return _brcm_sai_wb_state;
}


/* Set device type here directly to reduce comparison
   operations later */
bool
_brcm_sai_dev_set()
{
    if ((dev == 0xb850) ||
        (dev == 0xb854))
    {
        /* TD2 */
        chip_td2 = TRUE;
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_INFO,
                            "Switch detected Trident2 type device");

    }
    else if ((dev == 0xb960) ||
             (dev == 0xb963))
    {
        /* TH */
        chip_th = TRUE;
        chip_thx = TRUE;
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_INFO,
                            "Switch detected Tomahawk type device");
    }
    else if ((dev == 0xb970) ||
             (dev == 0xb971) ||
             (dev == 0xb972))
    {
        /* TH2 */
        chip_th2 = TRUE;
        chip_thx = TRUE;
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_INFO,
                            "Switch detected Tomahawk2 type device");
    }
    else if (dev == 0xb340)
    {
        /* Helix4 */
        chip_hx4 = TRUE;
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_INFO,
                            "Switch detected Helix4 type device");
    }
    else if ((dev == 0xb870) ||
             (dev == 0xb873))
    {
        /* TD3 */
        chip_td3 = TRUE;
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_INFO,
                            "Switch detected Trident3 type device");
    }
    else if (dev == 0xb980)
    {
        /* TH3 */
        chip_th3 = TRUE;
        chip_thx = TRUE;
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_INFO,
                            "Switch detected Tomahawk3 type device");
    }
    else
    {
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_CRITICAL,
                            "Broadcom SAI detected an unsupported device id 0x%x\n",
                            dev);
        return FALSE;
    }
    BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_INFO,
                        " Id 0x%x revision 0x%x", dev, rev);
    return TRUE;
}

/* These are only valid for when
   _brcm_sai_switch_initied == TRUE */
bool
_brcm_sai_dev_chk(int id)
{
    return (dev == id);
}

/* Internal APIs, do not use directly */
bool
_brcm_sai_dev_is_td2()
{
    return (chip_td2);
}

bool
_brcm_sai_dev_is_th()
{
    return (chip_th);
}

bool
_brcm_sai_dev_is_th2()
{
    return (chip_th2);
}

bool
_brcm_sai_dev_is_th3()
{
    return (chip_th3);
}

bool
_brcm_sai_dev_is_thx()
{
    return (chip_thx);
}

bool
_brcm_sai_dev_is_hx4()
{
    return (chip_hx4);
}


bool
_brcm_sai_dev_is_td3()
{
    return (chip_td3);
}

bool
_brcm_sai_dev_is_tdx()
{
    return (chip_td2) | (chip_td3);
}




STATIC sai_status_t
_brcm_sai_switch_mac_addr_update(const sai_mac_t mac)
{
    return SAI_STATUS_NOT_SUPPORTED;
}

void
_brcm_sai_switch_pbmp_all_get(bcm_pbmp_t *pbmp)
{
    BCM_PBMP_ASSIGN(*pbmp, pbmp_all);
}

void
_brcm_sai_switch_pbmp_get(bcm_pbmp_t *pbmp)
{
    BCM_PBMP_ASSIGN(*pbmp, pbmp_fp_plus_cpu);
}

void
_brcm_sai_switch_pbmp_fp_all_get(bcm_pbmp_t *pbmp)
{
    BCM_PBMP_ASSIGN(*pbmp, pbmp_all_front_panel);
}

int _brcm_sai_switch_fp_port_count()
{
    return front_panel_port_count;
}

STATIC sai_status_t
_brcm_sai_switch_fdb_miss_set(int type, int pa)
{
    int p, _p, sc;
    uint32 flags, flag;
    bcm_pbmp_t pbmp, _pbmp;
    _brcm_sai_data_t data;
    bcm_field_group_t group = 0;
    sai_status_t rv = SAI_STATUS_SUCCESS;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_SWITCH);

    if (SAI_SWITCH_ATTR_FDB_UNICAST_MISS_PACKET_ACTION == type)
    {
        sc = bcmSwitchUnknownUcastToCpu;
        flag = BCM_PORT_FLOOD_BLOCK_UNKNOWN_UCAST;
    }
    else if (SAI_SWITCH_ATTR_FDB_MULTICAST_MISS_PACKET_ACTION == type)
    {
        sc = bcmSwitchUnknownMcastToCpu;
        flag = BCM_PORT_FLOOD_BLOCK_UNKNOWN_MCAST;
    }
    else if (SAI_SWITCH_ATTR_FDB_BROADCAST_MISS_PACKET_ACTION == type)
    {
        flag = BCM_PORT_FLOOD_BLOCK_BCAST;
    }
    else
    {
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR, "Invalid type %d", type);
        return SAI_STATUS_FAILURE;
    }

    _brcm_sai_switch_pbmp_fp_all_get(&pbmp);
    /* Just need to get block mask flags for 1st port....
     * all others are the same.
     */
    rv = bcm_port_flood_block_get(0, 1, 0, &flags);
    BRCM_SAI_API_CHK(SAI_API_SWITCH, "port flood block get", rv);
    if (SAI_PACKET_ACTION_FORWARD == pa)
    {
        flags &= ~flag;
    }
    else
    {
        flags |= flag;
    }
    BCM_PBMP_ITER(pbmp, p)
    {
        _pbmp = pbmp;
        /* Handle flooding to CPU port */
        rv = bcm_port_flood_block_set(0, p, 0, flags);
        BRCM_SAI_API_CHK(SAI_API_SWITCH, "port flood block set", rv);
        BCM_PBMP_PORT_REMOVE(_pbmp, p);
        BCM_PBMP_ITER(_pbmp, _p)
        {
            rv = bcm_port_flood_block_set(0, p, _p, flags);
            BRCM_SAI_API_CHK(SAI_API_SWITCH, "port flood block set", rv);
        }
    }
    if (SAI_PACKET_ACTION_TRAP == pa)
    {
        if (SAI_SWITCH_ATTR_FDB_BROADCAST_MISS_PACKET_ACTION != type)
        {
            rv = bcm_switch_control_set(0, sc, 1);
            BRCM_SAI_API_CHK(SAI_API_SWITCH, "switch control set", rv);
        }
        else
        {
            if (FALSE == bcast_rule_installed)
            {
                rv = _brcm_sai_global_data_get(_BRCM_SAI_TRAP_FP_GROUP, &data);
                BRCM_SAI_RV_CHK(SAI_API_SWITCH, "getting global fp group", rv);
                group = data.u32;
                rv = bcm_field_entry_create(0, group, &bcast_entry);
                BRCM_SAI_API_CHK(SAI_API_SWITCH, "field entry create", rv);
                rv = bcm_field_qualify_PacketRes(0, bcast_entry, BCM_FIELD_PKT_RES_L2BC, 0x3f);
                BRCM_SAI_API_CHK(SAI_API_SWITCH, "field qualify", rv);
                rv = bcm_field_action_add(0, bcast_entry, bcmFieldActionCopyToCpu, 0, 0);
                BRCM_SAI_API_CHK(SAI_API_SWITCH, "field action add", rv);
                rv = bcm_field_entry_install(0, bcast_entry);
                BRCM_SAI_API_CHK(SAI_API_SWITCH, "field entry install", rv);
                bcast_rule_installed = data.bool_data = TRUE;
                rv = _brcm_sai_global_data_set(_BRCM_SAI_SW_BCAST_RULE_INSTALLED, &data);
                BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_ERROR,
                                        "setting global bcast rule installed", rv);
                data.s32 = bcast_entry;
                rv = _brcm_sai_global_data_set(_BRCM_SAI_SW_BCAST_ENTRY, &data);
                BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_ERROR,
                                        "setting global bcast entry", rv);
            }
        }
    }
    else
    {
        if (SAI_SWITCH_ATTR_FDB_BROADCAST_MISS_PACKET_ACTION != type)
        {
            rv = bcm_switch_control_set(0, sc, 0);
            BRCM_SAI_API_CHK(SAI_API_SWITCH, "switch control set", rv);
        }
        else
        {
            if (TRUE == bcast_rule_installed)
            {
                rv = bcm_field_entry_destroy(0, bcast_entry);
                BRCM_SAI_API_CHK(SAI_API_SWITCH, "field entry destroy", rv);
                bcast_rule_installed = data.bool_data = FALSE;
                rv = _brcm_sai_global_data_set(_BRCM_SAI_SW_BCAST_RULE_INSTALLED, &data);
                BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_ERROR,
                                        "setting global bcast rule installed", rv);
                data.s32 = 0;
                rv = _brcm_sai_global_data_set(_BRCM_SAI_SW_BCAST_ENTRY, &data);
                BRCM_SAI_RV_LVL_CHK(SAI_API_SWITCH, SAI_LOG_LEVEL_ERROR,
                                        "setting global bcast entry", rv);
            }
        }
    }
    BRCM_SAI_FUNCTION_EXIT(SAI_API_SWITCH);
    return rv;
}

/*******************************Closed code************************************/
STATIC sai_status_t
_brcm_sai_switch_mac_addr_set(const sai_mac_t _mac, int *intf, int *difid,
                              int *tifid)
{
    bcm_mac_t mac;
    bcm_vlan_t vid;
    bcm_if_t l3_if_id;
    bcm_l2_addr_t l2addr;
    bcm_l3_egress_t l3_eg;
    bcm_l3_intf_t l3_intf;
    sai_status_t rv = SAI_STATUS_SUCCESS;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_SWITCH);

    rv = bcm_vlan_default_get(0, &vid);
    BRCM_SAI_API_CHK(SAI_API_SWITCH, "Vlan default get", rv);
    sal_memcpy(&mac, _mac, sizeof(bcm_mac_t));
    bcm_l2_addr_t_init(&l2addr, mac, vid);
    l2addr.port = 0;
    l2addr.flags = (BCM_L2_STATIC | BCM_L2_REPLACE_DYNAMIC);
    rv = bcm_l2_addr_add(0, &l2addr);
    BRCM_SAI_API_CHK(SAI_API_SWITCH, "L2 address add", rv);
    if (!DEV_IS_HX4() && !DEV_IS_TD3() && !DEV_IS_TH3())
    {
        rv = _brcm_sai_global_data_bump(_BRCM_SAI_FDB_COUNT, INC);
        BRCM_SAI_RV_CHK(SAI_API_SWITCH, "fdb global count inc", rv);
    }
    /* Create drop intf for global vr */
    bcm_l3_intf_t_init(&l3_intf);
    l3_intf.l3a_ttl = _BRCM_SAI_VR_DEFAULT_TTL;
    l3_intf.l3a_mtu = _BRCM_SAI_RIF_DEFAULT_MTU;
    l3_intf.l3a_vid = vid;
    memcpy (&l3_intf.l3a_mac_addr, mac, sizeof(sai_mac_t));
    rv = bcm_l3_intf_create(0, &l3_intf);
    BRCM_SAI_API_CHK(SAI_API_SWITCH, "L3 intf create", rv);
    *intf = l3_intf.l3a_intf_id;
    bcm_l3_egress_t_init(&l3_eg);
    l3_eg.intf = l3_intf.l3a_intf_id;
    l3_eg.flags = BCM_L3_DST_DISCARD;
    memcpy(l3_eg.mac_addr, l3_intf.l3a_mac_addr, sizeof(l3_eg.mac_addr));
    rv = bcm_l3_egress_create(0, 0, &l3_eg, &l3_if_id);
    BRCM_SAI_API_CHK(SAI_API_SWITCH, "L3 egress create", rv);
    BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_DEBUG,
                        "Global drop L3 egress object id: %d\n", l3_if_id);
    *difid = l3_if_id;
    rv = _brcm_sai_global_data_bump(_BRCM_SAI_EGRESS_INUSE_COUNT, INC);
    BRCM_SAI_RV_CHK(SAI_API_SWITCH, "global egress inuse count increase", rv);

    /* Create trap intf for global vr */
    bcm_l3_egress_t_init(&l3_eg);
    l3_eg.intf = l3_intf.l3a_intf_id;
    l3_eg.flags = BCM_L3_KEEP_SRCMAC | BCM_L3_KEEP_DSTMAC |
                  BCM_L3_KEEP_TTL;
    memcpy(l3_eg.mac_addr, l3_intf.l3a_mac_addr, sizeof(l3_eg.mac_addr));
    rv = bcm_l3_egress_create(0, 0, &l3_eg, &l3_if_id);
    BRCM_SAI_API_CHK(SAI_API_SWITCH, "L3 egress create", rv);
    BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_DEBUG,
                        "Global trap L3 egress object id: %d\n", l3_if_id);
    *tifid = l3_if_id;
    rv = _brcm_sai_global_data_bump(_BRCM_SAI_EGRESS_INUSE_COUNT, INC);
    BRCM_SAI_RV_CHK(SAI_API_SWITCH, "global egress inuse count increase", rv);

    /* Add unicast arp trap */
    rv = _brcm_sai_arp_trap_add(0, -1, -1, 0, &mac);
    BRCM_SAI_RV_CHK(SAI_API_SWITCH, "arp trap add", rv);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_SWITCH);
    return rv;
}

sai_status_t
_brcm_sai_set_switch_attribute(_In_ const sai_attribute_t *attr)
{
#define _SET_SWITCH "Set switch"
    int val;
    sai_status_t rv = SAI_STATUS_SUCCESS;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_SWITCH);

    if (IS_NULL(attr))
    {
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR, "Null attr param\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    switch(attr->id)
    {
        case SAI_SWITCH_ATTR_LAG_DEFAULT_HASH_ALGORITHM:
            if (SAI_HASH_ALGORITHM_RANDOM == attr->value.u32)
            {
                rv = SAI_STATUS_NOT_SUPPORTED;
            }
            else
            {
                val = (SAI_HASH_ALGORITHM_CRC == attr->value.u32) ?
                       BCM_HASH_FIELD_CONFIG_CRC32LO :
                       BCM_HASH_FIELD_CONFIG_XOR16;
                rv = bcm_switch_control_set(0, bcmSwitchHashField0Config, val);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
            }
            break;
        case SAI_SWITCH_ATTR_ECMP_DEFAULT_HASH_ALGORITHM:
            if (SAI_HASH_ALGORITHM_RANDOM == attr->value.u32)
            {
                rv = SAI_STATUS_NOT_SUPPORTED;
            }
            else
            {
                val = (SAI_HASH_ALGORITHM_CRC == attr->value.u32) ?
                       BCM_HASH_FIELD_CONFIG_CRC32HI :
                       BCM_HASH_FIELD_CONFIG_CRC16XOR8;
                rv = bcm_switch_control_set(0, bcmSwitchHashField0Config1, val);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
            }
            break;
        case SAI_SWITCH_ATTR_ECMP_HASH_IPV4:
        {
            int h, hash = 0;
            bool l2 = FALSE, l3 = FALSE;
            _brcm_sai_indexed_data_t data;

            if (SAI_NULL_OBJECT_ID == attr->value.oid)
            {
                rv = _brcm_sai_load_balance_init(FALSE, 3);
                break;
            }
            if (BRCM_SAI_CHK_OBJ_MISMATCH(attr->value.oid, SAI_OBJECT_TYPE_HASH))
            {
                return SAI_STATUS_INVALID_OBJECT_TYPE;
            }
            h = BRCM_SAI_GET_OBJ_VAL(int, attr->value.oid);
            rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_HASH_INFO,
                                            &h, &data);
            BRCM_SAI_RV_CHK(SAI_API_SWITCH, "hash data get", rv);
            if (FALSE == data.hash.valid)
            {
                BRCM_SAI_LOG_HASH(SAI_LOG_LEVEL_ERROR,
                                  "Invalid object id 0x%016lx passed\n", attr->value.oid);
                return SAI_STATUS_INVALID_OBJECT_ID;
            }
            if (data.hash.hash_fields_count)
            {
                _brcm_sai_data_t gdata;

                rv = _brcm_sai_global_data_get(_BRCM_SAI_UDF_HASH_USED, &gdata);
                BRCM_SAI_RV_CHK(SAI_API_SWITCH, "hash data get", rv);
                if (0 == gdata.u32)
                {
                    rv = bcm_switch_control_get(0, bcmSwitchHashL2Field0, &val);
                    BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                    val &= ~(BCM_HASH_FIELD_SRCMOD | BCM_HASH_FIELD_SRCPORT);
                    rv = bcm_switch_control_set(0, bcmSwitchHashL2Field0, val);
                    BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                    rv = bcm_switch_control_get(0, bcmSwitchHashIP4Field0, &val);
                    BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                    val &= ~(BCM_HASH_FIELD_SRCMOD | BCM_HASH_FIELD_SRCPORT);
                    rv = bcm_switch_control_set(0, bcmSwitchHashIP4Field0, val);
                    BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                    rv = bcm_switch_control_get(0, bcmSwitchHashIP4TcpUdpField0, &val);
                    BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                    val &= ~(BCM_HASH_FIELD_SRCMOD | BCM_HASH_FIELD_SRCPORT);
                    rv = bcm_switch_control_set(0, bcmSwitchHashIP4TcpUdpField0, val);
                    BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);

                    rv = bcm_switch_control_get(0, bcmSwitchHashIP4TcpUdpPortsEqualField0, &val);
                    BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                    val &= ~(BCM_HASH_FIELD_SRCMOD | BCM_HASH_FIELD_SRCPORT);
                    rv = bcm_switch_control_set(0, bcmSwitchHashIP4TcpUdpPortsEqualField0, val);
                    BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);

                }
                for (h=0; h<data.hash.hash_fields_count; h++)
                {
                    switch (data.hash.hash_fields[h])
                    {
                        case SAI_NATIVE_HASH_FIELD_SRC_IP:
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP4Field0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= (BCM_HASH_FIELD_IP4SRC_HI | BCM_HASH_FIELD_IP4SRC_LO);
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP4Field0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP4TcpUdpField0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= (BCM_HASH_FIELD_IP4SRC_HI | BCM_HASH_FIELD_IP4SRC_LO);
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP4TcpUdpField0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP4TcpUdpPortsEqualField0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= (BCM_HASH_FIELD_IP4SRC_HI | BCM_HASH_FIELD_IP4SRC_LO);
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP4TcpUdpPortsEqualField0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);

                            l3 = TRUE;
                            break;
                        case SAI_NATIVE_HASH_FIELD_DST_IP:
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP4Field0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= (BCM_HASH_FIELD_IP4DST_HI | BCM_HASH_FIELD_IP4DST_LO);
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP4Field0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP4TcpUdpField0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= (BCM_HASH_FIELD_IP4DST_HI | BCM_HASH_FIELD_IP4DST_LO);
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP4TcpUdpField0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP4TcpUdpPortsEqualField0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= (BCM_HASH_FIELD_IP4DST_HI | BCM_HASH_FIELD_IP4DST_LO);
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP4TcpUdpPortsEqualField0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);

                            l3 = TRUE;
                            break;
                        case SAI_NATIVE_HASH_FIELD_VLAN_ID:
                            rv = bcm_switch_control_get(0, bcmSwitchHashL2Field0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= BCM_HASH_FIELD_VLAN;
                            rv = bcm_switch_control_set(0, bcmSwitchHashL2Field0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP4Field0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= BCM_HASH_FIELD_VLAN;
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP4Field0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP4TcpUdpField0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= BCM_HASH_FIELD_VLAN;
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP4TcpUdpField0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP4TcpUdpPortsEqualField0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= BCM_HASH_FIELD_VLAN;
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP4TcpUdpPortsEqualField0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);

                            break;
                        case SAI_NATIVE_HASH_FIELD_IP_PROTOCOL:
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP4Field0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= BCM_HASH_FIELD_PROTOCOL;
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP4Field0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP4TcpUdpField0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= BCM_HASH_FIELD_PROTOCOL;
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP4TcpUdpField0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP4TcpUdpPortsEqualField0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= BCM_HASH_FIELD_PROTOCOL;
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP4TcpUdpPortsEqualField0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            l3 = TRUE;
                            break;
                        case SAI_NATIVE_HASH_FIELD_ETHERTYPE:
                            rv = bcm_switch_control_get(0, bcmSwitchHashL2Field0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= BCM_HASH_FIELD_ETHER_TYPE;
                            rv = bcm_switch_control_set(0, bcmSwitchHashL2Field0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            l2 = TRUE;
                            break;
                        case SAI_NATIVE_HASH_FIELD_L4_SRC_PORT:
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP4Field0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= BCM_HASH_FIELD_SRCL4;
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP4Field0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP4TcpUdpField0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= BCM_HASH_FIELD_SRCL4;
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP4TcpUdpField0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP4TcpUdpPortsEqualField0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= BCM_HASH_FIELD_SRCL4;
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP4TcpUdpPortsEqualField0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            l3 = TRUE;
                            break;
                        case SAI_NATIVE_HASH_FIELD_L4_DST_PORT:
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP4Field0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= BCM_HASH_FIELD_DSTL4;
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP4Field0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP4TcpUdpField0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= BCM_HASH_FIELD_DSTL4;
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP4TcpUdpField0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP4TcpUdpPortsEqualField0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= BCM_HASH_FIELD_DSTL4;
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP4TcpUdpPortsEqualField0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            l3 = TRUE;
                            break;
                        case SAI_NATIVE_HASH_FIELD_SRC_MAC:
                            rv = bcm_switch_control_get(0, bcmSwitchHashL2Field0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= (BCM_HASH_FIELD_MACSA_HI | BCM_HASH_FIELD_MACSA_MI |
                                    BCM_HASH_FIELD_MACSA_LO);
                            rv = bcm_switch_control_set(0, bcmSwitchHashL2Field0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            l2 = TRUE;
                            break;
                        case SAI_NATIVE_HASH_FIELD_DST_MAC:
                            rv = bcm_switch_control_get(0, bcmSwitchHashL2Field0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= (BCM_HASH_FIELD_MACDA_HI | BCM_HASH_FIELD_MACDA_MI |
                                    BCM_HASH_FIELD_MACDA_LO);
                            rv = bcm_switch_control_set(0, bcmSwitchHashL2Field0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            l2 = TRUE;
                            break;
                        case SAI_NATIVE_HASH_FIELD_IN_PORT:
                            rv = bcm_switch_control_get(0, bcmSwitchHashL2Field0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= (BCM_HASH_FIELD_SRCMOD | BCM_HASH_FIELD_SRCPORT);
                            rv = bcm_switch_control_set(0, bcmSwitchHashL2Field0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP4Field0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= (BCM_HASH_FIELD_SRCMOD | BCM_HASH_FIELD_SRCPORT);
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP4Field0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP4TcpUdpField0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= (BCM_HASH_FIELD_SRCMOD | BCM_HASH_FIELD_SRCPORT);
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP4TcpUdpField0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP4TcpUdpPortsEqualField0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= (BCM_HASH_FIELD_SRCMOD | BCM_HASH_FIELD_SRCPORT);
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP4TcpUdpPortsEqualField0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            rv = _brcm_sai_global_data_bump(_BRCM_SAI_UDF_HASH_USED, INC);
                            BRCM_SAI_RV_CHK(SAI_API_SWITCH, "used hash used", rv);
                            break;
                        default:
                            break;
                    }
                }
                if (l2 && l3)
                {
                    BRCM_SAI_LOG_HASH(SAI_LOG_LEVEL_ERROR,
                                      "Can't support this combination of L2, L3 hash fields\n");
                    return SAI_STATUS_INVALID_PARAMETER;
                }

                else if (l3)
                {
                    rv = bcm_switch_control_get(0, bcmSwitchHashSelectControl, &val);
                    BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                    val &= ~BCM_HASH_FIELD0_DISABLE_IP4;
                    rv = bcm_switch_control_set(0, bcmSwitchHashSelectControl, val);
                    BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                    BRCM_SAI_LOG_HASH(SAI_LOG_LEVEL_DEBUG, "Using L3 fields for ECMP hashing\n");
                }
                else
                {
                    rv = bcm_switch_control_get(0, bcmSwitchHashSelectControl, &val);
                    BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                    val |= BCM_HASH_FIELD0_DISABLE_IP4;
                    rv = bcm_switch_control_set(0, bcmSwitchHashSelectControl, val);
                    BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                    BRCM_SAI_LOG_HASH(SAI_LOG_LEVEL_DEBUG, "Using L2 only fields for ECMP hashing\n");
                }
            }
            else
            {
                rv = bcm_switch_control_get(0, bcmSwitchHashL2Field0, &val);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                val |= (BCM_HASH_FIELD_SRCMOD | BCM_HASH_FIELD_SRCPORT);
                rv = bcm_switch_control_set(0, bcmSwitchHashL2Field0, val);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                rv = bcm_switch_control_get(0, bcmSwitchHashIP4Field0, &val);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                val |= (BCM_HASH_FIELD_SRCMOD | BCM_HASH_FIELD_SRCPORT);
                rv = bcm_switch_control_set(0, bcmSwitchHashIP4Field0, val);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                rv = bcm_switch_control_get(0, bcmSwitchHashIP4TcpUdpField0, &val);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                val |= (BCM_HASH_FIELD_SRCMOD | BCM_HASH_FIELD_SRCPORT);
                rv = bcm_switch_control_set(0, bcmSwitchHashIP4TcpUdpField0, val);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                rv = bcm_switch_control_get(0, bcmSwitchHashIP4TcpUdpPortsEqualField0, &val);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                val |= (BCM_HASH_FIELD_SRCMOD | BCM_HASH_FIELD_SRCPORT);
                rv = bcm_switch_control_set(0, bcmSwitchHashIP4TcpUdpPortsEqualField0, val);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);

                hash = BCM_HASH_FIELD0_ENABLE_UDFHASH;
                rv = _brcm_sai_global_data_bump(_BRCM_SAI_UDF_HASH_USED, INC);
                BRCM_SAI_RV_CHK(SAI_API_SWITCH, "used hash used", rv);
            }
            rv = bcm_switch_control_set(0, bcmSwitchUdfHashEnable, hash);
            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
            break;
        }
        case SAI_SWITCH_ATTR_ECMP_HASH_IPV6:
        {
            int h, hash = 0;
            bool l2 = FALSE, l3 = FALSE;
            _brcm_sai_indexed_data_t data;

            if (SAI_NULL_OBJECT_ID == attr->value.oid)
            {
                rv = _brcm_sai_load_balance_init(FALSE, 4);
                break;
            }
            if (BRCM_SAI_CHK_OBJ_MISMATCH(attr->value.oid, SAI_OBJECT_TYPE_HASH))
            {
                return SAI_STATUS_INVALID_OBJECT_TYPE;
            }
            h = BRCM_SAI_GET_OBJ_VAL(int, attr->value.oid);
            rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_HASH_INFO,
                                            &h, &data);
            BRCM_SAI_RV_CHK(SAI_API_SWITCH, "hash data get", rv);
            if (FALSE == data.hash.valid)
            {
                BRCM_SAI_LOG_HASH(SAI_LOG_LEVEL_ERROR,
                                  "Invalid object id 0x%016lx passed\n", attr->value.oid);
                return SAI_STATUS_INVALID_OBJECT_ID;
            }
            if (data.hash.hash_fields_count)
            {
                _brcm_sai_data_t gdata;

                rv = _brcm_sai_global_data_get(_BRCM_SAI_UDF_HASH_USED, &gdata);
                BRCM_SAI_RV_CHK(SAI_API_SWITCH, "hash data get", rv);
                if (0 == gdata.u32)
                {
                    rv = bcm_switch_control_get(0, bcmSwitchHashL2Field0, &val);
                    BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                    val &= ~(BCM_HASH_FIELD_SRCMOD | BCM_HASH_FIELD_SRCPORT);
                    rv = bcm_switch_control_set(0, bcmSwitchHashL2Field0, val);
                    BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                    rv = bcm_switch_control_get(0, bcmSwitchHashIP6Field0, &val);
                    BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                    val &= ~(BCM_HASH_FIELD_SRCMOD | BCM_HASH_FIELD_SRCPORT);
                    rv = bcm_switch_control_set(0, bcmSwitchHashIP6Field0, val);
                    BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                    rv = bcm_switch_control_get(0, bcmSwitchHashIP6TcpUdpField0, &val);
                    BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                    val &= ~(BCM_HASH_FIELD_SRCMOD | BCM_HASH_FIELD_SRCPORT);
                    rv = bcm_switch_control_set(0, bcmSwitchHashIP6TcpUdpField0, val);
                    BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                    rv = bcm_switch_control_get(0, bcmSwitchHashIP6TcpUdpPortsEqualField0, &val);
                    BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                    val &= ~(BCM_HASH_FIELD_SRCMOD | BCM_HASH_FIELD_SRCPORT);
                    rv = bcm_switch_control_set(0, bcmSwitchHashIP6TcpUdpPortsEqualField0, val);
                    BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                }
                for (h=0; h<data.hash.hash_fields_count; h++)
                {
                    switch (data.hash.hash_fields[h])
                    {
                        case SAI_NATIVE_HASH_FIELD_SRC_IP:
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP6Field0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= (BCM_HASH_FIELD_IP6SRC_HI | BCM_HASH_FIELD_IP6SRC_LO);
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP6Field0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP6TcpUdpField0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= (BCM_HASH_FIELD_IP6SRC_HI | BCM_HASH_FIELD_IP6SRC_LO);
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP6TcpUdpField0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP6TcpUdpPortsEqualField0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= (BCM_HASH_FIELD_IP6SRC_HI | BCM_HASH_FIELD_IP6SRC_LO);
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP6TcpUdpPortsEqualField0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);

                            l3 = TRUE;
                            break;
                        case SAI_NATIVE_HASH_FIELD_DST_IP:
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP6Field0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= (BCM_HASH_FIELD_IP6DST_HI | BCM_HASH_FIELD_IP6DST_LO);
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP6Field0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP6TcpUdpField0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= (BCM_HASH_FIELD_IP6DST_HI | BCM_HASH_FIELD_IP6DST_LO);
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP6TcpUdpField0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP6TcpUdpPortsEqualField0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= (BCM_HASH_FIELD_IP6DST_HI | BCM_HASH_FIELD_IP6DST_LO);
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP6TcpUdpPortsEqualField0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);

                            l3 = TRUE;
                            break;
                        case SAI_NATIVE_HASH_FIELD_VLAN_ID:
                            rv = bcm_switch_control_get(0, bcmSwitchHashL2Field0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= BCM_HASH_FIELD_VLAN;
                            rv = bcm_switch_control_set(0, bcmSwitchHashL2Field0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP6Field0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= BCM_HASH_FIELD_VLAN;
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP6Field0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP6TcpUdpField0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= BCM_HASH_FIELD_VLAN;
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP6TcpUdpField0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP6TcpUdpPortsEqualField0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= BCM_HASH_FIELD_VLAN;
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP6TcpUdpPortsEqualField0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            break;
                        case SAI_NATIVE_HASH_FIELD_IP_PROTOCOL:
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP6Field0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= BCM_HASH_FIELD_NXT_HDR;
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP6Field0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP6TcpUdpField0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= BCM_HASH_FIELD_NXT_HDR;
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP6TcpUdpField0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP6TcpUdpPortsEqualField0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= BCM_HASH_FIELD_NXT_HDR;
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP6TcpUdpPortsEqualField0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);

                            l3 = TRUE;
                            break;
                        case SAI_NATIVE_HASH_FIELD_ETHERTYPE:
                            rv = bcm_switch_control_get(0, bcmSwitchHashL2Field0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= BCM_HASH_FIELD_ETHER_TYPE;
                            rv = bcm_switch_control_set(0, bcmSwitchHashL2Field0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            l2 = TRUE;
                            break;
                        case SAI_NATIVE_HASH_FIELD_L4_SRC_PORT:
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP6Field0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= BCM_HASH_FIELD_SRCL4;
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP6Field0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP6TcpUdpField0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= BCM_HASH_FIELD_SRCL4;
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP6TcpUdpField0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP6TcpUdpPortsEqualField0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= BCM_HASH_FIELD_SRCL4;
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP6TcpUdpPortsEqualField0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);

                            l3 = TRUE;
                            break;
                        case SAI_NATIVE_HASH_FIELD_L4_DST_PORT:
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP6Field0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= BCM_HASH_FIELD_DSTL4;
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP6Field0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP6TcpUdpField0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= BCM_HASH_FIELD_DSTL4;
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP6TcpUdpField0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP6TcpUdpPortsEqualField0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= BCM_HASH_FIELD_DSTL4;
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP6TcpUdpPortsEqualField0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);

                            l3 = TRUE;
                            break;
                        case SAI_NATIVE_HASH_FIELD_SRC_MAC:
                            rv = bcm_switch_control_get(0, bcmSwitchHashL2Field0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= (BCM_HASH_FIELD_MACSA_HI | BCM_HASH_FIELD_MACSA_MI |
                                    BCM_HASH_FIELD_MACSA_LO);
                            rv = bcm_switch_control_set(0, bcmSwitchHashL2Field0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            l2 = TRUE;
                            break;
                        case SAI_NATIVE_HASH_FIELD_DST_MAC:
                            rv = bcm_switch_control_get(0, bcmSwitchHashL2Field0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= (BCM_HASH_FIELD_MACDA_HI | BCM_HASH_FIELD_MACDA_MI |
                                    BCM_HASH_FIELD_MACDA_LO);
                            rv = bcm_switch_control_set(0, bcmSwitchHashL2Field0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            l2 = TRUE;
                            break;
                        case SAI_NATIVE_HASH_FIELD_IN_PORT:
                            rv = bcm_switch_control_get(0, bcmSwitchHashL2Field0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= (BCM_HASH_FIELD_SRCMOD | BCM_HASH_FIELD_SRCPORT);
                            rv = bcm_switch_control_set(0, bcmSwitchHashL2Field0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP6Field0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= (BCM_HASH_FIELD_SRCMOD | BCM_HASH_FIELD_SRCPORT);
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP6Field0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP6TcpUdpField0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= (BCM_HASH_FIELD_SRCMOD | BCM_HASH_FIELD_SRCPORT);
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP6TcpUdpField0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            rv = bcm_switch_control_get(0, bcmSwitchHashIP6TcpUdpPortsEqualField0, &val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                            val |= (BCM_HASH_FIELD_SRCMOD | BCM_HASH_FIELD_SRCPORT);
                            rv = bcm_switch_control_set(0, bcmSwitchHashIP6TcpUdpPortsEqualField0, val);
                            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);

                            rv = _brcm_sai_global_data_bump(_BRCM_SAI_UDF_HASH_USED, INC);
                            BRCM_SAI_RV_CHK(SAI_API_SWITCH, "used hash used", rv);
                            break;
                        default:
                            break;
                    }
                }
                if (l2 && l3)
                {
                    BRCM_SAI_LOG_HASH(SAI_LOG_LEVEL_ERROR,
                                      "Can't support this combination of L2, L3 hash fields\n");
                    return SAI_STATUS_INVALID_PARAMETER;
                }
                else if (l3)
                {
                    rv = bcm_switch_control_get(0, bcmSwitchHashSelectControl, &val);
                    BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                    val &= ~BCM_HASH_FIELD0_DISABLE_IP6;
                    rv = bcm_switch_control_set(0, bcmSwitchHashSelectControl, val);
                    BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                    BRCM_SAI_LOG_HASH(SAI_LOG_LEVEL_DEBUG, "Using L3 fields for ECMP hashing\n");
                }
                else
                {
                    rv = bcm_switch_control_get(0, bcmSwitchHashSelectControl, &val);
                    BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                    val |= BCM_HASH_FIELD0_DISABLE_IP6;
                    rv = bcm_switch_control_set(0, bcmSwitchHashSelectControl, val);
                    BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                    BRCM_SAI_LOG_HASH(SAI_LOG_LEVEL_DEBUG, "Using L2 only fields for V6 ECMP hashing\n");
                }
            }
            else
            {
                rv = bcm_switch_control_get(0, bcmSwitchHashL2Field0, &val);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                val |= (BCM_HASH_FIELD_SRCMOD | BCM_HASH_FIELD_SRCPORT);
                rv = bcm_switch_control_set(0, bcmSwitchHashL2Field0, val);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                rv = bcm_switch_control_get(0, bcmSwitchHashIP6Field0, &val);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                val |= (BCM_HASH_FIELD_SRCMOD | BCM_HASH_FIELD_SRCPORT);
                rv = bcm_switch_control_set(0, bcmSwitchHashIP6Field0, val);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                rv = bcm_switch_control_get(0, bcmSwitchHashIP6TcpUdpField0, &val);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                val |= (BCM_HASH_FIELD_SRCMOD | BCM_HASH_FIELD_SRCPORT);
                rv = bcm_switch_control_set(0, bcmSwitchHashIP6TcpUdpField0, val);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                rv = bcm_switch_control_get(0, bcmSwitchHashIP6TcpUdpPortsEqualField0, &val);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
                val |= (BCM_HASH_FIELD_SRCMOD | BCM_HASH_FIELD_SRCPORT);
                rv = bcm_switch_control_set(0, bcmSwitchHashIP6TcpUdpPortsEqualField0, val);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);

                rv = _brcm_sai_global_data_bump(_BRCM_SAI_UDF_HASH_USED, INC);
                BRCM_SAI_RV_CHK(SAI_API_SWITCH, "used hash used", rv);
                hash = BCM_HASH_FIELD0_ENABLE_UDFHASH;
            }
            rv = bcm_switch_control_set(0, bcmSwitchUdfHashEnable, hash);
            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
            break;
        }
        case SAI_SWITCH_ATTR_MIRROR_TC:
        {
            if (0xff == attr->value.u8)
            {
                /* To disable */
                val = -1;
            }
            else
            {
                val = attr->value.u8;
            }
            rv = bcm_switch_control_set(0, bcmSwitchMirrorUnicastCosq, val);
            BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
            /* TH does not support redirecting Multicast packets to a
             * specific queue.
             */
            if (DEV_IS_TD2())
            {
                rv = bcm_switch_control_set(0, bcmSwitchMirrorMulticastCosq, val);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _SET_SWITCH, rv, attr->id);
            }
            break;
        }
        default:
            BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                                "Unknown switch attribute %d passed\n",
                                attr->id);
            rv = SAI_STATUS_INVALID_PARAMETER;;
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_SWITCH);
    return rv;
#undef _SET_SWITCH
}

sai_status_t
_brcm_sai_get_switch_attribute(_In_ uint32_t attr_count,
                               _Inout_ sai_attribute_t *attr_list)
{
#define _GET_SWITCH "Get switch"
    bcm_pbmp_t pbmp;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    int i, val;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_SWITCH);

    for (i = 0; i<attr_count; i++)
    {
        switch(attr_list[i].id)
        {
            case SAI_SWITCH_ATTR_PORT_LIST:
            {
                int p, limit, index = 0;
                sai_status_t rv1;
                uint32_t flags=0;
                _brcm_sai_indexed_data_t pdata;

                if (0 == BRCM_SAI_ATTR_LIST_OBJ_COUNT(i))
                {
                    rv = SAI_STATUS_INVALID_PARAMETER;
                    break;
                }
                _brcm_sai_switch_pbmp_fp_all_get(&pbmp);
                BCM_PBMP_COUNT(pbmp, p);
                if (BRCM_SAI_ATTR_LIST_OBJ_COUNT(i) < p)
                {
                    rv = SAI_STATUS_BUFFER_OVERFLOW;
                }
                limit = BRCM_SAI_ATTR_LIST_OBJ_COUNT(i) < p ?
                        BRCM_SAI_ATTR_LIST_OBJ_COUNT(i)-1 : p-1;
                BRCM_SAI_ATTR_LIST_OBJ_COUNT(i) = p;
                BCM_PBMP_ITER(pbmp, p)
                {
                    if (index > limit)
                    {
                        break;
                    }
                    BRCM_SAI_ATTR_LIST_OBJ_LIST(i, index++) =
                        BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_PORT, p);
                    rv1 = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_PORT_INFO,
                                                     &p, &pdata);
                    BRCM_SAI_RV_CHK(SAI_API_SWITCH, "port info data get", rv1);
                    pdata.port_info.bdg_port_admin_state = TRUE;
                    pdata.port_info.learn_flags = BCM_PORT_LEARN_ARL | BCM_PORT_LEARN_FWD;
                    rv1 = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_PORT_INFO,
                                                     &p, &pdata);
                    BRCM_SAI_RV_CHK(SAI_API_SWITCH, "port info data set", rv1);
                    /* read, modify, set.... remove RFILDR.
                     * Host IF TRAP/DROP packets were causing RFILDR to increment.
                     */
                    rv1 = bcm_stat_custom_get(0, p, snmpBcmCustomReceive0, &flags);
                    BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _GET_SWITCH, rv1,
                                          attr_list[i].id);
                    flags &= ~BCM_DBG_CNT_RFILDR;
                    rv1 = bcm_stat_custom_set(0, p, snmpBcmCustomReceive0, flags);
                    BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _GET_SWITCH, rv1,
                                          attr_list[i].id);
                }
                break;
            }
            case SAI_SWITCH_ATTR_MAX_VIRTUAL_ROUTERS:
                rv = bcm_switch_control_get(0, bcmSwitchVrfMax, &val);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _GET_SWITCH, rv,
                                      attr_list[i].id);
                attr_list[i].value.u32 = (uint32_t)val;
                break;
            case SAI_SWITCH_ATTR_ACL_ENTRY_MINIMUM_PRIORITY:
                attr_list[i].value.u32 = BCM_FIELD_ENTRY_PRIO_LOWEST;
                break;
            case SAI_SWITCH_ATTR_ACL_ENTRY_MAXIMUM_PRIORITY:
                attr_list[i].value.u32 = BCM_FIELD_ENTRY_PRIO_HIGHEST;
                break;
            case SAI_SWITCH_ATTR_MIRROR_TC:
                rv = bcm_switch_control_get(0, bcmSwitchMirrorUnicastCosq, &val);
                BRCM_SAI_ATTR_API_CHK(SAI_API_SWITCH, _GET_SWITCH, rv,
                                      attr_list[i].id);
                attr_list[i].value.u8 = (uint8_t)val;
                break;
            default:
                BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_ERROR,
                                    "Unknown switch attribute %d passed.\n",
                                    attr_list[i].id);
                rv = SAI_STATUS_INVALID_PARAMETER;
                break;
        }
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_INFO,
                                "Error processing switch attributes\n");
            return rv;
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_SWITCH);

    return rv;
#undef _GET_SWITCH
}

int
_brcm_sai_modport_port_get(bcm_gport_t port)
{
    if (BCM_GPORT_IS_MODPORT(port))
    {
        return BCM_GPORT_MODPORT_PORT_GET(port);
    }
    return -1;
}

/* mode 0 : all, 1: lag only, 2: ecmp only, 3: v4 only, 4: v6 only */
STATIC sai_status_t
_brcm_sai_load_balance_init(bool set, int mode)
{
    int arg;
    sai_status_t rv;

    if (mode <= 1)
    {
        /* For LAG */
        rv = bcm_switch_control_set(0, bcmSwitchHashField0Config,
                                    BCM_HASH_FIELD_CONFIG_CRC32LO);
        BRCM_SAI_API_CHK(SAI_API_SWITCH, "LAG hash config switch control", rv);
        arg = BCM_HASH_FIELD_MACSA_HI | BCM_HASH_FIELD_MACSA_MI |
              BCM_HASH_FIELD_MACSA_LO | BCM_HASH_FIELD_MACDA_HI |
              BCM_HASH_FIELD_MACDA_MI | BCM_HASH_FIELD_MACDA_LO |
              BCM_HASH_FIELD_ETHER_TYPE;
        rv =  bcm_switch_control_set(0, bcmSwitchHashL2Field0, arg);
        BRCM_SAI_API_CHK(SAI_API_SWITCH, "LAG L2 hash config switch control", rv);
    }

    if (0 == mode || mode > 1)
    {
        /* For ECMP:
         * For IPv4 packets, choose the four-tuples of IP packet along with
         * source and destination ports. This default is used for ECMP LB mode 6.
         * Note: We configure IPv4 packet field selection in the ECMP configuration,
         * below; i.e. bcmSwitchHashIP4Field0
         */

        if (0 == mode)
        {
            rv = bcm_switch_control_set(0, bcmSwitchHashField0Config1,
                                        BCM_HASH_FIELD_CONFIG_CRC32HI);
            BRCM_SAI_API_CHK(SAI_API_SWITCH, "ECMP hash config switch control", rv);
        }

        if (0 == mode || 3 == mode || 4 == mode)
        {
            rv = bcm_switch_control_set(0, bcmSwitchHashField0PreProcessEnable, 1);
            BRCM_SAI_API_CHK(SAI_API_SWITCH, "ECMP hash config switch control", rv);
        }
        if (0 == mode || 3 == mode)
        {
            arg = BCM_HASH_CONTROL_MULTIPATH_L4PORTS;
            rv =  bcm_switch_control_set(0, bcmSwitchHashControl, arg);
            BRCM_SAI_API_CHK(SAI_API_SWITCH, "ECMP hash config switch control", rv);
            /* Macro Flow is not available for Helix4 */
            if (!DEV_IS_HX4())
            {
                rv = bcm_switch_control_set(0, bcmSwitchEcmpMacroFlowHashEnable, FALSE);
                BRCM_SAI_API_CHK(SAI_API_SWITCH, "ECMP hash config switch control", rv);
            }
            rv = bcm_switch_control_set(0, bcmSwitchECMPHashSet0Offset, 68);
            BRCM_SAI_API_CHK(SAI_API_SWITCH, "ECMP hash config switch control", rv);
            arg = (BCM_HASH_FIELD_IP4SRC_HI | BCM_HASH_FIELD_IP4SRC_LO |
                   BCM_HASH_FIELD_IP4DST_HI | BCM_HASH_FIELD_IP4DST_LO |
                   BCM_HASH_FIELD_SRCL4 | BCM_HASH_FIELD_DSTL4 |
                   BCM_HASH_FIELD_PROTOCOL);
            rv = bcm_switch_control_set(0, bcmSwitchHashIP4Field0, set ? arg : 0);
            BRCM_SAI_API_CHK(SAI_API_SWITCH, "ECMP hash config switch control", rv);
            rv = bcm_switch_control_set(0, bcmSwitchHashIP4TcpUdpField0,
                                        set ? arg : 0);
            BRCM_SAI_API_CHK(SAI_API_SWITCH, "ECMP hash config switch control", rv);
            rv = bcm_switch_control_set(0, bcmSwitchHashIP4TcpUdpPortsEqualField0, set? arg : 0);
            BRCM_SAI_API_CHK(SAI_API_SWITCH, "ECMP eq SRC/DST ports hash config", rv);
        }
        if (0 == mode || 4 == mode)
        {
            arg = (BCM_HASH_FIELD_IP6SRC_HI | BCM_HASH_FIELD_IP6SRC_LO |
                   BCM_HASH_FIELD_IP6DST_HI | BCM_HASH_FIELD_IP6DST_LO |
                   BCM_HASH_FIELD_SRCL4 | BCM_HASH_FIELD_DSTL4 |
                   BCM_HASH_FIELD_NXT_HDR);
            rv = bcm_switch_control_set(0, bcmSwitchHashIP6Field0, set ? arg : 0);
            BRCM_SAI_API_CHK(SAI_API_SWITCH, "ECMP hash config switch control", rv);
            rv = bcm_switch_control_set(0, bcmSwitchHashIP6TcpUdpField0, set ? arg : 0);
            BRCM_SAI_API_CHK(SAI_API_SWITCH, "ECMP hash config switch control", rv);
            rv = bcm_switch_control_set(0, bcmSwitchHashIP6TcpUdpPortsEqualField0,
                                        set ? arg : 0);
            BRCM_SAI_API_CHK(SAI_API_SWITCH, "ECMP eq SRC/DST ports hash config", rv);
        }
        if (0 == mode)
        {
            rv = bcm_switch_control_get(0, bcmSwitchHashControl, &arg);
            BRCM_SAI_API_CHK(SAI_API_SWITCH, "ECMP hash config switch control", rv);
            arg |= (BCM_HASH_CONTROL_ECMP_ENHANCE);
            rv = bcm_switch_control_set(0, bcmSwitchHashControl, arg);
            BRCM_SAI_API_CHK(SAI_API_SWITCH, "ECMP hash config switch control", rv);
        }
    }

    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_switch_flex_counter_init()
{
    sai_status_t rv;
    /* init and create mode id for ingress and egress pg drop */
    /* For each pg, there are two counters for drop/non-drop, and each counter needs 2 selectors.*/
    uint32 total_counters = 2 * _BRCM_SAI_MAX_TC, num_selectors = 2 * total_counters;
    bcm_stat_group_mode_attr_selector_t attr_selectors[num_selectors];
    uint32 prio = 0, offset = 0, sel = 0;
    _brcm_sai_data_t data;

    for (sel = 0; sel< num_selectors/2; sel += 2)
    {
        bcm_stat_group_mode_attr_selector_t_init(&attr_selectors[sel]);
        attr_selectors[sel].counter_offset = offset;
        attr_selectors[sel].attr = bcmStatGroupModeAttrIntPri;
        attr_selectors[sel].attr_value = prio;
        prio++;
        attr_selectors[sel+1].counter_offset = offset;
        attr_selectors[sel+1].attr = bcmStatGroupModeAttrDrop;
        attr_selectors[sel+1].attr_value = 1;
        offset++;
    }

    prio = 0;
    for (sel = num_selectors/2; sel< num_selectors; sel += 2)
    {
        bcm_stat_group_mode_attr_selector_t_init(&attr_selectors[sel]);
        attr_selectors[sel].counter_offset = offset;
        attr_selectors[sel].attr = bcmStatGroupModeAttrIntPri;
        attr_selectors[sel].attr_value = prio;
        prio++;
        attr_selectors[sel+1].counter_offset = offset;
        attr_selectors[sel+1].attr = bcmStatGroupModeAttrDrop;
        attr_selectors[sel+1].attr_value = 0;
        offset++;
    }

    rv = bcm_stat_group_mode_id_create(0,
            BCM_STAT_GROUP_MODE_INGRESS,
            total_counters,
            num_selectors,
            attr_selectors,
            &ingress_pg_flex_counter_mode_id);
    BRCM_SAI_API_CHK(SAI_API_SWITCH, "create flex counter ingress pg mode", rv);
    data.u32 = ingress_pg_flex_counter_mode_id;
    _brcm_sai_global_data_set(_BRCM_SAI_ING_FLEX_CTR_MODE_ID,&data);

    rv = bcm_stat_group_mode_id_create(0,
            BCM_STAT_GROUP_MODE_EGRESS,
            total_counters,
            num_selectors,
            attr_selectors,
            &egress_pg_flex_counter_mode_id);
    BRCM_SAI_API_CHK(SAI_API_SWITCH, "create flex counter egress pg mode", rv);
    data.u32 = egress_pg_flex_counter_mode_id;
    _brcm_sai_global_data_set(_BRCM_SAI_EGR_FLEX_CTR_MODE_ID,&data);

    return SAI_STATUS_SUCCESS;
}

/* Used in WB mode to restore mode ids for flex counters */
sai_status_t _brcm_sai_switch_flex_counter_restore()
{
    _brcm_sai_data_t data;
    _brcm_sai_indexed_data_t idata;
    int p;
    sai_status_t rv;

    rv = _brcm_sai_global_data_get(_BRCM_SAI_ING_FLEX_CTR_MODE_ID, &data);
    BRCM_SAI_API_CHK(SAI_API_SWITCH, "restore flex counter ingress pg mode", rv);
    ingress_pg_flex_counter_mode_id = data.u32;

    rv = _brcm_sai_global_data_get(_BRCM_SAI_EGR_FLEX_CTR_MODE_ID, &data);
    BRCM_SAI_API_CHK(SAI_API_SWITCH, "restore flex counter egress pg mode", rv);
    egress_pg_flex_counter_mode_id = data.u32;

    BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_DEBUG, "Ing id %u egr mode %u\n",
                        ingress_pg_flex_counter_mode_id,
                        egress_pg_flex_counter_mode_id);

    BCM_PBMP_ITER(pbmp_all_front_panel, p)
    {
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_PORT_INFO,
                                        &p,
                                        &idata);
        BRCM_SAI_API_CHK(SAI_API_SWITCH,
                         "restore port flex data", rv);
        ingress_port_flex_counter_id_map[p] =  idata.port_info.flex_stat_map.ing_map;
        ingress_port_flex_counter_num_map[p] = idata.port_info.flex_stat_map.ing_ctr;
        egress_port_flex_counter_id_map[p] =   idata.port_info.flex_stat_map.egr_map;
        egress_port_flex_counter_num_map[p] =  idata.port_info.flex_stat_map.egr_ctr;
    }
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_switch_flex_counter_port_enable(int p)
{
    sai_status_t rv;
    bcm_gport_t gport;
    bcm_port_t port;
    _brcm_sai_indexed_data_t idata;

    rv = bcm_stat_custom_group_create(0,
            ingress_pg_flex_counter_mode_id,
            bcmStatObjectIngPort,
            &ingress_port_flex_counter_id_map[p],
            &ingress_port_flex_counter_num_map[p]);
    BRCM_SAI_API_CHK(SAI_API_SWITCH, "create ingress static counter id", rv);

    rv = bcm_stat_custom_group_create(0,
            egress_pg_flex_counter_mode_id,
            bcmStatObjectEgrPort,
            &egress_port_flex_counter_id_map[p],
            &egress_port_flex_counter_num_map[p]);
    BRCM_SAI_API_CHK(SAI_API_SWITCH, "create egress static counter id", rv);

    port = (BRCM_SAI_GET_OBJ_VAL(bcm_port_t, p));
    rv = bcm_port_gport_get(0, port, &gport);
    BRCM_SAI_API_CHK(SAI_API_SWITCH, "Get gport from port", rv);

    rv = bcm_port_stat_attach(0, gport, ingress_port_flex_counter_id_map[p]);
    BRCM_SAI_API_CHK(SAI_API_SWITCH, "Attach port to stat id", rv);

    rv = bcm_port_stat_attach(0, gport, egress_port_flex_counter_id_map[p]);
    BRCM_SAI_API_CHK(SAI_API_SWITCH, "Attach port to stat id", rv);

    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_PORT_INFO,
                                    &p,
                                    &idata);
    BRCM_SAI_API_CHK(SAI_API_SWITCH,
                     "restore port flex data", rv);

    idata.port_info.flex_stat_map.ing_map = ingress_port_flex_counter_id_map[p];
    idata.port_info.flex_stat_map.ing_ctr = ingress_port_flex_counter_num_map[p];
    idata.port_info.flex_stat_map.egr_map = egress_port_flex_counter_id_map[p];
    idata.port_info.flex_stat_map.egr_ctr = egress_port_flex_counter_num_map[p];

    /* Store in Syncdb */
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_PORT_INFO,
                                    &p, &idata);
    BRCM_SAI_API_CHK(SAI_API_SWITCH, "Syncdb add of flex stat", rv);
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_stats_init()
{
    sai_status_t rv;

    if (!DEV_IS_HX4())
    {
        /* Remove all of the FCoE custom counters. */
        rv = bcm_stat_custom_delete(0, 0, snmpBcmCustomReceive3,
                                    bcmDbgCntFcmPortClass3RxFrames);
        BRCM_SAI_API_CHK(SAI_API_SWITCH,
                         "Custom stat FcmPortClass3RxFrames delete", rv);
        rv = bcm_stat_custom_delete(0, 0, snmpBcmCustomReceive4,
                                    bcmDbgCntFcmPortClass3RxDiscards);
        BRCM_SAI_API_CHK(SAI_API_SWITCH,
                         "Custom stat FcmPortClass3RxDiscards delete", rv);
        rv = bcm_stat_custom_delete(0, 0, snmpBcmCustomReceive5,
                                    bcmDbgCntFcmPortClass2RxFrames);
        BRCM_SAI_API_CHK(SAI_API_SWITCH,
                         "Custom stat FcmPortClass2RxFrames delete", rv);
        rv = bcm_stat_custom_delete(0, 0, snmpBcmCustomReceive6,
                                    bcmDbgCntFcmPortClass2RxDiscards);
        BRCM_SAI_API_CHK(SAI_API_SWITCH,
                         "Custom stat FcmPortClass2RxDiscards delete", rv);
        rv = bcm_stat_custom_delete(0, 0, snmpBcmCustomTransmit6,
                                    bcmDbgCntFcmPortClass3TxFrames);
        BRCM_SAI_API_CHK(SAI_API_SWITCH,
                         "Custom stat FcmPortClass3TxFrames delete", rv);
        rv = bcm_stat_custom_delete(0, 0, snmpBcmCustomTransmit7,
                                    bcmDbgCntFcmPortClass2TxFrames);
        BRCM_SAI_API_CHK(SAI_API_SWITCH,
                         "Custom stat FcmPortClass2TxFrames delete", rv);
    }

    /* Add support for SAI requirements. */

    /* VLAN In Discards */
    if (DEV_IS_TD3())
    {
        rv = bcm_stat_custom_add(0, 0, snmpBcmCustomReceive3, bcmDbgCntRxVlanMismatch);
        BRCM_SAI_API_CHK(SAI_API_SWITCH, "Custom stat Rx Vlan Mismatch", rv);
        rv = bcm_stat_custom_add(0, 0, snmpBcmCustomReceive3, bcmDbgCntRxVlanMemberMismatch);
        BRCM_SAI_API_CHK(SAI_API_SWITCH, "Custom stat Rx Vlan Member Mismatch", rv);
        rv = bcm_stat_custom_add(0, 0, snmpBcmCustomReceive3, bcmDbgCntRxTpidMismatch);
        BRCM_SAI_API_CHK(SAI_API_SWITCH, "Custom stat Rx Tpid Mismatch", rv);
        rv = bcm_stat_custom_add(0, 0, snmpBcmCustomReceive3, bcmDbgCntRxPrivateVlanMismatch);
        BRCM_SAI_API_CHK(SAI_API_SWITCH, "Custom stat Rx Private Vlan Mismatch", rv);
    }
    else
    {
        if(!DEV_IS_TH3())
        {
            rv = bcm_stat_custom_add(0, 0, snmpBcmCustomReceive3, bcmDbgCntVLANDR);
            BRCM_SAI_API_CHK(SAI_API_SWITCH, "Custom stat VLANDR add", rv);
        }
    }
    /* IP In Receives v4 (Ucast + Mcast) */
    rv = bcm_stat_custom_add(0, 0, snmpBcmCustomReceive4, bcmDbgCntRIPC4);
    BRCM_SAI_API_CHK(SAI_API_SWITCH, "Custom stat RIPC4 add", rv);
    rv = bcm_stat_custom_add(0, 0, snmpBcmCustomReceive4, bcmDbgCntIMRP4);
    BRCM_SAI_API_CHK(SAI_API_SWITCH, "Custom stat IMRP4 add", rv);

    /* IP In Mcast v4 */
    rv = bcm_stat_custom_add(0, 0, snmpBcmCustomReceive5, bcmDbgCntIMRP4);
    BRCM_SAI_API_CHK(SAI_API_SWITCH, "Custom stat IMRP4 add", rv);

    /* IP In Ucast v6 */
    rv = bcm_stat_custom_add(0, 0, snmpBcmCustomReceive6, bcmDbgCntRIPC6);
    BRCM_SAI_API_CHK(SAI_API_SWITCH, "Custom stat RIPC6 add", rv);

    /* IP Out Ucast v4 */
    rv = bcm_stat_custom_add(0, 0, snmpBcmCustomTransmit6, bcmDbgCntTGIP4);
    BRCM_SAI_API_CHK(SAI_API_SWITCH, "Custom stat TGIP4 add", rv);

    /* IP Out Mcast v4 */
    rv = bcm_stat_custom_add(0, 0, snmpBcmCustomTransmit7, bcmDbgCntTGIPMC4);
    BRCM_SAI_API_CHK(SAI_API_SWITCH, "Custom stat TGIPMC4 add", rv);

    /* IP Out Discards v4 */
    rv = bcm_stat_custom_add(0, 0, snmpBcmCustomTransmit8, bcmDbgCntTIPD4);
    BRCM_SAI_API_CHK(SAI_API_SWITCH, "Custom stat TIPD4 add", rv);
    rv = bcm_stat_custom_add(0, 0, snmpBcmCustomTransmit8, bcmDbgCntTIPMCD4);
    BRCM_SAI_API_CHK(SAI_API_SWITCH, "Custom stat TIPMCD4 add", rv);

    /* IP Out Ucast v6 */
    rv = bcm_stat_custom_add(0, 0, snmpBcmCustomTransmit9, bcmDbgCntTGIP6);
    BRCM_SAI_API_CHK(SAI_API_SWITCH, "Custom stat TGIP6 add", rv);

    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_pol_sh_en()
{
    sai_status_t rv;

    rv = bcm_field_control_set(0, bcmFieldControlPolicerGroupSharingEnable, TRUE);
    BRCM_SAI_API_CHK(SAI_API_SWITCH, "field control set", rv);
    return rv;
}

sai_status_t
_brcm_sai_dscp_ecn_mode_set()
{
    sai_status_t rv = SAI_STATUS_FAILURE;

    if (DEV_IS_TD2())
    {
        rv = sh_process_command(0, "w EGR_DSCP_ECN_MAP 0 64 ECN_MODE=1");
        BRCM_SAI_API_CHK(SAI_API_SWITCH, "sh process cmd", rv);
    }
    else if (DEV_IS_THX() || DEV_IS_TD3())
    {
        /* For TH, keeping design point similar to TD2 behavior.
         * The following truth table is followed:
         *
         * Tunnel terminate:
         * Outer / Inner
         * X     /   00         - Do not copy
         * X     /   01,10      - Copy if Outer.ecn=11
         * X     /   11         - Do not copy
         *
         */
        int ecn_map_id;
        bcm_ecn_map_t ecn_map;

        rv = bcm_ecn_map_create(0, BCM_ECN_MAP_INGRESS | BCM_ECN_MAP_TUNNEL_TERM, &ecn_map_id);
        BRCM_SAI_API_CHK(SAI_API_SWITCH, "ecn map create", rv);
        ecn_map.action_flags = BCM_ECN_TRAFFIC_ACTION_INGRESS_ECN_MARKING;
        ecn_map.ecn = _BRCM_SAI_ECN_CE;
        ecn_map.inner_ecn = _BRCM_SAI_ECN_ECT1;
        ecn_map.new_ecn = _BRCM_SAI_ECN_CE;
        rv = bcm_ecn_map_set(0,0, ecn_map_id, &ecn_map);
        BRCM_SAI_API_CHK(SAI_API_SWITCH, "ecn map set", rv);
        ecn_map.inner_ecn = _BRCM_SAI_ECN_ECT0;
        rv = bcm_ecn_map_set(0,0, ecn_map_id, &ecn_map);
        BRCM_SAI_API_CHK(SAI_API_SWITCH, "ecn map set", rv);
    }
    else /* Helix4 */
    {
        BRCM_SAI_LOG_SWITCH(SAI_LOG_LEVEL_WARN, "set dscp ecn mode not supported");
        rv = SAI_STATUS_SUCCESS;
    }

    return rv;
}

STATIC sai_status_t
_brcm_sai_ecmp_members_get(int unit, int index, int intf_count,
                           bcm_if_t *info, void *user_data)
{
    bcm_if_t _dintf;
    sai_status_t rv = SAI_STATUS_FAILURE;

    rv = _brcm_sai_drop_if_get(&_dintf);
    BRCM_SAI_RV_CHK(SAI_API_SWITCH, "getting system drop intf global data", rv);
    if (((1 == intf_count) && (_dintf != info[0])) ||
         (1 < intf_count))
    {
        nhg_member_count += intf_count;
    }
    return rv;
}

STATIC sai_status_t
_brcm_sai_l2_count_init(void)
{
    sai_status_t rv = SAI_STATUS_FAILURE;
    _brcm_sai_data_t data;

    l2_entry_count = 0;
    rv = bcm_l2_traverse(0, _brcm_sai_l2_count_get, NULL);
    BRCM_SAI_API_CHK(SAI_API_SWITCH, "l2 traverse", rv);
    data.u32 = l2_entry_count;
    rv = _brcm_sai_global_data_set(_BRCM_SAI_FDB_COUNT, &data);
    BRCM_SAI_RV_CHK(SAI_API_SWITCH, "fdb global count get", rv);
    return rv;
}

STATIC sai_status_t
_brcm_sai_l2_count_get(int unit, bcm_l2_addr_t *l2addr, void *user_data)
{
    l2_entry_count++;
    return SAI_STATUS_SUCCESS;
}

STATIC sai_status_t
_brcm_sai_gather_fp_info(int unit,
                         bcm_field_group_t group,
                         void *user_data)
{
    sai_status_t rv = SAI_STATUS_SUCCESS;
    bcm_field_qset_t group_qset;
    bcm_field_group_mode_t group_mode;
    int stage, slice_count;

    /*
     *  This routine is invoked for every group.
     *
     *  1. For each group get qset
     *  2. For each group get group mode (Single, Double, etc.)
     *  3. Update counts
     *
     */

    rv = bcm_field_group_get(0, group, &group_qset);
    BRCM_SAI_API_CHK(SAI_API_SWITCH, "field group qset get", rv);
    rv = bcm_field_group_mode_get(0, group, &group_mode);
    BRCM_SAI_API_CHK(SAI_API_SWITCH, "field group status get", rv);

    stage = BCM_FIELD_QSET_TEST(group_qset, bcmFieldQualifyStageIngress) ?
        SAI_ACL_STAGE_INGRESS : SAI_ACL_STAGE_EGRESS;
    if ((bcmFieldGroupModeSingle == group_mode) ||
        (bcmFieldGroupModeIntraSliceDouble == group_mode))
    {
        slice_count = 1;
    }
    else if (bcmFieldGroupModeDouble == group_mode)
    {
        slice_count = 2;
    }
    else if (bcmFieldGroupModeTriple == group_mode)
    {
        slice_count = 3;
    }
    if (SAI_ACL_STAGE_INGRESS == stage)
    {
        ug_ing_count += slice_count;
    }
    else
    {
        ug_eg_count += slice_count;
    }

    return SAI_STATUS_SUCCESS;
}

int _brcm_sai_get_max_ecmp_members()
{
    if (!(DEV_IS_HX4() || DEV_IS_TD3()))
    {
        /* early return for most common devs */
        return _BRCM_SAI_MAX_ECMP_MEMBERS;
    }
    else if (DEV_IS_TD3())
    {
        return _BRCM_TD3_MAX_ECMP_MEMBERS;
    }
    else
    {
        return _BRCM_HX4_MAX_ECMP_MEMBERS;
    }
}

/*
################################################################################
#                                Functions map                                 #
################################################################################
*/
const sai_switch_api_t switch_apis = {
    brcm_sai_create_switch,
    brcm_sai_remove_switch,
    brcm_sai_set_switch_attribute,
    brcm_sai_get_switch_attribute,
};
