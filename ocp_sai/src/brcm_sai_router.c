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
#                          Non persistent local state                          #
################################################################################
*/
static int __brcm_sai_vr_max = 0;

/*
################################################################################
#                             Forward declarations                             #
################################################################################
*/
static sai_uint32_t
_brcm_sai_vr_count_get();

/*
################################################################################
#                              Router functions                                #
################################################################################
*/
/*
* Routine Description:
*    Create virtual router
*
* Arguments:
*    [in] vr_id - virtual router id
*    [in] attr_count - number of attributes
*    [in] attr_list - array of attributes
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_create_virtual_router(_Out_ sai_object_id_t *vr_id,
                               _In_ sai_object_id_t switch_id,
                               _In_ sai_uint32_t attr_count,
                               _In_ const sai_attribute_t *attr_list)
{
    int i, index;
    bool vmac = FALSE;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    bcm_l3_intf_t l3_intf;
    sai_uint32_t vr_count;
    _brcm_sai_vr_info_t vrf_map;
    _brcm_sai_indexed_data_t data;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_VIRTUAL_ROUTER);
    BRCM_SAI_SWITCH_INIT_CHECK;

    vr_count = _brcm_sai_vr_count_get();
    if (0 == __brcm_sai_vr_max)
    {
        BRCM_SAI_LOG_VR(SAI_LOG_LEVEL_ERROR, "NULL VR resource.\n");
        return SAI_STATUS_INSUFFICIENT_RESOURCES;
    }
    if (vr_count == __brcm_sai_vr_max)
    {
        BRCM_SAI_LOG_VR(SAI_LOG_LEVEL_ERROR, "No VR resource available.\n");
        return SAI_STATUS_INSUFFICIENT_RESOURCES;
    }
    /* Skip checking non-zero attributes for now as the system mac is used below */
    if (IS_NULL(vr_id))
    {
        BRCM_SAI_LOG_VR(SAI_LOG_LEVEL_ERROR, "NULL params passed.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    /* Reserve an unused id */
    rv = _brcm_sai_indexed_data_reserve_index(_BRCM_SAI_INDEXED_VR_INFO, 1,
                                              __brcm_sai_vr_max, &index);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_VR(SAI_LOG_LEVEL_ERROR, "Unexpected vrf resource issue.\n");
        return rv;
    }
    BRCM_SAI_LOG_VR(SAI_LOG_LEVEL_DEBUG, "Using vr_id: %d\n", index);
    if (SAI_STATUS_SUCCESS != _brcm_sai_global_data_bump(_BRCM_SAI_VR_COUNT, INC))
    {
        BRCM_SAI_LOG_VR(SAI_LOG_LEVEL_ERROR,
                        "Error incrementing vr count global data.\n");
        (void)_brcm_sai_indexed_data_free_index(_BRCM_SAI_INDEXED_VR_INFO, index);
        return SAI_STATUS_FAILURE;
    }

    bcm_l3_intf_t_init(&l3_intf);
    l3_intf.l3a_ttl = _BRCM_SAI_VR_DEFAULT_TTL;
    l3_intf.l3a_vrf = index;
    l3_intf.l3a_vid = 1;
    for (i=0; i<attr_count; i++)
    {
        if (SAI_VIRTUAL_ROUTER_ATTR_SRC_MAC_ADDRESS == attr_list[i].id)
        {
            sal_memcpy(l3_intf.l3a_mac_addr, attr_list[i].value.mac,
                       sizeof(l3_intf.l3a_mac_addr));
            vmac = TRUE;
            break;
        }
    }
    if (FALSE == vmac)
    {
        if ((rv = _brcm_sai_system_mac_get(&l3_intf.l3a_mac_addr))
            != SAI_STATUS_SUCCESS)
        {
            BRCM_SAI_LOG_VR(SAI_LOG_LEVEL_ERROR, "Error retreiving system mac.\n");
            (void)_brcm_sai_indexed_data_free_index(_BRCM_SAI_INDEXED_VR_INFO, index);
            return rv;
        }
    }
    sal_memcpy(vrf_map.vr_mac, l3_intf.l3a_mac_addr,
               sizeof(sai_mac_t));
    vrf_map.vr_id = index;
    vrf_map.ref_count = 0;
    data.vr_info = vrf_map;
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_VR_INFO, &index, &data);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_VR(SAI_LOG_LEVEL_ERROR, "Unable to save vr info data.\n");
        (void)_brcm_sai_indexed_data_free_index(_BRCM_SAI_INDEXED_VR_INFO, index);
        return rv;
    }
    *vr_id = BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_VIRTUAL_ROUTER, index);
    
    BRCM_SAI_FUNCTION_EXIT(SAI_API_VIRTUAL_ROUTER);

    return rv;
}

/*
* Routine Description:
*    Remove virtual router
*
* Arguments:
*    [in] vr_id - virtual router id
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_remove_virtual_router(_In_ sai_object_id_t vr_id)
{
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;
    sai_uint32_t _vr_id = BRCM_SAI_GET_OBJ_VAL(sai_uint32_t, vr_id);

    BRCM_SAI_FUNCTION_ENTER(SAI_API_VIRTUAL_ROUTER);
    BRCM_SAI_SWITCH_INIT_CHECK;

    if (__brcm_sai_vr_max < _vr_id)
    {
        BRCM_SAI_LOG_VR(SAI_LOG_LEVEL_DEBUG, "vr_id value %d out of bounds\n", _vr_id);
        return SAI_STATUS_INVALID_PARAMETER;
    }
    if (0 == _vr_id)
    {
        BRCM_SAI_LOG_VR(SAI_LOG_LEVEL_DEBUG, "can't delete global vr_id: %d\n", _vr_id);
        return SAI_STATUS_OBJECT_IN_USE;
    }
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_VR_INFO, (int*)&_vr_id, &data);
    BRCM_SAI_RV_CHK(SAI_API_VIRTUAL_ROUTER, "get vr info data", rv);
    if (data.vr_info.ref_count)
    {
        BRCM_SAI_LOG_VR(SAI_LOG_LEVEL_DEBUG, "vr_id: %d in use.\n", _vr_id);
        return SAI_STATUS_OBJECT_IN_USE;
    }
    (void)_brcm_sai_indexed_data_free_index(_BRCM_SAI_INDEXED_VR_INFO, _vr_id);
    if (SAI_STATUS_SUCCESS != _brcm_sai_global_data_bump(_BRCM_SAI_VR_COUNT, DEC))
    {
        BRCM_SAI_LOG_VR(SAI_LOG_LEVEL_ERROR,
                        "Error decrementing vr count global data.\n");
        return SAI_STATUS_FAILURE;
    }
    BRCM_SAI_LOG_VR(SAI_LOG_LEVEL_DEBUG, "freed vr_id: %d\n", _vr_id);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_VIRTUAL_ROUTER);

    return rv;
}

/*
* Routine Description:
*    Set virtual router attribute Value
*
* Arguments:
*    [in] vr_id - virtual router id
*    [in] attr - attribute
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_set_virtual_router_attribute(_In_ sai_object_id_t vr_id,
                                      _In_ const sai_attribute_t *attr)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_VIRTUAL_ROUTER);
    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_VIRTUAL_ROUTER);

    return rv;
}

/*
* Routine Description:
*    Get virtual router attribute Value
*
* Arguments:
*    [in] vr_id - virtual router id
*    [in] attr_count - number of attributes
*    [in] attr_list - array of attributes
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_get_virtual_router_attribute(_In_ sai_object_id_t vr_id,
                                      _In_ sai_uint32_t attr_count,
                                      _Inout_ sai_attribute_t *attr_list)
{
    int i, vrid;
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_VIRTUAL_ROUTER);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_GET_ATTRIB_PARAM_CHK;
    if (BRCM_SAI_CHK_OBJ_MISMATCH(vr_id, SAI_OBJECT_TYPE_VIRTUAL_ROUTER))
    {
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    vrid = BRCM_SAI_GET_OBJ_VAL(int, vr_id);
    BRCM_SAI_LOG_VR(SAI_LOG_LEVEL_DEBUG,
                    "Get vr id: %d\n", vrid);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_VR_INFO, &vrid, &data);
    BRCM_SAI_RV_CHK(SAI_API_VIRTUAL_ROUTER, "VR info get", rv);
    if (!data.vr_info.vr_id)
    {
        BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR, "Invalid VR\n");
        return SAI_STATUS_INVALID_OBJECT_ID;
    }
    for (i = 0; i<attr_count; i++)
    {
        switch(attr_list[i].id)
        {
            case SAI_VIRTUAL_ROUTER_ATTR_SRC_MAC_ADDRESS:
                sal_memcpy(attr_list[i].value.mac, data.vr_info.vr_mac,
                           sizeof(sai_mac_t));
                break;
            case SAI_VIRTUAL_ROUTER_ATTR_ADMIN_V4_STATE:
            case SAI_VIRTUAL_ROUTER_ATTR_ADMIN_V6_STATE:
                attr_list[i].value.booldata = TRUE;
                break;
            default:
                BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR,
                                    "Unknown VR attribute %d passed\n",
                                    attr_list[i].id);
                rv = SAI_STATUS_INVALID_PARAMETER;
                break;
        }
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_INFO,
                                "Error processing VR attributes\n");
            return rv;
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_VIRTUAL_ROUTER);

    return rv;
}

/*
################################################################################
#                                Internal functions                            #
################################################################################
*/
sai_uint32_t
_brcm_sai_vr_count_get()
{
    _brcm_sai_data_t data;
    if (SAI_STATUS_SUCCESS != _brcm_sai_global_data_get(_BRCM_SAI_VR_COUNT, &data))
    {
        BRCM_SAI_LOG_VR(SAI_LOG_LEVEL_CRITICAL,
                        "Error getting system vr count data.\n");
        return SAI_STATUS_SUCCESS;
    }
    return data.u32;
}

/* Routine to allocate vrf state */
sai_status_t
_brcm_sai_alloc_vrf()
{
    int max;
    sai_status_t rv;
    
    rv = _brcm_sai_switch_config_get(1, &max);
    BRCM_SAI_RV_CHK(SAI_API_VIRTUAL_ROUTER, "Switch config", rv);
    if (SAI_STATUS_SUCCESS != 
        _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_VR_INFO, max+1))
    {
        BRCM_SAI_LOG_VR(SAI_LOG_LEVEL_CRITICAL,
                        "Error initializing vrf state !!\n");
        return SAI_STATUS_FAILURE;
    }
    if (max)
    {
        __brcm_sai_vr_max = max;
    }
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_free_vrf()
{
    sai_status_t rv;
    
    rv = _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_VR_INFO, 
                                      0, __brcm_sai_vr_max,
                                      _brcm_sai_vr_count_get());
    BRCM_SAI_RV_LVL_CHK(SAI_API_VIRTUAL_ROUTER, SAI_LOG_LEVEL_CRITICAL,
                        "freeing vrf state", rv);

    return SAI_STATUS_SUCCESS;
}

/* Routine to verify if a vr_id is active/valid */
bool
_brcm_sai_vrf_valid(sai_uint32_t vr_id)
{
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;
    
    if (__brcm_sai_vr_max < vr_id)
    {
        return false;
    }
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_VR_INFO, (int *)&vr_id, &data);
    if (SAI_STATUS_SUCCESS == rv && data.vr_info.vr_id)
    {
        return true;
    }
    return false;
}

/* Routine to get vr properties */
sai_status_t
_brcm_sai_vrf_info_get(sai_uint32_t vr_id, sai_mac_t *mac)
{
    if (_brcm_sai_vrf_valid(vr_id))
    {
        _brcm_sai_indexed_data_t data;
        (void)_brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_VR_INFO, (int *)&vr_id, &data);
        
        sal_memcpy(mac, data.vr_info.vr_mac, sizeof(sai_mac_t));
        return SAI_STATUS_SUCCESS;
    }
    return SAI_STATUS_FAILURE;
}

bool _brcm_sai_vr_id_valid(sai_uint32_t vr_id)
{
    return (vr_id < __brcm_sai_vr_max);
}

sai_status_t
_brcm_sai_vrf_ref_count_update(sai_uint32_t vr_id, bool inc_dec)
{
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;

    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_VR_INFO, (int *)&vr_id, &data);
    BRCM_SAI_RV_CHK(SAI_API_ROUTER_INTERFACE, "vr info data set", rv);
    data.vr_info.ref_count = (INC == inc_dec) ?
                             data.vr_info.ref_count+1 : 
                             data.vr_info.ref_count-1;
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_VR_INFO, (int *)&vr_id, &data);
    BRCM_SAI_RV_CHK(SAI_API_ROUTER_INTERFACE, "vr info data set", rv);

    return rv;

}

/*
################################################################################
#                                Functions map                                 #
################################################################################
*/
const sai_virtual_router_api_t router_apis = {
    brcm_sai_create_virtual_router,
    brcm_sai_remove_virtual_router,
    brcm_sai_set_virtual_router_attribute,
    brcm_sai_get_virtual_router_attribute
};
