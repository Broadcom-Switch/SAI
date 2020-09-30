/*********************************************************************
 *
 * Copyright: (c) 2017 Broadcom.
 * Broadcom Proprietary and Confidential. All rights reserved.
 *
 *********************************************************************/

#include <sai.h>
#include <brcm_sai_common.h>

/*
################################################################################
#                                Local state                                   #
################################################################################
*/

static char *_brcm_sai_ver = BRCM_SAI_VER;
static char *_ocp_sai_ver = OCP_SAI_VER;
static char *_sdk_ver = SDK_VER;

typedef struct brcm_sai_api_map_s {
    sai_api_t api_id;
    const void *api_table;
} brcm_sai_api_map_t;

/*
################################################################################
#                                     API map                                  #
################################################################################
*/

const brcm_sai_api_map_t sai_api_map[] = {
    /* Note: Keep in sync with _brcm_sai_api_type_strings defined in common code */
    { SAI_API_UNSPECIFIED,      NULL },
    { SAI_API_SWITCH,           &switch_apis },
    { SAI_API_PORT,             &port_apis },
    { SAI_API_FDB,              &fdb_apis },
    { SAI_API_VLAN,             &vlan_apis },
    { SAI_API_VIRTUAL_ROUTER,   &router_apis },
    { SAI_API_ROUTE,            &route_apis },
    { SAI_API_NEXT_HOP,         &next_hop_apis },
    { SAI_API_NEXT_HOP_GROUP,   &next_hop_grp_apis },
    { SAI_API_ROUTER_INTERFACE, &router_intf_apis },
    { SAI_API_NEIGHBOR,         &neighbor_apis },
    { SAI_API_ACL,              &acl_apis },
    { SAI_API_HOSTIF,           &hostif_apis },
    { SAI_API_MIRROR,           &mirror_apis },
    { SAI_API_SAMPLEPACKET,     NULL },
    { SAI_API_STP,              NULL },
    { SAI_API_LAG,              &lag_apis },
    { SAI_API_POLICER,          &policer_apis },
    { SAI_API_WRED,             &wred_apis },
    { SAI_API_QOS_MAP,          &qos_map_apis },
    { SAI_API_QUEUE,            &qos_apis },
    { SAI_API_SCHEDULER,        &qos_scheduler_apis },
    { SAI_API_SCHEDULER_GROUP,  &scheduler_group_apis },
    { SAI_API_BUFFER,           &buffer_apis },
    { SAI_API_HASH,             &hash_api },
    { SAI_API_UDF,              &udf_apis },
    { SAI_API_TUNNEL,           &tunnel_apis },
    { SAI_API_L2MC,             NULL },
    { SAI_API_IPMC,             NULL },
    { SAI_API_RPF_GROUP,        NULL },
    { SAI_API_L2MC_GROUP,       NULL },
    { SAI_API_IPMC_GROUP,       NULL },
    { SAI_API_MCAST_FDB,        NULL },
    { SAI_API_BRIDGE,           &bridge_apis }
};

/*
################################################################################
#                               Global state                                   #
################################################################################
*/
static bool sai_api_inited = false;
sai_service_method_table_t host_services;
sai_log_level_t _adapter_log_level[BRCM_SAI_API_ID_MAX+1] = { SAI_LOG_LEVEL_WARN };

/*
################################################################################
#                              Well known functions                            #
################################################################################
*/

/*
* Routine Description:
*     Adapter module initialization call. This is NOT for SDK initialization.
*
* Arguments:
*     [in] flags - reserved for future use, must be zero
*     [in] services - methods table with services provided by adapter host
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
sai_status_t
sai_api_initialize(_In_ uint64_t flags,
                   _In_ const sai_service_method_table_t* services)
{

    BRCM_SAI_LOG("SAI Enter %s\n", __FUNCTION__);

    BRCM_SAI_INIT_LOG("BRCM SAI ver: [%s], OCP SAI ver: [%s], SDK ver: [%s]",
                      _brcm_sai_ver, _ocp_sai_ver, _sdk_ver);
    
    if (IS_NULL(services))
    {
        BRCM_SAI_INIT_LOG("NULL service_method_table_t* passed to %s\n",
                     __FUNCTION__);
        return SAI_STATUS_INVALID_PARAMETER;
    }
    if (IS_NULL(services->profile_get_next_value) ||
        IS_NULL(services->profile_get_value))
    {
        BRCM_SAI_INIT_LOG("NULL services fields* passed to %s\n",
                     __FUNCTION__);
        return SAI_STATUS_INVALID_PARAMETER;
    }
    memcpy(&host_services, services, sizeof(sai_service_method_table_t));
    sai_api_inited = true;

    BRCM_SAI_LOG("SAI Exit %s\n", __FUNCTION__);
    return SAI_STATUS_SUCCESS;
}

/*
* Routine Description:
*     Retrieve a pointer to the C-style method table for desired SAI
*     functionality as specified by the given sai_api_id.
*
* Arguments:
*     [in] sai_api_id - SAI api ID
*     [out] api_method_table - Caller allocated method table
*           The table must remain valid until the sai_api_uninitialize() is called
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
sai_status_t
sai_api_query(_In_ sai_api_t sai_api_id,
              _Out_ void** api_method_table)
{
    BRCM_SAI_LOG("SAI Enter %s\n", __FUNCTION__);
    if (!sai_api_inited)
    {
        BRCM_SAI_INIT_LOG("API not initialized yet.\n");
        return SAI_STATUS_UNINITIALIZED;
    }
    if (sai_api_id == SAI_API_UNSPECIFIED || sai_api_id > COUNTOF(sai_api_map))
    {
        BRCM_SAI_INIT_LOG("Invalid sai_api_t %d passed to %s\n", sai_api_id,
                     __FUNCTION__);
        return SAI_STATUS_INVALID_PARAMETER;
    }
    if (IS_NULL(sai_api_map[sai_api_id].api_table))
    {
        return SAI_STATUS_NOT_IMPLEMENTED;
    }
    *api_method_table = (void*)(sai_api_map[sai_api_id].api_table);

    BRCM_SAI_LOG("SAI Exit %s\n", __FUNCTION__);
    return SAI_STATUS_SUCCESS;
}

/*
* Routine Description:
*   Uninitialization of the adapter module. SAI functionalities, retrieved via
*   sai_api_query() cannot be used after this call.
*
* Arguments:
*   None
*
* Return Values:
*   SAI_STATUS_SUCCESS on success
*   Failure status code on error
*/
sai_status_t
sai_api_uninitialize(void)
{
    BRCM_SAI_LOG("SAI Enter %s\n", __FUNCTION__);

    memset(&host_services, 0, sizeof(sai_service_method_table_t));
    sai_api_inited = false;

    BRCM_SAI_LOG("SAI Exit %s\n", __FUNCTION__);
    return SAI_STATUS_SUCCESS;
}

/*
* Routine Description:
*     Set log level for sai api module. The default log level is SAI_LOG_LEVEL_WARN.
*
* Arguments:
*     [in] sai_api_id - SAI api ID
*     [in] log_level - log level
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
sai_status_t
sai_log_set(_In_ sai_api_t sai_api_id, _In_ sai_log_level_t log_level)
{
    BRCM_SAI_LOG("SAI Enter %s\n", __FUNCTION__);

    if ((sai_api_id >= BRCM_SAI_API_ID_MIN) &&
        (sai_api_id <= BRCM_SAI_API_ID_MAX) &&
        (log_level >= SAI_LOG_LEVEL_DEBUG) && (log_level <= SAI_LOG_LEVEL_CRITICAL))
    {
        _adapter_log_level[sai_api_id] = log_level;
    }
    else
    {
        return SAI_STATUS_INVALID_PARAMETER;
    }

    BRCM_SAI_LOG("SAI Exit %s\n", __FUNCTION__);
    return SAI_STATUS_SUCCESS;
}

/*
* Routine Description:
*     Query sai object type.
*
* Arguments:
*     [in] sai_object_id_t
*
* Return Values:
*    Return SAI_OBJECT_TYPE_NULL when sai_object_id is not valid.
*    Otherwise, return a valid sai object type SAI_OBJECT_TYPE_XXX
*/
sai_object_type_t 
sai_object_type_query(_In_ sai_object_id_t sai_object_id)
{
    sai_object_type_t obj;

    BRCM_SAI_LOG("SAI Enter %s\n", __FUNCTION__);
    obj = BRCM_SAI_GET_OBJ_TYPE(sai_object_id);
    if ((SAI_OBJECT_TYPE_NULL >= obj) || (SAI_OBJECT_TYPE_MAX <= obj))
    {
        return SAI_OBJECT_TYPE_NULL;
    }   
    BRCM_SAI_LOG("SAI Exit %s\n", __FUNCTION__);
    return obj;
}

/**
 * @brief Query sai switch id.
 *
 * @param[in] sai_object_id Object id
 *
 * @return Return #SAI_NULL_OBJECT_ID when sai_object_id is not valid.
 * Otherwise, return a valid SAI_OBJECT_TYPE_SWITCH object on which
 * provided object id belongs. If valid switch id object is provided
 * as input parameter it should returin itself.
 */
sai_object_id_t
sai_switch_id_query(_In_ sai_object_id_t sai_object_id)
{
    bcm_info_t info;
    sai_object_type_t obj;
    int rv, dev = 0, rev = 0;

    obj = BRCM_SAI_GET_OBJ_TYPE(sai_object_id);
    if ((SAI_OBJECT_TYPE_NULL >= obj) || (SAI_OBJECT_TYPE_MAX <= obj))
    {
        return SAI_NULL_OBJECT_ID;
    }   
    if (FALSE == _brcm_sai_switch_is_inited())
    {
        return SAI_NULL_OBJECT_ID;
    }
    /* FIXME: cache this info in the future */
    rv = bcm_info_get(0, &info);
    if (BCM_FAILURE(rv))
    {
        return SAI_NULL_OBJECT_ID;
    }
    dev = info.device;
    rev = info.revision;
    return BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_SWITCH, rev, dev, 0);
}

sai_status_t
sai_dbg_generate_dump(_In_ const char *dump_file_name)
{
    return SAI_STATUS_NOT_SUPPORTED;
}

_brcm_sai_version_t brcm_sai_version_get()
{
    _brcm_sai_version_t ver;
    ver.brcm_sai_ver = _brcm_sai_ver;
    ver.ocp_sai_ver = _ocp_sai_ver;
    ver.sdk_ver = _sdk_ver;

    return ver;
}

void _brcm_sai_ver_print()
{
    printf("BRCM SAI ver: [%s], OCP SAI ver: [%s], SDK ver: [%s]\n",
            _brcm_sai_ver, _ocp_sai_ver, _sdk_ver);
}

/*
################################################################################
#                                Internal functions                            #
################################################################################
*/
bool
_sai_api_is_inited(void)
{
    return sai_api_inited;
}
