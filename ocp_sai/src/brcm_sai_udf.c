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

/*
################################################################################
#                             Forward declarations                             #
################################################################################
*/
STATIC sai_uint32_t
_brcm_sai_udfg_count_get();
STATIC sai_uint32_t
_brcm_sai_udf_count_get(int *udf_count);
STATIC sai_int32_t
_brcm_sai_udf_shared_get(_brcm_sai_global_data_type_t type, int *shared_udf);
STATIC sai_status_t 
_brcm_sai_udfg_attach_object(int gid, sai_object_id_t object_id,
                             sai_uint8_t *hash_mask);
STATIC sai_status_t 
_brcm_sai_udfg_detach_object(int gid, sai_object_id_t object_id);

/*
################################################################################
#                                 UDF functions                                #
################################################################################
*/
/**
 * Routine Description:
 *    @brief Create UDF
 *
 * Arguments:
 *    @param[out] udf_id - UDF id
 *    @param[in] attr_count - number of attributes
 *    @param[in] attr_list - array of attributes
 *
 * Return Values:
 *    @return SAI_STATUS_SUCCESS on success
 *            Failure status code on error
 *
 */
STATIC sai_status_t
brcm_sai_create_udf(_Out_ sai_object_id_t* udf_id,
                    _In_ sai_object_id_t switch_id,
                    _In_ uint32_t attr_count,
                    _In_ const sai_attribute_t *attr_list)
{
    uint32 width;
    sai_status_t rv;
    _brcm_sai_data_t gdata;
    _brcm_sai_indexed_data_t data;
    _brcm_sai_global_data_type_t type;
    int layer = SAI_UDF_BASE_L2, pktid = -1, udfid;
    int i, j, gid = -1, start = -1, udf_count;
    int shared_udf, hash_mask_count = _BRCM_SAI_UDF_HASH_MASK_SIZE;
    sai_uint8_t hash_mask[_BRCM_SAI_UDF_HASH_MASK_SIZE] = { 0xff, 0xff };

    BRCM_SAI_FUNCTION_ENTER(SAI_API_UDF);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_OBJ_CREATE_PARAM_CHK(udf_id);
    
    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_UDF_ATTR_MATCH_ID:
                pktid = BRCM_SAI_GET_OBJ_VAL(int, attr_list[i].value.oid);
                break;
            case SAI_UDF_ATTR_GROUP_ID:
                gid = BRCM_SAI_GET_OBJ_VAL(int, attr_list[i].value.oid);
                break;
            case SAI_UDF_ATTR_BASE:
                layer = attr_list[i].value.s32;
                break;
            case SAI_UDF_ATTR_OFFSET:
                start = attr_list[i].value.u16;
                break;
            case SAI_UDF_ATTR_HASH_MASK:
            {
                hash_mask_count = attr_list[i].value.u8list.count;
                if (_BRCM_SAI_UDF_HASH_MASK_SIZE < hash_mask_count)
                {
                    return SAI_STATUS_INVALID_ATTR_VALUE_0;
                }
                for (j = 0; j < hash_mask_count; j++)
                {
                    hash_mask[j] = attr_list[i].value.u8list.list[j];
                }
                break;
            }
            default:
                break;
        }
    }
    if (-1 == start || -1 == pktid || 0 >= gid)
    {
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_UDFG_INFO,
                                    &gid, &data);
    BRCM_SAI_RV_CHK(SAI_API_UDF, "udf group data get", rv);
    width = data.udfg.length;
    if (0 == width)
    {
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }
    rv = _brcm_sai_udf_count_get(&udf_count);
    BRCM_SAI_RV_CHK(SAI_API_UDF, "udf count data get", rv);
    if (udf_count % 2)
    {
        type = _BRCM_SAI_UDF_SHARED_1;
    }
    else
    {
        type = _BRCM_SAI_UDF_SHARED_0;
    }
    rv = _brcm_sai_udf_shared_get(type, &shared_udf);
    BRCM_SAI_RV_CHK(SAI_API_UDF, "udf shared global data get", rv);

    if (SAI_STATUS_SUCCESS != _brcm_sai_global_data_bump(_BRCM_SAI_UDF_COUNT, INC))
    {
        BRCM_SAI_LOG_UDF(SAI_LOG_LEVEL_ERROR,
                         "Error incrementing udf count global data.\n");
        return SAI_STATUS_FAILURE;
    }
    rv = _brcm_sai_create_udf(pktid, layer, start, width, shared_udf,
                              hash_mask, &udfid);
    BRCM_SAI_RV_CHK(SAI_API_SWITCH, "udf create", rv);
    if (!shared_udf)
    {
        gdata.s32 = udfid;
        rv = _brcm_sai_global_data_set(type, &gdata);
        BRCM_SAI_RV_CHK(SAI_API_SWITCH, "udf shared global data set", rv);
    }
    *udf_id = BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_UDF, pktid, gid, udfid);
    BRCM_SAI_LOG_UDF(SAI_LOG_LEVEL_DEBUG, "Using udf id: %d\n", udfid);
    rv = _brcm_sai_udfg_attach_object(gid, *udf_id, hash_mask);
    BRCM_SAI_RV_CHK(SAI_API_UDF, "udf group attach udf obj", rv);
    
    BRCM_SAI_FUNCTION_EXIT(SAI_API_UDF);
    return rv;
}

/**
 * Routine Description:
 *    @brief Remove UDF
 *
 * Arguments:
 *    @param[in] udf_id - UDF id
 *
 * Return Values:
 *    @return SAI_STATUS_SUCCESS on success
 *            Failure status code on error
 */
STATIC sai_status_t
brcm_sai_remove_udf(_In_ sai_object_id_t udf_id)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_UDF);
    BRCM_SAI_SWITCH_INIT_CHECK;

    rv = _brcm_sai_udfg_detach_object(0, 0);
    BRCM_SAI_RV_CHK(SAI_API_UDF, "udf group attach udf obj", rv);
    rv = _brcm_sai_global_data_bump(_BRCM_SAI_UDF_COUNT, DEC);
    BRCM_SAI_RV_CHK(SAI_API_UDF, "decrementing udf count global data", rv);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_UDF);

    return rv;
}

/**
 * Routine Description:
 *    @brief Set UDF attribute
 *
 * Arguments:
 *    @param[in] udf_id - UDF id
 *    @param[in] attr - attribute
 *
 * Return Values:
 *    @return SAI_STATUS_SUCCESS on success
 *            Failure status code on error
 */
STATIC sai_status_t
brcm_sai_set_udf_attribute(_In_ sai_object_id_t udf_id,
                           _In_ const sai_attribute_t *attr)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_UDF);
    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_UDF);

    return rv;
}

/**
 * Routine Description:
 *    @brief Get UDF attribute value
 *
 * Arguments:
 *    @param[in] udf_id - UDF id
 *    @param[in] attr_count - number of attributes
 *    @param[inout] attrs - array of attributes
 *
 * Return Values:
 *    @return SAI_STATUS_SUCCESS on success
 *            Failure status code on error
 */
STATIC sai_status_t
brcm_sai_get_udf_attribute(_In_ sai_object_id_t udf_id,
                           _In_ uint32_t attr_count,
                           _Inout_ sai_attribute_t *attr_list)
{
    sai_status_t rv;
    bcm_udf_t udf_info;
    int i, udfid, mchid, gid;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_UDF);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_GET_ATTRIB_PARAM_CHK;
    if (BRCM_SAI_CHK_OBJ_MISMATCH(udf_id, SAI_OBJECT_TYPE_UDF))
    {
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    udfid = BRCM_SAI_GET_OBJ_VAL(int, udf_id);
    BRCM_SAI_LOG_UDF(SAI_LOG_LEVEL_DEBUG,
                     "Get udf: %d\n", udfid);
    mchid = BRCM_SAI_GET_OBJ_SUB_TYPE(udf_id);
    gid = BRCM_SAI_GET_OBJ_MAP(udf_id);
    rv = bcm_udf_get(0, udfid, &udf_info);
    BRCM_SAI_API_CHK(SAI_API_UDF, "udf get", rv);
    for (i = 0; i<attr_count; i++)
    {
        switch(attr_list[i].id)
        {
            case SAI_UDF_ATTR_MATCH_ID:
                BRCM_SAI_ATTR_LIST_OBJ(i) =
                    BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_UDF_MATCH, mchid);
                break;
            case SAI_UDF_ATTR_GROUP_ID:
                BRCM_SAI_ATTR_LIST_OBJ(i) =
                    BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_UDF_GROUP, gid);
                break;
            case SAI_UDF_ATTR_BASE:
                switch (udf_info.layer)
                {
                    case bcmUdfLayerL2Header:
                            attr_list[i].value.s32 = SAI_UDF_BASE_L2;
                        break;
                    case bcmUdfLayerL3OuterHeader:
                            attr_list[i].value.s32 = SAI_UDF_BASE_L3;
                        break;
                    case bcmUdfLayerL4OuterHeader:
                            attr_list[i].value.s32 = SAI_UDF_BASE_L4;
                        break;
                    default:
                        break;
                }
                break;
            case SAI_UDF_ATTR_OFFSET:
                attr_list[i].value.u16 = udf_info.start/8;
                break;
            case SAI_UDF_ATTR_HASH_MASK:
                /* FIXME */
                break;
            default:
                BRCM_SAI_LOG_UDF(SAI_LOG_LEVEL_ERROR,
                                 "Unknown UDF attribute %d passed\n",
                                 attr_list[i].id);
                rv = SAI_STATUS_INVALID_PARAMETER;
                break;
        }
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_UDF(SAI_LOG_LEVEL_INFO,
                             "Error processing UDF attributes\n");
            return rv;
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_UDF);

    return rv;
}

/**
 * Routine Description:
 *    @brief Create UDF match
 *
 * Arguments:
 *    @param[out] udf_match_id - UDF match id
 *    @param[in] attr_count - number of attributes
 *    @param[in] attr_list - array of attributes
 *
 * Return Values:
 *    @return SAI_STATUS_SUCCESS on success
 *            Failure status code on error
 *
 */
STATIC sai_status_t
brcm_sai_create_udf_match(_Out_ sai_object_id_t* udf_match_id,
                          _In_ sai_object_id_t switch_id,
                          _In_ uint32_t attr_count,
                          _In_ const sai_attribute_t *attr_list)
{
    sai_status_t rv;
    
    BRCM_SAI_FUNCTION_ENTER(SAI_API_UDF);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_OBJ_CREATE_PARAM_CHK(udf_match_id);

    rv = _brcm_sai_create_udf_match(udf_match_id, attr_count, attr_list);
    
    BRCM_SAI_FUNCTION_EXIT(SAI_API_UDF);
    return rv;
}

/**
 * Routine Description:
 *    @brief Remove UDF match
 *
 * Arguments:
 *    @param[in] udf_match_id - UDF match id
 *
 * Return Values:
 *    @return SAI_STATUS_SUCCESS on success
 *            Failure status code on error
 */
STATIC sai_status_t
brcm_sai_remove_udf_match(_In_ sai_object_id_t udf_match_id)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_UDF);
    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_UDF);

    return rv;
}

/**
 * Routine Description:
 *    @brief Set UDF match attribute
 *
 * Arguments:
 *    @param[in] udf_match_id - UDF match id
 *    @param[in] attr - attribute
 *
 * Return Values:
 *    @return SAI_STATUS_SUCCESS on success
 *            Failure status code on error
 */
STATIC sai_status_t
brcm_sai_set_udf_match_attribute(_In_ sai_object_id_t udf_match_id,
                                 _In_ const sai_attribute_t *attr)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_UDF);
    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_UDF);

    return rv;
}

/**
 * Routine Description:
 *    @brief Get UDF match attribute value
 *
 * Arguments:
 *    @param[in] udf_match_id - UDF match id
 *    @param[in] attr_count - number of attributes
 *    @param[inout] attrs - array of attributes
 *
 * Return Values:
 *    @return SAI_STATUS_SUCCESS on success
 *            Failure status code on error
 */
STATIC sai_status_t
brcm_sai_get_udf_match_attribute(_In_ sai_object_id_t udf_match_id,
                                 _In_ uint32_t attr_count,
                                 _Inout_ sai_attribute_t *attr_list)
{
    int i;
    sai_status_t rv;
    bcm_udf_pkt_format_id_t pktfmt_id; 
    bcm_udf_pkt_format_info_t pkt_format;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_UDF);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_GET_ATTRIB_PARAM_CHK;
    if (BRCM_SAI_CHK_OBJ_MISMATCH(udf_match_id, SAI_OBJECT_TYPE_UDF_MATCH))
    {
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    pktfmt_id = BRCM_SAI_GET_OBJ_VAL(int, udf_match_id);
    BRCM_SAI_LOG_UDF(SAI_LOG_LEVEL_DEBUG,
                     "Get udf match: %d\n", pktfmt_id);

    rv = bcm_udf_pkt_format_info_get(0, pktfmt_id, &pkt_format);
    BRCM_SAI_API_CHK(SAI_API_UDF, "udf pkt format info get", rv);
    for (i = 0; i<attr_count; i++)
    {
        switch(attr_list[i].id)
        {
            case SAI_UDF_MATCH_ATTR_L2_TYPE:
                attr_list[i].value.u16 = pkt_format.ethertype;
                break;
            case SAI_UDF_MATCH_ATTR_L3_TYPE:
                attr_list[i].value.u8 = pkt_format.ip_protocol;
                break;
            case SAI_UDF_MATCH_ATTR_GRE_TYPE:
                if (BCM_PKT_FORMAT_IP4 == pkt_format.inner_ip)
                {
                    attr_list[i].value.u16 = 0x800;
                }
                else if (BCM_PKT_FORMAT_IP6 == pkt_format.inner_ip)
                {
                    attr_list[i].value.u16 = 0x86dd;
                }
                break;
            case SAI_UDF_MATCH_ATTR_PRIORITY:
                attr_list[i].value.u8 = pkt_format.prio;
                break;
            default:
                BRCM_SAI_LOG_UDF(SAI_LOG_LEVEL_ERROR,
                                 "Unknown UDF match attribute %d passed\n",
                                 attr_list[i].id);
                rv = SAI_STATUS_INVALID_PARAMETER;
                break;
        }
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_UDF(SAI_LOG_LEVEL_INFO,
                             "Error processing UDF match attributes\n");
            return rv;
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_UDF);

    return rv;
}

/**
 * Routine Description:
 *    @brief Create UDF group
 *
 * Arguments:
 *    @param[out] udf_group_id - UDF group id
 *    @param[in] attr_count - number of attributes
 *    @param[in] attr_list - array of attributes
 *
 * Return Values:
 *    @return SAI_STATUS_SUCCESS on success
 *            Failure status code on error
 *
 */
STATIC sai_status_t
brcm_sai_create_udf_group(_Out_ sai_object_id_t* udf_group_id,
                          _In_ sai_object_id_t switch_id,
                          _In_ uint32_t attr_count,
                          _In_ const sai_attribute_t *attr_list)
{
    int i, length = -1, type = -1;
    _brcm_sai_indexed_data_t data;
    sai_status_t rv = SAI_STATUS_SUCCESS;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_UDF);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_OBJ_CREATE_PARAM_CHK(udf_group_id);
    
    i = _brcm_sai_udfg_count_get();
    if (i == _BRCM_SAI_MAX_UDF_GROUPS)
    {
        BRCM_SAI_LOG_UDF(SAI_LOG_LEVEL_ERROR, "No UDF group resource available.\n");
        return SAI_STATUS_INSUFFICIENT_RESOURCES;
    }
    for (i=0; i<attr_count; i++)
    {
        if (SAI_UDF_GROUP_ATTR_TYPE == attr_list[i].id)
        {
            if (SAI_UDF_GROUP_TYPE_HASH != attr_list[i].value.s32)
            {
                return SAI_STATUS_ATTR_NOT_SUPPORTED_0;
            }
            type = attr_list[i].value.s32;
        }
        else if (SAI_UDF_GROUP_ATTR_LENGTH == attr_list[i].id)
        {
            length = attr_list[i].value.u16;
        }
    }
    if (SAI_UDF_GROUP_TYPE_HASH != type || 0 > length)
    {
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }

    /* Reserve an unused id */
    rv = _brcm_sai_indexed_data_reserve_index(_BRCM_SAI_INDEXED_UDFG_INFO, 1,
                                              _BRCM_SAI_MAX_UDF_GROUPS, &i);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_UDF(SAI_LOG_LEVEL_ERROR, "Unexpected udf group resource issue.\n");
        return rv;
    }
    BRCM_SAI_LOG_UDF(SAI_LOG_LEVEL_DEBUG, "Using udf group id: %d\n", i);
    *udf_group_id = BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_UDF_GROUP, i);
    if (SAI_STATUS_SUCCESS != _brcm_sai_global_data_bump(_BRCM_SAI_UDFG_COUNT, INC))
    {
        BRCM_SAI_LOG_UDF(SAI_LOG_LEVEL_ERROR,
                         "Error incrementing udf group count global data.\n");
        (void)_brcm_sai_indexed_data_free_index(_BRCM_SAI_INDEXED_UDFG_INFO, i);
        return SAI_STATUS_FAILURE;
    }
    
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_UDFG_INFO,
                                    &i, &data);
    BRCM_SAI_RV_CHK(SAI_API_UDF, "udf group data get", rv);
    data.udfg.idx = i;
    data.udfg.length = length;
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_UDFG_INFO,
                                    &i, &data);
    BRCM_SAI_RV_CHK(SAI_API_UDF, "udf group data set", rv);
    
    BRCM_SAI_FUNCTION_EXIT(SAI_API_UDF);
    return rv;
}

/**
 * Routine Description:
 *    @brief Remove UDF group
 *
 * Arguments:
 *    @param[in] udf_group_id - UDF group id
 *
 * Return Values:
 *    @return SAI_STATUS_SUCCESS on success
 *            Failure status code on error
 */
STATIC sai_status_t
brcm_sai_remove_udf_group(_In_ sai_object_id_t udf_group_id)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_UDF);
    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_UDF);

    return rv;
}

/**
 * Routine Description:
 *    @brief Set UDF group attribute
 *
 * Arguments:
 *    @param[in] udf_group_id - UDF group id
 *    @param[in] attr - attribute
 *
 * Return Values:
 *    @return SAI_STATUS_SUCCESS on success
 *            Failure status code on error
 */
STATIC sai_status_t
brcm_sai_set_udf_group_attribute(_In_ sai_object_id_t udf_group_id,
                                 _In_ const sai_attribute_t *attr)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_UDF);
    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_UDF);

    return rv;
}

/**
 * Routine Description:
 *    @brief Get UDF group attribute value
 *
 * Arguments:
 *    @param[in] udf_group_id - UDF group id
 *    @param[in] attr_count - number of attributes
 *    @param[inout] attrs - array of attributes
 *
 * Return Values:
 *    @return SAI_STATUS_SUCCESS on success
 *            Failure status code on error
 */
STATIC sai_status_t
brcm_sai_get_udf_group_attribute(_In_ sai_object_id_t udf_group_id,
                                 _In_ uint32_t attr_count,
                                 _Inout_ sai_attribute_t *attr_list)
{
    int i, ugid;
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_UDF);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_GET_ATTRIB_PARAM_CHK;
    if (BRCM_SAI_CHK_OBJ_MISMATCH(udf_group_id, SAI_OBJECT_TYPE_UDF_GROUP))
    {
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    ugid = BRCM_SAI_GET_OBJ_VAL(int, udf_group_id);
    BRCM_SAI_LOG_UDF(SAI_LOG_LEVEL_DEBUG,
                     "Get udf group: %d\n", ugid);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_UDFG_INFO,
                                    &ugid, &data);
    BRCM_SAI_RV_CHK(SAI_API_UDF, "acl table group data get", rv);
    for (i = 0; i<attr_count; i++)
    {
        switch(attr_list[i].id)
        {
            case SAI_UDF_GROUP_ATTR_TYPE:
                attr_list[i].value.s32 = SAI_UDF_GROUP_TYPE_HASH;
                break;
            case SAI_UDF_GROUP_ATTR_LENGTH:
                attr_list[i].value.u16 = data.udfg.length;
                break;
            default:
                BRCM_SAI_LOG_UDF(SAI_LOG_LEVEL_ERROR,
                                 "Unknown UDF group attribute %d passed\n",
                                 attr_list[i].id);
                rv = SAI_STATUS_INVALID_PARAMETER;
                break;
        }
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_UDF(SAI_LOG_LEVEL_INFO,
                             "Error processing UDF group attributes\n");
            return rv;
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_UDF);

    return rv;
}

/*
################################################################################
#                              Internal functions                              #
################################################################################
*/
STATIC sai_uint32_t
_brcm_sai_udfg_count_get()
{
    _brcm_sai_data_t data;
    if (SAI_STATUS_SUCCESS != _brcm_sai_global_data_get(_BRCM_SAI_UDFG_COUNT, &data))
    {
        BRCM_SAI_LOG_UDF(SAI_LOG_LEVEL_CRITICAL,
                         "Error getting udf group count data.\n");
        return SAI_STATUS_FAILURE;
    }
    return data.u32;
}

STATIC sai_uint32_t
_brcm_sai_udf_count_get(int *udf_count)
{
    _brcm_sai_data_t data;
    if (SAI_STATUS_SUCCESS != _brcm_sai_global_data_get(_BRCM_SAI_UDF_COUNT, &data))
    {
        BRCM_SAI_LOG_UDF(SAI_LOG_LEVEL_CRITICAL,
                         "Error getting udf count data.\n");
        return SAI_STATUS_FAILURE;
    }
    *udf_count = data.u32;
    return SAI_STATUS_SUCCESS;
}

STATIC sai_int32_t
_brcm_sai_udf_shared_get(_brcm_sai_global_data_type_t type, int *shared_udf)
{
    _brcm_sai_data_t data;
    if (SAI_STATUS_SUCCESS != _brcm_sai_global_data_get(type, &data))
    {
        BRCM_SAI_LOG_UDF(SAI_LOG_LEVEL_CRITICAL,
                         "Error getting udf shared data.\n");
        return SAI_STATUS_FAILURE;
    }
    *shared_udf = data.s32;
    return SAI_STATUS_SUCCESS;
}

/* Routine to allocate udf state */
sai_status_t
_brcm_sai_alloc_udf()
{
    int s;
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;

    if (SAI_STATUS_SUCCESS != 
        _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_UDFG_INFO,
                                    _BRCM_SAI_MAX_UDF_GROUPS+1))
    {
        BRCM_SAI_LOG_UDF(SAI_LOG_LEVEL_CRITICAL,
                         "Error initializing udf group state !!\n");
        return SAI_STATUS_FAILURE;
    }
    
    if (_brcm_sai_switch_wb_state_get())
    {
        if (_brcm_sai_udfg_count_get())
        {
            for (s = 1; s <= _BRCM_SAI_MAX_UDF_GROUPS; s++)
            {
                rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_UDFG_INFO,
                                                &s, &data);
                BRCM_SAI_RV_CHK(SAI_API_UDF, "udf group data get", rv);
                if (data.udfg.valid && data.udfg.refs && data.udfg.ref_count)
                {
                    rv = _brcm_sai_list_init(_BRCM_SAI_LIST_UDFG_UDF_MAP,
                                             s, data.udfg.ref_count,
                                             (void**)&data.udfg.refs);
                    BRCM_SAI_RV_CHK(SAI_API_UDF, "list init udf group refs", rv);
                    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_UDFG_INFO,
                                                    &s, &data);
                    BRCM_SAI_RV_CHK(SAI_API_UDF, "udf group data set", rv);
                }
            }
        }
    }

    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_free_udf()
{
    int s;
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;

    for (s = 1; s <= _BRCM_SAI_MAX_UDF_GROUPS; s++)
    {
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_UDFG_INFO,
                                        &s, &data);
        BRCM_SAI_RV_CHK(SAI_API_UDF, "udf group data get", rv);
        if (data.udfg.valid && data.udfg.refs)
        {
            rv = _brcm_sai_list_free(_BRCM_SAI_LIST_UDFG_UDF_MAP,
                                     s, data.udfg.ref_count, data.udfg.refs);
            BRCM_SAI_RV_CHK(SAI_API_UDF, "list free udf group refs", rv);
            data.udfg.refs = (_brcm_sai_udf_object_t*)(uint64_t)s;
            rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_UDFG_INFO,
                                            &s, &data);
            BRCM_SAI_RV_CHK(SAI_API_UDF, "udf group data set", rv);
        }
    }
    rv = _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_UDFG_INFO, 
                                      1, _BRCM_SAI_MAX_UDF_GROUPS,
                                      _brcm_sai_udfg_count_get());
    BRCM_SAI_RV_LVL_CHK(SAI_API_UDF, SAI_LOG_LEVEL_CRITICAL,
                        "freeing udf state", rv);

    return SAI_STATUS_SUCCESS;
}

STATIC sai_status_t 
_brcm_sai_udfg_attach_object(int gid, sai_object_id_t object_id,
                             sai_uint8_t *hash_mask)
{
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;
    _brcm_sai_udf_object_t *current;
    _brcm_sai_udf_object_t *prev = NULL, *new_object;
    int i;
    
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_UDFG_INFO,
                                    &gid, &data);
    BRCM_SAI_RV_CHK(SAI_API_UDF, "udf group data get", rv);
    if (FALSE == data.udfg.valid)
    {
        return SAI_STATUS_INVALID_PARAMETER;
    }
    current = data.udfg.refs;
    
    /* See if the object is already in the list. */
    while (NULL != current)
    {
        if (current->object_id == object_id)
        {
            /* Node found */
            return SAI_STATUS_SUCCESS;
        }
        prev = current;
        current = current->next;
    }
    current = prev;
    
    /* Need to add to the list. */
    new_object = ALLOC_CLEAR(1, sizeof(_brcm_sai_udf_object_t));
    if (IS_NULL(new_object))
    {
        BRCM_SAI_LOG_UDF(SAI_LOG_LEVEL_CRITICAL,
                               "Error allocating memory for udfg refs list.\n");
        return SAI_STATUS_NO_MEMORY;
    }
    new_object->object_id = object_id;
    new_object->next = NULL;
    for (i = 0; i < _BRCM_SAI_UDF_HASH_MASK_SIZE; i++)
    {
        new_object->hash_mask[i] = hash_mask[i];
    }
    if (IS_NULL(current))
    {
        /* 1st object */
        current = new_object;
        data.udfg.refs = current;
    }
    else
    {
        current->next = new_object;
    }
    data.udfg.ref_count++;
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_UDFG_INFO,
                                    &gid, &data);
    BRCM_SAI_RV_CHK(SAI_API_UDF, "udf group data set", rv);
    
    return SAI_STATUS_SUCCESS;
}

STATIC sai_status_t 
_brcm_sai_udfg_detach_object(int gid, sai_object_id_t object_id)
{
    return SAI_STATUS_SUCCESS;
}

sai_status_t 
_brcm_sai_udfg_get_next_udf(int gid, sai_object_id_t **object_id)
{
    sai_status_t rv;
    bool no_match = TRUE;
    _brcm_sai_indexed_data_t data;
    _brcm_sai_udf_object_t *current;
    
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_UDFG_INFO,
                                    &gid, &data);
    BRCM_SAI_RV_CHK(SAI_API_UDF, "udf group data get", rv);
    if (FALSE == data.udfg.valid)
    {
        return SAI_STATUS_INVALID_PARAMETER;
    }
    current = data.udfg.refs;
    
    if (IS_NULL(current) || 0 == data.udfg.ref_count)
    {
        return SAI_STATUS_SUCCESS;
    }
    if (IS_NULL(*object_id))
    {
        *object_id = &current->object_id;
        no_match = FALSE;
    }
    else
    {
        /* Find object in the list and return next value if it exists */
        while (NULL != current)
        {
            if (current->object_id == **object_id && current->next)
            {
                *object_id = &current->next->object_id;
                no_match = FALSE;
                break;
            }
            current = current->next;
        }        
    }
    if (no_match)
    {
        *object_id = NULL;
    }
    
    return SAI_STATUS_SUCCESS;
}

/*******************************Closed code************************************/
sai_status_t
_brcm_sai_create_udf(int pktid, int layer, int start, int width, int shared,
                     sai_uint8_t hash_mask[_BRCM_SAI_UDF_HASH_MASK_SIZE],
                     _Out_ int* udf_id)
{
    sai_status_t rv;
    bcm_udf_t udf_info;
    bcm_udf_id_t udfid;
    bcm_udf_alloc_hints_t hints;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_UDF);
    
    switch (layer)
    {
        case SAI_UDF_BASE_L2:
            layer = bcmUdfLayerL2Header;
            break;
        case SAI_UDF_BASE_L3:
            layer = bcmUdfLayerL3OuterHeader;
            break;
        case SAI_UDF_BASE_L4:
            layer = bcmUdfLayerL4OuterHeader;
            break;
        default:
            break;
    }

    bcm_udf_alloc_hints_t_init(&hints);
    bcm_udf_t_init(&udf_info);
    hints.flags = BCM_UDF_CREATE_O_UDFHASH;
    if (shared)
    {
        hints.flags |= BCM_UDF_CREATE_O_SHARED_HWID;
        hints.shared_udf = shared;
    }
    udf_info.layer = layer;
    udf_info.start = start * 8;
    udf_info.width = width * 8;
    rv = bcm_udf_create(0, &hints, &udf_info, &udfid);
    BRCM_SAI_API_CHK(SAI_API_UDF, "udf create", rv);
    rv = bcm_udf_pkt_format_add(0, udfid, pktid);
    BRCM_SAI_API_CHK(SAI_API_UDF, "udf pkt format add", rv);
    *udf_id = udfid;
    BRCM_SAI_LOG_UDF(SAI_LOG_LEVEL_DEBUG, "Using udf id: %d\n", udfid);
    
    BRCM_SAI_FUNCTION_EXIT(SAI_API_UDF);
    return rv;
}

sai_status_t
_brcm_sai_create_udf_match(_Out_ sai_object_id_t* udf_match_id,
                           _In_ uint32_t attr_count,
                           _In_ const sai_attribute_t *attr_list)
{
    int i;
    bool no_gre = TRUE;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    bcm_udf_pkt_format_info_t pkt_format;
    bcm_udf_pkt_format_id_t id;

    bcm_udf_pkt_format_info_t_init(&pkt_format);
    pkt_format.l2 = BCM_PKT_FORMAT_L2_ETH_II;
    pkt_format.vlan_tag = BCM_PKT_FORMAT_VLAN_TAG_ANY;
    pkt_format.outer_ip = BCM_PKT_FORMAT_IP_NONE;
    pkt_format.inner_ip = BCM_PKT_FORMAT_IP_NONE;
    pkt_format.tunnel = BCM_PKT_FORMAT_TUNNEL_NONE;
    
    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_UDF_MATCH_ATTR_L2_TYPE:
                pkt_format.ethertype = attr_list[i].value.u16;
                pkt_format.ethertype_mask = _BRCM_SAI_MASK_16;
                if (0x0800 == pkt_format.ethertype)
                {
                    pkt_format.outer_ip = BCM_PKT_FORMAT_IP4;
                }
                else if (0x86dd == pkt_format.ethertype)
                {
                    pkt_format.outer_ip = BCM_PKT_FORMAT_IP6;
                }
                break;
            case SAI_UDF_MATCH_ATTR_L3_TYPE:
                pkt_format.ip_protocol = attr_list[i].value.u8;
                pkt_format.ip_protocol_mask = _BRCM_SAI_MASK_8;
                if (0x04 == pkt_format.ip_protocol || 
                    0x29 == pkt_format.ip_protocol)
                {
                    pkt_format.tunnel = BCM_PKT_FORMAT_TUNNEL_IP_IN_IP;
                    if (no_gre)
                    {
                        pkt_format.inner_ip = BCM_PKT_FORMAT_IP4;
                        if (0x29 == pkt_format.ip_protocol)
                        {
                            pkt_format.inner_ip = BCM_PKT_FORMAT_IP6;
                        }
                    }
                }
                else if (0x2f == pkt_format.ip_protocol)
                {
                    pkt_format.tunnel = BCM_PKT_FORMAT_TUNNEL_GRE;
                }
                break;
            case SAI_UDF_MATCH_ATTR_GRE_TYPE:
                pkt_format.inner_protocol = attr_list[i].value.u16;
                pkt_format.inner_protocol_mask = _BRCM_SAI_MASK_16;
                if (0x800 == pkt_format.inner_protocol)
                {
                    pkt_format.inner_ip = BCM_PKT_FORMAT_IP4;
                }
                else if (0x86dd == pkt_format.inner_protocol)
                {
                    pkt_format.inner_ip = BCM_PKT_FORMAT_IP6;
                }
                no_gre = FALSE;
                break;
            case SAI_UDF_MATCH_ATTR_PRIORITY:
                pkt_format.prio = attr_list[i].value.u8;
                break;
            default: 
                break;
        }
    }

    rv = bcm_udf_pkt_format_create(0, BCM_UDF_PKT_FORMAT_CREATE_O_NONE,
                                   &pkt_format, &id);
    BRCM_SAI_API_CHK(SAI_API_UDF, "udf pkt format create", rv);
    *udf_match_id = BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_UDF_MATCH, id);
    BRCM_SAI_LOG_UDF(SAI_LOG_LEVEL_DEBUG, "Using pkt format id: %d\n", id);
    
    return rv;
}

/*
################################################################################
#                                Functions map                                 #
################################################################################
*/
const sai_udf_api_t udf_apis = {
    brcm_sai_create_udf,
    brcm_sai_remove_udf,
    brcm_sai_set_udf_attribute,
    brcm_sai_get_udf_attribute,
    brcm_sai_create_udf_match,
    brcm_sai_remove_udf_match,
    brcm_sai_set_udf_match_attribute,
    brcm_sai_get_udf_match_attribute,
    brcm_sai_create_udf_group,
    brcm_sai_remove_udf_group,
    brcm_sai_set_udf_group_attribute,
    brcm_sai_get_udf_group_attribute
};
